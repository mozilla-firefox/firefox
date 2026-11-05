/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/Navigation.h"

#include "NavigationPrecommitController.h"
#include "fmt/format.h"
#include "jsapi.h"
#include "mozilla/CycleCollectedJSContext.h"
#include "mozilla/CycleCollectedUniquePtr.h"
#include "mozilla/HoldDropJSObjects.h"
#include "mozilla/Logging.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/UseCounter.h"
#include "mozilla/dom/DOMException.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/ErrorEvent.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/NavigationActivation.h"
#include "mozilla/dom/NavigationBinding.h"
#include "mozilla/dom/NavigationCurrentEntryChangeEvent.h"
#include "mozilla/dom/NavigationHistoryEntry.h"
#include "mozilla/dom/NavigationTransition.h"
#include "mozilla/dom/NavigationUtils.h"
#include "mozilla/dom/PContent.h"
#include "mozilla/dom/PageSwapEvent.h"
#include "mozilla/dom/PermissionsPolicy.h"
#include "mozilla/dom/Promise-inl.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/RootedDictionary.h"
#include "mozilla/dom/SessionHistoryEntry.h"
#include "mozilla/dom/ViewTransition.h"
#include "mozilla/dom/WindowContext.h"
#include "mozilla/dom/WindowGlobalChild.h"
#include "nsContentUtils.h"
#include "nsCycleCollectionParticipant.h"
#include "nsDocShell.h"
#include "nsGkAtoms.h"
#include "nsGlobalWindowInner.h"
#include "nsIMultiPartChannel.h"
#include "nsIPrincipal.h"
#include "nsISHistory.h"
#include "nsIScriptChannel.h"
#include "nsIStructuredCloneContainer.h"
#include "nsIXULRuntime.h"
#include "nsNetUtil.h"
#include "nsPIDOMWindowInlines.h"
#include "nsTHashtable.h"

mozilla::LazyLogModule gNavigationAPILog("NavigationAPI");

#define LOG_FMTE(format, ...) \
  MOZ_LOG_FMT(gNavigationAPILog, LogLevel::Error, format, ##__VA_ARGS__);

#define LOG_FMTW(format, ...) \
  MOZ_LOG_FMT(gNavigationAPILog, LogLevel::Warning, format, ##__VA_ARGS__);

#define LOG_FMTI(format, ...) \
  MOZ_LOG_FMT(gNavigationAPILog, LogLevel::Info, format, ##__VA_ARGS__);

#define LOG_FMTD(format, ...) \
  MOZ_LOG_FMT(gNavigationAPILog, LogLevel::Debug, format, ##__VA_ARGS__);

#define LOG_FMTV(format, ...) \
  MOZ_LOG_FMT(gNavigationAPILog, LogLevel::Verbose, format, ##__VA_ARGS__);

namespace mozilla::dom {

static void InitNavigationResult(NavigationResult& aResult,
                                 const RefPtr<Promise>& aCommitted,
                                 const RefPtr<Promise>& aFinished) {
  if (aCommitted) {
    aResult.mCommitted.Reset();
    aResult.mCommitted.Construct(*aCommitted);
  }

  if (aFinished) {
    aResult.mFinished.Reset();
    aResult.mFinished.Construct(*aFinished);
  }
}

NavigationAPIMethodTracker::NavigationAPIMethodTracker(
    Navigation* aNavigationObject, const Maybe<nsID> aKey,
    const JS::Value& aInfo, nsIStructuredCloneContainer* aSerializedState,
    NavigationHistoryEntry* aCommittedToEntry, Promise* aCommittedPromise,
    Promise* aFinishedPromise, bool aPending)
    : mNavigationObject(aNavigationObject),
      mKey(aKey),
      mInfo(aInfo),
      mPending(aPending),
      mSerializedState(aSerializedState),
      mCommittedToEntry(aCommittedToEntry),
      mCommittedPromise(aCommittedPromise),
      mFinishedPromise(aFinishedPromise) {
  mozilla::HoldJSObjects(this);
}

NavigationAPIMethodTracker::~NavigationAPIMethodTracker() {
  mozilla::DropJSObjects(this);
}

// https://html.spec.whatwg.org/#navigation-api-method-tracker-clean-up
void NavigationAPIMethodTracker::CleanUp() { Navigation::CleanUp(this); }

// https://html.spec.whatwg.org/#notify-about-the-committed-to-entry
void NavigationAPIMethodTracker::NotifyAboutCommittedToEntry(
    NavigationHistoryEntry* aNHE) {
  MOZ_DIAGNOSTIC_ASSERT(mCommittedPromise);
  // Step 1
  mCommittedToEntry = aNHE;
  if (mSerializedState) {
    // Step 2
    aNHE->SetNavigationAPIState(mSerializedState);
    // At this point, apiMethodTracker's serialized state is no longer needed.
    // We drop it do now for efficiency.
    mSerializedState = nullptr;
  }
  mCommittedPromise->MaybeResolve(aNHE);
}

// https://html.spec.whatwg.org/#resolve-the-finished-promise
void NavigationAPIMethodTracker::ResolveFinishedPromise() {
  MOZ_DIAGNOSTIC_ASSERT(mFinishedPromise);
  // Step 1
  MOZ_DIAGNOSTIC_ASSERT(mCommittedToEntry);
  // Step 2
  mFinishedPromise->MaybeResolve(mCommittedToEntry);
  // Step 3
  CleanUp();
}

// https://html.spec.whatwg.org/#reject-the-finished-promise
void NavigationAPIMethodTracker::RejectFinishedPromise(
    JS::Handle<JS::Value> aException) {
  MOZ_DIAGNOSTIC_ASSERT(mFinishedPromise);
  MOZ_DIAGNOSTIC_ASSERT(mCommittedPromise);
  // Step 1
  mCommittedPromise->MaybeReject(aException);
  // Step 2
  mFinishedPromise->MaybeReject(aException);
  // Step 3
  CleanUp();
}

// https://html.spec.whatwg.org/#navigation-api-method-tracker-derived-result
void NavigationAPIMethodTracker::CreateResult(JSContext* aCx,
                                              NavigationResult& aResult) {
  // A navigation API method tracker-derived result for a navigation API
  // method tracker is a NavigationResult dictionary instance given by the
  // following steps:
  // 1. If apiMethodTracker is pending, then return an early error result for
  //    an "AbortError" DOMException.
  if (mPending) {
    ErrorResult rv;
    rv.ThrowAbortError("Navigation aborted");
    mNavigationObject->SetEarlyErrorResult(aCx, aResult, std::move(rv));
    return;
  }
  // 2. Return «[ "committed" → apiMethodTracker's committed promise,
  //            "finished" → apiMethodTracker's finished promise ]».
  InitNavigationResult(aResult, mCommittedPromise, mFinishedPromise);
}

bool NavigationAPIMethodTracker::IsHandled() const {
  return this != mNavigationObject->mOngoingAPIMethodTracker && mKey &&
         !mNavigationObject->mUpcomingTraverseAPIMethodTrackers.Contains(*mKey);
}

NS_IMPL_CYCLE_COLLECTION_WITH_JS_MEMBERS(NavigationAPIMethodTracker,
                                         (mNavigationObject, mSerializedState,
                                          mCommittedToEntry, mCommittedPromise,
                                          mFinishedPromise),
                                         (mInfo))

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(NavigationAPIMethodTracker)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(NavigationAPIMethodTracker)
NS_IMPL_CYCLE_COLLECTING_RELEASE(NavigationAPIMethodTracker)

NS_IMPL_CYCLE_COLLECTION_INHERITED(Navigation, DOMEventTargetHelper, mEntries,
                                   mOngoingNavigateEvent, mTransition,
                                   mActivation, mOngoingAPIMethodTracker,
                                   mUpcomingTraverseAPIMethodTrackers);
NS_IMPL_ADDREF_INHERITED(Navigation, DOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(Navigation, DOMEventTargetHelper)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(Navigation)
NS_INTERFACE_MAP_END_INHERITING(DOMEventTargetHelper)

Navigation::Navigation(nsPIDOMWindowInner* aWindow)
    : DOMEventTargetHelper(aWindow) {
  MOZ_ASSERT(aWindow);
}

JSObject* Navigation::WrapObject(JSContext* aCx,
                                 JS::Handle<JSObject*> aGivenProto) {
  return Navigation_Binding::Wrap(aCx, this, aGivenProto);
}

void Navigation::EventListenerAdded(nsAtom* aType) {
  UpdateNeedsTraverse();

  auto counter = [aType]() -> Maybe<UseCounter> {
    if (aType == nsGkAtoms::onnavigate) {
      return Some(eUseCounter_custom_NavigationOnnavigate);
    }
    if (aType == nsGkAtoms::onnavigatesuccess) {
      return Some(eUseCounter_custom_NavigationOnnavigatesuccess);
    }
    if (aType == nsGkAtoms::onnavigateerror) {
      return Some(eUseCounter_custom_NavigationOnnavigateerror);
    }
    if (aType == nsGkAtoms::oncurrententrychange) {
      return Some(eUseCounter_custom_NavigationOncurrententrychange);
    }
    return Nothing();
  }();

  if (counter) {
    if (Document* doc = GetAssociatedDocument()) {
      doc->SetUseCounter(*counter);
    }
  }

  EventTarget::EventListenerAdded(aType);
}

void Navigation::EventListenerRemoved(nsAtom* aType) {
  UpdateNeedsTraverse();
  EventTarget::EventListenerRemoved(aType);
}

/* static */
bool Navigation::IsAPIEnabled(JSContext* /* unused */, JSObject* /* unused */) {
  return StaticPrefs::dom_navigation_webidl_enabled_DoNotUseDirectly();
}

void Navigation::Entries(
    nsTArray<RefPtr<NavigationHistoryEntry>>& aResult) const {
  if (HasEntriesAndEventsDisabled()) {
    aResult.Clear();
    return;
  }
  aResult = mEntries.Clone();
}

already_AddRefed<NavigationHistoryEntry> Navigation::GetCurrentEntry() const {
  if (HasEntriesAndEventsDisabled()) {
    return nullptr;
  }

  if (!mCurrentEntryIndex) {
    return nullptr;
  }

  MOZ_LOG(gNavigationAPILog, LogLevel::Debug,
          ("Current Entry: %d; Amount of Entries: %d", int(*mCurrentEntryIndex),
           int(mEntries.Length())));
  MOZ_ASSERT(*mCurrentEntryIndex < mEntries.Length());

  RefPtr entry{mEntries[*mCurrentEntryIndex]};
  return entry.forget();
}

// https://html.spec.whatwg.org/#dom-navigation-updatecurrententry
void Navigation::UpdateCurrentEntry(
    JSContext* aCx, const NavigationUpdateCurrentEntryOptions& aOptions,
    ErrorResult& aRv) {
  LOG_FMTI("Called navigation.updateCurrentEntry()");
  RefPtr currentEntry(GetCurrentEntry());
  if (!currentEntry) {
    aRv.ThrowInvalidStateError(
        "Can't call updateCurrentEntry without a valid entry.");
    return;
  }

  JS::Rooted<JS::Value> state(aCx, aOptions.mState);
  auto serializedState = MakeRefPtr<nsStructuredCloneContainer>();
  nsresult rv = serializedState->InitFromJSVal(state, aCx);
  if (NS_FAILED(rv)) {
    aRv.ThrowDataCloneError(
        "Failed to serialize value for updateCurrentEntry.");
    return;
  }

  currentEntry->SetNavigationAPIState(serializedState);

  ToMaybeRef(GetOwnerWindow())
      .andThen([](auto& aWindow) {
        return ToMaybeRef(aWindow.GetBrowsingContext());
      })
      .apply([serializedState](auto& navigable) {
        navigable.SynchronizeNavigationAPIState(serializedState);
        ToMaybeRef(nsDocShell::Cast(navigable.GetDocShell()))
            .andThen([](auto& docshell) {
              return ToMaybeRef(docshell.GetActiveSessionHistoryInfo());
            })
            .apply([serializedState](auto& activeInfo) {
              activeInfo.SetNavigationAPIState(serializedState);
            });
      });

  NavigationCurrentEntryChangeEventInit init;
  init.mFrom = currentEntry;
  // Leaving the navigation type unspecified means it will be initialized to
  // null.
  RefPtr event = NavigationCurrentEntryChangeEvent::Constructor(
      this, u"currententrychange"_ns, init);
  event->SetTrusted(true);
  DispatchEvent(*event);
}

NavigationTransition* Navigation::GetTransition() const { return mTransition; }

NavigationActivation* Navigation::GetActivation() const { return mActivation; }

template <typename I>
bool SupportsInterface(nsISupports* aSupports) {
  nsCOMPtr<I> ptr = do_QueryInterface(aSupports);
  return ptr != nullptr;
}

static bool IsNonBlankAboutPage(Document* aDocument) {
  return aDocument->IsAboutPage() &&
         !NS_IsAboutBlankAllowQueryAndFragment(aDocument->GetDocumentURI());
}

// https://html.spec.whatwg.org/#has-entries-and-events-disabled
bool Navigation::HasEntriesAndEventsDisabled() const {
  Document* doc = GetAssociatedDocument();
  return !doc || !doc->IsCurrentActiveDocument() ||
         doc->IsEverInitialDocument() ||
         doc->GetPrincipal()->GetIsNullPrincipal() ||
         // We explicitly disallow documents loaded through multipart and
         // script channels from having events or entries. See bug 1996218
         // and bug 1996221
         SupportsInterface<nsIMultiPartChannel>(doc->GetChannel()) ||
         SupportsInterface<nsIScriptChannel>(doc->GetChannel()) ||
         // We also disallow documents embedded using <object>/<embed>. See bug
         // 1996215.
         !doc->GetBrowsingContext() ||
         doc->GetBrowsingContext()->IsEmbedderTypeObjectOrEmbed() ||
         // Furthermore we disallow all about: documents that aren't non-initial
         // about:blank. See bug 2043508.
         IsNonBlankAboutPage(doc);
}

// https://html.spec.whatwg.org/#initialize-the-navigation-api-entries-for-a-new-document
void Navigation::InitializeHistoryEntries(
    mozilla::Span<const SessionHistoryInfo> aNewSHInfos,
    const SessionHistoryInfo* aInitialSHInfo) {
  LOG_FMTD("Attempting to initialize history entries for {}.",
           aInitialSHInfo->GetURI()
               ? aInitialSHInfo->GetURI()->GetSpecOrDefault()
               : "<no uri>"_ns)

  mEntries.Clear();
  mCurrentEntryIndex.reset();
  if (HasEntriesAndEventsDisabled()) {
    return;
  }

  for (auto i = 0ul; i < aNewSHInfos.Length(); i++) {
    mEntries.AppendElement(MakeRefPtr<NavigationHistoryEntry>(
        GetRelevantGlobal(), &aNewSHInfos[i], i));
    if (aNewSHInfos[i].NavigationKey() == aInitialSHInfo->NavigationKey()) {
      mCurrentEntryIndex = Some(i);
    }
  }

  LogHistory();

  nsID key = aInitialSHInfo->NavigationKey();
  nsID id = aInitialSHInfo->NavigationId();
  MOZ_LOG(
      gNavigationAPILog, LogLevel::Debug,
      ("aInitialSHInfo: %s %s\n", key.ToString().get(), id.ToString().get()));
}

// https://html.spec.whatwg.org/#update-the-navigation-api-entries-for-a-same-document-navigation
void Navigation::UpdateEntriesForSameDocumentNavigation(
    SessionHistoryInfo* aDestinationSHE, NavigationType aNavigationType,
    bool aFiredNavigateEvent) {
  // Step 1.
  if (HasEntriesAndEventsDisabled()) {
    return;
  }

  MOZ_LOG(gNavigationAPILog, LogLevel::Debug,
          ("Updating entries for same-document navigation"));

  // Steps 2-7.
  RefPtr<NavigationHistoryEntry> oldCurrentEntry = GetCurrentEntry();
  nsTArray<RefPtr<NavigationHistoryEntry>> disposedEntries;
  switch (aNavigationType) {
    case NavigationType::Traverse:
      MOZ_LOG(gNavigationAPILog, LogLevel::Debug, ("Traverse navigation"));
      SetCurrentEntryIndex(aDestinationSHE);
      MOZ_ASSERT(mCurrentEntryIndex);
      break;

    case NavigationType::Push:
      MOZ_LOG(gNavigationAPILog, LogLevel::Debug, ("Push navigation"));
      mCurrentEntryIndex =
          Some(mCurrentEntryIndex ? *mCurrentEntryIndex + 1 : 0);
      disposedEntries.AppendElements(Span(mEntries).From(*mCurrentEntryIndex));
      mEntries.RemoveElementsAt(*mCurrentEntryIndex,
                                mEntries.Length() - *mCurrentEntryIndex);
      mEntries.AppendElement(MakeRefPtr<NavigationHistoryEntry>(
          GetRelevantGlobal(), aDestinationSHE, *mCurrentEntryIndex));
      break;

    case NavigationType::Replace:
      MOZ_LOG(gNavigationAPILog, LogLevel::Debug, ("Replace navigation"));
      if (!oldCurrentEntry) {
        LOG_FMTE("No current entry.");
        MOZ_ASSERT(false, "FIXME");
        return;
      }
      disposedEntries.AppendElement(oldCurrentEntry);
      MOZ_DIAGNOSTIC_ASSERT(
          aDestinationSHE->NavigationKey() ==
          oldCurrentEntry->SessionHistoryInfo()->NavigationKey());
      mEntries[*mCurrentEntryIndex] = MakeRefPtr<NavigationHistoryEntry>(
          GetRelevantGlobal(), aDestinationSHE, *mCurrentEntryIndex);
      break;

    case NavigationType::Reload:
      break;
  }

  // Step 8.
  if (mOngoingAPIMethodTracker) {
    RefPtr<NavigationHistoryEntry> currentEntry = GetCurrentEntry();
    mOngoingAPIMethodTracker->NotifyAboutCommittedToEntry(currentEntry);
  }

  // Reset cached index for entries.
  for (auto& entry : disposedEntries) {
    entry->ResetIndexForDisposal();
  }

  RefPtr ongoingNavigateEvent =
      aFiredNavigateEvent ? mOngoingNavigateEvent : nullptr;
  RefPtr ongoingAPIMethodTracker = mOngoingAPIMethodTracker;

  {
    // Steps 9-12.

    // This prevents performing a microtask checkpoint, see
    // https://html.spec.whatwg.org/#note-suppress-microtasks-during-navigation-events.
    nsAutoMicroTask mt;
    NavigationCurrentEntryChangeEventInit init;
    init.mFrom = oldCurrentEntry;
    init.mNavigationType.SetValue(aNavigationType);
    RefPtr event = NavigationCurrentEntryChangeEvent::Constructor(
        this, u"currententrychange"_ns, init);
    event->SetTrusted(true);
    DispatchEvent(*event);

    for (RefPtr<NavigationHistoryEntry>& entry : disposedEntries) {
      MOZ_KnownLive(entry)->FireDisposeEvent();
    }

    if (ongoingNavigateEvent) {
      RunNavigateEventHandlerSteps(ongoingNavigateEvent,
                                   ongoingAPIMethodTracker);
    }
  }
}

void Navigation::TruncateForwardEntries(uint32_t aNewLength) {
  if (HasEntriesAndEventsDisabled()) {
    return;
  }

  if (aNewLength >= mEntries.Length()) {
    return;
  }

  if (mCurrentEntryIndex && *mCurrentEntryIndex >= aNewLength) {
    return;
  }

  nsTArray<RefPtr<NavigationHistoryEntry>> disposedEntries;
  disposedEntries.AppendElements(Span(mEntries).From(aNewLength));
  mEntries.TruncateLength(aNewLength);

  for (auto& entry : disposedEntries) {
    entry->ResetIndexForDisposal();
  }

  NS_DispatchToMainThread(NS_NewRunnableFunction(
      "Navigation::TruncateForwardEntries",
      [oldEntries =
           std::move(disposedEntries)]() MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA {
        for (const RefPtr<NavigationHistoryEntry>& disposedNHE : oldEntries) {
          MOZ_KnownLive(disposedNHE)->FireDisposeEvent();
        }
      }));
}

static bool Equals(nsIURI* aURI, nsIURI* aOtherURI) {
  bool equals = false;
  return aURI && aOtherURI && NS_SUCCEEDED(aURI->Equals(aOtherURI, &equals)) &&
         equals;
}

static void LogEvent(Event* aEvent, NavigateEvent* aOngoingEvent,
                     const nsACString& aReason) {
  if (!MOZ_LOG_TEST(gNavigationAPILog, LogLevel::Debug)) {
    return;
  }

  nsAutoString eventType;
  aEvent->GetType(eventType);

  nsTArray<nsCString> log = {nsCString(aReason),
                             NS_ConvertUTF16toUTF8(eventType)};

  if (aEvent->Cancelable()) {
    log.AppendElement("cancelable");
  }

  if (aOngoingEvent) {
    log.AppendElement(fmt::format("{}", aOngoingEvent->NavigationType()));

    if (RefPtr<NavigationDestination> destination =
            aOngoingEvent->Destination()) {
      log.AppendElement(destination->GetURL()->GetSpecOrDefault());
    }

    if (aOngoingEvent->HashChange()) {
      log.AppendElement("hashchange"_ns);
    }
  }

  LOG_FMTD("{}", fmt::join(log.begin(), log.end(), std::string_view{" "}));
}

struct NavigationWaitForAllScope final : public nsISupports,
                                         public SupportsWeakPtr {
  NavigationWaitForAllScope(Navigation* aNavigation,
                            NavigationAPIMethodTracker* aApiMethodTracker,
                            NavigateEvent* aEvent,
                            NavigationDestination* aDestination,
                            nsDocShellLoadState* aLoadState)
      : mNavigation(aNavigation),
        mAPIMethodTracker(aApiMethodTracker),
        mEvent(aEvent),
        mDestination(aDestination),
        mLoadState(aLoadState) {}
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS_FINAL
  NS_DECL_CYCLE_COLLECTION_CLASS(NavigationWaitForAllScope)
  RefPtr<Navigation> mNavigation;
  RefPtr<NavigationAPIMethodTracker> mAPIMethodTracker;
  RefPtr<NavigateEvent> mEvent;
  RefPtr<NavigationDestination> mDestination;
  RefPtr<nsDocShellLoadState> mLoadState;

 private:
  ~NavigationWaitForAllScope() = default;

  BrowsingContext* GetBrowsingContext() const {
    nsGlobalWindowInner* window = mNavigation->GetOwnerWindow();
    if (!window) {
      return nullptr;
    }
    return window->GetBrowsingContext();
  }

 public:
  // https://html.spec.whatwg.org/#process-navigate-event-handler-failure
  MOZ_CAN_RUN_SCRIPT void ProcessNavigateEventHandlerFailure(
      JS::Handle<JS::Value> aRejectionReason) {
    // To process navigate event handler failure given a NavigateEvent object
    // event and a reason:
    LogEvent(mEvent, mEvent, "Rejected"_ns);

    // 1. If event's relevant global object's associated Document is not fully
    //    active, then return.
    if (RefPtr document = mEvent->GetDocument();
        !document || !document->IsFullyActive()) {
      return;
    }

    // 2. If event's abort controller's signal is aborted, then return.
    if (AbortSignal* signal = mEvent->Signal(); signal->Aborted()) {
      return;
    }

    // 3. Assert: event is event's relevant global object's navigation API's
    //    ongoing navigate event.
    MOZ_DIAGNOSTIC_ASSERT(mEvent == mNavigation->mOngoingNavigateEvent);

    // 4. If event's interception state is not "intercepted", then finish event
    //    given false.
    RefPtr event = mEvent;
    if (mEvent->InterceptionState() !=
        NavigateEvent::InterceptionState::Intercepted) {
      event->Finish(false);
    }

    // 5. Abort event given reason.
    if (AutoJSAPI jsapi; !NS_WARN_IF(!jsapi.Init(mEvent->GetParentObject()))) {
      RefPtr navigation = mNavigation;
      navigation->AbortNavigateEvent(jsapi.cx(), event, aRejectionReason);
    }
  }

  // https://html.spec.whatwg.org/#commit-a-navigate-event
  MOZ_CAN_RUN_SCRIPT void CommitNavigateEvent() {
    // 1. Let navigation be event's target.
    // Omitted since Navigation is part of this's state.

    // 3. If event's relevant global object's associated Document is not fully
    //    active, then return.
    RefPtr document = mEvent->GetDocument();
    if (!document || !document->IsFullyActive()) {
      return;
    }
    // 2. Let navigable be event's relevant global object's navigable.
    RefPtr<nsDocShell> docShell = nsDocShell::Cast(document->GetDocShell());
    // 4. If event's abort controller's signal is aborted, then return.
    if (AbortSignal* signal = mEvent->Signal(); signal->Aborted()) {
      return;
    }

    // 6. Let endResultIsSameDocument be true if event's interception state is
    //    not "none" or event's destination's is same document is true.
    const bool endResultIsSameDocument =
        mEvent->InterceptionState() != NavigateEvent::InterceptionState::None ||
        mDestination->SameDocument();

    // Set up to maybe resume applying the history step. This needs to run after
    // the microtask checkpoint, and it's therefore important that this is done
    // before step 7.
    auto resumeApplyTheHistoryStep = MakeScopeExit(
        [browsingContext = RefPtr{GetBrowsingContext()},
         loadState = RefPtr{mLoadState}]() MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA {
          if (browsingContext && loadState) {
            // Resuming the history step reuses a load state that has already
            // been targeted by InternalLoad. Clear the target because LoadURI
            // expects targeting to happen in InternalLoad.
            loadState->SetTargetBrowsingContext(nullptr);
            browsingContext->LoadURI(loadState, /* aSetNavigating */ false);
          }
        });

    // 7. Prepare to run script given navigation's relevant settings object.
    // This runs step 12 when going out of scope. See
    // https://html.spec.whatwg.org/#note-suppress-microtasks-during-navigation-events.
    nsAutoMicroTask mt;

    bool traverseWasIntercepted = false;
    // 9. If event's interception state is not "none":
    if (mEvent->InterceptionState() != NavigateEvent::InterceptionState::None) {
      // The copy of the active session history info might be stale at this
      // point, so make sure to update that. This is not a spec step, but a side
      // effect of SHIP owning the session history entries making Navigation API
      // keep copies for its purposes. Should navigation get aborted at this
      // point, all we've done is eagerly stored scroll positions.
      if (RefPtr current = mNavigation->GetCurrentEntry()) {
        nsPoint scrollPos = docShell->GetCurScrollPos();
        current->SessionHistoryInfo()->SetScrollPosition(scrollPos.x,
                                                         scrollPos.y);
      }

      // 5. Set event's interception state to "committed".
      // See https://github.com/whatwg/html/issues/11830 for this change.
      mEvent->SetInterceptionState(NavigateEvent::InterceptionState::Committed);
      // 9.1 Switch on event's navigationType:
      switch (mEvent->NavigationType()) {
        case NavigationType::Push:
        case NavigationType::Replace:
          // Run the URL and history update steps given event's relevant
          // global object's associated Document and event's destination's
          // URL, with serializedData set to event's classic history API
          // state and historyHandling set to event's navigationType.
          if (docShell) {
            nsCOMPtr<nsIURI> destinationURI = mDestination->GetURL();
            nsCOMPtr<nsIURI> documentURI = document->GetDocumentURI();
            nsCOMPtr<nsIStructuredCloneContainer> state =
                mEvent->ClassicHistoryAPIState();
            docShell->UpdateURLAndHistory(
                document, destinationURI, state,
                *NavigationUtils::NavigationHistoryBehavior(
                    mEvent->NavigationType()),
                documentURI, Equals(destinationURI, documentURI));
          }
          break;
        case NavigationType::Reload:
          // Update the navigation API entries for a same-document navigation
          // given navigation, navigable's active session history entry, and
          // "reload".
          if (docShell) {
            RefPtr navigation = mNavigation;
            navigation->UpdateEntriesForSameDocumentNavigation(
                docShell->GetActiveSessionHistoryInfo(),
                mEvent->NavigationType());
          }
          break;
        case NavigationType::Traverse: {
          // 1. Set navigation's suppress normal scroll restoration during
          //    ongoing navigation to true.
          mNavigation->mSuppressNormalScrollRestorationDuringOngoingNavigation =
              true;
          // 2. Let userInvolvement be "none".
          // 3. If event's userInitiated is true, then set userInvolvement to
          // "activation".
          UserNavigationInvolvement userInvolvement =
              mEvent->UserInitiated() ? UserNavigationInvolvement::Activation
                                      : UserNavigationInvolvement::None;
          if (mLoadState) {
            mLoadState->SetUserNavigationInvolvement(userInvolvement);
            mLoadState->SetIsResumingInterceptedNavigation(true);
          }
          // 4. Append the following session history traversal steps to
          //    navigable's traversable navigable:
          // 4.1 Resume applying the traverse history step given event's
          //     destination's entry's session history entry's step,
          //     navigable's traversable navigable, and userInvolvement.
          traverseWasIntercepted = true;
          break;
        }
        default:
          break;
      }
    }

    if (!traverseWasIntercepted) {
      resumeApplyTheHistoryStep.release();
    }

    // 8. If navigation's transition is not null, then resolve navigation's
    //    transition's committed promise with undefined.
    // Steps 8 and 9 are swapped to have a consistent promise behavior
    // (see https://github.com/whatwg/html/issues/11842)
    if (mNavigation->mTransition) {
      mNavigation->mTransition->Committed()->MaybeResolveWithUndefined();
    }

    // 10. If endResultIsSameDocument is true:
    if (endResultIsSameDocument) {
      return;
    }

    if (mAPIMethodTracker && mNavigation->mOngoingAPIMethodTracker) {
      // In contrast to spec we add a check that we're still the ongoing
      // tracker. If we're not, then we've already been cleaned up.
      MOZ_DIAGNOSTIC_ASSERT(mAPIMethodTracker ==
                            mNavigation->mOngoingAPIMethodTracker);
      // Step 11
      mAPIMethodTracker->CleanUp();
    }

    return;
  }

  MOZ_CAN_RUN_SCRIPT void CommitNavigateEventSuccessSteps() {
    LogEvent(mEvent, mEvent, "Success"_ns);

    // 1. If event's relevant global object is not fully active, then abort
    //    these steps.
    RefPtr document = mEvent->GetDocument();
    if (!document || !document->IsFullyActive()) {
      return;
    }

    // 2. If event's abort controller's signal is aborted, then abort these
    //    steps.
    if (AbortSignal* signal = mEvent->Signal(); signal->Aborted()) {
      return;
    }

    // 3. Assert: event equals navigation's ongoing navigate event.
    MOZ_DIAGNOSTIC_ASSERT(mEvent == mNavigation->mOngoingNavigateEvent);

    // 4. Set navigation's ongoing navigate event to null.
    mNavigation->mOngoingNavigateEvent = nullptr;

    // 5. If apiMethodTracker is non-null, then resolve the finished promise for
    // apiMethodTracker.
    if (mAPIMethodTracker) {
      mAPIMethodTracker->ResolveFinishedPromise();
    }

    // 6. Finish event given true.
    RefPtr event = mEvent;
    event->Finish(true);

    // 7. Fire an event named navigatesuccess at navigation.
    RefPtr navigation = mNavigation;
    navigation->FireEvent(u"navigatesuccess"_ns);

    // 8. If navigation's transition is not null, then resolve navigation's
    //    transition's finished promise with undefined.
    if (mNavigation->mTransition) {
      mNavigation->mTransition->Finished()->MaybeResolveWithUndefined();
    }
    // 9. Set navigation's transition to null.
    mNavigation->mTransition = nullptr;
  }
};

NS_IMPL_CYCLE_COLLECTION_WEAK_PTR(NavigationWaitForAllScope, mNavigation,
                                  mAPIMethodTracker, mEvent, mDestination)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(NavigationWaitForAllScope)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(NavigationWaitForAllScope)
NS_IMPL_CYCLE_COLLECTING_RELEASE(NavigationWaitForAllScope)

void Navigation::RunNavigateEventHandlerSteps(
    NavigateEvent* aNavigateEvent,
    NavigationAPIMethodTracker* aAPIMethodTracker) {
  // 10.1 Let promisesList be an empty list.
  AutoTArray<RefPtr<Promise>, 16> promiseList;

  RefPtr event = aNavigateEvent;
  RefPtr tracker = aAPIMethodTracker;

  // 10.2 For each handler of event's navigation handler list:
  for (auto& handler : event->NavigationHandlerList().Clone()) {
    // 10.2.1 Append the result of invoking handler with an empty
    //        arguments list to promisesList.
    RefPtr promise = MOZ_KnownLive(handler)->Call();
    if (promise) {
      promiseList.AppendElement(promise);
    }
  }

  // 10.3 If promisesList's size is 0, then set promisesList to « a promise
  //      resolved with undefined ».
  //
  nsCOMPtr globalObject = GetRelevantGlobal();
  if (promiseList.IsEmpty()) {
    RefPtr promise = Promise::CreateResolvedWithUndefined(globalObject,
                                                          IgnoredErrorResult());
    if (promise) {
      promiseList.AppendElement(promise);
    }
  }

  // 10.4 Wait for all of promisesList, with the following success steps:
  RefPtr destination = event->Destination();
  RefPtr scope = MakeRefPtr<NavigationWaitForAllScope>(this, tracker, event,
                                                       destination, nullptr);

  // If the committed promise in the api method tracker hasn't resolved yet,
  // we can't run neither of the success nor failure steps. To handle that
  // we set up a callback for when that resolves. This differs from how spec
  // performs these steps, since spec can perform more of
  // #apply-the-history-steps in a synchronous way.
  auto cancelSteps =
      [weakScope = WeakPtr(scope)](JS::Handle<JS::Value> aRejectionReason)
          MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA {
            // If weakScope is null we've been cycle collected
            if (weakScope) {
              RefPtr scope = weakScope.get();
              scope->ProcessNavigateEventHandlerFailure(aRejectionReason);
            }
          };
  auto successSteps =
      [weakScope = WeakPtr(scope)](const Span<JS::Heap<JS::Value>>&)
          MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA {
            // If weakScope is null we've been cycle collected
            if (weakScope) {
              RefPtr scope = weakScope.get();
              scope->CommitNavigateEventSuccessSteps();
            }
          };

  Promise::WaitForAll(globalObject, promiseList, successSteps, cancelSteps,
                      scope);
}

// https://html.spec.whatwg.org/#update-the-navigation-api-entries-for-reactivation
void Navigation::UpdateForReactivation(
    Span<const SessionHistoryInfo> aNewSHEs,
    const SessionHistoryInfo* aReactivatedEntry) {
  // Step 1
  if (HasEntriesAndEventsDisabled()) {
    return;
  }

  LOG_FMTD(
      "Reactivate {} {}", fmt::ptr(aReactivatedEntry),
      fmt::join(
          [currentEntry = RefPtr{GetCurrentEntry()}](auto& aEntries) {
            nsTArray<nsCString> entries;
            (void)TransformIfAbortOnErr(
                aEntries, MakeBackInserter(entries), [](auto) { return true; },
                [currentEntry](auto& entry) -> Result<nsCString, nsresult> {
                  return nsPrintfCString(
                      "%s%s", entry.NavigationKey().ToString().get(),
                      currentEntry &&
                              currentEntry->Key() == entry.NavigationKey()
                          ? "*"
                          : "");
                });
            return entries;
          }(aNewSHEs),
          ", "));

  // Step 2
  nsTArray<RefPtr<NavigationHistoryEntry>> newNHEs;

  // Step 3
  nsTArray<RefPtr<NavigationHistoryEntry>> oldNHEs = mEntries.Clone();

  // Step 4
  for (const auto& newSHE : aNewSHEs) {
    // Step 4.1
    RefPtr<NavigationHistoryEntry> newNHE;
    if (ArrayIterator matchingOldNHE = std::find_if(
            oldNHEs.begin(), oldNHEs.end(),
            [newSHE](const auto& aNHE) { return aNHE->IsSameEntry(&newSHE); });
        matchingOldNHE != oldNHEs.end()) {
      // Step 4.2.1
      newNHE = *matchingOldNHE;
      // Since we cache indices we need to update newNHE here. Also, narrowing.
      // Yay.
      CheckedInt<int64_t> newIndex(newNHEs.Length());
      newNHE->SetIndex(newIndex.value());

      // Step 4.2.2
      oldNHEs.RemoveElementAt(matchingOldNHE);
    } else {
      // Step 4.3.1 and 4.3.2
      newNHE = MakeRefPtr<NavigationHistoryEntry>(GetRelevantGlobal(), &newSHE,
                                                  newNHEs.Length());
    }
    // Step 4.4
    newNHEs.AppendElement(newNHE);
  }

  // Step 5
  mEntries = std::move(newNHEs);

  // Step 6
  mCurrentEntryIndex = GetNavigationEntryIndex(*aReactivatedEntry);

  // Reset cached index for entries.
  for (const auto& oldEntry : oldNHEs) {
    oldEntry->ResetIndexForDisposal();
  }

  // Step 7
  NS_DispatchToMainThread(NS_NewRunnableFunction(
      "UpdateForReactivation",
      [oldEntries = std::move(oldNHEs)]() MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA {
        // Step 7.1
        for (const RefPtr<NavigationHistoryEntry>& disposedNHE : oldEntries) {
          // Step 7.1.1
          MOZ_KnownLive(disposedNHE)->FireDisposeEvent();
        }
      }));
}

// https://html.spec.whatwg.org/#navigation-api-early-error-result
void Navigation::SetEarlyErrorResult(JSContext* aCx, NavigationResult& aResult,
                                     ErrorResult&& aRv) const {
  MOZ_ASSERT(aRv.Failed());
  // An early error result for an exception e is a NavigationResult dictionary
  // instance given by
  // «[ "committed" → a promise rejected with e,
  //    "finished" → a promise rejected with e ]».

  // Get the global of the current realm to create the DOMException.
  // See https://webidl.spec.whatwg.org/#js-creating-throwing-exceptions
  nsIGlobalObject* global = GetCurrentGlobal();
  if (!global) {
    // Creating a promise should only fail if there is no global.
    // In this case, the only solution is to ignore the error.
    aRv.SuppressException();
    return;
  }
  JS::Rooted<JS::Value> rootedExceptionValue(aCx);
  MOZ_ALWAYS_TRUE(ToJSValue(aCx, std::move(aRv), &rootedExceptionValue));

  InitNavigationResult(
      aResult, Promise::Reject(global, rootedExceptionValue, IgnoreErrors()),
      Promise::Reject(global, rootedExceptionValue, IgnoreErrors()));
}

void Navigation::SetEarlyStateErrorResult(JSContext* aCx,
                                          NavigationResult& aResult,
                                          const nsACString& aMessage) const {
  ErrorResult rv;
  rv.ThrowInvalidStateError(aMessage);
  SetEarlyErrorResult(aCx, aResult, std::move(rv));
}

bool Navigation::CheckIfDocumentIsFullyActiveAndMaybeSetEarlyErrorResult(
    JSContext* aCx, const Document* aDocument,
    NavigationResult& aResult) const {
  if (!aDocument || !aDocument->IsFullyActive()) {
    ErrorResult rv;
    rv.ThrowInvalidStateError("Document is not fully active");
    SetEarlyErrorResult(aCx, aResult, std::move(rv));
    return false;
  }
  return true;
}

bool Navigation::CheckDocumentUnloadCounterAndMaybeSetEarlyErrorResult(
    JSContext* aCx, const Document* aDocument,
    NavigationResult& aResult) const {
  if (!aDocument || aDocument->ShouldIgnoreOpens()) {
    ErrorResult rv;
    rv.ThrowInvalidStateError("Document is unloading");
    SetEarlyErrorResult(aCx, aResult, std::move(rv));
    return false;
  }
  return true;
}

already_AddRefed<nsIStructuredCloneContainer>
Navigation::CreateSerializedStateAndMaybeSetEarlyErrorResult(
    JSContext* aCx, const JS::Value& aState, NavigationResult& aResult) const {
  JS::Rooted<JS::Value> state(aCx, aState);
  RefPtr global = GetRelevantGlobal();
  MOZ_DIAGNOSTIC_ASSERT(global);

  RefPtr<nsIStructuredCloneContainer> serializedState =
      new nsStructuredCloneContainer();
  const nsresult rv = serializedState->InitFromJSVal(state, aCx);
  if (NS_FAILED(rv)) {
    JS::Rooted<JS::Value> exception(aCx);
    if (JS_GetPendingException(aCx, &exception)) {
      JS_ClearPendingException(aCx);
      InitNavigationResult(aResult,
                           Promise::Reject(global, exception, IgnoreErrors()),
                           Promise::Reject(global, exception, IgnoreErrors()));
      return nullptr;
    }
    SetEarlyErrorResult(aCx, aResult, ErrorResult(rv));
    return nullptr;
  }
  return serializedState.forget();
}

// https://html.spec.whatwg.org/#dom-navigation-navigate
void Navigation::Navigate(JSContext* aCx, const nsAString& aUrl,
                          const NavigationNavigateOptions& aOptions,
                          NavigationResult& aResult) {
  LOG_FMTI("Called navigation.navigate() with url = {}",
           NS_ConvertUTF16toUTF8(aUrl));
  // 4. Let document be this's relevant global object's associated Document.
  const RefPtr<Document> document = GetAssociatedDocument();
  if (!document) {
    return;
  }

  // 1. Let urlRecord be the result of parsing a URL given url, relative to
  //    this's relevant settings object.
  RefPtr<nsIURI> urlRecord;
  nsresult res = NS_NewURI(getter_AddRefs(urlRecord), aUrl, nullptr,
                           document->GetDocBaseURI());
  if (NS_FAILED(res)) {
    // 2. If urlRecord is failure, then return an early error result for a
    //    "SyntaxError" DOMException.
    ErrorResult rv;
    rv.ThrowSyntaxError("URL given to navigate() is invalid");
    SetEarlyErrorResult(aCx, aResult, std::move(rv));
    return;
  }

  // 3. If urlRecord's scheme is "javascript", then return an early error result
  //    for a "NotSupportedError" DOMException.
  if (urlRecord->SchemeIs("javascript")) {
    ErrorResult rv;
    rv.ThrowNotSupportedError("The javascript: protocol is not supported");
    SetEarlyErrorResult(aCx, aResult, std::move(rv));
    return;
  }

  // 5. If options["history"] is "push", and the navigation must be a replace
  //    given urlRecord and document, then return an early error result for a
  //    "NotSupportedError" DOMException.
  if (aOptions.mHistory == NavigationHistoryBehavior::Push &&
      nsContentUtils::NavigationMustBeAReplace(*urlRecord, *document)) {
    ErrorResult rv;
    rv.ThrowNotSupportedError("Navigation must be a replace navigation");
    SetEarlyErrorResult(aCx, aResult, std::move(rv));
    return;
  }

  // 6. Let state be options["state"], if it exists; otherwise, undefined.
  // 7. Let serializedState be StructuredSerializeForStorage(state). If this
  //    throws an exception, then return an early error result for that
  //    exception.
  nsCOMPtr<nsIStructuredCloneContainer> serializedState =
      CreateSerializedStateAndMaybeSetEarlyErrorResult(aCx, aOptions.mState,
                                                       aResult);
  if (!serializedState) {
    return;
  }

  // 8. If document is not fully active, then return an early error result for
  //    an "InvalidStateError" DOMException.
  if (!CheckIfDocumentIsFullyActiveAndMaybeSetEarlyErrorResult(aCx, document,
                                                               aResult)) {
    return;
  }

  // 9. If document's unload counter is greater than 0, then return an early
  //    error result for an "InvalidStateError" DOMException.
  if (!CheckDocumentUnloadCounterAndMaybeSetEarlyErrorResult(aCx, document,
                                                             aResult)) {
    return;
  }

  // 10. Let info be options["info"], if it exists; otherwise, undefined.
  // 11. Let apiMethodTracker be the result of setting up a navigate API method
  //     tracker for this given info and serializedState.
  JS::Rooted<JS::Value> info(aCx, aOptions.mInfo);
  RefPtr<NavigationAPIMethodTracker> apiMethodTracker =
      SetUpNavigateReloadAPIMethodTracker(info, serializedState);
  MOZ_ASSERT(apiMethodTracker);

  // 12. Navigate document's node navigable to urlRecord using document, with
  //     historyHandling set to options["history"], navigationAPIState set to
  //     serializedState, and navigationAPIMethodTracker set to
  //     apiMethodTracker.

  RefPtr bc = document->GetBrowsingContext();
  MOZ_DIAGNOSTIC_ASSERT(bc);
  nsCOMPtr principal = document->NodePrincipal();
  bc->Navigate(urlRecord, document, *principal,
               /* per spec, error handling defaults to false */ IgnoreErrors(),
               aOptions.mHistory, /* aNeedsCompletelyLoadedDocument */ false,
               serializedState, apiMethodTracker);

  // 13. Return a navigation API method tracker-derived result for
  //     apiMethodTracker.
  apiMethodTracker->CreateResult(aCx, aResult);
}

// https://html.spec.whatwg.org/#performing-a-navigation-api-traversal
void Navigation::PerformNavigationTraversal(JSContext* aCx, const nsID& aKey,
                                            const NavigationOptions& aOptions,
                                            NavigationResult& aResult) {
  LOG_FMTV("traverse navigation to {}", aKey.ToString().get());
  // 1. Let document be navigation's relevant global object's associated
  //    Document.
  const Document* document = GetAssociatedDocument();

  // 2. If document is not fully active, then return an early error result for
  //    an "InvalidStateError" DOMException.
  if (!document || !document->IsFullyActive()) {
    SetEarlyStateErrorResult(aCx, aResult, "Document is not fully active"_ns);
    return;
  }

  // 3. If document's unload counter is greater than 0, then return an early
  //    error result for an "InvalidStateError" DOMException.
  if (document->ShouldIgnoreOpens()) {
    SetEarlyStateErrorResult(aCx, aResult, "Document is unloading"_ns);
    return;
  }

  // 4. Let current be the current entry of navigation.
  RefPtr<NavigationHistoryEntry> current = GetCurrentEntry();
  if (!current) {
    SetEarlyStateErrorResult(aCx, aResult,
                             "No current navigation history entry"_ns);
    return;
  }

  // 5. If key equals current's session history entry's navigation API key, then
  //    return «[ "committed" → a promise resolved with current, "finished" → a
  //    promise resolved with current ]».
  RefPtr global = GetRelevantGlobal();
  if (!global) {
    return;
  }

  if (current->Key() == aKey) {
    InitNavigationResult(aResult,
                         Promise::Resolve(global, current, IgnoreErrors()),
                         Promise::Resolve(global, current, IgnoreErrors()));
    return;
  }

  // 6. If navigation's upcoming traverse API method trackers[key] exists, then
  //    return a navigation API method tracker-derived result for navigation's
  //    upcoming traverse API method trackers[key].
  if (auto maybeTracker =
          mUpcomingTraverseAPIMethodTrackers.MaybeGet(aKey).valueOr(nullptr)) {
    maybeTracker->CreateResult(aCx, aResult);
    return;
  }

  // 7. Let info be options["info"], if it exists; otherwise, undefined.
  JS::Rooted<JS::Value> info(aCx, aOptions.mInfo);

  // 8. Let apiMethodTracker be the result of adding an upcoming traverse API
  //    method tracker for navigation given key and info.
  RefPtr apiMethodTracker = AddUpcomingTraverseAPIMethodTracker(aKey, info);

  // 9. Let navigable be document's node navigable.
  RefPtr<BrowsingContext> navigable = document->GetBrowsingContext();

  // 10. Let traversable be navigable's traversable navigable.
  RefPtr<BrowsingContext> traversable = navigable->Top();
  // 11. Let sourceSnapshotParams be the result of snapshotting source snapshot
  //     params given document.

  // 13. Return a navigation API method tracker-derived result for
  //     apiMethodTracker.
  apiMethodTracker->CreateResult(aCx, aResult);

  // 12. Append the following session history traversal steps to traversable:
  auto* childSHistory = traversable->GetChildSessionHistory();
  auto performNavigationTraversalSteps = [apiMethodTracker](nsresult aResult) {
    // 12.3 If targetSHE is navigable's active session history entry,
    //      then abort these steps.
    if (NS_SUCCEEDED(aResult)) {
      return;
    }

    // See https://github.com/whatwg/html/issues/12176
    if (apiMethodTracker->IsHandled()) {
      return;
    }

    AutoJSAPI jsapi;
    if (NS_WARN_IF(!jsapi.Init(
            apiMethodTracker->mNavigationObject->GetParentObject()))) {
      return;
    }

    ErrorResult rv;

    switch (aResult) {
      case NS_ERROR_DOM_INVALID_STATE_ERR:
        // 12.2 Let targetSHE be the session history entry in navigableSHEs
        //      whose navigation API key is key. If no such entry exists,
        //      then:
        rv.ThrowInvalidStateError("No such entry with key found");
        break;
      case NS_ERROR_DOM_ABORT_ERR:
        // 12.5 If result is "canceled-by-beforeunload", then queue a global
        //      task on the navigation and traversal task source given
        //      navigation's relevant global object to reject the finished
        //      promise for apiMethodTracker with a new "AbortError"
        //      DOMException
        // created in navigation's relevant realm.
        rv.ThrowAbortError("Navigation was canceled");
        break;
      case NS_ERROR_DOM_SECURITY_ERR:
        // 12.6 If result is "initiator-disallowed", then queue a global task on
        //      the navigation and traversal task source given navigation's
        //      relevant global object to reject the finished promise for
        //      apiMethodTracker with a new "SecurityError" DOMException
        //      created in navigation's relevant realm.
        rv.ThrowSecurityError("Navigation was not allowed");
        break;
      default:
        MOZ_DIAGNOSTIC_ASSERT(false, "Unexpected result");
        rv.ThrowInvalidStateError("Unexpected result");
        break;
    }
    JS::Rooted<JS::Value> rootedExceptionValue(jsapi.cx());
    MOZ_ALWAYS_TRUE(
        ToJSValue(jsapi.cx(), std::move(rv), &rootedExceptionValue));
    apiMethodTracker->RejectFinishedPromise(rootedExceptionValue);
  };

  // 12.4 Let result be the result of applying the traverse history step given
  //      by targetSHE's step to traversable, given sourceSnapshotParams,
  //      navigable, and "none".
  childSHistory->AsyncGo(aKey, navigable, /*aRequireUserInteraction=*/false,
                         /*aUserActivation=*/false,
                         /*aCheckForCancelation=*/true,
                         performNavigationTraversalSteps);
}

// https://html.spec.whatwg.org/#dom-navigation-reload
void Navigation::Reload(JSContext* aCx, const NavigationReloadOptions& aOptions,
                        NavigationResult& aResult) {
  LOG_FMTI("Called navigation.reload()");
  // 1. Let document be this's relevant global object's associated Document.
  const RefPtr<Document> document = GetAssociatedDocument();
  if (!document) {
    return;
  }

  // 2. Let serializedState be StructuredSerializeForStorage(undefined).
  RefPtr<nsIStructuredCloneContainer> serializedState;

  // 3. If options["state"] exists, then set serializedState to
  //    StructuredSerializeForStorage(options["state"]). If this throws an
  //    exception, then return an early error result for that exception.
  if (!aOptions.mState.isUndefined()) {
    serializedState = CreateSerializedStateAndMaybeSetEarlyErrorResult(
        aCx, aOptions.mState, aResult);
    if (!serializedState) {
      return;
    }
  } else {
    // 4. Otherwise:
    // 4.1 Let current be the current entry of this.
    // 4.2 If current is not null, then set serializedState to current's
    //     session history entry's navigation API state.
    if (RefPtr<NavigationHistoryEntry> current = GetCurrentEntry()) {
      serializedState = current->GetNavigationAPIState();
    }
  }
  // 5. If document is not fully active, then return an early error result for
  //    an "InvalidStateError" DOMException.
  if (!CheckIfDocumentIsFullyActiveAndMaybeSetEarlyErrorResult(aCx, document,
                                                               aResult)) {
    return;
  }

  // 6. If document's unload counter is greater than 0, then return an early
  //    error result for an "InvalidStateError" DOMException.
  if (!CheckDocumentUnloadCounterAndMaybeSetEarlyErrorResult(aCx, document,
                                                             aResult)) {
    return;
  }

  // 7. Let info be options["info"], if it exists; otherwise, undefined.
  JS::Rooted<JS::Value> info(aCx, aOptions.mInfo);
  // 8. Let apiMethodTracker be the result of setting up a reload API method
  //    tracker for this given info and serializedState.
  RefPtr<NavigationAPIMethodTracker> apiMethodTracker =
      SetUpNavigateReloadAPIMethodTracker(info, serializedState);
  MOZ_ASSERT(apiMethodTracker);
  // 9. Reload document's node navigable with navigationAPIState set to
  //    serializedState and navigationAPIMethodTracker set to apiMethodTracker.
  RefPtr docShell = nsDocShell::Cast(document->GetDocShell());
  MOZ_ASSERT(docShell);
  docShell->ReloadNavigable(Some(WrapNotNullUnchecked(aCx)),
                            nsIWebNavigation::LOAD_FLAGS_NONE, serializedState,
                            UserNavigationInvolvement::None, apiMethodTracker);

  // 10. Return a navigation API method tracker-derived result for
  //     apiMethodTracker.
  apiMethodTracker->CreateResult(aCx, aResult);
}

// https://html.spec.whatwg.org/#dom-navigation-traverseto
void Navigation::TraverseTo(JSContext* aCx, const nsAString& aKey,
                            const NavigationOptions& aOptions,
                            NavigationResult& aResult) {
  LOG_FMTI("Called navigation.traverseTo() with key = {}",
           NS_ConvertUTF16toUTF8(aKey).get());

  // 1. If this's current entry index is −1, then return an early error result
  //    for an "InvalidStateError" DOMException.
  if (mCurrentEntryIndex.isNothing()) {
    ErrorResult rv;
    rv.ThrowInvalidStateError("Current entry index is unexpectedly -1");
    SetEarlyErrorResult(aCx, aResult, std::move(rv));
    return;
  }

  // 2. If this's entry list does not contain a NavigationHistoryEntry whose
  //    session history entry's navigation API key equals key, then return an
  //    early error result for an "InvalidStateError" DOMException.
  nsID key{};
  const bool foundKey =
      key.Parse(NS_ConvertUTF16toUTF8(aKey)) &&
      std::find_if(mEntries.begin(), mEntries.end(), [&](const auto& aEntry) {
        return aEntry->Key() == key;
      }) != mEntries.end();
  if (!foundKey) {
    ErrorResult rv;
    rv.ThrowInvalidStateError("Session history entry key does not exist");
    SetEarlyErrorResult(aCx, aResult, std::move(rv));
    return;
  }

  // 3. Return the result of performing a navigation API traversal given this,
  //    key, and options.
  PerformNavigationTraversal(aCx, key, aOptions, aResult);
}

// https://html.spec.whatwg.org/#dom-navigation-back
void Navigation::Back(JSContext* aCx, const NavigationOptions& aOptions,
                      NavigationResult& aResult) {
  LOG_FMTI("Called navigation.back()");
  // 1. If this's current entry index is −1 or 0, then return an early error
  //    result for an "InvalidStateError" DOMException.
  if (mCurrentEntryIndex.isNothing() || *mCurrentEntryIndex == 0 ||
      *mCurrentEntryIndex > mEntries.Length() - 1) {
    SetEarlyStateErrorResult(aCx, aResult,
                             "Current entry index is unexpectedly -1 or 0"_ns);
    return;
  }

  // 2. Let key be this's entry list[this's current entry index − 1]'s session
  //    history entry's navigation API key.
  MOZ_DIAGNOSTIC_ASSERT(mEntries[*mCurrentEntryIndex - 1]);
  const nsID key = mEntries[*mCurrentEntryIndex - 1]->Key();

  // 3. Return the result of performing a navigation API traversal given this,
  //    key, and options.
  PerformNavigationTraversal(aCx, key, aOptions, aResult);
}

// https://html.spec.whatwg.org/#dom-navigation-forward
void Navigation::Forward(JSContext* aCx, const NavigationOptions& aOptions,
                         NavigationResult& aResult) {
  LOG_FMTI("Called navigation.forward()");

  // 1. If this's current entry index is −1 or is equal to this's entry list's
  //    size − 1, then return an early error result for an "InvalidStateError"
  //    DOMException.
  if (mCurrentEntryIndex.isNothing() ||
      *mCurrentEntryIndex >= mEntries.Length() - 1) {
    ErrorResult rv;
    rv.ThrowInvalidStateError(
        "Current entry index is unexpectedly -1 or entry list's size - 1");
    SetEarlyErrorResult(aCx, aResult, std::move(rv));
    return;
  }

  // 2. Let key be this's entry list[this's current entry index + 1]'s session
  //    history entry's navigation API key.
  MOZ_ASSERT(mEntries[*mCurrentEntryIndex + 1]);
  const nsID& key = mEntries[*mCurrentEntryIndex + 1]->Key();

  // 3. Return the result of performing a navigation API traversal given this,
  //    key, and options.
  PerformNavigationTraversal(aCx, key, aOptions, aResult);
}

namespace {

void LogEntry(NavigationHistoryEntry* aEntry, uint64_t aIndex, uint64_t aTotal,
              bool aIsCurrent) {
  if (!aEntry) {
    MOZ_LOG(gNavigationAPILog, LogLevel::Debug,
            (" +- %d NHEntry null\n", int(aIndex)));
    return;
  }

  nsString key, id;
  aEntry->GetKey(key);
  aEntry->GetId(id);
  MOZ_LOG(gNavigationAPILog, LogLevel::Debug,
          ("%s+- %d NHEntry %p %s %s\n", aIsCurrent ? ">" : " ", int(aIndex),
           aEntry, NS_ConvertUTF16toUTF8(key).get(),
           NS_ConvertUTF16toUTF8(id).get()));

  nsAutoString url;
  aEntry->GetUrl(url);
  MOZ_LOG(gNavigationAPILog, LogLevel::Debug,
          ("   URL = %s\n", NS_ConvertUTF16toUTF8(url).get()));
}

}  // namespace

// https://html.spec.whatwg.org/#fire-a-traverse-navigate-event
Navigation::FinalStatus Navigation::FireTraverseNavigateEvent(
    JSContext* aCx, nsDocShellLoadState* aLoadState,
    Maybe<UserNavigationInvolvement> aUserInvolvement) {
  // aDestinationSessionHistoryInfo corresponds to
  const SessionHistoryInfo& destinationSessionHistoryInfo =
      aLoadState->GetLoadingSessionHistoryInfo()->mInfo;
  // https://html.spec.whatwg.org/#fire-navigate-traverse-destinationshe

  // To not unnecessarily create an event that's never used, step 1 and step 2
  // in #fire-a-traverse-navigate-event have been moved to after step 25 in
  // #inner-navigate-event-firing-algorithm in our implementation.

  // Work around for https://github.com/whatwg/html/issues/11802
  InnerInformAboutAbortingNavigation(aCx);

  // Step 5
  RefPtr<NavigationHistoryEntry> destinationNHE =
      FindNavigationHistoryEntry(destinationSessionHistoryInfo);

  // Step 6.2 and step 7.2
  RefPtr<nsIStructuredCloneContainer> state =
      destinationNHE ? destinationNHE->GetNavigationAPIState() : nullptr;

  // Step 8
  bool isSameDocument =
      ToMaybeRef(
          nsDocShell::Cast(nsContentUtils::GetDocShellForEventTarget(this)))
          .andThen([](auto& aDocShell) {
            return ToMaybeRef(aDocShell.GetActiveSessionHistoryInfo());
          })
          .map([&destinationSessionHistoryInfo](auto& aSessionHistoryInfo) {
            return destinationSessionHistoryInfo.SharesDocumentWith(
                aSessionHistoryInfo);
          })
          .valueOr(false);

  // Step 3, step 4, step 6.1, and step 7.1.
  RefPtr<NavigationDestination> destination =
      MakeAndAddRef<NavigationDestination>(
          GetRelevantGlobal(), destinationSessionHistoryInfo.GetURI(),
          destinationNHE, state, isSameDocument);

  // Step 9
  return InnerFireNavigateEvent(
      aCx, NavigationType::Traverse, destination,
      aUserInvolvement.valueOr(UserNavigationInvolvement::None),
      /* aSourceElement */ nullptr,
      /* aFormDataEntryList*/ nullptr,
      /* aClassicHistoryAPIState */ nullptr,
      /* aDownloadRequestFilename */ VoidString(),
      /* aOngoingAPIMethodTracker */ nullptr, aLoadState);
}

// https://html.spec.whatwg.org/#fire-a-push/replace/reload-navigate-event
bool Navigation::FirePushReplaceReloadNavigateEvent(
    JSContext* aCx, NavigationType aNavigationType, nsIURI* aDestinationURL,
    bool aIsSameDocument, Maybe<UserNavigationInvolvement> aUserInvolvement,
    Element* aSourceElement, FormData* aFormDataEntryList,
    nsIStructuredCloneContainer* aNavigationAPIState,
    nsIStructuredCloneContainer* aClassicHistoryAPIState,
    NavigationAPIMethodTracker* aApiMethodTrackerForNavigateOrReload) {
  // 1. Let document be navigation's relevant global object's associated
  //    Document.
  RefPtr document = GetAssociatedDocument();

  // 2. Inform the navigation API about aborting navigation in document's node
  //    navigable.
  InnerInformAboutAbortingNavigation(aCx);

  // 3. If navigation has entries and events disabled, and
  //    apiMethodTrackerForNavigateOrReload is not null:
  if (HasEntriesAndEventsDisabled() && aApiMethodTrackerForNavigateOrReload) {
    // 3.1. Set apiMethodTrackerForNavigateOrReload's pending to false.
    aApiMethodTrackerForNavigateOrReload->MarkAsNotPending();
    aApiMethodTrackerForNavigateOrReload = nullptr;
  }

  // 4. If document is not fully active, then return false.
  if (!document || !document->IsFullyActive()) {
    return false;
  }

  // To not unnecessarily create an event that's never used, step 5 and step 6
  // in #fire-a-push/replace/reload-navigate-event have been moved to after step
  // 23 in #inner-navigate-event-firing-algorithm in our implementation.

  // Step 7 to step 11
  RefPtr<NavigationDestination> destination =
      MakeAndAddRef<NavigationDestination>(GetRelevantGlobal(), aDestinationURL,
                                           /* aEntry */ nullptr,
                                           /* aState */ aNavigationAPIState,
                                           aIsSameDocument);

  // Step 8
  return InnerFireNavigateEvent(
             aCx, aNavigationType, destination,
             aUserInvolvement.valueOr(UserNavigationInvolvement::None),
             aSourceElement, aFormDataEntryList, aClassicHistoryAPIState,
             /* aDownloadRequestFilename */ VoidString(),
             aApiMethodTrackerForNavigateOrReload) == FinalStatus::Continue;
}

// https://html.spec.whatwg.org/#fire-a-download-request-navigate-event
bool Navigation::FireDownloadRequestNavigateEvent(
    JSContext* aCx, nsIURI* aDestinationURL,
    UserNavigationInvolvement aUserInvolvement, Element* aSourceElement,
    const nsAString& aFilename) {
  // To not unnecessarily create an event that's never used, step 1 and step 2
  // in #fire-a-download-request-navigate-event have been moved to after step
  // 25 in #inner-navigate-event-firing-algorithm in our implementation.

  // Work around for https://github.com/whatwg/html/issues/11802
  InnerInformAboutAbortingNavigation(aCx);

  // Step 3 to step 7
  RefPtr<NavigationDestination> destination =
      MakeAndAddRef<NavigationDestination>(GetRelevantGlobal(), aDestinationURL,
                                           /* aEntry */ nullptr,
                                           /* aState */ nullptr,
                                           /* aIsSameDocument */ false);

  // Step 8
  return InnerFireNavigateEvent(aCx, NavigationType::Push, destination,
                                aUserInvolvement, aSourceElement,
                                /* aFormDataEntryList */ nullptr,
                                /* aClassicHistoryAPIState */ nullptr,
                                aFilename) == FinalStatus::Continue;
}

static bool HasHistoryActionActivation(
    Maybe<nsGlobalWindowInner&> aRelevantGlobalObject) {
  return aRelevantGlobalObject
      .map([](auto& aRelevantGlobalObject) {
        WindowContext* windowContext = aRelevantGlobalObject.GetWindowContext();
        return windowContext && windowContext->HasValidHistoryActivation();
      })
      .valueOr(false);
}

static void ConsumeHistoryActionUserActivation(
    Maybe<nsGlobalWindowInner&> aRelevantGlobalObject) {
  aRelevantGlobalObject.apply([](auto& aRelevantGlobalObject) {
    if (WindowContext* windowContext =
            aRelevantGlobalObject.GetWindowContext()) {
      windowContext->ConsumeHistoryActivation();
    }
  });
}

// Implementation of this will be done in Bug 1948593.
static bool HasUAVisualTransition(Maybe<Document&>) { return false; }

static bool EqualsExceptRef(nsIURI* aURI, nsIURI* aOtherURI) {
  bool equalsExceptRef = false;
  return aURI && aOtherURI &&
         NS_SUCCEEDED(aURI->EqualsExceptRef(aOtherURI, &equalsExceptRef)) &&
         equalsExceptRef;
}

static bool HasRef(nsIURI* aURI) {
  bool hasRef = false;
  aURI->GetHasRef(&hasRef);
  return hasRef;
}

static bool HasIdenticalFragment(nsIURI* aURI, nsIURI* aOtherURI) {
  nsAutoCString ref;

  if (HasRef(aURI) != HasRef(aOtherURI)) {
    return false;
  }

  if (NS_FAILED(aURI->GetRef(ref))) {
    return false;
  }

  nsAutoCString otherRef;
  if (NS_FAILED(aOtherURI->GetRef(otherRef))) {
    return false;
  }

  return ref.Equals(otherRef);
}

nsresult Navigation::FireEvent(const nsAString& aName) {
  RefPtr<Event> event = NS_NewDOMEvent(this, nullptr, nullptr);
  // it doesn't bubble, and it isn't cancelable
  event->InitEvent(aName, false, false);
  event->SetTrusted(true);
  ErrorResult rv;
  LogEvent(event, mOngoingNavigateEvent, "Fire"_ns);
  DispatchEvent(*event, rv);
  return rv.StealNSResult();
}

static void ExtractErrorInformation(JSContext* aCx,
                                    JS::Handle<JS::Value> aError,
                                    ErrorEventInit& aErrorEventInitDict,
                                    const NavigateEvent* aEvent) {
  nsContentUtils::ExtractErrorValues(
      aCx, aError, aErrorEventInitDict.mFilename, &aErrorEventInitDict.mLineno,
      &aErrorEventInitDict.mColno, aErrorEventInitDict.mMessage);
  aErrorEventInitDict.mError = aError;
  aErrorEventInitDict.mBubbles = false;
  aErrorEventInitDict.mCancelable = false;

  if (!aErrorEventInitDict.mFilename.IsEmpty()) {
    return;
  }

  RefPtr document = aEvent->GetAssociatedDocument();
  if (!document) {
    return;
  }

  if (auto* uri = document->GetDocumentURI()) {
    uri->GetSpec(aErrorEventInitDict.mFilename);
  }
}

nsresult Navigation::FireErrorEvent(const nsAString& aName,
                                    const ErrorEventInit& aEventInitDict) {
  RefPtr<Event> event = ErrorEvent::Constructor(this, aName, aEventInitDict);
  ErrorResult rv;

  LogEvent(event, mOngoingNavigateEvent, "Fire"_ns);
  DispatchEvent(*event, rv);
  return rv.StealNSResult();
}

// https://html.spec.whatwg.org/#inner-navigate-event-firing-algorithm
Navigation::FinalStatus Navigation::InnerFireNavigateEvent(
    JSContext* aCx, NavigationType aNavigationType,
    NavigationDestination* aDestination,
    UserNavigationInvolvement aUserInvolvement, Element* aSourceElement,
    FormData* aFormDataEntryList,
    nsIStructuredCloneContainer* aClassicHistoryAPIState,
    const nsAString& aDownloadRequestFilename,
    NavigationAPIMethodTracker* aNavigationAPIMethodTracker,
    nsDocShellLoadState* aLoadState) {
  nsCOMPtr<nsIGlobalObject> globalObject = GetRelevantGlobal();
  RefPtr apiMethodTracker = aNavigationAPIMethodTracker;

  // Step 1
  if (HasEntriesAndEventsDisabled()) {
    // Step 1.1 to step 1.3
    MOZ_DIAGNOSTIC_ASSERT(!mOngoingAPIMethodTracker);
    MOZ_DIAGNOSTIC_ASSERT(mUpcomingTraverseAPIMethodTrackers.IsEmpty());
    MOZ_DIAGNOSTIC_ASSERT(!aNavigationAPIMethodTracker);

    // Step 1.5
    return FinalStatus::Continue;
  }

  RootedDictionary<NavigateEventInit> init(RootingCx());

  // Step 2
  MOZ_DIAGNOSTIC_ASSERT(!mOngoingAPIMethodTracker);

  // Step 3
  Maybe<nsID> destinationKey;
  if (auto* destinationEntry = aDestination->GetEntry()) {
    // Step 3.1
    MOZ_DIAGNOSTIC_ASSERT(!aNavigationAPIMethodTracker);
    // Step 3.2
    destinationKey.emplace(destinationEntry->Key());
    // Step 3.3
    MOZ_DIAGNOSTIC_ASSERT(!destinationKey->Equals(nsID{}));
    // Step 3.4, 3.4.2
    if (auto entry =
            mUpcomingTraverseAPIMethodTrackers.Extract(*destinationKey)) {
      // Step 3.4.1
      apiMethodTracker = std::move(*entry);
    }
  }
  // Step 4
  if (apiMethodTracker) {
    apiMethodTracker->MarkAsNotPending();
  }

  // This step is currently missing in the spec. See
  // https://github.com/whatwg/html/issues/11816
  mOngoingAPIMethodTracker = apiMethodTracker;

  // Step 5
  Maybe<BrowsingContext&> navigable =
      ToMaybeRef(GetOwnerWindow()).andThen([](auto& aWindow) {
        return ToMaybeRef(aWindow.GetBrowsingContext());
      });

  // Step 6
  Document* document =
      navigable.map([](auto& aNavigable) { return aNavigable.GetDocument(); })
          .valueOr(nullptr);

  // Step7
  init.mCanIntercept = document &&
                       document->CanRewriteURL(aDestination->GetURL(),
                                               /*aReportErrors*/ false) &&
                       (aDestination->SameDocument() ||
                        aNavigationType != NavigationType::Traverse);

  // Step 8
  bool traverseCanBeCanceled =
      navigable->IsTop() && aDestination->SameDocument() &&
      (aUserInvolvement != UserNavigationInvolvement::BrowserUI ||
       HasHistoryActionActivation(ToMaybeRef(GetOwnerWindow())));

  // Step 9
  init.mCancelable =
      aNavigationType != NavigationType::Traverse || traverseCanBeCanceled;

  // Step 11
  init.mNavigationType = aNavigationType;

  // Step 12
  init.mDestination = aDestination;

  // Step 13
  init.mDownloadRequest = aDownloadRequestFilename;

  // Step 14
  if (apiMethodTracker) {
    init.mInfo = apiMethodTracker->mInfo;
  }

  // Step 15
  init.mHasUAVisualTransition =
      HasUAVisualTransition(ToMaybeRef(GetAssociatedDocument()));

  // Step 16
  if (aSourceElement &&
      aSourceElement->NodePrincipal()->Equals(document->NodePrincipal())) {
    init.mSourceElement = aSourceElement;
  }

  // Step 17
  RefPtr<AbortController> abortController = new AbortController(globalObject);

  // Step 18
  init.mSignal = abortController->Signal();

  // step 19
  nsCOMPtr<nsIURI> currentURL = document->GetDocumentURI();

  // step 20
  init.mHashChange = !aClassicHistoryAPIState && aDestination->SameDocument() &&
                     EqualsExceptRef(aDestination->GetURL(), currentURL) &&
                     !HasIdenticalFragment(aDestination->GetURL(), currentURL);

  // Step 21
  init.mUserInitiated = aUserInvolvement != UserNavigationInvolvement::None;

  // Step 22
  init.mFormData = aFormDataEntryList;

  // Step 23
  MOZ_DIAGNOSTIC_ASSERT(!mOngoingNavigateEvent);

  // We now have everything we need to fully initialize the NavigateEvent, so
  // we'll go ahead and create it now. This is done by the spec in step 1 and
  // step 2 of #fire-a-traverse-navigate-event,
  // #fire-a-push/replace/reload-navigate-event, or
  // #fire-a-download-request-navigate-event, but there's no reason to not
  // delay it until here. This also performs step 12.
  RefPtr<NavigateEvent> event = NavigateEvent::Constructor(
      this, u"navigate"_ns, init, aClassicHistoryAPIState, abortController);
  // Here we're running #concept-event-create from https://dom.spec.whatwg.org/
  // which explicitly sets event's isTrusted attribute to true.
  event->SetTrusted(true);

  // Step 24
  mOngoingNavigateEvent = event;

  // Step 25
  mFocusChangedDuringOngoingNavigation = false;

  // Step 26
  mSuppressNormalScrollRestorationDuringOngoingNavigation = false;

  // Step 27 and step 28
  LogEvent(event, mOngoingNavigateEvent, "Fire"_ns);
  if (!DispatchEvent(*event, CallerType::NonSystem, IgnoreErrors())) {
    // Step 28.1
    if (aNavigationType == NavigationType::Traverse) {
      ConsumeHistoryActionUserActivation(ToMaybeRef(GetOwnerWindow()));
    }

    // Step 28.2
    if (!abortController->Signal()->Aborted()) {
      AbortOngoingNavigation(aCx);
    }

    // Step 28.3
    return FinalStatus::Prevent;
  }

  // Step 29
  if (event->InterceptionState() != NavigateEvent::InterceptionState::None) {
    // Step 29.1
    RefPtr<NavigationHistoryEntry> fromNHE = GetCurrentEntry();

    // Step 29.2
    MOZ_DIAGNOSTIC_ASSERT(fromNHE);

    // Step 29.3
    RefPtr<Promise> committedPromise = Promise::CreateInfallible(globalObject);
    RefPtr<Promise> finishedPromise = Promise::CreateInfallible(globalObject);
    const RefPtr<NavigationDestination> destination = event->Destination();
    mTransition = MakeAndAddRef<NavigationTransition>(
        globalObject, aNavigationType, fromNHE, destination, committedPromise,
        finishedPromise);

    // Step 29.4
    MOZ_ALWAYS_TRUE(committedPromise->SetAnyPromiseIsHandled());
    // Step 29.5
    MOZ_ALWAYS_TRUE(finishedPromise->SetAnyPromiseIsHandled());
  }

  RefPtr scope = MakeRefPtr<NavigationWaitForAllScope>(
      this, apiMethodTracker, event, aDestination, aLoadState);
  // Step 30
  if (event->NavigationPrecommitHandlerList().IsEmpty()) {
    LOG_FMTD("No precommit handlers, committing directly");
    scope->CommitNavigateEvent();
  } else {
    LOG_FMTD("Running {} precommit handlers",
             event->NavigationPrecommitHandlerList().Length());
    // Step 31.1
    RefPtr precommitController =
        new NavigationPrecommitController(event, globalObject);
    // Step 31.2
    nsTArray<RefPtr<Promise>> precommitPromiseList;
    // Step 31.3
    for (auto& handler : event->NavigationPrecommitHandlerList().Clone()) {
      // Step 31.3.1
      RefPtr promise = MOZ_KnownLive(handler)->Call(*precommitController);
      if (promise) {
        precommitPromiseList.AppendElement(promise);
      }
    }
    // Step 31.4
    Promise::WaitForAll(
        globalObject, precommitPromiseList,
        [weakScope = WeakPtr(scope)](const Span<JS::Heap<JS::Value>>&)
            MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA {
              // If weakScope is null we've been cycle collected
              if (!weakScope) {
                return;
              }
              RefPtr scope = weakScope.get();
              scope->CommitNavigateEvent();
            },
        [weakScope = WeakPtr(scope)](JS::Handle<JS::Value> aRejectionReason)
            MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA {
              // If weakScope is null we've been cycle collected
              if (!weakScope) {
                return;
              }
              RefPtr scope = weakScope.get();
              scope->ProcessNavigateEventHandlerFailure(aRejectionReason);
            },
        scope);
  }
  // Step 32 and 33
  return event->InterceptionState() == NavigateEvent::InterceptionState::None
             ? FinalStatus::Continue
             : FinalStatus::Intercept;
}

NavigationHistoryEntry* Navigation::FindNavigationHistoryEntry(
    const SessionHistoryInfo& aSessionHistoryInfo) const {
  for (const auto& navigationHistoryEntry : mEntries) {
    if (navigationHistoryEntry->IsSameEntry(&aSessionHistoryInfo)) {
      return navigationHistoryEntry;
    }
  }

  return nullptr;
}

// https://html.spec.whatwg.org/#getting-the-navigation-api-entry-index
Maybe<size_t> Navigation::GetNavigationEntryIndex(
    const SessionHistoryInfo& aSessionHistoryInfo) const {
  size_t index = 0;
  for (const auto& navigationHistoryEntry : mEntries) {
    if (navigationHistoryEntry->IsSameEntry(&aSessionHistoryInfo)) {
      return Some(index);
    }

    index++;
  }

  return Nothing();
}

// https://html.spec.whatwg.org/#navigation-api-method-tracker-clean-up
/* static */ void Navigation::CleanUp(
    NavigationAPIMethodTracker* aNavigationAPIMethodTracker) {
  // Step 1
  RefPtr<Navigation> navigation =
      aNavigationAPIMethodTracker->mNavigationObject;

  auto needsTraverse =
      MakeScopeExit([navigation]() { navigation->UpdateNeedsTraverse(); });

  // Step 2
  if (navigation->mOngoingAPIMethodTracker == aNavigationAPIMethodTracker) {
    navigation->mOngoingAPIMethodTracker = nullptr;

    return;
  }

  // Step 3.1
  Maybe<nsID> key = aNavigationAPIMethodTracker->mKey;

  // Step 3.2
  MOZ_DIAGNOSTIC_ASSERT(key);

  // Step 3.3
  MOZ_DIAGNOSTIC_ASSERT(
      navigation->mUpcomingTraverseAPIMethodTrackers.Contains(*key));

  navigation->mUpcomingTraverseAPIMethodTrackers.Remove(*key);
}

void Navigation::SetCurrentEntryIndex(const SessionHistoryInfo* aTargetInfo) {
  mCurrentEntryIndex.reset();
  if (auto* entry = FindNavigationHistoryEntry(*aTargetInfo)) {
    MOZ_ASSERT(entry->Index() >= 0);
    mCurrentEntryIndex = Some(entry->Index());
    return;
  }

  LOG_FMTW("Session history entry did not exist");
}

// https://html.spec.whatwg.org/#inform-the-navigation-api-about-aborting-navigation
void Navigation::InnerInformAboutAbortingNavigation(JSContext* aCx) {
  // As per https://github.com/whatwg/html/issues/11579, we should abort all
  // ongoing navigate events within "inform the navigation API about aborting
  // navigation".

  while (HasOngoingNavigateEvent()) {
    AbortOngoingNavigation(aCx);
  }
}

// https://html.spec.whatwg.org/#apply-the-history-step step 8.3
void Navigation::ClearOngoingNavigationForHistoryStep() {
  if (mOngoingNavigateEvent && mOngoingNavigateEvent->InterceptionState() !=
                                   NavigateEvent::InterceptionState::None) {
    return;
  }

  // 8.3.2 Set navigation's ongoing navigate event to null.
  mOngoingNavigateEvent = nullptr;

  // 8.3.3 Set navigation's ongoing API method tracker to null.
  if (mOngoingAPIMethodTracker) {
    mOngoingAPIMethodTracker = nullptr;
    UpdateNeedsTraverse();
  }
}

// https://html.spec.whatwg.org/#abort-the-ongoing-navigation
void Navigation::AbortOngoingNavigation(JSContext* aCx,
                                        JS::Handle<JS::Value> aError) {
  // Step 1
  RefPtr<NavigateEvent> event = mOngoingNavigateEvent;

  LogEvent(event, event, "Abort"_ns);

  // Step 2
  MOZ_DIAGNOSTIC_ASSERT(event);

  // Step 3
  mFocusChangedDuringOngoingNavigation = false;

  // Step 4
  mSuppressNormalScrollRestorationDuringOngoingNavigation = false;

  JS::Rooted<JS::Value> error(aCx, aError);

  // Step 5
  if (aError.isUndefined()) {
    RefPtr<DOMException> exception =
        DOMException::Create(NS_ERROR_DOM_ABORT_ERR);
    // It's OK if this fails, it just means that we'll get an empty error
    // dictionary below.
    GetOrCreateDOMReflector(aCx, exception, &error);
  }

  // Step 6
  if (event->IsBeingDispatched()) {
    // This is a bit unusual, but we actually need to cancel even uncancelable
    // events here. This means that we can't just call preventDefault.
    event->Cancel();
  }

  // Step 7
  AbortNavigateEvent(aCx, event, error);
}

// https://html.spec.whatwg.org/#abort-a-navigateevent
void Navigation::AbortNavigateEvent(JSContext* aCx, const NavigateEvent* aEvent,
                                    JS::Handle<JS::Value> aReason) {
  // 1. Let navigation be event's relevant global object's navigation API.
  // Omitted since this is called from a Navigation object.

  // 2. Set navigation's ongoing navigate event to null.
  mOngoingNavigateEvent = nullptr;

  // 3. If navigation's ongoing API method tracker is non-null, then reject the
  //    finished promise for apiMethodTracker with reason.
  if (mOngoingAPIMethodTracker) {
    mOngoingAPIMethodTracker->RejectFinishedPromise(aReason);
  }

  // 4. Signal abort on event's abort controller given reason.
  aEvent->AbortController()->Abort(aCx, aReason);

  // 5. Let errorInfo be the result of extracting error information from reason.
  RootedDictionary<ErrorEventInit> init(aCx);
  ExtractErrorInformation(aCx, aReason, init, aEvent);

  // 6. Fire an event named navigateerror at navigation using ErrorEvent, with
  //    additional attributes initialized according to errorInfo.
  FireErrorEvent(u"navigateerror"_ns, init);

  // 7. If navigation's transition is null, then return.
  if (!mTransition) {
    return;
  }

  // 8. Reject navigation's transition's committed promise with reason.
  mTransition->Committed()->MaybeReject(aReason);
  // 9. Reject navigation's transition's finished promise with reason.
  mTransition->Finished()->MaybeReject(aReason);

  // 10. Set navigation's transition to null.
  mTransition = nullptr;
}

// https://html.spec.whatwg.org/#inform-the-navigation-api-about-child-navigable-destruction
void Navigation::InformAboutChildNavigableDestruction(JSContext* aCx) {
  // Step 3
  auto traversalAPIMethodTrackers = mUpcomingTraverseAPIMethodTrackers.Clone();

  // Step 4
  for (auto& apiMethodTracker : traversalAPIMethodTrackers.Values()) {
    ErrorResult rv;
    rv.ThrowAbortError("Navigable removed");
    JS::Rooted<JS::Value> rootedExceptionValue(aCx);
    MOZ_ALWAYS_TRUE(ToJSValue(aCx, std::move(rv), &rootedExceptionValue));
    apiMethodTracker->RejectFinishedPromise(rootedExceptionValue);
  }
}

bool Navigation::FocusedChangedDuringOngoingNavigation() const {
  return mFocusChangedDuringOngoingNavigation;
}

void Navigation::SetFocusedChangedDuringOngoingNavigation(
    bool aFocusChangedDUringOngoingNavigation) {
  mFocusChangedDuringOngoingNavigation = aFocusChangedDUringOngoingNavigation;
}

bool Navigation::HasOngoingNavigateEvent() const {
  return mOngoingNavigateEvent;
}

// The associated document of navigation's relevant global object.
Document* Navigation::GetAssociatedDocument() const {
  nsGlobalWindowInner* window = GetOwnerWindow();
  return window ? window->GetDocument() : nullptr;
}

void Navigation::UpdateNeedsTraverse() {
  nsGlobalWindowInner* innerWindow = GetOwnerWindow();
  if (!innerWindow) {
    return;
  }

  WindowContext* windowContext = innerWindow->GetWindowContext();
  if (!windowContext) {
    return;
  }

  // Since we only care about optimizing for the traversable, bail if we're not
  // the top-level context.
  if (BrowsingContext* browsingContext = innerWindow->GetBrowsingContext();
      !browsingContext || !browsingContext->IsTop()) {
    return;
  }

  // We need traverse if we have any method tracker.
  bool needsTraverse =
      mOngoingAPIMethodTracker || !mUpcomingTraverseAPIMethodTrackers.IsEmpty();

  // We need traverse if we have any event handlers.
  if (EventListenerManager* eventListenerManager =
          GetExistingListenerManager()) {
    needsTraverse = needsTraverse || eventListenerManager->HasListeners();
  }

  // Don't toggle if nothing's changed.
  if (windowContext->GetNeedsTraverse() == needsTraverse) {
    return;
  }

  (void)windowContext->SetNeedsTraverse(needsTraverse);
}

void Navigation::LogHistory() const {
  if (!MOZ_LOG_TEST(gNavigationAPILog, LogLevel::Debug)) {
    return;
  }

  MOZ_LOG(gNavigationAPILog, LogLevel::Debug,
          ("Navigation %p (current entry index: %d)\n", this,
           mCurrentEntryIndex ? int(*mCurrentEntryIndex) : -1));
  auto length = mEntries.Length();
  for (uint64_t i = 0; i < length; i++) {
    LogEntry(mEntries[i], i, length,
             mCurrentEntryIndex && i == *mCurrentEntryIndex);
  }
}

// https://html.spec.whatwg.org/#set-up-a-navigate/reload-api-method-tracker
RefPtr<NavigationAPIMethodTracker>
Navigation::SetUpNavigateReloadAPIMethodTracker(
    JS::Handle<JS::Value> aInfo,
    nsIStructuredCloneContainer* aSerializedState) {
  // To set up a navigate/reload API method tracker given a Navigation
  // navigation, a JavaScript value info, and a serialized state-or-null
  // serializedState:
  // 1. Let committedPromise and finishedPromise be new promises created in
  //    navigation's relevant realm.
  RefPtr committedPromise = Promise::CreateInfallible(GetRelevantGlobal());
  RefPtr finishedPromise = Promise::CreateInfallible(GetRelevantGlobal());
  // 2. Mark as handled finishedPromise.
  MOZ_ALWAYS_TRUE(finishedPromise->SetAnyPromiseIsHandled());

  // 3. Return a new navigation API method tracker with:
  RefPtr<NavigationAPIMethodTracker> apiMethodTracker =
      MakeAndAddRef<NavigationAPIMethodTracker>(
          this, /* aKey */ Nothing{}, aInfo, aSerializedState,
          /* aCommittedToEntry */ nullptr, committedPromise, finishedPromise,
          /* aPending */ !HasEntriesAndEventsDisabled());

  return apiMethodTracker;
}

// https://html.spec.whatwg.org/#add-an-upcoming-traverse-api-method-tracker
RefPtr<NavigationAPIMethodTracker>
Navigation::AddUpcomingTraverseAPIMethodTracker(const nsID& aKey,
                                                JS::Handle<JS::Value> aInfo) {
  // To add an upcoming traverse API method tracker given a Navigation
  // navigation, a string destinationKey, and a JavaScript value info:
  // 1. Let committedPromise and finishedPromise be new promises created in
  //    navigation's relevant realm.
  RefPtr committedPromise = Promise::CreateInfallible(GetRelevantGlobal());
  RefPtr finishedPromise = Promise::CreateInfallible(GetRelevantGlobal());

  // 2. Mark as handled finishedPromise.
  MOZ_ALWAYS_TRUE(finishedPromise->SetAnyPromiseIsHandled());

  // 3. Let apiMethodTracker be a new navigation API method tracker with:
  RefPtr<NavigationAPIMethodTracker> apiMethodTracker =
      MakeAndAddRef<NavigationAPIMethodTracker>(
          this, Some(aKey), aInfo,
          /* aSerializedState */ nullptr,
          /* aCommittedToEntry */ nullptr, committedPromise, finishedPromise,
          /* aPending */ false);

  // 4. Set navigation's upcoming traverse API method trackers[destinationKey]
  //    to apiMethodTracker.
  RefPtr methodTracker =
      mUpcomingTraverseAPIMethodTrackers.InsertOrUpdate(aKey, apiMethodTracker);

  UpdateNeedsTraverse();

  // 5. Return apiMethodTracker.
  return methodTracker;
}

// https://html.spec.whatwg.org/#fire-the-pageswap-event
void Navigation::FirePageSwapEvent(const SessionHistoryInfo* aTargetSHInfo,
                                   NavigationType aType,
                                   bool aCanHaveActivation,
                                   ViewTransition* aViewTransition) {
  // PageSwapEvent is only exposed when cross-document view transitions are
  // enabled, so there is nothing observable to dispatch otherwise.
  if (!StaticPrefs::dom_viewTransitions_cross_document_enabled()) {
    return;
  }

  nsCOMPtr<nsIGlobalObject> global = GetRelevantGlobal();
  nsPIDOMWindowInner* inner = global ? global->GetAsInnerWindow() : nullptr;
  if (!inner) {
    return;
  }

  // Steps 3-4. The callers check the step 4 conditions and pass the result as
  // aCanHaveActivation.
  RefPtr<NavigationActivation> activation;
  if (aCanHaveActivation) {
    // Step 4.1: Determine destinationEntry based on the navigation type.
    RefPtr<NavigationHistoryEntry> oldEntry = GetCurrentEntry();
    RefPtr<NavigationHistoryEntry> newEntry;
    switch (aType) {
      case NavigationType::Reload:
        newEntry = oldEntry;
        break;
      case NavigationType::Traverse:
        newEntry = aTargetSHInfo ? FindNavigationHistoryEntry(*aTargetSHInfo)
                                 : nullptr;
        break;
      case NavigationType::Push:
      case NavigationType::Replace:
        if (aTargetSHInfo) {
          newEntry =
              MakeRefPtr<NavigationHistoryEntry>(global, aTargetSHInfo, -1);
        }
        break;
    }
    // Step 4.2: Create the activation. The entry of an activation can not be
    // null. A traversal target is not in the entry list if script changed the
    // session history before the traversal completed, so then fire pageswap
    // without an activation.
    if (newEntry) {
      activation =
          MakeRefPtr<NavigationActivation>(global, newEntry, oldEntry, aType);
    }
  }

  // Step 5: Fire the event.
  RefPtr<nsGlobalWindowInner> window = nsGlobalWindowInner::Cast(inner);
  PageSwapEventInit init;
  init.mActivation = activation;
  init.mViewTransition = aViewTransition;
  RefPtr<PageSwapEvent> event =
      PageSwapEvent::Constructor(window, u"pageswap"_ns, init);
  event->SetTrusted(true);
  window->DispatchEvent(*event);
}

// https://html.spec.whatwg.org/#update-document-for-history-step-application
void Navigation::CreateNavigationActivationFrom(
    const Maybe<PreviousSessionHistoryInfo>& aPreviousEntryForActivation,
    Maybe<NavigationType> aNavigationType) {
  if (!aPreviousEntryForActivation) {
    return;
  }

  const SessionHistoryInfo* previousEntryForActivation =
      aPreviousEntryForActivation.ref().mSameOriginSessionHistoryInfo.ptrOr(
          nullptr);
  NavigationType navigationType = *aNavigationType;

  MOZ_LOG_FMT(gNavigationAPILog, LogLevel::Debug,
              "Creating NavigationActivation for from={}, type={}",
              fmt::ptr(previousEntryForActivation), navigationType);

  RefPtr<NavigationHistoryEntry> oldEntry;
  if (previousEntryForActivation) {
    // Note: we do Step 7.1 at the end of method so we can both create and
    // initialize the activation at once.

    // Step 7.2 Let previousEntryIndex be the result of getting the navigation
    // API entry index of previousEntryForActivation within navigation.
    auto possiblePreviousEntry =
        std::find_if(mEntries.begin(), mEntries.end(),
                     [previousEntryForActivation](const auto& entry) {
                       return entry->IsSameEntry(previousEntryForActivation);
                     });

    // 7.3 If previousEntryIndex is non-negative, then set activation's old
    // entry to navigation's entry list[previousEntryIndex].
    if (possiblePreviousEntry != mEntries.end()) {
      MOZ_LOG_FMT(gNavigationAPILog, LogLevel::Debug,
                  "Found previous entry at {}",
                  fmt::ptr(possiblePreviousEntry->get()));
      oldEntry = *possiblePreviousEntry;
    } else if (navigationType == NavigationType::Replace &&
               !previousEntryForActivation->IsTransient()) {
      oldEntry = MakeRefPtr<NavigationHistoryEntry>(
          GetRelevantGlobal(), previousEntryForActivation, -1);
      MOZ_LOG_FMT(gNavigationAPILog, LogLevel::Debug,
                  "Created a new entry at {}", fmt::ptr(oldEntry.get()));

    } else {
      LOG_FMTV("Didn't find previous entry id={}",
               previousEntryForActivation->NavigationId().ToString().get());
    }
  }
  // 1. If navigation's activation is null, then set navigation's
  // activation to a new NavigationActivation object in navigation's relevant
  // realm.
  // 5. Set activation's new entry to navigation's current entry.
  // 6. Set activation's navigation type to navigationType.
  RefPtr<NavigationHistoryEntry> currentEntry = GetCurrentEntry();
  if (!mActivation) {
    mActivation = MakeRefPtr<NavigationActivation>(
        GetRelevantGlobal(), currentEntry, oldEntry, navigationType);
  } else {
    mActivation->SetNewEntry(currentEntry);
    mActivation->SetNavigationType(navigationType);
    mActivation->SetOldEntry(oldEntry);
  }
}

// https://html.spec.whatwg.org/#dom-navigationprecommitcontroller-redirect
void Navigation::SetSerializedStateIntoOngoingAPIMethodTracker(
    nsIStructuredCloneContainer* aSerializedState) {
  // This is step 10.3 of NavigationPrecommitController.redirect()
  if (mOngoingAPIMethodTracker) {
    mOngoingAPIMethodTracker->SetSerializedState(aSerializedState);
  }
}

}  // namespace mozilla::dom

#undef LOG_FMTV
#undef LOG_FMTD
#undef LOG_FMTI
#undef LOG_FMTW
#undef LOG_FMTE
