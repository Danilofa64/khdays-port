/* The Stop magic's data (stop_data.h): stop_magic.txt read line by line. */
#include "stop_data.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *const khdays_stop_languages[KHDAYS_STOP_LANGUAGES] = {"en", "es", "fr", "de", "it"};

/* The line's next word (spaces between), or NULL at its end. */
static const char *next_word(const char **at, const char *end, int *length)
{
    const char *p = *at;
    while (p < end && (*p == ' ' || *p == '\t')) {
        ++p;
    }
    if (p >= end) {
        *at = p;
        return NULL;
    }
    const char *start = p;
    while (p < end && *p != ' ' && *p != '\t') {
        ++p;
    }
    *at = p;
    *length = (int)(p - start);
    return start;
}

static int word_int(const char *word, int length, int *out)
{
    char buffer[24];
    char *stop;
    if (word == NULL || length <= 0 || length >= (int)sizeof(buffer)) {
        return 0;
    }
    memcpy(buffer, word, (size_t)length);
    buffer[length] = '\0';
    const long value = strtol(buffer, &stop, 0);
    *out = (int)value;
    return *stop == '\0';
}

/* The rest of the line as the game's words: `\n` a line break, `[...]` highlighted (\x02 ... \x01). */
static char *game_words(const char *p, const char *end)
{
    while (p < end && (*p == ' ' || *p == '\t')) {
        ++p;
    }
    while (end > p && (end[-1] == ' ' || end[-1] == '\t')) {
        --end;
    }
    char *out = calloc((size_t)(end - p) + 1, 1);
    char *o = out;
    if (out == NULL) {
        return NULL;
    }
    for (; p < end; ++p) {
        if (*p == '\\' && p + 1 < end && p[1] == 'n') {
            *o++ = '\n';
            ++p;
        } else if (*p == '[') {
            *o++ = '\x02';
        } else if (*p == ']') {
            *o++ = '\x01';
        } else {
            *o++ = *p;
        }
    }
    *o = '\0';
    return out;
}

static int level_of(const char *word, int length, int *level)
{
    return word_int(word, length, level) && *level >= 1 && *level <= KHDAYS_STOP_LEVELS;
}

static int language_of(const char *word, int length)
{
    for (int i = 0; word != NULL && i < KHDAYS_STOP_LANGUAGES; ++i) {
        if (length == 2 && memcmp(word, khdays_stop_languages[i], 2) == 0) {
            return i;
        }
    }
    return -1;
}

static int read_level(const char *p, const char *end, KhdaysStopData *d)
{
    int n, level, length;
    const char *w = next_word(&p, end, &length);
    if (!level_of(w, length, &level)) {
        return 0;
    }
    KhdaysStopLevel *l = &d->levels[level - 1];
    char reach[24];
    if (!word_int(next_word(&p, end, &length), length, &l->id) ||
        !word_int(next_word(&p, end, &length), length, &l->ticks) || (w = next_word(&p, end, &n)) == NULL ||
        n >= (int)sizeof(reach) || !word_int(next_word(&p, end, &length), length, &l->price)) {
        return 0;
    }
    memcpy(reach, w, (size_t)n);
    reach[n] = '\0';
    l->reach = atof(reach);
    l->set = l->id > 0 && l->ticks > 0 && l->reach > 0.0;
    return l->set;
}

static int read_template(const char *p, const char *end, KhdaysStopData *d)
{
    int level, count, length;
    if (!level_of(next_word(&p, end, &length), length, &level) ||
        !word_int(next_word(&p, end, &length), length, &count) || count < 1 || count > KHDAYS_STOP_COUNTS) {
        return 0;
    }
    const char *hex = next_word(&p, end, &length);
    if (hex == NULL || length != 2 * KHDAYS_STOP_TEMPLATE) {
        return 0;
    }
    KhdaysStopLevel *l = &d->levels[level - 1];
    for (int i = 0; i < KHDAYS_STOP_TEMPLATE; ++i) {
        unsigned byte;
        char pair[3] = {hex[2 * i], hex[2 * i + 1], '\0'};
        if (!isxdigit((unsigned char)pair[0]) || !isxdigit((unsigned char)pair[1]) || sscanf(pair, "%x", &byte) != 1) {
            return 0;
        }
        l->templates[count - 1][i] = (unsigned char)byte;
    }
    l->template_set[count - 1] = 1;
    return 1;
}

static int read_recipe(const char *p, const char *end, KhdaysStopData *d)
{
    int level, length;
    if (!level_of(next_word(&p, end, &length), length, &level)) {
        return 0;
    }
    KhdaysStopRecipe *r = &d->recipes[level - 1];
    const char *w;
    free(r->materials);
    memset(r, 0, sizeof(*r));
    /* materials (<id>x<count>) until the munny, then the condition: the materials counted, then read */
    const char *first = p;
    int count = 0;
    while ((w = next_word(&p, end, &length)) != NULL && memchr(w, 'x', (size_t)length) != NULL) {
        ++count;
    }
    if (count == 0 || (r->materials = calloc((size_t)count, sizeof(*r->materials))) == NULL) {
        return 0;
    }
    p = first;
    for (int i = 0; i < count; ++i) {
        KhdaysStopMaterial *m = &r->materials[i];
        w = next_word(&p, end, &length);
        const char *x = memchr(w, 'x', (size_t)length);
        if (!word_int(w, (int)(x - w), &m->id) || !word_int(x + 1, length - (int)(x - w) - 1, &m->count) ||
            m->count <= 0) {
            return 0;
        }
        ++r->material_count;
    }
    if ((w = next_word(&p, end, &length)) == NULL || !word_int(w, length, &r->munny)) {
        return 0;
    }
    if ((w = next_word(&p, end, &length)) == NULL || length >= (int)sizeof(r->condition)) {
        return 0;
    }
    memcpy(r->condition, w, (size_t)length);
    r->condition[length] = '\0';
    r->set = 1;
    return 1;
}

static int read_words(const char *key, int key_length, const char *p, const char *end, KhdaysStopData *d)
{
    int level, length;
    const int language = language_of(next_word(&p, end, &length), length);
    if (language < 0 || !level_of(next_word(&p, end, &length), length, &level)) {
        return 0;
    }
    KhdaysStopWords *w = &d->words[language][level - 1];
    char **slot = key_length == 4 && memcmp(key, "name", 4) == 0    ? &w->name
                  : key_length == 5 && memcmp(key, "panel", 5) == 0 ? &w->panel
                                                                    : &w->magic;
    free(*slot);
    *slot = game_words(p, end);
    return *slot != NULL && (*slot)[0] != '\0';
}

int khdays_stop_data_read(const char *text, unsigned size, KhdaysStopData *out, char *why, int why_size)
{
    const char *p = text, *end = text + size;
    int line = 0;
    memset(out, 0, sizeof(*out));
    while (p < end) {
        const char *eol = memchr(p, '\n', (size_t)(end - p));
        const char *stop = eol != NULL ? eol : end;
        const char *hash = memchr(p, '#', (size_t)(stop - p));
        const char *at = p;
        int length, ok = 1;
        ++line;
        if (hash != NULL) {
            stop = hash;
        }
        while (stop > at && (stop[-1] == '\r' || stop[-1] == ' ' || stop[-1] == '\t')) {
            --stop;
        }
        const char *key = next_word(&at, stop, &length);
        if (key == NULL) {
            /* an empty line or a comment */
        } else if (length == 5 && memcmp(key, "level", 5) == 0) {
            ok = read_level(at, stop, out);
        } else if (length == 8 && memcmp(key, "template", 8) == 0) {
            ok = read_template(at, stop, out);
        } else if (length == 6 && memcmp(key, "recipe", 6) == 0) {
            ok = read_recipe(at, stop, out);
        } else if ((length == 4 && memcmp(key, "name", 4) == 0) || (length == 5 && memcmp(key, "panel", 5) == 0) ||
                   (length == 5 && memcmp(key, "magic", 5) == 0)) {
            ok = read_words(key, length, at, stop, out);
        } else {
            ok = 0;
        }
        if (!ok) {
            snprintf(why, (size_t)why_size, "line %d: %.*s", line, (int)(stop - p), p);
            return line;
        }
        p = eol != NULL ? eol + 1 : end;
    }
    return 0;
}

int khdays_stop_data_panels_whole(const KhdaysStopData *d)
{
    for (int l = 0; l < KHDAYS_STOP_LEVELS; ++l) {
        if (!d->levels[l].set) {
            return 0;
        }
        for (int g = 0; g < KHDAYS_STOP_LANGUAGES; ++g) {
            const KhdaysStopWords *w = &d->words[g][l];
            if (w->name == NULL || w->panel == NULL || w->magic == NULL) {
                return 0;
            }
        }
    }
    return 1;
}

int khdays_stop_data_battle_whole(const KhdaysStopData *d)
{
    for (int l = 0; l < KHDAYS_STOP_LEVELS; ++l) {
        for (int c = 0; c < KHDAYS_STOP_COUNTS; ++c) {
            if (!d->levels[l].template_set[c]) {
                return 0;
            }
        }
    }
    return khdays_stop_data_panels_whole(d);
}

int khdays_stop_data_shop_whole(const KhdaysStopData *d)
{
    for (int l = 0; l < KHDAYS_STOP_LEVELS; ++l) {
        if (!d->recipes[l].set) {
            return 0;
        }
    }
    return khdays_stop_data_panels_whole(d);
}

static void free_recipe(KhdaysStopRecipe *r)
{
    free(r->materials);
}

static void free_words(KhdaysStopWords *w)
{
    free(w->name);
    free(w->panel);
    free(w->magic);
}

void khdays_stop_data_free(KhdaysStopData *d)
{
    for (int l = 0; l < KHDAYS_STOP_LEVELS; ++l) {
        free_recipe(&d->recipes[l]);
    }
    for (int g = 0; g < KHDAYS_STOP_LANGUAGES; ++g) {
        for (int l = 0; l < KHDAYS_STOP_LEVELS; ++l) {
            free_words(&d->words[g][l]);
        }
    }
    memset(d, 0, sizeof(*d));
}
