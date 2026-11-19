/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebrtcCallWrapper.h"

#include "MediaConduitInterface.h"
#include "jsapi/PeerConnectionCtx.h"
#include "libwebrtcglue/WebrtcEnvironmentWrapper.h"

namespace mozilla {

/* static */ RefPtr<WebrtcCallWrapper> WebrtcCallWrapper::Create(
    RefPtr<WebrtcEnvironmentWrapper> aEnvWrapper,
    const dom::RTCStatsTimestampMaker& aTimestampMaker,
    UniquePtr<media::ShutdownBlockingTicket> aShutdownTicket,
    const RefPtr<SharedWebrtcState>& aSharedState) {
  auto videoBitrateAllocatorFactory =
      WrapUnique(webrtc::CreateBuiltinVideoBitrateAllocatorFactory().release());
  RefPtr<WebrtcCallWrapper> wrapper = new WebrtcCallWrapper(
      aSharedState, std::move(videoBitrateAllocatorFactory),
      std::move(aEnvWrapper), aTimestampMaker, std::move(aShutdownTicket));

  wrapper->mCallThread->Dispatch(
      NS_NewRunnableFunction(__func__, [wrapper, aSharedState] {
        webrtc::CallConfig config(wrapper->mEnvWrapper->Environment(), nullptr);
        // libwebrtc drops initial frames above 320x240 while the encoder
        // target is below 300kbps. Leave room for packet overhead and an audio
        // stream so that up to 640x480 can be sent from the start.
        config.bitrate_config.start_bitrate_bps = 400000;
        config.audio_state =
            webrtc::AudioState::Create(aSharedState->mAudioStateConfig);
        wrapper->SetCall(
            WrapUnique(webrtc::Call::Create(std::move(config)).release()));
      }));

  return wrapper;
}

void WebrtcCallWrapper::SetCall(UniquePtr<webrtc::Call> aCall) {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  MOZ_ASSERT(!mCall);
  mCall = std::move(aCall);
}

webrtc::Call* WebrtcCallWrapper::Call() const {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  return mCall.get();
}

void WebrtcCallWrapper::UnsetRemoteSSRC(uint32_t aSsrc) {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  for (const auto& conduit : mConduits) {
    conduit->UnsetRemoteSSRC(aSsrc);
  }
}

void WebrtcCallWrapper::RegisterConduit(MediaSessionConduit* conduit) {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  mConduits.insert(conduit);
}

void WebrtcCallWrapper::UnregisterConduit(MediaSessionConduit* conduit) {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  mConduits.erase(conduit);
}

void WebrtcCallWrapper::UpdateReadyTransports(webrtc::MediaType aType,
                                              int aDelta) {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  MOZ_ASSERT(aType == webrtc::MediaType::AUDIO ||
             aType == webrtc::MediaType::VIDEO);
  size_t& ready = aType == webrtc::MediaType::VIDEO ? mNumReadyVideoTransports
                                                    : mNumReadyAudioTransports;
  MOZ_ASSERT_IF(aDelta < 0, ready >= static_cast<size_t>(-aDelta));
  const bool wasReady = ready > 0;
  ready += aDelta;
  if (mCall && wasReady != (ready > 0)) {
    mCall->SignalChannelNetworkState(
        aType, ready > 0 ? webrtc::kNetworkUp : webrtc::kNetworkDown);
  }
}

void WebrtcCallWrapper::Destroy() {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  mCall = nullptr;
  mShutdownTicket = nullptr;
}

const dom::RTCStatsTimestampMaker& WebrtcCallWrapper::GetTimestampMaker()
    const {
  return mClock.mTimestampMaker;
}

WebrtcCallWrapper::~WebrtcCallWrapper() { MOZ_ASSERT(!mCall); }

WebrtcCallWrapper::WebrtcCallWrapper(
    RefPtr<SharedWebrtcState> aSharedState,
    UniquePtr<webrtc::VideoBitrateAllocatorFactory>
        aVideoBitrateAllocatorFactory,
    RefPtr<WebrtcEnvironmentWrapper> aEnvWrapper,
    const dom::RTCStatsTimestampMaker& aTimestampMaker,
    UniquePtr<media::ShutdownBlockingTicket> aShutdownTicket)
    : mSharedState(std::move(aSharedState)),
      mClock(aTimestampMaker),
      mShutdownTicket(std::move(aShutdownTicket)),
      mCallThread(mSharedState->mCallWorkerThread),
      mAudioDecoderFactory(mSharedState->mAudioDecoderFactory),
      mVideoBitrateAllocatorFactory(std::move(aVideoBitrateAllocatorFactory)),
      mEnvWrapper(std::move(aEnvWrapper)) {}

}  // namespace mozilla
