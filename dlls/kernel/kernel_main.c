/*
 * Kernel initialization code
 *
 * Copyright 2000 Alexandre Julliard
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

#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <sys/stat.h>
#include <signal.h>

#include "winbase.h"
#include "wincon.h"
#include "ntddk.h"

#include "wine/winbase16.h"
#include "wine/library.h"
#include "file.h"
#include "global.h"
#include "miscemu.h"
#include "module.h"
#include "task.h"

extern void LOCALE_Init(void);
extern BOOL RELAY_Init(void);

extern  int __wine_set_signal_handler(unsigned, int (*)(unsigned));
extern  int CONSOLE_HandleCtrlC(unsigned);

extern int main_create_flags;

/***********************************************************************
 *           KERNEL process initialisation routine
 */
static BOOL process_attach(void)
{
    HMODULE16 hModule;

    /* Get the umask */
    FILE_umask = umask(0777);
    umask( FILE_umask );

    /* Setup codepage info */
    LOCALE_Init();

    /* Initialize relay entry points */
    if (!RELAY_Init()) return FALSE;

    /* Initialize DOS memory */
    if (!DOSMEM_Init(0)) return FALSE;

    if ((hModule = LoadLibrary16( "krnl386.exe" )) < 32) return FALSE;

    /* Initialize special KERNEL entry points */

    /* Initialize KERNEL.178 (__WINFLAGS) with the correct flags value */
    NE_SetEntryPoint( hModule, 178, GetWinFlags16() );

    /* Initialize KERNEL.454/455 (__FLATCS/__FLATDS) */
    NE_SetEntryPoint( hModule, 454, wine_get_cs() );
    NE_SetEntryPoint( hModule, 455, wine_get_ds() );

    /* Initialize KERNEL.THHOOK */
    TASK_InstallTHHook(MapSL((SEGPTR)GetProcAddress16( hModule, (LPCSTR)332 )));

    /* Initialize the real-mode selector entry points */
#define SET_ENTRY_POINT( num, addr ) \
    NE_SetEntryPoint( hModule, (num), GLOBAL_CreateBlock( GMEM_FIXED, \
                      DOSMEM_MapDosToLinear(addr), 0x10000, hModule, \
                      WINE_LDT_FLAGS_DATA ))

    SET_ENTRY_POINT( 174, 0xa0000 );  /* KERNEL.174: __A000H */
    SET_ENTRY_POINT( 181, 0xb0000 );  /* KERNEL.181: __B000H */
    SET_ENTRY_POINT( 182, 0xb8000 );  /* KERNEL.182: __B800H */
    SET_ENTRY_POINT( 195, 0xc0000 );  /* KERNEL.195: __C000H */
    SET_ENTRY_POINT( 179, 0xd0000 );  /* KERNEL.179: __D000H */
    SET_ENTRY_POINT( 190, 0xe0000 );  /* KERNEL.190: __E000H */
    NE_SetEntryPoint( hModule, 183, DOSMEM_0000H );       /* KERNEL.183: __0000H */
    NE_SetEntryPoint( hModule, 173, DOSMEM_BiosSysSeg );  /* KERNEL.173: __ROMBIOS */
    NE_SetEntryPoint( hModule, 193, DOSMEM_BiosDataSeg ); /* KERNEL.193: __0040H */
    NE_SetEntryPoint( hModule, 194, DOSMEM_BiosSysSeg );  /* KERNEL.194: __F000H */
#undef SET_ENTRY_POINT

    /* Force loading of some dlls */
    if (LoadLibrary16( "system" ) < 32) return FALSE;

    /* Create 16-bit task */
    TASK_CreateMainTask();

    /* Create the shared heap for broken win95 native dlls */
    HeapCreate( HEAP_SHARED, 0, 0 );

    /* finish the process initialisation, if needed */
    __wine_set_signal_handler(SIGINT, CONSOLE_HandleCtrlC);

    if (main_create_flags & CREATE_NEW_CONSOLE)
    {
        HMODULE mod = GetModuleHandleA(0);
        if (RtlImageNtHeader(mod)->OptionalHeader.Subsystem == IMAGE_SUBSYSTEM_WINDOWS_CUI)
            AllocConsole();
    }

    return TRUE;
}

/***********************************************************************
 *           KERNEL initialisation routine
 */
BOOL WINAPI MAIN_KernelInit( HINSTANCE hinst, DWORD reason, LPVOID reserved )
{
    switch(reason)
    {
    case DLL_PROCESS_ATTACH:
        return process_attach();
    case DLL_PROCESS_DETACH:
        WriteOutProfiles16();
        break;
    }
    return TRUE;
}

/***********************************************************************
 *		EnableDos (KERNEL.41)
 *		DisableDos (KERNEL.42)
 *		GetLastDiskChange (KERNEL.98)
 *		ValidateCodeSegments (KERNEL.100)
 *		KbdRst (KERNEL.123)
 *		EnableKernel (KERNEL.124)
 *		DisableKernel (KERNEL.125)
 *		ValidateFreeSpaces (KERNEL.200)
 *		K237 (KERNEL.237)
 *		BUNNY_351 (KERNEL.351)
 *		PIGLET_361 (KERNEL.361)
 *
 * Entry point for kernel functions that do nothing.
 */
LONG WINAPI KERNEL_nop(void)
{
    return 0;
}

BOOL WINAPI SwitchToThread(VOID)
{
    Sleep(0);
    return 1;
}
