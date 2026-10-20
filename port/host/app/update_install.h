/* The system's part of the update that installs itself (updates.c): where the installer is
 * downloaded, and running it. Each system has its own (update_install_win.c on Windows). */
#ifndef KHDAYS_HOST_UPDATE_INSTALL_H
#define KHDAYS_HOST_UPDATE_INSTALL_H

#include <stddef.h>

/* Whether this copy can update itself: an installed one (its uninstaller beside it, in the folder
 * the game runs in), on Windows itself (under Proton or Wine the installer is the player's to run). */
int khdays_update_install_possible(void);

/* The folder the installer is downloaded into (the system's temporary files' khdays-update), made if
 * need be: into `out` (UTF-8) and 1, or 0. */
int khdays_update_install_folder(char *out, size_t size);

/* Removes the installers an earlier update left in that folder (the one running now stays). */
void khdays_update_install_clean(void);

/* Runs the installer `file` to update the copy in the folder the game runs in, without its pages
 * (it takes the choices of the copy's last install) and opening the game again when it is done; the
 * system asks the player for the administrator's permission. 1 when it started (the game is then to
 * quit, so its files can be replaced), 0 when it did not, with the reason in `why`. */
int khdays_update_install_run(const char *file, char *why, size_t size);

#endif
