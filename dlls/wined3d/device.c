/*
 * IWineD3DDevice implementation
 *
 * Copyright 2002-2004 Jason Edmeades
 * Copyright 2003-2004 Raphael Junqueira
 * Copyright 2004 Christian Costa
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
#include "wined3d_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d);
WINE_DECLARE_DEBUG_CHANNEL(d3d_caps);
WINE_DECLARE_DEBUG_CHANNEL(d3d_fps);
#define GLINFO_LOCATION ((IWineD3DImpl *)(This->wineD3D))->gl_info

/**********************************************************
 * Global variable / Constants follow
 **********************************************************/
const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};  /* When needed for comparisons */

/**********************************************************
 * Utility functions follow
 **********************************************************/
/* Convert the D3DLIGHT properties into equivalent gl lights */
void setup_light(IWineD3DDevice *iface, LONG Index, PLIGHTINFOEL *lightInfo) {

    float quad_att;
    float colRGBA[] = {0.0, 0.0, 0.0, 0.0};
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    /* Light settings are affected by the model view in OpenGL, the View transform in direct3d*/
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadMatrixf((float *) &This->stateBlock->transforms[D3DTS_VIEW].u.m[0][0]);

    /* Diffuse: */
    colRGBA[0] = lightInfo->OriginalParms.Diffuse.r;
    colRGBA[1] = lightInfo->OriginalParms.Diffuse.g;
    colRGBA[2] = lightInfo->OriginalParms.Diffuse.b;
    colRGBA[3] = lightInfo->OriginalParms.Diffuse.a;
    glLightfv(GL_LIGHT0+Index, GL_DIFFUSE, colRGBA);
    checkGLcall("glLightfv");

    /* Specular */
    colRGBA[0] = lightInfo->OriginalParms.Specular.r;
    colRGBA[1] = lightInfo->OriginalParms.Specular.g;
    colRGBA[2] = lightInfo->OriginalParms.Specular.b;
    colRGBA[3] = lightInfo->OriginalParms.Specular.a;
    glLightfv(GL_LIGHT0+Index, GL_SPECULAR, colRGBA);
    checkGLcall("glLightfv");

    /* Ambient */
    colRGBA[0] = lightInfo->OriginalParms.Ambient.r;
    colRGBA[1] = lightInfo->OriginalParms.Ambient.g;
    colRGBA[2] = lightInfo->OriginalParms.Ambient.b;
    colRGBA[3] = lightInfo->OriginalParms.Ambient.a;
    glLightfv(GL_LIGHT0+Index, GL_AMBIENT, colRGBA);
    checkGLcall("glLightfv");

    /* Attenuation - Are these right? guessing... */
    glLightf(GL_LIGHT0+Index, GL_CONSTANT_ATTENUATION,  lightInfo->OriginalParms.Attenuation0);
    checkGLcall("glLightf");
    glLightf(GL_LIGHT0+Index, GL_LINEAR_ATTENUATION,    lightInfo->OriginalParms.Attenuation1);
    checkGLcall("glLightf");

    quad_att = 1.4/(lightInfo->OriginalParms.Range*lightInfo->OriginalParms.Range);
    if (quad_att < lightInfo->OriginalParms.Attenuation2) quad_att = lightInfo->OriginalParms.Attenuation2;
    glLightf(GL_LIGHT0+Index, GL_QUADRATIC_ATTENUATION, quad_att);
    checkGLcall("glLightf");

    switch (lightInfo->OriginalParms.Type) {
    case D3DLIGHT_POINT:
        /* Position */
        glLightfv(GL_LIGHT0+Index, GL_POSITION, &lightInfo->lightPosn[0]);
        checkGLcall("glLightfv");
        glLightf(GL_LIGHT0 + Index, GL_SPOT_CUTOFF, lightInfo->cutoff);
        checkGLcall("glLightf");
        /* FIXME: Range */
        break;

    case D3DLIGHT_SPOT:
        /* Position */
        glLightfv(GL_LIGHT0+Index, GL_POSITION, &lightInfo->lightPosn[0]);
        checkGLcall("glLightfv");
        /* Direction */
        glLightfv(GL_LIGHT0+Index, GL_SPOT_DIRECTION, &lightInfo->lightDirn[0]);
        checkGLcall("glLightfv");
        glLightf(GL_LIGHT0 + Index, GL_SPOT_EXPONENT, lightInfo->exponent);
        checkGLcall("glLightf");
        glLightf(GL_LIGHT0 + Index, GL_SPOT_CUTOFF, lightInfo->cutoff);
        checkGLcall("glLightf");
        /* FIXME: Range */
        break;

    case D3DLIGHT_DIRECTIONAL:
        /* Direction */
        glLightfv(GL_LIGHT0+Index, GL_POSITION, &lightInfo->lightPosn[0]); /* Note gl uses w position of 0 for direction! */
        checkGLcall("glLightfv");
        glLightf(GL_LIGHT0+Index, GL_SPOT_CUTOFF, lightInfo->cutoff);
        checkGLcall("glLightf");
        glLightf(GL_LIGHT0+Index, GL_SPOT_EXPONENT, 0.0f);
        checkGLcall("glLightf");
        break;

    default:
        FIXME("Unrecognized light type %d\n", lightInfo->OriginalParms.Type);
    }

    /* Restore the modelview matrix */
    glPopMatrix();
}

/* Apply the current values to the specified texture stage */
void WINAPI IWineD3DDeviceImpl_SetupTextureStates(IWineD3DDevice *iface, DWORD Stage, DWORD Flags) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    int i = 0;
    float col[4];
    BOOL changeTexture = TRUE;

    TRACE("-----------------------> Updating the texture at stage %ld to have new texture state information\n", Stage);
    for (i = 1; i < HIGHEST_TEXTURE_STATE; i++) {

        BOOL skip = FALSE;

        switch (i) {
        /* Performance: For texture states where multiples effect the outcome, only bother
              applying the last one as it will pick up all the other values                */
        case D3DTSS_COLORARG0:  /* Will be picked up when setting color op */
        case D3DTSS_COLORARG1:  /* Will be picked up when setting color op */
        case D3DTSS_COLORARG2:  /* Will be picked up when setting color op */
        case D3DTSS_ALPHAARG0:  /* Will be picked up when setting alpha op */
        case D3DTSS_ALPHAARG1:  /* Will be picked up when setting alpha op */
        case D3DTSS_ALPHAARG2:  /* Will be picked up when setting alpha op */
           skip = TRUE;
           break;

        /* Performance: If the texture states only impact settings for the texture unit 
             (compared to the texture object) then there is no need to reapply them. The
             only time they need applying is the first time, since we cheat and put the  
             values into the stateblock without applying.                                
             Per-texture unit: texture function (eg. combine), ops and args
                               texture env color                                               
                               texture generation settings                               
           Note: Due to some special conditions there may be a need to do particular ones
             of these, which is what the Flags allows                                     */
        case D3DTSS_COLOROP:       
        case D3DTSS_TEXCOORDINDEX:
            if (!(Flags == REAPPLY_ALL)) skip=TRUE;
            break;

        case D3DTSS_ALPHAOP:       
            if (!(Flags & REAPPLY_ALPHAOP)) skip=TRUE;
            break;

        default:
            skip = FALSE;
        }

        if (skip == FALSE) {
           /* Performance: Only change to this texture if we have to */
           if (changeTexture) {
               /* Make appropriate texture active */
               if (GL_SUPPORT(ARB_MULTITEXTURE)) {
                   GLACTIVETEXTURE(Stage);
                } else if (Stage > 0) {
                    FIXME("Program using multiple concurrent textures which this opengl implementation doesn't support\n");
                }
                changeTexture = FALSE;
           }

           /* Now apply the change */
           IWineD3DDevice_SetTextureStageState(iface, Stage, i, This->stateBlock->textureState[Stage][i]);
        }
    }

    /* Note the D3DRS value applies to all textures, but GL has one
     *  per texture, so apply it now ready to be used!
     */
    D3DCOLORTOGLFLOAT4(This->stateBlock->renderState[D3DRS_TEXTUREFACTOR], col);
    glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, &col[0]);
    checkGLcall("glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, color);");

    TRACE("-----------------------> Updated the texture at stage %ld to have new texture state information\n", Stage);
}

/**********************************************************
 * IWineD3DDevice implementation follows
 **********************************************************/
HRESULT WINAPI IWineD3DDeviceImpl_GetParent(IWineD3DDevice *iface, IUnknown **pParent) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    *pParent = This->parent;
    IUnknown_AddRef(This->parent);
    return D3D_OK;
}

/*****
 * Creation of other classes
 *****/
HRESULT WINAPI IWineD3DDeviceImpl_CreateVertexBuffer(IWineD3DDevice *iface, UINT Size, DWORD Usage, 
                             DWORD FVF, D3DPOOL Pool, IWineD3DVertexBuffer** ppVertexBuffer, HANDLE *sharedHandle,
                             IUnknown *parent) {

    IWineD3DVertexBufferImpl *object;
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    /* Allocate the storage for the device */
    object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(IWineD3DVertexBufferImpl));
    if (NULL == object) {
        *ppVertexBuffer = NULL;
        return D3DERR_OUTOFVIDEOMEMORY;
    }
    object->lpVtbl                = &IWineD3DVertexBuffer_Vtbl;
    object->resource.wineD3DDevice= This;
    IWineD3DDevice_AddRef(iface);
    object->resource.parent       = parent;
    object->resource.resourceType = D3DRTYPE_VERTEXBUFFER;
    object->resource.ref          = 1;
    object->allocatedMemory       = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Size);
    object->currentDesc.Usage     = Usage;
    object->currentDesc.Pool      = Pool;
    object->currentDesc.FVF       = FVF;
    object->currentDesc.Size      = Size;

    TRACE("(%p) : Size=%d, Usage=%ld, FVF=%lx, Pool=%d - Memory@%p, Iface@%p\n", This, Size, Usage, FVF, Pool, object->allocatedMemory, object);
    *ppVertexBuffer = (IWineD3DVertexBuffer *)object;

    return D3D_OK;
}

HRESULT WINAPI IWineD3DDeviceImpl_CreateIndexBuffer(IWineD3DDevice *iface, UINT Length, DWORD Usage, 
                                                    D3DFORMAT Format, D3DPOOL Pool, IWineD3DIndexBuffer** ppIndexBuffer,
                                                    HANDLE *sharedHandle, IUnknown *parent) {
    IWineD3DIndexBufferImpl *object;
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    /* Allocate the storage for the device */
    object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(IWineD3DIndexBufferImpl));
    if (NULL == object) {
        *ppIndexBuffer = NULL;
        return D3DERR_OUTOFVIDEOMEMORY;
    }
    object->lpVtbl = &IWineD3DIndexBuffer_Vtbl;
    object->resource.wineD3DDevice = This;
    IWineD3DDevice_AddRef(iface);
    object->resource.resourceType  = D3DRTYPE_INDEXBUFFER;
    object->resource.parent        = parent;
    object->resource.ref = 1;
    object->allocatedMemory = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Length);
    object->currentDesc.Usage = Usage;
    object->currentDesc.Pool  = Pool;
    object->currentDesc.Format= Format;
    object->currentDesc.Size  = Length;

    TRACE("(%p) : Len=%d, Use=%lx, Format=(%u,%s), Pool=%d - Memory@%p, Iface@%p\n", This, Length, Usage, Format, 
                           debug_d3dformat(Format), Pool, object, object->allocatedMemory);
    *ppIndexBuffer = (IWineD3DIndexBuffer *) object;

    return D3D_OK;
}

HRESULT WINAPI IWineD3DDeviceImpl_CreateStateBlock(IWineD3DDevice* iface, D3DSTATEBLOCKTYPE Type, IWineD3DStateBlock** ppStateBlock, IUnknown *parent) {
  
    IWineD3DDeviceImpl     *This = (IWineD3DDeviceImpl *)iface;
    IWineD3DStateBlockImpl *object;
  
    /* Allocate Storage for the state block */
    object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(IWineD3DStateBlockImpl));
    if (NULL == object) {
        *ppStateBlock = NULL;
        return D3DERR_OUTOFVIDEOMEMORY;
    }
    object->lpVtbl        = &IWineD3DStateBlock_Vtbl;
    object->wineD3DDevice = This;
    IWineD3DDevice_AddRef(iface);
    object->parent        = parent;
    object->ref           = 1;
    object->blockType     = Type;
    *ppStateBlock         = (IWineD3DStateBlock *)object;

    /* Special case - Used during initialization to produce a placeholder stateblock
          so other functions called can update a state block                         */
    if (Type == (D3DSTATEBLOCKTYPE) 0) {
        /* Don't bother increasing the reference count otherwise a device will never
           be freed due to circular dependencies                                   */
        return D3D_OK;
    }

    /* Otherwise, might as well set the whole state block to the appropriate values */
    IWineD3DDevice_AddRef(iface);
    memcpy(object, This->stateBlock, sizeof(IWineD3DStateBlockImpl));
    FIXME("unfinished - needs to set up changed and set attributes\n");
    return D3D_OK;
}

HRESULT WINAPI IWineD3DDeviceImpl_CreateRenderTarget(IWineD3DDevice *iface, UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, 
                                                     DWORD MultisampleQuality, BOOL Lockable, IWineD3DSurface** ppSurface, HANDLE* pSharedHandle, 
                                                     IUnknown *parent) {
    IWineD3DSurfaceImpl *object;
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    
    object  = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(IWineD3DSurfaceImpl));
    if (NULL == object) {
        *ppSurface = NULL;
        return D3DERR_OUTOFVIDEOMEMORY;
    }
    object->lpVtbl                 = &IWineD3DSurface_Vtbl;
    object->resource.wineD3DDevice = This;
    IWineD3DDevice_AddRef(iface);
    object->resource.resourceType  = D3DRTYPE_SURFACE;
    object->resource.parent        = parent;
    object->resource.ref           = 1;
    *ppSurface = (IWineD3DSurface *)object;
    object->container     = (IUnknown*) This;

    object->currentDesc.Width  = Width;
    object->currentDesc.Height = Height;
    object->currentDesc.Format = Format;
    object->currentDesc.Type   = D3DRTYPE_SURFACE;
    object->currentDesc.Usage  = D3DUSAGE_RENDERTARGET;
    object->currentDesc.Pool   = D3DPOOL_DEFAULT;
    object->currentDesc.MultiSampleType = MultiSample;
    object->bytesPerPixel = D3DFmtGetBpp(This, Format);
    if (Format == D3DFMT_DXT1) { 
        object->currentDesc_size = (Width * object->bytesPerPixel)/2 * Height;  /* DXT1 is half byte per pixel */
    } else {
        object->currentDesc_size = (Width * object->bytesPerPixel) * Height;
    }
    object->allocatedMemory = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, object->currentDesc_size);
    object->lockable = Lockable;
    object->locked = FALSE;
    memset(&object->lockedRect, 0, sizeof(RECT));
    IWineD3DSurface_CleanDirtyRect(*ppSurface);

    TRACE("(%p) : w(%d) h(%d) fmt(%d,%s) lockable(%d) surf@%p, surfmem@%p, %d bytes\n", This, Width, Height, Format, debug_d3dformat(Format), Lockable, *ppSurface, object->allocatedMemory, object->currentDesc_size);
    return D3D_OK;
}

HRESULT WINAPI IWineD3DDeviceImpl_CreateOffscreenPlainSurface(IWineD3DDevice *iface, 
                                               UINT Width, UINT Height,
                                               D3DFORMAT Format, D3DPOOL Pool, 
                                               IWineD3DSurface** ppSurface,
                                               HANDLE* pSharedHandle, IUnknown *parent) {

    IWineD3DDeviceImpl        *This = (IWineD3DDeviceImpl *)iface;
    IWineD3DSurfaceImpl       *object;

    object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(IWineD3DSurfaceImpl));
    if (NULL == object) {
        *ppSurface = NULL;
        return D3DERR_OUTOFVIDEOMEMORY;
    }

    object->lpVtbl = &IWineD3DSurface_Vtbl;
    object->resource.wineD3DDevice = This;
    IWineD3DDevice_AddRef(iface);
    object->resource.resourceType  = D3DRTYPE_VOLUME;
    object->resource.parent        = parent;
    object->resource.ref           = 1;
    *ppSurface = (IWineD3DSurface *)object;
    object->container = (IUnknown*) This;

    TRACE("(%p) : W(%d) H(%d), Fmt(%u,%s), Pool(%s)\n", This, Width, Height, 
          Format, debug_d3dformat(Format), debug_d3dpool(Pool));

    object->currentDesc.Width  = Width;
    object->currentDesc.Height = Height;
    object->currentDesc.Format = Format;
    object->currentDesc.Type   = D3DRTYPE_SURFACE;
    object->currentDesc.Usage  = 0;
    object->currentDesc.Pool   = Pool;
    object->bytesPerPixel      = D3DFmtGetBpp(This, Format);

    /* DXTn mipmaps use the same number of 'levels' down to eg. 8x1, but since
       it is based around 4x4 pixel blocks it requires padding, so allocate enough
       space!                                                                      */
    if (Format == D3DFMT_DXT1) { 
        object->currentDesc_size = ((max(Width,4) * object->bytesPerPixel) * max(Height,4)) / 2; /* DXT1 is half byte per pixel */
    } else if (Format == D3DFMT_DXT2 || Format == D3DFMT_DXT3 || 
               Format == D3DFMT_DXT4 || Format == D3DFMT_DXT5) { 
        object->currentDesc_size = ((max(Width,4) * object->bytesPerPixel) * max(Height,4));
    } else {
        object->currentDesc_size = (Width * object->bytesPerPixel) * Height;
    }
    object->allocatedMemory = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, object->currentDesc_size);
    object->lockable = TRUE;
    object->locked   = FALSE;
    object->Dirty    = FALSE;
    TRACE("(%p) : w(%d) h(%d) fmt(%d,%s) surf@%p, surfmem@%p, %d bytes\n", This, Width, Height, Format, debug_d3dformat(Format), *ppSurface, object->allocatedMemory, object->currentDesc_size);
    
    memset(&object->lockedRect, 0, sizeof(RECT));
    return IWineD3DSurface_CleanDirtyRect(*ppSurface);
}

HRESULT WINAPI IWineD3DDeviceImpl_CreateTexture(IWineD3DDevice *iface, UINT Width, 
                                                UINT Height, UINT Levels, DWORD Usage,
                                                D3DFORMAT Format, D3DPOOL Pool, 
                                                IWineD3DTexture** ppTexture, 
                                                HANDLE* pSharedHandle, IUnknown *parent,
                                                D3DCB_CREATESURFACEFN D3DCB_CreateSurface) {

    IWineD3DDeviceImpl     *This = (IWineD3DDeviceImpl *)iface;
    IWineD3DTextureImpl    *object;
    unsigned int            i;
    UINT                    tmpW;
    UINT                    tmpH;

    object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(IWineD3DTextureImpl));
    if (NULL == object) {
        *ppTexture = NULL;
        return D3DERR_OUTOFVIDEOMEMORY;
    }

    object->lpVtbl = &IWineD3DTexture_Vtbl;
    object->resource.wineD3DDevice = This;
    IWineD3DDevice_AddRef(iface);
    object->resource.resourceType  = D3DRTYPE_TEXTURE;
    object->resource.parent        = parent;
    object->resource.ref           = 1;
    *ppTexture = (IWineD3DTexture *)object;

    TRACE("(%p) : W(%d) H(%d), Lvl(%d) Usage(%ld), Fmt(%u,%s), Pool(%s)\n", This, 
          Width, Height, Levels, Usage, Format, debug_d3dformat(Format), debug_d3dpool(Pool));
    object->width  = Width;
    object->height = Height;
    object->usage  = Usage;
    object->baseTexture.levels = Levels;
    object->baseTexture.format = Format;

    /* Calculate levels for mip mapping */
    if (Levels == 0) {
        object->baseTexture.levels++;
        tmpW = Width;
        tmpH = Height;
        while (tmpW > 1 && tmpH > 1) {
            tmpW = max(1, tmpW / 2);
            tmpH = max(1, tmpH / 2);
            object->baseTexture.levels++;
        }
        TRACE("Calculated levels = %d\n", object->baseTexture.levels);
    }

    /* Generate all the surfaces */
    tmpW = Width;
    tmpH = Height;
    for (i = 0; i < object->baseTexture.levels; i++) 
    {
        D3DCB_CreateSurface(This->parent, tmpW, tmpH, Format, Pool, 
                            (IWineD3DSurface **)&object->surfaces[i], pSharedHandle);
        object->surfaces[i]->container = (IUnknown*) object;
        object->surfaces[i]->currentDesc.Usage = Usage;
        object->surfaces[i]->currentDesc.Pool = Pool;

        /** 
         * As written in msdn in IDirect3DTexture8::LockRect
         *  Textures created in D3DPOOL_DEFAULT are not lockable.
         */
        if (D3DPOOL_DEFAULT == Pool) {
            object->surfaces[i]->lockable = FALSE;
        }

        TRACE("Created surface level %d @ %p, memory at %p\n", i, object->surfaces[i], object->surfaces[i]->allocatedMemory);
        tmpW = max(1, tmpW / 2);
        tmpH = max(1, tmpH / 2);
    }

    *ppTexture = (IWineD3DTexture *) object;
    TRACE("(%p) : Created texture %p\n", This, object);
    return D3D_OK;
}

HRESULT WINAPI IWineD3DDeviceImpl_CreateVolumeTexture(IWineD3DDevice *iface, 
                                                      UINT Width, UINT Height, UINT Depth, 
                                                      UINT Levels, DWORD Usage, 
                                                      D3DFORMAT Format, D3DPOOL Pool, 
                                                      IWineD3DVolumeTexture** ppVolumeTexture,
                                                      HANDLE* pSharedHandle, IUnknown *parent,
                                                      D3DCB_CREATEVOLUMEFN D3DCB_CreateVolume) {

    IWineD3DDeviceImpl        *This = (IWineD3DDeviceImpl *)iface;
    IWineD3DVolumeTextureImpl *object;
    unsigned int               i;
    UINT                       tmpW;
    UINT                       tmpH;
    UINT                       tmpD;

    object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(IWineD3DVolumeTextureImpl));
    if (NULL == object) {
        *ppVolumeTexture = NULL;
        return D3DERR_OUTOFVIDEOMEMORY;
    }

    object->lpVtbl = &IWineD3DVolumeTexture_Vtbl;
    object->resource.wineD3DDevice = This;
    IWineD3DDevice_AddRef(iface);
    object->resource.resourceType  = D3DRTYPE_VOLUMETEXTURE;
    object->resource.parent        = parent;
    object->resource.ref           = 1;
    *ppVolumeTexture = (IWineD3DVolumeTexture *)object;

    TRACE("(%p) : W(%d) H(%d) D(%d), Lvl(%d) Usage(%ld), Fmt(%u,%s), Pool(%s)\n", This, Width, Height, 
          Depth, Levels, Usage, Format, debug_d3dformat(Format), debug_d3dpool(Pool));

    object->width  = Width;
    object->height = Height;
    object->depth  = Depth;
    object->usage  = Usage;
    object->baseTexture.levels = Levels;
    object->baseTexture.format = Format;

    /* Calculate levels for mip mapping */
    if (Levels == 0) {
        object->baseTexture.levels++;
        tmpW = Width;
        tmpH = Height;
        tmpD = Depth;
        while (tmpW > 1 && tmpH > 1 && tmpD > 1) {
            tmpW = max(1, tmpW / 2);
            tmpH = max(1, tmpH / 2);
            tmpD = max(1, tmpD / 2);
            object->baseTexture.levels++;
        }
        TRACE("Calculated levels = %d\n", object->baseTexture.levels);
    }

    /* Generate all the surfaces */
    tmpW = Width;
    tmpH = Height;
    tmpD = Depth;

    for (i = 0; i < object->baseTexture.levels; i++) 
    {
        /* Create the volume - No entry point for this seperately?? */
        D3DCB_CreateVolume(This->parent, Width, Height, Depth, Format, Pool, Usage, 
                           (IWineD3DVolume **)&object->volumes[i], pSharedHandle);
        object->volumes[i]->container = (IUnknown*) object;

        tmpW = max(1, tmpW / 2);
        tmpH = max(1, tmpH / 2);
        tmpD = max(1, tmpD / 2);
    }

    *ppVolumeTexture = (IWineD3DVolumeTexture *) object;
    TRACE("(%p) : Created volume texture %p\n", This, object);
    return D3D_OK;
}

HRESULT WINAPI IWineD3DDeviceImpl_CreateVolume(IWineD3DDevice *iface, 
                                               UINT Width, UINT Height, UINT Depth, 
                                               DWORD Usage, 
                                               D3DFORMAT Format, D3DPOOL Pool, 
                                               IWineD3DVolume** ppVolume,
                                               HANDLE* pSharedHandle, IUnknown *parent) {

    IWineD3DDeviceImpl        *This = (IWineD3DDeviceImpl *)iface;
    IWineD3DVolumeImpl        *object;

    object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(IWineD3DVolumeImpl));
    if (NULL == object) {
        *ppVolume = NULL;
        return D3DERR_OUTOFVIDEOMEMORY;
    }

    object->lpVtbl = &IWineD3DVolume_Vtbl;
    object->wineD3DDevice = This;
    IWineD3DDevice_AddRef(iface);
    object->resourceType  = D3DRTYPE_VOLUME;
    object->parent        = parent;
    object->ref           = 1;
    *ppVolume = (IWineD3DVolume *)object;

    TRACE("(%p) : W(%d) H(%d) D(%d), Usage(%ld), Fmt(%u,%s), Pool(%s)\n", This, Width, Height, 
          Depth, Usage, Format, debug_d3dformat(Format), debug_d3dpool(Pool));

    object->currentDesc.Width  = Width;
    object->currentDesc.Height = Height;
    object->currentDesc.Depth  = Depth;
    object->currentDesc.Format = Format;
    object->currentDesc.Type   = D3DRTYPE_VOLUME;
    object->currentDesc.Pool   = Pool;
    object->currentDesc.Usage  = Usage;
    object->bytesPerPixel      = D3DFmtGetBpp(This, Format);

    /* Note: Volume textures cannot be dxtn, hence no need to check here */
    object->currentDesc.Size   = (Width * object->bytesPerPixel) * Height * Depth; 
    object->allocatedMemory    = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, object->currentDesc.Size);
    object->lockable = TRUE;
    object->locked = FALSE;
    memset(&object->lockedBox, 0, sizeof(D3DBOX));
    object->dirty = FALSE;
    return IWineD3DVolume_CleanDirtyBox((IWineD3DVolume *) object);
}

HRESULT WINAPI IWineD3DDeviceImpl_CreateCubeTexture(IWineD3DDevice *iface, UINT EdgeLength, 
                                                    UINT Levels, DWORD Usage, 
                                                    D3DFORMAT Format, D3DPOOL Pool, 
                                                    IWineD3DCubeTexture** ppCubeTexture,
                                                    HANDLE* pSharedHandle, IUnknown *parent,
                                                    D3DCB_CREATESURFACEFN D3DCB_CreateSurface) {

    IWineD3DDeviceImpl       *This = (IWineD3DDeviceImpl *)iface;
    IWineD3DCubeTextureImpl  *object;
    unsigned int              i,j;
    UINT                      tmpW;

    object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(IWineD3DCubeTextureImpl));
    if (NULL == object) {
        FIXME("Allocation of memory failed\n");
        *ppCubeTexture = NULL;
        return D3DERR_OUTOFVIDEOMEMORY;
    }

    object->lpVtbl = &IWineD3DCubeTexture_Vtbl;
    object->resource.wineD3DDevice = This;
    IWineD3DDevice_AddRef(iface);
    object->resource.resourceType  = D3DRTYPE_CUBETEXTURE;
    object->resource.parent        = parent;
    object->resource.ref           = 1;
    *ppCubeTexture = (IWineD3DCubeTexture *)object;

    /* Allocate the storage for it */
    TRACE("(%p) : Len(%d), Lvl(%d) Usage(%ld), Fmt(%u,%s), Pool(%s)\n", This, EdgeLength, Levels, Usage, Format, debug_d3dformat(Format), debug_d3dpool(Pool));
    
    object->usage              = Usage;
    object->edgeLength         = EdgeLength;
    object->baseTexture.levels = Levels;
    object->baseTexture.format = Format;

    /* Calculate levels for mip mapping */
    if (Levels == 0) {
        object->baseTexture.levels++;
        tmpW = EdgeLength;
        while (tmpW > 1) {
            tmpW = max(1, tmpW / 2);
            object->baseTexture.levels++;
        }
        TRACE("Calculated levels = %d\n", object->baseTexture.levels);
    }

    /* Generate all the surfaces */
    tmpW = EdgeLength;
    for (i = 0; i < object->baseTexture.levels; i++) {

        /* Create the 6 faces */
        for (j = 0; j < 6; j++) {

            D3DCB_CreateSurface(This->parent, tmpW, tmpW, Format, Pool, 
                                (IWineD3DSurface **)&object->surfaces[j][i], pSharedHandle);
            object->surfaces[j][i]->container = (IUnknown*) object;
            object->surfaces[j][i]->currentDesc.Usage = Usage;
            object->surfaces[j][i]->currentDesc.Pool = Pool;

            /** 
             * As written in msdn in IDirect3DCubeTexture8::LockRect
             *  Textures created in D3DPOOL_DEFAULT are not lockable.
             */
            if (D3DPOOL_DEFAULT == Pool) {
              object->surfaces[j][i]->lockable = FALSE;
            }

            TRACE("Created surface level %d @ %p, memory at %p\n", i, object->surfaces[j][i], object->surfaces[j][i]->allocatedMemory);
        }
        tmpW = max(1, tmpW / 2);
    }

    TRACE("(%p) : Created Cube Texture %p\n", This, object);
    *ppCubeTexture = (IWineD3DCubeTexture *) object;
    return D3D_OK;
}

/*****
 * Get / Set FVF
 *****/
HRESULT WINAPI IWineD3DDeviceImpl_SetFVF(IWineD3DDevice *iface, DWORD fvf) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    /* Update the current statte block */
    This->updateStateBlock->fvf              = fvf;
    This->updateStateBlock->changed.fvf      = TRUE;
    This->updateStateBlock->set.fvf          = TRUE;

    TRACE("(%p) : FVF Shader FVF set to %lx\n", This, fvf);
    
    /* No difference if recording or not */
    return D3D_OK;
}
HRESULT WINAPI IWineD3DDeviceImpl_GetFVF(IWineD3DDevice *iface, DWORD *pfvf) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p) : GetFVF returning %lx\n", This, This->stateBlock->fvf);
    *pfvf = This->stateBlock->fvf;
    return D3D_OK;
}

/*****
 * Get / Set Stream Source
 *****/
HRESULT WINAPI IWineD3DDeviceImpl_SetStreamSource(IWineD3DDevice *iface, UINT StreamNumber,IWineD3DVertexBuffer* pStreamData, UINT OffsetInBytes, UINT Stride) {
    IWineD3DDeviceImpl       *This = (IWineD3DDeviceImpl *)iface;
    IWineD3DVertexBuffer     *oldSrc;

    oldSrc = This->stateBlock->stream_source[StreamNumber];
    TRACE("(%p) : StreamNo: %d, OldStream (%p), NewStream (%p), NewStride %d\n", This, StreamNumber, oldSrc, pStreamData, Stride);

    This->updateStateBlock->changed.stream_source[StreamNumber] = TRUE;
    This->updateStateBlock->set.stream_source[StreamNumber]     = TRUE;
    This->updateStateBlock->stream_stride[StreamNumber]         = Stride;
    This->updateStateBlock->stream_source[StreamNumber]         = pStreamData;
    This->updateStateBlock->stream_offset[StreamNumber]         = OffsetInBytes;

    /* Handle recording of state blocks */
    if (This->isRecordingState) {
        TRACE("Recording... not performing anything\n");
        return D3D_OK;
    }

    /* Not recording... */
    if (oldSrc != NULL) IWineD3DVertexBuffer_Release(oldSrc);
    if (pStreamData != NULL) IWineD3DVertexBuffer_AddRef(pStreamData);

    return D3D_OK;
}

HRESULT WINAPI IWineD3DDeviceImpl_GetStreamSource(IWineD3DDevice *iface, UINT StreamNumber,IWineD3DVertexBuffer** pStream, UINT *pOffset, UINT* pStride) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    TRACE("(%p) : StreamNo: %d, Stream (%p), Stride %d\n", This, StreamNumber, This->stateBlock->stream_source[StreamNumber], This->stateBlock->stream_stride[StreamNumber]);
    *pStream = This->stateBlock->stream_source[StreamNumber];
    *pStride = This->stateBlock->stream_stride[StreamNumber];
    *pOffset = This->stateBlock->stream_offset[StreamNumber];
    if (*pStream != NULL) IWineD3DVertexBuffer_AddRef(*pStream); /* We have created a new reference to the VB */
    return D3D_OK;
}

/*****
 * Get / Set & Multipy Transform
 *****/
HRESULT  WINAPI  IWineD3DDeviceImpl_SetTransform(IWineD3DDevice *iface, D3DTRANSFORMSTATETYPE d3dts, CONST D3DMATRIX* lpmatrix) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    /* Most of this routine, comments included copied from ddraw tree initially: */
    TRACE("(%p) : Transform State=%d\n", This, d3dts);

    /* Handle recording of state blocks */
    if (This->isRecordingState) {
        TRACE("Recording... not performing anything\n");
        This->updateStateBlock->changed.transform[d3dts] = TRUE;
        This->updateStateBlock->set.transform[d3dts]     = TRUE;
        memcpy(&This->updateStateBlock->transforms[d3dts], lpmatrix, sizeof(D3DMATRIX));
        return D3D_OK;
    }

    /*
     * If the new matrix is the same as the current one,
     * we cut off any further processing. this seems to be a reasonable
     * optimization because as was noticed, some apps (warcraft3 for example)
     * tend towards setting the same matrix repeatedly for some reason.
     *
     * From here on we assume that the new matrix is different, wherever it matters.
     */
    if (!memcmp(&This->stateBlock->transforms[d3dts].u.m[0][0], lpmatrix, sizeof(D3DMATRIX))) {
        TRACE("The app is setting the same matrix over again\n");
        return D3D_OK;
    } else {
        conv_mat(lpmatrix, &This->stateBlock->transforms[d3dts].u.m[0][0]);
    }

    /*
       ScreenCoord = ProjectionMat * ViewMat * WorldMat * ObjectCoord
       where ViewMat = Camera space, WorldMat = world space.

       In OpenGL, camera and world space is combined into GL_MODELVIEW
       matrix.  The Projection matrix stay projection matrix. 
     */

    /* Capture the times we can just ignore the change for now */
    if (d3dts == D3DTS_WORLDMATRIX(0)) {
        This->modelview_valid = FALSE;
        return D3D_OK;

    } else if (d3dts == D3DTS_PROJECTION) {
        This->proj_valid = FALSE;
        return D3D_OK;

    } else if (d3dts >= D3DTS_WORLDMATRIX(1) && d3dts <= D3DTS_WORLDMATRIX(255)) { 
        /* Indexed Vertex Blending Matrices 256 -> 511  */
        /* Use arb_vertex_blend or NV_VERTEX_WEIGHTING? */
        FIXME("D3DTS_WORLDMATRIX(1..255) not handled\n");
        return D3D_OK;
    } 
    
    /* Now we really are going to have to change a matrix */
    ENTER_GL();

    if (d3dts >= D3DTS_TEXTURE0 && d3dts <= D3DTS_TEXTURE7) { /* handle texture matrices */
        if (d3dts < GL_LIMITS(textures)) {
            int tex = d3dts - D3DTS_TEXTURE0;
            GLACTIVETEXTURE(tex);
            set_texture_matrix((float *)lpmatrix, 
                               This->updateStateBlock->textureState[tex][D3DTSS_TEXTURETRANSFORMFLAGS]);
        }

    } else if (d3dts == D3DTS_VIEW) { /* handle the VIEW matrice */
        unsigned int k;

        /* If we are changing the View matrix, reset the light and clipping planes to the new view   
         * NOTE: We have to reset the positions even if the light/plane is not currently
         *       enabled, since the call to enable it will not reset the position.                 
         * NOTE2: Apparently texture transforms do NOT need reapplying
         */
        
        PLIGHTINFOEL *lightChain = NULL;
        This->modelview_valid = FALSE;
        This->view_ident = !memcmp(lpmatrix, identity, 16*sizeof(float));

        glMatrixMode(GL_MODELVIEW);
        checkGLcall("glMatrixMode(GL_MODELVIEW)");
        glPushMatrix();
        glLoadMatrixf((float *)lpmatrix);
        checkGLcall("glLoadMatrixf(...)");

        /* Reset lights */
        lightChain = This->stateBlock->lights;
        while (lightChain && lightChain->glIndex != -1) {
            glLightfv(GL_LIGHT0 + lightChain->glIndex, GL_POSITION, lightChain->lightPosn);
            checkGLcall("glLightfv posn");
            glLightfv(GL_LIGHT0 + lightChain->glIndex, GL_SPOT_DIRECTION, lightChain->lightDirn);
            checkGLcall("glLightfv dirn");
            lightChain = lightChain->next;
        }

        /* Reset Clipping Planes if clipping is enabled */
        for (k = 0; k < GL_LIMITS(clipplanes); k++) {
            glClipPlane(GL_CLIP_PLANE0 + k, This->stateBlock->clipplane[k]);
            checkGLcall("glClipPlane");
        }
        glPopMatrix();

    } else { /* What was requested!?? */
        WARN("invalid matrix specified: %i\n", d3dts);
    }

    /* Release lock, all done */
    LEAVE_GL();
    return D3D_OK;

}
HRESULT WINAPI IWineD3DDeviceImpl_GetTransform(IWineD3DDevice *iface, D3DTRANSFORMSTATETYPE State, D3DMATRIX* pMatrix) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p) : for Transform State %d\n", This, State);
    memcpy(pMatrix, &This->stateBlock->transforms[State], sizeof(D3DMATRIX));
    return D3D_OK;
}

HRESULT WINAPI IWineD3DDeviceImpl_MultiplyTransform(IWineD3DDevice *iface, D3DTRANSFORMSTATETYPE State, CONST D3DMATRIX* pMatrix) {
    D3DMATRIX *mat = NULL;
    D3DMATRIX temp;

    /* Note: Using 'updateStateBlock' rather than 'stateblock' in the code
     * below means it will be recorded in a state block change, but it
     * works regardless where it is recorded. 
     * If this is found to be wrong, change to StateBlock.
     */
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p) : For state %u\n", This, State);

    if (State < HIGHEST_TRANSFORMSTATE)
    {
        mat = &This->updateStateBlock->transforms[State];
    } else {
        FIXME("Unhandled transform state!!\n");
    }

    /* Copied from ddraw code:  */
    temp.u.s._11 = (mat->u.s._11 * pMatrix->u.s._11) + (mat->u.s._21 * pMatrix->u.s._12) + (mat->u.s._31 * pMatrix->u.s._13) + (mat->u.s._41 * pMatrix->u.s._14);
    temp.u.s._21 = (mat->u.s._11 * pMatrix->u.s._21) + (mat->u.s._21 * pMatrix->u.s._22) + (mat->u.s._31 * pMatrix->u.s._23) + (mat->u.s._41 * pMatrix->u.s._24);
    temp.u.s._31 = (mat->u.s._11 * pMatrix->u.s._31) + (mat->u.s._21 * pMatrix->u.s._32) + (mat->u.s._31 * pMatrix->u.s._33) + (mat->u.s._41 * pMatrix->u.s._34);
    temp.u.s._41 = (mat->u.s._11 * pMatrix->u.s._41) + (mat->u.s._21 * pMatrix->u.s._42) + (mat->u.s._31 * pMatrix->u.s._43) + (mat->u.s._41 * pMatrix->u.s._44);

    temp.u.s._12 = (mat->u.s._12 * pMatrix->u.s._11) + (mat->u.s._22 * pMatrix->u.s._12) + (mat->u.s._32 * pMatrix->u.s._13) + (mat->u.s._42 * pMatrix->u.s._14);
    temp.u.s._22 = (mat->u.s._12 * pMatrix->u.s._21) + (mat->u.s._22 * pMatrix->u.s._22) + (mat->u.s._32 * pMatrix->u.s._23) + (mat->u.s._42 * pMatrix->u.s._24);
    temp.u.s._32 = (mat->u.s._12 * pMatrix->u.s._31) + (mat->u.s._22 * pMatrix->u.s._32) + (mat->u.s._32 * pMatrix->u.s._33) + (mat->u.s._42 * pMatrix->u.s._34);
    temp.u.s._42 = (mat->u.s._12 * pMatrix->u.s._41) + (mat->u.s._22 * pMatrix->u.s._42) + (mat->u.s._32 * pMatrix->u.s._43) + (mat->u.s._42 * pMatrix->u.s._44);

    temp.u.s._13 = (mat->u.s._13 * pMatrix->u.s._11) + (mat->u.s._23 * pMatrix->u.s._12) + (mat->u.s._33 * pMatrix->u.s._13) + (mat->u.s._43 * pMatrix->u.s._14);
    temp.u.s._23 = (mat->u.s._13 * pMatrix->u.s._21) + (mat->u.s._23 * pMatrix->u.s._22) + (mat->u.s._33 * pMatrix->u.s._23) + (mat->u.s._43 * pMatrix->u.s._24);
    temp.u.s._33 = (mat->u.s._13 * pMatrix->u.s._31) + (mat->u.s._23 * pMatrix->u.s._32) + (mat->u.s._33 * pMatrix->u.s._33) + (mat->u.s._43 * pMatrix->u.s._34);
    temp.u.s._43 = (mat->u.s._13 * pMatrix->u.s._41) + (mat->u.s._23 * pMatrix->u.s._42) + (mat->u.s._33 * pMatrix->u.s._43) + (mat->u.s._43 * pMatrix->u.s._44);

    temp.u.s._14 = (mat->u.s._14 * pMatrix->u.s._11) + (mat->u.s._24 * pMatrix->u.s._12) + (mat->u.s._34 * pMatrix->u.s._13) + (mat->u.s._44 * pMatrix->u.s._14);
    temp.u.s._24 = (mat->u.s._14 * pMatrix->u.s._21) + (mat->u.s._24 * pMatrix->u.s._22) + (mat->u.s._34 * pMatrix->u.s._23) + (mat->u.s._44 * pMatrix->u.s._24);
    temp.u.s._34 = (mat->u.s._14 * pMatrix->u.s._31) + (mat->u.s._24 * pMatrix->u.s._32) + (mat->u.s._34 * pMatrix->u.s._33) + (mat->u.s._44 * pMatrix->u.s._34);
    temp.u.s._44 = (mat->u.s._14 * pMatrix->u.s._41) + (mat->u.s._24 * pMatrix->u.s._42) + (mat->u.s._34 * pMatrix->u.s._43) + (mat->u.s._44 * pMatrix->u.s._44);

    /* Apply change via set transform - will reapply to eg. lights this way */
    IWineD3DDeviceImpl_SetTransform(iface, State, &temp);
    return D3D_OK;
}

/*****
 * Get / Set Light
 *   WARNING: This code relies on the fact that D3DLIGHT8 == D3DLIGHT9
 *****/
/* Note lights are real special cases. Although the device caps state only eg. 8 are supported,
   you can reference any indexes you want as long as that number max are enabled at any
   one point in time! Therefore since the indexes can be anything, we need a linked list of them.
   However, this causes stateblock problems. When capturing the state block, I duplicate the list,
   but when recording, just build a chain pretty much of commands to be replayed.                  */
   
HRESULT WINAPI IWineD3DDeviceImpl_SetLight(IWineD3DDevice *iface, DWORD Index, CONST WINED3DLIGHT* pLight) {
    float rho;
    PLIGHTINFOEL *object, *temp;

    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p) : Idx(%ld), pLight(%p)\n", This, Index, pLight);

    /* If recording state block, just add to end of lights chain */
    if (This->isRecordingState) {
        object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(PLIGHTINFOEL));
        if (NULL == object) {
            return D3DERR_OUTOFVIDEOMEMORY;
        }
        memcpy(&object->OriginalParms, pLight, sizeof(D3DLIGHT9));
        object->OriginalIndex = Index;
        object->glIndex = -1;
        object->changed = TRUE;

        /* Add to the END of the chain of lights changes to be replayed */
        if (This->updateStateBlock->lights == NULL) {
            This->updateStateBlock->lights = object;
        } else {
            temp = This->updateStateBlock->lights;
            while (temp->next != NULL) temp=temp->next;
            temp->next = object;
        }
        TRACE("Recording... not performing anything more\n");
        return D3D_OK;
    }

    /* Ok, not recording any longer so do real work */
    object = This->stateBlock->lights;
    while (object != NULL && object->OriginalIndex != Index) object = object->next;

    /* If we didn't find it in the list of lights, time to add it */
    if (object == NULL) {
        PLIGHTINFOEL *insertAt,*prevPos;

        object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(PLIGHTINFOEL));
        if (NULL == object) {
            return D3DERR_OUTOFVIDEOMEMORY;
        }
        object->OriginalIndex = Index;
        object->glIndex = -1;

        /* Add it to the front of list with the idea that lights will be changed as needed 
           BUT after any lights currently assigned GL indexes                             */
        insertAt = This->stateBlock->lights;
        prevPos  = NULL;
        while (insertAt != NULL && insertAt->glIndex != -1) {
            prevPos  = insertAt;
            insertAt = insertAt->next;
        }

        if (insertAt == NULL && prevPos == NULL) { /* Start of list */
            This->stateBlock->lights = object;
        } else if (insertAt == NULL) { /* End of list */
            prevPos->next = object;
            object->prev = prevPos;
        } else { /* Middle of chain */
            if (prevPos == NULL) {
                This->stateBlock->lights = object;
            } else {
                prevPos->next = object;
            }
            object->prev = prevPos;
            object->next = insertAt;
            insertAt->prev = object;
        }
    }

    /* Initialze the object */
    TRACE("Light %ld setting to type %d, Diffuse(%f,%f,%f,%f), Specular(%f,%f,%f,%f), Ambient(%f,%f,%f,%f)\n", Index, pLight->Type,
          pLight->Diffuse.r, pLight->Diffuse.g, pLight->Diffuse.b, pLight->Diffuse.a,
          pLight->Specular.r, pLight->Specular.g, pLight->Specular.b, pLight->Specular.a,
          pLight->Ambient.r, pLight->Ambient.g, pLight->Ambient.b, pLight->Ambient.a);
    TRACE("... Pos(%f,%f,%f), Dirn(%f,%f,%f)\n", pLight->Position.x, pLight->Position.y, pLight->Position.z,
          pLight->Direction.x, pLight->Direction.y, pLight->Direction.z);
    TRACE("... Range(%f), Falloff(%f), Theta(%f), Phi(%f)\n", pLight->Range, pLight->Falloff, pLight->Theta, pLight->Phi);

    /* Save away the information */
    memcpy(&object->OriginalParms, pLight, sizeof(D3DLIGHT9));

    switch (pLight->Type) {
    case D3DLIGHT_POINT:
        /* Position */
        object->lightPosn[0] = pLight->Position.x;
        object->lightPosn[1] = pLight->Position.y;
        object->lightPosn[2] = pLight->Position.z;
        object->lightPosn[3] = 1.0f;
        object->cutoff = 180.0f;
        /* FIXME: Range */
        break;

    case D3DLIGHT_DIRECTIONAL:
        /* Direction */
        object->lightPosn[0] = -pLight->Direction.x;
        object->lightPosn[1] = -pLight->Direction.y;
        object->lightPosn[2] = -pLight->Direction.z;
        object->lightPosn[3] = 0.0;
        object->exponent     = 0.0f;
        object->cutoff       = 180.0f;
        break;

    case D3DLIGHT_SPOT:
        /* Position */
        object->lightPosn[0] = pLight->Position.x;
        object->lightPosn[1] = pLight->Position.y;
        object->lightPosn[2] = pLight->Position.z;
        object->lightPosn[3] = 1.0;

        /* Direction */
        object->lightDirn[0] = pLight->Direction.x;
        object->lightDirn[1] = pLight->Direction.y;
        object->lightDirn[2] = pLight->Direction.z;
        object->lightDirn[3] = 1.0;

        /*
         * opengl-ish and d3d-ish spot lights use too different models for the
         * light "intensity" as a function of the angle towards the main light direction,
         * so we only can approximate very roughly.
         * however spot lights are rather rarely used in games (if ever used at all).
         * furthermore if still used, probably nobody pays attention to such details.
         */
        if (pLight->Falloff == 0) {
            rho = 6.28f;
        } else {
            rho = pLight->Theta + (pLight->Phi - pLight->Theta)/(2*pLight->Falloff);
        }
        if (rho < 0.0001) rho = 0.0001f;
        object->exponent = -0.3/log(cos(rho/2));
        object->cutoff = pLight->Phi*90/M_PI;

        /* FIXME: Range */
        break;

    default:
        FIXME("Unrecognized light type %d\n", pLight->Type);
    }

    /* Update the live definitions if the light is currently assigned a glIndex */
    if (object->glIndex != -1) {
        setup_light(iface, object->glIndex, object);
    }
    return D3D_OK;
}

HRESULT WINAPI IWineD3DDeviceImpl_GetLight(IWineD3DDevice *iface, DWORD Index, WINED3DLIGHT* pLight) {
    PLIGHTINFOEL *lightInfo = NULL;
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface; 
    TRACE("(%p) : Idx(%ld), pLight(%p)\n", This, Index, pLight);
    
    /* Locate the light in the live lights */
    lightInfo = This->stateBlock->lights;
    while (lightInfo != NULL && lightInfo->OriginalIndex != Index) lightInfo = lightInfo->next;

    if (lightInfo == NULL) {
        TRACE("Light information requested but light not defined\n");
        return D3DERR_INVALIDCALL;
    }

    memcpy(pLight, &lightInfo->OriginalParms, sizeof(D3DLIGHT9));
    return D3D_OK;
}

/*****
 * Get / Set Light Enable 
 *   (Note for consistency, renamed d3dx function by adding the 'set' prefix)
 *****/
HRESULT WINAPI IWineD3DDeviceImpl_SetLightEnable(IWineD3DDevice *iface, DWORD Index, BOOL Enable) {
    PLIGHTINFOEL *lightInfo = NULL;
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p) : Idx(%ld), enable? %d\n", This, Index, Enable);

    /* If recording state block, just add to end of lights chain with changedEnable set to true */
    if (This->isRecordingState) {
        lightInfo = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(PLIGHTINFOEL));
        if (NULL == lightInfo) {
            return D3DERR_OUTOFVIDEOMEMORY;
        }
        lightInfo->OriginalIndex = Index;
        lightInfo->glIndex = -1;
        lightInfo->enabledChanged = TRUE;

        /* Add to the END of the chain of lights changes to be replayed */
        if (This->updateStateBlock->lights == NULL) {
            This->updateStateBlock->lights = lightInfo;
        } else {
            PLIGHTINFOEL *temp = This->updateStateBlock->lights;
            while (temp->next != NULL) temp=temp->next;
            temp->next = lightInfo;
        }
        TRACE("Recording... not performing anything more\n");
        return D3D_OK;
    }

    /* Not recording... So, locate the light in the live lights */
    lightInfo = This->stateBlock->lights;
    while (lightInfo != NULL && lightInfo->OriginalIndex != Index) lightInfo = lightInfo->next;

    /* Special case - enabling an undefined light creates one with a strict set of parms! */
    if (lightInfo == NULL) {
        D3DLIGHT9 lightParms;
        /* Warning - untested code :-) Prob safe to change fixme to a trace but
             wait until someone confirms it seems to work!                     */
        TRACE("Light enabled requested but light not defined, so defining one!\n"); 
        lightParms.Type = D3DLIGHT_DIRECTIONAL;
        lightParms.Diffuse.r = 1.0;
        lightParms.Diffuse.g = 1.0;
        lightParms.Diffuse.b = 1.0;
        lightParms.Diffuse.a = 0.0;
        lightParms.Specular.r = 0.0;
        lightParms.Specular.g = 0.0;
        lightParms.Specular.b = 0.0;
        lightParms.Specular.a = 0.0;
        lightParms.Ambient.r = 0.0;
        lightParms.Ambient.g = 0.0;
        lightParms.Ambient.b = 0.0;
        lightParms.Ambient.a = 0.0;
        lightParms.Position.x = 0.0;
        lightParms.Position.y = 0.0;
        lightParms.Position.z = 0.0;
        lightParms.Direction.x = 0.0;
        lightParms.Direction.y = 0.0;
        lightParms.Direction.z = 1.0;
        lightParms.Range = 0.0;
        lightParms.Falloff = 0.0;
        lightParms.Attenuation0 = 0.0;
        lightParms.Attenuation1 = 0.0;
        lightParms.Attenuation2 = 0.0;
        lightParms.Theta = 0.0;
        lightParms.Phi = 0.0;
        IWineD3DDeviceImpl_SetLight(iface, Index, &lightParms);

        /* Search for it again! Should be fairly quick as near head of list */
        lightInfo = This->stateBlock->lights;
        while (lightInfo != NULL && lightInfo->OriginalIndex != Index) lightInfo = lightInfo->next;
        if (lightInfo == NULL) {
            FIXME("Adding default lights has failed dismally\n");
            return D3DERR_INVALIDCALL;
        }
    }

    /* OK, we now have a light... */
    if (Enable == FALSE) {

        /* If we are disabling it, check it was enabled, and
           still only do something if it has assigned a glIndex (which it should have!)   */
        if ((lightInfo->lightEnabled) && (lightInfo->glIndex != -1)) {
            TRACE("Disabling light set up at gl idx %ld\n", lightInfo->glIndex);
            ENTER_GL();
            glDisable(GL_LIGHT0 + lightInfo->glIndex);
            checkGLcall("glDisable GL_LIGHT0+Index");
            LEAVE_GL();
        } else {
            TRACE("Nothing to do as light was not enabled\n");
        }
        lightInfo->lightEnabled = FALSE;
    } else {

        /* We are enabling it. If it is enabled, it's really simple */
        if (lightInfo->lightEnabled) {
            /* nop */
            TRACE("Nothing to do as light was enabled\n");

        /* If it already has a glIndex, it's still simple */
        } else if (lightInfo->glIndex != -1) {
            TRACE("Reusing light as already set up at gl idx %ld\n", lightInfo->glIndex);
            lightInfo->lightEnabled = TRUE;
            ENTER_GL();
            glEnable(GL_LIGHT0 + lightInfo->glIndex);
            checkGLcall("glEnable GL_LIGHT0+Index already setup");
            LEAVE_GL();

        /* Otherwise got to find space - lights are ordered gl indexes first */
        } else {
            PLIGHTINFOEL *bsf  = NULL;
            PLIGHTINFOEL *pos  = This->stateBlock->lights;
            PLIGHTINFOEL *prev = NULL;
            int           Index= 0;
            int           glIndex = -1;

            /* Try to minimize changes as much as possible */
            while (pos != NULL && pos->glIndex != -1 && Index < This->maxConcurrentLights) {

                /* Try to remember which index can be replaced if necessary */
                if (bsf==NULL && pos->lightEnabled == FALSE) {
                    /* Found a light we can replace, save as best replacement */
                    bsf = pos;
                }

                /* Step to next space */
                prev = pos;
                pos = pos->next;
                Index ++;
            }

            /* If we have too many active lights, fail the call */
            if ((Index == This->maxConcurrentLights) && (bsf == NULL)) {
                FIXME("Program requests too many concurrent lights\n");
                return D3DERR_INVALIDCALL;

            /* If we have allocated all lights, but not all are enabled,
               reuse one which is not enabled                           */
            } else if (Index == This->maxConcurrentLights) {
                /* use bsf - Simply swap the new light and the BSF one */
                PLIGHTINFOEL *bsfNext = bsf->next;
                PLIGHTINFOEL *bsfPrev = bsf->prev;

                /* Sort out ends */
                if (lightInfo->next != NULL) lightInfo->next->prev = bsf;
                if (bsf->prev != NULL) {
                    bsf->prev->next = lightInfo;
                } else {
                    This->stateBlock->lights = lightInfo;
                }

                /* If not side by side, lots of chains to update */
                if (bsf->next != lightInfo) {
                    lightInfo->prev->next = bsf;
                    bsf->next->prev = lightInfo;
                    bsf->next       = lightInfo->next;
                    bsf->prev       = lightInfo->prev;
                    lightInfo->next = bsfNext;
                    lightInfo->prev = bsfPrev;

                } else {
                    /* Simple swaps */
                    bsf->prev = lightInfo;
                    bsf->next = lightInfo->next;
                    lightInfo->next = bsf;
                    lightInfo->prev = bsfPrev;
                }


                /* Update states */
                glIndex = bsf->glIndex;
                bsf->glIndex = -1;
                lightInfo->glIndex = glIndex;
                lightInfo->lightEnabled = TRUE;

                /* Finally set up the light in gl itself */
                TRACE("Replacing light which was set up at gl idx %ld\n", lightInfo->glIndex);
                ENTER_GL();
                setup_light(iface, glIndex, lightInfo);
                glEnable(GL_LIGHT0 + glIndex);
                checkGLcall("glEnable GL_LIGHT0 new setup");
                LEAVE_GL();

            /* If we reached the end of the allocated lights, with space in the
               gl lights, setup a new light                                     */
            } else if (pos->glIndex == -1) {

                /* We reached the end of the allocated gl lights, so already 
                    know the index of the next one!                          */
                glIndex = Index;
                lightInfo->glIndex = glIndex;
                lightInfo->lightEnabled = TRUE;

                /* In an ideal world, it's already in the right place */
                if (lightInfo->prev == NULL || lightInfo->prev->glIndex!=-1) {
                   /* No need to move it */
                } else {
                    /* Remove this light from the list */
                    lightInfo->prev->next = lightInfo->next;
                    if (lightInfo->next != NULL) {
                        lightInfo->next->prev = lightInfo->prev;
                    }

                    /* Add in at appropriate place (inbetween prev and pos) */
                    lightInfo->prev = prev;
                    lightInfo->next = pos;
                    if (prev == NULL) {
                        This->stateBlock->lights = lightInfo;
                    } else {
                        prev->next = lightInfo;
                    }
                    if (pos != NULL) {
                        pos->prev = lightInfo;
                    }
                }

                /* Finally set up the light in gl itself */
                TRACE("Defining new light at gl idx %ld\n", lightInfo->glIndex);
                ENTER_GL();
                setup_light(iface, glIndex, lightInfo);
                glEnable(GL_LIGHT0 + glIndex);
                checkGLcall("glEnable GL_LIGHT0 new setup");
                LEAVE_GL();
                
            }
        }
    }
    return D3D_OK;
}

HRESULT WINAPI IWineD3DDeviceImpl_GetLightEnable(IWineD3DDevice *iface, DWORD Index,BOOL* pEnable) {

    PLIGHTINFOEL *lightInfo = NULL;
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface; 
    TRACE("(%p) : for idx(%ld)\n", This, Index);
    
    /* Locate the light in the live lights */
    lightInfo = This->stateBlock->lights;
    while (lightInfo != NULL && lightInfo->OriginalIndex != Index) lightInfo = lightInfo->next;

    if (lightInfo == NULL) {
        TRACE("Light enabled state requested but light not defined\n");
        return D3DERR_INVALIDCALL;
    }
    *pEnable = lightInfo->lightEnabled;
    return D3D_OK;
}

/*****
 * Get / Set Clip Planes
 *****/
HRESULT WINAPI IWineD3DDeviceImpl_SetClipPlane(IWineD3DDevice *iface, DWORD Index, CONST float *pPlane) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p) : for idx %ld, %p\n", This, Index, pPlane);

    /* Validate Index */
    if (Index >= GL_LIMITS(clipplanes)) {
        TRACE("Application has requested clipplane this device doesn't support\n");
        return D3DERR_INVALIDCALL;
    }

    This->updateStateBlock->changed.clipplane[Index] = TRUE;
    This->updateStateBlock->set.clipplane[Index] = TRUE;
    This->updateStateBlock->clipplane[Index][0] = pPlane[0];
    This->updateStateBlock->clipplane[Index][1] = pPlane[1];
    This->updateStateBlock->clipplane[Index][2] = pPlane[2];
    This->updateStateBlock->clipplane[Index][3] = pPlane[3];

    /* Handle recording of state blocks */
    if (This->isRecordingState) {
        TRACE("Recording... not performing anything\n");
        return D3D_OK;
    }

    /* Apply it */

    ENTER_GL();

    /* Clip Plane settings are affected by the model view in OpenGL, the View transform in direct3d */
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadMatrixf((float *) &This->stateBlock->transforms[D3DTS_VIEW].u.m[0][0]);

    TRACE("Clipplane [%f,%f,%f,%f]\n", 
          This->updateStateBlock->clipplane[Index][0], 
          This->updateStateBlock->clipplane[Index][1],
          This->updateStateBlock->clipplane[Index][2], 
          This->updateStateBlock->clipplane[Index][3]);
    glClipPlane(GL_CLIP_PLANE0 + Index, This->updateStateBlock->clipplane[Index]);
    checkGLcall("glClipPlane");

    glPopMatrix();
    LEAVE_GL();

    return D3D_OK;
}

HRESULT WINAPI IWineD3DDeviceImpl_GetClipPlane(IWineD3DDevice *iface, DWORD Index, float *pPlane) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p) : for idx %ld\n", This, Index);

    /* Validate Index */
    if (Index >= GL_LIMITS(clipplanes)) {
        TRACE("Application has requested clipplane this device doesn't support\n");
        return D3DERR_INVALIDCALL;
    }

    pPlane[0] = This->stateBlock->clipplane[Index][0];
    pPlane[1] = This->stateBlock->clipplane[Index][1];
    pPlane[2] = This->stateBlock->clipplane[Index][2];
    pPlane[3] = This->stateBlock->clipplane[Index][3];
    return D3D_OK;
}

/*****
 * Get / Set Clip Plane Status
 *   WARNING: This code relies on the fact that D3DCLIPSTATUS8 == D3DCLIPSTATUS9
 *****/
HRESULT  WINAPI  IWineD3DDeviceImpl_SetClipStatus(IWineD3DDevice *iface, CONST WINED3DCLIPSTATUS* pClipStatus) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    FIXME("(%p) : stub\n", This);
    if (NULL == pClipStatus) {
      return D3DERR_INVALIDCALL;
    }
    This->updateStateBlock->clip_status.ClipUnion = pClipStatus->ClipUnion;
    This->updateStateBlock->clip_status.ClipIntersection = pClipStatus->ClipIntersection;
    return D3D_OK;
}

HRESULT  WINAPI  IWineD3DDeviceImpl_GetClipStatus(IWineD3DDevice *iface, WINED3DCLIPSTATUS* pClipStatus) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    FIXME("(%p) : stub\n", This);    
    if (NULL == pClipStatus) {
      return D3DERR_INVALIDCALL;
    }
    pClipStatus->ClipUnion = This->updateStateBlock->clip_status.ClipUnion;
    pClipStatus->ClipIntersection = This->updateStateBlock->clip_status.ClipIntersection;
    return D3D_OK;
}

/*****
 * Get / Set Material
 *   WARNING: This code relies on the fact that D3DMATERIAL8 == D3DMATERIAL9
 *****/
HRESULT WINAPI IWineD3DDeviceImpl_SetMaterial(IWineD3DDevice *iface, CONST WINED3DMATERIAL* pMaterial) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    This->updateStateBlock->changed.material = TRUE;
    This->updateStateBlock->set.material = TRUE;
    memcpy(&This->updateStateBlock->material, pMaterial, sizeof(WINED3DMATERIAL));

    /* Handle recording of state blocks */
    if (This->isRecordingState) {
        TRACE("Recording... not performing anything\n");
        return D3D_OK;
    }

    ENTER_GL();
    TRACE("(%p) : Diffuse (%f,%f,%f,%f)\n", This, pMaterial->Diffuse.r, pMaterial->Diffuse.g, pMaterial->Diffuse.b, pMaterial->Diffuse.a);
    TRACE("(%p) : Ambient (%f,%f,%f,%f)\n", This, pMaterial->Ambient.r, pMaterial->Ambient.g, pMaterial->Ambient.b, pMaterial->Ambient.a);
    TRACE("(%p) : Specular (%f,%f,%f,%f)\n", This, pMaterial->Specular.r, pMaterial->Specular.g, pMaterial->Specular.b, pMaterial->Specular.a);
    TRACE("(%p) : Emissive (%f,%f,%f,%f)\n", This, pMaterial->Emissive.r, pMaterial->Emissive.g, pMaterial->Emissive.b, pMaterial->Emissive.a);
    TRACE("(%p) : Power (%f)\n", This, pMaterial->Power);

    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, (float*) &This->updateStateBlock->material.Ambient);
    checkGLcall("glMaterialfv");
    glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, (float*) &This->updateStateBlock->material.Diffuse);
    checkGLcall("glMaterialfv");

    /* Only change material color if specular is enabled, otherwise it is set to black */
    if (This->stateBlock->renderState[D3DRS_SPECULARENABLE]) {
       glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, (float*) &This->updateStateBlock->material.Specular);
       checkGLcall("glMaterialfv");
    } else {
       float black[4] = {0.0f, 0.0f, 0.0f, 0.0f};
       glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, &black[0]);
       checkGLcall("glMaterialfv");
    }
    glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, (float*) &This->updateStateBlock->material.Emissive);
    checkGLcall("glMaterialfv");
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, This->updateStateBlock->material.Power);
    checkGLcall("glMaterialf");

    LEAVE_GL();
    return D3D_OK;
}

HRESULT WINAPI IWineD3DDeviceImpl_GetMaterial(IWineD3DDevice *iface, WINED3DMATERIAL* pMaterial) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    memcpy(pMaterial, &This->updateStateBlock->material, sizeof (WINED3DMATERIAL));
    TRACE("(%p) : Diffuse (%f,%f,%f,%f)\n", This, pMaterial->Diffuse.r, pMaterial->Diffuse.g, pMaterial->Diffuse.b, pMaterial->Diffuse.a);
    TRACE("(%p) : Ambient (%f,%f,%f,%f)\n", This, pMaterial->Ambient.r, pMaterial->Ambient.g, pMaterial->Ambient.b, pMaterial->Ambient.a);
    TRACE("(%p) : Specular (%f,%f,%f,%f)\n", This, pMaterial->Specular.r, pMaterial->Specular.g, pMaterial->Specular.b, pMaterial->Specular.a);
    TRACE("(%p) : Emissive (%f,%f,%f,%f)\n", This, pMaterial->Emissive.r, pMaterial->Emissive.g, pMaterial->Emissive.b, pMaterial->Emissive.a);
    TRACE("(%p) : Power (%f)\n", This, pMaterial->Power);
    return D3D_OK;
}

/*****
 * Get / Set Indices
 *****/
HRESULT WINAPI IWineD3DDeviceImpl_SetIndices(IWineD3DDevice *iface, IWineD3DIndexBuffer* pIndexData, 
                                             UINT BaseVertexIndex) {
    IWineD3DDeviceImpl  *This = (IWineD3DDeviceImpl *)iface;
    IWineD3DIndexBuffer *oldIdxs;

    TRACE("(%p) : Setting to %p, base %d\n", This, pIndexData, BaseVertexIndex);
    oldIdxs = This->updateStateBlock->pIndexData;

    This->updateStateBlock->changed.indices = TRUE;
    This->updateStateBlock->set.indices = TRUE;
    This->updateStateBlock->pIndexData = pIndexData;
    This->updateStateBlock->baseVertexIndex = BaseVertexIndex;

    /* Handle recording of state blocks */
    if (This->isRecordingState) {
        TRACE("Recording... not performing anything\n");
        return D3D_OK;
    }

    if (oldIdxs)    IWineD3DIndexBuffer_Release(oldIdxs);
    if (pIndexData) IWineD3DIndexBuffer_AddRef(This->stateBlock->pIndexData);
    return D3D_OK;
}

HRESULT WINAPI IWineD3DDeviceImpl_GetIndices(IWineD3DDevice *iface, IWineD3DIndexBuffer** ppIndexData, UINT* pBaseVertexIndex) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    *ppIndexData = This->stateBlock->pIndexData;
    
    /* up ref count on ppindexdata */
    if (*ppIndexData) IWineD3DIndexBuffer_AddRef(*ppIndexData);
    *pBaseVertexIndex = This->stateBlock->baseVertexIndex;

    return D3D_OK;
}

/*****
 * Get / Set Viewports
 *****/
HRESULT WINAPI IWineD3DDeviceImpl_SetViewport(IWineD3DDevice *iface, CONST WINED3DVIEWPORT* pViewport) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    TRACE("(%p)\n", This);
    This->updateStateBlock->changed.viewport = TRUE;
    This->updateStateBlock->set.viewport = TRUE;
    memcpy(&This->updateStateBlock->viewport, pViewport, sizeof(WINED3DVIEWPORT));

    /* Handle recording of state blocks */
    if (This->isRecordingState) {
        TRACE("Recording... not performing anything\n");
        return D3D_OK;
    }

    ENTER_GL();

    TRACE("(%p) : x=%ld, y=%ld, wid=%ld, hei=%ld, minz=%f, maxz=%f\n", This,
          pViewport->X, pViewport->Y, pViewport->Width, pViewport->Height, pViewport->MinZ, pViewport->MaxZ);

    glDepthRange(pViewport->MinZ, pViewport->MaxZ);
    checkGLcall("glDepthRange");

    /* Note: GL requires lower left, DirectX supplies upper left */
    glViewport(pViewport->X, (This->renderTarget->currentDesc.Height - (pViewport->Y + pViewport->Height)), 
               pViewport->Width, pViewport->Height);
    checkGLcall("glViewport");

    LEAVE_GL();

    return D3D_OK;

}

HRESULT WINAPI IWineD3DDeviceImpl_GetViewport(IWineD3DDevice *iface, WINED3DVIEWPORT* pViewport) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p)\n", This);
    memcpy(pViewport, &This->stateBlock->viewport, sizeof(WINED3DVIEWPORT));
    return D3D_OK;
}

/*****
 * Get / Set Render States
 * TODO: Verify against dx9 definitions
 *****/
HRESULT WINAPI IWineD3DDeviceImpl_SetRenderState(IWineD3DDevice *iface, D3DRENDERSTATETYPE State, DWORD Value) {

    IWineD3DDeviceImpl  *This     = (IWineD3DDeviceImpl *)iface;
    DWORD                OldValue = This->stateBlock->renderState[State];
    
    /* Simple way of referring to either a DWORD or a 4 byte float */
    union {
        DWORD d;
        float f;
    } tmpvalue;
        
    TRACE("(%p)->state = %s(%d), value = %ld\n", This, debug_d3drenderstate(State), State, Value);
    This->updateStateBlock->changed.renderState[State] = TRUE;
    This->updateStateBlock->set.renderState[State] = TRUE;
    This->updateStateBlock->renderState[State] = Value;

    /* Handle recording of state blocks */
    if (This->isRecordingState) {
        TRACE("Recording... not performing anything\n");
        return D3D_OK;
    }

    ENTER_GL();

    switch (State) {
    case D3DRS_FILLMODE                  :
        switch ((D3DFILLMODE) Value) {
        case D3DFILL_POINT               : glPolygonMode(GL_FRONT_AND_BACK, GL_POINT); break;
        case D3DFILL_WIREFRAME           : glPolygonMode(GL_FRONT_AND_BACK, GL_LINE); break;
        case D3DFILL_SOLID               : glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); break;
        default:
            FIXME("Unrecognized D3DRS_FILLMODE value %ld\n", Value);
        }
        checkGLcall("glPolygonMode (fillmode)");
        break;

    case D3DRS_LIGHTING                  :
        if (Value) {
            glEnable(GL_LIGHTING);
            checkGLcall("glEnable GL_LIGHTING");
        } else {
            glDisable(GL_LIGHTING);
            checkGLcall("glDisable GL_LIGHTING");
        }
        break;

    case D3DRS_ZENABLE                   :
        switch ((D3DZBUFFERTYPE) Value) {
        case D3DZB_FALSE:
            glDisable(GL_DEPTH_TEST);
            checkGLcall("glDisable GL_DEPTH_TEST");
            break;
        case D3DZB_TRUE:
            glEnable(GL_DEPTH_TEST);
            checkGLcall("glEnable GL_DEPTH_TEST");
            break;
        case D3DZB_USEW:
            glEnable(GL_DEPTH_TEST);
            checkGLcall("glEnable GL_DEPTH_TEST");
            FIXME("W buffer is not well handled\n");
            break;
        default:
            FIXME("Unrecognized D3DZBUFFERTYPE value %ld\n", Value);
        }
        break;

    case D3DRS_CULLMODE                  :

        /* If we are culling "back faces with clockwise vertices" then
           set front faces to be counter clockwise and enable culling  
           of back faces                                               */
        switch ((D3DCULL) Value) {
        case D3DCULL_NONE:
            glDisable(GL_CULL_FACE);
            checkGLcall("glDisable GL_CULL_FACE");
            break;
        case D3DCULL_CW:
            glEnable(GL_CULL_FACE);
            checkGLcall("glEnable GL_CULL_FACE");
            if (This->renderUpsideDown) {
                glFrontFace(GL_CW);
                checkGLcall("glFrontFace GL_CW");
            } else {
                glFrontFace(GL_CCW);
                checkGLcall("glFrontFace GL_CCW");
            }
            glCullFace(GL_BACK);
            break;
        case D3DCULL_CCW:
            glEnable(GL_CULL_FACE);
            checkGLcall("glEnable GL_CULL_FACE");
            if (This->renderUpsideDown) {
                glFrontFace(GL_CCW); 
                checkGLcall("glFrontFace GL_CCW");
            } else {
                glFrontFace(GL_CW);
                checkGLcall("glFrontFace GL_CW");
            }
            glCullFace(GL_BACK);
            break;
        default:
            FIXME("Unrecognized/Unhandled D3DCULL value %ld\n", Value);
        }
        break;

    case D3DRS_SHADEMODE                 :
        switch ((D3DSHADEMODE) Value) {
        case D3DSHADE_FLAT:
            glShadeModel(GL_FLAT);
            checkGLcall("glShadeModel");
            break;
        case D3DSHADE_GOURAUD:
            glShadeModel(GL_SMOOTH);
            checkGLcall("glShadeModel");
            break;
        case D3DSHADE_PHONG:
            FIXME("D3DSHADE_PHONG isn't supported?\n");

            LEAVE_GL();
            return D3DERR_INVALIDCALL;
        default:
            FIXME("Unrecognized/Unhandled D3DSHADEMODE value %ld\n", Value);
        }
        break;

    case D3DRS_DITHERENABLE              :
        if (Value) {
            glEnable(GL_DITHER);
            checkGLcall("glEnable GL_DITHER");
        } else {
            glDisable(GL_DITHER);
            checkGLcall("glDisable GL_DITHER");
        }
        break;

    case D3DRS_ZWRITEENABLE              :
        if (Value) {
            glDepthMask(1);
            checkGLcall("glDepthMask");
        } else {
            glDepthMask(0);
            checkGLcall("glDepthMask");
        }
        break;

    case D3DRS_ZFUNC                     :
        {
            int glParm = GL_LESS;

            switch ((D3DCMPFUNC) Value) {
            case D3DCMP_NEVER:         glParm=GL_NEVER; break;
            case D3DCMP_LESS:          glParm=GL_LESS; break;
            case D3DCMP_EQUAL:         glParm=GL_EQUAL; break;
            case D3DCMP_LESSEQUAL:     glParm=GL_LEQUAL; break;
            case D3DCMP_GREATER:       glParm=GL_GREATER; break;
            case D3DCMP_NOTEQUAL:      glParm=GL_NOTEQUAL; break;
            case D3DCMP_GREATEREQUAL:  glParm=GL_GEQUAL; break;
            case D3DCMP_ALWAYS:        glParm=GL_ALWAYS; break;
            default:
                FIXME("Unrecognized/Unhandled D3DCMPFUNC value %ld\n", Value);
            }
            glDepthFunc(glParm);
            checkGLcall("glDepthFunc");
        }
        break;

    case D3DRS_AMBIENT                   :
        {
            float col[4];
            D3DCOLORTOGLFLOAT4(Value, col);
            TRACE("Setting ambient to (%f,%f,%f,%f)\n", col[0], col[1], col[2], col[3]);
            glLightModelfv(GL_LIGHT_MODEL_AMBIENT, col);
            checkGLcall("glLightModel for MODEL_AMBIENT");

        }
        break;

    case D3DRS_ALPHABLENDENABLE          :
        if (Value) {
            glEnable(GL_BLEND);
            checkGLcall("glEnable GL_BLEND");
        } else {
            glDisable(GL_BLEND);
            checkGLcall("glDisable GL_BLEND");
        };
        break;

    case D3DRS_SRCBLEND                  :
    case D3DRS_DESTBLEND                 :
        {
            int newVal = GL_ZERO;
            switch (Value) {
            case D3DBLEND_ZERO               : newVal = GL_ZERO;  break;
            case D3DBLEND_ONE                : newVal = GL_ONE;  break;
            case D3DBLEND_SRCCOLOR           : newVal = GL_SRC_COLOR;  break;
            case D3DBLEND_INVSRCCOLOR        : newVal = GL_ONE_MINUS_SRC_COLOR;  break;
            case D3DBLEND_SRCALPHA           : newVal = GL_SRC_ALPHA;  break;
            case D3DBLEND_INVSRCALPHA        : newVal = GL_ONE_MINUS_SRC_ALPHA;  break;
            case D3DBLEND_DESTALPHA          : newVal = GL_DST_ALPHA;  break;
            case D3DBLEND_INVDESTALPHA       : newVal = GL_ONE_MINUS_DST_ALPHA;  break;
            case D3DBLEND_DESTCOLOR          : newVal = GL_DST_COLOR;  break;
            case D3DBLEND_INVDESTCOLOR       : newVal = GL_ONE_MINUS_DST_COLOR;  break;
            case D3DBLEND_SRCALPHASAT        : newVal = GL_SRC_ALPHA_SATURATE;  break;

            case D3DBLEND_BOTHSRCALPHA       : newVal = GL_SRC_ALPHA;
                This->srcBlend = newVal;
                This->dstBlend = newVal;
                break;

            case D3DBLEND_BOTHINVSRCALPHA    : newVal = GL_ONE_MINUS_SRC_ALPHA;
                This->srcBlend = newVal;
                This->dstBlend = newVal;
                break;
            default:
                FIXME("Unrecognized src/dest blend value %ld (%d)\n", Value, State);
            }

            if (State == D3DRS_SRCBLEND) This->srcBlend = newVal;
            if (State == D3DRS_DESTBLEND) This->dstBlend = newVal;
            TRACE("glBlendFunc src=%x, dst=%x\n", This->srcBlend, This->dstBlend);
            glBlendFunc(This->srcBlend, This->dstBlend);

            checkGLcall("glBlendFunc");
        }
        break;

    case D3DRS_ALPHATESTENABLE           :
        if (Value) {
            glEnable(GL_ALPHA_TEST);
            checkGLcall("glEnable GL_ALPHA_TEST");
        } else {
            glDisable(GL_ALPHA_TEST);
            checkGLcall("glDisable GL_ALPHA_TEST");
        }
        break;

    case D3DRS_ALPHAFUNC                 :
        {
            int glParm = GL_LESS;
            float ref = ((float) This->stateBlock->renderState[D3DRS_ALPHAREF]) / 255.0f;

            switch ((D3DCMPFUNC) Value) {
            case D3DCMP_NEVER:         glParm = GL_NEVER; break;
            case D3DCMP_LESS:          glParm = GL_LESS; break;
            case D3DCMP_EQUAL:         glParm = GL_EQUAL; break;
            case D3DCMP_LESSEQUAL:     glParm = GL_LEQUAL; break;
            case D3DCMP_GREATER:       glParm = GL_GREATER; break;
            case D3DCMP_NOTEQUAL:      glParm = GL_NOTEQUAL; break;
            case D3DCMP_GREATEREQUAL:  glParm = GL_GEQUAL; break;
            case D3DCMP_ALWAYS:        glParm = GL_ALWAYS; break;
            default:
                FIXME("Unrecognized/Unhandled D3DCMPFUNC value %ld\n", Value);
            }
            TRACE("glAlphaFunc with Parm=%x, ref=%f\n", glParm, ref);
            glAlphaFunc(glParm, ref);
            This->alphafunc = glParm;
            checkGLcall("glAlphaFunc");
        }
        break;

    case D3DRS_ALPHAREF                  :
        {
            int glParm = This->alphafunc;
            float ref = 1.0f;

            ref = ((float) Value) / 255.0f;
            TRACE("glAlphaFunc with Parm=%x, ref=%f\n", glParm, ref);
            glAlphaFunc(glParm, ref);
            checkGLcall("glAlphaFunc");
        }
        break;

    case D3DRS_CLIPPLANEENABLE           :
    case D3DRS_CLIPPING                  :
        {
            /* Ensure we only do the changed clip planes */
            DWORD enable  = 0xFFFFFFFF;
            DWORD disable = 0x00000000;
            
            /* If enabling / disabling all */
            if (State == D3DRS_CLIPPING) {
                if (Value) {
                    enable  = This->stateBlock->renderState[D3DRS_CLIPPLANEENABLE];
                    disable = 0x00;
                } else {
                    disable = This->stateBlock->renderState[D3DRS_CLIPPLANEENABLE];
                    enable  = 0x00;
                }
            } else {
                enable =   Value & ~OldValue;
                disable = ~Value &  OldValue;
            }
            
            if (enable & D3DCLIPPLANE0)  { glEnable(GL_CLIP_PLANE0);  checkGLcall("glEnable(clip plane 0)"); }
            if (enable & D3DCLIPPLANE1)  { glEnable(GL_CLIP_PLANE1);  checkGLcall("glEnable(clip plane 1)"); }
            if (enable & D3DCLIPPLANE2)  { glEnable(GL_CLIP_PLANE2);  checkGLcall("glEnable(clip plane 2)"); }
            if (enable & D3DCLIPPLANE3)  { glEnable(GL_CLIP_PLANE3);  checkGLcall("glEnable(clip plane 3)"); }
            if (enable & D3DCLIPPLANE4)  { glEnable(GL_CLIP_PLANE4);  checkGLcall("glEnable(clip plane 4)"); }
            if (enable & D3DCLIPPLANE5)  { glEnable(GL_CLIP_PLANE5);  checkGLcall("glEnable(clip plane 5)"); }
            
            if (disable & D3DCLIPPLANE0) { glDisable(GL_CLIP_PLANE0); checkGLcall("glDisable(clip plane 0)"); }
            if (disable & D3DCLIPPLANE1) { glDisable(GL_CLIP_PLANE1); checkGLcall("glDisable(clip plane 1)"); }
            if (disable & D3DCLIPPLANE2) { glDisable(GL_CLIP_PLANE2); checkGLcall("glDisable(clip plane 2)"); }
            if (disable & D3DCLIPPLANE3) { glDisable(GL_CLIP_PLANE3); checkGLcall("glDisable(clip plane 3)"); }
            if (disable & D3DCLIPPLANE4) { glDisable(GL_CLIP_PLANE4); checkGLcall("glDisable(clip plane 4)"); }
            if (disable & D3DCLIPPLANE5) { glDisable(GL_CLIP_PLANE5); checkGLcall("glDisable(clip plane 5)"); }

            /** update clipping status */
            if (enable) {
              This->stateBlock->clip_status.ClipUnion = 0;
              This->stateBlock->clip_status.ClipIntersection = 0xFFFFFFFF;
            } else {
              This->stateBlock->clip_status.ClipUnion = 0;
              This->stateBlock->clip_status.ClipIntersection = 0;
            }
        }
        break;

    case D3DRS_BLENDOP                   :
        {
            int glParm = GL_FUNC_ADD;

            switch ((D3DBLENDOP) Value) {
            case D3DBLENDOP_ADD              : glParm = GL_FUNC_ADD;              break;
            case D3DBLENDOP_SUBTRACT         : glParm = GL_FUNC_SUBTRACT;         break;
            case D3DBLENDOP_REVSUBTRACT      : glParm = GL_FUNC_REVERSE_SUBTRACT; break;
            case D3DBLENDOP_MIN              : glParm = GL_MIN;                   break;
            case D3DBLENDOP_MAX              : glParm = GL_MAX;                   break;
            default:
                FIXME("Unrecognized/Unhandled D3DBLENDOP value %ld\n", Value);
            }
            TRACE("glBlendEquation(%x)\n", glParm);
            glBlendEquation(glParm);
            checkGLcall("glBlendEquation");
        }
        break;

    case D3DRS_TEXTUREFACTOR             :
        {
            unsigned int i;

            /* Note the texture color applies to all textures whereas 
               GL_TEXTURE_ENV_COLOR applies to active only */
            float col[4];
            D3DCOLORTOGLFLOAT4(Value, col);
            /* Set the default alpha blend color */
            glBlendColor(col[0], col[1], col[2], col[3]);
            checkGLcall("glBlendColor");

            /* And now the default texture color as well */
            for (i = 0; i < GL_LIMITS(textures); i++) {

                /* Note the D3DRS value applies to all textures, but GL has one
                   per texture, so apply it now ready to be used!               */
                if (GL_SUPPORT(ARB_MULTITEXTURE)) {
                    GLACTIVETEXTURE(i);
                } else if (i>0) {
                    FIXME("Program using multiple concurrent textures which this opengl implementation doesn't support\n");
                }

                glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, &col[0]);
                checkGLcall("glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, color);");
            }
        }
        break;

    case D3DRS_SPECULARENABLE            : 
        {
            /* Originally this used glLightModeli(GL_LIGHT_MODEL_COLOR_CONTROL,GL_SEPARATE_SPECULAR_COLOR)
               and (GL_LIGHT_MODEL_COLOR_CONTROL,GL_SINGLE_COLOR) to swap between enabled/disabled
               specular color. This is wrong:
               Separate specular color means the specular colour is maintained separately, whereas
               single color means it is merged in. However in both cases they are being used to
               some extent.
               To disable specular color, set it explicitly to black and turn off GL_COLOR_SUM_EXT
               NOTE: If not supported don't give FIXMEs the impact is really minimal and very few people are
                  running 1.4 yet!
             */
              if (Value) {
                glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, (float*) &This->updateStateBlock->material.Specular);
                checkGLcall("glMaterialfv");
                if (GL_SUPPORT(EXT_SECONDARY_COLOR)) {
                  glEnable(GL_COLOR_SUM_EXT);
                } else {
                  TRACE("Specular colors cannot be enabled in this version of opengl\n");
                }
                checkGLcall("glEnable(GL_COLOR_SUM)");
              } else {
                float black[4] = {0.0f, 0.0f, 0.0f, 0.0f};

                /* for the case of enabled lighting: */
                glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, &black[0]);
                checkGLcall("glMaterialfv");

                /* for the case of disabled lighting: */
                if (GL_SUPPORT(EXT_SECONDARY_COLOR)) {
                  glDisable(GL_COLOR_SUM_EXT);
                } else {
                  TRACE("Specular colors cannot be disabled in this version of opengl\n");
                }
                checkGLcall("glDisable(GL_COLOR_SUM)");
              }
        }
        break;

    case D3DRS_STENCILENABLE             :
        if (Value) {
            glEnable(GL_STENCIL_TEST);
            checkGLcall("glEnable GL_STENCIL_TEST");
        } else {
            glDisable(GL_STENCIL_TEST);
            checkGLcall("glDisable GL_STENCIL_TEST");
        }
        break;

    case D3DRS_STENCILFUNC               :
        {
           int glParm = GL_ALWAYS;
           int ref = This->stateBlock->renderState[D3DRS_STENCILREF];
           GLuint mask = This->stateBlock->renderState[D3DRS_STENCILMASK];

           switch ((D3DCMPFUNC) Value) {
           case D3DCMP_NEVER:         glParm=GL_NEVER; break;
           case D3DCMP_LESS:          glParm=GL_LESS; break;
           case D3DCMP_EQUAL:         glParm=GL_EQUAL; break;
           case D3DCMP_LESSEQUAL:     glParm=GL_LEQUAL; break;
           case D3DCMP_GREATER:       glParm=GL_GREATER; break;
           case D3DCMP_NOTEQUAL:      glParm=GL_NOTEQUAL; break;
           case D3DCMP_GREATEREQUAL:  glParm=GL_GEQUAL; break;
           case D3DCMP_ALWAYS:        glParm=GL_ALWAYS; break;
           default:
               FIXME("Unrecognized/Unhandled D3DCMPFUNC value %ld\n", Value);
           }
           TRACE("glStencilFunc with Parm=%x, ref=%d, mask=%x\n", glParm, ref, mask);
           This->stencilfunc = glParm;
           glStencilFunc(glParm, ref, mask);
           checkGLcall("glStencilFunc");
        }
        break;

    case D3DRS_STENCILREF                :
        {
           int glParm = This->stencilfunc;
           int ref = 0;
           GLuint mask = This->stateBlock->renderState[D3DRS_STENCILMASK];

           ref = Value;
           TRACE("glStencilFunc with Parm=%x, ref=%d, mask=%x\n", glParm, ref, mask);
           glStencilFunc(glParm, ref, mask);
           checkGLcall("glStencilFunc");
        }
        break;

    case D3DRS_STENCILMASK               :
        {
           int glParm = This->stencilfunc;
           int ref = This->stateBlock->renderState[D3DRS_STENCILREF];
           GLuint mask = Value;

           TRACE("glStencilFunc with Parm=%x, ref=%d, mask=%x\n", glParm, ref, mask);
           glStencilFunc(glParm, ref, mask);
           checkGLcall("glStencilFunc");
        }
        break;

    case D3DRS_STENCILFAIL               :
        {
            GLenum fail  ; 
            GLenum zpass ; 
            GLenum zfail ; 

            fail = StencilOp(Value);
            glGetIntegerv(GL_STENCIL_PASS_DEPTH_PASS, &zpass);
            checkGLcall("glGetIntegerv(GL_STENCIL_PASS_DEPTH_PASS, &zpass);");
            glGetIntegerv(GL_STENCIL_PASS_DEPTH_FAIL, &zfail);
            checkGLcall("glGetIntegerv(GL_STENCIL_PASS_DEPTH_FAIL, &zfail);");

            TRACE("StencilOp fail=%x, zfail=%x, zpass=%x\n", fail, zfail, zpass);
            glStencilOp(fail, zfail, zpass);
            checkGLcall("glStencilOp(fail, zfail, zpass);");
        }
        break;
    case D3DRS_STENCILZFAIL              :
        {
            GLenum fail  ; 
            GLenum zpass ; 
            GLenum zfail ; 

            glGetIntegerv(GL_STENCIL_FAIL, &fail);
            checkGLcall("glGetIntegerv(GL_STENCIL_FAIL, &fail);");
            glGetIntegerv(GL_STENCIL_PASS_DEPTH_PASS, &zpass);
            checkGLcall("glGetIntegerv(GL_STENCIL_PASS_DEPTH_PASS, &zpass);");
            zfail = StencilOp(Value);

            TRACE("StencilOp fail=%x, zfail=%x, zpass=%x\n", fail, zfail, zpass);
            glStencilOp(fail, zfail, zpass);
            checkGLcall("glStencilOp(fail, zfail, zpass);");
        }
        break;
    case D3DRS_STENCILPASS               :
        {
            GLenum fail  ; 
            GLenum zpass ; 
            GLenum zfail ; 

            glGetIntegerv(GL_STENCIL_FAIL, &fail);
            checkGLcall("glGetIntegerv(GL_STENCIL_FAIL, &fail);");
            zpass = StencilOp(Value);
            glGetIntegerv(GL_STENCIL_PASS_DEPTH_FAIL, &zfail);
            checkGLcall("glGetIntegerv(GL_STENCIL_PASS_DEPTH_FAIL, &zfail);");

            TRACE("StencilOp fail=%x, zfail=%x, zpass=%x\n", fail, zfail, zpass);
            glStencilOp(fail, zfail, zpass);
            checkGLcall("glStencilOp(fail, zfail, zpass);");
        }
        break;

    case D3DRS_STENCILWRITEMASK          :
        {
            glStencilMask(Value);
            TRACE("glStencilMask(%lu)\n", Value);
            checkGLcall("glStencilMask");
        }
        break;

    case D3DRS_FOGENABLE                 :
        {
          if (Value/* && This->stateBlock->renderState[D3DRS_FOGTABLEMODE] != D3DFOG_NONE*/) {
               glEnable(GL_FOG);
               checkGLcall("glEnable GL_FOG");
            } else {
               glDisable(GL_FOG);
               checkGLcall("glDisable GL_FOG");
            }
        }
        break;

    case D3DRS_RANGEFOGENABLE            :
        {
            if (Value) {
              TRACE("Enabled RANGEFOG");
            } else {
              TRACE("Disabled RANGEFOG");
            }
        }
        break;

    case D3DRS_FOGCOLOR                  :
        {
            float col[4];
            D3DCOLORTOGLFLOAT4(Value, col);
            /* Set the default alpha blend color */
            glFogfv(GL_FOG_COLOR, &col[0]);
            checkGLcall("glFog GL_FOG_COLOR");
        }
        break;

    case D3DRS_FOGTABLEMODE              :
        { 
          glHint(GL_FOG_HINT, GL_NICEST);
          switch (Value) {
          case D3DFOG_NONE:    /* I don't know what to do here */ checkGLcall("glFogi(GL_FOG_MODE, GL_EXP"); break; 
          case D3DFOG_EXP:     glFogi(GL_FOG_MODE, GL_EXP); checkGLcall("glFogi(GL_FOG_MODE, GL_EXP"); break; 
          case D3DFOG_EXP2:    glFogi(GL_FOG_MODE, GL_EXP2); checkGLcall("glFogi(GL_FOG_MODE, GL_EXP2"); break; 
          case D3DFOG_LINEAR:  glFogi(GL_FOG_MODE, GL_LINEAR); checkGLcall("glFogi(GL_FOG_MODE, GL_LINEAR"); break; 
          default:
            FIXME("Unsupported Value(%lu) for D3DRS_FOGTABLEMODE!\n", Value);
          }
          if (GL_SUPPORT(NV_FOG_DISTANCE)) {
            glFogi(GL_FOG_DISTANCE_MODE_NV, GL_EYE_PLANE_ABSOLUTE_NV);
          }
        }
        break;

    case D3DRS_FOGVERTEXMODE             :
        { 
          glHint(GL_FOG_HINT, GL_FASTEST);
          switch (Value) {
          case D3DFOG_NONE:    /* I don't know what to do here */ checkGLcall("glFogi(GL_FOG_MODE, GL_EXP"); break; 
          case D3DFOG_EXP:     glFogi(GL_FOG_MODE, GL_EXP); checkGLcall("glFogi(GL_FOG_MODE, GL_EXP"); break; 
          case D3DFOG_EXP2:    glFogi(GL_FOG_MODE, GL_EXP2); checkGLcall("glFogi(GL_FOG_MODE, GL_EXP2"); break; 
          case D3DFOG_LINEAR:  glFogi(GL_FOG_MODE, GL_LINEAR); checkGLcall("glFogi(GL_FOG_MODE, GL_LINEAR"); break; 
          default:
            FIXME("Unsupported Value(%lu) for D3DRS_FOGTABLEMODE!\n", Value);
          }
          if (GL_SUPPORT(NV_FOG_DISTANCE)) {
            glFogi(GL_FOG_DISTANCE_MODE_NV, This->stateBlock->renderState[D3DRS_RANGEFOGENABLE] ? GL_EYE_RADIAL_NV : GL_EYE_PLANE_ABSOLUTE_NV);
          }
        }
        break;

    case D3DRS_FOGSTART                  :
        {
            tmpvalue.d = Value;
            glFogfv(GL_FOG_START, &tmpvalue.f);
            checkGLcall("glFogf(GL_FOG_START, (float) Value)");
            TRACE("Fog Start == %f\n", tmpvalue.f);
        }
        break;

    case D3DRS_FOGEND                    :
        {
            tmpvalue.d = Value;
            glFogfv(GL_FOG_END, &tmpvalue.f);
            checkGLcall("glFogf(GL_FOG_END, (float) Value)");
            TRACE("Fog End == %f\n", tmpvalue.f);
        }
        break;

    case D3DRS_FOGDENSITY                :
        {
            tmpvalue.d = Value;
            glFogfv(GL_FOG_DENSITY, &tmpvalue.f);
            checkGLcall("glFogf(GL_FOG_DENSITY, (float) Value)");
        }
        break;

    case D3DRS_VERTEXBLEND               :
        {
          This->updateStateBlock->vertex_blend = (D3DVERTEXBLENDFLAGS) Value;
          TRACE("Vertex Blending state to %ld\n",  Value);
        }
        break;

    case D3DRS_TWEENFACTOR               :
        {
          tmpvalue.d = Value;
          This->updateStateBlock->tween_factor = tmpvalue.f;
          TRACE("Vertex Blending Tween Factor to %f\n", This->updateStateBlock->tween_factor);
        }
        break;

    case D3DRS_INDEXEDVERTEXBLENDENABLE  :
        {
          TRACE("Indexed Vertex Blend Enable to %ul\n", (BOOL) Value);
        }
        break;

    case D3DRS_COLORVERTEX               :
    case D3DRS_DIFFUSEMATERIALSOURCE     :
    case D3DRS_SPECULARMATERIALSOURCE    :
    case D3DRS_AMBIENTMATERIALSOURCE     :
    case D3DRS_EMISSIVEMATERIALSOURCE    :
        {
            GLenum Parm = GL_AMBIENT_AND_DIFFUSE;

            if (This->stateBlock->renderState[D3DRS_COLORVERTEX]) {
                TRACE("diff %ld, amb %ld, emis %ld, spec %ld\n",
                      This->stateBlock->renderState[D3DRS_DIFFUSEMATERIALSOURCE],
                      This->stateBlock->renderState[D3DRS_AMBIENTMATERIALSOURCE],
                      This->stateBlock->renderState[D3DRS_EMISSIVEMATERIALSOURCE],
                      This->stateBlock->renderState[D3DRS_SPECULARMATERIALSOURCE]);

                if (This->stateBlock->renderState[D3DRS_DIFFUSEMATERIALSOURCE] == D3DMCS_COLOR1) {
                    if (This->stateBlock->renderState[D3DRS_AMBIENTMATERIALSOURCE] == D3DMCS_COLOR1) {
                        Parm = GL_AMBIENT_AND_DIFFUSE;
                    } else {
                        Parm = GL_DIFFUSE;
                    }
                } else if (This->stateBlock->renderState[D3DRS_AMBIENTMATERIALSOURCE] == D3DMCS_COLOR1) {
                    Parm = GL_AMBIENT;
                } else if (This->stateBlock->renderState[D3DRS_EMISSIVEMATERIALSOURCE] == D3DMCS_COLOR1) {
                    Parm = GL_EMISSION;
                } else if (This->stateBlock->renderState[D3DRS_SPECULARMATERIALSOURCE] == D3DMCS_COLOR1) {
                    Parm = GL_SPECULAR;
                } else {
                    Parm = -1;
                }

                if (Parm == -1) {
                    if (This->tracking_color != DISABLED_TRACKING) This->tracking_color = NEEDS_DISABLE;
                } else {
                    This->tracking_color = NEEDS_TRACKING;
                    This->tracking_parm  = Parm;
                }

            } else {
                if (This->tracking_color != DISABLED_TRACKING) This->tracking_color = NEEDS_DISABLE;
            }
        }
        break; 

    case D3DRS_LINEPATTERN               :
        {
            union {
                DWORD                 d;
                D3DLINEPATTERN        lp;
            } tmppattern;
            tmppattern.d = Value;

            TRACE("Line pattern: repeat %d bits %x\n", tmppattern.lp.wRepeatFactor, tmppattern.lp.wLinePattern);

            if (tmppattern.lp.wRepeatFactor) {
                glLineStipple(tmppattern.lp.wRepeatFactor, tmppattern.lp.wLinePattern);
                checkGLcall("glLineStipple(repeat, linepattern)");
                glEnable(GL_LINE_STIPPLE);
                checkGLcall("glEnable(GL_LINE_STIPPLE);");
            } else {
                glDisable(GL_LINE_STIPPLE);
                checkGLcall("glDisable(GL_LINE_STIPPLE);");
            }
        }
        break;

    case D3DRS_ZBIAS                     :
        {
            if (Value) {
                tmpvalue.d = Value;
                TRACE("ZBias value %f\n", tmpvalue.f);
                glPolygonOffset(0, -tmpvalue.f);
                checkGLcall("glPolygonOffset(0, -Value)");
                glEnable(GL_POLYGON_OFFSET_FILL);
                checkGLcall("glEnable(GL_POLYGON_OFFSET_FILL);");
                glEnable(GL_POLYGON_OFFSET_LINE);
                checkGLcall("glEnable(GL_POLYGON_OFFSET_LINE);");
                glEnable(GL_POLYGON_OFFSET_POINT);
                checkGLcall("glEnable(GL_POLYGON_OFFSET_POINT);");
            } else {
                glDisable(GL_POLYGON_OFFSET_FILL);
                checkGLcall("glDisable(GL_POLYGON_OFFSET_FILL);");
                glDisable(GL_POLYGON_OFFSET_LINE);
                checkGLcall("glDisable(GL_POLYGON_OFFSET_LINE);");
                glDisable(GL_POLYGON_OFFSET_POINT);
                checkGLcall("glDisable(GL_POLYGON_OFFSET_POINT);");
            }
        }
        break;

    case D3DRS_NORMALIZENORMALS          :
        if (Value) {
            glEnable(GL_NORMALIZE);
            checkGLcall("glEnable(GL_NORMALIZE);");
        } else {
            glDisable(GL_NORMALIZE);
            checkGLcall("glDisable(GL_NORMALIZE);");
        }
        break;

    case D3DRS_POINTSIZE                 :
        tmpvalue.d = Value;
        TRACE("Set point size to %f\n", tmpvalue.f);
        glPointSize(tmpvalue.f);
        checkGLcall("glPointSize(...);");
        break;

    case D3DRS_POINTSIZE_MIN             :
        if (GL_SUPPORT(EXT_POINT_PARAMETERS)) {
          tmpvalue.d = Value;
          GL_EXTCALL(glPointParameterfEXT)(GL_POINT_SIZE_MIN_EXT, tmpvalue.f);
          checkGLcall("glPointParameterfEXT(...);");
        } else {
          FIXME("D3DRS_POINTSIZE_MIN not supported on this opengl\n");
        }
        break;

    case D3DRS_POINTSIZE_MAX             :
        if (GL_SUPPORT(EXT_POINT_PARAMETERS)) {
          tmpvalue.d = Value;
          GL_EXTCALL(glPointParameterfEXT)(GL_POINT_SIZE_MAX_EXT, tmpvalue.f);
          checkGLcall("glPointParameterfEXT(...);");
        } else {
          FIXME("D3DRS_POINTSIZE_MAX not supported on this opengl\n");
        }
        break;

    case D3DRS_POINTSCALE_A              :
    case D3DRS_POINTSCALE_B              :
    case D3DRS_POINTSCALE_C              :
    case D3DRS_POINTSCALEENABLE          :
        {
            /* If enabled, supply the parameters, otherwise fall back to defaults */
            if (This->stateBlock->renderState[D3DRS_POINTSCALEENABLE]) {
                GLfloat att[3] = {1.0f, 0.0f, 0.0f};
                att[0] = *((float*)&This->stateBlock->renderState[D3DRS_POINTSCALE_A]);
                att[1] = *((float*)&This->stateBlock->renderState[D3DRS_POINTSCALE_B]);
                att[2] = *((float*)&This->stateBlock->renderState[D3DRS_POINTSCALE_C]);

                if (GL_SUPPORT(EXT_POINT_PARAMETERS)) {
                  GL_EXTCALL(glPointParameterfvEXT)(GL_DISTANCE_ATTENUATION_EXT, att);
                  checkGLcall("glPointParameterfvEXT(GL_DISTANCE_ATTENUATION_EXT, ...);");
                } else {
                  TRACE("D3DRS_POINTSCALEENABLE not supported on this opengl\n");
                }
            } else {
                GLfloat att[3] = {1.0f, 0.0f, 0.0f};
                if (GL_SUPPORT(EXT_POINT_PARAMETERS)) {
                  GL_EXTCALL(glPointParameterfvEXT)(GL_DISTANCE_ATTENUATION_EXT, att);
                  checkGLcall("glPointParameterfvEXT(GL_DISTANCE_ATTENUATION_EXT, ...);");
                } else {
                  TRACE("D3DRS_POINTSCALEENABLE not supported, but not on either\n");
                }
            }
            break;
        }

    case D3DRS_COLORWRITEENABLE          :
      {
        TRACE("Color mask: r(%d) g(%d) b(%d) a(%d)\n", 
              Value & D3DCOLORWRITEENABLE_RED   ? 1 : 0,
              Value & D3DCOLORWRITEENABLE_GREEN ? 1 : 0,
              Value & D3DCOLORWRITEENABLE_BLUE  ? 1 : 0,
              Value & D3DCOLORWRITEENABLE_ALPHA ? 1 : 0); 
        glColorMask(Value & D3DCOLORWRITEENABLE_RED   ? GL_TRUE : GL_FALSE, 
                    Value & D3DCOLORWRITEENABLE_GREEN ? GL_TRUE : GL_FALSE,
                    Value & D3DCOLORWRITEENABLE_BLUE  ? GL_TRUE : GL_FALSE, 
                    Value & D3DCOLORWRITEENABLE_ALPHA ? GL_TRUE : GL_FALSE);
        checkGLcall("glColorMask(...)");
      }
      break;

    case D3DRS_LOCALVIEWER               :
      {
        GLint state = (Value) ? 1 : 0;
        TRACE("Local Viewer Enable to %ul\n", (BOOL) Value);        
        glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER, state);
      }
      break;

    case D3DRS_LASTPIXEL                 :
      {
        if (Value) {
          TRACE("Last Pixel Drawing Enabled\n");  
        } else {
          FIXME("Last Pixel Drawing Disabled, not handled yet\n");  
        }
      }
      break;

    case D3DRS_SOFTWAREVERTEXPROCESSING  :
      {
        if (Value) {
          TRACE("Software Processing Enabled\n");  
        } else {
          TRACE("Software Processing Disabled\n");  
        }
      }
      break;

      /** not supported */
    case D3DRS_ZVISIBLE                  :
      {
        LEAVE_GL();
        return D3DERR_INVALIDCALL;
      }

        /* Unhandled yet...! */
    case D3DRS_EDGEANTIALIAS             :
    case D3DRS_WRAP0                     :
    case D3DRS_WRAP1                     :
    case D3DRS_WRAP2                     :
    case D3DRS_WRAP3                     :
    case D3DRS_WRAP4                     :
    case D3DRS_WRAP5                     :
    case D3DRS_WRAP6                     :
    case D3DRS_WRAP7                     :
    case D3DRS_POINTSPRITEENABLE         :
    case D3DRS_MULTISAMPLEANTIALIAS      :
    case D3DRS_MULTISAMPLEMASK           :
    case D3DRS_PATCHEDGESTYLE            :
    case D3DRS_PATCHSEGMENTS             :
    case D3DRS_DEBUGMONITORTOKEN         :
    case D3DRS_POSITIONORDER             :
    case D3DRS_NORMALORDER               :
        /*Put back later: FIXME("(%p)->(%d,%ld) not handled yet\n", This, State, Value); */
        FIXME("(%p)->(%d,%ld) not handled yet\n", This, State, Value);
        break;
    default:
        FIXME("(%p)->(%d,%ld) unrecognized\n", This, State, Value);
    }

    LEAVE_GL();

    return D3D_OK;
}

HRESULT WINAPI IWineD3DDeviceImpl_GetRenderState(IWineD3DDevice *iface, D3DRENDERSTATETYPE State, DWORD *pValue) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p) for State %d = %ld\n", This, State, This->stateBlock->renderState[State]);
    *pValue = This->stateBlock->renderState[State];
    return D3D_OK;
}


/*****
 * Get / Set Texture Stage States
 * TODO: Verify against dx9 definitions
 *****/
HRESULT WINAPI IWineD3DDeviceImpl_SetTextureStageState(IWineD3DDevice *iface, DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    union {
        float f;
        DWORD d;
    } tmpvalue;

    /* FIXME: Handle 3d textures? What if TSS value set before set texture? Need to reapply all values? */
   
    TRACE("(%p) : Stage=%ld, Type=%s(%d), Value=%ld\n", This, Stage, debug_d3dtexturestate(Type), Type, Value);

    /* Reject invalid texture units */
    if (Stage >= GL_LIMITS(textures)) {
        TRACE("Attempt to access invalid texture rejected\n");
        return D3DERR_INVALIDCALL;
    }

    This->updateStateBlock->changed.textureState[Stage][Type] = TRUE;
    This->updateStateBlock->set.textureState[Stage][Type] = TRUE;
    This->updateStateBlock->textureState[Stage][Type] = Value;

    /* Handle recording of state blocks */
    if (This->isRecordingState) {
        TRACE("Recording... not performing anything\n");
        return D3D_OK;
    }

    ENTER_GL();

    /* Make appropriate texture active */
    VTRACE(("Activating appropriate texture state %ld\n", Stage));
    if (GL_SUPPORT(ARB_MULTITEXTURE)) {
        GLACTIVETEXTURE(Stage);
    } else if (Stage > 0) {
        FIXME("Program using multiple concurrent textures which this opengl implementation doesn't support\n");
    }

    switch (Type) {

    case D3DTSS_MINFILTER             :
    case D3DTSS_MIPFILTER             :
        {
            DWORD ValueMIN = This->stateBlock->textureState[Stage][D3DTSS_MINFILTER];
            DWORD ValueMIP = This->stateBlock->textureState[Stage][D3DTSS_MIPFILTER];
            GLint realVal = GL_LINEAR;

            if (ValueMIN == D3DTEXF_NONE) {
              /* Doesn't really make sense - Windows just seems to disable
                 mipmapping when this occurs                              */
              FIXME("Odd - minfilter of none, just disabling mipmaps\n");
              realVal = GL_LINEAR;
            } else if (ValueMIN == D3DTEXF_POINT) {
                /* GL_NEAREST_* */
              if (ValueMIP == D3DTEXF_NONE) {
                    realVal = GL_NEAREST;
                } else if (ValueMIP == D3DTEXF_POINT) {
                    realVal = GL_NEAREST_MIPMAP_NEAREST;
                } else if (ValueMIP == D3DTEXF_LINEAR) {
                    realVal = GL_NEAREST_MIPMAP_LINEAR;
                } else {
                    FIXME("Unhandled D3DTSS_MIPFILTER value of %ld\n", ValueMIP);
                    realVal = GL_NEAREST;
                }
            } else if (ValueMIN == D3DTEXF_LINEAR) {
                /* GL_LINEAR_* */
                if (ValueMIP == D3DTEXF_NONE) {
                    realVal = GL_LINEAR;
                } else if (ValueMIP == D3DTEXF_POINT) {
                    realVal = GL_LINEAR_MIPMAP_NEAREST;
                } else if (ValueMIP == D3DTEXF_LINEAR) {
                    realVal = GL_LINEAR_MIPMAP_LINEAR;
                } else {
                    FIXME("Unhandled D3DTSS_MIPFILTER value of %ld\n", ValueMIP);
                    realVal = GL_LINEAR;
                }
            } else if (ValueMIN == D3DTEXF_ANISOTROPIC) {
              if (GL_SUPPORT(EXT_TEXTURE_FILTER_ANISOTROPIC)) {
                if (ValueMIP == D3DTEXF_NONE) {
                  realVal = GL_LINEAR_MIPMAP_LINEAR;                  
                } else if (ValueMIP == D3DTEXF_POINT) {
                  realVal = GL_LINEAR_MIPMAP_NEAREST;
                } else if (ValueMIP == D3DTEXF_LINEAR) {
                    realVal = GL_LINEAR_MIPMAP_LINEAR;
                } else {
                  FIXME("Unhandled D3DTSS_MIPFILTER value of %ld\n", ValueMIP);
                  realVal = GL_LINEAR;
                }
              } else {
                WARN("Trying to use ANISOTROPIC_FILTERING for D3DTSS_MINFILTER. But not supported by OpenGL driver\n");
                realVal = GL_LINEAR;
              }
            } else {
                FIXME("Unhandled D3DTSS_MINFILTER value of %ld\n", ValueMIN);
                realVal = GL_LINEAR_MIPMAP_LINEAR;
            }

            TRACE("ValueMIN=%ld, ValueMIP=%ld, setting MINFILTER to %x\n", ValueMIN, ValueMIP, realVal);
            glTexParameteri(This->stateBlock->textureDimensions[Stage], GL_TEXTURE_MIN_FILTER, realVal);
            checkGLcall("glTexParameter GL_TEXTURE_MIN_FILTER, ...");
            /**
             * if we juste choose to use ANISOTROPIC filtering, refresh openGL state
             */
            if (GL_SUPPORT(EXT_TEXTURE_FILTER_ANISOTROPIC) && D3DTEXF_ANISOTROPIC == ValueMIN) {
              glTexParameteri(This->stateBlock->textureDimensions[Stage], 
                              GL_TEXTURE_MAX_ANISOTROPY_EXT, 
                              This->stateBlock->textureState[Stage][D3DTSS_MAXANISOTROPY]);
              checkGLcall("glTexParameter GL_TEXTURE_MAX_ANISOTROPY_EXT, ...");
            }
        }
        break;

    case D3DTSS_MAGFILTER             :
      {
        DWORD ValueMAG = This->stateBlock->textureState[Stage][D3DTSS_MAGFILTER];
        GLint realVal = GL_NEAREST;

        if (ValueMAG == D3DTEXF_POINT) {
          realVal = GL_NEAREST;
        } else if (ValueMAG == D3DTEXF_LINEAR) {
          realVal = GL_LINEAR;
        } else if (ValueMAG == D3DTEXF_ANISOTROPIC) {
          if (GL_SUPPORT(EXT_TEXTURE_FILTER_ANISOTROPIC)) {
            realVal = GL_LINEAR;
          } else {
            FIXME("Trying to use ANISOTROPIC_FILTERING for D3DTSS_MAGFILTER. But not supported by current OpenGL driver\n");
            realVal = GL_NEAREST;
          }
        } else {
          FIXME("Unhandled D3DTSS_MAGFILTER value of %ld\n", ValueMAG);
          realVal = GL_NEAREST;
        }
        TRACE("ValueMAG=%ld setting MAGFILTER to %x\n", ValueMAG, realVal);
        glTexParameteri(This->stateBlock->textureDimensions[Stage], GL_TEXTURE_MAG_FILTER, realVal);
        checkGLcall("glTexParameter GL_TEXTURE_MAG_FILTER, ...");
        /**
         * if we juste choose to use ANISOTROPIC filtering, refresh openGL state
         */
        if (GL_SUPPORT(EXT_TEXTURE_FILTER_ANISOTROPIC) && D3DTEXF_ANISOTROPIC == ValueMAG) {
          glTexParameteri(This->stateBlock->textureDimensions[Stage], 
                          GL_TEXTURE_MAX_ANISOTROPY_EXT, 
                          This->stateBlock->textureState[Stage][D3DTSS_MAXANISOTROPY]);
          checkGLcall("glTexParameter GL_TEXTURE_MAX_ANISOTROPY_EXT, ...");
        }
      }
      break;

    case D3DTSS_MAXMIPLEVEL           :
      {
        /**
         * Not really the same, but the more apprioprate than nothing
         */
        glTexParameteri(This->stateBlock->textureDimensions[Stage], 
                        GL_TEXTURE_BASE_LEVEL, 
                        This->stateBlock->textureState[Stage][D3DTSS_MAXMIPLEVEL]);
        checkGLcall("glTexParameteri GL_TEXTURE_BASE_LEVEL ...");
      }
      break;

    case D3DTSS_MAXANISOTROPY         :
      {        
        if (GL_SUPPORT(EXT_TEXTURE_FILTER_ANISOTROPIC)) {
          glTexParameteri(This->stateBlock->textureDimensions[Stage], 
                          GL_TEXTURE_MAX_ANISOTROPY_EXT, 
                          This->stateBlock->textureState[Stage][D3DTSS_MAXANISOTROPY]);
          checkGLcall("glTexParameteri GL_TEXTURE_MAX_ANISOTROPY_EXT ...");
        }
      }
      break;

    case D3DTSS_MIPMAPLODBIAS         :
      {        
        if (GL_SUPPORT(EXT_TEXTURE_LOD_BIAS)) {
          tmpvalue.d = Value;
          glTexEnvf(GL_TEXTURE_FILTER_CONTROL_EXT, 
                    GL_TEXTURE_LOD_BIAS_EXT,
                    tmpvalue.f);
          checkGLcall("glTexEnvi GL_TEXTURE_LOD_BIAS_EXT ...");
        }
      }
      break;

    case D3DTSS_ALPHAOP               :
    case D3DTSS_COLOROP               :
        {

            if ((Value == D3DTOP_DISABLE) && (Type == D3DTSS_COLOROP)) {
                /* TODO: Disable by making this and all later levels disabled */
                glDisable(GL_TEXTURE_1D);
                checkGLcall("Disable GL_TEXTURE_1D");
                glDisable(GL_TEXTURE_2D);
                checkGLcall("Disable GL_TEXTURE_2D");
                glDisable(GL_TEXTURE_3D);
                checkGLcall("Disable GL_TEXTURE_3D");
                break; /* Don't bother setting the texture operations */
            } else {
                /* Enable only the appropriate texture dimension */
                if (Type == D3DTSS_COLOROP) {
                    if (This->stateBlock->textureDimensions[Stage] == GL_TEXTURE_1D) {
                        glEnable(GL_TEXTURE_1D);
                        checkGLcall("Enable GL_TEXTURE_1D");
                    } else {
                        glDisable(GL_TEXTURE_1D);
                        checkGLcall("Disable GL_TEXTURE_1D");
                    } 
                    if (This->stateBlock->textureDimensions[Stage] == GL_TEXTURE_2D) {
                      if (GL_SUPPORT(NV_TEXTURE_SHADER) && This->texture_shader_active) {
                        glTexEnvi(GL_TEXTURE_SHADER_NV, GL_SHADER_OPERATION_NV, GL_TEXTURE_2D);
                        checkGLcall("Enable GL_TEXTURE_2D");
                      } else {
                        glEnable(GL_TEXTURE_2D);
                        checkGLcall("Enable GL_TEXTURE_2D");
                      }
                    } else {
                        glDisable(GL_TEXTURE_2D);
                        checkGLcall("Disable GL_TEXTURE_2D");
                    }
                    if (This->stateBlock->textureDimensions[Stage] == GL_TEXTURE_3D) {
                        glEnable(GL_TEXTURE_3D);
                        checkGLcall("Enable GL_TEXTURE_3D");
                    } else {
                        glDisable(GL_TEXTURE_3D);
                        checkGLcall("Disable GL_TEXTURE_3D");
                    }
                    if (This->stateBlock->textureDimensions[Stage] == GL_TEXTURE_CUBE_MAP_ARB) {
                        glEnable(GL_TEXTURE_CUBE_MAP_ARB);
                        checkGLcall("Enable GL_TEXTURE_CUBE_MAP");
                    } else {
                        glDisable(GL_TEXTURE_CUBE_MAP_ARB);
                        checkGLcall("Disable GL_TEXTURE_CUBE_MAP");
                    }
                }
            }
            /* Drop through... (Except disable case) */
        case D3DTSS_COLORARG0             :
        case D3DTSS_COLORARG1             :
        case D3DTSS_COLORARG2             :
        case D3DTSS_ALPHAARG0             :
        case D3DTSS_ALPHAARG1             :
        case D3DTSS_ALPHAARG2             :
            {
                BOOL isAlphaArg = (Type == D3DTSS_ALPHAOP || Type == D3DTSS_ALPHAARG1 || 
                                   Type == D3DTSS_ALPHAARG2 || Type == D3DTSS_ALPHAARG0);
                if (isAlphaArg) {
                    set_tex_op(iface, TRUE, Stage, This->stateBlock->textureState[Stage][D3DTSS_ALPHAOP],
                               This->stateBlock->textureState[Stage][D3DTSS_ALPHAARG1], 
                               This->stateBlock->textureState[Stage][D3DTSS_ALPHAARG2], 
                               This->stateBlock->textureState[Stage][D3DTSS_ALPHAARG0]);
                } else {
                    set_tex_op(iface, FALSE, Stage, This->stateBlock->textureState[Stage][D3DTSS_COLOROP],
                               This->stateBlock->textureState[Stage][D3DTSS_COLORARG1], 
                               This->stateBlock->textureState[Stage][D3DTSS_COLORARG2], 
                               This->stateBlock->textureState[Stage][D3DTSS_COLORARG0]);
                }
            }
            break;
        }

    case D3DTSS_ADDRESSU              :
    case D3DTSS_ADDRESSV              :
    case D3DTSS_ADDRESSW              :
        {
            GLint wrapParm = GL_REPEAT;

            switch (Value) {
            case D3DTADDRESS_WRAP:   wrapParm = GL_REPEAT; break;
            case D3DTADDRESS_CLAMP:  wrapParm = GL_CLAMP_TO_EDGE; break;      
            case D3DTADDRESS_BORDER: 
              {
                if (GL_SUPPORT(ARB_TEXTURE_BORDER_CLAMP)) {
                  wrapParm = GL_CLAMP_TO_BORDER_ARB; 
                } else {
                  /* FIXME: Not right, but better */
                  FIXME("Unrecognized or unsupported D3DTADDRESS_* value %ld, state %d\n", Value, Type);
                  wrapParm = GL_REPEAT; 
                }
              }
              break;
            case D3DTADDRESS_MIRROR: 
              {
                if (GL_SUPPORT(ARB_TEXTURE_MIRRORED_REPEAT)) {
                  wrapParm = GL_MIRRORED_REPEAT_ARB;
                } else {
                  /* Unsupported in OpenGL pre-1.4 */
                  FIXME("Unsupported D3DTADDRESS_MIRROR (needs GL_ARB_texture_mirrored_repeat) state %d\n", Type);
                  wrapParm = GL_REPEAT;
                }
              }
              break;
            case D3DTADDRESS_MIRRORONCE: 
              {
                if (GL_SUPPORT(ATI_TEXTURE_MIRROR_ONCE)) {
                  wrapParm = GL_MIRROR_CLAMP_TO_EDGE_ATI;
                } else {
                  FIXME("Unsupported D3DTADDRESS_MIRRORONCE (needs GL_ATI_texture_mirror_once) state %d\n", Type);
                  wrapParm = GL_REPEAT; 
                }
              }
              break;

            default:
                FIXME("Unrecognized or unsupported D3DTADDRESS_* value %ld, state %d\n", Value, Type);
                wrapParm = GL_REPEAT; 
            }

            switch (Type) {
            case D3DTSS_ADDRESSU:
                TRACE("Setting WRAP_S to %d for %x\n", wrapParm, This->stateBlock->textureDimensions[Stage]);
                glTexParameteri(This->stateBlock->textureDimensions[Stage], GL_TEXTURE_WRAP_S, wrapParm);
                checkGLcall("glTexParameteri(..., GL_TEXTURE_WRAP_S, wrapParm)");
                break;
            case D3DTSS_ADDRESSV:
                TRACE("Setting WRAP_T to %d for %x\n", wrapParm, This->stateBlock->textureDimensions[Stage]);
                glTexParameteri(This->stateBlock->textureDimensions[Stage], GL_TEXTURE_WRAP_T, wrapParm);
                checkGLcall("glTexParameteri(..., GL_TEXTURE_WRAP_T, wrapParm)");
                break;
            case D3DTSS_ADDRESSW:
                TRACE("Setting WRAP_R to %d for %x\n", wrapParm, This->stateBlock->textureDimensions[Stage]);
                glTexParameteri(This->stateBlock->textureDimensions[Stage], GL_TEXTURE_WRAP_R, wrapParm);
                checkGLcall("glTexParameteri(..., GL_TEXTURE_WRAP_R, wrapParm)");
                break;
            default: /* nop */
                      break; /** stupic compilator */
            }
        }
        break;

    case D3DTSS_BORDERCOLOR           :
        {
            float col[4];
            D3DCOLORTOGLFLOAT4(Value, col);
            TRACE("Setting border color for %x to %lx\n", This->stateBlock->textureDimensions[Stage], Value); 
            glTexParameterfv(This->stateBlock->textureDimensions[Stage], GL_TEXTURE_BORDER_COLOR, &col[0]);
            checkGLcall("glTexParameteri(..., GL_TEXTURE_BORDER_COLOR, ...)");
        }
        break;

    case D3DTSS_TEXCOORDINDEX         :
        {
            /* Values 0-7 are indexes into the FVF tex coords - See comments in DrawPrimitive */

            /* FIXME: From MSDN: The D3DTSS_TCI_* flags are mutually exclusive. If you include 
                  one flag, you can still specify an index value, which the system uses to 
                  determine the texture wrapping mode.  
                  eg. SetTextureStageState( 0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION | 1 );
                  means use the vertex position (camera-space) as the input texture coordinates 
                  for this texture stage, and the wrap mode set in the D3DRS_WRAP1 render 
                  state. We do not (yet) support the D3DRENDERSTATE_WRAPx values, nor tie them up
                  to the TEXCOORDINDEX value */
          
            /** 
             * Be careful the value of the mask 0xF0000 come from d3d8types.h infos 
             */
            switch (Value & 0xFFFF0000) {
            case D3DTSS_TCI_PASSTHRU:
              /*Use the specified texture coordinates contained within the vertex format. This value resolves to zero.*/
              glDisable(GL_TEXTURE_GEN_S);
              glDisable(GL_TEXTURE_GEN_T);
              glDisable(GL_TEXTURE_GEN_R);
              checkGLcall("glDisable(GL_TEXTURE_GEN_S,T,R)");
              break;

            case D3DTSS_TCI_CAMERASPACEPOSITION:
              /* CameraSpacePosition means use the vertex position, transformed to camera space, 
                 as the input texture coordinates for this stage's texture transformation. This 
                 equates roughly to EYE_LINEAR                                                  */
              {
                float s_plane[] = { 1.0, 0.0, 0.0, 0.0 };
                float t_plane[] = { 0.0, 1.0, 0.0, 0.0 };
                float r_plane[] = { 0.0, 0.0, 1.0, 0.0 };
                float q_plane[] = { 0.0, 0.0, 0.0, 1.0 };
                TRACE("D3DTSS_TCI_CAMERASPACEPOSITION - Set eye plane\n");

                glMatrixMode(GL_MODELVIEW);
                glPushMatrix();
                glLoadIdentity();
                glTexGenfv(GL_S, GL_EYE_PLANE, s_plane);
                glTexGenfv(GL_T, GL_EYE_PLANE, t_plane);
                glTexGenfv(GL_R, GL_EYE_PLANE, r_plane);
                glTexGenfv(GL_Q, GL_EYE_PLANE, q_plane);
                glPopMatrix();
                
                TRACE("D3DTSS_TCI_CAMERASPACEPOSITION - Set GL_TEXTURE_GEN_x and GL_x, GL_TEXTURE_GEN_MODE, GL_EYE_LINEAR\n");
                glEnable(GL_TEXTURE_GEN_S);
                checkGLcall("glEnable(GL_TEXTURE_GEN_S);");
                glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_EYE_LINEAR);
                checkGLcall("glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_EYE_LINEAR)");
                glEnable(GL_TEXTURE_GEN_T);
                checkGLcall("glEnable(GL_TEXTURE_GEN_T);");
                glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_EYE_LINEAR);
                checkGLcall("glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_EYE_LINEAR)");
                glEnable(GL_TEXTURE_GEN_R);
                checkGLcall("glEnable(GL_TEXTURE_GEN_R);");
                glTexGeni(GL_R, GL_TEXTURE_GEN_MODE, GL_EYE_LINEAR);
                checkGLcall("glTexGeni(GL_R, GL_TEXTURE_GEN_MODE, GL_EYE_LINEAR)");
              }
              break;

            case D3DTSS_TCI_CAMERASPACENORMAL:
              {
                if (GL_SUPPORT(NV_TEXGEN_REFLECTION)) {
                  float s_plane[] = { 1.0, 0.0, 0.0, 0.0 };
                  float t_plane[] = { 0.0, 1.0, 0.0, 0.0 };
                  float r_plane[] = { 0.0, 0.0, 1.0, 0.0 };
                  float q_plane[] = { 0.0, 0.0, 0.0, 1.0 };
                  TRACE("D3DTSS_TCI_CAMERASPACEPOSITION - Set eye plane\n");

                  glMatrixMode(GL_MODELVIEW);
                  glPushMatrix();
                  glLoadIdentity();
                  glTexGenfv(GL_S, GL_EYE_PLANE, s_plane);
                  glTexGenfv(GL_T, GL_EYE_PLANE, t_plane);
                  glTexGenfv(GL_R, GL_EYE_PLANE, r_plane);
                  glTexGenfv(GL_Q, GL_EYE_PLANE, q_plane);
                  glPopMatrix();
                  
                  glEnable(GL_TEXTURE_GEN_S);
                  checkGLcall("glEnable(GL_TEXTURE_GEN_S);");
                  glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_NORMAL_MAP_NV);
                  checkGLcall("glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_NORMAL_MAP_NV)");
                  glEnable(GL_TEXTURE_GEN_T);
                  checkGLcall("glEnable(GL_TEXTURE_GEN_T);");
                  glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_NORMAL_MAP_NV);
                  checkGLcall("glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_NORMAL_MAP_NV)");
                  glEnable(GL_TEXTURE_GEN_R);
                  checkGLcall("glEnable(GL_TEXTURE_GEN_R);");
                  glTexGeni(GL_R, GL_TEXTURE_GEN_MODE, GL_NORMAL_MAP_NV);
                  checkGLcall("glTexGeni(GL_R, GL_TEXTURE_GEN_MODE, GL_NORMAL_MAP_NV)");
                }
              }
              break;

            case D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR:
              {
                if (GL_SUPPORT(NV_TEXGEN_REFLECTION)) {
                  float s_plane[] = { 1.0, 0.0, 0.0, 0.0 };
                  float t_plane[] = { 0.0, 1.0, 0.0, 0.0 };
                  float r_plane[] = { 0.0, 0.0, 1.0, 0.0 };
                  float q_plane[] = { 0.0, 0.0, 0.0, 1.0 };
                  TRACE("D3DTSS_TCI_CAMERASPACEPOSITION - Set eye plane\n");
                  
                  glMatrixMode(GL_MODELVIEW);
                  glPushMatrix();
                  glLoadIdentity();
                  glTexGenfv(GL_S, GL_EYE_PLANE, s_plane);
                  glTexGenfv(GL_T, GL_EYE_PLANE, t_plane);
                  glTexGenfv(GL_R, GL_EYE_PLANE, r_plane);
                  glTexGenfv(GL_Q, GL_EYE_PLANE, q_plane);
                  glPopMatrix();
                  
                  glEnable(GL_TEXTURE_GEN_S);
                  checkGLcall("glEnable(GL_TEXTURE_GEN_S);");
                  glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_REFLECTION_MAP_NV);
                  checkGLcall("glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_REFLECTION_MAP_NV)");
                  glEnable(GL_TEXTURE_GEN_T);
                  checkGLcall("glEnable(GL_TEXTURE_GEN_T);");
                  glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_REFLECTION_MAP_NV);
                  checkGLcall("glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_REFLECTION_MAP_NV)");
                  glEnable(GL_TEXTURE_GEN_R);
                  checkGLcall("glEnable(GL_TEXTURE_GEN_R);");
                  glTexGeni(GL_R, GL_TEXTURE_GEN_MODE, GL_REFLECTION_MAP_NV);
                  checkGLcall("glTexGeni(GL_R, GL_TEXTURE_GEN_MODE, GL_REFLECTION_MAP_NV)");
                }
              }
              break;

            /* Unhandled types: */
            default:
                /* Todo: */
                /* ? disable GL_TEXTURE_GEN_n ? */ 
                glDisable(GL_TEXTURE_GEN_S);
                glDisable(GL_TEXTURE_GEN_T);
                glDisable(GL_TEXTURE_GEN_R);
                FIXME("Unhandled D3DTSS_TEXCOORDINDEX %lx\n", Value);
                break;
            }
        }
        break;

        /* Unhandled */
    case D3DTSS_TEXTURETRANSFORMFLAGS :
        set_texture_matrix((float *)&This->stateBlock->transforms[D3DTS_TEXTURE0 + Stage].u.m[0][0], Value);
        break; 

    case D3DTSS_BUMPENVMAT00          :
    case D3DTSS_BUMPENVMAT01          :
        TRACE("BUMPENVMAT0%u Stage=%ld, Type=%d, Value =%ld\n", Type - D3DTSS_BUMPENVMAT00, Stage, Type, Value);
        break;
    case D3DTSS_BUMPENVMAT10          :
    case D3DTSS_BUMPENVMAT11          :
        TRACE("BUMPENVMAT1%u Stage=%ld, Type=%d, Value =%ld\n", Type - D3DTSS_BUMPENVMAT10, Stage, Type, Value);
        break;

    case D3DTSS_BUMPENVLSCALE         :
      TRACE("BUMPENVLSCALE Stage=%ld, Type=%d, Value =%ld\n", Stage, Type, Value);
      break;

    case D3DTSS_BUMPENVLOFFSET        :
      TRACE("BUMPENVLOFFSET Stage=%ld, Type=%d, Value =%ld\n", Stage, Type, Value);
      break;

    case D3DTSS_RESULTARG             :
      TRACE("RESULTARG Still a stub, Stage=%ld, Type=%d, Value =%ld\n", Stage, Type, Value);
      break;

    default:
        /* Put back later: FIXME("(%p) : stub, Stage=%ld, Type=%d, Value =%ld\n", This, Stage, Type, Value); */
        TRACE("Still a stub, Stage=%ld, Type=%d, Value =%ld\n", Stage, Type, Value);
    }

    LEAVE_GL();

    return D3D_OK;
}

HRESULT WINAPI IWineD3DDeviceImpl_GetTextureStageState(IWineD3DDevice *iface, DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD* pValue) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p) : requesting Stage %ld, Type %d getting %ld\n", This, Stage, Type, This->updateStateBlock->textureState[Stage][Type]);
    *pValue = This->updateStateBlock->textureState[Stage][Type];
    return D3D_OK;
}

/*****
 * Get / Set Texture 
 *****/
HRESULT WINAPI IWineD3DDeviceImpl_SetTexture(IWineD3DDevice *iface, DWORD Stage, IWineD3DBaseTexture* pTexture) {

    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    IWineD3DBaseTexture   *oldTexture;
    BOOL                   reapplyStates = TRUE;
    DWORD                  reapplyFlags = 0;
    INT                    oldTextureDimensions = -1;
    D3DRESOURCETYPE        textureType;

    oldTexture = This->updateStateBlock->textures[Stage];
    TRACE("(%p) : Stage(%ld), Texture (%p)\n", This, Stage, pTexture);

    /* Reject invalid texture units */
    if (Stage >= GL_LIMITS(textures)) {
        TRACE("Attempt to access invalid texture rejected\n");
        return D3DERR_INVALIDCALL;
    }

    This->updateStateBlock->set.textures[Stage] = TRUE;
    This->updateStateBlock->changed.textures[Stage] = TRUE;
    This->updateStateBlock->textures[Stage] = pTexture;

    /* Handle recording of state blocks */
    if (This->isRecordingState) {
        TRACE("Recording... not performing anything\n");
        return D3D_OK;
    }

    oldTextureDimensions = This->updateStateBlock->textureDimensions[Stage];

    ENTER_GL();

    /* Make appropriate texture active */
    if (GL_SUPPORT(ARB_MULTITEXTURE)) {
        GLACTIVETEXTURE(Stage);

    } else if (Stage>0) {
        FIXME("Program using multiple concurrent textures which this opengl implementation doesn't support\n");
    }

    /* Decrement the count of the previous texture */
    if (NULL != oldTexture) {
        IWineD3DBaseTexture_Release(oldTexture);
    }

    if (NULL != pTexture) {
        IWineD3DBaseTexture_AddRef((IWineD3DBaseTexture *) This->updateStateBlock->textures[Stage]);

        /* Now setup the texture appropraitly */
        textureType = IWineD3DBaseTexture_GetType(pTexture);

        if (textureType == D3DRTYPE_TEXTURE) {

          if (oldTexture == pTexture && !IWineD3DBaseTexture_GetDirty(pTexture)) {
            TRACE("Skipping setting texture as old == new\n");
            reapplyStates = FALSE;

          } else {

            /* Standard 2D texture */
            TRACE("Standard 2d texture\n");
            This->updateStateBlock->textureDimensions[Stage] = GL_TEXTURE_2D;

            /* Load up the texture now */
            IWineD3DTexture_PreLoad((IWineD3DTexture *) pTexture);
          }

        } else if (textureType == D3DRTYPE_VOLUMETEXTURE) {

          if (oldTexture == pTexture && !IWineD3DBaseTexture_GetDirty(pTexture)) {
              TRACE("Skipping setting texture as old == new\n");
              reapplyStates = FALSE;

          } else {

              /* Standard 3D (volume) texture */
              TRACE("Standard 3d texture\n");
              This->updateStateBlock->textureDimensions[Stage] = GL_TEXTURE_3D;

              /* Load up the texture now */
              IWineD3DVolumeTexture_PreLoad((IWineD3DVolumeTexture *) pTexture);
          }

        } else if (textureType == D3DRTYPE_CUBETEXTURE) {

            if (oldTexture == pTexture && !IWineD3DBaseTexture_GetDirty(pTexture)) {
                TRACE("Skipping setting texture as old == new\n");
                reapplyStates = FALSE;

            } else {

                /* Standard Cube texture */
                TRACE("Standard Cube texture\n");
                This->updateStateBlock->textureDimensions[Stage] = GL_TEXTURE_CUBE_MAP_ARB;

                /* Load up the texture now */
                IWineD3DCubeTexture_PreLoad((IWineD3DCubeTexture *) pTexture);
            }

        } else {
            FIXME("(%p) : Incorrect type for a texture : (%d,%s)\n", This, textureType, debug_d3dresourcetype(textureType));
        }

    } else {

        TRACE("Setting to no texture (ie default texture)\n");
        This->updateStateBlock->textureDimensions[Stage] = GL_TEXTURE_1D;
        glBindTexture(GL_TEXTURE_1D, This->dummyTextureName[Stage]);
        checkGLcall("glBindTexture");
        TRACE("Bound dummy Texture to stage %ld (gl name %d)\n", Stage, This->dummyTextureName[Stage]);
    }

    /* Disable the old texture binding and enable the new one (unless operations are disabled) */
    if (oldTextureDimensions != This->updateStateBlock->textureDimensions[Stage]) {

       glDisable(oldTextureDimensions);
       checkGLcall("Disable oldTextureDimensions");

       if (This->stateBlock->textureState[Stage][D3DTSS_COLOROP] != D3DTOP_DISABLE) {
          glEnable(This->updateStateBlock->textureDimensions[Stage]);
          checkGLcall("glEnable new texture dimensions");
       }

       /* If Alpha arg1 is texture then handle the special case when there changes between a
          texture and no texture - See comments in set_tex_op                                  */
       if ((This->stateBlock->textureState[Stage][D3DTSS_ALPHAARG1] == D3DTA_TEXTURE) && 
           (((oldTexture == NULL) && (pTexture != NULL)) || ((pTexture == NULL) && (oldTexture != NULL))))
       {
           reapplyFlags |= REAPPLY_ALPHAOP;
       }
    }


    /* Even if the texture has been set to null, reapply the stages as a null texture to directx requires
       a dummy texture in opengl, and we always need to ensure the current view of the TextureStates apply */
    if (reapplyStates) {
       IWineD3DDeviceImpl_SetupTextureStates(iface, Stage, reapplyFlags);
    }

    LEAVE_GL();   
    TRACE("Texture now fully setup\n");

    return D3D_OK;
}

HRESULT WINAPI IWineD3DDeviceImpl_GetTexture(IWineD3DDevice *iface, DWORD Stage, IWineD3DBaseTexture** ppTexture) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p) : returning %p for stage %ld\n", This, This->updateStateBlock->textures[Stage], Stage);
    *ppTexture = (IWineD3DBaseTexture *) This->updateStateBlock->textures[Stage];
    if (*ppTexture)
        IWineD3DBaseTexture_AddRef(*ppTexture);
    return D3D_OK;
}

/*****
 * Get Back Buffer
 *****/
HRESULT WINAPI IWineD3DDeviceImpl_GetBackBuffer(IWineD3DDevice *iface, UINT iSwapChain, UINT BackBuffer, D3DBACKBUFFER_TYPE Type, 
                                                IWineD3DSurface** ppBackBuffer) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    
    *ppBackBuffer = (IWineD3DSurface *) This->backBuffer;
    TRACE("(%p) : BackBuf %d Type %d SwapChain %d returning %p\n", This, BackBuffer, Type, iSwapChain, *ppBackBuffer);

    if (BackBuffer > This->presentParms.BackBufferCount - 1) {
        FIXME("Only one backBuffer currently supported\n");
        return D3DERR_INVALIDCALL;
    }

    /* Note inc ref on returned surface */
    IWineD3DSurface_AddRef(*ppBackBuffer);

    return D3D_OK;
}

HRESULT WINAPI IWineD3DDeviceImpl_GetDeviceCaps(IWineD3DDevice *iface, D3DCAPS9* pCaps) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    WARN("(%p) : stub, calling idirect3d for now\n", This);
    IWineD3D_GetDeviceCaps(This->wineD3D, This->adapterNo, This->devType, pCaps);
    return D3D_OK;
}

HRESULT WINAPI IWineD3DDeviceImpl_GetDisplayMode(IWineD3DDevice *iface, UINT iSwapChain, D3DDISPLAYMODE* pMode) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    HDC                 hdc;
    int                 bpp = 0;

    pMode->Width        = GetSystemMetrics(SM_CXSCREEN);
    pMode->Height       = GetSystemMetrics(SM_CYSCREEN);
    pMode->RefreshRate  = 85; /*FIXME: How to identify? */

    hdc = CreateDCA("DISPLAY", NULL, NULL, NULL);
    bpp = GetDeviceCaps(hdc, BITSPIXEL);
    DeleteDC(hdc);

    switch (bpp) {
    case  8: pMode->Format       = D3DFMT_R8G8B8; break;
    case 16: pMode->Format       = D3DFMT_R5G6B5; break;
    case 24: /*pMode->Format       = D3DFMT_R8G8B8; break; */
    case 32: pMode->Format       = D3DFMT_A8R8G8B8; break;
    default: 
       FIXME("Unrecognized display mode format\n");
       pMode->Format       = D3DFMT_UNKNOWN;
    }

    FIXME("(%p) : returning w(%d) h(%d) rr(%d) fmt(%u,%s)\n", This, pMode->Width, pMode->Height, pMode->RefreshRate, 
          pMode->Format, debug_d3dformat(pMode->Format));
    return D3D_OK;
}
 
/*****
 * Scene related functions
 *****/
HRESULT WINAPI IWineD3DDeviceImpl_BeginScene(IWineD3DDevice *iface) {
    /* At the moment we have no need for any functionality at the beginning
       of a scene                                                          */
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p) : stub\n", This);
    return D3D_OK;
}

HRESULT WINAPI IWineD3DDeviceImpl_EndScene(IWineD3DDevice *iface) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p)\n", This);

    ENTER_GL();

    glFlush();
    checkGLcall("glFlush");

    if ((This->frontBuffer != This->renderTarget) && (This->backBuffer != This->renderTarget)) {

        /* If we are rendering to a texture (surface) then flag it as dirty.
           A surfaces container is either the appropriate texture or the device itself
              depending on how the surface was created.                                */
        if (This->renderTarget != NULL && ((IWineD3DDeviceImpl *)This->renderTarget->container != This)) {

            IWineD3DBaseTexture *cont = (IWineD3DBaseTexture *)This->renderTarget->container;
            /** always dirtify for now. we must find a better way to see that surface have been modified */
            This->renderTarget->inPBuffer = TRUE;
            This->renderTarget->inTexture = FALSE;
            IWineD3DBaseTexture_SetDirty(cont, TRUE);
            IWineD3DBaseTexture_PreLoad(cont);
            This->renderTarget->inPBuffer = FALSE;
        }
    }

    LEAVE_GL();
    return D3D_OK;
}

HRESULT WINAPI IWineD3DDeviceImpl_Present(IWineD3DDevice *iface, 
                                          CONST RECT* pSourceRect, CONST RECT* pDestRect, 
                                          HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p) Presenting the frame\n", This);

    ENTER_GL();

    if (pSourceRect || pDestRect) FIXME("Unhandled present options %p/%p\n", pSourceRect, pDestRect);

    glXSwapBuffers(This->display, This->drawable);
    /* Don't call checkGLcall, as glGetError is not applicable here */
    
    TRACE("glXSwapBuffers called, Starting new frame\n");

    /* FPS support */
    if (TRACE_ON(d3d_fps))
    {
        static long prev_time, frames;

        DWORD time = GetTickCount();
        frames++;
        /* every 1.5 seconds */
        if (time - prev_time > 1500) {
            TRACE_(d3d_fps)("@ approx %.2ffps\n", 1000.0*frames/(time - prev_time));
            prev_time = time;
            frames = 0;
        }
    }

#if defined(FRAME_DEBUGGING)
{
    if (GetFileAttributesA("C:\\D3DTRACE") != INVALID_FILE_ATTRIBUTES) {
        if (!isOn) {
            isOn = TRUE;
            FIXME("Enabling D3D Trace\n");
            __WINE_SET_DEBUGGING(__WINE_DBCL_TRACE, __wine_dbch_d3d, 1);
#if defined(SHOW_FRAME_MAKEUP)
            FIXME("Singe Frame snapshots Starting\n");
            isDumpingFrames = TRUE;
            glClear(GL_COLOR_BUFFER_BIT);
#endif

#if defined(SINGLE_FRAME_DEBUGGING)
        } else {
#if defined(SHOW_FRAME_MAKEUP)
            FIXME("Singe Frame snapshots Finishing\n");
            isDumpingFrames = FALSE;
#endif
            FIXME("Singe Frame trace complete\n");
            DeleteFileA("C:\\D3DTRACE");
            __WINE_SET_DEBUGGING(__WINE_DBCL_TRACE, __wine_dbch_d3d, 0);
#endif
        }
    } else {
        if (isOn) {
            isOn = FALSE;
#if defined(SHOW_FRAME_MAKEUP)
            FIXME("Singe Frame snapshots Finishing\n");
            isDumpingFrames = FALSE;
#endif
            FIXME("Disabling D3D Trace\n");
            __WINE_SET_DEBUGGING(__WINE_DBCL_TRACE, __wine_dbch_d3d, 0);
        }
    }
}
#endif

    LEAVE_GL();
    /* Although this is not strictly required, a simple demo showed this does occur
       on (at least non-debug) d3d                                                  */
    if (This->presentParms.SwapEffect == D3DSWAPEFFECT_DISCARD) {
       IWineD3DDevice_Clear(iface, 0, NULL, D3DCLEAR_STENCIL|D3DCLEAR_ZBUFFER|D3DCLEAR_TARGET, 0x00, 1.0, 0);
    }

    return D3D_OK;
}

HRESULT WINAPI IWineD3DDeviceImpl_Clear(IWineD3DDevice *iface, DWORD Count, CONST D3DRECT* pRects, 
                                        DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    /* TODO: From MSDN This method fails if you specify the D3DCLEAR_ZBUFFER or D3DCLEAR_STENCIL flags when the
      render target does not have an attached depth buffer. Similarly, if you specify the D3DCLEAR_STENCIL flag
      when the depth-buffer format does not contain stencil buffer information, this method fails. */
    GLbitfield     glMask = 0;
    GLboolean      old_ztest;
    GLfloat        old_z_clear_value;
    GLint          old_stencil_clear_value;
    GLfloat        old_color_clear_value[4];
    unsigned int   i;
    CONST D3DRECT* curRect;

    TRACE("(%p) Count (%ld), pRects (%p), Flags (%lx), Z (%f), Stencil (%ld)\n", This,
          Count, pRects, Flags, Z, Stencil);

    ENTER_GL();

    glEnable(GL_SCISSOR_TEST);
    checkGLcall("glEnable GL_SCISSOR_TEST");

    if (Count > 0 && pRects) {
        curRect = pRects;
    } else {
        curRect = NULL;
    }

    /* Only set the values up once, as they are not changing */
    if (Flags & D3DCLEAR_STENCIL) {        
        glGetIntegerv(GL_STENCIL_CLEAR_VALUE, &old_stencil_clear_value);
        glClearStencil(Stencil);
        checkGLcall("glClearStencil");
        glMask = glMask | GL_STENCIL_BUFFER_BIT;
        glStencilMask(0xFFFFFFFF);
    }

    if (Flags & D3DCLEAR_ZBUFFER) {
        glGetBooleanv(GL_DEPTH_WRITEMASK, &old_ztest);
        glDepthMask(GL_TRUE); 
        glGetFloatv(GL_DEPTH_CLEAR_VALUE, &old_z_clear_value);
        glClearDepth(Z);
        checkGLcall("glClearDepth");
        glMask = glMask | GL_DEPTH_BUFFER_BIT;
    }

    if (Flags & D3DCLEAR_TARGET) {
        TRACE("Clearing screen with glClear to color %lx\n", Color);
        glGetFloatv(GL_COLOR_CLEAR_VALUE, old_color_clear_value);
        glClearColor(((Color >> 16) & 0xFF) / 255.0f, 
                     ((Color >>  8) & 0xFF) / 255.0f,
                     ((Color >>  0) & 0xFF) / 255.0f, 
                     ((Color >> 24) & 0xFF) / 255.0f);
        checkGLcall("glClearColor");

        /* Clear ALL colors! */
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glMask = glMask | GL_COLOR_BUFFER_BIT;
    }

    /* Now process each rect in turn */
    for (i = 0; i < Count || i == 0; i++) {

        if (curRect) {
            /* Note gl uses lower left, width/height */
            TRACE("(%p) %p Rect=(%ld,%ld)->(%ld,%ld) glRect=(%ld,%ld), len=%ld, hei=%ld\n", This, curRect,
                  curRect->x1, curRect->y1, curRect->x2, curRect->y2,
                  curRect->x1, (This->renderTarget->currentDesc.Height - curRect->y2), 
                  curRect->x2 - curRect->x1, curRect->y2 - curRect->y1);
            glScissor(curRect->x1, (This->renderTarget->currentDesc.Height - curRect->y2), 
                      curRect->x2 - curRect->x1, curRect->y2 - curRect->y1);
            checkGLcall("glScissor");
        } else {
            glScissor(This->stateBlock->viewport.X, 
                      (This->renderTarget->currentDesc.Height - (This->stateBlock->viewport.Y + This->stateBlock->viewport.Height)), 
                      This->stateBlock->viewport.Width, 
                      This->stateBlock->viewport.Height);
            checkGLcall("glScissor");
        }

        /* Clear the selected rectangle (or full screen) */
        glClear(glMask);
        checkGLcall("glClear");

        /* Step to the next rectangle */
        if (curRect) curRect = curRect + sizeof(D3DRECT);
    }

    /* Restore the old values (why..?) */
    if (Flags & D3DCLEAR_STENCIL) {
        glClearStencil(old_stencil_clear_value);
        glStencilMask(This->stateBlock->renderState[D3DRS_STENCILWRITEMASK]);
    }    
    if (Flags & D3DCLEAR_ZBUFFER) {
        glDepthMask(old_ztest);
        glClearDepth(old_z_clear_value);
    }
    if (Flags & D3DCLEAR_TARGET) {
        glClearColor(old_color_clear_value[0], 
                     old_color_clear_value[1],
                     old_color_clear_value[2], 
                     old_color_clear_value[3]);
        glColorMask(This->stateBlock->renderState[D3DRS_COLORWRITEENABLE] & D3DCOLORWRITEENABLE_RED ? GL_TRUE : GL_FALSE, 
                    This->stateBlock->renderState[D3DRS_COLORWRITEENABLE] & D3DCOLORWRITEENABLE_GREEN ? GL_TRUE : GL_FALSE,
                    This->stateBlock->renderState[D3DRS_COLORWRITEENABLE] & D3DCOLORWRITEENABLE_BLUE  ? GL_TRUE : GL_FALSE, 
                    This->stateBlock->renderState[D3DRS_COLORWRITEENABLE] & D3DCOLORWRITEENABLE_ALPHA ? GL_TRUE : GL_FALSE);
    }

    glDisable(GL_SCISSOR_TEST);
    checkGLcall("glDisable");
    LEAVE_GL();

    return D3D_OK;
}

/*****
 * Drawing functions
 *****/
HRESULT WINAPI IWineD3DDeviceImpl_DrawPrimitive(IWineD3DDevice *iface, D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, 
                                                UINT PrimitiveCount) {

    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    This->stateBlock->streamIsUP = FALSE;

    TRACE("(%p) : Type=(%d,%s), Start=%d, Count=%d\n", This, PrimitiveType, 
                               debug_d3dprimitivetype(PrimitiveType), 
                               StartVertex, PrimitiveCount);
    drawPrimitive(iface, PrimitiveType, PrimitiveCount, StartVertex, -1, 0, NULL, 0);

    return D3D_OK;
}

/* TODO: baseVIndex needs to be provided from This->stateBlock->baseVertexIndex when called from d3d8 */
HRESULT  WINAPI  IWineD3DDeviceImpl_DrawIndexedPrimitive(IWineD3DDevice *iface, 
                                                           D3DPRIMITIVETYPE PrimitiveType,
                                                           INT baseVIndex, UINT minIndex,
                                                           UINT NumVertices,UINT startIndex,UINT primCount) {

    IWineD3DDeviceImpl  *This = (IWineD3DDeviceImpl *)iface;
    UINT                 idxStride = 2;
    IWineD3DIndexBuffer *pIB;
    D3DINDEXBUFFER_DESC  IdxBufDsc;
    
    pIB = This->stateBlock->pIndexData;
    This->stateBlock->streamIsUP = FALSE;

    TRACE("(%p) : Type=(%d,%s), min=%d, CountV=%d, startIdx=%d, baseVidx=%d, countP=%d \n", This, 
          PrimitiveType, debug_d3dprimitivetype(PrimitiveType),
          minIndex, NumVertices, startIndex, baseVIndex, primCount);

    IWineD3DIndexBuffer_GetDesc(pIB, &IdxBufDsc);
    if (IdxBufDsc.Format == D3DFMT_INDEX16) {
        idxStride = 2;
    } else {
        idxStride = 4;
    }

    drawPrimitive(iface, PrimitiveType, primCount, baseVIndex, 
                      startIndex, idxStride, 
                      ((IWineD3DIndexBufferImpl *) pIB)->allocatedMemory,
                      minIndex);

    return D3D_OK;
}

HRESULT WINAPI IWineD3DDeviceImpl_DrawPrimitiveUP(IWineD3DDevice *iface, D3DPRIMITIVETYPE PrimitiveType,
                                                    UINT PrimitiveCount, CONST void* pVertexStreamZeroData,
                                                    UINT VertexStreamZeroStride) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    TRACE("(%p) : Type=(%d,%s), pCount=%d, pVtxData=%p, Stride=%d\n", This, PrimitiveType, 
             debug_d3dprimitivetype(PrimitiveType), 
             PrimitiveCount, pVertexStreamZeroData, VertexStreamZeroStride);

    if (This->stateBlock->stream_source[0] != NULL) IWineD3DVertexBuffer_Release(This->stateBlock->stream_source[0]);

    /* Note in the following, it's not this type, but that's the purpose of streamIsUP */
    This->stateBlock->stream_source[0] = (IWineD3DVertexBuffer *)pVertexStreamZeroData; 
    This->stateBlock->stream_stride[0] = VertexStreamZeroStride;
    This->stateBlock->streamIsUP = TRUE;
    drawPrimitive(iface, PrimitiveType, PrimitiveCount, 0, 0, 0, NULL, 0);
    This->stateBlock->stream_stride[0] = 0;
    This->stateBlock->stream_source[0] = NULL;

    /*stream zero settings set to null at end, as per the msdn */
    return D3D_OK;
}

HRESULT WINAPI IWineD3DDeviceImpl_DrawIndexedPrimitiveUP(IWineD3DDevice *iface, D3DPRIMITIVETYPE PrimitiveType,
                                                             UINT MinVertexIndex,
                                                             UINT NumVertexIndices,UINT PrimitiveCount,CONST void* pIndexData,
                                                             D3DFORMAT IndexDataFormat,CONST void* pVertexStreamZeroData,
                                                             UINT VertexStreamZeroStride) {
    int                 idxStride;
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    TRACE("(%p) : Type=(%d,%s), MinVtxIdx=%d, NumVIdx=%d, PCount=%d, pidxdata=%p, IdxFmt=%d, pVtxdata=%p, stride=%d\n", 
             This, PrimitiveType, debug_d3dprimitivetype(PrimitiveType),
             MinVertexIndex, NumVertexIndices, PrimitiveCount, pIndexData,  
             IndexDataFormat, pVertexStreamZeroData, VertexStreamZeroStride);

    if (This->stateBlock->stream_source[0] != NULL) IWineD3DVertexBuffer_Release(This->stateBlock->stream_source[0]);

    if (IndexDataFormat == D3DFMT_INDEX16) {
        idxStride = 2;
    } else {
        idxStride = 4;
    }

    /* Note in the following, it's not this type, but that's the purpose of streamIsUP */
    This->stateBlock->stream_source[0] = (IWineD3DVertexBuffer *)pVertexStreamZeroData;
    This->stateBlock->streamIsUP = TRUE;
    This->stateBlock->stream_stride[0] = VertexStreamZeroStride;

    drawPrimitive(iface, PrimitiveType, PrimitiveCount, 0, 0, idxStride, pIndexData, MinVertexIndex);

    /* stream zero settings set to null at end as per the msdn */
    This->stateBlock->stream_source[0] = NULL;
    This->stateBlock->stream_stride[0] = 0;
    IWineD3DDevice_SetIndices(iface, NULL, 0);

    return D3D_OK;
}

/*****
 * Vertex Declaration
 *****/
extern HRESULT IWineD3DVertexDeclarationImpl_ParseDeclaration8(IWineD3DDeviceImpl* This, const DWORD* pDecl, IWineD3DVertexDeclarationImpl* object);
extern HRESULT IWineD3DVertexDeclarationImpl_ParseDeclaration9(IWineD3DDeviceImpl* This, const D3DVERTEXELEMENT9* pDecl, IWineD3DVertexDeclarationImpl* object);

HRESULT WINAPI IWineD3DDeviceImpl_CreateVertexDeclaration(IWineD3DDevice* iface, UINT iDeclVersion, CONST VOID* pDeclaration, IWineD3DVertexDeclaration** ppDecl) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    IWineD3DVertexDeclarationImpl* object = NULL;
    HRESULT hr = D3D_OK;

    TRACE("(%p) : iDeclVersion=%u, pFunction=%p, ppDecl=%p\n", This, iDeclVersion, pDeclaration, ppDecl);


    object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(IWineD3DVertexDeclarationImpl));
    
    object->lpVtbl = &IWineD3DVertexDeclaration_Vtbl;
    object->wineD3DDevice = This;
    object->ref = 1;
    object->allFVF = 0;

    *ppDecl = (IWineD3DVertexDeclaration*) object;

    if (8 == iDeclVersion) {
      /** @TODO */
      hr = IWineD3DVertexDeclarationImpl_ParseDeclaration8(This, (const DWORD*) pDeclaration, object);
    } else {
      hr = IWineD3DVertexDeclarationImpl_ParseDeclaration9(This, (const D3DVERTEXELEMENT9*) pDeclaration, object);
    }

    return hr;
}

HRESULT WINAPI IWineD3DDeviceImpl_SetVertexDeclaration(IWineD3DDevice* iface, IWineD3DVertexDeclaration* pDecl) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *) iface;   
    
    TRACE("(%p) : pDecl=%p\n", This, pDecl);

    IWineD3DVertexDeclaration_AddRef(pDecl);
    if (NULL != This->updateStateBlock->vertexDecl) {
      IWineD3DVertexDeclaration_Release(This->updateStateBlock->vertexDecl);
    }
    This->updateStateBlock->vertexDecl = pDecl;
    This->updateStateBlock->changed.vertexDecl = TRUE;
    This->updateStateBlock->set.vertexDecl = TRUE;
    return D3D_OK;
}
HRESULT WINAPI IWineD3DDeviceImpl_GetVertexDeclaration(IWineD3DDevice* iface, IWineD3DVertexDeclaration** ppDecl) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    TRACE("(%p) : ppDecl=%p\n", This, ppDecl);
    
    *ppDecl = This->updateStateBlock->vertexDecl;
    if (NULL != *ppDecl) IWineD3DVertexDeclaration_AddRef(*ppDecl);
    return D3D_OK;
}

/**********************************************************
 * IUnknown parts follows
 **********************************************************/

HRESULT WINAPI IWineD3DDeviceImpl_QueryInterface(IWineD3DDevice *iface,REFIID riid,LPVOID *ppobj)
{
    return E_NOINTERFACE;
}

ULONG WINAPI IWineD3DDeviceImpl_AddRef(IWineD3DDevice *iface) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    ULONG refCount = InterlockedIncrement(&This->ref);

    TRACE("(%p) : AddRef increasing from %ld\n", This, refCount - 1);
    return refCount;
}

ULONG WINAPI IWineD3DDeviceImpl_Release(IWineD3DDevice *iface) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    ULONG refCount = InterlockedDecrement(&This->ref);

    TRACE("(%p) : Releasing from %ld\n", This, refCount + 1);

    if (!refCount) {
        /*TODO: Remove me once d3d8 stateblocks are converted */
        if (This->stateBlock) IWineD3DStateBlock_Release((IWineD3DStateBlock *)This->stateBlock);
        IWineD3D_Release(This->wineD3D);
        HeapFree(GetProcessHeap(), 0, This);
    }
    return refCount;
}

/**********************************************************
 * IWineD3DDevice VTbl follows
 **********************************************************/

IWineD3DDeviceVtbl IWineD3DDevice_Vtbl =
{
    IWineD3DDeviceImpl_QueryInterface,
    IWineD3DDeviceImpl_AddRef,
    IWineD3DDeviceImpl_Release,
    IWineD3DDeviceImpl_GetParent,
    IWineD3DDeviceImpl_CreateVertexBuffer,
    IWineD3DDeviceImpl_CreateIndexBuffer,
    IWineD3DDeviceImpl_CreateStateBlock,
    IWineD3DDeviceImpl_CreateRenderTarget,
    IWineD3DDeviceImpl_CreateOffscreenPlainSurface,
    IWineD3DDeviceImpl_CreateTexture,
    IWineD3DDeviceImpl_CreateVolumeTexture,
    IWineD3DDeviceImpl_CreateVolume,
    IWineD3DDeviceImpl_CreateCubeTexture,

    IWineD3DDeviceImpl_SetFVF,
    IWineD3DDeviceImpl_GetFVF,
    IWineD3DDeviceImpl_SetStreamSource,
    IWineD3DDeviceImpl_GetStreamSource,
    IWineD3DDeviceImpl_SetTransform,
    IWineD3DDeviceImpl_GetTransform,
    IWineD3DDeviceImpl_MultiplyTransform,
    IWineD3DDeviceImpl_SetLight,
    IWineD3DDeviceImpl_GetLight,
    IWineD3DDeviceImpl_SetLightEnable,
    IWineD3DDeviceImpl_GetLightEnable,
    IWineD3DDeviceImpl_SetClipPlane,
    IWineD3DDeviceImpl_GetClipPlane,
    IWineD3DDeviceImpl_SetClipStatus,
    IWineD3DDeviceImpl_GetClipStatus,
    IWineD3DDeviceImpl_SetMaterial,
    IWineD3DDeviceImpl_GetMaterial,
    IWineD3DDeviceImpl_SetIndices,
    IWineD3DDeviceImpl_GetIndices,
    IWineD3DDeviceImpl_SetViewport,
    IWineD3DDeviceImpl_GetViewport,
    IWineD3DDeviceImpl_SetRenderState,
    IWineD3DDeviceImpl_GetRenderState,
    IWineD3DDeviceImpl_SetTextureStageState,
    IWineD3DDeviceImpl_GetTextureStageState,
    IWineD3DDeviceImpl_SetTexture,
    IWineD3DDeviceImpl_GetTexture,

    IWineD3DDeviceImpl_GetBackBuffer,
    IWineD3DDeviceImpl_GetDeviceCaps,
    IWineD3DDeviceImpl_GetDisplayMode,

    IWineD3DDeviceImpl_BeginScene,
    IWineD3DDeviceImpl_EndScene,
    IWineD3DDeviceImpl_Present,
    IWineD3DDeviceImpl_Clear,

    IWineD3DDeviceImpl_DrawPrimitive,
    IWineD3DDeviceImpl_DrawIndexedPrimitive,
    IWineD3DDeviceImpl_DrawPrimitiveUP,
    IWineD3DDeviceImpl_DrawIndexedPrimitiveUP,

    IWineD3DDeviceImpl_CreateVertexDeclaration,
    IWineD3DDeviceImpl_SetVertexDeclaration,
    IWineD3DDeviceImpl_GetVertexDeclaration,
    
    IWineD3DDeviceImpl_SetupTextureStates
};
