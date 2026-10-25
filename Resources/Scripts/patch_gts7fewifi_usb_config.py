#!/usr/bin/env python3
"""Remove the vendor timer's MSC dispatch from the imported USB platform driver."""

from pathlib import Path
import struct


def main() -> None:
    root = Path(__file__).resolve().parents[2]
    binary = root / "Platforms/Samsung/gts7fewifiPkg/Vendor/PatchedBinaries/UsbConfigDxe.efi"
    data = bytearray(binary.read_bytes())

    # The 100 ms platform callback calls the old MSC event handler ten times.
    # The MassStorage application owns all event dispatch for the source driver.
    offset = 0x47D4
    original = struct.pack("<I", 0x9400048D)
    replacement = struct.pack("<I", 0xD503201F)
    instruction = data[offset:offset + 4]

    if instruction == replacement:
        return

    if instruction != original:
        raise SystemExit("UsbConfigDxe MSC timer call does not match the SM-T733 input")

    data[offset:offset + 4] = replacement
    binary.write_bytes(data)
    print("UsbConfigDxe: disabled vendor timer MSC dispatch")


if __name__ == "__main__":
    main()
