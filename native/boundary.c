/* Additive preview boundary; all synthesis remains upstream reconstructed C. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "sapi4tts.h"
#include "resource_hashes.h"
/* The upstream translation unit alone is compiled with renamed symbols. */
extern int core_s4_init(const char *);
extern int core_s4_list_modes(const char *,s4_mode *,int);
extern s4_engine *core_s4_open(const char *,const char *,char *,size_t);
extern void core_s4_close(s4_engine *);
extern const s4_mode *core_s4_mode_info(const s4_engine *);
extern int core_s4_sample_rate(const s4_engine *);
extern void core_s4_get_limits(const s4_engine *,s4_limits *);
extern unsigned core_s4_get_pitch(const s4_engine *),core_s4_get_speed(const s4_engine *);
extern int core_s4_set_pitch(s4_engine *,unsigned),core_s4_set_speed(s4_engine *,unsigned);
extern int core_s4_speak(s4_engine *,const char *,unsigned,s4_pcm_fn,void *);
extern void core_s4_stop(s4_engine *);
extern int core_s4_reset(s4_engine *);
struct s4_engine { s4_engine *core; s4_mode mode; unsigned volume; uintptr_t cookie; };
/* Finite LIVE capacity (126 private + one shared word), not lifetime opens.
 * Opaque aligned integer cookies are never dereferenced. Registry records reuse;
 * cookies never reuse within this DLL load. Fail closed before integer overflow. */
#define S4_LIVE_LIMIT 126
static struct s4_engine handles[S4_LIVE_LIMIT];
static uintptr_t next_cookie=16;
static int initialized;
static SRWLOCK calls=SRWLOCK_INIT,registry=SRWLOCK_INIT;
static _Thread_local int in_speech;
static char pinned_dir[240];
static s4_engine *find(const s4_engine *h) {
 for(int i=0;i<S4_LIVE_LIMIT;i++) if((uintptr_t)h==handles[i].cookie && handles[i].core) return &handles[i];
 return NULL;
}
static int enter(void) {
 if(in_speech) return 0;
 AcquireSRWLockExclusive(&calls); return 1;
}
static void leave(void) { ReleaseSRWLockExclusive(&calls); }
/* Normalize without process-global current-directory changes. Unicode aliases
 * are optional filesystem facilities, not a promise of arbitrary Unicode I/O. */
static int path_ascii(const char *p,char out[240]) {
 if(!p || !p[0] || !p[1] || !p[2] || p[1]!=':' || (p[2]!='/' && p[2]!='\\')) return 0;
 wchar_t w[32768],full[32768],shortw[32768];
 if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,p,-1,w,32768)) return 0;
 DWORD n=GetFullPathNameW(w,32768,full,NULL); if(!n || n>=32768) return 0;
 int ascii=1; for(DWORD i=0;i<n;i++) if(full[i]>127) ascii=0;
 wchar_t *use=full;
 if(!ascii) { n=GetShortPathNameW(full,shortw,32768); if(!n || n>=32768) return 0; use=shortw; }
 if(n>=239) return 0;
 for(DWORD i=0;i<n;i++) { if(use[i]>127) return 0; out[i]=(char)use[i]; }
 while(n>3 && (out[n-1]=='\\' || out[n-1]=='/')) n--;
 out[n]=0; return 1;
}
static int hash_is(const char *path,const char *expected) {
 FILE *f=fopen(path,"rb"); if(!f) return 0;
 if(fseek(f,0,SEEK_END)) { fclose(f); return 0; }
 long n=ftell(f); if(n<1 || n>50000000 || fseek(f,0,SEEK_SET)) { fclose(f); return 0; }
 unsigned char *data=malloc((size_t)n); if(!data) { fclose(f); return 0; }
 int ok=fread(data,1,(size_t)n,f)==(size_t)n; fclose(f);
 BCRYPT_ALG_HANDLE alg=NULL; unsigned char hash[32]; char hex[65];
 if(ok) ok=BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,NULL,0)>=0;
 if(ok) ok=BCryptHash(alg,NULL,0,data,(ULONG)n,hash,32)>=0;
 if(alg) BCryptCloseAlgorithmProvider(alg,0);
 free(data); if(!ok) return 0;
 for(int i=0;i<32;i++) sprintf(hex+i*2,"%02x",hash[i]);
 return strcmp(hex,expected)==0;
}
static int voices(const char *dir,char p[240]) {
 if(!initialized || !path_ascii(dir,p)) return 0;
 if(pinned_dir[0]) return _stricmp(p,pinned_dir)==0;
 char file[300];
 for(size_t i=0;i<sizeof(resources)/sizeof(resources[0]);i++) {
  snprintf(file,sizeof(file),"%s\\%s",p,resources[i].name);
  if(!hash_is(file,resources[i].sha)) return 0;
 }
 strcpy(pinned_dir,p); return 1;
}
unsigned s4_abi_version(void) { return S4_ABI_VERSION; }
extern int core_s4_shutdown(void);
int s4_shutdown(void) {
 if(!enter()) return -1;
 AcquireSRWLockExclusive(&registry);
 for(int i=0;i<S4_LIVE_LIMIT;i++) if(handles[i].core) {
  ReleaseSRWLockExclusive(&registry);leave();return -1;
 }
 int r=core_s4_shutdown();
 if(!r) {
  initialized=0;pinned_dir[0]=0;
  /* These are plain records, never locks. Keep next_cookie monotonic! */
  memset(handles,0,sizeof handles);
 }
 ReleaseSRWLockExclusive(&registry);leave();return r;
}
int s4_init(const char *path) {
 if(!enter()) return -1;
 char p[240]; int r=-1;
 if(path_ascii(path,p) && hash_is(p,ENGINE_SHA)) {
  r=core_s4_init(p); if(!r) initialized=1;
 }
 leave(); return r;
}
int s4_list_modes(const char *dir,s4_mode *m,int max) {
 if(max<0 || (max>0 && !m) || !enter()) return -1;
 char p[240]; int r=voices(dir,p)?core_s4_list_modes(p,m,max):-1;
 leave(); return r;
}
s4_engine *s4_open(const char *dir,const char *name,char *err,size_t cap) {
 if(err && cap) err[0]=0;
 if(!name || !name[0] || strlen(name)>=64 || !enter()) {
  if(err && cap) snprintf(err,cap,"invalid mode or callback reentry"); return NULL;
 }
 char p[240]; s4_engine *result=NULL;
 int slot=0;while(slot<S4_LIVE_LIMIT && handles[slot].core)slot++;
 if(slot==S4_LIVE_LIMIT || !next_cookie || !voices(dir,p)) {
  if(err && cap) snprintf(err,cap,"invalid/unverified resources, live capacity, or cookie counter exhausted");
 } else {
  s4_engine *core=core_s4_open(p,name,err,cap);
  if(core) {
   const s4_mode *m=core_s4_mode_info(core);
   if(m->sample_rate!=22050 && m->sample_rate!=8000) {
    core_s4_close(core); if(err && cap) snprintf(err,cap,"unsupported native sample rate");
   } else {
    AcquireSRWLockExclusive(&registry);
    s4_engine *h=&handles[slot];h->core=core;h->mode=*m;h->volume=100;
    h->cookie=next_cookie;result=(s4_engine*)next_cookie;
    next_cookie=next_cookie>UINTPTR_MAX-16?0:next_cookie+16;
    ReleaseSRWLockExclusive(&registry);
   }
  }
 }
 leave(); return result;
}
void s4_close(s4_engine *e) {
 if(!enter()) return;
 AcquireSRWLockExclusive(&registry);
 s4_engine *h=find(e);
 if(h) { s4_engine *core=h->core; h->core=NULL; core_s4_close(core); }
 ReleaseSRWLockExclusive(&registry); leave();
}
void s4_stop(s4_engine *e) {
 AcquireSRWLockShared(&registry); s4_engine *h=find(e);
 if(h) core_s4_stop(h->core);
 ReleaseSRWLockShared(&registry);
}
const s4_mode *s4_mode_info(const s4_engine *e) {
 if(!enter()) return NULL; s4_engine *h=find(e); const s4_mode *m=h?&h->mode:NULL; leave(); return m;
}
void s4_get_limits(const s4_engine *e,s4_limits *l) {
 if(l) memset(l,0,sizeof *l); if(!l || !enter()) return;
 s4_engine *h=find(e); if(h) core_s4_get_limits(h->core,l); leave();
}
#define GETTER(type,name,expr) type name(const s4_engine *e) { \
 if(!enter()) return 0; s4_engine *h=find(e); type r=h?(expr):0; leave(); return r; }
GETTER(int,s4_sample_rate,core_s4_sample_rate(h->core))
GETTER(unsigned,s4_get_pitch,core_s4_get_pitch(h->core))
GETTER(unsigned,s4_get_speed,core_s4_get_speed(h->core))
GETTER(unsigned,s4_get_volume,h->volume)
#define SETTER(name,expr) int name(s4_engine *e,unsigned v) { \
 if(!enter()) return -1; s4_engine *h=find(e); int r=h?(expr):-1; leave(); return r; }
SETTER(s4_set_pitch,core_s4_set_pitch(h->core,v))
SETTER(s4_set_speed,core_s4_set_speed(h->core,v))
SETTER(s4_set_volume,v<=100?(h->volume=v,0):-1)
typedef struct Delivery { s4_pcm_fn fn; void *user; unsigned volume; } Delivery;
static int deliver(const int16_t *pcm,size_t n,void *u) {
 Delivery *d=u;
 if(d->volume==100) return d->fn(pcm,n,d->user);
 int16_t *copy=malloc(n*sizeof *copy); if(!copy) return 1;
 for(size_t i=0;i<n;i++) copy[i]=(int16_t)((int32_t)pcm[i]*(int)d->volume/100);
 int r=d->fn(copy,n,d->user); free(copy); return r;
}
int s4_speak(s4_engine *e,const char *text,unsigned flags,s4_pcm_fn fn,void *user) {
 if(!text || !fn || flags || strlen(text)>65535 || !enter()) return -1;
 s4_engine *h=find(e); int r=-1;
 if(h) { Delivery d={fn,user,h->volume}; in_speech=1; r=core_s4_speak(h->core,text,0,deliver,&d); in_speech=0; }
 leave(); return r;
}
int s4_reset(s4_engine *e) {
 if(!enter()) return -1; s4_engine *h=find(e); int r=-1;
 if(h) { r=core_s4_reset(h->core); if(!r) h->volume=100; }
 leave(); return r;
}

#include "fragments_impl.h"
