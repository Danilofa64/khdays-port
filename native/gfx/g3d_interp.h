/* A picture between two of the game's 3D lists, for frame
 * rates above the game's.
 *
 * The polygons the newer list shares with the older one (the same key,
 * gfx/g3d.h: the same model draw and the same place in it, or for one outside
 * model draws the same material and place among that material's; as many
 * vertices, and drawn with the same texture, palette and attributes but
 * alpha) get their vertices, before clipping, placed between the two
 * lists' at `alpha` (0 is the older list, 1 the newer); the other polygons
 * are the newer list's. Then the list is clipped and put through the viewport
 * like the game's (khdays_g3d_finish), so at `alpha` = 1 it is exactly the
 * newer list. Positions, colours and texture coordinates are placed linearly
 * in clip space; nothing is extrapolated. Plain C with no shared state: the
 * host's thread calls it for each picture. */
#ifndef KHDAYS_GFX_G3D_INTERP_H
#define KHDAYS_GFX_G3D_INTERP_H

#include "g3d.h"

typedef struct KhdaysG3dInterp {
    KhdaysG3dPreVertex *verts;  /* the picture's vertices before clipping */
    int32_t vert_capacity;
    int32_t *slots;             /* the older list's polygons by key: index + 1, 0 free */
    int32_t slot_capacity;
    int32_t matched, polygons;  /* the last picture's: polygons placed between, of all */
    int32_t jumped;             /* of those matched, how many moved more than a quarter of the view
                                   between the two lists (its first vertex): a wrong match's mark */
    int32_t jumped_mod;         /* of those, a mod's model's */
    struct {                    /* the draws they came from (the first few): resource, occurrence, how many */
        uint64_t draw;
        uint32_t occurrence;
        int32_t count;
    } jumped_in[4];
} KhdaysG3dInterp;

/* The picture at `alpha` between `older` and `newer` into `out`; 0 when
 * there is no memory (`out` then holds the newer list as it is, or nothing). */
int khdays_g3d_interpolate(KhdaysG3dInterp *work, const KhdaysG3dList *older, const KhdaysG3dList *newer, float alpha,
                           KhdaysG3dList *out);

void khdays_g3d_interp_free(KhdaysG3dInterp *work);

#endif
