// Pointer fields of the engine's structs. In the hook build (DECOMP_HOOK, the decompiled code running
// inside the emulator next to the original) a struct in guest memory stores 32-bit guest addresses, so
// the field is a uint32_t and GP() turns it into a host pointer; in the portable build it is simply a
// pointer. The same source compiles both ways, and in the hook build the layouts are the original's.
#pragma once
#include <stdint.h>
#ifdef DECOMP_HOOK
extern _Thread_local uint8_t *decomp_guest_mem;
#define GPTR(T) uint32_t
#define GP(T, v) ((T *)(void *)(decomp_guest_mem + (v)))
#define GPSET(field, p) ((field) = (uint32_t)((uint8_t *)(p) - decomp_guest_mem))
#define LAYOUT(T, field, off) _Static_assert(__builtin_offsetof(T, field) == (off), #T "." #field)
#else
#define GPTR(T) T *
#define GP(T, v) ((T *)(v))
#define GPSET(field, p) ((field) = (p))
#define LAYOUT(T, field, off)
#endif
