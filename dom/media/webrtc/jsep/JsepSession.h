/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef JSEPSESSION_H_
#define JSEPSESSION_H_

#include <string>
#include <vector>

#include "jsep/JsepTransceiver.h"
#include "jsep/JsepTransport.h"
#include "mozilla/Attributes.h"
#include "mozilla/Maybe.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/dom/PeerConnectionObserverEnumsBinding.h"
#include "nsError.h"
#include "nsTArray.h"
#include "sdp/Sdp.h"

namespace mozilla {

// Forward declarations
class JsepCodecDescription;

enum JsepSignalingState {
  kJsepStateStable,
  kJsepStateHaveLocalOffer,
  kJsepStateHaveRemoteOffer,
  kJsepStateHaveLocalPranswer,
  kJsepStateHaveRemotePranswer,
  kJsepStateClosed
};

inline const char* format_as(JsepSignalingState aState) {
  switch (aState) {
    case kJsepStateStable:
      return "stable";
    case kJsepStateHaveLocalOffer:
      return "have-local-offer";
    case kJsepStateHaveRemoteOffer:
      return "have-remote-offer";
    case kJsepStateHaveLocalPranswer:
      return "have-local-pranswer";
    case kJsepStateHaveRemotePranswer:
      return "have-remote-pranswer";
    case kJsepStateClosed:
      return "closed";
  }
  MOZ_CRASH(
      "Garbage JsepSignalingState, this is likely a memory error. Crashing.");
}

enum JsepSdpType {
  kJsepSdpOffer,
  kJsepSdpAnswer,
  kJsepSdpPranswer,
  kJsepSdpRollback
};

enum JsepDescriptionPendingOrCurrent {
  kJsepDescriptionCurrent,
  kJsepDescriptionPending,
  kJsepDescriptionPendingOrCurrent
};

struct JsepOAOptions {};
struct JsepOfferOptions : public JsepOAOptions {
  Maybe<size_t> mOfferToReceiveAudio;
  Maybe<size_t> mOfferToReceiveVideo;
  Maybe<bool> mIceRestart;  // currently ignored by JsepSession
};
struct JsepAnswerOptions : public JsepOAOptions {};

enum JsepBundlePolicy { kBundleBalanced, kBundleMaxCompat, kBundleMaxBundle };

enum JsepRtcpMuxPolicy { kRtcpMuxRequire, kRtcpMuxNegotiate };

enum JsepMediaType { kNone = 0, kAudio, kVideo, kAudioVideo };

struct JsepExtmapMediaType {
  JsepMediaType mMediaType;
  SdpExtmapAttributeList::Extmap mExtmap;
};

class JsepSession {
 public:
  explicit JsepSession(const std::string& name)
      : mName(name), mState(kJsepStateStable), mNegotiations(0) {}
  virtual ~JsepSession() {}

  virtual UniquePtr<JsepSession> Clone() const = 0;

  virtual nsresult Init() = 0;

  // Accessors for basic properties.
  virtual const std::string& GetName() const { return mName; }
  virtual JsepSignalingState GetState() const { return mState; }
  virtual uint32_t GetNegotiations() const { return mNegotiations; }

  // Set up the ICE And DTLS data.
  virtual nsresult SetBundlePolicy(JsepBundlePolicy policy) = 0;
  virtual nsresult SetRtcpMuxPolicy(JsepRtcpMuxPolicy policy) = 0;
  virtual JsepRtcpMuxPolicy GetRtcpMuxPolicy() const = 0;
  virtual bool RemoteIsIceLite() const = 0;
  virtual std::vector<std::string> GetIceOptions() const = 0;

  virtual nsresult AddDtlsFingerprint(const nsACString& algorithm,
                                      const std::vector<uint8_t>& value) = 0;

  virtual nsresult AddRtpExtension(
      JsepMediaType mediaType, const nsACString& extensionName,
      SdpDirectionAttribute::Direction direction) = 0;
  virtual nsresult AddAudioRtpExtension(
      const nsACString& extensionName,
      SdpDirectionAttribute::Direction direction) = 0;
  virtual nsresult AddVideoRtpExtension(
      const nsACString& extensionName,
      SdpDirectionAttribute::Direction direction) = 0;
  virtual nsresult AddAudioVideoRtpExtension(
      const nsACString& extensionName,
      SdpDirectionAttribute::Direction direction) = 0;

  virtual Span<UniquePtr<JsepCodecDescription>> Codecs() = 0;

  template <class UnaryFunction>
  void ForEachCodec(UnaryFunction& function) {
    Span codecs = Codecs();
    std::for_each(codecs.begin(), codecs.end(), function);
    for (auto& transceiver : GetTransceivers()) {
      transceiver.mSendTrack.ForEachCodec(function);
      transceiver.mRecvTrack.ForEachCodec(function);
    }
  }

  template <class BinaryPredicate>
  void SortCodecs(BinaryPredicate& sorter) {
    Span codecs = Codecs();
    std::stable_sort(codecs.begin(), codecs.end(), sorter);
    for (auto& transceiver : GetTransceivers()) {
      transceiver.mSendTrack.SortCodecs(sorter);
      transceiver.mRecvTrack.SortCodecs(sorter);
    }
  }

  // Would be nice to have this return a Maybe containing the return of
  // |aFunction|, but Maybe cannot contain a void.
  template <typename UnaryFunction>
  bool ApplyToTransceiver(const std::string& aId, UnaryFunction&& aFunction) {
    for (auto& transceiver : GetTransceivers()) {
      if (transceiver.GetUuid() == aId) {
        std::forward<UnaryFunction>(aFunction)(transceiver);
        return true;
      }
    }
    return false;
  }

  template <typename UnaryFunction>
  void ForEachTransceiver(UnaryFunction&& aFunction) {
    for (auto& transceiver : GetTransceivers()) {
      std::forward<UnaryFunction>(aFunction)(transceiver);
    }
  }

  template <typename UnaryFunction>
  void ForEachTransceiver(UnaryFunction&& aFunction) const {
    for (const auto& transceiver : GetTransceivers()) {
      std::forward<UnaryFunction>(aFunction)(transceiver);
    }
  }

  Maybe<const JsepTransceiver> GetTransceiver(const std::string& aId) const {
    for (const auto& transceiver : GetTransceivers()) {
      if (transceiver.GetUuid() == aId) {
        return Some(transceiver);
      }
    }
    return Nothing();
  }

  template <typename MatchFunction>
  Maybe<const JsepTransceiver> FindTransceiver(MatchFunction&& aFunc) const {
    for (const auto& transceiver : GetTransceivers()) {
      if (std::forward<MatchFunction>(aFunc)(transceiver)) {
        return Some(transceiver);
      }
    }
    return Nothing();
  }

  bool SetTransceiver(const JsepTransceiver& aNew) {
    return ApplyToTransceiver(aNew.GetUuid(),
                              [aNew](JsepTransceiver& aOld) { aOld = aNew; });
  }

  virtual void AddTransceiver(const JsepTransceiver& transceiver) = 0;

  class Result {
   public:
    Result() = default;
    MOZ_IMPLICIT Result(dom::PCError aError) : mError(Some(aError)) {}
    Result(dom::PCError aError, const std::string& aErrorDetail,
           Maybe<size_t> aSdpLineNumber = Nothing())
        : mError(Some(aError)),
          mErrorDetail(Some(aErrorDetail)),
          mSdpLineNumber(aSdpLineNumber) {}
    Maybe<dom::PCError> mError;
    Maybe<std::string> mErrorDetail;
    Maybe<size_t> mSdpLineNumber;
  };

  // Basic JSEP operations.
  virtual Result CreateOffer(const JsepOfferOptions& options,
                             std::string* offer) = 0;
  virtual Result CreateAnswer(const JsepAnswerOptions& options,
                              std::string* answer) = 0;
  virtual std::string GetLocalDescription(
      JsepDescriptionPendingOrCurrent type) const = 0;
  virtual std::string GetRemoteDescription(
      JsepDescriptionPendingOrCurrent type) const = 0;
  virtual Result SetLocalDescription(JsepSdpType type,
                                     const std::string& sdp) = 0;
  virtual Result SetRemoteDescription(JsepSdpType type,
                                      const std::string& sdp) = 0;
  virtual Result AddRemoteIceCandidate(const std::string& candidate,
                                       const std::string& mid,
                                       const Maybe<uint16_t>& level,
                                       const std::string& ufrag,
                                       std::string* transportId) = 0;
  virtual nsresult AddLocalIceCandidate(const std::string& candidate,
                                        const std::string& transportId,
                                        const std::string& ufrag,
                                        uint16_t* level, std::string* mid,
                                        bool* skipped) = 0;
  virtual nsresult UpdateDefaultCandidate(
      const std::string& defaultCandidateAddr, uint16_t defaultCandidatePort,
      const std::string& defaultRtcpCandidateAddr,
      uint16_t defaultRtcpCandidatePort, const std::string& transportId) = 0;
  virtual nsresult Close() = 0;

  // ICE controlling or controlled
  virtual bool IsIceControlling() const = 0;
  virtual Maybe<bool> IsPendingOfferer() const = 0;
  virtual Maybe<bool> IsCurrentOfferer() const = 0;
  virtual bool IsIceRestarting() const = 0;
  virtual std::set<std::pair<std::string, std::string>> GetLocalIceCredentials()
      const = 0;

  virtual const std::string GetLastError() const { return "Error"; }

  virtual const std::vector<std::pair<size_t, std::string>>&
  GetLastSdpParsingErrors() const = 0;

  static const char* GetStateStr(JsepSignalingState state) {
    static const char* states[] = {"stable",
                                   "have-local-offer",
                                   "have-remote-offer",
                                   "have-local-pranswer",
                                   "have-remote-pranswer",
                                   "closed"};

    return states[state];
  }

  virtual bool CheckNegotiationNeeded() const = 0;

  void CountTransceivers(
      uint16_t (&recvonly)[SdpMediaSection::kMediaTypes],
      uint16_t (&sendonly)[SdpMediaSection::kMediaTypes],
      uint16_t (&sendrecv)[SdpMediaSection::kMediaTypes]) const {
    memset(recvonly, 0, sizeof(recvonly));
    memset(sendonly, 0, sizeof(sendonly));
    memset(sendrecv, 0, sizeof(sendrecv));

    for (const auto& transceiver : GetTransceivers()) {
      if (!transceiver.IsNegotiated()) {
        continue;
      }
      if ((transceiver.mRecvTrack.GetActive() &&
           transceiver.mSendTrack.GetActive()) ||
          transceiver.GetMediaType() == SdpMediaSection::kApplication) {
        ++sendrecv[transceiver.GetMediaType()];
      } else if (transceiver.mRecvTrack.GetActive()) {
        ++recvonly[transceiver.GetMediaType()];
      } else if (transceiver.mSendTrack.GetActive()) {
        ++sendonly[transceiver.GetMediaType()];
      }
    }
  }

  void CountTracksAndDatachannels(
      uint16_t (&receiving)[SdpMediaSection::kMediaTypes],
      uint16_t (&sending)[SdpMediaSection::kMediaTypes]) const {
    memset(receiving, 0, sizeof(receiving));
    memset(sending, 0, sizeof(sending));

    for (const auto& transceiver : GetTransceivers()) {
      if (transceiver.mRecvTrack.GetActive() ||
          transceiver.GetMediaType() == SdpMediaSection::kApplication) {
        receiving[transceiver.mRecvTrack.GetMediaType()]++;
      }

      if (transceiver.mSendTrack.GetActive() ||
          transceiver.GetMediaType() == SdpMediaSection::kApplication) {
        sending[transceiver.mSendTrack.GetMediaType()]++;
      }
    }
  }

  virtual void SetDefaultCodecs(
      const nsTArray<UniquePtr<JsepCodecDescription>>& aPreferredCodecs) = 0;

  // See Bug 1642419, this can be removed when all sites are working with RTX.
  void SetRtxIsAllowed(bool aRtxIsAllowed) { mRtxIsAllowed = aRtxIsAllowed; }

  virtual void SetAlwaysNegotiateDataChannels(
      bool aAlwaysNegotiateDataChannels) = 0;

 protected:
  friend class JsepSessionTest;
  // Returns transceivers in the order they were added.
  virtual std::vector<JsepTransceiver>& GetTransceivers() = 0;
  virtual const std::vector<JsepTransceiver>& GetTransceivers() const = 0;

  const std::string mName;
  JsepSignalingState mState;
  uint32_t mNegotiations;

  // See Bug 1642419, this can be removed when all sites are working with RTX.
  bool mRtxIsAllowed = true;
};

}  // namespace mozilla

#endif
