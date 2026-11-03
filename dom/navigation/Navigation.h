/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_Navigation_h_
#define mozilla_dom_Navigation_h_

#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/DOMEventTargetHelper.h"
#include "mozilla/Maybe.h"
#include "mozilla/RefPtr.h"
#include "mozilla/dom/NavigateEvent.h"
#include "mozilla/dom/NavigationBinding.h"
#include "nsHashtablesFwd.h"
#include "nsStringFwd.h"

class nsIDHashKey;

namespace mozilla::dom {

class FormData;
class NavigationActivation;
class NavigationDestination;
class NavigationHistoryEntry;
struct NavigationNavigateOptions;
struct NavigationOptions;
class NavigationTransition;
struct NavigationUpdateCurrentEntryOptions;
struct NavigationReloadOptions;
struct NavigationResult;
class PreviousSessionHistoryInfo;
class ViewTransition;

class SessionHistoryInfo;

// https://html.spec.whatwg.org/#navigation-api-method-tracker
struct NavigationAPIMethodTracker final : public nsISupports {
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS_FINAL
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS(NavigationAPIMethodTracker)

  NavigationAPIMethodTracker(Navigation* aNavigationObject,
                             const Maybe<nsID> aKey, const JS::Value& aInfo,
                             nsIStructuredCloneContainer* aSerializedState,
                             NavigationHistoryEntry* aCommittedToEntry,
                             Promise* aCommittedPromise,
                             Promise* aFinishedPromise, bool aPending = false);

  // Mark this tracker as no longer pending (promoted to ongoing).
  void MarkAsNotPending() { mPending = false; }

  void CleanUp();
  void NotifyAboutCommittedToEntry(NavigationHistoryEntry* aNHE);
  void ResolveFinishedPromise();
  void RejectFinishedPromise(JS::Handle<JS::Value> aException);
  void CreateResult(JSContext* aCx, NavigationResult& aResult);
  void SetSerializedState(nsIStructuredCloneContainer* aSerializedState) {
    mSerializedState = aSerializedState;
  }

  Promise* CommittedPromise() { return mCommittedPromise; }
  Promise* FinishedPromise() { return mFinishedPromise; }

  bool IsHandled() const;

  RefPtr<Navigation> mNavigationObject;
  Maybe<nsID> mKey;
  JS::Heap<JS::Value> mInfo;

 private:
  ~NavigationAPIMethodTracker();

  bool mPending;
  RefPtr<nsIStructuredCloneContainer> mSerializedState;
  RefPtr<NavigationHistoryEntry> mCommittedToEntry;
  RefPtr<Promise> mCommittedPromise;
  RefPtr<Promise> mFinishedPromise;
};

class Navigation final : public DOMEventTargetHelper {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(Navigation, DOMEventTargetHelper)

  explicit Navigation(nsPIDOMWindowInner* aWindow);

  bool IsNavigation() const override { return true; }

  NS_IMPL_FROMEVENTTARGET_HELPER(Navigation, IsNavigation())

  using EventTarget::EventListenerAdded;
  virtual void EventListenerAdded(nsAtom* aType) override;
  using EventTarget::EventListenerRemoved;
  virtual void EventListenerRemoved(nsAtom* aType) override;

  // Navigation.webidl
  void Entries(nsTArray<RefPtr<NavigationHistoryEntry>>& aResult) const;
  already_AddRefed<NavigationHistoryEntry> GetCurrentEntry() const;
  MOZ_CAN_RUN_SCRIPT
  void UpdateCurrentEntry(JSContext* aCx,
                          const NavigationUpdateCurrentEntryOptions& aOptions,
                          ErrorResult& aRv);
  NavigationTransition* GetTransition() const;
  NavigationActivation* GetActivation() const;

  bool CanGoBack() {
    return !HasEntriesAndEventsDisabled() && mCurrentEntryIndex &&
           *mCurrentEntryIndex != 0;
  }
  bool CanGoForward() {
    return !HasEntriesAndEventsDisabled() && mCurrentEntryIndex &&
           *mCurrentEntryIndex != mEntries.Length() - 1;
  }

  MOZ_CAN_RUN_SCRIPT void Navigate(JSContext* aCx, const nsAString& aUrl,
                                   const NavigationNavigateOptions& aOptions,
                                   NavigationResult& aResult);

  MOZ_CAN_RUN_SCRIPT void Reload(JSContext* aCx,
                                 const NavigationReloadOptions& aOptions,
                                 NavigationResult& aResult);

  void TraverseTo(JSContext* aCx, const nsAString& aKey,
                  const NavigationOptions& aOptions, NavigationResult& aResult);
  void Back(JSContext* aCx, const NavigationOptions& aOptions,
            NavigationResult& aResult);
  void Forward(JSContext* aCx, const NavigationOptions& aOptions,
               NavigationResult& aResult);

  IMPL_EVENT_HANDLER(navigate);
  IMPL_EVENT_HANDLER(navigatesuccess);
  IMPL_EVENT_HANDLER(navigateerror);
  IMPL_EVENT_HANDLER(currententrychange);

  // https://html.spec.whatwg.org/multipage/nav-history-apis.html#initialize-the-navigation-api-entries-for-a-new-document
  void InitializeHistoryEntries(
      mozilla::Span<const SessionHistoryInfo> aNewSHInfos,
      const SessionHistoryInfo* aInitialSHInfo);

  // https://html.spec.whatwg.org/multipage/nav-history-apis.html#update-the-navigation-api-entries-for-reactivation
  MOZ_CAN_RUN_SCRIPT
  void UpdateForReactivation(Span<const SessionHistoryInfo> aNewSHEs,
                             const SessionHistoryInfo* aReactivatedEntry);

  MOZ_CAN_RUN_SCRIPT
  void UpdateEntriesForSameDocumentNavigation(
      SessionHistoryInfo* aDestinationSHE, NavigationType aNavigationType,
      bool aFiredNavigateEvent = true);

  MOZ_CAN_RUN_SCRIPT void TruncateForwardEntries(uint32_t aNewLength);

  MOZ_CAN_RUN_SCRIPT
  void RunNavigateEventHandlerSteps(
      NavigateEvent* aNavigateEvent,
      NavigationAPIMethodTracker* aAPIMethodTracker);

  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override;

  // The Navigation API is only enabled if the dom.navigation.webidl.enabled
  // pref is set.
  static bool IsAPIEnabled(JSContext* /* unused */ = nullptr,
                           JSObject* /* unused */ = nullptr);

  enum class FinalStatus : uint8_t { Continue, Intercept, Prevent };

  // Wrapper algorithms for firing the navigate event.
  // https://html.spec.whatwg.org/#navigate-event-firing
  MOZ_CAN_RUN_SCRIPT FinalStatus
  FireTraverseNavigateEvent(JSContext* aCx, nsDocShellLoadState* aLoadState,
                            Maybe<UserNavigationInvolvement> aUserInvolvement);

  MOZ_CAN_RUN_SCRIPT bool FirePushReplaceReloadNavigateEvent(
      JSContext* aCx, NavigationType aNavigationType, nsIURI* aDestinationURL,
      bool aIsSameDocument, Maybe<UserNavigationInvolvement> aUserInvolvement,
      Element* aSourceElement, FormData* aFormDataEntryList,
      nsIStructuredCloneContainer* aNavigationAPIState,
      nsIStructuredCloneContainer* aClassicHistoryAPIState,
      NavigationAPIMethodTracker* aApiMethodTrackerForNavigateOrReload =
          nullptr);

  MOZ_CAN_RUN_SCRIPT bool FireDownloadRequestNavigateEvent(
      JSContext* aCx, nsIURI* aDestinationURL,
      UserNavigationInvolvement aUserInvolvement, Element* aSourceElement,
      const nsAString& aFilename);

  bool FocusedChangedDuringOngoingNavigation() const;
  void SetFocusedChangedDuringOngoingNavigation(
      bool aFocusChangedDuringOngoingNavigation);

  bool HasOngoingNavigateEvent() const;

  MOZ_CAN_RUN_SCRIPT
  void InnerInformAboutAbortingNavigation(JSContext* aCx);

  // Drops the ongoing navigate event and API method tracker without aborting
  // them, for when a history step moves away from this document. An
  // intercepted navigate event is kept, since its handlers still finish it.
  void ClearOngoingNavigationForHistoryStep();

  MOZ_CAN_RUN_SCRIPT
  void AbortOngoingNavigation(
      JSContext* aCx, JS::Handle<JS::Value> aError = JS::UndefinedHandleValue);

  MOZ_CAN_RUN_SCRIPT
  void AbortNavigateEvent(JSContext* aCx, const NavigateEvent* aEvent,
                          JS::Handle<JS::Value> aReason);

  MOZ_CAN_RUN_SCRIPT
  void InformAboutChildNavigableDestruction(JSContext* aCx);

  void CreateNavigationActivationFrom(
      const Maybe<PreviousSessionHistoryInfo>& aPreviousEntryForActivation,
      Maybe<NavigationType> aNavigationType);

  // https://html.spec.whatwg.org/#fire-the-pageswap-event
  // aCanHaveActivation is false when the activation must be null, for example
  // for a cross-origin navigation.
  MOZ_CAN_RUN_SCRIPT void FirePageSwapEvent(
      const SessionHistoryInfo* aTargetSHInfo, NavigationType aType,
      bool aCanHaveActivation, ViewTransition* aViewTransition);

  void SetSerializedStateIntoOngoingAPIMethodTracker(
      nsIStructuredCloneContainer* aSerializedState);

 private:
  friend struct NavigationAPIMethodTracker;
  friend struct NavigationWaitForAllScope;
  using UpcomingTraverseAPIMethodTrackers =
      nsTHashMap<nsIDHashKey, RefPtr<NavigationAPIMethodTracker>>;

  ~Navigation() = default;

  // https://html.spec.whatwg.org/multipage/nav-history-apis.html#has-entries-and-events-disabled
  bool HasEntriesAndEventsDisabled() const;

  MOZ_CAN_RUN_SCRIPT
  nsresult FireEvent(const nsAString& aName);

  MOZ_CAN_RUN_SCRIPT
  nsresult FireErrorEvent(const nsAString& aName,
                          const ErrorEventInit& aEventInitDict);

  // https://html.spec.whatwg.org/#inner-navigate-event-firing-algorithm
  MOZ_CAN_RUN_SCRIPT FinalStatus InnerFireNavigateEvent(
      JSContext* aCx, NavigationType aNavigationType,
      NavigationDestination* aDestination,
      UserNavigationInvolvement aUserInvolvement, Element* aSourceElement,
      FormData* aFormDataEntryList,
      nsIStructuredCloneContainer* aClassicHistoryAPIState,
      const nsAString& aDownloadRequestFilename,
      NavigationAPIMethodTracker* aNavigationAPIMethodTracker = nullptr,
      nsDocShellLoadState* aLoadState = nullptr);

  NavigationHistoryEntry* FindNavigationHistoryEntry(
      const SessionHistoryInfo& aSessionHistoryInfo) const;

  Maybe<size_t> GetNavigationEntryIndex(
      const SessionHistoryInfo& aSessionHistoryInfo) const;

  RefPtr<NavigationAPIMethodTracker> SetUpNavigateReloadAPIMethodTracker(
      JS::Handle<JS::Value> aInfo,
      nsIStructuredCloneContainer* aSerializedState);

  RefPtr<NavigationAPIMethodTracker> AddUpcomingTraverseAPIMethodTracker(
      const nsID& aKey, JS::Handle<JS::Value> aInfo);

  void SetEarlyErrorResult(JSContext* aCx, NavigationResult& aResult,
                           ErrorResult&& aRv) const;

  void SetEarlyStateErrorResult(JSContext* aCx, NavigationResult& aResult,
                                const nsACString& aMessage) const;

  bool CheckIfDocumentIsFullyActiveAndMaybeSetEarlyErrorResult(
      JSContext* aCx, const Document* aDocument,
      NavigationResult& aResult) const;

  bool CheckDocumentUnloadCounterAndMaybeSetEarlyErrorResult(
      JSContext* aCx, const Document* aDocument,
      NavigationResult& aResult) const;

  already_AddRefed<nsIStructuredCloneContainer>
  CreateSerializedStateAndMaybeSetEarlyErrorResult(
      JSContext* aCx, const JS::Value& aState, NavigationResult& aResult) const;

  static void CleanUp(NavigationAPIMethodTracker* aNavigationAPIMethodTracker);

  void SetCurrentEntryIndex(const SessionHistoryInfo* aTargetInfo);

  Document* GetAssociatedDocument() const;

  // Update the state managing if we need to dispatch the traverse event or not.
  void UpdateNeedsTraverse();

  void LogHistory() const;

  void PerformNavigationTraversal(JSContext* aCx, const nsID& aKey,
                                  const NavigationOptions& aOptions,
                                  NavigationResult& aResult);

  // https://html.spec.whatwg.org/multipage/nav-history-apis.html#navigation-entry-list
  nsTArray<RefPtr<NavigationHistoryEntry>> mEntries;

  // https://html.spec.whatwg.org/multipage/nav-history-apis.html#navigation-current-entry
  Maybe<uint64_t> mCurrentEntryIndex;

  // https://html.spec.whatwg.org/#ongoing-navigation-tracking:navigateevent-2
  RefPtr<NavigateEvent> mOngoingNavigateEvent;

  // https://html.spec.whatwg.org/multipage/nav-history-apis.html#focus-changed-during-ongoing-navigation
  bool mFocusChangedDuringOngoingNavigation = false;

  // https://html.spec.whatwg.org/multipage/nav-history-apis.html#suppress-normal-scroll-restoration-during-ongoing-navigation
  bool mSuppressNormalScrollRestorationDuringOngoingNavigation = false;

  // https://html.spec.whatwg.org/multipage/nav-history-apis.html#ongoing-api-method-tracker
  RefPtr<NavigationAPIMethodTracker> mOngoingAPIMethodTracker;

  // https://html.spec.whatwg.org/multipage/nav-history-apis.html#upcoming-traverse-api-method-trackers
  UpcomingTraverseAPIMethodTrackers mUpcomingTraverseAPIMethodTrackers;

  // https://html.spec.whatwg.org/#concept-navigation-transition
  RefPtr<NavigationTransition> mTransition;

  // https://html.spec.whatwg.org/#navigation-activation
  RefPtr<NavigationActivation> mActivation;
};

inline Navigation* EventTarget::GetAsNavigation() {
  return IsNavigation() ? AsNavigation() : nullptr;
}
inline const Navigation* EventTarget::GetAsNavigation() const {
  return IsNavigation() ? AsNavigation() : nullptr;
}
inline Navigation* EventTarget::AsNavigation() {
  MOZ_DIAGNOSTIC_ASSERT(IsNavigation());
  return static_cast<Navigation*>(this);
}
inline const Navigation* EventTarget::AsNavigation() const {
  MOZ_DIAGNOSTIC_ASSERT(IsNavigation());
  return static_cast<const Navigation*>(this);
}

}  // namespace mozilla::dom

template <>
struct fmt::formatter<mozilla::dom::NavigationType, char>
    : public formatter<nsLiteralCString> {
  template <typename FmtContext>
  constexpr auto format(const mozilla::dom::NavigationType& aNavigationType,
                        FmtContext& aCtx) const {
    return formatter<nsLiteralCString>::format(
        mozilla::dom::GetEnumString(aNavigationType), aCtx);
  }
};

template <>
struct fmt::formatter<mozilla::dom::NavigationHistoryBehavior, char>
    : public formatter<nsLiteralCString> {
  template <typename FmtContext>
  constexpr auto format(
      const mozilla::dom::NavigationHistoryBehavior& aNavigationHistoryBehavior,
      FmtContext& aCtx) const {
    return formatter<nsLiteralCString>::format(
        mozilla::dom::GetEnumString(aNavigationHistoryBehavior), aCtx);
  }
};

#endif  // mozilla_dom_Navigation_h_
