/*
 * Copyright 2002 Andriy Palamarchuk
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

#ifndef __WINE_LMWKSTA_H
#define __WINE_LMWKSTA_H

#include <lmcons.h>

#ifdef __cplusplus
extern "C" {
#endif

/* NetBIOS */
UCHAR WINAPI Netbios(PNCB pncb);

typedef struct _WKSTA_USER_INFO_0 {
    LPWSTR  wkui0_username;
} WKSTA_USER_INFO_0, *PWKSTA_USER_INFO_0, *LPWKSTA_USER_INFO_0;

typedef struct _WKSTA_USER_INFO_1 {
    LPWSTR  wkui1_username;
    LPWSTR  wkui1_logon_domain;
    LPWSTR  wkui1_oth_domains;
    LPWSTR  wkui1_logon_server;
} WKSTA_USER_INFO_1, *PWKSTA_USER_INFO_1, *LPWKSTA_USER_INFO_1;

typedef struct _WKSTA_USER_INFO_1101 {
    LPWSTR  wkui1101_oth_domains;
} WKSTA_USER_INFO_1101, *PWKSTA_USER_INFO_1101, *LPWKSTA_USER_INFO_1101;

/* workstation */
NET_API_STATUS WINAPI NetWkstaUserGetInfo(LPWSTR reserved, DWORD level, PBYTE* bufptr);

#ifdef __cplusplus
}
#endif

#endif
