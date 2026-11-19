<!-- go/cmark -->

<!--* freshness: {owner: 'henrika' reviewed: '2024-09-02'} *-->

# Audio Device Module (ADM)

## Overview

The ADM(AudioDeviceModule) is responsible for driving input (microphone) and
output (speaker) audio in WebRTC and the API is defined in [audio_device.h][19].

Main functions of the ADM are:

- Initialization and termination of native audio libraries.
- Registration of an [AudioTransport object][16] which handles audio callbacks
  for audio in both directions.
- Device enumeration and selection (only for Linux, Windows and Mac OSX).
- Start/Stop physical audio streams:
  - Recording audio from the selected microphone, and
  - playing out audio on the selected speaker.
- Level control of the active audio streams.
- Control of built-in audio effects (Audio Echo Cancelation (AEC), Audio Gain
  Control (AGC) and Noise Suppression (NS)) for Android and iOS.

ADM implementations reside at two different locations in the WebRTC repository:
`/modules/audio_device/` and `/sdk/`. The latest implementations for [iOS][20]
and [Android][21] can be found under `/sdk/`. `/modules/audio_device/` contains
older versions for mobile platforms and also implementations for desktop
platforms such as [Linux][22], [Windows][23] and [Mac OSX][24]. This document is
focusing on the parts in `/modules/audio_device/` but implementation specific
details such as threading models are omitted to keep the descriptions as simple
as possible.

By default, the ADM in WebRTC is created in [`WebRtcVoiceEngine::Init`][1] but
an external implementation can also be injected using
[`webrtc::CreatePeerConnectionFactory`][25]. An example of where an external ADM
is injected can be found in [PeerConnectionInterfaceTest][26] where a so-called
[fake ADM][29] is utilized to avoid hardware dependency in a gtest. Clients can
also inject their own ADMs in situations where functionality is needed that is
not provided by the default implementations.

## Background

This section contains a historical background of the ADM API.

The ADM interface is old and has undergone many changes over the years. It used
to be much more granular but it still contains more than 50 methods and is
implemented on several different hardware platforms.

Some APIs are not implemented on all platforms, and functionality can be spread
out differently between the methods.

The most up-to-date implementations of the ADM interface are for [iOS][27] and
for [Android][28].

Desktop version are not updated to comply with the latest
[C++ style guide](https://chromium.googlesource.com/chromium/src/+/main/styleguide/c++/c++.md)
and more work is also needed to improve the performance and stability of these
versions.

## WebRtcVoiceEngine

[`WebRtcVoiceEngine`][2] does not utilize all methods of the ADM but it still
serves as the best example of its architecture and how to use it. For a more
detailed view of all methods in the ADM interface, see [ADM unit tests][3].

Assuming that an external ADM implementation is not injected, a default - or
internal - ADM is created in [`WebRtcVoiceEngine::Init`][1] using
[`AudioDeviceModule::Create`][4].

Basic initialization is done using a utility method called
[`adm_helpers::Init`][5] which calls fundamental ADM APIs like:

- [`AudiDeviceModule::Init`][6] - initializes the native audio parts required
  for each platform.
- [`AudiDeviceModule::SetPlayoutDevice`][7] - specifies which speaker to use for
  playing out audio using an `index` retrieved by the corresponding enumeration
  method [`AudiDeviceModule::PlayoutDeviceName`][8].
- [`AudiDeviceModule::SetRecordingDevice`][9] - specifies which microphone to
  use for recording audio using an `index` retrieved by the corresponding
  enumeration method which is [`AudiDeviceModule::RecordingDeviceName`][10].
- [`AudiDeviceModule::InitSpeaker`][11] - sets up the parts of the ADM needed to
  use the selected output device.
- [`AudiDeviceModule::InitMicrophone`][12] - sets up the parts of the ADM needed
  to use the selected input device.
- [`AudiDeviceModule::SetStereoPlayout`][13] - enables playout in stereo if the
  selected audio device supports it.
- [`AudiDeviceModule::SetStereoRecording`][14] - enables recording in stereo if
  the selected audio device supports it.

[`WebRtcVoiceEngine::Init`][1] also calls
[`AudiDeviceModule::RegisterAudioTransport`][15] to register an existing
[AudioTransport][16] implementation which handles audio callbacks in both
directions and therefore serves as the bridge between the native ADM and the
upper WebRTC layers.

Recorded audio samples are delivered from the ADM to the `WebRtcVoiceEngine`
(who owns the `AudioTransport` object) via
[`AudioTransport::RecordedDataIsAvailable`][17]:

```
int32_t RecordedDataIsAvailable(const void* audioSamples, size_t nSamples, size_t nBytesPerSample,
                                size_t nChannels, uint32_t samplesPerSec, uint32_t totalDelayMS,
                                int32_t clockDrift, uint32_t currentMicLevel, bool keyPressed,
                                uint32_t& newMicLevel, std::optional<int64_t> estimatedCaptureTimeNS)
```

Decoded audio samples ready to be played out are are delivered by the
`WebRtcVoiceEngine` to the ADM, via [`AudioTransport::NeedMorePlayoutData`][18]:

```
int32_t NeedMorePlayData(size_t nSamples, size_t nBytesPerSample, size_t nChannels, int32_t samplesPerSec,
                         void* audioSamples, size_t& nSamplesOut,
                         int64_t* elapsed_time_ms, int64_t* ntp_time_ms)
```

Audio samples are 16-bit [linear PCM](https://wiki.multimedia.cx/index.php/PCM)
using regular interleaving of channels within each sample.

`WebRtcVoiceEngine` also owns an [`AudioState`][30] member and this class is
used has helper to start and stop audio to and from the ADM. To initialize and
start recording, it calls:

- [`AudiDeviceModule::InitRecording`][31]
- [`AudiDeviceModule::StartRecording`][32]

and to initialize and start playout:

- [`AudiDeviceModule::InitPlayout`][33]
- [`AudiDeviceModule::StartPlayout`][34]

Finally, the corresponding stop methods [`AudiDeviceModule::StopRecording`][35]
and [`AudiDeviceModule::StopPlayout`][36] are called followed by
[`AudiDeviceModule::Terminate`][37].

[1]: https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/media/engine/webrtc_voice_engine.cc;l=314;drc=f7b1b95f11c74cb5369fdd528b73c70a50f2e206
[2]: https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/media/engine/webrtc_voice_engine.h;l=48;drc=d15a575ec3528c252419149d35977e55269d8a41
[3]: https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/modules/audio_device/audio_device_unittest.cc;l=1;drc=d15a575ec3528c252419149d35977e55269d8a41
[4]: https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/modules/audio_device/include/audio_device.h;l=46;drc=eb8c4ca608486add9800f6bfb7a8ba3cf23e738e
[5]: https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/media/engine/adm_helpers.h;drc=2222a80e79ae1ef5cb9510ec51d3868be75f47a2
[6]: https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/modules/audio_device/include/audio_device.h;l=62;drc=9438fb3fff97c803d1ead34c0e4f223db168526f
[7]: https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/modules/audio_device/include/audio_device.h;l=77;drc=9438fb3fff97c803d1ead34c0e4f223db168526f
[8]: https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/modules/audio_device/include/audio_device.h;l=69;drc=9438fb3fff97c803d1ead34c0e4f223db168526f
[9]: https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/modules/audio_device/include/audio_device.h;l=79;drc=9438fb3fff97c803d1ead34c0e4f223db168526f
[10]: https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/modules/audio_device/include/audio_device.h;l=72;drc=9438fb3fff97c803d1ead34c0e4f223db168526f
[11]: https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/modules/audio_device/include/audio_device.h;l=99;drc=9438fb3fff97c803d1ead34c0e4f223db168526f
[12]: https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/modules/audio_device/include/audio_device.h;l=101;drc=9438fb3fff97c803d1ead34c0e4f223db168526f
[13]: https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/modules/audio_device/include/audio_device.h;l=130;drc=9438fb3fff97c803d1ead34c0e4f223db168526f
[14]: https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/modules/audio_device/include/audio_device.h;l=133;drc=9438fb3fff97c803d1ead34c0e4f223db168526f
[15]: https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/modules/audio_device/include/audio_device.h;l=59;drc=9438fb3fff97c803d1ead34c0e4f223db168526f
[16]: https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/modules/audio_device/include/audio_device_defines.h;l=34;drc=9438fb3fff97c803d1ead34c0e4f223db168526f
[17]: https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/modules/audio_device/include/audio_device_defines.h;l=36;drc=9438fb3fff97c803d1ead34c0e4f223db168526f
[18]: https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/modules/audio_device/include/audio_device_defines.h;l=48;drc=9438fb3fff97c803d1ead34c0e4f223db168526f
[19]: https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/modules/audio_device/include/audio_device.h;drc=eb8c4ca608486add9800f6bfb7a8ba3cf23e738es
[20]: https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/sdk/objc/native/api/audio_device_module.h;drc=76443eafa9375374d9f1d23da2b913f2acac6ac2
[21]: https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/sdk/android/src/jni/audio_device/audio_device_module.h;drc=bbeb10925eb106eeed6143ccf571bc438ec22ce1
[22]: https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/modules/audio_device/linux/;drc=d15a575ec3528c252419149d35977e55269d8a41
[23]: https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/modules/audio_device/win/;drc=d15a575ec3528c252419149d35977e55269d8a41
[24]: https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/modules/audio_device/mac/;drc=3b68aa346a5d3483c3448852d19d91723846825c
[25]: https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/api/create_peerconnection_factory.h;l=45;drc=09ceed2165137c4bea4e02e8d3db31970d0bf273
[26]: https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/pc/peer_connection_interface_unittest.cc;l=692;drc=2efb8a5ec61b1b87475d046c03d20244f53b14b6
[27]: https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/sdk/objc/native/api/audio_device_module.h;drc=76443eafa9375374d9f1d23da2b913f2acac6ac2
[28]: https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/sdk/android/src/jni/audio_device/audio_device_module.h;drc=bbeb10925eb106eeed6143ccf571bc438ec22ce1
[29]: https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/pc/test/fake_audio_capture_module.h;l=42;drc=d15a575ec3528c252419149d35977e55269d8a41
[30]: https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/audio/audio_state.h;drc=d15a575ec3528c252419149d35977e55269d8a41
[31]: https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/modules/audio_device/include/audio_device.h;l=87;drc=eb8c4ca608486add9800f6bfb7a8ba3cf23e738e
[32]: https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/modules/audio_device/include/audio_device.h;l=94;drc=eb8c4ca608486add9800f6bfb7a8ba3cf23e738e
[33]: https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/modules/audio_device/include/audio_device.h;l=84;drc=eb8c4ca608486add9800f6bfb7a8ba3cf23e738e
[34]: https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/modules/audio_device/include/audio_device.h;l=91;drc=eb8c4ca608486add9800f6bfb7a8ba3cf23e738e
[35]: https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/modules/audio_device/include/audio_device.h;l=95;drc=eb8c4ca608486add9800f6bfb7a8ba3cf23e738e
[36]: https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/modules/audio_device/include/audio_device.h;l=92;drc=eb8c4ca608486add9800f6bfb7a8ba3cf23e738e
[37]: https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/modules/audio_device/include/audio_device.h;l=63;drc=eb8c4ca608486add9800f6bfb7a8ba3cf23e738e
