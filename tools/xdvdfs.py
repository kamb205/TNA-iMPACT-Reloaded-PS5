#!/usr/bin/env python3
"""Read-only XDVDFS (Xbox/Xbox 360 game partition) lister/extractor.

Opens the ISO read-only; never writes to it. Auto-detects the game partition
offset (plain XISO, XGD1, XGD2, XGD3).

  xdvdfs.py ls  <iso>                      -> TSV: size, sector, path
  xdvdfs.py get <iso> <outdir> [glob ...]  -> extract matching files (default: all)
"""
import fnmatch, os, struct, sys

SECTOR = 2048
MAGIC = b"MICROSOFT*XBOX*MEDIA"
# partition base offsets: plain XISO, XGD2, XGD1, XGD3
BASES = [0x0, 0xFD90000, 0x18300000, 0x2080000]


def find_base(f):
    for base in BASES:
        f.seek(base + 32 * SECTOR)
        if f.read(20) == MAGIC:
            return base
    raise SystemExit("no XDVDFS volume descriptor found at known offsets")


def walk(f, base, sector, size, prefix=""):
    if size == 0:
        return
    f.seek(base + sector * SECTOR)
    data = f.read(size)
    stack, seen = [0], set()
    while stack:
        off = stack.pop()
        if off in seen or off + 14 > len(data):
            continue
        seen.add(off)
        left, right, start, fsize, attr, nlen = struct.unpack_from("<HHIIBB", data, off)
        if left == 0xFFFF:  # padding
            continue
        name = data[off + 14: off + 14 + nlen].decode("latin-1")
        path = prefix + name
        if attr & 0x10:
            yield (path + "/", start, fsize, True)
            yield from walk(f, base, start, fsize, path + "/")
        else:
            yield (path, start, fsize, False)
        if left:
            stack.append(left * 4)
        if right:
            stack.append(right * 4)


def entries(f):
    base = find_base(f)
    f.seek(base + 32 * SECTOR + 20)
    root_sector, root_size = struct.unpack("<II", f.read(8))
    return base, sorted(walk(f, base, root_sector, root_size), key=lambda e: e[0].lower())


def main():
    cmd, iso = sys.argv[1], sys.argv[2]
    with open(iso, "rb") as f:
        base, ents = entries(f)
        if cmd == "ls":
            print(f"# partition_base=0x{base:X}")
            for path, start, size, is_dir in ents:
                if not is_dir:
                    print(f"{size}\t{start}\t{path}")
        elif cmd == "get":
            out, pats = sys.argv[3], sys.argv[4:] or ["*"]
            for path, start, size, is_dir in ents:
                if is_dir or not any(fnmatch.fnmatch(path.lower(), p.lower()) for p in pats):
                    continue
                dst = os.path.join(out, path)
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                f.seek(base + start * SECTOR)
                left = size
                with open(dst, "wb") as o:
                    while left:
                        chunk = f.read(min(left, 1 << 20))
                        if not chunk:
                            raise SystemExit(f"short read: {path}")
                        o.write(chunk)
                        left -= len(chunk)
                print(f"{size}\t{path}")
        else:
            raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
