/*
 * NT basis DLL
 * 
 * This file contains the Nt* API functions of NTDLL.DLL.
 * In the original ntdll.dll they all seem to just call int 0x2e (down to the HAL)
 *
 * Copyright 1996-1998 Marcus Meissner
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "debugtools.h"

#include "ntddk.h"
#include "ntdll_misc.h"
#include "server.h"

DEFAULT_DEBUG_CHANNEL(ntdll);

/*
 *	Timer object
 */
 
/**************************************************************************
 *		NtCreateTimer				[NTDLL.87]
 */
NTSTATUS WINAPI NtCreateTimer(
	OUT PHANDLE TimerHandle,
	IN ACCESS_MASK DesiredAccess,
	IN POBJECT_ATTRIBUTES ObjectAttributes OPTIONAL,
	IN TIMER_TYPE TimerType)
{
	FIXME("(%p,0x%08lx,%p,0x%08x) stub\n",
	TimerHandle,DesiredAccess,ObjectAttributes, TimerType);
	dump_ObjectAttributes(ObjectAttributes);
	return 0;
}
/**************************************************************************
 *		NtSetTimer				[NTDLL.221]
 */
NTSTATUS WINAPI NtSetTimer(
	IN HANDLE TimerHandle,
	IN PLARGE_INTEGER DueTime,
	IN PTIMERAPCROUTINE TimerApcRoutine,
	IN PVOID TimerContext,
	IN BOOLEAN WakeTimer,
	IN ULONG Period OPTIONAL,
	OUT PBOOLEAN PreviousState OPTIONAL)
{
	FIXME("(0x%08x,%p,%p,%p,%08x,0x%08lx,%p) stub\n",
	TimerHandle,DueTime,TimerApcRoutine,TimerContext,WakeTimer,Period,PreviousState);
	return 0;
}

/******************************************************************************
 * NtQueryTimerResolution [NTDLL.129]
 */
NTSTATUS WINAPI NtQueryTimerResolution(DWORD x1,DWORD x2,DWORD x3) 
{
	FIXME("(0x%08lx,0x%08lx,0x%08lx), stub!\n",x1,x2,x3);
	return 1;
}

/*
 *	Process object
 */

/******************************************************************************
 *  NtTerminateProcess			[NTDLL.] 
 *
 *  Native applications must kill themselves when done
 */
NTSTATUS WINAPI NtTerminateProcess( HANDLE handle, LONG exit_code )
{
    NTSTATUS ret;
    BOOL self;
    SERVER_START_REQ
    {
        struct terminate_process_request *req = server_alloc_req( sizeof(*req), 0 );
        req->handle    = handle;
        req->exit_code = exit_code;
        ret = server_call_noerr( REQ_TERMINATE_PROCESS );
        self = !ret && req->self;
    }
    SERVER_END_REQ;
    if (self) exit( exit_code );
    return ret;
}

/******************************************************************************
*  NtQueryInformationProcess		[NTDLL.] 
*
*/
NTSTATUS WINAPI NtQueryInformationProcess(
	IN HANDLE ProcessHandle,
	IN PROCESSINFOCLASS ProcessInformationClass,
	OUT PVOID ProcessInformation,
	IN ULONG ProcessInformationLength,
	OUT PULONG ReturnLength)
{
	FIXME("(0x%08x,0x%08x,%p,0x%08lx,%p),stub!\n",
		ProcessHandle,ProcessInformationClass,ProcessInformation,ProcessInformationLength,ReturnLength
	);
	/* "These are not the debuggers you are looking for." */
	if (ProcessInformationClass == ProcessDebugPort)
	    /* set it to 0 aka "no debugger" to satisfy copy protections */
	    memset(ProcessInformation,0,ProcessInformationLength);

	return 0;
}

/******************************************************************************
 * NtSetInformationProcess [NTDLL.207]
 */
NTSTATUS WINAPI NtSetInformationProcess(
	IN HANDLE ProcessHandle,
	IN PROCESSINFOCLASS ProcessInformationClass,
	IN PVOID ProcessInformation,
	IN ULONG ProcessInformationLength)
{
	FIXME("(0x%08x,0x%08x,%p,0x%08lx) stub\n",
	ProcessHandle,ProcessInformationClass,ProcessInformation,ProcessInformationLength);
	return 0;
}

/*
 *	Thread
 */

/******************************************************************************
 *  NtResumeThread	[NTDLL] 
 */
NTSTATUS WINAPI NtResumeThread(
	IN HANDLE ThreadHandle,
	IN PULONG SuspendCount) 
{
	FIXME("(0x%08x,%p),stub!\n",
	ThreadHandle,SuspendCount);
	return 0;
}


/******************************************************************************
 *  NtTerminateThread	[NTDLL] 
 */
NTSTATUS WINAPI NtTerminateThread( HANDLE handle, LONG exit_code )
{
    NTSTATUS ret;
    BOOL self, last;

    SERVER_START_REQ
    {
        struct terminate_thread_request *req = server_alloc_req( sizeof(*req), 0 );
        req->handle    = handle;
        req->exit_code = exit_code;
        ret = server_call_noerr( REQ_TERMINATE_THREAD );
        self = !ret && req->self;
        last = req->last;
    }
    SERVER_END_REQ;

    if (self)
    {
        if (last) exit( exit_code );
        else SYSDEPS_ExitThread( exit_code );
    }
    return ret;
}


/******************************************************************************
*  NtQueryInformationThread		[NTDLL.] 
*
*/
NTSTATUS WINAPI NtQueryInformationThread(
	IN HANDLE ThreadHandle,
	IN THREADINFOCLASS ThreadInformationClass,
	OUT PVOID ThreadInformation,
	IN ULONG ThreadInformationLength,
	OUT PULONG ReturnLength)
{
	FIXME("(0x%08x,0x%08x,%p,0x%08lx,%p),stub!\n",
		ThreadHandle, ThreadInformationClass, ThreadInformation,
		ThreadInformationLength, ReturnLength);
	return 0;
}

/******************************************************************************
 *  NtSetInformationThread		[NTDLL] 
 */
NTSTATUS WINAPI NtSetInformationThread(
	HANDLE ThreadHandle,
	THREADINFOCLASS ThreadInformationClass,
	PVOID ThreadInformation,
	ULONG ThreadInformationLength)
{
	FIXME("(0x%08x,0x%08x,%p,0x%08lx),stub!\n",
	ThreadHandle, ThreadInformationClass, ThreadInformation, ThreadInformationLength);
	return 0;
}

/*
 *	Token
 */

/******************************************************************************
 *  NtDuplicateToken		[NTDLL] 
 */
NTSTATUS WINAPI NtDuplicateToken(
        IN HANDLE ExistingToken,
        IN ACCESS_MASK DesiredAccess,
        IN POBJECT_ATTRIBUTES ObjectAttributes,
        IN SECURITY_IMPERSONATION_LEVEL ImpersonationLevel,
        IN TOKEN_TYPE TokenType,
        OUT PHANDLE NewToken)
{
	FIXME("(0x%08x,0x%08lx,%p,0x%08x,0x%08x,%p),stub!\n",
	ExistingToken, DesiredAccess, ObjectAttributes,
	ImpersonationLevel, TokenType, NewToken);
	dump_ObjectAttributes(ObjectAttributes);
	return 0;
}

/******************************************************************************
 *  NtOpenProcessToken		[NTDLL] 
 */
NTSTATUS WINAPI NtOpenProcessToken(
	HANDLE ProcessHandle,
	DWORD DesiredAccess, 
	HANDLE *TokenHandle) 
{
	FIXME("(0x%08x,0x%08lx,%p): stub\n",
	ProcessHandle,DesiredAccess, TokenHandle);
	*TokenHandle = 0xcafe;
	return 0;
}

/******************************************************************************
 *  NtOpenThreadToken		[NTDLL] 
 */
NTSTATUS WINAPI NtOpenThreadToken(
	HANDLE ThreadHandle,
	DWORD DesiredAccess, 
	BOOLEAN OpenAsSelf,
	HANDLE *TokenHandle) 
{
	FIXME("(0x%08x,0x%08lx,0x%08x,%p): stub\n",
	ThreadHandle,DesiredAccess, OpenAsSelf, TokenHandle);
	*TokenHandle = 0xcafe;
	return 0;
}

/******************************************************************************
 *  NtAdjustPrivilegesToken		[NTDLL] 
 *
 * FIXME: parameters unsafe
 */
NTSTATUS WINAPI NtAdjustPrivilegesToken(
	IN HANDLE TokenHandle,
	IN BOOLEAN DisableAllPrivileges,
	IN PTOKEN_PRIVILEGES NewState,
	IN DWORD BufferLength,
	OUT PTOKEN_PRIVILEGES PreviousState,
	OUT PDWORD ReturnLength)
{
	FIXME("(0x%08x,0x%08x,%p,0x%08lx,%p,%p),stub!\n",
	TokenHandle, DisableAllPrivileges, NewState, BufferLength, PreviousState, ReturnLength);
	return 0;
}

/******************************************************************************
*  NtQueryInformationToken		[NTDLL.156] 
*
* NOTES
*  Buffer for TokenUser:
*   0x00 TOKEN_USER the PSID field points to the SID
*   0x08 SID
*
*/
NTSTATUS WINAPI NtQueryInformationToken(
	HANDLE token,
	DWORD tokeninfoclass, 
	LPVOID tokeninfo,
	DWORD tokeninfolength,
	LPDWORD retlen ) 
{
    unsigned int len = 0;

    FIXME("(%08x,%ld,%p,%ld,%p): stub\n",
          token,tokeninfoclass,tokeninfo,tokeninfolength,retlen);

    switch (tokeninfoclass)
    {
    case TokenUser:
        len = sizeof(TOKEN_USER) + sizeof(SID);
        break;
    case TokenGroups:
        len = sizeof(TOKEN_GROUPS);
        break;
    case TokenPrivileges:
        len = sizeof(TOKEN_PRIVILEGES);
        break;
    case TokenOwner:
        len = sizeof(TOKEN_OWNER);
        break;
    case TokenPrimaryGroup:
        len = sizeof(TOKEN_PRIMARY_GROUP);
        break;
    case TokenDefaultDacl:
        len = sizeof(TOKEN_DEFAULT_DACL);
        break;
    case TokenSource:
        len = sizeof(TOKEN_SOURCE);
        break;
    case TokenType:
        len = sizeof (TOKEN_TYPE);
        break;
#if 0
    case TokenImpersonationLevel:
    case TokenStatistics:
#endif /* 0 */
    }

    /* FIXME: what if retlen == NULL ? */
    *retlen = len;

    if (tokeninfolength < len)
        return STATUS_BUFFER_TOO_SMALL;

    switch (tokeninfoclass)
    {
    case TokenUser:
        if( tokeninfo )
        {
            TOKEN_USER * tuser = tokeninfo;
            PSID sid = (PSID) (tuser + 1);
            SID_IDENTIFIER_AUTHORITY localSidAuthority = {SECURITY_NT_AUTHORITY};
            RtlInitializeSid(sid, &localSidAuthority, 1);
            *(RtlSubAuthoritySid(sid, 0)) = SECURITY_INTERACTIVE_RID;
            tuser->User.Sid = sid;
        }
        break;
    case TokenGroups:
        if (tokeninfo)
        {
            TOKEN_GROUPS *tgroups = tokeninfo;
            SID_IDENTIFIER_AUTHORITY sid = {SECURITY_NT_AUTHORITY};

            /* we need to show admin privileges ! */
            tgroups->GroupCount = 1;
            RtlAllocateAndInitializeSid( &sid,
                                         2,
                                         SECURITY_BUILTIN_DOMAIN_RID,
                                         DOMAIN_ALIAS_RID_ADMINS,
                                         0, 0, 0, 0, 0, 0,
                                         &(tgroups->Groups->Sid));
        }
        break;
    case TokenPrivileges:
        if (tokeninfo)
        {
            TOKEN_PRIVILEGES *tpriv = tokeninfo;
            tpriv->PrivilegeCount = 1;
        }
        break;
    }
    return 0;
}

/*
 *	Section
 */
 
/******************************************************************************
 *  NtCreateSection	[NTDLL] 
 */
NTSTATUS WINAPI NtCreateSection(
	OUT PHANDLE SectionHandle,
	IN ACCESS_MASK DesiredAccess,
	IN POBJECT_ATTRIBUTES ObjectAttributes OPTIONAL,
	IN PLARGE_INTEGER MaximumSize OPTIONAL,
	IN ULONG SectionPageProtection OPTIONAL,
	IN ULONG AllocationAttributes,
	IN HANDLE FileHandle OPTIONAL)
{
	FIXME("(%p,0x%08lx,%p,%p,0x%08lx,0x%08lx,0x%08x) stub\n",
	SectionHandle,DesiredAccess, ObjectAttributes,
	MaximumSize,SectionPageProtection,AllocationAttributes,FileHandle);
	dump_ObjectAttributes(ObjectAttributes);
	return 0;
}

/******************************************************************************
 *  NtOpenSection	[NTDLL] 
 */
NTSTATUS WINAPI NtOpenSection(
	PHANDLE SectionHandle,
	ACCESS_MASK DesiredAccess,
	POBJECT_ATTRIBUTES ObjectAttributes)
{
	FIXME("(%p,0x%08lx,%p),stub!\n",
	SectionHandle,DesiredAccess,ObjectAttributes);
	dump_ObjectAttributes(ObjectAttributes);
	return 0;
}

/******************************************************************************
 *  NtQuerySection	[NTDLL] 
 */
NTSTATUS WINAPI NtQuerySection(
	IN HANDLE SectionHandle,
	IN PVOID SectionInformationClass,
	OUT PVOID SectionInformation,
	IN ULONG Length,
	OUT PULONG ResultLength)
{
	FIXME("(0x%08x,%p,%p,0x%08lx,%p) stub!\n",
	SectionHandle,SectionInformationClass,SectionInformation,Length,ResultLength);
	return 0;
}

/******************************************************************************
 * NtMapViewOfSection	[NTDLL] 
 * FUNCTION: Maps a view of a section into the virtual address space of a process
 *
 * ARGUMENTS:
 *  SectionHandle	Handle of the section
 *  ProcessHandle	Handle of the process
 *  BaseAddress		Desired base address (or NULL) on entry
 *			Actual base address of the view on exit
 *  ZeroBits		Number of high order address bits that must be zero
 *  CommitSize		Size in bytes of the initially committed section of the view
 *  SectionOffset	Offset in bytes from the beginning of the section to the beginning of the view
 *  ViewSize		Desired length of map (or zero to map all) on entry 
 			Actual length mapped on exit
 *  InheritDisposition	Specified how the view is to be shared with
 *			child processes
 *  AllocateType	Type of allocation for the pages
 *  Protect		Protection for the committed region of the view
 */
NTSTATUS WINAPI NtMapViewOfSection(
	HANDLE SectionHandle,
	HANDLE ProcessHandle,
	PVOID* BaseAddress,
	ULONG ZeroBits,
	ULONG CommitSize,
	PLARGE_INTEGER SectionOffset,
	PULONG ViewSize,
	SECTION_INHERIT InheritDisposition,
	ULONG AllocationType,
	ULONG Protect)
{
	FIXME("(0x%08x,0x%08x,%p,0x%08lx,0x%08lx,%p,%p,0x%08x,0x%08lx,0x%08lx) stub\n",
	SectionHandle,ProcessHandle,BaseAddress,ZeroBits,CommitSize,SectionOffset,
	ViewSize,InheritDisposition,AllocationType,Protect);
	return 0;
}

/*
 *	ports
 */

/******************************************************************************
 *  NtCreatePort		[NTDLL] 
 */
NTSTATUS WINAPI NtCreatePort(DWORD x1,DWORD x2,DWORD x3,DWORD x4,DWORD x5) 
{
	FIXME("(0x%08lx,0x%08lx,0x%08lx,0x%08lx,0x%08lx),stub!\n",x1,x2,x3,x4,x5);
	return 0;
}

/******************************************************************************
 *  NtConnectPort		[NTDLL] 
 */
NTSTATUS WINAPI NtConnectPort(DWORD x1,PUNICODE_STRING uni,DWORD x3,DWORD x4,DWORD x5,DWORD x6,DWORD x7,DWORD x8) 
{
	FIXME("(0x%08lx,%s,0x%08lx,0x%08lx,0x%08lx,0x%08lx,0x%08lx,0x%08lx),stub!\n",
	x1,debugstr_w(uni->Buffer),x3,x4,x5,x6,x7,x8);
	return 0;
}

/******************************************************************************
 *  NtListenPort		[NTDLL] 
 */
NTSTATUS WINAPI NtListenPort(DWORD x1,DWORD x2) 
{
	FIXME("(0x%08lx,0x%08lx),stub!\n",x1,x2);
	return 0;
}

/******************************************************************************
 *  NtAcceptConnectPort	[NTDLL] 
 */
NTSTATUS WINAPI NtAcceptConnectPort(DWORD x1,DWORD x2,DWORD x3,DWORD x4,DWORD x5,DWORD x6) 
{
	FIXME("(0x%08lx,0x%08lx,0x%08lx,0x%08lx,0x%08lx,0x%08lx),stub!\n",x1,x2,x3,x4,x5,x6);
	return 0;
}

/******************************************************************************
 *  NtCompleteConnectPort	[NTDLL] 
 */
NTSTATUS WINAPI NtCompleteConnectPort(DWORD x1) 
{
	FIXME("(0x%08lx),stub!\n",x1);
	return 0;
}

/******************************************************************************
 *  NtRegisterThreadTerminatePort	[NTDLL] 
 */
NTSTATUS WINAPI NtRegisterThreadTerminatePort(DWORD x1) 
{
	FIXME("(0x%08lx),stub!\n",x1);
	return 0;
}

/******************************************************************************
 *  NtRequestWaitReplyPort		[NTDLL] 
 */
NTSTATUS WINAPI NtRequestWaitReplyPort(DWORD x1,DWORD x2,DWORD x3) 
{
	FIXME("(0x%08lx,0x%08lx,0x%08lx),stub!\n",x1,x2,x3);
	return 0;
}

/******************************************************************************
 *  NtReplyWaitReceivePort	[NTDLL] 
 */
NTSTATUS WINAPI NtReplyWaitReceivePort(DWORD x1,DWORD x2,DWORD x3,DWORD x4) 
{
	FIXME("(0x%08lx,0x%08lx,0x%08lx,0x%08lx),stub!\n",x1,x2,x3,x4);
	return 0;
}

/*
 *	Misc
 */

 /******************************************************************************
 *  NtSetIntervalProfile	[NTDLL] 
 */
NTSTATUS WINAPI NtSetIntervalProfile(DWORD x1,DWORD x2) {
	FIXME("(0x%08lx,0x%08lx),stub!\n",x1,x2);
	return 0;
}

/******************************************************************************
 *  NtQueryPerformanceCounter	[NTDLL] 
 */
NTSTATUS WINAPI NtQueryPerformanceCounter(
	IN PLARGE_INTEGER Counter,
	IN PLARGE_INTEGER Frequency) 
{
	FIXME("(%p, 0%p) stub\n",
	Counter, Frequency);
	return 0;
}

/******************************************************************************
 *  NtCreateMailslotFile	[NTDLL] 
 */
NTSTATUS WINAPI NtCreateMailslotFile(DWORD x1,DWORD x2,DWORD x3,DWORD x4,DWORD x5,DWORD x6,DWORD x7,DWORD x8) 
{
	FIXME("(0x%08lx,0x%08lx,0x%08lx,0x%08lx,0x%08lx,0x%08lx,0x%08lx,0x%08lx),stub!\n",x1,x2,x3,x4,x5,x6,x7,x8);
	return 0;
}

/******************************************************************************
 * NtQuerySystemInformation [NTDLL.168]
 *
 * ARGUMENTS:
 *  SystemInformationClass	Index to a certain information structure
 *	SystemTimeAdjustmentInformation	SYSTEM_TIME_ADJUSTMENT
 *	SystemCacheInformation		SYSTEM_CACHE_INFORMATION
 *	SystemConfigurationInformation	CONFIGURATION_INFORMATION
 *	observed (class/len): 
 *		0x0/0x2c
 *		0x12/0x18
 *		0x2/0x138
 *		0x8/0x600
 *  SystemInformation	caller supplies storage for the information structure
 *  Length		size of the structure
 *  ResultLength	Data written
 */
NTSTATUS WINAPI NtQuerySystemInformation(
	IN SYSTEM_INFORMATION_CLASS SystemInformationClass,
	OUT PVOID SystemInformation,
	IN ULONG Length,
	OUT PULONG ResultLength)
{
	FIXME("(0x%08x,%p,0x%08lx,%p) stub\n",
	SystemInformationClass,SystemInformation,Length,ResultLength);
	ZeroMemory (SystemInformation, Length);
	return 0;
}


/******************************************************************************
 *  NtCreatePagingFile		[NTDLL] 
 */
NTSTATUS WINAPI NtCreatePagingFile(
	IN PUNICODE_STRING PageFileName,
	IN ULONG MiniumSize,
	IN ULONG MaxiumSize,
	OUT PULONG ActualSize)
{
	FIXME("(%p(%s),0x%08lx,0x%08lx,%p),stub!\n",
	PageFileName->Buffer, debugstr_w(PageFileName->Buffer),MiniumSize,MaxiumSize,ActualSize);
	return 0;
}

/******************************************************************************
 *  NtDisplayString				[NTDLL.95] 
 * 
 * writes a string to the nt-textmode screen eg. during startup
 */
NTSTATUS WINAPI NtDisplayString ( PUNICODE_STRING string )
{
    STRING stringA;
    NTSTATUS ret;

    if (!(ret = RtlUnicodeStringToAnsiString( &stringA, string, TRUE )))
    {
        MESSAGE( "%.*s", stringA.Length, stringA.Buffer );
        RtlFreeAnsiString( &stringA );
    }
    return ret;
}

/******************************************************************************
 *  NtPowerInformation				[NTDLL] 
 * 
 */
NTSTATUS WINAPI NtPowerInformation(DWORD x1,DWORD x2,DWORD x3,DWORD x4,DWORD x5)
{
	FIXME("(0x%08lx,0x%08lx,0x%08lx,0x%08lx,0x%08lx),stub\n",x1,x2,x3,x4,x5);
	return 0;
}

/******************************************************************************
 *  NtAllocateLocallyUniqueId
 *
 * FIXME: the server should do that
 */
NTSTATUS WINAPI NtAllocateLocallyUniqueId(PLUID Luid)
{
	static LUID luid;

	FIXME("%p (0x%08lx%08lx)\n", Luid, luid.DUMMYSTRUCTNAME.HighPart, luid.DUMMYSTRUCTNAME.LowPart);

	luid.QuadPart++;
	
	Luid->QuadPart = luid.QuadPart;
	return STATUS_SUCCESS;
}
