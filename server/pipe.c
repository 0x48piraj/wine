/*
 * Server-side pipe management
 *
 * Copyright (C) 1998 Alexandre Julliard
 */

#include "config.h"

#include <assert.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_SYS_ERRNO_H
#include <sys/errno.h>
#endif
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "winbase.h"

#include "handle.h"
#include "thread.h"
#include "request.h"

enum side { READ_SIDE, WRITE_SIDE };

struct pipe
{
    struct object       obj;         /* object header */
    struct pipe        *other;       /* the pipe other end */
    enum side           side;        /* which side of the pipe is this */
};

static void pipe_dump( struct object *obj, int verbose );
static int pipe_get_poll_events( struct object *obj );
static int pipe_get_fd( struct object *obj );
static int pipe_get_info( struct object *obj, struct get_file_info_reply *reply );
static void pipe_destroy( struct object *obj );

static const struct object_ops pipe_ops =
{
    sizeof(struct pipe),          /* size */
    pipe_dump,                    /* dump */
    default_poll_add_queue,       /* add_queue */
    default_poll_remove_queue,    /* remove_queue */
    default_poll_signaled,        /* signaled */
    no_satisfied,                 /* satisfied */
    pipe_get_poll_events,         /* get_poll_events */
    default_poll_event,           /* poll_event */
    pipe_get_fd,                  /* get_fd */
    no_flush,                     /* flush */
    pipe_get_info,                /* get_file_info */
    pipe_destroy                  /* destroy */
};


static struct pipe *create_pipe_side( int fd, int side )
{
    struct pipe *pipe;

    if ((pipe = alloc_object( &pipe_ops, fd )))
    {
        pipe->other  = NULL;
        pipe->side   = side;
    }
    return pipe;
}

static int create_pipe( struct object *obj[2] )
{
    struct pipe *read_pipe;
    struct pipe *write_pipe;
    int fd[2];

    if (pipe( fd ) == -1)
    {
        file_set_error();
        return 0;
    }
    if ((read_pipe = create_pipe_side( fd[0], READ_SIDE )))
    {
        if ((write_pipe = create_pipe_side( fd[1], WRITE_SIDE )))
        {
            write_pipe->other = read_pipe;
            read_pipe->other  = write_pipe;
            obj[0] = &read_pipe->obj;
            obj[1] = &write_pipe->obj;
            return 1;
        }
        release_object( read_pipe );
    }
    else close( fd[1] );
    return 0;
}

static void pipe_dump( struct object *obj, int verbose )
{
    struct pipe *pipe = (struct pipe *)obj;
    assert( obj->ops == &pipe_ops );
    fprintf( stderr, "Pipe %s-side fd=%d\n",
             (pipe->side == READ_SIDE) ? "read" : "write", pipe->obj.fd );
}

static int pipe_get_poll_events( struct object *obj )
{
    struct pipe *pipe = (struct pipe *)obj;
    assert( obj->ops == &pipe_ops );
    return (pipe->side == READ_SIDE) ? POLLIN : POLLOUT;
}

static int pipe_get_fd( struct object *obj )
{
    struct pipe *pipe = (struct pipe *)obj;
    assert( obj->ops == &pipe_ops );

    if (!pipe->other)
    {
        set_error( STATUS_PIPE_BROKEN );
        return -1;
    }
    return pipe->obj.fd;
}

static int pipe_get_info( struct object *obj, struct get_file_info_reply *reply )
{
    if (reply)
    {
        reply->type        = FILE_TYPE_PIPE;
        reply->attr        = 0;
        reply->access_time = 0;
        reply->write_time  = 0;
        reply->size_high   = 0;
        reply->size_low    = 0;
        reply->links       = 0;
        reply->index_high  = 0;
        reply->index_low   = 0;
        reply->serial      = 0;
    }
    return FD_TYPE_DEFAULT;
}

static void pipe_destroy( struct object *obj )
{
    struct pipe *pipe = (struct pipe *)obj;
    assert( obj->ops == &pipe_ops );

    if (pipe->other) pipe->other->other = NULL;
}

/* create an anonymous pipe */
DECL_HANDLER(create_pipe)
{
    struct object *obj[2];
    handle_t hread = 0, hwrite = 0;

    if (create_pipe( obj ))
    {
        hread = alloc_handle( current->process, obj[0],
                              STANDARD_RIGHTS_REQUIRED|SYNCHRONIZE|GENERIC_READ,
                              req->inherit );
        if (hread)
        {
            hwrite = alloc_handle( current->process, obj[1],
                                   STANDARD_RIGHTS_REQUIRED|SYNCHRONIZE|GENERIC_WRITE,
                                   req->inherit );
            if (!hwrite) close_handle( current->process, hread, NULL );
        }
        release_object( obj[0] );
        release_object( obj[1] );
    }
    reply->handle_read  = hread;
    reply->handle_write = hwrite;
}
