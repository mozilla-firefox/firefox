/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "sdp/SipccSdp.h"

#include <charconv>

#include "mozilla/Assertions.h"
#include "mozilla/UniquePtr.h"
#include "sdp/SdpParser.h"

#ifdef CRLF
#  undef CRLF
#endif
#define CRLF "\r\n"

namespace mozilla {

SipccSdp::SipccSdp(const SipccSdp& aOrig)
    : mOrigin(aOrig.mOrigin),
      mBandwidths(aOrig.mBandwidths),
      mAttributeList(aOrig.mAttributeList, nullptr) {
  for (const auto& msection : aOrig.mMediaSections) {
    mMediaSections.emplace_back(
        new SipccSdpMediaSection(*msection, &mAttributeList));
  }
}

Sdp* SipccSdp::Clone() const { return new SipccSdp(*this); }

const SdpOrigin& SipccSdp::GetOrigin() const { return mOrigin; }

uint32_t SipccSdp::GetBandwidth(const std::string& type) const {
  auto found = mBandwidths.find(type);
  if (found == mBandwidths.end()) {
    return 0;
  }
  return found->second;
}

const SdpMediaSection& SipccSdp::GetMediaSection(size_t level) const {
  if (level > mMediaSections.size()) {
    MOZ_CRASH();
  }
  return *mMediaSections[level];
}

SdpMediaSection& SipccSdp::GetMediaSection(size_t level) {
  if (level > mMediaSections.size()) {
    MOZ_CRASH();
  }
  return *mMediaSections[level];
}

SdpMediaSection& SipccSdp::AddMediaSection(SdpMediaSection::MediaType mediaType,
                                           SdpDirectionAttribute::Direction dir,
                                           uint16_t port,
                                           SdpMediaSection::Protocol protocol,
                                           sdp::AddrType addrType,
                                           const std::string& addr) {
  size_t level = mMediaSections.size();
  SipccSdpMediaSection* media =
      new SipccSdpMediaSection(level, &mAttributeList);
  media->mMediaType = mediaType;
  media->mPort = port;
  media->mPortCount = 0;
  media->mProtocol = protocol;
  media->mConnection = MakeUnique<SdpConnection>(addrType, addr);
  media->GetAttributeList().SetAttribute(new SdpDirectionAttribute(dir));
  mMediaSections.emplace_back(media);
  return *media;
}

bool SipccSdp::LoadOrigin(sdp_t* sdp, InternalResults& results) {
  std::string username = sdp_get_owner_username(sdp);

  // Parse session fields using std::from_chars and strlen
  uint64_t sessId = 0;
  const char* sessionIdStr = sdp_get_owner_sessionid(sdp);
  std::from_chars(sessionIdStr, sessionIdStr + strlen(sessionIdStr), sessId,
                  10);

  uint64_t sessVer = 0;
  const char* sessionVersionStr = sdp_get_owner_version(sdp);
  std::from_chars(sessionVersionStr,
                  sessionVersionStr + strlen(sessionVersionStr), sessVer, 10);

  sdp_nettype_e type = sdp_get_owner_network_type(sdp);
  if (type != SDP_NT_INTERNET) {
    results.AddParseError(2, "Unsupported network type");
    return false;
  }

  sdp::AddrType addrType;
  switch (sdp_get_owner_address_type(sdp)) {
    case SDP_AT_IP4:
      addrType = sdp::kIPv4;
      break;
    case SDP_AT_IP6:
      addrType = sdp::kIPv6;
      break;
    default:
      results.AddParseError(2, "Unsupported address type");
      return false;
  }

  std::string address = sdp_get_owner_address(sdp);
  mOrigin = SdpOrigin(username, sessId, sessVer, addrType, address);
  return true;
}

bool SipccSdp::Load(sdp_t* sdp, InternalResults& results) {
  // Believe it or not, SDP_SESSION_LEVEL is 0xFFFF
  if (!mAttributeList.Load(sdp, SDP_SESSION_LEVEL, results)) {
    return false;
  }

  if (!LoadOrigin(sdp, results)) {
    return false;
  }

  if (!mBandwidths.Load(sdp, SDP_SESSION_LEVEL, results)) {
    return false;
  }

  for (int i = 0; i < sdp_get_num_media_lines(sdp); ++i) {
    // note that we pass a "level" here that is one higher
    // sipcc counts media sections from 1, using 0xFFFF as the "session"
    UniquePtr<SipccSdpMediaSection> section(
        new SipccSdpMediaSection(i, &mAttributeList));
    if (!section->Load(sdp, i + 1, results)) {
      return false;
    }
    mMediaSections.push_back(std::move(section));
  }
  return true;
}

void SipccSdp::Serialize(std::ostream& os) const {
  os << "v=0" << CRLF << mOrigin << "s=-" << CRLF;

  // We don't support creating i=, u=, e=, p=
  // We don't generate c= at the session level (only in media)

  mBandwidths.Serialize(os);
  os << "t=0 0" << CRLF;

  // We don't support r= or z=

  // attributes
  os << mAttributeList;

  // media sections
  for (const auto& msection : mMediaSections) {
    os << *msection;
  }
}

bool SipccSdpBandwidths::Load(sdp_t* sdp, uint16_t level,
                              InternalResults& results) {
  size_t count = sdp_get_num_bw_lines(sdp, level);
  for (size_t i = 1; i <= count; ++i) {
    sdp_bw_modifier_e bwtype = sdp_get_bw_modifier(sdp, level, i);
    uint32_t bandwidth = sdp_get_bw_value(sdp, level, i);
    if (bwtype != SDP_BW_MODIFIER_UNSUPPORTED) {
      const char* typeName = sdp_get_bw_modifier_name(bwtype);
      (*this)[typeName] = bandwidth;
    }
  }

  return true;
}

void SipccSdpBandwidths::Serialize(std::ostream& os) const {
  for (auto i = begin(); i != end(); ++i) {
    os << "b=" << i->first << ":" << i->second << CRLF;
  }
}

}  // namespace mozilla
