/* Forced include for the decompilation's sources as the rewrite
 * preprocesses them (port/tools/references, MSVC). It only papers over
 * compiler dialect; nothing here changes what the game code does. */
#ifndef KHDAYS_DECOMP_COMPAT_H
#define KHDAYS_DECOMP_COMPAT_H

/* GCC/mwcc attributes (alignment hints) have no MSVC C spelling here. */
#define __attribute__(x)

/* The ARM `clz` the decomp spells as `asm { clz x, x }` (prepare.py rewrites
 * it to this). */
static __inline unsigned int khdays_clz(unsigned int x)
{
    unsigned int n = 0;
    if (x == 0) {
        return 32;
    }
    while ((x & 0x80000000u) == 0) {
        x <<= 1;
        ++n;
    }
    return n;
}

/* Game data lives at its DS address (prepare.py): each initialised object's
 * initializer is kept as `<name>__khdays_init` and listed in section .khdi,
 * which the runtime walks to copy it into place when the object's module
 * (-1 static, -2 ITCM, -3 DTCM, else the overlay id) is loaded. `address` is
 * the object's reference -- its DS address; the rewrite's data
 * area records its objects the same way, module -4. */
/* Laid out the same in the game's files (/Zp4) and the runtime's. */
#pragma pack(push, 8)
struct khdays_data_init {
    unsigned int address;
    const void *init;
    unsigned int size;
    int module;
};
#pragma pack(pop)
#pragma section(".khdi$m", read, write)
#define KHDAYS_DATA_INIT(name, module)                                        \
    __declspec(allocate(".khdi$m")) struct khdays_data_init khdays_init_##name = { \
        (unsigned int)&name, (const void *)&name##__khdays_init,              \
        sizeof(name##__khdays_init), module};

/* A call the decomp's C makes with fewer arguments than the ROM passes, where
 * the missing value is not yet known (prepare.py, port/abi): reaching it
 * stops the game, naming the call. */
#ifdef __cplusplus
extern "C"
#endif
__declspec(noreturn) void khdays_abi_gap(const char *what);
#define KHDAYS_ABI_GAP(what) (khdays_abi_gap(what), 0)

/* The math unit's and the plain interrupt registers without the trap's
 * fault (runtime/core/io.h): prepare.py rewrites the decomp's casts of their
 * addresses to these, compat/include/nitro/hw.h its macros. */
#ifdef __cplusplus
extern "C" {
#endif
void *khdays_io_cp(unsigned int offset, int size);
/* A register that only holds a value, in the register model's memory
 * (runtime/core/io.h): in the window at KHDAYS_IO_VIEW_REF, the
 * address its reference (port/tools/references/build.py reads the number
 * from here). */
#define KHDAYS_IO_VIEW_REF 0x0b100000u
#define khdays_io_plain(offset) ((void *)(KHDAYS_IO_VIEW_REF + (offset)))
/* The register address a pointer holds, as khdays_io_load/store take it
 * (prepare.py, port/tools/registers.py; the decomp has no size_t). */
#define KHDAYS_IO_ADDRESS(pointer) ((unsigned int)(unsigned long long)(pointer))
/* A register read or written through the register model, as the trap would
 * (prepare.py GEOMETRY_STORE and its HOOKS for the SDK's DMA and matrix
 * commands). */
unsigned int khdays_io_load(unsigned int address, int size);
void khdays_io_store(unsigned int address, int size, unsigned int value);
#ifdef __cplusplus
}
#endif

/* `*(T (*)[n])dst = *(T (*)[n])src;` -- a whole-array assignment mwcc accepts
 * (prepare.py rewrites it to this). */
#define KHDAYS_ARRAY_ASSIGN(T, n, dst, src)                         \
    do {                                                            \
        T *khdays_d_ = (T *)(dst);                                  \
        const T *khdays_s_ = (const T *)(src);                      \
        for (int khdays_i_ = 0; khdays_i_ < (n); ++khdays_i_) {     \
            khdays_d_[khdays_i_] = khdays_s_[khdays_i_];            \
        }                                                           \
    } while (0)

#endif
