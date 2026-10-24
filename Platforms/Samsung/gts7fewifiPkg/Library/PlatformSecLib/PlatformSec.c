#include <Library/ConfigurationMapHelperLib.h>
#include <Library/IoLib.h>
#include <Library/PcdLib.h>
#include <Library/PlatformSecLib.h>

VOID
PlatformInitialize (VOID)
{
  UINT32  EarlyInitCoreCount;
  UINTN   Redistributor;

  EarlyInitCoreCount = 2;
  LocateConfigurationEntry32 ("EarlyInitCoreCnt", &EarlyInitCoreCount);

  // Gunyah requires the redistributors of the initially active cores awake.
  for (UINT32 Core = 0; Core < EarlyInitCoreCount; Core++) {
    Redistributor = FixedPcdGet64 (PcdGicRedistributorsBase) + Core * 0x20000;
    MmioAnd32 (Redistributor + 0x14, ~BIT1);
  }
}
