/*
 * Global heap declarations
 *
 * Copyright 1995 Alexandre Julliard
 */

#ifndef __WINE_GLOBAL_H
#define __WINE_GLOBAL_H

#include "windef.h"

/* memory/global.c */
extern HGLOBAL16 GLOBAL_CreateBlock( UINT16 flags, const void *ptr, DWORD size,
                                     HGLOBAL16 hOwner, BOOL16 isCode,
                                     BOOL16 is32Bit, BOOL16 isReadOnly);
extern BOOL16 GLOBAL_FreeBlock( HGLOBAL16 handle );
extern BOOL16 GLOBAL_MoveBlock( HGLOBAL16 handle, const void *ptr, DWORD size );
extern HGLOBAL16 GLOBAL_Alloc( WORD flags, DWORD size, HGLOBAL16 hOwner,
                               BOOL16 isCode, BOOL16 is32Bit,
                               BOOL16 isReadOnly );

/* memory/virtual.c */
extern DWORD VIRTUAL_GetPageSize(void);
extern DWORD VIRTUAL_GetGranularity(void);
extern LPVOID VIRTUAL_MapFileW( LPCWSTR name );

typedef BOOL (*HANDLERPROC)(LPVOID, LPCVOID);
extern BOOL VIRTUAL_SetFaultHandler(LPCVOID addr, HANDLERPROC proc, LPVOID arg);
extern DWORD VIRTUAL_HandleFault(LPCVOID addr);

/* memory/atom.c */
extern BOOL ATOM_Init( WORD globalTableSel );

#endif  /* __WINE_GLOBAL_H */
