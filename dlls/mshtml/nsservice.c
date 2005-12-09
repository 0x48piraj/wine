/*
 * Copyright 2005 Jacek Caban
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

#include <stdarg.h>

#define COBJMACROS

#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "winreg.h"
#include "ole2.h"

#include "wine/debug.h"
#include "wine/unicode.h"

#include "mshtml_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(mshtml);

#define NS_PROMPTSERVICE_CONTRACTID "@mozilla.org/embedcomp/prompt-service;1"

static const nsIID NS_PROMPTSERVICE_CID =
    {0xa2112d6a,0x0e28,0x421f,{0xb4,0x6a,0x25,0xc0,0xb3,0x8,0xcb,0xd0}};

static nsresult NSAPI nsPromptService_QueryInterface(nsIPromptService *iface,
                                                     nsIIDRef riid, nsQIResult result)
{
    *result = NULL;

    if(IsEqualGUID(&IID_nsISupports, riid)) {
        TRACE("(IID_nsISupports %p)\n", result);
        *result = iface;
    }else if(IsEqualGUID(&IID_nsIPromptService, riid)) {
        TRACE("(IID_nsIPromptService %p)\n", result);
        *result = iface;
    }

    if(*result)
        return NS_OK;

    TRACE("(%s %p)\n", debugstr_guid(riid), result);
    return NS_NOINTERFACE;
}

static nsrefcnt NSAPI nsPromptService_AddRef(nsIPromptService *iface)
{
    return 2;
}

static nsrefcnt NSAPI nsPromptService_Release(nsIPromptService *iface)
{
    return 1;
}

static nsresult NSAPI nsPromptService_Alert(nsIPromptService *iface, nsIDOMWindow *aParent,
        const PRUnichar *aDialogTitle, const PRUnichar *aText)
{
    FIXME("(%p %s %s)\n", aParent, debugstr_w(aDialogTitle), debugstr_w(aText));
    return NS_ERROR_NOT_IMPLEMENTED;
}

static nsresult NSAPI nsPromptService_AlertCheck(nsIPromptService *iface,
        nsIDOMWindow *aParent, const PRUnichar *aDialogTitle,
        const PRUnichar *aText, const PRUnichar *aCheckMsg, PRBool *aCheckState)
{
    FIXME("(%p %s %s %s %p)\n", aParent, debugstr_w(aDialogTitle), debugstr_w(aText),
          debugstr_w(aCheckMsg), aCheckState);
    return NS_ERROR_NOT_IMPLEMENTED;
}

static nsresult NSAPI nsPromptService_Confirm(nsIPromptService *iface,
        nsIDOMWindow *aParent, const PRUnichar *aDialogTitle, const PRUnichar *aText,
        PRBool *_retval)
{
    FIXME("(%p %s %s %p)\n", aParent, debugstr_w(aDialogTitle), debugstr_w(aText), _retval);
    return NS_ERROR_NOT_IMPLEMENTED;
}

static nsresult NSAPI nsPromptService_ConfirmCheck(nsIPromptService *iface,
        nsIDOMWindow *aParent, const PRUnichar *aDialogTitle,
        const PRUnichar *aText, const PRUnichar *aCheckMsg, PRBool *aCheckState,
        PRBool *_retval)
{
    FIXME("(%p %s %s %s %p %p)\n", aParent, debugstr_w(aDialogTitle), debugstr_w(aText),
        debugstr_w(aCheckMsg), aCheckState, _retval);
    return NS_ERROR_NOT_IMPLEMENTED;
}

static nsresult NSAPI nsPromptService_ConfirmEx(nsIPromptService *iface,
        nsIDOMWindow *aParent, const PRUnichar *aDialogTitle,
        const PRUnichar *aText, PRUint32 aButtonFlags, const PRUnichar *aButton0Title,
        const PRUnichar *aButton1Title, const PRUnichar *aButton2Title,
        const PRUnichar *aCheckMsg, PRBool *aCheckState, PRInt32 *_retval)
{
    static const PRUnichar wszContinue[] = {'C','o','n','t','i','n','u','e',0};

    FIXME("(%p %s %s %08lx %s %s %s %s %p %p) hack!\n", aParent, debugstr_w(aDialogTitle),
          debugstr_w(aText), aButtonFlags, debugstr_w(aButton0Title),
          debugstr_w(aButton1Title), debugstr_w(aButton2Title), debugstr_w(aCheckMsg),
          aCheckState, _retval);

    /*
     * FIXME:
     * This is really very very ugly hack!!!
     */

    if(!memcmp(aButton0Title, wszContinue, sizeof(wszContinue)))
        *_retval = 0;
    else if(!memcmp(aButton1Title, wszContinue, sizeof(wszContinue)))
        *_retval = 1;
    else if(!memcmp(aButton2Title, wszContinue, sizeof(wszContinue)))
        *_retval = 2;
    else
        *_retval = 0;

    return NS_OK;
}

static nsresult NSAPI nsPromptService_Prompt(nsIPromptService *iface,
        nsIDOMWindow *aParent, const PRUnichar *aDialogTitle,
        const PRUnichar *aText, PRUnichar **aValue, const PRUnichar *aCheckMsg,
        PRBool *aCheckState, PRBool *_retval)
{
    FIXME("(%p %s %s %p %s %p %p)\n", aParent, debugstr_w(aDialogTitle), debugstr_w(aText),
          aValue, debugstr_w(aCheckMsg), aCheckState, _retval);
    return NS_ERROR_NOT_IMPLEMENTED;
}

static nsresult NSAPI nsPromptService_PromptUsernameAndPassword(nsIPromptService *iface,
        nsIDOMWindow *aParent, const PRUnichar *aDialogTitle,
        const PRUnichar *aText, PRUnichar **aUsername, PRUnichar **aPassword,
        const PRUnichar *aCheckMsg, PRBool *aCheckState, PRBool *_retval)
{
    FIXME("(%p %s %s %p %p %s %p %p)\n", aParent, debugstr_w(aDialogTitle),
        debugstr_w(aText), aUsername, aPassword, debugstr_w(aCheckMsg), aCheckState,
        _retval);
    return NS_ERROR_NOT_IMPLEMENTED;
}

static nsresult NSAPI nsPromptService_PromptPassword(nsIPromptService *iface,
        nsIDOMWindow *aParent, const PRUnichar *aDialogTitle,
        const PRUnichar *aText, PRUnichar **aPassword, const PRUnichar *aCheckMsg,
        PRBool *aCheckState, PRBool *_retval)
{
    FIXME("(%p %s %s %p %s %p %p)\n", aParent, debugstr_w(aDialogTitle),
          debugstr_w(aText), aPassword, debugstr_w(aCheckMsg), aCheckState, _retval);
    return NS_ERROR_NOT_IMPLEMENTED;
}

static nsresult NSAPI nsPromptService_Select(nsIPromptService *iface,
        nsIDOMWindow *aParent, const PRUnichar *aDialogTitle,
        const PRUnichar *aText, PRUint32 aCount, const PRUnichar **aSelectList,
        PRInt32 *aOutSelection, PRBool *_retval)
{
    FIXME("(%p %s %s %ld %p %p %p)\n", aParent, debugstr_w(aDialogTitle),
        debugstr_w(aText), aCount, aSelectList, aOutSelection, _retval);
    return NS_ERROR_NOT_IMPLEMENTED;
}

static const nsIPromptServiceVtbl PromptServiceVtbl = {
    nsPromptService_QueryInterface,
    nsPromptService_AddRef,
    nsPromptService_Release,
    nsPromptService_Alert,
    nsPromptService_AlertCheck,
    nsPromptService_Confirm,
    nsPromptService_ConfirmCheck,
    nsPromptService_ConfirmEx,
    nsPromptService_Prompt,
    nsPromptService_PromptUsernameAndPassword,
    nsPromptService_PromptPassword,
    nsPromptService_Select
};

static nsIPromptService nsPromptService = { &PromptServiceVtbl };

typedef struct {
    const nsIFactoryVtbl *lpFactoryVtbl;
    nsISupports *service;
} nsServiceFactory;

#define NSFACTORY(x)  ((nsIFactory*)  &(x)->lpFactoryVtbl)

#define NSFACTORY_THIS(iface) DEFINE_THIS(nsServiceFactory, Factory, iface)

static nsresult NSAPI nsServiceFactory_QueryInterface(nsIFactory *iface, nsIIDRef riid,
                                                    nsQIResult result)
{
    nsServiceFactory *This = NSFACTORY_THIS(iface);

    *result = NULL;

    if(IsEqualGUID(&IID_nsISupports, riid)) {
        TRACE("(%p)->(IID_nsISupoprts %p)\n", This, result);
        *result = NSFACTORY(This);
    }else if(IsEqualGUID(&IID_nsIFactory, riid)) {
        TRACE("(%p)->(IID_nsIFactory %p)\n", This, result);
        *result = NSFACTORY(This);
    }

    if(*result)
        return NS_OK;

    WARN("(%p)->(%s %p)\n", This, debugstr_guid(riid), result);
    return NS_NOINTERFACE;
}

static nsrefcnt NSAPI nsServiceFactory_AddRef(nsIFactory *iface)
{
    return 2;
}

static nsrefcnt NSAPI nsServiceFactory_Release(nsIFactory *iface)
{
    return 1;
}

static nsresult NSAPI nsServiceFactory_CreateInstance(nsIFactory *iface,
        nsISupports *aOuter, const nsIID *iid, void **result)
{
    nsServiceFactory *This = NSFACTORY_THIS(iface);

    TRACE("(%p)->(%p %s %p)\n", This, aOuter, debugstr_guid(iid), result);

    return nsISupports_QueryInterface(This->service, iid, result);
}

static nsresult NSAPI nsServiceFactory_LockFactory(nsIFactory *iface, PRBool lock)
{
    nsServiceFactory *This = NSFACTORY_THIS(iface);
    WARN("(%p)->(%x)\n", This, lock);
    return NS_OK;
}

#undef NSFACTORY_THIS

static const nsIFactoryVtbl nsServiceFactoryVtbl = {
    nsServiceFactory_QueryInterface,
    nsServiceFactory_AddRef,
    nsServiceFactory_Release,
    nsServiceFactory_CreateInstance,
    nsServiceFactory_LockFactory
};

static nsServiceFactory PromptServiceFactory = {
    &nsServiceFactoryVtbl,
    (nsISupports*)&nsPromptService
};

void register_nsservice(nsIComponentRegistrar *registrar)
{
    nsresult nsres;

    nsres = nsIComponentRegistrar_RegisterFactory(registrar, &NS_PROMPTSERVICE_CID,
            "Prompt Service", NS_PROMPTSERVICE_CONTRACTID, NSFACTORY(&PromptServiceFactory));
    if(NS_FAILED(nsres))
        ERR("RegisterFactory failed: %08lx\n", nsres);
}
