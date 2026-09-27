#include "senone.h"
#include "vcrt.h"
#include <string.h>

LAYOUT(SenoneTree, qlists, 0x60);
LAYOUT(SenoneTree, roots, 0x4c);
LAYOUT(PhoneSet, count, 0x28);
LAYOUT(SenoneHeader, bit_offset, 0x20);

static int is_sil(const char *name) { return name[0] == '+' || vc_strnicmp(name, "SIL", 3) == 0; }

int senone_lookup(const SenoneTree *t, int cur, int left, int right, int pos, int state) {
    const PhoneSet *ps = GP(const PhoneSet, t->phones);
    int n = ps->count;
    if (cur < 0 || cur >= n || left < 0 || left >= n || right < 0 || right >= n) return -2;
    int m = GP(const int8_t, t->model)[cur];
    if (state < 0 || state >= GP(const int8_t, t->nstates)[m]) return -2;
    const TreeRoot *root = GP(const TreeRoot, GP(GPTR(const TreeRoot), t->roots)[m]) + state;
    if (root->present == 0) return -1;
    uint32_t p;
    switch (pos) {
    case 'b': case 'B': p = 1; break;
    case 'e': case 'E': p = 2; break;
    case 's': case 'S': p = 4; break;
    case 0: p = 8; break;
    default: return -2;
    }
    if (t->ignore_position) p = 8;
    const GPTR(const char) *names = GP(GPTR(const char), ps->names);
    if (is_sil(GP(const char, names[left]))) left = t->sil;
    const char *rn = GP(const char, names[right]);
    if (is_sil(rn)) right = t->sil;
    else if (t->word_end_right >= 0 && (p == 2 || p == 4)) right = t->word_end_right;
    const SenoneHeader *h = GP(const SenoneHeader, t->hdr);
    int off = h->bit_offset, base = h->right_base;
    int L = left + off + 4, R = right + off + 4;
    int lw = L / 32;
    uint32_t lbit = 1u << (L % 32);
    uint32_t rbit = 1u << (R % 32);
    int rw = R / 32 + base;
    const uint32_t *qbits = GP(const uint32_t, t->qbits);
    const uint16_t *qlists = GP(const uint16_t, t->qlists);
    const int stride = t->qstride;
    const TreeNode *nodes = GP(const TreeNode, root->nodes), *node = nodes;
    if (off) p |= 1u << (state + 4);
    if (t->flags & 2) {
        int C = cur + off + 4;
        uint32_t cbit = 1u << (C % 32);
        int cw = C / 32 + base * 2;
        for (;;) {
            if (node->yes < 0) return node->no;
            int next = node->no;
            for (const uint16_t *q = qlists + node->qlist; *q != 0xffff; q++) {
                const uint32_t *row = qbits + stride * *q;
                if ((p & row[0]) == p && (row[lw] & lbit) && (row[rw] & rbit) && (row[cw] & cbit)) {
                    next = node->yes;
                    break;
                }
            }
            node = nodes + next;
        }
    }
    for (;;) {
        if (node->yes < 0) return node->no;
        int next = node->no;
        for (const uint16_t *q = qlists + node->qlist; *q != 0xffff; q++) {
            const uint32_t *row = qbits + stride * *q;
            if ((p & row[0]) == p && (row[lw] & lbit) && (row[rw] & rbit)) {
                next = node->yes;
                break;
            }
        }
        node = nodes + next;
    }
}

void phone_fixups(char *ph, int n) {
    static const char *const from[5] = { "DX", "AW0", "AY0", "EY0", "OY0" };
    static const char *const to[5] = { "T", "AW1", "AY1", "EY1", "OY1" };
    for (; n > 0; n--, ph += 100) {
        for (int k = 0; k < 5; k++) {
            if (vc_strcmp(ph, from[k]) == 0) {
                strcpy(ph, to[k]);
                break;
            }
        }
    }
}
