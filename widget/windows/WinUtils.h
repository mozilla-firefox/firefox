/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_widget_WinUtils_h__
#define mozilla_widget_WinUtils_h__

#include "nscore.h"
#include <windows.h>
#include <shobjidl.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <unordered_map>
#include <utility>

// Undo the windows.h damage
#undef GetMessage
#undef CreateEvent
#undef GetClassName
#undef GetBinaryType
#undef RemoveDirectory

#include "nsString.h"
#include "nsRegion.h"
#include "nsRect.h"

#include "nsIRunnable.h"
#include "nsICryptoHash.h"
#ifdef MOZ_PLACES
#  include "nsIFaviconService.h"
#endif
#include "nsIDownloader.h"
#include "nsIURI.h"
#include "nsIWidget.h"
#include "nsWindowsHelpers.h"

#include "mozilla/Attributes.h"
#include "mozilla/EventForwards.h"
#include "mozilla/LazyIdleThread.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/Vector.h"
#include "mozilla/WindowsDpiAwareness.h"
#include "mozilla/WindowsProcessMitigations.h"
#include "mozilla/gfx/2D.h"

/**
 * NS_INLINE_DECL_IUNKNOWN_REFCOUNTING should be used for defining and
 * implementing AddRef() and Release() of IUnknown interface and mRefCnt.
 * NS_INLINE_DECL_IUNKNOWN_ADDREF_RELEASE should be used for overriding
 * AddRef() and Release() of IUnknown interface.
 * These depend on xpcom/base/nsISupportsImpl.h.
 */

#define NS_INLINE_DECL_IUNKNOWN_ADDREF_RELEASE(_class)                      \
 public:                                                                    \
  STDMETHODIMP_(ULONG) AddRef() {                                           \
    MOZ_ASSERT_TYPE_OK_FOR_REFCOUNTING(_class)                              \
    MOZ_ASSERT(int32_t(mRefCnt) >= 0, "illegal refcnt");                    \
    NS_ASSERT_OWNINGTHREAD(_class);                                         \
    ++mRefCnt;                                                              \
    NS_LOG_ADDREF(this, mRefCnt, #_class, sizeof(*this));                   \
    return static_cast<ULONG>(mRefCnt.get());                               \
  }                                                                         \
  STDMETHODIMP_(ULONG) Release() {                                          \
    MOZ_ASSERT(int32_t(mRefCnt) > 0,                                        \
               "Release called on object that has already been released!"); \
    NS_ASSERT_OWNINGTHREAD(_class);                                         \
    --mRefCnt;                                                              \
    NS_LOG_RELEASE(this, mRefCnt, #_class);                                 \
    if (mRefCnt == 0) {                                                     \
      NS_ASSERT_OWNINGTHREAD(_class);                                       \
      mRefCnt = 1; /* stabilize */                                          \
      delete this;                                                          \
      return 0;                                                             \
    }                                                                       \
    return static_cast<ULONG>(mRefCnt.get());                               \
  }

#define NS_INLINE_DECL_IUNKNOWN_REFCOUNTING(_class) \
 public:                                            \
  NS_INLINE_DECL_IUNKNOWN_ADDREF_RELEASE(_class)    \
                                                    \
 protected:                                         \
  nsAutoRefCnt mRefCnt;                             \
  NS_DECL_OWNINGTHREAD                              \
 public:

class nsWindow;
struct KeyPair;

namespace mozilla {
enum class PointerCapabilities : uint8_t;
#if defined(ACCESSIBILITY)
namespace a11y {
class LocalAccessible;
}  // namespace a11y
#endif  // defined(ACCESSIBILITY)

// Helper function: enumerate all the toplevel HWNDs attached to the current
// thread via ::EnumThreadWindows().
//
// Note that this use of ::EnumThreadWindows() is, unfortunately, not an
// abstract implementation detail.
template <typename F>
void EnumerateThreadWindows(F&& f)
// requires requires(F f, HWND h) { f(h); }
{
  class Impl {
   public:
    F f;
    explicit Impl(F&& f) : f(std::forward<F>(f)) {}

    void invoke() {
      WNDENUMPROC proc = &Impl::Callback;
      ::EnumThreadWindows(::GetCurrentThreadId(), proc,
                          reinterpret_cast<LPARAM>(&f));
    }

   private:
    static BOOL CALLBACK Callback(HWND hwnd, LPARAM lp) {
      (*reinterpret_cast<F*>(lp))(hwnd);
      return TRUE;
    }
  };

  Impl(std::forward<F>(f)).invoke();
}

namespace widget {

// More complete QS definitions for MsgWaitForMultipleObjects() and
// GetQueueStatus() that include newer win8 specific defines.

#ifndef QS_RAWINPUT
#  define QS_RAWINPUT 0x0400
#endif

#ifndef QS_TOUCH
#  define QS_TOUCH 0x0800
#  define QS_POINTER 0x1000
#endif

#define MOZ_QS_ALLEVENT                                                      \
  (QS_KEY | QS_MOUSEMOVE | QS_MOUSEBUTTON | QS_POSTMESSAGE | QS_TIMER |      \
   QS_PAINT | QS_SENDMESSAGE | QS_HOTKEY | QS_ALLPOSTMESSAGE | QS_RAWINPUT | \
   QS_TOUCH | QS_POINTER)

// Logging macros
#define LogFunction() mozilla::widget::WinUtils::Log(__FUNCTION__)
#define LogThread()                                                 \
  mozilla::widget::WinUtils::Log("%s: IsMainThread:%d ThreadId:%X", \
                                 __FUNCTION__, NS_IsMainThread(),   \
                                 GetCurrentThreadId())
#define LogThis() mozilla::widget::WinUtils::Log("[%X] %s", this, __FUNCTION__)
#define LogException(e)                                           \
  mozilla::widget::WinUtils::Log("%s Exception:%s", __FUNCTION__, \
                                 e->ToString()->Data())
#define LogHRESULT(hr) \
  mozilla::widget::WinUtils::Log("%s hr=%X", __FUNCTION__, hr)

#ifdef MOZ_PLACES
class myDownloadObserver final : public nsIDownloadObserver {
  ~myDownloadObserver() {}

 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIDOWNLOADOBSERVER
};
#endif

class WinUtils {
  // Function pointers for APIs that may not be available depending on
  // the Win10 update version -- will be set up in Initialize().
  static SetThreadDpiAwarenessContextProc sSetThreadDpiAwarenessContext;
  static EnableNonClientDpiScalingProc sEnableNonClientDpiScaling;
  static GetSystemMetricsForDpiProc sGetSystemMetricsForDpi;

  // Set on Initialize().
  static bool sHasPackageIdentity;

 public:
  class AutoSystemDpiAware {
   public:
    AutoSystemDpiAware() {
      MOZ_DIAGNOSTIC_ASSERT(!IsWin32kLockedDown());

      if (sSetThreadDpiAwarenessContext) {
        mPrevContext =
            sSetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
      }
    }

    ~AutoSystemDpiAware() {
      if (sSetThreadDpiAwarenessContext) {
        sSetThreadDpiAwarenessContext(mPrevContext);
      }
    }

   private:
    DPI_AWARENESS_CONTEXT mPrevContext;
  };

  // Wrapper for DefWindowProc that will enable non-client dpi scaling on the
  // window during creation.
  static LRESULT WINAPI NonClientDpiScalingDefWindowProcW(HWND hWnd, UINT msg,
                                                          WPARAM wParam,
                                                          LPARAM lParam);

  /**
   * Get the system's default logical-to-physical DPI scaling factor,
   * which is based on the primary display. Note however that unlike
   * LogToPhysFactor(GetPrimaryMonitor()), this will not change during
   * a session even if the displays are reconfigured. This scale factor
   * is used by Windows theme metrics etc, which do not fully support
   * dynamic resolution changes but are only updated on logout.
   */
  static double SystemScaleFactor();

  static bool IsPerMonitorDPIAware();
  /**
   * Get the DPI of the given monitor if it's per-monitor DPI aware, otherwise
   * return the system DPI.
   */
  static float MonitorDPI(HMONITOR aMonitor);
  static float SystemDPI();
  /**
   * Functions to convert between logical pixels as used by most Windows APIs
   * and physical (device) pixels.
   */
  static double LogToPhysFactor(HMONITOR aMonitor);
  static double LogToPhysFactor(HWND aWnd);
  static double LogToPhysFactor(HDC aDC) {
    return LogToPhysFactor(::WindowFromDC(aDC));
  }
  static int32_t LogToPhys(HMONITOR aMonitor, double aValue);
  static HMONITOR GetPrimaryMonitor();
  static HMONITOR MonitorFromRect(const gfx::Rect& rect);

  static bool HasSystemMetricsForDpi();
  static int GetSystemMetricsForDpi(int nIndex, UINT dpi);

  /**
   * @param msg Windows event message
   * @return User-friendly event name, or nullptr if no
   *         match is found.
   */
  static const char* WinEventToEventName(UINT msg);

  /**
   * @param aHdc HDC for printer
   * @return unwritable margins for currently set page on aHdc or empty margins
   *         if aHdc is null
   */
  static gfx::MarginDouble GetUnwriteableMarginsForDeviceInInches(HDC aHdc);

  static bool HasPackageIdentity() { return sHasPackageIdentity; }

  /*
   * The "family name" of a Windows app package is the full name without any of
   * the components that might change during the life cycle of the app (such as
   * the version number, or the architecture). This leaves only those properties
   * which together serve to uniquely identify the app within one Windows
   * installation, namely the base name and the publisher name. Meaning, this
   * string is safe to use anywhere that a string uniquely identifying an app
   * installation is called for (because multiple copies of the same app on the
   * same system is not a supported feature in the app framework).
   */
  static nsString GetPackageFamilyName();

  /**
   * Logging helpers that dump output to prlog module 'Widget', console, and
   * OutputDebugString. Note these output in both debug and release builds.
   */
  static void Log(const char* fmt, ...);
  static void LogW(const wchar_t* fmt, ...);

  /**
   * PeekMessage() and GetMessage() are wrapper methods for PeekMessageW(),
   * GetMessageW(), ITfMessageMgr::PeekMessageW() and
   * ITfMessageMgr::GetMessageW().
   * Don't call the native APIs directly.  You MUST use these methods instead.
   */
  static bool PeekMessage(LPMSG aMsg, HWND aWnd, UINT aFirstMessage,
                          UINT aLastMessage, UINT aOption);
  static bool GetMessage(LPMSG aMsg, HWND aWnd, UINT aFirstMessage,
                         UINT aLastMessage);

  /**
   * Wait until a message is ready to be processed.
   * Prefer using this method to directly calling ::WaitMessage since
   * ::WaitMessage will wait if there is an unread message in the queue.
   * That can cause freezes until another message enters the queue if the
   * message is marked read by a call to PeekMessage which the caller is
   * not aware of (e.g., from a different thread).
   * Note that this method may cause sync dispatch of sent (as opposed to
   * posted) messages.
   * @param aTimeoutMs Timeout for waiting in ms, defaults to INFINITE
   */
  static void WaitForMessage(DWORD aTimeoutMs = INFINITE);

  /**
   * GetTopLevelHWND() returns a window handle of the top level window which
   * aWnd belongs to.  Note that the result may not be our window, i.e., it
   * may not be managed by nsWindow.
   *
   * See follwing table for the detail of the result window type.
   *
   * +-------------------------+-----------------------------------------------+
   * |                         |                aStopIfNotPopup                |
   * +-------------------------+-----------------------+-----------------------+
   * |                         |         TRUE          |         FALSE         |
   + +-----------------+-------+-----------------------+-----------------------+
   * |                 |       |  * an independent top level window            |
   * |                 | TRUE  |  * a popup window (WS_POPUP)                  |
   * |                 |       |  * an owned top level window (like dialog)    |
   * | aStopIfNotChild +-------+-----------------------+-----------------------+
   * |                 |       |  * independent window | * only an independent |
   * |                 | FALSE |  * non-popup-owned-   |   top level window    |
   * |                 |       |    window like dialog |                       |
   * +-----------------+-------+-----------------------+-----------------------+
   */
  static HWND GetTopLevelHWND(HWND aWnd, bool aStopIfNotChild = false,
                              bool aStopIfNotPopup = true);

  /**
   * SetNSWindowPtr() associates aWindow with aWnd. If aWidget is nullptr, it
   * instead dissociates any nsWindow from aWnd.
   *
   * No AddRef is performed. May not be used off of the main thread.
   */
  static void SetNSWindowPtr(HWND aWnd, nsWindow* aWindow);
  /**
   * GetNSWindowPtr() returns a pointer to the associated nsWindow pointer, if
   * one exists, or nullptr, if not.
   *
   * No AddRef is performed. May not be used off of the main thread.
   */
  static nsWindow* GetNSWindowPtr(HWND aWnd);

  /**
   * IsOurProcessWindow() returns TRUE if aWnd belongs our process.
   * Otherwise, FALSE.
   */
  static bool IsOurProcessWindow(HWND aWnd);

  /**
   * FindOurProcessWindow() returns the nearest ancestor window which
   * belongs to our process.  If it fails to find our process's window by the
   * top level window, returns nullptr.  And note that this is using
   * ::GetParent() for climbing the window hierarchy, therefore, it gives
   * up at an owned top level window except popup window (e.g., dialog).
   */
  static HWND FindOurProcessWindow(HWND aWnd);

  /**
   * FindOurWindowAtPoint() returns the topmost child window which belongs to
   * our process's top level window.
   *
   * NOTE: the topmost child window may NOT be our process's window like a
   *       plugin's window.
   */
  static HWND FindOurWindowAtPoint(const POINT& aPointInScreen);

  /**
   * InitMSG() returns an MSG struct which was initialized by the params.
   * Don't trust the other members in the result.
   */
  static MSG InitMSG(UINT aMessage, WPARAM wParam, LPARAM lParam, HWND aWnd);

  /**
   * GetScanCode() returns a scan code for the LPARAM of WM_KEYDOWN, WM_KEYUP,
   * WM_CHAR and WM_UNICHAR.
   *
   */
  static WORD GetScanCode(LPARAM aLParam) { return (aLParam >> 16) & 0xFF; }

  /**
   * IsExtendedScanCode() returns TRUE if the LPARAM indicates the key message
   * is an extended key event.
   */
  static bool IsExtendedScanCode(LPARAM aLParam) {
    return (aLParam & 0x1000000) != 0;
  }

  /**
   * GetMouseInputSource() returns a pointing device information.  The value is
   * one of MouseEvent_Binding::MOZ_SOURCE_*.  This method MUST be called during
   * mouse message handling.
   */
  static uint16_t GetMouseInputSource();

  /**
   * Windows also fires mouse window messages for pens and touches, so we should
   * retrieve their pointer ID on receiving mouse events as well. Please refer
   * to
   * https://msdn.microsoft.com/en-us/library/windows/desktop/ms703320(v=vs.85).aspx
   */
  static uint16_t GetMousePointerID();

  static bool GetIsMouseFromTouch(EventMessage aEventType);

  /**
   * ConvertHRGNToRegion converts a Windows HRGN to an LayoutDeviceIntRegion.
   *
   * aRgn the HRGN to convert.
   * returns the LayoutDeviceIntRegion.
   */
  static LayoutDeviceIntRegion ConvertHRGNToRegion(HRGN aRgn);
  /** Performs the inverse of ConvertHRGNToRegion. */
  static nsAutoRegion RegionToHRGN(const LayoutDeviceIntRegion&);

  /**
   * ToIntRect converts a Windows RECT to a LayoutDeviceIntRect.
   *
   * aRect the RECT to convert.
   * returns the LayoutDeviceIntRect.
   */
  static LayoutDeviceIntRect ToIntRect(const RECT& aRect);
  /** Performs the inverse of ToIntRect */
  static RECT ToWinRect(const LayoutDeviceIntRect& aRect);

  /**
   * Returns true if the context or IME state is enabled.  Otherwise, false.
   */
  static bool IsIMEEnabled(const InputContext& aInputContext);
  static bool IsIMEEnabled(IMEEnabled aIMEState);

  /**
   * Returns modifier key array for aModifiers.  This is for
   * nsIWidget::SynthethizeNative*Event().
   */
  static void SetupKeyModifiersSequence(nsTArray<KeyPair>* aArray,
                                        uint32_t aModifiers, UINT aMessage);

  /**
   * Does device have touch support
   */
  static uint32_t IsTouchDeviceSupportPresent();

  /**
   * The maximum number of simultaneous touch contacts supported by the device.
   * In the case of devices with multiple digitizers (e.g. multiple touch
   * screens), the value will be the maximum of the set of maximum supported
   * contacts by each individual digitizer.
   */
  static uint32_t GetMaxTouchPoints();

  /**
   * Returns the windows power platform role, which is useful for detecting
   * tablets.
   */
  static POWER_PLATFORM_ROLE GetPowerPlatformRole();

  // For pointer and hover media queries features.
  static PointerCapabilities GetPrimaryPointerCapabilities();
  // For any-pointer and any-hover media queries features.
  static PointerCapabilities GetAllPointerCapabilities();

  // Returns whether the system has any active device for each pointer type.
  static bool SystemHasMouse();
  static bool SystemHasTouch();
  static bool SystemHasPen();

  /**
   * Fully resolves a path to its final path name. So if path contains
   * junction points or symlinks to other folders, we'll resolve the path
   * fully to the actual path that the links target.
   *
   * @param aPath path to be resolved.
   * @return true if successful, including if nothing needs to be changed.
   *         false if something failed or aPath does not exist, aPath will
   *               remain unchanged.
   */
  static bool ResolveJunctionPointsAndSymLinks(std::wstring& aPath);
  static bool ResolveJunctionPointsAndSymLinks(nsIFile* aPath);

  /**
   * Returns true if executable's path is on a network drive.
   */
  static bool RunningFromANetworkDrive();

  static void Initialize();

  static nsresult WriteBitmap(nsIFile* aFile,
                              mozilla::gfx::SourceSurface* surface);
  // This function is a helper, but it cannot be called from the main thread.
  // Use the one above!
  static nsresult WriteBitmap(nsIFile* aFile, imgIContainer* aImage);

  /**
   * Wrapper for PathCanonicalize().
   * Upon success, the resulting output string length is <= MAX_PATH.
   * @param  aPath [in,out] The path to transform.
   * @return true on success, false on failure.
   */
  static bool CanonicalizePath(nsAString& aPath);

  /**
   * Converts short paths (e.g. "C:\\PROGRA~1\\XYZ") to full paths.
   * Upon success, the resulting output string length is <= MAX_PATH.
   * @param  aPath [in,out] The path to transform.
   * @return true on success, false on failure.
   */
  static bool MakeLongPath(nsAString& aPath);

  /**
   * Wrapper for PathUnExpandEnvStringsW().
   * Upon success, the resulting output string length is <= MAX_PATH.
   * @param  aPath [in,out] The path to transform.
   * @return true on success, false on failure.
   */
  static bool UnexpandEnvVars(nsAString& aPath);

  /**
   * Retrieve a semicolon-delimited list of DLL files derived from AppInit_DLLs
   */
  static bool GetAppInitDLLs(nsAString& aOutput);

  enum class PathTransformFlags : uint32_t {
    Canonicalize = 1,
    Lengthen = 2,
    UnexpandEnvVars = 4,
    RequireFilePath = 8,

    Default = 7,  // Default omits RequireFilePath
  };

  /**
   * Given a path, transforms it in preparation to be reported via telemetry.
   * That can include canonicalization, converting short to long paths,
   * unexpanding environment strings, and removing potentially sensitive data
   * from the path.
   *
   * @param  aPath  [in,out] The path to transform.
   * @param  aFlags [in] Specifies which transformations to perform, allowing
   *                the caller to skip operations they know have already been
   *                performed.
   * @return true on success, false on failure.
   */
  static bool PreparePathForTelemetry(
      nsAString& aPath,
      PathTransformFlags aFlags = PathTransformFlags::Default);

  static const size_t kMaxWhitelistedItems = 3;
  using WhitelistVec =
      Vector<std::pair<nsString, nsDependentString>, kMaxWhitelistedItems>;

  static const WhitelistVec& GetWhitelistedPaths();

  static bool GetClassName(HWND aHwnd, nsAString& aName);

  static bool MicaAvailable();
  static bool MicaEnabled();
  static bool MicaPopupsEnabled();
  static void UpdateMicaInAllWindows();

  static void EnableWindowOcclusion(const bool aEnable);

  static bool GetTimezoneName(wchar_t* aBuffer);

#ifdef DEBUG
  static nsresult SetHiDPIMode(bool aHiDPI);
  static nsresult RestoreHiDPIMode();
#endif

  static bool GetAutoRotationState(AR_STATE* aRotationState);

  static void GetClipboardFormatAsString(UINT aFormat, nsAString& aOutput);

  static nsresult GetProcessImageName(DWORD aProcessId, nsAString& aName);

 private:
  static WhitelistVec BuildWhitelist();

 public:
#ifdef ACCESSIBILITY
  static a11y::LocalAccessible* GetRootAccessibleForHWND(HWND aHwnd);
#endif
};

typedef MozPromise<nsString, nsresult, true> ObtainCachedIconFileAsyncPromise;

/**
 * Asynchronously tries add the list to the build
 */
class AsyncEncodeAndWriteIcon : public nsIRunnable {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIRUNNABLE

  // Warning: AsyncEncodeAndWriteIcon assumes ownership of the aData buffer
  // passed in
  AsyncEncodeAndWriteIcon(
      const nsAString& aIconPath, UniquePtr<uint8_t[]> aData, uint32_t aStride,
      uint32_t aWidth, uint32_t aHeight,
      already_AddRefed<nsIRunnable> aRunnable,
      UniquePtr<MozPromiseHolder<ObtainCachedIconFileAsyncPromise>>
          aPromiseHolder = nullptr);

 private:
  virtual ~AsyncEncodeAndWriteIcon();

  nsAutoString mIconPath;
  UniquePtr<uint8_t[]> mBuffer;
  nsCOMPtr<nsIRunnable> mRunnable;
  UniquePtr<MozPromiseHolder<ObtainCachedIconFileAsyncPromise>> mPromiseHolder;
  uint32_t mStride;
  uint32_t mWidth;
  uint32_t mHeight;
};

class AsyncDeleteAllFaviconsFromDisk : public nsIRunnable {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIRUNNABLE

  explicit AsyncDeleteAllFaviconsFromDisk(bool aIgnoreRecent = false);

 private:
  virtual ~AsyncDeleteAllFaviconsFromDisk();

  int32_t mIcoNoDeleteSeconds;
  bool mIgnoreRecent;
  nsCOMPtr<nsIFile> mJumpListCacheDir;
};

class FaviconHelper {
 public:
  enum class IconCacheDir : uint8_t {
    JumpListCacheDir = 1,
    ShortcutCacheDir = 2
  };

  static const char kJumpListCacheDir[];
  static const char kShortcutCacheDir[];
  static nsresult ObtainCachedIconFile(
      nsCOMPtr<nsIURI> aFaviconPageURI, nsString& aICOFilePath,
      RefPtr<LazyIdleThread>& aIOThread, bool aURLShortcut,
      already_AddRefed<nsIRunnable> aRunnable = nullptr);
  static RefPtr<ObtainCachedIconFileAsyncPromise> ObtainCachedIconFileAsync(
      nsCOMPtr<nsIURI> aFaviconPageURI, RefPtr<LazyIdleThread>& aIOThread,
      IconCacheDir aCacheDir);

  static nsresult GetOutputIconPath(nsCOMPtr<nsIURI> aFaviconPageURI,
                                    nsCOMPtr<nsIFile>& aICOFile,
                                    bool aURLShortcut);

  static nsresult CacheIconFileFromFaviconURIAsync(
      nsCOMPtr<nsIURI> aFaviconPageURI, nsCOMPtr<nsIFile> aICOFile,
      RefPtr<nsISerialEventTarget> aIOThread, bool aURLShortcut,
      already_AddRefed<nsIRunnable> aRunnable,
      UniquePtr<MozPromiseHolder<ObtainCachedIconFileAsyncPromise>>
          aPromiseHolder);

  static int32_t GetICOCacheSecondsTimeout();
};

MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS(WinUtils::PathTransformFlags);

// RTL shim windows are temporary child windows of our nsWindows created to
// address RTL issues in picker dialogs. (See bug 588735.)
class ScopedRtlShimWindow {
 public:
  explicit ScopedRtlShimWindow(nsIWidget* aParent);
  ~ScopedRtlShimWindow();

  ScopedRtlShimWindow(const ScopedRtlShimWindow&) = delete;
  ScopedRtlShimWindow(ScopedRtlShimWindow&& that) noexcept : mWnd(that.mWnd) {
    that.mWnd = nullptr;
  };

  HWND get() const { return mWnd; }

 private:
  HWND mWnd;
};

}  // namespace widget
}  // namespace mozilla

#endif  // mozilla_widget_WinUtils_h__
