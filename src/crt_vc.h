// The Microsoft C runtime (MSVCRT, VC 5 era) behaviour the engine depends on, reproduced exactly:
// where the algorithm itself shows in the output (qsort's order of equal elements, rand's sequence).
#pragma once
#include <stddef.h>
// qsort exactly as the VC CRT does it (median-less quicksort, shortsort below 9 elements): elements
// that compare equal end up where the original put them.
void vc_qsort(void *base, size_t num, size_t width, int (*cmp)(const void *, const void *));
