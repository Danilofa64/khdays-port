/* The categories (categories.h), by how the player's name in the sound archive starts: PLAYER_BGM*
 * and the stream players PLAYER_STRM_BGM and PLAYER_STRM music, PLAYER_SE/MAGIC/WEAPON/GIMMICK/
 * SE_EVENT effects, PLAYER_VO1-4 and PLAYER_STRM_V01 voices (docs/SOUND.md). */
#include "categories.h"

#include <stdio.h>
#include <string.h>

static const struct {
    int stream;          /* 0 a sequence player, 1 a stream player */
    const char *prefix;  /* the name starts with it */
    int category;
} rules[] = {
    {0, "PLAYER_BGM", KHDAYS_SOUND_MUSIC},
    {0, "PLAYER_SE", KHDAYS_SOUND_EFFECTS},
    {0, "PLAYER_MAGIC", KHDAYS_SOUND_EFFECTS},
    {0, "PLAYER_WEAPON", KHDAYS_SOUND_EFFECTS},
    {0, "PLAYER_GIMMICK", KHDAYS_SOUND_EFFECTS},
    {0, "PLAYER_VO", KHDAYS_SOUND_VOICES},
    {1, "PLAYER_STRM_V", KHDAYS_SOUND_VOICES},  /* before PLAYER_STRM, which it starts with */
    {1, "PLAYER_STRM_BGM", KHDAYS_SOUND_MUSIC},
    {1, "PLAYER_STRM", KHDAYS_SOUND_MUSIC},
};

int khdays_sound_category(const char *player_name, int stream)
{
    static char unknown[8][32];
    static int unknowns;
    for (size_t i = 0; i < sizeof(rules) / sizeof(rules[0]); ++i) {
        if (rules[i].stream == stream && strncmp(player_name, rules[i].prefix, strlen(rules[i].prefix)) == 0) {
            return rules[i].category;
        }
    }
    for (int i = 0; i < unknowns; ++i) {
        if (strcmp(unknown[i], player_name) == 0) {
            return KHDAYS_SOUND_EFFECTS;
        }
    }
    if (unknowns < 8) {
        snprintf(unknown[unknowns++], sizeof(unknown[0]), "%s", player_name);
    }
    fprintf(stderr, "sound: the %s player \"%s\" is none of music, effects or voices: counted as effects\n",
            stream ? "stream" : "sequence", player_name);
    return KHDAYS_SOUND_EFFECTS;
}
