/*
 *  Copyright 2015 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#import "RTCCameraPreviewView.h"

#import <AVFoundation/AVFoundation.h>
#import <UIKit/UIKit.h>

#import "RTCDispatcher+Private.h"

@implementation RTC_OBJC_TYPE (RTCCameraPreviewView)

@synthesize captureSession = _captureSession;

+ (Class)layerClass {
  return [AVCaptureVideoPreviewLayer class];
}

- (instancetype)initWithFrame:(CGRect)aRect {
  self = [super initWithFrame:aRect];
  if (self) {
    [self addOrientationObserver];
  }
  return self;
}

- (instancetype)initWithCoder:(NSCoder*)aDecoder {
  self = [super initWithCoder:aDecoder];
  if (self) {
    [self addOrientationObserver];
  }
  return self;
}

- (void)dealloc {
  [self removeOrientationObserver];
}

- (void)setCaptureSession:(AVCaptureSession *)captureSession {
  if (_captureSession == captureSession) {
    return;
  }
  _captureSession = captureSession;
  [RTC_OBJC_TYPE(RTCDispatcher)
      dispatchAsyncOnType:RTCDispatcherTypeMain
                    block:^{
                      AVCaptureVideoPreviewLayer *previewLayer = [self previewLayer];
                      [RTC_OBJC_TYPE(RTCDispatcher)
                          dispatchAsyncOnType:RTCDispatcherTypeCaptureSession
                                        block:^{
                                          previewLayer.session = captureSession;
                                          [RTC_OBJC_TYPE(RTCDispatcher)
                                              dispatchAsyncOnType:RTCDispatcherTypeMain
                                                            block:^{
                                                              [self setCorrectVideoOrientation];
                                                            }];
                                        }];
                    }];
}

- (void)layoutSubviews {
  [super layoutSubviews];

  // Update the video orientation based on the device orientation.
  [self setCorrectVideoOrientation];
}

-(void)orientationChanged:(NSNotification *)notification {
  [self setCorrectVideoOrientation];
}

- (void)setCorrectVideoOrientation {
  // Get current device orientation.
  UIDeviceOrientation deviceOrientation = [UIDevice currentDevice].orientation;
  AVCaptureVideoPreviewLayer *previewLayer = [self previewLayer];

  // First check if we are allowed to set the video orientation.
  if (previewLayer.connection.isVideoOrientationSupported) {
    // Set the video orientation based on device orientation.
    if (deviceOrientation == UIDeviceOrientationPortraitUpsideDown) {
      previewLayer.connection.videoOrientation =
          AVCaptureVideoOrientationPortraitUpsideDown;
    } else if (deviceOrientation == UIDeviceOrientationLandscapeRight) {
      previewLayer.connection.videoOrientation =
          AVCaptureVideoOrientationLandscapeRight;
    } else if (deviceOrientation == UIDeviceOrientationLandscapeLeft) {
      previewLayer.connection.videoOrientation =
          AVCaptureVideoOrientationLandscapeLeft;
    } else if (deviceOrientation == UIDeviceOrientationPortrait) {
      previewLayer.connection.videoOrientation =
          AVCaptureVideoOrientationPortrait;
    }
    // If device orientation switches to FaceUp or FaceDown, don't change video orientation.
  }
}

#pragma mark - Private

- (void)addOrientationObserver {
  [[NSNotificationCenter defaultCenter] addObserver:self
                                            selector:@selector(orientationChanged:)
                                                name:UIDeviceOrientationDidChangeNotification
                                              object:nil];
}

- (void)removeOrientationObserver {
  [[NSNotificationCenter defaultCenter] removeObserver:self
                                                  name:UIDeviceOrientationDidChangeNotification
                                                object:nil];
}

- (AVCaptureVideoPreviewLayer *)previewLayer {
  return (AVCaptureVideoPreviewLayer *)self.layer;
}

@end
