/*
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

#include "rpc.h"
#include "rpcndr.h"
#ifndef COM_NO_WINDOWS_H
#include "windows.h"
#include "ole2.h"
#endif

#ifndef __WINE_DOCOBJ_H
#define __WINE_DOCOBJ_H

#include "winbase.h"
#include "windef.h"
#include "wingdi.h"
#include "winuser.h"
#include "ole2.h"
#include "wine/obj_oleaut.h"

/*****************************************************************************
 * Declare the structures
 */
typedef enum
{
	OLECMDF_SUPPORTED = 0x1,
	OLECMDF_ENABLED = 0x2,
	OLECMDF_LATCHED = 0x4,
	OLECMDF_NINCHED = 0x8
} OLECMDF;

typedef struct _tagOLECMD
{
	ULONG cmdID;
	DWORD cmdf;
} OLECMD;

typedef struct _tagOLECMDTEXT
{
	DWORD cmdtextf;
	ULONG cwActual;
	ULONG cwBuf;
	WCHAR rgwz[1];
} OLECMDTEXT;

typedef enum
{
	PRINTFLAG_MAYBOTHERUSER = 1,
	PRINTFLAG_PROMPTUSER  = 2,
	PRINTFLAG_USERMAYCHANGEPRINTER  = 4,
	PRINTFLAG_RECOMPOSETODEVICE = 8,
	PRINTFLAG_DONTACTUALLYPRINT = 16,
	PRINTFLAG_FORCEPROPERTIES = 32,
	PRINTFLAG_PRINTTOFILE = 64
} PRINTFLAG;

typedef struct tagPAGERANGE
{
	LONG nFromPage;
	LONG nToPage;
} PAGERANGE;

typedef struct  tagPAGESET
{
	ULONG cbStruct;
	BOOL fOddPages;
	BOOL fEvenPages;
	ULONG cPageRange;
	PAGERANGE rgPages[ 1 ];
} PAGESET;

typedef enum
{
	OLECMDTEXTF_NONE  = 0,
	OLECMDTEXTF_NAME  = 1,
	OLECMDTEXTF_STATUS  = 2
} OLECMDTEXTF;

typedef enum
{
	OLECMDEXECOPT_DODEFAULT = 0,
	OLECMDEXECOPT_PROMPTUSER  = 1,
	OLECMDEXECOPT_DONTPROMPTUSER  = 2,
	OLECMDEXECOPT_SHOWHELP  = 3
} OLECMDEXECOPT;

typedef enum OLECMDID
{
	OLECMDID_OPEN = 1,
	OLECMDID_NEW = 2,
	OLECMDID_SAVE = 3,
	OLECMDID_SAVEAS = 4,
	OLECMDID_SAVECOPYAS = 5,
	OLECMDID_PRINT = 6,
	OLECMDID_PRINTPREVIEW = 7,
	OLECMDID_PAGESETUP = 8,
	OLECMDID_SPELL = 9,
	OLECMDID_PROPERTIES = 10,
	OLECMDID_CUT = 11,
	OLECMDID_COPY = 12,
	OLECMDID_PASTE = 13,
	OLECMDID_PASTESPECIAL = 14,
	OLECMDID_UNDO = 15,
	OLECMDID_REDO = 16,
	OLECMDID_SELECTALL = 17,
	OLECMDID_CLEARSELECTION = 18,
	OLECMDID_ZOOM = 19,
	OLECMDID_GETZOOMRANGE = 20,
	OLECMDID_UPDATECOMMANDS = 21,
	OLECMDID_REFRESH = 22,
	OLECMDID_STOP = 23,
	OLECMDID_HIDETOOLBARS = 24,
	OLECMDID_SETPROGRESSMAX = 25,
	OLECMDID_SETPROGRESSPOS = 26,
	OLECMDID_SETPROGRESSTEXT = 27,
	OLECMDID_SETTITLE = 28,
	OLECMDID_SETDOWNLOADSTATE = 29,
	OLECMDID_STOPDOWNLOAD = 30,
	OLECMDID_ONTOOLBARACTIVATED = 31,
	OLECMDID_FIND = 32,
	OLECMDID_DELETE = 33,
	OLECMDID_HTTPEQUIV = 34,
	OLECMDID_HTTPEQUIV_DONE = 35,
	OLECMDID_ENABLE_INTERACTION = 36,
	OLECMDID_ONUNLOAD = 37,
	OLECMDID_PROPERTYBAG2 = 38,
	OLECMDID_PREREFRESH = 39
	/* iexplorer uses ... 44 */
}	OLECMDID;

#define OLECMDERR_E_FIRST            (OLE_E_LAST+1)
#define OLECMDERR_E_NOTSUPPORTED     (OLECMDERR_E_FIRST)
#define OLECMDERR_E_DISABLED         (OLECMDERR_E_FIRST+1)
#define OLECMDERR_E_NOHELP           (OLECMDERR_E_FIRST+2)
#define OLECMDERR_E_CANCELED         (OLECMDERR_E_FIRST+3)
#define OLECMDERR_E_UNKNOWNGROUP     (OLECMDERR_E_FIRST+4)

/*****************************************************************************
 * Predeclare the interfaces
 */
DEFINE_GUID(IID_IOleDocument, 0xb722bcc5,0x4e68,0x101b,0xa2,0xbc,0x00,0xaa,0x00,0x40,0x47,0x70);
typedef struct IOleDocument IOleDocument, *LPOLEDOCUMENT;

DEFINE_GUID(IID_IOleDocumentSite, 0xb722bcc7,0x4e68,0x101b,0xa2,0xbc,0x00,0xaa,0x00,0x40,0x47,0x70);
typedef struct IOleDocumentSite IOleDocumentSite, *LPOLEDOCUMENTSITE;

DEFINE_GUID(IID_IOleDocumentView, 0xb722bcc6,0x4e68,0x101b,0xa2,0xbc,0x00,0xaa,0x00,0x40,0x47,0x70);
typedef struct IOleDocumentView IOleDocumentView, *LPOLEDOCUMENTVIEW;

DEFINE_GUID(IID_IEnumOleDocumentViews, 0xb722bcc6,0x4e68,0x101b,0xa2,0xbc,0x00,0xaa,0x00,0x40,0x47,0x70);
typedef struct IEnumOleDocumentViews IEnumOleDocumentViews, *LPENUMOLEDOCUMENTVIEWS;

DEFINE_GUID(IID_IOleCommandTarget, 0xb722bccb,0x4e68,0x101b,0xa2,0xbc,0x00,0xaa,0x00,0x40,0x47,0x70);
typedef struct IOleCommandTarget IOleCommandTarget, *LPOLECOMMANDTARGET;

DEFINE_GUID(IID_IContinueCallback, 0xb722bcca,0x4e68,0x101b,0xa2,0xbc,0x00,0xaa,0x00,0x40,0x47,0x70);
typedef struct IContinueCallback IContinueCallback, *LPCONTINUECALLBACK;

DEFINE_GUID(IID_IPrint, 0xb722bcc9,0x4e68,0x101b,0xa2,0xbc,0x00,0xaa,0x00,0x40,0x47,0x70);
typedef struct IPrint IPrint, *LPPRINT;


/*****************************************************************************
 * IOleDocument interface
 */
#define ICOM_INTERFACE IOleDocument
#define IOleDocument_METHODS \
	ICOM_METHOD4(HRESULT,CreateView, IOleInPlaceSite*,pIPSite, IStream*,pstm, DWORD,dwReserved, IOleDocumentView**,ppView) \
	ICOM_METHOD1(HRESULT,GetDocMiscStatus, DWORD*,pdwStatus) \
	ICOM_METHOD2(HRESULT,EnumViews, IEnumOleDocumentViews**,ppEnum, IOleDocumentView**,ppView)
#define IOleDocument_IMETHODS \
	IUnknown_IMETHODS \
	IOleDocument_METHODS
ICOM_DEFINE(IOleDocument,IUnknown)
#undef ICOM_INTERFACE

/*** IUnknown methods ***/
#define IOleDocument_QueryInterface(p,a,b)      ICOM_CALL2(QueryInterface,p,a,b)
#define IOleDocument_AddRef(p)                  ICOM_CALL (AddRef,p)
#define IOleDocument_Release(p)                 ICOM_CALL (Release,p)
/*** IOleDocument methods ***/
#define IOleDocument_CreateView(p,a,b,c,d)      ICOM_CALL4(CreateView,p,a,b,c,d)
#define IOleDocument_GetDocMiscStatus(p,a)      ICOM_CALL1(GetDocMiscStatus,p,a)
#define IOleDocument_EnumViews(p,a,b)           ICOM_CALL2(EnumViews,p,a,b)


/*****************************************************************************
 * IOleDocumentSite interface
 */
#define ICOM_INTERFACE IOleDocumentSite
#define IOleDocumentSite_METHODS \
	ICOM_METHOD1(HRESULT,ActivateMe, IOleDocumentView*,pViewToActivate)
#define IOleDocumentSite_IMETHODS \
	IUnknown_IMETHODS \
	IOleDocumentSite_METHODS
ICOM_DEFINE(IOleDocumentSite,IUnknown)
#undef ICOM_INTERFACE

/*** IUnknown methods ***/
#define IOleDocumentSite_QueryInterface(p,a,b)      ICOM_CALL2(QueryInterface,p,a,b)
#define IOleDocumentSite_AddRef(p)                  ICOM_CALL (AddRef,p)
#define IOleDocumentSite_Release(p)                 ICOM_CALL (Release,p)
/*** IOleDocumentSite methods ***/
#define IOleDocumentSite_ActivateMe(p,a)            ICOM_CALL4(ActivateMe,p,a)


/*****************************************************************************
 * IOleDocumentSite interface
 */
#define ICOM_INTERFACE IOleDocumentView
#define IOleDocumentView_METHODS \
	ICOM_METHOD1(HRESULT,SetInPlaceSite, IOleInPlaceSite*,pIPSite) \
	ICOM_METHOD1(HRESULT,GetInPlaceSite, IOleInPlaceSite**,ppIPSite) \
	ICOM_METHOD1(HRESULT,GetDocument, IUnknown**,ppunk) \
	ICOM_METHOD1(HRESULT,SetRect, LPRECT,prcView) \
	ICOM_METHOD1(HRESULT,GetRect, LPRECT,prcView) \
	ICOM_METHOD4(HRESULT,SetRectComplex, LPRECT,prcView, LPRECT,prcHScroll, LPRECT,prcVScroll, LPRECT,prcSizeBox) \
	ICOM_METHOD1(HRESULT,Show, BOOL,fShow) \
	ICOM_METHOD1(HRESULT,UIActivate, BOOL,fUIActivate) \
	ICOM_METHOD (HRESULT,Open) \
	ICOM_METHOD1(HRESULT,CloseView, DWORD,dwReserved) \
	ICOM_METHOD1(HRESULT,SaveViewState, LPSTREAM,pstm) \
	ICOM_METHOD1(HRESULT,ApplyViewState,LPSTREAM,pstm) \
	ICOM_METHOD2(HRESULT,Clone, IOleInPlaceSite*,pIPSiteNew, IOleDocumentView**,ppViewNew)
#define IOleDocumentView_IMETHODS \
	IUnknown_IMETHODS \
	IOleDocumentView_METHODS
ICOM_DEFINE(IOleDocumentView,IUnknown)
#undef ICOM_INTERFACE

/*** IUnknown methods ***/
#define IOleDocumentView_QueryInterface(p,a,b)      ICOM_CALL2(QueryInterface,p,a,b)
#define IOleDocumentView_AddRef(p)                  ICOM_CALL (AddRef,p)
#define IOleDocumentView_Release(p)                 ICOM_CALL (Release,p)
/*** IOleDocumentView methods ***/
#define IOleDocumentView_SetInPlaceSite(p,a)        ICOM_CALL1(SetInPlaceSite,p,a)
#define IOleDocumentView_GetInPlaceSite(p,a)        ICOM_CALL1(GetInPlaceSite,p,a)
#define IOleDocumentView_GetDocument(p,a)           ICOM_CALL1(GetDocument,p,a)
#define IOleDocumentView_SetRect(p,a)               ICOM_CALL1(SetRect,p,a)
#define IOleDocumentView_GetRect(p,a)               ICOM_CALL1(GetRect,p,a)
#define IOleDocumentView_GetRectComplex(p,a,b,c,d)  ICOM_CALL4(GetRectComplex,p,a,b,c,d)
#define IOleDocumentView_Show(p,a)                  ICOM_CALL1(Show,p,a)
#define IOleDocumentView_UIActivate(p,a)            ICOM_CALL1(UIActivate,p,a)
#define IOleDocumentView_Open(p)                    ICOM_CALL (Open,p)
#define IOleDocumentView_CloseView(p,a)             ICOM_CALL1(CloseView,p,a)
#define IOleDocumentView_SaveViewState(p,a)         ICOM_CALL1(SaveViewState,p,a)
#define IOleDocumentView_ApplyViewState(p,a)        ICOM_CALL1(ApplyViewState,p,a)
#define IOleDocumentView_Clone(p,a,b)               ICOM_CALL2(Clone,p,a,b)


/*****************************************************************************
 * IOleDocumentSite interface
 */
#define ICOM_INTERFACE IEnumOleDocumentViews
#define IEnumOleDocumentViews_METHODS \
	ICOM_METHOD3(HRESULT,Next, ULONG,cViews, IOleDocumentView**,rgpView, ULONG*,pcFetched) \
	ICOM_METHOD1(HRESULT,Skip, ULONG,cViews) \
	ICOM_METHOD (HRESULT,Reset) \
	ICOM_METHOD1(HRESULT,Clone, IEnumOleDocumentViews**,ppEnum)
#define IEnumOleDocumentViews_IMETHODS \
	IUnknown_IMETHODS \
	IEnumOleDocumentViews_METHODS
ICOM_DEFINE(IEnumOleDocumentViews,IUnknown)
#undef ICOM_INTERFACE

/*** IUnknown methods ***/
#define IEnumOleDocumentViews_QueryInterface(p,a,b)   ICOM_CALL2(QueryInterface,p,a,b)
#define IEnumOleDocumentViews_AddRef(p)               ICOM_CALL (AddRef,p)
#define IEnumOleDocumentViews_Release(p)              ICOM_CALL (Release,p)
/*** IEnumOleDocumentViews methods ***/
#define IEnumOleDocumentViews_Next(p,a,b,c)           ICOM_CALL3(Next,p,a,b,c)
#define IEnumOleDocumentViews_Skip(p,a)               ICOM_CALL1(Skip,p,a)
#define IEnumOleDocumentViews_Reset(p)                ICOM_CALL (Reset,p)
#define IEnumOleDocumentViews_Clone(p,a)              ICOM_CALL1(Clone,p,a)


/*****************************************************************************
 * IOleCommandTarget interface
 */
#define ICOM_INTERFACE IOleCommandTarget
#define IOleCommandTarget_METHODS \
	ICOM_METHOD4(HRESULT,QueryStatus, const GUID*,pguidCmdGroup, ULONG,cCmds, OLECMD*,prgCmds, OLECMDTEXT*,pCmdText) \
	ICOM_METHOD5(HRESULT,Exec, const GUID*,pguidCmdGroup, DWORD,nCmdID, DWORD,nCmdexecopt, VARIANT*,pvaIn, VARIANT*,pvaOut)
#define IOleCommandTarget_IMETHODS \
	IUnknown_IMETHODS \
	IOleCommandTarget_METHODS
ICOM_DEFINE(IOleCommandTarget,IUnknown)
#undef ICOM_INTERFACE

/*** IUnknown methods ***/
#define IOleCommandTarget_QueryInterface(p,a,b)   ICOM_CALL2(QueryInterface,p,a,b)
#define IOleCommandTarget_AddRef(p)               ICOM_CALL (AddRef,p)
#define IOleCommandTarget_Release(p)              ICOM_CALL (Release,p)
/*** IOleCommandTarget methods ***/
#define IOleCommandTarget_QueryStatus(p,a,b,c,d)  ICOM_CALL4(QueryStatus,p,a,b,c,d)
#define IOleCommandTarget_Exec(p,a,b,c,d,e)       ICOM_CALL5(Exec,p,a,b,c,d,e)


/*****************************************************************************
 * IContinueCallback interface
 */
#define ICOM_INTERFACE IContinueCallback
#define IContinueCallback_METHODS \
	ICOM_METHOD (HRESULT,FContinue) \
	ICOM_METHOD3(HRESULT,FContinuePrinting, LONG,nCntPrinted, LONG,nCurPage, WCHAR*,pwszPrintStatus)
#define IContinueCallback_IMETHODS \
	IUnknown_IMETHODS \
	IContinueCallback_METHODS
ICOM_DEFINE(IContinueCallback,IUnknown)
#undef ICOM_INTERFACE

/*** IUnknown methods ***/
#define IContinueCallback_QueryInterface(p,a,b)   ICOM_CALL2(QueryInterface,p,a,b)
#define IContinueCallback_AddRef(p)               ICOM_CALL (AddRef,p)
#define IContinueCallback_Release(p)              ICOM_CALL (Release,p)
/*** IContinueCallback methods ***/
#define IContinueCallback_FContinue(p)               ICOM_CALL (FContinue,p)
#define IContinueCallback_FContinuePrinting(p,a,b,c) ICOM_CALL3(FContinuePrinting,p,a,b,c)


/*****************************************************************************
 * IPrint interface
 */
#define ICOM_INTERFACE IPrint
#define IPrint_METHODS \
	ICOM_METHOD1(HRESULT,SetInitialPageNum, LONG,nFirstPage) \
	ICOM_METHOD2(HRESULT,GetPageInfo, LONG*,pnFirstPage, LONG*,pcPages) \
	ICOM_METHOD8(HRESULT,Print, DWORD,grfFlags, DVTARGETDEVICE**,pptd, PAGESET**,ppPageSet, STGMEDIUM*,pstgmOptions, IContinueCallback*,pcallback, LONG,nFirstPage, LONG*,pcPagesPrinted, LONG*,pnLastPage)
#define IPrint_IMETHODS \
	IUnknown_IMETHODS \
	IPrint_METHODS
ICOM_DEFINE(IPrint,IUnknown)
#undef ICOM_INTERFACE

/*** IUnknown methods ***/
#define IPrint_QueryInterface(p,a,b)   ICOM_CALL2(QueryInterface,p,a,b)
#define IPrint_AddRef(p)               ICOM_CALL (AddRef,p)
#define IPrint_Release(p)              ICOM_CALL (Release,p)
/*** Iprint methods ***/
#define IPrint_SetInitialPageNum(p,a)  ICOM_CALL1(SetInitialPageNum,p,a)
#define IPrint_GetPageInfo(p,a,b)      ICOM_CALL2(GetPageInfo,p,a,b)
#define IPrint_Print(p,a,b,c,d,e,f,g)  ICOM_CALL7(Print,p,a,b,c,d,e,f,g)


#define LPMSODOCUMENT                  LPOLEDOCUMENT
#define LPMSODOCUMENTSITE              LPOLEDOCUMENTSITE
#define LPMSOVIEW                      LPOLEDOCUMENTVIEW
#define LPENUMMSOVIEW                  LPENUMOLEDOCUMENTVIEWS
#define LPMSOCOMMANDTARGET             LPOLECOMMANDTARGET
#define IID_IMsoDocument               IID_IOleDocument
#define IID_IMsoDocumentSite           IID_IOleDocumentSite
#define IID_IMsoView                   IID_IOleDocumentView
#define IID_IEnumMsoView               IID_IEnumOleDocumentViews
#define IID_IMsoCommandTarget          IID_IOleCommandTarget


#endif /* __WINE_DOCOBJ_H */
