/*		DirectDrawSurface Xlib implementation
 *
 * Copyright 1997-2000 Marcus Meissner
 * Copyright 1998-2000 Lionel Ulmer (most of Direct3D stuff)
 */
#include "config.h"
#include "winerror.h"

#include <unistd.h>
#include <assert.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "options.h"
#include "debugtools.h"
#include "x11_private.h"
#include "bitmap.h"
#include "win.h"
#include "d3d.h"

#ifdef HAVE_OPENGL
/* for d3d texture stuff */
# include "mesa_private.h"
#endif

DEFAULT_DEBUG_CHANNEL(ddraw);

#define VISIBLE(x) (SDDSCAPS(x) & (DDSCAPS_VISIBLE|DDSCAPS_PRIMARYSURFACE))

#define DDPRIVATE(x) x11_dd_private *ddpriv = ((x11_dd_private*)(x)->d->private)
#define DPPRIVATE(x) x11_dp_private *dppriv = ((x11_dp_private*)(x)->private)
#define DSPRIVATE(x) x11_ds_private *dspriv = ((x11_ds_private*)(x)->private)

static BYTE Xlib_TouchData(LPVOID data)
{
    /* this is a function so it doesn't get optimized out */
    return *(BYTE*)data;
}

/******************************************************************************
 *		IDirectDrawSurface methods
 *
 * Since DDS3 and DDS2 are supersets of DDS, we implement DDS3 and let
 * DDS and DDS2 use those functions. (Function calls did not change (except
 * using different DirectDrawSurfaceX version), just added flags and functions)
 */
HRESULT WINAPI Xlib_IDirectDrawSurface4Impl_QueryInterface(
    LPDIRECTDRAWSURFACE4 iface,REFIID refiid,LPVOID *obj
) {
    ICOM_THIS(IDirectDrawSurface4Impl,iface);

    TRACE("(%p)->(%s,%p)\n",This,debugstr_guid(refiid),obj);
    
    /* All DirectDrawSurface versions (1, 2, 3 and 4) use
     * the same interface. And IUnknown does that too of course.
     */
    if ( IsEqualGUID( &IID_IDirectDrawSurface4, refiid )	||
	 IsEqualGUID( &IID_IDirectDrawSurface3, refiid )	||
	 IsEqualGUID( &IID_IDirectDrawSurface2, refiid )	||
	 IsEqualGUID( &IID_IDirectDrawSurface,  refiid )	||
	 IsEqualGUID( &IID_IUnknown,            refiid )
    ) {
	    *obj = This;
	    IDirectDrawSurface4_AddRef(iface);

	    TRACE("  Creating IDirectDrawSurface interface (%p)\n", *obj);
	    return S_OK;
    }
#ifdef HAVE_OPENGL
    if ( IsEqualGUID( &IID_IDirect3DTexture2, refiid ) ) {
	/* Texture interface */
	*obj = d3dtexture2_create(This);
	IDirectDrawSurface4_AddRef(iface);
	TRACE("  Creating IDirect3DTexture2 interface (%p)\n", *obj);
	return S_OK;
    }
    if ( IsEqualGUID( &IID_IDirect3DTexture, refiid ) ) {
	/* Texture interface */
	*obj = d3dtexture_create(This);
	IDirectDrawSurface4_AddRef(iface);
	TRACE("  Creating IDirect3DTexture interface (%p)\n", *obj);
	return S_OK;
    }
#else
    if ( IsEqualGUID( &IID_IDirect3DTexture2, refiid ) ||
         IsEqualGUID( &IID_IDirect3DTexture, refiid )
       )
    {
       ERR( "Cannot provide 3D support without OpenGL/Mesa installed\n" );
    }
#endif /* HAVE_OPENGL */
    FIXME("(%p):interface for IID %s NOT found!\n",This,debugstr_guid(refiid));
    return OLE_E_ENUM_NOMORE;
}

HRESULT WINAPI Xlib_IDirectDrawSurface4Impl_Lock(
    LPDIRECTDRAWSURFACE4 iface,LPRECT lprect,LPDDSURFACEDESC lpddsd,DWORD flags, HANDLE hnd
) {
    ICOM_THIS(IDirectDrawSurface4Impl,iface);
    DSPRIVATE(This);
    DDPRIVATE(This->s.ddraw);

    /* DO NOT AddRef the surface! Lock/Unlock are NOT guaranteed to come in 
     * matched pairs! - Marcus Meissner 20000509 */
    TRACE("(%p)->Lock(%p,%p,%08lx,%08lx) ret=%p\n",This,lprect,lpddsd,flags,(DWORD)hnd,__builtin_return_address(0));
    if (flags & ~(DDLOCK_WAIT|DDLOCK_READONLY|DDLOCK_WRITEONLY))
	WARN("(%p)->Lock(%p,%p,%08lx,%08lx)\n",
		     This,lprect,lpddsd,flags,(DWORD)hnd);

    /* First, copy the Surface description */
    *lpddsd = This->s.surface_desc;
    TRACE("locked surface: height=%ld, width=%ld, pitch=%ld\n",
	  lpddsd->dwHeight,lpddsd->dwWidth,lpddsd->lPitch);

    /* If asked only for a part, change the surface pointer */
    if (lprect) {
	TRACE("	lprect: %dx%d-%dx%d\n",
		lprect->top,lprect->left,lprect->bottom,lprect->right
	);
	if ((lprect->top < 0) ||
	    (lprect->left < 0) ||
	    (lprect->bottom < 0) ||
	    (lprect->right < 0)) {
	  ERR(" Negative values in LPRECT !!!\n");
	  return DDERR_INVALIDPARAMS;
	}
	lpddsd->u1.lpSurface=(LPVOID)((char*)This->s.surface_desc.u1.lpSurface+
		(lprect->top*This->s.surface_desc.lPitch) +
		lprect->left*GET_BPP(This->s.surface_desc));
    } else
	assert(This->s.surface_desc.u1.lpSurface);
    /* wait for any previous operations to complete */
#ifdef HAVE_LIBXXSHM
    if (dspriv->info.image && VISIBLE(This) && ddpriv->xshm_active) {
/*
	int compl = InterlockedExchange( &(ddpriv->xshm_compl), 0 );
	if (compl) X11DRV_EVENT_WaitShmCompletion( compl );
*/
	X11DRV_EVENT_WaitShmCompletions( ddpriv->drawable );
    }
#endif

    /* If part of a visible 'clipped' surface, copy what is seen on the
       screen to the surface */
    if ((dspriv->info.image && VISIBLE(This)) &&
	(This->s.lpClipper)) {
          HWND hWnd = ((IDirectDrawClipperImpl *) This->s.lpClipper)->hWnd;
	  WND *wndPtr = WIN_FindWndPtr(hWnd);
	  Drawable drawable = X11DRV_WND_GetXWindow(wndPtr);
	  int width = wndPtr->rectClient.right - wndPtr->rectClient.left;
	  int height = wndPtr->rectClient.bottom - wndPtr->rectClient.top;
	  /* Now, get the source / destination coordinates */
	  int dest_x = wndPtr->rectClient.left;
	  int dest_y = wndPtr->rectClient.top;

	  if (!drawable) { /* we are running in -desktop mode */
	      drawable = X11DRV_WND_GetXWindow(WIN_GetDesktop());
	      /* FIXME: not sure whether these are the right offsets */
	      dest_x+=wndPtr->rectWindow.left;
	      dest_y+=wndPtr->rectWindow.top;
	      WIN_ReleaseDesktop();
	  }

	  TSXGetSubImage(display, drawable, 0, 0, width, height, 0xFFFFFFFF,
		       ZPixmap, dspriv->info.image, dest_x, dest_y);
	  
	  WIN_ReleaseWndPtr(wndPtr);
    }
    
    return DD_OK;
}

static void Xlib_copy_surface_on_screen(IDirectDrawSurface4Impl* This) {
  DSPRIVATE(This);
  DDPRIVATE(This->s.ddraw);
  Drawable drawable = ddpriv->drawable;
  POINT adjust[2] = {{0, 0}, {0, 0}};
  SIZE imgsiz;

  /* Get XImage size */
  imgsiz.cx = dspriv->info.image->width;
  imgsiz.cy = dspriv->info.image->height;

  if (This->s.lpClipper) {
    HWND hWnd = ((IDirectDrawClipperImpl *) This->s.lpClipper)->hWnd;
    SIZE csiz;
    WND *wndPtr = WIN_FindWndPtr(hWnd);
    drawable = X11DRV_WND_GetXWindow(wndPtr);
    
    MapWindowPoints(hWnd, 0, adjust, 2);

    imgsiz.cx -= adjust[0].x;
    imgsiz.cy -= adjust[0].y;
    /* (note: the rectWindow here should be the X window's interior rect, in
     *  case anyone thinks otherwise while rewriting managed mode) */
    adjust[1].x -= wndPtr->rectWindow.left;
    adjust[1].y -= wndPtr->rectWindow.top;
    csiz.cx = wndPtr->rectClient.right - wndPtr->rectClient.left;
    csiz.cy = wndPtr->rectClient.bottom - wndPtr->rectClient.top;
    if (csiz.cx < imgsiz.cx) imgsiz.cx = csiz.cx;
    if (csiz.cy < imgsiz.cy) imgsiz.cy = csiz.cy;
    
    TRACE("adjust: hwnd=%08x, surface %ldx%ld, drawable %ldx%ld\n", hWnd,
	  adjust[0].x, adjust[0].y,
	  adjust[1].x,adjust[1].y);
    
    WIN_ReleaseWndPtr(wndPtr);
  }

  if (!drawable) {
    WND *tmpWnd = WIN_FindWndPtr(This->s.ddraw->d->window);
    drawable = X11DRV_WND_GetXWindow(tmpWnd);
    WIN_ReleaseWndPtr(tmpWnd);

    /* We don't have a context for this window. Host off the desktop */
    if( !drawable ) {
	FIXME("Have to use Desktop Root Window??? Bummer.\n");
	drawable = X11DRV_WND_GetXWindow(WIN_GetDesktop());
	WIN_ReleaseDesktop();
    }
    ddpriv->drawable = drawable;
  }
  
  if (This->s.ddraw->d->pixel_convert != NULL)
    This->s.ddraw->d->pixel_convert(This->s.surface_desc.u1.lpSurface,
				   dspriv->info.image->data,
				   This->s.surface_desc.dwWidth,
				   This->s.surface_desc.dwHeight,
				   This->s.surface_desc.lPitch,
				   This->s.palette);

  /* if the DIB section is in GdiMod state, we must
   * touch the surface to get any updates from the DIB */
  Xlib_TouchData(dspriv->info.image->data);
#ifdef HAVE_LIBXXSHM
    if (ddpriv->xshm_active) {
/*
	X11DRV_EVENT_WaitReplaceShmCompletion( &(ddpriv->xshm_compl), This->s.ddraw->d.drawable );
*/
	/* let WaitShmCompletions track 'em for now */
	/* (you may want to track it again whenever you implement DX7's partial
	* surface locking, where threads have concurrent access) */
	X11DRV_EVENT_PrepareShmCompletion( ddpriv->drawable );
	TSXShmPutImage(display,
		       drawable,
		       DefaultGCOfScreen(X11DRV_GetXScreen()),
		       dspriv->info.image,
		       adjust[0].x, adjust[0].y, adjust[1].x, adjust[1].y,
		       imgsiz.cx, imgsiz.cy,
		       True
		       );
	/* make sure the image is transferred ASAP */
	TSXFlush(display);
    } else
#endif
	TSXPutImage(display,
		    drawable,
		    DefaultGCOfScreen(X11DRV_GetXScreen()),
		    dspriv->info.image,
		    adjust[0].x, adjust[0].y, adjust[1].x, adjust[1].y,
		    imgsiz.cx, imgsiz.cy
		    );
}

HRESULT WINAPI Xlib_IDirectDrawSurface4Impl_Unlock(
    LPDIRECTDRAWSURFACE4 iface,LPVOID surface
) {
    ICOM_THIS(IDirectDrawSurface4Impl,iface);
    DDPRIVATE(This->s.ddraw);
    DSPRIVATE(This);
    TRACE("(%p)->Unlock(%p)\n",This,surface);

    /*if (!This->s.ddraw->d.paintable)
	return DD_OK; */

    /* Only redraw the screen when unlocking the buffer that is on screen */
    if (dspriv->info.image && VISIBLE(This)) {
	Xlib_copy_surface_on_screen(This);
	if (This->s.palette) {
    	    DPPRIVATE(This->s.palette);
	    if(dppriv->cm)
		TSXSetWindowColormap(display,ddpriv->drawable,dppriv->cm);
	}
    }
    /* DO NOT Release the surface! Lock/Unlock are NOT guaranteed to come in 
     * matched pairs! - Marcus Meissner 20000509 */
    return DD_OK;
}

#ifdef HAVE_XVIDEO
static void Xlib_copy_overlay_on_screen(IDirectDrawSurface4Impl* This) {
  DSPRIVATE(This);
  DDPRIVATE(This->s.ddraw);
  Drawable drawable = ddpriv->drawable;

  if (!drawable) {
    WND *tmpWnd = WIN_FindWndPtr(This->s.ddraw->d->window);
    drawable = X11DRV_WND_GetXWindow(tmpWnd);
    WIN_ReleaseWndPtr(tmpWnd);

    /* We don't have a context for this window. Host off the desktop */
    if( !drawable ) {
	FIXME("Have to use Desktop Root Window??? Bummer.\n");
	drawable = X11DRV_WND_GetXWindow(WIN_GetDesktop());
	WIN_ReleaseDesktop();
    }
    ddpriv->drawable = drawable;
  }

#ifdef HAVE_LIBXXSHM
    if (ddpriv->xshm_active) {
	/* let WaitShmCompletions track 'em for now */
	/* (you may want to track it again whenever you implement DX7's partial
	* surface locking, where threads have concurrent access) */
	X11DRV_EVENT_PrepareShmCompletion( ddpriv->drawable );
	TSXvShmPutImage(display, ddpriv->port_id, drawable, DefaultGCOfScreen(X11DRV_GetXScreen()),
			dspriv->info.overlay.image,
			dspriv->info.overlay.src_rect.left, dspriv->info.overlay.src_rect.top,
			dspriv->info.overlay.src_rect.right - dspriv->info.overlay.src_rect.left,
			dspriv->info.overlay.src_rect.bottom - dspriv->info.overlay.src_rect.top,
			dspriv->info.overlay.dst_rect.left, dspriv->info.overlay.dst_rect.top,
			dspriv->info.overlay.dst_rect.right - dspriv->info.overlay.dst_rect.left,
			dspriv->info.overlay.dst_rect.bottom - dspriv->info.overlay.dst_rect.top,
			True);
	/* make sure the image is transferred ASAP */
	TSXFlush(display);
    } else
#endif
      TSXvPutImage(display, ddpriv->port_id, drawable, DefaultGCOfScreen(X11DRV_GetXScreen()),
		   dspriv->info.overlay.image,
		   dspriv->info.overlay.src_rect.left, dspriv->info.overlay.src_rect.top,
		   dspriv->info.overlay.src_rect.right - dspriv->info.overlay.src_rect.left,
		   dspriv->info.overlay.src_rect.bottom - dspriv->info.overlay.src_rect.top,
		   dspriv->info.overlay.dst_rect.left, dspriv->info.overlay.dst_rect.top,
		   dspriv->info.overlay.dst_rect.right - dspriv->info.overlay.dst_rect.left,
		   dspriv->info.overlay.dst_rect.bottom - dspriv->info.overlay.dst_rect.top);
}
#endif

HRESULT WINAPI Xlib_IDirectDrawSurface4Impl_Flip(
    LPDIRECTDRAWSURFACE4 iface,LPDIRECTDRAWSURFACE4 flipto,DWORD dwFlags
) {
    ICOM_THIS(IDirectDrawSurface4Impl,iface);
    XImage	*image;
    DDPRIVATE(This->s.ddraw);
    DSPRIVATE(This);
    x11_ds_private	*fspriv;
    LPBYTE	surf;
    IDirectDrawSurface4Impl* iflipto=(IDirectDrawSurface4Impl*)flipto;

    TRACE("(%p)->Flip(%p,%08lx)\n",This,iflipto,dwFlags);
    if ((!This->s.ddraw->d->paintable) && (dspriv->is_overlay == FALSE))
	return DD_OK;
    
    iflipto = _common_find_flipto(This,iflipto);
    fspriv = (x11_ds_private*)iflipto->private;

    /* We need to switch the lowlevel surfaces, for xlib this is: */
    /* The surface pointer */
    surf				= This->s.surface_desc.u1.lpSurface;
    This->s.surface_desc.u1.lpSurface	= iflipto->s.surface_desc.u1.lpSurface;
    iflipto->s.surface_desc.u1.lpSurface	= surf;

    /* the associated ximage

       NOTE : for XVideo, the pointer to the XvImage is at the same position
              in memory than the standard XImage. This means that this code
	      still works :-)
    */
    image		= dspriv->info.image;
    dspriv->info.image	= fspriv->info.image;
    fspriv->info.image	= image;

    if (dspriv->opengl_flip) {
#ifdef HAVE_OPENGL
      ENTER_GL();
      glXSwapBuffers(display, ddpriv->drawable);
      LEAVE_GL();
#endif
    } else if (dspriv->is_overlay) {
#ifdef HAVE_XVIDEO
      if (dspriv->info.overlay.shown)
	Xlib_copy_overlay_on_screen(This);
#else
      ERR("Why was this code activated WITHOUT XVideo support ?\n");
#endif
    } else {
#ifdef HAVE_LIBXXSHM
      if (ddpriv->xshm_active) {
	/*
	   int compl = InterlockedExchange( &(ddpriv->xshm_compl), 0 );
	   if (compl) X11DRV_EVENT_WaitShmCompletion( compl );
	   */
	X11DRV_EVENT_WaitShmCompletions( ddpriv->drawable );
      }
#endif
      Xlib_copy_surface_on_screen(This);
      if (iflipto->s.palette) {
        DPPRIVATE(iflipto->s.palette);
	if (dppriv->cm)
	  TSXSetWindowColormap(display,ddpriv->drawable,dppriv->cm);
      }
    }
    return DD_OK;
}

/* The IDirectDrawSurface4::SetPalette method attaches the specified
 * DirectDrawPalette object to a surface. The surface uses this palette for all
 * subsequent operations. The palette change takes place immediately.
 */
HRESULT WINAPI Xlib_IDirectDrawSurface4Impl_SetPalette(
    LPDIRECTDRAWSURFACE4 iface,LPDIRECTDRAWPALETTE pal
) {
    ICOM_THIS(IDirectDrawSurface4Impl,iface);
    DDPRIVATE(This->s.ddraw);
    IDirectDrawPaletteImpl* ipal=(IDirectDrawPaletteImpl*)pal;
    x11_dp_private	*dppriv;
    int i;

    TRACE("(%p)->(%p)\n",This,ipal);

    if (ipal == NULL) {
	if( This->s.palette != NULL )
	    IDirectDrawPalette_Release((IDirectDrawPalette*)This->s.palette);
	This->s.palette = ipal;
	return DD_OK;
    }
    dppriv = (x11_dp_private*)ipal->private;

    if (!dppriv->cm &&
	(This->s.ddraw->d->screen_pixelformat.u.dwRGBBitCount<=8)
    ) {
	dppriv->cm = TSXCreateColormap(
	    display,
	    ddpriv->drawable,
	    DefaultVisualOfScreen(X11DRV_GetXScreen()),
	    AllocAll
	);
	if (!Options.managed)
	    TSXInstallColormap(display,dppriv->cm);

	for (i=0;i<256;i++) {
	    XColor xc;

	    xc.red		= ipal->palents[i].peRed<<8;
	    xc.blue		= ipal->palents[i].peBlue<<8;
	    xc.green	= ipal->palents[i].peGreen<<8;
	    xc.flags	= DoRed|DoBlue|DoGreen;
	    xc.pixel	= i;
	    TSXStoreColor(display,dppriv->cm,&xc);
	}
	TSXInstallColormap(display,dppriv->cm);
    }
    /* According to spec, we are only supposed to 
     * AddRef if this is not the same palette.
     */
    if ( This->s.palette != ipal ) {
	if( ipal != NULL )
	    IDirectDrawPalette_AddRef( (IDirectDrawPalette*)ipal );
	if( This->s.palette != NULL )
	    IDirectDrawPalette_Release( (IDirectDrawPalette*)This->s.palette );
	This->s.palette = ipal; 
	/* Perform the refresh, only if a palette was created */
	if (dppriv->cm)
	  TSXSetWindowColormap(display,ddpriv->drawable,dppriv->cm);

	if (This->s.hdc != 0) {
	    /* hack: set the DIBsection color map */
	    BITMAPOBJ *bmp = (BITMAPOBJ *) GDI_GetObjPtr(This->s.DIBsection, BITMAP_MAGIC);
	    X11DRV_DIBSECTION *dib = (X11DRV_DIBSECTION *)bmp->dib;
	    dib->colorMap = This->s.palette ? This->s.palette->screen_palents : NULL;
	    GDI_ReleaseObj(This->s.DIBsection);
	}
    }
    return DD_OK;
}

ULONG WINAPI Xlib_IDirectDrawSurface4Impl_Release(LPDIRECTDRAWSURFACE4 iface) {
    ICOM_THIS(IDirectDrawSurface4Impl,iface);
    DSPRIVATE(This);
    DDPRIVATE(This->s.ddraw);

    TRACE( "(%p)->() decrementing from %lu.\n", This, This->ref );
    if (--(This->ref))
    	return This->ref;

    IDirectDraw2_Release((IDirectDraw2*)This->s.ddraw);

    /* This frees the program-side surface. In some cases it had been
     * allocated with MEM_SYSTEM, so it does not get 'really' freed
     */
    VirtualFree(This->s.surface_desc.u1.lpSurface, 0, MEM_RELEASE);

    /* Now free the XImages and the respective screen-side surfaces */
    if (dspriv->info.image != NULL) {
	if (dspriv->info.image->data != This->s.surface_desc.u1.lpSurface)
	    VirtualFree(dspriv->info.image->data, 0, MEM_RELEASE);
#ifdef HAVE_LIBXXSHM
	if (ddpriv->xshm_active) {
	    TSXShmDetach(display, &(dspriv->shminfo));
	    if (This->s.surface_desc.ddsCaps.dwCaps & DDSCAPS_OVERLAY) {
	      TSXFree(dspriv->info.image);
	    } else {
	      TSXDestroyImage(dspriv->info.image);
	    }
	    shmdt(dspriv->shminfo.shmaddr);
	} else 
#endif
	{
	  if (This->s.surface_desc.ddsCaps.dwCaps & DDSCAPS_OVERLAY) {
	    TSXFree(dspriv->info.image);
	  } else {
	    /* normal X Image memory was never allocated by X, but always by 
	     * ourselves -> Don't let X free our imagedata.
	     */
	    dspriv->info.image->data = NULL;
	    TSXDestroyImage(dspriv->info.image);
	  }
	}
	dspriv->info.image = 0;
    }

    if (This->s.palette)
	IDirectDrawPalette_Release((IDirectDrawPalette*)This->s.palette);

    /* Free the DIBSection (if any) */
    if (This->s.hdc != 0) {
	/* hack: restore the original DIBsection color map */
	BITMAPOBJ *bmp = (BITMAPOBJ *) GDI_GetObjPtr(This->s.DIBsection, BITMAP_MAGIC);
	X11DRV_DIBSECTION *dib = (X11DRV_DIBSECTION *)bmp->dib;
	dib->colorMap = dspriv->oldDIBmap;
	GDI_ReleaseObj(This->s.DIBsection);

	SelectObject(This->s.hdc, This->s.holdbitmap);
	DeleteDC(This->s.hdc);
	DeleteObject(This->s.DIBsection);
    }

    /* Free the clipper if present */
    if(This->s.lpClipper)
	IDirectDrawClipper_Release(This->s.lpClipper);
    HeapFree(GetProcessHeap(),0,This->private);
    HeapFree(GetProcessHeap(),0,This);
    return S_OK;
}

HRESULT WINAPI Xlib_IDirectDrawSurface4Impl_GetDC(LPDIRECTDRAWSURFACE4 iface,HDC* lphdc) {
    ICOM_THIS(IDirectDrawSurface4Impl,iface);
    DSPRIVATE(This);
    int was_ok = This->s.hdc != 0;
    HRESULT result = IDirectDrawSurface4Impl_GetDC(iface,lphdc);
    if (This->s.hdc && !was_ok) {
	/* hack: take over the DIBsection color map */
	BITMAPOBJ *bmp = (BITMAPOBJ *) GDI_GetObjPtr(This->s.DIBsection, BITMAP_MAGIC);
	X11DRV_DIBSECTION *dib = (X11DRV_DIBSECTION *)bmp->dib;
	dspriv->oldDIBmap = dib->colorMap;
	dib->colorMap = This->s.palette ? This->s.palette->screen_palents : NULL;
	GDI_ReleaseObj(This->s.DIBsection);
    }
    return result;
}

#ifdef HAVE_XVIDEO
typedef struct {
  BOOL shown;
  LPRECT src_rect;
  LPRECT dst_rect;
  LPDIRECTDRAWSURFACE dest_surface;
} UpdateOverlayEnumerate;

static HRESULT WINAPI enum_func(LPDIRECTDRAWSURFACE lpDDSurface,
				LPDDSURFACEDESC lpDDSurfaceDesc,  
				LPVOID lpContext) {
  ICOM_THIS(IDirectDrawSurface4Impl,lpDDSurface);
  DSPRIVATE(This);
  UpdateOverlayEnumerate *ctx = (UpdateOverlayEnumerate *) lpContext;

  if ((lpDDSurfaceDesc->ddsCaps.dwCaps) & DDSCAPS_BACKBUFFER) {
    TRACE("Upgrading surface %p\n", lpDDSurface);

    if (ctx->shown) {
      dspriv->info.overlay.shown = TRUE;
      dspriv->info.overlay.src_rect = *(ctx->src_rect);
      dspriv->info.overlay.dst_rect = *(ctx->dst_rect);
      dspriv->info.overlay.dest_surface = ctx->dest_surface;
    } else {
      dspriv->info.overlay.shown = FALSE;
    }
  }
  
  return DDENUMRET_OK;
}
#endif

HRESULT WINAPI Xlib_IDirectDrawSurface4Impl_UpdateOverlay(
    LPDIRECTDRAWSURFACE4 iface, LPRECT lpSrcRect,
    LPDIRECTDRAWSURFACE4 lpDDDestSurface, LPRECT lpDestRect, DWORD dwFlags,
    LPDDOVERLAYFX lpDDOverlayFx
) {
  ICOM_THIS(IDirectDrawSurface4Impl,iface);
#ifdef HAVE_XVIDEO
  DSPRIVATE(This);
  DDPRIVATE(This->s.ddraw);

  if (ddpriv->xvideo_active) {
    TRACE("(%p)->(%p,%p,%p,0x%08lx,%p)\n", This,
	  lpSrcRect, lpDDDestSurface, lpDestRect, dwFlags, lpDDOverlayFx );

    if (TRACE_ON(ddraw)) {
      DPRINTF(" - dwFlags : ");
      _dump_DDOVERLAY(dwFlags);
      
      if (lpSrcRect)  DPRINTF(" - src  rectangle :%dx%d-%dx%d\n",lpSrcRect->left,lpSrcRect->top,
			      lpSrcRect->right,lpSrcRect->bottom);
      if (lpDestRect) DPRINTF(" - dest rectangle :%dx%d-%dx%d\n",lpDestRect->left,lpDestRect->top,
			      lpDestRect->right,lpDestRect->bottom);
    }
    
    if (dwFlags & DDOVER_SHOW) {
      UpdateOverlayEnumerate ctx;
      
      dwFlags &= ~DDOVER_SHOW;

      if ((lpSrcRect == NULL) || (lpDestRect == NULL)) {
	FIXME("This is NOT supported yet...\n");
	return DD_OK;
      }
      
      /* Set the shown BOOL to TRUE and update the rectangles */
      dspriv->info.overlay.shown = TRUE;
      dspriv->info.overlay.src_rect = *lpSrcRect;
      dspriv->info.overlay.dst_rect = *lpDestRect;
      dspriv->info.overlay.dest_surface = (LPDIRECTDRAWSURFACE) lpDDDestSurface;

      /* Now the same for the backbuffers */
      ctx.shown = TRUE;
      ctx.src_rect = lpSrcRect;
      ctx.dst_rect = lpDestRect;
      ctx.dest_surface = (LPDIRECTDRAWSURFACE) lpDDDestSurface;

      IDirectDrawSurface4Impl_EnumAttachedSurfaces(iface, &ctx, enum_func);
    } else if (dwFlags & DDOVER_HIDE) {
      UpdateOverlayEnumerate ctx;
      
      dwFlags &= ~DDOVER_HIDE;

      /* Set the shown BOOL to FALSE for all overlays */
      dspriv->info.overlay.shown = FALSE;
      ctx.shown = FALSE;
      IDirectDrawSurface4Impl_EnumAttachedSurfaces(iface, &ctx, enum_func);
    }

    if (dwFlags && TRACE_ON(ddraw)) {
      WARN("Unsupported flags : ");
      _dump_DDOVERLAY(dwFlags);   
    }
  } else
#endif
    FIXME("(%p)->(%p,%p,%p,0x%08lx,%p) not supported without XVideo !\n", This,
	  lpSrcRect, lpDDDestSurface, lpDestRect, dwFlags, lpDDOverlayFx );  

  return DD_OK;
}

ICOM_VTABLE(IDirectDrawSurface4) xlib_dds4vt = 
{
    ICOM_MSVTABLE_COMPAT_DummyRTTIVALUE
    Xlib_IDirectDrawSurface4Impl_QueryInterface,
    IDirectDrawSurface4Impl_AddRef,
    Xlib_IDirectDrawSurface4Impl_Release,
    IDirectDrawSurface4Impl_AddAttachedSurface,
    IDirectDrawSurface4Impl_AddOverlayDirtyRect,
    IDirectDrawSurface4Impl_Blt,
    IDirectDrawSurface4Impl_BltBatch,
    IDirectDrawSurface4Impl_BltFast,
    IDirectDrawSurface4Impl_DeleteAttachedSurface,
    IDirectDrawSurface4Impl_EnumAttachedSurfaces,
    IDirectDrawSurface4Impl_EnumOverlayZOrders,
    Xlib_IDirectDrawSurface4Impl_Flip,
    IDirectDrawSurface4Impl_GetAttachedSurface,
    IDirectDrawSurface4Impl_GetBltStatus,
    IDirectDrawSurface4Impl_GetCaps,
    IDirectDrawSurface4Impl_GetClipper,
    IDirectDrawSurface4Impl_GetColorKey,
    Xlib_IDirectDrawSurface4Impl_GetDC,
    IDirectDrawSurface4Impl_GetFlipStatus,
    IDirectDrawSurface4Impl_GetOverlayPosition,
    IDirectDrawSurface4Impl_GetPalette,
    IDirectDrawSurface4Impl_GetPixelFormat,
    IDirectDrawSurface4Impl_GetSurfaceDesc,
    IDirectDrawSurface4Impl_Initialize,
    IDirectDrawSurface4Impl_IsLost,
    Xlib_IDirectDrawSurface4Impl_Lock,
    IDirectDrawSurface4Impl_ReleaseDC,
    IDirectDrawSurface4Impl_Restore,
    IDirectDrawSurface4Impl_SetClipper,
    IDirectDrawSurface4Impl_SetColorKey,
    IDirectDrawSurface4Impl_SetOverlayPosition,
    Xlib_IDirectDrawSurface4Impl_SetPalette,
    Xlib_IDirectDrawSurface4Impl_Unlock,
    Xlib_IDirectDrawSurface4Impl_UpdateOverlay,
    IDirectDrawSurface4Impl_UpdateOverlayDisplay,
    IDirectDrawSurface4Impl_UpdateOverlayZOrder,
    IDirectDrawSurface4Impl_GetDDInterface,
    IDirectDrawSurface4Impl_PageLock,
    IDirectDrawSurface4Impl_PageUnlock,
    IDirectDrawSurface4Impl_SetSurfaceDesc,
    IDirectDrawSurface4Impl_SetPrivateData,
    IDirectDrawSurface4Impl_GetPrivateData,
    IDirectDrawSurface4Impl_FreePrivateData,
    IDirectDrawSurface4Impl_GetUniquenessValue,
    IDirectDrawSurface4Impl_ChangeUniquenessValue
};
