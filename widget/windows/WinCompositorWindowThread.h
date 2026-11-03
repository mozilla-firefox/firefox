/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef widget_windows_WinCompositorWindowThread_h
#define widget_windows_WinCompositorWindowThread_h

#include "base/message_loop.h"
#include "base/thread.h"
#include "mozilla/StaticMonitor.h"

namespace mozilla {
namespace widget {

struct WinCompositorWnds {
  HWND mCompositorWnd;
  HWND mInitialParentWnd;
  WinCompositorWnds(HWND aCompositorWnd, HWND aInitialParentWnd)
      : mCompositorWnd(aCompositorWnd), mInitialParentWnd(aInitialParentWnd) {}
};

class WinCompositorWindowThread final {
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING_WITH_DELETE_ON_MAIN_THREAD(
      WinCompositorWindowThread)

 public:
  /// Can be called from any thread.
  static WinCompositorWindowThread* Get();

  /// Can only be called from the main thread.
  static void Start();

  /// Can only be called from the main thread.
  static void ShutDown();

  /// Can be called from any thread.
  static MessageLoop* Loop();

  /// Can be called from any thread.
  static bool IsInCompositorWindowThread();

  /// Can be called from any thread.
  static WinCompositorWnds CreateCompositorWindow();

  /// Can be called from any thread.
  static void DestroyCompositorWindow(WinCompositorWnds aWnds);

 private:
  static StaticMonitor sShutdownMonitor;
  static bool sShutdownComplete MOZ_GUARDED_BY(sShutdownMonitor);

  explicit WinCompositorWindowThread(base::Thread* aThread);
  ~WinCompositorWindowThread() = default;

  void ShutDownTask();

  UniquePtr<base::Thread> const mThread;

  // Has ShutDown been called on us? We might have survived if our thread join
  // timed out.
  bool mHasAttemptedShutdown = false;
};

}  // namespace widget
}  // namespace mozilla

#endif  // widget_windows_WinCompositorWindowThread_h
