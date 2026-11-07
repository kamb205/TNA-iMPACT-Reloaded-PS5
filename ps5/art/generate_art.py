#!/usr/bin/env python3
"""Generate the placeholder PS5 home-screen art for TNA iMPACT! (T004).

Plain dark background + "TNA iMPACT!" text: icon0.png 512x512 and pic0.png
1920x1080. The final art comes in T005; this tool has no dependencies (Pillow
is not installed on this Mac and the task forbids installing packages), so it
writes the PNGs directly with zlib and a small built-in bitmap font.
"""
import pathlib
import struct
import zlib

W, H = 512, 512

# 5x7 bitmap glyphs ("#" = pixel). Only the characters in "TNA iMPACT!".
GLYPHS = {
    " ": ["....."] * 7,
    "!": ["..#..", "..#..", "..#..", "..#..", "..#..", ".....", "..#.."],
    "A": [".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"],
    "C": [".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."],
    "I": ["#####", "..#..", "..#..", "..#..", "..#..", "..#..", "#####"],
    "M": ["#...#", "##.##", "#.#.#", "#...#", "#...#", "#...#", "#...#"],
    "N": ["#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#", "#...#"],
    "P": ["####.", "#...#", "#...#", "####.", "#....", "#....", "#...."],
    "T": ["#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."],
    "i": ["..#..", ".....", "..#..", "..#..", "..#..", "..#..", "..#.."],
}

TEXT = "TNA iMPACT!"


def png(path, width, height, pixels):
    """pixels: bytearray of RGB rows."""
    raw = b"".join(b"\x00" + bytes(pixels[y * width * 3:(y + 1) * width * 3])
                   for y in range(height))

    def chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data +
                struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF))

    png_bytes = (b"\x89PNG\r\n\x1a\n" +
                 chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
                 chunk(b"IDAT", zlib.compress(raw, 9)) +
                 chunk(b"IEND", b""))
    path.write_bytes(png_bytes)


def render(path, width, height, scale, color, background):
    px = bytearray()
    for _ in range(width * height):
        px += bytes(background)
    text_w = (len(TEXT) * 6 - 1) * scale
    text_h = 7 * scale
    x0 = (width - text_w) // 2
    y0 = (height - text_h) // 2
    for i, ch in enumerate(TEXT):
        glyph = GLYPHS[ch]
        for gy, row in enumerate(glyph):
            for gx, bit in enumerate(row):
                if bit != "#":
                    continue
                for dy in range(scale):
                    for dx in range(scale):
                        x = x0 + (i * 6 + gx) * scale + dx
                        y = y0 + gy * scale + dy
                        o = (y * width + x) * 3
                        px[o:o + 3] = bytes(color)
    png(path, width, height, px)


def main():
    here = pathlib.Path(__file__).resolve().parent
    dark = (16, 20, 27)
    render(here / "icon0.png", 512, 512, 6, (236, 28, 36), dark)
    render(here / "pic0.png", 1920, 1080, 14, (236, 28, 36), dark)
    print(f"wrote {here / 'icon0.png'} (512x512) and {here / 'pic0.png'} (1920x1080)")


if __name__ == "__main__":
    main()
