#!/usr/bin/env python3
"""Make the PS5 home-screen art from the user's own TNA iMPACT! disc (T005).

Reads private/extract/Splash/TNA_Load.bmp (the loading-screen background) and
private/extract/Splash/Splash.bmp (the TNA logo + legal screen) and writes:

  ps5/art/local/icon0.png   512x512    the app icon (the TNA logo)
  ps5/art/local/pic0.png    3840x2160  the PS5 home background
  ps5/art/local/pic1.png    1920x1080  the smaller background

Nothing derived from the disc is committed: ps5/art/local/ is git-ignored.
If the disc files aren't there, the existing placeholders are copied instead.

Pillow is used when installed. On a Mac without Pillow the script falls back
to macOS's sips for the big resizes and to a small built-in BMP reader / PNG
writer for the icon, so it works without installing anything.
"""
import pathlib
import shutil
import struct
import subprocess
import sys
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
EXTRACT = ROOT / "private" / "extract"
OUT = ROOT / "ps5" / "art" / "local"
PLACEHOLDERS = ROOT / "ps5" / "art"

try:  # optional: the fast, high-quality path
    from PIL import Image
except ImportError:  # (this Mac: Pillow isn't installed)
    Image = None


# ------------------------------------------------------------------ images

def read_bmp(path):
    """A BMP's pixels, top-down, as a list of bytearray RGB rows."""
    data = path.read_bytes()
    if data[:2] != b"BM":
        raise ValueError(f"{path}: not a BMP")
    offset = struct.unpack_from("<I", data, 10)[0]
    header = struct.unpack_from("<I", data, 14)[0]
    if header < 40:
        raise ValueError(f"{path}: unsupported BMP header")
    width, height, planes, bpp, compression = struct.unpack_from("<iiHHI", data, 18)
    if planes != 1 or compression not in (0, 3) or bpp not in (24, 32):
        raise ValueError(f"{path}: unsupported BMP ({bpp}-bit, compression {compression})")
    bottom_up = height > 0
    height = abs(height)
    stride = ((width * bpp // 8) + 3) & ~3
    rows = []
    for y in range(height):
        sy = height - 1 - y if bottom_up else y
        start = offset + sy * stride
        line = data[start:start + stride]
        row = bytearray(width * 3)
        for x in range(width):
            b, g, r = line[x * bpp // 8:x * bpp // 8 + 3]
            row[x * 3:x * 3 + 3] = bytes((r, g, b))
        rows.append(row)
    return width, height, rows


def load_rgb(path):
    """A picture's pixels: (width, height, top-down RGB bytearray rows)."""
    if Image is not None:
        im = Image.open(path).convert("RGB")
        w, h = im.size
        raw = im.tobytes()
        return w, h, [bytearray(raw[y * w * 3:(y + 1) * w * 3]) for y in range(h)]
    return read_bmp(path)


def write_png(path, width, height, rows):
    raw = b"".join(b"\x00" + bytes(row) for row in rows)

    def chunk(kind, payload):
        return (struct.pack(">I", len(payload)) + kind + payload +
                struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF))

    path.write_bytes(b"\x89PNG\r\n\x1a\n" +
                     chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
                     chunk(b"IDAT", zlib.compress(raw, 9)) +
                     chunk(b"IEND", b""))


def save_png(path, width, height, rows):
    if Image is not None:
        Image.frombytes("RGB", (width, height), b"".join(bytes(r) for r in rows)).save(path)
    else:
        write_png(path, width, height, rows)


def resize_pil(im, width, height):
    return im.resize((width, height), Image.LANCZOS)


def resize_rows(rows, sw, sh, dw, dh):
    """Bilinear scaling (the no-Pillow fallback; only the icon uses it)."""
    xmap = []
    for x in range(dw):
        fx = max(0.0, (x + 0.5) * sw / dw - 0.5)
        x0 = min(sw - 1, int(fx))
        xmap.append((x0, min(sw - 1, x0 + 1), fx - x0))
    out = []
    for y in range(dh):
        fy = max(0.0, (y + 0.5) * sh / dh - 0.5)
        y0 = min(sh - 1, int(fy))
        y1 = min(sh - 1, y0 + 1)
        t = fy - y0
        ra, rb = rows[y0], rows[y1]
        row = bytearray(dw * 3)
        for x, (xa, xb, u) in enumerate(xmap):
            for c in range(3):
                a_top = ra[xa * 3 + c]
                a_bottom = rb[xa * 3 + c]
                a = a_top + (a_bottom - a_top) * t
                b_top = ra[xb * 3 + c]
                b_bottom = rb[xb * 3 + c]
                b = b_top + (b_bottom - b_top) * t
                row[x * 3 + c] = int(a + (b - a) * u + 0.5)
        out.append(row)
    return out


def sips_resize(src, dst, width, height):
    if shutil.which("sips") is None:
        return False
    try:
        subprocess.run(["sips", "-s", "format", "png", "-z", str(height), str(width), str(src), "--out", str(dst)],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return True
    except (OSError, subprocess.CalledProcessError):
        return False


# -------------------------------------------------------------------- icon

def find_logo(rows, w, h):
    """The TNA logo's red body in the top half: its bbox.

    (Column/row histograms rather than a plain min/max: the truss in the
    background has a few warm pixels of its own, but the logo's red is dense.)
    """
    col_hits, row_hits = [0] * w, [0] * h
    for y in range(min(h, h // 2)):
        row = rows[y]
        for x in range(w):
            r, g, b = row[x * 3], row[x * 3 + 1], row[x * 3 + 2]
            if r > 110 and r > b + 70 and g < 130:  # the logo's red, not the gold/blue
                col_hits[x] += 1
                row_hits[y] += 1
    xs = [x for x, n in enumerate(col_hits) if n >= 10]
    ys = [y for y, n in enumerate(row_hits) if n >= 10]
    if not xs or not ys:
        return None
    return min(xs), min(ys), max(xs), max(ys)


def icon_from(splash):
    """The TNA logo on its dark background, the legal text under it removed."""
    w, h, rows = load_rgb(splash)
    logo = find_logo(rows, w, h)
    if logo is None:
        # (no logo found: take the top middle, still the disc's own picture)
        logo = (w // 4, 0, w * 3 // 4, h // 3)
    lx0, ly0, lx1, ly1 = logo
    cx, cy = (lx0 + lx1) // 2, (ly0 + ly1) // 2
    size = max(lx1 - lx0, ly1 - ly0) + 110
    size = max(size, 512)
    # (a dark pixel of the picture: the icon's padding)
    bg = bytes((rows[4][12], rows[4][13], rows[4][14]))
    canvas = [bytearray(bg * size) for _ in range(size)]
    left, top = cx - size // 2, cy - size // 2
    for dy in range(size):
        sy = top + dy
        if not (0 <= sy < h):
            continue
        sx0, sx1 = max(0, left), min(w, left + size)
        if sx1 <= sx0:
            continue
        dx = sx0 - left
        canvas[dy][dx * 3:(dx + sx1 - sx0) * 3] = rows[sy][sx0 * 3:sx1 * 3]
    # Below the logo, keep only the logo's own red and gold: the legal text
    # (white, then grey-blue where it fades) and the picture behind it become
    # the icon's flat background. The truss above the logo is left alone.
    for dy in range(max(0, ly0 - top - 6), size):
        row = canvas[dy]
        for dx in range(size):
            r, g, b = row[dx * 3], row[dx * 3 + 1], row[dx * 3 + 2]
            if not (r > 100 and r > b + 45):  # red / gold: the logo
                row[dx * 3:dx * 3 + 3] = bg
    if Image is not None:
        im = Image.frombytes("RGB", (size, size), b"".join(bytes(r) for r in canvas))
        resize_pil(im, 512, 512).save(OUT / "icon0.png")
    else:
        save_png(OUT / "icon0.png", 512, 512, resize_rows(canvas, size, size, 512, 512))


# -------------------------------------------------------------------- main

def copy_placeholder(src, dst, size):
    """The placeholder as-is, or resized when Pillow/sips can do it."""
    if size is not None:
        if Image is not None:
            resize_pil(Image.open(src).convert("RGB"), *size).save(dst)
            return True
        if sips_resize(src, dst, *size):
            return True
    shutil.copyfile(src, dst)
    return True


def placeholders():
    print("no disc art in private/extract/Splash: using the placeholders")
    icon = PLACEHOLDERS / "icon0.png"
    pic = PLACEHOLDERS / "pic0.png"
    if icon.exists():
        copy_placeholder(icon, OUT / "icon0.png", None)
    if pic.exists():
        copy_placeholder(pic, OUT / "pic0.png", (3840, 2160))
        copy_placeholder(pic, OUT / "pic1.png", (1920, 1080))
    for name in ("icon0.png", "pic0.png", "pic1.png"):
        if not (OUT / name).exists():
            print(f"missing {name} and no placeholder")


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    load = EXTRACT / "Splash" / "TNA_Load.bmp"
    splash = EXTRACT / "Splash" / "Splash.bmp"
    if not load.exists() and not splash.exists():
        placeholders()
        return

    icon_from(splash if splash.exists() else load)

    back = load if load.exists() else splash
    for name, size in (("pic0.png", (3840, 2160)), ("pic1.png", (1920, 1080))):
        if Image is not None:
            im = Image.open(back).convert("RGB")
            resize_pil(im, *size).save(OUT / name)
        elif not sips_resize(back, OUT / name, *size):
            w, h, rows = load_rgb(back)
            save_png(OUT / name, size[0], size[1], resize_rows(rows, w, h, size[0], size[1]))
    print(f"wrote {OUT}/icon0.png (512x512), pic0.png (3840x2160), pic1.png (1920x1080)")


if __name__ == "__main__":
    main()
