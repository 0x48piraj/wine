/*		DirectDraw using DGA or Xlib(XSHM)
 *
 * Copyright 1997-1999 Marcus Meissner
 * Copyright 1998 Lionel Ulmer (most of Direct3D stuff)
 */
#include "config.h"

#include <unistd.h>
#include <assert.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>

#include "winerror.h"
#include "options.h"
#include "debugtools.h"
#include "ddraw.h"

DEFAULT_DEBUG_CHANNEL(ddraw);

#include "x11_private.h"

#ifdef HAVE_LIBXXSHM
int XShmErrorFlag = 0;
#endif

static inline BOOL get_option( const char *name, BOOL def ) {
    return PROFILE_GetWineIniBool( "x11drv", name, def );
}

static BOOL
DDRAW_XSHM_Available(void) {
#ifdef HAVE_LIBXXSHM
    if (get_option( "UseXShm", 1 )) {
	if (TSXShmQueryExtension(display)) {
	    int		major,minor;
	    Bool	shpix;

	    if (TSXShmQueryVersion(display, &major, &minor, &shpix))
		return TRUE;
	}
    }
#endif
    return FALSE;
}

#ifdef HAVE_XVIDEO
static BOOL
DDRAW_XVIDEO_Available(x11_dd_private *x11ddp) {
  unsigned int p_version, p_release, p_request_base, p_event_base, p_error_base;
  
  if (TSXvQueryExtension(display, &p_version, &p_release, &p_request_base,
		       &p_event_base, &p_error_base) == Success) {
    XvAdaptorInfo *ai;
    int num_adaptators, i, default_port;
    
    if ((p_version < 2) || ((p_version == 2) && (p_release < 2))) {
      TRACE("XVideo extension does NOT support needed features (need version 2.2) !\n");
      return FALSE;
    }

    if (TSXvQueryAdaptors(display, X11DRV_GetXRootWindow(), &num_adaptators, &ai) != Success) {
      TRACE("Failed to get list of adaptators.\n");
      return FALSE;
    }
    if (num_adaptators == 0) {
      TRACE("No XVideo supporting adaptators found.\n");
      return FALSE;
    }

    default_port = PROFILE_GetWineIniInt("x11drv", "XVideoPort", -1);
    for (i = 0; i < num_adaptators; i++) {
      if ((ai[i].type & XvInputMask) && (ai[i].type & XvImageMask)) {
	/* This supports everything I want : XvImages and the possibility to put something */
	if (default_port == -1) {
	  default_port = ai[i].base_id;
	  break;
	} else {
	  if ((ai[i].base_id <= default_port) &&
	      ((ai[i].base_id + ai[i].num_ports) > default_port)) {
	    break;
	  }
	}
      }
    }
    if (i == num_adaptators) {
      if (default_port != -1) {
	ERR("User specified port (%d) not found.\n", default_port);
      } else {
	TRACE("No input + image capable device found.\n");
      }
      TSXvFreeAdaptorInfo(ai);
      return FALSE;
    }
    x11ddp->port_id = default_port;

    TRACE("XVideo support available (using version %d.%d)\n", p_version, p_release);
    TSXvFreeAdaptorInfo(ai);
    return TRUE;
  }
  return FALSE;
}
#endif

static HRESULT X11_Create( LPDIRECTDRAW *lplpDD ) {
    IDirectDrawImpl*	ddraw;
    int			depth;
    x11_dd_private	*x11priv;

    if (lplpDD == NULL) /* Testing ... this driver works all the time */
	return DD_OK;

    *lplpDD = (LPDIRECTDRAW)HeapAlloc(
	GetProcessHeap(),
	HEAP_ZERO_MEMORY,
	sizeof(IDirectDrawImpl)
    );
    ddraw = (IDirectDrawImpl*)*lplpDD;
    ICOM_VTBL(ddraw)= &xlib_ddvt;
    ddraw->ref	= 1;

    ddraw->d = HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*(ddraw->d)));
    ddraw->d->ref = 1;
    ddraw->d->private	= HeapAlloc(
	GetProcessHeap(),
	HEAP_ZERO_MEMORY,
	sizeof(x11_dd_private)
    );
    x11priv = (x11_dd_private*)ddraw->d->private;

    /* At DirectDraw creation, the depth is the default depth */
    depth = DefaultDepthOfScreen(X11DRV_GetXScreen());

    switch (_common_depth_to_pixelformat(depth,(LPDIRECTDRAW)ddraw)) {
    case -2: ERR("no depth conversion mode for depth %d found\n",depth); break;
    case -1: WARN("No conversion needed for depth %d.\n",depth); break;
    case 0: MESSAGE("Conversion needed from %d.\n",depth); break;
    }

    ddraw->d->height = GetSystemMetrics(SM_CYSCREEN);
    ddraw->d->width = GetSystemMetrics(SM_CXSCREEN);
#ifdef HAVE_LIBXXSHM
    /* Test if XShm is available. */
    if ((x11priv->xshm_active = DDRAW_XSHM_Available())) {
	x11priv->xshm_compl = 0;
	TRACE("Using XShm extension.\n");
    }
#endif

#ifdef HAVE_XVIDEO
    /* Test if XVideo support is available */
    if ((x11priv->xvideo_active = DDRAW_XVIDEO_Available(x11priv))) {
      TRACE("Using XVideo extension on port '%ld'.\n", x11priv->port_id);
    }
#endif
    return DD_OK;
}

/* Where do these GUIDs come from?  mkuuid.
 * They exist solely to distinguish between the targets Wine support,
 * and should be different than any other GUIDs in existence.
 */
static GUID X11_DirectDraw_GUID = { /* 1574a740-dc61-11d1-8407-f7875a7d1879 */
    0x1574a740,
    0xdc61,
    0x11d1,
    {0x84, 0x07, 0xf7, 0x87, 0x5a, 0x7d, 0x18, 0x79}
};

ddraw_driver x11_driver = {
    &X11_DirectDraw_GUID,
    "display",
    "WINE X11 DirectDraw Driver",
    50,
    X11_Create
}; 

DECL_GLOBAL_CONSTRUCTOR(X11_register) { ddraw_register_driver(&x11_driver); }
