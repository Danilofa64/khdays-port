/* The crowns on the story's day list (crowns.h): the port puts the list sheet's gold crown right of
 * a day whose missions all have the cleared seal, every chest and every badge (the game shows those
 * only on the list a day opens). docs/RUNTIME_MAP.md section 14, "The crowns on the story's day
 * list". */
#include "crowns.h"
#include "elements.h"
#include "mission_list.h"
#include "parts.h"
#include "../runtime/core/window.h"
#include "../runtime/menus/camp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the game's */
extern int GameState_GetField(int field, int bits);
extern int GameState_IsFlagSet(unsigned int flag);
extern int GetLanguage(void);
int khdays_overlay_loaded(int id); /* overlays.c */

#define OV_STORY_CAMP 25
#define HOLO_MISSIONS 1         /* the camp's screen (khdays_camp_screen) */
#define CAMP 0x020b5744u        /* its second word: the story camp's context (Ov025_GetPageB) */
#define PAGE_B 0x95a0u          /* the day list (Ov025ScrollList) */
#define LIST_ROWS 0x4u
#define LIST_COUNT 0x8u
#define LIST_SCROLL 0x2d0u      /* dots, 16 a row */
#define ROW_SIZE 0x10u          /* its first day, its last day, ... */
#define FIELD_STATUS 0x28e4     /* 3: cleared */
#define FIELD_BADGES 0x92b

/* The crown: the gold one of the list sheet's four (characters 0x40-0x47 over 0x60-0x67), 16 x 16,
 * inside a row's plate's right end; only the rows the list shows, from y 16 to 176. The list is on
 * engine B, its row text in BG1 over the plates in BG2 and BG3. */
#define CROWN_CHARACTER 0x46
#define CROWN_X 195
#define CROWN_Y 17
#define ROW_HEIGHT 16
#define LIST_TOP 16
#define LIST_BOTTOM 176
#define CROWN_LAYER 1

static int image = -1, image_scale;
static int trace = -1;

static uint32_t rd32(uint32_t a)
{
    return *(const volatile uint32_t *)KHDAYS_W(a);
}

/* Whether mission `m` of `room` is done with all its chests and badges. */
static int mission_complete(int language, uint16_t room, uint16_t m)
{
    const unsigned kinds = khdays_mission_badges(language, room, m);
    const unsigned found = (unsigned)GameState_GetField(FIELD_BADGES + 4 * m, 4);
    const unsigned ranks = (kinds & 1u) + ((kinds >> 1) & 1u);
    int chests;
    const int *flag = khdays_mission_chests(room, &chests);
    if (chests < 0 || GameState_GetField(FIELD_STATUS + 3 * m, 3) != 3) return 0;
    for (int k = 0; k < chests; ++k) {
        if (!GameState_IsFlagSet((unsigned int)flag[k])) return 0;
    }
    /* as Ov025_DrawMissionRow lights them: the ordeal badge and the blazon by their count (bits 0
     * and 1), the unity badge bit 3 */
    for (unsigned i = 0; i < ranks; ++i) {
        if (!((found >> i) & 1u)) return 0;
    }
    return !(kinds & 8u) || (found & 8u);
}

/* Whether the days from `first` to `last` show missions, all of them complete: those the day list
 * finds in the span (Ov025_MissionList_HasVisibleMissionInDays: its first day on or after `first`,
 * its last on or before `last`, not hidden). */
static int days_complete(int language, uint32_t first, uint32_t last)
{
    int n, missions = 0;
    const KhdaysMissionRecord *r = khdays_mission_records(language, &n);
    for (int i = 0; i < n; ++i) {
        if (r[i].kind != 0 || !r[i].counted || r[i].first_day < first || r[i].last_day > last) continue;
        if (!mission_complete(language, r[i].room, r[i].mission)) return 0;
        ++missions;
    }
    return missions > 0;
}

static int crown_image(int scale)
{
    if (image < 0 || image_scale != scale) {
        const uint32_t *colours = khdays_mission_list_colours();
        uint8_t cell[4][64];
        uint32_t *texels;
        khdays_ui_image_put(image);
        image = -1;
        for (int c = 0; c < 4; ++c) {
            if (colours == NULL || !khdays_mission_list_character(CROWN_CHARACTER + (unsigned)(c & 1) + (c >> 1) * 0x20u,
                                                                  cell[c])) {
                return -1;
            }
        }
        image = khdays_ui_image_get(16 * scale, 16 * scale);
        image_scale = scale;
        texels = image >= 0 ? khdays_ui_image_begin(image) : NULL;
        if (texels == NULL) return -1;
        for (int y = 0; y < 16 * scale; ++y) {
            for (int x = 0; x < 16 * scale; ++x) {
                const int dx = x / scale, dy = y / scale;
                texels[(size_t)y * (size_t)(16 * scale) + (size_t)x] =
                    colours[cell[(dy / 8) * 2 + dx / 8][(dy % 8) * 8 + dx % 8]];
            }
        }
        khdays_ui_image_end(image);
    }
    return image;
}

void khdays_ui_crowns_emit(void)
{
    static int told = -1;
    uint32_t ctx, list, rows, count;
    int story, language, scale, shown = 0;
    if (trace < 0) trace = getenv("KHDAYS_TRACE_CROWNS") != NULL;
    /* the check compares the game's own drawing, which has none of this */
    if (khdays_ui_mode(KHDAYS_UI_HUD) != KHDAYS_UI_ON || !khdays_overlay_loaded(OV_STORY_CAMP)) return;
    if (khdays_camp_screen(&story) != HOLO_MISSIONS || !story) return;
    ctx = rd32(CAMP + 4);
    list = khdays_game_ram(ctx) ? rd32(ctx + PAGE_B) : 0u;
    if (!khdays_game_ram(list)) return;
    rows = rd32(list + LIST_ROWS);
    count = rd32(list + LIST_COUNT);
    if (count == 0 || !khdays_game_ram(rows)) return;
    scale = khdays_ui_scale();
    language = GetLanguage();
    for (uint32_t r = 0; r < count; ++r) {
        const uint32_t first = rd32(rows + r * ROW_SIZE), last = rd32(rows + r * ROW_SIZE + 4);
        const int y = CROWN_Y + ROW_HEIGHT * (int)r - (int)rd32(list + LIST_SCROLL);
        KhdaysUiElement e;
        const int top = y - (CROWN_Y - LIST_TOP); /* the row's */
        if (top < LIST_TOP || top + ROW_HEIGHT > LIST_BOTTOM || !days_complete(language, first, last)) continue;
        if (crown_image(scale) < 0) return;
        e.id = "hud.crown";
        e.screen = 1;
        e.slot = KES_DIRECT | CROWN_LAYER;
        e.x = (float)CROWN_X;
        e.y = (float)y;
        e.w = 16.0f;
        e.h = 16.0f;
        e.image = image;
        khdays_ui_emit(KHDAYS_UI_HUD, &e);
        ++shown;
    }
    if (trace && shown != told) {
        fprintf(stderr, "crowns: %d crowns on the day list's %u rows (scroll %u)\n", shown, count,
                rd32(list + LIST_SCROLL));
        for (uint32_t r = 0; r < count; ++r) {
            const uint32_t first = rd32(rows + r * ROW_SIZE), last = rd32(rows + r * ROW_SIZE + 4);
            fprintf(stderr, "crowns:   days %u-%u: %s\n", first, last,
                    days_complete(language, first, last) ? "complete" : "not complete");
        }
        told = shown;
    }
}
