/*
 * Win32 kernel functions
 *
 * Copyright 1995 Martin von Loewis, Sven Verdoolaege, and Cameron Heide
 */

#include "config.h"
#include "wine/port.h"

#include <errno.h>
#ifdef HAVE_SYS_ERRNO_H
#include <sys/errno.h>
#endif
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifdef HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include "winbase.h"
#include "winerror.h"
#include "file.h"
#include "heap.h"
#include "debugtools.h"

DEFAULT_DEBUG_CHANNEL(file);

/***********************************************************************
 *              ReadFileEx                (KERNEL32.@)
 */
BOOL WINAPI ReadFileEx(HFILE hFile, LPVOID lpBuffer, DWORD numtoread,
			 LPOVERLAPPED lpOverlapped, 
			 LPOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine)
{

    FIXME("file %d to buf %p num %ld %p func %p stub\n",
	  hFile, lpBuffer, numtoread, lpOverlapped, lpCompletionRoutine);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return 0;
}

/**************************************************************************
 *              SetFileAttributes	(KERNEL.421)
 */
BOOL16 WINAPI SetFileAttributes16( LPCSTR lpFileName, DWORD attributes )
{
    return SetFileAttributesA( lpFileName, attributes );
}


/**************************************************************************
 *              SetFileAttributesA	(KERNEL32.@)
 */
BOOL WINAPI SetFileAttributesA(LPCSTR lpFileName, DWORD attributes)
{
    struct stat buf;
    DOS_FULL_NAME full_name;

    if (!DOSFS_GetFullName( lpFileName, TRUE, &full_name ))
        return FALSE;

    TRACE("(%s,%lx)\n",lpFileName,attributes);
    if (attributes & FILE_ATTRIBUTE_NORMAL) {
      attributes &= ~FILE_ATTRIBUTE_NORMAL;
      if (attributes)
        FIXME("(%s):%lx illegal combination with FILE_ATTRIBUTE_NORMAL.\n",
	      lpFileName,attributes);
    }
    if(stat(full_name.long_name,&buf)==-1)
    {
        FILE_SetDosError();
        return FALSE;
    }
    if (attributes & FILE_ATTRIBUTE_READONLY)
    {
        if(S_ISDIR(buf.st_mode))
            /* FIXME */
            WARN("FILE_ATTRIBUTE_READONLY ignored for directory.\n");
        else
            buf.st_mode &= ~0222; /* octal!, clear write permission bits */
        attributes &= ~FILE_ATTRIBUTE_READONLY;
    }
    else
    {
        /* add write permission */
        buf.st_mode |= 0600 | ((buf.st_mode & 044) >> 1);
    }
    if (attributes & FILE_ATTRIBUTE_DIRECTORY)
    {
        if (!S_ISDIR(buf.st_mode))
            FIXME("SetFileAttributes expected the file '%s' to be a directory",
                  lpFileName);
	attributes &= ~FILE_ATTRIBUTE_DIRECTORY;
    }
    attributes &= ~(FILE_ATTRIBUTE_ARCHIVE|FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM);
    if (attributes)
        FIXME("(%s):%lx attribute(s) not implemented.\n",
	      lpFileName,attributes);
    if (-1==chmod(full_name.long_name,buf.st_mode))
    {
        FILE_SetDosError();
        MESSAGE("Wine ERROR: Couldn't set file attributes for existing file \"%s\".\n"
                "Check permissions or set VFAT \"quiet\" mount flag\n", full_name.long_name);
        return TRUE;
    }
    return TRUE;
}


/**************************************************************************
 *              SetFileAttributesW	(KERNEL32.@)
 */
BOOL WINAPI SetFileAttributesW(LPCWSTR lpFileName, DWORD attributes)
{
    LPSTR afn = HEAP_strdupWtoA( GetProcessHeap(), 0, lpFileName );
    BOOL res = SetFileAttributesA( afn, attributes );
    HeapFree( GetProcessHeap(), 0, afn );
    return res;
}
