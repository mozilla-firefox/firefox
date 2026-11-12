/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jsep/JsepSessionImpl.h"

#include <algorithm>
#include <set>
#include <string>
#include <utility>

#include "api/rtp_parameters.h"
#include "common/RandomHex.h"
#include "jsep/JsepTrack.h"
#include "jsep/JsepTransport.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/net/DataChannelProtocol.h"
#include "nsDebug.h"
#include "nspr.h"
#include "nss.h"
#include "pk11pub.h"
#include "sdp/HybridSdpParser.h"
#include "sdp/SdpImpl.h"
#include "transport/logging.h"

namespace mozilla {

MOZ_MTLOG_MODULE("jsep")

#define JSEP_SET_ERROR(error)                                 \
  do {                                                        \
    std::ostringstream os;                                    \
    os << error;                                              \
    mLastError = os.str();                                    \
    MOZ_MTLOG(ML_ERROR, "[" << mName << "]: " << mLastError); \
  } while (0);

JsepSessionImpl::JsepSessionImpl(const JsepSessionImpl& aOrig)
    : JsepSession(aOrig),
      JsepSessionCopyableStuff(aOrig),
      mUuidGen(aOrig.mUuidGen->Clone()),
      mGeneratedOffer(aOrig.mGeneratedOffer ? aOrig.mGeneratedOffer->Clone()
                                            : nullptr),
      mGeneratedAnswer(aOrig.mGeneratedAnswer ? aOrig.mGeneratedAnswer->Clone()
                                              : nullptr),
      mCurrentLocalDescription(aOrig.mCurrentLocalDescription
                                   ? aOrig.mCurrentLocalDescription->Clone()
                                   : nullptr),
      mCurrentRemoteDescription(aOrig.mCurrentRemoteDescription
                                    ? aOrig.mCurrentRemoteDescription->Clone()
                                    : nullptr),
      mPendingLocalDescription(aOrig.mPendingLocalDescription
                                   ? aOrig.mPendingLocalDescription->Clone()
                                   : nullptr),
      mPendingRemoteDescription(aOrig.mPendingRemoteDescription
                                    ? aOrig.mPendingRemoteDescription->Clone()
                                    : nullptr),
      mSdpHelper(&mLastError),
      mParser(std::make_unique<HybridSdpParser>()) {
  for (const auto& codec : aOrig.mSupportedCodecs) {
    mSupportedCodecs.EmplaceBack(codec->Clone());
  }
}

nsresult JsepSessionImpl::Init() {
  mLastError.clear();

  MOZ_ASSERT(!mSessionId, "Init called more than once");

  nsresult rv = SetupIds();
  NS_ENSURE_SUCCESS(rv, rv);

  mEncodeTrackId =
      Preferences::GetBool("media.peerconnection.sdp.encode_track_id", true);

  mIceUfrag = GetRandomHex(1);
  mIcePwd = GetRandomHex(4);
  return NS_OK;
}

static void GetIceCredentials(
    const Sdp& aSdp,
    std::set<std::pair<std::string, std::string>>* aCredentials) {
  for (size_t i = 0; i < aSdp.GetMediaSectionCount(); ++i) {
    const SdpAttributeList& attrs = aSdp.GetMediaSection(i).GetAttributeList();
    if (attrs.HasAttribute(SdpAttribute::kIceUfragAttribute) &&
        attrs.HasAttribute(SdpAttribute::kIcePwdAttribute)) {
      aCredentials->insert(
          std::make_pair(attrs.GetIceUfrag(), attrs.GetIcePwd()));
    }
  }
}

std::set<std::pair<std::string, std::string>>
JsepSessionImpl::GetLocalIceCredentials() const {
  std::set<std::pair<std::string, std::string>> result;
  if (mCurrentLocalDescription) {
    GetIceCredentials(*mCurrentLocalDescription, &result);
  }
  if (mPendingLocalDescription) {
    GetIceCredentials(*mPendingLocalDescription, &result);
  }
  return result;
}

void JsepSessionImpl::AddTransceiver(const JsepTransceiver& aTransceiver) {
  mLastError.clear();
  MOZ_MTLOG(ML_DEBUG,
            "[" << mName << "]: Adding transceiver " << aTransceiver.GetUuid());
#ifdef DEBUG
  if (aTransceiver.GetMediaTypeInfo().IsDataChannel()) {
    // Make sure we don't add more than one DataChannel transceiver
    for (const auto& transceiver : mTransceivers) {
      MOZ_ASSERT(!transceiver.GetMediaTypeInfo().IsDataChannel());
    }
  }
#endif
  mTransceivers.push_back(aTransceiver);
  InitTransceiver(mTransceivers.back());
}

void JsepSessionImpl::InitTransceiver(JsepTransceiver& aTransceiver) {
  mLastError.clear();

  if (aTransceiver.GetMediaTypeInfo().IsRtp()) {
    // Make sure we have an ssrc. Might already be set.
    aTransceiver.mSendTrack.EnsureSsrcs(mSsrcGenerator, 1U);
    aTransceiver.mSendTrack.SetCNAME(mCNAME);

    // Make sure we have identifiers for send track, just in case.
    // (man I hate this)
    if (mEncodeTrackId) {
      aTransceiver.mSendTrack.SetTrackId(aTransceiver.GetUuid());
    }
  } else {
    // Datachannel transceivers should always be sendrecv. Just set it instead
    // of asserting.
    aTransceiver.mJsDirection = SdpDirectionAttribute::kSendrecv;
  }

  aTransceiver.mSendTrack.PopulateCodecs(mSupportedCodecs);
  aTransceiver.mRecvTrack.PopulateCodecs(mSupportedCodecs);
  // We do not set mLevel yet, we do that either on createOffer, or setRemote
}

nsresult JsepSessionImpl::SetRtcpMuxPolicy(JsepRtcpMuxPolicy policy) {
  mLastError.clear();
  if (mRtcpMuxPolicy == policy) {
    return NS_OK;
  }
  if (mCurrentLocalDescription) {
    JSEP_SET_ERROR(
        "Changing the rtcpMux policy is only supported before the "
        "first SetLocalDescription.");
    return NS_ERROR_UNEXPECTED;
  }
  mRtcpMuxPolicy = policy;
  return NS_OK;
}

nsresult JsepSessionImpl::SetBundlePolicy(JsepBundlePolicy policy) {
  mLastError.clear();

  if (mBundlePolicy == policy) {
    return NS_OK;
  }

  if (mCurrentLocalDescription) {
    JSEP_SET_ERROR(
        "Changing the bundle policy is only supported before the "
        "first SetLocalDescription.");
    return NS_ERROR_UNEXPECTED;
  }

  mBundlePolicy = policy;
  return NS_OK;
}

nsresult JsepSessionImpl::AddDtlsFingerprint(
    const nsACString& algorithm, const std::vector<uint8_t>& value) {
  mLastError.clear();
  JsepDtlsFingerprint fp;

  fp.mAlgorithm = algorithm;
  fp.mValue = value;

  mDtlsFingerprints.push_back(std::move(fp));

  return NS_OK;
}

nsresult JsepSessionImpl::AddRtpExtension(
    JsepMediaType mediaType, const nsACString& extensionName,
    SdpDirectionAttribute::Direction direction) {
  mLastError.clear();

  for (auto& ext : mRtpExtensions) {
    if (ext.mExtmap.extensionname == extensionName) {
      if (ext.mMediaType != mediaType) {
        ext.mMediaType = JsepMediaType::kAudioVideo;
      }
      if (ext.mExtmap.direction != direction) {
        ext.mExtmap.direction |= direction;
        ext.mExtmap.direction_specified =
            ext.mExtmap.direction != SdpDirectionAttribute::kSendrecv;
      }
      return NS_OK;
    }
  }

  uint16_t freeEntry = GetNeverUsedExtmapEntry();

  if (freeEntry == 0) {
    return NS_ERROR_FAILURE;
  }

  JsepExtmapMediaType extMediaType = {
      mediaType,
      {freeEntry, direction,
       // do we want to specify direction?
       direction != SdpDirectionAttribute::kSendrecv, nsCString(extensionName),
       ""_ns}};

  mRtpExtensions.push_back(std::move(extMediaType));
  return NS_OK;
}

nsresult JsepSessionImpl::AddAudioRtpExtension(
    const nsACString& extensionName,
    SdpDirectionAttribute::Direction direction) {
  return AddRtpExtension(JsepMediaType::kAudio, extensionName, direction);
}

nsresult JsepSessionImpl::AddVideoRtpExtension(
    const nsACString& extensionName,
    SdpDirectionAttribute::Direction direction) {
  return AddRtpExtension(JsepMediaType::kVideo, extensionName, direction);
}

nsresult JsepSessionImpl::AddAudioVideoRtpExtension(
    const nsACString& extensionName,
    SdpDirectionAttribute::Direction direction) {
  return AddRtpExtension(JsepMediaType::kAudioVideo, extensionName, direction);
}

nsresult JsepSessionImpl::CreateOfferMsection(const JsepOfferOptions& options,
                                              JsepTransceiver& transceiver,
                                              Sdp* local) {
  SdpMediaSection::Protocol protocol(
      SdpHelper::GetProtocolForMediaType(transceiver.GetMediaType()));

  const Sdp* answer(GetAnswer());
  const SdpMediaSection* lastAnswerMsection = nullptr;

  if (answer &&
      (local->GetMediaSectionCount() < answer->GetMediaSectionCount())) {
    lastAnswerMsection =
        &answer->GetMediaSection(local->GetMediaSectionCount());
    // Use the protocol the answer used, even if it is not what we would have
    // used.
    protocol = lastAnswerMsection->GetProtocol();
  }

  SdpMediaSection* msection = &local->AddMediaSection(
      transceiver.GetMediaTypeInfo(), transceiver.mJsDirection, 0, protocol,
      sdp::kIPv4, "0.0.0.0");

  // Some of this stuff (eg; mid) sticks around even if disabled
  if (lastAnswerMsection) {
    // An m= section being recycled is generated for a transceiver that can
    // have a different media type ([RFC9429] section 5.2.2). Such a
    // transceiver has not been associated with a mid yet.
    MOZ_ASSERT(!transceiver.IsAssociated() ||
               lastAnswerMsection->GetMediaTypeInfo().MatchesTypeOf(
                   transceiver.GetMediaTypeInfo()));
    nsresult rv = mSdpHelper.CopyStickyParams(*lastAnswerMsection, msection);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (transceiver.IsStopping() || transceiver.IsStopped()) {
    SdpHelper::DisableMsection(local, msection);
    return NS_OK;
  }

  msection->SetPort(9);

  // We don't do this in AddTransportAttributes because that is also used for
  // making answers, and we don't want to unconditionally set rtcp-mux or
  // rtcp-rsize there.
  if (mSdpHelper.HasRtcp(msection->GetProtocol())) {
    // Set RTCP-MUX.
    msection->GetAttributeList().SetAttribute(
        std::make_unique<SdpFlagAttribute>(SdpAttribute::kRtcpMuxAttribute));
    // Set RTCP-RSIZE
    if (msection->GetMediaType() == SdpMediaSection::MediaType::kVideo &&
        Preferences::GetBool("media.navigator.video.offer_rtcp_rsize", false)) {
      msection->GetAttributeList().SetAttribute(
          std::make_unique<SdpFlagAttribute>(
              SdpAttribute::kRtcpRsizeAttribute));
    }
  }

  if (msection->GetMediaTypeInfo().IsRtp()) {
    // Ditto for extmap-allow-mixed Add to session level instead of media
    // level. Adding more than once is a no-op.
    local->GetAttributeList().SetAttribute(std::make_unique<SdpFlagAttribute>(
        SdpAttribute::kExtmapAllowMixedAttribute));
  }

  nsresult rv = AddTransportAttributes(msection, SdpSetupAttribute::kActpass);
  NS_ENSURE_SUCCESS(rv, rv);

  transceiver.mSendTrack.AddToOffer(mSsrcGenerator, msection);
  transceiver.mRecvTrack.AddToOffer(mSsrcGenerator, msection);

  // The send track keeps codecs excluded by setCodecPreferences (so they can
  // still be used if negotiated), and writes their fmtp/rtcp-fb. The recv
  // track, which determines the m= line formats, never adds them. Drop the
  // attributes of payload types that did not make it into the m= line.
  if (msection->GetMediaTypeInfo().IsRtp()) {
    msection->RemoveUnlistedPayloadTypeAttributes();
  }

  AddExtmap(msection);

  std::string mid;
  // We do not set the mid on the transceiver, that happens when a description
  // is set.
  if (transceiver.IsAssociated()) {
    mid = transceiver.GetMid();
  } else {
    mid = GetNewMid();
  }

  msection->GetAttributeList().SetAttribute(
      std::make_unique<SdpStringAttribute>(SdpAttribute::kMidAttribute, mid));

  return NS_OK;
}

void JsepSessionImpl::SetupBundle(Sdp* sdp) const {
  std::vector<std::string> mids;
  std::set<SdpMediaSection::MediaType> observedTypes;

  // This has the effect of changing the bundle level if the first m-section
  // goes from disabled to enabled. This is kinda inefficient.

  for (size_t i = 0; i < sdp->GetMediaSectionCount(); ++i) {
    auto& attrs = sdp->GetMediaSection(i).GetAttributeList();
    if ((sdp->GetMediaSection(i).GetPort() != 0) &&
        attrs.HasAttribute(SdpAttribute::kMidAttribute)) {
      bool useBundleOnly = false;
      switch (mBundlePolicy) {
        case kBundleMaxCompat:
          // We don't use bundle-only for max-compat
          break;
        case kBundleBalanced:
          // balanced means we use bundle-only on everything but the first
          // m-section of a given type
          if (observedTypes.count(sdp->GetMediaSection(i).GetMediaType())) {
            useBundleOnly = true;
          }
          observedTypes.insert(sdp->GetMediaSection(i).GetMediaType());
          break;
        case kBundleMaxBundle:
          // max-bundle means we use bundle-only on everything but the first
          // m-section
          useBundleOnly = !mids.empty();
          break;
      }

      if (useBundleOnly) {
        attrs.SetAttribute(std::make_unique<SdpFlagAttribute>(
            SdpAttribute::kBundleOnlyAttribute));
        // Set port to 0 for sections with bundle-only attribute. (mjf)
        sdp->GetMediaSection(i).SetPort(0);
      }

      mids.push_back(attrs.GetMid());
    }
  }

  if (!mids.empty()) {
    auto groupAttr = std::make_unique<SdpGroupAttributeList>();
    groupAttr->PushEntry(SdpGroupAttributeList::kBundle, mids);
    sdp->GetAttributeList().SetAttribute(std::move(groupAttr));
  }
}

JsepSession::Result JsepSessionImpl::CreateOffer(
    const JsepOfferOptions& options, std::string* offer) {
  mLastError.clear();

  if (mState != kJsepStateStable && mState != kJsepStateHaveLocalOffer) {
    JSEP_SET_ERROR("Cannot create offer in state " << GetStateStr(mState));
    // Spec doesn't seem to say this is an error. It probably should.
    return dom::PCError::InvalidStateError;
  }

  // This is one of those places where CreateOffer sets some state.
  SetIceRestarting(options.mIceRestart.isSome() && *(options.mIceRestart));

  UniquePtr<Sdp> sdp;

  // Make the basic SDP that is common to offer/answer.
  nsresult rv = CreateGenericSDP(&sdp);
  NS_ENSURE_SUCCESS(rv, dom::PCError::OperationError);

  // Create a data "transceiver" if none exists yet.
  if (mAlwaysNegotiateDataChannels) {
    Maybe<JsepTransceiver> dcTransceiver =
        FindTransceiver([](const JsepTransceiver& aTransceiver) {
          return aTransceiver.GetMediaTypeInfo().IsDataChannel();
        });

    if (!dcTransceiver) {
      AddTransceiver(JsepTransceiver(
          {.type = SdpMediaSection::MediaType::kApplication}, *mUuidGen));
    }
  }

  for (size_t level = 0;
       Maybe<JsepTransceiver> transceiver = GetTransceiverForLocal(level);
       ++level) {
    rv = CreateOfferMsection(options, *transceiver, sdp.get());
    NS_ENSURE_SUCCESS(rv, dom::PCError::OperationError);
    SetTransceiver(*transceiver);
  }

  SetupBundle(sdp.get());

  if (mCurrentLocalDescription && GetAnswer()) {
    rv = CopyPreviousTransportParams(*GetAnswer(), *mCurrentLocalDescription,
                                     *sdp, sdp.get());
    NS_ENSURE_SUCCESS(rv, dom::PCError::OperationError);
  }

  *offer = sdp->ToString();
  mGeneratedOffer = std::move(sdp);
  ++mSessionVersion;
  MOZ_MTLOG(ML_DEBUG, "[" << mName << "]: CreateOffer \nSDP=\n" << *offer);

  return Result();
}

std::string JsepSessionImpl::GetLocalDescription(
    JsepDescriptionPendingOrCurrent type) const {
  std::ostringstream os;
  mozilla::Sdp* sdp = GetParsedLocalDescription(type);
  if (sdp) {
    sdp->Serialize(os);
  }
  return os.str();
}

std::string JsepSessionImpl::GetRemoteDescription(
    JsepDescriptionPendingOrCurrent type) const {
  std::ostringstream os;
  mozilla::Sdp* sdp = GetParsedRemoteDescription(type);
  if (sdp) {
    sdp->Serialize(os);
  }
  return os.str();
}

void JsepSessionImpl::AddExtmap(SdpMediaSection* msection) {
  auto extensions = GetRtpExtensions(*msection);

  if (!extensions.empty()) {
    auto extmap = std::make_unique<SdpExtmapAttributeList>();
    extmap->mExtmaps = std::move(extensions);
    msection->GetAttributeList().SetAttribute(std::move(extmap));
  }
}

std::vector<SdpExtmapAttributeList::Extmap> JsepSessionImpl::GetRtpExtensions(
    const SdpMediaSection& msection) {
  std::vector<SdpExtmapAttributeList::Extmap> result;
  JsepMediaType mediaType = JsepMediaType::kNone;
  const auto direction = msection.GetDirection();
  const auto includes_send = direction == SdpDirectionAttribute::kSendrecv ||
                             direction == SdpDirectionAttribute::kSendonly;
  switch (msection.GetMediaType()) {
    case SdpMediaSection::kAudio:
      mediaType = JsepMediaType::kAudio;
      break;
    case SdpMediaSection::kVideo:
      mediaType = JsepMediaType::kVideo;
      if (StaticPrefs::media_peerconnection_video_use_dd()) {
        // We always want to receive the dependency descriptor, as libwebrtc
        // relies on it for layered streams (Bug 2071030). We only send it for
        // simulcast.
        const bool sendSimulcast =
            includes_send && msection.GetAttributeList().HasAttribute(
                                 SdpAttribute::kSimulcastAttribute);
        AddVideoRtpExtension(
            nsLiteralCString(webrtc::RtpExtension::kDependencyDescriptorUri),
            sendSimulcast ? SdpDirectionAttribute::kSendrecv
                          : SdpDirectionAttribute::kRecvonly);
      }
      if (msection.GetAttributeList().HasAttribute(
              SdpAttribute::kRidAttribute)) {
        // We need RID support
        // TODO: Would it be worth checking that the direction is sane?
        AddVideoRtpExtension(nsLiteralCString(webrtc::RtpExtension::kRidUri),
                             SdpDirectionAttribute::kSendonly);

        if (mRtxIsAllowed &&
            Preferences::GetBool("media.peerconnection.video.use_rtx", false)) {
          AddVideoRtpExtension(
              nsLiteralCString(webrtc::RtpExtension::kRepairedRidUri),
              SdpDirectionAttribute::kSendonly);
        }
      }
      break;
    default:;
  }
  if (mediaType != JsepMediaType::kNone) {
    for (auto ext = mRtpExtensions.begin(); ext != mRtpExtensions.end();
         ++ext) {
      if (ext->mMediaType == mediaType ||
          ext->mMediaType == JsepMediaType::kAudioVideo) {
        result.push_back(ext->mExtmap);
      }
    }
  }
  return result;
}

std::string JsepSessionImpl::GetNewMid() {
  std::string mid;

  do {
    std::ostringstream osMid;
    osMid << mMidCounter++;
    mid = osMid.str();
  } while (mUsedMids.count(mid));

  mUsedMids.insert(mid);
  return mid;
}

void JsepSessionImpl::AddCommonExtmaps(const SdpMediaSection& remoteMsection,
                                       SdpMediaSection* msection) {
  auto negotiatedRtpExtensions = GetRtpExtensions(*msection);
  mSdpHelper.NegotiateAndAddExtmaps(remoteMsection, negotiatedRtpExtensions,
                                    msection);
}

uint16_t JsepSessionImpl::GetNeverUsedExtmapEntry() {
  uint16_t result = 1;

  // Walk the set in order, and return the first "hole" we find
  for (const auto used : mExtmapEntriesEverUsed) {
    if (result != used) {
      MOZ_ASSERT(result < used);
      break;
    }

    // RFC 5285 says entries >= 4096 are used in offers to force the answerer
    // to pick, so we do not want to actually use these
    if (used == 4095) {
      JSEP_SET_ERROR(
          "Too many rtp extensions have been added. "
          "That's 4095. Who _does_ that?");
      return 0;
    }

    result = used + 1;
  }

  mExtmapEntriesEverUsed.insert(result);
  return result;
}

JsepSession::Result JsepSessionImpl::CreateAnswer(
    const JsepAnswerOptions& options, std::string* answer) {
  mLastError.clear();

  if (mState != kJsepStateHaveRemoteOffer &&
      mState != kJsepStateHaveLocalPranswer) {
    JSEP_SET_ERROR("Cannot create answer in state " << GetStateStr(mState));
    return dom::PCError::InvalidStateError;
  }

  UniquePtr<Sdp> sdp;

  // Make the basic SDP that is common to offer/answer.
  nsresult rv = CreateGenericSDP(&sdp);
  NS_ENSURE_SUCCESS(rv, dom::PCError::OperationError);

  const Sdp& offer = *mPendingRemoteDescription;

  // Copy the bundle groups into our answer
  auto groupAttr = std::make_unique<SdpGroupAttributeList>();
  mSdpHelper.GetBundleGroups(offer, &groupAttr->mGroups);
  sdp->GetAttributeList().SetAttribute(std::move(groupAttr));

  // Copy EXTMAP-ALLOW-MIXED from the offer to the answer
  if (offer.GetAttributeList().HasAttribute(
          SdpAttribute::kExtmapAllowMixedAttribute)) {
    sdp->GetAttributeList().SetAttribute(std::make_unique<SdpFlagAttribute>(
        SdpAttribute::kExtmapAllowMixedAttribute));
  } else {
    sdp->GetAttributeList().RemoveAttribute(
        SdpAttribute::kExtmapAllowMixedAttribute);
  }

  for (size_t i = 0; i < offer.GetMediaSectionCount(); ++i) {
    // The transceivers are already in place, due to setRemote
    Maybe<JsepTransceiver> transceiver(GetTransceiverForLevel(i));
    if (!transceiver) {
      JSEP_SET_ERROR("No transceiver for level " << i);
      MOZ_ASSERT(false);
      return dom::PCError::OperationError;
    }
    rv = CreateAnswerMsection(options, *transceiver, offer.GetMediaSection(i),
                              sdp.get());
    NS_ENSURE_SUCCESS(rv, dom::PCError::OperationError);
    SetTransceiver(*transceiver);
  }

  // Ensure that each bundle-group starts with a mid that has a transport, in
  // case we've disabled what the offerer wanted to use. If the group doesn't
  // contain anything that has a transport, remove it.
  groupAttr = std::make_unique<SdpGroupAttributeList>();
  std::vector<SdpGroupAttributeList::Group> bundleGroups;
  mSdpHelper.GetBundleGroups(*sdp, &bundleGroups);
  for (auto& group : bundleGroups) {
    for (auto& mid : group.tags) {
      const SdpMediaSection* msection =
          mSdpHelper.FindMsectionByMid(offer, mid);

      if (msection && !msection->GetAttributeList().HasAttribute(
                          SdpAttribute::kBundleOnlyAttribute)) {
        std::swap(group.tags[0], mid);
        groupAttr->mGroups.push_back(group);
        break;
      }
    }
  }
  sdp->GetAttributeList().SetAttribute(std::move(groupAttr));

  if (mCurrentLocalDescription) {
    // per discussion with bwc, 3rd parm here should be offer, not *sdp. (mjf)
    rv = CopyPreviousTransportParams(*GetAnswer(), *mCurrentRemoteDescription,
                                     offer, sdp.get());
    NS_ENSURE_SUCCESS(rv, dom::PCError::OperationError);
  }

  *answer = sdp->ToString();
  mGeneratedAnswer = std::move(sdp);
  ++mSessionVersion;
  MOZ_MTLOG(ML_DEBUG, "[" << mName << "]: CreateAnswer \nSDP=\n" << *answer);

  return Result();
}

nsresult JsepSessionImpl::CreateAnswerMsection(
    const JsepAnswerOptions& options, JsepTransceiver& transceiver,
    const SdpMediaSection& remoteMsection, Sdp* sdp) {
  MOZ_ASSERT(transceiver.GetMediaTypeInfo().MatchesTypeOf(
      remoteMsection.GetMediaTypeInfo()));
  SdpDirectionAttribute::Direction direction =
      reverse(remoteMsection.GetDirection()) & transceiver.mJsDirection;
  SdpMediaSection& msection =
      sdp->AddMediaSection(remoteMsection.GetMediaTypeInfo(), direction, 9,
                           remoteMsection.GetProtocol(), sdp::kIPv4, "0.0.0.0");

  nsresult rv = mSdpHelper.CopyStickyParams(remoteMsection, &msection);
  NS_ENSURE_SUCCESS(rv, rv);

  if (mSdpHelper.MsectionIsDisabled(remoteMsection) ||
      remoteMsection.GetMediaTypeInfo().IsUnknown()) {
    // When media type == unknown we have nothing to negotiate
    // and so the port must be set to 0
    SdpHelper::DisableMsection(sdp, &msection);
    return NS_OK;
  }

  MOZ_ASSERT(transceiver.IsAssociated());
  if (msection.GetAttributeList().GetMid().empty()) {
    msection.GetAttributeList().SetAttribute(
        std::make_unique<SdpStringAttribute>(SdpAttribute::kMidAttribute,
                                             transceiver.GetMid()));
  }

  MOZ_ASSERT(transceiver.GetMid() == msection.GetAttributeList().GetMid());

  SdpSetupAttribute::Role role;
  if (transceiver.mTransport.mDtls && !IsIceRestarting()) {
    role = (transceiver.mTransport.mDtls->mRole ==
            JsepDtlsTransport::kJsepDtlsClient)
               ? SdpSetupAttribute::kActive
               : SdpSetupAttribute::kPassive;
  } else {
    rv = DetermineAnswererSetupRole(remoteMsection, &role);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  rv = AddTransportAttributes(&msection, role);
  NS_ENSURE_SUCCESS(rv, rv);

  transceiver.mSendTrack.AddToAnswer(remoteMsection, mSsrcGenerator, &msection);
  transceiver.mRecvTrack.AddToAnswer(remoteMsection, mSsrcGenerator, &msection);

  // Add extmap attributes. This logic will probably be moved to the track,
  // since it can be specified on a per-sender basis in JS.
  // We will need some validation to ensure that the ids are identical for
  // RTP streams that are bundled together, though (bug 1406529).
  AddCommonExtmaps(remoteMsection, &msection);

  if (msection.GetFormats().empty()) {
    // Could not negotiate anything. Disable m-section.
    SdpHelper::DisableMsection(sdp, &msection);
  }

  return NS_OK;
}

nsresult JsepSessionImpl::DetermineAnswererSetupRole(
    const SdpMediaSection& remoteMsection, SdpSetupAttribute::Role* rolep) {
  // Determine the role.
  // RFC 5763 says:
  //
  //   The endpoint MUST use the setup attribute defined in [RFC4145].
  //   The endpoint that is the offerer MUST use the setup attribute
  //   value of setup:actpass and be prepared to receive a client_hello
  //   before it receives the answer.  The answerer MUST use either a
  //   setup attribute value of setup:active or setup:passive.  Note that
  //   if the answerer uses setup:passive, then the DTLS handshake will
  //   not begin until the answerer is received, which adds additional
  //   latency. setup:active allows the answer and the DTLS handshake to
  //   occur in parallel.  Thus, setup:active is RECOMMENDED.  Whichever
  //   party is active MUST initiate a DTLS handshake by sending a
  //   ClientHello over each flow (host/port quartet).
  //
  //   We default to assuming that the offerer is passive and we are active.
  SdpSetupAttribute::Role role = SdpSetupAttribute::kActive;

  if (remoteMsection.GetAttributeList().HasAttribute(
          SdpAttribute::kSetupAttribute)) {
    switch (remoteMsection.GetAttributeList().GetSetup().mRole) {
      case SdpSetupAttribute::kActive:
        role = SdpSetupAttribute::kPassive;
        break;
      case SdpSetupAttribute::kPassive:
      case SdpSetupAttribute::kActpass:
        role = SdpSetupAttribute::kActive;
        break;
      case SdpSetupAttribute::kHoldconn:
        // This should have been caught by ParseSdp
        MOZ_ASSERT(false);
        JSEP_SET_ERROR(
            "The other side used an illegal setup attribute"
            " (\"holdconn\").");
        return NS_ERROR_INVALID_ARG;
    }
  }

  *rolep = role;
  return NS_OK;
}

JsepSession::Result JsepSessionImpl::SetLocalDescription(
    JsepSdpType type, const std::string& constSdp) {
  mLastError.clear();
  std::string sdp = constSdp;

  MOZ_MTLOG(ML_DEBUG, "[" << mName << "]: SetLocalDescription type=" << type
                          << "\nSDP=\n"
                          << sdp);

  // Syntactic sugar for repeated sLD(offer)
  if (type == kJsepSdpOffer && mState == kJsepStateHaveLocalOffer) {
    // Rollback previous offer before applying the new one.
    SetLocalDescription(kJsepSdpRollback, "");
    MOZ_ASSERT(mState == kJsepStateStable);
  }

  // State checking
  switch (type) {
    case kJsepSdpOffer:
      if (mState != kJsepStateStable) {
        JSEP_SET_ERROR("Cannot set a local offer in " << mState);
        return dom::PCError::InvalidStateError;
      }
      break;
    case kJsepSdpAnswer:
    case kJsepSdpPranswer:
      if (mState != kJsepStateHaveRemoteOffer &&
          mState != kJsepStateHaveLocalPranswer) {
        JSEP_SET_ERROR("Cannot set a local answer in " << mState);
        return dom::PCError::InvalidStateError;
      }
      break;
    case kJsepSdpRollback:
      if (mState != kJsepStateHaveLocalOffer) {
        JSEP_SET_ERROR("Cannot rollback a local offer in " << mState);
        return dom::PCError::InvalidStateError;
      }
  }

  // The most basic of munging checking
  switch (type) {
    case kJsepSdpOffer:
      if (!mGeneratedOffer) {
        JSEP_SET_ERROR(
            "Cannot set local offer when createOffer has not been called.");
        return dom::PCError::InvalidModificationError;
      }
      if (sdp.empty()) {
        sdp = mGeneratedOffer->ToString();
      }
      break;
    case kJsepSdpAnswer:
    case kJsepSdpPranswer:
      if (!mGeneratedAnswer) {
        JSEP_SET_ERROR(
            "Cannot set local answer when createAnswer has not been called.");
        return dom::PCError::InvalidModificationError;
      }
      if (sdp.empty()) {
        sdp = mGeneratedAnswer->ToString();
      }
      break;
    case kJsepSdpRollback:
      break;
  }

  if (type == kJsepSdpRollback) {
    mPendingLocalDescription.reset();
    SetState(kJsepStateStable);
    RollbackLocalOffer();
    return Result();
  }

  UniquePtr<Sdp> parsed;
  JsepSession::Result parse_result = ParseSdp(sdp, &parsed);
  if (parse_result.mError.isSome()) {
    return parse_result;
  }

  // Check that content hasn't done anything unsupported with the SDP
  Result result = ValidateLocalDescription(*parsed, type);
  if (result.mError.isSome()) {
    return result;
  }
  switch (type) {
    case kJsepSdpOffer:
      result = ValidateOffer(*parsed);
      break;
    case kJsepSdpAnswer:
    case kJsepSdpPranswer:
      result = ValidateAnswer(*mPendingRemoteDescription, *parsed);
      break;
    case kJsepSdpRollback:
      MOZ_CRASH();  // Handled above
  }
  if (result.mError.isSome()) {
    return result;
  }

  if (mState == kJsepStateHaveLocalPranswer) {
    // Might be a final answer, might be another pranswer
    result = ValidateAnswerAgainstPranswer(*mPendingLocalDescription, *parsed);
    if (result.mError.isSome()) {
      return result;
    }
  }

  if (type == kJsepSdpOffer) {
    // Save in case we need to rollback
    mOldTransceivers = mTransceivers;
  }

  SdpHelper::BundledMids bundledMids;
  nsresult ns_rv = mSdpHelper.GetBundledMids(*parsed, &bundledMids);
  NS_ENSURE_SUCCESS(ns_rv, dom::PCError::OperationError);

  SdpHelper::BundledMids remoteBundledMids;
  if (type != kJsepSdpOffer) {
    ns_rv = mSdpHelper.GetBundledMids(*mPendingRemoteDescription,
                                      &remoteBundledMids);
    NS_ENSURE_SUCCESS(ns_rv, dom::PCError::OperationError);
  }

  // This loop is inspecting the previous state on transceivers by calling
  // HasOwnTransport; do this before we begin updating them in the loop
  // below.
  std::set<size_t> levelsWithNegotiatedTransport;
  for (size_t i = 0; i < parsed->GetMediaSectionCount(); ++i) {
    Maybe<JsepTransceiver> currentTransportOwner;

    const auto& msection = parsed->GetMediaSection(i);
    if (msection.GetAttributeList().HasAttribute(SdpAttribute::kMidAttribute)) {
      auto bundleTagIt = bundledMids.find(msection.GetAttributeList().GetMid());
      if (bundleTagIt != bundledMids.end()) {
        // Bundled!
        currentTransportOwner =
            GetTransceiverForLevel(bundleTagIt->second->GetLevel());
      }
    }

    if (!currentTransportOwner) {
      // Not bundled! This transceiver owns its transport.
      currentTransportOwner = GetTransceiverForLevel(i);
    }

    // HasOwnTransport has not been updated yet; this tells us if the
    // *current* transport owner owned a transport *last* time. In other
    // words, the transport owner has an already negotiated transport, and
    // the transceiver at level i can use it.
    // Note: It is possible that this m-section is disabled, making the
    // setting of this flag moot.
    if (currentTransportOwner && currentTransportOwner->IsNegotiated() &&
        currentTransportOwner->HasOwnTransport()) {
      levelsWithNegotiatedTransport.insert(i);
    }
  }

  for (size_t i = 0; i < parsed->GetMediaSectionCount(); ++i) {
    Maybe<JsepTransceiver> transceiver(GetTransceiverForLevel(i));
    if (!transceiver) {
      MOZ_ASSERT(false);
      JSEP_SET_ERROR("No transceiver for level " << i);
      return dom::PCError::OperationError;
    }

    const auto& msection = parsed->GetMediaSection(i);
    transceiver->Associate(msection.GetAttributeList().GetMid());
    transceiver->mRecvTrack.RecvTrackSetLocal(msection);

    if (mSdpHelper.MsectionIsDisabled(msection)) {
      transceiver->mTransport.Close();
      transceiver->SetCanUseExistingTransport(false);
      SetTransceiver(*transceiver);
      continue;
    }

    // If the transceiver has been stopped, but we receive an enabled msection
    // for that transceiver, don't try to resurrect the dead transceiver.
    if (transceiver->IsStopped()) {
      JSEP_SET_ERROR("Transceiver for level " << i << " has been stopped.");
      return dom::PCError::OperationError;
    }

    transceiver->SetCanUseExistingTransport(
        levelsWithNegotiatedTransport.contains(i));

    bool hasOwnTransport = mSdpHelper.OwnsTransport(
        msection, bundledMids,
        (type == kJsepSdpOffer) ? sdp::kOffer : sdp::kAnswer);
    if (type != kJsepSdpOffer) {
      const auto& remoteMsection =
          mPendingRemoteDescription->GetMediaSection(i);
      // Don't allow the answer to override what the offer allowed for
      hasOwnTransport &= mSdpHelper.OwnsTransport(
          remoteMsection, remoteBundledMids, sdp::kOffer);
    }

    // For an offer, OwnsTransport() can't be sure a m-section that isn't
    // marked bundle-only will actually end up bundled (that depends on the
    // answer), so it conservatively says such a m-section owns its
    // transport.
    if (hasOwnTransport) {
      EnsureHasOwnTransport(parsed->GetMediaSection(i), *transceiver);
    }

    if (type == kJsepSdpOffer) {
      if (!hasOwnTransport) {
        auto it = bundledMids.find(transceiver->GetMid());
        if (it != bundledMids.end()) {
          transceiver->SetBundleLevel(it->second->GetLevel());
        }
      }
    } else {
      auto it = remoteBundledMids.find(transceiver->GetMid());
      if (it != remoteBundledMids.end()) {
        transceiver->SetBundleLevel(it->second->GetLevel());
      }
    }

    SetTransceiver(*transceiver);
  }

  CopyBundleTransports();

  switch (type) {
    case kJsepSdpOffer:
      ns_rv = SetLocalDescriptionOffer(std::move(parsed));
      break;
    case kJsepSdpAnswer:
    case kJsepSdpPranswer:
      ns_rv = SetLocalDescriptionAnswer(type, std::move(parsed));
      break;
    case kJsepSdpRollback:
      MOZ_CRASH();  // Handled above
  }

  NS_ENSURE_SUCCESS(ns_rv, dom::PCError::OperationError);
  return Result();
}

nsresult JsepSessionImpl::SetLocalDescriptionOffer(UniquePtr<Sdp> offer) {
  MOZ_ASSERT(mState == kJsepStateStable);
  mPendingLocalDescription = std::move(offer);
  mIsPendingOfferer = Some(true);
  SetState(kJsepStateHaveLocalOffer);

  std::vector<JsepTrack*> recvTracks;
  recvTracks.reserve(mTransceivers.size());
  for (auto& transceiver : mTransceivers) {
    if (transceiver.mJsDirection & sdp::kRecv) {
      recvTracks.push_back(&transceiver.mRecvTrack);
    } else {
      transceiver.mRecvTrack.ResetReceivePayloadTypes();
    }
  }

  JsepTrack::SetReceivePayloadTypes(recvTracks, true);

  return NS_OK;
}

nsresult JsepSessionImpl::SetLocalDescriptionAnswer(JsepSdpType type,
                                                    UniquePtr<Sdp> answer) {
  MOZ_ASSERT(mState == kJsepStateHaveRemoteOffer ||
             mState == kJsepStateHaveLocalPranswer);
  const bool firstAnswer = (mState == kJsepStateHaveRemoteOffer);
  mPendingLocalDescription = std::move(answer);

  nsresult rv = HandleNegotiatedSession(mPendingLocalDescription,
                                        mPendingRemoteDescription);
  NS_ENSURE_SUCCESS(rv, rv);

  // Counts exchanges, not answers; a repeated provisional answer is the same
  // exchange.
  if (firstAnswer) {
    mNegotiations++;
  }

  if (type == kJsepSdpPranswer) {
    SetState(kJsepStateHaveLocalPranswer);
    return NS_OK;
  }

  mGeneratedAnswer.reset();
  mGeneratedOffer.reset();
  mCurrentRemoteDescription = std::move(mPendingRemoteDescription);
  mCurrentLocalDescription = std::move(mPendingLocalDescription);
  MOZ_ASSERT(mIsPendingOfferer.isSome() && !*mIsPendingOfferer);
  mIsPendingOfferer.reset();
  mIsCurrentOfferer = Some(false);

  SetState(kJsepStateStable);
  return NS_OK;
}

JsepSession::Result JsepSessionImpl::SetRemoteDescription(
    JsepSdpType type, const std::string& sdp) {
  mLastError.clear();

  MOZ_MTLOG(ML_DEBUG, "[" << mName << "]: SetRemoteDescription type=" << type
                          << "\nSDP=\n"
                          << sdp);

  if (mState == kJsepStateHaveRemoteOffer && type == kJsepSdpOffer) {
    // Rollback previous offer before applying the new one.
    SetRemoteDescription(kJsepSdpRollback, "");
    MOZ_ASSERT(mState == kJsepStateStable);
  }

  if (type == kJsepSdpRollback) {
    if (mState != kJsepStateHaveRemoteOffer) {
      JSEP_SET_ERROR("Cannot rollback remote description in "
                     << GetStateStr(mState));
      return dom::PCError::InvalidStateError;
    }

    mPendingRemoteDescription.reset();
    SetState(kJsepStateStable);
    RollbackRemoteOffer();

    return Result();
  }

  switch (mState) {
    case kJsepStateStable:
      if (type != kJsepSdpOffer) {
        JSEP_SET_ERROR("Cannot set remote answer in state "
                       << GetStateStr(mState));
        return dom::PCError::InvalidStateError;
      }
      break;
    case kJsepStateHaveLocalOffer:
    case kJsepStateHaveRemotePranswer:
      if (type != kJsepSdpAnswer && type != kJsepSdpPranswer) {
        JSEP_SET_ERROR("Cannot set remote offer in state "
                       << GetStateStr(mState));
        return dom::PCError::InvalidStateError;
      }
      break;
    default:
      JSEP_SET_ERROR("Cannot set remote offer or answer in current state "
                     << GetStateStr(mState));
      return dom::PCError::InvalidStateError;
  }

  // Parse.
  UniquePtr<Sdp> parsed;
  JsepSession::Result parse_result = ParseSdp(sdp, &parsed);
  if (parse_result.mError.isSome()) {
    return parse_result;
  }

  Result result = ValidateRemoteDescription(*parsed);
  if (result.mError.isSome()) {
    return result;
  }

  switch (type) {
    case kJsepSdpOffer:
      result = ValidateOffer(*parsed);
      break;
    case kJsepSdpAnswer:
    case kJsepSdpPranswer:
      result = ValidateAnswer(*mPendingLocalDescription, *parsed);
      break;
    case kJsepSdpRollback:
      MOZ_CRASH();  // Handled above
  }
  if (result.mError.isSome()) {
    return result;
  }

  if (mState == kJsepStateHaveRemotePranswer) {
    // Might be a final answer, might be another pranswer
    result = ValidateAnswerAgainstPranswer(*mPendingRemoteDescription, *parsed);
    if (result.mError.isSome()) {
      return result;
    }
  }

  bool iceLite =
      parsed->GetAttributeList().HasAttribute(SdpAttribute::kIceLiteAttribute);

  // check for mismatch ufrag/pwd indicating ice restart
  // can't just check the first one because it might be disabled
  bool iceRestarting = false;
  if (mCurrentRemoteDescription.get()) {
    for (size_t i = 0; !iceRestarting &&
                       i < mCurrentRemoteDescription->GetMediaSectionCount();
         ++i) {
      const SdpMediaSection& newMsection = parsed->GetMediaSection(i);
      const SdpMediaSection& oldMsection =
          mCurrentRemoteDescription->GetMediaSection(i);

      if (mSdpHelper.MsectionIsDisabled(newMsection) ||
          mSdpHelper.MsectionIsDisabled(oldMsection)) {
        continue;
      }

      iceRestarting = mSdpHelper.IceCredentialsDiffer(newMsection, oldMsection);
    }
  }

  std::vector<std::string> iceOptions;
  if (parsed->GetAttributeList().HasAttribute(
          SdpAttribute::kIceOptionsAttribute)) {
    iceOptions = parsed->GetAttributeList().GetIceOptions().mValues;
  }

  if (type == kJsepSdpOffer) {
    // Save in case we need to rollback.
    mOldTransceivers = mTransceivers;
    for (auto& transceiver : mTransceivers) {
      if (!transceiver.IsNegotiated()) {
        // We chose a level for this transceiver, but never negotiated it.
        // Discard this state.
        transceiver.ClearLevel();
      }
    }
  }

  // TODO(bug 1095780): Note that we create remote tracks even when
  // They contain only codecs we can't negotiate or other craziness.
  nsresult ns_rv = UpdateTransceiversFromRemoteDescription(*parsed);
  NS_ENSURE_SUCCESS(ns_rv, dom::PCError::OperationError);

  for (size_t i = 0; i < parsed->GetMediaSectionCount(); ++i) {
    MOZ_ASSERT(GetTransceiverForLevel(i));
  }

  switch (type) {
    case kJsepSdpOffer:
      ns_rv = SetRemoteDescriptionOffer(std::move(parsed));
      break;
    case kJsepSdpAnswer:
    case kJsepSdpPranswer:
      ns_rv = SetRemoteDescriptionAnswer(type, std::move(parsed));
      break;
    case kJsepSdpRollback:
      MOZ_CRASH();  // Handled above
  }

  NS_ENSURE_SUCCESS(ns_rv, dom::PCError::OperationError);

  mRemoteIsIceLite = iceLite;
  mIceOptions = std::move(iceOptions);
  SetIceRestarting(iceRestarting);
  return Result();
}

nsresult JsepSessionImpl::HandleNegotiatedSession(
    const UniquePtr<Sdp>& local, const UniquePtr<Sdp>& remote) {
  // local ufrag/pwd has been negotiated; we will never go back to the old ones
  mOldIceUfrag.clear();
  mOldIcePwd.clear();

  bool remoteIceLite =
      remote->GetAttributeList().HasAttribute(SdpAttribute::kIceLiteAttribute);

  // RFC 8445 Section 9: roles persist for the session lifetime. Only set on
  // first negotiation, or force controlling if remote switched to ICE-lite.
  mIceControlling |= remoteIceLite;
  if (!mNegotiations) {
    mIceControlling |= *mIsPendingOfferer;
  }

  const Sdp& answer = *mIsPendingOfferer ? *remote : *local;

  SdpHelper::BundledMids bundledMids;
  nsresult rv = mSdpHelper.GetBundledMids(answer, &bundledMids);
  NS_ENSURE_SUCCESS(rv, rv);

  // First, set the bundle level on the transceivers
  for (auto& [mid, transportOwner] : bundledMids) {
    Maybe<JsepTransceiver> bundledTransceiver = GetTransceiverForMid(mid);
    if (!bundledTransceiver) {
      JSEP_SET_ERROR("No transceiver for bundled mid " << mid);
      return NS_ERROR_INVALID_ARG;
    }
    bundledTransceiver->SetBundleLevel(transportOwner->GetLevel());
    SetTransceiver(*bundledTransceiver);
  }

  // Now walk through the m-sections, perform negotiation, and update the
  // transceivers.
  for (size_t i = 0; i < local->GetMediaSectionCount(); ++i) {
    Maybe<JsepTransceiver> transceiver(GetTransceiverForLevel(i));
    if (!transceiver) {
      MOZ_ASSERT(false);
      JSEP_SET_ERROR("No transceiver for level " << i);
      return NS_ERROR_FAILURE;
    }

    if (mSdpHelper.MsectionIsDisabled(local->GetMediaSection(i))) {
      transceiver->SetRemoved();
    }

    // Skip disabled m-sections.
    if (mSdpHelper.MsectionIsDisabled(answer.GetMediaSection(i))) {
      transceiver->mTransport.Close();
      transceiver->SetStopped();
      transceiver->Disassociate();
      transceiver->ClearBundleLevel();
      transceiver->mSendTrack.SetActive(false);
      transceiver->mRecvTrack.SetActive(false);
      transceiver->SetCanRecycleMyMsection();
      SetTransceiver(*transceiver);
      // Do not clear mLevel yet! That will happen on the next negotiation.
      continue;
    }

    rv = MakeNegotiatedTransceiver(remote->GetMediaSection(i),
                                   local->GetMediaSection(i), *transceiver);
    NS_ENSURE_SUCCESS(rv, rv);
    SetTransceiver(*transceiver);
  }

  CopyBundleTransports();

  std::vector<JsepTrack*> receiveTracks;
  receiveTracks.reserve(mTransceivers.size());
  for (auto& transceiver : mTransceivers) {
    // Do not count payload types for non-active recv tracks as duplicates. If
    // we receive an RTP packet with a payload type that is used by both a
    // sendrecv and a sendonly m-section, there is no ambiguity; it is for the
    // sendrecv m-section. MediaPipelineFilter and conduits are informed of
    // their active status, so they know whether they can process packets and
    // learn new SSRCs.
    if (transceiver.mRecvTrack.GetActive()) {
      receiveTracks.push_back(&transceiver.mRecvTrack);
    } else {
      transceiver.mRecvTrack.ResetReceivePayloadTypes();
    }
  }
  JsepTrack::SetReceivePayloadTypes(receiveTracks);

  return NS_OK;
}

nsresult JsepSessionImpl::MakeNegotiatedTransceiver(
    const SdpMediaSection& remote, const SdpMediaSection& local,
    JsepTransceiver& transceiver) {
  const SdpMediaSection& answer = *mIsPendingOfferer ? remote : local;

  bool sending = false;
  bool receiving = false;

  // We do not pay any attention to whether the transceiver is stopped here,
  // because that is only a signal to the JSEP engine to _attempt_ to reject
  // the corresponding m-section the next time we're the offerer.
  if (*mIsPendingOfferer) {
    receiving = answer.IsSending();
    sending = answer.IsReceiving();
  } else {
    sending = answer.IsSending();
    receiving = answer.IsReceiving();
  }

  MOZ_MTLOG(ML_DEBUG, "[" << mName << "]: Negotiated m= line"
                          << " index=" << local.GetLevel() << " type="
                          << local.GetMediaType() << " sending=" << sending
                          << " receiving=" << receiving);

  transceiver.SetNegotiated();

  // Deliberately not touching CanUseExistingTransport() here: it's only
  // ever consulted while in have-local-offer, and gets recomputed from
  // scratch by SetLocalDescription() the next time around, so there's
  // nothing to update on this path.

  // Ensure that this is finalized in case we need to copy it below
  nsresult rv =
      FinalizeTransport(remote.GetAttributeList(), answer.GetAttributeList(),
                        &transceiver.mTransport);
  NS_ENSURE_SUCCESS(rv, rv);

  transceiver.mSendTrack.SetActive(sending);
  rv = transceiver.mSendTrack.Negotiate(answer, remote, local);
  if (NS_FAILED(rv)) {
    JSEP_SET_ERROR("Answer had no codecs in common with offer in m-section "
                   << local.GetLevel());
    return rv;
  }

  JsepTrack& recvTrack = transceiver.mRecvTrack;
  recvTrack.SetActive(receiving);
  rv = recvTrack.Negotiate(answer, remote, local);
  if (NS_FAILED(rv)) {
    JSEP_SET_ERROR("Answer had no codecs in common with offer in m-section "
                   << local.GetLevel());
    return rv;
  }

  if (transceiver.HasBundleLevel() && recvTrack.GetSsrcs().empty() &&
      recvTrack.GetMediaTypeInfo().IsRtp()) {
    // TODO(bug 1105005): Once we have urn:ietf:params:rtp-hdrext:sdes:mid
    // support, we should only fire this warning if that extension was not
    // negotiated.
    MOZ_MTLOG(ML_ERROR, "[" << mName
                            << "]: Bundled m-section has no ssrc "
                               "attributes. This may cause media packets to be "
                               "dropped.");
  }

  if (transceiver.mTransport.mComponents == 2) {
    // RTCP MUX or not.
    // TODO(bug 1095743): verify that the PTs are consistent with mux.
    MOZ_MTLOG(ML_DEBUG, "[" << mName << "]: RTCP-MUX is off");
  }

  if (answer.GetAttributeList().HasAttribute(SdpAttribute::kExtmapAttribute)) {
    const auto& extmaps = answer.GetAttributeList().GetExtmap().mExtmaps;
    for (const auto& negotiatedExtension : extmaps) {
      if (negotiatedExtension.entry == 0) {
        MOZ_ASSERT(false, "This should have been caught sooner");
        continue;
      }

      mExtmapEntriesEverNegotiated[negotiatedExtension.entry] =
          negotiatedExtension.extensionname;

      for (auto& originalExtension : mRtpExtensions) {
        if (negotiatedExtension.extensionname ==
            originalExtension.mExtmap.extensionname) {
          // Update extmap to match what was negotiated
          originalExtension.mExtmap.entry = negotiatedExtension.entry;
          mExtmapEntriesEverUsed.insert(negotiatedExtension.entry);
        } else if (originalExtension.mExtmap.entry ==
                   negotiatedExtension.entry) {
          // If this extmap entry was claimed for a different extension, update
          // it to a new value so we don't end up with a duplicate.
          originalExtension.mExtmap.entry = GetNeverUsedExtmapEntry();
        }
      }
    }
  }

  return NS_OK;
}

void JsepSessionImpl::EnsureHasOwnTransport(const SdpMediaSection& msection,
                                            JsepTransceiver& transceiver) {
  JsepTransport& transport = transceiver.mTransport;

  if (!transceiver.HasOwnTransport()) {
    // Transceiver didn't own this transport last time, it won't now either
    transport.Close();
  }

  transport.mLocalUfrag = msection.GetAttributeList().GetIceUfrag();
  transport.mLocalPwd = msection.GetAttributeList().GetIcePwd();

  transceiver.ClearBundleLevel();

  if (!transport.mComponents) {
    if (mSdpHelper.HasRtcp(msection.GetProtocol())) {
      transport.mComponents = 2;
    } else {
      transport.mComponents = 1;
    }
  }

  if (transport.mTransportId.empty()) {
    // TODO: Once we use different ICE ufrag/pass for each m-section, we can
    // use that here.
    std::ostringstream os;
    os << "transport_" << mTransportIdCounter++;
    transport.mTransportId = os.str();
  }
}

void JsepSessionImpl::CopyBundleTransports() {
  for (auto& transceiver : mTransceivers) {
    if (transceiver.HasBundleLevel()) {
      MOZ_MTLOG(ML_DEBUG,
                "[" << mName << "] Transceiver " << transceiver.GetLevel()
                    << " is in a bundle; transceiver "
                    << transceiver.BundleLevel() << " owns the transport.");
      Maybe<const JsepTransceiver> transportOwner =
          GetTransceiverForLevel(transceiver.BundleLevel());
      MOZ_ASSERT(transportOwner);
      if (transportOwner) {
        transceiver.mTransport = transportOwner->mTransport;
      }
    } else if (transceiver.HasLevel()) {
      MOZ_MTLOG(ML_DEBUG, "[" << mName << "] Transceiver "
                              << transceiver.GetLevel()
                              << " is not necessarily in a bundle.");
    }
    if (transceiver.HasLevel()) {
      MOZ_MTLOG(ML_DEBUG,
                "[" << mName << "] Transceiver " << transceiver.GetLevel()
                    << " transport-id: " << transceiver.mTransport.mTransportId
                    << " components: " << transceiver.mTransport.mComponents);
    }
  }
}

nsresult JsepSessionImpl::FinalizeTransport(const SdpAttributeList& remote,
                                            const SdpAttributeList& answer,
                                            JsepTransport* transport) const {
  if (!transport->mComponents) {
    return NS_OK;
  }

  if (!transport->mIce || transport->mIce->mUfrag != remote.GetIceUfrag() ||
      transport->mIce->mPwd != remote.GetIcePwd()) {
    std::unique_ptr<JsepIceTransport> ice =
        std::make_unique<JsepIceTransport>();
    transport->mDtls = nullptr;

    // We do sanity-checking for these in ParseSdp
    ice->mUfrag = remote.GetIceUfrag();
    ice->mPwd = remote.GetIcePwd();
    transport->mIce = std::move(ice);
  }

  if (remote.HasAttribute(SdpAttribute::kCandidateAttribute)) {
    transport->mIce->mCandidates = remote.GetCandidate();
  }

  if (!transport->mDtls) {
    // RFC 5763 says:
    //
    //   The endpoint MUST use the setup attribute defined in [RFC4145].
    //   The endpoint that is the offerer MUST use the setup attribute
    //   value of setup:actpass and be prepared to receive a client_hello
    //   before it receives the answer.  The answerer MUST use either a
    //   setup attribute value of setup:active or setup:passive.  Note that
    //   if the answerer uses setup:passive, then the DTLS handshake will
    //   not begin until the answerer is received, which adds additional
    //   latency. setup:active allows the answer and the DTLS handshake to
    //   occur in parallel.  Thus, setup:active is RECOMMENDED.  Whichever
    //   party is active MUST initiate a DTLS handshake by sending a
    //   ClientHello over each flow (host/port quartet).
    std::unique_ptr<JsepDtlsTransport> dtls =
        std::make_unique<JsepDtlsTransport>();
    dtls->mFingerprints = remote.GetFingerprint();
    if (!answer.HasAttribute(mozilla::SdpAttribute::kSetupAttribute)) {
      dtls->mRole = *mIsPendingOfferer ? JsepDtlsTransport::kJsepDtlsServer
                                       : JsepDtlsTransport::kJsepDtlsClient;
    } else {
      if (*mIsPendingOfferer) {
        dtls->mRole = (answer.GetSetup().mRole == SdpSetupAttribute::kActive)
                          ? JsepDtlsTransport::kJsepDtlsServer
                          : JsepDtlsTransport::kJsepDtlsClient;
      } else {
        dtls->mRole = (answer.GetSetup().mRole == SdpSetupAttribute::kActive)
                          ? JsepDtlsTransport::kJsepDtlsClient
                          : JsepDtlsTransport::kJsepDtlsServer;
      }
    }

    transport->mDtls = std::move(dtls);
  }

  if (answer.HasAttribute(SdpAttribute::kRtcpMuxAttribute)) {
    transport->mComponents = 1;
  }

  return NS_OK;
}

nsresult JsepSessionImpl::AddTransportAttributes(
    SdpMediaSection* msection, SdpSetupAttribute::Role dtlsRole) {
  if (mIceUfrag.empty() || mIcePwd.empty()) {
    JSEP_SET_ERROR("Missing ICE ufrag or password");
    return NS_ERROR_FAILURE;
  }

  SdpAttributeList& attrList = msection->GetAttributeList();
  attrList.SetAttribute(std::make_unique<SdpStringAttribute>(
      SdpAttribute::kIceUfragAttribute, mIceUfrag));
  attrList.SetAttribute(std::make_unique<SdpStringAttribute>(
      SdpAttribute::kIcePwdAttribute, mIcePwd));

  msection->GetAttributeList().SetAttribute(
      std::make_unique<SdpSetupAttribute>(dtlsRole));

  return NS_OK;
}

nsresult JsepSessionImpl::CopyPreviousTransportParams(
    const Sdp& oldAnswer, const Sdp& offerersPreviousSdp, const Sdp& newOffer,
    Sdp* newLocal) {
  for (size_t i = 0; i < oldAnswer.GetMediaSectionCount(); ++i) {
    if (!mSdpHelper.MsectionIsDisabled(newLocal->GetMediaSection(i)) &&
        mSdpHelper.AreOldTransportParamsValid(oldAnswer, offerersPreviousSdp,
                                              newOffer, i)) {
      // If newLocal is an offer, this will be the number of components we used
      // last time, and if it is an answer, this will be the number of
      // components we've decided we're using now.
      Maybe<const JsepTransceiver> transceiver(GetTransceiverForLevel(i));
      if (!transceiver) {
        MOZ_ASSERT(false);
        JSEP_SET_ERROR("No transceiver for level " << i);
        return NS_ERROR_FAILURE;
      }
      size_t numComponents = transceiver->mTransport.mComponents;
      nsresult rv = mSdpHelper.CopyTransportParams(
          numComponents, mCurrentLocalDescription->GetMediaSection(i),
          &newLocal->GetMediaSection(i));
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  return NS_OK;
}

JsepSession::Result JsepSessionImpl::ParseSdp(const std::string& sdp,
                                              UniquePtr<Sdp>* parsedp) {
  auto results = mParser->Parse(sdp);
  auto parsed = std::move(results->Sdp());
  mLastSdpParsingErrors = results->Errors();
  if (!parsed) {
    std::string error = results->ParserName() + " Failed to parse SDP: ";
    mSdpHelper.AppendSdpParseErrors(mLastSdpParsingErrors, &error);
    Maybe<size_t> lineNumber;
    if (!mLastSdpParsingErrors.empty()) {
      lineNumber = Some(mLastSdpParsingErrors[0].first);
    }
    JSEP_SET_ERROR(error);
    return Result(dom::PCError::SyntaxError, "sdp-syntax-error", lineNumber);
  }
  // Verify that the JSEP rules for all SDP are followed
  for (size_t i = 0; i < parsed->GetMediaSectionCount(); ++i) {
    if (mSdpHelper.MsectionIsDisabled(parsed->GetMediaSection(i))) {
      // Disabled, let this stuff slide.
      continue;
    }

    const SdpMediaSection& msection(parsed->GetMediaSection(i));
    auto& mediaAttrs = msection.GetAttributeList();

    if (mediaAttrs.HasAttribute(SdpAttribute::kMidAttribute) &&
        mediaAttrs.GetMid().length() > 16) {
      JSEP_SET_ERROR("Invalid description, mid length greater than 16");
      return Result(dom::PCError::OperationError);
    }

    if (mediaAttrs.HasAttribute(SdpAttribute::kExtmapAttribute)) {
      std::set<uint16_t> extIds;
      for (const auto& ext : mediaAttrs.GetExtmap().mExtmaps) {
        uint16_t id = ext.entry;

        if (id < 1 || id > 255) {
          JSEP_SET_ERROR("Description contains invalid extension id "
                         << id << " on level " << i
                         << " (valid range is 1-255)");
          return Result(dom::PCError::OperationError);
        }

        if (extIds.find(id) != extIds.end()) {
          JSEP_SET_ERROR("Description contains duplicate extension id "
                         << id << " on level " << i);
          return Result(dom::PCError::InvalidAccessError);
        }
        extIds.insert(id);
      }
    }

    if (msection.GetMediaType() == SdpMediaSection::kAudio ||
        msection.GetMediaType() == SdpMediaSection::kVideo) {
      const bool rtcpMux = msection.GetAttributeList().HasAttribute(
          SdpAttribute::kRtcpMuxAttribute);
      // Sanity-check that payload type can work with RTP
      for (const std::string& fmt : msection.GetFormats()) {
        uint16_t payloadType;
        if (!SdpHelper::GetPtAsInt(fmt, &payloadType)) {
          JSEP_SET_ERROR("Payload type \""
                         << fmt << "\" is not a 16-bit unsigned int at level "
                         << i);
          return Result(dom::PCError::InvalidAccessError);
        }
        if (payloadType > 127) {
          JSEP_SET_ERROR("audio/video payload type \""
                         << fmt << "\" is too large at level " << i);
          return Result(dom::PCError::InvalidAccessError);
        }
        // PT is well-formed, but it may be reserved
        switch (payloadType) {
          // https://www.rfc-editor.org/info/rfc3551/#section-6
          case 1:
          case 2:
          case 19:
            JSEP_SET_ERROR("Audio/video payload type \""
                           << fmt << "\" at level " << i
                           << " is using reserved payload type number");
            return Result(dom::PCError::InvalidAccessError);
          default:
            break;
        }
        if (rtcpMux && payloadType >= 64 && payloadType <= 95) {
          JSEP_SET_ERROR(
              "Audio/video payload type \""
              << fmt << "\" at level " << i
              << " is using payload type number in the range 64 to 95 which"
                 " is reserved while rtcp-mux is in use");
          return Result(dom::PCError::InvalidAccessError);
        }
      }
    }
  }

  *parsedp = std::move(parsed);
  return Result();
}

nsresult JsepSessionImpl::SetRemoteDescriptionOffer(UniquePtr<Sdp> offer) {
  MOZ_ASSERT(mState == kJsepStateStable);

  mPendingRemoteDescription = std::move(offer);
  mIsPendingOfferer = Some(false);

  SetState(kJsepStateHaveRemoteOffer);
  return NS_OK;
}

nsresult JsepSessionImpl::SetRemoteDescriptionAnswer(JsepSdpType type,
                                                     UniquePtr<Sdp> answer) {
  MOZ_ASSERT(mState == kJsepStateHaveLocalOffer ||
             mState == kJsepStateHaveRemotePranswer);
  const bool firstAnswer = (mState == kJsepStateHaveLocalOffer);
  mPendingRemoteDescription = std::move(answer);

  nsresult rv = HandleNegotiatedSession(mPendingLocalDescription,
                                        mPendingRemoteDescription);
  NS_ENSURE_SUCCESS(rv, rv);

  if (firstAnswer) {
    mNegotiations++;
  }

  if (type == kJsepSdpPranswer) {
    SetState(kJsepStateHaveRemotePranswer);
    return NS_OK;
  }

  mGeneratedAnswer.reset();
  mGeneratedOffer.reset();
  mCurrentRemoteDescription = std::move(mPendingRemoteDescription);
  mCurrentLocalDescription = std::move(mPendingLocalDescription);
  MOZ_ASSERT(mIsPendingOfferer.isSome() && *mIsPendingOfferer);
  mIsPendingOfferer.reset();
  mIsCurrentOfferer = Some(true);

  SetState(kJsepStateStable);
  return NS_OK;
}

Maybe<JsepTransceiver> JsepSessionImpl::GetTransceiverForLevel(
    size_t level) const {
  return FindTransceiver([level](const JsepTransceiver& transceiver) {
    return transceiver.HasLevel() && (transceiver.GetLevel() == level);
  });
}

Maybe<JsepTransceiver> JsepSessionImpl::GetTransceiverForMid(
    const std::string& mid) const {
  return FindTransceiver([mid](const JsepTransceiver& transceiver) {
    return transceiver.IsAssociated() && (transceiver.GetMid() == mid);
  });
}

Maybe<JsepTransceiver> JsepSessionImpl::GetTransceiverForLocal(size_t level) {
  if (Maybe<JsepTransceiver> transceiver = GetTransceiverForLevel(level)) {
    if (transceiver->CanRecycleMyMsection() &&
        !transceiver->GetMediaTypeInfo().IsDataChannel()) {
      // Attempt to recycle. If this fails, the old transceiver stays put.
      // [RFC9429] section 5.2.2 requires a zero port m= section to be recycled
      // for an added transceiver, whatever media type that transceiver has.
      transceiver->Disassociate();
      Maybe<JsepTransceiver> newTransceiver =
          FindUnassociatedNonAppTransceiver(Nothing(), false);
      if (newTransceiver) {
        newTransceiver->SetLevel(level);
        transceiver->ClearLevel();
        transceiver->mSendTrack.ClearRids();
        SetTransceiver(*newTransceiver);
        SetTransceiver(*transceiver);
        return newTransceiver;
      }
    }

    SetTransceiver(*transceiver);
    return transceiver;
  }

  // There is no transceiver for |level| right now.

  // The datachannel m-section comes before any m-section that has not been
  // negotiated yet when the alwaysNegotiateDataChannels flag is set.
  if (mAlwaysNegotiateDataChannels) {
    for (auto& transceiver : mTransceivers) {
      if (transceiver.GetMediaTypeInfo().IsDataChannel() &&
          transceiver.IsFreeToUse()) {
        transceiver.SetLevel(level);
        return Some(transceiver);
      }
    }
  }

  // Look for an RTP transceiver (spec requires us to give the lower levels to
  // new RTP transceivers)
  for (auto& transceiver : mTransceivers) {
    if (transceiver.GetMediaTypeInfo().IsRtp() && transceiver.IsFreeToUse()) {
      transceiver.SetLevel(level);
      return Some(transceiver);
    }
  }

  // Ok, look for a datachannel
  for (auto& transceiver : mTransceivers) {
    if (transceiver.IsFreeToUse()) {
      transceiver.SetLevel(level);
      return Some(transceiver);
    }
  }

  return Nothing();
}

Maybe<JsepTransceiver> JsepSessionImpl::GetTransceiverForRemote(
    const SdpMediaSection& msection) {
  size_t level = msection.GetLevel();
  Maybe<JsepTransceiver> transceiver = GetTransceiverForLevel(level);
  if (transceiver) {
    if (!transceiver->CanRecycleMyMsection()) {
      return transceiver;
    }
    // Recycle
    transceiver->Disassociate();
    transceiver->ClearLevel();
    transceiver->mSendTrack.ClearRids();
    SetTransceiver(*transceiver);
    // Shouldn't be strictly necessary to unset this, but it's clearer this way
    transceiver = Nothing();
  }

  // No transceiver for |level|
  if (!msection.GetMediaTypeInfo().IsUnknown()) {
    if (msection.GetMediaTypeInfo().IsDataChannel()) {
      // Other side has done something weird like rejecting the datachannel
      // m-section. Try to recover.
      for (auto& tx : mTransceivers) {
        if (tx.GetMediaTypeInfo().IsDataChannel()) {
          tx.RestartDatachannelTransceiver();
          transceiver = Some(tx);
          break;
        }
      }
    } else if (msection.IsReceiving()) {
      transceiver = FindUnassociatedNonAppTransceiver(
          Some(msection.GetMediaType()), true);
    }
  }

  // We found a transceiver to associate
  if (transceiver) {
    transceiver->SetLevel(level);
    SetTransceiver(*transceiver);
    return transceiver;
  }

  // Make a new transceiver
  JsepTransceiver newTransceiver(msection.GetMediaTypeInfo(), *mUuidGen,
                                 SdpDirectionAttribute::kRecvonly);
  newTransceiver.SetLevel(level);
  newTransceiver.SetOnlyExistsBecauseOfSetRemote(true);
  AddTransceiver(newTransceiver);
  return Some(mTransceivers.back());
}

Maybe<JsepTransceiver> JsepSessionImpl::GetTransceiverWithTransport(
    const std::string& transportId) const {
  for (const auto& transceiver : mTransceivers) {
    if (transceiver.HasOwnTransport() &&
        (transceiver.mTransport.mTransportId == transportId)) {
      MOZ_ASSERT(transceiver.HasLevel(),
                 "Transceiver has a transport, but no level!");
      return Some(transceiver);
    }
  }

  return Nothing();
}

nsresult JsepSessionImpl::UpdateTransceiversFromRemoteDescription(
    const Sdp& remote) {
  // Iterate over the sdp, updating remote tracks as we go
  for (size_t i = 0; i < remote.GetMediaSectionCount(); ++i) {
    const SdpMediaSection& msection = remote.GetMediaSection(i);

    // A mid in the remote description is in use even when we do not associate
    // a transceiver with the m= section, which is the case for m= sections
    // that are disabled or have a media type we do not know. GetNewMid must
    // not hand that value out to another m= section.
    if (msection.GetAttributeList().HasAttribute(SdpAttribute::kMidAttribute)) {
      mUsedMids.insert(msection.GetAttributeList().GetMid());
    }

    Maybe<JsepTransceiver> transceiver(GetTransceiverForRemote(msection));
    if (!transceiver) {
      return NS_ERROR_FAILURE;
    }

    if (!mSdpHelper.MsectionIsDisabled(msection) &&
        !msection.GetMediaTypeInfo().IsUnknown()) {
      if (msection.GetAttributeList().HasAttribute(
              SdpAttribute::kMidAttribute)) {
        transceiver->Associate(msection.GetAttributeList().GetMid());
      }
      if (!transceiver->IsAssociated()) {
        transceiver->Associate(GetNewMid());
      } else {
        mUsedMids.insert(transceiver->GetMid());
      }
    } else {
      // Unknown media types get rejected in the answer. Let's treat them as
      // disabled m-sections here.

      // We do not disassociate here, that happens when negotiation completes
      // These things cannot be rolled back.
      transceiver->mTransport.Close();
      transceiver->SetStopped();
      SetTransceiver(*transceiver);
      continue;
    }

    if (msection.GetMediaType() == SdpMediaSection::MediaType::kApplication) {
      SetTransceiver(*transceiver);
      continue;
    }

    transceiver->mSendTrack.SendTrackSetRemote(mSsrcGenerator, msection);

    // Interop workaround for endpoints that don't support msid.
    // Ensures that there is a default stream id set, provided the remote is
    // sending.
    // TODO(bug 1426005): Remove this, or at least move it to JsepTrack.
    transceiver->mRecvTrack.UpdateStreamIds({mDefaultRemoteStreamId});

    // This will process a=msid if present, or clear the stream ids if the
    // msection is not sending. If the msection is sending, and there are no
    // a=msid, the previously set default will stay.
    transceiver->mRecvTrack.RecvTrackSetRemote(
        remote, msection, transceiver->mSendTrack.GetSsrcs());
    SetTransceiver(*transceiver);
  }

  return NS_OK;
}

Maybe<JsepTransceiver> JsepSessionImpl::FindUnassociatedNonAppTransceiver(
    Maybe<SdpMediaSection::MediaType> type, bool magic) {
  if (type.isSome() && *type == SdpMediaSection::kApplication) {
    MOZ_ASSERT(false);
    return Nothing();
  }

  // Look through transceivers that are not mapped to an m-section
  for (auto& transceiver : mTransceivers) {
    if (transceiver.GetMediaType() == SdpMediaSection::kApplication) {
      continue;
    }
    if (transceiver.IsFreeToUse() &&
        (!magic || transceiver.HasAddTrackMagic()) &&
        (type.isNothing() || transceiver.GetMediaType() == *type)) {
      return Some(transceiver);
    }
  }

  return Nothing();
}

void JsepSessionImpl::RollbackLocalOffer() {
  for (size_t i = 0; i < mTransceivers.size(); ++i) {
    auto& transceiver = mTransceivers[i];
    if (mOldTransceivers.size() > i) {
      transceiver.Rollback(mOldTransceivers[i], false);
      mOldTransceivers[i] = transceiver;
      continue;
    }

    JsepTransceiver temp(transceiver.GetMediaTypeInfo(), *mUuidGen);
    InitTransceiver(temp);
    transceiver.Rollback(temp, false);
    mOldTransceivers.push_back(transceiver);
  }

  mTransceivers = std::move(mOldTransceivers);
}

void JsepSessionImpl::RollbackRemoteOffer() {
  for (size_t i = 0; i < mTransceivers.size(); ++i) {
    auto& transceiver = mTransceivers[i];
    if (mOldTransceivers.size() > i) {
      // Some stuff cannot be rolled back. Save this information.
      transceiver.Rollback(mOldTransceivers[i], true);
      mOldTransceivers[i] = transceiver;
      continue;
    }

    if (transceiver.HasLevel()) {
      // New transceiver, that was either created by the remote offer, or
      // attached to the remote offer.
      // We rollback even for transceivers we will remove, just to ensure we end
      // up at the starting state.
      JsepTransceiver temp(transceiver.GetMediaTypeInfo(), *mUuidGen);
      InitTransceiver(temp);
      transceiver.Rollback(temp, true);

      if (transceiver.OnlyExistsBecauseOfSetRemote()) {
        transceiver.SetStopped();
        transceiver.Disassociate();
        transceiver.SetRemoved();
      } else {
        // Oof. This hangs around because of addTrack. Make it magic!
        transceiver.SetAddTrackMagic();
      }
    }  // else, _we_ added this and it is not attached to the remote offer yet

    mOldTransceivers.push_back(transceiver);
  }

  mTransceivers = std::move(mOldTransceivers);
}

JsepSession::Result JsepSessionImpl::ValidateLocalDescription(
    const Sdp& description, JsepSdpType type) {
  Sdp* generated = nullptr;
  // TODO(bug 1095226): Better checking.
  if (type == kJsepSdpOffer) {
    generated = mGeneratedOffer.get();
  } else {
    generated = mGeneratedAnswer.get();
  }

  if (!generated) {
    JSEP_SET_ERROR(
        "Calling SetLocal without first calling CreateOffer/Answer"
        " is not supported. This operation should have been rejected"
        " before even getting here.");
    return Result(dom::PCError::OperationError);
  }

  if (description.GetMediaSectionCount() != generated->GetMediaSectionCount()) {
    JSEP_SET_ERROR("Changing the number of m-sections is not allowed");
    return Result(dom::PCError::InvalidModificationError);
  }

  for (size_t i = 0; i < description.GetMediaSectionCount(); ++i) {
    auto& origMsection = generated->GetMediaSection(i);
    auto& finalMsection = description.GetMediaSection(i);
    if (!origMsection.GetMediaTypeInfo().MatchesTypeOf(
            finalMsection.GetMediaTypeInfo())) {
      JSEP_SET_ERROR("Changing the media-type of m-sections is not allowed");
      return Result(dom::PCError::InvalidModificationError);
    }

    // These will be present in reoffer
    if (!mCurrentLocalDescription) {
      if (finalMsection.GetAttributeList().HasAttribute(
              SdpAttribute::kCandidateAttribute)) {
        JSEP_SET_ERROR(
            "Adding your own candidate attributes is not allowed,"
            "and SDP munging in general is not allowed either.");
        return Result(dom::PCError::InvalidModificationError);
      }

      if (finalMsection.GetAttributeList().HasAttribute(
              SdpAttribute::kEndOfCandidatesAttribute)) {
        JSEP_SET_ERROR("Why are you trying to set a=end-of-candidates?");
        return Result(dom::PCError::InvalidAccessError);
      }
    }

    if (mSdpHelper.MsectionIsDisabled(finalMsection)) {
      continue;
    }

    if (!finalMsection.GetAttributeList().HasAttribute(
            SdpAttribute::kMidAttribute)) {
      JSEP_SET_ERROR("Local descriptions must have a=mid attributes.");
      return Result(dom::PCError::InvalidModificationError);
    }

    if (finalMsection.GetAttributeList().GetMid() !=
        origMsection.GetAttributeList().GetMid()) {
      JSEP_SET_ERROR("Changing the mid of m-sections is not allowed.");
      return Result(dom::PCError::InvalidModificationError);
    }

    if (mSdpHelper.FingerprintsDiffer(origMsection, finalMsection)) {
      JSEP_SET_ERROR(
          "Changing the DTLS fingerprint of m-sections is not allowed.");
      return Result(dom::PCError::InvalidModificationError);
    }

    // TODO(bug 1095218): Check msid
    // TODO(bug 1095226): Check ice-ufrag and ice-pwd
    // TODO(bug 1095226): Check payload types (at least ensure that payload
    // types we don't actually support weren't added)
    // TODO(bug 1095226): Check ice-options?
  }

  if (description.GetAttributeList().HasAttribute(
          SdpAttribute::kIceLiteAttribute)) {
    JSEP_SET_ERROR("Running ICE in lite mode is unsupported");
    return Result(dom::PCError::InvalidModificationError);
  }

  return Result();
}

JsepSession::Result JsepSessionImpl::ValidateRemoteDescription(
    const Sdp& description) {
  if (!mCurrentLocalDescription) {
    // Initial offer; nothing to validate besides the stuff in ParseSdp
    return Result();
  }

  if (mCurrentLocalDescription->GetMediaSectionCount() >
      description.GetMediaSectionCount()) {
    JSEP_SET_ERROR(
        "New remote description has fewer m-sections than the "
        "previous remote description.");
    return Result(dom::PCError::InvalidModificationError);
  }

  for (size_t i = 0; i < description.GetMediaSectionCount(); ++i) {
    const SdpAttributeList& attrs =
        description.GetMediaSection(i).GetAttributeList();

    if (attrs.HasAttribute(SdpAttribute::kExtmapAttribute)) {
      for (const auto& ext : attrs.GetExtmap().mExtmaps) {
        if (mExtmapEntriesEverNegotiated.count(ext.entry) &&
            mExtmapEntriesEverNegotiated[ext.entry] != ext.extensionname) {
          JSEP_SET_ERROR(
              "Remote description attempted to remap RTP extension id "
              << ext.entry << " from "
              << mExtmapEntriesEverNegotiated[ext.entry] << " to "
              << ext.extensionname);
          return Result(dom::PCError::InvalidAccessError);
        }
      }
    }
  }

  if (!mCurrentRemoteDescription) {
    // No further checking for initial answers
    return Result();
  }

  // These are solely to check that bundle is valid
  SdpHelper::BundledMids bundledMids;
  nsresult ns_rv = GetNegotiatedBundledMids(&bundledMids);
  NS_ENSURE_SUCCESS(ns_rv, dom::PCError::InvalidAccessError);

  SdpHelper::BundledMids newBundledMids;
  ns_rv = mSdpHelper.GetBundledMids(description, &newBundledMids);
  NS_ENSURE_SUCCESS(ns_rv, dom::PCError::InvalidAccessError);

  // check for partial ice restart, which is not supported
  Maybe<bool> iceCredsDiffer;
  for (size_t i = 0; i < mCurrentRemoteDescription->GetMediaSectionCount();
       ++i) {
    const SdpMediaSection& newMsection = description.GetMediaSection(i);
    const SdpMediaSection& oldMsection =
        mCurrentRemoteDescription->GetMediaSection(i);

    // https://www.rfc-editor.org/rfc/rfc9429#section-5.2.2
    //
    // If any RtpTransceiver has been added and there exists an "m=" section
    // with a zero port in the current local description or the current remote
    // description, that "m=" section MUST be recycled by generating an "m="
    // section for the added RtpTransceiver as if the "m=" section were being
    // added to the session description (including a new MID value) and placing
    // it at the same index as the "m=" section with a zero port.
    //
    // Note:
    // An m= section whose media type we do not know is rejected in our own
    // answer, so the zero port is in the local description rather than the
    // remote one.
    if (mSdpHelper.MsectionIsDisabled(oldMsection) ||
        mSdpHelper.MsectionIsDisabled(
            mCurrentLocalDescription->GetMediaSection(i))) {
      bool oldIsApp =
          oldMsection.GetMediaType() == SdpMediaSection::kApplication;
      bool newIsApp =
          newMsection.GetMediaType() == SdpMediaSection::kApplication;
      if (oldIsApp != newIsApp) {
        JSEP_SET_ERROR("Remote description changes the media type of m-line "
                       << i
                       << " to or from application, which is not"
                          " permitted");
        return Result(dom::PCError::InvalidAccessError);
      }
      continue;
    }

    if (!oldMsection.GetMediaTypeInfo().MatchesTypeOf(
            newMsection.GetMediaTypeInfo())) {
      JSEP_SET_ERROR("Remote description changes the media type of m-line "
                     << i
                     << "; media type changes are only permitted when the "
                        "m-section was previously disabled");
      return Result(dom::PCError::InvalidAccessError);
    }

    if (mSdpHelper.MsectionIsDisabled(newMsection)) {
      // Nothing else to do media is being disabled to possibly be reused.
      continue;
    }

    bool differ = mSdpHelper.IceCredentialsDiffer(newMsection, oldMsection);

    if (mIsPendingOfferer.isSome() && *mIsPendingOfferer && differ &&
        !IsIceRestarting()) {
      JSEP_SET_ERROR(
          "Remote description indicates ICE restart but offer did not "
          "request ICE restart (new remote description changes either "
          "the ice-ufrag or ice-pwd)");
      return Result(dom::PCError::InvalidAccessError);
    }

    if (mSdpHelper.FingerprintsDiffer(newMsection, oldMsection) && !differ) {
      JSEP_SET_ERROR(
          "Remote description changes the DTLS fingerprint without changing "
          "the ICE credentials (i.e. without an ICE restart), which is not "
          "permitted (see RFC 9429 section 5.11)");
      return Result(dom::PCError::InvalidAccessError);
    }

    // Detect whether all the creds are the same or all are different
    if (!iceCredsDiffer.isSome()) {
      // for the first msection capture whether creds are different or same
      iceCredsDiffer = mozilla::Some(differ);
    } else if (iceCredsDiffer.isSome() && *iceCredsDiffer != differ) {
      // subsequent msections must match the first sections
      JSEP_SET_ERROR(
          "Partial ICE restart is unsupported at this time "
          "(new remote description changes either the ice-ufrag "
          "or ice-pwd on fewer than all msections)");
      return Result(dom::PCError::OperationError);
    }
  }

  return Result();
}

JsepSession::Result JsepSessionImpl::CheckRtcpMux(const Sdp& description) {
  if (mRtcpMuxPolicy != kRtcpMuxRequire) {
    return Result();
  }
  for (size_t i = 0; i < description.GetMediaSectionCount(); ++i) {
    const SdpMediaSection& msection = description.GetMediaSection(i);
    if (mSdpHelper.MsectionIsDisabled(msection)) {
      continue;
    }
    if (!mSdpHelper.HasRtcp(msection.GetProtocol())) {
      continue;
    }
    if (!msection.GetAttributeList().HasAttribute(
            SdpAttribute::kRtcpMuxAttribute)) {
      JSEP_SET_ERROR(
          "m-section at level "
          << i << " is missing a=rtcp-mux, which is required by rtcpMuxPolicy");
      return Result(dom::PCError::InvalidAccessError);
    }
  }
  return Result();
}

// Bug 2036111 - Syntax Errors Should Be Parser Generated
// All uses of this function should be eliminated when implement Bug 2036111.
inline static JsepSession::Result MkContextlessSdpResult(
    Maybe<dom::PCError> aErr) {
  constexpr const size_t BOGUS_LINE_NUMBER = 0;
  if (aErr) {
    const auto err = aErr.extract();
    if (err == dom::PCError::SyntaxError) {
      return JsepSession::Result(err, "sdp-syntax-error",
                                 Some(BOGUS_LINE_NUMBER));
    }
    return JsepSession::Result(err);
  }
  return JsepSession::Result();
}

JsepSession::Result JsepSessionImpl::ValidateOffer(const Sdp& offer) {
  Result result = CheckRtcpMux(offer);
  if (result.mError.isSome()) {
    return result;
  }
  return MkContextlessSdpResult(
      mSdpHelper.ValidateTransportAttributes(offer, sdp::kOffer));
}

JsepSession::Result JsepSessionImpl::ValidateAnswer(const Sdp& offer,
                                                    const Sdp& answer) {
  if (offer.GetMediaSectionCount() != answer.GetMediaSectionCount()) {
    JSEP_SET_ERROR("Offer and answer have different number of m-lines "
                   << "(" << offer.GetMediaSectionCount() << " vs "
                   << answer.GetMediaSectionCount() << ")");
    return Result(dom::PCError::InvalidAccessError);
  }

  Result result = CheckRtcpMux(answer);
  if (result.mError.isSome()) {
    return result;
  }

  Maybe validation_err =
      mSdpHelper.ValidateTransportAttributes(answer, sdp::kAnswer);
  if (validation_err.isSome()) {
    return MkContextlessSdpResult(validation_err);
  }

  for (size_t i = 0; i < offer.GetMediaSectionCount(); ++i) {
    const SdpMediaSection& offerMsection = offer.GetMediaSection(i);
    const SdpMediaSection& answerMsection = answer.GetMediaSection(i);

    if (!offerMsection.GetMediaTypeInfo().MatchesTypeOf(
            answerMsection.GetMediaTypeInfo())) {
      JSEP_SET_ERROR("Answer and offer have different media types at m-line "
                     << i);
      return Result(dom::PCError::InvalidAccessError);
    }

    if (mSdpHelper.MsectionIsDisabled(answerMsection)) {
      continue;
    }

    if (mSdpHelper.MsectionIsDisabled(offerMsection)) {
      JSEP_SET_ERROR(
          "Answer tried to enable an m-section that was disabled in the offer");
      return Result(dom::PCError::InvalidAccessError);
    }

    if (!offerMsection.IsSending() && answerMsection.IsReceiving()) {
      JSEP_SET_ERROR("Answer tried to set recv when offer did not set send");
      return Result(dom::PCError::InvalidAccessError);
    }

    if (!offerMsection.IsReceiving() && answerMsection.IsSending()) {
      JSEP_SET_ERROR("Answer tried to set send when offer did not set recv");
      return Result(dom::PCError::InvalidAccessError);
    }

    const SdpAttributeList& answerAttrs(answerMsection.GetAttributeList());
    const SdpAttributeList& offerAttrs(offerMsection.GetAttributeList());
    if (answerAttrs.HasAttribute(SdpAttribute::kMidAttribute) &&
        offerAttrs.HasAttribute(SdpAttribute::kMidAttribute) &&
        offerAttrs.GetMid() != answerAttrs.GetMid()) {
      JSEP_SET_ERROR("Answer changes mid for level, was \'"
                     << offerMsection.GetAttributeList().GetMid()
                     << "\', now \'"
                     << answerMsection.GetAttributeList().GetMid() << "\'");
      return Result(dom::PCError::InvalidAccessError);
    }

    // Sanity check extmap
    if (answerAttrs.HasAttribute(SdpAttribute::kExtmapAttribute)) {
      if (!offerAttrs.HasAttribute(SdpAttribute::kExtmapAttribute)) {
        JSEP_SET_ERROR("Answer adds extmap attributes to level " << i);
        return Result(dom::PCError::InvalidAccessError);
      }

      for (const auto& ansExt : answerAttrs.GetExtmap().mExtmaps) {
        bool found = false;
        for (const auto& offExt : offerAttrs.GetExtmap().mExtmaps) {
          if (ansExt.extensionname == offExt.extensionname) {
            if ((ansExt.direction & reverse(offExt.direction)) !=
                ansExt.direction) {
              // FIXME we do not return an error here, because Chrome up to
              // version 57 is actually tripping over this if they are the
              // answerer. See bug 1355010 for details.
              MOZ_MTLOG(ML_WARNING,
                        "[" << mName
                            << "]: Answer has inconsistent"
                               " direction on extmap attribute at level "
                            << i << " (" << ansExt.extensionname
                            << "). Offer had " << offExt.direction
                            << ", answer had " << ansExt.direction << ".");
              // return NS_ERROR_INVALID_ARG;
            }

            if (offExt.entry < 4096 && (offExt.entry != ansExt.entry)) {
              JSEP_SET_ERROR("Answer changed id for extmap attribute at level "
                             << i << " (" << offExt.extensionname << ") from "
                             << offExt.entry << " to " << ansExt.entry << ".");
              return Result(dom::PCError::InvalidAccessError);
            }

            if (ansExt.entry >= 4096) {
              JSEP_SET_ERROR("Answer used an invalid id ("
                             << ansExt.entry
                             << ") for extmap attribute at level " << i << " ("
                             << ansExt.extensionname << ").");
              return Result(dom::PCError::InvalidAccessError);
            }

            found = true;
            break;
          }
        }

        if (!found) {
          JSEP_SET_ERROR("Answer has extmap "
                         << ansExt.extensionname
                         << " at "
                            "level "
                         << i << " that was not present in offer.");
          return Result(dom::PCError::InvalidAccessError);
        }
      }
    }
  }

  return Result();
}

// Empty if |aAttrs| has no attribute of that type.
static std::string SerializeAttribute(const SdpAttributeList& aAttrs,
                                      SdpAttribute::AttributeType aType) {
  std::ostringstream os;
  if (aAttrs.HasAttribute(aType)) {
    aAttrs.GetAttribute(aType)->Serialize(os);
  }
  return os.str();
}

// Once a provisional answer has been applied, the answers that follow it
// (final, or more pranswers) may change what the media looks like, but not the
// transports it runs over. RFC 9429 sections 5.10 and 5.11 permit new ICE
// credentials or a new DTLS fingerprint (provided the offer had new
// credentials/fingerprints), but say nothing about BUNDLE, rtcp-mux, the DTLS
// role, recycling (or resurrection!) of m-sections, ice-lite, ice-options,
// msid, and on and on. We call shenanigans and forbid all of this. If someone
// complains, we'll cross that bridge when we get there.
JsepSession::Result JsepSessionImpl::ValidateAnswerAgainstPranswer(
    const Sdp& pranswer, const Sdp& answer) {
  struct Frozen {
    SdpAttribute::AttributeType mType;
    const char* mWhat;
  };

  // Session level. The identity assertion is validated again for every
  // remote description, so changing it would swap the peer identity out from
  // under JS; the ICE roles were decided from ice-lite at the provisional
  // answer.
  static const Frozen kSessionFrozen[] = {
      {SdpAttribute::kGroupAttribute, "group"},
      {SdpAttribute::kIdentityAttribute, "identity"},
      {SdpAttribute::kIceLiteAttribute, "ice-lite"},
  };
  for (const auto& [type, what] : kSessionFrozen) {
    if (SerializeAttribute(pranswer.GetAttributeList(), type) !=
        SerializeAttribute(answer.GetAttributeList(), type)) {
      JSEP_SET_ERROR("Answer changes " << what << " of the provisional answer");
      return Result(dom::PCError::InvalidAccessError);
    }
  }

  // Per m-section. Simulcast is driven by SDP and by JS at the same time, and
  // every answer, provisional or not, prunes send encodings that nothing puts
  // back. RFC 9429 section 5.11 would have an SCTP port change tear down the
  // association, even in have-remote-pranswer; we have no exercised path for
  // that.
  // TODO: Check a=tls-id when we support it, see
  // https://bugzilla.mozilla.org/show_bug.cgi?id=1319681
  // If we ever support ice-mismatch or ice-pacing, we'll want to check those
  // here too.
  // See https://bugzilla.mozilla.org/show_bug.cgi?id=2073595
  // See https://bugzilla.mozilla.org/show_bug.cgi?id=2073597
  static const Frozen kMsectionFrozen[] = {
      {SdpAttribute::kIceUfragAttribute, "ice-ufrag"},
      {SdpAttribute::kIcePwdAttribute, "ice-pwd"},
      {SdpAttribute::kIceOptionsAttribute, "ice-options"},
      {SdpAttribute::kFingerprintAttribute, "fingerprint"},
      {SdpAttribute::kSetupAttribute, "setup"},
      {SdpAttribute::kRtcpMuxAttribute, "rtcp-mux"},
      {SdpAttribute::kRtcpRsizeAttribute, "rtcp-rsize"},
      {SdpAttribute::kSimulcastAttribute, "simulcast"},
      {SdpAttribute::kRidAttribute, "rid"},
      {SdpAttribute::kSctpPortAttribute, "sctp-port"},
      {SdpAttribute::kSctpmapAttribute, "sctpmap"},
  };

  // ValidateAnswer has checked both against the offer, so the m-section
  // counts match.
  for (size_t i = 0; i < answer.GetMediaSectionCount(); ++i) {
    const SdpMediaSection& pranswerMsection = pranswer.GetMediaSection(i);
    const SdpMediaSection& answerMsection = answer.GetMediaSection(i);

    const bool answerDisabled = mSdpHelper.MsectionIsDisabled(answerMsection);
    if (answerDisabled != mSdpHelper.MsectionIsDisabled(pranswerMsection)) {
      JSEP_SET_ERROR("Answer " << (answerDisabled ? "rejects" : "accepts")
                               << " m-section " << i
                               << ", which the provisional answer "
                               << (answerDisabled ? "accepted" : "rejected"));
      return Result(dom::PCError::InvalidAccessError);
    }
    if (answerDisabled) {
      continue;
    }

    const SdpAttributeList& pranswerAttrs = pranswerMsection.GetAttributeList();
    const SdpAttributeList& answerAttrs = answerMsection.GetAttributeList();

    for (const auto& [type, what] : kMsectionFrozen) {
      if (SerializeAttribute(pranswerAttrs, type) !=
          SerializeAttribute(answerAttrs, type)) {
        JSEP_SET_ERROR("Answer changes " << what
                                         << " of the provisional answer at "
                                            "m-section "
                                         << i);
        return Result(dom::PCError::InvalidAccessError);
      }
    }

    // extmap ids are pinned to the offer's by ValidateAnswer, for the
    // provisional answer and this one alike, so an id cannot change meaning
    // between them. Which extensions are accepted is a media choice and may.

    // An answer may pick payload types of its own (asymmetric payload types),
    // and which codecs it accepts may change, but a number that appears in
    // both the provisional answer and this one has to keep its meaning.
    if (pranswerAttrs.HasAttribute(SdpAttribute::kRtpmapAttribute) &&
        answerAttrs.HasAttribute(SdpAttribute::kRtpmapAttribute)) {
      const SdpRtpmapAttributeList& answerRtpmap = answerAttrs.GetRtpmap();
      for (const auto& entry : pranswerAttrs.GetRtpmap().mRtpmaps) {
        if (!answerRtpmap.HasEntry(entry.pt)) {
          continue;
        }
        const auto& answerEntry = answerRtpmap.GetEntry(entry.pt);
        if (answerEntry.name != entry.name ||
            answerEntry.clock != entry.clock ||
            answerEntry.channels != entry.channels) {
          JSEP_SET_ERROR("Answer changes payload type "
                         << entry.pt
                         << " of the provisional answer at m-section " << i);
          return Result(dom::PCError::InvalidAccessError);
        }
      }
    }

    // msid may appear (the answerer started sending) or disappear (it
    // stopped), but a stream JS has already been told about keeps its id.
    if (pranswerAttrs.HasAttribute(SdpAttribute::kMsidAttribute) &&
        answerAttrs.HasAttribute(SdpAttribute::kMsidAttribute)) {
      const auto& pranswerMsids = pranswerAttrs.GetMsid().mMsids;
      const auto& answerMsids = answerAttrs.GetMsid().mMsids;
      bool sameStreams = pranswerMsids.size() == answerMsids.size();
      for (size_t j = 0; sameStreams && j < pranswerMsids.size(); ++j) {
        sameStreams = pranswerMsids[j].identifier == answerMsids[j].identifier;
      }
      if (!sameStreams) {
        JSEP_SET_ERROR(
            "Answer changes the msid of the provisional answer at "
            "m-section "
            << i);
        return Result(dom::PCError::InvalidAccessError);
      }
    }

    // max-message-size is meant to be picked up from any answer (webrtc-pc
    // "set the session description", step 10.1), so it is left alone.
  }

  return Result();
}

nsresult JsepSessionImpl::CreateGenericSDP(UniquePtr<Sdp>* sdpp) {
  // draft-ietf-rtcweb-jsep-08 Section 5.2.1:
  //  o  The second SDP line MUST be an "o=" line, as specified in
  //     [RFC4566], Section 5.2.  The value of the <username> field SHOULD
  //     be "-".  The value of the <sess-id> field SHOULD be a
  //     cryptographically random number.  To ensure uniqueness, this
  //     number SHOULD be at least 64 bits long.  The value of the <sess-
  //     version> field SHOULD be zero.  The value of the <nettype>
  //     <addrtype> <unicast-address> tuple SHOULD be set to a non-
  //     meaningful address, such as IN IP4 0.0.0.0, to prevent leaking the
  //     local address in this field.  As mentioned in [RFC4566], the
  //     entire o= line needs to be unique, but selecting a random number
  //     for <sess-id> is sufficient to accomplish this.
  //
  // Historical note: we used to report the actual version number here, after
  // "SDPARTA-", but that becomes a problem starting with version 100, since
  // some services parse 100 as "10" and give us legacy/broken behavior. So
  // we're freezing the version number at 99.0 in this string.
  auto origin = SdpOrigin("mozilla...THIS_IS_SDPARTA-99.0", mSessionId,
                          mSessionVersion, sdp::kIPv4, "0.0.0.0");

  std::unique_ptr<Sdp> sdp = std::make_unique<SdpImpl>(origin);

  if (mDtlsFingerprints.empty()) {
    JSEP_SET_ERROR("Missing DTLS fingerprint");
    return NS_ERROR_FAILURE;
  }

  UniquePtr<SdpFingerprintAttributeList> fpl =
      std::make_unique<SdpFingerprintAttributeList>();
  for (auto& dtlsFingerprint : mDtlsFingerprints) {
    fpl->PushEntry(dtlsFingerprint.mAlgorithm, dtlsFingerprint.mValue);
  }
  sdp->GetAttributeList().SetAttribute(std::move(fpl));

  auto iceOpts =
      std::make_unique<SdpOptionsAttribute>(SdpAttribute::kIceOptionsAttribute);
  iceOpts->PushEntry("trickle");
  sdp->GetAttributeList().SetAttribute(std::move(iceOpts));

  // This assumes content doesn't add a bunch of msid attributes with a
  // different semantic in mind.
  std::vector<std::string> msids;
  msids.push_back("*");
  mSdpHelper.SetupMsidSemantic(msids, sdp.get());

  *sdpp = std::move(sdp);
  return NS_OK;
}

nsresult JsepSessionImpl::SetupIds() {
  SECStatus rv = PK11_GenerateRandom(
      reinterpret_cast<unsigned char*>(&mSessionId), sizeof(mSessionId));
  // RFC 3264 says that session-ids MUST be representable as a _signed_
  // 64 bit number, meaning the MSB cannot be set.
  mSessionId = mSessionId >> 1;
  if (rv != SECSuccess) {
    JSEP_SET_ERROR("Failed to generate session id: " << rv);
    return NS_ERROR_FAILURE;
  }

  if (!mUuidGen->Generate(&mDefaultRemoteStreamId)) {
    JSEP_SET_ERROR("Failed to generate default uuid for streams");
    return NS_ERROR_FAILURE;
  }

  if (!mUuidGen->Generate(&mCNAME)) {
    JSEP_SET_ERROR("Failed to generate CNAME");
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

void JsepSessionImpl::SetDefaultCodecs(
    const nsTArray<UniquePtr<JsepCodecDescription>>& aPreferredCodecs) {
  mSupportedCodecs.Clear();

  for (const auto& codec : aPreferredCodecs) {
    mSupportedCodecs.EmplaceBack(codec->Clone());
  }
}

void JsepSessionImpl::SetState(JsepSignalingState state) {
  if (state == mState) return;

  MOZ_MTLOG(ML_NOTICE, "[" << mName << "]: " << GetStateStr(mState) << " -> "
                           << GetStateStr(state));
  mState = state;
}

JsepSession::Result JsepSessionImpl::AddRemoteIceCandidate(
    const std::string& candidate, const std::string& mid,
    const Maybe<uint16_t>& level, const std::string& ufrag,
    std::string* transportId) {
  mLastError.clear();
  if (!mCurrentRemoteDescription && !mPendingRemoteDescription) {
    JSEP_SET_ERROR("Cannot add ICE candidate when there is no remote SDP");
    return dom::PCError::InvalidStateError;
  }

  if (mid.empty() && !level.isSome() && candidate.empty()) {
    // Set end-of-candidates on SDP
    if (mCurrentRemoteDescription) {
      nsresult rv = mSdpHelper.SetIceGatheringComplete(
          mCurrentRemoteDescription.get(), ufrag);
      NS_ENSURE_SUCCESS(rv, dom::PCError::OperationError);
    }

    if (mPendingRemoteDescription) {
      // If we had an error when adding the candidate to the current
      // description, we stomp them here. This is deliberate.
      nsresult rv = mSdpHelper.SetIceGatheringComplete(
          mPendingRemoteDescription.get(), ufrag);
      NS_ENSURE_SUCCESS(rv, dom::PCError::OperationError);
    }
    return Result();
  }

  Maybe<JsepTransceiver> transceiver;
  if (!mid.empty()) {
    transceiver = GetTransceiverForMid(mid);
  } else if (level.isSome()) {
    transceiver = GetTransceiverForLevel(level.value());
  }

  if (!transceiver) {
    JSEP_SET_ERROR("Cannot set ICE candidate for level="
                   << level << " mid=" << mid << ": No such transceiver.");
    return dom::PCError::OperationError;
  }

  if (level.isSome() && transceiver->GetLevel() != level.value()) {
    MOZ_MTLOG(ML_WARNING, "Mismatch between mid and level - \""
                              << mid << "\" is not the mid for level "
                              << level);
  }

  *transportId = transceiver->mTransport.mTransportId;
  nsresult rv = NS_ERROR_UNEXPECTED;
  if (mCurrentRemoteDescription) {
    rv =
        mSdpHelper.AddCandidateToSdp(mCurrentRemoteDescription.get(), candidate,
                                     transceiver->GetLevel(), ufrag);
  }

  if (mPendingRemoteDescription) {
    // If we had an error when adding the candidate to the current description,
    // we stomp them here. This is deliberate.
    rv =
        mSdpHelper.AddCandidateToSdp(mPendingRemoteDescription.get(), candidate,
                                     transceiver->GetLevel(), ufrag);
  }

  NS_ENSURE_SUCCESS(rv, dom::PCError::OperationError);
  return Result();
}

nsresult JsepSessionImpl::AddLocalIceCandidate(const std::string& candidate,
                                               const std::string& transportId,
                                               const std::string& ufrag,
                                               uint16_t* level,
                                               std::string* mid,
                                               bool* skipped) {
  mLastError.clear();
  *skipped = true;
  if (!mCurrentLocalDescription && !mPendingLocalDescription) {
    JSEP_SET_ERROR("Cannot add ICE candidate when there is no local SDP");
    return NS_ERROR_UNEXPECTED;
  }

  Maybe<const JsepTransceiver> transceiver =
      GetTransceiverWithTransport(transportId);
  if (!transceiver || !transceiver->IsAssociated()) {
    // mainly here to make some testing less complicated, but also just in case
    return NS_OK;
  }

  *level = transceiver->GetLevel();
  *mid = transceiver->GetMid();

  nsresult rv = NS_ERROR_INVALID_ARG;
  if (mCurrentLocalDescription) {
    rv = mSdpHelper.AddCandidateToSdp(mCurrentLocalDescription.get(), candidate,
                                      *level, ufrag);
  }

  if (mPendingLocalDescription) {
    // If we had an error when adding the candidate to the current description,
    // we stomp them here. This is deliberate.
    rv = mSdpHelper.AddCandidateToSdp(mPendingLocalDescription.get(), candidate,
                                      *level, ufrag);
  }

  *skipped = false;
  return rv;
}

nsresult JsepSessionImpl::UpdateDefaultCandidate(
    const std::string& defaultCandidateAddr, uint16_t defaultCandidatePort,
    const std::string& defaultRtcpCandidateAddr,
    uint16_t defaultRtcpCandidatePort, const std::string& transportId) {
  mLastError.clear();

  mozilla::Sdp* sdp =
      GetParsedLocalDescription(kJsepDescriptionPendingOrCurrent);

  if (!sdp) {
    JSEP_SET_ERROR("Cannot add ICE candidate in state " << GetStateStr(mState));
    return NS_ERROR_UNEXPECTED;
  }

  for (const auto& transceiver : mTransceivers) {
    // We set the default address for bundled m-sections, but not candidate
    // attributes. Ugh.
    if (transceiver.mTransport.mTransportId == transportId) {
      MOZ_ASSERT(transceiver.HasLevel(),
                 "Transceiver has a transport, but no level! "
                 "This should never happen.");
      std::string defaultRtcpCandidateAddrCopy(defaultRtcpCandidateAddr);
      if (mState == kJsepStateStable) {
        if (transceiver.mTransport.mComponents == 1) {
          // We know we're doing rtcp-mux by now. Don't create an rtcp attr.
          defaultRtcpCandidateAddrCopy = "";
          defaultRtcpCandidatePort = 0;
        }
      }

      size_t level = transceiver.GetLevel();
      if (level >= sdp->GetMediaSectionCount()) {
        MOZ_ASSERT(false, "Transceiver's level is too large!");
        JSEP_SET_ERROR("Transceiver's level is too large!");
        return NS_ERROR_FAILURE;
      }

      auto& msection = sdp->GetMediaSection(level);

      // Do not add default candidate to a bundle-only m-section, since that
      // might confuse endpoints that do not support bundle-only.
      if (!msection.GetAttributeList().HasAttribute(
              SdpAttribute::kBundleOnlyAttribute)) {
        mSdpHelper.SetDefaultAddresses(
            defaultCandidateAddr, defaultCandidatePort,
            defaultRtcpCandidateAddrCopy, defaultRtcpCandidatePort, &msection);
      }
    }
  }

  return NS_OK;
}

nsresult JsepSessionImpl::GetNegotiatedBundledMids(
    SdpHelper::BundledMids* bundledMids) {
  const Sdp* answerSdp = GetAnswer();

  if (!answerSdp) {
    return NS_OK;
  }

  return mSdpHelper.GetBundledMids(*answerSdp, bundledMids);
}

bool JsepSessionImpl::LocalOfferedRecvParamsChanged(const std::string& aMid) {
  const mozilla::Sdp* currentSdp =
      GetParsedLocalDescription(kJsepDescriptionCurrent);
  if (!currentSdp) {
    return true;
  }
  const SdpMediaSection* current =
      mSdpHelper.FindMsectionByMid(*currentSdp, aMid);
  if (!current) {
    return true;
  }

  const mozilla::Sdp* pendingSdp =
      GetParsedLocalDescription(kJsepDescriptionPending);
  if (!pendingSdp) {
    return false;
  }
  const SdpMediaSection* pending =
      mSdpHelper.FindMsectionByMid(*pendingSdp, aMid);
  if (!pending) {
    return false;
  }

  return pending->GetFormats() != current->GetFormats() ||
         pending->GetDirectionAttribute().mValue !=
             current->GetDirectionAttribute().mValue;
}

mozilla::Sdp* JsepSessionImpl::GetParsedLocalDescription(
    JsepDescriptionPendingOrCurrent type) const {
  if (type == kJsepDescriptionPending) {
    return mPendingLocalDescription.get();
  } else if (mPendingLocalDescription &&
             type == kJsepDescriptionPendingOrCurrent) {
    return mPendingLocalDescription.get();
  }
  return mCurrentLocalDescription.get();
}

mozilla::Sdp* JsepSessionImpl::GetParsedRemoteDescription(
    JsepDescriptionPendingOrCurrent type) const {
  if (type == kJsepDescriptionPending) {
    return mPendingRemoteDescription.get();
  } else if (mPendingRemoteDescription &&
             type == kJsepDescriptionPendingOrCurrent) {
    return mPendingRemoteDescription.get();
  }
  return mCurrentRemoteDescription.get();
}

const Sdp* JsepSessionImpl::GetAnswer() const {
  return (mIsCurrentOfferer.isSome() && *mIsCurrentOfferer)
             ? mCurrentRemoteDescription.get()
             : mCurrentLocalDescription.get();
}

void JsepSessionImpl::SetIceRestarting(bool restarting) {
  if (restarting) {
    // not restarting -> restarting
    if (!IsIceRestarting()) {
      // We don't set this more than once, so the old ufrag/pwd is preserved
      // even if we CreateOffer({iceRestart:true}) multiple times in a row.
      mOldIceUfrag = mIceUfrag;
      mOldIcePwd = mIcePwd;
    }
    mIceUfrag = GetRandomHex(1);
    mIcePwd = GetRandomHex(4);
  } else if (IsIceRestarting()) {
    // restarting -> not restarting, restore old ufrag/pwd
    mIceUfrag = mOldIceUfrag;
    mIcePwd = mOldIcePwd;
    mOldIceUfrag.clear();
    mOldIcePwd.clear();
  }
}

nsresult JsepSessionImpl::Close() {
  mLastError.clear();
  SetState(kJsepStateClosed);
  return NS_OK;
}

const std::string JsepSessionImpl::GetLastError() const { return mLastError; }

const std::vector<std::pair<size_t, std::string>>&
JsepSessionImpl::GetLastSdpParsingErrors() const {
  return mLastSdpParsingErrors;
}

bool JsepSessionImpl::CheckNegotiationNeeded() const {
  MOZ_ASSERT(mState == kJsepStateStable);

  for (const auto& transceiver : mTransceivers) {
    if (transceiver.IsStopped()) {
      // Nothing to do with this
      continue;
    }

    if (transceiver.GetMediaType() == SdpMediaSection::kApplication) {
      // Whether this needs negotiation depends on whether a datachannel was
      // created, which JSEP does not know about. `alwaysNegotiateDataChannels`
      // can also create a datachannel transceiver without a datachannel.
      continue;
    }

    if (transceiver.IsStopping()) {
      MOZ_MTLOG(ML_DEBUG, "[" << mName
                              << "]: Negotiation needed because of "
                                 "transceiver we need to stop");
      return true;
    }

    if (!transceiver.IsAssociated()) {
      MOZ_MTLOG(ML_DEBUG, "[" << mName
                              << "]: Negotiation needed because of "
                                 "transceiver we need to associate.");
      return true;
    }

    MOZ_ASSERT(transceiver.IsAssociated() && !transceiver.IsStopping() &&
               !transceiver.IsStopped());

    if (!mCurrentLocalDescription || !mCurrentRemoteDescription) {
      MOZ_CRASH(
          "Transceivers should not be associated if we're in stable "
          "before the first negotiation.");
      continue;
    }

    if (!transceiver.HasLevel()) {
      MOZ_CRASH("Associated transceivers should always have a level.");
      continue;
    }

    size_t level = transceiver.GetLevel();
    if (NS_WARN_IF(mCurrentLocalDescription->GetMediaSectionCount() <= level) ||
        NS_WARN_IF(mCurrentRemoteDescription->GetMediaSectionCount() <=
                   level)) {
      MOZ_ASSERT(false);
      continue;
    }

    const SdpMediaSection& local =
        mCurrentLocalDescription->GetMediaSection(level);
    const SdpMediaSection& remote =
        mCurrentRemoteDescription->GetMediaSection(level);

    if (transceiver.mJsDirection & sdp::kSend) {
      std::vector<std::string> sdpMsids;
      if (local.GetAttributeList().HasAttribute(SdpAttribute::kMsidAttribute)) {
        for (const auto& msidAttr : local.GetAttributeList().GetMsid().mMsids) {
          if (msidAttr.identifier != "-") {
            sdpMsids.push_back(msidAttr.identifier);
          }
        }
      }
      std::sort(sdpMsids.begin(), sdpMsids.end());

      std::vector<std::string> jsepMsids;
      for (const auto& jsepMsid : transceiver.mSendTrack.GetStreamIds()) {
        jsepMsids.push_back(jsepMsid);
      }
      std::sort(jsepMsids.begin(), jsepMsids.end());

      if (!std::equal(sdpMsids.begin(), sdpMsids.end(), jsepMsids.begin(),
                      jsepMsids.end())) {
        MOZ_MTLOG(ML_DEBUG,
                  "[" << mName
                      << "]: Negotiation needed because transceiver "
                         "is sending, and the local SDP has different "
                         "msids than the send track");
        MOZ_MTLOG(ML_DEBUG, "[" << mName << "]: SDP msids = [");
        for (const auto& msid : sdpMsids) {
          MOZ_MTLOG(ML_DEBUG, msid << ", ");
        }
        MOZ_MTLOG(ML_DEBUG, "]");
        MOZ_MTLOG(ML_DEBUG, "[" << mName << "]: JSEP msids = [");
        for (const auto& msid : jsepMsids) {
          MOZ_MTLOG(ML_DEBUG, msid << ", ");
        }
        MOZ_MTLOG(ML_DEBUG, "]");
        return true;
      }
    }

    if (mIsCurrentOfferer.isSome() && *mIsCurrentOfferer) {
      if ((local.GetDirection() != transceiver.mJsDirection) &&
          reverse(remote.GetDirection()) != transceiver.mJsDirection) {
        MOZ_MTLOG(ML_DEBUG, "[" << mName
                                << "]: Negotiation needed because "
                                   "the direction on our offer, and the remote "
                                   "answer, does not "
                                   "match the direction on a transceiver.");
        return true;
      }
    } else if (local.GetDirection() !=
               (transceiver.mJsDirection & reverse(remote.GetDirection()))) {
      MOZ_MTLOG(
          ML_DEBUG,
          "[" << mName
              << "]: Negotiation needed because "
                 "the direction on our answer doesn't match the direction on a "
                 "transceiver, even though the remote offer would have allowed "
                 "it.");
      return true;
    }
  }

  return false;
}

}  // namespace mozilla
