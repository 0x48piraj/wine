/*
 * Copyright 1993 Alexandre Julliard
 * Copyright 1996 Alex Korobka
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

#ifndef __WINE_COLOR_H
#define __WINE_COLOR_H

#include "windef.h"
#include "wingdi.h"
#include "palette.h"

#define PC_SYS_USED     0x80		/* palentry is used (both system and logical) */
#define PC_SYS_RESERVED 0x40		/* system palentry is not to be mapped to */
#define PC_SYS_MAPPED   0x10		/* logical palentry is a direct alias for system palentry */

extern BOOL  COLOR_IsSolid(COLORREF color);

extern COLORREF            COLOR_GetSystemPaletteEntry(UINT);
extern const PALETTEENTRY *COLOR_GetSystemPaletteTemplate(void);

extern COLORREF	COLOR_LookupNearestColor(PALETTEENTRY *, int, COLORREF);
extern int      COLOR_PaletteLookupExactIndex(PALETTEENTRY *palPalEntry, int size, COLORREF col);
extern int      COLOR_PaletteLookupPixel(PALETTEENTRY *, int, int * , COLORREF, BOOL);

#endif /* __WINE_COLOR_H */
