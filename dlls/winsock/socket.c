/*
 * based on Windows Sockets 1.1 specs
 * (ftp.microsoft.com:/Advsys/winsock/spec11/WINSOCK.TXT)
 * 
 * (C) 1993,1994,1996,1997 John Brezak, Erik Bos, Alex Korobka.
 *
 * NOTE: If you make any changes to fix a particular app, make sure 
 * they don't break something else like Netscape or telnet and ftp 
 * clients and servers (www.winsite.com got a lot of those).
 *
 * NOTE 2: Many winsock structs such as servent, hostent, protoent, ...
 * are used with 1-byte alignment for Win16 programs and 4-byte alignment
 * for Win32 programs in winsock.h. winsock2.h uses forced 4-byte alignment.
 * So we have non-forced (just as MSDN) ws_XXXXent (winsock.h), 4-byte forced
 * ws_XXXXent32 (winsock2.h) and 1-byte forced ws_XXXXent16 (winsock16.h).
 */
 
#include "config.h"

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#ifdef HAVE_SYS_IPC_H
# include <sys/ipc.h>
#endif
#include <sys/ioctl.h>
#ifdef HAVE_SYS_FILIO_H
# include <sys/filio.h>
#endif
#ifdef HAVE_SYS_SOCKIO_H
# include <sys/sockio.h>
#endif

#if defined(__EMX__)
# include <sys/so_ioctl.h>
#endif

#ifdef HAVE_SYS_PARAM_H
# include <sys/param.h>
#endif

#ifdef HAVE_SYS_MSG_H
# include <sys/msg.h>
#endif
#ifdef HAVE_SYS_WAIT_H
# include <sys/wait.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_NETINET_IN_H
# include <netinet/in.h>
#endif
#ifdef HAVE_NETINET_TCP_H
# include <netinet/tcp.h>
#endif
#ifdef HAVE_ARPA_INET_H
# include <arpa/inet.h>
#endif
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#ifdef HAVE_SYS_ERRNO_H
#include <sys/errno.h>
#endif
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#ifdef HAVE_ARPA_NAMESER_H
# include <arpa/nameser.h>
#endif
#ifdef HAVE_RESOLV_H
# include <resolv.h>
#endif
#ifdef HAVE_NET_IF_H
# include <net/if.h>
#endif
#ifdef HAVE_IPX_GNU
# include <netipx/ipx.h>
# define HAVE_IPX
#endif
#ifdef HAVE_IPX_LINUX
# include <asm/types.h>
# include <linux/ipx.h>
# define HAVE_IPX
#endif

#include "wine/winbase16.h"
#include "wingdi.h"
#include "winuser.h"
#include "winsock2.h"
#include "wine/winsock16.h"
#include "winnt.h"
#include "heap.h"
#include "task.h"
#include "wine/port.h"
#include "services.h"
#include "server.h"
#include "file.h"
#include "debugtools.h"


DEFAULT_DEBUG_CHANNEL(winsock);

/* critical section to protect some non-rentrant net function */
extern CRITICAL_SECTION csWSgetXXXbyYYY;

#define DEBUG_SOCKADDR 0
#define dump_sockaddr(a) \
        DPRINTF("sockaddr_in: family %d, address %s, port %d\n", \
                        ((struct sockaddr_in *)a)->sin_family, \
                        inet_ntoa(((struct sockaddr_in *)a)->sin_addr), \
                        ntohs(((struct sockaddr_in *)a)->sin_port))

/* ----------------------------------- internal data */

/* ws_... struct conversion flags */

#define WS_DUP_LINEAR           0x0001
#define WS_DUP_NATIVE           0x0000          /* not used anymore */
#define WS_DUP_OFFSET           0x0002          /* internal pointers are offsets */
#define WS_DUP_SEGPTR           0x0004          /* internal pointers are SEGPTRs */
                                                /* by default, internal pointers are linear */
typedef struct          /* WSAAsyncSelect() control struct */
{
  HANDLE      service, event, sock;
  HWND        hWnd;
  UINT        uMsg;
  LONG        lEvent;
  struct _WSINFO *pwsi;
} ws_select_info;  

#define WS_MAX_SOCKETS_PER_PROCESS      128     /* reasonable guess */
#define WS_MAX_UDP_DATAGRAM             1024

#define WS_ACCEPT_QUEUE 6

#define WSI_BLOCKINGCALL        0x00000001      /* per-thread info flags */
#define WSI_BLOCKINGHOOK        0x00000002      /* 32-bit callback */

#define PROCFS_NETDEV_FILE   "/proc/net/dev" /* Points to the file in the /proc fs 
                                                that lists the network devices.
                                                Do we need an #ifdef LINUX for this? */

typedef struct _WSINFO
{
  DWORD			dwThisProcess;
  struct _WSINFO       *lpNextIData;

  unsigned		flags;
  INT16			num_startup;		/* reference counter */
  INT16			num_async_rq;
  INT16			last_free;		/* entry in the socket table */
  UINT16		buflen;
  char*			buffer;			/* allocated from SEGPTR heap */
  void			*he;			/* typecast for Win16/32 ws_hostent */
  int			helen;
  void			*se;			/* typecast for Win16/32 ws_servent */
  int			selen;
  void			*pe;			/* typecast for Win16/32 ws_protoent */
  int			pelen;
  char*			dbuffer;		/* buffer for dummies (32 bytes) */

  DWORD			blocking_hook;

  volatile HANDLE	accept_old[WS_ACCEPT_QUEUE], accept_new[WS_ACCEPT_QUEUE];
} WSINFO, *LPWSINFO;

/* function prototypes */
int WS_dup_he(LPWSINFO pwsi, struct hostent* p_he, int flag);
int WS_dup_pe(LPWSINFO pwsi, struct protoent* p_pe, int flag);
int WS_dup_se(LPWSINFO pwsi, struct servent* p_se, int flag);

typedef void	WIN_hostent;
typedef void	WIN_protoent;
typedef void	WIN_servent;

int WSAIOCTL_GetInterfaceCount(void);
int WSAIOCTL_GetInterfaceName(int intNumber, char *intName);

UINT16 wsaErrno(void);
UINT16 wsaHerrno(int errnr);
                                                      
static HANDLE 	_WSHeap = 0;

#define WS_ALLOC(size) \
	HeapAlloc(_WSHeap, HEAP_ZERO_MEMORY, (size) )
#define WS_FREE(ptr) \
	HeapFree(_WSHeap, 0, (ptr) )

static INT         _ws_sock_ops[] =
       { WS_SO_DEBUG, WS_SO_REUSEADDR, WS_SO_KEEPALIVE, WS_SO_DONTROUTE,
         WS_SO_BROADCAST, WS_SO_LINGER, WS_SO_OOBINLINE, WS_SO_SNDBUF,
         WS_SO_RCVBUF, WS_SO_ERROR, WS_SO_TYPE,
#ifdef SO_RCVTIMEO
	 WS_SO_RCVTIMEO,
#endif
#ifdef SO_SNDTIMEO
	 WS_SO_SNDTIMEO,
#endif
	 0 };
static int           _px_sock_ops[] =
       { SO_DEBUG, SO_REUSEADDR, SO_KEEPALIVE, SO_DONTROUTE, SO_BROADCAST,
         SO_LINGER, SO_OOBINLINE, SO_SNDBUF, SO_RCVBUF, SO_ERROR, SO_TYPE,
#ifdef SO_RCVTIMEO
	 SO_RCVTIMEO,
#endif
#ifdef SO_SNDTIMEO
	 SO_SNDTIMEO,
#endif
	};

static INT _ws_tcp_ops[] = {
#ifdef TCP_NODELAY
	WS_TCP_NODELAY,
#endif
	0
};
static int _px_tcp_ops[] = {
#ifdef TCP_NODELAY
	TCP_NODELAY,
#endif
	0
};

/* we need a special routine to handle WSA* errors */
static inline int sock_server_call( enum request req )
{
    unsigned int res = server_call_noerr( req );
    if (res)
    {
        /* do not map WSA errors */
        if ((res < WSABASEERR) || (res >= 0x10000000)) res = RtlNtStatusToDosError(res);
        SetLastError( res );
    }
    return res;
}

static int   _check_ws(LPWSINFO pwsi, SOCKET s);
static char* _check_buffer(LPWSINFO pwsi, int size);

static int _get_sock_fd(SOCKET s)
{
    int fd = FILE_GetUnixHandle( s, GENERIC_READ );
    if (fd == -1)
        FIXME("handle %d is not a socket (GLE %ld)\n",s,GetLastError());
    return fd;    
}

static void _enable_event(SOCKET s, unsigned int event,
			  unsigned int sstate, unsigned int cstate)
{
    SERVER_START_REQ
    {
        struct enable_socket_event_request *req = server_alloc_req( sizeof(*req), 0 );

        req->handle = s;
        req->mask   = event;
        req->sstate = sstate;
        req->cstate = cstate;
        sock_server_call( REQ_ENABLE_SOCKET_EVENT );
    }
    SERVER_END_REQ;
}

static int _is_blocking(SOCKET s)
{
    int ret;
    SERVER_START_REQ
    {
        struct get_socket_event_request *req = server_alloc_req( sizeof(*req), 0 );

        req->handle  = s;
        req->service = FALSE;
        req->s_event = 0;
        req->c_event = 0;
        sock_server_call( REQ_GET_SOCKET_EVENT );
        ret = (req->state & WS_FD_NONBLOCKING) == 0;
    }
    SERVER_END_REQ;
    return ret;
}

static unsigned int _get_sock_mask(SOCKET s)
{
    unsigned int ret;
    SERVER_START_REQ
    {
        struct get_socket_event_request *req = server_alloc_req( sizeof(*req), 0 );

        req->handle  = s;
        req->service = FALSE;
        req->s_event = 0;
        req->c_event = 0;
        sock_server_call( REQ_GET_SOCKET_EVENT );
        ret = req->mask;
    }
    SERVER_END_REQ;
    return ret;
}

static void _sync_sock_state(SOCKET s)
{
    /* do a dummy wineserver request in order to let
       the wineserver run through its select loop once */
    (void)_is_blocking(s);
}

static int _get_sock_error(SOCKET s, unsigned int bit)
{
    int ret;
    SERVER_START_REQ
    {
        struct get_socket_event_request *req = server_alloc_req( sizeof(*req),
                                                                 FD_MAX_EVENTS*sizeof(int) );
        req->handle  = s;
        req->service = FALSE;
        req->s_event = 0;
        req->c_event = 0;
        sock_server_call( REQ_GET_SOCKET_EVENT );
        ret = *((int *)server_data_ptr(req) + bit);
    }
    SERVER_END_REQ;
    return ret;
}

static LPWSINFO lpFirstIData = NULL;

static LPWSINFO WINSOCK_GetIData(void)
{
    DWORD pid = GetCurrentProcessId();
    LPWSINFO iData;

    for (iData = lpFirstIData; iData; iData = iData->lpNextIData) {
	if (iData->dwThisProcess == pid)
	    break;
    }
    return iData;
}

static BOOL WINSOCK_CreateIData(void)
{
    LPWSINFO iData;
    
    iData = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(WSINFO));
    if (!iData)
	return FALSE;
    iData->dwThisProcess = GetCurrentProcessId();
    iData->lpNextIData = lpFirstIData;
    lpFirstIData = iData;
    return TRUE;
}

static void WINSOCK_DeleteIData(void)
{
    LPWSINFO iData = WINSOCK_GetIData();
    LPWSINFO* ppid;
    if (iData) {
	for (ppid = &lpFirstIData; *ppid; ppid = &(*ppid)->lpNextIData) {
	    if (*ppid == iData) {
	        *ppid = iData->lpNextIData;
	        break;
	    }
	}

	if( iData->flags & WSI_BLOCKINGCALL )
	    TRACE("\tinside blocking call!\n");

	/* delete scratch buffers */

	if( iData->buffer ) SEGPTR_FREE(iData->buffer);
	if( iData->dbuffer ) SEGPTR_FREE(iData->dbuffer);

	HeapFree(GetProcessHeap(), 0, iData);
    }
}

/***********************************************************************
 *		WSOCK32_LibMain (WSOCK32.init)
 */
BOOL WINAPI WSOCK32_LibMain(HINSTANCE hInstDLL, DWORD fdwReason, LPVOID fImpLoad)
{
    TRACE("0x%x 0x%lx %p\n", hInstDLL, fdwReason, fImpLoad);
    switch (fdwReason) {
    case DLL_PROCESS_DETACH:
	WINSOCK_DeleteIData();
	break;
    }
    return TRUE;
}

/***********************************************************************
 *		WINSOCK_LibMain (WINSOCK.init)
 */
BOOL WINAPI WINSOCK_LibMain(DWORD fdwReason, HINSTANCE hInstDLL, WORD ds,
                            WORD wHeapSize, DWORD dwReserved1, WORD wReserved2)
{
    TRACE("0x%x 0x%lx\n", hInstDLL, fdwReason);
    switch (fdwReason) {
    case DLL_PROCESS_DETACH:
	WINSOCK_DeleteIData();
	break;
    }
    return TRUE;
}
                                                                          
/***********************************************************************
 *          convert_sockopt()
 *
 * Converts socket flags from Windows format.
 * Return 1 if converted, 0 if not (error).
 */
static int convert_sockopt(INT *level, INT *optname)
{
  int i;
  switch (*level)
  {
     case WS_SOL_SOCKET:
        *level = SOL_SOCKET;
        for(i=0; _ws_sock_ops[i]; i++)
            if( _ws_sock_ops[i] == *optname ) break;
        if( _ws_sock_ops[i] ) {
	    *optname = _px_sock_ops[i];
	    return 1;
	}
        FIXME("Unknown SOL_SOCKET optname 0x%x\n", *optname);
        break;
     case WS_IPPROTO_TCP:
        *level = IPPROTO_TCP;
        for(i=0; _ws_tcp_ops[i]; i++)
		if ( _ws_tcp_ops[i] == *optname ) break;
        if( _ws_tcp_ops[i] ) {
	    *optname = _px_tcp_ops[i];
	    return 1;
	}
        FIXME("Unknown IPPROTO_TCP optname 0x%x\n", *optname);
	break;
  }
  return 0;
}

/* ----------------------------------- Per-thread info (or per-process?) */

static int wsi_strtolo(LPWSINFO pwsi, const char* name, const char* opt)
{
    /* Stuff a lowercase copy of the string into the local buffer */

    int i = strlen(name) + 2;
    char* p = _check_buffer(pwsi, i + ((opt)?strlen(opt):0));

    if( p )
    {
	do *p++ = tolower(*name); while(*name++);
	i = (p - (char*)(pwsi->buffer));
	if( opt ) do *p++ = tolower(*opt); while(*opt++);
	return i;
    }
    return 0;
}

static fd_set* fd_set_import( fd_set* fds, LPWSINFO pwsi, void* wsfds, int* highfd, int lfd[], BOOL b32 )
{
    /* translate Winsock fd set into local fd set */

    if( wsfds ) 
    { 
#define wsfds16	((ws_fd_set16*)wsfds)
#define wsfds32 ((ws_fd_set32*)wsfds)
	int i, count;

	FD_ZERO(fds);
	count = b32 ? wsfds32->fd_count : wsfds16->fd_count;

	for( i = 0; i < count; i++ )
	{
	     int s = (b32) ? wsfds32->fd_array[i]
			   : wsfds16->fd_array[i];
	     if( _check_ws(pwsi, s) )
             {
                    int fd = _get_sock_fd(s);
                    lfd[ i ] = fd;
		    if( fd > *highfd ) *highfd = fd;
		    FD_SET(fd, fds);
	     }
	     else lfd[ i ] = -1;
	}
#undef wsfds32
#undef wsfds16
	return fds;
    }
    return NULL;
}

inline static int sock_error_p(int s)
{
    unsigned int optval, optlen;

    optlen = sizeof(optval);
    getsockopt(s, SOL_SOCKET, SO_ERROR, (void *) &optval, &optlen);
    if (optval) WARN("\t[%i] error: %d\n", s, optval);
    return optval != 0;
}

static int fd_set_export( LPWSINFO pwsi, fd_set* fds, fd_set* exceptfds, void* wsfds, int lfd[], BOOL b32 )
{
    int num_err = 0;

    /* translate local fd set into Winsock fd set, adding
     * errors to exceptfds (only if app requested it) */

    if( wsfds )
    {
#define wsfds16 ((ws_fd_set16*)wsfds)
#define wsfds32 ((ws_fd_set32*)wsfds)
	int i, j, count = (b32) ? wsfds32->fd_count : wsfds16->fd_count;

	for( i = 0, j = 0; i < count; i++ )
	{
	    if( lfd[i] >= 0 )
	    {
		int fd = lfd[i];
		if( FD_ISSET(fd, fds) )
		{
		    if ( exceptfds && sock_error_p(fd) )
		    {
			FD_SET(fd, exceptfds);
			num_err++;
		    }
		    else if( b32 )
			     wsfds32->fd_array[j++] = wsfds32->fd_array[i];
			 else
			     wsfds16->fd_array[j++] = wsfds16->fd_array[i];
		}
		close(fd);
		lfd[i] = -1;
	    }
	}

	if( b32 ) wsfds32->fd_count = j;
	else wsfds16->fd_count = j;

	TRACE("\n");
#undef wsfds32
#undef wsfds16
    }
    return num_err;
}

static void fd_set_unimport( void* wsfds, int lfd[], BOOL b32 )
{
    if ( wsfds )
    {
#define wsfds16 ((ws_fd_set16*)wsfds)
#define wsfds32 ((ws_fd_set32*)wsfds)
	int i, count = (b32) ? wsfds32->fd_count : wsfds16->fd_count;

	for( i = 0; i < count; i++ )
	    if ( lfd[i] >= 0 )
		close(lfd[i]);

	TRACE("\n");
#undef wsfds32
#undef wsfds16
    }
}

static int do_block( int fd, int mask )
{
    fd_set fds[3];
    int i, r;

    FD_ZERO(&fds[0]);
    FD_ZERO(&fds[1]);
    FD_ZERO(&fds[2]);
    for (i=0; i<3; i++)
	if (mask & (1<<i))
	    FD_SET(fd, &fds[i]);
    i = select( fd+1, &fds[0], &fds[1], &fds[2], NULL );
    if (i <= 0) return -1;
    r = 0;
    for (i=0; i<3; i++)
	if (FD_ISSET(fd, &fds[i]))
	    r |= 1<<i;
    return r;
}

void* __ws_memalloc( int size )
{
    return WS_ALLOC(size);
}

void __ws_memfree(void* ptr)
{
    WS_FREE(ptr);
}


/* ----------------------------------- API ----- 
 *
 * Init / cleanup / error checking.
 */

/***********************************************************************
 *      WSAStartup16()			(WINSOCK.115)
 *
 * Create socket control struct, attach it to the global list and
 * update a pointer in the task struct.
 */
INT16 WINAPI WSAStartup16(UINT16 wVersionRequested, LPWSADATA lpWSAData)
{
    WSADATA WINSOCK_data = { 0x0101, 0x0101,
                          "WINE Sockets 1.1",
                        #ifdef linux
                                "Linux/i386",
                        #elif defined(__NetBSD__)
                                "NetBSD/i386",
                        #elif defined(sunos)
                                "SunOS",
                        #elif defined(__FreeBSD__)
                                "FreeBSD",
                        #elif defined(__OpenBSD__)
                                "OpenBSD/i386",
                        #else
                                "Unknown",
                        #endif
			   WS_MAX_SOCKETS_PER_PROCESS,
			   WS_MAX_UDP_DATAGRAM, (SEGPTR)NULL };
    LPWSINFO            pwsi;

    TRACE("verReq=%x\n", wVersionRequested);

    if (LOBYTE(wVersionRequested) < 1 || (LOBYTE(wVersionRequested) == 1 &&
        HIBYTE(wVersionRequested) < 1)) return WSAVERNOTSUPPORTED;

    if (!lpWSAData) return WSAEINVAL;

    /* initialize socket heap */

    if( !_WSHeap )
    {
	_WSHeap = HeapCreate(HEAP_ZERO_MEMORY, 8120, 32768);
	if( !_WSHeap )
	{
	    ERR("Fatal: failed to create WinSock heap\n");
	    return 0;
	}
    }
    if( _WSHeap == 0 ) return WSASYSNOTREADY;

    pwsi = WINSOCK_GetIData();
    if( pwsi == NULL )
    {
        WINSOCK_CreateIData();
        pwsi = WINSOCK_GetIData();
	if (!pwsi) return WSASYSNOTREADY;
    }
    pwsi->num_startup++;

    /* return winsock information */

    memcpy(lpWSAData, &WINSOCK_data, sizeof(WINSOCK_data));

    TRACE("succeeded\n");
    return 0;
}

/***********************************************************************
 *      WSAStartup()		(WS2_32.115)	
 */
INT WINAPI WSAStartup(UINT wVersionRequested, LPWSADATA lpWSAData)
{
    WSADATA WINSOCK_data = { 0x0202, 0x0202,
                          "WINE Sockets 2.0",
                        #ifdef linux
                                "Linux/i386",
                        #elif defined(__NetBSD__)
                                "NetBSD/i386",
                        #elif defined(sunos)
                                "SunOS",
                        #elif defined(__FreeBSD__)
                                "FreeBSD",
                        #elif defined(__OpenBSD__)
                                "OpenBSD/i386",
                        #else
                                "Unknown",
                        #endif
			   WS_MAX_SOCKETS_PER_PROCESS,
			   WS_MAX_UDP_DATAGRAM, (SEGPTR)NULL };
    LPWSINFO            pwsi;

    TRACE("verReq=%x\n", wVersionRequested);

    if (LOBYTE(wVersionRequested) < 1)
        return WSAVERNOTSUPPORTED;

    if (!lpWSAData) return WSAEINVAL;

    /* initialize socket heap */

    if( !_WSHeap )
    {
	_WSHeap = HeapCreate(HEAP_ZERO_MEMORY, 8120, 32768);
	if( !_WSHeap )
	{
	    ERR("Fatal: failed to create WinSock heap\n");
	    return 0;
	}
    }
    if( _WSHeap == 0 ) return WSASYSNOTREADY;

    pwsi = WINSOCK_GetIData();
    if( pwsi == NULL )
    {
        WINSOCK_CreateIData();
        pwsi = WINSOCK_GetIData();
	if (!pwsi) return WSASYSNOTREADY;
    }
    pwsi->num_startup++;

    /* return winsock information */
    memcpy(lpWSAData, &WINSOCK_data, sizeof(WINSOCK_data));

    /* that's the whole of the negotiation for now */
    lpWSAData->wVersion = wVersionRequested;

    TRACE("succeeded\n");
    return 0;
}


/***********************************************************************
 *      WSACleanup()			(WINSOCK.116)
 */
INT WINAPI WSACleanup(void)
{
    LPWSINFO pwsi = WINSOCK_GetIData();
    if( pwsi ) {
	if( --pwsi->num_startup > 0 ) return 0;

	WINSOCK_DeleteIData();
	return 0;
    }
    SetLastError(WSANOTINITIALISED);
    return SOCKET_ERROR;
}


/***********************************************************************
 *      WSAGetLastError()		(WSOCK32.111)(WINSOCK.111)
 */
INT WINAPI WSAGetLastError(void)
{
	return GetLastError();
}

/***********************************************************************
 *      WSASetLastError()		(WSOCK32.112)
 */
void WINAPI WSASetLastError(INT iError) {
    SetLastError(iError);
}

/***********************************************************************
 *      WSASetLastError16()		(WINSOCK.112)
 */
void WINAPI WSASetLastError16(INT16 iError)
{
    WSASetLastError(iError);
}

int _check_ws(LPWSINFO pwsi, SOCKET s)
{
    if( pwsi )
    {
	int fd;
	if( pwsi->flags & WSI_BLOCKINGCALL ) SetLastError(WSAEINPROGRESS);
	if ( (fd = _get_sock_fd(s)) < 0 ) {
	    SetLastError(WSAENOTSOCK);
	    return 0;
	}
	/* FIXME: maybe check whether fd is really a socket? */
	close( fd );
	return 1;
    }
    return 0;
}

char* _check_buffer(LPWSINFO pwsi, int size)
{
    if( pwsi->buffer && pwsi->buflen >= size ) return pwsi->buffer;
    else SEGPTR_FREE(pwsi->buffer);

    pwsi->buffer = (char*)SEGPTR_ALLOC((pwsi->buflen = size)); 
    return pwsi->buffer;
}

struct ws_hostent* _check_buffer_he(LPWSINFO pwsi, int size)
{
    if( pwsi->he && pwsi->helen >= size ) return pwsi->he;
    else SEGPTR_FREE(pwsi->he);

    pwsi->he = SEGPTR_ALLOC((pwsi->helen = size)); 
    return pwsi->he;
}

void* _check_buffer_se(LPWSINFO pwsi, int size)
{
    if( pwsi->se && pwsi->selen >= size ) return pwsi->se;
    else SEGPTR_FREE(pwsi->se);

    pwsi->se = SEGPTR_ALLOC((pwsi->selen = size)); 
    return pwsi->se;
}

struct ws_protoent* _check_buffer_pe(LPWSINFO pwsi, int size)
{
    if( pwsi->pe && pwsi->pelen >= size ) return pwsi->pe;
    else SEGPTR_FREE(pwsi->pe);

    pwsi->pe = SEGPTR_ALLOC((pwsi->pelen = size)); 
    return pwsi->pe;
}

/* ----------------------------------- i/o APIs */

/***********************************************************************
 *		accept()		(WSOCK32.1)
 */
static void WSOCK32_async_accept(LPWSINFO pwsi, SOCKET s, SOCKET as)
{
    int q;
    /* queue socket for WSAAsyncSelect */
    for (q=0; q<WS_ACCEPT_QUEUE; q++)
	if (InterlockedCompareExchange((PVOID*)&pwsi->accept_old[q], (PVOID)s, (PVOID)0) == (PVOID)0)
	    break;
    if (q<WS_ACCEPT_QUEUE)
	pwsi->accept_new[q] = as;
    else
	ERR("accept queue too small\n");
    /* now signal our AsyncSelect handler */
    _enable_event(s, WS_FD_SERVEVENT, 0, 0);
}

SOCKET WINAPI WSOCK32_accept(SOCKET s, struct sockaddr *addr,
                                 INT *addrlen32)
{
    LPWSINFO                 pwsi = WINSOCK_GetIData();
#ifdef HAVE_IPX
    struct ws_sockaddr_ipx*  addr2 = (struct ws_sockaddr_ipx *)addr;
#endif

    TRACE("(%08x): socket %04x\n", 
				  (unsigned)pwsi, (UINT16)s ); 
    if( _check_ws(pwsi, s) )
    {
        SOCKET as;
	if (_is_blocking(s))
	{
	    /* block here */
	    int fd = _get_sock_fd(s);
	    do_block(fd, 5);
	    close(fd);
	    _sync_sock_state(s); /* let wineserver notice connection */
	    /* retrieve any error codes from it */
	    SetLastError(_get_sock_error(s, FD_ACCEPT_BIT));
	    /* FIXME: care about the error? */
	}
        SERVER_START_REQ
        {
            struct accept_socket_request *req = server_alloc_req( sizeof(*req), 0 );

            req->lhandle = s;
            req->access  = GENERIC_READ|GENERIC_WRITE|SYNCHRONIZE;
            req->inherit = TRUE;
            sock_server_call( REQ_ACCEPT_SOCKET );
            as = req->handle;
        }
        SERVER_END_REQ;
	if( ((int)as) >= 0 )
	{
	    unsigned omask = _get_sock_mask( s );
	    int fd = _get_sock_fd( as );
	    if( getpeername(fd, addr, addrlen32) != -1 )
	    {
#ifdef HAVE_IPX
		if (addr && ((struct sockaddr_ipx *)addr)->sipx_family == AF_IPX) {
		    addr = (struct sockaddr *)
				malloc(addrlen32 ? *addrlen32 : sizeof(*addr2));
		    memcpy(addr, addr2,
				addrlen32 ? *addrlen32 : sizeof(*addr2));
		    addr2->sipx_family = WS_AF_IPX;
		    addr2->sipx_network = ((struct sockaddr_ipx *)addr)->sipx_network;
		    addr2->sipx_port = ((struct sockaddr_ipx *)addr)->sipx_port;
		    memcpy(addr2->sipx_node,
			((struct sockaddr_ipx *)addr)->sipx_node, IPX_NODE_LEN);
		    free(addr);
		}
#endif
	    } else SetLastError(wsaErrno());
	    close(fd);
	    if (omask & WS_FD_SERVEVENT)
		WSOCK32_async_accept(pwsi, s, as);
	    return as;
	}
    }
    return INVALID_SOCKET;
}

/***********************************************************************
 *              accept()		(WINSOCK.1)
 */
SOCKET16 WINAPI WINSOCK_accept16(SOCKET16 s, struct sockaddr* addr,
                                 INT16* addrlen16 )
{
    INT addrlen32 = addrlen16 ? *addrlen16 : 0;
    SOCKET retSocket = WSOCK32_accept( s, addr, &addrlen32 );
    if( addrlen16 ) *addrlen16 = (INT16)addrlen32;
    return (SOCKET16)retSocket;
}

/***********************************************************************
 *		bind()			(WSOCK32.2)
 */
INT WINAPI WSOCK32_bind(SOCKET s, struct sockaddr *name, INT namelen)
{
    LPWSINFO                 pwsi = WINSOCK_GetIData();
#ifdef HAVE_IPX
    struct ws_sockaddr_ipx*  name2 = (struct ws_sockaddr_ipx *)name;
#endif

    TRACE("(%08x): socket %04x, ptr %8x, length %d\n", 
			   (unsigned)pwsi, s, (int) name, namelen);
#if DEBUG_SOCKADDR
    dump_sockaddr(name);
#endif

    if ( _check_ws(pwsi, s) )
    {
      int fd = _get_sock_fd(s);
      /* FIXME: what family does this really map to on the Unix side? */
      if (name && ((struct ws_sockaddr_ipx *)name)->sipx_family == WS_AF_PUP)
	((struct ws_sockaddr_ipx *)name)->sipx_family = AF_UNSPEC;
#ifdef HAVE_IPX
      else if (name &&
		((struct ws_sockaddr_ipx *)name)->sipx_family == WS_AF_IPX)
      {
	name = (struct sockaddr *) malloc(sizeof(struct sockaddr_ipx));
	memset(name, '\0', sizeof(struct sockaddr_ipx));
	((struct sockaddr_ipx *)name)->sipx_family = AF_IPX;
	((struct sockaddr_ipx *)name)->sipx_port = name2->sipx_port;
	((struct sockaddr_ipx *)name)->sipx_network = name2->sipx_network;
	memcpy(((struct sockaddr_ipx *)name)->sipx_node,
		name2->sipx_node, IPX_NODE_LEN);
	namelen = sizeof(struct sockaddr_ipx);
      }
#endif
      if ( namelen >= sizeof(*name) ) 
      {
	if ( name && (((struct ws_sockaddr_in *)name)->sin_family == AF_INET
#ifdef HAVE_IPX
             || ((struct sockaddr_ipx *)name)->sipx_family == AF_IPX
#endif
           ))
        {
	  if ( bind(fd, name, namelen) < 0 ) 
	  {
	     int	loc_errno = errno;
	     WARN("\tfailure - errno = %i\n", errno);
	     errno = loc_errno;
	     switch(errno)
	     {
		case EBADF: SetLastError(WSAENOTSOCK); break;
		case EADDRNOTAVAIL: SetLastError(WSAEINVAL); break;
		default: SetLastError(wsaErrno());break;
	     }
	  }
	  else {
#ifdef HAVE_IPX
	    if (((struct sockaddr_ipx *)name)->sipx_family == AF_IPX)
		free(name);
#endif
	    close(fd);
	    return 0; /* success */
	  }
        } else SetLastError(WSAEAFNOSUPPORT);
      } else SetLastError(WSAEFAULT);
#ifdef HAVE_IPX
      if (name && ((struct sockaddr_ipx *)name)->sipx_family == AF_IPX)
	free(name);
#endif
      close(fd);
    }
    return SOCKET_ERROR;
}

/***********************************************************************
 *              bind()			(WINSOCK.2)
 */
INT16 WINAPI WINSOCK_bind16(SOCKET16 s, struct sockaddr *name, INT16 namelen)
{
  return (INT16)WSOCK32_bind( s, name, namelen );
}

/***********************************************************************
 *		closesocket()		(WSOCK32.3)
 */
INT WINAPI WSOCK32_closesocket(SOCKET s)
{
    LPWSINFO      pwsi = WINSOCK_GetIData();

    TRACE("(%08x): socket %08x\n", (unsigned)pwsi, s);

    if( _check_ws(pwsi, s) )
    { 
	if( CloseHandle(s) )
	    return 0;
    }
    return SOCKET_ERROR;
}

/***********************************************************************
 *              closesocket()           (WINSOCK.3)
 */
INT16 WINAPI WINSOCK_closesocket16(SOCKET16 s)
{
    return (INT16)WSOCK32_closesocket(s);
}

/***********************************************************************
 *		connect()		(WSOCK32.4)
 */
INT WINAPI WSOCK32_connect(SOCKET s, struct sockaddr *name, INT namelen)
{
  LPWSINFO                 pwsi = WINSOCK_GetIData();
#ifdef HAVE_IPX
  struct ws_sockaddr_ipx*  name2 = (struct ws_sockaddr_ipx *)name;
#endif

  TRACE("(%08x): socket %04x, ptr %8x, length %d\n", 
			   (unsigned)pwsi, s, (int) name, namelen);
#if DEBUG_SOCKADDR
  dump_sockaddr(name);
#endif

  if( _check_ws(pwsi, s) )
  {
    int fd = _get_sock_fd(s);
    if (name && ((struct ws_sockaddr_ipx *)name)->sipx_family == WS_AF_PUP)
	((struct ws_sockaddr_ipx *)name)->sipx_family = AF_UNSPEC;
#ifdef HAVE_IPX
    else if (name && ((struct ws_sockaddr_ipx *)name)->sipx_family == WS_AF_IPX)
    {
	name = (struct sockaddr *) malloc(sizeof(struct sockaddr_ipx));
	memset(name, '\0', sizeof(struct sockaddr_ipx));
	((struct sockaddr_ipx *)name)->sipx_family = AF_IPX;
	((struct sockaddr_ipx *)name)->sipx_port = name2->sipx_port;
	((struct sockaddr_ipx *)name)->sipx_network = name2->sipx_network;
	memcpy(((struct sockaddr_ipx *)name)->sipx_node,
		name2->sipx_node, IPX_NODE_LEN);
	namelen = sizeof(struct sockaddr_ipx);
    }
#endif
    if (connect(fd, name, namelen) == 0) {
	close(fd);
	goto connect_success;
    }
    if (errno == EINPROGRESS)
    {
	/* tell wineserver that a connection is in progress */
	_enable_event(s, FD_CONNECT|FD_READ|FD_WRITE,
		      WS_FD_CONNECT|WS_FD_READ|WS_FD_WRITE,
		      WS_FD_CONNECTED|WS_FD_LISTENING);
	if (_is_blocking(s))
	{
	    int result;
	    /* block here */
	    do_block(fd, 6);
	    _sync_sock_state(s); /* let wineserver notice connection */
	    /* retrieve any error codes from it */
	    result = _get_sock_error(s, FD_CONNECT_BIT);
	    if (result)
		SetLastError(result);
	    else {
		close(fd);
		goto connect_success;
	    }
	}
	else SetLastError(WSAEWOULDBLOCK);
	close(fd);
    }
    else
    {
	SetLastError(wsaErrno());
	close(fd);
    }
  }
#ifdef HAVE_IPX
  if (name && ((struct sockaddr_ipx *)name)->sipx_family == AF_IPX)
    free(name);
#endif
  return SOCKET_ERROR;
connect_success:
#ifdef HAVE_IPX
    if (((struct sockaddr_ipx *)name)->sipx_family == AF_IPX)
	free(name);
#endif
    _enable_event(s, FD_CONNECT|FD_READ|FD_WRITE,
		  WS_FD_CONNECTED|WS_FD_READ|WS_FD_WRITE,
		  WS_FD_CONNECT|WS_FD_LISTENING);
    return 0; 
}

/***********************************************************************
 *              connect()               (WINSOCK.4)
 */
INT16 WINAPI WINSOCK_connect16(SOCKET16 s, struct sockaddr *name, INT16 namelen)
{
  return (INT16)WSOCK32_connect( s, name, namelen );
}

/***********************************************************************
 *		getpeername()		(WSOCK32.5)
 */
INT WINAPI WSOCK32_getpeername(SOCKET s, struct sockaddr *name,
                                   INT *namelen)
{
    LPWSINFO                 pwsi = WINSOCK_GetIData();
#ifdef HAVE_IPX
    struct ws_sockaddr_ipx*  name2 = (struct ws_sockaddr_ipx *)name;
#endif

    TRACE("(%08x): socket: %04x, ptr %8x, ptr %8x\n", 
			   (unsigned)pwsi, s, (int) name, *namelen);
    if( _check_ws(pwsi, s) )
    {
	int fd = _get_sock_fd(s);
	if (getpeername(fd, name, namelen) == 0) {
#ifdef HAVE_IPX
	    if (((struct ws_sockaddr_ipx *)name)->sipx_family == AF_IPX) {
		name = (struct sockaddr *)
				malloc(namelen ? *namelen : sizeof(*name2));
		memcpy(name, name2, namelen ? *namelen : sizeof(*name2));
		name2->sipx_family = WS_AF_IPX;
		name2->sipx_network = ((struct sockaddr_ipx *)name)->sipx_network;
		name2->sipx_port = ((struct sockaddr_ipx *)name)->sipx_port;
		memcpy(name2->sipx_node,
			((struct sockaddr_ipx *)name)->sipx_node, IPX_NODE_LEN);
		free(name);
	    }
#endif
	    close(fd);
	    return 0; 
	}
	SetLastError(wsaErrno());
	close(fd);
    }
    return SOCKET_ERROR;
}

/***********************************************************************
 *              getpeername()		(WINSOCK.5)
 */
INT16 WINAPI WINSOCK_getpeername16(SOCKET16 s, struct sockaddr *name,
                                   INT16 *namelen16)
{
    INT namelen32 = *namelen16;
    INT retVal = WSOCK32_getpeername( s, name, &namelen32 );

#if DEBUG_SOCKADDR
    dump_sockaddr(name);
#endif

   *namelen16 = namelen32;
    return (INT16)retVal;
}

/***********************************************************************
 *		getsockname()		(WSOCK32.6)
 */
INT WINAPI WSOCK32_getsockname(SOCKET s, struct sockaddr *name,
                                   INT *namelen)
{
    LPWSINFO                 pwsi = WINSOCK_GetIData();
#ifdef HAVE_IPX
    struct ws_sockaddr_ipx*  name2 = (struct ws_sockaddr_ipx *)name;
#endif

    TRACE("(%08x): socket: %04x, ptr %8x, ptr %8x\n", 
			  (unsigned)pwsi, s, (int) name, (int) *namelen);
    if( _check_ws(pwsi, s) )
    {
	int fd = _get_sock_fd(s);
	if (getsockname(fd, name, namelen) == 0) {
#ifdef HAVE_IPX
	    if (((struct sockaddr_ipx *)name)->sipx_family == AF_IPX) {
		name = (struct sockaddr *)
				malloc(namelen ? *namelen : sizeof(*name2));
		memcpy(name, name2, namelen ? *namelen : sizeof(*name2));
		name2->sipx_family = WS_AF_IPX;
		name2->sipx_network = ((struct sockaddr_ipx *)name)->sipx_network;
		name2->sipx_port = ((struct sockaddr_ipx *)name)->sipx_port;
		memcpy(name2->sipx_node,
			((struct sockaddr_ipx *)name)->sipx_node, IPX_NODE_LEN);
		free(name);
	    }
#endif
	    close(fd);
	    return 0; 
	}
	SetLastError(wsaErrno());
	close(fd);
    }
    return SOCKET_ERROR;
}

/***********************************************************************
 *              getsockname()		(WINSOCK.6)
 */
INT16 WINAPI WINSOCK_getsockname16(SOCKET16 s, struct sockaddr *name,
                                   INT16 *namelen16)
{
    INT retVal;

    if( namelen16 )
    {
        INT namelen32 = *namelen16;
        retVal = WSOCK32_getsockname( s, name, &namelen32 );
       *namelen16 = namelen32;

#if DEBUG_SOCKADDR
    dump_sockaddr(name);
#endif

    }
    else retVal = SOCKET_ERROR;
    return (INT16)retVal;
}


/***********************************************************************
 *		getsockopt()		(WSOCK32.7)
 */
INT WINAPI WSOCK32_getsockopt(SOCKET s, INT level, 
                                  INT optname, char *optval, INT *optlen)
{
    LPWSINFO      pwsi = WINSOCK_GetIData();

    TRACE("(%08x): socket: %04x, opt 0x%x, ptr %8x, len %d\n", 
			   (unsigned)pwsi, s, level, (int) optval, (int) *optlen);
    if( _check_ws(pwsi, s) )
    {
	int fd = _get_sock_fd(s);
	if (!convert_sockopt(&level, &optname)) {
	    SetLastError(WSAENOPROTOOPT);	/* Unknown option */
        } else {
	    if (getsockopt(fd, (int) level, optname, optval, optlen) == 0 )
	    {
		close(fd);
		return 0;
	    }
	    SetLastError((errno == EBADF) ? WSAENOTSOCK : wsaErrno());
	}
	close(fd);
    }
    return SOCKET_ERROR;
}

/***********************************************************************
 *              getsockopt()		(WINSOCK.7)
 */
INT16 WINAPI WINSOCK_getsockopt16(SOCKET16 s, INT16 level,
                                  INT16 optname, char *optval, INT16 *optlen)
{
    INT optlen32;
    INT *p = &optlen32;
    INT retVal;
    if( optlen ) optlen32 = *optlen; else p = NULL;
    retVal = WSOCK32_getsockopt( s, (UINT16)level, optname, optval, p );
    if( optlen ) *optlen = optlen32;
    return (INT16)retVal;
}

/***********************************************************************
 *		htonl()			(WINSOCK.8)(WSOCK32.8)
 */
u_long WINAPI WINSOCK_htonl(u_long hostlong)   { return( htonl(hostlong) ); }
/***********************************************************************
 *		htons()			(WINSOCK.9)(WSOCK32.9)
 */
u_short WINAPI WINSOCK_htons(u_short hostshort) { return( htons(hostshort) ); }
/***********************************************************************
 *		inet_addr()		(WINSOCK.10)(WSOCK32.10)
 */
u_long WINAPI WINSOCK_inet_addr(char *cp)      { return( inet_addr(cp) ); }
/***********************************************************************
 *		ntohl()			(WINSOCK.14)(WSOCK32.14)
 */
u_long WINAPI WINSOCK_ntohl(u_long netlong)    { return( ntohl(netlong) ); }
/***********************************************************************
 *		ntohs()			(WINSOCK.15)(WSOCK32.15)
 */
u_short WINAPI WINSOCK_ntohs(u_short netshort)  { return( ntohs(netshort) ); }

/***********************************************************************
 *		inet_ntoa()		(WINSOCK.11)(WSOCK32.11)
 */
char* WINAPI WSOCK32_inet_ntoa(struct in_addr in)
{
  /* use "buffer for dummies" here because some applications have 
   * propensity to decode addresses in ws_hostent structure without 
   * saving them first...
   */

    LPWSINFO      pwsi = WINSOCK_GetIData();

    if( pwsi )
    {
	char*	s = inet_ntoa(in);
	if( s ) 
	{
            if( pwsi->dbuffer == NULL ) {
                /* Yes, 16: 4*3 digits + 3 '.' + 1 '\0' */
		if((pwsi->dbuffer = (char*) SEGPTR_ALLOC(16)) == NULL )
		{
		    SetLastError(WSAENOBUFS);
		    return NULL;
		}
            }
	    strcpy(pwsi->dbuffer, s);
	    return pwsi->dbuffer; 
	}
	SetLastError(wsaErrno());
    }
    return NULL;
}

SEGPTR WINAPI WINSOCK_inet_ntoa16(struct in_addr in)
{
  char* retVal = WSOCK32_inet_ntoa(in);
  return retVal ? SEGPTR_GET(retVal) : (SEGPTR)NULL;
}


/**********************************************************************
 *              WSAIoctl                (WS2_32)
 *
 *
 *   FIXME:  Only SIO_GET_INTERFACE_LIST option implemented.
 */
INT WINAPI WSAIoctl (SOCKET s,
                     DWORD   dwIoControlCode,
                     LPVOID  lpvInBuffer,
                     DWORD   cbInBuffer,
                     LPVOID  lpbOutBuffer,
                     DWORD   cbOutBuffer,
                     LPDWORD lpcbBytesReturned,
                     LPWSAOVERLAPPED lpOverlapped,
                     LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine)
{
   LPWSINFO pwsi = WINSOCK_GetIData();

   if( _check_ws(pwsi, s) )
   {
      int fd = _get_sock_fd(s);

      switch( dwIoControlCode )
      {
         case SIO_GET_INTERFACE_LIST:
         {
            INTERFACE_INFO* intArray = (INTERFACE_INFO*)lpbOutBuffer;
            int i, numInt;
            struct ifreq ifInfo;
            char ifName[512];
            

            TRACE ("-> SIO_GET_INTERFACE_LIST request\n");
	    
            numInt = WSAIOCTL_GetInterfaceCount(); 
            if (numInt < 0)
            {
               ERR ("Unable to open /proc filesystem to determine number of network interfaces!\n");
               close(fd);
               WSASetLastError(WSAEINVAL);
               return (SOCKET_ERROR);
            }
            
            for (i=0; i<numInt; i++)
            {
               if (!WSAIOCTL_GetInterfaceName(i, ifName))
               {
                  ERR ("Error parsing /proc filesystem!\n");
                  close(fd);
                  WSASetLastError(WSAEINVAL);
                  return (SOCKET_ERROR);
               }
               
               ifInfo.ifr_addr.sa_family = AF_INET; 
            
               /* IP Address */
               strcpy (ifInfo.ifr_name, ifName);
               if (ioctl(fd, SIOCGIFADDR, &ifInfo) < 0) 
               {
                  ERR ("Error obtaining IP address\n");
                  close(fd);
                  WSASetLastError(WSAEINVAL);
                  return (SOCKET_ERROR);
               }
               else
               {
                  struct ws_sockaddr_in *ipTemp = (struct ws_sockaddr_in *)&ifInfo.ifr_addr;
               
                  intArray->iiAddress.AddressIn.sin_family = AF_INET;
                  intArray->iiAddress.AddressIn.sin_port = ipTemp->sin_port;
                  intArray->iiAddress.AddressIn.sin_addr.ws_addr = ipTemp->sin_addr.S_un.S_addr;
               }
               
               /* Broadcast Address */
               strcpy (ifInfo.ifr_name, ifName);
               if (ioctl(fd, SIOCGIFBRDADDR, &ifInfo) < 0)
               {
                  ERR ("Error obtaining Broadcast IP address\n");
                  close(fd);
                  WSASetLastError(WSAEINVAL);
                  return (SOCKET_ERROR);
               }
               else
               {
                  struct ws_sockaddr_in *ipTemp = (struct ws_sockaddr_in *)&ifInfo.ifr_broadaddr;
               
                  intArray->iiBroadcastAddress.AddressIn.sin_family = AF_INET; 
                  intArray->iiBroadcastAddress.AddressIn.sin_port = ipTemp->sin_port;
                  intArray->iiBroadcastAddress.AddressIn.sin_addr.ws_addr = ipTemp->sin_addr.S_un.S_addr; 
               }

               /* Subnet Mask */
               strcpy (ifInfo.ifr_name, ifName);
               if (ioctl(fd, SIOCGIFNETMASK, &ifInfo) < 0)
               {
                  ERR ("Error obtaining Subnet IP address\n");
                  close(fd);
                  WSASetLastError(WSAEINVAL);
                  return (SOCKET_ERROR);
               }
               else
               {
                  /* Trying to avoid some compile problems across platforms.
                     (Linux, FreeBSD, Solaris...) */
                  #ifndef ifr_netmask
                    #ifndef ifr_addr
                       intArray->iiNetmask.AddressIn.sin_family = AF_INET; 
                       intArray->iiNetmask.AddressIn.sin_port = 0;
                       intArray->iiNetmask.AddressIn.sin_addr.ws_addr = 0; 
                       ERR ("Unable to determine Netmask on your platform!\n");
                    #else
                       struct ws_sockaddr_in *ipTemp = (struct ws_sockaddr_in *)&ifInfo.ifr_addr;
            
                       intArray->iiNetmask.AddressIn.sin_family = AF_INET; 
                       intArray->iiNetmask.AddressIn.sin_port = ipTemp->sin_port;
                       intArray->iiNetmask.AddressIn.sin_addr.ws_addr = ipTemp->sin_addr.S_un.S_addr; 
                    #endif
                  #else
                     struct ws_sockaddr_in *ipTemp = (struct ws_sockaddr_in *)&ifInfo.ifr_netmask;
            
                     intArray->iiNetmask.AddressIn.sin_family = AF_INET; 
                     intArray->iiNetmask.AddressIn.sin_port = ipTemp->sin_port;
                     intArray->iiNetmask.AddressIn.sin_addr.ws_addr = ipTemp->sin_addr.S_un.S_addr; 
                  #endif
               }
               
               /* Socket Status Flags */
               strcpy(ifInfo.ifr_name, ifName);
               if (ioctl(fd, SIOCGIFFLAGS, &ifInfo) < 0) 
               {
                  ERR ("Error obtaining status flags for socket!\n");
                  close(fd);
                  WSASetLastError(WSAEINVAL);
                  return (SOCKET_ERROR);
               }
               else
               {
                  /* FIXME - Is this the right flag to use? */
                  intArray->iiFlags = ifInfo.ifr_flags;
               }
               intArray++; /* Prepare for another interface */
            }
            
            /* Calculate the size of the array being returned */
            *lpcbBytesReturned = sizeof(INTERFACE_INFO) * numInt;
            break;
         }

         default:
         {
            WARN("\tunsupported WS_IOCTL cmd (%08lx)\n", dwIoControlCode);
            close(fd);
            WSASetLastError(WSAEOPNOTSUPP);
            return (SOCKET_ERROR);
         }
      }

      /* Function executed with no errors */
      close(fd);
      return (0); 
   }
   else
   {
      WSASetLastError(WSAENOTSOCK);
      return (SOCKET_ERROR);
   }
}


/* 
  Helper function for WSAIoctl - Get count of the number of interfaces
  by parsing /proc filesystem.
*/
int WSAIOCTL_GetInterfaceCount(void)
{
   FILE *procfs;
   char buf[512];  /* Size doesn't matter, something big */
   int  intcnt=0;
 
 
   /* Open /proc filesystem file for network devices */ 
   procfs = fopen(PROCFS_NETDEV_FILE, "r");
   if (!procfs) 
   {
      /* If we can't open the file, return an error */
      return (-1);
   }
   
   /* Omit first two lines, they are only headers */
   fgets(buf, sizeof buf, procfs);	
   fgets(buf, sizeof buf, procfs);

   while (fgets(buf, sizeof buf, procfs)) 
   {
      /* Each line in the file represents a network interface */
      intcnt++;
   }

   fclose(procfs);
   return(intcnt);
}


/*
   Helper function for WSAIoctl - Get name of device from interface number
   by parsing /proc filesystem.
*/
int WSAIOCTL_GetInterfaceName(int intNumber, char *intName)
{
   FILE *procfs;
   char buf[512]; /* Size doesn't matter, something big */
   int  i;

   /* Open /proc filesystem file for network devices */ 
   procfs = fopen(PROCFS_NETDEV_FILE, "r");
   if (!procfs) 
   {
      /* If we can't open the file, return an error */
      return (-1);
   }
   
   /* Omit first two lines, they are only headers */
   fgets(buf, sizeof(buf), procfs);	
   fgets(buf, sizeof(buf), procfs);

   for (i=0; i<intNumber; i++)
   {
      /* Skip the lines that don't interest us. */
      fgets(buf, sizeof(buf), procfs);
   }
   fgets(buf, sizeof(buf), procfs); /* This is the line we want */

   
   /* Parse out the line, grabbing only the name of the device
      to the intName variable 
      
      The Line comes in like this: (we only care about the device name)
      lo:   21970 377 0 0 0 0 0 0 21970 377 0 0 0 0 0 0
   */
   i=0; 
   while (isspace(buf[i])) /* Skip initial space(s) */
   {
      i++;
   }

   while (buf[i]) 
   {
      if (isspace(buf[i]))
      {
         break;
      }
      
      if (buf[i] == ':')  /* FIXME: Not sure if this block (alias detection) works properly */
      {
         /* This interface could be an alias... */
         int hold = i;
         char *dotname = intName;
         *intName++ = buf[i++];
         
         while (isdigit(buf[i]))
         {
            *intName++ = buf[i++];
         }
         
         if (buf[i] != ':') 
         {
            /* ... It wasn't, so back up */
            i = hold;
            intName = dotname;
         }
 
         if (buf[i] == '\0')
         {
            fclose(procfs);
            return(FALSE);
         }
         
         i++;
         break;
      }
      
      *intName++ = buf[i++];
   }
   *intName++ = '\0';

   fclose(procfs);
   return(TRUE);
 }


/***********************************************************************
 *		ioctlsocket()		(WSOCK32.12)
 */
INT WINAPI WSOCK32_ioctlsocket(SOCKET s, LONG cmd, ULONG *argp)
{
  LPWSINFO      pwsi = WINSOCK_GetIData();

  TRACE("(%08x): socket %04x, cmd %08lx, ptr %8x\n", 
			  (unsigned)pwsi, s, cmd, (unsigned) argp);
  if( _check_ws(pwsi, s) )
  {
    int		fd = _get_sock_fd(s);
    long 	newcmd  = cmd;

    switch( cmd )
    {
	case WS_FIONREAD:   
		newcmd=FIONREAD; 
		break;

	case WS_FIONBIO:    
		newcmd=FIONBIO;  
		if( _get_sock_mask(s) )
		{
		    /* AsyncSelect()'ed sockets are always nonblocking */
		    if (*argp) {
			close(fd);
			return 0;
		    }
		    SetLastError(WSAEINVAL); 
		    close(fd);
		    return SOCKET_ERROR; 
		}
		close(fd);
		if (*argp)
		    _enable_event(s, 0, WS_FD_NONBLOCKING, 0);
		else
		    _enable_event(s, 0, 0, WS_FD_NONBLOCKING);
		return 0;

	case WS_SIOCATMARK: 
		newcmd=SIOCATMARK; 
		break;

	case WS_IOW('f',125,u_long): 
		WARN("Warning: WS1.1 shouldn't be using async I/O\n");
		SetLastError(WSAEINVAL); 
		return SOCKET_ERROR;

	default:	  
		/* Netscape tries hard to use bogus ioctl 0x667e */
		WARN("\tunknown WS_IOCTL cmd (%08lx)\n", cmd);
    }
    if( ioctl(fd, newcmd, (char*)argp ) == 0 )
    {
	close(fd);
	return 0;
    }
    SetLastError((errno == EBADF) ? WSAENOTSOCK : wsaErrno()); 
    close(fd);
  }
  return SOCKET_ERROR;
}

/***********************************************************************
 *              ioctlsocket()           (WINSOCK.12)
 */
INT16 WINAPI WINSOCK_ioctlsocket16(SOCKET16 s, LONG cmd, ULONG *argp)
{
    return (INT16)WSOCK32_ioctlsocket( s, cmd, argp );
}


/***********************************************************************
 *		listen()		(WSOCK32.13)
 */
INT WINAPI WSOCK32_listen(SOCKET s, INT backlog)
{
    LPWSINFO      pwsi = WINSOCK_GetIData();

    TRACE("(%08x): socket %04x, backlog %d\n", 
			    (unsigned)pwsi, s, backlog);
    if( _check_ws(pwsi, s) )
    {
	int fd = _get_sock_fd(s);
	if (listen(fd, backlog) == 0)
	{
	    close(fd);
	    _enable_event(s, FD_ACCEPT,
			  WS_FD_LISTENING,
			  WS_FD_CONNECT|WS_FD_CONNECTED);
	    return 0;
	}
	SetLastError(wsaErrno());
    }
    else SetLastError(WSAENOTSOCK);
    return SOCKET_ERROR;
}

/***********************************************************************
 *              listen()		(WINSOCK.13)
 */
INT16 WINAPI WINSOCK_listen16(SOCKET16 s, INT16 backlog)
{
    return (INT16)WSOCK32_listen( s, backlog );
}


/***********************************************************************
 *		recv()			(WSOCK32.16)
 */
INT WINAPI WSOCK32_recv(SOCKET s, char *buf, INT len, INT flags)
{
    LPWSINFO      pwsi = WINSOCK_GetIData();

    TRACE("(%08x): socket %04x, buf %8x, len %d, "
		    "flags %d\n", (unsigned)pwsi, s, (unsigned)buf, 
		    len, flags);
    if( _check_ws(pwsi, s) )
    {
	int fd = _get_sock_fd(s);
	INT length;

	if (_is_blocking(s))
	{
	    /* block here */
	    /* FIXME: OOB and exceptfds? */
	    do_block(fd, 1);
	}
	if ((length = recv(fd, buf, len, flags)) >= 0) 
	{ 
	    TRACE(" -> %i bytes\n", length);

	    close(fd);
	    _enable_event(s, FD_READ, 0, 0);
	    return length;
	}
	SetLastError(wsaErrno());
	close(fd);
    }
    else SetLastError(WSAENOTSOCK);
    WARN(" -> ERROR\n");
    return SOCKET_ERROR;
}

/***********************************************************************
 *              recv()			(WINSOCK.16)
 */
INT16 WINAPI WINSOCK_recv16(SOCKET16 s, char *buf, INT16 len, INT16 flags)
{
    return (INT16)WSOCK32_recv( s, buf, len, flags );
}


/***********************************************************************
 *		recvfrom()		(WSOCK32.17)
 */
INT WINAPI WSOCK32_recvfrom(SOCKET s, char *buf, INT len, INT flags, 
                                struct sockaddr *from, INT *fromlen32)
{
    LPWSINFO                 pwsi = WINSOCK_GetIData();
#ifdef HAVE_IPX
    struct ws_sockaddr_ipx*  from2 = (struct ws_sockaddr_ipx *)from;
#endif

    TRACE("(%08x): socket %04x, ptr %08x, "
		    "len %d, flags %d\n", (unsigned)pwsi, s, (unsigned)buf,
		    len, flags);
#if DEBUG_SOCKADDR
    if( from ) dump_sockaddr(from);
    else DPRINTF("from = NULL\n");
#endif

    if( _check_ws(pwsi, s) )
    {
	int fd = _get_sock_fd(s);
	int length;

	if (_is_blocking(s))
	{
	    /* block here */
	    /* FIXME: OOB and exceptfds */
	    do_block(fd, 1);
	}
	if ((length = recvfrom(fd, buf, len, flags, from, fromlen32)) >= 0)
	{
	    TRACE(" -> %i bytes\n", length);

#ifdef HAVE_IPX
	if (from && ((struct sockaddr_ipx *)from)->sipx_family == AF_IPX) {
	    from = (struct sockaddr *)
				malloc(fromlen32 ? *fromlen32 : sizeof(*from2));
	    memcpy(from, from2, fromlen32 ? *fromlen32 : sizeof(*from2));
	    from2->sipx_family = WS_AF_IPX;
	    from2->sipx_network = ((struct sockaddr_ipx *)from)->sipx_network;
	    from2->sipx_port = ((struct sockaddr_ipx *)from)->sipx_port;
	    memcpy(from2->sipx_node,
			((struct sockaddr_ipx *)from)->sipx_node, IPX_NODE_LEN);
	    free(from);
	}
#endif
	    close(fd);
	    _enable_event(s, FD_READ, 0, 0);
	    return (INT16)length;
	}
	SetLastError(wsaErrno());
	close(fd);
    }
    else SetLastError(WSAENOTSOCK);
    WARN(" -> ERROR\n");
#ifdef HAVE_IPX
    if (from && ((struct sockaddr_ipx *)from)->sipx_family == AF_IPX) {
	from = (struct sockaddr *)
				malloc(fromlen32 ? *fromlen32 : sizeof(*from2));
	memcpy(from, from2, fromlen32 ? *fromlen32 : sizeof(*from2));
	from2->sipx_family = WS_AF_IPX;
	from2->sipx_network = ((struct sockaddr_ipx *)from)->sipx_network;
	from2->sipx_port = ((struct sockaddr_ipx *)from)->sipx_port;
	memcpy(from2->sipx_node,
		((struct sockaddr_ipx *)from)->sipx_node, IPX_NODE_LEN);
	free(from);
    }
#endif
    return SOCKET_ERROR;
}

/***********************************************************************
 *              recvfrom()		(WINSOCK.17)
 */
INT16 WINAPI WINSOCK_recvfrom16(SOCKET16 s, char *buf, INT16 len, INT16 flags,
                                struct sockaddr *from, INT16 *fromlen16)
{
    INT fromlen32;
    INT *p = &fromlen32;
    INT retVal;

    if( fromlen16 ) fromlen32 = *fromlen16; else p = NULL;
    retVal = WSOCK32_recvfrom( s, buf, len, flags, from, p );
    if( fromlen16 ) *fromlen16 = fromlen32;
    return (INT16)retVal;
}

/***********************************************************************
 *		select()		(WINSOCK.18)(WSOCK32.18)
 */
static INT __ws_select( BOOL b32, void *ws_readfds, void *ws_writefds, void *ws_exceptfds,
			  struct timeval *timeout )
{
    LPWSINFO      pwsi = WINSOCK_GetIData();
	
    TRACE("(%08x): read %8x, write %8x, excp %8x\n", 
    (unsigned) pwsi, (unsigned) ws_readfds, (unsigned) ws_writefds, (unsigned) ws_exceptfds);

    if( pwsi )
    {
	int         highfd = 0;
	fd_set      readfds, writefds, exceptfds;
	fd_set     *p_read, *p_write, *p_except;
	int         readfd[FD_SETSIZE], writefd[FD_SETSIZE], exceptfd[FD_SETSIZE];

	p_read = fd_set_import(&readfds, pwsi, ws_readfds, &highfd, readfd, b32);
	p_write = fd_set_import(&writefds, pwsi, ws_writefds, &highfd, writefd, b32);
	p_except = fd_set_import(&exceptfds, pwsi, ws_exceptfds, &highfd, exceptfd, b32);

	if( (highfd = select(highfd + 1, p_read, p_write, p_except, timeout)) > 0 )
	{
	    fd_set_export(pwsi, &readfds, p_except, ws_readfds, readfd, b32);
	    fd_set_export(pwsi, &writefds, p_except, ws_writefds, writefd, b32);

	    if (p_except && ws_exceptfds)
	    {
#define wsfds16 ((ws_fd_set16*)ws_exceptfds)
#define wsfds32 ((ws_fd_set32*)ws_exceptfds)
		int i, j, count = (b32) ? wsfds32->fd_count : wsfds16->fd_count;

		for (i = j = 0; i < count; i++)
		{
		    int fd = exceptfd[i];
		    if( fd >= 0 && FD_ISSET(fd, &exceptfds) )
		    {
			if( b32 )
				wsfds32->fd_array[j++] = wsfds32->fd_array[i];
			else
				wsfds16->fd_array[j++] = wsfds16->fd_array[i];
		    }
		    if( fd >= 0 ) close(fd);
		    exceptfd[i] = -1;
		}
		if( b32 )
		    wsfds32->fd_count = j;
		else
		    wsfds16->fd_count = j;
#undef wsfds32
#undef wsfds16
	    }
	    return highfd; 
	}
	fd_set_unimport(ws_readfds, readfd, b32);
	fd_set_unimport(ws_writefds, writefd, b32);
	fd_set_unimport(ws_exceptfds, exceptfd, b32);
	if( ws_readfds ) ((ws_fd_set32*)ws_readfds)->fd_count = 0;
	if( ws_writefds ) ((ws_fd_set32*)ws_writefds)->fd_count = 0;
	if( ws_exceptfds ) ((ws_fd_set32*)ws_exceptfds)->fd_count = 0;

        if( highfd == 0 ) return 0;
	SetLastError(wsaErrno());
    } 
    return SOCKET_ERROR;
}

INT16 WINAPI WINSOCK_select16(INT16 nfds, ws_fd_set16 *ws_readfds,
                              ws_fd_set16 *ws_writefds, ws_fd_set16 *ws_exceptfds,
                              struct timeval *timeout)
{
    return (INT16)__ws_select( FALSE, ws_readfds, ws_writefds, ws_exceptfds, timeout );
}

INT WINAPI WSOCK32_select(INT nfds, ws_fd_set32 *ws_readfds,
                              ws_fd_set32 *ws_writefds, ws_fd_set32 *ws_exceptfds,
                              struct timeval *timeout)
{
    /* struct timeval is the same for both 32- and 16-bit code */
    return (INT)__ws_select( TRUE, ws_readfds, ws_writefds, ws_exceptfds, timeout );
}


/***********************************************************************
 *		send()			(WSOCK32.19)
 */
INT WINAPI WSOCK32_send(SOCKET s, char *buf, INT len, INT flags)
{
    LPWSINFO      pwsi = WINSOCK_GetIData();

    TRACE("(%08x): socket %04x, ptr %08x, length %d, flags %d\n", 
			   (unsigned)pwsi, s, (unsigned) buf, len, flags);
    if( _check_ws(pwsi, s) )
    {
	int	fd = _get_sock_fd(s);
	int	length;

	if (_is_blocking(s))
	{
	    /* block here */
	    /* FIXME: exceptfds */
	    do_block(fd, 2);
	}
	if ((length = send(fd, buf, len, flags)) < 0 ) 
	{
	    SetLastError(wsaErrno());
	    if( GetLastError() == WSAEWOULDBLOCK )
		_enable_event(s, FD_WRITE, 0, 0);
	}
	else
	{
	    close(fd);
	    return (INT16)length;
	}
	close(fd);
    }
    else SetLastError(WSAENOTSOCK);
    return SOCKET_ERROR;
}

/***********************************************************************
 *              send()			(WINSOCK.19)
 */
INT16 WINAPI WINSOCK_send16(SOCKET16 s, char *buf, INT16 len, INT16 flags)
{
    return WSOCK32_send( s, buf, len, flags );
}

/***********************************************************************
 *		sendto()		(WSOCK32.20)
 */
INT WINAPI WSOCK32_sendto(SOCKET s, char *buf, INT len, INT flags,
                              struct sockaddr *to, INT tolen)
{
    LPWSINFO                 pwsi = WINSOCK_GetIData();
#ifdef HAVE_IPX
    struct ws_sockaddr_ipx*  to2 = (struct ws_sockaddr_ipx *)to;
#endif

    TRACE("(%08x): socket %04x, ptr %08x, length %d, flags %d\n",
                          (unsigned)pwsi, s, (unsigned) buf, len, flags);
    if( _check_ws(pwsi, s) )
    {
	int	fd = _get_sock_fd(s);
	INT	length;

	if (to && ((struct ws_sockaddr_ipx *)to)->sipx_family == WS_AF_PUP)
	    ((struct ws_sockaddr_ipx *)to)->sipx_family = AF_UNSPEC;
#ifdef HAVE_IPX
	else if (to &&
		((struct ws_sockaddr_ipx *)to)->sipx_family == WS_AF_IPX)
	{
	    to = (struct sockaddr *) malloc(sizeof(struct sockaddr_ipx));
	    memset(to, '\0', sizeof(struct sockaddr_ipx));
	    ((struct sockaddr_ipx *)to)->sipx_family = AF_IPX;
	    ((struct sockaddr_ipx *)to)->sipx_port = to2->sipx_port;
	    ((struct sockaddr_ipx *)to)->sipx_network = to2->sipx_network;
	    memcpy(((struct sockaddr_ipx *)to)->sipx_node,
			to2->sipx_node, IPX_NODE_LEN);
	    tolen = sizeof(struct sockaddr_ipx);
	}
#endif
	if (_is_blocking(s))
	{
	    /* block here */
	    /* FIXME: exceptfds */
	    do_block(fd, 2);
	}
	if ((length = sendto(fd, buf, len, flags, to, tolen)) < 0 )
	{
	    SetLastError(wsaErrno());
	    if( GetLastError() == WSAEWOULDBLOCK )
		_enable_event(s, FD_WRITE, 0, 0);
	} 
	else {
#ifdef HAVE_IPX
	    if (to && ((struct sockaddr_ipx *)to)->sipx_family == AF_IPX) {
		free(to);
	    }
#endif
	    close(fd);
	    return length;
	}
	close(fd);
    }
    else SetLastError(WSAENOTSOCK);
#ifdef HAVE_IPX
    if (to && ((struct sockaddr_ipx *)to)->sipx_family == AF_IPX) {
	free(to);
    }
#endif
    return SOCKET_ERROR;
}

/***********************************************************************
 *              sendto()		(WINSOCK.20)
 */
INT16 WINAPI WINSOCK_sendto16(SOCKET16 s, char *buf, INT16 len, INT16 flags,
                              struct sockaddr *to, INT16 tolen)
{
    return (INT16)WSOCK32_sendto( s, buf, len, flags, to, tolen );
}

/***********************************************************************
 *		setsockopt()		(WSOCK32.21)
 */
INT WINAPI WSOCK32_setsockopt(SOCKET16 s, INT level, INT optname, 
                                  char *optval, INT optlen)
{
    LPWSINFO      pwsi = WINSOCK_GetIData();

    TRACE("(%08x): socket %04x, lev %d, opt 0x%x, ptr %08x, len %d\n",
			  (unsigned)pwsi, s, level, optname, (int) optval, optlen);
    if( _check_ws(pwsi, s) )
    {
	struct	linger linger;
	int fd = _get_sock_fd(s);
        int woptval;

        if(optname == WS_SO_DONTLINGER && level == WS_SOL_SOCKET) {
	    /* This is unique to WinSock and takes special conversion */
            linger.l_onoff	= *((int*)optval) ? 0: 1;
            linger.l_linger	= 0;
            optname=SO_LINGER;
            optval = (char*)&linger;
            optlen = sizeof(struct linger);
            level = SOL_SOCKET;
        }else{
            if (!convert_sockopt(&level, &optname)) {
		SetLastError(WSAENOPROTOOPT);
		close(fd);
		return SOCKET_ERROR;
	    }
            if (optname == SO_LINGER && optval) {
                /* yes, uses unsigned short in both win16/win32 */
                linger.l_onoff	= ((UINT16*)optval)[0];
                linger.l_linger	= ((UINT16*)optval)[1];
                /* FIXME: what is documented behavior if SO_LINGER optval
                   is null?? */
                optval = (char*)&linger;
                optlen = sizeof(struct linger);
            } else if (optlen < sizeof(int)){
                woptval= *((INT16 *) optval);
                optval= (char*) &woptval;
                optlen=sizeof(int);
            }
	}
        if(optname == SO_RCVBUF && *(int*)optval < 2048) {
            WARN("SO_RCVBF for %d bytes is too small: ignored\n", *(int*)optval );
            close( fd);
            return 0;
        }

	if (setsockopt(fd, level, optname, optval, optlen) == 0)
	{
	    close(fd);
	    return 0;
	}
	SetLastError(wsaErrno());
	close(fd);
    }
    else SetLastError(WSAENOTSOCK);
    return SOCKET_ERROR;
}

/***********************************************************************
 *              setsockopt()		(WINSOCK.21)
 */
INT16 WINAPI WINSOCK_setsockopt16(SOCKET16 s, INT16 level, INT16 optname,
                                  char *optval, INT16 optlen)
{
    if( !optval ) return SOCKET_ERROR;
    return (INT16)WSOCK32_setsockopt( s, (UINT16)level, optname, optval, optlen );
}


/***********************************************************************
 *		shutdown()		(WSOCK32.22)
 */
INT WINAPI WSOCK32_shutdown(SOCKET s, INT how)
{
    LPWSINFO      pwsi = WINSOCK_GetIData();

    TRACE("(%08x): socket %04x, how %i\n",
			    (unsigned)pwsi, s, how );
    if( _check_ws(pwsi, s) )
    {
	int fd = _get_sock_fd(s);
	    switch( how )
	    {
		case 0: /* drop receives */
			_enable_event(s, 0, 0, WS_FD_READ);
#ifdef SHUT_RD
			how = SHUT_RD;
#endif
			break;

		case 1: /* drop sends */
			_enable_event(s, 0, 0, WS_FD_WRITE);
#ifdef SHUT_WR
			how = SHUT_WR;
#endif
			break;

		case 2: /* drop all */
#ifdef SHUT_RDWR
			how = SHUT_RDWR;
#endif
		default:
			WSAAsyncSelect( s, 0, 0, 0 );
			break;
	    }

	if (shutdown(fd, how) == 0) 
	{
	    if( how > 1 ) 
	    {
		_enable_event(s, 0, 0, WS_FD_CONNECTED|WS_FD_LISTENING);
	    }
	    close(fd);
	    return 0;
	}
	SetLastError(wsaErrno());
	close(fd);
    } 
    else SetLastError(WSAENOTSOCK);
    return SOCKET_ERROR;
}

/***********************************************************************
 *              shutdown()		(WINSOCK.22)
 */
INT16 WINAPI WINSOCK_shutdown16(SOCKET16 s, INT16 how)
{
    return (INT16)WSOCK32_shutdown( s, how );
}


/***********************************************************************
 *		socket()		(WSOCK32.23)
 */
SOCKET WINAPI WSOCK32_socket(INT af, INT type, INT protocol)
{
  LPWSINFO      pwsi = WINSOCK_GetIData();
  SOCKET ret;

  TRACE("(%08x): af=%d type=%d protocol=%d\n", 
			  (unsigned)pwsi, af, type, protocol);

  if( pwsi )
  {
    /* check the socket family */
    switch(af) 
    {
#ifdef HAVE_IPX
	case WS_AF_IPX:	af = AF_IPX;
#endif
	case AF_INET:
	case AF_UNSPEC: break;
	default:        SetLastError(WSAEAFNOSUPPORT); 
			return INVALID_SOCKET;
    }

    /* check the socket type */
    switch(type) 
    {
	case SOCK_STREAM:
	case SOCK_DGRAM:
	case SOCK_RAW:  break;
	default:        SetLastError(WSAESOCKTNOSUPPORT); 
			return INVALID_SOCKET;
    }

    /* check the protocol type */
    if ( protocol < 0 )  /* don't support negative values */
    { SetLastError(WSAEPROTONOSUPPORT); return INVALID_SOCKET; }

    if ( af == AF_UNSPEC)  /* did they not specify the address family? */
        switch(protocol) 
	{
          case IPPROTO_TCP:
             if (type == SOCK_STREAM) { af = AF_INET; break; }
          case IPPROTO_UDP:
             if (type == SOCK_DGRAM)  { af = AF_INET; break; }
          default: SetLastError(WSAEPROTOTYPE); return INVALID_SOCKET;
        }

    SERVER_START_REQ
    {
        struct create_socket_request *req = server_alloc_req( sizeof(*req), 0 );
        req->family   = af;
        req->type     = type;
        req->protocol = protocol;
        req->access   = GENERIC_READ|GENERIC_WRITE|SYNCHRONIZE;
        req->inherit  = TRUE;
        sock_server_call( REQ_CREATE_SOCKET );
        ret = req->handle;
    }
    SERVER_END_REQ;
    if ( ((int) ret) >= 0)
    {
        TRACE("\tcreated %04x\n", ret );
        return ret;
    }

    if (GetLastError() == WSAEACCES) /* raw socket denied */
    {
	if (type == SOCK_RAW)
	    MESSAGE("WARNING: Trying to create a socket of type SOCK_RAW, will fail unless running as root\n");
        else
            MESSAGE("WS_SOCKET: not enough privileges to create socket, try running as root\n");
        SetLastError(WSAESOCKTNOSUPPORT);
    }
  }
 
  WARN("\t\tfailed!\n");
  return INVALID_SOCKET;
}

/***********************************************************************
 *              socket()		(WINSOCK.23)
 */
SOCKET16 WINAPI WINSOCK_socket16(INT16 af, INT16 type, INT16 protocol)
{
    return (SOCKET16)WSOCK32_socket( af, type, protocol );
}
    

/* ----------------------------------- DNS services
 *
 * IMPORTANT: 16-bit API structures have SEGPTR pointers inside them.
 * Also, we have to use wsock32 stubs to convert structures and
 * error codes from Unix to WSA, hence there is no direct mapping in 
 * the relay32/wsock32.spec.
 */

static char*	NULL_STRING = "NULL";

/***********************************************************************
 *		gethostbyaddr()		(WINSOCK.51)(WSOCK32.51)
 */
static WIN_hostent* __ws_gethostbyaddr(const char *addr, int len, int type, int dup_flag)
{
    WIN_hostent *retval = NULL;
    LPWSINFO      	pwsi = WINSOCK_GetIData();

    if( pwsi )
    {
	struct hostent*	host;
#if HAVE_LINUX_GETHOSTBYNAME_R_6
        char *extrabuf;
        int ebufsize=1024;
        struct hostent hostentry;
        int locerr=ENOBUFS;
        host = NULL;
        extrabuf=HeapAlloc(GetProcessHeap(),0,ebufsize) ;
        while(extrabuf) { 
            int res = gethostbyaddr_r(addr, len, type, 
                    &hostentry, extrabuf, ebufsize, &host, &locerr);
            if( res != ERANGE) break;
            ebufsize *=2;
            extrabuf=HeapReAlloc(GetProcessHeap(),0,extrabuf,ebufsize) ;
        }
        if (!host) SetLastError((locerr < 0) ? wsaErrno() : wsaHerrno(locerr));
#else
        EnterCriticalSection( &csWSgetXXXbyYYY );
        host = gethostbyaddr(addr, len, type);
        if (!host) SetLastError((h_errno < 0) ? wsaErrno() : wsaHerrno(h_errno));
#endif
	if( host != NULL )
        {
	    if( WS_dup_he(pwsi, host, dup_flag) )
		retval = (WIN_hostent*)(pwsi->he);
	    else 
		SetLastError(WSAENOBUFS);
        }
#ifdef  HAVE_LINUX_GETHOSTBYNAME_R_6
        HeapFree(GetProcessHeap(),0,extrabuf);
#else
        LeaveCriticalSection( &csWSgetXXXbyYYY );
#endif
    }
    return retval;
}

SEGPTR WINAPI WINSOCK_gethostbyaddr16(const char *addr, INT16 len, INT16 type)
{
    WIN_hostent* retval;
    TRACE("ptr %08x, len %d, type %d\n",
                            (unsigned) addr, len, type);
    retval = __ws_gethostbyaddr( addr, len, type, WS_DUP_SEGPTR );
    return retval ? SEGPTR_GET(retval) : ((SEGPTR)NULL);
}

WIN_hostent* WINAPI WSOCK32_gethostbyaddr(const char *addr, INT len,
                                                INT type)
{
    TRACE("ptr %08x, len %d, type %d\n",
                             (unsigned) addr, len, type);
    return __ws_gethostbyaddr(addr, len, type, WS_DUP_LINEAR);
}

/***********************************************************************
 *		gethostbyname()		(WINSOCK.52)(WSOCK32.52)
 */
static WIN_hostent * __ws_gethostbyname(const char *name, int dup_flag)
{
    WIN_hostent *retval = NULL;
    LPWSINFO              pwsi = WINSOCK_GetIData();

    if( pwsi )
    {
	struct hostent*     host;
#ifdef  HAVE_LINUX_GETHOSTBYNAME_R_6
        char *extrabuf;
        int ebufsize=1024;
        struct hostent hostentry;
        int locerr = ENOBUFS;
        host = NULL;
        extrabuf=HeapAlloc(GetProcessHeap(),0,ebufsize) ;
        while(extrabuf) { 
            int res = gethostbyname_r(name, &hostentry, extrabuf, ebufsize, &host, &locerr);
            if( res != ERANGE) break;
            ebufsize *=2;
            extrabuf=HeapReAlloc(GetProcessHeap(),0,extrabuf,ebufsize) ;
        }
        if (!host) SetLastError((locerr < 0) ? wsaErrno() : wsaHerrno(locerr));
#else
        EnterCriticalSection( &csWSgetXXXbyYYY );
        host = gethostbyname(name);
        if (!host) SetLastError((h_errno < 0) ? wsaErrno() : wsaHerrno(h_errno));
#endif
	if( host  != NULL )
        {
	     if( WS_dup_he(pwsi, host, dup_flag) )
		 retval = (WIN_hostent*)(pwsi->he);
	     else SetLastError(WSAENOBUFS);
        }
#ifdef  HAVE_LINUX_GETHOSTBYNAME_R_6
        HeapFree(GetProcessHeap(),0,extrabuf);
#else
        LeaveCriticalSection( &csWSgetXXXbyYYY );
#endif
    }
    return retval;
}

SEGPTR WINAPI WINSOCK_gethostbyname16(const char *name)
{
    WIN_hostent* retval;
    TRACE("%s\n", (name)?name:NULL_STRING);
    retval = __ws_gethostbyname( name, WS_DUP_SEGPTR );
    return (retval)? SEGPTR_GET(retval) : ((SEGPTR)NULL) ;
}

WIN_hostent* WINAPI WSOCK32_gethostbyname(const char* name)
{
    TRACE("%s\n", (name)?name:NULL_STRING);
    return __ws_gethostbyname( name, WS_DUP_LINEAR );
}


/***********************************************************************
 *		getprotobyname()	(WINSOCK.53)(WSOCK32.53)
 */
static WIN_protoent* __ws_getprotobyname(const char *name, int dup_flag)
{
    WIN_protoent* retval = NULL;
    LPWSINFO              pwsi = WINSOCK_GetIData();

    if( pwsi )
    {
	struct protoent*     proto;
        EnterCriticalSection( &csWSgetXXXbyYYY );
	if( (proto = getprotobyname(name)) != NULL )
        {
	    if( WS_dup_pe(pwsi, proto, dup_flag) )
		retval = (WIN_protoent*)(pwsi->pe);
	    else SetLastError(WSAENOBUFS);
        }
        else {
            MESSAGE("protocol %s not found; You might want to add "
                    "this to /etc/protocols\n", debugstr_a(name) );
            SetLastError(WSANO_DATA);
        }
        LeaveCriticalSection( &csWSgetXXXbyYYY );
    } else SetLastError(WSANOTINITIALISED);
    return retval;
}

SEGPTR WINAPI WINSOCK_getprotobyname16(const char *name)
{
    WIN_protoent* retval;
    TRACE("%s\n", (name)?name:NULL_STRING);
    retval = __ws_getprotobyname(name, WS_DUP_SEGPTR);
    return retval ? SEGPTR_GET(retval) : ((SEGPTR)NULL);
}

WIN_protoent* WINAPI WSOCK32_getprotobyname(const char* name)
{
    TRACE("%s\n", (name)?name:NULL_STRING);
    return __ws_getprotobyname(name, WS_DUP_LINEAR);
}


/***********************************************************************
 *		getprotobynumber()	(WINSOCK.54)(WSOCK32.54)
 */
static WIN_protoent* __ws_getprotobynumber(int number, int dup_flag)
{
    WIN_protoent* retval = NULL;
    LPWSINFO              pwsi = WINSOCK_GetIData();

    if( pwsi )
    {
	struct protoent*     proto;
        EnterCriticalSection( &csWSgetXXXbyYYY );
	if( (proto = getprotobynumber(number)) != NULL )
        {
	    if( WS_dup_pe(pwsi, proto, dup_flag) )
		retval = (WIN_protoent*)(pwsi->pe);
	    else SetLastError(WSAENOBUFS);
        }
        else {
            MESSAGE("protocol number %d not found; You might want to add "
                    "this to /etc/protocols\n", number );
            SetLastError(WSANO_DATA);
        }
        LeaveCriticalSection( &csWSgetXXXbyYYY );
    } else SetLastError(WSANOTINITIALISED);
    return retval;
}

SEGPTR WINAPI WINSOCK_getprotobynumber16(INT16 number)
{
    WIN_protoent* retval;
    TRACE("%i\n", number);
    retval = __ws_getprotobynumber(number, WS_DUP_SEGPTR);
    return retval ? SEGPTR_GET(retval) : ((SEGPTR)NULL);
}

WIN_protoent* WINAPI WSOCK32_getprotobynumber(INT number)
{
    TRACE("%i\n", number);
    return __ws_getprotobynumber(number, WS_DUP_LINEAR);
}


/***********************************************************************
 *		getservbyname()		(WINSOCK.55)(WSOCK32.55)
 */
static WIN_servent* __ws_getservbyname(const char *name, const char *proto, int dup_flag)
{
    WIN_servent* retval = NULL;
    LPWSINFO              pwsi = WINSOCK_GetIData();

    if( pwsi )
    {
	struct servent*     serv;
	int i = wsi_strtolo( pwsi, name, proto );

	if( i ) {
            EnterCriticalSection( &csWSgetXXXbyYYY );
	    serv = getservbyname(pwsi->buffer,
				 proto ? (pwsi->buffer + i) : NULL);
	    if( serv != NULL )
            {
		if( WS_dup_se(pwsi, serv, dup_flag) )
		    retval = (WIN_servent*)(pwsi->se);
		else SetLastError(WSAENOBUFS);
            }
	    else {
                MESSAGE("service %s protocol %s not found; You might want to add "
                        "this to /etc/services\n", debugstr_a(pwsi->buffer),
                        proto ? debugstr_a(pwsi->buffer+i):"*"); 
                SetLastError(WSANO_DATA);
            }
            LeaveCriticalSection( &csWSgetXXXbyYYY );
	}
	else SetLastError(WSAENOBUFS);
    } else SetLastError(WSANOTINITIALISED);
    return retval;
}

SEGPTR WINAPI WINSOCK_getservbyname16(const char *name, const char *proto)
{
    WIN_servent* retval;
    TRACE("'%s', '%s'\n",
                            (name)?name:NULL_STRING, (proto)?proto:NULL_STRING);
    retval = __ws_getservbyname(name, proto, WS_DUP_SEGPTR);
    return retval ? SEGPTR_GET(retval) : ((SEGPTR)NULL);
}

WIN_servent* WINAPI WSOCK32_getservbyname(const char *name, const char *proto)
{
    TRACE("'%s', '%s'\n",
                            (name)?name:NULL_STRING, (proto)?proto:NULL_STRING);
    return __ws_getservbyname(name, proto, WS_DUP_LINEAR);
}


/***********************************************************************
 *		getservbyport()		(WINSOCK.56)(WSOCK32.56)
 */
static WIN_servent* __ws_getservbyport(int port, const char* proto, int dup_flag)
{
    WIN_servent* retval = NULL;
    LPWSINFO              pwsi = WINSOCK_GetIData();

    if( pwsi )
    {
	struct servent*     serv;
	if (!proto || wsi_strtolo( pwsi, proto, NULL )) {
            EnterCriticalSection( &csWSgetXXXbyYYY );
	    if( (serv = getservbyport(port, (proto) ? pwsi->buffer : NULL)) != NULL ) {
		if( WS_dup_se(pwsi, serv, dup_flag) )
		    retval = (WIN_servent*)(pwsi->se);
		else SetLastError(WSAENOBUFS);
	    }
	    else {
                MESSAGE("service on port %lu protocol %s not found; You might want to add "
                        "this to /etc/services\n", (unsigned long)ntohl(port),
                        proto ? debugstr_a(pwsi->buffer) : "*"); 
                SetLastError(WSANO_DATA);
            }
            LeaveCriticalSection( &csWSgetXXXbyYYY );
	}
	else SetLastError(WSAENOBUFS);
    } else SetLastError(WSANOTINITIALISED);
    return retval;
}

SEGPTR WINAPI WINSOCK_getservbyport16(INT16 port, const char *proto)
{
    WIN_servent* retval;
    TRACE("%d (i.e. port %d), '%s'\n",
                            (int)port, (int)ntohl(port), (proto)?proto:NULL_STRING);
    retval = __ws_getservbyport(port, proto, WS_DUP_SEGPTR);
    return retval ? SEGPTR_GET(retval) : ((SEGPTR)NULL);
}

WIN_servent* WINAPI WSOCK32_getservbyport(INT port, const char *proto)
{
    TRACE("%d (i.e. port %d), '%s'\n",
                            (int)port, (int)ntohl(port), (proto)?proto:NULL_STRING);
    return __ws_getservbyport(port, proto, WS_DUP_LINEAR);
}


/***********************************************************************
 *              gethostname()           (WSOCK32.57)
 */
INT WINAPI WSOCK32_gethostname(char *name, INT namelen)
{
    LPWSINFO              pwsi = WINSOCK_GetIData();

    TRACE("(%08x): name %s, len %d\n",
                          (unsigned)pwsi, (name)?name:NULL_STRING, namelen);
    if( pwsi )
    {
	if (gethostname(name, namelen) == 0) return 0;
	SetLastError((errno == EINVAL) ? WSAEFAULT : wsaErrno());
    }
    return SOCKET_ERROR;
}

/***********************************************************************
 *              gethostname()           (WINSOCK.57)
 */
INT16 WINAPI WINSOCK_gethostname16(char *name, INT16 namelen)
{
    return (INT16)WSOCK32_gethostname(name, namelen);
}


/* ------------------------------------- Windows sockets extensions -- *
 *								       *
 * ------------------------------------------------------------------- */

/***********************************************************************
 *		WSAEnumNetworkEvents
 */
int WINAPI WSAEnumNetworkEvents(SOCKET s, WSAEVENT hEvent, LPWSANETWORKEVENTS lpEvent)
{
    LPWSINFO      pwsi = WINSOCK_GetIData();

    TRACE("(%08x): %08x, hEvent %08x, lpEvent %08x\n",
			  (unsigned)pwsi, s, hEvent, (unsigned)lpEvent );
    if( _check_ws(pwsi, s) )
    {
        SERVER_START_REQ
        {
            struct get_socket_event_request *req = server_alloc_req( sizeof(*req),
                                                                     sizeof(lpEvent->iErrorCode) );
            req->handle  = s;
            req->service = TRUE;
            req->s_event = 0;
            req->c_event = hEvent;
            sock_server_call( REQ_GET_SOCKET_EVENT );
            lpEvent->lNetworkEvents = req->pmask;
            memcpy(lpEvent->iErrorCode, server_data_ptr(req), server_data_size(req) );
        }
        SERVER_END_REQ;
        return 0;
    }
    else SetLastError(WSAEINVAL);
    return SOCKET_ERROR; 
}

/***********************************************************************
 *		WSAEventSelect
 */
int WINAPI WSAEventSelect(SOCKET s, WSAEVENT hEvent, LONG lEvent)
{
    LPWSINFO      pwsi = WINSOCK_GetIData();

    TRACE("(%08x): %08x, hEvent %08x, event %08x\n",
			  (unsigned)pwsi, s, hEvent, (unsigned)lEvent );
    if( _check_ws(pwsi, s) )
    {
        SERVER_START_REQ
        {
            struct set_socket_event_request *req = server_alloc_req( sizeof(*req), 0 );
            req->handle = s;
            req->mask   = lEvent;
            req->event  = hEvent;
            sock_server_call( REQ_SET_SOCKET_EVENT );
        }
        SERVER_END_REQ;
        return 0;
    }
    else SetLastError(WSAEINVAL);
    return SOCKET_ERROR; 
}

/***********************************************************************
 *      WSAAsyncSelect()		(WINSOCK.101)(WSOCK32.101)
 */

VOID CALLBACK WINSOCK_DoAsyncEvent( ULONG_PTR ptr )
{
    ws_select_info *info = (ws_select_info*)ptr;
    LPWSINFO      pwsi = info->pwsi;
    unsigned int i, pmask, orphan = FALSE;
    int errors[FD_MAX_EVENTS];

    TRACE("socket %08x, event %08x\n", info->sock, info->event);
    SetLastError(0);
    SERVER_START_REQ
    {
        struct get_socket_event_request *req = server_alloc_req( sizeof(*req), sizeof(errors) );
        req->handle  = info->sock;
        req->service = TRUE;
        req->s_event = info->event; /* <== avoid race conditions */
        req->c_event = info->event;
        sock_server_call( REQ_GET_SOCKET_EVENT );
        pmask = req->pmask;
        memcpy( errors, server_data_ptr(req), server_data_size(req) );
    }
    SERVER_END_REQ;
    if ( (GetLastError() == WSAENOTSOCK) || (GetLastError() == WSAEINVAL) )
    {
	/* orphaned event (socket closed or something) */
	pmask = WS_FD_SERVEVENT;
	orphan = TRUE;
    }

    /* check for accepted sockets that needs to inherit WSAAsyncSelect */
    if (pmask & WS_FD_SERVEVENT) {
	int q;
	for (q=0; q<WS_ACCEPT_QUEUE; q++)
	    if (pwsi->accept_old[q] == info->sock) {
		/* there's only one service thread per pwsi, no lock necessary */
		HANDLE as = pwsi->accept_new[q];
		if (as) {
		    pwsi->accept_new[q] = 0;
		    pwsi->accept_old[q] = 0;
		    WSAAsyncSelect(as, info->hWnd, info->uMsg, info->lEvent);
		}
	    }
	pmask &= ~WS_FD_SERVEVENT;
    }
    /* dispatch network events */
    for (i=0; i<FD_MAX_EVENTS; i++)
	if (pmask & (1<<i)) {
	    TRACE("post: event bit %d, error %d\n", i, errors[i]);
	    PostMessageA(info->hWnd, info->uMsg, info->sock,
			 WSAMAKESELECTREPLY(1<<i, errors[i]));
	}
    /* cleanup */
    if (orphan)
    {
	TRACE("orphaned event, self-destructing\n");
	/* SERVICE_Delete closes the event object */
	SERVICE_Delete( info->service );
	WS_FREE(info);
    }
}

INT WINAPI WSAAsyncSelect(SOCKET s, HWND hWnd, UINT uMsg, LONG lEvent)
{
    LPWSINFO      pwsi = WINSOCK_GetIData();

    TRACE("(%08x): %04x, hWnd %04x, uMsg %08x, event %08x\n",
			  (unsigned)pwsi, (SOCKET16)s, (HWND16)hWnd, uMsg, (unsigned)lEvent );
    if( _check_ws(pwsi, s) )
    {
	if( lEvent )
	{
	    ws_select_info *info = (ws_select_info*)WS_ALLOC(sizeof(ws_select_info));
	    if( info )
	    {
		HANDLE hObj = CreateEventA( NULL, TRUE, FALSE, NULL );
		INT err;
		
		info->sock   = s;
		info->event  = hObj;
		info->hWnd   = hWnd;
		info->uMsg   = uMsg;
		info->lEvent = lEvent;
		info->pwsi   = pwsi;
		info->service = SERVICE_AddObject( hObj, WINSOCK_DoAsyncEvent, (ULONG_PTR)info );

		err = WSAEventSelect( s, hObj, lEvent | WS_FD_SERVEVENT );
		if (err) {
		    /* SERVICE_Delete closes the event object */
		    SERVICE_Delete( info->service );
		    WS_FREE(info);
		    return err;
		}

		return 0; /* success */
	    }
	    else SetLastError(WSAENOBUFS);
	} 
	else
	{
	    WSAEventSelect(s, 0, 0);
	    return 0;
	}
    } 
    else SetLastError(WSAEINVAL);
    return SOCKET_ERROR; 
}

INT16 WINAPI WSAAsyncSelect16(SOCKET16 s, HWND16 hWnd, UINT16 wMsg, LONG lEvent)
{
    return (INT16)WSAAsyncSelect( s, hWnd, wMsg, lEvent );
}

/***********************************************************************
 *		WSARecvEx()			(WSOCK32.1107)
 *
 * WSARecvEx is a Microsoft specific extension to winsock that is identical to recv
 * except that has an in/out argument call flags that has the value MSG_PARTIAL ored
 * into the flags parameter when a partial packet is read. This only applies to
 * sockets using the datagram protocol. This method does not seem to be implemented
 * correctly by microsoft as the winsock implementation does not set the MSG_PARTIAL
 * flag when a fragmented packet arrives.
 */
INT     WINAPI   WSARecvEx(SOCKET s, char *buf, INT len, INT *flags) {
  FIXME("(WSARecvEx) partial packet return value not set \n");

  return WSOCK32_recv(s, buf, len, *flags);
}


/***********************************************************************
 *              WSARecvEx16()			(WINSOCK.1107)
 *
 * See description for WSARecvEx()
 */
INT16     WINAPI WSARecvEx16(SOCKET16 s, char *buf, INT16 len, INT16 *flags) {
  FIXME("(WSARecvEx16) partial packet return value not set \n");

  return WINSOCK_recv16(s, buf, len, *flags);
}


/***********************************************************************
 *      WSACreateEvent()          (WS2_32.???)
 *
 */
WSAEVENT WINAPI WSACreateEvent(void)
{
    /* Create a manual-reset event, with initial state: unsignealed */
    TRACE("\n");
    
    return CreateEventA(NULL, TRUE, FALSE, NULL);    
}

/***********************************************************************
 *      WSACloseEvent()          (WS2_32.???)
 *
 */
BOOL WINAPI WSACloseEvent(WSAEVENT event)
{
    TRACE ("event=0x%x\n", event);

    return CloseHandle(event);
}

/***********************************************************************
 *      WSASocketA()          (WS2_32.???)
 *
 */
SOCKET WINAPI WSASocketA(int af, int type, int protocol,
                         LPWSAPROTOCOL_INFOA lpProtocolInfo,
                         GROUP g, DWORD dwFlags)
{
   /* 
      FIXME: The "advanced" parameters of WSASocketA (lpProtocolInfo,
      g, dwFlags) are ignored.
   */
   
   TRACE("af=%d type=%d protocol=%d protocol_info=%p group=%d flags=0x%lx\n", 
         af, type, protocol, lpProtocolInfo, g, dwFlags );

   return ( WSOCK32_socket (af, type, protocol) );
}


/***********************************************************************
 *	__WSAFDIsSet()			(WINSOCK.151)
 */
INT16 WINAPI __WSAFDIsSet16(SOCKET16 s, ws_fd_set16 *set)
{
  int i = set->fd_count;
  
  TRACE("(%d,%8lx(%i))\n", s,(unsigned long)set, i);
    
  while (i--)
      if (set->fd_array[i] == s) return 1;
  return 0;
}                                                            

/***********************************************************************
 *      __WSAFDIsSet()			(WSOCK32.151)
 */
INT WINAPI __WSAFDIsSet(SOCKET s, ws_fd_set32 *set)
{
  int i = set->fd_count;

  TRACE("(%d,%8lx(%i))\n", s,(unsigned long)set, i);

  while (i--)
      if (set->fd_array[i] == s) return 1;
  return 0;
}

/***********************************************************************
 *      WSAIsBlocking()			(WINSOCK.114)(WSOCK32.114)
 */
BOOL WINAPI WSAIsBlocking(void)
{
  /* By default WinSock should set all its sockets to non-blocking mode
   * and poll in PeekMessage loop when processing "blocking" ones. This 
   * function is supposed to tell if the program is in this loop. Our 
   * blocking calls are truly blocking so we always return FALSE.
   *
   * Note: It is allowed to call this function without prior WSAStartup().
   */

  TRACE("\n");
  return FALSE;
}

/***********************************************************************
 *      WSACancelBlockingCall()		(WINSOCK.113)(WSOCK32.113)
 */
INT WINAPI WSACancelBlockingCall(void)
{
  LPWSINFO              pwsi = WINSOCK_GetIData();

  TRACE("(%08x)\n", (unsigned)pwsi);

  if( pwsi ) return 0;
  return SOCKET_ERROR;
}


/***********************************************************************
 *      WSASetBlockingHook16()		(WINSOCK.109)
 */
FARPROC16 WINAPI WSASetBlockingHook16(FARPROC16 lpBlockFunc)
{
  FARPROC16		prev;
  LPWSINFO              pwsi = WINSOCK_GetIData();

  TRACE("(%08x): hook %08x\n", 
	       (unsigned)pwsi, (unsigned) lpBlockFunc);
  if( pwsi ) 
  { 
      prev = (FARPROC16)pwsi->blocking_hook; 
      pwsi->blocking_hook = (DWORD)lpBlockFunc; 
      pwsi->flags &= ~WSI_BLOCKINGHOOK;
      return prev; 
  }
  return 0;
}


/***********************************************************************
 *      WSASetBlockingHook()
 */
FARPROC WINAPI WSASetBlockingHook(FARPROC lpBlockFunc)
{
  FARPROC             prev;
  LPWSINFO              pwsi = WINSOCK_GetIData();

  TRACE("(%08x): hook %08x\n",
	       (unsigned)pwsi, (unsigned) lpBlockFunc);
  if( pwsi ) {
      prev = (FARPROC)pwsi->blocking_hook;
      pwsi->blocking_hook = (DWORD)lpBlockFunc;
      pwsi->flags |= WSI_BLOCKINGHOOK;
      return prev;
  }
  return NULL;
}


/***********************************************************************
 *      WSAUnhookBlockingHook16()	(WINSOCK.110)
 */
INT16 WINAPI WSAUnhookBlockingHook16(void)
{
    LPWSINFO              pwsi = WINSOCK_GetIData();

    TRACE("(%08x)\n", (unsigned)pwsi);
    if( pwsi ) return (INT16)(pwsi->blocking_hook = 0);
    return SOCKET_ERROR;
}


/***********************************************************************
 *      WSAUnhookBlockingHook()
 */
INT WINAPI WSAUnhookBlockingHook(void)
{
    LPWSINFO              pwsi = WINSOCK_GetIData();

    TRACE("(%08x)\n", (unsigned)pwsi);
    if( pwsi )
    {
	pwsi->blocking_hook = 0;
	pwsi->flags &= ~WSI_BLOCKINGHOOK;
	return 0;
    }
    return SOCKET_ERROR;
}


/* ----------------------------------- end of API stuff */

/* ----------------------------------- helper functions -
 *
 * TODO: Merge WS_dup_..() stuff into one function that
 * would operate with a generic structure containing internal
 * pointers (via a template of some kind).
 */

static int list_size(char** l, int item_size)
{
  int i,j = 0;
  if(l)
  { for(i=0;l[i];i++) 
	j += (item_size) ? item_size : strlen(l[i]) + 1;
    j += (i + 1) * sizeof(char*); }
  return j;
}

static int list_dup(char** l_src, char* ref, char* base, int item_size)
{ 
   /* base is either either equal to ref or 0 or SEGPTR */

   char*		p = ref;
   char**		l_to = (char**)ref;
   int			i,j,k;

   for(j=0;l_src[j];j++) ;
   p += (j + 1) * sizeof(char*);
   for(i=0;i<j;i++)
   { l_to[i] = base + (p - ref);
     k = ( item_size ) ? item_size : strlen(l_src[i]) + 1;
     memcpy(p, l_src[i], k); p += k; }
   l_to[i] = NULL;
   return (p - ref);
}

/* ----- hostent */

static int hostent_size(struct hostent* p_he)
{
  int size = 0;
  if( p_he )
  { size  = sizeof(struct hostent); 
    size += strlen(p_he->h_name) + 1;
    size += list_size(p_he->h_aliases, 0);  
    size += list_size(p_he->h_addr_list, p_he->h_length ); }
  return size;
}

/* duplicate hostent entry
 * and handle all Win16/Win32 dependent things (struct size, ...) *correctly*.
 * Dito for protoent and servent.
 */
int WS_dup_he(LPWSINFO pwsi, struct hostent* p_he, int flag)
{
    /* Convert hostent structure into ws_hostent so that the data fits 
     * into pwsi->buffer. Internal pointers can be linear, SEGPTR, or 
     * relative to pwsi->buffer depending on "flag" value. Returns size
     * of the data copied (also in the pwsi->buflen).
     */

    int size = hostent_size(p_he);
    if( size )
    {
	char *p_name,*p_aliases,*p_addr,*p_base,*p;
	char *p_to;
	struct ws_hostent16 *p_to16;
	struct ws_hostent32 *p_to32;

	_check_buffer_he(pwsi, size);
	p_to = (char *)pwsi->he;
	p_to16 = (struct ws_hostent16*)pwsi->he;
	p_to32 = (struct ws_hostent32*)pwsi->he;

	p = p_to;
	p_base = (flag & WS_DUP_OFFSET) ? NULL
	    : ((flag & WS_DUP_SEGPTR) ? (char*)SEGPTR_GET(p) : p);
	p += (flag & WS_DUP_SEGPTR) ?
	    sizeof(struct ws_hostent16) : sizeof(struct ws_hostent32);
	p_name = p;
	strcpy(p, p_he->h_name); p += strlen(p) + 1;
	p_aliases = p;
	p += list_dup(p_he->h_aliases, p, p_base + (p - p_to), 0);
	p_addr = p;
	list_dup(p_he->h_addr_list, p, p_base + (p - p_to), p_he->h_length);

	if (flag & WS_DUP_SEGPTR) /* Win16 */
	{
	    p_to16->h_addrtype = (INT16)p_he->h_addrtype; 
	    p_to16->h_length = (INT16)p_he->h_length;
	    p_to16->h_name = (SEGPTR)(p_base + (p_name - p_to));
	    p_to16->h_aliases = (SEGPTR)(p_base + (p_aliases - p_to));
	    p_to16->h_addr_list = (SEGPTR)(p_base + (p_addr - p_to));
	    size += (sizeof(struct ws_hostent16) - sizeof(struct hostent));
	}
	else /* Win32 */
	{
	    p_to32->h_addrtype = p_he->h_addrtype; 
	    p_to32->h_length = p_he->h_length;
	    p_to32->h_name = (p_base + (p_name - p_to));
	    p_to32->h_aliases = (char **)(p_base + (p_aliases - p_to));
	    p_to32->h_addr_list = (char **)(p_base + (p_addr - p_to));
	    size += (sizeof(struct ws_hostent32) - sizeof(struct hostent));
	}
    }
    return size;
}

/* ----- protoent */

static int protoent_size(struct protoent* p_pe)
{
  int size = 0;
  if( p_pe )
  { size  = sizeof(struct protoent);
    size += strlen(p_pe->p_name) + 1;
    size += list_size(p_pe->p_aliases, 0); }
  return size;
}

int WS_dup_pe(LPWSINFO pwsi, struct protoent* p_pe, int flag)
{
    int size = protoent_size(p_pe);
    if( size )
    {
	char *p_to;
	struct ws_protoent16 *p_to16;
	struct ws_protoent32 *p_to32;
	char *p_name,*p_aliases,*p_base,*p;

	_check_buffer_pe(pwsi, size);
	p_to = (char *)pwsi->pe;
	p_to16 = (struct ws_protoent16*)pwsi->pe;
	p_to32 = (struct ws_protoent32*)pwsi->pe;
	p = p_to;
	p_base = (flag & WS_DUP_OFFSET) ? NULL
	    : ((flag & WS_DUP_SEGPTR) ? (char*)SEGPTR_GET(p) : p);
	p += (flag & WS_DUP_SEGPTR) ?
	    sizeof(struct ws_protoent16) : sizeof(struct ws_protoent32);
	p_name = p;
	strcpy(p, p_pe->p_name); p += strlen(p) + 1;
	p_aliases = p;
	list_dup(p_pe->p_aliases, p, p_base + (p - p_to), 0);

	if (flag & WS_DUP_SEGPTR) /* Win16 */
	{
	    p_to16->p_proto = (INT16)p_pe->p_proto;
	    p_to16->p_name = (SEGPTR)(p_base) + (p_name - p_to);
	    p_to16->p_aliases = (SEGPTR)((p_base) + (p_aliases - p_to)); 
	    size += (sizeof(struct ws_protoent16) - sizeof(struct protoent));
	}
	else /* Win32 */
	{
	    p_to32->p_proto = p_pe->p_proto;
	    p_to32->p_name = (p_base) + (p_name - p_to);
	    p_to32->p_aliases = (char **)((p_base) + (p_aliases - p_to)); 
	    size += (sizeof(struct ws_protoent32) - sizeof(struct protoent));
	}
    }
    return size;
}

/* ----- servent */

static int servent_size(struct servent* p_se)
{
  int size = 0;
  if( p_se )
  { size += sizeof(struct servent);
    size += strlen(p_se->s_proto) + strlen(p_se->s_name) + 2;
    size += list_size(p_se->s_aliases, 0); }
  return size;
}

int WS_dup_se(LPWSINFO pwsi, struct servent* p_se, int flag)
{
    int size = servent_size(p_se);
    if( size )
    {
	char *p_name,*p_aliases,*p_proto,*p_base,*p;
	char *p_to;
	struct ws_servent16 *p_to16;
	struct ws_servent32 *p_to32;

	_check_buffer_se(pwsi, size);
	p_to = (char *)pwsi->se;
	p_to16 = (struct ws_servent16*)pwsi->se;
	p_to32 = (struct ws_servent32*)pwsi->se;
	p = p_to;
	p_base = (flag & WS_DUP_OFFSET) ? NULL 
	    : ((flag & WS_DUP_SEGPTR) ? (char*)SEGPTR_GET(p) : p);
	p += (flag & WS_DUP_SEGPTR) ?
	    sizeof(struct ws_servent16) : sizeof(struct ws_servent32);
	p_name = p;
	strcpy(p, p_se->s_name); p += strlen(p) + 1;
	p_proto = p;
	strcpy(p, p_se->s_proto); p += strlen(p) + 1;
	p_aliases = p;
	list_dup(p_se->s_aliases, p, p_base + (p - p_to), 0);

	if (flag & WS_DUP_SEGPTR) /* Win16 */
	{ 
	    p_to16->s_port = (INT16)p_se->s_port;
	    p_to16->s_name = (SEGPTR)(p_base + (p_name - p_to));
	    p_to16->s_proto = (SEGPTR)(p_base + (p_proto - p_to));
	    p_to16->s_aliases = (SEGPTR)(p_base + (p_aliases - p_to));
	    size += (sizeof(struct ws_servent16) - sizeof(struct servent));
	}
	else /* Win32 */
	{
	    p_to32->s_port = p_se->s_port;
	    p_to32->s_name = (p_base + (p_name - p_to));
	    p_to32->s_proto = (p_base + (p_proto - p_to));
	    p_to32->s_aliases = (char **)(p_base + (p_aliases - p_to));
	    size += (sizeof(struct ws_servent32) - sizeof(struct servent));
	}
    }
    return size;
}

/* ----------------------------------- error handling */

UINT16 wsaErrno(void)
{
    int	loc_errno = errno; 
    WARN("errno %d, (%s).\n", loc_errno, strerror(loc_errno));

    switch(loc_errno)
    {
	case EINTR:		return WSAEINTR;
	case EBADF:		return WSAEBADF;
	case EPERM:
	case EACCES:		return WSAEACCES;
	case EFAULT:		return WSAEFAULT;
	case EINVAL:		return WSAEINVAL;
	case EMFILE:		return WSAEMFILE;
	case EWOULDBLOCK:	return WSAEWOULDBLOCK;
	case EINPROGRESS:	return WSAEINPROGRESS;
	case EALREADY:		return WSAEALREADY;
	case ENOTSOCK:		return WSAENOTSOCK;
	case EDESTADDRREQ:	return WSAEDESTADDRREQ;
	case EMSGSIZE:		return WSAEMSGSIZE;
	case EPROTOTYPE:	return WSAEPROTOTYPE;
	case ENOPROTOOPT:	return WSAENOPROTOOPT;
	case EPROTONOSUPPORT:	return WSAEPROTONOSUPPORT;
	case ESOCKTNOSUPPORT:	return WSAESOCKTNOSUPPORT;
	case EOPNOTSUPP:	return WSAEOPNOTSUPP;
	case EPFNOSUPPORT:	return WSAEPFNOSUPPORT;
	case EAFNOSUPPORT:	return WSAEAFNOSUPPORT;
	case EADDRINUSE:	return WSAEADDRINUSE;
	case EADDRNOTAVAIL:	return WSAEADDRNOTAVAIL;
	case ENETDOWN:		return WSAENETDOWN;
	case ENETUNREACH:	return WSAENETUNREACH;
	case ENETRESET:		return WSAENETRESET;
	case ECONNABORTED:	return WSAECONNABORTED;
	case EPIPE:
	case ECONNRESET:	return WSAECONNRESET;
	case ENOBUFS:		return WSAENOBUFS;
	case EISCONN:		return WSAEISCONN;
	case ENOTCONN:		return WSAENOTCONN;
	case ESHUTDOWN:		return WSAESHUTDOWN;
	case ETOOMANYREFS:	return WSAETOOMANYREFS;
	case ETIMEDOUT:		return WSAETIMEDOUT;
	case ECONNREFUSED:	return WSAECONNREFUSED;
	case ELOOP:		return WSAELOOP;
	case ENAMETOOLONG:	return WSAENAMETOOLONG;
	case EHOSTDOWN:		return WSAEHOSTDOWN;
	case EHOSTUNREACH:	return WSAEHOSTUNREACH;
	case ENOTEMPTY:		return WSAENOTEMPTY;
#ifdef EPROCLIM
	case EPROCLIM:		return WSAEPROCLIM;
#endif
#ifdef EUSERS
	case EUSERS:		return WSAEUSERS;
#endif
#ifdef EDQUOT
	case EDQUOT:		return WSAEDQUOT;
#endif
#ifdef ESTALE
	case ESTALE:		return WSAESTALE;
#endif
#ifdef EREMOTE
	case EREMOTE:		return WSAEREMOTE;
#endif

       /* just in case we ever get here and there are no problems */
	case 0:			return 0;
        default:
		WARN("Unknown errno %d!\n", loc_errno);
		return WSAEOPNOTSUPP;
    }
}

UINT16 wsaHerrno(int loc_errno)
{

    WARN("h_errno %d.\n", loc_errno);

    switch(loc_errno)
    {
	case HOST_NOT_FOUND:	return WSAHOST_NOT_FOUND;
	case TRY_AGAIN:		return WSATRY_AGAIN;
	case NO_RECOVERY:	return WSANO_RECOVERY;
	case NO_DATA:		return WSANO_DATA; 
	case ENOBUFS:		return WSAENOBUFS;

	case 0:			return 0;
        default:
		WARN("Unknown h_errno %d!\n", loc_errno);
		return WSAEOPNOTSUPP;
    }
}
