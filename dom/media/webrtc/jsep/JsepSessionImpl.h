/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef JSEPSESSIONIMPL_H_
#define JSEPSESSIONIMPL_H_

#include <map>
#include <set>
#include <string>
#include <vector>

#include "jsep/JsepCodecDescription.h"
#include "jsep/JsepSession.h"
#include "jsep/JsepTrack.h"
#include "jsep/JsepTransceiver.h"
#include "jsep/SsrcGenerator.h"
#include "sdp/HybridSdpParser.h"
#include "sdp/SdpHelper.h"

namespace mozilla {

// JsepSessionImpl members that have default copy c'tors, to simplify the
// implementation of the copy c'tor for JsepSessionImpl
class JsepSessionCopyableStuff {
 protected:
  struct JsepDtlsFingerprint {
    std::string mAlgorithm;
    std::vector<uint8_t> mValue;
  };

  Maybe<bool> mIsPendingOfferer;
  Maybe<bool> mIsCurrentOfferer;
  bool mIceControlling = false;
  std::string mIceUfrag;
  std::string mIcePwd;
  std::string mOldIceUfrag;
  std::string mOldIcePwd;
  bool mRemoteIsIceLite = false;
  std::vector<std::string> mIceOptions;
  JsepBundlePolicy mBundlePolicy = kBundleBalanced;
  JsepRtcpMuxPolicy mRtcpMuxPolicy = kRtcpMuxNegotiate;
  bool mAlwaysNegotiateDataChannels = false;
  std::vector<JsepDtlsFingerprint> mDtlsFingerprints;
  uint64_t mSessionId = 0;
  uint64_t mSessionVersion = 0;
  size_t mMidCounter = 0;
  std::set<std::string> mUsedMids;
  size_t mTransportIdCounter = 0;
  std::vector<JsepExtmapMediaType> mRtpExtensions;
  std::set<uint16_t> mExtmapEntriesEverUsed;
  std::map<uint16_t, nsCString> mExtmapEntriesEverNegotiated;
  std::string mDefaultRemoteStreamId;
  std::string mCNAME;
  // Used to prevent duplicate local SSRCs. Not used to prevent local/remote or
  // remote-only duplication, which will be important for EKT but not now.
  std::set<uint32_t> mSsrcs;
  std::string mLastError;
  std::vector<std::pair<size_t, std::string>> mLastSdpParsingErrors;
  bool mEncodeTrackId = true;
  SsrcGenerator mSsrcGenerator;
  // !!!NOT INDEXED BY LEVEL!!! The level mapping is done with
  // JsepTransceiver::mLevel. The keys are UUIDs.
  std::vector<JsepTransceiver> mTransceivers;
  // So we can rollback. Not as simple as just going back to the old, though...
  std::vector<JsepTransceiver> mOldTransceivers;
};

class JsepSessionImpl : public JsepSession, public JsepSessionCopyableStuff {
 public:
  JsepSessionImpl(const std::string& name, UniquePtr<JsepUuidGenerator> uuidgen)
      : JsepSession(name),
        mUuidGen(std::move(uuidgen)),
        mSdpHelper(&mLastError),
        mParser(MakeUnique<HybridSdpParser>()) {}

  JsepSessionImpl(const JsepSessionImpl& aOrig);

  UniquePtr<JsepSession> Clone() const override {
    return MakeUnique<JsepSessionImpl>(*this);
  }

  // Implement JsepSession methods.
  virtual nsresult Init() override;

  nsresult SetBundlePolicy(JsepBundlePolicy policy) override;
  nsresult SetRtcpMuxPolicy(JsepRtcpMuxPolicy policy) override;
  JsepRtcpMuxPolicy GetRtcpMuxPolicy() const override { return mRtcpMuxPolicy; }
  void SetAlwaysNegotiateDataChannels(
      bool aAlwaysNegotiateDataChannels) override {
    mAlwaysNegotiateDataChannels = aAlwaysNegotiateDataChannels;
  }

  virtual bool RemoteIsIceLite() const override { return mRemoteIsIceLite; }

  virtual std::vector<std::string> GetIceOptions() const override {
    return mIceOptions;
  }

  virtual nsresult AddDtlsFingerprint(
      const nsACString& algorithm, const std::vector<uint8_t>& value) override;

  virtual nsresult AddRtpExtension(
      JsepMediaType mediaType, const nsACString& extensionName,
      SdpDirectionAttribute::Direction direction) override;
  virtual nsresult AddAudioRtpExtension(
      const nsACString& extensionName,
      SdpDirectionAttribute::Direction direction =
          SdpDirectionAttribute::Direction::kSendrecv) override;

  virtual nsresult AddVideoRtpExtension(
      const nsACString& extensionName,
      SdpDirectionAttribute::Direction direction =
          SdpDirectionAttribute::Direction::kSendrecv) override;

  virtual nsresult AddAudioVideoRtpExtension(
      const nsACString& extensionName,
      SdpDirectionAttribute::Direction direction =
          SdpDirectionAttribute::Direction::kSendrecv) override;

  virtual Span<UniquePtr<JsepCodecDescription>> Codecs() override {
    return Span(mSupportedCodecs);
  }

  virtual Result CreateOffer(const JsepOfferOptions& options,
                             std::string* offer) override;

  virtual Result CreateAnswer(const JsepAnswerOptions& options,
                              std::string* answer) override;

  virtual std::string GetLocalDescription(
      JsepDescriptionPendingOrCurrent type) const override;

  virtual std::string GetRemoteDescription(
      JsepDescriptionPendingOrCurrent type) const override;

  virtual Result SetLocalDescription(JsepSdpType type,
                                     const std::string& sdp) override;

  virtual Result SetRemoteDescription(JsepSdpType type,
                                      const std::string& sdp) override;

  virtual Result AddRemoteIceCandidate(const std::string& candidate,
                                       const std::string& mid,
                                       const Maybe<uint16_t>& level,
                                       const std::string& ufrag,
                                       std::string* transportId) override;

  virtual nsresult AddLocalIceCandidate(const std::string& candidate,
                                        const std::string& transportId,
                                        const std::string& ufrag,
                                        uint16_t* level, std::string* mid,
                                        bool* skipped) override;

  virtual nsresult UpdateDefaultCandidate(
      const std::string& defaultCandidateAddr, uint16_t defaultCandidatePort,
      const std::string& defaultRtcpCandidateAddr,
      uint16_t defaultRtcpCandidatePort,
      const std::string& transportId) override;

  virtual nsresult Close() override;

  virtual const std::string GetLastError() const override;

  virtual const std::vector<std::pair<size_t, std::string>>&
  GetLastSdpParsingErrors() const override;

  virtual bool IsIceControlling() const override { return mIceControlling; }

  virtual Maybe<bool> IsPendingOfferer() const override {
    return mIsPendingOfferer;
  }

  virtual Maybe<bool> IsCurrentOfferer() const override {
    return mIsCurrentOfferer;
  }

  virtual bool IsIceRestarting() const override {
    return !mOldIceUfrag.empty();
  }

  virtual std::set<std::pair<std::string, std::string>> GetLocalIceCredentials()
      const override;

  virtual void AddTransceiver(const JsepTransceiver& transceiver) override;

  virtual bool CheckNegotiationNeeded() const override;

  virtual void SetDefaultCodecs(const nsTArray<UniquePtr<JsepCodecDescription>>&
                                    aPreferredCodecs) override;

 private:
  friend class JsepSessionTest;
  virtual const std::vector<JsepTransceiver>& GetTransceivers() const override {
    return mTransceivers;
  }

  virtual std::vector<JsepTransceiver>& GetTransceivers() override {
    return mTransceivers;
  }

  // Non-const so it can set mLastError
  nsresult CreateGenericSDP(UniquePtr<Sdp>* sdp);
  void AddExtmap(SdpMediaSection* msection);
  std::vector<SdpExtmapAttributeList::Extmap> GetRtpExtensions(
      const SdpMediaSection& msection);
  std::string GetNewMid();

  void AddCommonExtmaps(const SdpMediaSection& remoteMsection,
                        SdpMediaSection* msection);
  uint16_t GetNeverUsedExtmapEntry();
  nsresult SetupIds();
  void SetState(JsepSignalingState state);
  // Non-const so it can set mLastError
  JsepSession::Result ParseSdp(const std::string& sdp, UniquePtr<Sdp>* parsedp);
  nsresult SetLocalDescriptionOffer(UniquePtr<Sdp> offer);
  nsresult SetLocalDescriptionAnswer(JsepSdpType type, UniquePtr<Sdp> answer);
  nsresult SetRemoteDescriptionOffer(UniquePtr<Sdp> offer);
  nsresult SetRemoteDescriptionAnswer(JsepSdpType type, UniquePtr<Sdp> answer);
  JsepSession::Result ValidateLocalDescription(const Sdp& description,
                                               JsepSdpType type);
  JsepSession::Result ValidateRemoteDescription(const Sdp& description);
  JsepSession::Result ValidateOffer(const Sdp& offer);
  JsepSession::Result ValidateAnswer(const Sdp& offer, const Sdp& answer);
  JsepSession::Result CheckRtcpMux(const Sdp& description);
  nsresult UpdateTransceiversFromRemoteDescription(const Sdp& remote);
  Maybe<JsepTransceiver> GetTransceiverForLevel(size_t level) const;
  Maybe<JsepTransceiver> GetTransceiverForMid(const std::string& mid) const;
  Maybe<JsepTransceiver> GetTransceiverForLocal(size_t level);
  Maybe<JsepTransceiver> GetTransceiverForRemote(
      const SdpMediaSection& msection);
  Maybe<JsepTransceiver> GetTransceiverWithTransport(
      const std::string& transportId) const;
  // The w3c and IETF specs have a lot of "magical" behavior that happens when
  // addTrack is used. This was a deliberate design choice. Sadface.
  Maybe<JsepTransceiver> FindUnassociatedRtpTransceiver(
      SdpMediaSection::MediaType type, bool magic);
  // Called for rollback of local description
  void RollbackLocalOffer();
  // Called for rollback of remote description
  void RollbackRemoteOffer();
  nsresult HandleNegotiatedSession(const UniquePtr<Sdp>& local,
                                   const UniquePtr<Sdp>& remote);
  nsresult AddTransportAttributes(SdpMediaSection* msection,
                                  SdpSetupAttribute::Role dtlsRole);
  nsresult CopyPreviousTransportParams(const Sdp& oldAnswer,
                                       const Sdp& offerersPreviousSdp,
                                       const Sdp& newOffer, Sdp* newLocal);
  void EnsureMsid(Sdp* remote);
  void SetupBundle(Sdp* sdp) const;
  nsresult CreateOfferMsection(const JsepOfferOptions& options,
                               JsepTransceiver& transceiver, Sdp* local);
  nsresult CreateAnswerMsection(const JsepAnswerOptions& options,
                                JsepTransceiver& transceiver,
                                const SdpMediaSection& remoteMsection,
                                Sdp* sdp);
  nsresult DetermineAnswererSetupRole(const SdpMediaSection& remoteMsection,
                                      SdpSetupAttribute::Role* rolep);
  nsresult MakeNegotiatedTransceiver(const SdpMediaSection& remote,
                                     const SdpMediaSection& local,
                                     JsepTransceiver& transceiverOut);
  void EnsureHasOwnTransport(const SdpMediaSection& msection,
                             JsepTransceiver& transceiver);
  void CopyBundleTransports();

  nsresult FinalizeTransport(const SdpAttributeList& remote,
                             const SdpAttributeList& answer,
                             JsepTransport* transport) const;

  nsresult GetNegotiatedBundledMids(SdpHelper::BundledMids* bundledMids);

  mozilla::Sdp* GetParsedLocalDescription(
      JsepDescriptionPendingOrCurrent type) const;
  mozilla::Sdp* GetParsedRemoteDescription(
      JsepDescriptionPendingOrCurrent type) const;
  const Sdp* GetAnswer() const;
  void SetIceRestarting(bool restarting);

  void InitTransceiver(JsepTransceiver& aTransceiver);

  UniquePtr<JsepUuidGenerator> mUuidGen;
  UniquePtr<Sdp> mGeneratedOffer;   // Created but not set.
  UniquePtr<Sdp> mGeneratedAnswer;  // Created but not set.
  UniquePtr<Sdp> mCurrentLocalDescription;
  UniquePtr<Sdp> mCurrentRemoteDescription;
  UniquePtr<Sdp> mPendingLocalDescription;
  UniquePtr<Sdp> mPendingRemoteDescription;
  nsTArray<UniquePtr<JsepCodecDescription>> mSupportedCodecs;
  SdpHelper mSdpHelper;
  UniquePtr<SdpParser> mParser;
};

}  // namespace mozilla

#endif
