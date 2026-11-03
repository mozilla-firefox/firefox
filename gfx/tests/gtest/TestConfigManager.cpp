/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "gfxFeature.h"
#include "gtest/gtest.h"
#include "mozilla/gfx/gfxConfigManager.h"
#include "nsIGfxInfo.h"

using namespace mozilla;
using namespace mozilla::gfx;

namespace mozilla {
namespace gfx {

class MockGfxInfo final : public nsIGfxInfo {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS

  int32_t mStatusWr = nsIGfxInfo::FEATURE_STATUS_OK;
  int32_t mStatusWrCompositor = nsIGfxInfo::FEATURE_STATUS_OK;
  int32_t mStatusWrShaderCache = nsIGfxInfo::FEATURE_STATUS_OK;
  int32_t mStatusWrOptimizedShaders = nsIGfxInfo::FEATURE_STATUS_OK;
  int32_t mStatusWrPartialPresent = nsIGfxInfo::FEATURE_STATUS_OK;
  int32_t mStatusWrScissoredCacheClears = nsIGfxInfo::FEATURE_STATUS_OK;
  int32_t mStatusWrDComp = nsIGfxInfo::FEATURE_STATUS_OK;
  int32_t mStatusGLNorm16Textures = nsIGfxInfo::FEATURE_STATUS_OK;
  int32_t mStatusMetalAngle = nsIGfxInfo::FEATURE_STATUS_OK;
  int32_t mStatusGlAngle = nsIGfxInfo::FEATURE_STATUS_OK;
  Maybe<bool> mHasBattery = Some(false);
  const char* mVendorId = "0x10de";
  const char* mDeviceId = "";

  // Default allows WebRender + compositor, and is desktop NVIDIA.
  MockGfxInfo() = default;

  NS_IMETHOD GetFeatureStatus(int32_t aFeature, nsACString& aFailureId,
                              int32_t* _retval) override {
    switch (aFeature) {
      case nsIGfxInfo::FEATURE_WEBRENDER:
        *_retval = mStatusWr;
        break;
      case nsIGfxInfo::FEATURE_WEBRENDER_COMPOSITOR:
        *_retval = mStatusWrCompositor;
        break;
      case nsIGfxInfo::FEATURE_WEBRENDER_SHADER_CACHE:
        *_retval = mStatusWrShaderCache;
        break;
      case nsIGfxInfo::FEATURE_WEBRENDER_OPTIMIZED_SHADERS:
        *_retval = mStatusWrOptimizedShaders;
        break;
      case nsIGfxInfo::FEATURE_WEBRENDER_PARTIAL_PRESENT:
        *_retval = mStatusWrPartialPresent;
        break;
      case nsIGfxInfo::FEATURE_WEBRENDER_SCISSORED_CACHE_CLEARS:
        *_retval = mStatusWrScissoredCacheClears;
        break;
      case nsIGfxInfo::FEATURE_WEBRENDER_DCOMP:
        *_retval = mStatusWrDComp;
        break;
      case nsIGfxInfo::FEATURE_GL_NORM16_TEXTURES:
        *_retval = mStatusGLNorm16Textures;
        break;
      case nsIGfxInfo::FEATURE_WEBRENDER_ANGLE_METAL:
        *_retval = mStatusMetalAngle;
        break;
      case nsIGfxInfo::FEATURE_WEBRENDER_ANGLE_GL:
        *_retval = mStatusGlAngle;
        break;
      default:
        return NS_ERROR_NOT_IMPLEMENTED;
    }
    return NS_OK;
  }

  NS_IMETHOD GetHasBattery(bool* aHasBattery) override {
    if (mHasBattery.isNothing()) {
      return NS_ERROR_NOT_IMPLEMENTED;
    }
    *aHasBattery = *mHasBattery;
    return NS_OK;
  }

  NS_IMETHOD GetAdapterVendorID(nsAString& aAdapterVendorID) override {
    if (!mVendorId) {
      return NS_ERROR_NOT_IMPLEMENTED;
    }
    aAdapterVendorID.AssignASCII(mVendorId);
    return NS_OK;
  }

  NS_IMETHOD GetAdapterDeviceID(nsAString& aAdapterDeviceID) override {
    if (!mDeviceId) {
      return NS_ERROR_NOT_IMPLEMENTED;
    }
    aAdapterDeviceID.AssignASCII(mDeviceId);
    return NS_OK;
  }

  NS_IMETHOD GetTextScaleFactor(float* aTextScaleFactor) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  NS_IMETHOD GetAzureCanvasBackend(nsAString& aBackend) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  NS_IMETHOD GetAzureContentBackend(nsAString& aBackend) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  // The following methods we don't need for testing gfxConfigManager.
  NS_IMETHOD GetFeatureStatusStr(const nsAString& aFeature,
                                 nsACString& aFailureId,
                                 nsAString& _retval) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  NS_IMETHOD GetFeatureSuggestedDriverVersionStr(const nsAString& aFeature,
                                                 nsAString& _retval) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  NS_IMETHOD GetFeatureSuggestedDriverVersion(int32_t aFeature,
                                              nsAString& _retval) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  NS_IMETHOD GetMonitors(JSContext* cx,
                         JS::MutableHandle<JS::Value> _retval) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD GetFailures(nsTArray<int32_t>& indices,
                         nsTArray<nsCString>& failures) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD_(void) LogFailure(const nsACString& failure) override {}
  NS_IMETHOD GetInfo(JSContext*, JS::MutableHandle<JS::Value>) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD GetFeatures(JSContext*, JS::MutableHandle<JS::Value>) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD GetFeatureLog(JSContext*, JS::MutableHandle<JS::Value>) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD GetActiveCrashGuards(JSContext*,
                                  JS::MutableHandle<JS::Value>) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD GetFontVisibilityDetermination(
      nsIGfxInfo::FontVisibilityDeviceDetermination*
          aFontVisibilityDetermination) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD GetFontVisibilityDeterminationStr(
      nsAString& aFontVisibilityDeterminationStr) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD GetContentBackend(nsAString& aContentBackend) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD GetUsingGPUProcess(bool* aOutValue) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD GetUsingAcceleratedCanvas(bool* aOutValue) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD GetIsHeadless(bool* aIsHeadless) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD GetTargetFrameRate(uint32_t* aTargetFrameRate) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD GetCodecSupportInfo(nsACString& aCodecSupportInfo) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD_(void)
  SetCodecSupportInfo(const nsACString& aCodecSupportInfo) override {}
  NS_IMETHOD GetDWriteEnabled(bool* aDWriteEnabled) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD GetDWriteVersion(nsAString& aDwriteVersion) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD GetCleartypeParameters(nsAString& aCleartypeParams) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD GetWindowProtocol(nsAString& aWindowProtocol) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD GetTestType(nsAString& aTestType) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD GetAdapterDescription(nsAString& aAdapterDescription) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD GetAdapterDriver(nsAString& aAdapterDriver) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD GetAdapterSubsysID(nsAString& aAdapterSubsysID) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD GetAdapterRAM(uint32_t* aAdapterRAM) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD GetAdapterDriverVendor(nsAString& aAdapterDriverVendor) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD GetAdapterDriverVersion(
      nsAString& aAdapterDriverVersion) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD GetAdapterDriverDate(nsAString& aAdapterDriverDate) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD GetAdapterDescription2(nsAString& aAdapterDescription) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD GetAdapterDriver2(nsAString& aAdapterDriver) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD GetAdapterVendorID2(nsAString& aAdapterVendorID) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD GetAdapterDeviceID2(nsAString& aAdapterDeviceID) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD GetAdapterSubsysID2(nsAString& aAdapterSubsysID) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD GetAdapterRAM2(uint32_t* aAdapterRAM) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD GetAdapterDriverVendor2(nsAString& aAdapterDriverVendor) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD GetAdapterDriverVersion2(
      nsAString& aAdapterDriverVersion) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD GetAdapterDriverDate2(nsAString& aAdapterDriverDate) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD GetIsGPU2Active(bool* aIsGPU2Active) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD GetDrmRenderDevice(nsACString& aDrmRenderDevice) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD ControlGPUProcessForXPCShell(bool aEnable,
                                          bool* _retval) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD KillGPUProcessForTests() override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD CrashGPUProcessForTests() override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  NS_IMETHOD_(void) GetData() override {}

 private:
  virtual ~MockGfxInfo() = default;
};

NS_IMPL_ISUPPORTS(MockGfxInfo, nsIGfxInfo)

class GfxConfigManager : public ::testing::Test, public gfxConfigManager {
 public:
  GfxConfigManager() {
    mFeatureWr = &mFeatures.mWr;
    mFeatureWrCompositor = &mFeatures.mWrCompositor;
    mFeatureWrAngle = &mFeatures.mWrAngle;
    mFeatureWrDComp = &mFeatures.mWrDComp;
    mFeatureWrPartial = &mFeatures.mWrPartial;
    mFeatureWrShaderCache = &mFeatures.mWrShaderCache;
    mFeatureWrOptimizedShaders = &mFeatures.mWrOptimizedShaders;
    mFeatureWrScissoredCacheClears = &mFeatures.mWrScissoredCacheClears;
    mFeatureHwCompositing = &mFeatures.mHwCompositing;
    mFeatureGPUProcess = &mFeatures.mGPUProcess;
    mFeatureGLNorm16Textures = &mFeatures.mGLNorm16Textures;
  }

  void SetUp() override {
    mMockGfxInfo = new MockGfxInfo();
    mGfxInfo = mMockGfxInfo;

    mFeatureD3D11HwAngle = &mFeatures.mD3D11HwAngle;
    mFeatureMetalAngle = &mFeatures.mMetalAngle;
    mFeatureGlAngle = &mFeatures.mGlAngle;
    // Test as if we're on Windows by default.
    mFeatureWrAngleBackend = mFeatureD3D11HwAngle;
    mFeatureD3D11Compositing = &mFeatures.mD3D11Compositing;

    // By default, turn everything on. This effectively assumes we are on
    // qualified nightly Windows 10 with DirectComposition support.
    mFeatureHwCompositing->EnableByDefault();
    mFeatureD3D11HwAngle->EnableByDefault();
    mFeatureD3D11Compositing->EnableByDefault();
    mFeatureGPUProcess->EnableByDefault();

    mWrCompositorEnabled.emplace(true);
    // Keep the existing tests on the non-layer compositor configuration.
    mWrLayerCompositorEnabled = false;
    mWrAngleEnabled.emplace(true);
    mWrPartialPresent = true;
    mWrShaderCache.emplace(true);
    mWrOptimizedShaders = true;
    mWrRequireAngle = false;
    mWrDCompWinEnabled = true;
    mWrCompositorDCompRequired = true;
    mWrScissoredCacheClearsEnabled = true;
    ++mHwStretchingSupport.mBoth;
    mIsNightly = true;
    mIsEarlyBetaOrEarlier = true;
  }

  void TearDown() override {
    mFeatures.mWr.Reset();
    mFeatures.mWrCompositor.Reset();
    mFeatures.mWrAngle.Reset();
    mFeatures.mWrDComp.Reset();
    mFeatures.mWrPartial.Reset();
    mFeatures.mWrShaderCache.Reset();
    mFeatures.mWrOptimizedShaders.Reset();
    mFeatures.mWrScissoredCacheClears.Reset();
    mFeatures.mHwCompositing.Reset();
    mFeatures.mD3D11HwAngle.Reset();
    mFeatures.mMetalAngle.Reset();
    mFeatures.mGlAngle.Reset();
    mFeatures.mD3D11Compositing.Reset();
    mFeatures.mGPUProcess.Reset();
    mFeatures.mGLNorm16Textures.Reset();
  }

  struct {
    FeatureState mWr;
    FeatureState mWrCompositor;
    FeatureState mWrAngle;
    FeatureState mWrDComp;
    FeatureState mWrPartial;
    FeatureState mWrShaderCache;
    FeatureState mWrOptimizedShaders;
    FeatureState mWrScissoredCacheClears;
    FeatureState mHwCompositing;
    FeatureState mD3D11HwAngle;
    FeatureState mMetalAngle;
    FeatureState mGlAngle;
    FeatureState mD3D11Compositing;
    FeatureState mGPUProcess;
    FeatureState mGLNorm16Textures;
  } mFeatures;

  RefPtr<MockGfxInfo> mMockGfxInfo;
};

}  // namespace gfx
}  // namespace mozilla

TEST_F(GfxConfigManager, WebRenderDefault) {
  ConfigureWebRender();

  EXPECT_TRUE(mFeatures.mWr.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrCompositor.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrDComp.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrPartial.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrShaderCache.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrOptimizedShaders.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrScissoredCacheClears.IsEnabled());
  EXPECT_TRUE(mFeatures.mHwCompositing.IsEnabled());
  EXPECT_TRUE(mFeatures.mGPUProcess.IsEnabled());
  EXPECT_TRUE(mFeatures.mD3D11HwAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mGLNorm16Textures.IsEnabled());
}

TEST_F(GfxConfigManager, WebRenderDefaultRelease) {
  mIsNightly = mIsEarlyBetaOrEarlier = false;
  ConfigureWebRender();

  EXPECT_TRUE(mFeatures.mWr.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrCompositor.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrDComp.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrPartial.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrShaderCache.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrOptimizedShaders.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrScissoredCacheClears.IsEnabled());
  EXPECT_TRUE(mFeatures.mHwCompositing.IsEnabled());
  EXPECT_TRUE(mFeatures.mGPUProcess.IsEnabled());
  EXPECT_TRUE(mFeatures.mD3D11HwAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mGLNorm16Textures.IsEnabled());
}

TEST_F(GfxConfigManager, WebRenderNoPartialPresent) {
  mWrPartialPresent = false;
  ConfigureWebRender();

  EXPECT_TRUE(mFeatures.mWr.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrCompositor.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrDComp.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrPartial.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrShaderCache.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrOptimizedShaders.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrScissoredCacheClears.IsEnabled());
  EXPECT_TRUE(mFeatures.mHwCompositing.IsEnabled());
  EXPECT_TRUE(mFeatures.mGPUProcess.IsEnabled());
  EXPECT_TRUE(mFeatures.mD3D11HwAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mGLNorm16Textures.IsEnabled());
}

TEST_F(GfxConfigManager, WebRenderPartialBlocked) {
  mWrPartialPresent = true;
  mMockGfxInfo->mStatusWrPartialPresent = nsIGfxInfo::FEATURE_BLOCKED_DEVICE;
  ConfigureWebRender();

  EXPECT_FALSE(mFeatures.mWrPartial.IsEnabled());
}

TEST_F(GfxConfigManager, WebRenderForcePartialBlocked) {
  mWrForcePartialPresent = true;
  mMockGfxInfo->mStatusWrPartialPresent = nsIGfxInfo::FEATURE_BLOCKED_DEVICE;
  ConfigureWebRender();

  EXPECT_TRUE(mFeatures.mWrPartial.IsEnabled());
}

TEST_F(GfxConfigManager, WebRenderScaledResolutionWithHwStretching) {
  mScaledResolution = true;
  ConfigureWebRender();

  EXPECT_TRUE(mFeatures.mWr.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrCompositor.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrDComp.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrShaderCache.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrOptimizedShaders.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrScissoredCacheClears.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrPartial.IsEnabled());
  EXPECT_TRUE(mFeatures.mHwCompositing.IsEnabled());
  EXPECT_TRUE(mFeatures.mGPUProcess.IsEnabled());
  EXPECT_TRUE(mFeatures.mD3D11HwAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mGLNorm16Textures.IsEnabled());
}

TEST_F(GfxConfigManager, WebRenderScaledResolutionNoHwStretching) {
  ++mHwStretchingSupport.mNone;
  mScaledResolution = true;
  ConfigureWebRender();

  EXPECT_TRUE(mFeatures.mWr.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrCompositor.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrDComp.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrShaderCache.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrOptimizedShaders.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrScissoredCacheClears.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrPartial.IsEnabled());
  EXPECT_TRUE(mFeatures.mHwCompositing.IsEnabled());
  EXPECT_TRUE(mFeatures.mGPUProcess.IsEnabled());
  EXPECT_TRUE(mFeatures.mD3D11HwAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mGLNorm16Textures.IsEnabled());
}

TEST_F(GfxConfigManager,
       WebRenderLayerCompositorScaledResolutionNoHwStretching) {
  mWrLayerCompositorEnabled = true;
  ++mHwStretchingSupport.mNone;
  mScaledResolution = true;
  ConfigureWebRender();

  EXPECT_TRUE(mFeatures.mWr.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrCompositor.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrDComp.IsEnabled());
}

TEST_F(GfxConfigManager, WebRenderLayerCompositorNoHwStretchingDCompDisabled) {
  mWrLayerCompositorEnabled = true;
  ++mHwStretchingSupport.mNone;
  mScaledResolution = true;
  mWrDCompWinEnabled = false;
  ConfigureWebRender();

  EXPECT_TRUE(mFeatures.mWr.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrCompositor.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrDComp.IsEnabled());
}

TEST_F(GfxConfigManager, WebRenderEnabledWithDisableHwCompositingNoWr) {
  mDisableHwCompositingNoWr = true;
  ConfigureWebRender();

  EXPECT_TRUE(mFeatures.mWr.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrCompositor.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrDComp.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrShaderCache.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrOptimizedShaders.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrScissoredCacheClears.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrPartial.IsEnabled());
  EXPECT_TRUE(mFeatures.mHwCompositing.IsEnabled());
  EXPECT_TRUE(mFeatures.mGPUProcess.IsEnabled());
  EXPECT_TRUE(mFeatures.mD3D11HwAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mGLNorm16Textures.IsEnabled());
}

TEST_F(GfxConfigManager, WebRenderDisabledWithDisableHwCompositingNoWr) {
  mDisableHwCompositingNoWr = true;
  mMockGfxInfo->mStatusWr = nsIGfxInfo::FEATURE_BLOCKED_DEVICE;
  ConfigureWebRender();

  EXPECT_FALSE(mFeatures.mWr.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrCompositor.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrAngle.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrDComp.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrPartial.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrShaderCache.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrOptimizedShaders.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrScissoredCacheClears.IsEnabled());
  EXPECT_FALSE(mFeatures.mHwCompositing.IsEnabled());
  EXPECT_TRUE(mFeatures.mGPUProcess.IsEnabled());
  EXPECT_TRUE(mFeatures.mD3D11HwAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mGLNorm16Textures.IsEnabled());
}

TEST_F(GfxConfigManager, WebRenderSafeMode) {
  mSafeMode = true;
  ConfigureWebRender();

  EXPECT_FALSE(mFeatures.mWr.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrCompositor.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrAngle.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrDComp.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrPartial.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrShaderCache.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrOptimizedShaders.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrScissoredCacheClears.IsEnabled());
  EXPECT_TRUE(mFeatures.mHwCompositing.IsEnabled());
  EXPECT_TRUE(mFeatures.mGPUProcess.IsEnabled());
  EXPECT_TRUE(mFeatures.mD3D11HwAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mGLNorm16Textures.IsEnabled());
}

TEST_F(GfxConfigManager, WebRenderDCompDisabled) {
  mWrDCompWinEnabled = false;
  ConfigureWebRender();

  EXPECT_TRUE(mFeatures.mWr.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrCompositor.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrAngle.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrDComp.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrPartial.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrShaderCache.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrOptimizedShaders.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrScissoredCacheClears.IsEnabled());
  EXPECT_TRUE(mFeatures.mHwCompositing.IsEnabled());
  EXPECT_TRUE(mFeatures.mGPUProcess.IsEnabled());
  EXPECT_TRUE(mFeatures.mD3D11HwAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mGLNorm16Textures.IsEnabled());
}

TEST_F(GfxConfigManager, WebRenderDCompNotRequired) {
  mWrDCompWinEnabled = false;
  mWrCompositorDCompRequired = false;
  ConfigureWebRender();

  EXPECT_TRUE(mFeatures.mWr.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrCompositor.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrAngle.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrDComp.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrPartial.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrShaderCache.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrOptimizedShaders.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrScissoredCacheClears.IsEnabled());
  EXPECT_TRUE(mFeatures.mHwCompositing.IsEnabled());
  EXPECT_TRUE(mFeatures.mGPUProcess.IsEnabled());
  EXPECT_TRUE(mFeatures.mD3D11HwAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mGLNorm16Textures.IsEnabled());
}

TEST_F(GfxConfigManager, WebRenderDCompBlocked) {
  mWrPartialPresent = true;
  mMockGfxInfo->mStatusWrDComp = nsIGfxInfo::FEATURE_BLOCKED_DEVICE;
  ConfigureWebRender();

  EXPECT_TRUE(mFeatures.mWr.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrCompositor.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrAngle.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrDComp.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrPartial.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrShaderCache.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrOptimizedShaders.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrScissoredCacheClears.IsEnabled());
  EXPECT_TRUE(mFeatures.mHwCompositing.IsEnabled());
  EXPECT_TRUE(mFeatures.mGPUProcess.IsEnabled());
  EXPECT_TRUE(mFeatures.mD3D11HwAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mGLNorm16Textures.IsEnabled());
}

TEST_F(GfxConfigManager, WebRenderAngleDisabled) {
  mWrAngleEnabled = Some(false);
  ConfigureWebRender();

  EXPECT_TRUE(mFeatures.mWr.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrCompositor.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrAngle.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrDComp.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrPartial.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrShaderCache.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrOptimizedShaders.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrScissoredCacheClears.IsEnabled());
  EXPECT_TRUE(mFeatures.mHwCompositing.IsEnabled());
  EXPECT_TRUE(mFeatures.mGPUProcess.IsEnabled());
  EXPECT_TRUE(mFeatures.mD3D11HwAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mGLNorm16Textures.IsEnabled());
}

TEST_F(GfxConfigManager, WebRenderAngleDisabledAndRequired) {
  mWrAngleEnabled = Some(false);
  mWrRequireAngle = true;
  ConfigureWebRender();

  EXPECT_FALSE(mFeatures.mWr.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrCompositor.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrAngle.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrDComp.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrPartial.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrShaderCache.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrOptimizedShaders.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrScissoredCacheClears.IsEnabled());
  EXPECT_TRUE(mFeatures.mHwCompositing.IsEnabled());
  EXPECT_TRUE(mFeatures.mGPUProcess.IsEnabled());
  EXPECT_TRUE(mFeatures.mD3D11HwAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mGLNorm16Textures.IsEnabled());
}

TEST_F(GfxConfigManager, WebRenderD3D11HwAngleDisabled) {
  mFeatures.mD3D11HwAngle.UserDisable("", ""_ns);
  ConfigureWebRender();

  EXPECT_TRUE(mFeatures.mWr.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrCompositor.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrAngle.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrDComp.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrPartial.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrShaderCache.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrOptimizedShaders.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrScissoredCacheClears.IsEnabled());
  EXPECT_TRUE(mFeatures.mHwCompositing.IsEnabled());
  EXPECT_TRUE(mFeatures.mGPUProcess.IsEnabled());
  EXPECT_FALSE(mFeatures.mD3D11HwAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mGLNorm16Textures.IsEnabled());
}

TEST_F(GfxConfigManager, WebRenderD3D11HwAngleDisabledAndRequired) {
  mWrRequireAngle = true;
  mFeatures.mD3D11HwAngle.UserDisable("", ""_ns);
  ConfigureWebRender();

  EXPECT_FALSE(mFeatures.mWr.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrCompositor.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrAngle.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrDComp.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrPartial.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrShaderCache.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrOptimizedShaders.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrScissoredCacheClears.IsEnabled());
  EXPECT_TRUE(mFeatures.mHwCompositing.IsEnabled());
  EXPECT_TRUE(mFeatures.mGPUProcess.IsEnabled());
  EXPECT_FALSE(mFeatures.mD3D11HwAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mGLNorm16Textures.IsEnabled());
}

TEST_F(GfxConfigManager, WebRenderGlAngleEnabled) {
  mFeatureWrAngleBackend = mFeatureGlAngle;
  ConfigureWebRender();

  EXPECT_TRUE(mFeatures.mGlAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mWr.IsEnabled());
}

TEST_F(GfxConfigManager, WebRenderGlAngleBlocked) {
  mFeatureWrAngleBackend = mFeatureGlAngle;
  mMockGfxInfo->mStatusGlAngle = nsIGfxInfo::FEATURE_BLOCKED_DEVICE;
  ConfigureWebRender();

  EXPECT_FALSE(mFeatures.mGlAngle.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mWr.IsEnabled());
}

TEST_F(GfxConfigManager, WebRenderGlAngleDisabledByPref) {
  mFeatureWrAngleBackend = mFeatureGlAngle;
  mWrAngleEnabled = Some(false);
  ConfigureWebRender();

  EXPECT_TRUE(mFeatures.mGlAngle.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mWr.IsEnabled());
}

TEST_F(GfxConfigManager, WebRenderMetalAngleBlocked) {
  mFeatureWrAngleBackend = mFeatureMetalAngle;
  mMockGfxInfo->mStatusMetalAngle = nsIGfxInfo::FEATURE_BLOCKED_DEVICE;
  ConfigureWebRender();

  EXPECT_TRUE(mFeatures.mWr.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrCompositor.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrAngle.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrDComp.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrPartial.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrShaderCache.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrOptimizedShaders.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrScissoredCacheClears.IsEnabled());
  EXPECT_TRUE(mFeatures.mHwCompositing.IsEnabled());
  EXPECT_TRUE(mFeatures.mGPUProcess.IsEnabled());
  EXPECT_FALSE(mFeatures.mMetalAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mGLNorm16Textures.IsEnabled());
}

TEST_F(GfxConfigManager, WebRenderGPUProcessDisabled) {
  mFeatures.mGPUProcess.UserDisable("", ""_ns);
  ConfigureWebRender();

  EXPECT_TRUE(mFeatures.mWr.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrCompositor.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrAngle.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrDComp.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrPartial.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrShaderCache.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrOptimizedShaders.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrScissoredCacheClears.IsEnabled());
  EXPECT_TRUE(mFeatures.mHwCompositing.IsEnabled());
  EXPECT_FALSE(mFeatures.mGPUProcess.IsEnabled());
  EXPECT_TRUE(mFeatures.mD3D11HwAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mGLNorm16Textures.IsEnabled());
}

TEST_F(GfxConfigManager, WebRenderIntelBatteryNoHwStretchingNotNightly) {
  mIsNightly = mIsEarlyBetaOrEarlier = false;
  ++mHwStretchingSupport.mNone;
  mScaledResolution = true;
  mMockGfxInfo->mHasBattery.ref() = true;
  mMockGfxInfo->mVendorId = "0x8086";
  ConfigureWebRender();

  EXPECT_TRUE(mFeatures.mWr.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrCompositor.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrDComp.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrPartial.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrShaderCache.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrOptimizedShaders.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrScissoredCacheClears.IsEnabled());
  EXPECT_TRUE(mFeatures.mHwCompositing.IsEnabled());
  EXPECT_TRUE(mFeatures.mGPUProcess.IsEnabled());
  EXPECT_TRUE(mFeatures.mD3D11HwAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mGLNorm16Textures.IsEnabled());
}

TEST_F(GfxConfigManager, WebRenderSofwareAndNotQualified) {
  mMockGfxInfo->mStatusWr = nsIGfxInfo::FEATURE_BLOCKED_DEVICE;
  ConfigureWebRender();

  EXPECT_FALSE(mFeatures.mWr.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrCompositor.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrAngle.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrDComp.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrPartial.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrShaderCache.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrOptimizedShaders.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrScissoredCacheClears.IsEnabled());
  EXPECT_TRUE(mFeatures.mHwCompositing.IsEnabled());
  EXPECT_TRUE(mFeatures.mGPUProcess.IsEnabled());
  EXPECT_TRUE(mFeatures.mD3D11HwAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mGLNorm16Textures.IsEnabled());
}

TEST_F(GfxConfigManager, WebRenderForceEnabledEnvvar) {
  mWrEnvForceEnabled = true;
  mMockGfxInfo->mStatusWr = nsIGfxInfo::FEATURE_BLOCKED_DEVICE;
  ConfigureWebRender();

  EXPECT_TRUE(mFeatures.mWr.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrCompositor.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrDComp.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrPartial.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrShaderCache.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrOptimizedShaders.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrScissoredCacheClears.IsEnabled());
  EXPECT_TRUE(mFeatures.mHwCompositing.IsEnabled());
  EXPECT_TRUE(mFeatures.mGPUProcess.IsEnabled());
  EXPECT_TRUE(mFeatures.mD3D11HwAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mGLNorm16Textures.IsEnabled());
}

TEST_F(GfxConfigManager, WebRenderSoftwareAllowedForceEnabledPref) {
  mWrForceEnabled = true;
  mMockGfxInfo->mStatusWr = nsIGfxInfo::FEATURE_BLOCKED_DEVICE;
  ConfigureWebRender();

  EXPECT_TRUE(mFeatures.mWr.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrCompositor.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrDComp.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrPartial.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrShaderCache.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrOptimizedShaders.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrScissoredCacheClears.IsEnabled());
  EXPECT_TRUE(mFeatures.mHwCompositing.IsEnabled());
  EXPECT_TRUE(mFeatures.mGPUProcess.IsEnabled());
  EXPECT_TRUE(mFeatures.mD3D11HwAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mGLNorm16Textures.IsEnabled());
}

TEST_F(GfxConfigManager, WebRenderForceSoftwareForceEnabledEnvvar) {
  mWrEnvForceEnabled = true;
  mWrSoftwareForceEnabled = true;
  mMockGfxInfo->mStatusWr = nsIGfxInfo::FEATURE_BLOCKED_DEVICE;
  ConfigureWebRender();

  EXPECT_FALSE(mFeatures.mWr.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrCompositor.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrAngle.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrDComp.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrPartial.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrShaderCache.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrOptimizedShaders.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrScissoredCacheClears.IsEnabled());
  EXPECT_TRUE(mFeatures.mHwCompositing.IsEnabled());
  EXPECT_TRUE(mFeatures.mGPUProcess.IsEnabled());
  EXPECT_TRUE(mFeatures.mD3D11HwAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mGLNorm16Textures.IsEnabled());
}

TEST_F(GfxConfigManager, WebRenderSoftwareReleaseWindowsGPUProcessDisabled) {
  mIsNightly = mIsEarlyBetaOrEarlier = false;
  mMockGfxInfo->mStatusWr = nsIGfxInfo::FEATURE_BLOCKED_DEVICE;
  mFeatures.mGPUProcess.UserDisable("", ""_ns);
  ConfigureWebRender();

  EXPECT_FALSE(mFeatures.mWr.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrCompositor.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrAngle.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrDComp.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrPartial.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrShaderCache.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrOptimizedShaders.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrScissoredCacheClears.IsEnabled());
  EXPECT_TRUE(mFeatures.mHwCompositing.IsEnabled());
  EXPECT_FALSE(mFeatures.mGPUProcess.IsEnabled());
  EXPECT_TRUE(mFeatures.mD3D11HwAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mGLNorm16Textures.IsEnabled());
}

TEST_F(GfxConfigManager, WebRenderShaderCacheDisabled) {
  mWrShaderCache = Some(false);
  ConfigureWebRender();

  EXPECT_TRUE(mFeatures.mWr.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrCompositor.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrDComp.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrShaderCache.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrOptimizedShaders.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrScissoredCacheClears.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrPartial.IsEnabled());
  EXPECT_TRUE(mFeatures.mHwCompositing.IsEnabled());
  EXPECT_TRUE(mFeatures.mGPUProcess.IsEnabled());
  EXPECT_TRUE(mFeatures.mD3D11HwAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mGLNorm16Textures.IsEnabled());
}

TEST_F(GfxConfigManager, WebRenderShaderCacheBlocked) {
  mMockGfxInfo->mStatusWrShaderCache = nsIGfxInfo::FEATURE_BLOCKED_DEVICE;
  ConfigureWebRender();

  EXPECT_TRUE(mFeatures.mWr.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrCompositor.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrDComp.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrShaderCache.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrOptimizedShaders.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrScissoredCacheClears.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrPartial.IsEnabled());
  EXPECT_TRUE(mFeatures.mHwCompositing.IsEnabled());
  EXPECT_TRUE(mFeatures.mGPUProcess.IsEnabled());
  EXPECT_TRUE(mFeatures.mD3D11HwAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mGLNorm16Textures.IsEnabled());
}

TEST_F(GfxConfigManager, WebRenderScissoredCacheClearsDisabled) {
  mWrScissoredCacheClearsEnabled = false;
  ConfigureWebRender();

  EXPECT_TRUE(mFeatures.mWr.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrCompositor.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrDComp.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrShaderCache.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrOptimizedShaders.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrScissoredCacheClears.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrPartial.IsEnabled());
  EXPECT_TRUE(mFeatures.mHwCompositing.IsEnabled());
  EXPECT_TRUE(mFeatures.mGPUProcess.IsEnabled());
  EXPECT_TRUE(mFeatures.mD3D11HwAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mGLNorm16Textures.IsEnabled());
}

TEST_F(GfxConfigManager, WebRenderScissoredCacheClearsBlocked) {
  mMockGfxInfo->mStatusWrScissoredCacheClears =
      nsIGfxInfo::FEATURE_BLOCKED_DEVICE;
  ConfigureWebRender();

  EXPECT_TRUE(mFeatures.mWr.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrCompositor.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrDComp.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrShaderCache.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrOptimizedShaders.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrScissoredCacheClears.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrPartial.IsEnabled());
  EXPECT_TRUE(mFeatures.mHwCompositing.IsEnabled());
  EXPECT_TRUE(mFeatures.mGPUProcess.IsEnabled());
  EXPECT_TRUE(mFeatures.mD3D11HwAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mGLNorm16Textures.IsEnabled());
}

TEST_F(GfxConfigManager, WebRenderScissoredCacheClearsForceEnabled) {
  mWrScissoredCacheClearsForceEnabled = false;
  mMockGfxInfo->mStatusWrScissoredCacheClears =
      nsIGfxInfo::FEATURE_BLOCKED_DEVICE;
  ConfigureWebRender();

  EXPECT_TRUE(mFeatures.mWr.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrCompositor.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrDComp.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrShaderCache.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrOptimizedShaders.IsEnabled());
  EXPECT_FALSE(mFeatures.mWrScissoredCacheClears.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrPartial.IsEnabled());
  EXPECT_TRUE(mFeatures.mHwCompositing.IsEnabled());
  EXPECT_TRUE(mFeatures.mGPUProcess.IsEnabled());
  EXPECT_TRUE(mFeatures.mD3D11HwAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mGLNorm16Textures.IsEnabled());
}

TEST_F(GfxConfigManager, GLNorm16TexturesBlocked) {
  mMockGfxInfo->mStatusGLNorm16Textures = nsIGfxInfo::FEATURE_BLOCKED_DEVICE;
  ConfigureWebRender();

  EXPECT_TRUE(mFeatures.mWr.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrCompositor.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrAngle.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrDComp.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrShaderCache.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrOptimizedShaders.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrScissoredCacheClears.IsEnabled());
  EXPECT_TRUE(mFeatures.mWrPartial.IsEnabled());
  EXPECT_TRUE(mFeatures.mHwCompositing.IsEnabled());
  EXPECT_TRUE(mFeatures.mGPUProcess.IsEnabled());
  EXPECT_TRUE(mFeatures.mD3D11HwAngle.IsEnabled());
  EXPECT_FALSE(mFeatures.mGLNorm16Textures.IsEnabled());
}
