/*
 * Copyright 2002 Mike McCormack for CodeWeavers
 * Copyright (C) 2004 Juan Lang
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
#include "winreg.h"
#include "wincrypt.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(crypt);

#define WINE_CRYPTCERTSTORE_MAGIC 0x74726563

typedef struct WINE_CRYPTCERTSTORE
{
    DWORD dwMagic;
} WINECRYPT_CERTSTORE;


/*
 * CertOpenStore
 *
 * System Store CA is
 *  HKLM\\Software\\Microsoft\\SystemCertificates\\CA\\
 *    Certificates\\<compressed guid>
 *         "Blob" = REG_BINARY
 *    CRLs\\<compressed guid>
 *         "Blob" = REG_BINARY
 *    CTLs\\<compressed guid>
 *         "Blob" = REG_BINARY
 */
HCERTSTORE WINAPI CertOpenStore( LPCSTR lpszStoreProvider,
              DWORD dwMsgAndCertEncodingType, HCRYPTPROV hCryptProv, 
              DWORD dwFlags, const void* pvPara )
{
    WINECRYPT_CERTSTORE *hcs;

    FIXME("%s %08lx %08lx %08lx %p stub\n", debugstr_a(lpszStoreProvider),
          dwMsgAndCertEncodingType, hCryptProv, dwFlags, pvPara);

    if( lpszStoreProvider == (LPCSTR) 0x0009 )
    {
        FIXME("pvPara = %s\n", debugstr_a( (LPCSTR) pvPara ) );
    }

    hcs = HeapAlloc( GetProcessHeap(), 0, sizeof (WINECRYPT_CERTSTORE) );
    if( !hcs )
        return NULL;

    hcs->dwMagic = WINE_CRYPTCERTSTORE_MAGIC;

    return (HCERTSTORE) hcs;
}

HCERTSTORE WINAPI CertOpenSystemStoreA(HCRYPTPROV hProv,
 LPCSTR szSubSystemProtocol)
{
    FIXME("(%ld, %s), stub\n", hProv, debugstr_a(szSubSystemProtocol));
    return (HCERTSTORE)1;
}

HCERTSTORE WINAPI CertOpenSystemStoreW(HCRYPTPROV hProv,
 LPCWSTR szSubSystemProtocol)
{
    FIXME("(%ld, %s), stub\n", hProv, debugstr_w(szSubSystemProtocol));
    return (HCERTSTORE)1;
}

PCCERT_CONTEXT WINAPI CertEnumCertificatesInStore(HCERTSTORE hCertStore, PCCERT_CONTEXT pPrev)
{
    FIXME("(%p,%p)\n", hCertStore, pPrev);
    return NULL;
}

BOOL WINAPI CertSaveStore(HCERTSTORE hCertStore, DWORD dwMsgAndCertEncodingType,
             DWORD dwSaveAs, DWORD dwSaveTo, void* pvSaveToPara, DWORD dwFlags)
{
    FIXME("(%p,%ld,%ld,%ld,%p,%08lx) stub!\n", hCertStore, 
          dwMsgAndCertEncodingType, dwSaveAs, dwSaveTo, pvSaveToPara, dwFlags);
    return TRUE;
}

PCCRL_CONTEXT WINAPI CertCreateCRLContext( DWORD dwCertEncodingType,
  const BYTE* pbCrlEncoded, DWORD cbCrlEncoded)
{
    PCRL_CONTEXT pcrl;
    BYTE* data;

    TRACE("%08lx %p %08lx\n", dwCertEncodingType, pbCrlEncoded, cbCrlEncoded);

    pcrl = HeapAlloc( GetProcessHeap(), 0, sizeof (CRL_CONTEXT) );
    if( !pcrl )
        return NULL;

    data = HeapAlloc( GetProcessHeap(), 0, cbCrlEncoded );
    if( !data )
    {
        HeapFree( GetProcessHeap(), 0, pcrl );
        return NULL;
    }

    pcrl->dwCertEncodingType = dwCertEncodingType;
    pcrl->pbCrlEncoded       = data;
    pcrl->cbCrlEncoded       = cbCrlEncoded;
    pcrl->pCrlInfo           = NULL;
    pcrl->hCertStore         = 0;

    return pcrl;
}

BOOL WINAPI CertAddCRLContextToStore( HCERTSTORE hCertStore,
             PCCRL_CONTEXT pCrlContext, DWORD dwAddDisposition,
             PCCRL_CONTEXT* ppStoreContext )
{
    FIXME("%p %p %08lx %p\n", hCertStore, pCrlContext,
          dwAddDisposition, ppStoreContext);
    return TRUE;
}

BOOL WINAPI CertFreeCRLContext( PCCRL_CONTEXT pCrlContext)
{
    FIXME("%p\n", pCrlContext );

    return TRUE;
}

BOOL WINAPI CertCloseStore( HCERTSTORE hCertStore, DWORD dwFlags )
{
    FIXME("%p %08lx\n", hCertStore, dwFlags );
    if( ! hCertStore )
        return FALSE;

    HeapFree( GetProcessHeap(), 0, hCertStore );

    return TRUE;
}

BOOL WINAPI CertFreeCertificateContext( PCCERT_CONTEXT pCertContext )
{
    FIXME("%p stub\n", pCertContext);
    return TRUE;
}

PCCERT_CONTEXT WINAPI CertFindCertificateInStore(HCERTSTORE hCertStore,
		DWORD dwCertEncodingType, DWORD dwFlags, DWORD dwType,
		const void *pvPara, PCCERT_CONTEXT pPrevCertContext)
{
    FIXME("stub: %p %ld %ld %ld %p %p\n", hCertStore, dwCertEncodingType,
	dwFlags, dwType, pvPara, pPrevCertContext);
    SetLastError(CRYPT_E_NOT_FOUND);
    return NULL;
}
