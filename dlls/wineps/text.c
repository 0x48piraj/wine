/*
 *	PostScript driver text functions
 *
 *	Copyright 1998  Huw D M Davies
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
#include <string.h>
#include "gdi.h"
#include "psdrv.h"
#include "wine/debug.h"
#include "winspool.h"

WINE_DEFAULT_DEBUG_CHANNEL(psdrv);

static BOOL PSDRV_Text(PSDRV_PDEVICE *physDev, INT x, INT y, LPCWSTR str, UINT count,
		       BOOL bDrawBackground, const INT *lpDx);

/***********************************************************************
 *           PSDRV_ExtTextOut
 */
BOOL PSDRV_ExtTextOut( PSDRV_PDEVICE *physDev, INT x, INT y, UINT flags,
		       const RECT *lprect, LPCWSTR str, UINT count,
		       const INT *lpDx )
{
    BOOL bResult = TRUE;
    BOOL bClipped = FALSE;
    BOOL bOpaque = FALSE;
    RECT rect;

    TRACE("(x=%d, y=%d, flags=0x%08x, str=%s, count=%d, lpDx=%p)\n", x, y,
	  flags, debugstr_wn(str, count), count, lpDx);

    /* write font if not already written */
    PSDRV_SetFont(physDev);

    /* set clipping and/or draw background */
    if ((flags & (ETO_CLIPPED | ETO_OPAQUE)) && (lprect != NULL))
    {
        rect = *lprect;
        LPtoDP( physDev->hdc, (POINT *)&rect, 2 );
	PSDRV_WriteGSave(physDev);
	PSDRV_WriteRectangle(physDev, rect.left, rect.top, rect.right - rect.left, 
			     rect.bottom - rect.top);

	if (flags & ETO_OPAQUE)
	{
	    bOpaque = TRUE;
	    PSDRV_WriteGSave(physDev);
	    PSDRV_WriteSetColor(physDev, &physDev->bkColor);
	    PSDRV_WriteFill(physDev);
	    PSDRV_WriteGRestore(physDev);
	}

	if (flags & ETO_CLIPPED)
	{
	    bClipped = TRUE;
	    PSDRV_WriteClip(physDev);
	}

	bResult = PSDRV_Text(physDev, x, y, str, count, !(bClipped && bOpaque), lpDx); 
	PSDRV_WriteGRestore(physDev);
    }
    else
    {
	bResult = PSDRV_Text(physDev, x, y, str, count, TRUE, lpDx); 
    }

    return bResult;
}

/***********************************************************************
 *           PSDRV_Text
 */
static BOOL PSDRV_Text(PSDRV_PDEVICE *physDev, INT x, INT y, LPCWSTR str, UINT count,
		       BOOL bDrawBackground, const INT *lpDx)
{
    LPWSTR strbuf;
    SIZE sz;
    DC *dc = physDev->dc;
    UINT align = GetTextAlign( physDev->hdc );

    if (!count)
	return TRUE;

    strbuf = HeapAlloc( PSDRV_Heap, 0, (count + 1) * sizeof(WCHAR));
    if(!strbuf) {
        WARN("HeapAlloc failed\n");
        return FALSE;
    }

    if(align & TA_UPDATECP) {
	x = dc->CursPosX;
	y = dc->CursPosY;
    }

    x = INTERNAL_XWPTODP(dc, x, y);
    y = INTERNAL_YWPTODP(dc, x, y);

    GetTextExtentPoint32W(physDev->hdc, str, count, &sz);
    if(lpDx) {
        SIZE tmpsz;
	INT i;
	/* Get the width of the last char and add on all the offsets */
	GetTextExtentPoint32W(physDev->hdc, str + count - 1, 1, &tmpsz);
	for(i = 0; i < count-1; i++)
	    tmpsz.cx += lpDx[i];
	sz.cx = tmpsz.cx; /* sz.cy remains untouched */
    }

    sz.cx = INTERNAL_XWSTODS(dc, sz.cx);
    sz.cy = INTERNAL_YWSTODS(dc, sz.cy);
    TRACE("textAlign = %x\n", align);
    switch(align & (TA_LEFT | TA_CENTER | TA_RIGHT) ) {
    case TA_LEFT:
        if(align & TA_UPDATECP) {
	    dc->CursPosX = INTERNAL_XDPTOWP(dc, x + sz.cx, y);
	}
	break;

    case TA_CENTER:
	x -= sz.cx/2;
	break;

    case TA_RIGHT:
	x -= sz.cx;
	if(align & TA_UPDATECP) {
	    dc->CursPosX = INTERNAL_XDPTOWP(dc, x, y);
	}
	break;
    }

    switch(align & (TA_TOP | TA_BASELINE | TA_BOTTOM) ) {
    case TA_TOP:
        y += physDev->font.tm.tmAscent;
	break;

    case TA_BASELINE:
	break;

    case TA_BOTTOM:
        y -= physDev->font.tm.tmDescent;
	break;
    }

    memcpy(strbuf, str, count * sizeof(WCHAR));
    *(strbuf + count) = '\0';
    
    if ((GetBkMode( physDev->hdc ) != TRANSPARENT) && bDrawBackground)
    {
	PSDRV_WriteGSave(physDev);
	PSDRV_WriteNewPath(physDev);
	PSDRV_WriteRectangle(physDev, x, y - physDev->font.tm.tmAscent, sz.cx, 
			     physDev->font.tm.tmAscent + 
			     physDev->font.tm.tmDescent);
	PSDRV_WriteSetColor(physDev, &physDev->bkColor);
	PSDRV_WriteFill(physDev);
	PSDRV_WriteGRestore(physDev);
    }

    PSDRV_WriteMoveTo(physDev, x, y);
    
    if(!lpDx)
        PSDRV_WriteGlyphShow(physDev, strbuf, lstrlenW(strbuf));
    else {
        INT i;
	float dx = 0.0, dy = 0.0;
	float cos_theta = cos(physDev->font.escapement * M_PI / 1800.0);
	float sin_theta = sin(physDev->font.escapement * M_PI / 1800.0);
        for(i = 0; i < count-1; i++) {
	    TRACE("lpDx[%d] = %d\n", i, lpDx[i]);
	    PSDRV_WriteGlyphShow(physDev, &strbuf[i], 1);
	    dx += lpDx[i] * cos_theta;
	    dy -= lpDx[i] * sin_theta;
	    PSDRV_WriteMoveTo(physDev, x + INTERNAL_XWSTODS(dc, dx),
			      y + INTERNAL_YWSTODS(dc, dy));
	}
	PSDRV_WriteGlyphShow(physDev, &strbuf[i], 1);
    }

    /*
     * Underline and strikeout attributes.
     */
    if ((physDev->font.tm.tmUnderlined) || (physDev->font.tm.tmStruckOut)) {

        /* Get the thickness and the position for the underline attribute */
        /* We'll use the same thickness for the strikeout attribute       */

        float thick = physDev->font.afm->UnderlineThickness * physDev->font.scale;
        float pos   = -physDev->font.afm->UnderlinePosition * physDev->font.scale;
        SIZE size;
        INT escapement =  physDev->font.escapement;

        TRACE("Position = %f Thickness %f Escapement %d\n",
              pos, thick, escapement);

        /* Get the width of the text */

        PSDRV_GetTextExtentPoint(physDev, strbuf, lstrlenW(strbuf), &size);
        size.cx = INTERNAL_XWSTODS(dc, size.cx);

        /* Do the underline */

        if (physDev->font.tm.tmUnderlined) {
            PSDRV_WriteNewPath(physDev); /* will be closed by WriteRectangle */
            if (escapement != 0)  /* rotated text */
            {
                PSDRV_WriteGSave(physDev);  /* save the graphics state */
                PSDRV_WriteMoveTo(physDev, x, y); /* move to the start */

                /* temporarily rotate the coord system */
                PSDRV_WriteRotate(physDev, -escapement/10); 
                
                /* draw the underline relative to the starting point */
                PSDRV_WriteRRectangle(physDev, 0, (INT)pos, size.cx, (INT)thick);
            }
            else
                PSDRV_WriteRectangle(physDev, x, y + (INT)pos, size.cx, (INT)thick);

            PSDRV_WriteFill(physDev);

            if (escapement != 0)  /* rotated text */
                PSDRV_WriteGRestore(physDev);  /* restore the graphics state */
        }

        /* Do the strikeout */

        if (physDev->font.tm.tmStruckOut) {
            pos = -physDev->font.tm.tmAscent / 2;
            PSDRV_WriteNewPath(physDev); /* will be closed by WriteRectangle */
            if (escapement != 0)  /* rotated text */
            {
                PSDRV_WriteGSave(physDev);  /* save the graphics state */
                PSDRV_WriteMoveTo(physDev, x, y); /* move to the start */

                /* temporarily rotate the coord system */
                PSDRV_WriteRotate(physDev, -escapement/10);

                /* draw the underline relative to the starting point */
                PSDRV_WriteRRectangle(physDev, 0, (INT)pos, size.cx, (INT)thick);
            }
            else
                PSDRV_WriteRectangle(physDev, x, y + (INT)pos, size.cx, (INT)thick);

            PSDRV_WriteFill(physDev);

            if (escapement != 0)  /* rotated text */
                PSDRV_WriteGRestore(physDev);  /* restore the graphics state */
        }
    }

    HeapFree(PSDRV_Heap, 0, strbuf);
    return TRUE;
}
