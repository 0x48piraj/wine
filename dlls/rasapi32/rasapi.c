/*
 * RASAPI32
 * 
 * Copyright 1998 Marcus Meissner
 */

#include "windef.h"
#include "ras.h"
#include "debugtools.h"

DEFAULT_DEBUG_CHANNEL(ras);

/**************************************************************************
 *                 RasEnumConnectionsA			[RASAPI32.544]
 */
DWORD WINAPI RasEnumConnectionsA( LPRASCONNA rca, LPDWORD x, LPDWORD y) {
	/* Remote Access Service stuff is done by underlying OS anyway */
	FIXME("(%p,%p,%p),stub!\n",rca,x,y);
	return 0;
}

/**************************************************************************
 *                 RasEnumEntriesA		        	[RASAPI32.546]
 */
DWORD WINAPI RasEnumEntriesA( LPCSTR Reserved, LPCSTR lpszPhoneBook,
        LPRASENTRYNAMEA lpRasEntryName, 
        LPDWORD lpcb, LPDWORD lpcEntries) 
{
	FIXME("(%p,%s,%p,%p,%p),stub!\n",Reserved,debugstr_a(lpszPhoneBook),
            lpRasEntryName,lpcb,lpcEntries);
        *lpcEntries = 0;
	return 0;
}

/**************************************************************************
 *                 RasGetEntryDialParamsA			[RASAPI32.550]
 */
DWORD WINAPI RasGetEntryDialParamsA( LPCSTR lpszPhoneBook,
        LPRASDIALPARAMSA lpRasDialParams,
        LPBOOL lpfPassword) 
{
	FIXME("(%s,%p,%p),stub!\n",debugstr_a(lpszPhoneBook),
            lpRasDialParams,lpfPassword);
	return 0;
}

/**************************************************************************
 *                 RasHangUpA			[RASAPI32.556]
 */
DWORD WINAPI RasHangUpA( HRASCONN hrasconn)
{
	FIXME("(%x),stub!\n",hrasconn);
	return 0;
}
