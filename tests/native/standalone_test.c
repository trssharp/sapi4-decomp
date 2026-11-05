/* Native x64 standalone dynamic client; no engine object/library linked. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dynamic_api.h"
_Static_assert(sizeof(void*)==8,"x64 required");
_Static_assert(sizeof(s4_mode)==144,"s4_mode ABI");
_Static_assert(sizeof(s4_limits)==24,"s4_limits ABI");
static int failures,checks;
#define CHECK(x,label) do { int yes=!!(x); checks++; printf("%s %s\n",yes?"ok":"FAIL",label); fflush(stdout); failures+=!yes; } while(0)
typedef struct Capture {
 s4_engine *engine; int16_t *pcm; size_t n; int callbacks,ret,reenter;
 HANDLE entered,proceed; DWORD thread;
} Capture;
static int pcm(const int16_t *p,size_t n,void *arg) {
 Capture *c=arg;
 if(c->thread!=GetCurrentThreadId()) c->ret=-99;
 c->callbacks++;
 int16_t *newp=realloc(c->pcm,(c->n+n)*2); if(!newp) return 1;
 c->pcm=newp; memcpy(c->pcm+c->n,p,n*2); c->n+=n;
 if(c->reenter) {
  CHECK(s4_reset(c->engine)==-1 && s4_set_volume(c->engine,50)==-1,"callback reentry rejected");
  s4_close(c->engine); c->reenter=0;
 }
 if(c->entered) { SetEvent(c->entered); WaitForSingleObject(c->proceed,5000); }
 return c->ret>0?c->ret:0;
}
static const char *longtext="One. Two. Three. Four. Five. Six. Seven. Eight. Nine. Ten. The reconstructed engine speaks again.";
static DWORD WINAPI speaking(void *u) {
 Capture *c=u; c->thread=GetCurrentThreadId();
 c->ret=s4_speak(c->engine,longtext,0,pcm,c); return 0;
}
typedef struct Closing { s4_engine *engine; HANDLE done; } Closing;
static DWORD WINAPI closing(void *u) { Closing *c=u; s4_close(c->engine); SetEvent(c->done); return 0; }
static int speak(s4_engine *e,Capture *c,const char *text) { c->engine=e; c->thread=GetCurrentThreadId(); return s4_speak(e,text,0,pcm,c); }
static int lifetime(const char *dll,const char *dir) {
 CHECK(s4_load(dll),"fresh DLL load for lifetime speech boundary");
 char path[1024],err[256]; snprintf(path,sizeof path,"%s/msttssyn.dll",dir);
 CHECK(s4_init(path)==0,"fresh lifetime init");

 s4_engine *first=s4_open(dir,"Sam",err,sizeof err),*last=NULL,*stale[124];
 Capture baseline={0},final={0},again={0};
 CHECK(first && speak(first,&baseline,"Hello")==0 && baseline.n>0,"lifetime first handle PCM baseline");
 int opened=1;
 for(int i=0;i<124;i++) { stale[i]=s4_open(dir,"Sam",err,sizeof err); if(stale[i]) opened++; s4_close(stale[i]); }
 last=s4_open(dir,"Sam",err,sizeof err); if(last) opened++;
 CHECK(opened==126,"exactly126 successful core opens");
 int nonzero=0,ret=speak(last,&final,"Hello"); for(size_t i=0;i<final.n;i++) nonzero|=final.pcm[i]!=0;
 CHECK(ret==0 && nonzero && final.n==baseline.n && !memcmp(final.pcm,baseline.pcm,final.n*2),"slot126 nonzero PCM byte-exact to first baseline");
 s4_engine *reused=s4_open(dir,"Sam",err,sizeof err);
 CHECK(reused && s4_sample_rate(stale[0])==0,"open127 succeeds; recycled slot rejects original stale cookie");
 s4_close(reused);
 int inactive=1; Capture dead={0};
 for(int i=0;i<124;i++) if(s4_mode_info(stale[i]) || speak(stale[i],&dead,"Hello")!=-1) inactive=0;
 CHECK(inactive && !dead.callbacks,"all124 closed handles inactive at exhaustion");
 CHECK(speak(first,&again,"Hello")==0 && again.n>0,"first handle still synthesizes after exhaustion");
 s4_close(last); s4_close(first);
 reused=s4_open(dir,"Sam",err,sizeof err);
 CHECK(reused && reused!=first && reused!=last,"close restores live capacity with fresh cookie");
 s4_close(reused);
 free(baseline.pcm); free(final.pcm); free(again.pcm);
 CHECK(s4_unload(),"clean lifetime DLL unload"); s4_module=NULL;
 return failures?1:0;
}
int main(int argc,char **argv) {

 if(argc!=3) { fprintf(stderr,"usage: standalone_test.exe <absolute reconstructed DLL> <absolute resource dir>\n"); return 2; }
 CHECK(s4_load(argv[1]),"LoadLibraryExW reconstructed x64 DLL and all exports"); if(!s4_module) return 2;
 CHECK(s4_abi_version()==S4_ABI_VERSION,"ABI version / native 64-bit client");
 char datafile[1024]; snprintf(datafile,sizeof datafile,"%s/msttssyn.dll",argv[2]);
 CHECK(s4_init("C:/this-path-does-not-exist/msttssyn.dll")==-1,"bad initial DLL path rejected");
 CHECK(s4_init(datafile)==0 && s4_init(datafile)==0,"verified engine data init idempotent");
 s4_mode modes[64]; int n=s4_list_modes(argv[2],modes,64);
 CHECK(n==22 && s4_list_modes(argv[2],NULL,0)==22,"22 modes / count-only enumeration");
 CHECK(s4_list_modes(argv[2],NULL,1)==-1 && s4_list_modes(argv[2],modes,-1)==-1,"invalid enumeration arguments rejected");
 char err[256];
 CHECK(!s4_open("C:/missing","Sam",err,sizeof err) && err[0],"bad resource directory rejected with error");
 CHECK(!s4_open(argv[2],"No Such Voice",err,sizeof err) && err[0],"unknown voice rejected with error");
 CHECK(!s4_open(argv[2],NULL,err,sizeof err),"NULL voice rejected");
 s4_engine *e=s4_open(argv[2],"Sam",err,sizeof err);
 CHECK(e!=NULL,"open Sam"); if(!e) return 3;
 CHECK(s4_mode_info(e) && strcmp(s4_mode_info(e)->name,"Sam")==0 && s4_sample_rate(e)==22050,"borrowed mode info and sample rate");
 s4_limits lim; s4_get_limits(e,&lim);
 CHECK(s4_get_pitch(e)==lim.pitch_default && s4_get_speed(e)==lim.speed_default && s4_get_volume(e)==100,"native defaults / software volume100");
 CHECK(s4_set_pitch(e,0)==0 && s4_get_pitch(e)==lim.pitch_min && s4_set_pitch(e,~0u)==0 && s4_get_pitch(e)==lim.pitch_max,"pitch clamps to native limits");
 CHECK(s4_set_speed(e,0)==0 && s4_get_speed(e)==lim.speed_min && s4_set_speed(e,~0u)==0 && s4_get_speed(e)==lim.speed_max,"speed clamps to native limits");
 CHECK(s4_set_volume(e,101)==-1 && s4_get_volume(e)==100,"invalid volume rejected without mutation");
 CHECK(s4_reset(e)==0 && s4_reset(e)==0 && s4_get_pitch(e)==lim.pitch_default && s4_get_speed(e)==lim.speed_default,"reset idempotent restores native defaults");
 Capture c={0}; c.reenter=1;
 CHECK(speak(e,&c,"Hello. Native dynamic library test.")==0 && c.n>0,"real callback PCM / reentry guard");
 size_t first=c.n; free(c.pcm); memset(&c,0,sizeof c);
 CHECK(speak(e,&c,"Hello. Native dynamic library test.")==0 && c.n==first,"repeated speech / stable frame count"); free(c.pcm);
 Capture cancel={0}; cancel.ret=1;
 CHECK(speak(e,&cancel,longtext)==1 && cancel.n>0,"callback return cancels speech"); free(cancel.pcm);
 s4_stop(e); s4_stop(e); memset(&c,0,sizeof c);
 CHECK(speak(e,&c,"Recovery.")==0 && c.n>0,"recovery after cancel / repeated idle stop"); free(c.pcm);
 CHECK(s4_speak(e,NULL,0,pcm,&c)==-1 && s4_speak(e,"Hi",S4_TAGGED,pcm,&c)==-1 && s4_speak(e,"Hi",0,NULL,NULL)==-1,"invalid text flags callback rejected");
 CHECK(s4_speak(NULL,"Hi",0,pcm,&c)==-1 && s4_reset((s4_engine*)1)==-1,"NULL and unknown handle rejected");
 Capture async={0}; async.engine=e; async.entered=CreateEventW(NULL,TRUE,FALSE,NULL); async.proceed=CreateEventW(NULL,TRUE,FALSE,NULL);
 HANDLE th=CreateThread(NULL,0,speaking,&async,0,NULL);
 CHECK(WaitForSingleObject(async.entered,5000)==WAIT_OBJECT_0,"active speech callback handshake");
 s4_stop(e); s4_stop(e); SetEvent(async.proceed);
 CHECK(WaitForSingleObject(th,5000)==WAIT_OBJECT_0 && async.ret==1 && async.n>0,"cross-thread cancel bounded / returns1");
 CloseHandle(th); CloseHandle(async.entered); CloseHandle(async.proceed); free(async.pcm);
 memset(&c,0,sizeof c); CHECK(speak(e,&c,"Recovery after threaded cancel.")==0 && c.n>0,"speech recovery after threaded cancel"); free(c.pcm);
 /* Compare exact software scaling using two fresh engines with identical RNG starts. */
 s4_engine *a=s4_open(argv[2],"Sam",NULL,0),*b=s4_open(argv[2],"Sam",NULL,0); Capture ca={0},cb={0};
 s4_set_volume(b,50); int ra=speak(a,&ca,"Volume test."),rb=speak(b,&cb,"Volume test.");
 int scaled=ra==0 && rb==0 && ca.n==cb.n && ca.n>0;
 for(size_t i=0;scaled && i<ca.n;i++) if(cb.pcm[i]!=(int16_t)((int32_t)ca.pcm[i]*50/100)) scaled=0;
 CHECK(scaled,"volume50 exact signed integer PCM scaling"); free(ca.pcm); free(cb.pcm);
 s4_set_volume(b,0); memset(&cb,0,sizeof cb); speak(b,&cb,"Muted."); int zero=cb.n>0;
 for(size_t i=0;i<cb.n;i++) if(cb.pcm[i]) zero=0;
 CHECK(zero && s4_reset(b)==0 && s4_get_volume(b)==100,"volume0 / reset volume100"); free(cb.pcm); s4_close(a); s4_close(b);
 /* Close must wait for active callback; after it returns no callbacks may fire. */
 memset(&async,0,sizeof async); async.engine=e; async.entered=CreateEventW(NULL,TRUE,FALSE,NULL); async.proceed=CreateEventW(NULL,TRUE,FALSE,NULL);
 th=CreateThread(NULL,0,speaking,&async,0,NULL);
 CHECK(WaitForSingleObject(async.entered,5000)==WAIT_OBJECT_0,"close during speech handshake");
 Closing cl={e,CreateEventW(NULL,TRUE,FALSE,NULL)}; HANDLE ct=CreateThread(NULL,0,closing,&cl,0,NULL);
 CHECK(WaitForSingleObject(cl.done,50)==WAIT_TIMEOUT,"close waits while callback active");
 s4_stop(e); SetEvent(async.proceed);
 CHECK(WaitForSingleObject(th,5000)==WAIT_OBJECT_0 && WaitForSingleObject(ct,5000)==WAIT_OBJECT_0,"cancel then close bounded completion");
 int before=async.callbacks; Sleep(50);
 CHECK(async.callbacks==before && s4_mode_info(e)==NULL && s4_reset(e)==-1,"no callback after close / stale handle rejected");
 s4_close(e); s4_close(NULL); s4_stop(e); s4_stop(NULL);
 CHECK(s4_get_pitch(e)==0 && s4_sample_rate(e)==0,"repeated close and stale stop safe");
 CloseHandle(th); CloseHandle(ct); CloseHandle(cl.done); CloseHandle(async.entered); CloseHandle(async.proceed); free(async.pcm);
 CHECK(GetModuleHandleW(L"msttssyn.dll")==NULL,"original engine DLL never executable-loaded");
 CHECK(s4_unload(),"clean reconstructed DLL unload after joined threads"); s4_module=NULL;
 lifetime(argv[1],argv[2]);
 printf("RESULT checks=%d failures=%d\n",checks,failures); return failures?1:0;
}
