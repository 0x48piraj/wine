/* Typedefs for extensions loading

     Copyright (c) 2000 Lionel Ulmer
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
#ifndef __DLLS_OPENGL32_OPENGL_EXT_H
#define __DLLS_OPENGL32_OPENGL_EXT_H

typedef struct {
  char  *name;     /* name of the extension */
  char  *glx_name; /* name used on Unix's libGL */
  void  *func;     /* pointer to the Wine function for this extension */
  void **func_ptr; /* where to store the value of glXGetProcAddressARB */
} OpenGL_extension;

extern OpenGL_extension extension_registry[];
extern int extension_registry_size;

#endif /* __DLLS_OPENGL32_OPENGL_EXT_H */
