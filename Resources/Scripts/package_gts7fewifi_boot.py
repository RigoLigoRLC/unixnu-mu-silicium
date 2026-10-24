#!/usr/bin/env python3
"""Put the built T733 BootShim and FD into a separate stock-header-v3 BOOT package."""

import argparse
from pathlib import Path
import struct
import tarfile


PAGE_SIZE = 4096
BOOT_PARTITION_SIZE = 96 * 1024 * 1024
SAMSUNG_TRAILER = b"SEANDROIDENFORCE"


def align_page(size):
    return (size + PAGE_SIZE - 1) // PAGE_SIZE * PAGE_SIZE


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--recovery-image", required=True, type=Path,
                        help="tagged Mu-gts7fewifi image supplying the built gzip BootShim and FD")
    parser.add_argument("--stock-boot", required=True, type=Path,
                        help="matching T733ZCS9DYE1 boot.img supplying its v3 header and ramdisk")
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()

    with args.recovery_image.open("rb") as source:
        recovery_header = source.read(PAGE_SIZE)
        if (len(recovery_header) != PAGE_SIZE or recovery_header[:8] != b"ANDROID!" or
                struct.unpack_from("<I", recovery_header, 40)[0] != 2 or
                struct.unpack_from("<I", recovery_header, 36)[0] != PAGE_SIZE):
            parser.error("recovery-image must be the built T733 header-v2 image")

        kernel_size = struct.unpack_from("<I", recovery_header, 8)[0]
        if not kernel_size or kernel_size > BOOT_PARTITION_SIZE - PAGE_SIZE:
            parser.error("recovery-image kernel size is outside the BOOT partition")

        kernel = source.read(kernel_size)

    if len(kernel) != kernel_size or kernel[:2] != b"\x1f\x8b":
        parser.error("recovery-image must contain the gzip BootShim and FD kernel")

    with args.stock_boot.open("rb") as source:
        header = bytearray(source.read(PAGE_SIZE))
        if (len(header) != PAGE_SIZE or header[:8] != b"ANDROID!" or
                struct.unpack_from("<I", header, 40)[0] != 3 or
                struct.unpack_from("<I", header, 20)[0] != 1580):
            parser.error("stock-boot must contain the matching Android boot header v3")

        stock_kernel_size, ramdisk_size = struct.unpack_from("<II", header, 8)
        ramdisk_offset = PAGE_SIZE + align_page(stock_kernel_size)
        if (not ramdisk_size or ramdisk_offset > args.stock_boot.stat().st_size or
                ramdisk_size > args.stock_boot.stat().st_size - ramdisk_offset):
            parser.error("stock-boot ramdisk is outside its image")

        source.seek(ramdisk_offset)
        ramdisk = source.read(ramdisk_size)

    if len(ramdisk) != ramdisk_size:
        parser.error("stock-boot ramdisk read was incomplete")

    image_end = PAGE_SIZE + align_page(kernel_size) + align_page(ramdisk_size)
    if image_end + len(SAMSUNG_TRAILER) > BOOT_PARTITION_SIZE:
        parser.error("BOOT header, kernel, ramdisk and Samsung trailer exceed 96 MiB")

    struct.pack_into("<I", header, 8, kernel_size)
    args.output_dir.mkdir(parents=True, exist_ok=False)
    image_path = args.output_dir / "boot.img"
    with image_path.open("xb") as target:
        target.write(header)
        target.write(kernel)
        target.write(bytes(align_page(kernel_size) - kernel_size))
        target.write(ramdisk)
        target.write(bytes(align_page(ramdisk_size) - ramdisk_size))
        target.write(SAMSUNG_TRAILER)
        # A full partition image clears the old AVB footer after replacing the kernel.
        target.write(bytes(BOOT_PARTITION_SIZE - target.tell()))

    package = args.output_dir / "gts7fewifi-uefi-boot.tar"
    with tarfile.open(package, "x", format=tarfile.USTAR_FORMAT) as archive:
        archive.add(image_path, arcname="boot.img", recursive=False)

    print(f"UEFI BOOT package: {package.resolve()}")
    print("The existing matching vendor_boot and dtbo partitions supply the v3 device tree inputs.")


if __name__ == "__main__":
    main()
