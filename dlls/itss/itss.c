/*
 *    ITSS Class Factory
 *
 * Copyright 2002 Lionel Ulmer
 * Copyright 2004 Mike McCormack
 *
 *  see http://bonedaddy.net/pabs3/hhm/#chmspec
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

#include <stdarg.h>
#include <stdio.h>

#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "winnls.h"
#include "winreg.h"
#include "ole2.h"

#include "itss.h"
#include "uuids.h"

#include "wine/unicode.h"
#include "wine/debug.h"

#include "itsstor.h"

WINE_DEFAULT_DEBUG_CHANNEL(itss);

#include "initguid.h"

DEFINE_GUID(CLSID_ITStorage,0x5d02926a,0x212e,0x11d0,0x9d,0xf9,0x00,0xa0,0xc9,0x22,0xe6,0xec );
DEFINE_GUID(CLSID_ITSProtocol,0x9d148290,0xb9c8,0x11d0,0xa4,0xcc,0x00,0x00,0xf8,0x01,0x49,0xf6);
DEFINE_GUID(IID_IITStorage, 0x88cc31de, 0x27ab, 0x11d0, 0x9d, 0xf9, 0x0, 0xa0, 0xc9, 0x22, 0xe6, 0xec);

static HRESULT ITSS_create(IUnknown *pUnkOuter, LPVOID *ppObj);

BOOL WINAPI DllMain(HINSTANCE hInstDLL, DWORD fdwReason, LPVOID lpv)
{
    switch(fdwReason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hInstDLL);
        break;
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

/******************************************************************************
 * ITSS ClassFactory
 */
typedef struct {
    IClassFactory ITF_IClassFactory;

    DWORD ref;
    HRESULT (*pfnCreateInstance)(IUnknown *pUnkOuter, LPVOID *ppObj);
} IClassFactoryImpl;

struct object_creation_info
{
    const CLSID *clsid;
    LPCSTR szClassName;
    HRESULT (*pfnCreateInstance)(IUnknown *pUnkOuter, LPVOID *ppObj);
};

static const struct object_creation_info object_creation[] =
{
    { &CLSID_ITStorage, "ITStorage", ITSS_create },
    { &CLSID_ITSProtocol, "ITSProtocol", ITS_IParseDisplayName_create },
};

static HRESULT WINAPI
ITSSCF_QueryInterface(LPCLASSFACTORY iface,REFIID riid,LPVOID *ppobj)
{
    ICOM_THIS(IClassFactoryImpl,iface);

    if (IsEqualGUID(riid, &IID_IUnknown)
	|| IsEqualGUID(riid, &IID_IClassFactory))
    {
	IClassFactory_AddRef(iface);
	*ppobj = This;
	return S_OK;
    }

    WARN("(%p)->(%s,%p),not found\n",This,debugstr_guid(riid),ppobj);
    return E_NOINTERFACE;
}

static ULONG WINAPI ITSSCF_AddRef(LPCLASSFACTORY iface) {
    ICOM_THIS(IClassFactoryImpl,iface);
    return ++(This->ref);
}

static ULONG WINAPI ITSSCF_Release(LPCLASSFACTORY iface) {
    ICOM_THIS(IClassFactoryImpl,iface);

    ULONG ref = --This->ref;

    if (ref == 0)
	HeapFree(GetProcessHeap(), 0, This);

    return ref;
}


static HRESULT WINAPI ITSSCF_CreateInstance(LPCLASSFACTORY iface, LPUNKNOWN pOuter,
					  REFIID riid, LPVOID *ppobj) {
    ICOM_THIS(IClassFactoryImpl,iface);
    HRESULT hres;
    LPUNKNOWN punk;
    
    TRACE("(%p)->(%p,%s,%p)\n",This,pOuter,debugstr_guid(riid),ppobj);

    hres = This->pfnCreateInstance(pOuter, (LPVOID *) &punk);
    if (FAILED(hres)) {
        *ppobj = NULL;
        return hres;
    }
    hres = IUnknown_QueryInterface(punk, riid, ppobj);
    if (FAILED(hres)) {
        *ppobj = NULL;
	return hres;
    }
    IUnknown_Release(punk);
    return hres;
}

static HRESULT WINAPI ITSSCF_LockServer(LPCLASSFACTORY iface,BOOL dolock) {
    ICOM_THIS(IClassFactoryImpl,iface);
    FIXME("(%p)->(%d),stub!\n",This,dolock);
    return S_OK;
}

static IClassFactoryVtbl ITSSCF_Vtbl =
{
    ITSSCF_QueryInterface,
    ITSSCF_AddRef,
    ITSSCF_Release,
    ITSSCF_CreateInstance,
    ITSSCF_LockServer
};


HRESULT WINAPI ITSS_DllGetClassObject(REFCLSID rclsid, REFIID iid, LPVOID *ppv)
{
    int i;
    IClassFactoryImpl *factory;

    TRACE("%s %s %p\n",debugstr_guid(rclsid), debugstr_guid(iid), ppv);
    
    if ( !IsEqualGUID( &IID_IClassFactory, iid )
	 && ! IsEqualGUID( &IID_IUnknown, iid) )
	return E_NOINTERFACE;

    for (i=0; i < sizeof(object_creation)/sizeof(object_creation[0]); i++)
    {
	if (IsEqualGUID(object_creation[i].clsid, rclsid))
	    break;
    }

    if (i == sizeof(object_creation)/sizeof(object_creation[0]))
    {
	FIXME("%s: no class found.\n", debugstr_guid(rclsid));
	return CLASS_E_CLASSNOTAVAILABLE;
    }

    TRACE("Creating a class factory for %s\n",object_creation[i].szClassName);

    factory = HeapAlloc(GetProcessHeap(), 0, sizeof(*factory));
    if (factory == NULL) return E_OUTOFMEMORY;

    factory->ITF_IClassFactory.lpVtbl = &ITSSCF_Vtbl;
    factory->ref = 1;

    factory->pfnCreateInstance = object_creation[i].pfnCreateInstance;

    *ppv = &(factory->ITF_IClassFactory);

    TRACE("(%p) <- %p\n", ppv, &(factory->ITF_IClassFactory) );

    return S_OK;
}

/*****************************************************************************/

typedef struct {
    IITStorageVtbl *vtbl_IITStorage;
    DWORD ref;
} ITStorageImpl;


HRESULT WINAPI ITStorageImpl_QueryInterface(
    IITStorage* iface,
    REFIID riid,
    void** ppvObject)
{
    ICOM_THIS(ITStorageImpl,iface);
    if (IsEqualGUID(riid, &IID_IUnknown)
	|| IsEqualGUID(riid, &IID_IITStorage))
    {
	IClassFactory_AddRef(iface);
	*ppvObject = This;
	return S_OK;
    }

    WARN("(%p)->(%s,%p),not found\n",This,debugstr_guid(riid),ppvObject);
    return E_NOINTERFACE;
}

ULONG WINAPI ITStorageImpl_AddRef(
    IITStorage* iface)
{
    ICOM_THIS(ITStorageImpl,iface);
    TRACE("%p\n", This);
    return ++(This->ref);
}

ULONG WINAPI ITStorageImpl_Release(
    IITStorage* iface)
{
    ICOM_THIS(ITStorageImpl,iface);
    ULONG ref = --This->ref;

    if (ref == 0)
	HeapFree(GetProcessHeap(), 0, This);

    return ref;
}

HRESULT WINAPI ITStorageImpl_StgCreateDocfile(
    IITStorage* iface,
    const WCHAR* pwcsName,
    DWORD grfMode,
    DWORD reserved,
    IStorage** ppstgOpen)
{
    ICOM_THIS(ITStorageImpl,iface);

    TRACE("%p %s %lu %lu %p\n", This,
          debugstr_w(pwcsName), grfMode, reserved, ppstgOpen );

    return ITSS_StgOpenStorage( pwcsName, NULL, grfMode,
                                0, reserved, ppstgOpen);
}

HRESULT WINAPI ITStorageImpl_StgCreateDocfileOnILockBytes(
    IITStorage* iface,
    ILockBytes* plkbyt,
    DWORD grfMode,
    DWORD reserved,
    IStorage** ppstgOpen)
{
    ICOM_THIS(ITStorageImpl,iface);
    FIXME("%p\n", This);
    return E_NOTIMPL;
}

HRESULT WINAPI ITStorageImpl_StgIsStorageFile(
    IITStorage* iface,
    const WCHAR* pwcsName)
{
    ICOM_THIS(ITStorageImpl,iface);
    FIXME("%p\n", This);
    return E_NOTIMPL;
}

HRESULT WINAPI ITStorageImpl_StgIsStorageILockBytes(
    IITStorage* iface,
    ILockBytes* plkbyt)
{
    ICOM_THIS(ITStorageImpl,iface);
    FIXME("%p\n", This);
    return E_NOTIMPL;
}

HRESULT WINAPI ITStorageImpl_StgOpenStorage(
    IITStorage* iface,
    const WCHAR* pwcsName,
    IStorage* pstgPriority,
    DWORD grfMode,
    SNB snbExclude,
    DWORD reserved,
    IStorage** ppstgOpen)
{
    ICOM_THIS(ITStorageImpl,iface);

    TRACE("%p %s %p %ld %p\n", This, debugstr_w( pwcsName ),
           pstgPriority, grfMode, snbExclude );

    return ITSS_StgOpenStorage( pwcsName, pstgPriority, grfMode,
                                snbExclude, reserved, ppstgOpen);
}

HRESULT WINAPI ITStorageImpl_StgOpenStorageOnILockBytes(
    IITStorage* iface,
    ILockBytes* plkbyt,
    IStorage* pStgPriority,
    DWORD grfMode,
    SNB snbExclude,
    DWORD reserved,
    IStorage** ppstgOpen)
{
    ICOM_THIS(ITStorageImpl,iface);
    FIXME("%p\n", This);
    return E_NOTIMPL;
}

HRESULT WINAPI ITStorageImpl_StgSetTimes(
    IITStorage* iface,
    WCHAR* lpszName,
    FILETIME* pctime,
    FILETIME* patime,
    FILETIME* pmtime)
{
    ICOM_THIS(ITStorageImpl,iface);
    FIXME("%p\n", This);
    return E_NOTIMPL;
}

HRESULT WINAPI ITStorageImpl_SetControlData(
    IITStorage* iface,
    PITS_Control_Data pControlData)
{
    ICOM_THIS(ITStorageImpl,iface);
    FIXME("%p\n", This);
    return E_NOTIMPL;
}

HRESULT WINAPI ITStorageImpl_DefaultControlData(
    IITStorage* iface,
    PITS_Control_Data* ppControlData)
{
    ICOM_THIS(ITStorageImpl,iface);
    FIXME("%p\n", This);
    return E_NOTIMPL;
}

HRESULT WINAPI ITStorageImpl_Compact(
    IITStorage* iface,
    const WCHAR* pwcsName,
    ECompactionLev iLev)
{
    ICOM_THIS(ITStorageImpl,iface);
    FIXME("%p\n", This);
    return E_NOTIMPL;
}

static IITStorageVtbl ITStorageImpl_Vtbl =
{
    ITStorageImpl_QueryInterface,
    ITStorageImpl_AddRef,
    ITStorageImpl_Release,
    ITStorageImpl_StgCreateDocfile,
    ITStorageImpl_StgCreateDocfileOnILockBytes,
    ITStorageImpl_StgIsStorageFile,
    ITStorageImpl_StgIsStorageILockBytes,
    ITStorageImpl_StgOpenStorage,
    ITStorageImpl_StgOpenStorageOnILockBytes,
    ITStorageImpl_StgSetTimes,
    ITStorageImpl_SetControlData,
    ITStorageImpl_DefaultControlData,
    ITStorageImpl_Compact,
};

static HRESULT ITSS_create(IUnknown *pUnkOuter, LPVOID *ppObj)
{
    ITStorageImpl *its;

    its = HeapAlloc( GetProcessHeap(), 0, sizeof(ITStorageImpl) );
    its->vtbl_IITStorage = &ITStorageImpl_Vtbl;
    its->ref = 1;

    TRACE("-> %p\n", its);
    *ppObj = (LPVOID) its;

    return S_OK;
}

/*****************************************************************************/

HRESULT WINAPI ITSS_DllRegisterServer(void)
{
    FIXME("\n");
    return S_OK;
}

BOOL WINAPI ITSS_DllCanUnloadNow(void)
{
    FIXME("\n");

    return FALSE;
}
