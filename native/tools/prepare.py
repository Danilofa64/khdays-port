#!/usr/bin/env python3
"""Prepare the pinned khdays-decomp for the native (x64) build.

The decomp is consumed read-only. This script decides which sources build
the game, writes patched copies of the few that need a mechanical change
for MSVC (its C++ turned into C), and generates what the DS linker used to
provide:

  * hal_symbols.txt    -- files whose only source is ARM assembly; native/hal
                          implements what they define
  * duplicates.txt     -- globals several split files defined; one keeps the
                          definition, the rest become extern
  * ds_functions.c     -- the native function at each DS address

Then the game's C and the HAL are rewritten with every pointer a 4-byte
reference, the DS address it had (native/tools/references):
their rewritten copies in rewritten/, the data area's and the function
references' numbers in khdays_data_area.h, the DS addresses the port's side
reads in khdays_ds_symbols.h, and sources.cmake lists the rewritten files
(KHDAYS_REWRITTEN). The symbols at fixed DS addresses -- the BSS no C file
defines, OVERLAY_n_ID, the decomp's own linker-absolute symbols -- are
constants of the rewrite.

The source list comes from each module's delinks.txt: it names exactly the
files the DS build links, so every function has one provider.

Usage: python native/tools/prepare.py [--out build/native/gen-x64]
"""
from __future__ import annotations

import argparse
import bisect
import collections
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import abi_repair  # noqa: E402
import registers  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
DECOMP = ROOT / "external" / "khdays-decomp"
CONFIG = DECOMP / "config" / "arm9"

SECTION_RE = re.compile(
    r"^\s+(\.\w+)\s+start:0x([0-9a-fA-F]+)\s+end:0x([0-9a-fA-F]+)")
SYMBOL_RE = re.compile(r"^(\S+)\s+kind:(\w+)(?:\(([^)]*)\))?\s+addr:0x([0-9a-fA-F]+)")


@dataclass
class Module:
    name: str
    config: Path
    sections: dict[str, tuple[int, int]] = field(default_factory=dict)
    files: list[str] = field(default_factory=list)
    symbols: list[tuple[str, str, int]] = field(default_factory=list)  # name, kind, addr
    ranges: dict[str, list[tuple[int, int]]] = field(default_factory=dict)  # file -> sections
    thumb: set[str] = field(default_factory=set)  # Thumb functions
    sizes: dict[str, int] = field(default_factory=dict)  # function -> bytes


def load_modules() -> list[Module]:
    dirs = [CONFIG, CONFIG / "itcm", CONFIG / "dtcm"]
    dirs += sorted(p for p in (CONFIG / "overlays").iterdir() if p.is_dir())
    modules = []
    for d in dirs:
        delinks = d / "delinks.txt"
        if not delinks.exists():
            continue
        m = Module("main" if d == CONFIG else d.name, d)
        in_header = True
        for line in delinks.read_text(encoding="utf-8").splitlines():
            if not line.strip():
                in_header = False
                continue
            if in_header:
                s = SECTION_RE.match(line)
                if s:
                    m.sections[s[1]] = (int(s[2], 16), int(s[3], 16))
                continue
            if line.endswith(":") and not line.startswith(" "):
                m.files.append(line[:-1])
                continue
            s = SECTION_RE.match(line)
            if s and m.files:
                m.ranges.setdefault(m.files[-1], []).append((int(s[2], 16), int(s[3], 16)))
        syms = d / "symbols.txt"
        if syms.exists():
            for line in syms.read_text(encoding="utf-8").splitlines():
                s = SYMBOL_RE.match(line)
                if s:
                    m.symbols.append((s[1], s[2], int(s[4], 16)))
                    if s[2] == "function" and (s[3] or "").startswith("thumb"):
                        m.thumb.add(s[1])
                    size = re.search(r"size=0x([0-9a-fA-F]+)", s[3] or "")
                    if s[2] == "function" and size:
                        m.sizes[s[1]] = int(size[1], 16)
        modules.append(m)
    return modules


# --- Mechanical source transforms ---------------------------------------------

# `register int x asm("r4") = 0;` -- an ARM register binding; drop it.
REGISTER_ASM = re.compile(r"\basm\s*\(\s*\"r\d+\"\s*\)")
# `asm { clz x, y }` -- ARM count-leading-zeros; the compat header supplies it.
CLZ_ASM = re.compile(r"\basm\s*\{\s*clz\s+(\w+)\s*,\s*(\w+)\s*\}")
# Struct members whose array size is a constant expression equal to zero. A
# statement of the same shape (`return p[0];`) is no declaration: the line
# may not start with a statement keyword.
ZERO_MEMBER = re.compile(
    r"^([ \t]+)(?![ \t]|(?:return|else|case|goto|do)\b)[\w \t\*]+?\b(\w+)[ \t]*"
    r"\[([\w \t\+\-\*\(\)]+)\][ \t]*;[^\n]*$",
    re.MULTILINE)
# An object-like macro with a plain number as its value.
NUMERIC_DEFINE = re.compile(r"^[ \t]*#[ \t]*define[ \t]+(\w+)[ \t]+\(?(0[xX][0-9a-fA-F]+|\d+)[uU]?\)?[ \t]*$",
                            re.MULTILINE)
# `*(T (*)[N])dst = *(T (*)[N])src;` -- assigning a whole array, which mwcc
# accepts and C does not; copied element by element instead.
ARRAY_ASSIGN = re.compile(
    r"^([ \t]*)\*\((\w[\w \t]*?)[ \t]*\(\*\)\[([^\]\n]+)\]\)([^=\n]+?)[ \t]*=[ \t]*"
    r"\*\(\2[ \t]*\(\*\)\[\3\]\)([^;\n]+);", re.MULTILINE)
# DS Protect's encrypted-range markers are ARM assembly; see the native header.
DSPROT_RANGES_INCLUDE = re.compile(r'^([ \t]*#[ \t]*include[ \t]*)"dsprot_ranges\.h"', re.MULTILINE)
# A label closing a block (`out: }`), which C17 does not allow; `out: ; }` is
# the same program.
END_LABEL = re.compile(r"(^[ \t]*[A-Za-z_]\w*:)([ \t]*\n?[ \t]*\})", re.MULTILINE)
# A function definition header with an unnamed parameter such as `u32)`.
DEF_HEADER = re.compile(r"^(\w[\w \t\*]*\b\w+[ \t]*\()([^()\n]*)\)[ \t]*$", re.MULTILINE)
# A virtual member function (the decomp's C++ declares classes only to call
# through vtables whose entries are C functions taking `this` first). On the
# ARM `this` is simply r0; MSVC's default for members, __thiscall, passes it
# in ECX instead, so the C function would read its arguments one off.
# __cdecl members take `this` as the first stack argument, as the C does.
VIRTUAL_METHOD = re.compile(r"(\bvirtual\s+(?:[\w\*&]+\s+)*?)(~?[A-Za-z_]\w*\s*\()")
# mwcc gives a virtual destructor two vtable slots, the complete-object
# destructor and then the deleting one (`delete p` loads slot 1:
# ov024 0x02084ec4 `ldr r1,[r1,#4]`; data_ov024_020939c4 holds just those
# two), where MSVC gives it one. A placeholder declared first takes slot 0,
# so the destructor and every later method land on mwcc's slots.
VIRTUAL_DTOR = re.compile(r"^([ \t]*)(virtual __cdecl ~\w+\s*\(\s*\)\s*;)", re.MULTILINE)
# `delete p` of such a class: mwcc checks p and calls vtable slot 1 with p in
# r0 (ov024 0x02084ec4: `ldr r1,[r0]; ldr r1,[r1,#4]; blx r1`). MSVC keeps
# __thiscall for destructors whatever the declaration says, so the C function
# in that slot would get the delete flag as its argument; the call is spelled
# out instead.
DELETE_STATEMENT = re.compile(r"^([ \t]*)delete[ \t]+([^;\n]+);", re.MULTILINE)
# A definition with its return type on a line of its own (`void`, then
# `name(void)`): one line, as the definitions everything here matches are
# written. Only whitespace changes.
SPLIT_HEADER = re.compile(
    r"^((?:static[ \t]+|inline[ \t]+)*[A-Za-z_][\w \t\*]*?)[ \t]*\n"
    r"(?=[A-Za-z_]\w*[ \t]*\([^;{}]*\)[ \t]*\n?[ \t]*\{)", re.MULTILINE)
# An empty function (`bx lr` in the ROM) leaves r0 as its caller set it, and
# callers that read the result get their own first argument back -- the
# object dispatcher relies on it (an empty update keeps itself). Natively it
# returns its first argument, which it now takes.
EMPTY_FUNCTION = re.compile(r"^void[ \t]+(\w+)[ \t]*\([ \t]*(?:void)?[ \t]*\)[ \t]*\{[ \t]*\}[ \t]*$", re.MULTILINE)


# Whole modules the port replaces: none of their files
# is built; the HAL defines the functions the rest of the game calls in them,
# and a call to any other stops the game naming it (runtime/ds_calls.c).
# ov105 is NitroSDK's wireless library -- WM, the ARM7's protocol under it,
# and its helper wh.c -- which the game calls at 24 functions (every
# relocation into ov105 from main, the ITCM and the overlays): the port's
# wireless layer, native/hal/sdk_wireless.c, is them.
REPLACED_MODULES = {"ov105"}


# Where the runtime joins the game's own flow: for each function, a list of
# documented edits (text, replacement), applied in order; a text that is not
# there stops the script. A function is named by its key, module@address
# (function_keys), which stays put when the decomp renames it; the texts must
# match its current source.
HOOKS = {
    # The wireless layer's teardown loops: they spin on wh.c's
    # system state, which only the wireless interrupt advances; while it is
    # BUSY the loop halts at a wait point, where the port takes interrupts
    # and the wireless layer's deliveries (hal/sdk_irq.c), as the CPU would
    # take them between the loop's instructions.
    "main@020303d8": [(  # Game_RunSceneLoop
        "            case 3:\n                break;\n",
        "            case 3:\n                { extern void khdays_wait_point(void); khdays_wait_point(); }\n"
        "                break;\n",
    )],
    # Mission Mode's Retry in the pause (runtime/menus/retry.c): yes on the "Are
    # you sure?" page of Give Up tells the port; after a Retry the game over
    # leaves at once, dark (its mode 8, the one its A gives in Mission Mode),
    # and the session menu asks for the field again instead of the results.
    "main@02022eb0": [(  # PauseMenu_ConfirmFrame
        "            case 2:\n                Ov002_PauseMissionScene(0);\n",
        "            case 2:\n                { extern void khdays_retry_confirmed(void); khdays_retry_confirmed(); }\n"
        "                Ov002_PauseMissionScene(0);\n",
    )],
    "ov027@02082dac": [(  # Ov027_GameOverFadeIn
        "    pScene = NNSi_FndGetCurrentRootHeap();\n    pNext = 0;\n",
        "    pScene = NNSi_FndGetCurrentRootHeap();\n"
        "    { extern int khdays_retry_pending(void); if (khdays_retry_pending()) { pScene->nMode = 8; } }\n"
        "    pNext = 0;\n",
    )],
    "ov002@02068720": [(  # Ov002_StepSessionMenu
        "        case 0:\n            Ov002_FillMissionResult();\n",
        "        case 0:\n"
        "            { extern int khdays_retry_take(void);\n"
        "              if (khdays_retry_take()) { Scene_RequestPending(SCENE_FIELD, 0); break; } }\n"
        "            Ov002_FillMissionResult();\n",
    )],
    # The save lists in pages (runtime/menus/savelist.c): the game's questions (load, delete,
    # overwrite, create) name the file as its row + 1; with the page view the row's file is the
    # page's first + the row, so the number is the port's (khdays_savelist_file_number; the game's
    # own row + 1 without the pages).
    "ov000@020508a0": [  # Ov000_DrawLoadPageText
        ("void Ov000_DrawLoadPageText(void)\n",
         "extern int khdays_savelist_file_number(int slot);\nvoid Ov000_DrawLoadPageText(void)\n"),
        ("data_ov000_0205ac24->pageIndex + 1", "khdays_savelist_file_number(data_ov000_0205ac24->pageIndex)"),
    ],
    "ov025@0209b7a4": [  # Ov025_SaveMenuDrawPrompt
        ("void Ov025_SaveMenuDrawPrompt(Ov008SaveMenu *pMenu, int nPhase)\n",
         "extern int khdays_savelist_file_number(int slot);\n"
         "void Ov025_SaveMenuDrawPrompt(Ov008SaveMenu *pMenu, int nPhase)\n"),
        ("pMenu->nSlot + 1", "khdays_savelist_file_number(pMenu->nSlot)"),
    ],
    "ov008@020693c4": [  # Ov008_SaveMenuDrawPrompt
        ("void Ov008_SaveMenuDrawPrompt(Ov008SaveMenu *pMenu, int nPhase)\n",
         "extern int khdays_savelist_file_number(int slot);\n"
         "void Ov008_SaveMenuDrawPrompt(Ov008SaveMenu *pMenu, int nPhase)\n"),
        ("pMenu->nSlot + 1", "khdays_savelist_file_number(pMenu->nSlot)"),
    ],
    "ov006@02051884": [(  # Ov006_MissionInitVideoScene
        "            case 3:\n                break;\n",
        "            case 3:\n                { extern void khdays_wait_point(void); khdays_wait_point(); }\n"
        "                break;\n",
    )],
    "ov008@0207d5d8": [(  # Ov008_MissionInitVideoScene
        "            case 3:\n                break;\n",
        "            case 3:\n                { extern void khdays_wait_point(void); khdays_wait_point(); }\n"
        "                break;\n",
    )],
    # NitroSDK's PXI without the IPC registers:
    # the three functions that touch them hand the words to the port's
    # transport (runtime/core/pxi.h) -- a send to the ARM7's service at once, a
    # reply from the queue the receive interrupt's handler drains; the
    # callback table, the interrupt's installation, the interrupt masks
    # around each access and the dispatch stay the SDK's.
    # The save written whole once a save action of the game's ends:
    # every backup write, program or erase the game makes
    # goes through this stream command, which ends here; a changed chip is
    # committed to saves/khdays.sav atomically, and a commit that fails is the
    # game's failed save (as a failed write on the DS's chip would be).
    "main@0200e4e8": [  # CARDi_RequestStreamCommandCore
        ("\tCARDi_EndTask(p, TRUE);\n",
         "\t{\n"
         "\t\textern int khdays_backup_commit(void);\n"
         "\t\tif (!khdays_backup_commit() && p->cmd->result == CARD_RESULT_SUCCESS) {\n"
         "\t\t\tp->cmd->result = CARD_RESULT_FAILURE;\n"
         "\t\t}\n"
         "\t}\n"
         "\tCARDi_EndTask(p, TRUE);\n"),
    ],
    "main@02009278": [  # PXI_InitFifo
        ("        *(volatile unsigned short *)0x04000184 = 0xc408;\n",
         "        { extern void khdays_pxi_init(void); khdays_pxi_init(); } /* IPCFIFOCNT's enable */\n"),
        ("        count = 0;\n"
         "        for (;;) {\n"
         "            mark = *sync & 0xf;\n"
         "            *sync = (unsigned short)(mark << 8);\n"
         "            if (mark == 0 && count > 4) {\n"
         "                break;\n"
         "            }\n"
         "            timeout = 1000;\n"
         "            while ((*sync & 0xf) == mark) {\n"
         "                if (timeout <= 0) {\n"
         "                    count = 0;\n"
         "                    break;\n"
         "                }\n"
         "                timeout--;\n"
         "            }\n"
         "            count++;\n"
         "            }\n",
         "        /* the IPCSYNC handshake with the ARM7: none, natively */\n"),
    ],
    "main@020093e8": [  # PXI_SendWordByFifo
        ("    if (pFifo->wCnt & 0x4000) {\n"
         "        pFifo->wCnt |= 0xc000;\n"
         "        return -1;\n"
         "    }\n"
         "    state = OS_DisableInterrupts();\n"
         "    if (pFifo->wCnt & 2) {\n"
         "        OS_RestoreInterrupts(state);\n"
         "        return -2;\n"
         "    }\n"
         "    pFifo->dwSend = *(u32 *)&packet;\n",
         "    (void)pFifo; /* the FIFO's registers: the port's transport */\n"
         "    state = OS_DisableInterrupts();\n"
         "    { extern int khdays_pxi_send(unsigned int tag, unsigned int data, unsigned int err);\n"
         "      khdays_pxi_send(packet.nTag, packet.nData, packet.bErr); }\n"),
    ],
    "main@02009470": [  # PXIi_HandlerRecvFifoNotEmpty, its PXIi_SetToFifo and PXIi_GetFromFifo
        ("#define FifoRecvCallbackTable data_02046288\n",
         "#define FifoRecvCallbackTable data_02046288\n"
         "extern int khdays_pxi_send(unsigned int tag, unsigned int data, unsigned int err);\n"
         "extern int khdays_pxi_take(unsigned int *word);\n"),
        ("    if (reg_PXI_FIFO_CNT & REG_PXI_FIFO_CNT_ERR_MASK) {\n"
         "        reg_PXI_FIFO_CNT |= (REG_PXI_FIFO_CNT_E_MASK | REG_PXI_FIFO_CNT_ERR_MASK);\n"
         "        return PXI_FIFO_FAIL_SEND_ERR;\n"
         "    }\n", ""),
        ("    if (reg_PXI_FIFO_CNT & REG_PXI_FIFO_CNT_ERR_MASK) {\n"
         "        reg_PXI_FIFO_CNT |= (REG_PXI_FIFO_CNT_E_MASK | REG_PXI_FIFO_CNT_ERR_MASK);\n"
         "        return PXI_FIFO_FAIL_RECV_ERR;\n"
         "    }\n", ""),
        ("    if (reg_PXI_FIFO_CNT & REG_PXI_FIFO_CNT_SEND_FULL_MASK) {\n"
         "        (void)OS_RestoreInterrupts(enabled);\n"
         "        return PXI_FIFO_FAIL_SEND_FULL;\n"
         "    }\n"
         "\n"
         "    reg_PXI_SEND_FIFO = data;\n",
         "    khdays_pxi_send(data & 0x1fu, data >> 6, (data >> 5) & 1u);\n"),
        ("    if (reg_PXI_FIFO_CNT & REG_PXI_FIFO_CNT_RECV_EMP_MASK) {\n"
         "        (void)OS_RestoreInterrupts(enabled);\n"
         "        return PXI_FIFO_FAIL_RECV_EMPTY;\n"
         "    }\n"
         "\n"
         "    *data_buf = reg_PXI_RECV_FIFO;\n",
         "    if (!khdays_pxi_take(data_buf)) {\n"
         "        (void)OS_RestoreInterrupts(enabled);\n"
         "        return PXI_FIFO_FAIL_RECV_EMPTY;\n"
         "    }\n"),
    ],
    # The SDK's accesses to the DMA and geometry registers through pointers,
    # thousands a second in the field (KHDAYS_PROFILE's `io:` lines): each through the register model (runtime/core/io.h) instead of
    # the trap's fault -- the same reads, the same stores, in the same order.
    "itcm@01ff8590": [  # MIi_DmaSetParams
        ("    *p = (volatile unsigned int)src;\n"
         "    *(p + 1) = (volatile unsigned int)dest;\n"
         "    *(p + 2) = (volatile unsigned int)ctrl;\n",
         "    khdays_io_store((unsigned int)p, 4, src);\n"
         "    khdays_io_store((unsigned int)(p + 1), 4, dest);\n"
         "    khdays_io_store((unsigned int)(p + 2), 4, ctrl);\n"),
    ],
    "itcm@01ff85d0": [  # func_01ff85d0, MIi_DmaSetParams_wait
        ("    *p = (volatile unsigned int)src;\n"
         "    *(p + 1) = (volatile unsigned int)dest;\n"
         "    *(p + 2) = (volatile unsigned int)ctrl;\n"
         "    (void)*(volatile unsigned int *)0x040000B0;\n"
         "    (void)*(volatile unsigned int *)0x040000B0;\n",
         "    khdays_io_store((unsigned int)p, 4, src);\n"
         "    khdays_io_store((unsigned int)(p + 1), 4, dest);\n"
         "    khdays_io_store((unsigned int)(p + 2), 4, ctrl);\n"
         "    (void)khdays_io_load(0x040000B0u, 4);\n"
         "    (void)khdays_io_load(0x040000B0u, 4);\n"),
        ("        *p = 0;\n"
         "        *(p + 1) = 0;\n"
         "        *(p + 2) = 0x81400001;\n",
         "        khdays_io_store((unsigned int)p, 4, 0);\n"
         "        khdays_io_store((unsigned int)(p + 1), 4, 0);\n"
         "        khdays_io_store((unsigned int)(p + 2), 4, 0x81400001u);\n"),
    ],
    "main@02004000": [  # MIi_CheckAnotherAutoDMA
        ("        cnt = *cntp;\n", "        cnt = khdays_io_load((u32)cntp, 4);\n"),
    ],
    # The game's heaps in the port's region of its memory: DMA
    # channel 0 cannot read the DS's cartridge bus, so NitroSDK's check panics
    # for a source at or above 0x08000000 (0x020040b0-0x020040d4) -- where the
    # heaps now are. The port's DMA copies within the game's memory
    # (runtime/core/io_regs.c dma_unit) and has no such bus: a source wholly in the
    # game's RAM passes; the DS's own ranges keep the ROM's panic.
    "main@02004084": [  # MIi_CheckDma0SourceAddress
        ("/* MIi_CheckDma0SourceAddress -- NitroSDK mi_dma.c. */\n",
         "/* MIi_CheckDma0SourceAddress -- NitroSDK mi_dma.c. */\n"
         "extern int khdays_game_ram_ref(unsigned int reference);  /* runtime/core/window.c */\n"),
        ("        if (addStart == 0x04000000 || addStart >= 0x08000000 ||\n"
         "            addEnd == 0x04000000 || addEnd >= 0x08000000) {\n",
         "        if ((addStart == 0x04000000 || addStart >= 0x08000000 ||\n"
         "             addEnd == 0x04000000 || addEnd >= 0x08000000) &&\n"
         "            !(khdays_game_ram_ref(src) &&\n"
         "              khdays_game_ram_ref(dir == MI_DMA_SRC_DEC ? src - size : src + size - 1))) {\n"),
    ],
    # The game's two heaps: the main heap
    # (0xc8000 bytes from the main arena's low end, 0x0204ce88-0x0204cea0) and
    # the sub heap (the rest of the arena, or half of it under DS Protect's
    # emulator answer, 0x0204cee8-0x0204cf40) made by the port over its region
    # of the game's memory (runtime/core/game_heap.c) -- or, under its testing knob,
    # from the arena exactly as here.
    # A heap that gives no block: said in
    # the port's log with the heap's figures (runtime/core/game_heap.c); what the
    # game does with the 0 stays its own.
    "main@020107fc": [  # NNS_FndAllocFromExpHeapEx
        ("void *NNS_FndAllocFromExpHeapEx(void *heap, unsigned int size, int align) {\n",
         "extern void khdays_game_heap_failed(void *heap, unsigned int size);  /* runtime/core/game_heap.c */\n"
         "void *NNS_FndAllocFromExpHeapEx(void *heap, unsigned int size, int align) {\n"
         "    void *khdays_block;\n"),
        ("    if (align >= 0) {\n"
         "        return AllocFromHead(heap, size, align);\n"
         "    }\n"
         "    return AllocFromTail(heap, size, -align);\n",
         "    khdays_block = align >= 0 ? AllocFromHead(heap, size, align) : AllocFromTail(heap, size, -align);\n"
         "    if (khdays_block == 0) {\n"
         "        khdays_game_heap_failed(heap, size);\n"
         "    }\n"
         "    return khdays_block;\n"),
    ],
    # The headers' heaps: the game names a packed
    # archive's header in its file handles by 24 bits from 0x01ff8000
    # (`((header + 0x8000) & 0xfffffc) << 7 | 0x80000000 | index`, made in
    # some 200 places, read back by the two decoders below), so a header must
    # lie below 0x02ff8000. The headers Msg_OpenContainerAndReadHeader reads
    # (0x02024ee8, its one NNS_FndAllocFromDefaultExpHeapEx) are
    # taken from heaps of their own there (runtime/core/game_heap.c), freed back to
    # them whatever heap the game names, and remade with their game heap; the
    # decoders check that a handle's header lies where headers are.
    "main@02024ee8": [  # Msg_OpenContainerAndReadHeader
        ("    buf = (u32 *)NNS_FndAllocFromDefaultExpHeapEx((u16)(size + 0x10), 4);\n",
         "    { extern void *khdays_header_alloc(unsigned int size);  /* runtime/core/game_heap.c */\n"
         "      buf = (u32 *)khdays_header_alloc((u16)(size + 0x10)); }\n"),
    ],
    "main@0201098c": [  # NNS_FndFreeToExpHeap
        ("        NNSiFndHeapHead * pHeapHd = heap;\n",
         "        extern void *khdays_header_heap_of(void *heap, void *block);  /* runtime/core/game_heap.c */\n"
         "        NNSiFndHeapHead * pHeapHd = khdays_header_heap_of(heap, memBlock);\n"),
    ],
    "main@02023604": [  # HeapState_Recreate
        ("    NNS_FndInitAllocatorForExpHeap(&s->allocator, s->heap, 4);\n}",
         "    NNS_FndInitAllocatorForExpHeap(&s->allocator, s->heap, 4);\n"
         "    { extern void khdays_game_heap_recreated(void *heap);  /* runtime/core/game_heap.c */\n"
         "      khdays_game_heap_recreated(s->heap); }\n}"),
    ],
    "main@020250bc": [  # Archive_OpenSubfileByHandle
        ("    u8 *p = (u8 *)base;\n",
         "    u8 *p = (u8 *)base;\n"
         "    { extern void khdays_handle_check(unsigned int handle, unsigned int base);  /* runtime/core/game_heap.c */\n"
         "      khdays_handle_check(id, base); }\n"),
    ],
    "main@02025074": [  # Archive_SubfileIsCompressed
        ("    unsigned short lo = param_1 & (mask >> 15);\n",
         "    unsigned short lo = param_1 & (mask >> 15);\n"
         "    { extern void khdays_handle_check(unsigned int handle, unsigned int base);  /* runtime/core/game_heap.c */\n"
         "      khdays_handle_check(param_1, base); }\n"),
    ],
    "ov001@0204ce40": [  # Ov001_CreateMainAndSubHeaps
        ("#define HEAP_ALIGN 0x10\n",
         "#define HEAP_ALIGN 0x10\n"
         "extern void *khdays_game_heap(int which, u32 ds_size);  /* runtime/core/game_heap.c */\n"),
        ("        mainHeap = NNS_FndCreateExpHeapEx(\n"
         "            OS_AllocFromArenaLo(OS_ARENA_MAIN, MAIN_HEAP_SIZE, HEAP_ALIGN),\n"
         "            MAIN_HEAP_SIZE, 0);\n",
         "        mainHeap = khdays_game_heap(0, MAIN_HEAP_SIZE);\n"),
        ("        subHeap = NNS_FndCreateExpHeapEx(\n"
         "            OS_AllocFromArenaLo(OS_ARENA_MAIN, size, HEAP_ALIGN), size, 0);\n",
         "        subHeap = khdays_game_heap(1, size);\n"),
    ],
    "main@02003f1c": [  # MI_WaitDma
        ("    while ((*ctrl & 0x80000000) != 0) {\n",
         "    while ((khdays_io_load((unsigned int)ctrl, 4) & 0x80000000) != 0) {\n"),
        ("        reg[0] = 0;\n"
         "        reg[1] = 0;\n"
         "        reg[2] = 0x81400001;\n",
         "        khdays_io_store((unsigned int)&reg[0], 4, 0);\n"
         "        khdays_io_store((unsigned int)&reg[1], 4, 0);\n"
         "        khdays_io_store((unsigned int)&reg[2], 4, 0x81400001u);\n"),
    ],
    "main@02006f28": [  # G3X_GetClipMtx
        ("    if (*gxstat & 0x8000000) {\n",
         "    if (khdays_io_load((unsigned int)gxstat, 4) & 0x8000000) {\n"),
    ],
    "main@02006f58": [  # G3X_GetVectorMtx
        ("    if (*gxstat & 0x8000000) {\n",
         "    if (khdays_io_load((unsigned int)gxstat, 4) & 0x8000000) {\n"),
    ],
    "itcm@01ffc450": [  # NNSi_G3dFuncSbc_NODEMIX: the matrix commands, through the model
        ("    registers->mode = 0;\n"
         "    registers->store = 1;\n"
         "    registers->identity = 0;\n"
         "    registers->mode = 2;\n",
         "    khdays_io_store(0x04000440u, 4, 0);\n"
         "    khdays_io_store(0x0400044cu, 4, 1);\n"
         "    khdays_io_store(0x04000454u, 4, 0);\n"
         "    khdays_io_store(0x04000440u, 4, 2);\n"),
        ("            *restoreRegister = entry->matrixIndex;\n"
         "            registers->mode = 1;\n",
         "            khdays_io_store((unsigned int)restoreRegister, 4, entry->matrixIndex);\n"
         "            khdays_io_store(0x04000440u, 4, 1);\n"),
        ("            registers->mode = 2;\n"
         "            G3_MultMtx33(",
         "            khdays_io_store(0x04000440u, 4, 2);\n"
         "            G3_MultMtx33("),
        ("    registers->mode = 1;\n"
         "    G3_LoadMtx43(&accum.position);\n"
         "    registers->mode = 0;\n"
         "    registers->restore = 1;\n"
         "    registers->mode = 2;\n"
         "    registers->store = state->pSbc[1];\n",
         "    khdays_io_store(0x04000440u, 4, 1);\n"
         "    G3_LoadMtx43(&accum.position);\n"
         "    khdays_io_store(0x04000440u, 4, 0);\n"
         "    khdays_io_store(0x04000450u, 4, 1);\n"
         "    khdays_io_store(0x04000440u, 4, 2);\n"
         "    khdays_io_store(0x0400044cu, 4, state->pSbc[1]);\n"),
    ],
    # The field's HUD panel draws its 3D pieces (the hearts counter, the
    # countdown) with a camera of its own, then the markers that follow the
    # game's camera (Ov002_DrawMarkerQueue). The port marks the polygons the
    # panel sends as the HUD's, so widescreen can keep them to the sides of
    # the wider picture (runtime/display/display.c khdays_hud_3d, gpu3d.h): which
    # code sends them was traced (KHDAYS_TRACE_3D_CALLERS: the hearts counter
    # from Ov002_SceneDrawPanelWidgets and Ov002_SceneDrawPanelTotal, both
    # called here). Nothing the game computes changes.
    "ov002@020616a0": [  # Ov002_SceneStepPanel
        ("    Camera_CommitMatricesEx(s->aCamera, 0x3b33, -0x3b33, -0x4d9a, 0x4d9a);\n",
         "    Camera_CommitMatricesEx(s->aCamera, 0x3b33, -0x3b33, -0x4d9a, 0x4d9a);\n"
         "    { extern void khdays_hud_3d(int on); khdays_hud_3d(1); }\n"),
        ("    Ov002_DrawMarkerQueue(pCam);\n",
         "    { extern void khdays_hud_3d(int on); khdays_hud_3d(0); }\n"
         "    Ov002_DrawMarkerQueue(pCam);\n"
         "    { extern void khdays_hud_3d(int on); khdays_hud_3d(1); }\n"),
        ("    Ov002_SweepParkedValues(pCam);\n",
         "    { extern void khdays_hud_3d(int on); khdays_hud_3d(0); }\n"
         "    Ov002_SweepParkedValues(pCam);\n"),
    ],
    # The game over's floating orb (ov027's character object, an ov002
    # display object: two polygons Ov002_DrawSpriteQuad sends from here,
    # KHDAYS_TRACE_3D_CALLERS) drifts by the menu's chosen line.
    # The DS shows it over all the 2D (BG0 at priority 0), the lines under it
    # (BG1 at 2). The port marks it as the HUD's shown over the 2D, so that
    # the one picture's 3D shape for the game over places it with the lines
    # and in front of them, and widescreen leaves it in place.
    "ov027@0208344c": [  # Ov027_UpdateFlushForward
        ("    Ov002_ResourceEntryCallback(*(int *)(data_ov027_02084360 + 4) + 0x588);\n",
         "    { extern void khdays_hud_3d(int on); khdays_hud_3d(2); }\n"
         "    Ov002_ResourceEntryCallback(*(int *)(data_ov027_02084360 + 4) + 0x588);\n"
         "    { extern void khdays_hud_3d(int on); khdays_hud_3d(0); }\n"),
    ],
    # An overlay's data: its image has just been loaded and started; where the
    # SDK runs the overlay's static initializers, the native data initializers
    # of that overlay go over the image (runtime/core/data_init.c).
    "main@0200b4b8": [(  # FS_StartOverlay
        "FSOverlayInitFunc *p = p_ovi->header.sinit_init;",
        "FSOverlayInitFunc *p = (khdays_data_init((int)p_ovi->header.id), "
        "p_ovi->header.sinit_init);",
    )],
    # An object's update returns its next update. The ROM calls it with the
    # update's own address in r0 (0x02023b60 `ldr r0,[r1,#0x14]`, `blx r0`),
    # so one that leaves r0 alone -- an empty one, see EMPTY_FUNCTION -- keeps
    # itself; natively it gets that address as its argument. Whatever else it
    # returns must be native code (or -1/-2): anything not is caught here,
    # naming the update that returned it (runtime/core/diag.c).
    "main@02023adc": [(  # Obj_UpdateAll
        "                int cb = ((int (*)(void))((int *)gObjSystem[1])[5])();\n",
        "                int cb = ((int (*)(int))((int *)gObjSystem[1])[5])(((int *)gObjSystem[1])[5]);\n"
        "                { extern void khdays_check_update(int fn, int next);\n"
        "                  khdays_check_update(((int *)gObjSystem[1])[5], cb); }\n",
    )],
    # The camp menus on one screen (runtime/menus/camp.c): the status panel's numbers as the panel draws
    # them, each field with its values.
    "ov008@0206d46c": [(  # Ov008_DrawStatusField
        "    nCtx = Ov008_GetCtxBlock4a80();\n",
        "    { extern void khdays_camp_field(void *panel, int field, unsigned int delta, int style, void *values,\n"
        "                                    int animate);\n"
        "      khdays_camp_field(pPanel, nField, nDelta, nStyle, pValues, bAnimate); }\n"
        "    nCtx = Ov008_GetCtxBlock4a80();\n",
    )],
    # The story's camp menu the same (ov025, the same panel under its own name).
    "ov025@020a2b00": [(  # Ov025_DrawStatusField
        "    nCtx = Ov025_GetBlock4a80();\n",
        "    { extern void khdays_camp_field(void *panel, int field, unsigned int delta, int style, void *values,\n"
        "                                    int animate);\n"
        "      khdays_camp_field(pPanel, nField, nDelta, nStyle, pValues, bAnimate); }\n"
        "    nCtx = Ov025_GetBlock4a80();\n",
    )],
    # KHDAYS_TRACE_SCRIPT: the action-script interpreter reports each step it
    # stands on (runtime/core/diag.c), to see what a scene waits for.
    "main@02020e58": [(  # Game_RunActionScript
        "    cur = st + 4 + *(int *)(st + 0x124) * 0x48;\n",
        "    cur = st + 4 + *(int *)(st + 0x124) * 0x48;\n"
        "    { extern void khdays_trace_script(void *st, void *entry); khdays_trace_script(st, cur); }\n",
    )],
    # A scene has ended and the next is not built yet: a language the
    # settings menu asked for is set there, so that every scene is built whole
    # in one language (runtime/core/language.c).
    "main@0202099c": [(  # Scene_AdvanceToPending
        "            HeapState_Recreate(data_0204c02c);\n",
        "            { extern void khdays_language_scene_ending(void); khdays_language_scene_ending(); }\n"
        "            HeapState_Recreate(data_0204c02c);\n",
    )],
    # The game reads its buttons once per game frame, here (KEYINPUT and the
    # ARM7's X/Y word). The runtime takes that frame's input just before --
    # the script's keys and the player's (runtime/input/input.c) -- so every source
    # of input reaches the game the one way the DS's buttons do.
    "main@02023f78": [(  # Pad_Sample
        "    gPadHeld.prev = gPadHeld.cont;\n",
        "    { extern void khdays_input_pad_sample(void); khdays_input_pad_sample(); }\n"
        "    gPadHeld.prev = gPadHeld.cont;\n",
    )],
    # The game's ROM reads answered by the port (runtime/core/files.h).
    # Every read of the game's ROM reaches CARDi_ReadRom, whose
    # only caller is the FS ROM archive's FSi_ReadRomCallback. With the
    # port's reads (KHDAYS_FILES unset) the card DMA is not tried, so every
    # read takes CARDi_ReadRomSyncCore -- on the card's task thread when
    # asynchronous, at once when not, as the CPU path already does -- and
    # there the copy (the page cache, then card commands and data words read
    # 4 bytes at a time, each a trapped access) is one copy from the ROM image
    # into the destination: the card's common state's src (+0x1c), dst
    # (+0x20) and len (+0x24), left as the SDK's read leaves them. The
    # chip-ID check (answered by the port too, below), the result, the
    # flags, the wake-ups and the callback run as the SDK runs them.
    # KHDAYS_FILES=card keeps the card.
    "main@0200eea4": [(  # CARDi_ReadRom
        "    if (CARDi_TryReadCardDma(state)) {\n",
        "    extern int khdays_files_mode(void);\n"
        "    if (khdays_files_mode() != 0 && CARDi_TryReadCardDma(state)) {\n",
    )],
    # The card's chip ID, read after every ROM read to see the card is still
    # in (CARDi_CheckPulledOutCore compares it with the copy kept at boot):
    # with the port's reads the value the card answers, without its command.
    # The movies (runtime/movies/video.h). The frame's
    # decoder is the decomp's portable reconstruction of the payload ov024
    # copied to the ITCM (libs/mobiclip/video/portable, MobiClip_DecodeFrameCore:
    # the same state, planes and returned size -- every frame of the 46
    # movies checked against the payload before the ARM interpreter went),
    # called where the C calls the payload through the owner's +0x38 (through
    # runtime/movies/video.c, which hands it the state with host pointers); the
    # payload is still copied, as the game does, and nothing runs it. The
    # blit is native/hal/sdk_mobiclip.c. Both timed for KHDAYS_PROFILE.
    "ov024@020859d4": [(  # Ov024_MobiClip_DecodeFrame
        "    **(int **)(ctx + 0x34) += (*(int (**)(int))(ctx + 0x38))(*(int *)(ctx + 0x34));\n",
        "    {\n"
        "        extern void khdays_video_decode_begin(void);\n"
        "        extern void khdays_video_decode_end(void);\n"
        "        extern int khdays_video_decode(void *pState);\n"
        "        int consumed;\n"
        "        khdays_video_decode_begin();\n"
        "        consumed = khdays_video_decode((void *)*(int *)(ctx + 0x34));\n"
        "        khdays_video_decode_end();\n"
        "        **(int **)(ctx + 0x34) += consumed;\n"
        "    }\n",
    )],
    "ov024@02085ab8": [(  # Ov024_MobiClip_BlitFrame (C++, its externs inside extern "C")
        "extern void Ov024_MobiClip_BlitRows(MobiClipBlitRequest *pRequest);\n",
        "extern void Ov024_MobiClip_BlitRows(MobiClipBlitRequest *pRequest);\n"
        "extern void khdays_video_blit_begin(u32 destination, u32 stride, u32 height);\n"
        "extern void khdays_video_blit_end(void);\n"
        "extern void khdays_video_frame_blitted(u32 request);\n",
    ), (
        "    Ov024_MobiClip_BlitRows(&pDecoder->blit);\n",
        "    khdays_video_blit_begin((u32)pDecoder->blit.pDest, (u32)pDecoder->blit.nStride, pDecoder->nHeight);\n"
        "    Ov024_MobiClip_BlitRows(&pDecoder->blit);\n"
        "    khdays_video_blit_end();\n"
        "    khdays_video_frame_blitted((u32)&pDecoder->blit); /* the port's picture of it (US2) */\n",
    )],
    # A movie's sound: the channels Ov024_MobiClip_StartAudio locks (mask 3,
    # channels 0 and 1) until Ov024_MobiClip_ReleaseAudioChannel unlocks
    # them -- their category is the movies' (sound_bridge.h).
    "ov024@020840a4": [(  # Ov024_MobiClip_StartAudio
        "    SND_LockChannel(3, 0);\n",
        "    SND_LockChannel(3, 0);\n"
        "    { extern void khdays_sound_movie_channels(u32 mask); khdays_sound_movie_channels(3); }\n",
    )],
    "ov024@020841c4": [(  # Ov024_MobiClip_ReleaseAudioChannel
        "    SND_UnlockChannel(3, 0);\n",
        "    SND_UnlockChannel(3, 0);\n"
        "    { extern void khdays_sound_movie_channels(unsigned int mask); khdays_sound_movie_channels(0); }\n",
    )],
    # A movie a mod replaces (runtime/movies/video_mod.h):
    # the path the game opens a stream at, and the movie's stop.
    "ov024@02084650": [(  # Ov024_MobiClip_OpenStreamSlot
        "    if (FS_OpenFile(data_ov024_02093a48 + idx * 0x48, Msg_BuildLangPath(path)) == 0) {\n",
        "    const int built = Msg_BuildLangPath(path);\n"
        "    { extern void khdays_video_movie_opened(int slot, const char *path);\n"
        "      khdays_video_movie_opened(idx, (const char *)built); }\n"
        "    if (FS_OpenFile(data_ov024_02093a48 + idx * 0x48, built) == 0) {\n",
    )],
    "ov024@02084b14": [(  # Ov024_MobiClip_StopPlayback
        "    pAudio = data_ov024_02093a2c.pAudio;\n",
        "    { extern void khdays_video_movie_stopped(void); khdays_video_movie_stopped(); }\n"
        "    pAudio = data_ov024_02093a2c.pAudio;\n",
    )],
    # KHDAYS_TRACE_TIMING (runtime/core/events.h): the timer 1
    # interrupts (the alarms), timer 0's overflows (the tick's count-up) and
    # the movie's flips, counted.
    "main@0200316c": [(  # OSi_CountUpTick
        "    OSi_TickCounter++;\n",
        "    { extern void khdays_timing_countup(void); khdays_timing_countup(); }\n    OSi_TickCounter++;\n",
    )],
    "ov024@02084438": [(  # Ov024_MobiClip_PresentOnVBlank
        "    if (nScanline < 0xa0 || nScanline >= 0x104) {\n        return;\n    }\n",
        "    if (nScanline < 0xa0 || nScanline >= 0x104) {\n"
        "        { extern void khdays_timing_flip(int outside); khdays_timing_flip(1); }\n"
        "        return;\n    }\n",
    ), (
        "        pTimer->bPresented = 1;\n",
        "        pTimer->bPresented = 1;\n"
        "        { extern void khdays_timing_flip(int outside); khdays_timing_flip(0); }\n",
    ), (
        # The display line: this runs in the VBlank interrupt's
        # task list (OSi_VBlankInterruptHandler), where the DS reads line 192
        # -- inside the window it checks. No scan: 192 while the VBlank
        # handler runs, 0 anywhere else.
        "    nScanline = *reg_vcount;\n",
        "    { extern int khdays_events_in_vblank(void); nScanline = khdays_events_in_vblank() ? 192 : 0; }\n",
    )],
    # The SDK's tick and alarm timer on the PC's clock (hal/sdk_time.c;):
    # no timer register is touched.
    "main@020030e4": [(  # OS_InitTick
        "    TM0CNT_H = 0;\n    TM0CNT_L = 0;\n    TM0CNT_H = 0xC1;\n",
        "    { extern void khdays_tick_set(unsigned long long tick); khdays_tick_set(0); }\n",
    )],
    "main@020031d4": [(  # OS_GetTick
        "OSTick OS_GetTick (void)\n{\n",
        "OSTick OS_GetTick (void)\n{\n"
        "    { extern unsigned long long khdays_tick_now(void); return khdays_tick_now(); }\n",
    )],
    "main@02003274": [(  # OS_GetTickLo
        "    return *(volatile unsigned short *)0x04000100;\n",
        "    { extern unsigned long long khdays_tick_now(void); return (unsigned short)khdays_tick_now(); }\n",
    )],
    "main@02003284": [(  # OS_SetTick
        "void OS_SetTick(u64 nTick)\n{\n",
        "void OS_SetTick(u64 nTick)\n{\n"
        "    { extern void khdays_tick_set(unsigned long long tick); khdays_tick_set(nTick); return; }\n",
    )],
    "main@020032f4": [(  # OSi_SetTimer
        "void OSi_SetTimer (OSAlarm * alarm)\n{\n",
        "void OSi_SetTimer (OSAlarm * alarm)\n{\n"
        "    {\n"
        "        extern void khdays_alarm_timer_for(unsigned long long fire);\n"
        "        OSi_EnterTimerCallback(OSi_ALARM_TIMER, OSi_AlarmHandler, NULL);\n"
        "        khdays_alarm_timer_for(alarm->fire);\n"
        "        (void)OS_EnableIrqMask(OSi_ALARM_IE_TIMER);\n"
        "        return;\n"
        "    }\n",
    )],
    "main@020036b0": [(  # OSi_ArrangeTimer (and KHDAYS_TRACE_TIMING's count)
        "    OS_SetTimerControl(OSi_ALARM_TIMER, 0);\n    (void)OS_DisableIrqMask(OSi_ALARM_IE_TIMER);\n",
        "    { extern void khdays_timing_alarm(void); khdays_timing_alarm(); }\n"
        "    { extern void khdays_alarm_clear(void); khdays_alarm_clear(); }\n"
        "    (void)OS_DisableIrqMask(OSi_ALARM_IE_TIMER);\n",
    )],
    "main@02003b68": [(  # OS_GetLowEntropyData: entropy, not a line
        "static inline u16 GX_GetVCount(void) { return reg_GX_VCOUNT; }",
        "static inline u16 GX_GetVCount(void) { extern unsigned long long khdays_clock_cycles(void); "
        "return (u16)khdays_clock_cycles(); }",
    )],
    # The game's loops that wait on an interrupt taken inside them:
    # each halts at a wait point until an interrupt is due and
    # takes it (runtime/core/events.h khdays_wait_point), as the CPU would take it
    # between two of the loop's instructions; the loop then checks again.
    "ov012@0205b618": [(  # Ov012_RunOpeningScene: the opening's movie loop
        "                } while (Ov024_TickStreamSlots() == 0);\n",
        "                    { extern void khdays_wait_point(void); khdays_wait_point(); }\n                } while (Ov024_TickStreamSlots() == 0);\n",
    ), (
        # An HD video in the opening's place (runtime/movies/movie_hd.h):
        # its flags each turn -- bit 2 set by the START skip
        # just below; bit 3 given, as the opening reads START with no
        # condition, unlike the other movies (Ov024_MobiClip_UpdatePlayback).
        "                do {\n                    if ((*(u16 *)(context + 2) & 4) != 0) {\n",
        "                do {\n                    { extern void khdays_movie_hd_flags(unsigned flags);\n"
        "                      khdays_movie_hd_flags(*(u16 *)(context + 2) | 8u); }\n"
        "                    if ((*(u16 *)(context + 2) & 4) != 0) {\n",
    )],
    # The credits scene, ov011 (runtime/movies/movie_hd.h):
    # its movie ("843.z" from UI/sf/m.p2, the ov024 player opened through
    # Ov024_MobiClip_InstallStreamSourceVtbl) is followed by the staff roll,
    # which the HD video carries too -- so the scene's begin, the roll's end
    # (Ov011_StartTitleExit, from Ov011_TickTitleMenu's mode 3) and the
    # scene's end are told.
    "ov011@0205ac40": [(  # Ov011_CreateScene
        "    Ov024_MobiClip_InstallStreamSourceVtbl(pStream);\n",
        "    { extern void khdays_movie_hd_credits(int phase); khdays_movie_hd_credits(1); }\n"
        "    Ov024_MobiClip_InstallStreamSourceVtbl(pStream);\n",
    )],
    "ov011@0205c2fc": [(  # Ov011_StartTitleExit
        "    data_ov011_0205e960.pScene->nMode = MODE_EXIT;\n",
        "    data_ov011_0205e960.pScene->nMode = MODE_EXIT;\n"
        "    { extern void khdays_movie_hd_credits(int phase); khdays_movie_hd_credits(2); }\n",
    )],
    "ov011@0205b034": [(  # Ov011_DestroyScene
        "void Ov011_DestroyScene(void)\n{\n",
        "void Ov011_DestroyScene(void)\n{\n"
        "    { extern void khdays_movie_hd_credits(int phase); khdays_movie_hd_credits(0); }\n",
    )],
    "ov024@02082f04": [(  # Ov024_MobiClip_UpdatePlayback: the movies' loop
        "            }\n        }\n\n    drain:\n",
        "            }\n            { extern void khdays_wait_point(void); khdays_wait_point(); }\n        }\n\n    drain:\n",
    ), (
        # An HD video in the movie's place (runtime/movies/movie_hd.h):
        # the player's flags each turn -- bit 3 the DS lets
        # START skip the movie (Ov024_MobiClip_OpenPlayer's argument), bit 2
        # the skip taken.
        "        while (Ov024_TickStreamSlots() == 0) {\n            if (player->wFlags & 4) {\n",
        "        while (Ov024_TickStreamSlots() == 0) {\n"
        "            { extern void khdays_movie_hd_flags(unsigned flags); khdays_movie_hd_flags(player->wFlags); }\n"
        "            if (player->wFlags & 4) {\n",
    )],
    "main@0200ca50": [(  # TP_WaitBusy: the touch panel's reply (PXI)
        "    while (*(volatile unsigned short *)(&data_02046390 + 0x3a) & mask) {\n    }\n",
        "    while (*(volatile unsigned short *)(&data_02046390 + 0x3a) & mask) {\n        { extern void khdays_wait_point(void); khdays_wait_point(); }\n    }\n",
    )],
    "main@0200cab8": [(  # PMi_WaitBusy: the power manager's reply (PXI)
        "        if (OS_GetCpsrIrq() == 0x80) {\n            PXIi_HandlerRecvFifoNotEmpty();\n        }\n",
        "        if (OS_GetCpsrIrq() == 0x80) {\n            PXIi_HandlerRecvFifoNotEmpty();\n        } else { extern void khdays_wait_point(void); khdays_wait_point(); }\n",
    )],
    # The camera of each 3D list (gfx/g3d.h khdays_g3d_frame_camera):
    # what NitroSystem's flush sends, as it sends it.
    # NitroSystem's NNS_G3dGlbFlush, which the decomp calls
    # Gfx_SubmitCachedCommandBlock: the whole NNSG3dGlb block at once, the
    # projection at +0x08 and the camera at +0x4c.
    "main@0201571c": [(  # NNS_G3dGlbFlush
        "    unsigned int *p = (unsigned int *)&NNS_G3dGlb;\n",
        "    unsigned int *p = (unsigned int *)&NNS_G3dGlb;\n"
        "    { extern void khdays_g3d_frame_camera(const int *projection, const int *camera);\n"
        "      khdays_g3d_frame_camera((const int *)((const char *)&NNS_G3dGlb + 0x08),\n"
        "                              (const int *)((const char *)&NNS_G3dGlb + 0x4c)); }\n",
    )],
    "main@020157c4": [(  # NNS_G3dGlbFlushP
        "void NNS_G3dGlbFlushP(void)\n{\n",
        "void NNS_G3dGlbFlushP(void)\n{\n"
        "    { extern void khdays_g3d_frame_camera(const int *projection, const int *camera);\n"
        "      khdays_g3d_frame_camera((const int *)&NNS_G3dGlb.projMtx, (const int *)&NNS_G3dGlb.cameraMtx); }\n",
    )],
    "main@0200edb0": [(  # CARDi_ReadRomIDCore
        "    CARDi_SetRomOp(MROMOP_G_READ_ID, 0);\n",
        "    {\n"
        "        extern int khdays_files_mode(void);\n"
        "        extern u32 khdays_files_chip_id(void);\n"
        "        if (khdays_files_mode() == 0) {\n"
        "            return khdays_files_chip_id();\n"
        "        }\n"
        "    }\n"
        "    CARDi_SetRomOp(MROMOP_G_READ_ID, 0);\n",
    )],
    "main@0200ee0c": [(  # CARDi_ReadRomSyncCore
        "    if (CARDi_ReadFromCache(state)) {\n        (*state->read_func)(state);\n    }\n",
        "    {\n"
        "        extern int khdays_files_mode(void);\n"
        "        extern void khdays_files_read(unsigned int src, void *dst, unsigned int len);\n"
        "        struct CARDiCommon *c = &data_020464e0;\n"
        "        if (khdays_files_mode() == 0) {\n"
        "            khdays_files_read(c->src, (void *)c->dst, c->len);\n"
        "            c->src += c->len;\n"
        "            c->dst += c->len;\n"
        "            c->len = 0;\n"
        "        } else if (CARDi_ReadFromCache(state)) {\n"
        "            (*state->read_func)(state);\n"
        "        }\n"
        "    }\n",
    )],
    # A music stream prepared: its player has its channels, their rings, the
    # file's header and its first sample (docs/RUNTIME_MAP.md section 6,
    # "The music streams' ring"). The port reads the stream's file itself
    # from there (runtime/sound/sound_bridge.c, sound/stream_reader.c), so the
    # channels play what the ring holds whether or not the stream thread
    # has had its turn; the player goes on as before. NNS_SndArcStrmStart
    # (0x0201cb3c) is the game's only way here. The first sample is the one
    # computed at the top (`tmpU64`): by the end the stream thread has
    # already filled the ring and moved `curSample` on (NNS_SndStrmSetup's
    # first load wakes it, and it runs first).
    # The game asking for a music stream, for the delay until it is heard
    # (runtime/sound/sound_bridge.c, KHDAYS_TRACE_SOUND_COMMANDS).
    # Nothing the stream player does changes.
    "main@0201cac0": [(  # NNS_SndArcStrmPrepare
        "    strmInfo = NNS_SndArcGetStrmInfo(strmNo);\n",
        "    { extern void khdays_sound_stream_requested(int stream_number); khdays_sound_stream_requested(strmNo); }\n"
        "    strmInfo = NNS_SndArcGetStrmInfo(strmNo);\n",
    )],
    "main@0201ce18": [(  # PrepareStrm
        "        NNS_SndStrmSetChannelPan(&player->stream, 1, 127);\n    }\n\n    return TRUE;\n",
        "        NNS_SndStrmSetChannelPan(&player->stream, 1, 127);\n    }\n\n"
        "    { extern void khdays_sound_stream_prepared(int player, int stream_number, int channels,\n"
        "          const u8 *channel, u32 ring, u32 ring_bytes, const void *header, u32 file_at, int in_memory,\n"
        "          u32 top, u32 bottom, u32 start_sample, int mono);\n"
        "      extern void *NNS_SndArcGetFileAddress(u32 fileId);\n"
        "      khdays_sound_stream_prepared(player->playerNo, strmNo, player->stream.numChannels,\n"
        "          player->stream.channelNo,\n"
        "          (u32)player->buffer, player->stream.chBufLen, &player->info, player->fileOffset,\n"
        "          NNS_SndArcGetFileAddress(strmInfo->fileId) != NULL, player->file.prop.file.top,\n"
        "          player->file.prop.file.bottom, (u32)tmpU64, player->monoFlag); }\n"
        "    return TRUE;\n",
    )],
    # A sequence starts on an ARM7 sequence player (seqPlayer->playerNo, the
    # one SND_PrepareSeq names) for one of the sound archive's players
    # (seqPlayer->player, &data_0204a760[n], which NNSi_SndPlayerAllocSeqPlayer
    # set): the runtime learns which archive player each ARM7 player serves, so
    # the volumes can tell music, effects and voices apart (sound/categories.c;
    # docs/RUNTIME_MAP.md section 6). StartSeq and StartSeqArc both come here.
    # Which sequence, or which entry of which sequence archive, an ARM7
    # player plays, from the two functions that start them (sndarc_player.c)
    # once NNSi_SndPlayerStartSeq has prepared it: a mod's file may replace
    # it (runtime/sound/sound_mods.c; kinds as native/sound/archive.h numbers them).
    # With it the tag of the command list that prepares it
    # (seqPlayer->commandTag, as NNSi_SndPlayerMain waits on it).
    "main@0201c734": [(  # StartSeq
        # its entry, for the time its loading takes
        "    player = NNSi_SndPlayerAllocSeqPlayer(handle, playerNo, playerPrio);\n",
        "    { extern void khdays_sound_sequence_requested(void); khdays_sound_sequence_requested(); }\n"
        "    player = NNSi_SndPlayerAllocSeqPlayer(handle, playerNo, playerPrio);\n",
    ), (
        "    NNS_SndPlayerSetSeqNo(handle, seqNo);\n",
        "    NNS_SndPlayerSetSeqNo(handle, seqNo);\n"
        "    { extern void khdays_sound_sequence_started(int arm7_player, int kind, int number, int entry,\n"
        "          u32 command_tag);\n"
        "      khdays_sound_sequence_started(player->playerNo, 0, seqNo, -1, player->commandTag); }\n",
    )],
    "main@0201c830": [(  # StartSeqArc
        "    NNS_SndPlayerSetSeqArcNo(handle, seqArcNo, index);\n",
        "    NNS_SndPlayerSetSeqArcNo(handle, seqArcNo, index);\n"
        "    { extern void khdays_sound_sequence_started(int arm7_player, int kind, int number, int entry,\n"
        "          u32 command_tag);\n"
        "      khdays_sound_sequence_started(player->playerNo, 1, seqArcNo, index, player->commandTag); }\n",
    )],
    # Where each wave archive is once a bank's loading has it (sndarc_loader.c):
    # the channels that start on a wave a mod replaces play its file
    # (runtime/sound/sound_mods.c). A wave archive loaded wave by wave has its
    # waves' places filled in after (LoadSingleWaves); they are read when a
    # channel starts.
    "main@0201be70": [(  # NNSi_SndArcLoadWaveArc
        "    if (pData != NULL) *pData = waveArc;\n",
        "    { extern void khdays_sound_wave_archive_loaded(int wave_archive, const void *address);\n"
        "      khdays_sound_wave_archive_loaded(waveArcNo, waveArc); }\n"
        "    if (pData != NULL) *pData = waveArc;\n",
    )],
    "main@0201a568": [(  # NNSi_SndPlayerStartSeq
        "    player = seqPlayer->player;\n",
        "    player = seqPlayer->player;\n"
        "    { extern void khdays_sound_player_assigned(int arm7_player, int archive_player);\n"
        "      extern NNSSndPlayer data_0204a760[32];\n"
        "      khdays_sound_player_assigned(seqPlayer->playerNo, (int)(player - data_0204a760)); }\n",
    )],

    # The controls' enhancements meet the game in the following functions
    # (runtime/input/controls_hooks.c). Every call hands back what the game had
    # unless an enhancement the player switched on changes it; with
    # KHDAYS_TRACE_CONTROLS they also report what the game does with its
    # controls, the evidence each enhancement is built on (docs/RUNTIME_MAP.md).
    #
    # The field camera. The game turns it from `uVar20`: bits 4 and 8 turn
    # the yaw target by its rate, 1 and 2 tilt it within its limits; the
    # look view (Select, then the D-pad), a drag on the touch screen, and L/R
    # under Config option 0 = 1 set them (docs/RUNTIME_MAP.md). Where the game
    # applies them, the analog camera adds its own turn and tilt, the same
    # way, and marks the frame as a camera frame (0x10, a bit the turn and
    # tilt tests do not read) so the game does not pull the camera back
    # behind the player while it is being turned -- as with its own inputs.
    # The request the camera reads (the touch screen) is traced.
    # The field's pace with three or more players:
    # the game keeps frame mode 1 -- a step every three
    # frames, its battle and its 3D at 20 a second -- for a session of three
    # or four, the DS's processor and radio; the port keeps the pace of one
    # or two (mode 0, every two frames). KHDAYS_MP_DS_PACE=1 keeps the game's.
    "ov002@020679d4": [(  # Ov002_ConstructGameplayScene
        "    if (Session_IsActive() != 0 && *(int *)(Session_GetSetup() + 4) >= 3) {\n        i = 1;\n",
        "    if (Session_IsActive() != 0 && *(int *)(Session_GetSetup() + 4) >= 3) {\n"
        "        { extern int khdays_mp_field_mode(int players); i = khdays_mp_field_mode("
        "*(int *)(Session_GetSetup() + 4)); }\n",
    )],
    # A guest's prediction of its own character (runtime/net/mp_predict.c). The battle is
    # host-simulated: a host runs every character by Ov022_ActorUpdate's host
    # branch from the inputs Ov022_RefreshSlotRows copies in; a guest replays
    # the host's states. A guest's own character runs the host's branch, its
    # own last control copied in by the game's own Ov022_LoadNodeFromResource,
    # the host's state taken only to correct it, and what that branch sends
    # left to the host, which makes and sends it as today.
    # Inside the window (the own character's update) every "am I the host"
    # check answers as a host's (ROLE_QUERY below: khdays_mp_role), the
    # checks of "is this my character" as the guest's; the command menu runs
    # with the window paused, the guest's as before.
    "ov022@020a0008": [(  # Ov022_ActorUpdate
        "        Ov022_UpdateCommandInput(pActor);\n",
        "        { extern void khdays_mp_predict_pause(int paused); khdays_mp_predict_pause(1);\n"
        "          Ov022_UpdateCommandInput(pActor);\n"
        "          khdays_mp_predict_pause(0); }\n",
    )],
    "ov022@02087ac0": [(  # Ov022_Party_SyncAndUpdate
        "                Ov022_ActorUpdate(node);\n                Ov022_TickSubObjectChain(node);\n",
        "                { extern void khdays_mp_predict_open(int node, int local);\n"
        "                  extern void khdays_mp_predict_close(void);\n"
        "                  extern int Session_GetLocalPlayerIndex(void);\n"
        "                  extern int khdays_mp_predict_is_open(void);\n"
        "                  extern void func_ov022_020ad474(unsigned int *arg0);\n"
        "                  khdays_mp_predict_open(node, Session_GetLocalPlayerIndex());\n"
        "                  /* the host's tick for its characters (Ov022_RefreshSlotRows): the\n"
        "                   * event word reset and the pending one merged in */\n"
        "                  if (khdays_mp_predict_is_open() && (root->flags & 4) == 0) {\n"
        "                      func_ov022_020ad474((unsigned int *)node);\n"
        "                  }\n"
        "                  Ov022_ActorUpdate(node);\n"
        "                  Ov022_TickSubObjectChain(node);\n"
        "                  khdays_mp_predict_close(); }\n",
    )],
    "ov022@02087b7c": [(  # Ov022_Party_ApplyNetState
        "            changed = 0;\n\n            actor->flags01a = changed;\n",
        "            changed = 0;\n"
        "            { extern int khdays_mp_predict_keep(int own, int actor, int record);\n"
        "              extern void khdays_mp_predict_shift(int *x, int *y, int *z);\n"
        "              const int khdays_keep = khdays_mp_predict_keep(index == (unsigned int)QueryActiveStateOrDelegate(),"
        " (int)actor, (int)record);\n"
        "              if (khdays_keep != 0) {\n"
        "                  Ov022_ActorSetHp(actor, record->field20);\n"
        "                  /* visible as apply_entry has it: not while the field moves it to another\n"
        "                   * area (Ov002_StepRosterSlotRelease leaves its group -1 until\n"
        "                   * Ov002_StepRosterSlotPhase shows it in the new one) */\n"
        "                  func_ov022_020a0678(actor, localField66 != -1 && localField66 == actor->field066);\n"
        "                  /* what a host takes from its own character (Ov022_RefreshSlotRows):\n"
        "                   * the action lock and the camera's request */\n"
        "                  root->stateBits3c.bit4 = (*((unsigned char *)actor + 0x694) >> 3) & 1;\n"
        "                  if (*((signed char *)actor + 0x47a) > 0 && func_ov022_020886f8(index) == 0) {\n"
        "                      Ov002_Camera_SetMode(func_ov022_02083f0c(), *((signed char *)actor + 0x47a),\n"
        "                                           *((signed char *)actor + 0x47b));\n"
        "                  }\n"
        "                  if (khdays_keep == 2) {\n"
        "                      /* the host's run of it off the predicted path: moved by the difference */\n"
        "                      VecFx32 khdays_at = actor->position48c;\n"
        "                      khdays_mp_predict_shift(&khdays_at.x, &khdays_at.y, &khdays_at.z);\n"
        "                      Actor_SetVecAndSyncChild((void *)actor->child20, &khdays_at);\n"
        "                      actor->position48c = khdays_at;\n"
        "                  }\n"
        "                  continue;\n"
        "              } }\n\n"
        "            actor->flags01a = changed;\n",
    )],
    "main@02031258": [(  # MsgQueue_SendGate
        "    ret = 0;\n    doSend = 1;\n    switch (Session_GetLinkMode()) {\n",
        "    ret = 0;\n    doSend = 1;\n"
        "    { extern int khdays_mp_predict_drops(int gate, const void *data, int size);\n"
        "      const int khdays_drop = khdays_mp_predict_drops(param_1, param_2, param_3);\n"
        "      if (khdays_drop == 2) {\n"
        "          /* a guest's own shot: applied at once as a host applies its own (mode 2's\n"
        "           * dispatch below, AsyncMessage_Flush), the host's copy left out on arrival */\n"
        "          ((MsgHdr *)buf)->f0 = 1; /* as MsgQueue_Enqueue marks what the host sends */\n"
        "          ((MsgHdr *)buf)->type = param_1;\n"
        "          ((MsgHdr *)buf)->size = param_3;\n"
        "          MI_CpuCopy8(param_2, (unsigned char *)buf + 2, param_3);\n"
        "          dispatchByObjTypeBits((int)buf, param_3 + 2);\n"
        "          return 1;\n"
        "      }\n"
        "      if (khdays_drop != 0) {\n"
        "          return 1;\n"
        "      } }\n"
        "    switch (Session_GetLinkMode()) {\n",
    )],
    "ov002@0204d170": [  # Ov002_Camera_UpdateFollow
        ("  if ((piVar7[0xe] & 0x200U) == 0) {\n    if ((piVar7[0xe] & 2U) != 0) {\n",
         "  if ((piVar7[0xe] & 0x200U) == 0) {\n    if ((piVar7[0xe] & 2U) != 0) {\n"
         "      { extern int khdays_controls_camera_turn(int rate, int inverted, int bits, int flags, int flags2,"
         " int yaw, int target, int pitch);\n"
         "        int turn = (uVar23 & 0x400) != 0 ? 0 : khdays_controls_camera_turn(piVar7[0x2a],"
         " (piVar7[0xf] & 0x10U) != 0, uVar20, piVar7[0xe], piVar7[0xf], piVar7[0x16], piVar7[0x20],"
         " piVar7[0x21]);\n"
         "        if (turn != 0) {\n"
         "          piVar7[0x20] = (piVar7[0x20] + turn) & 0xffff;\n"
         "          if ((piVar7[0xe] & 0x10000U) != 0) func_ov022_02088280(idx, piVar7[0x20]);\n"
         "          uVar20 |= 0x10;\n"
         "        } }\n"),
        ("        piVar7[0x21] = piVar7[0x21] - (piVar7[0x2b] + iVar12);\n      }\n",
         "        piVar7[0x21] = piVar7[0x21] - (piVar7[0x2b] + iVar12);\n      }\n"
         "      { extern int khdays_controls_camera_pitch(int rate, int inverted);\n"
         "        int tilt = (uVar23 & 0x400) != 0 ? 0 : khdays_controls_camera_pitch(piVar7[0x2b] + iVar12,"
         " (piVar7[0xf] & 8U) != 0);\n"
         "        if (tilt > 0 && FX_Mul(fxRatio, 0x3400) > piVar7[0x21]) piVar7[0x21] += tilt;\n"
         "        if (tilt < 0 && FX_Mul(fxRatio, -0x2000) < piVar7[0x21]) piVar7[0x21] += tilt;\n"
         "        if (tilt != 0) uVar20 |= 0x10; }\n"),
        ("    puVar13 = Ov002_ResolveRequestUnlessKind2a(&request.x);\n",
         "    puVar13 = Ov002_ResolveRequestUnlessKind2a(&request.x);\n"
         "    { extern void khdays_trace_camera_request(int found, int x, int y, int kind, int state);\n"
         "      khdays_trace_camera_request(puVar13 != 0, request.x, request.y, request.kind, request.state); }\n"),
    ],
    # Lock-on: whether a target is locked, and the game's Config option 0
    # (the lock-on scheme), for separate lock-on; that the lock-on logic goes
    # on this frame with the buttons as they are (past its early returns --
    # flags0 bit 3, as with a tutorial's pages open -- and not suppressed:
    # flags0 bit 5 clear, or a paused session), so the lock-on buttons make
    # R presses only when the game will take them as lock-on; and, traced,
    # the lock-on logic's state before and after each frame and each call it
    # makes.
    "ov022@02085380": [  # Ov022_ReadSelectionInput
        ('#include "nitro/types.h"\n',
         '#include "nitro/types.h"\n'
         "extern void khdays_trace_call(const char *what);\n"
         "extern void khdays_trace_call_arg(const char *what, int arg);\n"
         "extern void *khdays_trace_pointer(const char *what, void *result);\n"),
        ("    held = gPadHeld;\n",
         "    { extern void khdays_controls_selection(int locked, int scheme, int held, int pressed, int flags,"
         " int active, int selected, int repeat100, int repeat200, int repeat_any);\n"
         "      khdays_controls_selection(context->flags0 & 4, GameState_GetField(0x37c4, 1), gPadHeld,"
         " gPadPressed, context->flags0, context->activationState, context->selectedIndex,"
         " context->repeat100, context->repeat200, context->repeatAny); }\n"
         "    held = gPadHeld;\n"),
        ("    if (suppressInput != 0) {\n        held = 0;\n        pressed = 0;\n    }\n",
         "    if (suppressInput != 0) {\n        held = 0;\n        pressed = 0;\n    }\n"
         "    { extern void khdays_controls_selection_live(int live); khdays_controls_selection_live(!suppressInput); }\n"),
        ("Ov022_SetSelectionEnabled(0);",
         "(khdays_trace_call_arg(\"SetSelectionEnabled\", 0), Ov022_SetSelectionEnabled(0));"),
        ("Ov022_SetSelectionEnabled(1);",
         "(khdays_trace_call_arg(\"SetSelectionEnabled\", 1), Ov022_SetSelectionEnabled(1));"),
        ("Ov022_TryLockOn();", "(khdays_trace_call(\"Ov022_TryLockOn\"), Ov022_TryLockOn());"),
        ("Ov022_PickSelectorTarget(0x100)",
         "khdays_trace_pointer(\"PickSelectorTarget(0x100)\", Ov022_PickSelectorTarget(0x100))"),
        ("Ov022_PickSelectorTarget(0x200)",
         "khdays_trace_pointer(\"PickSelectorTarget(0x200)\", Ov022_PickSelectorTarget(0x200))"),
        ("    Ov022_UpdateSelectionMarker();\n    Ov002_ReaimActor(object, 0);\n",
         "    { extern void khdays_trace_selection_after(int flags, int active, int selected, int value);\n"
         "      khdays_trace_selection_after(context->flags0, context->activationState, context->selectedIndex,"
         " context->selectedValue); }\n"
         "    Ov022_UpdateSelectionMarker();\n    Ov002_ReaimActor(object, 0);\n"),
    ],
    # The command menu: that the player has field control this frame (past
    # the suppression tests, with or without a HUD panel open) and the
    # game's Config option 6, for the command menu on the D-pad; and what
    # each press makes the menu do.
    "ov022@020a2978": [  # Ov022_UpdateCommandInput
        ('#include "nitro/types.h"\n',
         '#include "nitro/types.h"\n'
         "extern void khdays_trace_call(const char *what);\n"
         "extern void khdays_trace_call_arg(const char *what, int arg);\n"),
        ("    if (pActor->nMenuState == 2 || pActor->nMenuState == 3) {\n",
         "    { extern void khdays_controls_command_input(int scheme, int panel_open, int menu_state, int held,"
         " int pressed, int menu0, int menu1, int deck_word);\n"
         "      khdays_controls_command_input(GameState_GetField(0x37c6, 1), Ov002_Hud_IsPanelOpen(),"
         " pActor->nMenuState, gPadHeld, gPadPressed, pMenu[0], pMenu[1], Ov022_GetRepeatKeys()); }\n"
         "    if (pActor->nMenuState == 2 || pActor->nMenuState == 3) {\n"),
        ("Ov002_SelectShortcut(nCmd);",
         "(khdays_trace_call_arg(\"SetPanelModeForRequest\", nCmd), Ov002_SelectShortcut(nCmd));"),
        ("Ov002_Hud_ActivatePanelSlot();",
         "(khdays_trace_call(\"Hud_ActivatePanelSlot\"), Ov002_Hud_ActivatePanelSlot());"),
        ("Ov002_AcceptRequestAndNotify(1)",
         "(khdays_trace_call(\"AcceptRequestAndNotify\"), Ov002_AcceptRequestAndNotify(1))"),
        ("Ov002_RequestCrawlSkip();", "(khdays_trace_call(\"RequestCrawlSkip\"), Ov002_RequestCrawlSkip());"),
        ("Ov002_PanelCursorNext();", "(khdays_trace_call(\"PlayPanelConfirm\"), Ov002_PanelCursorNext());"),
        ("Ov002_PanelCursorPrev();", "(khdays_trace_call(\"PlayPanelCancel\"), Ov002_PanelCursorPrev());"),
        ("Ov002_PanelCursorStepLeft();",
         "(khdays_trace_call(\"PanelCursorStepLeft\"), Ov002_PanelCursorStepLeft());"),
        ("Ov002_PanelCursorStepRight();",
         "(khdays_trace_call(\"Panel_HandleConfirm\"), Ov002_PanelCursorStepRight());"),
    ],
    # The movement heading the game makes from the D-pad (Up 0, Left 0x4000,
    # Down 0x8000, Right 0xc000): analog movement gives the stick's instead,
    # for the local player's actor only.
    "ov022@020a0814": [(  # Ov022_HeadingFromHeldDpad
        "    if (cnt > 1) r >>= 1;\n    return r;\n",
        "    if (cnt > 1) r >>= 1;\n"
        "    { extern int khdays_controls_move_heading(int actor, int heading);\n"
        "      return khdays_controls_move_heading(actor, r); }\n",
    )],
    # The speed an idle actor moves toward and the animation speed it moves
    # with: analog movement's walk slows both; traced with the heading's
    # reference (the bias the game adds to it).
    "ov022@020a3ccc": [  # Ov022_SteerIdleByAim
        ("            nRate = pActor->nWalkSpeed;\n",
         "            { extern int khdays_controls_move_rate(int actor, int rate);\n"
         "              nRate = khdays_controls_move_rate((int)pActor, pActor->nWalkSpeed); }\n"),
        ("            vecMove.x = FxMul(vecMove.x, pActor->nStepRate);\n",
         "            { extern void khdays_trace_steer(int actor, int aim, int bias, int angle, int step, int rate);\n"
         "              khdays_trace_steer((int)pActor, nAimAngle, pActor->nAngleBias, nAngle, pActor->nStepRate,"
         " nRate); }\n"
         "            vecMove.x = FxMul(vecMove.x, pActor->nStepRate);\n"),
        ("                Ov022_SetAnimSpeed(pActor, (short)nRate);\n",
         "                { extern int khdays_controls_move_anim(int actor, int scale);\n"
         "                  Ov022_SetAnimSpeed(pActor, (short)khdays_controls_move_anim((int)pActor, nRate)); }\n"),
    ],
    # The states an actor enters (jump, guard, ...), traced for the local
    # player's actor: what each button makes Roxas do.
    "ov022@02098834": [(  # Ov022_EnterActorState
        "int Ov022_EnterActorState(struct Actor *pActor, int nState, u32 nPrevTimer)\n{\n",
        "int Ov022_EnterActorState(struct Actor *pActor, int nState, u32 nPrevTimer)\n{\n"
        "    { extern void khdays_trace_actor_state(int actor, int state);"
        " khdays_trace_actor_state((int)pActor, nState); }\n",
    )],
    # The frame-rate mode field play runs at, traced.
    "ov002@02067f78": [(  # Ov002_TickGameplayState
        "    SetGameMode((u8)*(int *)(base + 0x8b54));\n",
        "    { extern void khdays_trace_frame_mode(int value); khdays_trace_frame_mode(*(int *)(base + 0x8b54)); }\n"
        "    SetGameMode((u8)*(int *)(base + 0x8b54));\n",
    )],
    # The engine's three loads: which game file is
    # read into which buffer -- by path or by packed id, raw or decompressed --
    # for the port's names of the graphics the game uploads
    # (runtime/core/origin.c). Nothing the game does changes.
    "main@0201ebdc": [(  # Loader_RequestFile
        "    OS_SendMessage(data_0204bc1c, self, 1);\n    return (void *)self[10];\n",
        "    { extern void khdays_origin_loaded(unsigned top, unsigned bottom, const void *buffer, unsigned size,\n"
        "                                       int compressed);\n"
        "      khdays_origin_loaded((unsigned)self[3], (unsigned)self[4], (const void *)self[10], (unsigned)self[11],\n"
        "                           self[1]); }\n"
        "    OS_SendMessage(data_0204bc1c, self, 1);\n    return (void *)self[10];\n",
    )],
    "main@0201ed3c": [(  # FileLoader_LoadInto
        "    OS_SendMessage(data_0204bc1c, self, 1);\n    return self[11];\n",
        "    { extern void khdays_origin_loaded(unsigned top, unsigned bottom, const void *buffer, unsigned size,\n"
        "                                       int compressed);\n"
        "      khdays_origin_loaded((unsigned)self[3], (unsigned)self[4], (const void *)self[10], (unsigned)self[11],\n"
        "                           self[1]); }\n"
        "    OS_SendMessage(data_0204bc1c, self, 1);\n    return self[11];\n",
    )],
    "main@0201ef9c": [(  # Archive_LoadFile
        "    gFileLoader[5] = 0;\n    FS_CloseFile(&file);\n    return buf;\n",
        "    { extern void khdays_origin_loaded(unsigned top, unsigned bottom, const void *buffer, unsigned size,\n"
        "                                       int compressed);\n"
        "      khdays_origin_loaded(file.top, file.bottom, buf, buf != 0 ? size : 0, compressed != 0); }\n"
        "    gFileLoader[5] = 0;\n    FS_CloseFile(&file);\n    return buf;\n",
    )],
    # NitroSystem's geometry sends (RUNTIME_MAP §7 "The direct
    # geometry"): the command words it stores to GXFIFO one by one go to the
    # port's geometry (gfx/g3d.h) as they are, not through the I/O trap; its
    # blocks reach it through MIi_CpuSend32 (hal/sdk_mi.c). What the commands
    # do is unchanged.
    "itcm@01ff9f00": [(  # GX_SendFifoWords (NNS_G3dGeBufferOP_N)
        "    *(volatile u32 *)0x04000400 = op;\n",
        "    { extern void khdays_g3d_words(const unsigned int *words, unsigned int count);\n"
        "      khdays_g3d_words((const unsigned int *)&op, 1); }\n",
    )],
    "itcm@01ffa764": [(  # NNSi_G3dGeBufferCommand1
        "    *(volatile u32 *)0x04000400 = command;\n"
        "    *(volatile u32 *)0x04000400 = argument;\n",
        "    { extern void khdays_g3d_words(const unsigned int *words, unsigned int count);\n"
        "      const unsigned int words[2] = {command, argument};\n"
        "      khdays_g3d_words(words, 2); }\n",
    )],
    # A mod's models drawn instead of the game's (runtime/mods/models.h): each
    # model NitroSystem draws (NNSi_G3dDrawInternal, from start to end), each
    # node's matrix as NODEDESC leaves it -- after the node's callback, before
    # it is stored -- and its shapes' display lists left unsent while the
    # model is replaced (the SHP command), but for their matrix commands (a
    # model with a position scale scales the matrix the next nodes are placed
    # on in them). The game animates and draws as always; when no mod
    # replaces the model nothing changes.
    "itcm@01ffb8fc": [(  # NNSi_G3dDrawInternal
        "    do {\n        u32 command;\n",
        "    { extern void khdays_models_draw_begin(const void *model);\n"
        "      khdays_models_draw_begin(renderObj->pResMdl); }\n"
        "    do {\n        u32 command;\n",
    ), (
        "    renderObj->flags &= ~1;\n",
        "    { extern void khdays_models_draw_end(void); khdays_models_draw_end(); }\n"
        "    renderObj->flags &= ~1;\n",
    )],
    "itcm@01ffc164": [(  # NNSi_G3dFuncSbc_NODEDESC
        "    if (state->callbackTimings[6] == 3) {\n        state->sbcCallbacks[6](state);\n    }\n",
        "    if (state->callbackTimings[6] == 3) {\n        state->sbcCallbacks[6](state);\n    }\n"
        "    { extern void khdays_models_node(unsigned node, int sent);\n"
        "      khdays_models_node(nodeId, !(state->flags & 0x100)); }\n",
    )],
    "itcm@01ffc0d0": [(  # NNSi_G3dFuncSbcSHP
        "        NNS_G3dGeSendDL((u8 *)data + data->payloadOffset08, data->payloadSize0c);\n",
        "        { extern int khdays_models_shape(const void *list, unsigned int size);\n"
        "          if (!khdays_models_shape((u8 *)data + data->payloadOffset08, data->payloadSize0c)) {\n"
        "              NNS_G3dGeSendDL((u8 *)data + data->payloadOffset08, data->payloadSize0c);\n"
        "          } }\n",
    )],
}


# Hooks by the decomp file, for functions HOOKS cannot name: DS Protect's
# (ov028) are data in the symbols, their bodies being encrypted in the ROM,
# and several share a file. Applied like HOOKS.
# The routines whose drawing the port takes over,
# by the file that defines them: (function, part, the port's function, kind).
# The game's body stays, under `<function>__khdays_game`, and the routine
# asks its part's mode. "replace" -- off: the game's body; on: the port's,
# handed the same parameters; check: both (the port's first, then the
# game's, whose drawing the check compares). "follow" -- the game's body
# always, then (check, on) the port's, told what it was handed and what it
# returned. Parts: native/ui/parts.h.
UI_PARTS = {"text": 0, "hud": 1, "pause": 2, "camp": 3, "save": 4, "mission": 5, "shop": 6}
UI_HOOKS: dict[str, list[tuple[str, str, str, str]]] = {
    # The text: the two text surfaces that make
    # NitroSystem character canvases, the frees that end them, and the SDK's
    # loads of characters into VRAM that show them (the transfer queue's,
    # data_02041934, and the routines' own: Ov008_DrawPageBRows,
    # Ov002_BuildTileRow, ...).
    "src/engine/TileTextRenderer_Init.c": [("TileTextRenderer_Init", "text", "khdays_ui_text_renderer", "follow")],
    "src/engine/TileTextRenderer_Destroy.c": [
        ("TileTextRenderer_Destroy", "text", "khdays_ui_text_renderer_gone", "follow")],
    "src/engine/TileSurface_AddCanvas.c": [("TileSurface_AddCanvas", "text", "khdays_ui_text_surface_canvas", "follow")],
    # a canvas's buffer freed: the canvas is gone (ui/text.c khdays_ui_text_freed)
    "libs/nns/fnd/calls/NNS_FndFreeToExpHeap.c": [("NNS_FndFreeToExpHeap", "text", "khdays_ui_text_freed", "follow")],
    "libs/nitro/gx/calls/GX_LoadBG0Char.c": [("GX_LoadBG0Char", "text", "khdays_ui_text_chars_a0", "follow")],
    "libs/nitro/gx/calls/GX_LoadBG1Char.c": [("GX_LoadBG1Char", "text", "khdays_ui_text_chars_a1", "follow")],
    "libs/nitro/gx/calls/GX_LoadBG2Char.c": [("GX_LoadBG2Char", "text", "khdays_ui_text_chars_a2", "follow")],
    "libs/nitro/gx/calls/GX_LoadBG3Char.c": [("GX_LoadBG3Char", "text", "khdays_ui_text_chars_a3", "follow")],
    "libs/nitro/gx/calls/GXS_LoadBG0Char.c": [("GXS_LoadBG0Char", "text", "khdays_ui_text_chars_b0", "follow")],
    "libs/nitro/gx/calls/GXS_LoadBG1Char.c": [("GXS_LoadBG1Char", "text", "khdays_ui_text_chars_b1", "follow")],
    "libs/nitro/gx/calls/GXS_LoadBG2Char.c": [("GXS_LoadBG2Char", "text", "khdays_ui_text_chars_b2", "follow")],
    "libs/nitro/gx/calls/GXS_LoadBG3Char.c": [("GXS_LoadBG3Char", "text", "khdays_ui_text_chars_b3", "follow")],
    "libs/nitro/gx/calls/GX_LoadOBJ.c": [("GX_LoadOBJ", "text", "khdays_ui_text_obj_a", "follow")],
    "libs/nitro/gx/calls/GXS_LoadOBJ.c": [("GXS_LoadOBJ", "text", "khdays_ui_text_obj_b", "follow")],
}


def ui_hook(text: str, rel: str, name: str, part: str, port: str, kind: str = "replace") -> str:
    """`name`'s definition in `text` split in two: the game's body under a
    second name, static, and `name` asking `part`'s mode (UI_HOOKS)."""
    mask = abi_repair.code_mask(text)
    d = abi_repair.find_definition(text, mask, name)
    if d is None:
        raise SystemExit(f"UI hook: no definition of {name} in {rel}")
    head = text[d.ret_start:d.name_start]
    params = abi_repair.param_list(text, d)
    names = [abi_repair.param_name(p) for p in params]
    if any(not n for n in names):
        raise SystemExit(f"UI hook: {name} in {rel} has an unnamed parameter")
    void = re.sub(r"\b(?:static|inline|extern)\b", "", head).strip() == "void"
    signature = text[d.name_start:d.params[1] + 1]
    args = ", ".join(names)
    result_type = re.sub(r"\b(?:static|inline|extern)\b", "", head).strip()
    param_text = text[d.params[0]:d.params[1]].strip()
    if kind == "follow":
        told = ([param_text] if param_text not in ("", "void") else []) + ([] if void else [f"{result_type} khdays_result"])
        port_decl = f"extern void {port}({', '.join(told) or 'void'});"
    else:
        port_decl = f"extern {head.strip()} {port}({text[d.params[0]:d.params[1]]});"
    game = (text[:d.ret_start] + port_decl + "\nextern int khdays_ui_mode(int part);\n" +
            "static " + re.sub(r"\b(?:static|inline)\b\s*", "", head) + name + "__khdays_game" +
            text[d.name_start + len(name):d.body[1] + 1])
    mode = f"    const int khdays_mode = khdays_ui_mode({UI_PARTS[part]});  /* the port's {part} (prepare.py UI_HOOKS) */\n"
    if kind == "follow" and void:
        body = (f"\n{{\n{mode}    {name}__khdays_game({args});\n"
                f"    if (khdays_mode != 0) {{\n        {port}({args});\n    }}\n}}")
    elif kind == "follow":
        body = (f"\n{{\n{mode}    {result_type} khdays_result = {name}__khdays_game({args});\n"
                f"    if (khdays_mode != 0) {{\n        {port}({args + ', ' if args else ''}khdays_result);\n    }}\n"
                f"    return khdays_result;\n}}")
    elif void:
        body = (f"\n{{\n    const int khdays_mode = khdays_ui_mode({UI_PARTS[part]});  /* the port's {part} (prepare.py UI_HOOKS) */\n"
                f"    if (khdays_mode != 0) {{\n        {port}({args});\n    }}\n"
                f"    if (khdays_mode != 2) {{\n        {name}__khdays_game({args});\n    }}\n}}")
    else:
        body = (f"\n{{\n    const int khdays_mode = khdays_ui_mode({UI_PARTS[part]});  /* the port's {part} (prepare.py UI_HOOKS) */\n"
                f"    if (khdays_mode == 2) {{\n        return {port}({args});\n    }}\n"
                f"    if (khdays_mode != 0) {{\n        {port}({args});\n    }}\n"
                f"    return {name}__khdays_game({args});\n}}")
    return game + "\n\n" + head + signature + body + text[d.body[1] + 1:]


# The game's "am I the host" checks (constitution
# 2.3.0): the local player's index queried against 0 -- `== 0`, `!= 0`,
# `if (...)`, `!...` -- answers through the port (runtime/net/mp_predict.c
# khdays_mp_role): as a host's while a guest's own character is predicted,
# as the game's at every other moment. A query compared with an owner or a
# slot (is this my character) stays the game's call.
ROLE_QUERY = re.compile(
    r"Session_GetLocalPlayerIndex\(\)(?=\s*[!=]=\s*0\b)"
    r"|(?<=!)Session_GetLocalPlayerIndex\(\)"
    r"|(?<=if \()Session_GetLocalPlayerIndex\(\)(?=\))"
    r"|(?<=if\()Session_GetLocalPlayerIndex\(\)(?=\))")
# ... and the same check through a variable, as Zexion's parts make it
# (Ov045_StepParts and its three twins: `player = ...; if (player == 0 ...`).
ROLE_QUERY_VAR = re.compile(r"(\b\w+ = )Session_GetLocalPlayerIndex\(\)(;\s*if \(\w+ == 0\b)")
# Only a character's own run asks through the port: the battle's actors
# (ov022) and the characters' overlays. What the character does to the
# others and to the world -- hits delivered or taken, HP, items, abilities,
# the mission's tallies, the shared tables, the members' events, the network
# -- stays the game's answer, so a guest never makes locally what the host
# decides and sends; and so does everything outside these
# (the field, the enemies, the field's objects, the engine).
ROLE_DIRS = ("src/overlays/field/ov022_battle/", "src/overlays/players/")
ROLE_KEPT = {
    # hits delivered to others
    "Ov022_DeliverHit", "Ov022_SweepCapsuleOverEntries", "Ov022_SweepCapsuleOverGroup",
    "Ov022_SweepCapsuleOverParts", "Ov022_SweepFanOverEntries", "Ov022_SweepFanOverGroup",
    "Ov022_SweepFanOverParts", "Ov022_SendSweepHit", "Ov022_NotifyPeerHit", "Ov022_ReportHit",
    "Ov022_BroadcastCue", "func_ov022_02088398",
    # hits taken, HP, items, abilities
    "Ov022_ReceiveHit", "Ov022_ApplyDamageAndFlagHit", "Ov022_ApplyScaledDamage", "Ov022_ActorSetHp",
    "Ov022_FireReaction", "Ov022_ChargeGaugeForHit", "Ov022_SpendGuardOnHit", "Ov022_TryBlockHit",
    "Ov022_TryTriggerFinisher", "Ov022_Member_ShowDamage", "Ov022_Member_ShowSpotMessage",
    "Ov022_ApplyItemEffect", "func_ov022_02093900", "func_ov022_020888b8", "func_ov022_020888ec",
    # the mission's tallies, the shared tables, the members' events, the network, the battle's own loop
    "func_ov022_02088984", "func_ov022_0208a0b4", "func_ov022_0208a134", "Ov022_PublishRecord",
    "Ov022_PublishTableSlot", "Ov022_SendSyncRequest", "Ov022_StepSyncRequest", "Ov022_RefreshSlotRows",
    "Ov022_Party_ApplyNetState", "Ov022_TickSceneDispatch", "func_ov022_020897dc", "func_ov022_02089704",
    "Ov022_DispatchEntryByKind", "Ov022_SetupPauseMenu", "Ov022_InitActor", "Ov022_UpdateCommandInput",
}


def role_scoped(rel: str) -> bool:
    """Whether a file's "am I the host" checks answer through the port (ROLE_QUERY)."""
    path = rel.replace("\\", "/")
    return any(path.startswith(d) for d in ROLE_DIRS) and Path(rel).stem not in ROLE_KEPT

FILE_HOOKS = {
    # DS Protect's own read of the card (ROMUtil_Read, which its flashcart
    # check uses on six pages): with the port's reads
    # each page comes from the port as a cartridge answers it (below 0x8000
    # from 0x8000 + (address & 0x1ff)), and its words go where the loop
    # places the card's; the card's registers are left alone, the card's
    # lock kept.
    "src/overlays/system/ov028_dsprotect/dsprot/dsprot_rom_util.c": [(
        "    DSPROT_RANGE_BEGIN(KEY_ROM_UTIL_READ)\n"
        "\n"
        "    registerBase = 1;\n",
        "    DSPROT_RANGE_BEGIN(KEY_ROM_UTIL_READ)\n"
        "\n"
        "    {\n"
        "        extern int khdays_files_mode(void);\n"
        "        extern void khdays_files_card_page(u32 address, u8 *page);\n"
        "        if (khdays_files_mode() == 0) {\n"
        "            u8 page[CARD_ROM_PAGE_SIZE];\n"
        "            offset = 0 - (addr & (CARD_ROM_PAGE_SIZE - 1));\n"
        "            addr += offset;\n"
        "            while (offset < size) {\n"
        "                khdays_files_card_page(addr, page);\n"
        "                for (i = 0; i < CARD_ROM_PAGE_SIZE; i += 4) {\n"
        "                    data = page[i] | page[i + 1] << 8 | page[i + 2] << 16 | (u32)page[i + 3] << 24;\n"
        "                    if (offset >= 0 && offset < size) {\n"
        "                        *(u32 *)((u32)dest + offset) = data;\n"
        "                    }\n"
        "                    offset += 4;\n"
        "                }\n"
        "                addr += CARD_ROM_PAGE_SIZE;\n"
        "            }\n"
        "            goto khdays_read_done;\n"
        "        }\n"
        "    }\n"
        "    registerBase = 1;\n",
    ), (
        "    *(vu16 *)(registerBase + REG_EXMEMCNT_OFFSET) = exmemOriginal;\n"
        "\n"
        "    DSPROT_RANGE_END(KEY_ROM_UTIL_READ)\n",
        "    *(vu16 *)(registerBase + REG_EXMEMCNT_OFFSET) = exmemOriginal;\n"
        "\n"
        "khdays_read_done:\n"
        "    DSPROT_RANGE_END(KEY_ROM_UTIL_READ)\n",
    )],
    # A DMA channel's three registers, in the SDK's order (source, then
    # destination and control through a pointer into the channel): through
    # the register model, whose control write runs the transfer (the
    # struct's element and field forms are not registers.py's).
    "libs/nitro/mi/auto/MIi_DmaSetChannelRegs.c": [(
        "    struct DmaChannel *p = (struct DmaChannel *)0x40000b0;\n"
        "    volatile unsigned int *q = &p[ch].dst;\n"
        "    p[ch].src = src;\n"
        "    q[0] = dst;\n"
        "    q[1] = ctrl;\n",
        "    khdays_io_store(0x040000b0u + (unsigned int)ch * sizeof(struct DmaChannel), 4, src);\n"
        "    khdays_io_store(0x040000b4u + (unsigned int)ch * sizeof(struct DmaChannel), 4, dst);\n"
        "    khdays_io_store(0x040000b8u + (unsigned int)ch * sizeof(struct DmaChannel), 4, ctrl);\n",
    )],
    # The card's data port (reg_CARD_DATA, 0x0200ecc0 `ldr` through its
    # address), read through the register model. Only KHDAYS_FILES=card
    # reads the card this way.
    "libs/nitro/card/calls/CARDi_ReadCard.c": [(
        "                    u32 data = reg_CARD_DATA;\n",
        "                    u32 data = khdays_io_load(0x04100010u, 4); /* the card's data port */\n",
    )],
    # An 8-bit character's two words' masks: shifts by a register (0x020139a0
    # `lsr r1`, 0x020139a8 `lsl r1`, 0x020139c4 `lsl r3`, 0x020139cc `lsr r3`)
    # whose count is 32 for an area starting, or ending, at the character's
    # fifth pixel. The ARM gives 0 for a shift by 32 or more; x64 takes the
    # count modulo 32, which would clear the other four pixels too.
    "libs/nns/g2d/calls/ClearChar.c": [(
        "                mask_0 = (u32)(~0) >> x8;\n",
        "                mask_0 = x8 < 32 ? (u32)(~0) >> x8 : 0;  /* the ARM's shift by 32 (prepare.py) */\n",
    ), (
        "                    mask_0 <<= x8;\n",
        "                    mask_0 = x8 < 32 ? mask_0 << x8 : 0;\n",
    ), (
        "                mask_1 = (u32)(~0) << rw8;\n",
        "                mask_1 = rw8 < 32 ? (u32)(~0) << rw8 : 0;\n",
    ), (
        "                    mask_1 >>= rw8;\n",
        "                    mask_1 = rw8 < 32 ? mask_1 >> rw8 : 0;\n",
    )],
}


# Calls whose native form the ROM shows but no mechanical rule gives (see
# abi_repair.py for the rules): file -> [(text, replacement)], each read from
# the function's disassembly. Applied before the mechanical repairs. Keyed
# like HOOKS.
ABI_FIXES = {
    # Ov025_MoveGridCursor passes nDx and nDy to Ov025_DrawCardCountAndIcons
    # set only when the context has a list node (0x02094ac8-0x02094ae0); on
    # the other path they hold what was there, on the DS too, and the callee
    # reads them only with a shape, which that path never has (pShape stays 0
    # from 0x02094990). 0 says so, without the leftover.
    "ov025@02094988": [  # Ov025_MoveGridCursor
        ("    int nDx;\n", "    int nDx = 0;  /* unread without a list node (native/abi) */\n"),
        ("    int nDy;\n", "    int nDy = 0;\n"),
    ],
    # Ov006_MissionBuildOptionRows passes `input` to Ov006_SetTitleMode
    # unset in single-player mode: the ROM's r4 there (`mov r0,r4`,
    # 0x02050f4c) is its caller's, Obj_UpdateAll's previous heap
    # (`mov r4,r0` after func_0202362c, 0x02023b5c) -- an address, which
    # Ov006_SetTitleMode ignores as it does 0 (only 1-4 are stored,
    # 0x02055b44-0x02055b58). Natively the garbage could fall in 1-4.
    "ov006@02050e1c": [  # Ov006_MissionBuildOptionRows
        ("    int input;\n", "    int input = 0;  /* the ROM's r4 here: a heap address, ignored (native/abi) */\n"),
    ],
    # The same in Ov008_MissionMenuWaitReady: with the parameters ready (+0x28
    # set, 0x0207cdc4) nValue is never written, and `mov r0,r4` (0x0207ce0c)
    # hands Ov008_MissionScene_SetMode its entry r4 -- Obj_UpdateAll's (the
    # state is the mission menu object's update, first returned by its class's
    # constructor Ov008_MissionMenuCreate), the heap address above.
    # Ov008_MissionScene_SetMode stores only 1-4 (0x020818b4-0x020818c0).
    "ov008@0207cd04": [  # Ov008_MissionMenuWaitReady
        ("    int nValue;\n", "    int nValue = 0;  /* the ROM's r4 here: a heap address, ignored (native/abi) */\n"),
    ],
    # Ov002_MapTouchToCell sets no cell for a touch at x <= 0x30 on the
    # tab's second row (`bls`, 0x02066168) and clamps r4 as it came into
    # 0x16..0x1d (0x0206619c-0x020661ac). Its one caller,
    # Ov002_HandlePageTouch, runs it as a cue box's callback (`blx r1`,
    # 0x02065c78) with r4 the cue context's address (`ldr r4,[r0,#0x0]`,
    # 0x02065be4), which clamps to 0x1d; natively the garbage could be any
    # cell.
    "ov002@020660b0": [  # Ov002_MapTouchToCell
        ("    int nCell;\n", "    int nCell = 0x1d;  /* the ROM's r4 here: an address, clamped to 0x1d (native/abi) */\n"),
    ],
    # Ov005's direction and confirmation handlers keep r4 when no key bit is
    # set: Ov005_UpdateMainScene's, its state table's end (two `ldm r4!`
    # from 0x0205b368, 0x02053d84/0x02053d90), 0x0205b388. Ov005_MoveSelection
    # takes that as no direction (its default path, 0x02053578, loads r0
    # afresh) and Ov005_UpdateConfirmation as no action (`cmp r4,#0x20;
    # popne`, 0x02054864). 0 does the same; natively the garbage could be a
    # key.
    "ov005@020536ac": [  # Ov005_HandleDirectionalInput
        ("    int direction;\n", "    int direction = 0;  /* the ROM's r4 here: an address, no direction (native/abi) */\n"),
    ],
    "ov005@020547e4": [  # Ov005_UpdateConfirmation
        ("    int action;\n", "    int action = 0;  /* the ROM's r4 here: an address, no action (native/abi) */\n"),
    ],
    # Ov001_SeedMathRandContexts mixes the entropy into accumulators it never
    # sets: r4 (`r`) and r6:r5 (`x`, low:high; 0x0204ccf0-0x0204cd18). Neither
    # Ov001_BootInit nor what it calls before keeps them, so they are main's:
    # r4 = 1 (`ldr r4,=1`, 0x02000be0, the overlay FS_LoadOverlay loads), and
    # r5/r6 as crt0's autoload left them (func_020009fc: r5 = the last block's
    # destination, r6 = its end with BSS; the table at 0x0204c1e0 ends with the
    # DTCM block, 0x027e0000 + 0x60 + 0xe00).
    "ov001@0204cccc": [  # Ov001_SeedMathRandContexts
        ("    unsigned r;\n    unsigned long long x;\n",
         "    unsigned r = 1;  /* the ROM's r4 here: main's (native/abi) */\n"
         "    unsigned long long x = 0x027e0000027e0e60ULL;  /* r5:r6, crt0's autoload's (native/abi) */\n"),
    ],
    # ModelAnimSet_Bind leaves texSrc in r0: it stores its fourth argument at
    # [sp,#4] on entry and reloads it to store the half at +0x0a, last
    # (`ldr r0,[sp,#4]; strh r0,[fp,#0xa]`, 0x0202a378). Resource_BindFileToSlot
    # returns that.
    "main@0202a208": [  # ModelAnimSet_Bind
        ("void ModelAnimSet_Bind(ModelAnimSet *set, ModelInst *inst, void *file, int texSrc)",
         "int ModelAnimSet_Bind(ModelAnimSet *set, ModelInst *inst, void *file, int texSrc)"),
        ("    set->texSrc = texSrc;\n}", "    set->texSrc = texSrc;\n    return texSrc;\n}"),
    ],
    # Ov002_IsPanelModeSet(fallback) returns its r0 untouched when no panel is
    # installed (`ldr r1,=0x0207f628; ldr r1,[r1]; cmp r1,#0; bxeq lr`,
    # 0x02061b80); here that r0 is whatever Ov002_RepublishHud left, several
    # calls deep (9, or what 0x0205d98c's paths leave). Without a panel both
    # ways do the same: the branch sums the tally rows (func_ov022_020882f8,
    # the count, only reads, 0x020882f8-0x0208830c) and calls
    # Ov002_AddToPanelTotal, which returns at once with no panel. Reached in
    # mission 61; 0 skips the branch.
    "ov002@02069d40": [  # Ov002_CreateAndRestoreHud
        ("extern int Ov002_IsPanelModeSet(void);",
         "extern int Ov002_IsPanelModeSet(int fallback);"),
        ("    if (Ov002_IsPanelModeSet()) {",
         "    if (Ov002_IsPanelModeSet(0)) {  /* no panel: either way nothing (native/abi) */"),
    ],
}

# Ov022_StartPauseMenu, an object's update, leaves r0 as it came while the
# lock byte (data_0204be04) is set (`bne` to `pop {r3-r5,pc}`, 0x02082c98).
# Obj_UpdateAll calls each update with its own address in r0 (`ldr r0,[r1,
# #0x14]; blx r0`, 0x02023b60), so the update returns itself: the object
# stays and tries again the next frame. The C's bare `return;` gave the
# native caller whatever was in the register (1 in mission 61,
# the maintainer: the port stopped, no function's reference).
ABI_FIXES["ov022@02082c90"] = [  # Ov022_StartPauseMenu
    ("         * object keeps this update and tries again next frame. */\n        return;\n",
     "         * object keeps this update and tries again next frame. */\n"
     "        return (void *)&Ov022_StartPauseMenu;  /* r0 as it came: the update itself (native/abi) */\n"),
]

# Bare `return;`s in functions with a value, whose callers read it: the ROM's
# r0 on those paths, read from each one's code (after
# Ov022_StartPauseMenu above; the 15 such functions of the decomp, the rest
# have no caller that reads the value or need a malformed subtitle).
# (Ov022_ResolveGuardBreakState and the mega shadows' Ov181-Ov184_OnHit had
# such returns; the decomp's C now returns their r0.)
# Ov022_TestShotHit's early exit (`ldmialt`, 0x02091188) leaves r0 as the last
# call left it: Ov022_ComputeStrengthDamage's damage without the descriptor's
# row values, Ov022_ScaleRowValues' 0 with them (it ends `mov r0,#0`,
# 0x020a2410). Ov022_ResolveShotHit reads it as the hit.
ABI_FIXES["ov022@02090f5c"] = [  # Ov022_TestShotHit
    ("    if (pShot->nField134 != 0 && pShot->nAge < FIRST_HIT_AGE) {\n        return;\n    }\n",
     "    if (pShot->nField134 != 0 && pShot->nAge < FIRST_HIT_AGE) {\n"
     "        return (pDesc->nFlags & DESC_ROW_VALUES) == 0 ? params.nValue : 0;  /* the ROM's r0 (native/abi) */\n"
     "    }\n"),
]
# The menus' clamp-and-notify (Ov025 and Ov008, four with one body: the same
# bytes) leave r0 as it came -- the first argument -- on the early exit
# (`ldmiaeq`, ov025 0x020a4ed8); their RunTwoPassUpdate callers read it.
for _key in ("ov025@020a4eb8", "ov025@020a9750", "ov008@02074150", "ov008@0206f914"):
    ABI_FIXES[_key] = [
        ("    if (param_1 == 0 || param_2 == 0 || param_4 == 0) {\n        return;\n    }\n",
         "    if (param_1 == 0 || param_2 == 0 || param_4 == 0) {\n"
         "        return param_1;  /* the ROM's r0: the first argument, as it came (native/abi) */\n    }\n"),
    ]

# Ov107_Actor_SetAttachSlot takes a fifth, stack argument. Five effect actors
# store it in the outgoing-argument slot themselves and call with four: the
# C's `frame.outgoing = 0x1000` is that store (Ov117: `mov r0,#0x1000;
# str r0,[sp]` at 0x020cc09c/0x020cc0a4, then `bl` at 0x020cc0c4). Natively
# the fifth argument is passed.
for _key, _proto in (("ov117@020cbfc4", "struct Vec4 *"), ("ov118@020cfc04", "struct Vec4 *"),
                     ("ov185@020cfa2c", "int"), ("ov186@020d184c", "int"), ("ov187@020d548c", "int")):
    ABI_FIXES[_key] = [
        (f"extern void Ov107_Actor_SetAttachSlot(struct Obj *, int, unsigned int, {_proto});",
         f"extern void Ov107_Actor_SetAttachSlot(struct Obj *, int, unsigned int, {_proto}, unsigned int);"),
        ("    Ov107_Actor_SetAttachSlot(self, 2, 2, 0);",
         "    Ov107_Actor_SetAttachSlot(self, 2, 2, 0, frame.outgoing);"),
    ]

# The tailbunker and blade families' joint callbacks (two locals that were the
# DS's frame of one 4x3 matrix: natively the matrix overran them) and three
# script ops calling ScriptVm_ReadOperandInt without the interpreter's r0/r1
# had fixes here; the decomp's C now has the whole matrix and both
# arguments.

# r9 across the object updates (runtime.c khdays_rom_r9): Obj_UpdateAll
# starts with main's (unknown) and leaves each update's result in it
# (`blx r0; mov sb, r0`, 0x02023b64/68).
ABI_FIXES["main@02023adc"] = [  # Obj_UpdateAll
    ("void Obj_UpdateAll(int paused)\n{",
     "extern void khdays_rom_r9_unknown(void), khdays_rom_r9_set(int value);\n"
     "void Obj_UpdateAll(int paused)\n{"),
    ("    gObjSystem[1] = gObjSystem[3];\n",
     "    khdays_rom_r9_unknown();  /* main's r9 (native/abi) */\n"
     "    gObjSystem[1] = gObjSystem[3];\n"),
    ("                  khdays_check_update(((int *)gObjSystem[1])[5], cb); }\n",
     "                  khdays_check_update(((int *)gObjSystem[1])[5], cb); }\n"
     "                khdays_rom_r9_set(cb);  /* `mov sb, r0` */\n"),
]
# The tag trackers' `swap` is r9 (sb), which the ROM never sets when the table
# has no selection getter (+0x48): it reads what its caller left there (the
# decomp's header calls it a ROM bug, kept). The eight copies are the same
# 0x1a0 bytes; in Ov025's, reached from the camp menu's update through
# Ov025_GetIdleHandler, Ov025_CommitPage and Ov025_TickSelectionWidget, none
# of which touches r9, that is the previous update's result (0 in the
# Holomisiones list). With a nonzero r9 and no setter (+0x44) the ROM calls
# address 0 -- the ITCM mirror, OSi_VBlankInterruptHandler: the port stops.
for _key in ("ov000@020561b4", "ov002@020540f0", "ov005@0204cdd0", "ov006@0204cdd0",
             "ov008@020551a4", "ov009@02051f68", "ov025@02088e4c", "ov026@02082ecc"):
    ABI_FIXES[_key] = [
        ("    int swap;\n",
         "    extern int khdays_rom_r9(const char *reader);\n"
         "    extern void khdays_abi_gap(const char *what);\n"
         "    int swap = *(int *)(param_1 + 0x48) != 0 ? 0 : khdays_rom_r9(\n"
         "        \"a tag tracker's swap without a getter: r9 before the frame's first update\");\n"),
        ("        if (swap != 0) {\n            (*(void (**)(int))(param_1 + 0x44))(*(int *)(e + 0x28));",
         "        if (swap != 0) {\n"
         "            if (*(int *)(param_1 + 0x44) == 0) {\n"
         "                khdays_abi_gap(\"a tag tracker's setter at address 0 (the ROM runs the ITCM there)\");\n"
         "            }\n"
         "            (*(void (**)(int))(param_1 + 0x44))(*(int *)(e + 0x28));"),
    ]

# Calls to an address several overlays share, where the function the ROM
# reaches is known: caller -> address -> the overlay loaded there. What the
# call needs is what that function takes, not the one the C names.
SHARED_CALL_TARGETS = {
    # The opening movie: the call at 0x0205b1f0 reaches ov024's
    # Ov024_MobiClip_OpenStreams(request), not ov008's Ov008_RebuildShopList
    # (confirmed at run time; docs/DECOMP_FINDINGS.md section 1).
    "ov012@0205b0cc": {0x020846c0: "ov024"},
}

# Argument registers the Ghidra scan counts as read but the ROM shows the
# function never reads (it sets them first), by function: the calls into it
# need nothing more than the C passes.
ABI_NOT_READ = {
    # Ov008_QueryFieldBySelector switches on r1 to eight helpers; seven load
    # their own r3 before the tail call (`ldr r12,=0x02056268; ldr r3,=...;
    # bx r12`, 0x020562f0, 0x020563a0-0x02056404), the eighth (0x02056304)
    # writes r3 before reading it.
    "ov008@02056478": (3,),
    # Its copy in ov005 is the same: seven helpers `ldr r3,[pc,#4]` before
    # `bx ip` (0x0204ea58, 0x0204eb08-0x0204eb6c); the eighth, 0x0204ea68,
    # reads r3 only after `ldrhls r3,[r1,#8]` under the same condition.
    "ov005@0204ebdc": (3,),
    # Ov002_LoadPanelSlots (Thumb) loads r2 from its literal pool at
    # 0x020550dc before anything reads it, and r3 is only saved by the push
    # that keeps the stack aligned until 0x0205510c sets it.
    "ov002@020550d0": (2, 3),
    # Ov011_TickLayoutAnimator sets r0 (`add r0,r4,#2`, 0x0205b34c) and then
    # branches to its loop test, which sets r1 (`ldr r1,[r8]`, 0x0205b7d8),
    # before anything reads either.
    "ov011@0205b340": (0, 1),
    # Ov011_BlitTileRow reads one stack argument, its fifth (`ldr r8,[sp,#0x20]`
    # after pushing eight registers, 0x0205b828); nothing reads the sixth.
    "ov011@0205b814": (5,),
}

# Values the C returns from functions reached only through pointers, where
# the ROM's pointer calls never read r0 (the rest the need filter finds
# mechanically: no C reads them). By the function whose value it is.
VALUE_NOT_READ = {
    # Returned by the enemies' tick hooks at +0xc (Ov131..133/161..165/202/
    # 203_TickWithChildRefresh, Ov134..136_ReleaseAndDestroy). Every `blx`
    # of an [actor,#0xc] hook sets r0 next (`ldr r0,[r5,#0x384]`, `mov r0,#0`,
    # `add r0,r5,#0x44`...) or calls Session_GetLocalPlayerIndex, which loads
    # r0 first (Ov107_Scene_Tick, 0x020c98fc); Ov212_BoneCallback returns it,
    # and no C reads that.
    "ov107@020c6980": "Ov107_ProcessObjectTick",
    # Returned by Ov285/286_ForwardAnimEvent, the animation hook at +0x1dc.
    # Its callers: Ov107_AiState_OnMessage branches to `mov r0,sl`
    # (0x020c7a10), Ov144/145_Construct `mov r0,r4`; Ov107_PostTagUpdate
    # returns it to Ov245_ResetMode and Ov245_SetNodeMode3, whose values no
    # C reads.
    "main@0203b9fc": "SetSubitemState",
}
# (The C of these uses the parameters as locals: the calls pass zeros for
# them, abi_repair.pad_short_calls, so the writes stay in the call's frame.)


# Globals some split files define that symbols.txt does not name: statics of
# one original translation unit, repeated in each file the decomp split it
# into. On the DS they are fields of a block the module's own symbol anchors
# (tools/share_bss.py of the decomp). Their definitions become references;
# these are the ones code uses, at the addresses the ROM's own code reaches
# them by (base + offset read from its disassembly). A used one missing here
# fails the link.
PHANTOMS = {
    # os_thread.c, block data_0204430c: OSi_RescheduleThread (+0x04), the
    # scheduler's current-thread pointer (func_02001f10: ldr [base,#8]),
    # OS_ExitThread's destructor stack (func_02001e0c: ldr [base,#0x1c]),
    # the thread id counter (func_020018ec: ldr/str [base,#0x20]).
    "OSi_RescheduleCount": 0x02044310,
    "OSi_CurrentThreadPtr": 0x02044314,
    "OSi_IsThreadInitialized": 0x02044318,
    "OSi_StackForDestructor": 0x02044328,
    "OSi_ThreadIdCount": 0x0204432c,
    # snd_command.c, block data_02044748 (func_020085f0: [base,#4] finished
    # tag, #0x10 free-list end, #0x14 queue read index, #0x1c waiting count;
    # func_0200851c: #0x8/#0xc reserve list and its end, #0x18 queue write
    # index, #0x20 current tag = 1).
    "sFinishedTag": 0x0204474c,
    "sReserveList": 0x02044750,
    "sReserveListEnd": 0x02044754,
    "sFreeListEnd": 0x02044758,
    "sWaitingCommandListQueueRead": 0x0204475c,
    "sWaitingCommandListQueueWrite": 0x02044760,
    "sWaitingCommandListCount": 0x02044764,
    "sCurrentTag": 0x02044768,
    # nns snd_resource.c, block data_0204a2fc (func_02019d4c: [base,#4] alarm
    # lock; func_02019cb8: [base,#8] channel lock).
    "sAlarmLock": 0x0204a300,
    "sChannelLock": 0x0204a304,
    # nns snd_stream.c, block data_0204ad8c (func_0201d0e0: [base,#4] prepare
    # thread; func_0201c8f4: str sDecodeBufferArea -> [base,#8]).
    "sPrepareThread": 0x0204ad90,
    "sDecodeBuffer": 0x0204ad94,
    # ov105 wh.c, block data_ov105_020c04c0 (func_ov105_020bf7f4: #0x28 receive
    # size, #0x2c send size, #0x14 receive buffer, #0x1c send buffer, #0x34
    # connect mode, #0x40 child WEP key generator; func_ov105_020bf5a8: #0x4c
    # WM buffer, #0x20 receiver, #0x30 error code, strh #0xc disconnect reason,
    # #0x38 accept judge; func_ov105_020bf704: strh #0x4 MP frequency).
    "sWh_nMpFreq": 0x020c04c4,
    "sWh_nDisconnectReason": 0x020c04cc,
    "sWh_pRecvBuffer": 0x020c04d4,
    "sWh_pSendBuffer": 0x020c04dc,
    "sWh_pReceiver": 0x020c04e0,
    "sWh_nRecvBufferSize": 0x020c04e8,
    "sWh_nSendBufferSize": 0x020c04ec,
    "sWh_nErrCode": 0x020c04f0,
    "sWh_nConnectMode": 0x020c04f4,
    "sWh_pJudgeAccept": 0x020c04f8,
    "sWh_pChildWEPKeyGenerator": 0x020c0500,
    "sWh_pWmBuffer": 0x020c050c,
}

# The game's objects the port's side reads by name (in the rewrite, by
# their DS address in the window: khdays_ds_symbols.h, from the decomp's
# symbols): the sound archive's path the game opens (runtime/sound/sound_bridge.c);
# what the one-screen picture tells the game's situation from (ui/situation.c:
# the scene controller, the pause's mode and kind, the
# SDK's VRAM banks and reserved timers, NitroSystem's texture VRAM manager,
# and three overlays' own variables).
PORT_DS_SYMBOLS = ("gSndSoundDataPath", "gSceneCtl", "gPauseMode", "data_0204bda4", "data_02044660",
                   "data_020446d6", "data_020446d8", "data_020446e2", "data_020446e6", "gGfdFrmTexRegions",
                   "data_ov000_0205ac24", "data_ov002_0207f9fc", "data_ov025_020b574c", "data_0204c2d0",
                   "data_0204c3d8", "data_ov022_020b2e78",
                   # the pause's context and Mission Mode's flags (runtime/menus/retry.c)
                   "data_0204be08", "data_0204c240",
                   # the game's language and its message databases' table (runtime/core/language.c)
                   "gLangPath", "data_0204c238")


def forward_tail_call(text: str, name: str) -> tuple[str, str]:
    """`T name() { return g(); }` for a ROM tail call: take and pass on four
    argument words (cdecl: a caller that pushed fewer leaves its own frame in
    the extra ones, which g does not read). Returns the text and what was
    done."""
    body = re.search(rf"(\b{re.escape(name)}\s*\()\s*(?:void)?\s*(\)\s*\{{\s*(?:return\s+)?)"
                     rf"(\w+)\s*\(\s*\)(\s*;\s*\}})", text)
    if body is None:
        return text, "left (the C passes its arguments or is not a lone call)"
    callee = body[3]
    before = text[:body.start()]
    decl = None
    for decl in re.finditer(rf"\b{re.escape(callee)}\s*\(([^()]*)\)\s*;", before):
        pass
    if decl is not None and decl[1].strip() not in ("", "void"):
        return text, f"left ({callee} is declared with parameters)"
    words = ", ".join(f"khdays_r{i}" for i in range(4))
    params = ", ".join(f"int khdays_r{i}" for i in range(4))
    new = f"{body[1]}{params}{body[2]}{callee}({words}){body[4]}"
    text = before + new + text[body.end():]
    # declarations of either that say `(void)` would contradict the call and
    # the definition
    for fn in (callee, name):
        text = re.sub(rf"(\b{re.escape(fn)}\s*\()\s*void\s*(\)\s*;)", r"\1\2", text)
    return text, f"forwards four words to {callee}"


def zero_size(expr: str, defines: dict[str, str]) -> bool:
    expr = re.sub(r"\b[A-Za-z_]\w*\b",
                  lambda m: defines.get(m[0], m[0]), expr)
    if not re.fullmatch(r"[0-9a-fA-FxX\s\+\-\*\(\)]+", expr):
        return False
    try:
        return eval(expr, {"__builtins__": {}}) == 0  # numbers, + - * and ( ) only
    except Exception:
        return False


def name_unnamed_params(match: re.Match) -> str:
    params = [p.strip() for p in match[2].split(",")]
    if params == ["void"] or params == [""]:
        return match[0]
    fixed = []
    for i, p in enumerate(params):
        if p == "...":
            fixed.append(p)
            continue
        tokens = re.findall(r"\w+|\*", p)
        unnamed = p.endswith("*") or len(tokens) <= 1 or tokens[-1] in (
            "int", "char", "short", "long", "u8", "u16", "u32", "s8", "s16", "s32",
            "fx32", "fx16", "BOOL", "void", "unsigned", "signed")
        fixed.append(f"{p} _unnamed{i}" if unnamed else p)
    return f"{match[1]}{', '.join(fixed)})"


# A cast of the math unit's or IME's / IE's address, `(T *)0x04000280`: the
# register reached without the trap's fault (runtime/core/io.h, khdays_io_cp and
# khdays_io_plain) -- the same register, the same values. The size of a read
# there is the pointed-to type's, for the math unit's results.
UNTRAPPED_REGISTER = re.compile(r"\(([^()]*?)\*\s*\)\s*0x0?40002([0-9a-fA-F]{2})\b")

# `*(T *)0x04000400 = value;` -- a word stored to a geometry command port
# (GXFIFO 0x400-0x43f, the command ports 0x440-0x5ff): given to the register
# model as the trap would give it (runtime/core/io.h, khdays_io_store), without
# the fault. A statement of its own, the value without a `;`.
GEOMETRY_STORE = re.compile(
    r"\*\s*\(\s*(?:volatile\s+)?(?:unsigned\s+int|unsigned|u32|vu32|s32|int|REGType32v)\s*\*\s*\)\s*"
    r"0x0?4000([45][0-9a-fA-F]{2})\s*=(?!=)\s*([^;]+);")


def untrapped_register(m: re.Match) -> str:
    offset = int(m[2], 16)
    pointee = m[1]
    # A static object's initializer must be a constant: such a pointer keeps
    # the address (and the trap). Its statement starts after the last ; { }.
    statement = m.string[max(m.string.rfind(c, 0, m.start()) for c in ";{}") + 1:m.start()]
    if re.match(r"\s*(?:#[^\n]*\n\s*)*static\b", statement):
        return m[0]
    if 0x80 <= offset < 0xc0:
        if re.search(r"64|long\s+long", pointee):
            size = 8
        elif re.search(r"\b(?:u16|s16|vu16|vs16|short)\b", pointee):
            size = 2
        elif re.search(r"\b(?:u8|s8|vu8|char)\b", pointee):
            size = 1
        else:
            size = 4
        return f"({pointee}*)khdays_io_cp(0x2{m[2]}u, {size})"
    if 0x08 <= offset < 0x0c or 0x10 <= offset < 0x14:
        return f"({pointee}*)khdays_io_plain(0x2{m[2]}u)"
    return m[0]


def transform(text: str) -> str:
    text = UNTRAPPED_REGISTER.sub(untrapped_register, text)
    text = SPLIT_HEADER.sub(r"\1 ", text)
    text = REGISTER_ASM.sub("", text)
    text = CLZ_ASM.sub(lambda m: f"{m[1]} = khdays_clz({m[2]});", text)
    text = END_LABEL.sub(lambda m: f"{m[1]};{m[2]}", text)
    text = DSPROT_RANGES_INCLUDE.sub(r'\1"khdays_dsprot_ranges.h"', text)
    text = VIRTUAL_METHOD.sub(r"\1__cdecl \2", text)
    text = EMPTY_FUNCTION.sub(r"void *\1(void *khdays_r0) { return khdays_r0; }  /* bx lr: r0 unchanged */", text)
    text = VIRTUAL_DTOR.sub(r"\1virtual void __cdecl khdays_mwcc_complete_dtor();  /* mwcc's slot 0 */\n\1\2",
                            text)
    text = DELETE_STATEMENT.sub(
        r"\1{ void *khdays_p = (void *)(\2); if (khdays_p) "
        r"((void (__cdecl *)(void *))(*(void ***)khdays_p)[1])(khdays_p); }", text)
    text = ARRAY_ASSIGN.sub(
        lambda m: f"{m[1]}KHDAYS_ARRAY_ASSIGN({m[2].strip()}, {m[3]}, {m[4].strip()}, {m[5].strip()});",
        text)
    defines = {m[1]: m[2] for m in NUMERIC_DEFINE.finditer(text)}

    def drop_zero(m: re.Match) -> str:
        name = m[2]
        # Only a member nothing accesses (`.name` / `->name`) can go.
        if zero_size(m[3], defines) and not re.search(rf"(\.|->)\s*{re.escape(name)}\b", text):
            return f"{m[1]}/* zero-size member {name} removed for MSVC */"
        return m[0]

    text = ZERO_MEMBER.sub(drop_zero, text)
    text = DEF_HEADER.sub(name_unnamed_params, text)
    text = widen_narrow_returns(text)
    return text


# A function returning a char or short: the ARM callee leaves the value
# extended to 32 bits in r0, and the decomp's callers often declare it `int`;
# MSVC returns it in AL/AX and leaves the rest of the register as it was. So the
# definition returns an int holding the narrowed value, as r0 does.
NARROW_TYPE = r"(?:u8|s8|u16|s16|(?:unsigned\s+|signed\s+)?char|(?:unsigned\s+|signed\s+)?short(?:\s+int)?)"
NARROW_DEFINITION = re.compile(
    rf"^((?:static\s+)?)({NARROW_TYPE})(\s+)(\w+)(\s*\([^;{{}}]*\)\s*\{{)", re.MULTILINE)
RETURN_VALUE = re.compile(r"\breturn\b(\s*)([^;]+?)\s*;")


# The decomp's C++ (ov024's MobiClip stream layer) as C, for the rewrite:
# its classes are only declared, to call through vtables
# that are the game's data and whose entries are C functions taking `this`
# first. After transform() -- which put mwcc's two destructor slots in place
# (VIRTUAL_DTOR), made every member __cdecl and spelled `delete` out -- a
# class is a struct with the vtable's reference first and its data members
# after, and a virtual call is a call through the vtable's slot, the object
# first: the call the ROM makes (ov024 0x02084ec4: `ldr r1,[r0]; ldr r1,[r1,#n]; blx r1`).
CLASS_DECL = re.compile(r"\bclass\s+(\w+)\s*\{(.*?)\n\};", re.S)
VIRTUAL_DECL = re.compile(r"virtual\s+(?:(.*?)\s+)?__cdecl\s+(~?\w+)\s*\(([^()]*)\)\s*$", re.S)
STRUCT_DEF = re.compile(r"^struct\s+(\w+)\s*\{", re.M)
EXTERN_C_BLOCK = re.compile(r'\bextern\s+"C"\s*\{')


def matching_brace(text: str, open_brace: int) -> int:
    """The `}` closing the `{` at `open_brace`, past comments and literals."""
    depth, i = 0, open_brace
    while i < len(text):
        c = text[i]
        if text.startswith("/*", i):
            i = text.index("*/", i) + 2
            continue
        if text.startswith("//", i):
            i = text.index("\n", i)
            continue
        if c in "\"'":
            j = i + 1
            while text[j] != c:
                j += 2 if text[j] == "\\" else 1
            i = j + 1
            continue
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    raise SystemExit("prepare.py: an unclosed brace")


def cpp_to_c(text: str, rel: str) -> str:
    methods: dict[str, tuple[int, str, str]] = {}  # name -> (slot, result, parameters)

    def class_struct(m: re.Match) -> str:
        name, body = m[1], re.sub(r"\b(?:public|private|protected)\s*:", "", m[2])
        members, slot = [], 0
        for decl in body.split(";"):
            d = re.sub(r"/\*.*?\*/|//[^\n]*", "", decl, flags=re.S).strip()
            if not d:
                continue
            v = VIRTUAL_DECL.match(d)
            if v is None:
                if "(" in d:
                    raise SystemExit(f"prepare.py: {rel}: a member function that is not virtual: {d}")
                members.append(d + ";")
                continue
            if not v[2].startswith("~") and v[2] != "khdays_mwcc_complete_dtor":
                if v[2] in methods:
                    raise SystemExit(f"prepare.py: {rel}: two virtual {v[2]}s")
                methods[v[2]] = (slot, v[1] or "void", v[3].strip())
            slot += 1
        return (f"typedef struct {name} {{\n    unsigned int khdays_vtable;  /* the game's vtable (cpp_to_c) */\n"
                + "".join(f"    {d}\n" for d in members) + f"}} {name};")

    text = CLASS_DECL.sub(class_struct, text)
    if re.search(r"\bclass\b", re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)):
        raise SystemExit(f"prepare.py: {rel}: a class cpp_to_c does not take")
    # every struct its own typedef name, as C++ has it
    names = STRUCT_DEF.findall(text)
    if names:
        first = STRUCT_DEF.search(text).start()
        text = text[:first] + "".join(f"typedef struct {n} {n};\n" for n in names) + text[first:]
    for name, (slot, result, params) in methods.items():
        params = "" if params in ("", "void") else ", " + params
        call = re.compile(r"([A-Za-z_]\w*(?:\s*(?:->|\.)\s*[A-Za-z_]\w*)*)\s*->\s*" + name + r"\s*\(\s*(\))?")
        text = call.sub(lambda m: f"(({result} (__cdecl *)(void *{params}))(*(void ***)({m[1]}))[{slot}])"
                                  f"((void *)({m[1]})" + (")" if m[2] else ", "), text)
    while (m := EXTERN_C_BLOCK.search(text)) is not None:
        close = matching_brace(text, m.end() - 1)
        text = text[:m.start()] + text[m.end():close] + text[close + 1:]
    return text.replace('extern "C" ', "extern ")


def body_end(text: str, open_brace: int) -> int:
    """The index just past the brace that closes the one at `open_brace`
    (strings, characters and comments skipped)."""
    depth, i, n = 0, open_brace, len(text)
    while i < n:
        c = text[i]
        if c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            i = j + 1
            continue
        if text.startswith("/*", i):
            i = text.find("*/", i + 2) + 2 if text.find("*/", i + 2) >= 0 else n
            continue
        if text.startswith("//", i):
            i = text.find("\n", i) if text.find("\n", i) >= 0 else n
            continue
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    return n


def widen_narrow_returns(text: str) -> str:
    out, pos, widened = [], 0, []
    for m in NARROW_DEFINITION.finditer(text):
        if m.start() < pos:
            continue
        name, narrow = m[4], re.sub(r"\s+", " ", m[2])
        end = body_end(text, m.end() - 1)
        body = RETURN_VALUE.sub(lambda r: f"return{r[1]}({narrow})({r[2]});", text[m.end():end])
        out.append(text[pos:m.start()])
        out.append(f"{m[1]}int{m[3]}{name}{m[5]}{body}")
        pos = end
        widened.append(name)
    out.append(text[pos:])
    text = "".join(out)
    # the same functions' prototypes in this file say so too
    for name in widened:
        text = re.sub(rf"^(\s*(?:extern\s+|static\s+)?){NARROW_TYPE}(\s+{re.escape(name)}\s*\([^;{{}}]*\)\s*;)",
                      r"\1int\2", text, flags=re.MULTILINE)
    return text


# --- File-scope definitions (for duplicates and BSS) --------------------------

# A file-scope object definition header, on one line: `T name[..] =` or `T name;`.
DEFINITION_RE = re.compile(
    r"^(?!extern\b|static\b|typedef\b|return\b|union\b|enum\b|#)"
    r"([A-Za-z_][\w \t\*]*?)\b([A-Za-z_]\w*)((?:[ \t]*\[[^\]\n]*\])*)"
    r"((?:[ \t]*__attribute__[ \t]*\(\([^\n]*?\)\))?)[ \t]*(=|;)",
    re.MULTILINE)
KEYWORDS = {"else", "goto", "case", "do", "break", "continue", "return", "struct"}


def brace_depths(text: str) -> list[tuple[int, int]]:
    """(index, depth from there on) at every change of brace depth, outside
    comments, strings and character literals."""
    changes = [(0, 0)]
    depth = 0
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            i = j + 1
            continue
        if text.startswith("/*", i):
            end = text.find("*/", i + 2)
            i = n if end < 0 else end + 2
            continue
        if text.startswith("//", i):
            end = text.find("\n", i)
            i = n if end < 0 else end
            continue
        if c == "{" or c == "}":
            depth += 1 if c == "{" else -1
            changes.append((i + 1, depth))
        i += 1
    return changes


def definitions(text: str) -> list[re.Match]:
    """File-scope object definitions (a declaration at column 0 inside a
    function body is a local, not one of these)."""
    changes = brace_depths(text)
    starts = [i for i, _ in changes]
    out = []
    for m in DEFINITION_RE.finditer(text):
        head = m[1].split()
        if not head or head[-1] in KEYWORDS or m[2] in KEYWORDS:
            continue
        if changes[bisect.bisect_right(starts, m.start()) - 1][1] != 0:
            continue
        out.append(m)
    return out


def statement_end(text: str, start: int) -> int:
    """Index just past the `;` that ends the declaration starting at `start`,
    skipping braces, strings and comments inside an initializer."""
    depth = 0
    i = start
    n = len(text)
    while i < n:
        c = text[i]
        if c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            i = j + 1
            continue
        if text.startswith("/*", i):
            i = text.find("*/", i + 2) + 2
            continue
        if text.startswith("//", i):
            i = text.find("\n", i)
            continue
        if c in "{(":
            depth += 1
        elif c in "})":
            depth -= 1
        elif c == ";" and depth == 0:
            return i + 1
        i += 1
    return n


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", text)


def hal_definitions() -> set[str]:
    """What native/hal defines: functions, data and KHDAYS_HAL_TODO stubs."""
    names: set[str] = set()
    for source in (ROOT / "native" / "hal").glob("*.c"):
        text = strip_comments(source.read_text(encoding="utf-8"))
        names.update(re.findall(r"KHDAYS_HAL_TODO\((\w+)\)", text))
        names.update(re.findall(r"^(?!extern\b|static\b|typedef\b|#)[A-Za-z_][\w \t\*]*?\b([A-Za-z_]\w*)"
                                r"[ \t]*[\(\[=;]", text, re.MULTILINE))
    return names


def function_keys(modules: list[Module]) -> dict[str, str]:
    """Each function's key, `module@address`: what native/abi and the manual
    fixes name functions by, as the decomp renames them."""
    return {n: f"{m.name}@{a:08x}" for m in modules for n, k, a in m.symbols if k == "function"}


def module_id(name: str) -> int:
    """The module number KHDAYS_DATA_INIT records: an overlay's id, or -1/-2/-3
    for the static module, ITCM and DTCM (their data is placed at start-up)."""
    if name.startswith("ov"):
        return int(name[2:])
    return {"main": -1, "itcm": -2, "dtcm": -3}[name]


def decomp_absolute_symbols() -> dict[str, int]:
    """The ABSOLUTE_SYMBOLS table of the decomp's tools/configure.py."""
    text = (DECOMP / "tools" / "configure.py").read_text(encoding="utf-8")
    block = text[text.index("ABSOLUTE_SYMBOLS = {"):]
    block = block[:block.index("}")]
    return {n: int(v, 16) for n, v in re.findall(r'"(\w+)":\s*0x([0-9A-Fa-f]+)', block)}


RELOC_OVERLAYS_RE = re.compile(
    r"^from:0x[0-9a-fA-F]+\s+kind:\w+\s+to:0x([0-9a-fA-F]+)\s+module:overlays\(([\d,\s]+)\)")


def overlay_dispatch(modules: list[Module], texts: dict[str, str], module_of: dict[str, str]
                     ) -> tuple[dict[int, list[tuple[int, str]]], int]:
    """Routes references to an address several overlays' functions share
    through a thunk per address (see main). Returns the thunks (address ->
    [(overlay, function)]) and how many files were rerouted."""
    function_at: dict[tuple[int, int], str] = {}
    for m in modules:
        mo = re.fullmatch(r"ov(\d+)", m.name)
        if mo:
            for n, k, a in m.symbols:
                if k == "function":
                    function_at[(int(mo[1]), a)] = n
    thunks: dict[int, list[tuple[int, str]]] = {}
    reroute: dict[str, dict[str, str]] = collections.defaultdict(dict)  # module -> name -> thunk
    for m in modules:
        relocs = m.config / "relocs.txt"
        if not relocs.exists():
            continue
        for line in relocs.read_text(encoding="utf-8").splitlines():
            r = RELOC_OVERLAYS_RE.match(line)
            if not r:
                continue
            target = int(r[1], 16)
            candidates = [(o, function_at[(o, target)]) for o in
                          (int(x) for x in r[2].replace(" ", "").split(",")) if (o, target) in function_at]
            if len(candidates) < 2:
                continue  # data, or a function in only one of them
            thunks[target] = candidates
            for _, name in candidates:
                reroute[m.name][name] = f"khdays_ovcall_{target:08x}"
    files = 0
    for rel, text in texts.items():
        names = reroute.get(module_of[rel])
        if not names:
            continue
        used = [n for n in names if re.search(rf"\b{re.escape(n)}\b", text) and
                # not the file defining it (a definition starts at column 0)
                not re.search(rf"^[A-Za-z_][\w \t\*]*\b{re.escape(n)}\s*\([^;{{}}]*\)\s*\{{", text,
                              re.MULTILINE)]
        if used:
            texts[rel] = "".join(f"#define {n} {names[n]}  /* the overlay loaded there (prepare.py) */\n"
                                 for n in sorted(used)) + text
            files += 1
    return thunks, files


# Two punctuators that one token would be, written together (C11 6.4.6).
PASTED_PAIRS = {"--", "++", "-=", "+=", "->", "&&", "||", "<<", ">>", "==", "!=", "<=", ">=",
                "*=", "/=", "%=", "&=", "|=", "^=", "##", "/*", "//", ".."}
PUNCTUATORS = "+-*/%&|^!<>=.#"
MACRO_DEFINE = re.compile(r"^[ \t]*#[ \t]*define[ \t]+(\w+)(\()?((?:[^\n\\]|\\[\s\S])*)$", re.M)
CODE_SKIP = re.compile(r'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'|/\*.*?\*/|//[^\n]*', re.S)


def punctuator_macros(texts) -> tuple[dict[str, str], dict[str, str]]:
    """The macros whose expansion starts with a punctuator (any macro) and
    those whose expansion ends with one (object-like): name -> that
    punctuator."""
    starts: dict[str, str] = {}
    ends: dict[str, str] = {}
    for text in texts:
        for m in MACRO_DEFINE.finditer(text):
            rest = m[3]
            if m[2]:  # function-like: the body follows the parameters
                close = rest.find(")")
                if close < 0:
                    continue
                rest = rest[close + 1:]
            body = re.sub(r"/\*.*?\*/|//.*$", "", rest.replace("\\\n", " ")).strip()
            if body and body[0] in PUNCTUATORS:
                starts.setdefault(m[1], body[0])
            if body and body[-1] in PUNCTUATORS and not m[2]:
                ends.setdefault(m[1], body[-1])
    return starts, ends


def pasted_sites(text: str, starts: dict[str, str], ends: dict[str, str]) -> list[int]:
    """Where MSVC's preprocessed output (/E) would write two punctuators
    together that the compiler keeps apart -- a punctuator right before a
    macro whose expansion starts with one (`-EDGE_TILT`, EDGE_TILT -0xb33,
    comes out `--0xb33`), or after one ending with one: the offsets to put a
    space at, outside comments and literals."""
    skip = bytearray(len(text))
    for m in CODE_SKIP.finditer(text):
        skip[m.start():m.end()] = b"\1" * (m.end() - m.start())
    sites = []
    for m in re.finditer(r"([" + re.escape(PUNCTUATORS) + r"])([A-Za-z_]\w*)", text):
        if not skip[m.start()] and m[1] + starts.get(m[2], " ") in PASTED_PAIRS:
            sites.append(m.start(2))
    for m in re.finditer(r"\b([A-Za-z_]\w*)([" + re.escape(PUNCTUATORS) + r"])", text):
        if not skip[m.start(2)] and ends.get(m[1], " ") + m[2] in PASTED_PAIRS:
            sites.append(m.start(2))
    return sorted(set(sites))


def separate_pasted(texts: dict[str, str]) -> int:
    """The sites pasted_sites finds in the compiled sources, separated; one in
    a header (which this script does not patch) stops it, naming it."""
    headers = [p for d in (DECOMP / "include", DECOMP / "src", DECOMP / "libs", ROOT / "native" / "compat",
                           ROOT / "native" / "hal", ROOT / "native" / "runtime")
               for p in d.rglob("*.h")]
    header_texts = {p: p.read_text(encoding="utf-8", errors="replace") for p in headers}
    starts, ends = punctuator_macros(list(header_texts.values()) + list(texts.values()))
    in_headers = [f"{p.relative_to(ROOT)}: offset {at}" for p, t in header_texts.items()
                  for at in pasted_sites(t, starts, ends)]
    if in_headers:
        raise SystemExit("references: tokens MSVC's preprocessed output would paste, in headers:\n" +
                         "\n".join(in_headers[:20]))
    count = 0
    for rel, text in texts.items():
        sites = pasted_sites(text, starts, ends)
        for at in reversed(sites):
            text = text[:at] + " " + text[at:]
        if sites:
            texts[rel] = text
            count += len(sites)
    return count


def rewrite_game(out: Path, sources: list[tuple[str, str]], absolute: dict[str, int],
                 functions: dict[str, int], dispatched: dict[str, int]) -> tuple[list[str], int]:
    """The game's C (`sources`: (path compiled, original relative path)) and
    the HAL rewritten into out/rewritten (native/tools/references)."""
    from references.build import Program, rewrite_all
    jobs = [(path, str(out / "rewritten" / rel).replace("\\", "/")) for path, rel in sources]
    jobs += [(str(p).replace("\\", "/"), str(out / "rewritten" / "native_hal" / p.name).replace("\\", "/"))
             for p in sorted((ROOT / "native" / "hal").glob("*.c"))]
    thunks = {n for n in functions if n.startswith("khdays_ovcall_")}
    outputs, size = rewrite_all(jobs, Program(absolute=dict(absolute), functions=functions, game_functions=thunks,
                                              dispatched=dispatched), out)
    return outputs, size


def cmake_list(name: str, paths: list[str]) -> str:
    return f"set({name}\n" + "".join(f'    "{p}"\n' for p in paths) + ")\n"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(ROOT / "build" / "native" / "gen-x64"))
    args = ap.parse_args()
    out = Path(args.out).resolve()  # sources.cmake names its files absolutely
    patched_dir = out / "patched"
    patched_dir.mkdir(parents=True, exist_ok=True)

    modules = load_modules()
    compiled: list[tuple[Path, str]] = []   # (source, original relative path)
    hal_files: list[tuple[str, str]] = []  # (module, relative path)
    texts: dict[str, str] = {}
    module_of: dict[str, str] = {}
    # The manual edits by the file of the function they name (one function per
    # file, named after it).
    key_name = {k: n for n, k in function_keys(modules).items()}
    for key in list(HOOKS) + list(ABI_FIXES):
        if key not in key_name:
            raise SystemExit(f"no function at {key} (prepare.py HOOKS / ABI_FIXES)")
    for rel in FILE_HOOKS:
        if not (DECOMP / rel).exists():
            raise SystemExit(f"no file {rel} (prepare.py FILE_HOOKS)")
    hooks = {key_name[k]: v for k, v in HOOKS.items()}
    abi_fixes = {key_name[k]: v for k, v in ABI_FIXES.items()}
    register_tables = registers.header_tables(
        (DECOMP / "include" / "nitro" / "hw.h").read_text(encoding="utf-8"),
        (DECOMP / "include" / "nitro" / "gx.h").read_text(encoding="utf-8"))
    registers_left: list[str] = []
    as_c: set[str] = set()  # the C++ sources turned into C
    ui_hooked: set[str] = set()  # the files UI_HOOKS changed
    for m in modules:
        for rel in m.files:
            src = DECOMP / rel
            if "asm_stubs" in rel or src.suffix == ".s" or not src.exists() or m.name in REPLACED_MODULES:
                hal_files.append((m.name, rel))
                continue
            text = src.read_text(encoding="utf-8", errors="replace")
            # Anything still carrying ARM assembly after the transforms is
            # hardware-level code the HAL replaces.
            new = transform(text)
            file_hooks = hooks.get(Path(rel).stem, []) + FILE_HOOKS.get(rel, [])
            for ui_name, ui_part, ui_port, ui_kind in UI_HOOKS.get(rel, []):
                new = ui_hook(new, rel, ui_name, ui_part, ui_port, ui_kind)
                ui_hooked.add(rel)
            if file_hooks:
                for before, after in file_hooks:
                    if before not in new:
                        raise SystemExit(f"hook text not found in {rel} ({Path(rel).stem}): {before}")
                    new = new.replace(before, after)
                new = "extern void khdays_data_init(int module);\n" + new
            # the "am I the host" checks through the port (ROLE_QUERY)
            roles = 0
            if role_scoped(rel):
                new, roles = ROLE_QUERY.subn("khdays_mp_role()", new)
                new, more = ROLE_QUERY_VAR.subn(r"\1khdays_mp_role()\2", new)
                roles += more
            if roles:
                new = "extern int khdays_mp_role(void);\n" + new
            # after the hooks, which give some of these stores to the geometry themselves
            new = GEOMETRY_STORE.sub(
                lambda m: f"khdays_io_store(0x04000{m[1]}u, 4, (unsigned int)({m[2].strip()}));", new)
            # every other register access reaches the port without the trap's
            # fault (native/tools/registers.py)
            new = registers.rewrite(new, rel, register_tables, registers_left)
            if re.search(r"\basm\b|__asm\b", strip_comments(new)):
                hal_files.append((m.name, rel))
                continue
            if rel.endswith(".cpp"):
                new = cpp_to_c(new, rel)  # rewritten as C (T005a)
                as_c.add(rel)
            texts[rel] = new
            module_of[rel] = m.name
            compiled.append((src, rel))

    missing_ui = sorted(set(UI_HOOKS) - ui_hooked)
    if missing_ui:
        raise SystemExit("UI hooks on files the build does not compile: " + ", ".join(missing_ui))
    # Data sources no delinks.txt lists although they are the only definition of
    # a DS data symbol (the decomp's own build then takes those bytes from the
    # ROM, which natively would leave ARM addresses in function-pointer tables).
    # Each joins the module that owns its symbol.
    listed = {rel for m in modules for rel in m.files}
    provided = {d[2] for text in texts.values() for d in definitions(text)}
    owner_module = {n: m.name for m in modules for n, k, _ in m.symbols if k in ("data", "bss")}
    orphans = []
    for path in sorted((DECOMP / "src").rglob("data/*.c")):
        rel = path.relative_to(DECOMP).as_posix()
        if rel in listed:
            continue
        text = transform(path.read_text(encoding="utf-8", errors="replace"))
        names = [n for n in re.findall(r"(?:^|\})[ \t\w\*]*?\b(data_\w+)\s*(?:\[[^\]\n]*\])*\s*=",
                                       text, re.MULTILINE)
                 if n in owner_module and n not in provided]
        if names:
            texts[rel] = text
            module_of[rel] = owner_module[names[0]]
            compiled.append((path, rel))
            orphans.append(f"{rel}\t{' '.join(names)}")
    (out / "orphan_data.txt").write_text("".join(o + "\n" for o in orphans), encoding="utf-8")
    (out / "registers_left.txt").write_text("".join(f"{line}\n" for line in registers_left), encoding="utf-8")

    # Globals defined (with an initializer) in several files: the decomp split
    # one original translation unit into a file per function, repeating its
    # file-scope variables. Keep one definition -- in a data/ file when there
    # is one -- and make the rest `extern`.
    owners = collections.defaultdict(list)
    for rel, text in texts.items():
        for d in definitions(text):
            owners[d[2]].append(rel)
    duplicates = {n: sorted(set(f)) for n, f in owners.items() if len(set(f)) > 1}
    for name, files in duplicates.items():
        keep = next((f for f in files if "/data/" in f), files[0])
        for f in files:
            if f == keep:
                continue
            text = texts[f]
            for d in definitions(text):
                if d[2] != name:
                    continue
                end = statement_end(text, d.start())
                texts[f] = text[:d.start()] + f"extern {d[1]}{d[2]}{d[3]};" + text[end:]
                break
    (out / "duplicates.txt").write_text(
        "".join(f"{n}\t{' '.join(f)}\n" for n, f in sorted(duplicates.items())),
        encoding="utf-8")

    # Calls that lean on the ARM registers (native/tools/abi_repair.py): pass
    # and return what the ROM's code does, from native/abi/ghidra_abi.txt.
    fix_files = {Path(rel).stem: rel for rel in texts if Path(rel).stem in abi_fixes}
    for name, fixes in abi_fixes.items():
        rel = fix_files.get(name)
        if rel is None:
            raise SystemExit(f"no source defines {name} (prepare.py ABI_FIXES)")
        for before, after in fixes:
            if before not in texts[rel]:
                raise SystemExit(f"ABI fix text not found in {rel}: {before[:60]}")
            texts[rel] = texts[rel].replace(before, after)
    # Tail calls (native/abi/tail_calls.txt): the ROM's `ldr r12,=g; bx r12`
    # hands g the argument registers as the caller set them; a C body
    # `return g();` passes none. Such a body forwards four.
    tail_report = []
    tail_targets: dict[str, str] = {}
    for line in (ROOT / "native" / "abi" / "tail_calls.txt").read_text(encoding="utf-8").splitlines():
        key = line.strip()
        if not key or key.startswith("#"):
            continue
        name = key_name.get(key)
        rel = next((r for r in texts if Path(r).stem == name), None) if name else None
        if rel is None:
            tail_report.append(f"{key} {name}: no source")
            continue
        texts[rel], note = forward_tail_call(texts[rel], name)
        tail_report.append(f"{key} {name}: {note}")
        if note.startswith("forwards four words to "):
            tail_targets[name] = note.rsplit(" ", 1)[1]
    (out / "tail_calls.txt").write_text("\n".join(tail_report) + "\n", encoding="utf-8")

    names = {k: n for n, k in function_keys(modules).items()}
    findings = abi_repair.load_findings(ROOT / "native" / "abi" / "ghidra_abi.txt", names)
    # What the ROM's code inside a replaced module needs is moot: none of it
    # is built (REPLACED_MODULES).
    replaced = {n for m in modules if m.name in REPLACED_MODULES for n, _, _ in m.symbols}
    findings.calls = {k: v for k, v in findings.calls.items() if k[0] not in replaced}
    findings.returns = {k: v for k, v in findings.returns.items() if k not in replaced}
    functions = {m.name: {a: n for n, k, a in m.symbols if k == "function"} for m in modules}
    repairer = abi_repair.Repairer(texts, module_of, functions, findings)
    repairer.fixed = set(abi_fixes)
    # What each call needs is what its callee takes: the argument words of its
    # definition, more where the callee's own registers pass on to a call that
    # needs them (abi_repair's extensions), and for a tail call (which hands
    # on four words) what its target needs. A finding past that -- a register
    # the scan was asked about when the C said otherwise -- is dropped, as are
    # the registers the ROM shows a callee never reads (ABI_NOT_READ).
    def words(name: str) -> int:
        rel = repairer.def_file.get(name)
        d = abi_repair.find_definition(texts[rel], repairer.mask(rel), name) if rel else None
        if d is None:
            return 8  # no C definition to go by: keep every finding
        params = abi_repair.param_list(texts[rel], d)
        if any(p.strip() == "..." for p in params):
            return 8
        return sum(abi_repair.param_words(p) for p in params)
    involved = {f for pair in findings.calls for f in pair} | set(tail_targets) | set(tail_targets.values())
    need = {f: 0 if f in tail_targets else words(f) for f in involved}
    changed = True
    while changed:
        changed = False
        for wrapper, target in tail_targets.items():
            if need[target] > need[wrapper]:
                need[wrapper] = need[target]
                changed = True
        for (caller, callee), sites in findings.calls.items():
            for regs in sites.values():
                for k, alternatives in regs.items():
                    if k >= need[callee]:
                        continue
                    for s in alternatives:
                        if s.kind == "entry" and s.register + 1 > need[caller]:
                            need[caller] = s.register + 1
                            changed = True
    not_read = {names.get(key, key): set(registers) for key, registers in ABI_NOT_READ.items()}
    # the function a call to a shared address really reaches (SHARED_CALL_TARGETS)
    reaches: dict[tuple[str, str], str] = {}
    for caller_key, targets in SHARED_CALL_TARGETS.items():
        for address, module in targets.items():
            reached = functions.get(module, {}).get(address)
            for m in modules:
                named = functions[m.name].get(address)
                if named and reached and names.get(caller_key) and m.name != module:
                    reaches[(names[caller_key], named)] = reached
    # An empty function (`bx lr`, written to return its first argument, as
    # the decomp does and EMPTY_FUNCTION did) takes r0 only to give it back: a
    # call whose value goes nowhere needs nothing.
    def returns_first_argument(name: str) -> bool:
        rel = repairer.def_file.get(name)
        d = abi_repair.find_definition(texts[rel], repairer.mask(rel), name) if rel else None
        if d is None:
            return False
        params = abi_repair.param_list(texts[rel], d)
        first = abi_repair.param_name(params[0]) if len(params) == 1 else ""
        body = abi_repair.strip_comments_code(texts[rel][d.body[0] + 1:d.body[1]], repairer.mask(rel)[d.body[0] + 1:d.body[1]])
        return bool(first) and re.fullmatch(rf"\s*return\s+(?:\([^()]*\)\s*)?{re.escape(first)}\s*;\s*", body) is not None
    for caller, callee in list(findings.calls):
        if not returns_first_argument(callee):
            continue
        crel = repairer.def_file.get(caller)
        d = abi_repair.find_definition(texts[crel], repairer.mask(crel), caller) if crel else None
        sites = abi_repair.call_sites(texts[crel], repairer.mask(crel), callee, d.body) if d else []
        if sites and all(abi_repair.value_unused(texts[crel], repairer.mask(crel), s, c) for s, c in sites):
            del findings.calls[(caller, callee)]
    for pair in list(findings.calls):
        sites = findings.calls[pair]
        limit = words(reaches[pair]) if pair in reaches else need[pair[1]]
        for site in list(sites):
            for k in list(sites[site]):
                if k >= limit or k in not_read.get(pair[1], ()):
                    del sites[site][k]
            if not sites[site]:
                del sites[site]
        if not sites:
            del findings.calls[pair]
    # A value no C reads needs nothing: every call of the function is a
    # statement of its own, in a file that declares it void, the whole
    # argument of a call whose callee's definition takes fewer words than
    # that argument's position (Ov002_DismissIfState4 hands
    # Ov002_FillMapRows's to Ov002_HudTeardownToState5(void)), or returned
    # by a function no pointer reaches whose own value no C reads.
    defined_in: dict[str, list[str]] = collections.defaultdict(list)
    for fname, rel in repairer.def_file.items():
        defined_in[rel].append(fname)

    def address_taken(name: str) -> bool:
        for rel, text in texts.items():
            if name not in text:
                continue
            mask = repairer.mask(rel)
            for m in re.finditer(rf"\b{re.escape(name)}\b(?!\s*\()", text):
                if mask[m.start()]:
                    return True
        return False

    def returned_by(rel: str, text: str, mask: bytearray, start: int, close: int) -> str | None:
        """The function returning the call's value: `return f(...);`, or
        `v = f(...);` into a local it only returns."""
        if not re.match(r"\s*;", text[close + 1:]):
            return None
        before = text[:start]
        for fname in defined_in[rel]:
            d = abi_repair.find_definition(text, mask, fname)
            if not d or not d.body[0] < start < d.body[1]:
                continue
            if re.search(r"\breturn\s*(?:\([\w\s\*]+\)\s*)*$", before):
                return fname
            v = re.search(r"\b([A-Za-z_]\w*)\s*=\s*$", before)
            if not v or not mask[v.start()]:
                return None
            body = text[d.body[0]:d.body[1]]
            for use in re.finditer(rf"\b{v[1]}\b", body):
                at = d.body[0] + use.start()
                if not mask[at] or at == v.start(1):
                    continue
                if re.search(r"\breturn\s*$", body[:use.start()]) and re.match(r"\s*;", body[use.end():]):
                    continue
                if re.search(r"\b(?:int|u32|s32|void\s*\*)\s*$", body[:use.start()]) and \
                        re.match(r"\s*;", body[use.end():]):
                    continue  # its declaration
                return None
            return fname
        return None

    def value_read(name: str, seen: tuple[str, ...] = ()) -> bool:
        for rel, text in texts.items():
            if name not in text or rel == repairer.def_file.get(name):
                continue
            mask = repairer.mask(rel)
            decls = abi_repair.declarations(text, mask, name)
            if decls and all(abi_repair.VOID_BEFORE_NAME.search(text[s:o]) for s, o, _ in decls):
                continue
            prototypes = {o for _, o, _ in decls}
            for s, c in abi_repair.call_sites(text, mask, name, (0, len(text))):
                if text.find("(", s) in prototypes or abi_repair.value_unused(text, mask, s, c):
                    continue
                argument = abi_repair.argument_of(text, mask, s, c)
                if argument and argument[0] in repairer.def_file and words(argument[0]) <= argument[1]:
                    continue
                through = returned_by(rel, text, mask, s, c)
                if through and through not in seen and through != name and \
                        not address_taken(through) and not value_read(through, seen + (name,)):
                    continue
                return True
        return False
    values_unread = 0
    not_read_values = {names.get(key, key) for key in VALUE_NOT_READ}
    for name in sorted(findings.returns):
        if name in not_read_values or not value_read(name):
            values_unread += 1
            continue
        why = repairer.plan_return(name)
        if why == "return type is not plain void":
            continue  # a manual fix (ABI_FIXES) gave it its value
        if why:
            repairer.plan.gaps.append(f"{name}'s value: {why}")
    equivalent = 0
    for (caller, callee), sites in sorted(findings.calls.items()):
        ks = sorted({k for regs in sites.values() for k in regs})
        why = repairer.plan_call(caller, callee, ks[0], ks[-1] + 1)
        if why in ("struct-by-value", "resolved"):
            equivalent += 1
        elif why:
            repairer.gap_call(caller, callee, why, ks[-1] + 1)
    where = {}
    for m in modules:
        space = f"arm9_{m.name}" if m.name.startswith("ov") else "-"
        for n, k, a in m.symbols:
            if k == "function":
                where[n] = (space, a)
    new_texts = repairer.result()
    (out / "abi_next_targets.txt").write_text(
        "".join(t + "\n" for t in repairer.next_targets(where)), encoding="utf-8")
    texts.update(new_texts)
    # Locals read unset only to add `(x - x)` start at 0.
    initialized_texts, initialized = abi_repair.init_self_subtracted(texts)
    texts.update(initialized_texts)
    # Parameters used as locals that a call does not pass: natively the
    # definition would write over its caller's frame.
    padded_texts, padded = abi_repair.pad_short_calls(texts)
    texts.update(padded_texts)
    (out / "abi_padded.txt").write_text("\n".join(padded) + "\n", encoding="utf-8")
    # Parameters declared narrower than defined: the calls pass them extended.
    widened_texts, widened = abi_repair.widen_narrow_params(texts)
    texts.update(widened_texts)
    (out / "abi_narrow_params.txt").write_text("\n".join(widened) + "\n", encoding="utf-8")
    # Results declared wider than defined: the calls extend them themselves.
    narrowed_texts, narrowed = abi_repair.narrow_return_declarations(texts)
    texts.update(narrowed_texts)
    (out / "abi_narrow_returns.txt").write_text("\n".join(narrowed) + "\n", encoding="utf-8")
    # Functions reached through pointers, whose short calls nothing pads,
    # keep their parameters' slots to themselves: compiled unoptimized.
    unoptimized_texts, callbacks = abi_repair.unoptimize_callbacks(texts)
    texts.update(unoptimized_texts)
    (out / "abi_unoptimized.txt").write_text("\n".join(callbacks) + "\n", encoding="utf-8")
    (out / "abi_repairs.txt").write_text("\n".join(repairer.plan.applied) + "\n", encoding="utf-8")
    (out / "abi_gaps.txt").write_text("\n".join(repairer.plan.gaps) + "\n", encoding="utf-8")
    abi_summary = (f"ABI: {len(repairer.plan.applied)} repaired, {equivalent} equivalent as the C passes them, "
                   f"{values_unread} values no C reads, "
                   f"{len(repairer.plan.gaps)} gaps (abi_gaps.txt), {len(padded)} short calls padded "
                   f"(abi_padded.txt), {len(widened)} narrow parameters widened (abi_narrow_params.txt), "
                   f"{initialized} files' `(x - x)` locals set to 0, {len(callbacks)} functions reached "
                   f"through pointers in {len(unoptimized_texts)} unoptimized files (abi_unoptimized.txt)")

    # References to an address several overlays share (the decomp's relocs
    # name them all: module:overlays(a,b)). On the DS the one loaded there at
    # the time runs; the decomp's C names one of them, not always that one
    # (ov012 opens its movie through "func_ov008_020846c0", ov008's shop list,
    # where the ROM reaches ov024's MobiClip open at the same address). Natively
    # every such function reference in the referring module goes through a
    # thunk that jumps to the loaded overlay's function (runtime/core/overlays.c).
    thunks, dispatched = overlay_dispatch(modules, texts, module_of)

    # Game data at its DS address. Every object a C file defines that the DS
    # linker placed (symbols.txt) becomes an absolute symbol at that address,
    # so all code meets it where the DS had it -- including code that reaches
    # other memory relative to it. An initialised definition keeps its
    # initializer under a second name and registers it (KHDAYS_DATA_INIT); the
    # runtime copies it into place when its module is loaded, over the image
    # the DS itself loaded there, so pointers to functions in it are native.
    ds_data: dict[str, int] = {}
    for m in modules:
        for n, k, a in m.symbols:
            if k in ("data", "bss"):
                ds_data[n] = a
    converted: dict[str, int] = {}
    # C-defined globals the DS names nowhere: statics of a split unit (PHANTOMS).
    phantom_names = {n for n in owners if n not in ds_data and
                     not any(n in {s for s, _, _ in m.symbols} for m in modules)}
    for rel in list(texts):
        text = texts[rel]
        module = module_id(module_of[rel])
        extern = 'extern "C"' if rel.endswith(".cpp") and rel not in as_c else "extern"
        for d in reversed(definitions(text)):
            name = d[2]
            if name not in ds_data:
                if name in phantom_names:
                    text = text[:d.start()] + f"{extern} {d[1]}{name}{d[3]};" + \
                        text[statement_end(text, d.start()):]
                continue
            decl = f"{extern} {d[1]}{name}{d[3]};"
            if d[5] == "=":
                end = statement_end(text, d.start())
                text = (text[:d.start()] + decl + "\n" +
                        f"{d[1]}{name}__khdays_init{d[3]}{d[4]} =" + text[d.end():end] +
                        f"\nKHDAYS_DATA_INIT({name}, {module})" + text[end:])
            else:
                text = text[:d.start()] + decl + text[d.end():]
            converted[name] = ds_data[name]
        texts[rel] = text
    unconverted = sorted(n for n in owners if n in ds_data and n not in converted)
    (out / "unconverted_data.txt").write_text(
        "".join(f"{n}\t{' '.join(owners[n])}\n" for n in unconverted), encoding="utf-8")
    pasted = separate_pasted(texts)

    lines = []
    compiled_path: dict[str, str] = {}
    patched = 0
    for src, rel in compiled:
        text = texts[rel]
        original = src.read_text(encoding="utf-8", errors="replace")
        path = src
        if text != original:
            patched += 1
            path = patched_dir / (rel[:-4] + ".c" if rel in as_c else rel)
            path.parent.mkdir(parents=True, exist_ok=True)
            # Rewrite only on change, so the build does not recompile it.
            if not path.exists() or path.read_text(encoding="utf-8") != text:
                path.write_text(text, encoding="utf-8")
        lines.append(str(path).replace("\\", "/"))
        compiled_path[rel] = lines[-1]

    # The functions of the main module and the overlays at their DS addresses,
    # for a call through a DS address -- a pointer the ROM's own data holds,
    # such as an overlay's static initializers -- to run the native function
    # (runtime/core/window.c, runtime/core/overlays.c). The ITCM's
    # functions are listed with their sizes: the MobiClip decoder is copied
    # over the ITCM while a movie plays, so one runs natively only while its
    # bytes are still the game's (NitroSystem's G3D calls two through tables
    # in the ITCM that hold their DS addresses, at 0x01ffa1f8 and 0x01ffa598).
    # the names compat/decomp_defines.txt gives the game's functions natively
    # (main, the C runtime's, the system's): each listed under its native name
    renames = {m[1]: m[2] for m in re.finditer(r"^(\w+)=(\w+)\s*$",
                                               (ROOT / "native" / "compat" / "decomp_defines.txt").read_text(
                                                   encoding="utf-8"), re.M)}
    hal_names_all = hal_definitions()
    ds_functions: list[tuple[int, int, str]] = []
    for m in modules:
        if m.name == "dtcm":
            continue
        ds_module = -1 if m.name == "main" else -2 if m.name == "itcm" else int(m.name[2:])
        native: set[str] = set()
        for rel in m.files:
            text = texts.get(rel)
            if text is None:
                continue
            text = strip_comments(text)
            # a definition at a line's start or right after the previous
            # one's brace (`}int Ov022_StepGlideState(...)`), and one returning
            # a function pointer (`void (*Ov002_CaptureRootHeap(void))(void)`)
            for d in re.finditer(r"(?:^|\})[ \t]*(?!static\b|extern\b|typedef\b)[A-Za-z_][\w \t\*]*?"
                                 r"(?:\(\s*\*\s*([A-Za-z_]\w*)\s*\([^;{}]*\)\s*\)\s*\([^;{}]*\)|"
                                 r"\b([A-Za-z_]\w*)\s*\([^;{}]*\))\s*\{",
                                 text, re.MULTILINE):
                if rel.endswith(".cpp") and rel not in as_c and \
                        not re.search(r'extern\s*"C"\s*\{', text[:d.start()]):
                    continue  # C++ linkage
                native.add(d[1] or d[2])
        for n, k, a in m.symbols:
            # the HAL's too, of any module (the ITCM's OS_IrqHandler, which the
            # BIOS vector names by its DS address)
            if k == "function" and (n in native or n in hal_names_all):
                ds_functions.append((a, ds_module, n))
    ds_functions.sort()
    sizes = {n: sz for m in modules for n, sz in m.sizes.items()}
    ds_c = ["/* Generated by native/tools/prepare.py: each native function of the main module,",
            " * the ITCM and the overlays at its DS address, and its size (runtime/core/overlays.c). */",
            "typedef struct { unsigned address; int module; unsigned size; void (*native)(void); } KhdaysDsFunction;"]
    ds_c += [f"extern void {renames.get(n, n)}(void);" for n in sorted({n for _, _, n in ds_functions})]
    ds_c.append("const KhdaysDsFunction khdays_ds_functions[] = {")
    ds_c += [f"    {{0x{a:08x}, {mod}, 0x{sizes.get(n, 0):x}, {renames.get(n, n)}}}," for a, mod, n in ds_functions]
    ds_c += ["};", f"const unsigned khdays_ds_function_count = {len(ds_functions)};", ""]
    ds_path = out / "ds_functions.c"
    ds_text = "\n".join(ds_c)
    if not ds_path.exists() or ds_path.read_text(encoding="utf-8") != ds_text:
        ds_path.write_text(ds_text, encoding="utf-8", newline="\n")

    # Functions the HAL must provide: the symbols inside the assembly-only files.
    hal_syms = []
    for mod_name, rel in hal_files:
        stem = Path(rel).stem
        hal_syms.append(f"{mod_name}\t{stem}\t{rel}")
    (out / "hal_symbols.txt").write_text("\n".join(hal_syms) + "\n", encoding="utf-8")

    # Symbols the DS linker placed, emitted as ABSOLUTE symbols at their DS
    # addresses: the native process maps the DS memory at the same virtual
    # addresses, so BSS keeps its exact layout (overlays sharing an address
    # range alias each other, as on the DS), and literal addresses in the C
    # meet the same objects the symbols name.
    defined = set(owners) - set(converted) - phantom_names
    absolute: dict[str, int] = dict(converted)
    absolute.update(PHANTOMS)
    for m in modules:
        if ".bss" not in m.sections:
            continue
        start, end = m.sections[".bss"]
        for n, k, a in m.symbols:
            if start <= a < end and n not in defined:
                absolute[n] = a
    bss_count = len(absolute) - len(converted) - len(PHANTOMS)
    # The decomp's own linker-absolute symbols (tools/configure.py). A data_
    # entry that another, C-defined symbol shares the address of is an alias
    # of that table: point it there instead (the table's bytes live wherever
    # MSVC put the C definition, not at the DS address).
    by_addr = collections.defaultdict(list)
    for m in modules:
        for n, k, a in m.symbols:
            by_addr[a].append(n)
    alternates: dict[str, str] = {}
    for name, addr in decomp_absolute_symbols().items():
        target = next((n for n in by_addr.get(addr, []) if n != name and n in defined), None)
        if target is not None:
            alternates[name] = target
        else:
            absolute[name] = addr
    # What only an assembly file holds and native/hal does not implement is
    # the ROM's bytes at its DS address -- the module image the game loads
    # holds them: data is read there; code there has no native form, and a
    # call into it stops the game, naming it (runtime/core/window.c). rom_code.txt
    # lists what may be code: MobiClip's routines the game never reaches --
    # Ov024_MobiClip_AudioTransformDecode and func_ov024_02087318_unk, the
    # audio decoders of kinds 1 and 2 (every movie with sound is kind 3),
    # and func_ov024_02092e60_unk, the deblocking filter (the one call
    # reaching BlitFrame, from Ov024_MobiClip_FrameAlarm, passes mode 0) --
    # and a label inside func_0200302c, which the HAL has.
    # A Thumb function's address carries bit 0, as an ARM pointer to it does.
    hal_names = hal_definitions()
    by_name = {m.name: m for m in modules}
    rom_run: list[str] = []
    for mod_name, rel in hal_files:
        m = by_name[mod_name]
        for start, end in m.ranges.get(rel, []):
            for n, k, a in m.symbols:
                if start <= a < end and n not in hal_names and n not in absolute and n not in defined:
                    absolute[n] = a | (1 if n in m.thumb else 0)
                    if k == "function" or not rel.endswith(".s") or "_unk" in n:
                        rom_run.append(n)
    (out / "rom_code.txt").write_text("".join(f"{n}\n" for n in sorted(rom_run)), encoding="utf-8")
    # FS_OVERLAY_ID(n) is the ADDRESS of the linker symbol OVERLAY_n_ID.
    for d in sorted((CONFIG / "overlays").iterdir()):
        mo = re.fullmatch(r"ov(\d+)", d.name)
        if mo:
            absolute[f"OVERLAY_{int(mo[1])}_ID"] = int(mo[1])

    if alternates:
        raise SystemExit("references: second names of C-defined tables have no "
                         "reference yet: " + ", ".join(sorted(alternates)[:20]))
    # A function reference is its DS address where the runtime finds the
    # native function by it (ds_functions.c; an overlay's while it is
    # loaded there, an address overlays share through its thunk); one
    # with no native form there, the ROM's code, stops the game when
    # called. Every other function the build numbers.
    functions = {n: a for a, _, n in ds_functions}
    # the rewritten C names a renamed function by its native name
    functions.update({renames[n]: a for a, _, n in ds_functions if n in renames})
    functions.update({n: absolute[n] for n in rom_run if n in absolute})
    functions.update({f"khdays_ovcall_{target:08x}": target for target in thunks})
    c_sources = [(compiled_path[rel], rel[:-4] + ".c" if rel in as_c else rel) for _, rel in compiled
                 if not rel.endswith(".cpp") or rel in as_c]
    # The static initializers' table is the DS linker's .ctor, read where
    # the static module's image has it (runtime/core/start.c defines one only
    # for the build without references).
    placed = dict(absolute, ARM9_CTOR_START=by_name["main"].sections[".ctor"][0])
    # direct calls to an address several overlays share (their thunks) and
    # into the ROM's code reach it as a call through its DS address does
    dispatched_calls = {n: a for n, a in functions.items() if n.startswith("khdays_ovcall_")}
    dispatched_calls.update({n: absolute[n] for n in rom_run if n in absolute})
    rewritten, area = rewrite_game(out, c_sources, placed, functions, dispatched_calls)
    # the port's side calls the game's functions the host's way: none of
    # those taking their arguments in a block (references/emit.py)
    block = set((out / "references_block_abi.txt").read_text(encoding="utf-8").split())
    port_calls = []
    for source in sorted(p for d in ("runtime", "host", "gfx", "sound") for p in (ROOT / "native" / d).rglob("*.c*")):
        text = strip_comments(source.read_text(encoding="utf-8", errors="replace"))
        port_calls += [f"{source.name}: {n}" for n in sorted(block)
                       if re.search(rf"\b{re.escape(n)}\s*\(", text)]
    if port_calls:
        raise SystemExit("references: the port calls game functions that take their arguments in a block: " +
                         ", ".join(port_calls))
    missing = [n for n in PORT_DS_SYMBOLS if n not in ds_data]
    if missing:
        raise SystemExit(f"references: the port reads {', '.join(missing)}, which the decomp places nowhere")
    symbols_h = ("/* Generated by native/tools/prepare.py: the DS addresses of the\n"
                 " * game's objects the port's side reads (PORT_DS_SYMBOLS). */\n"
                 "#ifndef KHDAYS_DS_SYMBOLS_H\n#define KHDAYS_DS_SYMBOLS_H\n" +
                 "".join(f"#define KHDAYS_DS_{n} 0x{ds_data[n]:08x}u\n" for n in PORT_DS_SYMBOLS) + "#endif\n")
    target = out / "khdays_ds_symbols.h"
    if not target.exists() or target.read_text(encoding="utf-8") != symbols_h:
        target.write_text(symbols_h, encoding="utf-8")
    cmake = cmake_list("KHDAYS_REWRITTEN", rewritten)
    rewritten_summary = (f"; rewritten with references: {len(rewritten)} files ({pasted} pasted tokens "
                         f"separated), a data area of 0x{area:x} bytes")
    target = out / "sources.cmake"
    if not target.exists() or target.read_text(encoding="utf-8") != cmake:
        target.write_text(cmake, encoding="utf-8")

    print(f"{len(compiled)} sources ({patched} patched), "
          f"{len(hal_files)} assembly-only files for the HAL, "
          f"{len(orphans)} unlisted data sources taken in (orphan_data.txt), "
          f"{len(duplicates)} duplicated globals resolved; at DS addresses: "
          f"{len(converted)} C-defined data objects ({len(unconverted)} left in the image, "
          f"see unconverted_data.txt), {bss_count} other BSS symbols, "
          f"{len(PHANTOMS)} split-unit statics, "
          f"{len(absolute) - bss_count - len(converted) - len(PHANTOMS)} other absolute symbols; "
          f"{len(alternates)} aliases; {abi_summary}; "
          f"{len(thunks)} addresses shared by overlays' functions, reached through thunks from "
          f"{dispatched} files{rewritten_summary}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
