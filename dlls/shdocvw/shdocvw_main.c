/*
 * SHDOCVW - Internet Explorer Web Control
 *
 * Copyright 2001 John R. Sheets (for CodeWeavers)
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

#include <string.h>
#include "winreg.h"
#include "initguid.h"
#include "ole2.h"
#include "shlwapi.h"

#include "shdocvw.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(shdocvw);

/***********************************************************************
 *              DllCanUnloadNow (SHDOCVW.@)
 */
HRESULT WINAPI SHDOCVW_DllCanUnloadNow(void)
{
    FIXME("(void): stub\n");

    return S_FALSE;
}

/*************************************************************************
 *              DllGetClassObject (SHDOCVW.@)
 */
HRESULT WINAPI SHDOCVW_DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID *ppv)
{
    TRACE("\n");

    if (IsEqualGUID(&IID_IClassFactory, riid))
    {
        /* Pass back our shdocvw class factory */
        *ppv = (LPVOID)&SHDOCVW_ClassFactory;
        IClassFactory_AddRef((IClassFactory*)&SHDOCVW_ClassFactory);

        return S_OK;
    }

    return CLASS_E_CLASSNOTAVAILABLE;
}

/***********************************************************************
 *              DllGetVersion (SHDOCVW.@)
 */
HRESULT WINAPI SHDOCVW_DllGetVersion (DLLVERSIONINFO *pdvi)
{
    FIXME("(void): stub\n");
    return S_FALSE;
}

/*************************************************************************
 *              DllInstall (SHDOCVW.@)
 */
HRESULT WINAPI SHDOCVW_DllInstall(BOOL bInstall, LPCWSTR cmdline)
{
   FIXME("(%s, %s): stub!\n", bInstall ? "TRUE":"FALSE", debugstr_w(cmdline));

   return S_OK;
}

/***********************************************************************
 *		DllRegisterServer (SHDOCVW.@)
 */
HRESULT WINAPI SHDOCVW_DllRegisterServer()
{
    FIXME("(), stub!\n");
    return S_OK;
}

/***********************************************************************
 *		DllUnregisterServer (SHDOCVW.@)
 */
HRESULT WINAPI SHDOCVW_DllUnregisterServer()
{
    FIXME("(), stub!\n");
    return S_OK;
}

