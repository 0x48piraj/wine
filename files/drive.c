/*
 * DOS drives handling functions
 *
 * Copyright 1993 Erik Bos
 * Copyright 1996 Alexandre Julliard
 *
 * Label & serial number read support.
 *  (c) 1999 Petr Tomasek <tomasek@etf.cuni.cz>
 *  (c) 2000 Andreas Mohr (changes)
 *
 */

#include "config.h"

#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

#ifdef HAVE_SYS_PARAM_H
# include <sys/param.h>
#endif
#ifdef STATFS_DEFINED_BY_SYS_VFS
# include <sys/vfs.h>
#else
# ifdef STATFS_DEFINED_BY_SYS_MOUNT
#  include <sys/mount.h>
# else
#  ifdef STATFS_DEFINED_BY_SYS_STATFS
#   include <sys/statfs.h>
#  endif
# endif
#endif

#include "winbase.h"
#include "ntddk.h"
#include "wine/winbase16.h"   /* for GetCurrentTask */
#include "winerror.h"
#include "drive.h"
#include "cdrom.h"
#include "file.h"
#include "heap.h"
#include "msdos.h"
#include "options.h"
#include "wine/port.h"
#include "task.h"
#include "debugtools.h"

DEFAULT_DEBUG_CHANNEL(dosfs);
DECLARE_DEBUG_CHANNEL(file);

typedef struct
{
    char     *root;      /* root dir in Unix format without trailing / */
    char     *dos_cwd;   /* cwd in DOS format without leading or trailing \ */
    char     *unix_cwd;  /* cwd in Unix format without leading or trailing / */
    char     *device;    /* raw device path */
    char      label_conf[12]; /* drive label as cfg'd in wine config */
    char      label_read[12]; /* drive label as read from device */
    DWORD     serial_conf;    /* drive serial number as cfg'd in wine config */
    UINT      type;      /* drive type */
    UINT      flags;     /* drive flags */
    dev_t     dev;       /* unix device number */
    ino_t     ino;       /* unix inode number */
} DOSDRIVE;


static const char * const DRIVE_Types[] =
{
    "",         /* DRIVE_UNKNOWN */
    "",         /* DRIVE_NO_ROOT_DIR */
    "floppy",   /* DRIVE_REMOVABLE */
    "hd",       /* DRIVE_FIXED */
    "network",  /* DRIVE_REMOTE */
    "cdrom",    /* DRIVE_CDROM */
    "ramdisk"   /* DRIVE_RAMDISK */
};


/* Known filesystem types */

typedef struct
{
    const char *name;
    UINT      flags;
} FS_DESCR;

static const FS_DESCR DRIVE_Filesystems[] =
{
    { "unix",   DRIVE_CASE_SENSITIVE | DRIVE_CASE_PRESERVING },
    { "msdos",  DRIVE_SHORT_NAMES },
    { "dos",    DRIVE_SHORT_NAMES },
    { "fat",    DRIVE_SHORT_NAMES },
    { "vfat",   DRIVE_CASE_PRESERVING },
    { "win95",  DRIVE_CASE_PRESERVING },
    { NULL, 0 }
};


static DOSDRIVE DOSDrives[MAX_DOS_DRIVES];
static int DRIVE_CurDrive = -1;

static HTASK16 DRIVE_LastTask = 0;


/***********************************************************************
 *           DRIVE_GetDriveType
 */
static UINT DRIVE_GetDriveType( const char *name )
{
    char buffer[20];
    int i;

    PROFILE_GetWineIniString( name, "Type", "hd", buffer, sizeof(buffer) );
    for (i = 0; i < sizeof(DRIVE_Types)/sizeof(DRIVE_Types[0]); i++)
    {
        if (!strcasecmp( buffer, DRIVE_Types[i] )) return i;
    }
    MESSAGE("%s: unknown drive type '%s', defaulting to 'hd'.\n",
	name, buffer );
    return DRIVE_FIXED;
}


/***********************************************************************
 *           DRIVE_GetFSFlags
 */
static UINT DRIVE_GetFSFlags( const char *name, const char *value )
{
    const FS_DESCR *descr;

    for (descr = DRIVE_Filesystems; descr->name; descr++)
        if (!strcasecmp( value, descr->name )) return descr->flags;
    MESSAGE("%s: unknown filesystem type '%s', defaulting to 'win95'.\n",
	name, value );
    return DRIVE_CASE_PRESERVING;
}


/***********************************************************************
 *           DRIVE_Init
 */
int DRIVE_Init(void)
{
    int i, len, count = 0;
    char name[] = "Drive A";
    char drive_env[] = "=A:";
    char path[MAX_PATHNAME_LEN];
    char buffer[80];
    struct stat drive_stat_buffer;
    char *p;
    DOSDRIVE *drive;

    for (i = 0, drive = DOSDrives; i < MAX_DOS_DRIVES; i++, name[6]++, drive++)
    {
        PROFILE_GetWineIniString( name, "Path", "", path, sizeof(path)-1 );
        if (path[0])
        {
            p = path + strlen(path) - 1;
            while ((p > path) && ((*p == '/') || (*p == '\\'))) *p-- = '\0';
            if (!path[0]) strcpy( path, "/" );

            if (stat( path, &drive_stat_buffer ))
            {
                MESSAGE("Could not stat %s (%s), ignoring drive %c:\n",
                    path, strerror(errno), 'A' + i);
                continue;
            }
            if (!S_ISDIR(drive_stat_buffer.st_mode))
            {
                MESSAGE("%s is not a directory, ignoring drive %c:\n",
		    path, 'A' + i );
                continue;
            }

            drive->root = HEAP_strdupA( GetProcessHeap(), 0, path );
            drive->dos_cwd  = HEAP_strdupA( GetProcessHeap(), 0, "" );
            drive->unix_cwd = HEAP_strdupA( GetProcessHeap(), 0, "" );
            drive->type     = DRIVE_GetDriveType( name );
            drive->device   = NULL;
            drive->flags    = 0;
            drive->dev      = drive_stat_buffer.st_dev;
            drive->ino      = drive_stat_buffer.st_ino;

            /* Get the drive label */
            PROFILE_GetWineIniString( name, "Label", "", drive->label_conf, 12 );
            if ((len = strlen(drive->label_conf)) < 11)
            {
                /* Pad label with spaces */
                memset( drive->label_conf + len, ' ', 11 - len );
                drive->label_conf[11] = '\0';
            }

            /* Get the serial number */
            PROFILE_GetWineIniString( name, "Serial", "12345678",
                                      buffer, sizeof(buffer) );
            drive->serial_conf = strtoul( buffer, NULL, 16 );

            /* Get the filesystem type */
            PROFILE_GetWineIniString( name, "Filesystem", "win95",
                                      buffer, sizeof(buffer) );
            drive->flags = DRIVE_GetFSFlags( name, buffer );

            /* Get the device */
            PROFILE_GetWineIniString( name, "Device", "",
                                      buffer, sizeof(buffer) );
            if (buffer[0])
	    {
                drive->device = HEAP_strdupA( GetProcessHeap(), 0, buffer );
		if (PROFILE_GetWineIniBool( name, "ReadVolInfo", 1))
                    drive->flags |= DRIVE_READ_VOL_INFO;
	    }

            /* Get the FailReadOnly flag */
            if (PROFILE_GetWineIniBool( name, "FailReadOnly", 0 ))
                drive->flags |= DRIVE_FAIL_READ_ONLY;

            /* Make the first hard disk the current drive */
            if ((DRIVE_CurDrive == -1) && (drive->type == DRIVE_FIXED))
                DRIVE_CurDrive = i;

            count++;
            TRACE("%s: path=%s type=%s label='%s' serial=%08lx "
                  "flags=%08x dev=%x ino=%x\n",
                  name, path, DRIVE_Types[drive->type],
                  drive->label_conf, drive->serial_conf, drive->flags,
                  (int)drive->dev, (int)drive->ino );
        }
        else WARN("%s: not defined\n", name );
    }

    if (!count) 
    {
        MESSAGE("Warning: no valid DOS drive found, check your configuration file.\n" );
        /* Create a C drive pointing to Unix root dir */
        DOSDrives[2].root     = HEAP_strdupA( GetProcessHeap(), 0, "/" );
        DOSDrives[2].dos_cwd  = HEAP_strdupA( GetProcessHeap(), 0, "" );
        DOSDrives[2].unix_cwd = HEAP_strdupA( GetProcessHeap(), 0, "" );
        strcpy( DOSDrives[2].label_conf, "Drive C    " );
        DOSDrives[2].serial_conf   = 12345678;
        DOSDrives[2].type     = DRIVE_FIXED;
        DOSDrives[2].device   = NULL;
        DOSDrives[2].flags    = 0;
        DRIVE_CurDrive = 2;
    }

    /* Make sure the current drive is valid */
    if (DRIVE_CurDrive == -1)
    {
        for (i = 0, drive = DOSDrives; i < MAX_DOS_DRIVES; i++, drive++)
        {
            if (drive->root && !(drive->flags & DRIVE_DISABLED))
            {
                DRIVE_CurDrive = i;
                break;
            }
        }
    }

    /* get current working directory info for all drives */
    for (i = 0; i < MAX_DOS_DRIVES; i++, drive_env[1]++)
    {
        if (!GetEnvironmentVariableA(drive_env, path, sizeof(path))) continue;
        /* sanity check */
        if (toupper(path[0]) != drive_env[1] || path[1] != ':') continue;
        DRIVE_Chdir( i, path + 2 );
    }
    return 1;
}


/***********************************************************************
 *           DRIVE_IsValid
 */
int DRIVE_IsValid( int drive )
{
    if ((drive < 0) || (drive >= MAX_DOS_DRIVES)) return 0;
    return (DOSDrives[drive].root &&
            !(DOSDrives[drive].flags & DRIVE_DISABLED));
}


/***********************************************************************
 *           DRIVE_GetCurrentDrive
 */
int DRIVE_GetCurrentDrive(void)
{
    TDB *pTask = TASK_GetCurrent();
    if (pTask && (pTask->curdrive & 0x80)) return pTask->curdrive & ~0x80;
    return DRIVE_CurDrive;
}


/***********************************************************************
 *           DRIVE_SetCurrentDrive
 */
int DRIVE_SetCurrentDrive( int drive )
{
    TDB *pTask = TASK_GetCurrent();
    if (!DRIVE_IsValid( drive ))
    {
        SetLastError( ERROR_INVALID_DRIVE );
        return 0;
    }
    TRACE("%c:\n", 'A' + drive );
    DRIVE_CurDrive = drive;
    if (pTask) pTask->curdrive = drive | 0x80;
    chdir(DRIVE_GetUnixCwd(drive));
    return 1;
}


/***********************************************************************
 *           DRIVE_FindDriveRoot
 *
 * Find a drive for which the root matches the beginning of the given path.
 * This can be used to translate a Unix path into a drive + DOS path.
 * Return value is the drive, or -1 on error. On success, path is modified
 * to point to the beginning of the DOS path.
 */
int DRIVE_FindDriveRoot( const char **path )
{
    /* idea: check at all '/' positions.
     * If the device and inode of that path is identical with the
     * device and inode of the current drive then we found a solution.
     * If there is another drive pointing to a deeper position in
     * the file tree, we want to find that one, not the earlier solution.
     */
    int drive, rootdrive = -1;
    char buffer[MAX_PATHNAME_LEN];
    char *next = buffer;
    const char *p = *path;
    struct stat st;

    strcpy( buffer, "/" );
    for (;;)
    {
        if (stat( buffer, &st ) || !S_ISDIR( st.st_mode )) break;

        /* Find the drive */

        for (drive = 0; drive < MAX_DOS_DRIVES; drive++)
        {
           if (!DOSDrives[drive].root ||
               (DOSDrives[drive].flags & DRIVE_DISABLED)) continue;

           if ((DOSDrives[drive].dev == st.st_dev) &&
               (DOSDrives[drive].ino == st.st_ino))
           {
               rootdrive = drive;
               *path = p;
	       break;
           }
        }

        /* Get the next path component */

        *next++ = '/';
        while ((*p == '/') || (*p == '\\')) p++;
        if (!*p) break;
        while (!IS_END_OF_NAME(*p)) *next++ = *p++;
        *next = 0;
    }
    *next = 0;

    if (rootdrive != -1)
        TRACE("%s -> drive %c:, root='%s', name='%s'\n",
              buffer, 'A' + rootdrive, DOSDrives[rootdrive].root, *path );
    return rootdrive;
}


/***********************************************************************
 *           DRIVE_GetRoot
 */
const char * DRIVE_GetRoot( int drive )
{
    if (!DRIVE_IsValid( drive )) return NULL;
    return DOSDrives[drive].root;
}


/***********************************************************************
 *           DRIVE_GetDosCwd
 */
const char * DRIVE_GetDosCwd( int drive )
{
    TDB *pTask = TASK_GetCurrent();
    if (!DRIVE_IsValid( drive )) return NULL;

    /* Check if we need to change the directory to the new task. */
    if (pTask && (pTask->curdrive & 0x80) &&    /* The task drive is valid */
        ((pTask->curdrive & ~0x80) == drive) && /* and it's the one we want */
        (DRIVE_LastTask != GetCurrentTask()))   /* and the task changed */
    {
        /* Perform the task-switch */
        if (!DRIVE_Chdir( drive, pTask->curdir )) DRIVE_Chdir( drive, "\\" );
        DRIVE_LastTask = GetCurrentTask();
    }
    return DOSDrives[drive].dos_cwd;
}


/***********************************************************************
 *           DRIVE_GetUnixCwd
 */
const char * DRIVE_GetUnixCwd( int drive )
{
    TDB *pTask = TASK_GetCurrent();
    if (!DRIVE_IsValid( drive )) return NULL;

    /* Check if we need to change the directory to the new task. */
    if (pTask && (pTask->curdrive & 0x80) &&    /* The task drive is valid */
        ((pTask->curdrive & ~0x80) == drive) && /* and it's the one we want */
        (DRIVE_LastTask != GetCurrentTask()))   /* and the task changed */
    {
        /* Perform the task-switch */
        if (!DRIVE_Chdir( drive, pTask->curdir )) DRIVE_Chdir( drive, "\\" );
        DRIVE_LastTask = GetCurrentTask();
    }
    return DOSDrives[drive].unix_cwd;
}


/***********************************************************************
 *           DRIVE_GetDevice
 */
const char * DRIVE_GetDevice( int drive )
{
    return (DRIVE_IsValid( drive )) ? DOSDrives[drive].device : NULL;
}


/***********************************************************************
 *           DRIVE_ReadSuperblock
 *
 * NOTE 
 *      DRIVE_SetLabel and DRIVE_SetSerialNumber use this in order
 * to check, that they are writing on a FAT filesystem !
 */
int DRIVE_ReadSuperblock (int drive, char * buff)
{
#define DRIVE_SUPER 96
    int fd;
    off_t offs;

    if (memset(buff,0,DRIVE_SUPER)!=buff) return -1;
    if ((fd=open(DOSDrives[drive].device,O_RDONLY)) == -1)
    {
	struct stat st;
	if (!DOSDrives[drive].device)
	    ERR("No device configured for drive %c: !\n", 'A'+drive);
	else
	    ERR("Couldn't open device '%s' for drive %c: ! (%s)\n", DOSDrives[drive].device, 'A'+drive,
		 (stat(DOSDrives[drive].device, &st)) ?
			"not available or symlink not valid ?" : "no permission");
	ERR("Can't read drive volume info ! Either pre-set it or make sure the device to read it from is accessible !\n");
	PROFILE_UsageWineIni();
	return -1;
    }

    switch(DOSDrives[drive].type)
    {
	case DRIVE_REMOVABLE:
	case DRIVE_FIXED:
	    offs = 0;
	    break;
	case DRIVE_CDROM:
	    offs = CDROM_Data_FindBestVoldesc(fd);
	    break;
        default:
            offs = 0;
            break;
    }

    if ((offs) && (lseek(fd,offs,SEEK_SET)!=offs)) return -4;
    if (read(fd,buff,DRIVE_SUPER)!=DRIVE_SUPER) return -2;

    switch(DOSDrives[drive].type)
    {
	case DRIVE_REMOVABLE:
	case DRIVE_FIXED:
	    if ((buff[0x26]!=0x29) ||  /* Check for FAT present */
                /* FIXME: do really all FAT have their name beginning with
                   "FAT" ? (At least FAT12, FAT16 and FAT32 have :) */
	    	memcmp( buff+0x36,"FAT",3))
            {
                ERR("The filesystem is not FAT !! (device=%s)\n",
                    DOSDrives[drive].device);
                return -3;
            }
            break;
	case DRIVE_CDROM:
	    if (strncmp(&buff[1],"CD001",5)) /* Check for iso9660 present */
		return -3;
	    /* FIXME: do we need to check for "CDROM", too ? (high sierra) */
		break;
	default:
		return -3;
		break;
    }

    return close(fd);
}


/***********************************************************************
 *           DRIVE_WriteSuperblockEntry
 *
 * NOTE
 *	We are writing as little as possible (ie. not the whole SuperBlock)
 * not to interfere with kernel. The drive can be mounted !
 */
int DRIVE_WriteSuperblockEntry (int drive, off_t ofs, size_t len, char * buff)
{
    int fd;

    if ((fd=open(DOSDrives[drive].device,O_WRONLY))==-1) 
    {
        ERR("Cannot open the device %s (for writing)\n",
            DOSDrives[drive].device); 
        return -1;
    }
    if (lseek(fd,ofs,SEEK_SET)!=ofs)
    {
        ERR("lseek failed on device %s !\n",
            DOSDrives[drive].device); 
        close(fd);
        return -2;
    }
    if (write(fd,buff,len)!=len) 
    {
        close(fd);
        ERR("Cannot write on %s !\n",
            DOSDrives[drive].device); 
        return -3;
    }
    return close (fd);
}



/***********************************************************************
 *           DRIVE_GetLabel
 */
const char * DRIVE_GetLabel( int drive )
{
    int read = 0;
    char buff[DRIVE_SUPER];
    int offs = -1;

    if (!DRIVE_IsValid( drive )) return NULL;
    if (DOSDrives[drive].type == DRIVE_CDROM)
    {
	read = CDROM_GetLabel(drive, DOSDrives[drive].label_read); 
    }
    else
    if (DOSDrives[drive].flags & DRIVE_READ_VOL_INFO)
    {
	if (DRIVE_ReadSuperblock(drive,(char *) buff))
	    ERR("Invalid or unreadable superblock on %s (%c:).\n",
		DOSDrives[drive].device, (char)(drive+'A'));
	else {
	    if (DOSDrives[drive].type == DRIVE_REMOVABLE ||
		DOSDrives[drive].type == DRIVE_FIXED)
		offs = 0x2b;

	    /* FIXME: ISO9660 uses a 32 bytes long label. Should we do also? */
	    if (offs != -1) memcpy(DOSDrives[drive].label_read,buff+offs,11);
	    DOSDrives[drive].label_read[11]='\0';
	    read = 1;
	}
    }

    return (read) ?
	DOSDrives[drive].label_read : DOSDrives[drive].label_conf;
}


/***********************************************************************
 *           DRIVE_GetSerialNumber
 */
DWORD DRIVE_GetSerialNumber( int drive )
{
    DWORD serial = 0;
char buff[DRIVE_SUPER];

    if (!DRIVE_IsValid( drive )) return 0;
    
    if (DOSDrives[drive].flags & DRIVE_READ_VOL_INFO)
    {
	switch(DOSDrives[drive].type)
	{
        case DRIVE_REMOVABLE:
        case DRIVE_FIXED:
            if (DRIVE_ReadSuperblock(drive,(char *) buff))
                MESSAGE("Invalid or unreadable superblock on %s (%c:)."
                        " Maybe not FAT?\n" ,
                        DOSDrives[drive].device, 'A'+drive);
            else
                serial = *((DWORD*)(buff+0x27));
            break;
        case DRIVE_CDROM:
            serial = CDROM_GetSerial(drive);
            break;
        default:
            FIXME("Serial number reading from file system on drive %c: not supported yet.\n", drive+'A');
	}
    }
		
    return (serial) ? serial : DOSDrives[drive].serial_conf;
}


/***********************************************************************
 *           DRIVE_SetSerialNumber
 */
int DRIVE_SetSerialNumber( int drive, DWORD serial )
{
    char buff[DRIVE_SUPER];

    if (!DRIVE_IsValid( drive )) return 0;

    if (DOSDrives[drive].flags & DRIVE_READ_VOL_INFO)
    {
        if ((DOSDrives[drive].type != DRIVE_REMOVABLE) &&
            (DOSDrives[drive].type != DRIVE_FIXED)) return 0;
        /* check, if the drive has a FAT filesystem */ 
        if (DRIVE_ReadSuperblock(drive, buff)) return 0;
        if (DRIVE_WriteSuperblockEntry(drive, 0x27, 4, (char *) &serial)) return 0;
        return 1;
    }

    if (DOSDrives[drive].type == DRIVE_CDROM) return 0;
    DOSDrives[drive].serial_conf = serial;
    return 1;
}


/***********************************************************************
 *           DRIVE_GetType
 */
static UINT DRIVE_GetType( int drive )
{
    if (!DRIVE_IsValid( drive )) return DRIVE_UNKNOWN;
    return DOSDrives[drive].type;
}


/***********************************************************************
 *           DRIVE_GetFlags
 */
UINT DRIVE_GetFlags( int drive )
{
    if ((drive < 0) || (drive >= MAX_DOS_DRIVES)) return 0;
    return DOSDrives[drive].flags;
}


/***********************************************************************
 *           DRIVE_Chdir
 */
int DRIVE_Chdir( int drive, const char *path )
{
    DOS_FULL_NAME full_name;
    char buffer[MAX_PATHNAME_LEN];
    LPSTR unix_cwd;
    BY_HANDLE_FILE_INFORMATION info;
    TDB *pTask = TASK_GetCurrent();

    strcpy( buffer, "A:" );
    buffer[0] += drive;
    TRACE("(%s,%s)\n", buffer, path );
    lstrcpynA( buffer + 2, path, sizeof(buffer) - 2 );

    if (!DOSFS_GetFullName( buffer, TRUE, &full_name )) return 0;
    if (!FILE_Stat( full_name.long_name, &info )) return 0;
    if (!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
    {
        SetLastError( ERROR_FILE_NOT_FOUND );
        return 0;
    }
    unix_cwd = full_name.long_name + strlen( DOSDrives[drive].root );
    while (*unix_cwd == '/') unix_cwd++;

    TRACE("(%c:): unix_cwd=%s dos_cwd=%s\n",
                   'A' + drive, unix_cwd, full_name.short_name + 3 );

    HeapFree( GetProcessHeap(), 0, DOSDrives[drive].dos_cwd );
    HeapFree( GetProcessHeap(), 0, DOSDrives[drive].unix_cwd );
    DOSDrives[drive].dos_cwd  = HEAP_strdupA( GetProcessHeap(), 0,
                                              full_name.short_name + 3 );
    DOSDrives[drive].unix_cwd = HEAP_strdupA( GetProcessHeap(), 0, unix_cwd );

    if (pTask && (pTask->curdrive & 0x80) && 
        ((pTask->curdrive & ~0x80) == drive))
    {
        lstrcpynA( pTask->curdir, full_name.short_name + 2,
                     sizeof(pTask->curdir) );
        DRIVE_LastTask = GetCurrentTask();
        chdir(unix_cwd); /* Only change if on current drive */
    }
    return 1;
}


/***********************************************************************
 *           DRIVE_Disable
 */
int DRIVE_Disable( int drive  )
{
    if ((drive < 0) || (drive >= MAX_DOS_DRIVES) || !DOSDrives[drive].root)
    {
        SetLastError( ERROR_INVALID_DRIVE );
        return 0;
    }
    DOSDrives[drive].flags |= DRIVE_DISABLED;
    return 1;
}


/***********************************************************************
 *           DRIVE_Enable
 */
int DRIVE_Enable( int drive  )
{
    if ((drive < 0) || (drive >= MAX_DOS_DRIVES) || !DOSDrives[drive].root)
    {
        SetLastError( ERROR_INVALID_DRIVE );
        return 0;
    }
    DOSDrives[drive].flags &= ~DRIVE_DISABLED;
    return 1;
}


/***********************************************************************
 *           DRIVE_SetLogicalMapping
 */
int DRIVE_SetLogicalMapping ( int existing_drive, int new_drive )
{
 /* If new_drive is already valid, do nothing and return 0
    otherwise, copy DOSDrives[existing_drive] to DOSDrives[new_drive] */
  
    DOSDRIVE *old, *new;
    
    old = DOSDrives + existing_drive;
    new = DOSDrives + new_drive;

    if ((existing_drive < 0) || (existing_drive >= MAX_DOS_DRIVES) ||
        !old->root ||
	(new_drive < 0) || (new_drive >= MAX_DOS_DRIVES))
    {
        SetLastError( ERROR_INVALID_DRIVE );
        return 0;
    }

    if ( new->root )
    {
        TRACE("Can't map drive %c: to already existing drive %c:\n",
              'A' + existing_drive, 'A' + new_drive );
	/* it is already mapped there, so return success */
	if (!strcmp(old->root,new->root))
	    return 1;
	return 0;
    }

    new->root = HEAP_strdupA( GetProcessHeap(), 0, old->root );
    new->dos_cwd = HEAP_strdupA( GetProcessHeap(), 0, old->dos_cwd );
    new->unix_cwd = HEAP_strdupA( GetProcessHeap(), 0, old->unix_cwd );
    new->device = HEAP_strdupA( GetProcessHeap(), 0, old->device );
    memcpy ( new->label_conf, old->label_conf, 12 );
    memcpy ( new->label_read, old->label_read, 12 );
    new->serial_conf = old->serial_conf;
    new->type = old->type;
    new->flags = old->flags;
    new->dev = old->dev;
    new->ino = old->ino;

    TRACE("Drive %c: is now equal to drive %c:\n",
          'A' + new_drive, 'A' + existing_drive );

    return 1;
}


/***********************************************************************
 *           DRIVE_OpenDevice
 *
 * Open the drive raw device and return a Unix fd (or -1 on error).
 */
int DRIVE_OpenDevice( int drive, int flags )
{
    if (!DRIVE_IsValid( drive )) return -1;
    return open( DOSDrives[drive].device, flags );
}


/***********************************************************************
 *           DRIVE_RawRead
 *
 * Read raw sectors from a device
 */
int DRIVE_RawRead(BYTE drive, DWORD begin, DWORD nr_sect, BYTE *dataptr, BOOL fake_success)
{
    int fd;

    if ((fd = DRIVE_OpenDevice( drive, O_RDONLY )) != -1)
    {
        lseek( fd, begin * 512, SEEK_SET );
        /* FIXME: check errors */
        read( fd, dataptr, nr_sect * 512 );
        close( fd );
    }
    else
    {
        memset(dataptr, 0, nr_sect * 512);
	if (fake_success)
        {
	    if (begin == 0 && nr_sect > 1) *(dataptr + 512) = 0xf8;
	    if (begin == 1) *dataptr = 0xf8;
	}
	else
	    return 0;
    }
    return 1;
}


/***********************************************************************
 *           DRIVE_RawWrite
 *
 * Write raw sectors to a device
 */
int DRIVE_RawWrite(BYTE drive, DWORD begin, DWORD nr_sect, BYTE *dataptr, BOOL fake_success)
{
    int fd;

    if ((fd = DRIVE_OpenDevice( drive, O_RDONLY )) != -1)
    {
        lseek( fd, begin * 512, SEEK_SET );
        /* FIXME: check errors */
        write( fd, dataptr, nr_sect * 512 );
        close( fd );
    }
    else
    if (!(fake_success))
	return 0;

    return 1;
}


/***********************************************************************
 *           DRIVE_GetFreeSpace
 */
static int DRIVE_GetFreeSpace( int drive, PULARGE_INTEGER size, 
			       PULARGE_INTEGER available )
{
    struct statfs info;

    if (!DRIVE_IsValid(drive))
    {
        SetLastError( ERROR_INVALID_DRIVE );
        return 0;
    }

/* FIXME: add autoconf check for this */
#if defined(__svr4__) || defined(_SCO_DS) || defined(__sun)
    if (statfs( DOSDrives[drive].root, &info, 0, 0) < 0)
#else
    if (statfs( DOSDrives[drive].root, &info) < 0)
#endif
    {
        FILE_SetDosError();
        WARN("cannot do statfs(%s)\n", DOSDrives[drive].root);
        return 0;
    }

    size->QuadPart = RtlEnlargedUnsignedMultiply( info.f_bsize, info.f_blocks );
#ifdef STATFS_HAS_BAVAIL
    available->QuadPart = RtlEnlargedUnsignedMultiply( info.f_bavail, info.f_bsize );
#else
# ifdef STATFS_HAS_BFREE
    available->QuadPart = RtlEnlargedUnsignedMultiply( info.f_bfree, info.f_bsize );
# else
#  error "statfs has no bfree/bavail member!"
# endif
#endif
    if (DOSDrives[drive].type == DRIVE_CDROM)
    { /* ALWAYS 0, even if no real CD-ROM mounted there !! */
        available->QuadPart = 0;
    }
    return 1;
}

/***********************************************************************
 *       DRIVE_GetCurrentDirectory
 * Returns "X:\\path\\etc\\".
 *
 * Despite the API description, return required length including the 
 * terminating null when buffer too small. This is the real behaviour.
*/

static UINT DRIVE_GetCurrentDirectory( UINT buflen, LPSTR buf )
{
    UINT ret;
    const char *s = DRIVE_GetDosCwd( DRIVE_GetCurrentDrive() );

    assert(s);
    ret = strlen(s) + 3; /* length of WHOLE current directory */
    if (ret >= buflen) return ret + 1;
    lstrcpynA( buf, "A:\\", min( 4, buflen ) );
    if (buflen) buf[0] += DRIVE_GetCurrentDrive();
    if (buflen > 3) lstrcpynA( buf + 3, s, buflen - 3 );
    return ret;
}


/***********************************************************************
 *           DRIVE_BuildEnv
 *
 * Build the environment array containing the drives current directories.
 * Resulting pointer must be freed with HeapFree.
 */
char *DRIVE_BuildEnv(void)
{
    int i, length = 0;
    const char *cwd[MAX_DOS_DRIVES];
    char *env, *p;

    for (i = 0; i < MAX_DOS_DRIVES; i++)
    {
        if ((cwd[i] = DRIVE_GetDosCwd(i)) && cwd[i][0]) length += strlen(cwd[i]) + 8;
    }
    if (!(env = HeapAlloc( GetProcessHeap(), 0, length+1 ))) return NULL;
    for (i = 0, p = env; i < MAX_DOS_DRIVES; i++)
    {
        if (cwd[i] && cwd[i][0])
            p += sprintf( p, "=%c:=%c:\\%s", 'A'+i, 'A'+i, cwd[i] ) + 1;
    }
    *p = 0;
    return env;
}


/***********************************************************************
 *           GetDiskFreeSpace16   (KERNEL.422)
 */
BOOL16 WINAPI GetDiskFreeSpace16( LPCSTR root, LPDWORD cluster_sectors,
                                  LPDWORD sector_bytes, LPDWORD free_clusters,
                                  LPDWORD total_clusters )
{
    return GetDiskFreeSpaceA( root, cluster_sectors, sector_bytes,
                                free_clusters, total_clusters );
}


/***********************************************************************
 *           GetDiskFreeSpaceA   (KERNEL32.@)
 *
 * Fails if expression resulting from current drive's dir and "root"
 * is not a root dir of the target drive.
 *
 * UNDOC: setting some LPDWORDs to NULL is perfectly possible 
 * if the corresponding info is unneeded.
 *
 * FIXME: needs to support UNC names from Win95 OSR2 on.
 *
 * Behaviour under Win95a:
 * CurrDir     root   result
 * "E:\\TEST"  "E:"   FALSE
 * "E:\\"      "E:"   TRUE
 * "E:\\"      "E"    FALSE
 * "E:\\"      "\\"   TRUE
 * "E:\\TEST"  "\\"   TRUE
 * "E:\\TEST"  ":\\"  FALSE
 * "E:\\TEST"  "E:\\" TRUE
 * "E:\\TEST"  ""     FALSE
 * "E:\\"      ""     FALSE (!)
 * "E:\\"      0x0    TRUE
 * "E:\\TEST"  0x0    TRUE  (!)
 * "E:\\TEST"  "C:"   TRUE  (when CurrDir of "C:" set to "\\")
 * "E:\\TEST"  "C:"   FALSE (when CurrDir of "C:" set to "\\TEST")
 */
BOOL WINAPI GetDiskFreeSpaceA( LPCSTR root, LPDWORD cluster_sectors,
                                   LPDWORD sector_bytes, LPDWORD free_clusters,
                                   LPDWORD total_clusters )
{
    int	drive, sec_size;
    ULARGE_INTEGER size,available;
    LPCSTR path;
    DWORD cluster_sec;

    if ((!root) || (strcmp(root,"\\") == 0))
	drive = DRIVE_GetCurrentDrive();
    else
    if ( (strlen(root) >= 2) && (root[1] == ':')) /* root contains drive tag */
    {
        drive = toupper(root[0]) - 'A';
	path = &root[2];
	if (path[0] == '\0')
	    path = DRIVE_GetDosCwd(drive);
	else
	if (path[0] == '\\')
	    path++;
	if (strlen(path)) /* oops, we are in a subdir */
	    return FALSE;
    }
    else
        return FALSE;

    if (!DRIVE_GetFreeSpace(drive, &size, &available)) return FALSE;

    /* Cap the size and available at 2GB as per specs.  */
    if ((size.s.HighPart) ||(size.s.LowPart > 0x7fffffff))
    {
	size.s.HighPart = 0;
	size.s.LowPart = 0x7fffffff;
    }
    if ((available.s.HighPart) ||(available.s.LowPart > 0x7fffffff))
    {
	available.s.HighPart =0;
	available.s.LowPart = 0x7fffffff;
    }
    sec_size = (DRIVE_GetType(drive)==DRIVE_CDROM) ? 2048 : 512;
    size.s.LowPart            /= sec_size;
    available.s.LowPart       /= sec_size;
    /* fixme: probably have to adjust those variables too for CDFS */
    cluster_sec = 1;
    while (cluster_sec * 65536 < size.s.LowPart) cluster_sec *= 2;

    if (cluster_sectors)
	*cluster_sectors = cluster_sec;
    if (sector_bytes)
	*sector_bytes    = sec_size;
    if (free_clusters)
	*free_clusters   = available.s.LowPart / cluster_sec;
    if (total_clusters)
	*total_clusters  = size.s.LowPart / cluster_sec;
    return TRUE;
}


/***********************************************************************
 *           GetDiskFreeSpaceW   (KERNEL32.@)
 */
BOOL WINAPI GetDiskFreeSpaceW( LPCWSTR root, LPDWORD cluster_sectors,
                                   LPDWORD sector_bytes, LPDWORD free_clusters,
                                   LPDWORD total_clusters )
{
    LPSTR xroot;
    BOOL ret;

    xroot = HEAP_strdupWtoA( GetProcessHeap(), 0, root);
    ret = GetDiskFreeSpaceA( xroot,cluster_sectors, sector_bytes,
                               free_clusters, total_clusters );
    HeapFree( GetProcessHeap(), 0, xroot );
    return ret;
}


/***********************************************************************
 *           GetDiskFreeSpaceExA   (KERNEL32.@)
 *
 *  This function is used to acquire the size of the available and
 *  total space on a logical volume.
 *
 * RETURNS
 *
 *  Zero on failure, nonzero upon success. Use GetLastError to obtain
 *  detailed error information.
 *
 */
BOOL WINAPI GetDiskFreeSpaceExA( LPCSTR root,
				     PULARGE_INTEGER avail,
				     PULARGE_INTEGER total,
				     PULARGE_INTEGER totalfree)
{
    int	drive;
    ULARGE_INTEGER size,available;

    if (!root) drive = DRIVE_GetCurrentDrive();
    else
    {
        if ((root[1]) && ((root[1] != ':') || (root[2] != '\\')))
        {
            FIXME("there are valid root names which are not supported yet\n");
	    /* ..like UNC names, for instance. */

            WARN("invalid root '%s'\n", root );
            return FALSE;
        }
        drive = toupper(root[0]) - 'A';
    }

    if (!DRIVE_GetFreeSpace(drive, &size, &available)) return FALSE;

    if (total)
    {
        total->s.HighPart = size.s.HighPart;
        total->s.LowPart = size.s.LowPart;
    }

    if (totalfree)
    {
        totalfree->s.HighPart = available.s.HighPart;
        totalfree->s.LowPart = available.s.LowPart;
    }

    if (avail)
    {
        if (FIXME_ON(dosfs))
	{
            /* On Windows2000, we need to check the disk quota
	       allocated for the user owning the calling process. We
	       don't want to be more obtrusive than necessary with the
	       FIXME messages, so don't print the FIXME unless Wine is
	       actually masquerading as Windows2000. */

            OSVERSIONINFOA ovi;
	    ovi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOA);
	    if (GetVersionExA(&ovi))
	    {
	      if (ovi.dwPlatformId == VER_PLATFORM_WIN32_NT && ovi.dwMajorVersion > 4)
                  FIXME("no per-user quota support yet\n");
	    }
	}

        /* Quick hack, should eventually be fixed to work 100% with
           Windows2000 (see comment above). */
        avail->s.HighPart = available.s.HighPart;
        avail->s.LowPart = available.s.LowPart;
    }

    return TRUE;
}

/***********************************************************************
 *           GetDiskFreeSpaceExW   (KERNEL32.@)
 */
BOOL WINAPI GetDiskFreeSpaceExW( LPCWSTR root, PULARGE_INTEGER avail,
				     PULARGE_INTEGER total,
				     PULARGE_INTEGER  totalfree)
{
    LPSTR xroot;
    BOOL ret;

    xroot = HEAP_strdupWtoA( GetProcessHeap(), 0, root);
    ret = GetDiskFreeSpaceExA( xroot, avail, total, totalfree);
    HeapFree( GetProcessHeap(), 0, xroot );
    return ret;
}

/***********************************************************************
 *           GetDriveType16   (KERNEL.136)
 * This function returns the type of a drive in Win16. 
 * Note that it returns DRIVE_REMOTE for CD-ROMs, since MSCDEX uses the
 * remote drive API. The return value DRIVE_REMOTE for CD-ROMs has been
 * verified on Win 3.11 and Windows 95. Some programs rely on it, so don't
 * do any pseudo-clever changes.
 *
 * RETURNS
 *	drivetype DRIVE_xxx
 */
UINT16 WINAPI GetDriveType16( UINT16 drive ) /* [in] number (NOT letter) of drive */
{
    UINT type = DRIVE_GetType(drive);
    TRACE("(%c:)\n", 'A' + drive );
    if (type == DRIVE_CDROM) type = DRIVE_REMOTE;
    return type;
}


/***********************************************************************
 *           GetDriveTypeA   (KERNEL32.@)
 *
 * Returns the type of the disk drive specified. If root is NULL the
 * root of the current directory is used.
 *
 * RETURNS
 *
 *  Type of drive (from Win32 SDK):
 *
 *   DRIVE_UNKNOWN     unable to find out anything about the drive
 *   DRIVE_NO_ROOT_DIR nonexistent root dir
 *   DRIVE_REMOVABLE   the disk can be removed from the machine
 *   DRIVE_FIXED       the disk can not be removed from the machine
 *   DRIVE_REMOTE      network disk
 *   DRIVE_CDROM       CDROM drive
 *   DRIVE_RAMDISK     virtual disk in RAM
 */
UINT WINAPI GetDriveTypeA(LPCSTR root) /* [in] String describing drive */
{
    int drive;
    TRACE("(%s)\n", debugstr_a(root));

    if (NULL == root) drive = DRIVE_GetCurrentDrive();
    else
    {
        if ((root[1]) && (root[1] != ':'))
	{
	    WARN("invalid root %s\n", debugstr_a(root));
	    return DRIVE_NO_ROOT_DIR;
	}
	drive = toupper(root[0]) - 'A';
    }
    return DRIVE_GetType(drive);
}


/***********************************************************************
 *           GetDriveTypeW   (KERNEL32.@)
 */
UINT WINAPI GetDriveTypeW( LPCWSTR root )
{
    LPSTR xpath = HEAP_strdupWtoA( GetProcessHeap(), 0, root );
    UINT ret = GetDriveTypeA( xpath );
    HeapFree( GetProcessHeap(), 0, xpath );
    return ret;
}


/***********************************************************************
 *           GetCurrentDirectory16   (KERNEL.411)
 */
UINT16 WINAPI GetCurrentDirectory16( UINT16 buflen, LPSTR buf )
{
    return (UINT16)DRIVE_GetCurrentDirectory(buflen, buf);
}


/***********************************************************************
 *           GetCurrentDirectoryA   (KERNEL32.@)
 */
UINT WINAPI GetCurrentDirectoryA( UINT buflen, LPSTR buf )
{
    UINT ret;
    char longname[MAX_PATHNAME_LEN];
    char shortname[MAX_PATHNAME_LEN];
    ret = DRIVE_GetCurrentDirectory(MAX_PATHNAME_LEN, shortname);
    if ( ret > MAX_PATHNAME_LEN ) {
      ERR_(file)("pathnamelength (%d) > MAX_PATHNAME_LEN!\n", ret );
      return ret;
    }
    GetLongPathNameA(shortname, longname, MAX_PATHNAME_LEN);
    ret = strlen( longname ) + 1;
    if (ret > buflen) return ret;
    strcpy(buf, longname);
    return ret - 1;
}

/***********************************************************************
 *           GetCurrentDirectoryW   (KERNEL32.@)
 */
UINT WINAPI GetCurrentDirectoryW( UINT buflen, LPWSTR buf )
{
    LPSTR xpath = HeapAlloc( GetProcessHeap(), 0, buflen+1 );
    UINT ret = GetCurrentDirectoryA( buflen, xpath );
    if (ret < buflen) ret = MultiByteToWideChar( CP_ACP, 0, xpath, -1, buf, buflen ) - 1;
    HeapFree( GetProcessHeap(), 0, xpath );
    return ret;
}


/***********************************************************************
 *           SetCurrentDirectory   (KERNEL.412)
 */
BOOL16 WINAPI SetCurrentDirectory16( LPCSTR dir )
{
    return SetCurrentDirectoryA( dir );
}


/***********************************************************************
 *           SetCurrentDirectoryA   (KERNEL32.@)
 */
BOOL WINAPI SetCurrentDirectoryA( LPCSTR dir )
{
    int drive, olddrive = DRIVE_GetCurrentDrive();

    if (!dir) {
    	ERR_(file)("(NULL)!\n");
	return FALSE;
    }
    if (dir[0] && (dir[1]==':'))
    {
        drive = toupper( *dir ) - 'A';
        dir += 2;
    }
    else
	drive = olddrive;

    /* WARNING: we need to set the drive before the dir, as DRIVE_Chdir
       sets pTask->curdir only if pTask->curdrive is drive */
    if (!(DRIVE_SetCurrentDrive( drive )))
	return FALSE;
    /* FIXME: what about empty strings? Add a \\ ? */
    if (!DRIVE_Chdir( drive, dir )) {
	DRIVE_SetCurrentDrive(olddrive);
	return FALSE;
    }
    return TRUE;
}


/***********************************************************************
 *           SetCurrentDirectoryW   (KERNEL32.@)
 */
BOOL WINAPI SetCurrentDirectoryW( LPCWSTR dirW )
{
    LPSTR dir = HEAP_strdupWtoA( GetProcessHeap(), 0, dirW );
    BOOL res = SetCurrentDirectoryA( dir );
    HeapFree( GetProcessHeap(), 0, dir );
    return res;
}


/***********************************************************************
 *           GetLogicalDriveStringsA   (KERNEL32.@)
 */
UINT WINAPI GetLogicalDriveStringsA( UINT len, LPSTR buffer )
{
    int drive, count;

    for (drive = count = 0; drive < MAX_DOS_DRIVES; drive++)
        if (DRIVE_IsValid(drive)) count++;
    if ((count * 4) + 1 <= len)
    {
        LPSTR p = buffer;
        for (drive = 0; drive < MAX_DOS_DRIVES; drive++)
            if (DRIVE_IsValid(drive))
            {
                *p++ = 'a' + drive;
                *p++ = ':';
                *p++ = '\\';
                *p++ = '\0';
            }
        *p = '\0';
        return count * 4;
    }
    else
        return (count * 4) + 1; /* account for terminating null */
    /* The API tells about these different return values */
}


/***********************************************************************
 *           GetLogicalDriveStringsW   (KERNEL32.@)
 */
UINT WINAPI GetLogicalDriveStringsW( UINT len, LPWSTR buffer )
{
    int drive, count;

    for (drive = count = 0; drive < MAX_DOS_DRIVES; drive++)
        if (DRIVE_IsValid(drive)) count++;
    if (count * 4 * sizeof(WCHAR) <= len)
    {
        LPWSTR p = buffer;
        for (drive = 0; drive < MAX_DOS_DRIVES; drive++)
            if (DRIVE_IsValid(drive))
            {
                *p++ = (WCHAR)('a' + drive);
                *p++ = (WCHAR)':';
                *p++ = (WCHAR)'\\';
                *p++ = (WCHAR)'\0';
            }
        *p = (WCHAR)'\0';
    }
    return count * 4 * sizeof(WCHAR);
}


/***********************************************************************
 *           GetLogicalDrives   (KERNEL32.@)
 */
DWORD WINAPI GetLogicalDrives(void)
{
    DWORD ret = 0;
    int drive;

    for (drive = 0; drive < MAX_DOS_DRIVES; drive++)
    {
        if ( (DRIVE_IsValid(drive)) ||
            (DOSDrives[drive].type == DRIVE_CDROM)) /* audio CD is also valid */
            ret |= (1 << drive);
    }
    return ret;
}


/***********************************************************************
 *           GetVolumeInformationA   (KERNEL32.@)
 */
BOOL WINAPI GetVolumeInformationA( LPCSTR root, LPSTR label,
                                       DWORD label_len, DWORD *serial,
                                       DWORD *filename_len, DWORD *flags,
                                       LPSTR fsname, DWORD fsname_len )
{
    int drive;
    char *cp;

    /* FIXME, SetLastError()s missing */

    if (!root) drive = DRIVE_GetCurrentDrive();
    else
    {
        if ((root[1]) && (root[1] != ':'))
        {
            WARN("invalid root '%s'\n",root);
            return FALSE;
        }
        drive = toupper(root[0]) - 'A';
    }
    if (!DRIVE_IsValid( drive )) return FALSE;
    if (label)
    {
       lstrcpynA( label, DRIVE_GetLabel(drive), label_len );
       cp = label + strlen(label);
       while (cp != label && *(cp-1) == ' ') cp--;
       *cp = '\0';
    }
    if (serial) *serial = DRIVE_GetSerialNumber(drive);

    /* Set the filesystem information */
    /* Note: we only emulate a FAT fs at present */

    if (filename_len) {
    	if (DOSDrives[drive].flags & DRIVE_SHORT_NAMES)
	    *filename_len = 12;
	else
	    *filename_len = 255;
    }
    if (flags)
      {
       *flags=0;
       if (DOSDrives[drive].flags & DRIVE_CASE_SENSITIVE)
         *flags|=FS_CASE_SENSITIVE;
       if (DOSDrives[drive].flags & DRIVE_CASE_PRESERVING)
         *flags|=FS_CASE_IS_PRESERVED;
      }
    if (fsname) {
    	/* Diablo checks that return code ... */
        if (DOSDrives[drive].type == DRIVE_CDROM)
	    lstrcpynA( fsname, "CDFS", fsname_len );
	else
	    lstrcpynA( fsname, "FAT", fsname_len );
    }
    return TRUE;
}


/***********************************************************************
 *           GetVolumeInformationW   (KERNEL32.@)
 */
BOOL WINAPI GetVolumeInformationW( LPCWSTR root, LPWSTR label,
                                       DWORD label_len, DWORD *serial,
                                       DWORD *filename_len, DWORD *flags,
                                       LPWSTR fsname, DWORD fsname_len )
{
    LPSTR xroot    = HEAP_strdupWtoA( GetProcessHeap(), 0, root );
    LPSTR xvolname = label ? HeapAlloc(GetProcessHeap(),0,label_len) : NULL;
    LPSTR xfsname  = fsname ? HeapAlloc(GetProcessHeap(),0,fsname_len) : NULL;
    BOOL ret = GetVolumeInformationA( xroot, xvolname, label_len, serial,
                                          filename_len, flags, xfsname,
                                          fsname_len );
    if (ret)
    {
        if (label) MultiByteToWideChar( CP_ACP, 0, xvolname, -1, label, label_len );
        if (fsname) MultiByteToWideChar( CP_ACP, 0, xfsname, -1, fsname, fsname_len );
    }
    HeapFree( GetProcessHeap(), 0, xroot );
    HeapFree( GetProcessHeap(), 0, xvolname );
    HeapFree( GetProcessHeap(), 0, xfsname );
    return ret;
}

/***********************************************************************
 *           SetVolumeLabelA   (KERNEL32.@)
 */
BOOL WINAPI SetVolumeLabelA( LPCSTR root, LPCSTR volname )
{
    int drive;
    
    /* FIXME, SetLastErrors missing */

    if (!root) drive = DRIVE_GetCurrentDrive();
    else
    {
        if ((root[1]) && (root[1] != ':'))
        {
            WARN("invalid root '%s'\n",root);
            return FALSE;
        }
        drive = toupper(root[0]) - 'A';
    }
    if (!DRIVE_IsValid( drive )) return FALSE;

    /* some copy protection stuff check this */
    if (DOSDrives[drive].type == DRIVE_CDROM) return FALSE;

    FIXME("(%s,%s),stub!\n", root, volname);
    return TRUE;
}

/***********************************************************************
 *           SetVolumeLabelW   (KERNEL32.@)
 */
BOOL WINAPI SetVolumeLabelW(LPCWSTR rootpath,LPCWSTR volname)
{
    LPSTR xroot, xvol;
    BOOL ret;

    xroot = HEAP_strdupWtoA( GetProcessHeap(), 0, rootpath);
    xvol = HEAP_strdupWtoA( GetProcessHeap(), 0, volname);
    ret = SetVolumeLabelA( xroot, xvol );
    HeapFree( GetProcessHeap(), 0, xroot );
    HeapFree( GetProcessHeap(), 0, xvol );
    return ret;
}
