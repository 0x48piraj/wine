/*
 * TTY window driver
 *
 * Copyright 1998,1999 Patrik Stridvall
 */

#include "config.h"

#include "gdi.h"
#include "heap.h"
#include "ttydrv.h"
#include "win.h"
#include "debugtools.h"

DEFAULT_DEBUG_CHANNEL(ttydrv);

WND_DRIVER TTYDRV_WND_Driver =
{
  TTYDRV_WND_Initialize,
  TTYDRV_WND_Finalize,
  TTYDRV_WND_CreateDesktopWindow,
  TTYDRV_WND_CreateWindow,
  TTYDRV_WND_DestroyWindow,
  TTYDRV_WND_SetParent,
  TTYDRV_WND_ForceWindowRaise,
  TTYDRV_WND_SetWindowPos,
  TTYDRV_WND_SetText,
  TTYDRV_WND_SetFocus,
  TTYDRV_WND_PreSizeMove,
  TTYDRV_WND_PostSizeMove,
  TTYDRV_WND_ScrollWindow,
  TTYDRV_WND_SetDrawable,
  TTYDRV_WND_SetHostAttr,
  TTYDRV_WND_IsSelfClipping,
  TTYDRV_WND_SetWindowRgn
};


/***********************************************************************
 *		TTYDRV_WND_GetCursesWindow
 *
 * Return the Curses window associated to a window.
 */
WINDOW *TTYDRV_WND_GetCursesWindow(WND *wndPtr)
{
    return wndPtr && wndPtr->pDriverData ? 
      ((TTYDRV_WND_DATA *) wndPtr->pDriverData)->window : 0;
}

/**********************************************************************
 *		TTYDRV_WND_Initialize
 */
void TTYDRV_WND_Initialize(WND *wndPtr)
{
  TTYDRV_WND_DATA *pWndDriverData = 
    (TTYDRV_WND_DATA *) HeapAlloc(SystemHeap, 0, sizeof(TTYDRV_WND_DATA));

  TRACE("(%p)\n", wndPtr);

  wndPtr->pDriverData = (void *) pWndDriverData;

  pWndDriverData->window = NULL;
}

/**********************************************************************
 *		TTYDRV_WND_Finalize
 */
void TTYDRV_WND_Finalize(WND *wndPtr)
{
  TTYDRV_WND_DATA *pWndDriverData =
    (TTYDRV_WND_DATA *) wndPtr->pDriverData;

  TRACE("(%p)\n", wndPtr);

  if(!pWndDriverData) {
    ERR("WND already destroyed\n");
    return;
  }

  if(pWndDriverData->window) {
    ERR("WND destroyed without destroying the associated Curses Windows");
  }

  HeapFree(SystemHeap, 0, pWndDriverData);
  wndPtr->pDriverData = NULL;
}

/**********************************************************************
 *		TTYDRV_WND_CreateDesktopWindow
 */
BOOL TTYDRV_WND_CreateDesktopWindow(WND *wndPtr)
{
  TTYDRV_WND_DATA *pWndDriverData =
    (TTYDRV_WND_DATA *) wndPtr->pDriverData;

  TRACE("(%p)\n", wndPtr);

  if(!pWndDriverData) { ERR("WND never initialized\n"); return FALSE; }

  pWndDriverData->window = TTYDRV_GetRootWindow();
  return TRUE;
}

/**********************************************************************
 *		TTYDRV_WND_CreateWindow
 */
BOOL TTYDRV_WND_CreateWindow(WND *wndPtr, CREATESTRUCTA *cs, BOOL bUnicode)
{
#ifdef WINE_CURSES
  WINDOW *window;
  INT cellWidth=8, cellHeight=8; /* FIXME: Hardcoded */

  TRACE("(%p, %p, %d)\n", wndPtr, cs, bUnicode);

  /* Only create top-level windows */
  if(cs->style & WS_CHILD)
    return TRUE;

  window = subwin(TTYDRV_GetRootWindow(), cs->cy/cellHeight, cs->cx/cellWidth,
		  cs->y/cellHeight, cs->x/cellWidth);
  werase(window);
  wrefresh(window);
		  
  return TRUE;
#else /* defined(WINE_CURSES) */
  FIXME("(%p, %p, %p, %d): stub\n", wndPtr, cs, bUnicode);

  return TRUE;
#endif /* defined(WINE_CURSES) */
}

/***********************************************************************
 *		TTYDRV_WND_DestroyWindow
 */
BOOL TTYDRV_WND_DestroyWindow(WND *wndPtr)
{
#ifdef WINE_CURSES
  WINDOW *window;

  TRACE("(%p)\n", wndPtr);

  window = TTYDRV_WND_GetCursesWindow(wndPtr);
  if(window && window != TTYDRV_GetRootWindow()) {
    delwin(window);
  }

  return TRUE;
#else /* defined(WINE_CURSES) */
  FIXME("(%p): stub\n", wndPtr);

  return TRUE;
#endif /* defined(WINE_CURSES) */
}

/*****************************************************************
 *		TTYDRV_WND_SetParent
 */
WND *TTYDRV_WND_SetParent(WND *wndPtr, WND *pWndParent)
{
  FIXME("(%p, %p): stub\n", wndPtr, pWndParent);

  return NULL;
}

/***********************************************************************
 *		TTYDRV_WND_ForceWindowRaise
 */
void TTYDRV_WND_ForceWindowRaise(WND *wndPtr)
{
  FIXME("(%p): stub\n", wndPtr);
}

/***********************************************************************
 *           TTYDRV_WINPOS_SetWindowPos
 */
void TTYDRV_WND_SetWindowPos(WND *wndPtr, const WINDOWPOS *winpos, BOOL bSMC_SETXPOS)
{
  FIXME("(%p, %p, %d): stub\n", wndPtr, winpos, bSMC_SETXPOS);
}

/*****************************************************************
 *		TTYDRV_WND_SetText
 */
void TTYDRV_WND_SetText(WND *wndPtr, LPCWSTR text)
{
  FIXME("(%p, %s): stub\n", wndPtr, debugstr_w(text));
}

/*****************************************************************
 *		TTYDRV_WND_SetFocus
 */
void TTYDRV_WND_SetFocus(WND *wndPtr)
{
  FIXME("(%p): stub\n", wndPtr);
}

/*****************************************************************
 *		TTYDRV_WND_PreSizeMove
 */
void TTYDRV_WND_PreSizeMove(WND *wndPtr)
{
  FIXME("(%p): stub\n", wndPtr);
}

/*****************************************************************
 *		 TTYDRV_WND_PostSizeMove
 */
void TTYDRV_WND_PostSizeMove(WND *wndPtr)
{
  FIXME("(%p): stub\n", wndPtr);
}

/*****************************************************************
 *		 TTYDRV_WND_ScrollWindow
 */
void TTYDRV_WND_ScrollWindow( WND *wndPtr, HDC hdc, INT dx, INT dy, 
                              const RECT *clipRect, BOOL bUpdate)
{
  FIXME("(%p, %x, %d, %d, %p, %d): stub\n", 
	wndPtr, hdc, dx, dy, clipRect, bUpdate);
}

/***********************************************************************
 *		TTYDRV_WND_SetDrawable
 */
void TTYDRV_WND_SetDrawable(WND *wndPtr, HDC hdc, WORD flags, BOOL bSetClipOrigin)
{
    DC *dc = DC_GetDCPtr( hdc );
    if (!dc) return;
    TRACE("(%p, %p, %d, %d)\n", wndPtr, dc, flags, bSetClipOrigin);

    /* FIXME: Should be done in the common code instead */
    if(!wndPtr)  {
        dc->DCOrgX = 0;
        dc->DCOrgY = 0;
    } else {
        if(flags & DCX_WINDOW) {
            dc->DCOrgX = wndPtr->rectWindow.left;
            dc->DCOrgY = wndPtr->rectWindow.top;
        } else {
            dc->DCOrgX = wndPtr->rectClient.left;
            dc->DCOrgY = wndPtr->rectClient.top;
        }
    }
    GDI_ReleaseObj( hdc );
}

/***********************************************************************
 *              TTYDRV_WND_SetHostAttr
 */
BOOL TTYDRV_WND_SetHostAttr(WND *wndPtr, INT attr, INT value)
{
  FIXME("(%p): stub\n", wndPtr);

  return TRUE;
}

/***********************************************************************
 *		TTYDRV_WND_IsSelfClipping
 */
BOOL TTYDRV_WND_IsSelfClipping(WND *wndPtr)
{
  FIXME("(%p): semistub\n", wndPtr);

  return FALSE;
}

/***********************************************************************
 *		TTYDRV_WND_SetWindowRgn
 */
void TTYDRV_WND_SetWindowRgn(struct tagWND *wndPtr, HRGN hrgnWnd)
{
}

