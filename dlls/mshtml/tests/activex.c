/*
 * Copyright 2010 Jacek Caban for CodeWeavers
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#define COBJMACROS
#define CONST_VTABLE

#include <wine/test.h>
#include <stdarg.h>
#include <stdio.h>

#include "windef.h"
#include "winbase.h"
#include "ole2.h"
#include "mshtml.h"
#include "docobj.h"
#include "hlink.h"
#include "mshtmhst.h"
#include "mshtml_test.h"

#define DEFINE_EXPECT(func) \
    static BOOL expect_ ## func = FALSE, called_ ## func = FALSE

#define SET_EXPECT(func) \
    do { called_ ## func = FALSE; expect_ ## func = TRUE; } while(0)

#define CHECK_EXPECT2(func) \
    do { \
        ok(expect_ ##func, "unexpected call " #func "\n"); \
        called_ ## func = TRUE; \
    }while(0)

#define CHECK_EXPECT(func) \
    do { \
        CHECK_EXPECT2(func); \
        expect_ ## func = FALSE; \
    }while(0)

#define CHECK_CALLED(func) \
    do { \
        ok(called_ ## func, "expected " #func "\n"); \
        expect_ ## func = called_ ## func = FALSE; \
    }while(0)

DEFINE_EXPECT(CreateInstance);
DEFINE_EXPECT(FreezeEvents_TRUE);
DEFINE_EXPECT(FreezeEvents_FALSE);
DEFINE_EXPECT(QuickActivate);
DEFINE_EXPECT(IPersistPropertyBag_InitNew);
DEFINE_EXPECT(IPersistPropertyBag_Load);
DEFINE_EXPECT(Invoke_READYSTATE);

static HWND container_hwnd;

#define TESTACTIVEX_CLSID "{178fc163-f585-4e24-9c13-4bb7f6680746}"

static const GUID CLSID_TestActiveX =
    {0x178fc163,0xf585,0x4e24,{0x9c,0x13,0x4b,0xb7,0xf6,0x68,0x07,0x46}};

static const char object_ax_str[] =
    "<html><head></head><body>"
    "<object classid=\"clsid:" TESTACTIVEX_CLSID "\" width=\"300\" height=\"200\" id=\"objid\">"
    "<param name=\"param_name\" value=\"param_value\">"
    "<param name=\"num_param\" value=\"3\">"
    "</object>"
    "</body></html>";

static const REFIID pluginhost_iids[] = {
    &IID_IOleClientSite,
    &IID_IAdviseSink,
    &IID_IAdviseSinkEx,
    &IID_IPropertyNotifySink,
    &IID_IDispatch,
    &IID_IOleWindow,
    &IID_IOleInPlaceSite,
    &IID_IOleInPlaceSiteEx,
    &IID_IOleControlSite,
    &IID_IBindHost,
    &IID_IServiceProvider,
    NULL
};

static const char *debugstr_guid(REFIID riid)
{
    static char buf[50];

    sprintf(buf, "{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
            riid->Data1, riid->Data2, riid->Data3, riid->Data4[0],
            riid->Data4[1], riid->Data4[2], riid->Data4[3], riid->Data4[4],
            riid->Data4[5], riid->Data4[6], riid->Data4[7]);

    return buf;
}

static BOOL iface_cmp(IUnknown *iface1, IUnknown *iface2)
{
    IUnknown *unk1, *unk2;

    if(iface1 == iface2)
        return TRUE;

    IUnknown_QueryInterface(iface1, &IID_IUnknown, (void**)&unk1);
    IUnknown_Release(unk1);
    IUnknown_QueryInterface(iface2, &IID_IUnknown, (void**)&unk2);
    IUnknown_Release(unk2);

    return unk1 == unk2;
}

#define test_ifaces(i,ids) _test_ifaces(__LINE__,i,ids)
static void _test_ifaces(unsigned line, IUnknown *iface, REFIID *iids)
{
    const IID * const *piid;
    IUnknown *unk;
    HRESULT hres;

     for(piid = iids; *piid; piid++) {
        hres = IDispatch_QueryInterface(iface, *piid, (void**)&unk);
        ok_(__FILE__,line) (hres == S_OK, "Could not get %s interface: %08x\n", debugstr_guid(*piid), hres);
        if(SUCCEEDED(hres))
            IUnknown_Release(unk);
    }
}

static int strcmp_wa(LPCWSTR strw, const char *stra)
{
    CHAR buf[512];
    WideCharToMultiByte(CP_ACP, 0, strw, -1, buf, sizeof(buf), NULL, NULL);
    return lstrcmpA(stra, buf);
}

static IOleClientSite *client_site;
static READYSTATE plugin_readystate = READYSTATE_UNINITIALIZED;

static void set_plugin_readystate(READYSTATE state)
{
    IPropertyNotifySink *prop_notif;
    HRESULT hres;

    plugin_readystate = state;

    hres = IOleClientSite_QueryInterface(client_site, &IID_IPropertyNotifySink, (void**)&prop_notif);
    ok(hres == S_OK, "Could not get IPropertyNotifySink iface: %08x\n", hres);

    hres = IPropertyNotifySink_OnChanged(prop_notif, DISPID_READYSTATE);
    ok(hres == S_OK, "OnChanged(DISPID_READYSTATE) failed: %08x\n", hres);

    IPropertyNotifySink_Release(prop_notif);
}

static HRESULT ax_qi(REFIID,void**);

static HRESULT WINAPI OleControl_QueryInterface(IOleControl *iface, REFIID riid, void **ppv)
{
    return ax_qi(riid, ppv);
}

static ULONG WINAPI OleControl_AddRef(IOleControl *iface)
{
    return 2;
}

static ULONG WINAPI OleControl_Release(IOleControl *iface)
{
    return 1;
}

static HRESULT WINAPI OleControl_GetControlInfo(IOleControl *iface, CONTROLINFO *pCI)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI OleControl_OnMnemonic(IOleControl *iface, MSG *mMsg)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI OleControl_OnAmbientPropertyChange(IOleControl *iface, DISPID dispID)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI OleControl_FreezeEvents(IOleControl *iface, BOOL bFreeze)
{
    if(bFreeze)
        CHECK_EXPECT2(FreezeEvents_TRUE);
    else
        CHECK_EXPECT2(FreezeEvents_FALSE);
    return S_OK;
}

static const IOleControlVtbl OleControlVtbl = {
    OleControl_QueryInterface,
    OleControl_AddRef,
    OleControl_Release,
    OleControl_GetControlInfo,
    OleControl_OnMnemonic,
    OleControl_OnAmbientPropertyChange,
    OleControl_FreezeEvents
};

static IOleControl OleControl = { &OleControlVtbl };

static HRESULT WINAPI QuickActivate_QueryInterface(IQuickActivate *iface, REFIID riid, void **ppv)
{
    return ax_qi(riid, ppv);
}

static ULONG WINAPI QuickActivate_AddRef(IQuickActivate *iface)
{
    return 2;
}

static ULONG WINAPI QuickActivate_Release(IQuickActivate *iface)
{
    return 1;
}

static HRESULT WINAPI QuickActivate_QuickActivate(IQuickActivate *iface, QACONTAINER *container, QACONTROL *control)
{
    CHECK_EXPECT(QuickActivate);

    ok(container != NULL, "container == NULL\n");
    ok(container->cbSize == sizeof(*container), "container->cbSize = %d\n", container->cbSize);
    ok(container->pClientSite != NULL, "container->pClientSite == NULL\n");
    ok(container->pAdviseSink != NULL, "container->pAdviseSink == NULL\n");
    ok(container->pPropertyNotifySink != NULL, "container->pPropertyNotifySink == NULL\n");
    ok(!container->pUnkEventSink, "container->pUnkEventSink != NULL\n");
    ok(container->dwAmbientFlags == (QACONTAINER_SUPPORTSMNEMONICS|QACONTAINER_MESSAGEREFLECT|QACONTAINER_USERMODE),
       "container->dwAmbientFlags = %x\n", container->dwAmbientFlags);
    ok(!container->colorFore, "container->colorFore == 0\n"); /* FIXME */
    todo_wine
    ok(container->colorBack, "container->colorBack == 0\n"); /* FIXME */
    todo_wine
    ok(container->pFont != NULL, "container->pFont == NULL\n");
    todo_wine
    ok(container->pUndoMgr != NULL, "container->pUndoMgr == NULL\n");
    ok(!container->dwAppearance, "container->dwAppearance = %x\n", container->dwAppearance);
    ok(!container->lcid, "container->lcid = %x\n", container->lcid);
    ok(!container->hpal, "container->hpal = %p\n", container->hpal);
    ok(!container->pBindHost, "container->pBindHost != NULL\n");
    ok(!container->pOleControlSite, "container->pOleControlSite != NULL\n");
    ok(!container->pServiceProvider, "container->pServiceProvider != NULL\n");

    ok(control->cbSize == sizeof(*control), "control->cbSize = %d\n", control->cbSize);
    ok(!control->dwMiscStatus, "control->dwMiscStatus = %x\n", control->dwMiscStatus);
    ok(!control->dwViewStatus, "control->dwViewStatus = %x\n", control->dwViewStatus);
    ok(!control->dwEventCookie, "control->dwEventCookie = %x\n", control->dwEventCookie);
    ok(!control->dwPropNotifyCookie, "control->dwPropNotifyCookie = %x\n", control->dwPropNotifyCookie);
    ok(!control->dwPointerActivationPolicy, "control->dwPointerActivationPolicy = %x\n", control->dwPointerActivationPolicy);

    ok(iface_cmp((IUnknown*)container->pClientSite, (IUnknown*)container->pAdviseSink),
       "container->pClientSite != container->pAdviseSink\n");
    ok(iface_cmp((IUnknown*)container->pClientSite, (IUnknown*)container->pPropertyNotifySink),
       "container->pClientSite != container->pPropertyNotifySink\n");
    test_ifaces((IUnknown*)container->pClientSite, pluginhost_iids);

    IOleClientSite_AddRef(container->pClientSite);
    client_site = container->pClientSite;

    return S_OK;
}

static HRESULT WINAPI QuickActivate_SetContentExtent(IQuickActivate *iface, LPSIZEL pSizel)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI QuickActivate_GetContentExtent(IQuickActivate *iface, LPSIZEL pSizel)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static const IQuickActivateVtbl QuickActivateVtbl = {
    QuickActivate_QueryInterface,
    QuickActivate_AddRef,
    QuickActivate_Release,
    QuickActivate_QuickActivate,
    QuickActivate_GetContentExtent,
    QuickActivate_SetContentExtent
};

static IQuickActivate QuickActivate = { &QuickActivateVtbl };

static HRESULT WINAPI PersistPropertyBag_QueryInterface(IPersistPropertyBag *iface, REFIID riid, void **ppv)
{
    return ax_qi(riid, ppv);
}

static ULONG WINAPI PersistPropertyBag_AddRef(IPersistPropertyBag *iface)
{
    return 2;
}

static ULONG WINAPI PersistPropertyBag_Release(IPersistPropertyBag *iface)
{
    return 1;
}

static HRESULT WINAPI PersistPropertyBag_GetClassID(IPersistPropertyBag *face, CLSID *pClassID)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI PersistPropertyBag_InitNew(IPersistPropertyBag *face)
{
    CHECK_EXPECT(IPersistPropertyBag_InitNew);
    return S_OK;
}

static HRESULT WINAPI PersistPropertyBag_Load(IPersistPropertyBag *face, IPropertyBag *pPropBag, IErrorLog *pErrorLog)
{
    VARIANT v;
    HRESULT hres;

    static const WCHAR param_nameW[] = {'p','a','r','a','m','_','n','a','m','e',0};
    static const WCHAR num_paramW[] = {'n','u','m','_','p','a','r','a','m',0};
    static const WCHAR no_paramW[] = {'n','o','_','p','a','r','a','m',0};

    static const IID *propbag_ifaces[] = {
        &IID_IPropertyBag,
        &IID_IPropertyBag2,
        NULL
    };

    CHECK_EXPECT(IPersistPropertyBag_Load);

    ok(pPropBag != NULL, "pPropBag == NULL\n");
    ok(!pErrorLog, "pErrorLog != NULL\n");

    test_ifaces((IUnknown*)pPropBag, propbag_ifaces);

    V_VT(&v) = VT_BSTR;
    hres = IPropertyBag_Read(pPropBag, param_nameW, &v, NULL);
    ok(hres == S_OK, "Read failed: %08x\n", hres);
    ok(V_VT(&v) == VT_BSTR, "V_VT(&v) = %d\n", V_VT(&v));
    ok(!strcmp_wa(V_BSTR(&v), "param_value"), "V_BSTR(v) = %s\n", wine_dbgstr_w(V_BSTR(&v)));

    V_VT(&v) = VT_I4;
    V_I4(&v) = 0xdeadbeef;
    hres = IPropertyBag_Read(pPropBag, param_nameW, &v, NULL);
    ok(hres == DISP_E_TYPEMISMATCH, "Read failed: %08x, expected DISP_E_TYPEMISMATCH\n", hres);
    ok(V_VT(&v) == VT_I4, "V_VT(&v) = %d\n", V_VT(&v));
    ok(V_I4(&v) == 0xdeadbeef, "V_I4(v) = %x\n", V_I4(&v));

    V_VT(&v) = VT_BSTR;
    hres = IPropertyBag_Read(pPropBag, num_paramW, &v, NULL);
    ok(hres == S_OK, "Read failed: %08x\n", hres);
    ok(V_VT(&v) == VT_BSTR, "V_VT(&v) = %d\n", V_VT(&v));
    ok(!strcmp_wa(V_BSTR(&v), "3"), "V_BSTR(v) = %s\n", wine_dbgstr_w(V_BSTR(&v)));

    V_VT(&v) = VT_I4;
    V_I4(&v) = 0xdeadbeef;
    hres = IPropertyBag_Read(pPropBag, num_paramW, &v, NULL);
    ok(hres == S_OK, "Read failed: %08x\n", hres);
    ok(V_VT(&v) == VT_I4, "V_VT(&v) = %d\n", V_VT(&v));
    ok(V_I4(&v) == 3, "V_I4(v) = %x\n", V_I4(&v));

    V_VT(&v) = VT_BSTR;
    V_BSTR(&v) = (BSTR)0xdeadbeef;
    hres = IPropertyBag_Read(pPropBag, no_paramW, &v, NULL);
    ok(hres == E_INVALIDARG, "Read failed: %08x\n", hres);
    ok(V_VT(&v) == VT_BSTR, "V_VT(&v) = %d\n", V_VT(&v));
    ok(V_BSTR(&v) == (BSTR)0xdeadbeef, "V_BSTR(v) = %p\n", V_BSTR(&v));

    set_plugin_readystate(READYSTATE_INTERACTIVE);
    set_plugin_readystate(READYSTATE_COMPLETE);

    return S_OK;
}

static HRESULT WINAPI PersistPropertyBag_Save(IPersistPropertyBag *face, IPropertyBag *pPropBag, BOOL fClearDisrty, BOOL fSaveAllProperties)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static const IPersistPropertyBagVtbl PersistPropertyBagVtbl = {
    PersistPropertyBag_QueryInterface,
    PersistPropertyBag_AddRef,
    PersistPropertyBag_Release,
    PersistPropertyBag_GetClassID,
    PersistPropertyBag_InitNew,
    PersistPropertyBag_Load,
    PersistPropertyBag_Save

};

static IPersistPropertyBag PersistPropertyBag = { &PersistPropertyBagVtbl };

static HRESULT WINAPI Dispatch_QueryInterface(IDispatch *iface, REFIID riid, void **ppv)
{
    return ax_qi(riid, ppv);
}

static ULONG WINAPI Dispatch_AddRef(IDispatch *iface)
{
    return 2;
}

static ULONG WINAPI Dispatch_Release(IDispatch *iface)
{
    return 1;
}

static HRESULT WINAPI Dispatch_GetTypeInfoCount(IDispatch *iface, UINT *pctinfo)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI Dispatch_GetTypeInfo(IDispatch *iface, UINT iTInfo, LCID lcid,
        ITypeInfo **ppTInfo)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI Dispatch_GetIDsOfNames(IDispatch *iface, REFIID riid, LPOLESTR *rgszNames,
        UINT cNames, LCID lcid, DISPID *rgDispId)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI Dispatch_Invoke(IDispatch *iface, DISPID dispIdMember, REFIID riid,
        LCID lcid, WORD wFlags, DISPPARAMS *pDispParams, VARIANT *pVarResult,
        EXCEPINFO *pExcepInfo, UINT *puArgErr)
{
    ok(IsEqualGUID(riid, &IID_NULL), "riid = %s\n", debugstr_guid(riid));
    ok(pDispParams != NULL, "pDispParams == NULL\n");
    ok(!pDispParams->cNamedArgs, "pDispParams->cNamedArgs = %d\n", pDispParams->cNamedArgs);
    ok(!pDispParams->rgdispidNamedArgs, "pDispParams->rgdispidNamedArgs != NULL\n");
    ok(pVarResult != NULL, "pVarResult == NULL\n");
    ok(!pExcepInfo, "pExcepInfo != NULL\n");
    ok(puArgErr != NULL, "puArgErr == NULL\n");

    switch(dispIdMember) {
    case DISPID_READYSTATE:
        CHECK_EXPECT2(Invoke_READYSTATE);
        ok(wFlags == DISPATCH_PROPERTYGET, "wFlags = %x\n", wFlags);
        ok(!pDispParams->cArgs, "pDispParams->cArgs = %d\n", pDispParams->cArgs);
        ok(!pDispParams->rgvarg, "pDispParams->rgvarg != NULL\n");

        V_VT(pVarResult) = VT_I4;
        V_I4(pVarResult) = plugin_readystate;
        return S_OK;
    default:
        ok(0, "unexpected call %d\n", dispIdMember);
    }

    return E_NOTIMPL;
}

static const IDispatchVtbl DispatchVtbl = {
    Dispatch_QueryInterface,
    Dispatch_AddRef,
    Dispatch_Release,
    Dispatch_GetTypeInfoCount,
    Dispatch_GetTypeInfo,
    Dispatch_GetIDsOfNames,
    Dispatch_Invoke
};

static IDispatch Dispatch = { &DispatchVtbl };

static HRESULT ax_qi(REFIID riid, void **ppv)
{
    if(IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IOleControl)) {
        *ppv = &OleControl;
        return S_OK;
    }

    if(IsEqualGUID(riid, &IID_IQuickActivate)) {
        *ppv = &QuickActivate;
        return S_OK;
    }

    if(IsEqualGUID(riid, &IID_IPersistPropertyBag)) {
        *ppv = &PersistPropertyBag;
        return S_OK;
    }

    if(IsEqualGUID(riid, &IID_IDispatch)) {
        *ppv = &Dispatch;
        return S_OK;
    }

    *ppv = NULL;
    return E_NOINTERFACE;
}

static HRESULT WINAPI ClassFactory_QueryInterface(IClassFactory *iface, REFIID riid, void **ppv)
{
    *ppv = NULL;

    if(IsEqualGUID(&IID_IUnknown, riid) || IsEqualGUID(&IID_IClassFactory, riid)) {
        *ppv = iface;
        return S_OK;
    }

    if(IsEqualGUID(&IID_IMarshal, riid))
        return E_NOINTERFACE;
    if(IsEqualGUID(&CLSID_IdentityUnmarshal, riid))
        return E_NOINTERFACE;
    if(IsEqualGUID(&IID_IClassFactoryEx, riid))
        return E_NOINTERFACE; /* TODO */

    ok(0, "unexpected riid %s\n", debugstr_guid(riid));
    return E_NOTIMPL;
}

static ULONG WINAPI ClassFactory_AddRef(IClassFactory *iface)
{
    return 2;
}

static ULONG WINAPI ClassFactory_Release(IClassFactory *iface)
{
    return 1;
}

static HRESULT WINAPI ClassFactory_CreateInstance(IClassFactory *iface, IUnknown *outer, REFIID riid, void **ppv)
{
    CHECK_EXPECT(CreateInstance);

    ok(!outer, "outer = %p\n", outer);
    ok(IsEqualGUID(riid, &IID_IUnknown), "riid = %s\n", debugstr_guid(riid));

    *ppv = &OleControl;
    return S_OK;
}

static HRESULT WINAPI ClassFactory_LockServer(IClassFactory *iface, BOOL dolock)
{
    ok(0, "unexpected call\n");
    return S_OK;
}

static const IClassFactoryVtbl ClassFactoryVtbl = {
    ClassFactory_QueryInterface,
    ClassFactory_AddRef,
    ClassFactory_Release,
    ClassFactory_CreateInstance,
    ClassFactory_LockServer
};

static IClassFactory activex_cf = { &ClassFactoryVtbl };

static HRESULT cs_qi(REFIID,void **);
static IOleDocumentView *view;

static HRESULT WINAPI InPlaceFrame_QueryInterface(IOleInPlaceFrame *iface, REFIID riid, void **ppv)
{
    static const GUID undocumented_frame_iid = {0xfbece6c9,0x48d7,0x4a37,{0x8f,0xe3,0x6a,0xd4,0x27,0x2f,0xdd,0xac}};

    if(!IsEqualGUID(&undocumented_frame_iid, riid))
        ok(0, "unexpected riid %s\n", debugstr_guid(riid));

    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI InPlaceFrame_AddRef(IOleInPlaceFrame *iface)
{
    return 2;
}

static ULONG WINAPI InPlaceFrame_Release(IOleInPlaceFrame *iface)
{
    return 1;
}

static HRESULT WINAPI InPlaceFrame_GetWindow(IOleInPlaceFrame *iface, HWND *phwnd)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI InPlaceFrame_ContextSensitiveHelp(IOleInPlaceFrame *iface, BOOL fEnterMode)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI InPlaceFrame_GetBorder(IOleInPlaceFrame *iface, LPRECT lprectBorder)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI InPlaceFrame_RequestBorderSpace(IOleInPlaceFrame *iface,
        LPCBORDERWIDTHS pborderwidths)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI InPlaceFrame_SetBorderSpace(IOleInPlaceFrame *iface,
        LPCBORDERWIDTHS pborderwidths)
{
    return S_OK;
}

static HRESULT WINAPI InPlaceUIWindow_SetActiveObject(IOleInPlaceFrame *iface,
        IOleInPlaceActiveObject *pActiveObject, LPCOLESTR pszObjName)
{
    return S_OK;
}

static HRESULT WINAPI InPlaceFrame_SetActiveObject(IOleInPlaceFrame *iface,
        IOleInPlaceActiveObject *pActiveObject, LPCOLESTR pszObjName)
{
    return S_OK;
}

static HRESULT WINAPI InPlaceFrame_InsertMenus(IOleInPlaceFrame *iface, HMENU hmenuShared,
        LPOLEMENUGROUPWIDTHS lpMenuWidths)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI InPlaceFrame_SetMenu(IOleInPlaceFrame *iface, HMENU hmenuShared,
        HOLEMENU holemenu, HWND hwndActiveObject)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI InPlaceFrame_RemoveMenus(IOleInPlaceFrame *iface, HMENU hmenuShared)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI InPlaceFrame_SetStatusText(IOleInPlaceFrame *iface, LPCOLESTR pszStatusText)
{
    return S_OK;
}

static HRESULT WINAPI InPlaceFrame_EnableModeless(IOleInPlaceFrame *iface, BOOL fEnable)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI InPlaceFrame_TranslateAccelerator(IOleInPlaceFrame *iface, LPMSG lpmsg, WORD wID)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static const IOleInPlaceFrameVtbl InPlaceFrameVtbl = {
    InPlaceFrame_QueryInterface,
    InPlaceFrame_AddRef,
    InPlaceFrame_Release,
    InPlaceFrame_GetWindow,
    InPlaceFrame_ContextSensitiveHelp,
    InPlaceFrame_GetBorder,
    InPlaceFrame_RequestBorderSpace,
    InPlaceFrame_SetBorderSpace,
    InPlaceFrame_SetActiveObject,
    InPlaceFrame_InsertMenus,
    InPlaceFrame_SetMenu,
    InPlaceFrame_RemoveMenus,
    InPlaceFrame_SetStatusText,
    InPlaceFrame_EnableModeless,
    InPlaceFrame_TranslateAccelerator
};

static IOleInPlaceFrame InPlaceFrame = { &InPlaceFrameVtbl };

static const IOleInPlaceFrameVtbl InPlaceUIWindowVtbl = {
    InPlaceFrame_QueryInterface,
    InPlaceFrame_AddRef,
    InPlaceFrame_Release,
    InPlaceFrame_GetWindow,
    InPlaceFrame_ContextSensitiveHelp,
    InPlaceFrame_GetBorder,
    InPlaceFrame_RequestBorderSpace,
    InPlaceFrame_SetBorderSpace,
    InPlaceUIWindow_SetActiveObject,
};

static IOleInPlaceFrame InPlaceUIWindow = { &InPlaceUIWindowVtbl };

static HRESULT WINAPI InPlaceSite_QueryInterface(IOleInPlaceSite *iface, REFIID riid, void **ppv)
{
    return cs_qi(riid, ppv);
}

static ULONG WINAPI InPlaceSite_AddRef(IOleInPlaceSite *iface)
{
    return 2;
}

static ULONG WINAPI InPlaceSite_Release(IOleInPlaceSite *iface)
{
    return 1;
}

static HRESULT WINAPI InPlaceSite_GetWindow(IOleInPlaceSite *iface, HWND *phwnd)
{
    *phwnd = container_hwnd;
    return S_OK;
}

static HRESULT WINAPI InPlaceSite_ContextSensitiveHelp(IOleInPlaceSite *iface, BOOL fEnterMode)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI InPlaceSite_CanInPlaceActivate(IOleInPlaceSite *iface)
{
    return S_OK;
}

static HRESULT WINAPI InPlaceSite_OnInPlaceActivate(IOleInPlaceSite *iface)
{
    return S_OK;
}

static HRESULT WINAPI InPlaceSite_OnUIActivate(IOleInPlaceSite *iface)
{
    return S_OK;
}

static HRESULT WINAPI InPlaceSite_GetWindowContext(IOleInPlaceSite *iface,
        IOleInPlaceFrame **ppFrame, IOleInPlaceUIWindow **ppDoc, LPRECT lprcPosRect,
        LPRECT lprcClipRect, LPOLEINPLACEFRAMEINFO lpFrameInfo)
{
    static const RECT rect = {0,0,500,500};

    *ppFrame = &InPlaceFrame;
    *ppDoc = (IOleInPlaceUIWindow*)&InPlaceUIWindow;
    *lprcPosRect = rect;
    *lprcClipRect = rect;

    lpFrameInfo->cb = sizeof(*lpFrameInfo);
    lpFrameInfo->fMDIApp = FALSE;
    lpFrameInfo->hwndFrame = container_hwnd;
    lpFrameInfo->haccel = NULL;
    lpFrameInfo->cAccelEntries = 0;

    return S_OK;
}

static HRESULT WINAPI InPlaceSite_Scroll(IOleInPlaceSite *iface, SIZE scrollExtant)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI InPlaceSite_OnUIDeactivate(IOleInPlaceSite *iface, BOOL fUndoable)
{
    return S_OK;
}

static HRESULT WINAPI InPlaceSite_OnInPlaceDeactivate(IOleInPlaceSite *iface)
{
    return S_OK;
}

static HRESULT WINAPI InPlaceSite_DiscardUndoState(IOleInPlaceSite *iface)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI InPlaceSite_DeactivateAndUndo(IOleInPlaceSite *iface)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI InPlaceSite_OnPosRectChange(IOleInPlaceSite *iface, LPCRECT lprcPosRect)
{
    return E_NOTIMPL;
}

static const IOleInPlaceSiteVtbl InPlaceSiteVtbl = {
    InPlaceSite_QueryInterface,
    InPlaceSite_AddRef,
    InPlaceSite_Release,
    InPlaceSite_GetWindow,
    InPlaceSite_ContextSensitiveHelp,
    InPlaceSite_CanInPlaceActivate,
    InPlaceSite_OnInPlaceActivate,
    InPlaceSite_OnUIActivate,
    InPlaceSite_GetWindowContext,
    InPlaceSite_Scroll,
    InPlaceSite_OnUIDeactivate,
    InPlaceSite_OnInPlaceDeactivate,
    InPlaceSite_DiscardUndoState,
    InPlaceSite_DeactivateAndUndo,
    InPlaceSite_OnPosRectChange,
};

static IOleInPlaceSite InPlaceSite = { &InPlaceSiteVtbl };

static HRESULT WINAPI ClientSite_QueryInterface(IOleClientSite *iface, REFIID riid, void **ppv)
{
    return cs_qi(riid, ppv);
}

static ULONG WINAPI ClientSite_AddRef(IOleClientSite *iface)
{
    return 2;
}

static ULONG WINAPI ClientSite_Release(IOleClientSite *iface)
{
    return 1;
}

static HRESULT WINAPI ClientSite_SaveObject(IOleClientSite *iface)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI ClientSite_GetMoniker(IOleClientSite *iface, DWORD dwAssign, DWORD dwWhichMoniker,
        IMoniker **ppmon)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI ClientSite_GetContainer(IOleClientSite *iface, IOleContainer **ppContainer)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI ClientSite_ShowObject(IOleClientSite *iface)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI ClientSite_OnShowWindow(IOleClientSite *iface, BOOL fShow)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI ClientSite_RequestNewObjectLayout(IOleClientSite *iface)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static const IOleClientSiteVtbl ClientSiteVtbl = {
    ClientSite_QueryInterface,
    ClientSite_AddRef,
    ClientSite_Release,
    ClientSite_SaveObject,
    ClientSite_GetMoniker,
    ClientSite_GetContainer,
    ClientSite_ShowObject,
    ClientSite_OnShowWindow,
    ClientSite_RequestNewObjectLayout
};

static IOleClientSite ClientSite = { &ClientSiteVtbl };

static HRESULT WINAPI DocumentSite_QueryInterface(IOleDocumentSite *iface, REFIID riid, void **ppv)
{
    return cs_qi(riid, ppv);
}

static ULONG WINAPI DocumentSite_AddRef(IOleDocumentSite *iface)
{
    return 2;
}

static ULONG WINAPI DocumentSite_Release(IOleDocumentSite *iface)
{
    return 1;
}

static HRESULT WINAPI DocumentSite_ActivateMe(IOleDocumentSite *iface, IOleDocumentView *pViewToActivate)
{
    RECT rect = {0,0,400,500};
    IOleDocument *document;
    HRESULT hres;

    hres = IOleDocumentView_QueryInterface(pViewToActivate, &IID_IOleDocument, (void**)&document);
    ok(hres == S_OK, "could not get IOleDocument: %08x\n", hres);

    hres = IOleDocument_CreateView(document, &InPlaceSite, NULL, 0, &view);
    IOleDocument_Release(document);
    ok(hres == S_OK, "CreateView failed: %08x\n", hres);

    hres = IOleDocumentView_SetInPlaceSite(view, &InPlaceSite);
    ok(hres == S_OK, "SetInPlaceSite failed: %08x\n", hres);

    hres = IOleDocumentView_UIActivate(view, TRUE);
    ok(hres == S_OK, "UIActivate failed: %08x\n", hres);

    hres = IOleDocumentView_SetRect(view, &rect);
    ok(hres == S_OK, "SetRect failed: %08x\n", hres);

    hres = IOleDocumentView_Show(view, TRUE);
    ok(hres == S_OK, "Show failed: %08x\n", hres);

    return S_OK;
}

static const IOleDocumentSiteVtbl DocumentSiteVtbl = {
    DocumentSite_QueryInterface,
    DocumentSite_AddRef,
    DocumentSite_Release,
    DocumentSite_ActivateMe
};

static IOleDocumentSite DocumentSite = { &DocumentSiteVtbl };

static HRESULT cs_qi(REFIID riid, void **ppv)
{
    *ppv = NULL;

    if(IsEqualGUID(&IID_IUnknown, riid) || IsEqualGUID(&IID_IOleClientSite, riid))
        *ppv = &ClientSite;
    else if(IsEqualGUID(&IID_IOleDocumentSite, riid))
        *ppv = &DocumentSite;
    else if(IsEqualGUID(&IID_IOleWindow, riid) || IsEqualGUID(&IID_IOleInPlaceSite, riid))
        *ppv = &InPlaceSite;

    return *ppv ? S_OK : E_NOINTERFACE;
}

static IHTMLDocument2 *notif_doc;
static BOOL doc_complete;

static HRESULT WINAPI PropertyNotifySink_QueryInterface(IPropertyNotifySink *iface,
        REFIID riid, void**ppv)
{
    if(IsEqualGUID(&IID_IPropertyNotifySink, riid)) {
        *ppv = iface;
        return S_OK;
    }

    ok(0, "unexpected call\n");
    return E_NOINTERFACE;
}

static ULONG WINAPI PropertyNotifySink_AddRef(IPropertyNotifySink *iface)
{
    return 2;
}

static ULONG WINAPI PropertyNotifySink_Release(IPropertyNotifySink *iface)
{
    return 1;
}

static HRESULT WINAPI PropertyNotifySink_OnChanged(IPropertyNotifySink *iface, DISPID dispID)
{
    if(dispID == DISPID_READYSTATE){
        BSTR state;
        HRESULT hres;

        static const WCHAR completeW[] = {'c','o','m','p','l','e','t','e',0};

        hres = IHTMLDocument2_get_readyState(notif_doc, &state);
        ok(hres == S_OK, "get_readyState failed: %08x\n", hres);

        if(!lstrcmpW(state, completeW))
            doc_complete = TRUE;

        SysFreeString(state);
    }

    return S_OK;
}

static HRESULT WINAPI PropertyNotifySink_OnRequestEdit(IPropertyNotifySink *iface, DISPID dispID)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static IPropertyNotifySinkVtbl PropertyNotifySinkVtbl = {
    PropertyNotifySink_QueryInterface,
    PropertyNotifySink_AddRef,
    PropertyNotifySink_Release,
    PropertyNotifySink_OnChanged,
    PropertyNotifySink_OnRequestEdit
};

static IPropertyNotifySink PropertyNotifySink = { &PropertyNotifySinkVtbl };

static void doc_load_string(IHTMLDocument2 *doc, const char *str)
{
    IPersistStreamInit *init;
    IStream *stream;
    HGLOBAL mem;
    SIZE_T len;

    notif_doc = doc;

    doc_complete = FALSE;
    len = strlen(str);
    mem = GlobalAlloc(0, len);
    memcpy(mem, str, len);
    CreateStreamOnHGlobal(mem, TRUE, &stream);

    IHTMLDocument2_QueryInterface(doc, &IID_IPersistStreamInit, (void**)&init);

    IPersistStreamInit_Load(init, stream);
    IPersistStreamInit_Release(init);
    IStream_Release(stream);
}

static void do_advise(IUnknown *unk, REFIID riid, IUnknown *unk_advise)
{
    IConnectionPointContainer *container;
    IConnectionPoint *cp;
    DWORD cookie;
    HRESULT hres;

    hres = IUnknown_QueryInterface(unk, &IID_IConnectionPointContainer, (void**)&container);
    ok(hres == S_OK, "QueryInterface(IID_IConnectionPointContainer) failed: %08x\n", hres);

    hres = IConnectionPointContainer_FindConnectionPoint(container, riid, &cp);
    IConnectionPointContainer_Release(container);
    ok(hres == S_OK, "FindConnectionPoint failed: %08x\n", hres);

    hres = IConnectionPoint_Advise(cp, unk_advise, &cookie);
    IConnectionPoint_Release(cp);
    ok(hres == S_OK, "Advise failed: %08x\n", hres);
}

static void set_client_site(IHTMLDocument2 *doc, BOOL set)
{
    IOleObject *oleobj;
    HRESULT hres;

    if(!set && view) {
        IOleDocumentView_Show(view, FALSE);
        IOleDocumentView_CloseView(view, 0);
        IOleDocumentView_SetInPlaceSite(view, NULL);
        IOleDocumentView_Release(view);
        view = NULL;
    }

    hres = IHTMLDocument2_QueryInterface(doc, &IID_IOleObject, (void**)&oleobj);
    ok(hres == S_OK, "Could not et IOleObject: %08x\n", hres);

    hres = IOleObject_SetClientSite(oleobj, set ? &ClientSite : NULL);
    ok(hres == S_OK, "SetClientSite failed: %08x\n", hres);

    if(set) {
        IHlinkTarget *hlink;

        hres = IOleObject_QueryInterface(oleobj, &IID_IHlinkTarget, (void**)&hlink);
        ok(hres == S_OK, "Could not get IHlinkTarget iface: %08x\n", hres);

        hres = IHlinkTarget_Navigate(hlink, 0, NULL);
        ok(hres == S_OK, "Navgate failed: %08x\n", hres);

        IHlinkTarget_Release(hlink);
    }

    IOleObject_Release(oleobj);
}
static IHTMLDocument2 *create_document(void)
{
    IHTMLDocument2 *doc;
    HRESULT hres;

    hres = CoCreateInstance(&CLSID_HTMLDocument, NULL, CLSCTX_INPROC_SERVER|CLSCTX_INPROC_HANDLER,
            &IID_IHTMLDocument2, (void**)&doc);
    ok(hres == S_OK, "CoCreateInstance failed: %08x\n", hres);

    return doc;
}

static IHTMLDocument2 *create_doc(const char *str, BOOL *b)
{
    IHTMLDocument2 *doc;
    MSG msg;

    doc = create_document();
    set_client_site(doc, TRUE);
    doc_load_string(doc, str);
    do_advise((IUnknown*)doc, &IID_IPropertyNotifySink, (IUnknown*)&PropertyNotifySink);

    while((!doc_complete || (b && !*b)) && GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return doc;
}

static void release_doc(IHTMLDocument2 *doc)
{
    ULONG ref;

    if(client_site) {
        IOleClientSite_Release(client_site);
        client_site = NULL;
    }

    set_client_site(doc, FALSE);
    ref = IHTMLDocument2_Release(doc);
    ok(!ref, "ref = %d\n", ref);
}

static void test_object_ax(void)
{
    IHTMLDocument2 *doc;

    /*
     * We pump messages until both document is loaded and plugin instance is created.
     * Pumping until document is loaded should be enough, but Gecko loads plugins
     * asynchronously and until we'll work around it, we need this hack.
     */
    SET_EXPECT(CreateInstance);
    SET_EXPECT(FreezeEvents_TRUE);
    SET_EXPECT(QuickActivate);
    SET_EXPECT(FreezeEvents_FALSE);
    SET_EXPECT(IPersistPropertyBag_Load);
    SET_EXPECT(Invoke_READYSTATE);

    doc = create_doc(object_ax_str, &called_CreateInstance);

    CHECK_CALLED(CreateInstance);
    todo_wine
    CHECK_CALLED(FreezeEvents_TRUE);
    CHECK_CALLED(QuickActivate);
    todo_wine
    CHECK_CALLED(FreezeEvents_FALSE);
    CHECK_CALLED(IPersistPropertyBag_Load);
    CHECK_CALLED(Invoke_READYSTATE);

    release_doc(doc);
}

static LRESULT WINAPI wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static HWND create_container_window(void)
{
    static const WCHAR html_document_testW[] =
        {'H','T','M','L','D','o','c','u','m','e','n','t','T','e','s','t',0};
    static WNDCLASSEXW wndclass = {
        sizeof(WNDCLASSEXW),
        0,
        wnd_proc,
        0, 0, NULL, NULL, NULL, NULL, NULL,
        html_document_testW,
        NULL
    };

    RegisterClassExW(&wndclass);
    return CreateWindowW(html_document_testW, html_document_testW,
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
            515, 530, NULL, NULL, NULL, NULL);
}

static BOOL init_key(const char *key_name, const char *def_value, BOOL init)
{
    HKEY hkey;
    DWORD res;

    if(!init) {
        RegDeleteKey(HKEY_CLASSES_ROOT, key_name);
        return TRUE;
    }

    res = RegCreateKeyA(HKEY_CLASSES_ROOT, key_name, &hkey);
    if(res != ERROR_SUCCESS)
        return FALSE;

    if(def_value)
        res = RegSetValueA(hkey, NULL, REG_SZ, def_value, strlen(def_value));

    RegCloseKey(hkey);

    return res == ERROR_SUCCESS;
}

static BOOL init_registry(BOOL init)
{
    return init_key("TestActiveX\\CLSID", TESTACTIVEX_CLSID, init)
        && init_key("CLSID\\"TESTACTIVEX_CLSID"\\Implemented Categories\\{7dd95801-9882-11cf-9fa9-00aa006c42c4}",
                    NULL, init)
        && init_key("CLSID\\"TESTACTIVEX_CLSID"\\Implemented Categories\\{7dd95802-9882-11cf-9fa9-00aa006c42c4}",
                    NULL, init);
}

static BOOL register_activex(void)
{
    DWORD regid;
    HRESULT hres;

    if(!init_registry(TRUE)) {
        init_registry(FALSE);
        return FALSE;
    }

    hres = CoRegisterClassObject(&CLSID_TestActiveX, (IUnknown*)&activex_cf,
            CLSCTX_INPROC_SERVER, REGCLS_MULTIPLEUSE, &regid);
    ok(hres == S_OK, "Could not register control: %08x\n", hres);

    return TRUE;
}

static BOOL check_ie(void)
{
    IHTMLDocument5 *doc;
    HRESULT hres;

    static const WCHAR xW[] = {'x',0};
    static const WCHAR yW[] = {'y',0};

    if(!lstrcmpW(xW, yW))
        return FALSE;

    hres = CoCreateInstance(&CLSID_HTMLDocument, NULL, CLSCTX_INPROC_SERVER|CLSCTX_INPROC_HANDLER,
            &IID_IHTMLDocument5, (void**)&doc);
    if(FAILED(hres))
        return FALSE;

    IHTMLDocument5_Release(doc);
    return TRUE;
}

START_TEST(activex)
{
    CoInitialize(NULL);

    if(!check_ie()) {
        CoUninitialize();
        win_skip("Too old IE\n");
        return;
    }

    if(is_ie_hardened()) {
        CoUninitialize();
        win_skip("IE running in Enhanced Security Configuration\n");
        return;
    }

    container_hwnd = create_container_window();
    ShowWindow(container_hwnd, SW_SHOW);

    if(register_activex()) {
        test_object_ax();
        init_registry(FALSE);
    }else {
        skip("Could not register ActiveX\n");
    }

    DestroyWindow(container_hwnd);
    CoUninitialize();
}
