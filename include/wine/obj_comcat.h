/*
 * Defines the COM interfaces and APIs related to Component Category Manager
 *
 * Depends on 'obj_enumguid.h'.
 *
 * Copyright (C) 2002 John K. Hohm
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

#ifndef __WINE_WINE_OBJ_COMCAT_H
#define __WINE_WINE_OBJ_COMCAT_H

#ifdef __cplusplus
extern "C" {
#endif /* defined(__cplusplus) */

/*****************************************************************************
 * Types
 */
typedef GUID CATID;
typedef REFGUID REFCATID;
#define CATID_NULL GUID_NULL
#define IsEqualCATID(a, b) IsEqualGUID(a, b)

typedef struct tagCATEGORYINFO {
    CATID   catid;		/* category identifier for component */
    LCID    lcid;		/* locale identifier */
    OLECHAR szDescription[128];	/* description of the category */
} CATEGORYINFO, *LPCATEGORYINFO;

/*****************************************************************************
 * Category IDs
 */
DEFINE_GUID(CATID_Insertable,			  0x40FC6ED3, 0x2438, 0x11CF, 0xA3, 0xDB, 0x08, 0x00, 0x36, 0xF1, 0x25, 0x02);
DEFINE_GUID(CATID_Control,			  0x40FC6ED4, 0x2438, 0x11CF, 0xA3, 0xDB, 0x08, 0x00, 0x36, 0xF1, 0x25, 0x02);
DEFINE_GUID(CATID_Programmable,			  0x40FC6ED5, 0x2438, 0x11CF, 0xA3, 0xDB, 0x08, 0x00, 0x36, 0xF1, 0x25, 0x02);
DEFINE_GUID(CATID_IsShortcut,			  0x40FC6ED6, 0x2438, 0x11CF, 0xA3, 0xDB, 0x08, 0x00, 0x36, 0xF1, 0x25, 0x02);
DEFINE_GUID(CATID_NeverShowExt,			  0x40FC6ED7, 0x2438, 0x11CF, 0xA3, 0xDB, 0x08, 0x00, 0x36, 0xF1, 0x25, 0x02);
DEFINE_GUID(CATID_DocObject,			  0x40FC6ED8, 0x2438, 0x11CF, 0xA3, 0xDB, 0x08, 0x00, 0x36, 0xF1, 0x25, 0x02);
DEFINE_GUID(CATID_Printable,			  0x40FC6ED9, 0x2438, 0x11CF, 0xA3, 0xDB, 0x08, 0x00, 0x36, 0xF1, 0x25, 0x02);
DEFINE_GUID(CATID_RequiresDataPathHost,		  0x0DE86A50, 0x2BAA, 0x11CF, 0xA2, 0x29, 0x00, 0xAA, 0x00, 0x3D, 0x73, 0x52);
DEFINE_GUID(CATID_PersistsToMoniker,		  0x0DE86A51, 0x2BAA, 0x11CF, 0xA2, 0x29, 0x00, 0xAA, 0x00, 0x3D, 0x73, 0x52);
DEFINE_GUID(CATID_PersistsToStorage,		  0x0DE86A52, 0x2BAA, 0x11CF, 0xA2, 0x29, 0x00, 0xAA, 0x00, 0x3D, 0x73, 0x52);
DEFINE_GUID(CATID_PersistsToStreamInit,		  0x0DE86A53, 0x2BAA, 0x11CF, 0xA2, 0x29, 0x00, 0xAA, 0x00, 0x3D, 0x73, 0x52);
DEFINE_GUID(CATID_PersistsToStream,		  0x0DE86A54, 0x2BAA, 0x11CF, 0xA2, 0x29, 0x00, 0xAA, 0x00, 0x3D, 0x73, 0x52);
DEFINE_GUID(CATID_PersistsToMemory,		  0x0DE86A55, 0x2BAA, 0x11CF, 0xA2, 0x29, 0x00, 0xAA, 0x00, 0x3D, 0x73, 0x52);
DEFINE_GUID(CATID_PersistsToFile,		  0x0DE86A56, 0x2BAA, 0x11CF, 0xA2, 0x29, 0x00, 0xAA, 0x00, 0x3D, 0x73, 0x52);
DEFINE_GUID(CATID_PersistsToPropertyBag,	  0x0DE86A57, 0x2BAA, 0x11CF, 0xA2, 0x29, 0x00, 0xAA, 0x00, 0x3D, 0x73, 0x52);
DEFINE_GUID(CATID_InternetAware,		  0x0DE86A58, 0x2BAA, 0x11CF, 0xA2, 0x29, 0x00, 0xAA, 0x00, 0x3D, 0x73, 0x52);
DEFINE_GUID(CATID_DesignTimeUIActivatableControl, 0xF2BB56D1, 0xDB07, 0x11D1, 0xAA, 0x6B, 0x00, 0x60, 0x97, 0xDB, 0x95, 0x39);
 
/*****************************************************************************
 * Aliases for EnumGUID
 */
#define IEnumCATID IEnumGUID
#define LPENUMCATID LPENUMGUID
#define IID_IEnumCATID IID_IEnumGUID

#define IEnumCLSID IEnumGUID
#define LPENUMCLSID LPENUMGUID
#define IID_IEnumCLSID IID_IEnumGUID

/*****************************************************************************
 * Predeclare the interfaces
 */
DEFINE_OLEGUID(IID_ICatInformation, 0x0002E013L, 0, 0);
typedef struct ICatInformation ICatInformation, *LPCATINFORMATION;

DEFINE_OLEGUID(IID_ICatRegister, 0x0002E012L, 0, 0);
typedef struct ICatRegister ICatRegister, *LPCATREGISTER;

DEFINE_OLEGUID(IID_IEnumCATEGORYINFO, 0x0002E011L, 0, 0);
typedef struct IEnumCATEGORYINFO IEnumCATEGORYINFO, *LPENUMCATEGORYINFO;

/* The Component Category Manager */
DEFINE_OLEGUID(CLSID_StdComponentCategoriesMgr, 0x0002E005L, 0, 0);

/*****************************************************************************
 * ICatInformation
 */
#define ICOM_INTERFACE ICatInformation
#define ICatInformation_METHODS \
    ICOM_METHOD2(HRESULT, EnumCategories, LCID, lcid, IEnumCATEGORYINFO**, ppenumCatInfo) \
    ICOM_METHOD3(HRESULT, GetCategoryDesc, REFCATID, rcatid, LCID, lcid, PWCHAR*, ppszDesc) \
    ICOM_METHOD5(HRESULT, EnumClassesOfCategories, ULONG, cImplemented, CATID*, rgcatidImpl, ULONG, cRequired, CATID*, rgcatidReq, IEnumCLSID**, ppenumCLSID) \
    ICOM_METHOD5(HRESULT, IsClassOfCategories, REFCLSID, rclsid, ULONG, cImplemented, CATID*, rgcatidImpl, ULONG, cRequired, CATID*, rgcatidReq) \
    ICOM_METHOD2(HRESULT, EnumImplCategoriesOfClass, REFCLSID, rclsid, IEnumCATID**, ppenumCATID) \
    ICOM_METHOD2(HRESULT, EnumReqCategoriesOfClass, REFCLSID, rclsid, IEnumCATID**, ppenumCATID)
#define ICatInformation_IMETHODS \
    IUnknown_IMETHODS \
    ICatInformation_METHODS
ICOM_DEFINE(ICatInformation,IUnknown)
#undef ICOM_INTERFACE

#ifdef ICOM_CINTERFACE
/*** IUnknown methods ***/
#define ICatInformation_QueryInterface(p,a,b) ICOM_CALL2(QueryInterface,p,a,b)
#define ICatInformation_AddRef(p)             ICOM_CALL (AddRef,p)
#define ICatInformation_Release(p)            ICOM_CALL (Release,p)
/*** ICatInformation methods ***/
#define ICatInformation_EnumCategories(p,a,b) ICOM_CALL2(EnumCategories,p,a,b)
#define ICatInformation_GetCategoryDesc(p,a,b,c) ICOM_CALL3(GetCategoryDesc,p,a,b,c)
#define ICatInformation_EnumClassesOfCategories(p,a,b,c,d,e) ICOM_CALL5(EnumClassesOfCategories,p,a,b,c,d,e)
#define ICatInformation_IsClassOfCategories(p,a,b,c,d,e) ICOM_CALL5(IsClassOfCategories,p,a,b,c,d,e)
#define ICatInformation_EnumImplCategoriesOfClass(p,a,b) ICOM_CALL2(EnumImplCategoriesOfClass,p,a,b)
#define ICatInformation_EnumReqCategoriesOfClass(p,a,b) ICOM_CALL2(EnumReqCategoriesOfClass,p,a,b)
#endif

/*****************************************************************************
 * ICatRegister
 */
#define ICOM_INTERFACE ICatRegister
#define ICatRegister_METHODS \
    ICOM_METHOD2(HRESULT, RegisterCategories, ULONG, cCategories, CATEGORYINFO*, rgCategoryInfo) \
    ICOM_METHOD2(HRESULT, UnRegisterCategories, ULONG, cCategories, CATID*, rgcatid) \
    ICOM_METHOD3(HRESULT, RegisterClassImplCategories, REFCLSID, rclsid, ULONG, cCategories, CATID*, rgcatid) \
    ICOM_METHOD3(HRESULT, UnRegisterClassImplCategories, REFCLSID, rclsid, ULONG, cCategories, CATID*, rgcatid) \
    ICOM_METHOD3(HRESULT, RegisterClassReqCategories, REFCLSID, rclsid, ULONG, cCategories, CATID*, rgcatid) \
    ICOM_METHOD3(HRESULT, UnRegisterClassReqCategories, REFCLSID, rclsid, ULONG, cCategories, CATID*, rgcatid)
#define ICatRegister_IMETHODS \
    IUnknown_IMETHODS \
    ICatRegister_METHODS
ICOM_DEFINE(ICatRegister,IUnknown)
#undef ICOM_INTERFACE

#ifdef ICOM_CINTERFACE
/*** IUnknown methods ***/
#define ICatRegister_QueryInterface(p,a,b) ICOM_CALL2(QueryInterface,p,a,b)
#define ICatRegister_AddRef(p)             ICOM_CALL (AddRef,p)
#define ICatRegister_Release(p)            ICOM_CALL (Release,p)
/*** ICatRegister methods ***/
#define ICatRegister_RegisterCategories(p,a,b) ICOM_CALL2(RegisterCategories,p,a,b)
#define ICatRegister_UnRegisterCategories(p,a,b) ICOM_CALL2(UnRegisterCategories,p,a,b)
#define ICatRegister_RegisterClassImplCategories(p,a,b,c) ICOM_CALL3(RegisterClassImplCategories,p,a,b,c)
#define ICatRegister_UnRegisterClassImplCategories(p,a,b,c) ICOM_CALL3(UnRegisterClassImplCategories,p,a,b,c)
#define ICatRegister_RegisterClassReqCategories(p,a,b,c) ICOM_CALL3(RegisterClassReqCategories,p,a,b,c)
#define ICatRegister_UnRegisterClassReqCategories(p,a,b,c) ICOM_CALL3(UnRegisterClassReqCategories,p,a,b,c)
#endif

/*****************************************************************************
 * IEnumCATEGORYINFO
 */
#define ICOM_INTERFACE IEnumCATEGORYINFO
#define IEnumCATEGORYINFO_METHODS \
    ICOM_METHOD3(HRESULT, Next, ULONG, celt, CATEGORYINFO*, rgelt, ULONG*, pceltFetched) \
    ICOM_METHOD1(HRESULT, Skip, ULONG, celt) \
    ICOM_METHOD (HRESULT, Reset) \
    ICOM_METHOD1(HRESULT, Clone, IEnumCATEGORYINFO**, ppenum)
#define IEnumCATEGORYINFO_IMETHODS \
    IUnknown_IMETHODS \
    IEnumCATEGORYINFO_METHODS
ICOM_DEFINE(IEnumCATEGORYINFO,IUnknown)
#undef ICOM_INTERFACE

#ifdef ICOM_CINTERFACE
/*** IUnknown methods ***/
#define IEnumCATEGORYINFO_QueryInterface(p,a,b) ICOM_CALL2(QueryInterface,p,a,b)
#define IEnumCATEGORYINFO_AddRef(p)             ICOM_CALL (AddRef,p)
#define IEnumCATEGORYINFO_Release(p)            ICOM_CALL (Release,p)
/*** IEnumCATEGORYINFO methods ***/
#define IEnumCATEGORYINFO_Next(p,a,b,c)         ICOM_CALL3(Next,p,a,b,c)
#define IEnumCATEGORYINFO_Skip(p,a)             ICOM_CALL1(Skip,p,a)
#define IEnumCATEGORYINFO_Reset(p)              ICOM_CALL(Reset,p)
#define IEnumCATEGORYINFO_Clone(p,a)            ICOM_CALL1(Clone,p,a)
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* __WINE_WINE_OBJ_COMCAT_H */
