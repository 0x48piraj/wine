/*
 *	PostScript output functions
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

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "gdi.h"
#include "psdrv.h"
#include "winspool.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(psdrv);

static char psheader[] = /* title llx lly urx ury orientation */
"%%!PS-Adobe-3.0\n"
"%%%%Creator: Wine PostScript Driver\n"
"%%%%Title: %s\n"
"%%%%BoundingBox: %d %d %d %d\n"
"%%%%Pages: (atend)\n"
"%%%%Orientation: %s\n"
"%%%%EndComments\n";

static char psbeginprolog[] =
"%%BeginProlog\n";

static char psendprolog[] =
"%%EndProlog\n";

static char psprolog[] =
"/tmpmtrx matrix def\n"
"/hatch {\n"
"  pathbbox\n"
"  /b exch def /r exch def /t exch def /l exch def /gap 32 def\n"
"  l cvi gap idiv gap mul\n"
"  gap\n"
"  r cvi gap idiv gap mul\n"
"  {t moveto 0 b t sub rlineto}\n"
"  for\n"
"} bind def\n";

static char psbeginsetup[] =
"%%BeginSetup\n";

static char psendsetup[] =
"%%EndSetup\n";

static char psbeginfeature[] = /* feature, value */
"mark {\n"
"%%%%BeginFeature: %s %s\n";

static char psendfeature[] =
"\n%%EndFeature\n"
"} stopped cleartomark\n";

static char psnewpage[] = /* name, number, xres, yres, xtrans, ytrans, rot */
"%%%%Page: %s %d\n"
"%%%%BeginPageSetup\n"
"/pgsave save def\n"
"72 %d div 72 %d div scale\n"
"%d %d translate\n"
"1 -1 scale\n"
"%d rotate\n"
"%%%%EndPageSetup\n";

static char psendpage[] =
"pgsave restore\n"
"showpage\n";

static char psfooter[] = /* pages */
"%%%%Trailer\n"
"%%%%Pages: %d\n"
"%%%%EOF\n";

static char psmoveto[] = /* x, y */
"%d %d moveto\n";

static char pslineto[] = /* x, y */
"%d %d lineto\n";

static char psstroke[] =
"stroke\n";

static char psrectangle[] = /* x, y, width, height, -width */
"%d %d moveto\n"
"%d 0 rlineto\n"
"0 %d rlineto\n"
"%d 0 rlineto\n"
"closepath\n";

static char psrrectangle[] = /* x, y, width, height, -width */
"%d %d rmoveto\n"
"%d 0 rlineto\n"
"0 %d rlineto\n"
"%d 0 rlineto\n"
"closepath\n";

static const char psglyphshow[] = /* glyph name */
"/%s glyphshow\n";

static char pssetfont[] = /* fontname, xscale, yscale, ascent, escapement */
"/%s findfont\n"
"[%d 0 0 %d 0 0]\n"
"%d 10 div matrix rotate\n"
"matrix concatmatrix\n"
"makefont setfont\n";

static char pssetlinewidth[] = /* width */
"%d setlinewidth\n";

static char pssetdash[] = /* dash, offset */
"[%s] %d setdash\n";

static char pssetgray[] = /* gray */
"%.2f setgray\n";

static char pssetrgbcolor[] = /* r, g, b */
"%.2f %.2f %.2f setrgbcolor\n";

static char psarc[] = /* x, y, w, h, ang1, ang2 */
"tmpmtrx currentmatrix pop\n"
"%d %d translate\n"
"%d %d scale\n"
"0 0 0.5 %.1f %.1f arc\n"
"tmpmtrx setmatrix\n";

static char psgsave[] =
"gsave\n";

static char psgrestore[] =
"grestore\n";

static char psfill[] =
"fill\n";

static char pseofill[] =
"eofill\n";

static char psnewpath[] =
"newpath\n";

static char psclosepath[] =
"closepath\n";

static char psclip[] =
"clip\n";

static char psinitclip[] =
"initclip\n";

static char pseoclip[] =
"eoclip\n";

static char psrectclip[] =
"%d %d %d %d rectclip\n";

static char psrectclip2[] =
"%s rectclip\n";

static char pshatch[] =
"hatch\n";

static char psrotate[] = /* ang */
"%.1f rotate\n";

static char psarrayget[] =
"%s %d get\n";

static char psarrayput[] =
"%s %d %ld put\n";

static char psarraydef[] =
"/%s %d array def\n";


int PSDRV_WriteSpool(PSDRV_PDEVICE *physDev, LPSTR lpData, WORD cch)
{
    if(physDev->job.OutOfPage) { /* Will get here after NEWFRAME Escape */
        if( !PSDRV_StartPage(physDev) )
	    return FALSE;
    }
    return WriteSpool16( physDev->job.hJob, lpData, cch );
}


INT PSDRV_WriteFeature(HANDLE16 hJob, char *feature, char *value,
			 char *invocation)
{

    char *buf = (char *)HeapAlloc( PSDRV_Heap, 0, sizeof(psbeginfeature) +
				   strlen(feature) + strlen(value));


    sprintf(buf, psbeginfeature, feature, value);
    WriteSpool16( hJob, buf, strlen(buf) );

    WriteSpool16( hJob, invocation, strlen(invocation) );

    WriteSpool16( hJob, psendfeature, strlen(psendfeature) );

    HeapFree( PSDRV_Heap, 0, buf );
    return 1;
}



INT PSDRV_WriteHeader( PSDRV_PDEVICE *physDev, LPCSTR title )
{
    char *buf, *orient;
    INPUTSLOT *slot;
    PAGESIZE *page;
    int llx, lly, urx, ury;

    TRACE("'%s'\n", title);

    buf = (char *)HeapAlloc( PSDRV_Heap, 0, sizeof(psheader) +
			     strlen(title) + 30 );
    if(!buf) {
        WARN("HeapAlloc failed\n");
        return 0;
    }

    /* BBox co-ords are in default user co-ord system so urx < ury even in
       landscape mode */
    llx = physDev->ImageableArea.left * 72.0 / physDev->logPixelsX;
    lly = physDev->ImageableArea.bottom * 72.0 / physDev->logPixelsY;
    urx = physDev->ImageableArea.right * 72.0 / physDev->logPixelsX;
    ury = physDev->ImageableArea.top * 72.0 / physDev->logPixelsY;

    if(physDev->Devmode->dmPublic.u1.s1.dmOrientation == DMORIENT_LANDSCAPE) {
	orient = "Landscape";
    } else {
	orient = "Portrait";
    }

    /* FIXME should do something better with BBox */

    sprintf(buf, psheader, title, llx, lly, urx, ury, orient);

    if( WriteSpool16( physDev->job.hJob, buf, strlen(buf) ) !=
	                                             strlen(buf) ) {
        WARN("WriteSpool error\n");
	HeapFree( PSDRV_Heap, 0, buf );
	return 0;
    }
    HeapFree( PSDRV_Heap, 0, buf );

    WriteSpool16( physDev->job.hJob, psbeginprolog, strlen(psbeginprolog) );
    WriteSpool16( physDev->job.hJob, psprolog, strlen(psprolog) );
    WriteSpool16( physDev->job.hJob, psendprolog, strlen(psendprolog) );

    WriteSpool16( physDev->job.hJob, psbeginsetup, strlen(psbeginsetup) );

    for(slot = physDev->pi->ppd->InputSlots; slot; slot = slot->next) {
        if(slot->WinBin == physDev->Devmode->dmPublic.dmDefaultSource) {
	    if(slot->InvocationString) {
	        PSDRV_WriteFeature(physDev->job.hJob, "*InputSlot", slot->Name,
			     slot->InvocationString);
		break;
	    }
	}
    }

    for(page = physDev->pi->ppd->PageSizes; page; page = page->next) {
        if(page->WinPage == physDev->Devmode->dmPublic.u1.s1.dmPaperSize) {
	    if(page->InvocationString) {
	        PSDRV_WriteFeature(physDev->job.hJob, "*PageSize", page->Name,
			     page->InvocationString);
		break;
	    }
	}
    }

    WriteSpool16( physDev->job.hJob, psendsetup, strlen(psendsetup) );


    return 1;
}


INT PSDRV_WriteFooter( PSDRV_PDEVICE *physDev )
{
    char *buf;

    buf = (char *)HeapAlloc( PSDRV_Heap, 0, sizeof(psfooter) + 100 );
    if(!buf) {
        WARN("HeapAlloc failed\n");
        return 0;
    }

    sprintf(buf, psfooter, physDev->job.PageNo);

    if( WriteSpool16( physDev->job.hJob, buf, strlen(buf) ) !=
	                                             strlen(buf) ) {
        WARN("WriteSpool error\n");
	HeapFree( PSDRV_Heap, 0, buf );
	return 0;
    }
    HeapFree( PSDRV_Heap, 0, buf );
    return 1;
}



INT PSDRV_WriteEndPage( PSDRV_PDEVICE *physDev )
{
    if( WriteSpool16( physDev->job.hJob, psendpage, sizeof(psendpage)-1 ) !=
	                                             sizeof(psendpage)-1 ) {
        WARN("WriteSpool error\n");
	return 0;
    }
    return 1;
}




INT PSDRV_WriteNewPage( PSDRV_PDEVICE *physDev )
{
    char *buf;
    char name[100];
    signed int xtrans, ytrans, rotation;

    sprintf(name, "%d", physDev->job.PageNo);

    buf = (char *)HeapAlloc( PSDRV_Heap, 0, sizeof(psnewpage) + 200 );
    if(!buf) {
        WARN("HeapAlloc failed\n");
        return 0;
    }

    if(physDev->Devmode->dmPublic.u1.s1.dmOrientation == DMORIENT_LANDSCAPE) {
        if(physDev->pi->ppd->LandscapeOrientation == -90) {
	    xtrans = physDev->ImageableArea.right;
	    ytrans = physDev->ImageableArea.top;
	    rotation = 90;
	} else {
	    xtrans = physDev->ImageableArea.left;
	    ytrans = physDev->ImageableArea.bottom;
	    rotation = -90;
	}
    } else {
        xtrans = physDev->ImageableArea.left;
	ytrans = physDev->ImageableArea.top;
	rotation = 0;
    }

    sprintf(buf, psnewpage, name, physDev->job.PageNo,
	    physDev->logPixelsX, physDev->logPixelsY,
	    xtrans, ytrans, rotation);

    if( WriteSpool16( physDev->job.hJob, buf, strlen(buf) ) !=
	                                             strlen(buf) ) {
        WARN("WriteSpool error\n");
	HeapFree( PSDRV_Heap, 0, buf );
	return 0;
    }
    HeapFree( PSDRV_Heap, 0, buf );
    return 1;
}


BOOL PSDRV_WriteMoveTo(PSDRV_PDEVICE *physDev, INT x, INT y)
{
    char buf[100];

    sprintf(buf, psmoveto, x, y);
    return PSDRV_WriteSpool(physDev, buf, strlen(buf));
}

BOOL PSDRV_WriteLineTo(PSDRV_PDEVICE *physDev, INT x, INT y)
{
    char buf[100];

    sprintf(buf, pslineto, x, y);
    return PSDRV_WriteSpool(physDev, buf, strlen(buf));
}


BOOL PSDRV_WriteStroke(PSDRV_PDEVICE *physDev)
{
    return PSDRV_WriteSpool(physDev, psstroke, sizeof(psstroke)-1);
}



BOOL PSDRV_WriteRectangle(PSDRV_PDEVICE *physDev, INT x, INT y, INT width,
			INT height)
{
    char buf[100];

    sprintf(buf, psrectangle, x, y, width, height, -width);
    return PSDRV_WriteSpool(physDev, buf, strlen(buf));
}

BOOL PSDRV_WriteRRectangle(PSDRV_PDEVICE *physDev, INT x, INT y, INT width,
      INT height)
{
    char buf[100];

    sprintf(buf, psrrectangle, x, y, width, height, -width);
    return PSDRV_WriteSpool(physDev, buf, strlen(buf));
}

BOOL PSDRV_WriteArc(PSDRV_PDEVICE *physDev, INT x, INT y, INT w, INT h, double ang1,
		      double ang2)
{
    char buf[256];

    /* Make angles -ve and swap order because we're working with an upside
       down y-axis */
    sprintf(buf, psarc, x, y, w, h, -ang2, -ang1);
    return PSDRV_WriteSpool(physDev, buf, strlen(buf));
}


BOOL PSDRV_WriteSetFont(PSDRV_PDEVICE *physDev, const char *name, INT size, INT escapement)
{
    char *buf;

    buf = (char *)HeapAlloc( PSDRV_Heap, 0, sizeof(pssetfont) +
			     strlen(name) + 40);

    if(!buf) {
        WARN("HeapAlloc failed\n");
        return FALSE;
    }

    sprintf(buf, pssetfont, name, size, -size, -escapement);

    PSDRV_WriteSpool(physDev, buf, strlen(buf));
    HeapFree(PSDRV_Heap, 0, buf);
    return TRUE;
}

BOOL PSDRV_WriteSetColor(PSDRV_PDEVICE *physDev, PSCOLOR *color)
{
    char buf[256];

    PSDRV_CopyColor(&physDev->inkColor, color);
    switch(color->type) {
    case PSCOLOR_RGB:
        sprintf(buf, pssetrgbcolor, color->value.rgb.r, color->value.rgb.g,
		color->value.rgb.b);
	return PSDRV_WriteSpool(physDev, buf, strlen(buf));

    case PSCOLOR_GRAY:
        sprintf(buf, pssetgray, color->value.gray.i);
	return PSDRV_WriteSpool(physDev, buf, strlen(buf));

    default:
        ERR("Unkonwn colour type %d\n", color->type);
	break;
    }

    return FALSE;
}

BOOL PSDRV_WriteSetPen(PSDRV_PDEVICE *physDev)
{
    char buf[256];

    sprintf(buf, pssetlinewidth, physDev->pen.width);
    PSDRV_WriteSpool(physDev, buf, strlen(buf));

    if(physDev->pen.dash) {
        sprintf(buf, pssetdash, physDev->pen.dash, 0);
	PSDRV_WriteSpool(physDev, buf, strlen(buf));
    }

    return TRUE;
}

BOOL PSDRV_WriteGlyphShow(PSDRV_PDEVICE *physDev, LPCSTR g_name)
{
    char    buf[128];
    int     l;

    l = snprintf(buf, sizeof(buf), psglyphshow, g_name);

    if (l < sizeof(psglyphshow) - 2 || l > sizeof(buf) - 1) {
	WARN("Unusable glyph name '%s' - ignoring\n", g_name);
	return FALSE;
    }

    PSDRV_WriteSpool(physDev, buf, l);
    return TRUE;
}

BOOL PSDRV_WriteFill(PSDRV_PDEVICE *physDev)
{
    return PSDRV_WriteSpool(physDev, psfill, sizeof(psfill)-1);
}

BOOL PSDRV_WriteEOFill(PSDRV_PDEVICE *physDev)
{
    return PSDRV_WriteSpool(physDev, pseofill, sizeof(pseofill)-1);
}

BOOL PSDRV_WriteGSave(PSDRV_PDEVICE *physDev)
{
    return PSDRV_WriteSpool(physDev, psgsave, sizeof(psgsave)-1);
}

BOOL PSDRV_WriteGRestore(PSDRV_PDEVICE *physDev)
{
    return PSDRV_WriteSpool(physDev, psgrestore, sizeof(psgrestore)-1);
}

BOOL PSDRV_WriteNewPath(PSDRV_PDEVICE *physDev)
{
    return PSDRV_WriteSpool(physDev, psnewpath, sizeof(psnewpath)-1);
}

BOOL PSDRV_WriteClosePath(PSDRV_PDEVICE *physDev)
{
    return PSDRV_WriteSpool(physDev, psclosepath, sizeof(psclosepath)-1);
}

BOOL PSDRV_WriteClip(PSDRV_PDEVICE *physDev)
{
    return PSDRV_WriteSpool(physDev, psclip, sizeof(psclip)-1);
}

BOOL PSDRV_WriteEOClip(PSDRV_PDEVICE *physDev)
{
    return PSDRV_WriteSpool(physDev, pseoclip, sizeof(pseoclip)-1);
}

BOOL PSDRV_WriteInitClip(PSDRV_PDEVICE *physDev)
{
    return PSDRV_WriteSpool(physDev, psinitclip, sizeof(psinitclip)-1);
}

BOOL PSDRV_WriteHatch(PSDRV_PDEVICE *physDev)
{
    return PSDRV_WriteSpool(physDev, pshatch, sizeof(pshatch)-1);
}

BOOL PSDRV_WriteRotate(PSDRV_PDEVICE *physDev, float ang)
{
    char buf[256];

    sprintf(buf, psrotate, ang);
    return PSDRV_WriteSpool(physDev, buf, strlen(buf));
}

BOOL PSDRV_WriteIndexColorSpaceBegin(PSDRV_PDEVICE *physDev, int size)
{
    char buf[256];
    sprintf(buf, "[/Indexed /DeviceRGB %d\n<\n", size);
    return PSDRV_WriteSpool(physDev, buf, strlen(buf));
}

BOOL PSDRV_WriteIndexColorSpaceEnd(PSDRV_PDEVICE *physDev)
{
    char buf[] = ">\n] setcolorspace\n";
    return PSDRV_WriteSpool(physDev, buf, sizeof(buf) - 1);
}

BOOL PSDRV_WriteRGB(PSDRV_PDEVICE *physDev, COLORREF *map, int number)
{
    char *buf = HeapAlloc(PSDRV_Heap, 0, number * 7 + 1), *ptr;
    int i;

    ptr = buf;
    for(i = 0; i < number; i++) {
        sprintf(ptr, "%02x%02x%02x%c", (int)GetRValue(map[i]),
		(int)GetGValue(map[i]), (int)GetBValue(map[i]),
		((i & 0x7) == 0x7) || (i == number - 1) ? '\n' : ' ');
	ptr += 7;
    }
    PSDRV_WriteSpool(physDev, buf, number * 7);
    HeapFree(PSDRV_Heap, 0, buf);
    return TRUE;
}


BOOL PSDRV_WriteImageDict(PSDRV_PDEVICE *physDev, WORD depth, INT xDst, INT yDst,
			  INT widthDst, INT heightDst, INT widthSrc,
			  INT heightSrc, char *bits)
{
    char start[] = "%d %d translate\n%d %d scale\n<<\n"
      " /ImageType 1\n /Width %d\n /Height %d\n /BitsPerComponent %d\n"
      " /ImageMatrix [%d 0 0 %d 0 %d]\n";

    char decode1[] = " /Decode [0 %d]\n";
    char decode3[] = " /Decode [0 1 0 1 0 1]\n";

    char end[] = " /DataSource currentfile /ASCIIHexDecode filter\n>> image\n";
    char endbits[] = " /DataSource <%s>\n>> image\n";

    char *buf = HeapAlloc(PSDRV_Heap, 0, 1000);

    sprintf(buf, start, xDst, yDst, widthDst, heightDst, widthSrc, heightSrc,
	    (depth < 8) ? depth : 8, widthSrc, -heightSrc, heightSrc);

    PSDRV_WriteSpool(physDev, buf, strlen(buf));

    switch(depth) {
    case 8:
        sprintf(buf, decode1, 255);
	break;

    case 4:
        sprintf(buf, decode1, 15);
	break;

    case 1:
        sprintf(buf, decode1, 1);
	break;

    default:
        strcpy(buf, decode3);
	break;
    }

    PSDRV_WriteSpool(physDev, buf, strlen(buf));

    if(!bits)
        PSDRV_WriteSpool(physDev, end, sizeof(end) - 1);
    else {
        sprintf(buf, endbits, bits);
        PSDRV_WriteSpool(physDev, buf, strlen(buf));
    }

    HeapFree(PSDRV_Heap, 0, buf);
    return TRUE;
}


BOOL PSDRV_WriteBytes(PSDRV_PDEVICE *physDev, const BYTE *bytes, int number)
{
    char *buf = HeapAlloc(PSDRV_Heap, 0, number * 3 + 1);
    char *ptr;
    int i;

    ptr = buf;

    for(i = 0; i < number; i++) {
        sprintf(ptr, "%02x%c", bytes[i],
		((i & 0xf) == 0xf) || (i == number - 1) ? '\n' : ' ');
	ptr += 3;
    }
    PSDRV_WriteSpool(physDev, buf, number * 3);

    HeapFree(PSDRV_Heap, 0, buf);
    return TRUE;
}

BOOL PSDRV_WriteDIBits16(PSDRV_PDEVICE *physDev, const WORD *words, int number)
{
    char *buf = HeapAlloc(PSDRV_Heap, 0, number * 7 + 1);
    char *ptr;
    int i;

    ptr = buf;

    for(i = 0; i < number; i++) {
        int r, g, b;

	/* We want 0x0 -- 0x1f to map to 0x0 -- 0xff */

	r = words[i] >> 10 & 0x1f;
	r = r << 3 | r >> 2;
	g = words[i] >> 5 & 0x1f;
	g = g << 3 | g >> 2;
	b = words[i] & 0x1f;
	b = b << 3 | b >> 2;
        sprintf(ptr, "%02x%02x%02x%c", r, g, b,
		((i & 0x7) == 0x7) || (i == number - 1) ? '\n' : ' ');
	ptr += 7;
    }
    PSDRV_WriteSpool(physDev, buf, number * 7);

    HeapFree(PSDRV_Heap, 0, buf);
    return TRUE;
}

BOOL PSDRV_WriteDIBits24(PSDRV_PDEVICE *physDev, const BYTE *bits, int number)
{
    char *buf = HeapAlloc(PSDRV_Heap, 0, number * 7 + 1);
    char *ptr;
    int i;

    ptr = buf;

    for(i = 0; i < number; i++) {
        sprintf(ptr, "%02x%02x%02x%c", bits[i * 3 + 2], bits[i * 3 + 1],
		bits[i * 3],
		((i & 0x7) == 0x7) || (i == number - 1) ? '\n' : ' ');
	ptr += 7;
    }
    PSDRV_WriteSpool(physDev, buf, number * 7);

    HeapFree(PSDRV_Heap, 0, buf);
    return TRUE;
}

BOOL PSDRV_WriteDIBits32(PSDRV_PDEVICE *physDev, const BYTE *bits, int number)
{
    char *buf = HeapAlloc(PSDRV_Heap, 0, number * 7 + 1);
    char *ptr;
    int i;

    ptr = buf;

    for(i = 0; i < number; i++) {
        sprintf(ptr, "%02x%02x%02x%c", bits[i * 4 + 2], bits[i * 4 + 1],
		bits[i * 4],
		((i & 0x7) == 0x7) || (i == number - 1) ? '\n' : ' ');
	ptr += 7;
    }
    PSDRV_WriteSpool(physDev, buf, number * 7);

    HeapFree(PSDRV_Heap, 0, buf);
    return TRUE;
}

BOOL PSDRV_WriteArrayGet(PSDRV_PDEVICE *physDev, CHAR *pszArrayName, INT nIndex)
{
    char buf[100];

    sprintf(buf, psarrayget, pszArrayName, nIndex);
    return PSDRV_WriteSpool(physDev, buf, strlen(buf));
}

BOOL PSDRV_WriteArrayPut(PSDRV_PDEVICE *physDev, CHAR *pszArrayName, INT nIndex, LONG lObject)
{
    char buf[100];

    sprintf(buf, psarrayput, pszArrayName, nIndex, lObject);
    return PSDRV_WriteSpool(physDev, buf, strlen(buf));
}

BOOL PSDRV_WriteArrayDef(PSDRV_PDEVICE *physDev, CHAR *pszArrayName, INT nSize)
{
    char buf[100];

    sprintf(buf, psarraydef, pszArrayName, nSize);
    return PSDRV_WriteSpool(physDev, buf, strlen(buf));
}

BOOL PSDRV_WriteRectClip(PSDRV_PDEVICE *physDev, INT x, INT y, INT w, INT h)
{
    char buf[100];

    sprintf(buf, psrectclip, x, y, w, h);
    return PSDRV_WriteSpool(physDev, buf, strlen(buf));
}

BOOL PSDRV_WriteRectClip2(PSDRV_PDEVICE *physDev, CHAR *pszArrayName)
{
    char buf[100];

    sprintf(buf, psrectclip2, pszArrayName);
    return PSDRV_WriteSpool(physDev, buf, strlen(buf));
}

BOOL PSDRV_WritePatternDict(PSDRV_PDEVICE *physDev, BITMAP *bm, BYTE *bits)
{
    char start[] = "<<\n /PaintType 1\n /PatternType 1\n /TilingType 1\n "
      "/BBox [0 0 %d %d]\n /XStep %d\n /YStep %d\n /PaintProc {\n  begin\n";

    char end[] = "  end\n }\n>>\n matrix makepattern setpattern\n";
    char *buf, *ptr;
    INT w, h, x, y;
    COLORREF map[2];

    w = bm->bmWidth & ~0x7;
    h = bm->bmHeight & ~0x7;

    buf = HeapAlloc(PSDRV_Heap, 0, sizeof(start) + 100);
    sprintf(buf, start, w, h, w, h);
    PSDRV_WriteSpool(physDev,  buf, strlen(buf));
    PSDRV_WriteIndexColorSpaceBegin(physDev, 1);
    map[0] = GetTextColor( physDev->hdc );
    map[1] = GetBkColor( physDev->hdc );
    PSDRV_WriteRGB(physDev, map, 2);
    PSDRV_WriteIndexColorSpaceEnd(physDev);
    ptr = buf;
    for(y = h-1; y >= 0; y--) {
        for(x = 0; x < w/8; x++) {
	    sprintf(ptr, "%02x", *(bits + x/8 + y * bm->bmWidthBytes));
	    ptr += 2;
	}
    }
    PSDRV_WriteImageDict(physDev, 1, 0, 0, 8, 8, 8, 8, buf);
    PSDRV_WriteSpool(physDev, end, sizeof(end) - 1);
    HeapFree(PSDRV_Heap, 0, buf);
    return TRUE;
}

BOOL PSDRV_WriteDIBPatternDict(PSDRV_PDEVICE *physDev, BITMAPINFO *bmi, UINT usage)
{
    char start[] = "<<\n /PaintType 1\n /PatternType 1\n /TilingType 1\n "
      "/BBox [0 0 %d %d]\n /XStep %d\n /YStep %d\n /PaintProc {\n  begin\n";

    char end[] = "  end\n }\n>>\n matrix makepattern setpattern\n";
    char *buf, *ptr;
    BYTE *bits;
    INT w, h, x, y, colours;
    COLORREF map[2];

    if(bmi->bmiHeader.biBitCount != 1) {
        FIXME("dib depth %d not supported\n", bmi->bmiHeader.biBitCount);
	return FALSE;
    }

    bits = (char*)bmi + bmi->bmiHeader.biSize;
    colours = bmi->bmiHeader.biClrUsed;
    if(!colours && bmi->bmiHeader.biBitCount <= 8)
        colours = 1 << bmi->bmiHeader.biBitCount;
    bits += colours * ((usage == DIB_RGB_COLORS) ?
		       sizeof(RGBQUAD) : sizeof(WORD));

    w = bmi->bmiHeader.biWidth & ~0x7;
    h = bmi->bmiHeader.biHeight & ~0x7;

    buf = HeapAlloc(PSDRV_Heap, 0, sizeof(start) + 100);
    sprintf(buf, start, w, h, w, h);
    PSDRV_WriteSpool(physDev,  buf, strlen(buf));
    PSDRV_WriteIndexColorSpaceBegin(physDev, 1);
    map[0] = physDev->dc->textColor;
    map[1] = physDev->dc->backgroundColor;
    PSDRV_WriteRGB(physDev, map, 2);
    PSDRV_WriteIndexColorSpaceEnd(physDev);
    ptr = buf;
    for(y = h-1; y >= 0; y--) {
        for(x = 0; x < w/8; x++) {
	    sprintf(ptr, "%02x", *(bits + x/8 + y *
				   (bmi->bmiHeader.biWidth + 31) / 32 * 4));
	    ptr += 2;
	}
    }
    PSDRV_WriteImageDict(physDev, 1, 0, 0, 8, 8, 8, 8, buf);
    PSDRV_WriteSpool(physDev, end, sizeof(end) - 1);
    HeapFree(PSDRV_Heap, 0, buf);
    return TRUE;
}
