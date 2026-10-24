# SM-T733 UEFI release structure

The publication fork is
[RigoLigoRLC/unixnu-mu-silicium](https://github.com/RigoLigoRLC/unixnu-mu-silicium).
Its adaptation and default branch is `unixnu-sm-t733`, created from
`42fe563791e51bf5500c2e470ed294ca0c94b97c` on 2026-10-24. The description names
SM-T733. The local `origin` points to this fork and `upstream` retains
Project-Silicium/Mu-Silicium. This branch publishes the platform source and its
build and packaging entry points.

This repository builds the Samsung SM-T733 firmware and its BOOT and recovery
packages. The UniXNU device repository owns ADT generation, XNU configuration,
system images and FAT contents. The UniXNU source checkout supplies UniXNUPkg
and DarwinBoot to the firmware build.

## Independent build inputs

`Resources/Scripts/build_gts7fewifi_macos.sh [DEBUG|RELEASE] [TAG]` uses
`UNIXNU_TREE` when set, otherwise `UNIXNU_BASE`. Either path must contain
`package/UniXNUPkg` and `drivers/DarwinBoot/DarwinBootPkg`. An assembled UniXNU
tree provides device-selected modules through its existing symbolic links.
The SM-T733 device workflow passes the assembled tree so its DarwinBoot override
is also used by firmware builds. A bare upstream checkout is suitable only when
it already contains the matching loader changes.
The entry does not derive a UniXNU path from the firmware repository's parent.

The tool environment supplies Python 3.11 or newer with `coloredlogs`, the Mu
build dependencies, GNU make, Clang and LLVM tools for CLANGPDB. `PYTHON`
selects the Python executable; its default is `python3`. `CLANG` and
`LLVM_OBJCOPY` select the executables for USB patch generation and BootShim;
their defaults are `clang` and `llvm-objcopy` on PATH. Each variable names one
executable, including an absolute path when needed.

The firmware entry generates both USB recovery binaries from the preserved
vendor inputs before building BaseTools and firmware. It then applies the six
Mu patches, builds BootShim, builds the platform FD and packages the base
Android recovery image. Hardware configuration and binary patch contents are
unchanged by this entry arrangement.

Example, from this repository:

```sh
export UNIXNU_BASE=/path/to/UniXNU
export UNIXNU_RECORD_HASHES=0
export LLVM_OBJCOPY=/path/to/llvm-objcopy
export T733_USERDATA_PAYLOAD=1
bash Resources/Scripts/build_gts7fewifi_macos.sh DEBUG release-tag

export T733_STOCK_RECOVERY=/path/to/T733ZCS9DYE1/recovery.img
bash Resources/Scripts/package_gts7fewifi_recovery.sh \
    /path/to/recovery-payload-64m.img release-tag
```

`T733_USERDATA_PAYLOAD` defaults to zero. A system firmware build sets it to
one to enable the existing SHNGROOT userdata FAT loader. Recovery packaging
requires an explicit 64 MiB FAT argument and `T733_STOCK_RECOVERY`; it does not
locate device outputs or original firmware through a parent workspace. Relative
input paths are relative to the invoking shell's working directory. Packaging
requires a new output directory and keeps the UEFI and original recovery tar
files together under `Build/gts7fewifiPkg/recovery-packages-TAG/`.

Normal BOOT packaging already receives every input explicitly:

```sh
python3 Resources/Scripts/package_gts7fewifi_boot.py \
    --recovery-image Mu-gts7fewifi-release-tag.img \
    --stock-boot /path/to/T733ZCS9DYE1/boot.img \
    --output-dir Build/gts7fewifiPkg/boot-packages-release-tag
```

It retains the original header-v3 BOOT ramdisk, uses the newly built firmware
kernel and fills the 96 MiB BOOT partition. The matching vendor_boot and dtbo
remain external device inputs. Recovery uses the existing header-v2 format,
Samsung section-end marker, FAT at 8 MiB and full 78 MiB partition size.

`compose_gts7fewifi_userdata.py ROOT_IMAGE BOOT_FAT OUTPUT` receives its inputs
explicitly. It appends a 512 MiB FAT after the two SHNGROOT system windows and
updates the required SHNGROOT header digest. The device build owns root-image,
FAT and persistent Data production. This protocol digest is part of the image
format, separate from optional file-confirmation records.

## Source files that accompany the release

Preserve the entire `Platforms/Samsung/gts7fewifiPkg/` platform: DSC/FDF,
DeviceBuild.py, Include fragments, MemoryMapLib, ConfigurationMapLib,
PlatformSecLib, RecoveryPayloadDxe and UserdataPayload. Preserve
`BootShim/BootShim.S`, `build_uefi.py`, `Resources/Configs/gts7fewifi.toml`,
`Resources/MuPatches/` and the T733 build, packaging and composition scripts.

The firmware uses UniXNUPkg menu changes and DarwinBoot capacity/handoff changes
from the accompanying UniXNU source versions. The release must identify these
versions or supply their patches; the original submodule gitlink alone does not
contain uncommitted DarwinBoot edits.

The two USB source generators are:

- `Resources/Scripts/patch_gts7fewifi_usb_msd.py`, producing
  `Vendor/PatchedBinaries/UsbMsdDxe.efi` and its DEPEX.
- `Resources/Scripts/patch_gts7fewifi_usb_dwc.py`, producing
  `Vendor/PatchedBinaries/UsbfnDwc3Dxe.efi` and its DEPEX.

Both contain their complete inserted assembly and consume the corresponding
original `Vendor/QcomPkg/Drivers/` EFI and DEPEX. Preserve their patched INF
descriptors. `Build/gts7fewifi-usb-*-patch/` contains generated assembly, objects
and raw code; the generators are the source to publish. Other imported patched
drivers, including UFSDxe, are supplied by the pinned community input below.

The separate Aloha comparison workflow is outside this platform release.

## Vendor and original-firmware inputs

The imported vendor baseline is [qaz6750/mu_aloha_platforms](https://github.com/qaz6750/mu_aloha_platforms), branch
`xyz/sm7325/gts7fewifi`, commit
`9e6cb5f1249d18bab50fecb614b4928f6ed55c8b`. Preserve its source attribution and
input acquisition instructions alongside these mappings:

| This repository | Aloha source |
| --- | --- |
| `Platforms/Samsung/gts7fewifiPkg/Vendor/QcomPkg` | `Platforms/KodiakPkg/Device/samsung-gts7fewifi/Binaries/QcomPkg` |
| `Platforms/Samsung/gts7fewifiPkg/Vendor/PatchedBinaries` imported baseline | `Platforms/KodiakPkg/Device/samsung-gts7fewifi/PatchedBinaries` |
| `Platforms/Samsung/gts7fewifiPkg/Vendor/RawFiles` baseline | `Platforms/KodiakPkg/Device/samsung-gts7fewifi/Binaries/RawFiles` |
| `Resources/DTBs/gts7fewifi-community.dtb` | `Platforms/KodiakPkg/Device/samsung-gts7fewifi/DeviceTreeBlob/Android/android-gts7fewifi.dtb` |

The platform's `RawFiles/uefipil.cfg` preserves its empty AUTO list; replacing
the complete directory with the unmodified input would restore automatic ABL
loading. `Include/Vendor.inc` selects the locally generated USB modules.

The primary recovery configuration uses Samsung CHN `T733ZCS9DYE1` inputs:

- `Resources/DTBs/gts7fewifi.dtb`: original recovery DTB, 466624 bytes.
- `Resources/DTBs/gts7fewifi-recovery.dtbo`: original recovery DTBO, 758813 bytes.
- Original `recovery.img`: supplied by `T733_STOCK_RECOVERY` for the restore tar.
- Original `boot.img`: supplied to `package_gts7fewifi_boot.py` for header v3 and
  ramdisk input.

Vendor EFI, DEPEX, raw configuration and original firmware inputs need an
explicit distribution or acquisition route. Do not omit them merely because
generated FD and tar products are excluded from Git. The runtime FDT includes
device identifiers and Android state; it is not the boot-image DTB input.

## Import procedure and source notices

Clone the external inputs into the ignored cache and select the recorded
revision before importing. The importer copies QcomPkg, the community patched
drivers and RawFiles while preserving this branch's uefipil.cfg and USB INF
descriptors. It performs no file-confirmation hashing.

```sh
mkdir -p .cache
git clone --branch xyz/sm7325/gts7fewifi https://github.com/qaz6750/mu_aloha_platforms.git .cache/aloha
git -C .cache/aloha checkout 9e6cb5f1249d18bab50fecb614b4928f6ed55c8b
git clone https://android.googlesource.com/platform/system/tools/mkbootimg .cache/mkbootimg
git -C .cache/mkbootimg checkout d2bb0af5ba6d3198a3e99529c97eda1be0b5a093
```

Obtain the Samsung SM-T733 CHN firmware release `T733ZCS9DYE1`. Extract
`recovery.img.lz4` and `boot.img.lz4` from its `AP_*.tar.md5` archive into an
ignored local directory, then decompress them using `lz4 -d`. The original
recovery image supplies both device trees:

```sh
python3 .cache/mkbootimg/unpack_bootimg.py \
  --boot_img /path/to/recovery.img --out .cache/stock-recovery
python3 Resources/Scripts/import_gts7fewifi_inputs.py \
  --aloha .cache/aloha \
  --recovery-dtb .cache/stock-recovery/dtb \
  --recovery-dtbo .cache/stock-recovery/recovery_dtbo
```

Run the firmware entry above after importing. It creates the two patched USB
EFI modules from the original binaries. Reimporting replaces imported inputs;
the three source-controlled local configuration files remain intact. Keep the
original boot and recovery images for the explicit packaging arguments.

The community input is attributed to qaz6750 and Project Aloha; Qualcomm and
Samsung retain the rights to their firmware. Downloading that input does not
relicense its contents under this repository's source license. The vendor
EFI, DEPEX, raw data and Samsung device-tree binaries are external inputs and
are excluded from this source publication. Existing source license headers
remain attached to the platform, BootShim, Mu patches and tools. The local
uefipil.cfg is retained as the platform's edited loader configuration, with its
original header. AOSP's unpacking tool remains in its own checkout with its
license. USB patch generators contain the inserted assembly; their generated
binary outputs stay under the ignored Vendor and Build paths.

## Submodule baselines

These are the gitlink baselines inspected during release preparation. Recursive
dependencies follow each pinned submodule's own gitlinks.

| Path | Repository | Revision |
| --- | --- | --- |
| `Binaries` | Project-Silicium/Device-Binaries | `a1501e0c2e642d6ac0d8f52c85af2a67b75a7799` |
| `Common/Mu` | microsoft/mu_plus | `b8d46c71610318e36a661819e1e3c0b8bdda413a` |
| `Common/Mu_OEM_Sample` | microsoft/mu_oem_sample | `b70b4b7bb02c43bf9db2172dfb3ed0457e2361d6` |
| `Mu_Basecore` | microsoft/mu_basecore | `3c9c7b2e211e62d2a954ad760a36fe05adc43aa3` |
| `Silicium-ACPI` | Project-Silicium/Silicium-ACPI | `1c8bd63a7ffcaecf3c8764a6b01a11e053457543` |
| `Silicon/Silicium/OpensslPkg/Library/OpensslLib/openssl` | openssl/openssl | `67b5686b4419b4cb8caa502711c41815f5279751` |

Mu modifications are represented by `AsmMacroLib.patch`, `Auth-Service.patch`,
`Boot-Manager.patch`, `Timer.patch`, `Usb-Bus.patch` and
`Dxe-Ufs-Diagnostics.patch`. The last patch includes `T733UfsDispatch.h`.

This source publication keeps the existing submodule gitlinks. Build outputs,
Conf, Python bytecode and historical failure images stay local. The source
generators and source patches above are release inputs. No firmware build,
test or deployment was performed for this publication.

The development workspace's `tools/build-t733-uefi-update.sh` supplies
`UNIXNU_BASE`, defaults `UNIXNU_TREE` to that checkout, and supplies
`T733_STOCK_RECOVERY`. Explicit environment values override these defaults.
Its build-uefi log includes both USB generation steps because the firmware
entry owns them. The workspace wrapper retains its existing recovery payload,
BOOT/recovery output directories and per-stage status log; it is not required
by the independent commands above.
