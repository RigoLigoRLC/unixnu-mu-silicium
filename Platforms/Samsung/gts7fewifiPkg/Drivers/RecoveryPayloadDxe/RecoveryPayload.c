#include <Uefi.h>
#include <Guid/T733UfsDispatch.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DevicePath.h>
#include <Protocol/PartitionInfo.h>
#include <Protocol/RamDisk.h>
#include <Protocol/SimpleFileSystem.h>
#include "UserdataPayload.h"

#define PAYLOAD_OFFSET  0x00800000U
#define PAYLOAD_SIZE    0x04000000U

STATIC EFI_RAM_DISK_PROTOCOL  *mRamDisk;
STATIC VOID                  *mBlockIoRegistration;
STATIC VOID                  *mRamDiskRegistration;
STATIC BOOLEAN               mLoading;
STATIC BOOLEAN               mLoaded;
#if T733_USERDATA_PAYLOAD
STATIC BOOLEAN               mAllowRecovery;
#endif
STATIC CHAR16                mPayloadStatus[192];
STATIC EFI_GUID              mPayloadStatusGuid = {
  0x71A8180B, 0xB15B, 0x470A, {0x91, 0x88, 0x8C, 0xD4, 0xA8, 0x04, 0x44, 0x16}
};
STATIC EFI_GUID              mUfsDispatchGuid = T733_UFS_DISPATCH_GUID;

STATIC
VOID
ReportNoBlockIo (
  VOID
  )
{
  EFI_STATUS         Status;
  T733_UFS_DISPATCH  *Dispatch;

  Status = gBS->LocateProtocol (&mUfsDispatchGuid, NULL, (VOID **)&Dispatch);
  if (EFI_ERROR (Status)) {
    StrCpyS (mPayloadStatus, ARRAY_SIZE (mPayloadStatus),
             L"Recovery: BlockIO=0; UFSDxe dispatch record missing");
  } else if (Dispatch->Phase < T733_UFS_STARTING) {
    UnicodeSPrint (
      mPayloadStatus, sizeof (mPayloadStatus),
      L"Recovery: BlockIO=0; UFSDxe phase=%u load=%r; entry not reached",
      Dispatch->Phase, Dispatch->LoadStatus
      );
  } else {
    UnicodeSPrint (
      mPayloadStatus, sizeof (mPayloadStatus),
      L"Recovery: BlockIO=0; UFSDxe load=%r start=%r; IMEM=%08x/v%u/dev%u",
      Dispatch->LoadStatus, Dispatch->StartStatus,
      Dispatch->BootMagic, Dispatch->BootVersion, Dispatch->BootDevice
      );
  }

  DEBUG ((DEBUG_WARN, "T733: %s\n", mPayloadStatus));
  if (gST->ConOut != NULL) {
    Print (L"%s\r\n", mPayloadStatus);
  }
}

STATIC
VOID
ReportPayloadStatus (
  IN CONST CHAR16 *Stage,
  IN EFI_STATUS   Status
  )
{
  CHAR16 Message[192];

  UnicodeSPrint (Message, sizeof (Message), L"Recovery: %s: %r", Stage, Status);
  if (StrCmp (Message, mPayloadStatus) == 0) {
    return;
  }

  StrCpyS (mPayloadStatus, ARRAY_SIZE (mPayloadStatus), Message);
  DEBUG ((DEBUG_WARN, "T733: %s: %r\n", Stage, Status));
  if (gST->ConOut != NULL) {
    Print (L"T733: %s: %r\r\n", Stage, Status);
  }
}

// The registered RAM disk retains its pages until the payload loader exits
// boot services. FAT writes affect that RAM copy, leaving recovery unchanged.
STATIC
EFI_STATUS
LoadRecoveryPayload (
  IN EFI_BLOCK_IO_PROTOCOL *BlockIo
  )
{
  EFI_STATUS                Status;
  EFI_BLOCK_IO_MEDIA        *Media;
  EFI_DEVICE_PATH_PROTOCOL  *DevicePath;
  EFI_DEVICE_PATH_PROTOCOL  *RemainingPath;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
  EFI_HANDLE                RamDiskHandle;
  UINTN                     Alignment;
  UINT8                     *Buffer;

  Media = BlockIo->Media;
  if (!Media->MediaPresent || !Media->LogicalPartition ||
      Media->BlockSize == 0 ||
      (PAYLOAD_OFFSET % Media->BlockSize) != 0 ||
      (PAYLOAD_SIZE % Media->BlockSize) != 0 ||
      Media->LastBlock < ((PAYLOAD_OFFSET + PAYLOAD_SIZE) / Media->BlockSize) - 1)
  {
    ReportPayloadStatus (L"recovery media geometry", EFI_UNSUPPORTED);
    return EFI_UNSUPPORTED;
  }

  Alignment = MAX ((UINTN)Media->IoAlign, EFI_PAGE_SIZE);
  Buffer = AllocateAlignedPages (EFI_SIZE_TO_PAGES (PAYLOAD_SIZE), Alignment);
  if (Buffer == NULL) {
    ReportPayloadStatus (L"allocate 64 MiB recovery FAT buffer", EFI_OUT_OF_RESOURCES);
    return EFI_OUT_OF_RESOURCES;
  }

  DEBUG ((DEBUG_WARN, "T733: reading recovery offset 0x%x size 0x%x, block size %u\n",
          PAYLOAD_OFFSET, PAYLOAD_SIZE, Media->BlockSize));
  if (gST->ConOut != NULL) {
    Print (L"T733: reading 64 MiB FAT at recovery offset 8 MiB\r\n");
  }

  Status = BlockIo->ReadBlocks (
                     BlockIo,
                     Media->MediaId,
                     PAYLOAD_OFFSET / Media->BlockSize,
                     PAYLOAD_SIZE,
                     Buffer
                     );
  if (EFI_ERROR (Status)) {
    ReportPayloadStatus (L"read recovery FAT", Status);
    goto Failed;
  }

  // The board's packager places a FAT32 volume labelled T733ESP here.
  if (Buffer[510] != 0x55 || Buffer[511] != 0xAA ||
      CompareMem (Buffer + 71, "T733ESP    ", 11) != 0 ||
      CompareMem (Buffer + 82, "FAT32   ", 8) != 0)
  {
    Status = EFI_NOT_FOUND;
    ReportPayloadStatus (L"recognize T733ESP FAT32 boot sector", Status);
    goto Failed;
  }

  ReportPayloadStatus (L"read and recognize recovery FAT", EFI_SUCCESS);
  Status = mRamDisk->Register (
                      (UINTN)Buffer,
                      PAYLOAD_SIZE,
                      &gEfiVirtualDiskGuid,
                      NULL,
                      &DevicePath
                      );
  if (EFI_ERROR (Status)) {
    ReportPayloadStatus (L"register recovery RAM disk", Status);
    goto Failed;
  }

  ReportPayloadStatus (L"register recovery RAM disk", EFI_SUCCESS);
  DEBUG ((DEBUG_WARN, "T733: recovery FAT registered at %p, size 0x%x\n", Buffer, PAYLOAD_SIZE));

  RemainingPath = DevicePath;
  Status = gBS->LocateDevicePath (&gEfiBlockIoProtocolGuid, &RemainingPath, &RamDiskHandle);
  if (!EFI_ERROR (Status)) {
    Status = gBS->HandleProtocol (
                    RamDiskHandle,
                    &gEfiSimpleFileSystemProtocolGuid,
                    (VOID **)&FileSystem
                    );
  }

  ReportPayloadStatus (L"RAM disk FAT filesystem", Status);
  return EFI_SUCCESS;

Failed:
  FreeAlignedPages (Buffer, EFI_SIZE_TO_PAGES (PAYLOAD_SIZE));
  return Status;
}

STATIC
VOID
EFIAPI
OnPayloadInputsInstalled (
  IN EFI_EVENT Event,
  IN VOID      *Context
  )
{
  EFI_STATUS                   Status;
  EFI_HANDLE                   *Handles;
  UINTN                        Count;
  UINTN                        BlockIoCount;
  UINTN                        GptCount;
  BOOLEAN                      FoundRecovery;
#if T733_USERDATA_PAYLOAD
  BOOLEAN                      FoundUserdata;
#endif
  EFI_PARTITION_INFO_PROTOCOL   *Partition;
  EFI_BLOCK_IO_PROTOCOL         *BlockIo;

  if (mLoading || mLoaded) {
    return;
  }

  mLoading = TRUE;
#if !T733_USERDATA_PAYLOAD
  Status = gBS->LocateProtocol (&gEfiRamDiskProtocolGuid, NULL, (VOID **)&mRamDisk);
  if (EFI_ERROR (Status)) {
    ReportPayloadStatus (L"RAM disk protocol not installed", Status);
    mLoading = FALSE;
    return;
  }
#endif

  BlockIoCount = 0;
  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiBlockIoProtocolGuid,
                  NULL,
                  &BlockIoCount,
                  &Handles
                  );
  if (!EFI_ERROR (Status)) {
    FreePool (Handles);
  }

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiPartitionInfoProtocolGuid,
                  NULL,
                  &Count,
                  &Handles
                  );
  if (EFI_ERROR (Status)) {
    if (BlockIoCount == 0) {
      ReportNoBlockIo ();
    } else {
      UnicodeSPrint (
        mPayloadStatus, sizeof (mPayloadStatus),
        L"Recovery: %u Block I/O handles, no partition-info handles (%r)",
        (UINT32)BlockIoCount, Status
        );
    }
    mLoading = FALSE;
    return;
  }

#if T733_USERDATA_PAYLOAD
  // Storage enumeration can publish recovery before userdata. Recovery waits
  // for ReadyToBoot and for userdata's handle before choosing the fallback.
  FoundUserdata = FALSE;
  for (UINTN Index = 0; Index < Count; Index++) {
    Status = gBS->HandleProtocol (
                    Handles[Index], &gEfiPartitionInfoProtocolGuid,
                    (VOID **)&Partition
                    );
    if (EFI_ERROR (Status) || Partition->Type != PARTITION_TYPE_GPT ||
        CompareMem (Partition->Info.Gpt.PartitionName, L"userdata", sizeof (L"userdata")) != 0)
    {
      continue;
    }

    FoundUserdata = TRUE;
    Status = LoadUserdataPayload (Handles[Index]);
    ReportPayloadStatus (L"userdata FAT filesystem and configuration", Status);
    if (!EFI_ERROR (Status)) {
      mLoaded = TRUE;
      break;
    }
  }

  if (mLoaded || !mAllowRecovery || !FoundUserdata) {
    FreePool (Handles);
    mLoading = FALSE;
    return;
  }

  Status = gBS->LocateProtocol (&gEfiRamDiskProtocolGuid, NULL, (VOID **)&mRamDisk);
  if (EFI_ERROR (Status)) {
    ReportPayloadStatus (L"RAM disk protocol not installed for recovery fallback", Status);
    FreePool (Handles);
    mLoading = FALSE;
    return;
  }
#endif

  GptCount = 0;
  FoundRecovery = FALSE;
  for (UINTN Index = 0; Index < Count; Index++) {
    Status = gBS->HandleProtocol (
                    Handles[Index],
                    &gEfiPartitionInfoProtocolGuid,
                    (VOID **)&Partition
                    );
    if (EFI_ERROR (Status) || Partition->Type != PARTITION_TYPE_GPT) {
      continue;
    }

    GptCount++;
    if (CompareMem (Partition->Info.Gpt.PartitionName, L"recovery", sizeof (L"recovery")) != 0) {
      continue;
    }

    FoundRecovery = TRUE;
    Status = gBS->HandleProtocol (
                    Handles[Index],
                    &gEfiBlockIoProtocolGuid,
                    (VOID **)&BlockIo
                    );
    if (EFI_ERROR (Status)) {
      ReportPayloadStatus (L"open recovery Block I/O", Status);
      continue;
    }

    ReportPayloadStatus (L"locate GPT recovery partition", EFI_SUCCESS);
    Status = LoadRecoveryPayload (BlockIo);
    if (!EFI_ERROR (Status)) {
      mLoaded = TRUE;
      break;
    }

    DEBUG ((DEBUG_WARN, "T733: recovery FAT load returned %r\n", Status));
  }

  if (!FoundRecovery) {
    UnicodeSPrint (
      mPayloadStatus, sizeof (mPayloadStatus),
      L"Recovery: %u Block I/O handles, %u GPT partitions; recovery not found",
      (UINT32)BlockIoCount, (UINT32)GptCount
      );
  }

  FreePool (Handles);
  mLoading = FALSE;
}

#if T733_USERDATA_PAYLOAD
STATIC
VOID
EFIAPI
OnReadyToBoot (
  IN EFI_EVENT Event,
  IN VOID      *Context
  )
{
  mAllowRecovery = TRUE;
  OnPayloadInputsInstalled (Event, Context);
}
#endif

EFI_STATUS
EFIAPI
RecoveryPayloadEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  EFI_EVENT   Event;
  EFI_HANDLE  StatusHandle;

  // UniXNU reads this persistent text when no boot entry is available.
  StatusHandle = NULL;
#if T733_USERDATA_PAYLOAD
  StrCpyS (mPayloadStatus, ARRAY_SIZE (mPayloadStatus), L"Recovery: waiting for userdata storage; recovery fallback at ReadyToBoot");
#else
  StrCpyS (mPayloadStatus, ARRAY_SIZE (mPayloadStatus), L"Recovery: waiting for RAM disk and storage protocols");
#endif
  Status = gBS->InstallProtocolInterface (
                  &StatusHandle,
                  &mPayloadStatusGuid,
                  EFI_NATIVE_INTERFACE,
                  mPayloadStatus
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  ReportPayloadStatus (L"recovery payload driver started", EFI_SUCCESS);
  Event = EfiCreateProtocolNotifyEvent (
            &gEfiBlockIoProtocolGuid,
            TPL_CALLBACK,
            OnPayloadInputsInstalled,
            NULL,
            &mBlockIoRegistration
            );
  if (Event == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Event = EfiCreateProtocolNotifyEvent (
            &gEfiRamDiskProtocolGuid,
            TPL_CALLBACK,
            OnPayloadInputsInstalled,
            NULL,
            &mRamDiskRegistration
            );
  if (Event == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

#if T733_USERDATA_PAYLOAD
  return EfiCreateEventReadyToBootEx (TPL_CALLBACK, OnReadyToBoot, NULL, &Event);
#else
  return EFI_SUCCESS;
#endif
}
