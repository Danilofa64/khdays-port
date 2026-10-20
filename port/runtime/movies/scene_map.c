/* The scene map's text (scene_map.h): a line per scene,
 *   <script> <stem> [<stem> ...] ; <evidence>   <script> off ; <evidence>   <script> skip <stem> ; <evidence>
 * or per movie, mv#<movie> <stem> [until <seconds>] ; <evidence> (or off).
 * '#' comments, blank lines skipped. A script is EV_<world>#<index>, mi/<mission>/<script>:<section>
 * or mnl#<record> (a day's monologue, ui/mnl's record);
 * a stem is a video's file name without .mp4. */
#include "scene_map.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static char *copy_of(const char *s, size_t n)
{
    char *out = (char *)malloc(n + 1);
    if (out != NULL) {
        memcpy(out, s, n);
        out[n] = '\0';
    }
    return out;
}

static void entry_free(KhdaysSceneEntry *e)
{
    for (int i = 0; i < e->stem_count; ++i) {
        free(e->stems[i]);
    }
    free(e->stems);
    free(e->key);
    free(e->evidence);
    memset(e, 0, sizeof(*e));
}

/* Digits only, at least one. */
static int digits(const char *p)
{
    if (*p == '\0') {
        return 0;
    }
    for (; *p != '\0'; ++p) {
        if (!isdigit((unsigned char)*p)) {
            return 0;
        }
    }
    return 1;
}

/* EV_<world>#<index>, mi/<mission>/<script>:<section> (letters, digits and _ in the names),
 * mnl#<record> or mv#<movie> */
static int key_valid(const char *k)
{
    if (strncmp(k, "mv#", 3) == 0) {
        return digits(k + 3);
    }
    if (strncmp(k, "EV_", 3) == 0) {
        const char *hash = strchr(k, '#');
        if (hash == NULL || hash == k + 3) {
            return 0;
        }
        for (const char *p = k + 3; p < hash; ++p) {
            if (!isalnum((unsigned char)*p) && *p != '_') {
                return 0;
            }
        }
        return digits(hash + 1);
    }
    if (strncmp(k, "mnl#", 4) == 0) {
        return digits(k + 4);
    }
    if (strncmp(k, "mi/", 3) == 0) {
        const char *slash = strchr(k + 3, '/');
        const char *colon = slash != NULL ? strchr(slash + 1, ':') : NULL;
        return slash != NULL && slash > k + 3 && colon != NULL && colon > slash + 1 && colon[1] != '\0';
    }
    return 0;
}

static int stem_valid(const char *s)
{
    if (*s == '\0') {
        return 0;
    }
    for (; *s != '\0'; ++s) {
        if (!isalnum((unsigned char)*s) && *s != '_' && *s != '-') {
            return 0;
        }
    }
    return 1;
}

/* Splits `line` (its own copy) into words at spaces and tabs. Returns the words' count; grows. */
static int words_of(char *line, char ***words)
{
    int count = 0, capacity = 0;
    char *p = line;
    *words = NULL;
    for (;;) {
        while (*p == ' ' || *p == '\t') {
            ++p;
        }
        if (*p == '\0') {
            return count;
        }
        if (count == capacity) {
            char **grown;
            capacity = capacity ? capacity * 2 : 8;
            grown = (char **)realloc(*words, (size_t)capacity * sizeof(char *));
            if (grown == NULL) {
                return count;
            }
            *words = grown;
        }
        (*words)[count++] = p;
        while (*p != '\0' && *p != ' ' && *p != '\t') {
            ++p;
        }
        if (*p != '\0') {
            *p++ = '\0';
        }
    }
}

static void put(KhdaysSceneMap *map, KhdaysSceneEntry *e)
{
    for (int i = 0; i < map->count; ++i) {
        if (strcmp(map->entries[i].key, e->key) == 0) {
            entry_free(&map->entries[i]);
            map->entries[i] = *e;
            return;
        }
    }
    if (map->count == map->capacity) {
        const int capacity = map->capacity ? map->capacity * 2 : 64;
        KhdaysSceneEntry *grown = (KhdaysSceneEntry *)realloc(map->entries, (size_t)capacity * sizeof(*grown));
        if (grown == NULL) {
            entry_free(e);
            return;
        }
        map->entries = grown;
        map->capacity = capacity;
    }
    map->entries[map->count++] = *e;
}

/* One line (without its end): 1 an entry put, 0 nothing to read, -1 malformed (`why` set). */
static int read_line(KhdaysSceneMap *map, const char *start, size_t length, const char **why)
{
    char *line = copy_of(start, length);
    char *evidence, *hash;
    char **words = NULL;
    int count, result = 0;
    KhdaysSceneEntry e;
    if (line == NULL) {
        *why = "out of memory";
        return -1;
    }
    /* a comment from a '#' at a line's start or after a space (EV_S#4 keeps its own) */
    for (hash = line; (hash = strchr(hash, '#')) != NULL; ++hash) {
        if (hash == line || hash[-1] == ' ' || hash[-1] == '\t') {
            *hash = '\0';
            break;
        }
    }
    evidence = strchr(line, ';');
    if (evidence != NULL) {
        *evidence++ = '\0';
        while (*evidence == ' ' || *evidence == '\t') {
            ++evidence;
        }
    }
    count = words_of(line, &words);
    memset(&e, 0, sizeof(e));
    if (count == 0) {
        result = 0;
    } else if (!key_valid(words[0])) {
        *why = "not a scene's script (EV_<world>#<index>, mi/<mission>/<script>:<section> or mnl#<record>) "
               "nor a movie (mv#<movie>)";
        result = -1;
    } else if (count == 1) {
        *why = "no video and no `off`";
        result = -1;
    } else if (count == 2 && strcmp(words[1], "off") == 0) {
        e.key = copy_of(words[0], strlen(words[0]));
        e.off = 1;
        result = 1;
    } else if (strcmp(words[1], "skip") == 0) {
        if (count != 3 || strncmp(words[0], "mv#", 3) == 0 || !stem_valid(words[2])) {
            *why = "`skip` takes the HD movie's video that shows the scene (a scene's, not a movie's)";
            result = -1;
        } else {
            e.key = copy_of(words[0], strlen(words[0]));
            e.skip = 1;
            e.stems = (char **)calloc(1, sizeof(char *));
            if (e.stems != NULL) {
                e.stems[e.stem_count++] = copy_of(words[2], strlen(words[2]));
            }
            result = 1;
        }
    } else {
        const int movie = strncmp(words[0], "mv#", 3) == 0;
        const int until = movie && count == 4 && strcmp(words[2], "until") == 0;
        char *end = NULL;
        int i;
        if (until) {
            e.until = strtod(words[3], &end);
            count = 2; /* the video alone left to check */
        }
        for (i = 1; i < count && stem_valid(words[i]) && strcmp(words[i], "off") != 0 && strcmp(words[i], "skip") != 0;
             ++i) {
        }
        if (until && (end == words[3] || *end != '\0' || !(e.until > 0.0))) {
            *why = "`until` takes the seconds the video ends at";
            result = -1;
        } else if (movie && count != 2) {
            *why = "a movie takes one video (and `until` with its seconds)";
            result = -1;
        } else if (i < count) {
            *why = "a video's name is not a file's name (letters, digits, _ and -)";
            result = -1;
        } else {
            e.key = copy_of(words[0], strlen(words[0]));
            e.stems = (char **)calloc((size_t)(count - 1), sizeof(char *));
            if (e.stems != NULL) {
                for (i = 1; i < count; ++i) {
                    e.stems[e.stem_count++] = copy_of(words[i], strlen(words[i]));
                }
            }
            result = 1;
        }
    }
    if (result == 1) {
        size_t n = evidence != NULL ? strlen(evidence) : 0;
        while (n > 0 && (evidence[n - 1] == ' ' || evidence[n - 1] == '\t')) {
            --n;
        }
        e.evidence = copy_of(evidence != NULL ? evidence : "", n);
        put(map, &e);
    }
    free(words);
    free(line);
    return result;
}

int khdays_scene_map_read(KhdaysSceneMap *map, const char *text, size_t size, const char *file,
                          KhdaysSceneReport report)
{
    size_t at = 0;
    int line = 0, read = 0;
    while (at < size) {
        size_t end = at;
        const char *why = NULL;
        int r;
        while (end < size && text[end] != '\n') {
            ++end;
        }
        ++line;
        r = read_line(map, text + at, (end > at && text[end - 1] == '\r') ? end - at - 1 : end - at, &why);
        if (r > 0) {
            ++read;
        } else if (r < 0 && report != NULL) {
            report(file, line, why);
        }
        at = end + 1;
    }
    return read;
}

const KhdaysSceneEntry *khdays_scene_map_find(const KhdaysSceneMap *map, const char *key)
{
    for (int i = 0; i < map->count; ++i) {
        if (strcmp(map->entries[i].key, key) == 0) {
            return &map->entries[i];
        }
    }
    return NULL;
}

void khdays_scene_map_free(KhdaysSceneMap *map)
{
    for (int i = 0; i < map->count; ++i) {
        entry_free(&map->entries[i]);
    }
    free(map->entries);
    memset(map, 0, sizeof(*map));
}
