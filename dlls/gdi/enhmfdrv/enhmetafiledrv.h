/*
 * Enhanced MetaFile driver definitions
 *
 * Copyright 1999 Huw D M Davies
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

#ifndef __WINE_ENHMETAFILEDRV_H
#define __WINE_ENHMETAFILEDRV_H

#include "windef.h"
#include "wingdi.h"
#include "gdi.h"

/* Enhanced Metafile driver physical DC */

typedef struct
{
    HDC             hdc;
    DC             *dc;
    ENHMETAHEADER  *emh;           /* Pointer to enhanced metafile header */
    UINT       nextHandle;         /* Next handle number */
    HFILE      hFile;              /* HFILE for disk based MetaFile */ 
} EMFDRV_PDEVICE;


extern BOOL EMFDRV_WriteRecord( PHYSDEV dev, EMR *emr );
extern int EMFDRV_AddHandleDC( PHYSDEV dev );
extern void EMFDRV_UpdateBBox( PHYSDEV dev, RECTL *rect );
extern DWORD EMFDRV_CreateBrushIndirect( PHYSDEV dev, HBRUSH hBrush );

/* Metafile driver functions */
extern BOOL     EMFDRV_AbortPath( PHYSDEV dev );
extern BOOL     EMFDRV_Arc( PHYSDEV dev, INT left, INT top, INT right,
                            INT bottom, INT xstart, INT ystart, INT xend,
                            INT yend );
extern BOOL     EMFDRV_BeginPath( PHYSDEV dev );
extern BOOL     EMFDRV_BitBlt( PHYSDEV devDst, INT xDst, INT yDst,
                               INT width, INT height, PHYSDEV devSrc,
                               INT xSrc, INT ySrc, DWORD rop );
extern BOOL     EMFDRV_Chord( PHYSDEV dev, INT left, INT top, INT right,
                              INT bottom, INT xstart, INT ystart, INT xend,
                              INT yend );
extern BOOL     EMFDRV_CloseFigure( PHYSDEV dev );
extern BOOL     EMFDRV_Ellipse( PHYSDEV dev, INT left, INT top,
                                INT right, INT bottom );
extern BOOL     EMFDRV_EndPath( PHYSDEV dev );
extern INT      EMFDRV_ExcludeClipRect( PHYSDEV dev, INT left, INT top, INT right,
                                        INT bottom );
extern BOOL     EMFDRV_ExtFloodFill( PHYSDEV dev, INT x, INT y,
                                     COLORREF color, UINT fillType );
extern BOOL     EMFDRV_ExtTextOut( PHYSDEV dev, INT x, INT y,
                                   UINT flags, const RECT *lprect, LPCSTR str,
                                   UINT count, const INT *lpDx );
extern BOOL     EMFDRV_FillPath( PHYSDEV dev );
extern BOOL     EMFDRV_FillRgn( PHYSDEV dev, HRGN hrgn, HBRUSH hbrush );
extern BOOL     EMFDRV_FlattenPath( PHYSDEV dev );
extern BOOL     EMFDRV_FrameRgn( PHYSDEV dev, HRGN hrgn, HBRUSH hbrush, INT width,
                                 INT height );
extern INT      EMFDRV_IntersectClipRect( PHYSDEV dev, INT left, INT top, INT right,
                                          INT bottom );
extern BOOL     EMFDRV_InvertRgn( PHYSDEV dev, HRGN hrgn );
extern BOOL     EMFDRV_LineTo( PHYSDEV dev, INT x, INT y );
extern BOOL     EMFDRV_MoveTo( PHYSDEV dev, INT x, INT y );
extern INT      EMFDRV_OffsetClipRgn( PHYSDEV dev, INT x, INT y );
extern BOOL     EMFDRV_OffsetViewportOrg( PHYSDEV dev, INT x, INT y );
extern BOOL     EMFDRV_OffsetWindowOrg( PHYSDEV dev, INT x, INT y );
extern BOOL     EMFDRV_PaintRgn( PHYSDEV dev, HRGN hrgn );
extern BOOL     EMFDRV_PatBlt( PHYSDEV dev, INT left, INT top,
                               INT width, INT height, DWORD rop );
extern BOOL     EMFDRV_Pie( PHYSDEV dev, INT left, INT top, INT right,
                            INT bottom, INT xstart, INT ystart, INT xend,
                            INT yend );
extern BOOL     EMFDRV_PolyPolygon( PHYSDEV dev, const POINT* pt,
                                    const INT* counts, UINT polys);
extern BOOL     EMFDRV_PolyPolyline( PHYSDEV dev, const POINT* pt,
                                     const DWORD* counts, DWORD polys);
extern BOOL     EMFDRV_Polygon( PHYSDEV dev, const POINT* pt, INT count );
extern BOOL     EMFDRV_Polyline( PHYSDEV dev, const POINT* pt,INT count);
extern BOOL     EMFDRV_Rectangle( PHYSDEV dev, INT left, INT top,
                                  INT right, INT bottom);
extern BOOL     EMFDRV_RestoreDC( PHYSDEV dev, INT level );
extern BOOL     EMFDRV_RoundRect( PHYSDEV dev, INT left, INT top,
                                  INT right, INT bottom, INT ell_width,
                                  INT ell_height );
extern INT      EMFDRV_SaveDC( PHYSDEV dev );
extern BOOL     EMFDRV_ScaleViewportExt( PHYSDEV dev, INT xNum,
                                         INT xDenom, INT yNum, INT yDenom );
extern BOOL     EMFDRV_ScaleWindowExt( PHYSDEV dev, INT xNum, INT xDenom,
                                       INT yNum, INT yDenom );
extern HBITMAP  EMFDRV_SelectBitmap( PHYSDEV dev, HBITMAP handle );
extern HBRUSH   EMFDRV_SelectBrush( PHYSDEV dev, HBRUSH handle );
extern BOOL     EMFDRV_SelectClipPath( PHYSDEV dev, INT iMode );
extern HFONT    EMFDRV_SelectFont( PHYSDEV dev, HFONT handle );
extern HPEN     EMFDRV_SelectPen( PHYSDEV dev, HPEN handle );
extern COLORREF EMFDRV_SetBkColor( PHYSDEV dev, COLORREF color );
extern INT      EMFDRV_SetBkMode( PHYSDEV dev, INT mode );
extern INT      EMFDRV_SetDIBitsToDevice( PHYSDEV dev, INT xDest, INT yDest,
                                          DWORD cx, DWORD cy, INT xSrc,
                                          INT ySrc, UINT startscan, UINT lines,
                                          LPCVOID bits, const BITMAPINFO *info,
                                          UINT coloruse );
extern INT      EMFDRV_SetMapMode( PHYSDEV dev, INT mode );
extern DWORD    EMFDRV_SetMapperFlags( PHYSDEV dev, DWORD flags );
extern COLORREF EMFDRV_SetPixel( PHYSDEV dev, INT x, INT y, COLORREF color );
extern INT      EMFDRV_SetPolyFillMode( PHYSDEV dev, INT mode );
extern INT      EMFDRV_SetROP2( PHYSDEV dev, INT rop );
extern INT      EMFDRV_SetStretchBltMode( PHYSDEV dev, INT mode );
extern UINT     EMFDRV_SetTextAlign( PHYSDEV dev, UINT align );
extern COLORREF EMFDRV_SetTextColor( PHYSDEV dev, COLORREF color );
extern BOOL     EMFDRV_SetViewportExt( PHYSDEV dev, INT x, INT y );
extern BOOL     EMFDRV_SetViewportOrg( PHYSDEV dev, INT x, INT y );
extern BOOL     EMFDRV_SetWindowExt( PHYSDEV dev, INT x, INT y );
extern BOOL     EMFDRV_SetWindowOrg( PHYSDEV dev, INT x, INT y );
extern BOOL     EMFDRV_StretchBlt( PHYSDEV devDst, INT xDst, INT yDst,
                                   INT widthDst, INT heightDst,
                                   PHYSDEV devSrc, INT xSrc, INT ySrc,
                                   INT widthSrc, INT heightSrc, DWORD rop );
extern INT      EMFDRV_StretchDIBits( PHYSDEV dev, INT xDst, INT yDst, INT widthDst,
                                      INT heightDst, INT xSrc, INT ySrc,
                                      INT widthSrc, INT heightSrc,
                                      const void *bits, const BITMAPINFO *info,
                                      UINT wUsage, DWORD dwRop );
extern BOOL     EMFDRV_StrokeAndFillPath( PHYSDEV dev );
extern BOOL     EMFDRV_StrokePath( PHYSDEV dev );
extern BOOL     EMFDRV_WidenPath( PHYSDEV dev );


#endif  /* __WINE_METAFILEDRV_H */

