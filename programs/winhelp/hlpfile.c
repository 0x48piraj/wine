/*
 * Help Viewer
 *
 * Copyright    1996 Ulrich Schmid
 *              2002 Eric Pouech
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

#include <stdio.h>
#include <string.h>
#include "winbase.h"
#include "wingdi.h"
#include "winhelp.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(winhelp);

#define GET_USHORT(buffer, i)\
(((BYTE)((buffer)[(i)]) + 0x100 * (BYTE)((buffer)[(i)+1])))
#define GET_SHORT(buffer, i)\
(((BYTE)((buffer)[(i)]) + 0x100 * (signed char)((buffer)[(i)+1])))
#define GET_UINT(buffer, i)\
GET_USHORT(buffer, i) + 0x10000 * GET_USHORT(buffer, i+2)

static HLPFILE *first_hlpfile = 0;
static BYTE    *file_buffer;

static struct
{
    UINT        num;
    unsigned*   offsets;
    char*       buffer;
} phrases;

static struct
{
    BYTE**      map;
    BYTE*       end;
    UINT        wMapLen;
} topic;

static struct
{
    UINT                bDebug;
    UINT                wFont;
    UINT                wIndent;
    UINT                wHSpace;
    UINT                wVSpace;
    UINT                wVBackSpace;
    HLPFILE_LINK        link;
    HBITMAP             hBitmap;
    UINT                bmpPos;
} attributes;

static BOOL  HLPFILE_DoReadHlpFile(HLPFILE*, LPCSTR);
static BOOL  HLPFILE_ReadFileToBuffer(HFILE);
static BOOL  HLPFILE_FindSubFile(LPCSTR name, BYTE**, BYTE**);
static BOOL  HLPFILE_SystemCommands(HLPFILE*);
static INT   HLPFILE_UncompressedLZ77_Size(BYTE *ptr, BYTE *end);
static BYTE* HLPFILE_UncompressLZ77(BYTE *ptr, BYTE *end, BYTE *newptr);
static BOOL  HLPFILE_UncompressLZ77_Phrases(HLPFILE*);
static BOOL  HLPFILE_Uncompress_Phrases40(HLPFILE*);
static BOOL  HLPFILE_UncompressLZ77_Topic(HLPFILE*);
static BOOL  HLPFILE_GetContext(HLPFILE*);
static BOOL  HLPFILE_AddPage(HLPFILE*, BYTE*, BYTE*, unsigned);
static BOOL  HLPFILE_AddParagraph(HLPFILE*, BYTE *, BYTE*, unsigned*);
static UINT  HLPFILE_Uncompressed2_Size(BYTE*, BYTE*);
static void  HLPFILE_Uncompress2(BYTE**, BYTE*, BYTE*);
static BOOL  HLPFILE_Uncompress3(char*, const char*, const BYTE*, const BYTE*);
static void  HLPFILE_UncompressRLE(const BYTE* src, unsigned sz, BYTE** dst);
static BOOL  HLPFILE_ReadFont(HLPFILE* hlpfile);

/***********************************************************************
 *
 *           HLPFILE_Contents
 */
HLPFILE_PAGE *HLPFILE_Contents(LPCSTR lpszPath)
{
    HLPFILE *hlpfile = HLPFILE_ReadHlpFile(lpszPath);

    if (!hlpfile) return 0;

    return hlpfile->first_page;
}

/***********************************************************************
 *
 *           HLPFILE_PageByNumber
 */
HLPFILE_PAGE *HLPFILE_PageByNumber(LPCSTR lpszPath, UINT wNum)
{
    HLPFILE_PAGE *page;
    HLPFILE *hlpfile = HLPFILE_ReadHlpFile(lpszPath);

    if (!hlpfile) return 0;

    WINE_TRACE("[%s/%u]\n", lpszPath, wNum);

    for (page = hlpfile->first_page; page && wNum; page = page->next) wNum--;

    return page;
}

/***********************************************************************
 *
 *           HLPFILE_HlpFilePageByHash
 */
HLPFILE_PAGE *HLPFILE_PageByHash(LPCSTR lpszPath, LONG lHash)
{
    HLPFILE_PAGE*       page;
    HLPFILE_PAGE*       found;
    HLPFILE*            hlpfile = HLPFILE_ReadHlpFile(lpszPath);
    int                 i;

    WINE_TRACE("path<%s>[%lx]\n", lpszPath, lHash);

    if (!hlpfile) return 0;

    page = NULL;
    for (i = 0; i < hlpfile->wContextLen; i++)
    {
        if (hlpfile->Context[i].lHash != lHash) continue;

        /* FIXME:
         * this finds the page containing the offset. The offset can either
         * refer to the top of the page (offset == page->offset), or
         * to some paragraph inside the page...
         * As of today, we only return the page... we should also return
         * a paragraph, and then, while opening a new page, compute the
         * y-offset of the paragraph to be shown and scroll the window
         * accordinly
         */
        found = NULL;
        for (page = hlpfile->first_page; page; page = page->next)
        {
            if (page->offset <= hlpfile->Context[i].offset)
            {
                if (!found || found->offset < page->offset)
                    found = page;
            }
        }
        if (found) return found;

        WINE_ERR("Page of offset %lu not found in file %s\n",
                  hlpfile->Context[i].offset, lpszPath);
        return NULL;
    }
    WINE_ERR("Page of hash %lx not found in file %s\n", lHash, lpszPath);
    return NULL;
}

/***********************************************************************
 *
 *           HLPFILE_Hash
 */
LONG HLPFILE_Hash(LPCSTR lpszContext)
{
    LONG lHash = 0;
    CHAR c;

    while ((c = *lpszContext++))
    {
        CHAR x = 0;
        if (c >= 'A' && c <= 'Z') x = c - 'A' + 17;
        if (c >= 'a' && c <= 'z') x = c - 'a' + 17;
        if (c >= '1' && c <= '9') x = c - '0';
        if (c == '0') x = 10;
        if (c == '.') x = 12;
        if (c == '_') x = 13;
        if (x) lHash = lHash * 43 + x;
    }
    return lHash;
}
/***********************************************************************
 *
 *           HLPFILE_ReadHlpFile
 */
HLPFILE *HLPFILE_ReadHlpFile(LPCSTR lpszPath)
{
    HLPFILE*      hlpfile;

    for (hlpfile = first_hlpfile; hlpfile; hlpfile = hlpfile->next)
    {
        if (!lstrcmp(hlpfile->lpszPath, lpszPath))
        {
            hlpfile->wRefCount++;
            return hlpfile;
        }
    }

    hlpfile = HeapAlloc(GetProcessHeap(), 0, sizeof(HLPFILE) + lstrlen(lpszPath) + 1);
    if (!hlpfile) return 0;

    hlpfile->wRefCount   = 1;
    hlpfile->Context     = NULL;
    hlpfile->wContextLen = 0;
    hlpfile->first_page  = NULL;
    hlpfile->first_macro = NULL;
    hlpfile->prev        = NULL;
    hlpfile->next        = first_hlpfile;
    hlpfile->lpszPath    = (char*)hlpfile + sizeof(HLPFILE);
    hlpfile->lpszTitle   = NULL;

    hlpfile->numFonts    = 0;
    hlpfile->fonts       = NULL;

    strcpy(hlpfile->lpszPath, lpszPath);

    first_hlpfile = hlpfile;
    if (hlpfile->next) hlpfile->next->prev = hlpfile;

    phrases.offsets = NULL;
    phrases.buffer = NULL;
    topic.map = NULL;
    topic.end = NULL;
    file_buffer = NULL;

    if (!HLPFILE_DoReadHlpFile(hlpfile, lpszPath))
    {
        HLPFILE_FreeHlpFile(hlpfile);
        hlpfile = 0;
    }

    if (phrases.offsets)  HeapFree(GetProcessHeap(), 0, phrases.offsets);
    if (phrases.buffer)   HeapFree(GetProcessHeap(), 0, phrases.buffer);
    if (topic.map)        HeapFree(GetProcessHeap(), 0, topic.map);
    if (file_buffer)      HeapFree(GetProcessHeap(), 0, file_buffer);

    return hlpfile;
}

/***********************************************************************
 *
 *           HLPFILE_DoReadHlpFile
 */
static BOOL HLPFILE_DoReadHlpFile(HLPFILE *hlpfile, LPCSTR lpszPath)
{
    BOOL        ret;
    HFILE       hFile;
    OFSTRUCT    ofs;
    BYTE*       buf;
    DWORD       ref = 0x0C;
    unsigned    index, old_index, offset, len, offs;

    hFile = OpenFile(lpszPath, &ofs, OF_READ | OF_SEARCH);
    if (hFile == HFILE_ERROR) return FALSE;

    ret = HLPFILE_ReadFileToBuffer(hFile);
    _lclose(hFile);
    if (!ret) return FALSE;

    if (!HLPFILE_SystemCommands(hlpfile)) return FALSE;
    if (!HLPFILE_UncompressLZ77_Phrases(hlpfile) &&
        !HLPFILE_Uncompress_Phrases40(hlpfile))
        return FALSE;
    if (!HLPFILE_UncompressLZ77_Topic(hlpfile)) return FALSE;
    if (!HLPFILE_ReadFont(hlpfile)) return FALSE;

    buf = topic.map[0];
    old_index = -1;
    offs = 0;
    do
    {
        BYTE*   end;

        /* FIXME this depends on the blocksize, can be 2k in some cases */
        index  = (ref - 0x0C) >> 14;
        offset = (ref - 0x0C) & 0x3fff;

        WINE_TRACE("ref=%08lx => [%u/%u]\n", ref, index, offset);

        if (index >= topic.wMapLen) {WINE_WARN("maplen\n"); break;}
        buf = topic.map[index] + offset;
        if (buf + 0x15 >= topic.end) {WINE_WARN("extra\n"); break;}
        end = min(buf + GET_UINT(buf, 0), topic.end);
        if (index != old_index) {offs = 0; old_index = index;}

        switch (buf[0x14])
	{
	case 0x02:
            if (!HLPFILE_AddPage(hlpfile, buf, end, index * 0x8000L + offs)) return FALSE;
            break;

	case 0x20:
            if (!HLPFILE_AddParagraph(hlpfile, buf, end, &len)) return FALSE;
            offs += len;
            break;

	case 0x23:
            if (!HLPFILE_AddParagraph(hlpfile, buf, end, &len)) return FALSE;
            offs += len;
            break;

	default:
            WINE_ERR("buf[0x14] = %x\n", buf[0x14]);
	}

        ref = GET_UINT(buf, 0xc);
    } while (ref != 0xffffffff);

    return HLPFILE_GetContext(hlpfile);
}

/***********************************************************************
 *
 *           HLPFILE_AddPage
 */
static BOOL HLPFILE_AddPage(HLPFILE *hlpfile, BYTE *buf, BYTE *end, unsigned offset)
{
    HLPFILE_PAGE* page;
    BYTE*         title;
    UINT          titlesize;

    if (buf + 0x31 > end) {WINE_WARN("page1\n"); return FALSE;};
    title = buf + GET_UINT(buf, 0x10);
    if (title > end) {WINE_WARN("page2\n"); return FALSE;};

    if (GET_UINT(buf, 0x4) > GET_UINT(buf, 0) - GET_UINT(buf, 0x10))
    {
        if (hlpfile->hasPhrases)
        {
            titlesize = HLPFILE_Uncompressed2_Size(title, end);
            page = HeapAlloc(GetProcessHeap(), 0, sizeof(HLPFILE_PAGE) + titlesize);
            if (!page) return FALSE;

            page->lpszTitle = (char*)page + sizeof(HLPFILE_PAGE);
            HLPFILE_Uncompress2(&title, end, page->lpszTitle);
        }
        else
        {
            titlesize = GET_UINT(buf, 4) + 1;
            page = HeapAlloc(GetProcessHeap(), 0, sizeof(HLPFILE_PAGE) + titlesize);
            if (!page) return FALSE;
            page->lpszTitle = (char*)page + sizeof(HLPFILE_PAGE);

            HLPFILE_Uncompress3(page->lpszTitle, page->lpszTitle + titlesize, title, end);
        }
    }
    else
    {
        titlesize = GET_UINT(buf, 0x4);
        page = HeapAlloc(GetProcessHeap(), 0, sizeof(HLPFILE_PAGE) + titlesize);
        if (!page) return FALSE;

        page->lpszTitle = (char*)page + sizeof(HLPFILE_PAGE);
        memcpy(page->lpszTitle, title, titlesize);
    }

    if (hlpfile->first_page)
    {
        HLPFILE_PAGE  *p;

        for (p = hlpfile->first_page; p->next; p = p->next);
        page->prev = p;
        p->next    = page;
    }
    else
    {
        hlpfile->first_page = page;
        page->prev = NULL;
    }

    page->file            = hlpfile;
    page->next            = NULL;
    page->first_paragraph = NULL;
    page->wNumber         = GET_UINT(buf, 0x21);
    page->offset          = offset;

    WINE_TRACE("Added page[%d]: title='%s' offset=%08x\n",
               page->wNumber, page->lpszTitle, page->offset);

    memset(&attributes, 0, sizeof(attributes));

    return TRUE;
}

static long fetch_long(BYTE** ptr)
{
    long        ret;

    if (*(*ptr) & 1)
    {
        ret = (*(unsigned long*)(*ptr) - 0x80000000L) / 2;
        (*ptr) += 4;
    }
    else
    {
        ret = (*(unsigned short*)(*ptr) - 0x8000) / 2;
        (*ptr) += 2;
    }

    return ret;
}

static unsigned long fetch_ulong(BYTE** ptr)
{
    unsigned long        ret;

    if (*(*ptr) & 1)
    {
        ret = *(unsigned long*)(*ptr) / 2;
        (*ptr) += 4;
    }
    else
    {
        ret = *(unsigned short*)(*ptr) / 2;
        (*ptr) += 2;
    }
    return ret;
}

static short fetch_short(BYTE** ptr)
{
    short       ret;

    if (*(*ptr) & 1)
    {
        ret = (*(unsigned short*)(*ptr) - 0x8000) / 2;
        (*ptr) += 2;
    }
    else
    {
        ret = (*(unsigned char*)(*ptr) - 0x80) / 2;
        (*ptr)++;
    }
    return ret;
}

static unsigned short fetch_ushort(BYTE** ptr)
{
    unsigned short ret;

    if (*(*ptr) & 1)
    {
        ret = *(unsigned short*)(*ptr) / 2;
        (*ptr) += 2;
    }
    else
    {
        ret = *(unsigned char*)(*ptr) / 2;
        (*ptr)++;
    }
    return ret;
}

/******************************************************************
 *		HLPFILE_LoadPictureByAddr
 *
 *
 */
static  BOOL    HLPFILE_LoadPictureByAddr(HLPFILE *hlpfile, char* ref,
                                          unsigned long size, unsigned pos)
{
    unsigned    i, numpict;

    numpict = *(unsigned short*)(ref + 2);

    for (i = 0; i < numpict; i++)
    {
        BYTE                    *beg, *ptr;
        BYTE                    *pict_beg;
        BYTE                    type, pack;
        BITMAPINFO*             bi;
        unsigned long           off, sz;
        unsigned                shift;

        ptr = beg = ref + *((unsigned long*)ref + 1 + i);

        type = *ptr++;
        pack = *ptr++;

        bi = HeapAlloc(GetProcessHeap(), 0, sizeof(*bi));
        if (!bi) return FALSE;

        bi->bmiHeader.biSize = sizeof(bi->bmiHeader);
        bi->bmiHeader.biXPelsPerMeter = fetch_ulong(&ptr);
        bi->bmiHeader.biYPelsPerMeter = fetch_ulong(&ptr);
        bi->bmiHeader.biPlanes        = fetch_ushort(&ptr);
        bi->bmiHeader.biBitCount      = fetch_ushort(&ptr);
        bi->bmiHeader.biWidth         = fetch_ulong(&ptr);
        bi->bmiHeader.biHeight        = fetch_ulong(&ptr);
        bi->bmiHeader.biClrUsed       = fetch_ulong(&ptr);
        bi->bmiHeader.biClrImportant  = fetch_ulong(&ptr);
        bi->bmiHeader.biCompression   = BI_RGB;
        if (bi->bmiHeader.biBitCount > 32) WINE_FIXME("Unknown bit count %u\n", bi->bmiHeader.biBitCount);
        if (bi->bmiHeader.biPlanes != 1) WINE_FIXME("Unsupported planes %u\n", bi->bmiHeader.biPlanes);
        shift = 32 / bi->bmiHeader.biBitCount;
        bi->bmiHeader.biSizeImage = ((bi->bmiHeader.biWidth + shift - 1) / shift) * 4 * bi->bmiHeader.biHeight;

        sz = fetch_ulong(&ptr);
        fetch_ulong(&ptr); /* hotspot size */

        off = *(unsigned long*)ptr;     ptr += 4;
        /* *(unsigned long*)ptr; hotspot offset */ ptr += 4;

        /* now read palette info */
        if (type == 0x06)
        {
            unsigned nc = bi->bmiHeader.biClrUsed;
            unsigned i;

            if (!nc) nc = 1 << bi->bmiHeader.biBitCount;
            bi = HeapReAlloc(GetProcessHeap(), 0, bi, sizeof(*bi) + nc * sizeof(RGBQUAD));
            if (!bi) return FALSE;
            for (i = 0; i < nc; i++)
            {
                bi->bmiColors[i].rgbBlue     = ptr[0];
                bi->bmiColors[i].rgbGreen    = ptr[1];
                bi->bmiColors[i].rgbRed      = ptr[2];
                bi->bmiColors[i].rgbReserved = 0;
                ptr += 4;
            }
        }

        switch (pack)
        {
        case 0: /* uncompressed */
            pict_beg = beg + off;
            if (sz != bi->bmiHeader.biSizeImage)
                WINE_WARN("Bogus image sizes: %lu / %lu [sz=(%lu,%lu) bc=%u pl=%u]\n",
                          sz, bi->bmiHeader.biSizeImage,
                          bi->bmiHeader.biWidth, bi->bmiHeader.biHeight,
                          bi->bmiHeader.biBitCount, bi->bmiHeader.biPlanes);
            break;
        case 1: /* RunLen */
            {
                BYTE*   dst;

                dst = pict_beg = HeapAlloc(GetProcessHeap(), 0, bi->bmiHeader.biSizeImage);
                if (!pict_beg) return FALSE;
                HLPFILE_UncompressRLE(beg + off, sz, &dst);
                if (dst - pict_beg != bi->bmiHeader.biSizeImage)
                    WINE_FIXME("buffer XXX-flow\n");
            }
            break;
        case 2: /* LZ77 */
            {
                unsigned long esz;
                esz = HLPFILE_UncompressedLZ77_Size(beg + off, beg + off + sz);
                pict_beg = HeapAlloc(GetProcessHeap(), 0, esz);
                if (!pict_beg) return FALSE;
                HLPFILE_UncompressLZ77(beg + off, beg + off + sz, pict_beg);
                if (esz != bi->bmiHeader.biSizeImage)
                    WINE_WARN("Bogus image sizes: %lu / %lu [sz=(%lu,%lu) bc=%u pl=%u]\n",
                              esz, bi->bmiHeader.biSizeImage,
                              bi->bmiHeader.biWidth, bi->bmiHeader.biHeight,
                              bi->bmiHeader.biBitCount, bi->bmiHeader.biPlanes);
            }
            break;
        case 3: /* LZ77 then RLE */
            {
                BYTE*           tmp;
                unsigned long   sz77;
                BYTE*           dst;

                sz77 = HLPFILE_UncompressedLZ77_Size(beg + off, beg + off + sz);
                tmp = HeapAlloc(GetProcessHeap(), 0, bi->bmiHeader.biSizeImage);
                if (!tmp) return FALSE;
                HLPFILE_UncompressLZ77(beg + off, beg + off + sz, tmp);
                pict_beg = dst = HeapAlloc(GetProcessHeap(), 0, bi->bmiHeader.biSizeImage);
                if (!pict_beg) return FALSE;
                HLPFILE_UncompressRLE(tmp, sz77, &dst);
                if (dst - pict_beg != bi->bmiHeader.biSizeImage)
                    WINE_WARN("Bogus image sizes: %u / %lu [sz=(%lu,%lu) bc=%u pl=%u]\n",
                              dst - pict_beg, bi->bmiHeader.biSizeImage,
                              bi->bmiHeader.biWidth, bi->bmiHeader.biHeight,
                              bi->bmiHeader.biBitCount, bi->bmiHeader.biPlanes);
                HeapFree(GetProcessHeap(), 0, tmp);
            }
            break;
        default:
            WINE_FIXME("Unsupported packing %u\n", pack);
            return FALSE;
        }

        attributes.hBitmap = CreateDIBitmap(GetDC(0), &bi->bmiHeader, CBM_INIT,
                                            pict_beg, bi, DIB_RGB_COLORS);
        if (!attributes.hBitmap)
            WINE_ERR("Couldn't create bitmap\n");
        attributes.bmpPos = pos;

        HeapFree(GetProcessHeap(), 0, bi);
        if (pict_beg != beg + off) HeapFree(GetProcessHeap(), 0, pict_beg);

        /* FIXME: implement support for multiple picture format */
        if (numpict != 1) WINE_FIXME("Supporting only one bitmap format per logical bitmap (for now). Using first format\n");
        break;
    }
    return TRUE;
}

/******************************************************************
 *		HLPFILE_LoadPictureByIndex
 *
 *
 */
static  BOOL    HLPFILE_LoadPictureByIndex(HLPFILE *hlpfile, unsigned index, unsigned pos)
{
    char        tmp[16];
    BYTE        *ref, *end;

    WINE_TRACE("Loading picture #%d\n", index);
    sprintf(tmp, "bm%u", index);

    if (!HLPFILE_FindSubFile(tmp, &ref, &end)) {WINE_WARN("no sub file\n"); return FALSE;}

    ref += 9;

    return HLPFILE_LoadPictureByAddr(hlpfile, ref, end - ref, pos);
}

/***********************************************************************
 *
 *           HLPFILE_AddParagraph
 */
static BOOL HLPFILE_AddParagraph(HLPFILE *hlpfile, BYTE *buf, BYTE *end, unsigned* len)
{
    HLPFILE_PAGE      *page;
    HLPFILE_PARAGRAPH *paragraph, **paragraphptr;
    UINT               textsize;
    BYTE              *format, *format_end, *text, *text_end;
    long               size;
    unsigned short     bits;
    unsigned           nc, ncol = 1;

    if (!hlpfile->first_page) {WINE_WARN("no page\n"); return FALSE;};

    for (page = hlpfile->first_page; page->next; page = page->next) /* Nothing */;
    for (paragraphptr = &page->first_paragraph; *paragraphptr;
         paragraphptr = &(*paragraphptr)->next) /* Nothing */;

    if (buf + 0x19 > end) {WINE_WARN("header too small\n"); return FALSE;};

    size = GET_UINT(buf, 0x4);

    if (GET_UINT(buf, 0x4) > GET_UINT(buf, 0) - GET_UINT(buf, 0x10))
    {
        if (hlpfile->hasPhrases)
        {
            BYTE* lptr = buf + GET_UINT(buf, 0x10);
            unsigned size2;

            size2 = HLPFILE_Uncompressed2_Size(lptr, end);
            if (size2 != size + 1)
                WINE_FIXME("Mismatch in sizes: decomp2=%u header=%lu\n", size2, size);
            text = HeapAlloc(GetProcessHeap(), 0, size + 1);
            if (!text) return FALSE;
            HLPFILE_Uncompress2(&lptr, end, text);
        }
        else
        {
            /* block is compressed */
            text = HeapAlloc(GetProcessHeap(), 0, size);
            if (!text) return FALSE;
            HLPFILE_Uncompress3(text, text + size, buf + GET_UINT(buf, 0x10), end);
        }
    }
    else
    {
        text = buf + GET_UINT(buf, 0x10);
    }
    text_end = text + size;

    format = buf + 0x15;
    format_end = buf + GET_UINT(buf, 0x10);

    fetch_long(&format);
    *len = fetch_ushort(&format);
    if (buf[0x14] == 0x23)
    {
        char    type;

        ncol = *format++;

        WINE_TRACE("#cols %u\n", ncol);
        type = *format++;
        if (type == 0 || type == 2)
            format += 2;
        format += ncol * 4;
    }

    for (nc = 0; nc < ncol; nc++)
    {
        WINE_TRACE("looking for format at offset %u for column %d\n", format - (buf + 0x15), nc);
        if (buf[0x14] == 0x23)
            format += 5;
        format += 4;
        bits = *(unsigned short*)format; format += 2;
        if (bits & 0x0001) fetch_long(&format);
        if (bits & 0x0002) fetch_short(&format);
        if (bits & 0x0004) fetch_short(&format);
        if (bits & 0x0008) fetch_short(&format);
        if (bits & 0x0010) fetch_short(&format);
        if (bits & 0x0020) fetch_short(&format);
        if (bits & 0x0040) fetch_short(&format);
        if (bits & 0x0100) format += 3;
        if (bits & 0x0200)
        {
            int                 ntab = fetch_short(&format);
            unsigned short      ts;

            while (ntab-- > 0)
            {
                ts = fetch_ushort(&format);
                if (ts & 0x4000) fetch_ushort(&format);
            }
        }

        while (text < text_end && format < format_end)
        {
            WINE_TRACE("Got text: '%s' (%p/%p - %p/%p)\n", text, text, text_end, format, format_end);
            textsize = strlen(text) + 1;
            if (textsize > 1 || attributes.hBitmap)
            {
                paragraph = HeapAlloc(GetProcessHeap(), 0,
                                      sizeof(HLPFILE_PARAGRAPH) + textsize);
                if (!paragraph) return FALSE;
                *paragraphptr = paragraph;
                paragraphptr = &paragraph->next;

                paragraph->next     = NULL;
                paragraph->link     = NULL;

                if (attributes.hBitmap)
                {
                    paragraph->cookie           = para_image;
                    paragraph->u.image.hBitmap  = attributes.hBitmap;
                    paragraph->u.image.pos      = attributes.bmpPos;
                    if (attributes.wVSpace) paragraph->u.image.pos |= 0x8000;
                }
                else
                {
                    paragraph->cookie          = (attributes.bDebug) ? para_debug_text : para_normal_text;
                    paragraph->u.text.wFont    = attributes.wFont;
                    paragraph->u.text.wVSpace  = attributes.wVSpace;
                    paragraph->u.text.wHSpace  = attributes.wHSpace;
                    paragraph->u.text.wIndent  = attributes.wIndent;
                    paragraph->u.text.lpszText = (char*)paragraph + sizeof(HLPFILE_PARAGRAPH);
                    strcpy(paragraph->u.text.lpszText, text);
                }

                if (attributes.link.lpszPath)
                {
                    /* FIXME: should build a string table for the attributes.link.lpszPath
                     * they are reallocated for each link
                     */
                    paragraph->link = HeapAlloc(GetProcessHeap(), 0,
                                                sizeof(HLPFILE_LINK) + strlen(attributes.link.lpszPath) + 1);
                    if (!paragraph->link) return FALSE;

                    paragraph->link->lpszPath = (char*)paragraph->link + sizeof(HLPFILE_LINK);
                    strcpy((char*)paragraph->link->lpszPath, attributes.link.lpszPath);
                    paragraph->link->lHash    = attributes.link.lHash;

                    paragraph->link->bPopup   = attributes.link.bPopup;
                    WINE_TRACE("Link to %s/%08lx\n",
                               paragraph->link->lpszPath, paragraph->link->lHash);
                }
#if 0
                memset(&attributes, 0, sizeof(attributes));
#else
                attributes.hBitmap = 0;
                attributes.link.lpszPath = NULL;
                attributes.wVSpace = 0;
                attributes.wHSpace = 0;
                attributes.wIndent = 0;
#endif
            }
            /* else: null text, keep on storing attributes */
            text += textsize;

	    if (*format == 0xff)
            {
                format++;
                break;
            }

            WINE_TRACE("format=%02x\n", *format);
            switch (*format)
            {
            case 0x20:
                WINE_FIXME("NIY\n");
                format += 5;
                break;

            case 0x21:
                WINE_FIXME("NIY\n");
                format += 3;
                break;

	    case 0x80:
                attributes.wFont = GET_USHORT(format, 1);
                WINE_TRACE("Changing font to %d\n", attributes.wFont);
                format += 3;
                break;

	    case 0x81:
                attributes.wVSpace++;
                format += 1;
                break;

	    case 0x82:
                attributes.wVSpace += 2 - attributes.wVBackSpace;
                attributes.wVBackSpace = 0;
                attributes.wIndent = 0;
                format += 1;
                break;

	    case 0x83:
                attributes.wIndent++;
                format += 1;
                break;

#if 0
	    case 0x84:
                format += 3;
                break;
#endif

	    case 0x86:
	    case 0x87:
	    case 0x88:
                {
                    BYTE    pos = (*format - 0x86);
                    BYTE    type = format[1];
                    long    size;

                    format += 2;
                    size = fetch_long(&format);
                    switch (type)
                    {
                    case 0x22:
                        fetch_ushort(&format); /* hot spot */
                        /* fall thru */
                    case 0x03:
                        if (*(short*)format == 0)
                            HLPFILE_LoadPictureByIndex(hlpfile,
                                                       *(short*)(format + 2),
                                                       pos);
                        else
                        {
                            WINE_FIXME("does it work ???\n");
                            HLPFILE_LoadPictureByAddr(hlpfile, format + 2,
                                                      size - 4, pos);
                        }
                        break;
                    case 0x05:
                        WINE_FIXME("Got an embedded element %s\n", format + 6);
                        break;
                    default:
                        WINE_FIXME("Got a type %d picture\n", type);
                        break;
                    }
                    format += size;
                }
                break;

	    case 0x89:
                attributes.wVBackSpace++;
                format += 1;
                break;

            case 0x8B:
            case 0x8C:
                WINE_FIXME("NIY\n");
                format += 1;
                break;

#if 0
	    case 0xa9:
                format += 2;
                break;
#endif

            case 0xc8:
            case 0xcc:
                WINE_FIXME("macro NIY %s\n", format + 3);
                format += GET_USHORT(format, 1) + 3;
                break;

            case 0xe0:
            case 0xe1:
                WINE_WARN("jump topic 1 => %u\n", GET_UINT(format, 1));
                format += 5;
                break;

	    case 0xe2:
	    case 0xe3:
                attributes.link.lpszPath = hlpfile->lpszPath;
                attributes.link.lHash    = GET_UINT(format, 1);
                attributes.link.bPopup   = !(*format & 1);
                format += 5;
                break;

            case 0xe6:
            case 0xe7:
                WINE_WARN("jump topic 2 => %u\n", GET_UINT(format, 1));
                format += 5;
                break;

	    case 0xea:
                attributes.link.lpszPath = format + 8;
                attributes.link.lHash    = GET_UINT(format, 4);
                attributes.link.bPopup   = !(*format & 1);
                format += 3 + GET_USHORT(format, 1);
                break;

            case 0xee:
            case 0xef:
            case 0xeb:
                WINE_WARN("jump to external file\n");
                format += 3 + GET_USHORT(format, 1);
                break;

	    default:
                WINE_WARN("format %02x\n", *format);
                format++;
	    }
	}
    }
    if (text_end != buf + GET_UINT(buf, 0x10) + size)
        HeapFree(GetProcessHeap(), 0, text_end - size);
    return TRUE;
}

/******************************************************************
 *		HLPFILE_ReadFont
 *
 *
 */
static BOOL HLPFILE_ReadFont(HLPFILE* hlpfile)
{
    BYTE        *ref, *end;
    unsigned    i, len, idx;
    unsigned    face_num, dscr_num, face_offset, dscr_offset;
    BYTE        flag, family;

    if (!HLPFILE_FindSubFile("FONT", &ref, &end))
    {
        WINE_WARN("no subfile FONT\n");
        hlpfile->numFonts = 0;
        hlpfile->fonts = NULL;
        return FALSE;
    }

    ref += 9;

    face_num    = GET_USHORT(ref, 0);
    dscr_num    = GET_USHORT(ref, 2);
    face_offset = GET_USHORT(ref, 4);
    dscr_offset = GET_USHORT(ref, 6);

    WINE_TRACE("Got NumFacenames=%u@%u NumDesc=%u@%u\n",
               face_num, face_offset, dscr_num, dscr_offset);

    hlpfile->numFonts = dscr_num;
    hlpfile->fonts = HeapAlloc(GetProcessHeap(), 0, sizeof(HLPFILE_FONT) * dscr_num);

    len = (dscr_offset - face_offset) / face_num;
/* EPP     for (i = face_offset; i < dscr_offset; i += len) */
/* EPP         WINE_FIXME("[%d]: %*s\n", i / len, len, ref + i); */
    for (i = 0; i < dscr_num; i++)
    {
        flag = ref[dscr_offset + i * 11 + 0];
        family = ref[dscr_offset + i * 11 + 2];

        hlpfile->fonts[i].LogFont.lfHeight = -ref[dscr_offset + i * 11 + 1] / 2;
        hlpfile->fonts[i].LogFont.lfWidth = 0;
        hlpfile->fonts[i].LogFont.lfEscapement = 0;
        hlpfile->fonts[i].LogFont.lfOrientation = 0;
        hlpfile->fonts[i].LogFont.lfWeight = (flag & 1) ? 700 : 400;
        hlpfile->fonts[i].LogFont.lfItalic = (flag & 2) ? TRUE : FALSE;
        hlpfile->fonts[i].LogFont.lfUnderline = (flag & 4) ? TRUE : FALSE;
        hlpfile->fonts[i].LogFont.lfStrikeOut = (flag & 8) ? TRUE : FALSE;
        hlpfile->fonts[i].LogFont.lfCharSet = ANSI_CHARSET;
        hlpfile->fonts[i].LogFont.lfOutPrecision = OUT_DEFAULT_PRECIS;
        hlpfile->fonts[i].LogFont.lfClipPrecision = CLIP_DEFAULT_PRECIS;
        hlpfile->fonts[i].LogFont.lfQuality = DEFAULT_QUALITY;
        hlpfile->fonts[i].LogFont.lfPitchAndFamily = DEFAULT_PITCH;

        switch (family)
        {
        case 0x01: hlpfile->fonts[i].LogFont.lfPitchAndFamily |= FF_MODERN;     break;
        case 0x02: hlpfile->fonts[i].LogFont.lfPitchAndFamily |= FF_ROMAN;      break;
        case 0x03: hlpfile->fonts[i].LogFont.lfPitchAndFamily |= FF_SWISS;      break;
        case 0x04: hlpfile->fonts[i].LogFont.lfPitchAndFamily |= FF_SCRIPT;     break;
        case 0x05: hlpfile->fonts[i].LogFont.lfPitchAndFamily |= FF_DECORATIVE; break;
        default: WINE_FIXME("Unknown family %u\n", family);
        }
        idx = *(unsigned short*)(ref + dscr_offset + i * 11 + 3);

        if (idx < face_num)
        {
            strncpy(hlpfile->fonts[i].LogFont.lfFaceName, ref + face_offset + idx * len, min(len, LF_FACESIZE - 1));
            hlpfile->fonts[i].LogFont.lfFaceName[min(len, LF_FACESIZE - 1) + 1] = '\0';
        }
        else
        {
            WINE_FIXME("Too high face ref (%u/%u)\n", idx, face_num);
            strcpy(hlpfile->fonts[i].LogFont.lfFaceName, "Helv");
        }
        hlpfile->fonts[i].hFont = (HANDLE)0;
        hlpfile->fonts[i].color = RGB(ref[dscr_offset + i * 11 + 5],
                                      ref[dscr_offset + i * 11 + 6],
                                      ref[dscr_offset + i * 11 + 7]);
#define X(b,s) ((flag & (1 << b)) ? "-"s: "")
        WINE_TRACE("Font[%d]: flags=%02x%s%s%s%s%s%s pSize=%u family=%u face=%s[%u] color=%08lx\n",
                   i, flag,
                   X(0, "bold"),
                   X(1, "italic"),
                   X(2, "underline"),
                   X(3, "strikeOut"),
                   X(4, "dblUnderline"),
                   X(5, "smallCaps"),
                   ref[dscr_offset + i * 11 + 1],
                   family,
                   hlpfile->fonts[i].LogFont.lfFaceName, idx,
                   *(unsigned long*)(ref + dscr_offset + i * 11 + 5) & 0x00FFFFFF);
    }
/*
---				      only if FacenamesOffset >= 12
unsigned short NumStyles	      number of style descriptors
unsigned short StyleOffset	      start of array of style descriptors
				      relative to &NumFacenames
---				      only if FacenamesOffset >= 16
unsigned short NumCharMapTables       number of character mapping tables
unsigned short CharMapTableOffset     start of array of character mapping
table names relative to &NumFacenames
*/
    return TRUE;
}

/***********************************************************************
 *
 *           HLPFILE_ReadFileToBuffer
 */
static BOOL HLPFILE_ReadFileToBuffer(HFILE hFile)
{
    BYTE  header[16], dummy[1];
    UINT  size;

    if (_hread(hFile, header, 16) != 16) {WINE_WARN("header\n"); return FALSE;};

    size = GET_UINT(header, 12);
    file_buffer = HeapAlloc(GetProcessHeap(), 0, size + 1);
    if (!file_buffer) return FALSE;

    memcpy(file_buffer, header, 16);
    if (_hread(hFile, file_buffer + 16, size - 16) != size - 16)
    {WINE_WARN("filesize1\n"); return FALSE;};

    if (_hread(hFile, dummy, 1) != 0) WINE_WARN("filesize2\n");

    file_buffer[size] = '\0'; /* FIXME: was '0', sounds ackward to me */

    return TRUE;
}

/***********************************************************************
 *
 *           HLPFILE_FindSubFile
 */
static BOOL HLPFILE_FindSubFile(LPCSTR name, BYTE **subbuf, BYTE **subend)
{
    BYTE *root = file_buffer + GET_UINT(file_buffer,  4);
    BYTE *end  = file_buffer + GET_UINT(file_buffer, 12);
    BYTE *ptr  = root + 0x37;

    while (ptr < end && ptr[0] == 0x7c)
    {
        BYTE *fname = ptr + 1;
        ptr += strlen(ptr) + 1;
        if (!lstrcmpi(fname, name))
	{
            *subbuf = file_buffer + GET_UINT(ptr, 0);
            *subend = *subbuf + GET_UINT(*subbuf, 0);
            if (file_buffer > *subbuf || *subbuf > *subend || *subend > end)
	    {
                WINE_WARN("size mismatch\n");
                return FALSE;
	    }
            return TRUE;
	}
        else ptr += 4;
    }
    return FALSE;
}

/***********************************************************************
 *
 *           HLPFILE_SystemCommands
 */
static BOOL HLPFILE_SystemCommands(HLPFILE* hlpfile)
{
    BYTE *buf, *ptr, *end;
    HLPFILE_MACRO *macro, **m;
    LPSTR p;
    unsigned short magic, minor, major, flags;

    hlpfile->lpszTitle = NULL;

    if (!HLPFILE_FindSubFile("SYSTEM", &buf, &end)) return FALSE;

    magic = GET_USHORT(buf + 9, 0);
    minor = GET_USHORT(buf + 9, 2);
    major = GET_USHORT(buf + 9, 4);
    /* gen date on 4 bytes */
    flags = GET_USHORT(buf + 9, 10);
    WINE_TRACE("Got system header: magic=%04x version=%d.%d flags=%04x\n",
               magic, major, minor, flags);
    if (magic != 0x036C || major != 1)
    {WINE_WARN("Wrong system header\n"); return FALSE;}
    if (minor <= 16) {WINE_WARN("too old file format (NIY)\n"); return FALSE;}

    hlpfile->version = minor;
    hlpfile->flags = flags;

    for (ptr = buf + 0x15; ptr + 4 <= end; ptr += GET_USHORT(ptr, 2) + 4)
    {
        switch (GET_USHORT(ptr, 0))
	{
	case 1:
            if (hlpfile->lpszTitle) {WINE_WARN("title\n"); break;}
            hlpfile->lpszTitle = HeapAlloc(GetProcessHeap(), 0, strlen(ptr + 4) + 1);
            if (!hlpfile->lpszTitle) return FALSE;
            lstrcpy(hlpfile->lpszTitle, ptr + 4);
            WINE_TRACE("Title: %s\n", hlpfile->lpszTitle);
            break;

	case 2:
            if (GET_USHORT(ptr, 2) != 1 || ptr[4] != 0) WINE_WARN("system2\n");
            break;

	case 3:
            if (GET_USHORT(ptr, 2) != 4 || GET_UINT(ptr, 4) != 0)
                WINE_WARN("system3\n");
            break;

	case 4:
            macro = HeapAlloc(GetProcessHeap(), 0, sizeof(HLPFILE_MACRO) + lstrlen(ptr + 4) + 1);
            if (!macro) break;
            p = (char*)macro + sizeof(HLPFILE_MACRO);
            lstrcpy(p, (LPSTR)ptr + 4);
            macro->lpszMacro = p;
            macro->next = 0;
            for (m = &hlpfile->first_macro; *m; m = &(*m)->next);
            *m = macro;
            break;

	default:
            WINE_WARN("Unsupport SystemRecord[%d]\n", GET_USHORT(ptr, 0));
	}
    }
    return TRUE;
}

/***********************************************************************
 *
 *           HLPFILE_UncompressedLZ77_Size
 */
static INT HLPFILE_UncompressedLZ77_Size(BYTE *ptr, BYTE *end)
{
    int  i, newsize = 0;

    while (ptr < end)
    {
        int mask = *ptr++;
        for (i = 0; i < 8 && ptr < end; i++, mask >>= 1)
	{
            if (mask & 1)
	    {
                int code = GET_USHORT(ptr, 0);
                int len  = 3 + (code >> 12);
                newsize += len;
                ptr     += 2;
	    }
            else newsize++, ptr++;
	}
    }

    return newsize;
}

/***********************************************************************
 *
 *           HLPFILE_UncompressLZ77
 */
static BYTE *HLPFILE_UncompressLZ77(BYTE *ptr, BYTE *end, BYTE *newptr)
{
    int i;

    while (ptr < end)
    {
        int mask = *ptr++;
        for (i = 0; i < 8 && ptr < end; i++, mask >>= 1)
	{
            if (mask & 1)
	    {
                int code   = GET_USHORT(ptr, 0);
                int len    = 3 + (code >> 12);
                int offset = code & 0xfff;
                memcpy(newptr, newptr - offset - 1, len);
                newptr += len;
                ptr    += 2;
	    }
            else *newptr++ = *ptr++;
	}
    }

    return newptr;
}

/***********************************************************************
 *
 *           HLPFILE_UncompressLZ77_Phrases
 */
static BOOL HLPFILE_UncompressLZ77_Phrases(HLPFILE* hlpfile)
{
    UINT i, num, dec_size;
    BYTE *buf, *end;

    if (!HLPFILE_FindSubFile("Phrases", &buf, &end)) return FALSE;

    num = phrases.num = GET_USHORT(buf, 9);
    if (buf + 2 * num + 0x13 >= end) {WINE_WARN("1a\n"); return FALSE;};

    dec_size = HLPFILE_UncompressedLZ77_Size(buf + 0x13 + 2 * num, end);

    phrases.offsets = HeapAlloc(GetProcessHeap(), 0, sizeof(unsigned) * (num + 1));
    phrases.buffer  = HeapAlloc(GetProcessHeap(), 0, dec_size);
    if (!phrases.offsets || !phrases.buffer) return FALSE;

    for (i = 0; i <= num; i++)
        phrases.offsets[i] = GET_USHORT(buf, 0x11 + 2 * i) - 2 * num - 2;

    HLPFILE_UncompressLZ77(buf + 0x13 + 2 * num, end, phrases.buffer);

    hlpfile->hasPhrases = TRUE;
    return TRUE;
}

/***********************************************************************
 *
 *           HLPFILE_Uncompress_Phrases40
 */
static BOOL HLPFILE_Uncompress_Phrases40(HLPFILE* hlpfile)
{
    UINT num, dec_size;
    BYTE *buf_idx, *end_idx;
    BYTE *buf_phs, *end_phs;
    short i, n;
    long* ptr, mask = 0;
    unsigned short bc;

    if (!HLPFILE_FindSubFile("PhrIndex", &buf_idx, &end_idx) ||
        !HLPFILE_FindSubFile("PhrImage", &buf_phs, &end_phs)) return FALSE;

    ptr = (long*)(buf_idx + 9 + 28);
    bc = GET_USHORT(buf_idx, 9 + 24) & 0x0F;
    num = phrases.num = GET_USHORT(buf_idx, 9 + 4);

    WINE_TRACE("Index: Magic=%08x #entries=%u CpsdSize=%u PhrImgSize=%u\n"
               "\tPhrImgCprsdSize=%u 0=%u bc=%x ukn=%x\n",
               GET_UINT(buf_idx, 9 + 0),
               GET_UINT(buf_idx, 9 + 4),
               GET_UINT(buf_idx, 9 + 8),
               GET_UINT(buf_idx, 9 + 12),
               GET_UINT(buf_idx, 9 + 16),
               GET_UINT(buf_idx, 9 + 20),
               GET_USHORT(buf_idx, 9 + 24),
               GET_USHORT(buf_idx, 9 + 26));

    dec_size = GET_UINT(buf_idx, 9 + 12);
    if (dec_size != HLPFILE_UncompressedLZ77_Size(buf_phs + 9, end_phs))
    {
        WINE_WARN("size mismatch %u %u\n",
                  dec_size, HLPFILE_UncompressedLZ77_Size(buf_phs, end_phs));
        dec_size = max(dec_size, HLPFILE_UncompressedLZ77_Size(buf_phs, end_phs));
    }

    phrases.offsets = HeapAlloc(GetProcessHeap(), 0, sizeof(unsigned) * (num + 1));
    phrases.buffer  = HeapAlloc(GetProcessHeap(), 0, dec_size);
    if (!phrases.offsets || !phrases.buffer) return FALSE;

#define getbit() (ptr += (mask < 0), mask = mask*2 + (mask<=0), (*ptr & mask) != 0)

    phrases.offsets[0] = 0;
    for (i = 0; i < num; i++)
    {
        for (n = 1; getbit(); n += 1 << bc);
        if (getbit()) n++;
        if (bc > 1 && getbit()) n += 2;
        if (bc > 2 && getbit()) n += 4;
        if (bc > 3 && getbit()) n += 8;
        if (bc > 4 && getbit()) n += 16;
        phrases.offsets[i + 1] = phrases.offsets[i] + n;
    }
#undef getbit

    HLPFILE_UncompressLZ77(buf_phs + 9, end_phs, phrases.buffer);

    hlpfile->hasPhrases = FALSE;
    return TRUE;
}

/***********************************************************************
 *
 *           HLPFILE_UncompressLZ77_Topic
 */
static BOOL HLPFILE_UncompressLZ77_Topic(HLPFILE* hlpfile)
{
    BYTE *buf, *ptr, *end, *newptr;
    int  i, newsize = 0;

    if (!HLPFILE_FindSubFile("TOPIC", &buf, &end))
    {WINE_WARN("topic0\n"); return FALSE;}

    if (!(hlpfile->flags & 4)) WINE_FIXME("Unsupported format\n");

    buf += 9;
    topic.wMapLen = (end - buf - 1) / 0x1000 + 1;

    for (i = 0; i < topic.wMapLen; i++)
    {
        ptr = buf + i * 0x1000;

        /* I don't know why, it's necessary for printman.hlp */
        if (ptr + 0x44 > end) ptr = end - 0x44;

        newsize += HLPFILE_UncompressedLZ77_Size(ptr + 0xc, min(end, ptr + 0x1000));
    }

    topic.map = HeapAlloc(GetProcessHeap(), 0,
                          topic.wMapLen * sizeof(topic.map[0]) + newsize);
    if (!topic.map) return FALSE;
    newptr = (char*)topic.map + topic.wMapLen * sizeof(topic.map[0]);
    topic.end = newptr + newsize;

    for (i = 0; i < topic.wMapLen; i++)
    {
        ptr = buf + i * 0x1000;
        if (ptr + 0x44 > end) ptr = end - 0x44;

        topic.map[i] = newptr;
        newptr = HLPFILE_UncompressLZ77(ptr + 0xc, min(end, ptr + 0x1000), newptr);
    }

    return TRUE;
}

/***********************************************************************
 *
 *           HLPFILE_Uncompressed2_Size
 */
static UINT HLPFILE_Uncompressed2_Size(BYTE *ptr, BYTE *end)
{
    UINT wSize = 0;

    while (ptr < end)
    {
        if (!*ptr || *ptr >= 0x10)
            wSize++, ptr++;
        else
	{
            BYTE *phptr, *phend;
            UINT code  = 0x100 * ptr[0] + ptr[1];
            UINT index = (code - 0x100) / 2;

            if (index < phrases.num)
	    {
                phptr = phrases.buffer + phrases.offsets[index];
                phend = phrases.buffer + phrases.offsets[index + 1];

                if (phend < phptr) WINE_WARN("uncompress2a\n");

                wSize += phend - phptr;
                if (code & 1) wSize++;
	    }
            else WINE_WARN("uncompress2b %d|%d\n", index, phrases.num);

            ptr += 2;
	}
    }

    return wSize + 1;
}

/***********************************************************************
 *
 *           HLPFILE_Uncompress2
 */

static void HLPFILE_Uncompress2(BYTE **pptr, BYTE *end, BYTE *newptr)
{
    BYTE *ptr = *pptr;

    while (ptr < end)
    {
        if (!*ptr || *ptr >= 0x10)
            *newptr++ = *ptr++;
        else
	{
            BYTE *phptr, *phend;
            UINT code  = 0x100 * ptr[0] + ptr[1];
            UINT index = (code - 0x100) / 2;

            phptr = phrases.buffer + phrases.offsets[index];
            phend = phrases.buffer + phrases.offsets[index + 1];

            memcpy(newptr, phptr, phend - phptr);
            newptr += phend - phptr;
            if (code & 1) *newptr++ = ' ';

            ptr += 2;
	}
    }
    *newptr = '\0';
    *pptr = ptr;
}

/******************************************************************
 *		HLPFILE_Uncompress3
 *
 *
 */
static BOOL HLPFILE_Uncompress3(char* dst, const char* dst_end,
                                const BYTE* src, const BYTE* src_end)
{
    int         idx, len;

    for (; src < src_end; src++)
    {
        if ((*src & 1) == 0)
        {
            idx = *src / 2;
            if (idx > phrases.num) WINE_ERR("index in phrases\n");
            len = phrases.offsets[idx + 1] - phrases.offsets[idx];
            memcpy(dst, &phrases.buffer[phrases.offsets[idx]], len);
        }
        else if ((*src & 0x03) == 0x01)
        {
            idx = (*src + 1) * 64 + *++src;
            if (idx > phrases.num) WINE_ERR("index in phrases\n");
            len = phrases.offsets[idx + 1] - phrases.offsets[idx];
            memcpy(dst, &phrases.buffer[phrases.offsets[idx]], len);
        }
        else if ((*src & 0x07) == 0x03)
        {
            len = (*src / 8) + 1;
            memcpy(dst, src + 1, len);
            src += len;
        }
        else
        {
            len = (*src / 16) + 1;
            memset(dst, ((*src & 0x0F) == 0x07) ? ' ' : 0, len);
        }
        dst += len;
    }

    if (dst > dst_end) WINE_ERR("buffer overflow (%p > %p)\n", dst, dst_end);
    return TRUE;
}

/******************************************************************
 *		HLPFILE_UncompressRLE
 *
 *
 */
static void HLPFILE_UncompressRLE(const BYTE* src, unsigned sz, BYTE** dst)
{
    unsigned    i;
    BYTE        ch;

    for (i = 0; i < sz; i++)
    {
        ch = src[i];
        if (ch & 0x80)
        {
            ch &= 0x7F;
            memcpy(*dst, src + i + 1, ch);
            i += ch;
        }
        else
        {
            memset(*dst, (char)src[i + 1], ch);
            i++;
        }
        *dst += ch;
    }
}

/******************************************************************
 *		HLPFILE_EnumBTreeLeaves
 *
 *
 */
static void HLPFILE_EnumBTreeLeaves(const BYTE* buf, const BYTE* end, unsigned (*fn)(const BYTE*, void*), void* user)
{
    unsigned    psize, pnext;
    unsigned    num, nlvl;
    const BYTE* ptr;

    num    = GET_UINT(buf, 9 + 34);
    psize  = GET_USHORT(buf, 9 + 4);
    nlvl   = GET_USHORT(buf, 9 + 32);
    pnext  = GET_USHORT(buf, 26);

    WINE_TRACE("BTree: #entries=%u pagSize=%u #levels=%u #pages=%u root=%u struct%16s\n",
               num, psize, nlvl, GET_USHORT(buf, 9 + 30), pnext, buf + 9 + 6);
    if (!num) return;

    while (--nlvl > 0)
    {
        ptr = (buf + 9 + 38) + pnext * psize;
        WINE_TRACE("BTree: (index[%u]) unused=%u #entries=%u <%u\n",
                   pnext, GET_USHORT(ptr, 0), GET_USHORT(ptr, 2), GET_USHORT(ptr, 4));
        pnext = GET_USHORT(ptr, 6);
    }
    while (pnext != 0xFFFF)
    {
        const BYTE*     node_page;
        unsigned short  limit;

        node_page = ptr = (buf + 9 + 38) + pnext * psize;
        limit = GET_USHORT(ptr, 2);
        WINE_TRACE("BTree: (leaf [%u]) unused=%u #entries=%u <%u >%u\n",
                   pnext, GET_USHORT(ptr, 0), limit, GET_USHORT(ptr, 4), GET_USHORT(ptr, 6));
        ptr += 8;
        while (limit--)
            ptr += (fn)(ptr, user);
        pnext = GET_USHORT(node_page, 6);
    }
}

struct myfncb {
    HLPFILE*    hlpfile;
    int         i;
};

static unsigned myfn(const BYTE* ptr, void* user)
{
    struct myfncb*      m = user;

    m->hlpfile->Context[m->i].lHash  = GET_UINT(ptr, 0);
    m->hlpfile->Context[m->i].offset = GET_UINT(ptr, 4);
    m->i++;
    return 8;
}

/***********************************************************************
 *
 *           HLPFILE_GetContext
 */
static BOOL HLPFILE_GetContext(HLPFILE *hlpfile)
{
    BYTE                *cbuf, *cend;
    struct myfncb       m;
    unsigned            clen;

    if (!HLPFILE_FindSubFile("CONTEXT",  &cbuf, &cend)) {WINE_WARN("context0\n"); return FALSE;}

    clen = GET_UINT(cbuf, 0x2b);
    hlpfile->Context = HeapAlloc(GetProcessHeap(), 0, clen * sizeof(HLPFILE_CONTEXT));
    if (!hlpfile->Context) return FALSE;
    hlpfile->wContextLen = clen;

    m.hlpfile = hlpfile;
    m.i = 0;
    HLPFILE_EnumBTreeLeaves(cbuf, cend, myfn, &m);

    return TRUE;
}

/***********************************************************************
 *
 *           HLPFILE_DeleteParagraph
 */
static void HLPFILE_DeleteParagraph(HLPFILE_PARAGRAPH* paragraph)
{
    HLPFILE_PARAGRAPH* next;

    while (paragraph)
    {
        next = paragraph->next;
        if (paragraph->link) HeapFree(GetProcessHeap(), 0, paragraph->link);

        HeapFree(GetProcessHeap(), 0, paragraph);
        paragraph = next;
    }
}

/***********************************************************************
 *
 *           DeletePage
 */
static void HLPFILE_DeletePage(HLPFILE_PAGE* page)
{
    HLPFILE_PAGE* next;

    while (page)
    {
        next = page->next;
        HLPFILE_DeleteParagraph(page->first_paragraph);
        HeapFree(GetProcessHeap(), 0, page);
        page = next;
    }
}

/***********************************************************************
 *
 *           DeleteMacro
 */
static void HLPFILE_DeleteMacro(HLPFILE_MACRO* macro)
{
    HLPFILE_MACRO*      next;

    while (macro)
    {
        next = macro->next;
        HeapFree(GetProcessHeap(), 0, macro);
        macro = next;
    }
}

/***********************************************************************
 *
 *           HLPFILE_FreeHlpFile
 */
void HLPFILE_FreeHlpFile(HLPFILE* hlpfile)
{
    if (!hlpfile || --hlpfile->wRefCount > 0) return;

    if (hlpfile->next) hlpfile->next->prev = hlpfile->prev;
    if (hlpfile->prev) hlpfile->prev->next = hlpfile->next;
    else first_hlpfile = hlpfile->next;

    if (hlpfile->numFonts)
    {
        unsigned i;
        for (i = 0; i < hlpfile->numFonts; i++)
        {
            DeleteObject(hlpfile->fonts[i].hFont);
        }
        HeapFree(GetProcessHeap(), 0, hlpfile->fonts);
    }

    HLPFILE_DeletePage(hlpfile->first_page);
    HLPFILE_DeleteMacro(hlpfile->first_macro);

    if (hlpfile->Context)       HeapFree(GetProcessHeap(), 0, hlpfile->Context);
    if (hlpfile->lpszTitle)     HeapFree(GetProcessHeap(), 0, hlpfile->lpszTitle);
    HeapFree(GetProcessHeap(), 0, hlpfile);
}

/***********************************************************************
 *
 *           FreeHlpFilePage
 */
void HLPFILE_FreeHlpFilePage(HLPFILE_PAGE* page)
{
    if (!page) return;
    HLPFILE_FreeHlpFile(page->file);
}
