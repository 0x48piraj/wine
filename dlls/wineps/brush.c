/*
 *	PostScript brush handling
 *
 * Copyright 1998  Huw D M Davies
 *
 */

#include "psdrv.h"
#include "brush.h"
#include "debugtools.h"
#include "gdi.h"
#include "winbase.h"

DEFAULT_DEBUG_CHANNEL(psdrv)

/***********************************************************************
 *           PSDRV_BRUSH_SelectObject
 */
HBRUSH PSDRV_BRUSH_SelectObject( DC * dc, HBRUSH hbrush, BRUSHOBJ * brush )
{
    HBRUSH prevbrush = dc->w.hBrush;
    PSDRV_PDEVICE *physDev = (PSDRV_PDEVICE *)dc->physDev;

    TRACE("hbrush = %08x\n", hbrush);
    dc->w.hBrush = hbrush;

    switch(brush->logbrush.lbStyle) {

    case BS_SOLID:
        PSDRV_CreateColor(physDev, &physDev->brush.color, 
			  brush->logbrush.lbColor);
	break;

    case BS_NULL:
        break;

    case BS_HATCHED:
        PSDRV_CreateColor(physDev, &physDev->brush.color, 
			  brush->logbrush.lbColor);
        break;

    case BS_PATTERN:
        FIXME("Unsupported brush style %d\n", brush->logbrush.lbStyle);
	break;

    default:
        FIXME("Unrecognized brush style %d\n", brush->logbrush.lbStyle);
	break;
    }

    physDev->brush.set = FALSE;
    return prevbrush;
}


/**********************************************************************
 *
 *	PSDRV_SetBrush
 *
 */
static BOOL PSDRV_SetBrush(DC *dc)
{
    BOOL ret = TRUE;
    PSDRV_PDEVICE *physDev = (PSDRV_PDEVICE *)dc->physDev;
    BRUSHOBJ *brush = (BRUSHOBJ *)GDI_GetObjPtr( dc->w.hBrush, BRUSH_MAGIC );

    if(!brush) {
        ERR("Can't get BRUSHOBJ\n");
	return FALSE;
    }
    
    switch (brush->logbrush.lbStyle) {
    case BS_SOLID:
    case BS_HATCHED:
        PSDRV_WriteSetColor(dc, &physDev->brush.color);
	break;

    case BS_NULL:
        break;

    default:
        ret = FALSE;
        break;

    }
    physDev->brush.set = TRUE;
    GDI_ReleaseObj( dc->w.hBrush );
    return TRUE;
}


/**********************************************************************
 *
 *	PSDRV_Fill
 *
 */
static BOOL PSDRV_Fill(DC *dc, BOOL EO)
{
    if(!EO)
        return PSDRV_WriteFill(dc);
    else
      return PSDRV_WriteEOFill(dc);
}


/**********************************************************************
 *
 *	PSDRV_Clip
 *
 */
static BOOL PSDRV_Clip(DC *dc, BOOL EO)
{
    if(!EO)
        return PSDRV_WriteClip(dc);
    else
        return PSDRV_WriteEOClip(dc);
}

/**********************************************************************
 *
 *	PSDRV_Brush
 *
 */
BOOL PSDRV_Brush(DC *dc, BOOL EO)
{
    BOOL ret = TRUE;
    BRUSHOBJ *brush = (BRUSHOBJ *)GDI_GetObjPtr( dc->w.hBrush, BRUSH_MAGIC );
    PSDRV_PDEVICE *physDev = dc->physDev;

    if(!brush) {
        ERR("Can't get BRUSHOBJ\n");
	return FALSE;
    }

    switch (brush->logbrush.lbStyle) {
    case BS_SOLID:
        PSDRV_SetBrush(dc);
	PSDRV_WriteGSave(dc);
        PSDRV_Fill(dc, EO);
	PSDRV_WriteGRestore(dc);
	break;

    case BS_HATCHED:
        PSDRV_SetBrush(dc);

	switch(brush->logbrush.lbHatch) {
	case HS_VERTICAL:
	case HS_CROSS:
	    PSDRV_WriteGSave(dc);
	    PSDRV_Clip(dc, EO);
	    PSDRV_WriteHatch(dc);
	    PSDRV_WriteStroke(dc);
	    PSDRV_WriteGRestore(dc);
	    if(brush->logbrush.lbHatch == HS_VERTICAL)
	        break;
	    /* else fallthrough for HS_CROSS */

	case HS_HORIZONTAL:
	    PSDRV_WriteGSave(dc);
	    PSDRV_Clip(dc, EO);
	    PSDRV_WriteRotate(dc, 90.0);
	    PSDRV_WriteHatch(dc);
	    PSDRV_WriteStroke(dc);
	    PSDRV_WriteGRestore(dc);
	    break;

	case HS_FDIAGONAL:
	case HS_DIAGCROSS:
	    PSDRV_WriteGSave(dc);
	    PSDRV_Clip(dc, EO);
	    PSDRV_WriteRotate(dc, -45.0);
	    PSDRV_WriteHatch(dc);
	    PSDRV_WriteStroke(dc);
	    PSDRV_WriteGRestore(dc);
	    if(brush->logbrush.lbHatch == HS_FDIAGONAL)
	        break;
	    /* else fallthrough for HS_DIAGCROSS */
	    
	case HS_BDIAGONAL:
	    PSDRV_WriteGSave(dc);
	    PSDRV_Clip(dc, EO);
	    PSDRV_WriteRotate(dc, 45.0);
	    PSDRV_WriteHatch(dc);
	    PSDRV_WriteStroke(dc);
	    PSDRV_WriteGRestore(dc);
	    break;

	default:
	    ERR("Unknown hatch style\n");
	    ret = FALSE;
            break;
	}
	break;

    case BS_NULL:
	break;

    case BS_PATTERN:
        {
	    BITMAP bm;
	    BYTE *bits;
	    GetObjectA(brush->logbrush.lbHatch, sizeof(BITMAP), &bm);
	    TRACE("BS_PATTERN %dx%d %d bpp\n", bm.bmWidth, bm.bmHeight,
		  bm.bmBitsPixel);
	    bits = HeapAlloc(PSDRV_Heap, 0, bm.bmWidthBytes * bm.bmHeight);
	    GetBitmapBits(brush->logbrush.lbHatch,
			  bm.bmWidthBytes * bm.bmHeight, bits);

	    if(physDev->pi->ppd->LanguageLevel > 1) {
	        PSDRV_WriteGSave(dc);
	        PSDRV_WritePatternDict(dc, &bm, bits);
		PSDRV_Fill(dc, EO);
		PSDRV_WriteGRestore(dc);
	    } else {
	        FIXME("Trying to set a pattern brush on a level 1 printer\n");
		ret = FALSE;
	    }
	    HeapFree(PSDRV_Heap, 0, bits);	
	}
	break;

    default:
        ret = FALSE;
	break;
    }

    GDI_ReleaseObj( dc->w.hBrush );
    return ret;
}




