/*
 * Escape() function.
 *
 * Copyright 1994  Bob Amstadt
 */

#include <string.h>
#include "windef.h"
#include "wingdi.h"
#include "gdi.h"
#include "heap.h"
#include "debugtools.h"

DEFAULT_DEBUG_CHANNEL(driver);

/***********************************************************************
 *            Escape  [GDI.38]
 */
INT16 WINAPI Escape16( HDC16 hdc, INT16 nEscape, INT16 cbInput,
                       SEGPTR lpszInData, SEGPTR lpvOutData )
{
    INT16 ret = 0;
    DC * dc = DC_GetDCPtr( hdc );
    if (dc)
    {
        if (dc->funcs->pEscape)
        {
            if(nEscape == SETABORTPROC) SetAbortProc16(hdc, lpszInData);
            ret = dc->funcs->pEscape( dc, nEscape, cbInput, lpszInData, lpvOutData );
        }
        GDI_ReleaseObj( hdc );
    }
    return ret;
}

/************************************************************************
 *             Escape  [GDI32.@]
 */
INT WINAPI Escape( HDC hdc, INT nEscape, INT cbInput,
		   LPCSTR lpszInData, LPVOID lpvOutData )
{
    SEGPTR	segin,segout;
    INT	ret = 0;
    DC * dc = DC_GetDCPtr( hdc );
    if (!dc) return 0;
    if (!dc->funcs->pEscape) goto done;

    segin	= (SEGPTR)lpszInData;
    segout	= (SEGPTR)lpvOutData;
    switch (nEscape) {
    	/* Escape(hdc,QUERYESCSUPPORT,LPINT,NULL) */
        /* Escape(hdc,CLIP_TO_PATH,LPINT,NULL) */
        /* Escape(hdc,EPSPRINTING,LPINT,NULL) */
    case QUERYESCSUPPORT:
    case CLIP_TO_PATH:
    case EPSPRINTING:
      {
    	LPINT16 x = (LPINT16)SEGPTR_NEW(INT16);
	*x = *(INT*)lpszInData;
	segin = SEGPTR_GET(x);
 	cbInput = sizeof(INT16);
	break;
      }

    	/* Escape(hdc,GETSCALINGFACTOR,NULL,LPPOINT32) */
    	/* Escape(hdc,GETPHYSPAGESIZE,NULL,LPPOINT32) */
    	/* Escape(hdc,GETPRINTINGOFFSET,NULL,LPPOINT32) */

    case GETSCALINGFACTOR:
    case GETPHYSPAGESIZE:
    case GETPRINTINGOFFSET:
	segout = SEGPTR_GET(SEGPTR_NEW(POINT16));
	cbInput = sizeof(POINT16);
	break;

        /* Escape(hdc,EXT_DEVICE_CAPS,LPINT,LPDWORD) */
    case EXT_DEVICE_CAPS:
      {
    	LPINT16 lpIndex = (LPINT16)SEGPTR_NEW(INT16);
	LPDWORD lpCaps = (LPDWORD)SEGPTR_NEW(DWORD);
	*lpIndex = *(INT*)lpszInData;
	
	segin = SEGPTR_GET(lpIndex);
	segout = SEGPTR_GET(lpCaps);
 	cbInput = sizeof(INT16);
	break;
      }

        /* Escape(hdc,SETLINECAP,LPINT,LPINT) */
    case SETLINECAP:
    case SETLINEJOIN:
    case SETMITERLIMIT:
      {
    	LPINT16 new = (LPINT16)SEGPTR_NEW(INT16);
    	LPINT16 old = (LPINT16)SEGPTR_NEW(INT16);
	*new = *(INT*)lpszInData;
	segin = SEGPTR_GET(new);
	segout = SEGPTR_GET(old);
 	cbInput = sizeof(INT16);
	break;
      }
      /* Escape(hdc,GETTECHNOLOGY,NULL,LPSTR); */
    case GETTECHNOLOGY: {
        segout = SEGPTR_GET(SEGPTR_ALLOC(200)); /* enough I hope */
        break;

    }

      /* Escape(hdc,ENABLEPAIRKERNING,LPINT16,LPINT16); */

    case ENABLEPAIRKERNING: {
        LPINT16 enab = SEGPTR_NEW(INT16);
        segout = SEGPTR_GET(SEGPTR_NEW(INT16));
        segin = SEGPTR_GET(enab);
        *enab = *(INT*)lpszInData;
	cbInput = sizeof(INT16);
        break;
    }

      /* Escape(hdc,GETFACENAME,NULL,LPSTR); */

    case GETFACENAME: {
        segout = SEGPTR_GET(SEGPTR_ALLOC(200));
        break;
    }

      /* Escape(hdc,STARTDOC,LPSTR,LPDOCINFOA);
       * lpvOutData is actually a pointer to the DocInfo structure and used as
       * a second input parameter
       */

    case STARTDOC: /* string may not be \0 terminated */
        if(lpszInData) {
	    char *cp = SEGPTR_ALLOC(cbInput);
	    memcpy(cp, lpszInData, cbInput);
	    segin = SEGPTR_GET(cp);
	} else
	    segin = 0;

	if(lpvOutData) {
	    DOCINFO16 *lpsegdoc = SEGPTR_NEW(DOCINFO16);
	    DOCINFOA *lpdoc = lpvOutData;
	    memset(lpsegdoc, 0, sizeof(*lpsegdoc));
	    lpsegdoc->cbSize = sizeof(*lpsegdoc);
	    lpsegdoc->lpszDocName = SEGPTR_GET(SEGPTR_STRDUP(lpdoc->lpszDocName));
	    lpsegdoc->lpszOutput = SEGPTR_GET(SEGPTR_STRDUP(lpdoc->lpszOutput));
	    lpsegdoc->lpszDatatype = SEGPTR_GET(SEGPTR_STRDUP(lpdoc->lpszDatatype));
	    lpsegdoc->fwType = lpdoc->fwType;
	    segout = SEGPTR_GET(lpsegdoc);
	}
	break;

    case SETABORTPROC:
        SetAbortProc(hdc, (ABORTPROC)lpszInData);
	break;

      /* Escape(hdc,END_PATH,PATHINFO,NULL); */
    case END_PATH:
      {
        BYTE *p = SEGPTR_ALLOC(cbInput);
	memcpy(p, lpszInData, cbInput);
	segin = SEGPTR_GET(p);
	break;
      }

    default:
        break;

    }

    ret = dc->funcs->pEscape( dc, nEscape, cbInput, segin, segout );

    switch(nEscape) {
    case QUERYESCSUPPORT:
    	if (ret)
		TRACE("target DC implements Escape %d\n",nEscape);
    	SEGPTR_FREE(MapSL(segin));
	break;

    case SETLINECAP:
    case SETLINEJOIN:
    case SETMITERLIMIT:
        *(LPINT)lpvOutData = *(LPINT16)MapSL(segout);
        SEGPTR_FREE(MapSL(segin));
	SEGPTR_FREE(MapSL(segout));
	break;
    case GETSCALINGFACTOR:
    case GETPRINTINGOFFSET:
    case GETPHYSPAGESIZE: {
    	LPPOINT16 x = MapSL(segout);
	CONV_POINT16TO32(x,(LPPOINT)lpvOutData);
	SEGPTR_FREE(x);
	break;
    }
    case EXT_DEVICE_CAPS:
        *(LPDWORD)lpvOutData = *(LPDWORD)MapSL(segout);
        SEGPTR_FREE(MapSL(segin));
        SEGPTR_FREE(MapSL(segout));
	break;

    case GETTECHNOLOGY: {
        LPSTR x=MapSL(segout);
        strcpy(lpvOutData,x);
        SEGPTR_FREE(x);
	break;
    }
    case ENABLEPAIRKERNING: {
        LPINT16 enab = MapSL(segout);

        *(LPINT)lpvOutData = *enab;
        SEGPTR_FREE(enab);
        SEGPTR_FREE(MapSL(segin));
	break;
    }
    case GETFACENAME: {
        LPSTR x = (LPSTR)MapSL(segout);
        strcpy(lpvOutData,x);
        SEGPTR_FREE(x);
        break;
    }
    case STARTDOC: {
        DOCINFO16 *doc = MapSL(segout);
	SEGPTR_FREE(MapSL(doc->lpszDocName));
	SEGPTR_FREE(MapSL(doc->lpszOutput));
	SEGPTR_FREE(MapSL(doc->lpszDatatype));
	SEGPTR_FREE(doc);
	SEGPTR_FREE(MapSL(segin));
	break;
    }

    case CLIP_TO_PATH:
    case END_PATH:
        SEGPTR_FREE(MapSL(segin));
	break;

    default:
    	break;
    }
 done:
    GDI_ReleaseObj( hdc );
    return ret;
}

/******************************************************************************
 *		ExtEscape	[GDI32.@]
 *
 * PARAMS
 *    hdc         [I] Handle to device context
 *    nEscape     [I] Escape function
 *    cbInput     [I] Number of bytes in input structure
 *    lpszInData  [I] Pointer to input structure
 *    cbOutput    [I] Number of bytes in output structure
 *    lpszOutData [O] Pointer to output structure
 *
 * RETURNS
 *    Success: >0
 *    Not implemented: 0
 *    Failure: <0
 */
INT WINAPI ExtEscape( HDC hdc, INT nEscape, INT cbInput, 
		      LPCSTR lpszInData, INT cbOutput, LPSTR lpszOutData )
{
    char *inBuf, *outBuf;
    INT ret;

    inBuf = SEGPTR_ALLOC(cbInput);
    memcpy(inBuf, lpszInData, cbInput);
    outBuf = cbOutput ? SEGPTR_ALLOC(cbOutput) : NULL;
    ret = Escape16( hdc, nEscape, cbInput, SEGPTR_GET(inBuf),
		    SEGPTR_GET(outBuf) );
    SEGPTR_FREE(inBuf);
    if(outBuf) {
	memcpy(lpszOutData, outBuf, cbOutput);
	SEGPTR_FREE(outBuf);
    }
    return ret;
}

/*******************************************************************
 *      DrawEscape [GDI32.@]
 *
 *
 */
INT WINAPI DrawEscape(HDC hdc, INT nEscape, INT cbInput, LPCSTR lpszInData)
{
    FIXME("DrawEscape, stub\n");
    return 0;
}
