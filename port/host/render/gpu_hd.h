/* The replacement graphics (runtime/mods/textures.h) on the graphics card: the mods' 3D textures, 2D
 * tile sheets and models' textures in one buffer both passes read -- a table of four words per
 * replacement, then the texels, a still picture's followed by smaller copies (info bit 0x8)
 * (docs/RENDERING.md, "On the graphics card"). */
#ifndef KHDAYS_GPU_HD_H
#define KHDAYS_GPU_HD_H

#include <SDL3/SDL.h>

/* Creates the buffer on `device`. Opening it again on the same device does nothing. */
int khdays_gpu_hd_open(SDL_GPUDevice *device);
void khdays_gpu_hd_close(void);

/* Uploads the replacements the card does not have yet, and the ones whose
 * pixels changed, in a copy pass of their own in `cmd` (call it before the
 * passes that read them). When the buffer must grow it is made anew and
 * everything is uploaded again. Returns 0 if that failed. */
int khdays_gpu_hd_sync(SDL_GPUCommandBuffer *cmd);

SDL_GPUBuffer *khdays_gpu_hd_buffer(void);

#endif
