#ifndef T733_USERDATA_PAYLOAD_H
#define T733_USERDATA_PAYLOAD_H

#include <Uefi.h>

#ifndef T733_USERDATA_PAYLOAD
#define T733_USERDATA_PAYLOAD 0
#endif

EFI_STATUS
LoadUserdataPayload (
  IN EFI_HANDLE PartitionHandle
  );

#endif
