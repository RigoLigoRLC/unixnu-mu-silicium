#include "UserdataPayload.h"

#if T733_USERDATA_PAYLOAD

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DevicePath.h>
#include <Protocol/SimpleFileSystem.h>

#define USERDATA_SLICE_SIGNATURE  SIGNATURE_32 ('T', '7', 'F', 'S')
#define ROOT_HEADER_SIZE          4096U
#define ROOT_ALIGNMENT            0x100000ULL

// USERDATA_SLICE exposes the FAT following the SHNGROOT system windows.
// Reads retain the parent UFS block size; DiskIo supplies byte-sized FAT reads.
typedef struct {
  UINT32                    Signature;
  EFI_BLOCK_IO_PROTOCOL     BlockIo;
  EFI_BLOCK_IO_MEDIA        Media;
  EFI_BLOCK_IO_PROTOCOL     *Parent;
  EFI_LBA                   Start;
  EFI_HANDLE                Handle;
  EFI_DEVICE_PATH_PROTOCOL  *DevicePath;
} USERDATA_SLICE;

STATIC USERDATA_SLICE *mSlice;
STATIC EFI_GUID mSliceGuid = {
  0x908F50D1, 0x68B8, 0x475B, {0x84, 0xBD, 0x10, 0x0F, 0x6A, 0x3C, 0x7B, 0xD2}
};

STATIC
EFI_STATUS
EFIAPI
SliceReset (
  IN EFI_BLOCK_IO_PROTOCOL *This,
  IN BOOLEAN              ExtendedVerification
  )
{
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
SliceRead (
  IN EFI_BLOCK_IO_PROTOCOL *This,
  IN UINT32               MediaId,
  IN EFI_LBA              Lba,
  IN UINTN                BufferSize,
  OUT VOID                *Buffer
  )
{
  USERDATA_SLICE *Slice;
  UINTN         Blocks;

  Slice = CR (This, USERDATA_SLICE, BlockIo, USERDATA_SLICE_SIGNATURE);
  if (MediaId != Slice->Media.MediaId) {
    return EFI_MEDIA_CHANGED;
  }

  if (BufferSize == 0) {
    return EFI_SUCCESS;
  }

  if (Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if ((BufferSize % Slice->Media.BlockSize) != 0) {
    return EFI_BAD_BUFFER_SIZE;
  }

  Blocks = BufferSize / Slice->Media.BlockSize;
  if (Lba > Slice->Media.LastBlock || Blocks > Slice->Media.LastBlock - Lba + 1) {
    return EFI_INVALID_PARAMETER;
  }

  return Slice->Parent->ReadBlocks (
                          Slice->Parent, MediaId, Slice->Start + Lba,
                          BufferSize, Buffer
                          );
}

STATIC
EFI_STATUS
EFIAPI
SliceWrite (
  IN EFI_BLOCK_IO_PROTOCOL *This,
  IN UINT32               MediaId,
  IN EFI_LBA              Lba,
  IN UINTN                BufferSize,
  IN VOID                 *Buffer
  )
{
  return EFI_WRITE_PROTECTED;
}

STATIC
EFI_STATUS
EFIAPI
SliceFlush (
  IN EFI_BLOCK_IO_PROTOCOL *This
  )
{
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
ConnectPayload (
  VOID
  )
{
  EFI_STATUS                      Status;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
  EFI_FILE_PROTOCOL               *Root;
  EFI_FILE_PROTOCOL               *Config;

  gBS->ConnectController (mSlice->Handle, NULL, NULL, TRUE);
  Status = gBS->HandleProtocol (
                  mSlice->Handle, &gEfiSimpleFileSystemProtocolGuid,
                  (VOID **)&FileSystem
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = FileSystem->OpenVolume (FileSystem, &Root);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = Root->Open (Root, &Config, L"EFI\\UniXNU\\unixnu.toml", EFI_FILE_MODE_READ, 0);
  if (!EFI_ERROR (Status)) {
    Config->Close (Config);
  }

  Root->Close (Root);
  return Status;
}

EFI_STATUS
LoadUserdataPayload (
  IN EFI_HANDLE PartitionHandle
  )
{
  EFI_STATUS            Status;
  EFI_BLOCK_IO_PROTOCOL *Parent;
  EFI_BLOCK_IO_MEDIA    *Media;
  UINT8                 *Header;
  UINT8                 *Entry;
  UINT32                Count;
  UINT64                ImageBytes;
  UINT64                PartitionBytes;
  UINT64                Offset;
  UINT64                Size;
  UINT64                FatOffset;
  UINT64                FatSize;
  UINT32                SectorSize;
  VENDOR_DEVICE_PATH    Node;

  if (mSlice != NULL) {
    return ConnectPayload ();
  }

  Status = gBS->HandleProtocol (PartitionHandle, &gEfiBlockIoProtocolGuid, (VOID **)&Parent);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Media = Parent->Media;
  if (!Media->MediaPresent || !Media->LogicalPartition || Media->BlockSize != 4096 ||
      Media->LastBlock >= MAX_UINT64 / Media->BlockSize)
  {
    return EFI_UNSUPPORTED;
  }

  Header = AllocateAlignedPages (1, MAX ((UINTN)Media->IoAlign, EFI_PAGE_SIZE));
  if (Header == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = Parent->ReadBlocks (Parent, Media->MediaId, 0, ROOT_HEADER_SIZE, Header);
  if (EFI_ERROR (Status)) {
    goto Done;
  }

  Status = EFI_NOT_FOUND;
  if (CompareMem (Header, "SHNGROOT", 8) != 0) {
    goto Done;
  }

  Status = EFI_VOLUME_CORRUPTED;
  Count = ReadUnaligned32 ((UINT32 *)(Header + 12));
  ImageBytes = ReadUnaligned64 ((UINT64 *)(Header + 16));
  PartitionBytes = (Media->LastBlock + 1) * Media->BlockSize;
  if (ReadUnaligned32 ((UINT32 *)(Header + 8)) != 1 || Count == 0 || Count > 4 ||
      ImageBytes < ROOT_HEADER_SIZE || ImageBytes > PartitionBytes ||
      (ImageBytes % Media->BlockSize) != 0)
  {
    goto Done;
  }

  FatOffset = ROOT_ALIGNMENT;
  for (UINT32 Index = 0; Index < Count; Index++) {
    Entry = Header + 24 + Index * 128;
    Offset = ReadUnaligned64 ((UINT64 *)(Entry + 32));
    Size = ReadUnaligned64 ((UINT64 *)(Entry + 40));
    if (Offset < ROOT_ALIGNMENT || Offset >= ImageBytes || Size == 0 ||
        Size > ImageBytes - Offset || (Offset % Media->BlockSize) != 0 ||
        (Size % 512) != 0 || ReadUnaligned32 ((UINT32 *)(Entry + 48)) != 512 ||
        ReadUnaligned32 ((UINT32 *)(Entry + 52)) != 1)
    {
      goto Done;
    }

    FatOffset = MAX (FatOffset, Offset + Size);
  }

  if (FatOffset > MAX_UINT64 - (ROOT_ALIGNMENT - 1)) {
    goto Done;
  }

  FatOffset = (FatOffset + ROOT_ALIGNMENT - 1) & ~(ROOT_ALIGNMENT - 1);
  Status = EFI_NOT_FOUND;
  if (FatOffset >= ImageBytes || ImageBytes - FatOffset < Media->BlockSize) {
    goto Done;
  }

  Status = Parent->ReadBlocks (
                     Parent, Media->MediaId, FatOffset / Media->BlockSize,
                     ROOT_HEADER_SIZE, Header
                     );
  if (EFI_ERROR (Status)) {
    goto Done;
  }

  Status = EFI_VOLUME_CORRUPTED;
  SectorSize = ReadUnaligned16 ((UINT16 *)(Header + 11));
  FatSize = (UINT64)ReadUnaligned32 ((UINT32 *)(Header + 32)) * SectorSize;
  if (Header[510] != 0x55 || Header[511] != 0xAA ||
      CompareMem (Header + 71, "T733ESP    ", 11) != 0 ||
      CompareMem (Header + 82, "FAT32   ", 8) != 0 ||
      SectorSize != 512 || FatSize == 0 || FatSize > ImageBytes - FatOffset ||
      (FatSize % Media->BlockSize) != 0)
  {
    goto Done;
  }

  mSlice = AllocateZeroPool (sizeof (*mSlice));
  if (mSlice == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Done;
  }

  mSlice->Signature = USERDATA_SLICE_SIGNATURE;
  mSlice->Parent = Parent;
  mSlice->Start = FatOffset / Media->BlockSize;
  CopyMem (&mSlice->Media, Media, sizeof (*Media));
  mSlice->Media.LastBlock = FatSize / Media->BlockSize - 1;
  mSlice->Media.LogicalPartition = FALSE;
  mSlice->Media.ReadOnly = TRUE;
  mSlice->BlockIo.Revision = EFI_BLOCK_IO_PROTOCOL_REVISION;
  mSlice->BlockIo.Media = &mSlice->Media;
  mSlice->BlockIo.Reset = SliceReset;
  mSlice->BlockIo.ReadBlocks = SliceRead;
  mSlice->BlockIo.WriteBlocks = SliceWrite;
  mSlice->BlockIo.FlushBlocks = SliceFlush;

  ZeroMem (&Node, sizeof (Node));
  Node.Header.Type = MEDIA_DEVICE_PATH;
  Node.Header.SubType = MEDIA_VENDOR_DP;
  SetDevicePathNodeLength (&Node.Header, sizeof (Node));
  CopyGuid (&Node.Guid, &mSliceGuid);
  mSlice->DevicePath = AppendDevicePathNode (DevicePathFromHandle (PartitionHandle), &Node.Header);
  if (mSlice->DevicePath == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
  } else {
    Status = gBS->InstallMultipleProtocolInterfaces (
                    &mSlice->Handle,
                    &gEfiDevicePathProtocolGuid, mSlice->DevicePath,
                    &gEfiBlockIoProtocolGuid, &mSlice->BlockIo,
                    NULL
                    );
  }

  if (EFI_ERROR (Status)) {
    if (mSlice->DevicePath != NULL) {
      FreePool (mSlice->DevicePath);
    }

    FreePool (mSlice);
    mSlice = NULL;
    goto Done;
  }

  Status = ConnectPayload ();

Done:
  FreeAlignedPages (Header, 1);
  return Status;
}

#endif
