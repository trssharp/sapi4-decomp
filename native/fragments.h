/* MIT: synchronous borrowed fragment ABI. Kinds: literal=0, index=1.
 * Caller retains arrays, strings and callbacks until return; callback returns
 * nonzero to stop. <=4096 fragments and <=65535 escaped CP1252 bytes.
 * Include this header for the additive marker API; s4_speak remains plain-only.
 */
#ifndef MSTTSSAPI4_FRAGMENTS_H
#define MSTTSSAPI4_FRAGMENTS_H
#include "sapi4tts.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct S4Fragment {uint32_t kind, index; const char *text;} S4Fragment;
typedef int (*S4MarkerFn)(uint32_t index,uint64_t frames,void *user);
int s4_speak_fragments(s4_engine *e,const S4Fragment *parts,uint32_t n,
                      S4MarkerFn marker,void *mu,s4_pcm_fn pcm,void *pu);
/* Internal core/boundary hook, not a DLL export. */
void stock_marker_begin(S4MarkerFn fn,void *user);
#ifdef __cplusplus
}
#endif
#endif
