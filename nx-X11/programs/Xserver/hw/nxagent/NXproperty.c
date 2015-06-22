/**************************************************************************/
/*                                                                        */
/* Copyright (c) 2001, 2011 NoMachine, http://www.nomachine.com/.         */
/*                                                                        */
/* NXAGENT, NX protocol compression and NX extensions to this software    */
/* are copyright of NoMachine. Redistribution and use of the present      */
/* software is allowed according to terms specified in the file LICENSE   */
/* which comes in the source distribution.                                */
/*                                                                        */
/* Check http://www.nomachine.com/licensing.html for applicability.       */
/*                                                                        */
/* NX and NoMachine are trademarks of Medialogic S.p.A.                   */
/*                                                                        */
/* All rights reserved.                                                   */
/*                                                                        */
/**************************************************************************/

/* $XFree86: xc/programs/Xserver/dix/property.c,v 3.12 2002/02/19 11:09:22 alanh Exp $ */
/***********************************************************

Copyright 1987, 1998  The Open Group

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of The Open Group shall not be
used in advertising or otherwise to promote the sale, use or other dealings
in this Software without prior written authorization from The Open Group.


Copyright 1987 by Digital Equipment Corporation, Maynard, Massachusetts.

                        All Rights Reserved

Permission to use, copy, modify, and distribute this software and its 
documentation for any purpose and without fee is hereby granted, 
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in 
supporting documentation, and that the name of Digital not be
used in advertising or publicity pertaining to distribution of the
software without specific, written prior permission.  

DIGITAL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
DIGITAL BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.

******************************************************************/
/* $Xorg: property.c,v 1.4 2001/02/09 02:04:40 xorgcvs Exp $ */

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <X11/X.h>
#define NEED_REPLIES
#define NEED_EVENTS
#include <X11/Xproto.h>
#include "windowstr.h"
#include "propertyst.h"
#include "dixstruct.h"
#include "../../dix/dispatch.h"
#include "swaprep.h"
#ifdef XCSECURITY
#define _SECURITY_SERVER
#include <X11/extensions/security.h>
#endif
#ifdef LBX
#include "lbxserve.h"
#include "lbxtags.h"
#endif

#include "Options.h"
#include "Rootless.h"
#include "Client.h"
#include "Windows.h"

#if defined(LBX) || defined(LBX_COMPAT)
#if 0 /* no header in X11 environment, not used in X11 environment */
int fWriteToClient(ClientPtr client, int len, char *buf)
{
    return WriteToClient(client, len, buf);
}
#endif
#endif

extern Atom clientCutProperty;

#ifdef NXAGENT_SERVER
typedef struct
{
  CARD32 state;
  Window icon;
}
nxagentWMStateRec;
#endif

/*****************************************************************
 * Property Stuff
 *
 *    ChangeProperty, DeleteProperty, GetProperties,
 *    ListProperties
 *
 *   Properties below to windows.  A allocate slots each time
 *   a property is added.  No fancy searching done.
 *
 *****************************************************************/

#ifdef notdef
static void
PrintPropertys(WindowPtr pWin)
{
    PropertyPtr pProp;
    register int j;

    pProp = pWin->userProps;
    while (pProp)
    {
        ErrorF(  "%x %x\n", pProp->propertyName, pProp->type);
        ErrorF("property format: %d\n", pProp->format);
        ErrorF("property data: \n");
        for (j=0; j<(pProp->format/8)*pProp->size; j++)
           ErrorF("%c\n", pProp->data[j]);
        pProp = pProp->next;
    }
}
#endif

int
ProcRotateProperties(ClientPtr client)
{
    int     i, j, delta;
    REQUEST(xRotatePropertiesReq);
    WindowPtr pWin;
    register    Atom * atoms;
    PropertyPtr * props;               /* array of pointer */
    PropertyPtr pProp;
    xEvent event;

    REQUEST_FIXED_SIZE(xRotatePropertiesReq, stuff->nAtoms << 2);
    UpdateCurrentTime();
    pWin = (WindowPtr) SecurityLookupWindow(stuff->window, client,
					    SecurityWriteAccess);
    if (!pWin)
        return(BadWindow);
    if (!stuff->nAtoms)
	return(Success);
    atoms = (Atom *) & stuff[1];
    props = (PropertyPtr *)ALLOCATE_LOCAL(stuff->nAtoms * sizeof(PropertyPtr));
    if (!props)
	return(BadAlloc);
    for (i = 0; i < stuff->nAtoms; i++)
    {
#ifdef XCSECURITY
	char action = SecurityCheckPropertyAccess(client, pWin, atoms[i],
				SecurityReadAccess|SecurityWriteAccess);
#endif
        if (!ValidAtom(atoms[i])
#ifdef XCSECURITY
	    || (SecurityErrorOperation == action)
#endif
	   )
        {
            DEALLOCATE_LOCAL(props);
	    client->errorValue = atoms[i];
            return BadAtom;
        }
#ifdef XCSECURITY
	if (SecurityIgnoreOperation == action)
        {
            DEALLOCATE_LOCAL(props);
	    return Success;
	}
#endif
        for (j = i + 1; j < stuff->nAtoms; j++)
            if (atoms[j] == atoms[i])
            {
                DEALLOCATE_LOCAL(props);
                return BadMatch;
            }
        pProp = wUserProps (pWin);
        while (pProp)
        {
            if (pProp->propertyName == atoms[i])
                goto found;
	    pProp = pProp->next;
        }
        DEALLOCATE_LOCAL(props);
        return BadMatch;
found: 
        props[i] = pProp;
    }
    delta = stuff->nPositions;

    /* If the rotation is a complete 360 degrees, then moving the properties
	around and generating PropertyNotify events should be skipped. */

    if ( (stuff->nAtoms != 0) && (abs(delta) % stuff->nAtoms) != 0 ) 
    {
	while (delta < 0)                  /* faster if abs value is small */
            delta += stuff->nAtoms;
    	for (i = 0; i < stuff->nAtoms; i++)
 	{
	    /* Generate a PropertyNotify event for each property whose value
		is changed in the order in which they appear in the request. */
 
 	    event.u.u.type = PropertyNotify;
            event.u.property.window = pWin->drawable.id;
    	    event.u.property.state = PropertyNewValue;
	    event.u.property.atom = props[i]->propertyName;	
	    event.u.property.time = currentTime.milliseconds;
	    DeliverEvents(pWin, &event, 1, (WindowPtr)NULL);
	
            props[i]->propertyName = atoms[(i + delta) % stuff->nAtoms];
	}
    }
    DEALLOCATE_LOCAL(props);
    return Success;
}

int 
ProcChangeProperty(ClientPtr client)
{	      
    WindowPtr pWin;
    char format, mode;
    unsigned long len;
    int sizeInBytes;
    int totalSize;
    int err;
    REQUEST(xChangePropertyReq);

    REQUEST_AT_LEAST_SIZE(xChangePropertyReq);
    UpdateCurrentTime();
    format = stuff->format;
    mode = stuff->mode;
    if ((mode != PropModeReplace) && (mode != PropModeAppend) &&
	(mode != PropModePrepend))
    {
	client->errorValue = mode;
	return BadValue;
    }
    if ((format != 8) && (format != 16) && (format != 32))
    {
	client->errorValue = format;
        return BadValue;
    }
    len = stuff->nUnits;
    if (len > ((0xffffffff - sizeof(xChangePropertyReq)) >> 2))
	return BadLength;
    sizeInBytes = format>>3;
    totalSize = len * sizeInBytes;
    REQUEST_FIXED_SIZE(xChangePropertyReq, totalSize);

#ifdef NXAGENT_CLIPBOARD
    {
       extern WindowPtr nxagentGetClipboardWindow(Atom, WindowPtr);

       pWin = nxagentGetClipboardWindow(stuff->property, NULL);
    }

    if (pWin == NULL)
#endif
    pWin = (WindowPtr)SecurityLookupWindow(stuff->window, client,
					   SecurityWriteAccess);
    if (!pWin)
	return(BadWindow);
    if (!ValidAtom(stuff->property))
    {
	client->errorValue = stuff->property;
	return(BadAtom);
    }
    if (!ValidAtom(stuff->type))
    {
	client->errorValue = stuff->type;
	return(BadAtom);
    }

#ifdef XCSECURITY
    switch (SecurityCheckPropertyAccess(client, pWin, stuff->property,
					SecurityWriteAccess))
    {
	case SecurityErrorOperation:
	    client->errorValue = stuff->property;
	    return BadAtom;
	case SecurityIgnoreOperation:
	    return Success;
    }
#endif

#ifdef NXAGENT_ARTSD
    {
    /* Do not process MCOPGLOBALS property changes,
      they are already set reflecting the server side settings.
      Just return success.
    */
      extern Atom mcop_local_atom;
      if (stuff->property == mcop_local_atom)
        return client->noClientException;
    }
#endif

#ifdef LBX
    err = LbxChangeWindowProperty(client, pWin, stuff->property, stuff->type,
	 (int)format, (int)mode, len, TRUE, (void *)&stuff[1], TRUE, NULL);
#else
    err = ChangeWindowProperty(pWin, stuff->property, stuff->type, (int)format,
			       (int)mode, len, (void *)&stuff[1], TRUE);
#endif
    if (err != Success)
	return err;
    else
    {
      if (nxagentOption(Rootless) == 1)
      {
        nxagentExportProperty(pWin, stuff->property, stuff->type, (int) format,
                                  (int) mode, len, (void *) &stuff[1]);
      }

      nxagentGuessClientHint(client, stuff->property, (char *) &stuff[1]);

      nxagentGuessShadowHint(client, stuff->property);

      #ifdef NX_DEBUG_INPUT
      nxagentGuessDumpInputInfo(client, stuff->property, (char *) &stuff[1]);
      #endif

      return client->noClientException;
    }
}

int
ChangeWindowProperty(WindowPtr pWin, Atom property, Atom type, int format, 
                     int mode, unsigned long len, void * value, 
                     Bool sendevent)
{
#ifdef LBX
    return LbxChangeWindowProperty(NULL, pWin, property, type,
				   format, mode, len, TRUE, value,
				   sendevent, NULL);
#else
    PropertyPtr pProp;
    xEvent event;
    int sizeInBytes;
    int totalSize;
    void * data;
    int copySize;

    sizeInBytes = format>>3;
    totalSize = len * sizeInBytes;

    copySize = nxagentOption(CopyBufferSize);

    if (copySize != COPY_UNLIMITED && property == clientCutProperty)
    {
      if (totalSize > copySize)
      {
        totalSize = copySize;
        totalSize = totalSize - (totalSize % sizeInBytes);
        len = totalSize / sizeInBytes;
      }
    }

    /* first see if property already exists */

    pProp = wUserProps (pWin);
    while (pProp)
    {
	if (pProp->propertyName == property)
	    break;
	pProp = pProp->next;
    }
    if (!pProp)   /* just add to list */
    {
	if (!pWin->optional && !MakeWindowOptional (pWin))
	    return(BadAlloc);
        pProp = (PropertyPtr)xalloc(sizeof(PropertyRec));
	if (!pProp)
	    return(BadAlloc);
        data = (void *)xalloc(totalSize);
	if (!data && len)
	{
	    xfree(pProp);
	    return(BadAlloc);
	}
        pProp->propertyName = property;
        pProp->type = type;
        pProp->format = format;
        pProp->data = data;
	if (len)
	    memmove((char *)data, (char *)value, totalSize);
	pProp->size = len;
        pProp->next = pWin->optional->userProps;
        pWin->optional->userProps = pProp;
    }
    else
    {
	/* To append or prepend to a property the request format and type
		must match those of the already defined property.  The
		existing format and type are irrelevant when using the mode
		"PropModeReplace" since they will be written over. */

        if ((format != pProp->format) && (mode != PropModeReplace))
	    return(BadMatch);
        if ((pProp->type != type) && (mode != PropModeReplace))
            return(BadMatch);
        if (mode == PropModeReplace)
        {
	    if (totalSize != pProp->size * (pProp->format >> 3))
	    {
	    	data = (void *)xrealloc(pProp->data, totalSize);
	    	if (!data && len)
		    return(BadAlloc);
            	pProp->data = data;
	    }
	    if (len)
		memmove((char *)pProp->data, (char *)value, totalSize);
	    pProp->size = len;
    	    pProp->type = type;
	    pProp->format = format;
	}
	else if (len == 0)
	{
	    /* do nothing */
	}
        else if (mode == PropModeAppend)
        {
	    data = (void *)xrealloc(pProp->data,
				     sizeInBytes * (len + pProp->size));
	    if (!data)
		return(BadAlloc);
            pProp->data = data;
	    memmove(&((char *)data)[pProp->size * sizeInBytes], 
		    (char *)value,
		  totalSize);
            pProp->size += len;
	}
        else if (mode == PropModePrepend)
        {
            data = (void *)xalloc(sizeInBytes * (len + pProp->size));
	    if (!data)
		return(BadAlloc);
	    memmove(&((char *)data)[totalSize], (char *)pProp->data, 
		  (int)(pProp->size * sizeInBytes));
            memmove((char *)data, (char *)value, totalSize);
	    xfree(pProp->data);
            pProp->data = data;
            pProp->size += len;
	}
    }
    if (sendevent)
    {
	event.u.u.type = PropertyNotify;
	event.u.property.window = pWin->drawable.id;
	event.u.property.state = PropertyNewValue;
	event.u.property.atom = pProp->propertyName;
	event.u.property.time = currentTime.milliseconds;
	DeliverEvents(pWin, &event, 1, (WindowPtr)NULL);
    }
    return(Success);
#endif
}

int
DeleteProperty(WindowPtr pWin, Atom propName)
{
    PropertyPtr pProp, prevProp;
    xEvent event;

    if (!(pProp = wUserProps (pWin)))
	return(Success);
    prevProp = (PropertyPtr)NULL;
    while (pProp)
    {
	if (pProp->propertyName == propName)
	    break;
        prevProp = pProp;
	pProp = pProp->next;
    }
    if (pProp) 
    {		    
        if (prevProp == (PropertyPtr)NULL)      /* takes care of head */
        {
            if (!(pWin->optional->userProps = pProp->next))
		CheckWindowOptionalNeed (pWin);
        }
	else
        {
            prevProp->next = pProp->next;
        }
#ifdef LBX
	if (pProp->tag_id)
	    TagDeleteTag(pProp->tag_id);
#endif
	event.u.u.type = PropertyNotify;
	event.u.property.window = pWin->drawable.id;
	event.u.property.state = PropertyDelete;
        event.u.property.atom = pProp->propertyName;
	event.u.property.time = currentTime.milliseconds;
	DeliverEvents(pWin, &event, 1, (WindowPtr)NULL);
	xfree(pProp->data);
        xfree(pProp);
    }
    return(Success);
}

void
DeleteAllWindowProperties(WindowPtr pWin)
{
    PropertyPtr pProp, pNextProp;
    xEvent event;

    pProp = wUserProps (pWin);
    while (pProp)
    {
#ifdef LBX
	if (pProp->tag_id)
	    TagDeleteTag(pProp->tag_id);
#endif
	event.u.u.type = PropertyNotify;
	event.u.property.window = pWin->drawable.id;
	event.u.property.state = PropertyDelete;
	event.u.property.atom = pProp->propertyName;
	event.u.property.time = currentTime.milliseconds;
	DeliverEvents(pWin, &event, 1, (WindowPtr)NULL);
	pNextProp = pProp->next;
        xfree(pProp->data);
        xfree(pProp);
	pProp = pNextProp;
    }
}

static int
NullPropertyReply(
    ClientPtr client,
    ATOM propertyType,
    int format,
    xGetPropertyReply *reply)
{
    reply->nItems = 0;
    reply->length = 0;
    reply->bytesAfter = 0;
    reply->propertyType = propertyType;
    reply->format = format;
    WriteReplyToClient(client, sizeof(xGenericReply), reply);
    return(client->noClientException);
}

/*****************
 * GetProperty
 *    If type Any is specified, returns the property from the specified
 *    window regardless of its type.  If a type is specified, returns the
 *    property only if its type equals the specified type.
 *    If delete is True and a property is returned, the property is also
 *    deleted from the window and a PropertyNotify event is generated on the
 *    window.
 *****************/

int
ProcGetProperty(ClientPtr client)
{
    #ifdef NXAGENT_SERVER
    nxagentWMStateRec wmState;
    nxagentWMStateRec *wmsP = &wmState;
    #endif

    PropertyPtr pProp, prevProp;
    unsigned long n, len, ind;
    WindowPtr pWin;
    xGetPropertyReply reply;
    REQUEST(xGetPropertyReq);

    REQUEST_SIZE_MATCH(xGetPropertyReq);

    if (stuff->delete)
	UpdateCurrentTime();
    pWin = (WindowPtr)SecurityLookupWindow(stuff->window, client,
					   SecurityReadAccess);
    if (!pWin)
	return BadWindow;

    if (!ValidAtom(stuff->property))
    {
	client->errorValue = stuff->property;
	return(BadAtom);
    }
    if ((stuff->delete != xTrue) && (stuff->delete != xFalse))
    {
	client->errorValue = stuff->delete;
	return(BadValue);
    }
    if ((stuff->type != AnyPropertyType) && !ValidAtom(stuff->type))
    {
	client->errorValue = stuff->type;
	return(BadAtom);
    }

    pProp = wUserProps (pWin);
    prevProp = (PropertyPtr)NULL;
    while (pProp)
    {
	if (pProp->propertyName == stuff->property) 
	    break;
	prevProp = pProp;
	pProp = pProp->next;
    }

    memset(&reply, 0, sizeof(xGetPropertyReply));
    reply.type = X_Reply;
    reply.sequenceNumber = client->sequence;

    #ifdef NXAGENT_SERVER

    /*
     * Creating a reply for WM_STATE property if it doesn't exist.
     * This is intended to allow drag & drop work in JAva 1.6 when
     * the agent is connected to NXWin in multiwindow mode.
     */

    if (nxagentOption(Rootless) &&
            nxagentWindowTopLevel(pWin) &&
                (!pProp) &&
                    strcmp(NameForAtom(stuff->property), "WM_STATE") == 0)
    {
      wmState.state = 1;
      wmState.icon = None;

      if (ChangeWindowProperty(pWin, stuff->property, stuff->property, 32, 0, 2, &wmState, 1) == Success)
      {
        nxagentExportProperty(pWin, stuff->property, stuff->property, 32, 0, 2, &wmState);
      }

      n = 8;
      ind = stuff->longOffset << 2;        

      if (n < ind)
      {
        client->errorValue = stuff->longOffset;
        return BadValue;
      }

      len = min(n - ind, 4 * stuff->longLength);

      reply.bytesAfter = n - (ind + len);
      reply.length = (len + 3) >> 2;

      reply.format = 32;
      reply.nItems = len / 4;
      reply.propertyType = stuff->property;

      WriteReplyToClient(client, sizeof(xGenericReply), &reply);

      if (len)
      {
        client->pSwapReplyFunc = (ReplySwapPtr)CopySwap32Write;

        WriteSwappedDataToClient(client, len, (char *)wmsP + ind);
      }

      return(client->noClientException);
    }
    #endif

    if (!pProp) 
	return NullPropertyReply(client, None, 0, &reply);

#ifdef XCSECURITY
    {
	Mask access_mode = SecurityReadAccess;

	if (stuff->delete)
	    access_mode |= SecurityDestroyAccess;
	switch(SecurityCheckPropertyAccess(client, pWin, stuff->property,
					   access_mode))
	{
	    case SecurityErrorOperation:
		client->errorValue = stuff->property;
		return BadAtom;;
	    case SecurityIgnoreOperation:
		return NullPropertyReply(client, pProp->type, pProp->format,
					 &reply);
	}
    }
#endif
    /* If the request type and actual type don't match. Return the
    property information, but not the data. */

    if (((stuff->type != pProp->type) &&
	 (stuff->type != AnyPropertyType))
       )
    {
	reply.bytesAfter = pProp->size;
	reply.format = pProp->format;
	reply.length = 0;
	reply.nItems = 0;
	reply.propertyType = pProp->type;
	WriteReplyToClient(client, sizeof(xGenericReply), &reply);
	return(Success);
    }
#ifdef LBX
    /* make sure we have the current value */                       
    if (pProp->tag_id && pProp->owner_pid) {
	LbxStallPropRequest(client, pProp);
	return client->noClientException;
    }                                              
#endif

/*
 *  Return type, format, value to client
 */
    n = (pProp->format/8) * pProp->size; /* size (bytes) of prop */
    ind = stuff->longOffset << 2;        

   /* If longOffset is invalid such that it causes "len" to
	    be negative, it's a value error. */

    if (n < ind)
    {
	client->errorValue = stuff->longOffset;
	return BadValue;
    }

    len = min(n - ind, 4 * stuff->longLength);

    reply.bytesAfter = n - (ind + len);
    reply.format = pProp->format;
    reply.length = (len + 3) >> 2;
    reply.nItems = len / (pProp->format / 8 );
    reply.propertyType = pProp->type;

    if (stuff->delete && (reply.bytesAfter == 0))
    { /* send the event */
	xEvent event;

	event.u.u.type = PropertyNotify;
	event.u.property.window = pWin->drawable.id;
	event.u.property.state = PropertyDelete;
	event.u.property.atom = pProp->propertyName;
	event.u.property.time = currentTime.milliseconds;
	DeliverEvents(pWin, &event, 1, (WindowPtr)NULL);
    }

    WriteReplyToClient(client, sizeof(xGenericReply), &reply);
    if (len)
    {
	switch (reply.format) {
	case 32: client->pSwapReplyFunc = (ReplySwapPtr)CopySwap32Write; break;
	case 16: client->pSwapReplyFunc = (ReplySwapPtr)CopySwap16Write; break;
	default: client->pSwapReplyFunc = (ReplySwapPtr)WriteToClient; break;
	}
	WriteSwappedDataToClient(client, len,
				 (char *)pProp->data + ind);
    }

    if (stuff->delete && (reply.bytesAfter == 0))
    { /* delete the Property */
#ifdef LBX
	if (pProp->tag_id)
	    TagDeleteTag(pProp->tag_id);
#endif
	if (prevProp == (PropertyPtr)NULL) /* takes care of head */
	{
	    if (!(pWin->optional->userProps = pProp->next))
		CheckWindowOptionalNeed (pWin);
	}
	else
	    prevProp->next = pProp->next;
	xfree(pProp->data);
	xfree(pProp);
    }
    return(client->noClientException);
}

#ifdef NXAGENT_CLIPBOARD
/* GetWindowProperty clipboard use only */
int
GetWindowProperty(pWin, property, longOffset, longLength, delete,
            type, actualType, format, nItems, bytesAfter, propData )
    WindowPtr	        pWin;
    Atom		property;
    long			longOffset;
    long			longLength;
    Bool			delete;
    Atom		type;
    Atom		*actualType;
    int			*format;
    unsigned long	*nItems;
    unsigned long	*bytesAfter;
    unsigned char	**propData;
{
    PropertyPtr pProp, prevProp;
    unsigned long n, len, ind;

    if (!pWin)
	return BadWindow;


    if (!ValidAtom(property))
    {
	return(BadAtom);
    }
    if ((type != AnyPropertyType) && !ValidAtom(type))
    {
	return(BadAtom);
    }

    pProp = wUserProps (pWin);
    prevProp = (PropertyPtr)NULL;

    while (pProp)
    {
	if (pProp->propertyName == property)
	    break;
	prevProp = pProp;
	pProp = pProp->next;
    }


    if (!pProp)
	return (BadAtom);

    /* If the request type and actual type don't match. Return the
    property information, but not the data. */

    if (((type != pProp->type) &&
	 (type != AnyPropertyType))
       )
    {
	*bytesAfter = pProp->size;
	*format = pProp->format;
	*nItems = 0;
	*actualType = pProp->type;
	return(Success);
    }

/*
 *  Return type, format, value to client
 */
    n = (pProp->format/8) * pProp->size; /* size (bytes) of prop */
    ind = longOffset << 2;

   /* If longOffset is invalid such that it causes "len" to
	    be negative, it's a value error. */

    if (n < ind)
    {
	return BadValue;
    }

    len = min(n - ind, 4 * longLength);

    *bytesAfter = n - (ind + len);
    *format = pProp->format;
    *nItems = len / (pProp->format / 8 );
    *actualType = pProp->type;

    if (delete && (*bytesAfter == 0))
    { /* send the event */
	xEvent event;

	event.u.u.type = PropertyNotify;
	event.u.property.window = pWin->drawable.id;
	event.u.property.state = PropertyDelete;
	event.u.property.atom = pProp->propertyName;
	event.u.property.time = currentTime.milliseconds;
	DeliverEvents(pWin, &event, 1, (WindowPtr)NULL);
    }

    if (len)
    {
	 *propData = (unsigned char *)(pProp->data) + ind;
    }

    if (delete && (*bytesAfter == 0))
    { /* delete the Property */
#ifdef LBX
	if (pProp->tag_id)
	    TagDeleteTag(pProp->tag_id);
#endif
	if (prevProp == (PropertyPtr)NULL) /* takes care of head */
	{
	    if (!(pWin->optional->userProps = pProp->next))
		CheckWindowOptionalNeed (pWin);
	}
	else
	    prevProp->next = pProp->next;
	xfree(pProp->data);
	xfree(pProp);
    }
    return(Success);
}
#endif

int
ProcListProperties(ClientPtr client)
{
    Atom *pAtoms = NULL, *temppAtoms;
    xListPropertiesReply xlpr;
    int	numProps = 0;
    WindowPtr pWin;
    PropertyPtr pProp;
    REQUEST(xResourceReq);

    REQUEST_SIZE_MATCH(xResourceReq);
    pWin = (WindowPtr)SecurityLookupWindow(stuff->id, client,
					   SecurityReadAccess);
    if (!pWin)
        return(BadWindow);

    pProp = wUserProps (pWin);
    while (pProp)
    {        
        pProp = pProp->next;
	numProps++;
    }
    if (numProps)
        if(!(pAtoms = (Atom *)ALLOCATE_LOCAL(numProps * sizeof(Atom))))
            return(BadAlloc);

    xlpr.type = X_Reply;
    xlpr.nProperties = numProps;
    xlpr.length = (numProps * sizeof(Atom)) >> 2;
    xlpr.sequenceNumber = client->sequence;
    pProp = wUserProps (pWin);
    temppAtoms = pAtoms;
    while (pProp)
    {
	*temppAtoms++ = pProp->propertyName;
	pProp = pProp->next;
    }
    WriteReplyToClient(client, sizeof(xGenericReply), &xlpr);
    if (numProps)
    {
        client->pSwapReplyFunc = (ReplySwapPtr)Swap32Write;
        WriteSwappedDataToClient(client, numProps * sizeof(Atom), pAtoms);
        DEALLOCATE_LOCAL(pAtoms);
    }
    return(client->noClientException);
}

int 
ProcDeleteProperty(register ClientPtr client)
{
    WindowPtr pWin;
    REQUEST(xDeletePropertyReq);
    int result;
              
    REQUEST_SIZE_MATCH(xDeletePropertyReq);
    UpdateCurrentTime();
    pWin = (WindowPtr)SecurityLookupWindow(stuff->window, client,
					   SecurityWriteAccess);
    if (!pWin)
        return(BadWindow);
    if (!ValidAtom(stuff->property))
    {
	client->errorValue = stuff->property;
	return (BadAtom);
    }

#ifdef XCSECURITY
    switch(SecurityCheckPropertyAccess(client, pWin, stuff->property,
				       SecurityDestroyAccess))
    {
	case SecurityErrorOperation:
	    client->errorValue = stuff->property;
	    return BadAtom;;
	case SecurityIgnoreOperation:
	    return Success;
    }
#endif

    result = DeleteProperty(pWin, stuff->property);
    if (client->noClientException != Success)
	return(client->noClientException);
    else
	return(result);
}

