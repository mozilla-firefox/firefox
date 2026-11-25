/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#import <AVFoundation/AVFoundation.h>
#import <QuartzCore/QuartzCore.h>

#include "CFTypeRefPtr.h"
#include "gtest/gtest.h"
#include "mozilla/Preferences.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/TypedEnumBits.h"
#include "mozilla/gfx/MacIOSurface.h"
#include "mozilla/layers/NativeLayerCA.h"

using namespace mozilla;
using namespace mozilla::gfx;
using namespace mozilla::layers;

static const IntSize kVideoSize(64, 32);

// Properties of a presented frame.
enum class PresentFlags : uint8_t {
  None = 0,
  DRM = 1 << 0,
  HDR = 1 << 1,
};
MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS(PresentFlags)

// A single surface-presentation layer committed into a detached root CALayer,
// either as the onscreen tree or as the offscreen tree used for snapshots.
class NativeLayerCAVideo : public ::testing::TestWithParam<bool> {
 protected:
  void SetUp() override {
    mRootCALayer = [[CALayer layer] retain];
    mOffscreenRootCALayer = [[CALayer layer] retain];
    mRoot = NativeLayerRootCA::CreateForCALayer(mRootCALayer);
    RefPtr<NativeLayer> layer =
        mRoot->CreateLayerForSurfacePresentation(kVideoSize, true);
    ASSERT_TRUE(layer);
    mLayer = layer->AsNativeLayerCA();
    ASSERT_TRUE(mLayer);
    mLayer->SetDisplayRect(IntRect(IntPoint(), kVideoSize));
    mRoot->AppendLayer(mLayer);
  }

  void TearDown() override {
    mRoot->RemoveLayer(mLayer);
    mLayer = nullptr;
    mRoot = nullptr;
    [mOffscreenRootCALayer release];
    [mRootCALayer release];
  }

  bool IsOffscreen() const { return GetParam(); }

  void Present(const RefPtr<MacIOSurface>& aSurface, PresentFlags aFlags) {
    ASSERT_TRUE(aSurface);
    IntSize size = kVideoSize;
    mLayer->SetSurfaceToPresent(aSurface->GetIOSurfaceRef(), size,
                                bool(aFlags & PresentFlags::DRM),
                                bool(aFlags & PresentFlags::HDR));
    if (IsOffscreen()) {
      mRoot->CommitOffscreen(mOffscreenRootCALayer);
    } else {
      mRoot->CommitToScreen();
    }
  }

  AVSampleBufferDisplayLayer* VideoLayer() {
    return FindVideoLayer(IsOffscreen() ? mOffscreenRootCALayer : mRootCALayer);
  }

  // The layer composited directly under the video layer.
  CALayer* LayerUnderVideoLayer() {
    AVSampleBufferDisplayLayer* videoLayer = VideoLayer();
    if (!videoLayer) {
      return nil;
    }
    NSArray<CALayer*>* siblings = videoLayer.superlayer.sublayers;
    NSUInteger index = [siblings indexOfObject:videoLayer];
    return index > 0 ? siblings[index - 1] : nil;
  }

  void ExpectOpaqueBlackBacking(CALayer* aLayer) {
    ASSERT_NE(aLayer, nil);
    EXPECT_TRUE(aLayer.opaque);
    EXPECT_TRUE(CGColorEqualToColor(aLayer.backgroundColor,
                                    CGColorGetConstantColor(kCGColorBlack)));
    EXPECT_TRUE(CGRectEqualToRect(aLayer.bounds, VideoLayer().bounds));
    EXPECT_TRUE(CGPointEqualToPoint(aLayer.position, VideoLayer().position));
    EXPECT_TRUE(CATransform3DEqualToTransform(aLayer.transform,
                                              VideoLayer().transform));
  }

  static RefPtr<MacIOSurface> CreateNV12Surface() {
    return MacIOSurface::CreateBiPlanarSurface(
        kVideoSize, IntSize(kVideoSize.width / 2, kVideoSize.height / 2),
        ChromaSubsampling::HALF_WIDTH_AND_HEIGHT, YUVColorSpace::BT709,
        TransferFunction::BT709, ColorRange::LIMITED, ColorDepth::COLOR_8,
        MacIOSurface::AllowAlpha::No);
  }

  static RefPtr<MacIOSurface> CreateBGRASurface() {
    return MacIOSurface::CreateIOSurface(kVideoSize.width, kVideoSize.height,
                                         MacIOSurface::AllowAlpha::No);
  }

  static AVSampleBufferDisplayLayer* FindVideoLayer(CALayer* aLayer) {
    if ([aLayer isKindOfClass:[AVSampleBufferDisplayLayer class]]) {
      return static_cast<AVSampleBufferDisplayLayer*>(aLayer);
    }
    for (CALayer* sublayer in aLayer.sublayers) {
      if (AVSampleBufferDisplayLayer* videoLayer = FindVideoLayer(sublayer)) {
        return videoLayer;
      }
    }
    return nil;
  }

  CALayer* mRootCALayer = nil;
  CALayer* mOffscreenRootCALayer = nil;
  RefPtr<NativeLayerRootCA> mRoot;
  RefPtr<NativeLayerCA> mLayer;
};

TEST_P(NativeLayerCAVideo, DRMVideoLayerIsCaptureProtected) {
  // Present a DRM video frame; the video layer must block capture and hold
  // no frame in its contents.
  ASSERT_NO_FATAL_FAILURE(Present(CreateNV12Surface(), PresentFlags::DRM));
  ASSERT_NE(VideoLayer(), nil);
  EXPECT_TRUE(VideoLayer().preventsCapture);
  EXPECT_EQ(VideoLayer().contents, nil);
}

TEST_P(NativeLayerCAVideo, RebuiltDRMVideoLayerIsCaptureProtected) {
  // Present a DRM video frame; DRM video always uses a video layer, so one is
  // built.
  ASSERT_NO_FATAL_FAILURE(Present(CreateNV12Surface(), PresentFlags::DRM));
  ASSERT_NE(VideoLayer(), nil);

  // Present a non-video surface with DRM still set, so the video layer is torn
  // down by the layer type changing alone, without a DRM state change.
  ASSERT_NO_FATAL_FAILURE(Present(CreateBGRASurface(), PresentFlags::DRM));
  ASSERT_EQ(VideoLayer(), nil);

  // Present a DRM video frame again; the rebuilt video layer must block
  // capture and hold no frame in its contents.
  ASSERT_NO_FATAL_FAILURE(Present(CreateNV12Surface(), PresentFlags::DRM));
  ASSERT_NE(VideoLayer(), nil);
  EXPECT_TRUE(VideoLayer().preventsCapture);
  EXPECT_EQ(VideoLayer().contents, nil);
}

TEST_P(NativeLayerCAVideo, NonDRMVideoLayerShowsFirstFrame) {
  // Present a non-DRM HDR frame, which also uses a video layer; the layer
  // must show the first frame through its contents and allow capture.
  ASSERT_NO_FATAL_FAILURE(Present(CreateNV12Surface(), PresentFlags::HDR));
  ASSERT_NE(VideoLayer(), nil);
  EXPECT_FALSE(VideoLayer().preventsCapture);
  EXPECT_NE(VideoLayer().contents, nil);
}

TEST_P(NativeLayerCAVideo, SwitchingToDRMDropsNonDRMFrame) {
  // Present a non-DRM HDR frame; the video layer shows it through its
  // contents.
  ASSERT_NO_FATAL_FAILURE(Present(CreateNV12Surface(), PresentFlags::HDR));
  ASSERT_NE(VideoLayer(), nil);
  ASSERT_NE(VideoLayer().contents, nil);

  // Present a DRM HDR frame on the same layer; the video layer must block
  // capture and hold no frame in its contents.
  ASSERT_NO_FATAL_FAILURE(
      Present(CreateNV12Surface(), PresentFlags::DRM | PresentFlags::HDR));
  ASSERT_NE(VideoLayer(), nil);
  EXPECT_TRUE(VideoLayer().preventsCapture);
  EXPECT_EQ(VideoLayer().contents, nil);
}

TEST_P(NativeLayerCAVideo, SwitchingFromDRMUsesNewLayer) {
  // Present a DRM HDR frame; the video layer blocks capture. Keep it alive to
  // inspect it after it is replaced.
  ASSERT_NO_FATAL_FAILURE(
      Present(CreateNV12Surface(), PresentFlags::DRM | PresentFlags::HDR));
  AVSampleBufferDisplayLayer* drmLayer = [VideoLayer() retain];
  auto releaseDRMLayer = MakeScopeExit([&] { [drmLayer release]; });
  ASSERT_NE(drmLayer, nil);
  ASSERT_TRUE(drmLayer.preventsCapture);

  // Present a non-DRM HDR frame on the same layer; it must go to a new video
  // layer that shows its first frame, while the layer that held DRM frames
  // stays capture-protected.
  ASSERT_NO_FATAL_FAILURE(Present(CreateNV12Surface(), PresentFlags::HDR));
  ASSERT_NE(VideoLayer(), nil);
  EXPECT_NE(VideoLayer(), drmLayer);
  EXPECT_FALSE(VideoLayer().preventsCapture);
  EXPECT_NE(VideoLayer().contents, nil);
  EXPECT_TRUE(drmLayer.preventsCapture);
}

TEST_P(NativeLayerCAVideo, DRMVideoLayerHasOpaqueBlackBacking) {
  // Present a DRM video frame on a scaled layer; an opaque black layer must
  // sit directly under the video layer, covering the same area, so that a
  // composition which leaves out the capture-protected video layer shows
  // black there.
  mLayer->SetTransform(Matrix4x4::Scaling(2, 3, 1));
  ASSERT_NO_FATAL_FAILURE(Present(CreateNV12Surface(), PresentFlags::DRM));
  ASSERT_NE(VideoLayer(), nil);
  ASSERT_FALSE(CATransform3DIsIdentity(VideoLayer().transform));
  ExpectOpaqueBlackBacking(LayerUnderVideoLayer());
}

TEST_P(NativeLayerCAVideo, BackingFollowsDRMChanges) {
  // Present a DRM frame, then a non-DRM one on the same layer; the backing
  // goes away with the DRM video layer it was under.
  ASSERT_NO_FATAL_FAILURE(Present(CreateNV12Surface(), PresentFlags::DRM));
  ASSERT_NO_FATAL_FAILURE(ExpectOpaqueBlackBacking(LayerUnderVideoLayer()));
  ASSERT_NO_FATAL_FAILURE(Present(CreateNV12Surface(), PresentFlags::HDR));
  ASSERT_NE(VideoLayer(), nil);
  EXPECT_EQ(LayerUnderVideoLayer(), nil);

  // Present a DRM frame again; the new video layer gets exactly one backing.
  ASSERT_NO_FATAL_FAILURE(Present(CreateNV12Surface(), PresentFlags::DRM));
  ASSERT_NO_FATAL_FAILURE(ExpectOpaqueBlackBacking(LayerUnderVideoLayer()));
  EXPECT_EQ(VideoLayer().superlayer.sublayers.count, 2u);
}

TEST_P(NativeLayerCAVideo, NonDRMVideoLayerHasNoBacking) {
  // Present a non-DRM HDR frame; the video layer is the bottom layer of its
  // parent, with nothing composited under it.
  ASSERT_NO_FATAL_FAILURE(Present(CreateNV12Surface(), PresentFlags::HDR));
  ASSERT_NE(VideoLayer(), nil);
  EXPECT_EQ(LayerUnderVideoLayer(), nil);
}

INSTANTIATE_TEST_SUITE_P(, NativeLayerCAVideo, ::testing::Bool(),
                         [](const ::testing::TestParamInfo<bool>& aInfo) {
                           return aInfo.param ? "Offscreen" : "Onscreen";
                         });

static const DeviceColor kBackdropColor(0.25f, 0.5f, 0.75f, 1.0f);

// A color layer committed into a detached root CALayer, either as the onscreen
// tree or as the offscreen tree used for snapshots.
class NativeLayerCAColor : public ::testing::TestWithParam<bool> {
 protected:
  void SetUp() override {
    mOriginalNativeSRGB =
        Preferences::GetBool("gfx.color_management.native_srgb");
    mRootCALayer = [[CALayer layer] retain];
    mOffscreenRootCALayer = [[CALayer layer] retain];
    mRoot = NativeLayerRootCA::CreateForCALayer(mRootCALayer);
  }

  void TearDown() override {
    if (mLayer) {
      mRoot->RemoveLayer(mLayer);
      mLayer = nullptr;
    }
    mRoot = nullptr;
    [mOffscreenRootCALayer release];
    [mRootCALayer release];
    Preferences::SetBool("gfx.color_management.native_srgb",
                         mOriginalNativeSRGB);
  }

  bool IsOffscreen() const { return GetParam(); }

  // Returns the background color of the committed color layer.
  CGColorRef CommitColorLayer(bool aNativeSRGB) {
    Preferences::SetBool("gfx.color_management.native_srgb", aNativeSRGB);
    RefPtr<NativeLayer> layer = mRoot->CreateLayerForColor(kBackdropColor);
    mLayer = layer->AsNativeLayerCA();
    mLayer->SetDisplayRect(IntRect(0, 0, 16, 16));
    mRoot->AppendLayer(mLayer);
    if (IsOffscreen()) {
      mRoot->CommitOffscreen(mOffscreenRootCALayer);
      return FindBackgroundColor(mOffscreenRootCALayer);
    }
    mRoot->CommitToScreen();
    return FindBackgroundColor(mRootCALayer);
  }

  static CGColorRef FindBackgroundColor(CALayer* aLayer) {
    if (aLayer.backgroundColor) {
      return aLayer.backgroundColor;
    }
    for (CALayer* sublayer in aLayer.sublayers) {
      if (CGColorRef color = FindBackgroundColor(sublayer)) {
        return color;
      }
    }
    return nullptr;
  }

  static void ExpectBackdropComponents(CGColorRef aColor) {
    ASSERT_EQ(CGColorGetNumberOfComponents(aColor), 4u);
    const CGFloat* components = CGColorGetComponents(aColor);
    EXPECT_FLOAT_EQ(components[0], kBackdropColor.r);
    EXPECT_FLOAT_EQ(components[1], kBackdropColor.g);
    EXPECT_FLOAT_EQ(components[2], kBackdropColor.b);
    EXPECT_FLOAT_EQ(components[3], kBackdropColor.a);
  }

  bool mOriginalNativeSRGB = false;
  CALayer* mRootCALayer = nil;
  CALayer* mOffscreenRootCALayer = nil;
  RefPtr<NativeLayerRootCA> mRoot;
  RefPtr<NativeLayerCA> mLayer;
};

TEST_P(NativeLayerCAColor, BackdropIsDeviceRGBWithoutNativeSRGB) {
  // Our IOSurfaces are untagged when native_srgb is off, so backdrop colors
  // must be untagged too (DeviceRGB) or Core Animation converts them and they
  // no longer match the tiles next to them.
  CGColorRef color = CommitColorLayer(false);
  ASSERT_NE(color, nullptr);
  CFTypeRefPtr<CGColorSpaceRef> deviceRGB =
      CFTypeRefPtr<CGColorSpaceRef>::WrapUnderCreateRule(
          CGColorSpaceCreateDeviceRGB());
  EXPECT_TRUE(CFEqual(CGColorGetColorSpace(color), deviceRGB.get()));
  ExpectBackdropComponents(color);
}

TEST_P(NativeLayerCAColor, BackdropIsSRGBWithNativeSRGB) {
  CGColorRef color = CommitColorLayer(true);
  ASSERT_NE(color, nullptr);
  CFTypeRefPtr<CFStringRef> name =
      CFTypeRefPtr<CFStringRef>::WrapUnderCreateRule(
          CGColorSpaceCopyName(CGColorGetColorSpace(color)));
  ASSERT_TRUE(name);
  EXPECT_TRUE(CFEqual(name.get(), kCGColorSpaceSRGB));
  ExpectBackdropComponents(color);
}

INSTANTIATE_TEST_SUITE_P(, NativeLayerCAColor, ::testing::Bool(),
                         [](const ::testing::TestParamInfo<bool>& aInfo) {
                           return aInfo.param ? "Offscreen" : "Onscreen";
                         });
