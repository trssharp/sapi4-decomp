#include "dynamic_api.h"
#include <stdio.h>
#include <psapi.h>
static void memory(int utterances){PROCESS_MEMORY_COUNTERS_EX m={0};m.cb=sizeof m;if(!GetProcessMemoryInfo(GetCurrentProcess(),(PROCESS_MEMORY_COUNTERS*)&m,sizeof m))exit(15);printf("MEM utterances=%d private=%llu\n",utterances,(unsigned long long)m.PrivateUsage);}
static HANDLE entered,release;
static int result;static size_t frames;static s4_engine *active;
static int pcm(const int16_t*p,size_t n,void*u){(void)p;(void)u;frames+=n;if(entered){SetEvent(entered);WaitForSingleObject(release,5000);}return 0;}
static DWORD WINAPI speech(void*u){(void)u;result=s4_speak(active,"Cookie reuse must not cancel this speech.",0,pcm,NULL);return 0;}
int main(int argc,char**argv){
 if(argc!=3 || !s4_load(argv[1]))return 2;
 char p[2048],err[256];snprintf(p,sizeof p,"%s/msttssyn.dll",argv[2]);if(s4_init(p))return 3;
 s4_engine *handles[126];
 for(int i=0;i<126;i++){handles[i]=s4_open(argv[2],"Sam",err,sizeof err);if(!handles[i])return 4;}
 if(s4_open(argv[2],"Sam",err,sizeof err)||!err[0])return 5;
 frames=0;if(s4_speak(handles[125],"Last live slot.",0,pcm,NULL)||!frames)return 6;
 s4_engine *stale=handles[30];s4_close(stale);
 active=handles[30]=s4_open(argv[2],"Sam",err,sizeof err);
 if(!active||active==stale||s4_set_speed(stale,100)!=-1||s4_reset(stale)!=-1)return 7;
 entered=CreateEventW(NULL,1,0,NULL);release=CreateEventW(NULL,1,0,NULL);
 HANDLE t=CreateThread(NULL,0,speech,NULL,0,NULL);
 if(WaitForSingleObject(entered,5000)!=WAIT_OBJECT_0)return 8;
 /* Different thread, stale close/stop after the registry record was reused. */
 s4_stop(stale);SetEvent(release);
 if(WaitForSingleObject(t,10000)!=WAIT_OBJECT_0||result!=0)return 9;
 CloseHandle(t);CloseHandle(entered);CloseHandle(release);entered=NULL;
 s4_close(stale);frames=0;
 if(s4_speak(active,"Still active.",0,pcm,NULL)||!frames)return 10;
 for(int i=0;i<126;i++)s4_close(handles[i]);
 active=s4_open(argv[2],"Sam",err,sizeof err);if(!active)return 11;
 memory(0);
 for(int i=0;i<1000;i++){frames=0;if(s4_speak(active,"Hi.",0,pcm,NULL)||!frames)return 12;if((i+1)%250==0)memory(i+1);}
 s4_close(active);
 puts("PASS live_capacity=126 rejects_127=1 reusable_capacity=1 threaded_stale_stop=1 stale_close=1 same_handle_pcm_utterances=1000");return 0;
}
