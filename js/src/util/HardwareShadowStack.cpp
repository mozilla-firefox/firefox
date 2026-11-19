/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "util/HardwareShadowStack.h"

#include "mozilla/Assertions.h"

#ifdef XP_LINUX
#  include <sys/mman.h>
#  include <sys/syscall.h>
#  include <unistd.h>

// The sysroot headers may predate the map_shadow_stack syscall.
// JS_HW_SHADOW_STACK is currently restricted to x86_64 only.
#  ifndef __NR_map_shadow_stack
#    define __NR_map_shadow_stack 453
#  endif
#  ifndef SHADOW_STACK_SET_TOKEN
#    define SHADOW_STACK_SET_TOKEN 0x1
#  endif
#endif

void* js::MapShadowStack(size_t size, uintptr_t** initialShadowStackPointer) {
#ifdef XP_LINUX
  void* base = reinterpret_cast<void*>(
      syscall(__NR_map_shadow_stack, 0, size, SHADOW_STACK_SET_TOKEN));
  if (base == MAP_FAILED) {
    return nullptr;
  }
  // The kernel places the restore token in the slot just below the top of the
  // stack, so switching to it leaves the shadow stack pointer at the top.
  *initialShadowStackPointer =
      reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(base) + size);
  return base;
#else
  return nullptr;
#endif
}

void js::UnmapShadowStack(void* base, size_t size) {
#ifdef XP_LINUX
  uintptr_t ssp = reinterpret_cast<uintptr_t>(GetShadowStackPointer());
  uintptr_t mappingBase = reinterpret_cast<uintptr_t>(base);
  // Ensure that we aren't unmapping the shadow stack being used by the current
  // thread.
  MOZ_ASSERT(ssp < mappingBase || ssp > mappingBase + size);
  (void)munmap(base, size);
#else
  MOZ_CRASH("MapShadowStack can't have succeeded on this platform");
#endif
}

bool js::CanSwitchToShadowStack(const uintptr_t* shadowStackPointer) {
  // A shadow stack is switched to through the restore token in the slot just
  // below the shadow stack pointer it resumes with. The token holds that
  // shadow stack pointer, with bit 0 set.
  return shadowStackPointer[-1] ==
         (reinterpret_cast<uintptr_t>(shadowStackPointer) | 1);
}
