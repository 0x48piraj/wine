/*
 * NT basis DLL
 * 
 * This file contains the Rtl* API functions. These should be implementable.
 * 
 * Copyright 1996-1998 Marcus Meissner
 *		  1999 Alex Korobka
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "heap.h"
#include "debugtools.h"
#include "windef.h"
#include "winerror.h"
#include "stackframe.h"

#include "ntddk.h"
#include "winreg.h"

DEFAULT_DEBUG_CHANNEL(ntdll);


static RTL_CRITICAL_SECTION peb_lock = CRITICAL_SECTION_INIT;

/*
 *	resource functions
 */

/***********************************************************************
 *           RtlInitializeResource	(NTDLL.409)
 *
 * xxxResource() functions implement multiple-reader-single-writer lock.
 * The code is based on information published in WDJ January 1999 issue.
 */
void WINAPI RtlInitializeResource(LPRTL_RWLOCK rwl)
{
    if( rwl )
    {
	rwl->iNumberActive = 0;
	rwl->uExclusiveWaiters = 0;
	rwl->uSharedWaiters = 0;
	rwl->hOwningThreadId = 0;
	rwl->dwTimeoutBoost = 0; /* no info on this one, default value is 0 */
	RtlInitializeCriticalSection( &rwl->rtlCS );
        NtCreateSemaphore( &rwl->hExclusiveReleaseSemaphore, 0, NULL, 0, 65535 );
        NtCreateSemaphore( &rwl->hSharedReleaseSemaphore, 0, NULL, 0, 65535 );
    }
}


/***********************************************************************
 *           RtlDeleteResource		(NTDLL.330)
 */
void WINAPI RtlDeleteResource(LPRTL_RWLOCK rwl)
{
    if( rwl )
    {
	RtlEnterCriticalSection( &rwl->rtlCS );
	if( rwl->iNumberActive || rwl->uExclusiveWaiters || rwl->uSharedWaiters )
	    MESSAGE("Deleting active MRSW lock (%p), expect failure\n", rwl );
	rwl->hOwningThreadId = 0;
	rwl->uExclusiveWaiters = rwl->uSharedWaiters = 0;
	rwl->iNumberActive = 0;
	NtClose( rwl->hExclusiveReleaseSemaphore );
	NtClose( rwl->hSharedReleaseSemaphore );
	RtlLeaveCriticalSection( &rwl->rtlCS );
	RtlDeleteCriticalSection( &rwl->rtlCS );
    }
}


/***********************************************************************
 *          RtlAcquireResourceExclusive	(NTDLL.256)
 */
BYTE WINAPI RtlAcquireResourceExclusive(LPRTL_RWLOCK rwl, BYTE fWait)
{
    BYTE retVal = 0;
    if( !rwl ) return 0;

start:
    RtlEnterCriticalSection( &rwl->rtlCS );
    if( rwl->iNumberActive == 0 ) /* lock is free */
    {
	rwl->iNumberActive = -1;
	retVal = 1;
    }
    else if( rwl->iNumberActive < 0 ) /* exclusive lock in progress */
    {
	 if( rwl->hOwningThreadId == GetCurrentThreadId() )
	 {
	     retVal = 1;
	     rwl->iNumberActive--;
	     goto done;
	 }
wait:
	 if( fWait )
	 {
	     rwl->uExclusiveWaiters++;

	     RtlLeaveCriticalSection( &rwl->rtlCS );
	     if( WaitForSingleObject( rwl->hExclusiveReleaseSemaphore, INFINITE ) == WAIT_FAILED )
		 goto done;
	     goto start; /* restart the acquisition to avoid deadlocks */
	 }
    }
    else  /* one or more shared locks are in progress */
	 if( fWait )
	     goto wait;
	 
    if( retVal == 1 )
	rwl->hOwningThreadId = GetCurrentThreadId();
done:
    RtlLeaveCriticalSection( &rwl->rtlCS );
    return retVal;
}

/***********************************************************************
 *          RtlAcquireResourceShared	(NTDLL.257)
 */
BYTE WINAPI RtlAcquireResourceShared(LPRTL_RWLOCK rwl, BYTE fWait)
{
    DWORD dwWait = WAIT_FAILED;
    BYTE retVal = 0;
    if( !rwl ) return 0;

start:
    RtlEnterCriticalSection( &rwl->rtlCS );
    if( rwl->iNumberActive < 0 )
    {
	if( rwl->hOwningThreadId == GetCurrentThreadId() )
	{
	    rwl->iNumberActive--;
	    retVal = 1;
	    goto done;
	}
	
	if( fWait )
	{
	    rwl->uSharedWaiters++;
	    RtlLeaveCriticalSection( &rwl->rtlCS );
	    if( (dwWait = WaitForSingleObject( rwl->hSharedReleaseSemaphore, INFINITE )) == WAIT_FAILED )
		goto done;
	    goto start;
	}
    }
    else 
    {
	if( dwWait != WAIT_OBJECT_0 ) /* otherwise RtlReleaseResource() has already done it */
	    rwl->iNumberActive++;
	retVal = 1;
    }
done:
    RtlLeaveCriticalSection( &rwl->rtlCS );
    return retVal;
}


/***********************************************************************
 *           RtlReleaseResource		(NTDLL.471)
 */
void WINAPI RtlReleaseResource(LPRTL_RWLOCK rwl)
{
    RtlEnterCriticalSection( &rwl->rtlCS );

    if( rwl->iNumberActive > 0 ) /* have one or more readers */
    {
	if( --rwl->iNumberActive == 0 )
	{
	    if( rwl->uExclusiveWaiters )
	    {
wake_exclusive:
		rwl->uExclusiveWaiters--;
		NtReleaseSemaphore( rwl->hExclusiveReleaseSemaphore, 1, NULL );
	    }
	}
    }
    else 
    if( rwl->iNumberActive < 0 ) /* have a writer, possibly recursive */
    {
	if( ++rwl->iNumberActive == 0 )
	{
	    rwl->hOwningThreadId = 0;
	    if( rwl->uExclusiveWaiters )
		goto wake_exclusive;
	    else
		if( rwl->uSharedWaiters )
		{
		    UINT n = rwl->uSharedWaiters;
		    rwl->iNumberActive = rwl->uSharedWaiters; /* prevent new writers from joining until
							       * all queued readers have done their thing */
		    rwl->uSharedWaiters = 0;
		    NtReleaseSemaphore( rwl->hSharedReleaseSemaphore, n, NULL );
		}
	}
    }
    RtlLeaveCriticalSection( &rwl->rtlCS );
}


/***********************************************************************
 *           RtlDumpResource		(NTDLL.340)
 */
void WINAPI RtlDumpResource(LPRTL_RWLOCK rwl)
{
    if( rwl )
    {
	MESSAGE("RtlDumpResource(%p):\n\tactive count = %i\n\twaiting readers = %i\n\twaiting writers = %i\n",  
		rwl, rwl->iNumberActive, rwl->uSharedWaiters, rwl->uExclusiveWaiters );
	if( rwl->iNumberActive )
	    MESSAGE("\towner thread = %08x\n", rwl->hOwningThreadId );
    }
}

/*
 *	heap functions
 */

/******************************************************************************
 *  RtlCreateHeap		[NTDLL] 
 */
HANDLE WINAPI RtlCreateHeap(
	ULONG Flags,
	PVOID BaseAddress,
	ULONG SizeToReserve,
	ULONG SizeToCommit,
	PVOID Unknown,
	PRTL_HEAP_DEFINITION Definition)
{
	FIXME("(0x%08lx, %p, 0x%08lx, 0x%08lx, %p, %p) semi-stub\n",
	Flags, BaseAddress, SizeToReserve, SizeToCommit, Unknown, Definition);
	
	return HeapCreate ( Flags, SizeToCommit, SizeToReserve);

}	
/******************************************************************************
 *  RtlAllocateHeap		[NTDLL] 
 */
PVOID WINAPI RtlAllocateHeap(
	HANDLE Heap,
	ULONG Flags,
	ULONG Size)
{
	TRACE("(0x%08x, 0x%08lx, 0x%08lx) semi stub\n",
	Heap, Flags, Size);
	return HeapAlloc(Heap, Flags, Size);
}

/******************************************************************************
 *  RtlFreeHeap		[NTDLL] 
 */
BOOLEAN WINAPI RtlFreeHeap(
	HANDLE Heap,
	ULONG Flags,
	PVOID Address)
{
	TRACE("(0x%08x, 0x%08lx, %p) semi stub\n",
	Heap, Flags, Address);
	return HeapFree(Heap, Flags, Address);
}
	
/******************************************************************************
 *  RtlDestroyHeap		[NTDLL] 
 *
 * FIXME: prototype guessed
 */
BOOLEAN WINAPI RtlDestroyHeap(
	HANDLE Heap)
{
	TRACE("(0x%08x) semi stub\n", Heap);
	return HeapDestroy(Heap);
}
	
/*
 *	misc functions
 */

/******************************************************************************
 *	DbgPrint	[NTDLL] 
 */
void WINAPIV DbgPrint(LPCSTR fmt, ...)
{
       char buf[512];
       va_list args;

       va_start(args, fmt);
       vsprintf(buf,fmt, args);
       va_end(args); 

	MESSAGE("DbgPrint says: %s",buf);
	/* hmm, raise exception? */
}

/******************************************************************************
 *  RtlAcquirePebLock		[NTDLL] 
 */
VOID WINAPI RtlAcquirePebLock(void)
{
    RtlEnterCriticalSection( &peb_lock );
}

/******************************************************************************
 *  RtlReleasePebLock		[NTDLL] 
 */
VOID WINAPI RtlReleasePebLock(void)
{
    RtlLeaveCriticalSection( &peb_lock );
}

/******************************************************************************
 *  RtlIntegerToChar	[NTDLL] 
 */
DWORD WINAPI RtlIntegerToChar(DWORD x1,DWORD x2,DWORD x3,DWORD x4) {
	FIXME("(0x%08lx,0x%08lx,0x%08lx,0x%08lx),stub!\n",x1,x2,x3,x4);
	return 0;
}
/******************************************************************************
 *  RtlSetEnvironmentVariable		[NTDLL] 
 */
DWORD WINAPI RtlSetEnvironmentVariable(DWORD x1,PUNICODE_STRING key,PUNICODE_STRING val) {
	FIXME("(0x%08lx,%s,%s),stub!\n",x1,debugstr_w(key->Buffer),debugstr_w(val->Buffer));
	return 0;
}

/******************************************************************************
 *  RtlNewSecurityObject		[NTDLL] 
 */
DWORD WINAPI RtlNewSecurityObject(DWORD x1,DWORD x2,DWORD x3,DWORD x4,DWORD x5,DWORD x6) {
	FIXME("(0x%08lx,0x%08lx,0x%08lx,0x%08lx,0x%08lx,0x%08lx),stub!\n",x1,x2,x3,x4,x5,x6);
	return 0;
}

/******************************************************************************
 *  RtlDeleteSecurityObject		[NTDLL] 
 */
DWORD WINAPI RtlDeleteSecurityObject(DWORD x1) {
	FIXME("(0x%08lx),stub!\n",x1);
	return 0;
}

/**************************************************************************
 *                 RtlNormalizeProcessParams		[NTDLL.441]
 */
LPVOID WINAPI RtlNormalizeProcessParams(LPVOID x)
{
    FIXME("(%p), stub\n",x);
    return x;
}

/**************************************************************************
 *                 RtlGetNtProductType			[NTDLL.390]
 */
BOOLEAN WINAPI RtlGetNtProductType(LPDWORD type)
{
    FIXME("(%p): stub\n", type);
    *type=3; /* dunno. 1 for client, 3 for server? */
    return 1;
}

/**************************************************************************
 *                 NTDLL_chkstk				[NTDLL.862]
 *                 NTDLL_alloca_probe				[NTDLL.861]
 * Glorified "enter xxxx".
 */
void WINAPI NTDLL_chkstk( CONTEXT86 *context )
{
    context->Esp -= context->Eax;
}
void WINAPI NTDLL_alloca_probe( CONTEXT86 *context )
{
    context->Esp -= context->Eax;
}

/**************************************************************************
 *                 RtlDosPathNameToNtPathName_U		[NTDLL.338]
 *
 * FIXME: convert to UNC or whatever is expected here
 */
BOOLEAN  WINAPI RtlDosPathNameToNtPathName_U(
	LPWSTR from,PUNICODE_STRING us,DWORD x2,DWORD x3)
{
    FIXME("(%s,%p,%08lx,%08lx)\n",debugstr_w(from),us,x2,x3);
    if (us) RtlCreateUnicodeString( us, from );
    return TRUE;
}


/***********************************************************************
 *           RtlImageNtHeader   (NTDLL)
 */
PIMAGE_NT_HEADERS WINAPI RtlImageNtHeader(HMODULE hModule)
{
    IMAGE_NT_HEADERS *ret = NULL;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)hModule;

    if (dos->e_magic == IMAGE_DOS_SIGNATURE)
    {
        ret = (IMAGE_NT_HEADERS *)((char *)dos + dos->e_lfanew);
        if (ret->Signature != IMAGE_NT_SIGNATURE) ret = NULL;
    }
    return ret;
}


/******************************************************************************
 *  RtlCreateEnvironment		[NTDLL] 
 */
DWORD WINAPI RtlCreateEnvironment(DWORD x1,DWORD x2) {
	FIXME("(0x%08lx,0x%08lx),stub!\n",x1,x2);
	return 0;
}


/******************************************************************************
 *  RtlDestroyEnvironment		[NTDLL] 
 */
DWORD WINAPI RtlDestroyEnvironment(DWORD x) {
	FIXME("(0x%08lx),stub!\n",x);
	return 0;
}

/******************************************************************************
 *  RtlQueryEnvironmentVariable_U		[NTDLL] 
 */
DWORD WINAPI RtlQueryEnvironmentVariable_U(DWORD x1,PUNICODE_STRING key,PUNICODE_STRING val) {
	FIXME("(0x%08lx,%s,%p),stub!\n",x1,debugstr_w(key->Buffer),val);
	return 0;
}
/******************************************************************************
 *  RtlInitializeGenericTable		[NTDLL] 
 */
DWORD WINAPI RtlInitializeGenericTable(void)
{
	FIXME("\n");
	return 0;
}

/******************************************************************************
 *  RtlInitializeBitMap			[NTDLL] 
 * 
 */
NTSTATUS WINAPI RtlInitializeBitMap(DWORD x1,DWORD x2,DWORD x3)
{
	FIXME("(0x%08lx,0x%08lx,0x%08lx),stub\n",x1,x2,x3);
	return 0;
}

/******************************************************************************
 *  RtlSetBits				[NTDLL] 
 * 
 */
NTSTATUS WINAPI RtlSetBits(DWORD x1,DWORD x2,DWORD x3)
{
	FIXME("(0x%08lx,0x%08lx,0x%08lx),stub\n",x1,x2,x3);
	return 0;
}

/******************************************************************************
 *  RtlFindClearBits			[NTDLL] 
 * 
 */
NTSTATUS WINAPI RtlFindClearBits(DWORD x1,DWORD x2,DWORD x3)
{
	FIXME("(0x%08lx,0x%08lx,0x%08lx),stub\n",x1,x2,x3);
	return 0;
}

/******************************************************************************
 *  RtlClearBits			[NTDLL] 
 * 
 */
NTSTATUS WINAPI RtlClearBits(DWORD x1,DWORD x2,DWORD x3)
{
	FIXME("(0x%08lx,0x%08lx,0x%08lx),stub\n",x1,x2,x3);
	return 0;
}

/******************************************************************************
 *  RtlCopyMemory   [NTDLL] 
 * 
 */
#undef RtlCopyMemory
VOID WINAPI RtlCopyMemory( VOID *Destination, CONST VOID *Source, SIZE_T Length )
{
    memcpy(Destination, Source, Length);
}	

/******************************************************************************
 *  RtlMoveMemory   [NTDLL] 
 */
#undef RtlMoveMemory
VOID WINAPI RtlMoveMemory( VOID *Destination, CONST VOID *Source, SIZE_T Length )
{
    memmove(Destination, Source, Length);
}

/******************************************************************************
 *  RtlFillMemory   [NTDLL] 
 */
#undef RtlFillMemory
VOID WINAPI RtlFillMemory( VOID *Destination, SIZE_T Length, BYTE Fill )
{
    memset(Destination, Fill, Length);
}

/******************************************************************************
 *  RtlZeroMemory   [NTDLL] 
 */
#undef RtlZeroMemory
VOID WINAPI RtlZeroMemory( VOID *Destination, SIZE_T Length )
{
    memset(Destination, 0, Length);
}

/******************************************************************************
 *  RtlCompareMemory   [NTDLL] 
 */
SIZE_T WINAPI RtlCompareMemory( const VOID *Source1, const VOID *Source2, SIZE_T Length)
{
    int i;
    for(i=0; (i<Length) && (((LPBYTE)Source1)[i]==((LPBYTE)Source2)[i]); i++);
    return i;
}

/******************************************************************************
 *  RtlAssert                           [NTDLL]
 *
 * Not implemented in non-debug versions.
 */
void WINAPI RtlAssert(LPVOID x1,LPVOID x2,DWORD x3, DWORD x4)
{
	FIXME("(%p,%p,0x%08lx,0x%08lx),stub\n",x1,x2,x3,x4);
}
