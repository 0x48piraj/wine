/*
 * Defines the COM interfaces and APIs related to structured data storage.
 *
 * Depends on 'obj_base.h'.
 *
 * Copyright (C) 1999 Paul Quinn
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

#ifndef __WINE_WINE_OBJ_CONNECTION_H
#define __WINE_WINE_OBJ_CONNECTION_H

#ifdef __cplusplus
extern "C" {
#endif /* defined(__cplusplus) */

/*****************************************************************************
 * Declare the structures
 */

typedef struct tagCONNECTDATA
{
				  IUnknown *pUnk;
					  DWORD dwCookie;
} CONNECTDATA, *LPCONNECTDATA;
 
/*****************************************************************************
 * Predeclare the interfaces
 */
DEFINE_GUID(IID_IConnectionPoint, 0xb196b286, 0xbab4, 0x101a, 0xb6, 0x9c, 0x00, 0xaa, 0x00, 0x34, 0x1d, 0x07);
typedef struct IConnectionPoint IConnectionPoint, *LPCONNECTIONPOINT;

DEFINE_GUID(IID_IConnectionPointContainer, 0xb196b284, 0xbab4, 0x101a, 0xb6, 0x9c, 0x00, 0xaa, 0x00, 0x34, 0x1d, 0x07);
typedef struct IConnectionPointContainer IConnectionPointContainer, *LPCONNECTIONPOINTCONTAINER;

DEFINE_GUID(IID_IEnumConnections, 0xb196b287, 0xbab4, 0x101a, 0xb6, 0x9c, 0x00, 0xaa, 0x00, 0x34, 0x1d, 0x07);
typedef struct IEnumConnections IEnumConnections, *LPENUMCONNECTIONS;

DEFINE_GUID(IID_IEnumConnectionPoints, 0xb196b285, 0xbab4, 0x101a, 0xb6, 0x9c, 0x00, 0xaa, 0x00, 0x34, 0x1d, 0x07);
typedef struct IEnumConnectionPoints IEnumConnectionPoints, *LPENUMCONNECTIONPOINTS;

/*****************************************************************************
 * IConnectionPoint interface
 */
#define ICOM_INTERFACE IConnectionPoint
#define IConnectionPoint_METHODS \
	ICOM_METHOD1(HRESULT,GetConnectionInterface, IID*,pIID) \
	ICOM_METHOD1(HRESULT,GetConnectionPointContainer, IConnectionPointContainer**,ppCPC) \
	ICOM_METHOD2(HRESULT,Advise, IUnknown*,pUnkSink, DWORD*,pdwCookie) \
	ICOM_METHOD1(HRESULT,Unadvise, DWORD,dwCookie) \
	ICOM_METHOD1(HRESULT,EnumConnections, IEnumConnections**,ppEnum)
#define IConnectionPoint_IMETHODS \
	IUnknown_IMETHODS \
	IConnectionPoint_METHODS
ICOM_DEFINE(IConnectionPoint,IUnknown)
#undef ICOM_INTERFACE

/*** IUnknown methods ***/
#define IConnectionPoint_QueryInterface(p,a,b)      ICOM_CALL2(QueryInterface,p,a,b)
#define IConnectionPoint_AddRef(p)                  ICOM_CALL (AddRef,p)
#define IConnectionPoint_Release(p)                 ICOM_CALL (Release,p)
/*** IConnectionPointContainer methods ***/
#define IConnectionPoint_GetConnectionInterface(p,a)      ICOM_CALL1(GetConnectionInterface,p,a)
#define IConnectionPoint_GetConnectionPointContainer(p,a) ICOM_CALL1(GetConnectionPointContainer,p,a)
#define IConnectionPoint_Advise(p,a,b)                    ICOM_CALL2(Advise,p,a,b)
#define IConnectionPoint_Unadvise(p,a)                    ICOM_CALL1(Unadvise,p,a)
#define IConnectionPoint_EnumConnections(p,a)             ICOM_CALL1(EnumConnections,p,a)


/*****************************************************************************
 * IConnectionPointContainer interface
 */
#define ICOM_INTERFACE IConnectionPointContainer
#define IConnectionPointContainer_METHODS \
	ICOM_METHOD1(HRESULT,EnumConnectionPoints, IEnumConnectionPoints**,ppEnum) \
	ICOM_METHOD2(HRESULT,FindConnectionPoint, REFIID,riid, IConnectionPoint**,ppCP)
#define IConnectionPointContainer_IMETHODS \
	IUnknown_IMETHODS \
	IConnectionPointContainer_METHODS
ICOM_DEFINE(IConnectionPointContainer,IUnknown)
#undef ICOM_INTERFACE

/*** IUnknown methods ***/
#define IConnectionPointContainer_QueryInterface(p,a,b)      ICOM_CALL2(QueryInterface,p,a,b)
#define IConnectionPointContainer_AddRef(p)                  ICOM_CALL (AddRef,p)
#define IConnectionPointContainer_Release(p)                 ICOM_CALL (Release,p)
/*** IConnectionPointContainer methods ***/
#define IConnectionPointContainer_EnumConnectionPoints(p,a)  ICOM_CALL1(EnumConnectionPoints,p,a)
#define IConnectionPointContainer_FindConnectionPoint(p,a,b) ICOM_CALL2(FindConnectionPoint,p,a,b)


/*****************************************************************************
 * IEnumConnections interface
 */
#define ICOM_INTERFACE IEnumConnections
#define IEnumConnections_METHODS \
	ICOM_METHOD3(HRESULT,Next, ULONG,cConnections, LPCONNECTDATA,rgcd, ULONG*,pcFectched) \
	ICOM_METHOD1(HRESULT,Skip, ULONG,cConnections) \
	ICOM_METHOD (HRESULT,Reset) \
	ICOM_METHOD1(HRESULT,Clone, IEnumConnections**,ppEnum)
#define IEnumConnections_IMETHODS \
	IUnknown_IMETHODS \
	IEnumConnections_METHODS
ICOM_DEFINE(IEnumConnections,IUnknown)
#undef ICOM_INTERFACE

/*** IUnknown methods ***/
#define IEnumConnections_QueryInterface(p,a,b)      ICOM_CALL2(QueryInterface,p,a,b)
#define IEnumConnections_AddRef(p)                  ICOM_CALL (AddRef,p)
#define IEnumConnections_Release(p)                 ICOM_CALL (Release,p)
/*** IConnectionPointContainer methods ***/
#define IEnumConnections_Next(p,a,b,c)              ICOM_CALL3(Next,p,a,b,c)
#define IEnumConnections_Skip(p,a)                  ICOM_CALL1(Skip,p,a)
#define IEnumConnections_Reset(p)                   ICOM_CALL (Reset,p)
#define IEnumConnections_Clone(p,a)                 ICOM_CALL1(Clone,p,a)

/*****************************************************************************
 * IEnumConnectionPoints interface
 */
#define ICOM_INTERFACE IEnumConnectionPoints
#define IEnumConnectionPoints_METHODS \
	ICOM_METHOD3(HRESULT,Next, ULONG,cConnections, LPCONNECTIONPOINT*,ppCP, ULONG*,pcFectched) \
	ICOM_METHOD1(HRESULT,Skip, ULONG,cConnections) \
	ICOM_METHOD (HRESULT,Reset) \
	ICOM_METHOD1(HRESULT,Clone, IEnumConnections**,ppEnum)
#define IEnumConnectionPoints_IMETHODS \
	IUnknown_IMETHODS \
	IEnumConnectionPoints_METHODS
ICOM_DEFINE(IEnumConnectionPoints,IUnknown)
#undef ICOM_INTERFACE

/*** IUnknown methods ***/
#define IEnumConnectionPoints_QueryInterface(p,a,b)      ICOM_CALL2(QueryInterface,p,a,b)
#define IEnumConnectionPoints_AddRef(p)                  ICOM_CALL (AddRef,p)
#define IEnumConnectionPoints_Release(p)                 ICOM_CALL (Release,p)
/*** IConnectionPointContainer methods ***/
#define IEnumConnectionPoints_Next(p,a,b,c)              ICOM_CALL3(Next,p,a,b,c)
#define IEnumConnectionPoints_Skip(p,a)                  ICOM_CALL1(Skip,p,a)
#define IEnumConnectionPoints_Reset(p)                   ICOM_CALL (Reset,p)
#define IEnumConnectionPoints_Clone(p,a)                 ICOM_CALL1(Clone,p,a)
				
#ifdef __cplusplus
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* __WINE_WINE_OBJ_CONTROL_H */


