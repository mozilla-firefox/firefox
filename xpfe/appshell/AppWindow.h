/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_AppWindow_h_
#define mozilla_AppWindow_h_

// Local Includes
#include "nsChromeTreeOwner.h"
#include "nsContentTreeOwner.h"

// Helper classes
#include "nsCOMPtr.h"
#include "nsTArray.h"
#include "nsString.h"
#include "nsWeakReference.h"
#include "nsDocShell.h"
#include "Units.h"
#include "mozilla/Maybe.h"

// Interfaces needed
#include "nsIBaseWindow.h"
#include "nsIDocShellTreeItem.h"
#include "nsIInterfaceRequestor.h"
#include "nsIAppWindow.h"
#include "nsIPrompt.h"
#include "nsIAuthPrompt.h"
#include "nsIXULBrowserWindow.h"
#include "nsIWidgetListener.h"
#include "nsIRemoteTab.h"
#include "nsIWebProgressListener.h"
#include "nsITimer.h"
#include "nsIXULStore.h"

class nsAtom;
class nsXULTooltipListener;
class nsIOpenWindowInfo;

namespace mozilla {
class PresShell;
class AppWindowTimerCallback;
class L10nReadyPromiseHandler;
namespace dom {
class Element;
}  // namespace dom
namespace widget {
struct InitData;
}  // namespace widget
}  // namespace mozilla

// AppWindow

#define NS_APPWINDOW_IMPL_CID                 \
  {/* 8eaec2f3-ed02-4be2-8e0f-342798477298 */ \
   0x8eaec2f3,                                \
   0xed02,                                    \
   0x4be2,                                    \
   {0x8e, 0x0f, 0x34, 0x27, 0x98, 0x47, 0x72, 0x98}}

class nsContentShellInfo;

namespace mozilla {

class AppWindow final : public nsIBaseWindow,
                        public nsIInterfaceRequestor,
                        public nsIAppWindow,
                        public nsSupportsWeakReference,
                        public nsIWebProgressListener {
  friend class ::nsChromeTreeOwner;
  friend class ::nsContentTreeOwner;

 public:
  // The implementation of non-refcounted nsIWidgetListener, which would hold a
  // strong reference on stack before calling AppWindow's
  // MOZ_CAN_RUN_SCRIPT methods.
  class WidgetListenerDelegate final : public nsIWidgetListener {
   public:
    explicit WidgetListenerDelegate(AppWindow* aAppWindow)
        : mAppWindow(aAppWindow) {}

    MOZ_CAN_RUN_SCRIPT_BOUNDARY
    nsIAppWindow* GetAppWindow() override;
    MOZ_CAN_RUN_SCRIPT_BOUNDARY
    mozilla::PresShell* GetPresShell() override;
    MOZ_CAN_RUN_SCRIPT_BOUNDARY
    void WindowMoved(nsIWidget*, const LayoutDeviceIntPoint&,
                     ByMoveToRect) override;
    MOZ_CAN_RUN_SCRIPT_BOUNDARY
    void WindowResized(nsIWidget*, const LayoutDeviceIntSize&) override;
    MOZ_CAN_RUN_SCRIPT_BOUNDARY
    bool RequestWindowClose(nsIWidget* aWidget) override;
    MOZ_CAN_RUN_SCRIPT_BOUNDARY
    void SizeModeChanged(nsSizeMode sizeMode) override;
    MOZ_CAN_RUN_SCRIPT_BOUNDARY
    void FullscreenChangeFailed(bool aInFullscreen) override;
    MOZ_CAN_RUN_SCRIPT_BOUNDARY
    void MacFullscreenMenubarOverlapChanged(
        mozilla::DesktopCoord aOverlapAmount) override;
    MOZ_CAN_RUN_SCRIPT_BOUNDARY
    void OcclusionStateChanged(bool aIsFullyOccluded) override;
    MOZ_CAN_RUN_SCRIPT_BOUNDARY
    void WindowActivated() override;
    MOZ_CAN_RUN_SCRIPT_BOUNDARY
    void WindowDeactivated() override;

   private:
    // The lifetime of WidgetListenerDelegate is bound to AppWindow so
    // we just use a raw pointer here.
    AppWindow* mAppWindow;
  };

  NS_DECL_THREADSAFE_ISUPPORTS

  NS_DECL_NSIINTERFACEREQUESTOR
  NS_DECL_NSIAPPWINDOW
  NS_DECL_NSIBASEWINDOW

  NS_INLINE_DECL_STATIC_IID(NS_APPWINDOW_IMPL_CID)

  void LockUntilChromeLoad() { mLockedUntilChromeLoad = true; }
  bool IsLocked() const { return mLockedUntilChromeLoad; }
  void IgnoreXULSizeMode(bool aEnable) { mIgnoreXULSizeMode = aEnable; }
  void WasRegistered() { mRegistered = true; }

  using nsIBaseWindow::GetPositionAndSize;
  using nsIBaseWindow::GetSize;

  // AppWindow methods...
  nsresult Initialize(nsIAppWindow* aParent, nsIAppWindow* aOpener,
                      int32_t aInitialWidth, int32_t aInitialHeight,
                      bool aIsHiddenWindow, widget::InitData& widgetInitData,
                      nsIOpenWindowInfo* aOpenWindowInfo);

  nsDocShell* GetDocShell() { return mDocShell; }

  nsresult Toolbar();

  // nsIWebProgressListener
  NS_DECL_NSIWEBPROGRESSLISTENER

  // nsIWidgetListener methods for WidgetListenerDelegate.
  nsIAppWindow* GetAppWindow() { return this; }
  mozilla::PresShell* GetPresShell();
  MOZ_CAN_RUN_SCRIPT
  void WindowMoved(nsIWidget*, const mozilla::LayoutDeviceIntPoint&);
  MOZ_CAN_RUN_SCRIPT
  void WindowResized(nsIWidget*, const mozilla::LayoutDeviceIntSize&);
  MOZ_CAN_RUN_SCRIPT bool RequestWindowClose(nsIWidget* aWidget);
  MOZ_CAN_RUN_SCRIPT void SizeModeChanged(nsSizeMode aSizeMode);
  MOZ_CAN_RUN_SCRIPT void FullscreenChangeFailed(bool aInFullscreen);
  MOZ_CAN_RUN_SCRIPT void FullscreenWillChange(bool aInFullscreen);
  MOZ_CAN_RUN_SCRIPT void FullscreenChanged(bool aInFullscreen);
  MOZ_CAN_RUN_SCRIPT void MacFullscreenMenubarOverlapChanged(
      mozilla::DesktopCoord aOverlapAmount);
  MOZ_CAN_RUN_SCRIPT void OcclusionStateChanged(bool aIsFullyOccluded);
  void RecomputeBrowsingContextVisibility();
  MOZ_CAN_RUN_SCRIPT void WindowActivated();
  MOZ_CAN_RUN_SCRIPT void WindowDeactivated();

  explicit AppWindow(uint32_t aChromeFlags);

 protected:
  enum class PersistentAttribute : uint8_t {
    Position,
    Size,
    Misc,
  };
  using PersistentAttributes = EnumSet<PersistentAttribute>;

  static PersistentAttributes AllPersistentAttributes() {
    return {PersistentAttribute::Position, PersistentAttribute::Size,
            PersistentAttribute::Misc};
  }

  virtual ~AppWindow();

  friend class mozilla::AppWindowTimerCallback;

  MOZ_CAN_RUN_SCRIPT bool ExecuteCloseHandler();
  void ConstrainToOpenerScreen(int32_t* aX, int32_t* aY);

  void SetPersistenceTimer(uint32_t aDirtyFlags);
  void FirePersistenceTimer();

  NS_IMETHOD EnsureChromeTreeOwner();
  NS_IMETHOD EnsureContentTreeOwner();
  NS_IMETHOD EnsurePrimaryContentTreeOwner();
  NS_IMETHOD EnsurePrompter();
  NS_IMETHOD EnsureAuthPrompter();
  NS_IMETHOD ForceRoundedDimensions();
  Maybe<CSSIntSize> GetAvailScreenSize();

  MOZ_CAN_RUN_SCRIPT void FinishFullscreenChange(bool aInFullscreen);

  MOZ_CAN_RUN_SCRIPT_BOUNDARY void SizeShell();
  void OnChromeLoaded();
  void StaggerPosition(int32_t& aRequestedX, int32_t& aRequestedY,
                       int32_t aSpecWidth, int32_t aSpecHeight);
  bool LoadPositionFromXUL(int32_t aSpecWidth, int32_t aSpecHeight);
  bool LoadSizeFromXUL(int32_t& aSpecWidth, int32_t& aSpecHeight);
  void SetSpecifiedSize(const CSSIntSize&);
  MOZ_CAN_RUN_SCRIPT bool UpdateWindowStateFromMiscXULAttributes();
  void SyncAttributesToWidget();
  void SavePersistentAttributes(PersistentAttributes);
  void MaybeSavePersistentPositionAndSize(PersistentAttributes,
                                          dom::Element& aRootElement,
                                          const nsAString& aPersistString);
  void MaybeSavePersistentMiscAttributes(PersistentAttributes,
                                         dom::Element& aRootElement,
                                         const nsAString& aPersistString);
  void SavePersistentAttributes() {
    SavePersistentAttributes(mPersistentAttributesDirty);
  }

  bool NeedsTooltipListener();
  void AddTooltipSupport();
  void RemoveTooltipSupport();

  NS_IMETHOD GetWindowDOMWindow(mozIDOMWindowProxy** aDOMWindow);
  dom::Element* GetWindowDOMElement() const;

  // See nsIDocShellTreeOwner for docs on next two methods
  nsresult ContentShellAdded(nsIDocShellTreeItem* aContentShell, bool aPrimary);
  nsresult ContentShellRemoved(nsIDocShellTreeItem* aContentShell);
  NS_IMETHOD GetPrimaryContentSize(int32_t* aWidth, int32_t* aHeight);
  NS_IMETHOD SetPrimaryContentSize(int32_t aWidth, int32_t aHeight);
  nsresult GetRootShellSize(int32_t* aWidth, int32_t* aHeight);
  nsresult SetRootShellSize(int32_t aWidth, int32_t aHeight);

  NS_IMETHOD SizeShellTo(nsIDocShellTreeItem* aShellItem, int32_t aCX,
                         int32_t aCY);
  NS_IMETHOD ExitModalLoop(nsresult aStatus);
  MOZ_CAN_RUN_SCRIPT
  NS_IMETHOD CreateNewChromeWindow(int32_t aChromeFlags,
                                   nsIAppWindow** _retval);
  MOZ_CAN_RUN_SCRIPT
  NS_IMETHOD CreateNewContentWindow(int32_t aChromeFlags,
                                    nsIOpenWindowInfo* aOpenWindowInfo,
                                    nsIAppWindow** _retval);
  NS_IMETHOD GetHasPrimaryContent(bool* aResult);

  void EnableParent(bool aEnable);
  void PlaceWindowLayersBehind(uint32_t aLowLevel, uint32_t aHighLevel,
                               nsIAppWindow* aBehind);

  enum PersistentAttributeUpdate { Sync, Async };
  void PersistentAttributesDirty(PersistentAttributes,
                                 PersistentAttributeUpdate);

  bool ShouldSavePersistentValues() const;
  void LoadPersistentWindowState();
  nsresult GetPersistentValue(const nsAtom* aAttr, nsAString& aValue);
  void MaybeSetPersistentValue(const nsAtom* aAttr, const nsAString& aValue);

  // Saves window size and positioning values in order to display a very early
  // skeleton UI. This has to happen before we can reasonably initialize the
  // xulstore (i.e., before even loading libxul), so they have to use a special
  // purpose store to do so.
  nsresult MaybeSaveEarlyWindowPersistentValues(
      const LayoutDeviceIntRect& aRect);

  // Gets the uri spec and the window element ID for this window.
  nsresult GetDocXulStoreKeys(nsString& aUriSpec, nsString& aWindowElementId);

  // Enum for the current state of a fullscreen change.
  //
  // It is used to ensure that fullscreen change is issued after both
  // the window state change and the window size change at best effort.
  // This is needed because some platforms can't guarantee the order
  // between such two events.
  //
  // It's changed in the following way:
  // +---------------------------+--------------------------------------+
  // |                           |                                      |
  // |                           v                                      |
  // |                      NotChanging                                 |
  // |                           +                                      |
  // |                           | FullscreenWillChange                 |
  // |                           v                                      |
  // |        +-----------+ WillChange +------------------+             |
  // |        | WindowResized           FullscreenChanged |             |
  // |        v                                           v             |
  // |  WidgetResized                         WidgetEnteredFullscreen   |
  // |        +                              or WidgetExitedFullscreen  |
  // |        | FullscreenChanged                         +             |
  // |        v                          WindowResized or |             |
  // +--------+                          delayed dispatch |             |
  //                                                      v             |
  //                                                      +-------------+
  //
  // The delayed dispatch serves as timeout, which is necessary because it's
  // not even guaranteed that the widget will be resized at all.
  enum class FullscreenChangeState : uint8_t {
    // No current fullscreen change. Any previous change has finished.
    NotChanging,
    // Indicate there is going to be a fullscreen change.
    WillChange,
    // The widget has been resized since WillChange.
    WidgetResized,
    // The widget has entered fullscreen state since WillChange.
    WidgetEnteredFullscreen,
    // The widget has exited fullscreen state since WillChange.
    WidgetExitedFullscreen,
  };

  nsChromeTreeOwner* mChromeTreeOwner;
  nsContentTreeOwner* mContentTreeOwner;
  nsContentTreeOwner* mPrimaryContentTreeOwner;
  nsCOMPtr<nsIWidget> mWindow;
  RefPtr<nsDocShell> mDocShell;
  nsCOMPtr<nsPIDOMWindowOuter> mDOMWindow;
  nsWeakPtr mParentWindow;
  nsCOMPtr<nsIPrompt> mPrompter;
  nsCOMPtr<nsIAuthPrompt> mAuthPrompter;
  nsCOMPtr<nsIXULBrowserWindow> mXULBrowserWindow;
  nsCOMPtr<nsIDocShellTreeItem> mPrimaryContentShell;
  nsresult mModalStatus;
  FullscreenChangeState mFullscreenChangeState;
  bool mContinueModalLoop;
  bool mDebuting;            // being made visible right now
  bool mChromeLoaded;        // True when chrome has loaded
  bool mSizingShellFromXUL;  // true when in SizeShell()
  bool mShowAfterLoad;
  bool mIntrinsicallySized;
  bool mCenterAfterLoad;
  bool mIsHiddenWindow;
  bool mLockedUntilChromeLoad;
  bool mIgnoreXULSize;
  bool mIgnoreXULPosition;
  bool mIgnoreXULSizeMode;
  // mDestroying is used to prevent reentry into into Destroy(), which can
  // otherwise happen due to script running as we tear down various things.
  bool mDestroying;
  bool mRegistered;
  // Indicator for whether the client size, instead of the window size, should
  // be maintained in case of a change in their relation.
  bool mDominantClientSize;
  PersistentAttributes mPersistentAttributesDirty;
  PersistentAttributes mPersistentAttributesMask;
  uint32_t mChromeFlags;
  nsCOMPtr<nsIOpenWindowInfo> mInitialOpenWindowInfo;
  nsString mTitle;

  // The screen rect of the opener.
  mozilla::DesktopIntRect mOpenerScreenRect;

  nsCOMPtr<nsIRemoteTab> mPrimaryBrowserParent;

  nsCOMPtr<nsITimer> mSPTimer;
  WidgetListenerDelegate mWidgetListenerDelegate;

 private:
  MOZ_CAN_RUN_SCRIPT void IntrinsicallySizeShell(const CSSIntSize& aWindowDiff,
                                                 int32_t& aSpecWidth,
                                                 int32_t& aSpecHeight);

  // GetPrimaryBrowserParentSize is called from xpidl methods and we don't have
  // a good way to annotate those with MOZ_CAN_RUN_SCRIPT yet.  It takes no
  // refcounted args other than "this", and the "this" uses seem ok.
  MOZ_CAN_RUN_SCRIPT_BOUNDARY nsresult
  GetPrimaryRemoteTabSize(int32_t* aWidth, int32_t* aHeight);
  nsresult GetPrimaryContentShellSize(int32_t* aWidth, int32_t* aHeight);
  nsresult SetPrimaryRemoteTabSize(int32_t aWidth, int32_t aHeight);
  void SizeShellToWithLimit(int32_t aDesiredWidth, int32_t aDesiredHeight,
                            int32_t shellItemWidth, int32_t shellItemHeight);
  nsresult MoveResize(const Maybe<LayoutDeviceIntPoint>& aPosition,
                      const Maybe<LayoutDeviceIntSize>& aSize, bool aRepaint);
  nsresult MoveResize(const Maybe<DesktopPoint>& aPosition,
                      const Maybe<DesktopSize>& aSize, bool aRepaint);
  nsresult CenterImpl(nsIAppWindow* aRelative, bool aScreen, bool aAlert,
                      bool aAllowCenteringForSizeChange);

  nsCOMPtr<nsIXULStore> mLocalStore;
  bool mIsWidgetInFullscreen = false;
};

}  // namespace mozilla

#endif /* mozilla_AppWindow_h_ */
