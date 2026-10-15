/* The text part: each NitroSystem character
 * canvas the game shows becomes an element where the game shows the canvas,
 * its image being what VRAM holds of the canvas, drawn at the internal
 * resolution.
 *
 * The functions below are what the game's hooks call (native/tools/
 * prepare.py UI_HOOKS), handed what the game's routines are: host pointers
 * to the game's structures, which lie in the game's memory with the DS's
 * layout (their pointers are references). */
#ifndef KHDAYS_UI_TEXT_H
#define KHDAYS_UI_TEXT_H

#include <stdint.h>

/* Registers the part's elements (at start). */
void khdays_ui_text_init(void);

/* What the hooks follow: a text renderer attached to a layer
 * (TileTextRenderer_Init) and let go (TileTextRenderer_Destroy); a canvas
 * added to a tile surface (TileSurface_AddCanvas); a block of the game's
 * heaps freed (NNS_FndFreeToExpHeap: a canvas whose buffer it was is gone);
 * and characters loaded into a BG layer's memory (GX_LoadBG0Char-BG3Char,
 * GXS_LoadBG0Char-BG3Char: both the transfer queue's loads and the
 * routines' own) or into the sprites' (GX_LoadOBJ, GXS_LoadOBJ). */
void khdays_ui_text_renderer(const void *renderer, int layer, const void *resource, const void *frame, int result);
void khdays_ui_text_renderer_gone(const void *renderer, int result);
void khdays_ui_text_surface_canvas(const void *surface, int current, const void *canvas_object);
void khdays_ui_text_freed(const void *heap, const void *block);
void khdays_ui_text_chars_a0(const void *src, unsigned int offset, unsigned int size);
void khdays_ui_text_chars_a1(const void *src, unsigned int offset, unsigned int size);
void khdays_ui_text_chars_a2(const void *src, unsigned int offset, unsigned int size);
void khdays_ui_text_chars_a3(const void *src, unsigned int offset, unsigned int size);
void khdays_ui_text_chars_b0(const void *src, unsigned int offset, unsigned int size);
void khdays_ui_text_chars_b1(const void *src, unsigned int offset, unsigned int size);
void khdays_ui_text_chars_b2(const void *src, unsigned int offset, unsigned int size);
void khdays_ui_text_chars_b3(const void *src, unsigned int offset, unsigned int size);
void khdays_ui_text_obj_a(const void *src, uint32_t offset, uint32_t size);
void khdays_ui_text_obj_b(const void *src, uint32_t offset, uint32_t size);

#endif
