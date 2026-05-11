// META: global=window,dedicatedworker

// With media.mediacapabilities.legacy.enabled:true, scalabilityMode on a
// non-webrtc encoding type and colorGamut/transferFunction on encoding configs
// must not reject (these validations are bypassed in legacy mode).

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
}, "encodingInfo resolves with scalabilityMode for non-webrtc type in legacy mode");

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
}, "encodingInfo resolves with colorGamut for encoding type in legacy mode");

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
}, "encodingInfo resolves with transferFunction for encoding type in legacy mode");
