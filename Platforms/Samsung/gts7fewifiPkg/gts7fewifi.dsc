# SPDX-License-Identifier: BSD-2-Clause-Patent

[Defines]
  PLATFORM_NAME             = gts7fewifi
  PLATFORM_GUID             = A6F2641E-FA0D-4D06-98E5-13E8706A73C3
  PLATFORM_VERSION          = 0.1
  DSC_SPECIFICATION         = 0x0001001C
  OUTPUT_DIRECTORY          = Build/gts7fewifiPkg
  SUPPORTED_ARCHITECTURES   = AARCH64
  BUILD_TARGETS             = RELEASE|DEBUG
  SKUID_IDENTIFIER          = DEFAULT
  FLASH_DEFINITION          = gts7fewifiPkg/gts7fewifi.fdf
  USE_CUSTOM_DISPLAY_DRIVER = 0
  SOC_TYPE                  = 0
!ifndef T733_USERDATA_PAYLOAD
  DEFINE T733_USERDATA_PAYLOAD = 0
!endif

!include KodiakPkg/KodiakPkg.dsc.inc

[BuildOptions]
  *_*_AARCH64_CC_FLAGS = -march=armv8.2-a+crypto+rcpc -DT733_VENDOR_POWER_KEY -DT733_UFS_DIAGNOSTICS -DT733_USERDATA_PAYLOAD=$(T733_USERDATA_PAYLOAD)

[PcdsFixedAtBuild]
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x80000000
  gArmPlatformTokenSpaceGuid.PcdCPUCoresStackBase|0x9FF90000
  gArmPlatformTokenSpaceGuid.PcdCPUCorePrimaryStackSize|0x40000

  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemManufacturer|"Samsung"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemModel|"Galaxy Tab S7 FE Wi-Fi"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailModel|"gts7fewifi"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailSku|"SM-T733"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemBoardModel|"Galaxy Tab S7 FE"

  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferWidth|1600
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferHeight|2560
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferColorDepth|32

  gQcomPkgTokenSpaceGuid.PcdPlatformType|"LA"
  gQcomPkgTokenSpaceGuid.PcdStorageIsUfs3|FALSE

[LibraryClasses]
  PlatformSecLib|gts7fewifiPkg/Library/PlatformSecLib/PlatformSecLib.inf
  MemoryMapLib|gts7fewifiPkg/Library/MemoryMapLib/MemoryMapLib.inf
  ConfigurationMapLib|gts7fewifiPkg/Library/ConfigurationMapLib/ConfigurationMapLib.inf
  UniXNULib|UniXNUPkg/Library/UniXNULib/UniXNULib.inf
  FileExplorerLib|MdeModulePkg/Library/FileExplorerLib/FileExplorerLib.inf

[Components]
  MdeModulePkg/Universal/Disk/RamDiskDxe/RamDiskDxe.inf
  gts7fewifiPkg/Drivers/RecoveryPayloadDxe/RecoveryPayloadDxe.inf
  UniXNUPkg/Application/UniXNU/UniXNU.inf
  UniXNUPkg/Driver/UniXNUDxe/UniXNUDxe.inf
  DarwinBootPkg/Driver/DarwinBoot.inf
