/*
 * defines helperfunctions to manipulate the contents of a IShellFolder
 *
 * Copyright 2000 Juergen Schmied
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

#ifndef __WINE_SHELLFOLDER_HELP_H
#define __WINE_SHELLFOLDER_HELP_H

#include "winbase.h"

#include "wine/obj_base.h"
#include "wine/obj_shellfolder.h"


/*****************************************************************************
 * Predeclare the interfaces
 */
DEFINE_GUID(IID_ISFHelper, 0x1fe68efbL, 0x1874, 0x9812, 0x56, 0xdc, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
typedef struct ISFHelper ISFHelper, *LPISFHELPER;

/*****************************************************************************
 * ISFHelper interface
 */

#define ICOM_INTERFACE ISFHelper
#define ISFHelper_METHODS \
	ICOM_METHOD2 (HRESULT, GetUniqueName, LPSTR, lpName, UINT, uLen) \
	ICOM_METHOD3 (HRESULT, AddFolder, HWND, hwnd, LPCSTR, lpName, LPITEMIDLIST*, ppidlOut) \
	ICOM_METHOD2 (HRESULT, DeleteItems, UINT, cidl, LPCITEMIDLIST*, apidl) \
	ICOM_METHOD3 (HRESULT, CopyItems, IShellFolder*, pSFFrom, UINT, cidl, LPCITEMIDLIST*, apidl)
#define ISFHelper_IMETHODS \
    IUnknown_IMETHODS \
    ISFHelper_METHODS
ICOM_DEFINE(ISFHelper, IUnknown)
#undef ICOM_INTERFACE

/*** IUnknown methods ***/
#define ISFHelper_QueryInterface(p,a,b)		ICOM_CALL2(QueryInterface,p,a,b)
#define ISFHelper_AddRef(p)			ICOM_CALL (AddRef,p)
#define ISFHelper_Release(p)			ICOM_CALL (Release,p)
/*** ISFHelper methods ***/
#define ISFHelper_GetUniqueName(p,a,b)		ICOM_CALL2(GetUniqueName,p,a,b)
#define ISFHelper_AddFolder(p,a,b,c)		ICOM_CALL3(AddFolder,p,a,b,c)
#define ISFHelper_DeleteItems(p,a,b)		ICOM_CALL2(DeleteItems,p,a,b)
#define ISFHelper_CopyItems(p,a,b,c)		ICOM_CALL3(CopyItems,p,a,b,c)

#endif /* __WINE_SHELLFOLDER_HELP_H */
