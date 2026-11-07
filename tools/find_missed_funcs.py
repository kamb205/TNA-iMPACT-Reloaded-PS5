#!/usr/bin/env python3
"""Find guest function starts that ReXGlue's discovery pass missed.

ReXGlue only discovers functions reachable by direct branches/calls.  Functions that
are only ever reached through a function pointer (vtable slot, callback stored in
.data/.rdata, or an address built with `lis` + `addi`/`ori`) are skipped and later
abort at runtime with:

    [FATAL] Call to invalid or unregistered function at guest address 0x...

This tool scans a decrypted/decompressed guest image (the file:// dump of the mapped
image, as produced by SvR2010-Native's SVR_DUMP_IMAGE hook) for such addresses:

  (a) not already a registered function start (generated/default/tnaimpact_register.cpp
      plus the [entrypoint.functions] seeds in tnaimpact_manifest.toml),
  (b) referenced by a `lis` + `addi`/`ori` pair in .text, and/or by a 32-bit word in
      the data part of the image (vtables / pointer tables), and
  (c) sitting at the start of a gap: the preceding instruction is `blr`, `b`, `bctr`,
      or padding (0x00000000 / nop).

Output is TOML lines ready to paste into [entrypoint.functions]:

    0x82714EC0 = {}  # lis+addi ref at 0x8270F1A4

Usage:
    python3 -I tools/find_missed_funcs.py <image.bin> [base=0x82000000] [options]

The image must be the raw mapped image: byte 0 of the file is guest address `base`
(0x82000000 for this game), i.e. what DumpGuestImageIfRequested() writes.

Text bounds are taken from generated/default/tnaimpact_pch.h (REX_CODE_BASE /
REX_CODE_SIZE), unless --text-start/--text-size or --text-range is given.  Everything
in the image outside .text is treated as data (vtables/pointer tables).

Self-check (works even when the address is already a known seed):

    python3 -I tools/find_missed_funcs.py image.bin --self-check 0x82714EC0

Exit codes: 0 = ok, 1 = usage/IO error, 2 = self-check failed.
"""

import argparse
import os
import re
import struct
import sys

DEFAULT_BASE = 0x82000000
DEFAULT_TEXT_SIZE = 0xF60EC4  # REX_CODE_SIZE in generated/default/tnaimpact_pch.h

# PowerPC instruction helpers (big endian, 32-bit words).
OP_ADDI = 14
OP_ADDIS = 15
OP_B = 18
OP_BC = 16
OP_ORI = 24
OP_BCLR_BCTR = 19

BLR = 0x4E800020
BCTR = 0x4E800420
NOP = 0x60000000
PADDING = (0x00000000, NOP)


def u32(buf, off):
    return struct.unpack_from(">I", buf, off)[0]


def opcode(word):
    return word >> 26


def reg_rt(word):
    return (word >> 21) & 31


def reg_ra(word):
    return (word >> 16) & 31


def sign16(value):
    return value - 0x10000 if value & 0x8000 else value


def is_branch(word):
    return opcode(word) in (OP_B, OP_BC, OP_BCLR_BCTR)


def is_gap_start(prev):
    """True when `prev` (the instruction before a candidate) can end a function/gap."""
    if prev in PADDING or prev == BLR or prev == BCTR:
        return True
    # `b` (unconditional, with or without AA; LK=0) also ends blocks.
    if opcode(prev) == OP_B and not (prev & 1):
        return True
    return False


def find_pe_text_range(image, base):
    """Best-effort .text range from the PE headers embedded at the start of the image.

    Returns (start, size) or None.  The ReXGlue dump is the mapped image, so the PE
    header is at file offset 0 when base == image base (0x82000000).
    """
    if len(image) < 0x40 or image[:2] != b"MZ":
        return None
    pe = u32(image, 0x3C)
    if pe + 0x18 > len(image) or image[pe:pe + 4] != b"PE\0\0":
        return None
    nsections = struct.unpack_from(">H", image, pe + 6)[0]
    opt_size = struct.unpack_from(">H", image, pe + 20)[0]
    table = pe + 24 + opt_size
    if table + nsections * 40 > len(image):
        return None
    for i in range(nsections):
        hdr = table + i * 40
        vsize = u32(image, hdr + 8)
        vaddr = u32(image, hdr + 12)
        chars = u32(image, hdr + 36)
        if chars & 0x20000000:  # IMAGE_SCN_MEM_EXECUTE
            return base + vaddr, max(vsize & 0xFFFFFFFF, 1)
    return None


def read_project_defaults(root, explicit_start, explicit_size):
    """Resolve .text bounds and known-function sources for the repo at `root`."""
    gen = os.path.join(root, "generated", "default")
    pch = os.path.join(gen, "tnaimpact_pch.h")
    register = os.path.join(gen, "tnaimpact_register.cpp")
    manifest = os.path.join(root, "tnaimpact_manifest.toml")

    text_start, text_size = explicit_start, explicit_size
    if text_start is None or text_size is None:
        try:
            with open(pch, "r", encoding="utf-8", errors="replace") as f:
                head = f.read(65536)
        except OSError:
            head = ""
        m = re.search(r"#define\s+REX_CODE_BASE\s+(0x[0-9A-Fa-f]+)", head)
        if text_start is None and m:
            text_start = int(m.group(1), 16)
        m = re.search(r"#define\s+REX_CODE_SIZE\s+(0x[0-9A-Fa-f]+)", head)
        if text_size is None and m:
            text_size = int(m.group(1), 16)
    return text_start, text_size, register, manifest


def load_known_functions(register_path, manifest_path):
    """Registered starts from the generated registration table + manifest seeds."""
    known = set()

    if register_path and os.path.exists(register_path):
        rx = re.compile(r"SetFunction\(\s*(0x[0-9A-Fa-f]+)\s*,")
        with open(register_path, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                m = rx.search(line)
                if m:
                    known.add(int(m.group(1), 16))

    if manifest_path and os.path.exists(manifest_path):
        in_functions = False
        seed_rx = re.compile(r"^\s*(0x[0-9A-Fa-f]+)\s*=\s*\{\s*\}")
        with open(manifest_path, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                stripped = line.strip()
                if stripped.startswith("["):
                    in_functions = stripped == "[entrypoint.functions]"
                    continue
                if in_functions:
                    m = seed_rx.match(line)
                    if m:
                        known.add(int(m.group(1), 16))

    return known


def scan_code_refs(image, text_start, text_size, base):
    """Return {target_addr: first ref address} for `lis` + `addi`/`ori` pairs."""
    refs = {}
    start = text_start - base
    if start < 0:
        return refs
    end = min(start + text_size, len(image))
    for off in range(start, end - 3, 4):
        w = u32(image, off)
        if opcode(w) != OP_ADDIS or reg_ra(w) != 0:
            continue  # not `lis`
        rd = reg_rt(w)
        hi = (w & 0xFFFF) << 16
        # A pair is normally adjacent; allow one unrelated instruction in between and
        # stop at any branch (the base register does not survive a call/return).
        for step in range(1, 4):
            next_off = off + step * 4
            if next_off + 4 > end:
                break
            nw = u32(image, next_off)
            if is_branch(nw):
                break
            nop = opcode(nw)
            if nop == OP_ADDI and reg_ra(nw) == rd:
                target = (hi + sign16(nw & 0xFFFF)) & 0xFFFFFFFF
                refs.setdefault(target, base + off)
            elif nop == OP_ORI and reg_ra(nw) == rd:
                target = (hi | (nw & 0xFFFF)) & 0xFFFFFFFF
                refs.setdefault(target, base + off)
    return refs


def scan_data_refs(image, text_start, text_size, base):
    """Return {target_addr: first data-word address} for pointer words outside .text."""
    refs = {}
    tstart = text_start - base
    tend = tstart + text_size

    def consider(value, off):
        if text_start <= value < text_start + text_size:
            refs.setdefault(value, base + off)

    # Before .text.
    for off in range(0, max(0, min(tstart, len(image))), 4):
        consider(u32(image, off), off)
    # After .text.
    start = max(0, tend)
    for off in range(start, len(image) - 3, 4):
        consider(u32(image, off), off)
    return refs


def scan(image, base, text_start, text_size, known, ignore_known=False):
    """Return (candidates, stats). Candidate keys are guest addresses."""
    if text_start is None:
        raise SystemExit("error: could not determine .text start; pass --text-start/--text-size")
    if text_size is None:
        text_size = DEFAULT_TEXT_SIZE

    code_refs = scan_code_refs(image, text_start, text_size, base)
    data_refs = scan_data_refs(image, text_start, text_size, base)

    pool = set(code_refs) | set(data_refs)
    candidates = {}
    stats = {
        "code_refs": len(code_refs),
        "data_refs": len(data_refs),
        "pool": len(pool),
        "known": 0,
        "out_of_text": 0,
        "not_gap": 0,
        "reported": 0,
    }
    text_end = text_start + text_size
    prev_off_min = 4
    for addr in sorted(pool):
        if not ignore_known and addr in known:
            stats["known"] += 1
            continue
        if not (text_start <= addr < text_end) or addr % 4:
            stats["out_of_text"] += 1
            continue
        off = addr - base
        if off < prev_off_min or off + 4 > len(image):
            stats["out_of_text"] += 1
            continue
        prev = u32(image, off - 4)
        if not is_gap_start(prev):
            stats["not_gap"] += 1
            continue
        first = u32(image, off)
        if first in PADDING:
            stats["not_gap"] += 1
            continue
        candidates[addr] = (code_refs.get(addr), data_refs.get(addr))
    stats["reported"] = len(candidates)
    return candidates, stats


def reason_for(addr, refs):
    code_at, data_at = refs
    parts = []
    if code_at is not None:
        parts.append("lis+addi ref at 0x%08X" % code_at)
    if data_at is not None:
        parts.append("data word at 0x%08X" % data_at)
    if not parts:  # ignore_known self-check path can see unreferenced values? not expected
        parts.append("referenced")
    return "; ".join(parts)


def main(argv):
    ap = argparse.ArgumentParser(
        description="Find ReXGlue-missed guest function starts in a mapped image dump.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__.split("Usage:", 1)[1].split("The image must", 1)[0].strip(),
    )
    ap.add_argument("image", help="decrypted/decompressed mapped image (byte 0 = base)")
    ap.add_argument("base", nargs="?", default=None,
                    help="guest address of image byte 0 (default 0x82000000)")
    ap.add_argument("--text-start", default=None,
                    help="guest address of .text start (default: REX_CODE_BASE from pch)")
    ap.add_argument("--text-size", default=None,
                    help="size of .text in bytes (default: REX_CODE_SIZE from pch)")
    ap.add_argument("--register", default=None,
                    help="tnaimpact_register.cpp (default: generated/default/...)")
    ap.add_argument("--manifest", default=None,
                    help="manifest TOML (default: <repo>/tnaimpact_manifest.toml)")
    ap.add_argument("--ignore-known", action="store_true",
                    help="do not filter out already-registered starts (self-check)")
    ap.add_argument("--self-check", action="append", default=[], metavar="0xADDR",
                    help="assert this address is detected; exit 2 if not (repeatable)")
    ap.add_argument("--output", default=None, help="write TOML lines to this file")
    args = ap.parse_args(argv)

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    base = int(args.base, 0) if args.base else DEFAULT_BASE
    text_start = int(args.text_start, 0) if args.text_start else None
    text_size = int(args.text_size, 0) if args.text_size else None
    checks = [int(v, 0) for v in args.self_check]

    register = args.register or os.path.join(root, "generated", "default", "tnaimpact_register.cpp")
    manifest = args.manifest or os.path.join(root, "tnaimpact_manifest.toml")

    resolved_start, resolved_size, register, manifest = read_project_defaults(
        root, text_start, text_size)
    if args.register:
        register = args.register
    if args.manifest:
        manifest = args.manifest
    text_start, text_size = resolved_start, resolved_size

    try:
        with open(args.image, "rb") as f:
            image = f.read()
    except OSError as exc:
        print("error: cannot read image: %s" % exc, file=sys.stderr)
        return 1

    if text_start is None:
        pe = find_pe_text_range(image, base)
        if pe:
            text_start, text_size = pe
    if text_start is None:
        print("error: no .text bounds (pass --text-start/--text-size)", file=sys.stderr)
        return 1
    if text_size is None:
        text_size = DEFAULT_TEXT_SIZE

    known = load_known_functions(register, manifest)
    candidates, stats = scan(image, base, text_start, text_size, known,
                             ignore_known=args.ignore_known)

    lines = ["0x%08X = {}  # %s" % (addr, reason_for(addr, refs))
             for addr, refs in sorted(candidates.items())]
    text_out = "\n".join(lines) + ("\n" if lines else "")
    if args.output:
        with open(args.output, "w", encoding="utf-8") as f:
            f.write(text_out)
        print("wrote %d lines to %s" % (len(lines), args.output), file=sys.stderr)
    else:
        sys.stdout.write(text_out)

    print("image=%s base=0x%08X text=0x%08X+0x%X known=%d code_refs=%d data_refs=%d "
          "pool=%d filtered(known=%d out_of_text=%d not_gap=%d) candidates=%d"
          % (args.image, base, text_start, text_size, len(known), stats["code_refs"],
             stats["data_refs"], stats["pool"], stats["known"], stats["out_of_text"],
             stats["not_gap"], stats["reported"]), file=sys.stderr)

    if checks:
        detected = candidates
        if not args.ignore_known:
            # Self-check must also work for addresses that are already seeded.
            detected, _ = scan(image, base, text_start, text_size, known, ignore_known=True)
        failures = 0
        for addr in checks:
            status = "PASS" if addr in detected else "FAIL"
            extra = " (already registered)" if addr in known else ""
            print("self-check 0x%08X: %s%s" % (addr, status, extra), file=sys.stderr)
            failures += status == "FAIL"
        if failures:
            return 2

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
