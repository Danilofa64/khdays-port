/* NitroSDK's MATH_QSort (0x0200fc00, called from Ov003_RankPlayers), which the decompilation keeps as
 * ARM assembly, translated: a quicksort without recursion on a stack of ranges, its pivot, partition,
 * swaps and signed comparisons as the routine's (docs/PLATFORM.md, "The HAL's translations"). */
#include "hal.h"


typedef s32 (*KhdaysQsortCompare)(void *a, void *b);

static u32 bits(u32 v)
{
    u32 n = 0;
    while (v != 0) {
        ++n;
        v >>= 1;
    }
    return n; /* 32 - clz(v) */
}

static void swap(u8 *a, u8 *b, u32 width)
{
    for (u32 i = 0; i < width; ++i) {
        const u8 t = a[i];
        a[i] = b[i];
        b[i] = t;
    }
}

void Util_QuickSortWithWork(void *head, u32 num, u32 width, KhdaysQsortCompare compare, void *stack_buffer)
{
    u32 own[64]; /* 32 - clz(num) ranges at most: 32 */
    u32 *stack, *base;
    const u32 shift = bits(width);
    if ((s32)num <= 1) {
        return;
    }
    base = stack = stack_buffer != 0 ? (u32 *)stack_buffer : own;
    *stack++ = (u32)head;
    *stack++ = (u32)((u8 *)head + (num - 1) * width);
    while (stack != base) {
        u8 *const right = (u8 *)*--stack;
        u8 *const left = (u8 *)*--stack;
        u8 *l, *r;
        if ((u32)(right - left) == width) {
            if (compare(left, right) > 0) {
                swap(left, right, width);
            }
            continue;
        }
        swap(left + (((u32)(right - left) >> shift) * width), left, width);
        l = left + width;
        r = right;
        for (;;) {
            while ((s32)l < (s32)right && compare(l, left) < 0) {
                l += width;
            }
            while (compare(r, left) > 0) {
                r -= width;
            }
            if ((s32)l >= (s32)r) {
                break;
            }
            swap(l, r, width);
            l += width;
            r -= width;
            if ((s32)l > (s32)r) {
                break;
            }
        }
        swap(left, r, width);
        if ((s32)(r - left) > (s32)(right - r)) {
            if ((s32)left < (s32)(r - width)) {
                *stack++ = (u32)left;
                *stack++ = (u32)(r - width);
            }
            if ((s32)(r + width) < (s32)right) {
                *stack++ = (u32)(r + width);
                *stack++ = (u32)right;
            }
        } else {
            if ((s32)(r + width) < (s32)right) {
                *stack++ = (u32)(r + width);
                *stack++ = (u32)right;
            }
            if ((s32)left < (s32)(r - width)) {
                *stack++ = (u32)left;
                *stack++ = (u32)(r - width);
            }
        }
    }
}
