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
    GPTR(const float) window;  // 0x38 (fft_n) cross-fade window for dsp_fit_period
    GPTR(const int8_t) noise;  // 0x3c noise table
} VocVoice;

struct Effect;
// the synthesizer object (C++ in the original)
typedef struct VocSynthState {
    GPTR(const VocVoice) voice;  // 0x00
    float rate;             // 0x04 sample rate
    int32_t order;          // 0x08 LPC order
    int32_t fixed_rate;     // 0x0c 1: pitch marks at rate / pitch (see voc_pitch_marks)
    GPTR(VocUnit) unit;     // 0x10 the unit being synthesized
    GPTR(float) mem;        // 0x14 synthesis filter state [order + 1]
    GPTR(float) out;        // 0x18 output buffer
    int32_t cap;            // 0x1c its size in samples
    int32_t noise_pos;      // 0x20 read position in the voice's noise table
    uint32_t flags;         // 0x24 bit 0 whisper (every frame unvoiced), 1 FIR, 2 IIR, 3 effect
    GPTR(float) fmem;       // 0x28 FIR / IIR state
    GPTR(const float) fcoef;  // 0x2c FIR / IIR coefficients
    int32_t forder;         // 0x30
    GPTR(struct Effect) effect;  // 0x34
    GPTR(float) last_lpc;   // 0x38 the last frame's LPC coefficients [order + 1]
} VocSynthState;

// one synthesis request (a unit with its prosody)
typedef struct VocRequest {
    int32_t _00;            // 0x00 passed on to voc_pitch_marks, which ignores it
    int32_t unit;           // 0x04 unit index; 0 = silence
    float dur;              // 0x08 duration scale (silence: seconds)
    int32_t npoints;        // 0x0c prosody contour points
    GPTR(float) times;      // 0x10 [npoints] sample positions
    GPTR(float) pitch;      // 0x14 [npoints] (scaled in place by the unit's length when not fixed_rate)
    GPTR(const float) gains;  // 0x18 [npoints]
    int32_t produced;       // 0x1c samples written to out
} VocRequest;

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

// @0x63675bc4 stdcall: free a unit and its arrays
void voc_unit_free(VocUnit *u);
// @0x636710da stdcall: place the pitch marks of a unit. For each frame, periods[f] < 0 is an unvoiced
// stretch of -periods[f] samples; otherwise the pitch at the current position is interpolated from the
// contour (times[], values[], npoints) and the frame's period resampled to it (or, with fixed_rate, the
// period rate / pitch). Marks are emitted while they fall inside the frame's time span; per mark:
// len[j] (samples), frame[j] (source frame index), sign[j] (+1, or -1 for every second repeat of an
// unvoiced frame: played reversed). Returns the number of marks.
int voc_pitch_marks(const float *periods, int nframes, float *len, float time_scale, float rate,
                    const float *times, const float *values, int npoints, int fixed_rate, int unused,
                    int32_t *frame, float *sign);
// @0x63671429 stdcall: assemble the excitation: for each pitch mark j, source frame frames[j]'s
// excitation period (at exc + sum of the earlier |periods|) is fitted to lens[j] samples at out+pos
// (dsp_period: reversed when signs[j] is -1), scaled by the gain contour (times[], gains[]) at pos.
void voc_excitation(const float *periods, int nframes, const float *exc, const float *lens, int count,
                    const int32_t *frames, const float *signs, float *out, const float *times,
                    const float *gains, int npoints, const float *window, int winn);
// @0x636715e2 thiscall: synthesize one request into synth->out: decode the unit, place pitch marks,
// assemble the excitation, run the LPC synthesis filter per mark, then the mode's FIR / IIR / effect.
// Returns 1, or 0 if the unit cannot be decoded or the output does not fit.
int voc_synth_unit(VocSynthState *s, VocRequest *req);
