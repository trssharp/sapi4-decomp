// The hook table: every decompiled function, by address. Keep sorted by address.
#include "hooks.h"
#include "adapters.h"

const DecompHook decomp_hooks[] = {
#define HOOK(addr, name, argbytes) { addr, #name, hk_##name, argbytes },
#include "hooklist.h"
#undef HOOK
    { 0, NULL, NULL, 0 }
};
const int decomp_nhooks = (int)(sizeof decomp_hooks / sizeof decomp_hooks[0]) - 1;
