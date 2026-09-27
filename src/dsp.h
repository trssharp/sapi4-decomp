// Signal-processing leaves of msttssyn.dll (1999). Names are ours; addresses are the DLL's own.
#pragma once
#include <stdint.h>

// @0x636712d5 stdcall: all-pole (IIR) filter in place, direct form. a[1..order] are used (a[0] is not);
// mem[0..order] is the state: mem[1..order] the previous outputs. 26% of all guest instructions.
void dsp_allpole(const float *a, int order, float *mem, float *x, int n);
// @0x63671943 stdcall: FIR filter in place: x[i] = sum c[k] * x[i-k], k = 0..order-1; mem[0..order-1] holds
// the previous inputs.
void dsp_fir(float *x, int n, const float *c, float *mem, int order);
// @0x636719a0 stdcall: recursive filter in place: y = c[0]*x + sum c[k] * mem[k-1] (k = order-1..1); the
// output is shifted into mem[0].
void dsp_iir_c0(float *x, int n, const float *c, float *mem, int order);
// @0x63671e49 stdcall: sum of |x[i]|, returned as a double (ST0)
double dsp_sum_abs(const float *x, int n);
// @0x63671e79 stdcall: x[i] *= g
void dsp_scale(float *x, int n, float g);
// @0x63671e96 stdcall: dst[i] = src[n-1-i]
void dsp_reverse(const float *src, int n, float *dst);
// @0x63671ebc stdcall: float -> 16-bit PCM, round to nearest (no clipping: the low 16 bits of fistp)
void dsp_to_pcm16(int16_t *dst, const float *src, int n);
// @0x63671eee stdcall: float (16-bit scale) -> unsigned 8-bit PCM
void dsp_to_pcm8(uint8_t *dst, const float *src, int n);
// @0x63671f31 stdcall: max |x[i]| (x[0] when n <= 1), returned as a double (ST0)
double dsp_max_abs(const float *x, int n);
