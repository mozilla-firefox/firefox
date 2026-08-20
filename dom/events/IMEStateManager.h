/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_IMEStateManager_h_
#define mozilla_IMEStateManager_h_

#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/EventForwards.h"
#include "mozilla/Maybe.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/dom/BrowserParent.h"
#include "nsIWidget.h"

class nsIContent;
class nsINode;
class nsIURI;
class nsPresContext;

namespace mozilla {

class EditorBase;
class EventDispatchingCallback;
class IMEContentObserver;
class PseudoFocusChangeRunnable;
class TextCompositionArray;
class TextComposition;

namespace dom {
class Element;
class Selection;
}  // namespace dom

/**
 * IMEStateManager manages InputContext (e.g., active editor type, IME enabled
 * state and IME open state) of nsIWidget instances, manages IMEContentObserver
 * and provides useful API for IME.
 */

class IMEStateManager {
  using BrowserParent = dom::BrowserParent;
  using IMEMessage = widget::IMEMessage;
  using IMENotification = widget::IMENotification;
  using IMEState = widget::IMEState;
  using InputContext = widget::InputContext;
  using InputContextAction = widget::InputContextAction;

 public:
  static void Init();
  static void Shutdown();

  /**
   * GetActiveBrowserParent() returns a pointer to a BrowserParent instance
   * which is managed by the focused content (sFocusedElement).  If the focused
   * content isn't managing another process, this returns nullptr.
   */
  static BrowserParent* GetActiveBrowserParent() {
    // If menu has pseudo focus, we should ignore active child process.
    if (sInstalledMenuKeyboardListener) {
      return nullptr;
    }
    // If we know focused browser parent, use it for making any events related
    // to composition go to same content process.
    if (sFocusedIMEBrowserParent) {
      return sFocusedIMEBrowserParent;
    }
    return BrowserParent::GetFocused();
  }

  /**
   * DoesBrowserParentHaveIMEFocus() returns true when aBrowserParent has IME
   * focus, i.e., the BrowserParent sent "focus" notification but not yet sends
   * "blur". Note that this doesn't check if the remote processes are same
   * because if another BrowserParent has focus, committing composition causes
   * firing composition events in different BrowserParent.  (Anyway, such case
   * shouldn't occur.)
   */
  static bool DoesBrowserParentHaveIMEFocus(
      const BrowserParent* aBrowserParent) {
    MOZ_ASSERT(aBrowserParent);
    return sFocusedIMEBrowserParent == aBrowserParent;
  }

  /**
   * If CanSendNotificationToWidget() returns false (it should occur
   * only in a content process), we shouldn't notify the widget of
   * any focused editor changes since the content process was blurred.
   * Also, even if content process, widget has native text event dispatcher such
   * as Android, it still notify it.
   */
  static bool CanSendNotificationToWidget() {
#ifdef MOZ_WIDGET_ANDROID
    return true;
#else
    return !sCleaningUpForStoppingIMEStateManagement;
#endif
  }

  /**
   * Focus moved between browsers from aBlur to aFocus. (nullptr means the
   * chrome process.)
   */
  static void OnFocusMovedBetweenBrowsers(BrowserParent* aBlur,
                                          BrowserParent* aFocus);

  /**
   * Called when aWidget is being deleted.
   */
  static void WidgetDestroyed(nsIWidget* aWidget);

  /**
   * Called when a widget exists when the app is quitting
   */
  static void WidgetOnQuit(nsIWidget* aWidget);

  /**
   * GetWidgetForActiveInputContext() returns a widget which IMEStateManager
   * is managing input context with.  If a widget instance needs to cache
   * the last input context for nsIWidget::GetInputContext() or something,
   * it should check if its cache is valid with this method before using it
   * because if this method returns another instance, it means that
   * IMEStateManager may have already changed shared input context via the
   * widget.
   */
  static nsIWidget* GetWidgetForActiveInputContext() {
    return sActiveInputContextWidget;
  }

  /**
   * Return a widget which is for handling text input. This should be valid
   * while an editable element has focus or an editable document has focus.
   */
  static nsIWidget* GetWidgetForTextInputHandling() {
    return sTextInputHandlingWidget;
  }

  /**
   * SetIMEContextForChildProcess() is called when aBrowserParent receives
   * SetInputContext() from the remote process.
   */
  static void SetInputContextForChildProcess(BrowserParent* aBrowserParent,
                                             const InputContext& aInputContext,
                                             const InputContextAction& aAction);

  /**
   * StopIMEStateManagement() is called when the process should stop managing
   * IME state.
   */
  static void StopIMEStateManagement();

  /**
   * MaybeStartOffsetUpdatedInChild() is called when composition start offset
   * is maybe updated in the child process.  I.e., even if it's not updated,
   * this is called and never called if the composition is in this process.
   * @param aWidget             The widget whose native IME context has the
   *                            composition.
   * @param aStartOffset        New composition start offset with native
   *                            linebreaks.
   */
  static void MaybeStartOffsetUpdatedInChild(nsIWidget* aWidget,
                                             uint32_t aStartOffset);

  MOZ_CAN_RUN_SCRIPT static nsresult OnDestroyPresContext(
      nsPresContext& aPresContext);
  MOZ_CAN_RUN_SCRIPT static nsresult OnRemoveContent(
      nsPresContext& aPresContext, dom::Element& aElement);
  /**
   * Called when the parent chain of the observing element of IMEContentObserver
   * is changed.
   *
   * @param aObserver   The IMEContentObserver which received the notification.
   * @param aContent    The topmost content which is changed.
   */
  MOZ_CAN_RUN_SCRIPT static void OnParentChainChangedOfObservingElement(
      IMEContentObserver& aObserver, nsIContent& aContent);

  /**
   * Called when HTMLEditor updates the root element which is <body> of the
   * document if there is (or the document element otherwise).
   */
  MOZ_CAN_RUN_SCRIPT static void OnUpdateHTMLEditorRootElement(
      HTMLEditor& aHTMLEditor, dom::Element* aNewRootElement);

  /**
   * OnChangeFocus() should be called when focused content is changed or
   * IME enabled state is changed.  If nobody has focus, set both aPresContext
   * and aContent nullptr.  E.g., all windows are deactivated.  Otherwise,
   * set focused element (even if it won't receive `focus`event) and
   * corresponding nsPresContext for it.  Then, IMEStateManager can avoid
   * handling delayed notifications from the others with verifying the
   * focused element.
   */
  MOZ_CAN_RUN_SCRIPT static nsresult OnChangeFocus(
      nsPresContext* aPresContext, dom::Element* aElement,
      InputContextAction::Cause aCause);

  /**
   * OnInstalledMenuKeyboardListener() is called when menu keyboard listener
   * is installed or uninstalled in the process.  So, even if menu keyboard
   * listener was installed in chrome process, this won't be called in content
   * processes.
   *
   * @param aInstalling     true if menu keyboard listener is installed.
   *                        Otherwise, i.e., menu keyboard listener is
   *                        uninstalled, false.
   */
  MOZ_CAN_RUN_SCRIPT static void OnInstalledMenuKeyboardListener(
      bool aInstalling);

  // These two methods manage focus and selection/text observers.
  // They are separate from OnChangeFocus above because this offers finer
  // control compared to having the two methods incorporated into OnChangeFocus

  // Get the focused editor's selection and root
  static nsresult GetFocusSelectionAndRootElement(dom::Selection** aSel,
                                                  dom::Element** aRootElement);
  // This method updates the current IME state.  However, if the enabled state
  // isn't changed by the new state, this method does nothing.
  // Note that this method changes the IME state of the active element in the
  // widget.  So, the caller must have focus.
  // XXX Changing this to MOZ_CAN_RUN_SCRIPT requires too many callers to be
  //     marked too.  Probably, we should initialize IMEContentObserver
  //     asynchronously.
  enum class UpdateIMEStateOption {
    ForceUpdate,
    DontCommitComposition,
  };
  using UpdateIMEStateOptions = EnumSet<UpdateIMEStateOption, uint32_t>;
  MOZ_CAN_RUN_SCRIPT static void UpdateIMEState(
      const IMEState& aNewIMEState, dom::Element* aElement,
      EditorBase& aEditorBase, const UpdateIMEStateOptions& aOptions = {});

  // This method is called when user operates mouse button in focused editor
  // and before the editor handles it.
  // Returns true if IME consumes the event.  Otherwise, false.
  MOZ_CAN_RUN_SCRIPT static bool OnMouseButtonEventInEditor(
      nsPresContext& aPresContext, dom::Element* aElement,
      WidgetMouseEvent& aMouseEvent);

  // This method is called when user clicked in an editor.
  // aElement must be:
  //   If the editor is for <input> or <textarea>, the element.
  //   If the editor is for contenteditable, the active editinghost.
  //   If the editor is for designMode, nullptr.
  MOZ_CAN_RUN_SCRIPT static void OnClickInEditor(
      nsPresContext& aPresContext, dom::Element* aElement,
      const WidgetMouseEvent& aMouseEvent);

  // This method is called when editor actually gets focus.
  // aContent must be:
  //   If the editor is for <input> or <textarea>, the element.
  //   If the editor is for contenteditable, the active editinghost.
  //   If the editor is for designMode, nullptr.
  static void OnFocusInEditor(nsPresContext& aPresContext,
                              dom::Element* aElement, EditorBase& aEditorBase);

  // This method is called when the editor is initialized.
  static void OnEditorInitialized(EditorBase& aEditorBase);

  // This method is called when the editor is (might be temporarily) being
  // destroyed.
  static void OnEditorDestroying(EditorBase& aEditorBase);

  // This method is called when focus is set to same content again.
  MOZ_CAN_RUN_SCRIPT static void OnReFocus(nsPresContext& aPresContext,
                                           dom::Element& aElement);

  // This method is called when designMode is set to "off" or an editing host
  // becomes not editable due to removing `contenteditable` attribute or setting
  // it to "false".
  MOZ_CAN_RUN_SCRIPT static void MaybeOnEditableStateDisabled(
      nsPresContext& aPresContext, dom::Element* aElement);

  /**
   * All composition events must be dispatched via DispatchCompositionEvent()
   * for storing the composition target and ensuring a set of composition
   * events must be fired the stored target.  If the stored composition event
   * target is destroying, this removes the stored composition automatically.
   */
  MOZ_CAN_RUN_SCRIPT static void DispatchCompositionEvent(
      nsINode* aEventTargetNode, nsPresContext* aPresContext,
      BrowserParent* aBrowserParent, WidgetCompositionEvent* aCompositionEvent,
      nsEventStatus* aStatus, EventDispatchingCallback* aCallBack,
      bool aIsSynthesized = false);

  /**
   * All selection events must be handled via HandleSelectionEvent()
   * because they must be handled by same target as composition events when
   * there is a composition.
   */
  MOZ_CAN_RUN_SCRIPT
  static void HandleSelectionEvent(nsPresContext* aPresContext,
                                   nsIContent* aEventTargetContent,
                                   WidgetSelectionEvent* aSelectionEvent);

  /**
   * This is called when PresShell ignores a composition event due to not safe
   * to dispatch events.
   */
  static void OnCompositionEventDiscarded(
      WidgetCompositionEvent* aCompositionEvent);

  /**
   * Get TextComposition from widget.
   */
  static TextComposition* GetTextCompositionFor(nsIWidget* aWidget);

  /**
   * Returns TextComposition instance for the event.
   */
  static TextComposition* GetTextCompositionFor(
      const WidgetCompositionEvent* aCompositionEvent);

  /**
   * Returns TextComposition instance for the pres context.
   * Be aware, even if another pres context which shares native IME context with
   * specified pres context has composition, this returns nullptr.
   */
  static TextComposition* GetTextCompositionFor(nsPresContext* aPresContext);

  /**
   * Send a notification to IME.  It depends on the IME or platform spec what
   * will occur (or not occur).
   */
  static nsresult NotifyIME(const IMENotification& aNotification,
                            nsIWidget* aWidget,
                            BrowserParent* aBrowserParent = nullptr);
  static nsresult NotifyIME(IMEMessage aMessage, nsIWidget* aWidget,
                            BrowserParent* aBrowserParent = nullptr);
  static nsresult NotifyIME(IMEMessage aMessage, nsPresContext* aPresContext,
                            BrowserParent* aBrowserParent = nullptr);

  /**
   * Returns active IMEContentObserver but may be nullptr if focused content
   * isn't editable or focus in a remote process.
   */
  static IMEContentObserver* GetActiveContentObserver();

  /**
   * Return focused element which was notified by a OnChangeFocus() call.
   */
  static dom::Element* GetFocusedElement();

 protected:
  MOZ_CAN_RUN_SCRIPT static nsresult OnChangeFocusInternal(
      nsPresContext* aPresContext, dom::Element* aElement,
      InputContextAction aAction);
  MOZ_CAN_RUN_SCRIPT static void SetIMEState(const IMEState& aState,
                                             const nsPresContext* aPresContext,
                                             dom::Element* aElement,
                                             nsIWidget& aWidget,
                                             InputContextAction aAction,
                                             InputContext::Origin aOrigin);
  static void SetInputContext(nsIWidget& aWidget,
                              const InputContext& aInputContext,
                              const InputContextAction& aAction);
  static IMEState GetNewIMEState(const nsPresContext& aPresContext,
                                 dom::Element* aElement);

  /**
   * Return a URI which is exposable via the native IME API to the system or
   * IME.
   */
  static already_AddRefed<nsIURI> GetExposableURL(
      const nsPresContext* aPresContext);

  static void EnsureTextCompositionArray();

  // XXX Changing this to MOZ_CAN_RUN_SCRIPT requires too many callers to be
  //     marked too.  Probably, we should initialize IMEContentObserver
  //     asynchronously.
  MOZ_CAN_RUN_SCRIPT_BOUNDARY static void CreateIMEContentObserver(
      EditorBase& aEditorBase, dom::Element* aFocusedElement);

  /**
   * Check whether the content matches or does not match with focus information
   * which is previously notified via OnChangeFocus();
   */
  [[nodiscard]] static bool IsFocusedElement(
      const nsPresContext& aPresContext, const dom::Element* aFocusedElement);

  static void DestroyIMEContentObserver();

  [[nodiscard]] static bool IsIMEObserverNeeded(const IMEState& aState);

  [[nodiscard]] static nsIContent* GetRootContent(nsPresContext* aPresContext);

  /**
   * CanHandleWith() returns false if it's destroyed.
   */
  [[nodiscard]] static bool CanHandleWith(const nsPresContext* aPresContext);

  /**
   * ResetActiveChildInputContext() resets sActiveChildInputContext.
   * So, HasActiveChildSetInputContext() will return false until a remote
   * process gets focus and set input context.
   */
  static void ResetActiveChildInputContext();

  /**
   * HasActiveChildSetInputContext() returns true if a remote tab has focus
   * and it has already set input context.  Otherwise, returns false.
   */
  static bool HasActiveChildSetInputContext();

  static void AdvanceFocusGeneration() {
    if (sFocusGeneration == UINT32_MAX) [[unlikely]] {
      sFocusGeneration = 0;
    } else {
      sFocusGeneration++;
    }
  }

  /**
   * This is the runner of OnInstalledMenuKeyboardListener(), called by
   * PseudoFocusChangeRunnable maybe asynchronously.
   *
   * @param aCaller             The caller instance, used only for debug.
   * @param aSetPseudoFocus     Whether the menu keyboard listener is installed
   *                            or uninstalled when
   *                            OnInstalledMenuKeyboardListener() is called and
   *                            the PseudoFocusChangeRunnable instance is
   *                            created.
   * @param aFocusedPresContextAtRequested
   *                            sFocusedPresContext when
   *                            OnInstalledMenuKeyboardListener() is called and
   *                            the PseudoFocusChangeRunnable instance is
   *                            created.
   */
  MOZ_CAN_RUN_SCRIPT static void SetMenubarPseudoFocus(
      PseudoFocusChangeRunnable* aCaller, bool aSetPseudoFocus,
      nsPresContext* aFocusedPresContextAtRequested);

  // sFocusedElement and sFocusedPresContext are the focused content and
  // PresContext.  If a document has focus but there is no focused element,
  // sFocusedElement may be nullptr.
  static StaticRefPtr<dom::Element> sFocusedElement;
  static StaticRefPtr<nsPresContext> sFocusedPresContext;
  // sTextInputHandlingWidget is cache for the result of
  // sFocusedPresContext->GetTextInputHandlingWidget().  Even after
  // sFocusedPresContext has gone, we need to clean up some IME state on the
  // widget if the widget is available.
  // Note that this is cleared when the widget is being destroyed.
  static nsIWidget* sTextInputHandlingWidget;
  // sFocusedIMEBrowserParent is the tab parent, which send "focus" notification
  // to sFocusedIMEWidget (and didn't yet sent "blur" notification).
  // Note that this is cleared when the widget is being destroyed.
  static nsIWidget* sFocusedIMEWidget;
  static StaticRefPtr<BrowserParent> sFocusedIMEBrowserParent;
  // sActiveInputContextWidget is the last widget whose SetInputContext() is
  // called.  This is important to reduce sync IPC cost with parent process.
  // If IMEStateManager set input context to different widget, PuppetWidget can
  // return cached input context safely.
  // Note that this is cleared when the widget is being destroyed.
  static nsIWidget* sActiveInputContextWidget;
  // sActiveIMEContentObserver points to the currently active
  // IMEContentObserver.  This is null if there is no focused editor.
  static StaticRefPtr<IMEContentObserver> sActiveIMEContentObserver;

  // All active compositions in the process are stored by this array.
  // When you get an item of this array and use it, please be careful.
  // The instances in this array can be destroyed automatically if you do
  // something to cause committing or canceling the composition.
  static TextCompositionArray* sTextCompositions;

  // Origin type of current process.
  static InputContext::Origin sOrigin;

  // sActiveChildInputContext is valid only when BrowserParent::GetFocused() is
  // not nullptr.  This stores last information of input context in the remote
  // process of BrowserParent::GetFocused().  I.e., they are set when
  // SetInputContextForChildProcess() is called.  This is necessary for
  // restoring IME state when menu keyboard listener is uninstalled.
  static InputContext sActiveChildInputContext;

  // The sequencial number of focus changes.
  static uint32_t sFocusGeneration;

  // sInstalledMenuKeyboardListener is true if menu keyboard listener is
  // installed in the process.
  static bool sInstalledMenuKeyboardListener;

  static bool sIsGettingNewIMEState;
  static bool sCheckForIMEUnawareWebApps;

  // Set to true only if this is an instance in a content process and
  // only while `IMEStateManager::StopIMEStateManagement()`.
  static bool sCleaningUpForStoppingIMEStateManagement;

  // Set to true when:
  // - In the main process, a window belonging to this app is active in the
  //   desktop.
  // - In a content process, the process has focus.
  //
  // This is updated by `OnChangeFocusInternal()` is called in the main
  // process.  Therefore, this indicates the active state which
  // `IMEStateManager` notified the focus change, there is timelag from
  // the `nsFocusManager`'s status update.  This allows that all methods
  // to handle something specially when they are called while the process
  // is being activated or inactivated.  E.g., `OnFocusMovedBetweenBrowsers()`
  // is called twice before `OnChangeFocusInternal()` when the main process
  // becomes active.  In this case, it wants to wait a following call of
  // `OnChangeFocusInternal()` to keep active composition.  See also below.
  static bool sIsActive;

  // While the application is being activated, `OnFocusMovedBetweenBrowsers()`
  // are called twice before `OnChangeFocusInternal()`.  First time, aBlur is
  // the last focused `BrowserParent` at deactivating and aFocus is always
  // `nullptr`.  Then, it'll be called again with actually focused
  // `BrowserParent` when a content in a remote process has focus.  If we need
  // to keep active composition while all windows are deactivated, we shouldn't
  // commit it at the first call since usually, the second call's aFocus
  // and the first call's aBlur are same `BrowserParent`.  For solving this
  // issue, we need to merge the given `BrowserParent`s of multiple calls of
  // `OnFocusMovedBetweenBrowsers()`. The following struct is the data for
  // calling `OnFocusMovedBetweenBrowsers()` later from
  // `OnChangeFocusInternal()`.  Note that focus can be moved even while the
  // main process is not active because JS can change focus.  In such case,
  // composition is committed at that time.  Therefore, this is required only
  // when the main process is activated and there is a composition in a remote
  // process.
  struct PendingFocusedBrowserSwitchingData final {
    RefPtr<BrowserParent> mBrowserParentBlurred;
    RefPtr<BrowserParent> mBrowserParentFocused;

    PendingFocusedBrowserSwitchingData() = delete;
    explicit PendingFocusedBrowserSwitchingData(BrowserParent* aBlur,
                                                BrowserParent* aFocus)
        : mBrowserParentBlurred(aBlur), mBrowserParentFocused(aFocus) {}
  };
  static Maybe<PendingFocusedBrowserSwitchingData>
      sPendingFocusedBrowserSwitchingData;

  class MOZ_STACK_CLASS GettingNewIMEStateBlocker final {
   public:
    GettingNewIMEStateBlocker()
        : mOldValue(IMEStateManager::sIsGettingNewIMEState) {
      IMEStateManager::sIsGettingNewIMEState = true;
    }
    ~GettingNewIMEStateBlocker() {
      IMEStateManager::sIsGettingNewIMEState = mOldValue;
    }

   private:
    bool mOldValue;
  };

  // OnInstalledMenuKeyboardListener may be called when it's not safe.
  // Therefore, it tries to update with adding this as a script runner.
  static StaticRefPtr<PseudoFocusChangeRunnable> sPseudoFocusChangeRunnable;
  friend class PseudoFocusChangeRunnable;
};

}  // namespace mozilla

#endif  // mozilla_IMEStateManager_h_
