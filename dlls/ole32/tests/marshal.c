/*
 * Marshaling Tests
 *
 * Copyright 2004 Robert Shearman
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

#define _WIN32_DCOM
#define COBJMACROS

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "objbase.h"

#include "wine/test.h"

/* functions that are not present on all versions of Windows */
HRESULT (WINAPI * pCoInitializeEx)(LPVOID lpReserved, DWORD dwCoInit);

/* helper macros to make tests a bit leaner */
#define ok_more_than_one_lock() ok(cLocks > 0, "Number of locks should be > 0, but actually is %ld\n", cLocks)
#define ok_no_locks() ok(cLocks == 0, "Number of locks should be 0, but actually is %ld\n", cLocks)
#define ok_ole_success(hr, func) ok(hr == S_OK, #func " failed with error 0x%08lx\n", hr)

static void test_CoGetPSClsid()
{
	HRESULT hr;
	CLSID clsid;
	static const CLSID IID_IWineTest = {
	    0x5201163f,
	    0x8164,
	    0x4fd0,
	    {0xa1, 0xa2, 0x5d, 0x5a, 0x36, 0x54, 0xd3, 0xbd}
	}; /* 5201163f-8164-4fd0-a1a2-5d5a3654d3bd */

	hr = CoGetPSClsid(&IID_IClassFactory, &clsid);
	ok_ole_success(hr, CoGetPSClsid);

	hr = CoGetPSClsid(&IID_IWineTest, &clsid);
	ok(hr == REGDB_E_IIDNOTREG,
	   "CoGetPSClsid for random IID returned 0x%08lx instead of REGDB_E_IIDNOTREG\n",
	   hr);
}

static const LARGE_INTEGER ullZero;
static LONG cLocks;

static void LockModule()
{
    InterlockedIncrement(&cLocks);
}

static void UnlockModule()
{
    InterlockedDecrement(&cLocks);
}


static HRESULT WINAPI Test_IUnknown_QueryInterface(
    LPUNKNOWN iface,
    REFIID riid,
    LPVOID *ppvObj)
{
    if (ppvObj == NULL) return E_POINTER;

    if (IsEqualGUID(riid, &IID_IUnknown))
    {
        *ppvObj = (LPVOID)iface;
        IUnknown_AddRef(iface);
        return S_OK;
    }

    *ppvObj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI Test_IUnknown_AddRef(LPUNKNOWN iface)
{
    LockModule();
    return 2; /* non-heap-based object */
}

static ULONG WINAPI Test_IUnknown_Release(LPUNKNOWN iface)
{
    UnlockModule();
    return 1; /* non-heap-based object */
}

static IUnknownVtbl TestUnknown_Vtbl =
{
    Test_IUnknown_QueryInterface,
    Test_IUnknown_AddRef,
    Test_IUnknown_Release,
};

static IUnknown Test_Unknown = { &TestUnknown_Vtbl };


static HRESULT WINAPI Test_IClassFactory_QueryInterface(
    LPCLASSFACTORY iface,
    REFIID riid,
    LPVOID *ppvObj)
{
    if (ppvObj == NULL) return E_POINTER;

    if (IsEqualGUID(riid, &IID_IUnknown) ||
        IsEqualGUID(riid, &IID_IClassFactory))
    {
        *ppvObj = (LPVOID)iface;
        IClassFactory_AddRef(iface);
        return S_OK;
    }

    *ppvObj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI Test_IClassFactory_AddRef(LPCLASSFACTORY iface)
{
    LockModule();
    return 2; /* non-heap-based object */
}

static ULONG WINAPI Test_IClassFactory_Release(LPCLASSFACTORY iface)
{
    UnlockModule();
    return 1; /* non-heap-based object */
}

static HRESULT WINAPI Test_IClassFactory_CreateInstance(
    LPCLASSFACTORY iface,
    LPUNKNOWN pUnkOuter,
    REFIID riid,
    LPVOID *ppvObj)
{
    if (pUnkOuter) return CLASS_E_NOAGGREGATION;
    return IUnknown_QueryInterface((IUnknown*)&Test_Unknown, riid, ppvObj);
}

static HRESULT WINAPI Test_IClassFactory_LockServer(
    LPCLASSFACTORY iface,
    BOOL fLock)
{
    return S_OK;
}

static IClassFactoryVtbl TestClassFactory_Vtbl =
{
    Test_IClassFactory_QueryInterface,
    Test_IClassFactory_AddRef,
    Test_IClassFactory_Release,
    Test_IClassFactory_CreateInstance,
    Test_IClassFactory_LockServer
};

static IClassFactory Test_ClassFactory = { &TestClassFactory_Vtbl };

#define RELEASEMARSHALDATA WM_USER

struct host_object_data
{
    IStream *stream;
    IID iid;
    IUnknown *object;
    MSHLFLAGS marshal_flags;
    HANDLE marshal_event;
    IMessageFilter *filter;
};

static DWORD CALLBACK host_object_proc(LPVOID p)
{
    struct host_object_data *data = (struct host_object_data *)p;
    HRESULT hr;
    MSG msg;

    pCoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    if (data->filter)
    {
        IMessageFilter * prev_filter = NULL;
        hr = CoRegisterMessageFilter(data->filter, &prev_filter);
        if (prev_filter) IMessageFilter_Release(prev_filter);
        ok_ole_success(hr, CoRegisterMessageFilter);
    }

    hr = CoMarshalInterface(data->stream, &data->iid, data->object, MSHCTX_INPROC, NULL, data->marshal_flags);
    ok_ole_success(hr, CoMarshalInterface);

    /* force the message queue to be created before signaling parent thread */
    PeekMessage(&msg, NULL, WM_USER, WM_USER, PM_NOREMOVE);

    SetEvent(data->marshal_event);

    while (GetMessage(&msg, NULL, 0, 0))
    {
        if (msg.hwnd == NULL && msg.message == RELEASEMARSHALDATA)
        {
            trace("releasing marshal data\n");
            CoReleaseMarshalData(data->stream);
            SetEvent((HANDLE)msg.lParam);
        }
        else
            DispatchMessage(&msg);
    }

    HeapFree(GetProcessHeap(), 0, data);

    CoUninitialize();

    return hr;
}

static DWORD start_host_object2(IStream *stream, REFIID riid, IUnknown *object, MSHLFLAGS marshal_flags, IMessageFilter *filter, HANDLE *thread)
{
    DWORD tid = 0;
    HANDLE marshal_event = CreateEvent(NULL, FALSE, FALSE, NULL);
    struct host_object_data *data = (struct host_object_data *)HeapAlloc(GetProcessHeap(), 0, sizeof(*data));

    data->stream = stream;
    data->iid = *riid;
    data->object = object;
    data->marshal_flags = marshal_flags;
    data->marshal_event = marshal_event;
    data->filter = filter;

    *thread = CreateThread(NULL, 0, host_object_proc, data, 0, &tid);

    /* wait for marshaling to complete before returning */
    WaitForSingleObject(marshal_event, INFINITE);
    CloseHandle(marshal_event);

    return tid;
}

static DWORD start_host_object(IStream *stream, REFIID riid, IUnknown *object, MSHLFLAGS marshal_flags, HANDLE *thread)
{
    return start_host_object2(stream, riid, object, marshal_flags, NULL, thread);
}

/* asks thread to release the marshal data because it has to be done by the
 * same thread that marshaled the interface in the first place. */
static void release_host_object(DWORD tid)
{
    HANDLE event = CreateEvent(NULL, FALSE, FALSE, NULL);
    PostThreadMessage(tid, RELEASEMARSHALDATA, 0, (LPARAM)event);
    WaitForSingleObject(event, INFINITE);
    CloseHandle(event);
}

static void end_host_object(DWORD tid, HANDLE thread)
{
    BOOL ret = PostThreadMessage(tid, WM_QUIT, 0, 0);
    ok(ret, "PostThreadMessage failed with error %ld\n", GetLastError());
    /* be careful of races - don't return until hosting thread has terminated */
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
}

/* tests failure case of interface not having a marshaler specified in the
 * registry */
static void test_no_marshaler()
{
    IStream *pStream;
    HRESULT hr;
    static const CLSID IID_IWineTest = {
        0x5201163f,
        0x8164,
        0x4fd0,
        {0xa1, 0xa2, 0x5d, 0x5a, 0x36, 0x54, 0xd3, 0xbd}
    }; /* 5201163f-8164-4fd0-a1a2-5d5a3654d3bd */

    hr = CreateStreamOnHGlobal(NULL, TRUE, &pStream);
    ok_ole_success(hr, CreateStreamOnHGlobal);
    hr = CoMarshalInterface(pStream, &IID_IWineTest, (IUnknown*)&Test_ClassFactory, MSHCTX_INPROC, NULL, MSHLFLAGS_NORMAL);
    ok(hr == E_NOINTERFACE, "CoMarshalInterface should have returned E_NOINTERFACE instead of 0x%08lx\n", hr);

    IStream_Release(pStream);
}

/* tests normal marshal and then release without unmarshaling */
static void test_normal_marshal_and_release()
{
    HRESULT hr;
    IStream *pStream = NULL;

    cLocks = 0;

    hr = CreateStreamOnHGlobal(NULL, TRUE, &pStream);
    ok_ole_success(hr, CreateStreamOnHGlobal);
    hr = CoMarshalInterface(pStream, &IID_IClassFactory, (IUnknown*)&Test_ClassFactory, MSHCTX_INPROC, NULL, MSHLFLAGS_NORMAL);
    ok_ole_success(hr, CoMarshalInterface);

    ok_more_than_one_lock();

    IStream_Seek(pStream, ullZero, STREAM_SEEK_SET, NULL);
    hr = CoReleaseMarshalData(pStream);
    ok_ole_success(hr, CoReleaseMarshalData);
    IStream_Release(pStream);

    ok_no_locks();
}

/* tests success case of a same-thread marshal and unmarshal */
static void test_normal_marshal_and_unmarshal()
{
    HRESULT hr;
    IStream *pStream = NULL;
    IUnknown *pProxy = NULL;

    cLocks = 0;

    hr = CreateStreamOnHGlobal(NULL, TRUE, &pStream);
    ok_ole_success(hr, CreateStreamOnHGlobal);
    hr = CoMarshalInterface(pStream, &IID_IClassFactory, (IUnknown*)&Test_ClassFactory, MSHCTX_INPROC, NULL, MSHLFLAGS_NORMAL);
    ok_ole_success(hr, CoMarshalInterface);

    ok_more_than_one_lock();
    
    IStream_Seek(pStream, ullZero, STREAM_SEEK_SET, NULL);
    hr = CoUnmarshalInterface(pStream, &IID_IClassFactory, (void **)&pProxy);
    ok_ole_success(hr, CoUnmarshalInterface);
    IStream_Release(pStream);

    ok_more_than_one_lock();

    IUnknown_Release(pProxy);

    ok_no_locks();
}

/* tests failure case of a unmarshaling an freed object */
static void test_marshal_and_unmarshal_invalid()
{
    HRESULT hr;
    IStream *pStream = NULL;
    IClassFactory *pProxy = NULL;
    DWORD tid;
    void * dummy;
    HANDLE thread;

    cLocks = 0;

    hr = CreateStreamOnHGlobal(NULL, TRUE, &pStream);
    ok_ole_success(hr, CreateStreamOnHGlobal);
    tid = start_host_object(pStream, &IID_IClassFactory, (IUnknown*)&Test_ClassFactory, MSHLFLAGS_NORMAL, &thread);

    ok_more_than_one_lock();
	
    IStream_Seek(pStream, ullZero, STREAM_SEEK_SET, NULL);
    hr = CoReleaseMarshalData(pStream);
    ok_ole_success(hr, CoReleaseMarshalData);

    ok_no_locks();

    IStream_Seek(pStream, ullZero, STREAM_SEEK_SET, NULL);
    hr = CoUnmarshalInterface(pStream, &IID_IClassFactory, (void **)&pProxy);
    todo_wine { ok_ole_success(hr, CoUnmarshalInterface); }

    ok_no_locks();

    if (pProxy)
    {
        hr = IClassFactory_CreateInstance(pProxy, NULL, &IID_IUnknown, &dummy);
        ok(hr == RPC_E_DISCONNECTED, "Remote call should have returned RPC_E_DISCONNECTED, instead of 0x%08lx\n", hr);

        IClassFactory_Release(pProxy);
    }

    IStream_Release(pStream);

    end_host_object(tid, thread);
}

/* tests success case of an interthread marshal */
static void test_interthread_marshal_and_unmarshal()
{
    HRESULT hr;
    IStream *pStream = NULL;
    IUnknown *pProxy = NULL;
    DWORD tid;
    HANDLE thread;

    cLocks = 0;

    hr = CreateStreamOnHGlobal(NULL, TRUE, &pStream);
    ok_ole_success(hr, CreateStreamOnHGlobal);
    tid = start_host_object(pStream, &IID_IClassFactory, (IUnknown*)&Test_ClassFactory, MSHLFLAGS_NORMAL, &thread);

    ok_more_than_one_lock();
    
    IStream_Seek(pStream, ullZero, STREAM_SEEK_SET, NULL);
    hr = CoUnmarshalInterface(pStream, &IID_IClassFactory, (void **)&pProxy);
    ok_ole_success(hr, CoUnmarshalInterface);
    IStream_Release(pStream);

    ok_more_than_one_lock();

    IUnknown_Release(pProxy);

    ok_no_locks();

    end_host_object(tid, thread);
}

/* tests that stubs are released when the containing apartment is destroyed */
static void test_marshal_stub_apartment_shutdown()
{
    HRESULT hr;
    IStream *pStream = NULL;
    IUnknown *pProxy = NULL;
    DWORD tid;
    HANDLE thread;

    cLocks = 0;

    hr = CreateStreamOnHGlobal(NULL, TRUE, &pStream);
    ok_ole_success(hr, CreateStreamOnHGlobal);
    tid = start_host_object(pStream, &IID_IClassFactory, (IUnknown*)&Test_ClassFactory, MSHLFLAGS_NORMAL, &thread);

    ok_more_than_one_lock();
    
    IStream_Seek(pStream, ullZero, STREAM_SEEK_SET, NULL);
    hr = CoUnmarshalInterface(pStream, &IID_IClassFactory, (void **)&pProxy);
    ok_ole_success(hr, CoUnmarshalInterface);
    IStream_Release(pStream);

    ok_more_than_one_lock();

    end_host_object(tid, thread);

    ok_no_locks();

    IUnknown_Release(pProxy);

    ok_no_locks();
}

/* tests that proxies are released when the containing apartment is destroyed */
static void test_marshal_proxy_apartment_shutdown()
{
    HRESULT hr;
    IStream *pStream = NULL;
    IUnknown *pProxy = NULL;
    DWORD tid;
    HANDLE thread;

    cLocks = 0;

    hr = CreateStreamOnHGlobal(NULL, TRUE, &pStream);
    ok_ole_success(hr, CreateStreamOnHGlobal);
    tid = start_host_object(pStream, &IID_IClassFactory, (IUnknown*)&Test_ClassFactory, MSHLFLAGS_NORMAL, &thread);

    ok_more_than_one_lock();
    
    IStream_Seek(pStream, ullZero, STREAM_SEEK_SET, NULL);
    hr = CoUnmarshalInterface(pStream, &IID_IClassFactory, (void **)&pProxy);
    ok_ole_success(hr, CoUnmarshalInterface);
    IStream_Release(pStream);

    ok_more_than_one_lock();

    CoUninitialize();

    ok_no_locks();

    IUnknown_Release(pProxy);

    ok_no_locks();

    end_host_object(tid, thread);

    pCoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
}

/* tests that proxies are released when the containing mta apartment is destroyed */
static void test_marshal_proxy_mta_apartment_shutdown()
{
    HRESULT hr;
    IStream *pStream = NULL;
    IUnknown *pProxy = NULL;
    DWORD tid;
    HANDLE thread;

    CoUninitialize();
    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    cLocks = 0;

    hr = CreateStreamOnHGlobal(NULL, TRUE, &pStream);
    ok_ole_success(hr, CreateStreamOnHGlobal);
    tid = start_host_object(pStream, &IID_IClassFactory, (IUnknown*)&Test_ClassFactory, MSHLFLAGS_NORMAL, &thread);

    ok_more_than_one_lock();
	
    IStream_Seek(pStream, ullZero, STREAM_SEEK_SET, NULL);
    hr = CoUnmarshalInterface(pStream, &IID_IClassFactory, (void **)&pProxy);
    ok_ole_success(hr, CoUnmarshalInterface);
    IStream_Release(pStream);

    ok_more_than_one_lock();

    CoUninitialize();

    ok_no_locks();

    IUnknown_Release(pProxy);

    ok_no_locks();

    end_host_object(tid, thread);

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
}

struct ncu_params
{
	LPSTREAM stream;
	HANDLE marshal_event;
	HANDLE unmarshal_event;
};

/* helper for test_proxy_used_in_wrong_thread */
static DWORD CALLBACK no_couninitialize_proc(LPVOID p)
{
	struct ncu_params *ncu_params = (struct ncu_params *)p;
	HRESULT hr;

	CoInitializeEx(NULL, COINIT_MULTITHREADED);

	hr = CoMarshalInterface(ncu_params->stream, &IID_IClassFactory, (IUnknown*)&Test_ClassFactory, MSHCTX_INPROC, NULL, MSHLFLAGS_NORMAL);
	ok_ole_success(hr, CoMarshalInterface);

	SetEvent(ncu_params->marshal_event);

	WaitForSingleObject(ncu_params->unmarshal_event, INFINITE);

	/* die without calling CoUninitialize */

	return 0;
}

/* tests apartment that an apartment is released if the owning thread exits */
static void test_no_couninitialize()
{
    HRESULT hr;
    IStream *pStream = NULL;
    IUnknown *pProxy = NULL;
    DWORD tid;
    HANDLE thread;
    struct ncu_params ncu_params;

    cLocks = 0;

    ncu_params.marshal_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    ncu_params.unmarshal_event = CreateEvent(NULL, TRUE, FALSE, NULL);

    hr = CreateStreamOnHGlobal(NULL, TRUE, &pStream);
    ok_ole_success(hr, CreateStreamOnHGlobal);
    ncu_params.stream = pStream;

    thread = CreateThread(NULL, 0, no_couninitialize_proc, &ncu_params, 0, &tid);

    WaitForSingleObject(ncu_params.marshal_event, INFINITE);
    ok_more_than_one_lock();
	
    IStream_Seek(pStream, ullZero, STREAM_SEEK_SET, NULL);
    hr = CoUnmarshalInterface(pStream, &IID_IClassFactory, (void **)&pProxy);
    ok_ole_success(hr, CoUnmarshalInterface);
    IStream_Release(pStream);

    ok_more_than_one_lock();

    SetEvent(ncu_params.unmarshal_event);
    WaitForSingleObject(thread, INFINITE);

    ok_no_locks();

    CloseHandle(thread);
    CloseHandle(ncu_params.marshal_event);
    CloseHandle(ncu_params.unmarshal_event);

    IUnknown_Release(pProxy);

    ok_no_locks();
}

/* tests success case of a same-thread table-weak marshal, unmarshal, unmarshal */
static void test_tableweak_marshal_and_unmarshal_twice()
{
    HRESULT hr;
    IStream *pStream = NULL;
    IUnknown *pProxy1 = NULL;
    IUnknown *pProxy2 = NULL;
    DWORD tid;
    HANDLE thread;

    cLocks = 0;

    hr = CreateStreamOnHGlobal(NULL, TRUE, &pStream);
    ok_ole_success(hr, CreateStreamOnHGlobal);
    tid = start_host_object(pStream, &IID_IClassFactory, (IUnknown*)&Test_ClassFactory, MSHLFLAGS_TABLEWEAK, &thread);

    ok_more_than_one_lock();

    IStream_Seek(pStream, ullZero, STREAM_SEEK_SET, NULL);
    hr = CoUnmarshalInterface(pStream, &IID_IClassFactory, (void **)&pProxy1);
    ok_ole_success(hr, CoUnmarshalInterface);

    ok_more_than_one_lock();

    IStream_Seek(pStream, ullZero, STREAM_SEEK_SET, NULL);
    hr = CoUnmarshalInterface(pStream, &IID_IClassFactory, (void **)&pProxy2);
    ok_ole_success(hr, CoUnmarshalInterface);

    ok_more_than_one_lock();

    IUnknown_Release(pProxy1);
    IUnknown_Release(pProxy2);

    /* this line is shows the difference between weak and strong table marshaling:
     *  weak has cLocks == 0
     *  strong has cLocks > 0 */
    ok_no_locks();

    end_host_object(tid, thread);
}

/* tests releasing after unmarshaling one object */
static void test_tableweak_marshal_releasedata1()
{
    HRESULT hr;
    IStream *pStream = NULL;
    IUnknown *pProxy1 = NULL;
    IUnknown *pProxy2 = NULL;
    DWORD tid;
    HANDLE thread;

    cLocks = 0;

    hr = CreateStreamOnHGlobal(NULL, TRUE, &pStream);
    ok_ole_success(hr, CreateStreamOnHGlobal);
    tid = start_host_object(pStream, &IID_IClassFactory, (IUnknown*)&Test_ClassFactory, MSHLFLAGS_TABLEWEAK, &thread);

    ok_more_than_one_lock();

    IStream_Seek(pStream, ullZero, STREAM_SEEK_SET, NULL);
    hr = CoUnmarshalInterface(pStream, &IID_IClassFactory, (void **)&pProxy1);
    ok_ole_success(hr, CoUnmarshalInterface);

    ok_more_than_one_lock();

    /* release the remaining reference on the object by calling
     * CoReleaseMarshalData in the hosting thread */
    IStream_Seek(pStream, ullZero, STREAM_SEEK_SET, NULL);
    release_host_object(tid);

    todo_wine { ok_more_than_one_lock(); }

    IStream_Seek(pStream, ullZero, STREAM_SEEK_SET, NULL);
    hr = CoUnmarshalInterface(pStream, &IID_IClassFactory, (void **)&pProxy2);
    todo_wine { ok_ole_success(hr, CoUnmarshalInterface); }
    IStream_Release(pStream);

    todo_wine { ok_more_than_one_lock(); }

    IUnknown_Release(pProxy1);
    if (pProxy2)
        IUnknown_Release(pProxy2);

    /* this line is shows the difference between weak and strong table marshaling:
     *  weak has cLocks == 0
     *  strong has cLocks > 0 */
    ok_no_locks();

    end_host_object(tid, thread);
}

/* tests releasing after unmarshaling one object */
static void test_tableweak_marshal_releasedata2()
{
    HRESULT hr;
    IStream *pStream = NULL;
    IUnknown *pProxy = NULL;
    DWORD tid;
    HANDLE thread;

    cLocks = 0;

    hr = CreateStreamOnHGlobal(NULL, TRUE, &pStream);
    ok_ole_success(hr, CreateStreamOnHGlobal);
    tid = start_host_object(pStream, &IID_IClassFactory, (IUnknown*)&Test_ClassFactory, MSHLFLAGS_TABLEWEAK, &thread);

    ok_more_than_one_lock();

    /* release the remaining reference on the object by calling
     * CoReleaseMarshalData in the hosting thread */
    IStream_Seek(pStream, ullZero, STREAM_SEEK_SET, NULL);
    release_host_object(tid);

    todo_wine
    {

    ok_no_locks();

    IStream_Seek(pStream, ullZero, STREAM_SEEK_SET, NULL);
    hr = CoUnmarshalInterface(pStream, &IID_IClassFactory, (void **)&pProxy);
    ok(hr == CO_E_OBJNOTREG,
       "CoUnmarshalInterface should have failed with CO_E_OBJNOTREG, but returned 0x%08lx instead\n",
       hr);
    IStream_Release(pStream);

    ok_no_locks();
    }

    end_host_object(tid, thread);
}

/* tests success case of a same-thread table-strong marshal, unmarshal, unmarshal */
static void test_tablestrong_marshal_and_unmarshal_twice()
{
    HRESULT hr;
    IStream *pStream = NULL;
    IUnknown *pProxy1 = NULL;
    IUnknown *pProxy2 = NULL;
    DWORD tid;
    HANDLE thread;

    cLocks = 0;

    hr = CreateStreamOnHGlobal(NULL, TRUE, &pStream);
    ok_ole_success(hr, CreateStreamOnHGlobal);
    tid = start_host_object(pStream, &IID_IClassFactory, (IUnknown*)&Test_ClassFactory, MSHLFLAGS_TABLESTRONG, &thread);

    ok_more_than_one_lock();

    IStream_Seek(pStream, ullZero, STREAM_SEEK_SET, NULL);
    hr = CoUnmarshalInterface(pStream, &IID_IClassFactory, (void **)&pProxy1);
    ok_ole_success(hr, CoUnmarshalInterface);

    ok_more_than_one_lock();

    IStream_Seek(pStream, ullZero, STREAM_SEEK_SET, NULL);
    hr = CoUnmarshalInterface(pStream, &IID_IClassFactory, (void **)&pProxy2);
    ok_ole_success(hr, CoUnmarshalInterface);

    ok_more_than_one_lock();

    if (pProxy1) IUnknown_Release(pProxy1);
    if (pProxy2) IUnknown_Release(pProxy2);

    /* this line is shows the difference between weak and strong table marshaling:
     *  weak has cLocks == 0
     *  strong has cLocks > 0 */
    ok_more_than_one_lock();

    /* release the remaining reference on the object by calling
     * CoReleaseMarshalData in the hosting thread */
    IStream_Seek(pStream, ullZero, STREAM_SEEK_SET, NULL);
    release_host_object(tid);
    IStream_Release(pStream);

    ok_no_locks();

    end_host_object(tid, thread);
}

/* tests CoLockObjectExternal */
static void test_lock_object_external()
{
    HRESULT hr;
    IStream *pStream = NULL;

    cLocks = 0;

    hr = CreateStreamOnHGlobal(NULL, TRUE, &pStream);
    ok_ole_success(hr, CreateStreamOnHGlobal);
    hr = CoMarshalInterface(pStream, &IID_IClassFactory, (IUnknown*)&Test_ClassFactory, MSHCTX_INPROC, NULL, MSHLFLAGS_NORMAL);
    ok_ole_success(hr, CoMarshalInterface);

    CoLockObjectExternal((IUnknown*)&Test_ClassFactory, TRUE, TRUE);

    ok_more_than_one_lock();
    
    IStream_Seek(pStream, ullZero, STREAM_SEEK_SET, NULL);
    hr = CoReleaseMarshalData(pStream);
    ok_ole_success(hr, CoReleaseMarshalData);
    IStream_Release(pStream);

    ok_more_than_one_lock();

    CoLockObjectExternal((IUnknown*)&Test_ClassFactory, FALSE, TRUE);

    ok_no_locks();
}

/* tests disconnecting stubs */
static void test_disconnect_stub()
{
    HRESULT hr;
    IStream *pStream = NULL;

    cLocks = 0;

    hr = CreateStreamOnHGlobal(NULL, TRUE, &pStream);
    ok_ole_success(hr, CreateStreamOnHGlobal);
    hr = CoMarshalInterface(pStream, &IID_IClassFactory, (IUnknown*)&Test_ClassFactory, MSHCTX_INPROC, NULL, MSHLFLAGS_NORMAL);
    ok_ole_success(hr, CoMarshalInterface);

    CoLockObjectExternal((IUnknown*)&Test_ClassFactory, TRUE, TRUE);

    ok_more_than_one_lock();
    
    IStream_Seek(pStream, ullZero, STREAM_SEEK_SET, NULL);
    hr = CoReleaseMarshalData(pStream);
    ok_ole_success(hr, CoReleaseMarshalData);
    IStream_Release(pStream);

    ok_more_than_one_lock();

    CoDisconnectObject((IUnknown*)&Test_ClassFactory, 0);

    todo_wine { ok_no_locks(); }
}

/* tests failure case of a same-thread marshal and unmarshal twice */
static void test_normal_marshal_and_unmarshal_twice()
{
    HRESULT hr;
    IStream *pStream = NULL;
    IUnknown *pProxy1 = NULL;
    IUnknown *pProxy2 = NULL;

    cLocks = 0;

    hr = CreateStreamOnHGlobal(NULL, TRUE, &pStream);
    ok_ole_success(hr, CreateStreamOnHGlobal);
    hr = CoMarshalInterface(pStream, &IID_IClassFactory, (IUnknown*)&Test_ClassFactory, MSHCTX_INPROC, NULL, MSHLFLAGS_NORMAL);
    ok_ole_success(hr, CoMarshalInterface);

    ok_more_than_one_lock();
    
    IStream_Seek(pStream, ullZero, STREAM_SEEK_SET, NULL);
    hr = CoUnmarshalInterface(pStream, &IID_IClassFactory, (void **)&pProxy1);
    ok_ole_success(hr, CoUnmarshalInterface);

    ok_more_than_one_lock();

    IStream_Seek(pStream, ullZero, STREAM_SEEK_SET, NULL);
    hr = CoUnmarshalInterface(pStream, &IID_IClassFactory, (void **)&pProxy2);
    ok(hr == CO_E_OBJNOTCONNECTED,
        "CoUnmarshalInterface should have failed with error CO_E_OBJNOTCONNECTED for double unmarshal, instead of 0x%08lx\n", hr);

    IStream_Release(pStream);

    ok_more_than_one_lock();

    IUnknown_Release(pProxy1);

    ok_no_locks();
}

/* tests success case of marshaling and unmarshaling an HRESULT */
static void test_hresult_marshaling()
{
    HRESULT hr;
    HRESULT hr_marshaled = 0;
    IStream *pStream = NULL;
    static const HRESULT E_DEADBEEF = 0xdeadbeef;

    hr = CreateStreamOnHGlobal(NULL, TRUE, &pStream);
    ok_ole_success(hr, CreateStreamOnHGlobal);

    hr = CoMarshalHresult(pStream, E_DEADBEEF);
    ok_ole_success(hr, CoMarshalHresult);

    IStream_Seek(pStream, ullZero, STREAM_SEEK_SET, NULL);
    hr = IStream_Read(pStream, &hr_marshaled, sizeof(HRESULT), NULL);
    ok_ole_success(hr, IStream_Read);

    ok(hr_marshaled == E_DEADBEEF, "Didn't marshal HRESULT as expected: got value 0x%08lx instead\n", hr_marshaled);

    hr_marshaled = 0;
    IStream_Seek(pStream, ullZero, STREAM_SEEK_SET, NULL);
    hr = CoUnmarshalHresult(pStream, &hr_marshaled);
    ok_ole_success(hr, CoUnmarshalHresult);

    ok(hr_marshaled == E_DEADBEEF, "Didn't marshal HRESULT as expected: got value 0x%08lx instead\n", hr_marshaled);

    IStream_Release(pStream);
}


/* helper for test_proxy_used_in_wrong_thread */
static DWORD CALLBACK bad_thread_proc(LPVOID p)
{
    IClassFactory * cf = (IClassFactory *)p;
    HRESULT hr;
    IUnknown * proxy = NULL;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    
    hr = IClassFactory_CreateInstance(cf, NULL, &IID_IUnknown, (LPVOID*)&proxy);
    if (proxy) IUnknown_Release(proxy);
    todo_wine {
    ok(hr == RPC_E_WRONG_THREAD,
        "COM should have failed with RPC_E_WRONG_THREAD on using proxy from wrong apartment, but instead returned 0x%08lx\n",
        hr);
    }

    CoUninitialize();

    return 0;
}

/* tests failure case of a using a proxy in the wrong apartment */
static void test_proxy_used_in_wrong_thread()
{
    HRESULT hr;
    IStream *pStream = NULL;
    IUnknown *pProxy = NULL;
    DWORD tid, tid2;
    HANDLE thread;
    HANDLE host_thread;

    cLocks = 0;

    hr = CreateStreamOnHGlobal(NULL, TRUE, &pStream);
    ok_ole_success(hr, CreateStreamOnHGlobal);
    tid = start_host_object(pStream, &IID_IClassFactory, (IUnknown*)&Test_ClassFactory, MSHLFLAGS_NORMAL, &host_thread);

    ok_more_than_one_lock();
    
    IStream_Seek(pStream, ullZero, STREAM_SEEK_SET, NULL);
    hr = CoUnmarshalInterface(pStream, &IID_IClassFactory, (void **)&pProxy);
    ok_ole_success(hr, CoUnmarshalInterface);
    IStream_Release(pStream);

    ok_more_than_one_lock();

    /* create a thread that we can misbehave in */
    thread = CreateThread(NULL, 0, bad_thread_proc, (LPVOID)pProxy, 0, &tid2);

    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);

    IUnknown_Release(pProxy);

    ok_no_locks();

    end_host_object(tid, host_thread);
}

static HRESULT WINAPI MessageFilter_QueryInterface(IMessageFilter *iface, REFIID riid, void ** ppvObj)
{
    if (ppvObj == NULL) return E_POINTER;

    if (IsEqualGUID(riid, &IID_IUnknown) ||
        IsEqualGUID(riid, &IID_IClassFactory))
    {
        *ppvObj = (LPVOID)iface;
        IClassFactory_AddRef(iface);
        return S_OK;
    }

    return E_NOINTERFACE;
}

static ULONG WINAPI MessageFilter_AddRef(IMessageFilter *iface)
{
    return 2; /* non-heap object */
}

static ULONG WINAPI MessageFilter_Release(IMessageFilter *iface)
{
    return 1; /* non-heap object */
}

static DWORD WINAPI MessageFilter_HandleInComingCall(
  IMessageFilter *iface,
  DWORD dwCallType,
  HTASK threadIDCaller,
  DWORD dwTickCount,
  LPINTERFACEINFO lpInterfaceInfo)
{
    static int callcount = 0;
    DWORD ret;
    trace("HandleInComingCall\n");
    switch (callcount)
    {
    case 0:
        ret = SERVERCALL_REJECTED;
        break;
    case 1:
        ret = SERVERCALL_RETRYLATER;
        break;
    default:
        ret = SERVERCALL_ISHANDLED;
        break;
    }
    callcount++;
    return ret;
}

static DWORD WINAPI MessageFilter_RetryRejectedCall(
  IMessageFilter *iface,
  HTASK threadIDCallee,
  DWORD dwTickCount,
  DWORD dwRejectType)
{
    trace("RetryRejectedCall\n");
    return 0;
}

static DWORD WINAPI MessageFilter_MessagePending(
  IMessageFilter *iface,
  HTASK threadIDCallee,
  DWORD dwTickCount,
  DWORD dwPendingType)
{
    trace("MessagePending\n");
    return PENDINGMSG_WAITNOPROCESS;
}

static IMessageFilterVtbl MessageFilter_Vtbl =
{
    MessageFilter_QueryInterface,
    MessageFilter_AddRef,
    MessageFilter_Release,
    MessageFilter_HandleInComingCall,
    MessageFilter_RetryRejectedCall,
    MessageFilter_MessagePending
};

static IMessageFilter MessageFilter = { &MessageFilter_Vtbl };

static void test_message_filter()
{
    HRESULT hr;
    IStream *pStream = NULL;
    IClassFactory *cf = NULL;
    DWORD tid;
    IUnknown *proxy = NULL;
    IMessageFilter *prev_filter = NULL;
    HANDLE thread;

    cLocks = 0;

    hr = CreateStreamOnHGlobal(NULL, TRUE, &pStream);
    ok_ole_success(hr, CreateStreamOnHGlobal);
    tid = start_host_object2(pStream, &IID_IClassFactory, (IUnknown*)&Test_ClassFactory, MSHLFLAGS_NORMAL, &MessageFilter, &thread);

    ok_more_than_one_lock();

    IStream_Seek(pStream, ullZero, STREAM_SEEK_SET, NULL);
    hr = CoUnmarshalInterface(pStream, &IID_IClassFactory, (void **)&cf);
    ok_ole_success(hr, CoUnmarshalInterface);
    IStream_Release(pStream);

    ok_more_than_one_lock();

    hr = IClassFactory_CreateInstance(cf, NULL, &IID_IUnknown, (LPVOID*)&proxy);
    todo_wine { ok(hr == RPC_E_CALL_REJECTED, "Call should have returned RPC_E_CALL_REJECTED, but return 0x%08lx instead\n", hr); }
    if (proxy) IUnknown_Release(proxy);
    proxy = NULL;

    hr = CoRegisterMessageFilter(&MessageFilter, &prev_filter);
    ok_ole_success(hr, CoRegisterMessageFilter);
    if (prev_filter) IMessageFilter_Release(prev_filter);

    hr = IClassFactory_CreateInstance(cf, NULL, &IID_IUnknown, (LPVOID*)&proxy);
    ok_ole_success(hr, IClassFactory_CreateInstance);

    IUnknown_Release(proxy);

    IClassFactory_Release(cf);

    ok_no_locks();

    end_host_object(tid, thread);
}

/* test failure case of trying to unmarshal from bad stream */
static void test_bad_marshal_stream()
{
    HRESULT hr;
    IStream *pStream = NULL;

    hr = CreateStreamOnHGlobal(NULL, TRUE, &pStream);
    ok_ole_success(hr, CreateStreamOnHGlobal);
    hr = CoMarshalInterface(pStream, &IID_IClassFactory, (IUnknown*)&Test_ClassFactory, MSHCTX_INPROC, NULL, MSHLFLAGS_NORMAL);
    ok_ole_success(hr, CoMarshalInterface);

    ok_more_than_one_lock();

    /* try to read beyond end of stream */
    hr = CoReleaseMarshalData(pStream);
    ok(hr == STG_E_READFAULT, "Should have failed with STG_E_READFAULT, but returned 0x%08lx instead\n", hr);

    /* now release for real */
    IStream_Seek(pStream, ullZero, STREAM_SEEK_SET, NULL);
    hr = CoReleaseMarshalData(pStream);
    ok_ole_success(hr, CoReleaseMarshalData);

    IStream_Release(pStream);
}

/* tests that proxies implement certain interfaces */
static void test_proxy_interfaces()
{
    HRESULT hr;
    IStream *pStream = NULL;
    IUnknown *pProxy = NULL;
    IUnknown *pOtherUnknown = NULL;
    DWORD tid;
    HANDLE thread;

    cLocks = 0;

    hr = CreateStreamOnHGlobal(NULL, TRUE, &pStream);
    ok_ole_success(hr, CreateStreamOnHGlobal);
    tid = start_host_object(pStream, &IID_IClassFactory, (IUnknown*)&Test_ClassFactory, MSHLFLAGS_NORMAL, &thread);

    ok_more_than_one_lock();
	
    IStream_Seek(pStream, ullZero, STREAM_SEEK_SET, NULL);
    hr = CoUnmarshalInterface(pStream, &IID_IUnknown, (void **)&pProxy);
    ok_ole_success(hr, CoUnmarshalInterface);
    IStream_Release(pStream);

    ok_more_than_one_lock();

    hr = IUnknown_QueryInterface(pProxy, &IID_IUnknown, (LPVOID*)&pOtherUnknown);
    ok_ole_success(hr, IUnknown_QueryInterface IID_IUnknown);
    if (hr == S_OK) IUnknown_Release(pOtherUnknown);

    hr = IUnknown_QueryInterface(pProxy, &IID_IClientSecurity, (LPVOID*)&pOtherUnknown);
    todo_wine { ok_ole_success(hr, IUnknown_QueryInterface IID_IClientSecurity); }
    if (hr == S_OK) IUnknown_Release(pOtherUnknown);

    hr = IUnknown_QueryInterface(pProxy, &IID_IMultiQI, (LPVOID*)&pOtherUnknown);
    ok_ole_success(hr, IUnknown_QueryInterface IID_IMultiQI);
    if (hr == S_OK) IUnknown_Release(pOtherUnknown);

    hr = IUnknown_QueryInterface(pProxy, &IID_IMarshal, (LPVOID*)&pOtherUnknown);
    todo_wine { ok_ole_success(hr, IUnknown_QueryInterface IID_IMarshal); }
    if (hr == S_OK) IUnknown_Release(pOtherUnknown);

    /* IMarshal2 is also supported on NT-based systems, but is pretty much
     * useless as it has no more methods over IMarshal that it inherits from. */

    IUnknown_Release(pProxy);

    ok_no_locks();

    end_host_object(tid, thread);
}

static void test_stubbuffer(REFIID riid)
{
    HRESULT hr;
    IPSFactoryBuffer *psfb;
    IRpcStubBuffer *stub;
    ULONG refs;
    CLSID clsid;

    cLocks = 0;

    hr = CoGetPSClsid(riid, &clsid);
    ok_ole_success(hr, CoGetPSClsid);

    hr = CoGetClassObject(&clsid, CLSCTX_INPROC_SERVER, NULL, &IID_IPSFactoryBuffer, (LPVOID*)&psfb);
    ok_ole_success(hr, CoGetClassObject);

    hr = IPSFactoryBuffer_CreateStub(psfb, riid, (IUnknown*)&Test_ClassFactory, &stub);
    ok_ole_success(hr, IPSFactoryBuffer_CreateStub);

    refs = IPSFactoryBuffer_Release(psfb);
#if 0 /* not reliable on native. maybe it leaks references */
    ok(refs == 0, "Ref-count leak of %ld on IPSFactoryBuffer\n", refs);
#endif

    ok_more_than_one_lock();

    IRpcStubBuffer_Disconnect(stub);

    ok_no_locks();

    refs = IRpcStubBuffer_Release(stub);
    ok(refs == 0, "Ref-count leak of %ld on IRpcProxyBuffer\n", refs);
}


/* doesn't pass with Win9x COM DLLs (even though Essential COM says it should) */
#if 0

static HANDLE heventShutdown;

static void LockModuleOOP()
{
    InterlockedIncrement(&cLocks); /* for test purposes only */
    CoAddRefServerProcess();
}

static void UnlockModuleOOP()
{
    InterlockedDecrement(&cLocks); /* for test purposes only */
    if (!CoReleaseServerProcess())
        SetEvent(heventShutdown);
}


static HRESULT WINAPI TestOOP_IClassFactory_QueryInterface(
    LPCLASSFACTORY iface,
    REFIID riid,
    LPVOID *ppvObj)
{
    if (ppvObj == NULL) return E_POINTER;

    if (IsEqualGUID(riid, &IID_IUnknown) ||
        IsEqualGUID(riid, &IID_IClassFactory))
    {
        *ppvObj = (LPVOID)iface;
        IClassFactory_AddRef(iface);
        return S_OK;
    }

    return E_NOINTERFACE;
}

static ULONG WINAPI TestOOP_IClassFactory_AddRef(LPCLASSFACTORY iface)
{
    return 2; /* non-heap-based object */
}

static ULONG WINAPI TestOOP_IClassFactory_Release(LPCLASSFACTORY iface)
{
    return 1; /* non-heap-based object */
}

static HRESULT WINAPI TestOOP_IClassFactory_CreateInstance(
    LPCLASSFACTORY iface,
    LPUNKNOWN pUnkOuter,
    REFIID riid,
    LPVOID *ppvObj)
{
    return CLASS_E_CLASSNOTAVAILABLE;
}

static HRESULT WINAPI TestOOP_IClassFactory_LockServer(
    LPCLASSFACTORY iface,
    BOOL fLock)
{
    if (fLock)
        LockModuleOOP();
    else
        UnlockModuleOOP();
    return S_OK;
}

static IClassFactoryVtbl TestClassFactoryOOP_Vtbl =
{
    TestOOP_IClassFactory_QueryInterface,
    TestOOP_IClassFactory_AddRef,
    TestOOP_IClassFactory_Release,
    TestOOP_IClassFactory_CreateInstance,
    TestOOP_IClassFactory_LockServer
};

static IClassFactory TestOOP_ClassFactory = { &TestClassFactoryOOP_Vtbl };

/* tests functions commonly used by out of process COM servers */
static void test_out_of_process_com()
{
    static const CLSID CLSID_WineOOPTest = {
        0x5201163f,
        0x8164,
        0x4fd0,
        {0xa1, 0xa2, 0x5d, 0x5a, 0x36, 0x54, 0xd3, 0xbd}
    }; /* 5201163f-8164-4fd0-a1a2-5d5a3654d3bd */
    DWORD cookie;
    HRESULT hr;
    IClassFactory * cf;
    DWORD ret;

    heventShutdown = CreateEvent(NULL, TRUE, FALSE, NULL);

    cLocks = 0;

    /* Start the object suspended */
    hr = CoRegisterClassObject(&CLSID_WineOOPTest, (IUnknown *)&TestOOP_ClassFactory,
        CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE | REGCLS_SUSPENDED, &cookie);
    ok_ole_success(hr, CoRegisterClassObject);

    /* ... and CoGetClassObject does not find it and fails when it looks for the
     * class in the registry */
    hr = CoGetClassObject(&CLSID_WineOOPTest, CLSCTX_INPROC_SERVER,
        NULL, &IID_IClassFactory, (LPVOID*)&cf);
    todo_wine {
    ok(hr == REGDB_E_CLASSNOTREG,
        "CoGetClassObject should have returned REGDB_E_CLASSNOTREG instead of 0x%08lx\n", hr);
    }

    /* Resume the object suspended above ... */
    hr = CoResumeClassObjects();
    ok_ole_success(hr, CoResumeClassObjects);

    /* ... and now it should succeed */
    hr = CoGetClassObject(&CLSID_WineOOPTest, CLSCTX_INPROC_SERVER,
        NULL, &IID_IClassFactory, (LPVOID*)&cf);
    ok_ole_success(hr, CoGetClassObject);

    /* Now check the locking is working */
    /* NOTE: we are accessing the class directly, not through a proxy */

    ok_no_locks();

    hr = IClassFactory_LockServer(cf, TRUE);
    trace("IClassFactory_LockServer returned 0x%08lx\n", hr);

    ok_more_than_one_lock();
    
    IClassFactory_LockServer(cf, FALSE);

    ok_no_locks();

    IClassFactory_Release(cf);

    /* wait for shutdown signal */
    ret = WaitForSingleObject(heventShutdown, 5000);
    todo_wine { ok(ret != WAIT_TIMEOUT, "Server didn't shut down or machine is under very heavy load\n"); }

    /* try to connect again after SCM has suspended registered class objects */
    hr = CoGetClassObject(&CLSID_WineOOPTest, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER, NULL,
        &IID_IClassFactory, (LPVOID*)&cf);
    todo_wine {
    ok(hr == CO_E_SERVER_STOPPING,
        "CoGetClassObject should have returned CO_E_SERVER_STOPPING instead of 0x%08lx\n", hr);
    }

    hr = CoRevokeClassObject(cookie);
    ok_ole_success(hr, CoRevokeClassObject);

    CloseHandle(heventShutdown);
}
#endif

START_TEST(marshal)
{
    HMODULE hOle32 = GetModuleHandle("ole32");
    if (!(pCoInitializeEx = (void*)GetProcAddress(hOle32, "CoInitializeEx"))) goto no_test;

    pCoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    /* FIXME: test CoCreateInstanceEx */

    /* helper function tests */
    test_CoGetPSClsid();

    /* lifecycle management and marshaling tests */
    test_no_marshaler();
    test_normal_marshal_and_release();
    test_normal_marshal_and_unmarshal();
    test_marshal_and_unmarshal_invalid();
    test_interthread_marshal_and_unmarshal();
    test_marshal_stub_apartment_shutdown();
    test_marshal_proxy_apartment_shutdown();
    test_marshal_proxy_mta_apartment_shutdown();
    test_no_couninitialize();
    test_tableweak_marshal_and_unmarshal_twice();
    test_tableweak_marshal_releasedata1();
    test_tableweak_marshal_releasedata2();
    test_tablestrong_marshal_and_unmarshal_twice();
    test_lock_object_external();
    test_disconnect_stub();
    test_normal_marshal_and_unmarshal_twice();
    test_hresult_marshaling();
    test_proxy_used_in_wrong_thread();
    test_message_filter();
    test_bad_marshal_stream();
    test_proxy_interfaces();
    test_stubbuffer(&IID_IClassFactory);
    /* FIXME: test GIT */
    /* FIXME: test COM re-entrancy */

/*    test_out_of_process_com(); */
    CoUninitialize();
    return;

no_test:
    trace("You need DCOM95 installed to run this test\n");
    return;
}
