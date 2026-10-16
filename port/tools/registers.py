"""The game's accesses to the DS's registers, rewritten so that none faults (prepare.py runs it on
every decompiled file): a register that only holds a value reaches plain memory (khdays_io_plain),
one that acts or is computed becomes khdays_io_store/khdays_io_load calls, one reached through a
run-time address is routed at each access; registers_left.txt lists what it does not know.
docs/BUILD.md, "The registers"."""
from __future__ import annotations

import re
from dataclasses import dataclass, field

IO = 0x04000000
IO_END = 0x04002000
IO2 = 0x04100000
IO2_END = 0x04100020

# Offsets from IO whose writes act, and whose reads the port computes
# (port/runtime/core/io_regs.c: khdays_io_write, khdays_io_read).
WRITE_ACTS = [
    (0x004, 0x008),  # DISPSTAT (its status bits are the frame loop's), VCOUNT
    (0x0b8, 0x0bc), (0x0c4, 0x0c8), (0x0d0, 0x0d4), (0x0dc, 0x0e0),  # DMA control
    (0x100, 0x110),  # the timers (said once)
    (0x180, 0x18c),  # IPC (said once)
    (0x1a0, 0x1a8),  # the card: SPI, ROMCTRL
    (0x214, 0x218),  # IF: writing one clears
    (0x240, 0x24a),  # VRAMCNT, WRAMCNT
    (0x280, 0x2c0),  # the math unit (prepare.py's; here only so it is never "plain")
    (0x400, 0x604),  # GXFIFO, the geometry's command ports, GXSTAT
]
READ_COMPUTED = [
    (0x004, 0x008),  # DISPSTAT, VCOUNT
    (0x100, 0x110),  # the timers
    (0x130, 0x132),  # KEYINPUT
    (0x180, 0x188),  # IPC
    (0x280, 0x2c0),  # the math unit
    (0x600, 0x6a4),  # the geometry's status and results
]

SIZES = {
    "u8": 1, "s8": 1, "vu8": 1, "vs8": 1, "char": 1, "REGType8": 1, "REGType8v": 1,
    "u16": 2, "s16": 2, "vu16": 2, "vs16": 2, "short": 2, "REGType16": 2, "REGType16v": 2,
    "u32": 4, "s32": 4, "vu32": 4, "vs32": 4, "int": 4, "unsigned": 4, "long": 4, "fx32": 4,
    "REGType32": 4, "REGType32v": 4, "BOOL": 4, "OSIrqMask": 4,
    "u64": 8, "s64": 8, "vu64": 8, "vs64": 8, "REGType64": 8, "REGType64v": 8,
    # structs the C lays over registers, their sizes from their definitions:
    "BgControl": 2,  # unsigned short bitfields, 16 bits (Ov002_GetItemFlag.c, TileTextRenderer_Init.c)
    "GXAffineReg": 16,  # paPb, pcPd, x, y (include/nitro/gx.h)
}

# SDK functions the decomp's C defines that are given a register's address:
# (which argument, how many bytes from it they reach), read off their C
# (libs/nitro/gx/auto, libs/nitro/gx/calls). Given a value register, the
# argument becomes its place in the plain view.
PLAIN_CALLEES = {
    "G2x_SetBlendAlpha_": (0, 4),  # *r0 = ... (one word: BLDCNT, BLDALPHA)
    "G2x_SetBlendBrightness_": (0, 6),  # dst[0], dst[2] (u16): BLDCNT, BLDY
    "G2x_SetBlendBrightnessExt_": (0, 6),  # +0, +1, +2 (u16)
    "G2x_ChangeBlendBrightness_": (0, 6),  # +0 read and written, +2 (u16)
    "GXx_SetMasterBrightness_": (0, 2),  # *dst (u16)
    "GXx_GetMasterBrightness_": (0, 2),  # *reg (u16)
    "G2x_SetBGyAffine_": (0, 16),  # GXAffineReg: paPb, pcPd, x, y
    # (dest, the argument that holds the size): MI_CpuFill32 is a static
    # inline of the files that use it (*d++ = data, size / 4 words);
    # INITi_CpuClear32_0x01ff86fc is the HAL's clear32 (port/hal/sdk_mi.c)
    "MI_CpuFill32": (0, ("argument", 2)),
    "INITi_CpuClear32_0x01ff86fc": (1, ("argument", 2)),
}
# The port's own implementations of SDK routines (port/hal/sdk_mi.c,
# sdk_gx.c) that take a register's DS address as it is and reach the port
# without a fault (khdays_io_is_register / touches_registers there).
HAL_CALLEES = {
    "MIi_CpuSend32", "MIi_CpuCopy32", "MIi_CpuCopy16", "MIi_CpuCopyFast", "MI_Copy36B", "MI_Copy48B",
    "MI_Copy64B", "GX_SendFifo48B", "GXi_NopClearFifo128_",
}
# Routines given a register's DS address as a value, to program the DMA with
# (the port's DMA reaches the register: runtime/core/io_regs.c).
VALUE_CALLEES = {"MIi_DmaSetParams", "MIi_DmaSetParams_wait", "func_01ff85d0", "MIi_DmaSetChannelRegs",
                 "func_01ff8664", "MIi_CardDmaCopy32", "MI_DmaFill32", "MI_DmaFill32Async", "MI_DmaFill16",
                 "MI_DmaCopy32", "MI_DmaCopy16", "MI_DmaCopy32Async", "MI_DmaCopy16Async"}
# Files whose every register address is one that only holds a value, reached
# in forms this does not follow (a table of pointers, an integer address,
# pointers moved and cast), read off their C: each address there becomes its
# place in the plain view. prepare.py stops if one is not a value register.
REVIEWED_PLAIN_FILES = {
    "libs/nns/g2d/auto/g2d_bg_cnt_table.c":
        "NitroSystem's NNSiG2dBGCNTTable, each BG's BGnCNT: its users (LoadBGCharacter.c, SetBGnControlTo*.c, "
        "BgCharVram_Upload.c, func_020149c4.c) read and write the u16 it points at",
    "libs/nns/g2d/calls/GetBGExtPlttSlot.c":
        "HW_REG_BASE plus data_02041a04's offset (g2d_screen_tables.c: each engine's BG0CNT and BG1CNT, or 0 for "
        "none), the u16 there read",
    "src/overlays/scenes/ov003/Ov003_DisplaySetup.c":
        "every register it reaches is a 2D one of either engine (DISPCNT, BGnCNT, the scrolls, the windows, the "
        "blending; not DISPSTAT), through pointers it moves and casts",
    "src/overlays/scenes/ov004_calendar/Ov004_ConfigureGraphics.c":
        "BG1-3CNT through pControl[0..2], DISPCNT and the blending (+0x50) through setup's pointers, BGnCNT, "
        "DISP3DCNT and POWCNT1",
    "src/overlays/screens/ov025_camp_menu_2/Ov025_InitDisplayRegs.c":
        "engine B's DISPCNT and BG0-3CNT (+8 to +0xe) through one pointer",
}
# A register's address in the C that is no access, by file: why.
NOT_ACCESSES = {
    ("src/overlays/scenes/ov003/Ov003_SceneInit.c", 0x04001008):
        "CamAnim_Start's fourth argument: CamAnim_Start (src/engine/CamAnim_Start.c) takes two",
}

TYPE = (r"(?:volatile\s+)?(?:const\s+)?(?:volatile\s+)?(?:struct\s+|union\s+)?(?:unsigned\s+|signed\s+)?[A-Za-z_]\w*"
        r"(?:\s+(?:long|int|short|char))*")
LIT_CORE = r"0x0?4[01][0-9a-fA-F]{5}"
LITERAL = r"(?<![\w.])" + LIT_CORE + r"[uUlL]*\b"
NUMBER = r"(?:0x[0-9a-fA-F]+|\d+)[uUlL]*"
REG_DEFINE = re.compile(
    r"^([ \t]*#[ \t]*define[ \t]+)(\w+)([ \t]+)\(\s*\*\s*\(\s*(" + TYPE + r")\s*\*\s*\)\s*\(?\s*(" + LITERAL +
    r"|[A-Za-z_]\w*)\s*\)?\s*\)[ \t]*$", re.M)
# a file's own macro for a pointer to a register: `#define NAME ((T *)LIT)`
POINTER_DEFINE = re.compile(
    r"^[ \t]*#[ \t]*define[ \t]+(\w+)[ \t]+(\(\s*\(\s*" + TYPE + r"\s*\*\s*\)\s*\(?\s*" + LITERAL +
    r"\s*\)?\s*\))[ \t]*$", re.M)
# `#define NAME(p) (*(T *)(p))`
DEREF_MACRO = re.compile(
    r"^[ \t]*#[ \t]*define[ \t]+(\w+)\(\s*(\w+)\s*\)[ \t]+\(\s*\*\s*\(\s*(" + TYPE +
    r")\s*\*\s*\)\s*\(\s*\2\s*\)\s*\)[ \t]*$", re.M)
OBJECT_DEFINE = re.compile(r"^[ \t]*#[ \t]*define[ \t]+([A-Za-z_]\w*)[ \t]+([^\n\\]*?)[ \t]*(?:/[/*][^\n]*)?$", re.M)
DEFINE_LINE = re.compile(r"^[ \t]*#[ \t]*define\b[^\n]*(?:\\\n[^\n]*)*", re.M)
# `*(T *)LIT` or `*(T *)(LIT)`
CAST = re.compile(r"\*\s*\(\s*(" + TYPE + r")\s*\*\s*\)\s*(?:\(\s*(" + LITERAL + r")\s*\)|(" + LITERAL + r"))")
ASSIGN = r"(?:=(?!=)|\|=|&=|\^=|\+=|-=|<<=|>>=)"
COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)
BYTE_TYPE = r"(?:const\s+)?(?:volatile\s+)?(?:u8|char|unsigned\s+char|vu8|s8)"
KEYWORDS = {"if", "while", "for", "switch", "return", "sizeof", "do", "else", "case"}


def lit(text: str) -> int:
    return int(text.rstrip("uUlL"), 0)


def type_size(type_text: str) -> int | None:
    words = [w for w in re.split(r"\s+", type_text.strip()) if w not in ("volatile", "const")]
    if words[-2:] == ["long", "long"]:
        return 8
    if words in (["unsigned", "short"], ["short", "int"], ["signed", "short"]):
        return 2
    if words in (["unsigned", "char"], ["signed", "char"]):
        return 1
    if words in (["unsigned", "int"], ["unsigned", "long"], ["signed", "int"]):
        return 4
    return SIZES.get(words[-1]) if words else None


def overlaps(offset: int, size: int, ranges) -> bool:
    return any(offset < end and start < offset + size for start, end in ranges)


def is_register(address: int) -> bool:
    return IO <= address < IO_END or IO2 <= address < IO2_END


def kind(address: int, size: int) -> str | None:
    """'plain', 'acts' (writes call), 'computed' (reads call), 'both', or None
    outside the registers."""
    if IO2 <= address < IO2_END:
        return "both"
    if not IO <= address < IO_END:
        return None
    if address + size > IO_END:
        return "both"  # past the registers: never plain
    off = address - IO
    w, r = overlaps(off, size, WRITE_ACTS), overlaps(off, size, READ_COMPUTED)
    return "both" if w and r else "acts" if w else "computed" if r else "plain"


def constants(text: str, known: dict[str, int]) -> dict[str, int]:
    """The object-like macros whose body is an integer constant expression
    (over numbers and other such macros), evaluated."""
    found = dict(known)
    pending = {m[1]: m[2] for m in OBJECT_DEFINE.finditer(text) if m[2]}
    token = re.compile(r"0x[0-9a-fA-F]+[uUlL]*|\d+[uUlL]*|[A-Za-z_]\w*|<<|>>|[-+*|&()~]|\S")
    for _ in range(len(pending) + 1):
        progress = False
        for name, body in list(pending.items()):
            parts = []
            for t in token.findall(body):
                if re.fullmatch(NUMBER, t):
                    parts.append(str(lit(t)))
                elif re.fullmatch(r"[A-Za-z_]\w*", t):
                    if t not in found:
                        break
                    parts.append(str(found[t]))
                elif t in ("<<", ">>", "-", "+", "*", "|", "&", "(", ")", "~"):
                    parts.append(t)
                else:
                    pending.pop(name)
                    break
            else:
                try:
                    value = eval("".join(parts) or "x", {"__builtins__": {}}, {})  # noqa: S307 (numbers, operators)
                except Exception:
                    pending.pop(name, None)
                    continue
                if isinstance(value, int):
                    found[name] = value
                pending.pop(name, None)
                progress = True
        if not progress:
            break
    return found


@dataclass
class Shared:
    """What the shared headers define: their constant macros, their register
    macros (name -> type, address, width), their structs."""
    values: dict[str, int]
    macros: dict[str, tuple[str, int, int]]
    layouts: dict[str, "Layout"] = field(default_factory=dict)


def header_tables(hw_text: str, *other_headers: str) -> Shared:
    """The shared headers' tables (nitro/hw.h's register macros but the math
    unit's, which are the compat header's)."""
    values = constants(hw_text, {})
    macros: dict[str, tuple[str, int, int]] = {}
    for m in REG_DEFINE.finditer(hw_text):
        address = lit(m[5]) if m[5][0].isdigit() else values.get(m[5])
        size = type_size(m[4])
        if address is not None and size is not None and kind(address, size) is not None and \
                not overlaps(address - IO, size, [(0x280, 0x2c0)]):
            macros[m[2]] = (m[4], address, size)
    layouts: dict[str, Layout] = {}
    for header in (hw_text, *other_headers):
        layouts = struct_layouts(header, layouts, values)
    return Shared(values, macros, layouts)


def plain_pointer(type_text: str, address: int) -> str:
    return f"({type_text} *)khdays_io_plain(0x{address - IO:x}u)"


def plain_lvalue(type_text: str, address: int) -> str:
    return f"(*{plain_pointer(type_text, address)})"


def plain_integer(address: int) -> str:
    """A value register's place in the plain view, as the integer the literal was."""
    return f"KHDAYS_IO_ADDRESS(khdays_io_plain(0x{address - IO:x}u))"


def load(type_text: str, address: int, size: int) -> str:
    type_text = value_type(type_text)
    if size == 8:
        return (f"(({type_text})((unsigned long long)khdays_io_load(0x{address:08x}u, 4) | "
                f"(unsigned long long)khdays_io_load(0x{address + 4:08x}u, 4) << 32))")
    return f"(({type_text})khdays_io_load(0x{address:08x}u, {size}))"


def value_type(type_text: str) -> str:
    """A type as a value's (a cast to it): without volatile and const."""
    return " ".join(w for w in type_text.split() if w not in ("volatile", "const"))


def load_at(type_text: str, address: str, size: int) -> str:
    """A read at a run-time address (an expression without side effects)."""
    type_text = value_type(type_text)
    if size == 8:
        return (f"(({type_text})((unsigned long long)khdays_io_load({address}, 4) | "
                f"(unsigned long long)khdays_io_load(({address}) + 4, 4) << 32))")
    return f"(({type_text})khdays_io_load({address}, {size}))"


def store_at(address: str, size: int, value: str) -> str:
    """A statement storing at a run-time address: a 64-bit one as two words, low first."""
    if size == 8:
        return (f"{{ const unsigned long long khdays_v_ = (unsigned long long)({value}); "
                f"khdays_io_store({address}, 4, (unsigned int)khdays_v_); "
                f"khdays_io_store(({address}) + 4, 4, (unsigned int)(khdays_v_ >> 32)); }}")
    return f"khdays_io_store({address}, {size}, (unsigned int)({value}));"


def store(address: int, size: int, value: str) -> str:
    if size == 8:
        return (f"{{ const unsigned long long khdays_v_ = (unsigned long long)({value}); "
                f"khdays_io_store(0x{address:08x}u, 4, (unsigned int)khdays_v_); "
                f"khdays_io_store(0x{address + 4:08x}u, 4, (unsigned int)(khdays_v_ >> 32)); }}")
    return f"khdays_io_store(0x{address:08x}u, {size}, (unsigned int)({value}));"


def rewrite_uses(text: str, target: str, type_text: str, address: int, size: int, k: str) -> str:
    """`target` (a regex: a macro's name, or a cast of the address) rewritten:
    its statement assignments, then its reads, by the register's kind. An
    lvalue in another form (its address taken, incremented, assigned inside an
    expression) keeps the address: the final scan lists it."""
    # `(target)` as `target` where the parentheses change nothing (not a
    # call's, not before a postfix operator): `(*(T *)LIT) = v;` is an
    # assignment like any other
    bare = re.compile(r"(?<![\w\]])\(\s*(" + target + r")\s*\)(?!\s*(?:\.|->|\[|\(|\+\+|--))")
    for _ in range(4):
        new = bare.sub(lambda m: m[1], text)
        if new == text:
            break
        text = new
    assign = re.compile(r"(?<![\w&+-])" + target + r"\s*(" + ASSIGN + r")\s*([^;]+);")

    def on_assign(m: re.Match) -> str:
        op, value = m[1], m[2].strip()
        if k in ("acts", "both"):
            if op == "=":
                return store(address, size, value)
            return store(address, size, f"{load(type_text, address, size)} {op[:-1]} ({value})")
        return f"{plain_lvalue(type_text, address)} {op} {value};"  # computed only: written plain

    text = assign.sub(on_assign, text)
    # an lvalue in any other form (assigned inside an expression, behind a
    # parenthesis kept, incremented, its address taken) is left as it is: it
    # faults, and the final scan lists it -- never read as a value
    read = re.compile(r"(?<![\w&+-])" + target + r"(?!(?:\s*\))*\s*(?:\+\+|--|" + ASSIGN + r"))")
    replacement = load(type_text, address, size) if k in ("computed", "both") else plain_lvalue(type_text, address)
    return read.sub(lambda m: replacement, text)


def outside(text: str, spans: list[tuple[int, int]], fn) -> str:
    """`fn` applied to the text between the spans, the spans kept."""
    parts, last = [], 0
    for start, end in spans:
        parts.append(fn(text[last:start]))
        parts.append(text[start:end])
        last = end
    parts.append(fn(text[last:]))
    return "".join(parts)


def blank(text: str) -> str:
    return re.sub(r"[^\n]", " ", text)


def code_view(text: str) -> str:
    """The text with its comments and #define lines blanked (the positions
    kept): where the uses are."""
    view = COMMENT.sub(lambda m: blank(m[0]), text)
    return DEFINE_LINE.sub(lambda m: blank(m[0]), view)


def substitute(text: str, view: str, pattern: re.Pattern, fn) -> str:
    """`pattern` found in `view` (the same positions as `text`), each match
    replaced in `text` by fn(match), last first."""
    for m in reversed(list(pattern.finditer(view))):
        text = text[:m.start()] + fn(m) + text[m.end():]
    return text


def takes_address(fn: str) -> bool:
    """Whether a call takes a register's DS address as it is: the port's own
    (khdays_io_load/store, KHDAYS_IO_ADDRESS), a HAL routine, the DMA's."""
    return fn.startswith(("khdays_io_", "KHDAYS_IO_")) or fn in HAL_CALLEES or fn in VALUE_CALLEES


def enclosing_call(before: str) -> tuple[str, int] | None:
    """The call whose argument list `before` ends inside: its name and which
    argument, or None. Casts and grouping parentheses are looked through."""
    depth, commas = 0, 0
    for i in range(len(before) - 1, -1, -1):
        c = before[i]
        if c in ")]":
            depth += 1
        elif c in "([":
            if depth:
                depth -= 1
                continue
            if c == "[":
                return None
            name = re.search(r"([A-Za-z_]\w*)\s*$", before[:i])
            if name and name[1] not in KEYWORDS:
                return name[1], commas
            commas = 0  # a grouping parenthesis: the argument is inside it
        elif c == "," and depth == 0:
            commas += 1
        elif c in ";{}" and depth == 0:
            return None
    return None


# --- structs laid over registers --------------------------------------------------

@dataclass
class Field:
    offset: int
    size: int
    count: int  # an array's elements (0: not an array)
    scalar: str | None  # its type when a plain number (a store or a load can take it)


@dataclass
class Layout:
    size: int
    align: int
    fields: dict[str, Field]


QUALIFIERS = {"volatile", "const", "struct", "union", "static", "register"}


def base_type(type_text: str) -> str:
    words = [w for w in re.split(r"\s+", type_text.strip()) if w not in QUALIFIERS]
    return words[-1] if words else ""


def matching_brace(text: str, open_at: int) -> int:
    depth = 0
    for i in range(open_at, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return i
    return -1


def evaluate(expr: str, values: dict[str, int]) -> int | None:
    parts = []
    for t in re.findall(r"0x[0-9a-fA-F]+[uUlL]*|\d+[uUlL]*|[A-Za-z_]\w*|<<|>>|\S", expr):
        if re.fullmatch(NUMBER, t):
            parts.append(str(lit(t)))
        elif re.fullmatch(r"[A-Za-z_]\w*", t):
            if t not in values:
                return None
            parts.append(str(values[t]))
        elif t in ("<<", ">>", "-", "+", "*", "/", "(", ")"):
            parts.append(t.replace("/", "//"))
        else:
            return None
    try:
        value = eval("".join(parts), {"__builtins__": {}}, {})  # noqa: S307 (numbers, operators)
    except Exception:
        return None
    return value if isinstance(value, int) else None


def struct_body(body: str, is_union: bool, layouts: dict[str, Layout], values: dict[str, int]) -> Layout | None:
    """A struct's or a union's layout (the DS's: each field at its natural
    alignment, bitfields packed in units of their type), or None when a
    field's type is not known."""
    fields: dict[str, Field] = {}
    offset = size = 0
    align = 1
    bits_left = 0  # room in the open bitfield unit
    unit = 0
    pos = 0
    while pos < len(body):
        brace = body.find("{", pos)
        semi = body.find(";", pos)
        if semi < 0:
            break
        if 0 <= brace < semi:  # a nested struct or union
            close = matching_brace(body, brace)
            head = body[pos:brace]
            semi = body.find(";", close)
            inner = struct_body(body[brace + 1:close], "union" in head, layouts, values)
            if inner is None:
                return None
            declarators = body[close + 1:semi]
            types = [(inner, d) for d in declarators.split(",") if d.strip()] or [(inner, "")]
            type_text = None
        else:
            decl = body[pos:semi].strip()
            pos = semi + 1
            if not decl:
                continue
            m = re.match(r"((?:(?:volatile|const|unsigned|signed|struct|union)\s+)*[A-Za-z_]\w*"
                         r"(?:\s+(?:long|int|short|char))*)\s+(.*)$", decl, re.S)
            if not m:
                return None
            type_text = m[1]
            sized = type_size(type_text)
            inner = layouts.get(base_type(type_text)) if sized is None else Layout(sized, sized, {})
            if inner is None:
                return None
            types = [(inner, d) for d in m[2].split(",")]
        pos = semi + 1
        for t, d in types:
            d = d.strip()
            bits = re.match(r"(\w*)\s*:\s*(.+)$", d)
            if bits:  # a bitfield
                width = evaluate(bits[2], values)
                if width is None:
                    return None
                if is_union:
                    size = max(size, t.size)
                elif bits_left >= width and unit == t.size:
                    bits_left -= width
                else:
                    offset = -(-offset // t.align) * t.align
                    if bits[1]:
                        fields[bits[1]] = Field(offset, t.size, 0, None)
                    offset += t.size
                    unit, bits_left = t.size, t.size * 8 - width
                align = max(align, t.align)
                continue
            bits_left = 0
            dm = re.match(r"(\**)\s*(\w+)\s*((?:\[[^\]]*\]\s*)*)$", d) if d else None
            if d and not dm:
                return None
            count = 1
            for dim in re.findall(r"\[([^\]]*)\]", dm[3]) if dm else []:
                n = evaluate(dim, values)
                if n is None:
                    return None
                count *= n
            element = Layout(4, 4, {}) if dm and dm[1] else t  # a pointer: 4 bytes, as the DS's
            here = 0 if is_union else -(-offset // element.align) * element.align
            if dm:
                scalar = type_text if type_text and not dm[1] and not dm[3] and type_size(type_text) else None
                fields[dm[2]] = Field(here, element.size * count, count if dm[3] else 0, scalar)
            if is_union:
                size = max(size, element.size * count)
            else:
                offset = here + element.size * count
            align = max(align, element.align)
    total = size if is_union else offset
    return Layout(-(-total // align) * align, align, fields)


STRUCT_HEAD = re.compile(r"\b(typedef\s+)?(struct|union)\s+(\w+)?\s*\{|\b(typedef\s+)(struct|union)\s*\{")


def struct_layouts(text: str, known: dict[str, Layout], values: dict[str, int]) -> dict[str, Layout]:
    """The structs and unions `text` defines (by tag and by typedef name)."""
    layouts = dict(known)
    view = COMMENT.sub(lambda m: blank(m[0]), text)
    for m in STRUCT_HEAD.finditer(view):
        open_at = m.end() - 1
        close = matching_brace(view, open_at)
        if close < 0:
            continue
        layout = struct_body(view[open_at + 1:close], (m[2] or m[5]) == "union", layouts, values)
        if layout is None:
            continue
        if m[3]:
            layouts[m[3]] = layout
        if m[1] or m[4]:
            names = re.match(r"\s*([^;{}]*);", view[close + 1:])
            for n in (names[1].split(",") if names else []):
                if re.fullmatch(r"\s*\w+\s*", n):
                    layouts[n.strip()] = layout
    return layouts


# --- 1. macros and constant sums ----------------------------------------------

def expand_macros(text: str, values: dict[str, int]) -> str:
    """Where used: the macros for a register's address (the shared headers'
    and the file's), a file's macros for a pointer to one, and
    `#define NAME(p) (*(T *)(p))` given one."""
    bodies = {n: f"(0x{v:08x})" for n, v in values.items() if is_register(v)}
    bodies.update({m[1]: m[2].strip() for m in POINTER_DEFINE.finditer(text)})
    used = [n for n in bodies if re.search(r"\b" + n + r"\b", text)]
    if used:
        names = re.compile(r"(?<![\w.])(" + "|".join(map(re.escape, sorted(used, key=len, reverse=True))) +
                           r")\b(?!\s*\()")
        text = substitute(text, code_view(text), names, lambda m: bodies[m[1]])
    for m in list(DEREF_MACRO.finditer(text)):
        name, type_text = m[1], m[3]
        call = re.compile(r"(?<![\w.])" + name + r"\s*\(\s*(\(?\s*" + LITERAL + r"\s*\)?)\s*\)")
        text = substitute(text, code_view(text), call, lambda c: f"(*({type_text} *)({c[1]}))")
    return text


def fold(text: str, values: dict[str, int]) -> str:
    """`LIT + K`, `(LIT) + K`, `(LIT + K)` as one literal (K a number or a
    constant macro), until none is left -- unless a higher operator binds K
    (`LIT + 4 * i` stays). A parenthesized literal loses its parentheses but
    after a cast or a name (a call's argument)."""
    names = [n for n in values if re.search(r"\b" + n + r"\b", text)]
    k = NUMBER if not names else r"(?:" + NUMBER + r"|" + "|".join(map(re.escape, names)) + r")"
    sum_ = re.compile(r"\(\s*(" + LITERAL + r")\s*\)|(?<![\w)])(" + LITERAL + r")\s*([+-])\s*(" + k +
                      r")(?![\w.]|\s*[*/%(\[])")

    def one(m: re.Match, view: str) -> str:
        if m[1]:
            if re.search(r"(?:\*\s*\)|\w)\s*$", view[max(0, m.start() - 40):m.start()]):
                return m[0]
            return m[1]
        k_text = m[4]
        k_value = values[k_text] if k_text in values else lit(k_text)
        return f"0x{lit(m[2]) + (k_value if m[3] == '+' else -k_value):08x}"

    for _ in range(8):
        view = code_view(text)
        new = substitute(text, view, sum_, lambda m: one(m, view))
        if new == text:
            break
        text = new
    return text


# --- 2. pointers set to a register's address -----------------------------------

@dataclass
class Init:
    name: str
    name_start: int
    cast_start: int  # where `(T *)...` starts
    end: int
    type_text: str
    base: int | None  # None: a register's address computed at run time


def closing(text: str, open_at: int) -> int:
    """The index of the bracket that closes the one at `open_at`, or -1."""
    pairs = {"(": ")", "[": "]"}
    want = [pairs[text[open_at]]]
    for i in range(open_at + 1, len(text)):
        c = text[i]
        if c in pairs:
            want.append(pairs[c])
        elif c in ")]":
            if not want or c != want[-1]:
                return -1
            want.pop()
            if not want:
                return i
        elif c in ";{}":
            return -1
    return -1


INIT_HEAD = re.compile(r"(?<![\w.>])([A-Za-z_]\w*)\s*=\s*(&\s*\(\s*)?\(\s*(" + TYPE + r")\s*\*\s*\)\s*")
RUNTIME_BASE = re.compile(r"^\s*(?:\(\s*(?:u32|unsigned\s+int|unsigned\s+long)\s*\)\s*)?" + LITERAL + r"\s*[+-]|\+\s*" +
                          LITERAL + r"\s*$")


def pointer_inits(view: str) -> list[Init]:
    """`N = (T *)LIT`, `N = (T *)(LIT)`; at run time `N = (T *)(LIT + e)`,
    `N = &((T *)LIT)[e]`."""
    found = []
    for m in INIT_HEAD.finditer(view):
        at = m.end()
        if m[2]:  # &((T *)LIT)[e]
            lm = re.match(LITERAL + r"\s*\)\s*\[", view[at:])
            if not lm:
                continue
            close = closing(view, at + lm.end() - 1)
            if close < 0:
                continue
            end, base = close + 1, None
        else:
            lm = re.match(r"\(\s*(" + LITERAL + r")\s*\)|(" + LITERAL + r")", view[at:])
            if lm:
                end, base = at + lm.end(), lit(lm[1] or lm[2])
            elif view.startswith("(", at):
                close = closing(view, at)
                if close < 0 or not RUNTIME_BASE.search(view[at + 1:close]):
                    continue
                end, base = close + 1, None
            else:
                continue
        if not re.match(r"\s*[;,)]", view[end:]):
            continue
        cast_start = view.index("(", view.index("=", m.end(1)))
        found.append(Init(m[1], m.start(1), cast_start, end, m[3], base))
    return found


@dataclass
class Use:
    start: int
    end: int
    form: str  # 'deref', 'callee', 'address', 'step', 'set'
    offset: int | None = None  # None: computed at run time
    width: int | None = None
    type_text: str | None = None  # an lvalue's own type, when a plain number


def pointer_uses(view: str, name: str, inits: list[Init], layouts: dict[str, Layout]) -> list[Use] | None:
    """Each use of a pointer `name` but its initializers: 'deref', 'callee' (given to a
    PLAIN_CALLEES function), 'address' (its DS address as it is), 'step', 'set'; None for a form not
    known. A pointer to a struct (`layouts`): its fields' offsets and widths."""
    init_spans = [(i.name_start, i.end) for i in inits]
    pointees = {base_type(i.type_text) for i in inits}
    layout = layouts.get(next(iter(pointees))) if len(pointees) == 1 else None
    sizes = [type_size(i.type_text) or (layout.size if layout else None) for i in inits]
    elt = max(sizes) if all(sizes) else None
    scalar = inits[0].type_text if len({i.type_text for i in inits}) == 1 and type_size(inits[0].type_text) else None
    uses = []

    def number(e: str) -> int | None:
        return lit(e.strip()) if re.fullmatch(r"\s*" + NUMBER + r"\s*", e) else None

    for u in re.finditer(r"(?<![\w.>])" + name + r"\b", view):
        if any(s <= u.start() < e for s, e in init_spans):
            continue
        before = view[max(0, u.start() - 200):u.start()]
        after = view[u.end():u.end() + 200]
        b0 = u.start() - len(before)
        call = enclosing_call(before)
        if call and takes_address(call[0]):
            uses.append(Use(u.start(), u.end(), "address"))  # the port takes the DS address as it is
            continue
        # N->field, N->field[e]; N->field.x and the like reach the whole field
        m = re.match(r"\s*->\s*(\w+)", after)
        if m and layout and m[1] in layout.fields and not re.search(r"&\s*$", before):
            f = layout.fields[m[1]]
            end = u.end() + m.end()
            if after[m.end():].lstrip().startswith("[") and f.count:
                open_at = end + after[m.end():].index("[")
                close = closing(view, open_at)
                if close < 0:
                    return None
                k = number(view[open_at + 1:close])
                width = f.size // f.count
                uses.append(Use(u.start(), close + 1, "deref", None if k is None else f.offset + k * width, width,
                                f.scalar))
            elif re.match(r"\s*(?:\.|\[)", after[m.end():]) or f.scalar is None:
                uses.append(Use(u.start(), end, "deref", f.offset, f.size, None))
            else:
                uses.append(Use(u.start(), end, "deref", f.offset, f.size, f.scalar))
            continue
        # N[e]
        if after.lstrip().startswith("[") and elt:
            open_at = u.end() + after.index("[")
            close = closing(view, open_at)
            if close < 0:
                return None
            k = number(view[open_at + 1:close])
            uses.append(Use(u.start(), close + 1, "deref", None if k is None else k * elt, elt, scalar))
            continue
        # *(T2 *)((u8 *)N + e)  /  *(T2 *)(N + e)  /  *(T2 *)N  /  *(N + e)
        mb = re.search(r"\*\s*(?:\(\s*(" + TYPE + r")\s*\*\s*\)\s*)?(\(\s*)?(?:\(\s*(" + BYTE_TYPE +
                       r")\s*\*\s*\)\s*)?$", before)
        if mb and (mb[1] or mb[2]):
            width = type_size(mb[1]) if mb[1] else elt
            type_text = mb[1] or scalar
            if mb[2]:
                open_at = b0 + mb.start(2)
                close = closing(view, open_at)
                inner = view[u.end():close] if close > 0 else ""
                am = re.fullmatch(r"\s*\+\s*(.+)", inner, re.S)
                if close < 0 or not (am or not inner.strip()):
                    return None
                k = number(am[1]) if am else 0
                scale = 1 if mb[3] else elt
                off = None if k is None or not scale else k * scale
                end = close + 1
            else:
                off, end = 0, u.end()
            if width:
                uses.append(Use(b0 + mb.start(), end, "deref", off, width, type_text))
                continue
        # its declaration (`T *N;`, `T *a, *N;`)
        if re.search(r"(?:^|[;{}(])\s*(?:(?:register|static|volatile|const)\s+)*" + TYPE +
                     r"\s*\*\s*(?:const\s+)?(?:\w+\s*,\s*\*\s*)*$", before) and re.match(r"\s*[;,]", after):
            continue
        # *N (after an operator, or a cast: `(void)*N`)
        stripped = before.rstrip()
        if stripped.endswith("*") and not re.match(r"\s*(?:->|\+(?!\+)|-(?![-=]))", after) and elt:
            prev = stripped[:-1].rstrip()
            if not prev or not re.match(r"[\w)\]]", prev[-1]) or \
                    re.search(r"\(\s*" + TYPE + r"\s*(?:\*\s*)?\)$", prev):
                uses.append(Use(u.start() - (len(before) - len(stripped)) - 1, u.end(), "deref", 0, elt, scalar))
                continue
        # N++, N += K: moved; N = e: set to something else
        if re.match(r"\s*(?:\+\+|--|\+=|-=)", after) or re.search(r"(?:\+\+|--)\s*$", before):
            uses.append(Use(u.start(), u.end(), "step"))
            continue
        if re.match(r"\s*=(?!=)", after):
            uses.append(Use(u.start(), u.end(), "set"))
            continue
        # (void)N
        if re.search(r"\(\s*void\s*\)\s*$", before):
            uses.append(Use(u.start(), u.end(), "address"))
            continue
        tail = re.match(r"\s*(?:\+\s*(" + NUMBER + r")\s*)?\)?\s*[,)]", after)
        if call and tail:
            fn, index = call
            byte = re.search(r"\(\s*" + BYTE_TYPE + r"\s*\*\s*\)\s*$", before)
            reach = callee_reach(fn, index, view, u.start())
            if reach and (byte or elt or not tail[1]):
                off = lit(tail[1]) * (1 if byte else elt) if tail[1] else 0
                uses.append(Use(u.start(), u.end(), "callee", off, reach))
                continue
        return None
    return uses


def statement_start(view: str, at: int) -> bool:
    """Whether `at` begins a statement."""
    before = view[:at].rstrip()
    return not before or before[-1] in ";{}):" or re.search(r"\belse$", before) is not None


def side_effects(expr: str) -> bool:
    return re.search(r"\+\+|--|(?<![=!<>])=(?!=)|\w\s*\(", expr) is not None


def route(text: str, view: str, lvalues: list[tuple[int, int, str]], rel: str, left: list[str]) -> str:
    """Each lvalue (start, end, its type) in a register, whose address is the
    DS's (a pointer that keeps it, or a cast computed at run time): a
    statement assignment to it through khdays_io_store, a read through
    khdays_io_load -- the register's effect, whatever its kind."""
    spans = sorted(lvalues, reverse=True)
    for start, end, type_text in spans:
        nested = any(s < start < e or s < end < e for s, e, _ in spans if (s, e) != (start, end))
        lvalue = text[start:end]
        size = type_size(type_text)
        line = view.count("\n", 0, start) + 1
        if nested or size is None or side_effects(lvalue):
            left.append(f"{rel}:{line}: {lvalue.strip()[:60]} -- an access through a register's address not routed")
            continue
        address = f"KHDAYS_IO_ADDRESS(&({lvalue}))"
        # the parentheses around it that change nothing, taken in
        while True:
            b = re.search(r"(?<![\w\]])\(\s*$", text[:start])
            a = re.match(r"\s*\)(?!\s*(?:\.|->|\[|\(|\+\+|--))", text[end:])
            if not (b and a):
                break
            start, end = b.start(), end + a.end()
        after = text[end:]
        m = re.match(r"\s*(" + ASSIGN + r")\s*([^;]+);", after)
        if m and statement_start(view, start):
            op, value = m[1], m[2].strip()
            if op != "=":
                value = f"{load_at(type_text, address, size)} {op[:-1]} ({value})"
            text = text[:start] + store_at(address, size, value) + text[end + m.end():]
        elif re.match(r"(?:\s*\))*\s*(?:\+\+|--|" + ASSIGN + r")", after) or \
                re.search(r"(?:\+\+|--|&)\s*$", text[:start]):
            left.append(f"{rel}:{line}: {lvalue.strip()[:60]} -- an lvalue form not routed")
        else:
            text = text[:start] + load_at(type_text, address, size) + text[end:]
    return text


def rewrite_pointers(text: str, rel: str, left: list[str], layouts: dict[str, Layout]) -> str:
    view = code_view(text)
    inits_by_name: dict[str, list[Init]] = {}
    for i in pointer_inits(view):
        inits_by_name.setdefault(i.name, []).append(i)
    edits = []  # (start, end, replacement): the plain ones
    lvalues = []  # (start, end, type): the routed ones
    for name, inits in inits_by_name.items():
        bases = [i.base for i in inits]
        if not any(b is None or is_register(b) for b in bases):
            continue
        line = view.count("\n", 0, inits[0].name_start) + 1
        what = f"0x{bases[0]:08x}" if bases[0] is not None else "a register's address"
        uses = pointer_uses(view, name, inits, layouts)
        if uses is None:
            left.append(f"{rel}:{line}: pointer {name} = {what} -- a use in a form not known")
            continue
        reaches = [u for u in uses if u.form in ("deref", "callee")]
        if not reaches:
            continue  # only its address used: nothing reaches a register through it
        if any(u.form == "set" for u in uses):
            left.append(f"{rel}:{line}: pointer {name} = {what} -- set to something else, and dereferenced")
            continue
        if None not in bases and all(u.form in ("deref", "callee") and u.offset is not None for u in uses) and \
                all(kind(b + u.offset, u.width) == "plain" for b in bases for u in reaches):
            for i in inits:
                edits.append((i.cast_start, i.end, plain_pointer(i.type_text, i.base)))
            continue
        # the pointer keeps the DS address: each access through it is routed
        if any(u.form == "callee" for u in uses) or any(u.type_text is None for u in reaches):
            left.append(f"{rel}:{line}: pointer {name} = {what} -- an access through it not routed")
            continue
        lvalues.extend((u.start, u.end, u.type_text) for u in reaches)
    # the casts of a register's address computed at run time, dereferenced
    for m in re.finditer(r"\*\s*\(\s*(\(\s*)?(" + TYPE + r")\s*\*\s*\)\s*\(", view):
        open_at = m.end() - 1
        close = closing(view, open_at)
        if close < 0 or not RUNTIME_BASE.search(view[open_at + 1:close]):
            continue
        end = close + 1
        if m[1]:
            outer = re.match(r"\s*\)", view[end:])
            if not outer:
                continue
            end += outer.end()
        if not any(s <= m.start() < e for s, e, _ in lvalues):
            lvalues.append((m.start(), end, m[2]))
    # a struct laid over registers, through a cast of its address: each field
    for m in re.finditer(r"\(\s*\(\s*(" + TYPE + r")\s*\*\s*\)\s*(?:\(\s*(" + LITERAL + r")\s*\)|(" + LITERAL +
                         r"))\s*\)\s*->\s*(\w+)", view):
        layout = layouts.get(base_type(m[1]))
        f = layout.fields.get(m[4]) if layout else None
        if f is None or f.scalar is None or re.match(r"\s*[.\[]", view[m.end():]):
            line = view.count("\n", 0, m.start()) + 1
            left.append(f"{rel}:{line}: {m[0]} -- a field not known, or not a number")
            continue
        lvalues.append((m.start(), m.end(), f.scalar))
    for start, end, replacement in sorted(edits, reverse=True):
        text = text[:start] + replacement + text[end:]
    if lvalues:
        # the plain edits are initializers, before or after a routed use: its
        # position moved by those before it
        moved = []
        for start, end, type_text in lvalues:
            delta = sum(len(r) - (e - s) for s, e, r in edits if e <= start)
            moved.append((start + delta, end + delta, type_text))
        text = route(text, code_view(text), moved, rel, left)
    return text


# --- 3. a register's address given to an SDK function ---------------------------

def call_arguments(view: str, open_at: int) -> list[str] | None:
    """A call's arguments (its parenthesis at `open_at`), as text."""
    close = closing(view, open_at)
    if close < 0:
        return None
    args, depth, last = [], 0, open_at + 1
    for i in range(open_at + 1, close):
        c = view[i]
        if c in "([":
            depth += 1
        elif c in ")]":
            depth -= 1
        elif c == "," and depth == 0:
            args.append(view[last:i])
            last = i + 1
    args.append(view[last:close])
    return args


def callee_reach(fn: str, index: int, view: str, at: int) -> int | None:
    """How many bytes PLAIN_CALLEES function `fn` reaches from its argument
    `index`, for the call around `at`; None if not known."""
    if fn not in PLAIN_CALLEES or PLAIN_CALLEES[fn][0] != index:
        return None
    reach = PLAIN_CALLEES[fn][1]
    if isinstance(reach, int):
        return reach
    m = None
    for m in re.finditer(r"\b" + fn + r"\s*\(", view[:at]):
        pass
    args = call_arguments(view, m.end() - 1) if m else None
    if not args or len(args) <= reach[1] or not re.fullmatch(r"\s*" + NUMBER + r"\s*", args[reach[1]]):
        return None
    return lit(args[reach[1]].strip())


def rewrite_callee_arguments(text: str) -> str:
    view = code_view(text)
    names = "|".join(PLAIN_CALLEES)
    call = re.compile(r"\b(" + names + r")\s*\(")
    edits = []
    for m in call.finditer(view):
        fn = m[1]
        index = PLAIN_CALLEES[fn][0]
        args = call_arguments(view, m.end() - 1)
        if not args or len(args) <= index:
            continue
        arg_start = m.end() + sum(len(a) + 1 for a in args[:index])
        arg = args[index]
        am = re.fullmatch(r"(\s*)(\(\s*)?(?:\(\s*(" + TYPE + r")\s*\*\s*\)\s*)?(?:\(\s*(" + LITERAL + r")\s*\)|(" +
                          LITERAL + r"))(\s*\))?(\s*)", arg)
        if not am or bool(am[2]) != bool(am[6]):
            continue
        address = lit(am[4] or am[5])
        reach = callee_reach(fn, index, view, m.end())
        if reach is None or kind(address, reach) != "plain":
            continue
        replacement = plain_pointer(am[3], address) if am[3] else plain_integer(address)
        edits.append((arg_start, arg_start + len(arg), f"{am[1]}{replacement}{am[7]}"))
    for start, end, replacement in sorted(edits, reverse=True):
        text = text[:start] + replacement + text[end:]
    return text


def reviewed_plain(text: str, rel: str) -> str:
    """A REVIEWED_PLAIN_FILES file: each register address in the plain view
    (after a pointer cast, the pointer; else the integer)."""
    view = code_view(text)
    edits = []
    for m in re.finditer(LITERAL, view):
        address = lit(m[0])
        if not is_register(address):
            continue
        cast = re.search(r"\(\s*(" + TYPE + r")\s*\*\s*\)\s*(\(\s*)?$", view[max(0, m.start() - 120):m.start()])
        size = type_size(cast[1]) if cast and type_size(cast[1]) else 2
        if kind(address, size) != "plain":
            raise SystemExit(f"registers.py: {rel} is reviewed as reaching value registers only, "
                             f"but names 0x{address:08x} ({kind(address, size)})")
        if cast and cast[2]:
            close = re.match(r"\s*\)", view[m.end():])
            if close:
                edits.append((m.start() - len(cast[0]), m.end() + close.end(), plain_pointer(cast[1], address)))
                continue
        if cast:
            edits.append((m.start() - len(cast[0]), m.end(), plain_pointer(cast[1], address)))
        else:
            edits.append((m.start(), m.end(), plain_integer(address)))
    for start, end, replacement in sorted(edits, reverse=True):
        text = text[:start] + replacement + text[end:]
    return text


# --- the file ------------------------------------------------------------------

def rewrite(text: str, rel: str, shared: Shared, left: list[str]) -> str:
    """One decompiled file's register accesses rewritten."""
    if not re.search(LITERAL + r"|\bREG_|\breg_|\bHW_", text):
        return text
    values = constants(text, shared.values)
    layouts = struct_layouts(text, shared.layouts, values)
    text = expand_macros(text, values)
    text = fold(text, values)
    if rel in REVIEWED_PLAIN_FILES:
        return reviewed_plain(text, rel)
    # `*((T *)LIT)` as `(*(T *)LIT)`
    text = re.sub(r"\*\s*\(\s*(\(\s*" + TYPE + r"\s*\*\s*\)\s*" + LITERAL + r")\s*\)", r"(*\1)", text)
    macros = dict(shared.macros)
    # 1. this file's register macros: a value register's defined plain
    plain_defines = []
    for m in REG_DEFINE.finditer(text):
        address = lit(m[5]) if m[5][0].isdigit() else values.get(m[5])
        size = type_size(m[4])
        if address is None or size is None or kind(address, size) is None:
            continue
        if kind(address, size) == "plain":
            plain_defines.append((m.start(), m.end(), f"{m[1]}{m[2]}{m[3]}{plain_lvalue(m[4], address)}"))
            macros.pop(m[2], None)  # this file's own, over a shared one
        else:
            macros[m[2]] = (m[4], address, size)
    for start, end, line in reversed(plain_defines):
        text = text[:start] + line + text[end:]

    def define_spans(t: str) -> list[tuple[int, int]]:
        return [(m.start(), m.end()) for m in DEFINE_LINE.finditer(t)]

    # 2. an acting or computed register's macro: its uses
    acting = []
    for name, (type_text, address, size) in macros.items():
        k = kind(address, size)
        if k == "plain" or not re.search(r"\b" + name + r"\b", text):
            continue  # a shared plain one is the compat header's
        acting.append(name)
        text = outside(text, define_spans(text),
                       lambda part: re.sub(r"&\s*" + name + r"\b", f"({type_text} *)0x{address:08x}", part))
        text = outside(text, define_spans(text),
                       lambda part: rewrite_uses(part, r"\b" + name + r"\b", type_text, address, size, k))

    # 3. pointers set to a register's address
    text = rewrite_pointers(text, rel, left, layouts)

    # 4. the casts of a register's address, dereferenced
    def casts(part: str) -> str:
        for m in reversed(list(CAST.finditer(part))):
            address, size = lit(m[2] or m[3]), type_size(m[1])
            if size is not None and kind(address, size) == "plain":
                part = part[:m.start()] + plain_lvalue(m[1], address) + part[m.end():]
        for type_text, literal in sorted({(m[1], m[2] or m[3]) for m in CAST.finditer(part)}):
            address, size = lit(literal), type_size(type_text)
            if size is None or kind(address, size) in (None, "plain"):
                continue
            target = (r"\*\s*\(\s*" + r"\s+".join(map(re.escape, type_text.split())) + r"\s*\*\s*\)\s*(?:\(\s*" +
                      re.escape(literal) + r"\s*\)|" + re.escape(literal) + r"(?!\w))")
            part = rewrite_uses(part, target, type_text, address, size, kind(address, size))
        return part

    text = outside(text, define_spans(text), casts)

    # 5. a register's address given to an SDK function
    text = rewrite_callee_arguments(text)

    # what still names a register: listed
    view = code_view(text)
    lines = text.splitlines()

    def note(position: int, what: str) -> None:
        line_no = view.count("\n", 0, position) + 1
        line = lines[line_no - 1].strip() if line_no <= len(lines) else ""
        left.append(f"{rel}:{line_no}: {what} -- {line[:160]}")

    for name in acting:
        for m in re.finditer(r"\b" + name + r"\b", view):
            note(m.start(), name)
    inits = pointer_inits(view)
    for m in re.finditer(LITERAL, view):
        address = lit(m[0])
        if not is_register(address) or (rel, address) in NOT_ACCESSES:
            continue
        before = view[max(0, m.start() - 200):m.start()]
        call = enclosing_call(before)
        if call and takes_address(call[0]):
            continue  # an address the port takes as it is
        if re.search(r"(?:[=!<>]=|[<>&|^]|\bcase)\s*(?:\(\s*(?:u32|unsigned\s+int)\s*\)\s*)?$", before) or \
                re.match(r"\s*(?:[=!<>]=|[<>&|^](?![=&|]))", view[m.end():]):
            continue  # compared, or a bit mask: a number
        init = next((i for i in inits if i.cast_start <= m.start() < i.end), None)
        if init:
            if any(e.startswith(f"{rel}:") and f"pointer {init.name} " in e for e in left):
                continue  # listed as a pointer already
            uses = pointer_uses(view, init.name, [i for i in inits if i.name == init.name], layouts)
            if uses is not None and all(u.form in ("address", "step") for u in uses):
                continue  # only its address used
        note(m.start(), f"0x{address:08x}")
    return text
