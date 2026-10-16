/* The DS's nine VRAM banks (GBATEK, "DS Memory Control - VRAM"). A bank the ARM9 sees (LCDC, a BG or
 * OBJ window) lives in that window of the address space, so stores and DMA reach it directly; one it
 * does not see lives in `store`. When VRAMCNT changes a mapping the contents move: the visible banks
 * saved, their old windows cleared (no bank reads zero), the banks loaded into their new windows. The
 * display reads through page tables to wherever each bank lives. */
#include "vram.h"
#include "io.h"
#include "window.h"

#include <stdio.h>
#include <string.h>

static u8 store[KHDAYS_VRAM_TOTAL];
static u8 current[KHDAYS_VRAM_BANKS];  /* the VRAMCNT values in effect */
static KhdaysBankPlace place[KHDAYS_VRAM_BANKS];
static KhdaysVramPages pages;

static u8 *bank_memory(int bank)
{
    return place[bank].cpu != 0 ? (u8 *)KHDAYS_W(place[bank].cpu) : store + khdays_vram_bank_offset[bank];
}

static void rebuild_pages(void)
{
    u8 *memory[KHDAYS_VRAM_BANKS];
    for (int bank = 0; bank < KHDAYS_VRAM_BANKS; ++bank) {
        memory[bank] = bank_memory(bank);
    }
    if (khdays_vram_build_pages(current, memory, &pages) != 0) {
        static int said;
        if (!said) {
            said = 1;
            fprintf(stderr, "vram: two banks mapped to the same place (the hardware ORs their "
                            "reads; here the later bank wins)\n");
        }
    }
}

void khdays_vram_reset(void)
{
    memset(current, 0, sizeof(current));
    memset(place, 0, sizeof(place));
    for (int bank = 0; bank < KHDAYS_VRAM_BANKS; ++bank) {
        place[bank].area = -1;
    }
    rebuild_pages();
}

void khdays_vram_control_written(void)
{
    u8 next[KHDAYS_VRAM_BANKS];
    int changed = 0;
    for (int bank = 0; bank < KHDAYS_VRAM_BANKS; ++bank) {
        next[bank] = IO8(khdays_vram_cnt_reg[bank]);
        changed |= next[bank] != current[bank];
    }
    if (!changed) {
        return;
    }
    for (int bank = 0; bank < KHDAYS_VRAM_BANKS; ++bank) {
        if (place[bank].cpu != 0) {
            memcpy(store + khdays_vram_bank_offset[bank], (const void *)KHDAYS_W(place[bank].cpu),
                   khdays_vram_bank_size[bank]);
        }
    }
    for (int bank = 0; bank < KHDAYS_VRAM_BANKS; ++bank) {
        if (place[bank].cpu != 0) {
            memset((void *)KHDAYS_W(place[bank].cpu), 0, khdays_vram_bank_size[bank]);
        }
    }
    for (int bank = 0; bank < KHDAYS_VRAM_BANKS; ++bank) {
        current[bank] = next[bank];
        if (!khdays_vram_place(bank, next[bank], &place[bank])) {
            fprintf(stderr, "vram: bank %c control 0x%02x is not a mapping GBATEK lists; "
                            "the bank is left unmapped\n", 'A' + bank, next[bank]);
        }
    }
    for (int bank = 0; bank < KHDAYS_VRAM_BANKS; ++bank) {
        if (place[bank].cpu != 0) {
            memcpy((void *)KHDAYS_W(place[bank].cpu), store + khdays_vram_bank_offset[bank],
                   khdays_vram_bank_size[bank]);
        }
    }
    rebuild_pages();
}

const KhdaysVramPages *khdays_vram_pages(void)
{
    return &pages;
}

const u8 *khdays_vram_bank(int bank)
{
    return bank_memory(bank);
}

u8 *khdays_vram_bank_written(int bank)
{
    return bank_memory(bank);
}
