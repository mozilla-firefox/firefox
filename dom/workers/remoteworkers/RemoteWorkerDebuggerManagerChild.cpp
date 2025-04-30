/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RemoteWorkerDebuggerManagerChild.h"
#include "RemoteWorkerService.h"

namespace mozilla::dom {

RemoteWorkerDebuggerManagerChild::RemoteWorkerDebuggerManagerChild() {
  MOZ_ASSERT_DEBUG_OR_FUZZING(
      RemoteWorkerService::Thread() &&
      RemoteWorkerService::Thread()->IsOnCurrentThread());
}

RemoteWorkerDebuggerManagerChild::~RemoteWorkerDebuggerManagerChild() {}

}  // end of namespace mozilla::dom
