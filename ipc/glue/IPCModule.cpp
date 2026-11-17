/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "IPCModule.h"

namespace mozilla::ipc {

namespace shared_memory {
// Defined in SharedMemoryHandle.cpp
nsresult RegisterHandleMemoryReporter();
// Defined in SharedMemoryMapping.cpp
nsresult RegisterMappingMemoryReporter();
}  // namespace shared_memory

nsresult InitIPCModule() {
  auto rv = shared_memory::RegisterHandleMemoryReporter();
  if (rv != NS_OK) {
    return rv;
  }
  rv = shared_memory::RegisterMappingMemoryReporter();
  if (rv != NS_OK) {
    return rv;
  }
  return NS_OK;
}

}  // namespace mozilla::ipc
