/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SpeechRecognition.h"

#include <algorithm>
#include <cmath>

#include "AudioSegment.h"
#include "CubebUtils.h"
#include "MainThreadUtils.h"
#include "MediaEnginePrefs.h"
#include "SpeechRecognitionAlternative.h"
#include "SpeechRecognitionBackend.h"
#include "SpeechRecognitionModelMapping.h"
#include "SpeechRecognitionResult.h"
#include "SpeechRecognitionResultList.h"
#include "SpeechTrackListener.h"
#include "VideoUtils.h"
#include "mozilla/AbstractThread.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/MediaManager.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/dom/AudioStreamTrack.h"
#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/MediaStreamBinding.h"
#include "mozilla/dom/MediaStreamError.h"
#include "mozilla/dom/MediaStreamTrackBinding.h"
#include "mozilla/dom/Navigator.h"
#include "mozilla/dom/PermissionsPolicyUtils.h"
#include "mozilla/dom/PromiseNativeHandler.h"
#include "mozilla/dom/RootedDictionary.h"
#include "mozilla/dom/SpeechGrammar.h"
#include "mozilla/dom/SpeechRecognitionErrorEvent.h"
#include "mozilla/dom/SpeechRecognitionEvent.h"
#include "mozilla/dom/SpeechRecognitionPhrase.h"
#include "mozilla/glean/DomMediaWebspeechMetrics.h"
#include "mozilla/hwinference/PSpeechRecognitionChild.h"
#include "mozilla/intl/Locale.h"
#include "nsCOMPtr.h"
#include "nsComponentManagerUtils.h"
#include "nsContentUtils.h"
#include "nsCycleCollectionParticipant.h"
#include "nsGkAtoms.h"
#include "nsGlobalWindowInner.h"
#include "nsIContent.h"
#include "nsID.h"
#include "nsIPermissionManager.h"
#include "nsIPrincipal.h"
#include "nsPIDOMWindow.h"
#include "nsQueryObject.h"
#include "nsServiceManagerUtils.h"
#include "nsString.h"
#include "nsTHashMap.h"
#include "nsUnicharUtils.h"

// Undo the windows.h damage
#if defined(XP_WIN) && defined(GetMessage)
#  undef GetMessage
#endif

namespace mozilla {
class Promise;
};

namespace mozilla::dom {

static LazyLogModule gSpeechRecognitionLog("SpeechRecognition");

using InitFailure = glean::media_speech_recognition::InitFailureLabel;

#define LOG(...) \
  MOZ_LOG_FMT(gSpeechRecognitionLog, LogLevel::Debug, __VA_ARGS__)
#define LOGV(...) \
  MOZ_LOG_FMT(gSpeechRecognitionLog, LogLevel::Verbose, __VA_ARGS__)
#define LOGE(...) \
  MOZ_LOG_FMT(gSpeechRecognitionLog, LogLevel::Error, __VA_ARGS__)

static StaticAutoPtr<
    nsTHashMap<nsCStringHashKey, RefPtr<SpeechRecognitionInstallTransaction>>>
    sInstallTransactions;

static nsCString MakeInstallTransactionKey(
    nsPIDOMWindowInner* aWindow, const nsTArray<nsCString>& aLanguages) {
  nsCString key;
  key.AppendInt(aWindow->WindowID());
  key.Append('|');
  for (const nsCString& lang : aLanguages) {
    key.AppendInt(static_cast<uint32_t>(lang.Length()));
    key.Append(':');
    key.Append(lang);
    key.Append(';');
  }
  return key;
}

SpeechRecognitionInstallTransaction::SpeechRecognitionInstallTransaction(
    nsCString&& aKey, const nsTArray<nsCString>& aLanguages)
    : mKey(std::move(aKey)), mLanguages(aLanguages.Clone()) {}

/* static */
already_AddRefed<SpeechRecognitionInstallTransaction>
SpeechRecognitionInstallTransaction::GetOrCreate(
    nsPIDOMWindowInner* aWindow, const nsTArray<nsCString>& aLanguages,
    Promise* aPromise, bool* aCreated) {
  AssertIsOnMainThread();
  MOZ_ASSERT(aWindow);
  MOZ_ASSERT(aPromise);
  MOZ_ASSERT(aCreated);

  if (!sInstallTransactions) {
    sInstallTransactions =
        new nsTHashMap<nsCStringHashKey,
                       RefPtr<SpeechRecognitionInstallTransaction>>();
    ClearOnShutdown(&sInstallTransactions);
  }

  nsCString key = MakeInstallTransactionKey(aWindow, aLanguages);
  if (auto entry = sInstallTransactions->Lookup(key)) {
    RefPtr<SpeechRecognitionInstallTransaction> transaction = entry.Data();
    transaction->mPromises.AppendElement(aPromise);
    *aCreated = false;
    return transaction.forget();
  }

  RefPtr<SpeechRecognitionInstallTransaction> transaction =
      new SpeechRecognitionInstallTransaction(std::move(key), aLanguages);
  transaction->mPromises.AppendElement(aPromise);
  sInstallTransactions->InsertOrUpdate(transaction->mKey, transaction);
  *aCreated = true;
  return transaction.forget();
}

void SpeechRecognitionInstallTransaction::Resolve(bool aSuccess) {
  AssertIsOnMainThread();

  nsTArray<RefPtr<Promise>> promises = std::move(mPromises);
  if (sInstallTransactions) {
    sInstallTransactions->Remove(mKey);
  }

  for (RefPtr<Promise>& promise : promises) {
    promise->MaybeResolve(aSuccess);
  }
}

NS_IMPL_CYCLE_COLLECTION_CLASS(SpeechRecognition)
NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(SpeechRecognition,
                                                DOMEventTargetHelper)
  if (tmp->mTrack) {
    tmp->mTrack->RemovePrincipalChangeObserver(tmp);
  }
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mTrack, mSpeechGrammarList, mListener,
                                  mPhrases, mRecognitionResults)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_WEAK_PTR
NS_IMPL_CYCLE_COLLECTION_UNLINK_END
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(SpeechRecognition,
                                                  DOMEventTargetHelper)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mTrack, mSpeechGrammarList, mListener,
                                    mPhrases, mRecognitionResults)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(SpeechRecognition)
NS_INTERFACE_MAP_END_INHERITING(DOMEventTargetHelper)
NS_IMPL_ADDREF_INHERITED(SpeechRecognition, DOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(SpeechRecognition, DOMEventTargetHelper)

NS_IMPL_CYCLE_COLLECTION_INHERITED(SpeechRecognition::TrackListener,
                                   DOMMediaStream::TrackListener,
                                   mSpeechRecognition)
NS_IMPL_ADDREF_INHERITED(SpeechRecognition::TrackListener,
                         DOMMediaStream::TrackListener)
NS_IMPL_RELEASE_INHERITED(SpeechRecognition::TrackListener,
                          DOMMediaStream::TrackListener)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(SpeechRecognition::TrackListener)
NS_INTERFACE_MAP_END_INHERITING(DOMMediaStream::TrackListener)

// Lifetime considerations:
// This class, like other classes interacting with MediaStreams, has a
// non-standard lifetime:
// - If script has a direct ref, SpeechRecognition stays alive, by definition.
// Depending on its state, the backend can be cleared / destroyed early or not.
// - Otherwise, if the input track's readyState is "live" and there is some
// callback registered, SpeechRecognition stays alive. Otherwise, e.g. if the
// input track is live, script has no refs, and there are no callbacks, the
// instance isn't useful and can be collected.
//
// This is implemented using the KeepAliveIfHasListenersFor mechanism from
// DOMEventTargetHelper. When recognition starts (mStarted becomes true), we
// register the relevant event types that should keep this object alive if
// listeners are present. When recognition ends (Reset is called -- directly or
// indirectly), we unregister them.
static constexpr nsStaticAtom* const kKeepAliveEventTypes[] = {
    nsGkAtoms::onstart,       nsGkAtoms::onaudiostart, nsGkAtoms::onsoundstart,
    nsGkAtoms::onspeechstart, nsGkAtoms::onspeechend,  nsGkAtoms::onsoundend,
    nsGkAtoms::onaudioend,    nsGkAtoms::onresult,     nsGkAtoms::onnomatch,
    nsGkAtoms::onerror,       nsGkAtoms::onend};

SpeechRecognition::SpeechRecognition(nsPIDOMWindowInner* aOwnerWindow)
    : DOMEventTargetHelper(aOwnerWindow),
      mStarted(false),
      mSpeechGrammarList(new SpeechGrammarList(aOwnerWindow)),
      mContinuous(false),
      mInterimResults(false),
      mMaxAlternatives(1) {
  LOG("SpeechRecognition::SpeechRecognition");

  Reset();
}

SpeechRecognition::~SpeechRecognition() {
  MOZ_ASSERT(NS_IsMainThread(), "Destructor must be on main thread");
  LOG("SpeechRecognition::~SpeechRecognition");

  // Ensure backend is properly cleaned up
  if (mBackend) {
    mBackend->Abort(TrailingEvents::Skip);
    mBackend = nullptr;
  }
  if (mTrack) {
    mTrack->RemovePrincipalChangeObserver(this);
  }
}

JSObject* SpeechRecognition::WrapObject(JSContext* aCx,
                                        JS::Handle<JSObject*> aGivenProto) {
  return SpeechRecognition_Binding::Wrap(aCx, this, aGivenProto);
}

void SpeechRecognition::DisconnectFromOwner() {
  AssertIsOnMainThread();
  if (mBackend) {
    mBackend->Abort(TrailingEvents::Skip);
    mBackend = nullptr;
  }
  Reset();
  DOMEventTargetHelper::DisconnectFromOwner();
}

already_AddRefed<SpeechRecognition> SpeechRecognition::Constructor(
    const GlobalObject& aGlobal, ErrorResult& aRv) {
  nsCOMPtr<nsPIDOMWindowInner> win = do_QueryInterface(aGlobal.GetAsSupports());
  if (!win) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  RefPtr<SpeechRecognition> object = new SpeechRecognition(win);
  return object.forget();
}

// The media.speech_recognition.error label for an error code.
static glean::media_speech_recognition::ErrorLabel ErrorCodeLabel(
    SpeechRecognitionErrorCode aCode) {
  using Label = glean::media_speech_recognition::ErrorLabel;
  switch (aCode) {
    case SpeechRecognitionErrorCode::No_speech:
      return Label::eNoSpeech;
    case SpeechRecognitionErrorCode::Aborted:
      return Label::eAborted;
    case SpeechRecognitionErrorCode::Audio_capture:
      return Label::eAudioCapture;
    case SpeechRecognitionErrorCode::Network:
      return Label::eNetwork;
    case SpeechRecognitionErrorCode::Not_allowed:
      return Label::eNotAllowed;
    case SpeechRecognitionErrorCode::Service_not_allowed:
      return Label::eServiceNotAllowed;
    case SpeechRecognitionErrorCode::Bad_grammar:
      return Label::eBadGrammar;
    case SpeechRecognitionErrorCode::Language_not_supported:
      return Label::eLanguageNotSupported;
    case SpeechRecognitionErrorCode::Phrases_not_supported:
      return Label::ePhrasesNotSupported;
    default:
      MOZ_ASSERT_UNREACHABLE(
          "Unhandled SpeechRecognitionErrorCode, add a label for it in "
          "metrics.yaml");
      return Label::e__Other__;
  }
}

void SpeechRecognition::RecordSessionEnded() {
  AssertIsOnMainThread();
  MOZ_ASSERT(mStarted);

  glean::media_speech_recognition::SessionEndedExtra extra;
  if (mSessionError) {
    extra.outcome.emplace("error"_ns);
    // Spelled like the media.speech_recognition.error labels, so the two
    // always agree.
    nsAutoCString errorCode(GetEnumString(*mSessionError));
    errorCode.ReplaceChar('-', '_');
    extra.errorCode.emplace(errorCode);
  } else if (mAborting) {
    extra.outcome.emplace("aborted"_ns);
    extra.errorCode.emplace(EmptyCString());
  } else if (mStopping) {
    extra.outcome.emplace("stopped"_ns);
    extra.errorCode.emplace(EmptyCString());
  } else {
    extra.outcome.emplace("discarded"_ns);
    extra.errorCode.emplace(EmptyCString());
  }
  if (!mSessionStartTime.IsNull()) {
    extra.duration.emplace(static_cast<uint32_t>(
        (TimeStamp::Now() - mSessionStartTime).ToMilliseconds()));
  }
  extra.sessionId.emplace(mSessionId);
  if (mResultLatencySampleCount) {
    glean::media_speech_recognition::result_latency.AccumulateRawDuration(
        mResultLatencyTotal.MultDouble(1.0 / mResultLatencySampleCount));
  }
  glean::media_speech_recognition::session_ended.Record(Some(std::move(extra)));
}

void SpeechRecognition::Reset() {
  MOZ_ASSERT(NS_IsMainThread(), "Reset must be on main thread");
  if (mStarted) {
    RecordSessionEnded();
    for (nsStaticAtom* atom : kKeepAliveEventTypes) {
      IgnoreKeepAliveIfHasListenersFor(atom);
    }
  }
  mStarted = false;
  mStopping = false;
  mAborting = false;
  mBackendListening = false;
  mStartDispatched = false;
  mAwaitingModelInstall = false;
  // A track obtained via our own getUserMedia() call (the microphone path)
  // has nobody else to stop it; an explicitly-passed track is the caller's
  // to manage.
  if (mTrack) {
    mTrack->RemovePrincipalChangeObserver(this);
    if (mTrackIsOwned) {
      mTrack->Stop();
    }
  }
  mTrack = nullptr;
  mTrackIsOwned = false;
  // The microphone path (Start() with no explicit track) registers mListener
  // on mStream; it must be unregistered before being cleared (see
  // DOMMediaStream::TrackListener). Without this, mListener/mStream survive a
  // stop()/abort() and a later Start() hits MOZ_ASSERT(!mListener).
  if (mStream && mListener) {
    mStream->UnregisterTrackListener(mListener);
  }
  mListener = nullptr;
  mStream = nullptr;
  mRecognitionResults.Clear();
}

void SpeechRecognition::ResetAndEnd() {
  Reset();
  if (mPerf.mStop) {
    mPerf.mFinalization = Some(TimeStamp::Now() - *mPerf.mStop);
  }
  DispatchTrustedEvent(u"end"_ns);
}

void SpeechRecognition::PostResetAndEnd() {
  AssertIsOnMainThread();
  RefPtr<SpeechRecognition> self = this;
  NS_DispatchToMainThread(NS_NewRunnableFunction(
      "SpeechRecognition::PostResetAndEnd", [self = std::move(self)]() {
        // Don't end a session that started since this was queued: an "end"
        // handler can call start().
        if (self->mBackend) {
          return;
        }
        // Reset() cleared [[started]], so this session has already ended. A
        // newer session that both started and finished in between gets its
        // "end" from here, and its own runnable then no-ops - exactly one
        // "end" either way.
        if (!self->mStarted) {
          return;
        }
        self->ResetAndEnd();
      }));
}

void SpeechRecognition::MaybeDispatchStart() {
  AssertIsOnMainThread();
  if (mStartDispatched || !mStarted) {
    return;
  }
  if (!mBackendListening || !mTrack) {
    return;
  }
  mStartDispatched = true;
  DispatchTrustedEvent(u"start"_ns);
}

void SpeechRecognition::NotifyBackendListening() {
  AssertIsOnMainThread();
  mBackendListening = true;
  mPerf.mEngineReady = Some(TimeStamp::Now() - mPerf.mStart);
  MaybeDispatchStart();
}

NS_IMETHODIMP
SpeechRecognition::StartRecording(RefPtr<AudioStreamTrack>& aTrack) {
  AssertIsOnMainThread();
  MOZ_ASSERT(!aTrack->Ended());
  MOZ_ASSERT(mBackend);

  mTrack = aTrack;
  mBackend->AttachToTrack(aTrack);
  PrincipalChanged(mTrack);
  mTrack->AddPrincipalChangeObserver(this);
  MaybeDispatchStart();

  return NS_OK;
}

already_AddRefed<SpeechGrammarList> SpeechRecognition::Grammars() const {
  RefPtr<SpeechGrammarList> speechGrammarList = mSpeechGrammarList;
  return speechGrammarList.forget();
}

void SpeechRecognition::SetGrammars(SpeechGrammarList& aArg) {
  mSpeechGrammarList = &aArg;
}

void SpeechRecognition::GetLang(nsString& aRetVal) const { aRetVal = mLang; }

void SpeechRecognition::SetLang(const nsAString& aArg) { mLang = aArg; }

bool SpeechRecognition::GetContinuous(ErrorResult& aRv) const {
  return mContinuous;
}

void SpeechRecognition::SetContinuous(bool aArg, ErrorResult& aRv) {
  mContinuous = aArg;
}

bool SpeechRecognition::InterimResults() const { return mInterimResults; }

void SpeechRecognition::SetInterimResults(bool aArg) { mInterimResults = aArg; }

uint32_t SpeechRecognition::MaxAlternatives() const { return mMaxAlternatives; }

void SpeechRecognition::SetMaxAlternatives(uint32_t aArg) {
  mMaxAlternatives = aArg;
}

static bool ValidateBCP47Language(const nsACString& aLang, ErrorResult& aRv) {
  Span<const char> langSpan(aLang.BeginReading(), aLang.Length());

  // Empty strings are not valid BCP47 language tags
  if (langSpan.IsEmpty()) {
    aRv.ThrowSyntaxError("Invalid BCP47 language tag");
    return false;
  }

  intl::Locale locale;
  auto result = intl::LocaleParser::TryParse(langSpan, locale);

  if (result.isErr()) {
    aRv.ThrowSyntaxError("Invalid BCP47 language tag");
    return false;
  }

  return true;
}

bool SpeechRecognition::ProcessLocally() const { return mProcessLocally; }

void SpeechRecognition::SetProcessLocally(bool aProcessLocally) {
  mProcessLocally = aProcessLocally;
}

bool SpeechRecognition::UnspokenPunctuation() const {
  return mUnspokenPunctuation;
}

void SpeechRecognition::SetUnspokenPunctuation(bool aUnspokenPunctuation) {
  mUnspokenPunctuation = aUnspokenPunctuation;
}

void SpeechRecognition::OnSetPhrases(SpeechRecognitionPhrase& aPhrase,
                                     uint32_t aIndex, ErrorResult& aRv) {
  // Note: The spec is unclear on whether dynamic updates during recognition
  // should affect ongoing recognition. For now, the backend only gets phrases
  // at Start() time.
  mPhrases.InsertElementAt(aIndex, &aPhrase);
}

void SpeechRecognition::OnDeletePhrases(SpeechRecognitionPhrase& aPhrase,
                                        uint32_t aIndex, ErrorResult& aRv) {
  MOZ_ASSERT(mPhrases.ElementAt(aIndex) == &aPhrase);
  // Similar comment as OnSetPhrases here: changes aren't sent to the backend
  // after start().
  mPhrases.RemoveElementAt(aIndex);
}

// https://webaudio.github.io/web-speech-api/#dom-speechrecognition-available
// Runs the availability algorithm:
// https://webaudio.github.io/web-speech-api/#availability-algorithm
/* static */
// Returns true when on-device speech recognition is blocked by the user's AI
// Controls setting (Settings > Firefox AI). The state pref may be "default",
// in which case the global browser.ai.control.default applies. These prefs are
// mirrored to content processes, so this content-side API reads them directly.
static bool IsBlockedByAIControls() {
  nsAutoCString state;
  Preferences::GetCString("browser.ai.control.speechRecognition", state);
  if (state.IsEmpty() || state.EqualsLiteral("default")) {
    Preferences::GetCString("browser.ai.control.default", state);
  }
  return state.EqualsLiteral("blocked");
}

already_AddRefed<Promise> SpeechRecognition::Available(
    const GlobalObject& aGlobal, const SpeechRecognitionOptions& aOptions,
    ErrorResult& aRv) {
  AssertIsOnMainThread();

  // Step 1: Check if Document is fully active.
  nsCOMPtr<nsPIDOMWindowInner> window =
      do_QueryInterface(aGlobal.GetAsSupports());
  if (!window || !window->IsFullyActive()) {
    aRv.ThrowInvalidStateError("The document is not fully active.");
    return nullptr;
  }

  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());
  if (!global) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  // Step 3: Validate all language tags are valid BCP47.
  for (const nsCString& lang : aOptions.mLangs) {
    if (!ValidateBCP47Language(lang, aRv)) {
      return nullptr;
    }
  }

  RefPtr<Promise> promise = Promise::Create(global, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  // Step 4: If processLocally is false, Gecko doesn't support remote
  // recognition.
  if (!aOptions.mProcessLocally) {
    SpeechRecognitionBackend::ResolveAvailability(
        promise, AvailabilityStatus::Unavailable);
    return promise.forget();
  }

  Document* doc = window->GetExtantDoc();
  if (!doc || !PermissionsPolicyUtils::IsFeatureAllowed(
                  doc, u"on-device-speech-recognition"_ns)) {
    SpeechRecognitionBackend::ResolveAvailability(
        promise, AvailabilityStatus::Unavailable);
    return promise.forget();
  }

  // The user can turn on-device speech recognition off via AI Controls.
  if (IsBlockedByAIControls()) {
    doc->WarnOnceAbout(Document::eSpeechRecognitionBlockedByAIControls);
    SpeechRecognitionBackend::ResolveAvailability(
        promise, AvailabilityStatus::Unavailable);
    return promise.forget();
  }

  // Step 5: processLocally is true.
  // If langs is empty, return unavailable.
  if (aOptions.mLangs.IsEmpty()) {
    SpeechRecognitionBackend::ResolveAvailability(
        promise, AvailabilityStatus::Unavailable);
    return promise.forget();
  }

  // Step 5.2.2.5: "Else (on-device speech recognition for language is not
  // supported), set currentLanguageStatus to unavailable", which per step
  // 5.2.2.6 comes last in the status ordering, so it settles the answer.
  for (const nsCString& lang : aOptions.mLangs) {
    if (SpeechModelFor(lang).isNothing()) {
      promise->MaybeResolve(AvailabilityStatus::Unavailable);
      return promise.forget();
    }
  }

  return SpeechRecognitionBackend::Available(global, aOptions.mLangs);
}

// Bridges the parent-driven install result (consent prompt + download, handled
// in the parent process) back to the shared install() transaction, and thus to
// every SpeechRecognition.install() promise waiting on it.
class SpeechRecognitionInstallHandler final : public PromiseNativeHandler {
 public:
  NS_DECL_ISUPPORTS

  explicit SpeechRecognitionInstallHandler(
      SpeechRecognitionInstallTransaction* aTransaction)
      : mTransaction(aTransaction) {}

  void ResolvedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                        ErrorResult& aRv) override {
    mTransaction->Resolve(aValue.isBoolean() && aValue.toBoolean());
  }

  void RejectedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                        ErrorResult& aRv) override {
    mTransaction->Resolve(false);
  }

 private:
  ~SpeechRecognitionInstallHandler() = default;

  RefPtr<SpeechRecognitionInstallTransaction> mTransaction;
};

NS_IMPL_ISUPPORTS0(SpeechRecognitionInstallHandler)

// https://webaudio.github.io/web-speech-api/#dom-speechrecognition-install
/* static */
already_AddRefed<Promise> SpeechRecognition::Install(
    const GlobalObject& aGlobal, const SpeechRecognitionOptions& aOptions,
    ErrorResult& aRv) {
  AssertIsOnMainThread();
  // Step 1: the document must be fully active.
  nsCOMPtr<nsPIDOMWindowInner> window =
      do_QueryInterface(aGlobal.GetAsSupports());
  nsCOMPtr<Document> doc = window ? window->GetExtantDoc() : nullptr;
  if (!window || !window->IsFullyActive() || !doc) {
    aRv.ThrowInvalidStateError("The document is not fully active.");
    return nullptr;
  }

  // Not conditioned on processLocally: there is no remote backend to install
  // for, so every install() ends up fetching an on-device model and has to
  // clear these gates.
  if (!PermissionsPolicyUtils::IsFeatureAllowed(
          doc, u"on-device-speech-recognition"_ns)) {
    aRv.ThrowNotAllowedError(
        "on-device speech recognition is not allowed in this cross-origin "
        "iframe");
    return nullptr;
  }

  // Blocked in AI Controls means there is nothing to install, which is not an
  // error the page can do anything about: resolve false, as available()
  // reports unavailable, rather than making the setting observable as a
  // distinct rejection.
  if (IsBlockedByAIControls()) {
    doc->WarnOnceAbout(Document::eSpeechRecognitionBlockedByAIControls);
    RefPtr<Promise> promise = Promise::Create(window->AsGlobal(), aRv);
    if (aRv.Failed()) {
      return nullptr;
    }
    promise->MaybeResolve(false);
    return promise.forget();
  }

  // install() initiates a potentially large download and a permission prompt,
  // so it requires transient user activation, and consumes it: one user
  // gesture buys at most one download prompt. The spec has no such
  // requirement, but the WPT for this method asserts that install() without a
  // user gesture rejects with a NotAllowedError.
  // https://github.com/WebAudio/web-speech-api/issues/202
  if (!doc->ConsumeTransientUserGestureActivation()) {
    aRv.ThrowNotAllowedError("install() requires transient user activation");
    return nullptr;
  }

  // Step 3: a language tag that is not valid BCP47 is a SyntaxError, as in
  // available().
  for (const nsCString& lang : aOptions.mLangs) {
    if (!ValidateBCP47Language(lang, aRv)) {
      return nullptr;
    }
  }

  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());
  if (!global) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  RefPtr<Promise> promise = Promise::Create(global, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  // Step 4: "If langs of options is an empty sequence, resolve promise with
  // false, abort these steps".
  if (aOptions.mLangs.IsEmpty()) {
    promise->MaybeResolve(false);
    return promise.forget();
  }

  // Step 5: "If the on-device speech recognition language pack for any lang in
  // langs of options is unsupported, resolve promise with false, abort these
  // steps" -- so before downloading anything.
  for (const nsCString& lang : aOptions.mLangs) {
    if (SpeechModelFor(lang).isNothing()) {
      promise->MaybeResolve(false);
      return promise.forget();
    }
  }

  bool transactionCreated = false;
  RefPtr<SpeechRecognitionInstallTransaction> transaction =
      SpeechRecognitionInstallTransaction::GetOrCreate(
          window, aOptions.mLangs, promise, &transactionCreated);

  if (!transactionCreated) {
    // An install() for these languages is already in flight in this window; it
    // will settle this promise too.
    return promise.forget();
  }

  // The user's consent to download, and the download itself, are obtained and
  // enforced in the parent process (see nsIMLModelResolver and its speech
  // implementation SpeechModelResolver, which skips the prompt entirely when
  // the model is already in the local cache). Content only asks, passing its
  // inner window id so the parent can verify ownership, identify the
  // requesting tab/principal, and anchor the permission prompt there.
  RefPtr<Promise> installPromise = SpeechRecognitionBackend::Install(
      global, transaction->Languages(), window->WindowID());
  if (!installPromise) {
    transaction->Resolve(false);
    return promise.forget();
  }

  RefPtr<SpeechRecognitionInstallHandler> handler =
      MakeRefPtr<SpeechRecognitionInstallHandler>(transaction);
  installPromise->AppendNativeHandler(handler);

  return promise.forget();
}

SpeechRecognitionPerfStats SpeechRecognition::BuildPerfStats() const {
  SpeechRecognitionPerfStats stats;
  if (mPerf.mEngineReady) {
    stats.mEngineReadyDuration = mPerf.mEngineReady->ToMilliseconds();
  }
  if (mPerf.mFirstResult) {
    stats.mFirstResultDuration = mPerf.mFirstResult->ToMilliseconds();
  }
  if (mPerf.mFinalization) {
    stats.mFinalizationDuration = mPerf.mFinalization->ToMilliseconds();
  }
  stats.mFedAudioDuration = mPerf.mEngine.mFedAudioMs;
  stats.mInferenceDuration = mPerf.mEngine.mInferenceMs;
  return stats;
}

already_AddRefed<Promise> SpeechRecognition::GetPerfStats(ErrorResult& aRv) {
  AssertIsOnMainThread();
  RefPtr<Promise> promise = Promise::Create(GetParentObject(), aRv);
  if (aRv.Failed()) {
    return nullptr;
  }
  promise->MaybeResolve(BuildPerfStats());
  return promise.forget();
}

void SpeechRecognition::Start(CallerType aCallerType, ErrorResult& aRv) {
  StartImpl(nullptr, aCallerType, aRv);
}

void SpeechRecognition::Start(MediaStreamTrack& aAudioTrack,
                              CallerType aCallerType, ErrorResult& aRv) {
  StartImpl(&aAudioTrack, aCallerType, aRv);
}

// https://webaudio.github.io/web-speech-api/#start-session-algorithm
void SpeechRecognition::StartImpl(MediaStreamTrack* aAudioTrack,
                                  CallerType aCallerType, ErrorResult& aRv) {
  AssertIsOnMainThread();
  LOG("SpeechRecognition::Start called");

  // Step 1: if the relevant global's associated Document is not fully active,
  // throw an InvalidStateError.
  nsPIDOMWindowInner* win = GetOwnerWindow();
  if (!win || !win->IsFullyActive()) {
    aRv.ThrowInvalidStateError("The document is not fully active.");
    return;
  }

  // The user can turn on-device speech recognition off via AI Controls.
  if (IsBlockedByAIControls()) {
    if (Document* doc = win->GetExtantDoc()) {
      doc->WarnOnceAbout(Document::eSpeechRecognitionBlockedByAIControls);
    }
    aRv.ThrowNotAllowedError(
        "on-device speech recognition is blocked by the user's AI settings");
    return;
  }

  // Step 2: if [[started]] is true and no error or end event has fired on it,
  // throw an InvalidStateError. mStarted is cleared once error/end fires, so it
  // tracks exactly that condition.
  if (mStarted) {
    aRv.ThrowInvalidStateError("Recognition has already been started");
    return;
  }

  MOZ_ASSERT(!mListener);
  MOZ_ASSERT(!mBackend);

  // Step 3 (phrases-not-supported) does not apply: Gecko supports contextual
  // biasing, so phrases are honoured rather than rejected (see below).
  uint32_t graphRate = 0;
  if (aAudioTrack) {
    graphRate = aAudioTrack->Graph()->GraphRate();
  } else {
    // If using the microphone, it is always at the preferred rate
    graphRate =
        CubebUtils::PreferredSampleRate(/* shouldResistFingerPrinting*/ false);
  }

  // init and start the backend
  // Extract phrase strings from our local copy of SpeechRecognitionPhrase
  // objects. The backend gets these at Start() time; the spec is unclear on
  // dynamic updates
  // https://github.com/WebAudio/web-speech-api/issues/172
  nsTArray<nsString> phrasesForBackend;
  for (const auto& phrase : mPhrases) {
    if (phrase) {
      nsString phraseStr;
      phrase->GetPhrase(phraseStr);
      phrasesForBackend.AppendElement(phraseStr);
    }
  }
  // Validate track if provided
  RefPtr<AudioStreamTrack> audioTrack;
  if (aAudioTrack) {
    audioTrack = aAudioTrack->AsAudioStreamTrack();

    if (!audioTrack) {
      aRv.ThrowInvalidStateError("MediaStreamTrack must be an audio track");
      return;
    }

    if (audioTrack->Ended()) {
      aRv.ThrowInvalidStateError("MediaStreamTrack is ended");
      return;
    }
  }

  // lang "will default to use the language of the html document root element
  // and associated hierarchy".
  nsString effectiveLang = mLang;
  if (effectiveLang.IsEmpty()) {
    if (nsCOMPtr<Document> doc = win->GetExtantDoc()) {
      if (Element* root = doc->GetRootElement()) {
        root->GetLang(effectiveLang);
      }
      if (effectiveLang.IsEmpty()) {
        if (nsAtom* language = doc->GetContentLanguageAsAtomForStyle()) {
          language->ToString(effectiveLang);
        }
      }
    }
  }

  // A document that declares no language at all leaves HTML's "language of a
  // node" unknown, which is the common case: no lang attribute, and no
  // Content-Language for anything the network did not serve. Recognition has
  // to run in some language, so use the user's own, which says more about what
  // is about to be spoken than the document does anyway. Chrome does the same.
  bool langFromUserLanguage = false;
  if (effectiveLang.IsEmpty()) {
    if (Document* doc = win->GetExtantDoc()) {
      doc->WarnOnceAbout(
          Document::eSpeechRecognitionLangDefaultedToUserLanguage);
    }
    win->Navigator()->GetLanguage(effectiveLang);
    langFromUserLanguage = true;
  }

  // Step 4 also treats a language pack as unavailable while its download
  // masking state is not "unmasked" for the caller's storage key, which only
  // install() does. Gecko doesn't track that state, but a document that
  // Permissions Policy keeps from calling install() can't unmask it itself.
  Document* doc = win->GetExtantDoc();
  if (!doc || !PermissionsPolicyUtils::IsFeatureAllowed(
                  doc, u"on-device-speech-recognition"_ns)) {
    LOGE("on-device-speech-recognition is not allowed in this document");
    DispatchErrorAndEnd(SpeechRecognitionErrorCode::Service_not_allowed,
                        "Local speech recognition is not available"_ns);
    return;
  }

  // Step 4.1: "If the user agent determines that local speech recognition is
  // not available for this.lang [...] fire an event named error [...] with its
  // error attribute initialized to service-not-allowed".
  // The match is kept rather than discarded: it names the model that will run
  // and its own locale for this language, which is what session_started
  // reports, `lang` being merely what the page asked for.
  SpeechModelMatch model;
  if (effectiveLang.IsEmpty()) {
    // Nothing to negotiate with, so the engine runs its own default, picked
    // the same way on the other side of the IPC boundary (see RecvInit).
    model = DefaultSpeechModel();
  } else if (Maybe<SpeechModelMatch> match =
                 SpeechModelFor(NS_ConvertUTF16toUTF8(effectiveLang))) {
    model = std::move(*match);
  } else {
    LOGE("No on-device model recognizes this language");
    // The spec code below is shared with three other causes, and this is the
    // common one, so record which it was. This happens before [[started]], so
    // it is also the only trace this session leaves.
    glean::media_speech_recognition::init_failure
        .EnumGet(InitFailure::eLanguageNotSupported)
        .Add();
    DispatchErrorAndEnd(SpeechRecognitionErrorCode::Service_not_allowed,
                        "No on-device model recognizes this language"_ns);
    return;
  }

  mPerf = PerfTimeline{};

  // Step 5: set [[started]] to true, before the model install below: for
  // script the session is running from here on.
  mStarted = true;
  mBackendListening = false;
  mStartDispatched = false;
  const uint32_t generation = ++mSessionGeneration;

  mSessionStartTime = TimeStamp::Now();
  mResultLatencyTotal = TimeDuration();
  mResultLatencySampleCount = 0;
  mSessionError = Nothing();
  mSessionId = nsIDToCString(nsID::GenerateUUID()).get();

  {
    glean::media_speech_recognition::SessionStartedExtra extra;
    extra.lang.emplace(NS_ConvertUTF16toUTF8(effectiveLang));
    extra.langSource.emplace(!mLang.IsEmpty()          ? "attribute"_ns
                             : effectiveLang.IsEmpty() ? "none"_ns
                             : langFromUserLanguage    ? "user"_ns
                                                       : "document"_ns);
    extra.modelId.emplace(model.mId);
    extra.modelLocale.emplace(model.mLocale);
    extra.sessionId.emplace(mSessionId);
    glean::media_speech_recognition::session_started.Record(
        Some(std::move(extra)));
  }

  // Register keep-alive event types. While recognition is active, if script
  // has listeners for these events, the object stays alive even without a
  // direct reference from script.
  for (nsStaticAtom* atom : kKeepAliveEventTypes) {
    KeepAliveIfHasListenersFor(atom);
  }

  PendingSession session{std::move(audioTrack), aCallerType, effectiveLang,
                         graphRate, std::move(phrasesForBackend)};

  // Pages predating install() just call start(): offer to download the model
  // rather than failing the session. No event has fired yet, so it merely
  // starts later. With no language there is no model to ask for. Like
  // install(), the offer needs transient user activation, and without it
  // start() proceeds as if the pref were off.
  if (!StaticPrefs::media_webspeech_recognition_install_on_start() ||
      effectiveLang.IsEmpty() ||
      !doc->ConsumeTransientUserGestureActivation()) {
    BeginSession(std::move(session));
    return;
  }

  mAwaitingModelInstall = true;
  AutoTArray<nsCString, 1> languages{NS_ConvertUTF16toUTF8(effectiveLang)};
  SpeechRecognitionBackend::EnsureModelsInstalled(languages, win->WindowID())
      ->Then(GetMainThreadSerialEventTarget(), __func__,
             [self = RefPtr{this}, generation, session = std::move(session)](
                 SpeechRecognitionBackend::ModelInstallPromise::
                     ResolveOrRejectValue&& aValue) mutable {
               AssertIsOnMainThread();
               self->OnModelInstalled(
                   generation,
                   aValue.IsResolve() ? Some(aValue.ResolveValue()) : Nothing(),
                   std::move(session));
             });
}

void SpeechRecognition::OnModelInstalled(
    uint32_t aGeneration, Maybe<hwinference::ModelInstallResult> aResult,
    PendingSession&& aSession) {
  AssertIsOnMainThread();
  // Dropped if the session ended, or was superseded, while waiting.
  if (!mStarted || mStopping || mAborting ||
      mSessionGeneration != aGeneration) {
    LOG("{} - dropped: result={} started={} stopping={} aborting={} "
        "generation={} current={}",
        __func__, aResult ? int(uint8_t(*aResult)) : -1, mStarted, mStopping,
        mAborting, aGeneration, mSessionGeneration);
    return;
  }
  mAwaitingModelInstall = false;

  if (aResult.isNothing()) {
    // The request never reached the service, so there is nothing to recognize
    // with, the same as a backend that cannot be created below.
    LOGE("Could not ask for the on-device model");
    glean::media_speech_recognition::init_failure
        .EnumGet(InitFailure::eModelInstallUnavailable)
        .Add();
    DispatchErrorAndEnd(SpeechRecognitionErrorCode::Service_not_allowed,
                        "Local speech recognition is not available"_ns);
    return;
  }

  if (*aResult == hwinference::ModelInstallResult::Installed) {
    BeginSession(std::move(aSession));
    return;
  }
  // A refused download is a refused permission, like a refused microphone,
  // rather than a language that cannot be recognized.
  const bool denied = *aResult == hwinference::ModelInstallResult::Denied;
  LOGE("The on-device model was not installed, denied={}", denied);
  DispatchErrorAndEnd(denied ? SpeechRecognitionErrorCode::Not_allowed
                             : SpeechRecognitionErrorCode::Network,
                      denied ? "The model download was refused"_ns
                             : "The model could not be downloaded"_ns);
}

void SpeechRecognition::BeginSession(PendingSession&& aSession) {
  AssertIsOnMainThread();
  MOZ_ASSERT(mStarted);
  MOZ_ASSERT(!mBackend);

  // Timed from here rather than from start(): a model install can sit in
  // front of this waiting on the user, and that wait is not engine latency.
  mPerf.mStart = TimeStamp::Now();

  // start() throws for a track that has already ended; one can also end
  // while the model is downloading.
  if (aSession.mTrack && aSession.mTrack->Ended()) {
    LOGE("The audio track ended before the session could start");
    DispatchErrorAndEnd(SpeechRecognitionErrorCode::Audio_capture,
                        "MediaStreamTrack is ended"_ns);
    return;
  }

  // Step 4: processLocally is always true here (on-device recognition). If the
  // backend cannot start (local recognition unavailable for this lang), fire a
  // service-not-allowed error and abort. DispatchErrorAndEnd queues the event.
  mBackend = SpeechRecognitionBackend::Create(
      this, aSession.mGraphRate, aSession.mLanguage, aSession.mPhrases);
  if (!mBackend) {
    LOGE("Failed to create the backend");
    glean::media_speech_recognition::init_failure
        .EnumGet(InitFailure::eBackendCreationFailed)
        .Add();
    DispatchErrorAndEnd(SpeechRecognitionErrorCode::Service_not_allowed,
                        "Local speech recognition is not available"_ns);
    return;
  }
  mBackend->Start();

  // "start" fires once the system is successfully listening (see
  // MaybeDispatchStart()), not here: at this point neither the backend
  // session nor (for the microphone path) the track are ready yet.

  // MediaStreamTrack (argument passed) vs. Microphone (no argument passed)
  if (aSession.mTrack) {
    NotifyTrackAdded(aSession.mTrack);
  } else {
    mListener = new TrackListener(this);
    // Identifies the session this continuation belongs to: mListener is
    // freshly allocated per Start() call, so comparing against the live
    // mListener below detects both "stopped" (mListener now null) and
    // "superseded by a newer session" (mListener now points elsewhere)
    // uniformly, the same way IsCurrentBackend() does for backend callbacks.
    RefPtr<TrackListener> startedListener = mListener;

    MediaStreamConstraints constraints;
    constraints.mAudio.SetAsBoolean() = true;

    AutoNoJSAPI nojsapi;
    RefPtr<SpeechRecognition> self(this);
    MediaManager::Get()
        ->GetUserMedia(GetOwnerWindow(), constraints, aSession.mCallerType)
        ->Then(
            GetCurrentSerialEventTarget(), __func__,
            [this, self, startedListener](RefPtr<DOMMediaStream>&& aStream) {
              nsTArray<RefPtr<AudioStreamTrack>> tracks;
              aStream->GetAudioTracks(tracks);
              if (mListener != startedListener) {
                // Recognition was stopped, or superseded by a newer session
                // on this instance. Exit early.
                for (const RefPtr<AudioStreamTrack>& track : tracks) {
                  track->Stop();
                }
                return;
              }
              mStream = std::move(aStream);
              mStream->RegisterTrackListener(mListener);
              // This track came from our own getUserMedia() call, so nobody
              // else will stop it; Reset() must do so on teardown.
              mTrackIsOwned = true;
              for (const RefPtr<AudioStreamTrack>& track : tracks) {
                if (!track->Ended()) {
                  NotifyTrackAdded(track);
                }
              }
            },
            [this, self, startedListener](RefPtr<MediaMgrError>&& error) {
              if (mListener != startedListener) {
                // Recognition was stopped, or superseded by a newer session
                // on this instance. Exit early.
                return;
              }
              SpeechRecognitionErrorCode errorCode;

              if (error->mName == MediaMgrError::Name::NotAllowedError) {
                errorCode = SpeechRecognitionErrorCode::Not_allowed;
              } else {
                errorCode = SpeechRecognitionErrorCode::Audio_capture;
              }
              DispatchErrorAndEnd(errorCode, error->mMessage);
            });
  }
}

void SpeechRecognition::Stop() {
  AssertIsOnMainThread();
  // https://webaudio.github.io/web-speech-api/#dom-speechrecognition-stop
  // "If the stop method is called on an object which is already stopped or
  // being stopped [...] the user agent must ignore the call."
  if (!mStarted || mStopping || (!mBackend && !mAwaitingModelInstall)) {
    return;
  }
  mStopping = true;
  mPerf.mStop = Some(TimeStamp::Now());

  if (mAwaitingModelInstall) {
    // Nothing was captured: no result to return, no audio lifecycle to close.
    PostResetAndEnd();
    return;
  }

  // Same section: "The speech service must attempt to return a recognition
  // result (or a nomatch) based on the audio that it has already collected."
  // So mBackend stays live - late results are wanted here, unlike on the
  // abort path - and "end" waits for OnSessionFinished(). Dispatches soundend
  // (if needed) and audioend via main thread runnables in the meantime.
  mBackend->Stop();
}

void SpeechRecognition::OnSessionFinished(bool aProducedResult,
                                          EnginePerfStats aEngineStats) {
  AssertIsOnMainThread();
  LOG("OnSessionFinished: producedResult={}", aProducedResult);
  mPerf.mEngine = aEngineStats;
  mBackend = nullptr;

  if (!aProducedResult) {
    DispatchNoMatch();
  }
  PostResetAndEnd();
}

void SpeechRecognition::DispatchNoMatch() {
  AssertIsOnMainThread();
  // https://webaudio.github.io/web-speech-api/#eventdef-speechrecognition-nomatch
  // The event's results "may contain speech recognition results that are below
  // the confidence threshold or may be null"; the engine hands us nothing at
  // all in this case, so the list is empty.
  RootedDictionary<SpeechRecognitionEventInit> init(RootingCx());
  // https://webaudio.github.io/web-speech-api/#speechreco-events
  // "These events do not bubble and are not cancelable."
  init.mBubbles = false;
  init.mCancelable = false;
  init.mResultIndex = 0;
  init.mResults = new SpeechRecognitionResultList(this);
  init.mInterpretation = JS::NullValue();

  RefPtr<SpeechRecognitionEvent> domEvent =
      SpeechRecognitionEvent::Constructor(this, u"nomatch"_ns, init);
  domEvent->SetTrusted(true);
  DispatchEvent(*domEvent);
}

void SpeechRecognition::Abort() {
  AssertIsOnMainThread();
  // https://webaudio.github.io/web-speech-api/#dom-speechrecognition-abort
  // "If the abort method is called on an object which is already stopped or
  // aborting (that is, start was never called on it, the end or error event
  // has fired on it, or abort was previously called on it), the user agent
  // must ignore the call." Without this, a second abort() would queue a second
  // reset-and-end, firing "end" twice.
  if (!mStarted || mAborting) {
    return;
  }
  mAborting = true;

  if (mBackend) {
    mBackend->Abort(TrailingEvents::Fire);
    // Clear backend after abort since no more results are expected
    mBackend = nullptr;
  }

  // https://webaudio.github.io/web-speech-api/#dom-speechrecognition-abort
  // "The user agent must raise an end event once the speech service is no
  // longer connected."
  PostResetAndEnd();
}

void SpeechRecognition::NotifyTrackAdded(
    const RefPtr<MediaStreamTrack>& aTrack) {
  if (mTrack) {
    return;
  }

  // Stop()/Abort() clear mBackend synchronously but only clear mListener
  // (and thus invalidate the getUserMedia continuation's startedListener
  // check) asynchronously via PostResetAndEnd(). A track can be reported
  // added - via that continuation or via the TrackListener callback - in the
  // gap between the two, when mListener still looks live but there is no
  // backend left to record into.
  if (!mBackend) {
    return;
  }

  RefPtr<AudioStreamTrack> audioTrack = aTrack->AsAudioStreamTrack();
  if (!audioTrack) {
    return;
  }

  if (audioTrack->Ended()) {
    return;
  }

  StartRecording(audioTrack);
}

// Called on the main thread, but enacted on the MediaTrackGraph thread. This
// is safe because MediaStreamTrack combines the old and the new principal and
// notifies its observers before any content under the new principal is
// inserted into the graph, so SetEnabled() is queued ahead of that content.
// See the longer explanation on
// MediaStreamTrackAudioSourceNode::PrincipalChanged.
void SpeechRecognition::PrincipalChanged(MediaStreamTrack* aMediaStreamTrack) {
  AssertIsOnMainThread();
  MOZ_ASSERT(aMediaStreamTrack == mTrack);

  bool subsumes = false;
  Document* doc = nullptr;
  if (nsPIDOMWindowInner* win = GetOwnerWindow()) {
    doc = win->GetExtantDoc();
    if (doc) {
      nsIPrincipal* docPrincipal = doc->NodePrincipal();
      nsIPrincipal* trackPrincipal = aMediaStreamTrack->GetPrincipal();
      if (!trackPrincipal ||
          NS_FAILED(docPrincipal->Subsumes(trackPrincipal, &subsumes))) {
        subsumes = false;
      }
    }
  }
  bool enabled = subsumes;
  if (mBackend) {
    mBackend->SetEnabled(enabled);
  }

  if (!enabled && doc) {
    doc->WarnOnceAbout(Document::eSpeechRecognitionIsolatedTrack);
  }
}

void SpeechRecognition::DispatchError(SpeechRecognitionErrorCode aErrorCode,
                                      const nsACString& aMessage) {
  MOZ_ASSERT(NS_IsMainThread(), "DispatchError must be on main thread");

  glean::media_speech_recognition::error.EnumGet(ErrorCodeLabel(aErrorCode))
      .Add(1);
  mSessionError = Some(aErrorCode);

  RefPtr<SpeechRecognitionErrorEvent> srError =
      new SpeechRecognitionErrorEvent(nullptr, nullptr, nullptr);

  // https://webaudio.github.io/web-speech-api/#speechreco-events
  // "These events do not bubble and are not cancelable."
  srError->InitSpeechRecognitionError(u"error"_ns, false, false, aErrorCode,
                                      aMessage);
  srError->SetTrusted(true);

  DispatchEvent(*srError);
}

// https://webaudio.github.io/web-speech-api/#eventdef-speechrecognition-end
// "Fired when the service has disconnected. The event must always be
// generated when the session ends no matter the reason for the end."
void SpeechRecognition::DispatchErrorAndEnd(
    SpeechRecognitionErrorCode aErrorCode, const nsACString& aMessage) {
  AssertIsOnMainThread();
  DispatchError(aErrorCode, aMessage);
  if (!mStarted) {
    // The session never reached [[started]] == true (e.g. the backend
    // failed to start before we got there); nothing to tear down and no
    // "end" event is expected.
    return;
  }
  if (mBackend) {
    mBackend->Abort(TrailingEvents::Skip);
    mBackend = nullptr;
  }
  PostResetAndEnd();
}

void SpeechRecognition::DispatchTrustedEventWithTimestamp(
    const nsAString& aEventName, TimeStamp aTimeStamp) {
  LOG("Dispatching trusted event: {}", NS_ConvertUTF16toUTF8(aEventName).get());
  RefPtr<Event> event = NS_NewDOMEvent(this, nullptr, nullptr);
  event->InitEvent(aEventName, false, false);
  if (!aTimeStamp.IsNull()) {
    event->WidgetEventPtr()->mTimeStamp = aTimeStamp;
  }
  event->SetTrusted(true);
  ErrorResult rv;
  DispatchEvent(*event, rv);
}

void SpeechRecognition::HandleRecognitionResultFromBackend(
    const nsCString& aTranscript, bool aIsFinal, float aConfidence,
    TimeStamp aEventTime) {
  MOZ_ASSERT(NS_IsMainThread(), "Must be called on main thread");
  LOG("HandleRecognitionResultFromBackend: {} (final={}, conf={})",
      aTranscript.get(), aIsFinal, aConfidence);

  // Check if still active
  if (!mBackend) {
    LOG("Ignoring result - backend is gone");
    return;
  }

  // Per spec: when interimResults is false, interim results must not be
  // returned
  if (!aIsFinal && !mInterimResults) {
    LOG("Ignoring interim result - interimResults is false");
    return;
  }

  // https://webaudio.github.io/web-speech-api/#dom-speechrecognition-continuous
  // "When the continuous attribute is set to false, the user agent must return
  // no more than one final result". Such a session is stopped as soon as its
  // first final result is dispatched (below), but the backend's end-of-stream
  // flush can still produce results while it winds down; drop those. Only the
  // finals: the same section notes that continuous "does not affect interim
  // results".
  if (aIsFinal && !mContinuous && !mRecognitionResults.IsEmpty()) {
    LOG("Ignoring result - non-continuous session already delivered its final "
        "result");
    return;
  }

  if (!aEventTime.IsNull()) {
    mResultLatencyTotal += TimeStamp::Now() - aEventTime;
    mResultLatencySampleCount++;
  }

  RefPtr<SpeechRecognitionResult> result = new SpeechRecognitionResult(this);

  RefPtr<SpeechRecognitionAlternative> alternative =
      new SpeechRecognitionAlternative(this);

  // https://webaudio.github.io/web-speech-api/#dom-speechrecognitionalternative-transcript
  // "For continuous recognition, leading or trailing whitespace MUST be
  // included where necessary such that concatenation of consecutive
  // SpeechRecognitionResults produces a proper transcript of the session."
  // The backend reports one utterance at a time, unpadded.
  alternative->mTranscript = NS_ConvertUTF8toUTF16(aTranscript);
  if (!mRecognitionResults.IsEmpty()) {
    alternative->mTranscript.Insert(u' ', 0);
  }
  // Per-result confidence, aggregated by the backend from the model's per-word
  // confidences (mean). The spec leaves the exact aggregation engine-defined;
  // the legacy backend, which has no per-word scores, reports 1.0.
  alternative->mConfidence =
      std::isfinite(aConfidence) ? std::clamp(aConfidence, 0.0f, 1.0f) : 0.0f;

  result->mItems.AppendElement(alternative);

  result->SetFinal(aIsFinal);

  // event.results is every final of the session followed by the interim in
  // flight, and resultIndex is the lowest changed index.
  uint32_t resultIndex = mRecognitionResults.Length();
  if (aIsFinal) {
    mRecognitionResults.AppendElement(result);
  }

  RefPtr<SpeechRecognitionResultList> resultList =
      new SpeechRecognitionResultList(this);
  resultList->mItems.AppendElements(mRecognitionResults);
  if (!aIsFinal) {
    resultList->mItems.AppendElement(result);
  }

  RootedDictionary<SpeechRecognitionEventInit> init(RootingCx());
  // https://webaudio.github.io/web-speech-api/#speechreco-events
  // "These events do not bubble and are not cancelable."
  init.mBubbles = false;
  init.mCancelable = false;
  init.mResultIndex = resultIndex;
  init.mResults = resultList;
  init.mInterpretation = JS::NullValue();

  RefPtr<SpeechRecognitionEvent> domEvent =
      SpeechRecognitionEvent::Constructor(this, u"result"_ns, init);
  domEvent->SetTrusted(true);
  if (!aEventTime.IsNull()) {
    domEvent->WidgetEventPtr()->mTimeStamp = aEventTime;
  }
  // From now, not from aEventTime, which is when the audio behind this result
  // was captured: what matters is when the page got the word.
  if (!mPerf.mFirstResult) {
    mPerf.mFirstResult = Some(TimeStamp::Now() - mPerf.mStart);
  }
  DispatchEvent(*domEvent);

  // https://webaudio.github.io/web-speech-api/#dom-speechrecognition-continuous
  // False describes "a single turn pattern of interaction": the turn is over
  // once its one final result has been delivered, so end the session rather
  // than keep the microphone open. Stop() rather than Abort(), so that the
  // backend still flushes and "end" fires the usual way.
  if (aIsFinal && !mContinuous) {
    Stop();
  }
}

void SpeechRecognition::HandleRecognitionErrorFromBackend(
    const nsCString& aError) {
  MOZ_ASSERT(NS_IsMainThread(), "Must be called on main thread");
  LOGE("HandleRecognitionErrorFromBackend: {}", aError.get());

  // Check if we're still active
  if (!mBackend) {
    LOG("Ignoring error - backend is gone");
    return;
  }

  // Map backend errors to appropriate error codes
  SpeechRecognitionErrorCode errorCode = SpeechRecognitionErrorCode::Network;
  if (aError.EqualsLiteral("concurrent-session") ||
      aError.EqualsLiteral("service-not-allowed")) {
    errorCode = SpeechRecognitionErrorCode::Service_not_allowed;
  }

  LOG("Dispatching error DOM event: {}", aError.get());
  DispatchErrorAndEnd(errorCode, aError);
}

}  // namespace mozilla::dom

#undef LOG
#undef LOGV
#undef LOGE
