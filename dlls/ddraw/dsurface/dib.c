/*		DIBSection DirectDrawSurface driver
 *
 * Copyright 1997-2000 Marcus Meissner
 * Copyright 1998-2000 Lionel Ulmer
 * Copyright 2000 TransGaming Technologies Inc.
 */

#include "config.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "winerror.h"
#include "bitmap.h"
#include "debugtools.h"
#include "ddraw_private.h"
#include "dsurface/main.h"
#include "dsurface/dib.h"

DEFAULT_DEBUG_CHANNEL(ddraw);

static ICOM_VTABLE(IDirectDrawSurface7) DIB_IDirectDrawSurface7_VTable;

static HRESULT create_dib(IDirectDrawSurfaceImpl* This)
{
    BITMAPINFO* b_info;
    UINT usage;
    HDC ddc;
    DIB_DirectDrawSurfaceImpl* priv = This->private;

    assert(This->surface_desc.lpSurface != NULL);

    switch (This->surface_desc.u4.ddpfPixelFormat.u1.dwRGBBitCount)
    {
    case 16:
    case 32:
	/* Allocate extra space to store the RGB bit masks. */
	b_info = (BITMAPINFO*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
					sizeof(BITMAPINFOHEADER)
					+ 3 * sizeof(DWORD));
	break;

    case 24:
	b_info = (BITMAPINFO*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
					sizeof(BITMAPINFOHEADER));
	break;

    default:
	/* Allocate extra space for a palette. */
	b_info = (BITMAPINFO*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
					sizeof(BITMAPINFOHEADER)
					+ sizeof(RGBQUAD)
					* (1 << This->surface_desc.u4.ddpfPixelFormat.u1.dwRGBBitCount));
	break;
    }

    b_info->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    b_info->bmiHeader.biWidth = This->surface_desc.dwWidth;
    b_info->bmiHeader.biHeight = -This->surface_desc.dwHeight;
    b_info->bmiHeader.biPlanes = 1;
    b_info->bmiHeader.biBitCount = This->surface_desc.u4.ddpfPixelFormat.u1.dwRGBBitCount;

    if ((This->surface_desc.u4.ddpfPixelFormat.u1.dwRGBBitCount != 16)
	&& (This->surface_desc.u4.ddpfPixelFormat.u1.dwRGBBitCount != 32))
        b_info->bmiHeader.biCompression = BI_RGB;
    else
        b_info->bmiHeader.biCompression = BI_BITFIELDS;

    b_info->bmiHeader.biSizeImage
	= (This->surface_desc.u4.ddpfPixelFormat.u1.dwRGBBitCount / 8)
	* This->surface_desc.dwWidth * This->surface_desc.dwHeight;

    b_info->bmiHeader.biXPelsPerMeter = 0;
    b_info->bmiHeader.biYPelsPerMeter = 0;
    b_info->bmiHeader.biClrUsed = 0;
    b_info->bmiHeader.biClrImportant = 0;

    switch (This->surface_desc.u4.ddpfPixelFormat.u1.dwRGBBitCount)
    {
    case 16:
    case 32:
    {
	DWORD *masks = (DWORD *) &(b_info->bmiColors);

	usage = 0;
	masks[0] = This->surface_desc.u4.ddpfPixelFormat.u2.dwRBitMask;
	masks[1] = This->surface_desc.u4.ddpfPixelFormat.u3.dwGBitMask;
	masks[2] = This->surface_desc.u4.ddpfPixelFormat.u4.dwBBitMask;
    }
    break;

    case 24:
	/* Nothing to do */
	usage = DIB_RGB_COLORS;
	break;

    default:
	/* Don't know palette */
	usage = 0;
	break;
    }

    ddc = CreateDCA("DISPLAY", NULL, NULL, NULL);
    if (ddc == 0)
    {
	HeapFree(GetProcessHeap(), 0, b_info);
	return HRESULT_FROM_WIN32(GetLastError());
    }

    priv->dib.DIBsection
	= DIB_CreateDIBSection(ddc, b_info, usage, &(priv->dib.bitmap_data), 0,
			       (DWORD)This->surface_desc.lpSurface,
			       This->surface_desc.u1.lPitch);
    DeleteDC(ddc);
    if (!priv->dib.DIBsection) {
	ERR("CreateDIBSection failed!\n");
	HeapFree(GetProcessHeap(), 0, b_info);
	return HRESULT_FROM_WIN32(GetLastError());
    }

    TRACE("DIBSection at : %p\n", priv->dib.bitmap_data);
    if (!This->surface_desc.u1.lPitch) {
	/* This can't happen, right? */
	/* or use GDI_GetObj to get it from the created DIB? */
	This->surface_desc.u1.lPitch = DIB_GetDIBWidthBytes(b_info->bmiHeader.biWidth, b_info->bmiHeader.biBitCount);
	This->surface_desc.dwFlags |= DDSD_PITCH;
    }

    if (!This->surface_desc.lpSurface) {
	This->surface_desc.lpSurface = priv->dib.bitmap_data;
	This->surface_desc.dwFlags |= DDSD_LPSURFACE;
    }

    HeapFree(GetProcessHeap(), 0, b_info);

    /* I don't think it's worth checking for this. */
    if (priv->dib.bitmap_data != This->surface_desc.lpSurface)
	ERR("unexpected error creating DirectDrawSurface DIB section\n");

    return S_OK;
}

void DIB_DirectDrawSurface_final_release(IDirectDrawSurfaceImpl* This)
{
    DIB_DirectDrawSurfaceImpl* priv = This->private;

    DeleteObject(priv->dib.DIBsection);

    if (!priv->dib.client_memory)
	VirtualFree(This->surface_desc.lpSurface, 0, MEM_RELEASE);

    Main_DirectDrawSurface_final_release(This);
}

HRESULT DIB_DirectDrawSurface_duplicate_surface(IDirectDrawSurfaceImpl* This,
						LPDIRECTDRAWSURFACE7* ppDup)
{
    return DIB_DirectDrawSurface_Create(This->ddraw_owner,
					&This->surface_desc, ppDup, NULL);
}

HRESULT DIB_DirectDrawSurface_Construct(IDirectDrawSurfaceImpl *This,
					IDirectDrawImpl *pDD,
					const DDSURFACEDESC2 *pDDSD)
{
    HRESULT hr;
    DIB_DirectDrawSurfaceImpl* priv = This->private;

    TRACE("(%p)->(%p,%p)\n",This,pDD,pDDSD);
    hr = Main_DirectDrawSurface_Construct(This, pDD, pDDSD);
    if (FAILED(hr)) return hr;

    ICOM_INIT_INTERFACE(This, IDirectDrawSurface7,
			DIB_IDirectDrawSurface7_VTable);

    This->final_release = DIB_DirectDrawSurface_final_release;
    This->duplicate_surface = DIB_DirectDrawSurface_duplicate_surface;
    This->flip_data = DIB_DirectDrawSurface_flip_data;

    This->get_dc     = DIB_DirectDrawSurface_get_dc;
    This->release_dc = DIB_DirectDrawSurface_release_dc;
    This->hDC = (HDC)NULL;

    This->set_palette    = DIB_DirectDrawSurface_set_palette;
    This->update_palette = DIB_DirectDrawSurface_update_palette;

    TRACE("(%ldx%ld, pitch=%ld)\n",
	  This->surface_desc.dwWidth, This->surface_desc.dwHeight,
	  This->surface_desc.u1.lPitch);
    /* XXX load dwWidth and dwHeight from pDD if they are not specified? */

    if (This->surface_desc.dwFlags & DDSD_LPSURFACE)
    {
	/* "Client memory": it is managed by the application. */
	/* XXX What if lPitch is not set? Use dwWidth or fail? */

	priv->dib.client_memory = TRUE;
    }
    else
    {
	if (!(This->surface_desc.dwFlags & DDSD_PITCH))
	{
	    int pitch = This->surface_desc.u1.lPitch;
	    if (pitch % 8 != 0)
		pitch += 8 - (pitch % 8);
	}
	/* XXX else: how should lPitch be verified? */

	This->surface_desc.dwFlags |= DDSD_PITCH|DDSD_LPSURFACE;

	This->surface_desc.lpSurface
	    = VirtualAlloc(NULL, This->surface_desc.u1.lPitch
			   * This->surface_desc.dwHeight,
			   MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

	if (This->surface_desc.lpSurface == NULL)
	{
	    Main_DirectDrawSurface_final_release(This);
	    return HRESULT_FROM_WIN32(GetLastError());
	}

	priv->dib.client_memory = FALSE;
    }

    hr = create_dib(This);
    if (FAILED(hr))
    {
	if (!priv->dib.client_memory)
	    VirtualFree(This->surface_desc.lpSurface, 0, MEM_RELEASE);

	Main_DirectDrawSurface_final_release(This);

	return hr;
    }

    return DD_OK;
}

/* Not an API */
HRESULT DIB_DirectDrawSurface_Create(IDirectDrawImpl *pDD,
				     const DDSURFACEDESC2 *pDDSD,
				     LPDIRECTDRAWSURFACE7 *ppSurf,
				     IUnknown *pUnkOuter)
{
    IDirectDrawSurfaceImpl* This;
    HRESULT hr;
    assert(pUnkOuter == NULL);

    This = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
		     sizeof(*This) + sizeof(DIB_DirectDrawSurfaceImpl));
    if (This == NULL) return E_OUTOFMEMORY;

    This->private = (DIB_DirectDrawSurfaceImpl*)(This+1);

    hr = DIB_DirectDrawSurface_Construct(This, pDD, pDDSD);
    if (FAILED(hr))
	HeapFree(GetProcessHeap(), 0, This);
    else
	*ppSurf = ICOM_INTERFACE(This, IDirectDrawSurface7);

    return hr;

}

/* AddAttachedSurface: generic */
/* AddOverlayDirtyRect: generic, unimplemented */

static HRESULT _Blt_ColorFill(
    LPBYTE buf, int width, int height, int bpp, LONG lPitch, DWORD color
) {
    int x, y;
    LPBYTE first;

    /* Do first row */

#define COLORFILL_ROW(type) { \
    type *d = (type *) buf; \
    for (x = 0; x < width; x++) \
	d[x] = (type) color; \
    break; \
}

    switch(bpp) {
    case 1: COLORFILL_ROW(BYTE)
    case 2: COLORFILL_ROW(WORD)
    case 4: COLORFILL_ROW(DWORD)
    default:
	FIXME("Color fill not implemented for bpp %d!\n", bpp*8);
	return DDERR_UNSUPPORTED;
    }

#undef COLORFILL_ROW

    /* Now copy first row */
    first = buf;
    for (y = 1; y < height; y++) {
	buf += lPitch;
	memcpy(buf, first, width * bpp);
    }
    return DD_OK;
}

HRESULT WINAPI
DIB_DirectDrawSurface_Blt(LPDIRECTDRAWSURFACE7 iface, LPRECT rdst,
			  LPDIRECTDRAWSURFACE7 src, LPRECT rsrc,
			  DWORD dwFlags, LPDDBLTFX lpbltfx)
{
    ICOM_THIS(IDirectDrawSurfaceImpl,iface);
    RECT		xdst,xsrc;
    DDSURFACEDESC2	ddesc,sdesc;
    HRESULT		ret = DD_OK;
    int bpp, srcheight, srcwidth, dstheight, dstwidth, width;
    int x, y;
    LPBYTE dbuf, sbuf;

    TRACE("(%p)->(%p,%p,%p,%08lx,%p)\n", This,rdst,src,rsrc,dwFlags,lpbltfx);

    DD_STRUCT_INIT(&ddesc);
    DD_STRUCT_INIT(&sdesc);

    sdesc.dwSize = sizeof(sdesc);
    if (src) IDirectDrawSurface7_Lock(src, NULL, &sdesc, 0, 0);
    ddesc.dwSize = sizeof(ddesc);
    IDirectDrawSurface7_Lock(iface,NULL,&ddesc,0,0);

    if (TRACE_ON(ddraw)) {
	if (rdst) TRACE("\tdestrect :%dx%d-%dx%d\n",rdst->left,rdst->top,rdst->right,rdst->bottom);
	if (rsrc) TRACE("\tsrcrect  :%dx%d-%dx%d\n",rsrc->left,rsrc->top,rsrc->right,rsrc->bottom);
	TRACE("\tflags: ");
	DDRAW_dump_DDBLT(dwFlags);
	if (dwFlags & DDBLT_DDFX) {
	    TRACE("\tblitfx: ");
	    DDRAW_dump_DDBLTFX(lpbltfx->dwDDFX);
	}
    }

    if (rdst) {
	if ((rdst->top < 0) ||
	    (rdst->left < 0) ||
	    (rdst->bottom < 0) ||
	    (rdst->right < 0)) {
	  ERR(" Negative values in LPRECT !!!\n");
	  goto release;
	}
	memcpy(&xdst,rdst,sizeof(xdst));
    } else {
	xdst.top	= 0;
	xdst.bottom	= ddesc.dwHeight;
	xdst.left	= 0;
	xdst.right	= ddesc.dwWidth;
    }

    if (rsrc) {
	if ((rsrc->top < 0) ||
	    (rsrc->left < 0) ||
	    (rsrc->bottom < 0) ||
	    (rsrc->right < 0)) {
	  ERR(" Negative values in LPRECT !!!\n");
	  goto release;
	}
	memcpy(&xsrc,rsrc,sizeof(xsrc));
    } else {
	if (src) {
	    xsrc.top	= 0;
	    xsrc.bottom	= sdesc.dwHeight;
	    xsrc.left	= 0;
	    xsrc.right	= sdesc.dwWidth;
	} else {
	    memset(&xsrc,0,sizeof(xsrc));
	}
    }
    if (src) assert((xsrc.bottom-xsrc.top) <= sdesc.dwHeight);
    assert((xdst.bottom-xdst.top) <= ddesc.dwHeight);

    bpp = GET_BPP(ddesc);
    srcheight = xsrc.bottom - xsrc.top;
    srcwidth = xsrc.right - xsrc.left;
    dstheight = xdst.bottom - xdst.top;
    dstwidth = xdst.right - xdst.left;
    width = (xdst.right - xdst.left) * bpp;

    assert(width <= ddesc.u1.lPitch);

    dbuf = (BYTE*)ddesc.lpSurface+(xdst.top*ddesc.u1.lPitch)+(xdst.left*bpp);

    dwFlags &= ~(DDBLT_WAIT|DDBLT_ASYNC);/* FIXME: can't handle right now */

    /* First, all the 'source-less' blits */
    if (dwFlags & DDBLT_COLORFILL) {
	ret = _Blt_ColorFill(dbuf, dstwidth, dstheight, bpp,
			     ddesc.u1.lPitch, lpbltfx->u5.dwFillColor);
	dwFlags &= ~DDBLT_COLORFILL;
    }

    if (dwFlags & DDBLT_DEPTHFILL)
	FIXME("DDBLT_DEPTHFILL needs to be implemented!\n");
    if (dwFlags & DDBLT_ROP) {
	/* Catch some degenerate cases here */
	switch(lpbltfx->dwROP) {
	case BLACKNESS:
	    ret = _Blt_ColorFill(dbuf,dstwidth,dstheight,bpp,ddesc.u1.lPitch,0);
	    break;
	case 0xAA0029: /* No-op */
	    break;
	case WHITENESS:
	    ret = _Blt_ColorFill(dbuf,dstwidth,dstheight,bpp,ddesc.u1.lPitch,~0);
	    break;
	case SRCCOPY: /* well, we do that below ? */
	    break;
	default: 
	    FIXME("Unsupported raster op: %08lx  Pattern: %p\n", lpbltfx->dwROP, lpbltfx->u5.lpDDSPattern);
	    goto error;
	}
	dwFlags &= ~DDBLT_ROP;
    }
    if (dwFlags & DDBLT_DDROPS) {
	FIXME("\tDdraw Raster Ops: %08lx  Pattern: %p\n", lpbltfx->dwDDROP, lpbltfx->u5.lpDDSPattern);
    }
    /* Now the 'with source' blits */
    if (src) {
	LPBYTE sbase;
	int sx, xinc, sy, yinc;

	if (!dstwidth || !dstheight) /* hmm... stupid program ? */
	    goto release;
	sbase = (BYTE*)sdesc.lpSurface+(xsrc.top*sdesc.u1.lPitch)+xsrc.left*bpp;
	xinc = (srcwidth << 16) / dstwidth;
	yinc = (srcheight << 16) / dstheight;

	if (!dwFlags) {
	    /* No effects, we can cheat here */
	    if (dstwidth == srcwidth) {
		if (dstheight == srcheight) {
		    /* No stretching in either direction. This needs to be as
		     * fast as possible */
		    sbuf = sbase;
		    for (y = 0; y < dstheight; y++) {
			memcpy(dbuf, sbuf, width);
			sbuf += sdesc.u1.lPitch;
			dbuf += ddesc.u1.lPitch;
		    }
		} else {
		    /* Stretching in Y direction only */
		    for (y = sy = 0; y < dstheight; y++, sy += yinc) {
			sbuf = sbase + (sy >> 16) * sdesc.u1.lPitch;
			memcpy(dbuf, sbuf, width);
			dbuf += ddesc.u1.lPitch;
		    }
		}
	    } else {
		/* Stretching in X direction */
		int last_sy = -1;
		for (y = sy = 0; y < dstheight; y++, sy += yinc) {
		    sbuf = sbase + (sy >> 16) * sdesc.u1.lPitch;

		    if ((sy >> 16) == (last_sy >> 16)) {
			/* this sourcerow is the same as last sourcerow -
			 * copy already stretched row
			 */
			memcpy(dbuf, dbuf - ddesc.u1.lPitch, width);
		    } else {
#define STRETCH_ROW(type) { \
		    type *s = (type *) sbuf, *d = (type *) dbuf; \
		    for (x = sx = 0; x < dstwidth; x++, sx += xinc) \
		    d[x] = s[sx >> 16]; \
		    break; }

		    switch(bpp) {
		    case 1: STRETCH_ROW(BYTE)
		    case 2: STRETCH_ROW(WORD)
		    case 4: STRETCH_ROW(DWORD)
		    case 3: {
			LPBYTE s,d = dbuf;
			for (x = sx = 0; x < dstwidth; x++, sx+= xinc) {
			    DWORD pixel;

			    s = sbuf+3*(sx>>16);
			    pixel = (s[0]<<16)|(s[1]<<8)|s[2];
			    d[0] = (pixel>>16)&0xff;
			    d[1] = (pixel>> 8)&0xff;
			    d[2] = (pixel    )&0xff;
			    d+=3;
			}
			break;
		    }
		    default:
			FIXME("Stretched blit not implemented for bpp %d!\n", bpp*8);
			ret = DDERR_UNSUPPORTED;
			goto error;
		    }
#undef STRETCH_ROW
		    }
		    dbuf += ddesc.u1.lPitch;
		    last_sy = sy;
		}
	    }
	} else if (dwFlags & (DDBLT_KEYSRC | DDBLT_KEYDEST)) {
	    DWORD keylow, keyhigh;

	    if (dwFlags & DDBLT_KEYSRC) {
		keylow  = sdesc.ddckCKSrcBlt.dwColorSpaceLowValue;
		keyhigh = sdesc.ddckCKSrcBlt.dwColorSpaceHighValue;
	    } else {
		/* I'm not sure if this is correct */
		FIXME("DDBLT_KEYDEST not fully supported yet.\n");
		keylow  = ddesc.ddckCKDestBlt.dwColorSpaceLowValue;
		keyhigh = ddesc.ddckCKDestBlt.dwColorSpaceHighValue;
	    }


	    for (y = sy = 0; y < dstheight; y++, sy += yinc) {
		sbuf = sbase + (sy >> 16) * sdesc.u1.lPitch;

#define COPYROW_COLORKEY(type) { \
		type *s = (type *) sbuf, *d = (type *) dbuf, tmp; \
		for (x = sx = 0; x < dstwidth; x++, sx += xinc) { \
		    tmp = s[sx >> 16]; \
		    if (tmp < keylow || tmp > keyhigh) d[x] = tmp; \
		} \
		break; }

		switch (bpp) {
		case 1: COPYROW_COLORKEY(BYTE)
		case 2: COPYROW_COLORKEY(WORD)
		case 4: COPYROW_COLORKEY(DWORD)
		default:
		    FIXME("%s color-keyed blit not implemented for bpp %d!\n",
		    (dwFlags & DDBLT_KEYSRC) ? "Source" : "Destination", bpp*8);
		    ret = DDERR_UNSUPPORTED;
		    goto error;
		}
		dbuf += ddesc.u1.lPitch;
	    }
#undef COPYROW_COLORKEY
	    dwFlags &= ~(DDBLT_KEYSRC | DDBLT_KEYDEST);
	}
    }

error:
    if (dwFlags && FIXME_ON(ddraw)) {
	FIXME("\tUnsupported flags: ");
	DDRAW_dump_DDBLT(dwFlags);
    }

release:
    IDirectDrawSurface7_Unlock(iface,NULL);
    if (src) IDirectDrawSurface7_Unlock(src,NULL);
    return DD_OK;
}

/* BltBatch: generic, unimplemented */

HRESULT WINAPI
DIB_DirectDrawSurface_BltFast(LPDIRECTDRAWSURFACE7 iface, DWORD dstx,
			      DWORD dsty, LPDIRECTDRAWSURFACE7 src,
			      LPRECT rsrc, DWORD trans)
{
    ICOM_THIS(IDirectDrawSurfaceImpl,iface);
    int			bpp, w, h, x, y;
    DDSURFACEDESC2	ddesc,sdesc;
    HRESULT		ret = DD_OK;
    LPBYTE		sbuf, dbuf;
    RECT		rsrc2;


    if (TRACE_ON(ddraw)) {
	FIXME("(%p)->(%ld,%ld,%p,%p,%08lx)\n",
		This,dstx,dsty,src,rsrc,trans
	);
	FIXME("	trans:");
	if (FIXME_ON(ddraw))
	  DDRAW_dump_DDBLTFAST(trans);
	if (rsrc)
	  FIXME("\tsrcrect: %dx%d-%dx%d\n",rsrc->left,rsrc->top,rsrc->right,rsrc->bottom);
	else
	  FIXME(" srcrect: NULL\n");
    }
    
    /* We need to lock the surfaces, or we won't get refreshes when done. */
    sdesc.dwSize = sizeof(sdesc);
    IDirectDrawSurface7_Lock(src, NULL,&sdesc,DDLOCK_READONLY, 0);
    ddesc.dwSize = sizeof(ddesc);
    IDirectDrawSurface7_Lock(iface,NULL,&ddesc,DDLOCK_WRITEONLY,0);

   if (!rsrc) {
	   WARN("rsrc is NULL!\n");
	   rsrc = &rsrc2;
	   rsrc->left = rsrc->top = 0;
	   rsrc->right = sdesc.dwWidth;
	   rsrc->bottom = sdesc.dwHeight;
   }

    bpp = GET_BPP(This->surface_desc);
    sbuf = (BYTE *)sdesc.lpSurface+(rsrc->top*sdesc.u1.lPitch)+rsrc->left*bpp;
    dbuf = (BYTE *)ddesc.lpSurface+(dsty*ddesc.u1.lPitch)+dstx* bpp;


    h=rsrc->bottom-rsrc->top;
    if (h>ddesc.dwHeight-dsty) h=ddesc.dwHeight-dsty;
    if (h>sdesc.dwHeight-rsrc->top) h=sdesc.dwHeight-rsrc->top;
    if (h<0) h=0;

    w=rsrc->right-rsrc->left;
    if (w>ddesc.dwWidth-dstx) w=ddesc.dwWidth-dstx;
    if (w>sdesc.dwWidth-rsrc->left) w=sdesc.dwWidth-rsrc->left;
    if (w<0) w=0;

    if (trans & (DDBLTFAST_SRCCOLORKEY | DDBLTFAST_DESTCOLORKEY)) {
	DWORD keylow, keyhigh;
	if (trans & DDBLTFAST_SRCCOLORKEY) {
	    keylow  = sdesc.ddckCKSrcBlt.dwColorSpaceLowValue;
	    keyhigh = sdesc.ddckCKSrcBlt.dwColorSpaceHighValue;
	} else {
	    /* I'm not sure if this is correct */
	    FIXME("DDBLTFAST_DESTCOLORKEY not fully supported yet.\n");
	    keylow  = ddesc.ddckCKDestBlt.dwColorSpaceLowValue;
	    keyhigh = ddesc.ddckCKDestBlt.dwColorSpaceHighValue;
	}

#define COPYBOX_COLORKEY(type) { \
    type *d = (type *)dbuf, *s = (type *)sbuf, tmp; \
    s = (type *) ((BYTE *) sdesc.lpSurface + (rsrc->top * sdesc.u1.lPitch) + rsrc->left * bpp); \
    d = (type *) ((BYTE *) ddesc.lpSurface + (dsty * ddesc.u1.lPitch) + dstx * bpp); \
    for (y = 0; y < h; y++) { \
	for (x = 0; x < w; x++) { \
	    tmp = s[x]; \
	    if (tmp < keylow || tmp > keyhigh) d[x] = tmp; \
	} \
	(LPBYTE)s += sdesc.u1.lPitch; \
	(LPBYTE)d += ddesc.u1.lPitch; \
    } \
    break; \
}

	switch (bpp) {
	case 1: COPYBOX_COLORKEY(BYTE)
	case 2: COPYBOX_COLORKEY(WORD)
	case 4: COPYBOX_COLORKEY(DWORD)
	default:
	    FIXME("Source color key blitting not supported for bpp %d\n",bpp*8);
	    ret = DDERR_UNSUPPORTED;
	    goto error;
	}
#undef COPYBOX_COLORKEY
    } else {
	int width = w * bpp;

	for (y = 0; y < h; y++) {
	    memcpy(dbuf, sbuf, width);
	    sbuf += sdesc.u1.lPitch;
	    dbuf += ddesc.u1.lPitch;
	}
    }
error:
    IDirectDrawSurface7_Unlock(iface, NULL);
    IDirectDrawSurface7_Unlock(src, NULL);
    return ret;
}

/* ChangeUniquenessValue: generic */
/* DeleteAttachedSurface: generic */
/* EnumAttachedSurfaces: generic */
/* EnumOverlayZOrders: generic, unimplemented */

BOOL DIB_DirectDrawSurface_flip_data(IDirectDrawSurfaceImpl* front,
				     IDirectDrawSurfaceImpl* back,
				     DWORD dwFlags)
{
    DIB_DirectDrawSurfaceImpl* front_priv = front->private;
    DIB_DirectDrawSurfaceImpl* back_priv = back->private;

    TRACE("(%p,%p)\n",front,back);

    {
	HBITMAP tmp;
	tmp = front_priv->dib.DIBsection;
	front_priv->dib.DIBsection = back_priv->dib.DIBsection;
	back_priv->dib.DIBsection = tmp;
    }

    {
	void* tmp;
	tmp = front_priv->dib.bitmap_data;
	front_priv->dib.bitmap_data = back_priv->dib.bitmap_data;
	back_priv->dib.bitmap_data = tmp;

	tmp = front->surface_desc.lpSurface;
	front->surface_desc.lpSurface = back->surface_desc.lpSurface;
	back->surface_desc.lpSurface = tmp;
    }

    /* client_memory should not be different, but just in case */
    {
	BOOL tmp;
	tmp = front_priv->dib.client_memory;
	front_priv->dib.client_memory = back_priv->dib.client_memory;
	back_priv->dib.client_memory = tmp;
    }

    return Main_DirectDrawSurface_flip_data(front, back, dwFlags);
}

/* Flip: generic */
/* FreePrivateData: generic */
/* GetAttachedSurface: generic */
/* GetBltStatus: generic */
/* GetCaps: generic (Returns the caps from This->surface_desc.) */
/* GetClipper: generic */
/* GetColorKey: generic */

HRESULT DIB_DirectDrawSurface_alloc_dc(IDirectDrawSurfaceImpl* This, HDC* phDC)
{
    DIB_PRIV_VAR(priv, This);
    HDC hDC;

    TRACE("Grabbing a DC for surface: %p\n", This);

    hDC = CreateCompatibleDC(0);
    priv->dib.holdbitmap = SelectObject(hDC, priv->dib.DIBsection);
    if (This->palette)
	SelectPalette(hDC, This->palette->hpal, FALSE);

    *phDC = hDC;

    return S_OK;
}

HRESULT DIB_DirectDrawSurface_free_dc(IDirectDrawSurfaceImpl* This, HDC hDC)
{
    DIB_PRIV_VAR(priv, This);

    TRACE("Releasing DC for surface: %p\n", This);

    SelectObject(hDC, priv->dib.holdbitmap);
    DeleteDC(hDC);

    return S_OK;
}

HRESULT DIB_DirectDrawSurface_get_dc(IDirectDrawSurfaceImpl* This, HDC* phDC)
{
    return DIB_DirectDrawSurface_alloc_dc(This, phDC);
}

HRESULT DIB_DirectDrawSurface_release_dc(IDirectDrawSurfaceImpl* This, HDC hDC)
{
    return DIB_DirectDrawSurface_free_dc(This, hDC);
}

/* GetDDInterface: generic */
/* GetFlipStatus: generic */
/* GetLOD: generic */
/* GetOverlayPosition: generic */
/* GetPalette: generic */
/* GetPixelFormat: generic */
/* GetPriority: generic */
/* GetPrivateData: generic */
/* GetSurfaceDesc: generic */
/* GetUniquenessValue: generic */
/* Initialize: generic */
/* IsLost: generic */
/* Lock: generic with callback? */
/* PageLock: generic */
/* PageUnlock: generic */

HRESULT WINAPI
DIB_DirectDrawSurface_Restore(LPDIRECTDRAWSURFACE7 iface)
{
    TRACE("(%p)\n",iface);
    return DD_OK;	/* ??? */
}

/* SetClipper: generic */
/* SetColorKey: generic */
/* SetLOD: generic */
/* SetOverlayPosition: generic */

void DIB_DirectDrawSurface_set_palette(IDirectDrawSurfaceImpl* This,
				       IDirectDrawPaletteImpl* pal) 
{
    if (!pal) return;
    if (This->surface_desc.ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE)
	This->update_palette(This, pal,
			     0, pal->palNumEntries,
			     pal->palents);
}

void DIB_DirectDrawSurface_update_palette(IDirectDrawSurfaceImpl* This,
					  IDirectDrawPaletteImpl* pal,
					  DWORD dwStart, DWORD dwCount,
					  LPPALETTEENTRY palent)
{
    RGBQUAD col[256];
    int n;
    HDC dc;

    TRACE("updating primary palette\n");
    for (n=0; n<dwCount; n++) {
      col[n].rgbRed   = palent[n].peRed;
      col[n].rgbGreen = palent[n].peGreen;
      col[n].rgbBlue  = palent[n].peBlue;
      col[n].rgbReserved = 0;
    }
    This->get_dc(This, &dc);
    SetDIBColorTable(dc, dwStart, dwCount, col);
    This->release_dc(This, dc);
    /* FIXME: propagate change to backbuffers */
}


/* SetPalette: generic */
/* SetPriority: generic */
/* SetPrivateData: generic */

HRESULT WINAPI
DIB_DirectDrawSurface_SetSurfaceDesc(LPDIRECTDRAWSURFACE7 iface,
				     LPDDSURFACEDESC2 pDDSD, DWORD dwFlags)
{
    /* XXX */
    FIXME("(%p)->(%p,%08lx)\n",iface,pDDSD,dwFlags);
    abort();
    return E_FAIL;
}

/* Unlock: ???, need callback */
/* UpdateOverlay: generic */
/* UpdateOverlayDisplay: generic */
/* UpdateOverlayZOrder: generic */

static ICOM_VTABLE(IDirectDrawSurface7) DIB_IDirectDrawSurface7_VTable =
{
    Main_DirectDrawSurface_QueryInterface,
    Main_DirectDrawSurface_AddRef,
    Main_DirectDrawSurface_Release,
    Main_DirectDrawSurface_AddAttachedSurface,
    Main_DirectDrawSurface_AddOverlayDirtyRect,
    DIB_DirectDrawSurface_Blt,
    Main_DirectDrawSurface_BltBatch,
    DIB_DirectDrawSurface_BltFast,
    Main_DirectDrawSurface_DeleteAttachedSurface,
    Main_DirectDrawSurface_EnumAttachedSurfaces,
    Main_DirectDrawSurface_EnumOverlayZOrders,
    Main_DirectDrawSurface_Flip,
    Main_DirectDrawSurface_GetAttachedSurface,
    Main_DirectDrawSurface_GetBltStatus,
    Main_DirectDrawSurface_GetCaps,
    Main_DirectDrawSurface_GetClipper,
    Main_DirectDrawSurface_GetColorKey,
    Main_DirectDrawSurface_GetDC,
    Main_DirectDrawSurface_GetFlipStatus,
    Main_DirectDrawSurface_GetOverlayPosition,
    Main_DirectDrawSurface_GetPalette,
    Main_DirectDrawSurface_GetPixelFormat,
    Main_DirectDrawSurface_GetSurfaceDesc,
    Main_DirectDrawSurface_Initialize,
    Main_DirectDrawSurface_IsLost,
    Main_DirectDrawSurface_Lock,
    Main_DirectDrawSurface_ReleaseDC,
    DIB_DirectDrawSurface_Restore,
    Main_DirectDrawSurface_SetClipper,
    Main_DirectDrawSurface_SetColorKey,
    Main_DirectDrawSurface_SetOverlayPosition,
    Main_DirectDrawSurface_SetPalette,
    Main_DirectDrawSurface_Unlock,
    Main_DirectDrawSurface_UpdateOverlay,
    Main_DirectDrawSurface_UpdateOverlayDisplay,
    Main_DirectDrawSurface_UpdateOverlayZOrder,
    Main_DirectDrawSurface_GetDDInterface,
    Main_DirectDrawSurface_PageLock,
    Main_DirectDrawSurface_PageUnlock,
    DIB_DirectDrawSurface_SetSurfaceDesc,
    Main_DirectDrawSurface_SetPrivateData,
    Main_DirectDrawSurface_GetPrivateData,
    Main_DirectDrawSurface_FreePrivateData,
    Main_DirectDrawSurface_GetUniquenessValue,
    Main_DirectDrawSurface_ChangeUniquenessValue,
    Main_DirectDrawSurface_SetPriority,
    Main_DirectDrawSurface_GetPriority,
    Main_DirectDrawSurface_SetLOD,
    Main_DirectDrawSurface_GetLOD
};
