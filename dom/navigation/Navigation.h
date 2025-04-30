/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_Navigation_h___
#define mozilla_dom_Navigation_h___

#include "nsHashtablesFwd.h"
#include "nsStringFwd.h"

#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/DOMEventTargetHelper.h"
#include "mozilla/Maybe.h"
#include "mozilla/RefPtr.h"

#include "mozilla/dom/NavigateEvent.h"
#include "mozilla/dom/NavigationBinding.h"

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

class SessionHistoryInfo;

struct NavigationAPIMethodTracker;

class Navigation final : public DOMEventTargetHelper {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(Navigation, DOMEventTargetHelper)

  explicit Navigation(nsPIDOMWindowInner* aWindow);

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

  void Navigate(JSContext* aCx, const nsAString& aUrl,
                const NavigationNavigateOptions& aOptions,
                NavigationResult& aResult) {}

  MOZ_CAN_RUN_SCRIPT void Reload(JSContext* aCx,
                                 const NavigationReloadOptions& aOptions,
                                 NavigationResult& aResult);

  void TraverseTo(JSContext* aCx, const nsAString& aKey,
                  const NavigationOptions& aOptions,
                  NavigationResult& aResult) {}
  void Back(JSContext* aCx, const NavigationOptions& aOptions,
            NavigationResult& aResult) {}
  void Forward(JSContext* aCx, const NavigationOptions& aOptions,
               NavigationResult& aResult) {}

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
  void UpdateForReactivation(SessionHistoryInfo* aReactivatedEntry);

  // https://html.spec.whatwg.org/multipage/nav-history-apis.html#update-the-navigation-api-entries-for-a-same-document-navigation
  void UpdateEntriesForSameDocumentNavigation(
      SessionHistoryInfo* aDestinationSHE, NavigationType aNavigationType);

  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override;

  // The Navigation API is only enabled if both SessionHistoryInParent and
  // the dom.navigation.webidl.enabled pref are set.
  static bool IsAPIEnabled(JSContext* /* unused */ = nullptr,
                           JSObject* /* unused */ = nullptr);

  // Wrapper algorithms for firing the navigate event.
  // https://html.spec.whatwg.org/#navigate-event-firing

  MOZ_CAN_RUN_SCRIPT bool FireTraverseNavigateEvent(
      JSContext* aCx, SessionHistoryInfo* aDestinationSessionHistoryInfo,
      Maybe<UserNavigationInvolvement> aUserInvolvement);

  MOZ_CAN_RUN_SCRIPT bool FirePushReplaceReloadNavigateEvent(
      JSContext* aCx, NavigationType aNavigationType, nsIURI* aDestinationURL,
      bool aIsSameDocument, Maybe<UserNavigationInvolvement> aUserInvolvement,
      Element* aSourceElement, Maybe<const FormData&> aFormDataEntryList,
      nsIStructuredCloneContainer* aNavigationAPIState,
      nsIStructuredCloneContainer* aClassicHistoryAPIState);

  MOZ_CAN_RUN_SCRIPT bool FireDownloadRequestNavigateEvent(
      JSContext* aCx, nsIURI* aDestinationURL,
      UserNavigationInvolvement aUserInvolvement, Element* aSourceElement,
      const nsAString& aFilename);

  bool FocusedChangedDuringOngoingNavigation() const;
  void SetFocusedChangedDuringOngoingNavigation(
      bool aFocusChangedDuringOngoingNavigation);

 private:
  using UpcomingTraverseAPIMethodTrackers =
      nsTHashMap<nsIDHashKey, RefPtr<NavigationAPIMethodTracker>>;

  ~Navigation() = default;

  // https://html.spec.whatwg.org/multipage/nav-history-apis.html#has-entries-and-events-disabled
  bool HasEntriesAndEventsDisabled() const;

  void ScheduleEventsFromNavigation(
      NavigationType aType,
      const RefPtr<NavigationHistoryEntry>& aPreviousEntry,
      nsTArray<RefPtr<NavigationHistoryEntry>>&& aDisposedEntries);

  nsresult FireEvent(const nsAString& aName);

  nsresult FireErrorEvent(const nsAString& aName,
                          const ErrorEventInit& aEventInitDict);

  // https://html.spec.whatwg.org/#inner-navigate-event-firing-algorithm
  MOZ_CAN_RUN_SCRIPT bool InnerFireNavigateEvent(
      JSContext* aCx, NavigationType aNavigationType,
      NavigationDestination* aDestination,
      UserNavigationInvolvement aUserInvolvement, Element* aSourceElement,
      Maybe<const FormData&> aFormDataEntryList,
      nsIStructuredCloneContainer* aClassicHistoryAPIState,
      const nsAString& aDownloadRequestFilename);

  NavigationHistoryEntry* FindNavigationHistoryEntry(
      SessionHistoryInfo* aSessionHistoryInfo) const;

  void PromoteUpcomingAPIMethodTrackerToOngoing(Maybe<nsID>&& aDestinationKey);

  RefPtr<NavigationAPIMethodTracker>
  MaybeSetUpcomingNonTraverseAPIMethodTracker(
      JS::Handle<JS::Value> aInfo,
      nsIStructuredCloneContainer* aSerializedState);

  RefPtr<NavigationAPIMethodTracker> AddUpcomingTraverseAPIMethodTracker(
      const nsID& aKey, JS::Handle<JS::Value> aInfo);

  void SetEarlyErrorResult(NavigationResult& aResult, ErrorResult&& aRv) const;

  bool CheckIfDocumentIsFullyActiveAndMaybeSetEarlyErrorResult(
      const Document* aDocument, NavigationResult& aResult) const;

  bool CheckDocumentUnloadCounterAndMaybeSetEarlyErrorResult(
      const Document* aDocument, NavigationResult& aResult) const;

  already_AddRefed<nsIStructuredCloneContainer>
  CreateSerializedStateAndMaybeSetEarlyErrorResult(
      JSContext* aCx, const JS::Value& aState, NavigationResult& aResult) const;

  static void CleanUp(NavigationAPIMethodTracker* aNavigationAPIMethodTracker);

  void AbortOngoingNavigation(
      JSContext* aCx, JS::Handle<JS::Value> aError = JS::UndefinedHandleValue);

  void LogHistory() const;

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

  // https://html.spec.whatwg.org/multipage/nav-history-apis.html#upcoming-non-traverse-api-method-tracker
  RefPtr<NavigationAPIMethodTracker> mUpcomingNonTraverseAPIMethodTracker;

  // https://html.spec.whatwg.org/multipage/nav-history-apis.html#upcoming-traverse-api-method-trackers
  UpcomingTraverseAPIMethodTrackers mUpcomingTraverseAPIMethodTrackers;

  // https://html.spec.whatwg.org/#concept-navigation-transition
  RefPtr<NavigationTransition> mTransition;

  // https://html.spec.whatwg.org/#navigation-activation
  RefPtr<NavigationActivation> mActivation;
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_Navigation_h___
