/* The port's release versions as its tags write them ("v0.1.0"), and the latest release's tag in
 * GitHub's answer: pure functions, for the update check (updates.c; tests/updates_test.c). */
#ifndef KHDAYS_HOST_RELEASE_VERSION_H
#define KHDAYS_HOST_RELEASE_VERSION_H

#include <stddef.h>

/* Whether `s` is a version: "1", "1.2" or "1.2.3", with a leading "v" or not, then nothing, a
 * pre-release ("-rc1") or build metadata ("+abc"). */
int khdays_version_valid(const char *s);

/* Below zero when version `a` is older than `b`, zero when they are the same, above zero when it
 * is newer. Missing parts count 0; at equal numbers a pre-release is the older. A text that is no
 * version counts older than any version. */
int khdays_version_compare(const char *a, const char *b);

/* The latest release's tag in GitHub's JSON answer (its "tag_name") into `tag`, at most `size`
 * bytes with its NUL: 1, or 0 when the answer has none, or none that is a version. */
int khdays_release_tag(const char *json, char *tag, size_t size);

#endif
