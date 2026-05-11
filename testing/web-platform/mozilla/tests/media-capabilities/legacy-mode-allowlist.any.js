// META: global=window,dedicatedworker

// With media.mediacapabilities.legacy.enabled:false but the test host in
// media.mediacapabilities.legacy.allowlist, the legacy-mode validation bypass
// must apply (same behavior as when legacy.enabled:true).

promise_test(() => {
  return navigator.mediaCapabilities.encodingInfo({
    type: 'record',
    video: {
      contentType: 'video/webm; codecs="vp09.00.10.08"',
      width: 800,
      height: 600,
      bitrate: 3000,
      framerate: 24,
      scalabilityMode: 'L1T1',
    },
  });
}, "encodingInfo resolves with scalabilityMode for non-webrtc type when host is in legacy.allowlist");

promise_test(() => {
  return navigator.mediaCapabilities.encodingInfo({
    type: 'record',
    video: {
      contentType: 'video/webm; codecs="vp09.00.10.08"',
      width: 800,
      height: 600,
      bitrate: 3000,
      framerate: 24,
      colorGamut: 'srgb',
    },
  });
}, "encodingInfo resolves with colorGamut for encoding type when host is in legacy.allowlist");

promise_test(() => {
  return navigator.mediaCapabilities.encodingInfo({
    type: 'record',
    video: {
      contentType: 'video/webm; codecs="vp09.00.10.08"',
      width: 800,
      height: 600,
      bitrate: 3000,
      framerate: 24,
      transferFunction: 'srgb',
    },
  });
}, "encodingInfo resolves with transferFunction for encoding type when host is in legacy.allowlist");
