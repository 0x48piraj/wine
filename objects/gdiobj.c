/*
 * GDI functions
 *
 * Copyright 1993 Alexandre Julliard
 */

#include "config.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#include "windef.h"
#include "wingdi.h"
#include "winerror.h"
#include "wine/winbase16.h"

#include "bitmap.h"
#include "brush.h"
#include "dc.h"
#include "font.h"
#include "heap.h"
#include "options.h"
#include "palette.h"
#include "pen.h"
#include "region.h"
#include "debugtools.h"
#include "gdi.h"
#include "tweak.h"
#include "syslevel.h"

DEFAULT_DEBUG_CHANNEL(gdi);

/**********************************************************************/

GDI_DRIVER *GDI_Driver = NULL;

/***********************************************************************
 *          GDI stock objects 
 */

static BRUSHOBJ WhiteBrush =
{
    { 0, BRUSH_MAGIC, 1 },             /* header */
    { BS_SOLID, RGB(255,255,255), 0 }  /* logbrush */
};

static BRUSHOBJ LtGrayBrush =
{
    { 0, BRUSH_MAGIC, 1 },             /* header */
/* FIXME : this should perhaps be BS_HATCHED, at least for 1 bitperpixel */
    { BS_SOLID, RGB(192,192,192), 0 }  /* logbrush */
};

static BRUSHOBJ GrayBrush =
{
    { 0, BRUSH_MAGIC, 1 },             /* header */
/* FIXME : this should perhaps be BS_HATCHED, at least for 1 bitperpixel */
    { BS_SOLID, RGB(128,128,128), 0 }  /* logbrush */
};

static BRUSHOBJ DkGrayBrush =
{
    { 0, BRUSH_MAGIC, 1 },          /* header */
/* This is BS_HATCHED, for 1 bitperpixel. This makes the spray work in pbrush */
/* NB_HATCH_STYLES is an index into HatchBrushes */
    { BS_HATCHED, RGB(0,0,0), NB_HATCH_STYLES }  /* logbrush */
};

static BRUSHOBJ BlackBrush =
{
    { 0, BRUSH_MAGIC, 1 },       /* header */
    { BS_SOLID, RGB(0,0,0), 0 }  /* logbrush */
};

static BRUSHOBJ NullBrush =
{
    { 0, BRUSH_MAGIC, 1 },  /* header */
    { BS_NULL, 0, 0 }       /* logbrush */
};

static PENOBJ WhitePen =
{
    { 0, PEN_MAGIC, 1 },                     /* header */
    { PS_SOLID, { 1, 0 }, RGB(255,255,255) } /* logpen */
};

static PENOBJ BlackPen =
{
    { 0, PEN_MAGIC, 1 },               /* header */
    { PS_SOLID, { 1, 0 }, RGB(0,0,0) } /* logpen */
};

static PENOBJ NullPen =
{
    { 0, PEN_MAGIC, 1 },      /* header */
    { PS_NULL, { 1, 0 }, 0 }  /* logpen */
};

static FONTOBJ OEMFixedFont =
{
    { 0, FONT_MAGIC, 1 },   /* header */
    { 0, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, OEM_CHARSET,
      0, 0, DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, "" }
};
/* Filler to make the location counter dword aligned again.  This is necessary
   since (a) FONTOBJ is packed, (b) gcc places initialised variables in the code
   segment, and (c) Solaris assembler is stupid.  */
static UINT16 align_OEMFixedFont = 1;

static FONTOBJ AnsiFixedFont =
{
    { 0, FONT_MAGIC, 1 },   /* header */
    { 0, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
      0, 0, DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, "" }
};
static UINT16 align_AnsiFixedFont = 1;

static FONTOBJ AnsiVarFont =
{
    { 0, FONT_MAGIC, 1 },   /* header */
    { 0, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
      0, 0, DEFAULT_QUALITY, VARIABLE_PITCH | FF_SWISS, "MS Sans Serif" }
};
static UINT16 align_AnsiVarFont = 1;

static FONTOBJ SystemFont =
{
    { 0, FONT_MAGIC, 1 },
    { 0, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
      0, 0, DEFAULT_QUALITY, VARIABLE_PITCH | FF_SWISS, "System" }
};
static UINT16 align_SystemFont = 1;

static FONTOBJ DeviceDefaultFont =
{
    { 0, FONT_MAGIC, 1 },   /* header */
    { 0, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
      0, 0, DEFAULT_QUALITY, VARIABLE_PITCH | FF_SWISS, "" }
};
static UINT16 align_DeviceDefaultFont = 1;

static FONTOBJ SystemFixedFont =
{
    { 0, FONT_MAGIC, 1 },   /* header */
    { 0, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
      0, 0, DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, "" }
};
static UINT16 align_SystemFixedFont = 1;

/* FIXME: Is this correct? */
static FONTOBJ DefaultGuiFont =
{
    { 0, FONT_MAGIC, 1 },   /* header */
    { 0, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
      0, 0, DEFAULT_QUALITY, VARIABLE_PITCH | FF_SWISS, "MS Sans Serif" }
};
static UINT16 align_DefaultGuiFont = 1;


static GDIOBJHDR * StockObjects[NB_STOCK_OBJECTS] =
{
    (GDIOBJHDR *) &WhiteBrush,
    (GDIOBJHDR *) &LtGrayBrush,
    (GDIOBJHDR *) &GrayBrush,
    (GDIOBJHDR *) &DkGrayBrush,
    (GDIOBJHDR *) &BlackBrush,
    (GDIOBJHDR *) &NullBrush,
    (GDIOBJHDR *) &WhitePen,
    (GDIOBJHDR *) &BlackPen,
    (GDIOBJHDR *) &NullPen,
    NULL,
    (GDIOBJHDR *) &OEMFixedFont,
    (GDIOBJHDR *) &AnsiFixedFont,
    (GDIOBJHDR *) &AnsiVarFont,
    (GDIOBJHDR *) &SystemFont,
    (GDIOBJHDR *) &DeviceDefaultFont,
    NULL,            /* DEFAULT_PALETTE created by PALETTE_Init */
    (GDIOBJHDR *) &SystemFixedFont,
    (GDIOBJHDR *) &DefaultGuiFont
};

HBITMAP hPseudoStockBitmap; /* 1x1 bitmap for memory DCs */

static SYSLEVEL GDI_level;
static WORD GDI_HeapSel;


/******************************************************************************
 *
 *   void  ReadFontInformation(
 *      char const  *fontName,
 *      FONTOBJ  *font,
 *      int  defHeight,
 *      int  defBold,
 *      int  defItalic,
 *      int  defUnderline,
 *      int  defStrikeOut )
 *
 *   ReadFontInformation() checks the Wine configuration file's Tweak.Fonts
 *   section for entries containing fontName.Height, fontName.Bold, etc.,
 *   where fontName is the name specified in the call (e.g., "System").  It
 *   attempts to be user friendly by accepting 'n', 'N', 'f', 'F', or '0' as
 *   the first character in the boolean attributes (bold, italic, and
 *   underline).
 *****************************************************************************/

static void  ReadFontInformation(
    char const *fontName,
    FONTOBJ *font,
    int  defHeight,
    int  defBold,
    int  defItalic,
    int  defUnderline,
    int  defStrikeOut )
{
    char  key[256];

    /* In order for the stock fonts to be independent of 
     * mapping mode, the height (& width) must be 0
     */
    sprintf(key, "%s.Height", fontName);
    font->logfont.lfHeight =
	PROFILE_GetWineIniInt("Tweak.Fonts", key, defHeight);

    sprintf(key, "%s.Bold", fontName);
    font->logfont.lfWeight =
	(PROFILE_GetWineIniBool("Tweak.Fonts", key, defBold)) ?
	FW_BOLD : FW_NORMAL;

    sprintf(key, "%s.Italic", fontName);
    font->logfont.lfItalic =
	PROFILE_GetWineIniBool("Tweak.Fonts", key, defItalic);

    sprintf(key, "%s.Underline", fontName);
    font->logfont.lfUnderline =
	PROFILE_GetWineIniBool("Tweak.Fonts", key, defUnderline);

    sprintf(key, "%s.StrikeOut", fontName);
    font->logfont.lfStrikeOut =
	PROFILE_GetWineIniBool("Tweak.Fonts", key, defStrikeOut);

    return;
}

/***********************************************************************
 * Because the stock fonts have their structure initialized with
 * a height of 0 to keep them independent of mapping mode, simply
 * returning the LOGFONT as is will not work correctly.
 * These "FixStockFontSizeXXX()" methods will get the correct
 * size for the fonts.
 */
static void GetFontMetrics(HFONT handle, LPTEXTMETRICA lptm)
{
  HDC         hdc;
  HFONT       hOldFont;

  hdc = CreateDCA("DISPLAY", NULL, NULL, NULL);

  hOldFont = (HFONT)SelectObject(hdc, handle);

  GetTextMetricsA(hdc, lptm);

  SelectObject(hdc, hOldFont);

  DeleteDC(hdc);
}

static inline void FixStockFontSize16(
  HFONT  handle, 
  INT16  count, 
  LPVOID buffer)
{
  TEXTMETRICA tm;
  LOGFONT16*  pLogFont = (LOGFONT16*)buffer;

  /*
   * Was the lfHeight field copied (it's the first field)?
   * If it was and it was null, replace the height.
   */
  if ( (count >= 2*sizeof(INT16)) &&
       (pLogFont->lfHeight == 0) )
  {
    GetFontMetrics(handle, &tm);
    
    pLogFont->lfHeight = tm.tmHeight;
    pLogFont->lfWidth  = tm.tmAveCharWidth;
  }
}

static inline void FixStockFontSizeA(
  HFONT  handle, 
  INT    count, 
  LPVOID buffer)
{
  TEXTMETRICA tm;
  LOGFONTA*  pLogFont = (LOGFONTA*)buffer;

  /*
   * Was the lfHeight field copied (it's the first field)?
   * If it was and it was null, replace the height.
   */
  if ( (count >= 2*sizeof(INT)) &&
       (pLogFont->lfHeight == 0) )
  {
    GetFontMetrics(handle, &tm);

    pLogFont->lfHeight = tm.tmHeight;
    pLogFont->lfWidth  = tm.tmAveCharWidth;
  }
}

/**
 * Since the LOGFONTA and LOGFONTW structures are identical up to the 
 * lfHeight member (the one of interest in this case) we simply define
 * the W version as the A version. 
 */
#define FixStockFontSizeW FixStockFontSizeA

#define TRACE_SEC(handle,text) \
   TRACE("(%04x): " text " %ld\n", (handle), GDI_level.crst.RecursionCount)

/***********************************************************************
 *           GDI_Init
 *
 * GDI initialization.
 */
BOOL GDI_Init(void)
{
    BOOL systemIsBold = (TWEAK_WineLook == WIN31_LOOK);
    HPALETTE16 hpalette;
    HINSTANCE16 instance;

    _CreateSysLevel( &GDI_level, 3 );

    /* create GDI heap */
    if ((instance = LoadLibrary16( "GDI.EXE" )) < 32) return FALSE;
    GDI_HeapSel = GlobalHandleToSel16( instance );

    /* Kill some warnings.  */
    (void)align_OEMFixedFont;
    (void)align_AnsiFixedFont;
    (void)align_AnsiVarFont;
    (void)align_SystemFont;
    (void)align_DeviceDefaultFont;
    (void)align_SystemFixedFont;
    (void)align_DefaultGuiFont;

    /* TWEAK: Initialize font hints */
    ReadFontInformation("OEMFixed", &OEMFixedFont, 0, 0, 0, 0, 0);
    ReadFontInformation("AnsiFixed", &AnsiFixedFont, 0, 0, 0, 0, 0);
    ReadFontInformation("AnsiVar", &AnsiVarFont, 0, 0, 0, 0, 0);
    ReadFontInformation("System", &SystemFont, 0, systemIsBold, 0, 0, 0);
    ReadFontInformation("DeviceDefault", &DeviceDefaultFont, 0, 0, 0, 0, 0);
    ReadFontInformation("SystemFixed", &SystemFixedFont, 0, systemIsBold, 0, 0, 0);
    ReadFontInformation("DefaultGui", &DefaultGuiFont, 0, 0, 0, 0, 0);

    /* Create default palette */

    /* DR well *this* palette can't be moveable (?) */
    hpalette = PALETTE_Init();
    if( !hpalette ) return FALSE;
    StockObjects[DEFAULT_PALETTE] = (GDIOBJHDR *)LOCAL_Lock( GDI_HeapSel, hpalette );

    hPseudoStockBitmap = CreateBitmap( 1, 1, 1, 1, NULL ); 
    return TRUE;
}


/***********************************************************************
 *           GDI_AllocObject
 */
void *GDI_AllocObject( WORD size, WORD magic, HGDIOBJ *handle )
{
    static DWORD count = 0;
    GDIOBJHDR *obj;

    _EnterSysLevel( &GDI_level );
    if (!(*handle = LOCAL_Alloc( GDI_HeapSel, LMEM_MOVEABLE, size )))
    {
        _LeaveSysLevel( &GDI_level );
        return NULL;
    }
    obj = (GDIOBJHDR *)LOCAL_Lock( GDI_HeapSel, *handle );
    obj->hNext   = 0;
    obj->wMagic  = magic|OBJECT_NOSYSTEM;
    obj->dwCount = ++count;

    TRACE_SEC( *handle, "enter" );
    return obj;
}


/***********************************************************************
 *           GDI_ReallocObject
 *
 * The object ptr must have been obtained with GDI_GetObjPtr.
 * The new pointer must be released with GDI_ReleaseObj.
 */
void *GDI_ReallocObject( WORD size, HGDIOBJ handle, void *object )
{
    HGDIOBJ new_handle;

    LOCAL_Unlock( GDI_HeapSel, handle );
    if (!(new_handle = LOCAL_ReAlloc( GDI_HeapSel, handle, size, LMEM_MOVEABLE )))
    {
        TRACE_SEC( handle, "leave" );
        _LeaveSysLevel( &GDI_level );
        return NULL;
    }
    assert( new_handle == handle );  /* moveable handle cannot change */
    return LOCAL_Lock( GDI_HeapSel, handle );
}
 

/***********************************************************************
 *           GDI_FreeObject
 */
BOOL GDI_FreeObject( HGDIOBJ handle, void *ptr )
{
    GDIOBJHDR *object = ptr;

    /* can't free stock objects */
    if (handle < FIRST_STOCK_HANDLE)
    {
        object->wMagic = 0;  /* Mark it as invalid */
        LOCAL_Unlock( GDI_HeapSel, handle );
        LOCAL_Free( GDI_HeapSel, handle );
    }
    TRACE_SEC( handle, "leave" );
    _LeaveSysLevel( &GDI_level );
    return TRUE;
}


/***********************************************************************
 *           GDI_GetObjPtr
 *
 * Return a pointer to the GDI object associated to the handle.
 * Return NULL if the object has the wrong magic number.
 * The object must be released with GDI_ReleaseObj.
 */
void *GDI_GetObjPtr( HGDIOBJ handle, WORD magic )
{
    GDIOBJHDR *ptr = NULL;

    _EnterSysLevel( &GDI_level );

    if (handle >= FIRST_STOCK_HANDLE)
    {
        if (handle <= LAST_STOCK_HANDLE) ptr = StockObjects[handle - FIRST_STOCK_HANDLE];
        if (ptr && (magic != MAGIC_DONTCARE)
	&& (GDIMAGIC(ptr->wMagic) != magic)) ptr = NULL;
    }
    else
    {
        ptr = (GDIOBJHDR *)LOCAL_Lock( GDI_HeapSel, handle );
        if (ptr &&
	(magic != MAGIC_DONTCARE) && (GDIMAGIC(ptr->wMagic) != magic))
        {
            LOCAL_Unlock( GDI_HeapSel, handle );
            ptr = NULL;
        }
    }

    if (!ptr)
    {
        _LeaveSysLevel( &GDI_level );
        SetLastError( ERROR_INVALID_HANDLE );
    }
    else TRACE_SEC( handle, "enter" );

    return ptr;
}


/***********************************************************************
 *           GDI_ReleaseObj
 *
 */
void GDI_ReleaseObj( HGDIOBJ handle )
{
    if (handle < FIRST_STOCK_HANDLE) LOCAL_Unlock( GDI_HeapSel, handle );
    TRACE_SEC( handle, "leave" );
    _LeaveSysLevel( &GDI_level );
}


/***********************************************************************
 *           DeleteObject16    (GDI.69)
 */
BOOL16 WINAPI DeleteObject16( HGDIOBJ16 obj )
{
    return DeleteObject( obj );
}


/***********************************************************************
 *           DeleteObject    (GDI32.70)
 */
BOOL WINAPI DeleteObject( HGDIOBJ obj )
{
      /* Check if object is valid */

    GDIOBJHDR * header;
    if (HIWORD(obj)) return FALSE;
    if ((obj >= FIRST_STOCK_HANDLE) && (obj <= LAST_STOCK_HANDLE)) {
	TRACE("Preserving Stock object %04x\n", obj );
	/* NOTE: No GDI_Release is necessary */
        return TRUE;
    }
    if (obj == hPseudoStockBitmap) return TRUE;
    if (!(header = GDI_GetObjPtr( obj, MAGIC_DONTCARE ))) return FALSE;

    if (!(header->wMagic & OBJECT_NOSYSTEM)
    &&   (header->wMagic >= FIRST_MAGIC) && (header->wMagic <= LAST_MAGIC))
    {
	TRACE("Preserving system object %04x\n", obj);
        GDI_ReleaseObj( obj );
	return TRUE;
    }
	
    TRACE("%04x\n", obj );

      /* Delete object */

    switch(GDIMAGIC(header->wMagic))
    {
      case PEN_MAGIC:     return GDI_FreeObject( obj, header );
      case BRUSH_MAGIC:   return BRUSH_DeleteObject( obj, (BRUSHOBJ*)header );
      case FONT_MAGIC:    return GDI_FreeObject( obj, header );
      case PALETTE_MAGIC: return PALETTE_DeleteObject(obj,(PALETTEOBJ*)header);
      case BITMAP_MAGIC:  return BITMAP_DeleteObject( obj, (BITMAPOBJ*)header);
      case REGION_MAGIC:  return REGION_DeleteObject( obj, (RGNOBJ*)header );
      case DC_MAGIC:
          GDI_ReleaseObj( obj );
          return DeleteDC(obj);
      case 0 :
        WARN("Already deleted\n");
        break;
      default:
        WARN("Unknown magic number (%d)\n",GDIMAGIC(header->wMagic));
    }
    GDI_ReleaseObj( obj );
    return FALSE;
}

/***********************************************************************
 *           GetStockObject16    (GDI.87)
 */
HGDIOBJ16 WINAPI GetStockObject16( INT16 obj )
{
    return (HGDIOBJ16)GetStockObject( obj );
}


/***********************************************************************
 *           GetStockObject    (GDI32.220)
 */
HGDIOBJ WINAPI GetStockObject( INT obj )
{
    HGDIOBJ ret;
    if ((obj < 0) || (obj >= NB_STOCK_OBJECTS)) return 0;
    if (!StockObjects[obj]) return 0;
    ret = (HGDIOBJ16)(FIRST_STOCK_HANDLE + obj);
    TRACE("returning %4x\n", ret );
    return ret;
}


/***********************************************************************
 *           GetObject16    (GDI.82)
 */
INT16 WINAPI GetObject16( HANDLE16 handle, INT16 count, LPVOID buffer )
{
    GDIOBJHDR * ptr;
    INT16 result = 0;
    TRACE("%04x %d %p\n", handle, count, buffer );
    if (!count) return 0;

    if (!(ptr = GDI_GetObjPtr( handle, MAGIC_DONTCARE ))) return 0;
    
    switch(GDIMAGIC(ptr->wMagic))
      {
      case PEN_MAGIC:
	result = PEN_GetObject16( (PENOBJ *)ptr, count, buffer );
	break;
      case BRUSH_MAGIC: 
	result = BRUSH_GetObject16( (BRUSHOBJ *)ptr, count, buffer );
	break;
      case BITMAP_MAGIC: 
	result = BITMAP_GetObject16( (BITMAPOBJ *)ptr, count, buffer );
	break;
      case FONT_MAGIC:
	result = FONT_GetObject16( (FONTOBJ *)ptr, count, buffer );
	
	/*
	 * Fix the LOGFONT structure for the stock fonts
	 */
	if ( (handle >= FIRST_STOCK_HANDLE) && 
	     (handle <= LAST_STOCK_HANDLE) )
	  FixStockFontSize16(handle, count, buffer);	
	break;
      case PALETTE_MAGIC:
	result = PALETTE_GetObject( (PALETTEOBJ *)ptr, count, buffer );
	break;
      }
    GDI_ReleaseObj( handle );
    return result;
}


/***********************************************************************
 *           GetObjectA    (GDI32.204)
 */
INT WINAPI GetObjectA( HANDLE handle, INT count, LPVOID buffer )
{
    GDIOBJHDR * ptr;
    INT result = 0;
    TRACE("%08x %d %p\n", handle, count, buffer );
    if (!count) return 0;

    if (!(ptr = GDI_GetObjPtr( handle, MAGIC_DONTCARE ))) return 0;

    switch(GDIMAGIC(ptr->wMagic))
    {
      case PEN_MAGIC:
	  result = PEN_GetObject( (PENOBJ *)ptr, count, buffer );
	  break;
      case BRUSH_MAGIC: 
	  result = BRUSH_GetObject( (BRUSHOBJ *)ptr, count, buffer );
	  break;
      case BITMAP_MAGIC: 
	  result = BITMAP_GetObject( (BITMAPOBJ *)ptr, count, buffer );
	  break;
      case FONT_MAGIC:
	  result = FONT_GetObjectA( (FONTOBJ *)ptr, count, buffer );
	  
	  /*
	   * Fix the LOGFONT structure for the stock fonts
	   */
	  if ( (handle >= FIRST_STOCK_HANDLE) && 
	       (handle <= LAST_STOCK_HANDLE) )
	    FixStockFontSizeA(handle, count, buffer);
	  break;
      case PALETTE_MAGIC:
	  result = PALETTE_GetObject( (PALETTEOBJ *)ptr, count, buffer );
	  break;

      case REGION_MAGIC:
      case DC_MAGIC:
      case DISABLED_DC_MAGIC:
      case META_DC_MAGIC:
      case METAFILE_MAGIC:
      case METAFILE_DC_MAGIC:
      case ENHMETAFILE_MAGIC:
      case ENHMETAFILE_DC_MAGIC:
          FIXME("Magic %04x not implemented\n", GDIMAGIC(ptr->wMagic) );
          break;

      default:
          ERR("Invalid GDI Magic %04x\n", GDIMAGIC(ptr->wMagic));
          break;
    }
    GDI_ReleaseObj( handle );
    return result;
}

/***********************************************************************
 *           GetObjectW    (GDI32.206)
 */
INT WINAPI GetObjectW( HANDLE handle, INT count, LPVOID buffer )
{
    GDIOBJHDR * ptr;
    INT result = 0;
    TRACE("%08x %d %p\n", handle, count, buffer );
    if (!count) return 0;

    if (!(ptr = GDI_GetObjPtr( handle, MAGIC_DONTCARE ))) return 0;

    switch(GDIMAGIC(ptr->wMagic))
    {
      case PEN_MAGIC:
	  result = PEN_GetObject( (PENOBJ *)ptr, count, buffer );
	  break;
      case BRUSH_MAGIC: 
	  result = BRUSH_GetObject( (BRUSHOBJ *)ptr, count, buffer );
	  break;
      case BITMAP_MAGIC: 
	  result = BITMAP_GetObject( (BITMAPOBJ *)ptr, count, buffer );
	  break;
      case FONT_MAGIC:
	  result = FONT_GetObjectW( (FONTOBJ *)ptr, count, buffer );

	  /*
	   * Fix the LOGFONT structure for the stock fonts
	   */
	  if ( (handle >= FIRST_STOCK_HANDLE) && 
	       (handle <= LAST_STOCK_HANDLE) )
	    FixStockFontSizeW(handle, count, buffer);
	  break;
      case PALETTE_MAGIC:
	  result = PALETTE_GetObject( (PALETTEOBJ *)ptr, count, buffer );
	  break;
      default:
          FIXME("Magic %04x not implemented\n", GDIMAGIC(ptr->wMagic) );
          break;
    }
    GDI_ReleaseObj( handle );
    return result;
}

/***********************************************************************
 *           GetObjectType    (GDI32.205)
 */
DWORD WINAPI GetObjectType( HANDLE handle )
{
    GDIOBJHDR * ptr;
    INT result = 0;
    TRACE("%08x\n", handle );

    if (!(ptr = GDI_GetObjPtr( handle, MAGIC_DONTCARE ))) return 0;
    
    switch(GDIMAGIC(ptr->wMagic))
    {
      case PEN_MAGIC:
	  result = OBJ_PEN;
	  break;
      case BRUSH_MAGIC: 
	  result = OBJ_BRUSH;
	  break;
      case BITMAP_MAGIC: 
	  result = OBJ_BITMAP;
	  break;
      case FONT_MAGIC:
	  result = OBJ_FONT;
	  break;
      case PALETTE_MAGIC:
	  result = OBJ_PAL;
	  break;
      case REGION_MAGIC:
	  result = OBJ_REGION;
	  break;
      case DC_MAGIC:
	  result = OBJ_DC;
	  break;
      case META_DC_MAGIC:
	  result = OBJ_METADC;
	  break;
      case METAFILE_MAGIC:
	  result = OBJ_METAFILE;
	  break;
      case METAFILE_DC_MAGIC:
	  result = OBJ_METADC;
	  break;
      case ENHMETAFILE_MAGIC:
	  result = OBJ_ENHMETAFILE;
	  break;
      case ENHMETAFILE_DC_MAGIC:
	  result = OBJ_ENHMETADC;
	  break;
      default:
	  FIXME("Magic %04x not implemented\n", GDIMAGIC(ptr->wMagic) );
	  break;
    }
    GDI_ReleaseObj( handle );
    return result;
}

/***********************************************************************
 *           GetCurrentObject    	(GDI32.166)
 */
HANDLE WINAPI GetCurrentObject(HDC hdc,UINT type)
{
    HANDLE ret = 0;
    DC * dc = DC_GetDCPtr( hdc );

    if (dc) 
    {
    switch (type) {
	case OBJ_PEN:	 ret = dc->w.hPen; break;
	case OBJ_BRUSH:	 ret = dc->w.hBrush; break;
	case OBJ_PAL:	 ret = dc->w.hPalette; break;
	case OBJ_FONT:	 ret = dc->w.hFont; break;
	case OBJ_BITMAP: ret = dc->w.hBitmap; break;
    default:
    	/* the SDK only mentions those above */
    	FIXME("(%08x,%d): unknown type.\n",hdc,type);
	    break;
        }
        GDI_ReleaseObj( hdc );
    }
    return ret;
}


/***********************************************************************
 *           SelectObject16    (GDI.45)
 */
HGDIOBJ16 WINAPI SelectObject16( HDC16 hdc, HGDIOBJ16 handle )
{
    return (HGDIOBJ16)SelectObject( hdc, handle );
}


/***********************************************************************
 *           SelectObject    (GDI32.299)
 */
HGDIOBJ WINAPI SelectObject( HDC hdc, HGDIOBJ handle )
{
    HGDIOBJ ret = 0;
    DC * dc = DC_GetDCUpdate( hdc );
    if (!dc) return 0;
    TRACE("hdc=%04x %04x\n", hdc, handle );
    if (dc->funcs->pSelectObject)
        ret = dc->funcs->pSelectObject( dc, handle );
    GDI_ReleaseObj( hdc );
    return ret;
}


/***********************************************************************
 *           UnrealizeObject16    (GDI.150)
 */
BOOL16 WINAPI UnrealizeObject16( HGDIOBJ16 obj )
{
    return UnrealizeObject( obj );
}


/***********************************************************************
 *           UnrealizeObject    (GDI32.358)
 */
BOOL WINAPI UnrealizeObject( HGDIOBJ obj )
{
    BOOL result = TRUE;
  /* Check if object is valid */

    GDIOBJHDR * header = GDI_GetObjPtr( obj, MAGIC_DONTCARE );
    if (!header) return FALSE;

    TRACE("%04x\n", obj );

      /* Unrealize object */

    switch(GDIMAGIC(header->wMagic))
    {
    case PALETTE_MAGIC: 
        result = PALETTE_UnrealizeObject( obj, (PALETTEOBJ *)header );
	break;

    case BRUSH_MAGIC:
        /* Windows resets the brush origin. We don't need to. */
        break;
    }
    GDI_ReleaseObj( obj );
    return result;
}


/***********************************************************************
 *           EnumObjects16    (GDI.71)
 */
INT16 WINAPI EnumObjects16( HDC16 hdc, INT16 nObjType,
                            GOBJENUMPROC16 lpEnumFunc, LPARAM lParam )
{
    /* Solid colors to enumerate */
    static const COLORREF solid_colors[] =
    { RGB(0x00,0x00,0x00), RGB(0xff,0xff,0xff),
      RGB(0xff,0x00,0x00), RGB(0x00,0xff,0x00),
      RGB(0x00,0x00,0xff), RGB(0xff,0xff,0x00),
      RGB(0xff,0x00,0xff), RGB(0x00,0xff,0xff),
      RGB(0x80,0x00,0x00), RGB(0x00,0x80,0x00),
      RGB(0x80,0x80,0x00), RGB(0x00,0x00,0x80),
      RGB(0x80,0x00,0x80), RGB(0x00,0x80,0x80),
      RGB(0x80,0x80,0x80), RGB(0xc0,0xc0,0xc0)
    };
    
    INT16 i, retval = 0;
    LOGPEN16 *pen;
    LOGBRUSH16 *brush = NULL;

    TRACE("%04x %d %08lx %08lx\n",
                 hdc, nObjType, (DWORD)lpEnumFunc, lParam );
    switch(nObjType)
    {
    case OBJ_PEN:
        /* Enumerate solid pens */
        if (!(pen = SEGPTR_NEW(LOGPEN16))) break;
        for (i = 0; i < sizeof(solid_colors)/sizeof(solid_colors[0]); i++)
        {
            pen->lopnStyle   = PS_SOLID;
            pen->lopnWidth.x = 1;
            pen->lopnWidth.y = 0;
            pen->lopnColor   = solid_colors[i];
            retval = lpEnumFunc( SEGPTR_GET(pen), lParam );
            TRACE("solid pen %08lx, ret=%d\n",
                         solid_colors[i], retval);
            if (!retval) break;
        }
        SEGPTR_FREE(pen);
        break;

    case OBJ_BRUSH:
        /* Enumerate solid brushes */
        if (!(brush = SEGPTR_NEW(LOGBRUSH16))) break;
        for (i = 0; i < sizeof(solid_colors)/sizeof(solid_colors[0]); i++)
        {
            brush->lbStyle = BS_SOLID;
            brush->lbColor = solid_colors[i];
            brush->lbHatch = 0;
            retval = lpEnumFunc( SEGPTR_GET(brush), lParam );
            TRACE("solid brush %08lx, ret=%d\n",
                         solid_colors[i], retval);
            if (!retval) break;
        }

        /* Now enumerate hatched brushes */
        if (retval) for (i = HS_HORIZONTAL; i <= HS_DIAGCROSS; i++)
        {
            brush->lbStyle = BS_HATCHED;
            brush->lbColor = RGB(0,0,0);
            brush->lbHatch = i;
            retval = lpEnumFunc( SEGPTR_GET(brush), lParam );
            TRACE("hatched brush %d, ret=%d\n",
                         i, retval);
            if (!retval) break;
        }
        SEGPTR_FREE(brush);
        break;

    default:
        WARN("(%d): Invalid type\n", nObjType );
        break;
    }
    return retval;
}


/***********************************************************************
 *           EnumObjects    (GDI32.89)
 */
INT WINAPI EnumObjects( HDC hdc, INT nObjType,
                            GOBJENUMPROC lpEnumFunc, LPARAM lParam )
{
    /* Solid colors to enumerate */
    static const COLORREF solid_colors[] =
    { RGB(0x00,0x00,0x00), RGB(0xff,0xff,0xff),
      RGB(0xff,0x00,0x00), RGB(0x00,0xff,0x00),
      RGB(0x00,0x00,0xff), RGB(0xff,0xff,0x00),
      RGB(0xff,0x00,0xff), RGB(0x00,0xff,0xff),
      RGB(0x80,0x00,0x00), RGB(0x00,0x80,0x00),
      RGB(0x80,0x80,0x00), RGB(0x00,0x00,0x80),
      RGB(0x80,0x00,0x80), RGB(0x00,0x80,0x80),
      RGB(0x80,0x80,0x80), RGB(0xc0,0xc0,0xc0)
    };
    
    INT i, retval = 0;
    LOGPEN pen;
    LOGBRUSH brush;

    TRACE("%04x %d %08lx %08lx\n",
                 hdc, nObjType, (DWORD)lpEnumFunc, lParam );
    switch(nObjType)
    {
    case OBJ_PEN:
        /* Enumerate solid pens */
        for (i = 0; i < sizeof(solid_colors)/sizeof(solid_colors[0]); i++)
        {
            pen.lopnStyle   = PS_SOLID;
            pen.lopnWidth.x = 1;
            pen.lopnWidth.y = 0;
            pen.lopnColor   = solid_colors[i];
            retval = lpEnumFunc( &pen, lParam );
            TRACE("solid pen %08lx, ret=%d\n",
                         solid_colors[i], retval);
            if (!retval) break;
        }
        break;

    case OBJ_BRUSH:
        /* Enumerate solid brushes */
        for (i = 0; i < sizeof(solid_colors)/sizeof(solid_colors[0]); i++)
        {
            brush.lbStyle = BS_SOLID;
            brush.lbColor = solid_colors[i];
            brush.lbHatch = 0;
            retval = lpEnumFunc( &brush, lParam );
            TRACE("solid brush %08lx, ret=%d\n",
                         solid_colors[i], retval);
            if (!retval) break;
        }

        /* Now enumerate hatched brushes */
        if (retval) for (i = HS_HORIZONTAL; i <= HS_DIAGCROSS; i++)
        {
            brush.lbStyle = BS_HATCHED;
            brush.lbColor = RGB(0,0,0);
            brush.lbHatch = i;
            retval = lpEnumFunc( &brush, lParam );
            TRACE("hatched brush %d, ret=%d\n",
                         i, retval);
            if (!retval) break;
        }
        break;

    default:
        /* FIXME: implement Win32 types */
        WARN("(%d): Invalid type\n", nObjType );
        break;
    }
    return retval;
}


/***********************************************************************
 *           IsGDIObject    (GDI.462)
 * 
 * returns type of object if valid (W95 system programming secrets p. 264-5)
 */
BOOL16 WINAPI IsGDIObject16( HGDIOBJ16 handle )
{
    UINT16 magic = 0;

    GDIOBJHDR *object = GDI_GetObjPtr( handle, MAGIC_DONTCARE );
    if (object)
    {
        magic = GDIMAGIC(object->wMagic) - PEN_MAGIC + 1;
        GDI_ReleaseObj( handle );
    }
    return magic;
}


/***********************************************************************
 *           SetObjectOwner16    (GDI.461)
 */
void WINAPI SetObjectOwner16( HGDIOBJ16 handle, HANDLE16 owner )
{
    /* Nothing to do */
}


/***********************************************************************
 *           SetObjectOwner    (GDI32.386)
 */
void WINAPI SetObjectOwner( HGDIOBJ handle, HANDLE owner )
{
    /* Nothing to do */
}


/***********************************************************************
 *           MakeObjectPrivate    (GDI.463)
 *
 * What does that mean ?
 * Some little docu can be found in "Undocumented Windows",
 * but this is basically useless.
 * At least we know that this flags the GDI object's wMagic
 * with 0x2000 (OBJECT_PRIVATE), so we just do it.
 * But Wine doesn't react on that yet.
 */
void WINAPI MakeObjectPrivate16( HGDIOBJ16 handle, BOOL16 private )
{
    GDIOBJHDR *ptr = GDI_GetObjPtr( handle, MAGIC_DONTCARE );
    if (!ptr)
    {
	ERR("invalid GDI object %04x !\n", handle);
	return;
    }
    ptr->wMagic |= OBJECT_PRIVATE;
    GDI_ReleaseObj( handle );
}


/***********************************************************************
 *           GdiFlush    (GDI32.128)
 */
BOOL WINAPI GdiFlush(void)
{
    return TRUE;  /* FIXME */
}


/***********************************************************************
 *           GdiGetBatchLimit    (GDI32.129)
 */
DWORD WINAPI GdiGetBatchLimit(void)
{
    return 1;  /* FIXME */
}


/***********************************************************************
 *           GdiSetBatchLimit    (GDI32.139)
 */
DWORD WINAPI GdiSetBatchLimit( DWORD limit )
{
    return 1; /* FIXME */
}


/***********************************************************************
 *           GdiSeeGdiDo   (GDI.452)
 */
DWORD WINAPI GdiSeeGdiDo16( WORD wReqType, WORD wParam1, WORD wParam2,
                          WORD wParam3 )
{
    switch (wReqType)
    {
    case 0x0001:  /* LocalAlloc */
        return LOCAL_Alloc( GDI_HeapSel, wParam1, wParam3 );
    case 0x0002:  /* LocalFree */
        return LOCAL_Free( GDI_HeapSel, wParam1 );
    case 0x0003:  /* LocalCompact */
        return LOCAL_Compact( GDI_HeapSel, wParam3, 0 );
    case 0x0103:  /* LocalHeap */
        return GDI_HeapSel;
    default:
        WARN("(wReqType=%04x): Unknown\n", wReqType);
        return (DWORD)-1;
    }
}

/***********************************************************************
 *           GdiSignalProc     (GDI.610)
 */
WORD WINAPI GdiSignalProc( UINT uCode, DWORD dwThreadOrProcessID,
                           DWORD dwFlags, HMODULE16 hModule )
{
    return 0;
}

/***********************************************************************
 *           FinalGdiInit16     (GDI.405)
 */
void WINAPI FinalGdiInit16( HANDLE16 unknown )
{
}

/***********************************************************************
 *           GdiFreeResources   (GDI.609)
 */
WORD WINAPI GdiFreeResources16( DWORD reserve )
{
   return (WORD)( (int)LOCAL_CountFree( GDI_HeapSel ) * 100 /
                  (int)LOCAL_HeapSize( GDI_HeapSel ) );
}

/***********************************************************************
 *           MulDiv16   (GDI.128)
 */
INT16 WINAPI MulDiv16(
	     INT16 nMultiplicand, 
	     INT16 nMultiplier,
	     INT16 nDivisor)
{
    INT ret;
    if (!nDivisor) return -32768;
    /* We want to deal with a positive divisor to simplify the logic. */
    if (nDivisor < 0)
    {
      nMultiplicand = - nMultiplicand;
      nDivisor = -nDivisor;
    }
    /* If the result is positive, we "add" to round. else, 
     * we subtract to round. */
    if ( ( (nMultiplicand <  0) && (nMultiplier <  0) ) ||
	 ( (nMultiplicand >= 0) && (nMultiplier >= 0) ) )
        ret = (((int)nMultiplicand * nMultiplier) + (nDivisor/2)) / nDivisor;
    else
        ret = (((int)nMultiplicand * nMultiplier) - (nDivisor/2)) / nDivisor;
    if ((ret > 32767) || (ret < -32767)) return -32768;
    return (INT16) ret;
}


/*******************************************************************
 *      GetColorAdjustment [GDI32.164]
 *
 *
 */
BOOL WINAPI GetColorAdjustment(HDC hdc, LPCOLORADJUSTMENT lpca)
{
        FIXME("GetColorAdjustment, stub\n");
        return 0;
}

/*******************************************************************
 *      GetMiterLimit [GDI32.201]
 *
 *
 */
BOOL WINAPI GetMiterLimit(HDC hdc, PFLOAT peLimit)
{
        FIXME("GetMiterLimit, stub\n");
        return 0;
}

/*******************************************************************
 *      SetMiterLimit [GDI32.325]
 *
 *
 */
BOOL WINAPI SetMiterLimit(HDC hdc, FLOAT eNewLimit, PFLOAT peOldLimit)
{
        FIXME("SetMiterLimit, stub\n");
        return 0;
}

/*******************************************************************
 *      GdiComment [GDI32.109]
 *
 *
 */
BOOL WINAPI GdiComment(HDC hdc, UINT cbSize, const BYTE *lpData)
{
        FIXME("GdiComment, stub\n");
        return 0;
}
/*******************************************************************
 *      SetColorAdjustment [GDI32.309]
 *
 *
 */
BOOL WINAPI SetColorAdjustment(HDC hdc, const COLORADJUSTMENT* lpca)
{
        FIXME("SetColorAdjustment, stub\n");
        return 0;
}
 
