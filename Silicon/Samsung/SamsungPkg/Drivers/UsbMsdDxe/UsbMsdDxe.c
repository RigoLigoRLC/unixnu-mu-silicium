/** @file
  USB mass storage transport for the firmware's assigned BlockIo devices.

  The menu assigns whole-device BlockIo instances and polls EventHandler. This
  driver owns BOT command sequencing and synchronous storage requests; USBFN
  owns endpoint DMA and reports its completion before buffers are reused.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <Protocol/UsbFunctionIo.h>
#include <Protocol/EFIUsbMsd.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>

#define MSD_LUNS 16
#define MSD_BUFFER_SIZE (128 * 1024)
#define CBW_SIGNATURE 0x43425355U
#define CSW_SIGNATURE 0x53425355U
#define BULK_OUT EfiUsbEndpointDirectionHostOut
#define BULK_IN EfiUsbEndpointDirectionHostIn
#define HALT_OUT 1
#define HALT_IN 2

typedef enum {
  BotIdle, BotCbw, BotRead, BotWrite, BotReply, BotVerify,
  BotCsw, BotWaitCsw, BotBlocked
} BOT_STATE;

typedef enum {
  ControlIdle, ControlDataIn, ControlStatusOut, ControlStatusIn
} CONTROL_STATE;

/* MSD_LUN contains one menu-assigned disk and its independent SCSI state. */
typedef struct {
  EFI_BLOCK_IO_PROTOCOL *Io;
  UINT8 Key;
  UINT8 Asc;
  UINT8 Ascq;
  BOOLEAN Stopped;
  BOOLEAN Ejected;
  BOOLEAN Prevent;
} MSD_LUN;

/* MSD_DEVICE retains all DMA allocations until StopController has succeeded. */
typedef struct {
  EFI_USBFN_IO_PROTOCOL *Usb;
  MSD_LUN Lun[MSD_LUNS];
  UINT8 *Control;
  UINT8 *Cbw;
  UINT8 *Csw;
  UINT8 *Data;
  UINTN ChunkLimit;
  UINT16 PacketSize;
  UINT8 MaxLun;
  UINT8 Configuration;
  UINT8 PendingConfiguration;
  BOOLEAN ChangeConfiguration;
  BOOLEAN Running;
  BOOLEAN Fatal;
  BOOLEAN Rearm;
  BOOLEAN NeedReset;
  BOOLEAN ResetSeen;
  UINT8 ResetClears;
  UINT8 HaltMask;
  CONTROL_STATE ControlState;
  UINTN ControlLength;
  BOT_STATE State;
  BOOLEAN Busy;
  EFI_USBFN_ENDPOINT_DIRECTION Direction;
  VOID *TransferBuffer;
  UINTN TransferLength;
  UINT8 Command[16];
  UINT8 CommandLength;
  UINT8 CurrentLun;
  UINT8 Status;
  UINT32 Tag;
  UINT32 HostLength;
  UINT32 Relevant;
  BOOLEAN HostIn;
  UINT64 Remaining;
  UINT64 Lba;
  UINT32 BlockSize;
  UINT32 MediaId;
  BOOLEAN Dirty;
  BOOLEAN StallAfterData;
} MSD_DEVICE;

STATIC MSD_DEVICE m;
STATIC EFI_USB_MSD_PROTOCOL mProtocol;
STATIC EFI_USB_DEVICE_DESCRIPTOR mDevice = {
  18, 1, 0x0200, 0, 0, 0, 64, 0x1209, 0x0002, 0x0100, 1, 2, 3, 1
};
STATIC EFI_USB_CONFIG_DESCRIPTOR mConfig = {9, 2, 32, 1, 1, 0, 0x80, 250};
STATIC EFI_USB_INTERFACE_DESCRIPTOR mInterface = {9, 4, 0, 0, 2, 8, 6, 0x50, 0};
STATIC EFI_USB_ENDPOINT_DESCRIPTOR mOut = {7, 5, 1, 2, 512, 0};
STATIC EFI_USB_ENDPOINT_DESCRIPTOR mIn = {7, 5, 0x81, 2, 512, 0};
STATIC EFI_USB_ENDPOINT_DESCRIPTOR *mEndpoints[] = {&mOut, &mIn};
STATIC EFI_USB_INTERFACE_INFO mInterfaceInfo = {&mInterface, mEndpoints};
STATIC EFI_USB_INTERFACE_INFO *mInterfaces[] = {&mInterfaceInfo};
STATIC EFI_USB_CONFIG_INFO mConfigInfo = {&mConfig, mInterfaces};
STATIC EFI_USB_CONFIG_INFO *mConfigs[] = {&mConfigInfo};
STATIC EFI_USB_DEVICE_INFO mDeviceInfo = {&mDevice, mConfigs};

STATIC UINT32
Le32 (CONST UINT8 *P)
{
  return (UINT32)P[0] | ((UINT32)P[1] << 8) | ((UINT32)P[2] << 16) | ((UINT32)P[3] << 24);
}

STATIC UINT64
Be (CONST UINT8 *P, UINTN Count)
{
  UINT64 Value = 0;
  while (Count-- != 0) {
    Value = (Value << 8) | *P++;
  }
  return Value;
}

STATIC VOID
PutBe (UINT8 *P, UINT64 Value, UINTN Count)
{
  while (Count != 0) {
    P[--Count] = (UINT8)Value;
    Value >>= 8;
  }
}

STATIC VOID
PutLe32 (UINT8 *P, UINT32 Value)
{
  UINTN Index;
  for (Index = 0; Index < 4; ++Index) {
    P[Index] = (UINT8)(Value >> (8 * Index));
  }
}

STATIC EFI_STATUS
SetHalt (EFI_USBFN_ENDPOINT_DIRECTION Direction, BOOLEAN Halt)
{
  EFI_STATUS Status;
  UINT8 Mask = Direction == BULK_IN ? HALT_IN : HALT_OUT;
  Status = m.Usb->SetEndpointStallState (m.Usb, 1, Direction, &Halt);
  if (!EFI_ERROR (Status)) {
    if (Halt) {
      m.HaltMask |= Mask;
    } else {
      m.HaltMask &= (UINT8)~Mask;
    }
  }
  return Status;
}

STATIC VOID
Fault (EFI_STATUS Status)
{
  if (m.Fatal) {
    return;
  }
  DEBUG ((DEBUG_ERROR, "SourceMsd: transport failed %r; reset/restart required\n", Status));
  m.Fatal = TRUE;
  m.NeedReset = TRUE;
  m.Rearm = FALSE;
  m.State = BotBlocked;
  SetHalt (BULK_IN, TRUE);
  SetHalt (BULK_OUT, TRUE);
}

STATIC VOID
Sense (UINT8 Key, UINT8 Asc, UINT8 Ascq)
{
  MSD_LUN *Lun = &m.Lun[m.CurrentLun];
  Lun->Key = Key;
  Lun->Asc = Asc;
  Lun->Ascq = Ascq;
  if (m.Status != 2) {
    m.Status = 1;
  }
}

STATIC VOID
StorageError (EFI_STATUS Status, BOOLEAN Write)
{
  if (Status == EFI_NO_MEDIA) {
    Sense (2, 0x3a, 0);
  } else if (Status == EFI_MEDIA_CHANGED) {
    Sense (6, 0x28, 0);
  } else if (Status == EFI_WRITE_PROTECTED) {
    Sense (7, 0x27, 0);
  } else {
    Sense (3, Write ? 0x0c : 0x11, 0);
  }
  DEBUG ((DEBUG_ERROR, "SourceMsd: LUN%u command %02x LBA%Lu failed %r\n",
          m.CurrentLun, m.Command[0], m.Lba, Status));
}

STATIC EFI_STATUS
Bulk (EFI_USBFN_ENDPOINT_DIRECTION Direction, VOID *Buffer, UINTN Length, BOT_STATE State)
{
  EFI_STATUS Status;
  UINTN Size = Length;
  if (m.Busy || m.Fatal) {
    return EFI_NOT_READY;
  }
  Status = m.Usb->Transfer (m.Usb, 1, Direction, &Size, Buffer);
  if (EFI_ERROR (Status) || Size != Length) {
    Fault (EFI_ERROR (Status) ? Status : EFI_BAD_BUFFER_SIZE);
    return EFI_ERROR (Status) ? Status : EFI_BAD_BUFFER_SIZE;
  }
  m.Busy = TRUE;
  m.Direction = Direction;
  m.TransferBuffer = Buffer;
  m.TransferLength = Length;
  m.State = State;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
CancelBulk (VOID)
{
  EFI_STATUS Status;
  Status = m.Usb->AbortTransfer (m.Usb, 1, BULK_OUT);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = m.Usb->AbortTransfer (m.Usb, 1, BULK_IN);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  m.Busy = FALSE;
  m.State = BotIdle;
  m.Rearm = FALSE;
  if (m.Dirty && m.Lun[m.CurrentLun].Io != NULL) {
    Status = m.Lun[m.CurrentLun].Io->FlushBlocks (m.Lun[m.CurrentLun].Io);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "SourceMsd: cancelled command flush failed %r\n", Status));
      return Status;
    }
    m.Dirty = FALSE;
  }
  return EFI_SUCCESS;
}

STATIC VOID
SendCsw (VOID)
{
  if (m.HaltMask & HALT_IN) {
    m.State = BotWaitCsw;
    return;
  }
  PutLe32 (m.Csw, CSW_SIGNATURE);
  PutLe32 (m.Csw + 4, m.Tag);
  PutLe32 (m.Csw + 8, m.HostLength - m.Relevant);
  m.Csw[12] = m.Status;
  Bulk (BULK_IN, m.Csw, 13, BotCsw);
}

STATIC VOID
Finish (VOID)
{
  EFI_STATUS Status;
  if (m.Dirty && m.Lun[m.CurrentLun].Io != NULL) {
    Status = m.Lun[m.CurrentLun].Io->FlushBlocks (m.Lun[m.CurrentLun].Io);
    if (EFI_ERROR (Status)) {
      StorageError (Status, TRUE);
    }
    m.Dirty = FALSE;
  }
  if (m.StallAfterData) {
    Status = SetHalt (m.HostIn ? BULK_IN : BULK_OUT, TRUE);
    if (EFI_ERROR (Status)) {
      Fault (Status);
      return;
    }
  }
  SendCsw ();
}

STATIC BOOLEAN
Ready (VOID)
{
  MSD_LUN *Lun = &m.Lun[m.CurrentLun];
  if (Lun->Io == NULL || !Lun->Io->Media->MediaPresent || Lun->Ejected) {
    Sense (2, 0x3a, 0);
    return FALSE;
  }
  if (Lun->Stopped) {
    Sense (2, 4, 2);
    return FALSE;
  }
  m.BlockSize = Lun->Io->Media->BlockSize;
  m.MediaId = Lun->Io->Media->MediaId;
  if (m.BlockSize == 0 || m.BlockSize > m.ChunkLimit ||
      (Lun->Io->Media->IoAlign > 1 &&
       ((UINTN)m.Data % Lun->Io->Media->IoAlign) != 0)) {
    Sense (4, 0x44, 0);
    return FALSE;
  }
  return TRUE;
}

STATIC BOOLEAN
Range (UINT64 Lba, UINT64 Blocks)
{
  EFI_LBA Last = m.Lun[m.CurrentLun].Io->Media->LastBlock;
  if (Blocks != 0 && (Lba > Last || Blocks - 1 > Last - Lba)) {
    Sense (5, 0x21, 0);
    return FALSE;
  }
  return TRUE;
}

STATIC VOID
NextData (VOID)
{
  EFI_STATUS Status;
  EFI_BLOCK_IO_PROTOCOL *Io = m.Lun[m.CurrentLun].Io;
  UINTN Chunk;
  UINTN StorageBytes;
  BOT_STATE State = m.State;
  if (m.Remaining == 0) {
    Finish ();
    return;
  }
  Chunk = (UINTN)MIN (m.Remaining, (UINT64)(m.ChunkLimit / m.BlockSize * m.BlockSize));
  if (State == BotWrite) {
    Bulk (BULK_OUT, m.Data, Chunk, BotWrite);
    return;
  }
  StorageBytes = (Chunk + m.BlockSize - 1) / m.BlockSize * m.BlockSize;
  Status = Io->ReadBlocks (Io, m.MediaId, m.Lba, StorageBytes, m.Data);
  if (EFI_ERROR (Status)) {
    StorageError (Status, FALSE);
    m.Remaining = 0;
    m.StallAfterData = m.HostLength > m.Relevant;
    Finish ();
    return;
  }
  if (State == BotVerify) {
    m.Remaining -= Chunk;
    m.Lba += StorageBytes / m.BlockSize;
    if (m.Remaining == 0) {
      Finish ();
    }
  } else {
    Bulk (BULK_IN, m.Data, Chunk, BotRead);
  }
}

/* TransportPlan applies the BOT host/device length and direction cases before I/O. */
STATIC BOOLEAN
TransportPlan (UINT64 DeviceLength, BOOLEAN DeviceIn)
{
  m.Remaining = DeviceLength;
  if (m.HostLength == 0) {
    if (DeviceLength != 0) {
      m.Status = 2;
      m.Remaining = 0;
      Finish ();
      return FALSE;
    }
    return TRUE;
  }
  if (DeviceLength == 0) {
    m.StallAfterData = TRUE;
    return TRUE;
  }
  if (m.HostIn != DeviceIn) {
    m.Status = 2;
    m.StallAfterData = TRUE;
    m.Remaining = 0;
    Finish ();
    return FALSE;
  }
  if (m.HostLength < DeviceLength) {
    m.Status = 2;
    if (!DeviceIn) {
      /* An incomplete WRITE command cannot authorize any disk modification. */
      m.StallAfterData = TRUE;
      m.Remaining = 0;
      Finish ();
      return FALSE;
    }
    m.Remaining = m.HostLength;
  } else if (m.HostLength > DeviceLength) {
    m.StallAfterData = TRUE;
  }
  return TRUE;
}

STATIC VOID
Reply (UINTN Length, UINT32 Allocation)
{
  Length = MIN (Length, Allocation);
  if (!TransportPlan (Length, TRUE)) {
    return;
  }
  Length = (UINTN)m.Remaining;
  if (Length == 0) {
    Finish ();
  } else {
    Bulk (BULK_IN, m.Data, Length, BotReply);
  }
}

STATIC VOID
CommandFailed (VOID)
{
  m.Remaining = 0;
  m.StallAfterData = m.HostLength != 0;
  Finish ();
}

STATIC VOID
Execute (VOID)
{
  UINT8 *C = m.Command;
  MSD_LUN *Lun = &m.Lun[m.CurrentLun];
  EFI_BLOCK_IO_PROTOCOL *Io = Lun->Io;
  EFI_STATUS Status;
  UINT64 Blocks = 0;
  UINT64 Lba = 0;
  UINTN Length;
  UINTN Header;
  UINTN Descriptor;
  UINT32 Allocation;
  BOOLEAN Write = FALSE;
  BOOLEAN Verify = FALSE;
  UINT8 Required;

  Required = ((C[0] >> 5) == 0) ? 6 : (((C[0] >> 5) == 4) ? 16 : (((C[0] >> 5) == 5) ? 12 : 10));
  if (m.CommandLength < Required) {
    Sense (5, 0x24, 0);
    CommandFailed ();
    return;
  }
  ZeroMem (m.Data, 512);
  if (C[0] == 0x03) {
    if (C[1] & 1) {
      Sense (5, 0x24, 0);
      CommandFailed ();
      return;
    }
    m.Data[0] = 0x70;
    m.Data[2] = Lun->Key;
    m.Data[7] = 10;
    m.Data[12] = Lun->Asc;
    m.Data[13] = Lun->Ascq;
    Lun->Key = Lun->Asc = Lun->Ascq = 0;
    Reply (18, C[4]);
    return;
  }
  if (C[0] == 0x12) {
    if (C[1] & ~1U) {
      Sense (5, 0x24, 0);
      CommandFailed ();
      return;
    }
    m.Data[0] = Io == NULL ? 0x7f : 0;
    if (C[1] & 1) {
      m.Data[1] = C[2];
      if (C[2] == 0) {
        m.Data[3] = 2;
        m.Data[4] = 0;
        m.Data[5] = 0x80;
        Length = 6;
      } else if (C[2] == 0x80) {
        m.Data[3] = 12;
        CopyMem (m.Data + 4, "T733MSD00000", 12);
        m.Data[15] = "0123456789ABCDEF"[m.CurrentLun];
        Length = 16;
      } else {
        Sense (5, 0x24, 0);
        CommandFailed ();
        return;
      }
    } else {
      if (C[2] != 0) {
        Sense (5, 0x24, 0);
        CommandFailed ();
        return;
      }
      m.Data[2] = 5;
      m.Data[3] = 2;
      m.Data[4] = 31;
      CopyMem (m.Data + 8, "UniXNU  ", 8);
      CopyMem (m.Data + 16, "T733 UFS Storage", 16);
      CopyMem (m.Data + 32, "0001", 4);
      Length = 36;
    }
    Reply (Length, (UINT32)Be (C + 3, 2));
    return;
  }
  if (C[0] == 0xa0) {
    if (C[2] > 2) {
      Sense (5, 0x24, 0);
      CommandFailed ();
      return;
    }
    PutBe (m.Data, (m.MaxLun + 1) * 8, 4);
    for (Length = 0; Length <= m.MaxLun; ++Length) {
      m.Data[8 + Length * 8 + 1] = (UINT8)Length;
    }
    Reply (8 + (m.MaxLun + 1) * 8, (UINT32)Be (C + 6, 4));
    return;
  }
  if (Io == NULL) {
    Sense (5, 0x25, 0);
    CommandFailed ();
    return;
  }
  if (C[0] == 0x1b) {
    if ((C[1] & ~1U) != 0 || (C[4] & ~3U) != 0) {
      Sense (5, 0x24, 0);
      CommandFailed ();
      return;
    }
    if (!TransportPlan (0, FALSE)) {
      return;
    }
    if (!(C[4] & 1) && (C[4] & 2) && Lun->Prevent) {
      Sense (5, 0x53, 2);
    } else {
      Status = Io->FlushBlocks (Io);
      if (EFI_ERROR (Status)) {
        StorageError (Status, TRUE);
      } else {
        Lun->Stopped = (C[4] & 1) == 0;
        Lun->Ejected = Lun->Stopped && (C[4] & 2);
      }
    }
    Finish ();
    return;
  }
  if (!Ready ()) {
    CommandFailed ();
    return;
  }
  switch (C[0]) {
    case 0x00:
    case 0x1e:
    case 0x35:
    case 0x91:
      if (!TransportPlan (0, FALSE)) {
        return;
      }
      if (C[0] == 0x1e) {
        if (C[4] > 1) {
          Sense (5, 0x24, 0);
        } else {
          Lun->Prevent = C[4] != 0;
        }
      } else if (C[0] != 0x00) {
        Status = Io->FlushBlocks (Io);
        if (EFI_ERROR (Status)) {
          StorageError (Status, TRUE);
        }
      }
      Finish ();
      return;
    case 0x25:
      PutBe (m.Data, MIN (Io->Media->LastBlock, MAX_UINT32), 4);
      PutBe (m.Data + 4, m.BlockSize, 4);
      Reply (8, 8);
      return;
    case 0x9e:
      if ((C[1] & 0x1f) != 0x10) {
        break;
      }
      PutBe (m.Data, Io->Media->LastBlock, 8);
      PutBe (m.Data + 8, m.BlockSize, 4);
      Reply (32, (UINT32)Be (C + 10, 4));
      return;
    case 0x23:
      m.Data[3] = 8;
      PutBe (m.Data + 4, MIN (Io->Media->LastBlock + 1, MAX_UINT32), 4);
      m.Data[8] = 2;
      PutBe (m.Data + 9, m.BlockSize, 3);
      Reply (12, (UINT32)Be (C + 7, 2));
      return;
    case 0x1a:
    case 0x5a:
      if (((C[2] & 0x3f) != 8 && (C[2] & 0x3f) != 0x3f && (C[2] & 0x3f) != 0) || C[3] != 0 ||
          (C[2] >> 6) == 3) {
        break;
      }
      Header = C[0] == 0x1a ? 4 : 8;
      Descriptor = (C[1] & 8) ? 0 : 8;
      m.Data[C[0] == 0x1a ? 2 : 3] = Io->Media->ReadOnly ? 0x80 : 0;
      m.Data[Header - 1] = (UINT8)Descriptor;
      if (Descriptor != 0) {
        PutBe (m.Data + Header + 1, MIN (Io->Media->LastBlock + 1, 0xffffffU), 3);
        PutBe (m.Data + Header + 5, m.BlockSize, 3);
      }
      Length = Header + Descriptor;
      if ((C[2] & 0x3f) != 0) {
        m.Data[Length] = 8;
        m.Data[Length + 1] = 18;
        Length += 20;
      }
      /* WCE remains clear: every successful WRITE CSW follows FlushBlocks. */
      if (Header == 4) {
        m.Data[0] = (UINT8)(Length - 1);
        Allocation = C[4];
      } else {
        PutBe (m.Data, Length - 2, 2);
        Allocation = (UINT32)Be (C + 7, 2);
      }
      Reply (Length, Allocation);
      return;
    case 0x08:
    case 0x0a:
      Write = C[0] == 0x0a;
      Lba = Be (C + 1, 3) & 0x1fffff;
      Blocks = C[4] == 0 ? 256 : C[4];
      goto BlockCommand;
    case 0x28:
    case 0x2a:
    case 0x2f:
      Write = C[0] == 0x2a;
      Verify = C[0] == 0x2f;
      Lba = Be (C + 2, 4);
      Blocks = Be (C + 7, 2);
      goto BlockCommand;
    case 0xa8:
    case 0xaa:
      Write = C[0] == 0xaa;
      Lba = Be (C + 2, 4);
      Blocks = Be (C + 6, 4);
      goto BlockCommand;
    case 0x88:
    case 0x8a:
    case 0x8f:
      Write = C[0] == 0x8a;
      Verify = C[0] == 0x8f;
      Lba = Be (C + 2, 8);
      Blocks = Be (C + 10, 4);
      goto BlockCommand;
    default:
      Sense (5, 0x20, 0);
      CommandFailed ();
      return;
  }
  Sense (5, 0x24, 0);
  CommandFailed ();
  return;

BlockCommand:
  if ((Verify && (C[1] & 6)) || (!Verify && Required != 6 && (C[1] & 0xe0))) {
    Sense (5, 0x24, 0);
    CommandFailed ();
    return;
  }
  if (!Range (Lba, Blocks)) {
    CommandFailed ();
    return;
  }
  if (Write && Io->Media->ReadOnly) {
    Sense (7, 0x27, 0);
    CommandFailed ();
    return;
  }
  m.Lba = Lba;
  if (!TransportPlan (Verify ? 0 : Blocks * m.BlockSize, !Write)) {
    return;
  }
  if (Verify) {
    m.Remaining = Blocks * m.BlockSize;
  }
  m.State = Verify ? BotVerify : (Write ? BotWrite : BotRead);
  NextData ();
}

STATIC VOID
ReceiveCbw (UINTN Length)
{
  UINT8 *C = m.Cbw;
  if (Length != 31 || Le32 (C) != CBW_SIGNATURE || (C[12] & 0x7f) != 0 ||
      (C[13] & 0xf0) != 0 || C[14] == 0 || C[14] > 16) {
    m.NeedReset = TRUE;
    m.ResetSeen = FALSE;
    m.State = BotBlocked;
    if (EFI_ERROR (SetHalt (BULK_IN, TRUE)) || EFI_ERROR (SetHalt (BULK_OUT, TRUE))) {
      Fault (EFI_DEVICE_ERROR);
    }
    return;
  }
  m.Tag = Le32 (C + 4);
  m.HostLength = Le32 (C + 8);
  m.HostIn = (C[12] & 0x80) != 0;
  m.CurrentLun = C[13];
  m.CommandLength = C[14];
  CopyMem (m.Command, C + 15, 16);
  m.Status = 0;
  m.Relevant = 0;
  m.Remaining = 0;
  m.Dirty = FALSE;
  m.StallAfterData = FALSE;
  Execute ();
}

STATIC VOID
BulkComplete (EFI_USBFN_TRANSFER_RESULT *Result)
{
  EFI_STATUS Status;
  EFI_BLOCK_IO_PROTOCOL *Io;
  BOT_STATE State = m.State;
  UINTN Length = Result->BytesTransferred;
  if (!m.Busy) {
    return;
  }
  if (Result->TransferStatus == UsbTransferStatusAborted) {
    Fault (EFI_ABORTED);
    return;
  }
  if (Result->TransferStatus == UsbTransferStatusActive) {
    return;
  }
  if (Result->TransferStatus != UsbTransferStatusComplete || Result->Buffer != m.TransferBuffer ||
      Result->Direction != m.Direction || Length > m.TransferLength) {
    Fault (EFI_PROTOCOL_ERROR);
    return;
  }
  m.Busy = FALSE;
  if (State == BotCbw) {
    ReceiveCbw (Length);
    return;
  }
  if (Length != m.TransferLength) {
    if (State == BotCsw) {
      Fault (EFI_PROTOCOL_ERROR);
      return;
    }
    m.Status = 2;
    m.StallAfterData = TRUE;
    Finish ();
    return;
  }
  if (State == BotCsw) {
    if (m.Status == 2) {
      m.NeedReset = TRUE;
      m.ResetSeen = FALSE;
      m.State = BotBlocked;
      SetHalt (BULK_IN, TRUE);
      SetHalt (BULK_OUT, TRUE);
    } else {
      m.State = BotIdle;
      m.Rearm = TRUE;
    }
    return;
  }
  if (State == BotReply) {
    m.Relevant += (UINT32)Length;
    m.Remaining = 0;
    Finish ();
    return;
  }
  if (State == BotWrite) {
    Io = m.Lun[m.CurrentLun].Io;
    m.Dirty = TRUE;
    Status = Io->WriteBlocks (Io, m.MediaId, m.Lba, Length, m.Data);
    if (EFI_ERROR (Status)) {
      StorageError (Status, TRUE);
      m.Remaining = 0;
      m.StallAfterData = TRUE;
      Finish ();
      return;
    }
  }
  m.Relevant += (UINT32)Length;
  m.Remaining -= Length;
  m.Lba += (Length + m.BlockSize - 1) / m.BlockSize;
  NextData ();
}

STATIC EFI_STATUS
ControlTransfer (EFI_USBFN_ENDPOINT_DIRECTION Direction, UINTN Length, CONTROL_STATE State)
{
  EFI_STATUS Status;
  UINTN Size = Length;
  Status = m.Usb->Transfer (m.Usb, 0, Direction, &Size, m.Control);
  if (!EFI_ERROR (Status) && Size != Length) {
    Status = EFI_BAD_BUFFER_SIZE;
  }
  if (EFI_ERROR (Status)) {
    Fault (Status);
  } else {
    m.ControlState = State;
    m.ControlLength = Length;
  }
  return Status;
}

STATIC VOID
ControlStall (VOID)
{
  BOOLEAN Halt = TRUE;
  m.Usb->SetEndpointStallState (m.Usb, 0, BULK_OUT, &Halt);
  m.ControlState = ControlIdle;
}

STATIC UINTN
StringDescriptor (UINT8 Index)
{
  CONST CHAR8 *String;
  UINTN Length = 2;
  if (Index == 0) {
    m.Control[0] = 4;
    m.Control[1] = 3;
    m.Control[2] = 9;
    m.Control[3] = 4;
    return 4;
  }
  switch (Index) {
    case 1:
      String = "UniXNU";
      break;
    case 2:
      String = "T733 UFS Mass Storage";
      break;
    case 3:
      String = "T733MSD001";
      break;
    default:
      return 0;
  }
  while (*String != 0 && Length < 126) {
    m.Control[Length++] = (UINT8)*String++;
    m.Control[Length++] = 0;
  }
  m.Control[0] = (UINT8)Length;
  m.Control[1] = 3;
  return Length;
}

STATIC VOID
Setup (EFI_USB_DEVICE_REQUEST *Request)
{
  UINTN Length = 0;
  UINT16 Value = Request->Value;
  UINT16 Index = Request->Index;
  UINT8 Type = Request->RequestType;
  EFI_STATUS Status;
  EFI_USBFN_ENDPOINT_DIRECTION Direction;
  BOOLEAN Halt;
  UINT8 Mask;
  m.ControlState = ControlIdle;
  m.ChangeConfiguration = FALSE;
  ZeroMem (m.Control, 512);
  if (Type == 0x80 && Request->Request == 6) {
    switch (Value >> 8) {
      case 1:
        if ((Value & 255) == 0) {
          CopyMem (m.Control, &mDevice, sizeof (mDevice));
          Length = sizeof (mDevice);
        }
        break;
      case 2:
      case 7:
        if ((Value & 255) == 0) {
          CopyMem (m.Control, &mConfig, 9);
          CopyMem (m.Control + 9, &mInterface, 9);
          CopyMem (m.Control + 18, &mOut, 7);
          CopyMem (m.Control + 25, &mIn, 7);
          m.Control[1] = (UINT8)(Value >> 8);
          Value = (Value >> 8) == 7 ? (m.PacketSize == 512 ? 64 : 512) : m.PacketSize;
          m.Control[22] = m.Control[29] = (UINT8)Value;
          m.Control[23] = m.Control[30] = (UINT8)(Value >> 8);
          Length = 32;
        }
        break;
      case 3:
        Length = StringDescriptor ((UINT8)Value);
        break;
      case 6:
        if ((Value & 255) == 0) {
          CONST UINT8 Qualifier[] = {10, 6, 0, 2, 0, 0, 0, 64, 1, 0};
          CopyMem (m.Control, Qualifier, sizeof (Qualifier));
          Length = sizeof (Qualifier);
        }
        break;
    }
    if (Length == 0 || Request->Length == 0) {
      ControlStall ();
      return;
    }
  } else if (Type == 0 && Request->Request == 9 && Value <= 1 && Index == 0 && Request->Length == 0) {
    Status = CancelBulk ();
    if (EFI_ERROR (Status)) {
      Fault (Status);
      return;
    }
    m.Configuration = 0;
    m.PendingConfiguration = (UINT8)Value;
    m.ChangeConfiguration = TRUE;
    m.NeedReset = m.ResetSeen = FALSE;
    m.ResetClears = 0;
    if (EFI_ERROR (SetHalt (BULK_IN, FALSE)) || EFI_ERROR (SetHalt (BULK_OUT, FALSE))) {
      Fault (EFI_DEVICE_ERROR);
      return;
    }
  } else if (Type == 0x80 && Request->Request == 8 && Value == 0 && Index == 0 && Request->Length == 1) {
    m.Control[0] = m.Configuration;
    Length = 1;
  } else if (Type == 0x81 && Request->Request == 10 && Value == 0 && Index == 0 && Request->Length == 1 && m.Configuration) {
    Length = 1;
  } else if (Type == 1 && Request->Request == 11 && Value == 0 && Index == 0 && Request->Length == 0 && m.Configuration) {
    Status = CancelBulk ();
    if (EFI_ERROR (Status)) {
      Fault (Status);
      return;
    }
    m.Rearm = !m.NeedReset;
  } else if (Type == 0xa1 && Request->Request == 0xfe && Value == 0 && Index == 0 && Request->Length == 1 && m.Configuration) {
    m.Control[0] = m.MaxLun;
    Length = 1;
  } else if (Type == 0x21 && Request->Request == 0xff && Value == 0 && Index == 0 && Request->Length == 0 && m.Configuration) {
    Status = CancelBulk ();
    if (EFI_ERROR (Status)) {
      Fault (Status);
      return;
    }
    m.NeedReset = TRUE;
    m.ResetSeen = TRUE;
    m.ResetClears = 0;
    m.State = BotBlocked;
  } else if (Type == 2 && (Request->Request == 1 || Request->Request == 3) &&
             Value == 0 && (Index == 1 || Index == 0x81) && Request->Length == 0 && m.Configuration) {
    Direction = Index == 0x81 ? BULK_IN : BULK_OUT;
    Mask = Direction == BULK_IN ? HALT_IN : HALT_OUT;
    Halt = Request->Request == 3;
    if (Halt && m.Busy && m.Direction == Direction) {
      Status = CancelBulk ();
      if (EFI_ERROR (Status)) {
        Fault (Status);
        return;
      }
      m.NeedReset = TRUE;
      m.ResetSeen = FALSE;
      m.State = BotBlocked;
    }
    if (!Halt && m.NeedReset && !m.ResetSeen) {
      Halt = TRUE;
    }
    Status = SetHalt (Direction, Halt);
    if (EFI_ERROR (Status)) {
      Fault (Status);
      return;
    }
    if (!Halt && m.ResetSeen) {
      m.ResetClears |= Mask;
      if (m.ResetClears == (HALT_IN | HALT_OUT)) {
        m.NeedReset = m.ResetSeen = FALSE;
        m.State = BotIdle;
        m.Rearm = TRUE;
      }
    }
  } else if (Request->Request == 0 && Value == 0 && Request->Length == 2 &&
             ((Type == 0x80 && Index == 0) || (Type == 0x81 && Index == 0 && m.Configuration) ||
              (Type == 0x82 && (Index == 0 || Index == 0x80 ||
               (m.Configuration && (Index == 1 || Index == 0x81)))))) {
    if (Type == 0x82 && (Index & 15) == 1) {
      m.Control[0] = (m.HaltMask & (Index == 0x81 ? HALT_IN : HALT_OUT)) != 0;
    }
    Length = 2;
  } else {
    ControlStall ();
    return;
  }
  if (Request->Length == 0) {
    ControlTransfer (BULK_IN, 0, ControlStatusIn);
  } else {
    ControlTransfer (BULK_IN, MIN (Length, Request->Length), ControlDataIn);
  }
}

STATIC VOID
ControlComplete (EFI_USBFN_TRANSFER_RESULT *Result)
{
  if (Result->TransferStatus == UsbTransferStatusAborted || m.ControlState == ControlIdle) {
    return;
  }
  if (Result->TransferStatus == UsbTransferStatusActive) {
    return;
  }
  if (Result->TransferStatus != UsbTransferStatusComplete || Result->Buffer != m.Control ||
      Result->BytesTransferred != m.ControlLength ||
      Result->Direction != (m.ControlState == ControlStatusOut ? BULK_OUT : BULK_IN)) {
    Fault (EFI_PROTOCOL_ERROR);
    return;
  }
  if (m.ControlState == ControlDataIn) {
    ControlTransfer (BULK_OUT, 0, ControlStatusOut);
  } else {
    m.ControlState = ControlIdle;
    if (m.ChangeConfiguration) {
      m.ChangeConfiguration = FALSE;
      m.Configuration = m.PendingConfiguration;
      m.Rearm = m.Configuration != 0;
    }
  }
}

STATIC EFI_STATUS EFIAPI
MsdEvent (EFI_USB_MSD_PROTOCOL *This)
{
  EFI_USBFN_MESSAGE Message;
  EFI_USBFN_MESSAGE_PAYLOAD Payload;
  EFI_STATUS Status;
  UINTN Size = sizeof (Payload);
  if (This != &mProtocol || !m.Running) {
    return EFI_NOT_READY;
  }
  if (m.Fatal) {
    return EFI_DEVICE_ERROR;
  }
  Status = m.Usb->EventHandler (m.Usb, &Message, &Size, &Payload);
  if (EFI_ERROR (Status)) {
    if (Status != EFI_NOT_READY) {
      Fault (Status);
    }
    return Status;
  }
  switch (Message) {
    case EfiUsbMsgSetupPacket:
      if (!m.Fatal) {
        Setup (&Payload.udr);
      }
      break;
    case EfiUsbMsgEndpointStatusChangedRx:
    case EfiUsbMsgEndpointStatusChangedTx:
      if (m.Fatal) {
        break;
      }
      if (Payload.utr.EndpointIndex == 0) {
        ControlComplete (&Payload.utr);
      } else if (Payload.utr.EndpointIndex == 1) {
        BulkComplete (&Payload.utr);
      }
      break;
    case EfiUsbMsgBusEventReset:
    case EfiUsbMsgBusEventDetach:
      /* USBFN retires old DMA and completion events before reporting this event. */
      m.Busy = FALSE;
      m.Configuration = 0;
      m.ControlState = ControlIdle;
      m.ChangeConfiguration = FALSE;
      m.State = BotIdle;
      m.HaltMask = 0;
      m.NeedReset = m.ResetSeen = m.Rearm = FALSE;
      m.ResetClears = 0;
      if (m.Dirty && m.Lun[m.CurrentLun].Io != NULL) {
        Status = m.Lun[m.CurrentLun].Io->FlushBlocks (m.Lun[m.CurrentLun].Io);
        if (EFI_ERROR (Status)) {
          Fault (Status);
        } else {
          m.Dirty = FALSE;
        }
      }
      break;
    case EfiUsbMsgBusEventSpeed:
      if (Payload.ubs != UsbBusSpeedHigh && Payload.ubs != UsbBusSpeedFull) {
        Fault (EFI_UNSUPPORTED);
      } else {
        m.PacketSize = Payload.ubs == UsbBusSpeedHigh ? 512 : 64;
      }
      break;
    case EfiUsbMsgNone:
      if (m.Fatal || !m.Configuration || m.Busy || m.NeedReset || m.ControlState != ControlIdle) {
        break;
      }
      if (m.State == BotWaitCsw && !(m.HaltMask & HALT_IN)) {
        SendCsw ();
      } else if (m.State == BotVerify) {
        NextData ();
      } else if (m.Rearm && m.HaltMask == 0) {
        m.Rearm = FALSE;
        Bulk (BULK_OUT, m.Cbw, 512, BotCbw);
      }
      break;
    default:
      break;
  }
  return m.Fatal ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI
Assign (EFI_USB_MSD_PROTOCOL *This, EFI_BLOCK_IO_PROTOCOL *Io, UINT32 Lun)
{
  if (This != &mProtocol || Lun >= MSD_LUNS) {
    return EFI_INVALID_PARAMETER;
  }
  if (m.Running) {
    return EFI_ACCESS_DENIED;
  }
  if (Io != NULL && (Io->Media == NULL || Io->ReadBlocks == NULL || Io->WriteBlocks == NULL || Io->FlushBlocks == NULL)) {
    return EFI_INVALID_PARAMETER;
  }
  ZeroMem (&m.Lun[Lun], sizeof (m.Lun[Lun]));
  m.Lun[Lun].Io = Io;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI
Query (EFI_USB_MSD_PROTOCOL *This, UINT8 *Count)
{
  if (This != &mProtocol || Count == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  *Count = MSD_LUNS;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI
Stop (EFI_USB_MSD_PROTOCOL *This)
{
  EFI_STATUS Status;
  EFI_STATUS FlushStatus = EFI_SUCCESS;
  UINTN Index;
  if (This != &mProtocol || !m.Running) {
    return EFI_NOT_READY;
  }
  m.Configuration = 0;
  m.Rearm = FALSE;
  /* StopController must end all DMA, even when AbortTransfer reported failure. */
  CancelBulk ();
  Status = m.Usb->StopController (m.Usb);
  if (EFI_ERROR (Status)) {
    Fault (Status);
    return Status;
  }
  m.Running = FALSE;
  m.Busy = FALSE;
  for (Index = 0; Index < MSD_LUNS; ++Index) {
    if (m.Lun[Index].Io != NULL) {
      Status = m.Lun[Index].Io->FlushBlocks (m.Lun[Index].Io);
      if (EFI_ERROR (Status)) {
        FlushStatus = Status;
      }
    }
  }
  return FlushStatus;
}

STATIC EFI_STATUS EFIAPI
Start (EFI_USB_MSD_PROTOCOL *This)
{
  EFI_STATUS Status;
  UINTN Index;
  BOOLEAN Gap = FALSE;
  BOOLEAN Zlp = FALSE;
  UINTN Maximum;
  if (This != &mProtocol) {
    return EFI_INVALID_PARAMETER;
  }
  if (m.Running) {
    return EFI_ALREADY_STARTED;
  }
  if (m.Lun[0].Io == NULL) {
    return EFI_NO_MEDIA;
  }
  m.MaxLun = 0;
  for (Index = 0; Index < MSD_LUNS; ++Index) {
    if (m.Lun[Index].Io == NULL) {
      Gap = TRUE;
    } else {
      if (Gap) {
        return EFI_INVALID_PARAMETER;
      }
      m.MaxLun = (UINT8)Index;
    }
  }
  Status = m.Usb->GetMaxTransferSize (m.Usb, &Maximum);
  if (EFI_ERROR (Status) || Maximum < 512) {
    return EFI_UNSUPPORTED;
  }
  m.ChunkLimit = MIN (Maximum, MSD_BUFFER_SIZE);
  if (m.Control == NULL) {
    Status = m.Usb->AllocateTransferBuffer (m.Usb, 512, (VOID **)&m.Control);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }
  if (m.Cbw == NULL) {
    Status = m.Usb->AllocateTransferBuffer (m.Usb, 512, (VOID **)&m.Cbw);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }
  if (m.Csw == NULL) {
    Status = m.Usb->AllocateTransferBuffer (m.Usb, 512, (VOID **)&m.Csw);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }
  if (m.Data == NULL) {
    Status = m.Usb->AllocateTransferBuffer (m.Usb, MSD_BUFFER_SIZE, (VOID **)&m.Data);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }
  /* BOT explicitly stalls short data phases; automatic bulk ZLPs are disabled. */
  Status = m.Usb->SetEndpointPolicy (m.Usb, 1, BULK_IN, EfiUsbPolicyZeroLengthTermination, sizeof (Zlp), &Zlp);
  if (EFI_ERROR (Status) && Status != EFI_UNSUPPORTED) {
    return Status;
  }
  Status = m.Usb->StartController (m.Usb);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  m.Running = TRUE;
  m.Fatal = m.Busy = m.NeedReset = m.ResetSeen = m.Rearm = FALSE;
  m.Configuration = m.HaltMask = m.ResetClears = 0;
  m.ControlState = ControlIdle;
  m.State = BotIdle;
  m.PacketSize = 512;
  Status = m.Usb->ConfigureEnableEndpoints (m.Usb, &mDeviceInfo);
  if (EFI_ERROR (Status)) {
    Stop (This);
    return Status;
  }
  DEBUG ((DEBUG_INFO, "SourceMsd: started LUNs=%u, chunk=%Lu bytes\n", m.MaxLun + 1, (UINT64)m.ChunkLimit));
  return EFI_SUCCESS;
}

EFI_STATUS EFIAPI
UsbMsdEntryPoint (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;
  EFI_HANDLE Handle = NULL;
  Status = gBS->LocateProtocol (&gEfiUsbFunctionIoProtocolGuid, NULL, (VOID **)&m.Usb);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  mProtocol.Revision = 0x00010000;
  mProtocol.AssignBlkIoHandle = Assign;
  mProtocol.QueryMaxLun = Query;
  mProtocol.EventHandler = MsdEvent;
  mProtocol.StartDevice = Start;
  mProtocol.StopDevice = Stop;
  Status = gBS->InstallMultipleProtocolInterfaces (&Handle, &gEfiUsbMsdProtocolGuid, &mProtocol, NULL);
  DEBUG ((DEBUG_INFO, "SourceMsd: protocol installation %r\n", Status));
  return Status;
}
