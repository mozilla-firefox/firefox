/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is
 * https://w3c.github.io/webrtc-pc/#rtcconfiguration-dictionary
 */

enum RTCIceCredentialType {
    "password",
};

dictionary RTCIceServer {
    (DOMString or sequence<DOMString>) urls;
    DOMString  url; //deprecated
    DOMString username;
    DOMString credential;
    RTCIceCredentialType credentialType = "password";
};

enum RTCIceTransportPolicy {
    "relay",
    "all"
};

enum RTCBundlePolicy {
    "balanced",
    "max-compat",
    "max-bundle"
};

enum RTCRtcpMuxPolicy {
    "require",
    "negotiate"
};

dictionary RTCConfiguration {
    sequence<RTCIceServer> iceServers = [];
    RTCIceTransportPolicy  iceTransportPolicy = "all";
    RTCBundlePolicy bundlePolicy = "balanced";
    RTCRtcpMuxPolicy rtcpMuxPolicy = "require";
    DOMString? peerIdentity = null;
    sequence<RTCCertificate> certificates = [];

    // Non-standard. Only here to be able to detect and warn in web console.
    // Uses DOMString over enum as a trade-off between type errors and safety.
    // TODO: Remove once sdpSemantics usage drops to zero (bug 1632243).
    DOMString sdpSemantics;
};

// Extensions defined in
// https://w3c.github.io/webrtc-extensions/
partial dictionary RTCConfiguration {
    // https://w3c.github.io/webrtc-extensions/#always-negotiating-datachannels-configuration
    boolean alwaysNegotiateDataChannels = false;
};
