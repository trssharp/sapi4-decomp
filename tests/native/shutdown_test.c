/* Dynamic native x64 shutdown contracts. No playback or original DLL loading. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "dynamic_api.h"
static int checks,failures;
#define CHECK(x,label) do {int checked_value=!!(x);checks++;failures+=!checked_value;printf("%s %s\n",checked_value?"ok":"FAIL",label);fflush(stdout);}while(0)
typedef struct Audio {unsigned char *data;size_t size;int callback_reject;HANDLE entered,release;s4_engine *engine;} Audio;
static int pcm(const int16_t*p,size_t n,void*u) {
 Audio*a=u;
 unsigned char*q=realloc(a->data,a->size+n*2);if(!q)return 1;
 a->data=q;memcpy(q+a->size,p,n*2);a->size+=n*2;
 if(a->callback_reject){CHECK(s4_shutdown()==-1,"same-thread callback shutdown rejects without deadlock");a->callback_reject=0;}
 if(a->entered){SetEvent(a->entered);WaitForSingleObject(a->release,10000);}
 return 0;
}
static int talking(s4_engine*e,Audio*a){return s4_speak(e,"Explicit shutdown preserves this exact audio.",0,pcm,a);}
static DWORD WINAPI speaking(void*u){Audio*a=u;return talking(a->engine,a);}
typedef struct Shutdown {HANDLE done;int result;} Shutdown;
static DWORD WINAPI shutting(void*u){Shutdown*s=u;s->result=s4_shutdown();SetEvent(s->done);return 0;}
static int same(Audio*a,Audio*b){return a->size && a->size==b->size && !memcmp(a->data,b->data,a->size);}
int main(int argc,char**argv) {
 if(argc<5)return 2;
 const char *dll=argv[1],*dir=argv[2],*other=argv[3],*bad=argv[4];char original[1024],second[1024],invalid[1024],err[256];
 snprintf(original,sizeof original,"%s/msttssyn.dll",dir);snprintf(second,sizeof second,"%s/msttssyn.dll",other);snprintf(invalid,sizeof invalid,"%s/msttssyn.dll",bad);
 CHECK(s4_load(dll),"resolve all18 exports dynamically");if(!s4_module)return 3;
 CHECK(s4_shutdown()==0 && s4_shutdown()==0,"cold shutdown idempotent");
 CHECK(s4_init(invalid)==-1 && s4_shutdown()==0,"failed engine-image validation cleans up");
 CHECK(s4_init(original)==0,"initialize verified data");
 s4_mode modes[22];CHECK(s4_list_modes(dir,modes,22)==22,"complete22 modes");
 Audio baseline={0};s4_engine*e=s4_open(dir,"Sam",err,sizeof err);
 CHECK(e && s4_shutdown()==-1,"live handle rejects shutdown");
 CHECK(s4_sample_rate(e)==22050 && s4_list_modes(dir,modes,22)==22,"rejection leaves getters and resources intact");
 baseline.callback_reject=1;
 CHECK(talking(e,&baseline)==0 && baseline.size>0,"handle speaks after live/callback shutdown rejection");
 s4_close(e);s4_engine*stale=e;
 CHECK(s4_shutdown()==0 && s4_shutdown()==0,"close then shutdown twice");
 CHECK(s4_sample_rate(stale)==0 && s4_list_modes(dir,modes,22)==-1,"closed borrowed mode and initialized state invalidated");
 CHECK(s4_init(second)==0 && s4_list_modes(other,modes,22)==22,"different resource directory reinitializes after shutdown");
 CHECK(s4_list_modes(dir,modes,22)==-1,"new resource directory pins independently");
 Audio again={0};e=s4_open(other,"Sam",err,sizeof err);
 CHECK(e && e!=stale && s4_sample_rate(stale)==0,"cookie generation never resets across shutdown");
 s4_close(stale);s4_stop(stale);
 CHECK(talking(e,&again)==0 && same(&baseline,&again),"same-load reinit PCM byte-exact and stale calls inert");s4_close(e);free(again.data);
 CHECK(s4_shutdown()==0 && s4_init(original)==0,"switch back to first directory after shutdown");
 CHECK(s4_list_modes(bad,modes,22)==-1,"changed resources cannot inherit prior validation cache");
 CHECK(s4_list_modes(dir,modes,22)==22,"failed resource validation leaves good resources usable");
 e=s4_open(dir,"Sam",err,sizeof err);Audio held={0};held.engine=e;held.entered=CreateEventA(NULL,TRUE,FALSE,NULL);held.release=CreateEventA(NULL,TRUE,FALSE,NULL);
 HANDLE talk=CreateThread(NULL,0,speaking,&held,0,NULL);CHECK(WaitForSingleObject(held.entered,5000)==WAIT_OBJECT_0,"active speech callback held");
 Shutdown sd={CreateEventA(NULL,TRUE,FALSE,NULL),99};HANDLE shut=CreateThread(NULL,0,shutting,&sd,0,NULL);
 CHECK(WaitForSingleObject(sd.done,100)==WAIT_TIMEOUT,"other-thread shutdown serializes behind active speech");
 SetEvent(held.release);WaitForSingleObject(talk,10000);WaitForSingleObject(shut,10000);
 DWORD result=99;GetExitCodeThread(talk,&result);
 CHECK(result==0 && sd.result==-1 && s4_sample_rate(e)==22050,"serialized shutdown rejects still-live handle without mutation");
 CloseHandle(talk);CloseHandle(shut);CloseHandle(sd.done);CloseHandle(held.entered);CloseHandle(held.release);free(held.data);s4_close(e);
 CHECK(s4_shutdown()==0,"joined threads and closed engine permit shutdown");
 int reinit_ok=1,fail_ok=1;
 for(int i=0;i<100;i++) {
  if(s4_init(invalid)!=-1 || s4_shutdown()!=0 || s4_init(original)!=0)fail_ok=0;
  e=s4_open(dir,"Sam",err,sizeof err);Audio a={0};
  if(!e || e==stale || talking(e,&a)!=0 || !same(&baseline,&a))reinit_ok=0;
  s4_close(e);free(a.data);
  if(s4_shutdown()!=0 || s4_shutdown()!=0)reinit_ok=0;
 }
 CHECK(fail_ok,"100 failed resource-init/shutdown/recovery cycles");
 CHECK(reinit_ok,"100 same-load close/shutdown/reinit cycles exact PCM and idempotence");
 if(argc>5) {
  int ok=1;
  for(int stage=1;stage<=4;stage++)for(int i=0;i<100;i++) {
   char v[40];snprintf(v,sizeof v,"S4_FAIL_STAGE=%d",stage);_putenv(v);
   int ir=s4_init(original);if(ir!=0) {fprintf(stderr,"FAULT_INIT stage=%d i=%d r=%d\n",stage,i,ir);ok=0;}
   e=s4_open(dir,"Sam",err,sizeof err);if(e) {fprintf(stderr,"FAULT_OPEN stage=%d i=%d unexpected=%p\n",stage,i,(void*)e);ok=0;s4_close(e);}
   int sr=s4_shutdown();if(sr!=0){fprintf(stderr,"FAULT_SHUTDOWN stage=%d i=%d r=%d\n",stage,i,sr);ok=0;}
   _putenv("S4_FAIL_STAGE=");
   ir=s4_init(original);if(ir!=0){fprintf(stderr,"FAULT_REINIT stage=%d i=%d r=%d\n",stage,i,ir);ok=0;}
   e=s4_open(dir,"Sam",err,sizeof err);Audio a={0};
   int speech=e?talking(e,&a):-99;
   if(!e || speech!=0 || !same(&baseline,&a)) {
    if(ok)fprintf(stderr,"FAULT_RECOVERY stage=%d cycle=%d engine=%p speech=%d pcm=%llu baseline=%llu\n",stage,i,(void*)e,speech,(unsigned long long)a.size,(unsigned long long)baseline.size);
    ok=0;
   }
   s4_close(e);free(a.data);sr=s4_shutdown();if(sr!=0){fprintf(stderr,"FAULT_FINAL stage=%d i=%d r=%d\n",stage,i,sr);ok=0;}
  }
  CHECK(ok,"400 injected partial-setup failure/shutdown/reinit PCM recovery cycles");
 }
 free(baseline.data);
 CHECK(s4_unload(),"explicit shutdown then FreeLibrary succeeds");
 CHECK(GetModuleHandleA(dll)==NULL,"actual module absent after unload");
 printf("SHUTDOWN_RESULT checks=%d failures=%d\n",checks,failures);return failures?1:0;
}
