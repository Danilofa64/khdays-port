#!/usr/bin/env python3
"""A listing of the ARM7's sound driver, to read the native driver from.

  python native/tools/arm7_listing.py [--arm7 PATH] [--out PATH]

The driver is the first autoload segment of the game's ARM7 binary, the one
the native driver reads its tables from (native/sound/snd7_native.h: the same
offset, size, address and FNV-1a hash). It is disassembled with Capstone by
following the code from the entry points the port knew -- SND_Init and the
ARM7 routines the interpreted driver answered natively -- through every
branch, call and switch
(`cmp rN, #K; addls pc, pc, rN, lsl #2`, then a table of K + 1 branches); from the
code addresses its literal pools hold (odd: Thumb; even: ARM, when the word
there is an ARM instruction that always runs, condition 0xE -- a handler
that only jumps on has no prologue); and from every ARM function prologue
(`stmdb sp!, {..., lr}`) in the segment, since the driver hands its routines
to the OS and to the FIFO as pointers. Each function says how it was found. What no
path reaches is printed as data.

The listing is the game's code: it goes to build/native/arm7/driver.lst by
default (git ignores build/) and the tool refuses to write it anywhere in the
repository but build/ or saves/.

Needs the player's extracted data (data/extracted/<id>/system/arm7.bin) and
Capstone (pip install capstone).
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

try:
    import capstone
except ImportError:
    sys.exit("arm7_listing: needs Capstone (pip install capstone)")

ROOT = Path(__file__).resolve().parents[2]

# The segment, as native/sound/snd7_native.h defines it.
SEGMENT_OFFSET = 0x01E8
SEGMENT_ADDRESS = 0x037F8000
SEGMENT_SIZE = 0xF4B0
SEGMENT_FNV1A = 0x9F4BF423

# What the port knew in the segment (the interpreted driver it had until):
# the driver's entry and the ARM7 routines it answered natively -- names only: the walk
# starts at SND_Init, which the port calls in ARM state, and each routine
# takes its state from the call that reaches it.
KNOWN = {
    0x037FEEF8: "SND_Init",
    0x037FC054: "OS_CreateThread",
    0x037FC290: "OS_SleepThread",
    0x037FC2E4: "OS_WakeupThread",
    0x037FC36C: "OS_WakeupThreadDirect",
    0x037FD21C: "OS_GetTick",
    0x037FD4DC: "OS_SetAlarm",
    0x037FD54C: "OS_SetPeriodicAlarm",
    0x037FD5C0: "OS_CancelAlarm",
    0x037FDE70: "OS_Panic",
    0x037FE39C: "PXI_SetFifoRecvCallback",
    0x037FE410: "PXI_SendWordByFifo",
}


def fnv1a(data: bytes) -> int:
    h = 0x811C9DC5
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def find_arm7() -> Path:
    found = sorted((ROOT / "data" / "extracted").glob("*/system/arm7.bin"))
    if not found:
        sys.exit("arm7_listing: no data/extracted/<id>/system/arm7.bin (extract the game first)")
    return found[0]


def in_segment(address: int) -> bool:
    return SEGMENT_ADDRESS <= address < SEGMENT_ADDRESS + SEGMENT_SIZE


class Listing:
    def __init__(self, code: bytes):
        self.code = code
        self.arm = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM)
        self.thumb = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB)
        for md in (self.arm, self.thumb):
            md.detail = True
        self.insns: dict[int, tuple[bool, object]] = {}  # address -> (thumb, insn)
        self.previous = None                             # the instruction decoded before
        self.functions: dict[int, bool] = {}            # start -> thumb
        self.data: set[int] = set()                     # literal pool words
        self.from_literal: set[int] = set()
        self.from_prologue: set[int] = set()

    def word(self, address: int) -> int:
        return struct.unpack_from("<I", self.code, address - SEGMENT_ADDRESS)[0]

    def decode(self, address: int, thumb: bool):
        md = self.thumb if thumb else self.arm
        offset = address - SEGMENT_ADDRESS
        for insn in md.disasm(self.code[offset:offset + 4], address, count=1):
            return insn
        return None

    def arm_prologue(self, address: int) -> bool:
        """`stmdb sp!, {..., lr}`: an ARM function saving its return address."""
        return (self.word(address) & 0xFFFF4000) == 0xE92D4000

    def add_function(self, address: int, thumb: bool, work: list):
        if in_segment(address) and address not in self.functions:
            self.functions[address] = thumb
            work.append((address, thumb))

    def walk(self, seeds: dict[int, bool]):
        work: list[tuple[int, bool]] = []
        for address, thumb in seeds.items():
            self.add_function(address, thumb, work)
        for address in range(SEGMENT_ADDRESS, SEGMENT_ADDRESS + SEGMENT_SIZE, 4):
            if self.arm_prologue(address) and address not in self.functions:
                self.from_prologue.add(address)
                self.add_function(address, False, work)
        while work:
            address, thumb = work.pop()
            while in_segment(address) and address not in self.insns and address not in self.data:
                insn = self.decode(address, thumb)
                if insn is None:
                    break
                self.insns[address] = (thumb, insn)
                address = self.follow(insn, thumb, work)
                self.previous = insn
                if address is None:
                    break

    def follow(self, insn, thumb: bool, work: list):
        """Queues what the instruction reaches; returns the next address, or None."""
        mnemonic = insn.mnemonic
        ops = insn.operands
        after = insn.address + insn.size
        conditional = insn.cc not in (capstone.arm.ARM_CC_AL, capstone.arm.ARM_CC_INVALID)
        # literal loads: ldr rX, [pc, #imm]
        if mnemonic.startswith("ldr") and len(ops) == 2 and ops[1].type == capstone.arm.ARM_OP_MEM \
                and ops[1].mem.base == capstone.arm.ARM_REG_PC:
            pc = (insn.address + (4 if thumb else 8)) & ~3
            literal = pc + ops[1].mem.disp
            if in_segment(literal):
                self.data.add(literal)
                value = self.word(literal)
                if in_segment(value & ~1) and (value & 1):
                    self.from_literal.add(value & ~1)
                    self.add_function(value & ~1, True, work)
                elif in_segment(value) and value % 4 == 0 and (self.word(value) >> 28) == 0xE                         and self.decode(value, False) is not None:
                    self.from_literal.add(value)
                    self.add_function(value, False, work)
            if ops[0].reg == capstone.arm.ARM_REG_PC:
                return None
        arm = capstone.arm
        # a switch: `cmp rN, #K` right before `addls pc, pc, rN, lsl #2` -- a
        # table of K + 1 branches after the default branch that follows
        prev = self.previous
        if not thumb and insn.id == arm.ARM_INS_ADD and len(ops) == 3 and ops[0].type == arm.ARM_OP_REG \
                and ops[0].reg == arm.ARM_REG_PC and ops[1].type == arm.ARM_OP_REG and ops[1].reg == arm.ARM_REG_PC \
                and prev is not None and prev.address == insn.address - 4 and prev.id == arm.ARM_INS_CMP \
                and prev.operands[1].type == arm.ARM_OP_IMM:
            for i in range(prev.operands[1].imm + 1):
                work.append((insn.address + 8 + 4 * i, False))
            return after if conditional else None
        # by instruction, not by name: "ble" and "bls" are branches, not calls
        if insn.id in (arm.ARM_INS_BL, arm.ARM_INS_BLX) and ops and ops[0].type == arm.ARM_OP_IMM:
            target = ops[0].imm
            self.add_function(target, (not thumb) if insn.id == arm.ARM_INS_BLX else thumb, work)
            return after
        if insn.id in (arm.ARM_INS_B, arm.ARM_INS_CBZ, arm.ARM_INS_CBNZ) and ops \
                and ops[-1].type == arm.ARM_OP_IMM:
            work.append((ops[-1].imm, thumb))
            return after if conditional or insn.id != arm.ARM_INS_B else None
        if insn.id in (arm.ARM_INS_BX, arm.ARM_INS_BLX):
            return after if (conditional or insn.id == arm.ARM_INS_BLX) else None
        if mnemonic.startswith("pop") or mnemonic.startswith("ldm"):
            if any(o.type == capstone.arm.ARM_OP_REG and o.reg == capstone.arm.ARM_REG_PC for o in ops):
                return after if conditional else None
        if mnemonic.startswith("mov") and ops and ops[0].type == capstone.arm.ARM_OP_REG \
                and ops[0].reg == capstone.arm.ARM_REG_PC:
            return after if conditional else None
        return after

    def write(self, out):
        names = dict(KNOWN)
        for address in self.functions:
            names.setdefault(address, f"sub_{address:08x}")
        out.write(f"; the ARM7 sound driver: {SEGMENT_SIZE:#x} bytes at {SEGMENT_ADDRESS:#010x}"
                  f" (arm7.bin + {SEGMENT_OFFSET:#x}), FNV-1a {SEGMENT_FNV1A:#010x}\n")
        out.write(f"; {len(self.functions)} functions, {len(self.insns)} instructions reached\n")
        address = SEGMENT_ADDRESS
        end = SEGMENT_ADDRESS + SEGMENT_SIZE
        while address < end:
            if address in names and address in self.functions:
                mode = "thumb" if self.functions[address] else "arm"
                note = (" (from a literal)" if address in self.from_literal else
                        " (from its prologue)" if address in self.from_prologue else "")
                note = "" if address in KNOWN else note
                out.write(f"\n{names[address]}:  ; {mode}{note}\n")
            if address in self.insns:
                thumb, insn = self.insns[address]
                text = f"{insn.mnemonic} {insn.op_str}".strip()
                if insn.id in (capstone.arm.ARM_INS_BL, capstone.arm.ARM_INS_BLX, capstone.arm.ARM_INS_B) \
                        and insn.operands and insn.operands[0].type == capstone.arm.ARM_OP_IMM:
                    target = insn.operands[0].imm
                    if target in names:
                        text += f"  ; {names[target]}"
                out.write(f"  {address:08x}: {insn.bytes.hex():<8}  {text}\n")
                address += insn.size
            elif address % 4 == 0 and address + 4 <= end:
                value = self.word(address)
                label = f"  ; {names[value & ~1]}" if (value & ~1) in names else ""
                out.write(f"  {address:08x}: .word {value:#010x}{label}\n")
                address += 4
            else:
                out.write(f"  {address:08x}: .hword {struct.unpack_from('<H', self.code, address - SEGMENT_ADDRESS)[0]:#06x}\n")
                address += 2


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--arm7", type=Path, help="system/arm7.bin (default: found under data/extracted)")
    parser.add_argument("--out", type=Path, default=ROOT / "build" / "native" / "arm7" / "driver.lst")
    args = parser.parse_args()

    out = args.out.resolve()
    allowed = [(ROOT / "build").resolve(), (ROOT / "saves").resolve()]
    inside_repo = ROOT.resolve() in out.parents
    if inside_repo and not any(a == out or a in out.parents for a in allowed):
        sys.exit("arm7_listing: the listing is the game's code; write it under build/ or saves/ only")

    arm7 = (args.arm7 or find_arm7()).read_bytes()
    code = arm7[SEGMENT_OFFSET:SEGMENT_OFFSET + SEGMENT_SIZE]
    if len(code) != SEGMENT_SIZE or fnv1a(code) != SEGMENT_FNV1A:
        sys.exit("arm7_listing: this arm7.bin is not the one the port's driver was read from")

    listing = Listing(code)
    listing.walk({0x037FEEF8: False})  # SND_Init, entered in ARM state
    out.parent.mkdir(parents=True, exist_ok=True)
    with out.open("w", encoding="utf-8", newline="\n") as f:
        listing.write(f)
    print(f"{out}: {len(listing.functions)} functions, {len(listing.insns)} instructions")
    return 0


if __name__ == "__main__":
    sys.exit(main())
