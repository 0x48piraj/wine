/*
 * Win32 advapi functions
 *
 * Copyright 1995 Sven Verdoolaege, 1998 Juergen Schmied
 */

#include "winbase.h"
#include "windef.h"
#include "winerror.h"
#include "heap.h"

#include "debugtools.h"

DEFAULT_DEBUG_CHANNEL(advapi);

/******************************************************************************
 * BackupEventLogA [ADVAPI32.15]
 */
BOOL WINAPI BackupEventLogA( HANDLE hEventLog, LPCSTR lpBackupFileName )
{
	FIXME("stub\n");
	return TRUE;
}

/******************************************************************************
 * BackupEventLogW [ADVAPI32.16]
 *
 * PARAMS
 *   hEventLog        []
 *   lpBackupFileName []
 */
BOOL WINAPI
BackupEventLogW( HANDLE hEventLog, LPCWSTR lpBackupFileName )
{
	FIXME("stub\n");
	return TRUE;
}

/******************************************************************************
 * ClearEventLogA [ADVAPI32.19]
 */
BOOL WINAPI ClearEventLogA ( HANDLE hEventLog, LPCSTR lpBackupFileName )
{
	FIXME("stub\n");
	return TRUE;
}

/******************************************************************************
 * ClearEventLogW [ADVAPI32.20]
 */
BOOL WINAPI ClearEventLogW ( HANDLE hEventLog, LPCWSTR lpBackupFileName )
{
	FIXME("stub\n");
	return TRUE;
}

/******************************************************************************
 * CloseEventLog [ADVAPI32.21]
 */
BOOL WINAPI CloseEventLog ( HANDLE hEventLog )
{
	FIXME("stub\n");
	return TRUE;
}

/******************************************************************************
 * DeregisterEventSource [ADVAPI32.32]
 * Closes a handle to the specified event log
 *
 * PARAMS
 *    hEventLog [I] Handle to event log
 *
 * RETURNS STD
 */
BOOL WINAPI DeregisterEventSource( HANDLE hEventLog )
{
    FIXME("(%d): stub\n",hEventLog);
    return TRUE;
}

/******************************************************************************
 * GetNumberOfEventLogRecords [ADVAPI32.49]
 *
 * PARAMS
 *   hEventLog       []
 *   NumberOfRecords []
 */
BOOL WINAPI
GetNumberOfEventLogRecords( HANDLE hEventLog, PDWORD NumberOfRecords )
{
	FIXME("stub\n");
	return TRUE;
}

/******************************************************************************
 * GetOldestEventLogRecord [ADVAPI32.50]
 *
 * PARAMS
 *   hEventLog    []
 *   OldestRecord []
 */
BOOL WINAPI
GetOldestEventLogRecord( HANDLE hEventLog, PDWORD OldestRecord )
{
	FIXME(":stub\n");
	return TRUE;
}

/******************************************************************************
 * NotifyChangeEventLog [ADVAPI32.98]
 *
 * PARAMS
 *   hEventLog []
 *   hEvent    []
 */
BOOL WINAPI NotifyChangeEventLog( HANDLE hEventLog, HANDLE hEvent )
{
	FIXME("stub\n");
	return TRUE;
}

/******************************************************************************
 * OpenBackupEventLogA [ADVAPI32.105]
 */
HANDLE WINAPI
OpenBackupEventLogA( LPCSTR lpUNCServerName, LPCSTR lpFileName )
{
	FIXME("stub\n");
	return TRUE;
}

/******************************************************************************
 * OpenBackupEventLogW [ADVAPI32.106]
 *
 * PARAMS
 *   lpUNCServerName []
 *   lpFileName      []
 */
HANDLE WINAPI
OpenBackupEventLogW( LPCWSTR lpUNCServerName, LPCWSTR lpFileName )
{
	FIXME("stub\n");
	return TRUE;
}

/******************************************************************************
 * OpenEventLogA [ADVAPI32.107]
 */
HANDLE WINAPI OpenEventLogA(LPCSTR uncname,LPCSTR source) 
{
	FIXME("(%s,%s),stub!\n",uncname,source);
	return 0xcafe4242;
}

/******************************************************************************
 * OpenEventLogW [ADVAPI32.108]
 *
 * PARAMS
 *   uncname []
 *   source  []
 */
HANDLE WINAPI
OpenEventLogW( LPCWSTR uncname, LPCWSTR source )
{
	FIXME("stub\n");
	return TRUE;
}

/******************************************************************************
 * ReadEventLogA [ADVAPI32.124]
 */
BOOL WINAPI ReadEventLogA( HANDLE hEventLog, DWORD dwReadFlags, DWORD dwRecordOffset,
    LPVOID lpBuffer, DWORD nNumberOfBytesToRead, DWORD *pnBytesRead, DWORD *pnMinNumberOfBytesNeeded )
{
	FIXME("stub\n");
	return TRUE;
}

/******************************************************************************
 * ReadEventLogW [ADVAPI32.125]
 *
 * PARAMS
 *   hEventLog                []
 *   dwReadFlags              []
 *   dwRecordOffset           []
 *   lpBuffer                 []
 *   nNumberOfBytesToRead     []
 *   pnBytesRead              []
 *   pnMinNumberOfBytesNeeded []
 */
BOOL WINAPI
ReadEventLogW( HANDLE hEventLog, DWORD dwReadFlags, DWORD dwRecordOffset,
                 LPVOID lpBuffer, DWORD nNumberOfBytesToRead, 
                 DWORD *pnBytesRead, DWORD *pnMinNumberOfBytesNeeded )
{
	FIXME("stub\n");
	return TRUE;
}

/******************************************************************************
 * RegisterEventSourceA [ADVAPI32.174]
 */
HANDLE WINAPI RegisterEventSourceA( LPCSTR lpUNCServerName, LPCSTR lpSourceName )
{
    LPWSTR lpUNCServerNameW = HEAP_strdupAtoW(GetProcessHeap(),0,lpUNCServerName);
    LPWSTR lpSourceNameW = HEAP_strdupAtoW(GetProcessHeap(),0,lpSourceName);
    HANDLE ret = RegisterEventSourceW(lpUNCServerNameW,lpSourceNameW);
    HeapFree(GetProcessHeap(),0,lpSourceNameW);
    HeapFree(GetProcessHeap(),0,lpUNCServerNameW);
    return ret;
}

/******************************************************************************
 * RegisterEventSourceW [ADVAPI32.175]
 * Returns a registered handle to an event log
 *
 * PARAMS
 *   lpUNCServerName [I] Server name for source
 *   lpSourceName    [I] Source name for registered handle
 *
 * RETURNS
 *    Success: Handle
 *    Failure: NULL
 */
HANDLE WINAPI
RegisterEventSourceW( LPCWSTR lpUNCServerName, LPCWSTR lpSourceName )
{
    FIXME("(%s,%s): stub\n", debugstr_w(lpUNCServerName),
          debugstr_w(lpSourceName));
    return 1;
}

/******************************************************************************
 * ReportEventA [ADVAPI32.178]
 */
BOOL WINAPI ReportEventA ( HANDLE hEventLog, WORD wType, WORD wCategory, DWORD dwEventID,
    PSID lpUserSid, WORD wNumStrings, DWORD dwDataSize, LPCSTR *lpStrings, LPVOID lpRawData)
{
	FIXME("stub\n");
	return TRUE;
}

/******************************************************************************
 * ReportEventW [ADVAPI32.179]
 *
 * PARAMS
 *   hEventLog   []
 *   wType       []
 *   wCategory   []
 *   dwEventID   []
 *   lpUserSid   []
 *   wNumStrings []
 *   dwDataSize  []
 *   lpStrings   []
 *   lpRawData   []
 */
BOOL WINAPI
ReportEventW( HANDLE hEventLog, WORD wType, WORD wCategory, 
                DWORD dwEventID, PSID lpUserSid, WORD wNumStrings, 
                DWORD dwDataSize, LPCWSTR *lpStrings, LPVOID lpRawData )
{
	FIXME("stub\n");
	return TRUE;
}
