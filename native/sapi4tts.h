/* Canonical native Windows x64 C ABI. MIT: reconstructed code only.
 * Read docs/NATIVE-NVDA.md before embedding. Existing s4_* signatures retained.
 * C ABI / Windows x64 calling convention; sizeof(size_t)==8, int/unsigned==4.
 * Paths are absolute drive-qualified UTF-8. ASCII paths work directly; Unicode
 * paths require an existing ASCII Windows short-path alias, otherwise fail.
 * Resource paths after normalization must fit 239 bytes. No chdir is performed.
 * Exactly the verified 11 user-owned resources are accepted (see resource_hashes.h).
 * Text is NUL-terminated Windows-1252, plain text only, <=65535 bytes; flags==0.
 * Only voice-native mono signed16 PCM rates 22050 and 8000 are exposed.
 * All calls serialize globally except stop. s4_speak is synchronous; callbacks
 * execute on its calling thread. Caller must retain callback/user until return.
 * PCM is borrowed only during callback; copy it, never free it. Callback may only
 * call s4_stop; other reentrant calls fail/no-op (never wait). No callbacks occur
 * after speak returns or close returns. Close waits for active work; it does not
 * cancel automatically. Stop then join speech before close for bounded shutdown.
 * Repeated close/stop, NULL and stale handles are safe within one DLL load.
 * Handles are opaque aligned integer cookies cast to s4_engine*, never object
 * addresses: do not dereference, free, do arithmetic on, or serialize them.
 * Registry records reuse; cookies never do. Discard all cookies at DLL unload.
 * Never unload DLL while any call/callback is active. Do not throw/longjmp across
 * the C boundary. Arbitrary invalid memory pointers are not validated by this ABI.
 * Close frees engine-owned allocations and events and recycles word/handle
 * slots. At most126 LIVE engines; no cumulative open-attempt cap. Shared voice
 * inventories, lexicons, one shared word, and reusable event-node pool remain
 * for this DLL load. Cookies fail closed before uintptr_t counter wrap (x64:
 * 2^60-1 successful opens). Explicit s4_shutdown releases shared state.
 * Unimplemented upstream paths still abort if reached: this is a local preview,
 * not a fault-isolation boundary. Do not pass untrusted/mutating resources.
 */
#ifndef MSTTSSAPI4_PREVIEW03_H
#define MSTTSSAPI4_PREVIEW03_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define S4_ABI_VERSION 0x00010001u
#define S4_TAGGED 1u /* reserved, rejected by this preview */
typedef struct s4_engine s4_engine;
typedef struct s4_mode {
 char name[64], speaker[64];
 int gender, age; uint32_t features; int sample_rate;
} s4_mode;
typedef struct s4_limits {
 unsigned pitch_default,pitch_min,pitch_max,speed_default,speed_min,speed_max;
} s4_limits;
typedef int (*s4_pcm_fn)(const int16_t *,size_t,void *);
/* init: 0 success / -1 failure; first successful initialization pins engine data. */
unsigned s4_abi_version(void);
int s4_init(const char *dll_path);
/* MUST call successfully before FreeLibrary: close every handle, stop/join all
 * caller threads and end all borrowed access first. 0 success (idempotent), -1
 * live handles or callback reentry; rejected shutdown leaves state unchanged.
 * No DllMain cleanup. Bare FreeLibrary leaks shared caches. Reinit in the same
 * loaded DLL is supported, including a new immutable resource directory. Cookies
 * stay monotonic across shutdown; none are valid across DLL unload/reload.
 * External synchronization must prevent new calls after successful shutdown
 * through FreeLibrary. Mode-info borrowed references end at close/shutdown. */
int s4_shutdown(void);
/* Returns total count, writes min(count,max); NULL modes allowed only when max=0. */
int s4_list_modes(const char *voice_dir,s4_mode *modes,int max);
s4_engine *s4_open(const char *voice_dir,const char *mode_name,char *err,size_t errlen);
void s4_close(s4_engine *e);
/* mode_info borrowed until close. Other getters return 0 / zero struct on error. */
const s4_mode *s4_mode_info(const s4_engine *e);
int s4_sample_rate(const s4_engine *e);
void s4_get_limits(const s4_engine *e,s4_limits *lim);
unsigned s4_get_pitch(const s4_engine *e);
int s4_set_pitch(s4_engine *e,unsigned pitch);
unsigned s4_get_speed(const s4_engine *e);
int s4_set_speed(s4_engine *e,unsigned wpm);
/* Additive software volume: 0..100, default100; >100 returns -1, not clamped.
 * PCM scaled in DLL callback using signed integer division toward zero. */
unsigned s4_get_volume(const s4_engine *e);
int s4_set_volume(s4_engine *e,unsigned percent);
/* 0 complete,1 canceled,-1 invalid input/state. NULL callback rejected. */
int s4_speak(s4_engine *e,const char *text,unsigned flags,s4_pcm_fn fn,void *user);
void s4_stop(s4_engine *e);
/* Restores native pitch/speed and volume100. RNG sequences continue upstream. */
int s4_reset(s4_engine *e);
#ifdef __cplusplus
}
#endif
#endif
