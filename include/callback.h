/*
 * Callback functions
 *
 * Copyright 1995 Alexandre Julliard
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

#ifndef __WINE_CALLBACK_H
#define __WINE_CALLBACK_H

#include "windef.h"
#include "winnt.h"

typedef struct {
    void (WINAPI *LoadDosExe)( LPCSTR filename, HANDLE hFile );

    /* DPMI functions */
    void (WINAPI *CallRMInt)( CONTEXT86 *context );
    void (WINAPI *CallRMProc)( CONTEXT86 *context, int iret );
    void (WINAPI *AllocRMCB)( CONTEXT86 *context );
    void (WINAPI *FreeRMCB)( CONTEXT86 *context );

    /* I/O functions */
    void (WINAPI *SetTimer)( unsigned ticks );
    unsigned (WINAPI *GetTimer)( void );
    BOOL (WINAPI *inport)( int port, int size, DWORD *res );
    BOOL (WINAPI *outport)( int port, int size, DWORD val );
    void (WINAPI *ASPIHandler)( CONTEXT86 *context );
} DOSVM_TABLE;

extern DOSVM_TABLE Dosvm;

#endif /* __WINE_CALLBACK_H */
