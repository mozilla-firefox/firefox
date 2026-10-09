/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is
 * https://w3c.github.io/webrtc-encoded-transform
 */

dictionary RTCEncodedAudioFrameMetadata : RTCEncodedFrameMetadata{
    short sequenceNumber;
    double audioLevel;
};

dictionary RTCEncodedAudioFrameOptions {
    RTCEncodedAudioFrameMetadata metadata;
};

// [Serializable] is implemented without adding attribute here,
// because we don't implement "full serialization" to disk.
[Pref="media.peerconnection.enabled",
 Pref="media.peerconnection.scripttransform.enabled",
 Exposed=(Window,DedicatedWorker)]
interface RTCEncodedAudioFrame {
    [Throws]
    constructor(RTCEncodedAudioFrame originalFrame, optional RTCEncodedAudioFrameOptions options = {});
    readonly attribute unsigned long timestamp;    // legacy name of metadata rtpTimestamp
    [SetterThrows]
    attribute ArrayBuffer data;
    RTCEncodedAudioFrameMetadata getMetadata();
};
