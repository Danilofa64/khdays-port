/* A frame of a screen's 2D as the port describes it: the layers, sprites, windows and effects the game
 * asked for, already decoded, and the memory they read. Built at VBlank (describe.c) and composed by
 * compose.h on the CPU and the GPU. The description is words (KD_* their offsets), so the GPU reads it
 * as it is; plain C, its #defines included by the shaders too (docs/RENDERING.md). */
#ifndef KHDAYS_GFX_FRAME_H
#define KHDAYS_GFX_FRAME_H

#include "elements.h"

/* --- A screen's description, in 32-bit words ------------------------------------ */

#define KD_FLAGS 0 /* KDF_* */
#define KDF_DISPLAY_MASK   0x3u      /* 0 off, 1 the layers, 2 a VRAM bank shown, 3 main memory */
#define KDF_FORCED_BLANK   0x4u
#define KDF_BG0_IS_3D      0x8u
#define KDF_OBJ_ON         0x10u
#define KDF_BG_ON_SHIFT    8         /* 4 bits: which layers show */
#define KDF_WIN_ON_SHIFT   12        /* 3 bits: window 0, window 1, the sprite window */
#define KDF_ENGINE_B       0x10000u
#define KDF_FINE           0x20000u  /* set by the host: the 3D has 8-bit channels (HD textures), all is
                                        composed in 8 bits and kd_pixel gives 8-bit channels (not the DS) */
#define KDF_SPLIT          0x40000u  /* set by the host for the one-screen picture: kd_pixel
                                        gives the 2D apart from the 3D, no master brightness (kd_split) */

#define KD_OBJ 1 /* how sprites find their tiles (KDO_*) */
#define KDO_TILE_1D        0x1u
#define KDO_TILE_STEP_SHIFT 1        /* 2 bits: a tile number steps 32 << n bytes (1D) */
#define KDO_BITMAP_1D      0x8u
#define KDO_BITMAP_STEP256 0x10u     /* 1D bitmaps: a tile number steps 256 bytes, else 128 */
#define KDO_BITMAP_WIDE    0x20u     /* 2D bitmaps: rows of 256 pixels, else 128 */
#define KDO_OBJ_EXT_PAL    0x40u
#define KDO_BG_EXT_PAL     0x80u

#define KD_BACKDROP 2 /* BGR555 */
#define KD_MASTER   3 /* bits 0-4 the factor (0-16), bits 14-15 the mode: 1 brighter, 2 darker */
#define KD_BLEND    4 /* bits 0-5 first targets, 8-13 second targets, 16-17 the effect (1 blend, 2 brighter, 3 darker) */
#define KD_COEF     5 /* bits 0-4 EVA, 8-12 EVB, 16-20 EVY, each 0-16 */
#define KD_WIN0X    6 /* KDW_*: window 0's columns */
#define KD_WIN1X    7
#define KDW_NONE    0x80000000u      /* shows nowhere (X1 = X2 = 0) */
#define KDW_WRAPS   0x40000000u      /* X1 > X2: from X1 to the end and from 0 to X2 */
/* otherwise x1 in bits 0-8 and x2 (1-256) in bits 16-24 */
#define KD_WININ    8 /* bits 0-5 what window 0 lets show, 8-13 window 1 */
#define KD_WINOUT   9 /* bits 0-5 outside the windows, 8-13 the sprite window */
#define KD_MOSAIC  10 /* the layers' mosaic: bits 0-4 width (1-16), 8-12 height */
#define KD_HOFS3D  11 /* the 3D layer's horizontal scroll, over 512 dots */
#define KD_VRAM    12 /* the bank a VRAM display shows (0-3: A-D) */

#define KD_LAYERS      16 /* 4 layers of KD_LAYER_WORDS */
#define KD_LAYER_WORDS 12
#define KDL_KIND   0 /* KDK_*, with bits 8-9 the priority and KDL_* flags */
#define KDK_NONE          0
#define KDK_TEXT          1
#define KDK_AFFINE        2
#define KDK_AFFINE_EXT    3  /* 16-bit map entries (flips, palettes) */
#define KDK_BITMAP256     4
#define KDK_BITMAP_DIRECT 5
#define KDK_LARGE_BITMAP  6
#define KDK_3D            7
#define KDL_256COLOURS   0x1000u
#define KDL_EXT_SLOT_SHIFT 13        /* 3 bits: the extended palette slot + 1, 0 the standard palette */
#define KDL_WRAP         0x10000u
#define KDL_MOSAIC       0x20000u
#define KDL_WIDTH  1 /* pixels */
#define KDL_HEIGHT 2
#define KDL_CHARS  3 /* where its tiles are, bytes into the layers' memory */
#define KDL_MAP    4 /* where its map is */
#define KDL_HOFS   5
#define KDL_VOFS   6
#define KDL_REFX   7 /* affine: the reference point, 8 fractional bits, signed */
#define KDL_REFY   8
#define KDL_PAPB   9 /* pa in bits 0-15, pb in 16-31, signed 8.8 */
#define KDL_PCPD  10
#define KDL_BITMAP 11 /* a bitmap's start, bytes into the layers' memory */

#define KD_WINY 64 /* each window's lines: 6 words of 32 lines, window 0 then window 1 */

#define KD_SPRITES      80 /* 128 sprites of KD_SPRITE_WORDS, in their order */
#define KD_SPRITE_WORDS 6
#define KDS_X    0 /* signed */
#define KDS_BOX  1 /* bits 0-7 y (0-255), 8-15 box width, 16-23 box height (doubled for a double-size affine one) */
#define KDS_SIZE 2 /* bits 0-7 width, 8-15 height, and KDS_* flags */
#define KDS_SHOWN       0x10000u
#define KDS_AFFINE      0x20000u
#define KDS_HFLIP       0x40000u
#define KDS_VFLIP       0x80000u
#define KDS_256COLOURS  0x100000u
#define KDS_MODE_SHIFT  21           /* 2 bits: 0 normal, 1 semi-transparent, 2 the sprite window, 3 bitmap */
#define KDS_PRIO_SHIFT  23           /* 2 bits */
#define KDS_TILE 3 /* bits 0-9 the first tile, 12-15 the palette */
#define KDS_PAPB 4
#define KDS_PCPD 5

/* Widescreen (engine A with the 3D): how many DS columns the picture has
 * beside the DS's 256 on each side (0: none), and each 8x8 cell's side, 2
 * bits a cell (0 stays, 1 moved left, 2 moved right), 16 cells a word, row
 * by row (describe.h khdays_gfx_describe_wide). */
#define KD_WIDE  (KD_SPRITES + 128 * KD_SPRITE_WORDS)
#define KD_SIDES (KD_WIDE + 1)

/* The port's elements on the screen (gfx/elements.h): how many
 * there are, KE_WORDS each, after the description and its palettes on the
 * graphics card (KD_ELEMENT). */
#define KD_ELEMENTS (KD_SIDES + 48)
/* 1 when what the parts that are on stand for is left out of its slot: after
 * the elements, a bit for each 32 bytes of the layers' memory (engine A's
 * 512 KB: 512 words; B's 128 KB: 128), then one for each of the 128
 * sprites (4 words). */
#define KD_LEFT_OUT (KD_ELEMENTS + 1)

#define KHDAYS_DESC_WORDS (KD_LEFT_OUT + 1)

/* --- The memory a screen's layers and sprites read ----------------------------- */

/* Byte sizes; engine B's layers and sprites have 128 KB. */
#define KHDAYS_GFX_BG_BYTES      0x80000u /* the layers' tiles, maps and bitmaps */
#define KHDAYS_GFX_OBJ_BYTES     0x40000u /* the sprites' tiles and bitmaps */
#define KHDAYS_GFX_BGEXT_BYTES   0x8000u  /* four extended palette slots of 8 KB */
#define KHDAYS_GFX_OBJEXT_BYTES  0x2000u
#define KHDAYS_GFX_PAL_BYTES     0x400u   /* the layers' 256 colours, then the sprites' */
#define KHDAYS_GFX_VRAM_BYTES    0x20000u /* the bank a VRAM display shows */

#endif
