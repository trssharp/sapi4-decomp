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
#define GPSET(field, p) ((field) = (p) ? (uint32_t)((uint8_t *)(p) - decomp_guest_mem) : 0u)
// a global of the original DLL, by its preferred-base address (hook build: the live copy in the image)
extern uint32_t decomp_load_delta;
#define DLLVAR(T, addr) ((T *)(void *)(decomp_guest_mem + (uint32_t)(addr) + decomp_load_delta))
#define LAYOUT(T, field, off) _Static_assert(__builtin_offsetof(T, field) == (off), #T "." #field)
#else
#define GPTR(T) T *
#define GP(T, v) ((T *)(v))
#define GPSET(field, p) ((field) = (p))
// portable build: the DLL's data tables are not extracted yet
// (no definition yet: a portable build that uses one fails to link until data extraction exists)
void *decomp_dll_data(uint32_t addr);
#define DLLVAR(T, addr) ((T *)decomp_dll_data(addr))
#define LAYOUT(T, field, off)
#endif
