/* Native stand-in for the decomp's dsprot_ranges.h (prepare.py points the includes here): on the DS
 * DS Protect keeps some instruction ranges encrypted in the ROM, decrypted by an opening marker and
 * encrypted again by a closing one; natively the range is ordinary C, so both markers are empty. */
#ifndef KHDAYS_DSPROT_RANGES_H
#define KHDAYS_DSPROT_RANGES_H

extern void func_ov028_0208a7e0(unsigned int *pRange); /* Encryptor_StartRange */
extern void func_ov028_0208a8ac(unsigned int *pRange); /* Encryptor_EndRange */

#define DSPROT_RANGE_BEGIN(key)
#define DSPROT_RANGE_END(key)

#endif
