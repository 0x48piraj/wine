/*
 * X11 mouse driver
 *
 * Copyright 1998 Ulrich Weigand
 * Copyright 2007 Henri Verbeet
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "config.h"
#include "wine/port.h"

#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <stdarg.h>
#ifdef HAVE_X11_EXTENSIONS_XINPUT2_H
#include <X11/extensions/XInput2.h>
#endif

#ifdef SONAME_LIBXCURSOR
# include <X11/Xcursor/Xcursor.h>
static void *xcursor_handle;
# define MAKE_FUNCPTR(f) static typeof(f) * p##f
MAKE_FUNCPTR(XcursorImageCreate);
MAKE_FUNCPTR(XcursorImageDestroy);
MAKE_FUNCPTR(XcursorImageLoadCursor);
MAKE_FUNCPTR(XcursorImagesCreate);
MAKE_FUNCPTR(XcursorImagesDestroy);
MAKE_FUNCPTR(XcursorImagesLoadCursor);
MAKE_FUNCPTR(XcursorLibraryLoadCursor);
# undef MAKE_FUNCPTR
#endif /* SONAME_LIBXCURSOR */

#define NONAMELESSUNION
#define NONAMELESSSTRUCT
#define OEMRESOURCE
#include "windef.h"
#include "winbase.h"
#include "winreg.h"

#include "x11drv.h"
#include "wine/server.h"
#include "wine/library.h"
#include "wine/unicode.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(cursor);

/**********************************************************************/

#ifndef Button6Mask
#define Button6Mask (1<<13)
#endif
#ifndef Button7Mask
#define Button7Mask (1<<14)
#endif

#define NB_BUTTONS   9     /* Windows can handle 5 buttons and the wheel too */

static const UINT button_down_flags[NB_BUTTONS] =
{
    MOUSEEVENTF_LEFTDOWN,
    MOUSEEVENTF_MIDDLEDOWN,
    MOUSEEVENTF_RIGHTDOWN,
    MOUSEEVENTF_WHEEL,
    MOUSEEVENTF_WHEEL,
    MOUSEEVENTF_XDOWN,  /* FIXME: horizontal wheel */
    MOUSEEVENTF_XDOWN,
    MOUSEEVENTF_XDOWN,
    MOUSEEVENTF_XDOWN
};

static const UINT button_up_flags[NB_BUTTONS] =
{
    MOUSEEVENTF_LEFTUP,
    MOUSEEVENTF_MIDDLEUP,
    MOUSEEVENTF_RIGHTUP,
    0,
    0,
    MOUSEEVENTF_XUP,
    MOUSEEVENTF_XUP,
    MOUSEEVENTF_XUP,
    MOUSEEVENTF_XUP
};

static const UINT button_down_data[NB_BUTTONS] =
{
    0,
    0,
    0,
    WHEEL_DELTA,
    -WHEEL_DELTA,
    XBUTTON1,
    XBUTTON2,
    XBUTTON1,
    XBUTTON2
};

static const UINT button_up_data[NB_BUTTONS] =
{
    0,
    0,
    0,
    0,
    0,
    XBUTTON1,
    XBUTTON2,
    XBUTTON1,
    XBUTTON2
};

static HWND cursor_window;
static HCURSOR last_cursor;
static DWORD last_cursor_change;
static XContext cursor_context;
static RECT clip_rect;
static Cursor create_cursor( HANDLE handle );

#ifdef HAVE_X11_EXTENSIONS_XINPUT2_H
static BOOL xinput2_available;
static int xinput2_core_pointer;
#define MAKE_FUNCPTR(f) static typeof(f) * p##f
MAKE_FUNCPTR(XIFreeDeviceInfo);
MAKE_FUNCPTR(XIQueryDevice);
MAKE_FUNCPTR(XIQueryVersion);
MAKE_FUNCPTR(XISelectEvents);
#undef MAKE_FUNCPTR
#endif

/***********************************************************************
 *		X11DRV_Xcursor_Init
 *
 * Load the Xcursor library for use.
 */
void X11DRV_Xcursor_Init(void)
{
#ifdef SONAME_LIBXCURSOR
    xcursor_handle = wine_dlopen(SONAME_LIBXCURSOR, RTLD_NOW, NULL, 0);
    if (!xcursor_handle)  /* wine_dlopen failed. */
    {
        WARN("Xcursor failed to load.  Using fallback code.\n");
        return;
    }
#define LOAD_FUNCPTR(f) \
        p##f = wine_dlsym(xcursor_handle, #f, NULL, 0)

    LOAD_FUNCPTR(XcursorImageCreate);
    LOAD_FUNCPTR(XcursorImageDestroy);
    LOAD_FUNCPTR(XcursorImageLoadCursor);
    LOAD_FUNCPTR(XcursorImagesCreate);
    LOAD_FUNCPTR(XcursorImagesDestroy);
    LOAD_FUNCPTR(XcursorImagesLoadCursor);
    LOAD_FUNCPTR(XcursorLibraryLoadCursor);
#undef LOAD_FUNCPTR
#endif /* SONAME_LIBXCURSOR */
}


/***********************************************************************
 *		get_empty_cursor
 */
static Cursor get_empty_cursor(void)
{
    static Cursor cursor;
    static const char data[] = { 0 };

    wine_tsx11_lock();
    if (!cursor)
    {
        XColor bg;
        Pixmap pixmap;

        bg.red = bg.green = bg.blue = 0x0000;
        pixmap = XCreateBitmapFromData( gdi_display, root_window, data, 1, 1 );
        if (pixmap)
        {
            cursor = XCreatePixmapCursor( gdi_display, pixmap, pixmap, &bg, &bg, 0, 0 );
            XFreePixmap( gdi_display, pixmap );
        }
    }
    wine_tsx11_unlock();
    return cursor;
}

/***********************************************************************
 *		set_window_cursor
 */
void set_window_cursor( Window window, HCURSOR handle )
{
    Cursor cursor, prev;

    wine_tsx11_lock();
    if (!handle) cursor = get_empty_cursor();
    else if (!cursor_context || XFindContext( gdi_display, (XID)handle, cursor_context, (char **)&cursor ))
    {
        /* try to create it */
        wine_tsx11_unlock();
        if (!(cursor = create_cursor( handle ))) return;

        wine_tsx11_lock();
        if (!cursor_context) cursor_context = XUniqueContext();
        if (!XFindContext( gdi_display, (XID)handle, cursor_context, (char **)&prev ))
        {
            /* someone else was here first */
            XFreeCursor( gdi_display, cursor );
            cursor = prev;
        }
        else
        {
            XSaveContext( gdi_display, (XID)handle, cursor_context, (char *)cursor );
            TRACE( "cursor %p created %lx\n", handle, cursor );
        }
    }

    XDefineCursor( gdi_display, window, cursor );
    /* make the change take effect immediately */
    XFlush( gdi_display );
    wine_tsx11_unlock();
}

/***********************************************************************
 *              sync_window_cursor
 */
void sync_window_cursor( Window window )
{
    HCURSOR cursor;

    SERVER_START_REQ( set_cursor )
    {
        req->flags = 0;
        wine_server_call( req );
        cursor = reply->prev_count >= 0 ? wine_server_ptr_handle( reply->prev_handle ) : 0;
    }
    SERVER_END_REQ;

    set_window_cursor( window, cursor );
}

/***********************************************************************
 *              enable_xinput2
 */
static void enable_xinput2(void)
{
#ifdef HAVE_X11_EXTENSIONS_XINPUT2_H
    struct x11drv_thread_data *data = x11drv_thread_data();
    XIDeviceInfo *devices;
    XIEventMask mask;
    unsigned char mask_bits[XIMaskLen(XI_LASTEVENT)];
    int i, count;

    if (!xinput2_available) return;

    if (data->xi2_state == xi_unknown)
    {
        int major = 2, minor = 0;
        wine_tsx11_lock();
        if (!pXIQueryVersion( data->display, &major, &minor )) data->xi2_state = xi_disabled;
        else
        {
            data->xi2_state = xi_unavailable;
            WARN( "X Input 2 not available\n" );
        }
        wine_tsx11_unlock();
    }
    if (data->xi2_state == xi_unavailable) return;

    wine_tsx11_lock();
    devices = pXIQueryDevice( data->display, XIAllDevices, &count );
    for (i = 0; i < count; ++i)
    {
        if (devices[i].use != XIMasterPointer) continue;
        TRACE( "Using %u (%s) as core pointer\n",
               devices[i].deviceid, debugstr_a(devices[i].name) );
        xinput2_core_pointer = devices[i].deviceid;
        break;
    }

    mask.mask     = mask_bits;
    mask.mask_len = sizeof(mask_bits);
    memset( mask_bits, 0, sizeof(mask_bits) );

    XISetMask( mask_bits, XI_RawButtonPress );
    XISetMask( mask_bits, XI_RawButtonRelease );
    XISetMask( mask_bits, XI_RawMotion );

    for (i = 0; i < count; ++i)
    {
        if (devices[i].use == XISlavePointer && devices[i].attachment == xinput2_core_pointer)
        {
            TRACE( "Device %u (%s) is attached to the core pointer\n",
                   devices[i].deviceid, debugstr_a(devices[i].name) );
            mask.deviceid = devices[i].deviceid;
            pXISelectEvents( data->display, DefaultRootWindow( data->display ), &mask, 1 );
            data->xi2_state = xi_enabled;
        }
    }

    pXIFreeDeviceInfo( devices );
    wine_tsx11_unlock();
#endif
}

/***********************************************************************
 *              disable_xinput2
 */
static void disable_xinput2(void)
{
#ifdef HAVE_X11_EXTENSIONS_XINPUT2_H
    struct x11drv_thread_data *data = x11drv_thread_data();
    XIEventMask mask;
    XIDeviceInfo *devices;
    int i, count;

    if (data->xi2_state != xi_enabled) return;

    TRACE( "disabling\n" );
    data->xi2_state = xi_disabled;

    mask.mask = NULL;
    mask.mask_len = 0;

    wine_tsx11_lock();
    devices = pXIQueryDevice( data->display, XIAllDevices, &count );
    for (i = 0; i < count; ++i)
    {
        if (devices[i].use == XISlavePointer && devices[i].attachment == xinput2_core_pointer)
        {
            mask.deviceid = devices[i].deviceid;
            pXISelectEvents( data->display, DefaultRootWindow( data->display ), &mask, 1 );
        }
    }
    pXIFreeDeviceInfo( devices );
    wine_tsx11_unlock();
#endif
}

/***********************************************************************
 *             create_clipping_msg_window
 */
static HWND create_clipping_msg_window(void)
{
    static const WCHAR class_name[] = {'_','_','x','1','1','d','r','v','_','c','l','i','p','_','c','l','a','s','s',0};
    static ATOM clip_class;

    if (!clip_class)
    {
        WNDCLASSW class;
        ATOM atom;

        memset( &class, 0, sizeof(class) );
        class.lpfnWndProc   = DefWindowProcW;
        class.hInstance     = GetModuleHandleW(0);
        class.lpszClassName = class_name;
        if ((atom = RegisterClassW( &class ))) clip_class = atom;
    }
    return CreateWindowW( class_name, NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, 0, GetModuleHandleW(0), NULL );
}

/***********************************************************************
 *		grab_clipping_window
 *
 * Start a pointer grab on the clip window.
 */
static BOOL grab_clipping_window( const RECT *clip )
{
    struct x11drv_thread_data *data = x11drv_thread_data();
    Window clip_window;
    HWND msg_hwnd = 0;

    if (!data) return FALSE;
    if (!(clip_window = init_clip_window())) return TRUE;

    /* create a clip message window unless we are already clipping */
    if (!data->clip_hwnd)
    {
        if (!(msg_hwnd = create_clipping_msg_window())) return TRUE;
        enable_xinput2();
    }

    /* don't clip to 1x1 rectangle if we don't have XInput */
    if (data->xi2_state != xi_enabled && clip->right - clip->left == 1 && clip->bottom - clip->top == 1)
    {
        WARN( "XInput2 not supported, refusing to clip to %s\n", wine_dbgstr_rect(clip) );
        if (msg_hwnd) DestroyWindow( msg_hwnd );
        ClipCursor( NULL );
        return TRUE;
    }

    TRACE( "clipping to %s\n", wine_dbgstr_rect(clip) );

    wine_tsx11_lock();
    if (msg_hwnd) XUnmapWindow( data->display, clip_window );
    XMoveResizeWindow( data->display, clip_window,
                       clip->left - virtual_screen_rect.left, clip->top - virtual_screen_rect.top,
                       clip->right - clip->left, clip->bottom - clip->top );
    if (msg_hwnd) XMapWindow( data->display, clip_window );
    if (!XGrabPointer( data->display, clip_window, False,
                       PointerMotionMask | ButtonPressMask | ButtonReleaseMask,
                       GrabModeAsync, GrabModeAsync, clip_window, None, CurrentTime ))
        clipping_cursor = 1;
    wine_tsx11_unlock();

    if (!clipping_cursor)
    {
        disable_xinput2();
        if (msg_hwnd) DestroyWindow( msg_hwnd );
        return FALSE;
    }
    if (msg_hwnd)
    {
        data->clip_hwnd = msg_hwnd;
        sync_window_cursor( clip_window );
        clip_rect = *clip;
        SendMessageW( GetDesktopWindow(), WM_X11DRV_CLIP_CURSOR, 0, (LPARAM)msg_hwnd );
    }
    return TRUE;
}

/***********************************************************************
 *		ungrab_clipping_window
 *
 * Release the pointer grab on the clip window.
 */
void ungrab_clipping_window(void)
{
    Display *display = thread_init_display();
    Window clip_window = init_clip_window();

    if (!clip_window) return;

    TRACE( "no longer clipping\n" );
    wine_tsx11_lock();
    XUnmapWindow( display, clip_window );
    wine_tsx11_unlock();
    clipping_cursor = 0;
    SendMessageW( GetDesktopWindow(), WM_X11DRV_CLIP_CURSOR, 0, 0 );
}

/***********************************************************************
 *             clip_cursor_notify
 *
 * Notification function called upon receiving a WM_X11DRV_CLIP_CURSOR.
 */
LRESULT clip_cursor_notify( HWND hwnd, HWND new_clip_hwnd )
{
    struct x11drv_thread_data *data = x11drv_thread_data();

    if (hwnd == GetDesktopWindow())  /* change the clip window stored in the desktop process */
    {
        static HWND clip_hwnd;

        HWND prev = clip_hwnd;
        clip_hwnd = new_clip_hwnd;
        if (prev || new_clip_hwnd) TRACE( "clip hwnd changed from %p to %p\n", prev, new_clip_hwnd );
        if (prev) SendNotifyMessageW( prev, WM_X11DRV_CLIP_CURSOR, 0, 0 );
    }
    else if (hwnd == data->clip_hwnd)  /* this is a notification that clipping has been reset */
    {
        data->clip_hwnd = 0;
        disable_xinput2();
        DestroyWindow( hwnd );
    }
    else if (hwnd == GetForegroundWindow())  /* request to clip */
    {
        RECT clip;

        GetClipCursor( &clip );
        if (clip.left > virtual_screen_rect.left || clip.right < virtual_screen_rect.right ||
            clip.top > virtual_screen_rect.top   || clip.bottom < virtual_screen_rect.bottom)
            return grab_clipping_window( &clip );
    }
    return 0;
}

/***********************************************************************
 *		send_mouse_input
 *
 * Update the various window states on a mouse event.
 */
static void send_mouse_input( HWND hwnd, Window window, unsigned int state, INPUT *input )
{
    struct x11drv_win_data *data;
    POINT pt;

    input->type = INPUT_MOUSE;

    if (!hwnd && window == x11drv_thread_data()->clip_window)
    {
        input->u.mi.dx += clip_rect.left;
        input->u.mi.dy += clip_rect.top;
        if (x11drv_thread_data()->xi2_state != xi_enabled) __wine_send_input( hwnd, input );
        return;
    }

    if (!(data = X11DRV_get_win_data( hwnd ))) return;

    if (window == data->whole_window)
    {
        input->u.mi.dx += data->whole_rect.left - data->client_rect.left;
        input->u.mi.dy += data->whole_rect.top - data->client_rect.top;
    }
    if (window == root_window)
    {
        input->u.mi.dx += virtual_screen_rect.left;
        input->u.mi.dy += virtual_screen_rect.top;
    }
    pt.x = input->u.mi.dx;
    pt.y = input->u.mi.dy;
    if (GetWindowLongW( data->hwnd, GWL_EXSTYLE ) & WS_EX_LAYOUTRTL)
        pt.x = data->client_rect.right - data->client_rect.left - 1 - pt.x;
    MapWindowPoints( hwnd, 0, &pt, 1 );

    if (InterlockedExchangePointer( (void **)&cursor_window, hwnd ) != hwnd ||
        GetTickCount() - last_cursor_change > 100)
    {
        sync_window_cursor( data->whole_window );
        last_cursor_change = GetTickCount();
    }

    if (hwnd != GetDesktopWindow()) hwnd = GetAncestor( hwnd, GA_ROOT );

    /* update the wine server Z-order */

    if (window != x11drv_thread_data()->grab_window &&
        /* ignore event if a button is pressed, since the mouse is then grabbed too */
        !(state & (Button1Mask|Button2Mask|Button3Mask|Button4Mask|Button5Mask|Button6Mask|Button7Mask)))
    {
        RECT rect;
        SetRect( &rect, pt.x, pt.y, pt.x + 1, pt.y + 1 );
        MapWindowPoints( 0, hwnd, (POINT *)&rect, 2 );

        SERVER_START_REQ( update_window_zorder )
        {
            req->window      = wine_server_user_handle( hwnd );
            req->rect.left   = rect.left;
            req->rect.top    = rect.top;
            req->rect.right  = rect.right;
            req->rect.bottom = rect.bottom;
            wine_server_call( req );
        }
        SERVER_END_REQ;
    }

    input->u.mi.dx = pt.x;
    input->u.mi.dy = pt.y;
    __wine_send_input( hwnd, input );
}

#ifdef SONAME_LIBXCURSOR

/***********************************************************************
 *              create_xcursor_frame
 *
 * Use Xcursor to create a frame of an X cursor from a Windows one.
 */
static XcursorImage *create_xcursor_frame( HDC hdc, const ICONINFOEXW *iinfo, HANDLE icon,
                                           HBITMAP hbmColor, unsigned char *color_bits, int color_size,
                                           HBITMAP hbmMask, unsigned char *mask_bits, int mask_size,
                                           int width, int height, int istep )
{
    XcursorImage *image, *ret = NULL;
    DWORD delay_jiffies, num_steps;
    int x, y, i, has_alpha = FALSE;
    XcursorPixel *ptr;

    wine_tsx11_lock();
    image = pXcursorImageCreate( width, height );
    wine_tsx11_unlock();
    if (!image)
    {
        ERR("X11 failed to produce a cursor frame!\n");
        goto cleanup;
    }

    image->xhot = iinfo->xHotspot;
    image->yhot = iinfo->yHotspot;

    image->delay = 100; /* fallback delay, 100 ms */
    if (GetCursorFrameInfo(icon, 0x0 /* unknown parameter */, istep, &delay_jiffies, &num_steps) != 0)
        image->delay = (100 * delay_jiffies) / 6; /* convert jiffies (1/60s) to milliseconds */
    else
        WARN("Failed to retrieve animated cursor frame-rate for frame %d.\n", istep);

    /* draw the cursor frame to a temporary buffer then copy it into the XcursorImage */
    memset( color_bits, 0x00, color_size );
    SelectObject( hdc, hbmColor );
    if (!DrawIconEx( hdc, 0, 0, icon, width, height, istep, NULL, DI_NORMAL ))
    {
        TRACE("Could not draw frame %d (walk past end of frames).\n", istep);
        goto cleanup;
    }
    memcpy( image->pixels, color_bits, color_size );

    /* check if the cursor frame was drawn with an alpha channel */
    for (i = 0, ptr = image->pixels; i < width * height; i++, ptr++)
        if ((has_alpha = (*ptr & 0xff000000) != 0)) break;

    /* if no alpha channel was drawn then generate it from the mask */
    if (!has_alpha)
    {
        unsigned int width_bytes = (width + 31) / 32 * 4;

        /* draw the cursor mask to a temporary buffer */
        memset( mask_bits, 0xFF, mask_size );
        SelectObject( hdc, hbmMask );
        if (!DrawIconEx( hdc, 0, 0, icon, width, height, istep, NULL, DI_MASK ))
        {
            ERR("Failed to draw frame mask %d.\n", istep);
            goto cleanup;
        }
        /* use the buffer to directly modify the XcursorImage alpha channel */
        for (y = 0, ptr = image->pixels; y < height; y++)
            for (x = 0; x < width; x++, ptr++)
                if (!((mask_bits[y * width_bytes + x / 8] << (x % 8)) & 0x80))
                    *ptr |= 0xff000000;
    }
    ret = image;

cleanup:
    if (ret == NULL) pXcursorImageDestroy( image );
    return ret;
}

/***********************************************************************
 *              create_xcursor_cursor
 *
 * Use Xcursor to create an X cursor from a Windows one.
 */
static Cursor create_xcursor_cursor( HDC hdc, const ICONINFOEXW *iinfo, HANDLE icon, int width, int height )
{
    unsigned char *color_bits, *mask_bits;
    HBITMAP hbmColor = 0, hbmMask = 0;
    DWORD nFrames, delay_jiffies, i;
    int color_size, mask_size;
    BITMAPINFO *info = NULL;
    XcursorImages *images;
    XcursorImage **imgs;
    Cursor cursor = 0;

    /* Retrieve the number of frames to render */
    if (!GetCursorFrameInfo(icon, 0x0 /* unknown parameter */, 0, &delay_jiffies, &nFrames)) return 0;
    if (!(imgs = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(XcursorImage*)*nFrames ))) return 0;

    /* Allocate all of the resources necessary to obtain a cursor frame */
    if (!(info = HeapAlloc( GetProcessHeap(), 0, FIELD_OFFSET( BITMAPINFO, bmiColors[256] )))) goto cleanup;
    info->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info->bmiHeader.biWidth = width;
    info->bmiHeader.biHeight = -height;
    info->bmiHeader.biPlanes = 1;
    info->bmiHeader.biCompression = BI_RGB;
    info->bmiHeader.biXPelsPerMeter = 0;
    info->bmiHeader.biYPelsPerMeter = 0;
    info->bmiHeader.biClrUsed = 0;
    info->bmiHeader.biClrImportant = 0;
    info->bmiHeader.biBitCount = 32;
    color_size = width * height * 4;
    info->bmiHeader.biSizeImage = color_size;
    hbmColor = CreateDIBSection( hdc, info, DIB_RGB_COLORS, (VOID **) &color_bits, NULL, 0);
    if (!hbmColor)
    {
        ERR("Failed to create DIB section for cursor color data!\n");
        goto cleanup;
    }
    info->bmiHeader.biBitCount = 1;
    mask_size = ((width + 31) / 32 * 4) * height; /* width_bytes * height */
    info->bmiHeader.biSizeImage = mask_size;
    hbmMask = CreateDIBSection( hdc, info, DIB_RGB_COLORS, (VOID **) &mask_bits, NULL, 0);
    if (!hbmMask)
    {
        ERR("Failed to create DIB section for cursor mask data!\n");
        goto cleanup;
    }

    /* Create an XcursorImage for each frame of the cursor */
    for (i=0; i<nFrames; i++)
    {
        imgs[i] = create_xcursor_frame( hdc, iinfo, icon,
                                        hbmColor, color_bits, color_size,
                                        hbmMask, mask_bits, mask_size,
                                        width, height, i );
        if (!imgs[i]) goto cleanup;
    }

    /* Build an X cursor out of all of the frames */
    if (!(images = pXcursorImagesCreate( nFrames ))) goto cleanup;
    for (images->nimage = 0; images->nimage < nFrames; images->nimage++)
        images->images[images->nimage] = imgs[images->nimage];
    wine_tsx11_lock();
    cursor = pXcursorImagesLoadCursor( gdi_display, images );
    wine_tsx11_unlock();
    pXcursorImagesDestroy( images ); /* Note: this frees each individual frame (calls XcursorImageDestroy) */
    HeapFree( GetProcessHeap(), 0, imgs );
    imgs = NULL;

cleanup:
    if (imgs)
    {
        /* Failed to produce a cursor, free previously allocated frames */
        for (i=0; i<nFrames && imgs[i]; i++)
            pXcursorImageDestroy( imgs[i] );
        HeapFree( GetProcessHeap(), 0, imgs );
    }
    /* Cleanup all of the resources used to obtain the frame data */
    if (hbmColor) DeleteObject( hbmColor );
    if (hbmMask) DeleteObject( hbmMask );
    HeapFree( GetProcessHeap(), 0, info );
    return cursor;
}


struct system_cursors
{
    WORD id;
    const char *name;
};

static const struct system_cursors user32_cursors[] =
{
    { OCR_NORMAL,      "left_ptr" },
    { OCR_IBEAM,       "xterm" },
    { OCR_WAIT,        "watch" },
    { OCR_CROSS,       "cross" },
    { OCR_UP,          "center_ptr" },
    { OCR_SIZE,        "fleur" },
    { OCR_SIZEALL,     "fleur" },
    { OCR_ICON,        "icon" },
    { OCR_SIZENWSE,    "nwse-resize" },
    { OCR_SIZENESW,    "nesw-resize" },
    { OCR_SIZEWE,      "ew-resize" },
    { OCR_SIZENS,      "ns-resize" },
    { OCR_NO,          "not-allowed" },
    { OCR_HAND,        "hand2" },
    { OCR_APPSTARTING, "left_ptr_watch" },
    { OCR_HELP,        "question_arrow" },
    { 0 }
};

static const struct system_cursors comctl32_cursors[] =
{
    { 102, "move" },
    { 104, "copy" },
    { 105, "left_ptr" },
    { 106, "row-resize" },
    { 107, "row-resize" },
    { 108, "hand2" },
    { 135, "col-resize" },
    { 0 }
};

static const struct system_cursors ole32_cursors[] =
{
    { 1, "no-drop" },
    { 2, "move" },
    { 3, "copy" },
    { 4, "alias" },
    { 0 }
};

static const struct system_cursors riched20_cursors[] =
{
    { 105, "hand2" },
    { 107, "right_ptr" },
    { 109, "copy" },
    { 110, "move" },
    { 111, "no-drop" },
    { 0 }
};

static const struct
{
    const struct system_cursors *cursors;
    WCHAR name[16];
} module_cursors[] =
{
    { user32_cursors, {'u','s','e','r','3','2','.','d','l','l',0} },
    { comctl32_cursors, {'c','o','m','c','t','l','3','2','.','d','l','l',0} },
    { ole32_cursors, {'o','l','e','3','2','.','d','l','l',0} },
    { riched20_cursors, {'r','i','c','h','e','d','2','0','.','d','l','l',0} }
};

/***********************************************************************
 *		create_xcursor_system_cursor
 *
 * Create an X cursor for a system cursor.
 */
static Cursor create_xcursor_system_cursor( const ICONINFOEXW *info )
{
    static const WCHAR idW[] = {'%','h','u',0};
    const struct system_cursors *cursors;
    unsigned int i;
    Cursor cursor = 0;
    HMODULE module;
    HKEY key;
    WCHAR *p, name[MAX_PATH * 2], valueW[64];
    char valueA[64];
    DWORD size, ret;

    if (!pXcursorLibraryLoadCursor) return 0;
    if (!info->szModName[0]) return 0;

    p = strrchrW( info->szModName, '\\' );
    strcpyW( name, p ? p + 1 : info->szModName );
    p = name + strlenW( name );
    *p++ = ',';
    if (info->szResName[0]) strcpyW( p, info->szResName );
    else sprintfW( p, idW, info->wResID );
    valueA[0] = 0;

    /* @@ Wine registry key: HKCU\Software\Wine\X11 Driver\Cursors */
    if (!RegOpenKeyA( HKEY_CURRENT_USER, "Software\\Wine\\X11 Driver\\Cursors", &key ))
    {
        size = sizeof(valueW) / sizeof(WCHAR);
        ret = RegQueryValueExW( key, name, NULL, NULL, (BYTE *)valueW, &size );
        RegCloseKey( key );
        if (!ret)
        {
            if (!valueW[0]) return 0; /* force standard cursor */
            if (!WideCharToMultiByte( CP_UNIXCP, 0, valueW, -1, valueA, sizeof(valueA), NULL, NULL ))
                valueA[0] = 0;
            goto done;
        }
    }

    if (info->szResName[0]) goto done;  /* only integer resources are supported here */
    if (!(module = GetModuleHandleW( info->szModName ))) goto done;

    for (i = 0; i < sizeof(module_cursors)/sizeof(module_cursors[0]); i++)
        if (GetModuleHandleW( module_cursors[i].name ) == module) break;
    if (i == sizeof(module_cursors)/sizeof(module_cursors[0])) goto done;

    cursors = module_cursors[i].cursors;
    for (i = 0; cursors[i].id; i++)
        if (cursors[i].id == info->wResID)
        {
            strcpy( valueA, cursors[i].name );
            break;
        }

done:
    if (valueA[0])
    {
        wine_tsx11_lock();
        cursor = pXcursorLibraryLoadCursor( gdi_display, valueA );
        wine_tsx11_unlock();
        if (!cursor) WARN( "no system cursor found for %s mapped to %s\n",
                           debugstr_w(name), debugstr_a(valueA) );
    }
    else WARN( "no system cursor found for %s\n", debugstr_w(name) );
    return cursor;
}

#endif /* SONAME_LIBXCURSOR */


/***********************************************************************
 *		create_cursor_from_bitmaps
 *
 * Create an X11 cursor from source bitmaps.
 */
static Cursor create_cursor_from_bitmaps( HBITMAP src_xor, HBITMAP src_and, int width, int height,
                                          int xor_y, int and_y, XColor *fg, XColor *bg,
                                          int hotspot_x, int hotspot_y )
{
    HDC src = 0, dst = 0;
    HBITMAP bits = 0, mask = 0, mask_inv = 0;
    Cursor cursor = 0;

    if (!(src = CreateCompatibleDC( 0 ))) goto done;
    if (!(dst = CreateCompatibleDC( 0 ))) goto done;

    if (!(bits = CreateBitmap( width, height, 1, 1, NULL ))) goto done;
    if (!(mask = CreateBitmap( width, height, 1, 1, NULL ))) goto done;
    if (!(mask_inv = CreateBitmap( width, height, 1, 1, NULL ))) goto done;

    /* We have to do some magic here, as cursors are not fully
     * compatible between Windows and X11. Under X11, there are
     * only 3 possible color cursor: black, white and masked. So
     * we map the 4th Windows color (invert the bits on the screen)
     * to black and an additional white bit on an other place
     * (+1,+1). This require some boolean arithmetic:
     *
     *         Windows          |          X11
     * And    Xor      Result   |   Bits     Mask     Result
     *  0      0     black      |    0        1     background
     *  0      1     white      |    1        1     foreground
     *  1      0     no change  |    X        0     no change
     *  1      1     inverted   |    0        1     background
     *
     * which gives:
     *  Bits = not 'And' and 'Xor' or 'And2' and 'Xor2'
     *  Mask = not 'And' or 'Xor' or 'And2' and 'Xor2'
     */
    SelectObject( src, src_and );
    SelectObject( dst, bits );
    BitBlt( dst, 0, 0, width, height, src, 0, and_y, SRCCOPY );
    SelectObject( dst, mask );
    BitBlt( dst, 0, 0, width, height, src, 0, and_y, SRCCOPY );
    SelectObject( dst, mask_inv );
    BitBlt( dst, 0, 0, width, height, src, 0, and_y, SRCCOPY );
    SelectObject( src, src_xor );
    BitBlt( dst, 0, 0, width, height, src, 0, xor_y, SRCAND /* src & dst */ );
    SelectObject( dst, bits );
    BitBlt( dst, 0, 0, width, height, src, 0, xor_y, SRCERASE /* src & ~dst */ );
    SelectObject( dst, mask );
    BitBlt( dst, 0, 0, width, height, src, 0, xor_y, 0xdd0228 /* src | ~dst */ );
    /* additional white */
    SelectObject( src, mask_inv );
    BitBlt( dst, 1, 1, width, height, src, 0, 0, SRCPAINT /* src | dst */);
    SelectObject( dst, bits );
    BitBlt( dst, 1, 1, width, height, src, 0, 0, SRCPAINT /* src | dst */ );

    wine_tsx11_lock();
    cursor = XCreatePixmapCursor( gdi_display, X11DRV_get_pixmap(bits), X11DRV_get_pixmap(mask),
                                  fg, bg, hotspot_x, hotspot_y );
    wine_tsx11_unlock();

done:
    DeleteDC( src );
    DeleteDC( dst );
    DeleteObject( bits );
    DeleteObject( mask );
    DeleteObject( mask_inv );
    return cursor;
}

/***********************************************************************
 *		create_xlib_cursor
 *
 * Create an X cursor from a Windows one.
 */
static Cursor create_xlib_cursor( HDC hdc, const ICONINFOEXW *icon, int width, int height )
{
    XColor fg, bg;
    Cursor cursor = None;
    HBITMAP xor_bitmap = 0;
    BITMAPINFO *info;
    unsigned int *color_bits = NULL, *ptr;
    unsigned char *mask_bits = NULL, *xor_bits = NULL;
    int i, x, y, has_alpha = 0;
    int rfg, gfg, bfg, rbg, gbg, bbg, fgBits, bgBits;
    unsigned int width_bytes = (width + 31) / 32 * 4;

    if (!(info = HeapAlloc( GetProcessHeap(), 0, FIELD_OFFSET( BITMAPINFO, bmiColors[256] ))))
        return FALSE;
    info->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info->bmiHeader.biWidth = width;
    info->bmiHeader.biHeight = -height;
    info->bmiHeader.biPlanes = 1;
    info->bmiHeader.biBitCount = 1;
    info->bmiHeader.biCompression = BI_RGB;
    info->bmiHeader.biSizeImage = width_bytes * height;
    info->bmiHeader.biXPelsPerMeter = 0;
    info->bmiHeader.biYPelsPerMeter = 0;
    info->bmiHeader.biClrUsed = 0;
    info->bmiHeader.biClrImportant = 0;

    if (!(mask_bits = HeapAlloc( GetProcessHeap(), 0, info->bmiHeader.biSizeImage ))) goto done;
    if (!GetDIBits( hdc, icon->hbmMask, 0, height, mask_bits, info, DIB_RGB_COLORS )) goto done;

    info->bmiHeader.biBitCount = 32;
    info->bmiHeader.biSizeImage = width * height * 4;
    if (!(color_bits = HeapAlloc( GetProcessHeap(), 0, info->bmiHeader.biSizeImage ))) goto done;
    if (!(xor_bits = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, width_bytes * height ))) goto done;
    GetDIBits( hdc, icon->hbmColor, 0, height, color_bits, info, DIB_RGB_COLORS );

    /* compute fg/bg color and xor bitmap based on average of the color values */

    if (!(xor_bitmap = CreateBitmap( width, height, 1, 1, NULL ))) goto done;
    rfg = gfg = bfg = rbg = gbg = bbg = fgBits = 0;
    for (y = 0, ptr = color_bits; y < height; y++)
    {
        for (x = 0; x < width; x++, ptr++)
        {
            int red   = (*ptr >> 16) & 0xff;
            int green = (*ptr >> 8) & 0xff;
            int blue  = (*ptr >> 0) & 0xff;
            if (red + green + blue > 0x40)
            {
                rfg += red;
                gfg += green;
                bfg += blue;
                fgBits++;
                xor_bits[y * width_bytes + x / 8] |= 0x80 >> (x % 8);
            }
            else
            {
                rbg += red;
                gbg += green;
                bbg += blue;
            }
        }
    }
    if (fgBits)
    {
        fg.red   = rfg * 257 / fgBits;
        fg.green = gfg * 257 / fgBits;
        fg.blue  = bfg * 257 / fgBits;
    }
    else fg.red = fg.green = fg.blue = 0;
    bgBits = width * height - fgBits;
    if (bgBits)
    {
        bg.red   = rbg * 257 / bgBits;
        bg.green = gbg * 257 / bgBits;
        bg.blue  = bbg * 257 / bgBits;
    }
    else bg.red = bg.green = bg.blue = 0;

    info->bmiHeader.biBitCount = 1;
    info->bmiHeader.biSizeImage = width_bytes * height;
    SetDIBits( hdc, xor_bitmap, 0, height, xor_bits, info, DIB_RGB_COLORS );

    /* generate mask from the alpha channel if we have one */

    for (i = 0, ptr = color_bits; i < width * height; i++, ptr++)
        if ((has_alpha = (*ptr & 0xff000000) != 0)) break;

    if (has_alpha)
    {
        memset( mask_bits, 0, width_bytes * height );
        for (y = 0, ptr = color_bits; y < height; y++)
            for (x = 0; x < width; x++, ptr++)
                if ((*ptr >> 24) > 25) /* more than 10% alpha */
                    mask_bits[y * width_bytes + x / 8] |= 0x80 >> (x % 8);

        info->bmiHeader.biBitCount = 1;
        info->bmiHeader.biSizeImage = width_bytes * height;
        SetDIBits( hdc, icon->hbmMask, 0, height, mask_bits, info, DIB_RGB_COLORS );

        wine_tsx11_lock();
        cursor = XCreatePixmapCursor( gdi_display,
                                      X11DRV_get_pixmap(xor_bitmap),
                                      X11DRV_get_pixmap(icon->hbmMask),
                                      &fg, &bg, icon->xHotspot, icon->yHotspot );
        wine_tsx11_unlock();
    }
    else
    {
        cursor = create_cursor_from_bitmaps( xor_bitmap, icon->hbmMask, width, height, 0, 0,
                                             &fg, &bg, icon->xHotspot, icon->yHotspot );
    }

done:
    DeleteObject( xor_bitmap );
    HeapFree( GetProcessHeap(), 0, info );
    HeapFree( GetProcessHeap(), 0, color_bits );
    HeapFree( GetProcessHeap(), 0, xor_bits );
    HeapFree( GetProcessHeap(), 0, mask_bits );
    return cursor;
}

/***********************************************************************
 *		create_cursor
 *
 * Create an X cursor from a Windows one.
 */
static Cursor create_cursor( HANDLE handle )
{
    Cursor cursor = 0;
    ICONINFOEXW info;
    BITMAP bm;

    if (!handle) return get_empty_cursor();

    info.cbSize = sizeof(info);
    if (!GetIconInfoExW( handle, &info )) return 0;

#ifdef SONAME_LIBXCURSOR
    if (use_system_cursors && (cursor = create_xcursor_system_cursor( &info )))
    {
        DeleteObject( info.hbmColor );
        DeleteObject( info.hbmMask );
        return cursor;
    }
#endif

    GetObjectW( info.hbmMask, sizeof(bm), &bm );
    if (!info.hbmColor) bm.bmHeight /= 2;

    /* make sure hotspot is valid */
    if (info.xHotspot >= bm.bmWidth || info.yHotspot >= bm.bmHeight)
    {
        info.xHotspot = bm.bmWidth / 2;
        info.yHotspot = bm.bmHeight / 2;
    }

    if (info.hbmColor)
    {
        HDC hdc = CreateCompatibleDC( 0 );
        if (hdc)
        {
#ifdef SONAME_LIBXCURSOR
            if (pXcursorImagesLoadCursor)
                cursor = create_xcursor_cursor( hdc, &info, handle, bm.bmWidth, bm.bmHeight );
#endif
            if (!cursor) cursor = create_xlib_cursor( hdc, &info, bm.bmWidth, bm.bmHeight );
        }
        DeleteObject( info.hbmColor );
        DeleteDC( hdc );
    }
    else
    {
        XColor fg, bg;
        fg.red = fg.green = fg.blue = 0xffff;
        bg.red = bg.green = bg.blue = 0;
        cursor = create_cursor_from_bitmaps( info.hbmMask, info.hbmMask, bm.bmWidth, bm.bmHeight,
                                             bm.bmHeight, 0, &fg, &bg, info.xHotspot, info.yHotspot );
    }

    DeleteObject( info.hbmMask );
    return cursor;
}

/***********************************************************************
 *		DestroyCursorIcon (X11DRV.@)
 */
void CDECL X11DRV_DestroyCursorIcon( HCURSOR handle )
{
    Cursor cursor;

    wine_tsx11_lock();
    if (cursor_context && !XFindContext( gdi_display, (XID)handle, cursor_context, (char **)&cursor ))
    {
        TRACE( "%p xid %lx\n", handle, cursor );
        XFreeCursor( gdi_display, cursor );
        XDeleteContext( gdi_display, (XID)handle, cursor_context );
    }
    wine_tsx11_unlock();
}

/***********************************************************************
 *		SetCursor (X11DRV.@)
 */
void CDECL X11DRV_SetCursor( HCURSOR handle )
{
    if (InterlockedExchangePointer( (void **)&last_cursor, handle ) != handle ||
        GetTickCount() - last_cursor_change > 100)
    {
        last_cursor_change = GetTickCount();
        if (clipping_cursor) set_window_cursor( init_clip_window(), handle );
        else if (cursor_window) SendNotifyMessageW( cursor_window, WM_X11DRV_SET_CURSOR, 0, (LPARAM)handle );
    }
}

/***********************************************************************
 *		SetCursorPos (X11DRV.@)
 */
BOOL CDECL X11DRV_SetCursorPos( INT x, INT y )
{
    struct x11drv_thread_data *data = x11drv_init_thread_data();

    if (data->xi2_state == xi_enabled) return TRUE;

    TRACE( "warping to (%d,%d)\n", x, y );

    wine_tsx11_lock();
    XWarpPointer( data->display, root_window, root_window, 0, 0, 0, 0,
                  x - virtual_screen_rect.left, y - virtual_screen_rect.top );
    XFlush( data->display ); /* avoids bad mouse lag in games that do their own mouse warping */
    wine_tsx11_unlock();
    return TRUE;
}

/***********************************************************************
 *		GetCursorPos (X11DRV.@)
 */
BOOL CDECL X11DRV_GetCursorPos(LPPOINT pos)
{
    Display *display = thread_init_display();
    Window root, child;
    int rootX, rootY, winX, winY;
    unsigned int xstate;
    BOOL ret;

    wine_tsx11_lock();
    ret = XQueryPointer( display, root_window, &root, &child, &rootX, &rootY, &winX, &winY, &xstate );
    if (ret)
    {
        pos->x = winX + virtual_screen_rect.left;
        pos->y = winY + virtual_screen_rect.top;
        TRACE("pointer at (%d,%d)\n", pos->x, pos->y );
    }
    wine_tsx11_unlock();
    return ret;
}

/***********************************************************************
 *		ClipCursor (X11DRV.@)
 */
BOOL CDECL X11DRV_ClipCursor( LPCRECT clip )
{
    if (!clip)
    {
        ungrab_clipping_window();
        return TRUE;
    }

    if (GetWindowThreadProcessId( GetDesktopWindow(), NULL ) == GetCurrentThreadId())
        return TRUE;  /* don't clip in the desktop process */

    /* we are clipping if the clip rectangle is smaller than the screen */
    if (grab_pointer && (clip->left > virtual_screen_rect.left ||
                         clip->right < virtual_screen_rect.right ||
                         clip->top > virtual_screen_rect.top ||
                         clip->bottom < virtual_screen_rect.bottom))
    {
        DWORD tid, pid;
        HWND foreground = GetForegroundWindow();

        /* forward request to the foreground window if it's in a different thread */
        tid = GetWindowThreadProcessId( foreground, &pid );
        if (tid && tid != GetCurrentThreadId() && pid == GetCurrentProcessId())
        {
            TRACE( "forwarding clip request to %p\n", foreground );
            if (SendMessageW( foreground, WM_X11DRV_CLIP_CURSOR, 0, 0 )) return TRUE;
        }
        else if (grab_clipping_window( clip )) return TRUE;
    }

    ungrab_clipping_window();
    return TRUE;
}

/***********************************************************************
 *           X11DRV_ButtonPress
 */
void X11DRV_ButtonPress( HWND hwnd, XEvent *xev )
{
    XButtonEvent *event = &xev->xbutton;
    int buttonNum = event->button - 1;
    INPUT input;

    if (buttonNum >= NB_BUTTONS) return;

    TRACE( "hwnd %p/%lx button %u pos %d,%d\n", hwnd, event->window, buttonNum, event->x, event->y );

    input.u.mi.dx          = event->x;
    input.u.mi.dy          = event->y;
    input.u.mi.mouseData   = button_down_data[buttonNum];
    input.u.mi.dwFlags     = button_down_flags[buttonNum] | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE;
    input.u.mi.time        = EVENT_x11_time_to_win32_time( event->time );
    input.u.mi.dwExtraInfo = 0;

    update_user_time( event->time );
    send_mouse_input( hwnd, event->window, event->state, &input );
}


/***********************************************************************
 *           X11DRV_ButtonRelease
 */
void X11DRV_ButtonRelease( HWND hwnd, XEvent *xev )
{
    XButtonEvent *event = &xev->xbutton;
    int buttonNum = event->button - 1;
    INPUT input;

    if (buttonNum >= NB_BUTTONS || !button_up_flags[buttonNum]) return;

    TRACE( "hwnd %p/%lx button %u pos %d,%d\n", hwnd, event->window, buttonNum, event->x, event->y );

    input.u.mi.dx          = event->x;
    input.u.mi.dy          = event->y;
    input.u.mi.mouseData   = button_up_data[buttonNum];
    input.u.mi.dwFlags     = button_up_flags[buttonNum] | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE;
    input.u.mi.time        = EVENT_x11_time_to_win32_time( event->time );
    input.u.mi.dwExtraInfo = 0;

    send_mouse_input( hwnd, event->window, event->state, &input );
}


/***********************************************************************
 *           X11DRV_MotionNotify
 */
void X11DRV_MotionNotify( HWND hwnd, XEvent *xev )
{
    XMotionEvent *event = &xev->xmotion;
    INPUT input;

    TRACE( "hwnd %p/%lx pos %d,%d is_hint %d\n", hwnd, event->window, event->x, event->y, event->is_hint );

    input.u.mi.dx          = event->x;
    input.u.mi.dy          = event->y;
    input.u.mi.mouseData   = 0;
    input.u.mi.dwFlags     = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
    input.u.mi.time        = EVENT_x11_time_to_win32_time( event->time );
    input.u.mi.dwExtraInfo = 0;

    send_mouse_input( hwnd, event->window, event->state, &input );
}


/***********************************************************************
 *           X11DRV_EnterNotify
 */
void X11DRV_EnterNotify( HWND hwnd, XEvent *xev )
{
    XCrossingEvent *event = &xev->xcrossing;
    INPUT input;

    TRACE( "hwnd %p/%lx pos %d,%d detail %d\n", hwnd, event->window, event->x, event->y, event->detail );

    if (event->detail == NotifyVirtual || event->detail == NotifyNonlinearVirtual) return;
    if (event->window == x11drv_thread_data()->grab_window) return;

    /* simulate a mouse motion event */
    input.u.mi.dx          = event->x;
    input.u.mi.dy          = event->y;
    input.u.mi.mouseData   = 0;
    input.u.mi.dwFlags     = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
    input.u.mi.time        = EVENT_x11_time_to_win32_time( event->time );
    input.u.mi.dwExtraInfo = 0;

    send_mouse_input( hwnd, event->window, event->state, &input );
}

#ifdef HAVE_X11_EXTENSIONS_XINPUT2_H

/***********************************************************************
 *           X11DRV_RawButtonPress
 */
static void X11DRV_RawButtonPress( XIRawEvent *event )
{
    int button = event->detail - 1;
    INPUT input;

    if (button >= NB_BUTTONS) return;

    TRACE( "button %u\n", button );

    input.type             = INPUT_MOUSE;
    input.u.mi.dx          = 0;
    input.u.mi.dy          = 0;
    input.u.mi.mouseData   = button_down_data[button];
    input.u.mi.dwFlags     = button_down_flags[button];
    input.u.mi.time        = EVENT_x11_time_to_win32_time( event->time );
    input.u.mi.dwExtraInfo = 0;

    update_user_time( event->time );
    input.type = INPUT_MOUSE;
    __wine_send_input( 0, &input );
}


/***********************************************************************
 *           X11DRV_RawButtonRelease
 */
static void X11DRV_RawButtonRelease( XIRawEvent *event )
{
    int button = event->detail - 1;
    INPUT input;

    if (button >= NB_BUTTONS) return;

    TRACE( "button %u\n", button );

    input.u.mi.dx          = 0;
    input.u.mi.dy          = 0;
    input.u.mi.mouseData   = button_up_data[button];
    input.u.mi.dwFlags     = button_up_flags[button];
    input.u.mi.time        = EVENT_x11_time_to_win32_time( event->time );
    input.u.mi.dwExtraInfo = 0;

    input.type = INPUT_MOUSE;
    __wine_send_input( 0, &input );
}


/***********************************************************************
 *           X11DRV_RawMotion
 */
static void X11DRV_RawMotion( XIRawEvent *event )
{
    const double *values = event->valuators.values;
    INPUT input;

    if (!event->valuators.mask_len) return;

    input.u.mi.dx          = 0;
    input.u.mi.dy          = 0;
    input.u.mi.mouseData   = 0;
    input.u.mi.dwFlags     = MOUSEEVENTF_MOVE;
    input.u.mi.time        = EVENT_x11_time_to_win32_time( event->time );
    input.u.mi.dwExtraInfo = 0;

    if (XIMaskIsSet( event->valuators.mask, 0 )) input.u.mi.dx = *values++;
    if (XIMaskIsSet( event->valuators.mask, 1 )) input.u.mi.dy = *values++;

    TRACE( "pos %d,%d\n", input.u.mi.dx, input.u.mi.dy );

    input.type = INPUT_MOUSE;
    __wine_send_input( 0, &input );
}

#endif /* HAVE_X11_EXTENSIONS_XINPUT2_H */


/***********************************************************************
 *              X11DRV_XInput2_Init
 */
void X11DRV_XInput2_Init(void)
{
#if defined(SONAME_LIBXI) && defined(HAVE_X11_EXTENSIONS_XINPUT2_H)
    int event, error;
    void *libxi_handle = wine_dlopen( SONAME_LIBXI, RTLD_NOW, NULL, 0 );

    if (!libxi_handle)
    {
        WARN( "couldn't load %s\n", SONAME_LIBXI );
        return;
    }
#define LOAD_FUNCPTR(f) \
    if (!(p##f = wine_dlsym( libxi_handle, #f, NULL, 0))) \
    { \
        WARN("Failed to load %s.\n", #f); \
        return; \
    }

    LOAD_FUNCPTR(XIFreeDeviceInfo);
    LOAD_FUNCPTR(XIQueryDevice);
    LOAD_FUNCPTR(XIQueryVersion);
    LOAD_FUNCPTR(XISelectEvents);
#undef LOAD_FUNCPTR

    wine_tsx11_lock();
    xinput2_available = XQueryExtension( gdi_display, "XInputExtension", &xinput2_opcode, &event, &error );
    wine_tsx11_unlock();
#else
    TRACE( "X Input 2 support not compiled in.\n" );
#endif
}


/***********************************************************************
 *           X11DRV_GenericEvent
 */
void X11DRV_GenericEvent( HWND hwnd, XEvent *xev )
{
#ifdef HAVE_X11_EXTENSIONS_XINPUT2_H
    XGenericEventCookie *event = &xev->xcookie;

    if (!event->data) return;
    if (event->extension != xinput2_opcode) return;

    switch (event->evtype)
    {
    case XI_RawButtonPress:
        X11DRV_RawButtonPress( event->data );
        break;

    case XI_RawButtonRelease:
        X11DRV_RawButtonRelease( event->data );
        break;

    case XI_RawMotion:
        X11DRV_RawMotion( event->data );
        break;

    default:
        TRACE( "Unhandled event %#x\n", event->evtype );
        break;
    }
#endif
}
