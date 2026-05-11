// META: global=window,dedicatedworker

// WebRTC single-codec MIME types (e.g. video/h264, audio/opus) carry fmtp
// attributes as MIME parameters. These must not be rejected.

promise_test(() => {
  return navigator.mediaCapabilities.decodingInfo({
    type: 'webrtc',
    video: {
      contentType: 'video/h264; profile-level-id="42e01f"',
      width: 640,
      height: 480,
      bitrate: 1000000,
      framerate: 30,
    },
  });
}, "decodingInfo resolves for WebRTC video with MIME parameters");

promise_test(() => {
  return navigator.mediaCapabilities.decodingInfo({
    type: 'webrtc',
    audio: {
      contentType: 'audio/opus; useinbandfec=1',
    },
  });
}, "decodingInfo resolves for WebRTC audio with MIME parameters");

promise_test(() => {
  return navigator.mediaCapabilities.encodingInfo({
    type: 'webrtc',
    video: {
      contentType: 'video/h264; profile-level-id="42e01f"',
      width: 640,
      height: 480,
      bitrate: 1000000,
      framerate: 30,
    },
  });
}, "encodingInfo resolves for WebRTC video with MIME parameters");

promise_test(() => {
  return navigator.mediaCapabilities.encodingInfo({
    type: 'webrtc',
    audio: {
      contentType: 'audio/opus; useinbandfec=1',
    },
  });
}, "encodingInfo resolves for WebRTC audio with MIME parameters");
