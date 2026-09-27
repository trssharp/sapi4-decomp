// Unit decoding of msttssyn.dll's vocoder (@0x636719fa), decompiled. Compare ms-sam-mike-mary-decomp's
// unit_decode (FUN_5ed58ee3), the SAPI 5 descendant of the same routine.
#include "voice.h"
#include "codec.h"
#include "crt_vc.h"
#include "dsp.h"
#include "fft.h"
#include "lpc.h"
#include "x87.h"
#include <string.h>

LAYOUT(VocVoice, noise, 0x3c);
LAYOUT(VocSynthState, flags, 0x24);
LAYOUT(VocUnit, _1c, 0x1c);

VocUnit *voc_unit_decode(int index, const VocVoice *v, VocSynthState *s) {
    float lsf[38], spec[512], buf[512];
    if (index < 0 || v->fft_n > 0x200) return NULL;
    VocUnit *u = vc_malloc(0x20);
    if (!u) return NULL;
    const uint8_t *p = GP(const uint8_t, GP(const uint32_t, v->units)[index]);
    int32_t nframes;
    memcpy(&nframes, p, 4);
    p += 4;
    u->nframes = nframes;
    float *periods = vc_malloc((size_t)(uint32_t)(nframes * 4));
    GPSET(u->periods, periods);
    p += codec_bytes_to_floats((const int8_t *)p, nframes, periods, 0);
    const int order = v->order;
    u->order = order;
    float *lpc = vc_malloc((size_t)(uint32_t)(nframes * (order + 1) * 4));
    GPSET(u->lpc, lpc);
    for (int f = 0; f < nframes; f++) {
        float *o = lsf;
        for (int i = 0; i < v->n_lsf; i++) {
            const Codebook *cb = GP(const Codebook, GP(const uint32_t, v->lsf)[i]);
            int dim = cb->dim;
            uint8_t idx = *p++;
            const float *src = GP(const float, cb->data) + idx * dim;
            for (int k = 0; k < dim; k++) *o++ = src[k];
        }
        lpc_from_lsf(lsf, lpc + f * (order + 1), order, f);
    }
    vc_sleep(0);
    float *gains = vc_malloc((size_t)(uint32_t)(nframes * 4));
    GPSET(u->gains, gains);
    memcpy(gains, p, (size_t)(uint32_t)(nframes * 4));
    p += nframes * 4;
    u->total = 0;
    for (int f = 0; f < nframes; f++) u->total += x87_ftol(x87_abs_cmp0(periods[f]));
    float *pos = vc_malloc((size_t)(uint32_t)(u->total * 4));
    GPSET(u->exc, pos);
    int spec_set = 0;
    for (int f = 0; f < nframes; f++) {
        int n = x87_ftol(x87_abs_cmp0(periods[f]));
        if ((double)periods[f] > 0.0 && !(s->flags & 1)) {
            int bin = 1;
            if (v->n_dexc && f && (double)periods[f - 1] >= 0.0) {   // (jb: a NaN is not delta)
#ifdef DECOMP_HOOK
                if (!spec_set) decomp_warn("voc_unit_decode: delta frame before any voiced frame (the original reads stale stack)");
#endif
                for (int b = 0; b < v->n_dexc; b++) {
                    const Codebook *cb = GP(const Codebook, GP(const uint32_t, v->dexc)[b]);
                    int half = cb->dim / 2;
                    uint8_t idx = *p++;
                    dsp_add_halves(spec, GP(const float, cb->data) + idx * cb->dim, bin, half, v->fft_n);
                    bin += half;
                }
            } else {
                if (v->fft_n > 0) memset(spec, 0, (size_t)v->fft_n * 4);
                for (int b = 0; b < v->n_exc; b++) {
                    const Codebook *cb = GP(const Codebook, GP(const uint32_t, v->exc)[b]);
                    int half = cb->dim / 2;
                    uint8_t idx = *p++;
                    dsp_place_halves(spec, GP(const float, cb->data) + idx * cb->dim, bin, half, v->fft_n);
                    bin += half;
                }
                spec_set = 1;
            }
            memcpy(buf, spec, (size_t)v->fft_n * 4);
            fft_inverse_real(buf, v->fft_n, v->fft_log2, GP(const float, v->sine));
            dsp_scale_b(buf, v->fft_n, gains[f]);
            dsp_fold_pulse(pos, n, buf, v->fft_n);
        } else {
            double g = (double)gains[f] * (double)0.02f;
            if (s->noise_pos + n >= v->noise_len) s->noise_pos = 0;
            const int8_t *noise = GP(const int8_t, v->noise) + s->noise_pos;
            for (int k = 0; k < n; k++) pos[k] = (float)((double)noise[k] * g);
            s->noise_pos += n;
        }
        if ((double)periods[f] > 0.0 && (s->flags & 1)) periods[f] = (float)-(double)periods[f];
        pos += n;
    }
    u->_1c = 0;
    return u;
}
