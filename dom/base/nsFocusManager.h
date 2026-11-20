/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsFocusManager_h_
#define nsFocusManager_h_

#include "mozilla/Attributes.h"
#include "mozilla/Maybe.h"
#include "mozilla/RefPtr.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/dom/Document.h"
#include "nsCycleCollectionParticipant.h"
#include "nsIContent.h"
#include "nsIFocusManager.h"
#include "nsIObserver.h"
#include "nsPIDOMWindowInlines.h"  // FIXME: Stop including inline definitions!
#include "nsWeakReference.h"

#define FOCUSMANAGER_CONTRACTID "@mozilla.org/focus-manager;1"

class nsIContent;
class nsPIDOMWindowOuter;

namespace mozilla {
class PresShell;
namespace dom {
class Element;
class HTMLAreaElement;
struct FocusOptions;
class BrowserParent;
class ContentChild;
class ContentParent;
}  // namespace dom
}  // namespace mozilla

struct nsDelayedBlurOrFocusEvent;
class LazyFocusActionId;

/**
 * The focus manager keeps track of where the focus is, that is, the node
 * which receives key events.
 */

class nsFocusManager final : public nsIFocusManager,
                             public nsIObserver,
                             public nsSupportsWeakReference {
  using InputContextAction = mozilla::widget::InputContextAction;
  using Document = mozilla::dom::Document;
  friend class mozilla::dom::ContentChild;
  friend class mozilla::dom::ContentParent;

 public:
  NS_DECL_CYCLE_COLLECTION_CLASS_AMBIGUOUS(nsFocusManager, nsIFocusManager)
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS_FINAL
  NS_DECL_NSIOBSERVER
  NS_DECL_NSIFOCUSMANAGER

  // called to initialize and stop the focus manager at startup and shutdown
  static nsresult Init();
  static void Shutdown();

  // Simple helper to call SetFocusedWindow on the instance.
  //
  // This raises the window and switches to the tab as needed.
  MOZ_CAN_RUN_SCRIPT static void FocusWindow(
      nsPIDOMWindowOuter* aWindow, mozilla::dom::CallerType aCallerType);

  MOZ_CAN_RUN_SCRIPT_BOUNDARY static void PrefChanged(const char* aPref,
                                                      void* aSelf);
  MOZ_CAN_RUN_SCRIPT void PrefChanged(const char* aPref);

  /**
   * Retrieve the single focus manager.
   */
  static nsFocusManager* GetFocusManager() { return sInstance; }

  /**
   * A faster version of nsIFocusManager::GetFocusedElement, returning a
   * raw Element pointer (instead of having AddRef-ed Element
   * pointer filled in to an out-parameter).
   */
  mozilla::dom::Element* GetFocusedElement() { return mFocusedElement; }
  static mozilla::dom::Element* GetFocusedElementStatic() {
    return sInstance ? sInstance->GetFocusedElement() : nullptr;
  }

  /**
   * Returns true if aContent currently has focus.
   */
  bool IsFocused(nsIContent* aContent);

  /**
   * Returns true if test mode is enabled.
   */
  bool IsTestMode();

  /**
   * Return a focused window. Version of nsIFocusManager::GetFocusedWindow.
   */
  nsPIDOMWindowOuter* GetFocusedWindow() const { return mFocusedWindow; }
  static nsPIDOMWindowOuter* GetFocusedWindowStatic() {
    return sInstance ? sInstance->GetFocusedWindow() : nullptr;
  }

  /**
   * In the chrome process, retrieves the BrowsingContext corresponding
   * to GetFocusedWindow(). In a content process, retrieves the
   * focused BrowsingContext, which may not belong to this process.
   */
  mozilla::dom::BrowsingContext* GetFocusedBrowsingContext() const {
    if (XRE_IsParentProcess()) {
      if (mFocusedWindow) {
        return mFocusedWindow->GetBrowsingContext();
      }
      return nullptr;
    }
    return mFocusedBrowsingContextInContent;
  }

  /**
   * Returns whether the given browsing context is in the active window.
   */
  bool IsInActiveWindow(mozilla::dom::BrowsingContext*) const;

  /**
   * Return an active window. Version of nsIFocusManager::GetActiveWindow.
   */
  nsPIDOMWindowOuter* GetActiveWindow() const { return mActiveWindow; }

  /**
   * In the chrome process, retrieves the BrowsingContext corresponding
   * to GetActiveWindow(). In a content process, retrieves the
   * BrowsingContext of the top-level Web content in the active tab if
   * in the same process as the caller or nullptr otherwise.
   */
  mozilla::dom::BrowsingContext* GetActiveBrowsingContext() const {
    if (XRE_IsParentProcess()) {
      if (mActiveWindow) {
        return mActiveWindow->GetBrowsingContext();
      }
      return nullptr;
    }
    return mActiveBrowsingContextInContent;
  }

  void ContentInserted(nsIContent* aChild, const ContentInsertInfo& aInfo);

  void ContentAppended(nsIContent* aFirstNewContent,
                       const ContentAppendInfo& aInfo);

  /**
   * Called when content has been removed.
   */
  MOZ_CAN_RUN_SCRIPT nsresult ContentRemoved(Document* aDocument,
                                             nsIContent* aContent,
                                             const ContentRemoveInfo& aInfo);

  void NeedsFlushBeforeEventHandling(mozilla::dom::Element* aElement) {
    if (mFocusedElement == aElement) {
      mEventHandlingNeedsFlush = true;
    }
  }

  bool CanSkipFocus(nsIContent* aContent);

  MOZ_CAN_RUN_SCRIPT void FlushBeforeEventHandlingIfNeeded(
      nsIContent* aContent) {
    if (mEventHandlingNeedsFlush) {
      nsCOMPtr<Document> doc = aContent->GetComposedDoc();
      if (doc) {
        mEventHandlingNeedsFlush = false;
        doc->FlushPendingNotifications(mozilla::FlushType::Layout);
      }
    }
  }

  /**
   * Update the caret with current mode (whether in caret browsing mode or not).
   */
  MOZ_CAN_RUN_SCRIPT void UpdateCaretForCaretBrowsingMode();

  /** @see nsIFocusManager.getLastFocusMethod() */
  uint32_t GetLastFocusMethod(nsPIDOMWindowOuter*) const;

  /**
   * Returns the content node that would be focused if aWindow was in an
   * active window. This will traverse down the frame hierarchy, starting at
   * the given window aWindow. Sets aFocusedWindow to the window with the
   * document containing aFocusedContent. If no element is focused,
   * aFocusedWindow may be still be set -- this means that the document is
   * focused but no element within it is focused.
   *
   * aWindow, aFocusIsOutOfProcess, aFocusedWindow must all be non-null.
   */
  enum SearchRange {
    // Return focused content in aWindow.  So, aFocusedWindow is always aWindow.
    eOnlyCurrentWindow,
    // Return focused content in aWindow or one of all sub windows.
    eIncludeAllDescendants,
    // Return focused content in aWindow or one of visible sub windows.
    eIncludeVisibleDescendants,
  };
  static mozilla::dom::Element* GetFocusedDescendant(
      nsPIDOMWindowOuter* aWindow, SearchRange aSearchRange,
      nsPIDOMWindowOuter** aFocusedWindow);

  /**
   * Helper function for MoveFocus which determines the next element
   * to move the focus to and returns it in aNextContent.
   *
   * aWindow is the window to adjust the focus within, and aStart is
   * the element to start navigation from. For tab key navigation,
   * this should be the currently focused element.
   *
   * aType is the type passed to MoveFocus. If aNoParentTraversal is set,
   * navigation is not done to parent documents and iteration returns to the
   * beginning (or end) of the starting document.
   *
   * aNavigateByKey to move focus by keyboard as a side effect of computing the
   * next target.
   */
  MOZ_CAN_RUN_SCRIPT nsresult DetermineElementToMoveFocus(
      nsPIDOMWindowOuter* aWindow, nsIContent* aStart, int32_t aType,
      bool aNoParentTraversal, bool aNavigateByKey, nsIContent** aNextContent);

  /**
   * Setter for focusedWindow with CallerType
   */
  MOZ_CAN_RUN_SCRIPT nsresult SetFocusedWindowWithCallerType(
      mozIDOMWindowProxy* aWindowToFocus, mozilla::dom::CallerType aCallerType);

  /** Given a focused frame loader owner, fix up the focus to be consistent */
  MOZ_CAN_RUN_SCRIPT void FixUpFocusAfterFrameLoaderChange(
      mozilla::dom::Element&);
  /**
   * Keep track of whether the focused window is about to go away, and if so fix
   * up the focus state so that we can know if we're still focused by the time
   * the frame loader swap ends.
   */
  void FixUpFocusBeforeFrameLoaderChange(mozilla::dom::Element&,
                                         mozilla::dom::BrowsingContext* aBc);

  /**
   * Raises the top-level window aWindow at the widget level.
   */
  MOZ_CAN_RUN_SCRIPT void RaiseWindow(nsPIDOMWindowOuter* aWindow,
                                      mozilla::dom::CallerType aCallerType,
                                      LazyFocusActionId& aActionId);

  /**
   * Called when a window has been raised.
   */
  MOZ_CAN_RUN_SCRIPT void WindowRaised(mozIDOMWindowProxy* aWindow,
                                       uint64_t aActionId);

  /**
   * Called when a window has been lowered.
   */
  MOZ_CAN_RUN_SCRIPT void WindowLowered(mozIDOMWindowProxy* aWindow,
                                        uint64_t aActionId);

  /**
   * Called when a new document in a window is shown.
   *
   * If aNeedsFocus is true, then focus events are expected to be fired on the
   * window if this window is in the focused window chain.
   */
  MOZ_CAN_RUN_SCRIPT void WindowShown(mozIDOMWindowProxy* aWindow,
                                      bool aNeedsFocus);

  /**
   * Called when a document in a window has been hidden or otherwise can no
   * longer accept focus.
   */
  MOZ_CAN_RUN_SCRIPT void WindowHidden(mozIDOMWindowProxy* aWindow,
                                       uint64_t aActionId,
                                       bool aIsEnteringBFCache);

  /**
   * Fire any events that have been delayed due to synchronized actions.
   */
  MOZ_CAN_RUN_SCRIPT void FireDelayedEvents(Document* aDocument);

  void WasNuked(nsPIDOMWindowOuter* aWindow);

  static uint32_t ProgrammaticFocusFlags(
      const mozilla::dom::FocusOptions& aOptions);

  /**
   * Returns an InputContextAction cause for aFlags.
   */
  static InputContextAction::Cause GetFocusMoveActionCause(uint32_t aFlags);

  /**
   * Notify of re-focus to same element.
   *
   * aElement is focused element.
   */
  MOZ_CAN_RUN_SCRIPT void NotifyOfReFocus(mozilla::dom::Element& aElement);

  static void MarkUncollectableForCCGeneration(uint32_t aGeneration);

  struct BlurredElementInfo {
    const mozilla::OwningNonNull<mozilla::dom::Element> mElement;

    explicit BlurredElementInfo(mozilla::dom::Element&);
    ~BlurredElementInfo();
  };

 protected:
  nsFocusManager();
  ~nsFocusManager();

  /**
   * Ensure that the widget associated with the currently focused window is
   * focused at the widget level.
   */
  void EnsureCurrentWidgetFocused(mozilla::dom::CallerType aCallerType);

  /**
   * Focus the last focused element in aWindow, after aWindow was raised (or if
   * aWindow was already raised).
   */
  MOZ_CAN_RUN_SCRIPT void MoveFocusToWindowAfterRaise(nsPIDOMWindowOuter*,
                                                      uint64_t aActionId);

  /**
   * Activate or deactivate the window and send the activate/deactivate events.
   */
  MOZ_CAN_RUN_SCRIPT void ActivateOrDeactivate(nsPIDOMWindowOuter* aWindow,
                                               bool aActive);

  /**
   * Blur whatever is currently focused and focus aNewContent. aFlags is a
   * bitmask of the flags defined in nsIFocusManager. If aFocusChanged is
   * true, then the focus has actually shifted and the caret position will be
   * updated to the new focus, aNewContent will be scrolled into view (unless
   * a flag disables this) and the focus method for the window will be updated.
   * If aAdjustWidget is false, don't change the widget focus state.
   *
   * All actual focus changes must use this method to do so. (as opposed
   * to those that update the focus in an inactive window for instance).
   *
   * Returns Nothing() if we end up not trying to focus the element,
   * otherwise returns the generated action id.
   */
  MOZ_CAN_RUN_SCRIPT mozilla::Maybe<uint64_t> SetFocusInner(
      mozilla::dom::Element* aNewContent, int32_t aFlags, bool aFocusChanged,
      bool aAdjustWidget);

  /**
   * Returns true if aPossibleAncestor is the same as aWindow or an
   * ancestor of aWindow.
   */
  bool IsSameOrAncestor(nsPIDOMWindowOuter* aPossibleAncestor,
                        nsPIDOMWindowOuter* aWindow) const;
  bool IsSameOrAncestor(nsPIDOMWindowOuter* aPossibleAncestor,
                        mozilla::dom::BrowsingContext* aContext) const;
  bool IsSameOrAncestor(mozilla::dom::BrowsingContext* aPossibleAncestor,
                        nsPIDOMWindowOuter* aWindow) const;

 public:
  bool IsSameOrAncestor(mozilla::dom::BrowsingContext* aPossibleAncestor,
                        mozilla::dom::BrowsingContext* aContext) const;

 protected:
  /**
   * Returns the window that is the lowest common ancestor of both aWindow
   * and aContext, or null if they share no common ancestor.
   */
  mozilla::dom::BrowsingContext* GetCommonAncestor(
      nsPIDOMWindowOuter* aWindow, mozilla::dom::BrowsingContext* aContext);

  /**
   * When aBrowsingContext is focused or blurred, adjust the ancestors of
   * aBrowsingContext so that they also have their corresponding frames focused
   * or blurred. Thus, one can start at the active top-level window and navigate
   * down the currently focused elements for each frame in the tree to get to
   * aBrowsingContext.
   */
  MOZ_CAN_RUN_SCRIPT bool AdjustInProcessWindowFocus(
      mozilla::dom::BrowsingContext* aBrowsingContext, bool aCheckPermission,
      bool aIsVisible, uint64_t aActionId, bool aShouldClearAncestorFocus,
      mozilla::dom::BrowsingContext* aAncestorBrowsingContextToFocus);

  MOZ_CAN_RUN_SCRIPT void AdjustWindowFocus(
      mozilla::dom::BrowsingContext* aBrowsingContext, bool aCheckPermission,
      bool aIsVisible, uint64_t aActionId, bool aShouldClearAncestorFocus,
      mozilla::dom::BrowsingContext* aAncestorBrowsingContextToFocus);

  /**
   * Returns true if aWindow is visible.
   */
  bool IsWindowVisible(nsPIDOMWindowOuter* aWindow);

  /**
   * Returns true if aContent is a root element and not focusable.
   * I.e., even if aContent is editable root element, this returns true when
   * the document is in designMode.
   *
   * @param aContent must not be null and must be in a document.
   */
  bool IsNonFocusableRoot(nsIContent* aContent);

  /**
   * First flushes the pending notifications to ensure the PresShell and frames
   * are updated.
   * Checks and returns aElement if it may be focused, another element node if
   * the focus should be retargeted at another node, or null if the node
   * cannot be focused. aFlags are the flags passed to SetFocus and similar
   * methods.
   *
   * An element is focusable if it is in a document, the document isn't in
   * print preview mode and the element has an nsIFrame where the
   * IsFocusable method returns true. For <area> elements, there is no
   * frame, so only the IsFocusable method on the content node must be
   * true.
   */
  MOZ_CAN_RUN_SCRIPT mozilla::dom::Element* FlushAndCheckIfFocusable(
      mozilla::dom::Element* aElement, uint32_t aFlags);

  /**
   * Blurs the currently focused element. Returns false if another element was
   * focused as a result. This would mean that the caller should not proceed
   * with a pending call to Focus. Normally, true would be returned.
   *
   * The currently focused element within aBrowsingContextToClear will be
   * cleared. aBrowsingContextToClear may be null, which means that no window is
   * cleared. This will be the case, for example, when lowering a window, as we
   * want to fire a blur, but not actually change what element would be focused,
   * so that the same element will be focused again when the window is raised.
   *
   * aAncestorBrowsingContextToFocus should be set to the common ancestor of the
   * window that is being blurred and the window that is going to focused, when
   * switching focus to a sibling window.
   *
   * aIsLeavingDocument should be set to true if the document/window is being
   * blurred as well. Document/window blur events will be fired. It should be
   * false if an element is the same document is about to be focused.
   *
   * If aAdjustWidget is false, don't change the widget focus state.
   */
  MOZ_CAN_RUN_SCRIPT bool Blur(
      mozilla::dom::BrowsingContext* aBrowsingContextToClear,
      mozilla::dom::BrowsingContext* aAncestorBrowsingContextToFocus,
      bool aIsLeavingDocument, bool aAdjustWidget, bool aRemainActive,
      uint64_t aActionId, mozilla::dom::Element* aElementToFocus = nullptr);
  MOZ_CAN_RUN_SCRIPT void BlurFromOtherProcess(
      mozilla::dom::BrowsingContext* aFocusedBrowsingContext,
      mozilla::dom::BrowsingContext* aBrowsingContextToClear,
      mozilla::dom::BrowsingContext* aAncestorBrowsingContextToFocus,
      bool aIsLeavingDocument, bool aAdjustWidget, uint64_t aActionId);
  MOZ_CAN_RUN_SCRIPT bool BlurImpl(
      mozilla::dom::BrowsingContext* aBrowsingContextToClear,
      mozilla::dom::BrowsingContext* aAncestorBrowsingContextToFocus,
      bool aIsLeavingDocument, bool aAdjustWidget, bool aRemainActive,
      mozilla::dom::Element* aElementToFocus, uint64_t aActionId);

  /**
   * Focus an element in the active window and child frame.
   *
   * aWindow is the window containing the element aContent to focus.
   *
   * aFlags is the flags passed to the various focus methods in
   * nsIFocusManager.
   *
   * aIsNewDocument should be true if a new document is being focused.
   * Document/window focus events will be fired.
   *
   * aFocusChanged should be true if a new content node is being focused, so
   * the focused content will be scrolled into view and the caret position
   * will be updated. If false is passed, then a window is simply being
   * refocused, for instance, due to a window being raised, or a tab is being
   * switched to.
   *
   * If aFocusChanged is true, then the focus has moved to a new location.
   * Otherwise, the focus is just being updated because the window was
   * raised.
   *
   * aWindowRaised should be true if the window is being raised. In this case,
   * command updaters will not be called.
   *
   * If aAdjustWidget is false, don't change the widget focus state.
   */
  MOZ_CAN_RUN_SCRIPT void Focus(
      nsPIDOMWindowOuter* aWindow, mozilla::dom::Element* aContent,
      uint32_t aFlags, bool aIsNewDocument, bool aFocusChanged,
      bool aWindowRaised, bool aAdjustWidget, uint64_t aActionId,
      const mozilla::Maybe<BlurredElementInfo>& = mozilla::Nothing());

  /**
   * Send a focus or blur event at aTarget. It may be added to the delayed
   * event queue if the document is suppressing events.
   *
   * aEventMessage should be either eFocus or eBlur.
   *
   * aWindowRaised should only be true if called from WindowRaised.
   */
  MOZ_CAN_RUN_SCRIPT void SendFocusOrBlurEvent(
      mozilla::EventMessage aEventMessage, mozilla::PresShell* aPresShell,
      Document* aDocument, mozilla::dom::EventTarget* aTarget,
      bool aWindowRaised, bool aIsRefocus = false,
      mozilla::dom::EventTarget* aRelatedTarget = nullptr);
  /**
   * Fire a focus or blur event at aTarget.
   *
   * aEventMessage should be either eFocus or eBlur.
   * For blur events, aFocusMethod should normally be non-zero.
   *
   * aWindowRaised should only be true if called from WindowRaised.
   */
  MOZ_CAN_RUN_SCRIPT void FireFocusOrBlurEvent(
      mozilla::EventMessage aEventMessage, mozilla::PresShell* aPresShell,
      mozilla::dom::EventTarget* aTarget, bool aWindowRaised,
      bool aIsRefocus = false,
      mozilla::dom::EventTarget* aRelatedTarget = nullptr);

  /**
   *  Fire a focusin or focusout event
   *
   *  aEventMessage should be either eFocusIn or eFocusOut.
   *
   *  aTarget is the content the event will fire on (the object that gained
   *  focus for focusin, the object blurred for focusout).
   *
   *  aCurrentFocusedWindow is the window focused before the focus/blur event
   *  was fired.
   *
   *  aCurrentFocusedContent is the content focused before the focus/blur event
   *  was fired.
   *
   *  aRelatedTarget is the content related to the event (the object
   *  losing focus for focusin, the object getting focus for focusout).
   */
  MOZ_CAN_RUN_SCRIPT void FireFocusInOrOutEvent(
      mozilla::EventMessage aEventMessage, mozilla::PresShell* aPresShell,
      mozilla::dom::EventTarget* aTarget,
      nsPIDOMWindowOuter* aCurrentFocusedWindow,
      nsIContent* aCurrentFocusedContent,
      mozilla::dom::EventTarget* aRelatedTarget = nullptr);

  /**
   * Scrolls aContent into view unless the FLAG_NOSCROLL flag is set.
   */
  MOZ_CAN_RUN_SCRIPT
  void ScrollIntoView(mozilla::PresShell* aPresShell, nsIContent* aContent,
                      uint32_t aFlags);

  /**
   * Updates the caret positon and visibility to match the focus.
   *
   * aMoveCaretToFocus should be true to move the caret to aContent.
   *
   * aUpdateVisibility should be true to update whether the caret is
   * visible or not.
   */
  MOZ_CAN_RUN_SCRIPT void UpdateCaret(bool aMoveCaretToFocus,
                                      bool aUpdateVisibility,
                                      nsIContent* aContent);

  /**
   * Helper method to move the caret to the focused element aContent.
   */
  MOZ_CAN_RUN_SCRIPT void MoveCaretToFocus(mozilla::PresShell* aPresShell,
                                           nsIContent* aContent);

  /**
   * Makes the caret visible or not, depending on aVisible.
   */
  nsresult SetCaretVisible(mozilla::PresShell* aPresShell, bool aVisible,
                           nsIContent* aContent);

  // the remaining functions are used for tab key and document-navigation

  /**
   * Retrieves the start and end points of the current selection for
   * aDocument and stores them in aStartContent and aEndContent.
   */
  void GetSelectionLocation(Document* aDocument, mozilla::PresShell* aPresShell,
                            nsIContent** aStartContent,
                            nsIContent** aEndContent);

  /*
   * Determine place to start sequential focus navigation, for
   * MOVEFOCUS_FORWARD/BACKWARD.
   * *aConsiderStartContent is set to true if we should move
   * focus directly to *aStartContent if it is focusable
   * (rather than the next/previous content).
   */
  void GetSequentialFocusNavigationStartingPoint(Document* aDocument,
                                                 nsIContent* aFocusedContent,
                                                 bool aForward,
                                                 nsIContent** aStartContent,
                                                 bool* aConsiderStartContent);

  /**
   * Retrieve the next tabbable element in scope owned by aOwner, using
   * focusability and tabindex to determine the tab order.
   *
   * aOwner is the owner of scope to search in.
   *
   * aStartContent is the starting point for this call of this method.
   *
   * aOriginalStartContent is the initial starting point for sequential
   * navigation.
   *
   * aForward should be true for forward navigation or false for backward
   * navigation.
   *
   * aCurrentTabIndex is the current tabindex.
   *
   * aIgnoreTabIndex to ignore the current tabindex and find the element
   * irrespective or the tab index.
   *
   * aForDocumentNavigation informs whether we're navigating only through
   * documents.
   *
   * aSkipOwner to skip owner while searching. The flag is set when caller is
   * |GetNextTabbableContent| in order to let caller handle owner.
   *
   * aReachedToEndForDocumentNavigation is true when this is a document
   * navigation and the focus algorithm has reached to the end of the top-level
   * document.
   *
   * NOTE:
   *   Consider the method searches downwards in flattened subtree
   *   rooted at aOwner.
   */
  MOZ_CAN_RUN_SCRIPT nsIContent* GetNextTabbableContentInScope(
      nsIContent* aOwner, nsIContent* aStartContent,
      nsIContent* aOriginalStartContent, bool aForward,
      int32_t aCurrentTabIndex, bool aIgnoreTabIndex,
      bool aForDocumentNavigation, bool aNavigateByKey, bool aSkipOwner,
      bool aReachedToEndForDocumentNavigation);

  /**
   * Retrieve the next tabbable element in scope including aStartContent
   * and the scope's ancestor scopes, using focusability and tabindex to
   * determine the tab order.
   *
   * aStartOwner is the scope owner of the aStartContent.
   *
   * aStartContent an in/out paremeter. It as input is the starting point
   * for this call of this method; as output it is the shadow host in
   * light DOM if the next tabbable element is not found in shadow DOM,
   * in order to continue searching in light DOM.
   *
   * aOriginalStartContent is the initial starting point for sequential
   * navigation.
   *
   * aForward should be true for forward navigation or false for backward
   * navigation.
   *
   * aCurrentTabIndex returns tab index of shadow host in light DOM if the
   * next tabbable element is not found in shadow DOM, in order to continue
   * searching in light DOM.
   *
   * aIgnoreTabIndex to ignore the current tabindex and find the element
   * irrespective or the tab index.
   *
   * aForDocumentNavigation informs whether we're navigating only through
   * documents.
   *
   * aNavigateByKey to move focus by keyboard as a side effect of computing the
   * next target.
   *
   * aReachedToEndForDocumentNavigation is true when this is a document
   * navigation and the focus algorithm has reached to the end of the top-level
   * document.
   *
   * NOTE:
   *   Consider the method searches upwards in all shadow host- or slot-rooted
   *   flattened subtrees that contains aStartContent as non-root, except
   *   the flattened subtree rooted at shadow host in light DOM.
   */
  MOZ_CAN_RUN_SCRIPT nsIContent* GetNextTabbableContentInAncestorScopes(
      nsIContent* aStartOwner, nsCOMPtr<nsIContent>& aStartContent /* inout */,
      nsIContent* aOriginalStartContent, bool aForward,
      int32_t* aCurrentTabIndex, bool* aIgnoreTabIndex,
      bool aForDocumentNavigation, bool aNavigateByKey,
      bool aReachedToEndForDocumentNavigation);

  /**
   * Retrieve the next tabbable element within a document, using focusability
   * and tabindex to determine the tab order. The element is returned in
   * aResultContent.
   *
   * aRootContent is the root node -- nodes above this will not be examined.
   * Typically this will be the root node of a document, but could also be
   * a popup node.
   *
   * aOriginalStartContent is the content which was originally the starting
   * node, in the case of recursive or looping calls.
   *
   * aStartContent is the starting point for this call of this method.
   * If aStartContent doesn't have visual representation, the next content
   * object, which does have a primary frame, will be used as a start.
   * If that content object is focusable, the method may return it.
   *
   * aForward should be true for forward navigation or false for backward
   * navigation.
   *
   * aCurrentTabIndex is the current tabindex.
   *
   * aIgnoreTabIndex to ignore the current tabindex and find the element
   * irrespective or the tab index. This will be true when a selection is
   * active, since we just want to focus the next element in tree order
   * from where the selection is. Similarly, if the starting element isn't
   * focusable, since it doesn't really have a defined tab index.
   *
   * aNavigateByKey to move focus by keyboard as a side effect of computing the
   * next target.
   *
   * aReachedToEndForDocumentNavigation is true when this is a document
   * navigation and the focus algorithm has reached to the end of the top-level
   * document.
   */
  MOZ_CAN_RUN_SCRIPT nsresult GetNextTabbableContent(
      mozilla::PresShell* aPresShell, nsIContent* aRootContent,
      nsIContent* aOriginalStartContent, nsIContent* aStartContent,
      bool aForward, int32_t aCurrentTabIndex, bool aIgnoreTabIndex,
      bool aForDocumentNavigation, bool aNavigateByKey,
      bool aReachedToEndForDocumentNavigation, nsIContent** aResultContent);

  /**
   * Get the next tabbable image map area and returns it.
   *
   * aForward should be true for forward navigation or false for backward
   * navigation.
   *
   * aCurrentTabIndex is the current tabindex.
   *
   * aImageContent is the image.
   *
   * aStartContent is the current image map area.
   */
  nsIContent* GetNextTabbableMapArea(bool aForward, int32_t aCurrentTabIndex,
                                     mozilla::dom::Element* aImageContent,
                                     nsIContent* aStartContent);

  /**
   * Return the next valid tabindex value after aCurrentTabIndex, if aForward
   * is true, or the previous tabindex value if aForward is false. aParent is
   * the node from which to start looking for tab indicies.
   */
  int32_t GetNextTabIndex(nsIContent* aParent, int32_t aCurrentTabIndex,
                          bool aForward);

  /**
   * Focus the first focusable content within the document with a root node of
   * aRootContent. For content documents, this will be aRootContent itself, but
   * for chrome documents, this will locate the next focusable content.
   *
   * aReachedToEndForDocumentNavigation is true when the focus algorithm has
   * reached to the end of the top-level document.
   */
  MOZ_CAN_RUN_SCRIPT nsresult
  FocusFirst(mozilla::dom::Element* aRootContent, nsIContent** aNextContent,
             bool aReachedToEndForDocumentNavigation);

  /**
   * Retrieves and returns the root node from aDocument to be focused. Will
   * return null if the root node cannot be focused. There are several reasons
   * for this:
   *
   * - if aForDocumentNavigation is false and aWindow is a chrome shell.
   * - if aCheckVisibility is true and the aWindow is not visible.
   * - if aDocument is a frameset document.
   */
  mozilla::dom::Element* GetRootForFocus(nsPIDOMWindowOuter* aWindow,
                                         Document* aDocument,
                                         bool aForDocumentNavigation,
                                         bool aCheckVisibility);

  /**
   * Retrieves and returns the root node as with GetRootForFocus but only if
   * aContent is a frame with a valid child document.
   */
  mozilla::dom::Element* GetRootForChildDocument(nsIContent* aContent);

  /**
   * Retreives a focusable element within the current selection of aWindow.
   * Currently, this only detects links.
   *
   * This is used when MoveFocus is called with a type of MOVEFOCUS_CARET,
   * which is used, for example, to focus links as the caret is moved over
   * them.
   */
  void GetFocusInSelection(nsPIDOMWindowOuter* aWindow,
                           nsIContent* aStartSelection,
                           nsIContent* aEndSelection,
                           nsIContent** aFocusedContent);

 private:
  /**
   * Given an element, which must be the focused element, activate the remote
   * frame it embeds, if any.
   */
  void ActivateRemoteFrameIfNeeded(mozilla::dom::Element&, uint64_t aActionId);

  // Notify that the focus state of aElement has changed.  Note that we need to
  // pass in whether the window should show a focus ring before the
  // SetFocusedNode call on it happened when losing focus and after the
  // SetFocusedNode call when gaining focus, which is why that information needs
  // to be an explicit argument instead of just passing in the window and asking
  // it whether it should show focus rings: in the losing focus case that
  // information could be wrong.
  //
  // aShouldShowFocusRing is only relevant if aGettingFocus is true.
  static void NotifyFocusStateChange(mozilla::dom::Element* aElement,
                                     mozilla::dom::Element* aElementToFocus,
                                     int32_t aFlags, bool aGettingFocus,
                                     bool aShouldShowFocusRing);

  void SetFocusedWindowInternal(nsPIDOMWindowOuter* aWindow, uint64_t aActionId,
                                bool aSyncBrowsingContext = true);

  MOZ_CAN_RUN_SCRIPT bool TryDocumentNavigation(nsIContent* aCurrentContent,
                                                bool* aCheckSubDocument,
                                                nsIContent** aResultContent);

  MOZ_CAN_RUN_SCRIPT bool TryToMoveFocusToSubDocument(
      nsIContent* aCurrentContent, nsIContent* aOriginalStartContent,
      bool aForward, bool aForDocumentNavigation, bool aNavigateByKey,
      bool aReachedToEndForDocumentNavigation, nsIContent** aResultContent);

  // Sets the focused BrowsingContext and, if appropriate, syncs it to
  // other processes.
  void SetFocusedBrowsingContext(mozilla::dom::BrowsingContext* aContext,
                                 uint64_t aActionId);

  // Content-only
  // Called when receiving an IPC message about another process setting
  // the focused BrowsingContext.
  void SetFocusedBrowsingContextFromOtherProcess(
      mozilla::dom::BrowsingContext* aContext, uint64_t aActionId);

  // Chrome-only
  // When returning true, sets the chrome process notion of what
  // BrowsingContext is focused in content. When returning false,
  // ignores the attempt to set as out-of-sequence.
  bool SetFocusedBrowsingContextInChrome(
      mozilla::dom::BrowsingContext* aContext, uint64_t aActionId);

  void InsertNewFocusActionId(uint64_t aActionId);

  bool ProcessPendingActiveBrowsingContextActionId(uint64_t aActionId,
                                                   bool aSettingToNonNull);

  bool ProcessPendingFocusedBrowsingContextActionId(uint64_t aActionId);

 public:
  // Chrome-only
  // Gets the chrome process notion of what BrowsingContext is focused
  // in content.
  mozilla::dom::BrowsingContext* GetFocusedBrowsingContextInChrome();

  // Chrome-only
  // Notifies the focus manager that BrowsingContext::Detach was called
  // on a BrowsingContext so that pointers to it can be forgotten.
  void BrowsingContextDetached(mozilla::dom::BrowsingContext* aContext);

 private:
  // Content-only
  // Sets the BrowsingContext corresponding to top-level Web content
  // in the frontmost tab if focus is in Web content.
  void SetActiveBrowsingContextInContent(
      mozilla::dom::BrowsingContext* aContext, uint64_t aActionId,
      bool aIsEnteringBFCache);

  // Content-only
  // Receives notification of another process setting the top-level Web
  // content as being in the frontmost tab with focus in Web content.
  void SetActiveBrowsingContextFromOtherProcess(
      mozilla::dom::BrowsingContext* aContext, uint64_t aActionId);

  // Content-only
  // Receives notification that another process determined that focus
  // moved to chrome so a particular BrowsingContext is no longer the
  // "active" one.
  void UnsetActiveBrowsingContextFromOtherProcess(
      mozilla::dom::BrowsingContext* aContext, uint64_t aActionId);

  // Content-only
  // Receives a notification from parent that this content process's
  // attempt to set the active browsing context was late and the
  // prevailing browsing context is instead the second argument of
  // this method call. This should be ignored if the first argument
  // doesn't match the latest action id associated with setting the
  // active browsing context in this process, because in that case,
  // this revision is late.
  void ReviseActiveBrowsingContext(uint64_t aOldActionId,
                                   mozilla::dom::BrowsingContext* aContext,
                                   uint64_t aNewActionId);

  // Receives a notification from parent that this content process's
  // attempt to set the focused browsing context was late and the
  // prevailing browsing context is instead the second argument of
  // this method call. This should be ignored if the first argument
  // doesn't match the latest action id associated with setting the
  // active browsing context in this process, because in that case,
  // this revision is late.
  void ReviseFocusedBrowsingContext(uint64_t aOldActionId,
                                    mozilla::dom::BrowsingContext* aContext,
                                    uint64_t aNewActionId);

  // Chrome-only
  // Sets the chrome process notion of what content believes to be
  // the top-level BrowsingContext in the frontmost tab when focus
  // is in Web content.
  // Returns true if set and false if ignored.
  bool SetActiveBrowsingContextInChrome(mozilla::dom::BrowsingContext* aContext,
                                        uint64_t aActionId);

  void FocusedElementMayHaveMoved(nsIContent* aContent, nsINode* aOldParent);

 public:
  // Chrome-only
  // Gets the chrome process notion of what content believes to be
  // the top-level BrowsingContext in the frontmost tab when focus
  // is in Web content.
  mozilla::dom::BrowsingContext* GetActiveBrowsingContextInChrome();

  uint64_t GetActionIdForActiveBrowsingContextInChrome() const;

  uint64_t GetActionIdForFocusedBrowsingContextInChrome() const;

  static uint64_t GenerateFocusActionId();

  // This function works very similar to
  // https://html.spec.whatwg.org/#get-the-focusable-area
  static mozilla::dom::Element* GetTheFocusableArea(
      mozilla::dom::Element* aTarget, uint32_t aFlags);

  // Returns true if it's an area element with one or more shapes that are
  // focusable areas.
  static bool IsAreaElementFocusable(mozilla::dom::HTMLAreaElement& aArea);

 private:
  // In the chrome process, the currently active and front-most top-most
  // window. Not supposed to be used in a meaningful way in content
  // processes. For legacy reasons, this exists as a separate field
  // instead of being derived from mFocusedWindow when needed, because
  // the defined relation that mActiveWindow is supposed to be the same
  // as or ancestor of mFocusedWindow is temporarily broken when a
  // window is being raised or lowered.
  nsCOMPtr<nsPIDOMWindowOuter> mActiveWindow;

  // In a content process, the BrowsingContext corresponding to top-level
  // Web content in the active tab or nullptr if focus is not in a
  // BrowsingContextGroup that this process participates in. Synced
  // across processes in a BrowsingContextGroup. This field exists
  // separately from mFocusedBrowsingContextInContent instead of being being
  // derived from it, because for legacy reasons the relation
  // mFocusedBrowsingContextInContent->Top() == mActiveBrowsingContextInContent
  // is temporarily broken when a window is being raised or lowered.
  // Not supposed to be used in a meaningful way in the chrome process.
  RefPtr<mozilla::dom::BrowsingContext> mActiveBrowsingContextInContent;

  // If this content process set mActiveBrowsingContextInContent, this
  // field holds the corresponding actionId so that
  // mActiveBrowsingContextInContent can be revised of the parent rejects
  // the update. This field is used for accepting revisions only if nothing
  // else has updated mActiveBrowsingContextInContent before the revision
  // arrives.
  uint64_t mActionIdForActiveBrowsingContextInContent;

  uint64_t mActionIdForActiveBrowsingContextInChrome;

  // If this content process set mFocusedBrowsingContextInContent, this
  // field holds the corresponding actionId so that
  // mFocusedBrowsingContextInContent can be revised of the parent rejects
  // the update. This field is used for accepting revisions only if nothing
  // else has updated mFocusedBrowsingContextInContent before the revision
  // arrives.
  uint64_t mActionIdForFocusedBrowsingContextInContent;

  uint64_t mActionIdForFocusedBrowsingContextInChrome;

  // Whether or not mActiveBrowsingContextInContent was set from another process
  // or from this process.
  bool mActiveBrowsingContextInContentSetFromOtherProcess;

  // This is the chrome process notion of content's
  // mActiveBrowsingContextInContent. Avoiding field reuse for different
  // semantics in different process types to make it easier to catch bugs.
  RefPtr<mozilla::dom::BrowsingContext> mActiveBrowsingContextInChrome;

  // the child or top-level window that is currently focused. In the chrome
  // process, when a window isn't being raised or lowered, this window will
  // either be the same window as mActiveWindow or a descendant of it.
  // Except during shutdown use SetFocusedWindowInternal to set mFocusedWindow!
  nsCOMPtr<nsPIDOMWindowOuter> mFocusedWindow;

  // The focused BrowsingContext if this is a chrome process and focus is
  // in chrome or if this is a content process and focus is in Web content
  // in this BrowsingContextGroup. nullptr otherwise.
  // Except during shutdown, must be set via SetFocusedWindowInternal which
  // calls SetFocusedBrowsingContext or if the value is coming in via IPC
  // via SetFocusedBrowsingContextFromOtherProcess.
  RefPtr<mozilla::dom::BrowsingContext> mFocusedBrowsingContextInContent;

  // This is the chrome process notion of content's
  // mFocusedBrowsingContextInContent. Avoiding field reuse for different
  // semantics in different process types to make it easier to catch bugs.
  RefPtr<mozilla::dom::BrowsingContext> mFocusedBrowsingContextInChrome;

  // the currently focused content if in-process or the XUL browser in which
  // Web content focus resides. Always inside mFocusedWindow. When a window
  // isn't being raised or lowered, this is a cached copy of the
  // mFocusedWindow's current content. This may be null if no content is
  // focused.
  RefPtr<mozilla::dom::Element> mFocusedElement;

  // keep track of a window while it is being lowered
  nsCOMPtr<nsPIDOMWindowOuter> mWindowBeingLowered;

  // synchronized actions cannot be interrupted with events, so queue these up
  // and fire them later.
  nsTArray<nsDelayedBlurOrFocusEvent> mDelayedBlurFocusEvents;

  // Array of focus action ids for which we haven't seen an active browsing
  // context set yet. As set is allowed to overwrite an unset. Therefore,
  // an unset removes earlier ids but not the matching id. A set removes
  // earlier ids and the matching id.
  //
  // Conceptually, active browsing context shouldn't have to exist as a
  // field, because it should be possible to always derive it from the
  // focused browsing context. Unfortunately, for legacy reasons, this
  // is not the case while a window is being raised or lowered.
  //
  // Conceptually, it should be possible for the parent to manage the
  // active browsing context. Unfortunately, for legacy reasons, the
  // code for setting the active browsing context needs to reside in
  // the content process to retain the existing and test-passing code
  // flow.
  //
  // This, obviously, raises the issue of content processes racing to
  // set the active browsing context. In particular, there is a pattern
  // that the parent initiates actions that cause multiple content
  // processes to mutate the active browsing context at almost the
  // same time. When two native browser windows change order, the
  // lowering isn't distinguished from the case of lowering the
  // entire app. For this reason, the owner of the previous active
  // browsing context tries to unset it and at almost the same time
  // the another content process sets a new active browsing context.
  // If the IPC messages for these unset and set actions were to
  // arrive in the wrong order, this could get in the wrong state.
  //
  // To address this issue, the parent manages an authortative order
  // of attempts to (un)set the active browsing context using the
  // array mPendingActiveBrowsingContextActions.
  //
  // A process reserves a slot in the order by calling
  // GenerateFocusActionId(). Per one call to GenerateFocusActionId(),
  // there may be at most one action to set the active browsing context
  // to a new value. There may be logically prior attempts to unset it
  // (i.e. set it to nullptr). That is, if there are both attempts to
  // unset and set the active browsing context with the same action id,
  // the attempt to set to a non-null value wins.
  //
  // The completion of an action from reserting the slot in the order
  // and actually performing the setting of the active browsing context
  // may span multiple processes and IPC messages.
  //
  // The at-most-once property is not asserted, because the process
  // claiming the position in the order and the process setting the
  // active browsing context with that actionId may be different, and
  // the act of using an actionId to set the active browsing context
  // is used to delete stale items from the array to avoid excessive
  // growth of the array.
  nsTArray<uint64_t> mPendingActiveBrowsingContextActions;

  // Like mPendingActiveBrowsingContextActions but for the focused
  // browsing context.
  nsTArray<uint64_t> mPendingFocusedBrowsingContextActions;

  // If set to true, layout of the document of the event target should be
  // flushed before handling focus depending events.
  bool mEventHandlingNeedsFlush;

  static bool sTestMode;

  // Process-specific counter for maintaining the prosess-specific
  // uniqueness of actionIds.
  static uint64_t sFocusActionCounter;

  // the single focus manager
  static mozilla::StaticRefPtr<nsFocusManager> sInstance;
};

/**
 * The id of a focus action whose slot in the parent's order is reserved only
 * when the action first needs it. A reserved slot stays queued in the parent
 * until a newer action supersedes it, so a path that ends up doing nothing
 * should not reserve one. It can't be copied, so that everything handling one
 * action shares one slot. This deliberately doesn't expose whether the id was
 * supplied by the caller, so that it can't stand for where the action came
 * from.
 */
class MOZ_STACK_CLASS LazyFocusActionId final {
 public:
  LazyFocusActionId() = default;
  explicit LazyFocusActionId(uint64_t aActionId)
      : mActionId(mozilla::Some(aActionId)) {}
  LazyFocusActionId(const LazyFocusActionId&) = delete;
  LazyFocusActionId& operator=(const LazyFocusActionId&) = delete;

  // Reserves the slot on the first call.
  uint64_t Get() {
    if (mActionId.isNothing()) {
      mActionId.emplace(nsFocusManager::GenerateFocusActionId());
    }
    return *mActionId;
  }

 private:
  mozilla::Maybe<uint64_t> mActionId;
};

nsresult NS_NewFocusManager(nsIFocusManager** aResult);

#endif
