/*
 * UrlMon
 *
 * Copyright (c) 2000 Patrik Stridvall
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

#include "windef.h"
#include "winerror.h"
#include "wtypes.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(win32);

/***********************************************************************
 *		DllInstall (URLMON.@)
 */
HRESULT WINAPI URLMON_DllInstall(BOOL bInstall, LPCWSTR cmdline)
{
  FIXME("(%s, %s): stub\n", bInstall?"TRUE":"FALSE", 
	debugstr_w(cmdline));

  return S_OK;
}

/***********************************************************************
 *		DllCanUnloadNow (URLMON.@)
 */
HRESULT WINAPI URLMON_DllCanUnloadNow(void)
{
    FIXME("(void): stub\n");

    return S_FALSE;
}

/***********************************************************************
 *		DllGetClassObject (URLMON.@)
 */
HRESULT WINAPI URLMON_DllGetClassObject(REFCLSID rclsid, REFIID riid,
					LPVOID *ppv)
{
    FIXME("(%p, %p, %p): stub\n", debugstr_guid(rclsid), 
	  debugstr_guid(riid), ppv);

    return CLASS_E_CLASSNOTAVAILABLE;
}

/***********************************************************************
 *		DllRegisterServer (URLMON.@)
 */
HRESULT WINAPI URLMON_DllRegisterServer(void)
{
    FIXME("(void): stub\n");

    return S_OK;
}

/***********************************************************************
 *		DllRegisterServerEx (URLMON.@)
 */
HRESULT WINAPI URLMON_DllRegisterServerEx(void)
{
    FIXME("(void): stub\n");

    return E_FAIL;
}

/***********************************************************************
 *		DllUnregisterServer (URLMON.@)
 */
HRESULT WINAPI URLMON_DllUnregisterServer(void)
{
    FIXME("(void): stub\n");

    return S_OK;
}

/**************************************************************************
 *                 UrlMkSetSessionOption (URLMON.@)
 */
 HRESULT WINAPI UrlMkSetSessionOption(long lost, LPVOID *splat, long time,
 					long nosee)
{
    FIXME("(%#lx, %p, %#lx, %#lx): stub\n", lost, splat, time, nosee);
    
    return S_OK;
}

/**************************************************************************
 *                 CoInternetGetSession (URLMON.@)
 */
HRESULT WINAPI CoInternetGetSession(DWORD dwSessionMode,
                                    LPVOID /* IInternetSession ** */ ppIInternetSession,
                                    DWORD dwReserved)
{
    FIXME("(%ld, %p, %ld): stub\n", dwSessionMode, ppIInternetSession,
	  dwReserved);

    if(dwSessionMode) {
      ERR("dwSessionMode: %ld, must be zero\n", dwSessionMode);
    }

    if(dwReserved) {
      ERR("dwReserved: %ld, must be zero\n", dwReserved);
    }

    return S_OK;
}


/**************************************************************************
 *                 ObtainUserAgentString (URLMON.@)
 */
HRESULT WINAPI ObtainUserAgentString(DWORD dwOption, LPCSTR pcszUAOut, DWORD *cbSize)
{
    FIXME("(%ld, %p, %p): stub\n", dwOption, pcszUAOut, cbSize);

    if(dwOption) {
      ERR("dwOption: %ld, must be zero\n", dwOption);
    }

    return S_OK;
}
