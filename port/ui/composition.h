/* The one-screen picture's compositions: for each situation, which screens it shows, whose brightness it
 * takes, and the shapes that place pieces of the screens in it. Read from a
 * mod's layouts/compositions.txt, else the port's own (compositions/port_days.txt).
 *
 * The file: `units port`, then sections `[<situation> [if <condition> ...]]` -- a state, `!` before
 * it for its absence, `wide` (16:9 or wider) or `narrow` (4:3) -- taken in the file's order while
 * their conditions hold. Their lines:
 *   screens <upper|lower|stacked|side-by-side>   the picture's ground (the first that says it)
 *   brightness <default|upper|lower|halves|own>  whose master brightness the picture takes
 *   shape <upper|lower> [<x> <y> <w> <h>] at <corner> [margin <l> <t> <r> <b>]
 *         [scale <s> [<sy>] | scale cover] [effects]
 *                          a piece of a screen (all of it without a rectangle), scaled (a DS dot
 *                          becomes s picture dots), at a corner of the picture (centre, top-left,
 *                          top, ..., left), moved by its margins; the first shape that gives a
 *                          texel shows it (composition.c read_shape: the effects)
 *   shape3d <hud|all> [<x> <y>] [<w> <h>] at <corner> [margin ...] [scale <s>]
 *                          the HUD's screen-space 3D (or all the 3D) in a rectangle, moved
 *   hide3d <hud|all>       not drawn
 *   menu                   the common rule: the screens that show something, whole, as large as
 *                          the picture allows (a situation no section names takes it too)
 *   stop                   no later section
 * A length is in the picture's DS dots at the HUD's scale (its factor, 1 at the default HUD size);
 * `fixed(N)` one that does not follow it, `A+fixed(B)` both.
 *
 * Besides: `backdrop <file> [at <corner>] [fit]`,
 * a picture of a mod's layouts/ folder (PNG) behind everything, covering the
 * picture (with `fit`, inside it: all of it shows at any aspect ratio), its
 * aspect kept, held at the corner; its name may hold the title
 * menu's state -- {lang} the game's language (en, fr, de, it, es), {menu}
 * root, story or mission (intro: its fade-in while the lower screen is still
 * white; logo: the boot logos, 3 of them, {row} the one on show; newgame: the
 * new game's dialogs, ui/situation.h KHDAYS_TITLE_NEWGAME), {count} the
 * options the level shows, {row} the one
 * selected -- and a section whose picture no mod gives (this frame's) is not
 * used. `hide3d all`: no 3D in the picture. `preload <file>`: a picture named
 * as a backdrop's is (and an animated one's atlas) read ahead on a thread of
 * its own while its section holds, so that showing it later holds nothing up.
 *
 * A backdrop named .txt is an animated picture (tools/theater_title.py makes
 * them): pieces of an atlas, each a rectangle of the picture, moving and
 * fading over the frames of its clips. Its lines:
 *   picture <w> <h>        the picture's size, in its own texels
 *   ground <r> <g> <b>     behind every piece (0 to 1)
 *   atlas <file>           the pieces' texels (PNG), beside this file
 *   group <name>           its group, which a clip of the next one may name
 *   clip <frames> after <group> ...
 *                          played when the picture comes after one of those
 *                          groups (none: after no animated picture)
 *   loop <frames>          after the clip, from the frame it reached, again
 *                          and again; from its start when no clip names the
 *                          group before
 *                          After a picture of its own group (another row
 *                          selected), the clip that follows the same groups
 *                          and the loop go on from the frame they were at.
 *   piece <x> <y> <w> <h> <over|add|take> <16 numbers>
 *                          its texels in the atlas (a negative width or
 *                          height turns them over), how it goes on what is
 *                          under it, the colours of its left top, right top,
 *                          left bottom and right bottom corners (RGBA, 1 =
 *                          the texel's own), blended across it
 *   key <frame> <x0> <y0> <x1> <y1> <r> <g> <b> <a>
 *                          the piece's rectangle in the picture and its
 *                          colour over all of it at a frame of its clip;
 *                          between two keys a line between them
 *   prompt <action>        (after a piece) it shows the input the player has
 *                          for the action on the device played, where an
 *                          icon for it is given (khdays_composition_set_
 *                          prompt), its height kept, its right edge where it
 *                          is; its own texels otherwise
 *   icon <name> <x> <y> <w> <h>
 *                          an input's icon in the atlas, named <family>:
 *                          <input> (xbox, playstation, pad; host/app/profile.c's
 *                          gamepad names)
 *   button <action> <x0> <y0> <x1> <y1>
 *                          a rectangle of the picture the mouse clicks to do
 *                          the action (open_settings: the settings menu)
 * The clips' clock is the game's VBlanks, 60 a second, whether or not the
 * game's picture changes. */
#ifndef KHDAYS_UI_COMPOSITION_H
#define KHDAYS_UI_COMPOSITION_H

#include "situation.h"
#include "../gfx/onescreen.h"

#include <stdint.h>

enum {
    KHDAYS_SCREENS_UPPER,       /* C x 192: the 3D and the shapes */
    KHDAYS_SCREENS_LOWER,       /* C x 192: the lower screen's backdrop and the shapes */
    KHDAYS_SCREENS_STACKED,     /* the two-screen picture */
    KHDAYS_SCREENS_SIDE_BY_SIDE /* both whole, the upper on the left */
};

/* A shape in the one picture, built for this frame: its record for the pass
 * (gfx/onescreen.h, KO_WORDS words) and its colours made transparent. */
typedef struct KhdaysShape {
    uint32_t words[KO_WORDS];
} KhdaysShape;

/* A 3D shape: the HUD's screen-space polygons wholly inside a rectangle of
 * the DS's screen (DS dots), placed in the picture (its top-left at x, y,
 * DS dots of the picture, scaled) or hidden; or, group "all",
 * every polygon whose box's middle is inside the rectangle (a model in a
 * menu's frame, which the port draws larger elsewhere). */
enum { KHDAYS_SHAPE3D_HUD, KHDAYS_SHAPE3D_ALL };
typedef struct KhdaysShape3d {
    int group;      /* KHDAYS_SHAPE3D_* */
    int hide;
    float x, y, scale;
    float src_x, src_y, src_w, src_h;
    int whole; /* group "all": every polygon placed, not only those with their middle inside, where
                * the screen over the picture cuts the rest at its frame (the camp's model window) */
} KhdaysShape3d;

/* A piece of an animated picture as this frame has it, as the one-screen pass reads it
 * (KHDAYS_PIECE_WORDS words): its rectangle in the picture's texels, its texels in the atlas (a
 * negative width or height turns them over), its corners' colours (RGBA, 1 = the texel's own:
 * left top, right top, left bottom, right bottom), how it goes on what is under it. */
enum { KHDAYS_PIECE_OVER, KHDAYS_PIECE_ADD, KHDAYS_PIECE_TAKE };
#define KHDAYS_PIECE_WORDS 32
typedef struct KhdaysPiece {
    float rect[4];
    float texels[4];
    float corners[16];
    uint32_t blend;
    uint32_t unused[7];
} KhdaysPiece;

/* A rectangle of an animated picture the mouse clicks (`button`): its action and where it is, in
 * the picture's texels. */
typedef struct KhdaysButton {
    const char *action;
    float rect[4];
} KhdaysButton;

/* This frame's composition: the arrays are the module's, valid until the
 * next call. */
typedef struct KhdaysComposition {
    int screens;    /* KHDAYS_SCREENS_* */
    int brightness; /* KOB_* (gfx/onescreen.h) */
    const KhdaysShape *shapes;
    int shape_count;
    const uint32_t *keys; /* BGR555 colours made transparent (KO_KEYS) */
    int key_count;
    const KhdaysShape3d *shapes3d;
    int shape3d_count;
    /* the picture behind everything (`backdrop`): RGBA, red first, kept until the file is read
     * again; NULL when none. It covers the picture, its aspect kept, held at (backdrop_ax,
     * backdrop_ay): 0 its left or top edge on the picture's, 1 its right or bottom one. */
    const uint8_t *backdrop;
    int backdrop_w, backdrop_h;
    float backdrop_ax, backdrop_ay;
    int backdrop_fit; /* 1: inside the picture, its aspect kept, instead of covering it (`backdrop ...
                       * fit`; a screen of the port's laid out over it: host/screens/camp_screen.c) */
    int hide3d_all; /* `hide3d all`: the 3D not drawn */
    /* an animated picture's: its size (the one that covers the picture; a picture's own size
     * otherwise), its ground (8-bit RGB, red in the low byte) and this frame's pieces, drawn in
     * order on it from the atlas `backdrop` is then; none for a picture */
    int animated; /* 1: `backdrop` is an animated picture's atlas */
    int picture_w, picture_h;
    uint32_t ground;
    const KhdaysPiece *pieces;
    int piece_count;
    const KhdaysButton *buttons; /* an animated picture's */
    int button_count;
} KhdaysComposition;

/* The file, read once (and again when the screens become one). */
void khdays_composition_load(void);

/* The player's HUD size ([video] hud_size: 1 to 8, 4 the default). */
void khdays_composition_set_hud_size(int size);
/* The HUD's factor its size gives (1 at the default size 4). */
float khdays_composition_hud_factor(void);

/* The inputs the player has for `action` on the device played, as icons are named (`icon`), in
 * the order to try; none (count 0) when the keyboard is played: the pieces' own texels. Copied. */
void khdays_composition_set_prompt(const char *action, const char *const *inputs, int count);

/* The composition for a situation in a picture C DS dots wide. */
void khdays_composition_for(const KhdaysSituation *situation, int columns, KhdaysComposition *out);

#endif
