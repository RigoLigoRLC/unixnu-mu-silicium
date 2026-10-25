#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/PlatformUsbfnLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>

#define USB_BASE       0x0A600000U
#define USB_SCRATCH    0x0A6F8800U
#define USB_GCC        0x0010F000U
#define USB_PHY        0x088E3000U
#define SMMU_BASE      0x15000000U
#define SMMU_SIZE      0x00100000U
#define USB_STREAM     0xE0U
#define UFS_CONTEXT    3U
#define INVALID_INDEX  0xFFFFFFFFU

typedef struct _T733_USB_CONFIG T733_USB_CONFIG;
typedef EFI_STATUS (EFIAPI *T733_USB_GET_BASE)(
  T733_USB_CONFIG *This, UINT32 CoreType, UINTN *Base);
typedef EFI_STATUS (EFIAPI *T733_USB_CONFIGURE)(
  T733_USB_CONFIG *This, UINT32 Mode, UINT32 CoreNumber);

/* T733_USB_CONFIG contains the prefix of the protocol installed by the board's
 * UsbConfigDxe revision 0x10005. Its local image has GetBase at table +8 and
 * ConfigUsb at +16. ConfigUsb(4, 0) calls USB0 InitCommon then InitDevice;
 * StartController belongs to a separate entry and is not needed here.
 */
struct _T733_USB_CONFIG {
  UINT32              Revision;
  T733_USB_GET_BASE    GetBase;
  T733_USB_CONFIGURE   Configure;
};

extern EFI_GUID gT733UsbConfigProtocolGuid;
STATIC BOOLEAN mInitialized;
STATIC UINT32  mUsb2Configuration;

STATIC
UINT32
SmmuRead (UINT32 Offset)
{
  return MmioRead32 (SMMU_BASE + Offset);
}

STATIC
VOID
SmmuWrite (UINT32 Offset, UINT32 Value)
{
  MmioWrite32 (SMMU_BASE + Offset, Value);
  MemoryFence ();
}

STATIC
EFI_STATUS
HaltController (VOID)
{
  UINT32 Attempt;
  UINT32 Pending;

  MmioWrite32 (USB_BASE + 0xC708, 0);
  MmioAnd32 (USB_BASE + 0xC704, ~(BIT31 | BIT19));

  for (Attempt = 0; Attempt < 5000; ++Attempt) {
    Pending = MmioRead32 (USB_BASE + 0xC40C) & 0xFFFF;
    if (Pending != 0) {
      MmioWrite32 (USB_BASE + 0xC40C, Pending);
    }

    if ((MmioRead32 (USB_BASE + 0xC70C) & BIT22) != 0) {
      return EFI_SUCCESS;
    }

    MicroSecondDelay (100);
  }

  DEBUG ((DEBUG_ERROR, "T733 USB: halt timeout DCTL=%08x DSTS=%08x\n",
    MmioRead32 (USB_BASE + 0xC704), MmioRead32 (USB_BASE + 0xC70C)));
  return EFI_TIMEOUT;
}

/* USB DMA uses physical addresses from the generic driver's uncached pages.
 * An exact USB SID entry receives an unused stage-1 context with translation
 * disabled and stage-2 bypass. Existing bypass entries remain intact. All
 * other streams and context banks, including UFS CB3, retain their state.
 */
STATIC
EFI_STATUS
ConfigureUsbStream (VOID)
{
  UINT32  Count;
  UINT32  Id1;
  UINT32  Banks;
  UINT32  Pages;
  UINT32  Slot;
  UINT32  FreeSlot;
  UINT32  Matches;
  UINT32  Index;
  UINT32  Bank;
  UINT32  Smr;
  UINT32  Route;
  UINT32  Mask;
  UINT32  Context;
  BOOLEAN Used[128];

  Count = SmmuRead (0x20) & 255;
  Id1 = SmmuRead (0x24);
  Banks = Id1 & 255;
  Pages = 1U << (((Id1 >> 28) & 7) + 1);
  Slot = INVALID_INDEX;
  FreeSlot = INVALID_INDEX;
  Matches = 0;
  ZeroMem (Used, sizeof (Used));

  if ((Count == 0) || (Count > 128) || (Banks < 2) || (Banks > 128) ||
      ((Id1 & BIT31) != 0) || ((Pages + Banks) * 4096 > SMMU_SIZE) ||
      ((SmmuRead (0x28) & BIT12) == 0)) {
    DEBUG ((DEBUG_ERROR, "T733 USB: unsupported SMMU ID0=%08x ID1=%08x ID2=%08x\n",
      SmmuRead (0x20), Id1, SmmuRead (0x28)));
    return EFI_UNSUPPORTED;
  }

  for (Index = 0; Index < Count; ++Index) {
    Smr = SmmuRead (0x800 + 4 * Index);
    Route = SmmuRead (0xC00 + 4 * Index);
    if ((Smr & BIT31) == 0) {
      FreeSlot = Index;
      continue;
    }

    Mask = (Smr >> 16) & 0x7FFF;
    if (((Smr ^ USB_STREAM) & ~Mask & 0xFFFF) == 0) {
      DEBUG ((DEBUG_INFO, "T733 USB: SID=e0 slot=%u SMR=%08x S2CR=%08x\n",
        Index, Smr, Route));
      ++Matches;
      if ((Mask != 0) || (Matches > 1)) {
        return EFI_ACCESS_DENIED;
      }

      Slot = Index;
    }

    if ((((Route >> 16) & 3) == 0) && ((Route & 255) < Banks)) {
      Used[Route & 255] = TRUE;
    }
  }

  if (Slot == INVALID_INDEX) {
    Slot = FreeSlot;
  }

  if (Slot == INVALID_INDEX) {
    return EFI_OUT_OF_RESOURCES;
  }

  if ((Matches != 0) && (((SmmuRead (0xC00 + 4 * Slot) >> 16) & 3) == 1)) {
    return EFI_SUCCESS;
  }

  for (Bank = 1; Bank < Banks; ++Bank) {
    if ((Bank != UFS_CONTEXT) && !Used[Bank] &&
        ((SmmuRead ((Pages + Bank) * 4096) & BIT0) == 0)) {
      break;
    }
  }

  if (Bank == Banks) {
    return EFI_OUT_OF_RESOURCES;
  }

  Context = (Pages + Bank) * 4096;
  SmmuWrite (Context, 0);
  SmmuWrite (0x1800 + 4 * Bank, 1);
  SmmuWrite (0x1000 + 4 * Bank, 0x10000);
  if (((SmmuRead (Context) & BIT0) != 0) ||
      ((SmmuRead (0x1800 + 4 * Bank) & BIT0) == 0)) {
    return EFI_DEVICE_ERROR;
  }

  SmmuWrite (0xC00 + 4 * Slot, Bank);
  SmmuWrite (0x800 + 4 * Slot, BIT31 | USB_STREAM);
  DEBUG ((DEBUG_INFO, "T733 USB: SID=e0 identity slot=%u CB=%u S2CR=%08x\n",
    Slot, Bank, SmmuRead (0xC00 + 4 * Slot)));
  if ((SmmuRead (0xC00 + 4 * Slot) != Bank) ||
      (SmmuRead (0x800 + 4 * Slot) != (BIT31 | USB_STREAM))) {
    return EFI_DEVICE_ERROR;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
InitializeHardware (VOID)
{
  T733_USB_CONFIG *Config;
  EFI_STATUS      Status;
  UINTN           Base;
  UINT32          Id;
  UINT32          Index;
  UINT32          Gate;
  STATIC CONST UINT32 Gates[] = { 0x10, 0x1C, 0x18, 0x7C, 0x80 };

  if (mInitialized) {
    return EFI_SUCCESS;
  }

  Status = gBS->LocateProtocol (&gT733UsbConfigProtocolGuid, NULL, (VOID **)&Config);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if ((Config->Revision != 0x10005) || (Config->GetBase == NULL) ||
      (Config->Configure == NULL)) {
    DEBUG ((DEBUG_ERROR, "T733 USB: unsupported UsbConfig revision=%x\n", Config->Revision));
    return EFI_UNSUPPORTED;
  }

  Base = 0;
  Status = Config->GetBase (Config, 5, &Base);
  if (EFI_ERROR (Status) || (Base != USB_BASE)) {
    DEBUG ((DEBUG_ERROR, "T733 USB: GetBase status=%r base=%lx\n", Status, Base));
    return EFI_DEVICE_ERROR;
  }

  Status = Config->Configure (Config, 4, 0);
  DEBUG ((DEBUG_INFO, "T733 USB: ConfigUsb device/core0 status=%r\n", Status));
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if ((MmioRead32 (USB_GCC + 4) & BIT31) == 0) {
    return EFI_NOT_READY;
  }

  for (Index = 0; Index < ARRAY_SIZE (Gates); ++Index) {
    Gate = MmioRead32 (USB_GCC + Gates[Index]);
    DEBUG ((DEBUG_INFO, "T733 USB: GCC +%x=%08x\n", Gates[Index], Gate));
    if ((Gate & BIT31) != 0) {
      return EFI_NOT_READY;
    }
  }

  Id = MmioRead32 (USB_BASE + 0xC120);
  if (((Id >> 16) != 0x5533) && ((Id >> 16) != 0x3331)) {
    return EFI_UNSUPPORTED;
  }

  Status = HaltController ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // NativeUsbDebug takes over the halted controller in device mode.
  MmioAndThenOr32 (USB_BASE + 0xC110, ~(BIT12 | BIT13), BIT13);
  MmioAnd32 (USB_BASE + 0xC200, ~(BIT6 | BIT8));

  Status = ConfigureUsbStream ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  MmioOr32 (USB_SCRATCH + 0x10, BIT20 | BIT28);
  MmioOr32 (USB_SCRATCH + 0x30, BIT24);
  mUsb2Configuration = MmioRead32 (USB_BASE + 0xC200);
  DEBUG ((DEBUG_INFO, "T733 USB: ready id=%08x PHY=%08x USB2CFG=%08x DMA identity/UC\n",
    Id, MmioRead32 (USB_PHY + 0x54), mUsb2Configuration));
  mInitialized = TRUE;
  return EFI_SUCCESS;
}

EFI_STATUS
UsbfnPlatformGetData (OUT USBFN_PLATFORM_DATA *PlatformData)
{
  if (PlatformData == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  /* Driver registration only needs the board configuration. StartController
   * refreshes the PHY fields after UsbfnPlatformPhyInit has powered USB0.
   */
  ZeroMem (PlatformData, sizeof (*PlatformData));
  PlatformData->ControllerBase = USB_BASE;
  PlatformData->MaximumSpeed = UsbBusSpeedHigh;
  PlatformData->UtmiWidth = (mUsb2Configuration & BIT3) != 0 ?
    UsbfnUtmiWidth16Bit : UsbfnUtmiWidth8Bit;
  PlatformData->Usb2FreeClockExists = (mUsb2Configuration & BIT30) != 0;
  PlatformData->SuperSpeedPhyDisabled = TRUE;
  PlatformData->VendorId = 0x18D1;
  PlatformData->ProductId = 0xD00D;
  return EFI_SUCCESS;
}

EFI_STATUS
UsbfnPlatformPhyInit (VOID)
{
  return InitializeHardware ();
}

EFI_STATUS
UsbfnPlatformPhyExit (VOID)
{
  /* The controller driver retires DMA before this call. USB0 clocks, PHY and
   * the stream remain configured for NativeUsbDebug after ExitBootServices.
   */
  if (mInitialized) {
    MmioAnd32 (USB_BASE + 0xC200, ~(BIT6 | BIT8));
  }

  return EFI_SUCCESS;
}

BOOLEAN
UsbfnPlatformIsVbusPresent (VOID)
{
  /* DWC3 disconnect/link events provide cable state. A PMIC protocol call
   * here would perform serial bus I/O from the controller's polling path.
   */
  return TRUE;
}
