#!/usr/bin/env python3
"""Generate the synthetic plugin fixtures used by test_esp.cpp.

    python3 make_fixtures.py [output-dir]

The output is deterministic; the generated files are committed next to this
script. Every file is a Skyrim SE style plugin: a lone TES4 record whose data
holds HEDR, CNAM, SNAM and MAST sub-records.
"""
import struct
import sys
from pathlib import Path

FLAG_MASTER = 0x1
FLAG_LIGHT = 0x200  # ESL


def subrecord(kind: bytes, payload: bytes) -> bytes:
    assert len(kind) == 4 and len(payload) < 0x10000
    return kind + struct.pack("<H", len(payload)) + payload


def tes4(flags: int, masters, author: str, description: str, num_records: int) -> bytes:
    data = subrecord(b"HEDR", struct.pack("<fiI", 1.7, num_records, 0x800))
    data += subrecord(b"CNAM", author.encode() + b"\0")
    data += subrecord(b"SNAM", description.encode() + b"\0")
    for master in masters:
        data += subrecord(b"MAST", master.encode() + b"\0")
        data += subrecord(b"DATA", b"\0" * 8)  # every MAST is followed by an 8 byte DATA
    # type, data size, flags, form id, version control info, form version, unknown
    header = struct.pack("<4sIIIIHH", b"TES4", len(data), flags, 0, 0, 44, 0)
    return header + data


def main() -> None:
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent
    out.mkdir(parents=True, exist_ok=True)
    (out / "light.esp").write_bytes(
        tes4(FLAG_LIGHT, ["Skyrim.esm"], "mo2 test", "synthetic light plugin", 3))
    (out / "master.esm").write_bytes(
        tes4(FLAG_MASTER, [], "mo2 test", "synthetic master", 0))
    (out / "garbage.esp").write_bytes(b"NOPE" + b"\0" * 28)


if __name__ == "__main__":
    main()
