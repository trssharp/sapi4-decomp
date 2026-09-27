// Adapters: guest arguments -> the portable C in src/ -> guest results. Pointers are translated to host
// pointers into guest memory (bounds-checked for the extent the function touches).
#include "adapters.h"
#include "dsp.h"
#include "fft.h"
#include "lpc.h"
#include "codec.h"
#include "effect.h"
#include "voice.h"
#include "tags.h"
#include "containers.h"
#include <string.h>

#define A(i) x86_arg(c, (i))
#define AI(i) ((int32_t)x86_arg(c, (i)))
static inline float argf(X86 *c, int i) { uint32_t u = x86_arg(c, i); float f; memcpy(&f, &u, 4); return f; }
// host pointer to n elements of guest memory at a (NULL when n <= 0: the function never dereferences it)
#define PTR(T, a, n) ((n) > 0 ? GPH(T, (a), (uint32_t)(n) * sizeof(T)) : (T *)NULL)
// host pointer to guest a, checking the element range [lo, hi) the function touches (may be negative)
static void *span(X86 *c, uint32_t a, long lo, long hi, uint32_t es) {
    if (hi <= lo) return c->mem + a;
    uint32_t start = a + (uint32_t)(lo * (long)es);
    x86_ptr(c, start, (uint32_t)((hi - lo) * (long)es));
    return c->mem + a;
}
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

void hk_dsp_allpole(Emu *e, X86 *c) {
    int order = AI(1), n = AI(4);
    int m = order > 0 ? order + 1 : 1;
    dsp_allpole(PTR(float, A(0), m), order, PTR(float, A(2), m), PTR(float, A(3), n), n);
}
void hk_dsp_fir(Emu *e, X86 *c) {
    int n = AI(1), order = AI(4);
    int m = order > 0 ? order : 1;
    dsp_fir(PTR(float, A(0), n), n, PTR(float, A(2), m), PTR(float, A(3), m), order);
}
void hk_dsp_iir_c0(Emu *e, X86 *c) {
    int n = AI(1), order = AI(4);
    int m = order > 0 ? order : 1;
    dsp_iir_c0(PTR(float, A(0), n), n, PTR(float, A(2), m), PTR(float, A(3), m), order);
}
void hk_dsp_sum_abs(Emu *e, X86 *c) {
    int n = AI(1);
    x86_fpu_push(c, dsp_sum_abs(PTR(float, A(0), n), n));
}
void hk_dsp_scale(Emu *e, X86 *c) {
    int n = AI(1);
    dsp_scale(PTR(float, A(0), n), n, argf(c, 2));
}
void hk_dsp_reverse(Emu *e, X86 *c) {
    int n = AI(1);
    dsp_reverse(PTR(float, A(0), n), n, PTR(float, A(2), n));
}
void hk_dsp_to_pcm16(Emu *e, X86 *c) {
    int n = AI(2);
    dsp_to_pcm16(PTR(int16_t, A(0), n), PTR(float, A(1), n), n);
}
void hk_dsp_to_pcm8(Emu *e, X86 *c) {
    int n = AI(2);
    dsp_to_pcm8(PTR(uint8_t, A(0), n), PTR(float, A(1), n), n);
}
void hk_dsp_max_abs(Emu *e, X86 *c) {
    int n = AI(1);
    x86_fpu_push(c, dsp_max_abs(PTR(float, A(0), n > 1 ? n : 1), n));
}

void hk_dsp_scale_b(Emu *e, X86 *c) {
    int n = AI(1);
    dsp_scale_b(PTR(float, A(0), n), n, argf(c, 2));
}
void hk_dsp_fold_pulse(Emu *e, X86 *c) {
    int n = AI(1), len = AI(3);
    int m = MIN(len / 2, n);
    float *dst = span(c, A(0), MIN(0, m), MAX(n, 0), 4);
    const float *src = span(c, A(2), 0, MAX(MAX(m, 0), len), 4);
    dsp_fold_pulse(dst, n, src, len);
}
static void halves(X86 *c, int add) {
    int off = AI(2), n = AI(3), len = AI(4);
    long b = (long)len - off - n + 1;
    float *dst = n > 0 ? span(c, A(0), MIN(off, b), MAX(off + n, b + n), 4) : NULL;
    const float *src = n > 0 ? span(c, A(1), 0, 2L * n, 4) : NULL;
    if (add) dsp_add_halves(dst, src, off, n, len); else dsp_place_halves(dst, src, off, n, len);
}
void hk_dsp_place_halves(Emu *e, X86 *c) { halves(c, 0); }
void hk_dsp_add_halves(Emu *e, X86 *c) { halves(c, 1); }
void hk_dsp_cmp_float(Emu *e, X86 *c) {
    E_RET(dsp_cmp_float(GPH(float, A(0), 4), GPH(float, A(1), 4)));
}
void hk_dsp_sort_floats(Emu *e, X86 *c) {
    int n = AI(1);
    E_RET(dsp_sort_floats(PTR(float, A(0), n), n));
}

void hk_fft_inverse_real(Emu *e, X86 *c) {
    int n = AI(1), m = AI(2);
    // the table is read up to sine[3*step*j + n/2] < 2n
    fft_inverse_real(PTR(float, A(0), n), n, m, PTR(float, A(3), n > 0 ? 2 * n : 0));
}

void hk_lpc_from_lsf(Emu *e, X86 *c) {
    int order = AI(2);
    lpc_from_lsf(PTR(float, A(0), order), PTR(float, A(1), order + 1), order, AI(3));
}

void hk_dsp_fit_period(Emu *e, X86 *c) {
    int srcn = AI(1), dstn = AI(3), winn = AI(5);
    dsp_fit_period(PTR(float, A(0), MAX(srcn, 1)), srcn, PTR(float, A(2), MAX(dstn, 1)), dstn,
                   PTR(float, A(4), MAX(winn + 1, 1)), winn);
}
void hk_dsp_period(Emu *e, X86 *c) {
    int srcn = AI(1), dstn = AI(3), winn = AI(7);
    dsp_period(PTR(float, A(0), MAX(MAX(srcn, dstn), 1)), srcn, PTR(float, A(2), MAX(dstn, 1)), dstn, argf(c, 4),
               AI(5), PTR(float, A(6), MAX(winn + 1, 1)), winn);
}
void hk_codec_bytes_to_floats(Emu *e, X86 *c) {
    int n = AI(1);
    // the escapes make the byte extent data dependent: check the start, then walk
    const int8_t *p = (const int8_t *)x86_ptr(c, A(0), 1);
    E_RET(codec_bytes_to_floats(p, n, PTR(float, A(2), n), AI(3)));
}

// thiscall: `this` in ECX
#define THIS(T) GPH(T, c->s.r[ECX], sizeof(T))
void hk_effect_db_to_gain(Emu *e, X86 *c) { x86_fpu_push(c, effect_db_to_gain(THIS(Effect), argf(c, 0))); }
void hk_effect_gain_copy(Emu *e, X86 *c) {
    uint32_t n = A(2);
    effect_gain_copy(THIS(Effect), PTR(float, A(0), n), PTR(float, A(1), n), n, argf(c, 3));
}
void hk_effect_mix_clip(Emu *e, X86 *c) {
    uint32_t n = A(2);
    effect_mix_clip(THIS(Effect), PTR(float, A(0), n), PTR(float, A(1), n), n, argf(c, 3));
}
void hk_effect_allpass(Emu *e, X86 *c) {
    uint32_t n = A(1);
    effect_allpass(THIS(Effect), GPH(Allpass, A(0), sizeof(Allpass)), n, PTR(float, A(2), n), PTR(float, A(3), n));
}
void hk_effect_chain(Emu *e, X86 *c) {
    int n = AI(1);
    effect_chain(THIS(Effect), PTR(float, A(0), n), n, GPH(uint32_t, A(2), 20));
}
void hk_effect_process(Emu *e, X86 *c) {
    int n = AI(1);
    effect_process(THIS(Effect), PTR(float, A(0), n), n);
}

static uint32_t host_to_guest(const void *p) { return p ? (uint32_t)((const uint8_t *)p - decomp_guest_mem) : 0; }
void hk_effect_ap_clear(Emu *e, X86 *c) { effect_ap_clear(THIS(Effect), GPH(Allpass, A(0), sizeof(Allpass))); E_RET(A(0)); }
void hk_effect_ap_init(Emu *e, X86 *c) {
    E_RET(effect_ap_init(THIS(Effect), GPH(Allpass, A(0), sizeof(Allpass)), argf(c, 1), AI(2), AI(3)));
}
void hk_effect_build_stages(Emu *e, X86 *c) {
    int16_t n = (int16_t)A(0);
    int k = n > 0 ? n : 1;
    E_RET((uint16_t)effect_build_stages(THIS(Effect), n, GPH(uint32_t, A(1), 4u * k), PTR(float, A(2), k),
                                        PTR(float, A(3), k), argf(c, 4)));
}
void hk_effect_free(Emu *e, X86 *c) { effect_free(THIS(Effect)); }
void hk_effect_preset(Emu *e, X86 *c) { E_RET(effect_preset(THIS(Effect), AI(0))); }
void hk_effect_init(Emu *e, X86 *c) { E_RET((uint16_t)effect_init(THIS(Effect), AI(0), AI(1))); }
void hk_effect_ctor(Emu *e, X86 *c) { E_RET(host_to_guest(effect_ctor(THIS(Effect)))); }

void hk_voc_unit_decode(Emu *e, X86 *c) {
    E_RET(host_to_guest(voc_unit_decode(AI(0), GPH(VocVoice, A(1), sizeof(VocVoice)), GPH(VocSynthState, A(2), sizeof(VocSynthState)))));
}

void hk_voc_unit_free(Emu *e, X86 *c) { voc_unit_free(GPH(VocUnit, A(0), sizeof(VocUnit))); }
// the extents of these arrays are data dependent (counts, frame indices); guest memory is contiguous,
// so the host pointers are only range-checked at their start
#define P0(T, a) ((a) ? (T *)(void *)x86_ptr(c, (a), 1) : (T *)NULL)
void hk_voc_pitch_marks(Emu *e, X86 *c) {
    E_RET(voc_pitch_marks(P0(const float, A(0)), AI(1), P0(float, A(2)), argf(c, 3), argf(c, 4), P0(const float, A(5)),
                          P0(const float, A(6)), AI(7), AI(8), AI(9), P0(int32_t, A(10)), P0(float, A(11))));
}
void hk_voc_excitation(Emu *e, X86 *c) {
    voc_excitation(P0(const float, A(0)), AI(1), P0(const float, A(2)), P0(const float, A(3)), AI(4),
                   P0(const int32_t, A(5)), P0(const float, A(6)), P0(float, A(7)), P0(const float, A(8)),
                   P0(const float, A(9)), AI(10), P0(const float, A(11)), AI(12));
}

void hk_voc_synth_unit(Emu *e, X86 *c) {
    E_RET(voc_synth_unit(THIS(VocSynthState), GPH(VocRequest, A(0), sizeof(VocRequest))));
}

// the tag lexer: pp is a guest char** (a pointer to a guest pointer), buffers are bounded by n
#define PP(i) GPH(uint32_t, A(i), 4)
void hk_tag_lex_number(Emu *e, X86 *c) { E_RET(tag_lex_number(PP(0), A(1) ? GPH(int32_t, A(1), 4) : NULL)); }
void hk_tag_lex_word(Emu *e, X86 *c) { int n = AI(2); E_RET(tag_lex_word(PP(0), GPH(char, A(1), n > 0 ? n + 1 : 1), n)); }
void hk_tag_keyword(Emu *e, X86 *c) { E_RET(tag_keyword((const char *)x86_ptr(c, A(0), 1))); }
void hk_tag_lex_string(Emu *e, X86 *c) { int n = AI(2); E_RET(tag_lex_string(PP(0), GPH(char, A(1), n > 1 ? n + 1 : 2), n)); }
void hk_tag_lex_brace(Emu *e, X86 *c) { int n = AI(2); E_RET(tag_lex_brace(PP(0), GPH(char, A(1), n > 1 ? n + 1 : 2), n)); }
void hk_tag_lex(Emu *e, X86 *c) { int n = AI(2); E_RET(tag_lex(PP(0), GPH(char, A(1), n > 4 ? n + 1 : 5), n)); }
#define TAGH(name) void hk_##name(Emu *e, X86 *c) { int n = AI(4); \
    E_RET(name(AI(0), PP(1), GPH(TagOut, A(2), sizeof(TagOut)), GPH(char, A(3), n > 4 ? n + 1 : 5), n)); }
TAGH(tag_parse_unsupported)
TAGH(tag_parse_flag)
TAGH(tag_parse_number)
TAGH(tag_parse_string)
void hk_tag_parse(Emu *e, X86 *c) { E_RET(tag_parse((char *)x86_ptr(c, A(0), 1), GPH(TagOut, A(1), sizeof(TagOut)))); }

#define STR(i) ((const char *)x86_ptr(c, A(i), 1))
void hk_hash_lookup(Emu *e, X86 *c) {
    E_RET(hash_lookup(A(0) ? GPH(HashTable, A(0), sizeof(HashTable)) : NULL, A(1) ? STR(1) : NULL, A(2) ? GPH(int32_t, A(2), 4) : NULL));
}
void hk_named_index(Emu *e, X86 *c) { E_RET(named_index(GPH(NamedTable, A(0), sizeof(NamedTable)), STR(1))); }
void hk_named_item(Emu *e, X86 *c) { E_RET(named_item(GPH(NamedTable, A(0), sizeof(NamedTable)), AI(1))); }
void hk_prefix_cmp(Emu *e, X86 *c) { E_RET(prefix_cmp(STR(0), STR(1), GPH(int32_t, A(2), 4))); }
void hk_intvec_ctor(Emu *e, X86 *c) { E_RET(host_to_guest(intvec_ctor(THIS(IntVec)))); }
void hk_intvec_free_data(Emu *e, X86 *c) { intvec_free_data(THIS(IntVec)); }
void hk_intvec_reserve(Emu *e, X86 *c) { intvec_reserve(THIS(IntVec), AI(0), AI(1)); }
void hk_vecarray_ctor(Emu *e, X86 *c) { E_RET(host_to_guest(vecarray_ctor(THIS(VecArray)))); }
void hk_vecarray_destroy(Emu *e, X86 *c) { vecarray_destroy(THIS(VecArray)); }
void hk_vecarray_init(Emu *e, X86 *c) { E_RET(vecarray_init(THIS(VecArray), AI(0), AI(1), AI(2))); }
void hk_blob_copy80(Emu *e, X86 *c) { E_RET(blob_copy80(GPH(uint8_t, A(0), 0x50), GPH(Blob, A(1), sizeof(Blob)))); }
