#pragma once
#include "hooks.h"
#define HOOK(addr, name, argbytes) void hk_##name(Emu *e, X86 *c);
#include "hooklist.h"
#undef HOOK
