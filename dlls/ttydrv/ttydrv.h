/*
 * TTY driver definitions
 *
 * Copyright 1998 Patrik Stridvall
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

#ifndef __WINE_TTYDRV_H
#define __WINE_TTYDRV_H

#ifndef __WINE_CONFIG_H 
# error You must include config.h to use this header 
#endif 

#undef ERR
#ifdef HAVE_NCURSES_H
# include <ncurses.h>
#elif defined(HAVE_CURSES_H)
# include <curses.h>
#endif

#include "windef.h"
#include "wingdi.h"
#include "wine/winuser16.h"
#include "wine/wingdi16.h"
#include "user.h"

struct tagBITMAPOBJ;
struct tagCLASS;
struct tagDESKTOP;
struct tagPALETTEOBJ;
struct tagWND;
struct tagCURSORICONINFO;
struct tagCREATESTRUCTA;
struct tagWINDOWPOS;

#if defined(HAVE_LIBCURSES) || defined(HAVE_LIBNCURSES)
#define WINE_CURSES
#endif

/**************************************************************************
 * TTY GDI driver
 */

#ifndef WINE_CURSES
typedef struct { int dummy; } WINDOW;
#endif

typedef struct {
  HDC hdc;
  DC *dc;
  WINDOW *window;
  int cellWidth;
  int cellHeight;
} TTYDRV_PDEVICE;

typedef struct {
  int dummy; /* FIXME: Remove later */
} TTYDRV_PHYSBITMAP;

extern BOOL TTYDRV_GDI_Initialize(void);

/* TTY GDI bitmap driver */

extern HBITMAP TTYDRV_BITMAP_CreateDIBSection(TTYDRV_PDEVICE *physDev, BITMAPINFO *bmi, UINT usage, LPVOID *bits, HANDLE section, DWORD offset);
extern void TTYDRV_BITMAP_DeleteDIBSection(struct tagBITMAPOBJ *bmp);

extern BOOL TTYDRV_DC_CreateBitmap(HBITMAP hbitmap);

extern BOOL TTYDRV_DC_Arc(TTYDRV_PDEVICE *physDev, INT left, INT top, INT right, INT bottom, INT xstart, INT ystart, INT xend, INT yend);
extern LONG TTYDRV_DC_BitmapBits(HBITMAP hbitmap, void *bits, LONG count, WORD flags);
extern BOOL TTYDRV_DC_CreateBitmap(HBITMAP hbitmap);
extern BOOL TTYDRV_DC_CreateDC(DC *dc, LPCSTR driver, LPCSTR device, LPCSTR output, const DEVMODEA *initData);
extern BOOL TTYDRV_DC_DeleteDC(TTYDRV_PDEVICE *physDev);
extern BOOL TTYDRV_DC_DeleteObject(HGDIOBJ handle);
extern BOOL TTYDRV_DC_BitBlt(TTYDRV_PDEVICE *physDevDst, INT xDst, INT yDst, INT width, INT height, TTYDRV_PDEVICE *physDevSrc, INT xSrc, INT ySrc, DWORD rop);
extern BOOL TTYDRV_DC_Chord(TTYDRV_PDEVICE *physDev, INT left, INT top, INT right, INT bottom, INT xstart, INT ystart, INT xend, INT yend);
extern BOOL TTYDRV_DC_Ellipse(TTYDRV_PDEVICE *physDev, INT left, INT top, INT right, INT bottom);
extern INT TTYDRV_DC_Escape(TTYDRV_PDEVICE *physDev, INT nEscape, INT cbInput, SEGPTR lpInData, SEGPTR lpOutData);
extern BOOL TTYDRV_DC_ExtFloodFill(TTYDRV_PDEVICE *physDev, INT x, INT y, COLORREF color, UINT fillType);
extern BOOL TTYDRV_DC_ExtTextOut(TTYDRV_PDEVICE *physDev, INT x, INT y, UINT flags, const RECT *lpRect, LPCWSTR str, UINT count, const INT *lpDx);
extern BOOL TTYDRV_DC_GetCharWidth(TTYDRV_PDEVICE *physDev, UINT firstChar, UINT lastChar, LPINT buffer);
extern COLORREF TTYDRV_DC_GetPixel(TTYDRV_PDEVICE *physDev, INT x, INT y);

extern BOOL TTYDRV_DC_GetTextExtentPoint(TTYDRV_PDEVICE *physDev, LPCWSTR str, INT count, LPSIZE size);
extern BOOL TTYDRV_DC_GetTextMetrics(TTYDRV_PDEVICE *physDev, TEXTMETRICW *metrics);
extern BOOL TTYDRV_DC_LineTo(TTYDRV_PDEVICE *physDev, INT x, INT y);
extern BOOL TTYDRV_DC_PaintRgn(TTYDRV_PDEVICE *physDev, HRGN hrgn);
extern BOOL TTYDRV_DC_PatBlt(TTYDRV_PDEVICE *physDev, INT left, INT top, INT width, INT height, DWORD rop);
extern BOOL TTYDRV_DC_Pie(TTYDRV_PDEVICE *physDev, INT left, INT top, INT right, INT bottom, INT xstart, INT ystart, INT xend, INT yend);
extern BOOL TTYDRV_DC_Polygon(TTYDRV_PDEVICE *physDev, const POINT* pt, INT count);
extern BOOL TTYDRV_DC_Polyline(TTYDRV_PDEVICE *physDev, const POINT* pt, INT count);
extern BOOL TTYDRV_DC_PolyPolygon(TTYDRV_PDEVICE *physDev, const POINT* pt, const INT* counts, UINT polygons);
extern BOOL TTYDRV_DC_PolyPolyline(TTYDRV_PDEVICE *physDev, const POINT* pt, const DWORD* counts, DWORD polylines);
extern BOOL TTYDRV_DC_Rectangle(TTYDRV_PDEVICE *physDev, INT left, INT top, INT right, INT bottom);
extern BOOL TTYDRV_DC_RoundRect(TTYDRV_PDEVICE *physDev, INT left, INT top, INT right, INT bottom, INT ell_width, INT ell_height);
extern COLORREF TTYDRV_DC_SetPixel(TTYDRV_PDEVICE *physDev, INT x, INT y, COLORREF color);
extern BOOL TTYDRV_DC_StretchBlt(TTYDRV_PDEVICE *physDevDst, INT xDst, INT yDst, INT widthDst, INT heightDst, TTYDRV_PDEVICE *physDevSrc, INT xSrc, INT ySrc, INT widthSrc, INT heightSrc, DWORD rop);
INT TTYDRV_DC_SetDIBitsToDevice(TTYDRV_PDEVICE *physDev, INT xDest, INT yDest, DWORD cx, DWORD cy, INT xSrc, INT ySrc, UINT startscan, UINT lines, LPCVOID bits, const BITMAPINFO *info, UINT coloruse);

/* TTY GDI palette driver */

extern struct tagPALETTE_DRIVER TTYDRV_PALETTE_Driver;

extern BOOL TTYDRV_PALETTE_Initialize(void);
extern int TTYDRV_PALETTE_SetMapping(struct tagPALETTEOBJ *palPtr, UINT uStart, UINT uNum, BOOL mapOnly);
extern int TTYDRV_PALETTE_UpdateMapping(struct tagPALETTEOBJ *palPtr);
extern BOOL TTYDRV_PALETTE_IsDark(int pixel);

/**************************************************************************
 * TTY USER driver
 */

extern int cell_width;
extern int cell_height;
extern int screen_rows;
extern int screen_cols;
extern WINDOW *root_window;

#endif /* !defined(__WINE_TTYDRV_H) */
