/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is
 * https://w3c.github.io/webrtc-pc/#rtcpeerconnectioniceevent
 */

dictionary RTCPeerConnectionIceEventInit : EventInit {
  RTCIceCandidate? candidate = null;
};

[Pref="media.peerconnection.enabled",
 Exposed=Window]
interface RTCPeerConnectionIceEvent : Event {
  constructor(DOMString type,
              optional RTCPeerConnectionIceEventInit eventInitDict = {});

  readonly attribute RTCIceCandidate? candidate;
};
