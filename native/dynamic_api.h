#ifndef S4_DYNAMIC_API_H
#define S4_DYNAMIC_API_H
#include <windows.h>
#include <stdlib.h>
#include "sapi4tts.h"
static HMODULE s4_module;
static int (*p_s4_shutdown)(void);
static int s4_unload(void) {
 if(!s4_module)return 1;
 if(!p_s4_shutdown || p_s4_shutdown())return 0;
 HMODULE h=s4_module;
 if(!FreeLibrary(h))return 0;
 s4_module=NULL;return 1;
}
static void s4_cleanup(void) { if(!s4_unload()) abort(); }
static int s4_exit_registered;
static __typeof__(&s4_abi_version) p_s4_abi_version;
static __typeof__(&s4_init) p_s4_init;
static __typeof__(&s4_list_modes) p_s4_list_modes;
static __typeof__(&s4_open) p_s4_open;
static __typeof__(&s4_close) p_s4_close;
static __typeof__(&s4_mode_info) p_s4_mode_info;
static __typeof__(&s4_sample_rate) p_s4_sample_rate;
static __typeof__(&s4_get_limits) p_s4_get_limits;
static __typeof__(&s4_get_pitch) p_s4_get_pitch;
static __typeof__(&s4_set_pitch) p_s4_set_pitch;
static __typeof__(&s4_get_speed) p_s4_get_speed;
static __typeof__(&s4_set_speed) p_s4_set_speed;
static __typeof__(&s4_get_volume) p_s4_get_volume;
static __typeof__(&s4_set_volume) p_s4_set_volume;
static __typeof__(&s4_speak) p_s4_speak;
static __typeof__(&s4_stop) p_s4_stop;
static __typeof__(&s4_reset) p_s4_reset;
static int s4_load(const char *utf8) {
 wchar_t path[32768];
 if(!utf8 || !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8,-1,path,32768)) return 0;
 s4_module=LoadLibraryExW(path,NULL,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
 if(!s4_module) return 0;
 p_s4_abi_version=(__typeof__(p_s4_abi_version))GetProcAddress(s4_module,"s4_abi_version"); if(!p_s4_abi_version) { FreeLibrary(s4_module); s4_module=NULL; return 0; }
 p_s4_init=(__typeof__(p_s4_init))GetProcAddress(s4_module,"s4_init"); if(!p_s4_init) { FreeLibrary(s4_module); s4_module=NULL; return 0; }
 p_s4_list_modes=(__typeof__(p_s4_list_modes))GetProcAddress(s4_module,"s4_list_modes"); if(!p_s4_list_modes) { FreeLibrary(s4_module); s4_module=NULL; return 0; }
 p_s4_open=(__typeof__(p_s4_open))GetProcAddress(s4_module,"s4_open"); if(!p_s4_open) { FreeLibrary(s4_module); s4_module=NULL; return 0; }
 p_s4_close=(__typeof__(p_s4_close))GetProcAddress(s4_module,"s4_close"); if(!p_s4_close) { FreeLibrary(s4_module); s4_module=NULL; return 0; }
 p_s4_mode_info=(__typeof__(p_s4_mode_info))GetProcAddress(s4_module,"s4_mode_info"); if(!p_s4_mode_info) { FreeLibrary(s4_module); s4_module=NULL; return 0; }
 p_s4_sample_rate=(__typeof__(p_s4_sample_rate))GetProcAddress(s4_module,"s4_sample_rate"); if(!p_s4_sample_rate) { FreeLibrary(s4_module); s4_module=NULL; return 0; }
 p_s4_get_limits=(__typeof__(p_s4_get_limits))GetProcAddress(s4_module,"s4_get_limits"); if(!p_s4_get_limits) { FreeLibrary(s4_module); s4_module=NULL; return 0; }
 p_s4_get_pitch=(__typeof__(p_s4_get_pitch))GetProcAddress(s4_module,"s4_get_pitch"); if(!p_s4_get_pitch) { FreeLibrary(s4_module); s4_module=NULL; return 0; }
 p_s4_set_pitch=(__typeof__(p_s4_set_pitch))GetProcAddress(s4_module,"s4_set_pitch"); if(!p_s4_set_pitch) { FreeLibrary(s4_module); s4_module=NULL; return 0; }
 p_s4_get_speed=(__typeof__(p_s4_get_speed))GetProcAddress(s4_module,"s4_get_speed"); if(!p_s4_get_speed) { FreeLibrary(s4_module); s4_module=NULL; return 0; }
 p_s4_set_speed=(__typeof__(p_s4_set_speed))GetProcAddress(s4_module,"s4_set_speed"); if(!p_s4_set_speed) { FreeLibrary(s4_module); s4_module=NULL; return 0; }
 p_s4_get_volume=(__typeof__(p_s4_get_volume))GetProcAddress(s4_module,"s4_get_volume"); if(!p_s4_get_volume) { FreeLibrary(s4_module); s4_module=NULL; return 0; }
 p_s4_set_volume=(__typeof__(p_s4_set_volume))GetProcAddress(s4_module,"s4_set_volume"); if(!p_s4_set_volume) { FreeLibrary(s4_module); s4_module=NULL; return 0; }
 p_s4_speak=(__typeof__(p_s4_speak))GetProcAddress(s4_module,"s4_speak"); if(!p_s4_speak) { FreeLibrary(s4_module); s4_module=NULL; return 0; }
 p_s4_stop=(__typeof__(p_s4_stop))GetProcAddress(s4_module,"s4_stop"); if(!p_s4_stop) { FreeLibrary(s4_module); s4_module=NULL; return 0; }
 p_s4_reset=(__typeof__(p_s4_reset))GetProcAddress(s4_module,"s4_reset"); if(!p_s4_reset) { FreeLibrary(s4_module); s4_module=NULL; return 0; }
 p_s4_shutdown=(int(*)(void))GetProcAddress(s4_module,"s4_shutdown");
 if(!p_s4_shutdown) { FreeLibrary(s4_module);s4_module=NULL;return 0; }
 if(!s4_exit_registered) { atexit(s4_cleanup);s4_exit_registered=1; }
 return 1;
}
#define s4_shutdown p_s4_shutdown
#define s4_abi_version p_s4_abi_version
#define s4_init p_s4_init
#define s4_list_modes p_s4_list_modes
#define s4_open p_s4_open
#define s4_close p_s4_close
#define s4_mode_info p_s4_mode_info
#define s4_sample_rate p_s4_sample_rate
#define s4_get_limits p_s4_get_limits
#define s4_get_pitch p_s4_get_pitch
#define s4_set_pitch p_s4_set_pitch
#define s4_get_speed p_s4_get_speed
#define s4_set_speed p_s4_set_speed
#define s4_get_volume p_s4_get_volume
#define s4_set_volume p_s4_set_volume
#define s4_speak p_s4_speak
#define s4_stop p_s4_stop
#define s4_reset p_s4_reset
#endif
