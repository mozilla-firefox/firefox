/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_MEDIA_WEBRTC_COMMON_ICESERVERPARSER_H_
#define DOM_MEDIA_WEBRTC_COMMON_ICESERVERPARSER_H_

#include "mozilla/ErrorResult.h"
#include "mozilla/Result.h"
#include "mozilla/dom/RTCConfigurationBinding.h"
#include "nsString.h"
#include "nsTArray.h"

namespace mozilla {

class IceServerParser {
 public:
  // Known acceptable ports for webrtc. These are allowed for outgoing
  // connections even when they appear on Necko's generic port block list.
  static constexpr uint16_t kGoodWebrtcPortList[] = {
      53,    // Some deployments use DNS port to punch through overzealous NATs
      3478,  // stun or turn
      5349,  // stuns or turns
  };

  enum class StunTurnScheme : uint8_t {
    Stun,
    Stuns,
    Turn,
    Turns,
  };

  enum class IceTransport : uint8_t {
    Udp,
    Tcp,
  };

  struct StunTurnUri {
    StunTurnScheme mScheme;
    nsCString mHost;
    uint16_t mPort;
    IceTransport mTransport;

    bool IsTurn() const {
      return mScheme == StunTurnScheme::Turn ||
             mScheme == StunTurnScheme::Turns;
    }

    bool IsTls() const {
      return mScheme == StunTurnScheme::Stuns ||
             mScheme == StunTurnScheme::Turns;
    }

    bool operator==(const StunTurnUri& aOther) const = default;
  };

  struct ParsedIceServer {
    StunTurnUri mUri;
    nsCString mUsername;
    nsCString mPassword;

    bool operator==(const ParsedIceServer& aOther) const = default;
  };

  // Parse a STUN/TURN URI per webrtc-pc, RFCs 7064/7065
  // Uses NS_NewURI for initial parsing and scheme normalization, then applies
  // STUN/TURN-specific validation.
  // Possible exceptions currently include SyntaxError and NotSupportedError.
  static Result<StunTurnUri, ErrorResult> ParseStunTurnUri(
      const nsACString& aUri);

  // Parse and validate an array of RTCIceServer into flattened ParsedIceServer
  // structs. Checks URI syntax, credential presence/length for TURN, and port
  // safety.
  // Possible exceptions currently include SyntaxError, NotSupportedError, and
  // InvalidAccessError.
  static Result<nsTArray<ParsedIceServer>, ErrorResult> Parse(
      const nsTArray<dom::RTCIceServer>& aIceServers);
};

// Returns true if aPort may be used as an outgoing webrtc destination: either
// it is one of IceServerParser::kGoodWebrtcPortList or it is not on Necko's
// generic outgoing port block list.
bool IsWebrtcPortAllowed(uint16_t aPort);

}  // namespace mozilla

#endif  // DOM_MEDIA_WEBRTC_COMMON_ICESERVERPARSER_H_
