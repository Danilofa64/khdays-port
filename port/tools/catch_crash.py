#!/usr/bin/env python3
"""Run the port under a small debugger of its own and say where it died: a stack guard's fast fail
(0xc0000409) or an unhandled exception, the thread's registers and return addresses named with the
build's symbols, into the console and crash.txt. Windows only (docs/BUILD.md, "The tools").
    python port/tools/catch_crash.py [EXE] [--out FILE] [-- ARGS...]
"""
from __future__ import annotations

import argparse
import ctypes
import ctypes.wintypes as wt
import glob
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_EXE = ROOT / "build" / "port" / "obj-release-x64" / "khdays-native.exe"

DEBUG_ONLY_THIS_PROCESS = 0x2
INFINITE = 0xFFFFFFFF
DBG_CONTINUE = 0x00010002
DBG_EXCEPTION_NOT_HANDLED = 0x80010001
EXCEPTION_DEBUG_EVENT = 1
CREATE_PROCESS_DEBUG_EVENT = 3
EXIT_PROCESS_DEBUG_EVENT = 5
LOAD_DLL_DEBUG_EVENT = 6
STATUS_BREAKPOINT = 0x80000003
STATUS_WX86_BREAKPOINT = 0x4000001F
STATUS_STACK_BUFFER_OVERRUN = 0xC0000409
THREAD_ALL_ACCESS = 0x1FFFFF
CONTEXT_AMD64 = 0x00100000
CONTEXT_FULL = CONTEXT_AMD64 | 0x1 | 0x2 | 0x8


class EXCEPTION_RECORD(ctypes.Structure):
    _fields_ = [("ExceptionCode", wt.DWORD), ("ExceptionFlags", wt.DWORD), ("ExceptionRecord", ctypes.c_void_p),
                ("ExceptionAddress", ctypes.c_void_p), ("NumberParameters", wt.DWORD),
                ("ExceptionInformation", ctypes.c_ulonglong * 15)]


class EXCEPTION_DEBUG_INFO(ctypes.Structure):
    _fields_ = [("ExceptionRecord", EXCEPTION_RECORD), ("dwFirstChance", wt.DWORD)]


class CREATE_PROCESS_DEBUG_INFO(ctypes.Structure):
    _fields_ = [("hFile", wt.HANDLE), ("hProcess", wt.HANDLE), ("hThread", wt.HANDLE),
                ("lpBaseOfImage", ctypes.c_void_p), ("dwDebugInfoFileOffset", wt.DWORD),
                ("nDebugInfoSize", wt.DWORD), ("lpThreadLocalBase", ctypes.c_void_p),
                ("lpStartAddress", ctypes.c_void_p), ("lpImageName", ctypes.c_void_p), ("fUnicode", wt.WORD)]


class LOAD_DLL_DEBUG_INFO(ctypes.Structure):
    _fields_ = [("hFile", wt.HANDLE), ("lpBaseOfDll", ctypes.c_void_p), ("dwDebugInfoFileOffset", wt.DWORD),
                ("nDebugInfoSize", wt.DWORD), ("lpImageName", ctypes.c_void_p), ("fUnicode", wt.WORD)]


class DEBUG_EVENT_UNION(ctypes.Union):
    _fields_ = [("Exception", EXCEPTION_DEBUG_INFO), ("CreateProcessInfo", CREATE_PROCESS_DEBUG_INFO),
                ("LoadDll", LOAD_DLL_DEBUG_INFO), ("raw", ctypes.c_byte * 160)]


class DEBUG_EVENT(ctypes.Structure):
    _fields_ = [("dwDebugEventCode", wt.DWORD), ("dwProcessId", wt.DWORD), ("dwThreadId", wt.DWORD),
                ("u", DEBUG_EVENT_UNION)]


class M128A(ctypes.Structure):
    _fields_ = [("Low", ctypes.c_ulonglong), ("High", ctypes.c_longlong)]


class CONTEXT(ctypes.Structure):
    _pack_ = 16
    _fields_ = [("P1Home", ctypes.c_ulonglong), ("P2Home", ctypes.c_ulonglong), ("P3Home", ctypes.c_ulonglong),
                ("P4Home", ctypes.c_ulonglong), ("P5Home", ctypes.c_ulonglong), ("P6Home", ctypes.c_ulonglong),
                ("ContextFlags", wt.DWORD), ("MxCsr", wt.DWORD), ("SegCs", wt.WORD), ("SegDs", wt.WORD),
                ("SegEs", wt.WORD), ("SegFs", wt.WORD), ("SegGs", wt.WORD), ("SegSs", wt.WORD),
                ("EFlags", wt.DWORD), ("Dr0", ctypes.c_ulonglong), ("Dr1", ctypes.c_ulonglong),
                ("Dr2", ctypes.c_ulonglong), ("Dr3", ctypes.c_ulonglong), ("Dr6", ctypes.c_ulonglong),
                ("Dr7", ctypes.c_ulonglong), ("Rax", ctypes.c_ulonglong), ("Rcx", ctypes.c_ulonglong),
                ("Rdx", ctypes.c_ulonglong), ("Rbx", ctypes.c_ulonglong), ("Rsp", ctypes.c_ulonglong),
                ("Rbp", ctypes.c_ulonglong), ("Rsi", ctypes.c_ulonglong), ("Rdi", ctypes.c_ulonglong),
                ("R8", ctypes.c_ulonglong), ("R9", ctypes.c_ulonglong), ("R10", ctypes.c_ulonglong),
                ("R11", ctypes.c_ulonglong), ("R12", ctypes.c_ulonglong), ("R13", ctypes.c_ulonglong),
                ("R14", ctypes.c_ulonglong), ("R15", ctypes.c_ulonglong), ("Rip", ctypes.c_ulonglong),
                ("FltSave", ctypes.c_byte * 512), ("VectorRegister", M128A * 26),
                ("VectorControl", ctypes.c_ulonglong), ("DebugControl", ctypes.c_ulonglong),
                ("LastBranchToRip", ctypes.c_ulonglong), ("LastBranchFromRip", ctypes.c_ulonglong),
                ("LastExceptionToRip", ctypes.c_ulonglong), ("LastExceptionFromRip", ctypes.c_ulonglong)]


class STARTUPINFO(ctypes.Structure):
    _fields_ = [("cb", wt.DWORD), ("lpReserved", wt.LPWSTR), ("lpDesktop", wt.LPWSTR), ("lpTitle", wt.LPWSTR),
                ("dwX", wt.DWORD), ("dwY", wt.DWORD), ("dwXSize", wt.DWORD), ("dwYSize", wt.DWORD),
                ("dwXCountChars", wt.DWORD), ("dwYCountChars", wt.DWORD), ("dwFillAttribute", wt.DWORD),
                ("dwFlags", wt.DWORD), ("wShowWindow", wt.WORD), ("cbReserved2", wt.WORD),
                ("lpReserved2", ctypes.c_void_p), ("hStdInput", wt.HANDLE), ("hStdOutput", wt.HANDLE),
                ("hStdError", wt.HANDLE)]


class PROCESS_INFORMATION(ctypes.Structure):
    _fields_ = [("hProcess", wt.HANDLE), ("hThread", wt.HANDLE), ("dwProcessId", wt.DWORD),
                ("dwThreadId", wt.DWORD)]


k32 = ctypes.WinDLL("kernel32", use_last_error=True)
k32.WaitForDebugEvent.argtypes = [ctypes.POINTER(DEBUG_EVENT), wt.DWORD]
k32.ContinueDebugEvent.argtypes = [wt.DWORD, wt.DWORD, wt.DWORD]
k32.OpenThread.restype = wt.HANDLE
k32.OpenThread.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
k32.GetThreadContext.argtypes = [wt.HANDLE, ctypes.c_void_p]
k32.ReadProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                                  ctypes.POINTER(ctypes.c_size_t)]
k32.CloseHandle.argtypes = [wt.HANDLE]
k32.TerminateProcess.argtypes = [wt.HANDLE, wt.UINT]


def image_size(exe: Path) -> int:
    """SizeOfImage from the executable's PE optional header."""
    data = exe.read_bytes()[:4096]
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    return struct.unpack_from("<I", data, pe + 24 + 56)[0]


def symbolizer() -> str | None:
    found = glob.glob(r"C:\Program Files\Microsoft Visual Studio\*\*\VC\Tools\Llvm\x64\bin\llvm-symbolizer.exe")
    return found[0] if found else None


def name(exe: Path, rvas: list[int]) -> list[str]:
    tool = symbolizer()
    if tool is None or not rvas:
        return [f"+0x{r:x}" for r in rvas]
    out = subprocess.run([tool, f"--obj={exe}", "--relative-address"] + [f"0x{r:x}" for r in rvas],
                         capture_output=True, text=True).stdout
    blocks = [b.strip().splitlines() for b in out.strip().split("\n\n")]
    return [f"+0x{r:x} {' / '.join(b)}" for r, b in zip(rvas, blocks)]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("exe", type=Path, nargs="?", default=DEFAULT_EXE)
    ap.add_argument("--out", type=Path, default=Path("crash.txt"))
    ap.add_argument("args", nargs="*")
    args = ap.parse_args()
    exe = args.exe.resolve()
    size = image_size(exe)
    si = STARTUPINFO()
    si.cb = ctypes.sizeof(si)
    pi = PROCESS_INFORMATION()
    cmd = ctypes.create_unicode_buffer(subprocess.list2cmdline([str(exe)] + args.args))
    if not k32.CreateProcessW(None, cmd, None, None, False, DEBUG_ONLY_THIS_PROCESS, None, str(ROOT),
                              ctypes.byref(si), ctypes.byref(pi)):
        sys.exit(f"cannot start {exe}: error {ctypes.get_last_error()}")
    base = 0
    event = DEBUG_EVENT()
    report: list[str] = []
    while True:
        if not k32.WaitForDebugEvent(ctypes.byref(event), INFINITE):
            break
        code = event.dwDebugEventCode
        status = DBG_CONTINUE
        if code == CREATE_PROCESS_DEBUG_EVENT:
            base = event.u.CreateProcessInfo.lpBaseOfImage or 0
            if event.u.CreateProcessInfo.hFile:
                k32.CloseHandle(event.u.CreateProcessInfo.hFile)
        elif code == LOAD_DLL_DEBUG_EVENT:
            if event.u.LoadDll.hFile:
                k32.CloseHandle(event.u.LoadDll.hFile)
        elif code == EXCEPTION_DEBUG_EVENT:
            rec = event.u.Exception.ExceptionRecord
            first = event.u.Exception.dwFirstChance
            exc = rec.ExceptionCode & 0xFFFFFFFF
            if exc in (STATUS_BREAKPOINT, STATUS_WX86_BREAKPOINT):
                status = DBG_CONTINUE  # the loader's own breakpoint
            elif exc == STATUS_STACK_BUFFER_OVERRUN or not first:
                report = describe(pi.hProcess, event.dwThreadId, exc, rec, base, size, exe)
                print("\n".join(report))
                k32.TerminateProcess(pi.hProcess, exc)
                status = DBG_EXCEPTION_NOT_HANDLED
            else:
                status = DBG_EXCEPTION_NOT_HANDLED  # the port's own (its I/O traps): back to it
        elif code == EXIT_PROCESS_DEBUG_EVENT:
            k32.ContinueDebugEvent(event.dwProcessId, event.dwThreadId, DBG_CONTINUE)
            break
        k32.ContinueDebugEvent(event.dwProcessId, event.dwThreadId, status)
    if report:
        args.out.write_text("\n".join(report) + "\n", encoding="utf-8")
        print(f"(written to {args.out.resolve()})")
        return 1
    print("the game ended with no failure caught")
    return 0


def describe(process, thread_id: int, exc: int, rec: EXCEPTION_RECORD, base: int, size: int, exe: Path) -> list[str]:
    lines = [f"exception 0x{exc:08x} at 0x{rec.ExceptionAddress or 0:x}"
             + (f" (fast fail code {rec.ExceptionInformation[0]})" if exc == STATUS_STACK_BUFFER_OVERRUN else "")]
    thread = k32.OpenThread(THREAD_ALL_ACCESS, False, thread_id)
    raw = ctypes.create_string_buffer(ctypes.sizeof(CONTEXT) + 16)
    addr = (ctypes.addressof(raw) + 15) & ~15
    ctx = CONTEXT.from_address(addr)
    ctx.ContextFlags = CONTEXT_FULL
    if not thread or not k32.GetThreadContext(thread, ctypes.c_void_p(addr)):
        lines.append(f"no registers (error {ctypes.get_last_error()})")
        return lines
    rvas = []
    if base <= ctx.Rip < base + size:
        rvas.append(ctx.Rip - base)
    # the stack read in pieces up to 16 KB: a read that runs past the stack's
    # top fails whole, so each piece is read alone until one fails
    piece = ctypes.create_string_buffer(0x100)
    for at in range(0, 0x4000, len(piece)):
        got = ctypes.c_size_t(0)
        if not k32.ReadProcessMemory(process, ctypes.c_void_p(ctx.Rsp + at), piece, len(piece), ctypes.byref(got)):
            break
        for off in range(0, got.value - 7, 8):
            value = struct.unpack_from("<Q", piece.raw, off)[0]
            if base <= value < base + size:
                rvas.append(value - base)
    k32.CloseHandle(thread)
    lines.append(f"rip +0x{ctx.Rip - base:x}  rsp 0x{ctx.Rsp:x}; return addresses on the stack, nearest first:")
    seen = []
    for r in rvas:
        if r not in seen:
            seen.append(r)
    lines += ["  " + n for n in name(exe, seen[:40])]
    return lines


if __name__ == "__main__":
    sys.exit(main())
