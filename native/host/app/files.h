/* What the runtime needs from the system for the ARM7's services: the save file, written so that it
 * is always either the whole old file or the whole new one, whatever stops the process or the
 * machine; and the local time. The Windows version is files_win.c. */
#ifndef KHDAYS_HOST_FILES_H
#define KHDAYS_HOST_FILES_H

#include <stddef.h>

/* Writes `size` bytes to `path` (UTF-8), creating its folder if missing. The
 * data goes to a temporary file beside it, is flushed to the disk, then the
 * temporary file is renamed over the old one. Returns 1 on success; on
 * failure returns 0, leaves the old file untouched, and prints the reason on
 * stderr. */
int khdays_host_write_file_atomic(const char *path, const void *data, size_t size);

/* Reads the file at `path` into `data`, at most `room` bytes. Returns the
 * bytes read, or -1 if the file is missing or cannot be read. */
long khdays_host_read_file(const char *path, void *data, size_t room);

/* The PC's local time of day. */
void khdays_host_local_time(int *hour, int *minute, int *second);

#endif
