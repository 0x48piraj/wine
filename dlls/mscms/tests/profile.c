/*
 * Tests for color profile functions
 *
 * Copyright 2004 Hans Leidekker
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

#include "windef.h"
#include "winbase.h"
#include "winnls.h"
#include "wingdi.h"
#include "winuser.h"
#include "icm.h"

#include "wine/test.h"

static const char machine[] = "dummy";
static const WCHAR machineW[] = { 'd','u','m','m','y',0 };

/*  To do any real functionality testing with this suite you need a copy of
 *  the freely distributable standard RGB color space profile. It's comes
 *  standard with Windows, but on Wine you probably need to install it yourself
 *  in one of the locations mentioned below. Here's a link to the profile in
 *  a self extracting zip file:
 *
 *  http://download.microsoft.com/download/whistler/hwdev1/1.0/wxp/en-us/ColorProfile.exe
 */

/* Two common places to find the standard color space profile */
static const char profile1[] =
"c:\\windows\\system\\color\\srgb color space profile.icm";
static const char profile2[] =
"c:\\windows\\system32\\spool\\drivers\\color\\srgb color space profile.icm";

static const WCHAR profile1W[] =
{ 'c',':','\\','w','i','n','d','o','w','s','\\', 's','y','s','t','e','m',
  '\\','c','o','l','o','r','\\','s','r','g','b',' ','c','o','l','o','r',' ',
  's','p','a','c','e',' ','p','r','o','f','i','l','e','.','i','c','m',0 };
static const WCHAR profile2W[] =
{ 'c',':','\\','w','i','n','d','o','w','s','\\', 's','y','s','t','e','m','3','2',
  '\\','s','p','o','o','l','\\','d','r','i','v','e','r','s',
  '\\','c','o','l','o','r','\\','s','r','g','b',' ','c','o','l','o','r',' ',
  's','p','a','c','e',' ','p','r','o','f','i','l','e','.','i','c','m',0 };

static const unsigned char rgbheader[] =
{ 0x48, 0x0c, 0x00, 0x00, 0x6f, 0x6e, 0x69, 0x4c, 0x00, 0x00, 0x10, 0x02,
  0x72, 0x74, 0x6e, 0x6d, 0x20, 0x42, 0x47, 0x52, 0x20, 0x5a, 0x59, 0x58,
  0x02, 0x00, 0xce, 0x07, 0x06, 0x00, 0x09, 0x00, 0x00, 0x00, 0x31, 0x00,
  0x70, 0x73, 0x63, 0x61, 0x54, 0x46, 0x53, 0x4d, 0x00, 0x00, 0x00, 0x00,
  0x20, 0x43, 0x45, 0x49, 0x42, 0x47, 0x52, 0x73, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xd6, 0xf6, 0x00, 0x00,
  0x00, 0x00, 0x01, 0x00, 0x2d, 0xd3, 0x00, 0x00, 0x20, 0x20, 0x50, 0x48 };

static LPSTR standardprofile;
static LPWSTR standardprofileW;

static LPSTR testprofile;
static LPWSTR testprofileW;

#define IS_SEPARATOR(ch)  ((ch) == '\\' || (ch) == '/')

static void MSCMS_basenameA( LPCSTR path, LPSTR name )
{
    INT i = strlen( path );

    while (i > 0 && !IS_SEPARATOR(path[i - 1])) i--;
    strcpy( name, &path[i] );
}

static void MSCMS_basenameW( LPCWSTR path, LPWSTR name )
{
    INT i = lstrlenW( path );

    while (i > 0 && !IS_SEPARATOR(path[i - 1])) i--;
    lstrcpyW( name, &path[i] );
}

static void test_GetColorDirectoryA()
{
    BOOL ret;
    DWORD size;
    char buffer[MAX_PATH];

    /* Parameter checks */

    ret = GetColorDirectoryA( NULL, NULL, NULL );
    ok( !ret, "GetColorDirectoryA() succeeded (%ld)\n", GetLastError() );

    size = 0;

    ret = GetColorDirectoryA( NULL, NULL, &size );
    ok( !ret && size > 0, "GetColorDirectoryA() succeeded (%ld)\n", GetLastError() );

    size = 0;

    ret = GetColorDirectoryA( NULL, buffer, &size );
    ok( !ret && size > 0, "GetColorDirectoryA() succeeded (%ld)\n", GetLastError() );

    size = 1;

    ret = GetColorDirectoryA( NULL, buffer, &size );
    ok( !ret && size > 0, "GetColorDirectoryA() succeeded (%ld)\n", GetLastError() );

    /* Functional checks */

    size = sizeof(buffer);

    ret = GetColorDirectoryA( NULL, buffer, &size );
    ok( ret && size > 0, "GetColorDirectoryA() failed (%ld)\n", GetLastError() );
}

static void test_GetColorDirectoryW()
{
    BOOL ret;
    DWORD size;
    WCHAR buffer[MAX_PATH];

    /* Parameter checks */

    /* This one crashes win2k
    
    ret = GetColorDirectoryW( NULL, NULL, NULL );
    ok( !ret, "GetColorDirectoryW() succeeded (%ld)\n", GetLastError() );

     */

    size = 0;

    ret = GetColorDirectoryW( NULL, NULL, &size );
    ok( !ret && size > 0, "GetColorDirectoryW() succeeded (%ld)\n", GetLastError() );

    size = 0;

    ret = GetColorDirectoryW( NULL, buffer, &size );
    ok( !ret && size > 0, "GetColorDirectoryW() succeeded (%ld)\n", GetLastError() );

    size = 1;

    ret = GetColorDirectoryW( NULL, buffer, &size );
    ok( !ret && size > 0, "GetColorDirectoryW() succeeded (%ld)\n", GetLastError() );

    /* Functional checks */

    size = sizeof(buffer);

    ret = GetColorDirectoryW( NULL, buffer, &size );
    ok( ret && size > 0, "GetColorDirectoryW() failed (%ld)\n", GetLastError() );
    ret = GetColorDirectoryW( NULL, buffer, &size );
}

static void test_GetColorProfileElement()
{
    if (standardprofile)
    {
        PROFILE profile;
        HPROFILE handle;
        BOOL ret, ref;
        DWORD size;
        TAGTYPE tag = 0x63707274;  /* 'cprt' */
        static char buffer[51];
        static const char expect[] =
            { 0x74, 0x65, 0x78, 0x74, 0x00, 0x00, 0x00, 0x00, 0x43, 0x6f, 0x70,
              0x79, 0x72, 0x69, 0x67, 0x68, 0x74, 0x20, 0x28, 0x63, 0x29, 0x20,
              0x31, 0x39, 0x39, 0x38, 0x20, 0x48, 0x65, 0x77, 0x6c, 0x65, 0x74,
              0x74, 0x2d, 0x50, 0x61, 0x63, 0x6b, 0x61, 0x72, 0x64, 0x20, 0x43,
              0x6f, 0x6d, 0x70, 0x61, 0x6e, 0x79, 0x00 };

        profile.dwType = PROFILE_FILENAME;
        profile.pProfileData = standardprofile;
        profile.cbDataSize = strlen(standardprofile);

        handle = OpenColorProfileA( &profile, PROFILE_READ, 0, OPEN_EXISTING );
        ok( handle != NULL, "OpenColorProfileA() failed (%ld)\n", GetLastError() );

        /* Parameter checks */

        ret = GetColorProfileElement( handle, tag, 0, NULL, NULL, &ref );
        ok( !ret, "GetColorProfileElement() succeeded (%ld)\n", GetLastError() );

        ret = GetColorProfileElement( handle, tag, 0, &size, NULL, NULL );
        ok( !ret, "GetColorProfileElement() succeeded (%ld)\n", GetLastError() );

        size = 0;

        ret = GetColorProfileElement( handle, tag, 0, &size, NULL, &ref );
        ok( !ret && size > 0, "GetColorProfileElement() succeeded (%ld)\n", GetLastError() );

        size = sizeof(buffer);

        /* Functional checks */

        ret = GetColorProfileElement( handle, tag, 0, &size, buffer, &ref );
        ok( ret && size > 0, "GetColorProfileElement() failed (%ld)\n", GetLastError() );

        ok( !memcmp( buffer, expect, sizeof(expect) ), "Unexpected tag data\n" );

        CloseColorProfile( handle );
    }
}

static void test_GetColorProfileElementTag()
{
    if (standardprofile)
    {
        PROFILE profile;
        HPROFILE handle;
        BOOL ret;
        DWORD index = 1;
        TAGTYPE tag, expect = 0x63707274;  /* 'cprt' */

        profile.dwType = PROFILE_FILENAME;
        profile.pProfileData = standardprofile;
        profile.cbDataSize = strlen(standardprofile);

        handle = OpenColorProfileA( &profile, PROFILE_READ, 0, OPEN_EXISTING );
        ok( handle != NULL, "OpenColorProfileA() failed (%ld)\n", GetLastError() );

        /* Parameter checks */

        ret = GetColorProfileElementTag( NULL, index, &tag );
        ok( !ret, "GetColorProfileElementTag() succeeded (%ld)\n", GetLastError() );

        ret = GetColorProfileElementTag( handle, 0, &tag );
        ok( !ret, "GetColorProfileElementTag() succeeded (%ld)\n", GetLastError() );

        ret = GetColorProfileElementTag( handle, index, NULL );
        ok( !ret, "GetColorProfileElementTag() succeeded (%ld)\n", GetLastError() );

        ret = GetColorProfileElementTag( handle, 18, NULL );
        ok( !ret, "GetColorProfileElementTag() succeeded (%ld)\n", GetLastError() );

        /* Functional checks */

        ret = GetColorProfileElementTag( handle, index, &tag );
        ok( ret && tag == expect, "GetColorProfileElementTag() failed (%ld)\n",
            GetLastError() );

        CloseColorProfile( handle );
    }
}

static void test_GetColorProfileFromHandle()
{
    if (testprofile)
    {
        PROFILE profile;
        HPROFILE handle;
        DWORD size;
        BOOL ret;
        static const unsigned char expect[] =
            { 0x00, 0x00, 0x0c, 0x48, 0x4c, 0x69, 0x6e, 0x6f, 0x02, 0x10, 0x00,
              0x00, 0x6d, 0x6e, 0x74, 0x72, 0x52, 0x47, 0x42, 0x20, 0x58, 0x59,
              0x5a, 0x20, 0x07, 0xce, 0x00, 0x02, 0x00, 0x09, 0x00, 0x06, 0x00,
              0x31, 0x00, 0x00, 0x61, 0x63, 0x73, 0x70, 0x4d, 0x53, 0x46, 0x54,
              0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x43, 0x20, 0x73, 0x52, 0x47,
              0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0xf6, 0xd6, 0x00, 0x01, 0x00, 0x00, 0x00,
              0x00, 0xd3, 0x2d, 0x48, 0x50, 0x20, 0x20 };

        unsigned char *buffer;

        profile.dwType = PROFILE_FILENAME;
        profile.pProfileData = testprofile;
        profile.cbDataSize = strlen(testprofile);

        handle = OpenColorProfileA( &profile, PROFILE_READ, 0, OPEN_EXISTING );
        ok( handle != NULL, "OpenColorProfileA() failed (%ld)\n", GetLastError() );

        /* Parameter checks */

        size = 0;

        ret = GetColorProfileFromHandle( handle, NULL, &size );
        ok( !ret && size > 0, "GetColorProfileFromHandle() failed (%ld)\n", GetLastError() );

        buffer = HeapAlloc( GetProcessHeap(), 0, size );

        if (buffer)
        {
            ret = GetColorProfileFromHandle( NULL, buffer, &size );
            ok( !ret, "GetColorProfileFromHandle() succeeded (%ld)\n", GetLastError() );

            ret = GetColorProfileFromHandle( handle, buffer, NULL );
            ok( !ret, "GetColorProfileFromHandle() succeeded (%ld)\n", GetLastError() );

            /* Functional checks */

            ret = GetColorProfileFromHandle( handle, buffer, &size );
            ok( ret && size > 0, "GetColorProfileFromHandle() failed (%ld)\n", GetLastError() );

            ok( !memcmp( buffer, expect, sizeof(expect) ), "Unexpected header data\n" );

            HeapFree( GetProcessHeap(), 0, buffer );
        }

        CloseColorProfile( handle );
    }
}

static void test_GetColorProfileHeader()
{
    if (testprofile)
    {
        PROFILE profile;
        HPROFILE handle;
        BOOL ret;
        PROFILEHEADER header;

        profile.dwType = PROFILE_FILENAME;
        profile.pProfileData = testprofile;
        profile.cbDataSize = strlen(testprofile);

        handle = OpenColorProfileA( &profile, PROFILE_READ, 0, OPEN_EXISTING );
        ok( handle != NULL, "OpenColorProfileA() failed (%ld)\n", GetLastError() );

        /* Parameter checks */

        ret = GetColorProfileHeader( NULL, NULL );
        ok( !ret, "GetColorProfileHeader() succeeded (%ld)\n", GetLastError() );

        ret = GetColorProfileHeader( NULL, &header );
        ok( !ret, "GetColorProfileHeader() succeeded (%ld)\n", GetLastError() );

        ret = GetColorProfileHeader( handle, NULL );
        ok( !ret, "GetColorProfileHeader() succeeded (%ld)\n", GetLastError() );

        /* Functional checks */

        ret = GetColorProfileHeader( handle, &header );
        ok( ret, "GetColorProfileHeader() failed (%ld)\n", GetLastError() );

        ok( !memcmp( &header, rgbheader, sizeof(rgbheader) ), "Unexpected header data\n" );

        CloseColorProfile( handle );
    }
}

static void test_GetCountColorProfileElements()
{
    if (standardprofile)
    {
        PROFILE profile;
        HPROFILE handle;
        BOOL ret;
        DWORD count, expect = 17;

        profile.dwType = PROFILE_FILENAME;
        profile.pProfileData = standardprofile;
        profile.cbDataSize = strlen(standardprofile);

        handle = OpenColorProfileA( &profile, PROFILE_READ, 0, OPEN_EXISTING );
        ok( handle != NULL, "OpenColorProfileA() failed (%ld)\n", GetLastError() );

        /* Parameter checks */

        ret = GetCountColorProfileElements( NULL, &count );
        ok( !ret, "GetCountColorProfileElements() succeeded (%ld)\n",
            GetLastError() );

        ret = GetCountColorProfileElements( handle, NULL );
        ok( !ret, "GetCountColorProfileElements() succeeded (%ld)\n",
            GetLastError() );

        /* Functional checks */

        ret = GetCountColorProfileElements( handle, &count );
        ok( ret && count == expect,
            "GetCountColorProfileElements() failed (%ld)\n", GetLastError() );

        CloseColorProfile( handle );
    }
}

static void test_GetStandardColorSpaceProfileA()
{
    BOOL ret;
    DWORD size;
    CHAR oldprofile[MAX_PATH];
    CHAR newprofile[MAX_PATH];

    /* Parameter checks */

    ret = GetStandardColorSpaceProfileA( NULL, 0, newprofile, NULL );
    ok( !ret, "GetStandardColorSpaceProfileA() succeeded (%ld)\n", GetLastError() );

    ret = GetStandardColorSpaceProfileA( machine, 0, newprofile, &size );
    ok( !ret, "GetStandardColorSpaceProfileA() succeeded (%ld)\n", GetLastError() );

    size = 0;

    ret = GetStandardColorSpaceProfileA( NULL, 0, NULL, &size );
    ok( !ret, "GetStandardColorSpaceProfileA() succeeded (%ld)\n", GetLastError() );

    size = sizeof(newprofile);

    ret = GetStandardColorSpaceProfileA( NULL, 0, newprofile, &size );
    ok( !ret, "GetStandardColorSpaceProfileA() succeeded (%ld)\n", GetLastError() );

    /* Functional checks */

    if (standardprofile)
    {
        size = sizeof(oldprofile);

        ret = GetStandardColorSpaceProfileA( NULL, SPACE_RGB, oldprofile, &size );
        ok( ret, "GetStandardColorSpaceProfileA() failed (%ld)\n", GetLastError() );

        ret = SetStandardColorSpaceProfileA( NULL, SPACE_RGB, standardprofile );
        ok( ret, "SetStandardColorSpaceProfileA() failed (%ld)\n", GetLastError() );

        size = sizeof(newprofile);

        ret = GetStandardColorSpaceProfileA( NULL, SPACE_RGB, newprofile, &size );
        ok( ret, "GetStandardColorSpaceProfileA() failed (%ld)\n", GetLastError() );

        ok( !lstrcmpiA( (LPSTR)&newprofile, standardprofile ), "Unexpected profile\n" );

        ret = SetStandardColorSpaceProfileA( NULL, SPACE_RGB, oldprofile );
        ok( ret, "SetStandardColorSpaceProfileA() failed (%ld)\n", GetLastError() );
    }
}

static void test_GetStandardColorSpaceProfileW()
{
    BOOL ret;
    DWORD size;
    WCHAR oldprofile[MAX_PATH];
    WCHAR newprofile[MAX_PATH];

    /* Parameter checks */

    ret = GetStandardColorSpaceProfileW( NULL, 0, newprofile, NULL );
    ok( !ret, "GetStandardColorSpaceProfileW() succeeded (%ld)\n", GetLastError() );

    ret = GetStandardColorSpaceProfileW( machineW, 0, newprofile, &size );
    ok( !ret, "GetStandardColorSpaceProfileW() succeeded (%ld)\n", GetLastError() );

    size = 0;

    ret = GetStandardColorSpaceProfileW( NULL, 0, NULL, &size );
    ok( !ret, "GetStandardColorSpaceProfileW() succeeded (%ld)\n", GetLastError() );

    size = sizeof(newprofile);

    ret = GetStandardColorSpaceProfileW( NULL, 0, newprofile, &size );
    ok( !ret, "GetStandardColorSpaceProfileW() succeeded (%ld)\n", GetLastError() );

    /* Functional checks */

    if (standardprofileW)
    {
        size = sizeof(oldprofile);

        ret = GetStandardColorSpaceProfileW( NULL, SPACE_RGB, oldprofile, &size );
        ok( ret, "GetStandardColorSpaceProfileW() failed (%ld)\n", GetLastError() );

        ret = SetStandardColorSpaceProfileW( NULL, SPACE_RGB, standardprofileW );
        ok( ret, "SetStandardColorSpaceProfileW() failed (%ld)\n", GetLastError() );

        size = sizeof(newprofile);

        ret = GetStandardColorSpaceProfileW( NULL, SPACE_RGB, newprofile, &size );
        ok( ret, "GetStandardColorSpaceProfileW() failed (%ld)\n", GetLastError() );

        ok( !lstrcmpiW( (LPWSTR)&newprofile, standardprofileW ), "Unexpected profile\n" );

        ret = SetStandardColorSpaceProfileW( NULL, SPACE_RGB, oldprofile );
        ok( ret, "SetStandardColorSpaceProfileW() failed (%ld)\n", GetLastError() );
    }
}

static void test_InstallColorProfileA()
{
    BOOL ret;

    /* Parameter checks */

    ret = InstallColorProfileA( NULL, NULL );
    ok( !ret, "InstallColorProfileA() succeeded (%ld)\n", GetLastError() );

    ret = InstallColorProfileA( machine, NULL );
    ok( !ret, "InstallColorProfileA() succeeded (%ld)\n", GetLastError() );

    ret = InstallColorProfileA( NULL, machine );
    ok( !ret, "InstallColorProfileA() succeeded (%ld)\n", GetLastError() );

    if (standardprofile)
    {
        ret = InstallColorProfileA( NULL, standardprofile );
        ok( ret, "InstallColorProfileA() failed (%ld)\n", GetLastError() );
    }

    /* Functional checks */

    if (testprofile)
    {
        CHAR dest[MAX_PATH], base[MAX_PATH];
        DWORD size = sizeof(dest);
        CHAR slash[] = "\\";
        HANDLE handle;

        ret = InstallColorProfileA( NULL, testprofile );
        ok( ret, "InstallColorProfileA() failed (%ld)\n", GetLastError() );

        ret = GetColorDirectoryA( NULL, dest, &size );
        ok( ret, "GetColorDirectoryA() failed (%ld)\n", GetLastError() );

        MSCMS_basenameA( testprofile, base );

        strcat( dest, slash );
        strcat( dest, base );

        /* Check if the profile is really there */ 
        handle = CreateFileA( dest, 0 , 0, NULL, OPEN_EXISTING, 0, NULL );
        ok( handle != INVALID_HANDLE_VALUE, "Couldn't find the profile (%ld)\n", GetLastError() );
        CloseHandle( handle );
        
        ret = UninstallColorProfileA( NULL, dest, TRUE );
        ok( ret, "UninstallColorProfileA() failed (%ld)\n", GetLastError() );
    }
}

static void test_InstallColorProfileW()
{
    BOOL ret;

    /* Parameter checks */

    ret = InstallColorProfileW( NULL, NULL );
    ok( !ret, "InstallColorProfileW() succeeded (%ld)\n", GetLastError() );

    ret = InstallColorProfileW( machineW, NULL );
    ok( !ret, "InstallColorProfileW() succeeded (%ld)\n", GetLastError() );

    ret = InstallColorProfileW( NULL, machineW );
    ok( !ret, "InstallColorProfileW() failed (%ld)\n", GetLastError() );

    if (standardprofileW)
    {
        ret = InstallColorProfileW( NULL, standardprofileW );
        ok( ret, "InstallColorProfileW() failed (%ld)\n", GetLastError() );
    }

    /* Functional checks */

    if (testprofileW)
    {
        WCHAR dest[MAX_PATH], base[MAX_PATH];
        DWORD size = sizeof(dest);
        WCHAR slash[] = { '\\', 0 };
        HANDLE handle;

        ret = InstallColorProfileW( NULL, testprofileW );
        ok( ret, "InstallColorProfileW() failed (%ld)\n", GetLastError() );

        ret = GetColorDirectoryW( NULL, dest, &size );
        ok( ret, "GetColorDirectoryW() failed (%ld)\n", GetLastError() );

        MSCMS_basenameW( testprofileW, base );

        lstrcatW( dest, slash );
        lstrcatW( dest, base );

        /* Check if the profile is really there */
        handle = CreateFileW( dest, 0 , 0, NULL, OPEN_EXISTING, 0, NULL );
        ok( handle != INVALID_HANDLE_VALUE, "Couldn't find the profile (%ld)\n", GetLastError() );
        CloseHandle( handle );

        ret = UninstallColorProfileW( NULL, dest, TRUE );
        ok( ret, "UninstallColorProfileW() failed (%ld)\n", GetLastError() );
    }
}

static void test_IsColorProfileTagPresent()
{
    if (standardprofile)
    {
        PROFILE profile;
        HPROFILE handle;
        BOOL ret, present;
        TAGTYPE tag;

        profile.dwType = PROFILE_FILENAME;
        profile.pProfileData = standardprofile;
        profile.cbDataSize = strlen(standardprofile);

        handle = OpenColorProfileA( &profile, PROFILE_READ, 0, OPEN_EXISTING );
        ok( handle != NULL, "OpenColorProfileA() failed (%ld)\n", GetLastError() );

        /* Parameter checks */

        tag = 0;

        ret = IsColorProfileTagPresent( handle, tag, &present );
        ok( !(ret && present), "IsColorProfileTagPresent() succeeded (%ld)\n", GetLastError() );

        tag = 0x63707274;  /* 'cprt' */

        ret = IsColorProfileTagPresent( NULL, tag, &present );
        ok( !ret, "IsColorProfileTagPresent() succeeded (%ld)\n", GetLastError() );

        ret = IsColorProfileTagPresent( handle, tag, NULL );
        ok( !ret, "IsColorProfileTagPresent() succeeded (%ld)\n", GetLastError() );

        /* Functional checks */

        ret = IsColorProfileTagPresent( handle, tag, &present );
        ok( ret && present, "IsColorProfileTagPresent() failed (%ld)\n", GetLastError() );

        CloseColorProfile( handle );
    }
}

static void test_OpenColorProfileA()
{
    PROFILE profile;
    HPROFILE handle;

    profile.dwType = PROFILE_FILENAME;
    profile.pProfileData = NULL;
    profile.cbDataSize = 0;

    /* Parameter checks */

    handle = OpenColorProfileA( NULL, 0, 0, 0 );
    ok( handle == NULL, "OpenColorProfileA() failed (%ld)\n", GetLastError() );

    handle = OpenColorProfileA( &profile, 0, 0, 0 );
    ok( handle == NULL, "OpenColorProfileA() failed (%ld)\n", GetLastError() );

    handle = OpenColorProfileA( &profile, PROFILE_READ, 0, 0 );
    ok( handle == NULL, "OpenColorProfileA() failed (%ld)\n", GetLastError() );

    handle = OpenColorProfileA( &profile, PROFILE_READWRITE, 0, 0 );
    ok( handle == NULL, "OpenColorProfileA() failed (%ld)\n", GetLastError() );

    ok ( !CloseColorProfile( NULL ), "CloseColorProfile() succeeded\n" );

    if (standardprofile)
    {
        profile.pProfileData = standardprofile;
        profile.cbDataSize = strlen(standardprofile);

        handle = OpenColorProfileA( &profile, 0, 0, 0 );
        ok( handle == NULL, "OpenColorProfileA() failed (%ld)\n", GetLastError() );

        handle = OpenColorProfileA( &profile, PROFILE_READ, 0, 0 );
        ok( handle == NULL, "OpenColorProfileA() failed (%ld)\n", GetLastError() );

        handle = OpenColorProfileA( &profile, PROFILE_READ|PROFILE_READWRITE, 0, 0 );
        ok( handle == NULL, "OpenColorProfileA() failed (%ld)\n", GetLastError() );

        /* Functional checks */

        handle = OpenColorProfileA( &profile, PROFILE_READ, 0, OPEN_EXISTING );
        ok( handle != NULL, "OpenColorProfileA() failed (%ld)\n", GetLastError() );

        ok( CloseColorProfile( handle ), "CloseColorProfile() failed (%ld)\n", GetLastError() );
    }
}

static void test_OpenColorProfileW()
{
    PROFILE profile;
    HPROFILE handle;

    profile.dwType = PROFILE_FILENAME;
    profile.pProfileData = NULL;
    profile.cbDataSize = 0;

    /* Parameter checks */

    handle = OpenColorProfileW( NULL, 0, 0, 0 );
    ok( handle == NULL, "OpenColorProfileW() failed (%ld)\n", GetLastError() );

    handle = OpenColorProfileW( &profile, 0, 0, 0 );
    ok( handle == NULL, "OpenColorProfileW() failed (%ld)\n", GetLastError() );

    handle = OpenColorProfileW( &profile, PROFILE_READ, 0, 0 );
    ok( handle == NULL, "OpenColorProfileW() failed (%ld)\n", GetLastError() );

    handle = OpenColorProfileW( &profile, PROFILE_READWRITE, 0, 0 );
    ok( handle == NULL, "OpenColorProfileW() failed (%ld)\n", GetLastError() );

    ok ( !CloseColorProfile( NULL ), "CloseColorProfile() succeeded\n" );

    if (standardprofileW)
    {
        profile.pProfileData = standardprofileW;
        profile.cbDataSize = lstrlenW(standardprofileW) * sizeof(WCHAR);

        handle = OpenColorProfileW( &profile, 0, 0, 0 );
        ok( handle == NULL, "OpenColorProfileW() failed (%ld)\n", GetLastError() );

        handle = OpenColorProfileW( &profile, PROFILE_READ, 0, 0 );
        ok( handle == NULL, "OpenColorProfileW() failed (%ld)\n", GetLastError() );

        handle = OpenColorProfileW( &profile, PROFILE_READ|PROFILE_READWRITE, 0, 0 );
        ok( handle == NULL, "OpenColorProfileW() failed (%ld)\n", GetLastError() );

        /* Functional checks */

        handle = OpenColorProfileW( &profile, PROFILE_READ, 0, OPEN_EXISTING );
        ok( handle != NULL, "OpenColorProfileW() failed (%ld)\n", GetLastError() );

        ok( CloseColorProfile( handle ), "CloseColorProfile() failed (%ld)\n", GetLastError() );
    }
}

static void test_SetColorProfileElement()
{
    if (testprofile)
    {
        PROFILE profile;
        HPROFILE handle;
        DWORD size;
        BOOL ret, ref;

        TAGTYPE tag = 0x63707274;  /* 'cprt' */
        static char data[] = "(c) The Wine Project";
        static char buffer[51];

        profile.dwType = PROFILE_FILENAME;
        profile.pProfileData = testprofile;
        profile.cbDataSize = strlen(testprofile);

        handle = OpenColorProfileA( &profile, PROFILE_READ, 0, OPEN_EXISTING );
        ok( handle != NULL, "OpenColorProfileA() failed (%ld)\n", GetLastError() );

        todo_wine
        {
        ret = SetColorProfileElement( handle, tag, 0, &size, data );
        ok( !ret, "SetColorProfileElement() succeeded (%ld)\n", GetLastError() );
        }

        CloseColorProfile( handle );

        handle = OpenColorProfileA( &profile, PROFILE_READWRITE, 0, OPEN_EXISTING );
        ok( handle != NULL, "OpenColorProfileA() failed (%ld)\n", GetLastError() );

        /* Parameter checks */

        ret = SetColorProfileElement( NULL, 0, 0, NULL, NULL );
        ok( !ret, "SetColorProfileElement() succeeded (%ld)\n", GetLastError() );

        ret = SetColorProfileElement( handle, 0, 0, NULL, NULL );
        ok( !ret, "SetColorProfileElement() succeeded (%ld)\n", GetLastError() );

        ret = SetColorProfileElement( handle, tag, 0, NULL, NULL );
        ok( !ret, "SetColorProfileElement() succeeded (%ld)\n", GetLastError() );

        ret = SetColorProfileElement( handle, tag, 0, &size, NULL );
        ok( !ret, "SetColorProfileElement() succeeded (%ld)\n", GetLastError() );

        /* Functional checks */

        ret = SetColorProfileElement( handle, tag, 0, &size, data );
        ok( ret, "SetColorProfileElement() failed (%ld)\n", GetLastError() );

        size = sizeof(buffer);

        ret = GetColorProfileElement( handle, tag, 0, &size, buffer, &ref );
        ok( ret && size > 0, "GetColorProfileElement() failed (%ld)\n", GetLastError() );

        ok( !memcmp( data, buffer, sizeof(data) ), "Unexpected tag data\n" );

        CloseColorProfile( handle );
    }
}

static void test_SetColorProfileHeader()
{
    if (testprofile)
    {
        PROFILE profile;
        HPROFILE handle;
        BOOL ret;
        PROFILEHEADER header;

        profile.dwType = PROFILE_FILENAME;
        profile.pProfileData = testprofile;
        profile.cbDataSize = strlen(testprofile);

        header.phSize = 0x00000c48;
        header.phCMMType = 0x4c696e6f;
        header.phVersion = 0x02100000;
        header.phClass = 0x6d6e7472;
        header.phDataColorSpace = 0x52474220;
        header.phConnectionSpace  = 0x58595a20;
        header.phDateTime[0] = 0x07ce0002;
        header.phDateTime[1] = 0x00090006;
        header.phDateTime[2] = 0x00310000;
        header.phSignature = 0x61637370;
        header.phPlatform = 0x4d534654;
        header.phProfileFlags = 0x00000000;
        header.phManufacturer = 0x49454320;
        header.phModel = 0x73524742;
        header.phAttributes[0] = 0x00000000;
        header.phAttributes[1] = 0x00000000;
        header.phRenderingIntent = 0x00000000;
        header.phIlluminant.ciexyzX = 0x0000f6d6;
        header.phIlluminant.ciexyzY = 0x00010000;
        header.phIlluminant.ciexyzZ = 0x0000d32d;
        header.phCreator = 0x48502020;

        /* Parameter checks */

        handle = OpenColorProfileA( &profile, PROFILE_READ, 0, OPEN_EXISTING );
        ok( handle != NULL, "OpenColorProfileA() failed (%ld)\n", GetLastError() );

        todo_wine
        {
        ret = SetColorProfileHeader( handle, &header );
        ok( !ret, "SetColorProfileHeader() succeeded (%ld)\n", GetLastError() );
        }

        CloseColorProfile( handle );

        handle = OpenColorProfileA( &profile, PROFILE_READWRITE, 0, OPEN_EXISTING );
        ok( handle != NULL, "OpenColorProfileA() failed (%ld)\n", GetLastError() );

        ret = SetColorProfileHeader( NULL, NULL );
        ok( !ret, "SetColorProfileHeader() succeeded (%ld)\n", GetLastError() );

        ret = SetColorProfileHeader( handle, NULL );
        ok( !ret, "SetColorProfileHeader() succeeded (%ld)\n", GetLastError() );

        ret = SetColorProfileHeader( NULL, &header );
        ok( !ret, "SetColorProfileHeader() succeeded (%ld)\n", GetLastError() );

        /* Functional checks */

        ret = SetColorProfileHeader( handle, &header );
        ok( ret, "SetColorProfileHeader() failed (%ld)\n", GetLastError() );

        ret = GetColorProfileHeader( handle, &header );
        ok( ret, "GetColorProfileHeader() failed (%ld)\n", GetLastError() );

        ok( !memcmp( &header, rgbheader, sizeof(rgbheader) ), "Unexpected header data\n" );

        CloseColorProfile( handle );
    }
}

static void test_UninstallColorProfileA()
{
    BOOL ret;

    /* Parameter checks */

    ret = UninstallColorProfileA( NULL, NULL, FALSE );
    ok( !ret, "UninstallColorProfileA() succeeded (%ld)\n", GetLastError() );

    ret = UninstallColorProfileA( machine, NULL, FALSE );
    ok( !ret, "UninstallColorProfileA() succeeded (%ld)\n", GetLastError() );

    /* Functional checks */

    if (testprofile)
    {
        CHAR dest[MAX_PATH], base[MAX_PATH];
        DWORD size = sizeof(dest);
        CHAR slash[] = "\\";
        HANDLE handle;

        ret = InstallColorProfileA( NULL, testprofile );
        ok( ret, "InstallColorProfileA() failed (%ld)\n", GetLastError() );

        ret = GetColorDirectoryA( NULL, dest, &size );
        ok( ret, "GetColorDirectoryA() failed (%ld)\n", GetLastError() );

        MSCMS_basenameA( testprofile, base );

        strcat( dest, slash );
        strcat( dest, base );

        ret = UninstallColorProfileA( NULL, dest, TRUE );
        ok( ret, "UninstallColorProfileA() failed (%ld)\n", GetLastError() );

        /* Check if the profile is really gone */
        handle = CreateFileA( dest, 0 , 0, NULL, OPEN_EXISTING, 0, NULL );
        ok( handle == INVALID_HANDLE_VALUE, "Found the profile (%ld)\n", GetLastError() );
        CloseHandle( handle );
    }
}

static void test_UninstallColorProfileW()
{
    BOOL ret;

    /* Parameter checks */

    ret = UninstallColorProfileW( NULL, NULL, FALSE );
    ok( !ret, "UninstallColorProfileW() succeeded (%ld)\n", GetLastError() );

    ret = UninstallColorProfileW( machineW, NULL, FALSE );
    ok( !ret, "UninstallColorProfileW() succeeded (%ld)\n", GetLastError() );

    /* Functional checks */

    if (testprofileW)
    {
        WCHAR dest[MAX_PATH], base[MAX_PATH];
        DWORD size = sizeof(dest);
        WCHAR slash[] = { '\\', 0 };
        HANDLE handle;

        ret = InstallColorProfileW( NULL, testprofileW );
        ok( ret, "InstallColorProfileW() failed (%ld)\n", GetLastError() );

        ret = GetColorDirectoryW( NULL, dest, &size );
        ok( ret, "GetColorDirectoryW() failed (%ld)\n", GetLastError() );

        MSCMS_basenameW( testprofileW, base );

        lstrcatW( dest, slash );
        lstrcatW( dest, base );

        ret = UninstallColorProfileW( NULL, dest, TRUE );
        ok( ret, "UninstallColorProfileW() failed (%ld)\n", GetLastError() );

        /* Check if the profile is really gone */
        handle = CreateFileW( dest, 0 , 0, NULL, OPEN_EXISTING, 0, NULL );
        ok( handle == INVALID_HANDLE_VALUE, "Found the profile (%ld)\n", GetLastError() );
        CloseHandle( handle );
    }
}

START_TEST(profile)
{
    UINT len;
    HANDLE handle;
    char path[MAX_PATH], file[MAX_PATH];
    WCHAR fileW[MAX_PATH];

    /* See if we can find the standard color profile */
    handle = CreateFileA( profile1, 0 , 0, NULL, OPEN_EXISTING, 0, NULL );

    if (handle != INVALID_HANDLE_VALUE)
    {
        standardprofile = (LPSTR)&profile1;
        standardprofileW = (LPWSTR)&profile1W;
        CloseHandle( handle );
    }

    handle = CreateFileA( profile2, 0 , 0, NULL, OPEN_EXISTING, 0, NULL );

    if (handle != INVALID_HANDLE_VALUE)
    {
        standardprofile = (LPSTR)&profile2;
        standardprofileW = (LPWSTR)&profile2W;
        CloseHandle( handle );
    }

    /* If found, create a temporary copy for testing purposes */
    if (standardprofile && GetTempPath( sizeof(path), path ))
    {
        if (GetTempFileName( path, "rgb", 0, file ))
        {
            if (CopyFileA( standardprofile, file, FALSE ))
            {
                testprofile = (LPSTR)&file;

                len = MultiByteToWideChar( CP_ACP, 0, testprofile, -1, NULL, 0 );
                MultiByteToWideChar( CP_ACP, 0, testprofile, -1, fileW, len );

                testprofileW = (LPWSTR)&fileW;
            }
        }
    }

    test_GetColorDirectoryA();
    test_GetColorDirectoryW();

    test_GetColorProfileElement();
    test_GetColorProfileElementTag();

    test_GetColorProfileFromHandle();
    test_GetColorProfileHeader();

    test_GetCountColorProfileElements();

    test_GetStandardColorSpaceProfileA();
    test_GetStandardColorSpaceProfileW();

    test_InstallColorProfileA();
    test_InstallColorProfileW();

    test_IsColorProfileTagPresent();

    test_OpenColorProfileA();
    test_OpenColorProfileW();

    test_SetColorProfileElement();
    test_SetColorProfileHeader();

    test_UninstallColorProfileA();
    test_UninstallColorProfileW();

    /* Clean up */
    if (testprofile)
        DeleteFileA( testprofile );
}
