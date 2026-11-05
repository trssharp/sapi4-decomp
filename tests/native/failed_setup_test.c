#include "dynamic_api.h"
#include <psapi.h>
#include <stdio.h>
static size_t frames;
static int pcm(const int16_t *p,size_t n,void*u){(void)p;(void)u;frames+=n;return 0;}
static void mem(int stage){PROCESS_MEMORY_COUNTERS_EX m={0};m.cb=sizeof m;GetProcessMemoryInfo(GetCurrentProcess(),(PROCESS_MEMORY_COUNTERS*)&m,sizeof m);printf("MEM stage=%d private=%llu\n",stage,(unsigned long long)m.PrivateUsage);}
int main(int argc,char **argv){
 if(argc!=3 || !s4_load(argv[1]))return 2;
 char p[2048],err[256];snprintf(p,sizeof p,"%s/msttssyn.dll",argv[2]);if(s4_init(p))return 3;
 s4_engine *keep=s4_open(argv[2],"Sam",err,sizeof err);if(!keep)return 4;
 mem(0);
 for(int stage=1;stage<=4;stage++){
  snprintf(p,sizeof p,"S4_FAIL_STAGE=%d",stage);_putenv(p);
  for(int i=0;i<250;i++) if(s4_open(argv[2],"Mike",err,sizeof err) || !strstr(err,"set-up failed")){fprintf(stderr,"fault not observed stage=%d %s\n",stage,err);return 5;}
  _putenv("S4_FAIL_STAGE=");
  s4_engine *h=s4_open(argv[2],"Mike",err,sizeof err);if(!h)return 6;
  frames=0;if(s4_speak(h,"Recover.",0,pcm,NULL)||!frames)return 7;s4_close(h);
  frames=0;if(s4_speak(keep,"Still here.",0,pcm,NULL)||!frames)return 8;
  mem(stage);
 }
 s4_close(keep);mem(5);puts("PASS injected_setup_failures=1000 stages=4 retained_engine_survives=1");return 0;
}
