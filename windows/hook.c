/*
 * Windows hook functions
 *
 * Copyright 1994, 1995 Alexandre Julliard
 *                 1996 Andrew Lewycky
 *
 * Based on investigations by Alex Korobka
 */

/*
 * Warning!
 * A HHOOK is a 32-bit handle for compatibility with Windows 3.0 where it was
 * a pointer to the next function. Now it is in fact composed of a USER heap
 * handle in the low 16 bits and of a HOOK_MAGIC value in the high 16 bits.
 */

#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"
#include "wine/winuser16.h"
#include "wine/winbase16.h"
#include "callback.h"
#include "hook.h"
#include "win.h"
#include "queue.h"
#include "user.h"
#include "heap.h"
#include "struct32.h"
#include "winproc.h"
#include "debugtools.h"

DEFAULT_DEBUG_CHANNEL(hook);

#include "pshpack1.h"

  /* Hook data (pointed to by a HHOOK) */
typedef struct
{
    HANDLE16   next;               /* 00 Next hook in chain */
    HOOKPROC   proc;               /* 02 Hook procedure (original) */
    INT16      id;                 /* 06 Hook id (WH_xxx) */
    HQUEUE16   ownerQueue;         /* 08 Owner queue (0 for system hook) */
    HMODULE16  ownerModule;        /* 0a Owner module */
    WORD       flags;              /* 0c flags */
    HOOKPROC   thunk;              /* 0e Hook procedure (CallTo16 thunk) */
} HOOKDATA;

#include "poppack.h"

#define HOOK_MAGIC  ((int)'H' | (int)'K' << 8)  /* 'HK' */

  /* This should probably reside in USER heap */
static HANDLE16 HOOK_systemHooks[WH_NB_HOOKS] = { 0, };

typedef VOID (*HOOK_MapFunc)(INT, INT, WPARAM *, LPARAM *);
typedef VOID (*HOOK_UnMapFunc)(INT, INT, WPARAM, LPARAM, WPARAM,
			       LPARAM);

/***********************************************************************
 *           HOOK_Map16To32Common
 */
static void HOOK_Map16To32Common(INT id, INT code, WPARAM *pwParam,
				 LPARAM *plParam, BOOL bA )
{

   switch( id )
   {
	case WH_MSGFILTER:
	case WH_SYSMSGFILTER: 
	case WH_GETMESSAGE: 
	case WH_JOURNALRECORD:
        {
            LPMSG16 lpmsg16 = MapSL(*plParam);
            LPMSG lpmsg32 = HeapAlloc( GetProcessHeap(), 0, sizeof(*lpmsg32) );
	
            STRUCT32_MSG16to32( lpmsg16, lpmsg32 );
            *plParam = (LPARAM)lpmsg32;
	    break;
        } 

	case WH_JOURNALPLAYBACK:
        {
            LPEVENTMSG16 lpem16 = MapSL(*plParam);
            LPEVENTMSG lpem32 = HeapAlloc( GetProcessHeap(), 0, sizeof(*lpem32) );

            lpem32->message = lpem16->message;
            lpem32->paramL = lpem16->paramL;
            lpem32->paramH = lpem16->paramH;
            lpem32->time = lpem16->time;
            lpem32->hwnd = 0;	/* FIXME */

            *plParam = (LPARAM)lpem32;
	    break;
        } 

	case WH_CALLWNDPROC:
	{
	    LPCWPSTRUCT16   lpcwp16 = MapSL(*plParam);
	    LPCWPSTRUCT   lpcwp32 = HeapAlloc( GetProcessHeap(), 0, sizeof(*lpcwp32) );
	    
	    lpcwp32->hwnd = WIN_Handle32(lpcwp16->hwnd);
	    lpcwp32->lParam = lpcwp16->lParam;
	    
            if (bA) WINPROC_MapMsg16To32A( lpcwp32->hwnd, lpcwp16->message, lpcwp16->wParam,
                                           &lpcwp32->message, &lpcwp32->wParam,
                                           &lpcwp32->lParam );
            else WINPROC_MapMsg16To32W( lpcwp32->hwnd,lpcwp16->message, lpcwp16->wParam,
                                        &lpcwp32->message, &lpcwp32->wParam,
                                        &lpcwp32->lParam );
	    *plParam = (LPARAM)lpcwp32;
	    break;
	}

	case WH_CBT:
	  switch (code)
	  {
	    case HCBT_CREATEWND:
	    {
		LPCBT_CREATEWND16  lpcbtcw16 = MapSL(*plParam);
		LPCREATESTRUCT16   lpcs16 = MapSL((SEGPTR)lpcbtcw16->lpcs);
		LPCBT_CREATEWNDA lpcbtcw32 = HeapAlloc( GetProcessHeap(), 0,
							  sizeof(*lpcbtcw32) );
		lpcbtcw32->lpcs = HeapAlloc( GetProcessHeap(), 0,
					     sizeof(*lpcbtcw32->lpcs) );

		STRUCT32_CREATESTRUCT16to32A( lpcs16,
					     (LPCREATESTRUCTA)lpcbtcw32->lpcs );

		if (HIWORD(lpcs16->lpszName))
		    lpcbtcw32->lpcs->lpszName = 
			(bA) ? MapSL(lpcs16->lpszName)
			     : HEAP_strdupAtoW( GetProcessHeap(), 0,
                                                MapSL(lpcs16->lpszName) );
		else
		    lpcbtcw32->lpcs->lpszName = (LPCSTR)lpcs16->lpszName;

		if (HIWORD(lpcs16->lpszClass))
		    lpcbtcw32->lpcs->lpszClass =
			(bA) ? MapSL(lpcs16->lpszClass)
			     : HEAP_strdupAtoW( GetProcessHeap(), 0,
                                                MapSL(lpcs16->lpszClass) );
		else
		    lpcbtcw32->lpcs->lpszClass = (LPCSTR)lpcs16->lpszClass;

		lpcbtcw32->hwndInsertAfter = WIN_Handle32( lpcbtcw16->hwndInsertAfter );

		*plParam = (LPARAM)lpcbtcw32;
		break;
	    } 
	    case HCBT_ACTIVATE:
            {
                LPCBTACTIVATESTRUCT16 lpcas16 = MapSL(*plParam);
                LPCBTACTIVATESTRUCT lpcas32 = HeapAlloc( GetProcessHeap(), 0,
                                                           sizeof(*lpcas32) );
                lpcas32->fMouse = lpcas16->fMouse;
                lpcas32->hWndActive = WIN_Handle32(lpcas16->hWndActive);
                *plParam = (LPARAM)lpcas32;
                break;
            }
            case HCBT_CLICKSKIPPED:
            {
                LPMOUSEHOOKSTRUCT16 lpms16 = MapSL(*plParam);
                LPMOUSEHOOKSTRUCT lpms32 = HeapAlloc( GetProcessHeap(), 0,
                                                        sizeof(*lpms32) );

                CONV_POINT16TO32( &lpms16->pt, &lpms32->pt );

                /* wHitTestCode may be negative, so convince compiler to do
                   correct sign extension. Yay. :| */
                lpms32->wHitTestCode = (INT)((INT16)lpms16->wHitTestCode);

                lpms32->dwExtraInfo = lpms16->dwExtraInfo;
                lpms32->hwnd = WIN_Handle32( lpms16->hwnd );
                *plParam = (LPARAM)lpms32;
                break;
            }
            case HCBT_MOVESIZE:
            {
                LPRECT16 lprect16 = MapSL(*plParam);
                LPRECT lprect32 = HeapAlloc( GetProcessHeap(), 0,
                                               sizeof(*lprect32) );

                CONV_RECT16TO32( lprect16, lprect32 );
                *plParam = (LPARAM)lprect32;
                break;
            }
	  } 
	  break;

	case WH_MOUSE:
        {
            LPMOUSEHOOKSTRUCT16 lpms16 = MapSL(*plParam);
            LPMOUSEHOOKSTRUCT lpms32 = HeapAlloc( GetProcessHeap(), 0,
                                                    sizeof(*lpms32) );

            CONV_POINT16TO32( &lpms16->pt, &lpms32->pt );

            /* wHitTestCode may be negative, so convince compiler to do
               correct sign extension. Yay. :| */
            lpms32->wHitTestCode = (INT)((INT16)lpms16->wHitTestCode);
            lpms32->dwExtraInfo = lpms16->dwExtraInfo;
            lpms32->hwnd = WIN_Handle32(lpms16->hwnd);
            *plParam = (LPARAM)lpms32;
	    break;
        } 

	case WH_DEBUG:
        {
            LPDEBUGHOOKINFO16 lpdh16 = MapSL(*plParam);
            LPDEBUGHOOKINFO lpdh32 = HeapAlloc( GetProcessHeap(), 0,
                                                  sizeof(*lpdh32) );

            lpdh32->idThread = 0;               /* FIXME */
            lpdh32->idThreadInstaller = 0;	/* FIXME */
            lpdh32->lParam = lpdh16->lParam;	/* FIXME Check for sign ext */
            lpdh32->wParam = lpdh16->wParam;
            lpdh32->code = lpdh16->code;
	  
            /* do sign extension if it was WH_MSGFILTER */
            if (*pwParam == 0xffff) *pwParam = WH_MSGFILTER;

            *plParam = (LPARAM)lpdh32;
	    break;
        }

	case WH_SHELL:
	case WH_KEYBOARD:
	    break;

	case WH_HARDWARE: 
	case WH_FOREGROUNDIDLE: 
	case WH_CALLWNDPROCRET:
	    FIXME("\t[%i] 16to32 translation unimplemented\n", id);
    }
}


/***********************************************************************
 *           HOOK_Map16To32A
 */
static void HOOK_Map16To32A(INT id, INT code, WPARAM *pwParam,
			    LPARAM *plParam)
{
    HOOK_Map16To32Common( id, code, pwParam, plParam, TRUE );
}


/***********************************************************************
 *           HOOK_Map16To32W
 */
static void HOOK_Map16To32W(INT id, INT code, WPARAM *pwParam,
			    LPARAM *plParam)
{
    HOOK_Map16To32Common( id, code, pwParam, plParam, FALSE );
}


/***********************************************************************
 *           HOOK_UnMap16To32Common
 */
static void HOOK_UnMap16To32Common(INT id, INT code, WPARAM wParamOrig,
				   LPARAM lParamOrig, WPARAM wParam,
				   LPARAM lParam, BOOL bA)
{
    switch (id)
    {
	case WH_MSGFILTER:
	case WH_SYSMSGFILTER:
	case WH_JOURNALRECORD:
	case WH_JOURNALPLAYBACK:
      
	    HeapFree( GetProcessHeap(), 0, (LPVOID)lParam );
	    break;

	case WH_CALLWNDPROC:
	{
            LPCWPSTRUCT   lpcwp32 = (LPCWPSTRUCT)lParam;
            if (bA) WINPROC_UnmapMsg16To32A( lpcwp32->hwnd,lpcwp32->message, lpcwp32->wParam,
                                             lpcwp32->lParam, 0 );
            else WINPROC_UnmapMsg16To32W( lpcwp32->hwnd,lpcwp32->message, lpcwp32->wParam,
                                          lpcwp32->lParam, 0 );
	    HeapFree( GetProcessHeap(), 0, lpcwp32 );
            break;
	}

	case WH_GETMESSAGE:
        {
	    LPMSG16 lpmsg16 = MapSL(lParamOrig);
	    STRUCT32_MSG32to16( (LPMSG)lParam, lpmsg16 );
	    HeapFree( GetProcessHeap(), 0, (LPVOID)lParam );
	    break;
        }

        case WH_MOUSE:
        case WH_DEBUG:

	    HeapFree( GetProcessHeap(), 0, (LPVOID)lParam );
	    break;

        case WH_CBT:
	    switch (code)
  	    {
	      case HCBT_CREATEWND:
	      {
		LPCBT_CREATEWNDA lpcbtcw32 = (LPCBT_CREATEWNDA)lParam;
		LPCBT_CREATEWND16  lpcbtcw16 = MapSL(lParamOrig);

		if( !bA )
		{
		   if (HIWORD(lpcbtcw32->lpcs->lpszName))
                       HeapFree( GetProcessHeap(), 0, (LPWSTR)lpcbtcw32->lpcs->lpszName );
		   if (HIWORD(lpcbtcw32->lpcs->lpszClass))
                       HeapFree( GetProcessHeap(), 0, (LPWSTR)lpcbtcw32->lpcs->lpszClass );
		}

		lpcbtcw16->hwndInsertAfter = WIN_Handle16( lpcbtcw32->hwndInsertAfter );

		HeapFree( GetProcessHeap(), 0, lpcbtcw32->lpcs );
	      } /* fall through */

	      case HCBT_ACTIVATE:
	      case HCBT_CLICKSKIPPED:
	      case HCBT_MOVESIZE:

	        HeapFree( GetProcessHeap(), 0, (LPVOID)lParam);
	        break;
	    }
  	    break;

        case WH_SHELL:
        case WH_KEYBOARD:
	    break;

        case WH_HARDWARE:
	case WH_FOREGROUNDIDLE:
	case WH_CALLWNDPROCRET:
	    FIXME("\t[%i] skipping unmap\n", id);
  	    break;
    }
}


/***********************************************************************
 *           HOOK_UnMap16To32A
 */
static void HOOK_UnMap16To32A(INT id, INT code, WPARAM wParamOrig,
			      LPARAM lParamOrig, WPARAM wParam,
			      LPARAM lParam)
{
    HOOK_UnMap16To32Common( id, code, wParamOrig, lParamOrig, wParam,
			    lParam, TRUE );
}


/***********************************************************************
 *           HOOK_UnMap16To32W
 */
static void HOOK_UnMap16To32W(INT id, INT code, WPARAM wParamOrig,
			      LPARAM lParamOrig, WPARAM wParam,
			      LPARAM lParam)
{
    HOOK_UnMap16To32Common( id, code, wParamOrig, lParamOrig, wParam, 
			    lParam, FALSE );
}


/***********************************************************************
 *           HOOK_Map32To16Common
 */
static void HOOK_Map32To16Common(INT id, INT code, WPARAM *pwParam,
				 LPARAM *plParam, BOOL bA)
{
    switch (id)
    {
      case WH_MSGFILTER:
      case WH_SYSMSGFILTER:
      case WH_GETMESSAGE:
      case WH_JOURNALRECORD:
      {
	  LPMSG lpmsg32 = (LPMSG)*plParam;
	  LPMSG16 lpmsg16 = SEGPTR_NEW( MSG16 );

	  STRUCT32_MSG32to16( lpmsg32, lpmsg16 );

	  *plParam = (LPARAM)SEGPTR_GET( lpmsg16 );
	  break;
      }

      case WH_JOURNALPLAYBACK:
      {
	  LPEVENTMSG lpem32 = (LPEVENTMSG)*plParam;
	  LPEVENTMSG16 lpem16 = SEGPTR_NEW( EVENTMSG16 );

	  lpem16->message = lpem32->message;
	  lpem16->paramL  = lpem32->paramL;
	  lpem16->paramH  = lpem32->paramH;
	  lpem16->time    = lpem32->time;

	  *plParam = (LPARAM)SEGPTR_GET( lpem16 );
	  break;
      }

      case WH_CALLWNDPROC:
      {
          LPCWPSTRUCT   lpcwp32 = (LPCWPSTRUCT)*plParam;
	  LPCWPSTRUCT16   lpcwp16 = SEGPTR_NEW( CWPSTRUCT16 );

          lpcwp16->hwnd = WIN_Handle16(lpcwp32->hwnd);
          lpcwp16->lParam = lpcwp32->lParam;

          if (bA) WINPROC_MapMsg32ATo16( lpcwp32->hwnd, lpcwp32->message,
                                         lpcwp32->wParam, &lpcwp16->message,
                                         &lpcwp16->wParam, &lpcwp16->lParam );
          else WINPROC_MapMsg32WTo16( lpcwp32->hwnd, lpcwp32->message,
                                      lpcwp32->wParam, &lpcwp16->message,
                                      &lpcwp16->wParam, &lpcwp16->lParam );
	  *plParam = (LPARAM)SEGPTR_GET( lpcwp16 );
          break;
      }

      case WH_CBT:
	switch (code)
	{
	  case HCBT_ACTIVATE:
	  {
	      LPCBTACTIVATESTRUCT lpcas32 = (LPCBTACTIVATESTRUCT)*plParam;
	      LPCBTACTIVATESTRUCT16 lpcas16 =SEGPTR_NEW( CBTACTIVATESTRUCT16 );

	      lpcas16->fMouse     = lpcas32->fMouse;
	      lpcas16->hWndActive = WIN_Handle16( lpcas32->hWndActive );

	      *plParam = (LPARAM)SEGPTR_GET( lpcas16 );
	      break;
	  }
	      
	  case HCBT_CLICKSKIPPED:
	  {
	      LPMOUSEHOOKSTRUCT lpms32 = (LPMOUSEHOOKSTRUCT)*plParam;
	      LPMOUSEHOOKSTRUCT16 lpms16 = SEGPTR_NEW( MOUSEHOOKSTRUCT16 );

	      CONV_POINT32TO16( &lpms32->pt, &lpms16->pt );

	      lpms16->hwnd         = WIN_Handle16( lpms32->hwnd );
	      lpms16->wHitTestCode = lpms32->wHitTestCode;
	      lpms16->dwExtraInfo  = lpms32->dwExtraInfo;

	      *plParam = (LPARAM)SEGPTR_GET( lpms16 );
	      break;
	  }

	  case HCBT_MOVESIZE:
	  {
	      LPRECT lprect32 = (LPRECT)*plParam;
	      LPRECT16 lprect16 = SEGPTR_NEW( RECT16 );

	      CONV_RECT32TO16( lprect32, lprect16 );

	      *plParam = (LPARAM)SEGPTR_GET( lprect16 );
	      break;
	  }
	}
	break;

      case WH_MOUSE:
      {
	  LPMOUSEHOOKSTRUCT lpms32 = (LPMOUSEHOOKSTRUCT)*plParam;
	  LPMOUSEHOOKSTRUCT16 lpms16 = SEGPTR_NEW( MOUSEHOOKSTRUCT16 );

	  CONV_POINT32TO16( &lpms32->pt, &lpms16->pt );

	  lpms16->hwnd = WIN_Handle16( lpms32->hwnd );
	  lpms16->wHitTestCode = lpms32->wHitTestCode;
	  lpms16->dwExtraInfo = lpms32->dwExtraInfo;

	  *plParam = (LPARAM)SEGPTR_GET( lpms16 );
	  break;
      }

      case WH_DEBUG:
      {
	  LPDEBUGHOOKINFO lpdh32 = (LPDEBUGHOOKINFO)*plParam;
	  LPDEBUGHOOKINFO16 lpdh16 = SEGPTR_NEW( DEBUGHOOKINFO16 );

	  lpdh16->hModuleHook = 0;	/* FIXME */
	  lpdh16->reserved    = 0;
	  lpdh16->lParam      = lpdh32->lParam;
	  lpdh16->wParam      = lpdh32->wParam;
	  lpdh16->code        = lpdh32->code;
	  
	  *plParam = (LPARAM)SEGPTR_GET( lpdh16 );
	  break;
      }

      case WH_SHELL:
      case WH_KEYBOARD:
	break;

      case WH_HARDWARE:
      case WH_FOREGROUNDIDLE:
      case WH_CALLWNDPROCRET:
	FIXME("\t[%i] 32to16 translation unimplemented\n", id);
    }
}


/***********************************************************************
 *           HOOK_Map32ATo16
 */
static void HOOK_Map32ATo16(INT id, INT code, WPARAM *pwParam,
			    LPARAM *plParam)
{
    if (id == WH_CBT && code == HCBT_CREATEWND)
    {
	LPCBT_CREATEWNDA lpcbtcw32 = (LPCBT_CREATEWNDA)*plParam;
	LPCBT_CREATEWND16 lpcbtcw16 = SEGPTR_NEW( CBT_CREATEWND16 );
	LPCREATESTRUCT16 lpcs16 = SEGPTR_NEW( CREATESTRUCT16 );

	lpcbtcw16->lpcs = (LPCREATESTRUCT16)SEGPTR_GET( lpcs16 );
	STRUCT32_CREATESTRUCT32Ato16( lpcbtcw32->lpcs, lpcs16 );

	if (HIWORD(lpcbtcw32->lpcs->lpszName))
	  lpcs16->lpszName =
	    SEGPTR_GET( SEGPTR_STRDUP( lpcbtcw32->lpcs->lpszName ) );
	else
	  lpcs16->lpszName = (SEGPTR)lpcbtcw32->lpcs->lpszName;

	if (HIWORD(lpcbtcw32->lpcs->lpszClass))
	  lpcs16->lpszClass =
	    SEGPTR_GET( SEGPTR_STRDUP( lpcbtcw32->lpcs->lpszClass ) );
	else
	  lpcs16->lpszClass = (SEGPTR)lpcbtcw32->lpcs->lpszClass;

	lpcbtcw16->hwndInsertAfter = WIN_Handle16( lpcbtcw32->hwndInsertAfter );

	*plParam = (LPARAM)SEGPTR_GET( lpcbtcw16 );
    }
    else HOOK_Map32To16Common(id, code, pwParam, plParam, TRUE);
}


/***********************************************************************
 *           HOOK_Map32WTo16
 */
static void HOOK_Map32WTo16(INT id, INT code, WPARAM *pwParam,
			    LPARAM *plParam)
{
    if (id == WH_CBT && code == HCBT_CREATEWND)
    {
        LPSTR name, cls;
	LPCBT_CREATEWNDW lpcbtcw32 = (LPCBT_CREATEWNDW)*plParam;
	LPCBT_CREATEWND16 lpcbtcw16 = SEGPTR_NEW( CBT_CREATEWND16 );
	LPCREATESTRUCT16 lpcs16 = SEGPTR_NEW( CREATESTRUCT16 );

	lpcbtcw16->lpcs = (LPCREATESTRUCT16)SEGPTR_GET( lpcs16 );
	STRUCT32_CREATESTRUCT32Ato16( (LPCREATESTRUCTA)lpcbtcw32->lpcs,
				      lpcs16 );

        name = SEGPTR_STRDUP_WtoA( lpcbtcw32->lpcs->lpszName );
        cls  = SEGPTR_STRDUP_WtoA( lpcbtcw32->lpcs->lpszClass );
        lpcs16->lpszName  = SEGPTR_GET( name );
        lpcs16->lpszClass = SEGPTR_GET( cls );
        lpcbtcw16->hwndInsertAfter = WIN_Handle16( lpcbtcw32->hwndInsertAfter );

	*plParam = (LPARAM)SEGPTR_GET( lpcbtcw16 );
    }
    else HOOK_Map32To16Common(id, code, pwParam, plParam, FALSE);
}


/***********************************************************************
 *           HOOK_UnMap32To16Common
 */
static void HOOK_UnMap32To16Common(INT id, INT code, WPARAM wParamOrig,
				   LPARAM lParamOrig, WPARAM wParam,
				   LPARAM lParam, BOOL bA)
{
    switch (id)
    {
      case WH_MSGFILTER:
      case WH_SYSMSGFILTER:
      case WH_JOURNALRECORD:
      case WH_JOURNALPLAYBACK:
      case WH_MOUSE:
      case WH_DEBUG:
	SEGPTR_FREE( MapSL(lParam) );
	break;

      case WH_CALLWNDPROC:
      {
          LPCWPSTRUCT16   lpcwp16 = MapSL(lParam);
	  LPCWPSTRUCT   lpcwp32 = (LPCWPSTRUCT)lParamOrig;
	  MSGPARAM16	  mp16;

	  mp16.wParam = lpcwp16->wParam;
	  mp16.lParam = lpcwp16->lParam;
	  mp16.lResult = 0;

          if (bA) WINPROC_UnmapMsg32ATo16( lpcwp32->hwnd,lpcwp32->message, lpcwp32->wParam,
                                           lpcwp32->lParam, &mp16 );
          else WINPROC_UnmapMsg32WTo16( lpcwp32->hwnd,lpcwp32->message, lpcwp32->wParam,
                                        lpcwp32->lParam, &mp16 );
	  SEGPTR_FREE( MapSL(lParam) );
          break;
      }

      case WH_GETMESSAGE:
      {
	  LPMSG lpmsg32 = (LPMSG)lParamOrig;

	  STRUCT32_MSG16to32( MapSL(lParam), lpmsg32 );
	  SEGPTR_FREE( MapSL(lParam) );
	  break;
      }

      case WH_CBT:
	switch (code)
	{
	  case HCBT_CREATEWND:
	  {
	       LPCBT_CREATEWNDA lpcbtcw32 = (LPCBT_CREATEWNDA)(lParamOrig);
               LPCBT_CREATEWND16 lpcbtcw16 = MapSL(lParam);
               LPCREATESTRUCT16  lpcs16 = MapSL((SEGPTR)lpcbtcw16->lpcs);

               if (HIWORD(lpcs16->lpszName))
                   SEGPTR_FREE( MapSL(lpcs16->lpszName) );

               if (HIWORD(lpcs16->lpszClass))
                   SEGPTR_FREE( MapSL(lpcs16->lpszClass) );

               lpcbtcw32->hwndInsertAfter = WIN_Handle32( lpcbtcw16->hwndInsertAfter );

               SEGPTR_FREE( lpcs16 );
	  } /* fall through */

	  case HCBT_ACTIVATE:
	  case HCBT_CLICKSKIPPED:
	  case HCBT_MOVESIZE:

	       SEGPTR_FREE( MapSL(lParam) );
	       break;
	}
	break;

      case WH_SHELL:
      case WH_KEYBOARD:
	break;

      case WH_HARDWARE:
      case WH_FOREGROUNDIDLE:
      case WH_CALLWNDPROCRET:
	FIXME("\t[%i] skipping unmap\n", id);
    }
}


/***********************************************************************
 *           HOOK_UnMap32ATo16
 */
static void HOOK_UnMap32ATo16(INT id, INT code, WPARAM wParamOrig,
			      LPARAM lParamOrig, WPARAM wParam,
			      LPARAM lParam)
{
    HOOK_UnMap32To16Common( id, code, wParamOrig, lParamOrig, wParam,
			    lParam, TRUE );
}


/***********************************************************************
 *           HOOK_UnMap32WTo16
 */
static void HOOK_UnMap32WTo16(INT id, INT code, WPARAM wParamOrig,
			      LPARAM lParamOrig, WPARAM wParam,
			      LPARAM lParam)
{
    HOOK_UnMap32To16Common( id, code, wParamOrig, lParamOrig, wParam,
                            lParam, FALSE );
}


/***********************************************************************
 *           HOOK_Map32ATo32W
 */
static void HOOK_Map32ATo32W(INT id, INT code, WPARAM *pwParam,
			     LPARAM *plParam)
{
    if (id == WH_CBT && code == HCBT_CREATEWND)
    {
	LPCBT_CREATEWNDA lpcbtcwA = (LPCBT_CREATEWNDA)*plParam;
	LPCBT_CREATEWNDW lpcbtcwW = HeapAlloc( GetProcessHeap(), 0,
						 sizeof(*lpcbtcwW) );
	lpcbtcwW->lpcs = HeapAlloc( GetProcessHeap(), 0, sizeof(*lpcbtcwW->lpcs) );

	lpcbtcwW->hwndInsertAfter = lpcbtcwA->hwndInsertAfter;
	*lpcbtcwW->lpcs = *(LPCREATESTRUCTW)lpcbtcwA->lpcs;

	if (HIWORD(lpcbtcwA->lpcs->lpszName))
	{
	    lpcbtcwW->lpcs->lpszName = HEAP_strdupAtoW( GetProcessHeap(), 0,
                                                    lpcbtcwA->lpcs->lpszName );
	}
	else
	  lpcbtcwW->lpcs->lpszName = (LPWSTR)lpcbtcwA->lpcs->lpszName;

	if (HIWORD(lpcbtcwA->lpcs->lpszClass))
	{
	    lpcbtcwW->lpcs->lpszClass = HEAP_strdupAtoW( GetProcessHeap(), 0,
                                                   lpcbtcwA->lpcs->lpszClass );
	}
	else
	  lpcbtcwW->lpcs->lpszClass = (LPCWSTR)lpcbtcwA->lpcs->lpszClass;
	*plParam = (LPARAM)lpcbtcwW;
    }
    return;
}


/***********************************************************************
 *           HOOK_UnMap32ATo32W
 */
static void HOOK_UnMap32ATo32W(INT id, INT code, WPARAM wParamOrig,
			       LPARAM lParamOrig, WPARAM wParam,
			       LPARAM lParam)
{
    if (id == WH_CBT && code == HCBT_CREATEWND)
    {
	LPCBT_CREATEWNDW lpcbtcwW = (LPCBT_CREATEWNDW)lParam;
	if (HIWORD(lpcbtcwW->lpcs->lpszName))
            HeapFree( GetProcessHeap(), 0, (LPWSTR)lpcbtcwW->lpcs->lpszName );
	if (HIWORD(lpcbtcwW->lpcs->lpszClass))
            HeapFree( GetProcessHeap(), 0, (LPWSTR)lpcbtcwW->lpcs->lpszClass );
	HeapFree( GetProcessHeap(), 0, lpcbtcwW->lpcs );
	HeapFree( GetProcessHeap(), 0, lpcbtcwW );
    }
    return;
}


/***********************************************************************
 *           HOOK_Map32WTo32A
 */
static void HOOK_Map32WTo32A(INT id, INT code, WPARAM *pwParam,
			     LPARAM *plParam)
{
    if (id == WH_CBT && code == HCBT_CREATEWND)
    {
	LPCBT_CREATEWNDW lpcbtcwW = (LPCBT_CREATEWNDW)*plParam;
	LPCBT_CREATEWNDA lpcbtcwA = HeapAlloc( GetProcessHeap(), 0,
						 sizeof(*lpcbtcwA) );
	lpcbtcwA->lpcs = HeapAlloc( GetProcessHeap(), 0, sizeof(*lpcbtcwA->lpcs) );

	lpcbtcwA->hwndInsertAfter = lpcbtcwW->hwndInsertAfter;
	*lpcbtcwA->lpcs = *(LPCREATESTRUCTA)lpcbtcwW->lpcs;

	if (HIWORD(lpcbtcwW->lpcs->lpszName))
	  lpcbtcwA->lpcs->lpszName = HEAP_strdupWtoA( GetProcessHeap(), 0,
                                                    lpcbtcwW->lpcs->lpszName );
	else
	  lpcbtcwA->lpcs->lpszName = (LPSTR)lpcbtcwW->lpcs->lpszName;

	if (HIWORD(lpcbtcwW->lpcs->lpszClass))
	  lpcbtcwA->lpcs->lpszClass = HEAP_strdupWtoA( GetProcessHeap(), 0,
                                                   lpcbtcwW->lpcs->lpszClass );
	else
	  lpcbtcwA->lpcs->lpszClass = (LPSTR)lpcbtcwW->lpcs->lpszClass;
	*plParam = (LPARAM)lpcbtcwA;
    }
    return;
}


/***********************************************************************
 *           HOOK_UnMap32WTo32A
 */
static void HOOK_UnMap32WTo32A(INT id, INT code, WPARAM wParamOrig,
			       LPARAM lParamOrig, WPARAM wParam,
			       LPARAM lParam)
{
    if (id == WH_CBT && code == HCBT_CREATEWND)
    {
	LPCBT_CREATEWNDA lpcbtcwA = (LPCBT_CREATEWNDA)lParam;
	if (HIWORD(lpcbtcwA->lpcs->lpszName))
            HeapFree( GetProcessHeap(), 0, (LPSTR)lpcbtcwA->lpcs->lpszName );
	if (HIWORD(lpcbtcwA->lpcs->lpszClass))
            HeapFree( GetProcessHeap(), 0, (LPSTR)lpcbtcwA->lpcs->lpszClass );
	HeapFree( GetProcessHeap(), 0, lpcbtcwA->lpcs );
	HeapFree( GetProcessHeap(), 0, lpcbtcwA );
    }
    return;
}


/***********************************************************************
 *           Map Function Tables
 */
static const HOOK_MapFunc HOOK_MapFuncs[3][3] = 
{
    { NULL,            HOOK_Map16To32A,  HOOK_Map16To32W },
    { HOOK_Map32ATo16, NULL,             HOOK_Map32ATo32W },
    { HOOK_Map32WTo16, HOOK_Map32WTo32A, NULL }
};

static const HOOK_UnMapFunc HOOK_UnMapFuncs[3][3] = 
{
    { NULL,              HOOK_UnMap16To32A,  HOOK_UnMap16To32W },
    { HOOK_UnMap32ATo16, NULL,               HOOK_UnMap32ATo32W },
    { HOOK_UnMap32WTo16, HOOK_UnMap32WTo32A, NULL }
};


/***********************************************************************
 *           Internal Functions
 */

/***********************************************************************
 *           HOOK_GetNextHook
 *
 * Get the next hook of a given hook.
 */
static HANDLE16 HOOK_GetNextHook( HANDLE16 hook )
{
    HOOKDATA *data = (HOOKDATA *)USER_HEAP_LIN_ADDR( hook );

    if (!data || !hook) return 0;
    if (data->next) return data->next;
    if (!data->ownerQueue) return 0;  /* Already system hook */

    /* Now start enumerating the system hooks */
    return HOOK_systemHooks[data->id - WH_MINHOOK];
}


/***********************************************************************
 *           HOOK_GetHook
 *
 * Get the first hook for a given type.
 */
static HANDLE16 HOOK_GetHook( INT16 id )
{
    MESSAGEQUEUE *queue;
    HANDLE16 hook = 0;

    if ((queue = QUEUE_Current()) != NULL)
        hook = queue->hooks[id - WH_MINHOOK];
    if (!hook) hook = HOOK_systemHooks[id - WH_MINHOOK];
    return hook;
}


/***********************************************************************
 *           HOOK_SetHook
 *
 * Install a given hook.
 */
/* ### start build ### */
extern LONG CALLBACK HOOK_CallTo16_long_wwl(FARPROC16,WORD,WORD,LONG);
/* ### stop build ### */
static HHOOK HOOK_SetHook( INT16 id, LPVOID proc, INT type,
		           HMODULE16 hModule, DWORD dwThreadId )
{
    HOOKDATA *data;
    HANDLE16 handle;
    HQUEUE16 hQueue = 0;

    if ((id < WH_MINHOOK) || (id > WH_MAXHOOK)) return 0;

    TRACE("Setting hook %d: %08x %04x %08lx\n",
                  id, (UINT)proc, hModule, dwThreadId );

    /* Create task queue if none present */
    InitThreadInput16( 0, 0 );

    if (id == WH_JOURNALPLAYBACK) EnableHardwareInput16(FALSE);

    if (dwThreadId)  /* Task-specific hook */
    {
	if ((id == WH_JOURNALRECORD) || (id == WH_JOURNALPLAYBACK) ||
	    (id == WH_SYSMSGFILTER)) return 0;  /* System-only hooks */
        if (!(hQueue = GetThreadQueue16( dwThreadId )))
            return 0;
    }

    /* Create the hook structure */

    if (!(handle = USER_HEAP_ALLOC( sizeof(HOOKDATA) ))) return 0;
    data = (HOOKDATA *) USER_HEAP_LIN_ADDR( handle );
    data->proc        = proc;
    data->id          = id;
    data->ownerQueue  = hQueue;
    data->ownerModule = hModule;
    data->flags       = type;

    /* Create CallTo16 thunk for 16-bit hooks */

    if ( (data->flags & HOOK_MAPTYPE) == HOOK_WIN16 )
        data->thunk = (HOOKPROC)THUNK_Alloc( (FARPROC16)data->proc, 
                                             (RELAY)HOOK_CallTo16_long_wwl );
    else
        data->thunk = data->proc;

    if ( !data->thunk && data->proc )
    {
        USER_HEAP_FREE( handle );
        return 0;
    }

    /* Insert it in the correct linked list */

    if (hQueue)
    {
        MESSAGEQUEUE *queue = (MESSAGEQUEUE *)QUEUE_Lock( hQueue );
        data->next = queue->hooks[id - WH_MINHOOK];
        queue->hooks[id - WH_MINHOOK] = handle;
        QUEUE_Unlock( queue );
    }
    else
    {
        data->next = HOOK_systemHooks[id - WH_MINHOOK];
        HOOK_systemHooks[id - WH_MINHOOK] = handle;
    }
    TRACE("Setting hook %d: ret=%04x [next=%04x]\n", 
			   id, handle, data->next );

    return (HHOOK)( handle? MAKELONG( handle, HOOK_MAGIC ) : 0 );
}


/***********************************************************************
 *           HOOK_RemoveHook
 *
 * Remove a hook from the list.
 */
static BOOL HOOK_RemoveHook( HANDLE16 hook )
{
    HOOKDATA *data;
    HANDLE16 *prevHook;

    TRACE("Removing hook %04x\n", hook );

    if (!(data = (HOOKDATA *)USER_HEAP_LIN_ADDR(hook))) return FALSE;
    if (data->flags & HOOK_INUSE)
    {
        /* Mark it for deletion later on */
        WARN("Hook still running, deletion delayed\n" );
        data->proc = (HOOKPROC)0;
        return TRUE;
    }

    if (data->id == WH_JOURNALPLAYBACK) EnableHardwareInput16(TRUE);
     
    /* Remove it from the linked list */

    if (data->ownerQueue)
    {
        MESSAGEQUEUE *queue = (MESSAGEQUEUE *)QUEUE_Lock( data->ownerQueue );
        if (!queue) return FALSE;
        prevHook = &queue->hooks[data->id - WH_MINHOOK];
        QUEUE_Unlock( queue );
    }
    else prevHook = &HOOK_systemHooks[data->id - WH_MINHOOK];

    while (*prevHook && *prevHook != hook)
        prevHook = &((HOOKDATA *)USER_HEAP_LIN_ADDR(*prevHook))->next;

    if (!*prevHook) return FALSE;
    *prevHook = data->next;

    if ( (data->flags & HOOK_MAPTYPE) == HOOK_WIN16 )
        THUNK_Free( (FARPROC)data->thunk );

    USER_HEAP_FREE( hook );
    return TRUE;
}


/***********************************************************************
 *           HOOK_FindValidHook
 */
static HANDLE16 HOOK_FindValidHook( HANDLE16 hook )
{
    HOOKDATA *data;

    for (;;)
    {
	if (!(data = (HOOKDATA *)USER_HEAP_LIN_ADDR(hook))) return 0;
	if (data->proc) return hook;
	hook = data->next;
    }
}


/***********************************************************************
 *           HOOK_CallHook
 *
 * Call a hook procedure.
 */
static LRESULT HOOK_CallHook( HANDLE16 hook, INT fromtype, INT code,
                              WPARAM wParam, LPARAM lParam )
{
    MESSAGEQUEUE *queue;
    HANDLE16 prevHook;
    HOOKDATA *data = (HOOKDATA *)USER_HEAP_LIN_ADDR(hook);
    LRESULT ret;
    int iWndsLocks;

    WPARAM wParamOrig = wParam;
    LPARAM lParamOrig = lParam;
    HOOK_MapFunc MapFunc;
    HOOK_UnMapFunc UnMapFunc;

    MapFunc = HOOK_MapFuncs[fromtype][data->flags & HOOK_MAPTYPE];
    UnMapFunc = HOOK_UnMapFuncs[fromtype][data->flags & HOOK_MAPTYPE];

    if (MapFunc)
      MapFunc( data->id, code, &wParam, &lParam );

    /* Now call it */

    if (!(queue = QUEUE_Current())) return 0;
    prevHook = queue->hCurHook;
    queue->hCurHook = hook;
    data->flags |= HOOK_INUSE;

    TRACE("Calling hook %04x: %d %08x %08lx\n",
                  hook, code, wParam, lParam );

    /* Suspend window structure locks before calling user code */
    iWndsLocks = WIN_SuspendWndsLock();

    ret = data->thunk(code, wParam, lParam);

    /* Grrr. While the hook procedure is supposed to have an LRESULT return
       value even in Win16, it seems that for those hook types where the 
       return value is interpreted as BOOL, Windows doesn't actually check
       the HIWORD ...  Some buggy Win16 programs, notably WINFILE, rely on
       that, because they neglect to clear DX ... */
    if (    (data->flags & HOOK_MAPTYPE) == HOOK_WIN16 
         && data->id != WH_JOURNALPLAYBACK )
        ret = LOWORD( ret );

    WIN_RestoreWndsLock(iWndsLocks);

    TRACE("Ret hook %04x = %08lx\n", hook, ret );

    data->flags &= ~HOOK_INUSE;
    queue->hCurHook = prevHook;

    if (UnMapFunc)
      UnMapFunc( data->id, code, wParamOrig, lParamOrig, wParam, lParam );

    if (!data->proc) HOOK_RemoveHook( hook );

    return ret;
}

/***********************************************************************
 *           Exported Functions & APIs
 */

/***********************************************************************
 *           HOOK_IsHooked
 *
 * Replacement for calling HOOK_GetHook from other modules.
 */
BOOL HOOK_IsHooked( INT16 id )
{
    return HOOK_GetHook( id ) != 0;
}


/***********************************************************************
 *           HOOK_CallHooks16
 *
 * Call a hook chain.
 */
LRESULT HOOK_CallHooks16( INT16 id, INT16 code, WPARAM16 wParam,
                          LPARAM lParam )
{
    HANDLE16 hook; 

    if (!(hook = HOOK_GetHook( id ))) return 0;
    if (!(hook = HOOK_FindValidHook(hook))) return 0;
    return HOOK_CallHook( hook, HOOK_WIN16, code, wParam, lParam );
}

/***********************************************************************
 *           HOOK_CallHooksA
 *
 * Call a hook chain.
 */
LRESULT HOOK_CallHooksA( INT id, INT code, WPARAM wParam,
                           LPARAM lParam )
{
    HANDLE16 hook; 

    if (!(hook = HOOK_GetHook( id ))) return 0;
    if (!(hook = HOOK_FindValidHook(hook))) return 0;
    return HOOK_CallHook( hook, HOOK_WIN32A, code, wParam, lParam );
}

/***********************************************************************
 *           HOOK_CallHooksW
 *
 * Call a hook chain.
 */
LRESULT HOOK_CallHooksW( INT id, INT code, WPARAM wParam,
                           LPARAM lParam )
{
    HANDLE16 hook; 

    if (!(hook = HOOK_GetHook( id ))) return 0;
    if (!(hook = HOOK_FindValidHook(hook))) return 0;
    return HOOK_CallHook( hook, HOOK_WIN32W, code, wParam,
			  lParam );
}


/***********************************************************************
 *	     HOOK_FreeModuleHooks
 */
void HOOK_FreeModuleHooks( HMODULE16 hModule )
{
 /* remove all system hooks registered by this module */

  HOOKDATA*     hptr;
  HHOOK         hook, next;
  int           id;

  for( id = WH_MINHOOK; id <= WH_MAXHOOK; id++ )
    {
       hook = HOOK_systemHooks[id - WH_MINHOOK];
       while( hook )
          if( (hptr = (HOOKDATA *)USER_HEAP_LIN_ADDR(hook)) )
	    {
	      next = hptr->next;
	      if( hptr->ownerModule == hModule )
                {
                  hptr->flags &= HOOK_MAPTYPE;
                  HOOK_RemoveHook(hook);
                }
	      hook = next;
	    }
	  else hook = 0;
    }
}

/***********************************************************************
 *	     HOOK_FreeQueueHooks
 */
void HOOK_FreeQueueHooks(void)
{
  /* remove all hooks registered by the current queue */

  HOOKDATA*	hptr = NULL;
  HHOOK 	hook, next;
  int 		id;

  for( id = WH_MINHOOK; id <= WH_MAXHOOK; id++ )
    {
       hook = HOOK_GetHook( id );
       while( hook )
	{
	  next = HOOK_GetNextHook(hook);

	  hptr = (HOOKDATA *)USER_HEAP_LIN_ADDR(hook);
	  if( hptr && hptr->ownerQueue )
	    {
	      hptr->flags &= HOOK_MAPTYPE;
	      HOOK_RemoveHook(hook);
	    }
	  hook = next;
	}
    }
}


/***********************************************************************
 *		SetWindowsHook (USER.121)
 */
FARPROC16 WINAPI SetWindowsHook16( INT16 id, HOOKPROC16 proc )
{
    HINSTANCE16 hInst = FarGetOwner16( HIWORD(proc) );

    /* WH_MSGFILTER is the only task-specific hook for SetWindowsHook() */
    HTASK16 hTask = (id == WH_MSGFILTER) ? GetCurrentTask() : 0;

    return (FARPROC16)SetWindowsHookEx16( id, proc, hInst, hTask );
}

/***********************************************************************
 *		SetWindowsHookA (USER32.@)
 */
HHOOK WINAPI SetWindowsHookA( INT id, HOOKPROC proc )
{
    return SetWindowsHookExA( id, proc, 0, GetCurrentThreadId() );
}

/***********************************************************************
 *		SetWindowsHookW (USER32.@)
 */
HHOOK WINAPI SetWindowsHookW( INT id, HOOKPROC proc )
{
    return SetWindowsHookExW( id, proc, 0, GetCurrentThreadId() );
}


/***********************************************************************
 *		SetWindowsHookEx (USER.291)
 *		SetWindowsHookEx16 (USER32.@)
 */
HHOOK WINAPI SetWindowsHookEx16( INT16 id, HOOKPROC16 proc, HINSTANCE16 hInst,
                                 HTASK16 hTask )
{
    if (id == WH_DEBUG)
    {
	FIXME("WH_DEBUG is broken in 16-bit Windows.\n");
	return 0;
    }
    return HOOK_SetHook( id, proc, HOOK_WIN16, GetExePtr(hInst), (DWORD)hTask );
}

/***********************************************************************
 *		SetWindowsHookExA (USER32.@)
 */
HHOOK WINAPI SetWindowsHookExA( INT id, HOOKPROC proc, HINSTANCE hInst,
                                  DWORD dwThreadId )
{
    return HOOK_SetHook( id, proc, HOOK_WIN32A, MapHModuleLS(hInst), dwThreadId );
}

/***********************************************************************
 *		SetWindowsHookExW (USER32.@)
 */
HHOOK WINAPI SetWindowsHookExW( INT id, HOOKPROC proc, HINSTANCE hInst,
                                  DWORD dwThreadId )
{
    return HOOK_SetHook( id, proc, HOOK_WIN32W, MapHModuleLS(hInst), dwThreadId );
}


/***********************************************************************
 *		UnhookWindowsHook (USER.234)
 */
BOOL16 WINAPI UnhookWindowsHook16( INT16 id, HOOKPROC16 proc )
{
    return UnhookWindowsHook( id, (HOOKPROC)proc );
}

/***********************************************************************
 *		UnhookWindowsHook (USER32.@)
 */
BOOL WINAPI UnhookWindowsHook( INT id, HOOKPROC proc )
{
    HANDLE16 hook = HOOK_GetHook( id );

    TRACE("%d %08lx\n", id, (DWORD)proc );

    while (hook)
    {
        HOOKDATA *data = (HOOKDATA *)USER_HEAP_LIN_ADDR(hook);
        if (data->proc == proc) break;
        hook = HOOK_GetNextHook( hook );
    }
    if (!hook) return FALSE;
    return HOOK_RemoveHook( hook );
}


/***********************************************************************
 *		UnhookWindowsHookEx (USER.292)
 */
BOOL16 WINAPI UnhookWindowsHookEx16( HHOOK hhook )
{
    return UnhookWindowsHookEx( hhook );
}

/***********************************************************************
 *		UnhookWindowsHookEx (USER32.@)
 */
BOOL WINAPI UnhookWindowsHookEx( HHOOK hhook )
{
    if (HIWORD(hhook) != HOOK_MAGIC) return FALSE;  /* Not a new format hook */
    return HOOK_RemoveHook( LOWORD(hhook) );
}


/***********************************************************************
 *		CallNextHookEx (USER.293)
 *		CallNextHookEx16 (USER32.@)
 *
 * I wouldn't have separated this into 16 and 32 bit versions, but I
 * need a way to figure out if I need to do a mapping or not.
 */
LRESULT WINAPI CallNextHookEx16( HHOOK hhook, INT16 code, WPARAM16 wParam,
                                 LPARAM lParam )
{
    HANDLE16 next;

    if (HIWORD(hhook) != HOOK_MAGIC) return 0;  /* Not a new format hook */
    if (!(next = HOOK_GetNextHook( LOWORD(hhook) ))) return 0;

    return HOOK_CallHook( next, HOOK_WIN16, code, wParam, lParam );
}


/***********************************************************************
 *		CallNextHookEx (USER32.@)
 *
 * There aren't ANSI and UNICODE versions of this.
 */
LRESULT WINAPI CallNextHookEx( HHOOK hhook, INT code, WPARAM wParam,
                                 LPARAM lParam )
{
    HANDLE16 next;
    INT fromtype;	/* figure out Ansi/Unicode */
    HOOKDATA *oldhook;

    if (HIWORD(hhook) != HOOK_MAGIC) return 0;  /* Not a new format hook */
    if (!(next = HOOK_GetNextHook( LOWORD(hhook) ))) return 0;

    oldhook = (HOOKDATA *)USER_HEAP_LIN_ADDR( LOWORD(hhook) );
    fromtype = oldhook->flags & HOOK_MAPTYPE;

    if (fromtype == HOOK_WIN16)
      ERR("called from 16bit hook!\n");

    return HOOK_CallHook( next, fromtype, code, wParam, lParam );
}


/***********************************************************************
 *		DefHookProc (USER.235)
 */
LRESULT WINAPI DefHookProc16( INT16 code, WPARAM16 wParam, LPARAM lParam,
                              HHOOK *hhook )
{
    /* Note: the *hhook parameter is never used, since we rely on the
     * current hook value from the task queue to find the next hook. */
    MESSAGEQUEUE *queue;

    if (!(queue = QUEUE_Current())) return 0;
    return CallNextHookEx16( queue->hCurHook, code, wParam, lParam );
}


/***********************************************************************
 *		CallMsgFilter (USER.123)
 */
BOOL16 WINAPI CallMsgFilter16( SEGPTR msg, INT16 code )
{
    if (GetSysModalWindow16()) return FALSE;
    if (HOOK_CallHooks16( WH_SYSMSGFILTER, code, 0, (LPARAM)msg )) return TRUE;
    return HOOK_CallHooks16( WH_MSGFILTER, code, 0, (LPARAM)msg );
}


/***********************************************************************
 *		CallMsgFilter32 (USER.823)
 */
BOOL16 WINAPI CallMsgFilter32_16( SEGPTR msg16_32, INT16 code, BOOL16 wHaveParamHigh )
{
    MSG32_16 *lpmsg16_32 = MapSL(msg16_32);

    if (wHaveParamHigh == FALSE)
    {
        lpmsg16_32->wParamHigh = 0;
        /* WARNING: msg16_32->msg has to be the first variable in the struct */ 
        return CallMsgFilter16(msg16_32, code);
    }
    else
    {
        MSG msg32;
        BOOL16 ret;

        msg32.hwnd      = WIN_Handle32( lpmsg16_32->msg.hwnd );
        msg32.message   = lpmsg16_32->msg.message;
        msg32.wParam    = MAKELONG(lpmsg16_32->msg.wParam, lpmsg16_32->wParamHigh);
        msg32.lParam    = lpmsg16_32->msg.lParam;
        msg32.time      = lpmsg16_32->msg.time;
        msg32.pt.x      = lpmsg16_32->msg.pt.x;
        msg32.pt.y      = lpmsg16_32->msg.pt.y;

        ret = (BOOL16)CallMsgFilterA(&msg32, (INT)code);

        lpmsg16_32->msg.hwnd    = WIN_Handle16( msg32.hwnd );
        lpmsg16_32->msg.message = msg32.message;
        lpmsg16_32->msg.wParam  = LOWORD(msg32.wParam);
        lpmsg16_32->msg.lParam  = msg32.lParam;
        lpmsg16_32->msg.time    = msg32.time;
        lpmsg16_32->msg.pt.x    = msg32.pt.x;
        lpmsg16_32->msg.pt.y    = msg32.pt.y;
        lpmsg16_32->wParamHigh  = HIWORD(msg32.wParam);

        return ret;
    }
}


/***********************************************************************
 *		CallMsgFilterA (USER32.@)
 *
 * FIXME: There are ANSI and UNICODE versions of this, plus an unspecified
 * version, plus USER (the 16bit one) has a CallMsgFilter32 function.
 */
BOOL WINAPI CallMsgFilterA( LPMSG msg, INT code )
{
    if (GetSysModalWindow16()) return FALSE;	/* ??? */
    if (HOOK_CallHooksA( WH_SYSMSGFILTER, code, 0, (LPARAM)msg ))
      return TRUE;
    return HOOK_CallHooksA( WH_MSGFILTER, code, 0, (LPARAM)msg );
}


/***********************************************************************
 *		CallMsgFilterW (USER32.@)
 */
BOOL WINAPI CallMsgFilterW( LPMSG msg, INT code )
{
    if (GetSysModalWindow16()) return FALSE;	/* ??? */
    if (HOOK_CallHooksW( WH_SYSMSGFILTER, code, 0, (LPARAM)msg ))
      return TRUE;
    return HOOK_CallHooksW( WH_MSGFILTER, code, 0, (LPARAM)msg );
}

