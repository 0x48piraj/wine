/*
 * Win32 threads
 *
 * Copyright 1996 Alexandre Julliard
 */

#include "config.h"

#include <assert.h>
#include <fcntl.h>
#include <sys/types.h>
#ifdef HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif
#include <unistd.h>
#include "wine/winbase16.h"
#include "wine/port.h"
#include "thread.h"
#include "task.h"
#include "module.h"
#include "winerror.h"
#include "heap.h"
#include "selectors.h"
#include "winnt.h"
#include "server.h"
#include "services.h"
#include "stackframe.h"
#include "builtin16.h"
#include "debugtools.h"
#include "winnls.h"

DEFAULT_DEBUG_CHANNEL(thread);
DECLARE_DEBUG_CHANNEL(relay);

/* TEB of the initial thread */
static TEB initial_teb;

extern struct _PDB current_process;

/***********************************************************************
 *           THREAD_IdToTEB
 *
 * Convert a thread id to a TEB, making sure it is valid.
 */
TEB *THREAD_IdToTEB( DWORD id )
{
    TEB *ret = NULL;

    if (!id || id == GetCurrentThreadId()) return NtCurrentTeb();

    SERVER_START_REQ( get_thread_info )
    {
        req->handle = 0;
        req->tid_in = (void *)id;
        if (!SERVER_CALL()) ret = req->teb;
    }
    SERVER_END_REQ;

    if (!ret)
    {
        /* Allow task handles to be used; convert to main thread */
        if ( IsTask16( id ) )
        {
            TDB *pTask = TASK_GetPtr( id );
            if (pTask) return pTask->teb;
        }
        SetLastError( ERROR_INVALID_PARAMETER );
    }
    return ret;
}


/***********************************************************************
 *           THREAD_InitTEB
 *
 * Initialization of a newly created TEB.
 */
static BOOL THREAD_InitTEB( TEB *teb )
{
    teb->except    = (void *)~0UL;
    teb->self      = teb;
    teb->tibflags  = TEBF_WIN32;
    teb->tls_ptr   = teb->tls_array;
    teb->exit_code = STILL_ACTIVE;
    teb->request_fd = -1;
    teb->reply_fd   = -1;
    teb->wait_fd[0] = -1;
    teb->wait_fd[1] = -1;
    teb->stack_top  = (void *)~0UL;
    teb->StaticUnicodeString.MaximumLength = sizeof(teb->StaticUnicodeBuffer);
    teb->StaticUnicodeString.Buffer = (PWSTR)teb->StaticUnicodeBuffer;
    teb->teb_sel = SELECTOR_AllocBlock( teb, 0x1000, WINE_LDT_FLAGS_DATA|WINE_LDT_FLAGS_32BIT );
    return (teb->teb_sel != 0);
}


/***********************************************************************
 *           THREAD_FreeTEB
 *
 * Free data structures associated with a thread.
 * Must be called from the context of another thread.
 */
static void CALLBACK THREAD_FreeTEB( TEB *teb )
{
    TRACE("(%p) called\n", teb );
    if (teb->cleanup) SERVICE_Delete( teb->cleanup );

    /* Free the associated memory */

    close( teb->request_fd );
    close( teb->reply_fd );
    close( teb->wait_fd[0] );
    close( teb->wait_fd[1] );
    if (teb->stack_sel) FreeSelector16( teb->stack_sel );
    FreeSelector16( teb->teb_sel );
    if (teb->buffer) munmap( (void *)teb->buffer, teb->buffer_size );
    if (teb->debug_info) HeapFree( GetProcessHeap(), 0, teb->debug_info );
    VirtualFree( teb->stack_base, 0, MEM_RELEASE );
}


/***********************************************************************
 *           THREAD_InitStack
 *
 * Allocate the stack of a thread.
 */
TEB *THREAD_InitStack( TEB *teb, DWORD stack_size )
{
    DWORD old_prot, total_size;
    DWORD page_size = getpagesize();
    void *base;

    /* Allocate the stack */

    if (stack_size >= 16*1024*1024)
    	WARN("Thread stack size is %ld MB.\n",stack_size/1024/1024);

    /* if size is smaller than default, get stack size from parent */
    if (stack_size < 1024 * 1024)
    {
        if (teb)
            stack_size = 1024 * 1024;  /* no parent */
        else
            stack_size = ((char *)NtCurrentTeb()->stack_top - (char *)NtCurrentTeb()->stack_base
                          - SIGNAL_STACK_SIZE - 3 * page_size);
    }

    /* FIXME: some Wine functions use a lot of stack, so we add 64Kb here */
    stack_size += 64 * 1024;

    /* Memory layout in allocated block:
     *
     *   size                 contents
     * 1 page              NOACCESS guard page
     * SIGNAL_STACK_SIZE   signal stack
     * 1 page              NOACCESS guard page
     * 1 page              PAGE_GUARD guard page
     * stack_size          normal stack
     * 64Kb                16-bit stack (optional)
     * 1 page              TEB (except for initial thread)
     */

    stack_size = (stack_size + (page_size - 1)) & ~(page_size - 1);
    total_size = stack_size + SIGNAL_STACK_SIZE + 3 * page_size;
    total_size += 0x10000; /* 16-bit stack */
    if (!teb) total_size += page_size;

    if (!(base = VirtualAlloc( NULL, total_size, MEM_COMMIT, PAGE_EXECUTE_READWRITE )))
        return NULL;

    if (!teb)
    {
        teb = (TEB *)((char *)base + total_size - page_size);
        if (!THREAD_InitTEB( teb ))
        {
            VirtualFree( base, 0, MEM_RELEASE );
            return NULL;
        }
    }

    teb->stack_low    = base;
    teb->stack_base   = base;
    teb->signal_stack = (char *)base + page_size;
    teb->stack_top    = (char *)base + 3 * page_size + SIGNAL_STACK_SIZE + stack_size;

    /* Setup guard pages */

    VirtualProtect( base, 1, PAGE_NOACCESS, &old_prot );
    VirtualProtect( (char *)teb->signal_stack + SIGNAL_STACK_SIZE, 1, PAGE_NOACCESS, &old_prot );
    VirtualProtect( (char *)teb->signal_stack + SIGNAL_STACK_SIZE + page_size, 1,
                    PAGE_EXECUTE_READWRITE | PAGE_GUARD, &old_prot );

    /* Allocate the 16-bit stack selector */

    teb->stack_sel = SELECTOR_AllocBlock( teb->stack_top, 0x10000, WINE_LDT_FLAGS_DATA );
    if (!teb->stack_sel) goto error;
    teb->cur_stack = MAKESEGPTR( teb->stack_sel, 0x10000 - sizeof(STACK16FRAME) );

    return teb;

error:
    THREAD_FreeTEB( teb );
    return NULL;
}


/***********************************************************************
 *           thread_errno_location
 *
 * Get the per-thread errno location.
 */
static int *thread_errno_location(void)
{
    return &NtCurrentTeb()->thread_errno;
}

/***********************************************************************
 *           thread_h_errno_location
 *
 * Get the per-thread h_errno location.
 */
static int *thread_h_errno_location(void)
{
    return &NtCurrentTeb()->thread_h_errno;
}

/***********************************************************************
 *           THREAD_Init
 *
 * Setup the initial thread.
 *
 * NOTES: The first allocated TEB on NT is at 0x7ffde000.
 */
void THREAD_Init(void)
{
    if (!initial_teb.self)  /* do it only once */
    {
        THREAD_InitTEB( &initial_teb );
        assert( initial_teb.teb_sel );
        initial_teb.process = &current_process;
        SYSDEPS_SetCurThread( &initial_teb );
        wine_errno_location = thread_errno_location;
        wine_h_errno_location = thread_h_errno_location;
    }
}

DECL_GLOBAL_CONSTRUCTOR(thread_init) { THREAD_Init(); }


/***********************************************************************
 *           THREAD_Start
 *
 * Start execution of a newly created thread. Does not return.
 */
static void THREAD_Start(void)
{
    HANDLE cleanup_object;
    LPTHREAD_START_ROUTINE func = (LPTHREAD_START_ROUTINE)NtCurrentTeb()->entry_point;

    /* install cleanup handler */
    if (DuplicateHandle( GetCurrentProcess(), GetCurrentThread(),
                         GetCurrentProcess(), &cleanup_object, 
                         0, FALSE, DUPLICATE_SAME_ACCESS ))
        NtCurrentTeb()->cleanup = SERVICE_AddObject( cleanup_object, (PAPCFUNC)THREAD_FreeTEB,
                                                     (ULONG_PTR)NtCurrentTeb() );

    if (TRACE_ON(relay))
        DPRINTF("%08lx:Starting thread (entryproc=%p)\n", GetCurrentThreadId(), func );

    PROCESS_CallUserSignalProc( USIG_THREAD_INIT, 0 );
    PE_InitTls();
    MODULE_DllThreadAttach( NULL );
    ExitThread( func( NtCurrentTeb()->entry_arg ) );
}


/***********************************************************************
 *           CreateThread   (KERNEL32.@)
 */
HANDLE WINAPI CreateThread( SECURITY_ATTRIBUTES *sa, DWORD stack,
                            LPTHREAD_START_ROUTINE start, LPVOID param,
                            DWORD flags, LPDWORD id )
{
    HANDLE handle = 0;
    TEB *teb;
    void *tid = 0;
    int request_pipe[2];

    if (pipe( request_pipe ) == -1)
    {
        SetLastError( ERROR_TOO_MANY_OPEN_FILES );
        return 0;
    }
    fcntl( request_pipe[1], F_SETFD, 1 ); /* set close on exec flag */
    wine_server_send_fd( request_pipe[0] );

    SERVER_START_REQ( new_thread )
    {
        req->suspend    = ((flags & CREATE_SUSPENDED) != 0);
        req->inherit    = (sa && (sa->nLength>=sizeof(*sa)) && sa->bInheritHandle);
        req->request_fd = request_pipe[0];
        if (!SERVER_CALL_ERR())
        {
            handle = req->handle;
            tid = req->tid;
        }
        close( request_pipe[0] );
    }
    SERVER_END_REQ;

    if (!handle || !(teb = THREAD_InitStack( NULL, stack )))
    {
        close( request_pipe[1] );
        return 0;
    }

    teb->process     = NtCurrentTeb()->process;
    teb->tid         = tid;
    teb->request_fd  = request_pipe[1];
    teb->entry_point = start;
    teb->entry_arg   = param;
    teb->startup     = THREAD_Start;
    teb->htask16     = GetCurrentTask();

    if (id) *id = (DWORD)tid;
    if (SYSDEPS_SpawnThread( teb ) == -1)
    {
        CloseHandle( handle );
        THREAD_FreeTEB( teb );
        return 0;
    }
    return handle;
}

/***********************************************************************
 *           CreateThread16   (KERNEL.441)
 */
static DWORD CALLBACK THREAD_StartThread16( LPVOID threadArgs )
{
    FARPROC16 start = ((FARPROC16 *)threadArgs)[0];
    DWORD     param = ((DWORD *)threadArgs)[1];
    HeapFree( GetProcessHeap(), 0, threadArgs );

    ((LPDWORD)CURRENT_STACK16)[-1] = param;
    return wine_call_to_16_long( start, sizeof(DWORD) );
}
HANDLE WINAPI CreateThread16( SECURITY_ATTRIBUTES *sa, DWORD stack,
                              FARPROC16 start, SEGPTR param,
                              DWORD flags, LPDWORD id )
{
    DWORD *threadArgs = HeapAlloc( GetProcessHeap(), 0, 2*sizeof(DWORD) );
    if (!threadArgs) return INVALID_HANDLE_VALUE;
    threadArgs[0] = (DWORD)start;
    threadArgs[1] = (DWORD)param;

    return CreateThread( sa, stack, THREAD_StartThread16, threadArgs, flags, id );
}


/***********************************************************************
 * ExitThread [KERNEL32.@]  Ends a thread
 *
 * RETURNS
 *    None
 */
void WINAPI ExitThread( DWORD code ) /* [in] Exit code for this thread */
{
    BOOL last;
    SERVER_START_REQ( terminate_thread )
    {
        /* send the exit code to the server */
        req->handle    = GetCurrentThread();
        req->exit_code = code;
        SERVER_CALL();
        last = req->last;
    }
    SERVER_END_REQ;

    if (last)
    {
        MODULE_DllProcessDetach( TRUE, (LPVOID)1 );
        exit( code );
    }
    else
    {
        MODULE_DllThreadDetach( NULL );
        if (!(NtCurrentTeb()->tibflags & TEBF_WIN32)) TASK_ExitTask();
        SYSDEPS_ExitThread( code );
    }
}


/***********************************************************************
 * SetThreadContext [KERNEL32.@]  Sets context of thread.
 *
 * RETURNS
 *    Success: TRUE
 *    Failure: FALSE
 */
BOOL WINAPI SetThreadContext( HANDLE handle,           /* [in]  Handle to thread with context */
                              const CONTEXT *context ) /* [in] Address of context structure */
{
    BOOL ret;
    SERVER_START_VAR_REQ( set_thread_context, sizeof(*context) )
    {
        req->handle = handle;
        req->flags = context->ContextFlags;
        memcpy( server_data_ptr(req), context, sizeof(*context) );
        ret = !SERVER_CALL_ERR();
    }
    SERVER_END_VAR_REQ;
    return ret;
}


/***********************************************************************
 * GetThreadContext [KERNEL32.@]  Retrieves context of thread.
 *
 * RETURNS
 *    Success: TRUE
 *    Failure: FALSE
 */
BOOL WINAPI GetThreadContext( HANDLE handle,     /* [in]  Handle to thread with context */
                              CONTEXT *context ) /* [out] Address of context structure */
{
    BOOL ret;
    SERVER_START_VAR_REQ( get_thread_context, sizeof(*context) )
    {
        req->handle = handle;
        req->flags = context->ContextFlags;
        memcpy( server_data_ptr(req), context, sizeof(*context) );
        if ((ret = !SERVER_CALL_ERR()))
            memcpy( context, server_data_ptr(req), sizeof(*context) );
    }
    SERVER_END_VAR_REQ;
    return ret;
}


/**********************************************************************
 * GetThreadPriority [KERNEL32.@]  Returns priority for thread.
 *
 * RETURNS
 *    Success: Thread's priority level.
 *    Failure: THREAD_PRIORITY_ERROR_RETURN
 */
INT WINAPI GetThreadPriority(
    HANDLE hthread) /* [in] Handle to thread */
{
    INT ret = THREAD_PRIORITY_ERROR_RETURN;
    SERVER_START_REQ( get_thread_info )
    {
        req->handle = hthread;
        req->tid_in = 0;
        if (!SERVER_CALL_ERR()) ret = req->priority;
    }
    SERVER_END_REQ;
    return ret;
}


/**********************************************************************
 * SetThreadPriority [KERNEL32.@]  Sets priority for thread.
 *
 * RETURNS
 *    Success: TRUE
 *    Failure: FALSE
 */
BOOL WINAPI SetThreadPriority(
    HANDLE hthread, /* [in] Handle to thread */
    INT priority)   /* [in] Thread priority level */
{
    BOOL ret;
    SERVER_START_REQ( set_thread_info )
    {
        req->handle   = hthread;
        req->priority = priority;
        req->mask     = SET_THREAD_INFO_PRIORITY;
        ret = !SERVER_CALL_ERR();
    }
    SERVER_END_REQ;
    return ret;
}


/**********************************************************************
 * GetThreadPriorityBoost [KERNEL32.@]  Returns priority boost for thread.
 *
 * Always reports that priority boost is disabled.
 *
 * RETURNS
 *    Success: TRUE.
 *    Failure: FALSE
 */
BOOL WINAPI GetThreadPriorityBoost(
    HANDLE hthread, /* [in] Handle to thread */
    PBOOL pstate)   /* [out] pointer to var that receives the boost state */
{
    if (pstate) *pstate = FALSE;
    return NO_ERROR;
}


/**********************************************************************
 * SetThreadPriorityBoost [KERNEL32.@]  Sets priority boost for thread.
 *
 * Priority boost is not implemented. Thsi function always returns 
 * FALSE and sets last error to ERROR_CALL_NOT_IMPLEMENTED
 *
 * RETURNS
 *    Always returns FALSE to indicate a failure 
 */
BOOL WINAPI SetThreadPriorityBoost(
    HANDLE hthread, /* [in] Handle to thread */
    BOOL disable)   /* [in] TRUE to disable priority boost */
{
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}


/**********************************************************************
 *           SetThreadAffinityMask   (KERNEL32.@)
 */
DWORD WINAPI SetThreadAffinityMask( HANDLE hThread, DWORD dwThreadAffinityMask )
{
    DWORD ret;
    SERVER_START_REQ( set_thread_info )
    {
        req->handle   = hThread;
        req->affinity = dwThreadAffinityMask;
        req->mask     = SET_THREAD_INFO_AFFINITY;
        ret = !SERVER_CALL_ERR();
        /* FIXME: should return previous value */
    }
    SERVER_END_REQ;
    return ret;
}


/**********************************************************************
 * TerminateThread [KERNEL32.@]  Terminates a thread
 *
 * RETURNS
 *    Success: TRUE
 *    Failure: FALSE
 */
BOOL WINAPI TerminateThread( HANDLE handle,    /* [in] Handle to thread */
                             DWORD exit_code)  /* [in] Exit code for thread */
{
    NTSTATUS status = NtTerminateThread( handle, exit_code );
    if (status) SetLastError( RtlNtStatusToDosError(status) );
    return !status;
}


/**********************************************************************
 *		GetExitCodeThread (KERNEL32.@)
 *		GetExitCodeThread (WIN32S16.13)
 *
 * Gets termination status of thread.
 * 
 * RETURNS
 *    Success: TRUE
 *    Failure: FALSE
 */
BOOL WINAPI GetExitCodeThread(
    HANDLE hthread, /* [in]  Handle to thread */
    LPDWORD exitcode) /* [out] Address to receive termination status */
{
    BOOL ret;
    SERVER_START_REQ( get_thread_info )
    {
        req->handle = hthread;
        req->tid_in = 0;
        ret = !SERVER_CALL_ERR();
        if (ret && exitcode) *exitcode = req->exit_code;
    }
    SERVER_END_REQ;
    return ret;
}


/**********************************************************************
 * ResumeThread [KERNEL32.@]  Resumes a thread.
 *
 * Decrements a thread's suspend count.  When count is zero, the
 * execution of the thread is resumed.
 *
 * RETURNS
 *    Success: Previous suspend count
 *    Failure: 0xFFFFFFFF
 *    Already running: 0
 */
DWORD WINAPI ResumeThread(
    HANDLE hthread) /* [in] Identifies thread to restart */
{
    DWORD ret = 0xffffffff;
    SERVER_START_REQ( resume_thread )
    {
        req->handle = hthread;
        if (!SERVER_CALL_ERR()) ret = req->count;
    }
    SERVER_END_REQ;
    return ret;
}


/**********************************************************************
 * SuspendThread [KERNEL32.@]  Suspends a thread.
 *
 * RETURNS
 *    Success: Previous suspend count
 *    Failure: 0xFFFFFFFF
 */
DWORD WINAPI SuspendThread(
    HANDLE hthread) /* [in] Handle to the thread */
{
    DWORD ret = 0xffffffff;
    SERVER_START_REQ( suspend_thread )
    {
        req->handle = hthread;
        if (!SERVER_CALL_ERR()) ret = req->count;
    }
    SERVER_END_REQ;
    return ret;
}


/***********************************************************************
 *              QueueUserAPC  (KERNEL32.@)
 */
DWORD WINAPI QueueUserAPC( PAPCFUNC func, HANDLE hthread, ULONG_PTR data )
{
    DWORD ret;
    SERVER_START_REQ( queue_apc )
    {
        req->handle = hthread;
        req->user   = 1;
        req->func   = func;
        req->param  = (void *)data;
        ret = !SERVER_CALL_ERR();
    }
    SERVER_END_REQ;
    return ret;
}


/**********************************************************************
 * GetThreadTimes [KERNEL32.@]  Obtains timing information.
 *
 * NOTES
 *    What are the fields where these values are stored?
 *
 * RETURNS
 *    Success: TRUE
 *    Failure: FALSE
 */
BOOL WINAPI GetThreadTimes( 
    HANDLE thread,         /* [in]  Specifies the thread of interest */
    LPFILETIME creationtime, /* [out] When the thread was created */
    LPFILETIME exittime,     /* [out] When the thread was destroyed */
    LPFILETIME kerneltime,   /* [out] Time thread spent in kernel mode */
    LPFILETIME usertime)     /* [out] Time thread spent in user mode */
{
    FIXME("(0x%08x): stub\n",thread);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}


/**********************************************************************
 * VWin32_BoostThreadGroup [KERNEL.535]
 */
VOID WINAPI VWin32_BoostThreadGroup( DWORD threadId, INT boost )
{
    FIXME("(0x%08lx,%d): stub\n", threadId, boost);
}

/**********************************************************************
 * VWin32_BoostThreadStatic [KERNEL.536]
 */
VOID WINAPI VWin32_BoostThreadStatic( DWORD threadId, INT boost )
{
    FIXME("(0x%08lx,%d): stub\n", threadId, boost);
}


/***********************************************************************
 *           GetThreadLocale    (KERNEL32.@)
 */
LCID WINAPI GetThreadLocale(void)
{
    LCID ret = NtCurrentTeb()->CurrentLocale;
    if (!ret) NtCurrentTeb()->CurrentLocale = ret = GetUserDefaultLCID();
    return ret;
}


/**********************************************************************
 * SetThreadLocale [KERNEL32.@]  Sets the calling threads current locale.
 *
 * RETURNS
 *    Success: TRUE
 *    Failure: FALSE
 *
 * FIXME
 *  check if lcid is a valid cp
 */
BOOL WINAPI SetThreadLocale(
    LCID lcid)     /* [in] Locale identifier */
{
    switch (lcid)
    {
      case LOCALE_SYSTEM_DEFAULT:
        lcid = GetSystemDefaultLCID();
	break;
      case LOCALE_USER_DEFAULT:
      case LOCALE_NEUTRAL:
        lcid = GetUserDefaultLCID();
	break;
    }
    NtCurrentTeb()->CurrentLocale = lcid;
    return TRUE;
}


/***********************************************************************
 * GetCurrentThread [KERNEL32.@]  Gets pseudohandle for current thread
 *
 * RETURNS
 *    Pseudohandle for the current thread
 */
#undef GetCurrentThread
HANDLE WINAPI GetCurrentThread(void)
{
    return 0xfffffffe;
}


/***********************************************************************
 * ProcessIdToSessionId   (KERNEL32.@)
 * This function is available on Terminal Server 4SP4 and Windows 2000
 */
BOOL WINAPI ProcessIdToSessionId( DWORD procid, DWORD *sessionid_ptr )
{
	/* According to MSDN, if the calling process is not in a terminal
	 * services environment, then the sessionid returned is zero.
	 */
	*sessionid_ptr = 0;
	return TRUE;
}

/***********************************************************************
 * SetThreadExecutionState (KERNEL32.@)
 *
 * Informs the system that activity is taking place for
 * power management purposes.
 */ 
EXECUTION_STATE WINAPI SetThreadExecutionState(EXECUTION_STATE flags)
{
    FIXME("(0x%lx): stub\n", flags);
    return ES_SYSTEM_REQUIRED|ES_DISPLAY_REQUIRED|ES_USER_PRESENT;
}


#ifdef __i386__

/* void WINAPI SetLastError( DWORD error ); */
__ASM_GLOBAL_FUNC( SetLastError,
                   "movl 4(%esp),%eax\n\t"
                   ".byte 0x64\n\t"
                   "movl %eax,0x60\n\t"
                   "ret $4" );

/* DWORD WINAPI GetLastError(void); */
__ASM_GLOBAL_FUNC( GetLastError, ".byte 0x64\n\tmovl 0x60,%eax\n\tret" );

/* DWORD WINAPI GetCurrentProcessId(void) */
__ASM_GLOBAL_FUNC( GetCurrentProcessId, ".byte 0x64\n\tmovl 0x20,%eax\n\tret" );
                   
/* DWORD WINAPI GetCurrentThreadId(void) */
__ASM_GLOBAL_FUNC( GetCurrentThreadId, ".byte 0x64\n\tmovl 0x24,%eax\n\tret" );
                   
#else  /* __i386__ */

/**********************************************************************
 *		SetLastError (KERNEL.147)
 *		SetLastError (KERNEL32.@)
 *
 * Sets the last-error code.
 */
void WINAPI SetLastError( DWORD error ) /* [in] Per-thread error code */
{
    NtCurrentTeb()->last_error = error;
}

/**********************************************************************
 *		GetLastError (KERNEL.10)
 *              GetLastError (KERNEL32.@)
 *              GetLastError (WIN32S16.10)
 *
 * Returns last-error code.
 */
DWORD WINAPI GetLastError(void)
{
    return NtCurrentTeb()->last_error;
}

/***********************************************************************
 *		GetCurrentProcessId (KERNEL.471)
 *		GetCurrentProcessId (KERNEL32.@)
 *
 * Returns process identifier.
 */
DWORD WINAPI GetCurrentProcessId(void)
{
    return (DWORD)NtCurrentTeb()->pid;
}

/***********************************************************************
 *		GetCurrentThreadId (KERNEL.462)
 *		GetCurrentThreadId (KERNEL32.@)
 *
 * Returns thread identifier.
 */
DWORD WINAPI GetCurrentThreadId(void)
{
    return (DWORD)NtCurrentTeb()->tid;
}

#endif  /* __i386__ */
