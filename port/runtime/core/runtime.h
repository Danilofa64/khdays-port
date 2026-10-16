/* The native runtime: what hosts the recompiled game on a PC. */
#ifndef KHDAYS_RUNTIME_H
#define KHDAYS_RUNTIME_H

#ifdef __cplusplus
extern "C" {
#endif

/* Crash and stall reports (diag.c). */
void khdays_diag_init(void);

/* The native address of the function named `name` (from the PDB), or 0. */
unsigned long long khdays_diag_symbol(const char *name);  /* a native address */
/* The name of the native function holding `address` (from the PDB), "?" if
 * none; valid until the next call. */
const char *khdays_diag_function_at(const void *address);
/* The native call stack here, symbolized, on stderr (for a fatal report). */
void khdays_diag_stack(void);

/* The names of the native functions calling this one, the nearest first,
 * `skip` of them left out, as "a < b < c" into `out` (for traces). */
void khdays_diag_callers(char *out, int size, int skip);

/* Maps the DS address space at its real virtual addresses (memory.c). Must
 * run before anything touches DS memory; returns 0 (and says why) when an
 * address range is already taken. */
int khdays_memory_map(void);

/* The game's heaps given back at the port's reset, so that heaps made again start on zero memory as
 * at the first boot (memory.c). */
void khdays_heaps_reset(void);

/* The main-RAM state the BIOS and firmware leave for the game (boot.c). */
void khdays_boot_environment(void);

/* The parameter OS_ResetSystem left, for the boot after the reset (boot.c). */
void khdays_boot_reset_parameter(unsigned int parameter);

/* The owner's nickname in the firmware's user settings, from the player's
 * settings (boot.c): at boot, and again when the settings menu closes. */
void khdays_boot_owner_name(void);

/* A read of the ARM9 BIOS (0xffff0000), which is in no process's memory:
 * `size` bytes at `address` into `destination` (boot.c). */
void khdays_bios_read(unsigned int address, void *destination, unsigned int size);

/* Copies a module's native data initializers into place: -1 static, -2 ITCM, -3 DTCM, -4 the data
 * area, else an overlay id (data_init.c). */
void khdays_data_init(int module);

/* Brings up the host side (window, audio, files, threads). */
int khdays_runtime_init(int argc, char **argv);

/* The game's scene now (its scene controller's current id: 1 the front-end -- the boot logos, the
 * title and its menus --, 2 the field, ...); 0 before the game sets one. Any thread. */
unsigned int khdays_runtime_scene(void);

#ifdef __cplusplus
}
#endif

#endif
