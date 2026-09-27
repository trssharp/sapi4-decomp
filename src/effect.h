// The voice-mode effects of msttssyn.dll ("in Hall", "in Stadium", "in Space", RoboSoft One..Six):
// a chain of up to five allpass delay lines on a send copy of the signal, mixed back with the dry
// signal and clipped. RoboSoft One/Six shorten every delay loop by a random amount per chunk (the
// warble). C++ objects in the original (thiscall methods); layouts as in the original.
#pragma once
#include "gptr.h"

typedef struct Allpass {
    float gain;             // 0x00 feedback gain (linear)
    int32_t delay;          // 0x04
    int32_t len;            // 0x08 buffer length in samples
    GPTR(float) buf;        // 0x0c
    GPTR(float) wr;         // 0x10 write position
    GPTR(float) rd;         // 0x14 read position
    GPTR(float) end;        // 0x18 buf + len
    GPTR(float) fir;        // 0x1c FIR history (only when the effect has FIR coefficients)
} Allpass;

typedef struct EffectPreset {
    float send_db, dry_db;  // 0x00, 0x04
    int16_t nstages, _pad;  // 0x08
    GPTR(float) delays;     // 0x0c (ms)
    GPTR(float) gains_db;   // 0x10
    float warble;           // 0x14 random shortening depth (0 = none)
} EffectPreset;

typedef struct Effect {
    int32_t chunk;          // 0x00 samples per processing chunk (0x400)
    GPTR(float) scratch;    // 0x04 [chunk]
    float send;             // 0x08 linear gain into the allpass chain
    float dry;              // 0x0c linear gain of the direct signal
    int32_t nstages;        // 0x10
    GPTR(Allpass) stage[5]; // 0x14
    GPTR(const EffectPreset) preset;  // 0x28
    float rnd;              // 0x2c rand() / 4096, drawn after every call to effect_process
    float shrink;           // 0x30 1 - preset->warble * rnd: delay loops run over len * shrink samples
    GPTR(float) coef;       // 0x34 FIR coefficients (ncoef)
    int32_t ncoef;          // 0x38
} Effect;

// @0x6367fbfe thiscall: dB -> linear amplitude (0 below -110 dB), as a double (ST0)
double effect_db_to_gain(Effect *self, float db);
// @0x6367ff1d thiscall: dst = g * src (cleared if g <= 0, copied if g == 1)
void effect_gain_copy(Effect *self, float *dst, const float *src, uint32_t n, float g);
// @0x6367ff87 thiscall: y = clip(g * y + x) to -32768..32767 (nothing if g <= 0)
void effect_mix_clip(Effect *self, const float *x, float *y, uint32_t n, float g);
// @0x63680036 thiscall: one allpass section (v = x + g*d, y = d - g*v), |v| < 0.001 flushed to 0
void effect_allpass(Effect *self, Allpass *ap, uint32_t n, const float *in, float *out);
// @0x63680155 thiscall: the allpass chain in place (up to 5 stages, stops at the first empty one)
void effect_chain(Effect *self, float *buf, int n, GPTR(Allpass) *stages);
// @0x63680189 thiscall: the whole effect over a buffer, in chunks; then draws the next warble value
void effect_process(Effect *self, float *buf, int n);
