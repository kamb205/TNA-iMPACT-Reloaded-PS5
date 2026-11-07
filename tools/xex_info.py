#!/usr/bin/env python3
"""Print XEX2 header metadata (no decryption): execution info, file format,
entry point, base address, import library names, static libs, page map.

  xex_info.py <default.xex>
"""
import struct, sys

NAMES = {
    0x000002FF: "RESOURCE_INFO", 0x000003FF: "FILE_FORMAT_INFO", 0x000005FF: "DELTA_PATCH_DESCRIPTOR",
    0x00000405: "BASE_REFERENCE", 0x000080FF: "BOUNDING_PATH", 0x00008105: "DEVICE_ID",
    0x00010001: "ORIGINAL_BASE_ADDRESS", 0x00010100: "ENTRY_POINT", 0x00010201: "IMAGE_BASE_ADDRESS",
    0x000103FF: "IMPORT_LIBRARIES", 0x00018002: "CHECKSUM_TIMESTAMP", 0x00018102: "ENABLED_FOR_CALLCAP",
    0x00018200: "ENABLED_FOR_FASTCAP", 0x000183FF: "ORIGINAL_PE_NAME", 0x000200FF: "STATIC_LIBRARIES",
    0x00020104: "TLS_INFO", 0x00020200: "DEFAULT_STACK_SIZE", 0x00020301: "DEFAULT_FILESYSTEM_CACHE_SIZE",
    0x00020401: "DEFAULT_HEAP_SIZE", 0x00028002: "PAGE_HEAP_SIZE_AND_FLAGS", 0x00030000: "SYSTEM_FLAGS",
    0x00040006: "EXECUTION_INFO", 0x00040201: "TITLE_WORKSPACE_SIZE", 0x00040310: "GAME_RATINGS",
    0x00040404: "LAN_KEY", 0x000405FF: "XBOX360_LOGO", 0x000406FF: "MULTIDISC_MEDIA_IDS",
    0x000407FF: "ALTERNATE_TITLE_IDS", 0x00040801: "ADDITIONAL_TITLE_MEMORY", 0x00E10402: "EXPORTS_BY_NAME",
}
ENC = {0: "none", 1: "normal (AES)"}
COMP = {0: "none", 1: "basic (zero-run)", 2: "normal (LZX)", 3: "delta"}


def u32(b, o): return struct.unpack_from(">I", b, o)[0]
def u16(b, o): return struct.unpack_from(">H", b, o)[0]


def main(path):
    b = open(path, "rb").read()
    assert b[:4] == b"XEX2", f"not XEX2: {b[:4]!r}"
    mod_flags, pe_off, _, sec_off, nhdr = struct.unpack_from(">IIIII", b, 4)
    print(f"module_flags=0x{mod_flags:08X} pe_data_offset=0x{pe_off:X} security_offset=0x{sec_off:X} headers={nhdr}")
    for i in range(nhdr):
        key, val = struct.unpack_from(">II", b, 24 + i * 8)
        name = NAMES.get(key, f"0x{key:08X}")
        lo = key & 0xFF
        if lo in (0, 1):
            print(f"  {name} = 0x{val:08X}")
            continue
        size = u32(b, val) if lo == 0xFF else lo * 4
        d = b[val: val + size]
        if key == 0x00040006:
            media, ver, base_ver, tid = struct.unpack_from(">IIII", d)
            plat, ex_tab, disc, ndisc, save = struct.unpack_from(">BBBBI", d, 16)
            fv = lambda v: f"{v >> 28}.{(v >> 24) & 0xF}.{(v >> 8) & 0xFFFF}.{v & 0xFF}"
            print(f"  EXECUTION_INFO title_id={tid:08X} media_id={media:08X} version={fv(ver)} "
                  f"base_version={fv(base_ver)} disc={disc}/{ndisc} savegame_id={save:08X}")
        elif key == 0x000003FF:
            enc, comp = struct.unpack_from(">HH", d, 4)
            print(f"  FILE_FORMAT_INFO encryption={ENC.get(enc, enc)} compression={COMP.get(comp, comp)}")
        elif key == 0x000183FF:
            print(f"  ORIGINAL_PE_NAME {d[4:].split(b'\0')[0].decode()}")
        elif key == 0x000200FF:
            for o in range(4, size, 16):
                nm = d[o:o + 8].split(b"\0")[0].decode()
                ma, mi, bu, qf = struct.unpack_from(">HHHH", d, o + 8)
                print(f"  STATIC_LIB {nm:10s} {ma}.{mi}.{bu}.{qf & 0xFF}")
        elif key == 0x000103FF:
            str_size, nstr = u32(d, 4), u32(d, 8)
            strs = [s.decode() for s in d[12:12 + str_size].split(b"\0") if s]
            o = 12 + str_size
            while o < size:
                lsize = u32(d, o)
                ver, vmin = u32(d, o + 28), u32(d, o + 32)
                nidx, nrec = u16(d, o + 36), u16(d, o + 38)
                fv = lambda v: f"{v >> 28}.{(v >> 24) & 0xF}.{(v >> 8) & 0xFFFF}.{v & 0xFF}"
                print(f"  IMPORT_LIB {strs[nidx]:12s} version={fv(ver)} min={fv(vmin)} records={nrec}")
                o += lsize
        elif key == 0x00020104:
            print(f"  TLS_INFO slots={u32(d,0)} raw_addr=0x{u32(d,4):08X} data_size=0x{u32(d,8):X} raw_size=0x{u32(d,12):X}")
        else:
            print(f"  {name} ({size} bytes @0x{val:X})")
    s = b[sec_off:]
    img_size, img_flags, load = u32(s, 4), u32(s, 0x10C), u32(s, 0x110)
    region, media_types, npage = u32(s, 0x178), u32(s, 0x17C), u32(s, 0x180)
    print(f"security: image_size=0x{img_size:X} load_address=0x{load:08X} image_flags=0x{img_flags:08X} "
          f"region=0x{region:08X} allowed_media=0x{media_types:08X} page_descriptors={npage}")
    kinds = {1: "code", 2: "data", 3: "rodata"}
    page = 0x1000 if img_flags & 0x10000000 else 0x10000  # XEX2 small-pages flag
    addr, runs = load, []
    for i in range(npage):
        info = u32(s, 0x184 + i * 24)
        pages, kind = info >> 4, info & 0xF
        if runs and runs[-1][2] == kind:
            runs[-1][1] += pages * page
        else:
            runs.append([addr, pages * page, kind])
        addr += pages * page
    for a, sz, k in runs:
        print(f"  section {kinds.get(k, k):6s} 0x{a:08X}-0x{a + sz:08X} ({sz / 1048576:.2f} MB)")


if __name__ == "__main__":
    main(sys.argv[1])
