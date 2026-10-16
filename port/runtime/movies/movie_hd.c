/* How an HD movie runs (movie_hd.h), as states: Idle, Playing, Held (the game's movie ended first),
 * Finishing (the video ended first), Skipping (START where the DS allows it); the credits scene held at
 * its roll's end instead. The game's thread brings the movie's events, the window's the video's and
 * START; one lock keeps them in order (docs/PLATFORM.md, "HD movies"). */
#include "movie_hd.h"

#include "../core/clock.h"
#include "../input/input.h"
#include "../core/rom.h"
#include "../core/session_log.h"
#include "../../host/app/host.h"
#include "../../host/movies/movie_hd.h"
#include "../../host/movies/movie_subtitles.h"
#include "../../host/app/sound_output.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

extern int GetLanguage(void);     /* the game's: gLangPath's language */
int khdays_language_wanted(void); /* language.c */

enum { IDLE, PLAYING, HELD, FINISHING, SKIPPING };

static SRWLOCK lock = SRWLOCK_INIT;
static volatile LONG state = IDLE;    /* read without the lock to see whether a course runs */
static char id[64];                   /* the movie's, as the game names it */
static int skippable = -1;            /* the DS lets START skip it: 1, 0, -1 not said yet */
static int skip_wanted;               /* START came before the movie said */
static int skipped;                   /* the game marked its skip */
static uint16_t game_last, host_last; /* the buttons last seen, for START's press */
static volatile LONG ended;           /* a course has just ended: the buttons held then are withheld */

/* state and ended read as they are written: atomically (the decoder's thread
 * and the game's both look; volatile alone is atomic only by MSVC's reading) */
static LONG state_now(void)
{
    return InterlockedCompareExchange(&state, 0, 0);
}

static LONG ended_now(void)
{
    return InterlockedCompareExchange(&ended, 0, 0);
}
static uint16_t stuck;     /* ... until they are let go */
static int credits_scene;  /* the credits scene runs (khdays_movie_hd_credits) */
static int course_credits; /* the course is its movie's */

/* The ids already told about (no video, or one that cannot be played): once each. */
static char (*told)[64];
static int told_count, told_capacity;

static int tell_once(const char *movie)
{
    for (int i = 0; i < told_count; ++i) {
        if (strcmp(told[i], movie) == 0) {
            return 0;
        }
    }
    if (told_count == told_capacity) {
        const int capacity = told_capacity ? told_capacity * 2 : 16;
        char(*grown)[64] = realloc(told, (size_t)capacity * sizeof(*told));
        if (grown == NULL) {
            return 1;
        }
        told = grown;
        told_capacity = capacity;
    }
    snprintf(told[told_count++], sizeof(told[0]), "%s", movie);
    return 1;
}

/* The movie's id: the path's file name before its first dot ("/mv/837.mods" -> "837", "843.z" ->
 * "843"). */
static int movie_id(const char *path, char *out, size_t size)
{
    const char *base = path, *dot;
    size_t n;
    for (const char *p = path; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }
    dot = strchr(base, '.');
    n = dot != NULL ? (size_t)(dot - base) : strlen(base);
    if (n == 0 || n >= size) {
        return 0;
    }
    memcpy(out, base, n);
    out[n] = '\0';
    return 1;
}

static const char *skippable_text(void)
{
    return skippable > 0 ? "skippable" : skippable == 0 ? "not skippable" : "not said";
}

/* Back to the game: shown, heard, its clock let go. */
static void finish(const char *how)
{
    khdays_hd_close();
    khdays_clock_hold(KHDAYS_HOLD_MOVIE, 0);
    khdays_sound_output_mute(0);
    khdays_log("movie %s: the HD video %s (the game's movie %s); the game goes on", id, how, skippable_text());
    fprintf(stderr, "movies: %s: the HD video %s; the game goes on\n", id, how);
    InterlockedExchange(&ended, 1);
    InterlockedExchange(&state, IDLE);
}

/* The game's own movie plays: from a stream's opening to the playback's stop (the hooks below), HD
 * video or not -- for the one screen's situation (ui/situation_decide.c). */
static volatile LONG game_movie;

void khdays_movie_hd_reset(void)
{
    InterlockedExchange(&game_movie, 0);
    if (state_now() == IDLE) {
        return;
    }
    AcquireSRWLockExclusive(&lock);
    if (state_now() != IDLE) {
        finish("stopped by the game's reset");
    }
    ReleaseSRWLockExclusive(&lock);
}

static void begin_skip(void)
{
    khdays_hd_close(); /* the picture and the sound stop at once; the window keeps the last picture */
    skipped = 0;
    InterlockedExchange(&state, SKIPPING);
    khdays_log("movie %s: the HD video stopped by START; START given to the game for its movie", id);
}

static void request_skip(void)
{
    switch (state_now()) {
    case PLAYING:
        if (skippable > 0) {
            begin_skip();
        } else if (skippable < 0) {
            skip_wanted = 1; /* decided when the movie says (khdays_movie_hd_flags) */
        }
        break;
    case FINISHING:
        if (skippable > 0) {
            skipped = 0;
            InterlockedExchange(&state, SKIPPING);
        }
        break;
    case HELD:
        if (skippable != 0) {
            finish("skipped");
        }
        break;
    default: break;
    }
}

int khdays_movie_game_playing(void)
{
    return InterlockedCompareExchange(&game_movie, 0, 0) != 0;
}

void khdays_movie_hd_stream(int slot, const char *path, int replaced)
{
    char movie[64], file[1024], why[320];
    KhdaysHdInfo info;
    (void)slot;
    InterlockedExchange(&game_movie, 1);
    if (path == NULL || replaced || state_now() != IDLE || !(khdays_host_graphics_flags() & KHDAYS_HOST_MOVIES_HD) ||
        !movie_id(path, movie, sizeof(movie))) {
        return;
    }
    /* a mod's video first (mods/<Mod>/movies/hd<id>.mp4, as khdays-studio
     * saves one), then the player's own HD videos */
    {
        char relative[64];
        snprintf(relative, sizeof(relative), "movies/hd%s.mp4", movie);
        if (!khdays_mods_find(relative, file, (int)sizeof(file))) {
            snprintf(file, sizeof(file), "data/cutscenes/cinematics/hd%s.mp4", movie);
        }
    }
    if (GetFileAttributesA(file) == INVALID_FILE_ATTRIBUTES) {
        if (tell_once(movie)) {
            khdays_log("movie %s: no HD video (%s); the game's movie plays", movie, file);
        }
        return;
    }
    /* opened outside the lock: only this thread starts a course */
    if (!khdays_hd_open(file, &info, why, sizeof(why))) {
        if (tell_once(movie)) {
            khdays_log("movie %s: %s cannot be played (%s); the game's movie plays", movie, file, why);
            fprintf(stderr, "movies: %s cannot be played (%s); the game's movie plays\n", file, why);
        }
        return;
    }
    /* the theater's subtitles for it in the game's language (a language just chosen first) */
    khdays_movie_subtitles_load(movie, khdays_language_wanted() != 0 ? khdays_language_wanted() : GetLanguage());
    AcquireSRWLockExclusive(&lock);
    snprintf(id, sizeof(id), "%s", movie);
    skippable = -1;
    skip_wanted = 0;
    skipped = 0;
    course_credits = credits_scene;
    game_last = host_last = 0xffff; /* what is held now is no press */
    khdays_sound_output_mute(1);
    khdays_hd_start();
    InterlockedExchange(&state, PLAYING);
    ReleaseSRWLockExclusive(&lock);
    khdays_log("movie %s: the HD video %s plays (%dx%d, %.3f frames a second, %s)", movie, file, info.width,
               info.height, info.fps, info.channels > 0 ? "with its sound" : "no sound");
    fprintf(stderr, "movies: %s shown instead from %s (%dx%d, %s)\n", movie, file, info.width, info.height,
            info.channels > 0 ? "with its sound" : "no sound");
}

void khdays_movie_hd_stopped(void)
{
    InterlockedExchange(&game_movie, 0);
    if (state_now() == IDLE) {
        return;
    }
    AcquireSRWLockExclusive(&lock);
    if (state_now() == PLAYING && course_credits) {
        khdays_log("movie %s: the game's movie ended; its staff roll runs on behind the HD video", id);
    } else if (state_now() == PLAYING) {
        khdays_clock_hold(KHDAYS_HOLD_MOVIE, 1); /* the game stops at its next wait */
        InterlockedExchange(&state, HELD);
        khdays_log("movie %s: the game's movie ended; the game waits for the HD video", id);
        fprintf(stderr, "movies: %s: the game's movie ended; the game waits for the HD video\n", id);
    } else if (state_now() == FINISHING) {
        finish("ended");
    } else if (state_now() == SKIPPING) {
        finish("skipped");
    }
    ReleaseSRWLockExclusive(&lock);
}

void khdays_movie_hd_credits(int phase)
{
    AcquireSRWLockExclusive(&lock);
    if (phase == 1) {
        credits_scene = 1;
    } else if (state_now() != IDLE && course_credits) {
        if (phase == 2 && state_now() == PLAYING) {
            khdays_clock_hold(KHDAYS_HOLD_MOVIE, 1);
            InterlockedExchange(&state, HELD);
            khdays_log("movie %s: the staff roll ended; the game waits for the HD video", id);
            fprintf(stderr, "movies: %s: the staff roll ended; the game waits for the HD video\n", id);
        } else if (phase == 0 || state_now() == SKIPPING) {
            finish(state_now() == SKIPPING ? "skipped" : "ended with the credits scene");
        }
    }
    if (phase == 0) {
        credits_scene = 0;
    }
    ReleaseSRWLockExclusive(&lock);
}

void khdays_movie_hd_flags(unsigned flags)
{
    if (state_now() == IDLE) {
        return;
    }
    AcquireSRWLockExclusive(&lock);
    if (state_now() != IDLE) {
        skippable = (flags & 8) != 0;
        if (state_now() == SKIPPING && (flags & 4)) {
            skipped = 1;
        }
        if (skip_wanted) {
            skip_wanted = 0;
            request_skip();
        }
    }
    ReleaseSRWLockExclusive(&lock);
}

uint16_t khdays_movie_hd_game_buttons(uint16_t buttons)
{
    uint16_t given;
    if (state_now() == IDLE && !ended_now() && stuck == 0) {
        return buttons;
    }
    AcquireSRWLockExclusive(&lock);
    if (state_now() != IDLE) {
        const uint16_t pressed = (uint16_t)(buttons & ~game_last);
        game_last = buttons;
        if (pressed & KHDAYS_KEY_START) {
            request_skip();
        }
        given = state_now() == SKIPPING && !skipped ? (uint16_t)KHDAYS_KEY_START : 0;
    } else {
        if (ended_now()) {
            stuck = buttons; /* held as the course ended */
            InterlockedExchange(&ended, 0);
        }
        stuck &= buttons;
        given = (uint16_t)(buttons & ~stuck);
    }
    ReleaseSRWLockExclusive(&lock);
    return given;
}

int khdays_movie_hd_showing(void)
{
    return state_now() != IDLE;
}

void khdays_movie_hd_poll(void)
{
    char why[320];
    int status;
    if (state_now() != PLAYING && state_now() != HELD) {
        return;
    }
    AcquireSRWLockExclusive(&lock);
    if (state_now() == PLAYING || state_now() == HELD) {
        status = khdays_hd_status(why, sizeof(why));
        if (status == -1) {
            khdays_log("movie %s: the HD video stopped part-way (%s); the game's movie shows", id, why);
            fprintf(stderr, "movies: %s: the HD video stopped part-way (%s)\n", id, why);
            finish("stopped part-way");
        } else if (status == 1 && (state_now() == HELD || course_credits)) {
            finish("ended");
        } else if (status == 1) {
            InterlockedExchange(&state, FINISHING);
            khdays_log("movie %s: the HD video ended before the game's movie; its last picture stays", id);
            fprintf(stderr, "movies: %s: the HD video ended before the game's movie\n", id);
        }
    }
    ReleaseSRWLockExclusive(&lock);
}

void khdays_movie_hd_host_buttons(uint16_t buttons)
{
    if (state_now() == IDLE) {
        return;
    }
    AcquireSRWLockExclusive(&lock);
    if (state_now() != IDLE) {
        const uint16_t pressed = (uint16_t)(buttons & ~host_last);
        host_last = buttons;
        if (pressed & KHDAYS_KEY_START) {
            request_skip();
        }
    }
    ReleaseSRWLockExclusive(&lock);
}
