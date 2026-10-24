#!/usr/bin/env python3
"""The SM-T733 importer copies external vendor files and stock recovery trees.

The caller supplies the pinned Aloha checkout and unpacked Samsung DTB/DTBO.
The platform's uefipil.cfg and USB INF files remain owned by this repository.
The firmware build generates the patched USB binaries after this import.
"""

import argparse
from pathlib import Path
import shutil


ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--aloha", type=Path, required=True)
    parser.add_argument("--recovery-dtb", type=Path, required=True)
    parser.add_argument("--recovery-dtbo", type=Path, required=True)
    args = parser.parse_args()

    source = args.aloha.resolve() / "Platforms/KodiakPkg/Device/samsung-gts7fewifi"
    vendor = ROOT / "Platforms/Samsung/gts7fewifiPkg/Vendor"
    mappings = (
        ("Binaries/QcomPkg", "QcomPkg", ()),
        ("Binaries/RawFiles", "RawFiles", ("uefipil.cfg",)),
        ("PatchedBinaries", "PatchedBinaries", ("UsbMsdDxe.inf", "UsbfnDwc3Dxe.inf")),
    )
    for original, destination, preserved in mappings:
        shutil.copytree(source / original, vendor / destination, dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns(*preserved))

    dtbs = ROOT / "Resources/DTBs"
    dtbs.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source / "DeviceTreeBlob/Android/android-gts7fewifi.dtb",
                    dtbs / "gts7fewifi-community.dtb")
    shutil.copyfile(args.recovery_dtb, dtbs / "gts7fewifi.dtb")
    shutil.copyfile(args.recovery_dtbo, dtbs / "gts7fewifi-recovery.dtbo")
    print("Imported SM-T733 vendor inputs and recovery device trees")


if __name__ == "__main__":
    main()
