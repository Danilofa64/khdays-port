/* The badges' icons on the field's mission panel: a port addition. On the DS
 * only the camp's mission list shows them. */
#ifndef KHDAYS_UI_BADGES_H
#define KHDAYS_UI_BADGES_H

/* Called by the HUD's emit while its mission panel is there (hud.c, the
 * "hud.mission" piece). In a story mission with badges, gives the element
 * "hud.badges": one icon per badge the mission has, left of the panel's
 * chest icon, lit once it is found -- or on one screen a badge and the
 * found and total count. */
void khdays_ui_badges_emit(void);

#endif
