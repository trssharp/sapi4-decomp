// Unit decoding of msttssyn.dll's vocoder (@0x636719fa), decompiled. Compare ms-sam-mike-mary-decomp's
// unit_decode (FUN_5ed58ee3), the SAPI 5 descendant of the same routine.
#include "voice.h"
#include "codec.h"
#include "crt_vc.h"
#include "dsp.h"
#include "fft.h"
#include "lpc.h"
#include "x87.h"
#include "effect.h"
#include <string.h>

LAYOUT(VocVoice, noise, 0x3c);
LAYOUT(VocSynthState, flags, 0x24);
LAYOUT(VocSynthState, last_lpc, 0x38);
LAYOUT(VocRequest, produced, 0x1c);
LAYOUT(VocUnit, _1c, 0x1c);

VocUnit *voc_unit_decode(int index, const VocVoice *v, VocSynthState *s) {
    float lsf[38], spec[512], buf[512];
    if (index < 0 || v->fft_n > 0x200) return NULL;
    VocUnit *u = vc_malloc(0x20);
    if (!u) return NULL;
    const uint8_t *p = GP(const uint8_t, GP(GPTR(const uint8_t), v->units)[index]);
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
            const Codebook *cb = GP(const Codebook, GP(GPTR(const Codebook), v->lsf)[i]);
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
    (void)spec_set;
    for (int f = 0; f < nframes; f++) {
        int n = x87_ftol(x87_abs_cmp0(periods[f]));
        if ((double)periods[f] > 0.0 && !(s->flags & 1)) {
            int bin = 1;
            if (v->n_dexc && f && (double)periods[f - 1] >= 0.0) {   // (jb: a NaN is not delta)
#ifdef DECOMP_HOOK
                if (!spec_set) decomp_warn("voc_unit_decode: delta frame before any voiced frame (the original reads stale stack)");
#endif
                for (int b = 0; b < v->n_dexc; b++) {
                    const Codebook *cb = GP(const Codebook, GP(GPTR(const Codebook), v->dexc)[b]);
                    int half = cb->dim / 2;
                    uint8_t idx = *p++;
                    dsp_add_halves(spec, GP(const float, cb->data) + idx * cb->dim, bin, half, v->fft_n);
                    bin += half;
                }
            } else {
                if (v->fft_n > 0) memset(spec, 0, (size_t)v->fft_n * 4);
                for (int b = 0; b < v->n_exc; b++) {
                    const Codebook *cb = GP(const Codebook, GP(GPTR(const Codebook), v->exc)[b]);
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

void voc_unit_free(VocUnit *u) {
    if (u->periods) vc_free(GP(void, u->periods));
    if (u->exc) vc_free(GP(void, u->exc));
    if (u->lpc) vc_free(GP(void, u->lpc));
    if (u->gains) vc_free(GP(void, u->gains));
    vc_free(u);
}

int voc_pitch_marks(const float *periods, int nframes, float *len, float time_scale, float rate,
                    const float *times, const float *values, int npoints, int fixed_rate, int unused,
                    int32_t *frame, float *sign) {
    (void)unused;
    const double K = -0.5;
    float tprev = 0.0f, h = 0.0f;
    int f = 0, pos = 0, run = 0, count = 0, reverse = 0;
    // (the original also sums |periods| with dsp_sum_abs and throws the result away)
    if (nframes <= 0) return 0;
    double acc = 0.0;       // the time the marks have reached (x87 register, never rounded)
    const float *pf = periods;
    do {
        int n, m, unvoiced;
        if (!((double)*pf >= 0.0)) {
            n = m = x87_ftol(-(double)*pf - K);
            unvoiced = 1;
        } else {
            int k = 1;
            if (npoints - 1 > 1) {
                double dpos = (double)pos;
                while (dpos > (double)times[k]) {   // jbe leaves the loop on <= or unordered
                    k++;
                    if (k >= npoints - 1) break;
                }
            }
            double a = values[k - 1], b = times[k - 1];
            double q = ((double)values[k] - a) * ((double)pos - b) / ((double)times[k] - b);
            double P = q + a;
            n = x87_ftol((double)*pf - K);
            m = x87_ftol((double)n / P - K);
            if (fixed_rate) {
                m = x87_ftol((double)rate / P);
                if (m > 2 * n) m = 2 * n;
                if (n > 2 * m) m = n / 2;
            }
            unvoiced = 0;
        }
        int skip = 0;
        double md = (double)m;
        double hd = md * K / (double)time_scale;
        h = (float)hd;
        float e = (float)((double)n + tprev);
        if (acc - hd > (double)e) {
            skip = 1;
        } else {
            reverse = (unvoiced && (run & 1)) ? 1 : 0;
            pos += m;
            run++;
            acc = md / (double)time_scale + acc;
            len[count] = (float)md;
            frame[count] = f;
            sign[count] = reverse ? -1.0f : 1.0f;
            count++;
        }
        if (acc - (double)h > (double)e || run >= 3 || reverse == 1 || skip) {
            tprev = e;
            f++;
            pf++;
            run = 0;
        }
    } while (f < nframes);
    return count;
}

void voc_excitation(const float *periods, int nframes, const float *exc, const float *lens, int count,
                    const int32_t *frames, const float *signs, float *out, const float *times,
                    const float *gains, int npoints, const float *window, int winn) {
    int outpos = 0;
    double s1 = 0.0, s2 = 0.0;
    for (int i = 0; i < nframes; i++) s1 = s1 + x87_abs_cmp0(periods[i]);
    for (int j = 0; j < count; j++) s2 = s2 + x87_abs_cmp0(lens[j]);
    float ratio = (float)(s2 / s1);
    float t = 0.0f;   // (computed and never used by the original)
    for (int j = 0; j < count; j++) {
        int fi = frames[j];
        const float *src = exc;
        for (int i = 0; i < fi; i++) src += x87_ftol(x87_abs_cmp0(periods[i]));
        int rev = !((double)signs[j] != -1.0);   // jne: equal or unordered -> reversed
        float *dst = out + outpos;
        int srcn = x87_ftol(x87_abs_cmp0(periods[fi]));
        int dstn = x87_ftol((double)lens[j]);
        int k = 1;
        if (npoints - 1 > 1) {
            double dpos = (double)outpos;
            while (dpos > (double)times[k]) {
                k++;
                if (k >= npoints - 1) break;
            }
        }
        double a = gains[k - 1], b = times[k - 1];
        double q = ((double)gains[k] - a) * ((double)outpos - b) / ((double)times[k] - b);
        dsp_period(src, srcn, dst, dstn, (float)(q + a), rev, window, winn);
        outpos += dstn;
        t = (float)((double)dstn / ratio + t);
    }
    (void)t;
}

int voc_synth_unit(VocSynthState *s, VocRequest *req) {
    const VocVoice *v = GP(const VocVoice, s->voice);
    float *out = GP(float, s->out);
    int ok = 1;
    float *last = NULL;
    if (req->unit == 0) {
        int n = x87_ftol((double)req->dur * s->rate);
        if (n > s->cap) return 0;
        memset(out, 0, (size_t)(uint32_t)(n * 4));
        req->produced = n;
        memset(GP(float, s->mem), 0, (size_t)(uint32_t)(s->order * 4 + 4));
        if (s->flags & 8) effect_process(GP(Effect, s->effect), out, req->produced);
        return 1;
    }
    VocUnit *u = voc_unit_decode(req->unit, v, s);
    GPSET(s->unit, u);
    if (!u) {
        req->produced = 0;
        return 0;
    }
    vc_sleep(0);
    const int nframes = u->nframes;
    float *periods = GP(float, u->periods);
    (void)dsp_max_abs(periods, nframes);   // (result discarded by the original)
    double T = (double)u->total, D = req->dur;
    float dscale = (float)D;
    float nf = (float)(double)nframes;
    int cap = x87_ftol(D * nf * 3.0);
    float *pitch = GP(float, req->pitch);
    if (!s->fixed_rate) {
        for (int i = 0; i < req->npoints; i++) pitch[i] = (float)(T * pitch[i]);
    } else {
        int m = x87_ftol((double)pitch[0]);
        for (int i = 1; i < req->npoints; i++) {
            double md = (double)m;
            if (!(md >= (double)pitch[i])) m = x87_ftol((double)pitch[i]);
        }
        cap = x87_ftol((double)m * D * 3.0);
        dscale = (float)((double)s->rate * D / T);
        cap = 2 * x87_ftol((double)cap - (double)dscale * nf * -3.0);
    }
    size_t size = (size_t)(uint32_t)(cap * 4);
    int32_t *frames = vc_malloc(size);
    float *signs = vc_malloc(size);
    float *lens = vc_malloc(size);
    int count = voc_pitch_marks(periods, nframes, lens, dscale, s->rate, GP(const float, req->times), pitch,
                                req->npoints, s->fixed_rate, req->_00, frames, signs);
    req->produced = 0;
    for (int j = 0; j < count; j++) req->produced = x87_ftol((double)req->produced + x87_abs_cmp0(lens[j]));
    if (req->produced > s->cap) {
        ok = 0;
    } else {
        voc_excitation(periods, nframes, GP(const float, u->exc), lens, count, frames, signs, out,
                       GP(const float, req->times), GP(const float, req->gains), req->npoints,
                       GP(const float, v->window), v->fft_n);
        vc_sleep(0);
        float *x = out;
        float *lpc = GP(float, u->lpc);
        for (int j = 0; j < count; j++) {
            last = lpc + frames[j] * (s->order + 1);
            int n = x87_ftol(x87_abs_cmp0(lens[j]));
            last[0] = 1.0f;
            dsp_allpole(last, s->order, GP(float, s->mem), x, n);
            x += n;
        }
        if (last) memcpy(GP(float, s->last_lpc), last, (size_t)(uint32_t)(s->order * 4 + 4));
        vc_sleep(0);
        if (s->flags & 2) dsp_fir(out, req->produced, GP(const float, s->fcoef), GP(float, s->fmem), s->forder);
        if (s->flags & 4) dsp_iir_c0(out, req->produced, GP(const float, s->fcoef), GP(float, s->fmem), s->forder);
        if (s->flags & 8) effect_process(GP(Effect, s->effect), out, req->produced);
    }
    vc_free(lens);
    vc_free(frames);
    vc_free(signs);
    voc_unit_free(GP(VocUnit, s->unit));
    return ok;
}
