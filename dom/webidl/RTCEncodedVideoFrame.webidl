/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is
 * https://w3c.github.io/webrtc-encoded-transform
 */

// New enum for video frame types. Will eventually re-use the equivalent defined
// by WebCodecs.
enum RTCEncodedVideoFrameType {
    "empty",
    "key",
    "delta",
};

dictionary RTCEncodedVideoFrameMetadata : RTCEncodedFrameMetadata {
    unsigned long long frameId;
    sequence<unsigned long long> dependencies;
    unsigned short width;
    unsigned short height;
    unsigned long spatialIndex;
    unsigned long temporalIndex;
    long long timestamp;    // microseconds
};

dictionary RTCEncodedVideoFrameOptions {
    RTCEncodedVideoFrameMetadata metadata;
};

// New interfaces to define encoded video and audio frames. Will eventually
// re-use or extend the equivalent defined in WebCodecs.
//
// [Serializable] is implemented without adding attribute here,
// because we don't implement "full serialization" to disk.
[Pref="media.peerconnection.enabled",
 Pref="media.peerconnection.scripttransform.enabled",
 Exposed=(Window,DedicatedWorker)]
interface RTCEncodedVideoFrame {
    [Throws]
    constructor(RTCEncodedVideoFrame originalFrame, optional RTCEncodedVideoFrameOptions options = {});
    readonly attribute RTCEncodedVideoFrameType type;
    readonly attribute unsigned long timestamp;    // legacy name of metadata rtpTimestamp
    [SetterThrows]
    attribute ArrayBuffer data;
    RTCEncodedVideoFrameMetadata getMetadata();
};
