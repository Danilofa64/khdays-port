/* The game's language changed while it runs (language.c). */
#ifndef KHDAYS_LANGUAGE_H
#define KHDAYS_LANGUAGE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Any thread: the game's language becomes `language` (1 English .. 5
 * Spanish, the firmware's numbering) when the scene on show ends. A second
 * request before then replaces the first. */
void khdays_language_request(int language);

/* The game's thread, at the start of each game frame (Pad_Sample's hook, input.c): a request taken. */
void khdays_language_frame(void);

/* The game's thread: the scene has ended and the next is not built yet
 * (Scene_AdvanceToPending's hook): a language asked for is set. */
void khdays_language_scene_ending(void);

/* The game's thread: the language asked for and not set yet (1..5), or 0.
 * What the port draws itself (the title's animated picture) follows it at
 * once; the game's own text when the scene ends. */
int khdays_language_wanted(void);

#ifdef __cplusplus
}
#endif

#endif
