/* The shapes of the one-screen pass.
 * Each shape is a rectangle of one of the screens as the port composed it,
 * placed in the single picture (C x 192 DS dots), with its effects. For each
 * texel of the picture the pass tries the shapes in order; the first whose
 * destination holds the texel and whose source texel is visible gives it. A
 * shape is KO_WORDS words; floats are stored as their bits.
 *
 * Plain C, only #defines: the shaders include it too. */
#ifndef KHDAYS_GFX_ONESCREEN_H
#define KHDAYS_GFX_ONESCREEN_H

#define KO_WORDS 19

#define KO_FLAGS 0 /* KOF_* */
#define KOF_LOWER        0x1u    /* taken from the lower screen, else the upper one's 2D apart from its 3D */
#define KOF_MIRROR_X     0x4u    /* its source turned over left to right */
#define KOF_MIRROR_Y     0x8u    /* top to bottom */
#define KOF_REPEAT_X     0x40u   /* its source repeated along its rows, across every column */
#define KOF_REPEAT_Y     0x80u   /* the same down its columns, across every row */
#define KOF_FORCE        0x100u  /* its texels used where they are invisible too */
#define KOF_WEIGHT       0x800u  /* KO_OPACITY and KO_FADE weigh it against what is under it */
#define KOF_SEE_THROUGH  0x2000u /* KO_SEE_THROUGH: the texels of one colour drawn at an opacity */
#define KOF_DIM          0x4000u /* `dim`: its colours times KO_OPACITY's factor, toward black (not a weight) */
#define KOF_3D           0x8000u /* `3d`: the 3D at the texel (the backdrop where none is), not its source */

#define KO_SRC_X  1 /* float: the source rectangle, DS dots of its screen */
#define KO_SRC_Y  2
#define KO_SRC_W  3
#define KO_SRC_H  4
#define KO_DST_X1 5 /* float: the destination, DS dots of the one picture */
#define KO_DST_Y1 6
#define KO_DST_X2 7
#define KO_DST_Y2 8
#define KO_FADE   9  /* float x4: left, top, right, bottom, destination dots */
#define KO_OPACITY 13 /* float */
#define KO_KEYS    14 /* the first of its colours made transparent in the keys' buffer */
#define KO_KEY_COUNT 15
#define KO_SCALE_X 16 /* float: destination dots per source dot (the sampling's) */
#define KO_SCALE_Y 17
#define KO_SEE_THROUGH 18 /* a colour (BGR555, bits 0-14) its texels drawn at an opacity (bits 16-23, of 255) */

/* The pass's constants: the picture's width C in DS dots, the internal
 * resolution, the shapes' count, the frame (KOS_*), the brightness rule
 * (KOB_*) and each screen's master brightness (MASTER_BRIGHT). */
#define KOS_UPPER 0u /* C x 192: the 3D, the shapes over it; where neither, the upper screen's backdrop */
#define KOS_LOWER 1u /* C x 192: the same, the lower screen's backdrop */

#define KOB_DEFAULT 0u /* the upper screen's, the lower's while it fades to black */
#define KOB_UPPER   1u
#define KOB_LOWER   2u
#define KOB_HALVES  3u /* each half of the picture its screen's */
#define KOB_OWN     4u /* each shape its own screen's */

#endif /* KHDAYS_GFX_ONESCREEN_H */

/* --- The pass's steps for one output texel ----------------------------------------
 *
 * The colours are 8-bit channels, red in bits 0-7, compared with a key or a
 * see-through colour in the DS's 5 bits a channel (the game's colours are
 * the DS's, so a piece's colour is matched exactly). With KO_STEPS defined the
 * includer defines, before including it (again, after the constants):
 *
 *   KO_FN              how a function is declared (C: static inline; HLSL: nothing)
 *   KO_SHAPE(i, w)     word w of shape i (KO_*)
 *   KO_KEY(i)          colour i of the keys' buffer (BGR555)
 *   KO_ASFLOAT(u)      the float whose bits u holds
 *   KO_FLOOR(f) KO_ABS(i)  a float's floor, an int's magnitude
 *   KO_SOURCE(lower, x, y)  the source screen's texel at (x, y) DS dots
 *                      (floats): the upper's as kd_split gives it, the
 *                      lower's colour with its alpha 0 on the backdrop
 *
 * and the types `uint` and `int` (32 bits) and `float`. */
#if defined(KO_STEPS) && !defined(KHDAYS_GFX_ONESCREEN_STEPS)
#define KHDAYS_GFX_ONESCREEN_STEPS

KO_FN uint ko_channel(uint c, uint n)
{
    return (c >> (8u * n)) & 255u;
}

/* The master brightness (MASTER_BRIGHT: bits 0-4 the factor, 14-15 the mode) on 8-bit channels. */
KO_FN uint ko_bright(uint c, uint master)
{
    const uint mode = (master >> 14) & 3u;
    const uint factor = (master & 31u) > 16u ? 16u : master & 31u;
    uint acc = 0u, n;
    for (n = 0u; n < 3u; ++n) {
        uint v = ko_channel(c, n);
        if (mode == 1u) {
            v += (255u - v) * factor / 16u;
        } else if (mode == 2u) {
            v -= v * factor / 16u;
        }
        acc |= v << (8u * n);
    }
    return acc;
}

/* c x (1 - w) + d x w, 8-bit channels. */
KO_FN uint ko_mix(uint c, uint d, float w)
{
    uint acc = 0u, n;
    for (n = 0u; n < 3u; ++n) {
        const float v = (float)ko_channel(c, n) * (1.0f - w) + (float)ko_channel(d, n) * w;
        acc |= (uint)(v < 0.0f ? 0.0f : v > 255.0f ? 255.0f : v + 0.5f) << (8u * n);
    }
    return acc;
}

/* K + F x X (kd_split's texel over the 3D's colour X), clamped. */
KO_FN uint ko_over3d(uint k, uint x3d)
{
    const float f = (float)((k >> 24) - 1u) / 254.0f;
    uint acc = 0u, n;
    for (n = 0u; n < 3u; ++n) {
        const float v = (float)ko_channel(k, n) + f * (float)ko_channel(x3d, n);
        acc |= (uint)(v > 255.0f ? 255.0f : v + 0.5f) << (8u * n);
    }
    return acc;
}

KO_FN uint ko_bgr555(uint c)
{
    return (ko_channel(c, 0) >> 3) | ((ko_channel(c, 1) >> 3) << 5) | ((ko_channel(c, 2) >> 3) << 10);
}

KO_FN float ko_float(uint i, uint w)
{
    return KO_ASFLOAT(KO_SHAPE(i, w));
}

KO_FN float ko_clamp01(float v)
{
    return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
}

KO_FN float ko_clamp(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}
/* How many source texels one output texel covers along an axis of a shape
 * that scales its source by `scale` (destination dots per source dot): one
 * when it keeps or enlarges it, 1 / scale rounded up when it shrinks it. */
KO_FN uint ko_taps(float scale)
{
    return scale > 0.0f && scale < 1.0f ? (uint)(1.0f / scale + 0.999f) : 1u;
}

/* The output texel at (px, py), DS dots of the picture (its side `texel` DS
 * dots: 1 / the internal resolution): the first shape that gives it over the
 * 3D's colour x3d (8-bit channels; bit 24: the 3D shows there), else the 3D or
 * the backdrop; then the brightness of the rule.
 *
 * A shape that shrinks its source covers several of the source's texels with
 * one output texel; their colours are averaged (a box filter), so a line of
 * the game's text one texel thick keeps its share of the colour instead of
 * vanishing when it falls between two samples. Where only some
 * of them show, the others count as what is under the shape. A shape that
 * keeps or enlarges its source takes the one texel there, sharp, as before. A 3D
 * the DS shows over all its 2D (bit 25, its alpha 0-31 in bits 26-30: the
 * game over's orb, KHDAYS_G3D_OVER_2D) goes over the shape's texel with its
 * alpha, as the DS blends the 3D with the layer under it.
 * The 3D is engine A's: `a_upper` 1 when engine A is the upper screen
 * (POWCNT1), its shapes' texels then K + F x X. The DS draws one 3D, engine
 * A's, whichever screen engine A shows; the picture has it under everything
 * either way, so a 3D shown on the lower screen alone is the picture's too.
 *
 * The effects: a key colour's texels are not shown (what is under the shape
 * shows); a see-through colour's are mixed with what is under the shape at
 * their opacity; `dim` takes the colours toward black by a factor; the
 * weight (`opacity`, `fade`) mixes the shape with what is under it -- `fade`
 * from nothing at an edge to the whole shape over its length, the lesser of
 * the two edges of an axis, the two axes multiplied; `mirror` turns the
 * source over, `repeat` repeats it along its rows or columns, `force` shows
 * texels the 2D would leave invisible over the 3D. `masters`: the upper screen's MASTER_BRIGHT, the lower's in
 * bits 16-31; `backdrop`: the frame's own where nothing is (8-bit); `half`:
 * the picture's middle column. */
KO_FN uint ko_pixel(float px, float py, float texel, uint x3d, uint shape_count, uint rule, uint masters,
                    uint backdrop, float half, uint a_upper)
{
    const uint upper_master = masters & 0xffffu, lower_master = masters >> 16;
    uint i;
    uint under = ((x3d >> 24) & 1u) != 0u ? x3d & 0xffffffu : backdrop;
    uint result = under;
    int from = -1; /* the screen whose brightness the texel takes: -1 the 3D's or the backdrop's */
    for (i = 0u; i < shape_count; ++i) {
        const uint flags = KO_SHAPE(i, KO_FLAGS);
        const float x1 = ko_float(i, KO_DST_X1), y1 = ko_float(i, KO_DST_Y1);
        const float x2 = ko_float(i, KO_DST_X2), y2 = ko_float(i, KO_DST_Y2);
        const float sx = ko_float(i, KO_SCALE_X), sy = ko_float(i, KO_SCALE_Y);
        const float w = ko_float(i, KO_SRC_W), h = ko_float(i, KO_SRC_H);
        float fx, fy;
        /* the output texel's two sides in the source's dots, turned over as the shape turns it */
        float ax = texel / sx, ay = 0.0f, bx = 0.0f, by = texel / sy;
        uint colour;
        /* a repeated shape keeps its own rows (repeat x) or columns (repeat y) */
        if (!(((px >= x1 && px <= x2) || (flags & KOF_REPEAT_X) != 0u) &&
              ((py >= y1 && py <= y2) || (flags & KOF_REPEAT_Y) != 0u))) {
            continue;
        }
        if ((flags & KOF_3D) != 0u) {
            /* the picture's 3D itself where the shape is (a field behind a page, dimmed with `dim`) */
            result = (flags & KOF_DIM) != 0u ? ko_mix(0u, under, ko_float(i, KO_OPACITY)) : under;
            from = -1;
            break;
        }
        fx = (px - x1) / sx;
        fy = (py - y1) / sy;
        if ((flags & (KOF_REPEAT_X | KOF_REPEAT_Y)) != 0u) {
            const float lx = (x2 - x1) / sx, ly = (y2 - y1) / sy;
            if ((flags & KOF_REPEAT_X) != 0u && lx > 0.0f) {
                fx = fx - lx * KO_FLOOR(fx / lx);
            }
            if ((flags & KOF_REPEAT_Y) != 0u && ly > 0.0f) {
                fy = fy - ly * KO_FLOOR(fy / ly);
            }
        }
        if ((flags & KOF_MIRROR_X) != 0u) {
            fx = w - fx;
            ax = -ax;
        }
        if ((flags & KOF_MIRROR_Y) != 0u) {
            fy = h - fy;
            by = -by;
        }
        {
            /* the source texels the output texel covers, averaged (above) */
            const uint nx = ko_taps(sx), ny = ko_taps(sy), lower = (flags & KOF_LOWER) != 0u ? 1u : 0u;
            const uint from_a = (lower != 0u) == (a_upper == 0u) ? 1u : 0u;
            const float src_x = ko_float(i, KO_SRC_X), src_y = ko_float(i, KO_SRC_Y);
            float sum0 = 0.0f, sum1 = 0.0f, sum2 = 0.0f;
            uint shown_count = 0u, j, m, k;
            for (j = 0u; j < nx; ++j) {
                for (m = 0u; m < ny; ++m) {
                    const float u = ((float)j + 0.5f) / (float)nx - 0.5f, v = ((float)m + 0.5f) / (float)ny - 0.5f;
                    float qx = fx + u * ax + v * bx, qy = fy + u * ay + v * by;
                    uint got, shown, c = under;
                    if (nx * ny > 1u) {
                        /* the footprint stays inside the source rectangle, not on its neighbours */
                        qx = ko_clamp(qx, 0.0f, w - 0.001f);
                        qy = ko_clamp(qy, 0.0f, h - 0.001f);
                    }
                    got = KO_SOURCE(lower, src_x + qx, src_y + qy);
                    /* not shown over the 3D, or a colour made transparent: what is under the shape */
                    shown = (flags & KOF_FORCE) != 0u || (got >> 24) != 0u ? 1u : 0u;
                    for (k = 0u; k < KO_SHAPE(i, KO_KEY_COUNT); ++k) {
                        if (ko_bgr555(got) == KO_KEY(KO_SHAPE(i, KO_KEYS) + k)) {
                            shown = 0u;
                        }
                    }
                    if (shown != 0u) {
                        c = from_a != 0u && (got >> 24) != 0u ? ko_over3d(got, x3d) : got & 0xffffffu;
                        if ((flags & KOF_SEE_THROUGH) != 0u &&
                            ko_bgr555(got) == (KO_SHAPE(i, KO_SEE_THROUGH) & 0x7fffu)) {
                            /* that colour over what is under the shape, at its opacity */
                            c = ko_mix(under, c, (float)((KO_SHAPE(i, KO_SEE_THROUGH) >> 16) & 255u) / 255.0f);
                        }
                        ++shown_count;
                    }
                    sum0 += (float)ko_channel(c, 0u);
                    sum1 += (float)ko_channel(c, 1u);
                    sum2 += (float)ko_channel(c, 2u);
                }
            }
            if (shown_count == 0u) {
                continue; /* nothing of it shown here: the next shape's */
            }
            {
                const float n = (float)(nx * ny);
                colour = (uint)(sum0 / n + 0.5f) | ((uint)(sum1 / n + 0.5f) << 8) | ((uint)(sum2 / n + 0.5f) << 16);
            }
        }
        if ((flags & KOF_DIM) != 0u) {
            colour = ko_mix(0u, colour, ko_float(i, KO_OPACITY)); /* toward black, whatever is under it */
        }
        if ((flags & KOF_WEIGHT) != 0u) {
            float weight = ko_float(i, KO_OPACITY);
            const float f0 = ko_float(i, KO_FADE), f1 = ko_float(i, KO_FADE + 1);
            const float f2 = ko_float(i, KO_FADE + 2), f3 = ko_float(i, KO_FADE + 3);
            const float left = f0 == 0.0f ? 1.0f : ko_clamp01((px - x1) / f0);
            const float top = f1 == 0.0f ? 1.0f : ko_clamp01((py - y1) / f1);
            const float right = f2 == 0.0f ? 1.0f : ko_clamp01((x2 - px) / f2);
            const float bottom = f3 == 0.0f ? 1.0f : ko_clamp01((y2 - py) / f3);
            weight *= (left < right ? left : right) * (top < bottom ? top : bottom);
            colour = ko_mix(under, colour, weight);
        }
        result = colour;
        from = (flags & KOF_LOWER) != 0u ? 1 : 0;
        if (((x3d >> 25) & 1u) != 0u) {
            result = ko_mix(result, x3d & 0xffffffu, (float)((x3d >> 26) & 31u) / 31.0f);
        }
        break;
    }
    if (rule == KOB_UPPER) {
        return ko_bright(result, upper_master);
    }
    if (rule == KOB_LOWER) {
        return ko_bright(result, lower_master);
    }
    if (rule == KOB_HALVES) {
        return ko_bright(result, px < half ? upper_master : lower_master);
    }
    if (rule == KOB_OWN) {
        return from < 0 ? result : ko_bright(result, from == 1 ? lower_master : upper_master);
    }
    /* the default: the upper screen's, the lower's while it is dark ("down") */
    return ko_bright(result, ((lower_master >> 14) & 3u) == 2u ? lower_master : upper_master);
}

#endif /* KO_STEPS */
