/*
 * MPR Password Cache functions
 *
 * Copyright 1999 Ulrich Weigand
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

#include "winbase.h"
#include "winnetwk.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(mpr);

/**************************************************************************
 * WNetCachePassword [MPR.@]  Saves password in cache
 *
 * NOTES
 *	only the parameter count is verifyed  
 *
 *	---- everything below this line might be wrong (js) -----
 * RETURNS
 *    Success: WN_SUCCESS
 *    Failure: WN_ACCESS_DENIED, WN_BAD_PASSWORD, WN_BADVALUE, WN_NET_ERROR,
 *             WN_NOT_SUPPORTED, WN_OUT_OF_MEMORY
 */
DWORD WINAPI WNetCachePassword(
    LPSTR pbResource, /* [in] Name of workgroup, computer, or resource */
    WORD cbResource,  /* [in] Size of name */
    LPSTR pbPassword, /* [in] Buffer containing password */
    WORD cbPassword,  /* [in] Size of password */
    BYTE nType,       /* [in] Type of password to cache */
    WORD x)
    
{
    FIXME( "(%p(%s), %d, %p(%s), %d, %d, 0x%08x): stub\n", 
           pbResource, debugstr_a(pbResource), cbResource,
	   pbPassword, debugstr_a(pbPassword), cbPassword,
	   nType, x );

    return WN_SUCCESS;
}

/*****************************************************************
 *  WNetRemoveCachedPassword [MPR.@]
 */
UINT WINAPI WNetRemoveCachedPassword( LPSTR pbResource, WORD cbResource, 
                                      BYTE nType )
{
    FIXME( "(%p(%s), %d, %d): stub\n", 
           pbResource, debugstr_a(pbResource), cbResource, nType );

    return WN_SUCCESS;
}

/*****************************************************************
 * WNetGetCachedPassword [MPR.@]  Retrieves password from cache
 *
 * NOTES
 *  the stub seems to be wrong:
 *	arg1:	ptr	0x40xxxxxx -> (no string)
 *	arg2:	len	36
 *	arg3:	ptr	0x40xxxxxx -> (no string)
 *	arg4:	ptr	0x40xxxxxx -> 0xc8
 *	arg5:	type?	4
 *  
 *	---- everything below this line might be wrong (js) -----
 * RETURNS
 *    Success: WN_SUCCESS
 *    Failure: WN_ACCESS_DENIED, WN_BAD_PASSWORD, WN_BAD_VALUE, 
 *             WN_NET_ERROR, WN_NOT_SUPPORTED, WN_OUT_OF_MEMORY
 */
DWORD WINAPI WNetGetCachedPassword(
    LPSTR pbResource,   /* [in]  Name of workgroup, computer, or resource */
    WORD cbResource,    /* [in]  Size of name */
    LPSTR pbPassword,   /* [out] Buffer to receive password */
    LPWORD pcbPassword, /* [out] Receives size of password */
    BYTE nType)         /* [in]  Type of password to retrieve */
{
    FIXME( "(%p(%s), %d, %p, %p, %d): stub\n",
           pbResource, debugstr_a(pbResource), cbResource,
	   pbPassword, pcbPassword, nType );

    SetLastError(WN_NO_NETWORK);
    return WN_NO_NETWORK;
}

/*******************************************************************
 * WNetEnumCachedPasswords [MPR.@]
 *
 * NOTES
 *	the parameter count is verifyed  
 *
 *  observed values:
 *	arg1	ptr	0x40xxxxxx -> (no string)
 *	arg2	int	32
 *	arg3	type?	4
 *	arg4	enumPasswordProc (verifyed)
 *	arg5	ptr	0x40xxxxxx -> 0x0
 *
 *	---- everything below this line might be wrong (js) -----
 */

UINT WINAPI WNetEnumCachedPasswords( LPSTR pbPrefix, WORD cbPrefix,
                                     BYTE nType, ENUMPASSWORDPROC enumPasswordProc, DWORD x)
{
    FIXME( "(%p(%s), %d, %d, %p, 0x%08lx): stub\n",
           pbPrefix, debugstr_a(pbPrefix), cbPrefix,
	   nType, enumPasswordProc, x );

    SetLastError(WN_NO_NETWORK);
    return WN_NO_NETWORK;
}


