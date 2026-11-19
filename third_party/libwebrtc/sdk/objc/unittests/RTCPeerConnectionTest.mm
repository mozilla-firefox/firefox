/*
 *  Copyright 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#import <Foundation/Foundation.h>
#import <XCTest/XCTest.h>
#import <objc/runtime.h>

#include "test/gtest.h"

#include <memory>
#include <vector>

#import "api/peerconnection/RTCConfiguration+Private.h"
#import "api/peerconnection/RTCConfiguration.h"
#import "api/peerconnection/RTCCryptoOptions.h"
#import "api/peerconnection/RTCIceCandidate.h"
#import "api/peerconnection/RTCIceServer.h"
#import "api/peerconnection/RTCMediaConstraints.h"
#import "api/peerconnection/RTCPeerConnection.h"
#import "api/peerconnection/RTCPeerConnectionFactory+Native.h"
#import "api/peerconnection/RTCPeerConnectionFactory+Private.h"
#import "api/peerconnection/RTCPeerConnectionFactory.h"
#import "api/peerconnection/RTCSessionDescription.h"
#import "base/RTCSSLCertificateVerifier.h"
#import "helpers/NSString+StdString.h"
#include "rtc_base/thread.h"

namespace {

// Key for associating an RTCDeallocSentinel with another object.
char kDeallocSentinelKey;

// Returns the deadline for waiting for an asynchronous event.
dispatch_time_t TimeoutFromNow() {
  return dispatch_time(DISPATCH_TIME_NOW,
                       static_cast<int64_t>(15 * NSEC_PER_SEC));
}

NSError *DeallocatedPeerConnectionError() {
  return [NSError
      errorWithDomain:kRTCPeerConnectionErrorDomain
                 code:-1
             userInfo:@{
               NSLocalizedDescriptionKey :
                   @"PeerConnection was deallocated during negotiation."
             }];
}

}  // namespace

// Calls a block when deallocated. Associate it with another object to find out
// when, and on which thread, that object is deallocated.
@interface RTCDeallocSentinel : NSObject
- (instancetype)initWithBlock:(void (^)(void))block;
@end

@implementation RTCDeallocSentinel {
  void (^_block)(void);
}

- (instancetype)initWithBlock:(void (^)(void))block {
  self = [super init];
  if (self) {
    _block = block;
  }
  return self;
}

- (void)dealloc {
  _block();
}

@end

// Accepts all certificates. A native PeerConnection owns its certificate
// verifier, so the verifier is deallocated when the native PeerConnection is
// destroyed.
@interface RTCAcceptingCertificateVerifier
    : NSObject <RTC_OBJC_TYPE (RTCSSLCertificateVerifier)>
@end

@implementation RTCAcceptingCertificateVerifier

- (BOOL)verify:(NSData *)derCertificate {
  return YES;
}

@end

// Holds the only reference to a peer connection and releases it from
// peerConnection:didChangeSignalingState: when the state changes to
// `releaseState`. The callback runs on the signaling thread.
@interface RTCReleasingPeerConnectionDelegate
    : NSObject <RTC_OBJC_TYPE (RTCPeerConnectionDelegate)>
@property(nonatomic, nullable) RTC_OBJC_TYPE(RTCPeerConnection) *
    peerConnection;
@property(nonatomic) RTCSignalingState releaseState;
@end

@implementation RTCReleasingPeerConnectionDelegate

@synthesize peerConnection = _peerConnection;
@synthesize releaseState = _releaseState;

- (void)peerConnection:(RTC_OBJC_TYPE(RTCPeerConnection) *)peerConnection
    didChangeSignalingState:(RTCSignalingState)stateChanged {
  if (stateChanged == _releaseState) {
    _peerConnection = nil;
  }
}

- (void)peerConnection:(RTC_OBJC_TYPE(RTCPeerConnection) *)peerConnection
          didAddStream:(RTC_OBJC_TYPE(RTCMediaStream) *)stream {
}

- (void)peerConnection:(RTC_OBJC_TYPE(RTCPeerConnection) *)peerConnection
       didRemoveStream:(RTC_OBJC_TYPE(RTCMediaStream) *)stream {
}

- (void)peerConnectionShouldNegotiate:
    (RTC_OBJC_TYPE(RTCPeerConnection) *)peerConnection {
}

- (void)peerConnection:(RTC_OBJC_TYPE(RTCPeerConnection) *)peerConnection
    didChangeIceConnectionState:(RTCIceConnectionState)newState {
}

- (void)peerConnection:(RTC_OBJC_TYPE(RTCPeerConnection) *)peerConnection
    didChangeIceGatheringState:(RTCIceGatheringState)newState {
}

- (void)peerConnection:(RTC_OBJC_TYPE(RTCPeerConnection) *)peerConnection
    didGenerateIceCandidate:(RTC_OBJC_TYPE(RTCIceCandidate) *)candidate {
}

- (void)peerConnection:(RTC_OBJC_TYPE(RTCPeerConnection) *)peerConnection
    didRemoveIceCandidates:
        (NSArray<RTC_OBJC_TYPE(RTCIceCandidate) *> *)candidates {
}

- (void)peerConnection:(RTC_OBJC_TYPE(RTCPeerConnection) *)peerConnection
    didOpenDataChannel:(RTC_OBJC_TYPE(RTCDataChannel) *)dataChannel {
}

@end

@interface RTCPeerConnectionTests : XCTestCase
@end

@implementation RTCPeerConnectionTests

- (void)testConfigurationGetter {
  NSArray *urlStrings = @[ @"stun:stun1.example.net" ];
  RTC_OBJC_TYPE(RTCIceServer) *server =
      [[RTC_OBJC_TYPE(RTCIceServer) alloc] initWithURLStrings:urlStrings];

  RTC_OBJC_TYPE(RTCConfiguration) *config =
      [[RTC_OBJC_TYPE(RTCConfiguration) alloc] init];
  config.sdpSemantics = RTCSdpSemanticsUnifiedPlan;
  config.iceServers = @[ server ];
  config.iceTransportPolicy = RTCIceTransportPolicyRelay;
  config.bundlePolicy = RTCBundlePolicyMaxBundle;
  config.rtcpMuxPolicy = RTCRtcpMuxPolicyNegotiate;
  config.tcpCandidatePolicy = RTCTcpCandidatePolicyDisabled;
  config.candidateNetworkPolicy = RTCCandidateNetworkPolicyLowCost;
  const int maxPackets = 60;
  const int timeout = 1500;
  const int interval = 2000;
  config.audioJitterBufferMaxPackets = maxPackets;
  config.audioJitterBufferFastAccelerate = YES;
  config.iceConnectionReceivingTimeout = timeout;
  config.iceBackupCandidatePairPingInterval = interval;
  config.continualGatheringPolicy =
      RTCContinualGatheringPolicyGatherContinually;
  config.shouldPruneTurnPorts = YES;
  config.activeResetSrtpParams = YES;
  config.cryptoOptions = [[RTC_OBJC_TYPE(RTCCryptoOptions) alloc]
           initWithSrtpEnableGcmCryptoSuites:YES
                   srtpPreferGcmCryptoSuites:NO
         srtpEnableAes128Sha1_32CryptoCipher:YES
         srtpEnableAes128Sha1_80CryptoCipher:NO
      srtpEnableEncryptedRtpHeaderExtensions:NO
                sframeRequireFrameEncryption:NO];

  RTC_OBJC_TYPE(RTCMediaConstraints) *contraints =
      [[RTC_OBJC_TYPE(RTCMediaConstraints) alloc]
          initWithMandatoryConstraints:@{}
                   optionalConstraints:nil];
  RTC_OBJC_TYPE(RTCPeerConnectionFactory) *factory =
      [[RTC_OBJC_TYPE(RTCPeerConnectionFactory) alloc] init];

  RTC_OBJC_TYPE(RTCConfiguration) * newConfig;
  @autoreleasepool {
    RTC_OBJC_TYPE(RTCPeerConnection) *peerConnection =
        [factory peerConnectionWithConfiguration:config
                                     constraints:contraints
                                        delegate:nil];
    newConfig = peerConnection.configuration;

    EXPECT_TRUE([peerConnection
        setBweMinBitrateBps:[NSNumber numberWithInt:100000]
          currentBitrateBps:[NSNumber numberWithInt:5000000]
              maxBitrateBps:[NSNumber numberWithInt:500000000]]);
    EXPECT_FALSE([peerConnection setBweMinBitrateBps:[NSNumber numberWithInt:2]
                                   currentBitrateBps:[NSNumber numberWithInt:1]
                                       maxBitrateBps:nil]);
  }

  EXPECT_EQ([config.iceServers count], [newConfig.iceServers count]);
  RTC_OBJC_TYPE(RTCIceServer) *newServer = newConfig.iceServers[0];
  RTC_OBJC_TYPE(RTCIceServer) *origServer = config.iceServers[0];
  std::string origUrl = origServer.urlStrings.firstObject.UTF8String;
  std::string url = newServer.urlStrings.firstObject.UTF8String;
  EXPECT_EQ(origUrl, url);

  EXPECT_EQ(config.iceTransportPolicy, newConfig.iceTransportPolicy);
  EXPECT_EQ(config.bundlePolicy, newConfig.bundlePolicy);
  EXPECT_EQ(config.rtcpMuxPolicy, newConfig.rtcpMuxPolicy);
  EXPECT_EQ(config.tcpCandidatePolicy, newConfig.tcpCandidatePolicy);
  EXPECT_EQ(config.candidateNetworkPolicy, newConfig.candidateNetworkPolicy);
  EXPECT_EQ(config.audioJitterBufferMaxPackets,
            newConfig.audioJitterBufferMaxPackets);
  EXPECT_EQ(config.audioJitterBufferFastAccelerate,
            newConfig.audioJitterBufferFastAccelerate);
  EXPECT_EQ(config.iceConnectionReceivingTimeout,
            newConfig.iceConnectionReceivingTimeout);
  EXPECT_EQ(config.iceBackupCandidatePairPingInterval,
            newConfig.iceBackupCandidatePairPingInterval);
  EXPECT_EQ(config.continualGatheringPolicy,
            newConfig.continualGatheringPolicy);
  EXPECT_EQ(config.shouldPruneTurnPorts, newConfig.shouldPruneTurnPorts);
  EXPECT_EQ(config.activeResetSrtpParams, newConfig.activeResetSrtpParams);
  EXPECT_EQ(config.cryptoOptions.srtpEnableGcmCryptoSuites,
            newConfig.cryptoOptions.srtpEnableGcmCryptoSuites);
  EXPECT_EQ(config.cryptoOptions.srtpPreferGcmCryptoSuites,
            newConfig.cryptoOptions.srtpPreferGcmCryptoSuites);
  EXPECT_EQ(config.cryptoOptions.srtpEnableAes128Sha1_32CryptoCipher,
            newConfig.cryptoOptions.srtpEnableAes128Sha1_32CryptoCipher);
  EXPECT_EQ(config.cryptoOptions.srtpEnableAes128Sha1_80CryptoCipher,
            newConfig.cryptoOptions.srtpEnableAes128Sha1_80CryptoCipher);
  EXPECT_EQ(config.cryptoOptions.srtpEnableEncryptedRtpHeaderExtensions,
            newConfig.cryptoOptions.srtpEnableEncryptedRtpHeaderExtensions);
  EXPECT_EQ(config.cryptoOptions.sframeRequireFrameEncryption,
            newConfig.cryptoOptions.sframeRequireFrameEncryption);
}

- (void)testWithDependencies {
  NSArray *urlStrings = @[ @"stun:stun1.example.net" ];
  RTC_OBJC_TYPE(RTCIceServer) *server =
      [[RTC_OBJC_TYPE(RTCIceServer) alloc] initWithURLStrings:urlStrings];

  RTC_OBJC_TYPE(RTCConfiguration) *config =
      [[RTC_OBJC_TYPE(RTCConfiguration) alloc] init];
  config.sdpSemantics = RTCSdpSemanticsUnifiedPlan;
  config.iceServers = @[ server ];
  RTC_OBJC_TYPE(RTCMediaConstraints) *contraints =
      [[RTC_OBJC_TYPE(RTCMediaConstraints) alloc]
          initWithMandatoryConstraints:@{}
                   optionalConstraints:nil];
  RTC_OBJC_TYPE(RTCPeerConnectionFactory) *factory =
      [[RTC_OBJC_TYPE(RTCPeerConnectionFactory) alloc] init];

  std::unique_ptr<webrtc::PeerConnectionDependencies> pc_dependencies =
      std::make_unique<webrtc::PeerConnectionDependencies>(nullptr);
  @autoreleasepool {
    RTC_OBJC_TYPE(RTCPeerConnection) *peerConnection =
        [factory peerConnectionWithDependencies:config
                                    constraints:contraints
                                   dependencies:std::move(pc_dependencies)
                                       delegate:nil];
    ASSERT_NE(peerConnection, nil);
  }
}

- (void)testWithInvalidSDP {
  RTC_OBJC_TYPE(RTCPeerConnectionFactory) *factory =
      [[RTC_OBJC_TYPE(RTCPeerConnectionFactory) alloc] init];

  RTC_OBJC_TYPE(RTCConfiguration) *config =
      [[RTC_OBJC_TYPE(RTCConfiguration) alloc] init];
  config.sdpSemantics = RTCSdpSemanticsUnifiedPlan;
  RTC_OBJC_TYPE(RTCMediaConstraints) *contraints =
      [[RTC_OBJC_TYPE(RTCMediaConstraints) alloc]
          initWithMandatoryConstraints:@{}
                   optionalConstraints:nil];
  RTC_OBJC_TYPE(RTCPeerConnection) *peerConnection =
      [factory peerConnectionWithConfiguration:config
                                   constraints:contraints
                                      delegate:nil];

  dispatch_semaphore_t negotiatedSem = dispatch_semaphore_create(0);
  [peerConnection setRemoteDescription:[[RTC_OBJC_TYPE(RTCSessionDescription)
                                           alloc] initWithType:RTCSdpTypeOffer
                                                           sdp:@"invalid"]
                     completionHandler:^(NSError *error) {
                       ASSERT_NE(error, nil);
                       if (error != nil) {
                         dispatch_semaphore_signal(negotiatedSem);
                       }
                     }];

  NSTimeInterval timeout = 5;
  ASSERT_EQ(
      0,
      dispatch_semaphore_wait(
          negotiatedSem,
          dispatch_time(DISPATCH_TIME_NOW, (int64_t)(timeout * NSEC_PER_SEC))));
  [peerConnection close];
}

- (void)testWithInvalidIceCandidate {
  RTC_OBJC_TYPE(RTCPeerConnectionFactory) *factory =
      [[RTC_OBJC_TYPE(RTCPeerConnectionFactory) alloc] init];

  RTC_OBJC_TYPE(RTCConfiguration) *config =
      [[RTC_OBJC_TYPE(RTCConfiguration) alloc] init];
  config.sdpSemantics = RTCSdpSemanticsUnifiedPlan;
  RTC_OBJC_TYPE(RTCMediaConstraints) *contraints =
      [[RTC_OBJC_TYPE(RTCMediaConstraints) alloc]
          initWithMandatoryConstraints:@{}
                   optionalConstraints:nil];
  RTC_OBJC_TYPE(RTCPeerConnection) *peerConnection =
      [factory peerConnectionWithConfiguration:config
                                   constraints:contraints
                                      delegate:nil];

  dispatch_semaphore_t negotiatedSem = dispatch_semaphore_create(0);
  [peerConnection addIceCandidate:[[RTC_OBJC_TYPE(RTCIceCandidate) alloc]
                                        initWithSdp:@"invalid"
                                      sdpMLineIndex:-1
                                             sdpMid:nil]
                completionHandler:^(NSError *error) {
                  ASSERT_NE(error, nil);
                  if (error != nil) {
                    dispatch_semaphore_signal(negotiatedSem);
                  }
                }];

  NSTimeInterval timeout = 5;
  ASSERT_EQ(
      0,
      dispatch_semaphore_wait(
          negotiatedSem,
          dispatch_time(DISPATCH_TIME_NOW, (int64_t)(timeout * NSEC_PER_SEC))));
  [peerConnection close];
}

// The factory owns its signaling and network threads. Releasing the last
// reference to it on one of those threads must not make the thread stop and
// join itself.
- (void)testReleaseFactoryOnSignalingThread {
  RTC_OBJC_TYPE(RTCPeerConnectionFactory) * factory;
  @autoreleasepool {
    factory = [[RTC_OBJC_TYPE(RTCPeerConnectionFactory) alloc] init];
  }
  __weak RTC_OBJC_TYPE(RTCPeerConnectionFactory) *weakFactory = factory;
  factory.signalingThread->BlockingCall([&] { factory = nil; });
  XCTAssertNil(weakFactory);
  XCTAssertTrue([RTC_OBJC_TYPE(RTCPeerConnectionFactory)
      waitForThreadTeardownForTesting:5]);
}

- (void)testReleaseFactoryOnNetworkThread {
  RTC_OBJC_TYPE(RTCPeerConnectionFactory) * factory;
  @autoreleasepool {
    factory = [[RTC_OBJC_TYPE(RTCPeerConnectionFactory) alloc] init];
  }
  __weak RTC_OBJC_TYPE(RTCPeerConnectionFactory) *weakFactory = factory;
  factory.networkThread->BlockingCall([&] { factory = nil; });
  XCTAssertNil(weakFactory);
  XCTAssertTrue([RTC_OBJC_TYPE(RTCPeerConnectionFactory)
      waitForThreadTeardownForTesting:5]);
}

// Releases a peer connection and its factory from a completion handler, which
// runs on the signaling thread of the factory.
- (void)testReleaseFactoryInSetRemoteDescriptionCompletionHandler {
  __block RTC_OBJC_TYPE(RTCPeerConnectionFactory) * callerFactory;
  __block RTC_OBJC_TYPE(RTCPeerConnection) * caller;
  RTC_OBJC_TYPE(RTCPeerConnectionFactory) * calleeFactory;
  RTC_OBJC_TYPE(RTCPeerConnection) * callee;
  @autoreleasepool {
    callerFactory = [[RTC_OBJC_TYPE(RTCPeerConnectionFactory) alloc] init];
    caller = [self peerConnectionWithFactory:callerFactory delegate:nil];
    calleeFactory = [[RTC_OBJC_TYPE(RTCPeerConnectionFactory) alloc] init];
    callee = [self peerConnectionWithFactory:calleeFactory delegate:nil];
  }
  dispatch_semaphore_t callerFactoryDeallocated = dispatch_semaphore_create(0);
  __block BOOL deallocatedOnMainThread = YES;
  objc_setAssociatedObject(
      callerFactory,
      &kDeallocSentinelKey,
      [[RTCDeallocSentinel alloc] initWithBlock:^{
        deallocatedOnMainThread = [NSThread isMainThread];
        dispatch_semaphore_signal(callerFactoryDeallocated);
      }],
      OBJC_ASSOCIATION_RETAIN_NONATOMIC);

  __block NSError *negotiationError;
  [self negotiateWithCaller:caller
                     callee:callee
          completionHandler:^(NSError *error) {
            negotiationError = error;
            caller = nil;
            callerFactory = nil;
          }];

  XCTAssertEqual(
      dispatch_semaphore_wait(callerFactoryDeallocated, TimeoutFromNow()), 0);
  XCTAssertNil(negotiationError);
  XCTAssertFalse(deallocatedOnMainThread);
  XCTAssertTrue([RTC_OBJC_TYPE(RTCPeerConnectionFactory)
      waitForThreadTeardownForTesting:5]);
  [callee close];
}

// Releases a peer connection from its setRemoteDescription completion handler,
// which runs on the signaling thread, while the factory stays alive. The native
// PeerConnection is destroyed from a separate task.
- (void)testReleaseInSetRemoteDescriptionCompletionHandler {
  RTC_OBJC_TYPE(RTCPeerConnectionFactory) * callerFactory;
  __block RTC_OBJC_TYPE(RTCPeerConnection) * caller;
  RTC_OBJC_TYPE(RTCPeerConnectionFactory) * calleeFactory;
  RTC_OBJC_TYPE(RTCPeerConnection) * callee;
  // Only accessed on the signaling thread of the caller.
  __block BOOL nativeCallerDestroyed = NO;
  dispatch_semaphore_t nativeCallerDestroyedSemaphore =
      dispatch_semaphore_create(0);
  @autoreleasepool {
    callerFactory = [[RTC_OBJC_TYPE(RTCPeerConnectionFactory) alloc] init];
    caller =
        [self peerConnectionWithFactory:callerFactory
                               delegate:nil
                   nativeDestroyedBlock:^{
                     nativeCallerDestroyed = YES;
                     dispatch_semaphore_signal(nativeCallerDestroyedSemaphore);
                   }];
    calleeFactory = [[RTC_OBJC_TYPE(RTCPeerConnectionFactory) alloc] init];
    callee = [self peerConnectionWithFactory:calleeFactory delegate:nil];
  }
  dispatch_semaphore_t callerDeallocated = dispatch_semaphore_create(0);
  __block BOOL deallocatedOnMainThread = YES;
  objc_setAssociatedObject(caller,
                           &kDeallocSentinelKey,
                           [[RTCDeallocSentinel alloc] initWithBlock:^{
                             deallocatedOnMainThread = [NSThread isMainThread];
                             dispatch_semaphore_signal(callerDeallocated);
                           }],
                           OBJC_ASSOCIATION_RETAIN_NONATOMIC);

  __block NSError *negotiationError;
  __block BOOL nativeCallerDestroyedSynchronously = YES;
  [self negotiateWithCaller:caller
                     callee:callee
          completionHandler:^(NSError *error) {
            negotiationError = error;
            caller = nil;
            nativeCallerDestroyedSynchronously = nativeCallerDestroyed;
          }];

  dispatch_time_t timeout = TimeoutFromNow();
  XCTAssertEqual(dispatch_semaphore_wait(callerDeallocated, timeout), 0);
  XCTAssertFalse(deallocatedOnMainThread);
  XCTAssertEqual(
      dispatch_semaphore_wait(nativeCallerDestroyedSemaphore, timeout), 0);
  XCTAssertNil(negotiationError);
  XCTAssertFalse(nativeCallerDestroyedSynchronously);
  // The factory is still usable.
  RTC_OBJC_TYPE(RTCPeerConnection) *peerConnection =
      [self peerConnectionWithFactory:callerFactory delegate:nil];
  XCTAssertNotNil(peerConnection);
  [peerConnection close];
  [callee close];
}

// Releases a peer connection from a delegate callback, which is called on the
// signaling thread while the native PeerConnection applies the remote answer.
// The native PeerConnection completes the operation before it is destroyed
// from a separate task.
- (void)testReleaseInDelegateCallback {
  RTCReleasingPeerConnectionDelegate *delegate =
      [[RTCReleasingPeerConnectionDelegate alloc] init];
  delegate.releaseState = RTCSignalingStateStable;
  RTC_OBJC_TYPE(RTCPeerConnectionFactory) * callerFactory;
  RTC_OBJC_TYPE(RTCPeerConnectionFactory) * calleeFactory;
  __weak RTC_OBJC_TYPE(RTCPeerConnection) * weakCaller;
  RTC_OBJC_TYPE(RTCPeerConnection) * callee;
  dispatch_semaphore_t callerDeallocated = dispatch_semaphore_create(0);
  __block BOOL deallocatedOnMainThread = YES;
  // Only accessed on the signaling thread of the caller.
  __block BOOL nativeCallerDestroyed = NO;
  dispatch_semaphore_t nativeCallerDestroyedSemaphore =
      dispatch_semaphore_create(0);
  @autoreleasepool {
    callerFactory = [[RTC_OBJC_TYPE(RTCPeerConnectionFactory) alloc] init];
    RTC_OBJC_TYPE(RTCPeerConnection) *caller =
        [self peerConnectionWithFactory:callerFactory
                               delegate:delegate
                   nativeDestroyedBlock:^{
                     nativeCallerDestroyed = YES;
                     dispatch_semaphore_signal(nativeCallerDestroyedSemaphore);
                   }];
    objc_setAssociatedObject(caller,
                             &kDeallocSentinelKey,
                             [[RTCDeallocSentinel alloc] initWithBlock:^{
                               deallocatedOnMainThread =
                                   [NSThread isMainThread];
                               dispatch_semaphore_signal(callerDeallocated);
                             }],
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    delegate.peerConnection = caller;
    weakCaller = caller;
    calleeFactory = [[RTC_OBJC_TYPE(RTCPeerConnectionFactory) alloc] init];
    callee = [self peerConnectionWithFactory:calleeFactory delegate:nil];
  }

  dispatch_semaphore_t negotiated = dispatch_semaphore_create(0);
  __block NSError *negotiationError;
  __block BOOL nativeCallerDestroyedBeforeCompletion = YES;
  [self negotiateWithCaller:weakCaller
                     callee:callee
          completionHandler:^(NSError *error) {
            negotiationError = error;
            nativeCallerDestroyedBeforeCompletion = nativeCallerDestroyed;
            dispatch_semaphore_signal(negotiated);
          }];

  dispatch_time_t timeout = TimeoutFromNow();
  XCTAssertEqual(dispatch_semaphore_wait(callerDeallocated, timeout), 0);
  XCTAssertFalse(deallocatedOnMainThread);
  XCTAssertEqual(dispatch_semaphore_wait(negotiated, timeout), 0);
  XCTAssertNil(negotiationError);
  XCTAssertFalse(nativeCallerDestroyedBeforeCompletion);
  XCTAssertEqual(
      dispatch_semaphore_wait(nativeCallerDestroyedSemaphore, timeout), 0);
  [callee close];
}

// Releases a peer connection on the network thread, e.g. from a certificate
// verifier. The native PeerConnection blocks on the network thread while it is
// destroyed, so it must not be destroyed synchronously there.
- (void)testReleaseOnNetworkThread {
  RTC_OBJC_TYPE(RTCPeerConnectionFactory) *factory =
      [[RTC_OBJC_TYPE(RTCPeerConnectionFactory) alloc] init];
  RTC_OBJC_TYPE(RTCPeerConnection) * peerConnection;
  dispatch_semaphore_t nativeDestroyed = dispatch_semaphore_create(0);
  @autoreleasepool {
    peerConnection = [self peerConnectionWithFactory:factory
                                            delegate:nil
                                nativeDestroyedBlock:^{
                                  dispatch_semaphore_signal(nativeDestroyed);
                                }];
  }
  XCTAssertNotNil(peerConnection);
  factory.networkThread->BlockingCall([&] { peerConnection = nil; });
  XCTAssertEqual(dispatch_semaphore_wait(nativeDestroyed, TimeoutFromNow()), 0);
}

#pragma mark - Helpers

- (RTC_OBJC_TYPE(RTCPeerConnection) *)
    peerConnectionWithFactory:(RTC_OBJC_TYPE(RTCPeerConnectionFactory) *)factory
                     delegate:
                         (nullable id<RTC_OBJC_TYPE(RTCPeerConnectionDelegate)>)
                             delegate {
  return [self peerConnectionWithFactory:factory
                                delegate:delegate
                    nativeDestroyedBlock:nil];
}

// If `nativeDestroyedBlock` isn't nil, it is called when the native
// PeerConnection is destroyed, which deallocates the certificate verifier that
// the native PeerConnection owns. Create the peer connection in an autorelease
// pool, so that no other reference keeps the verifier alive.
- (RTC_OBJC_TYPE(RTCPeerConnection) *)
    peerConnectionWithFactory:(RTC_OBJC_TYPE(RTCPeerConnectionFactory) *)factory
                     delegate:
                         (nullable id<RTC_OBJC_TYPE(RTCPeerConnectionDelegate)>)
                             delegate
         nativeDestroyedBlock:(nullable void (^)(void))nativeDestroyedBlock {
  RTC_OBJC_TYPE(RTCConfiguration) *config =
      [[RTC_OBJC_TYPE(RTCConfiguration) alloc] init];
  config.sdpSemantics = RTCSdpSemanticsUnifiedPlan;
  RTC_OBJC_TYPE(RTCMediaConstraints) *constraints =
      [[RTC_OBJC_TYPE(RTCMediaConstraints) alloc]
          initWithMandatoryConstraints:@{}
                   optionalConstraints:nil];
  RTCAcceptingCertificateVerifier *verifier =
      [[RTCAcceptingCertificateVerifier alloc] init];
  if (nativeDestroyedBlock != nil) {
    objc_setAssociatedObject(
        verifier,
        &kDeallocSentinelKey,
        [[RTCDeallocSentinel alloc] initWithBlock:nativeDestroyedBlock],
        OBJC_ASSOCIATION_RETAIN_NONATOMIC);
  }
  return [factory peerConnectionWithConfiguration:config
                                      constraints:constraints
                              certificateVerifier:verifier
                                         delegate:delegate];
}

// Negotiates a session between `caller` and `callee` and calls
// `completionHandler` when setRemoteDescription completes on the caller, or
// with the error of the first step that fails. The blocks only hold weak
// references to the peer connections, so that `completionHandler` can release
// the last reference to the caller.
- (void)negotiateWithCaller:(RTC_OBJC_TYPE(RTCPeerConnection) *)caller
                     callee:(RTC_OBJC_TYPE(RTCPeerConnection) *)callee
          completionHandler:
              (RTCSetSessionDescriptionCompletionHandler)completionHandler {
  if (caller == nil || callee == nil) {
    completionHandler(DeallocatedPeerConnectionError());
    return;
  }
  __weak RTC_OBJC_TYPE(RTCPeerConnection) *weakCaller = caller;
  __weak RTC_OBJC_TYPE(RTCPeerConnection) *weakCallee = callee;
  RTC_OBJC_TYPE(RTCMediaConstraints) *constraints =
      [[RTC_OBJC_TYPE(RTCMediaConstraints)
          alloc] initWithMandatoryConstraints:@{
        kRTCMediaConstraintsOfferToReceiveAudio : kRTCMediaConstraintsValueTrue
      }
                          optionalConstraints:nil];
  [caller
      offerForConstraints:constraints
        completionHandler:^(RTC_OBJC_TYPE(RTCSessionDescription) * offer,
                            NSError * offerError) {
          if (offerError != nil) {
            completionHandler(offerError);
            return;
          }
          RTC_OBJC_TYPE(RTCPeerConnection) *callerForSld = weakCaller;
          if (callerForSld == nil) {
            completionHandler(DeallocatedPeerConnectionError());
            return;
          }
          [callerForSld
              setLocalDescription:offer
                completionHandler:^(NSError *sldError) {
                  if (sldError != nil) {
                    completionHandler(sldError);
                    return;
                  }
                  RTC_OBJC_TYPE(RTCPeerConnection) *calleeForSrd = weakCallee;
                  if (calleeForSrd == nil) {
                    completionHandler(DeallocatedPeerConnectionError());
                    return;
                  }
                  [calleeForSrd
                      setRemoteDescription:offer
                         completionHandler:^(NSError *srdError) {
                           if (srdError != nil) {
                             completionHandler(srdError);
                             return;
                           }
                           RTC_OBJC_TYPE(RTCPeerConnection) *calleeForAnswer =
                               weakCallee;
                           if (calleeForAnswer == nil) {
                             completionHandler(
                                 DeallocatedPeerConnectionError());
                             return;
                           }
                           [calleeForAnswer
                               answerForConstraints:constraints
                                  completionHandler:^(
                                      RTC_OBJC_TYPE(RTCSessionDescription) *
                                          answer,
                                      NSError * answerError) {
                                    if (answerError != nil) {
                                      completionHandler(answerError);
                                      return;
                                    }
                                    RTC_OBJC_TYPE(RTCPeerConnection)
                                    *calleeForSld = weakCallee;
                                    if (calleeForSld == nil) {
                                      completionHandler(
                                          DeallocatedPeerConnectionError());
                                      return;
                                    }
                                    [calleeForSld
                                        setLocalDescription:answer
                                          completionHandler:^(
                                              NSError *sldAnswerError) {
                                            if (sldAnswerError != nil) {
                                              completionHandler(sldAnswerError);
                                              return;
                                            }
                                            RTC_OBJC_TYPE(RTCPeerConnection)
                                            *callerForSrd = weakCaller;
                                            if (callerForSrd == nil) {
                                              completionHandler(
                                                  DeallocatedPeerConnectionError());
                                              return;
                                            }
                                            [callerForSrd
                                                setRemoteDescription:answer
                                                   completionHandler:
                                                       completionHandler];
                                          }];
                                  }];
                         }];
                }];
        }];
}

@end
