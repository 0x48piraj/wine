/*
 * Copyright (C) 2000 Francois Gouget
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

#ifndef __WINE_RPCNDR_H
#define __WINE_RPCNDR_H

#ifndef __RPCNDR_H_VERSION__
/* FIXME: I'm not sure what version though */
#define __RPCNDR_H_VERSION__
#endif // __RPCNDR_H_VERSION__

typedef unsigned char byte;
typedef __int64 hyper;
typedef __uint64 MIDL_uhyper;
/* 'boolean' tend to conflict, let's call it _wine_boolean */
typedef unsigned char _wine_boolean;
/* typedef _wine_boolean boolean; */

typedef struct
{
  void *pad[2];
  void *userContext;
} *NDR_SCONTEXT;

#define NDRSContextValue(hContext) (&(hContext)->userContext)
#define cbNDRContext 20

typedef void (__RPC_USER *NDR_RUNDOWN)(void *context);
typedef void (__RPC_USER *NDR_NOTIFY_ROUTINE)(void);
typedef void (__RPC_USER *NDR_NOTIFY2_ROUTINE)(_wine_boolean flag);

#define DECLSPEC_UUID(x)
#define MIDL_INTERFACE(x)   struct

struct _MIDL_STUB_MESSAGE;
struct _MIDL_STUB_DESC;
struct _FULL_PTR_XLAT_TABLES;

typedef void (__RPC_USER *EXPR_EVAL)(struct _MIDL_STUB_MESSAGE *);
typedef const unsigned char *PFORMAT_STRING;

typedef struct
{
  long Dimension;
  unsigned long *BufferConformanceMark;
  unsigned long *BufferVarianceMark;
  unsigned long *MaxCountArray;
  unsigned long *OffsetArray;
  unsigned long *ActualCountArray;
} ARRAY_INFO, *PARRAY_INFO;

typedef struct _NDR_PIPE_DESC *PNDR_PIPE_DESC;
typedef struct _NDR_PIPE_MESSAGE *PNDR_PIPE_MESSAGE;
typedef struct _NDR_ASYNC_MESSAGE *PNDR_ASYNC_MESSAGE;
typedef struct _NDR_CORRELATION_INFO *PNDR_CORRELATION_INFO;

#include "pshpack4.h"
typedef struct _MIDL_STUB_MESSAGE
{
  PRPC_MESSAGE RpcMsg;
  unsigned char *Buffer;
  unsigned char *BufferStart;
  unsigned char *BufferEnd;
  unsigned char *BufferMark;
  unsigned long BufferLength;
  unsigned long MemorySize;
  unsigned char *Memory;
  int IsClient;
  int ReuseBuffer;
  unsigned char *AllocAllNodesMemory;
  unsigned char *AllocAllNodesMemoryEnd;
  int IgnoreEmbeddedPointers;
  unsigned char *PointerBufferMark;
  unsigned char fBufferValid;
  unsigned char uFlags;
  ULONG_PTR MaxCount;
  unsigned long Offset;
  unsigned long ActualCount;
  void * (__RPC_API *pfnAllocate)(size_t);
  void (__RPC_API *pfnFree)(void *);
  unsigned char *StackTop;
  unsigned char *pPresentedType;
  unsigned char *pTransmitType;
  handle_t SavedHandle;
  const struct _MIDL_STUB_DESC *StubDesc;
  struct _FULL_PTR_XLAT_TABLES *FullPtrXlatTables;
  unsigned long FullPtrRefId;
  unsigned long ulUnused1;
  int fInDontFree:1;
  int fDontCallFreeInst:1;
  int fInOnlyParam:1;
  int fHasReturn:1;
  int fHasExtensions:1;
  int fHasNewCorrDesc:1;
  int fUnused:10;
  unsigned long dwDestContext;
  void *pvDestContext;
  NDR_SCONTEXT *SavedContextHandles;
  long ParamNumber;
  struct IRpcChannelBuffer *pRpcChannelBuffer;
  PARRAY_INFO pArrayInfo;
  unsigned long *SizePtrCountArray;
  unsigned long *SizePtrOffsetArray;
  unsigned long *SizePtrLengthArray;
  void *pArgQueue;
  unsigned long dwStubPhase;
  PNDR_PIPE_DESC pPipeDesc;
  PNDR_ASYNC_MESSAGE pAsyncMsg;
  PNDR_CORRELATION_INFO pCorrInfo;
  unsigned char *pCorrMemory;
  void *pMemoryList;
  ULONG_PTR w2kReserved[5];
} MIDL_STUB_MESSAGE, *PMIDL_STUB_MESSAGE;
#include "poppack.h"

typedef struct _GENERIC_BINDING_ROUTINE_PAIR GENERIC_BINDING_ROUTINE_PAIR, *PGENERIC_BINDING_ROUTINE_PAIR;

typedef struct __GENERIC_BINDING_INFO GENERIC_BINDING_INFO, *PGENERIC_BINDING_INFO;

typedef struct _XMIT_ROUTINE_QUINTUPLE XMIT_ROUTINE_QUINTUPLE, *PXMIT_ROUTINE_QUINTUPLE;

typedef struct _USER_MARSHAL_ROUTINE_QUADRUPLE USER_MARSHAL_ROUTINE_QUADRUPLE;

typedef struct _MALLOC_FREE_STRUCT MALLOC_FREE_STRUCT;

typedef struct _COMM_FAULT_OFFSETS COMM_FAULT_OFFSETS;

typedef struct _MIDL_STUB_DESC
{
  void *RpcInterfaceInformation;
  void * (__RPC_API *pfnAllocate)(size_t);
  void (__RPC_API *pfnFree)(void *);
  union {
    handle_t *pAutoHandle;
    handle_t *pPrimitiveHandle;
    PGENERIC_BINDING_INFO pGenericBindingInfo;
  } IMPLICIT_HANDLE_INFO;
  const NDR_RUNDOWN *apfnNdrRundownRoutines;
  const GENERIC_BINDING_ROUTINE_PAIR *aGenericBindingRoutinePairs;
  const EXPR_EVAL *apfnExprEval;
  const XMIT_ROUTINE_QUINTUPLE *aXmitQuintuple;
  const unsigned char *pFormatTypes;
  int fCheckBounds;
  unsigned long Version;
  MALLOC_FREE_STRUCT *pMallocFreeStruct;
  long MIDLVersion;
  const COMM_FAULT_OFFSETS *CommFaultOffsets;
  const USER_MARSHAL_ROUTINE_QUADRUPLE *aUserMarshalQuadruple;
  const NDR_NOTIFY_ROUTINE *NotifyRoutineTable;
  ULONG_PTR mFlags;
  ULONG_PTR Reserved3;
  ULONG_PTR Reserved4;
  ULONG_PTR Reserved5;
} MIDL_STUB_DESC;
typedef const MIDL_STUB_DESC *PMIDL_STUB_DESC;

typedef struct _MIDL_FORMAT_STRING
{
  short Pad;
#if defined(__GNUC__)
  unsigned char Format[0];
#else
  unsigned char Format[1];
#endif
} MIDL_FORMAT_STRING;

typedef void (__RPC_API *STUB_THUNK)( PMIDL_STUB_MESSAGE );

typedef long (__RPC_API *SERVER_ROUTINE)();

typedef struct _MIDL_SERVER_INFO_
{
  PMIDL_STUB_DESC pStubDesc;
  const SERVER_ROUTINE *DispatchTable;
  PFORMAT_STRING ProcString;
  const unsigned short *FmtStringOffset;
  const STUB_THUNK *ThunkTable;
  PFORMAT_STRING LocalFormatTypes;
  PFORMAT_STRING LocalProcString;
  const unsigned short *LocalFmtStringOffset;
} MIDL_SERVER_INFO, *PMIDL_SERVER_INFO;

typedef enum {
  STUB_UNMARSHAL,
  STUB_CALL_SERVER,
  STUB_MARSHAL,
  STUB_CALL_SERVER_NO_HRESULT
} STUB_PHASE;

typedef enum {
  PROXY_CALCSIZE,
  PROXY_GETBUFFER,
  PROXY_MARSHAL,
  PROXY_SENDRECEIVE,
  PROXY_UNMARSHAL
} PROXY_PHASE;

struct IRpcStubBuffer;

RPCRTAPI void RPC_ENTRY
  NdrSimpleTypeMarshall( PMIDL_STUB_MESSAGE pStubMsg, unsigned char* pMemory, unsigned char FormatChar );
RPCRTAPI void RPC_ENTRY
  NdrSimpleTypeUnmarshall( PMIDL_STUB_MESSAGE pStubMsg, unsigned char* pMemory, unsigned char FormatChar );

/* while MS declares each prototype separately, I prefer to use macros for this kind of thing instead */
#define TYPE_MARSHAL(type) \
RPCRTAPI unsigned char* RPC_ENTRY \
  Ndr##type##Marshall( PMIDL_STUB_MESSAGE pStubMsg, unsigned char* pMemory, PFORMAT_STRING pFormat ); \
RPCRTAPI unsigned char* RPC_ENTRY \
  Ndr##type##Unmarshall( PMIDL_STUB_MESSAGE pStubMsg, unsigned char** ppMemory, PFORMAT_STRING pFormat, unsigned char fMustAlloc ); \
RPCRTAPI void RPC_ENTRY \
  Ndr##type##BufferSize( PMIDL_STUB_MESSAGE pStubMsg, unsigned char* pMemory, PFORMAT_STRING pFormat ); \
RPCRTAPI unsigned long RPC_ENTRY \
  Ndr##type##MemorySize( PMIDL_STUB_MESSAGE pStubMsg, PFORMAT_STRING pFormat ); \
RPCRTAPI void RPC_ENTRY \
  Ndr##type##Free( PMIDL_STUB_MESSAGE pStubMsg, unsigned char* pMemory, PFORMAT_STRING pFormat );

TYPE_MARSHAL(Pointer)
TYPE_MARSHAL(SimpleStruct)
TYPE_MARSHAL(ConformantStruct)
TYPE_MARSHAL(ConformantVaryingStruct)
TYPE_MARSHAL(ComplexStruct)
TYPE_MARSHAL(FixedArray)
TYPE_MARSHAL(ConformantArray)
TYPE_MARSHAL(ConformantVaryingArray)
TYPE_MARSHAL(VaryingArray)
TYPE_MARSHAL(ComplexArray)
TYPE_MARSHAL(EncapsulatedUnion)
TYPE_MARSHAL(NonEncapsulatedUnion)
TYPE_MARSHAL(ByteCountPointer)
TYPE_MARSHAL(XmitOrRepAs)
TYPE_MARSHAL(UserMarshal)
TYPE_MARSHAL(InterfacePointer)

#undef TYPE_MARSHAL

RPCRTAPI void RPC_ENTRY
  NdrConvert2( PMIDL_STUB_MESSAGE pStubMsg, PFORMAT_STRING pFormat, long NumberParams );
RPCRTAPI void RPC_ENTRY
  NdrConvert( PMIDL_STUB_MESSAGE pStubMsg, PFORMAT_STRING pFormat );

RPCRTAPI void* RPC_ENTRY
  NdrOleAllocate( size_t Size );
RPCRTAPI void RPC_ENTRY
  NdrOleFree( void* NodeToFree );

#endif /*__WINE_RPCNDR_H */
