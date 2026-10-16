/* A picture between two of the game's 3D lists, for frame rates above the game's: the polygons the
 * newer list shares with the older one get their vertices placed between the two at `alpha` (0 the
 * older, 1 the newer), linearly in clip space; then the list is finished like the game's, so at 1 it
 * is exactly the newer list. Plain C, no shared state (docs/RENDERING.md, "The 3D"). */
#ifndef KHDAYS_GFX_G3D_INTERP_H
#define KHDAYS_GFX_G3D_INTERP_H

#include "g3d.h"

typedef struct KhdaysG3dInterp {
    KhdaysG3dPreVertex *verts; /* the picture's vertices before clipping */
    int32_t vert_capacity;
    int32_t *slots; /* the older list's polygons by key: index + 1, 0 free */
    int32_t slot_capacity;
    int32_t matched, polygons; /* the last picture's: polygons placed between, of all */
    int32_t jumped;            /* of those matched, how many moved more than a quarter of the view
                                   between the two lists (its first vertex): a wrong match's mark */
    int32_t jumped_mod;        /* of those, a mod's model's */
    struct {                   /* the draws they came from (the first few): resource, occurrence, how many */
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
