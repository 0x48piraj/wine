/* 
 * Wininet - Utility functions
 *
 * Copyright 1999 Corel Corporation
 *
 * Ulrich Czekalla
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

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "windef.h"
#include "winbase.h"
#include "wininet.h"
#include "winerror.h"

#include "wine/debug.h"
#include "internet.h"

WINE_DEFAULT_DEBUG_CHANNEL(wininet);

#define TIME_STRING_LEN  30

time_t ConvertTimeString(LPCSTR asctime)
{
    char tmpChar[TIME_STRING_LEN];
    char *tmpChar2;
    struct tm t;
    int timelen = strlen(asctime);

    if(!asctime || !timelen)
        return 0;

    strncpy(tmpChar, asctime, TIME_STRING_LEN);

    /* Assert that the string is the expected length */
    if (tmpChar[TIME_STRING_LEN] != '\0')
    { 
        tmpChar[TIME_STRING_LEN] = '\0';
        FIXME("\n");
    }
    
    /* Convert a time such as 'Mon, 15 Nov 1999 16:09:35 GMT' into a SYSTEMTIME structure
     * We assume the time is in this format
     * and divide it into easy to swallow chunks
     */
    tmpChar[3]='\0';
    tmpChar[7]='\0';
    tmpChar[11]='\0';
    tmpChar[16]='\0';
    tmpChar[19]='\0';
    tmpChar[22]='\0';
    tmpChar[25]='\0';

    t.tm_year = atoi(tmpChar+12) - 1900;
    t.tm_mday = atoi(tmpChar+5);
    t.tm_hour = atoi(tmpChar+17);
    t.tm_min = atoi(tmpChar+20);
    t.tm_sec = atoi(tmpChar+23);
	
    /* and month */
    tmpChar2 = tmpChar + 8;
    switch(tmpChar2[2])
    {
        case 'n':
            if(tmpChar2[1]=='a')
                t.tm_mon = 0;
            else
                t.tm_mon = 5;
            break;
        case 'b':
            t.tm_mon = 1;
            break;
        case 'r':
            if(tmpChar2[1]=='a')
                t.tm_mon = 2;
            else
                t.tm_mon = 3;
            break;
        case 'y':
            t.tm_mon = 4;
            break;
        case 'l':
            t.tm_mon = 6;
            break;
        case 'g':
            t.tm_mon = 7;
            break;
        case 'p':
            t.tm_mon = 8;
            break;
        case 't':
            t.tm_mon = 9;
            break;
        case 'v':
            t.tm_mon = 10;
            break;
        case 'c':
            t.tm_mon = 11;
            break;
        default:
            FIXME("\n");
    }

    return mktime(&t);
}


BOOL GetAddress(LPCSTR lpszServerName, INTERNET_PORT nServerPort,
	struct hostent **phe, struct sockaddr_in *psa)
{
    char *found;

    TRACE("%s\n", lpszServerName);

    /* Validate server name first
     * Check if there is sth. like
     * pinger.macromedia.com:80
     * if yes, eliminate the :80....
     */
    found = strchr(lpszServerName, ':');
    if (found)
    {
        int len = found - lpszServerName;
        char *new = HeapAlloc(GetProcessHeap(), 0, len + 1);
        memcpy( new, lpszServerName, len );
        new[len] = '\0';
        TRACE("Found a ':' inside the server name, reparsed name: %s\n", new);
        *phe = gethostbyname(new);
        HeapFree( GetProcessHeap(), 0, new );
    }
    else *phe = gethostbyname(lpszServerName);

    if (NULL == *phe)
    {
        TRACE("Failed to get hostname: (%s)\n", lpszServerName);
        return FALSE;
    }

    memset(psa,0,sizeof(struct sockaddr_in));
    memcpy((char *)&psa->sin_addr, (*phe)->h_addr, (*phe)->h_length);
    psa->sin_family = (*phe)->h_addrtype;
    psa->sin_port = htons((u_short)nServerPort);

    return TRUE;
}
