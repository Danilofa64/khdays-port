/* The field's HUD. */
#ifndef KHDAYS_UI_HUD_H
#define KHDAYS_UI_HUD_H

#include <stdint.h>

/* KHDAYS_TRACE_HUD's frames, read at start. */
void khdays_ui_hud_init(void);

/* Called at each VBlank. At the frames KHDAYS_TRACE_HUD lists, it reports
 * each text layer's groups of cells that show something, on stderr and in
 * the session log. */
void khdays_ui_hud_vblank(uint32_t frame);

#endif
