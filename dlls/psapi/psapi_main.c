/*
 *      PSAPI library
 *
 *      Copyright 1998  Patrik Stridvall
 */

#include "winbase.h"
#include "windef.h"
#include "winerror.h"
#include "debugtools.h"
#include "psapi.h"

DEFAULT_DEBUG_CHANNEL(psapi);

#include <string.h>

/***********************************************************************
 *           EmptyWorkingSet (PSAPI.@)
 */
BOOL WINAPI EmptyWorkingSet(HANDLE hProcess)
{
  return SetProcessWorkingSetSize(hProcess, 0xFFFFFFFF, 0xFFFFFFFF);
}

/***********************************************************************
 *           EnumDeviceDrivers (PSAPI.@)
 */
BOOL WINAPI EnumDeviceDrivers(
  LPVOID *lpImageBase, DWORD cb, LPDWORD lpcbNeeded)
{
  FIXME("(%p, %ld, %p): stub\n", lpImageBase, cb, lpcbNeeded);

  if(lpcbNeeded)
    *lpcbNeeded = 0;

  return TRUE;
}    


/***********************************************************************
 *           EnumProcesses (PSAPI.@)
 */
BOOL WINAPI EnumProcesses(DWORD *lpidProcess, DWORD cb, DWORD *lpcbNeeded)
{
  FIXME("(%p, %ld, %p): stub\n", lpidProcess,cb, lpcbNeeded);

  if(lpcbNeeded)
    *lpcbNeeded = 0;

  return TRUE;
}

/***********************************************************************
 *           EnumProcessModules (PSAPI.@)
 */
BOOL WINAPI EnumProcessModules(
  HANDLE hProcess, HMODULE *lphModule, DWORD cb, LPDWORD lpcbNeeded)
{
  FIXME("(hProcess=0x%08x, %p, %ld, %p): stub\n",
    hProcess, lphModule, cb, lpcbNeeded
  );

  if(lpcbNeeded)
    *lpcbNeeded = 0;

  return TRUE;
}

/***********************************************************************
 *          GetDeviceDriverBaseNameA (PSAPI.@)
 */
DWORD WINAPI GetDeviceDriverBaseNameA(
  LPVOID ImageBase, LPSTR lpBaseName, DWORD nSize)
{
  FIXME("(%p, %s, %ld): stub\n",
    ImageBase, debugstr_a(lpBaseName), nSize
  );

  if(lpBaseName && nSize)
    lpBaseName[0] = '\0';

  return 0;
}

/***********************************************************************
 *           GetDeviceDriverBaseNameW (PSAPI.@)
 */
DWORD WINAPI GetDeviceDriverBaseNameW(
  LPVOID ImageBase, LPWSTR lpBaseName, DWORD nSize)
{
  FIXME("(%p, %s, %ld): stub\n",
    ImageBase, debugstr_w(lpBaseName), nSize
  );

  if(lpBaseName && nSize)
    lpBaseName[0] = '\0';

  return 0;
}

/***********************************************************************
 *           GetDeviceDriverFileNameA (PSAPI.@)
 */
DWORD WINAPI GetDeviceDriverFileNameA(
  LPVOID ImageBase, LPSTR lpFilename, DWORD nSize)
{
  FIXME("(%p, %s, %ld): stub\n",
    ImageBase, debugstr_a(lpFilename), nSize
  );

  if(lpFilename && nSize)
    lpFilename[0] = '\0';

  return 0;
}

/***********************************************************************
 *           GetDeviceDriverFileNameW (PSAPI.@)
 */
DWORD WINAPI GetDeviceDriverFileNameW(
  LPVOID ImageBase, LPWSTR lpFilename, DWORD nSize)
{
  FIXME("(%p, %s, %ld): stub\n",
    ImageBase, debugstr_w(lpFilename), nSize
  );

  if(lpFilename && nSize)
    lpFilename[0] = '\0';

  return 0;
}

/***********************************************************************
 *           GetMappedFileNameA (PSAPI.@)
 */
DWORD WINAPI GetMappedFileNameA(
  HANDLE hProcess, LPVOID lpv, LPSTR lpFilename, DWORD nSize)
{
  FIXME("(hProcess=0x%08x, %p, %s, %ld): stub\n",
    hProcess, lpv, debugstr_a(lpFilename), nSize
  );

  if(lpFilename && nSize)
    lpFilename[0] = '\0';

  return 0;
}

/***********************************************************************
 *           GetMappedFileNameW (PSAPI.@)
 */
DWORD WINAPI GetMappedFileNameW(
  HANDLE hProcess, LPVOID lpv, LPWSTR lpFilename, DWORD nSize)
{
  FIXME("(hProcess=0x%08x, %p, %s, %ld): stub\n",
    hProcess, lpv, debugstr_w(lpFilename), nSize
  );

  if(lpFilename && nSize)
    lpFilename[0] = '\0';

  return 0;
}

/***********************************************************************
 *           GetModuleBaseNameA (PSAPI.@)
 */
DWORD WINAPI GetModuleBaseNameA(
  HANDLE hProcess, HMODULE hModule, LPSTR lpBaseName, DWORD nSize)
{
  FIXME("(hProcess=0x%08x, hModule=0x%08x, %s, %ld): stub\n",
    hProcess, hModule, debugstr_a(lpBaseName), nSize
  );

  if(lpBaseName && nSize)
    lpBaseName[0] = '\0';

  return 0;
}

/***********************************************************************
 *           GetModuleBaseNameW (PSAPI.@)
 */
DWORD WINAPI GetModuleBaseNameW(
  HANDLE hProcess, HMODULE hModule, LPWSTR lpBaseName, DWORD nSize)
{
  FIXME("(hProcess=0x%08x, hModule=0x%08x, %s, %ld): stub\n",
    hProcess, hModule, debugstr_w(lpBaseName), nSize);

  if(lpBaseName && nSize)
    lpBaseName[0] = '\0';

  return 0;
}

/***********************************************************************
 *           GetModuleFileNameExA (PSAPI.@)
 */
DWORD WINAPI GetModuleFileNameExA(
  HANDLE hProcess, HMODULE hModule, LPSTR lpFilename, DWORD nSize)
{
  FIXME("(hProcess=0x%08x,hModule=0x%08x, %s, %ld): stub\n",
    hProcess, hModule, debugstr_a(lpFilename), nSize
  );

  if(lpFilename&&nSize)
    lpFilename[0]='\0';

  return 0;
}

/***********************************************************************
 *           GetModuleFileNameExW (PSAPI.@)
 */
DWORD WINAPI GetModuleFileNameExW(
  HANDLE hProcess, HMODULE hModule, LPWSTR lpFilename, DWORD nSize)
{
  FIXME("(hProcess=0x%08x,hModule=0x%08x, %s, %ld): stub\n",
    hProcess, hModule, debugstr_w(lpFilename), nSize
  );

  if(lpFilename && nSize)
    lpFilename[0] = '\0';

  return 0;
}

/***********************************************************************
 *           GetModuleInformation (PSAPI.@)
 */
BOOL WINAPI GetModuleInformation(
  HANDLE hProcess, HMODULE hModule, LPMODULEINFO lpmodinfo, DWORD cb)
{
  FIXME("(hProcess=0x%08x, hModule=0x%08x, %p, %ld): stub\n",
    hProcess, hModule, lpmodinfo, cb
  );

  memset(lpmodinfo, 0, cb);

  return TRUE;
}

/***********************************************************************
 *           GetProcessMemoryInfo (PSAPI.@)
 */
BOOL WINAPI GetProcessMemoryInfo(
  HANDLE Process, PPROCESS_MEMORY_COUNTERS ppsmemCounters, DWORD cb)
{
  FIXME("(hProcess=0x%08x, %p, %ld): stub\n",
    Process, ppsmemCounters, cb
  );

  memset(ppsmemCounters, 0, cb);

  return TRUE;
}

/***********************************************************************
 *           GetWsChanges (PSAPI.@)
 */
BOOL WINAPI GetWsChanges(
  HANDLE hProcess, PPSAPI_WS_WATCH_INFORMATION lpWatchInfo, DWORD cb)
{
  FIXME("(hProcess=0x%08x, %p, %ld): stub\n",
    hProcess, lpWatchInfo, cb
  );

  memset(lpWatchInfo, 0, cb);

  return TRUE;
}

/***********************************************************************
 *           InitializeProcessForWsWatch (PSAPI.@)
 */
BOOL WINAPI InitializeProcessForWsWatch(HANDLE hProcess)
{
  FIXME("(hProcess=0x%08x): stub\n", hProcess);

  return TRUE;
}

/***********************************************************************
 *           QueryWorkingSet (PSAPI.?)
 * FIXME
 *     I haven't been able to find the ordinal for this function,
 *     This means it can't be called from outside the DLL.
 */
BOOL WINAPI QueryWorkingSet(HANDLE hProcess, LPVOID pv, DWORD cb)
{
  FIXME("(hProcess=0x%08x, %p, %ld)\n", hProcess, pv, cb);

  if(pv && cb)
    ((DWORD *) pv)[0] = 0; /* Empty WorkingSet */

  return TRUE;
}





