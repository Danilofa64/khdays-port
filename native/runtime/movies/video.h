/* The game's movies: ov024 decodes a frame (Ov024_MobiClip_DecodeFrame) and blits it into 15-bit
 * pixels (Ov024_MobiClip_BlitFrame); prepare.py's hooks call in here around both. */
#ifndef KHDAYS_VIDEO_H
#define KHDAYS_VIDEO_H

#include "../../hal/hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Around the native decoder's call, where the C called the payload copied
 * to the ITCM (for KHDAYS_PROFILE). */
void khdays_video_decode_begin(void);
void khdays_video_decode_end(void);

/* The frame decoder (MobiClip_DecodeFrameCore, the decomp's portable C++)
 * on the game's decoder state: its pointers are the game's references
 * (window.h), the decoder's its host addresses, so the state is handed to it
 * as its own and what it changes is brought back. Returns the bytes the
 * frame used. */
int khdays_video_decode(void *state);

/* Around Ov024_MobiClip_BlitRows: `height` rows of `stride` bytes written at
 * `destination`. */
void khdays_video_blit_begin(u32 destination, u32 stride, u32 height);
void khdays_video_blit_end(void);

/* After a blit: the movie's picture, the port's own -- the frame kept where it went in VRAM, the
 * planes converted at full precision (the blit's YCoCg equations in 8 bits a channel, no 5-bit ramp,
 * no dither) into a changing replacement (textures.h) drawn over the blitted pixels while they are
 * still there. Not with the Video setting `movies = ds`. */
void khdays_video_frame_blitted(u32 request);

/* For the 2D's HD map (tiles2d.c): whether a kept movie frame is in VRAM
 * bank `bank` at `offset` and the 32 bytes there (`bytes`) are still what
 * the blit wrote; then `*entry` is its picture's id + 1 in bits 20-31 and
 * the 32-byte unit's number from the frame's start in bits 0-19. */
int khdays_video_unit(int bank, u32 offset, const u8 *bytes, u32 *entry);

/* Whether any movie frame is kept (the HD map then looks for them). */
int khdays_video_kept(void);

/* KHDAYS_PROFILE, once a second (events.c): `video: N frames, decode X ms, blit Y ms`. */
void khdays_video_second(void);

/* After an engine's 2D is described (display.c): with the subtitles off, a movie's subtitle layers
 * left out. The movie's engine is set up by Ov024_MobiClip_SetUpMainEngine/SubEngine: the picture in
 * BG3, the subtitles' text in BG0 and its shadow in BG1 and BG2; then BG0-BG2 do not show. */
void khdays_video_subtitles_out(const u8 *registers, int engine, u32 *desc);

#ifdef __cplusplus
}
#endif

#endif
