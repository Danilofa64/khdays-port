/* host/app/updates.h. */
#include "updates.h"

#include "release_version.h"
#include "../net/https.h"
#include "../../runtime/core/session_log.h"

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef KHDAYS_VERSION
#define KHDAYS_VERSION "0.0.0" /* port/CMakeLists.txt gives the VERSION file's */
#endif

#define REPOSITORY "Yokimitsuro/khdays-port"
#define LATEST "https://api.github.com/repos/" REPOSITORY "/releases/latest"
#define STATE "saves/updates.txt"

/* a version's tag is short: a longer one is no tag of the port's */
enum { TAG = 64 };

static SDL_Mutex *lock;   /* latest, skip and asked: the asking thread writes them */
static char latest[TAG];  /* the newest release GitHub gave ("v0.1.1"), or "" */
static char skip[TAG];    /* the release the player said not to be told of */
static long long asked;   /* when GitHub was last asked, in seconds since 1970 */
static int looking, told; /* the window thread's: the check is on; the player answered this run */

const char *khdays_updates_version(void)
{
    return KHDAYS_VERSION;
}

static char *trim(char *s)
{
    char *end;
    while (*s == ' ' || *s == '\t') {
        ++s;
    }
    end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) {
        *--end = '\0';
    }
    return s;
}

static void keep_tag(char *to, const char *value)
{
    if (strlen(value) < TAG && khdays_version_valid(value)) {
        SDL_strlcpy(to, value, TAG);
    }
}

static void read_state(void)
{
    char *text = (char *)SDL_LoadFile(STATE, NULL);
    char *line = text;
    if (text == NULL) {
        return;
    }
    while (line != NULL && *line != '\0') {
        char *next = strchr(line, '\n'), *equals;
        if (next != NULL) {
            *next++ = '\0';
        }
        equals = strchr(line, '=');
        if (line[0] != '#' && equals != NULL) {
            char *key, *value;
            *equals = '\0';
            key = trim(line);
            value = trim(equals + 1);
            if (strcmp(key, "asked") == 0) {
                asked = strtoll(value, NULL, 10);
            } else if (strcmp(key, "latest") == 0) {
                keep_tag(latest, value);
            } else if (strcmp(key, "skip") == 0) {
                keep_tag(skip, value);
            }
        }
        line = next;
    }
    SDL_free(text);
}

/* Under the lock. Written whole: a new file, then put in place. */
static void write_state(void)
{
    char text[512];
    const int n = SDL_snprintf(text, sizeof(text),
                               "# The port's look for a newer release (host/app/updates.c): when GitHub was last\n"
                               "# asked, the newest release it gave, the one the player said not to be told of.\n"
                               "asked = %lld\nlatest = %s\nskip = %s\n",
                               asked, latest, skip);
    SDL_CreateDirectory("saves");
    if (n > 0 && SDL_SaveFile(STATE ".partial", text, (size_t)n)) {
        SDL_RenamePath(STATE ".partial", STATE);
    }
}

static int SDLCALL ask(void *unused)
{
    const char *source = SDL_getenv("KHDAYS_UPDATE_SOURCE");
    char agent[TAG + 16], tag[TAG];
    char *json;
    int found;
    (void)unused;
    SDL_snprintf(agent, sizeof(agent), "khdays-port/%s", KHDAYS_VERSION);
    json = khdays_https_get(source != NULL && *source != '\0' ? source : LATEST, agent, 8000);
    if (json == NULL) {
        khdays_log("updates: no answer about the latest release (asked again at the next start)");
        return 0;
    }
    found = khdays_release_tag(json, tag, sizeof(tag));
    free(json);
    SDL_LockMutex(lock);
    asked = (long long)time(NULL);
    if (found) {
        SDL_strlcpy(latest, tag, sizeof(latest));
    }
    write_state();
    SDL_UnlockMutex(lock);
    if (found) {
        khdays_log("updates: the latest release is %s; this is %s", tag, KHDAYS_VERSION);
    } else {
        khdays_log("updates: the answer named no release");
    }
    return 0;
}

void khdays_updates_start(int enabled)
{
    const char *off = SDL_getenv("KHDAYS_UPDATES");
    SDL_Thread *thread;
    if (!enabled || SDL_getenv("KHDAYS_INPUT") != NULL || SDL_getenv("KHDAYS_HEADLESS") != NULL ||
        (off != NULL && SDL_strcasecmp(off, "off") == 0)) {
        return; /* turned off, or a run nobody watches */
    }
    lock = SDL_CreateMutex();
    if (lock == NULL) {
        return;
    }
    read_state();
    looking = 1;
    /* at every start: a release is seen the first time the game starts after it (GitHub answers an
     * address 60 times an hour without signing in) */
    thread = SDL_CreateThread(ask, "updates", NULL);
    if (thread != NULL) {
        SDL_DetachThread(thread);
    }
}

int khdays_updates_offer(char *tag, size_t size)
{
    int offer;
    if (!looking || told || tag == NULL || size == 0) {
        return 0;
    }
    SDL_LockMutex(lock);
    offer = latest[0] != '\0' && khdays_version_compare(latest, KHDAYS_VERSION) > 0 && strcmp(latest, skip) != 0;
    if (offer) {
        SDL_strlcpy(tag, latest, size);
    }
    SDL_UnlockMutex(lock);
    return offer;
}

void khdays_updates_answer(int answer)
{
    char page[TAG + 64] = "";
    if (!looking || told) {
        return;
    }
    told = 1;
    SDL_LockMutex(lock);
    if (answer == KHDAYS_UPDATE_DOWNLOAD) {
        SDL_snprintf(page, sizeof(page), "https://github.com/" REPOSITORY "/releases/tag/%s", latest);
    } else if (answer == KHDAYS_UPDATE_SKIP) {
        SDL_strlcpy(skip, latest, sizeof(skip));
        write_state();
    }
    SDL_UnlockMutex(lock);
    khdays_log("updates: the player chose %s", answer == KHDAYS_UPDATE_DOWNLOAD ? "to download it"
                                               : answer == KHDAYS_UPDATE_SKIP   ? "not to be told of it again"
                                                                                : "later");
    if (page[0] != '\0' && !SDL_OpenURL(page)) {
        khdays_log("updates: the release's page could not be opened (%s)", SDL_GetError());
    }
}
