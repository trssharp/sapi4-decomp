/* Dynamic native lifetime RED/GREEN; no audio playback. */
#include "dynamic_api.h"
#include <psapi.h>
#include <stdint.h>
#include <stdio.h>
static size_t samples;
static int signal;
static int pcm(const int16_t *p,size_t n,void *u) { (void)u;samples+=n;for(size_t i=0;i<n;i++)signal|=p[i]!=0;return 0; }
static void memory(int phase) {
 PROCESS_MEMORY_COUNTERS_EX m={0};m.cb=sizeof m;
 if(!GetProcessMemoryInfo(GetCurrentProcess(),(PROCESS_MEMORY_COUNTERS*)&m,sizeof m)) exit(4);
 printf("MEM phase=%d private=%llu working=%llu\n",phase,(unsigned long long)m.PrivateUsage,(unsigned long long)m.WorkingSetSize);fflush(stdout);
}
int main(int argc,char **argv) {
 if(argc<3 || !s4_load(argv[1])) return 2;
 char file[2048],err[256];snprintf(file,sizeof file,"%s/msttssyn.dll",argv[2]);
 if(s4_init(file)) return 3;
 s4_mode modes[32];int n=s4_list_modes(argv[2],modes,32);if(n!=22) return 4;
 int cycles=argc>3?atoi(argv[3]):1000;
 s4_engine *stale=NULL;
 unsigned pcm_voice_mask=0;int pcm_utterances=0;
 memory(0);
 for(int i=0;i<cycles;i++) {
  s4_engine *h=s4_open(argv[2],modes[argc>4?i%n:0].name,err,sizeof err);
  if(!h) {printf("FAIL open cycle=%d error=%s\n",i+1,err);return 5;}
  if(stale && (stale==h || s4_sample_rate(stale)!=0 || s4_speak(stale,"Hi.",0,pcm,NULL)!=-1)) return 6;
  if(i<3 || (i>=120 && i<=135) || i%25==0 || i==cycles-1) {
   samples=0;signal=0;if(s4_speak(h,"Hello.",0,pcm,NULL) || !samples || !signal) return 7;
   pcm_voice_mask|=1u<<(argc>4?i%n:0);pcm_utterances++;
  }
  s4_close(h);s4_close(h);s4_stop(h);
  if(s4_sample_rate(h)!=0) return 8;
  stale=h;
  if(i==24 || i==99 || i==125 || i==126 || (i+1)%100==0) memory(i+1);
 }
 /* Failed mode lookups must neither exhaust nor leak. */
 for(int i=0;i<1000;i++) if(s4_open(argv[2],"nonexistent mode",err,sizeof err)) return 9;
 memory(-1);
 s4_engine *a=s4_open(argv[2],modes[0].name,err,sizeof err);
 s4_engine *b=s4_open(argv[2],modes[1].name,err,sizeof err);
 if(!a || !b) return 10;
 samples=0;if(s4_speak(a,"First.",0,pcm,NULL) || !samples) return 11;
 s4_close(a);samples=0;
 if(s4_speak(b,"Still alive.",0,pcm,NULL) || !samples) return 12;
 s4_close(b);memory(-2);
 if(argc>4 && pcm_voice_mask!=((1u<<n)-1))return 13;
 printf("PASS cycles=%d failed_opens=1000 stale_rejected=1 multi_engine=1 nonzero_pcm_utterances=%d pcm_voice_mask=%x\n",cycles,pcm_utterances,pcm_voice_mask);return 0;
}
