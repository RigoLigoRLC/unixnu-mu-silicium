#!/usr/bin/env python3
"""Generate the T733 vendor MSC reset and bulk-completion recovery patch."""

import argparse
from pathlib import Path
import shutil
import struct
import subprocess


ROOT = Path(__file__).resolve().parents[2]
VENDOR = ROOT / "Platforms/Samsung/gts7fewifiPkg/Vendor"

# The stub runs inside the original dispatcher's frame. x19 owns its device
# context and w20 retains the old state for the original transition code.
ASSEMBLY = r"""
.text
.org 0x209c
dispatch_success:
.org 0x20c8
    b recovery_dispatch
.org 0x20d0
dispatch_resume:
.org 0x2120
dispatch_transition:
.org 0x364c
cancel_transfer:
.org 0xa200
recovery_dispatch:
    ldr w20, [x0, #0x360]
    mov x19, x0
    cmp w1, #4
    b.eq recover_bus
    cmp w1, #6
    b.eq recover_bus
    cmp w1, #2
    b.eq bulk_event
    cmp w1, #3
    b.ne dispatch_resume
bulk_event:
    ldrb w9, [x3, #12]
    cbz w9, dispatch_resume
    cmp w9, #1
    b.ne dispatch_success
    cmp w20, #5
    b.lo dispatch_success
    ldr w9, [x3, #8]
    cmp w9, #1
    b.ne dispatch_success
    cmp w1, #2
    b.ne transmit_event
    cmp w20, #5
    b.eq cbw_event
    cmp w20, #7
    b.ne dispatch_success
    ldr x9, [x19, #0x448]
    ldr x10, [x19, #0x458]
    add x9, x9, x10
    b check_buffer
transmit_event:
    cmp w20, #6
    b.eq transmit_data
    cmp w20, #8
    b.ne dispatch_success
    ldr x9, [x19, #0x4c0]
    ldr x10, [x19, #0x4d0]
    add x9, x9, x10
    b check_buffer
transmit_data:
    ldr x9, [x19, #0x470]
    ldr x10, [x19, #0x480]
    add x9, x9, x10
    b check_buffer
cbw_event:
    ldr x9, [x19, #0x498]
    ldr x10, [x19, #0x4a8]
    add x9, x9, x10
    ldr x10, [x3, #24]
    cmp x9, x10
    b.ne dispatch_success
    ldr w9, [x3, #8]
    cmp w9, #1
    b.ne dispatch_success
    ldr x9, [x3]
    cmp x9, #31
    b.ne invalid_cbw
    b dispatch_resume
check_buffer:
    // IssueXfer submits Buffer + BytesTransferred for each remaining segment.
    // DWC reports the submitted segment's TRB buffer, not the allocation base.
    ldr x10, [x3, #24]
    cmp x9, x10
    b.ne dispatch_success
    b dispatch_resume
invalid_cbw:
    // An invalid CBW has no CSW. Both bulk endpoints wait for BOT reset.
    str wzr, [x19, #0x494]
    str xzr, [x19, #0x4a8]
    ldr x0, [x19, #0x368]
    mov w1, #1
    mov w2, #0
    mov w3, #1
    ldr x8, [x0, #0x40]
    blr x8
    ldr x0, [x19, #0x368]
    mov w1, #1
    mov w2, #1
    mov w3, #1
    ldr x8, [x0, #0x40]
    blr x8
    b dispatch_success
recover_bus:
    // DWC has already canceled its URBs before publishing the bus event.
    // These records retain only MSC bookkeeping for the previous connection.
    str wzr, [x19, #0x41c]
    str wzr, [x19, #0x444]
    str wzr, [x19, #0x46c]
    str wzr, [x19, #0x494]
    str wzr, [x19, #0x4bc]
    str xzr, [x19, #0x430]
    str xzr, [x19, #0x458]
    str xzr, [x19, #0x480]
    str xzr, [x19, #0x4a8]
    str xzr, [x19, #0x4d0]
    strb wzr, [x19, #0x37c]
    str wzr, [x19, #0x378]
    cmp w1, #6
    mov w0, #2
    csel w0, w0, wzr, eq
    b dispatch_transition
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--clang", default="clang")
    parser.add_argument("--objcopy", default="llvm-objcopy")
    args = parser.parse_args()

    source = VENDOR / "QcomPkg/Drivers/UsbMsdDxe/UsbMsdDxe.efi"
    data = bytearray(source.read_bytes())
    expected = {
        0x20c8: "146043b9f30300aa",
        0x211c: "00013fd6",
        0x2210: "fd7bbea9f30b00f9",
        0x300c: "880a40b91f050071",
        0x364c: "fd7bbea9f44f01a9",
    }
    for offset, value in expected.items():
        original = bytes.fromhex(value)
        if data[offset:offset + len(original)] != original:
            raise SystemExit(f"Unsupported T733 MSC instruction bytes at {offset:#x}")

    # Existing state pointers are relocated by the firmware loader. The stub
    # uses only PC-relative branches and original protocol pointers.
    for state in range(5, 9):
        for column in (6, 8):
            if struct.unpack_from("<Q", data, 0x77e0 + state * 96 + column * 8)[0] != 0x21b0:
                raise SystemExit("Unsupported vendor MSC state table")
    if len(data) != 0xd000 or any(data[0xa200:0xb000]):
        raise SystemExit("Unsupported vendor MSC image size or occupied code space")

    work = ROOT / "Build/gts7fewifi-usb-msd-patch"
    work.mkdir(parents=True, exist_ok=True)
    assembly = work / "recovery.S"
    assembly.write_text(ASSEMBLY)
    obj = work / "recovery.o"
    raw = work / "recovery.bin"
    subprocess.run([args.clang, "--target=aarch64-none-elf", "-c", str(assembly), "-o", str(obj)], check=True)
    subprocess.run([args.objcopy, "-O", "binary", "--only-section=.text", str(obj), str(raw)], check=True)
    patch = raw.read_bytes()
    if len(patch) > 0xb000:
        raise SystemExit("MSC recovery code exceeds reserved space")
    data[0x20c8:0x20cc] = patch[0x20c8:0x20cc]
    data[0xa200:len(patch)] = patch[0xa200:]

    output = VENDOR / "PatchedBinaries"
    output.mkdir(parents=True, exist_ok=True)
    (output / "UsbMsdDxe.efi").write_bytes(data)
    shutil.copyfile(source.with_suffix(".depex"), output / "UsbMsdDxe.depex")
    print(f"T733 MSC recovery image: {output / 'UsbMsdDxe.efi'}")


if __name__ == "__main__":
    main()
