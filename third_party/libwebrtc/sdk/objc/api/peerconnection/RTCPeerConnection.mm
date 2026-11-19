/*
 *  Copyright 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#import "RTCPeerConnection+Private.h"

#import "RTCConfiguration+Private.h"
#import "RTCDataChannel+Private.h"
#import "RTCIceCandidate+Private.h"
#import "RTCIceCandidateErrorEvent+Private.h"
#import "RTCLegacyStatsReport+Private.h"
#import "RTCMediaConstraints+Private.h"
#import "RTCMediaStream+Private.h"
#import "RTCMediaStreamTrack+Private.h"
#import "RTCPeerConnectionFactory+Private.h"
#import "RTCRtpReceiver+Private.h"
#import "RTCRtpSender+Private.h"
#import "RTCRtpTransceiver+Private.h"
#import "RTCSessionDescription+Private.h"
#import "base/RTCLogging.h"
#import "helpers/NSString+StdString.h"

#include <memory>

#include "api/jsep.h"
#include "api/rtc_event_log_output_file.h"
#include "api/set_local_description_observer_interface.h"
#include "api/set_remote_description_observer_interface.h"
#include "rtc_base/checks.h"
#include "rtc_base/numerics/safe_conversions.h"
#include "rtc_base/system/plan_b_only.h"
#include "sdk/objc/native/api/ssl_certificate_verifier.h"

NSString *const kRTCPeerConnectionErrorDomain =
    @"org.webrtc.RTC_OBJC_TYPE(RTCPeerConnection)";
int const kRTCPeerConnnectionSessionDescriptionError = -1;

namespace {

class SetSessionDescriptionObserver
    : public webrtc::SetLocalDescriptionObserverInterface,
      public webrtc::SetRemoteDescriptionObserverInterface {
 public:
  SetSessionDescriptionObserver(
      RTCSetSessionDescriptionCompletionHandler completionHandler) {
    completion_handler_ = completionHandler;
  }

  virtual void OnSetLocalDescriptionComplete(webrtc::RTCError error) override {
    OnCompelete(error);
  }

  virtual void OnSetRemoteDescriptionComplete(webrtc::RTCError error) override {
    OnCompelete(error);
  }

 private:
  void OnCompelete(webrtc::RTCError error) {
    RTC_DCHECK(completion_handler_ != nil);
    if (error.ok()) {
      completion_handler_(nil);
    } else {
      // TODO(hta): Add handling of error.type()
      NSString *str = [NSString stringForStdString:error.message()];
      NSError *err =
          [NSError errorWithDomain:kRTCPeerConnectionErrorDomain
                              code:kRTCPeerConnnectionSessionDescriptionError
                          userInfo:@{NSLocalizedDescriptionKey : str}];
      completion_handler_(err);
    }
    completion_handler_ = nil;
  }
  RTCSetSessionDescriptionCompletionHandler completion_handler_;
};

}  // anonymous namespace

namespace webrtc {

class CreateSessionDescriptionObserverAdapter
    : public CreateSessionDescriptionObserver {
 public:
  CreateSessionDescriptionObserverAdapter(void (^completionHandler)(
      RTC_OBJC_TYPE(RTCSessionDescription) * sessionDescription,
      NSError *error)) {
    completion_handler_ = completionHandler;
  }

  ~CreateSessionDescriptionObserverAdapter() override {
    completion_handler_ = nil;
  }

  void OnSuccess(SessionDescriptionInterface *desc) override {
    RTC_DCHECK(completion_handler_);
    std::unique_ptr<webrtc::SessionDescriptionInterface> description =
        std::unique_ptr<webrtc::SessionDescriptionInterface>(desc);
    RTC_OBJC_TYPE(RTCSessionDescription) *session =
        [[RTC_OBJC_TYPE(RTCSessionDescription) alloc]
            initWithNativeDescription:description.get()];
    completion_handler_(session, nil);
    completion_handler_ = nil;
  }

  void OnFailure(RTCError error) override {
    RTC_DCHECK(completion_handler_);
    // TODO(hta): Add handling of error.type()
    NSString *str = [NSString stringForStdString:error.message()];
    NSError *err =
        [NSError errorWithDomain:kRTCPeerConnectionErrorDomain
                            code:kRTCPeerConnnectionSessionDescriptionError
                        userInfo:@{NSLocalizedDescriptionKey : str}];
    completion_handler_(nil, err);
    completion_handler_ = nil;
  }

 private:
  void (^completion_handler_)(RTC_OBJC_TYPE(RTCSessionDescription) *
                                  sessionDescription,
                              NSError *error);
};

PeerConnectionDelegateAdapter::PeerConnectionDelegateAdapter(
    RTC_OBJC_TYPE(RTCPeerConnection) * peerConnection) {
  peer_connection_ = peerConnection;
}

PeerConnectionDelegateAdapter::~PeerConnectionDelegateAdapter() {
  peer_connection_ = nil;
}

void PeerConnectionDelegateAdapter::OnSignalingChange(
    PeerConnectionInterface::SignalingState new_state) {
  RTCSignalingState state = [[RTC_OBJC_TYPE(RTCPeerConnection) class]
      signalingStateForNativeState:new_state];
  RTC_OBJC_TYPE(RTCPeerConnection) *peer_connection = peer_connection_;
  if (peer_connection == nil) {
    return;
  }
  id<RTC_OBJC_TYPE(RTCPeerConnectionDelegate)> delegate =
      peer_connection.delegate;
  if (delegate == nil) {
    return;
  }
  [delegate peerConnection:peer_connection didChangeSignalingState:state];
}

void PeerConnectionDelegateAdapter::OnAddStream(
    webrtc::scoped_refptr<MediaStreamInterface> stream) {
  RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN();
  RTC_OBJC_TYPE(RTCPeerConnection) *peer_connection = peer_connection_;
  if (peer_connection == nil) {
    return;
  }
  id<RTC_OBJC_TYPE(RTCPeerConnectionDelegate)> delegate =
      peer_connection.delegate;
  if (delegate == nil) {
    return;
  }

  RTC_OBJC_TYPE(RTCMediaStream) *media_stream = [[RTC_OBJC_TYPE(RTCMediaStream)
      alloc] initWithFactory:peer_connection.factory nativeMediaStream:stream];

  [delegate peerConnection:peer_connection didAddStream:media_stream];
  RTC_ALLOW_PLAN_B_DEPRECATION_END();
}

void PeerConnectionDelegateAdapter::OnRemoveStream(
    webrtc::scoped_refptr<MediaStreamInterface> stream) {
  RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN();
  RTC_OBJC_TYPE(RTCPeerConnection) *peer_connection = peer_connection_;
  if (peer_connection == nil) {
    return;
  }
  id<RTC_OBJC_TYPE(RTCPeerConnectionDelegate)> delegate =
      peer_connection.delegate;
  if (delegate == nil) {
    return;
  }
  RTC_OBJC_TYPE(RTCMediaStream) *mediaStream = [[RTC_OBJC_TYPE(RTCMediaStream)
      alloc] initWithFactory:peer_connection.factory nativeMediaStream:stream];

  [delegate peerConnection:peer_connection didRemoveStream:mediaStream];
  RTC_ALLOW_PLAN_B_DEPRECATION_END();
}

void PeerConnectionDelegateAdapter::OnTrack(
    webrtc::scoped_refptr<RtpTransceiverInterface> nativeTransceiver) {
  RTC_OBJC_TYPE(RTCPeerConnection) *peer_connection = peer_connection_;
  if (peer_connection == nil) {
    return;
  }
  id<RTC_OBJC_TYPE(RTCPeerConnectionDelegate)> delegate =
      peer_connection.delegate;
  if (delegate == nil) {
    return;
  }

  RTC_OBJC_TYPE(RTCRtpTransceiver) *transceiver =
      [[RTC_OBJC_TYPE(RTCRtpTransceiver) alloc]
               initWithFactory:peer_connection.factory
          nativeRtpTransceiver:nativeTransceiver];
  if ([delegate respondsToSelector:@selector(peerConnection:
                                       didStartReceivingOnTransceiver:)]) {
    [delegate peerConnection:peer_connection
        didStartReceivingOnTransceiver:transceiver];
  }
}

void PeerConnectionDelegateAdapter::OnDataChannel(
    webrtc::scoped_refptr<DataChannelInterface> data_channel) {
  RTC_OBJC_TYPE(RTCPeerConnection) *peer_connection = peer_connection_;
  if (peer_connection == nil) {
    return;
  }
  id<RTC_OBJC_TYPE(RTCPeerConnectionDelegate)> delegate =
      peer_connection.delegate;
  if (delegate == nil) {
    return;
  }
  RTC_OBJC_TYPE(RTCDataChannel) *dataChannel = [[RTC_OBJC_TYPE(RTCDataChannel)
      alloc] initWithFactory:peer_connection.factory
           nativeDataChannel:data_channel];
  [delegate peerConnection:peer_connection didOpenDataChannel:dataChannel];
}

void PeerConnectionDelegateAdapter::OnRenegotiationNeeded() {
  RTC_OBJC_TYPE(RTCPeerConnection) *peer_connection = peer_connection_;
  if (peer_connection == nil) {
    return;
  }
  id<RTC_OBJC_TYPE(RTCPeerConnectionDelegate)> delegate =
      peer_connection.delegate;
  if (delegate == nil) {
    return;
  }
  [delegate peerConnectionShouldNegotiate:peer_connection];
}

void PeerConnectionDelegateAdapter::OnIceConnectionChange(
    PeerConnectionInterface::IceConnectionState new_state) {
  RTC_OBJC_TYPE(RTCPeerConnection) *peer_connection = peer_connection_;
  if (peer_connection == nil) {
    return;
  }
  id<RTC_OBJC_TYPE(RTCPeerConnectionDelegate)> delegate =
      peer_connection.delegate;
  if (delegate == nil) {
    return;
  }
  RTCIceConnectionState state = [RTC_OBJC_TYPE(RTCPeerConnection)
      iceConnectionStateForNativeState:new_state];
  [delegate peerConnection:peer_connection didChangeIceConnectionState:state];
}

void PeerConnectionDelegateAdapter::OnStandardizedIceConnectionChange(
    PeerConnectionInterface::IceConnectionState new_state) {
  RTC_OBJC_TYPE(RTCPeerConnection) *peer_connection = peer_connection_;
  if (peer_connection == nil) {
    return;
  }
  id<RTC_OBJC_TYPE(RTCPeerConnectionDelegate)> delegate =
      peer_connection.delegate;
  if (delegate == nil) {
    return;
  }
  if ([delegate respondsToSelector:@selector
                (peerConnection:didChangeStandardizedIceConnectionState:)]) {
    RTCIceConnectionState state = [RTC_OBJC_TYPE(RTCPeerConnection)
        iceConnectionStateForNativeState:new_state];
    [delegate peerConnection:peer_connection
        didChangeStandardizedIceConnectionState:state];
  }
}

void PeerConnectionDelegateAdapter::OnConnectionChange(
    PeerConnectionInterface::PeerConnectionState new_state) {
  RTC_OBJC_TYPE(RTCPeerConnection) *peer_connection = peer_connection_;
  if (peer_connection == nil) {
    return;
  }
  id<RTC_OBJC_TYPE(RTCPeerConnectionDelegate)> delegate =
      peer_connection.delegate;
  if (delegate == nil) {
    return;
  }
  if ([delegate respondsToSelector:@selector(peerConnection:
                                       didChangeConnectionState:)]) {
    RTCPeerConnectionState state = [RTC_OBJC_TYPE(RTCPeerConnection)
        connectionStateForNativeState:new_state];
    [delegate peerConnection:peer_connection didChangeConnectionState:state];
  }
}

void PeerConnectionDelegateAdapter::OnIceGatheringChange(
    PeerConnectionInterface::IceGatheringState new_state) {
  RTC_OBJC_TYPE(RTCPeerConnection) *peer_connection = peer_connection_;
  if (peer_connection == nil) {
    return;
  }
  id<RTC_OBJC_TYPE(RTCPeerConnectionDelegate)> delegate =
      peer_connection.delegate;
  if (delegate == nil) {
    return;
  }
  RTCIceGatheringState state = [[RTC_OBJC_TYPE(RTCPeerConnection) class]
      iceGatheringStateForNativeState:new_state];
  [delegate peerConnection:peer_connection didChangeIceGatheringState:state];
}

void PeerConnectionDelegateAdapter::OnIceCandidate(
    const IceCandidate *candidate) {
  RTC_OBJC_TYPE(RTCPeerConnection) *peer_connection = peer_connection_;
  if (peer_connection == nil) {
    return;
  }
  id<RTC_OBJC_TYPE(RTCPeerConnectionDelegate)> delegate =
      peer_connection.delegate;
  if (delegate == nil) {
    return;
  }
  RTC_OBJC_TYPE(RTCIceCandidate) *iceCandidate =
      [[RTC_OBJC_TYPE(RTCIceCandidate) alloc]
          initWithNativeCandidate:candidate];
  [delegate peerConnection:peer_connection
      didGenerateIceCandidate:iceCandidate];
}

void PeerConnectionDelegateAdapter::OnIceCandidateError(
    const std::string &address,
    int port,
    const std::string &url,
    int error_code,
    const std::string &error_text) {
  RTC_OBJC_TYPE(RTCPeerConnection) *peer_connection = peer_connection_;
  if (peer_connection == nil) {
    return;
  }
  id<RTC_OBJC_TYPE(RTCPeerConnectionDelegate)> delegate =
      peer_connection.delegate;
  if (delegate == nil) {
    return;
  }
  RTC_OBJC_TYPE(RTCIceCandidateErrorEvent) *event =
      [[RTC_OBJC_TYPE(RTCIceCandidateErrorEvent) alloc]
          initWithAddress:address
                     port:port
                      url:url
                errorCode:error_code
                errorText:error_text];
  if ([delegate respondsToSelector:@selector(peerConnection:
                                       didFailToGatherIceCandidate:)]) {
    [delegate peerConnection:peer_connection didFailToGatherIceCandidate:event];
  }
}

void PeerConnectionDelegateAdapter::OnIceCandidateRemoved(
    const webrtc::IceCandidate *c) {
  RTC_OBJC_TYPE(RTCPeerConnection) *peer_connection = peer_connection_;
  if (peer_connection == nil) {
    return;
  }
  id<RTC_OBJC_TYPE(RTCPeerConnectionDelegate)> delegate =
      peer_connection.delegate;
  if (delegate == nil) {
    return;
  }
  NSMutableArray *ice_candidates = [NSMutableArray arrayWithCapacity:1];
  RTC_OBJC_TYPE(RTCIceCandidate) *ice_candidate =
      [[RTC_OBJC_TYPE(RTCIceCandidate) alloc] initWithNativeCandidate:c];
  [ice_candidates addObject:ice_candidate];
  [delegate peerConnection:peer_connection
      didRemoveIceCandidates:ice_candidates];
}

void PeerConnectionDelegateAdapter::OnIceSelectedCandidatePairChanged(
    const webrtc::CandidatePairChangeEvent &event) {
  RTC_OBJC_TYPE(RTCPeerConnection) *peer_connection = peer_connection_;
  if (peer_connection == nil) {
    return;
  }
  id<RTC_OBJC_TYPE(RTCPeerConnectionDelegate)> delegate =
      peer_connection.delegate;
  if (delegate == nil) {
    return;
  }
  const auto &selected_pair = event.selected_candidate_pair;
  IceCandidate local_candidate_wrapper(
      event.transport_name, -1, selected_pair.local_candidate());
  RTC_OBJC_TYPE(RTCIceCandidate) *local_candidate =
      [[RTC_OBJC_TYPE(RTCIceCandidate) alloc]
          initWithNativeCandidate:&local_candidate_wrapper];
  IceCandidate remote_candidate_wrapper(
      event.transport_name, -1, selected_pair.remote_candidate());
  RTC_OBJC_TYPE(RTCIceCandidate) *remote_candidate =
      [[RTC_OBJC_TYPE(RTCIceCandidate) alloc]
          initWithNativeCandidate:&remote_candidate_wrapper];
  NSString *nsstr_reason = [NSString stringForStdString:event.reason];
  if ([delegate respondsToSelector:@selector
                (peerConnection:
                    didChangeLocalCandidate:remoteCandidate:lastReceivedMs
                                           :changeReason:)]) {
    [delegate peerConnection:peer_connection
        didChangeLocalCandidate:local_candidate
                remoteCandidate:remote_candidate
                 lastReceivedMs:event.last_data_received_ms
                   changeReason:nsstr_reason];
  }
}

void PeerConnectionDelegateAdapter::OnAddTrack(
    webrtc::scoped_refptr<RtpReceiverInterface> receiver,
    const std::vector<webrtc::scoped_refptr<MediaStreamInterface>> &streams) {
  RTC_OBJC_TYPE(RTCPeerConnection) *peer_connection = peer_connection_;
  if (peer_connection == nil) {
    return;
  }
  id<RTC_OBJC_TYPE(RTCPeerConnectionDelegate)> delegate =
      peer_connection.delegate;
  if (delegate == nil) {
    return;
  }

  if ([delegate respondsToSelector:@selector(peerConnection:
                                             didAddReceiver:streams:)]) {
    NSMutableArray *mediaStreams =
        [NSMutableArray arrayWithCapacity:streams.size()];
    for (const auto &nativeStream : streams) {
      RTC_OBJC_TYPE(RTCMediaStream) *mediaStream =
          [[RTC_OBJC_TYPE(RTCMediaStream) alloc]
                initWithFactory:peer_connection.factory
              nativeMediaStream:nativeStream];
      [mediaStreams addObject:mediaStream];
    }
    RTC_OBJC_TYPE(RTCRtpReceiver) *rtpReceiver = [[RTC_OBJC_TYPE(RTCRtpReceiver)
        alloc] initWithFactory:peer_connection.factory
             nativeRtpReceiver:receiver];

    [delegate peerConnection:peer_connection
              didAddReceiver:rtpReceiver
                     streams:mediaStreams];
  }
}

void PeerConnectionDelegateAdapter::OnRemoveTrack(
    webrtc::scoped_refptr<RtpReceiverInterface> receiver) {
  RTC_OBJC_TYPE(RTCPeerConnection) *peer_connection = peer_connection_;
  if (peer_connection == nil) {
    return;
  }
  id<RTC_OBJC_TYPE(RTCPeerConnectionDelegate)> delegate =
      peer_connection.delegate;
  if (delegate == nil) {
    return;
  }
  if ([delegate respondsToSelector:@selector(peerConnection:
                                          didRemoveReceiver:)]) {
    RTC_OBJC_TYPE(RTCRtpReceiver) *rtpReceiver = [[RTC_OBJC_TYPE(RTCRtpReceiver)
        alloc] initWithFactory:peer_connection.factory
             nativeRtpReceiver:receiver];
    [delegate peerConnection:peer_connection didRemoveReceiver:rtpReceiver];
  }
}

}  // namespace webrtc

@implementation RTC_OBJC_TYPE (RTCPeerConnection) {
  RTC_OBJC_TYPE(RTCPeerConnectionFactory) * _factory;
  NSMutableArray<RTC_OBJC_TYPE(RTCMediaStream) *> *_localStreams;
  std::unique_ptr<webrtc::PeerConnectionDelegateAdapter> _observer;
  webrtc::scoped_refptr<webrtc::PeerConnectionInterface> _peerConnection;
  std::unique_ptr<webrtc::MediaConstraints> _nativeConstraints;
  BOOL _hasStartedRtcEventLog;
}

@synthesize delegate = _delegate;
@synthesize factory = _factory;

- (nullable instancetype)
        initWithFactory:(RTC_OBJC_TYPE(RTCPeerConnectionFactory) *)factory
          configuration:(RTC_OBJC_TYPE(RTCConfiguration) *)configuration
            constraints:(RTC_OBJC_TYPE(RTCMediaConstraints) *)constraints
    certificateVerifier:(nullable id<RTC_OBJC_TYPE(RTCSSLCertificateVerifier)>)
                            certificateVerifier
               delegate:(id<RTC_OBJC_TYPE(RTCPeerConnectionDelegate)>)delegate {
  NSParameterAssert(factory);
  std::unique_ptr<webrtc::PeerConnectionDependencies> dependencies =
      std::make_unique<webrtc::PeerConnectionDependencies>(nullptr);
  if (certificateVerifier != nil) {
    dependencies->tls_cert_verifier =
        webrtc::ObjCToNativeCertificateVerifier(certificateVerifier);
  }
  return [self initWithDependencies:factory
                      configuration:configuration
                        constraints:constraints
                       dependencies:std::move(dependencies)
                           delegate:delegate];
}

- (nullable instancetype)
    initWithDependencies:(RTC_OBJC_TYPE(RTCPeerConnectionFactory) *)factory
           configuration:(RTC_OBJC_TYPE(RTCConfiguration) *)configuration
             constraints:(RTC_OBJC_TYPE(RTCMediaConstraints) *)constraints
            dependencies:(std::unique_ptr<webrtc::PeerConnectionDependencies>)
                             dependencies
                delegate:
                    (id<RTC_OBJC_TYPE(RTCPeerConnectionDelegate)>)delegate {
  NSParameterAssert(factory);
  NSParameterAssert(dependencies.get());
  std::unique_ptr<webrtc::PeerConnectionInterface::RTCConfiguration> config(
      [configuration createNativeConfiguration]);
  if (!config) {
    return nil;
  }
  self = [super init];
  if (self) {
    _observer.reset(new webrtc::PeerConnectionDelegateAdapter(self));
    _nativeConstraints = constraints.nativeConstraints;
    CopyConstraintsIntoRtcConfiguration(_nativeConstraints.get(), config.get());

    webrtc::PeerConnectionDependencies deps = std::move(*dependencies);
    deps.observer = _observer.get();
    auto result = factory.nativeFactory->CreatePeerConnectionOrError(
        *config, std::move(deps));

    if (!result.ok()) {
      return nil;
    }
    _peerConnection = result.MoveValue();
    _factory = factory;
    _localStreams = [[NSMutableArray alloc] init];
    _delegate = delegate;
  }
  return self;
}

- (void)dealloc {
  if (_peerConnection == nullptr) {
    // Initialization failed.
    return;
  }
  webrtc::Thread *signalingThread = _peerConnection->signaling_thread();
  if (!signalingThread->IsCurrent() && !_factory.networkThread->IsCurrent()) {
    // .cxx_destruct releases the proxy, which destroys the native
    // PeerConnection on the signaling thread.
    return;
  }
  // Released on the signaling thread, possibly from inside a PeerConnection
  // callback, or on the network thread, e.g. from a certificate verifier.
  // Destroying the native PeerConnection here could delete it while its
  // methods are on the stack, or deadlock since it blocks on the network
  // thread while it's destroyed. Destroy it from a separate task on the
  // signaling thread instead. The observer must outlive the native
  // PeerConnection, and the factory keeps the signaling thread running until
  // the task has run.
  signalingThread->PostTask([peerConnection = std::move(_peerConnection),
                             observer = std::move(_observer),
                             factory = _factory]() mutable {
    RTC_DCHECK(factory.signalingThread->IsCurrent());
    peerConnection = nullptr;
    observer = nullptr;
    // If this is the last reference to the factory, the factory is
    // deallocated here, on its signaling thread, which it supports.
    factory = nil;
  });
}

- (NSArray<RTC_OBJC_TYPE(RTCMediaStream) *> *)localStreams {
  return [_localStreams copy];
}

- (RTC_OBJC_TYPE(RTCSessionDescription) *)localDescription {
  // It's only safe to operate on SessionDescriptionInterface on the signaling
  // thread.
  return _peerConnection->signaling_thread()->BlockingCall([self] {
    const webrtc::SessionDescriptionInterface *description =
        _peerConnection->local_description();
    return description ? [[RTC_OBJC_TYPE(RTCSessionDescription) alloc]
                             initWithNativeDescription:description] :
                         nil;
  });
}

- (RTC_OBJC_TYPE(RTCSessionDescription) *)remoteDescription {
  // It's only safe to operate on SessionDescriptionInterface on the signaling
  // thread.
  return _peerConnection->signaling_thread()->BlockingCall([self] {
    const webrtc::SessionDescriptionInterface *description =
        _peerConnection->remote_description();
    return description ? [[RTC_OBJC_TYPE(RTCSessionDescription) alloc]
                             initWithNativeDescription:description] :
                         nil;
  });
}

- (RTCSignalingState)signalingState {
  return [[self class]
      signalingStateForNativeState:_peerConnection->signaling_state()];
}

- (RTCIceConnectionState)iceConnectionState {
  return [[self class]
      iceConnectionStateForNativeState:_peerConnection->ice_connection_state()];
}

- (RTCPeerConnectionState)connectionState {
  return [[self class]
      connectionStateForNativeState:_peerConnection->peer_connection_state()];
}

- (RTCIceGatheringState)iceGatheringState {
  return [[self class]
      iceGatheringStateForNativeState:_peerConnection->ice_gathering_state()];
}

- (BOOL)setConfiguration:(RTC_OBJC_TYPE(RTCConfiguration) *)configuration {
  std::unique_ptr<webrtc::PeerConnectionInterface::RTCConfiguration> config(
      [configuration createNativeConfiguration]);
  if (!config) {
    return NO;
  }
  CopyConstraintsIntoRtcConfiguration(_nativeConstraints.get(), config.get());
  return _peerConnection->SetConfiguration(*config).ok();
}

- (RTC_OBJC_TYPE(RTCConfiguration) *)configuration {
  webrtc::PeerConnectionInterface::RTCConfiguration config =
      _peerConnection->GetConfiguration();
  return [[RTC_OBJC_TYPE(RTCConfiguration) alloc]
      initWithNativeConfiguration:config];
}

- (void)close {
  _peerConnection->Close();
}

- (void)addIceCandidate:(RTC_OBJC_TYPE(RTCIceCandidate) *)candidate {
  std::unique_ptr<const webrtc::IceCandidate> iceCandidate(
      candidate.nativeCandidate);
  _peerConnection->AddIceCandidate(iceCandidate.get());
}
- (void)addIceCandidate:(RTC_OBJC_TYPE(RTCIceCandidate) *)candidate
      completionHandler:(void (^)(NSError *_Nullable error))completionHandler {
  RTC_DCHECK(completionHandler != nil);
  _peerConnection->AddIceCandidate(
      candidate.nativeCandidate, [completionHandler](const auto &error) {
        if (error.ok()) {
          completionHandler(nil);
        } else {
          NSString *str = [NSString stringForStdString:error.message()];
          NSError *err =
              [NSError errorWithDomain:kRTCPeerConnectionErrorDomain
                                  code:static_cast<NSInteger>(error.type())
                              userInfo:@{NSLocalizedDescriptionKey : str}];
          completionHandler(err);
        }
      });
}
- (void)removeIceCandidates:
    (NSArray<RTC_OBJC_TYPE(RTCIceCandidate) *> *)iceCandidates {
  for (RTC_OBJC_TYPE(RTCIceCandidate) * iceCandidate in iceCandidates) {
    std::unique_ptr<webrtc::IceCandidate> candidate(
        iceCandidate.nativeCandidate);
    if (candidate) {
      _peerConnection->RemoveIceCandidate(candidate.get());
    }
  }
}

- (void)addStream:(RTC_OBJC_TYPE(RTCMediaStream) *)stream {
  RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN();
  if (!_peerConnection->AddStream(stream.nativeMediaStream.get())) {
    RTCLogError(@"Failed to add stream: %@", stream);
    return;
  }
  [_localStreams addObject:stream];
  RTC_ALLOW_PLAN_B_DEPRECATION_END();
}

- (void)removeStream:(RTC_OBJC_TYPE(RTCMediaStream) *)stream {
  RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN();
  _peerConnection->RemoveStream(stream.nativeMediaStream.get());
  [_localStreams removeObject:stream];
  RTC_ALLOW_PLAN_B_DEPRECATION_END();
}

- (nullable RTC_OBJC_TYPE(RTCRtpSender) *)
     addTrack:(RTC_OBJC_TYPE(RTCMediaStreamTrack) *)track
    streamIds:(NSArray<NSString *> *)streamIds {
  std::vector<std::string> nativeStreamIds;
  for (NSString *streamId in streamIds) {
    nativeStreamIds.push_back([streamId UTF8String]);
  }
  webrtc::RTCErrorOr<webrtc::scoped_refptr<webrtc::RtpSenderInterface>>
      nativeSenderOrError =
          _peerConnection->AddTrack(track.nativeTrack, nativeStreamIds);
  if (!nativeSenderOrError.ok()) {
    RTCLogError(@"Failed to add track %@: %s",
                track,
                nativeSenderOrError.error().message());
    return nil;
  }
  return [[RTC_OBJC_TYPE(RTCRtpSender) alloc]
      initWithFactory:self.factory
      nativeRtpSender:nativeSenderOrError.MoveValue()];
}

- (BOOL)removeTrack:(RTC_OBJC_TYPE(RTCRtpSender) *)sender {
  bool result =
      _peerConnection->RemoveTrackOrError(sender.nativeRtpSender).ok();
  if (!result) {
    RTCLogError(@"Failed to remote track %@", sender);
  }
  return result;
}

- (nullable RTC_OBJC_TYPE(RTCRtpTransceiver) *)addTransceiverWithTrack:
    (RTC_OBJC_TYPE(RTCMediaStreamTrack) *)track {
  return [self addTransceiverWithTrack:track
                                  init:[[RTC_OBJC_TYPE(RTCRtpTransceiverInit)
                                           alloc] init]];
}

- (nullable RTC_OBJC_TYPE(RTCRtpTransceiver) *)
    addTransceiverWithTrack:(RTC_OBJC_TYPE(RTCMediaStreamTrack) *)track
                       init:(RTC_OBJC_TYPE(RTCRtpTransceiverInit) *)init {
  webrtc::RTCErrorOr<webrtc::scoped_refptr<webrtc::RtpTransceiverInterface>>
      nativeTransceiverOrError =
          _peerConnection->AddTransceiver(track.nativeTrack, init.nativeInit);
  if (!nativeTransceiverOrError.ok()) {
    RTCLogError(@"Failed to add transceiver %@: %s",
                track,
                nativeTransceiverOrError.error().message());
    return nil;
  }
  return [[RTC_OBJC_TYPE(RTCRtpTransceiver) alloc]
           initWithFactory:self.factory
      nativeRtpTransceiver:nativeTransceiverOrError.MoveValue()];
}

- (nullable RTC_OBJC_TYPE(RTCRtpTransceiver) *)addTransceiverOfType:
    (RTCRtpMediaType)mediaType {
  return [self
      addTransceiverOfType:mediaType
                      init:[[RTC_OBJC_TYPE(RTCRtpTransceiverInit) alloc] init]];
}

- (nullable RTC_OBJC_TYPE(RTCRtpTransceiver) *)
    addTransceiverOfType:(RTCRtpMediaType)mediaType
                    init:(RTC_OBJC_TYPE(RTCRtpTransceiverInit) *)init {
  webrtc::RTCErrorOr<webrtc::scoped_refptr<webrtc::RtpTransceiverInterface>>
      nativeTransceiverOrError = _peerConnection->AddTransceiver(
          [RTC_OBJC_TYPE(RTCRtpReceiver) nativeMediaTypeForMediaType:mediaType],
          init.nativeInit);
  if (!nativeTransceiverOrError.ok()) {
    RTCLogError(@"Failed to add transceiver %@: %s",
                [RTC_OBJC_TYPE(RTCRtpReceiver) stringForMediaType:mediaType],
                nativeTransceiverOrError.error().message());
    return nil;
  }
  return [[RTC_OBJC_TYPE(RTCRtpTransceiver) alloc]
           initWithFactory:self.factory
      nativeRtpTransceiver:nativeTransceiverOrError.MoveValue()];
}

- (void)restartIce {
  _peerConnection->RestartIce();
}

- (void)offerForConstraints:(RTC_OBJC_TYPE(RTCMediaConstraints) *)constraints
          completionHandler:
              (RTCCreateSessionDescriptionCompletionHandler)completionHandler {
  RTC_DCHECK(completionHandler != nil);
  webrtc::scoped_refptr<webrtc::CreateSessionDescriptionObserverAdapter>
      observer = webrtc::make_ref_counted<
          webrtc::CreateSessionDescriptionObserverAdapter>(completionHandler);
  webrtc::PeerConnectionInterface::RTCOfferAnswerOptions options;
  CopyConstraintsIntoOfferAnswerOptions(constraints.nativeConstraints.get(),
                                        &options);

  _peerConnection->CreateOffer(observer.get(), options);
}

- (void)answerForConstraints:(RTC_OBJC_TYPE(RTCMediaConstraints) *)constraints
           completionHandler:
               (RTCCreateSessionDescriptionCompletionHandler)completionHandler {
  RTC_DCHECK(completionHandler != nil);
  webrtc::scoped_refptr<webrtc::CreateSessionDescriptionObserverAdapter>
      observer = webrtc::make_ref_counted<
          webrtc::CreateSessionDescriptionObserverAdapter>(completionHandler);
  webrtc::PeerConnectionInterface::RTCOfferAnswerOptions options;
  CopyConstraintsIntoOfferAnswerOptions(constraints.nativeConstraints.get(),
                                        &options);

  _peerConnection->CreateAnswer(observer.get(), options);
}

- (void)setLocalDescription:(RTC_OBJC_TYPE(RTCSessionDescription) *)sdp
          completionHandler:
              (RTCSetSessionDescriptionCompletionHandler)completionHandler {
  RTC_DCHECK(completionHandler != nil);
  webrtc::scoped_refptr<webrtc::SetLocalDescriptionObserverInterface> observer =
      webrtc::make_ref_counted<::SetSessionDescriptionObserver>(
          completionHandler);
  _peerConnection->SetLocalDescription(sdp.nativeDescription, observer);
}

- (void)setLocalDescriptionWithCompletionHandler:
    (RTCSetSessionDescriptionCompletionHandler)completionHandler {
  RTC_DCHECK(completionHandler != nil);
  webrtc::scoped_refptr<webrtc::SetLocalDescriptionObserverInterface> observer =
      webrtc::make_ref_counted<::SetSessionDescriptionObserver>(
          completionHandler);
  _peerConnection->SetLocalDescription(observer);
}

- (void)setRemoteDescription:(RTC_OBJC_TYPE(RTCSessionDescription) *)sdp
           completionHandler:
               (RTCSetSessionDescriptionCompletionHandler)completionHandler {
  RTC_DCHECK(completionHandler != nil);
  webrtc::scoped_refptr<webrtc::SetRemoteDescriptionObserverInterface>
      observer = webrtc::make_ref_counted<::SetSessionDescriptionObserver>(
          completionHandler);
  _peerConnection->SetRemoteDescription(sdp.nativeDescription, observer);
}

- (BOOL)setBweMinBitrateBps:(nullable NSNumber *)minBitrateBps
          currentBitrateBps:(nullable NSNumber *)currentBitrateBps
              maxBitrateBps:(nullable NSNumber *)maxBitrateBps {
  webrtc::BitrateSettings params;
  if (minBitrateBps != nil) {
    params.min_bitrate_bps = std::optional<int>(minBitrateBps.intValue);
  }
  if (currentBitrateBps != nil) {
    params.start_bitrate_bps = std::optional<int>(currentBitrateBps.intValue);
  }
  if (maxBitrateBps != nil) {
    params.max_bitrate_bps = std::optional<int>(maxBitrateBps.intValue);
  }
  return _peerConnection->SetBitrate(params).ok();
}

- (BOOL)startRtcEventLogWithFilePath:(NSString *)filePath
                      maxSizeInBytes:(int64_t)maxSizeInBytes {
  RTC_DCHECK(filePath.length);
  RTC_DCHECK_GT(maxSizeInBytes, 0);
  RTC_DCHECK(!_hasStartedRtcEventLog);
  if (_hasStartedRtcEventLog) {
    RTCLogError(@"Event logging already started.");
    return NO;
  }
  FILE *f = fopen(filePath.UTF8String, "wb");
  if (!f) {
    RTCLogError(@"Error opening file: %@. Error: %d", filePath, errno);
    return NO;
  }
  // TODO(eladalon): It would be better to not allow negative values into PC.
  const size_t max_size = (maxSizeInBytes < 0) ?
      webrtc::RtcEventLog::kUnlimitedOutput :
      webrtc::saturated_cast<size_t>(maxSizeInBytes);

  _hasStartedRtcEventLog = _peerConnection->StartRtcEventLog(
      std::make_unique<webrtc::RtcEventLogOutputFile>(f, max_size));
  return _hasStartedRtcEventLog;
}

- (void)stopRtcEventLog {
  _peerConnection->StopRtcEventLog();
  _hasStartedRtcEventLog = NO;
}

- (RTC_OBJC_TYPE(RTCRtpSender) *)senderWithKind:(NSString *)kind
                                       streamId:(NSString *)streamId {
  std::string nativeKind = [NSString stdStringForString:kind];
  std::string nativeStreamId = [NSString stdStringForString:streamId];
  RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN();
  webrtc::scoped_refptr<webrtc::RtpSenderInterface> nativeSender(
      _peerConnection->CreateSender(nativeKind, nativeStreamId));
  RTC_ALLOW_PLAN_B_DEPRECATION_END();
  return nativeSender ?
      [[RTC_OBJC_TYPE(RTCRtpSender) alloc] initWithFactory:self.factory
                                           nativeRtpSender:nativeSender] :
      nil;
}

- (NSArray<RTC_OBJC_TYPE(RTCRtpSender) *> *)senders {
  std::vector<webrtc::scoped_refptr<webrtc::RtpSenderInterface>> nativeSenders(
      _peerConnection->GetSenders());
  NSMutableArray *senders = [[NSMutableArray alloc] init];
  for (const auto &nativeSender : nativeSenders) {
    RTC_OBJC_TYPE(RTCRtpSender) *sender =
        [[RTC_OBJC_TYPE(RTCRtpSender) alloc] initWithFactory:self.factory
                                             nativeRtpSender:nativeSender];
    [senders addObject:sender];
  }
  return senders;
}

- (NSArray<RTC_OBJC_TYPE(RTCRtpReceiver) *> *)receivers {
  std::vector<webrtc::scoped_refptr<webrtc::RtpReceiverInterface>>
      nativeReceivers(_peerConnection->GetReceivers());
  NSMutableArray *receivers = [[NSMutableArray alloc] init];
  for (const auto &nativeReceiver : nativeReceivers) {
    RTC_OBJC_TYPE(RTCRtpReceiver) *receiver =
        [[RTC_OBJC_TYPE(RTCRtpReceiver) alloc] initWithFactory:self.factory
                                             nativeRtpReceiver:nativeReceiver];
    [receivers addObject:receiver];
  }
  return receivers;
}

- (NSArray<RTC_OBJC_TYPE(RTCRtpTransceiver) *> *)transceivers {
  std::vector<webrtc::scoped_refptr<webrtc::RtpTransceiverInterface>>
      nativeTransceivers(_peerConnection->GetTransceivers());
  NSMutableArray *transceivers = [[NSMutableArray alloc] init];
  for (const auto &nativeTransceiver : nativeTransceivers) {
    RTC_OBJC_TYPE(RTCRtpTransceiver) *transceiver =
        [[RTC_OBJC_TYPE(RTCRtpTransceiver) alloc]
                 initWithFactory:self.factory
            nativeRtpTransceiver:nativeTransceiver];
    [transceivers addObject:transceiver];
  }
  return transceivers;
}

#pragma mark - Private

+ (webrtc::PeerConnectionInterface::SignalingState)nativeSignalingStateForState:
    (RTCSignalingState)state {
  switch (state) {
    case RTCSignalingStateStable:
      return webrtc::PeerConnectionInterface::kStable;
    case RTCSignalingStateHaveLocalOffer:
      return webrtc::PeerConnectionInterface::kHaveLocalOffer;
    case RTCSignalingStateHaveLocalPrAnswer:
      return webrtc::PeerConnectionInterface::kHaveLocalPrAnswer;
    case RTCSignalingStateHaveRemoteOffer:
      return webrtc::PeerConnectionInterface::kHaveRemoteOffer;
    case RTCSignalingStateHaveRemotePrAnswer:
      return webrtc::PeerConnectionInterface::kHaveRemotePrAnswer;
    case RTCSignalingStateClosed:
      return webrtc::PeerConnectionInterface::kClosed;
  }
}

+ (RTCSignalingState)signalingStateForNativeState:
    (webrtc::PeerConnectionInterface::SignalingState)nativeState {
  switch (nativeState) {
    case webrtc::PeerConnectionInterface::kStable:
      return RTCSignalingStateStable;
    case webrtc::PeerConnectionInterface::kHaveLocalOffer:
      return RTCSignalingStateHaveLocalOffer;
    case webrtc::PeerConnectionInterface::kHaveLocalPrAnswer:
      return RTCSignalingStateHaveLocalPrAnswer;
    case webrtc::PeerConnectionInterface::kHaveRemoteOffer:
      return RTCSignalingStateHaveRemoteOffer;
    case webrtc::PeerConnectionInterface::kHaveRemotePrAnswer:
      return RTCSignalingStateHaveRemotePrAnswer;
    case webrtc::PeerConnectionInterface::kClosed:
      return RTCSignalingStateClosed;
  }
}

+ (NSString *)stringForSignalingState:(RTCSignalingState)state {
  switch (state) {
    case RTCSignalingStateStable:
      return @"STABLE";
    case RTCSignalingStateHaveLocalOffer:
      return @"HAVE_LOCAL_OFFER";
    case RTCSignalingStateHaveLocalPrAnswer:
      return @"HAVE_LOCAL_PRANSWER";
    case RTCSignalingStateHaveRemoteOffer:
      return @"HAVE_REMOTE_OFFER";
    case RTCSignalingStateHaveRemotePrAnswer:
      return @"HAVE_REMOTE_PRANSWER";
    case RTCSignalingStateClosed:
      return @"CLOSED";
  }
}

+ (webrtc::PeerConnectionInterface::PeerConnectionState)
    nativeConnectionStateForState:(RTCPeerConnectionState)state {
  switch (state) {
    case RTCPeerConnectionStateNew:
      return webrtc::PeerConnectionInterface::PeerConnectionState::kNew;
    case RTCPeerConnectionStateConnecting:
      return webrtc::PeerConnectionInterface::PeerConnectionState::kConnecting;
    case RTCPeerConnectionStateConnected:
      return webrtc::PeerConnectionInterface::PeerConnectionState::kConnected;
    case RTCPeerConnectionStateFailed:
      return webrtc::PeerConnectionInterface::PeerConnectionState::kFailed;
    case RTCPeerConnectionStateDisconnected:
      return webrtc::PeerConnectionInterface::PeerConnectionState::
          kDisconnected;
    case RTCPeerConnectionStateClosed:
      return webrtc::PeerConnectionInterface::PeerConnectionState::kClosed;
  }
}

+ (RTCPeerConnectionState)connectionStateForNativeState:
    (webrtc::PeerConnectionInterface::PeerConnectionState)nativeState {
  switch (nativeState) {
    case webrtc::PeerConnectionInterface::PeerConnectionState::kNew:
      return RTCPeerConnectionStateNew;
    case webrtc::PeerConnectionInterface::PeerConnectionState::kConnecting:
      return RTCPeerConnectionStateConnecting;
    case webrtc::PeerConnectionInterface::PeerConnectionState::kConnected:
      return RTCPeerConnectionStateConnected;
    case webrtc::PeerConnectionInterface::PeerConnectionState::kFailed:
      return RTCPeerConnectionStateFailed;
    case webrtc::PeerConnectionInterface::PeerConnectionState::kDisconnected:
      return RTCPeerConnectionStateDisconnected;
    case webrtc::PeerConnectionInterface::PeerConnectionState::kClosed:
      return RTCPeerConnectionStateClosed;
  }
}

+ (NSString *)stringForConnectionState:(RTCPeerConnectionState)state {
  switch (state) {
    case RTCPeerConnectionStateNew:
      return @"NEW";
    case RTCPeerConnectionStateConnecting:
      return @"CONNECTING";
    case RTCPeerConnectionStateConnected:
      return @"CONNECTED";
    case RTCPeerConnectionStateFailed:
      return @"FAILED";
    case RTCPeerConnectionStateDisconnected:
      return @"DISCONNECTED";
    case RTCPeerConnectionStateClosed:
      return @"CLOSED";
  }
}

+ (webrtc::PeerConnectionInterface::IceConnectionState)
    nativeIceConnectionStateForState:(RTCIceConnectionState)state {
  switch (state) {
    case RTCIceConnectionStateNew:
      return webrtc::PeerConnectionInterface::kIceConnectionNew;
    case RTCIceConnectionStateChecking:
      return webrtc::PeerConnectionInterface::kIceConnectionChecking;
    case RTCIceConnectionStateConnected:
      return webrtc::PeerConnectionInterface::kIceConnectionConnected;
    case RTCIceConnectionStateCompleted:
      return webrtc::PeerConnectionInterface::kIceConnectionCompleted;
    case RTCIceConnectionStateFailed:
      return webrtc::PeerConnectionInterface::kIceConnectionFailed;
    case RTCIceConnectionStateDisconnected:
      return webrtc::PeerConnectionInterface::kIceConnectionDisconnected;
    case RTCIceConnectionStateClosed:
      return webrtc::PeerConnectionInterface::kIceConnectionClosed;
    case RTCIceConnectionStateCount:
      return webrtc::PeerConnectionInterface::kIceConnectionMax;
  }
}

+ (RTCIceConnectionState)iceConnectionStateForNativeState:
    (webrtc::PeerConnectionInterface::IceConnectionState)nativeState {
  switch (nativeState) {
    case webrtc::PeerConnectionInterface::kIceConnectionNew:
      return RTCIceConnectionStateNew;
    case webrtc::PeerConnectionInterface::kIceConnectionChecking:
      return RTCIceConnectionStateChecking;
    case webrtc::PeerConnectionInterface::kIceConnectionConnected:
      return RTCIceConnectionStateConnected;
    case webrtc::PeerConnectionInterface::kIceConnectionCompleted:
      return RTCIceConnectionStateCompleted;
    case webrtc::PeerConnectionInterface::kIceConnectionFailed:
      return RTCIceConnectionStateFailed;
    case webrtc::PeerConnectionInterface::kIceConnectionDisconnected:
      return RTCIceConnectionStateDisconnected;
    case webrtc::PeerConnectionInterface::kIceConnectionClosed:
      return RTCIceConnectionStateClosed;
    case webrtc::PeerConnectionInterface::kIceConnectionMax:
      return RTCIceConnectionStateCount;
  }
}

+ (NSString *)stringForIceConnectionState:(RTCIceConnectionState)state {
  switch (state) {
    case RTCIceConnectionStateNew:
      return @"NEW";
    case RTCIceConnectionStateChecking:
      return @"CHECKING";
    case RTCIceConnectionStateConnected:
      return @"CONNECTED";
    case RTCIceConnectionStateCompleted:
      return @"COMPLETED";
    case RTCIceConnectionStateFailed:
      return @"FAILED";
    case RTCIceConnectionStateDisconnected:
      return @"DISCONNECTED";
    case RTCIceConnectionStateClosed:
      return @"CLOSED";
    case RTCIceConnectionStateCount:
      return @"COUNT";
  }
}

+ (webrtc::PeerConnectionInterface::IceGatheringState)
    nativeIceGatheringStateForState:(RTCIceGatheringState)state {
  switch (state) {
    case RTCIceGatheringStateNew:
      return webrtc::PeerConnectionInterface::kIceGatheringNew;
    case RTCIceGatheringStateGathering:
      return webrtc::PeerConnectionInterface::kIceGatheringGathering;
    case RTCIceGatheringStateComplete:
      return webrtc::PeerConnectionInterface::kIceGatheringComplete;
  }
}

+ (RTCIceGatheringState)iceGatheringStateForNativeState:
    (webrtc::PeerConnectionInterface::IceGatheringState)nativeState {
  switch (nativeState) {
    case webrtc::PeerConnectionInterface::kIceGatheringNew:
      return RTCIceGatheringStateNew;
    case webrtc::PeerConnectionInterface::kIceGatheringGathering:
      return RTCIceGatheringStateGathering;
    case webrtc::PeerConnectionInterface::kIceGatheringComplete:
      return RTCIceGatheringStateComplete;
  }
}

+ (NSString *)stringForIceGatheringState:(RTCIceGatheringState)state {
  switch (state) {
    case RTCIceGatheringStateNew:
      return @"NEW";
    case RTCIceGatheringStateGathering:
      return @"GATHERING";
    case RTCIceGatheringStateComplete:
      return @"COMPLETE";
  }
}

+ (webrtc::PeerConnectionInterface::StatsOutputLevel)
    nativeStatsOutputLevelForLevel:(RTCStatsOutputLevel)level {
  switch (level) {
    case RTCStatsOutputLevelStandard:
      return webrtc::PeerConnectionInterface::kStatsOutputLevelStandard;
    case RTCStatsOutputLevelDebug:
      return webrtc::PeerConnectionInterface::kStatsOutputLevelDebug;
  }
}

- (webrtc::scoped_refptr<webrtc::PeerConnectionInterface>)nativePeerConnection {
  return _peerConnection;
}

@end
