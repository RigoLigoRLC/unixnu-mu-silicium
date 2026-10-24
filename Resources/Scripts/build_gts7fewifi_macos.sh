#!/bin/bash
set -euo pipefail

uefi_root=$(cd "$(dirname "$0")/../.." && pwd)
build_target=${1:-DEBUG}
artifact_tag=${2:-}
unixnu_tree=${UNIXNU_TREE:-${UNIXNU_BASE:-}}
python_command=${PYTHON:-python3}
clang_command=${CLANG:-clang}
objcopy_command=${LLVM_OBJCOPY:-llvm-objcopy}

if [ -z "$unixnu_tree" ]; then
    printf 'Set UNIXNU_TREE to an assembled UniXNU tree or UNIXNU_BASE to its source checkout.\n' >&2
    exit 1
fi

unixnu_tree=$(cd "$unixnu_tree" && pwd)
if [ ! -d "$unixnu_tree/package/UniXNUPkg" ] || \
   [ ! -d "$unixnu_tree/drivers/DarwinBoot/DarwinBootPkg" ]; then
    printf 'UniXNU package and DarwinBoot sources are required in %s\n' "$unixnu_tree" >&2
    exit 1
fi

cd "$uefi_root"

export WORKSPACE="$uefi_root"
export EDK_TOOLS_PATH="$uefi_root/Mu_Basecore/BaseTools"
export EDK_TOOLS_BIN="$EDK_TOOLS_PATH/Source/C/bin"
export PACKAGES_PATH="$uefi_root/Platforms/Samsung:$uefi_root/Common/Mu:$uefi_root/Common/Mu_OEM_Sample:$uefi_root/Mu_Basecore:$uefi_root/Silicon/Qualcomm:$uefi_root/Silicon/Samsung:$uefi_root/Silicon/Silicium:$uefi_root/Silicium-ACPI:$unixnu_tree/package:$unixnu_tree/drivers/DarwinBoot"
export PATH="$EDK_TOOLS_BIN:$EDK_TOOLS_PATH/BinWrappers/PosixLike:$PATH"

"$python_command" Resources/Scripts/patch_gts7fewifi_usb_msd.py \
    --clang "$clang_command" --objcopy "$objcopy_command"
"$python_command" Resources/Scripts/patch_gts7fewifi_usb_dwc.py \
    --clang "$clang_command" --objcopy "$objcopy_command"

make -C "$EDK_TOOLS_PATH/Source/C" -j8
mkdir -p Conf

for template_name in build_rule tools_def target; do
    if [ ! -f "Conf/$template_name.txt" ]; then
        cp "$EDK_TOOLS_PATH/Conf/$template_name.template" "Conf/$template_name.txt"
    fi
done

for patch_name in AsmMacroLib Auth-Service Boot-Manager Timer Usb-Bus Dxe-Ufs-Diagnostics; do
    patch_file="$uefi_root/Resources/MuPatches/$patch_name.patch"
    git -C Mu_Basecore apply --reverse "$patch_file" >/dev/null 2>&1 || true
    git -C Mu_Basecore apply "$patch_file"
done

make -C BootShim CC="$clang_command" OBJCOPY="$objcopy_command" \
    REQUIRES_KERNEL_HEADER=1 FD_BASE=0x9fc41000 FD_SIZE=0x2bf000

build -a AARCH64 -t CLANGPDB -b "$build_target" \
    -p Platforms/Samsung/gts7fewifiPkg/gts7fewifi.dsc \
    -D ENABLE_SECUREBOOT=0 -D FD_BASE=0x9fc41000 \
    -D FD_SIZE=0x2bf000 -D FD_BLOCKS=0x2bf -D DEVICE_MODEL=0 \
    -D MEMORY_PROTECTION=TRUE -D SHIP_MODE=FALSE -D BUILDID_STRING=Unknown \
    -D T733_USERDATA_PAYLOAD="${T733_USERDATA_PAYLOAD:-0}"

"$python_command" - "$build_target" "$artifact_tag" <<'PY'
import sys

import build_uefi

build_uefi.setup_logger()
context = build_uefi.BuildContext(
    device="gts7fewifi",
    device_model=0,
    build_mode=sys.argv[1],
    enable_secureboot=False,
    cleanup=False,
    update=False,
)
config = build_uefi.parse_device_config(context.device)
if sys.argv[2]:
    config["boot_image"]["output_name"] = f"Mu-gts7fewifi-{sys.argv[2]}.img"

if not build_uefi.create_output_image(context, config, True):
    raise SystemExit(1)
PY

if [ -n "$artifact_tag" ]; then
    mkdir -p "Build/gts7fewifiPkg/$artifact_tag"
    cp "Build/gts7fewifiPkg/${build_target}_CLANGPDB/FV/SILICIUM_UEFI.fd" \
        "Build/gts7fewifiPkg/$artifact_tag/SILICIUM_UEFI.fd"
fi
