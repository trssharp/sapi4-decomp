/* Native x64 same-process DLL lifetime probe; no playback, threads or stale calls. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "sapi4tts.h"
static int (*shutdown_api)(void);
static int (*init)(const char*);
static int (*modes)(const char*,s4_mode*,int);
static s4_engine* (*open_engine)(const char*,const char*,char*,size_t);
static void (*close_engine)(s4_engine*);
static int (*speak)(s4_engine*,const char*,unsigned,s4_pcm_fn,void*);
static unsigned long long samples;
static unsigned char *capture,*reference[22];
static size_t captured,reference_size[22];
static int pcm(const int16_t *p,size_t n,void*u) {
 (void)u;samples+=n;
 unsigned char*q=realloc(capture,captured+n*2);if(!q)exit(40);
 capture=q;memcpy(capture+captured,p,n*2);captured+=n*2;return 0;
}
static void sample(const char*phase,int i,const char*stage,const char*dll){
 PROCESS_MEMORY_COUNTERS_EX m={0};DWORD handles=0; m.cb=sizeof m;
 if(!GetProcessMemoryInfo(GetCurrentProcess(),(PROCESS_MEMORY_COUNTERS*)&m,sizeof m)||!GetProcessHandleCount(GetCurrentProcess(),&handles))exit(30);
 HANDLE hs[1024];DWORD nh=GetProcessHeaps(1024,hs);unsigned long long bytes=0,blocks=0;int errors=0;
 if(nh>1024)exit(31);
 for(DWORD j=0;j<nh;j++) {PROCESS_HEAP_ENTRY e={0};if(!HeapLock(hs[j])){errors++;continue;}
 while(HeapWalk(hs[j],&e))if(e.wFlags&PROCESS_HEAP_ENTRY_BUSY){bytes+=e.cbData;blocks++;}
 if(GetLastError()!=ERROR_NO_MORE_ITEMS)errors++;HeapUnlock(hs[j]);}
 printf("%s,%d,%s,%llu,%llu,%lu,%d,%llu,%llu,%lu,%d,%llu\n",phase,i,stage,(unsigned long long)m.PrivateUsage,(unsigned long long)m.WorkingSetSize,(unsigned long)handles,GetModuleHandleA(dll)!=NULL,bytes,blocks,(unsigned long)nh,errors,samples);fflush(stdout);
}
static HMODULE load(const char*dll){HMODULE h=LoadLibraryExA(dll,NULL,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);if(!h){fprintf(stderr,"load error %lu\n",GetLastError());exit(2);}return h;}
#define RESOLVE(v,n) do{*(FARPROC*)&v=GetProcAddress(h,n);if(!v)exit(3);}while(0)
int main(int argc,char**argv){
 if(argc!=6)return 1;const char*dll=argv[1],*dir=argv[2],*phase=argv[3];int count=atoi(argv[4]),all=atoi(argv[5]);
 int cold=!strcmp(phase,"cold"),initonly=!strcmp(phase,"init"),retained=!strcmp(phase,"retained");HMODULE h=NULL;
 char original[1024];snprintf(original,sizeof original,"%s/msttssyn.dll",dir);
 /* Prime the probe's own stdio and process metrics before the baseline. */
 puts("phase,iteration,stage,private_bytes,working_set,handles,module_present,heap_busy_bytes,heap_busy_blocks,heaps,heapwalk_errors,pcm_samples");
 sample(phase,0,"baseline",dll);
 for(int i=1;i<=count;i++){
  if(!retained||i==1) {
   h=load(dll);RESOLVE(shutdown_api,"s4_shutdown");
   if(!cold){RESOLVE(init,"s4_init");RESOLVE(modes,"s4_list_modes");RESOLVE(open_engine,"s4_open");RESOLVE(close_engine,"s4_close");RESOLVE(speak,"s4_speak");}
  }
  if(!cold && init(original))exit(4);
  if(!cold&&!initonly){s4_mode list[22];int n=modes(dir,list,22);if(n!=22)exit(5);
   for(int v=0;v<(all?n:1);v++){char err[256];const char*name=all?list[v].name:"Sam";s4_engine*e=open_engine(dir,name,err,sizeof err);if(!e){fprintf(stderr,"open %s: %s\n",name,err);exit(6);}unsigned long long before=samples;captured=0;int r=speak(e,"This is a memory lifetime test.",0,pcm,NULL);close_engine(e);if(r||samples==before)exit(7);
    if(i==1){reference[v]=malloc(captured);reference_size[v]=captured;memcpy(reference[v],capture,captured);}
    else if(captured!=reference_size[v] || memcmp(reference[v],capture,captured))exit(41);}
  }
  sample(phase,i,"closed_loaded",dll);
  if(shutdown_api()!=0 || shutdown_api()!=0)exit(11);
  sample(phase,i,"shutdown_loaded",dll);
  if(!retained){if(!FreeLibrary(h))exit(8);h=NULL;if(GetModuleHandleA(dll))exit(9);}
  sample(phase,i,retained?"retained":"unloaded",dll);
 }
 if(h){if(shutdown_api()!=0 || !FreeLibrary(h)||GetModuleHandleA(dll))return 10;sample(phase,count,"final_unloaded",dll);}
 for(int v=0;v<22;v++)free(reference[v]);free(capture);
 return 0;
}
