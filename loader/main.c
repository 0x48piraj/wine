/*
 * Main initialization code
 */

#include <locale.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#ifdef MALLOC_DEBUGGING
# include <malloc.h>
#endif
#include "windef.h"
#include "wine/winbase16.h"
#include "drive.h"
#include "file.h"
#include "options.h"
#include "debugtools.h"
#include "server.h"
#include "loadorder.h"

DEFAULT_DEBUG_CHANNEL(server);

extern void SHELL_LoadRegistry(void);

/***********************************************************************
 *           Main initialisation routine
 */
BOOL MAIN_MainInit(void)
{
#ifdef MALLOC_DEBUGGING
    char *trace;

    mcheck(NULL);
    if (!(trace = getenv("MALLOC_TRACE")))
        MESSAGE( "MALLOC_TRACE not set. No trace generated\n" );
    else
    {
        MESSAGE( "malloc trace goes to %s\n", trace );
        mtrace();
    }
#endif
    setbuf(stdout,NULL);
    setbuf(stderr,NULL);
    setlocale(LC_CTYPE,"");

    /* Load the configuration file */
    if (!PROFILE_LoadWineIni()) return FALSE;

    /* Initialise DOS drives */
    if (!DRIVE_Init()) return FALSE;

    /* Initialise DOS directories */
    if (!DIR_Init()) return FALSE;

    /* Registry initialisation */
    SHELL_LoadRegistry();
    
    /* Global boot finished, the rest is process-local */
    CLIENT_BootDone( TRACE_ON(server) );

    /* Initialize module loadorder */
    if (!MODULE_InitLoadOrder()) return FALSE;

    return TRUE;
}


/***********************************************************************
 *           ExitKernel16 (KERNEL.2)
 *
 * Clean-up everything and exit the Wine process.
 *
 */
void WINAPI ExitKernel16( void )
{
    /* Do the clean-up stuff */

    WriteOutProfiles16();
    TerminateProcess( GetCurrentProcess(), 0 );
}

