// META: global=window,dedicatedworker

// With media.mediacapabilities.webrtc.enabled:false but the test host in
// media.mediacapabilities.webrtc.enabled.allowlist, webrtc-typed calls must
// resolve rather than rejecting with TypeError.

promise_test(() => {
  return navigator.mediaCapabilities.decodingInfo({
    type: 'webrtc',
    video: {
      contentType: 'video/VP9',
      width: 640,
      height: 480,
      bitrate: 1000000,
      framerate: 30,
    },
  });
}, "decodingInfo resolves for webrtc type when host is in webrtc.enabled.allowlist");

promise_test(() => {
  return navigator.mediaCapabilities.encodingInfo({
    type: 'webrtc',
    video: {
      contentType: 'video/VP9',
      width: 640,
      height: 480,
      bitrate: 1000000,
      framerate: 30,
    },
  });
}, "encodingInfo resolves for webrtc type when host is in webrtc.enabled.allowlist");
