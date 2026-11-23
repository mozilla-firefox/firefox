/*
 *  Copyright 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <memory>
#include <optional>

#import "RTCPeerConnectionFactory+Native.h"
#import "RTCPeerConnectionFactory+Private.h"
#import "RTCPeerConnectionFactoryOptions+Private.h"
#import "RTCRtpCapabilities+Private.h"

#import "RTCAudioSource+Private.h"
#import "RTCAudioTrack+Private.h"
#import "RTCMediaConstraints+Private.h"
#import "RTCMediaStream+Private.h"
#import "RTCPeerConnection+Private.h"
#import "RTCVideoSource+Private.h"
#import "RTCVideoTrack+Private.h"
#import "base/RTCLogging.h"
#import "base/RTCVideoDecoderFactory.h"
#import "base/RTCVideoEncoderFactory.h"
#import "helpers/NSString+StdString.h"
#include "rtc_base/checks.h"
#include "sdk/objc/native/api/network_monitor_factory.h"
#include "sdk/objc/native/api/ssl_certificate_verifier.h"

#include "api/audio/audio_device.h"
#include "api/audio/audio_processing.h"
#include "api/audio/builtin_audio_processing_builder.h"
#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/create_modular_peer_connection_factory.h"
#include "api/enable_media.h"
#include "api/environment/environment.h"
#include "api/environment/environment_factory.h"
#include "api/media_stream_interface.h"
#include "api/rtc_event_log/rtc_event_log_factory.h"
#include "api/scoped_refptr.h"
#import "components/video_codec/RTCVideoDecoderFactoryH264.h"
#import "components/video_codec/RTCVideoEncoderFactoryH264.h"
#include "media/base/media_constants.h"

#include "sdk/objc/native/api/objc_audio_device_module.h"
#include "sdk/objc/native/api/video_decoder_factory.h"
#include "sdk/objc/native/api/video_encoder_factory.h"
#include "sdk/objc/native/src/objc_video_decoder_factory.h"
#include "sdk/objc/native/src/objc_video_encoder_factory.h"
#include "sdk/objc/native/src/objc_video_track_source.h"

#if defined(WEBRTC_IOS)
#import "sdk/objc/native/api/audio_device_module.h"
#endif

namespace {

// Serial queue that stops and joins the threads of factories that were
// released on one of their own threads.
dispatch_queue_t ThreadTeardownQueue() {
  static dispatch_queue_t queue = dispatch_queue_create(
      "org.webrtc.RTCPeerConnectionFactory.teardown", DISPATCH_QUEUE_SERIAL);
  return queue;
}

// Tracks the teardowns that are pending on ThreadTeardownQueue().
dispatch_group_t ThreadTeardownGroup() {
  static dispatch_group_t group = dispatch_group_create();
  return group;
}

}  // namespace

@implementation RTC_OBJC_TYPE (RTCPeerConnectionFactory) {
  std::optional<webrtc::Environment> _env;
  std::unique_ptr<webrtc::Thread> _networkThread;
  std::unique_ptr<webrtc::Thread> _signalingThread;
  BOOL _hasStartedAecDump;
}

@synthesize nativeFactory = _nativeFactory;

- (instancetype)init {
  webrtc::PeerConnectionFactoryDependencies dependencies;
  dependencies.audio_encoder_factory =
      webrtc::CreateBuiltinAudioEncoderFactory();
  dependencies.audio_decoder_factory =
      webrtc::CreateBuiltinAudioDecoderFactory();
  dependencies.video_encoder_factory = webrtc::ObjCToNativeVideoEncoderFactory(
      [[RTC_OBJC_TYPE(RTCVideoEncoderFactoryH264) alloc] init]);
  dependencies.video_decoder_factory = webrtc::ObjCToNativeVideoDecoderFactory(
      [[RTC_OBJC_TYPE(RTCVideoDecoderFactoryH264) alloc] init]);
  dependencies.env = webrtc::CreateEnvironment();
#ifdef WEBRTC_IOS
  dependencies.adm = webrtc::CreateAudioDeviceModule(*dependencies.env);
#endif
  return [self initWithMediaAndDependencies:dependencies];
}

- (instancetype)
    initWithEncoderFactory:
        (nullable id<RTC_OBJC_TYPE(RTCVideoEncoderFactory)>)encoderFactory
            decoderFactory:(nullable id<RTC_OBJC_TYPE(RTCVideoDecoderFactory)>)
                               decoderFactory {
  return [self initWithEncoderFactory:encoderFactory
                       decoderFactory:decoderFactory
                          audioDevice:nil];
}

- (instancetype)
    initWithEncoderFactory:
        (nullable id<RTC_OBJC_TYPE(RTCVideoEncoderFactory)>)encoderFactory
            decoderFactory:(nullable id<RTC_OBJC_TYPE(RTCVideoDecoderFactory)>)
                               decoderFactory
               audioDevice:
                   (nullable id<RTC_OBJC_TYPE(RTCAudioDevice)>)audioDevice {
  webrtc::PeerConnectionFactoryDependencies dependencies;
  dependencies.env = webrtc::CreateEnvironment();
  dependencies.audio_encoder_factory =
      webrtc::CreateBuiltinAudioEncoderFactory();
  dependencies.audio_decoder_factory =
      webrtc::CreateBuiltinAudioDecoderFactory();
  if (encoderFactory) {
    dependencies.video_encoder_factory =
        webrtc::ObjCToNativeVideoEncoderFactory(encoderFactory);
  }
  if (decoderFactory) {
    dependencies.video_decoder_factory =
        webrtc::ObjCToNativeVideoDecoderFactory(decoderFactory);
  }
  if (audioDevice) {
    dependencies.adm =
        webrtc::CreateAudioDeviceModule(*dependencies.env, audioDevice);
#ifdef WEBRTC_IOS
  } else {
    dependencies.adm = webrtc::CreateAudioDeviceModule(*dependencies.env);
#endif
  }
  return [self initWithMediaAndDependencies:dependencies];
}

- (instancetype)initWithNativeDependencies:
    (webrtc::PeerConnectionFactoryDependencies &)dependencies {
  self = [super init];
  if (self) {
    _networkThread = webrtc::Thread::CreateWithSocketServer();
    _networkThread->SetName("network_thread", _networkThread.get());
    BOOL result = _networkThread->Start();
    RTC_DCHECK(result) << "Failed to start network thread.";

    _signalingThread = webrtc::Thread::Create();
    _signalingThread->SetName("signaling_thread", _signalingThread.get());
    result = _signalingThread->Start();
    RTC_DCHECK(result) << "Failed to start signaling thread.";

    // Set fields that are relevant both to 'no media' and 'with media'
    // scenarios.
    dependencies.network_thread = _networkThread.get();
    dependencies.signaling_thread = _signalingThread.get();
    if (!dependencies.env.has_value()) {
      dependencies.env = webrtc::CreateEnvironment();
    }
    _env = dependencies.env;
    if (dependencies.network_monitor_factory == nullptr &&
        _env->field_trials().IsEnabled("WebRTC-Network-UseNWPathMonitor")) {
      dependencies.network_monitor_factory =
          webrtc::CreateNetworkMonitorFactory();
    }

    _nativeFactory =
        webrtc::CreateModularPeerConnectionFactory(std::move(dependencies));
    NSAssert(_nativeFactory, @"Failed to initialize PeerConnectionFactory!");
  }
  return self;
}

- (instancetype)initWithNoMedia {
  webrtc::PeerConnectionFactoryDependencies default_deps;
  return [self initWithNativeDependencies:default_deps];
}

- (instancetype)
    initWithNativeAudioEncoderFactory:
        (webrtc::scoped_refptr<webrtc::AudioEncoderFactory>)audioEncoderFactory
            nativeAudioDecoderFactory:
                (webrtc::scoped_refptr<webrtc::AudioDecoderFactory>)
                    audioDecoderFactory
            nativeVideoEncoderFactory:
                (std::unique_ptr<webrtc::VideoEncoderFactory>)
                    videoEncoderFactory
            nativeVideoDecoderFactory:
                (std::unique_ptr<webrtc::VideoDecoderFactory>)
                    videoDecoderFactory
                    audioDeviceModule:
                        (webrtc::AudioDeviceModule *)audioDeviceModule
                audioProcessingModule:
                    (webrtc::scoped_refptr<webrtc::AudioProcessing>)
                        audioProcessingModule {
  webrtc::PeerConnectionFactoryDependencies dependencies;
  dependencies.audio_encoder_factory = std::move(audioEncoderFactory);
  dependencies.audio_decoder_factory = std::move(audioDecoderFactory);
  dependencies.video_encoder_factory = std::move(videoEncoderFactory);
  dependencies.video_decoder_factory = std::move(videoDecoderFactory);
  dependencies.adm = std::move(audioDeviceModule);
  if (audioProcessingModule != nullptr) {
    dependencies.audio_processing_builder =
        CustomAudioProcessing(std::move(audioProcessingModule));
  }
  return [self initWithMediaAndDependencies:dependencies];
}

- (instancetype)
    initWithNativeAudioEncoderFactory:
        (webrtc::scoped_refptr<webrtc::AudioEncoderFactory>)audioEncoderFactory
            nativeAudioDecoderFactory:
                (webrtc::scoped_refptr<webrtc::AudioDecoderFactory>)
                    audioDecoderFactory
            nativeVideoEncoderFactory:
                (std::unique_ptr<webrtc::VideoEncoderFactory>)
                    videoEncoderFactory
            nativeVideoDecoderFactory:
                (std::unique_ptr<webrtc::VideoDecoderFactory>)
                    videoDecoderFactory
                    audioDeviceModule:
                        (webrtc::AudioDeviceModule *)audioDeviceModule
                audioProcessingModule:
                    (webrtc::scoped_refptr<webrtc::AudioProcessing>)
                        audioProcessingModule
             networkControllerFactory:
                 (std::unique_ptr<webrtc::NetworkControllerFactoryInterface>)
                     networkControllerFactory {
  webrtc::PeerConnectionFactoryDependencies dependencies;
  dependencies.adm = std::move(audioDeviceModule);
  dependencies.audio_encoder_factory = std::move(audioEncoderFactory);
  dependencies.audio_decoder_factory = std::move(audioDecoderFactory);
  dependencies.video_encoder_factory = std::move(videoEncoderFactory);
  dependencies.video_decoder_factory = std::move(videoDecoderFactory);
  if (audioProcessingModule != nullptr) {
    dependencies.audio_processing_builder =
        CustomAudioProcessing(std::move(audioProcessingModule));
  }
  dependencies.network_controller_factory = std::move(networkControllerFactory);
  return [self initWithMediaAndDependencies:dependencies];
}

- (instancetype)initWithMediaAndDependencies:
    (webrtc::PeerConnectionFactoryDependencies &)dependencies {
#ifndef WEBRTC_EXCLUDE_AUDIO_PROCESSING_MODULE
  if (dependencies.audio_processing_builder == nullptr) {
    dependencies.audio_processing_builder =
        std::make_unique<webrtc::BuiltinAudioProcessingBuilder>();
  }
#endif
  if (dependencies.event_log_factory == nullptr) {
    dependencies.event_log_factory =
        std::make_unique<webrtc::RtcEventLogFactory>();
  }
  webrtc::EnableMedia(dependencies);
  return [self initWithNativeDependencies:dependencies];
}

- (void)dealloc {
  if (_signalingThread == nullptr || _networkThread == nullptr ||
      (!_signalingThread->IsCurrent() && !_networkThread->IsCurrent())) {
    // .cxx_destruct releases the native factory, which its proxy destroys on
    // the signaling thread, and then stops and joins the threads.
    return;
  }

  // Released on a thread owned by this factory, e.g. from a completion
  // handler. A thread can't stop and join itself. Release the native factory
  // from a separate task on the signaling thread, then stop and join the
  // threads from a serial dispatch queue. Only this class makes the signaling
  // thread quit, so the task isn't dropped. The threads are passed as raw
  // pointers since blocks can't capture move-only types.
  dispatch_group_t group = ThreadTeardownGroup();
  dispatch_group_enter(group);
  webrtc::Thread *signalingThread = _signalingThread.release();
  webrtc::Thread *networkThread = _networkThread.release();
  signalingThread->PostTask([nativeFactory = std::move(_nativeFactory),
                             signalingThread,
                             networkThread,
                             group]() mutable {
    // Destroy the native factory while both threads are still running.
    nativeFactory = nullptr;
    dispatch_async(ThreadTeardownQueue(), ^{
      // Stop the signaling thread first, since its remaining tasks may use
      // the network thread.
      delete signalingThread;
      delete networkThread;
      dispatch_group_leave(group);
    });
  });
}

+ (BOOL)waitForThreadTeardownForTesting:(NSTimeInterval)timeout {
  dispatch_time_t deadline = dispatch_time(
      DISPATCH_TIME_NOW, static_cast<int64_t>(timeout * NSEC_PER_SEC));
  return dispatch_group_wait(ThreadTeardownGroup(), deadline) == 0;
}

- (RTC_OBJC_TYPE(RTCRtpCapabilities) *)rtpSenderCapabilitiesForKind:
    (NSString *)kind {
  webrtc::MediaType mediaType = [[self class] mediaTypeForKind:kind];

  webrtc::RtpCapabilities rtpCapabilities =
      _nativeFactory->GetRtpSenderCapabilities(mediaType);
  return [[RTC_OBJC_TYPE(RTCRtpCapabilities) alloc]
      initWithNativeRtpCapabilities:rtpCapabilities];
}

- (RTC_OBJC_TYPE(RTCRtpCapabilities) *)rtpReceiverCapabilitiesForKind:
    (NSString *)kind {
  webrtc::MediaType mediaType = [[self class] mediaTypeForKind:kind];

  webrtc::RtpCapabilities rtpCapabilities =
      _nativeFactory->GetRtpReceiverCapabilities(mediaType);
  return [[RTC_OBJC_TYPE(RTCRtpCapabilities) alloc]
      initWithNativeRtpCapabilities:rtpCapabilities];
}

- (RTC_OBJC_TYPE(RTCAudioSource) *)audioSourceWithConstraints:
    (nullable RTC_OBJC_TYPE(RTCMediaConstraints) *)constraints {
  std::unique_ptr<webrtc::MediaConstraints> nativeConstraints;
  if (constraints) {
    nativeConstraints = constraints.nativeConstraints;
  }
  webrtc::AudioOptions options;
  CopyConstraintsIntoAudioOptions(nativeConstraints.get(), &options);

  webrtc::scoped_refptr<webrtc::AudioSourceInterface> source =
      _nativeFactory->CreateAudioSource(options);
  return [[RTC_OBJC_TYPE(RTCAudioSource) alloc] initWithFactory:self
                                              nativeAudioSource:source];
}

- (RTC_OBJC_TYPE(RTCAudioTrack) *)audioTrackWithTrackId:(NSString *)trackId {
  RTC_OBJC_TYPE(RTCAudioSource) *audioSource =
      [self audioSourceWithConstraints:nil];
  return [self audioTrackWithSource:audioSource trackId:trackId];
}

- (RTC_OBJC_TYPE(RTCAudioTrack) *)audioTrackWithSource:
                                      (RTC_OBJC_TYPE(RTCAudioSource) *)source
                                               trackId:(NSString *)trackId {
  return [[RTC_OBJC_TYPE(RTCAudioTrack) alloc] initWithFactory:self
                                                        source:source
                                                       trackId:trackId];
}

- (RTC_OBJC_TYPE(RTCVideoSource) *)videoSource {
  return [self videoSourceForScreenCast:NO];
}

- (RTC_OBJC_TYPE(RTCVideoSource) *)videoSourceForScreenCast:
    (BOOL)forScreenCast {
  return [[RTC_OBJC_TYPE(RTCVideoSource) alloc]
        initWithFactory:self
        signalingThread:_signalingThread.get()
           workerThread:_networkThread.get()
      nativeVideoSource:webrtc::make_ref_counted<webrtc::ObjCVideoTrackSource>(
                            *_env, forScreenCast)];
}

- (RTC_OBJC_TYPE(RTCVideoTrack) *)videoTrackWithSource:
                                      (RTC_OBJC_TYPE(RTCVideoSource) *)source
                                               trackId:(NSString *)trackId {
  return [[RTC_OBJC_TYPE(RTCVideoTrack) alloc] initWithFactory:self
                                                        source:source
                                                       trackId:trackId];
}

- (RTC_OBJC_TYPE(RTCMediaStream) *)mediaStreamWithStreamId:
    (NSString *)streamId {
  return [[RTC_OBJC_TYPE(RTCMediaStream) alloc] initWithFactory:self
                                                       streamId:streamId];
}

- (nullable RTC_OBJC_TYPE(RTCPeerConnection) *)
    peerConnectionWithConfiguration:
        (RTC_OBJC_TYPE(RTCConfiguration) *)configuration
                        constraints:
                            (RTC_OBJC_TYPE(RTCMediaConstraints) *)constraints
                           delegate:(nullable id<RTC_OBJC_TYPE(
                                         RTCPeerConnectionDelegate)>)delegate {
  return [[RTC_OBJC_TYPE(RTCPeerConnection) alloc] initWithFactory:self
                                                     configuration:configuration
                                                       constraints:constraints
                                               certificateVerifier:nil
                                                          delegate:delegate];
}

- (nullable RTC_OBJC_TYPE(RTCPeerConnection) *)
    peerConnectionWithConfiguration:
        (RTC_OBJC_TYPE(RTCConfiguration) *)configuration
                        constraints:
                            (RTC_OBJC_TYPE(RTCMediaConstraints) *)constraints
                certificateVerifier:
                    (id<RTC_OBJC_TYPE(RTCSSLCertificateVerifier)>)
                        certificateVerifier
                           delegate:(nullable id<RTC_OBJC_TYPE(
                                         RTCPeerConnectionDelegate)>)delegate {
  return [[RTC_OBJC_TYPE(RTCPeerConnection) alloc]
          initWithFactory:self
            configuration:configuration
              constraints:constraints
      certificateVerifier:certificateVerifier
                 delegate:delegate];
}

- (nullable RTC_OBJC_TYPE(RTCPeerConnection) *)
    peerConnectionWithDependencies:
        (RTC_OBJC_TYPE(RTCConfiguration) *)configuration
                       constraints:
                           (RTC_OBJC_TYPE(RTCMediaConstraints) *)constraints
                      dependencies:
                          (std::unique_ptr<webrtc::PeerConnectionDependencies>)
                              dependencies
                          delegate:
                              (id<RTC_OBJC_TYPE(RTCPeerConnectionDelegate)>)
                                  delegate {
  return [[RTC_OBJC_TYPE(RTCPeerConnection) alloc]
      initWithDependencies:self
             configuration:configuration
               constraints:constraints
              dependencies:std::move(dependencies)
                  delegate:delegate];
}

- (void)setOptions:
    (nonnull RTC_OBJC_TYPE(RTCPeerConnectionFactoryOptions) *)options {
  RTC_DCHECK(options != nil);
  _nativeFactory->SetOptions(options.nativeOptions);
}

- (BOOL)startAecDumpWithFilePath:(NSString *)filePath
                  maxSizeInBytes:(int64_t)maxSizeInBytes {
  RTC_DCHECK(filePath.length);
  RTC_DCHECK_GT(maxSizeInBytes, 0);

  if (_hasStartedAecDump) {
    RTCLogError(@"Aec dump already started.");
    return NO;
  }
  FILE *f = fopen(filePath.UTF8String, "wb");
  if (!f) {
    RTCLogError(
        @"Error opening file: %@. Error: %s", filePath, strerror(errno));
    return NO;
  }
  _hasStartedAecDump = _nativeFactory->StartAecDump(f, maxSizeInBytes);
  return _hasStartedAecDump;
}

- (void)stopAecDump {
  _nativeFactory->StopAecDump();
  _hasStartedAecDump = NO;
}

- (webrtc::Thread *)signalingThread {
  return _signalingThread.get();
}

- (webrtc::Thread *)workerThread {
  // The network thread also acts as the worker thread. See
  // https://groups.google.com/g/discuss-webrtc/c/Fs_Hd5XNJh0
  return _networkThread.get();
}

- (webrtc::Thread *)networkThread {
  return _networkThread.get();
}

#pragma mark - Private

+ (webrtc::MediaType)mediaTypeForKind:(NSString *)kind {
  if (kind == kRTCMediaStreamTrackKindAudio) {
    return webrtc::MediaType::AUDIO;
  } else if (kind == kRTCMediaStreamTrackKindVideo) {
    return webrtc::MediaType::VIDEO;
  } else {
    RTC_DCHECK_NOTREACHED();
    return webrtc::MediaType::UNSUPPORTED;
  }
}

@end
