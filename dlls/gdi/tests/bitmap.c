/*
 * Unit test suite for bitmaps
 *
 * Copyright 2004 Huw Davies
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

#include <stdarg.h>
#include <assert.h>

#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"

#include "wine/test.h"


static void test_createdibitmap(void)
{
    HDC hdc, hdcmem;
    BITMAPINFOHEADER bmih;
    BITMAP bm;
    HBITMAP hbm, hbm_colour, hbm_old;
    INT screen_depth;

    hdc = GetDC(0);
    screen_depth = GetDeviceCaps(hdc, BITSPIXEL);
    memset(&bmih, 0, sizeof(bmih));
    bmih.biSize = sizeof(bmih);
    bmih.biWidth = 10;
    bmih.biHeight = 10;
    bmih.biPlanes = 1;
    bmih.biBitCount = 32;
    bmih.biCompression = BI_RGB;
 
    /* First create an un-initialised bitmap.  The depth of the bitmap
       should match that of the hdc and not that supplied in bmih.
    */

    /* First try 32 bits */
    hbm = CreateDIBitmap(hdc, &bmih, 0, NULL, NULL, 0);
    ok(hbm != NULL, "CreateDIBitmap failed\n");
    ok(GetObject(hbm, sizeof(bm), &bm), "GetObject failed\n");

    ok(bm.bmBitsPixel == screen_depth, "CreateDIBitmap created bitmap of incorrect depth %d != %d\n", bm.bmBitsPixel, screen_depth);
    DeleteObject(hbm);
    
    /* Then 16 */
    bmih.biBitCount = 16;
    hbm = CreateDIBitmap(hdc, &bmih, 0, NULL, NULL, 0);
    ok(hbm != NULL, "CreateDIBitmap failed\n");
    ok(GetObject(hbm, sizeof(bm), &bm), "GetObject failed\n");

    ok(bm.bmBitsPixel == screen_depth, "CreateDIBitmap created bitmap of incorrect depth %d != %d\n", bm.bmBitsPixel, screen_depth);
    DeleteObject(hbm);

    /* Then 1 */
    bmih.biBitCount = 1;
    hbm = CreateDIBitmap(hdc, &bmih, 0, NULL, NULL, 0);
    ok(hbm != NULL, "CreateDIBitmap failed\n");
    ok(GetObject(hbm, sizeof(bm), &bm), "GetObject failed\n");

    ok(bm.bmBitsPixel == screen_depth, "CreateDIBitmap created bitmap of incorrect depth %d != %d\n", bm.bmBitsPixel, screen_depth);
    DeleteObject(hbm);

    /* Now with a monochrome dc we expect a monochrome bitmap */
    hdcmem = CreateCompatibleDC(hdc);

    /* First try 32 bits */
    bmih.biBitCount = 32;
    hbm = CreateDIBitmap(hdcmem, &bmih, 0, NULL, NULL, 0);
    ok(hbm != NULL, "CreateDIBitmap failed\n");
    ok(GetObject(hbm, sizeof(bm), &bm), "GetObject failed\n");

    ok(bm.bmBitsPixel == 1, "CreateDIBitmap created bitmap of incorrect depth %d != %d\n", bm.bmBitsPixel, 1);
    DeleteObject(hbm);
    
    /* Then 16 */
    bmih.biBitCount = 16;
    hbm = CreateDIBitmap(hdcmem, &bmih, 0, NULL, NULL, 0);
    ok(hbm != NULL, "CreateDIBitmap failed\n");
    ok(GetObject(hbm, sizeof(bm), &bm), "GetObject failed\n");

    ok(bm.bmBitsPixel == 1, "CreateDIBitmap created bitmap of incorrect depth %d != %d\n", bm.bmBitsPixel, 1);
    DeleteObject(hbm);
    
    /* Then 1 */
    bmih.biBitCount = 1;
    hbm = CreateDIBitmap(hdcmem, &bmih, 0, NULL, NULL, 0);
    ok(hbm != NULL, "CreateDIBitmap failed\n");
    ok(GetObject(hbm, sizeof(bm), &bm), "GetObject failed\n");

    ok(bm.bmBitsPixel == 1, "CreateDIBitmap created bitmap of incorrect depth %d != %d\n", bm.bmBitsPixel, 1);
    DeleteObject(hbm);

    /* Now select a polychrome bitmap into the dc and we expect
       screen_depth bitmaps again */
    hbm_colour = CreateCompatibleBitmap(hdc, 1, 1);
    hbm_old = SelectObject(hdcmem, hbm_colour);

    /* First try 32 bits */
    bmih.biBitCount = 32;
    hbm = CreateDIBitmap(hdcmem, &bmih, 0, NULL, NULL, 0);
    ok(hbm != NULL, "CreateDIBitmap failed\n");
    ok(GetObject(hbm, sizeof(bm), &bm), "GetObject failed\n");

    ok(bm.bmBitsPixel == screen_depth, "CreateDIBitmap created bitmap of incorrect depth %d != %d\n", bm.bmBitsPixel, screen_depth);
    DeleteObject(hbm);
    
    /* Then 16 */
    bmih.biBitCount = 16;
    hbm = CreateDIBitmap(hdcmem, &bmih, 0, NULL, NULL, 0);
    ok(hbm != NULL, "CreateDIBitmap failed\n");
    ok(GetObject(hbm, sizeof(bm), &bm), "GetObject failed\n");

    ok(bm.bmBitsPixel == screen_depth, "CreateDIBitmap created bitmap of incorrect depth %d != %d\n", bm.bmBitsPixel, screen_depth);
    DeleteObject(hbm);
    
    /* Then 1 */
    bmih.biBitCount = 1;
    hbm = CreateDIBitmap(hdcmem, &bmih, 0, NULL, NULL, 0);
    ok(hbm != NULL, "CreateDIBitmap failed\n");
    ok(GetObject(hbm, sizeof(bm), &bm), "GetObject failed\n");

    ok(bm.bmBitsPixel == screen_depth, "CreateDIBitmap created bitmap of incorrect depth %d != %d\n", bm.bmBitsPixel, screen_depth);
    DeleteObject(hbm);

    SelectObject(hdcmem, hbm_old);
    DeleteObject(hbm_colour);
    DeleteDC(hdcmem);

    /* If hdc == 0 then we get a 1 bpp bitmap */
    bmih.biBitCount = 32;
    hbm = CreateDIBitmap(0, &bmih, 0, NULL, NULL, 0);
    ok(hbm != NULL, "CreateDIBitmap failed\n");
    ok(GetObject(hbm, sizeof(bm), &bm), "GetObject failed\n");

    ok(bm.bmBitsPixel == 1, "CreateDIBitmap created bitmap of incorrect depth %d != %d\n", bm.bmBitsPixel, 1);
    DeleteObject(hbm);
    
    ReleaseDC(0, hdc);
}

static void test_dibsections(void)
{
    HDC hdc, hdcmem, hdcmem2;
    HBITMAP hdib, oldbm, hdib2, oldbm2;
    char bmibuf[sizeof(BITMAPINFO) + 256 * sizeof(RGBQUAD)];
    char bcibuf[sizeof(BITMAPCOREINFO) + 256 * sizeof(RGBTRIPLE)];
    BITMAPINFO *pbmi = (BITMAPINFO *)bmibuf;
    BITMAPCOREINFO *pbci = (BITMAPCOREINFO *)bcibuf;
    HBITMAP hcoredib;
    char coreBits[256];
    BYTE *bits;
    RGBQUAD rgb[256];
    int ret;
    char logpalbuf[sizeof(LOGPALETTE) + 256 * sizeof(PALETTEENTRY)];
    LOGPALETTE *plogpal = (LOGPALETTE*)logpalbuf;
    WORD *index;
    DWORD *bits32;
    HPALETTE hpal, oldpal;

    hdc = GetDC(0);
    memset(pbmi, 0, sizeof(bmibuf));
    pbmi->bmiHeader.biSize = sizeof(pbmi->bmiHeader);
    pbmi->bmiHeader.biHeight = 16;
    pbmi->bmiHeader.biWidth = 16;
    pbmi->bmiHeader.biBitCount = 1;
    pbmi->bmiHeader.biPlanes = 1;
    pbmi->bmiHeader.biCompression = BI_RGB;
    pbmi->bmiColors[0].rgbRed = 0xff;
    pbmi->bmiColors[0].rgbGreen = 0;
    pbmi->bmiColors[0].rgbBlue = 0;
    pbmi->bmiColors[1].rgbRed = 0;
    pbmi->bmiColors[1].rgbGreen = 0;
    pbmi->bmiColors[1].rgbBlue = 0xff;

    hdib = CreateDIBSection(hdc, pbmi, DIB_RGB_COLORS, (void**)&bits, NULL, 0);
    ok(hdib != NULL, "CreateDIBSection failed\n");

    /* Test if the old BITMAPCOREINFO structure is supported */    
        
    pbci->bmciHeader.bcSize = sizeof(BITMAPCOREHEADER);
    pbci->bmciHeader.bcBitCount = 0;

    ret = GetDIBits(hdc, hdib, 0, 16, NULL, (BITMAPINFO*) pbci, DIB_RGB_COLORS);    
    ok(ret, "GetDIBits doesn't work with a BITMAPCOREHEADER\n");
    ok((pbci->bmciHeader.bcWidth == 16) && (pbci->bmciHeader.bcHeight == 16)
        && (pbci->bmciHeader.bcBitCount == 1) && (pbci->bmciHeader.bcPlanes == 1),
	"GetDIBits did't fill in the BITMAPCOREHEADER structure properly\n");

    ret = GetDIBits(hdc, hdib, 0, 16, &coreBits, (BITMAPINFO*) pbci, DIB_RGB_COLORS);
    ok(ret, "GetDIBits doesn't work with a BITMAPCOREHEADER\n");
    ok((pbci->bmciColors[0].rgbtRed == 0xff) && (pbci->bmciColors[0].rgbtGreen == 0) &&
       (pbci->bmciColors[0].rgbtBlue == 0) && (pbci->bmciColors[1].rgbtRed == 0) &&
       (pbci->bmciColors[1].rgbtGreen == 0) && (pbci->bmciColors[1].rgbtBlue == 0xff),
       "The color table has not been translated to the old BITMAPCOREINFO format\n");

    hcoredib = CreateDIBSection(hdc, (BITMAPINFO*) pbci, DIB_RGB_COLORS, (void**)&bits, NULL, 0);
    ok(hcoredib != NULL, "CreateDIBSection failed with a BITMAPCOREINFO\n");

    ZeroMemory(pbci->bmciColors, 256 * sizeof(RGBTRIPLE));
    ret = GetDIBits(hdc, hcoredib, 0, 16, &coreBits, (BITMAPINFO*) pbci, DIB_RGB_COLORS);
    ok(ret, "GetDIBits doesn't work with a BITMAPCOREHEADER\n");
    ok((pbci->bmciColors[0].rgbtRed == 0xff) && (pbci->bmciColors[0].rgbtGreen == 0) &&
       (pbci->bmciColors[0].rgbtBlue == 0) && (pbci->bmciColors[1].rgbtRed == 0) &&
       (pbci->bmciColors[1].rgbtGreen == 0) && (pbci->bmciColors[1].rgbtBlue == 0xff),
       "The color table has not been translated to the old BITMAPCOREINFO format\n");    

    DeleteObject(hcoredib);

    hdcmem = CreateCompatibleDC(hdc);
    oldbm = SelectObject(hdcmem, hdib);

    ret = GetDIBColorTable(hdcmem, 0, 2, rgb);
    ok(ret == 2, "GetDIBColorTable returned %d\n", ret);
    ok(!memcmp(rgb, pbmi->bmiColors, 2 * sizeof(RGBQUAD)),
       "GetDIBColorTable returns table 0: r%02x g%02x b%02x res%02x 1: r%02x g%02x b%02x res%02x\n",
       rgb[0].rgbRed, rgb[0].rgbGreen, rgb[0].rgbBlue, rgb[0].rgbReserved,
       rgb[1].rgbRed, rgb[1].rgbGreen, rgb[1].rgbBlue, rgb[1].rgbReserved);

    SelectObject(hdcmem, oldbm);
    DeleteObject(hdib);

    /* Now create a palette and a palette indexed dib section */
    memset(plogpal, 0, sizeof(logpalbuf));
    plogpal->palVersion = 0x300;
    plogpal->palNumEntries = 2;
    plogpal->palPalEntry[0].peRed = 0xff;
    plogpal->palPalEntry[0].peBlue = 0xff;
    plogpal->palPalEntry[1].peGreen = 0xff;

    index = (WORD*)pbmi->bmiColors;
    *index++ = 0;
    *index = 1;
    hpal = CreatePalette(plogpal);
    ok(hpal != NULL, "CreatePalette failed\n");
    oldpal = SelectPalette(hdc, hpal, TRUE);
    hdib = CreateDIBSection(hdc, pbmi, DIB_PAL_COLORS, (void**)&bits, NULL, 0);
    ok(hdib != NULL, "CreateDIBSection failed\n");

    /* The colour table has already been grabbed from the dc, so we select back the
       old palette */

    SelectPalette(hdc, oldpal, TRUE);
    oldbm = SelectObject(hdcmem, hdib);

    ret = GetDIBColorTable(hdcmem, 0, 2, rgb);
    ok(ret == 2, "GetDIBColorTable returned %d\n", ret);
    ok(rgb[0].rgbRed == 0xff && rgb[0].rgbBlue == 0xff && rgb[0].rgbGreen == 0 &&
       rgb[1].rgbRed == 0    && rgb[1].rgbBlue == 0    && rgb[1].rgbGreen == 0xff,
       "GetDIBColorTable returns table 0: r%02x g%02x b%02x res%02x 1: r%02x g%02x b%02x res%02x\n",
       rgb[0].rgbRed, rgb[0].rgbGreen, rgb[0].rgbBlue, rgb[0].rgbReserved,
       rgb[1].rgbRed, rgb[1].rgbGreen, rgb[1].rgbBlue, rgb[1].rgbReserved);

    /* Bottom and 2nd row from top green, everything else magenta */
    bits[0] = bits[1] = 0xff;
    bits[13 * 4] = bits[13*4 + 1] = 0xff;


    pbmi->bmiHeader.biBitCount = 32;

    hdib2 = CreateDIBSection(NULL, pbmi, DIB_RGB_COLORS, (void **)&bits32, NULL, 0);
    ok(hdib2 != NULL, "CreateDIBSection failed\n");
    hdcmem2 = CreateCompatibleDC(hdc);
    oldbm2 = SelectObject(hdcmem2, hdib2);

    BitBlt(hdcmem2, 0, 0, 16,16, hdcmem, 0, 0, SRCCOPY);

    ok(bits32[0] == 0xff00, "lower left pixel is %08lx\n", bits32[0]);
    ok(bits32[17] == 0xff00ff, "bottom but one, left pixel is %08lx\n", bits32[17]);

    SelectObject(hdcmem2, oldbm2);
    DeleteObject(hdib2);

    SelectObject(hdcmem, oldbm);
    DeleteObject(hdib);


    DeleteDC(hdcmem);
    ReleaseDC(0, hdc);
}    

START_TEST(bitmap)
{
    test_createdibitmap();
    test_dibsections();
}
