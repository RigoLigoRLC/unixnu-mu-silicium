#!/usr/bin/env python3
"""Append a boot FAT to a separate copy of the T733 SHNGROOT system image."""

import argparse
import hashlib
from pathlib import Path
import shutil
import struct


MIB = 1 << 20
FAT_SIZE = 512 * MIB
USERDATA_SIZE = 117878796288


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root_image", type=Path)
    parser.add_argument("boot_fat", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    with args.root_image.open("rb") as source:
        header = bytearray(source.read(4096))

    if len(header) != 4096 or header[:8] != b"SHNGROOT":
        parser.error("root_image must contain a SHNGROOT header")

    version, count, image_bytes = struct.unpack_from("<IIQ", header, 8)
    if version != 1 or count != 2 or image_bytes != args.root_image.stat().st_size:
        parser.error("root_image must be an unextended SHNGROOT v1 image with two windows")

    highest_end = MIB
    names = []
    for index in range(count):
        entry = 24 + index * 128
        name, offset, size, block_size, flags = struct.unpack_from("<32sQQII", header, entry)
        names.append(name.split(b"\0", 1)[0])
        if (offset < MIB or offset % 4096 or not size or size % 512 or
                offset >= image_bytes or size > image_bytes - offset or
                block_size != 512 or flags != 1):
            parser.error("root window has unsupported geometry")

        highest_end = max(highest_end, offset + size)

    if set(names) != {b"OS", b"SystemOS"}:
        parser.error("root_image must contain the OS and SystemOS windows")

    fat_offset = (highest_end + MIB - 1) // MIB * MIB
    if fat_offset != image_bytes:
        parser.error("root_image must end at the aligned end of its system windows")

    with args.boot_fat.open("rb") as source:
        bpb = source.read(512)

    if (len(bpb) != 512 or bpb[510:512] != b"\x55\xaa" or
            bpb[71:82] != b"T733ESP    " or bpb[82:90] != b"FAT32   " or
            struct.unpack_from("<H", bpb, 11)[0] != 512 or
            struct.unpack_from("<I", bpb, 32)[0] * 512 != FAT_SIZE or
            args.boot_fat.stat().st_size != FAT_SIZE):
        parser.error("boot_fat must be a 512 MiB T733ESP FAT32 image with 512-byte sectors")

    combined_bytes = fat_offset + FAT_SIZE
    if combined_bytes + 4 * MIB > USERDATA_SIZE:
        parser.error("combined image leaves no room for the native storage guard")

    struct.pack_into("<Q", header, 16, combined_bytes)
    # SHNGROOT v1 requires this digest of its header; the window digests stay intact.
    header[4064:4096] = hashlib.sha256(header[:4064]).digest()

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("xb") as target:
        target.write(header)
        with args.root_image.open("rb") as source:
            source.seek(4096)
            shutil.copyfileobj(source, target, 8 * MIB)

        with args.boot_fat.open("rb") as source:
            shutil.copyfileobj(source, target, 8 * MIB)

    print(f"Combined userdata image: {args.output.resolve()}")
    print(f"FAT offset: {fat_offset:#x}; FAT size: {FAT_SIZE:#x}; image_bytes: {combined_bytes:#x}")


if __name__ == "__main__":
    main()
