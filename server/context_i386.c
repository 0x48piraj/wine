/*
 * i386 register context support
 *
 * Copyright (C) 1999 Alexandre Julliard
 */

#include "config.h"

#ifdef __i386__

#include <assert.h>
#include <errno.h>
#ifdef HAVE_SYS_REG_H
#include <sys/reg.h>
#endif
#include <unistd.h>
#ifdef HAVE_SYS_PTRACE_H
# include <sys/ptrace.h>
#endif
#ifdef HAVE_SYS_PARAM_H
# include <sys/param.h>
#endif
#ifdef HAVE_SYS_USER_H
# include <sys/user.h>
#endif

#include "winbase.h"
#include "thread.h"
#include "request.h"

#ifndef PTRACE_PEEKUSER
#  ifdef PTRACE_PEEKUSR /* libc5 uses USR not USER */
#    define PTRACE_PEEKUSER PTRACE_PEEKUSR
#  else
#    define PTRACE_PEEKUSER PT_READ_U
#  endif
#endif

#ifndef PTRACE_POKEUSER
#  ifdef PTRACE_POKEUSR /* libc5 uses USR not USER */
#    define PTRACE_POKEUSER PTRACE_POKEUSR
#  else
#    define PTRACE_POKEUSER PT_WRITE_U
#  endif
#endif

#ifndef PTRACE_GETREGS
#define PTRACE_GETREGS PT_GETREGS
#endif
#ifndef PTRACE_GETFPREGS
#define PTRACE_GETFPREGS PT_GETFPREGS
#endif
#ifndef PTRACE_SETREGS
#define PTRACE_SETREGS PT_SETREGS
#endif
#ifndef PTRACE_SETFPREGS
#define PTRACE_SETFPREGS PT_SETFPREGS
#endif

#ifdef linux

/* user_regs definitions from asm/user.h */
struct kernel_user_regs_struct
{
    long ebx, ecx, edx, esi, edi, ebp, eax;
    unsigned short ds, __ds, es, __es;
    unsigned short fs, __fs, gs, __gs;
    long orig_eax, eip;
    unsigned short cs, __cs;
    long eflags, esp;
    unsigned short ss, __ss;
};

/* debug register offset in struct user */
#define DR_OFFSET(dr) ((int)((((struct user *)0)->u_debugreg) + (dr)))

/* retrieve a debug register */
static inline int get_debug_reg( int pid, int num, DWORD *data )
{
    int res = ptrace( PTRACE_PEEKUSER, pid, DR_OFFSET(num), 0 );
    if ((res == -1) && errno)
    {
        file_set_error();
        return -1;
    }
    *data = res;
    return 0;
}

/* retrieve a thread context */
static void get_thread_context( struct thread *thread, unsigned int flags, CONTEXT *context )
{
    int pid = thread->unix_pid;
    if (flags & CONTEXT_FULL)
    {
        struct kernel_user_regs_struct regs;
        if (ptrace( PTRACE_GETREGS, pid, 0, &regs ) == -1) goto error;
        if (flags & CONTEXT_INTEGER)
        {
            context->Eax = regs.eax;
            context->Ebx = regs.ebx;
            context->Ecx = regs.ecx;
            context->Edx = regs.edx;
            context->Esi = regs.esi;
            context->Edi = regs.edi;
        }
        if (flags & CONTEXT_CONTROL)
        {
            context->Ebp    = regs.ebp;
            context->Esp    = regs.esp;
            context->Eip    = regs.eip;
            context->SegCs  = regs.cs;
            context->SegSs  = regs.ss;
            context->EFlags = regs.eflags;
        }
        if (flags & CONTEXT_SEGMENTS)
        {
            context->SegDs = regs.ds;
            context->SegEs = regs.es;
            context->SegFs = regs.fs;
            context->SegGs = regs.gs;
        }
    }
    if (flags & CONTEXT_DEBUG_REGISTERS)
    {
        if (get_debug_reg( pid, 0, &context->Dr0 ) == -1) goto error;
        if (get_debug_reg( pid, 1, &context->Dr1 ) == -1) goto error;
        if (get_debug_reg( pid, 2, &context->Dr2 ) == -1) goto error;
        if (get_debug_reg( pid, 3, &context->Dr3 ) == -1) goto error;
        if (get_debug_reg( pid, 6, &context->Dr6 ) == -1) goto error;
        if (get_debug_reg( pid, 7, &context->Dr7 ) == -1) goto error;
    }
    if (flags & CONTEXT_FLOATING_POINT)
    {
        /* we can use context->FloatSave directly as it is using the */
        /* correct structure (the same as fsave/frstor) */
        if (ptrace( PTRACE_GETFPREGS, pid, 0, &context->FloatSave ) == -1) goto error;
        context->FloatSave.Cr0NpxState = 0;  /* FIXME */
    }
    return;
 error:
    file_set_error();
}


/* set a thread context */
static void set_thread_context( struct thread *thread, unsigned int flags, CONTEXT *context )
{
    int pid = thread->unix_pid;
    if (flags & CONTEXT_FULL)
    {
        struct kernel_user_regs_struct regs;
        if (((flags | CONTEXT_i386) & CONTEXT_FULL) != CONTEXT_FULL)
        {
            /* need to preserve some registers */
            if (ptrace( PTRACE_GETREGS, pid, 0, &regs ) == -1) goto error;
        }
        if (flags & CONTEXT_INTEGER)
        {
            regs.eax = context->Eax;
            regs.ebx = context->Ebx;
            regs.ecx = context->Ecx;
            regs.edx = context->Edx;
            regs.esi = context->Esi;
            regs.edi = context->Edi;
        }
        if (flags & CONTEXT_CONTROL)
        {
            regs.ebp = context->Ebp;
            regs.esp = context->Esp;
            regs.eip = context->Eip;
            regs.cs  = context->SegCs;
            regs.ss  = context->SegSs;
            regs.eflags = context->EFlags;
        }
        if (flags & CONTEXT_SEGMENTS)
        {
            regs.ds = context->SegDs;
            regs.es = context->SegEs;
            regs.fs = context->SegFs;
            regs.gs = context->SegGs;
        }
        if (ptrace( PTRACE_SETREGS, pid, 0, &regs ) == -1) goto error;
    }
    if (flags & CONTEXT_DEBUG_REGISTERS)
    {
        if (ptrace( PTRACE_POKEUSER, pid, DR_OFFSET(0), context->Dr0 ) == -1) goto error;
        if (ptrace( PTRACE_POKEUSER, pid, DR_OFFSET(1), context->Dr1 ) == -1) goto error;
        if (ptrace( PTRACE_POKEUSER, pid, DR_OFFSET(2), context->Dr2 ) == -1) goto error;
        if (ptrace( PTRACE_POKEUSER, pid, DR_OFFSET(3), context->Dr3 ) == -1) goto error;
        if (ptrace( PTRACE_POKEUSER, pid, DR_OFFSET(6), context->Dr6 ) == -1) goto error;
        if (ptrace( PTRACE_POKEUSER, pid, DR_OFFSET(7), context->Dr7 ) == -1) goto error;
    }
    if (flags & CONTEXT_FLOATING_POINT)
    {
        /* we can use context->FloatSave directly as it is using the */
        /* correct structure (the same as fsave/frstor) */
        if (ptrace( PTRACE_SETFPREGS, pid, 0, &context->FloatSave ) == -1) goto error;
        context->FloatSave.Cr0NpxState = 0;  /* FIXME */
    }
    return;
 error:
    file_set_error();
}

#elif defined(__sun) || defined(__sun__)

/* retrieve a thread context */
static void get_thread_context( struct thread *thread, unsigned int flags, CONTEXT *context )
{
    int pid = thread->unix_pid;
    if (flags & CONTEXT_FULL)
    {
        struct regs regs;
        if (ptrace( PTRACE_GETREGS, pid, 0, (int) &regs ) == -1) goto error;
        if (flags & CONTEXT_INTEGER)
        {
            context->Eax = regs.r_eax;
            context->Ebx = regs.r_ebx;
            context->Ecx = regs.r_ecx;
            context->Edx = regs.r_edx;
            context->Esi = regs.r_esi;
            context->Edi = regs.r_edi;
        }
        if (flags & CONTEXT_CONTROL)
        {
            context->Ebp    = regs.r_ebp;
            context->Esp    = regs.r_esp;
            context->Eip    = regs.r_eip;
            context->SegCs  = regs.r_cs & 0xffff;
            context->SegSs  = regs.r_ss & 0xffff;
            context->EFlags = regs.r_efl;
        }
        if (flags & CONTEXT_SEGMENTS)
        {
            context->SegDs = regs.r_ds & 0xffff;
            context->SegEs = regs.r_es & 0xffff;
            context->SegFs = regs.r_fs & 0xffff;
            context->SegGs = regs.r_gs & 0xffff;
        }
    }
    if (flags & CONTEXT_DEBUG_REGISTERS)
    {
        /* FIXME: How is this done on Solaris? */
    }
    if (flags & CONTEXT_FLOATING_POINT)
    {
        /* we can use context->FloatSave directly as it is using the */
        /* correct structure (the same as fsave/frstor) */
        if (ptrace( PTRACE_GETFPREGS, pid, 0, (int) &context->FloatSave ) == -1) goto error;
        context->FloatSave.Cr0NpxState = 0;  /* FIXME */
    }
    return;
 error:
    file_set_error();
}


/* set a thread context */
static void set_thread_context( struct thread *thread, unsigned int flags, CONTEXT *context )
{
    int pid = thread->unix_pid;
    if (flags & CONTEXT_FULL)
    {
        struct regs regs;
        if (((flags | CONTEXT_i386) & CONTEXT_FULL) != CONTEXT_FULL)
        {
            /* need to preserve some registers */
            if (ptrace( PTRACE_GETREGS, pid, 0, (int) &regs ) == -1) goto error;
        }
        if (flags & CONTEXT_INTEGER)
        {
            regs.r_eax = context->Eax;
            regs.r_ebx = context->Ebx;
            regs.r_ecx = context->Ecx;
            regs.r_edx = context->Edx;
            regs.r_esi = context->Esi;
            regs.r_edi = context->Edi;
        }
        if (flags & CONTEXT_CONTROL)
        {
            regs.r_ebp = context->Ebp;
            regs.r_esp = context->Esp;
            regs.r_eip = context->Eip;
            regs.r_cs = context->SegCs;
            regs.r_ss = context->SegSs;
            regs.r_efl = context->EFlags;
        }
        if (flags & CONTEXT_SEGMENTS)
        {
            regs.r_ds = context->SegDs;
            regs.r_es = context->SegEs;
            regs.r_fs = context->SegFs;
            regs.r_gs = context->SegGs;
        }
        if (ptrace( PTRACE_SETREGS, pid, 0, (int) &regs ) == -1) goto error;
    }
    if (flags & CONTEXT_DEBUG_REGISTERS)
    {
        /* FIXME: How is this done on Solaris? */
    }
    if (flags & CONTEXT_FLOATING_POINT)
    {
        /* we can use context->FloatSave directly as it is using the */
        /* correct structure (the same as fsave/frstor) */
        if (ptrace( PTRACE_SETFPREGS, pid, 0, (int) &context->FloatSave ) == -1) goto error;
        context->FloatSave.Cr0NpxState = 0;  /* FIXME */
    }
    return;
 error:
    file_set_error();
}

#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#include <machine/reg.h>

/* retrieve a thread context */
static void get_thread_context( struct thread *thread, unsigned int flags, CONTEXT *context )
{
    int pid = thread->unix_pid;
    if (flags & CONTEXT_FULL)
    {
        struct reg regs;
        if (ptrace( PTRACE_GETREGS, pid, 0, (int) &regs ) == -1) goto error;
        if (flags & CONTEXT_INTEGER)
        {
            context->Eax = regs.r_eax;
            context->Ebx = regs.r_ebx;
            context->Ecx = regs.r_ecx;
            context->Edx = regs.r_edx;
            context->Esi = regs.r_esi;
            context->Edi = regs.r_edi;
        }
        if (flags & CONTEXT_CONTROL)
        {
            context->Ebp    = regs.r_ebp;
            context->Esp    = regs.r_esp;
            context->Eip    = regs.r_eip;
            context->SegCs  = regs.r_cs & 0xffff;
            context->SegSs  = regs.r_ss & 0xffff;
            context->EFlags = regs.r_eflags;
        }
        if (flags & CONTEXT_SEGMENTS)
        {
            context->SegDs = regs.r_ds & 0xffff;
            context->SegEs = regs.r_es & 0xffff;
            context->SegFs = regs.r_fs & 0xffff;
            context->SegGs = regs.r_gs & 0xffff;
        }
    }
    if (flags & CONTEXT_DEBUG_REGISTERS)
    {
        /* FIXME: How is this done on FreeBSD? */
    }
    if (flags & CONTEXT_FLOATING_POINT)
    {
        /* we can use context->FloatSave directly as it is using the */
        /* correct structure (the same as fsave/frstor) */
        if (ptrace( PTRACE_GETFPREGS, pid, 0, (int) &context->FloatSave ) == -1) goto error;
        context->FloatSave.Cr0NpxState = 0;  /* FIXME */
    }
    return;
 error:
    file_set_error();
}


/* set a thread context */
static void set_thread_context( struct thread *thread, unsigned int flags, CONTEXT *context )
{
    int pid = thread->unix_pid;
    if (flags & CONTEXT_FULL)
    {
        struct reg regs;
        if (((flags | CONTEXT_i386) & CONTEXT_FULL) != CONTEXT_FULL)
        {
            /* need to preserve some registers */
            if (ptrace( PTRACE_GETREGS, pid, 0, (int) &regs ) == -1) goto error;
        }
        if (flags & CONTEXT_INTEGER)
        {
            regs.r_eax = context->Eax;
            regs.r_ebx = context->Ebx;
            regs.r_ecx = context->Ecx;
            regs.r_edx = context->Edx;
            regs.r_esi = context->Esi;
            regs.r_edi = context->Edi;
        }
        if (flags & CONTEXT_CONTROL)
        {
            regs.r_ebp = context->Ebp;
            regs.r_esp = context->Esp;
            regs.r_eip = context->Eip;
            regs.r_cs = context->SegCs;
            regs.r_ss = context->SegSs;
            regs.r_eflags = context->EFlags;
        }
        if (flags & CONTEXT_SEGMENTS)
        {
            regs.r_ds = context->SegDs;
            regs.r_es = context->SegEs;
            regs.r_fs = context->SegFs;
            regs.r_gs = context->SegGs;
        }
        if (ptrace( PTRACE_SETREGS, pid, 0, (int) &regs ) == -1) goto error;
    }
    if (flags & CONTEXT_DEBUG_REGISTERS)
    {
        /* FIXME: How is this done on FreeBSD? */
    }
    if (flags & CONTEXT_FLOATING_POINT)
    {
        /* we can use context->FloatSave directly as it is using the */
        /* correct structure (the same as fsave/frstor) */
        if (ptrace( PTRACE_SETFPREGS, pid, 0, (int) &context->FloatSave ) == -1) goto error;
        context->FloatSave.Cr0NpxState = 0;  /* FIXME */
    }
    return;
 error:
    file_set_error();
}

#else  /* linux || __sun__ || __FreeBSD__ */
#error You must implement get/set_thread_context for your platform
#endif  /* linux || __sun__ || __FreeBSD__ */


/* copy a context structure according to the flags */
static void copy_context( CONTEXT *to, CONTEXT *from, int flags )
{
    if (flags & CONTEXT_CONTROL)
    {
        to->Ebp    = from->Ebp;
        to->Eip    = from->Eip;
        to->Esp    = from->Esp;
        to->SegCs  = from->SegCs;
        to->SegSs  = from->SegSs;
        to->EFlags = from->EFlags;
    }
    if (flags & CONTEXT_INTEGER)
    {
        to->Eax = from->Eax;
        to->Ebx = from->Ebx;
        to->Ecx = from->Ecx;
        to->Edx = from->Edx;
        to->Esi = from->Esi;
        to->Edi = from->Edi;
    }
    if (flags & CONTEXT_SEGMENTS)
    {
        to->SegDs = from->SegDs;
        to->SegEs = from->SegEs;
        to->SegFs = from->SegFs;
        to->SegGs = from->SegGs;
    }
    if (flags & CONTEXT_FLOATING_POINT)
    {
        to->FloatSave = from->FloatSave;
    }
    /* we don't bother copying the debug registers, since they */
    /* always need to be accessed by ptrace anyway */
}

/* retrieve the current instruction pointer of a thread */
void *get_thread_ip( struct thread *thread )
{
    CONTEXT context;
    context.Eip = 0;
    if (suspend_for_ptrace( thread ))
    {
        get_thread_context( thread, CONTEXT_CONTROL, &context );
        resume_thread( thread );
    }
    return (void *)context.Eip;
}

/* determine if we should continue the thread in single-step mode */
int get_thread_single_step( struct thread *thread )
{
    CONTEXT context;
    if (thread->context) return 0;  /* don't single-step inside exception event */
    get_thread_context( thread, CONTEXT_CONTROL, &context );
    return (context.EFlags & 0x100) != 0;
}

/* retrieve the current context of a thread */
DECL_HANDLER(get_thread_context)
{
    struct thread *thread;
    int flags = req->flags & ~CONTEXT_i386;  /* get rid of CPU id */

    if (get_req_data_size(req) < sizeof(CONTEXT))
    {
        set_error( STATUS_INVALID_PARAMETER );
        return;
    }
    if ((thread = get_thread_from_handle( req->handle, THREAD_GET_CONTEXT )))
    {
        if (thread->context)  /* thread is inside an exception event */
        {
            copy_context( get_req_data(req), thread->context, flags );
            flags &= CONTEXT_DEBUG_REGISTERS;
        }
        if (flags && suspend_for_ptrace( thread ))
        {
            get_thread_context( thread, flags, get_req_data(req) );
            resume_thread( thread );
        }
        release_object( thread );
    }
}


/* set the current context of a thread */
DECL_HANDLER(set_thread_context)
{
    struct thread *thread;
    int flags = req->flags & ~CONTEXT_i386;  /* get rid of CPU id */

    if (get_req_data_size(req) < sizeof(CONTEXT))
    {
        set_error( STATUS_INVALID_PARAMETER );
        return;
    }
    if ((thread = get_thread_from_handle( req->handle, THREAD_SET_CONTEXT )))
    {
        if (thread->context)  /* thread is inside an exception event */
        {
            copy_context( thread->context, get_req_data(req), flags );
            flags &= CONTEXT_DEBUG_REGISTERS;
        }
        if (flags && suspend_for_ptrace( thread ))
        {
            set_thread_context( thread, flags, get_req_data(req) );
            resume_thread( thread );
        }
        release_object( thread );
    }
}

#endif  /* __i386__ */
