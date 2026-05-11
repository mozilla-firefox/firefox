// META: global=window,dedicatedworker

// With media.mediacapabilities.webrtc.enabled:false and the test host not in
// media.mediacapabilities.webrtc.enabled.allowlist, webrtc-typed calls must
// reject with TypeError.

promise_test(t => {
  return promise_rejects_js(t, TypeError, navigator.mediaCapabilities.decodingInfo({
    type: 'webrtc',
    video: {
      contentType: 'video/VP9',
      width: 640,
      height: 480,
      bitrate: 1000000,
      framerate: 30,
    },
  }));
}, "decodingInfo rejects with TypeError for webrtc type when host is not in webrtc.enabled.allowlist");

promise_test(t => {
  return promise_rejects_js(t, TypeError, navigator.mediaCapabilities.encodingInfo({
    type: 'webrtc',
    video: {
      contentType: 'video/VP9',
      width: 640,
      height: 480,
      bitrate: 1000000,
      framerate: 30,
    },
  }));
}, "encodingInfo rejects with TypeError for webrtc type when host is not in webrtc.enabled.allowlist");
