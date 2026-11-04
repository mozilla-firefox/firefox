/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef JSEPTRACK_H_
#define JSEPTRACK_H_

#include <mozilla/UniquePtr.h>

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "jsep/JsepTrackEncoding.h"
#include "jsep/SsrcGenerator.h"
#include "libwebrtcglue/RtpRtcpConfig.h"
#include "mozilla/Preferences.h"
#include "nsError.h"
#include "sdp/Sdp.h"
#include "sdp/SdpAttribute.h"
#include "sdp/SdpMediaSection.h"
namespace mozilla {

class JsepTrackNegotiatedDetails {
 public:
  JsepTrackNegotiatedDetails()
      : mTias(0), mRtpRtcpConf(webrtc::RtcpMode::kCompound, true) {}

  JsepTrackNegotiatedDetails(const JsepTrackNegotiatedDetails& orig)
      : mExtmap(orig.mExtmap),
        mTias(orig.mTias),
        mRtpRtcpConf(orig.mRtpRtcpConf) {
    for (const auto& encoding : orig.mEncodings) {
      mEncodings.emplace_back(new JsepTrackEncoding(*encoding));
    }
  }

  size_t GetEncodingCount() const { return mEncodings.size(); }

  const JsepTrackEncoding& GetEncoding(size_t index) const {
    MOZ_RELEASE_ASSERT(index < mEncodings.size());
    return *mEncodings[index];
  }

  void TruncateEncodings(size_t aSize) {
    if (mEncodings.size() < aSize) {
      MOZ_ASSERT(false);
      return;
    }
    mEncodings.resize(aSize);
  }

  const SdpExtmapAttributeList::Extmap* GetExt(
      const nsACString& ext_name) const {
    auto it = mExtmap.find(nsCString(ext_name));
    if (it != mExtmap.end()) {
      return &it->second;
    }
    return nullptr;
  }

  void ForEachRTPHeaderExtension(
      const std::function<void(const SdpExtmapAttributeList::Extmap& extmap)>&
          fn) const {
    for (const auto& entry : mExtmap) {
      fn(entry.second);
    }
  }

  uint32_t GetTias() const { return mTias; }

  RtpRtcpConfig GetRtpRtcpConfig() const { return mRtpRtcpConf; }

 private:
  friend class JsepTrack;

  std::map<nsCString, SdpExtmapAttributeList::Extmap> mExtmap;
  std::vector<UniquePtr<JsepTrackEncoding>> mEncodings;
  uint32_t mTias;  // bits per second
  RtpRtcpConfig mRtpRtcpConf;
};

class JsepTrack {
 public:
  JsepTrack(mozilla::SdpMediaSection::MediaTypeInfo type,
            sdp::Direction direction)
      : mType(std::move(type)),
        mDirection(direction),
        mActive(false),
        mRemoteSetSendBit(false) {}

  virtual ~JsepTrack() = default;

  void UpdateStreamIds(const std::vector<std::string>& streamIds) {
    mStreamIds = streamIds;
  }

  void SetTrackId(const std::string& aTrackId) { mTrackId = aTrackId; }

  void ClearStreamIds() { mStreamIds.clear(); }

  void RecvTrackSetRemote(const Sdp& aSdp, const SdpMediaSection& aMsection,
                          const std::vector<uint32_t>& aOwnSendSsrcs = {});
  void RecvTrackSetLocal(const SdpMediaSection& aMsection);

  // This is called whenever a remote description is set; we do not wait for
  // offer/answer to complete, since there's nothing to actually negotiate here.
  void SendTrackSetRemote(SsrcGenerator& aSsrcGenerator,
                          const SdpMediaSection& aRemoteMsection);

  JsepTrack(const JsepTrack& orig) { *this = orig; }

  JsepTrack(JsepTrack&& orig) = default;
  JsepTrack& operator=(JsepTrack&& rhs) = default;

  JsepTrack& operator=(const JsepTrack& rhs) {
    if (this != &rhs) {
      mType = rhs.mType;
      mStreamIds = rhs.mStreamIds;
      mTrackId = rhs.mTrackId;
      mCNAME = rhs.mCNAME;
      mDirection = rhs.mDirection;
      mRids = rhs.mRids;
      mSsrcs = rhs.mSsrcs;
      mSsrcToRtxSsrc = rhs.mSsrcToRtxSsrc;
      mActive = rhs.mActive;
      mRemoteSetSendBit = rhs.mRemoteSetSendBit;
      mReceptive = rhs.mReceptive;
      mMaxEncodings = rhs.mMaxEncodings;
      mInHaveRemote = rhs.mInHaveRemote;
      mRtxIsAllowed = rhs.mRtxIsAllowed;
      mUsePreferredCodecsOrder = rhs.mUsePreferredCodecsOrder;
      mFecCodec = rhs.mFecCodec;
      mAudioPreferredCodec = rhs.mAudioPreferredCodec;
      mVideoPreferredCodec = rhs.mVideoPreferredCodec;
      mUniqueReceivePayloadTypes = rhs.mUniqueReceivePayloadTypes;
      mReceivePayloadTypes = rhs.mReceivePayloadTypes;
      mOtherReceivePayloadTypes = rhs.mOtherReceivePayloadTypes;

      mPrototypeCodecs.clear();
      for (const auto& codec : rhs.mPrototypeCodecs) {
        mPrototypeCodecs.emplace_back(codec->Clone());
      }
      mEarlyRecvCodecs.clear();
      for (const auto& codec : rhs.mEarlyRecvCodecs) {
        mEarlyRecvCodecs.emplace_back(codec->Clone());
      }
      mEarlyRtpExtensions = rhs.mEarlyRtpExtensions;
      if (rhs.mNegotiatedDetails) {
        mNegotiatedDetails = std::make_unique<JsepTrackNegotiatedDetails>(
            *rhs.mNegotiatedDetails);
      }
    }
    return *this;
  }

  virtual mozilla::SdpMediaSection::MediaType GetMediaType() const {
    return mType.type;
  }

  virtual mozilla::SdpMediaSection::MediaTypeInfo GetMediaTypeInfo() const {
    return mType;
  }

  virtual const std::vector<std::string>& GetStreamIds() const {
    return mStreamIds;
  }

  virtual const std::string& GetCNAME() const { return mCNAME; }

  virtual void SetCNAME(const std::string& cname) { mCNAME = cname; }

  virtual sdp::Direction GetDirection() const { return mDirection; }

  virtual const std::vector<uint32_t>& GetSsrcs() const { return mSsrcs; }

  virtual std::vector<uint32_t> GetRtxSsrcs() const;

  virtual void EnsureSsrcs(SsrcGenerator& ssrcGenerator, size_t aNumber);

  bool GetActive() const { return mActive; }

  void SetActive(bool active) { mActive = active; }

  bool GetRemoteSetSendBit() const { return mRemoteSetSendBit; }
  bool GetReceptive() const { return mReceptive; }

  void PopulatePreferredCodecs(
      const nsTArray<UniquePtr<JsepCodecDescription>>& aPreferredCodecs,
      bool aUsePreferredCodecsOrder);

  virtual void PopulateCodecs(
      const nsTArray<UniquePtr<JsepCodecDescription>>& prototype,
      bool aUsePreferredCodecsOrder = false);

  template <class UnaryFunction>
  void ForEachCodec(UnaryFunction func) {
    std::for_each(mPrototypeCodecs.begin(), mPrototypeCodecs.end(), func);
  }

  template <class UnaryFunction>
  void ForEachEarlyRecvCodec(UnaryFunction func) {
    std::for_each(mEarlyRecvCodecs.begin(), mEarlyRecvCodecs.end(), func);
  }

  bool HasEarlyRecvCodecs() const { return !mEarlyRecvCodecs.empty(); }

  // For a receive track, the recv-usable RTP header extensions from the
  // local m-section last passed to RecvTrackSetLocal(). Used for early
  // media (bug 2019381) instead of GetNegotiatedDetails(), which is stale
  // during a pending offer.
  const std::vector<SdpExtmapAttributeList::Extmap>& GetEarlyRtpExtensions()
      const {
    return mEarlyRtpExtensions;
  }

  template <class BinaryPredicate>
  void SortCodecs(BinaryPredicate sorter) {
    std::stable_sort(mPrototypeCodecs.begin(), mPrototypeCodecs.end(), sorter);
  }

  // These two are non-const because this is where ssrcs are chosen.
  virtual void AddToOffer(SsrcGenerator& ssrcGenerator, SdpMediaSection* offer);
  virtual void AddToAnswer(const SdpMediaSection& offer,
                           SsrcGenerator& ssrcGenerator,
                           SdpMediaSection* answer);

  virtual nsresult Negotiate(const SdpMediaSection& answer,
                             const SdpMediaSection& remote,
                             const SdpMediaSection& local);
  static void SetReceivePayloadTypes(std::vector<JsepTrack*>& tracks,
                                     bool localOffer = false);
  virtual void GetNegotiatedPayloadTypes(
      std::vector<uint16_t>* payloadTypes) const;

  // This will be set when negotiation is carried out.
  virtual const JsepTrackNegotiatedDetails* GetNegotiatedDetails() const {
    if (mNegotiatedDetails) {
      return mNegotiatedDetails.get();
    }
    return nullptr;
  }

  virtual JsepTrackNegotiatedDetails* GetNegotiatedDetails() {
    if (mNegotiatedDetails) {
      return mNegotiatedDetails.get();
    }
    return nullptr;
  }

  virtual void ClearNegotiatedDetails() { mNegotiatedDetails.reset(); }

  void SetRids(const std::vector<std::string>& aRids);
  void ClearRids() { mRids.clear(); }
  const std::vector<std::string>& GetRids() const { return mRids; }

  void AddToMsection(const std::vector<std::string>& aRids,
                     sdp::Direction direction, SsrcGenerator& ssrcGenerator,
                     bool rtxEnabled, SdpMediaSection* msection);

  // See Bug 1642419, this can be removed when all sites are working with RTX.
  void SetRtxIsAllowed(bool aRtxIsAllowed) { mRtxIsAllowed = aRtxIsAllowed; }

  void SetMaxEncodings(size_t aMax);
  bool IsInHaveRemote() const { return mInHaveRemote; }

  const std::string& GetFecCodecName() const { return mFecCodec; }
  const std::string& GetAudioPreferredCodec() const {
    return mAudioPreferredCodec;
  }
  const std::string& GetVideoPreferredCodec() const {
    return mVideoPreferredCodec;
  }

  void ResetReceivePayloadTypes() {
    mUniqueReceivePayloadTypes.clear();
    mOtherReceivePayloadTypes.clear();
  }

  const std::vector<uint8_t>& GetUniqueReceivePayloadTypes() const {
    return mUniqueReceivePayloadTypes;
  }

  const std::vector<uint8_t>& GetOtherReceivePayloadTypes() const {
    return mOtherReceivePayloadTypes;
  }

 private:
  std::vector<UniquePtr<JsepCodecDescription>> GetCodecClones() const;
  static void EnsureNoDuplicatePayloadTypes(
      std::vector<UniquePtr<JsepCodecDescription>>* codecs);
  static void GetPayloadTypes(
      const std::vector<UniquePtr<JsepCodecDescription>>& codecs,
      std::vector<uint16_t>* pts);
  void AddToMsection(const std::vector<UniquePtr<JsepCodecDescription>>& codecs,
                     SdpMediaSection* msection) const;
  void GetRids(const SdpMediaSection& msection, sdp::Direction direction,
               std::vector<SdpRidAttributeList::Rid>* rids) const;
  void CreateEncodings(
      const SdpMediaSection& remote,
      const std::vector<UniquePtr<JsepCodecDescription>>& negotiatedCodecs,
      JsepTrackNegotiatedDetails* details);
  // Identifies codecs we want to store for logging purposes.
  void MaybeStoreCodecToLog(const std::string& codec,
                            SdpMediaSection::MediaType type);
  virtual std::vector<UniquePtr<JsepCodecDescription>> NegotiateCodecs(
      const SdpMediaSection& remote, bool remoteIsOffer,
      Maybe<const SdpMediaSection&> local);

  void UpdateSsrcs(SsrcGenerator& ssrcGenerator, size_t encodings);
  void PruneSsrcs(size_t aNumSsrcs);
  bool IsRtxEnabled(
      const std::vector<UniquePtr<JsepCodecDescription>>& codecs) const;

  // Carries type, also carries the extended type info for unknown media types
  mozilla::SdpMediaSection::MediaTypeInfo mType;
  // These are the ids that everyone outside of JsepSession care about
  std::vector<std::string> mStreamIds;
  std::string mTrackId;
  std::string mCNAME;
  sdp::Direction mDirection;
  std::vector<UniquePtr<JsepCodecDescription>> mPrototypeCodecs;
  // For a receive track, a snapshot of mPrototypeCodecs taken at the moment
  // they were last written into an offer (see AddToOffer()). Used for early
  // media (bug 2019381) instead of mPrototypeCodecs directly.
  std::vector<UniquePtr<JsepCodecDescription>> mEarlyRecvCodecs;
  // For a receive track, see GetEarlyRtpExtensions().
  std::vector<SdpExtmapAttributeList::Extmap> mEarlyRtpExtensions;
  // List of rids. May be initially populated from JS, or from a remote SDP.
  // Can be updated by remote SDP. If no negotiation has taken place at all,
  // this will be empty. If negotiation has taken place, but no simulcast
  // attr was negotiated, this will contain the empty string as a single
  // element. If a simulcast attribute was negotiated, this will contain the
  // negotiated rids.
  std::vector<std::string> mRids;
  UniquePtr<JsepTrackNegotiatedDetails> mNegotiatedDetails;
  // Storage of mSsrcs and mSsrcToRtxSsrc could be improved, see Bug 1990364
  // For send tracks: the SSRCs this endpoint will transmit on, populated by
  // UpdateSsrcs(). For recv tracks: SSRCs from the remote's a=ssrc lines,
  // populated by RecvTrackSetRemote(); any that duplicate our own send SSRCs
  // (aOwnSendSsrcs) are filtered out to prevent EnsureLocalSSRC() from
  // regenerating our send SSRC to a value the peer never negotiated.
  std::vector<uint32_t> mSsrcs;
  std::map<uint32_t, uint32_t> mSsrcToRtxSsrc;
  bool mActive;
  bool mRemoteSetSendBit;
  // This is used to drive RTCRtpTransceiver.[[Receptive]]. Basically, this
  // denotes whether we are prepared to receive RTP. When we apply a local
  // description with the recv bit set, this is set to true, even if we have
  // not seen the remote description yet. If we apply either a local or remote
  // description without the recv bit set (from our perspective), this is set
  // to false.
  bool mReceptive = false;
  size_t mMaxEncodings = 3;
  bool mInHaveRemote = false;

  // See Bug 1642419, this can be removed when all sites are working with RTX.
  bool mRtxIsAllowed = true;

  // Used with setCodecPreferences to determine if an answer created should
  // match the order of preferred codecs.
  bool mUsePreferredCodecsOrder = false;

  // Codec names for logging
  std::string mFecCodec;
  std::string mAudioPreferredCodec;
  std::string mVideoPreferredCodec;

  // Only the unique PTs we are willing to receive, not necessarily all PTs.
  // Used for matching SSRC to PT as only unique PTs support for this.
  std::vector<uint8_t> mUniqueReceivePayloadTypes;
  std::vector<uint16_t> mReceivePayloadTypes;
  // Payload types that are registered to some track but not us.
  std::vector<uint8_t> mOtherReceivePayloadTypes;
};

}  // namespace mozilla

#endif
