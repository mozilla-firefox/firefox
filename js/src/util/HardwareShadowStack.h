/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef util_HardwareShadowStack_h
#define util_HardwareShadowStack_h

#include "mozilla/Attributes.h"

#include <stddef.h>
#include <stdint.h>

namespace js {

// Return the current thread's shadow stack pointer, or nullptr if the shadow
// stack isn't enabled for this thread.
MOZ_ALWAYS_INLINE uintptr_t* GetShadowStackPointer() {
  uintptr_t ssp = 0;
  // rdsspq is a NOP when shadow stack is disabled.
  asm volatile("rdsspq %0" : "+r"(ssp));
  return reinterpret_cast<uintptr_t*>(ssp);
}

MOZ_ALWAYS_INLINE bool IsShadowStackEnabled() {
  return !!GetShadowStackPointer();
}

// Map a new shadow stack region of |size| bytes.
// Returns nullptr on failure or if the current target can't allocate shadow
// stacks.
void* MapShadowStack(size_t size, uintptr_t** initialShadowStackPointer);

// Unmap a shadow stack region previously returned by MapShadowStack.
void UnmapShadowStack(void* base, size_t size);

// Whether the shadow stack can be switched to |shadowStackPointer|.
bool CanSwitchToShadowStack(const uintptr_t* shadowStackPointer);

}  // namespace js

#endif  // util_HardwareShadowStack_h
