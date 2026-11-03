/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_VR_MUTEX_H
#define GFX_VR_MUTEX_H

#if defined(XP_WIN)
#  include "nsTString.h"
#endif

namespace mozilla {
namespace gfx {

#if defined(XP_WIN)
class WaitForMutex {
 public:
  explicit WaitForMutex(HANDLE handle) : mHandle(handle), mStatus(false) {
    MOZ_RELEASE_ASSERT(mHandle);

    DWORD dwWaitResult;
    dwWaitResult = WaitForSingleObject(mHandle,    // handle to mutex
                                       INFINITE);  // no time-out interval

    switch (dwWaitResult) {
      // The thread got ownership of the mutex
      case WAIT_OBJECT_0:
        mStatus = true;
        break;

      // The thread got ownership of an abandoned mutex
      // The shmem is in an indeterminate state
      case WAIT_ABANDONED:
        mStatus = false;
        break;
      default:
        mStatus = false;
        break;
    }
  }

  ~WaitForMutex() {
    if (mHandle && !ReleaseMutex(mHandle)) {
#  ifdef MOZILLA_INTERNAL_API
      nsAutoCString msg;
      msg.AppendPrintf("WaitForMutex %p ReleaseMutex error \"%lu\".", mHandle,
                       GetLastError());
      NS_WARNING(msg.get());
#  endif
      MOZ_ASSERT(false, "Failed to release mutex.");
    }
  }

  bool GetStatus() { return mStatus; }

 private:
  HANDLE mHandle;
  bool mStatus;
};
#endif

}  // namespace gfx
}  // namespace mozilla

#endif /* GFX_VR_MUTEX_H */
