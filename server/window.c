/*
 * Server-side window handling
 *
 * Copyright (C) 2001 Alexandre Julliard
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

#include "config.h"
#include "wine/port.h"

#include <assert.h>

#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"

#include "object.h"
#include "request.h"
#include "thread.h"
#include "process.h"
#include "user.h"
#include "unicode.h"

/* a window property */
struct property
{
    unsigned short type;     /* property type (see below) */
    atom_t         atom;     /* property atom */
    handle_t       handle;   /* property handle (user-defined storage) */
};

enum property_type
{
    PROP_TYPE_FREE,   /* free entry */
    PROP_TYPE_STRING, /* atom that was originally a string */
    PROP_TYPE_ATOM    /* plain atom */
};


struct window
{
    struct window   *parent;          /* parent window */
    struct window   *owner;           /* owner of this window */
    struct window   *first_child;     /* first child in Z-order */
    struct window   *last_child;      /* last child in Z-order */
    struct window   *first_unlinked;  /* first child not linked in the Z-order list */
    struct window   *next;            /* next window in Z-order */
    struct window   *prev;            /* prev window in Z-order */
    user_handle_t    handle;          /* full handle for this window */
    struct thread   *thread;          /* thread owning the window */
    atom_t           atom;            /* class atom */
    rectangle_t      window_rect;     /* window rectangle */
    rectangle_t      client_rect;     /* client rectangle */
    unsigned int     style;           /* window style */
    unsigned int     ex_style;        /* window extended style */
    unsigned int     id;              /* window id */
    void*            instance;        /* creator instance */
    void*            user_data;       /* user-specific data */
    WCHAR           *text;            /* window caption text */
    int              paint_count;     /* count of pending paints for this window */
    int              prop_inuse;      /* number of in-use window properties */
    int              prop_alloc;      /* number of allocated window properties */
    struct property *properties;      /* window properties array */
};

static struct window *top_window;  /* top-level (desktop) window */


/* retrieve a pointer to a window from its handle */
inline static struct window *get_window( user_handle_t handle )
{
    struct window *ret = get_user_object( handle, USER_WINDOW );
    if (!ret) set_error( STATUS_INVALID_HANDLE );
    return ret;
}

/* unlink a window from the tree */
static void unlink_window( struct window *win )
{
    struct window *parent = win->parent;

    assert( parent );

    if (win->next) win->next->prev = win->prev;
    else if (parent->last_child == win) parent->last_child = win->prev;

    if (win->prev) win->prev->next = win->next;
    else if (parent->first_child == win) parent->first_child = win->next;
    else if (parent->first_unlinked == win) parent->first_unlinked = win->next;
}


/* link a window into the tree (or unlink it if the new parent is NULL)  */
static void link_window( struct window *win, struct window *parent, struct window *previous )
{
    unlink_window( win );  /* unlink it from the previous location */

    if (parent)
    {
        if (win->parent != parent)
        {
            win->owner = NULL;  /* reset owner if changing parent */
            win->parent = parent;
        }
        if ((win->prev = previous))
        {
            if ((win->next = previous->next)) win->next->prev = win;
            else if (win->parent->last_child == previous) win->parent->last_child = win;
            win->prev->next = win;
        }
        else
        {
            if ((win->next = parent->first_child)) win->next->prev = win;
            else win->parent->last_child = win;
            parent->first_child = win;
        }
    }
    else  /* move it to parent unlinked list */
    {
        parent = win->parent;
        if ((win->next = parent->first_unlinked)) win->next->prev = win;
        win->prev = NULL;
        parent->first_unlinked = win;
    }
}

/* set a window property */
static void set_property( struct window *win, atom_t atom, handle_t handle,
                          enum property_type type )
{
    int i, free = -1;
    struct property *new_props;

    /* check if it exists already */
    for (i = 0; i < win->prop_inuse; i++)
    {
        if (win->properties[i].type == PROP_TYPE_FREE)
        {
            free = i;
            continue;
        }
        if (win->properties[i].atom == atom)
        {
            win->properties[i].type = type;
            win->properties[i].handle = handle;
            return;
        }
    }

    /* need to add an entry */
    if (!grab_global_atom( atom )) return;
    if (free == -1)
    {
        /* no free entry */
        if (win->prop_inuse >= win->prop_alloc)
        {
            /* need to grow the array */
            if (!(new_props = realloc( win->properties,
                                       sizeof(*new_props) * (win->prop_alloc + 16) )))
            {
                set_error( STATUS_NO_MEMORY );
                release_global_atom( atom );
                return;
            }
            win->prop_alloc += 16;
            win->properties = new_props;
        }
        free = win->prop_inuse++;
    }
    win->properties[free].atom   = atom;
    win->properties[free].type   = type;
    win->properties[free].handle = handle;
}

/* remove a window property */
static handle_t remove_property( struct window *win, atom_t atom )
{
    int i;

    for (i = 0; i < win->prop_inuse; i++)
    {
        if (win->properties[i].type == PROP_TYPE_FREE) continue;
        if (win->properties[i].atom == atom)
        {
            release_global_atom( atom );
            win->properties[i].type = PROP_TYPE_FREE;
            return win->properties[i].handle;
        }
    }
    /* FIXME: last error? */
    return 0;
}

/* find a window property */
static handle_t get_property( struct window *win, atom_t atom )
{
    int i;

    for (i = 0; i < win->prop_inuse; i++)
    {
        if (win->properties[i].type == PROP_TYPE_FREE) continue;
        if (win->properties[i].atom == atom) return win->properties[i].handle;
    }
    /* FIXME: last error? */
    return 0;
}

/* destroy all properties of a window */
inline static void destroy_properties( struct window *win )
{
    int i;

    if (!win->properties) return;
    for (i = 0; i < win->prop_inuse; i++)
    {
        if (win->properties[i].type == PROP_TYPE_FREE) continue;
        release_global_atom( win->properties[i].atom );
    }
    free( win->properties );
}

/* destroy a window */
static void destroy_window( struct window *win )
{
    assert( win != top_window );

    /* destroy all children */
    while (win->first_child) destroy_window( win->first_child );
    while (win->first_unlinked) destroy_window( win->first_unlinked );

    /* reset siblings owner */
    if (win->parent)
    {
        struct window *ptr;
        for (ptr = win->parent->first_child; ptr; ptr = ptr->next)
            if (ptr->owner == win) ptr->owner = NULL;
        for (ptr = win->parent->first_unlinked; ptr; ptr = ptr->next)
            if (ptr->owner == win) ptr->owner = NULL;
    }

    if (win->thread->queue)
    {
        if (win->paint_count) inc_queue_paint_count( win->thread, -win->paint_count );
        queue_cleanup_window( win->thread, win->handle );
    }
    free_user_handle( win->handle );
    destroy_properties( win );
    unlink_window( win );
    if (win->text) free( win->text );
    memset( win, 0x55, sizeof(*win) );
    free( win );
}

/* create a new window structure (note: the window is not linked in the window tree) */
static struct window *create_window( struct window *parent, struct window *owner, atom_t atom )
{
    struct window *win = mem_alloc( sizeof(*win) );
    if (!win) return NULL;

    if (!(win->handle = alloc_user_handle( win, USER_WINDOW )))
    {
        free( win );
        return NULL;
    }
    win->parent         = parent;
    win->owner          = owner;
    win->first_child    = NULL;
    win->last_child     = NULL;
    win->first_unlinked = NULL;
    win->thread         = current;
    win->atom           = atom;
    win->style          = 0;
    win->ex_style       = 0;
    win->id             = 0;
    win->instance       = NULL;
    win->user_data      = NULL;
    win->text           = NULL;
    win->paint_count    = 0;
    win->prop_inuse     = 0;
    win->prop_alloc     = 0;
    win->properties     = NULL;

    if (parent)  /* put it on parent unlinked list */
    {
        if ((win->next = parent->first_unlinked)) win->next->prev = win;
        win->prev = NULL;
        parent->first_unlinked = win;
    }
    else win->next = win->prev = NULL;

    return win;
}

/* destroy all windows belonging to a given thread */
void destroy_thread_windows( struct thread *thread )
{
    user_handle_t handle = 0;
    struct window *win;

    while ((win = next_user_handle( &handle, USER_WINDOW )))
    {
        if (win->thread != thread) continue;
        destroy_window( win );
    }
}

/* check whether child is a descendant of parent */
int is_child_window( user_handle_t parent, user_handle_t child )
{
    struct window *child_ptr = get_user_object( child, USER_WINDOW );
    struct window *parent_ptr = get_user_object( parent, USER_WINDOW );

    if (!child_ptr || !parent_ptr) return 0;
    while (child_ptr->parent)
    {
        if (child_ptr->parent == parent_ptr) return 1;
        child_ptr = child_ptr->parent;
    }
    return 0;
}

/* return the thread owning a window */
struct thread *get_window_thread( user_handle_t handle )
{
    struct window *win = get_user_object( handle, USER_WINDOW );
    if (!win || !win->thread) return NULL;
    return (struct thread *)grab_object( win->thread );
}

/* find a child of the specified window that needs repainting */
static struct window *find_child_to_repaint( struct window *parent, struct thread *thread )
{
    struct window *ptr, *ret = NULL;

    for (ptr = parent->first_child; ptr && !ret; ptr = ptr->next)
    {
        if (!(ptr->style & WS_VISIBLE)) continue;
        if (ptr->paint_count && ptr->thread == thread)
            ret = ptr;
        else /* explore its children */
            ret = find_child_to_repaint( ptr, thread );
    }

    if (ret && (ret->ex_style & WS_EX_TRANSPARENT))
    {
        /* transparent window, check for non-transparent sibling to paint first */
        for (ptr = ret->next; ptr; ptr = ptr->next)
        {
            if (!(ptr->style & WS_VISIBLE)) continue;
            if (ptr->ex_style & WS_EX_TRANSPARENT) continue;
            if (ptr->paint_count && ptr->thread == thread) return ptr;
        }
    }
    return ret;
}


/* find a window that needs repainting */
user_handle_t find_window_to_repaint( user_handle_t parent, struct thread *thread )
{
    struct window *win = parent ? get_window( parent ) : top_window;

    if (!win || !(win->style & WS_VISIBLE)) return 0;
    if (!win->paint_count || win->thread != thread)
        win = find_child_to_repaint( win, thread );
    return win ? win->handle : 0;
}


/* create a window */
DECL_HANDLER(create_window)
{
    reply->handle = 0;
    if (!req->parent)  /* return desktop window */
    {
        if (!top_window)
        {
            if (!(top_window = create_window( NULL, NULL, req->atom ))) return;
            top_window->thread = NULL;  /* no thread owns the desktop */
        }
        reply->handle = top_window->handle;
    }
    else
    {
        struct window *win, *parent, *owner = NULL;

        if (!(parent = get_window( req->parent ))) return;
        if (req->owner && !(owner = get_window( req->owner ))) return;
        if (owner == top_window) owner = NULL;
        else if (owner && owner->parent != parent)
        {
            /* owner must be a sibling of the new window */
            set_error( STATUS_ACCESS_DENIED );
            return;
        }
        if (!(win = create_window( parent, owner, req->atom ))) return;
        reply->handle = win->handle;
    }
}


/* link a window into the tree */
DECL_HANDLER(link_window)
{
    struct window *win, *parent = NULL, *previous = NULL;

    if (!(win = get_window( req->handle ))) return;
    if (req->parent && !(parent = get_window( req->parent ))) return;

    if (win == top_window)
    {
        set_error( STATUS_INVALID_PARAMETER );
        return;
    }
    reply->full_parent = parent ? parent->handle : 0;
    if (parent && req->previous)
    {
        if (req->previous == (user_handle_t)1)  /* special case: HWND_BOTTOM */
        {
            previous = parent->last_child;
            if (previous == win) return;  /* nothing to do */
        }
        else
        {
            if (!(previous = get_window( req->previous ))) return;
            /* previous must be a child of parent, and not win itself */
            if (previous->parent != parent || previous == win)
            {
                set_error( STATUS_INVALID_PARAMETER );
                return;
            }
        }
    }
    link_window( win, parent, previous );
}


/* destroy a window */
DECL_HANDLER(destroy_window)
{
    struct window *win = get_window( req->handle );
    if (win)
    {
        if (win != top_window) destroy_window( win );
        else set_error( STATUS_ACCESS_DENIED );
    }
}


/* set a window owner */
DECL_HANDLER(set_window_owner)
{
    struct window *win = get_window( req->handle );
    struct window *owner = get_window( req->owner );

    if (!win || !owner) return;
    if (owner->parent != win->parent)
    {
        /* owner has to be a sibling of window */
        set_error( STATUS_ACCESS_DENIED );
        return;
    }
    win->owner = owner;
    reply->full_owner = owner->handle;
}


/* get information from a window handle */
DECL_HANDLER(get_window_info)
{
    struct window *win = get_window( req->handle );

    reply->full_handle = 0;
    reply->tid = reply->pid = 0;
    if (win)
    {
        reply->full_handle = win->handle;
        if (win->thread)
        {
            reply->tid  = get_thread_id( win->thread );
            reply->pid  = get_process_id( win->thread->process );
            reply->atom = win->atom;
        }
    }
}


/* set some information in a window */
DECL_HANDLER(set_window_info)
{
    struct window *win = get_window( req->handle );
    if (!win) return;
    reply->old_style     = win->style;
    reply->old_ex_style  = win->ex_style;
    reply->old_id        = win->id;
    reply->old_instance  = win->instance;
    reply->old_user_data = win->user_data;
    if (req->flags & SET_WIN_STYLE) win->style = req->style;
    if (req->flags & SET_WIN_EXSTYLE) win->ex_style = req->ex_style;
    if (req->flags & SET_WIN_ID) win->id = req->id;
    if (req->flags & SET_WIN_INSTANCE) win->instance = req->instance;
    if (req->flags & SET_WIN_USERDATA) win->user_data = req->user_data;
}


/* get a list of the window parents, up to the root of the tree */
DECL_HANDLER(get_window_parents)
{
    struct window *ptr, *win = get_window( req->handle );
    int total = 0;
    user_handle_t *data;
    size_t len;

    if (win) for (ptr = win->parent; ptr; ptr = ptr->parent) total++;

    reply->count = total;
    len = min( get_reply_max_size(), total * sizeof(user_handle_t) );
    if (len && ((data = set_reply_data_size( len ))))
    {
        for (ptr = win->parent; ptr && len; ptr = ptr->parent, len -= sizeof(*data))
            *data++ = ptr->handle;
    }
}


/* get a list of the window children */
DECL_HANDLER(get_window_children)
{
    struct window *ptr, *parent = get_window( req->parent );
    int total = 0;
    user_handle_t *data;
    size_t len;

    if (parent)
        for (ptr = parent->first_child, total = 0; ptr; ptr = ptr->next)
        {
            if (req->atom && ptr->atom != req->atom) continue;
            if (req->tid && get_thread_id(ptr->thread) != req->tid) continue;
            total++;
        }

    reply->count = total;
    len = min( get_reply_max_size(), total * sizeof(user_handle_t) );
    if (len && ((data = set_reply_data_size( len ))))
    {
        for (ptr = parent->first_child; ptr && len; ptr = ptr->next)
        {
            if (req->atom && ptr->atom != req->atom) continue;
            if (req->tid && get_thread_id(ptr->thread) != req->tid) continue;
            *data++ = ptr->handle;
            len -= sizeof(*data);
        }
    }
}


/* get window tree information from a window handle */
DECL_HANDLER(get_window_tree)
{
    struct window *win = get_window( req->handle );

    if (!win) return;

    if (win->parent)
    {
        struct window *parent = win->parent;
        reply->parent        = parent->handle;
        reply->owner         = win->owner ? win->owner->handle : 0;
        reply->next_sibling  = win->next ? win->next->handle : 0;
        reply->prev_sibling  = win->prev ? win->prev->handle : 0;
        reply->first_sibling = parent->first_child ? parent->first_child->handle : 0;
        reply->last_sibling  = parent->last_child ? parent->last_child->handle : 0;
    }
    else
    {
        reply->parent        = 0;
        reply->owner         = 0;
        reply->next_sibling  = 0;
        reply->prev_sibling  = 0;
        reply->first_sibling = 0;
        reply->last_sibling  = 0;
    }
    reply->first_child = win->first_child ? win->first_child->handle : 0;
    reply->last_child  = win->last_child ? win->last_child->handle : 0;
}


/* set the window and client rectangles of a window */
DECL_HANDLER(set_window_rectangles)
{
    struct window *win = get_window( req->handle );

    if (win)
    {
        win->window_rect = req->window;
        win->client_rect = req->client;
    }
}


/* get the window and client rectangles of a window */
DECL_HANDLER(get_window_rectangles)
{
    struct window *win = get_window( req->handle );

    if (win)
    {
        reply->window = win->window_rect;
        reply->client = win->client_rect;
    }
}


/* get the window text */
DECL_HANDLER(get_window_text)
{
    struct window *win = get_window( req->handle );

    if (win && win->text)
    {
        size_t len = strlenW( win->text ) * sizeof(WCHAR);
        if (len > get_reply_max_size()) len = get_reply_max_size();
        set_reply_data( win->text, len );
    }
}


/* set the window text */
DECL_HANDLER(set_window_text)
{
    struct window *win = get_window( req->handle );

    if (win)
    {
        WCHAR *text = NULL;
        size_t len = get_req_data_size() / sizeof(WCHAR);
        if (len)
        {
            if (!(text = mem_alloc( (len+1) * sizeof(WCHAR) ))) return;
            memcpy( text, get_req_data(), len * sizeof(WCHAR) );
            text[len] = 0;
        }
        if (win->text) free( win->text );
        win->text = text;
    }
}


/* increment the window paint count */
DECL_HANDLER(inc_window_paint_count)
{
    struct window *win = get_window( req->handle );

    if (win && win->thread)
    {
        int old = win->paint_count;
        if ((win->paint_count += req->incr) < 0) win->paint_count = 0;
        inc_queue_paint_count( win->thread, win->paint_count - old );
    }
}


/* get the coordinates offset between two windows */
DECL_HANDLER(get_windows_offset)
{
    struct window *win;

    reply->x = reply->y = 0;
    if (req->from)
    {
        if (!(win = get_window( req->from ))) return;
        while (win)
        {
            reply->x += win->client_rect.left;
            reply->y += win->client_rect.top;
            win = win->parent;
        }
    }
    if (req->to)
    {
        if (!(win = get_window( req->to ))) return;
        while (win)
        {
            reply->x -= win->client_rect.left;
            reply->y -= win->client_rect.top;
            win = win->parent;
        }
    }
}


/* set a window property */
DECL_HANDLER(set_window_property)
{
    struct window *win = get_window( req->window );

    if (win) set_property( win, req->atom, req->handle,
                           req->string ? PROP_TYPE_STRING : PROP_TYPE_ATOM );
}


/* remove a window property */
DECL_HANDLER(remove_window_property)
{
    struct window *win = get_window( req->window );
    reply->handle = 0;
    if (win) reply->handle = remove_property( win, req->atom );
}


/* get a window property */
DECL_HANDLER(get_window_property)
{
    struct window *win = get_window( req->window );
    reply->handle = 0;
    if (win) reply->handle = get_property( win, req->atom );
}


/* get the list of properties of a window */
DECL_HANDLER(get_window_properties)
{
    property_data_t *data;
    int i, count, max = get_reply_max_size() / sizeof(*data);
    struct window *win = get_window( req->window );

    reply->total = 0;
    if (!win) return;

    for (i = count = 0; i < win->prop_inuse; i++)
        if (win->properties[i].type != PROP_TYPE_FREE) count++;
    reply->total = count;

    if (count > max) count = max;
    if (!count || !(data = set_reply_data_size( count * sizeof(*data) ))) return;

    for (i = 0; i < win->prop_inuse && count; i++)
    {
        if (win->properties[i].type == PROP_TYPE_FREE) continue;
        data->atom   = win->properties[i].atom;
        data->string = (win->properties[i].type == PROP_TYPE_STRING);
        data->handle = win->properties[i].handle;
        data++;
        count--;
    }
}
