#!/usr/bin/env python3
"""Generate the synthetic archive fixture used by test_bsa.cpp.

    python3 make_fixtures.py [output-dir]

Writes skyrimse.bsa, a Skyrim SE (version 105) archive, plus the original
contents of its files under files/ so the tests can compare extracted bytes.
The output is deterministic; the generated files are committed next to this
script. The archive holds

    textures\\a.dds          stored uncompressed
    textures\\actors\\b.dds   stored uncompressed (nested folder)
    meshes\\c.nif            LZ4 frame compressed (per-file compression toggle)

Name hashes follow bsatk's filehash.cpp (the algorithm the format uses), so
bsatk can validate them. Only the standard library is needed: the LZ4 frame
is assembled by hand around a block that contains one real match sequence.
"""
import struct
import sys
from pathlib import Path

ARCHIVE_VERSION = 105
FLAG_HAS_DIR_NAMES = 0x1
FLAG_HAS_FILE_NAMES = 0x2
COMPRESS_TOGGLE = 0x40000000  # file record size bit: flip the archive default
FILE_FLAG_NIF = 0x1
FILE_FLAG_DDS = 0x2


def hash_int(data: bytes) -> int:
    h = 0
    for b in data:
        h = (h * 0x1003F + b) & 0xFFFFFFFF
    return h


def bsa_hash(name: str) -> int:
    """Port of calculateBSAHash() (bsatk filehash.cpp)."""
    n = name.lower().replace("/", "\\").encode("ascii")
    dot = n.rfind(b".")
    ext = n[dot:] if dot >= 0 else b""
    length = len(n) - len(ext)
    h1 = 0
    if length > 0:
        h1 = (n[length - 1]
              | ((n[length - 2] if length > 2 else 0) << 8)
              | (length << 16)
              | (n[0] << 24))
    if ext:
        h1 |= {b".kf": 0x80, b".nif": 0x8000, b".dds": 0x8080,
               b".wav": 0x80000000}.get(ext, 0)
        middle = hash_int(n[1:length - 2]) if length - 2 > 1 else 0
        h2 = (middle + hash_int(ext)) & 0xFFFFFFFF
        h1 |= h2 << 32
    return h1


def xxh32(data: bytes, seed: int = 0) -> int:
    p1, p2, p3, p4, p5 = 2654435761, 2246822519, 3266489917, 668265263, 374761393
    m = 0xFFFFFFFF

    def rotl(x, r):
        return ((x << r) | (x >> (32 - r))) & m

    i, n = 0, len(data)
    if n >= 16:
        v = [(seed + p1 + p2) & m, (seed + p2) & m, seed & m, (seed - p1) & m]
        while i <= n - 16:
            for k in range(4):
                lane = struct.unpack_from("<I", data, i + 4 * k)[0]
                v[k] = (rotl((v[k] + lane * p2) & m, 13) * p1) & m
            i += 16
        h = (rotl(v[0], 1) + rotl(v[1], 7) + rotl(v[2], 12) + rotl(v[3], 18)) & m
    else:
        h = (seed + p5) & m
    h = (h + n) & m
    while i + 4 <= n:
        h = (rotl((h + struct.unpack_from("<I", data, i)[0] * p3) & m, 17) * p4) & m
        i += 4
    while i < n:
        h = (rotl((h + data[i] * p5) & m, 11) * p1) & m
        i += 1
    h ^= h >> 15
    h = (h * p2) & m
    h ^= h >> 13
    h = (h * p3) & m
    h ^= h >> 16
    return h


def lz4_length_tail(extra: int) -> bytes:
    out = bytearray()
    while extra >= 255:
        out.append(255)
        extra -= 255
    out.append(extra)
    return bytes(out)


def lz4_block(prefix: bytes, unit: bytes, repeats: int) -> bytes:
    """LZ4 block for prefix + unit * repeats (len(unit) == 4): one literal run
    (prefix and the first unit), one overlapping match (offset 4), then the 8
    trailing literals the format requires."""
    assert len(unit) == 4 and repeats >= 6
    literals = prefix + unit
    trailing = unit * 2
    match_len = 4 * (repeats - 1) - len(trailing)
    token = (min(len(literals), 15) << 4) | min(match_len - 4, 15)
    block = bytes([token])
    if len(literals) >= 15:
        block += lz4_length_tail(len(literals) - 15)
    block += literals + struct.pack("<H", 4)
    if match_len - 4 >= 15:
        block += lz4_length_tail(match_len - 4 - 15)
    return block + bytes([len(trailing) << 4]) + trailing


def lz4_frame(prefix: bytes, unit: bytes, repeats: int) -> bytes:
    flg, bd = 0x60, 0x40              # version 1, independent blocks, 64 KiB max block
    descriptor = bytes([flg, bd])
    header = struct.pack("<I", 0x184D2204) + descriptor + bytes([(xxh32(descriptor) >> 8) & 0xFF])
    block = lz4_block(prefix, unit, repeats)
    return header + struct.pack("<I", len(block)) + block + struct.pack("<I", 0)


COMPRESSED_PARTS = (b"Gamebryo File Format ", b"ABCD", 64)


def fake_dds(seed: int, size: int) -> bytes:
    body = bytes((seed * 31 + i * 7) & 0xFF for i in range(size - 4))
    return b"DDS " + body


def build_bsa(entries):
    """entries: list of (folder, file name, content, compressed)."""
    folders = {}
    for folder, name, content, compressed in entries:
        folders.setdefault(folder, []).append((name, content, compressed))

    folder_list = sorted(folders, key=bsa_hash)
    for folder in folder_list:
        folders[folder].sort(key=lambda f: bsa_hash(f[0]))

    all_files = [f for folder in folder_list for f in folders[folder]]
    folder_names_len = sum(len(f) + 1 for f in folder_list)
    file_names_len = sum(len(f[0]) + 1 for f in all_files)

    folder_block_start = 36 + 24 * len(folder_list)
    blocks, offsets, pos = [], {}, folder_block_start
    for folder in folder_list:
        offsets[folder] = pos + file_names_len  # SE: offset of the name block + file names length
        size = 1 + len(folder) + 1 + 16 * len(folders[folder])
        blocks.append(size)
        pos += size
    data_start = pos + file_names_len

    # payloads: SE compressed data = original size (u32) + LZ4 frame
    payloads = {}
    cursor = data_start
    for folder in folder_list:
        for name, content, compressed in folders[folder]:
            if compressed:
                prefix, unit, repeats = COMPRESSED_PARTS
                assert content == prefix + unit * repeats
                blob = struct.pack("<I", len(content)) + lz4_frame(prefix, unit, repeats)
            else:
                blob = content
            payloads[(folder, name)] = (cursor, blob)
            cursor += len(blob)

    out = bytearray()
    file_flags = FILE_FLAG_NIF | FILE_FLAG_DDS
    out += struct.pack("<4sIIIIIIII", b"BSA\0", ARCHIVE_VERSION, 36,
                       FLAG_HAS_DIR_NAMES | FLAG_HAS_FILE_NAMES,
                       len(folder_list), len(all_files),
                       folder_names_len, file_names_len, file_flags)
    for folder in folder_list:
        out += struct.pack("<QIIQ", bsa_hash(folder), len(folders[folder]), 0, offsets[folder])
    for folder in folder_list:
        out += bytes([len(folder) + 1]) + folder.encode() + b"\0"
        for name, content, compressed in folders[folder]:
            offset, blob = payloads[(folder, name)]
            size = len(blob) | (COMPRESS_TOGGLE if compressed else 0)
            out += struct.pack("<QII", bsa_hash(name), size, offset)
    for name, _, _ in all_files:
        out += name.encode() + b"\0"
    assert len(out) == data_start
    for folder in folder_list:
        for name, _, _ in folders[folder]:
            out += payloads[(folder, name)][1]
    return bytes(out)


def main() -> None:
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent
    contents = {
        ("textures", "a.dds"): (fake_dds(1, 200), False),
        ("textures\\actors", "b.dds"): (fake_dds(2, 96), False),
        ("meshes", "c.nif"): (COMPRESSED_PARTS[0] + COMPRESSED_PARTS[1] * COMPRESSED_PARTS[2], True),
    }
    for (folder, name), (content, _) in contents.items():
        target = out / "files" / folder.replace("\\", "/") / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(content)
    entries = [(f, n, c, z) for (f, n), (c, z) in contents.items()]
    (out / "skyrimse.bsa").write_bytes(build_bsa(entries))


if __name__ == "__main__":
    main()
