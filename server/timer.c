/*
 * Waitable timers management
 *
 * Copyright (C) 1999 Alexandre Julliard
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>

#include "winnt.h"
#include "handle.h"
#include "request.h"

struct timer
{
    struct object        obj;       /* object header */
    int                  manual;    /* manual reset */
    int                  signaled;  /* current signaled state */
    int                  period;    /* timer period in ms */
    struct timeval       when;      /* next expiration */
    struct timeout_user *timeout;   /* timeout user */
    struct thread       *thread;    /* thread that set the APC function */
    void                *callback;  /* callback APC function */
    void                *arg;       /* callback argument */
};

static void timer_dump( struct object *obj, int verbose );
static int timer_signaled( struct object *obj, struct thread *thread );
static int timer_satisfied( struct object *obj, struct thread *thread );
static void timer_destroy( struct object *obj );

static const struct object_ops timer_ops =
{
    sizeof(struct timer),      /* size */
    timer_dump,                /* dump */
    add_queue,                 /* add_queue */
    remove_queue,              /* remove_queue */
    timer_signaled,            /* signaled */
    timer_satisfied,           /* satisfied */
    NULL,                      /* get_poll_events */
    NULL,                      /* poll_event */
    no_read_fd,                /* get_read_fd */
    no_write_fd,               /* get_write_fd */
    no_flush,                  /* flush */
    no_get_file_info,          /* get_file_info */
    timer_destroy              /* destroy */
};


/* create a timer object */
static struct timer *create_timer( const WCHAR *name, size_t len, int manual )
{
    struct timer *timer;

    if ((timer = create_named_object( &timer_ops, name, len )))
    {
        if (get_error() != STATUS_OBJECT_NAME_COLLISION)
        {
            /* initialize it if it didn't already exist */
            timer->manual       = manual;
            timer->signaled     = 0;
            timer->when.tv_sec  = 0;
            timer->when.tv_usec = 0;
            timer->period       = 0;
            timer->timeout      = NULL;
            timer->thread       = NULL;
        }
    }
    return timer;
}

/* callback on timer expiration */
static void timer_callback( void *private )
{
    struct timer *timer = (struct timer *)private;

    /* queue an APC */
    if (timer->thread)
        thread_queue_apc( timer->thread, &timer->obj, timer->callback, APC_TIMER, 3,
                          (void *)timer->when.tv_sec, (void *)timer->when.tv_usec, timer->arg );

    if (timer->period)  /* schedule the next expiration */
    {
        add_timeout( &timer->when, timer->period );
        timer->timeout = add_timeout_user( &timer->when, timer_callback, timer );
    }
    else timer->timeout = NULL;

    /* wake up waiters */
    timer->signaled = 1;
    wake_up( &timer->obj, 0 );
}

/* cancel a running timer */
static void cancel_timer( struct timer *timer )
{
    if (timer->timeout)
    {
        remove_timeout_user( timer->timeout );
        timer->timeout = NULL;
    }
    if (timer->thread)
    {
        thread_cancel_apc( timer->thread, &timer->obj );
        timer->thread = NULL;
    }
}

/* set the timer expiration and period */
static void set_timer( struct timer *timer, int sec, int usec, int period,
                       void *callback, void *arg )
{
    cancel_timer( timer );
    if (timer->manual)
    {
        period = 0;  /* period doesn't make any sense for a manual timer */
        timer->signaled = 0;
    }
    if (!sec && !usec)
    {
        /* special case: use now + period as first expiration */
        gettimeofday( &timer->when, 0 );
        add_timeout( &timer->when, period );
    }
    else
    {
        timer->when.tv_sec  = sec;
        timer->when.tv_usec = usec;
    }
    timer->period       = period;
    timer->callback     = callback;
    timer->arg          = arg;
    if (callback) timer->thread = current;
    timer->timeout = add_timeout_user( &timer->when, timer_callback, timer );
}

static void timer_dump( struct object *obj, int verbose )
{
    struct timer *timer = (struct timer *)obj;
    assert( obj->ops == &timer_ops );
    fprintf( stderr, "Timer manual=%d when=%ld.%06ld period=%d ",
             timer->manual, timer->when.tv_sec, timer->when.tv_usec, timer->period );
    dump_object_name( &timer->obj );
    fputc( '\n', stderr );
}

static int timer_signaled( struct object *obj, struct thread *thread )
{
    struct timer *timer = (struct timer *)obj;
    assert( obj->ops == &timer_ops );
    return timer->signaled;
}

static int timer_satisfied( struct object *obj, struct thread *thread )
{
    struct timer *timer = (struct timer *)obj;
    assert( obj->ops == &timer_ops );
    if (!timer->manual) timer->signaled = 0;
    return 0;
}

static void timer_destroy( struct object *obj )
{
    struct timer *timer = (struct timer *)obj;
    assert( obj->ops == &timer_ops );

    if (timer->timeout) remove_timeout_user( timer->timeout );
}

/* create a timer */
DECL_HANDLER(create_timer)
{
    struct timer *timer;

    req->handle = -1;
    if ((timer = create_timer( get_req_data(req), get_req_data_size(req), req->manual )))
    {
        req->handle = alloc_handle( current->process, timer, TIMER_ALL_ACCESS, req->inherit );
        release_object( timer );
    }
}

/* open a handle to a timer */
DECL_HANDLER(open_timer)
{
    req->handle = open_object( get_req_data(req), get_req_data_size(req),
                               &timer_ops, req->access, req->inherit );
}

/* set a waitable timer */
DECL_HANDLER(set_timer)
{
    struct timer *timer;

    if ((timer = (struct timer *)get_handle_obj( current->process, req->handle,
                                                 TIMER_MODIFY_STATE, &timer_ops )))
    {
        set_timer( timer, req->sec, req->usec, req->period, req->callback, req->arg );
        release_object( timer );
    }
}

/* cancel a waitable timer */
DECL_HANDLER(cancel_timer)
{
    struct timer *timer;

    if ((timer = (struct timer *)get_handle_obj( current->process, req->handle,
                                                 TIMER_MODIFY_STATE, &timer_ops )))
    {
        cancel_timer( timer );
        release_object( timer );
    }
}
