/* Small embedding example: dynamic native PCM capture, never audio playback. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "dynamic_api.h"
typedef struct Sink {FILE *file;unsigned long long samples;int failed;} Sink;
static int pcm(const int16_t*p,size_t n,void*u){Sink*s=u;s->samples+=n;if(fwrite(p,2,n,s->file)!=n)s->failed=1;return s->failed;}
int main(int argc,char**argv) {
 if(argc!=6){fprintf(stderr,"Usage: sample_client DLL resource-dir voice text output.pcm\n");return 2;}
 int result=1;char original[1024],err[256];s4_engine *engine=NULL;Sink sink={0};
 if(!s4_load(argv[1])){fprintf(stderr,"Cannot load all18 exports\n");return 3;}
 snprintf(original,sizeof original,"%s/msttssyn.dll",argv[2]);
 if(s4_init(original)!=0)goto cleanup;
 engine=s4_open(argv[2],argv[3],err,sizeof err);
 if(!engine){fprintf(stderr,"Open: %s\n",err);goto cleanup;}
 sink.file=fopen(argv[5],"wb");if(!sink.file)goto cleanup;
 int rate=s4_sample_rate(engine);
 int spoken=s4_speak(engine,argv[4],0,pcm,&sink);
 if(spoken==0 && !sink.failed && sink.samples){printf("native_rate=%d channels=1 signed16_le samples=%llu bytes=%llu\n",rate,sink.samples,sink.samples*2);result=0;}
cleanup:
 if(sink.file && fclose(sink.file))result=1;
 if(engine)s4_close(engine);
 /* In a threaded consumer, stop/join ALL workers and end borrowed access here.
  * Failed shutdown must keep the DLL loaded. Never unload around active callers. */
 if(s4_shutdown()!=0){fprintf(stderr,"Shutdown rejected; DLL intentionally retained\n");return 4;}
 if(!s4_unload() || GetModuleHandleA(argv[1])!=NULL)return 5;
 return result;
}
