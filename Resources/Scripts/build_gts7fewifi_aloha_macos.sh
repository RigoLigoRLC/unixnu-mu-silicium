#!/bin/bash
set -euo pipefail

uefi_root=$(cd "$(dirname "$0")/../.." && pwd)
workspace_root=$(cd "$uefi_root/../.." && pwd)
aloha_root="$workspace_root/references/mu_aloha_platforms"
build_target=${1:-RELEASE}
crypto_root="MU_BASECORE/CryptoPkg/Binaries/crypto"

cd "$aloha_root"
export WORKSPACE="$aloha_root"
export EDK_TOOLS_PATH="$aloha_root/MU_BASECORE/BaseTools"
export EDK_TOOLS_BIN="$EDK_TOOLS_PATH/Source/C/bin"
export PACKAGES_PATH="$aloha_root/Platforms:$aloha_root/MU_BASECORE:$aloha_root/Common/MU:$aloha_root/Common/MU_TIANO:$aloha_root/Common/MU_OEM_SAMPLE:$aloha_root/Silicon/Arm/MU_TIANO:$aloha_root/Features/DFCI:$aloha_root/Features/CONFIG:$aloha_root/Binaries:$aloha_root/Silicon/QC/Sm7325"
export PATH="$EDK_TOOLS_BIN:$EDK_TOOLS_PATH/BinWrappers/PosixLike:$PATH"

make -C "$EDK_TOOLS_PATH/Source/C" -j8
mkdir -p Conf

for template_name in build_rule tools_def target; do
    if [ ! -f "Conf/$template_name.txt" ]; then
        cp "$EDK_TOOLS_PATH/Conf/$template_name.template" "Conf/$template_name.txt"
    fi
done

generated_dir="$aloha_root/Platforms/AndromedaPkg/Include/Generated"
mkdir -p "$generated_dir"
(
    cd "$generated_dir"
    python "$aloha_root/Features/CONFIG/SetupDataPkg/Tools/KnobService.py" \
        generateheader_efi \
        "$aloha_root/Platforms/AndromedaPkg/CfgData/AndromedaPkgCfgData.xml" \
        ConfigClientGenerated.h ConfigServiceGenerated.h ConfigDataGenerated.h
)

make -C BootShim UEFI_BASE=0x9fc41000 UEFI_SIZE=0x2bf000

build -a AARCH64 -t CLANGPDB -b "$build_target" \
    -p Platforms/KodiakPkg/KodiakNoSb.dsc \
    -D TARGET_DEVICE=samsung-gts7fewifi -D FDT=gts7fewifi.dtb \
    -D SHARED_CRYPTO_PATH="$crypto_root" \
    -D MEMORY_PROTECTION=TRUE -D SHIP_MODE=FALSE -D BUILDID_STRING=Unknown

python - "$build_target" <<'PY'
import logging
from pathlib import Path
import sys

root = Path.cwd()
sys.path.insert(0, str(root / "Platforms/KodiakPkg/PythonLibs"))
import PostBuild

logging.basicConfig(level=logging.INFO)
output = root / "Build/KodiakPkg/original-gts7fewifi"
output.mkdir(parents=True, exist_ok=True)
PostBuild.makeAndroidImage(
    str(root / f"Build/KodiakPkg/{sys.argv[1]}_CLANGPDB"),
    str(output),
    str(root),
    "samsung-gts7fewifi",
    "gts7fewifi.dtb",
)
PY
