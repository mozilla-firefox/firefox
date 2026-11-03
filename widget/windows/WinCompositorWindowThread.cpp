/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WinCompositorWindowThread.h"

#include "base/platform_thread.h"
#include "mozilla/StaticMonitor.h"
#include "mozilla/StaticPrefs_apz.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/WindowsUserHandleValidation.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/layers/SynchronousTask.h"
#include "transport/runnable_utils.h"

namespace mozilla {
namespace widget {

static StaticRefPtr<WinCompositorWindowThread> sWinCompositorWindowThread;

StaticMonitor WinCompositorWindowThread::sShutdownMonitor;
bool WinCompositorWindowThread::sShutdownComplete MOZ_GUARDED_BY(
    WinCompositorWindowThread::sShutdownMonitor) = false;

// Ensures the flag that forces user32 handle validation calls onto the slow
// kernel path is cleared for this thread, since it creates, destroys and
// dispatches messages to its own HWNDs. Overriding Init() (rather than
// posting a task after Start()) guarantees this runs on every OS thread this
// object drives, including after a Stop()/StartWithOptions() restart.
class CompositorWindowThread final : public base::Thread {
 public:
  CompositorWindowThread() : base::Thread("WinCompositor") {}

 protected:
  void Init() override { ForceToGuiThreadAndFixTebValidateHandlesFlag(); }
};

/// A window procedure that logs when an input event is received to the gfx
/// error log
///
/// This is done because this window is supposed to be WM_DISABLED, but
/// malfunctioning software may still end up targetting this window. If that
/// happens, it's almost-certainly a bug and should be brought to the attention
/// of the developers that are debugging the issue.
static LRESULT CALLBACK InputEventRejectingWindowProc(HWND window, UINT msg,
                                                      WPARAM wparam,
                                                      LPARAM lparam) {
  switch (msg) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
    case WM_MOUSEMOVE:
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
      gfxCriticalNoteOnce
          << "The compositor window received an input event even though it's "
             "disabled. There is likely malfunctioning "
             "software on the user's machine.";

      break;
    default:
      break;
  }
  return ::DefWindowProcW(window, msg, wparam, lparam);
}

WinCompositorWindowThread::WinCompositorWindowThread(base::Thread* aThread)
    : mThread(aThread) {}

/* static */
WinCompositorWindowThread* WinCompositorWindowThread::Get() {
  if (!sWinCompositorWindowThread ||
      sWinCompositorWindowThread->mHasAttemptedShutdown) {
    return nullptr;
  }
  return sWinCompositorWindowThread;
}

/* static */
void WinCompositorWindowThread::Start() {
  MOZ_ASSERT(NS_IsMainThread());

  base::Thread::Options options;
  // HWND requests ui thread.
  options.message_loop_type = MessageLoop::TYPE_UI;

  if (sWinCompositorWindowThread) {
    // Try to reuse the thread, which involves stopping and restarting it.
    sWinCompositorWindowThread->mThread->Stop();
    if (sWinCompositorWindowThread->mThread->StartWithOptions(options)) {
      // Success!
      sWinCompositorWindowThread->mHasAttemptedShutdown = false;
      return;
    }
    // Restart failed, so null out our sWinCompositorWindowThread and
    // try again with a new thread. This will cause the old singleton
    // instance to be deallocated, which will destroy its mThread as well.
    sWinCompositorWindowThread = nullptr;
  }

  base::Thread* thread = new CompositorWindowThread();
  if (!thread->StartWithOptions(options)) {
    delete thread;
    return;
  }

  sWinCompositorWindowThread = new WinCompositorWindowThread(thread);
}

/* static */
void WinCompositorWindowThread::ShutDown() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(sWinCompositorWindowThread);

  sWinCompositorWindowThread->mHasAttemptedShutdown = true;

  // Our thread could hang while we're waiting for it to stop.
  // Since we're shutting down, that's not a critical problem.
  // We set a reasonable amount of time to wait for shutdown,
  // and if it succeeds within that time, we correctly stop
  // our thread by nulling out the refptr, which will cause it
  // to be deallocated and join the thread. If it times out,
  // we do nothing, which means that the thread will not be
  // joined and sWinCompositorWindowThread memory will leak.
  bool shutdownComplete = false;
  {
    // It's important to hold the lock before posting the
    // runnable. This ensures that the runnable can't begin
    // until we've started our Wait, which prevents us from
    // Waiting on a monitor that has already been notified.
    StaticMonitorAutoLock lock(sShutdownMonitor);

    static const TimeDuration TIMEOUT = TimeDuration::FromSeconds(2.0);
    RefPtr<Runnable> runnable =
        NewRunnableMethod("WinCompositorWindowThread::ShutDownTask",
                          sWinCompositorWindowThread.get(),
                          &WinCompositorWindowThread::ShutDownTask);
    Loop()->PostTask(runnable.forget());

    // sShutdownMonitor can wake up spuriously, which can have
    // spurious wakeups which are reported as timeouts, so we
    // check timestamps to ensure that we've waited as long we
    // intended to. If we wake early, we don't bother calculating
    // a precise amount for the next wait; we just wait the same
    // amount of time. This means timeout might happen after as
    // much as 2x the TIMEOUT time.
    TimeStamp timeStart = TimeStamp::NowLoRes();
    do {
      sShutdownMonitor.Wait(TIMEOUT);
    } while (!sShutdownComplete &&
             ((TimeStamp::NowLoRes() - timeStart) < TIMEOUT));

    // Make a local copy of sShutdownComplete, for use outside of
    // the lock.
    shutdownComplete = sShutdownComplete;
  }

  if (shutdownComplete) {
    sWinCompositorWindowThread = nullptr;
  }
}

void WinCompositorWindowThread::ShutDownTask() {
  StaticMonitorAutoLock lock(sShutdownMonitor);

  MOZ_ASSERT(IsInCompositorWindowThread());

  sShutdownComplete = true;
  sShutdownMonitor.NotifyAll();
}

/* static */
MessageLoop* WinCompositorWindowThread::Loop() {
  return sWinCompositorWindowThread
             ? sWinCompositorWindowThread->mThread->message_loop()
             : nullptr;
}

/* static */
bool WinCompositorWindowThread::IsInCompositorWindowThread() {
  return sWinCompositorWindowThread &&
         sWinCompositorWindowThread->mThread->thread_id() ==
             PlatformThread::CurrentId();
}

const wchar_t kClassNameCompositorInitalParent[] =
    L"MozillaCompositorInitialParentClass";
const wchar_t kClassNameCompositor[] = L"MozillaCompositorWindowClass";

ATOM g_compositor_inital_parent_window_class;
ATOM g_compositor_window_class;

// This runs on the window owner thread.
void InitializeInitialParentWindowClass() {
  if (g_compositor_inital_parent_window_class) {
    return;
  }

  WNDCLASSW wc;
  wc.style = 0;
  wc.lpfnWndProc = ::DefWindowProcW;
  wc.cbClsExtra = 0;
  wc.cbWndExtra = 0;
  wc.hInstance = GetModuleHandle(nullptr);
  wc.hIcon = nullptr;
  wc.hCursor = nullptr;
  wc.hbrBackground = nullptr;
  wc.lpszMenuName = nullptr;
  wc.lpszClassName = kClassNameCompositorInitalParent;
  g_compositor_inital_parent_window_class = ::RegisterClassW(&wc);
}

// This runs on the window owner thread.
void InitializeWindowClass() {
  if (g_compositor_window_class) {
    return;
  }

  WNDCLASSW wc;
  wc.style = CS_OWNDC;
  wc.lpfnWndProc = InputEventRejectingWindowProc;
  wc.cbClsExtra = 0;
  wc.cbWndExtra = 0;
  wc.hInstance = GetModuleHandle(nullptr);
  wc.hIcon = nullptr;
  wc.hCursor = nullptr;
  wc.hbrBackground = nullptr;
  wc.lpszMenuName = nullptr;
  wc.lpszClassName = kClassNameCompositor;
  g_compositor_window_class = ::RegisterClassW(&wc);
}

/* static */
WinCompositorWnds WinCompositorWindowThread::CreateCompositorWindow() {
  MOZ_ASSERT(Loop());

  if (!Loop()) {
    return WinCompositorWnds(nullptr, nullptr);
  }

  layers::SynchronousTask task("Create compositor window");

  HWND initialParentWnd = nullptr;
  HWND compositorWnd = nullptr;

  RefPtr<Runnable> runnable = NS_NewRunnableFunction(
      "WinCompositorWindowThread::CreateCompositorWindow::Runnable", [&]() {
        layers::AutoCompleteTask complete(&task);

        InitializeInitialParentWindowClass();
        InitializeWindowClass();

        // Create initial parent window.
        // We could not directly create a compositor window with a main window
        // as parent window, so instead create it with a temporary placeholder
        // parent. Its parent is set as main window in UI process.
        initialParentWnd =
            ::CreateWindowEx(WS_EX_TOOLWINDOW, kClassNameCompositorInitalParent,
                             nullptr, WS_POPUP | WS_DISABLED, 0, 0, 1, 1,
                             nullptr, 0, GetModuleHandle(nullptr), 0);
        if (!initialParentWnd) {
          gfxCriticalNoteOnce << "Inital parent window failed "
                              << ::GetLastError();
          return;
        }

        DWORD extendedStyle = WS_EX_NOPARENTNOTIFY | WS_EX_NOREDIRECTIONBITMAP;

        if (!StaticPrefs::apz_windows_force_disable_direct_manipulation()) {
          extendedStyle |= WS_EX_LAYERED | WS_EX_TRANSPARENT;
        }

        compositorWnd = ::CreateWindowEx(
            extendedStyle, kClassNameCompositor, nullptr,
            WS_CHILDWINDOW | WS_DISABLED | WS_VISIBLE, 0, 0, 1, 1,
            initialParentWnd, 0, GetModuleHandle(nullptr), 0);
        if (!compositorWnd) {
          gfxCriticalNoteOnce << "Compositor window failed "
                              << ::GetLastError();
        }
      });

  Loop()->PostTask(runnable.forget());

  task.Wait();

  return WinCompositorWnds(compositorWnd, initialParentWnd);
}

/* static */
void WinCompositorWindowThread::DestroyCompositorWindow(
    WinCompositorWnds aWnds) {
  MOZ_ASSERT(aWnds.mCompositorWnd);
  MOZ_ASSERT(aWnds.mInitialParentWnd);
  MOZ_ASSERT(Loop());

  if (!Loop()) {
    return;
  }

  RefPtr<Runnable> runnable = NS_NewRunnableFunction(
      "WinCompositorWidget::CreateNativeWindow::Runnable", [aWnds]() {
        ::DestroyWindow(aWnds.mCompositorWnd);
        ::DestroyWindow(aWnds.mInitialParentWnd);
      });

  Loop()->PostTask(runnable.forget());
}

}  // namespace widget
}  // namespace mozilla
