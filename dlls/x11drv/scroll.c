/*
 * Scroll windows and DCs
 *
 * Copyright 1993  David W. Metcalfe
 * Copyright 1995, 1996 Alex Korobka
 * Copyright 2001 Alexandre Julliard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config.h"

#include "ts_xlib.h"

#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"

#include "x11drv.h"
#include "win.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(scroll);


/*************************************************************************
 *		ScrollDC   (X11DRV.@)
 *
 * Only the hrgnUpdate is returned in device coordinates.
 * rcUpdate must be returned in logical coordinates to comply with win API.
 * FIXME: the doc explicitly states the opposite, to be checked
 */
BOOL X11DRV_ScrollDC( HDC hdc, INT dx, INT dy, const RECT *rc,
                      const RECT *clipRect, HRGN hrgnUpdate, LPRECT rcUpdate )
{
    RECT rect, rClip, rDst;

    TRACE( "%04x %d,%d hrgnUpdate=%04x rcUpdate = %p\n", hdc, dx, dy, hrgnUpdate, rcUpdate );
    if (clipRect) TRACE( "cliprc = (%d,%d,%d,%d)\n",
                         clipRect->left, clipRect->top, clipRect->right, clipRect->bottom );
    if (rc) TRACE( "rc = (%d,%d,%d,%d)\n", rc->left, rc->top, rc->right, rc->bottom );

    /* compute device clipping region (in device coordinates) */

    if (rc) rect = *rc;
    else GetClipBox( hdc, &rect );

    if (clipRect)
    {
        rClip = *clipRect;
        IntersectRect( &rClip, &rect, &rClip );
    }
    else rClip = rect;

    rDst = rClip;
    OffsetRect( &rDst, dx,  dy );
    IntersectRect( &rDst, &rDst, &rClip );

    if (!IsRectEmpty(&rDst))
    {
        /* copy bits */
        if (!BitBlt( hdc, rDst.left, rDst.top,
                     rDst.right - rDst.left, rDst.bottom - rDst.top,
                     hdc, rDst.left - dx, rDst.top - dy, SRCCOPY))
            return FALSE;
    }

    /* compute update areas */

    if (hrgnUpdate || rcUpdate)
    {
        HRGN hrgn = hrgnUpdate, hrgn2;

        /* map everything to device coordinates */
        LPtoDP( hdc, (LPPOINT)&rClip, 2 );
        LPtoDP( hdc, (LPPOINT)&rDst, 2 );

        hrgn2 = CreateRectRgnIndirect( &rDst );
        if (hrgn) SetRectRgn( hrgn, rClip.left, rClip.top, rClip.right, rClip.bottom );
        else hrgn = CreateRectRgn( rClip.left, rClip.top, rClip.right, rClip.bottom );
        CombineRgn( hrgn, hrgn, hrgn2, RGN_DIFF );

        if( rcUpdate )
        {
            GetRgnBox( hrgn, rcUpdate );

            /* Put the rcUpdate in logical coordinate */
            DPtoLP( hdc, (LPPOINT)rcUpdate, 2 );
        }
        if (!hrgnUpdate) DeleteObject( hrgn );
        DeleteObject( hrgn2 );
    }
    return TRUE;
}


/*************************************************************************
 *		ScrollWindowEx   (X11DRV.@)
 *
 * Note: contrary to what the doc says, pixels that are scrolled from the
 *      outside of clipRect to the inside are NOT painted.
 *
 * Parameter are the same as in ScrollWindowEx, with the additional
 * requirement that rect and clipRect are _valid_ pointers, to
 * rectangles _within_ the client are. Moreover, there is something
 * to scroll.
 */
INT X11DRV_ScrollWindowEx( HWND hwnd, INT dx, INT dy,
                           const RECT *rect, const RECT *clipRect,
                           HRGN hrgnUpdate, LPRECT rcUpdate, UINT flags )
{
    INT   retVal;
    BOOL  bOwnRgn = TRUE;
    BOOL  bUpdate = (rcUpdate || hrgnUpdate || flags & (SW_INVALIDATE | SW_ERASE));
    HRGN  hrgnClip = CreateRectRgnIndirect(clipRect);
    HRGN  hrgnTemp;
    HDC   hDC;

    TRACE( "%04x, %d,%d hrgnUpdate=%04x rcUpdate = %p rect=(%d,%d-%d,%d) %04x\n",
           hwnd, dx, dy, hrgnUpdate, rcUpdate,
           rect->left, rect->top, rect->right, rect->bottom, flags );
    TRACE( "clipRect = (%d,%d,%d,%d)\n",
           clipRect->left, clipRect->top, clipRect->right, clipRect->bottom );

    if( hrgnUpdate ) bOwnRgn = FALSE;
    else if( bUpdate ) hrgnUpdate = CreateRectRgn( 0, 0, 0, 0 );

    hDC = GetDCEx( hwnd, 0, DCX_CACHE | DCX_USESTYLE );
    if (hDC)
    {
        HRGN hrgn = CreateRectRgn( 0, 0, 0, 0 );
        X11DRV_StartGraphicsExposures( hDC );
        X11DRV_ScrollDC( hDC, dx, dy, rect, clipRect, hrgnUpdate, rcUpdate );
        X11DRV_EndGraphicsExposures( hDC, hrgn );
        ReleaseDC( hwnd, hDC );
        if (bUpdate) CombineRgn( hrgnUpdate, hrgnUpdate, hrgn, RGN_OR );
        else RedrawWindow( hwnd, NULL, hrgn, RDW_INVALIDATE | RDW_ERASE );
        DeleteObject( hrgn );
    }

    /* Take into account the fact that some damages may have occured during the scroll */
    hrgnTemp = CreateRectRgn( 0, 0, 0, 0 );
    retVal = GetUpdateRgn( hwnd, hrgnTemp, FALSE );
    if (retVal != NULLREGION)
    {
        OffsetRgn( hrgnTemp, dx, dy );
        CombineRgn( hrgnTemp, hrgnTemp, hrgnClip, RGN_AND );
        RedrawWindow( hwnd, NULL, hrgnTemp, RDW_INVALIDATE | RDW_ERASE );
    }
    DeleteObject( hrgnTemp );

    if( flags & SW_SCROLLCHILDREN )
    {
        HWND *list = WIN_ListChildren( hwnd );
        if (list)
        {
            int i;
            RECT r, dummy;
            for (i = 0; list[i]; i++)
            {
                GetWindowRect( list[i], &r );
                MapWindowPoints( 0, hwnd, (POINT *)&r, 2 );
                if (!rect || IntersectRect(&dummy, &r, rect))
                    SetWindowPos( list[i], 0, r.left + dx, r.top  + dy, 0, 0,
                                  SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE |
                                  SWP_NOREDRAW | SWP_DEFERERASE );
            }
            HeapFree( GetProcessHeap(), 0, list );
        }
    }

    if( flags & (SW_INVALIDATE | SW_ERASE) )
        RedrawWindow( hwnd, NULL, hrgnUpdate, RDW_INVALIDATE | RDW_ERASE |
                      ((flags & SW_ERASE) ? RDW_ERASENOW : 0) |
                      ((flags & SW_SCROLLCHILDREN) ? RDW_ALLCHILDREN : 0 ) );

    if( bOwnRgn && hrgnUpdate ) DeleteObject( hrgnUpdate );
    DeleteObject( hrgnClip );
    
    return retVal;
}
