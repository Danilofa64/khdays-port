/* SHA-1 (FIPS 180-4), as the DS's MATH_CalcSHA1 gives it: the save files' signatures
 * (saveslots.c) and the rooms' passwords (net_session.c). */
#ifndef KHDAYS_SHA1_H
#define KHDAYS_SHA1_H

#ifdef __cplusplus
extern "C" {
#endif

/* SHA-1 of `len` bytes. */
void khdays_sha1(const unsigned char *data, unsigned int len, unsigned char digest[20]);

#ifdef __cplusplus
}
#endif

#endif
