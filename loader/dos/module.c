/*
 * DOS (MZ) loader
 *
 * Copyright 1998 Ove K�ven
 *
 * This code hasn't been completely cleaned up yet.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "windef.h"
#include "wine/winbase16.h"
#include "winerror.h"
#include "module.h"
#include "neexe.h"
#include "task.h"
#include "selectors.h"
#include "file.h"
#include "ldt.h"
#include "process.h"
#include "miscemu.h"
#include "debugtools.h"
#include "dosexe.h"
#include "dosmod.h"
#include "options.h"
#include "vga.h"

DEFAULT_DEBUG_CHANNEL(module);

#ifdef MZ_SUPPORTED

#ifdef HAVE_SYS_MMAN_H
# include <sys/mman.h>
#endif

/* define this to try mapping through /proc/pid/mem instead of a temp file,
   but Linus doesn't like mmapping /proc/pid/mem, so it doesn't work for me */
#undef MZ_MAPSELF

#define BIOS_DATA_SEGMENT 0x40
#define START_OFFSET 0
#define PSP_SIZE 0x10

#define SEG16(ptr,seg) ((LPVOID)((BYTE*)ptr+((DWORD)(seg)<<4)))
#define SEGPTR16(ptr,segptr) ((LPVOID)((BYTE*)ptr+((DWORD)SELECTOROF(segptr)<<4)+OFFSETOF(segptr)))

static LPDOSTASK dos_current;

static void MZ_Launch(void);

static void MZ_CreatePSP( LPVOID lpPSP, WORD env )
{
 PDB16*psp=lpPSP;

 psp->int20=0x20CD; /* int 20 */
 /* some programs use this to calculate how much memory they need */
 psp->nextParagraph=0x9FFF;
 psp->environment=env;
 /* FIXME: more PSP stuff */
}

static void MZ_FillPSP( LPVOID lpPSP, LPCSTR cmdline )
{
 PDB16*psp=lpPSP;
 const char*cmd=cmdline?strchr(cmdline,' '):NULL;

 /* copy parameters */
 if (cmd) {
#if 0
  /* command.com doesn't do this */
  while (*cmd == ' ') cmd++;
#endif
  psp->cmdLine[0]=strlen(cmd);
  strcpy(psp->cmdLine+1,cmd);
  psp->cmdLine[psp->cmdLine[0]+1]='\r';
 } else psp->cmdLine[1]='\r';
 /* FIXME: more PSP stuff */
}

/* default INT 08 handler: increases timer tick counter but not much more */
static char int08[]={
 0xCD,0x1C,           /* int $0x1c */
 0x50,                /* pushw %ax */
 0x1E,                /* pushw %ds */
 0xB8,0x40,0x00,      /* movw $0x40,%ax */
 0x8E,0xD8,           /* movw %ax,%ds */
#if 0
 0x83,0x06,0x6C,0x00,0x01, /* addw $1,(0x6c) */
 0x83,0x16,0x6E,0x00,0x00, /* adcw $0,(0x6e) */
#else
 0x66,0xFF,0x06,0x6C,0x00, /* incl (0x6c) */
#endif
 0xB0,0x20,           /* movb $0x20,%al */
 0xE6,0x20,           /* outb %al,$0x20 */
 0x1F,                /* popw %ax */
 0x58,                /* popw %ax */
 0xCF                 /* iret */
};

static void MZ_InitHandlers( LPDOSTASK lpDosTask )
{
 WORD seg;
 LPBYTE start=DOSMEM_GetBlock(sizeof(int08),&seg);
 memcpy(start,int08,sizeof(int08));
/* INT 08: point it at our tick-incrementing handler */
 ((SEGPTR*)(lpDosTask->img))[0x08]=PTR_SEG_OFF_TO_SEGPTR(seg,0);
/* INT 1C: just point it to IRET, we don't want to handle it ourselves */
 ((SEGPTR*)(lpDosTask->img))[0x1C]=PTR_SEG_OFF_TO_SEGPTR(seg,sizeof(int08)-1);
}

static char enter_xms[]={
/* XMS hookable entry point */
 0xEB,0x03,           /* jmp entry */
 0x90,0x90,0x90,      /* nop;nop;nop */
                      /* entry: */
/* real entry point */
/* for simplicity, we'll just use the same hook as DPMI below */
 0xCD,0x31,           /* int $0x31 */
 0xCB                 /* lret */
};

static void MZ_InitXMS( LPDOSTASK lpDosTask )
{
 LPBYTE start=DOSMEM_GetBlock(sizeof(enter_xms),&(lpDosTask->xms_seg));
 memcpy(start,enter_xms,sizeof(enter_xms));
}

static char enter_pm[]={
 0x50,                /* pushw %ax */
 0x52,                /* pushw %dx */
 0x55,                /* pushw %bp */
 0x89,0xE5,           /* movw %sp,%bp */
/* get return CS */
 0x8B,0x56,0x08,      /* movw 8(%bp),%dx */
/* just call int 31 here to get into protected mode... */
/* it'll check whether it was called from dpmi_seg... */
 0xCD,0x31,           /* int $0x31 */
/* we are now in the context of a 16-bit relay call */
/* need to fixup our stack;
 * 16-bit relay return address will be lost, but we won't worry quite yet */
 0x8E,0xD0,           /* movw %ax,%ss */
 0x66,0x0F,0xB7,0xE5, /* movzwl %bp,%esp */
/* set return CS */
 0x89,0x56,0x08,      /* movw %dx,8(%bp) */
 0x5D,                /* popw %bp */
 0x5A,                /* popw %dx */
 0x58,                /* popw %ax */
 0xCB                 /* lret */
};

static void MZ_InitDPMI( LPDOSTASK lpDosTask )
{
 unsigned size=sizeof(enter_pm);
 LPBYTE start=DOSMEM_GetBlock(size,&(lpDosTask->dpmi_seg));
 
 lpDosTask->dpmi_sel = SELECTOR_AllocBlock( start, size, SEGMENT_CODE, FALSE, FALSE );

 memcpy(start,enter_pm,sizeof(enter_pm));
}

static WORD MZ_InitEnvironment( LPDOSTASK lpDosTask, LPCSTR env, LPCSTR name )
{
 unsigned sz=0;
 WORD seg;
 LPSTR envblk;

 if (env) {
  /* get size of environment block */
  while (env[sz++]) sz+=strlen(env+sz)+1;
 } else sz++;
 /* allocate it */
 envblk=DOSMEM_GetBlock(sz+sizeof(WORD)+strlen(name)+1,&seg);
 /* fill it */
 if (env) {
  memcpy(envblk,env,sz);
 } else envblk[0]=0;
 /* DOS 3.x: the block contains 1 additional string */
 *(WORD*)(envblk+sz)=1;
 /* being the program name itself */
 strcpy(envblk+sz+sizeof(WORD),name);
 return seg;
}

static BOOL MZ_InitMemory( LPDOSTASK lpDosTask )
{
 if (lpDosTask->img) return TRUE; /* already allocated */

 /* allocate 1MB+64K shared memory */
 lpDosTask->img_ofs=START_OFFSET;
#ifdef MZ_MAPSELF
 lpDosTask->img=VirtualAlloc(NULL,0x110000,MEM_COMMIT,PAGE_READWRITE);
 /* make sure mmap accepts it */
 ((char*)lpDosTask->img)[0x10FFFF]=0;
#else
 tmpnam(lpDosTask->mm_name);
/* strcpy(lpDosTask->mm_name,"/tmp/mydosimage"); */
 lpDosTask->mm_fd=open(lpDosTask->mm_name,O_RDWR|O_CREAT /* |O_TRUNC */,S_IRUSR|S_IWUSR);
 if (lpDosTask->mm_fd<0) ERR("file %s could not be opened\n",lpDosTask->mm_name);
 /* expand file to 1MB+64K */
 ftruncate(lpDosTask->mm_fd,0x110000);
 /* map it in */
 lpDosTask->img=mmap(NULL,0x110000-START_OFFSET,PROT_READ|PROT_WRITE,MAP_SHARED,lpDosTask->mm_fd,0);
#endif
 if (lpDosTask->img==(LPVOID)-1) {
  ERR("could not map shared memory, error=%s\n",strerror(errno));
  return FALSE;
 }
 TRACE("DOS VM86 image mapped at %08lx\n",(DWORD)lpDosTask->img);

 /* initialize the memory */
 TRACE("Initializing DOS memory structures\n");
 DOSMEM_Init(TRUE);
 MZ_InitHandlers(lpDosTask);
 MZ_InitXMS(lpDosTask);
 MZ_InitDPMI(lpDosTask);
 return TRUE;
}

BOOL MZ_LoadImage( HMODULE module, HANDLE hFile, LPCSTR filename )
{
  LPDOSTASK lpDosTask = dos_current;
  IMAGE_NT_HEADERS *win_hdr = PE_HEADER(module);
  IMAGE_DOS_HEADER mz_header;
  DWORD image_start,image_size,min_size,max_size,avail;
  BYTE*psp_start,*load_start;
  int x, old_com=0, alloc=0;
  SEGPTR reloc;
  WORD env_seg;
  DWORD len;

  win_hdr->OptionalHeader.Subsystem = IMAGE_SUBSYSTEM_WINDOWS_CUI;
  win_hdr->OptionalHeader.AddressOfEntryPoint = (LPBYTE)MZ_Launch - (LPBYTE)module;

  if (!lpDosTask) {
    alloc=1;
    lpDosTask = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(DOSTASK));
    lpDosTask->mm_fd = -1;
    dos_current = lpDosTask;
  }

 SetFilePointer(hFile,0,NULL,FILE_BEGIN);
 if (   !ReadFile(hFile,&mz_header,sizeof(mz_header),&len,NULL)
     || len != sizeof(mz_header) 
     || mz_header.e_magic != IMAGE_DOS_SIGNATURE) {
  old_com=1; /* assume .COM file */
  image_start=0;
  image_size=GetFileSize(hFile,NULL);
  min_size=0x10000; max_size=0x100000;
  mz_header.e_crlc=0;
  mz_header.e_ss=0; mz_header.e_sp=0xFFFE;
  mz_header.e_cs=0; mz_header.e_ip=0x100;
 } else {
  /* calculate load size */
  image_start=mz_header.e_cparhdr<<4;
  image_size=mz_header.e_cp<<9; /* pages are 512 bytes */
  if ((mz_header.e_cblp!=0)&&(mz_header.e_cblp!=4)) image_size-=512-mz_header.e_cblp;
  image_size-=image_start;
  min_size=image_size+((DWORD)mz_header.e_minalloc<<4)+(PSP_SIZE<<4);
  max_size=image_size+((DWORD)mz_header.e_maxalloc<<4)+(PSP_SIZE<<4);
 }

 MZ_InitMemory(lpDosTask);

 /* allocate environment block */
 env_seg=MZ_InitEnvironment(lpDosTask,GetEnvironmentStringsA(),filename);

 /* allocate memory for the executable */
 TRACE("Allocating DOS memory (min=%ld, max=%ld)\n",min_size,max_size);
 avail=DOSMEM_Available();
 if (avail<min_size) {
  ERR("insufficient DOS memory\n");
  SetLastError(ERROR_NOT_ENOUGH_MEMORY);
  goto load_error;
 }
 if (avail>max_size) avail=max_size;
 psp_start=DOSMEM_GetBlock(avail,&lpDosTask->psp_seg);
 if (!psp_start) {
  ERR("error allocating DOS memory\n");
  SetLastError(ERROR_NOT_ENOUGH_MEMORY);
  goto load_error;
 }
 lpDosTask->load_seg=lpDosTask->psp_seg+(old_com?0:PSP_SIZE);
 load_start=psp_start+(PSP_SIZE<<4);
 MZ_CreatePSP(psp_start, env_seg);

 /* load executable image */
 TRACE("loading DOS %s image, %08lx bytes\n",old_com?"COM":"EXE",image_size);
 SetFilePointer(hFile,image_start,NULL,FILE_BEGIN);
 if (!ReadFile(hFile,load_start,image_size,&len,NULL) || len != image_size) {
  SetLastError(ERROR_BAD_FORMAT);
  goto load_error;
 }

 if (mz_header.e_crlc) {
  /* load relocation table */
  TRACE("loading DOS EXE relocation table, %d entries\n",mz_header.e_crlc);
  /* FIXME: is this too slow without read buffering? */
  SetFilePointer(hFile,mz_header.e_lfarlc,NULL,FILE_BEGIN);
  for (x=0; x<mz_header.e_crlc; x++) {
   if (!ReadFile(hFile,&reloc,sizeof(reloc),&len,NULL) || len != sizeof(reloc)) {
    SetLastError(ERROR_BAD_FORMAT);
    goto load_error;
   }
   *(WORD*)SEGPTR16(load_start,reloc)+=lpDosTask->load_seg;
  }
 }

 lpDosTask->init_cs=lpDosTask->load_seg+mz_header.e_cs;
 lpDosTask->init_ip=mz_header.e_ip;
 lpDosTask->init_ss=lpDosTask->load_seg+mz_header.e_ss;
 lpDosTask->init_sp=mz_header.e_sp;

  TRACE("entry point: %04x:%04x\n",lpDosTask->init_cs,lpDosTask->init_ip);

  if (!MZ_InitTask(lpDosTask)) {
    MZ_KillTask(lpDosTask);
    SetLastError(ERROR_GEN_FAILURE);
    return FALSE;
  }

  return TRUE;

load_error:
  if (alloc) {
    dos_current = NULL;
    if (lpDosTask->mm_name[0]!=0) {
      if (lpDosTask->img!=NULL) munmap(lpDosTask->img,0x110000-START_OFFSET);
      if (lpDosTask->mm_fd>=0) close(lpDosTask->mm_fd);
      unlink(lpDosTask->mm_name);
    } else
      if (lpDosTask->img!=NULL) VirtualFree(lpDosTask->img,0x110000,MEM_RELEASE);
  }

  return FALSE;
}

LPDOSTASK MZ_AllocDPMITask( void )
{
  LPDOSTASK lpDosTask = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(DOSTASK));

  if (lpDosTask) {
    lpDosTask->mm_fd = -1;
    dos_current = lpDosTask;
    MZ_InitMemory(lpDosTask);
  }
  return lpDosTask;
}

static void MZ_InitTimer( LPDOSTASK lpDosTask, int ver )
{
 if (ver<1) {
  /* can't make timer ticks */
 } else {
  int func;
  struct timeval tim;

  /* start dosmod timer at 55ms (18.2Hz) */
  func=DOSMOD_SET_TIMER;
  tim.tv_sec=0; tim.tv_usec=54925;
  write(lpDosTask->write_pipe,&func,sizeof(func));
  write(lpDosTask->write_pipe,&tim,sizeof(tim));
 }
}

BOOL MZ_InitTask( LPDOSTASK lpDosTask )
{
  int write_fd[2],x_fd;
  pid_t child;
  char *fname,*farg,arg[16],fproc[64],path[256],*fpath;

  if (!lpDosTask) return FALSE;
  /* create pipes */
  if (!CreatePipe(&(lpDosTask->hReadPipe),&(lpDosTask->hXPipe),NULL,0)) return FALSE;
  if (pipe(write_fd)<0) {
    CloseHandle(lpDosTask->hReadPipe);
    CloseHandle(lpDosTask->hXPipe);
    return FALSE;
  }

  lpDosTask->read_pipe = FILE_GetUnixHandle( lpDosTask->hReadPipe, GENERIC_READ );
  x_fd = FILE_GetUnixHandle( lpDosTask->hXPipe, GENERIC_WRITE );

  TRACE("win32 pipe: read=%d, write=%d, unix pipe: read=%d, write=%d\n",
	       lpDosTask->hReadPipe,lpDosTask->hXPipe,lpDosTask->read_pipe,x_fd);
  TRACE("outbound unix pipe: read=%d, write=%d, pid=%d\n",write_fd[0],write_fd[1],getpid());

  lpDosTask->write_pipe=write_fd[1];

  lpDosTask->hConInput=GetStdHandle(STD_INPUT_HANDLE);
  lpDosTask->hConOutput=GetStdHandle(STD_OUTPUT_HANDLE);

  /* if we have a mapping file, use it */
  fname=lpDosTask->mm_name; farg=NULL;
  if (!fname[0]) {
    /* otherwise, map our own memory image */
    sprintf(fproc,"/proc/%d/mem",getpid());
    sprintf(arg,"%ld",(unsigned long)lpDosTask->img);
    fname=fproc; farg=arg;
  }

  TRACE("Loading DOS VM support module\n");
  if ((child=fork())<0) {
    close(write_fd[0]);
    close(lpDosTask->read_pipe);
    close(lpDosTask->write_pipe);
    close(x_fd);
    CloseHandle(lpDosTask->hReadPipe);
    CloseHandle(lpDosTask->hXPipe);
    return FALSE;
  }
 if (child!=0) {
  /* parent process */
  int ret;

  close(write_fd[0]);
  close(x_fd);
  lpDosTask->task=child;
  /* wait for child process to signal readiness */
  while (1) {
    if (read(lpDosTask->read_pipe,&ret,sizeof(ret))==sizeof(ret)) break;
    if ((errno==EINTR)||(errno==EAGAIN)) continue;
    /* failure */
    ERR("dosmod has failed to initialize\n");
    if (lpDosTask->mm_name[0]!=0) unlink(lpDosTask->mm_name);
    return FALSE;
  }
  /* the child has now mmaped the temp file, it's now safe to unlink.
   * do it here to avoid leaving a mess in /tmp if/when Wine crashes... */
  if (lpDosTask->mm_name[0]!=0) unlink(lpDosTask->mm_name);
  /* start simulated system timer */
  MZ_InitTimer(lpDosTask,ret);
  if (ret<2) {
    ERR("dosmod version too old! Please install newer dosmod properly\n");
    ERR("If you don't, the new dosmod event handling system will not work\n");
  }
  /* all systems are now go */
 } else {
  /* child process */
  close(lpDosTask->read_pipe);
  close(lpDosTask->write_pipe);
  /* put our pipes somewhere dosmod can find them */
  dup2(write_fd[0],0); /* stdin */
  dup2(x_fd,1);        /* stdout */
  /* now load dosmod */
  /* check argv[0]-derived paths first, since the newest dosmod is most likely there
   * (at least it was once for Andreas Mohr, so I decided to make it easier for him) */
  fpath=strrchr(strcpy(path,full_argv0),'/');
  if (fpath) {
   strcpy(fpath,"/dosmod");
   execl(path,fname,farg,NULL);
   strcpy(fpath,"/loader/dos/dosmod");
   execl(path,fname,farg,NULL);
  }
  /* okay, it wasn't there, try in the path */
  execlp("dosmod",fname,farg,NULL);
  /* last desperate attempts: current directory */
  execl("dosmod",fname,farg,NULL);
  /* and, just for completeness... */
  execl("loader/dos/dosmod",fname,farg,NULL);
  /* if failure, exit */
  ERR("Failed to spawn dosmod, error=%s\n",strerror(errno));
  exit(1);
 }
 return TRUE;
}

static void MZ_Launch(void)
{
  LPDOSTASK lpDosTask = MZ_Current();
  BYTE *psp_start = (BYTE*)lpDosTask->img + ((DWORD)lpDosTask->psp_seg << 4);

  MZ_FillPSP(psp_start, GetCommandLineA());

  DOSVM_Enter(NULL);
}

void MZ_KillTask( LPDOSTASK lpDosTask )
{
  DOSEVENT *event,*p_event;
  DOSSYSTEM *sys,*p_sys;

  TRACE("killing DOS task\n");
  VGA_Clean();
  if (lpDosTask->mm_name[0]!=0) {
    munmap(lpDosTask->img,0x110000-START_OFFSET);
    close(lpDosTask->mm_fd);
  } else VirtualFree(lpDosTask->img,0x110000,MEM_RELEASE);
  close(lpDosTask->read_pipe);
  close(lpDosTask->write_pipe);
  CloseHandle(lpDosTask->hReadPipe);
  CloseHandle(lpDosTask->hXPipe);
  kill(lpDosTask->task,SIGTERM);
/* free memory allocated for events and systems */
#define DFREE(var,pvar,svar) \
  var = lpDosTask->svar; \
  while (var) { \
    if (var->data) free(var->data); \
    pvar = var->next; free(var); var = pvar; \
  }

  DFREE(event,p_event,pending)
  DFREE(event,p_event,current)
  DFREE(sys,p_sys,sys)

#undef DFREE

#if 0
  /* FIXME: this seems to crash */
  if (lpDosTask->dpmi_sel)
    SELECTOR_FreeBlock(lpDosTask->dpmi_sel, 1);
#endif
}

LPDOSTASK MZ_Current( void )
{
  return dos_current;
}

#else /* !MZ_SUPPORTED */

BOOL MZ_LoadImage( HMODULE module, HANDLE hFile, LPCSTR filename )
{
 WARN("DOS executables not supported on this architecture\n");
 SetLastError(ERROR_BAD_FORMAT);
 return FALSE;
}

LPDOSTASK MZ_Current( void )
{
  return NULL;
}

#endif
