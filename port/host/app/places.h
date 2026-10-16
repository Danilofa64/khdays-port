/* The colours of a Mission Mode group's four places, as the game's own bars have them: the middle
 * shade of their labels (UI/mlt/res_&.p2 entry 0, cells 0-3, coloured with res.p2 entry 1's
 * palette), the same in the five languages -- blue, red, yellow, green. Their labels are
 * ui_text.h's TXT_ROOM_PLACE (the bars' 1P-4P, J1-J4, S1-S4, G1-G4). */
#ifndef KHDAYS_HOST_PLACES_H
#define KHDAYS_HOST_PLACES_H

#include <SDL3/SDL.h>

static const Uint8 KHDAYS_PLACE_COLOURS[4][3] = {{24, 49, 148}, {148, 24, 33}, {148, 115, 24}, {24, 148, 41}};

#endif
