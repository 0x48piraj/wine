/*
 * USER text functions
 *
 * Copyright 1993, 1994 Alexandre Julliard
 *
 */

#include <string.h>

#include "windef.h"
#include "wingdi.h"
#include "wine/winuser16.h"
#include "winbase.h"
#include "winerror.h"
#include "winnls.h"
#include "cache.h"
#include "ldt.h"
#include "debugtools.h"

DEFAULT_DEBUG_CHANNEL(text);

#define TAB     9
#define LF     10
#define CR     13
#define SPACE  32
#define PREFIX 38

#define ELLIPSIS "..."
#define FORWARD_SLASH '/'
#define BACK_SLASH '\\'

#define SWAP_INT(a,b)  { int t = a; a = b; b = t; }

static int tabstop = 8;
static int tabwidth;
static int spacewidth;
static int prefix_offset;

static const char *TEXT_NextLine( HDC hdc, const char *str, int *count,
                                  char *dest, int *len, int width, WORD format)
{
    /* Return next line of text from a string.
     * 
     * hdc - handle to DC.
     * str - string to parse into lines.
     * count - length of str.
     * dest - destination in which to return line.
     * len - length of resultant line in dest in chars.
     * width - maximum width of line in pixels.
     * format - format type passed to DrawText.
     *
     * Returns pointer to next char in str after end of the line
     * or NULL if end of str reached.
     */

    /* FIXME:
     * GetTextExtentPoint is used to get the width of each character, 
     * rather than GetCharABCWidth...  So the whitespace between
     * characters is ignored, and the reported len is too great.
     */

    int i = 0, j = 0, k;
    int plen = 0;
    int numspaces;
    SIZE size;
    int lasttab = 0;
    int wb_i = 0, wb_j = 0, wb_count = 0;

    while (*count)
    {
	switch (str[i])
	{
	case CR:
	case LF:
	    if (!(format & DT_SINGLELINE))
	    {
		if ((*count > 1) && (str[i] == CR) && (str[i+1] == LF))
                {
		    (*count)--;
                    i++;
                }
		i++;
		*len = j;
		(*count)--;
		return (&str[i]);
	    }
	    dest[j++] = str[i++];
	    if (!(format & DT_NOCLIP) || !(format & DT_NOPREFIX) ||
		(format & DT_WORDBREAK))
	    {
		if (!GetTextExtentPointA(hdc, &dest[j-1], 1, &size))
		    return NULL;
		plen += size.cx;
	    }
	    break;
	    
	case PREFIX:
	    if (!(format & DT_NOPREFIX) && *count > 1)
                {
                if (str[++i] == PREFIX)
		    (*count)--;
		else {
                    prefix_offset = j;
                    break;
                }
	    }
            dest[j++] = str[i++];
            if (!(format & DT_NOCLIP) || !(format & DT_NOPREFIX) ||
                (format & DT_WORDBREAK))
            {
                if (!GetTextExtentPointA(hdc, &dest[j-1], 1, &size))
                    return NULL;
                plen += size.cx;
            }
	    break;
	    
	case TAB:
	    if (format & DT_EXPANDTABS)
	    {
		wb_i = ++i;
		wb_j = j;
		wb_count = *count;

		if (!GetTextExtentPointA(hdc, &dest[lasttab], j - lasttab, &size))
		    return NULL;

		numspaces = (tabwidth - size.cx) / spacewidth;
		for (k = 0; k < numspaces; k++)
		    dest[j++] = SPACE;
		plen += tabwidth - size.cx;
		lasttab = wb_j + numspaces;
	    }
	    else
	    {
		dest[j++] = str[i++];
		if (!(format & DT_NOCLIP) || !(format & DT_NOPREFIX) ||
		    (format & DT_WORDBREAK))
		{
		    if (!GetTextExtentPointA(hdc, &dest[j-1], 1, &size))
			return NULL;
		    plen += size.cx;
		}
	    }
	    break;

	case SPACE:
	    dest[j++] = str[i++];
	    if (!(format & DT_NOCLIP) || !(format & DT_NOPREFIX) ||
		(format & DT_WORDBREAK))
	    {
		wb_i = i;
		wb_j = j - 1;
		wb_count = *count;
		if (!GetTextExtentPointA(hdc, &dest[j-1], 1, &size))
		    return NULL;
		plen += size.cx;
	    }
	    break;

	default:
	    dest[j++] = str[i++];
	    if (!(format & DT_NOCLIP) || !(format & DT_NOPREFIX) ||
		(format & DT_WORDBREAK))
	    {
		if (!GetTextExtentPointA(hdc, &dest[j-1], 1, &size))
		    return NULL;
		plen += size.cx;
	    }
	}

	(*count)--;
	if (!(format & DT_NOCLIP) || (format & DT_WORDBREAK))
	{
	    if (plen > width)
	    {
		if (format & DT_WORDBREAK)
		{
		    if (wb_j)
		    {
			*len = wb_j;
			*count = wb_count - 1;
			return (&str[wb_i]);
		    }
		}
		else
		{
		    *len = j;
		    return (&str[i]);
		}
	    }
	}
    }
    
    *len = j;
    return NULL;
}


/***********************************************************************
 *           DrawText16    (USER.85)
 */
INT16 WINAPI DrawText16( HDC16 hdc, LPCSTR str, INT16 count, LPRECT16 rect, UINT16 flags )
{
    INT16 ret;

    if (rect)
    {
        RECT rect32;
        CONV_RECT16TO32( rect, &rect32 );
        ret = DrawTextA( hdc, str, count, &rect32, flags );
        CONV_RECT32TO16( &rect32, rect );
    }
    else ret = DrawTextA( hdc, str, count, NULL, flags);
    return ret;
}


/***********************************************************************
 *           DrawTextA    (USER32.164)
 */
INT WINAPI DrawTextA( HDC hdc, LPCSTR str, INT i_count, LPRECT rect, UINT flags )
{
    SIZE size;
    const char *strPtr;
    static char line[1024];
    int len, lh, count=i_count;
    int prefix_x = 0;
    int prefix_end = 0;
    TEXTMETRICA tm;
    int x = rect->left, y = rect->top;
    int width = rect->right - rect->left;
    int max_width = 0;

    TRACE("%s, %d , [(%d,%d),(%d,%d)]\n",
	  debugstr_an (str, count), count,
	  rect->left, rect->top, rect->right, rect->bottom);

    if (!str) return 0;
    if (count == -1) count = strlen(str);
    if (count == 0) return 0;
    strPtr = str;

    GetTextMetricsA(hdc, &tm);
    if (flags & DT_EXTERNALLEADING)
	lh = tm.tmHeight + tm.tmExternalLeading;
    else
	lh = tm.tmHeight;

    if (flags & DT_TABSTOP)
	tabstop = flags >> 8;

    if (flags & DT_EXPANDTABS)
    {
	GetTextExtentPointA(hdc, " ", 1, &size);
	spacewidth = size.cx;
	GetTextExtentPointA(hdc, "o", 1, &size);
	tabwidth = size.cx * tabstop;
    }

    if (flags & DT_CALCRECT) flags |= DT_NOCLIP;

    do
    {
	prefix_offset = -1;
	strPtr = TEXT_NextLine(hdc, strPtr, &count, line, &len, width, flags);

	if (prefix_offset != -1)
	{
	    GetTextExtentPointA(hdc, line, prefix_offset, &size);
	    prefix_x = size.cx;
	    GetTextExtentPointA(hdc, line, prefix_offset + 1, &size);
	    prefix_end = size.cx - 1;
	}

	if (!GetTextExtentPointA(hdc, line, len, &size)) return 0;
	if (flags & DT_CENTER) x = (rect->left + rect->right -
				    size.cx) / 2;
	else if (flags & DT_RIGHT) x = rect->right - size.cx;

	if (flags & DT_SINGLELINE)
	{
	    if (flags & DT_VCENTER) y = rect->top + 
	    	(rect->bottom - rect->top) / 2 - size.cy / 2;
	    else if (flags & DT_BOTTOM) y = rect->bottom - size.cy;

	    if (flags & (DT_PATH_ELLIPSIS | DT_END_ELLIPSIS | DT_WORD_ELLIPSIS))
	    {
	        char swapStr[sizeof(line)];
	        char* fnameDelim = NULL;
	        int totalLen = i_count >= 0 ? i_count : strlen(str);

		if (size.cx > width)
	        {
	            int fnameLen = totalLen;

	            /* allow room for '...' */
	            count = min(totalLen+3, sizeof(line)-3);

                    if (flags & DT_WORD_ELLIPSIS)
	                flags |= DT_WORDBREAK;

		    if (flags & DT_PATH_ELLIPSIS) 
	            {
			char* lastBkSlash = NULL;
			char* lastFwdSlash = NULL;
	                strncpy(line, str, totalLen); line[totalLen] = '\0';
	                lastBkSlash = strrchr(line, BACK_SLASH);
	                lastFwdSlash = strrchr(line, FORWARD_SLASH);
			fnameDelim = lastFwdSlash ? lastFwdSlash : lastBkSlash;
			if (lastBkSlash && lastFwdSlash) /* which is last? */
			   if (lastBkSlash > lastFwdSlash)
				fnameDelim = lastBkSlash;

	                if (fnameDelim)
	                    fnameLen = &line[totalLen] - fnameDelim;
	                else 
	                    fnameDelim = (char*)str;

	                strcpy(swapStr, ELLIPSIS);
	                strncat(swapStr, fnameDelim, fnameLen);
	                swapStr[fnameLen+3] = '\0';
	                strncat(swapStr, str, totalLen - fnameLen);
	                swapStr[totalLen+3] = '\0';
                    }
                    else  /* DT_END_ELLIPSIS | DT_WORD_ELLIPSIS */
	            {
	                strcpy(swapStr, ELLIPSIS);
	                strncat(swapStr, str, totalLen);
	            }

	            TEXT_NextLine(hdc, swapStr, &count, line, &len, width, flags); 

	            /* if only the ELLIPSIS will fit, just let it be clipped */
	            len = max(3, len);
	            GetTextExtentPointA(hdc, line, len, &size);

	            /* FIXME:
	             * NextLine uses GetTextExtentPoint for each character, 
	             * rather than GetCharABCWidth...  So the whitespace between
	             * characters is ignored in the width measurement, and the 
	             * reported len is too great.  To compensate, we must get
	             * the width of the entire line and adjust len accordingly.
	            */
	            while ((size.cx > width) && (len > 3))
	            {
	                line[--len] = '\0';
	                GetTextExtentPointA(hdc, line, len, &size);
	            }

	            if (fnameLen < len-3) /* some of the path will fit */
	            {
	                /* put the ELLIPSIS between the path and filename */
	                strncpy(swapStr, &line[fnameLen+3], len-3-fnameLen); 
	                swapStr[len-3-fnameLen] = '\0';
	                strcat(swapStr, ELLIPSIS); 
	                strncat(swapStr, &line[3], fnameLen); 
	            }
	            else
	            {
	                /* move the ELLIPSIS to the end */
	                strncpy(swapStr, &line[3], len-3);
	                swapStr[len-3] = '\0';
	                strcat(swapStr, ELLIPSIS);
	            }

	            strncpy(line, swapStr, len);
	            line[len] = '\0';
	            strPtr = NULL;
	        }
	    }
	}
	if (!(flags & DT_CALCRECT))
	{
	    if (!ExtTextOutA(hdc, x, y, (flags & DT_NOCLIP) ? 0 : ETO_CLIPPED,
                             rect, line, len, NULL )) return 0;
            if (prefix_offset != -1)
            {
                HPEN hpen = CreatePen( PS_SOLID, 1, GetTextColor(hdc) );
                HPEN oldPen = SelectObject( hdc, hpen );
                MoveToEx(hdc, x + prefix_x, y + tm.tmAscent + 1, NULL );
                LineTo(hdc, x + prefix_end + 1, y + tm.tmAscent + 1 );
                SelectObject( hdc, oldPen );
                DeleteObject( hpen );
            }
	}
	else if (size.cx > max_width)
	    max_width = size.cx;

	y += lh;
	if (strPtr)
	{
	    if (!(flags & DT_NOCLIP))
	    {
		if (y > rect->bottom - lh)
		    break;
	    }
	}
    }
    while (strPtr);
    if (flags & DT_CALCRECT)
    {
	rect->right = rect->left + max_width;
	rect->bottom = y;
    }
    return y - rect->top;
}


/***********************************************************************
 *           DrawTextW    (USER32.167)
 */
INT WINAPI DrawTextW( HDC hdc, LPCWSTR str, INT count,
                          LPRECT rect, UINT flags )
{
    LPSTR p;
    INT acount;
    INT ret;
    UINT codepage = CP_ACP; /* FIXME: get codepage of font charset */
        
    acount = WideCharToMultiByte(codepage,0,str,count,NULL,0,NULL,NULL);
    p = HeapAlloc( GetProcessHeap(), 0, acount );
    acount = WideCharToMultiByte(codepage,0,str,count,p,acount,NULL,NULL);
    if (count == -1) acount = -1;
    ret = DrawTextA( hdc, p, acount, rect, flags );

    HeapFree( GetProcessHeap(), 0, p );
    return ret;
}

/***********************************************************************
 *           DrawTextExA    (USER32.165)
 */
INT WINAPI DrawTextExA( HDC hdc, LPCSTR str, INT count,
                     LPRECT rect, UINT flags, LPDRAWTEXTPARAMS dtp )
{
    TRACE("(%d,'%s',%d,%p,0x%08x,%p)\n",hdc,str,count,rect,flags,dtp);
    if(dtp) {
        FIXME("Ignores params:%d,%d,%d,%d\n",dtp->cbSize,
                   dtp->iTabLength,dtp->iLeftMargin,dtp->iRightMargin);
    }
    return DrawTextA(hdc,str,count,rect,flags);
}

/***********************************************************************
 *           DrawTextExW    (USER32.166)
 */
INT WINAPI DrawTextExW( HDC hdc, LPCWSTR str, INT count,
                     LPRECT rect, UINT flags, LPDRAWTEXTPARAMS dtp )
{
    TRACE("(%d,%p,%d,%p,0x%08x,%p)\n",hdc,str,count,rect,flags,dtp);
    FIXME("ignores extended functionality\n");
    return DrawTextW(hdc,str,count,rect,flags);
}
/***********************************************************************
 *           TEXT_GrayString
 *
 * FIXME: The call to 16-bit code only works because the wine GDI is a 16-bit
 * heap and we can guarantee that the handles fit in an INT16. We have to
 * rethink the strategy once the migration to NT handles is complete.
 * We are going to get a lot of code-duplication once this migration is
 * completed...
 * 
 */
static BOOL TEXT_GrayString(HDC hdc, HBRUSH hb, GRAYSTRINGPROC fn, LPARAM lp, INT len,
                            INT x, INT y, INT cx, INT cy, BOOL unicode, BOOL _32bit)
{
    HBITMAP hbm, hbmsave;
    HBRUSH hbsave;
    HFONT hfsave;
    HDC memdc = CreateCompatibleDC(hdc);
    int slen = len;
    BOOL retval = TRUE;
    COLORREF fg, bg;

    if(!hdc) return FALSE;
    
    if(len == 0)
    {
        if(unicode)
    	    slen = lstrlenW((LPCWSTR)lp);
    	else if(_32bit)
    	    slen = strlen((LPCSTR)lp);
    	else
    	    slen = strlen((LPCSTR)PTR_SEG_TO_LIN(lp));
    }

    if((cx == 0 || cy == 0) && slen != -1)
    {
        SIZE s;
        if(unicode)
            GetTextExtentPoint32W(hdc, (LPCWSTR)lp, slen, &s);
        else if(_32bit)
            GetTextExtentPoint32A(hdc, (LPCSTR)lp, slen, &s);
        else
            GetTextExtentPoint32A(hdc, (LPCSTR)PTR_SEG_TO_LIN(lp), slen, &s);
        if(cx == 0) cx = s.cx;
        if(cy == 0) cy = s.cy;
    }

    hbm = CreateBitmap(cx, cy, 1, 1, NULL);
    hbmsave = (HBITMAP)SelectObject(memdc, hbm);
    hbsave = SelectObject( memdc, GetStockObject(BLACK_BRUSH) );
    PatBlt( memdc, 0, 0, cx, cy, PATCOPY );
    SelectObject( memdc, hbsave );
    SetTextColor(memdc, RGB(255, 255, 255));
    SetBkColor(memdc, RGB(0, 0, 0));
    hfsave = (HFONT)SelectObject(memdc, GetCurrentObject(hdc, OBJ_FONT));
            
    if(fn)
        if(_32bit)
            retval = fn(memdc, lp, slen);
        else
            retval = (BOOL)((BOOL16)((GRAYSTRINGPROC16)fn)((HDC16)memdc, lp, (INT16)slen));
    else
        if(unicode)
            TextOutW(memdc, 0, 0, (LPCWSTR)lp, slen);
        else if(_32bit)
            TextOutA(memdc, 0, 0, (LPCSTR)lp, slen);
        else
            TextOutA(memdc, 0, 0, (LPCSTR)PTR_SEG_TO_LIN(lp), slen);

    SelectObject(memdc, hfsave);

/*
 * Windows doc says that the bitmap isn't grayed when len == -1 and
 * the callback function returns FALSE. However, testing this on
 * win95 showed otherwise...
*/
#ifdef GRAYSTRING_USING_DOCUMENTED_BEHAVIOUR
    if(retval || len != -1)
#endif
    {
        hbsave = (HBRUSH)SelectObject(memdc, CACHE_GetPattern55AABrush());
        PatBlt(memdc, 0, 0, cx, cy, 0x000A0329);
        SelectObject(memdc, hbsave);
    }

    if(hb) hbsave = (HBRUSH)SelectObject(hdc, hb);
    fg = SetTextColor(hdc, RGB(0, 0, 0));
    bg = SetBkColor(hdc, RGB(255, 255, 255));
    BitBlt(hdc, x, y, cx, cy, memdc, 0, 0, 0x00E20746);
    SetTextColor(hdc, fg);
    SetBkColor(hdc, bg);
    if(hb) SelectObject(hdc, hbsave);

    SelectObject(memdc, hbmsave);
    DeleteObject(hbm);
    DeleteDC(memdc);
    return retval;
}


/***********************************************************************
 *           GrayString16   (USER.185)
 */
BOOL16 WINAPI GrayString16( HDC16 hdc, HBRUSH16 hbr, GRAYSTRINGPROC16 gsprc,
                            LPARAM lParam, INT16 cch, INT16 x, INT16 y,
                            INT16 cx, INT16 cy )
{
    return TEXT_GrayString(hdc, hbr, (GRAYSTRINGPROC)gsprc, lParam, cch, x, y, cx, cy, FALSE, FALSE);
}


/***********************************************************************
 *           GrayStringA   (USER32.315)
 */
BOOL WINAPI GrayStringA( HDC hdc, HBRUSH hbr, GRAYSTRINGPROC gsprc,
                             LPARAM lParam, INT cch, INT x, INT y,
                             INT cx, INT cy )
{
    return TEXT_GrayString(hdc, hbr, gsprc, lParam, cch, x, y, cx, cy, FALSE, TRUE);
}


/***********************************************************************
 *           GrayStringW   (USER32.316)
 */
BOOL WINAPI GrayStringW( HDC hdc, HBRUSH hbr, GRAYSTRINGPROC gsprc,
                             LPARAM lParam, INT cch, INT x, INT y,
                             INT cx, INT cy )
{
    return TEXT_GrayString(hdc, hbr, gsprc, lParam, cch, x, y, cx, cy, TRUE, TRUE);
}

/***********************************************************************
 *           TEXT_TabbedTextOut
 *
 * Helper function for TabbedTextOut() and GetTabbedTextExtent().
 * Note: this doesn't work too well for text-alignment modes other
 *       than TA_LEFT|TA_TOP. But we want bug-for-bug compatibility :-)
 */
static LONG TEXT_TabbedTextOut( HDC hdc, INT x, INT y, LPCSTR lpstr,
                                INT count, INT cTabStops, const INT16 *lpTabPos16,
                                const INT *lpTabPos32, INT nTabOrg,
                                BOOL fDisplayText )
{
    INT defWidth;
    SIZE extent;
    int i, tabPos = x;
    int start = x;

    extent.cx = 0;
    extent.cy = 0;

    if (cTabStops == 1)
    {
        defWidth = lpTabPos32 ? *lpTabPos32 : *lpTabPos16;
        cTabStops = 0;
    }
    else
    {
        TEXTMETRICA tm;
        GetTextMetricsA( hdc, &tm );
        defWidth = 8 * tm.tmAveCharWidth;
    }

    while (count > 0)
    {
        for (i = 0; i < count; i++)
            if (lpstr[i] == '\t') break;
        GetTextExtentPointA( hdc, lpstr, i, &extent );
        if (lpTabPos32)
        {
            while ((cTabStops > 0) &&
                   (nTabOrg + *lpTabPos32 <= x + extent.cx))
            {
                lpTabPos32++;
                cTabStops--;
            }
        }
        else
        {
            while ((cTabStops > 0) &&
                   (nTabOrg + *lpTabPos16 <= x + extent.cx))
            {
                lpTabPos16++;
                cTabStops--;
            }
        }
        if (i == count)
            tabPos = x + extent.cx;
        else if (cTabStops > 0)
            tabPos = nTabOrg + (lpTabPos32 ? *lpTabPos32 : *lpTabPos16);
        else
            tabPos = nTabOrg + ((x + extent.cx - nTabOrg) / defWidth + 1) * defWidth;
        if (fDisplayText)
        {
            RECT r;
            r.left   = x;
            r.top    = y;
            r.right  = tabPos;
            r.bottom = y + extent.cy;
            ExtTextOutA( hdc, x, y,
                           GetBkMode(hdc) == OPAQUE ? ETO_OPAQUE : 0,
                           &r, lpstr, i, NULL );
        }
        x = tabPos;
        count -= i+1;
        lpstr += i+1;
    }
    return MAKELONG(tabPos - start, extent.cy);
}


/***********************************************************************
 *           TabbedTextOut16    (USER.196)
 */
LONG WINAPI TabbedTextOut16( HDC16 hdc, INT16 x, INT16 y, LPCSTR lpstr,
                             INT16 count, INT16 cTabStops,
                             const INT16 *lpTabPos, INT16 nTabOrg )
{
    TRACE("%04x %d,%d '%.*s' %d\n",
                  hdc, x, y, count, lpstr, count );
    return TEXT_TabbedTextOut( hdc, x, y, lpstr, count, cTabStops,
                               lpTabPos, NULL, nTabOrg, TRUE );
}


/***********************************************************************
 *           TabbedTextOutA    (USER32.542)
 */
LONG WINAPI TabbedTextOutA( HDC hdc, INT x, INT y, LPCSTR lpstr,
                              INT count, INT cTabStops,
                              const INT *lpTabPos, INT nTabOrg )
{
    TRACE("%04x %d,%d '%.*s' %d\n",
                  hdc, x, y, count, lpstr, count );
    return TEXT_TabbedTextOut( hdc, x, y, lpstr, count, cTabStops,
                               NULL, lpTabPos, nTabOrg, TRUE );
}


/***********************************************************************
 *           TabbedTextOutW    (USER32.543)
 */
LONG WINAPI TabbedTextOutW( HDC hdc, INT x, INT y, LPCWSTR str,
                              INT count, INT cTabStops,
                              const INT *lpTabPos, INT nTabOrg )
{
    LONG ret;
    LPSTR p;
    INT acount;
    UINT codepage = CP_ACP; /* FIXME: get codepage of font charset */

    acount = WideCharToMultiByte(codepage,0,str,count,NULL,0,NULL,NULL);
    p = HeapAlloc( GetProcessHeap(), 0, acount );
    if(p == NULL) return 0; /* FIXME: is this the correct return on failure */ 
    acount = WideCharToMultiByte(codepage,0,str,count,p,acount,NULL,NULL);
    ret = TabbedTextOutA( hdc, x, y, p, acount, cTabStops,
                            lpTabPos, nTabOrg );
    HeapFree( GetProcessHeap(), 0, p );
    return ret;
}


/***********************************************************************
 *           GetTabbedTextExtent16    (USER.197)
 */
DWORD WINAPI GetTabbedTextExtent16( HDC16 hdc, LPCSTR lpstr, INT16 count, 
                                    INT16 cTabStops, const INT16 *lpTabPos )
{
    TRACE("%04x '%.*s' %d\n",
                  hdc, count, lpstr, count );
    return TEXT_TabbedTextOut( hdc, 0, 0, lpstr, count, cTabStops,
                               lpTabPos, NULL, 0, FALSE );
}


/***********************************************************************
 *           GetTabbedTextExtentA    (USER32.293)
 */
DWORD WINAPI GetTabbedTextExtentA( HDC hdc, LPCSTR lpstr, INT count, 
                                     INT cTabStops, const INT *lpTabPos )
{
    TRACE("%04x '%.*s' %d\n",
                  hdc, count, lpstr, count );
    return TEXT_TabbedTextOut( hdc, 0, 0, lpstr, count, cTabStops,
                               NULL, lpTabPos, 0, FALSE );
}


/***********************************************************************
 *           GetTabbedTextExtentW    (USER32.294)
 */
DWORD WINAPI GetTabbedTextExtentW( HDC hdc, LPCWSTR lpstr, INT count, 
                                     INT cTabStops, const INT *lpTabPos )
{
    LONG ret;
    LPSTR p;
    INT acount;
    UINT codepage = CP_ACP; /* FIXME: get codepage of font charset */

    acount = WideCharToMultiByte(codepage,0,lpstr,count,NULL,0,NULL,NULL);
    p = HeapAlloc( GetProcessHeap(), 0, acount );
    if(p == NULL) return 0; /* FIXME: is this the correct failure value? */
    acount = WideCharToMultiByte(codepage,0,lpstr,count,p,acount,NULL,NULL);
    ret = GetTabbedTextExtentA( hdc, p, acount, cTabStops, lpTabPos );
    HeapFree( GetProcessHeap(), 0, p );
    return ret;
}
