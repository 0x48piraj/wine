/*
 * Copyright 2006 Juan Lang for CodeWeavers
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
#include "wincrypt.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(crypt);

DWORD WINAPI CertRDNValueToStrA(DWORD dwValueType, PCERT_RDN_VALUE_BLOB pValue,
 LPSTR psz, DWORD csz)
{
    DWORD ret = 0;

    TRACE("(%ld, %p, %p, %ld)\n", dwValueType, pValue, psz, csz);

    switch (dwValueType)
    {
    case CERT_RDN_ANY_TYPE:
        break;
    case CERT_RDN_PRINTABLE_STRING:
    case CERT_RDN_IA5_STRING:
        if (!psz || !csz)
            ret = pValue->cbData;
        else
        {
            DWORD chars = min(pValue->cbData, csz - 1);

            if (chars)
            {
                memcpy(psz, pValue->pbData, chars);
                ret += chars;
                csz -= chars;
            }
        }
        break;
    default:
        FIXME("string type %ld unimplemented\n", dwValueType);
    }
    if (psz && csz)
    {
        *(psz + ret) = '\0';
        csz--;
        ret++;
    }
    else
        ret++;
    return ret;
}

DWORD WINAPI CertRDNValueToStrW(DWORD dwValueType, PCERT_RDN_VALUE_BLOB pValue,
 LPWSTR psz, DWORD csz)
{
    FIXME("(%ld, %p, %p, %ld): stub\n", dwValueType, pValue, psz, csz);
    return 0;
}


DWORD WINAPI CertNameToStrA(DWORD dwCertEncodingType, PCERT_NAME_BLOB pName,
 DWORD dwStrType, LPSTR psz, DWORD csz)
{
    static const DWORD unsupportedFlags = CERT_NAME_STR_NO_QUOTING_FLAG |
     CERT_NAME_STR_REVERSE_FLAG | CERT_NAME_STR_ENABLE_T61_UNICODE_FLAG;
    static const char commaSep[] = ", ";
    static const char semiSep[] = "; ";
    static const char crlfSep[] = "\r\n";
    static const char plusSep[] = " + ";
    static const char spaceSep[] = " ";
    DWORD ret = 0, bytes = 0;
    BOOL bRet;
    CERT_NAME_INFO *info;

    TRACE("(%ld, %p, %p, %ld)\n", dwCertEncodingType, pName, psz, csz);
    if (dwStrType & unsupportedFlags)
        FIXME("unsupported flags: %08lx\n", dwStrType & unsupportedFlags);

    bRet = CryptDecodeObjectEx(dwCertEncodingType, X509_NAME, pName->pbData,
     pName->cbData, CRYPT_DECODE_ALLOC_FLAG, NULL, &info, &bytes);
    if (bRet)
    {
        DWORD i, j, sepLen, rdnSepLen;
        LPCSTR sep, rdnSep;

        if (dwStrType & CERT_NAME_STR_SEMICOLON_FLAG)
            sep = semiSep;
        else if (dwStrType & CERT_NAME_STR_CRLF_FLAG)
            sep = crlfSep;
        else
            sep = commaSep;
        sepLen = strlen(sep);
        if (dwStrType & CERT_NAME_STR_NO_PLUS_FLAG)
            rdnSep = spaceSep;
        else
            rdnSep = plusSep;
        rdnSepLen = strlen(rdnSep);
        for (i = 0; ret < csz && i < info->cRDN; i++)
        {
            for (j = 0; ret < csz && j < info->rgRDN[i].cRDNAttr; j++)
            {
                DWORD chars;

                if ((dwStrType & 0x000000ff) == CERT_OID_NAME_STR)
                {
                    /* - 1 is needed to account for the NULL terminator. */
                    chars = min(
                     lstrlenA(info->rgRDN[i].rgRDNAttr[j].pszObjId),
                     csz - ret - 1);
                    if (psz && chars)
                        memcpy(psz + ret, info->rgRDN[i].rgRDNAttr[j].pszObjId,
                         chars);
                    ret += chars;
                    csz -= chars;
                    if (csz > 1)
                    {
                        if (psz)
                            *(psz + ret) = '=';
                        ret++;
                        csz--;
                    }
                }
                /* FIXME: handle quoting */
                chars = CertRDNValueToStrA(
                 info->rgRDN[i].rgRDNAttr[j].dwValueType, 
                 &info->rgRDN[i].rgRDNAttr[j].Value, psz ? psz + ret : NULL,
                 csz - ret - 1);
                if (chars)
                    ret += chars - 1;
                if (j < info->rgRDN[i].cRDNAttr - 1)
                {
                    if (psz && ret < csz - rdnSepLen - 1)
                        memcpy(psz + ret, rdnSep, rdnSepLen);
                    ret += rdnSepLen;
                }
            }
            if (i < info->cRDN - 1)
            {
                if (psz && ret < csz - sepLen - 1)
                    memcpy(psz + ret, sep, sepLen);
                ret += sepLen;
            }
        }
        LocalFree(info);
    }
    if (psz && csz)
    {
        *(psz + ret) = '\0';
        csz--;
        ret++;
    }
    else
        ret++;
    return ret;
}

DWORD WINAPI CertNameToStrW(DWORD dwCertEncodingType, PCERT_NAME_BLOB pName,
 DWORD dwStrType, LPWSTR psz, DWORD csz)
{
    FIXME("(%ld, %p, %p, %ld): stub\n", dwCertEncodingType, pName, psz, csz);
    return 0;
}
