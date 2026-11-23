/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef mozilla_freestanding_SafeThreadLocal_h
#define mozilla_freestanding_SafeThreadLocal_h

#include <type_traits>

#include "mozilla/NativeNt.h"
#include "mozilla/ThreadLocal.h"

namespace mozilla {
namespace freestanding {

/**
 * This class holds data as a thread-local variable, or as a global variable
 * if the thread local storage is not initialized yet.  It should be safe
 * because in that early stage we assume there is no more than a single thread.
 */
template <typename T>
class SafeThreadLocal final {
  static MOZ_THREAD_LOCAL(T) sThreadLocal;
  static T sGlobal;
  static bool sIsTlsUsed;

  // In normal cases, TLS is always available and the class uses sThreadLocal
  // without changing sMainThreadId.  So sMainThreadId is likely to be 0.
  //
  // If TLS is not available, we use sGlobal instead and update sMainThreadId
  // so that that thread keeps using sGlobal even after TLS is initialized
  // later.
  static DWORD sMainThreadId;

  // Need non-inline accessors to prevent the compiler from generating code
  // accessing sThreadLocal before checking a condition.
  MOZ_NEVER_INLINE static void SetGlobalValue(T aValue) { sGlobal = aValue; }
  MOZ_NEVER_INLINE static T GetGlobalValue() { return sGlobal; }

 public:
  static bool set(T aValue) {
    static_assert(std::is_pointer_v<T>,
                  "SafeThreadLocal must be used with a pointer");

    if (sMainThreadId == mozilla::nt::RtlGetCurrentThreadId()) {
      SetGlobalValue(aValue);
    } else if (sIsTlsUsed) {
      if (!mozilla::nt::RtlGetThreadLocalStoragePointer()) {
        // Some threads may be created without a TLS vector (for example by
        // using THREAD_CREATE_FLAGS_SKIP_LOADER_INIT), even when others use
        // one.  Setting a TLS value would fail.
        return false;
      }
      sThreadLocal.set(aValue);
    } else if (mozilla::nt::RtlGetThreadLocalStoragePointer()) {
      sIsTlsUsed = true;
      sThreadLocal.set(aValue);
    } else if (sMainThreadId == 0) {
      sMainThreadId = mozilla::nt::RtlGetCurrentThreadId();
      SetGlobalValue(aValue);
    } else {
      return false;
    }
    return true;
  }

  static T get() {
    if (sMainThreadId == mozilla::nt::RtlGetCurrentThreadId()) {
      return GetGlobalValue();
    }
    if (!sIsTlsUsed || !mozilla::nt::RtlGetThreadLocalStoragePointer()) {
      return T();
    }
    return sThreadLocal.get();
  }
};

template <typename T>
MOZ_GLOBINIT MOZ_THREAD_LOCAL(T) SafeThreadLocal<T>::sThreadLocal;

template <typename T>
T SafeThreadLocal<T>::sGlobal = nullptr;

template <typename T>
bool SafeThreadLocal<T>::sIsTlsUsed = false;

template <typename T>
DWORD SafeThreadLocal<T>::sMainThreadId = 0;

}  // namespace freestanding
}  // namespace mozilla

#endif  // mozilla_freestanding_SafeThreadLocal_h
