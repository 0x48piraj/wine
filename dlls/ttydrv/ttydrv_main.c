/*
 * TTYDRV initialization code
 */

#include "config.h"

#include <stdio.h>

#include "winbase.h"
#include "wine/winbase16.h"
#include "gdi.h"
#include "user.h"
#include "win.h"
#include "debugtools.h"
#include "ttydrv.h"

DEFAULT_DEBUG_CHANNEL(ttydrv);

int cell_width = 8;
int cell_height = 8;
int screen_rows = 50;  /* default value */
int screen_cols = 80;  /* default value */
WINDOW *root_window;


/***********************************************************************
 *           TTYDRV process initialisation routine
 */
static void process_attach(void)
{
#ifdef WINE_CURSES
    if ((root_window = initscr()))
    {
        werase(root_window);
        wrefresh(root_window);
    }
    getmaxyx(root_window, screen_rows, screen_cols);
#endif  /* WINE_CURSES */

    TTYDRV_GDI_Initialize();

    /* load display.dll */
    LoadLibrary16( "display" );
}


/***********************************************************************
 *           TTYDRV process termination routine
 */
static void process_detach(void)
{
    TTYDRV_GDI_Finalize();

#ifdef WINE_CURSES
    if (root_window) endwin();
#endif  /* WINE_CURSES */
}


/***********************************************************************
 *           TTYDRV initialisation routine
 */
BOOL WINAPI TTYDRV_Init( HINSTANCE hinst, DWORD reason, LPVOID reserved )
{
    switch(reason)
    {
    case DLL_PROCESS_ATTACH:
        process_attach();
        break;

    case DLL_PROCESS_DETACH:
        process_detach();
        break;
    }
    return TRUE;
}
