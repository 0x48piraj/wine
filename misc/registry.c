/*
 * 	Registry Functions
 *
 * Copyright 1996 Marcus Meissner
 * Copyright 1998 Matthew Becker
 * Copyright 1999 Sylvain St-Germain
 *
 * December 21, 1997 - Kevin Cozens
 * Fixed bugs in the _w95_loadreg() function. Added extra information
 * regarding the format of the Windows '95 registry files.
 *
 * NOTES
 *    When changing this file, please re-run the regtest program to ensure
 *    the conditions are handled properly.
 *
 * TODO
 *    Security access
 *    Option handling
 *    Time for RegEnumKey*, RegQueryInfoKey*
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <ctype.h>
#include <errno.h>
#ifdef HAVE_SYS_ERRNO_H
#include <sys/errno.h>
#endif
#include <sys/types.h>
#include <fcntl.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include "windef.h"
#include "winbase.h"
#include "winnls.h"
#include "wine/winbase16.h"
#include "winerror.h"
#include "file.h"
#include "heap.h"
#include "debugtools.h"
#include "options.h"
#include "winreg.h"
#include "server.h"
#include "services.h"

DEFAULT_DEBUG_CHANNEL(reg);

static void REGISTRY_Init(void);
/* FIXME: following defines should be configured global ... */

#define SAVE_USERS_DEFAULT          ETCDIR"/wine.userreg"
#define SAVE_LOCAL_MACHINE_DEFAULT  ETCDIR"/wine.systemreg"

/* relative in ~user/.wine/ : */
#define SAVE_CURRENT_USER           "user.reg"
#define SAVE_DEFAULT_USER           "userdef.reg"
#define SAVE_LOCAL_USERS_DEFAULT    "wine.userreg"
#define SAVE_LOCAL_MACHINE          "system.reg"


static void *xmalloc( size_t size )
{
    void *res;
 
    res = malloc (size ? size : 1);
    if (res == NULL) {
        WARN("Virtual memory exhausted.\n");
        exit (1);
    }
    return res;
}                                                                              


/******************************************************************************
 * REGISTRY_Init [Internal]
 * Registry initialisation, allocates some default keys. 
 */
static void REGISTRY_Init(void) {
	HKEY	hkey;
	char	buf[200];

	TRACE("(void)\n");

	RegCreateKeyA(HKEY_DYN_DATA,"PerfStats\\StatData",&hkey);
	RegCloseKey(hkey);

        /* This was an Open, but since it is called before the real registries
           are loaded, it was changed to a Create - MTB 980507*/
	RegCreateKeyA(HKEY_LOCAL_MACHINE,"HARDWARE\\DESCRIPTION\\System",&hkey);
	RegSetValueExA(hkey,"Identifier",0,REG_SZ,"SystemType WINE",strlen("SystemType WINE"));
	RegCloseKey(hkey);

	/* \\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion
	 *						CurrentVersion
	 *						CurrentBuildNumber
	 *						CurrentType
	 *					string	RegisteredOwner
	 *					string	RegisteredOrganization
	 *
	 */
	/* System\\CurrentControlSet\\Services\\SNMP\\Parameters\\RFC1156Agent
	 * 					string	SysContact
	 * 					string	SysLocation
	 * 						SysServices
	 */
	if (-1!=gethostname(buf,200)) {
		RegCreateKeyA(HKEY_LOCAL_MACHINE,"System\\CurrentControlSet\\Control\\ComputerName\\ComputerName",&hkey);
		RegSetValueExA(hkey,"ComputerName",0,REG_SZ,buf,strlen(buf)+1);
		RegCloseKey(hkey);
	}
}


/************************ LOAD Registry Function ****************************/



/******************************************************************************
 * _find_or_add_key [Internal]
 */
static inline HKEY _find_or_add_key( HKEY hkey, LPWSTR keyname )
{
    HKEY subkey;
    if (RegCreateKeyW( hkey, keyname, &subkey ) != ERROR_SUCCESS) subkey = 0;
    if (keyname) free( keyname );
    return subkey;
}

/******************************************************************************
 * _find_or_add_value [Internal]
 */
static void _find_or_add_value( HKEY hkey, LPWSTR name, DWORD type, LPBYTE data, DWORD len )
{
    RegSetValueExW( hkey, name, 0, type, data, len );
    if (name) free( name );
    if (data) free( data );
}


/******************************************************************************
 * _wine_read_line [Internal]
 *
 * reads a line including dynamically enlarging the readbuffer and throwing
 * away comments
 */
static int _wine_read_line( FILE *F, char **buf, int *len )
{
	char	*s,*curread;
	int	mylen,curoff;

	curread	= *buf;
	mylen	= *len;
	**buf	= '\0';
	while (1) {
		while (1) {
			s=fgets(curread,mylen,F);
			if (s==NULL)
				return 0; /* EOF */
			if (NULL==(s=strchr(curread,'\n'))) {
				/* buffer wasn't large enough */
				curoff	= strlen(*buf);
				curread	= realloc(*buf,*len*2);
                                if(curread == NULL) {
                                    WARN("Out of memory");
                                    return 0;
                                }
                                *buf	= curread;
				curread+= curoff;
				mylen	= *len;	/* we filled up the buffer and 
						 * got new '*len' bytes to fill
						 */
				*len	= *len * 2;
			} else {
				*s='\0';
				break;
			}
		}
		/* throw away comments */
		if (**buf=='#' || **buf==';') {
			curread	= *buf;
			mylen	= *len;
			continue;
		}
		if (s) 	/* got end of line */
			break;
	}
	return 1;
}


/******************************************************************************
 * _wine_read_USTRING [Internal]
 *
 * converts a char* into a UNICODE string (up to a special char)
 * and returns the position exactly after that string
 */
static char* _wine_read_USTRING( char *buf, LPWSTR *str )
{
	char	*s;
	LPWSTR	ws;

	/* read up to "=" or "\0" or "\n" */
	s	= buf;
	*str	= (LPWSTR)xmalloc(2*strlen(buf)+2);
	ws	= *str;
	while (*s && (*s!='\n') && (*s!='=')) {
		if (*s!='\\')
			*ws++=*((unsigned char*)s++);
		else {
			s++;
			if (!*s) {
				/* Dangling \ ... may only happen if a registry
				 * write was short. FIXME: What to do?
				 */
				 break;
			}
			if (*s=='\\') {
				*ws++='\\';
				s++;
				continue;
			}
			if (*s!='u') {
				WARN("Non unicode escape sequence \\%c found in |%s|\n",*s,buf);
				*ws++='\\';
				*ws++=*s++;
			} else {
				char	xbuf[5];
				int	wc;

				s++;
				memcpy(xbuf,s,4);xbuf[4]='\0';
				if (!sscanf(xbuf,"%x",&wc))
					WARN("Strange escape sequence %s found in |%s|\n",xbuf,buf);
				s+=4;
				*ws++	=(unsigned short)wc;
			}
		}
	}
	*ws	= 0;
	return s;
}


/******************************************************************************
 * _wine_loadsubkey [Internal]
 *
 * NOTES
 *    It seems like this is returning a boolean.  Should it?
 *
 * RETURNS
 *    Success: 1
 *    Failure: 0
 */
static int _wine_loadsubkey( FILE *F, HKEY hkey, int level, char **buf, int *buflen )
{
    	HKEY subkey;
	int		i;
	char		*s;
	LPWSTR		name;

    TRACE("(%p,%x,%d,%s,%d)\n", F, hkey, level, debugstr_a(*buf), *buflen);

    /* Good.  We already got a line here ... so parse it */
    subkey = 0;
    while (1) {
        i=0;s=*buf;
        while (*s=='\t') {
            s++;
            i++;
        }
        if (i>level) {
            if (!subkey) {
                WARN("Got a subhierarchy without resp. key?\n");
                return 0;
            }
	    if (!_wine_loadsubkey(F,subkey,level+1,buf,buflen))
	       if (!_wine_read_line(F,buf,buflen))
		  goto done;
            continue;
        }

		/* let the caller handle this line */
		if (i<level || **buf=='\0')
			goto done;

		/* it can be: a value or a keyname. Parse the name first */
		s=_wine_read_USTRING(s,&name);

		/* switch() default: hack to avoid gotos */
		switch (0) {
		default:
			if (*s=='\0') {
                                if (subkey) RegCloseKey( subkey );
				subkey=_find_or_add_key(hkey,name);
			} else {
				LPBYTE		data;
				int		len,lastmodified,type;

				if (*s!='=') {
					WARN("Unexpected character: %c\n",*s);
					break;
				}
				s++;
				if (2!=sscanf(s,"%d,%d,",&type,&lastmodified)) {
					WARN("Haven't understood possible value in |%s|, skipping.\n",*buf);
					break;
				}
				/* skip the 2 , */
				s=strchr(s,',');s++;
				s=strchr(s,',');
				if (!s++) {
					WARN("Haven't understood possible value in |%s|, skipping.\n",*buf);
					break;
				}
				if (type == REG_SZ || type == REG_EXPAND_SZ) {
					s=_wine_read_USTRING(s,(LPWSTR*)&data);
                                        len = lstrlenW((LPWSTR)data)*2+2;
				} else {
					len=strlen(s)/2;
					data = (LPBYTE)xmalloc(len+1);
					for (i=0;i<len;i++) {
						data[i]=0;
						if (*s>='0' && *s<='9')
							data[i]=(*s-'0')<<4;
						if (*s>='a' && *s<='f')
							data[i]=(*s-'a'+'\xa')<<4;
						if (*s>='A' && *s<='F')
							data[i]=(*s-'A'+'\xa')<<4;
						s++;
						if (*s>='0' && *s<='9')
							data[i]|=*s-'0';
						if (*s>='a' && *s<='f')
							data[i]|=*s-'a'+'\xa';
						if (*s>='A' && *s<='F')
							data[i]|=*s-'A'+'\xa';
						s++;
					}
				}
				_find_or_add_value(hkey,name,type,data,len);
			}
		}
		/* read the next line */
		if (!_wine_read_line(F,buf,buflen))
			goto done;
    }
 done:
    if (subkey) RegCloseKey( subkey );
    return 1;
}


/******************************************************************************
 * _wine_loadsubreg [Internal]
 */
static int _wine_loadsubreg( FILE *F, HKEY hkey, const char *fn )
{
	int	ver;
	char	*buf;
	int	buflen;

	buf=xmalloc(10);buflen=10;
	if (!_wine_read_line(F,&buf,&buflen)) {
		free(buf);
		return 0;
	}
	if (!sscanf(buf,"WINE REGISTRY Version %d",&ver)) {
		free(buf);
		return 0;
	}
	if (ver!=1) {
            if (ver == 2)  /* new version */
            {
                HANDLE file;
                if ((file = FILE_CreateFile( fn, GENERIC_READ, 0, NULL, OPEN_EXISTING,
                                      FILE_ATTRIBUTE_NORMAL, -1, TRUE )) != INVALID_HANDLE_VALUE)
                {
                    SERVER_START_REQ
                    {
                        struct load_registry_request *req = server_alloc_req( sizeof(*req), 0 );
                        req->hkey    = hkey;
                        req->file    = file;
                        server_call( REQ_LOAD_REGISTRY );
                    }
                    SERVER_END_REQ;
                    CloseHandle( file );
                }
                free( buf );
                return 1;
            }
            else
            {
		TRACE("Old format (%d) registry found, ignoring it. (buf was %s).\n",ver,buf);
		free(buf);
		return 0;
            }
	}
	if (!_wine_read_line(F,&buf,&buflen)) {
		free(buf);
		return 0;
	}
	if (!_wine_loadsubkey(F,hkey,0,&buf,&buflen)) {
		free(buf);
		return 0;
	}
	free(buf);
	return 1;
}


/******************************************************************************
 * _wine_loadreg [Internal]
 */
static int _wine_loadreg( HKEY hkey, char *fn )
{
    FILE *F;

    TRACE("(%x,%s)\n",hkey,debugstr_a(fn));

    F = fopen(fn,"rb");
    if (F==NULL) {
        WARN("Couldn't open %s for reading: %s\n",fn,strerror(errno) );
        return -1;
    }
    _wine_loadsubreg(F,hkey,fn);
    fclose(F);
    return 0;
}

/* NT REGISTRY LOADER */

#ifdef HAVE_SYS_MMAN_H
# include <sys/mman.h>
#endif

#ifndef MAP_FAILED
#define MAP_FAILED ((LPVOID)-1)
#endif

#define  NT_REG_BLOCK_SIZE		0x1000

#define NT_REG_HEADER_BLOCK_ID       0x66676572	/* regf */
#define NT_REG_POOL_BLOCK_ID         0x6E696268	/* hbin */
#define NT_REG_KEY_BLOCK_ID          0x6b6e /* nk */
#define NT_REG_VALUE_BLOCK_ID        0x6b76 /* vk */

/* subblocks of nk */
#define NT_REG_HASH_BLOCK_ID         0x666c /* lf */
#define NT_REG_NOHASH_BLOCK_ID       0x696c /* li */
#define NT_REG_RI_BLOCK_ID	     0x6972 /* ri */

#define NT_REG_KEY_BLOCK_TYPE        0x20
#define NT_REG_ROOT_KEY_BLOCK_TYPE   0x2c

typedef struct 
{
	DWORD	id;		/* 0x66676572 'regf'*/
	DWORD	uk1;		/* 0x04 */
	DWORD	uk2;		/* 0x08 */
	FILETIME	DateModified;	/* 0x0c */
	DWORD	uk3;		/* 0x14 */
	DWORD	uk4;		/* 0x18 */
	DWORD	uk5;		/* 0x1c */
	DWORD	uk6;		/* 0x20 */
	DWORD	RootKeyBlock;	/* 0x24 */
	DWORD	BlockSize;	/* 0x28 */
	DWORD   uk7[116];	
	DWORD	Checksum; /* at offset 0x1FC */
} nt_regf;

typedef struct
{
	DWORD	blocksize;
	BYTE	data[1];
} nt_hbin_sub;

typedef struct
{
	DWORD	id;		/* 0x6E696268 'hbin' */
	DWORD	off_prev;
	DWORD	off_next;
	DWORD	uk1;
	DWORD	uk2;		/* 0x10 */
	DWORD	uk3;		/* 0x14 */
	DWORD	uk4;		/* 0x18 */
	DWORD	size;		/* 0x1C */
	nt_hbin_sub	hbin_sub;	/* 0x20 */
} nt_hbin;

/*
 * the value_list consists of offsets to the values (vk)
 */
typedef struct
{
	WORD	SubBlockId;		/* 0x00 0x6B6E */
	WORD	Type;			/* 0x02 for the root-key: 0x2C, otherwise 0x20*/
	FILETIME	writetime;	/* 0x04 */
	DWORD	uk1;			/* 0x0C */
	DWORD	parent_off;		/* 0x10 Offset of Owner/Parent key */
	DWORD	nr_subkeys;		/* 0x14 number of sub-Keys */
	DWORD	uk8;			/* 0x18 */
	DWORD	lf_off;			/* 0x1C Offset of the sub-key lf-Records */
	DWORD	uk2;			/* 0x20 */
	DWORD	nr_values;		/* 0x24 number of values */
	DWORD	valuelist_off;		/* 0x28 Offset of the Value-List */
	DWORD	off_sk;			/* 0x2c Offset of the sk-Record */
	DWORD	off_class;		/* 0x30 Offset of the Class-Name */
	DWORD	uk3;			/* 0x34 */
	DWORD	uk4;			/* 0x38 */
	DWORD	uk5;			/* 0x3c */
	DWORD	uk6;			/* 0x40 */
	DWORD	uk7;			/* 0x44 */
	WORD	name_len;		/* 0x48 name-length */
	WORD	class_len;		/* 0x4a class-name length */
	char	name[1];		/* 0x4c key-name */
} nt_nk;

typedef struct
{
	DWORD	off_nk;	/* 0x00 */
	DWORD	name;	/* 0x04 */
} hash_rec;

typedef struct
{
	WORD	id;		/* 0x00 0x666c */
	WORD	nr_keys;	/* 0x06 */
	hash_rec	hash_rec[1];
} nt_lf;

/*
 list of subkeys without hash

 li --+-->nk
      |
      +-->nk
 */
typedef struct
{
	WORD	id;		/* 0x00 0x696c */
	WORD	nr_keys;
	DWORD	off_nk[1];
} nt_li;

/*
 this is a intermediate node

 ri --+-->li--+-->nk
      |       +
      |       +-->nk
      |
      +-->li--+-->nk
              +
	      +-->nk
 */
typedef struct
{
	WORD	id;		/* 0x00 0x6972 */
	WORD	nr_li;		/* 0x02 number off offsets */
	DWORD	off_li[1];	/* 0x04 points to li */
} nt_ri;

typedef struct
{
	WORD	id;		/* 0x00 'vk' */
	WORD	nam_len;
	DWORD	data_len;
	DWORD	data_off;
	DWORD	type;
	WORD	flag;
	WORD	uk1;
	char	name[1];
} nt_vk;

LPSTR _strdupnA( LPCSTR str, int len )
{
    LPSTR ret;

    if (!str) return NULL;
    ret = xmalloc( len + 1 );
    memcpy( ret, str, len );
    ret[len] = 0x00;
    return ret;
}

static int _nt_parse_nk(HKEY hkey, char * base, nt_nk * nk, int level);
static int _nt_parse_vk(HKEY hkey, char * base, nt_vk * vk);
static int _nt_parse_lf(HKEY hkey, char * base, int subkeys, nt_lf * lf, int level);


/*
 * gets a value
 *
 * vk->flag:
 *  0 value is a default value
 *  1 the value has a name
 *
 * vk->data_len
 *  len of the whole data block
 *  - reg_sz (unicode)
 *    bytes including the terminating \0 = 2*(number_of_chars+1)
 *  - reg_dword, reg_binary:
 *    if highest bit of data_len is set data_off contains the value
 */
static int _nt_parse_vk(HKEY hkey, char * base, nt_vk * vk)
{
	WCHAR name [256];
	DWORD len, ret;
	BYTE * pdata = (BYTE *)(base+vk->data_off+4); /* start of data */

	if(vk->id != NT_REG_VALUE_BLOCK_ID) goto error;

        if (!(len = MultiByteToWideChar( CP_ACP, 0, vk->name, vk->nam_len, name, 256 )) && vk->nam_len)
        {
            ERR("name too large '%.*s' (%d)\n", vk->nam_len, vk->name, vk->nam_len );
            return FALSE;
        }
        name[len] = 0;

	ret = RegSetValueExW( hkey, (vk->flag & 0x00000001) ? name : NULL, 0, vk->type,
			(vk->data_len & 0x80000000) ? (LPBYTE)&(vk->data_off): pdata,
			(vk->data_len & 0x7fffffff) );
	if (ret) ERR("RegSetValueEx failed (0x%08lx)\n", ret);
	return TRUE;
error:
	ERR("unknown block found (0x%04x), please report!\n", vk->id);
	return FALSE;
}

/*
 * get the subkeys
 *
 * this structure contains the hash of a keyname and points to all
 * subkeys
 *
 * exception: if the id is 'il' there are no hash values and every 
 * dword is a offset
 */
static int _nt_parse_lf(HKEY hkey, char * base, int subkeys, nt_lf * lf, int level)
{
	int i;

	if (lf->id == NT_REG_HASH_BLOCK_ID)
	{
	  if (subkeys != lf->nr_keys) goto error1;

	  for (i=0; i<lf->nr_keys; i++)
	  {
	    if (!_nt_parse_nk(hkey, base, (nt_nk*)(base+lf->hash_rec[i].off_nk+4), level)) goto error;
	  }
	}
	else if (lf->id == NT_REG_NOHASH_BLOCK_ID)
	{
	  nt_li * li = (nt_li*)lf;
	  if (subkeys != li->nr_keys) goto error1;

	  for (i=0; i<li->nr_keys; i++)
	  {
	    if (!_nt_parse_nk(hkey, base, (nt_nk*)(base+li->off_nk[i]+4), level)) goto error;
	  }
	}
	else if (lf->id == NT_REG_RI_BLOCK_ID) /* ri */
	{
	  nt_ri * ri = (nt_ri*)lf;
	  int li_subkeys = 0;

	  /* count all subkeys */
	  for (i=0; i<ri->nr_li; i++)
	  {
	    nt_li * li = (nt_li*)(base+ri->off_li[i]+4);
	    if(li->id != NT_REG_NOHASH_BLOCK_ID) goto error2;
	    li_subkeys += li->nr_keys;
	  }

	  /* check number */
	  if (subkeys != li_subkeys) goto error1;

	  /* loop through the keys */
	  for (i=0; i<ri->nr_li; i++)
	  {
	    nt_li * li = (nt_li*)(base+ri->off_li[i]+4);
	    if (!_nt_parse_lf(hkey, base, li->nr_keys, (nt_lf*)li, level)) goto error;
	  }
	}
	else 
	{
	  goto error2;
	}
	return TRUE;

error2: ERR("unknown node id 0x%04x, please report!\n", lf->id);
	return TRUE;
	
error1:	ERR("registry file corrupt! (inconsistent number of subkeys)\n");
	return FALSE;

error:	ERR("error reading lf block\n");
	return FALSE;
}

static int _nt_parse_nk(HKEY hkey, char * base, nt_nk * nk, int level)
{
	char * name;
	unsigned int n;
	DWORD * vl;
	HKEY subkey = hkey;

	if(nk->SubBlockId != NT_REG_KEY_BLOCK_ID)
	{
	  ERR("unknown node id 0x%04x, please report!\n", nk->SubBlockId);
	  goto error;
	}

	if((nk->Type!=NT_REG_ROOT_KEY_BLOCK_TYPE) &&
	   (((nt_nk*)(base+nk->parent_off+4))->SubBlockId != NT_REG_KEY_BLOCK_ID))
	{
	  ERR("registry file corrupt!\n");
	  goto error;
	}

	/* create the new key */
	if(level <= 0)
	{
	  name = _strdupnA( nk->name, nk->name_len);
	  if(RegCreateKeyA( hkey, name, &subkey )) { free(name); goto error; }
	  free(name);
	}

	/* loop through the subkeys */
	if (nk->nr_subkeys)
	{
	  nt_lf * lf = (nt_lf*)(base+nk->lf_off+4);
	  if (!_nt_parse_lf(subkey, base, nk->nr_subkeys, lf, level-1)) goto error1;
	}

	/* loop trough the value list */
	vl = (DWORD *)(base+nk->valuelist_off+4);
	for (n=0; n<nk->nr_values; n++)
	{
	  nt_vk * vk = (nt_vk*)(base+vl[n]+4);
	  if (!_nt_parse_vk(subkey, base, vk)) goto error1;
	}

	/* Don't close the subkey if it is the hkey that was passed
	 * (i.e. Level was <= 0)
	 */
	if( subkey!=hkey ) RegCloseKey(subkey);
	return TRUE;
	
error1:	RegCloseKey(subkey);
error:	return FALSE;
}

/* end nt loader */

/* windows 95 registry loader */

/* SECTION 1: main header
 *
 * once at offset 0
 */
#define	W95_REG_CREG_ID	0x47455243

typedef struct 
{
	DWORD	id;		/* "CREG" = W95_REG_CREG_ID */
	DWORD	version;	/* ???? 0x00010000 */
	DWORD	rgdb_off;	/* 0x08 Offset of 1st RGDB-block */
	DWORD	uk2;		/* 0x0c */
	WORD	rgdb_num;	/* 0x10 # of RGDB-blocks */
	WORD	uk3;
	DWORD	uk[3];
	/* rgkn */
} _w95creg;

/* SECTION 2: Directory information (tree structure)
 *
 * once on offset 0x20
 *
 * structure: [rgkn][dke]*	(repeat till last_dke is reached)
 */
#define	W95_REG_RGKN_ID	0x4e4b4752

typedef struct
{
	DWORD	id;		/*"RGKN" = W95_REG_RGKN_ID */
	DWORD	size;		/* Size of the RGKN-block */
	DWORD	root_off;	/* Rel. Offset of the root-record */
	DWORD   last_dke;       /* Offset to last DKE ? */
	DWORD	uk[4];
} _w95rgkn;

/* Disk Key Entry Structure
 *
 * the 1st entry in a "usual" registry file is a nul-entry with subkeys: the
 * hive itself. It looks the same like other keys. Even the ID-number can
 * be any value.
 *
 * The "hash"-value is a value representing the key's name. Windows will not
 * search for the name, but for a matching hash-value. if it finds one, it
 * will compare the actual string info, otherwise continue with the next key.
 * To calculate the hash initialize a D-Word with 0 and add all ASCII-values 
 * of the string which are smaller than 0x80 (128) to this D-Word.   
 *
 * If you want to modify key names, also modify the hash-values, since they
 * cannot be found again (although they would be displayed in REGEDIT)
 * End of list-pointers are filled with 0xFFFFFFFF
 *
 * Disk keys are layed out flat ... But, sometimes, nrLS and nrMS are both
 * 0xFFFF, which means skipping over nextkeyoffset bytes (including this
 * structure) and reading another RGDB_section.
 *
 * The last DKE (see field last_dke in _w95_rgkn) has only 3 DWORDs with
 * 0x80000000 (EOL indicator ?) as x1, the hash value and 0xFFFFFFFF as x3.
 * The remaining space between last_dke and the offset calculated from
 * rgkn->size seems to be free for use for more dke:s.
 * So it seems if more dke:s are added, they are added to that space and
 * last_dke is grown, and in case that "free" space is out, the space
 * gets grown and rgkn->size gets adjusted.
 *
 * there is a one to one relationship between dke and dkh
 */
 /* key struct, once per key */
typedef struct
{
	DWORD	x1;		/* Free entry indicator(?) */
	DWORD	hash;		/* sum of bytes of keyname */
	DWORD	x3;		/* Root key indicator? usually 0xFFFFFFFF */
	DWORD	prevlvl;	/* offset of previous key */
	DWORD	nextsub;	/* offset of child key */
	DWORD	next;		/* offset of sibling key */
	WORD	nrLS;		/* id inside the rgdb block */
	WORD	nrMS;		/* number of the rgdb block */
} _w95dke;

/* SECTION 3: key information, values and data
 *
 * structure:
 *  section:	[blocks]*		(repeat creg->rgdb_num times)
 *  blocks:	[rgdb] [subblocks]* 	(repeat till block size reached )
 *  subblocks:	[dkh] [dkv]*		(repeat dkh->values times )
 *
 * An interesting relationship exists in RGDB_section. The DWORD value
 * at offset 0x10 equals the one at offset 0x04 minus the one at offset 0x08.
 * I have no idea at the moment what this means.  (Kevin Cozens)
 */

/* block header, once per block */
#define W95_REG_RGDB_ID	0x42444752

typedef struct
{
	DWORD	id;	/* 0x00 'RGDB' = W95_REG_RGDB_ID */
	DWORD	size;	/* 0x04 */
	DWORD	uk1;	/* 0x08 */
	DWORD	uk2;	/* 0x0c */
	DWORD	uk3;	/* 0x10 */
	DWORD	uk4;	/* 0x14 */
	DWORD	uk5;	/* 0x18 */
	DWORD	uk6;	/* 0x1c */
	/* dkh */
} _w95rgdb;

/* Disk Key Header structure (RGDB part), once per key */
typedef	struct 
{
	DWORD	nextkeyoff; 	/* 0x00 offset to next dkh */
	WORD	nrLS;		/* 0x04 id inside the rgdb block */
	WORD	nrMS;		/* 0x06 number of the rgdb block */
	DWORD	bytesused;	/* 0x08 */
	WORD	keynamelen;	/* 0x0c len of name */
	WORD	values;		/* 0x0e number of values */
	DWORD	xx1;		/* 0x10 */
	char	name[1];	/* 0x14 */
	/* dkv */		/* 0x14 + keynamelen */
} _w95dkh;

/* Disk Key Value structure, once per value */
typedef	struct
{
	DWORD	type;		/* 0x00 */
	DWORD	x1;		/* 0x04 */
	WORD	valnamelen;	/* 0x08 length of name, 0 is default key */
	WORD	valdatalen;	/* 0x0A length of data */
	char	name[1];	/* 0x0c */
	/* raw data */		/* 0x0c + valnamelen */
} _w95dkv;

/******************************************************************************
 * _w95_lookup_dkh [Internal]
 *
 * seeks the dkh belonging to a dke
 */
static _w95dkh * _w95_lookup_dkh (_w95creg *creg, int nrLS, int nrMS)
{
	_w95rgdb * rgdb;
	_w95dkh * dkh;
	int i;
	
	/* get the beginning of the rgdb datastore */
	rgdb = (_w95rgdb*)((char*)creg+creg->rgdb_off);

	/* check: requested block < last_block) */
	if (creg->rgdb_num <= nrMS)				
	{
	  ERR("registry file corrupt! requested block no. beyond end.\n");
	  goto error;
	}
	
	/* find the right block */
	for(i=0; i<nrMS ;i++)
	{
	  if(rgdb->id != W95_REG_RGDB_ID)			/* check the magic */
	  {
	    ERR("registry file corrupt! bad magic 0x%08lx\n", rgdb->id);
	    goto error;
	  }
	  rgdb = (_w95rgdb*) ((char*)rgdb+rgdb->size);		/* find next block */
	}

	dkh = (_w95dkh*)(rgdb + 1);				/* first sub block within the rgdb */

	do
	{
	  if(nrLS==dkh->nrLS ) return dkh;
	  dkh = (_w95dkh*)((char*)dkh + dkh->nextkeyoff);	/* find next subblock */
	} while ((char *)dkh < ((char*)rgdb+rgdb->size));

error:	return NULL;
}	
 
/******************************************************************************
 * _w95_parse_dkv [Internal]
 */
static int _w95_parse_dkv (
	HKEY hkey,
	_w95dkh * dkh,
	int nrLS,
	int nrMS )
{
	_w95dkv * dkv;
	int i;
	DWORD ret;
	char * name;
			
	/* first value block */
	dkv = (_w95dkv*)((char*)dkh+dkh->keynamelen+0x14);

	/* loop trought the values */
	for (i=0; i< dkh->values; i++)
	{
	  name = _strdupnA(dkv->name, dkv->valnamelen);
	  ret = RegSetValueExA(hkey, name, 0, dkv->type, &(dkv->name[dkv->valnamelen]),dkv->valdatalen); 
	  if (ret) FIXME("RegSetValueEx returned: 0x%08lx\n", ret);
	  free (name);

	  /* next value */
	  dkv = (_w95dkv*)((char*)dkv+dkv->valnamelen+dkv->valdatalen+0x0c);
	}
	return TRUE;
}

/******************************************************************************
 * _w95_parse_dke [Internal]
 */
static int _w95_parse_dke( 
	HKEY hkey,
	_w95creg * creg,
	_w95rgkn *rgkn,
	_w95dke * dke,
	int level )
{
	_w95dkh * dkh;
	HKEY hsubkey = hkey;
	char * name;
	int ret = FALSE;

	/* special root key */
	if (dke->nrLS == 0xffff || dke->nrMS==0xffff)		/* eg. the root key has no name */
	{
	  /* parse the one subkey */
	  if (dke->nextsub != 0xffffffff) 
	  {
    	    return _w95_parse_dke(hsubkey, creg, rgkn, (_w95dke*)((char*)rgkn+dke->nextsub), level);
	  }
	  /* has no sibling keys */
	  goto error;
	}

	/* search subblock */
	if (!(dkh = _w95_lookup_dkh(creg, dke->nrLS, dke->nrMS)))
	{
	  fprintf(stderr, "dke pointing to missing dkh !\n");
	  goto error;
	}

	if ( level <= 0 )
	{
	  /* walk sibling keys */
	  if (dke->next != 0xffffffff )
	  {
    	    if (!_w95_parse_dke(hkey, creg, rgkn, (_w95dke*)((char*)rgkn+dke->next), level)) goto error;
	  }

	  /* create subkey and insert values */
	  name = _strdupnA( dkh->name, dkh->keynamelen);
	  if (RegCreateKeyA(hkey, name, &hsubkey)) { free(name); goto error; }
	  free(name);
	  if (!_w95_parse_dkv(hsubkey, dkh, dke->nrLS, dke->nrMS)) goto error1;
	}  
	
 	/* next sub key */
	if (dke->nextsub != 0xffffffff) 
	{
    	  if (!_w95_parse_dke(hsubkey, creg, rgkn, (_w95dke*)((char*)rgkn+dke->nextsub), level-1)) goto error1;
	}

	ret = TRUE;
error1:	if (hsubkey != hkey) RegCloseKey(hsubkey);
error:	return ret;
}
/* end windows 95 loader */

/******************************************************************************
 *	NativeRegLoadKey [Internal]
 *
 * Loads a native registry file (win95/nt)
 * 	hkey	root key
 *	fn	filename
 *	level	number of levels to cut away (eg. ".Default" in user.dat)
 *
 * this function intentionally uses unix file functions to make it possible
 * to move it to a seperate registry helper programm
 */
static int NativeRegLoadKey( HKEY hkey, char* fn, int level )
{
	int fd = 0;
	struct stat st;
        DOS_FULL_NAME full_name;
	int ret = FALSE;
	void * base;
	char *filetype = "unknown";
			
        if (!DOSFS_GetFullName( fn, 0, &full_name )) return FALSE;
	
	/* map the registry into the memory */
	if ((fd = open(full_name.long_name, O_RDONLY | O_NONBLOCK)) == -1) return FALSE;
	if ((fstat(fd, &st) == -1)) goto error;
	if (!st.st_size) goto error;
	if ((base = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0)) == MAP_FAILED) goto error;

	switch (*(LPDWORD)base)
	{
	  /* windows 95 'CREG' */
	  case W95_REG_CREG_ID:
	    {
	      _w95creg *creg;
	      _w95rgkn *rgkn;
	      _w95dke *dke, *root_dke;
	      creg = base;
	      filetype = "win95";
	      TRACE("Loading %s registry '%s' '%s'\n", filetype, fn, full_name.long_name);

	      /* load the header (rgkn) */
	      rgkn = (_w95rgkn*)(creg + 1);
	      if (rgkn->id != W95_REG_RGKN_ID) 
	      {
		ERR("second IFF header not RGKN, but %lx\n", rgkn->id);
		goto error1;
	      }
	      if (rgkn->root_off != 0x20)
	      {
		ERR("rgkn->root_off not 0x20, please report !\n");
		goto error1;
	      }
	      if (rgkn->last_dke > rgkn->size)
	      {
		ERR("registry file corrupt! last_dke > size!\n");
		goto error1;
	      }
	      /* verify last dke */
	      dke = (_w95dke*)((char*)rgkn + rgkn->last_dke);
	      if (dke->x1 != 0x80000000)
	      { /* wrong magic */
		ERR("last dke invalid !\n");
		goto error1;
	      }
	      if (rgkn->size > creg->rgdb_off)
	      {
		ERR("registry file corrupt! rgkn size > rgdb_off !\n");
		goto error1;
	      }
	      root_dke = (_w95dke*)((char*)rgkn + rgkn->root_off);
	      if ( (root_dke->prevlvl != 0xffffffff)
	        || (root_dke->next != 0xffffffff) )
	      {
		ERR("registry file corrupt! invalid root dke !\n");
		goto error1;
	      }

	      ret = _w95_parse_dke(hkey, creg, rgkn, root_dke, level);
	    }
	    break;
	  /* nt 'regf'*/
	  case NT_REG_HEADER_BLOCK_ID:
	    {
	      nt_regf * regf;
	      nt_hbin * hbin;
	      nt_hbin_sub * hbin_sub;
	      nt_nk* nk;

	      filetype = "NT";
	      TRACE("Loading %s registry '%s' '%s'\n", filetype, fn, full_name.long_name);

	      /* start block */
	      regf = base;

	      /* hbin block */
	      hbin = (nt_hbin*)((char*) base + 0x1000);
	      if (hbin->id != NT_REG_POOL_BLOCK_ID)
	      {
	        ERR( "hbin block invalid\n");
	        goto error1;
	      }

	      /* hbin_sub block */
	      hbin_sub = (nt_hbin_sub*)&(hbin->hbin_sub);
	      if ((hbin_sub->data[0] != 'n') || (hbin_sub->data[1] != 'k'))
	      {
	        ERR( "hbin_sub block invalid\n");
	        goto error1;
	      }

	      /* nk block */
	      nk = (nt_nk*)&(hbin_sub->data[0]);
	      if (nk->Type != NT_REG_ROOT_KEY_BLOCK_TYPE)
	      {
	        ERR( "special nk block not found\n");
	        goto error1;
	      }

	      ret = _nt_parse_nk (hkey, (char *) base + 0x1000, nk, level);
	    }
	    break;
	  default:
	    {
	      ERR("unknown registry signature !\n");
	      goto error1;
	    }
	}
error1:	if(!ret)
	{
	  ERR("error loading %s registry file %s\n",
						filetype, full_name.long_name);
	  if (!strcmp(filetype, "win95"))
	    ERR("Please report to a.mohr@mailto.de.\n");
	  ERR("Make a backup of the file, run a good reg cleaner program and try again !\n");
	}
	munmap(base, st.st_size);
error:	close(fd);
	return ret;	
}

/* WINDOWS 31 REGISTRY LOADER, supplied by Tor Sj�wall, tor@sn.no */
/*
    reghack - windows 3.11 registry data format demo program.

    The reg.dat file has 3 parts, a header, a table of 8-byte entries that is
    a combined hash table and tree description, and finally a text table.

    The header is obvious from the struct header. The taboff1 and taboff2
    fields are always 0x20, and their usage is unknown.

    The 8-byte entry table has various entry types.

    tabent[0] is a root index. The second word has the index of the root of
            the directory.
    tabent[1..hashsize] is a hash table. The first word in the hash entry is
            the index of the key/value that has that hash. Data with the same
            hash value are on a circular list. The other three words in the
            hash entry are always zero.
    tabent[hashsize..tabcnt] is the tree structure. There are two kinds of
            entry: dirent and keyent/valent. They are identified by context.
    tabent[freeidx] is the first free entry. The first word in a free entry
            is the index of the next free entry. The last has 0 as a link.
            The other three words in the free list are probably irrelevant.

    Entries in text table are preceeded by a word at offset-2. This word
    has the value (2*index)+1, where index is the referring keyent/valent
    entry in the table. I have no suggestion for the 2* and the +1.
    Following the word, there are N bytes of data, as per the keyent/valent
    entry length. The offset of the keyent/valent entry is from the start
    of the text table to the first data byte.

    This information is not available from Microsoft. The data format is
    deduced from the reg.dat file by me. Mistakes may
    have been made. I claim no rights and give no guarantees for this program.

    Tor Sj�wall, tor@sn.no
*/

/* reg.dat header format */
struct _w31_header {
	char		cookie[8];	/* 'SHCC3.10' */
	unsigned long	taboff1;	/* offset of hash table (??) = 0x20 */
	unsigned long	taboff2;	/* offset of index table (??) = 0x20 */
	unsigned long	tabcnt;		/* number of entries in index table */
	unsigned long	textoff;	/* offset of text part */
	unsigned long	textsize;	/* byte size of text part */
	unsigned short	hashsize;	/* hash size */
	unsigned short	freeidx;	/* free index */
};

/* generic format of table entries */
struct _w31_tabent {
	unsigned short w0, w1, w2, w3;
};

/* directory tabent: */
struct _w31_dirent {
	unsigned short	sibling_idx;	/* table index of sibling dirent */
	unsigned short	child_idx;	/* table index of child dirent */
	unsigned short	key_idx;	/* table index of key keyent */
	unsigned short	value_idx;	/* table index of value valent */
};

/* key tabent: */
struct _w31_keyent {
	unsigned short	hash_idx;	/* hash chain index for string */
	unsigned short	refcnt;		/* reference count */
	unsigned short	length;		/* length of string */
	unsigned short	string_off;	/* offset of string in text table */
};

/* value tabent: */
struct _w31_valent {
	unsigned short	hash_idx;	/* hash chain index for string */
	unsigned short	refcnt;		/* reference count */
	unsigned short	length;		/* length of string */
	unsigned short	string_off;	/* offset of string in text table */
};

/* recursive helper function to display a directory tree */
void
__w31_dumptree(	unsigned short idx,
		unsigned char *txt,
		struct _w31_tabent *tab,
		struct _w31_header *head,
		HKEY hkey,
		time_t		lastmodified,
		int		level
) {
	struct _w31_dirent	*dir;
	struct _w31_keyent	*key;
	struct _w31_valent	*val;
        HKEY subkey = 0;
	static char		tail[400];

	while (idx!=0) {
		dir=(struct _w31_dirent*)&tab[idx];

		if (dir->key_idx) {
			key = (struct _w31_keyent*)&tab[dir->key_idx];

			memcpy(tail,&txt[key->string_off],key->length);
			tail[key->length]='\0';
			/* all toplevel entries AND the entries in the 
			 * toplevel subdirectory belong to \SOFTWARE\Classes
			 */
			if (!level && !strcmp(tail,".classes")) {
				__w31_dumptree(dir->child_idx,txt,tab,head,hkey,lastmodified,level+1);
				idx=dir->sibling_idx;
				continue;
			}
                        if (subkey) RegCloseKey( subkey );
                        if (RegCreateKeyA( hkey, tail, &subkey ) != ERROR_SUCCESS) subkey = 0;
			/* only add if leaf node or valued node */
			if (dir->value_idx!=0||dir->child_idx==0) {
				if (dir->value_idx) {
					val=(struct _w31_valent*)&tab[dir->value_idx];
					memcpy(tail,&txt[val->string_off],val->length);
					tail[val->length]='\0';
                                        RegSetValueA( subkey, NULL, REG_SZ, tail, 0 );
				}
			}
		} else {
			TRACE("strange: no directory key name, idx=%04x\n", idx);
		}
		__w31_dumptree(dir->child_idx,txt,tab,head,subkey,lastmodified,level+1);
		idx=dir->sibling_idx;
	}
        if (subkey) RegCloseKey( subkey );
}


/******************************************************************************
 * _w31_loadreg [Internal]
 */
void _w31_loadreg(void) {
	HFILE			hf;
	struct _w31_header	head;
	struct _w31_tabent	*tab;
	unsigned char		*txt;
	unsigned int		len;
	OFSTRUCT		ofs;
	BY_HANDLE_FILE_INFORMATION hfinfo;
	time_t			lastmodified;

	TRACE("(void)\n");

	hf = OpenFile("reg.dat",&ofs,OF_READ);
	if (hf==HFILE_ERROR)
		return;

	/* read & dump header */
	if (sizeof(head)!=_lread(hf,&head,sizeof(head))) {
		ERR("reg.dat is too short.\n");
		_lclose(hf);
		return;
	}
	if (memcmp(head.cookie, "SHCC3.10", sizeof(head.cookie))!=0) {
		ERR("reg.dat has bad signature.\n");
		_lclose(hf);
		return;
	}

	len = head.tabcnt * sizeof(struct _w31_tabent);
	/* read and dump index table */
	tab = xmalloc(len);
	if (len!=_lread(hf,tab,len)) {
		ERR("couldn't read %d bytes.\n",len); 
		free(tab);
		_lclose(hf);
		return;
	}

	/* read text */
	txt = xmalloc(head.textsize);
	if (-1==_llseek(hf,head.textoff,SEEK_SET)) {
		ERR("couldn't seek to textblock.\n"); 
		free(tab);
		free(txt);
		_lclose(hf);
		return;
	}
	if (head.textsize!=_lread(hf,txt,head.textsize)) {
		ERR("textblock too short (%d instead of %ld).\n",len,head.textsize); 
		free(tab);
		free(txt);
		_lclose(hf);
		return;
	}

	if (!GetFileInformationByHandle(hf,&hfinfo)) {
		ERR("GetFileInformationByHandle failed?.\n"); 
		free(tab);
		free(txt);
		_lclose(hf);
		return;
	}
	lastmodified = DOSFS_FileTimeToUnixTime(&hfinfo.ftLastWriteTime,NULL);
	__w31_dumptree(tab[0].w1,txt,tab,&head,HKEY_CLASSES_ROOT,lastmodified,0);
	free(tab);
	free(txt);
	_lclose(hf);
	return;
}


static void save_at_exit( HKEY hkey, const char *path )
{
    const char *confdir = get_config_dir();
    size_t len = strlen(confdir) + strlen(path) + 2;
    if (len > REQUEST_MAX_VAR_SIZE)
    {
        ERR( "config dir '%s' too long\n", confdir );
        return;
    }
    SERVER_START_REQ
    {
        struct save_registry_atexit_request *req = server_alloc_req( sizeof(*req), len );
        sprintf( server_data_ptr(req), "%s/%s", confdir, path );
        req->hkey = hkey;
        server_call( REQ_SAVE_REGISTRY_ATEXIT );
    }
    SERVER_END_REQ;
}

/* configure save files and start the periodic saving timer */
static void SHELL_InitRegistrySaving( HKEY hkey_users_default )
{
    int all = PROFILE_GetWineIniBool( "registry", "SaveOnlyUpdatedKeys", 1 );
    int period = PROFILE_GetWineIniInt( "registry", "PeriodicSave", 0 );

    /* set saving level (0 for saving everything, 1 for saving only modified keys) */
    SERVER_START_REQ
    {
        struct set_registry_levels_request *req = server_alloc_req( sizeof(*req), 0 );
        req->current = 1;
        req->saving  = !all;
        req->period  = period * 1000;
        server_call( REQ_SET_REGISTRY_LEVELS );
    }
    SERVER_END_REQ;

    if (PROFILE_GetWineIniBool("registry","WritetoHomeRegistries",1))
    {
        save_at_exit( HKEY_CURRENT_USER, SAVE_CURRENT_USER );
        save_at_exit( HKEY_LOCAL_MACHINE, SAVE_LOCAL_MACHINE );
        save_at_exit( hkey_users_default, SAVE_DEFAULT_USER );
    }
}


/**********************************************************************************
 * SetLoadLevel [Internal]
 *
 * set level to 0 for loading system files
 * set level to 1 for loading user files
 */
static void SetLoadLevel(int level)
{
    SERVER_START_REQ
    {
        struct set_registry_levels_request *req = server_alloc_req( sizeof(*req), 0 );

	req->current = level;
	req->saving  = 0;
        req->period  = 0;
	server_call( REQ_SET_REGISTRY_LEVELS );
    }
    SERVER_END_REQ;
}

/**********************************************************************************
 * SHELL_LoadRegistry [Internal]
 */
#define REG_DONTLOAD -1
#define REG_WIN31  0
#define REG_WIN95  1
#define REG_WINNT  2

void SHELL_LoadRegistry( void )
{
  HKEY	hkey;
  char windir[MAX_PATHNAME_LEN];
  char path[MAX_PATHNAME_LEN];
  int  systemtype = REG_WIN31;
  HKEY hkey_users_default;

  TRACE("(void)\n");

  if (!CLIENT_IsBootThread()) return;  /* already loaded */

  REGISTRY_Init();
  SetLoadLevel(0);

  if (RegCreateKeyA(HKEY_USERS, ".Default", &hkey_users_default))
	  hkey_users_default = 0;

  GetWindowsDirectoryA( windir, MAX_PATHNAME_LEN );

  if (PROFILE_GetWineIniBool( "Registry", "LoadWindowsRegistryFiles", 1))
  {
    /* test %windir%/system32/config/system --> winnt */
    strcpy(path, windir);
    strncat(path, "\\system32\\config\\system", MAX_PATHNAME_LEN - strlen(path) - 1);
    if(GetFileAttributesA(path) != (DWORD)-1) 
    {
      systemtype = REG_WINNT;
    }
    else
    {
       /* test %windir%/system.dat --> win95 */
      strcpy(path, windir);
      strncat(path, "\\system.dat", MAX_PATHNAME_LEN - strlen(path) - 1);
      if(GetFileAttributesA(path) != (DWORD)-1)
      {
        systemtype = REG_WIN95;
      }
    }

    if ((systemtype==REG_WINNT)
      && (! PROFILE_GetWineIniString( "Wine", "Profile", "", path, MAX_PATHNAME_LEN)))
    {
       MESSAGE("When you are running with a native NT directory specify\n");
       MESSAGE("'Profile=<profiledirectory>' or disable loading of Windows\n");
       MESSAGE("registry (LoadWindowsRegistryFiles=N)\n");
       systemtype = REG_DONTLOAD;
    }
  }
  else
  {
    /* only wine registry */
    systemtype = REG_DONTLOAD;
  }  

  switch (systemtype)
  {
    case REG_WIN31:
      _w31_loadreg();
      break;

    case REG_WIN95:  
      /* Load windows 95 entries */
      NativeRegLoadKey(HKEY_LOCAL_MACHINE, "C:\\system.1st", 0);

      strcpy(path, windir);
      strncat(path, "\\system.dat", MAX_PATHNAME_LEN - strlen(path) - 1);
      NativeRegLoadKey(HKEY_LOCAL_MACHINE, path, 0);

      if (PROFILE_GetWineIniString( "Wine", "Profile", "", path, MAX_PATHNAME_LEN))
      {
	/* user specific user.dat */
	strncat(path, "\\user.dat", MAX_PATHNAME_LEN - strlen(path) - 1);
        if (!NativeRegLoadKey( HKEY_CURRENT_USER, path, 1 ))
	{
	  MESSAGE("can't load win95 user-registry %s\n", path);
	  MESSAGE("check wine.conf, section [Wine], value 'Profile'\n");
	}
	/* default user.dat */
	if (hkey_users_default)
	{
          strcpy(path, windir);
          strncat(path, "\\user.dat", MAX_PATHNAME_LEN - strlen(path) - 1);
          NativeRegLoadKey(hkey_users_default, path, 1);
	}
      }
      else
      {
        /* global user.dat */
	strcpy(path, windir);
        strncat(path, "\\user.dat", MAX_PATHNAME_LEN - strlen(path) - 1);
        NativeRegLoadKey(HKEY_CURRENT_USER, path, 1);
      }
      break;

    case REG_WINNT:  
      /* default user.dat */
      if (PROFILE_GetWineIniString( "Wine", "Profile", "", path, MAX_PATHNAME_LEN))
      {
        strncat(path, "\\ntuser.dat", MAX_PATHNAME_LEN - strlen(path) - 1);
        if(!NativeRegLoadKey( HKEY_CURRENT_USER, path, 1 ))
        {
           MESSAGE("can't load NT user-registry %s\n", path);
	   MESSAGE("check wine.conf, section [Wine], value 'Profile'\n");
        }
      }

      /* default user.dat */
      if (hkey_users_default)
      {
        strcpy(path, windir);
        strncat(path, "\\system32\\config\\default", MAX_PATHNAME_LEN - strlen(path) - 1);
        NativeRegLoadKey(hkey_users_default, path, 1);
      }

      /*
      * FIXME
      *  map HLM\System\ControlSet001 to HLM\System\CurrentControlSet
      */

      if (!RegCreateKeyA(HKEY_LOCAL_MACHINE, "SYSTEM", &hkey))
      {
	strcpy(path, windir);
	strncat(path, "\\system32\\config\\system", MAX_PATHNAME_LEN - strlen(path) - 1);
	NativeRegLoadKey(hkey, path, 1);
        RegCloseKey(hkey);
      }

      if (!RegCreateKeyA(HKEY_LOCAL_MACHINE, "SOFTWARE", &hkey))
      {
	strcpy(path, windir);
	strncat(path, "\\system32\\config\\software", MAX_PATHNAME_LEN - strlen(path) - 1);
	NativeRegLoadKey(hkey, path, 1);
        RegCloseKey(hkey);
      }

      strcpy(path, windir);
      strncat(path, "\\system32\\config\\sam", MAX_PATHNAME_LEN - strlen(path) - 1);
      NativeRegLoadKey(HKEY_LOCAL_MACHINE, path, 0);

      strcpy(path, windir);
      strncat(path, "\\system32\\config\\security", MAX_PATHNAME_LEN - strlen(path) - 1);
      NativeRegLoadKey(HKEY_LOCAL_MACHINE, path, 0);

      /* this key is generated when the nt-core booted successfully */
      if (!RegCreateKeyA(HKEY_LOCAL_MACHINE,"System\\Clone",&hkey))
        RegCloseKey(hkey);
      break;
  } /* switch */
  
  if (PROFILE_GetWineIniBool ("registry","LoadGlobalRegistryFiles", 1))
  {
      /* 
       * Load the global HKU hive directly from sysconfdir
       */ 
      _wine_loadreg( HKEY_USERS, SAVE_USERS_DEFAULT );

      /* 
       * Load the global machine defaults directly from sysconfdir
       */
      _wine_loadreg( HKEY_LOCAL_MACHINE, SAVE_LOCAL_MACHINE_DEFAULT );
  }

  SetLoadLevel(1);

  /*
   * Load the user saved registries 
   */
  if (PROFILE_GetWineIniBool("registry", "LoadHomeRegistryFiles", 1))
  {
      const char *confdir = get_config_dir();
      unsigned int len = strlen(confdir) + 20;
      char *fn = path;

      if (len > sizeof(path)) fn = HeapAlloc( GetProcessHeap(), 0, len );
      /* 
       * Load user's personal versions of global HKU/.Default keys
       */
      if (fn)
      {
          char *str;
          strcpy( fn, confdir );
          str = fn + strlen(fn);
          *str++ = '/';

          /* try to load HKU\.Default key only */
          strcpy( str, SAVE_DEFAULT_USER );
          if (_wine_loadreg( hkey_users_default, fn ))
          {
              /* if not found load old file containing both HKU\.Default and HKU\user */
              strcpy( str, SAVE_LOCAL_USERS_DEFAULT );
              _wine_loadreg( HKEY_USERS, fn ); 
          }

          strcpy( str, SAVE_CURRENT_USER );
          _wine_loadreg( HKEY_CURRENT_USER, fn );

          strcpy( str, SAVE_LOCAL_MACHINE );
          _wine_loadreg( HKEY_LOCAL_MACHINE, fn );

          if (fn != path) HeapFree( GetProcessHeap(), 0, fn );
      }
  }
  SHELL_InitRegistrySaving( hkey_users_default );
  RegCloseKey( hkey_users_default );
}

/********************* API FUNCTIONS ***************************************/




/******************************************************************************
 * RegFlushKey [KERNEL.227] [ADVAPI32.143]
 * Immediately writes key to registry.
 * Only returns after data has been written to disk.
 *
 * FIXME: does it really wait until data is written ?
 *
 * PARAMS
 *    hkey [I] Handle of key to write
 *
 * RETURNS
 *    Success: ERROR_SUCCESS
 *    Failure: Error code
 */
DWORD WINAPI RegFlushKey( HKEY hkey )
{
    FIXME( "(%x): stub\n", hkey );
    return ERROR_SUCCESS;
}


/******************************************************************************
 * RegUnLoadKeyA [ADVAPI32.172]
 */
LONG WINAPI RegUnLoadKeyA( HKEY hkey, LPCSTR lpSubKey )
{
    FIXME("(%x,%s): stub\n",hkey, debugstr_a(lpSubKey));
    return ERROR_SUCCESS;
}


/******************************************************************************
 * RegRestoreKeyW [ADVAPI32.164]
 *
 * PARAMS
 *    hkey    [I] Handle of key where restore begins
 *    lpFile  [I] Address of filename containing saved tree
 *    dwFlags [I] Optional flags
 */
LONG WINAPI RegRestoreKeyW( HKEY hkey, LPCWSTR lpFile, DWORD dwFlags )
{
    TRACE("(%x,%s,%ld)\n",hkey,debugstr_w(lpFile),dwFlags);

    /* It seems to do this check before the hkey check */
    if (!lpFile || !*lpFile)
        return ERROR_INVALID_PARAMETER;

    FIXME("(%x,%s,%ld): stub\n",hkey,debugstr_w(lpFile),dwFlags);

    /* Check for file existence */

    return ERROR_SUCCESS;
}


/******************************************************************************
 * RegRestoreKeyA [ADVAPI32.163]
 */
LONG WINAPI RegRestoreKeyA( HKEY hkey, LPCSTR lpFile, DWORD dwFlags )
{
    LPWSTR lpFileW = HEAP_strdupAtoW( GetProcessHeap(), 0, lpFile );
    LONG ret = RegRestoreKeyW( hkey, lpFileW, dwFlags );
    HeapFree( GetProcessHeap(), 0, lpFileW );
    return ret;
}


/******************************************************************************
 * RegReplaceKeyA [ADVAPI32.161]
 */
LONG WINAPI RegReplaceKeyA( HKEY hkey, LPCSTR lpSubKey, LPCSTR lpNewFile,
                              LPCSTR lpOldFile )
{
    FIXME("(%x,%s,%s,%s): stub\n", hkey, debugstr_a(lpSubKey),
          debugstr_a(lpNewFile),debugstr_a(lpOldFile));
    return ERROR_SUCCESS;
}






/* 16-bit functions */

/* 0 and 1 are valid rootkeys in win16 shell.dll and are used by
 * some programs. Do not remove those cases. -MM
 */
static inline void fix_win16_hkey( HKEY *hkey )
{
    if (*hkey == 0 || *hkey == 1) *hkey = HKEY_CLASSES_ROOT;
}

/******************************************************************************
 *           RegEnumKey16   [KERNEL.216] [SHELL.7]
 */
DWORD WINAPI RegEnumKey16( HKEY hkey, DWORD index, LPSTR name, DWORD name_len )
{
    fix_win16_hkey( &hkey );
    return RegEnumKeyA( hkey, index, name, name_len );
}

/******************************************************************************
 *           RegOpenKey16   [KERNEL.217] [SHELL.1]
 */
DWORD WINAPI RegOpenKey16( HKEY hkey, LPCSTR name, LPHKEY retkey )
{
    fix_win16_hkey( &hkey );
    return RegOpenKeyA( hkey, name, retkey );
}

/******************************************************************************
 *           RegCreateKey16   [KERNEL.218] [SHELL.2]
 */
DWORD WINAPI RegCreateKey16( HKEY hkey, LPCSTR name, LPHKEY retkey )
{
    fix_win16_hkey( &hkey );
    return RegCreateKeyA( hkey, name, retkey );
}

/******************************************************************************
 *           RegDeleteKey16   [KERNEL.219] [SHELL.4]
 */
DWORD WINAPI RegDeleteKey16( HKEY hkey, LPCSTR name )
{
    fix_win16_hkey( &hkey );
    return RegDeleteKeyA( hkey, name );
}

/******************************************************************************
 *           RegCloseKey16   [KERNEL.220] [SHELL.3]
 */
DWORD WINAPI RegCloseKey16( HKEY hkey )
{
    fix_win16_hkey( &hkey );
    return RegCloseKey( hkey );
}

/******************************************************************************
 *           RegSetValue16   [KERNEL.221] [SHELL.5]
 */
DWORD WINAPI RegSetValue16( HKEY hkey, LPCSTR name, DWORD type, LPCSTR data, DWORD count )
{
    fix_win16_hkey( &hkey );
    return RegSetValueA( hkey, name, type, data, count );
}

/******************************************************************************
 *           RegDeleteValue16  [KERNEL.222]
 */
DWORD WINAPI RegDeleteValue16( HKEY hkey, LPSTR name )
{
    fix_win16_hkey( &hkey );
    return RegDeleteValueA( hkey, name );
}

/******************************************************************************
 *           RegEnumValue16   [KERNEL.223]
 */
DWORD WINAPI RegEnumValue16( HKEY hkey, DWORD index, LPSTR value, LPDWORD val_count,
                             LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD count )
{
    fix_win16_hkey( &hkey );
    return RegEnumValueA( hkey, index, value, val_count, reserved, type, data, count );
}

/******************************************************************************
 *           RegQueryValue16   [KERNEL.224] [SHELL.6]
 *
 * NOTES
 *    Is this HACK still applicable?
 *
 * HACK
 *    The 16bit RegQueryValue doesn't handle selectorblocks anyway, so we just
 *    mask out the high 16 bit.  This (not so much incidently) hopefully fixes
 *    Aldus FH4)
 */
DWORD WINAPI RegQueryValue16( HKEY hkey, LPCSTR name, LPSTR data, LPDWORD count )
{
    fix_win16_hkey( &hkey );
    if (count) *count &= 0xffff;
    return RegQueryValueA( hkey, name, data, count );
}

/******************************************************************************
 *           RegQueryValueEx16   [KERNEL.225]
 */
DWORD WINAPI RegQueryValueEx16( HKEY hkey, LPCSTR name, LPDWORD reserved, LPDWORD type,
                                LPBYTE data, LPDWORD count )
{
    fix_win16_hkey( &hkey );
    return RegQueryValueExA( hkey, name, reserved, type, data, count );
}

/******************************************************************************
 *           RegSetValueEx16   [KERNEL.226]
 */
DWORD WINAPI RegSetValueEx16( HKEY hkey, LPCSTR name, DWORD reserved, DWORD type,
                              CONST BYTE *data, DWORD count )
{
    fix_win16_hkey( &hkey );
    if (!count && (type==REG_SZ)) count = strlen(data);
    return RegSetValueExA( hkey, name, reserved, type, data, count );
}

/******************************************************************************
 *           RegFlushKey16   [KERNEL.227]
 */
DWORD WINAPI RegFlushKey16( HKEY hkey )
{
    fix_win16_hkey( &hkey );
    return RegFlushKey( hkey );
}
