/* A mod's models drawn instead of the game's: mods/<Mod>/graphics/<name>.gltf (or .glb, .fbx), named as
 * the dump names the game's, while HD graphics are on. The game draws as always; the port takes each
 * node's matrix, leaves out the model's shapes and draws the mod's meshes on their own skeleton,
 * joints following nodes by name or <model>.bones.txt (docs/RENDERING.md, "Mods' graphics"). */
#ifndef KHDAYS_MODELS_H
#define KHDAYS_MODELS_H

#ifdef __cplusplus
extern "C" {
#endif

/* NNSi_G3dDrawInternal's start, for `model` (its NNSG3dResMdl), and its end. */
void khdays_models_draw_begin(const void *model);
void khdays_models_draw_end(void);

/* NNSi_G3dFuncSbc_NODEDESC: node `node`'s matrix is the current one (`sent`:
 * it was sent, not only computed). */
void khdays_models_node(unsigned node, int sent);

/* NNSi_G3dFuncSbcSHP, the shape's display list `list` (`size` bytes): whether
 * it is left unsent (the draw's model is replaced). One left unsent still has
 * its matrix commands run (MTX_RESTORE, MTX_SCALE, ...): the nodes after it
 * are placed on the matrix it leaves (a model with a position scale, 051's,
 * scales the matrix in its lists). */
int khdays_models_shape(const void *list, unsigned int size);

#ifdef __cplusplus
}
#endif

#endif
