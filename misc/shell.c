/*
 * 				Shell Library Functions
 *
 *  1998 Marcus Meissner
 *  1998 Juergen Schmied (jsch)
 *  currently work in progress on SH* and SHELL32_DllGetClassObject functions
 *  <contact juergen.schmied@metronet.de 980624>
 */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include "windows.h"
#include "winerror.h"
#include "file.h"
#include "shell.h"
#include "heap.h"
#include "module.h"
#include "neexe.h"
#include "resource.h"
#include "dlgs.h"
#include "win.h"
#include "graphics.h"
#include "cursoricon.h"
#include "interfaces.h"
#include "sysmetrics.h"
#include "shlobj.h"
#include "debug.h"
#include "winreg.h"
#include "imagelist.h"
#include "commctrl.h"

/* FIXME should be moved to a header file. IsEqualGUID 
is declared but not exported in compobj.c !!!*/
#define IsEqualGUID(rguid1, rguid2) (!memcmp(rguid1, rguid2, sizeof(GUID)))
#define IsEqualIID(riid1, riid2) IsEqualGUID(riid1, riid2)
#define IsEqualCLSID(rclsid1, rclsid2) IsEqualGUID(rclsid1, rclsid2)

static const char * const SHELL_People[] =
{
    "Bob Amstadt",
    "Dag Asheim",
    "Martin Ayotte",
    "Karl Backstr�m",
    "Peter Bajusz",
    "Marcel Baur",
    "Georg Beyerle",
    "Ross Biro",
    "Martin Boehme",
    "Uwe Bonnes",
    "Erik Bos",
    "Fons Botman",
    "John Brezak",
    "Andrew Bulhak",
    "John Burton",
    "Niels de Carpentier",
    "Gordon Chaffee",
    "Jimen Ching",
    "Pascal Cuoq",
    "David A. Cuthbert",
    "Huw D. M. Davies",
    "Roman Dolejsi",
    "Frans van Dorsselaer",
    "Chris Faherty",
    "Carsten Fallesen",
    "Paul Falstad",
    "David Faure",
    "Claus Fischer",
    "Olaf Flebbe",
    "Chad Fraleigh",
    "Matthew Francis",
    "Peter Galbavy",
    "Ramon Garcia",
    "Matthew Ghio",
    "Jody Goldberg",
    "Hans de Graaff",
    "Charles M. Hannum",
    "Adrian Harvey",
    "John Harvey",
    "Bill Hawes",
    "Cameron Heide",
    "Jochen Hoenicke",
    "Onno Hovers",
    "Jeffrey Hsu",
    "Miguel de Icaza",
    "Jukka Iivonen",
    "Lee Jaekil",
    "Alexandre Julliard",
    "Bang Jun-Young",
    "Pavel Kankovsky",
    "Jochen Karrer",
    "Andreas Kirschbaum",
    "Rein Klazes",
    "Albrecht Kleine",
    "Eric Kohl",
    "Jon Konrath",
    "Alex Korobka",
    "Greg Kreider",
    "Anand Kumria",
    "Ove K�ven",
    "Scott A. Laird",
    "David Lee Lambert",
    "Andrew Lewycky",
    "Per Lindstr�m",
    "Martin von Loewis",
    "Michiel van Loon",
    "Kenneth MacDonald",
    "Peter MacDonald",
    "William Magro",
    "Juergen Marquardt",
    "Ricardo Massaro",
    "Marcus Meissner",
    "Graham Menhennitt",
    "David Metcalfe",
    "Bruce Milner",
    "Steffen Moeller",
    "Andreas Mohr",
    "James Moody",
    "Philippe De Muyter",
    "Itai Nahshon",
    "Kristian Nielsen",
    "Henrik Olsen",
    "Michael Patra",
    "Dimitrie O. Paun",
    "Jim Peterson",
    "Robert Pouliot",
    "Keith Reynolds",
    "Slaven Rezic",
    "John Richardson",
    "Rick Richardson",
    "Doug Ridgway",
    "Bernhard Rosenkraenzer",
    "Johannes Ruscheinski",
    "Thomas Sandford",
    "Constantine Sapuntzakis",
    "Pablo Saratxaga",
    "Daniel Schepler",
    "Peter Schlaile",
    "Ulrich Schmid",
    "Bernd Schmidt",
    "Juergen Schmied",
    "Ingo Schneider",
    "Victor Schneider",
    "Yngvi Sigurjonsson",
    "Stephen Simmons",
    "Rick Sladkey",
    "William Smith",
    "Dominik Strasser",
    "Vadim Strizhevsky",
    "Bertho Stultiens",
    "Erik Svendsen",
    "Tristan Tarrant",
    "Andrew Taylor",
    "Duncan C Thomson",
    "Goran Thyni",
    "Jimmy Tirtawangsa",
    "Jon Tombs",
    "Linus Torvalds",
    "Gregory Trubetskoy",
    "Petri Tuomola",
    "Michael Veksler",
    "Sven Verdoolaege",
    "Ronan Waide",
    "Eric Warnke",
    "Manfred Weichel",
    "Ulrich Weigand",
    "Morten Welinder",
    "Len White",
    "Lawson Whitney",
    "Jan Willamowius",
    "Carl Williams",
    "Karl Guenter Wuensch",
    "Eric Youngdale",
    "James Youngman",
    "Nikita V. Youshchenko",
    "Mikolaj Zalewski",
    "John Zero",
    "Luiz Otavio L. Zorzella",
    NULL
};


/* .ICO file ICONDIR definitions */

#pragma pack(1)

typedef struct
{
    BYTE        bWidth;          /* Width, in pixels, of the image	*/
    BYTE        bHeight;         /* Height, in pixels, of the image	*/
    BYTE        bColorCount;     /* Number of colors in image (0 if >=8bpp) */
    BYTE        bReserved;       /* Reserved ( must be 0)		*/
    WORD        wPlanes;         /* Color Planes			*/
    WORD        wBitCount;       /* Bits per pixel			*/
    DWORD       dwBytesInRes;    /* How many bytes in this resource?	*/
    DWORD       dwImageOffset;   /* Where in the file is this image?	*/
} icoICONDIRENTRY, *LPicoICONDIRENTRY;

typedef struct
{
    WORD            idReserved;   /* Reserved (must be 0)		*/
    WORD            idType;       /* Resource Type (1 for icons)	*/
    WORD            idCount;      /* How many images?			*/
    icoICONDIRENTRY idEntries[1]; /* An entry for each image (idCount of 'em) */
} icoICONDIR, *LPicoICONDIR;

#pragma pack(4)

static const char*	lpstrMsgWndCreated = "OTHERWINDOWCREATED";
static const char*	lpstrMsgWndDestroyed = "OTHERWINDOWDESTROYED";
static const char*	lpstrMsgShellActivate = "ACTIVATESHELLWINDOW";

static HWND16	SHELL_hWnd = 0;
static HHOOK	SHELL_hHook = 0;
static UINT16	uMsgWndCreated = 0;
static UINT16	uMsgWndDestroyed = 0;
static UINT16	uMsgShellActivate = 0;

/*************************************************************************
 *				DragAcceptFiles		[SHELL.9]
 */
void WINAPI DragAcceptFiles(HWND16 hWnd, BOOL16 b)
{ WND* wnd = WIN_FindWndPtr(hWnd);

    if( wnd )
	wnd->dwExStyle = b? wnd->dwExStyle | WS_EX_ACCEPTFILES
			  : wnd->dwExStyle & ~WS_EX_ACCEPTFILES;
}


/*************************************************************************
 *				DragQueryFile		[SHELL.11]
 */
UINT16 WINAPI DragQueryFile(HDROP16 hDrop, WORD wFile, LPSTR lpszFile,
                            WORD wLength)
{ /* hDrop is a global memory block allocated with GMEM_SHARE 
     * with DROPFILESTRUCT as a header and filenames following
     * it, zero length filename is in the end */       
    
    LPDROPFILESTRUCT lpDropFileStruct;
    LPSTR lpCurrent;
    WORD  i;
    
  TRACE(shell,"(%04x, %i, %p, %u)\n",
		hDrop,wFile,lpszFile,wLength);
    
    lpDropFileStruct = (LPDROPFILESTRUCT) GlobalLock16(hDrop); 
  if(!lpDropFileStruct)
    return 0;

    lpCurrent = (LPSTR) lpDropFileStruct + lpDropFileStruct->wSize;
    
    i = 0;
    while (i++ < wFile)
  { while (*lpCurrent++);  /* skip filename */
	if (!*lpCurrent) 
	    return (wFile == 0xFFFF) ? i : 0;  
    }
    
    i = strlen(lpCurrent); 
  if (!lpszFile)
    return i+1;   /* needed buffer size */
    
    i = (wLength > i) ? i : wLength-1;
    strncpy(lpszFile, lpCurrent, i);
    lpszFile[i] = '\0';

    GlobalUnlock16(hDrop);
    return i;
}


/*************************************************************************
 *				DragFinish		[SHELL.12]
 */
void WINAPI DragFinish(HDROP16 h)
{ TRACE(shell,"\n");
    GlobalFree16((HGLOBAL16)h);
}


/*************************************************************************
 *				DragQueryPoint		[SHELL.13]
 */
BOOL16 WINAPI DragQueryPoint(HDROP16 hDrop, POINT16 *p)
{ LPDROPFILESTRUCT lpDropFileStruct;  
    BOOL16           bRet;
  TRACE(shell,"\n");
    lpDropFileStruct = (LPDROPFILESTRUCT) GlobalLock16(hDrop);

    memcpy(p,&lpDropFileStruct->ptMousePos,sizeof(POINT16));
    bRet = lpDropFileStruct->fInNonClientArea;

    GlobalUnlock16(hDrop);
    return bRet;
}

/*************************************************************************
 *	SHELL_FindExecutable [Internal]
 *
 * Utility for code sharing between FindExecutable and ShellExecute
 */
static HINSTANCE32 SHELL_FindExecutable( LPCSTR lpFile, 
                                         LPCSTR lpOperation,
                                         LPSTR lpResult)
{ char *extension = NULL; /* pointer to file extension */
    char tmpext[5];         /* local copy to mung as we please */
    char filetype[256];     /* registry name for this filetype */
    LONG filetypelen=256;   /* length of above */
    char command[256];      /* command from registry */
    LONG commandlen=256;    /* This is the most DOS can handle :) */
    char buffer[256];       /* Used to GetProfileString */
    HINSTANCE32 retval=31;  /* default - 'No association was found' */
    char *tok;              /* token pointer */
    int i;                  /* random counter */
    char xlpFile[256];      /* result of SearchPath */

  TRACE(shell, "%s\n", (lpFile != NULL?lpFile:"-") );

    lpResult[0]='\0'; /* Start off with an empty return string */

    /* trap NULL parameters on entry */
    if (( lpFile == NULL ) || ( lpResult == NULL ) || ( lpOperation == NULL ))
  { WARN(exec, "(lpFile=%s,lpResult=%s,lpOperation=%s): NULL parameter\n",
           lpFile, lpOperation, lpResult);
        return 2; /* File not found. Close enough, I guess. */
    }

    if (SearchPath32A( NULL, lpFile,".exe",sizeof(xlpFile),xlpFile,NULL))
  { TRACE(shell, "SearchPath32A returned non-zero\n");
        lpFile = xlpFile;
    }

    /* First thing we need is the file's extension */
    extension = strrchr( xlpFile, '.' ); /* Assume last "." is the one; */
					/* File->Run in progman uses */
					/* .\FILE.EXE :( */
  TRACE(shell, "xlpFile=%s,extension=%s\n", xlpFile, extension);

    if ((extension == NULL) || (extension == &xlpFile[strlen(xlpFile)]))
  { WARN(shell, "Returning 31 - No association\n");
        return 31; /* no association */
    }

    /* Make local copy & lowercase it for reg & 'programs=' lookup */
    lstrcpyn32A( tmpext, extension, 5 );
    CharLower32A( tmpext );
  TRACE(shell, "%s file\n", tmpext);
    
    /* Three places to check: */
    /* 1. win.ini, [windows], programs (NB no leading '.') */
    /* 2. Registry, HKEY_CLASS_ROOT\<filetype>\shell\open\command */
    /* 3. win.ini, [extensions], extension (NB no leading '.' */
    /* All I know of the order is that registry is checked before */
    /* extensions; however, it'd make sense to check the programs */
    /* section first, so that's what happens here. */

    /* See if it's a program - if GetProfileString fails, we skip this
     * section. Actually, if GetProfileString fails, we've probably
     * got a lot more to worry about than running a program... */
    if ( GetProfileString32A("windows", "programs", "exe pif bat com",
						  buffer, sizeof(buffer)) > 0 )
  { for (i=0;i<strlen(buffer); i++) buffer[i]=tolower(buffer[i]);

		tok = strtok(buffer, " \t"); /* ? */
		while( tok!= NULL)
		  {
			if (strcmp(tok, &tmpext[1])==0) /* have to skip the leading "." */
			  {
				strcpy(lpResult, xlpFile);
				/* Need to perhaps check that the file has a path
				 * attached */
        TRACE(shell, "found %s\n", lpResult);
                                return 33;

		/* Greater than 32 to indicate success FIXME According to the
		 * docs, I should be returning a handle for the
		 * executable. Does this mean I'm supposed to open the
		 * executable file or something? More RTFM, I guess... */
			  }
			tok=strtok(NULL, " \t");
		  }
	  }

    /* Check registry */
    if (RegQueryValue16( HKEY_CLASSES_ROOT, tmpext, filetype,
                         &filetypelen ) == ERROR_SUCCESS )
    {
	filetype[filetypelen]='\0';
  TRACE(shell, "File type: %s\n", filetype);

	/* Looking for ...buffer\shell\lpOperation\command */
	strcat( filetype, "\\shell\\" );
	strcat( filetype, lpOperation );
	strcat( filetype, "\\command" );
	
	if (RegQueryValue16( HKEY_CLASSES_ROOT, filetype, command,
                             &commandlen ) == ERROR_SUCCESS )
	{
	    /* Is there a replace() function anywhere? */
	    command[commandlen]='\0';
	    strcpy( lpResult, command );
	    tok=strstr( lpResult, "%1" );
	    if (tok != NULL)
	    {
		tok[0]='\0'; /* truncate string at the percent */
		strcat( lpResult, xlpFile ); /* what if no dir in xlpFile? */
		tok=strstr( command, "%1" );
		if ((tok!=NULL) && (strlen(tok)>2))
		{
		    strcat( lpResult, &tok[2] );
		}
	    }
	    retval=33; /* FIXME see above */
	}
    }
    else /* Check win.ini */
    {
	/* Toss the leading dot */
	extension++;
	if ( GetProfileString32A( "extensions", extension, "", command,
                                  sizeof(command)) > 0)
	  {
		if (strlen(command)!=0)
		  {
			strcpy( lpResult, command );
			tok=strstr( lpResult, "^" ); /* should be ^.extension? */
			if (tok != NULL)
			  {
				tok[0]='\0';
				strcat( lpResult, xlpFile ); /* what if no dir in xlpFile? */
				tok=strstr( command, "^" ); /* see above */
				if ((tok != NULL) && (strlen(tok)>5))
				  {
					strcat( lpResult, &tok[5]);
				  }
			  }
			retval=33; /* FIXME - see above */
		  }
	  }
	}

    TRACE(shell, "returning %s\n", lpResult);
    return retval;
}

/*************************************************************************
 *				ShellExecute16		[SHELL.20]
 */
HINSTANCE16 WINAPI ShellExecute16( HWND16 hWnd, LPCSTR lpOperation,
                                   LPCSTR lpFile, LPCSTR lpParameters,
                                   LPCSTR lpDirectory, INT16 iShowCmd )
{   HINSTANCE16 retval=31;
    char old_dir[1024];
    char cmd[256];

    TRACE(shell, "(%04x,'%s','%s','%s','%s',%x)\n",
		hWnd, lpOperation ? lpOperation:"<null>", lpFile ? lpFile:"<null>",
		lpParameters ? lpParameters : "<null>", 
		lpDirectory ? lpDirectory : "<null>", iShowCmd);

    if (lpFile==NULL) return 0; /* should not happen */
    if (lpOperation==NULL) /* default is open */
      lpOperation="open";

    if (lpDirectory)
    { GetCurrentDirectory32A( sizeof(old_dir), old_dir );
        SetCurrentDirectory32A( lpDirectory );
    }

    retval = SHELL_FindExecutable( lpFile, lpOperation, cmd );

    if (retval > 32)  /* Found */
    { if (lpParameters)
      { strcat(cmd," ");
            strcat(cmd,lpParameters);
        }

      TRACE(shell,"starting %s\n",cmd);
        retval = WinExec32( cmd, iShowCmd );
    }
    if (lpDirectory)
      SetCurrentDirectory32A( old_dir );
    return retval;
}


/*************************************************************************
 *             ShellExecute32A   (SHELL32.245)
 */
HINSTANCE32 WINAPI ShellExecute32A( HWND32 hWnd, LPCSTR lpOperation,
                                    LPCSTR lpFile, LPCSTR lpParameters,
                                    LPCSTR lpDirectory, INT32 iShowCmd )
{   TRACE(shell,"\n");
    return ShellExecute16( hWnd, lpOperation, lpFile, lpParameters,
                           lpDirectory, iShowCmd );
}

/*************************************************************************
 *             FindExecutable16   (SHELL.21)
 */
HINSTANCE16 WINAPI FindExecutable16( LPCSTR lpFile, LPCSTR lpDirectory,
                                     LPSTR lpResult )
{ return (HINSTANCE16)FindExecutable32A( lpFile, lpDirectory, lpResult );
}

/*************************************************************************
 *             FindExecutable32A   (SHELL32.184)
 */
HINSTANCE32 WINAPI FindExecutable32A( LPCSTR lpFile, LPCSTR lpDirectory,
                                      LPSTR lpResult )
{ HINSTANCE32 retval=31;    /* default - 'No association was found' */
    char old_dir[1024];

  TRACE(shell, "File %s, Dir %s\n", 
		 (lpFile != NULL?lpFile:"-"), 
		 (lpDirectory != NULL?lpDirectory:"-"));

    lpResult[0]='\0'; /* Start off with an empty return string */

    /* trap NULL parameters on entry */
    if (( lpFile == NULL ) || ( lpResult == NULL ))
  { /* FIXME - should throw a warning, perhaps! */
	return 2; /* File not found. Close enough, I guess. */
    }

    if (lpDirectory)
  { GetCurrentDirectory32A( sizeof(old_dir), old_dir );
        SetCurrentDirectory32A( lpDirectory );
    }

    retval = SHELL_FindExecutable( lpFile, "open", lpResult );

  TRACE(shell, "returning %s\n", lpResult);
  if (lpDirectory)
    SetCurrentDirectory32A( old_dir );
    return retval;
}

typedef struct
{ LPCSTR  szApp;
    LPCSTR  szOtherStuff;
    HICON32 hIcon;
} ABOUT_INFO;

#define		IDC_STATIC_TEXT		100
#define		IDC_LISTBOX		99
#define		IDC_WINE_TEXT		98

#define		DROP_FIELD_TOP		(-15)
#define		DROP_FIELD_HEIGHT	15

extern HICON32 hIconTitleFont;

static BOOL32 __get_dropline( HWND32 hWnd, LPRECT32 lprect )
{ HWND32 hWndCtl = GetDlgItem32(hWnd, IDC_WINE_TEXT);
    if( hWndCtl )
  { GetWindowRect32( hWndCtl, lprect );
	MapWindowPoints32( 0, hWnd, (LPPOINT32)lprect, 2 );
	lprect->bottom = (lprect->top += DROP_FIELD_TOP);
	return TRUE;
    }
    return FALSE;
}

/*************************************************************************
 *             AboutDlgProc32  (not an exported API function)
 */
LRESULT WINAPI AboutDlgProc32( HWND32 hWnd, UINT32 msg, WPARAM32 wParam,
                               LPARAM lParam )
{   HWND32 hWndCtl;
    char Template[512], AppTitle[512];

    TRACE(shell,"\n");

    switch(msg)
    { case WM_INITDIALOG:
      { ABOUT_INFO *info = (ABOUT_INFO *)lParam;
            if (info)
        { const char* const *pstr = SHELL_People;
                SendDlgItemMessage32A(hWnd, stc1, STM_SETICON32,info->hIcon, 0);
                GetWindowText32A( hWnd, Template, sizeof(Template) );
                sprintf( AppTitle, Template, info->szApp );
                SetWindowText32A( hWnd, AppTitle );
                SetWindowText32A( GetDlgItem32(hWnd, IDC_STATIC_TEXT),
                                  info->szOtherStuff );
                hWndCtl = GetDlgItem32(hWnd, IDC_LISTBOX);
                SendMessage32A( hWndCtl, WM_SETREDRAW, 0, 0 );
                SendMessage32A( hWndCtl, WM_SETFONT, hIconTitleFont, 0 );
                while (*pstr)
          { SendMessage32A( hWndCtl, LB_ADDSTRING32, (WPARAM32)-1, (LPARAM)*pstr );
                    pstr++;
                }
                SendMessage32A( hWndCtl, WM_SETREDRAW, 1, 0 );
            }
        }
        return 1;

    case WM_PAINT:
      { RECT32 rect;
	    PAINTSTRUCT32 ps;
	    HDC32 hDC = BeginPaint32( hWnd, &ps );

	    if( __get_dropline( hWnd, &rect ) )
		GRAPH_DrawLines( hDC, (LPPOINT32)&rect, 1, GetStockObject32( BLACK_PEN ) );
	    EndPaint32( hWnd, &ps );
	}
	break;

    case WM_LBTRACKPOINT:
	hWndCtl = GetDlgItem32(hWnd, IDC_LISTBOX);
	if( (INT16)GetKeyState16( VK_CONTROL ) < 0 )
      { if( DragDetect32( hWndCtl, *((LPPOINT32)&lParam) ) )
        { INT32 idx = SendMessage32A( hWndCtl, LB_GETCURSEL32, 0, 0 );
		if( idx != -1 )
          { INT32 length = SendMessage32A( hWndCtl, LB_GETTEXTLEN32, (WPARAM32)idx, 0 );
		    HGLOBAL16 hMemObj = GlobalAlloc16( GMEM_MOVEABLE, length + 1 );
		    char* pstr = (char*)GlobalLock16( hMemObj );

		    if( pstr )
            { HCURSOR16 hCursor = LoadCursor16( 0, MAKEINTRESOURCE16(OCR_DRAGOBJECT) );
			SendMessage32A( hWndCtl, LB_GETTEXT32, (WPARAM32)idx, (LPARAM)pstr );
			SendMessage32A( hWndCtl, LB_DELETESTRING32, (WPARAM32)idx, 0 );
			UpdateWindow32( hWndCtl );
			if( !DragObject16((HWND16)hWnd, (HWND16)hWnd, DRAGOBJ_DATA, 0, (WORD)hMemObj, hCursor) )
			    SendMessage32A( hWndCtl, LB_ADDSTRING32, (WPARAM32)-1, (LPARAM)pstr );
		    }
            if( hMemObj )
              GlobalFree16( hMemObj );
		}
	    }
	}
	break;

    case WM_QUERYDROPOBJECT:
	if( wParam == 0 )
      { LPDRAGINFO lpDragInfo = (LPDRAGINFO)PTR_SEG_TO_LIN((SEGPTR)lParam);
	    if( lpDragInfo && lpDragInfo->wFlags == DRAGOBJ_DATA )
        { RECT32 rect;
		if( __get_dropline( hWnd, &rect ) )
          { POINT32 pt = { lpDragInfo->pt.x, lpDragInfo->pt.y };
		    rect.bottom += DROP_FIELD_HEIGHT;
		    if( PtInRect32( &rect, pt ) )
            { SetWindowLong32A( hWnd, DWL_MSGRESULT, 1 );
			return TRUE;
		    }
		}
	    }
	}
	break;

    case WM_DROPOBJECT:
	if( wParam == hWnd )
      { LPDRAGINFO lpDragInfo = (LPDRAGINFO)PTR_SEG_TO_LIN((SEGPTR)lParam);
	    if( lpDragInfo && lpDragInfo->wFlags == DRAGOBJ_DATA && lpDragInfo->hList )
        { char* pstr = (char*)GlobalLock16( (HGLOBAL16)(lpDragInfo->hList) );
		if( pstr )
          { static char __appendix_str[] = " with";

		    hWndCtl = GetDlgItem32( hWnd, IDC_WINE_TEXT );
		    SendMessage32A( hWndCtl, WM_GETTEXT, 512, (LPARAM)Template );
		    if( !lstrncmp32A( Template, "WINE", 4 ) )
			SetWindowText32A( GetDlgItem32(hWnd, IDC_STATIC_TEXT), Template );
		    else
          { char* pch = Template + strlen(Template) - strlen(__appendix_str);
			*pch = '\0';
			SendMessage32A( GetDlgItem32(hWnd, IDC_LISTBOX), LB_ADDSTRING32, 
					(WPARAM32)-1, (LPARAM)Template );
		    }

		    lstrcpy32A( Template, pstr );
		    lstrcat32A( Template, __appendix_str );
		    SetWindowText32A( hWndCtl, Template );
		    SetWindowLong32A( hWnd, DWL_MSGRESULT, 1 );
		    return TRUE;
		}
	    }
	}
	break;

    case WM_COMMAND:
        if (wParam == IDOK)
    {  EndDialog32(hWnd, TRUE);
            return TRUE;
        }
        break;
    }
    return 0;
}


/*************************************************************************
 *             AboutDlgProc16   (SHELL.33)
 */
LRESULT WINAPI AboutDlgProc16( HWND16 hWnd, UINT16 msg, WPARAM16 wParam,
                               LPARAM lParam )
{ return AboutDlgProc32( hWnd, msg, wParam, lParam );
}


/*************************************************************************
 *             ShellAbout16   (SHELL.22)
 */
BOOL16 WINAPI ShellAbout16( HWND16 hWnd, LPCSTR szApp, LPCSTR szOtherStuff,
                            HICON16 hIcon )
{ return ShellAbout32A( hWnd, szApp, szOtherStuff, hIcon );
}

/*************************************************************************
 *             ShellAbout32A   (SHELL32.243)
 */
BOOL32 WINAPI ShellAbout32A( HWND32 hWnd, LPCSTR szApp, LPCSTR szOtherStuff,
                             HICON32 hIcon )
{   ABOUT_INFO info;
    TRACE(shell,"\n");
    info.szApp        = szApp;
    info.szOtherStuff = szOtherStuff;
    info.hIcon        = hIcon;
    if (!hIcon) info.hIcon = LoadIcon16( 0, MAKEINTRESOURCE16(OIC_WINEICON) );
    return DialogBoxIndirectParam32A( WIN_GetWindowInstance( hWnd ),
                         SYSRES_GetResPtr( SYSRES_DIALOG_SHELL_ABOUT_MSGBOX ),
                                      hWnd, AboutDlgProc32, (LPARAM)&info );
}


/*************************************************************************
 *             ShellAbout32W   (SHELL32.244)
 */
BOOL32 WINAPI ShellAbout32W( HWND32 hWnd, LPCWSTR szApp, LPCWSTR szOtherStuff,
                             HICON32 hIcon )
{   BOOL32 ret;
    ABOUT_INFO info;

    TRACE(shell,"\n");
    
    info.szApp        = HEAP_strdupWtoA( GetProcessHeap(), 0, szApp );
    info.szOtherStuff = HEAP_strdupWtoA( GetProcessHeap(), 0, szOtherStuff );
    info.hIcon        = hIcon;
    if (!hIcon) info.hIcon = LoadIcon16( 0, MAKEINTRESOURCE16(OIC_WINEICON) );
    ret = DialogBoxIndirectParam32A( WIN_GetWindowInstance( hWnd ),
                         SYSRES_GetResPtr( SYSRES_DIALOG_SHELL_ABOUT_MSGBOX ),
                                      hWnd, AboutDlgProc32, (LPARAM)&info );
    HeapFree( GetProcessHeap(), 0, (LPSTR)info.szApp );
    HeapFree( GetProcessHeap(), 0, (LPSTR)info.szOtherStuff );
    return ret;
}

/*************************************************************************
 *				Shell_NotifyIcon	[SHELL32.249]
 *	FIXME
 *	This function is supposed to deal with the systray.
 *	Any ideas on how this is to be implimented?
 */
BOOL32 WINAPI Shell_NotifyIcon(	DWORD dwMessage,
				PNOTIFYICONDATA pnid )
{   TRACE(shell,"\n");
    return FALSE;
}

/*************************************************************************
 *				Shell_NotifyIcon	[SHELL32.240]
 *	FIXME
 *	This function is supposed to deal with the systray.
 *	Any ideas on how this is to be implimented?
 */
BOOL32 WINAPI Shell_NotifyIconA(DWORD dwMessage,
				PNOTIFYICONDATA pnid )
{   TRACE(shell,"\n");
    return FALSE;
}

/*************************************************************************
 *				SHELL_GetResourceTable
 */
static DWORD SHELL_GetResourceTable(HFILE32 hFile,LPBYTE *retptr)
{
  IMAGE_DOS_HEADER	mz_header;
  char			magic[4];
  int			size;
  TRACE(shell,"\n");  
  *retptr = NULL;
  _llseek32( hFile, 0, SEEK_SET );
  if (	(_lread32(hFile,&mz_header,sizeof(mz_header)) != sizeof(mz_header)) ||
  	(mz_header.e_magic != IMAGE_DOS_SIGNATURE)
  ) { /* .ICO file ? */
        if (mz_header.e_cblp == 1) { /* ICONHEADER.idType, must be 1 */
	    *retptr = (LPBYTE)-1;
  	    return 1;
	}
	else
	    return 0; /* failed */
  }
  _llseek32( hFile, mz_header.e_lfanew, SEEK_SET );
  if (_lread32( hFile, magic, sizeof(magic) ) != sizeof(magic))
	return 0;
  _llseek32( hFile, mz_header.e_lfanew, SEEK_SET);

  if (*(DWORD*)magic  == IMAGE_NT_SIGNATURE)
	return IMAGE_NT_SIGNATURE;
  if (*(WORD*)magic == IMAGE_OS2_SIGNATURE) {
  	IMAGE_OS2_HEADER	ne_header;
  	LPBYTE			pTypeInfo = (LPBYTE)-1;

  	if (_lread32(hFile,&ne_header,sizeof(ne_header))!=sizeof(ne_header))
		return 0;

	if (ne_header.ne_magic != IMAGE_OS2_SIGNATURE) return 0;
	size = ne_header.rname_tab_offset - ne_header.resource_tab_offset;
	if( size > sizeof(NE_TYPEINFO) )
	{
	    pTypeInfo = (BYTE*)HeapAlloc( GetProcessHeap(), 0, size);
	    if( pTypeInfo ) {
		_llseek32(hFile, mz_header.e_lfanew+ne_header.resource_tab_offset, SEEK_SET);
		if( _lread32( hFile, (char*)pTypeInfo, size) != size ) { 
		    HeapFree( GetProcessHeap(), 0, pTypeInfo); 
		    pTypeInfo = NULL;
		}
	    }
	}
  	*retptr = pTypeInfo;
        return IMAGE_OS2_SIGNATURE;
  } else
  	return 0; /* failed */
}

/*************************************************************************
 *			SHELL_LoadResource
 */
static HGLOBAL16 SHELL_LoadResource(HINSTANCE16 hInst, HFILE32 hFile, NE_NAMEINFO* pNInfo, WORD sizeShift)
{ BYTE*  ptr;
 HGLOBAL16 handle = DirectResAlloc( hInst, 0x10, (DWORD)pNInfo->length << sizeShift);
  TRACE(shell,"\n");
 if( (ptr = (BYTE*)GlobalLock16( handle )) )
  { _llseek32( hFile, (DWORD)pNInfo->offset << sizeShift, SEEK_SET);
     _lread32( hFile, (char*)ptr, pNInfo->length << sizeShift);
     return handle;
   }
 return 0;
}

/*************************************************************************
 *                      ICO_LoadIcon
 */
static HGLOBAL16 ICO_LoadIcon(HINSTANCE16 hInst, HFILE32 hFile, LPicoICONDIRENTRY lpiIDE)
{ BYTE*  ptr;
 HGLOBAL16 handle = DirectResAlloc( hInst, 0x10, lpiIDE->dwBytesInRes);
  TRACE(shell,"\n");
 if( (ptr = (BYTE*)GlobalLock16( handle )) )
  { _llseek32( hFile, lpiIDE->dwImageOffset, SEEK_SET);
     _lread32( hFile, (char*)ptr, lpiIDE->dwBytesInRes);
     return handle;
   }
 return 0;
}

/*************************************************************************
 *                      ICO_GetIconDirectory
 *
 *  Read .ico file and build phony ICONDIR struct for GetIconID
 */
static HGLOBAL16 ICO_GetIconDirectory(HINSTANCE16 hInst, HFILE32 hFile, LPicoICONDIR* lplpiID ) 
{ WORD    id[3];  /* idReserved, idType, idCount */
  LPicoICONDIR	lpiID;
  int		i;
 
  TRACE(shell,"\n"); 
  _llseek32( hFile, 0, SEEK_SET );
  if( _lread32(hFile,(char*)id,sizeof(id)) != sizeof(id) ) return 0;

  /* check .ICO header 
   *
   * - see http://www.microsoft.com/win32dev/ui/icons.htm
   */

  if( id[0] || id[1] != 1 || !id[2] ) return 0;

  i = id[2]*sizeof(icoICONDIRENTRY) + sizeof(id);

  lpiID = (LPicoICONDIR)HeapAlloc( GetProcessHeap(), 0, i);

  if( _lread32(hFile,(char*)lpiID->idEntries,i) == i )
  { HGLOBAL16 handle = DirectResAlloc( hInst, 0x10,
                                     id[2]*sizeof(ICONDIRENTRY) + sizeof(id) );
     if( handle ) 
    { CURSORICONDIR*     lpID = (CURSORICONDIR*)GlobalLock16( handle );
       lpID->idReserved = lpiID->idReserved = id[0];
       lpID->idType = lpiID->idType = id[1];
       lpID->idCount = lpiID->idCount = id[2];
       for( i=0; i < lpiID->idCount; i++ )
      { memcpy((void*)(lpID->idEntries + i), 
		   (void*)(lpiID->idEntries + i), sizeof(ICONDIRENTRY) - 2);
	    lpID->idEntries[i].icon.wResId = i;
         }
      *lplpiID = lpiID;
       return handle;
     }
  }
  /* fail */

  HeapFree( GetProcessHeap(), 0, lpiID);
  return 0;
}

/*************************************************************************
 *			InternalExtractIcon		[SHELL.39]
 *
 * This abortion is called directly by Progman
 */
HGLOBAL16 WINAPI InternalExtractIcon(HINSTANCE16 hInstance,
                                     LPCSTR lpszExeFileName, UINT16 nIconIndex,
                                     WORD n )
{
  HGLOBAL16 	hRet = 0;
  HGLOBAL16*	RetPtr = NULL;
  LPBYTE  	pData;
  OFSTRUCT 	ofs;
  DWORD		sig;
  HFILE32 	hFile = OpenFile32( lpszExeFileName, &ofs, OF_READ );
  UINT16	iconDirCount = 0,iconCount = 0;
  
  TRACE(shell,"(%04x,file %s,start %d,extract %d\n", 
		       hInstance, lpszExeFileName, nIconIndex, n);

  if( hFile == HFILE_ERROR32 || !n )
    return 0;

  hRet = GlobalAlloc16( GMEM_FIXED | GMEM_ZEROINIT, sizeof(HICON16)*n);
  RetPtr = (HICON16*)GlobalLock16(hRet);

  *RetPtr = (n == 0xFFFF)? 0: 1;	/* error return values */

  sig = SHELL_GetResourceTable(hFile,&pData);

  if((sig == IMAGE_OS2_SIGNATURE)
  || (sig == 1)) /* .ICO file */
  {
    HICON16	 hIcon = 0;
    NE_TYPEINFO* pTInfo = (NE_TYPEINFO*)(pData + 2);
    NE_NAMEINFO* pIconStorage = NULL;
    NE_NAMEINFO* pIconDir = NULL;
    LPicoICONDIR lpiID = NULL;
 
    if( pData == (BYTE*)-1 )
    {
	/* check for .ICO file */

	hIcon = ICO_GetIconDirectory(hInstance, hFile, &lpiID);
	if( hIcon ) { iconDirCount = 1; iconCount = lpiID->idCount; }
    }
    else while( pTInfo->type_id && !(pIconStorage && pIconDir) )
    {
	/* find icon directory and icon repository */

	if( pTInfo->type_id == NE_RSCTYPE_GROUP_ICON ) 
	  {
	     iconDirCount = pTInfo->count;
	     pIconDir = ((NE_NAMEINFO*)(pTInfo + 1));
       TRACE(shell,"\tfound directory - %i icon families\n", iconDirCount);
	  }
	if( pTInfo->type_id == NE_RSCTYPE_ICON ) 
	  { 
	     iconCount = pTInfo->count;
	     pIconStorage = ((NE_NAMEINFO*)(pTInfo + 1));
       TRACE(shell,"\ttotal icons - %i\n", iconCount);
	  }
  	pTInfo = (NE_TYPEINFO *)((char*)(pTInfo+1)+pTInfo->count*sizeof(NE_NAMEINFO));
    }

    /* load resources and create icons */

    if( (pIconStorage && pIconDir) || lpiID )
      if( nIconIndex == (UINT16)-1 ) RetPtr[0] = iconDirCount;
      else if( nIconIndex < iconDirCount )
      {
	  UINT16   i, icon;

	  if( n > iconDirCount - nIconIndex ) n = iconDirCount - nIconIndex;

	  for( i = nIconIndex; i < nIconIndex + n; i++ ) 
	  {
	      /* .ICO files have only one icon directory */

	      if( lpiID == NULL )
	           hIcon = SHELL_LoadResource( hInstance, hFile, pIconDir + i, 
							      *(WORD*)pData );
	      RetPtr[i-nIconIndex] = GetIconID( hIcon, 3 );
	      GlobalFree16(hIcon); 
          }

	  for( icon = nIconIndex; icon < nIconIndex + n; icon++ )
	  {
	      hIcon = 0;
	      if( lpiID )
		   hIcon = ICO_LoadIcon( hInstance, hFile, 
					 lpiID->idEntries + RetPtr[icon-nIconIndex]);
	      else
	         for( i = 0; i < iconCount; i++ )
		   if( pIconStorage[i].id == (RetPtr[icon-nIconIndex] | 0x8000) )
		     hIcon = SHELL_LoadResource( hInstance, hFile, pIconStorage + i,
								    *(WORD*)pData );
	      if( hIcon )
	      {
		  RetPtr[icon-nIconIndex] = LoadIconHandler( hIcon, TRUE ); 
		  FarSetOwner( RetPtr[icon-nIconIndex], GetExePtr(hInstance) );
	      }
	      else
		  RetPtr[icon-nIconIndex] = 0;
	  }
      }
    if( lpiID ) HeapFree( GetProcessHeap(), 0, lpiID);
    else HeapFree( GetProcessHeap(), 0, pData);
  } 
  if( sig == IMAGE_NT_SIGNATURE)
  {
  	LPBYTE			peimage,idata,igdata;
	LPIMAGE_DOS_HEADER	dheader;
	LPIMAGE_NT_HEADERS	pe_header;
	LPIMAGE_SECTION_HEADER	pe_sections;
	LPIMAGE_RESOURCE_DIRECTORY	rootresdir,iconresdir,icongroupresdir;
	LPIMAGE_RESOURCE_DATA_ENTRY	idataent,igdataent;
	HANDLE32		fmapping;
	int			i,j;
	LPIMAGE_RESOURCE_DIRECTORY_ENTRY	xresent;
	CURSORICONDIR		**cids;
	
	fmapping = CreateFileMapping32A(hFile,NULL,PAGE_READONLY|SEC_COMMIT,0,0,NULL);
	if (fmapping == 0) { /* FIXME, INVALID_HANDLE_VALUE? */
    WARN(shell,"failed to create filemap.\n");
		_lclose32( hFile);
		return 0;
	}
	peimage = MapViewOfFile(fmapping,FILE_MAP_READ,0,0,0);
	if (!peimage) {
    WARN(shell,"failed to mmap filemap.\n");
		CloseHandle(fmapping);
		_lclose32( hFile);
		return 0;
	}
	dheader = (LPIMAGE_DOS_HEADER)peimage;
	/* it is a pe header, SHELL_GetResourceTable checked that */
	pe_header = (LPIMAGE_NT_HEADERS)(peimage+dheader->e_lfanew);
	/* probably makes problems with short PE headers... but I haven't seen 
	 * one yet... 
	 */
	pe_sections = (LPIMAGE_SECTION_HEADER)(((char*)pe_header)+sizeof(*pe_header));
	rootresdir = NULL;
	for (i=0;i<pe_header->FileHeader.NumberOfSections;i++) {
		if (pe_sections[i].Characteristics & IMAGE_SCN_CNT_UNINITIALIZED_DATA)
			continue;
		/* FIXME: doesn't work when the resources are not in a seperate section */
		if (pe_sections[i].VirtualAddress == pe_header->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_RESOURCE].VirtualAddress) {
			rootresdir = (LPIMAGE_RESOURCE_DIRECTORY)((char*)peimage+pe_sections[i].PointerToRawData);
			break;
		}
	}

	if (!rootresdir) {
    WARN(shell,"haven't found section for resource directory.\n");
		UnmapViewOfFile(peimage);
		CloseHandle(fmapping);
		_lclose32( hFile);
		return 0;
	}
	icongroupresdir = GetResDirEntryW(rootresdir,RT_GROUP_ICON32W,
                                          (DWORD)rootresdir,FALSE);
	if (!icongroupresdir) {
    WARN(shell,"No Icongroupresourcedirectory!\n");
		UnmapViewOfFile(peimage);
		CloseHandle(fmapping);
		_lclose32( hFile);
		return 0;
	}

	iconDirCount = icongroupresdir->NumberOfNamedEntries+icongroupresdir->NumberOfIdEntries;
	if( nIconIndex == (UINT16)-1 ) {
		RetPtr[0] = iconDirCount;
		UnmapViewOfFile(peimage);
		CloseHandle(fmapping);
		_lclose32( hFile);
		return hRet;
	}

	if (nIconIndex >= iconDirCount) {
    WARN(shell,"nIconIndex %d is larger than iconDirCount %d\n",
			    nIconIndex,iconDirCount);
		UnmapViewOfFile(peimage);
		CloseHandle(fmapping);
		_lclose32( hFile);
		GlobalFree16(hRet);
		return 0;
	}
	cids = (CURSORICONDIR**)HeapAlloc(GetProcessHeap(),0,n*sizeof(CURSORICONDIR*));
		
	/* caller just wanted the number of entries */

	xresent = (LPIMAGE_RESOURCE_DIRECTORY_ENTRY)(icongroupresdir+1);
	/* assure we don't get too much ... */
	if( n > iconDirCount - nIconIndex ) n = iconDirCount - nIconIndex;

	/* starting from specified index ... */
	xresent = xresent+nIconIndex;

	for (i=0;i<n;i++,xresent++) {
		CURSORICONDIR	*cid;
		LPIMAGE_RESOURCE_DIRECTORY	resdir;

		/* go down this resource entry, name */
		resdir = (LPIMAGE_RESOURCE_DIRECTORY)((DWORD)rootresdir+(xresent->u2.s.OffsetToDirectory));
		/* default language (0) */
		resdir = GetResDirEntryW(resdir,(LPWSTR)0,(DWORD)rootresdir,TRUE);
		igdataent = (LPIMAGE_RESOURCE_DATA_ENTRY)resdir;

		/* lookup address in mapped image for virtual address */
		igdata = NULL;
		for (j=0;j<pe_header->FileHeader.NumberOfSections;j++) {
			if (igdataent->OffsetToData < pe_sections[j].VirtualAddress)
				continue;
			if (igdataent->OffsetToData+igdataent->Size > pe_sections[j].VirtualAddress+pe_sections[j].SizeOfRawData)
				continue;
			igdata = peimage+(igdataent->OffsetToData-pe_sections[j].VirtualAddress+pe_sections[j].PointerToRawData);
		}
		if (!igdata) {
      WARN(shell,"no matching real address for icongroup!\n");
			UnmapViewOfFile(peimage);
			CloseHandle(fmapping);
			_lclose32( hFile);
			return 0;
		}
		/* found */
		cid = (CURSORICONDIR*)igdata;
		cids[i] = cid;
		RetPtr[i] = LookupIconIdFromDirectoryEx32(igdata,TRUE,SYSMETRICS_CXICON,SYSMETRICS_CYICON,0);
	}
	iconresdir=GetResDirEntryW(rootresdir,RT_ICON32W,
                                   (DWORD)rootresdir,FALSE);
	if (!iconresdir) {
      WARN(shell,"No Iconresourcedirectory!\n");
	    UnmapViewOfFile(peimage);
	    CloseHandle(fmapping);
	    _lclose32( hFile);
	    return 0;
	}
	for (i=0;i<n;i++) {
	    LPIMAGE_RESOURCE_DIRECTORY	xresdir;

	    xresdir = GetResDirEntryW(iconresdir,(LPWSTR)RetPtr[i],(DWORD)rootresdir,FALSE);
	    xresdir = GetResDirEntryW(xresdir,(LPWSTR)0,(DWORD)rootresdir,TRUE);

	    idataent = (LPIMAGE_RESOURCE_DATA_ENTRY)xresdir;

	    idata = NULL;
	    /* map virtual to address in image */
	    for (j=0;j<pe_header->FileHeader.NumberOfSections;j++) {
		if (idataent->OffsetToData < pe_sections[j].VirtualAddress)
		    continue;
		if (idataent->OffsetToData+idataent->Size > pe_sections[j].VirtualAddress+pe_sections[j].SizeOfRawData)
		    continue;
		idata = peimage+(idataent->OffsetToData-pe_sections[j].VirtualAddress+pe_sections[j].PointerToRawData);
	    }
	    if (!idata) {
    WARN(shell,"no matching real address found for icondata!\n");
		RetPtr[i]=0;
		continue;
	    }
	    RetPtr[i] = CreateIconFromResourceEx32(idata,idataent->Size,TRUE,0x00030000,SYSMETRICS_CXICON,SYSMETRICS_CYICON,0);
	}
	UnmapViewOfFile(peimage);
	CloseHandle(fmapping);
	_lclose32( hFile);
	return hRet;
  }
  _lclose32( hFile );
  /* return array with icon handles */
  return hRet;

}

/*************************************************************************
 *             ExtractIcon16   (SHELL.34)
 */
HICON16 WINAPI ExtractIcon16( HINSTANCE16 hInstance, LPCSTR lpszExeFileName,
	UINT16 nIconIndex )
{   TRACE(shell,"\n");
    return ExtractIcon32A( hInstance, lpszExeFileName, nIconIndex );
}


/*************************************************************************
 *             ExtractIcon32A   (SHELL32.133)
 */
HICON32 WINAPI ExtractIcon32A( HINSTANCE32 hInstance, LPCSTR lpszExeFileName,
	UINT32 nIconIndex )
{   HGLOBAL16 handle = InternalExtractIcon(hInstance,lpszExeFileName,nIconIndex, 1);
    TRACE(shell,"\n");
    if( handle )
    {
	HICON16* ptr = (HICON16*)GlobalLock16(handle);
	HICON16  hIcon = *ptr;

	GlobalFree16(handle);
	return hIcon;
    }
    return 0;
}

/*************************************************************************
 *             ExtractIcon32W   (SHELL32.180)
 */
HICON32 WINAPI ExtractIcon32W( HINSTANCE32 hInstance, LPCWSTR lpszExeFileName,
	UINT32 nIconIndex )
{ LPSTR  exefn;
  HICON32  ret;
  TRACE(shell,"\n");

  exefn = HEAP_strdupWtoA(GetProcessHeap(),0,lpszExeFileName);
  ret = ExtractIcon32A(hInstance,exefn,nIconIndex);

	HeapFree(GetProcessHeap(),0,exefn);
	return ret;
}


/*************************************************************************
 *				ExtractAssociatedIcon	[SHELL.36]
 * 
 * Return icon for given file (either from file itself or from associated
 * executable) and patch parameters if needed.
 */
HICON32 WINAPI ExtractAssociatedIcon32A(HINSTANCE32 hInst,LPSTR lpIconPath,
	LPWORD lpiIcon)
{ TRACE(shell,"\n");
	return ExtractAssociatedIcon16(hInst,lpIconPath,lpiIcon);
}

HICON16 WINAPI ExtractAssociatedIcon16(HINSTANCE16 hInst,LPSTR lpIconPath,
	LPWORD lpiIcon)
{ HICON16 hIcon;

  TRACE(shell,"\n");

  hIcon = ExtractIcon16(hInst, lpIconPath, *lpiIcon);

  if( hIcon < 2 )
  { if( hIcon == 1 ) /* no icons found in given file */
    { char  tempPath[0x80];
	    UINT16  uRet = FindExecutable16(lpIconPath,NULL,tempPath);

	    if( uRet > 32 && tempPath[0] )
      { strcpy(lpIconPath,tempPath);
		hIcon = ExtractIcon16(hInst, lpIconPath, *lpiIcon);
        if( hIcon > 2 ) 
          return hIcon;
	    }
	    else hIcon = 0;
	}

	if( hIcon == 1 ) 
	    *lpiIcon = 2;   /* MSDOS icon - we found .exe but no icons in it */
	else
	    *lpiIcon = 6;   /* generic icon - found nothing */

	GetModuleFileName16(hInst, lpIconPath, 0x80);
	hIcon = LoadIcon16( hInst, MAKEINTRESOURCE16(*lpiIcon));
    }
    return hIcon;
}

/*************************************************************************
 *				FindEnvironmentString	[SHELL.38]
 *
 * Returns a pointer into the DOS environment... Ugh.
 */
LPSTR SHELL_FindString(LPSTR lpEnv, LPCSTR entry)
{ UINT16 l;

  TRACE(shell,"\n");

  l = strlen(entry); 
  for( ; *lpEnv ; lpEnv+=strlen(lpEnv)+1 )
  { if( lstrncmpi32A(lpEnv, entry, l) ) 
      continue;
	if( !*(lpEnv+l) )
	    return (lpEnv + l); 		/* empty entry */
	else if ( *(lpEnv+l)== '=' )
	    return (lpEnv + l + 1);
    }
    return NULL;
}

SEGPTR WINAPI FindEnvironmentString(LPSTR str)
{ SEGPTR  spEnv;
  LPSTR lpEnv,lpString;
  TRACE(shell,"\n");
    
  spEnv = GetDOSEnvironment();

  lpEnv = (LPSTR)PTR_SEG_TO_LIN(spEnv);
  lpString = (spEnv)?SHELL_FindString(lpEnv, str):NULL; 

    if( lpString )		/*  offset should be small enough */
	return spEnv + (lpString - lpEnv);
    return (SEGPTR)NULL;
}

/*************************************************************************
 *              		DoEnvironmentSubst      [SHELL.37]
 *
 * Replace %KEYWORD% in the str with the value of variable KEYWORD
 * from "DOS" environment.
 */
DWORD WINAPI DoEnvironmentSubst(LPSTR str,WORD length)
{
  LPSTR   lpEnv = (LPSTR)PTR_SEG_TO_LIN(GetDOSEnvironment());
  LPSTR   lpBuffer = (LPSTR)HeapAlloc( GetProcessHeap(), 0, length);
  LPSTR   lpstr = str;
  LPSTR   lpbstr = lpBuffer;

  CharToOem32A(str,str);

  TRACE(shell,"accept %s\n", str);

  while( *lpstr && lpbstr - lpBuffer < length )
   {
     LPSTR lpend = lpstr;

     if( *lpstr == '%' )
       {
	  do { lpend++; } while( *lpend && *lpend != '%' );
	  if( *lpend == '%' && lpend - lpstr > 1 )	/* found key */
	    {
	       LPSTR lpKey;
	      *lpend = '\0';  
	       lpKey = SHELL_FindString(lpEnv, lpstr+1);
	       if( lpKey )				/* found key value */
		 {
		   int l = strlen(lpKey);

		   if( l > length - (lpbstr - lpBuffer) - 1 )
		     {
           WARN(shell,"-- Env subst aborted - string too short\n");
		      *lpend = '%';
		       break;
		     }
		   strcpy(lpbstr, lpKey);
		   lpbstr += l;
		 }
	       else break;
	      *lpend = '%';
	       lpstr = lpend + 1;
	    }
	  else break;					/* back off and whine */

	  continue;
       } 

     *lpbstr++ = *lpstr++;
   }

 *lpbstr = '\0';
  if( lpstr - str == strlen(str) )
    {
      strncpy(str, lpBuffer, length);
      length = 1;
    }
  else
      length = 0;

  TRACE(shell,"-- return %s\n", str);

  OemToChar32A(str,str);
  HeapFree( GetProcessHeap(), 0, lpBuffer);

  /*  Return str length in the LOWORD
   *  and 1 in HIWORD if subst was successful.
   */
 return (DWORD)MAKELONG(strlen(str), length);
}

/*************************************************************************
 *				ShellHookProc		[SHELL.103]
 * System-wide WH_SHELL hook.
 */
LRESULT WINAPI ShellHookProc(INT16 code, WPARAM16 wParam, LPARAM lParam)
{
    TRACE(shell,"%i, %04x, %08x\n", code, wParam, 
						      (unsigned)lParam );
    if( SHELL_hHook && SHELL_hWnd )
    {
	UINT16	uMsg = 0;
        switch( code )
        {
	    case HSHELL_WINDOWCREATED:		uMsg = uMsgWndCreated;   break;
	    case HSHELL_WINDOWDESTROYED:	uMsg = uMsgWndDestroyed; break;
	    case HSHELL_ACTIVATESHELLWINDOW: 	uMsg = uMsgShellActivate;
        }
	PostMessage16( SHELL_hWnd, uMsg, wParam, 0 );
    }
    return CallNextHookEx16( WH_SHELL, code, wParam, lParam );
}

/*************************************************************************
 *				RegisterShellHook	[SHELL.102]
 */
BOOL32 WINAPI RegisterShellHook(HWND16 hWnd, UINT16 uAction)
{ TRACE(shell,"%04x [%u]\n", hWnd, uAction );

    switch( uAction )
  { case 2:  /* register hWnd as a shell window */
	     if( !SHELL_hHook )
      { HMODULE16 hShell = GetModuleHandle16( "SHELL" );
        SHELL_hHook = SetWindowsHookEx16( WH_SHELL, ShellHookProc, hShell, 0 );
		if( SHELL_hHook )
        { uMsgWndCreated = RegisterWindowMessage32A( lpstrMsgWndCreated );
		    uMsgWndDestroyed = RegisterWindowMessage32A( lpstrMsgWndDestroyed );
		    uMsgShellActivate = RegisterWindowMessage32A( lpstrMsgShellActivate );
		} 
        else 
          WARN(shell,"-- unable to install ShellHookProc()!\n");
	     }

      if( SHELL_hHook )
        return ((SHELL_hWnd = hWnd) != 0);
	     break;

	default:
    WARN(shell, "-- unknown code %i\n", uAction );
	     /* just in case */
	     SHELL_hWnd = 0;
    }
    return FALSE;
}

/*************************************************************************
 *				SHGetFileInfoA		[SHELL32.218]
 *
 * FIXME
 *   
 */
HIMAGELIST ShellSmallIconList = 0;
HIMAGELIST ShellBigIconList = 0;

DWORD WINAPI SHGetFileInfo32A(LPCSTR path,DWORD dwFileAttributes,
                              SHFILEINFO32A *psfi, UINT32 sizeofpsfi,
                              UINT32 flags )
{ CHAR szTemp[MAX_PATH];
  DWORD ret=0;
  
  TRACE(shell,"(%s,0x%x,%p,0x%x,0x%x)\n",
	      path,dwFileAttributes,psfi,sizeofpsfi,flags);

  /* translate the pidl to a path*/
  if (flags & SHGFI_PIDL)
  { SHGetPathFromIDList32A ((LPCITEMIDLIST)path,szTemp);
    TRACE(shell,"pidl=%p is %s\n",path,szTemp);
  }

  if (flags & SHGFI_ATTRIBUTES)
  { FIXME(shell,"file attributes, stub\n");
    psfi->dwAttributes=0;
    ret=TRUE;    
  }

  if (flags & SHGFI_DISPLAYNAME)
  { if (flags & SHGFI_PIDL)
    { strcpy(psfi->szDisplayName,szTemp);
    }
    else
    { strcpy(psfi->szDisplayName,path);
      TRACE(shell,"displayname=%s\n", szTemp);
    }
    ret=TRUE;
  }
  
  if (flags & SHGFI_TYPENAME)
  { FIXME(shell,"get the file type, stub\n");
    strcpy(psfi->szTypeName,"");
    ret=TRUE;
  }
  
  if (flags & SHGFI_ICONLOCATION)
  { FIXME(shell,"location of icon, stub\n");
    strcpy(psfi->szDisplayName,"");
    ret=TRUE;
  }

  if (flags & SHGFI_EXETYPE)
    FIXME(shell,"type of executable, stub\n");

  if (flags & SHGFI_LINKOVERLAY)
    FIXME(shell,"set icon to link, stub\n");

  if (flags & SHGFI_OPENICON)
    FIXME(shell,"set to open icon, stub\n");

  if (flags & SHGFI_SELECTED)
    FIXME(shell,"set icon to selected, stub\n");

  if (flags & SHGFI_SHELLICONSIZE)
    FIXME(shell,"set icon to shell size, stub\n");

  if (flags & SHGFI_USEFILEATTRIBUTES)
    FIXME(shell,"use the dwFileAttributes, stub\n");
 
  if (flags & SHGFI_ICON)
  { FIXME(shell,"icon handle\n");
    if (flags & SHGFI_SMALLICON)
     { TRACE(shell,"set to small icon\n"); 
       psfi->hIcon=ImageList_GetIcon(ShellSmallIconList,0,ILD_NORMAL);
       ret = (DWORD) ShellSmallIconList;
     }
     else
     { TRACE(shell,"set to big icon\n");
       psfi->hIcon=ImageList_GetIcon(ShellBigIconList,0,ILD_NORMAL);
       ret = (DWORD) ShellBigIconList;
     }      
  }

  if (flags & SHGFI_SYSICONINDEX)
  {  FIXME(shell,"get the SYSICONINDEX\n");
     psfi->iIcon=1;
     if (flags & SHGFI_SMALLICON)
     { TRACE(shell,"set to small icon\n"); 
       ret = (DWORD) ShellSmallIconList;
     }
     else        
     { TRACE(shell,"set to big icon\n");
       ret = (DWORD) ShellBigIconList;
     }
  }

 
  return ret;
}

/*************************************************************************
 *				SHAppBarMessage32	[SHELL32.207]
 */
UINT32 WINAPI SHAppBarMessage32(DWORD msg, PAPPBARDATA data)
{ FIXME(shell,"(0x%08lx,%p): stub\n", msg, data);
#if 0
  switch (msg)
  { case ABM_ACTIVATE:
        case ABM_GETAUTOHIDEBAR:
        case ABM_GETSTATE:
        case ABM_GETTASKBARPOS:
        case ABM_NEW:
        case ABM_QUERYPOS:
        case ABM_REMOVE:
        case ABM_SETAUTOHIDEBAR:
        case ABM_SETPOS:
        case ABM_WINDOWPOSCHANGED:
	    ;
    }
#endif
    return 0;
}

/*************************************************************************
 *				CommandLineToArgvW	[SHELL32.7]
 */
LPWSTR* WINAPI CommandLineToArgvW(LPWSTR cmdline,LPDWORD numargs)
{ LPWSTR  *argv,s,t;
	int	i;
  TRACE(shell,"\n");

        /* to get writeable copy */
	cmdline = HEAP_strdupW( GetProcessHeap(), 0, cmdline);
	s=cmdline;i=0;
  while (*s)
  { /* space */
    if (*s==0x0020) 
    { i++;
			s++;
			while (*s && *s==0x0020)
				s++;
			continue;
		}
		s++;
	}
	argv=(LPWSTR*)HeapAlloc( GetProcessHeap(), 0, sizeof(LPWSTR)*(i+1) );
	s=t=cmdline;
	i=0;
  while (*s)
  { if (*s==0x0020)
    { *s=0;
			argv[i++]=HEAP_strdupW( GetProcessHeap(), 0, t );
			*s=0x0020;
			while (*s && *s==0x0020)
				s++;
			if (*s)
				t=s+1;
			else
				t=s;
			continue;
		}
		s++;
	}
	if (*t)
		argv[i++]=(LPWSTR)HEAP_strdupW( GetProcessHeap(), 0, t );
	HeapFree( GetProcessHeap(), 0, cmdline );
	argv[i]=NULL;
	*numargs=i;
	return argv;
}

/*************************************************************************
 *				Control_RunDLL		[SHELL32.12]
 *
 * Wild speculation in the following!
 *
 * http://premium.microsoft.com/msdn/library/techart/msdn193.htm
 */

void WINAPI Control_RunDLL (HWND32 hwnd, LPCVOID code, LPCSTR cmd, DWORD arg4)
{ FIXME(shell, "(%08x, %p, \"%s\", %08lx)\n",
	hwnd, code ? code : "(null)", cmd ? cmd : "(null)", arg4);
}

/*************************************************************************
 * FreeIconList
 */
void WINAPI FreeIconList( DWORD dw )
{ FIXME(shell, "(%lx): stub\n",dw);
}

/*************************************************************************
 * SHELL32_DllGetClassObject   [SHELL32.128]
 *
 * [Standart OLE/COM Interface Method]
 * This Function retrives the pointer to a specified interface (iid) of
 * a given class (rclsid).
 * With this pointer it's possible to call the IClassFactory_CreateInstance
 * method to get a instance of the requested Class.
 * This function does NOT instantiate the Class!!!
 * 
 * RETURNS
 *   HRESULT
 *
 */
DWORD WINAPI SHELL32_DllGetClassObject(REFCLSID rclsid,REFIID iid,LPVOID *ppv)
{ HRESULT	hres = E_OUTOFMEMORY;
  LPCLASSFACTORY lpclf;

  char	xclsid[50],xiid[50];
  WINE_StringFromCLSID((LPCLSID)rclsid,xclsid);
  WINE_StringFromCLSID((LPCLSID)iid,xiid);
  TRACE(shell,"\n\tCLSID:\t%s,\n\tIID:\t%s\n",xclsid,xiid);
	
  *ppv = NULL;
	if(IsEqualCLSID(rclsid, &CLSID_ShellDesktop)|| 
	   IsEqualCLSID(rclsid, &CLSID_ShellLink))
	{ if(IsEqualCLSID(rclsid, &CLSID_ShellDesktop))      /*debug*/
	    TRACE(shell,"requested CLSID_ShellDesktop\n");
	  if(IsEqualCLSID(rclsid, &CLSID_ShellLink))         /*debug*/
	    TRACE(shell,"requested CLSID_ShellLink\n");

	  lpclf = IClassFactory_Constructor();
    if(lpclf)
    { hres = lpclf->lpvtbl->fnQueryInterface(lpclf,iid, ppv);
		  lpclf->lpvtbl->fnRelease(lpclf);
		}
	}
	else
  { WARN(shell, "clsid(%s) not in buildin SHELL32\n",xclsid);
    hres = CLASS_E_CLASSNOTAVAILABLE;
	}
  TRACE(shell,"RETURN pointer to interface: %p\n",ppv);
  return hres;
}

/*************************************************************************
 *  SHGetDesktopFolder		[SHELL32.216]
 * 
 *  SDK header win95/shlobj.h: This is equivalent to call CoCreateInstance with
 *  CLSID_ShellDesktop
 *  CoCreateInstance(CLSID_Desktop, NULL, CLSCTX_INPROC, IID_IShellFolder, &pshf);
 *
 * RETURNS
 *   the interface to the shell desktop folder.
 *
 * FIXME
 *   the pdesktopfolder has to be released at the end (at dll unloading???)
 */
LPSHELLFOLDER pdesktopfolder=NULL;

DWORD WINAPI SHGetDesktopFolder(LPSHELLFOLDER *shellfolder)
{ HRESULT	hres = E_OUTOFMEMORY;
  LPCLASSFACTORY lpclf;
	TRACE(shell,"%p->(%p)\n",shellfolder,*shellfolder);

  if (pdesktopfolder)
	{	hres = NOERROR;
	}
	else
  { lpclf = IClassFactory_Constructor();
    /* fixme: the buildin IClassFactory_Constructor is at the moment only 
 		for rclsid=CLSID_ShellDesktop, so we get the right Interface (jsch)*/
    if(lpclf)
    { hres = lpclf->lpvtbl->fnCreateInstance(lpclf,NULL,(REFIID)&IID_IShellFolder, (void*)&pdesktopfolder);
	 	  lpclf->lpvtbl->fnRelease(lpclf);
	  }  
  }
	
  if (pdesktopfolder)
	{ *shellfolder = pdesktopfolder;
    pdesktopfolder->lpvtbl->fnAddRef(pdesktopfolder);
	}
  else
	{ *shellfolder=NULL;
	}	

  TRACE(shell,"-- %p->(%p)\n",shellfolder, *shellfolder);
	return hres;
}

/*************************************************************************
 *			 SHGetMalloc			[SHELL32.220]
 * returns the interface to shell malloc.
 *
 * [SDK header win95/shlobj.h:
 * equivalent to:  #define SHGetMalloc(ppmem)   CoGetMalloc(MEMCTX_TASK, ppmem)
 * ]
 * What we are currently doing is not very wrong, since we always use the same
 * heap (ProcessHeap).
 */
DWORD WINAPI SHGetMalloc(LPMALLOC32 *lpmal) 
{	TRACE(shell,"(%p)\n", lpmal);
	return CoGetMalloc32(0,lpmal);
}

/*************************************************************************
 *			 SHGetSpecialFolderLocation	[SHELL32.223]
 * gets the folder locations from the registry and creates a pidl
 * creates missing reg keys and directorys
 * 
 * PARAMS
 *   hwndOwner [I]
 *   nFolder   [I] CSIDL_xxxxx
 *   ppidl     [O] PIDL of a special folder
 *
 * RETURNS
 *    HResult
 *
 * FIXME
 *   - look for "User Shell Folder" first
 *
 */
HRESULT WINAPI SHGetSpecialFolderLocation(HWND32 hwndOwner, INT32 nFolder, LPITEMIDLIST * ppidl)
{	LPSHELLFOLDER shellfolder;
  DWORD  pchEaten,tpathlen=MAX_PATH,type,dwdisp,res;
	CHAR   pszTemp[256],buffer[256],tpath[MAX_PATH],npath[MAX_PATH];
	LPWSTR lpszDisplayName = (LPWSTR)&pszTemp[0];
	HKEY	 key;

  enum 
	{	FT_UNKNOWN= 0x00000000,
	  FT_DIR=     0x00000001, 
	  FT_DESKTOP= 0x00000002
	} tFolder; 

  TRACE(shell,"(%04x,%d,%p)\n", hwndOwner,nFolder,ppidl);

  strcpy(buffer,"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Folders\\");

  res=RegCreateKeyEx32A(HKEY_CURRENT_USER,buffer,0,NULL,REG_OPTION_NON_VOLATILE,KEY_WRITE,NULL,&key,&dwdisp);
	if (res)
	{ ERR(shell,"Could not create key %s %08lx \n",buffer,res);
	  return E_OUTOFMEMORY;
	}

	tFolder=FT_DIR;	
	switch (nFolder)
	{	case CSIDL_BITBUCKET:
			strcpy (buffer,"xxx");			/*not in the registry*/
			TRACE (shell,"looking for Recycler\n");
			tFolder=FT_UNKNOWN;
      break;
		case CSIDL_CONTROLS:
			strcpy (buffer,"xxx");			/*virtual folder*/
      TRACE (shell,"looking for Control\n");
			tFolder=FT_UNKNOWN;
      break;
		case CSIDL_DESKTOP:
			strcpy (buffer,"xxx");			/*virtual folder*/
			TRACE (shell,"looking for Desktop\n");
			tFolder=FT_DESKTOP;			
      break;
		case CSIDL_DESKTOPDIRECTORY:
			strcpy (buffer,"Desktop");
      break;
		case CSIDL_DRIVES:
			strcpy (buffer,"xxx");			/*virtual folder*/
      TRACE (shell,"looking for Drives\n");
			tFolder=FT_UNKNOWN;
      break;
		case CSIDL_FONTS:
			strcpy (buffer,"Fonts");			
      break;
		case CSIDL_NETHOOD:
			strcpy (buffer,"NetHood");			
      break;
		case CSIDL_NETWORK:
			strcpy (buffer,"xxx");				/*virtual folder*/
			TRACE (shell,"looking for Network\n");
			tFolder=FT_UNKNOWN;
      break;
		case CSIDL_PERSONAL:
			strcpy (buffer,"Personal");			
      break;
		case CSIDL_FAVORITES:
			strcpy (buffer,"Favorites");			
      break;
		case CSIDL_PRINTERS:
			strcpy (buffer,"PrintHood");			
      break;
		case CSIDL_PROGRAMS:
			strcpy (buffer,"Programs");			
      break;
		case CSIDL_RECENT:
			strcpy (buffer,"Recent");
      break;
		case CSIDL_SENDTO:
			strcpy (buffer,"SendTo");
 		  break;
		case CSIDL_STARTMENU:
			strcpy (buffer,"Start Menu");
      break;
		case CSIDL_STARTUP:
			strcpy (buffer,"Startup");			
      break;
		case CSIDL_TEMPLATES:
			strcpy (buffer,"Templates");			
      break;
		default:
      ERR (shell,"unknown CSIDL\n");
			tFolder=FT_UNKNOWN;			
      break;
	}

  TRACE(shell,"Key=%s\n",buffer);

  type=REG_SZ;

  switch (tFolder)
	{ case FT_DIR:
	    /* Directory: get the value from the registry, if its not there 
			create it and the directory*/
    	if (RegQueryValueEx32A(key,buffer,NULL,&type,tpath,&tpathlen))
  	  { GetWindowsDirectory32A(npath,MAX_PATH);
	      PathAddBackslash(npath);
  			switch (nFolder)
  			{	case CSIDL_DESKTOPDIRECTORY:
      			strcat (npath,"Desktop");
            break;
      		case CSIDL_FONTS:
      			strcat (npath,"Fonts");			
            break;
      		case CSIDL_NETHOOD:
      			strcat (npath,"NetHood");			
            break;
  		    case CSIDL_PERSONAL:
      			strcpy (npath,"C:\\Personal");			
            break;
      		case CSIDL_FAVORITES:
      			strcat (npath,"Favorites");			
            break;
  		    case CSIDL_PRINTERS:
      			strcat (npath,"PrintHood");			
            break;
      		case CSIDL_PROGRAMS:
      			strcat (npath,"Start Menu");			
  					CreateDirectory32A(npath,NULL);
      			strcat (npath,"\\Programs");			
            break;
      		case CSIDL_RECENT:
      			strcat (npath,"Recent");
            break;
      		case CSIDL_SENDTO:
      			strcat (npath,"SendTo");
      			break;
      		case CSIDL_STARTMENU:
      			strcat (npath,"Start Menu");
            break;
      		case CSIDL_STARTUP:
      			strcat (npath,"Start Menu");			
  					CreateDirectory32A(npath,NULL);
      			strcat (npath,"\\Startup");			
            break;
      		case CSIDL_TEMPLATES:
      			strcat (npath,"Templates");			
            break;
  				default:
         	  RegCloseKey(key);
        	  return E_OUTOFMEMORY;
  			}
    		if (RegSetValueEx32A(key,buffer,0,REG_SZ,npath,sizeof(npath)+1))
        {	ERR(shell,"could not create value %s\n",buffer);
      	  RegCloseKey(key);
      	  return E_OUTOFMEMORY;
    		}
    		TRACE(shell,"value %s=%s created\n",buffer,npath);
    	  CreateDirectory32A(npath,NULL);
      }
			break;
		case FT_DESKTOP:
			strcpy (tpath,"Desktop");			
		  break;
	  default:
      RegCloseKey(key);
      return E_OUTOFMEMORY;
		  break;
  }

	RegCloseKey(key);

  TRACE(shell,"Value=%s\n",tpath);
  LocalToWideChar32(lpszDisplayName, tpath, 256);
  
	if (SHGetDesktopFolder(&shellfolder)==S_OK)
	{ shellfolder->lpvtbl->fnParseDisplayName(shellfolder,hwndOwner, NULL,lpszDisplayName,&pchEaten,ppidl,NULL);
	  shellfolder->lpvtbl->fnRelease(shellfolder);
	}

	TRACE(shell, "-- (new pidl %p)\n",*ppidl);
	return NOERROR;
}

/*************************************************************************
 * SHGetPathFromIDList32A        [SHELL32.261][NT 4.0: SHELL32.220]
 *
 * PARAMETERS
 *  pidl,   [IN] pidl 
 *  pszPath [OUT] path
 *
 * RETURNS 
 *  path from a passed PIDL.
 *
 * NOTES
 *     exported by name
 *
 * FIXME
 *  fnGetDisplayNameOf can return different types of OLEString
 */
DWORD WINAPI SHGetPathFromIDList32A (LPCITEMIDLIST pidl,LPSTR pszPath)
{	STRRET lpName;
	LPSHELLFOLDER shellfolder;
  CHAR  buffer[MAX_PATH],tpath[MAX_PATH];
  DWORD type,tpathlen=MAX_PATH,dwdisp;
  HKEY  key;

	TRACE(shell,"(pidl=%p,%p)\n",pidl,pszPath);

  if (!pidl)
  {  strcpy(buffer,"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Folders\\");

     if (RegCreateKeyEx32A(HKEY_CURRENT_USER,buffer,0,NULL,REG_OPTION_NON_VOLATILE,KEY_WRITE,NULL,&key,&dwdisp))
     { return E_OUTOFMEMORY;
     }
     type=REG_SZ;    
     strcpy (buffer,"Desktop");					/*registry name*/
     if ( RegQueryValueEx32A(key,buffer,NULL,&type,tpath,&tpathlen))
     { GetWindowsDirectory32A(tpath,MAX_PATH);
       PathAddBackslash(tpath);
       strcat (tpath,"Desktop");				/*folder name*/
       RegSetValueEx32A(key,buffer,0,REG_SZ,tpath,tpathlen);
       CreateDirectory32A(tpath,NULL);
     }
     RegCloseKey(key);
     strcpy(pszPath,tpath);
  }
  else
  { if (SHGetDesktopFolder(&shellfolder)==S_OK)
	{ shellfolder->lpvtbl->fnGetDisplayNameOf(shellfolder,pidl,SHGDN_FORPARSING,&lpName);
	  shellfolder->lpvtbl->fnRelease(shellfolder);
	}
  /*WideCharToLocal32(pszPath, lpName.u.pOleStr, MAX_PATH);*/
	strcpy(pszPath,lpName.u.cStr);
	/* fixme free the olestring*/
  }
	TRACE(shell,"-- (%s)\n",pszPath);
	return NOERROR;
}
/*************************************************************************
 * SHGetPathFromIDList32W [SHELL32.262]
 */
DWORD WINAPI SHGetPathFromIDList32W (LPCITEMIDLIST pidl,LPWSTR pszPath)
{ FIXME (shell,"(pidl=%p %s):stub.\n", pidl, debugstr_w(pszPath));
  return 0;
}

/*************************************************************************
 *			 SHGetPathFromIDList		[SHELL32.221][NT 4.0: SHELL32.219]
 */
BOOL32 WINAPI SHGetPathFromIDList32(LPCITEMIDLIST pidl,LPSTR pszPath)     
{ TRACE(shell,"(pidl=%p,%p)\n",pidl,pszPath);
  return SHGetPathFromIDList32A(pidl,pszPath);
}

/*************************************************************************
 * SHHelpShortcuts_RunDLL [SHELL32.224]
 *
 */
DWORD WINAPI SHHelpShortcuts_RunDLL (DWORD dwArg1, DWORD dwArg2, DWORD dwArg3, DWORD dwArg4)
{ FIXME (exec, "(%lx, %lx, %lx, %lx) empty stub!\n",
	dwArg1, dwArg2, dwArg3, dwArg4);

  return 0;
}

/*************************************************************************
 * SHLoadInProc [SHELL32.225]
 *
 */

DWORD WINAPI SHLoadInProc (DWORD dwArg1)
{ FIXME (shell, "(%lx) empty stub!\n", dwArg1);
    return 0;
}

/*************************************************************************
 * SHBrowseForFolderA [SHELL32.209]
 *
 */
LPITEMIDLIST WINAPI SHBrowseForFolder32A (LPBROWSEINFO32A lpbi)
{ FIXME (shell, "(%lx,%s) empty stub!\n", (DWORD)lpbi, lpbi->lpszTitle);
  return NULL;
}

/*************************************************************************
 * SHELL32 LibMain
 *
 * FIXME
 *  at the moment the icons are extracted from shell32.dll
 */
HINSTANCE32 shell32_hInstance; 

BOOL32 WINAPI Shell32LibMain(HINSTANCE32 hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{ HICON32 htmpIcon;
  UINT32 iiconindex;
  UINT32 index;
  CHAR   szShellPath[MAX_PATH];
  
  TRACE(shell,"0x%x 0x%lx %p\n", hinstDLL, fdwReason, lpvReserved);

  shell32_hInstance = hinstDLL;
  
  GetWindowsDirectory32A(szShellPath,MAX_PATH);
  PathAddBackslash(szShellPath);
  strcat(szShellPath,"system\\shell32.dll");
       
  if (fdwReason==DLL_PROCESS_ATTACH)
  { if ( ! ShellSmallIconList )
    { if ( (ShellSmallIconList = ImageList_Create(16,16,ILC_COLOR,64,16)) )
      { for (index=0;index < 40; index++)
        { if ( ( htmpIcon = ExtractIcon32A(hinstDLL, szShellPath, index)) )
          { iiconindex = ImageList_AddIcon (ShellSmallIconList, htmpIcon);
          }
          else
          { ERR(shell,"could not initialize iconlist (is shell32.dll in the system directory?)\n");
            break;
          }
        }
      }
    }
    if ( ! ShellBigIconList )
    { if ( (ShellBigIconList = ImageList_Create(32,32,ILC_COLOR,64,16)) )
      { for (index=0;index < 40; index++)
        { if ( (htmpIcon = ExtractIcon32A( hinstDLL, szShellPath, index)) )
          { iiconindex = ImageList_AddIcon (ShellBigIconList, htmpIcon);
          }
          else
          { ERR(shell,"could not initialize iconlist (is shell32.dll in the system directory?)\n");
            break;
          }
        }
      }
    }
  }
  return TRUE;
}
