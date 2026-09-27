// The thread-safe message queue between the engine's stages (records of 20 bytes, a critical
// section, and four events: not empty, empty, full (above the high-water mark) and drained). Layout
// as in the original.
#pragma once
#include "gptr.h"

typedef struct QItem { uint32_t v[5]; } QItem;
typedef struct Queue {
    uint32_t count;         // 0x00
    uint32_t cap_bytes;     // 0x04
    GPTR(QItem) items;      // 0x08
    int32_t full;           // 0x0c set at the high-water mark, cleared at a quarter of it
    int32_t _10;
    uint32_t high;          // 0x14 high-water mark
    uint8_t cs[24];         // 0x18 CRITICAL_SECTION
    uint32_t ev_nonempty;   // 0x30
    uint32_t ev_full;       // 0x34
    uint32_t ev_empty;      // 0x38
    uint32_t ev_room;       // 0x3c
} Queue;

int queue_grow(Queue *q, uint32_t n);                   // @0x63684899 thiscall: room for n (x1.5); 1 = ok
void queue_update_events(Queue *q);                     // @0x63684905 thiscall
uint32_t queue_count(Queue *q);                         // @0x6368498f thiscall (locked)
int queue_is_full(Queue *q);                            // @0x636849ab thiscall: count >= high (locked)
int queue_push(Queue *q, const QItem *it);              // @0x636849cd thiscall
int queue_push_n(Queue *q, const QItem *its, uint32_t n);   // @0x63684a29 thiscall
int queue_pop(Queue *q, QItem *out);                    // @0x63684aa1 thiscall
// locked reads of the first field of three other objects (their critical sections at +0x58, +0x11c,
// +0x2cc)
uint32_t locked_get_58(void *obj);                      // @0x6368447a thiscall
uint32_t locked_get_11c(void *obj);                     // @0x6368225f thiscall
uint32_t locked_get_2cc(void *obj);                     // @0x63687cfd thiscall
