/*
 * Wine server processes
 *
 * Copyright (C) 1999 Alexandre Julliard
 */

#ifndef __WINE_SERVER_PROCESS_H
#define __WINE_SERVER_PROCESS_H

#ifndef __WINE_SERVER__
#error This file can only be used in the Wine server
#endif

#include "object.h"

struct msg_queue;
struct atom_table;

/* process structures */

struct process_dll
{
    struct process_dll  *next;            /* per-process dll list */
    struct process_dll  *prev;
    struct file         *file;            /* dll file */
    void                *base;            /* dll base address (in process addr space) */
    void                *name;            /* ptr to ptr to name (in process addr space) */
    int                  dbg_offset;      /* debug info offset */
    int                  dbg_size;        /* debug info size */
};

struct process
{
    struct object        obj;             /* object header */
    struct process      *next;            /* system-wide process list */
    struct process      *prev;
    struct thread       *thread_list;     /* head of the thread list */
    struct thread       *debugger;        /* thread debugging this process */
    struct object       *handles;         /* handle entries */
    int                  exit_code;       /* process exit code */
    int                  running_threads; /* number of threads running in this process */
    struct timeval       start_time;      /* absolute time at process start */
    struct timeval       end_time;        /* absolute time at process end */
    int                  priority;        /* priority class */
    int                  affinity;        /* process affinity mask */
    int                  suspend;         /* global process suspend count */
    int                  create_flags;    /* process creation flags */
    struct object       *console_in;      /* console input */
    struct object       *console_out;     /* console output */
    struct event        *init_event;      /* event for init done */
    struct event        *idle_event;      /* event for input idle */
    struct msg_queue    *queue;           /* main message queue */
    struct atom_table   *atom_table;      /* pointer to local atom table */
    struct process_dll   exe;             /* main exe file */
    void                *ldt_copy;        /* pointer to LDT copy in client addr space */
    void                *ldt_flags;       /* pointer to LDT flags in client addr space */
};

struct process_snapshot
{
    struct process *process;  /* process ptr */
    struct process *parent;   /* process parent */
    int             count;    /* process refcount */
    int             threads;  /* number of threads */
    int             priority; /* priority class */
};

struct module_snapshot
{
    void           *base;     /* module base addr */
};

/* process functions */

extern struct thread *create_process( int fd );
extern struct process *get_process_from_id( void *id );
extern struct process *get_process_from_handle( handle_t handle, unsigned int access );
extern int process_set_debugger( struct process *process, struct thread *thread );
extern void add_process_thread( struct process *process,
                                struct thread *thread );
extern void remove_process_thread( struct process *process,
                                   struct thread *thread );
extern void suspend_process( struct process *process );
extern void resume_process( struct process *process );
extern void kill_debugged_processes( struct thread *debugger, int exit_code );
extern struct process_snapshot *process_snap( int *count );
extern struct module_snapshot *module_snap( struct process *process, int *count );

static inline void *get_process_id( struct process *process ) { return process; }

#endif  /* __WINE_SERVER_PROCESS_H */
