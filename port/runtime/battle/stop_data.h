/* The Stop magic's data (stop_data.c): what the game left unmade of its sixth magic -- the levels' numbers, the
 * battle's templates, the moogle's recipes and the words -- read from the port's own file, stop_magic.txt.
 * docs/RUNTIME_MAP.md, "The magic". Plain C: no game, no system calls. */
#ifndef KHDAYS_STOP_DATA_H
#define KHDAYS_STOP_DATA_H

#ifdef __cplusplus
extern "C" {
#endif

#define KHDAYS_STOP_LEVELS 3    /* Stop, Stopra, Stopga: the three places the battle's tables left */
#define KHDAYS_STOP_COUNTS 5    /* a magic's templates: one for each count of its panels, 1 to 5 (mp.z) */
#define KHDAYS_STOP_TEMPLATE 36 /* a template's bytes (0x24) */
#define KHDAYS_STOP_LANGUAGES 5 /* the game's: en, es, fr, de, it */

typedef struct KhdaysStopLevel {
    int set;      /* given in the file */
    int id;       /* its panel's item id */
    int ticks;    /* how long it holds an enemy, in battle ticks (30 a second) */
    double reach; /* how far from its caster it holds enemies, in the game's units */
    int price;    /* its panel's price */
    int template_set[KHDAYS_STOP_COUNTS];
    unsigned char templates[KHDAYS_STOP_COUNTS][KHDAYS_STOP_TEMPLATE];
} KhdaysStopLevel;

typedef struct KhdaysStopMaterial {
    int id, count;
} KhdaysStopMaterial;

typedef struct KhdaysStopRecipe {
    int set;
    KhdaysStopMaterial *materials; /* as many as the line names */
    int material_count;
    int munny;
    char condition[64]; /* when the moogle shows it */
} KhdaysStopRecipe;

/* One language's words for a level, UTF-8, with the game's highlight (\x02 ... \x01) and line breaks. */
typedef struct KhdaysStopWords {
    char *name;
    char *panel; /* the panel's description (the camp, the shop) */
    char *magic; /* the magic's description (the magic's own list) */
} KhdaysStopWords;

typedef struct KhdaysStopData {
    KhdaysStopLevel levels[KHDAYS_STOP_LEVELS];
    KhdaysStopRecipe recipes[KHDAYS_STOP_LEVELS];
    KhdaysStopWords words[KHDAYS_STOP_LANGUAGES][KHDAYS_STOP_LEVELS];
} KhdaysStopData;

/* The game's languages in the file's order, by their two letters. */
extern const char *const khdays_stop_languages[KHDAYS_STOP_LANGUAGES];

/* Reads `text` (`size` bytes) into `out`. Returns 0, or the first bad line's number with the reason in `why`. */
int khdays_stop_data_read(const char *text, unsigned size, KhdaysStopData *out, char *why, int why_size);

/* Whether each part is whole: the panels (every level and every word), the battle (every template), the shop
 * (every recipe). A part missing anything is left out whole. */
int khdays_stop_data_panels_whole(const KhdaysStopData *d);
int khdays_stop_data_battle_whole(const KhdaysStopData *d);
int khdays_stop_data_shop_whole(const KhdaysStopData *d);

void khdays_stop_data_free(KhdaysStopData *d);

#ifdef __cplusplus
}
#endif

#endif
