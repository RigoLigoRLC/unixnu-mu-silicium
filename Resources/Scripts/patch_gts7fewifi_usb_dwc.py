#!/usr/bin/env python3
"""Generate the T733 vendor DWC bulk STARTTRANSFER completion patch."""

import argparse
from pathlib import Path
import shutil
import subprocess


ROOT = Path(__file__).resolve().parents[2]
VENDOR = ROOT / "Platforms/Samsung/gts7fewifiPkg/Vendor"

ASSEMBLY = r"""
.text
.org 0x9dac
    b bulk_start_command
.org 0x9db0
command_resume:
.org 0x9e50
    b bulk_start_completion
.org 0x9e54
synchronous_completion:
.org 0x9e74
    b command_wait_result
.org 0x9e78
command_read_result:
.org 0x9ea8
    b failed_start_command
.org 0x9eac
failed_start_resume:
.org 0x9ee0
submit_return:
.org 0xa040
    b save_start_resource
.org 0xa044
save_started_state:
.org 0xa458
event_return:
.org 0xa714
    b finish_pending_start
.org 0xa718
transfer_event_resume:
.org 0xbefc
debug_print:
.org 0x15e10
bulk_start_command:
    orr w9, w10, w9
    // Bulk START uses DEPCMD completion, so no later command event owns it.
    cmp w19, #2
    b.eq clear_command_interrupt
    cmp w19, #3
    b.ne command_resume
clear_command_interrupt:
    bic w9, w9, #0x100
    b command_resume

bulk_start_completion:
    cmp w19, #2
    b.eq synchronous_completion
    cmp w19, #3
    b.eq synchronous_completion
    tbz w23, #31, synchronous_bulk_start
    b submit_return
synchronous_bulk_start:
    b synchronous_completion

command_wait_result:
    // The existing DEPCMD wait must finish before status/resource are read.
    tbnz w0, #31, command_wait_failed
    ldr w8, [x20]
    b command_read_result
command_wait_failed:
    // Hardware still owns this request. Return accepted so the outer submit
    // links the URB into its active list instead of recycling its storage.
    // A transfer event can retire START; Reset/Detach can cancel the request.
    adr x1, pending_message
    mov w0, #0x80000000
    bl debug_print
    mov w22, wzr
    b submit_return

failed_start_command:
    // CMDACT is clear and status is nonzero: no transfer was started.
    str xzr, [x25, #0x28]
    mov w8, #0xff
    strb w8, [x25, #0x1c]
    mov w8, #1
    str w8, [x24]
    mov w8, #2
    b failed_start_resume

save_start_resource:
    // The synchronous path reads the resource from the completed DEPCMD.
    // DwcSubmitURB needs it for UPDATE; AbortTransfer needs it for END.
    ubfx w8, w0, #16, #7
    strb w8, [x25, #0x1c]
    mov w8, #2
    b save_started_state

finish_pending_start:
    ldr w8, [x24, #0x18]!
    cmp w8, #4
    b.ne transfer_event_resume
    cmp w20, #2
    b.eq read_pending_command
    cmp w20, #3
    b.ne transfer_event_resume
read_pending_command:
    // The event belongs to the retained URB. Check the hardware command
    // before the original code reads TRBs and reports a completion upstream.
    ldr w11, [x21]
    mov w12, #0xc80c
    add w11, w11, w20, lsl #4
    add w11, w11, w12
    dmb sy
    ldr w11, [x11]
    dmb sy
    tst w11, #0x400
    b.ne pending_command_unfinished
    tst w11, #0xf000
    b.ne pending_command_unfinished
    ubfx w12, w11, #16, #7
    strb w12, [x26, #0x1c]
    mov w8, #2
    str w8, [x24]
    b transfer_event_resume
pending_command_unfinished:
    // An error return prevents the outer pump from removing the active URB.
    mov w22, #6
    movk w22, #0x8000, lsl #16
    b event_return
pending_message:
    .asciz "T733 DWC START timeout: request retained until transfer or bus reset\n"
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--clang", default="clang")
    parser.add_argument("--objcopy", default="llvm-objcopy")
    args = parser.parse_args()

    source = VENDOR / "QcomPkg/Drivers/UsbfnDwc3Dxe/UsbfnDwc3Dxe.efi"
    data = bytearray(source.read_bytes())
    expected = {
        0x9dac: "4901092a",
        0x9e50: "9704f837",
        0x9e74: "880240b9",
        0x9ea8: "48008052",
        0xa040: "48008052",
        0xa714: "088f41b8",
    }
    for offset, value in expected.items():
        original = bytes.fromhex(value)
        if data[offset:offset + len(original)] != original:
            raise SystemExit(f"Unsupported T733 DWC instruction bytes at {offset:#x}")

    if len(data) != 0x18000 or any(data[0x15e10:0x16000]):
        raise SystemExit("Unsupported vendor DWC image size or occupied code space")

    work = ROOT / "Build/gts7fewifi-usb-dwc-patch"
    work.mkdir(parents=True, exist_ok=True)
    assembly = work / "start-completion.S"
    assembly.write_text(ASSEMBLY)
    obj = work / "start-completion.o"
    raw = work / "start-completion.bin"
    subprocess.run([args.clang, "--target=aarch64-none-elf", "-c", str(assembly), "-o", str(obj)], check=True)
    subprocess.run([args.objcopy, "-O", "binary", "--only-section=.text", str(obj), str(raw)], check=True)
    patch = raw.read_bytes()
    if len(patch) > 0x16000:
        raise SystemExit("DWC STARTTRANSFER code exceeds reserved space")
    for offset in expected:
        data[offset:offset + 4] = patch[offset:offset + 4]
    data[0x15e10:len(patch)] = patch[0x15e10:]

    output = VENDOR / "PatchedBinaries"
    output.mkdir(parents=True, exist_ok=True)
    (output / "UsbfnDwc3Dxe.efi").write_bytes(data)
    shutil.copyfile(source.with_suffix(".depex"), output / "UsbfnDwc3Dxe.depex")
    print(f"T733 DWC bulk STARTTRANSFER image: {output / 'UsbfnDwc3Dxe.efi'}")


if __name__ == "__main__":
    main()
