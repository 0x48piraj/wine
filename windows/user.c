/*
 * Misc. USER functions
 *
 * Copyright 1993 Robert J. Amstadt
 *	     1996 Alex Korobka 
 */

#include <stdlib.h>
#include "wine/winbase16.h"
#include "windef.h"
#include "wingdi.h"
#include "winuser.h"
#include "heap.h"
#include "user.h"
#include "task.h"
#include "queue.h"
#include "win.h"
#include "controls.h"
#include "cursoricon.h"
#include "hook.h"
#include "toolhelp.h"
#include "message.h"
#include "miscemu.h"
#include "sysmetrics.h"
#include "callback.h"
#include "local.h"
#include "module.h"
#include "debugtools.h"

DECLARE_DEBUG_CHANNEL(hook);
DECLARE_DEBUG_CHANNEL(local);
DECLARE_DEBUG_CHANNEL(system);
DECLARE_DEBUG_CHANNEL(win);
DECLARE_DEBUG_CHANNEL(win32);

/***********************************************************************
 *           GetFreeSystemResources   (USER.284)
 */
WORD WINAPI GetFreeSystemResources16( WORD resType )
{
    HINSTANCE16 gdi_inst;
    WORD gdi_heap;
    int userPercent, gdiPercent;

    if ((gdi_inst = LoadLibrary16( "GDI" )) < 32) return 0;
    gdi_heap = GlobalHandleToSel16( gdi_inst );

    switch(resType)
    {
    case GFSR_USERRESOURCES:
        userPercent = (int)LOCAL_CountFree( USER_HeapSel ) * 100 /
                               LOCAL_HeapSize( USER_HeapSel );
        gdiPercent  = 100;
        break;

    case GFSR_GDIRESOURCES:
        gdiPercent  = (int)LOCAL_CountFree( gdi_inst ) * 100 /
                               LOCAL_HeapSize( gdi_inst );
        userPercent = 100;
        break;

    case GFSR_SYSTEMRESOURCES:
        userPercent = (int)LOCAL_CountFree( USER_HeapSel ) * 100 /
                               LOCAL_HeapSize( USER_HeapSel );
        gdiPercent  = (int)LOCAL_CountFree( gdi_inst ) * 100 /
                               LOCAL_HeapSize( gdi_inst );
        break;

    default:
        userPercent = gdiPercent = 0;
        break;
    }
    FreeLibrary16( gdi_inst );
    return (WORD)min( userPercent, gdiPercent );
}


/**********************************************************************
 *           InitApp   (USER.5)
 */
INT16 WINAPI InitApp16( HINSTANCE16 hInstance )
{
    /* Hack: restore the divide-by-zero handler */
    /* FIXME: should set a USER-specific handler that displays a msg box */
    INT_SetPMHandler( 0, INT_GetPMHandler( 0xff ) );

    /* Create task message queue */
    if ( !GetFastQueue16() ) return 0;

    return 1;
}

/**********************************************************************
 *           USER_ModuleUnload
 */
static void USER_ModuleUnload( HMODULE16 hModule )
{
    HOOK_FreeModuleHooks( hModule );
    CLASS_FreeModuleClasses( hModule );
    CURSORICON_FreeModuleIcons( hModule );
}

/**********************************************************************
 *           USER_QueueCleanup
 */
static void USER_QueueCleanup( HQUEUE16 hQueue )
{
    if ( hQueue )
    {
        WND* desktop = WIN_GetDesktop();

        /* Patch desktop window */
        if ( desktop->hmemTaskQ == hQueue )
        {
            HTASK16 nextTask = TASK_GetNextTask( GetCurrentTask() );
            desktop->hmemTaskQ = GetTaskQueue16( nextTask );
        }

        /* Patch resident popup menu window */
        MENU_PatchResidentPopup( hQueue, NULL );

        TIMER_RemoveQueueTimers( hQueue );

        HOOK_FreeQueueHooks( hQueue );

        QUEUE_SetExitingQueue( hQueue );
        WIN_ResetQueueWindows( desktop, hQueue, (HQUEUE16)0);
        QUEUE_SetExitingQueue( 0 );

        /* Free the message queue */
        QUEUE_DeleteMsgQueue( hQueue );

        WIN_ReleaseDesktop();
    }
}

/**********************************************************************
 *           USER_AppExit
 */
static void USER_AppExit( HINSTANCE16 hInstance )
{
    /* FIXME: maybe destroy menus (Windows only complains about them
     * but does nothing);
     */

    /* ModuleUnload() in "Internals" */

    hInstance = GetExePtr( hInstance );
    if( GetModuleUsage16( hInstance ) <= 1 ) 
	USER_ModuleUnload( hInstance );
}


/***********************************************************************
 *           USER_SignalProc (USER.314)
 */
void WINAPI USER_SignalProc( HANDLE16 hTaskOrModule, UINT16 uCode,
                             UINT16 uExitFn, HINSTANCE16 hInstance,
                             HQUEUE16 hQueue )
{
    FIXME_(win)("Win 3.1 USER signal %04x\n", uCode );
}

/***********************************************************************
 *           FinalUserInit (USER.400)
 */
void WINAPI FinalUserInit16( void )
{
    /* FIXME: Should chain to FinalGdiInit now. */
}

/***********************************************************************
 *           UserSignalProc     (USER.610) (USER32.559)
 *
 * For comments about the meaning of uCode and dwFlags 
 * see PROCESS_CallUserSignalProc.
 *
 */
WORD WINAPI UserSignalProc( UINT uCode, DWORD dwThreadOrProcessID,
                            DWORD dwFlags, HMODULE16 hModule )
{
    HINSTANCE16 hInst;

    /* FIXME: Proper reaction to most signals still missing. */

    switch ( uCode )
    {
    case USIG_DLL_UNLOAD_WIN16:
    case USIG_DLL_UNLOAD_WIN32:
        USER_ModuleUnload( hModule );
        break;

    case USIG_DLL_UNLOAD_ORPHANS:
        break;

    case USIG_FAULT_DIALOG_PUSH:
    case USIG_FAULT_DIALOG_POP:
        break;

    case USIG_THREAD_INIT:
        break;

    case USIG_THREAD_EXIT:
        USER_QueueCleanup( GetThreadQueue16( dwThreadOrProcessID ) );
        SetThreadQueue16( dwThreadOrProcessID, 0 );
        break;

    case USIG_PROCESS_CREATE:
      break;

    case USIG_PROCESS_INIT:
    case USIG_PROCESS_LOADED:
      break;
    case USIG_PROCESS_RUNNING:
        break;

    case USIG_PROCESS_EXIT:
        break;

    case USIG_PROCESS_DESTROY:      
      hInst = ((TDB *)GlobalLock16( GetCurrentTask() ))->hInstance;
      USER_AppExit( hInst );
      break;

    default:
        FIXME_(win)("(%04x, %08lx, %04lx, %04x)\n",
                    uCode, dwThreadOrProcessID, dwFlags, hModule );
        break;
    }

    /* FIXME: Should chain to GdiSignalProc now. */

    return 0;
}

/***********************************************************************
 *           USER_DllEntryPoint  (USER.374)
 */
BOOL WINAPI USER_DllEntryPoint( DWORD dwReason, HINSTANCE hInstDLL, WORD ds,
                                WORD wHeapSize, DWORD dwReserved1, WORD wReserved2 )
{
    switch ( dwReason )
    {
    case DLL_PROCESS_ATTACH:
        /* 
         * We need to load the 32-bit library so as to be able
         * to access the system resources stored there!
         */
        if ( !LoadLibraryA("USER32.DLL") )
        {
            ERR_(win)( "Could not load USER32.DLL\n" );
            return FALSE;
        }
    }

    return TRUE;
}


/***********************************************************************
 *           ExitWindows16   (USER.7)
 */
BOOL16 WINAPI ExitWindows16( DWORD dwReturnCode, UINT16 wReserved )
{
    return ExitWindowsEx( EWX_LOGOFF, 0xffffffff );
}


/***********************************************************************
 *           ExitWindowsExec16   (USER.246)
 */
BOOL16 WINAPI ExitWindowsExec16( LPCSTR lpszExe, LPCSTR lpszParams )
{
    TRACE_(system)("Should run the following in DOS-mode: \"%s %s\"\n",
	lpszExe, lpszParams);
    return ExitWindowsEx( EWX_LOGOFF, 0xffffffff );
}


/***********************************************************************
 *           ExitWindowsEx   (USER32.196)
 */
BOOL WINAPI ExitWindowsEx( UINT flags, DWORD reserved )
{
    int i;
    BOOL result;
    WND **list, **ppWnd;
        
    /* We have to build a list of all windows first, as in EnumWindows */

    if (!(list = WIN_BuildWinArray( WIN_GetDesktop(), 0, NULL )))
    {
        WIN_ReleaseDesktop();
        return FALSE;
    }

    /* Send a WM_QUERYENDSESSION message to every window */

    for (ppWnd = list, i = 0; *ppWnd; ppWnd++, i++)
    {
        /* Make sure that the window still exists */
        if (!IsWindow( (*ppWnd)->hwndSelf )) continue;
	if (!SendMessage16( (*ppWnd)->hwndSelf, WM_QUERYENDSESSION, 0, 0 ))
            break;
    }
    result = !(*ppWnd);

    /* Now notify all windows that got a WM_QUERYENDSESSION of the result */

    for (ppWnd = list; i > 0; i--, ppWnd++)
    {
        if (!IsWindow( (*ppWnd)->hwndSelf )) continue;
	SendMessage16( (*ppWnd)->hwndSelf, WM_ENDSESSION, result, 0 );
    }
    WIN_ReleaseWinArray(list);

    if (result) ExitKernel16();
    WIN_ReleaseDesktop();
    return FALSE;
}

static void _dump_CDS_flags(DWORD flags) {
#define X(x) if (flags & CDS_##x) MESSAGE(""#x ",");
	X(UPDATEREGISTRY);X(TEST);X(FULLSCREEN);X(GLOBAL);
	X(SET_PRIMARY);X(RESET);X(SETRECT);X(NORESET);
#undef X
}

/***********************************************************************
 *           ChangeDisplaySettingsA    (USER32.589)
 */
LONG WINAPI ChangeDisplaySettingsA( LPDEVMODEA devmode, DWORD flags )
{
  FIXME_(system)("(%p,0x%08lx), stub\n",devmode,flags);
  MESSAGE("\tflags=");_dump_CDS_flags(flags);MESSAGE("\n");
  if (devmode==NULL)
    FIXME_(system)("   devmode=NULL (return to default mode)\n");
  else if ( (devmode->dmBitsPerPel != GetSystemMetrics(SM_WINE_BPP)) 
	    || (devmode->dmPelsHeight != GetSystemMetrics(SM_CYSCREEN))
	    || (devmode->dmPelsWidth != GetSystemMetrics(SM_CXSCREEN)) )

  {

    if (devmode->dmFields & DM_BITSPERPEL)
      FIXME_(system)("   bpp=%ld\n",devmode->dmBitsPerPel);
    if (devmode->dmFields & DM_PELSWIDTH)
      FIXME_(system)("   width=%ld\n",devmode->dmPelsWidth);
    if (devmode->dmFields & DM_PELSHEIGHT)
      FIXME_(system)("   height=%ld\n",devmode->dmPelsHeight);
    FIXME_(system)(" (Putting X in this mode beforehand might help)\n"); 
    /* we don't, but the program ... does not need to know */
    return DISP_CHANGE_SUCCESSFUL; 
  }
  return DISP_CHANGE_SUCCESSFUL;
}

/***********************************************************************
 *           ChangeDisplaySettingsExA    (USER32.604)
 */
LONG WINAPI ChangeDisplaySettingsExA( 
	LPCSTR devname, LPDEVMODEA devmode, HWND hwnd, DWORD flags,
	LPARAM lparam
) {
  FIXME_(system)("(%s,%p,0x%04x,0x%08lx,0x%08lx), stub\n",devname,devmode,hwnd,flags,lparam);
  MESSAGE("\tflags=");_dump_CDS_flags(flags);MESSAGE("\n");
  if (devmode==NULL)
    FIXME_(system)("   devmode=NULL (return to default mode)\n");
  else if ( (devmode->dmBitsPerPel != GetSystemMetrics(SM_WINE_BPP))
	    || (devmode->dmPelsHeight != GetSystemMetrics(SM_CYSCREEN))
	    || (devmode->dmPelsWidth != GetSystemMetrics(SM_CXSCREEN)) )

  {

    if (devmode->dmFields & DM_BITSPERPEL)
      FIXME_(system)("   bpp=%ld\n",devmode->dmBitsPerPel);
    if (devmode->dmFields & DM_PELSWIDTH)
      FIXME_(system)("   width=%ld\n",devmode->dmPelsWidth);
    if (devmode->dmFields & DM_PELSHEIGHT)
      FIXME_(system)("   height=%ld\n",devmode->dmPelsHeight);
    FIXME_(system)(" (Putting X in this mode beforehand might help)\n"); 
    /* we don't, but the program ... does not need to know */
    return DISP_CHANGE_SUCCESSFUL; 
  }
  return DISP_CHANGE_SUCCESSFUL;
}

/***********************************************************************
 *           EnumDisplaySettingsA   (USER32.592)
 * FIXME: Currently uses static list of modes.
 *
 * RETURNS
 *	TRUE if nth setting exists found (described in the LPDEVMODEA struct)
 *	FALSE if we do not have the nth setting
 */
BOOL WINAPI EnumDisplaySettingsA(
	LPCSTR name,		/* [in] huh? */
	DWORD n,		/* [in] nth entry in display settings list*/
	LPDEVMODEA devmode	/* [out] devmode for that setting */
) {
#define NRMODES 5
#define NRDEPTHS 4
	struct {
		int w,h;
	} modes[NRMODES]={{512,384},{640,400},{640,480},{800,600},{1024,768}};
	int depths[4] = {8,16,24,32};

	TRACE_(system)("(%s,%ld,%p)\n",name,n,devmode);
	if (n==0) {
		devmode->dmBitsPerPel = GetSystemMetrics(SM_WINE_BPP);
		devmode->dmPelsHeight = GetSystemMetrics(SM_CYSCREEN);
		devmode->dmPelsWidth  = GetSystemMetrics(SM_CXSCREEN);
		return TRUE;
	}
	if ((n-1)<NRMODES*NRDEPTHS) {
		devmode->dmBitsPerPel	= depths[(n-1)/NRMODES];
		devmode->dmPelsHeight	= modes[(n-1)%NRMODES].h;
		devmode->dmPelsWidth	= modes[(n-1)%NRMODES].w;
		return TRUE;
	}
	return FALSE;
}

/***********************************************************************
 *           EnumDisplaySettingsW   (USER32.593)
 */
BOOL WINAPI EnumDisplaySettingsW(LPCWSTR name,DWORD n,LPDEVMODEW devmode) {
	LPSTR nameA = HEAP_strdupWtoA(GetProcessHeap(),0,name);
	DEVMODEA	devmodeA; 
	BOOL ret = EnumDisplaySettingsA(nameA,n,&devmodeA); 

	if (ret) {
		devmode->dmBitsPerPel	= devmodeA.dmBitsPerPel;
		devmode->dmPelsHeight	= devmodeA.dmPelsHeight;
		devmode->dmPelsWidth	= devmodeA.dmPelsWidth;
		/* FIXME: convert rest too, if they are ever returned */
	}
	HeapFree(GetProcessHeap(),0,nameA);
	return ret;
}

/***********************************************************************
 *           EnumDisplayDevicesA   (USER32.184)
 */
BOOL WINAPI EnumDisplayDevicesA(
	LPVOID unused,DWORD i,LPDISPLAY_DEVICEA lpDisplayDevice,DWORD dwFlags
) {
	if (i)
		return FALSE;
	FIXME_(system)("(%p,%ld,%p,0x%08lx), stub!\n",unused,i,lpDisplayDevice,dwFlags);
	strcpy(lpDisplayDevice->DeviceName,"X11");
	strcpy(lpDisplayDevice->DeviceString,"X 11 Windowing System");
	lpDisplayDevice->StateFlags =
			DISPLAY_DEVICE_ATTACHED_TO_DESKTOP	|
			DISPLAY_DEVICE_PRIMARY_DEVICE		|
			DISPLAY_DEVICE_VGA_COMPATIBLE;
	return TRUE;
}

/***********************************************************************
 *           EnumDisplayDevicesW   (USER32.185)
 */
BOOL WINAPI EnumDisplayDevicesW(
	LPVOID unused,DWORD i,LPDISPLAY_DEVICEW lpDisplayDevice,DWORD dwFlags
) {
	if (i)
		return FALSE;
	FIXME_(system)("(%p,%ld,%p,0x%08lx), stub!\n",unused,i,lpDisplayDevice,dwFlags);
        MultiByteToWideChar( CP_ACP, 0, "X11", -1, lpDisplayDevice->DeviceName,
                             sizeof(lpDisplayDevice->DeviceName)/sizeof(WCHAR) );
        MultiByteToWideChar( CP_ACP, 0, "X11 Windowing System", -1, lpDisplayDevice->DeviceString,
                             sizeof(lpDisplayDevice->DeviceString)/sizeof(WCHAR) );
	lpDisplayDevice->StateFlags =
			DISPLAY_DEVICE_ATTACHED_TO_DESKTOP	|
			DISPLAY_DEVICE_PRIMARY_DEVICE		|
			DISPLAY_DEVICE_VGA_COMPATIBLE;
	return TRUE;
}

/***********************************************************************
 *           SetEventHook   (USER.321)
 *
 *	Used by Turbo Debugger for Windows
 */
FARPROC16 WINAPI SetEventHook16(FARPROC16 lpfnEventHook)
{
	FIXME_(hook)("(lpfnEventHook=%08x): stub\n", (UINT)lpfnEventHook);
	return NULL;
}

/***********************************************************************
 *           UserSeeUserDo   (USER.216)
 */
DWORD WINAPI UserSeeUserDo16(WORD wReqType, WORD wParam1, WORD wParam2, WORD wParam3)
{
    switch (wReqType)
    {
    case USUD_LOCALALLOC:
        return LOCAL_Alloc(USER_HeapSel, wParam1, wParam3);
    case USUD_LOCALFREE:
        return LOCAL_Free(USER_HeapSel, wParam1);
    case USUD_LOCALCOMPACT:
        return LOCAL_Compact(USER_HeapSel, wParam3, 0);
    case USUD_LOCALHEAP:
        return USER_HeapSel;
    case USUD_FIRSTCLASS:
        FIXME_(local)("return a pointer to the first window class.\n"); 
        return (DWORD)-1;
    default:
        WARN_(local)("wReqType %04x (unknown)", wReqType);
        return (DWORD)-1;
    }
}

/***********************************************************************
 *         GetSystemDebugState16   (USER.231)
 */
WORD WINAPI GetSystemDebugState16(void)
{
    return 0;  /* FIXME */
}

/***********************************************************************
 *           RegisterLogonProcess   (USER32.434)
 */
DWORD WINAPI RegisterLogonProcess(HANDLE hprocess,BOOL x) {
	FIXME_(win32)("(%d,%d),stub!\n",hprocess,x);
	return 1;
}

/***********************************************************************
 *           CreateWindowStationW   (USER32.86)
 */
HWINSTA WINAPI CreateWindowStationW(
	LPWSTR winstation,DWORD res1,DWORD desiredaccess,
	LPSECURITY_ATTRIBUTES lpsa
) {
	FIXME_(win32)("(%s,0x%08lx,0x%08lx,%p),stub!\n",debugstr_w(winstation),
		res1,desiredaccess,lpsa
	);
	return 0xdeadcafe;
}

/***********************************************************************
 *           SetProcessWindowStation   (USER32.496)
 */
BOOL WINAPI SetProcessWindowStation(HWINSTA hWinSta) {
	FIXME_(win32)("(%d),stub!\n",hWinSta);
	return TRUE;
}

/***********************************************************************
 *           SetUserObjectSecurity   (USER32.514)
 */
BOOL WINAPI SetUserObjectSecurity(
	HANDLE hObj,
	PSECURITY_INFORMATION pSIRequested,
	PSECURITY_DESCRIPTOR pSID
) {
	FIXME_(win32)("(0x%08x,%p,%p),stub!\n",hObj,pSIRequested,pSID);
	return TRUE;
}

/***********************************************************************
 *           CreateDesktopA   (USER32.68)
 */
HDESK WINAPI CreateDesktopA(
	LPSTR lpszDesktop,LPSTR lpszDevice,LPDEVMODEA pDevmode,
	DWORD dwFlags,DWORD dwDesiredAccess,LPSECURITY_ATTRIBUTES lpsa
) {
	FIXME_(win32)("(%s,%s,%p,0x%08lx,0x%08lx,%p),stub!\n",
		lpszDesktop,lpszDevice,pDevmode,
		dwFlags,dwDesiredAccess,lpsa
	);
	return 0xcafedead;
}

/***********************************************************************
 *           CreateDesktopW   (USER32.69)
 */
HDESK WINAPI CreateDesktopW(
	LPWSTR lpszDesktop,LPWSTR lpszDevice,LPDEVMODEW pDevmode,
	DWORD dwFlags,DWORD dwDesiredAccess,LPSECURITY_ATTRIBUTES lpsa
) {
	FIXME_(win32)("(%s,%s,%p,0x%08lx,0x%08lx,%p),stub!\n",
		debugstr_w(lpszDesktop),debugstr_w(lpszDevice),pDevmode,
		dwFlags,dwDesiredAccess,lpsa
	);
	return 0xcafedead;
}

/***********************************************************************
 *           EnumDesktopWindows   (USER32.591)
 */
BOOL WINAPI EnumDesktopWindows( HDESK hDesktop, WNDENUMPROC lpfn, LPARAM lParam ) {
  FIXME_(win32)("(0x%08x, %p, 0x%08lx), stub!\n", hDesktop, lpfn, lParam );
  return TRUE;
}


/***********************************************************************
 *           CloseWindowStation
 */
BOOL WINAPI CloseWindowStation(HWINSTA hWinSta)
{
    FIXME_(win32)("(0x%08x)\n", hWinSta);
    return TRUE;
}

/***********************************************************************
 *           CloseDesktop
 */
BOOL WINAPI CloseDesktop(HDESK hDesk)
{
    FIXME_(win32)("(0x%08x)\n", hDesk);
    return TRUE;
}

/***********************************************************************
 *           SetWindowStationUser   (USER32.521)
 */
DWORD WINAPI SetWindowStationUser(DWORD x1,DWORD x2) {
	FIXME_(win32)("(0x%08lx,0x%08lx),stub!\n",x1,x2);
	return 1;
}

/***********************************************************************
 *           SetLogonNotifyWindow   (USER32.486)
 */
DWORD WINAPI SetLogonNotifyWindow(HWINSTA hwinsta,HWND hwnd) {
	FIXME_(win32)("(0x%x,%04x),stub!\n",hwinsta,hwnd);
	return 1;
}

/***********************************************************************
 *           LoadLocalFonts   (USER32.486)
 */
VOID WINAPI LoadLocalFonts(VOID) {
	/* are loaded. */
	return;
}
/***********************************************************************
 *           GetUserObjectInformationA   (USER32.299)
 */
BOOL WINAPI GetUserObjectInformationA( HANDLE hObj, INT nIndex, LPVOID pvInfo, DWORD nLength, LPDWORD lpnLen )
{	FIXME_(win32)("(0x%x %i %p %ld %p),stub!\n", hObj, nIndex, pvInfo, nLength, lpnLen );
	return TRUE;
}
/***********************************************************************
 *           GetUserObjectInformationW   (USER32.300)
 */
BOOL WINAPI GetUserObjectInformationW( HANDLE hObj, INT nIndex, LPVOID pvInfo, DWORD nLength, LPDWORD lpnLen )
{	FIXME_(win32)("(0x%x %i %p %ld %p),stub!\n", hObj, nIndex, pvInfo, nLength, lpnLen );
	return TRUE;
}
/***********************************************************************
 *           GetUserObjectSecurity   (USER32.300)
 */
BOOL WINAPI GetUserObjectSecurity(HANDLE hObj, PSECURITY_INFORMATION pSIRequested,
	PSECURITY_DESCRIPTOR pSID, DWORD nLength, LPDWORD lpnLengthNeeded)
{	FIXME_(win32)("(0x%x %p %p len=%ld %p),stub!\n",  hObj, pSIRequested, pSID, nLength, lpnLengthNeeded);
	return TRUE;
}

/***********************************************************************
 *           SetSystemCursor   (USER32.507)
 */
BOOL WINAPI SetSystemCursor(HCURSOR hcur, DWORD id)
{	FIXME_(win32)("(%08x,%08lx),stub!\n",  hcur, id);
	return TRUE;
}

/***********************************************************************
 *	RegisterSystemThread	(USER32.435)
 */
void WINAPI RegisterSystemThread(DWORD flags, DWORD reserved)
{
	FIXME_(win32)("(%08lx, %08lx)\n", flags, reserved);
}

/***********************************************************************
 *	RegisterDeviceNotificationA	(USER32.477)
 */
HDEVNOTIFY WINAPI RegisterDeviceNotificationA(
	HANDLE hnd, LPVOID notifyfilter, DWORD flags
) {
	FIXME_(win32)("(hwnd=%08x, filter=%p,flags=0x%08lx), STUB!\n",
		hnd,notifyfilter,flags
	);
	return 0;
}
