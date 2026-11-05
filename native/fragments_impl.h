#include "fragments.h"
/* Canonical supported bookmark grammar only. Arbitrary text never enters tags.
 * Prosody and explicit breaks remain preview04 barriers pending qualification. */
int s4_speak_fragments(s4_engine *e,const S4Fragment *parts,uint32_t n,
                      S4MarkerFn marker,void *mu,s4_pcm_fn pcm,void *pu) {
 if(!pcm || n>4096 || (n&&!parts))return -1;
 char *text=malloc(65536);if(!text)return -1;
 size_t k=0;int valid=1;
 for(uint32_t i=0;i<n&&valid;i++) {
  if(parts[i].kind==0 && parts[i].text) {
   const unsigned char *p=(const unsigned char*)parts[i].text;
   for(size_t j=0;p[j];j++) {
    if(k+(p[j]=='\\'?2:1)>65535){valid=0;break;}
    text[k++]=(char)p[j];if(p[j]=='\\')text[k++]='\\';
   }
  } else if(parts[i].kind==1 && !parts[i].text && marker && parts[i].index<=2147483647u) {
   char tag[32];int z=snprintf(tag,sizeof tag,"\\mrk=%u\\",parts[i].index);
   if(k+(size_t)z>65535){valid=0;break;}
   memcpy(text+k,tag,z);k+=z;
  } else valid=0;
 }
 text[k]=0;
 if(!valid||!enter()){free(text);return -1;}
 s4_engine *h=find(e);int r=-1;
 if(h) {
  Delivery d={pcm,pu,h->volume};stock_marker_begin(marker,mu);in_speech=1;
  r=core_s4_speak(h->core,text,1,deliver,&d);
  in_speech=0;stock_marker_begin(NULL,NULL);
 }
 leave();free(text);return r;
}
