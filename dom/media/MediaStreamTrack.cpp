/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MediaStreamTrack.h"

#include "DOMMediaStream.h"
#include "MediaSegment.h"
#include "MediaStreamError.h"
#include "MediaTrackGraph.h"
#include "MediaTrackGraphImpl.h"
#include "MediaTrackListener.h"
#include "mozilla/AbstractThread.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/dom/Promise.h"
#include "nsContentUtils.h"
#include "nsGlobalWindowInner.h"
#include "nsIUUIDGenerator.h"
#include "nsServiceManagerUtils.h"
#include "systemservices/MediaUtils.h"

mozilla::LazyLogModule gMediaStreamTrackLog("MediaStreamTrack");
#define LOG(type, ...) \
  MOZ_LOG_FMT(gMediaStreamTrackLog, type, MOZ_LOG_EXPAND_ARGS __VA_ARGS__)

using namespace mozilla::media;

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTING_ADDREF(MediaStreamTrackSource)
NS_IMPL_CYCLE_COLLECTING_RELEASE(MediaStreamTrackSource)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(MediaStreamTrackSource)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTION_CLASS(MediaStreamTrackSource)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(MediaStreamTrackSource)
  tmp->Destroy();
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mPrincipal)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(MediaStreamTrackSource)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mPrincipal)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

MediaStreamTrackSource::CloneResult::~CloneResult() = default;

auto MediaStreamTrackSource::Clone() -> CloneResult { return {}; }

auto MediaStreamTrackSource::ApplyConstraints(
    const dom::MediaTrackConstraints& aConstraints, CallerType aCallerType)
    -> RefPtr<ApplyConstraintsPromise> {
  return ApplyConstraintsPromise::CreateAndReject(
      MakeRefPtr<MediaMgrError>(MediaMgrError::Name::OverconstrainedError, ""),
      __func__);
}

class MediaStreamTrack::TrackSink : public MediaStreamTrackSource::Sink {
 public:
  explicit TrackSink(MediaStreamTrack* aTrack) : mTrack(aTrack) {}

  /**
   * Keep the track source alive. This track and any clones are controlling the
   * lifetime of the source by being registered as its sinks.
   */
  bool KeepsSourceAlive() const override { return true; }

  bool Enabled() const override {
    if (!mTrack) {
      return false;
    }
    return mTrack->Enabled();
  }

  void PrincipalChanged() override {
    if (mTrack) {
      mTrack->PrincipalChanged();
    }
  }

  void MutedChanged(bool aNewState) override {
    if (mTrack) {
      mTrack->MutedChanged(aNewState);
    }
  }

  void ConstraintsChanged(const MediaTrackConstraints& aConstraints) override {
    if (mTrack) {
      mTrack->ConstraintsChanged(aConstraints);
    }
  }

  void OverrideEnded() override {
    if (mTrack) {
      mTrack->OverrideEnded();
    }
  }

 private:
  WeakPtr<MediaStreamTrack> mTrack;
};

MediaStreamTrack::MediaStreamTrack(nsPIDOMWindowInner* aWindow,
                                   mozilla::MediaTrack* aInputTrack,
                                   MediaStreamTrackSource* aSource,
                                   MediaStreamTrackState aReadyState,
                                   bool aMuted,
                                   const MediaTrackConstraints& aConstraints)
    : mWindow(aWindow),
      mInputTrack(aInputTrack),
      mSource(aSource),
      mSink(std::make_unique<TrackSink>(this)),
      mPrincipal(aSource->GetPrincipal()),
      mReadyState(aReadyState),
      mEnabled(true),
      mMuted(aMuted),
      mConstraints(aConstraints),
      mWatchManager(this, AbstractThread::MainThread()),
      mTrackEnded(AbstractThread::MainThread(), false,
                  "MediaStreamTrack::mTrackEnded"),
      mTrackPrincipalHandle(AbstractThread::MainThread(), PRINCIPAL_HANDLE_NONE,
                            "MediaStreamTrack::mTrackPrincipalHandle") {
  if (!Ended()) {
    GetSource().RegisterSink(mSink.get());

    // Even if the input track is destroyed we need mTrack so that methods
    // like AddListener still work. Keeping the number of paths to a minimum
    // also helps prevent bugs elsewhere. We'll be ended through the
    // MediaStreamTrackSource soon enough.
    auto graph = mInputTrack->IsDestroyed()
                     ? MediaTrackGraph::GetInstanceIfExists(
                           mWindow, mInputTrack->mSampleRate,
                           MediaTrackGraph::DEFAULT_OUTPUT_DEVICE)
                     : mInputTrack->Graph();
    MOZ_DIAGNOSTIC_ASSERT(graph,
                          "A destroyed input track is only expected when "
                          "cloning, but since we're live there must be another "
                          "live track that is keeping the graph alive");

    mTrack = graph->CreateForwardedInputTrack(
        mInputTrack->mType, mozilla::MediaTrack::Flag::EnableCanonicals);
    mPort = mTrack->AllocateInputPort(mInputTrack);
    mTrackEnded.Connect(&mTrack->CanonicalEnded());
    mWatchManager.Watch(mTrackEnded, &MediaStreamTrack::OnTrackEnded);
    mTrackPrincipalHandle.Connect(&mTrack->CanonicalPrincipalHandle());
    mWatchManager.Watch(mTrackPrincipalHandle,
                        &MediaStreamTrack::OnPrincipalHandleChanged);
  }

  nsresult rv;
  nsCOMPtr<nsIUUIDGenerator> uuidgen =
      do_GetService("@mozilla.org/uuid-generator;1", &rv);

  nsID uuid;
  memset(&uuid, 0, sizeof(uuid));
  if (uuidgen) {
    uuidgen->GenerateUUIDInPlace(&uuid);
  }

  char chars[NSID_LENGTH];
  uuid.ToProvidedString(chars);
  mID = NS_ConvertASCIItoUTF16(chars);
}

MediaStreamTrack::~MediaStreamTrack() { Destroy(); }

void MediaStreamTrack::Destroy() {
  SetReadyState(MediaStreamTrackState::Ended);
  // Remove all listeners -- avoid iterating over the list we're removing from
  for (const auto& listener : mTrackListeners.Clone()) {
    RemoveListener(listener);
  }
  // Do the same as above for direct listeners
  for (const auto& listener : mDirectTrackListeners.Clone()) {
    RemoveDirectListener(listener);
  }
}

NS_IMPL_CYCLE_COLLECTION_CLASS(MediaStreamTrack)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(MediaStreamTrack,
                                                DOMEventTargetHelper)
  tmp->Destroy();
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mWindow)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mSource)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mPrincipal)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mPendingPrincipal)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_WEAK_PTR
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(MediaStreamTrack,
                                                  DOMEventTargetHelper)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mWindow)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mSource)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mPrincipal)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mPendingPrincipal)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_ADDREF_INHERITED(MediaStreamTrack, DOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(MediaStreamTrack, DOMEventTargetHelper)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(MediaStreamTrack)
NS_INTERFACE_MAP_END_INHERITING(DOMEventTargetHelper)

JSObject* MediaStreamTrack::WrapObject(JSContext* aCx,
                                       JS::Handle<JSObject*> aGivenProto) {
  return MediaStreamTrack_Binding::Wrap(aCx, this, aGivenProto);
}

void MediaStreamTrack::GetId(nsAString& aID) const { aID = mID; }

void MediaStreamTrack::SetEnabled(bool aEnabled) {
  LOG(LogLevel::Info, ("MediaStreamTrack {} {}", fmt::ptr(this),
                       aEnabled ? "Enabled" : "Disabled"));

  if (mEnabled == aEnabled) {
    return;
  }

  mEnabled = aEnabled;

  if (Ended()) {
    return;
  }

  mTrack->SetDisabledTrackMode(mEnabled ? DisabledTrackMode::ENABLED
                                        : DisabledTrackMode::SILENCE_BLACK);
  NotifyEnabledChanged();
}

void MediaStreamTrack::Stop() {
  LOG(LogLevel::Info, ("MediaStreamTrack {} Stop()", fmt::ptr(this)));

  if (Ended()) {
    LOG(LogLevel::Warning,
        ("MediaStreamTrack {} Already ended", fmt::ptr(this)));
    return;
  }

  SetReadyState(MediaStreamTrackState::Ended);

  NotifyEnded();
}

void MediaStreamTrack::GetCapabilities(MediaTrackCapabilities& aResult,
                                       CallerType aCallerType) {
  GetSource().GetCapabilities(aResult);
}

void MediaStreamTrack::GetConstraints(dom::MediaTrackConstraints& aResult) {
  aResult = mConstraints;
}

void MediaStreamTrack::GetSettings(dom::MediaTrackSettings& aResult,
                                   CallerType aCallerType) {
  GetSource().GetSettings(aResult);

  // Spoof values when privacy.resistFingerprinting is true.
  nsIGlobalObject* global = mWindow ? mWindow->AsGlobal() : nullptr;
  if (!nsContentUtils::ShouldResistFingerprinting(
          aCallerType, global, RFPTarget::StreamVideoFacingMode)) {
    return;
  }
  if (aResult.mFacingMode.WasPassed()) {
    aResult.mFacingMode.Value().AssignASCII(
        GetEnumString(VideoFacingModeEnum::User));
  }
}

already_AddRefed<Promise> MediaStreamTrack::ApplyConstraints(
    const MediaTrackConstraints& aConstraints, CallerType aCallerType,
    ErrorResult& aRv) {
  if (MOZ_LOG_TEST(gMediaStreamTrackLog, LogLevel::Info)) {
    nsString str;
    aConstraints.ToJSON(str);

    LOG(LogLevel::Info, ("MediaStreamTrack {} ApplyConstraints() with "
                         "constraints {}",
                         fmt::ptr(this), NS_ConvertUTF16toUTF8(str).get()));
  }

  nsIGlobalObject* go = mWindow ? mWindow->AsGlobal() : nullptr;

  RefPtr<Promise> promise = Promise::Create(go, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  // Forward constraints to the source.
  //
  // After GetSource().ApplyConstraints succeeds (after it's been to
  // media-thread and back), and no sooner, do we set mConstraints to the newly
  // applied values.

  // Keep a reference to this, to make sure it's still here when we get back.
  RefPtr<MediaStreamTrack> self(this);
  GetSource()
      .ApplyConstraints(aConstraints, aCallerType)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [this, self, promise, aConstraints](bool aDummy) {
            if (!mWindow || !mWindow->IsCurrentInnerWindow()) {
              return;  // Leave Promise pending after navigation by design.
            }
            promise->MaybeResolve(false);
          },
          [this, self, promise](const RefPtr<MediaMgrError>& aError) {
            if (!mWindow || !mWindow->IsCurrentInnerWindow()) {
              return;  // Leave Promise pending after navigation by design.
            }
            promise->MaybeReject(
                MakeRefPtr<MediaStreamError>(mWindow, *aError));
          });
  return promise.forget();
}

ProcessedMediaTrack* MediaStreamTrack::GetTrack() const {
  MOZ_DIAGNOSTIC_ASSERT(!Ended());
  return mTrack;
}

MediaTrackGraph* MediaStreamTrack::Graph() const {
  MOZ_DIAGNOSTIC_ASSERT(!Ended());
  return mTrack->Graph();
}

MediaTrackGraphImpl* MediaStreamTrack::GraphImpl() const {
  MOZ_DIAGNOSTIC_ASSERT(!Ended());
  return mTrack->GraphImpl();
}

void MediaStreamTrack::SetPrincipal(nsIPrincipal* aPrincipal) {
  if (aPrincipal == mPrincipal) {
    return;
  }
  mPrincipal = aPrincipal;

  LOG(LogLevel::Info,
      ("MediaStreamTrack {} principal changed to {}. Now: "
       "null={}, codebase={}, expanded={}, system={}",
       fmt::ptr(this), fmt::ptr(mPrincipal.get()),
       mPrincipal->GetIsNullPrincipal(), mPrincipal->GetIsContentPrincipal(),
       mPrincipal->GetIsExpandedPrincipal(), mPrincipal->IsSystemPrincipal()));
  for (PrincipalChangeObserver<MediaStreamTrack>* observer :
       mPrincipalChangeObservers) {
    observer->PrincipalChanged(this);
  }
}

void MediaStreamTrack::PrincipalChanged() {
  mPendingPrincipal = GetSource().GetPrincipal();
  nsCOMPtr<nsIPrincipal> newPrincipal = mPrincipal;
  LOG(LogLevel::Info, ("MediaStreamTrack {} Principal changed on main thread "
                       "to {} (pending). Combining with existing principal {}.",
                       fmt::ptr(this), fmt::ptr(mPendingPrincipal.get()),
                       fmt::ptr(mPrincipal.get())));
  if (nsContentUtils::CombineResourcePrincipals(&newPrincipal,
                                                mPendingPrincipal)) {
    SetPrincipal(newPrincipal);
  }
}

/**
 * mTrackPrincipalHandle mirrors the PrincipalHandle of the media flowing
 * through the MediaTrackGraph, and the following applies:
 *
 * When the main thread principal for a MediaStreamTrack changes, its principal
 * will be set to the combination of the previous principal and the new one.
 *
 * As a PrincipalHandle change later happens on the MediaTrackGraph thread, we
 * will be notified. If the latest principal on main thread matches the
 * PrincipalHandle we just saw on MTG thread, we will set the track's principal
 * to the new one.
 *
 * We know at this point that the old principal has been flushed out and data
 * under it cannot leak to consumers.
 *
 * In case of multiple changes to the main thread state, the track's principal
 * will be a combination of its old principal and all the new ones until the
 * latest main thread principal matches the PrincipalHandle on the MTG thread.
 */
void MediaStreamTrack::OnPrincipalHandleChanged() {
  MOZ_ASSERT(NS_IsMainThread());
  const PrincipalHandle& newPrincipalHandle = mTrackPrincipalHandle;
  LOG(LogLevel::Info,
      ("MediaStreamTrack {} principalHandle changed on "
       "MediaTrackGraph thread to {}. Current principal: {}, "
       "pending: {}",
       fmt::ptr(this), fmt::ptr(GetPrincipalFromHandle(newPrincipalHandle)),
       fmt::ptr(mPrincipal.get()), fmt::ptr(mPendingPrincipal.get())));
  if (PrincipalHandleMatches(newPrincipalHandle, mPendingPrincipal)) {
    SetPrincipal(mPendingPrincipal);
    mPendingPrincipal = nullptr;
  }
}

void MediaStreamTrack::MutedChanged(bool aNewState) {
  MOZ_ASSERT(NS_IsMainThread());

  /**
   * 4.3.1 Life-cycle and Media flow - Media flow
   * To set a track's muted state to newState, the User Agent MUST run the
   * following steps:
   *  1. Let track be the MediaStreamTrack in question.
   *  2. Set track's muted attribute to newState.
   *  3. If newState is true let eventName be mute, otherwise unmute.
   *  4. Fire a simple event named eventName on track.
   */

  if (mMuted == aNewState) {
    return;
  }

  LOG(LogLevel::Info, ("MediaStreamTrack {} became {}", fmt::ptr(this),
                       aNewState ? "muted" : "unmuted"));

  mMuted = aNewState;

  if (Ended()) {
    return;
  }

  nsString eventName = aNewState ? u"mute"_ns : u"unmute"_ns;
  DispatchTrustedEvent(eventName);
}

void MediaStreamTrack::ConstraintsChanged(
    const MediaTrackConstraints& aConstraints) {
  MOZ_ASSERT(NS_IsMainThread());
  mConstraints = aConstraints;
}

void MediaStreamTrack::NotifyEnded() {
  MOZ_ASSERT(mReadyState == MediaStreamTrackState::Ended);

  for (const auto& consumer : mConsumers.Clone()) {
    if (consumer) {
      consumer->NotifyEnded(this);
    } else {
      MOZ_ASSERT_UNREACHABLE("A consumer was not explicitly removed");
      mConsumers.RemoveElement(consumer);
    }
  }
}

void MediaStreamTrack::NotifyEnabledChanged() {
  GetSource().SinkEnabledStateChanged();

  for (const auto& consumer : mConsumers.Clone()) {
    if (consumer) {
      consumer->NotifyEnabledChanged(this, Enabled());
    } else {
      MOZ_ASSERT_UNREACHABLE("A consumer was not explicitly removed");
      mConsumers.RemoveElement(consumer);
    }
  }
}

bool MediaStreamTrack::AddPrincipalChangeObserver(
    PrincipalChangeObserver<MediaStreamTrack>* aObserver) {
  // XXX(Bug 1631371) Check if this should use a fallible operation as it
  // pretended earlier.
  mPrincipalChangeObservers.AppendElement(aObserver);
  return true;
}

bool MediaStreamTrack::RemovePrincipalChangeObserver(
    PrincipalChangeObserver<MediaStreamTrack>* aObserver) {
  return mPrincipalChangeObservers.RemoveElement(aObserver);
}

void MediaStreamTrack::AddConsumer(MediaStreamTrackConsumer* aConsumer) {
  MOZ_ASSERT(!mConsumers.Contains(aConsumer));
  mConsumers.AppendElement(aConsumer);

  // Remove destroyed consumers for cleanliness
  while (mConsumers.RemoveElement(nullptr)) {
    MOZ_ASSERT_UNREACHABLE("A consumer was not explicitly removed");
  }
}

void MediaStreamTrack::RemoveConsumer(MediaStreamTrackConsumer* aConsumer) {
  mConsumers.RemoveElement(aConsumer);

  // Remove destroyed consumers for cleanliness
  while (mConsumers.RemoveElement(nullptr)) {
    MOZ_ASSERT_UNREACHABLE("A consumer was not explicitly removed");
  }
}

void MediaStreamTrack::SetReadyState(MediaStreamTrackState aState) {
  MOZ_ASSERT(!(mReadyState == MediaStreamTrackState::Ended &&
               aState == MediaStreamTrackState::Live),
             "We don't support overriding the ready state from ended to live");

  if (Ended()) {
    return;
  }

  if (mReadyState == MediaStreamTrackState::Live &&
      aState == MediaStreamTrackState::Ended) {
    if (mSource) {
      mSource->UnregisterSink(mSink.get());
    }
    mWatchManager.Shutdown();
    mTrackEnded.DisconnectIfConnected();
    mTrackPrincipalHandle.DisconnectIfConnected();
    if (mPort) {
      mPort->Destroy();
    }
    if (mTrack) {
      mTrack->Destroy();
    }
    mPort = nullptr;
    mTrack = nullptr;
  }

  mReadyState = aState;
}

void MediaStreamTrack::OnTrackEnded() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mTrackEnded);
  if (!GetParentObject()) {
    return;
  }
  // OverrideEnded() fires "ended" synchronously to content JS after setting
  // [[ReadyState]], so call it from its own regular task.
  AbstractThread::MainThread()->Dispatch(
      NewRunnableMethod("MediaStreamTrack::OverrideEnded", this,
                        &MediaStreamTrack::OverrideEnded));
}

void MediaStreamTrack::OverrideEnded() {
  MOZ_ASSERT(NS_IsMainThread());

  if (Ended()) {
    return;
  }

  LOG(LogLevel::Info, ("MediaStreamTrack {} ended", fmt::ptr(this)));

  SetReadyState(MediaStreamTrackState::Ended);

  NotifyEnded();

  DispatchTrustedEvent(u"ended"_ns);
}

void MediaStreamTrack::AddListener(MediaTrackListener* aListener) {
  LOG(LogLevel::Debug, ("MediaStreamTrack {} adding listener {}",
                        fmt::ptr(this), fmt::ptr(aListener)));
  mTrackListeners.AppendElement(aListener);

  if (Ended()) {
    return;
  }
  mTrack->AddListener(aListener);
}

void MediaStreamTrack::RemoveListener(MediaTrackListener* aListener) {
  LOG(LogLevel::Debug, ("MediaStreamTrack {} removing listener {}",
                        fmt::ptr(this), fmt::ptr(aListener)));
  mTrackListeners.RemoveElement(aListener);

  if (Ended()) {
    return;
  }
  mTrack->RemoveListener(aListener);
}

void MediaStreamTrack::AddDirectListener(DirectMediaTrackListener* aListener) {
  LOG(LogLevel::Debug,
      ("MediaStreamTrack {} ({}) adding direct listener {} to "
       "track {}",
       fmt::ptr(this), AsAudioStreamTrack() ? "audio" : "video",
       fmt::ptr(aListener), fmt::ptr(mTrack.get())));
  mDirectTrackListeners.AppendElement(aListener);

  if (Ended()) {
    return;
  }
  mTrack->AddDirectListener(aListener);
}

void MediaStreamTrack::RemoveDirectListener(
    DirectMediaTrackListener* aListener) {
  LOG(LogLevel::Debug,
      ("MediaStreamTrack {} removing direct listener {} from track {}",
       fmt::ptr(this), fmt::ptr(aListener), fmt::ptr(mTrack.get())));
  mDirectTrackListeners.RemoveElement(aListener);

  if (Ended()) {
    return;
  }
  mTrack->RemoveDirectListener(aListener);
}

already_AddRefed<MediaInputPort> MediaStreamTrack::ForwardTrackContentsTo(
    ProcessedMediaTrack* aTrack) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_RELEASE_ASSERT(aTrack);
  return aTrack->AllocateInputPort(mTrack);
}

}  // namespace mozilla::dom

#undef LOG
