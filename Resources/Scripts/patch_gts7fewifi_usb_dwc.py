#!/usr/bin/env python3
"""Generate the T733 vendor DWC pending-command event handling patch."""

import argparse
from pathlib import Path
import shutil
import subprocess


ROOT = Path(__file__).resolve().parents[2]
VENDOR = ROOT / "Platforms/Samsung/gts7fewifiPkg/Vendor"

ASSEMBLY = r"""
.text
.org 0x4e24
pump_event:
.org 0x56c8
    b retire_pending_command
.org 0x56cc
submit_resume:
.org 0x57f4
submit_return:
.org 0xbefc
debug_print:
.org 0x15e10
retire_pending_command:
    // This hook precedes TRB preparation and the new URB's active-list insertion.
    // The event pump retains ordinary USB messages for the protocol consumer.
    cmp w1, #1
    b.ne submit_original
    cmp w2, #1
    b.hi submit_original
    add w9, w2, w1, lsl #1
    add x10, x0, x9, lsl #5
    ldr w10, [x10, #0x208]
    cmp w10, #4
    b.ne submit_original
    sub sp, sp, #96
    stp x0, x1, [sp]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    str x30, [sp, #64]
    str wzr, [sp, #72]
    str w9, [sp, #80]
    ldrb w10, [x0, #0x18a]
    str w10, [sp, #88]
pending_event:
    ldr x0, [sp]
    bl pump_event
    ldr w9, [sp, #72]
    add w9, w9, #1
    str w9, [sp, #72]
    ldr x10, [sp]
    ldrb w12, [x10, #0x18a]
    ldr w11, [sp, #80]
    add x10, x10, x11, lsl #5
    ldr w3, [x10, #0x208]
    ldr w10, [sp, #88]
    cmp w12, w10
    b.ne pending_result
    cmp w3, #4
    b.ne pending_result
    cmp w9, #128
    b.lo pending_event
pending_result:
    // The original submit path reports reset, stall, and timeout failures.
    // Only a consumed command-completion event changes the endpoint state.
    mov w4, w9
    sub w10, w3, #1
    cmp w10, #1
    cset w10, hi
    ldr w11, [sp, #88]
    cmp w12, w11
    csinc w10, w10, wzr, eq
    str w10, [sp, #84]
    ldr w2, [sp, #80]
    adr x1, pending_message
    mov w0, #0x80000000
    bl debug_print
    ldp x0, x1, [sp]
    ldp x2, x3, [sp, #16]
    ldp x4, x5, [sp, #32]
    ldp x6, x7, [sp, #48]
    ldr x30, [sp, #64]
    ldr w9, [sp, #84]
    add sp, sp, #96
    cbz w9, submit_original
    mov x0, #7
    movk x0, #0x8000, lsl #48
    b submit_return
submit_original:
    ldr x8, [x19, #0x450]
    b submit_resume
pending_message:
    .asciz "T733 DWC pending START: ep=%u state=%u polls=%u\n"
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--clang", default="clang")
    parser.add_argument("--objcopy", default="llvm-objcopy")
    args = parser.parse_args()

    source = VENDOR / "QcomPkg/Drivers/UsbfnDwc3Dxe/UsbfnDwc3Dxe.efi"
    data = bytearray(source.read_bytes())
    expected = {
        0x4e24: "ff4302d1",
        0x56c8: "682a42f9",
        0x9e44: "88008052351700f9080300b9",
        0xa6e0: "48008052480100b9",
    }
    for offset, value in expected.items():
        original = bytes.fromhex(value)
        if data[offset:offset + len(original)] != original:
            raise SystemExit(f"Unsupported T733 DWC instruction bytes at {offset:#x}")

    if len(data) != 0x18000 or any(data[0x15e10:0x16000]):
        raise SystemExit("Unsupported vendor DWC image size or occupied code space")

    work = ROOT / "Build/gts7fewifi-usb-dwc-patch"
    work.mkdir(parents=True, exist_ok=True)
    assembly = work / "pending.S"
    assembly.write_text(ASSEMBLY)
    obj = work / "pending.o"
    raw = work / "pending.bin"
    subprocess.run([args.clang, "--target=aarch64-none-elf", "-c", str(assembly), "-o", str(obj)], check=True)
    subprocess.run([args.objcopy, "-O", "binary", "--only-section=.text", str(obj), str(raw)], check=True)
    patch = raw.read_bytes()
    if len(patch) > 0x16000:
        raise SystemExit("DWC pending-command code exceeds reserved space")
    data[0x56c8:0x56cc] = patch[0x56c8:0x56cc]
    data[0x15e10:len(patch)] = patch[0x15e10:]

    output = VENDOR / "PatchedBinaries"
    output.mkdir(parents=True, exist_ok=True)
    (output / "UsbfnDwc3Dxe.efi").write_bytes(data)
    shutil.copyfile(source.with_suffix(".depex"), output / "UsbfnDwc3Dxe.depex")
    print(f"T733 DWC pending-command image: {output / 'UsbfnDwc3Dxe.efi'}")


if __name__ == "__main__":
    main()
