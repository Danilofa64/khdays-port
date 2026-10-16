/* Whether a newer release of the port is out. At most once a day a thread asks GitHub for the
 * latest release (KHDAYS_UPDATE_SOURCE, an address, asks there instead, at every start: for a
 * test); what it found is kept in saves/updates.txt, and the window tells the player on the title
 * screen (the settings menu's question). A scripted or headless run (KHDAYS_INPUT,
 * KHDAYS_HEADLESS: the walks, the tests) never asks, nor does KHDAYS_UPDATES=off. */
#ifndef KHDAYS_HOST_UPDATES_H
#define KHDAYS_HOST_UPDATES_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The player's answers to the offer. */
enum { KHDAYS_UPDATE_DOWNLOAD, KHDAYS_UPDATE_LATER, KHDAYS_UPDATE_SKIP };

/* This build's version ("0.1.0", the repository's VERSION file). */
const char *khdays_updates_version(void);

/* At the start, once, with the player's setting (`enabled`): reads what was found before, and when
 * GitHub was last asked a day ago or more, asks again in the background. */
void khdays_updates_start(int enabled);

/* A newer release to tell the player of now: its tag into `tag` (`size` bytes) and 1; or 0 when
 * there is none, the player was told of it already this run, or said not to be told of it. */
int khdays_updates_offer(char *tag, size_t size);

/* The player's answer to the offer (KHDAYS_UPDATE_*): Download opens the release's page, Skip
 * remembers not to tell of this release again, Later tells again at the next start. Either way
 * the offer is not made again this run. */
void khdays_updates_answer(int answer);

#ifdef __cplusplus
}
#endif

#endif
