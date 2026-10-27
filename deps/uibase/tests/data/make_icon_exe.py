#!/usr/bin/env python3
"""Regenerates icon.exe, the tiny PE executable used by test_linux_utility.cpp (PeIcon).

It is a minimal PE32+ file with only a .rsrc section holding RT_GROUP_ICON id 1 with two
RT_ICON images (ids 1 and 2): 16x16 and 32x32, 32-bit solid-colour PNGs. No toolchain
is needed (version.exe, in contrast, needs MinGW). The output is deterministic.

Usage: python3 -I make_icon_exe.py   (writes icon.exe next to this script)
"""
import os
import struct
import zlib


def png(size, rgba):
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    row = b"\x00" + bytes(rgba) * size
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(row * size, 9))
            + chunk(b"IEND", b""))


def align(n, a):
    return (n + a - 1) & ~(a - 1)


RT_ICON, RT_GROUP_ICON = 3, 14
RVA = 0x1000          # virtual address of .rsrc
RAW = 0x200           # file offset of .rsrc

images = [(1, 16, png(16, (255, 0, 0, 255))), (2, 32, png(32, (0, 0, 255, 255)))]

group = struct.pack("<HHH", 0, 1, len(images))
for ident, size, data in images:
    # GRPICONDIRENTRY: width, height, colours, reserved, planes, bits, bytes, id
    group += struct.pack("<BBBBHHIH", size, size, 0, 0, 1, 32, len(data), ident)

# (type, name id, payload); resources are laid out as type -> name -> language 1033
resources = [(RT_ICON, i, d) for i, _, d in images] + [(RT_GROUP_ICON, 1, group)]
types = sorted({t for t, _, _ in resources})


def dir_table(entries):
    """entries: list of (id, target offset, is_subdir)"""
    out = struct.pack("<IIHHHH", 0, 0, 0, 0, 0, len(entries))
    for ident, target, sub in entries:
        out += struct.pack("<II", ident, target | (0x80000000 if sub else 0))
    return out


# sizes first, then fill in offsets
root_size = 16 + 8 * len(types)
name_dirs = {t: sorted(n for tt, n, _ in resources if tt == t) for t in types}
off = root_size
name_off = {}
for t in types:
    name_off[t] = off
    off += 16 + 8 * len(name_dirs[t])
lang_off = {}
for t in types:
    for n in name_dirs[t]:
        lang_off[(t, n)] = off
        off += 16 + 8
entry_off = {}
for t in types:
    for n in name_dirs[t]:
        entry_off[(t, n)] = off
        off += 16
data_start = align(off, 4)

blob = dir_table([(t, name_off[t], True) for t in types])
for t in types:
    blob += dir_table([(n, lang_off[(t, n)], True) for n in name_dirs[t]])
for t in types:
    for n in name_dirs[t]:
        blob += dir_table([(1033, entry_off[(t, n)], False)])
payload = b""
data_entries = b""
pos = data_start
for t in types:
    for n in name_dirs[t]:
        d = next(p for tt, nn, p in resources if (tt, nn) == (t, n))
        data_entries += struct.pack("<IIII", RVA + pos, len(d), 0, 0)
        padded = d + b"\0" * (align(len(d), 4) - len(d))
        payload += padded
        pos += len(padded)
blob += data_entries
blob += b"\0" * (data_start - len(blob))
blob += payload

virtual_size = len(blob)
raw_size = align(len(blob), 0x200)
blob += b"\0" * (raw_size - len(blob))

opt_size = 112 + 16 * 8
pe_off = 0x40
dos = b"MZ" + b"\0" * 58 + struct.pack("<I", pe_off)
coff = b"PE\0\0" + struct.pack("<HHIIIHH", 0x8664, 1, 0, 0, 0, opt_size, 0x0022)

opt = bytearray(opt_size)
struct.pack_into("<HBB", opt, 0, 0x20B, 14, 0)
struct.pack_into("<I", opt, 8, raw_size)               # SizeOfInitializedData
struct.pack_into("<Q", opt, 24, 0x140000000)           # ImageBase
struct.pack_into("<II", opt, 32, 0x1000, 0x200)        # Section/FileAlignment
struct.pack_into("<HHHHHH", opt, 40, 6, 0, 0, 0, 6, 0)  # OS/image/subsystem versions
struct.pack_into("<I", opt, 56, RVA + align(raw_size, 0x1000))  # SizeOfImage
struct.pack_into("<I", opt, 60, RAW)                   # SizeOfHeaders
struct.pack_into("<H", opt, 68, 3)                     # Subsystem: console
struct.pack_into("<I", opt, 108, 16)                   # NumberOfRvaAndSizes
struct.pack_into("<II", opt, 112 + 2 * 8, RVA, virtual_size)  # resource directory

section = struct.pack("<8sIIIIIIHHI", b".rsrc", virtual_size, RVA, raw_size, RAW, 0,
                      0, 0, 0, 0x40000040)

header = dos + coff + bytes(opt) + section
header += b"\0" * (RAW - len(header))

out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "icon.exe")
with open(out, "wb") as f:
    f.write(header + blob)
os.chmod(out, 0o644)
print("wrote %s (%d bytes)" % (out, len(header) + len(blob)))
