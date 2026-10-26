# SM-T733 UEFI implementation

Date: 2026-10-22.

## Implemented source integration

`Platforms/Samsung/gts7fewifiPkg` is the Mu-Silicium platform for Samsung Galaxy Tab S7 FE Wi-Fi, SM-T733, SM7325. `Resources/Configs/gts7fewifi.toml` selects it through the existing `build_uefi.py -d gts7fewifi` entry point.

The platform uses `KodiakPkg` for the GIC, the virtual timer and SM7325 errata, with a T733 `PlatformSecLib` for early CPU and GIC initialization. Its `DeviceBuild.py` provides the package paths for this checkout plus the sibling `UniXNU/package` and `UniXNU/drivers/DarwinBoot` directories. The platform DSC and FDF include `UniXNU`, `UniXNUDxe` and `DarwinBoot` in the same firmware volume. `UniXNUDxe` registers the application in `BootOrder`; the common Mu BDS starts that application. The T733 platform has no device ACPI table input. DarwinBoot receives the device ADT from the payload prepared by the UniXNU device repository.

`Library/MemoryMapLib` uses the current Mu memory descriptor API, Aloha's firmware reservations and fixed reserved regions from the selected matching stock DT. The firmware layout is:

| Region | Base | Size |
| --- | --- | --- |
| Space preceding the UEFI FD | `0x9FC00000` | `0x41000` |
| UEFI FD | `0x9FC41000` | `0x2BF000` |
| SEC heap | `0x9FF00000` | `0x8C000` |
| CPU vectors | `0x9FF8C000` | `0x1000` |
| MMU tables | `0x9FF8D000` | `0x3000` |
| Primary stack | `0x9FF90000` | `0x40000` |
| Qualcomm log buffer | `0x9FFF7000` | `0x8000` |
| Qualcomm information block | `0x9FFFF000` | `0x1000` |
| Display splash reservation | `0xE1000000` | `0x2300000` |
| Display DFPS data | `0xE3300000` | `0x100000` |

The FD base and size come from `references/mu_aloha_platforms/build_cfg/sm7325.json`. The device config passes them to both BootShim and the FDF. The memory table names the matching FD range. `RamManagerDxe` obtains RAM bank ranges from the Qualcomm RAM partition protocol and adds uncovered available portions. The platform does not assign RAM capacity from Android `MemTotal`. The T733 TGCM reservation at `0x9DC00000` is retained. GCC is mapped from the reference `yupik.dtsi`. The UFS subsystem occupies the full `0x01D80000/0x20000` window requested by the vendor HWIO driver, including the host, PHY, ICE and ICE HWKM registers.

`Library/ConfigurationMapLib` exposes the T733 Qualcomm configuration entries through the current Mu API. Display threading and Qualcomm multithreading are disabled according to the T733 reference. `UEFIExitUfsSSURequired` is `1`, matching that reference. The imported patched UFSDxe has its ExitBootServices callback registration at RVA `0x4CD8` replaced with NOP, so this configuration value does not establish that this Mu image issues POWERDOWN at exit. NativeUfs retains its explicit SSU ACTIVE sequence when taking over storage.

`Include/Vendor.inc` integrates the T733 Qualcomm drivers and the reference's patched UFS, SD, USB configuration, feature enabler and watchdog binaries. `Include/DXE.inc` adds standard Disk I/O, GPT partition discovery, FAT access, USB keyboard support and the Mu GOP driver. `Include/APRIORI.inc` orders the low level Qualcomm services before storage, RAM management and GOP. The platform uses the reference's original physical ButtonsDxe input driver.

The GOP setup uses display base `0xE1000000` and `1600 x 2560`, 32 bits per pixel. The runtime DT confirms the splash reservation base and its `0x2300000` size. Android's first rooted boot log reports `hactive=1600, vactive=2560, fps=60`, continuous splash mode, and release of that exact splash buffer. The connected panel is `BOE_876270`; the runtime boot arguments select `ss_dsi_panel_FT8203_TS124QDM_WQXGA`. Mu's SimpleFbDxe uses 1600 pixels per scan line and `PixelBlueGreenRedReserved8BitPerColor` for this depth. Stride and pixel format remain community parameters requiring a UEFI boot. Android has no `/sys/class/graphics/fb0` on this firmware. DarwinBoot obtains the framebuffer from the running GOP instead of storing an Android framebuffer address in its ADT.

The matching firmware DTB index 0 plus recovery DTBO entry 1 has been merged at `sources/UniXNU-gts7fewifi/.cache/firmware/device-tree/T733ZCS9DYE1/selected-stock.dts` under the workspace root. Its `/memory/reg` contains zero values. Its fixed reservations add `QRTR Shared` at `0xD7EF7000`, the two Neuron channels at `0xD7F00000` and `0xD7F80000`, Samsung debug memory at `0xF0000000/0x900000`, Samsung PLog at `0xF0900000/0x200000` and ramoops at `0xF0B00000/0x100000`. The memory map publishes these as reserved before `RamManagerDxe` adds available RAM. It also reserves the `0x808FF000` APSS security-data page. The DT's splash and DFPS reservations occupy separate memory descriptors.

## Runtime memory input

The rooted device's FDT is saved at `backups/sm-t733-runtime-2026-10-22/binary/running-device-tree.dtb` under the workspace root. Its decoded source is `sources/UniXNU-gts7fewifi/.cache/firmware/device-tree/runtime-2026-10-22/running-device-tree.dts`. The runtime `/memory/reg` contains these ranges:

| Base | Length | End, exclusive |
| --- | --- | --- |
| `0x80000000` | `0x3A800000` | `0xBA800000` |
| `0xBAE00000` | `0x01E00000` | `0xBCC00000` |
| `0xC0000000` | `0x40000000` | `0x100000000` |
| `0x100000000` | `0x100000000` | `0x200000000` |

These ranges span a 6 GiB address interval starting at `0x80000000`, with gaps at `0xBA800000/0x600000` and `0xBCC00000/0x3400000`. Both gaps are represented by reserved HOBs without MMU cache mappings. Samsung TIMA at `0xB0100000/0x100000`, UH heap at `0xB0200000/0x200000`, and UH guest memory at `0xB0400000/0x1200000` have individual reserved descriptors. The rest of Aloha's `0xB0000000..0xC0000000` HYP reservation is retained for its vendor drivers. The runtime HDM page at `0x9FB00000` is covered by the ABOOT reservation; the KASLR page at `0xA0001000` is covered by the UCSI reservation; the runtime dummy reservation at `0x9C700000/0x100000` is covered by PIL Reserved.

Android's `proc/iomem` is recorded at `backups/sm-t733-runtime-2026-10-22/text/proc/iomem.txt`. It confirms the UFS and SMMU registers and the full TLMM register window at `0x0F000000/0x1000000`, which the platform now maps. Linux kernel and CMA allocations shown in this capture belong to that Android boot. The firmware retains its own allocation map and obtains RAM bank ranges from the Qualcomm protocol. DarwinBoot chooses its contiguous allocation from the resulting UEFI map; the 6 GiB address interval is not a contiguous free allocation.

The display evidence above comes from `backups/sm-t733-root-2026-10-22/text/dmesg.txt`, including the initial continuous splash handoff. The recovery image continues to embed the original matching firmware DTB and DTBO. The captured running FDT contains Android boot arguments, initrd addresses and private device identifiers, so it is an input to platform configuration rather than a replacement boot image DTB.

## Imported inputs and provenance

All imported vendor assets are copied from the already downloaded local Aloha checkout, branch `xyz/sm7325/gts7fewifi`, commit `9e6cb5f1249d18bab50fecb614b4928f6ed55c8b`:

| Destination | Reference input |
| --- | --- |
| `Platforms/Samsung/gts7fewifiPkg/Vendor/QcomPkg` | `Platforms/KodiakPkg/Device/samsung-gts7fewifi/Binaries/QcomPkg` |
| `Platforms/Samsung/gts7fewifiPkg/Vendor/PatchedBinaries` | `Platforms/KodiakPkg/Device/samsung-gts7fewifi/PatchedBinaries` |
| `Platforms/Samsung/gts7fewifiPkg/Vendor/RawFiles` | `Platforms/KodiakPkg/Device/samsung-gts7fewifi/Binaries/RawFiles` |
| `Resources/DTBs/gts7fewifi-community.dtb` | `Platforms/KodiakPkg/Device/samsung-gts7fewifi/DeviceTreeBlob/Android/android-gts7fewifi.dtb` |

These vendor binaries and the `gts7fewifi-community.dtb` file are community inputs. The connected device reports CHN firmware `T733ZCS9DYE1`, Android 14 / One UI 6.1, hardware revision 2, sub-PCB revision 3, `dtb_idx=0`, and `dtbo_idx=1`. The vendor binaries have not been identified as binaries extracted from this firmware.

The default DT assets come from the downloaded matching Samsung firmware:

| Destination | Matching firmware input under the workspace root |
| --- | --- |
| `Resources/DTBs/gts7fewifi.dtb` | `downloads/samsung/SM-T733-CHN-T733ZCS9DYE1/unpacked/recovery/dtb`, 466624 bytes |
| `Resources/DTBs/gts7fewifi-recovery.dtbo` | `downloads/samsung/SM-T733-CHN-T733ZCS9DYE1/unpacked/recovery/recovery_dtbo`, 758813 bytes |

Image header extraction is recorded in `logs/unpack-stock-recovery.log`, `logs/unpack-stock-boot.log` and `logs/unpack-stock-vendor_boot.log`. The connected device's PIT is saved as `backups/sm-t733-2026-10-22/device.pit`, with decoded fields in `info/device-pit.txt` alongside it.

## Image packaging

`gts7fewifi.toml` selects the matching stock recovery format: header version 2, page size 4096, base 0, board name `SRPUF17A009`, OS version 11.0.0 and patch level 2025-03. These OS fields are copied from the original image header. BootShim plus FD is gzip-compressed as in the T733 Aloha entry. The original DTB is supplied as the dedicated DTB section and the original recovery DTBO as its dedicated section. The kernel does not contain an appended DTB.

The image addresses are kernel `0x8000`, ramdisk `0x2000000`, tags `0x1E00000` and DTB `0x1F00000`. The recovery command line comes from the matching image. `build_uefi.py` forwards board name, page size, recovery DTBO and zero-valued fields to mkbootimg. Mkbootimg computes the new recovery DTBO offset from the newly generated kernel and ramdisk sizes. The original `0x395C000` DTBO offset belongs to the stock image. The configured Samsung trailer appends `SEANDROIDENFORCE`, following the T733 recovery packaging source in `references/android_device_samsung_gts7fewifi-twrp/mkbootimg.mk`.

This configuration describes a recovery image. The device's stock boot uses header version 3 and its stock vendor_boot uses version 3 with a separate DTB. Both the PIT and the rooted device's GPT capture report the recovery partition at 4096-byte block 124400, 19968 blocks, or 81788928 bytes. The boot partition starts at block 99824 with 24576 blocks; vendor_boot starts at block 144368 with 24576 blocks. The GPT is saved in `backups/sm-t733-gpt-2026-10-22`, including `partitions.tsv`. Actual acceptance of the generated recovery image by the unlocked Samsung loader remains a device boot task.

The DEBUG build completed on 2026-10-22 and generated `Build/gts7fewifiPkg/DEBUG_CLANGPDB/FV/SILICIUM_UEFI.fd` and the recovery image `Mu-gts7fewifi.img`. The firmware build reported a `0x2BF000`-byte volume, `0x1239C0` bytes used and `0x19B640` bytes free, or 41% used. The image packaging command completed in the same successful invocation.

## macOS build entry

The local build command, run from this checkout, is:

```sh
source /Users/rigoligo/expm/unixnu-sm-t733/tools/activate.sh
bash Resources/Scripts/build_gts7fewifi_macos.sh DEBUG
```

The script compiles the C BaseTools from `Mu_Basecore/BaseTools/Source/C`, uses the existing Homebrew LLVM tools and project Python environment, applies the five Mu patches supplied by this checkout, builds BootShim and the platform, then invokes `build_uefi.py`'s recovery packager. The source BaseTools target builds the programs needed for firmware generation. It does not invoke the BaseTools test target. The platform package paths include the existing Silicium-ACPI root; the A52s configuration's two obsolete nested ACPI paths were removed from the T733 builder.

The initial `build_uefi.py` invocation reached its Stuart dependency update and stopped because its NASM NuGet package required Mono and its remote prebuilt tools were unavailable through the network sandbox. The local build entry uses the already available NASM and compiled BaseTools. Recorded command results are:

| Log under the workspace root | Result |
| --- | --- |
| `logs/build-uefi-gts7fewifi-debug.log` | Initial Stuart update stopped at host dependency resolution; BootShim had compiled |
| `logs/build-uefi-basetools.log` | Native AARCH64 macOS C BaseTools build completed with exit code 0 |
| `logs/build-uefi-gts7fewifi-native.log` | DEBUG platform build and recovery packaging completed with exit code 0; firmware build reported 18 seconds |
| `logs/build-uefi-gts7fewifi-runtime.log` | Runtime memory and TLMM update built with exit code 0; firmware build reported 11 seconds |
| `logs/build-uefi-gts7fewifi-recovery-payload.log` | RecoveryPayloadDxe and standard RamDiskDxe built with exit code 0; firmware build reported 12 seconds |
| `logs/package-uefi-gts7fewifi-recovery.log` | Recovery image with the 64 MiB FAT payload and both USTAR packages produced with exit code 0 |

The final DEBUG volume uses `0x126BE0` bytes of the `0x2BF000`-byte compact firmware volume. Its free space is `0x198420` bytes. `Mu-gts7fewifi.img` contains the firmware recovery image before the FAT is attached; deploy the composed image or USTAR package described below to include the menu and kernel.

## Existing logging path

Mu's DEBUG configuration selects `FrameBufferSerialPortLib` and `BaseDebugLibSerialPort`. Its serial output draws directly into the display reservation before the normal GOP console becomes available. The same display can show UniXNU messages and DarwinBoot startup diagnostics. Capturing the screen during an explicitly requested device boot records this output. RELEASE selects the null debug library. The Qualcomm log buffer listed above is reserved, but its presence alone does not establish a host-readable persistent logging transport. UART routing and retrieval of a saved device boot log remain unfinished.

## Recovery-only deployment preparation

### First-boot observations

The deployed DEBUG image exposes the following progress messages on the display:

| Visible message | Execution reached |
| --- | --- |
| `Project Silicium for Samsung Galaxy Tab S7 FE Wi-Fi` | BootShim copied the FD and entered SEC; `PlatformInitialize` returned |
| `Successfully Mapped RAM Range` | RamManagerDxe added an available RAM range |
| `T733: recovery FAT registered at ...` | RecoveryPayloadDxe read the appended FAT and registered the RAM disk |
| `T733: recovery FAT load returned ...` | The recovery partition was found, but the read, FAT identification or RAM disk registration failed with the displayed EFI status |
| `UNIXNU: UniXNU ... starting ...` | BDS started the UniXNU application |
| `UNIXNU: config: loaded ...` | UniXNU found and parsed a configuration file |
| `UNIXNU: config: none found, using built-in configuration` | UniXNU started without finding the packaged configuration |
| `UNIXNU: ui: graphics 1600x2560 ...` | UniXNU initialized its UI using GOP |

SEC exceptions print `Synchronous Exception at ...`, `IRQ Exception at ...`, `FIQ Exception at ...` or `SError/Abort Exception at ...` and enter a CPU dead loop. Record the address and preceding messages. BootShim has no output of its own. A screen that retains only the Samsung splash does not distinguish BootShim/SEC failure from an incorrect framebuffer address or inactive scanout; the first changed pixels and any exception text narrow the stage. The framebuffer logger clears its screen when the text reaches the bottom, so a short video preserves messages that a later photograph would miss.

The compiled `SerialPortLib` draws pixels at `0xE1000000`; it does not send bytes to a physical UART or a USB serial endpoint. UniXNU's logging writes to DEBUG and any installed Serial I/O protocol. Its current implementation does not create a filesystem log, and changes to the recovery-loaded FAT RAM disk do not persist to the recovery partition.

After returning to Android, collect the files present under `/sys/fs/pstore` and the new `dmesg` before another reboot. The runtime DT reserves ramoops at `0xF0B00000/0x100000`, with record, console, ftrace and pmsg sizes of `0x40000` each. Existing `console-ramoops-*`, `dmesg-ramoops-*` or `pmsg-ramoops-*` files can retain Linux evidence. This UEFI build has no ramoops writer, so its display messages are captured on screen. The Qualcomm log reservation `0x9FFF7000/0x8000` and information block `0x9FFFF000/0x1000` have no implemented host export path in this platform, and a subsequent Android boot can reuse that memory.

### Host packaging and device entry

After a firmware build, the following host-only command packages both the UEFI image and the matching stock recovery:

```sh
bash Resources/Scripts/package_gts7fewifi_recovery.sh
```

The output directory is `Build/gts7fewifiPkg/recovery-packages`. `gts7fewifi-uefi-recovery.tar` contains one `recovery.img` member assembled from the generated `Mu-gts7fewifi.img` and `sources/UniXNU-gts7fewifi/build/gts7fewifi-platform-payload.img` under the workspace root. The input FAT can be supplied as the script's first argument. `gts7fewifi-stock-recovery.tar` contains one `recovery.img` member from the matching downloaded original firmware. Plain USTAR packages are used without an appended MD5 record. The current rooted Android boot, vendor_boot and vbmeta are already installed and remain the normal boot path.

`append_gts7fewifi_payload.py` preserves the recovery header, all header-described sections, and the `SEANDROIDENFORCE` marker immediately following those sections. It pads to `0x800000`, copies the 64 MiB FAT image, then pads to the GPT's full `0x4E00000`-byte recovery partition. The FAT lies outside the Android header's kernel, ramdisk, DTBO and DTB sections. Writing the complete partition removes any footer or signature left by a previous recovery image. No AVB footer or signed descriptor is generated for this custom image; it uses the device's unlocked loader and already installed Magisk-patched vbmeta configuration. The stock restore package retains the original recovery image's metadata.

`Platforms/Samsung/gts7fewifiPkg/Drivers/RecoveryPayloadDxe` waits for Block I/O installation and locates the GPT partition named `recovery`. It reads the 64 MiB extent at `0x800000` into an aligned Boot Services allocation, requires a FAT32 boot sector with the `T733ESP` label, and registers those pages with the standard `EFI_RAM_DISK_PROTOCOL`. RamDiskDxe publishes Block I/O and connects the FAT driver. UniXNU discovers `EFI/UniXNU/unixnu.toml` on that volume and loads the included kernel and ADT. The driver reads the recovery partition; writes through the FAT filesystem affect the RAM copy. This path carries the payload without an external storage device or changes to userdata. DarwinBoot's default 256 MiB allocation is selected from the UEFI memory map and is separate from the 64 MiB RAM disk.

Device deployment writes the UEFI package to recovery using the existing Samsung flashing client, lets Android start normally, and enters recovery with standard `adb reboot recovery`, as explicitly requested by the user after the second attempt. AOSP init writes `boot-recovery` into misc for recovery reboot; the captured stock recovery log records its subsequent `clearBCB` operation. The separate GPT and partition backup work supplies the saved misc and recovery content. The firmware's menu reset action calls `ResetSystem(EfiResetCold)` through the PSCI reset library. Samsung's recovery selection state can survive this reset; return to Android requires a normal boot selection rather than merely a cold reset. The menu's return-to-firmware action returns to Mu BDS and does not request an Android reboot.

If the UEFI screen or input does not become usable, enter Samsung Download Mode with the device keys and use `gts7fewifi-stock-recovery.tar` to restore recovery. A recovery-only package does not contain a PIT or super payload and does not request repartitioning. The root agent performs device writes and startup; this UEFI subtask produces host artifacts.

## First device boot and early-start revision

The root agent wrote the initial recovery package successfully and Android then reached `sys.boot_completed=1`. A subsequent rooted recovery reboot produced a black-screen reboot loop; the user saw no `Project Silicium` text. Restoring the stock recovery package succeeded, stock recovery appeared, and its `Reboot system now` action returned the device to Android. The failure capture is at `backups/sm-t733-first-uefi-failure-2026-10-22`. Its pstore content shows the stock recovery session after restoration. The captured `misc` command/status/recovery fields are empty after stock recovery ran. These observations do not identify the failed UEFI instruction.

The deployed failed artifacts are preserved under `Build/gts7fewifiPkg/failed-first-boot`, including the USTAR package, composed recovery image, base recovery image, FD, BootShim and device config. The existing `recovery-packages` files remain that first deployed version. The corrected build uses the `early-start` tag and writes `Mu-gts7fewifi-early-start.img`, `Build/gts7fewifiPkg/early-start/SILICIUM_UEFI.fd` and `Build/gts7fewifiPkg/recovery-packages-early-start`.

The corrections follow specific differences from the T733 Aloha source and the matching stock image:

- Aloha `Silicon/QC/Sm7325/QcomPkg/Library/PlatformPrePiLib/PlatformUtils.c` only wakes the first two GIC redistributors. Mu's shared Kodiak library also cleared SMMU stream mappings and context-bank translation registers before the first display message. The T733 platform library now performs the GIC initialization while preserving the bootloader's SMMU state.
- Aloha `Platforms/AndromedaPkg/PrePi/AArch64/ModuleEntryPoint.S` masks IRQ/FIQ, disables MMU and caches, invalidates instruction cache and invalidates the TLB before assigning a physical stack. T733's assembly platform hook now establishes the same state before Mu's common SEC entry assigns the stack. It operates at the incoming EL1 or EL2 and preserves that exception level.
- Aloha `Silicon/QC/Sm7325/QcomPkg/QcomPkg.dsc.inc` selects `armv8.2-a+crypto+rcpc`. T733 now selects that instruction set instead of the shared Kodiak `armv8.4-a` setting.
- The first payload composer removed `SEANDROIDENFORCE` from the end of the Android header-described sections and moved it to the end of the appended FAT. The matching stock recovery and the TWRP packaging source place the marker immediately after the Android sections. The composer now preserves that position and fills the complete partition, removing the old AVB footer that a short write could leave behind.

BootShim's FD destination `0x9FC41000`, copy size `0x2BF000`, stack range and DXE heap agree with the T733 reference and remain unchanged. No repeated device trials or parameter search were used to choose the corrections.

The `early-start` DEBUG build completed with exit code 0 in 11 seconds, recorded in `logs/build-uefi-gts7fewifi-early-start.log`. The firmware volume uses `0x126C50` bytes and has `0x1983B0` bytes free. The corresponding package command completed with exit code 0 in `logs/package-uefi-gts7fewifi-early-start.log`. Its actual recovery startup also produced a black-screen reboot loop. These artifacts are preserved under `Build/gts7fewifiPkg/failed-early-start` and in the original tagged output paths.

The revised host commands are:

```sh
source /Users/rigoligo/expm/unixnu-sm-t733/tools/activate.sh
bash Resources/Scripts/build_gts7fewifi_macos.sh DEBUG early-start
bash Resources/Scripts/package_gts7fewifi_recovery.sh \
  /Users/rigoligo/expm/unixnu-sm-t733/sources/UniXNU-gts7fewifi/build/gts7fewifi-platform-payload.img \
  early-start
```

## Second startup and BootShim copy correction

The second actual recovery entry used the static `reboot-recovery-direct` helper, which calls `sync()` and `RESTART2("recovery")` without Android init. The captured misc fields remained empty. After stock recovery restoration the device returned directly to Android with `sys.boot_completed=1` and boot reason `cold`. The failure therefore occurred with an empty BCB.

`backups/sm-t733-early-start-failure-2026-10-22/last_kmsg.txt` contains retained XBL/ABL messages. ABL loaded recovery, allowed its authentication failure under the unlocked policy, decompressed the kernel, selected the DT and completed ExitBootServices. At line 15303 the bootloader exception handler reports `ESR=0x02000000`, `PC=0x9FC41000`, `X1=0xA00F8070`, `X4=0xA03B7070`, `X5=0x9FC41000` and `X6=0`. This identifies a completed BootShim copy followed by an undefined instruction at the FD destination. Later ABL messages report `PARAM_BOOT_RECOVERY_ENTER`; persistent recovery selection also exists outside the BCB.

The failed BootShim's disassembly shows `_Payload` at offset `0x70`, followed by the assembler's 16-byte literal pool containing `0x9FC41000` and `0x2BF000`. `BootShim.bin` is `0x80` bytes long, and the image producer appends the FD after that binary. Consequently the copy begins 16 bytes before the FD, installs `0x9FC41000` as the first instruction word, and jumps to that word. The saved FD itself starts with valid AArch64 branch `0x14000400`; its entry point was displaced by the copied constants.

`BootShim/BootShim.S` now emits `.ltorg` before aligning and defining `_Payload`. The address constants remain at offsets `0x70` and `0x78`, while the entry `adr` now points to `0x80`, where the appended FD begins. The actual assembly output is saved in `Build/gts7fewifiPkg/bootshim-payload/BootShim.disassembly.txt`, alongside the built BootShim and FD. The original Aloha `BootShim/BootShim.S` avoids this problem by loading the two values directly from its ARM64 header fields, rather than generating a trailing literal pool. This is a source and captured-exception diagnosis, with no parameter trial.

The corrected DEBUG build completed with exit code 0 in 12 seconds. Its compact FV uses `0x126BD8` bytes and has `0x198428` bytes free. Logs are `logs/build-uefi-gts7fewifi-bootshim-payload.log` and `logs/package-uefi-gts7fewifi-bootshim-payload.log`. The `bootshim-payload` revision retains the successfully loaded header v2, matching stock DTB/DTBO, early-start initialization, FAT offset and full partition length. Its output paths are:

- `Mu-gts7fewifi-bootshim-payload.img`
- `Build/gts7fewifiPkg/bootshim-payload/SILICIUM_UEFI.fd`
- `Build/gts7fewifiPkg/recovery-packages-bootshim-payload/uefi/recovery.img`
- `Build/gts7fewifiPkg/recovery-packages-bootshim-payload/gts7fewifi-uefi-recovery.tar`
- `Build/gts7fewifiPkg/recovery-packages-bootshim-payload/gts7fewifi-stock-recovery.tar`

The first-person report at <https://www.karantonisa.com/windows.html> names the pinned Aloha branch and commit for a 4 GB/64 GB T733, while its project notes do not identify every installed binary. The repository's release API returned no published release assets. The original packaging code uses header v0 with `gzip(BootShim + FD)` followed by its merged Android DTB. The captured exception above gives a direct correction for the current firmware without changing that already accepted header v2 loading path.

## Third startup and PIL automatic ABL loading

The root agent flashed `bootshim-payload`, observed normal Android startup, then used the requested standard `adb reboot recovery`. The user recorded several screens of UEFI output followed by a reboot loop. The last readable message was `pil-abl Failed to load ELF file`. This startup reached executable firmware and DXE driver output after the BootShim correction. The deployed files are preserved under `Build/gts7fewifiPkg/failed-bootshim-payload`, alongside the PILDxe disassembly used for diagnosis.

The message is present in the imported `Vendor/QcomPkg/Drivers/PILDxe/PILDxe.efi`. Its relative code address `0x19B0` selects `pil-%s Failed to load ELF file` after the loader callback returns failure. Driver initialization at `0x15D4` iterates the `uefipil.cfg` automatic list. A failed entry leads to `Auto processing of uefipil.cfg failed. Calling target panic` at `0x1600` and a call to `0x4A2C` at `0x1608`. The latter routine enters a permanent loop at `0x4A4C`. This is a fatal initialization path, rather than an informational message that the driver ignores.

The imported T733 configuration had only `ABL` in `[AUTO]`, with `Type=elf_fv`, subsystem 21, and partition GUID `BD6928A1-4CE0-A038-4F3A-1495E3EDDFFB`. The captured `backups/sm-t733-gpt-2026-10-22/sde/gpt.json` uses that same GUID for `abl`; the GUID is not changed. Mu already runs its own BDS and starts UniXNU from its firmware volume. It does not need to load the Android bootloader again during DXE initialization.

The `pil-auto` revision removes `ABL` from this platform's `[AUTO]` list and retains the section. The binary's list parser accumulates entries only for actual item lines; its initialization branch at `0x15BC` skips loading when the count is zero and continues installing the PIL protocol at `0x160C`. `[RETAIL]`, the `[ABL]` definition, other subsystem definitions, and the driver remain available for explicit requests. The binary contains no `LoadAutoImageInPILFlag` lookup, so this revision changes the list the driver actually reads.

The `pil-auto` DEBUG build completed with exit code 0 in 11 seconds, using `0x126BC0` bytes of the compact FV and leaving `0x198440` free. Packaging also completed with exit code 0. Logs are `logs/build-uefi-gts7fewifi-pil-auto.log` and `logs/package-uefi-gts7fewifi-pil-auto.log`. The next recovery package is `Build/gts7fewifiPkg/recovery-packages-pil-auto/gts7fewifi-uefi-recovery.tar`; the same directory holds its stock recovery restore package. This revision retains the corrected BootShim and the header v2/full-partition/FAT layout from the third startup.

## Confirmed UniXNU menu startup

On 2026-10-22 the root agent deployed `pil-auto` and the user confirmed that the device entered UniXNU. This is the first confirmed menu startup on the physical SM-T733. The user subsequently reported `no boot entry found`, with only reset and return-to-firmware actions. The menu therefore proves firmware and application startup, while the recovery FAT and XNU entry were not available. Volume up/down move the selection; the power key does not activate it. The device agent owns the corresponding input correction.

The recovery package that displayed the default menu stays at `Build/gts7fewifiPkg/recovery-packages-pil-auto/gts7fewifi-uefi-recovery.tar`. An additional preserved copy is under `Build/gts7fewifiPkg/menu-success-pil-auto`, together with its base Android image, FD and BootShim. That directory records menu display success only; it does not identify a successful payload boot.

The UFS agent traced the earlier PIL failure below the general ELF message. For `Type=elf_fv`, code at `0x2918` calls the partition reader at `0x7070`, which enumerates existing Block I/O handles using the configured partition GUID, allocates a buffer for the partition and reads from LBA 0. A read failure prints `Failed to read from partition` and returns PIL status 3 before ELF header validation. The saved stock ABL is ELF32 and contains an FV; the earlier message alone does not identify an invalid ABL image.

The platform's `Include/APRIORI.inc` includes all vendor drivers before DiskIoDxe, PartitionDxe and FAT. Thus its PILDxe initialization occurs before those standard storage drivers are dispatched. Its `elf_fv` reader enumerates existing handles without calling `ConnectController`; the separate mount path containing that connection call is skipped for this image type. Merely moving PartitionDxe earlier would not establish that GPT child handles already exist. Any future explicit PIL load from a partition must run after storage drivers are installed and the UFS controller has been connected recursively, or after BDS has completed the corresponding connections. The current empty `[AUTO]` avoids issuing that premature request. This ordering limitation is documented without changing the successfully booted firmware.

## Recovery FAT discovery diagnosis

The built `Build/gts7fewifiPkg/DEBUG_CLANGPDB/AARCH64/MdeModulePkg/Universal/Disk/RamDiskDxe/RamDiskDxe/OUTPUT/RamDiskDxe.inf` contains the combined dependency expression. In addition to the HII database and routing protocols listed by RamDiskDxe itself, its FileExplorerLib dependency requires `gEfiFormBrowser2ProtocolGuid`. SetupBrowserDxe is already included through `QcomPkg/Extra.fdf.inc` and `SiliciumPkg/Extra.fdf.inc`. An attempted direct addition to the T733 DXE list produced GenFv's duplicate GUID error and was removed. That failed build is recorded in `logs/build-uefi-gts7fewifi-recovery-fat.log`; missing SetupBrowserDxe is not the cause.

RecoveryPayloadDxe now reports driver startup, the recovery partition match, allocation and read failures, FAT32 label recognition, RAM disk registration, and the FAT filesystem protocol at registration to DEBUG and to the text console when present. It publishes a persistent `CHAR16[192]` status buffer using protocol GUID `71A8180B-B15B-470A-9188-8CD4A8044416`, which UniXNU displays on its no-entry page. The buffer also reports Block I/O and GPT counts when recovery is absent. Its dependency expression is now TRUE so this diagnostic can appear even when the RAM disk protocol is absent; notifications for RAM disk and Block I/O installation start loading when the inputs exist.

The existing Block I/O notification is retained: `CoreInstallMultipleProtocolInterfaces` holds TPL_NOTIFY for the full operation, so a TPL_CALLBACK notification cannot observe only the first protocol of PartitionDxe's atomic installation. That rejected timing hypothesis is not the cause of the missing RAM disk. The original payload's FAT32 boot sector has the expected `T733ESP` label and FAT32 identifier at the offsets used by the loader.

The next deployable package also includes the device agent's input correction. The `pil-auto` images remain preserved while these changes are built separately. The root agent is first collecting the current device's full firmware logs. The earlier PIL partition read failure and the missing recovery FAT may share an unavailable UFS or GPT handle path; the new diagnostic build is preparation and does not replace that current-log investigation.

The combined `recovery-diagnostics` DEBUG build completed on 2026-10-23 with exit code 0 in 11 seconds. Its compact FV uses `0x127550` bytes and leaves `0x197AB0` free. Packaging also completed with exit code 0. It includes the device agent's `Menu.c` change accepting the vendor power-key scan code `0x80` with Unicode zero in both the menu and confirmation page, along with visible scan-code/Unicode diagnostics and the persistent recovery status line. The FAT payload contents remain unchanged.

The prepared package is `Build/gts7fewifiPkg/recovery-packages-recovery-diagnostics/gts7fewifi-uefi-recovery.tar`; its directory also contains the matching stock restore package. The build and packaging logs are `logs/build-uefi-gts7fewifi-recovery-diagnostics.log` and `logs/package-uefi-gts7fewifi-recovery-diagnostics.log`. This revision awaits the root agent's current-log investigation and deployment decision; no device startup result is assigned to it.

## Project Silicium menu power key

The user also reported that the Project Silicium menu ignores the physical power key. `SiliciumPkg/Library/BootManagerLib/KeyCallback.c` registers volume-up notification for entering that menu. Selection itself is handled by `MdeModulePkg/Application/BootManagerMenuApp/BootManagerMenu.c`, which previously dispatched only Unicode carriage return as confirmation. The captured vendor button conversion uses scan `0x80` and Unicode zero for the power key.

`Resources/MuPatches/Boot-Manager.patch` now converts that exact key pair to carriage return before the existing menu switch, under `T733_VENDOR_POWER_KEY`. The T733 DSC defines this macro; other platforms retain their existing input behavior. The T733 menu help now reads `Volume Up/Down: move selection` and `Power or Enter: select`. Existing Enter, volume navigation and Escape processing remain intact. UniXNU's separate menu and confirmation dialog already include the same power-key pair through the device agent's update.

The combined `recovery-diagnostics-keys` DEBUG build completed on 2026-10-23 with exit code 0 in 12 seconds. The compact FV uses `0x127680` bytes and leaves `0x197980` free. The initial application of the edited patch encountered mixed line-ending context; the patch was regenerated from the focused applied diff before the successful build. Packaging completed with exit code 0. Logs are `logs/build-uefi-gts7fewifi-recovery-diagnostics-keys.log` and `logs/package-uefi-gts7fewifi-recovery-diagnostics-keys.log`.

The new package is `Build/gts7fewifiPkg/recovery-packages-recovery-diagnostics-keys/gts7fewifi-uefi-recovery.tar`, with the stock restore package alongside it. It combines both menus' power-key support with persistent Recovery status and keeps the original recovery FAT contents. `pil-auto`, the menu-success copy, and the preceding diagnostic artifacts remain preserved.

The root agent deployed `recovery-diagnostics-keys` and read the recovery partition at offset 8 MiB before entering recovery. The saved `out/t733-failure-analysis/recovery-diagnostics-keys-bpb.bin` contains a 512-byte sector size, 131072 sectors, the `T733ESP` label at offset 71 and the `FAT32` identifier at offset 82. The appended volume is present on the physical partition.

The user saw `T733: RAM Disk protocol not installed: Not found`, followed later by RamDiskDxe loading. This console line is emitted during the early protocol-notification scan. RecoveryPayloadDxe also registers for RAM disk installation and rescans when that protocol appears. The persistent menu line beginning with `Recovery:` supplies the latest scan result; the earlier console line alone does not establish that RAM disk installation ultimately failed.

The actual generated `OUTPUT/RamDiskDxe.depex` requires five protocols: HiiConfigRouting, HiiDatabase, Pcd, HiiString and FormBrowser2. PcdDxe supplies Pcd; HiiDatabaseDxe installs the three HII protocols together; SetupBrowserDxe installs FormBrowser2. All three providers are included in this firmware. FileExplorerLib's constructor installs its configuration-access handle and HII packages before locating FormBrowser2. RamDiskDxe's entry then allocates its configuration data, installs its own configuration form, initializes the disk list and installs EFI_RAM_DISK_PROTOCOL. Its ACPI lookup runs later through ReadyToBoot and does not precede RAM disk protocol installation.

The user subsequently supplied the persistent menu result: `Recovery: 0 Block I/O handles, no partition-info handles (Not Found)`. RamDisk installed and its notification caused another scan. The missing storage handles precede GPT parsing and recovery payload reads. The UniXNU `drivers 0 loaded / 0 failed` line describes payload drivers and does not report the firmware's UFSDxe dispatch result.

## UFSDxe dispatch diagnosis

`Resources/MuPatches/Dxe-Ufs-Diagnostics.patch` adds a T733-only record around the existing DXE dispatcher's calls for file GUID `0D35CD8E-97EA-4F9A-96AF-0F0D89F76567`. It records driver selection, the original CoreLoadImage result, entry invocation and the original CoreStartImage result. The existing calls retain their position and execute once. The record remains in DXE Core storage if an unsuccessful entry causes the driver image to be unloaded. Protocol GUID `92124B17-7456-4B4B-8A27-12D959F38672` exposes that record to RecoveryPayloadDxe.

Immediately before the original UFSDxe entry call, the dispatcher reads Shared IMEM at `0x146AA000`, `0x146AA004` and `0x146AA050`. The vendor code expects magic `0xC1F8DB40`, version at least 3 and raw boot-device 8 for UFS. The running device tree maps this IMEM range, and the board configuration supplies the same base. The vendor's override configuration is consulted only after the IMEM fields pass its parsing, so a configuration override would not bypass an invalid cookie.

When no Block I/O handles exist, RecoveryPayloadDxe now leaves the captured LoadImage and StartImage results plus IMEM magic, version and raw boot-device in the menu's persistent `Recovery:` line. A missing record or a load failure has a separate message. This identifies successful early return, failed driver entry and failure before entry without requiring the user to read transient console lines.

The independent `ufs-dispatch` DEBUG build completed on 2026-10-23 with exit code 0 in 11 seconds. Packaging also returned exit code 0. Its compact FV uses `0x1277F8` bytes and leaves `0x197808` free. The deployable package is `Build/gts7fewifiPkg/recovery-packages-ufs-dispatch/gts7fewifi-uefi-recovery.tar`; the matching stock restore tar is alongside it. Logs are `logs/build-uefi-gts7fewifi-ufs-dispatch.log` and `logs/package-uefi-gts7fewifi-ufs-dispatch.log`. The existing power-key handling, RAM disk notification and FAT contents are retained. The previously deployed artifacts remain preserved.

The deployed diagnostic build supplied a concrete failure in the user's screenshot, `/Users/rigoligo/Downloads/Screenshot_20261023_003021_Gallery.jpg`. UFSDxe loaded successfully, read IMEM magic `C1F8DB40`, version 3 and boot-device 8, then failed HWIO mapping of physical `0x01D80000` with size `0x20000` at SetMemorySpaceAttributes. DAL reported `HWIO mapping failed: USMHS2AREG`; UFSDxe reported failure mapping `UFS_MEM_UFS`, and its original StartImage returned Protocol Error. The driver reached its UFS branch and failed before opening the device WLUN.

The previous four UFS entries in MemoryMapLib covered only `[0x01D84000, 0x01D99000)`. HWIO's requested `[0x01D80000, 0x01DA0000)` therefore included a missing `0x4000` at the start and `0x7000` at the end. MemoryInitPei emits resource HOBs for AddDev entries, and DXE Core builds its GCD map from those HOBs. DALSYSDxe's mapping function at RVA `0x3738` first obtains the descriptor at the requested base. An existing MMIO descriptor is accepted; a NonExistent descriptor causes AddMemorySpace for the entire requested window, followed by SetMemorySpaceAttributes on success. The old base was NonExistent, so that AddMemorySpace request overlapped the four existing middle entries and met GCD's EFI_ACCESS_DENIED condition. DAL's common failure print at `0x3810` includes the text SetMemorySpaceAttributes for failures from any of these calls. The screenshot's wording therefore identifies the mapping operation, while the DAL and GCD code explain the overlapping-add failure.

MemoryMapLib now declares one UFS Subsystem region at `0x01D80000` with length `0x20000`, preserving AddDev, MMAP_IO, UNCACHEABLE, MmIO and NS_DEVICE. It replaces the four smaller entries so the resource HOBs do not overlap. The concrete controller registers retain their existing addresses; the firmware's MMIO mapping now covers the full vendor HWIO request.

The `ufs-mmio` DEBUG build completed on 2026-10-23 with exit code 0 in 12 seconds, and packaging completed with exit code 0. Its compact FV uses `0x127780` bytes and leaves `0x197880` free. The deployable package is `Build/gts7fewifiPkg/recovery-packages-ufs-mmio/gts7fewifi-uefi-recovery.tar`. Logs are `logs/build-uefi-gts7fewifi-ufs-mmio.log` and `logs/package-uefi-gts7fewifi-ufs-mmio.log`. Dispatch diagnostics remain enabled for the next device startup.

## Device reached DarwinBoot handoff

The root agent deployed `ufs-mmio` successfully, and the user reported that XNU loaded, a green bar appeared and progress then stopped. This establishes working UFS block publication, GPT recovery lookup, the appended FAT read, RAM disk filesystem publication and loading of the configured DarwinBoot payload. The root agent traced the green bar to the beginning of DarwinBoot's `DarwinEnter` assembly, called only after ExitBootServices succeeds. Its preceding boot path also requires EL1. The visible green bar therefore confirms arrival at the loader's final assembly handoff; kernel entry and XNU initialization are the remaining milestones.

The UEFI exit path disables the timer, signals ExitBootServices callbacks, disables CPU interrupts and clears boot services and console pointers. ArmGicDxe's callback disables interrupt sources, the CPU GIC interface and distributor. TimerDxe disables the architectural timer. T733 PlatformSecLib preserves the inherited SMMU mapping, and the patched UFSDxe does not register its POWERDOWN exit callback. The BDS KernelErrataPatcherLib hook restores the original ExitBootServices pointer and passes through when no Winload image was identified. The observed green bar places this run after those exit callbacks.

The platform keeps the FD at `0x9FC41000/0x2BF000` and SEC stack at `0x9FF90000/0x40000` as Boot Services data. Display Reserved remains reserved at `0xE1000000/0x02300000`, with DFPS at `0xE3300000/0x00100000`. DarwinBoot allocates its owned 256 MiB from the UEFI memory map. The firmware does not switch exception levels or disable CPU MMU and caches in ExitBootServices; DarwinBoot performs its cache maintenance, MMU shutdown and final branch afterward.

The DEBUG SerialPortLib is FrameBufferSerialPortLib: it draws directly into Display Reserved and flushes that framebuffer range. The platform has no configured physical UART output. UEFI console services are unavailable after successful ExitBootServices, so the next handoff observations use direct framebuffer writes in DarwinBoot and kernel startup. The root agent owns the DarwinBoot instrumentation and the kernel agent owns kernel-entry observations; no new firmware build has been assigned to this research step.

The subsequent `handoff-stages` package combines the root agent's DarwinBoot assembly markers with the kernel agent's early-entry markers. Green marks entry into DarwinEnter after successful ExitBootServices; cyan marks completion of its cache maintenance; blue marks MMU shutdown; magenta marks the kernel's first entry; yellow marks completion of its initial page-table preparation. The device agent staged the marked kernel separately in `sources/UniXNU-gts7fewifi/build/handoff-stages-esp` and generated `sources/UniXNU-gts7fewifi/build/gts7fewifi-handoff-stages-payload.img`. That new FAT image is the explicit input to this package. The prior `gts7fewifi-platform-payload.img` remains preserved.

The `handoff-stages` DEBUG firmware build completed on 2026-10-23 with exit code 0 in 13 seconds. Its compact FV uses `0x1278C0` bytes and leaves `0x197740` free. Packaging completed with exit code 0. The deployable package is `Build/gts7fewifiPkg/recovery-packages-handoff-stages/gts7fewifi-uefi-recovery.tar`, with a matching stock restore tar alongside it. Logs are `logs/build-uefi-gts7fewifi-handoff-stages.log` and `logs/package-uefi-gts7fewifi-handoff-stages.log`. This build awaits physical-device observations of those five stages.

The root agent subsequently deployed `handoff-stages`, and the user's photo showed all five bars through yellow. This confirms DarwinBoot's cache maintenance and MMU shutdown, the branch into XNU and completion of the marked initial page-table preparation. The next execution interval is the kernel's common_start path toward arm_init.

UEFI's EL1 floating-point setup is explicit in source. SEC calls ArmEnableVFP, then SetupExceptionLevel1 writes CPACR_DEFAULT through ArmWriteCpacr. `MdePkg/Include/AArch64/AArch64.h` defines that value as `3UL << 20`, or `0x00300000`, enabling FP/SIMD at EL1. CoreExitBootServices does not rewrite CPACR_EL1. The EL1 initialization path does not change CPTR_EL2; no runtime sample of that higher-level trap register has been captured. These facts describe the firmware's writes, while any kernel changes to CPACR belong to the subsequent startup path.

ArmGicV3Dxe enables ICC_SRE_EL1.SRE, writes ICC_BPR1_EL1 as 7 and ICC_PMR_EL1 as 255, and clears ICC_CTLR_EL1.EOImode. Its ExitBootServices callback writes ICC_IGRPEN1_EL1 as zero and disables the distributor and interrupt sources. It retains the system-register interface enable bit. The kernel agent received these source locations and values for its common_start investigation. No UEFI code or artifact changed during this investigation.

The next `mmu-stages` package uses the device agent's independent `sources/UniXNU-gts7fewifi/build/gts7fewifi-mmu-stages-payload.img`, staged from `build/mmu-stages-esp`. The kernel adds red before its SCTLR write, white after enabling the MMU and orange on entering the kernel virtual-address trampoline. Its diagnostic framebuffer has an explicit TTBR0 Device mapping, and CONFIG_XNU_DT_PANIC_REGION selects the device-tree panic region. The root agent updated DarwinBoot's visible legend for these three additional markers.

The `mmu-stages` DEBUG build completed on 2026-10-23 with exit code 0 in 10 seconds, and packaging completed with exit code 0. Its compact FV uses `0x127810` bytes and leaves `0x1977F0` free. The package is `Build/gts7fewifiPkg/recovery-packages-mmu-stages/gts7fewifi-uefi-recovery.tar`; the matching stock restore tar is alongside it. Logs are `logs/build-uefi-gts7fewifi-mmu-stages.log` and `logs/package-uefi-gts7fewifi-mmu-stages.log`. Earlier firmware packages and payload FAT images remain preserved. Device observations of the added stages are pending.

The deployed `mmu-stages` reached red, white and orange on the physical device. The user reported a restart about 3 to 15 seconds after orange. The marked MMU enable and kernel virtual-address trampoline completed; the interval toward arm_init remains under investigation. This elapsed-time estimate does not identify the reset source.

## Watchdog source investigation

The standard MdeModulePkg WatchdogTimerDxe implements its timeout with a Boot Services timer event. CoreExitBootServices disables the architectural timer and CPU interrupts before returning. Its software event is separate from the Qualcomm hardware and hypervisor watchdogs.

The actual imported QcomWDogDxe binary was disassembled into `out/t733-failure-analysis/QcomWDogDxe.disassembly.txt`. The compatibility patch at RVA `0x23BC` returns success immediately, bypassing the remaining function that obtains the hardware watchdog service table. That table pointer at RVA `0x61E8` is initially zero. The enable and timeout wrappers at `0x2320` and `0x2354` return EFI_UNSUPPORTED when it is absent. The entry chooses its setting from `ForceEnableHWWdog`, a default or the debug information block. If enabling fails, it prints `Failed to Enable Wdog` and follows cleanup. Cleanup at `0x1700` disables hardware only when the driver's own enabled flag at `0x6154` was set. The failure therefore supplies no evidence that an inherited watchdog was disabled or that this module started one. Its ExitBootServices callback at `0x16FC` also uses that cleanup path.

The captured running device tree marks `qcom,msm-watchdog` at `0x17C10000/0x1000` disabled and includes an enabled `qcom,hh-watchdog`. The matching Samsung kernel's `drivers/virt/haven/hh_virt_wdt.c` controls the latter through fast 32-bit vendor-hypervisor SMCCC calls: function 5 for control, 6 for status, 7 for pet and 8 for bark/bite timing. The status response uses a1 bit 0 for enabled, a1 bit 31 for expired and a2 for time since the last pet. Control values 3 and 2 enable and disable while retaining required bit 1. These calls were read from source; none were issued during this investigation.

Samsung's qcom_wdt_core.c registers the hypervisor watchdog's bark interrupt and panic/restart handlers, sets bark and bite times and starts a periodic pet task. The previous Android boot's drivers and their scheduling do not continue after starting this recovery firmware. The kernel agent confirmed that the current XNU source at `out/t733-kernel/gts7fewifi/tree/.cache/xnu-work` has no Qualcomm or Haven watchdog takeover. Whether a firmware or hypervisor watchdog remains active at the XNU handoff still requires an actual state observation or reset evidence. No watchdog setting or firmware behavior was changed for this investigation.

The kernel and device agents then found a concrete early panic path: the valid arm-io base zero caused pe_identify_machine to return before reading the timebase, leaving timebase zero for wfe_timeout_init. The kernel agent is correcting that parsing path. The next image addresses that identified failure while retaining the watchdog research as background; no new SMCCC calls are added.

The `c-stages` package uses `sources/UniXNU-gts7fewifi/build/gts7fewifi-c-stages-payload.img`, containing the kernel's zero SoC-base/timebase parsing correction, English C-startup stage messages and early panic caller/raw-format output. UEFI behavior is unchanged. The DEBUG build completed on 2026-10-23 with exit code 0 in 12 seconds, and packaging returned exit code 0. The package is `Build/gts7fewifiPkg/recovery-packages-c-stages/gts7fewifi-uefi-recovery.tar`; logs are `logs/build-uefi-gts7fewifi-c-stages.log` and `logs/package-uefi-gts7fewifi-c-stages.log`. The matching stock restore tar and prior tagged artifacts are retained.

The deployed `c-stages` reached `entering arm_vm_init`. The kernel agent is adding VM stages and restoring framebuffer output after the page-table change. The UEFI subtask read the existing synchronous-exception path: common_start installs the kernel ExceptionVectorsBase address in VBAR before enabling the MMU; locore.s reads ESR_EL1 and FAR_EL1, and exception_asm.h saves them with the ELR-derived PC in arm_saved_state64. `sleh.c:panic_with_thread_kernel_state` retains original_faulting_state and passes PC, LR, CPSR, ESR and FAR to its existing panic_plain format. `Kernel data abort.` is one of its callers.

The current arm_early_panic interface receives only the raw format string and the panic caller address. Its displayed text therefore does not expand the saved-state values supplied as panic arguments. It also returns immediately while arm_early_video is zero; arm_early_end clears that value before entering arm_vm_init. The kernel agent received the saved-state call site as the direct source for an exception-specific diagnostic. A normal explicit panic's caller address and an exception's saved PC identify different instructions. No kernel source was modified by this read-only investigation.

The `vm-stages` package uses the newly assembled `sources/UniXNU-gts7fewifi/build/gts7fewifi-vm-stages-payload.img`. The DEBUG firmware build completed on 2026-10-23 with exit code 0 in 12 seconds; packaging returned exit code 0. UEFI behavior remains unchanged. The package is `Build/gts7fewifiPkg/recovery-packages-vm-stages/gts7fewifi-uefi-recovery.tar`, with a matching stock restore tar alongside it. Logs are `logs/build-uefi-gts7fewifi-vm-stages.log` and `logs/package-uefi-gts7fewifi-vm-stages.log`. Prior tagged packages remain preserved.

The `undef-stages` DEBUG firmware build completed on 2026-10-23 with exit code 0 in 11 seconds while the device agent assembled its new FAT. Packaging started after the new `sources/UniXNU-gts7fewifi/build/gts7fewifi-undef-stages-payload.img` was reported complete and returned exit code 0. The package is `Build/gts7fewifiPkg/recovery-packages-undef-stages/gts7fewifi-uefi-recovery.tar`. Logs are `logs/build-uefi-gts7fewifi-undef-stages.log` and `logs/package-uefi-gts7fewifi-undef-stages.log`. UEFI behavior and prior tagged packages remain unchanged.

The `fb-map-space` DEBUG firmware build completed on 2026-10-23 with exit code 0 in 12 seconds. After the device agent reported the new `sources/UniXNU-gts7fewifi/build/gts7fewifi-fb-map-space-payload.img` assembled successfully, packaging returned exit code 0 and the root agent immediately received the deployable path. The new kernel corrects space reserved for the additional diagnostic framebuffer mapping. The package is `Build/gts7fewifiPkg/recovery-packages-fb-map-space/gts7fewifi-uefi-recovery.tar`. Logs are `logs/build-uefi-gts7fewifi-fb-map-space.log` and `logs/package-uefi-gts7fewifi-fb-map-space.log`. The firmware behavior and previous tagged artifacts are retained.

The `virtual-timer` DEBUG firmware build completed on 2026-10-23 with exit code 0 in 9 seconds. The device agent then supplied the successfully assembled `sources/UniXNU-gts7fewifi/build/gts7fewifi-virtual-timer-payload.img`, containing the kernel correction for the physical-timer instruction in fiq_context_init. Packaging returned exit code 0, and the package was handed to the root agent before this record was written. The root agent confirmed flashing completed with exit code 0; the device startup observation is pending.

The deployed package is `Build/gts7fewifiPkg/recovery-packages-virtual-timer/gts7fewifi-uefi-recovery.tar`. Logs are `logs/build-uefi-gts7fewifi-virtual-timer.log` and `logs/package-uefi-gts7fewifi-virtual-timer.log`. UEFI behavior remains unchanged and previous tagged artifacts are retained. Subsequent packages follow the same delivery order: hand the completed package to the root agent, then record it after the root agent confirms flashing succeeded.

## Original Aloha build

`Resources/Scripts/build_gts7fewifi_aloha_macos.sh` builds the original reference tree with native macOS BaseTools and the project's LLVM. It uses `Platforms/KodiakPkg/KodiakNoSb.dsc`, `TARGET_DEVICE=samsung-gts7fewifi`, the pinned package revisions, the original BootShim, and the original `Platforms/KodiakPkg/PythonLibs/PostBuild.py`. The build preserves the upstream serial-output defaults and original platform source. It does not include UniXNU or the recovery FAT driver. The source project's setup instructions target Linux; this script supplies equivalent package paths and generates its configuration headers directly before EDK compilation.

The pinned Microsoft shared crypto release `Mu_CryptoBin_v1_1_3.zip` is saved under `downloads/aloha-build` and extracted into the reference tree's `MU_BASECORE/CryptoPkg/Binaries/crypto`. The configuration generator required `xmlschema==4.3.1`, installed in the workspace Python environment. Firmware build submodules are populated. An unused libspdm test dependency, GitLab cmocka, failed its network fetch; it is not part of this platform build.

The original DEBUG compilation reached firmware-volume generation, where GenFv required `0x2E1D30` bytes against the unchanged `0x2BF000` limit. The original RELEASE target subsequently completed with exit code 0 in 53 seconds and PostBuild produced its Android image. Its compact FV uses `0x2A93C0` bytes with `0x15C40` free. Logs are `logs/build-uefi-gts7fewifi-aloha-original.log` and `logs/build-uefi-gts7fewifi-aloha-original-release.log`.

The independent baseline outputs are under `references/mu_aloha_platforms/Build/KodiakPkg/original-gts7fewifi`:

- `samsung-gts7fewifi.img`: original header v0, 4096-byte pages, empty ramdisk, empty command line, OS 11.0.0, patch 2023-04-01.
- `bootpayload.bin`: gzip-compressed original BootShim and FD followed by `Platforms/KodiakPkg/Device/samsung-gts7fewifi/DeviceTreeBlob/Android/android-gts7fewifi.dtb`.
- `SM7325_EFI.fd` and `BootShim.bin`: the exact inputs retained with the baseline output.

The original PostBuild produces no Samsung marker, appended FAT or full-partition padding. These files preserve that build output for comparison; no baseline image has been deployed. The corrected Mu recovery package remains the active device-startup candidate.

## Userdata boot files and normal BOOT entry, 2026-10-23

The root agent confirmed that `text-console` reached ordinary XNU initialization and native UFS subsequently completed LUN0 capacity and GPT/userdata discovery. The user selected a dedicated UniXNU installation and authorized userdata for the system. The implementation below built successfully in `system-root-3`; its userdata, BOOT and recovery images were deployed successfully through `system-root-3-sparse`. Discovery of the userdata FAT and startup through the new normal BOOT entry still await device feedback.

The system image remains `out/t733-rootfs/gts7fewifi/userdata/userdata-root.img`, 9,382,658,048 bytes. Its SHNGROOT v1 header stays at userdata byte zero and still describes exactly the original OS and SystemOS windows. A separate combined image appends a 512 MiB T733ESP FAT32 volume at `0x22F400000`, making `image_bytes` equal to `0x24F400000` (9,919,528,960 bytes). The system-window offsets, sizes and stored digests remain unchanged. Only `image_bytes` and the format-required header digest change. NativeUfs already uses `image_bytes` plus its 4 MiB guard when admitting the persistent Data store and log/export regions, so these allocations exclude the appended FAT.

`Resources/Scripts/compose_gts7fewifi_userdata.py` receives the original system image, a 512 MiB FAT image and a new output pathname. It requires a two-window OS/SystemOS image ending at the aligned end of its windows, and a FAT32 BPB with 512-byte sectors and the T733ESP label. It opens the output exclusively, copies both source files, and emits the combined size and FAT offset. It calculates only the SHNGROOT header digest required by the existing disk format. It does not compute file confirmation digests.

Example from the workspace root, with the system FAT pathname supplied by the device packaging step:

```sh
python sources/uefi-gts7fewifi/Resources/Scripts/compose_gts7fewifi_userdata.py \
  out/t733-rootfs/gts7fewifi/userdata/userdata-root.img \
  /absolute/path/to/gts7fewifi-system-payload.img \
  out/t733-rootfs/userdata-system-with-boot-fat.img
```

The system inputs include 73,287,008 bytes of two-volume policy linked into the system kernel and a separate 33,554,432-byte Data seed. The completed `system-root-3` prelinked kernel is 125,284,352 bytes. Its 512 MiB FAT contains that kernel, the Data seed, trust data, ADT and configuration. The old 64 MiB development FAT and original system image are retained.

`Drivers/RecoveryPayloadDxe/UserdataPayload.c` reads the userdata SHNGROOT header, checks its structural bounds, calculates the next 1 MiB boundary after all described windows, and reads the FAT BPB there. The BPB determines the exposed volume size, bounded by `image_bytes`. The driver publishes a read-only BlockIo child using the UFS media's 4096-byte blocks and connects DiskIo and FatDxe. FatDxe consumes bytes through DiskIo, including the FAT's 512-byte sectors. This path reads files from UFS as requested instead of allocating a 512 MiB RAM disk. NativeUfs performs its existing manifest-integrity handling when the kernel opens the system windows.

The driver selects userdata only after its SimpleFileSystem can open `EFI/UniXNU/unixnu.toml`. Recovery's existing 8 MiB offset and 64 MiB RAM-disk loading remain a development fallback. Recovery loading waits until ReadyToBoot and until the userdata GPT handle exists, so publication of recovery earlier in partition enumeration does not select the old kernel ahead of a valid userdata FAT. When userdata supplies the configuration, the recovery RAM disk is not registered. UniXNU's existing configuration search and relative payload paths then use the userdata filesystem. Its TOML `block_slice` entries are created after configuration loading, so they cannot perform this initial filesystem discovery.

These userdata operations and changed fallback timing require `T733_USERDATA_PAYLOAD=1`. The DSC and header default to zero, and `build_gts7fewifi_macos.sh` forwards the environment value with a default of zero. A default build retains the original RAM-disk prerequisite, protocol notifications, recovery selection and status text, without a ReadyToBoot callback or userdata reads. The earlier `ufs-console` diagnosis used that recovery procedure. The `system-root-3` build enables userdata loading. Its `UserdataPayload.c` includes `Library/DebugLib.h`, added by the root agent to satisfy the `CR` macro during compilation. The standalone build invocation is:

```sh
T733_USERDATA_PAYLOAD=1 bash sources/uefi-gts7fewifi/Resources/Scripts/build_gts7fewifi_macos.sh DEBUG userdata-boot
```

Normal power-on can use the BOOT partition. `Resources/Scripts/package_gts7fewifi_boot.py` is a separate packaging entry: it extracts the already-built gzip BootShim/FD from a tagged header-v2 Mu image, replaces the kernel in the matching stock header-v3 BOOT layout, and preserves that stock header's OS fields, command line and ramdisk. The matching existing vendor_boot and dtbo partitions supply the v3 device-tree inputs. The package places `SEANDROIDENFORCE` immediately after the padded Android sections and fills the output to the full 96 MiB BOOT size, clearing the old AVB footer. It emits a tar member named `boot.img`. Its output directory must be new.

```sh
python sources/uefi-gts7fewifi/Resources/Scripts/package_gts7fewifi_boot.py \
  --recovery-image sources/uefi-gts7fewifi/Mu-gts7fewifi-TAG.img \
  --stock-boot downloads/samsung/SM-T733-CHN-T733ZCS9DYE1/images/boot.img \
  --output-dir sources/uefi-gts7fewifi/Build/gts7fewifiPkg/boot-packages-TAG
```

For userdata loading, `TAG` must name firmware built with `T733_USERDATA_PAYLOAD=1`. The normal-BOOT packager reuses that artifact's kernel and does not compile or alter firmware options.

BOOT packaging completed in `system-root-3`, and that BOOT image was deployed with the system package. Its actual startup outcome is pending. Recovery retains its development payload fallback. Once a persistent firmware entry loads userdata's FAT, kernel/configuration updates can target that FAT without rebuilding or rewriting the firmware partition. Actual updates still need a host or device transport that can write the bounded FAT region; the firmware child device is read-only. No GPT repartitioning is required by this layout.

### System configuration and loader capacity

`sources/UniXNU-gts7fewifi/board.system.toml` is an independent overlay consumed by the root agent's `--system` build workflow and the CLI's `--board-overlay` option. It selects the original 21H461 security/filesystem prelink roots, AppleMobileFileIntegrity early initializer `0xfffffff005ab4ae0`, prelink slide `-0x1000000`, UFS/AES/RAM block archive builders and the 512 MiB payload. Its `--storage-bindings` prelink argument consumes the NativeUfs binding record produced during the same build.

The independent `configs/gts7fewifi_system_defconfig` enables the trust-cache runtime, 4 KiB code-signature support, VerifiedRoot provider, NativeUfs owner-root enforcement and OS/SystemOS/Data mounting. It uses actual SystemOS UUID `8846FE78-2910-4297-AF97-D59BB6F8FC16` and Data seed UUID `09A342FC-CB72-44DE-A10E-B89F14508869`. The source board and console defconfig remain separate from this system profile. Kernel implementation and binding details belong to the root-level XNU and UFS records.

DarwinBoot reads a kernel file up to 512 MiB. It reads the ADT before parsing Mach-O segments, then bounds each segment and the total virtual-address span by `darwinboot-memory-size`. System staging requests a 1 GiB window. `AllocateWindow` obtains a real contiguous conventional-memory allocation with `AllocatePages`; the combined kernel, arguments, ADT, Data seed and trust data must fit before handoff. The physical 6 GiB DRAM envelope is not substituted for that allocation.

The Data seed follows the existing loader interface: `stage-payload.py --data-ramdisk` copies `data.img`, writes `entry.initrd`, and requests the ADT's zeroed `RAMDisk` property. UniXNU reads the file and passes its buffer as `Request.Ramdisk`. DarwinBoot copies it into the allocated window and fills the ADT address and size. The trust bundle uses the kernel's `.trustcache` sidecar and ADT `TrustCache` property. The userdata FAT driver reads requested disk blocks without allocating a full FAT-sized RAM copy.

### Built and deployed artifacts

`logs/build-t733-recovery-system-root-3.status.tsv` records exit code 0 for kernel configuration, compilation, prelink, snapshot, FAT assembly, UEFI compilation, recovery packaging, userdata composition, BOOT packaging and system packaging. The artifacts are:

- `sources/UniXNU-gts7fewifi/build/gts7fewifi-system-root-3-payload.img`, the 512 MiB FAT. Its stage directory is `build/system-root-3-esp` and its ADT is 11,052 bytes.
- `out/t733-system-packages/system-root-3/userdata.img`, the combined image ending at `0x24F400000`.
- `out/t733-system-packages/system-root-3/boot/gts7fewifi-uefi-boot.tar`, the independent header-v3 BOOT package.
- `sources/uefi-gts7fewifi/Build/gts7fewifiPkg/recovery-packages-system-root-3/gts7fewifi-uefi-recovery.tar`, the corresponding recovery package.
- `out/t733-system-packages/system-root-3/gts7fewifi-unixnu-system-sparse.tar`, the system package successfully sent to the device.

The initial raw-userdata transfer returned Loke code 3. The root agent encoded userdata in Android sparse format and then confirmed successful writes of userdata, BOOT and recovery with exit code 0. The operation is recorded in `logs/flash-system-root-3-sparse.log` and `logs/flash-system-root-3-sparse.exit-code`; encoding and packaging logs are `logs/encode-userdata-system-root-3.log` and `logs/package-t733-system-system-root-3-sparse.log`. This records completed deployment. Userdata filesystem discovery, normal BOOT execution and complete system startup remain pending actual screen or log feedback.

## MSC payload update completed, 2026-10-23

The device's Project Silicium Mass Storage application successfully exported UFS LUN0 to macOS. In this connection macOS assigned the complete LUN to `disk16` and userdata to partition 34, `/dev/rdisk16s34`. The application selects the LUN's BlockIo handle through `QcomPkg/Applications/MassStorage/MassStorage.c`; the host sees the existing GPT and its partitions. These disk names describe this connection and must be obtained again after reconnecting.

The backed-up GPT places userdata at LBA 2,320,896 through 31,099,898 inclusive, using 4096-byte blocks. Its byte offset within LUN0 is 9,506,390,016 and its length is 117,878,796,288 bytes. The boot FAT occupies userdata-relative `[0x22F400000, 0x24F400000)`, exactly 536,870,912 bytes. A write through the userdata partition device uses that relative offset; it must not add the LUN's userdata offset a second time.

The root agent confirmed deployment of `sources/UniXNU-gts7fewifi/build/gts7fewifi-system-root-4d-payload.img`: the user's terminal completed the privileged 512 MiB write in 15.824337 seconds, followed by `sync`. The root agent then ran `diskutil eject disk16`, which returned 0, and confirmed completion to the user. BOOT and recovery were not rewritten for this update. The previous application-hosted attempts in `logs/update-msc-system-root-4d.log`, `logs/update-msc-system-root-4d-dd.log` and `logs/update-msc-system-root-4d-authorized.log` record macOS `Operation not permitted`; the successful terminal transfer was reported separately by the user. This records transport and deployment success. Startup of `system-root-4d` remains pending device feedback.

### Restart after an MSC payload update

The successful terminal output is saved in `logs/update-msc-system-root-4d-user-terminal.txt`. After this write, the user exited Mass Storage and entered UniXNU within the same firmware session. UniXNU showed no boot entry while the persistent recovery status still said `userdata FAT filesystem and configuration: Success`. The root agent requested a complete reboot; its result is pending.

The source explains why that status cannot establish a fresh configuration read after MSC:

- `RecoveryPayloadDxe/RecoveryPayload.c` sets `mLoaded` after the userdata configuration opens successfully. Its scan callback immediately returns when `mLoaded` is true, retaining the earlier status text throughout the firmware session.
- `UserdataPayload.c` copies the parent media structure, including MediaId, when it creates the slice. `SliceReset` returns success without refreshing the media or filesystem. `ConnectPayload` calls `ConnectController` on the existing slice and opens the configuration; it does not disconnect and recreate the existing filesystem.
- `QcomPkg/Applications/MassStorage/MassStorage.c` exits by calling `StopDevice`, then unassigns its BlockIo handles. This path does not notify the userdata slice, change its MediaId or reconnect FatDxe. Host writes reach the exported parent LUN while the firmware's already-mounted FAT retains its directory and disk caches.
- `Mu_Basecore/FatPkg/EnhancedFatDxe/Init.c` stores MediaId and initializes directory and disk caches for the mounted volume. UniXNU's `UniXNULib/Config.c` does initialize a fresh configuration on each application entry, trying its own device handle and then other SimpleFileSystem handles. Those reads still use the existing filesystem instance in this session.

After writing the complete FAT, run `sync`, eject the host disk, exit Mass Storage, and select Reboot to start a new firmware session before loading UniXNU. The new session reconstructs the userdata slice and FAT filesystem from disk. Returning directly to UniXNU after MSC is insufficient to refresh these objects. The observed no-entry screen alone does not establish whether the new on-disk configuration failed; the complete-reboot result remains the next device observation.

### Odin sparse cannot preserve the preceding system windows

The source of this conclusion is the original device's Odin application, extracted from the backed-up ABL: `out/t733-failure-analysis/OdinApp-stock.efi`. Its instruction listing is `out/t733-failure-analysis/OdinApp-stock.disassembly.txt`; the disassembly input copy preserves code and data and clears only the debug-directory entry that prevented the tool from reading it. Addresses below are image RVAs.

- The sparse chunk dispatch at `0x2728..0x2750` indexes the four bytes `00 4D 5E 69` at `0x81CCA`. Chunk type `0xCAC3` selects `0x28CC`. That path calls `0x7B0C` for the cumulative-block overflow condition, then `0x297C..0x2980` adds the chunk's block count to the output cursor at state offset `0x68`. It performs no storage write or erase for that chunk.
- The outer download path first checks the sparse magic `0xED26FF3A` at `0x4B48..0x4B5C`. Before parsing its chunks, `0x4B64` calls `0x4D54` with the current partition name. That function resolves the partition BlockIo and handle through `0x14530`, then calls `0xFB90` at `0x4DBC`.
- `0xFB90` calls `0x10518` to obtain `(LastBlock + 1) * BlockSize`, locates the EraseBlock protocol, and invokes its erase method at `0xFC58`. The arguments set LBA to zero at `0xFC48` and the size to the complete partition byte count at `0xFC4C`.

Consequently, a sparse image containing a leading DONT_CARE chunk and only the new FAT would erase the existing SHNGROOT header and OS/SystemOS windows before skipping their positions. FAT updates use the bounded MSC partition write described above. The full sparse image remains suitable for complete system deployment because it supplies the system windows again.

## Completed and remaining work

Completed: the T733 platform build entry, DSC/FDF, memory and configuration library adaptation, community vendor assets and reference DTB import, patched vendor driver selection, GOP and input/storage module integration, firmware volume layout, UniXNU application/registrar/DarwinBoot inclusion, runtime DRAM/reservation/TLMM integration, recovery image packaging inputs from the matching original firmware, recovery-contained FAT loading, both deployment/stock-restore packages, the updated DEBUG firmware/recovery build on macOS, confirmed UniXNU menu startup with `pil-auto`, and physical-device payload loading through the recovery FAT to DarwinBoot's post-ExitBootServices green bar with `ufs-mmio`.

Later device observations have confirmed both menu power-key paths, recovery FAT payload loading, ExitBootServices, the kernel entry and ordinary XNU initialization. The root agent now owns the active native-UFS discovery deployment; kernel and storage observations are recorded in `notes/XNU_BRINGUP.md` and `notes/UFS_IMPLEMENTATION.md` at the workspace root.

The userdata firmware, system FAT, combined userdata image and normal-BOOT package have now been built and deployed successfully as `system-root-3-sparse`. Remaining execution is to observe userdata FAT selection, normal BOOT and recovery fallback behavior, and the full system's startup result. The root agent owns device writes and startup operations. This documentation update changed no runtime source or configuration and performed no tests, additional logic checks, file confirmation hashes or device operations.

## 2026-10-24: Scrollable UniXNU menu through FAT chainloading

The expanded CPU selection menu exposed a rendering defect: selection advanced beyond the visible cards, while `UniXNUUiDrawEntry` rejected every entry whose absolute index exceeded `VisibleRows`. The menu did not change the first displayed entry. On the device, only the first three entries were visible.

The fix resides in `sources/UniXNU/package/UniXNUPkg/Library/UniXNULib/{Internal.h,Menu.c,Ui.c}` relative to the workspace. `FirstVisible` records the first displayed entry. Selection outside the displayed range moves that range and redraws its rows. Portrait cards, landscape rows and text-console rows use the index relative to `FirstVisible`; entry numbers retain their original indices. The existing GOP dimensions, font scale, card dimensions and footer layout determine the visible row count.

The `menu-scroll-13` UEFI, BOOT and recovery artifacts were built, but their BOOT/recovery images were not flashed. The compiled application is retained at `sources/UniXNU-gts7fewifi/package/menu-scroll-13/UniXNU.efi`. The deployed `system-smp-15` FAT includes it as `EFI/UniXNU/UpdatedMenu.efi` and exposes an `Updated menu` entry immediately after `Boot UniXNU`.

The existing firmware menu loads this application through its `type="efi"` path in `Boot.c` using `LoadImage` and `StartImage`. The new application reads the same FAT configuration through its loaded-image device handle. The default remains `xnu` with timeout disabled. `UniXNUDxe` still selects the firmware-volume application first; copying a removable-media boot filename alone does not replace that path.

The coordinator confirmed the `system-smp-15` FAT write and sync. The user entered the updated menu and explicitly confirmed that scrolling works and that the eight-CPU entry was started. Menu scrolling is now confirmed on the physical device; CPU online results are tracked separately in the SMP record.

## 2026-10-24: MSC transfer failure and pending firmware recovery work

The first `system-display-16` FAT write ended after 6 MiB and 118.284192 seconds with `Device not configured`; dd reported seven input records and six output records. After the user disconnected the cable and fully restarted MSC, the retry completed all 536870912 bytes and sync. Workspace logs are `logs/msc-system-display-16-write-fat.jsonl` and `logs/msc-system-display-16-write-fat-retry.jsonl`.

The active USB storage drivers are closed binaries selected by `Platforms/Samsung/gts7fewifiPkg/Include/Vendor.inc`: `Vendor/QcomPkg/Drivers/UsbMsdDxe/UsbMsdDxe.efi` and `Vendor/QcomPkg/Drivers/UsbfnDwc3Dxe/UsbfnDwc3Dxe.efi`. The prior binary analysis found that the MSC state table at 0x77e0 sends Reset/Detach in CBW/Data/CSW states to the invalid-event handler at 0x21b0, while common handlers exist at 0x2210/0x224c. DWC reset clears endpoint state, so continuing the previous MSC request can submit to an unconfigured endpoint. The CBW completion path at 0x300c also parses stale contents after a zero-byte completion; the invalid-CBW path stalls OUT and then attempts to receive another CBW.

Pending firmware work is to handle reset/detach across transfer states, clear outstanding requests, reject stale completion events and wait for endpoint configuration before queuing a new CBW. Invalid CBWs must enter BOT reset recovery. The TX-failure transition to ATTACHED also needs this recovery procedure. Merely redirecting state-table pointers does not establish that request cleanup is complete. The original cause of the completion event with no active transfer remains under investigation.

`Silicon/Qualcomm/QcomPkg/Applications/MassStorage/MassStorage.c` owns only the outer StartDevice/EventHandler/StopDevice loop; the invalid-event handler returns success, so an outer-loop error handler alone cannot repair this defect. The host continues using system dd without eject or whole-disk unmount. No firmware, service or build changes were made for this investigation; display and touch delivery retain priority. Further detail is in workspace `notes/MSC_SERVICE.md`.

## 2026-10-24: msc-recovery-23 built and flashed

`Resources/Scripts/patch_gts7fewifi_usb_msd.py` now generates the board-specific MSC recovery binary while retaining the original vendor EFI. The generator matches original instruction bytes, state-table entries and the unused code area before inserting its dispatcher code. `Platforms/Samsung/gts7fewifiPkg/Vendor/PatchedBinaries/UsbMsdDxe.inf` and `Include/Vendor.inc` select the generated binary.

Reset and detach cancel all five software transfer records and clear their status, accumulated lengths and device configuration before using the original state transition. Bulk completions require a matching state, buffer and completion status. A zero-length or short CBW stalls both bulk endpoints and waits for BOT reset instead of parsing the old command buffer. The underlying UsbfnDwc3Dxe binary remains unchanged; the initial cause of completion events without active transfers has not been established as fixed.

Workspace `logs/build-t733-uefi-msc-recovery-23.status.tsv` records successful patch generation, UEFI build, recovery packaging and BOOT packaging. The recovery package is `Build/gts7fewifiPkg/recovery-packages-msc-recovery-23/gts7fewifi-uefi-recovery.tar`; the BOOT package is workspace `out/t733-uefi-updates/msc-recovery-23/boot/gts7fewifi-uefi-boot.tar`.

The coordinator flashed both BOOT and RECOVERY through the authorized samloader session, which exited 0. Workspace `logs/flash-uefi-msc-recovery-23.log` records the successful close response and reboot command. The user has re-entered MSC. The recovery changes are deployed; transfer stability remains subject to observation during the pending touch24 FAT write. No claim is made that every MSC failure cause has been resolved.

## 2026-10-24: msc-recovery-25 corrects segmented completion matching

The first touch24 FAT write under msc-recovery-23 failed after 24 MiB and about 110.7 seconds. The final dd result supersedes the earlier zero-byte progress display. Workspace `logs/msc-system-touch-24-write-fat.jsonl` records the failure. The user's image `/Users/rigoligo/Downloads/20261024_012554.jpg` shows unsupported SCSI 0x1B commands and empty sense responses, an Ep1/IN completion without an active transfer, and subsequent endpoint-2 configuration and CBW-queue failures.

The first patch compared completion Buffer with the allocation base. Original IssueXfer at 0x35d8 through 0x35f4 instead submits Buffer plus BytesTransferred. The OUT completion path accumulates that offset before submitting remaining data; DWC completion generation at 0x59a4 through 0x59c4 reports the current TRB buffer. The patch could therefore discard legitimate later segments. The generator now compares the submitted address for CBW, data OUT, data IN and CSW records.

After a full MSC restart, the coordinator successfully rewrote all 536870912 bytes of touch24 and synced. This retry preceded deployment of msc-recovery-25 and does not establish that the new correction works on hardware.

The msc-recovery-25 patch, UEFI build and both packaging stages exited 0. The coordinator flashed BOOT and RECOVERY successfully; samloader session 7828 exited 0, and workspace `logs/flash-uefi-msc-recovery-25.log` records close response 0 and reboot. The segmented-completion correction is deployed. Further MSC transfers must establish its runtime behavior; the original DWC completion and endpoint reconfiguration faults are not recorded as fully resolved.

## 2026-10-24: msc-recovery-28 deployed after further failures under 25

The first touch26 FAT write under 25 stopped progressing at 197 MiB and eventually returned ENXIO after 232783872 bytes (222 MiB), about 77.98 seconds. The image `/Users/rigoligo/Downloads/20261024_014005.jpg` shows physical endpoint 3 in state 4, SubmitUrb failure 0x8000000F, EP 0x81 IssueXfer failure and failed CSW submission, followed by ATTACHED rejecting Setup. After a full restart, the second attempt failed at 481296384 bytes (459 MiB) after 14.454 seconds; the user confirmed that the tablet crashed and rebooted itself. Workspace logs are `logs/msc-system-touch-26-write-fat.jsonl` and `logs/msc-system-touch-26-write-fat-retry.jsonl`. A third attempt completed all 536870912 bytes and sync before the subsequent firmware deployment.

The original DWC reset path cancels non-control requests at 0x493c through 0x414c, clears endpoint configuration at 0x496c, and publishes BusReset at 0x4974. Revision 27 removes the five extra AbortTransfer calls from the MSC reset/detach hook and keeps software-record cleanup and state transitions. Revision 27 was built but not flashed. Its change is included in 28; the observed crash has not been uniquely attributed to these repeated cancellation calls.

The endpoint state 4 has a specific meaning in the original DWC implementation. DwcSubmitURB issues STARTTRANSFER at 0x9e38 and stores state 4 at 0x9e4c. The hardware event reader matches its EPCmdCmplt at 0xa5b4 through 0xa5ec; success changes state to 2 at 0xa6e0 and records the transfer resource index. A new CSW submitted while state 4 remains is rejected by the original submission routine.

`Resources/Scripts/patch_gts7fewifi_usb_dwc.py` now generates a board-specific DWC binary, preserving the vendor input and requiring matching original instruction bytes and unused insertion space. `Vendor/PatchedBinaries/UsbfnDwc3Dxe.inf` and `Include/Vendor.inc` select it. The hook at 0x56c8 runs before TRB preparation and active-list insertion. For bulk endpoint 1 in state 4, it invokes the existing hardware event pump at most 128 times. Command completions update state through the original implementation, and ordinary USB messages remain in the original message queue. Configuration changes or an endpoint that does not return to state 1/2 cause DeviceError; the original caller returns the unsubmitted URB to its free list. The patch emits `T733 DWC pending START: ep=N state=N polls=N` when this wait occurs.

The workspace build entry `tools/build-t733-uefi-update.sh` includes the DWC generation stage. All five stages for 28 exited 0. The packages are `Build/gts7fewifiPkg/recovery-packages-msc-recovery-28/gts7fewifi-uefi-recovery.tar` and workspace `out/t733-uefi-updates/msc-recovery-28/boot/gts7fewifi-uefi-boot.tar`. The coordinator flashed BOOT and RECOVERY; samloader session 60911 exited 0, and workspace `logs/flash-uefi-msc-recovery-28.log` records Close response 0 and reboot. Deployment was confirmed to the user.

Revision 28 combines the segmented-buffer correction from 25, software-only reset/detach cleanup from 27, and bounded DWC event processing. The immediate next device action is booting the already-written touch26 system. MSC transfer stability under 28 remains pending its next actual use.

### First actual MSC transfer under 28 succeeded

The first MSC use after deploying firmware 28 completed the `system-touch-29` FAT write in one attempt: 536870912 bytes followed by successful sync. The coordinator confirmed deployment to the user. Workspace `logs/msc-system-touch-29-write-fat.jsonl` records the result. This establishes one complete successful transfer under 28; longer-term transfer stability remains an observation item.

## 2026-10-24: 42a bulk STARTTRANSFER handshake deployed

Firmware 28 continued to lose MSC transfers. The first 41a write failed at 275 MiB and its retry succeeded. The first two 41b attempts ended at 345 MiB and 182 MiB with `Device not configured`; workspace `logs/msc-system-usb-network-41b-write-fat*.jsonl` retains these results.

The vendor event reader at 0xa714 through 0xa730 processes transfer TRBs only in endpoint states 2, 3 and 5. State 4 returns success without processing them, while the outer event pump at 0x4edc through 0x4ee8 still generates a transfer completion and removes the active URB. A transfer completion consumed before software retires START can therefore separate endpoint state from request ownership. The synchronous START path also omitted the resource index and ignored the command-wait timeout result. These binary paths establish concrete defects; the recorded 41a/41b failures have not been uniquely attributed to this event order.

`Resources/Scripts/patch_gts7fewifi_usb_dwc.py` now clears CMDIOC for STARTTRANSFER on physical endpoints 2 and 3 and uses the existing 10 ms DEPCMD handshake. It waits for CMDACT to clear, checks command status, saves the resource index, and then publishes state 2. The outer submit routine inserts the active URB before later event processing reads its TRBs and reports completion. UPDATE and END consume the saved resource index. This replaces revision 28's 128 event-pump calls before the next submission.

A completed command with nonzero status clears the core URB pointer, sets the resource index to 0xff and restores state 1 before returning the original error. A handshake timeout retains the request and prints `T733 DWC START timeout: request retained until transfer or bus reset`. Returning request acceptance lets the outer routine link its URB into the active queue instead of recycling memory still potentially owned by hardware. A later transfer event checks DEPCMD again and proceeds through the original TRB completion path only after CMDACT clears with successful status; otherwise it returns an error before the outer pump can remove the URB. Existing BusReset/Detach cancellation remains responsible for requests abandoned by the host.

The first `msc-start-completion-42` build failed because an AArch64 `tbnz` target exceeded its signed 32 KiB range. The coordinator changed it to a nearby conditional branch followed by an unconditional branch to the distant target. Revision 42 was not deployed. The unified 42a build and packaging completed; workspace `logs/build-t733-uefi-msc-start-completion-42a.status.tsv` records the stages. Packages are `Build/gts7fewifiPkg/recovery-packages-msc-start-completion-42a/gts7fewifi-uefi-recovery.tar` and workspace `out/t733-uefi-updates/msc-start-completion-42a/boot/gts7fewifi-uefi-boot.tar`.

The coordinator flashed 42a BOOT and recovery successfully, with samloader exit 0 recorded in workspace `logs/flash-uefi-msc-start-completion-42a.log`. The tablet subsequently entered 41e XNU and connected CDC. No continuous MSC write has yet run under 42a; transfer stability remains pending hardware observation.

## 2026-10-24: msc-source-43 replaces the vendor USB transport and MSC binaries

The first subsequent 41f FAT write under 42a stopped progressing at 436207616 bytes and ended at 452984832 bytes with `Device not configured`. Workspace `logs/msc-system-usb-network-41f-write-fat.jsonl` records the result. The user's image `/Users/rigoligo/Downloads/20261024_193822.jpg` shows EP1 OUT and IN completions without active transfers, followed by unconfigured physical endpoint 2 and PrepareUrb error `0x8000000c`. The user requested replacement of the Qualcomm USB transfer driver.

Three agents implemented separate source components:

- `/root/msc_transfer_recovery` adapted `Silicon/Samsung/SamsungPkg/Drivers/UsbfnDwc3Dxe/` and owns DSC/FDF integration. The standard `EFI_USBFN_IO_PROTOCOL` implementation waits for endpoint command completion, checks status and saves the resource index before accepting START. Real XFERCOMPLETE events produce transfer results. Synchronous END and removal of queued endpoint completions precede reuse. Command failures and timeouts retain DMA ownership until controller halt succeeds. EP0 handles SET_ADDRESS internally and reports completion of status transfers explicitly submitted by the upper driver.
- `/root/build_flow_cleanup` implemented `Silicon/Samsung/SamsungPkg/Drivers/UsbMsdDxe/`. Its `EFI_USB_MSD_PROTOCOL` retains compatibility with the existing MassStorage application and uses standard UsbFunctionIo for EP1 bulk IN/OUT. BOT validates the 31-byte CBW, processes SCSI commands and sense, and transfers BlockIo data in 128 KiB chunks. Invalid CBWs wait for BOT reset and both clear-halt requests. BusReset/Detach clear request records and gate the next CBW on configuration. Fatal errors stop submissions while the application's key polling remains available.
- `/root/t733_basic_input_research` implemented `Platforms/Samsung/gts7fewifiPkg/Library/Sm7325PlatformUsbfnLib/`, including its C source, INF and `Sm7325Usbfn.dec`. Its Depex waits for the actual UsbConfig protocol. `ConfigUsb(This, 4, 0)` initializes common/device clocks and PHY without dispatching the old transfer stack. After halting the controller, the library gives USB SID `0xe0` its own identity DMA context and preserves UFS CB3. Exit retains the powered PHY, clocks and mapping needed by NativeUsbDebug in XNU.

`Platforms/Samsung/gts7fewifiPkg/Include/Vendor.inc` no longer loads the old UsbfnDwc3Dxe, UsbMsdDxe, UsbDeviceDxe, UsbInitDxe, XhciPciEmulationDxe or XhciDxe modules. UsbConfigDxe and UsbPwrCtrl remain for platform initialization. `Include/DXE.inc` loads the two source drivers outside APRIORI, and `gts7fewifi.dsc` binds the new platform library. `Resources/Scripts/build_gts7fewifi_macos.sh` no longer generates the two USB binary patches. Archived patch sources and vendor inputs remain available.

The coordinator's unified build succeeded. Workspace `logs/build-t733-uefi-msc-source-43.status.tsv` records exit 0 for menu-sources, build-uefi, package-recovery and package-boot. Packages are `Build/gts7fewifiPkg/recovery-packages-msc-source-43/gts7fewifi-uefi-recovery.tar` and workspace `out/t733-uefi-updates/msc-source-43/boot/gts7fewifi-uefi-boot.tar`. BOOT and recovery were flashed successfully; samloader exited 0, recorded in workspace `logs/flash-uefi-msc-source-43.log`. Deployment has been confirmed to the user.

The next device operation is entering the new MSC implementation to write the 44a FAT. Enumeration, complete reads/writes and sustained transfer stability under 43 await device results.

## 2026-10-24: 43 startup failure and deployed 43a correction

The first MSC selection under 43 repeatedly printed `UsbfnMsdEventHandler3: Error Locating EfiUsbMsdPeripheralProtocol`. The FAT transfer did not complete. This handler belongs to the retained `Vendor/PatchedBinaries/UsbConfigDxe.efi`, at RVA `0x5a08`. It locates GUID `c8591faf-dbcc-479e-9ef2-fd085bc37bc7`, which is the same `gEfiUsbMsdProtocolGuid` already produced by the source driver, then calls `EventHandler(This)` through protocol offset `0x18`.

UsbConfig entry calls its initialization at `0x18b8`. That initialization creates a TPL_CALLBACK timer at `0x4038`, signals it at `0x4080`, and sets a 100 ms period at `0x40a8`. Callback `0x4790` calls the old MSC handler ten times through `0x47d4` before continuing platform polling. Successful ConfigUsb increments the gate byte at `0x13959`. In 43, UsbfnPlatformGetData initialized hardware before USBFN/MSD protocol installation and enabled this callback too early. After protocol installation, the vendor timer would also enter the new MSC event handler independently of the application.

Revision 43a adds `Resources/Scripts/patch_gts7fewifi_usb_config.py` to the firmware build. It matches instruction `0x9400048d` at file/RVA `0x47d4` and replaces that call with NOP `0xd503201f`. The platform timer and its remaining polling continue; MSC event dispatch belongs to the application. `Platforms/Samsung/gts7fewifiPkg/Library/Sm7325PlatformUsbfnLib/PlatformUsbfnLib.c` now returns parameters from GetData and defers hardware initialization to PhyInit. `UsbfnDwc3Dxe.c` refreshes GetData after PhyInit in StartController to obtain actual PHY parameters. The coordinator added USBFN/MSD entry and protocol-installation diagnostics. `Silicon/Qualcomm/QcomPkg/Applications/MassStorage/MassStorage.c` displays startup or event-processing failures and waits for Volume Up to return.

Workspace `logs/build-t733-uefi-msc-source-43a.status.tsv` records all four build/package stages exiting 0. The coordinator flashed BOOT and recovery successfully; samloader exited 0 in `logs/flash-uefi-msc-source-43a.log`, and deployment was confirmed to the user. Entry into MSC and actual transfers under 43a are pending. Sustained transfer stability remains unconfirmed.

## 2026-10-24: 43a EP0 resource failure and deployed 43b correction

The 43a device image `/Users/rigoligo/Downloads/20261024_200415.jpg` shows completed PHY initialization followed by `Endpoint Command 0x6 on Physical EP0 failed Status1`. The STARTTRANSFER diagnostic identifies setup TRB type `0x20`, length 8, resource index 0 and EP0 setup state. SourceMsd reports Device Error and the menu displays event-processing failure. The local Samsung Linux reference at workspace `references/android_kernel_samsung_sm7325/drivers/usb/dwc3/core.h` defines command status 1 as `DEPEVT_TRANSFER_NO_RESOURCE`.

The previous BusReset path ended and immediately restarted EP0 setup unconditionally. Both the device's Linux reset handler and working NativeUsbDebug retain an EP0 request already receiving SETUP. Revision 43b changes RESET in `Dwc3Endpoint.c` to preserve the setup TRB and resource when EP0 is in setup state with an active OUT request. Ordinary cancellation starts at physical EP2. A control transfer outside setup is explicitly ended in both directions before stall/rearm. CONNECT_DONE modifies already enabled EP0 and bulk endpoint configurations and handles command failures, preserving existing resources.

END now has an explicit retirement condition. The Linux reference documents that CMDACT polling guarantees END completion only on supported DWC_usb3 revisions with `GUCTL2[14]` enabled; DWC_usb31 lacks that mode. Revision 43a had not established that condition. In 43b, `Dwc3EndTransfer` sends CMDIOC and waits for a successful EPCMDCMPLT matching the physical endpoint and END command before clearing its resource index, active state and TRB. Errors or timeouts retain ownership and mark the controller Faulted.

The wait reads events without recursively dispatching them. Other endpoints and bus events retain their order in `DWC3_DEV.DeferredEvents`; old events for the cancelled endpoint are removed when END retires. `Dwc3ProcessEvents` drains deferred events first, then reads and acknowledges individual hardware events. Its previous Count snapshot is removed so events consumed while a handler waits for END cannot be read or acknowledged twice. Event-buffer setup resets the deferred queue, and AbortTransfer drains both sources.

Workspace `logs/build-t733-uefi-msc-source-43b.status.tsv` records successful completion of all four build/package stages. The coordinator flashed BOOT and recovery with samloader exit 0, recorded in `logs/flash-uefi-msc-source-43b.log`, and confirmed deployment to the user. Entry into 43b MSC and complete transfers are pending; transfer stability awaits device results.

### First complete transfer under 43b succeeded

The first physical-device MSC transfer under 43b wrote the entire `system-native-debug-44a` FAT through `mscctl write-fat` in one attempt: 536870912 bytes followed by successful sync and final `ok: true`. Workspace `logs/msc-system-native-debug-44a-write-fat.jsonl` records the result, and the coordinator confirmed completion to the user. This establishes the source stack's first complete 512 MiB write. Long-term sustained transfer stability remains unfinished.

## 2026-10-24: 43c USB handoff correction deployed

After a full restart under 43b, the user confirmed that 44a reached the desktop, while the host saw neither CDC nor ECM. Entering MSC before starting XNU also left USB absent. Source inspection identified two handoff gaps: GetData only returned configuration, so a direct XNU boot never called the PHY initialization owned by MSC StartController; after an MSC session, StopController called Dwc3CoreExit, which set USB2 SUSPHY. NativeUsbDebug requires firmware-provided power and clocks and checks that GCTL selects device mode before proceeding.

Revision 43c changes `Silicon/Samsung/SamsungPkg/Drivers/UsbfnDwc3Dxe/UsbfnDwc3Dxe.c` to call PhyInit after installing the USBFN protocol, then refresh the actual platform parameters through GetData. Initialization failures are logged and the installed protocol remains available for a later StartController retry. `Platforms/Samsung/gts7fewifiPkg/Library/Sm7325PlatformUsbfnLib/PlatformUsbfnLib.c` selects GCTL device mode after halting the controller and clears GUSB2PHYCFG SUSPHY/ENBLSLPM. PhyExit also clears these two USB2 bits after MSC cleanup. The controller remains halted; endpoint transfers start only when requested by the MSC application.

The retained UsbConfig binary registers ExitBootServices, but its imported patches skip core shutdown when the callback passes argument 1 and bypass USB0/USB1 resource-release calls. No additional EBS binary modification was needed in this revision.

Workspace `logs/build-t733-uefi-msc-source-43c.status.tsv` records all four build/package stages exiting 0. The coordinator flashed BOOT and recovery; samloader exited 0 and sent reboot, recorded in `logs/flash-uefi-msc-source-43c.log`. Deployment has been confirmed. CDC/ECM enumeration during the new 43c boot remains pending.

### 43c device results and pending removal of the old USB startup notification

CDC/ECM now enumerate under 43c. The Mac exposes en9, and workspace `logs/t733-usb-live/t733-20261024T121906.725788Z.raw` records the new CDC session. The subsequent 44b FAT update completed all 512 MiB in one attempt and synced successfully. The coordinator is continuing native debugging on 44b.

The menu's `UsbStartController` and `UsbConfigStartControllerCb` Not Found messages originate from BootManager's old `gUsbControllerInitGuid` notification. UsbConfig callback `0x1744` calls `UsbStartController(0,4)`, which attempts ConnectController on the vendor peripheral handle at `0x45e0`. The associated vendor DriverBinding has been removed; the source USBFN driver installs its protocol directly.

`Silicon/Qualcomm/QcomPkg/Library/PlatformBootManagerLib/PlatformBootManager.c` now returns immediately from `DeviceBootManagerOnDemandConInConnect` when `T733_USERDATA_PAYLOAD` is defined. The T733 DSC always defines that existing macro, including when its value is zero, so both board payload configurations skip creation and signaling of the obsolete notification. Other platforms retain the existing behavior. This source change is pending the next necessary firmware build and deployment; no separate build or flash was performed. Long-term MSC transfer stability remains under observation.
