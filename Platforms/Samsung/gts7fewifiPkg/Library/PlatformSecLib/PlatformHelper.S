// SPDX-License-Identifier: BSD-2-Clause-Patent

#include <AsmMacroLib.h>

ASM_FUNC (PlatformAssemblyInitialize)
  // SEC calls this function before assigning its physical stack address.
  msr   daifset, #3
  isb

  EL1_OR_EL2 (x1)
1:mrs   x0, sctlr_el1
  b     3f
2:mrs   x0, sctlr_el2
3:bic   x0, x0, #1
  bic   x0, x0, #4
  bic   x0, x0, #0x1000

  EL1_OR_EL2 (x1)
1:msr   sctlr_el1, x0
  b     3f
2:msr   sctlr_el2, x0
3:dsb   sy
  isb

  ic    iallu
  dsb   sy
  isb

  EL1_OR_EL2 (x0)
1:tlbi  vmalle1
  b     3f
2:tlbi  alle2
3:dsb   sy
  isb
  ret
