/*
 * What processor?
 *
 * Copyright 1995,1997 Morten Welinder
 * Copyright 1997-1998 Marcus Meissner
 */

#include "config.h"
#include "wine/port.h"

#include <ctype.h>
#include <string.h>
#include <stdio.h>

#include "winbase.h"
#include "winreg.h"
#include "winnt.h"
#include "winerror.h"
#include "debugtools.h"

DEFAULT_DEBUG_CHANNEL(reg);

static BYTE PF[64] = {0,};

/***********************************************************************
 * 			GetSystemInfo            	[KERNEL32.@]
 *
 * Gets the current system information.
 *
 * On the first call it creates cached values, so it doesn't have to determine
 * them repeatedly. On Linux, the /proc/cpuinfo special file is used.
 *
 * It creates a registry subhierarchy, looking like:
 * \HARDWARE\DESCRIPTION\System\CentralProcessor\<processornumber>\
 *							Identifier (CPU x86)
 * Note that there is a hierarchy for every processor installed, so this
 * supports multiprocessor systems. This is done like Win95 does it, I think.
 *							
 * It also creates a cached flag array for IsProcessorFeaturePresent().
 *
 * No NULL ptr check for LPSYSTEM_INFO in Win9x.
 * 
 * RETURNS
 *	nothing, really
 */
VOID WINAPI GetSystemInfo(
	LPSYSTEM_INFO si	/* [out] system information */
) {
	static int cache = 0;
	static SYSTEM_INFO cachedsi;
	HKEY	xhkey=0,hkey;

	if (cache) {
		memcpy(si,&cachedsi,sizeof(*si));
		return;
	}
	memset(PF,0,sizeof(PF));

	/* choose sensible defaults ...
	 * FIXME: perhaps overrideable with precompiler flags?
	 */
	cachedsi.u.s.wProcessorArchitecture     = PROCESSOR_ARCHITECTURE_INTEL;
	cachedsi.dwPageSize 			= getpagesize();

	/* FIXME: the two entries below should be computed somehow... */
	cachedsi.lpMinimumApplicationAddress	= (void *)0x00010000;
	cachedsi.lpMaximumApplicationAddress	= (void *)0x7FFFFFFF;
	cachedsi.dwActiveProcessorMask		= 1;
	cachedsi.dwNumberOfProcessors		= 1;
	cachedsi.dwProcessorType		= PROCESSOR_INTEL_386;
	cachedsi.dwAllocationGranularity	= 0x10000;
	cachedsi.wProcessorLevel		= 3; /* 386 */
	cachedsi.wProcessorRevision		= 0;

	cache = 1; /* even if there is no more info, we now have a cacheentry */
	memcpy(si,&cachedsi,sizeof(*si));

	/* Hmm, reasonable processor feature defaults? */

        /* Create these registry keys for all systems
	 * FPU entry is often empty on Windows, so we don't care either */
	if ( (RegCreateKeyA(HKEY_LOCAL_MACHINE,"HARDWARE\\DESCRIPTION\\System\\FloatingPointProcessor",&hkey)!=ERROR_SUCCESS)
	  || (RegCreateKeyA(HKEY_LOCAL_MACHINE,"HARDWARE\\DESCRIPTION\\System\\CentralProcessor",&hkey)!=ERROR_SUCCESS) )
	{
            WARN("Unable to write FPU/CPU info to registry\n");
        }

#ifdef linux
	{
	char buf[20];
	char line[200];
	FILE *f = fopen ("/proc/cpuinfo", "r");

	if (!f)
		return;
        xhkey = 0;
	while (fgets(line,200,f)!=NULL) {
		char	*s,*value;

		/* NOTE: the ':' is the only character we can rely on */
		if (!(value = strchr(line,':')))
			continue;
		/* terminate the valuename */
		*value++ = '\0';
		/* skip any leading spaces */
		while (*value==' ') value++;
		if ((s=strchr(value,'\n')))
			*s='\0';

		/* 2.1 method */
		if (!strncasecmp(line, "cpu family",strlen("cpu family"))) {
			if (isdigit (value[0])) {
				switch (value[0] - '0') {
				case 3: cachedsi.dwProcessorType = PROCESSOR_INTEL_386;
					cachedsi.wProcessorLevel= 3;
					break;
				case 4: cachedsi.dwProcessorType = PROCESSOR_INTEL_486;
					cachedsi.wProcessorLevel= 4;
					break;
				case 5:
				case 6: /* PPro/2/3 has same info as P1 */
					cachedsi.dwProcessorType = PROCESSOR_INTEL_PENTIUM;
					cachedsi.wProcessorLevel= 5;
					break;
				case 1: /* two-figure levels */
                                    if (value[1] == '5')
                                    {
                                        cachedsi.dwProcessorType = PROCESSOR_INTEL_PENTIUM;
                                        cachedsi.wProcessorLevel= 5;
                                        break;
                                    }
                                    /* fall through */
				default:
					FIXME("unknown cpu family '%s', please report ! (-> setting to 386)\n", value);
					break;
				}
			}
			/* set the CPU type of the current processor */
			sprintf(buf,"CPU %ld",cachedsi.dwProcessorType);
			if (xhkey)
				RegSetValueExA(xhkey,"Identifier",0,REG_SZ,buf,strlen(buf));
			continue;
		}
		/* old 2.0 method */
		if (!strncasecmp(line, "cpu",strlen("cpu"))) {
			if (	isdigit (value[0]) && value[1] == '8' && 
				value[2] == '6' && value[3] == 0
			) {
				switch (value[0] - '0') {
				case 3: cachedsi.dwProcessorType = PROCESSOR_INTEL_386;
					cachedsi.wProcessorLevel= 3;
					break;
				case 4: cachedsi.dwProcessorType = PROCESSOR_INTEL_486;
					cachedsi.wProcessorLevel= 4;
					break;
				case 5:
				case 6: /* PPro/2/3 has same info as P1 */
					cachedsi.dwProcessorType = PROCESSOR_INTEL_PENTIUM;
					cachedsi.wProcessorLevel= 5;
					break;
				default:
					FIXME("unknown Linux 2.0 cpu family '%s', please report ! (-> setting to 386)\n", value);
					break;
				}
			}
			/* set the CPU type of the current processor
			 * FIXME: someone reported P4 as being set to
			 * "              Intel(R) Pentium(R) 4 CPU 1500MHz"
			 * Do we need to do the same ?
			 * */
			sprintf(buf,"CPU %ld",cachedsi.dwProcessorType);
			if (xhkey)
				RegSetValueExA(xhkey,"Identifier",0,REG_SZ,buf,strlen(buf));
			continue;
		}
		if (!strncasecmp(line,"fdiv_bug",strlen("fdiv_bug"))) {
			if (!strncasecmp(value,"yes",3))
				PF[PF_FLOATING_POINT_PRECISION_ERRATA] = TRUE;

			continue;
		}
		if (!strncasecmp(line,"fpu",strlen("fpu"))) {
			if (!strncasecmp(value,"no",2))
				PF[PF_FLOATING_POINT_EMULATED] = TRUE;

			continue;
		}
		if (!strncasecmp(line,"processor",strlen("processor"))) {
			/* processor number counts up... */
			unsigned int x;

			if (sscanf(value,"%d",&x))
				if (x+1>cachedsi.dwNumberOfProcessors)
					cachedsi.dwNumberOfProcessors=x+1;

			/* Create a new processor subkey on a multiprocessor
			 * system
			 */
			sprintf(buf,"%d",x);
			if (xhkey)
				RegCloseKey(xhkey);
			RegCreateKeyA(hkey,buf,&xhkey);
		}
		if (!strncasecmp(line,"stepping",strlen("stepping"))) {
			int	x;

			if (sscanf(value,"%d",&x))
				cachedsi.wProcessorRevision = x;
		}
		if (	!strncasecmp(line,"flags",strlen("flags"))	||
			!strncasecmp(line,"features",strlen("features"))
		) {
			if (strstr(value,"cx8"))
				PF[PF_COMPARE_EXCHANGE_DOUBLE] = TRUE;
			if (strstr(value,"mmx"))
				PF[PF_MMX_INSTRUCTIONS_AVAILABLE] = TRUE;
			if (strstr(value,"tsc"))
				PF[PF_RDTSC_INSTRUCTION_AVAILABLE] = TRUE;

		}
	}
	fclose (f);
	}
	memcpy(si,&cachedsi,sizeof(*si));
#else  /* linux */
	/* FIXME: how do we do this on other systems? */

	RegCreateKeyA(hkey,"0",&xhkey);
	RegSetValueExA(xhkey,"Identifier",0,REG_SZ,"CPU 386",strlen("CPU 386"));
#endif  /* !linux */
	if (xhkey)
		RegCloseKey(xhkey);
	if (hkey)
		RegCloseKey(hkey);
}


/***********************************************************************
 * 			IsProcessorFeaturePresent	[KERNEL32.@]
 * RETURNS:
 *	TRUE if processor feature present
 *	FALSE otherwise
 */
BOOL WINAPI IsProcessorFeaturePresent (
	DWORD feature	/* [in] feature number, see PF_ defines */
) {
  SYSTEM_INFO si;
  GetSystemInfo (&si); /* To ensure the information is loaded and cached */

  if (feature < 64)
    return PF[feature];
  else
    return FALSE;
}
