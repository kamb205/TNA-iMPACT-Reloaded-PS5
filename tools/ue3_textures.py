#!/usr/bin/env python3
"""List and dump Texture2D exports from TNA iMPACT! (Xbox 360) UE3 packages.

Packages are uncompressed big-endian UE3 packages.  Xbox 360 texture mips are
Xenos 2D-tiled; this script untiles them with the standard Xenia formula and
decodes DXT1/DXT5/A8R8G8B8/A8 to PNG.

Usage:
  python tools/ue3_textures.py PKG --list
  python tools/ue3_textures.py PKG --dump OUTDIR [--only SUBSTR]

Run with ~/ps5-deps/artvenv/bin/python (Pillow + numpy).
"""
import argparse
import os
import struct
import sys

import numpy as np
from PIL import Image

# ---------------------------------------------------------------- package ---

def be_i32(d, o):
    return struct.unpack('>i', d[o:o + 4])[0]


def be_u32(d, o):
    return struct.unpack('>I', d[o:o + 4])[0]


def fstring(d, o):
    n = be_i32(d, o)
    o += 4
    if n < 0:
        n = -n
        s = d[o:o + n * 2].decode('utf-16-be', 'replace')
        return s.rstrip('\x00'), o + n * 2
    s = d[o:o + n].decode('latin-1')
    return s.rstrip('\x00'), o + n


class Package:
    def __init__(self, path):
        self.path = path
        self.d = open(path, 'rb').read()
        d = self.d
        if be_u32(d, 0) != 0x9E2A83C1:
            raise ValueError('not a UE3 package: %s' % path)
        self.file_version = struct.unpack('>H', d[4:6])[0]
        self.licensee_version = struct.unpack('>H', d[6:8])[0]
        self.header_size = be_i32(d, 8)
        # TNA packages keep the first name table at offset 105; locate it by
        # finding the longest run of FString+8 byte name entries.
        self.name_offset, names = self._find_names()
        p = self.name_offset
        self.name_count = be_i32(d, p - 72)
        self.names = names[:self.name_count]
        self.export_count = be_i32(d, p - 64)
        self.export_offset = be_i32(d, p - 60)
        self.import_count = be_i32(d, p - 56)
        self.import_offset = be_i32(d, p - 52)
        self.imports = self._read_imports()
        self.exports = self._read_exports()

    def _find_names(self):
        d = self.d
        best = (0, 0, 0)
        for start in range(8, min(self.header_size, 4096)):
            count = 0
            i = start
            while count < 100000 and i + 12 <= len(d):
                ln = be_i32(d, i)
                if not 1 <= ln <= 200 or i + 4 + ln > len(d):
                    break
                b = d[i + 4:i + 4 + ln]
                if b[-1:] != b'\x00' or not all(32 <= c < 127 for c in b[:-1]):
                    break
                count += 1
                i += 4 + ln + 8
            if count > best[0]:
                best = (count, start, i)
        if best[0] < 2:
            raise ValueError('name table not found')
        _, start, _ = best
        names = []
        i = start
        for _ in range(100000):
            ln = be_i32(d, i)
            if not 1 <= ln <= 200:
                break
            b = d[i + 4:i + 4 + ln]
            if b[-1:] != b'\x00':
                break
            names.append(b[:-1].decode('latin-1'))
            i += 4 + ln + 8
        return start, names

    def _read_imports(self):
        out = []
        off = self.import_offset
        for _ in range(self.import_count):
            class_pkg = be_i32(self.d, off)
            class_name = be_i32(self.d, off + 8)
            outer = be_i32(self.d, off + 16)
            obj_name = be_i32(self.d, off + 20)
            # FName = (index, number): class package + class name + object name.
            out.append({
                'class_package': self._uname(class_pkg),
                'class_name': self._uname(class_name),
                'outer': outer,
                'name': self._uname(obj_name),
            })
            off += 28
        return out

    def _uname(self, idx):
        return self.names[idx] if 0 <= idx < len(self.names) else '?'

    def _ref(self, v):
        """Decode an FCompactIndex object reference to ('import'|'export', idx)."""
        if v == 0:
            return None
        if v < 0:
            return ('import', -v - 1)
        return ('export', v - 1)

    def _read_exports(self):
        out = []
        off = self.export_offset
        for k in range(self.export_count):
            cls = be_i32(self.d, off)
            sup = be_i32(self.d, off + 4)
            outer = be_i32(self.d, off + 8)
            name_idx = be_i32(self.d, off + 12)
            arche = be_i32(self.d, off + 16)
            size = be_i32(self.d, off + 32)
            data_off = be_i32(self.d, off + 36)
            out.append({
                'index': k,
                'class': self._ref(cls),
                'super': sup,
                'outer': self._ref(outer),
                'name': self._uname(name_idx),
                'archetype': arche,
                'serial_size': size,
                'serial_offset': data_off,
            })
            off += 68
        return out

    def class_name(self, export):
        ref = export['class']
        if not ref:
            return None
        kind, idx = ref
        if kind == 'import' and 0 <= idx < len(self.imports):
            return self.imports[idx]['name']
        if kind == 'export' and 0 <= idx < len(self.exports):
            return self.exports[idx]['name']
        return None

    def textures(self):
        return [e for e in self.exports if self.class_name(e) == 'Texture2D']


# --------------------------------------------------------------- texture ---

# UE3 EPixelFormat values (subset). TNA uses DXT1/DXT5/A8R8G8B8/A8.
EPIXELFORMAT = {
    3: ('G8', None, 1),
    5: ('DXT1', 'dxt1', 4),
    7: ('DXT5', 'dxt5', 3),
    12: ('A8R8G8B8', 'bgra8', 5),
    28: ('A8', 'a8', 3),
}


def xg_tiled_offset(x, y, width, log2_bpp):
    """Xenia XGAddress2DTiledOffset (byte offset / block index)."""
    aligned_width = (width + 31) & ~31
    log2_bpp_x2 = log2_bpp * 2
    macro = ((x >> 5) + (y >> 5) * (aligned_width >> 5)) << (log2_bpp_x2 + 8)
    micro = ((x & 7) + ((y & 0xE) << 2)) << log2_bpp
    return (macro + ((micro & ~0xF) << 1) + (micro & 0xF) +
            ((y & 1) << 4) + ((y & 0x10) << (log2_bpp_x2 + 1)) +
            ((x & 0x10) << log2_bpp) + ((y & 8) << 2))


def untile_blocks(raw, width_blocks, height_blocks, log2_bpp, block_bytes):
    out = np.zeros((height_blocks, width_blocks, block_bytes), np.uint8)
    src = np.frombuffer(raw, np.uint8)
    n = width_blocks * height_blocks * block_bytes
    src = src[:n]
    for y in range(height_blocks):
        for x in range(width_blocks):
            idx = xg_tiled_offset(x, y, width_blocks, log2_bpp) >> log2_bpp
            o = idx * block_bytes
            if o + block_bytes <= len(src):
                out[y, x] = src[o:o + block_bytes]
    return out.tobytes()


def parse_texture(pkg, export):
    """Return (width, height, format_code, mip0_data, mip0_offset) or None."""
    d = pkg.d
    o = export['serial_offset']
    end = o + export['serial_size']
    # Skip the tagged property list: FName(8) FName(8) size(4) index(4) value.
    while o + 8 <= end:
        name_idx = be_i32(d, o)
        if name_idx == 0:          # "None" terminator
            o += 8
            break
        typ_idx = be_i32(d, o + 4)
        size = be_i32(d, o + 12)
        o += 16
        if typ_idx < 0 or typ_idx >= len(pkg.names):
            return None
        if size < 0 or size > end - o:
            return None
        o += size
    if o + 4 > end:
        return None
    # UTexture2D (UE3 360): INT MipCount? then per-mip FByteBulkData.
    # Layout found in TNA: u32 numMips, then each mip: u32 flags, u32 size,
    # u32 sizeX, u32 sizeY, u32 format, data[size].
    count = be_u32(d, o)
    o += 4
    if not 1 <= count <= 16:
        return None
    mips = []
    for _ in range(count):
        if o + 20 > end:
            return None
        _flags = be_u32(d, o)
        size = be_u32(d, o + 4)
        sx = be_u32(d, o + 8)
        sy = be_u32(d, o + 12)
        fmt = be_u32(d, o + 16)
        o += 20
        if size > end - o:
            return None
        mips.append((sx, sy, fmt, o, size))
        o += size
        o = (o + 3) & ~3
    if not mips:
        return None
    sx, sy, fmt, off, size = mips[0]
    return sx, sy, fmt, off, size, mips


def decode_texture(pkg, tex):
    d = pkg.d
    sx, sy, fmt, off, size, mips = tex
    if fmt not in EPIXELFORMAT:
        raise ValueError('unsupported format %d' % fmt)
    name, dec, log2_bpp = EPIXELFORMAT[fmt]
    raw = d[off:off + size]
    if dec in ('dxt1', 'dxt5'):
        bw, bh = (sx + 3) // 4, (sy + 3) // 4
        block = 8 if dec == 'dxt1' else 16
        linear = untile_blocks(raw, bw, bh, log2_bpp, block)
        im = Image.frombytes('RGBA', (bw * 4, bh * 4), linear, 'bcn',
                             1 if dec == 'dxt1' else 2)
        return im.crop((0, 0, sx, sy))
    if dec == 'bgra8':
        bw, bh = sx, sy
        linear = untile_blocks(raw, bw, bh, log2_bpp, 4)
        a = np.frombuffer(linear, np.uint8).reshape(sy, sx, 4)
        return Image.fromarray(a[:, :, [2, 1, 0, 3]], 'RGBA')
    if dec == 'a8':
        return Image.fromarray(np.frombuffer(raw, np.uint8)[:sx * sy].reshape(sy, sx), 'L').convert('RGBA')
    raise ValueError('unhandled %s' % name)


# ------------------------------------------------------------------- cli ---

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('package')
    ap.add_argument('--list', action='store_true')
    ap.add_argument('--dump')
    ap.add_argument('--only', default=None)
    args = ap.parse_args()
    pkg = Package(args.package)
    texs = pkg.textures()
    if args.list or not args.dump:
        print('%s: %d names, %d exports, %d imports, %d textures'
              % (os.path.basename(args.package), len(pkg.names),
                 pkg.export_count, pkg.import_count, len(texs)))
        for e in texs:
            info = parse_texture(pkg, e)
            desc = ''
            if info:
                sx, sy, fmt, off, size, _ = info
                fname = EPIXELFORMAT.get(fmt, ('fmt%d' % fmt, None, 0))[0]
                desc = '%dx%d %s mip0@%d (%d bytes)' % (sx, sy, fname, off, size)
            print('  %-40s %s' % (e['name'], desc))
        return
    os.makedirs(args.dump, exist_ok=True)
    for e in texs:
        if args.only and args.only.lower() not in e['name'].lower():
            continue
        info = parse_texture(pkg, e)
        if not info:
            print('skip (unparsed):', e['name'])
            continue
        try:
            im = decode_texture(pkg, info)
        except Exception as exc:  # noqa: BLE001
            print('skip (%s): %s' % (exc, e['name']))
            continue
        out = os.path.join(args.dump, '%s.png' % e['name'])
        im.save(out)
        print('wrote', out, im.size)


if __name__ == '__main__':
    main()
