/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_RemoteWorkerDebuggerManagerChild_h
#define mozilla_dom_RemoteWorkerDebuggerManagerChild_h

#include "mozilla/dom/PRemoteWorkerDebuggerManagerChild.h"

namespace mozilla::dom {

class RemoteWorkerDebuggerManagerChild final
    : public PRemoteWorkerDebuggerManagerChild {
  friend class PRemoteWorkerDebuggerManagerChild;

 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(RemoteWorkerDebuggerManagerChild, final)

  RemoteWorkerDebuggerManagerChild();

 private:
  ~RemoteWorkerDebuggerManagerChild();
};

}  // end of namespace mozilla::dom

#endif
