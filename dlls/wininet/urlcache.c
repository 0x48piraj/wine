/*
 * Wininet - Url Cache functions
 *
 * Copyright 2001,2002 CodeWeavers
 *
 * Eric Kohl
 * Aric Stewart
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

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "windef.h"
#include "winbase.h"
#include "wininet.h"
#include "winerror.h"

#include "wine/debug.h"
#include "internet.h"

WINE_DEFAULT_DEBUG_CHANNEL(wininet);


INTERNETAPI HANDLE WINAPI FindFirstUrlCacheEntryA(LPCSTR lpszUrlSearchPattern,
 LPINTERNET_CACHE_ENTRY_INFOA lpFirstCacheEntryInfo, LPDWORD lpdwFirstCacheEntryInfoBufferSize)
{
  FIXME("STUB\n");
  return 0;
}

INTERNETAPI HANDLE WINAPI FindFirstUrlCacheEntryW(LPCWSTR lpszUrlSearchPattern,
 LPINTERNET_CACHE_ENTRY_INFOW lpFirstCacheEntryInfo, LPDWORD lpdwFirstCacheEntryInfoBufferSize)
{
  FIXME("STUB\n");
  return 0;
}

BOOL WINAPI RetrieveUrlCacheEntryFileA (LPCSTR lpszUrlName,
                                        LPINTERNET_CACHE_ENTRY_INFOA lpCacheEntryInfo, LPDWORD
                                        lpdwCacheEntryInfoBufferSize, DWORD dwReserved)
{
    FIXME("STUB\n");
    SetLastError(ERROR_FILE_NOT_FOUND);
    return FALSE;
}

BOOL WINAPI DeleteUrlCacheEntry(LPCSTR lpszUrlName)
{
    FIXME("STUB (%s)\n",lpszUrlName);
    SetLastError(ERROR_FILE_NOT_FOUND);
    return FALSE;
}
