/* Direct3D Light
 * Copyright (c) 1998 Lionel ULMER
 *
 * This file contains the implementation of Direct3DLight.
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
#include "windef.h"
#include "winerror.h"
#include "wine/obj_base.h"
#include "ddraw.h"
#include "d3d.h"
#include "wine/debug.h"

#include "mesa_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(ddraw);

#define D3DLPRIVATE(x) mesa_d3dl_private*dlpriv=((mesa_d3dl_private*)x->private)

static ICOM_VTABLE(IDirect3DLight) light_vtable;

enum {
  D3D_1,
  D3D_2
};

/*******************************************************************************
 *				Light static functions
 */
static const float zero_value[] = {
  0.0, 0.0, 0.0, 0.0
};

static void update(IDirect3DLightImpl* This) {
  D3DLPRIVATE(This);
  switch (This->light.dltType) {
  case D3DLIGHT_POINT:         /* 1 */
    TRACE("Activating POINT\n");
    break;

  case D3DLIGHT_SPOT:          /* 2 */
    TRACE("Activating SPOT\n");
    break;

  case D3DLIGHT_DIRECTIONAL: {  /* 3 */
    float direction[4];

    TRACE("Activating DIRECTIONAL\n");
    TRACE("  direction : %f %f %f\n",
	  This->light.dvDirection.u1.x,
	  This->light.dvDirection.u2.y,
	  This->light.dvDirection.u3.z);
    _dump_colorvalue(" color    ", This->light.dcvColor);

    glLightfv(dlpriv->light_num, GL_AMBIENT, (float *) zero_value);
    glLightfv(dlpriv->light_num, GL_DIFFUSE, (float *) &(This->light.dcvColor));

    direction[0] = -This->light.dvDirection.u1.x;
    direction[1] = -This->light.dvDirection.u2.y;
    direction[2] = -This->light.dvDirection.u3.z;
    direction[3] = 0.0; /* This is a directional light */

    glLightfv(dlpriv->light_num, GL_POSITION, (float *) direction);
  } break;

  case D3DLIGHT_PARALLELPOINT:  /* 4 */
    TRACE("Activating PARRALLEL-POINT\n");
    break;

  default:
    TRACE("Not a known Light Type: %d\n",This->light.dltType);
    break;
  }
}

static void activate(IDirect3DLightImpl* This) {
  D3DLPRIVATE(This);

  ENTER_GL();
  update(This);
  /* If was not active, activate it */
  if (This->is_active == 0) {
    glEnable(dlpriv->light_num);
    This->is_active = 1;
  }
  LEAVE_GL();

  return ;
}

/*******************************************************************************
 *				Light Creation functions
 */
LPDIRECT3DLIGHT d3dlight_create(IDirect3D2Impl* d3d2)
{
  IDirect3DLightImpl* light;

  light = HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(IDirect3DLightImpl));
  light->ref = 1;
  ICOM_VTBL(light) = &light_vtable;
  light->d3d.d3d2 = d3d2;
  light->type = D3D_2;

  light->next = NULL;
  light->prev = NULL;
  light->activate = activate;
  light->is_active = 0;

  return (LPDIRECT3DLIGHT)light;
}

LPDIRECT3DLIGHT d3dlight_create_dx3(IDirect3DImpl* d3d1)
{
  IDirect3DLightImpl* light;

  light = HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(IDirect3DLightImpl));
  light->ref = 1;
  ICOM_VTBL(light) = &light_vtable;

  light->d3d.d3d1 = d3d1;
  light->type = D3D_1;

  light->next = NULL;
  light->prev = NULL;
  light->activate = activate;
  light->is_active = 0;

  return (LPDIRECT3DLIGHT)light;
}

/*******************************************************************************
 *				IDirect3DLight methods
 */

static HRESULT WINAPI IDirect3DLightImpl_QueryInterface(LPDIRECT3DLIGHT iface,
						    REFIID riid,
						    LPVOID* ppvObj)
{
  ICOM_THIS(IDirect3DLightImpl,iface);

  FIXME("(%p)->(%s,%p): stub\n", This, debugstr_guid(riid),ppvObj);

  return S_OK;
}



static ULONG WINAPI IDirect3DLightImpl_AddRef(LPDIRECT3DLIGHT iface)
{
  ICOM_THIS(IDirect3DLightImpl,iface);
  TRACE("(%p)->()incrementing from %lu.\n", This, This->ref );

  return ++(This->ref);
}



static ULONG WINAPI IDirect3DLightImpl_Release(LPDIRECT3DLIGHT iface)
{
  ICOM_THIS(IDirect3DLightImpl,iface);
  FIXME("(%p)->() decrementing from %lu.\n", This, This->ref );

  if (!--(This->ref)) {
    HeapFree(GetProcessHeap(),0,This);
    return 0;
  }

  return This->ref;
}

/*** IDirect3DLight methods ***/
static void dump_light(LPD3DLIGHT light)
{
  DPRINTF("  dwSize : %ld\n", light->dwSize);
}

static HRESULT WINAPI IDirect3DLightImpl_GetLight(LPDIRECT3DLIGHT iface,
					      LPD3DLIGHT lpLight)
{
  ICOM_THIS(IDirect3DLightImpl,iface);
  TRACE("(%p)->(%p)\n", This, lpLight);
  if (TRACE_ON(ddraw))
    dump_light(lpLight);

  /* Copies the light structure */
  switch (This->type) {
  case D3D_1:
    *((LPD3DLIGHT)lpLight) = *((LPD3DLIGHT) &(This->light));
    break;
  case D3D_2:
    *((LPD3DLIGHT2)lpLight) = *((LPD3DLIGHT2) &(This->light));
    break;
  }

  return DD_OK;
}

static HRESULT WINAPI IDirect3DLightImpl_SetLight(LPDIRECT3DLIGHT iface,
					      LPD3DLIGHT lpLight)
{
  ICOM_THIS(IDirect3DLightImpl,iface);
  TRACE("(%p)->(%p)\n", This, lpLight);
  if (TRACE_ON(ddraw))
    dump_light(lpLight);

  /* Stores the light */
  switch (This->type) {
  case D3D_1:
    *((LPD3DLIGHT) &(This->light)) = *((LPD3DLIGHT)lpLight);
    break;
  case D3D_2:
    *((LPD3DLIGHT2) &(This->light)) = *((LPD3DLIGHT2)lpLight);
    break;
  }

  ENTER_GL();
  if (This->is_active)
    update(This);
  LEAVE_GL();

  return DD_OK;
}

static HRESULT WINAPI IDirect3DLightImpl_Initialize(LPDIRECT3DLIGHT iface,
						LPDIRECT3D lpDirect3D)

{
  ICOM_THIS(IDirect3DLightImpl,iface);
  TRACE("(%p)->(%p)\n", This, lpDirect3D);

  return DDERR_ALREADYINITIALIZED;
}


/*******************************************************************************
 *				IDirect3DLight VTable
 */
static ICOM_VTABLE(IDirect3DLight) light_vtable =
{
  ICOM_MSVTABLE_COMPAT_DummyRTTIVALUE
  /*** IUnknown methods ***/
  IDirect3DLightImpl_QueryInterface,
  IDirect3DLightImpl_AddRef,
  IDirect3DLightImpl_Release,
  /*** IDirect3DLight methods ***/
  IDirect3DLightImpl_Initialize,
  IDirect3DLightImpl_SetLight,
  IDirect3DLightImpl_GetLight
};
