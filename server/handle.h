/*
 * Server-side handle definitions
 *
 * Copyright (C) 1999 Alexandre Julliard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __WINE_SERVER_HANDLE_H
#define __WINE_SERVER_HANDLE_H

#include <stdlib.h>
#include "windef.h"
#include "wine/server_protocol.h"

struct process;
struct object_ops;

/* handle functions */

/* alloc_handle takes a void *obj for convenience, but you better make sure */
/* that the thing pointed to starts with a struct object... */
extern handle_t alloc_handle( struct process *process, void *obj,
                              unsigned int access, int inherit );
extern int close_handle( struct process *process, handle_t handle, int *fd );
extern struct object *get_handle_obj( struct process *process, handle_t handle,
                                      unsigned int access, const struct object_ops *ops );
extern int get_handle_fd( struct process *process, handle_t handle, unsigned int access );
extern handle_t duplicate_handle( struct process *src, handle_t src_handle, struct process *dst,
                                  unsigned int access, int inherit, int options );
extern handle_t open_object( const WCHAR *name, size_t len, const struct object_ops *ops,
                             unsigned int access, int inherit );
extern struct object *alloc_handle_table( struct process *process, int count );
extern struct object *copy_handle_table( struct process *process, struct process *parent );
extern void close_global_handles(void);

#endif  /* __WINE_SERVER_HANDLE_H */
