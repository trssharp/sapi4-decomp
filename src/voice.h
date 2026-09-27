// The synthesizer's view of a voice (the .vce Inventory stream, loaded) and the per-unit data it
// decodes. Offsets are the original's; fields not yet identified are padding.
#pragma once
#include "gptr.h"

typedef struct Codebook {
    int32_t count;          // 0x00 (inferred: number of entries - not read by the code decompiled so far)
    int32_t dim;            // 0x04 floats per entry
    GPTR(const float) data; // 0x08 count * dim floats
} Codebook;

typedef struct VocVoice {
    int32_t _00;
    int32_t noise_len;      // 0x04 length of the noise table (samples)
    int32_t n_lsf;          // 0x08 LSF codebooks
    int32_t n_exc;          // 0x0c excitation codebooks
    int32_t n_dexc;         // 0x10 delta-excitation codebooks
    GPTR(GPTR(const Codebook)) lsf;   // 0x14
    GPTR(GPTR(const Codebook)) exc;   // 0x18
    GPTR(GPTR(const Codebook)) dexc;  // 0x1c
    int32_t _20;
    GPTR(GPTR(const uint8_t)) units;  // 0x24 unit blobs
    int32_t order;          // 0x28 LPC order
    int32_t fft_n;          // 0x2c
    int32_t fft_log2;       // 0x30
    GPTR(const float) sine; // 0x34 sin table for the FFT
    int32_t _38;
    GPTR(const int8_t) noise;  // 0x3c noise table
} VocVoice;

// the part of the synthesizer object unit decoding uses
typedef struct VocSynthState {
    uint8_t _00[0x20];
    int32_t noise_pos;      // 0x20 read position in the voice's noise table
    uint32_t flags;         // 0x24 bit 0: whisper (every frame unvoiced)
} VocSynthState;

// one decoded unit (malloc'd, 0x20 bytes)
typedef struct VocUnit {
    int32_t nframes;        // 0x00
    int32_t total;          // 0x04 excitation samples (sum of |period|)
    int32_t order;          // 0x08
    GPTR(float) periods;    // 0x0c per frame: > 0 voiced period, <= 0 unvoiced length
    GPTR(float) lpc;        // 0x10 nframes * (order + 1)
    GPTR(float) exc;        // 0x14 total
    GPTR(float) gains;      // 0x18 nframes
    int32_t _1c;            // 0x1c set to 0
} VocUnit;

// @0x636719fa stdcall: decode unit `index`: periods, LSF frames -> LPC, gains, and the excitation -
// voiced periods from codebook spectra (delta-coded against the previous voiced frame) through the
// inverse FFT, unvoiced ones from the noise table. NULL if index < 0, fft_n > 512 or out of memory.
VocUnit *voc_unit_decode(int index, const VocVoice *v, VocSynthState *s);
