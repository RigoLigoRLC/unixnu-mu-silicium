#!/usr/bin/env python3
"""Compose the T733 recovery image and its FAT payload for RecoveryPayloadDxe."""

import argparse
from pathlib import Path
import shutil


PAYLOAD_OFFSET = 0x00800000
PAYLOAD_SIZE = 0x04000000
RECOVERY_PARTITION_SIZE = 0x04E00000
SAMSUNG_TRAILER = b"SEANDROIDENFORCE"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("recovery", type=Path)
    parser.add_argument("payload", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    recovery = args.recovery.read_bytes()
    if not recovery.endswith(SAMSUNG_TRAILER):
        parser.error("the input recovery image must end with SEANDROIDENFORCE")

    if len(recovery) > PAYLOAD_OFFSET:
        parser.error("the recovery header and sections exceed the 8 MiB payload offset")

    if args.payload.stat().st_size != PAYLOAD_SIZE:
        parser.error("RecoveryPayloadDxe requires a 64 MiB FAT image")

    with args.output.open("wb") as output:
        output.write(recovery)
        output.write(bytes(PAYLOAD_OFFSET - len(recovery)))

        with args.payload.open("rb") as payload:
            shutil.copyfileobj(payload, output)

        # The original trailer remains immediately after the Android sections.
        # Fill the partition tail so no footer from a prior image survives.
        output.write(bytes(RECOVERY_PARTITION_SIZE - output.tell()))

    print(f"Recovery with FAT payload: {args.output}")


if __name__ == "__main__":
    main()
