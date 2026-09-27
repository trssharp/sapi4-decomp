#include "unitsel.h"
#include "crt_vc.h"
#include "x87.h"
#include <math.h>
#include <string.h>

LAYOUT(UnitRec, dur, 0x08);
LAYOUT(UnitRec, c, 0x38);
_Static_assert(sizeof(UnitRec) == 0x4c, "UnitRec");
LAYOUT(UnitStage, sil_phone, 0x2a0);
LAYOUT(UnitStage, tree_mode, 0x298);
LAYOUT(TreeQuery, _2c, 0x2c);

static int is_mark(char ch) { return ch == '\\' || ch == '#'; }

void unit_select(UnitStage *u, const char *ph, int n) {
    int count = 0, prev = u->sil_phone, end = 0, begin = 0;
    for (int i = 0; i < n; i++)
        if (!is_mark(ph[100 * i])) count++;
    if (u->cap < count) {
        u->cap = count;
        GPSET(u->recs, (UnitRec *)vc_realloc(GP(void, u->recs), (size_t)(uint32_t)(count * 0x4c)));
        GPSET(u->ids, (int16_t *)vc_realloc(GP(void, u->ids), (size_t)(uint32_t)(u->cap * 2)));
        GPSET(u->senones, (int16_t *)vc_realloc(GP(void, u->senones), (size_t)(uint32_t)(u->cap * 2)));
    }
    int k = 0;
    for (int i = 0; i < n; i++) {
        const char *p = ph + 100 * i;
        if (is_mark(*p)) continue;
        int16_t id = (int16_t)named_index(GP(const NamedTable, u->phone_names), p);
        GP(int16_t, u->ids)[k] = id;
        GP(UnitRec, u->recs)[k].phone = GP(int16_t, u->ids)[k];
        k++;
    }
    k = 0;
    for (int i = 0; i < n; i++) {
        const char *p = ph + 100 * i;
        if (is_mark(*p)) {
            begin = 1;
            continue;
        }
        if (is_mark(p[100])) end = 1;   // (also reads the slot after the last phone, as the original)
        int right = k + 1 < count ? GP(int16_t, u->ids)[k + 1] : u->sil_phone;
        int8_t pos = end ? (begin ? 's' : 'e') : (begin ? 'b' : 0);
        UnitRec *r = GP(UnitRec, u->recs) + k;
        int32_t s;
        if (u->tree_mode == 1) {
            s = senone_lookup(GP(const SenoneTree, u->tree), r->phone, prev, right, pos, 0);
        } else {
            TreeQuery q;
            memset(&q, 0, sizeof q);   // (the original leaves the unnamed bytes as stack garbage)
            q.cur = (int16_t)r->phone;
            q.left = (int16_t)prev;
            q.right = (int16_t)right;
            q.pos = pos;
            q._2c = 0;
            s = tree_query(GP(const SenoneTree, u->tree), &q);
        }
        r->senone = s;
        end = begin = 0;
        GP(int16_t, u->senones)[k] = (int16_t)r->senone;
        prev = GP(int16_t, u->ids)[k];
        k++;
    }
}

LAYOUT(UnitStage, recs, 0x25c);
LAYOUT(UnitStage, amp, 0x28c);
LAYOUT(UnitOut, c, 0x3c);
_Static_assert(sizeof(UnitOut) == 0x50, "UnitOut");
_Static_assert(sizeof(PhoneIn) == 0x64, "PhoneIn");
LAYOUT(SenoneTree, phone_units, 0x30);
LAYOUT(SenoneTree, nstates, 0x38);

int unit_prosody(UnitStage *u, const PhoneIn *ph, int n, int32_t *last, int next_phone, UnitOutBuf *out) {
    const double K = 0.0;
    float scale = (float)((double)u->amp * (double)1.5259021893143654e-05f);
    UnitRec *recs = GP(UnitRec, u->recs);
    for (int i = 0; i < n; i++) {
        UnitRec *r = recs + i;
        r->dur = ph[i].dur;
        r->nstates = ph[i].nstates;
        for (int k = 0; k < r->nstates; k++) {
            r->a[k] = ph[i].a[k];
            r->b[k] = ph[i].b[k];
            r->c[k] = ph[i].c[k];
        }
    }
    for (int i = 0; i < n; i++) {
        UnitRec *r = recs + i;
        if (r->phone == u->sil_phone) {
            for (int k = 0; k < r->nstates; k++) r->c[k] = 1.0f;
            if (x87_je(K, r->dur)) r->dur = 1.0f;
            if (!x87_jbe(K, r->dur)) {
                r->dur = (float)(-(double)u->dur_scale * r->dur * 0.001);
            } else if (!x87_jbe(r->dur, 5.0)) {
                r->dur = (float)(((double)r->dur - 5.0) * u->dur_scale + u->sil_dur);
            } else if (!x87_jae(K, r->dur)) {
                r->dur = (float)((double)u->dur_scale * r->dur + u->sil_dur);
            }
        } else {
            const SenStat *st = GP(const SenStat, u->stats[r->phone]) + r->senone;
            for (int k = 0; k < r->nstates; k++) {
                if (x87_je(K, r->c[k])) r->c[k] = (float)sqrt((double)st->var);
                else if (!x87_jae(K, r->c[k])) r->c[k] = (float)(sqrt((double)st->var) * r->c[k]);
            }
            if (x87_je(K, r->dur)) r->dur = 1.0f;
            if (!x87_jbe(K, r->dur)) {
                r->dur = (float)((double)r->dur * (double)-0.001f);
            } else if (!x87_jbe(r->dur, 5.0)) {
                r->dur = (float)(((double)r->dur - 5.0) * st->scale * (double)0.001f);
            } else {
                r->dur = (float)((double)u->dur_scale * st->scale * r->dur);
                r->dur = (float)((double)r->dur * (double)0.001f);
            }
        }
    }
    unit_prosody_a(u, n);
    unit_prosody_b(u, n);
    out->bytes = n;
    if (u->final_sil && next_phone != u->sil_phone) out->bytes = n + 1;
    out->bytes *= 0x50;
    UnitOut *o = vc_malloc((size_t)(uint32_t)out->bytes);
    GPSET(out->units, o);
    recs = GP(UnitRec, u->recs);
    const SenoneTree *tree = GP(const SenoneTree, u->tree);
    for (int i = 0; i < n; i++, o++) {
        const UnitRec *r = recs + i;
        strcpy(o->name, GP(const char, named_item(GP(const NamedTable, u->phone_names), r->phone)));
        if (r->phone == u->sil_phone) {
            o->unit = 0;
        } else {
            int16_t s = GP(int16_t, u->senones)[i];
            if (s > tree->max_senone) o->unit = s;
            else o->unit = (int16_t)(GP(const uint16_t, tree->phone_units)[r->phone * 2] - (uint16_t)u->senone_base +
                                     (uint16_t)r->senone + 1);
        }
        o->nstates = r->nstates;
        o->dur = r->dur;
        for (int k = 0; k < o->nstates; k++) {
            o->a[k] = r->a[k];
            o->b[k] = (int16_t)x87_ftol((double)r->b[k]);
            o->c[k] = (float)((double)r->c[k] * scale);
        }
    }
    if (u->final_sil && next_phone != u->sil_phone) {
        strcpy(o->name, "SIL");
        o->unit = 0;
        o->nstates = 0;
        memcpy(&o->dur, &u->sil_dur, 4);
        o->a[0] = 1.0f;
        o->b[0] = o[-1].b[o[-1].nstates - 1];
        o->c[0] = 0.0f;
    }
    *last = recs[n - 1].phone;
    return 0;
}

LAYOUT(UnitStage, last_c, 0x2b8);

// @0x63686c7d: fill in the unspecified (0) b[] targets of every state by interpolating in time between
// the specified ones, inserting a state at a unit's start (a == 0 there) and end (a == 1) where needed.
// Written as the original's control flow (labels = its basic blocks) because the running time lives on
// the x87 stack across them and is sometimes re-read from a float copy: the roundings are the point.
void unit_prosody_a(UnitStage *u, int n) {
    UnitRec *R;
    const double Z = 0.0;
    float a28 = 0.0f, T10 = 0.0f, Tn = 0.0f, Tc, bval, last;
    int i = 0, s = 0, cur = 0, carry = 0, last_i = n - 1;
    int found, i_end, s_end, j, lim, cnt, found2, i2;
    double t = 0.0;
top:
    R = GP(UnitRec, u->recs);
    if (i < last_i) goto a;
    if (i != last_i) goto end;
    if (s < R[n - 1].nstates) goto a;
    if (R[n - 1].nstates > 1) goto end;
a:
    found = 0;
    T10 = (float)t;
    if (i >= n) goto b;
    for (;;) {
        if (s < R[i].nstates) {
            for (;;) {
                if (!x87_je(R[i].b[s], Z)) { found = 1; break; }
                s++;
                if (s >= R[i].nstates) break;
            }
        }
        if (found) goto f;
        if (i + 1 != n) s = 0;
        t = t + R[i].dur;
        i++;
        if (i >= n) break;
    }
b:
    if (!found) goto g;
f: {
        double v;
        if (!x87_je(Z, R[i].a[s])) {
            v = (double)R[i].dur * R[i].a[s];
            Tn = (float)(v + t);
        } else if (i == 0) {
            Tn = (float)t;
        } else {
            if (R[i - 1].nstates == 0) v = R[i - 1].dur;
            else v = (double)R[i - 1].a[R[i - 1].nstates - 1] * R[i - 1].dur;
            Tn = (float)(v + t);
        }
        bval = R[i].b[s];
        if (u->last_b == -1) u->last_b = x87_ftol(R[i].b[s]);
        goto m;
    }
g:
    Tn = (float)t;
    if (u->last_b == -1) u->last_b = 0x6e;
    i = last_i;
    bval = (float)(double)u->last_b;
    s = R[last_i].nstates;
m:
    s_end = s;
    t = T10;
    Tc = T10;
    i_end = i;
    last = -1.0f;
    j = cur;
    if (cur > i_end) goto nn;
    for (;;) {
        R = GP(UnitRec, u->recs);
        UnitRec *r = R + j;
        int nst = r->nstates;
        if (nst == 0) {
            r->nstates = nst + 1;
            r->a[0] = 0.0f; r->b[0] = 0.0f; r->c[0] = 0.0f;
            if (j == i_end) s_end++;
        } else if (!x87_je(Z, r->a[0])) {
            if (nst == 5) r->nstates = 4;
            memmove(&r->a[1], &r->a[0], (size_t)(uint32_t)(r->nstates * 4));
            memmove(&r->b[1], &r->b[0], (size_t)(uint32_t)(r->nstates * 4));
            memmove(&r->c[1], &r->c[0], (size_t)(uint32_t)(r->nstates * 4));
            r->a[0] = 0.0f; r->b[0] = 0.0f; r->c[0] = 0.0f;
            r->nstates++;
            if (j == i_end) s_end++;
            t = Tc;
        }
        nst = r->nstates;
        if (!x87_je(r->a[nst - 1], 1.0)) {
            if (nst == 5) r->nstates = 4;
            r->a[r->nstates] = 1.0f;
            r->b[r->nstates] = 0.0f;
            r->c[r->nstates] = 0.0f;
            r->nstates++;
        }
        lim = j == i_end ? s_end : r->nstates;
        int ecx = carry;
        if (carry < lim) {
            cnt = lim - carry;
            for (int k = carry; cnt; k++, cnt--) {
                if (!x87_je(Z, r->b[k])) {
#ifdef DECOMP_HOOK
                    decomp_warn("unit_prosody_a: a specified b[] inside an interpolated stretch (the original calls DebugBreak)");
#endif
                    t = Tc;
                    continue;
                }
                double P0 = (double)u->last_b;
                double q = ((double)r->dur * r->a[k] + t - a28) * ((double)bval - P0);
                r->b[k] = (float)(q / ((double)Tn - a28) + P0);
                last = r->b[k];
            }
            ecx = lim;
        }
        carry = 0;
        if (ecx == r->nstates) {
            double x = (double)r->dur + t;
            Tc = (float)x;
            t = x;
        }
        j++;
        if (j > i_end) break;
    }
nn:
    if (!x87_je(-1.0, last)) u->last_b = x87_ftol(last);
    i2 = i_end;
    s = s_end;
    found2 = 1;
    R = GP(UnitRec, u->recs);
    while (i2 < n) {
        UnitRec *r = R + i2;
        int nst = r->nstates;
        if (nst == 0 || !x87_je(Z, r->a[0])) break;
        while (s < nst) {
            if (x87_je(Z, r->b[s])) { found2 = 0; break; }
            u->last_b = x87_ftol(r->b[s]);
            a28 = (float)((double)r->dur * r->a[s] + t);
            s++;
        }
        if (!found2) break;
        if (!x87_je(1.0, r->a[s - 1])) break;
        i2++;
        if (i2 != n) s = 0;
        t = t + r->dur;
    }
    carry = s;
    cur = i2;
    i = i2;
    goto top;
end:
    R = GP(UnitRec, u->recs);
    if (u->sil_phone == R[n - 1].phone) u->last_b = -1;
}
