/*
 * Exported functions from the PostScript driver.
 *
 * [Ext]DeviceMode, DeviceCapabilities, AdvancedSetupDialog. 
 *
 * Will need ExtTextOut for winword6 (urgh!)
 *
 * Copyright 1998  Huw D M Davies
 *
 */

#include <string.h>
#include "psdrv.h"
#include "debugtools.h"
#include "winuser.h"
#include "winspool.h"
#include "prsht.h"
#include "psdlg.h"

DEFAULT_DEBUG_CHANNEL(psdrv);


/************************************************************************
 *
 *		PSDRV_MergeDevmodes
 *
 * Updates dm1 with some fields from dm2
 *
 */
void PSDRV_MergeDevmodes(PSDRV_DEVMODEA *dm1, PSDRV_DEVMODEA *dm2,
			 PRINTERINFO *pi)
{
    /* some sanity checks here on dm2 */

    if(dm2->dmPublic.dmFields & DM_ORIENTATION)
        dm1->dmPublic.u1.s1.dmOrientation = dm2->dmPublic.u1.s1.dmOrientation;

    /* NB PaperWidth is always < PaperLength */

    if(dm2->dmPublic.dmFields & DM_PAPERSIZE) {
        PAGESIZE *page;

	for(page = pi->ppd->PageSizes; page; page = page->next) {
	    if(page->WinPage == dm2->dmPublic.u1.s1.dmPaperSize)
	        break;
	}
	if(page) {
	    dm1->dmPublic.u1.s1.dmPaperSize = dm2->dmPublic.u1.s1.dmPaperSize;
	    dm1->dmPublic.u1.s1.dmPaperWidth = page->PaperDimension->x * 
								254.0 / 72.0;
	    dm1->dmPublic.u1.s1.dmPaperLength = page->PaperDimension->y *
								254.0 / 72.0;
	    TRACE("Changing page to %s %d x %d\n", page->FullName,
		  dm1->dmPublic.u1.s1.dmPaperWidth,
		  dm1->dmPublic.u1.s1.dmPaperLength );
	} else {
	    TRACE("Trying to change to unsupported pagesize %d\n",
		      dm2->dmPublic.u1.s1.dmPaperSize);
	}
    }

    if(dm2->dmPublic.dmFields & DM_PAPERLENGTH) {
        dm1->dmPublic.u1.s1.dmPaperLength = dm2->dmPublic.u1.s1.dmPaperLength; 
	TRACE("Changing PaperLength to %d\n",
	      dm2->dmPublic.u1.s1.dmPaperLength);
	FIXME("Changing PaperLength.  Do we adjust PaperSize?\n");
    }

    if(dm2->dmPublic.dmFields & DM_PAPERWIDTH) {
        dm1->dmPublic.u1.s1.dmPaperWidth = dm2->dmPublic.u1.s1.dmPaperWidth; 
	TRACE("Changing PaperWidth to %d\n",
	      dm2->dmPublic.u1.s1.dmPaperWidth);
	FIXME("Changing PaperWidth.  Do we adjust PaperSize?\n");
    }

    if(dm2->dmPublic.dmFields & DM_SCALE) {
        dm1->dmPublic.dmScale = dm2->dmPublic.dmScale;
	TRACE("Changing Scale to %d\n", dm2->dmPublic.dmScale);
    }

    if(dm2->dmPublic.dmFields & DM_COPIES) {
        dm1->dmPublic.dmCopies = dm2->dmPublic.dmCopies;
	TRACE("Changing Copies to %d\n", dm2->dmPublic.dmCopies);
    }

    if(dm2->dmPublic.dmFields & DM_DEFAULTSOURCE) {
        INPUTSLOT *slot;
	
	for(slot = pi->ppd->InputSlots; slot; slot = slot->next) {
	    if(slot->WinBin == dm2->dmPublic.dmDefaultSource)
	        break;
	}
	if(slot) {
	    dm1->dmPublic.dmDefaultSource = dm2->dmPublic.dmDefaultSource;
	    TRACE("Changing bin to '%s'\n", slot->FullName);
	} else {
	  TRACE("Trying to change to unsupported bin %d\n",
		dm2->dmPublic.dmDefaultSource);
	}
    }

   if (dm2->dmPublic.dmFields & DM_DEFAULTSOURCE )
       dm1->dmPublic.dmDefaultSource = dm2->dmPublic.dmDefaultSource;
   if (dm2->dmPublic.dmFields & DM_PRINTQUALITY )
       dm1->dmPublic.dmPrintQuality = dm2->dmPublic.dmPrintQuality;
   if (dm2->dmPublic.dmFields & DM_COLOR )
       dm1->dmPublic.dmColor = dm2->dmPublic.dmColor;
   if (dm2->dmPublic.dmFields & DM_DUPLEX )
       dm1->dmPublic.dmDuplex = dm2->dmPublic.dmDuplex;
   if (dm2->dmPublic.dmFields & DM_YRESOLUTION )
       dm1->dmPublic.dmYResolution = dm2->dmPublic.dmYResolution;
   if (dm2->dmPublic.dmFields & DM_TTOPTION )
       dm1->dmPublic.dmTTOption = dm2->dmPublic.dmTTOption;
   if (dm2->dmPublic.dmFields & DM_COLLATE )
       dm1->dmPublic.dmCollate = dm2->dmPublic.dmCollate;
   if (dm2->dmPublic.dmFields & DM_FORMNAME )
       lstrcpynA(dm1->dmPublic.dmFormName, dm2->dmPublic.dmFormName, CCHFORMNAME);
   if (dm2->dmPublic.dmFields & DM_BITSPERPEL )
       dm1->dmPublic.dmBitsPerPel = dm2->dmPublic.dmBitsPerPel;
   if (dm2->dmPublic.dmFields & DM_PELSWIDTH )
       dm1->dmPublic.dmPelsWidth = dm2->dmPublic.dmPelsWidth;
   if (dm2->dmPublic.dmFields & DM_PELSHEIGHT )
       dm1->dmPublic.dmPelsHeight = dm2->dmPublic.dmPelsHeight;
   if (dm2->dmPublic.dmFields & DM_DISPLAYFLAGS )
       dm1->dmPublic.dmDisplayFlags = dm2->dmPublic.dmDisplayFlags;
   if (dm2->dmPublic.dmFields & DM_DISPLAYFREQUENCY )
       dm1->dmPublic.dmDisplayFrequency = dm2->dmPublic.dmDisplayFrequency;
   if (dm2->dmPublic.dmFields & DM_POSITION )
       dm1->dmPublic.u1.dmPosition = dm2->dmPublic.u1.dmPosition;
   if (dm2->dmPublic.dmFields & DM_LOGPIXELS )
       dm1->dmPublic.dmLogPixels = dm2->dmPublic.dmLogPixels;
   if (dm2->dmPublic.dmFields & DM_ICMMETHOD )
       dm1->dmPublic.dmICMMethod = dm2->dmPublic.dmICMMethod;
   if (dm2->dmPublic.dmFields & DM_ICMINTENT )
       dm1->dmPublic.dmICMIntent = dm2->dmPublic.dmICMIntent;
   if (dm2->dmPublic.dmFields & DM_MEDIATYPE )
       dm1->dmPublic.dmMediaType = dm2->dmPublic.dmMediaType;
   if (dm2->dmPublic.dmFields & DM_DITHERTYPE )
       dm1->dmPublic.dmDitherType = dm2->dmPublic.dmDitherType;
   if (dm2->dmPublic.dmFields & DM_PANNINGWIDTH )
       dm1->dmPublic.dmPanningWidth = dm2->dmPublic.dmPanningWidth;
   if (dm2->dmPublic.dmFields & DM_PANNINGHEIGHT )
       dm1->dmPublic.dmPanningHeight = dm2->dmPublic.dmPanningHeight;

    return;
}


/**************************************************************
 *
 *	PSDRV_AdvancedSetupDialog16	[WINEPS.93]
 *
 */
WORD WINAPI PSDRV_AdvancedSetupDialog16(HWND16 hwnd, HANDLE16 hDriver,
					 LPDEVMODEA devin, LPDEVMODEA devout)
{

  TRACE("hwnd = %04x, hDriver = %04x devin=%p devout=%p\n", hwnd,
	hDriver, devin, devout);
  return IDCANCEL;
}

/****************************************************************
 *       PSDRV_PaperDlgProc
 *
 * Dialog proc for 'Paper' propsheet
 */
BOOL WINAPI PSDRV_PaperDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM
			       lParam)
{
  PSDRV_DLGINFO *di;
  int i, Cursel = 0; 
  PAGESIZE *ps;


  switch(msg) {
  case WM_INITDIALOG:
    di = (PSDRV_DLGINFO*)((PROPSHEETPAGEA*)lParam)->lParam;
    SetWindowLongA(hwnd, DWL_USER, (LONG)di);

    for(ps = di->pi->ppd->PageSizes, i = 0; ps; ps = ps->next, i++) {
      SendDlgItemMessageA(hwnd, IDD_PAPERS, LB_INSERTSTRING, i, 
			  (LPARAM)ps->FullName);
      if(di->pi->Devmode->dmPublic.u1.s1.dmPaperSize == ps->WinPage)
	Cursel = i;
    }
    SendDlgItemMessageA(hwnd, IDD_PAPERS, LB_SETCURSEL, Cursel, 0);

    CheckRadioButton(hwnd, IDD_ORIENT_PORTRAIT, IDD_ORIENT_LANDSCAPE,
		     di->pi->Devmode->dmPublic.u1.s1.dmOrientation ==
		     DMORIENT_PORTRAIT ? IDD_ORIENT_PORTRAIT :
		     IDD_ORIENT_LANDSCAPE);
    break;

  case WM_COMMAND:
    di = (PSDRV_DLGINFO *)GetWindowLongA(hwnd, DWL_USER);
    switch(LOWORD(wParam)) {
    case IDD_PAPERS:
      if(HIWORD(wParam) == LBN_SELCHANGE) {
	Cursel = SendDlgItemMessageA(hwnd, LOWORD(wParam), LB_GETCURSEL, 0, 0);
	for(i = 0, ps = di->pi->ppd->PageSizes; i < Cursel; i++, ps = ps->next)
	  ;
	TRACE("Setting pagesize to item %d Winpage = %d\n", Cursel,
	      ps->WinPage);
	di->dlgdm->dmPublic.u1.s1.dmPaperSize = ps->WinPage;
      }
      break;
    case IDD_ORIENT_PORTRAIT:
    case IDD_ORIENT_LANDSCAPE:
      TRACE("Setting orientation to %s\n", wParam == IDD_ORIENT_PORTRAIT ?
	    "portrait" : "landscape");
      di->dlgdm->dmPublic.u1.s1.dmOrientation = wParam == IDD_ORIENT_PORTRAIT ?
	DMORIENT_PORTRAIT : DMORIENT_LANDSCAPE;
      break;
    }
    break;

  case WM_NOTIFY:
   {
    NMHDR *nmhdr = (NMHDR *)lParam;
    di = (PSDRV_DLGINFO *)GetWindowLongA(hwnd, DWL_USER);
    switch(nmhdr->code) {
    case PSN_APPLY:
      memcpy(di->pi->Devmode, di->dlgdm, sizeof(PSDRV_DEVMODEA));
      SetWindowLongA(hwnd, DWL_MSGRESULT, PSNRET_NOERROR);
      return TRUE;
      break;

    default:
      return FALSE;
      break;
    }
    break;
   }
   
  default:
    return FALSE;
  }
  return TRUE;
}


static void (WINAPI* pInitCommonControls) (void);
static HPROPSHEETPAGE (WINAPI* pCreatePropertySheetPage) (LPCPROPSHEETPAGEA);
static int (WINAPI* pPropertySheet) (LPCPROPSHEETHEADERA);

/***************************************************************
 *
 *	PSDRV_ExtDeviceMode16	[WINEPS.90]
 *
 * Just returns default devmode at the moment
 */
INT16 WINAPI PSDRV_ExtDeviceMode16(HWND16 hwnd, HANDLE16 hDriver,
				   LPDEVMODEA lpdmOutput, LPSTR lpszDevice,
				   LPSTR lpszPort, LPDEVMODEA lpdmInput,
				   LPSTR lpszProfile, WORD fwMode)
{
  PRINTERINFO *pi = PSDRV_FindPrinterInfo(lpszDevice);
  if(!pi) return -1;

  TRACE("(hwnd=%04x, hDriver=%04x, devOut=%p, Device='%s', Port='%s', devIn=%p, Profile='%s', Mode=%04x)\n",
hwnd, hDriver, lpdmOutput, lpszDevice, lpszPort, lpdmInput, lpszProfile,
fwMode);

  if(!fwMode)
    return sizeof(DEVMODEA); /* Just copy dmPublic bit of PSDRV_DEVMODE */

  if((fwMode & DM_MODIFY) && lpdmInput) {
    TRACE("DM_MODIFY set. devIn->dmFields = %08lx\n", lpdmInput->dmFields);
    PSDRV_MergeDevmodes(pi->Devmode, (PSDRV_DEVMODEA *)lpdmInput, pi);
  }

  if(fwMode & DM_PROMPT) {
    HINSTANCE hinstComctl32, hinstWineps32 = LoadLibraryA("WINEPS");
    HPROPSHEETPAGE hpsp[1];
    PROPSHEETPAGEA psp;
    PROPSHEETHEADERA psh;
    PSDRV_DLGINFO *di;
    PSDRV_DEVMODEA *dlgdm;

    hinstComctl32 = LoadLibraryA("comctl32.dll");
    pInitCommonControls = (void*)GetProcAddress(hinstComctl32,
						"InitCommonControls");
    pCreatePropertySheetPage = (void*)GetProcAddress(hinstComctl32,
						    "CreatePropertySheetPage");
    pPropertySheet = (void*)GetProcAddress(hinstComctl32, "PropertySheet"); 
    memset(&psp,0,sizeof(psp));
    dlgdm = HeapAlloc( PSDRV_Heap, 0, sizeof(*dlgdm) );
    memcpy(dlgdm, pi->Devmode, sizeof(*dlgdm));
    di = HeapAlloc( PSDRV_Heap, 0, sizeof(*di) );
    di->pi = pi;
    di->dlgdm = dlgdm;
    psp.dwSize = sizeof(psp);
    psp.hInstance = hinstWineps32;
    psp.u.pszTemplate = "PAPER";
    psp.u2.pszIcon = NULL;
    psp.pfnDlgProc = PSDRV_PaperDlgProc;
    psp.lParam = (LPARAM)di;
    hpsp[0] = pCreatePropertySheetPage(&psp);

    memset(&psh, 0, sizeof(psh));
    psh.dwSize = sizeof(psh);
    psh.pszCaption = "Setup";
    psh.nPages = 1;
    psh.hwndParent = hwnd;
    psh.u3.phpage = hpsp;
    
    pPropertySheet(&psh);
    
  }
  if(fwMode & DM_UPDATE)
    FIXME("Mode DM_UPDATE.  Just do the same as DM_COPY\n");

  if((fwMode & DM_COPY) || (fwMode & DM_UPDATE)) {
    memcpy(lpdmOutput, pi->Devmode, sizeof(DEVMODEA));
  }
  return IDOK;
}

/**************************************************************
 *
 *       PSDRV_ExtDeviceMode
 */
INT PSDRV_ExtDeviceMode(LPSTR lpszDriver, HWND hwnd, LPDEVMODEA lpdmOutput,
			LPSTR lpszDevice, LPSTR lpszPort, LPDEVMODEA lpdmInput,
			LPSTR lpszProfile, DWORD dwMode)
{
    return PSDRV_ExtDeviceMode16(hwnd, 0, lpdmOutput, lpszDevice, lpszPort,
				 lpdmInput, lpszProfile, dwMode);
}

/***********************************************************************
 *
 *	PSDRV_DeviceCapabilities16	[WINEPS.91]
 *
 */
DWORD WINAPI PSDRV_DeviceCapabilities16(LPCSTR lpszDevice, LPCSTR lpszPort,
					WORD fwCapability, LPSTR lpszOutput,
					LPDEVMODEA lpDevMode)
{
  PRINTERINFO *pi;
  DEVMODEA *lpdm;
  pi = PSDRV_FindPrinterInfo(lpszDevice);

  TRACE("Cap=%d. Got PrinterInfo = %p\n", fwCapability, pi);


  if (!pi) {
	  ERR("no printerinfo for %s, return 0!\n",lpszDevice);
	  return 0;
  }


  lpdm = lpDevMode ? lpDevMode : (DEVMODEA *)pi->Devmode;

  switch(fwCapability) {

  case DC_PAPERS:
    {
      PAGESIZE *ps;
      WORD *wp = (WORD *)lpszOutput;
      int i = 0;

      for(ps = pi->ppd->PageSizes; ps; ps = ps->next, i++)
	if(lpszOutput != NULL)
	  *wp++ = ps->WinPage;
      return i;
    }

  case DC_PAPERSIZE:
    {
      PAGESIZE *ps;
      POINT16 *pt = (POINT16 *)lpszOutput;
      int i = 0;

      for(ps = pi->ppd->PageSizes; ps; ps = ps->next, i++)
	if(lpszOutput != NULL) {
	  pt->x = ps->PaperDimension->x * 254.0 / 72.0;
	  pt->y = ps->PaperDimension->y * 254.0 / 72.0;
	  pt++;
	}
      return i;
    }

  case DC_PAPERNAMES:
    {
      PAGESIZE *ps;
      char *cp = lpszOutput;
      int i = 0;

      for(ps = pi->ppd->PageSizes; ps; ps = ps->next, i++)
	if(lpszOutput != NULL) {
	  lstrcpynA(cp, ps->FullName, 64);
	  cp += 64;
	}
      return i;
    }

  case DC_ORIENTATION:
    return pi->ppd->LandscapeOrientation ? pi->ppd->LandscapeOrientation : 90;

  case DC_BINS:
    {
      INPUTSLOT *slot;
      WORD *wp = (WORD *)lpszOutput;
      int i = 0;

      /* We explicitly list DMBIN_AUTO first; actually while win9x does this
	 win2000 lists DMBIN_FORMSOURCE instead. */
      i++;
      if(lpszOutput != NULL)
	*wp++ = DMBIN_AUTO;

      for(slot = pi->ppd->InputSlots; slot; slot = slot->next, i++)
	if(lpszOutput != NULL)
	  *wp++ = slot->WinBin;
      return i;
    }

  case DC_BINNAMES:
    {
      INPUTSLOT *slot;
      char *cp = lpszOutput;
      int i = 0;
      
      /* Add an entry corresponding to DMBIN_AUTO, see DC_BINS */
      i++;
      if(lpszOutput != NULL) {
	strcpy(cp, "Automatically Select");
	cp += 24;
      }

      for(slot = pi->ppd->InputSlots; slot; slot = slot->next, i++)
	if(lpszOutput != NULL) {
	  lstrcpynA(cp, slot->FullName, 24);
	  cp += 24;
	}
      return i;
    }

  case DC_BINADJUST:
    FIXME("DC_BINADJUST: stub.\n");
    return DCBA_FACEUPNONE;

  case DC_ENUMRESOLUTIONS:
    {
      LONG *lp = (LONG*)lpszOutput;

      if(lpszOutput != NULL) {
	lp[0] = (LONG)pi->ppd->DefaultResolution;
	lp[1] = (LONG)pi->ppd->DefaultResolution;
      }
      return 1;
    }

  case DC_COPIES:
    FIXME("DC_COPIES: returning %d.  Is this correct?\n", lpdm->dmCopies);
    return lpdm->dmCopies;

  case DC_DRIVER:
    return lpdm->dmDriverVersion;

  case DC_DATATYPE_PRODUCED:
    FIXME("DATA_TYPE_PRODUCED: stub.\n");
    return -1; /* simulate that the driver supports 'RAW' */

  case DC_DUPLEX:
    FIXME("DC_DUPLEX: returning %d.  Is this correct?\n", lpdm->dmDuplex);
    return lpdm->dmDuplex;

  case DC_EMF_COMPLIANT:
    FIXME("DC_EMF_COMPLIANT: stub.\n");
    return -1; /* simulate that the driver do not support EMF */

  case DC_EXTRA:
    return lpdm->dmDriverExtra;

  case DC_FIELDS:
    return lpdm->dmFields;

  case DC_FILEDEPENDENCIES:
    FIXME("DC_FILEDEPENDENCIES: stub.\n");
    return 0;

  case DC_MAXEXTENT:
    {
      PAGESIZE *ps;
      int i;
      POINT ptMax;
      ptMax.x = ptMax.y = 0;

      if(lpszOutput == NULL)
	return -1;
 
      i = 0;
      for(ps = pi->ppd->PageSizes; ps; ps = ps->next, i++) {
	if(ps->PaperDimension->x > ptMax.x)
	  ptMax.x = ps->PaperDimension->x;
	if(ps->PaperDimension->y > ptMax.y)
	  ptMax.y = ps->PaperDimension->y;
      }
      *((POINT*)lpszOutput) = ptMax;
      return 1;
    }

  case DC_MINEXTENT:
    {
      PAGESIZE *ps;
      int i;
      POINT ptMax;
      ptMax.x = ptMax.y = 0;

      if(lpszOutput == NULL)
	return -1;
 
      i = 0;
      for(ps = pi->ppd->PageSizes; ps; ps = ps->next, i++) {
	if(ps->PaperDimension->x > ptMax.x)
	  ptMax.x = ps->PaperDimension->x;
	if(ps->PaperDimension->y > ptMax.y)
	  ptMax.y = ps->PaperDimension->y;
      }
      *((POINT*)lpszOutput) = ptMax;
      return 1;
    }

  case DC_SIZE:
    return lpdm->dmSize;

  case DC_TRUETYPE:
    FIXME("DC_TRUETYPE: stub\n");
    return DCTT_SUBDEV;

  case DC_VERSION:
    return lpdm->dmSpecVersion;

  default:
    FIXME("Unsupported capability %d\n", fwCapability);
  }
  return -1;
}

/**************************************************************
 *
 *     PSDRV_DeviceCapabilities
 */
DWORD PSDRV_DeviceCapabilities(LPSTR lpszDriver, LPCSTR lpszDevice,
			       LPCSTR lpszPort, WORD fwCapability,
			       LPSTR lpszOutput, LPDEVMODEA lpdm)
{
    return PSDRV_DeviceCapabilities16(lpszDevice, lpszPort, fwCapability,
				      lpszOutput, lpdm);
}

/***************************************************************
 *
 *	PSDRV_DeviceMode16	[WINEPS.13]
 *
 */
void WINAPI PSDRV_DeviceMode16(HWND16 hwnd, HANDLE16 hDriver,
LPSTR lpszDevice, LPSTR lpszPort)
{
    PSDRV_ExtDeviceMode16( hwnd, hDriver, NULL, lpszDevice, lpszPort, NULL, 
			   NULL, DM_PROMPT );
    return;
}

#if 0
typedef struct {
  DWORD nPages;
  DWORD Unknown;
  HPROPSHEETPAGE hPages[10];
} EDMPS;

INT PSDRV_ExtDeviceModePropSheet(HWND hwnd, LPSTR lpszDevice, LPSTR lpszPort,
				 LPVOID pPropSheet)
{
    EDMPS *ps = pPropSheet;
    PROPSHEETPAGE psp;

    psp->dwSize = sizeof(psp);
    psp->hInstance = 0x1234;
    
    ps->nPages = 1;
    
}

#endif
