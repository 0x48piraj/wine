/* DirectMusicStyle Main
 *
 * Copyright (C) 2003-2004 Rok Mandeljc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "dmstyle_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(dmstyle);

typedef struct {
    /* IUnknown fields */
    IClassFactoryVtbl          *lpVtbl;
    DWORD                       ref;
} IClassFactoryImpl;

/******************************************************************
 *		DirectMusicSection ClassFactory
 */
static HRESULT WINAPI SectionCF_QueryInterface(LPCLASSFACTORY iface,REFIID riid,LPVOID *ppobj) {
	IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
	FIXME("(%p, %s, %p): stub\n", This, debugstr_dmguid(riid), ppobj);
	return E_NOINTERFACE;
}

static ULONG WINAPI SectionCF_AddRef(LPCLASSFACTORY iface) {
	IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
	return ++(This->ref);
}

static ULONG WINAPI SectionCF_Release(LPCLASSFACTORY iface) {
	IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
	/* static class, won't be  freed */
	return --(This->ref);
}

static HRESULT WINAPI SectionCF_CreateInstance(LPCLASSFACTORY iface, LPUNKNOWN pOuter, REFIID riid, LPVOID *ppobj) {
	IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
	TRACE ("(%p, %p, %s, %p)\n", This, pOuter, debugstr_dmguid(riid), ppobj);
	/* nothing here yet */
	WARN("(%p, %s, %p): not found\n", This, debugstr_dmguid(riid), ppobj);
	return E_NOINTERFACE;
}

static HRESULT WINAPI SectionCF_LockServer(LPCLASSFACTORY iface,BOOL dolock) {
	IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
	FIXME("(%p, %d): stub\n", This, dolock);
	return S_OK;
}

static IClassFactoryVtbl SectionCF_Vtbl = {
	SectionCF_QueryInterface,
	SectionCF_AddRef,
	SectionCF_Release,
	SectionCF_CreateInstance,
	SectionCF_LockServer
};

static IClassFactoryImpl Section_CF = {&SectionCF_Vtbl, 1 };

/******************************************************************
 *		DirectMusicStyle ClassFactory
 */
static HRESULT WINAPI StyleCF_QueryInterface(LPCLASSFACTORY iface,REFIID riid,LPVOID *ppobj) {
	IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
	FIXME("(%p, %s, %p): stub\n", This, debugstr_dmguid(riid), ppobj);
	return E_NOINTERFACE;
}

static ULONG WINAPI StyleCF_AddRef(LPCLASSFACTORY iface) {
	IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
	return ++(This->ref);
}

static ULONG WINAPI StyleCF_Release(LPCLASSFACTORY iface) {
	IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
	/* static class, won't be  freed */
	return --(This->ref);
}

static HRESULT WINAPI StyleCF_CreateInstance(LPCLASSFACTORY iface, LPUNKNOWN pOuter, REFIID riid, LPVOID *ppobj) {
	IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
	TRACE ("(%p, %p, %s, %p)\n", This, pOuter, debugstr_dmguid(riid), ppobj);
	return DMUSIC_CreateDirectMusicStyleImpl (riid, ppobj, pOuter);
}

static HRESULT WINAPI StyleCF_LockServer(LPCLASSFACTORY iface,BOOL dolock) {
	IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
	FIXME("(%p, %d): stub\n", This, dolock);
	return S_OK;
}

static IClassFactoryVtbl StyleCF_Vtbl = {
	StyleCF_QueryInterface,
	StyleCF_AddRef,
	StyleCF_Release,
	StyleCF_CreateInstance,
	StyleCF_LockServer
};

static IClassFactoryImpl Style_CF = {&StyleCF_Vtbl, 1 };

/******************************************************************
 *		DirectMusicChordTrack ClassFactory
 */
static HRESULT WINAPI ChordTrackCF_QueryInterface(LPCLASSFACTORY iface,REFIID riid,LPVOID *ppobj) {
	IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
	FIXME("(%p, %s, %p): stub\n", This, debugstr_dmguid(riid), ppobj);
	return E_NOINTERFACE;
}

static ULONG WINAPI ChordTrackCF_AddRef(LPCLASSFACTORY iface) {
	IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
	return ++(This->ref);
}

static ULONG WINAPI ChordTrackCF_Release(LPCLASSFACTORY iface) {
	IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
	/* static class, won't be  freed */
	return --(This->ref);
}

static HRESULT WINAPI ChordTrackCF_CreateInstance(LPCLASSFACTORY iface, LPUNKNOWN pOuter, REFIID riid, LPVOID *ppobj) {
	IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
	TRACE ("(%p, %p, %s, %p)\n", This, pOuter, debugstr_dmguid(riid), ppobj);
	return DMUSIC_CreateDirectMusicChordTrack (riid, ppobj, pOuter);
}

static HRESULT WINAPI ChordTrackCF_LockServer(LPCLASSFACTORY iface,BOOL dolock) {
	IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
	FIXME("(%p, %d): stub\n", This, dolock);
	return S_OK;
}

static IClassFactoryVtbl ChordTrackCF_Vtbl = {
	ChordTrackCF_QueryInterface,
	ChordTrackCF_AddRef,
	ChordTrackCF_Release,
	ChordTrackCF_CreateInstance,
	ChordTrackCF_LockServer
};

static IClassFactoryImpl ChordTrack_CF = {&ChordTrackCF_Vtbl, 1 };

/******************************************************************
 *		DirectMusicCommandTrack ClassFactory
 */
static HRESULT WINAPI CommandTrackCF_QueryInterface(LPCLASSFACTORY iface,REFIID riid,LPVOID *ppobj) {
	IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
	FIXME("(%p, %s, %p): stub\n", This, debugstr_dmguid(riid), ppobj);
	return E_NOINTERFACE;
}

static ULONG WINAPI CommandTrackCF_AddRef(LPCLASSFACTORY iface) {
	IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
	return ++(This->ref);
}

static ULONG WINAPI CommandTrackCF_Release(LPCLASSFACTORY iface) {
	IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
	/* static class, won't be  freed */
	return --(This->ref);
}

static HRESULT WINAPI CommandTrackCF_CreateInstance(LPCLASSFACTORY iface, LPUNKNOWN pOuter, REFIID riid, LPVOID *ppobj) {
	IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
	TRACE ("(%p, %p, %s, %p)\n", This, pOuter, debugstr_dmguid(riid), ppobj);
	return DMUSIC_CreateDirectMusicCommandTrack (riid, ppobj, pOuter);
}

static HRESULT WINAPI CommandTrackCF_LockServer(LPCLASSFACTORY iface,BOOL dolock) {
	IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
	FIXME("(%p, %d): stub\n", This, dolock);
	return S_OK;
}

static IClassFactoryVtbl CommandTrackCF_Vtbl = {
	CommandTrackCF_QueryInterface,
	CommandTrackCF_AddRef,
	CommandTrackCF_Release,
	CommandTrackCF_CreateInstance,
	CommandTrackCF_LockServer
};

static IClassFactoryImpl CommandTrack_CF = {&CommandTrackCF_Vtbl, 1 };

/******************************************************************
 *		DirectMusicStyleTrack ClassFactory
 */
static HRESULT WINAPI StyleTrackCF_QueryInterface(LPCLASSFACTORY iface,REFIID riid,LPVOID *ppobj) {
	IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
	FIXME("(%p, %s, %p): stub\n", This, debugstr_dmguid(riid), ppobj);
	return E_NOINTERFACE;
}

static ULONG WINAPI StyleTrackCF_AddRef(LPCLASSFACTORY iface) {
	IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
	return ++(This->ref);
}

static ULONG WINAPI StyleTrackCF_Release(LPCLASSFACTORY iface) {
	IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
	/* static class, won't be  freed */
	return --(This->ref);
}

static HRESULT WINAPI StyleTrackCF_CreateInstance(LPCLASSFACTORY iface, LPUNKNOWN pOuter, REFIID riid, LPVOID *ppobj) {
	IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
	TRACE ("(%p, %p, %s, %p)\n", This, pOuter, debugstr_dmguid(riid), ppobj);
	return DMUSIC_CreateDirectMusicStyleTrack (riid, ppobj, pOuter);
}

static HRESULT WINAPI StyleTrackCF_LockServer(LPCLASSFACTORY iface,BOOL dolock) {
	IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
	FIXME("(%p, %d): stub\n", This, dolock);
	return S_OK;
}

static IClassFactoryVtbl StyleTrackCF_Vtbl = {
	StyleTrackCF_QueryInterface,
	StyleTrackCF_AddRef,
	StyleTrackCF_Release,
	StyleTrackCF_CreateInstance,
	StyleTrackCF_LockServer
};

static IClassFactoryImpl StyleTrack_CF = {&StyleTrackCF_Vtbl, 1 };

/******************************************************************
 *		DirectMusicMotifTrack ClassFactory
 */
static HRESULT WINAPI MotifTrackCF_QueryInterface(LPCLASSFACTORY iface,REFIID riid,LPVOID *ppobj) {
	IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
	FIXME("(%p, %s, %p): stub\n", This, debugstr_dmguid(riid), ppobj);
	return E_NOINTERFACE;
}

static ULONG WINAPI MotifTrackCF_AddRef(LPCLASSFACTORY iface) {
	IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
	return ++(This->ref);
}

static ULONG WINAPI MotifTrackCF_Release(LPCLASSFACTORY iface) {
	IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
	/* static class, won't be  freed */
	return --(This->ref);
}

static HRESULT WINAPI MotifTrackCF_CreateInstance(LPCLASSFACTORY iface, LPUNKNOWN pOuter, REFIID riid, LPVOID *ppobj) {
	IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
	TRACE ("(%p, %p, %s, %p)\n", This, pOuter, debugstr_dmguid(riid), ppobj);
	return DMUSIC_CreateDirectMusicMotifTrack (riid, ppobj, pOuter);
}

static HRESULT WINAPI MotifTrackCF_LockServer(LPCLASSFACTORY iface,BOOL dolock) {
	IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
	FIXME("(%p, %d): stub\n", This, dolock);
	return S_OK;
}

static IClassFactoryVtbl MotifTrackCF_Vtbl = {
	MotifTrackCF_QueryInterface,
	MotifTrackCF_AddRef,
	MotifTrackCF_Release,
	MotifTrackCF_CreateInstance,
	MotifTrackCF_LockServer
};

static IClassFactoryImpl MotifTrack_CF = {&MotifTrackCF_Vtbl, 1 };

/******************************************************************
 *		DirectMusicAuditionTrack ClassFactory
 */
static HRESULT WINAPI AuditionTrackCF_QueryInterface(LPCLASSFACTORY iface,REFIID riid,LPVOID *ppobj) {
	IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
	FIXME("(%p, %s, %p): stub\n", This, debugstr_dmguid(riid), ppobj);
	return E_NOINTERFACE;
}

static ULONG WINAPI AuditionTrackCF_AddRef(LPCLASSFACTORY iface) {
	IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
	return ++(This->ref);
}

static ULONG WINAPI AuditionTrackCF_Release(LPCLASSFACTORY iface) {
	IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
	/* static class, won't be  freed */
	return --(This->ref);
}

static HRESULT WINAPI AuditionTrackCF_CreateInstance(LPCLASSFACTORY iface, LPUNKNOWN pOuter, REFIID riid, LPVOID *ppobj) {
	IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
	TRACE ("(%p, %p, %s, %p)\n", This, pOuter, debugstr_dmguid(riid), ppobj);
	return DMUSIC_CreateDirectMusicAuditionTrack (riid, ppobj, pOuter);
}

static HRESULT WINAPI AuditionTrackCF_LockServer(LPCLASSFACTORY iface,BOOL dolock) {
	IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
	FIXME("(%p, %d): stub\n", This, dolock);
	return S_OK;
}

static IClassFactoryVtbl AuditionTrackCF_Vtbl = {
	AuditionTrackCF_QueryInterface,
	AuditionTrackCF_AddRef,
	AuditionTrackCF_Release,
	AuditionTrackCF_CreateInstance,
	AuditionTrackCF_LockServer
};

static IClassFactoryImpl AuditionTrack_CF = {&AuditionTrackCF_Vtbl, 1 };

/******************************************************************
 *		DirectMusicMuteTrack ClassFactory
 */
static HRESULT WINAPI MuteTrackCF_QueryInterface(LPCLASSFACTORY iface,REFIID riid,LPVOID *ppobj) {
	IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
	FIXME("(%p, %s, %p): stub\n", This, debugstr_dmguid(riid), ppobj);
	return E_NOINTERFACE;
}

static ULONG WINAPI MuteTrackCF_AddRef(LPCLASSFACTORY iface) {
	IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
	return ++(This->ref);
}

static ULONG WINAPI MuteTrackCF_Release(LPCLASSFACTORY iface) {
	IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
	/* static class, won't be  freed */
	return --(This->ref);
}

static HRESULT WINAPI MuteTrackCF_CreateInstance(LPCLASSFACTORY iface, LPUNKNOWN pOuter, REFIID riid, LPVOID *ppobj) {
	IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
	TRACE ("(%p, %p, %s, %p)\n", This, pOuter, debugstr_dmguid(riid), ppobj);
	return DMUSIC_CreateDirectMusicMuteTrack (riid, ppobj, pOuter);
}

static HRESULT WINAPI MuteTrackCF_LockServer(LPCLASSFACTORY iface,BOOL dolock) {
	IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
	FIXME("(%p, %d): stub\n", This, dolock);
	return S_OK;
}

static IClassFactoryVtbl MuteTrackCF_Vtbl = {
	MuteTrackCF_QueryInterface,
	MuteTrackCF_AddRef,
	MuteTrackCF_Release,
	MuteTrackCF_CreateInstance,
	MuteTrackCF_LockServer
};

static IClassFactoryImpl MuteTrack_CF = {&MuteTrackCF_Vtbl, 1 };

/******************************************************************
 *		DllMain
 *
 *
 */
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
	if (fdwReason == DLL_PROCESS_ATTACH) {
            DisableThreadLibraryCalls(hinstDLL);
		/* FIXME: Initialisation */
	} else if (fdwReason == DLL_PROCESS_DETACH) {
		/* FIXME: Cleanup */
	}

	return TRUE;
}


/******************************************************************
 *		DllCanUnloadNow (DMSTYLE.1)
 *
 *
 */
HRESULT WINAPI DMSTYLE_DllCanUnloadNow(void) {
    FIXME("(void): stub\n");
    return S_FALSE;
}


/******************************************************************
 *		DllGetClassObject (DMSTYLE.2)
 *
 *
 */
HRESULT WINAPI DMSTYLE_DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID *ppv) {
    TRACE("(%s, %s, %p)\n", debugstr_dmguid(rclsid), debugstr_dmguid(riid), ppv);
    
	if (IsEqualCLSID (rclsid, &CLSID_DirectMusicSection) && IsEqualIID (riid, &IID_IClassFactory)) {
		*ppv = (LPVOID) &Section_CF;
		IClassFactory_AddRef((IClassFactory*)*ppv);
		return S_OK;
	} else if (IsEqualCLSID (rclsid, &CLSID_DirectMusicStyle) && IsEqualIID (riid, &IID_IClassFactory)) {
		*ppv = (LPVOID) &Style_CF;
		IClassFactory_AddRef((IClassFactory*)*ppv);
		return S_OK;		
	} else if (IsEqualCLSID (rclsid, &CLSID_DirectMusicChordTrack) && IsEqualIID (riid, &IID_IClassFactory)) {
		*ppv = (LPVOID) &ChordTrack_CF;
		IClassFactory_AddRef((IClassFactory*)*ppv);
		return S_OK;	
	} else if (IsEqualCLSID (rclsid, &CLSID_DirectMusicCommandTrack) && IsEqualIID (riid, &IID_IClassFactory)) {
		*ppv = (LPVOID) &CommandTrack_CF;
		IClassFactory_AddRef((IClassFactory*)*ppv);
		return S_OK;		
	} else if (IsEqualCLSID (rclsid, &CLSID_DirectMusicStyleTrack) && IsEqualIID (riid, &IID_IClassFactory)) {
		*ppv = (LPVOID) &StyleTrack_CF;
		IClassFactory_AddRef((IClassFactory*)*ppv);
		return S_OK;		
	} else if (IsEqualCLSID (rclsid, &CLSID_DirectMusicMotifTrack) && IsEqualIID (riid, &IID_IClassFactory)) {
		*ppv = (LPVOID) &MotifTrack_CF;
		IClassFactory_AddRef((IClassFactory*)*ppv);
		return S_OK;		
	} else if (IsEqualCLSID (rclsid, &CLSID_DirectMusicAuditionTrack) && IsEqualIID (riid, &IID_IClassFactory)) {
		*ppv = (LPVOID) &AuditionTrack_CF;
		IClassFactory_AddRef((IClassFactory*)*ppv);
		return S_OK;		
	} else if (IsEqualCLSID (rclsid, &CLSID_DirectMusicMuteTrack) && IsEqualIID (riid, &IID_IClassFactory)) {
		*ppv = (LPVOID) &MuteTrack_CF;
		IClassFactory_AddRef((IClassFactory*)*ppv);
		return S_OK;		
	}

    WARN("(%s, %s, %p): no interface found.\n", debugstr_dmguid(rclsid), debugstr_dmguid(riid), ppv);
    return CLASS_E_CLASSNOTAVAILABLE;
}
