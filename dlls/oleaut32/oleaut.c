/*
 *	OLEAUT32
 *
 */
#include <string.h>

#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"
#include "winerror.h"

#include "ole2.h"
#include "heap.h"
#include "debugtools.h"

DEFAULT_DEBUG_CHANNEL(ole);

static WCHAR	_delimiter[2] = {'!',0}; /* default delimiter apparently */
static WCHAR	*pdelimiter = &_delimiter[0];

/***********************************************************************
 *		RegisterActiveObject
 */
HRESULT WINAPI RegisterActiveObject(
	LPUNKNOWN punk,REFCLSID rcid,DWORD dwFlags,LPDWORD pdwRegister
) {
	WCHAR 			guidbuf[80];
	HRESULT			ret;
	LPRUNNINGOBJECTTABLE	runobtable;
	LPMONIKER		moniker;

	StringFromGUID2(rcid,guidbuf,39);
	ret = CreateItemMoniker(pdelimiter,guidbuf,&moniker);
	if (FAILED(ret)) 
		return ret;
	ret = GetRunningObjectTable(0,&runobtable);
	if (FAILED(ret)) {
		IMoniker_Release(moniker);
		return ret;
	}
	ret = IRunningObjectTable_Register(runobtable,dwFlags,punk,moniker,pdwRegister);
	IRunningObjectTable_Release(runobtable);
	IMoniker_Release(moniker);
	return ret;
}

/***********************************************************************
 *		RevokeActiveObject
 */
HRESULT WINAPI RevokeActiveObject(DWORD xregister,LPVOID reserved)
{
	LPRUNNINGOBJECTTABLE	runobtable;
	HRESULT			ret;

	ret = GetRunningObjectTable(0,&runobtable);
	if (FAILED(ret)) return ret;
	ret = IRunningObjectTable_Revoke(runobtable,xregister);
	if (SUCCEEDED(ret)) ret = S_OK;
	IRunningObjectTable_Release(runobtable);
	return ret;
}

/***********************************************************************
 *		GetActiveObject
 */
HRESULT WINAPI GetActiveObject(REFCLSID rcid,LPVOID preserved,LPUNKNOWN *ppunk)
{
	WCHAR 			guidbuf[80];
	HRESULT			ret;
	LPRUNNINGOBJECTTABLE	runobtable;
	LPMONIKER		moniker;

	StringFromGUID2(rcid,guidbuf,39);
	ret = CreateItemMoniker(pdelimiter,guidbuf,&moniker);
	if (FAILED(ret)) 
		return ret;
	ret = GetRunningObjectTable(0,&runobtable);
	if (FAILED(ret)) {
		IMoniker_Release(moniker);
		return ret;
	}
	ret = IRunningObjectTable_GetObject(runobtable,moniker,ppunk);
	IRunningObjectTable_Release(runobtable);
	IMoniker_Release(moniker);
	return ret;
}

/***********************************************************************
 *           OaBuildVersion           [OLEAUT32.170]
 *
 * known OLEAUT32.DLL versions:
 * OLE 2.1  NT				1993-95	10     3023
 * OLE 2.1					10     3027
 * OLE 2.20 W95/NT			1993-96	20     4112
 * OLE 2.20 W95/NT			1993-96	20     4118
 * OLE 2.20 W95/NT			1993-96	20     4122
 * OLE 2.30 W95/NT			1993-98	30     4265
 * OLE 2.40 NT??			1993-98	40     4267
 * OLE 2.40 W98 SE orig. file		1993-98	40     4275
 */
UINT WINAPI OaBuildVersion()
{
    FIXME("Please report to a.mohr@mailto.de if you get version error messages !\n");
    switch(GetVersion() & 0x8000ffff)  /* mask off build number */
    {
    case 0x80000a03:  /* WIN31 */
		return MAKELONG(4049, 20); /* from Win32s 1.1e */
    case 0x80000004:  /* WIN95 */
		return MAKELONG(4265, 30);
    case 0x80000a04:  /* WIN98 */
		return MAKELONG(4275, 40); /* value of W98 SE; orig. W98 AFAIK has 4265, 30 just as W95 */
    case 0x00003303:  /* NT351 */
		return MAKELONG(4265, 30); /* value borrowed from Win95 */
    case 0x00000004:  /* NT40 */
		return MAKELONG(4122, 20); /* ouch ! Quite old, I guess */
    default:
		ERR("Version value not known yet. Please investigate it !\n");
		return 0x0;
    }
}

/***********************************************************************
 *		DllRegisterServer
 */
HRESULT WINAPI OLEAUT32_DllRegisterServer() { 
    FIXME("stub!\n");
    return S_OK;
}

/***********************************************************************
 *		DllUnregisterServer
 */
HRESULT WINAPI OLEAUT32_DllUnregisterServer() {
    FIXME("stub!\n");
    return S_OK;
}
