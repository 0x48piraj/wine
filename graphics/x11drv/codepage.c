/*
 * X11 codepage handling
 *
 * Copyright 2000 Hidenori Takeshima <hidenori@a2.ctktv.ne.jp>
 */

#include "config.h"

#include "ts_xlib.h"

#include <math.h>

#include "windef.h"
#include "winnls.h"
#include "heap.h"
#include "x11font.h"
#include "debugtools.h"

DEFAULT_DEBUG_CHANNEL(text);

/***********************************************************************
 *           IsLegalDBCSChar for cp932/936/949/950
 */
static inline
int IsLegalDBCSChar_cp932( BYTE lead, BYTE trail )
{
    return ( ( ( lead >= (BYTE)0x81 && lead <= (BYTE)0x9f ) ||
	       ( lead >= (BYTE)0xe0 && lead <= (BYTE)0xfc ) ) &&
	     ( ( trail >= (BYTE)0x40 && trail <= (BYTE)0x7e ) ||
	       ( trail >= (BYTE)0x80 && trail <= (BYTE)0xfc ) ) );
}

static inline
int IsLegalDBCSChar_cp936( BYTE lead, BYTE trail )
{
    return ( ( lead >= (BYTE)0x81 && lead <= (BYTE)0xfe ) &&
	     ( trail >= (BYTE)0x40 && trail <= (BYTE)0xfe ) );
}

static inline
int IsLegalDBCSChar_cp949( BYTE lead, BYTE trail )
{
    return ( ( lead >= (BYTE)0x81 && lead <= (BYTE)0xfe ) &&
	     ( trail >= (BYTE)0x41 && trail <= (BYTE)0xfe ) );
}

static inline
int IsLegalDBCSChar_euckr( BYTE lead, BYTE trail )
{
    return ( ( lead >= (BYTE)0xa1 && lead <= (BYTE)0xfe ) &&
	     ( trail >= (BYTE)0xa1 && trail <= (BYTE)0xfe ) );
}

static inline
int IsLegalDBCSChar_cp950( BYTE lead, BYTE trail )
{
    return (   ( lead >= (BYTE)0x81 && lead <= (BYTE)0xfe ) &&
	     ( ( trail >= (BYTE)0x40 && trail <= (BYTE)0x7e ) ||
	       ( trail >= (BYTE)0xa1 && trail <= (BYTE)0xfe ) ) );
}

/***********************************************************************
 *           DBCSCharToXChar2b for cp932/949
 */

static inline
void DBCSCharToXChar2b_cp932( XChar2b* pch, BYTE lead, BYTE trail )
{
    unsigned int  high, low;

    high = (unsigned int)lead;
    low = (unsigned int)trail;

    if ( high <= 0x9f )
	high = (high<<1) - 0xe0;
    else
	high = (high<<1) - 0x160;
    if ( low < 0x9f )
    {
	high --;
	if ( low < 0x7f )
	    low -= 0x1f;
	else
	    low -= 0x20;
    }
    else
    {
	low -= 0x7e;
    }

    pch->byte1 = (unsigned char)high;
    pch->byte2 = (unsigned char)low;
}

static inline
void DBCSCharToXChar2b_euckr( XChar2b* pch, BYTE lead, BYTE trail )
{
    pch->byte1 = lead & (BYTE)0x7f;
    pch->byte2 = trail & (BYTE)0x7f;
}




static WORD X11DRV_enum_subfont_charset_normal( UINT index )
{
    return DEFAULT_CHARSET;
}

static WORD X11DRV_enum_subfont_charset_cp932( UINT index )
{
    switch ( index )
    {
    case 0: return X11FONT_JISX0201_CHARSET;
    case 1: return X11FONT_JISX0212_CHARSET;
    }

    return DEFAULT_CHARSET;
}

static WORD X11DRV_enum_subfont_charset_cp936( UINT index )
{
    FIXME( "please implement X11DRV_enum_subfont_charset_cp936!\n" );
    return DEFAULT_CHARSET;
}

static WORD X11DRV_enum_subfont_charset_cp949( UINT index )
{
    switch ( index )
    {
    case 0: return ANSI_CHARSET;
    }

    return DEFAULT_CHARSET;
}

static WORD X11DRV_enum_subfont_charset_cp950( UINT index )
{
    FIXME( "please implement X11DRV_enum_subfont_charset_cp950!\n" );
    return DEFAULT_CHARSET;
}


static XChar2b* X11DRV_unicode_to_char2b_sbcs( fontObject* pfo,
                                               LPCWSTR lpwstr, UINT count )
{
    XChar2b *str2b;
    UINT i;
    BYTE *str;
    UINT codepage = pfo->fi->codepage;
    char ch = pfo->fs->default_char;

    if (!(str2b = HeapAlloc( GetProcessHeap(), 0, count * sizeof(XChar2b) )))
	return NULL;
    if (!(str = HeapAlloc( GetProcessHeap(), 0, count )))
    {
	HeapFree( GetProcessHeap(), 0, str2b );
	return NULL;
    }

    WideCharToMultiByte( codepage, 0, lpwstr, count, str, count, &ch, NULL );

    for (i = 0; i < count; i++)
    {
	str2b[i].byte1 = 0;
	str2b[i].byte2 = str[i];
    }
    HeapFree( GetProcessHeap(), 0, str );

    return str2b;
}

static XChar2b* X11DRV_unicode_to_char2b_unicode( fontObject* pfo,
                                                  LPCWSTR lpwstr, UINT count )
{
    XChar2b *str2b;
    UINT i;

    if (!(str2b = HeapAlloc( GetProcessHeap(), 0, count * sizeof(XChar2b) )))
	return NULL;

    for (i = 0; i < count; i++)
    {
	str2b[i].byte1 = lpwstr[i] >> 8;
	str2b[i].byte2 = lpwstr[i] & 0xff;
    }

    return str2b;
}

/* FIXME: handle jisx0212.1990... */
static XChar2b* X11DRV_unicode_to_char2b_cp932( fontObject* pfo,
                                                LPCWSTR lpwstr, UINT count )
{
    XChar2b *str2b;
    XChar2b *str2b_dst;
    BYTE *str;
    BYTE *str_src;
    UINT i;
    char ch = pfo->fs->default_char;

    if (!(str2b = HeapAlloc( GetProcessHeap(), 0, count * sizeof(XChar2b) )))
	return NULL;
    if (!(str = HeapAlloc( GetProcessHeap(), 0, count*2 )))
    {
	HeapFree( GetProcessHeap(), 0, str2b );
	return NULL;
    }

    /* handle jisx0212.1990... */
    WideCharToMultiByte( 932, 0, lpwstr, count, str, count*2, &ch, NULL );

    str_src = str;
    str2b_dst = str2b;
    for (i = 0; i < count; i++, str_src++, str2b_dst++)
    {
	if ( IsLegalDBCSChar_cp932( *str_src, *(str_src+1) ) )
	{
	    DBCSCharToXChar2b_cp932( str2b_dst, *str_src, *(str_src+1) );
	    str_src++;
	}
	else
	{
	    str2b_dst->byte1 = 0;
	    str2b_dst->byte2 = *str_src;
	}
    }

    HeapFree( GetProcessHeap(), 0, str );

    return str2b;
}


static XChar2b* X11DRV_unicode_to_char2b_cp936( fontObject* pfo,
                                                LPCWSTR lpwstr, UINT count )
{
    FIXME( "please implement X11DRV_unicode_to_char2b_cp936!\n" );
    return NULL;
}

static XChar2b* X11DRV_unicode_to_char2b_cp949( fontObject* pfo,
                                                LPCWSTR lpwstr, UINT count )
{
    XChar2b *str2b;
    XChar2b *str2b_dst;
    BYTE *str;
    BYTE *str_src;
    UINT i;
    char ch = pfo->fs->default_char;

    if (!(str2b = HeapAlloc( GetProcessHeap(), 0, count * sizeof(XChar2b) )))
	return NULL;
    if (!(str = HeapAlloc( GetProcessHeap(), 0, count*2 )))
    {
	HeapFree( GetProcessHeap(), 0, str2b );
	return NULL;
    }
    WideCharToMultiByte( 949, 0, lpwstr, count, str, count*2, &ch, NULL );

    str_src = str;
    str2b_dst = str2b;
    for (i = 0; i < count; i++, str_src++, str2b_dst++)
    {
	if ( IsLegalDBCSChar_cp949( *str_src, *(str_src+1) ) )
	{
	    if ( IsLegalDBCSChar_euckr( *str_src, *(str_src+1) ) )
	    {
		DBCSCharToXChar2b_euckr( str2b_dst, *str_src, *(str_src+1) );
	    }
	    else
	    {
		/* FIXME */
		str2b_dst->byte1 = 0;
		str2b_dst->byte2 = 0;
	    }
	    str_src++;
	}
	else
	{
	    str2b_dst->byte1 = 0;
	    str2b_dst->byte2 = *str_src;
	}
    }

    HeapFree( GetProcessHeap(), 0, str );

    return str2b;
}


static XChar2b* X11DRV_unicode_to_char2b_cp950( fontObject* pfo,
                                                LPCWSTR lpwstr, UINT count )
{
    FIXME( "please implement X11DRV_unicode_to_char2b_cp950!\n" );
    return NULL;
}


static void X11DRV_DrawString_normal( fontObject* pfo, Display* pdisp,
                                      Drawable d, GC gc, int x, int y,
                                      XChar2b* pstr, int count )
{
    TSXDrawString16( pdisp, d, gc, x, y, pstr, count );
}

static int X11DRV_TextWidth_normal( fontObject* pfo, XChar2b* pstr, int count )
{
    return TSXTextWidth16( pfo->fs, pstr, count );
}

static void X11DRV_DrawText_normal( fontObject* pfo, Display* pdisp, Drawable d,
                                    GC gc, int x, int y, XTextItem16* pitems,
                                    int count )
{
    TSXDrawText16( pdisp, d, gc, x, y, pitems, count );
}

static void X11DRV_TextExtents_normal( fontObject* pfo, XChar2b* pstr, int count,
                                       int* pdir, int* pascent, int* pdescent,
                                       int* pwidth )
{
    XCharStruct info;

    TSXTextExtents16( pfo->fs, pstr, count, pdir, pascent, pdescent, &info );
    *pwidth = info.width;
}

static void X11DRV_GetTextMetricsA_normal( fontObject* pfo, LPTEXTMETRICA pTM )
{
    LPIFONTINFO16 pdf = &pfo->fi->df;

    if( ! pfo->lpX11Trans ) {
      pTM->tmAscent = pfo->fs->ascent;
      pTM->tmDescent = pfo->fs->descent;
    } else {
      pTM->tmAscent = pfo->lpX11Trans->ascent;
      pTM->tmDescent = pfo->lpX11Trans->descent;
    }

    pTM->tmAscent *= pfo->rescale;
    pTM->tmDescent *= pfo->rescale;

    pTM->tmHeight = pTM->tmAscent + pTM->tmDescent;

    pTM->tmAveCharWidth = pfo->foAvgCharWidth * pfo->rescale;
    pTM->tmMaxCharWidth = pfo->foMaxCharWidth * pfo->rescale;

    pTM->tmInternalLeading = pfo->foInternalLeading * pfo->rescale;
    pTM->tmExternalLeading = pdf->dfExternalLeading * pfo->rescale;

    pTM->tmStruckOut = (pfo->fo_flags & FO_SYNTH_STRIKEOUT )
			? 1 : pdf->dfStrikeOut;
    pTM->tmUnderlined = (pfo->fo_flags & FO_SYNTH_UNDERLINE )
			? 1 : pdf->dfUnderline;

    pTM->tmOverhang = 0;
    if( pfo->fo_flags & FO_SYNTH_ITALIC ) 
    {
	pTM->tmOverhang += pTM->tmHeight/3;
	pTM->tmItalic = 1;
    } else 
	pTM->tmItalic = pdf->dfItalic;

    pTM->tmWeight = pdf->dfWeight;
    if( pfo->fo_flags & FO_SYNTH_BOLD ) 
    {
	pTM->tmOverhang++; 
	pTM->tmWeight += 100;
    } 

    pTM->tmFirstChar = pdf->dfFirstChar;
    pTM->tmLastChar = pdf->dfLastChar;
    pTM->tmDefaultChar = pdf->dfDefaultChar;
    pTM->tmBreakChar = pdf->dfBreakChar;

    pTM->tmCharSet = pdf->dfCharSet;
    pTM->tmPitchAndFamily = pdf->dfPitchAndFamily;

    pTM->tmDigitizedAspectX = pdf->dfHorizRes;
    pTM->tmDigitizedAspectY = pdf->dfVertRes;
}



static
void X11DRV_DrawString_dbcs( fontObject* pfo, Display* pdisp,
                             Drawable d, GC gc, int x, int y,
                             XChar2b* pstr, int count )
{
    XTextItem16 item;

    item.chars = pstr;
    item.delta = 0;
    item.nchars = count;
    item.font = None;
    X11DRV_cptable[pfo->fi->cptable].pDrawText(
		pfo, pdisp, d, gc, x, y, &item, 1 );
}

static
int X11DRV_TextWidth_dbcs_2fonts( fontObject* pfo, XChar2b* pstr, int count )
{
    int i;
    int width;
    int curfont;
    fontObject* pfos[X11FONT_REFOBJS_MAX+1];

    pfos[0] = XFONT_GetFontObject( pfo->prefobjs[0] );
    pfos[1] = pfo;
    if ( pfos[0] == NULL ) pfos[0] = pfo;

    width = 0;
    for ( i = 0; i < count; i++ )
    {
	curfont = ( pstr->byte1 != 0 ) ? 1 : 0;
	width += TSXTextWidth16( pfos[curfont]->fs, pstr, 1 );
	pstr ++;
    }

    return width;
}

static
void X11DRV_DrawText_dbcs_2fonts( fontObject* pfo, Display* pdisp, Drawable d,
                                  GC gc, int x, int y, XTextItem16* pitems,
                                  int count )
{
    int i, nitems, prevfont = -1, curfont;
    XChar2b* pstr;
    XTextItem16* ptibuf;
    XTextItem16* pti;
    fontObject* pfos[X11FONT_REFOBJS_MAX+1];

    pfos[0] = XFONT_GetFontObject( pfo->prefobjs[0] );
    pfos[1] = pfo;
    if ( pfos[0] == NULL ) pfos[0] = pfo;

    nitems = 0;
    for ( i = 0; i < count; i++ )
	nitems += pitems->nchars;
    ptibuf = HeapAlloc( GetProcessHeap(), 0, sizeof(XTextItem16) * nitems );
    if ( ptibuf == NULL )
	return; /* out of memory */

    pti = ptibuf;
    while ( count-- > 0 )
    {
	pti->chars = pstr = pitems->chars;
	pti->delta = pitems->delta;
	pti->font = None;
	for ( i = 0; i < pitems->nchars; i++, pstr++ )
	{
	    curfont = ( pstr->byte1 != 0 ) ? 1 : 0;
	    if ( curfont != prevfont )
	    {
		if ( pstr != pti->chars )
		{
		    pti->nchars = pstr - pti->chars;
		    pti ++;
		    pti->chars = pstr;
		    pti->delta = 0;
		}
		pti->font = pfos[curfont]->fs->fid;
		prevfont = curfont;
	    }
	}
	pti->nchars = pstr - pti->chars;
	pitems ++; pti ++;
    }
    TSXDrawText16( pdisp, d, gc, x, y, ptibuf, pti - ptibuf );
    HeapFree( GetProcessHeap(), 0, ptibuf );
}

static
void X11DRV_TextExtents_dbcs_2fonts( fontObject* pfo, XChar2b* pstr, int count,
                                     int* pdir, int* pascent, int* pdescent,
                                     int* pwidth )
{
    XCharStruct info;
    int ascent, descent, width;
    int i;
    int curfont;
    fontObject* pfos[X11FONT_REFOBJS_MAX+1];

    pfos[0] = XFONT_GetFontObject( pfo->prefobjs[0] );
    pfos[1] = pfo;
    if ( pfos[0] == NULL ) pfos[0] = pfo;

    width = 0;
    *pascent = 0;
    *pdescent = 0;
    for ( i = 0; i < count; i++ )
    {
	curfont = ( pstr->byte1 != 0 ) ? 1 : 0;
	TSXTextExtents16( pfos[curfont]->fs, pstr, 1, pdir,
			  &ascent, &descent, &info );
	if ( *pascent < ascent ) *pascent = ascent;
	if ( *pdescent < descent ) *pdescent = descent;
	width += info.width;

	pstr ++;
    }

    *pwidth = width;
}

static void X11DRV_GetTextMetricsA_cp932( fontObject* pfo, LPTEXTMETRICA pTM )
{
    fontObject* pfo_ansi = XFONT_GetFontObject( pfo->prefobjs[0] );
    LPIFONTINFO16 pdf = &pfo->fi->df;
    LPIFONTINFO16 pdf_ansi;

    pdf_ansi = ( pfo_ansi != NULL ) ? (&pfo_ansi->fi->df) : pdf;

    if( ! pfo->lpX11Trans ) {
      pTM->tmAscent = pfo->fs->ascent;
      pTM->tmDescent = pfo->fs->descent;
    } else {
      pTM->tmAscent = pfo->lpX11Trans->ascent;
      pTM->tmDescent = pfo->lpX11Trans->descent;
    }

    pTM->tmAscent *= pfo->rescale;
    pTM->tmDescent *= pfo->rescale;

    pTM->tmHeight = pTM->tmAscent + pTM->tmDescent;

    if ( pfo_ansi != NULL )
    {
	pTM->tmAveCharWidth = floor((pfo_ansi->foAvgCharWidth * 2.0 + pfo->foAvgCharWidth) / 3.0 * pfo->rescale + 0.5);
	pTM->tmMaxCharWidth = __max(pfo_ansi->foMaxCharWidth, pfo->foMaxCharWidth) * pfo->rescale;
    }
    else
    {
	pTM->tmAveCharWidth = floor((pfo->foAvgCharWidth * pfo->rescale + 1.0) / 2.0);
	pTM->tmMaxCharWidth = pfo->foMaxCharWidth * pfo->rescale;
    }

    pTM->tmInternalLeading = pfo->foInternalLeading * pfo->rescale;
    pTM->tmExternalLeading = pdf->dfExternalLeading * pfo->rescale;

    pTM->tmStruckOut = (pfo->fo_flags & FO_SYNTH_STRIKEOUT )
			? 1 : pdf->dfStrikeOut;
    pTM->tmUnderlined = (pfo->fo_flags & FO_SYNTH_UNDERLINE )
			? 1 : pdf->dfUnderline;

    pTM->tmOverhang = 0;
    if( pfo->fo_flags & FO_SYNTH_ITALIC ) 
    {
	pTM->tmOverhang += pTM->tmHeight/3;
	pTM->tmItalic = 1;
    } else 
	pTM->tmItalic = pdf->dfItalic;

    pTM->tmWeight = pdf->dfWeight;
    if( pfo->fo_flags & FO_SYNTH_BOLD ) 
    {
	pTM->tmOverhang++; 
	pTM->tmWeight += 100;
    } 

    pTM->tmFirstChar = pdf_ansi->dfFirstChar;
    pTM->tmLastChar = pdf_ansi->dfLastChar;
    pTM->tmDefaultChar = pdf_ansi->dfDefaultChar;
    pTM->tmBreakChar = pdf_ansi->dfBreakChar;

    pTM->tmCharSet = pdf->dfCharSet;
    pTM->tmPitchAndFamily = pdf->dfPitchAndFamily;

    pTM->tmDigitizedAspectX = pdf->dfHorizRes;
    pTM->tmDigitizedAspectY = pdf->dfVertRes;
}





const X11DRV_CP X11DRV_cptable[X11DRV_CPTABLE_COUNT] =
{
    { /* SBCS */
	X11DRV_enum_subfont_charset_normal,
	X11DRV_unicode_to_char2b_sbcs,
	X11DRV_DrawString_normal,
	X11DRV_TextWidth_normal,
	X11DRV_DrawText_normal,
	X11DRV_TextExtents_normal,
	X11DRV_GetTextMetricsA_normal,
    },
    { /* UNICODE */
	X11DRV_enum_subfont_charset_normal,
	X11DRV_unicode_to_char2b_unicode,
	X11DRV_DrawString_normal,
	X11DRV_TextWidth_normal,
	X11DRV_DrawText_normal,
	X11DRV_TextExtents_normal,
        X11DRV_GetTextMetricsA_normal,
    },
    { /* CP932 */
	X11DRV_enum_subfont_charset_cp932,
	X11DRV_unicode_to_char2b_cp932,
	X11DRV_DrawString_dbcs,
	X11DRV_TextWidth_dbcs_2fonts,
	X11DRV_DrawText_dbcs_2fonts,
	X11DRV_TextExtents_dbcs_2fonts,
        X11DRV_GetTextMetricsA_cp932,
    },
    { /* CP936 */
	X11DRV_enum_subfont_charset_cp936,
	X11DRV_unicode_to_char2b_cp936,
	X11DRV_DrawString_normal, /* FIXME */
	X11DRV_TextWidth_normal, /* FIXME */
	X11DRV_DrawText_normal, /* FIXME */
	X11DRV_TextExtents_normal, /* FIXME */
        X11DRV_GetTextMetricsA_normal, /* FIXME */
    },
    { /* CP949 */
	X11DRV_enum_subfont_charset_cp949,
	X11DRV_unicode_to_char2b_cp949,
	X11DRV_DrawString_dbcs,
	X11DRV_TextWidth_dbcs_2fonts,
	X11DRV_DrawText_dbcs_2fonts,
	X11DRV_TextExtents_dbcs_2fonts,
        X11DRV_GetTextMetricsA_normal, /* FIXME */
    },
    { /* CP950 */
	X11DRV_enum_subfont_charset_cp950,
	X11DRV_unicode_to_char2b_cp950,
	X11DRV_DrawString_normal, /* FIXME */
	X11DRV_TextWidth_normal, /* FIXME */
	X11DRV_DrawText_normal, /* FIXME */
	X11DRV_TextExtents_normal, /* FIXME */
        X11DRV_GetTextMetricsA_normal, /* FIXME */
    },
};
