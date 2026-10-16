/* The camp's mission list as the game's files hold it, for the parts that add to it (badges.c,
 * crowns.c): its records (UI/cm/msl_<language>.msi.z), the rooms' chests (mi/mi/trbox) and the
 * characters and palette of its sheet (UI/cm/cm.p2, entries 17 and 20). Read at VBlank, with a
 * reader of its own. */
#ifndef KHDAYS_UI_MISSION_LIST_H
#define KHDAYS_UI_MISSION_LIST_H

#include <stdint.h>

/* A record: a mission (kind 0) or one of its badges (kind 1 the ordeal badge, 2 the blazon, 4 the
 * unity badge), its room, the days the story's list shows it on (+6 to +8, as
 * Ov025_MissionList_HasVisibleMissionInDays reads them), whether it counts (+0xc is 1: hidden). */
typedef struct KhdaysMissionRecord {
    uint16_t room, mission;
    uint16_t first_day, last_day;
    uint8_t counted, kind;
} KhdaysMissionRecord;

/* The records in the game's language (GetLanguage's 1-5), read again when it changes; NULL and
 * *count 0 when the file is not there. */
const KhdaysMissionRecord *khdays_mission_records(int language, int *count);

/* The badges of `mission` played in `room`, as bits (kind - 1): 0 when the list has no such
 * mission there (a challenge's number, say). */
unsigned khdays_mission_badges(int language, uint16_t room, uint16_t mission);

/* The game flags of a room's chests, each found while its flag is set (the file's numbers + 9, as
 * Ov025_CountGroupFlagsSet reads them); NULL and *count 0 when the room has none, -1 when the
 * table is not there. */
const int *khdays_mission_chests(uint16_t room, int *count);

/* Character `c` of the list's sheet (its 8 x 8 palette indices, 8 bits a pixel) and the sheet's
 * palette as colours (RGBA, index 0 clear); 0 / NULL when they are not there. */
int khdays_mission_list_character(unsigned c, uint8_t out[64]);
const uint32_t *khdays_mission_list_colours(void);

#endif
