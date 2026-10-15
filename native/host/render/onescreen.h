/* The one-screen picture, on the presenting side: for each frame package it decides the situation
 * (ui/situation.h), logs it when it changes, and turns its composition (ui/composition.h) into what the
 * one-screen pass draws (gfx_render.h KhdaysOneScreenDraw). */
#ifndef KHDAYS_HOST_ONESCREEN_H
#define KHDAYS_HOST_ONESCREEN_H

#include "gfx_render.h"
#include "../../ui/composition.h"

typedef struct KhdaysFramePackage KhdaysFramePackage; /* runtime/display/frame_package.h */

/* Works out the situation and the composition of `package` for a picture
 * `columns` DS columns wide. `draw` points into `composition`, valid until the
 * next call. A package without the inputs for the situation keeps the last
 * situation. */
void khdays_onescreen_prepare(const KhdaysFramePackage *package, int columns, KhdaysComposition *composition,
                              KhdaysOneScreenDraw *draw);

/* The last situation decided. */
const KhdaysSituation *khdays_onescreen_situation(void);

/* Whether `package`, decided last, is to be decided again as the game's VBlanks pass: its picture
 * stands still and the situation's history is still settling (ui/situation.h). */
int khdays_onescreen_settling(const KhdaysFramePackage *package);

/* Whether `package` (already prepared) makes a new picture. With the 3D on both screens the game
 * moves engine A, the one that draws the 3D, to the other screen at every VBlank (POWCNT1 bit 15:
 * runtime/display/display.c's engine_swaps), drawing each screen's view in turn while the display
 * capture keeps the other's: one VBlank's 3D is the upper screen's, the next the lower's. The picture
 * is the upper screen's, so it is made on the VBlanks engine A shows the upper screen and the others
 * keep it (each one made had shown the lower's view every other picture). */
int khdays_onescreen_makes_picture(const KhdaysFramePackage *package);

#endif
