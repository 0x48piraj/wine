/*
 * X11DRV device-independent bitmaps
 *
 * Copyright 1993,1994  Alexandre Julliard
 */

#include "config.h"

#include "ts_xlib.h"
#include "ts_xutil.h"
#ifdef HAVE_LIBXXSHM
# include "ts_xshm.h"
# ifdef HAVE_SYS_SHM_H
#  include <sys/shm.h>
# endif
# ifdef HAVE_SYS_IPC_H
#  include <sys/ipc.h>
# endif
#endif /* defined(HAVE_LIBXXSHM) */

#include <stdlib.h>
#include <string.h>
#include "windef.h"
#include "bitmap.h"
#include "x11drv.h"
#include "debugtools.h"
#include "gdi.h"
#include "palette.h"
#include "global.h"

DEFAULT_DEBUG_CHANNEL(bitmap);
DECLARE_DEBUG_CHANNEL(x11drv);

static int ximageDepthTable[32];

#ifdef HAVE_LIBXXSHM
static int XShmErrorFlag = 0;
#endif

/* This structure holds the arguments for DIB_SetImageBits() */
typedef struct
{
    struct tagDC   *dc;
    LPCVOID         bits;
    XImage         *image;
    PALETTEENTRY   *palentry;
    int             lines;
    DWORD           infoWidth;
    WORD            depth;
    WORD            infoBpp;
    WORD            compression;
    RGBQUAD        *colorMap;
    int             nColorMap;
    Drawable        drawable;
    GC              gc;
    int             xSrc;
    int             ySrc;
    int             xDest;
    int             yDest;
    int             width;
    int             height;
    DWORD           rMask;
    DWORD           gMask;
    DWORD           bMask;
    BOOL            useShm;
    int             dibpitch;
} X11DRV_DIB_IMAGEBITS_DESCR;


enum Rle_EscapeCodes
{
  RLE_EOL   = 0, /* End of line */
  RLE_END   = 1, /* End of bitmap */
  RLE_DELTA = 2  /* Delta */
};

/***********************************************************************
 *           X11DRV_DIB_GetXImageWidthBytes
 *
 * Return the width of an X image in bytes
 */
inline static int X11DRV_DIB_GetXImageWidthBytes( int width, int depth )
{
    if (!depth || depth > 32) goto error;

    if (!ximageDepthTable[depth-1])
    {
        XImage *testimage = XCreateImage( gdi_display, visual, depth,
                                          ZPixmap, 0, NULL, 1, 1, 32, 20 );
        if (testimage)
        {
            ximageDepthTable[depth-1] = testimage->bits_per_pixel;
            XDestroyImage( testimage );
        }
        else ximageDepthTable[depth-1] = -1;
    }
    if (ximageDepthTable[depth-1] != -1)
        return (4 * ((width * ximageDepthTable[depth-1] + 31) / 32));

 error:
    WARN( "(%d): Unsupported depth\n", depth );
    return 4 * width;
}


/***********************************************************************
 *           X11DRV_DIB_CreateXImage
 *
 * Create an X image.
 */
XImage *X11DRV_DIB_CreateXImage( int width, int height, int depth )
{
    int width_bytes;
    XImage *image;

    wine_tsx11_lock();
    width_bytes = X11DRV_DIB_GetXImageWidthBytes( width, depth );
    image = XCreateImage( gdi_display, visual, depth, ZPixmap, 0,
                          calloc( height, width_bytes ),
                          width, height, 32, width_bytes );
    wine_tsx11_unlock();
    return image;
}


/***********************************************************************
 *           X11DRV_DIB_GenColorMap
 *
 * Fills the color map of a bitmap palette. Should not be called
 * for a >8-bit deep bitmap.
 */
int *X11DRV_DIB_GenColorMap( DC *dc, int *colorMapping,
                             WORD coloruse, WORD depth, BOOL quads,
                             const void *colorPtr, int start, int end )
{
    int i;

    if (coloruse == DIB_RGB_COLORS)
    {
        if (quads)
        {
            RGBQUAD * rgb = (RGBQUAD *)colorPtr;

            if (depth == 1)  /* Monochrome */
                for (i = start; i < end; i++, rgb++)
                    colorMapping[i] = (rgb->rgbRed + rgb->rgbGreen +
                                       rgb->rgbBlue > 255*3/2);
            else
                for (i = start; i < end; i++, rgb++)
                    colorMapping[i] = X11DRV_PALETTE_ToPhysical( dc, RGB(rgb->rgbRed,
                                                                rgb->rgbGreen,
                                                                rgb->rgbBlue));
        }
        else
        {
            RGBTRIPLE * rgb = (RGBTRIPLE *)colorPtr;

            if (depth == 1)  /* Monochrome */
                for (i = start; i < end; i++, rgb++)
                    colorMapping[i] = (rgb->rgbtRed + rgb->rgbtGreen +
                                       rgb->rgbtBlue > 255*3/2);
            else
                for (i = start; i < end; i++, rgb++)
                    colorMapping[i] = X11DRV_PALETTE_ToPhysical( dc, RGB(rgb->rgbtRed,
                                                               rgb->rgbtGreen,
                                                               rgb->rgbtBlue));
        }
    }
    else  /* DIB_PAL_COLORS */
    {
        if (colorPtr) {
            WORD * index = (WORD *)colorPtr;

            for (i = start; i < end; i++, index++)
                colorMapping[i] = X11DRV_PALETTE_ToPhysical( dc, PALETTEINDEX(*index) );
        } else {
            for (i = start; i < end; i++)
                colorMapping[i] = X11DRV_PALETTE_ToPhysical( dc, PALETTEINDEX(i) );
        }
    }

    return colorMapping;
}

/***********************************************************************
 *           X11DRV_DIB_BuildColorMap
 *
 * Build the color map from the bitmap palette. Should not be called
 * for a >8-bit deep bitmap.
 */
int *X11DRV_DIB_BuildColorMap( DC *dc, WORD coloruse, WORD depth, 
                               const BITMAPINFO *info, int *nColors )
{
    int colors;
    BOOL isInfo;
    const void *colorPtr;
    int *colorMapping;

    if ((isInfo = (info->bmiHeader.biSize == sizeof(BITMAPINFOHEADER))))
    {
        colors = info->bmiHeader.biClrUsed;
        if (!colors) colors = 1 << info->bmiHeader.biBitCount;
        colorPtr = info->bmiColors;
    }
    else  /* assume BITMAPCOREINFO */
    {
        colors = 1 << ((BITMAPCOREHEADER *)&info->bmiHeader)->bcBitCount;
        colorPtr = (WORD *)((BITMAPCOREINFO *)info)->bmciColors;
    }

    if (colors > 256)
    {
        ERR("called with >256 colors!\n");
        return NULL;
    }

    /* just so CopyDIBSection doesn't have to create an identity palette */
    if (coloruse == (WORD)-1) colorPtr = NULL;

    if (!(colorMapping = (int *)HeapAlloc(GetProcessHeap(), 0,
                                          colors * sizeof(int) ))) 
	return NULL;

    *nColors = colors;
    return X11DRV_DIB_GenColorMap( dc, colorMapping, coloruse, depth,
                                   isInfo, colorPtr, 0, colors);
}


/***********************************************************************
 *           X11DRV_DIB_MapColor
 */
int X11DRV_DIB_MapColor( int *physMap, int nPhysMap, int phys, int oldcol )
{
    int color;

    if ((oldcol < nPhysMap) && (physMap[oldcol] == phys))
        return oldcol;

    for (color = 0; color < nPhysMap; color++)
        if (physMap[color] == phys)
            return color;

    WARN("Strange color %08x\n", phys);
    return 0;
}


/*********************************************************************
 *         X11DRV_DIB_GetNearestIndex
 *
 * Helper for X11DRV_DIB_GetDIBits.
 * Returns the nearest colour table index for a given RGB.
 * Nearest is defined by minimizing the sum of the squares.
 */
static INT X11DRV_DIB_GetNearestIndex(RGBQUAD *colormap, int numColors, BYTE r, BYTE g, BYTE b)
{
    INT i, best = -1, diff, bestdiff = -1;
    RGBQUAD *color;

    for(color = colormap, i = 0; i < numColors; color++, i++) {
        diff = (r - color->rgbRed) * (r - color->rgbRed) +
	       (g - color->rgbGreen) * (g - color->rgbGreen) +
	       (b - color->rgbBlue) * (b - color->rgbBlue);
	if(diff == 0)
	    return i;
	if(best == -1 || diff < bestdiff) {
	    best = i;
	    bestdiff = diff;
	}
    }
    return best;
}
/*********************************************************************
 *         X11DRV_DIB_MaskToShift
 *
 * Helper for X11DRV_DIB_GetDIBits.
 * Returns the by how many bits to shift a given color so that it is 
 * in the proper position.
 */
static INT X11DRV_DIB_MaskToShift(DWORD mask)
{
    int shift;

    if (mask==0)
        return 0;

    shift=0;
    while ((mask&1)==0) {
        mask>>=1;
        shift++;
    }
    return shift;
}

/***********************************************************************
 *           X11DRV_DIB_Convert_any_asis
 *
 * All X11DRV_DIB_Convert_Xxx functions take at least the following 
 * parameters:
 * - width
 *   This is the width in pixel of the surface to copy. This may be less 
 *   than the full width of the image.
 * - height
 *   The number of lines to copy. This may be less than the full height
 *   of the image. This is always >0.
 * - srcbits
 *   Points to the first byte containing data to be copied. If the source
 *   surface starts are coordinates (x,y) then this is:
 *   image_ptr+x*bytes_pre_pixel+y*bytes_per_line
 *   (with further adjustments for top-down/bottom-up images)
 * - srclinebytes
 *   This is the number of bytes per line. It may be >0 or <0 depending on
 *   whether this is a top-down or bottom-up image.
 * - dstbits
 *   Same as srcbits but for the destination
 * - dstlinebytes
 *   Same as srclinebytes but for the destination.
 *
 * Notes:
 * - The supported Dib formats are: pal1, pal4, pal8, rgb555, bgr555, 
 *   rgb565, bgr565, rgb888 and any 32bit (0888) format.
 *   The supported XImage (Bmp) formats are: pal1, pal4, pal8, 
 *   rgb555, bgr555, rgb565, bgr565, rgb888, bgr888, rgb0888, bgr0888.
 * - Rgb formats are those for which the masks are such that:
 *   red_mask > green_mask > blue_mask
 * - Bgr formats are those for which the masks sort in the other direction.
 * - Many conversion functions handle both rgb->bgr and bgr->rgb conversions
 *   so the comments use h, g, l to mean respectively the source color in the 
 *   high bits, the green, and the source color in the low bits.
 */
static void X11DRV_DIB_Convert_any_asis(int width, int height,
                                    int bytes_per_pixel,
                                    const void* srcbits, int srclinebytes,
                                    void* dstbits, int dstlinebytes)
{
    int y;

    width*=bytes_per_pixel;
    for (y=0; y<height; y++) {
        memcpy(dstbits, srcbits, width);
        srcbits += srclinebytes;
        dstbits += dstlinebytes;
    }
}

/*
 * 15 bit conversions
 */

static void X11DRV_DIB_Convert_555_reverse(int width, int height,
                                    const void* srcbits, int srclinebytes,
                                    void* dstbits, int dstlinebytes)
{
    const DWORD* srcpixel;
    DWORD* dstpixel;
    int x,y;

    for (y=0; y<height; y++) {
        srcpixel=srcbits;
        dstpixel=dstbits;
        for (x=0; x<width/2; x++) {
            /* Do 2 pixels at a time */
            DWORD srcval;
            srcval=*srcpixel++;
            *dstpixel++=((srcval << 10) & 0x7c007c00) | /* h */
                        ( srcval        & 0x03e003e0) | /* g */
                        ((srcval >> 10) & 0x001f001f);  /* l */
        }
        if (width&1) {
            /* And the the odd pixel */
            WORD srcval;
            srcval=*((WORD*)srcpixel);
            *((WORD*)dstpixel)=((srcval << 10) & 0x7c00) | /* h */
                               ( srcval        & 0x03e0) | /* g */
                               ((srcval >> 10) & 0x001f);  /* l */
        }
        srcbits += srclinebytes;
        dstbits += dstlinebytes;
    }
}

static void X11DRV_DIB_Convert_555_to_565_asis(int width, int height,
                                    const void* srcbits, int srclinebytes,
                                    void* dstbits, int dstlinebytes)
{
    const DWORD* srcpixel;
    DWORD* dstpixel;
    int x,y;

    for (y=0; y<height; y++) {
        srcpixel=srcbits;
        dstpixel=dstbits;
        for (x=0; x<width/2; x++) {
            /* Do 2 pixels at a time */
            DWORD srcval;
            srcval=*srcpixel++;
            *dstpixel++=((srcval << 1) & 0xffc0ffc0) | /* h, g */
                        ((srcval >> 4) & 0x00200020) | /* g - 1 bit */
                        ( srcval       & 0x001f001f);  /* l */
        }
        if (width&1) {
            /* And the the odd pixel */
            WORD srcval;
            srcval=*((WORD*)srcpixel);
            *((WORD*)dstpixel)=((srcval << 1) & 0xffc0) | /* h, g */
                               ((srcval >> 4) & 0x0020) | /* g - 1 bit */
                                (srcval       & 0x001f);  /* l */
        }
        srcbits += srclinebytes;
        dstbits += dstlinebytes;
    }
}

static void X11DRV_DIB_Convert_555_to_565_reverse(int width, int height,
                                    const void* srcbits, int srclinebytes,
                                    void* dstbits, int dstlinebytes)
{
    const DWORD* srcpixel;
    DWORD* dstpixel;
    int x,y;

    for (y=0; y<height; y++) {
        srcpixel=srcbits;
        dstpixel=dstbits;
        for (x=0; x<width/2; x++) {
            /* Do 2 pixels at a time */
            DWORD srcval;
            srcval=*srcpixel++;
            *dstpixel++=((srcval >> 10) & 0x001f001f) | /* h */
                        ((srcval <<  1) & 0x07c007c0) | /* g */
                        ((srcval >>  4) & 0x00200020) | /* g - 1 bit */
                        ((srcval << 11) & 0xf800f800);  /* l */
        }
        if (width&1) {
            /* And the the odd pixel */
            WORD srcval;
            srcval=*((WORD*)srcpixel);
            *((WORD*)dstpixel)=((srcval >> 10) & 0x001f) | /* h */
                               ((srcval <<  1) & 0x07c0) | /* g */
                               ((srcval >>  4) & 0x0020) | /* g - 1 bit */
                               ((srcval << 11) & 0xf800);  /* l */
        }
        srcbits += srclinebytes;
        dstbits += dstlinebytes;
    }
}

static void X11DRV_DIB_Convert_555_to_888_asis(int width, int height,
                                    const void* srcbits, int srclinebytes,
                                    void* dstbits, int dstlinebytes)
{
    const WORD* srcpixel;
    BYTE* dstpixel;
    int x,y;

    for (y=0; y<height; y++) {
        srcpixel=srcbits;
        dstpixel=dstbits;
        for (x=0; x<width; x++) {
            WORD srcval;
            srcval=*srcpixel++;
            dstpixel[0]=((srcval <<  3) & 0xf8) | /* l */
                        ((srcval >>  2) & 0x07);  /* l - 3 bits */
            dstpixel[1]=((srcval >>  2) & 0xf8) | /* g */
                        ((srcval >>  7) & 0x07);  /* g - 3 bits */
            dstpixel[2]=((srcval >>  7) & 0xf8) | /* h */
                        ((srcval >> 12) & 0x07);  /* h - 3 bits */
            dstpixel+=3;
        }
        srcbits += srclinebytes;
        dstbits += dstlinebytes;
    }
}

static void X11DRV_DIB_Convert_555_to_888_reverse(int width, int height,
                                    const void* srcbits, int srclinebytes,
                                    void* dstbits, int dstlinebytes)
{
    const WORD* srcpixel;
    BYTE* dstpixel;
    int x,y;

    for (y=0; y<height; y++) {
        srcpixel=srcbits;
        dstpixel=dstbits;
        for (x=0; x<width; x++) {
            WORD srcval;
            srcval=*srcpixel++;
            dstpixel[0]=((srcval >>  7) & 0xf8) | /* h */
                        ((srcval >> 12) & 0x07);  /* h - 3 bits */
            dstpixel[1]=((srcval >>  2) & 0xf8) | /* g */
                        ((srcval >>  7) & 0x07);  /* g - 3 bits */
            dstpixel[2]=((srcval <<  3) & 0xf8) | /* l */
                        ((srcval >>  2) & 0x07);  /* l - 3 bits */
            dstpixel+=3;
        }
        srcbits += srclinebytes;
        dstbits += dstlinebytes;
    }
}

static void X11DRV_DIB_Convert_555_to_0888_asis(int width, int height,
                                    const void* srcbits, int srclinebytes,
                                    void* dstbits, int dstlinebytes)
{
    const WORD* srcpixel;
    DWORD* dstpixel;
    int x,y;

    for (y=0; y<height; y++) {
        srcpixel=srcbits;
        dstpixel=dstbits;
        for (x=0; x<width; x++) {
            WORD srcval;
            srcval=*srcpixel++;
            *dstpixel++=((srcval << 9) & 0xf80000) | /* h */
                        ((srcval << 4) & 0x070000) | /* h - 3 bits */
                        ((srcval << 6) & 0x00f800) | /* g */
                        ((srcval << 1) & 0x000700) | /* g - 3 bits */
                        ((srcval << 3) & 0x0000f8) | /* l */
                        ((srcval >> 2) & 0x000007);  /* l - 3 bits */
        }
        srcbits += srclinebytes;
        dstbits += dstlinebytes;
    }
}

static void X11DRV_DIB_Convert_555_to_0888_reverse(int width, int height,
                                    const void* srcbits, int srclinebytes,
                                    void* dstbits, int dstlinebytes)
{
    const WORD* srcpixel;
    DWORD* dstpixel;
    int x,y;

    for (y=0; y<height; y++) {
        srcpixel=srcbits;
        dstpixel=dstbits;
        for (x=0; x<width; x++) {
            WORD srcval;
            srcval=*srcpixel++;
            *dstpixel++=((srcval >>  7) & 0x0000f8) | /* h */
                        ((srcval >> 12) & 0x000007) | /* h - 3 bits */
                        ((srcval <<  6) & 0x00f800) | /* g */
                        ((srcval <<  1) & 0x000700) | /* g - 3 bits */
                        ((srcval << 19) & 0xf80000) | /* l */
                        ((srcval << 14) & 0x070000);  /* l - 3 bits */
        }
        srcbits += srclinebytes;
        dstbits += dstlinebytes;
    }
}

static void X11DRV_DIB_Convert_5x5_to_any0888(int width, int height,
                                    const void* srcbits, int srclinebytes,
                                    WORD rsrc, WORD gsrc, WORD bsrc,
                                    void* dstbits, int dstlinebytes,
                                    DWORD rdst, DWORD gdst, DWORD bdst)
{
    int rRightShift1,gRightShift1,bRightShift1;
    int rRightShift2,gRightShift2,bRightShift2;
    BYTE gMask1,gMask2;
    int rLeftShift,gLeftShift,bLeftShift;
    const WORD* srcpixel;
    DWORD* dstpixel;
    int x,y;

    /* Note, the source pixel value is shifted left by 16 bits so that
     * we know we will always have to shift right to extract the components.
     */
    rRightShift1=16+X11DRV_DIB_MaskToShift(rsrc)-3;
    gRightShift1=16+X11DRV_DIB_MaskToShift(gsrc)-3;
    bRightShift1=16+X11DRV_DIB_MaskToShift(bsrc)-3;
    rRightShift2=rRightShift1+5;
    gRightShift2=gRightShift1+5;
    bRightShift2=bRightShift1+5;
    if (gsrc==0x03e0) {
        /* Green has 5 bits, like the others */
        gMask1=0xf8;
        gMask2=0x07;
    } else {
        /* Green has 6 bits, not 5. Compensate. */
        gRightShift1++;
        gRightShift2+=2;
        gMask1=0xfc;
        gMask2=0x03;
    }

    rLeftShift=X11DRV_DIB_MaskToShift(rdst);
    gLeftShift=X11DRV_DIB_MaskToShift(gdst);
    bLeftShift=X11DRV_DIB_MaskToShift(bdst);

    for (y=0; y<height; y++) {
        srcpixel=srcbits;
        dstpixel=dstbits;
        for (x=0; x<width; x++) {
            DWORD srcval;
            BYTE red,green,blue;
            srcval=*srcpixel++ << 16;
            red=  ((srcval >> rRightShift1) & 0xf8) |
                  ((srcval >> rRightShift2) & 0x07);
            green=((srcval >> gRightShift1) & gMask1) |
                  ((srcval >> gRightShift2) & gMask2);
            blue= ((srcval >> bRightShift1) & 0xf8) |
                  ((srcval >> bRightShift2) & 0x07);
            *dstpixel++=(red   << rLeftShift) |
                        (green << gLeftShift) |
                        (blue  << bLeftShift);
        }
        srcbits += srclinebytes;
        dstbits += dstlinebytes;
    }
}

/*
 * 16 bits conversions
 */

static void X11DRV_DIB_Convert_565_reverse(int width, int height,
                                    const void* srcbits, int srclinebytes,
                                    void* dstbits, int dstlinebytes)
{
    const DWORD* srcpixel;
    DWORD* dstpixel;
    int x,y;

    for (y=0; y<height; y++) {
        srcpixel=srcbits;
        dstpixel=dstbits;
        for (x=0; x<width/2; x++) {
            /* Do 2 pixels at a time */
            DWORD srcval;
            srcval=*srcpixel++;
            *dstpixel++=((srcval << 11) & 0xf800f800) | /* h */
                        ( srcval        & 0x07e007e0) | /* g */
                        ((srcval >> 11) & 0x001f001f);  /* l */
        }
        if (width&1) {
            /* And the the odd pixel */
            WORD srcval;
            srcval=*((WORD*)srcpixel);
            *((WORD*)dstpixel)=((srcval << 11) & 0xf800) | /* h */
                               ( srcval        & 0x07e0) | /* g */
                               ((srcval >> 11) & 0x001f);  /* l */
        }
        srcbits += srclinebytes;
        dstbits += dstlinebytes;
    }
}

static void X11DRV_DIB_Convert_565_to_555_asis(int width, int height,
                                    const void* srcbits, int srclinebytes,
                                    void* dstbits, int dstlinebytes)
{
    const DWORD* srcpixel;
    DWORD* dstpixel;
    int x,y;

    for (y=0; y<height; y++) {
        srcpixel=srcbits;
        dstpixel=dstbits;
        for (x=0; x<width/2; x++) {
            /* Do 2 pixels at a time */
            DWORD srcval;
            srcval=*srcpixel++;
            *dstpixel++=((srcval >> 1) & 0x7fe07fe0) | /* h, g */
                        ( srcval       & 0x001f001f);  /* l */
        }
        if (width&1) {
            /* And the the odd pixel */
            WORD srcval;
            srcval=*((WORD*)srcpixel);
            *((WORD*)dstpixel)=((srcval >> 1) & 0x7fe0) | /* h, g */
                               ( srcval       & 0x001f);  /* l */
        }
        srcbits += srclinebytes;
        dstbits += dstlinebytes;
    }
}

static void X11DRV_DIB_Convert_565_to_555_reverse(int width, int height,
                                    const void* srcbits, int srclinebytes,
                                    void* dstbits, int dstlinebytes)
{
    const DWORD* srcpixel;
    DWORD* dstpixel;
    int x,y;

    for (y=0; y<height; y++) {
        srcpixel=srcbits;
        dstpixel=dstbits;
        for (x=0; x<width/2; x++) {
            /* Do 2 pixels at a time */
            DWORD srcval;
            srcval=*srcpixel++;
            *dstpixel++=((srcval >> 11) & 0x001f001f) | /* h */
                        ((srcval >>  1) & 0x03e003e0) | /* g */
                        ((srcval << 10) & 0x7c007c00);  /* l */
        }
        if (width&1) {
            /* And the the odd pixel */
            WORD srcval;
            srcval=*((WORD*)srcpixel);
            *((WORD*)dstpixel)=((srcval >> 11) & 0x001f) | /* h */
                               ((srcval >>  1) & 0x03e0) | /* g */
                               ((srcval << 10) & 0x7c00);  /* l */
        }
        srcbits += srclinebytes;
        dstbits += dstlinebytes;
    }
}

static void X11DRV_DIB_Convert_565_to_888_asis(int width, int height,
                                    const void* srcbits, int srclinebytes,
                                    void* dstbits, int dstlinebytes)
{
    const WORD* srcpixel;
    BYTE* dstpixel;
    int x,y;

    for (y=0; y<height; y++) {
        srcpixel=srcbits;
        dstpixel=dstbits;
        for (x=0; x<width; x++) {
            WORD srcval;
            srcval=*srcpixel++;
            dstpixel[0]=((srcval <<  3) & 0xf8) | /* l */
                        ((srcval >>  2) & 0x07);  /* l - 3 bits */
            dstpixel[1]=((srcval >>  3) & 0xfc) | /* g */
                        ((srcval >>  9) & 0x03);  /* g - 2 bits */
            dstpixel[2]=((srcval >>  8) & 0xf8) | /* h */
                        ((srcval >> 13) & 0x07);  /* h - 3 bits */
            dstpixel+=3;
        }
        srcbits += srclinebytes;
        dstbits += dstlinebytes;
    }
}

static void X11DRV_DIB_Convert_565_to_888_reverse(int width, int height,
                                    const void* srcbits, int srclinebytes,
                                    void* dstbits, int dstlinebytes)
{
    const WORD* srcpixel;
    BYTE* dstpixel;
    int x,y;

    for (y=0; y<height; y++) {
        srcpixel=srcbits;
        dstpixel=dstbits;
        for (x=0; x<width; x++) {
            WORD srcval;
            srcval=*srcpixel++;
            dstpixel[0]=((srcval >>  8) & 0xf8) | /* h */
                        ((srcval >> 13) & 0x07);  /* h - 3 bits */
            dstpixel[1]=((srcval >>  3) & 0xfc) | /* g */
                        ((srcval >>  9) & 0x03);  /* g - 2 bits */
            dstpixel[2]=((srcval <<  3) & 0xf8) | /* l */
                        ((srcval >>  2) & 0x07);  /* l - 3 bits */
            dstpixel+=3;
        }
        srcbits += srclinebytes;
        dstbits += dstlinebytes;
    }
}

static void X11DRV_DIB_Convert_565_to_0888_asis(int width, int height,
                                    const void* srcbits, int srclinebytes,
                                    void* dstbits, int dstlinebytes)
{
    const WORD* srcpixel;
    DWORD* dstpixel;
    int x,y;

    for (y=0; y<height; y++) {
        srcpixel=srcbits;
        dstpixel=dstbits;
        for (x=0; x<width; x++) {
            WORD srcval;
            srcval=*srcpixel++;
            *dstpixel++=((srcval << 8) & 0xf80000) | /* h */
                        ((srcval << 3) & 0x070000) | /* h - 3 bits */
                        ((srcval << 5) & 0x00fc00) | /* g */
                        ((srcval >> 1) & 0x000300) | /* g - 2 bits */
                        ((srcval << 3) & 0x0000f8) | /* l */
                        ((srcval >> 2) & 0x000007);  /* l - 3 bits */
        }
        srcbits += srclinebytes;
        dstbits += dstlinebytes;
    }
}

static void X11DRV_DIB_Convert_565_to_0888_reverse(int width, int height,
                                    const void* srcbits, int srclinebytes,
                                    void* dstbits, int dstlinebytes)
{
    const WORD* srcpixel;
    DWORD* dstpixel;
    int x,y;

    for (y=0; y<height; y++) {
        srcpixel=srcbits;
        dstpixel=dstbits;
        for (x=0; x<width; x++) {
            WORD srcval;
            srcval=*srcpixel++;
            *dstpixel++=((srcval >>  8) & 0x0000f8) | /* h */
                        ((srcval >> 13) & 0x000007) | /* h - 3 bits */
                        ((srcval <<  5) & 0x00fc00) | /* g */
                        ((srcval >>  1) & 0x000300) | /* g - 2 bits */
                        ((srcval << 19) & 0xf80000) | /* l */
                        ((srcval << 14) & 0x070000);  /* l - 3 bits */
        }
        srcbits += srclinebytes;
        dstbits += dstlinebytes;
    }
}

/*
 * 24 bit conversions
 */

static void X11DRV_DIB_Convert_888_reverse(int width, int height,
                                    const void* srcbits, int srclinebytes,
                                    void* dstbits, int dstlinebytes)
{
    const BYTE* srcpixel;
    BYTE* dstpixel;
    int x,y;

    for (y=0; y<height; y++) {
        srcpixel=srcbits;
        dstpixel=dstbits;
        for (x=0; x<width; x++) {
            dstpixel[0]=srcpixel[2];
            dstpixel[1]=srcpixel[1];
            dstpixel[2]=srcpixel[0];
            srcpixel+=3;
            dstpixel+=3;
        }
        srcbits += srclinebytes;
        dstbits += dstlinebytes;
    }
}

static void X11DRV_DIB_Convert_888_to_555_asis(int width, int height,
                                    const void* srcbits, int srclinebytes,
                                    void* dstbits, int dstlinebytes)
{
    const DWORD* srcpixel;
    const BYTE* srcbyte;
    WORD* dstpixel;
    int x,y;
    int oddwidth;

    oddwidth=width & 3;
    width=width/4;
    for (y=0; y<height; y++) {
        srcpixel=srcbits;
        dstpixel=dstbits;
        for (x=0; x<width; x++) {
            /* Do 4 pixels at a time: 3 dwords in and 4 words out */
            DWORD srcval1,srcval2;
            srcval1=srcpixel[0];
            dstpixel[0]=((srcval1 >>  3) & 0x001f) | /* l1 */
                        ((srcval1 >>  6) & 0x03e0) | /* g1 */
                        ((srcval1 >>  9) & 0x7c00);  /* h1 */
            srcval2=srcpixel[1];
            dstpixel[1]=((srcval1 >> 27) & 0x001f) | /* l2 */
                        ((srcval2 <<  2) & 0x03e0) | /* g2 */
                        ((srcval2 >>  1) & 0x7c00);  /* h2 */
            srcval1=srcpixel[2];
            dstpixel[2]=((srcval2 >> 19) & 0x001f) | /* l3 */
                        ((srcval2 >> 22) & 0x03e0) | /* g3 */
                        ((srcval1 <<  7) & 0x7c00);  /* h3 */
            dstpixel[3]=((srcval1 >> 11) & 0x001f) | /* l4 */
                        ((srcval1 >> 14) & 0x03e0) | /* g4 */
                        ((srcval1 >> 17) & 0x7c00);  /* h4 */
            srcpixel+=3;
            dstpixel+=4;
        }
        /* And now up to 3 odd pixels */
        srcbyte=(LPBYTE)srcpixel;
        for (x=0; x<oddwidth; x++) {
            WORD dstval;
            dstval =((srcbyte[0] >> 3) & 0x001f);    /* l */
            dstval|=((srcbyte[1] << 2) & 0x03e0);    /* g */
            dstval|=((srcbyte[2] << 7) & 0x7c00);    /* h */
            *dstpixel++=dstval;
            srcbyte+=3;
        }
        srcbits += srclinebytes;
        dstbits += dstlinebytes;
    }
}

static void X11DRV_DIB_Convert_888_to_555_reverse(int width, int height,
                                    const void* srcbits, int srclinebytes,
                                    void* dstbits, int dstlinebytes)
{
    const DWORD* srcpixel;
    const BYTE* srcbyte;
    WORD* dstpixel;
    int x,y;
    int oddwidth;

    oddwidth=width & 3;
    width=width/4;
    for (y=0; y<height; y++) {
        srcpixel=srcbits;
        dstpixel=dstbits;
        for (x=0; x<width; x++) {
            /* Do 4 pixels at a time: 3 dwords in and 4 words out */
            DWORD srcval1,srcval2;
            srcval1=srcpixel[0];
            dstpixel[0]=((srcval1 <<  7) & 0x7c00) | /* l1 */
                        ((srcval1 >>  6) & 0x03e0) | /* g1 */
                        ((srcval1 >> 19) & 0x001f);  /* h1 */
            srcval2=srcpixel[1];
            dstpixel[1]=((srcval1 >> 17) & 0x7c00) | /* l2 */
                        ((srcval2 <<  2) & 0x03e0) | /* g2 */
                        ((srcval2 >> 11) & 0x001f);  /* h2 */
            srcval1=srcpixel[2];
            dstpixel[2]=((srcval2 >>  9) & 0x7c00) | /* l3 */
                        ((srcval2 >> 22) & 0x03e0) | /* g3 */
                        ((srcval1 >>  3) & 0x001f);  /* h3 */
            dstpixel[3]=((srcval1 >>  1) & 0x7c00) | /* l4 */
                        ((srcval1 >> 14) & 0x03e0) | /* g4 */
                        ((srcval1 >> 27) & 0x001f);  /* h4 */
            srcpixel+=3;
            dstpixel+=4;
        }
        /* And now up to 3 odd pixels */
        srcbyte=(LPBYTE)srcpixel;
        for (x=0; x<oddwidth; x++) {
            WORD dstval;
            dstval =((srcbyte[0] << 7) & 0x7c00);    /* l */
            dstval|=((srcbyte[1] << 2) & 0x03e0);    /* g */
            dstval|=((srcbyte[2] >> 3) & 0x001f);    /* h */
            *dstpixel++=dstval;
            srcbyte+=3;
       }
        srcbits += srclinebytes;
        dstbits += dstlinebytes;
    }
}

static void X11DRV_DIB_Convert_888_to_565_asis(int width, int height,
                                    const void* srcbits, int srclinebytes,
                                    void* dstbits, int dstlinebytes)
{
    const DWORD* srcpixel;
    const BYTE* srcbyte;
    WORD* dstpixel;
    int x,y;
    int oddwidth;

    oddwidth=width & 3;
    width=width/4;
    for (y=0; y<height; y++) {
        srcpixel=srcbits;
        dstpixel=dstbits;
        for (x=0; x<width; x++) {
            /* Do 4 pixels at a time: 3 dwords in and 4 words out */
            DWORD srcval1,srcval2;
            srcval1=srcpixel[0];
            dstpixel[0]=((srcval1 >>  3) & 0x001f) | /* l1 */
                        ((srcval1 >>  5) & 0x07e0) | /* g1 */
                        ((srcval1 >>  8) & 0xf800);  /* h1 */
            srcval2=srcpixel[1];
            dstpixel[1]=((srcval1 >> 27) & 0x001f) | /* l2 */
                        ((srcval2 <<  3) & 0x07e0) | /* g2 */
                        ( srcval2        & 0xf800);  /* h2 */
            srcval1=srcpixel[2];
            dstpixel[2]=((srcval2 >> 19) & 0x001f) | /* l3 */
                        ((srcval2 >> 21) & 0x07e0) | /* g3 */
                        ((srcval1 <<  8) & 0xf800);  /* h3 */
            dstpixel[3]=((srcval1 >> 11) & 0x001f) | /* l4 */
                        ((srcval1 >> 13) & 0x07e0) | /* g4 */
                        ((srcval1 >> 16) & 0xf800);  /* h4 */
            srcpixel+=3;
            dstpixel+=4;
        }
        /* And now up to 3 odd pixels */
        srcbyte=(LPBYTE)srcpixel;
        for (x=0; x<oddwidth; x++) {
            WORD dstval;
            dstval =((srcbyte[0] >> 3) & 0x001f);    /* l */
            dstval|=((srcbyte[1] << 3) & 0x07e0);    /* g */
            dstval|=((srcbyte[2] << 8) & 0xf800);    /* h */
            *dstpixel++=dstval;
            srcbyte+=3;
        }
        srcbits += srclinebytes;
        dstbits += dstlinebytes;
    }
}

static void X11DRV_DIB_Convert_888_to_565_reverse(int width, int height,
                                    const void* srcbits, int srclinebytes,
                                    void* dstbits, int dstlinebytes)
{
    const DWORD* srcpixel;
    const BYTE* srcbyte;
    WORD* dstpixel;
    int x,y;
    int oddwidth;

    oddwidth=width & 3;
    width=width/4;
    for (y=0; y<height; y++) {
        srcpixel=srcbits;
        dstpixel=dstbits;
        for (x=0; x<width; x++) {
            /* Do 4 pixels at a time: 3 dwords in and 4 words out */
            DWORD srcval1,srcval2;
            srcval1=srcpixel[0];
            dstpixel[0]=((srcval1 <<  8) & 0xf800) | /* l1 */
                        ((srcval1 >>  5) & 0x07e0) | /* g1 */
                        ((srcval1 >> 19) & 0x001f);  /* h1 */
            srcval2=srcpixel[1];
            dstpixel[1]=((srcval1 >> 16) & 0xf800) | /* l2 */
                        ((srcval2 <<  3) & 0x07e0) | /* g2 */
                        ((srcval2 >> 11) & 0x001f);  /* h2 */
            srcval1=srcpixel[2];
            dstpixel[2]=((srcval2 >>  8) & 0xf800) | /* l3 */
                        ((srcval2 >> 21) & 0x07e0) | /* g3 */
                        ((srcval1 >>  3) & 0x001f);  /* h3 */
            dstpixel[3]=(srcval1         & 0xf800) | /* l4 */
                        ((srcval1 >> 13) & 0x07e0) | /* g4 */
                        ((srcval1 >> 27) & 0x001f);  /* h4 */
            srcpixel+=3;
            dstpixel+=4;
        }
        /* And now up to 3 odd pixels */
        srcbyte=(LPBYTE)srcpixel;
        for (x=0; x<oddwidth; x++) {
            WORD dstval;
            dstval =((srcbyte[0] << 8) & 0xf800);    /* l */
            dstval|=((srcbyte[1] << 3) & 0x07e0);    /* g */
            dstval|=((srcbyte[2] >> 3) & 0x001f);    /* h */
            *dstpixel++=dstval;
            srcbyte+=3;
        }
        srcbits += srclinebytes;
        dstbits += dstlinebytes;
    }
}

static void X11DRV_DIB_Convert_888_to_0888_asis(int width, int height,
                                    const void* srcbits, int srclinebytes,
                                    void* dstbits, int dstlinebytes)
{
    const DWORD* srcpixel;
    DWORD* dstpixel;
    int x,y;
    int oddwidth;

    oddwidth=width & 3;
    width=width/4;
    for (y=0; y<height; y++) {
        srcpixel=srcbits;
        dstpixel=dstbits;
        for (x=0; x<width; x++) {
            /* Do 4 pixels at a time: 3 dwords in and 4 dwords out */
            DWORD srcval1,srcval2;
            srcval1=srcpixel[0];
            dstpixel[0]=( srcval1        & 0x00ffffff);  /* h1, g1, l1 */
            srcval2=srcpixel[1];
            dstpixel[1]=( srcval1 >> 24) |              /* l2 */
                        ((srcval2 <<  8) & 0x00ffff00); /* h2, g2 */
            srcval1=srcpixel[2];
            dstpixel[2]=( srcval2 >> 16) |              /* g3, l3 */
                        ((srcval1 << 16) & 0x00ff0000); /* h3 */
            dstpixel[3]=( srcval1 >>  8);               /* h4, g4, l4 */
            srcpixel+=3;
            dstpixel+=4;
        }
        /* And now up to 3 odd pixels */
        for (x=0; x<oddwidth; x++) {
            DWORD srcval;
            srcval=*srcpixel;
            srcpixel=(LPDWORD)(((char*)srcpixel)+3);
            *dstpixel++=( srcval         & 0x00ffffff); /* h, g, l */
        }
        srcbits += srclinebytes;
        dstbits += dstlinebytes;
    }
}

static void X11DRV_DIB_Convert_888_to_0888_reverse(int width, int height,
                                    const void* srcbits, int srclinebytes,
                                    void* dstbits, int dstlinebytes)
{
    const DWORD* srcpixel;
    DWORD* dstpixel;
    int x,y;
    int oddwidth;

    oddwidth=width & 3;
    width=width/4;
    for (y=0; y<height; y++) {
        srcpixel=srcbits;
        dstpixel=dstbits;
        for (x=0; x<width; x++) {
            /* Do 4 pixels at a time: 3 dwords in and 4 dwords out */
            DWORD srcval1,srcval2;

            srcval1=srcpixel[0];
            dstpixel[0]=((srcval1 >> 16) & 0x0000ff) | /* h1 */
                        ( srcval1        & 0x00ff00) | /* g1 */
                        ((srcval1 << 16) & 0xff0000);  /* l1 */
            srcval2=srcpixel[1];
            dstpixel[1]=((srcval1 >>  8) & 0xff0000) | /* l2 */
                        ((srcval2 <<  8) & 0x00ff00) | /* g2 */
                        ((srcval2 >>  8) & 0x0000ff);  /* h2 */
            srcval1=srcpixel[2];
            dstpixel[2]=( srcval2        & 0xff0000) | /* l3 */
                        ((srcval2 >> 16) & 0x00ff00) | /* g3 */
                        ( srcval1        & 0x0000ff);  /* h3 */
            dstpixel[3]=((srcval1 >> 24) & 0x0000ff) | /* h4 */
                        ((srcval1 >>  8) & 0x00ff00) | /* g4 */
                        ((srcval1 <<  8) & 0xff0000);  /* l4 */
            srcpixel+=3;
            dstpixel+=4;
        }
        /* And now up to 3 odd pixels */
        for (x=0; x<oddwidth; x++) {
            DWORD srcval;
            srcval=*srcpixel;
            srcpixel=(LPDWORD)(((char*)srcpixel)+3);
            *dstpixel++=((srcval  >> 16) & 0x0000ff) | /* h */
                        ( srcval         & 0x00ff00) | /* g */
                        ((srcval  << 16) & 0xff0000);  /* l */
        }
        srcbits += srclinebytes;
        dstbits += dstlinebytes;
    }
}

static void X11DRV_DIB_Convert_rgb888_to_any0888(int width, int height,
                                    const void* srcbits, int srclinebytes,
                                    void* dstbits, int dstlinebytes,
                                    DWORD rdst, DWORD gdst, DWORD bdst)
{
    int rLeftShift,gLeftShift,bLeftShift;
    const BYTE* srcpixel;
    DWORD* dstpixel;
    int x,y;

    rLeftShift=X11DRV_DIB_MaskToShift(rdst);
    gLeftShift=X11DRV_DIB_MaskToShift(gdst);
    bLeftShift=X11DRV_DIB_MaskToShift(bdst);
    for (y=0; y<height; y++) {
        srcpixel=srcbits;
        dstpixel=dstbits;
        for (x=0; x<width; x++) {
            *dstpixel++=(srcpixel[0] << bLeftShift) | /* b */
                        (srcpixel[1] << gLeftShift) | /* g */
                        (srcpixel[2] << rLeftShift);  /* r */
            srcpixel+=3;
        }
        srcbits += srclinebytes;
        dstbits += dstlinebytes;
    }
}

static void X11DRV_DIB_Convert_bgr888_to_any0888(int width, int height,
                                    const void* srcbits, int srclinebytes,
                                    void* dstbits, int dstlinebytes,
                                    DWORD rdst, DWORD gdst, DWORD bdst)
{
    int rLeftShift,gLeftShift,bLeftShift;
    const BYTE* srcpixel;
    DWORD* dstpixel;
    int x,y;

    rLeftShift=X11DRV_DIB_MaskToShift(rdst);
    gLeftShift=X11DRV_DIB_MaskToShift(gdst);
    bLeftShift=X11DRV_DIB_MaskToShift(bdst);
    for (y=0; y<height; y++) {
        srcpixel=srcbits;
        dstpixel=dstbits;
        for (x=0; x<width; x++) {
            *dstpixel++=(srcpixel[0] << rLeftShift) | /* r */
                        (srcpixel[1] << gLeftShift) | /* g */
                        (srcpixel[2] << bLeftShift);  /* b */
            srcpixel+=3;
        }
        srcbits += srclinebytes;
        dstbits += dstlinebytes;
    }
}

/*
 * 32 bit conversions
 */

static void X11DRV_DIB_Convert_0888_reverse(int width, int height,
                                    const void* srcbits, int srclinebytes,
                                    void* dstbits, int dstlinebytes)
{
    const DWORD* srcpixel;
    DWORD* dstpixel;
    int x,y;

    for (y=0; y<height; y++) {
        srcpixel=srcbits;
        dstpixel=dstbits;
        for (x=0; x<width; x++) {
            DWORD srcval;
            srcval=*srcpixel++;
            *dstpixel++=((srcval << 16) & 0x00ff0000) | /* h */
                        ( srcval        & 0x0000ff00) | /* g */
                        ((srcval >> 16) & 0x000000ff);  /* l */
        }
        srcbits += srclinebytes;
        dstbits += dstlinebytes;
    }
}

static void X11DRV_DIB_Convert_0888_any(int width, int height,
                                    const void* srcbits, int srclinebytes,
                                    DWORD rsrc, DWORD gsrc, DWORD bsrc,
                                    void* dstbits, int dstlinebytes,
                                    DWORD rdst, DWORD gdst, DWORD bdst)
{
    int rRightShift,gRightShift,bRightShift;
    int rLeftShift,gLeftShift,bLeftShift;
    const DWORD* srcpixel;
    DWORD* dstpixel;
    int x,y;

    rRightShift=X11DRV_DIB_MaskToShift(rsrc);
    gRightShift=X11DRV_DIB_MaskToShift(gsrc);
    bRightShift=X11DRV_DIB_MaskToShift(bsrc);
    rLeftShift=X11DRV_DIB_MaskToShift(rdst);
    gLeftShift=X11DRV_DIB_MaskToShift(gdst);
    bLeftShift=X11DRV_DIB_MaskToShift(bdst);
    for (y=0; y<height; y++) {
        srcpixel=srcbits;
        dstpixel=dstbits;
        for (x=0; x<width; x++) {
            DWORD srcval;
            srcval=*srcpixel++;
            *dstpixel++=(((srcval >> rRightShift) & 0xff) << rLeftShift) |
                        (((srcval >> gRightShift) & 0xff) << gLeftShift) |
                        (((srcval >> bRightShift) & 0xff) << bLeftShift);
        }
        srcbits += srclinebytes;
        dstbits += dstlinebytes;
    }
}

static void X11DRV_DIB_Convert_0888_to_555_asis(int width, int height,
                                    const void* srcbits, int srclinebytes,
                                    void* dstbits, int dstlinebytes)
{
    const DWORD* srcpixel;
    WORD* dstpixel;
    int x,y;

    for (y=0; y<height; y++) {
        srcpixel=srcbits;
        dstpixel=dstbits;
        for (x=0; x<width; x++) {
            DWORD srcval;
            srcval=*srcpixel++;
            *dstpixel++=((srcval >> 9) & 0x7c00) | /* h */
                        ((srcval >> 6) & 0x03e0) | /* g */
                        ((srcval >> 3) & 0x001f);  /* l */
        }
        srcbits += srclinebytes;
        dstbits += dstlinebytes;
    }
}

static void X11DRV_DIB_Convert_0888_to_555_reverse(int width, int height,
                                    const void* srcbits, int srclinebytes,
                                    void* dstbits, int dstlinebytes)
{
    const DWORD* srcpixel;
    WORD* dstpixel;
    int x,y;

    for (y=0; y<height; y++) {
        srcpixel=srcbits;
        dstpixel=dstbits;
        for (x=0; x<width; x++) {
            DWORD srcval;
            srcval=*srcpixel++;
            *dstpixel++=((srcval >> 19) & 0x001f) | /* h */
                        ((srcval >>  6) & 0x03e0) | /* g */
                        ((srcval <<  7) & 0x7c00);  /* l */
        }
        srcbits += srclinebytes;
        dstbits += dstlinebytes;
    }
}

static void X11DRV_DIB_Convert_0888_to_565_asis(int width, int height,
                                    const void* srcbits, int srclinebytes,
                                    void* dstbits, int dstlinebytes)
{
    const DWORD* srcpixel;
    WORD* dstpixel;
    int x,y;

    for (y=0; y<height; y++) {
        srcpixel=srcbits;
        dstpixel=dstbits;
        for (x=0; x<width; x++) {
            DWORD srcval;
            srcval=*srcpixel++;
            *dstpixel++=((srcval >> 8) & 0xf800) | /* h */
                        ((srcval >> 5) & 0x07e0) | /* g */
                        ((srcval >> 3) & 0x001f);  /* l */
        }
        srcbits += srclinebytes;
        dstbits += dstlinebytes;
    }
}

static void X11DRV_DIB_Convert_0888_to_565_reverse(int width, int height,
                                    const void* srcbits, int srclinebytes,
                                    void* dstbits, int dstlinebytes)
{
    const DWORD* srcpixel;
    WORD* dstpixel;
    int x,y;

    for (y=0; y<height; y++) {
        srcpixel=srcbits;
        dstpixel=dstbits;
        for (x=0; x<width; x++) {
            DWORD srcval;
            srcval=*srcpixel++;
            *dstpixel++=((srcval >> 19) & 0x001f) | /* h */
                        ((srcval >>  5) & 0x07e0) | /* g */
                        ((srcval <<  8) & 0xf800);  /* l */
        }
        srcbits += srclinebytes;
        dstbits += dstlinebytes;
    }
}

static void X11DRV_DIB_Convert_any0888_to_5x5(int width, int height,
                                    const void* srcbits, int srclinebytes,
                                    DWORD rsrc, DWORD gsrc, DWORD bsrc,
                                    void* dstbits, int dstlinebytes,
                                    WORD rdst, WORD gdst, WORD bdst)
{
    int rRightShift,gRightShift,bRightShift;
    int rLeftShift,gLeftShift,bLeftShift;
    const DWORD* srcpixel;
    WORD* dstpixel;
    int x,y;

    /* Here is how we proceed. Assume we have rsrc=0x0000ff00 and our pixel
     * contains 0x11223344.
     * - first we shift 0x11223344 right by rRightShift to bring the most 
     *   significant bits of the red components in the bottom 5 (or 6) bits
     *   -> 0x4488c
     * - then we remove non red bits by anding with the modified rdst (0x1f)
     *   -> 0x0c
     * - finally shift these bits left by rLeftShift so that they end up in 
     *   the right place
     *   -> 0x3000
     */
    rRightShift=X11DRV_DIB_MaskToShift(rsrc)+3;
    gRightShift=X11DRV_DIB_MaskToShift(gsrc);
    gRightShift+=(gdst==0x07e0?2:3);
    bRightShift=X11DRV_DIB_MaskToShift(bsrc)+3;

    rLeftShift=X11DRV_DIB_MaskToShift(rdst);
    rdst=rdst >> rLeftShift;
    gLeftShift=X11DRV_DIB_MaskToShift(gdst);
    gdst=gdst >> gLeftShift;
    bLeftShift=X11DRV_DIB_MaskToShift(bdst);
    bdst=bdst >> bLeftShift;

    for (y=0; y<height; y++) {
        srcpixel=srcbits;
        dstpixel=dstbits;
        for (x=0; x<width; x++) {
            DWORD srcval;
            srcval=*srcpixel++;
            *dstpixel++=(((srcval >> rRightShift) & rdst) << rLeftShift) |
                        (((srcval >> gRightShift) & gdst) << gLeftShift) |
                        (((srcval >> bRightShift) & bdst) << bLeftShift);
        }
        srcbits += srclinebytes;
        dstbits += dstlinebytes;
    }
}

static void X11DRV_DIB_Convert_0888_to_888_asis(int width, int height,
                                    const void* srcbits, int srclinebytes,
                                    void* dstbits, int dstlinebytes)
{
    const DWORD* srcpixel;
    DWORD* dstpixel;
    BYTE* dstbyte;
    int x,y;
    int oddwidth;

    oddwidth=width & 3;
    width=width/4;
    for (y=0; y<height; y++) {
        srcpixel=srcbits;
        dstpixel=dstbits;
        for (x=0; x<width; x++) {
            /* Do 4 pixels at a time: 4 dwords in and 3 dwords out */
            DWORD srcval;
            srcval=((*srcpixel++)       & 0x00ffffff);  /* h1, g1, l1*/
            *dstpixel++=srcval | ((*srcpixel)   << 24); /* h2 */
            srcval=((*srcpixel++ >> 8 ) & 0x0000ffff);  /* g2, l2 */
            *dstpixel++=srcval | ((*srcpixel)   << 16); /* h3, g3 */
            srcval=((*srcpixel++ >> 16) & 0x000000ff);  /* l3 */
            *dstpixel++=srcval | ((*srcpixel++) << 8);  /* h4, g4, l4 */
        }
        /* And now up to 3 odd pixels */
        dstbyte=(BYTE*)dstpixel;
        for (x=0; x<oddwidth; x++) {
            DWORD srcval;
            srcval=*srcpixel++;
            *((WORD*)dstbyte)++=srcval;                 /* h, g */
            *dstbyte++=srcval >> 16;                    /* l */
        }
        srcbits += srclinebytes;
        dstbits += dstlinebytes;
    }
}

static void X11DRV_DIB_Convert_0888_to_888_reverse(int width, int height,
                                    const void* srcbits, int srclinebytes,
                                    void* dstbits, int dstlinebytes)
{
    const DWORD* srcpixel;
    DWORD* dstpixel;
    BYTE* dstbyte;
    int x,y;
    int oddwidth;

    oddwidth=width & 3;
    width=width/4;
    for (y=0; y<height; y++) {
        srcpixel=srcbits;
        dstpixel=dstbits;
        for (x=0; x<width; x++) {
            /* Do 4 pixels at a time: 4 dwords in and 3 dwords out */
            DWORD srcval1,srcval2;
            srcval1=*srcpixel++;
            srcval2=    ((srcval1 >> 16) & 0x000000ff) | /* h1 */
                        ( srcval1        & 0x0000ff00) | /* g1 */
                        ((srcval1 << 16) & 0x00ff0000);  /* l1 */
            srcval1=*srcpixel++;
            *dstpixel++=srcval2 |
                        ((srcval1 <<  8) & 0xff000000);  /* h2 */
            srcval2=    ((srcval1 >>  8) & 0x000000ff) | /* g2 */
                        ((srcval1 <<  8) & 0x0000ff00);  /* l2 */
            srcval1=*srcpixel++;
            *dstpixel++=srcval2 |
                        ( srcval1        & 0x00ff0000) | /* h3 */
                        ((srcval1 << 16) & 0xff000000);  /* g3 */
            srcval2=    ( srcval1        & 0x000000ff);  /* l3 */
            srcval1=*srcpixel++;
            *dstpixel++=srcval2 |
                        ((srcval1 >>  8) & 0x0000ff00) | /* h4 */
                        ((srcval1 <<  8) & 0x00ff0000) | /* g4 */
                        ( srcval1 << 24);                /* l4 */
        }
        /* And now up to 3 odd pixels */
        dstbyte=(BYTE*)dstpixel;
        for (x=0; x<oddwidth; x++) {
            DWORD srcval;
            srcval=*srcpixel++;
            *((WORD*)dstbyte)++=((srcval >> 16) & 0x00ff) | /* h */
                                (srcval         & 0xff00);  /* g */
            *dstbyte++=srcval;                              /* l */
        }
        srcbits += srclinebytes;
        dstbits += dstlinebytes;
    }
}

static void X11DRV_DIB_Convert_any0888_to_rgb888(int width, int height,
                                    const void* srcbits, int srclinebytes,
                                    DWORD rsrc, DWORD gsrc, DWORD bsrc,
                                    void* dstbits, int dstlinebytes)
{
    int rRightShift,gRightShift,bRightShift;
    const DWORD* srcpixel;
    BYTE* dstpixel;
    int x,y;

    rRightShift=X11DRV_DIB_MaskToShift(rsrc);
    gRightShift=X11DRV_DIB_MaskToShift(gsrc);
    bRightShift=X11DRV_DIB_MaskToShift(bsrc);
    for (y=0; y<height; y++) {
        srcpixel=srcbits;
        dstpixel=dstbits;
        for (x=0; x<width; x++) {
            DWORD srcval;
            srcval=*srcpixel++;
            dstpixel[0]=(srcval >> bRightShift); /* b */
            dstpixel[1]=(srcval >> gRightShift); /* g */
            dstpixel[2]=(srcval >> rRightShift); /* r */
            dstpixel+=3;
        }
        srcbits += srclinebytes;
        dstbits += dstlinebytes;
    }
}

static void X11DRV_DIB_Convert_any0888_to_bgr888(int width, int height,
                                    const void* srcbits, int srclinebytes,
                                    DWORD rsrc, DWORD gsrc, DWORD bsrc,
                                    void* dstbits, int dstlinebytes)
{
    int rRightShift,gRightShift,bRightShift;
    const DWORD* srcpixel;
    BYTE* dstpixel;
    int x,y;

    rRightShift=X11DRV_DIB_MaskToShift(rsrc);
    gRightShift=X11DRV_DIB_MaskToShift(gsrc);
    bRightShift=X11DRV_DIB_MaskToShift(bsrc);
    for (y=0; y<height; y++) {
        srcpixel=srcbits;
        dstpixel=dstbits;
        for (x=0; x<width; x++) {
            DWORD srcval;
            srcval=*srcpixel++;
            dstpixel[0]=(srcval >> rRightShift); /* r */
            dstpixel[1]=(srcval >> gRightShift); /* g */
            dstpixel[2]=(srcval >> bRightShift); /* b */
            dstpixel+=3;
        }
        srcbits += srclinebytes;
        dstbits += dstlinebytes;
    }
}

/***********************************************************************
 *           X11DRV_DIB_SetImageBits_1
 *
 * SetDIBits for a 1-bit deep DIB.
 */
static void X11DRV_DIB_SetImageBits_1( int lines, const BYTE *srcbits,
                                DWORD srcwidth, DWORD dstwidth, int left,
                                int *colors, XImage *bmpImage, DWORD linebytes)
{
    int h;
    const BYTE* srcbyte;
    BYTE srcval, extra;
    DWORD i, x;

    if (lines < 0 ) {
        lines = -lines;
        srcbits = srcbits + linebytes * (lines - 1);
        linebytes = -linebytes;
    }

    if ((extra = (left & 7)) != 0) {
        left &= ~7;
        dstwidth += extra;
    }
    srcbits += left >> 3;

    /* ==== pal 1 dib -> any bmp format ==== */
    for (h = lines-1; h >=0; h--) {
        srcbyte=srcbits;
        /* FIXME: should avoid putting x<left pixels (minor speed issue) */
        for (i = dstwidth/8, x = left; i > 0; i--) {
            srcval=*srcbyte++;
            XPutPixel( bmpImage, x++, h, colors[ srcval >> 7] );
            XPutPixel( bmpImage, x++, h, colors[(srcval >> 6) & 1] );
            XPutPixel( bmpImage, x++, h, colors[(srcval >> 5) & 1] );
            XPutPixel( bmpImage, x++, h, colors[(srcval >> 4) & 1] );
            XPutPixel( bmpImage, x++, h, colors[(srcval >> 3) & 1] );
            XPutPixel( bmpImage, x++, h, colors[(srcval >> 2) & 1] );
            XPutPixel( bmpImage, x++, h, colors[(srcval >> 1) & 1] );
            XPutPixel( bmpImage, x++, h, colors[ srcval       & 1] );
        }
        srcval=*srcbyte;
        switch (dstwidth & 7)
        {
        case 7: XPutPixel(bmpImage, x++, h, colors[srcval >> 7]); srcval<<=1;
        case 6: XPutPixel(bmpImage, x++, h, colors[srcval >> 7]); srcval<<=1;
        case 5: XPutPixel(bmpImage, x++, h, colors[srcval >> 7]); srcval<<=1;
        case 4: XPutPixel(bmpImage, x++, h, colors[srcval >> 7]); srcval<<=1;
        case 3: XPutPixel(bmpImage, x++, h, colors[srcval >> 7]); srcval<<=1;
        case 2: XPutPixel(bmpImage, x++, h, colors[srcval >> 7]); srcval<<=1;
        case 1: XPutPixel(bmpImage, x++, h, colors[srcval >> 7]);
        }
        srcbits += linebytes;
    }
}

/***********************************************************************
 *           X11DRV_DIB_GetImageBits_1
 *
 * GetDIBits for a 1-bit deep DIB.
 */
static void X11DRV_DIB_GetImageBits_1( int lines, BYTE *dstbits,
				       DWORD dstwidth, DWORD srcwidth,
				       RGBQUAD *colors, PALETTEENTRY *srccolors, 
                                XImage *bmpImage, DWORD linebytes )
{
    DWORD x;
    int h;

    if (lines < 0 ) {
        lines = -lines;
        dstbits = dstbits + linebytes * (lines - 1);
        linebytes = -linebytes;
    }

    switch (bmpImage->depth)
    {
    case 1:
    case 4:
        if (bmpImage->red_mask==0 && bmpImage->green_mask==0 && bmpImage->blue_mask==0 && srccolors) {
            /* ==== pal 1 or 4 bmp -> pal 1 dib ==== */
            BYTE* dstbyte;

            for (h=lines-1; h>=0; h--) {
                BYTE dstval;
                dstbyte=dstbits;
                dstval=0;
                for (x=0; x<dstwidth; x++) {
                    PALETTEENTRY srcval;
                    srcval=srccolors[XGetPixel(bmpImage, x, h)];
                    dstval|=(X11DRV_DIB_GetNearestIndex
                             (colors, 2,
                              srcval.peRed,
                              srcval.peGreen,
                              srcval.peBlue) << (7 - (x & 7)));
                    if ((x&7)==7) {
                        *dstbyte++=dstval;
                        dstval=0;
                    }
                }
                if ((dstwidth&7)!=0) {
                    *dstbyte=dstval;
                }
                dstbits += linebytes;
            }
        } else {
            goto notsupported;
        }
        break;

    case 8:
        if (bmpImage->red_mask==0 && bmpImage->green_mask==0 && bmpImage->blue_mask==0 && srccolors) {
            /* ==== pal 8 bmp -> pal 1 dib ==== */
            const void* srcbits;
            const BYTE* srcpixel;
            BYTE* dstbyte;

            srcbits=bmpImage->data+(lines-1)*bmpImage->bytes_per_line;

            for (h=0; h<lines; h++) {
                BYTE dstval;
                srcpixel=srcbits;
                dstbyte=dstbits;
                dstval=0;
                for (x=0; x<dstwidth; x++) {
                    PALETTEENTRY srcval;
                    srcval=srccolors[(int)*srcpixel++];
                    dstval|=(X11DRV_DIB_GetNearestIndex
                             (colors, 2,
                              srcval.peRed,
                              srcval.peGreen,
                              srcval.peBlue) << (7-(x&7)) );
                    if ((x&7)==7) {
                        *dstbyte++=dstval;
                        dstval=0;
                    }
                }
                if ((dstwidth&7)!=0) {
                    *dstbyte=dstval;
                }
                srcbits -= bmpImage->bytes_per_line;
                dstbits += linebytes;
            }
        } else {
            goto notsupported;
        }
        break;

    case 15:
    case 16:
        {
            const void* srcbits;
            const WORD* srcpixel;
            BYTE* dstbyte;

            srcbits=bmpImage->data+(lines-1)*bmpImage->bytes_per_line;

            if (bmpImage->green_mask==0x03e0) {
                if (bmpImage->red_mask==0x7c00) {
                    /* ==== rgb 555 bmp -> pal 1 dib ==== */
                    for (h=0; h<lines; h++) {
                        BYTE dstval;
                        srcpixel=srcbits;
                        dstbyte=dstbits;
                        dstval=0;
                        for (x=0; x<dstwidth; x++) {
                            WORD srcval;
                            srcval=*srcpixel++;
                            dstval|=(X11DRV_DIB_GetNearestIndex
                                     (colors, 2,
                                      ((srcval >>  7) & 0xf8) | /* r */
                                      ((srcval >> 12) & 0x07),
                                      ((srcval >>  2) & 0xf8) | /* g */
                                      ((srcval >>  7) & 0x07),
                                      ((srcval <<  3) & 0xf8) | /* b */
                                      ((srcval >>  2) & 0x07) ) << (7-(x&7)) );
                            if ((x&7)==7) {
                                *dstbyte++=dstval;
                                dstval=0;
                            }
                        }
                        if ((dstwidth&7)!=0) {
                            *dstbyte=dstval;
                        }
                        srcbits -= bmpImage->bytes_per_line;
                        dstbits += linebytes;
                    }
                } else if (bmpImage->blue_mask==0x7c00) {
                    /* ==== bgr 555 bmp -> pal 1 dib ==== */
                    for (h=0; h<lines; h++) {
                        WORD dstval;
                        srcpixel=srcbits;
                        dstbyte=dstbits;
                        dstval=0;
                        for (x=0; x<dstwidth; x++) {
                            BYTE srcval;
                            srcval=*srcpixel++;
                            dstval|=(X11DRV_DIB_GetNearestIndex
                                     (colors, 2,
                                      ((srcval <<  3) & 0xf8) | /* r */
                                      ((srcval >>  2) & 0x07),
                                      ((srcval >>  2) & 0xf8) | /* g */
                                      ((srcval >>  7) & 0x07),
                                      ((srcval >>  7) & 0xf8) | /* b */
                                      ((srcval >> 12) & 0x07) ) << (7-(x&7)) );
                            if ((x&7)==7) {
                                *dstbyte++=dstval;
                                dstval=0;
                            }
                        }
                        if ((dstwidth&7)!=0) {
                            *dstbyte=dstval;
                        }
                        srcbits -= bmpImage->bytes_per_line;
                        dstbits += linebytes;
                    }
                } else {
                    goto notsupported;
                }
            } else if (bmpImage->green_mask==0x07e0) {
                if (bmpImage->red_mask==0xf800) {
                    /* ==== rgb 565 bmp -> pal 1 dib ==== */
                    for (h=0; h<lines; h++) {
                        BYTE dstval;
                        srcpixel=srcbits;
                        dstbyte=dstbits;
                        dstval=0;
                        for (x=0; x<dstwidth; x++) {
                            WORD srcval;
                            srcval=*srcpixel++;
                            dstval|=(X11DRV_DIB_GetNearestIndex
                                     (colors, 2,
                                      ((srcval >>  8) & 0xf8) | /* r */
                                      ((srcval >> 13) & 0x07),
                                      ((srcval >>  3) & 0xfc) | /* g */
                                      ((srcval >>  9) & 0x03),
                                      ((srcval <<  3) & 0xf8) | /* b */
                                      ((srcval >>  2) & 0x07) ) << (7-(x&7)) );
                            if ((x&7)==7) {
                                *dstbyte++=dstval;
                                dstval=0;
                            }
                        }
                        if ((dstwidth&7)!=0) {
                            *dstbyte=dstval;
                        }
                        srcbits -= bmpImage->bytes_per_line;
                        dstbits += linebytes;
                    }
                } else if (bmpImage->blue_mask==0xf800) {
                    /* ==== bgr 565 bmp -> pal 1 dib ==== */
                    for (h=0; h<lines; h++) {
                        BYTE dstval;
                        srcpixel=srcbits;
                        dstbyte=dstbits;
                        dstval=0;
                        for (x=0; x<dstwidth; x++) {
                            WORD srcval;
                            srcval=*srcpixel++;
                            dstval|=(X11DRV_DIB_GetNearestIndex
                                     (colors, 2,
                                      ((srcval <<  3) & 0xf8) | /* r */
                                      ((srcval >>  2) & 0x07),
                                      ((srcval >>  3) & 0xfc) | /* g */
                                      ((srcval >>  9) & 0x03),
                                      ((srcval >>  8) & 0xf8) | /* b */
                                      ((srcval >> 13) & 0x07) ) << (7-(x&7)) );
                            if ((x&7)==7) {
                                *dstbyte++=dstval;
                                dstval=0;
                            }
                        }
                        if ((dstwidth&7)!=0) {
                            *dstbyte=dstval;
                        }
                        srcbits -= bmpImage->bytes_per_line;
                        dstbits += linebytes;
                    }
                } else {
                    goto notsupported;
                }
            } else {
                goto notsupported;
            }
        }
        break;

    case 24:
    case 32:
        {
            const void* srcbits;
            const BYTE *srcbyte;
            BYTE* dstbyte;
            int bytes_per_pixel;

            srcbits=bmpImage->data+(lines-1)*bmpImage->bytes_per_line;
            bytes_per_pixel=(bmpImage->bits_per_pixel==24?3:4);

            if (bmpImage->green_mask!=0x00ff00 ||
                (bmpImage->red_mask|bmpImage->blue_mask)!=0xff00ff) {
                goto notsupported;
            } else if (bmpImage->blue_mask==0xff) {
                /* ==== rgb 888 or 0888 bmp -> pal 1 dib ==== */
                for (h=0; h<lines; h++) {
                    BYTE dstval;
                    srcbyte=srcbits;
                    dstbyte=dstbits;
                    dstval=0;
                    for (x=0; x<dstwidth; x++) {
                        dstval|=(X11DRV_DIB_GetNearestIndex
                                 (colors, 2,
                                  srcbyte[2],
                                  srcbyte[1],
                                  srcbyte[0]) << (7-(x&7)) );
                        srcbyte+=bytes_per_pixel;
                        if ((x&7)==7) {
                            *dstbyte++=dstval;
                            dstval=0;
                        }
                    }
                    if ((dstwidth&7)!=0) {
                        *dstbyte=dstval;
                    }
                    srcbits -= bmpImage->bytes_per_line;
                    dstbits += linebytes;
                }
            } else {
                /* ==== bgr 888 or 0888 bmp -> pal 1 dib ==== */
                for (h=0; h<lines; h++) {
                    BYTE dstval;
                    srcbyte=srcbits;
                    dstbyte=dstbits;
                    dstval=0;
                    for (x=0; x<dstwidth; x++) {
                        dstval|=(X11DRV_DIB_GetNearestIndex
                                 (colors, 2,
                                  srcbyte[0],
                                  srcbyte[1],
                                  srcbyte[2]) << (7-(x&7)) );
                        srcbyte+=bytes_per_pixel;
                        if ((x&7)==7) {
                            *dstbyte++=dstval;
                            dstval=0;
                        }
                    }
                    if ((dstwidth&7)!=0) {
                        *dstbyte=dstval;
                    }
                    srcbits -= bmpImage->bytes_per_line;
                    dstbits += linebytes;
                }
            }
        }
        break;

    default:
    notsupported:
        {
            BYTE* dstbyte;
            unsigned long white = (1 << bmpImage->bits_per_pixel) - 1;

            /* ==== any bmp format -> pal 1 dib ==== */
            WARN("from unknown %d bit bitmap (%lx,%lx,%lx) to 1 bit DIB\n",
                  bmpImage->bits_per_pixel, bmpImage->red_mask,
                  bmpImage->green_mask, bmpImage->blue_mask );

            for (h=lines-1; h>=0; h--) {
                BYTE dstval;
                dstbyte=dstbits;
                dstval=0;
                for (x=0; x<dstwidth; x++) {
                    dstval|=(XGetPixel( bmpImage, x, h) >= white) << (7 - (x&7));
                    if ((x&7)==7) {
                        *dstbyte++=dstval;
                        dstval=0;
                    }
                }
                if ((dstwidth&7)!=0) {
                    *dstbyte=dstval;
                }
                dstbits += linebytes;
            }
        }
        break;
    }
}

/***********************************************************************
 *           X11DRV_DIB_SetImageBits_4
 *
 * SetDIBits for a 4-bit deep DIB.
 */
static void X11DRV_DIB_SetImageBits_4( int lines, const BYTE *srcbits,
                                DWORD srcwidth, DWORD dstwidth, int left,
                                int *colors, XImage *bmpImage, DWORD linebytes)
{
    int h;
    const BYTE* srcbyte;
    DWORD i, x;

    if (lines < 0 ) {
        lines = -lines;
        srcbits = srcbits + linebytes * (lines - 1);
        linebytes = -linebytes;
    }

    if (left & 1) {
        left--;
        dstwidth++;
    }
    srcbits += left >> 1;

    /* ==== pal 4 dib -> any bmp format ==== */
    for (h = lines-1; h >= 0; h--) {
        srcbyte=srcbits;
        for (i = dstwidth/2, x = left; i > 0; i--) {
            BYTE srcval=*srcbyte++;
            XPutPixel( bmpImage, x++, h, colors[srcval >> 4] );
            XPutPixel( bmpImage, x++, h, colors[srcval & 0x0f] );
        }
        if (dstwidth & 1)
            XPutPixel( bmpImage, x, h, colors[*srcbyte >> 4] );
        srcbits += linebytes;
    }
}



/***********************************************************************
 *           X11DRV_DIB_GetImageBits_4
 *
 * GetDIBits for a 4-bit deep DIB.
 */
static void X11DRV_DIB_GetImageBits_4( int lines, BYTE *dstbits,
				       DWORD srcwidth, DWORD dstwidth,
				       RGBQUAD *colors, PALETTEENTRY *srccolors, 
				       XImage *bmpImage, DWORD linebytes )
{
    DWORD x;
    int h;
    BYTE *bits;

    if (lines < 0 )
    {
       lines = -lines;
       dstbits = dstbits + ( linebytes * (lines-1) );
       linebytes = -linebytes;
    }

    bits = dstbits;

    switch (bmpImage->depth) {
    case 1:
    case 4:
        if (bmpImage->red_mask==0 && bmpImage->green_mask==0 && bmpImage->blue_mask==0 && srccolors) {
            /* ==== pal 1 or 4 bmp -> pal 4 dib ==== */
            BYTE* dstbyte;

            for (h = lines-1; h >= 0; h--) {
                BYTE dstval;
                dstbyte=dstbits;
                dstval=0;
                for (x = 0; x < dstwidth; x++) {
                    PALETTEENTRY srcval;
                    srcval=srccolors[XGetPixel(bmpImage, x, h)];
                    dstval|=(X11DRV_DIB_GetNearestIndex
                             (colors, 16,
                              srcval.peRed,
                              srcval.peGreen,
                              srcval.peBlue) << (4-((x&1)<<2)));
                    if ((x&1)==1) {
                        *dstbyte++=dstval;
                        dstval=0;
                    }
                }
                if ((dstwidth&1)!=0) {
                    *dstbyte=dstval;
                }
                dstbits += linebytes;
            }
        } else {
            goto notsupported;
        }
        break;

    case 8:
        if (bmpImage->red_mask==0 && bmpImage->green_mask==0 && bmpImage->blue_mask==0 && srccolors) {
            /* ==== pal 8 bmp -> pal 4 dib ==== */
            const void* srcbits;
            const BYTE *srcpixel;
            BYTE* dstbyte;

            srcbits=bmpImage->data+(lines-1)*bmpImage->bytes_per_line;
            for (h=0; h<lines; h++) {
                BYTE dstval;
                srcpixel=srcbits;
                dstbyte=dstbits;
                dstval=0;
                for (x=0; x<dstwidth; x++) {
                    PALETTEENTRY srcval;
                    srcval = srccolors[(int)*srcpixel++];
                    dstval|=(X11DRV_DIB_GetNearestIndex
                             (colors, 16,
                              srcval.peRed,
                              srcval.peGreen,
                              srcval.peBlue) << (4*(1-(x&1))) );
                    if ((x&1)==1) {
                        *dstbyte++=dstval;
                        dstval=0;
                    }
                }
                if ((dstwidth&1)!=0) {
                    *dstbyte=dstval;
                }
                srcbits -= bmpImage->bytes_per_line;
                dstbits += linebytes;
            }
        } else {
            goto notsupported;
        }
        break;

    case 15:
    case 16:
        {
            const void* srcbits;
            const WORD* srcpixel;
            BYTE* dstbyte;

            srcbits=bmpImage->data+(lines-1)*bmpImage->bytes_per_line;

            if (bmpImage->green_mask==0x03e0) {
                if (bmpImage->red_mask==0x7c00) {
                    /* ==== rgb 555 bmp -> pal 4 dib ==== */
                    for (h=0; h<lines; h++) {
                        BYTE dstval;
                        srcpixel=srcbits;
                        dstbyte=dstbits;
                        dstval=0;
                        for (x=0; x<dstwidth; x++) {
                            WORD srcval;
                            srcval=*srcpixel++;
                            dstval|=(X11DRV_DIB_GetNearestIndex
                                     (colors, 16,
                                      ((srcval >>  7) & 0xf8) | /* r */
                                      ((srcval >> 12) & 0x07),
                                      ((srcval >>  2) & 0xf8) | /* g */
                                      ((srcval >>  7) & 0x07),
                                      ((srcval <<  3) & 0xf8) | /* b */
                                      ((srcval >>  2) & 0x07) ) << ((1-(x&1))<<2) );
                            if ((x&1)==1) {
                                *dstbyte++=dstval;
                                dstval=0;
                            }
                        }
                        if ((dstwidth&1)!=0) {
                            *dstbyte=dstval;
                        }
                        srcbits -= bmpImage->bytes_per_line;
                        dstbits += linebytes;
                    }
                } else if (bmpImage->blue_mask==0x7c00) {
                    /* ==== bgr 555 bmp -> pal 4 dib ==== */
                    for (h=0; h<lines; h++) {
                        WORD dstval;
                        srcpixel=srcbits;
                        dstbyte=dstbits;
                        dstval=0;
                        for (x=0; x<dstwidth; x++) {
                            WORD srcval;
                            srcval=*srcpixel++;
                            dstval|=(X11DRV_DIB_GetNearestIndex
                                     (colors, 16,
                                      ((srcval <<  3) & 0xf8) | /* r */
                                      ((srcval >>  2) & 0x07),
                                      ((srcval >>  2) & 0xf8) | /* g */
                                      ((srcval >>  7) & 0x07),
                                      ((srcval >>  7) & 0xf8) | /* b */
                                      ((srcval >> 12) & 0x07) ) << ((1-(x&1))<<2) );
                            if ((x&1)==1) {
                                *dstbyte++=dstval;
                                dstval=0;
                            }
                        }
                        if ((dstwidth&1)!=0) {
                            *dstbyte=dstval;
                        }
                        srcbits -= bmpImage->bytes_per_line;
                        dstbits += linebytes;
                    }
                } else {
                    goto notsupported;
                }
            } else if (bmpImage->green_mask==0x07e0) {
                if (bmpImage->red_mask==0xf800) {
                    /* ==== rgb 565 bmp -> pal 4 dib ==== */
                    for (h=0; h<lines; h++) {
                        BYTE dstval;
                        srcpixel=srcbits;
                        dstbyte=dstbits;
                        dstval=0;
                        for (x=0; x<dstwidth; x++) {
                            WORD srcval;
                            srcval=*srcpixel++;
                            dstval|=(X11DRV_DIB_GetNearestIndex
                                     (colors, 16,
                                      ((srcval >>  8) & 0xf8) | /* r */
                                      ((srcval >> 13) & 0x07),
                                      ((srcval >>  3) & 0xfc) | /* g */
                                      ((srcval >>  9) & 0x03),
                                      ((srcval <<  3) & 0xf8) | /* b */
                                      ((srcval >>  2) & 0x07) ) << ((1-(x&1))<<2) );
                            if ((x&1)==1) {
                                *dstbyte++=dstval;
                                dstval=0;
                            }
                        }
                        if ((dstwidth&1)!=0) {
                            *dstbyte=dstval;
                        }
                        srcbits -= bmpImage->bytes_per_line;
                        dstbits += linebytes;
                    }
                } else if (bmpImage->blue_mask==0xf800) {
                    /* ==== bgr 565 bmp -> pal 4 dib ==== */
                    for (h=0; h<lines; h++) {
                        WORD dstval;
                        srcpixel=srcbits;
                        dstbyte=dstbits;
                        dstval=0;
                        for (x=0; x<dstwidth; x++) {
                            WORD srcval;
                            srcval=*srcpixel++;
                            dstval|=(X11DRV_DIB_GetNearestIndex
                                     (colors, 16,
                                      ((srcval <<  3) & 0xf8) | /* r */
                                      ((srcval >>  2) & 0x07),
                                      ((srcval >>  3) & 0xfc) | /* g */
                                      ((srcval >>  9) & 0x03),
                                      ((srcval >>  8) & 0xf8) | /* b */
                                      ((srcval >> 13) & 0x07) ) << ((1-(x&1))<<2) );
                            if ((x&1)==1) {
                                *dstbyte++=dstval;
                                dstval=0;
                            }
                        }
                        if ((dstwidth&1)!=0) {
                            *dstbyte=dstval;
                        }
                        srcbits -= bmpImage->bytes_per_line;
                        dstbits += linebytes;
                    }
                } else {
                    goto notsupported;
                }
            } else {
                goto notsupported;
            }
        }
        break;

    case 24:
        if (bmpImage->bits_per_pixel==24) {
            const void* srcbits;
            const BYTE *srcbyte;
            BYTE* dstbyte;

            srcbits=bmpImage->data+(lines-1)*bmpImage->bytes_per_line;

            if (bmpImage->green_mask!=0x00ff00 ||
                (bmpImage->red_mask|bmpImage->blue_mask)!=0xff00ff) {
                goto notsupported;
            } else if (bmpImage->blue_mask==0xff) {
                /* ==== rgb 888 bmp -> pal 4 dib ==== */
                for (h=0; h<lines; h++) {
                    srcbyte=srcbits;
                    dstbyte=dstbits;
                    for (x=0; x<dstwidth/2; x++) {
                        /* Do 2 pixels at a time */
                        *dstbyte++=(X11DRV_DIB_GetNearestIndex
                                    (colors, 16,
                                     srcbyte[2],
                                     srcbyte[1],
                                     srcbyte[0]) << 4) |
                                    X11DRV_DIB_GetNearestIndex
                                    (colors, 16,
                                     srcbyte[5],
                                     srcbyte[4],
                                     srcbyte[3]);
                        srcbyte+=6;
                    }
                    if (dstwidth&1) {
                        /* And the the odd pixel */
                        *dstbyte++=(X11DRV_DIB_GetNearestIndex
                                    (colors, 16,
                                     srcbyte[2],
                                     srcbyte[1],
                                     srcbyte[0]) << 4);
                    }
                    srcbits -= bmpImage->bytes_per_line;
                    dstbits += linebytes;
                }
            } else {
                /* ==== bgr 888 bmp -> pal 4 dib ==== */
                for (h=0; h<lines; h++) {
                    srcbyte=srcbits;
                    dstbyte=dstbits;
                    for (x=0; x<dstwidth/2; x++) {
                        /* Do 2 pixels at a time */
                        *dstbyte++=(X11DRV_DIB_GetNearestIndex
                                    (colors, 16,
                                     srcbyte[0],
                                     srcbyte[1],
                                     srcbyte[2]) << 4) |
                                    X11DRV_DIB_GetNearestIndex
                                    (colors, 16,
                                     srcbyte[3],
                                     srcbyte[4],
                                     srcbyte[5]);
                        srcbyte+=6;
                    }
                    if (dstwidth&1) {
                        /* And the the odd pixel */
                        *dstbyte++=(X11DRV_DIB_GetNearestIndex
                                    (colors, 16,
                                     srcbyte[0],
                                     srcbyte[1],
                                     srcbyte[2]) << 4);
                    }
                    srcbits -= bmpImage->bytes_per_line;
                    dstbits += linebytes;
                }
            }
            break;
        }
        /* Fall through */

    case 32:
        {
            const void* srcbits;
            const BYTE *srcbyte;
            BYTE* dstbyte;

            srcbits=bmpImage->data+(lines-1)*bmpImage->bytes_per_line;

            if (bmpImage->green_mask!=0x00ff00 ||
                (bmpImage->red_mask|bmpImage->blue_mask)!=0xff00ff) {
                goto notsupported;
            } else if (bmpImage->blue_mask==0xff) {
                /* ==== rgb 0888 bmp -> pal 4 dib ==== */
                for (h=0; h<lines; h++) {
                    srcbyte=srcbits;
                    dstbyte=dstbits;
                    for (x=0; x<dstwidth/2; x++) {
                        /* Do 2 pixels at a time */
                        *dstbyte++=(X11DRV_DIB_GetNearestIndex
                                    (colors, 16,
                                     srcbyte[2],
                                     srcbyte[1],
                                     srcbyte[0]) << 4) |
                                    X11DRV_DIB_GetNearestIndex
                                    (colors, 16,
                                     srcbyte[6],
                                     srcbyte[5],
                                     srcbyte[4]);
                        srcbyte+=8;
                    }
                    if (dstwidth&1) {
                        /* And the the odd pixel */
                        *dstbyte++=(X11DRV_DIB_GetNearestIndex
                                    (colors, 16,
                                     srcbyte[2],
                                     srcbyte[1],
                                     srcbyte[0]) << 4);
                    }
                    srcbits -= bmpImage->bytes_per_line;
                    dstbits += linebytes;
                }
            } else {
                /* ==== bgr 0888 bmp -> pal 4 dib ==== */
                for (h=0; h<lines; h++) {
                    srcbyte=srcbits;
                    dstbyte=dstbits;
                    for (x=0; x<dstwidth/2; x++) {
                        /* Do 2 pixels at a time */
                        *dstbyte++=(X11DRV_DIB_GetNearestIndex
                                    (colors, 16,
                                     srcbyte[0],
                                     srcbyte[1],
                                     srcbyte[2]) << 4) |
                                    X11DRV_DIB_GetNearestIndex
                                    (colors, 16,
                                     srcbyte[4],
                                     srcbyte[5],
                                     srcbyte[6]);
                        srcbyte+=8;
                    }
                    if (dstwidth&1) {
                        /* And the the odd pixel */
                        *dstbyte++=(X11DRV_DIB_GetNearestIndex
                                    (colors, 16,
                                     srcbyte[0],
                                     srcbyte[1],
                                     srcbyte[2]) << 4);
                    }
                    srcbits -= bmpImage->bytes_per_line;
                    dstbits += linebytes;
                }
            }
        }
        break;

    default:
    notsupported:
        {
            BYTE* dstbyte;

            /* ==== any bmp format -> pal 4 dib ==== */
            WARN("from unknown %d bit bitmap (%lx,%lx,%lx) to 4 bit DIB\n",
                  bmpImage->bits_per_pixel, bmpImage->red_mask,
                  bmpImage->green_mask, bmpImage->blue_mask );
            for (h=lines-1; h>=0; h--) {
                dstbyte=dstbits;
                for (x=0; x<(dstwidth & ~1); x+=2) {
                    *dstbyte++=(X11DRV_DIB_MapColor((int*)colors, 16, XGetPixel(bmpImage, x, h), 0) << 4) |
                        X11DRV_DIB_MapColor((int*)colors, 16, XGetPixel(bmpImage, x+1, h), 0);
                }
                if (dstwidth & 1) {
                    *dstbyte=(X11DRV_DIB_MapColor((int *)colors, 16, XGetPixel(bmpImage, x, h), 0) << 4);
                }
                dstbits += linebytes;
            }
        }
        break;
    }
}

/***********************************************************************
 *           X11DRV_DIB_SetImageBits_RLE4
 *
 * SetDIBits for a 4-bit deep compressed DIB.
 */
static void X11DRV_DIB_SetImageBits_RLE4( int lines, const BYTE *bits,
					  DWORD width, DWORD dstwidth,
					  int left, int *colors,
					  XImage *bmpImage )
{
    int x = 0, y = lines - 1, c, length;
    const BYTE *begin = bits;

    while (y >= 0)
    {
        length = *bits++;
	if (length) {	/* encoded */
	    c = *bits++;
	    while (length--) {
                if (x >= width) break;
                XPutPixel(bmpImage, x++, y, colors[c >> 4]);
                if (!length--) break;
                if (x >= width) break;
                XPutPixel(bmpImage, x++, y, colors[c & 0xf]);
	    }
	} else {
	    length = *bits++;
	    switch (length)
            {
            case RLE_EOL:
                x = 0;
                y--;
                break;

            case RLE_END:
	        return;

            case RLE_DELTA:
                x += *bits++;
                y -= *bits++;
                break;

	    default: /* absolute */
	        while (length--) {
		    c = *bits++;
                    if (x < width) XPutPixel(bmpImage, x++, y, colors[c >> 4]);
                    if (!length--) break;
                    if (x < width) XPutPixel(bmpImage, x++, y, colors[c & 0xf]);
		}
		if ((bits - begin) & 1)
		    bits++;
	    }
	}
    }
}



/***********************************************************************
 *           X11DRV_DIB_SetImageBits_8
 *
 * SetDIBits for an 8-bit deep DIB.
 */
static void X11DRV_DIB_SetImageBits_8( int lines, const BYTE *srcbits,
				DWORD srcwidth, DWORD dstwidth, int left,
                                const int *colors, XImage *bmpImage,
				DWORD linebytes )
{
    DWORD x;
    int h;
    const BYTE* srcbyte;
    BYTE* dstbits;

    if (lines < 0 )
    {
        lines = -lines;
        srcbits = srcbits + linebytes * (lines-1);
        linebytes = -linebytes;
    }
    srcbits += left;
    srcbyte = srcbits;

    switch (bmpImage->depth) {
    case 15:
    case 16:
#if defined(__i386__) && defined(__GNUC__)
	/* Some X servers might have 32 bit/ 16bit deep pixel */
	if (lines && dstwidth && (bmpImage->bits_per_pixel == 16))
	{
	    dstbits=bmpImage->data+left*2+(lines-1)*bmpImage->bytes_per_line;
	    /* FIXME: Does this really handle all these cases correctly? */
	    /* ==== pal 8 dib -> rgb or bgr 555 or 565 bmp ==== */
	    for (h = lines ; h--; ) {
		int _cl1,_cl2; /* temp outputs for asm below */
		/* Borrowed from DirectDraw */
		__asm__ __volatile__(
		"xor %%eax,%%eax\n"
		"cld\n"
		"1:\n"
		"    lodsb\n"
		"    movw (%%edx,%%eax,4),%%ax\n"
		"    stosw\n"
		"      xor %%eax,%%eax\n"
		"    loop 1b\n"
		:"=S" (srcbyte), "=D" (_cl1), "=c" (_cl2)
		:"S" (srcbyte),
		 "D" (dstbits),
		 "c" (dstwidth),
		 "d" (colors)
		:"eax", "cc", "memory"
		);
		srcbyte = (srcbits += linebytes);
		dstbits -= bmpImage->bytes_per_line;
	    }
	    return;
	}
	break;
#endif
    case 24:
    case 32:
#if defined(__i386__) && defined(__GNUC__)
	if (lines && dstwidth && (bmpImage->bits_per_pixel == 32))
	{
	    dstbits=bmpImage->data+left*4+(lines-1)*bmpImage->bytes_per_line;
	    /* FIXME: Does this really handle both cases correctly? */
	    /* ==== pal 8 dib -> rgb or bgr 0888 bmp ==== */
	    for (h = lines ; h--; ) {
		int _cl1,_cl2; /* temp outputs for asm below */
		/* Borrowed from DirectDraw */
		__asm__ __volatile__(
		"xor %%eax,%%eax\n"
		"cld\n"
		"1:\n"
		"    lodsb\n"
		"    movl (%%edx,%%eax,4),%%eax\n"
		"    stosl\n"
		"      xor %%eax,%%eax\n"
		"    loop 1b\n"
		:"=S" (srcbyte), "=D" (_cl1), "=c" (_cl2)
		:"S" (srcbyte),
		 "D" (dstbits),
		 "c" (dstwidth),
		 "d" (colors)
		:"eax", "cc", "memory"
		);
		srcbyte = (srcbits += linebytes);
		dstbits -= bmpImage->bytes_per_line;
	    }
	    return;
	}
	break;
#endif
    default:
        break; /* use slow generic case below */
    }

    /* ==== pal 8 dib -> any bmp format ==== */
    for (h=lines-1; h>=0; h--) {
        for (x=left; x<dstwidth+left; x++) {
            XPutPixel(bmpImage, x, h, colors[*srcbyte++]);
        }
        srcbyte = (srcbits += linebytes);
    }
}

/***********************************************************************
 *           X11DRV_DIB_GetImageBits_8
 *
 * GetDIBits for an 8-bit deep DIB.
 */
static void X11DRV_DIB_GetImageBits_8( int lines, BYTE *dstbits,
				       DWORD srcwidth, DWORD dstwidth,
				       RGBQUAD *colors, PALETTEENTRY *srccolors, 
				       XImage *bmpImage, DWORD linebytes )
{
    DWORD x;
    int h;
    BYTE* dstbyte;

    if (lines < 0 )
    {
       lines = -lines;
       dstbits = dstbits + ( linebytes * (lines-1) );
       linebytes = -linebytes;
    }

    /* 
     * Hack for now 
     * This condition is true when GetImageBits has been called by 
     * UpdateDIBSection. For now, GetNearestIndex is too slow to support 
     * 256 colormaps, so we'll just use for for GetDIBits calls. 
     * (In somes cases, in a updateDIBSection, the returned colors are bad too)
     */
    if (!srccolors) goto updatesection;

    switch (bmpImage->depth) {
    case 1:
    case 4:
        if (bmpImage->red_mask==0 && bmpImage->green_mask==0 && bmpImage->blue_mask==0 && srccolors) {

            /* ==== pal 1 bmp -> pal 8 dib ==== */
            /* ==== pal 4 bmp -> pal 8 dib ==== */
            for (h=lines-1; h>=0; h--) {
                dstbyte=dstbits;
                for (x=0; x<dstwidth; x++) {
                    PALETTEENTRY srcval;
                    srcval=srccolors[XGetPixel(bmpImage, x, h)];
                    *dstbyte++=X11DRV_DIB_GetNearestIndex(colors, 256,
                                                          srcval.peRed,
                                                          srcval.peGreen,
                                                          srcval.peBlue);
                }
                dstbits += linebytes;
            }
        } else {
            goto notsupported;
        }
        break;

    case 8:
       if (bmpImage->red_mask==0 && bmpImage->green_mask==0 && bmpImage->blue_mask==0 && srccolors) {
            /* ==== pal 8 bmp -> pal 8 dib ==== */
           const void* srcbits;
           const BYTE* srcpixel;

           srcbits=bmpImage->data+(lines-1)*bmpImage->bytes_per_line;
           for (h=0; h<lines; h++) {
               srcpixel=srcbits;
               dstbyte=dstbits;
               for (x = 0; x < dstwidth; x++) {
                   PALETTEENTRY srcval;
                   srcval=srccolors[(int)*srcpixel++];
                   *dstbyte++=X11DRV_DIB_GetNearestIndex(colors, 256,
                                                         srcval.peRed,
                                                         srcval.peGreen,
                                                         srcval.peBlue);
               }
               srcbits -= bmpImage->bytes_per_line;
               dstbits += linebytes;
           }
       } else {
           goto notsupported;
       }
       break;

    case 15:
    case 16:
        {
            const void* srcbits;
            const WORD* srcpixel;
            BYTE* dstbyte;

            srcbits=bmpImage->data+(lines-1)*bmpImage->bytes_per_line;

            if (bmpImage->green_mask==0x03e0) {
                if (bmpImage->red_mask==0x7c00) {
                    /* ==== rgb 555 bmp -> pal 8 dib ==== */
                    for (h=0; h<lines; h++) {
                        srcpixel=srcbits;
                        dstbyte=dstbits;
                        for (x=0; x<dstwidth; x++) {
                            WORD srcval;
                            srcval=*srcpixel++;
                            *dstbyte++=X11DRV_DIB_GetNearestIndex
                                (colors, 256,
                                 ((srcval >>  7) & 0xf8) | /* r */
                                 ((srcval >> 12) & 0x07),
                                 ((srcval >>  2) & 0xf8) | /* g */
                                 ((srcval >>  7) & 0x07),
                                 ((srcval <<  3) & 0xf8) | /* b */
                                 ((srcval >>  2) & 0x07) );
                        }
                        srcbits -= bmpImage->bytes_per_line;
                        dstbits += linebytes;
                    }
                } else if (bmpImage->blue_mask==0x7c00) {
                    /* ==== bgr 555 bmp -> pal 8 dib ==== */
                    for (h=0; h<lines; h++) {
                        srcpixel=srcbits;
                        dstbyte=dstbits;
                        for (x=0; x<dstwidth; x++) {
                            WORD srcval;
                            srcval=*srcpixel++;
                            *dstbyte++=X11DRV_DIB_GetNearestIndex
                                (colors, 256,
                                 ((srcval <<  3) & 0xf8) | /* r */
                                 ((srcval >>  2) & 0x07),
                                 ((srcval >>  2) & 0xf8) | /* g */
                                 ((srcval >>  7) & 0x07),
                                 ((srcval >>  7) & 0xf8) | /* b */
                                 ((srcval >> 12) & 0x07) );
                        }
                        srcbits -= bmpImage->bytes_per_line;
                        dstbits += linebytes;
                    }
                } else {
                    goto notsupported;
                }
            } else if (bmpImage->green_mask==0x07e0) {
                if (bmpImage->red_mask==0xf800) {
                    /* ==== rgb 565 bmp -> pal 8 dib ==== */
                    for (h=0; h<lines; h++) {
                        srcpixel=srcbits;
                        dstbyte=dstbits;
                        for (x=0; x<dstwidth; x++) {
                            WORD srcval;
                            srcval=*srcpixel++;
                            *dstbyte++=X11DRV_DIB_GetNearestIndex
                                (colors, 256,
                                 ((srcval >>  8) & 0xf8) | /* r */
                                 ((srcval >> 13) & 0x07),
                                 ((srcval >>  3) & 0xfc) | /* g */
                                 ((srcval >>  9) & 0x03),
                                 ((srcval <<  3) & 0xf8) | /* b */
                                 ((srcval >>  2) & 0x07) );
                        }
                        srcbits -= bmpImage->bytes_per_line;
                        dstbits += linebytes;
                    }
                } else if (bmpImage->blue_mask==0xf800) {
                    /* ==== bgr 565 bmp -> pal 8 dib ==== */
                    for (h=0; h<lines; h++) {
                        srcpixel=srcbits;
                        dstbyte=dstbits;
                        for (x=0; x<dstwidth; x++) {
                            WORD srcval;
                            srcval=*srcpixel++;
                            *dstbyte++=X11DRV_DIB_GetNearestIndex
                                (colors, 256,
                                 ((srcval <<  3) & 0xf8) | /* r */
                                 ((srcval >>  2) & 0x07),
                                 ((srcval >>  3) & 0xfc) | /* g */
                                 ((srcval >>  9) & 0x03),
                                 ((srcval >>  8) & 0xf8) | /* b */
                                 ((srcval >> 13) & 0x07) );
                        }
                        srcbits -= bmpImage->bytes_per_line;
                        dstbits += linebytes;
                    }
                } else {
                    goto notsupported;
                }
            } else {
                goto notsupported;
            }
        }
        break;

    case 24:
    case 32:
        {
            const void* srcbits;
            const BYTE *srcbyte;
            BYTE* dstbyte;
            int bytes_per_pixel;

            srcbits=bmpImage->data+(lines-1)*bmpImage->bytes_per_line;
            bytes_per_pixel=(bmpImage->bits_per_pixel==24?3:4);

            if (bmpImage->green_mask!=0x00ff00 ||
                (bmpImage->red_mask|bmpImage->blue_mask)!=0xff00ff) {
                goto notsupported;
            } else if (bmpImage->blue_mask==0xff) {
                /* ==== rgb 888 or 0888 bmp -> pal 8 dib ==== */
                for (h=0; h<lines; h++) {
                    srcbyte=srcbits;
                    dstbyte=dstbits;
                    for (x=0; x<dstwidth; x++) {
                        *dstbyte++=X11DRV_DIB_GetNearestIndex
                            (colors, 256,
                             srcbyte[2],
                             srcbyte[1],
                             srcbyte[0]);
                        srcbyte+=bytes_per_pixel;
                    }
                    srcbits -= bmpImage->bytes_per_line;
                    dstbits += linebytes;
                }
            } else {
                /* ==== bgr 888 or 0888 bmp -> pal 8 dib ==== */
                for (h=0; h<lines; h++) {
                    srcbyte=srcbits;
                    dstbyte=dstbits;
                    for (x=0; x<dstwidth; x++) {
                        *dstbyte++=X11DRV_DIB_GetNearestIndex
                            (colors, 256,
                             srcbyte[0],
                             srcbyte[1],
                             srcbyte[2]);
                        srcbyte+=bytes_per_pixel;
                    }
                    srcbits -= bmpImage->bytes_per_line;
                    dstbits += linebytes;
                }
            }
        }
        break;

    default:
    notsupported:
        WARN("from unknown %d bit bitmap (%lx,%lx,%lx) to 8 bit DIB\n",
              bmpImage->depth, bmpImage->red_mask,
              bmpImage->green_mask, bmpImage->blue_mask );
    updatesection:
        /* ==== any bmp format -> pal 8 dib ==== */
        for (h=lines-1; h>=0; h--) {
            dstbyte=dstbits;
            for (x=0; x<dstwidth; x++) {
                *dstbyte=X11DRV_DIB_MapColor
                    ((int*)colors, 256,
                     XGetPixel(bmpImage, x, h), *dstbyte);
                dstbyte++;
            }
            dstbits += linebytes;
        }
        break;
    }
}

/***********************************************************************
 *	      X11DRV_DIB_SetImageBits_RLE8
 *
 * SetDIBits for an 8-bit deep compressed DIB.
 *
 * This function rewritten 941113 by James Youngman.  WINE blew out when I
 * first ran it because my desktop wallpaper is a (large) RLE8 bitmap.  
 *
 * This was because the algorithm assumed that all RLE8 bitmaps end with the  
 * 'End of bitmap' escape code.  This code is very much laxer in what it
 * allows to end the expansion.  Possibly too lax.  See the note by 
 * case RleDelta.  BTW, MS's documentation implies that a correct RLE8
 * bitmap should end with RleEnd, but on the other hand, software exists 
 * that produces ones that don't and Windows 3.1 doesn't complain a bit
 * about it.
 *
 * (No) apologies for my English spelling.  [Emacs users: c-indent-level=4].
 *			James A. Youngman <mbcstjy@afs.man.ac.uk>
 *						[JAY]
 */
static void X11DRV_DIB_SetImageBits_RLE8( int lines, const BYTE *bits,
					  DWORD width, DWORD dstwidth,
					  int left, int *colors,
					  XImage *bmpImage )
{
    int x;			/* X-positon on each line.  Increases. */
    int y;			/* Line #.  Starts at lines-1, decreases */
    const BYTE *pIn = bits;     /* Pointer to current position in bits */
    BYTE length;		/* The length pf a run */
    BYTE escape_code;		/* See enum Rle8_EscapeCodes.*/

    /*
     * Note that the bitmap data is stored by Windows starting at the
     * bottom line of the bitmap and going upwards.  Within each line,
     * the data is stored left-to-right.  That's the reason why line
     * goes from lines-1 to 0.			[JAY]
     */

    x = 0;
    y = lines - 1;
    while (y >= 0)
    {
        length = *pIn++;

        /* 
         * If the length byte is not zero (which is the escape value),
         * We have a run of length pixels all the same colour.  The colour 
         * index is stored next. 
         *
         * If the length byte is zero, we need to read the next byte to
         * know what to do.			[JAY]
         */
        if (length != 0)
        {
            /* 
             * [Run-Length] Encoded mode 
             */
            int color = colors[*pIn++];
            while (length-- && x < dstwidth) XPutPixel(bmpImage, x++, y, color);
        }
        else
        {
            /* 
             * Escape codes (may be an absolute sequence though)
             */
            escape_code = (*pIn++);
            switch(escape_code)
            {
            case RLE_EOL:
                x = 0;
                y--;
                break;

            case RLE_END:
                /* Not all RLE8 bitmaps end with this code.  For
                 * example, Paint Shop Pro produces some that don't.
                 * That's (I think) what caused the previous
                 * implementation to fail.  [JAY]
                 */
                return;

            case RLE_DELTA:
                x += (*pIn++);
                y -= (*pIn++);
                break;

            default:  /* switch to absolute mode */
                length = escape_code;
                while (length--)
                {
                    int color = colors[*pIn++];
                    if (x >= dstwidth)
                    {
                        pIn += length;
                        break;
                    }
                    XPutPixel(bmpImage, x++, y, color);
                }
                /*
                 * If you think for a moment you'll realise that the
                 * only time we could ever possibly read an odd
                 * number of bytes is when there is a 0x00 (escape),
                 * a value >0x02 (absolute mode) and then an odd-
                 * length run.  Therefore this is the only place we
                 * need to worry about it.  Everywhere else the
                 * bytes are always read in pairs.  [JAY]
                 */
                if (escape_code & 1) pIn++; /* Throw away the pad byte. */
                break;
            } /* switch (escape_code) : Escape sequence */
        }
    }
}


/***********************************************************************
 *           X11DRV_DIB_SetImageBits_16
 *
 * SetDIBits for a 16-bit deep DIB.
 */
static void X11DRV_DIB_SetImageBits_16( int lines, const BYTE *srcbits,
                                 DWORD srcwidth, DWORD dstwidth, int left,
                                       DC *dc, DWORD rSrc, DWORD gSrc, DWORD bSrc,
                                       XImage *bmpImage, DWORD linebytes )
{
    DWORD x;
    int h;

    if (lines < 0 )
    {
        lines = -lines;
        srcbits = srcbits + ( linebytes * (lines-1));
        linebytes = -linebytes;
    }

    switch (bmpImage->depth)
    {
    case 15:
    case 16:
        {
            char* dstbits;

            srcbits=srcbits+left*2;
            dstbits=bmpImage->data+left*2+(lines-1)*bmpImage->bytes_per_line;

            if (bmpImage->green_mask==0x03e0) {
                if (gSrc==bmpImage->green_mask) {
                    if (rSrc==bmpImage->red_mask) {
                        /* ==== rgb 555 dib -> rgb 555 bmp ==== */
                        /* ==== bgr 555 dib -> bgr 555 bmp ==== */
                        X11DRV_DIB_Convert_any_asis
                            (dstwidth,lines,2,
                             srcbits,linebytes,
                             dstbits,-bmpImage->bytes_per_line);
                    } else if (rSrc==bmpImage->blue_mask) {
                        /* ==== rgb 555 dib -> bgr 555 bmp ==== */
                        /* ==== bgr 555 dib -> rgb 555 bmp ==== */
                        X11DRV_DIB_Convert_555_reverse
                            (dstwidth,lines,
                             srcbits,linebytes,
                             dstbits,-bmpImage->bytes_per_line);
                    }
                } else {
                    if (rSrc==bmpImage->red_mask || bSrc==bmpImage->blue_mask) {
                        /* ==== rgb 565 dib -> rgb 555 bmp ==== */
                        /* ==== bgr 565 dib -> bgr 555 bmp ==== */
                        X11DRV_DIB_Convert_565_to_555_asis
                            (dstwidth,lines,
                             srcbits,linebytes,
                             dstbits,-bmpImage->bytes_per_line);
                    } else {
                        /* ==== rgb 565 dib -> bgr 555 bmp ==== */
                        /* ==== bgr 565 dib -> rgb 555 bmp ==== */
                        X11DRV_DIB_Convert_565_to_555_reverse
                            (dstwidth,lines,
                             srcbits,linebytes,
                             dstbits,-bmpImage->bytes_per_line);
                    }
                }
            } else if (bmpImage->green_mask==0x07e0) {
                if (gSrc==bmpImage->green_mask) {
                    if (rSrc==bmpImage->red_mask) {
                        /* ==== rgb 565 dib -> rgb 565 bmp ==== */
                        /* ==== bgr 565 dib -> bgr 565 bmp ==== */
                        X11DRV_DIB_Convert_any_asis
                            (dstwidth,lines,2,
                             srcbits,linebytes,
                             dstbits,-bmpImage->bytes_per_line);
                    } else {
                        /* ==== rgb 565 dib -> bgr 565 bmp ==== */
                        /* ==== bgr 565 dib -> rgb 565 bmp ==== */
                        X11DRV_DIB_Convert_565_reverse
                            (dstwidth,lines,
                             srcbits,linebytes,
                             dstbits,-bmpImage->bytes_per_line);
                    }
                } else {
                    if (rSrc==bmpImage->red_mask || bSrc==bmpImage->blue_mask) {
                        /* ==== rgb 555 dib -> rgb 565 bmp ==== */
                        /* ==== bgr 555 dib -> bgr 565 bmp ==== */
                        X11DRV_DIB_Convert_555_to_565_asis
                            (dstwidth,lines,
                             srcbits,linebytes,
                             dstbits,-bmpImage->bytes_per_line);
                    } else {
                        /* ==== rgb 555 dib -> bgr 565 bmp ==== */
                        /* ==== bgr 555 dib -> rgb 565 bmp ==== */
                        X11DRV_DIB_Convert_555_to_565_reverse
                            (dstwidth,lines,
                             srcbits,linebytes,
                             dstbits,-bmpImage->bytes_per_line);
                    }
                }
            } else {
                goto notsupported;
            }
        }
        break;

    case 24:
        if (bmpImage->bits_per_pixel==24) {
            char* dstbits;

            srcbits=srcbits+left*2;
            dstbits=bmpImage->data+left*3+(lines-1)*bmpImage->bytes_per_line;

            if (bmpImage->green_mask!=0x00ff00 ||
                (bmpImage->red_mask|bmpImage->blue_mask)!=0xff00ff) {
                goto notsupported;
            } else if ((rSrc==0x1f && bmpImage->red_mask==0xff) ||
                       (bSrc==0x1f && bmpImage->blue_mask==0xff)) {
                if (gSrc==0x03e0) {
                    /* ==== rgb 555 dib -> rgb 888 bmp ==== */
                    /* ==== bgr 555 dib -> bgr 888 bmp ==== */
                    X11DRV_DIB_Convert_555_to_888_asis
                        (dstwidth,lines,
                         srcbits,linebytes,
                         dstbits,-bmpImage->bytes_per_line);
                } else {
                    /* ==== rgb 565 dib -> rgb 888 bmp ==== */
                    /* ==== bgr 565 dib -> bgr 888 bmp ==== */
                    X11DRV_DIB_Convert_565_to_888_asis
                        (dstwidth,lines,
                         srcbits,linebytes,
                         dstbits,-bmpImage->bytes_per_line);
                }
            } else {
                if (gSrc==0x03e0) {
                    /* ==== rgb 555 dib -> bgr 888 bmp ==== */
                    /* ==== bgr 555 dib -> rgb 888 bmp ==== */
                    X11DRV_DIB_Convert_555_to_888_reverse
                        (dstwidth,lines,
                         srcbits,linebytes,
                         dstbits,-bmpImage->bytes_per_line);
                } else {
                    /* ==== rgb 565 dib -> bgr 888 bmp ==== */
                    /* ==== bgr 565 dib -> rgb 888 bmp ==== */
                    X11DRV_DIB_Convert_565_to_888_reverse
                        (dstwidth,lines,
                         srcbits,linebytes,
                         dstbits,-bmpImage->bytes_per_line);
                }
            }
            break;
        }
        /* Fall through */

    case 32:
        {
            char* dstbits;

            srcbits=srcbits+left*2;
            dstbits=bmpImage->data+left*4+(lines-1)*bmpImage->bytes_per_line;

            if (bmpImage->green_mask!=0x00ff00 ||
                (bmpImage->red_mask|bmpImage->blue_mask)!=0xff00ff) {
                goto notsupported;
            } else if ((rSrc==0x1f && bmpImage->red_mask==0xff) ||
                       (bSrc==0x1f && bmpImage->blue_mask==0xff)) {
                if (gSrc==0x03e0) {
                    /* ==== rgb 555 dib -> rgb 0888 bmp ==== */
                    /* ==== bgr 555 dib -> bgr 0888 bmp ==== */
                    X11DRV_DIB_Convert_555_to_0888_asis
                        (dstwidth,lines,
                         srcbits,linebytes,
                         dstbits,-bmpImage->bytes_per_line);
                } else {
                    /* ==== rgb 565 dib -> rgb 0888 bmp ==== */
                    /* ==== bgr 565 dib -> bgr 0888 bmp ==== */
                    X11DRV_DIB_Convert_565_to_0888_asis
                        (dstwidth,lines,
                         srcbits,linebytes,
                         dstbits,-bmpImage->bytes_per_line);
                }
            } else {
                if (gSrc==0x03e0) {
                    /* ==== rgb 555 dib -> bgr 0888 bmp ==== */
                    /* ==== bgr 555 dib -> rgb 0888 bmp ==== */
                    X11DRV_DIB_Convert_555_to_0888_reverse
                        (dstwidth,lines,
                         srcbits,linebytes,
                         dstbits,-bmpImage->bytes_per_line);
                } else {
                    /* ==== rgb 565 dib -> bgr 0888 bmp ==== */
                    /* ==== bgr 565 dib -> rgb 0888 bmp ==== */
                    X11DRV_DIB_Convert_565_to_0888_reverse
                        (dstwidth,lines,
                         srcbits,linebytes,
                         dstbits,-bmpImage->bytes_per_line);
                }
            }
        }
        break;

    default:
    notsupported:
        WARN("from 16 bit DIB (%lx,%lx,%lx) to unknown %d bit bitmap (%lx,%lx,%lx)\n",
              rSrc, gSrc, bSrc, bmpImage->bits_per_pixel, bmpImage->red_mask,
              bmpImage->green_mask, bmpImage->blue_mask );
        /* fall through */
    case 1:
    case 4:
    case 8:
        {
            /* ==== rgb or bgr 555 or 565 dib -> pal 1, 4 or 8 ==== */
            const WORD* srcpixel;
            int rShift1,gShift1,bShift1;
            int rShift2,gShift2,bShift2;
            BYTE gMask1,gMask2;

            /* Set color scaling values */
            rShift1=16+X11DRV_DIB_MaskToShift(rSrc)-3;
            gShift1=16+X11DRV_DIB_MaskToShift(gSrc)-3;
            bShift1=16+X11DRV_DIB_MaskToShift(bSrc)-3;
            rShift2=rShift1+5;
            gShift2=gShift1+5;
            bShift2=bShift1+5;
            if (gSrc==0x03e0) {
                /* Green has 5 bits, like the others */
                gMask1=0xf8;
                gMask2=0x07;
            } else {
                /* Green has 6 bits, not 5. Compensate. */
                gShift1++;
                gShift2+=2;
                gMask1=0xfc;
                gMask2=0x03;
            }

            srcbits+=2*left;

            /* We could split it into four separate cases to optimize 
             * but it is probably not worth it.
             */
            for (h=lines-1; h>=0; h--) {
                srcpixel=(const WORD*)srcbits;
                for (x=left; x<dstwidth+left; x++) {
                    DWORD srcval;
                    BYTE red,green,blue;
                    srcval=*srcpixel++ << 16;
                    red=  ((srcval >> rShift1) & 0xf8) |
                        ((srcval >> rShift2) & 0x07);
                    green=((srcval >> gShift1) & gMask1) |
                        ((srcval >> gShift2) & gMask2);
                    blue= ((srcval >> bShift1) & 0xf8) |
                        ((srcval >> bShift2) & 0x07);
                    XPutPixel(bmpImage, x, h,
                              X11DRV_PALETTE_ToPhysical
                              (dc, RGB(red,green,blue)));
                }
                srcbits += linebytes;
            }
        }
        break;
    }
}


/***********************************************************************
 *           X11DRV_DIB_GetImageBits_16
 *
 * GetDIBits for an 16-bit deep DIB.
 */
static void X11DRV_DIB_GetImageBits_16( int lines, BYTE *dstbits,
					DWORD dstwidth, DWORD srcwidth,
					PALETTEENTRY *srccolors,
					DWORD rDst, DWORD gDst, DWORD bDst,
					XImage *bmpImage, DWORD dibpitch )
{
    DWORD x;
    int h;

    DWORD linebytes = dibpitch;

    if (lines < 0 )
    {
        lines = -lines;
        dstbits = dstbits + ( linebytes * (lines-1));
        linebytes = -linebytes;
    }

    switch (bmpImage->depth)
    {
    case 15:
    case 16:
        {
            const char* srcbits;

            srcbits=bmpImage->data+(lines-1)*bmpImage->bytes_per_line;

            if (bmpImage->green_mask==0x03e0) {
                if (gDst==bmpImage->green_mask) {
                    if (rDst==bmpImage->red_mask) {
                        /* ==== rgb 555 bmp -> rgb 555 dib ==== */
                        /* ==== bgr 555 bmp -> bgr 555 dib ==== */
                        X11DRV_DIB_Convert_any_asis
                            (dstwidth,lines,2,
                             srcbits,-bmpImage->bytes_per_line,
                             dstbits,linebytes);
                    } else {
                        /* ==== rgb 555 bmp -> bgr 555 dib ==== */
                        /* ==== bgr 555 bmp -> rgb 555 dib ==== */
                        X11DRV_DIB_Convert_555_reverse
                            (dstwidth,lines,
                             srcbits,-bmpImage->bytes_per_line,
                             dstbits,linebytes);
                    }
                } else {
                    if (rDst==bmpImage->red_mask || bDst==bmpImage->blue_mask) {
                        /* ==== rgb 555 bmp -> rgb 565 dib ==== */
                        /* ==== bgr 555 bmp -> bgr 565 dib ==== */
                        X11DRV_DIB_Convert_555_to_565_asis
                            (dstwidth,lines,
                             srcbits,-bmpImage->bytes_per_line,
                             dstbits,linebytes);
                    } else {
                        /* ==== rgb 555 bmp -> bgr 565 dib ==== */
                        /* ==== bgr 555 bmp -> rgb 565 dib ==== */
                        X11DRV_DIB_Convert_555_to_565_reverse
                            (dstwidth,lines,
                             srcbits,-bmpImage->bytes_per_line,
                             dstbits,linebytes);
                    }
                }
            } else if (bmpImage->green_mask==0x07e0) {
                if (gDst==bmpImage->green_mask) {
                    if (rDst == bmpImage->red_mask) {
                        /* ==== rgb 565 bmp -> rgb 565 dib ==== */
                        /* ==== bgr 565 bmp -> bgr 565 dib ==== */
                        X11DRV_DIB_Convert_any_asis
                            (dstwidth,lines,2,
                             srcbits,-bmpImage->bytes_per_line,
                             dstbits,linebytes);
                    } else {
                        /* ==== rgb 565 bmp -> bgr 565 dib ==== */
                        /* ==== bgr 565 bmp -> rgb 565 dib ==== */
                        X11DRV_DIB_Convert_565_reverse
                            (dstwidth,lines,
                             srcbits,-bmpImage->bytes_per_line,
                             dstbits,linebytes);
                    }
                } else {
                    if (rDst==bmpImage->red_mask || bDst==bmpImage->blue_mask) {
                        /* ==== rgb 565 bmp -> rgb 555 dib ==== */
                        /* ==== bgr 565 bmp -> bgr 555 dib ==== */
                        X11DRV_DIB_Convert_565_to_555_asis
                            (dstwidth,lines,
                             srcbits,-bmpImage->bytes_per_line,
                             dstbits,linebytes);
                    } else {
                        /* ==== rgb 565 bmp -> bgr 555 dib ==== */
                        /* ==== bgr 565 bmp -> rgb 555 dib ==== */
                        X11DRV_DIB_Convert_565_to_555_reverse
                            (dstwidth,lines,
                             srcbits,-bmpImage->bytes_per_line,
                             dstbits,linebytes);
                    }
                }
            } else {
                goto notsupported;
            }
        }
        break;

    case 24:
        if (bmpImage->bits_per_pixel == 24) {
            const char* srcbits;

            srcbits=bmpImage->data+(lines-1)*bmpImage->bytes_per_line;

            if (bmpImage->green_mask!=0x00ff00 ||
                (bmpImage->red_mask|bmpImage->blue_mask)!=0xff00ff) {
                goto notsupported;
            } else if ((rDst==0x1f && bmpImage->red_mask==0xff) ||
                    (bDst==0x1f && bmpImage->blue_mask==0xff)) {
                if (gDst==0x03e0) {
                    /* ==== rgb 888 bmp -> rgb 555 dib ==== */
                    /* ==== bgr 888 bmp -> bgr 555 dib ==== */
                    X11DRV_DIB_Convert_888_to_555_asis
                        (dstwidth,lines,
                         srcbits,-bmpImage->bytes_per_line,
                         dstbits,linebytes);
                } else {
                    /* ==== rgb 888 bmp -> rgb 565 dib ==== */
                    /* ==== rgb 888 bmp -> rgb 565 dib ==== */
                    X11DRV_DIB_Convert_888_to_565_asis
                        (dstwidth,lines,
                         srcbits,-bmpImage->bytes_per_line,
                         dstbits,linebytes);
                }
            } else {
                if (gDst==0x03e0) {
                    /* ==== rgb 888 bmp -> bgr 555 dib ==== */
                    /* ==== bgr 888 bmp -> rgb 555 dib ==== */
                    X11DRV_DIB_Convert_888_to_555_reverse
                        (dstwidth,lines,
                         srcbits,-bmpImage->bytes_per_line,
                         dstbits,linebytes);
                } else {
                    /* ==== rgb 888 bmp -> bgr 565 dib ==== */
                    /* ==== bgr 888 bmp -> rgb 565 dib ==== */
                    X11DRV_DIB_Convert_888_to_565_reverse
                        (dstwidth,lines,
                         srcbits,-bmpImage->bytes_per_line,
                         dstbits,linebytes);
                }
            }
            break;
        }
        /* Fall through */

    case 32:
        {
            const char* srcbits;

            srcbits=bmpImage->data+(lines-1)*bmpImage->bytes_per_line;

            if (bmpImage->green_mask!=0x00ff00 ||
                (bmpImage->red_mask|bmpImage->blue_mask)!=0xff00ff) {
                goto notsupported;
            } else if ((rDst==0x1f && bmpImage->red_mask==0xff) ||
                       (bDst==0x1f && bmpImage->blue_mask==0xff)) {
                if (gDst==0x03e0) {
                    /* ==== rgb 0888 bmp -> rgb 555 dib ==== */
                    /* ==== bgr 0888 bmp -> bgr 555 dib ==== */
                    X11DRV_DIB_Convert_0888_to_555_asis
                        (dstwidth,lines,
                         srcbits,-bmpImage->bytes_per_line,
                         dstbits,linebytes);
                } else {
                    /* ==== rgb 0888 bmp -> rgb 565 dib ==== */
                    /* ==== bgr 0888 bmp -> bgr 565 dib ==== */
                    X11DRV_DIB_Convert_0888_to_565_asis
                        (dstwidth,lines,
                         srcbits,-bmpImage->bytes_per_line,
                         dstbits,linebytes);
                }
            } else {
                if (gDst==0x03e0) {
                    /* ==== rgb 0888 bmp -> bgr 555 dib ==== */
                    /* ==== bgr 0888 bmp -> rgb 555 dib ==== */
                    X11DRV_DIB_Convert_0888_to_555_reverse
                        (dstwidth,lines,
                         srcbits,-bmpImage->bytes_per_line,
                         dstbits,linebytes);
                } else {
                    /* ==== rgb 0888 bmp -> bgr 565 dib ==== */
                    /* ==== bgr 0888 bmp -> rgb 565 dib ==== */
                    X11DRV_DIB_Convert_0888_to_565_reverse
                        (dstwidth,lines,
                         srcbits,-bmpImage->bytes_per_line,
                         dstbits,linebytes);
                }
            }
        }
        break;

    case 1:
    case 4:
        if (bmpImage->red_mask==0 && bmpImage->green_mask==0 && bmpImage->blue_mask==0 && srccolors) {
            /* ==== pal 1 or 4 bmp -> rgb or bgr 555 or 565 dib ==== */
            int rShift,gShift,bShift;
            WORD* dstpixel;

            /* Shift everything 16 bits left so that all shifts are >0,
             * even for BGR DIBs. Then a single >> 16 will bring everything
             * back into place.
             */
            rShift=16+X11DRV_DIB_MaskToShift(rDst)-3;
            gShift=16+X11DRV_DIB_MaskToShift(gDst)-3;
            bShift=16+X11DRV_DIB_MaskToShift(bDst)-3;
            if (gDst==0x07e0) {
                /* 6 bits for the green */
                gShift++;
            }
            rDst=rDst << 16;
            gDst=gDst << 16;
            bDst=bDst << 16;
            for (h = lines - 1; h >= 0; h--) {
                dstpixel=(LPWORD)dstbits;
                for (x = 0; x < dstwidth; x++) {
                    PALETTEENTRY srcval;
                    DWORD dstval;
                    srcval=srccolors[XGetPixel(bmpImage, x, h)];
                    dstval=((srcval.peRed   << rShift) & rDst) |
                           ((srcval.peGreen << gShift) & gDst) |
                           ((srcval.peBlue  << bShift) & bDst);
                    *dstpixel++=dstval >> 16;
                }
                dstbits += linebytes;
            }
        } else {
            goto notsupported;
        }
        break;

    case 8:
        if (bmpImage->red_mask==0 && bmpImage->green_mask==0 && bmpImage->blue_mask==0 && srccolors) {
            /* ==== pal 8 bmp -> rgb or bgr 555 or 565 dib ==== */
            int rShift,gShift,bShift;
            const BYTE* srcbits;
            const BYTE* srcpixel;
            WORD* dstpixel;

            /* Shift everything 16 bits left so that all shifts are >0,
             * even for BGR DIBs. Then a single >> 16 will bring everything
             * back into place.
             */
            rShift=16+X11DRV_DIB_MaskToShift(rDst)-3;
            gShift=16+X11DRV_DIB_MaskToShift(gDst)-3;
            bShift=16+X11DRV_DIB_MaskToShift(bDst)-3;
            if (gDst==0x07e0) {
                /* 6 bits for the green */
                gShift++;
            }
            rDst=rDst << 16;
            gDst=gDst << 16;
            bDst=bDst << 16;
            srcbits=bmpImage->data+(lines-1)*bmpImage->bytes_per_line;
            for (h=0; h<lines; h++) {
                srcpixel=srcbits;
                dstpixel=(LPWORD)dstbits;
                for (x = 0; x < dstwidth; x++) {
                    PALETTEENTRY srcval;
                    DWORD dstval;
                    srcval=srccolors[(int)*srcpixel++];
                    dstval=((srcval.peRed   << rShift) & rDst) |
                           ((srcval.peGreen << gShift) & gDst) |
                           ((srcval.peBlue  << bShift) & bDst);
                    *dstpixel++=dstval >> 16;
                }
                srcbits -= bmpImage->bytes_per_line;
                dstbits += linebytes;
            }
        } else {
            goto notsupported;
        }
        break;

    default:
    notsupported:
        {
            /* ==== any bmp format -> rgb or bgr 555 or 565 dib ==== */
            int rShift,gShift,bShift;
            WORD* dstpixel;

            WARN("from unknown %d bit bitmap (%lx,%lx,%lx) to 16 bit DIB (%lx,%lx,%lx)\n",
                  bmpImage->depth, bmpImage->red_mask,
                  bmpImage->green_mask, bmpImage->blue_mask,
                  rDst, gDst, bDst);

            /* Shift everything 16 bits left so that all shifts are >0,
             * even for BGR DIBs. Then a single >> 16 will bring everything
             * back into place.
             */
            rShift=16+X11DRV_DIB_MaskToShift(rDst)-3;
            gShift=16+X11DRV_DIB_MaskToShift(gDst)-3;
            bShift=16+X11DRV_DIB_MaskToShift(bDst)-3;
            if (gDst==0x07e0) {
                /* 6 bits for the green */
                gShift++;
            }
            rDst=rDst << 16;
            gDst=gDst << 16;
            bDst=bDst << 16;
            for (h = lines - 1; h >= 0; h--) {
                dstpixel=(LPWORD)dstbits;
                for (x = 0; x < dstwidth; x++) {
                    COLORREF srcval;
                    DWORD dstval;
                    srcval=X11DRV_PALETTE_ToLogical(XGetPixel(bmpImage, x, h));
                    dstval=((GetRValue(srcval) << rShift) & rDst) |
                           ((GetGValue(srcval) << gShift) & gDst) |
                           ((GetBValue(srcval) << bShift) & bDst);
                    *dstpixel++=dstval >> 16;
                }
                dstbits += linebytes;
            }
        }
        break;
    }
}


/***********************************************************************
 *           X11DRV_DIB_SetImageBits_24
 *
 * SetDIBits for a 24-bit deep DIB.
 */
static void X11DRV_DIB_SetImageBits_24( int lines, const BYTE *srcbits,
                                 DWORD srcwidth, DWORD dstwidth, int left,
                                 DC *dc,
                                 DWORD rSrc, DWORD gSrc, DWORD bSrc,
                                 XImage *bmpImage, DWORD linebytes )
{
    DWORD x;
    int h;

    if (lines < 0 )
    {
        lines = -lines;
        srcbits = srcbits + linebytes * (lines - 1);
        linebytes = -linebytes;
    }

    switch (bmpImage->depth)
    {
    case 24:
        if (bmpImage->bits_per_pixel==24) {
            char* dstbits;

            srcbits=srcbits+left*3;
            dstbits=bmpImage->data+left*3+(lines-1)*bmpImage->bytes_per_line;

            if (bmpImage->green_mask!=0x00ff00 ||
                (bmpImage->red_mask|bmpImage->blue_mask)!=0xff00ff) {
                goto notsupported;
            } else if (rSrc==bmpImage->red_mask) {
                /* ==== rgb 888 dib -> rgb 888 bmp ==== */
                /* ==== bgr 888 dib -> bgr 888 bmp ==== */
                X11DRV_DIB_Convert_any_asis
                    (dstwidth,lines,3,
                     srcbits,linebytes,
                     dstbits,-bmpImage->bytes_per_line);
            } else {
                /* ==== rgb 888 dib -> bgr 888 bmp ==== */
                /* ==== bgr 888 dib -> rgb 888 bmp ==== */
                X11DRV_DIB_Convert_888_reverse
                    (dstwidth,lines,
                     srcbits,linebytes,
                     dstbits,-bmpImage->bytes_per_line);
            }
            break;
        }
        /* fall through */

    case 32:
        {
            char* dstbits;

            srcbits=srcbits+left*3;
            dstbits=bmpImage->data+left*4+(lines-1)*bmpImage->bytes_per_line;

            if (bmpImage->green_mask!=0x00ff00 ||
                (bmpImage->red_mask|bmpImage->blue_mask)!=0xff00ff) {
                goto notsupported;
            } else if (rSrc==bmpImage->red_mask) {
                /* ==== rgb 888 dib -> rgb 0888 bmp ==== */
                /* ==== bgr 888 dib -> bgr 0888 bmp ==== */
                X11DRV_DIB_Convert_888_to_0888_asis
                    (dstwidth,lines,
                     srcbits,linebytes,
                     dstbits,-bmpImage->bytes_per_line);
            } else {
                /* ==== rgb 888 dib -> bgr 0888 bmp ==== */
                /* ==== bgr 888 dib -> rgb 0888 bmp ==== */
                X11DRV_DIB_Convert_888_to_0888_reverse
                    (dstwidth,lines,
                     srcbits,linebytes,
                     dstbits,-bmpImage->bytes_per_line);
            }
            break;
        }

    case 15:
    case 16:
        {
            char* dstbits;

            srcbits=srcbits+left*3;
            dstbits=bmpImage->data+left*2+(lines-1)*bmpImage->bytes_per_line;

            if (bmpImage->green_mask==0x03e0) {
                if ((rSrc==0xff0000 && bmpImage->red_mask==0x7f00) ||
                    (bSrc==0xff0000 && bmpImage->blue_mask==0x7f00)) {
                    /* ==== rgb 888 dib -> rgb 555 bmp ==== */
                    /* ==== bgr 888 dib -> bgr 555 bmp ==== */
                    X11DRV_DIB_Convert_888_to_555_asis
                        (dstwidth,lines,
                         srcbits,linebytes,
                         dstbits,-bmpImage->bytes_per_line);
                } else if ((rSrc==0xff && bmpImage->red_mask==0x7f00) ||
                           (bSrc==0xff && bmpImage->blue_mask==0x7f00)) {
                    /* ==== rgb 888 dib -> bgr 555 bmp ==== */
                    /* ==== bgr 888 dib -> rgb 555 bmp ==== */
                    X11DRV_DIB_Convert_888_to_555_reverse
                        (dstwidth,lines,
                         srcbits,linebytes,
                         dstbits,-bmpImage->bytes_per_line);
                } else {
                    goto notsupported;
                }
            } else if (bmpImage->green_mask==0x07e0) {
                if ((rSrc==0xff0000 && bmpImage->red_mask==0xf800) ||
                    (bSrc==0xff0000 && bmpImage->blue_mask==0xf800)) {
                    /* ==== rgb 888 dib -> rgb 565 bmp ==== */
                    /* ==== bgr 888 dib -> bgr 565 bmp ==== */
                    X11DRV_DIB_Convert_888_to_565_asis
                        (dstwidth,lines,
                         srcbits,linebytes,
                         dstbits,-bmpImage->bytes_per_line);
                } else if ((rSrc==0xff && bmpImage->red_mask==0xf800) ||
                           (bSrc==0xff && bmpImage->blue_mask==0xf800)) {
                    /* ==== rgb 888 dib -> bgr 565 bmp ==== */
                    /* ==== bgr 888 dib -> rgb 565 bmp ==== */
                    X11DRV_DIB_Convert_888_to_565_reverse
                        (dstwidth,lines,
                         srcbits,linebytes,
                         dstbits,-bmpImage->bytes_per_line);
                } else {
                    goto notsupported;
                }
            } else {
                goto notsupported;
            }
        }
        break;

    default:
    notsupported:
        WARN("from 24 bit DIB (%lx,%lx,%lx) to unknown %d bit bitmap (%lx,%lx,%lx)\n",
              rSrc, gSrc, bSrc, bmpImage->bits_per_pixel, bmpImage->red_mask,
              bmpImage->green_mask, bmpImage->blue_mask );
        /* fall through */
    case 1:
    case 4:
    case 8:
        {
            /* ==== rgb 888 dib -> any bmp bormat ==== */
            const BYTE* srcbyte;

            /* Windows only supports one 24bpp DIB format: RGB888 */
            srcbits+=left*3;
            for (h = lines - 1; h >= 0; h--) {
                srcbyte=(const BYTE*)srcbits;
                for (x = left; x < dstwidth+left; x++) {
                    XPutPixel(bmpImage, x, h,
                              X11DRV_PALETTE_ToPhysical
                              (dc, RGB(srcbyte[2], srcbyte[1], srcbyte[0])));
                    srcbyte+=3;
                }
                srcbits += linebytes;
            }
        }
        break;
    }
}


/***********************************************************************
 *           X11DRV_DIB_GetImageBits_24
 *
 * GetDIBits for an 24-bit deep DIB.
 */
static void X11DRV_DIB_GetImageBits_24( int lines, BYTE *dstbits,
					DWORD dstwidth, DWORD srcwidth,
					PALETTEENTRY *srccolors,
                                        DWORD rDst, DWORD gDst, DWORD bDst,
					XImage *bmpImage, DWORD linebytes )
{
    DWORD x;
    int h;

    if (lines < 0 )
    {
        lines = -lines;
        dstbits = dstbits + ( linebytes * (lines-1) );
        linebytes = -linebytes;
    }

    switch (bmpImage->depth)
    {
    case 24:
        if (bmpImage->bits_per_pixel==24) {
            const char* srcbits;

            srcbits=bmpImage->data+(lines-1)*bmpImage->bytes_per_line;

            if (bmpImage->green_mask!=0x00ff00 ||
                (bmpImage->red_mask|bmpImage->blue_mask)!=0xff00ff) {
                goto notsupported;
            } else if (rDst==bmpImage->red_mask) {
                /* ==== rgb 888 bmp -> rgb 888 dib ==== */
                /* ==== bgr 888 bmp -> bgr 888 dib ==== */
                X11DRV_DIB_Convert_any_asis
                    (dstwidth,lines,3,
                     srcbits,-bmpImage->bytes_per_line,
                     dstbits,linebytes);
            } else {
                /* ==== rgb 888 bmp -> bgr 888 dib ==== */
                /* ==== bgr 888 bmp -> rgb 888 dib ==== */
                X11DRV_DIB_Convert_888_reverse
                    (dstwidth,lines,
                     srcbits,-bmpImage->bytes_per_line,
                     dstbits,linebytes);
            }
            break;
        }
        /* fall through */

    case 32:
        {
            const char* srcbits;

            srcbits=bmpImage->data+(lines-1)*bmpImage->bytes_per_line;

            if (bmpImage->green_mask!=0x00ff00 ||
                (bmpImage->red_mask|bmpImage->blue_mask)!=0xff00ff) {
                goto notsupported;
            } else if (rDst==bmpImage->red_mask) {
                /* ==== rgb 888 bmp -> rgb 0888 dib ==== */
                /* ==== bgr 888 bmp -> bgr 0888 dib ==== */
                X11DRV_DIB_Convert_0888_to_888_asis
                    (dstwidth,lines,
                     srcbits,-bmpImage->bytes_per_line,
                     dstbits,linebytes);
            } else {
                /* ==== rgb 888 bmp -> bgr 0888 dib ==== */
                /* ==== bgr 888 bmp -> rgb 0888 dib ==== */
                X11DRV_DIB_Convert_0888_to_888_reverse
                    (dstwidth,lines,
                     srcbits,-bmpImage->bytes_per_line,
                     dstbits,linebytes);
            }
            break;
        }

    case 15:
    case 16:
        {
            const char* srcbits;

            srcbits=bmpImage->data+(lines-1)*bmpImage->bytes_per_line;

            if (bmpImage->green_mask==0x03e0) {
                if ((rDst==0xff0000 && bmpImage->red_mask==0x7f00) ||
                    (bDst==0xff0000 && bmpImage->blue_mask==0x7f00)) {
                    /* ==== rgb 555 bmp -> rgb 888 dib ==== */
                    /* ==== bgr 555 bmp -> bgr 888 dib ==== */
                    X11DRV_DIB_Convert_555_to_888_asis
                        (dstwidth,lines,
                         srcbits,-bmpImage->bytes_per_line,
                         dstbits,linebytes);
                } else if ((rDst==0xff && bmpImage->red_mask==0x7f00) ||
                           (bDst==0xff && bmpImage->blue_mask==0x7f00)) {
                    /* ==== rgb 555 bmp -> bgr 888 dib ==== */
                    /* ==== bgr 555 bmp -> rgb 888 dib ==== */
                    X11DRV_DIB_Convert_555_to_888_reverse
                        (dstwidth,lines,
                         srcbits,-bmpImage->bytes_per_line,
                         dstbits,linebytes);
                } else {
                    goto notsupported;
                }
            } else if (bmpImage->green_mask==0x07e0) {
                if ((rDst==0xff0000 && bmpImage->red_mask==0xf800) ||
                    (bDst==0xff0000 && bmpImage->blue_mask==0xf800)) {
                    /* ==== rgb 565 bmp -> rgb 888 dib ==== */
                    /* ==== bgr 565 bmp -> bgr 888 dib ==== */
                    X11DRV_DIB_Convert_565_to_888_asis
                        (dstwidth,lines,
                         srcbits,-bmpImage->bytes_per_line,
                         dstbits,linebytes);
                } else if ((rDst==0xff && bmpImage->red_mask==0xf800) ||
                           (bDst==0xff && bmpImage->blue_mask==0xf800)) {
                    /* ==== rgb 565 bmp -> bgr 888 dib ==== */
                    /* ==== bgr 565 bmp -> rgb 888 dib ==== */
                    X11DRV_DIB_Convert_565_to_888_reverse
                        (dstwidth,lines,
                         srcbits,-bmpImage->bytes_per_line,
                         dstbits,linebytes);
                } else {
                    goto notsupported;
                }
            } else {
                goto notsupported;
            }
        }
        break;

    case 1:
    case 4:
        if (bmpImage->red_mask==0 && bmpImage->green_mask==0 && bmpImage->blue_mask==0 && srccolors) {
            /* ==== pal 1 or 4 bmp -> rgb 888 dib ==== */
            BYTE* dstbyte;

            /* Windows only supports one 24bpp DIB format: rgb 888 */
            for (h = lines - 1; h >= 0; h--) {
                dstbyte=dstbits;
                for (x = 0; x < dstwidth; x++) {
                    PALETTEENTRY srcval;
                    srcval=srccolors[XGetPixel(bmpImage, x, h)];
                    dstbyte[0]=srcval.peBlue;
                    dstbyte[1]=srcval.peGreen;
                    dstbyte[2]=srcval.peRed;
                    dstbyte+=3;
                }
                dstbits += linebytes;
            }
        } else {
            goto notsupported;
        }
        break;

    case 8:
        if (bmpImage->red_mask==0 && bmpImage->green_mask==0 && bmpImage->blue_mask == 0 && srccolors) {
            /* ==== pal 8 bmp -> rgb 888 dib ==== */
            const void* srcbits;
            const BYTE* srcpixel;
            BYTE* dstbyte;

            /* Windows only supports one 24bpp DIB format: rgb 888 */
            srcbits=bmpImage->data+(lines-1)*bmpImage->bytes_per_line;
            for (h = lines - 1; h >= 0; h--) {
                srcpixel=srcbits;
                dstbyte=dstbits;
                for (x = 0; x < dstwidth; x++ ) {
                    PALETTEENTRY srcval;
                    srcval=srccolors[(int)*srcpixel++];
                    dstbyte[0]=srcval.peBlue;
                    dstbyte[1]=srcval.peGreen;
                    dstbyte[2]=srcval.peRed;
                    dstbyte+=3;
                }
                srcbits -= bmpImage->bytes_per_line;
                dstbits += linebytes;
            }
        } else {
            goto notsupported;
        }
        break;

    default:
    notsupported:
        {
            /* ==== any bmp format -> 888 dib ==== */
            BYTE* dstbyte;

            WARN("from unknown %d bit bitmap (%lx,%lx,%lx) to 24 bit DIB (%lx,%lx,%lx)\n",
                  bmpImage->depth, bmpImage->red_mask,
                  bmpImage->green_mask, bmpImage->blue_mask,
                  rDst, gDst, bDst );

            /* Windows only supports one 24bpp DIB format: rgb 888 */
            for (h = lines - 1; h >= 0; h--) {
                dstbyte=dstbits;
                for (x = 0; x < dstwidth; x++) {
                    COLORREF srcval=X11DRV_PALETTE_ToLogical
                        (XGetPixel( bmpImage, x, h ));
                    dstbyte[0]=GetBValue(srcval);
                    dstbyte[1]=GetGValue(srcval);
                    dstbyte[2]=GetRValue(srcval);
                    dstbyte+=3;
                }
                dstbits += linebytes;
            }
        }
        break;
    }
}


/***********************************************************************
 *           X11DRV_DIB_SetImageBits_32
 *
 * SetDIBits for a 32-bit deep DIB.
 */
static void X11DRV_DIB_SetImageBits_32(int lines, const BYTE *srcbits,
                                       DWORD srcwidth, DWORD dstwidth, int left,
                                       DC *dc,
                                       DWORD rSrc, DWORD gSrc, DWORD bSrc,
                                       XImage *bmpImage,
                                       DWORD linebytes)
{
    DWORD x, *ptr;
    int h;

    if (lines < 0 )
    {
       lines = -lines;
       srcbits = srcbits + ( linebytes * (lines-1) );
       linebytes = -linebytes;
    }

    ptr = (DWORD *) srcbits + left;

    switch (bmpImage->depth)
    {
    case 24:
        if (bmpImage->bits_per_pixel==24) {
            char* dstbits;

            srcbits=srcbits+left*4;
            dstbits=bmpImage->data+left*3+(lines-1)*bmpImage->bytes_per_line;

            if (rSrc==bmpImage->red_mask && gSrc==bmpImage->green_mask && bSrc==bmpImage->blue_mask) {
                /* ==== rgb 0888 dib -> rgb 888 bmp ==== */
                /* ==== bgr 0888 dib -> bgr 888 bmp ==== */
                X11DRV_DIB_Convert_0888_to_888_asis
                    (dstwidth,lines,
                     srcbits,linebytes,
                     dstbits,-bmpImage->bytes_per_line);
            } else if (bmpImage->green_mask!=0x00ff00 ||
                       (bmpImage->red_mask|bmpImage->blue_mask)!=0xff00ff) {
                goto notsupported;
                /* the tests below assume sane bmpImage masks */
            } else if (rSrc==bmpImage->blue_mask && gSrc==bmpImage->green_mask && bSrc==bmpImage->red_mask) {
                /* ==== rgb 0888 dib -> bgr 888 bmp ==== */
                /* ==== bgr 0888 dib -> rgb 888 bmp ==== */
                X11DRV_DIB_Convert_0888_to_888_reverse
                    (dstwidth,lines,
                     srcbits,linebytes,
                     dstbits,-bmpImage->bytes_per_line);
            } else if (bmpImage->blue_mask==0xff) {
                /* ==== any 0888 dib -> rgb 888 bmp ==== */
                X11DRV_DIB_Convert_any0888_to_rgb888
                    (dstwidth,lines,
                     srcbits,linebytes,
                     rSrc,gSrc,bSrc,
                     dstbits,-bmpImage->bytes_per_line);
            } else {
                /* ==== any 0888 dib -> bgr 888 bmp ==== */
                X11DRV_DIB_Convert_any0888_to_bgr888
                    (dstwidth,lines,
                     srcbits,linebytes,
                     rSrc,gSrc,bSrc,
                     dstbits,-bmpImage->bytes_per_line);
            }
            break;
        }
        /* fall through */

    case 32:
        {
            char* dstbits;

            srcbits=srcbits+left*4;
            dstbits=bmpImage->data+left*4+(lines-1)*bmpImage->bytes_per_line;

            if (gSrc==bmpImage->green_mask) {
                if (rSrc==bmpImage->red_mask && bSrc==bmpImage->blue_mask) {
                    /* ==== rgb 0888 dib -> rgb 0888 bmp ==== */
                    /* ==== bgr 0888 dib -> bgr 0888 bmp ==== */
                    X11DRV_DIB_Convert_any_asis
                        (dstwidth,lines,4,
                         srcbits,linebytes,
                         dstbits,-bmpImage->bytes_per_line);
                } else if (bmpImage->green_mask!=0x00ff00 ||
                           (bmpImage->red_mask|bmpImage->blue_mask)!=0xff00ff) {
                    goto notsupported;
                    /* the tests below assume sane bmpImage masks */
                } else if (rSrc==bmpImage->blue_mask && bSrc==bmpImage->red_mask) {
                    /* ==== rgb 0888 dib -> bgr 0888 bmp ==== */
                    /* ==== bgr 0888 dib -> rgb 0888 bmp ==== */
                    X11DRV_DIB_Convert_0888_reverse
                        (dstwidth,lines,
                         srcbits,linebytes,
                         dstbits,-bmpImage->bytes_per_line);
                } else {
                    /* ==== any 0888 dib -> any 0888 bmp ==== */
                    X11DRV_DIB_Convert_0888_any
                        (dstwidth,lines,
                         srcbits,linebytes,
                         rSrc,gSrc,bSrc,
                         dstbits,-bmpImage->bytes_per_line,
                         bmpImage->red_mask,bmpImage->green_mask,bmpImage->blue_mask);
                }
            } else if (bmpImage->green_mask!=0x00ff00 ||
                       (bmpImage->red_mask|bmpImage->blue_mask)!=0xff00ff) {
                goto notsupported;
                /* the tests below assume sane bmpImage masks */
            } else {
                /* ==== any 0888 dib -> any 0888 bmp ==== */
                X11DRV_DIB_Convert_0888_any
                    (dstwidth,lines,
                     srcbits,linebytes,
                     rSrc,gSrc,bSrc,
                     dstbits,-bmpImage->bytes_per_line,
                     bmpImage->red_mask,bmpImage->green_mask,bmpImage->blue_mask);
            }
        }
        break;

    case 15:
    case 16:
        {
            char* dstbits;

            srcbits=srcbits+left*4;
            dstbits=bmpImage->data+left*2+(lines-1)*bmpImage->bytes_per_line;

            if (rSrc==0xff0000 && gSrc==0x00ff00 && bSrc==0x0000ff) {
                if (bmpImage->green_mask==0x03e0) {
                    if (bmpImage->red_mask==0x7f00) {
                        /* ==== rgb 0888 dib -> rgb 555 bmp ==== */
                        X11DRV_DIB_Convert_0888_to_555_asis
                            (dstwidth,lines,
                             srcbits,linebytes,
                             dstbits,-bmpImage->bytes_per_line);
                    } else if (bmpImage->blue_mask==0x7f00) {
                        /* ==== rgb 0888 dib -> bgr 555 bmp ==== */
                        X11DRV_DIB_Convert_0888_to_555_reverse
                            (dstwidth,lines,
                             srcbits,linebytes,
                             dstbits,-bmpImage->bytes_per_line);
                    } else {
                        goto notsupported;
                    }
                } else if (bmpImage->green_mask==0x07e0) {
                    if (bmpImage->red_mask==0xf800) {
                        /* ==== rgb 0888 dib -> rgb 565 bmp ==== */
                        X11DRV_DIB_Convert_0888_to_565_asis
                            (dstwidth,lines,
                             srcbits,linebytes,
                             dstbits,-bmpImage->bytes_per_line);
                    } else if (bmpImage->blue_mask==0xf800) {
                        /* ==== rgb 0888 dib -> bgr 565 bmp ==== */
                        X11DRV_DIB_Convert_0888_to_565_reverse
                            (dstwidth,lines,
                             srcbits,linebytes,
                             dstbits,-bmpImage->bytes_per_line);
                    } else {
                        goto notsupported;
                    }
                } else {
                    goto notsupported;
                }
            } else if (rSrc==0x0000ff && gSrc==0x00ff00 && bSrc==0xff0000) {
                if (bmpImage->green_mask==0x03e0) {
                    if (bmpImage->blue_mask==0x7f00) {
                        /* ==== bgr 0888 dib -> bgr 555 bmp ==== */
                        X11DRV_DIB_Convert_0888_to_555_asis
                            (dstwidth,lines,
                             srcbits,linebytes,
                             dstbits,-bmpImage->bytes_per_line);
                    } else if (bmpImage->red_mask==0x7f00) {
                        /* ==== bgr 0888 dib -> rgb 555 bmp ==== */
                        X11DRV_DIB_Convert_0888_to_555_reverse
                            (dstwidth,lines,
                             srcbits,linebytes,
                             dstbits,-bmpImage->bytes_per_line);
                    } else {
                        goto notsupported;
                    }
                } else if (bmpImage->green_mask==0x07e0) {
                    if (bmpImage->blue_mask==0xf800) {
                        /* ==== bgr 0888 dib -> bgr 565 bmp ==== */
                        X11DRV_DIB_Convert_0888_to_565_asis
                            (dstwidth,lines,
                             srcbits,linebytes,
                             dstbits,-bmpImage->bytes_per_line);
                    } else if (bmpImage->red_mask==0xf800) {
                        /* ==== bgr 0888 dib -> rgb 565 bmp ==== */
                        X11DRV_DIB_Convert_0888_to_565_reverse
                            (dstwidth,lines,
                             srcbits,linebytes,
                             dstbits,-bmpImage->bytes_per_line);
                    } else {
                        goto notsupported;
                    }
                } else {
                    goto notsupported;
                }
            } else {
                if (bmpImage->green_mask==0x03e0 &&
                    (bmpImage->red_mask==0x7f00 ||
                     bmpImage->blue_mask==0x7f00)) {
                    /* ==== any 0888 dib -> rgb or bgr 555 bmp ==== */
                    X11DRV_DIB_Convert_any0888_to_5x5
                        (dstwidth,lines,
                         srcbits,linebytes,
                         rSrc,gSrc,bSrc,
                         dstbits,-bmpImage->bytes_per_line,
                         bmpImage->red_mask,bmpImage->green_mask,bmpImage->blue_mask);
                } else if (bmpImage->green_mask==0x07e0 &&
                           (bmpImage->red_mask==0xf800 ||
                            bmpImage->blue_mask==0xf800)) {
                    /* ==== any 0888 dib -> rgb or bgr 565 bmp ==== */
                    X11DRV_DIB_Convert_any0888_to_5x5
                        (dstwidth,lines,
                         srcbits,linebytes,
                         rSrc,gSrc,bSrc,
                         dstbits,-bmpImage->bytes_per_line,
                         bmpImage->red_mask,bmpImage->green_mask,bmpImage->blue_mask);
                } else {
                    goto notsupported;
                }
            }
        }
        break;

    default:
    notsupported:
        WARN("from 32 bit DIB (%lx,%lx,%lx) to unknown %d bit bitmap (%lx,%lx,%lx)\n",
              rSrc, gSrc, bSrc, bmpImage->bits_per_pixel, bmpImage->red_mask,
              bmpImage->green_mask, bmpImage->blue_mask );
        /* fall through */
    case 1:
    case 4:
    case 8:
        {
            /* ==== any 0888 dib -> pal 1, 4 or 8 bmp ==== */
            const DWORD* srcpixel;
            int rShift,gShift,bShift;

            rShift=X11DRV_DIB_MaskToShift(rSrc);
            gShift=X11DRV_DIB_MaskToShift(gSrc);
            bShift=X11DRV_DIB_MaskToShift(bSrc);
            srcbits+=left*4;
            for (h = lines - 1; h >= 0; h--) {
                srcpixel=(const DWORD*)srcbits;
                for (x = left; x < dstwidth+left; x++) {
                    DWORD srcvalue;
                    BYTE red,green,blue;
                    srcvalue=*srcpixel++;
                    red=  (srcvalue >> rShift) & 0xff;
                    green=(srcvalue >> gShift) & 0xff;
                    blue= (srcvalue >> bShift) & 0xff;
                    XPutPixel(bmpImage, x, h, X11DRV_PALETTE_ToPhysical
                              (dc, RGB(red,green,blue)));
                }
                srcbits += linebytes;
            }
        }
        break;
    }

}

/***********************************************************************
 *           X11DRV_DIB_GetImageBits_32
 *
 * GetDIBits for an 32-bit deep DIB.
 */
static void X11DRV_DIB_GetImageBits_32( int lines, BYTE *dstbits,
					DWORD dstwidth, DWORD srcwidth,
					PALETTEENTRY *srccolors,
					DWORD rDst, DWORD gDst, DWORD bDst,
					XImage *bmpImage, DWORD linebytes )
{
    DWORD x;
    int h;
    BYTE *bits;

    if (lines < 0 )
    {
        lines = -lines;
        dstbits = dstbits + ( linebytes * (lines-1) );
        linebytes = -linebytes;
    }

    bits = dstbits;

    switch (bmpImage->depth)
    {
    case 24:
        if (bmpImage->bits_per_pixel==24) {
            const void* srcbits;

            srcbits=bmpImage->data+(lines-1)*bmpImage->bytes_per_line;

            if (rDst==bmpImage->red_mask && gDst==bmpImage->green_mask && bDst==bmpImage->blue_mask) {
                /* ==== rgb 888 bmp -> rgb 0888 dib ==== */
                /* ==== bgr 888 bmp -> bgr 0888 dib ==== */
                X11DRV_DIB_Convert_888_to_0888_asis
                    (dstwidth,lines,
                     srcbits,-bmpImage->bytes_per_line,
                     dstbits,linebytes);
            } else if (bmpImage->green_mask!=0x00ff00 ||
                       (bmpImage->red_mask|bmpImage->blue_mask)!=0xff00ff) {
                goto notsupported;
                /* the tests below assume sane bmpImage masks */
            } else if (rDst==bmpImage->blue_mask && gDst==bmpImage->green_mask && bDst==bmpImage->red_mask) {
                /* ==== rgb 888 bmp -> bgr 0888 dib ==== */
                /* ==== bgr 888 bmp -> rgb 0888 dib ==== */
                X11DRV_DIB_Convert_888_to_0888_reverse
                    (dstwidth,lines,
                     srcbits,-bmpImage->bytes_per_line,
                     dstbits,linebytes);
            } else if (bmpImage->blue_mask==0xff) {
                /* ==== rgb 888 bmp -> any 0888 dib ==== */
                X11DRV_DIB_Convert_rgb888_to_any0888
                    (dstwidth,lines,
                     srcbits,-bmpImage->bytes_per_line,
                     dstbits,linebytes,
                     rDst,gDst,bDst);
            } else {
                /* ==== bgr 888 bmp -> any 0888 dib ==== */
                X11DRV_DIB_Convert_bgr888_to_any0888
                    (dstwidth,lines,
                     srcbits,-bmpImage->bytes_per_line,
                     dstbits,linebytes,
                     rDst,gDst,bDst);
            }
            break;
        }
        /* fall through */

    case 32:
        {
            const char* srcbits;

            srcbits=bmpImage->data+(lines-1)*bmpImage->bytes_per_line;

            if (gDst==bmpImage->green_mask) {
                if (rDst==bmpImage->red_mask && bDst==bmpImage->blue_mask) {
                    /* ==== rgb 0888 bmp -> rgb 0888 dib ==== */
                    /* ==== bgr 0888 bmp -> bgr 0888 dib ==== */
                    X11DRV_DIB_Convert_any_asis
                        (dstwidth,lines,4,
                         srcbits,-bmpImage->bytes_per_line,
                         dstbits,linebytes);
                } else if (bmpImage->green_mask!=0x00ff00 ||
                           (bmpImage->red_mask|bmpImage->blue_mask)!=0xff00ff) {
                    goto notsupported;
                    /* the tests below assume sane bmpImage masks */
                } else if (rDst==bmpImage->blue_mask && bDst==bmpImage->red_mask) {
                    /* ==== rgb 0888 bmp -> bgr 0888 dib ==== */
                    /* ==== bgr 0888 bmp -> rgb 0888 dib ==== */
                    X11DRV_DIB_Convert_0888_reverse
                        (dstwidth,lines,
                         srcbits,-bmpImage->bytes_per_line,
                         dstbits,linebytes);
                } else {
                    /* ==== any 0888 bmp -> any 0888 dib ==== */
                    X11DRV_DIB_Convert_0888_any
                        (dstwidth,lines,
                         srcbits,-bmpImage->bytes_per_line,
                         bmpImage->red_mask,bmpImage->green_mask,bmpImage->blue_mask,
                         dstbits,linebytes,
                         rDst,gDst,bDst);
                }
            } else if (bmpImage->green_mask!=0x00ff00 ||
                       (bmpImage->red_mask|bmpImage->blue_mask)!=0xff00ff) {
                goto notsupported;
                /* the tests below assume sane bmpImage masks */
            } else {
                /* ==== any 0888 bmp -> any 0888 dib ==== */
                X11DRV_DIB_Convert_0888_any
                    (dstwidth,lines,
                     srcbits,-bmpImage->bytes_per_line,
                     bmpImage->red_mask,bmpImage->green_mask,bmpImage->blue_mask,
                     dstbits,linebytes,
                     rDst,gDst,bDst);
            }
        }
        break;

    case 15:
    case 16:
        {
            const char* srcbits;

            srcbits=bmpImage->data+(lines-1)*bmpImage->bytes_per_line;

            if (rDst==0xff0000 && gDst==0x00ff00 && bDst==0x0000ff) {
                if (bmpImage->green_mask==0x03e0) {
                    if (bmpImage->red_mask==0x7f00) {
                        /* ==== rgb 555 bmp -> rgb 0888 dib ==== */
                        X11DRV_DIB_Convert_555_to_0888_asis
                            (dstwidth,lines,
                             srcbits,-bmpImage->bytes_per_line,
                             dstbits,linebytes);
                    } else if (bmpImage->blue_mask==0x7f00) {
                        /* ==== bgr 555 bmp -> rgb 0888 dib ==== */
                        X11DRV_DIB_Convert_555_to_0888_reverse
                            (dstwidth,lines,
                             srcbits,-bmpImage->bytes_per_line,
                             dstbits,linebytes);
                    } else {
                        goto notsupported;
                    }
                } else if (bmpImage->green_mask==0x07e0) {
                    if (bmpImage->red_mask==0xf800) {
                        /* ==== rgb 565 bmp -> rgb 0888 dib ==== */
                        X11DRV_DIB_Convert_565_to_0888_asis
                            (dstwidth,lines,
                             srcbits,-bmpImage->bytes_per_line,
                             dstbits,linebytes);
                    } else if (bmpImage->blue_mask==0xf800) {
                        /* ==== bgr 565 bmp -> rgb 0888 dib ==== */
                        X11DRV_DIB_Convert_565_to_0888_reverse
                            (dstwidth,lines,
                             srcbits,-bmpImage->bytes_per_line,
                             dstbits,linebytes);
                    } else {
                        goto notsupported;
                    }
                } else {
                    goto notsupported;
                }
            } else if (rDst==0x0000ff && gDst==0x00ff00 && bDst==0xff0000) {
                if (bmpImage->green_mask==0x03e0) {
                    if (bmpImage->blue_mask==0x7f00) {
                        /* ==== bgr 555 bmp -> bgr 0888 dib ==== */
                        X11DRV_DIB_Convert_555_to_0888_asis
                            (dstwidth,lines,
                             srcbits,-bmpImage->bytes_per_line,
                             dstbits,linebytes);
                    } else if (bmpImage->red_mask==0x7f00) {
                        /* ==== rgb 555 bmp -> bgr 0888 dib ==== */
                        X11DRV_DIB_Convert_555_to_0888_reverse
                            (dstwidth,lines,
                             srcbits,-bmpImage->bytes_per_line,
                             dstbits,linebytes);
                    } else {
                        goto notsupported;
                    }
                } else if (bmpImage->green_mask==0x07e0) {
                    if (bmpImage->blue_mask==0xf800) {
                        /* ==== bgr 565 bmp -> bgr 0888 dib ==== */
                        X11DRV_DIB_Convert_565_to_0888_asis
                            (dstwidth,lines,
                             srcbits,-bmpImage->bytes_per_line,
                             dstbits,linebytes);
                    } else if (bmpImage->red_mask==0xf800) {
                        /* ==== rgb 565 bmp -> bgr 0888 dib ==== */
                        X11DRV_DIB_Convert_565_to_0888_reverse
                            (dstwidth,lines,
                             srcbits,-bmpImage->bytes_per_line,
                             dstbits,linebytes);
                    } else {
                        goto notsupported;
                    }
                } else {
                    goto notsupported;
                }
            } else {
                if (bmpImage->green_mask==0x03e0 &&
                    (bmpImage->red_mask==0x7f00 ||
                     bmpImage->blue_mask==0x7f00)) {
                    /* ==== rgb or bgr 555 bmp -> any 0888 dib ==== */
                    X11DRV_DIB_Convert_5x5_to_any0888
                        (dstwidth,lines,
                         srcbits,-bmpImage->bytes_per_line,
                         bmpImage->red_mask,bmpImage->green_mask,bmpImage->blue_mask,
                         dstbits,linebytes,
                         rDst,gDst,bDst);
                } else if (bmpImage->green_mask==0x07e0 &&
                           (bmpImage->red_mask==0xf800 ||
                            bmpImage->blue_mask==0xf800)) {
                    /* ==== rgb or bgr 565 bmp -> any 0888 dib ==== */
                    X11DRV_DIB_Convert_5x5_to_any0888
                        (dstwidth,lines,
                         srcbits,-bmpImage->bytes_per_line,
                         bmpImage->red_mask,bmpImage->green_mask,bmpImage->blue_mask,
                         dstbits,linebytes,
                         rDst,gDst,bDst);
                } else {
                    goto notsupported;
                }
            }
        }
        break;

    case 1:
    case 4:
        if (bmpImage->red_mask==0 && bmpImage->green_mask==0 && bmpImage->blue_mask==0 && srccolors) {
            /* ==== pal 1 or 4 bmp -> any 0888 dib ==== */
            int rShift,gShift,bShift;
            DWORD* dstpixel;

            rShift=X11DRV_DIB_MaskToShift(rDst);
            gShift=X11DRV_DIB_MaskToShift(gDst);
            bShift=X11DRV_DIB_MaskToShift(bDst);
            for (h = lines - 1; h >= 0; h--) {
                dstpixel=(DWORD*)dstbits;
                for (x = 0; x < dstwidth; x++) {
                    PALETTEENTRY srcval;
                    srcval = srccolors[XGetPixel(bmpImage, x, h)];
                    *dstpixel++=(srcval.peRed   << rShift) |
                                (srcval.peGreen << gShift) |
                                (srcval.peBlue  << bShift);
                }
                dstbits += linebytes;
            }
        } else {
            goto notsupported;
        }
        break;

    case 8:
        if (bmpImage->red_mask==0 && bmpImage->green_mask==0 && bmpImage->blue_mask==0 && srccolors) {
            /* ==== pal 8 bmp -> any 0888 dib ==== */
            int rShift,gShift,bShift;
            const void* srcbits;
            const BYTE* srcpixel;
            DWORD* dstpixel;

            rShift=X11DRV_DIB_MaskToShift(rDst);
            gShift=X11DRV_DIB_MaskToShift(gDst);
            bShift=X11DRV_DIB_MaskToShift(bDst);
            srcbits=bmpImage->data+(lines-1)*bmpImage->bytes_per_line;
            for (h = lines - 1; h >= 0; h--) {
                srcpixel=srcbits;
                dstpixel=(DWORD*)dstbits;
                for (x = 0; x < dstwidth; x++) {
                    PALETTEENTRY srcval;
                    srcval=srccolors[(int)*srcpixel++];
                    *dstpixel++=(srcval.peRed   << rShift) |
                                (srcval.peGreen << gShift) |
                                (srcval.peBlue  << bShift);
                }
                srcbits -= bmpImage->bytes_per_line;
                dstbits += linebytes;
            }
        } else {
            goto notsupported;
        }
        break;

    default:
    notsupported:
        {
            /* ==== any bmp format -> any 0888 dib ==== */
            int rShift,gShift,bShift;
            DWORD* dstpixel;

            WARN("from unknown %d bit bitmap (%lx,%lx,%lx) to 32 bit DIB (%lx,%lx,%lx)\n",
                  bmpImage->depth, bmpImage->red_mask,
                  bmpImage->green_mask, bmpImage->blue_mask,
                  rDst,gDst,bDst);

            rShift=X11DRV_DIB_MaskToShift(rDst);
            gShift=X11DRV_DIB_MaskToShift(gDst);
            bShift=X11DRV_DIB_MaskToShift(bDst);
            for (h = lines - 1; h >= 0; h--) {
                dstpixel=(DWORD*)dstbits;
                for (x = 0; x < dstwidth; x++) {
                    COLORREF srcval;
                    srcval=X11DRV_PALETTE_ToLogical(XGetPixel(bmpImage, x, h));
                    *dstpixel++=(GetRValue(srcval) << rShift) |
                                (GetGValue(srcval) << gShift) |
                                (GetBValue(srcval) << bShift);
                }
                dstbits += linebytes;
            }
        }
        break;
    }
}

/***********************************************************************
 *           X11DRV_DIB_SetImageBits
 *
 * Transfer the bits to an X image.
 * Helper function for SetDIBits() and SetDIBitsToDevice().
 */
static int X11DRV_DIB_SetImageBits( const X11DRV_DIB_IMAGEBITS_DESCR *descr )
{
    int lines = descr->lines >= 0 ? descr->lines : -descr->lines;
    XImage *bmpImage;

    wine_tsx11_lock();
    if (descr->image)
        bmpImage = descr->image;
    else {
        bmpImage = XCreateImage( gdi_display, visual, descr->depth, ZPixmap, 0, NULL,
				 descr->infoWidth, lines, 32, 0 );
	bmpImage->data = calloc( lines, bmpImage->bytes_per_line );
        if(bmpImage->data == NULL) {
            ERR("Out of memory!\n");
            XDestroyImage( bmpImage );
            wine_tsx11_unlock();
            return lines;
        }
    }

    TRACE("Dib: depth=%d r=%lx g=%lx b=%lx\n",
          descr->infoBpp,descr->rMask,descr->gMask,descr->bMask);
    TRACE("Bmp: depth=%d/%d r=%lx g=%lx b=%lx\n",
          bmpImage->depth,bmpImage->bits_per_pixel,
          bmpImage->red_mask,bmpImage->green_mask,bmpImage->blue_mask);

      /* Transfer the pixels */
    switch(descr->infoBpp)
    {
    case 1:
	X11DRV_DIB_SetImageBits_1( descr->lines, descr->bits, descr->infoWidth,
				   descr->width, descr->xSrc, (int *)(descr->colorMap),
				   bmpImage, descr->dibpitch );
	break;
    case 4:
        if (descr->compression) {
	    XGetSubImage( gdi_display, descr->drawable, descr->xDest, descr->yDest,
			  descr->width, descr->height, AllPlanes, ZPixmap, 
			  bmpImage, descr->xSrc, descr->ySrc );

	    X11DRV_DIB_SetImageBits_RLE4( descr->lines, descr->bits,
					  descr->infoWidth, descr->width,
					  descr->xSrc, (int *)(descr->colorMap),
					  bmpImage );
	} else
	    X11DRV_DIB_SetImageBits_4( descr->lines, descr->bits,
				       descr->infoWidth, descr->width,
				       descr->xSrc, (int*)(descr->colorMap),
				       bmpImage, descr->dibpitch );
	break;
    case 8:
        if (descr->compression) {
	    XGetSubImage( gdi_display, descr->drawable, descr->xDest, descr->yDest,
			  descr->width, descr->height, AllPlanes, ZPixmap, 
			  bmpImage, descr->xSrc, descr->ySrc );
	    X11DRV_DIB_SetImageBits_RLE8( descr->lines, descr->bits,
					  descr->infoWidth, descr->width,
					  descr->xSrc, (int *)(descr->colorMap), 
					  bmpImage );
	} else
	    X11DRV_DIB_SetImageBits_8( descr->lines, descr->bits,
				       descr->infoWidth, descr->width,
				       descr->xSrc, (int *)(descr->colorMap),
				       bmpImage, descr->dibpitch );
	break;
    case 15:
    case 16:
	X11DRV_DIB_SetImageBits_16( descr->lines, descr->bits,
				    descr->infoWidth, descr->width,
                                   descr->xSrc, descr->dc,
                                   descr->rMask, descr->gMask, descr->bMask,
                                   bmpImage, descr->dibpitch);
	break;
    case 24:
	X11DRV_DIB_SetImageBits_24( descr->lines, descr->bits,
				    descr->infoWidth, descr->width,
				    descr->xSrc, descr->dc, 
                                    descr->rMask, descr->gMask, descr->bMask,
				    bmpImage, descr->dibpitch);
	break;
    case 32:
	X11DRV_DIB_SetImageBits_32( descr->lines, descr->bits,
				    descr->infoWidth, descr->width,
                                   descr->xSrc, descr->dc,
                                   descr->rMask, descr->gMask, descr->bMask,
                                   bmpImage, descr->dibpitch);
	break;
    default:
        WARN("(%d): Invalid depth\n", descr->infoBpp );
        break;
    }

    TRACE("XPutImage(%ld,%p,%p,%d,%d,%d,%d,%d,%d)\n",
     descr->drawable, descr->gc, bmpImage,
     descr->xSrc, descr->ySrc, descr->xDest, descr->yDest,
     descr->width, descr->height);
#ifdef HAVE_LIBXXSHM
    if (descr->useShm)
    {
        XShmPutImage( gdi_display, descr->drawable, descr->gc, bmpImage,
                      descr->xSrc, descr->ySrc, descr->xDest, descr->yDest,
                      descr->width, descr->height, FALSE );
        XSync( gdi_display, 0 );
    }
    else
#endif
        XPutImage( gdi_display, descr->drawable, descr->gc, bmpImage,
		   descr->xSrc, descr->ySrc, descr->xDest, descr->yDest,
		   descr->width, descr->height );

    if (!descr->image) XDestroyImage( bmpImage );
    wine_tsx11_unlock();
    return lines;
}

/***********************************************************************
 *           X11DRV_DIB_GetImageBits
 *
 * Transfer the bits from an X image.
 */
static int X11DRV_DIB_GetImageBits( const X11DRV_DIB_IMAGEBITS_DESCR *descr )
{
    int lines = descr->lines >= 0 ? descr->lines : -descr->lines;
    XImage *bmpImage;

    wine_tsx11_lock();
   if (descr->image)
        bmpImage = descr->image;
    else {
        bmpImage = XCreateImage( gdi_display, visual, descr->depth, ZPixmap, 0, NULL,
				 descr->infoWidth, lines, 32, 0 );
	bmpImage->data = calloc( lines, bmpImage->bytes_per_line );
        if(bmpImage->data == NULL) {
            ERR("Out of memory!\n");
            XDestroyImage( bmpImage );
            wine_tsx11_unlock();
            return lines;
        }
    }

    TRACE("XGetSubImage(%ld,%d,%d,%d,%d,%ld,%d,%p,%d,%d)\n",
     descr->drawable, descr->xSrc, descr->ySrc, descr->width,
     lines, AllPlanes, ZPixmap, bmpImage, descr->xDest, descr->yDest);
    XGetSubImage( gdi_display, descr->drawable, descr->xSrc, descr->ySrc,
                  descr->width, lines, AllPlanes, ZPixmap,
                  bmpImage, descr->xDest, descr->yDest );

    TRACE("Dib: depth=%2d r=%lx g=%lx b=%lx\n",
          descr->infoBpp,descr->rMask,descr->gMask,descr->bMask);
    TRACE("Bmp: depth=%2d/%2d r=%lx g=%lx b=%lx\n",
          bmpImage->depth,bmpImage->bits_per_pixel,
          bmpImage->red_mask,bmpImage->green_mask,bmpImage->blue_mask);
      /* Transfer the pixels */
    switch(descr->infoBpp)
    {
    case 1:
          X11DRV_DIB_GetImageBits_1( descr->lines,(LPVOID)descr->bits, 
				     descr->infoWidth, descr->width,
				     descr->colorMap, descr->palentry, 
                                     bmpImage, descr->dibpitch );
       break;

    case 4:
       if (descr->compression)
	   FIXME("Compression not yet supported!\n");
       else
	   X11DRV_DIB_GetImageBits_4( descr->lines,(LPVOID)descr->bits, 
				      descr->infoWidth, descr->width, 
				      descr->colorMap, descr->palentry, 
				      bmpImage, descr->dibpitch );
       break;

    case 8:
       if (descr->compression)
	   FIXME("Compression not yet supported!\n");
       else
	   X11DRV_DIB_GetImageBits_8( descr->lines, (LPVOID)descr->bits,
				      descr->infoWidth, descr->width,
				      descr->colorMap, descr->palentry,
				      bmpImage, descr->dibpitch );
       break;
    case 15:
    case 16:
       X11DRV_DIB_GetImageBits_16( descr->lines, (LPVOID)descr->bits,
				   descr->infoWidth,descr->width,
				   descr->palentry,
				   descr->rMask, descr->gMask, descr->bMask,
				   bmpImage, descr->dibpitch );
       break;

    case 24:
       X11DRV_DIB_GetImageBits_24( descr->lines, (LPVOID)descr->bits,
				   descr->infoWidth,descr->width,
				   descr->palentry,
				   descr->rMask, descr->gMask, descr->bMask,
                                   bmpImage, descr->dibpitch);
       break;

    case 32:
       X11DRV_DIB_GetImageBits_32( descr->lines, (LPVOID)descr->bits,
				   descr->infoWidth, descr->width,
				   descr->palentry,
                                   descr->rMask, descr->gMask, descr->bMask,
                                   bmpImage, descr->dibpitch);
       break;

    default:
        WARN("(%d): Invalid depth\n", descr->infoBpp );
        break;
    }

    if (!descr->image) XDestroyImage( bmpImage );
    wine_tsx11_unlock();
    return lines;
}

/*************************************************************************
 *		X11DRV_SetDIBitsToDevice
 *
 */
INT X11DRV_SetDIBitsToDevice( DC *dc, INT xDest, INT yDest, DWORD cx,
				DWORD cy, INT xSrc, INT ySrc,
				UINT startscan, UINT lines, LPCVOID bits,
				const BITMAPINFO *info, UINT coloruse )
{
    X11DRV_DIB_IMAGEBITS_DESCR descr;
    DWORD width, oldcy = cy;
    INT result;
    int height, tmpheight;
    X11DRV_PDEVICE *physDev = (X11DRV_PDEVICE *)dc->physDev;


    if (DIB_GetBitmapInfo( &info->bmiHeader, &width, &height, 
			   &descr.infoBpp, &descr.compression ) == -1)
        return 0;
    tmpheight = height;
    if (height < 0) height = -height;
    if (!lines || (startscan >= height)) return 0;
    if (startscan + lines > height) lines = height - startscan;
    if (ySrc < startscan) ySrc = startscan;
    else if (ySrc >= startscan + lines) return 0;
    if (xSrc >= width) return 0;
    if (ySrc + cy >= startscan + lines) cy = startscan + lines - ySrc;
    if (xSrc + cx >= width) cx = width - xSrc;
    if (!cx || !cy) return 0;

    X11DRV_SetupGCForText( dc );  /* To have the correct colors */
    TSXSetFunction(gdi_display, physDev->gc, X11DRV_XROPfunction[dc->ROPmode-1]);

    switch (descr.infoBpp)
    {
       case 1:
       case 4:
       case 8:
               descr.colorMap = (RGBQUAD *)X11DRV_DIB_BuildColorMap( 
                                            coloruse == DIB_PAL_COLORS ? dc : NULL, coloruse,
                                            dc->bitsPerPixel, info, &descr.nColorMap );
               if (!descr.colorMap) return 0;
               descr.rMask = descr.gMask = descr.bMask = 0;
               break;
       case 15:
       case 16:
               descr.rMask = (descr.compression == BI_BITFIELDS) ? *(DWORD *)info->bmiColors : 0x7c00;
               descr.gMask = (descr.compression == BI_BITFIELDS) ?  *((DWORD *)info->bmiColors + 1) : 0x03e0;
               descr.bMask = (descr.compression == BI_BITFIELDS) ?  *((DWORD *)info->bmiColors + 2) : 0x001f;
               descr.colorMap = 0;
               break;

       case 24:
       case 32:
               descr.rMask = (descr.compression == BI_BITFIELDS) ? *(DWORD *)info->bmiColors       : 0xff0000;
               descr.gMask = (descr.compression == BI_BITFIELDS) ? *((DWORD *)info->bmiColors + 1) : 0x00ff00;
               descr.bMask = (descr.compression == BI_BITFIELDS) ? *((DWORD *)info->bmiColors + 2) : 0x0000ff;
               descr.colorMap = 0;
               break;
    }

    descr.dc        = dc;
    descr.bits      = bits;
    descr.image     = NULL;
    descr.palentry  = NULL;
    descr.lines     = tmpheight >= 0 ? lines : -lines;
    descr.infoWidth = width;
    descr.depth     = dc->bitsPerPixel;
    descr.drawable  = physDev->drawable;
    descr.gc        = physDev->gc;
    descr.xSrc      = xSrc;
    descr.ySrc      = tmpheight >= 0 ? lines-(ySrc-startscan)-cy+(oldcy-cy) 
                                     : ySrc - startscan;
    descr.xDest     = dc->DCOrgX + XLPTODP( dc, xDest );
    descr.yDest     = dc->DCOrgY + YLPTODP( dc, yDest ) +
                                     (tmpheight >= 0 ? oldcy-cy : 0);
    descr.width     = cx;
    descr.height    = cy;
    descr.useShm    = FALSE;
    descr.dibpitch  = ((width * descr.infoBpp + 31) &~31) / 8;

    result = X11DRV_DIB_SetImageBits( &descr );

    if (descr.infoBpp <= 8)
       HeapFree(GetProcessHeap(), 0, descr.colorMap);
    return result;
}

/***********************************************************************
 *           X11DRV_DIB_SetDIBits
 */
INT X11DRV_DIB_SetDIBits(
  BITMAPOBJ *bmp, DC *dc, UINT startscan,
  UINT lines, LPCVOID bits, const BITMAPINFO *info,
  UINT coloruse, HBITMAP hbitmap)
{
  X11DRV_DIB_IMAGEBITS_DESCR descr;
  int height, tmpheight;
  INT result;

  descr.dc = dc;

  if (DIB_GetBitmapInfo( &info->bmiHeader, &descr.infoWidth, &height,
			 &descr.infoBpp, &descr.compression ) == -1)
      return 0;

  tmpheight = height;
  if (height < 0) height = -height;
  if (!lines || (startscan >= height))
      return 0;

  if (startscan + lines > height) lines = height - startscan;

  switch (descr.infoBpp)
  {
       case 1:
       case 4:
       case 8:
	       descr.colorMap = (RGBQUAD *)X11DRV_DIB_BuildColorMap(
                        coloruse == DIB_PAL_COLORS ? descr.dc : NULL, coloruse,
                                                          bmp->bitmap.bmBitsPixel,
                                                          info, &descr.nColorMap );
               if (!descr.colorMap) return 0;
               descr.rMask = descr.gMask = descr.bMask = 0;
               break;
       case 15:
       case 16:
               descr.rMask = (descr.compression == BI_BITFIELDS) ? *(DWORD *)info->bmiColors : 0x7c00;
               descr.gMask = (descr.compression == BI_BITFIELDS) ? *((DWORD *)info->bmiColors + 1) : 0x03e0;
               descr.bMask = (descr.compression == BI_BITFIELDS) ? *((DWORD *)info->bmiColors + 2) : 0x001f;
               descr.colorMap = 0;
               break;

       case 24:
       case 32:
               descr.rMask = (descr.compression == BI_BITFIELDS) ? *(DWORD *)info->bmiColors       : 0xff0000;
               descr.gMask = (descr.compression == BI_BITFIELDS) ? *((DWORD *)info->bmiColors + 1) : 0x00ff00;
               descr.bMask = (descr.compression == BI_BITFIELDS) ? *((DWORD *)info->bmiColors + 2) : 0x0000ff;
               descr.colorMap = 0;
               break;

       default: break;
  }

  /* HACK for now */
  if(!bmp->physBitmap)
    X11DRV_CreateBitmap(hbitmap);

  descr.bits      = bits;
  descr.image     = NULL;
  descr.palentry  = NULL;
  descr.lines     = tmpheight >= 0 ? lines : -lines;
  descr.depth     = bmp->bitmap.bmBitsPixel;
  descr.drawable  = (Pixmap)bmp->physBitmap;
  descr.gc        = BITMAP_GC(bmp);
  descr.xSrc      = 0;
  descr.ySrc      = 0;
  descr.xDest     = 0;
  descr.yDest     = height - startscan - lines;
  descr.width     = bmp->bitmap.bmWidth;
  descr.height    = lines;
  descr.useShm    = FALSE;
  descr.dibpitch  = ((descr.infoWidth * descr.infoBpp + 31) &~31) / 8;
  result = X11DRV_DIB_SetImageBits( &descr );

  if (descr.colorMap) HeapFree(GetProcessHeap(), 0, descr.colorMap);

  return result;
}

/***********************************************************************
 *           X11DRV_DIB_GetDIBits
 */
INT X11DRV_DIB_GetDIBits(
  BITMAPOBJ *bmp, DC *dc, UINT startscan, 
  UINT lines, LPVOID bits, BITMAPINFO *info,
  UINT coloruse, HBITMAP hbitmap)
{
  X11DRV_DIBSECTION *dib = (X11DRV_DIBSECTION *) bmp->dib;
  X11DRV_DIB_IMAGEBITS_DESCR descr;
  PALETTEOBJ * palette;
  int height;
  
  TRACE("%u scanlines of (%i,%i) -> (%i,%i) starting from %u\n",
	lines, bmp->bitmap.bmWidth, bmp->bitmap.bmHeight,
	(int)info->bmiHeader.biWidth, (int)info->bmiHeader.biHeight,
        startscan );

  if (!(palette = (PALETTEOBJ*)GDI_GetObjPtr( dc->hPalette, PALETTE_MAGIC )))
      return 0;

  if( lines > bmp->bitmap.bmHeight ) lines = bmp->bitmap.bmHeight;

  height = info->bmiHeader.biHeight;
  if (height < 0) height = -height;
  if( lines > height ) lines = height;
  /* Top-down images have a negative biHeight, the scanlines of theses images
   * were inverted in X11DRV_DIB_GetImageBits_xx
   * To prevent this we simply change the sign of lines
   * (the number of scan lines to copy).
   * Negative lines are correctly handled by X11DRV_DIB_GetImageBits_xx.
   */
  if( info->bmiHeader.biHeight < 0 && lines > 0) lines = -lines;

  if( startscan >= bmp->bitmap.bmHeight )
  {
      lines = 0;
      goto done;
  }
  
  if (DIB_GetBitmapInfo( &info->bmiHeader, &descr.infoWidth, &descr.lines,
                        &descr.infoBpp, &descr.compression ) == -1)
  {
      lines = 0;
      goto done;
  }

  switch (descr.infoBpp)
  {
      case 1:
      case 4:
      case 8:
          descr.rMask= descr.gMask = descr.bMask = 0;
          break;
      case 15:
      case 16:
          descr.rMask = (descr.compression == BI_BITFIELDS) ? *(DWORD *)info->bmiColors : 0x7c00;
          descr.gMask = (descr.compression == BI_BITFIELDS) ?  *((DWORD *)info->bmiColors + 1) : 0x03e0;
          descr.bMask = (descr.compression == BI_BITFIELDS) ?  *((DWORD *)info->bmiColors + 2) : 0x001f;
          break;
      case 24:
      case 32:
          descr.rMask = (descr.compression == BI_BITFIELDS) ? *(DWORD *)info->bmiColors : 0xff0000;
          descr.gMask = (descr.compression == BI_BITFIELDS) ?  *((DWORD *)info->bmiColors + 1) : 0x00ff00;
          descr.bMask = (descr.compression == BI_BITFIELDS) ?  *((DWORD *)info->bmiColors + 2) : 0x0000ff;
          break;
  }

  /* Hack for now */
  if(!bmp->physBitmap)
    X11DRV_CreateBitmap(hbitmap);


  descr.dc        = dc;
  descr.palentry  = palette->logpalette.palPalEntry;
  descr.bits      = bits;
  descr.image     = NULL;
  descr.lines     = lines;
  descr.depth     = bmp->bitmap.bmBitsPixel;
  descr.drawable  = (Pixmap)bmp->physBitmap;
  descr.gc        = BITMAP_GC(bmp);
  descr.width     = bmp->bitmap.bmWidth;
  descr.height    = bmp->bitmap.bmHeight;
  descr.colorMap  = info->bmiColors;
  descr.xDest     = 0;
  descr.yDest     = 0;
  descr.xSrc      = 0;
  
  if (descr.lines > 0)
  {
     descr.ySrc = (descr.height-1) - (startscan + (lines-1));
  }
  else
  {
     descr.ySrc = startscan;
  }
#ifdef HAVE_LIBXXSHM
  descr.useShm = dib ? (dib->shminfo.shmid != -1) : FALSE;
#else
  descr.useShm = FALSE;
#endif
  descr.dibpitch = dib ? (dib->dibSection.dsBm.bmWidthBytes)
		       : (((descr.infoWidth * descr.infoBpp + 31) &~31) / 8);

  X11DRV_DIB_GetImageBits( &descr );

  if(info->bmiHeader.biSizeImage == 0) /* Fill in biSizeImage */
      info->bmiHeader.biSizeImage = DIB_GetDIBImageBytes(
					 info->bmiHeader.biWidth,
					 info->bmiHeader.biHeight,
					 info->bmiHeader.biBitCount );

  info->bmiHeader.biCompression = 0;
  if (descr.compression == BI_BITFIELDS)
  {
    *(DWORD *)info->bmiColors = descr.rMask;
    *((DWORD *)info->bmiColors+1) = descr.gMask;
    *((DWORD *)info->bmiColors+2) = descr.bMask;
  }

done:
  GDI_ReleaseObj( dc->hPalette );
 
  return lines;
}

/***********************************************************************
 *           DIB_DoProtectDIBSection
 */
static void X11DRV_DIB_DoProtectDIBSection( BITMAPOBJ *bmp, DWORD new_prot )
{
    DIBSECTION *dib = bmp->dib;
    INT effHeight = dib->dsBm.bmHeight >= 0? dib->dsBm.bmHeight
                                             : -dib->dsBm.bmHeight;
    /* use the biSizeImage data as the memory size only if we're dealing with a
       compressed image where the value is set.  Otherwise, calculate based on
       width * height */
    INT totalSize = dib->dsBmih.biSizeImage && dib->dsBmih.biCompression != BI_RGB
                         ? dib->dsBmih.biSizeImage
                         : dib->dsBm.bmWidthBytes * effHeight;
    DWORD old_prot;

    VirtualProtect(dib->dsBm.bmBits, totalSize, new_prot, &old_prot);
    TRACE("Changed protection from %ld to %ld\n", old_prot, new_prot);
}

/***********************************************************************
 *           X11DRV_DIB_DoUpdateDIBSection
 */
static void X11DRV_DIB_DoCopyDIBSection(BITMAPOBJ *bmp, BOOL toDIB,
					void *colorMap, int nColorMap,
					Drawable dest,
					DWORD xSrc, DWORD ySrc,
					DWORD xDest, DWORD yDest,
					DWORD width, DWORD height)
{
  X11DRV_DIBSECTION *dib = (X11DRV_DIBSECTION *) bmp->dib;
  X11DRV_DIB_IMAGEBITS_DESCR descr;
  
  if (DIB_GetBitmapInfo( &dib->dibSection.dsBmih, &descr.infoWidth, &descr.lines,
			 &descr.infoBpp, &descr.compression ) == -1)
    return;

  descr.dc        = NULL;
  descr.palentry  = NULL;
  descr.image     = dib->image;
  descr.colorMap  = colorMap;
  descr.nColorMap = nColorMap;
  descr.bits      = dib->dibSection.dsBm.bmBits;
  descr.depth     = bmp->bitmap.bmBitsPixel;

  switch (descr.infoBpp)
  {
    case 1:
    case 4:
    case 8:
      descr.rMask = descr.gMask = descr.bMask = 0;
      break;
    case 15:
    case 16:
      descr.rMask = (descr.compression == BI_BITFIELDS) ? dib->dibSection.dsBitfields[0] : 0x7c00;
      descr.gMask = (descr.compression == BI_BITFIELDS) ? dib->dibSection.dsBitfields[1] : 0x03e0;
      descr.bMask = (descr.compression == BI_BITFIELDS) ? dib->dibSection.dsBitfields[2] : 0x001f;
      break;

    case 24:
    case 32:
      descr.rMask = (descr.compression == BI_BITFIELDS) ? dib->dibSection.dsBitfields[0] : 0xff0000;
      descr.gMask = (descr.compression == BI_BITFIELDS) ? dib->dibSection.dsBitfields[1] : 0x00ff00;
      descr.bMask = (descr.compression == BI_BITFIELDS) ? dib->dibSection.dsBitfields[2] : 0x0000ff;
      break;
  }

  /* Hack for now */
  descr.drawable  = dest;
  descr.gc        = BITMAP_GC(bmp);
  descr.xSrc      = xSrc;
  descr.ySrc      = ySrc;
  descr.xDest     = xDest;
  descr.yDest     = yDest;
  descr.width     = width;
  descr.height    = height;
#ifdef HAVE_LIBXXSHM
  descr.useShm = (dib->shminfo.shmid != -1);
#else
  descr.useShm = FALSE;
#endif
  descr.dibpitch = dib->dibSection.dsBm.bmWidthBytes;

  if (toDIB)
    {
      TRACE("Copying from Pixmap to DIB bits\n");
      X11DRV_DIB_GetImageBits( &descr );
    }
  else
    {
      TRACE("Copying from DIB bits to Pixmap\n"); 
      X11DRV_DIB_SetImageBits( &descr );
    }
}

/***********************************************************************
 *           X11DRV_DIB_CopyDIBSection
 */
void X11DRV_DIB_CopyDIBSection(DC *dcSrc, DC *dcDst,
			       DWORD xSrc, DWORD ySrc,
			       DWORD xDest, DWORD yDest,
			       DWORD width, DWORD height)
{
  BITMAPOBJ *bmp;
  X11DRV_PDEVICE *physDev = (X11DRV_PDEVICE *)dcDst->physDev;
  int nColorMap = 0, *colorMap = NULL, aColorMap = FALSE;

  TRACE("(%p,%p,%ld,%ld,%ld,%ld,%ld,%ld)\n", dcSrc, dcDst,
    xSrc, ySrc, xDest, yDest, width, height);
  /* this function is meant as an optimization for BitBlt,
   * not to be called otherwise */
  if (!(dcSrc->flags & DC_MEMORY)) {
    ERR("called for non-memory source DC!?\n");
    return;
  }

  bmp = (BITMAPOBJ *)GDI_GetObjPtr( dcSrc->hBitmap, BITMAP_MAGIC );
  if (!(bmp && bmp->dib)) {
    ERR("called for non-DIBSection!?\n");
    GDI_ReleaseObj( dcSrc->hBitmap );
    return;
  }
  /* while BitBlt should already have made sure we only get
   * positive values, we should check for oversize values */
  if ((xSrc < bmp->bitmap.bmWidth) &&
      (ySrc < bmp->bitmap.bmHeight)) {
    if (xSrc + width > bmp->bitmap.bmWidth)
      width = bmp->bitmap.bmWidth - xSrc;
    if (ySrc + height > bmp->bitmap.bmHeight)
      height = bmp->bitmap.bmHeight - ySrc;
    /* if the source bitmap is 8bpp or less, we're supposed to use the
     * DC's palette for color conversion (not the DIB color table) */
    if (bmp->dib->dsBm.bmBitsPixel <= 8) {
      X11DRV_DIBSECTION *dib = (X11DRV_DIBSECTION *) bmp->dib;
      if ((!dcSrc->hPalette) ||
	  (dcSrc->hPalette == GetStockObject(DEFAULT_PALETTE))) {
	/* HACK: no palette has been set in the source DC,
	 * use the DIB colormap instead - this is necessary in some
	 * cases since we need to do depth conversion in some places
	 * where real Windows can just copy data straight over */
	colorMap = dib->colorMap;
	nColorMap = dib->nColorMap;
      } else {
	colorMap = X11DRV_DIB_BuildColorMap( dcSrc, (WORD)-1,
					     bmp->dib->dsBm.bmBitsPixel,
					     (BITMAPINFO*)&(bmp->dib->dsBmih),
					     &nColorMap );
	if (colorMap) aColorMap = TRUE;
      }
    }
    /* perform the copy */
    X11DRV_DIB_DoCopyDIBSection(bmp, FALSE, colorMap, nColorMap,
				physDev->drawable, xSrc, ySrc, xDest, yDest,
				width, height);
    /* free color mapping */
    if (aColorMap)
      HeapFree(GetProcessHeap(), 0, colorMap);
  }
  GDI_ReleaseObj( dcSrc->hBitmap );
}

/***********************************************************************
 *           X11DRV_DIB_DoUpdateDIBSection
 */
static void X11DRV_DIB_DoUpdateDIBSection(BITMAPOBJ *bmp, BOOL toDIB)
{
  X11DRV_DIBSECTION *dib = (X11DRV_DIBSECTION *) bmp->dib;
  X11DRV_DIB_DoCopyDIBSection(bmp, toDIB, dib->colorMap, dib->nColorMap,
			      (Drawable)bmp->physBitmap, 0, 0, 0, 0,
			      bmp->bitmap.bmWidth, bmp->bitmap.bmHeight);
}

/***********************************************************************
 *           X11DRV_DIB_FaultHandler
 */
static BOOL X11DRV_DIB_FaultHandler( LPVOID res, LPCVOID addr )
{
  BITMAPOBJ *bmp;
  INT state;
  
  bmp = (BITMAPOBJ *)GDI_GetObjPtr( (HBITMAP)res, BITMAP_MAGIC );
  if (!bmp) return FALSE;

  state = X11DRV_DIB_Lock(bmp, DIB_Status_None, FALSE);
  if (state != DIB_Status_InSync) {
    /* no way to tell whether app needs read or write yet,
     * try read first */
    X11DRV_DIB_Coerce(bmp, DIB_Status_InSync, FALSE);
  } else {
    /* hm, apparently the app must have write access */
    X11DRV_DIB_Coerce(bmp, DIB_Status_AppMod, FALSE);
  }
  X11DRV_DIB_Unlock(bmp, TRUE);

  GDI_ReleaseObj( (HBITMAP)res );
  return TRUE;
}

/***********************************************************************
 *           X11DRV_DIB_Coerce
 */
INT X11DRV_DIB_Coerce(BITMAPOBJ *bmp, INT req, BOOL lossy)
{
  X11DRV_DIBSECTION *dib = (X11DRV_DIBSECTION *) bmp->dib;
  INT ret = DIB_Status_None;

  if (dib) {
    EnterCriticalSection(&(dib->lock));
    ret = dib->status;
    switch (req) {
    case DIB_Status_GdiMod:
      /* GDI access - request to draw on pixmap */
      switch (dib->status)
      {
        default:
        case DIB_Status_None:
	  dib->p_status = DIB_Status_GdiMod;
	  X11DRV_DIB_DoUpdateDIBSection( bmp, FALSE );
	  break;

        case DIB_Status_GdiMod:
	  TRACE("GdiMod requested in status GdiMod\n" );
	  break;

        case DIB_Status_InSync:
	  TRACE("GdiMod requested in status InSync\n" );
	  X11DRV_DIB_DoProtectDIBSection( bmp, PAGE_NOACCESS );
	  dib->status = DIB_Status_GdiMod;
	  dib->p_status = DIB_Status_InSync;
	  break;

	case DIB_Status_AuxMod:
	  TRACE("GdiMod requested in status AuxMod\n" );
	  if (lossy) dib->status = DIB_Status_GdiMod;
	  else (*dib->copy_aux)(dib->aux_ctx, DIB_Status_GdiMod);
	  dib->p_status = DIB_Status_AuxMod;
	  if (dib->status != DIB_Status_AppMod) {
	    X11DRV_DIB_DoProtectDIBSection( bmp, PAGE_NOACCESS );
	    break;
	  }
	  /* fall through if copy_aux() had to change to AppMod state */

        case DIB_Status_AppMod:
	  TRACE("GdiMod requested in status AppMod\n" );
	  if (!lossy) {
	    /* make it readonly to avoid app changing data while we copy */
	    X11DRV_DIB_DoProtectDIBSection( bmp, PAGE_READONLY );
	    X11DRV_DIB_DoUpdateDIBSection( bmp, FALSE );
	  }
	  X11DRV_DIB_DoProtectDIBSection( bmp, PAGE_NOACCESS );
	  dib->p_status = DIB_Status_AppMod;
	  dib->status = DIB_Status_GdiMod;
	  break;
      }
      break;

    case DIB_Status_InSync:
      /* App access - request access to read DIB surface */
      /* (typically called from signal handler) */
      switch (dib->status)
      {
        default:
        case DIB_Status_None:
	  /* shouldn't happen from signal handler */
	  break;

	case DIB_Status_AuxMod:
	  TRACE("InSync requested in status AuxMod\n" );
	  if (lossy) dib->status = DIB_Status_InSync;
	  else {
	    X11DRV_DIB_DoProtectDIBSection( bmp, PAGE_READWRITE );
	    (*dib->copy_aux)(dib->aux_ctx, DIB_Status_InSync);
	  }
	  if (dib->status != DIB_Status_GdiMod) {
	    X11DRV_DIB_DoProtectDIBSection( bmp, PAGE_READONLY );
	    break;
	  }
	  /* fall through if copy_aux() had to change to GdiMod state */

	case DIB_Status_GdiMod:
	  TRACE("InSync requested in status GdiMod\n" );
	  if (!lossy) {
	    X11DRV_DIB_DoProtectDIBSection( bmp, PAGE_READWRITE );
	    X11DRV_DIB_DoUpdateDIBSection( bmp, TRUE );
	  }
	  X11DRV_DIB_DoProtectDIBSection( bmp, PAGE_READONLY );
	  dib->status = DIB_Status_InSync;
	  break;

        case DIB_Status_InSync:
	  TRACE("InSync requested in status InSync\n" );
	  /* shouldn't happen from signal handler */
	  break;

        case DIB_Status_AppMod:
	  TRACE("InSync requested in status AppMod\n" );
	  /* no reason to do anything here, and this
	   * shouldn't happen from signal handler */
	  break;
      }
      break;

    case DIB_Status_AppMod:
      /* App access - request access to write DIB surface */
      /* (typically called from signal handler) */
      switch (dib->status)
      {
        default:
        case DIB_Status_None:
	  /* shouldn't happen from signal handler */
	  break;

	case DIB_Status_AuxMod:
	  TRACE("AppMod requested in status AuxMod\n" );
	  X11DRV_DIB_DoProtectDIBSection( bmp, PAGE_READWRITE );
	  if (lossy) dib->status = DIB_Status_AppMod;
	  else (*dib->copy_aux)(dib->aux_ctx, DIB_Status_AppMod);
	  if (dib->status != DIB_Status_GdiMod)
	    break;
	  /* fall through if copy_aux() had to change to GdiMod state */

	case DIB_Status_GdiMod:
	  TRACE("AppMod requested in status GdiMod\n" );
	  X11DRV_DIB_DoProtectDIBSection( bmp, PAGE_READWRITE );
	  if (!lossy) X11DRV_DIB_DoUpdateDIBSection( bmp, TRUE );
	  dib->status = DIB_Status_AppMod;
	  break;

        case DIB_Status_InSync:
	  TRACE("AppMod requested in status InSync\n" );
	  X11DRV_DIB_DoProtectDIBSection( bmp, PAGE_READWRITE );
	  dib->status = DIB_Status_AppMod;
	  break;

        case DIB_Status_AppMod:
	  TRACE("AppMod requested in status AppMod\n" );
	  /* shouldn't happen from signal handler */
	  break;
      }
      break;

    case DIB_Status_AuxMod:
      if (dib->status == DIB_Status_None) {
	dib->p_status = req;
      } else {
	if (dib->status != DIB_Status_AuxMod)
	  dib->p_status = dib->status;
	dib->status = DIB_Status_AuxMod;
      }
      break;
      /* it is up to the caller to do the copy/conversion, probably
       * using the return value to decide where to copy from */
    }
    LeaveCriticalSection(&(dib->lock));
  }
  return ret;
}

/***********************************************************************
 *           X11DRV_DIB_Lock
 */
INT X11DRV_DIB_Lock(BITMAPOBJ *bmp, INT req, BOOL lossy)
{
  X11DRV_DIBSECTION *dib = (X11DRV_DIBSECTION *) bmp->dib;
  INT ret = DIB_Status_None;

  if (dib) {
    TRACE("Locking %p from thread %08lx\n", bmp, GetCurrentThreadId());
    EnterCriticalSection(&(dib->lock));
    ret = dib->status;
    if (req != DIB_Status_None)
      X11DRV_DIB_Coerce(bmp, req, lossy);
  }
  return ret;
}

/***********************************************************************
 *           X11DRV_DIB_Unlock
 */
void X11DRV_DIB_Unlock(BITMAPOBJ *bmp, BOOL commit)
{
  X11DRV_DIBSECTION *dib = (X11DRV_DIBSECTION *) bmp->dib;

  if (dib) {
    switch (dib->status)
    {
      default:
      case DIB_Status_None:
	/* in case anyone is wondering, this is the "signal handler doesn't
	 * work" case, where we always have to be ready for app access */
	if (commit) {
	  switch (dib->p_status)
	  {
	    case DIB_Status_AuxMod:
	      TRACE("Unlocking and syncing from AuxMod\n" );
	      (*dib->copy_aux)(dib->aux_ctx, DIB_Status_AppMod);
	      if (dib->status != DIB_Status_None) {
		dib->p_status = dib->status;
		dib->status = DIB_Status_None;
	      }
	      if (dib->p_status != DIB_Status_GdiMod)
		break;
	      /* fall through if copy_aux() had to change to GdiMod state */

	    case DIB_Status_GdiMod:
	      TRACE("Unlocking and syncing from GdiMod\n" );
	      X11DRV_DIB_DoUpdateDIBSection( bmp, TRUE );
	      break;

	    default:
	      TRACE("Unlocking without needing to sync\n" );
	      break;
	  }
	}
	else TRACE("Unlocking with no changes\n");
	dib->p_status = DIB_Status_None;
	break;

      case DIB_Status_GdiMod:
	TRACE("Unlocking in status GdiMod\n" );
	/* DIB was protected in Coerce */
	if (!commit) {
	  /* no commit, revert to InSync if applicable */
	  if ((dib->p_status == DIB_Status_InSync) ||
	      (dib->p_status == DIB_Status_AppMod)) {
	    X11DRV_DIB_DoProtectDIBSection( bmp, PAGE_READONLY );
	    dib->status = DIB_Status_InSync;
	  }
	}
	break;

      case DIB_Status_InSync:
	TRACE("Unlocking in status InSync\n" );
	/* DIB was already protected in Coerce */
	break;

      case DIB_Status_AppMod:
	TRACE("Unlocking in status AppMod\n" );
	/* DIB was already protected in Coerce */
	/* this case is ordinary only called from the signal handler,
	 * so we don't bother to check for !commit */
	break;

      case DIB_Status_AuxMod:
	TRACE("Unlocking in status AuxMod\n" );
	if (commit) {
	  /* DIB may need protection now */
	  if ((dib->p_status == DIB_Status_InSync) ||
	      (dib->p_status == DIB_Status_AppMod))
	    X11DRV_DIB_DoProtectDIBSection( bmp, PAGE_NOACCESS );
	} else {
	  /* no commit, revert to previous state */
	  if (dib->p_status != DIB_Status_None)
	    dib->status = dib->p_status;
	  /* no protections changed */
	}
	dib->p_status = DIB_Status_None;
	break;
    }
    LeaveCriticalSection(&(dib->lock));
    TRACE("Unlocked %p\n", bmp);
  }
}

/***********************************************************************
 *           X11DRV_CoerceDIBSection2
 */
INT X11DRV_CoerceDIBSection2(HBITMAP hBmp, INT req, BOOL lossy)
{
  BITMAPOBJ *bmp;
  INT ret;

  bmp = (BITMAPOBJ *)GDI_GetObjPtr( hBmp, BITMAP_MAGIC );
  if (!bmp) return DIB_Status_None;
  ret = X11DRV_DIB_Coerce(bmp, req, lossy);
  GDI_ReleaseObj( hBmp );
  return ret;
}

/***********************************************************************
 *           X11DRV_LockDIBSection2
 */
INT X11DRV_LockDIBSection2(HBITMAP hBmp, INT req, BOOL lossy)
{
  BITMAPOBJ *bmp;
  INT ret;

  bmp = (BITMAPOBJ *)GDI_GetObjPtr( hBmp, BITMAP_MAGIC );
  if (!bmp) return DIB_Status_None;
  ret = X11DRV_DIB_Lock(bmp, req, lossy);
  GDI_ReleaseObj( hBmp );
  return ret;
}

/***********************************************************************
 *           X11DRV_UnlockDIBSection2
 */
void X11DRV_UnlockDIBSection2(HBITMAP hBmp, BOOL commit)
{
  BITMAPOBJ *bmp;

  bmp = (BITMAPOBJ *)GDI_GetObjPtr( hBmp, BITMAP_MAGIC );
  if (!bmp) return;
  X11DRV_DIB_Unlock(bmp, commit);
  GDI_ReleaseObj( hBmp );
}

/***********************************************************************
 *           X11DRV_CoerceDIBSection
 */
INT X11DRV_CoerceDIBSection(DC *dc, INT req, BOOL lossy)
{
  if (!dc) return DIB_Status_None;
  return X11DRV_CoerceDIBSection2( dc->hBitmap, req, lossy );
}

/***********************************************************************
 *           X11DRV_LockDIBSection
 */
INT X11DRV_LockDIBSection(DC *dc, INT req, BOOL lossy)
{
  if (!dc) return DIB_Status_None;
  if (!(dc->flags & DC_MEMORY)) return DIB_Status_None;

  return X11DRV_LockDIBSection2( dc->hBitmap, req, lossy );
}

/***********************************************************************
 *           X11DRV_UnlockDIBSection
 */
void X11DRV_UnlockDIBSection(DC *dc, BOOL commit)
{
  if (!dc) return;
  if (!(dc->flags & DC_MEMORY)) return;

  X11DRV_UnlockDIBSection2( dc->hBitmap, commit );
}


#ifdef HAVE_LIBXXSHM
/***********************************************************************
 *           X11DRV_XShmErrorHandler
 *
 */
static int XShmErrorHandler(Display *dpy, XErrorEvent *event) 
{
    XShmErrorFlag = 1;
    return 0;
}

/***********************************************************************
 *           X11DRV_XShmCreateImage
 *
 */
static XImage *X11DRV_XShmCreateImage( int width, int height, int bpp,
                                       XShmSegmentInfo* shminfo)
{
    int (*WineXHandler)(Display *, XErrorEvent *);
    XImage *image;

    wine_tsx11_lock();
    image = XShmCreateImage(gdi_display, visual, bpp, ZPixmap, NULL, shminfo, width, height);
    if (image)
    {
        shminfo->shmid = shmget(IPC_PRIVATE, image->bytes_per_line * height,
                                  IPC_CREAT|0700);
        if( shminfo->shmid != -1 )
        {
            shminfo->shmaddr = image->data = shmat(shminfo->shmid, 0, 0);
            if( shminfo->shmaddr != (char*)-1 )
            {
                shminfo->readOnly = FALSE;
                if( XShmAttach( gdi_display, shminfo ) != 0)
                {
                    /* Reset the error flag */
                    XShmErrorFlag = 0;
                    WineXHandler = XSetErrorHandler(XShmErrorHandler);
                    XSync( gdi_display, 0 );

                    if (!XShmErrorFlag)
                    {
			shmctl(shminfo->shmid, IPC_RMID, 0);

                        XSetErrorHandler(WineXHandler);
                        wine_tsx11_unlock();
                        return image; /* Success! */
                    }
                    /* An error occured */
                    XShmErrorFlag = 0;
                    XSetErrorHandler(WineXHandler);
                }
                shmdt(shminfo->shmaddr);
            }
            shmctl(shminfo->shmid, IPC_RMID, 0);
        }
        XFlush(gdi_display);
        XDestroyImage(image);
        image = NULL;
    }
    wine_tsx11_unlock();
    return image;
}
#endif /* HAVE_LIBXXSHM */


/***********************************************************************
 *           X11DRV_DIB_CreateDIBSection
 */
HBITMAP X11DRV_DIB_CreateDIBSection(
  DC *dc, BITMAPINFO *bmi, UINT usage,
  LPVOID *bits, HANDLE section,
  DWORD offset, DWORD ovr_pitch)
{
  HBITMAP res = 0;
  BITMAPOBJ *bmp = NULL;
  X11DRV_DIBSECTION *dib = NULL;
  int *colorMap = NULL;
  int nColorMap;
  
  /* Fill BITMAP32 structure with DIB data */
  BITMAPINFOHEADER *bi = &bmi->bmiHeader;
  INT effHeight, totalSize;
  BITMAP bm;
  LPVOID mapBits = NULL;
  
  TRACE("format (%ld,%ld), planes %d, bpp %d, size %ld, colors %ld (%s)\n",
	bi->biWidth, bi->biHeight, bi->biPlanes, bi->biBitCount,
	bi->biSizeImage, bi->biClrUsed, usage == DIB_PAL_COLORS? "PAL" : "RGB");
  
  effHeight = bi->biHeight >= 0 ? bi->biHeight : -bi->biHeight;
  bm.bmType = 0;
  bm.bmWidth = bi->biWidth;
  bm.bmHeight = effHeight;
  bm.bmWidthBytes = ovr_pitch ? ovr_pitch
			      : DIB_GetDIBWidthBytes(bm.bmWidth, bi->biBitCount);
  bm.bmPlanes = bi->biPlanes;
  bm.bmBitsPixel = bi->biBitCount;
  bm.bmBits = NULL;
  
  /* Get storage location for DIB bits.  Only use biSizeImage if it's valid and
     we're dealing with a compressed bitmap.  Otherwise, use width * height. */
  totalSize = bi->biSizeImage && bi->biCompression != BI_RGB
    ? bi->biSizeImage : bm.bmWidthBytes * effHeight;
  
  if (section)
  {
      SYSTEM_INFO SystemInfo;
      DWORD mapOffset;
      INT mapSize;

      GetSystemInfo( &SystemInfo );
      mapOffset = offset - (offset % SystemInfo.dwAllocationGranularity);
      mapSize = totalSize + (offset - mapOffset);
      mapBits = MapViewOfFile( section,
			       FILE_MAP_ALL_ACCESS, 
			       0L,
			       mapOffset,
			       mapSize );
      bm.bmBits = (char *)mapBits + (offset - mapOffset);
  }
  else if (ovr_pitch && offset)
    bm.bmBits = (LPVOID) offset;
  else {
    offset = 0;
    bm.bmBits = VirtualAlloc(NULL, totalSize, 
			     MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
  }
  
  /* Create Color Map */
  if (bm.bmBits && bm.bmBitsPixel <= 8)
      colorMap = X11DRV_DIB_BuildColorMap( usage == DIB_PAL_COLORS? dc : NULL, 
				usage, bm.bmBitsPixel, bmi, &nColorMap );

  /* Allocate Memory for DIB and fill structure */
  if (bm.bmBits)
    dib = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(X11DRV_DIBSECTION));
  if (dib)
    {
      dib->dibSection.dsBm = bm;
      dib->dibSection.dsBmih = *bi;
      dib->dibSection.dsBmih.biSizeImage = totalSize;

      /* Set dsBitfields values */
       if ( usage == DIB_PAL_COLORS || bi->biBitCount <= 8)
       {
           dib->dibSection.dsBitfields[0] = dib->dibSection.dsBitfields[1] = dib->dibSection.dsBitfields[2] = 0;
       }
       else switch( bi->biBitCount )
       {
           case 15:
           case 16:
               dib->dibSection.dsBitfields[0] = (bi->biCompression == BI_BITFIELDS) ? *(DWORD *)bmi->bmiColors : 0x7c00;
               dib->dibSection.dsBitfields[1] = (bi->biCompression == BI_BITFIELDS) ? *((DWORD *)bmi->bmiColors + 1) : 0x03e0;
               dib->dibSection.dsBitfields[2] = (bi->biCompression == BI_BITFIELDS) ? *((DWORD *)bmi->bmiColors + 2) : 0x001f;
               break;

           case 24:
           case 32:
               dib->dibSection.dsBitfields[0] = (bi->biCompression == BI_BITFIELDS) ? *(DWORD *)bmi->bmiColors       : 0xff0000;
               dib->dibSection.dsBitfields[1] = (bi->biCompression == BI_BITFIELDS) ? *((DWORD *)bmi->bmiColors + 1) : 0x00ff00;
               dib->dibSection.dsBitfields[2] = (bi->biCompression == BI_BITFIELDS) ? *((DWORD *)bmi->bmiColors + 2) : 0x0000ff;
               break;
       }
      dib->dibSection.dshSection = section;
      dib->dibSection.dsOffset = offset;
      
      dib->status    = DIB_Status_None;
      dib->nColorMap = nColorMap;
      dib->colorMap  = colorMap;
    }
  
  /* Create Device Dependent Bitmap and add DIB pointer */
  if (dib) 
    {
      res = CreateDIBitmap(dc->hSelf, bi, 0, NULL, bmi, usage);
      if (res)
	{
	  bmp = (BITMAPOBJ *) GDI_GetObjPtr(res, BITMAP_MAGIC);
	  if (bmp)
	    {
	      bmp->dib = (DIBSECTION *) dib;
	      /* HACK for now */
	      if(!bmp->physBitmap)
		X11DRV_CreateBitmap(res); 
	    }
	}
    }
  
  /* Create XImage */
  if (dib && bmp)
  {
#ifdef HAVE_LIBXXSHM
      if (TSXShmQueryExtension(gdi_display) &&
          (dib->image = X11DRV_XShmCreateImage( bm.bmWidth, effHeight,
                                                bmp->bitmap.bmBitsPixel, &dib->shminfo )) )
      {
	; /* Created Image */
      } else {
          dib->image = X11DRV_DIB_CreateXImage( bm.bmWidth, effHeight, bmp->bitmap.bmBitsPixel );
          dib->shminfo.shmid = -1;
      }
#else
      dib->image = X11DRV_DIB_CreateXImage( bm.bmWidth, effHeight, bmp->bitmap.bmBitsPixel );
#endif
  }
  
  /* Clean up in case of errors */
  if (!res || !bmp || !dib || !bm.bmBits || (bm.bmBitsPixel <= 8 && !colorMap))
    {
      TRACE("got an error res=%08x, bmp=%p, dib=%p, bm.bmBits=%p\n",
	    res, bmp, dib, bm.bmBits);
      if (bm.bmBits)
        {
	  if (section)
	    UnmapViewOfFile(mapBits), bm.bmBits = NULL;
	  else if (!offset)
	    VirtualFree(bm.bmBits, 0L, MEM_RELEASE), bm.bmBits = NULL;
        }
      
      if (dib && dib->image) { XDestroyImage(dib->image); dib->image = NULL; }
      if (colorMap) { HeapFree(GetProcessHeap(), 0, colorMap); colorMap = NULL; }
      if (dib) { HeapFree(GetProcessHeap(), 0, dib); dib = NULL; }
      if (bmp) { GDI_ReleaseObj(res); bmp = NULL; }
      if (res) { DeleteObject(res); res = 0; }
    }
  else if (bm.bmBits)
    {
      /* Install fault handler, if possible */
      InitializeCriticalSection(&(dib->lock));
      if (VIRTUAL_SetFaultHandler(bm.bmBits, X11DRV_DIB_FaultHandler, (LPVOID)res))
        {
          if (section || offset)
            {
              X11DRV_DIB_DoProtectDIBSection( bmp, PAGE_READWRITE );
              if (dib) dib->status = DIB_Status_AppMod;
            }
          else
            {
	      X11DRV_DIB_DoProtectDIBSection( bmp, PAGE_READONLY );
	      if (dib) dib->status = DIB_Status_InSync;
	    }
        }
    }

  /* Return BITMAP handle and storage location */
  if (bmp) GDI_ReleaseObj(res);
  if (bm.bmBits && bits) *bits = bm.bmBits;
  return res;
}

/***********************************************************************
 *           X11DRV_DIB_DeleteDIBSection
 */
void X11DRV_DIB_DeleteDIBSection(BITMAPOBJ *bmp)
{
  X11DRV_DIBSECTION *dib = (X11DRV_DIBSECTION *) bmp->dib;

  if (dib->image) 
  {
#ifdef HAVE_LIBXXSHM
      if (dib->shminfo.shmid != -1)
      {
          TSXShmDetach (gdi_display, &(dib->shminfo));
          XDestroyImage (dib->image);
          shmdt (dib->shminfo.shmaddr);
          dib->shminfo.shmid = -1;
      }
      else
#endif
          XDestroyImage( dib->image );
  }
  
  if (dib->colorMap)
    HeapFree(GetProcessHeap(), 0, dib->colorMap);

  DeleteCriticalSection(&(dib->lock));
}

/***********************************************************************
 *           X11DRV_DIB_SetDIBColorTable
 */
UINT X11DRV_DIB_SetDIBColorTable(BITMAPOBJ *bmp, DC *dc, UINT start, UINT count, const RGBQUAD *colors)
{
  X11DRV_DIBSECTION *dib = (X11DRV_DIBSECTION *) bmp->dib;

  if (dib && dib->colorMap) {
    /*
     * Changing color table might change the mapping between
     * DIB colors and X11 colors and thus alter the visible state
     * of the bitmap object.
     */
    X11DRV_DIB_Lock(bmp, DIB_Status_AppMod, FALSE);
    X11DRV_DIB_GenColorMap( dc, dib->colorMap, DIB_RGB_COLORS, 
                           dib->dibSection.dsBm.bmBitsPixel,
                            TRUE, colors, start, count + start );
    X11DRV_DIB_Unlock(bmp, TRUE);
    return count;
  }
  return 0;
}

/***********************************************************************
 *           X11DRV_DIB_GetDIBColorTable
 */
UINT X11DRV_DIB_GetDIBColorTable(BITMAPOBJ *bmp, DC *dc, UINT start, UINT count, RGBQUAD *colors)
{
  X11DRV_DIBSECTION *dib = (X11DRV_DIBSECTION *) bmp->dib;

  if (dib && dib->colorMap) {
    int i, end = count + start;
    if (end > dib->nColorMap) end = dib->nColorMap;
    for (i = start; i < end; i++,colors++) {
      COLORREF col = X11DRV_PALETTE_ToLogical( dib->colorMap[i] );
      colors->rgbBlue  = GetBValue(col);
      colors->rgbGreen = GetGValue(col);
      colors->rgbRed   = GetRValue(col);
      colors->rgbReserved = 0;
    }
    return end-start;
  }
  return 0;
}


/**************************************************************************
 *	        X11DRV_DIB_CreateDIBFromPixmap
 *
 *  Allocates a packed DIB and copies the Pixmap data into it.
 *  If bDeletePixmap is TRUE, the Pixmap passed in is deleted after the conversion.
 */
HGLOBAL X11DRV_DIB_CreateDIBFromPixmap(Pixmap pixmap, HDC hdc, BOOL bDeletePixmap)
{
    HBITMAP hBmp = 0;
    BITMAPOBJ *pBmp = NULL;
    HGLOBAL hPackedDIB = 0;

    /* Allocates an HBITMAP which references the Pixmap passed to us */
    hBmp = X11DRV_BITMAP_CreateBitmapHeaderFromPixmap(pixmap);
    if (!hBmp)
    {
        TRACE("\tCould not create bitmap header for Pixmap\n");
        goto END;
    }

    /*
     * Create a packed DIB from the Pixmap wrapper bitmap created above.
     * A packed DIB contains a BITMAPINFO structure followed immediately by
     * an optional color palette and the pixel data.
     */
    hPackedDIB = DIB_CreateDIBFromBitmap(hdc, hBmp);
    
    /* Get a pointer to the BITMAPOBJ structure */
    pBmp = (BITMAPOBJ *)GDI_GetObjPtr( hBmp, BITMAP_MAGIC );

    /* We can now get rid of the HBITMAP wrapper we created earlier.
     * Note: Simply calling DeleteObject will free the embedded Pixmap as well.
     */
    if (!bDeletePixmap)
    {
        /* Clear the physBitmap to prevent the Pixmap from being deleted by DeleteObject */
        pBmp->physBitmap = NULL;
        pBmp->funcs = NULL;
    }
    GDI_ReleaseObj( hBmp );
    DeleteObject(hBmp);  
    
END:
    TRACE("\tReturning packed DIB %x\n", hPackedDIB);
    return hPackedDIB;
}


/**************************************************************************
 *	           X11DRV_DIB_CreatePixmapFromDIB
 *
 *    Creates a Pixmap from a packed DIB
 */
Pixmap X11DRV_DIB_CreatePixmapFromDIB( HGLOBAL hPackedDIB, HDC hdc )
{
    Pixmap pixmap = None;
    HBITMAP hBmp = 0;
    BITMAPOBJ *pBmp = NULL;
    LPBYTE pPackedDIB = NULL;
    LPBITMAPINFO pbmi = NULL;
    LPBITMAPINFOHEADER pbmiHeader = NULL;
    LPBYTE pbits = NULL;
    
    /* Get a pointer to the packed DIB's data  */
    pPackedDIB = (LPBYTE)GlobalLock(hPackedDIB);
    pbmiHeader = (LPBITMAPINFOHEADER)pPackedDIB;
    pbmi = (LPBITMAPINFO)pPackedDIB;
    pbits = (LPBYTE)(pPackedDIB
                     + DIB_BitmapInfoSize( (LPBITMAPINFO)pbmiHeader, DIB_RGB_COLORS ));
    
    /* Create a DDB from the DIB */
     
    hBmp = CreateDIBitmap(hdc,
                          pbmiHeader,
                          CBM_INIT,
                          (LPVOID)pbits,
                          pbmi,
                          DIB_RGB_COLORS);

    GlobalUnlock(hPackedDIB);

    TRACE("CreateDIBitmap returned %x\n", hBmp);

    /* Retrieve the internal Pixmap from the DDB */
     
    pBmp = (BITMAPOBJ *) GDI_GetObjPtr( hBmp, BITMAP_MAGIC );

    pixmap = (Pixmap)pBmp->physBitmap;
    /* clear the physBitmap so that we can steal its pixmap */
    pBmp->physBitmap = NULL;
    pBmp->funcs = NULL;

    /* Delete the DDB we created earlier now that we have stolen its pixmap */
    GDI_ReleaseObj( hBmp );
    DeleteObject(hBmp);
    
    TRACE("\tReturning Pixmap %ld\n", pixmap);
    return pixmap;
}
