/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef mozilla_gfx_config_gfxVars_h
#define mozilla_gfx_config_gfxVars_h

#include <stdint.h>

#include "MediaCodecsSupport.h"
#include "mozilla/Assertions.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/gfx/GraphicsMessages.h"
#include "mozilla/gfx/Point.h"
#include "mozilla/gfx/Types.h"
#include "nsTArray.h"
#include "nsXULAppAPI.h"

namespace mozilla::gfx {

class gfxVarReceiver;
class MOZ_STACK_CLASS gfxVarsCollectUpdates;

#ifdef MOZ_WIDGET_ANDROID
#  define GFX_VARS_LIST_ANDROID(_)                               \
    _(PlatformMediaCodecsSupported, MediaCodecsSupported,        \
      MediaCodecsSupported())                                    \
    _(PlatformMediaCodecs10BitSupported, MediaCodecsSupported,   \
      MediaCodecsSupported())                                    \
    _(PlatformH264HwDecodeMaxSize, IntSize, IntSize())           \
    _(PlatformHEVCHwDecodeMaxSize, IntSize, IntSize())           \
    _(PlatformVP8HwDecodeMaxSize, IntSize, IntSize())            \
    _(PlatformVP9HwDecodeMaxSize, IntSize, IntSize())            \
    _(PlatformAV1HwDecodeMaxSize, IntSize, IntSize())            \
    _(PlatformUnmappedSwDecodeMimeTypes, nsCString, nsCString()) \
    _(PlatformUnmappedHwDecodeMimeTypes, nsCString, nsCString()) \
    _(WidevineSupport, DrmSchemeSupport, DrmSchemeSupport::Unknown)
#else
#  define GFX_VARS_LIST_ANDROID(_)
#endif

// Generator for graphics vars.
#define GFX_VARS_LIST(_)                                           \
  /* C++ Name,                  Data Type,        Default Value */ \
  _(AllowEglRbab, bool, true)                                      \
  _(AllowWebGL, bool, true)                                        \
  _(AllowWebgl2, bool, true)                                       \
  _(AllowWebglAccelAngle, bool, true)                              \
  _(BrowserTabsRemoteAutostart, bool, false)                       \
  _(ContentBackend, BackendType, BackendType::NONE)                \
  _(SoftwareBackend, BackendType, BackendType::NONE)               \
  _(OffscreenFormat, gfxImageFormat,                               \
    mozilla::gfx::SurfaceFormat::X8R8G8B8_UINT32)                  \
  _(RequiresAcceleratedGLContextForCompositorOGL, bool, false)     \
  _(CanUseHardwareVideoDecoding, bool, false)                      \
  _(CanUseHardwareVideoEncoding, bool, false)                      \
  _(DXInterop2Blocked, bool, false)                                \
  _(DXNV12Blocked, bool, false)                                    \
  _(DXP010Blocked, bool, false)                                    \
  _(DXP016Blocked, bool, false)                                    \
  _(UseWebRenderANGLE, bool, false)                                \
  _(UseWebRenderFlipSequentialWin, bool, false)                    \
  _(UseWebRenderDCompWin, bool, false)                             \
  _(UseWebRenderDCompVideoHwOverlayWin, bool, false)               \
  _(UseWebRenderDCompVideoSwOverlayWin, bool, false)               \
  _(UseWebRenderTripleBufferingWin, bool, false)                   \
  _(UseWebRenderCompositor, bool, false)                           \
  _(UseWebRenderProgramBinaryDisk, bool, false)                    \
  _(ShouldWarmUpWebRenderProgramBinaries, bool, false)             \
  _(UseWebRenderOptimizedShaders, bool, false)                     \
  _(UseWebRenderScissoredCacheClears, bool, true)                  \
  _(WebRenderProfilerUI, nsCString, nsCString())                   \
  _(WebglAllowCoreProfile, bool, true)                             \
  _(WebglAllowWindowsNativeGl, bool, false)                        \
  _(WebRenderMaxPartialPresentRects, int32_t, 0)                   \
  _(WebRenderDebugFlags, uint64_t, 0)                              \
  _(WebRenderBoolParameters, int32_t, 0)                           \
  _(WebRenderBatchingLookback, int32_t, 10)                        \
  _(WebRenderBlobTileSize, int32_t, 256)                           \
  _(WebRenderBatchedUploadThreshold, int32_t, 512 * 512)           \
  _(WebRenderPathAaSamples, int32_t, 8)                            \
  _(WebRenderSlowCpuFrameThreshold, float, 10.0)                   \
  _(UseSoftwareWebRender, bool, false)                             \
  _(AllowSoftwareWebRenderD3D11, bool, false)                      \
  _(PrimaryScreenDepth, int32_t, 0)                                \
  _(GREDirectory, nsString, nsString())                            \
  _(ProfDirectory, nsString, nsString())                           \
  _(AllowD3D11KeyedMutex, bool, false)                             \
  _(SwapIntervalGLX, bool, false)                                  \
  _(SwapIntervalEGL, bool, false)                                  \
  _(SystemTextQuality, int32_t, 5 /* CLEARTYPE_QUALITY */)         \
  _(SystemTextClearTypeLevel, float, 1.0f)                         \
  _(SystemTextEnhancedContrast, float, 1.0f)                       \
  _(SystemTextGamma, float, 2.2f)                                  \
  _(SystemTextPixelGeometry, int32_t, 1 /* pixel geometry RGB */)  \
  _(SystemTextRenderingMode, int32_t, 0)                           \
  _(SystemGDIGamma, float, 1.4f)                                   \
  _(LayersWindowRecordingPath, nsCString, nsCString())             \
  _(UseDoubleBufferingWithCompositor, bool, false)                 \
  _(UseGLSwizzle, bool, true)                                      \
  _(ForceSubpixelAAWherePossible, bool, false)                     \
  _(UseAHardwareBufferSharedSurfaceWebglOop, bool, false)          \
  _(UseEGL, bool, false)                                           \
  _(DrmRenderDevice, nsCString, nsCString())                       \
  _(UseDMABuf, bool, false)                                        \
  _(UseDMABufWebGL, bool, true)                                    \
  _(DMABufModifiersXRGB, ArrayOfuint64_t, nsTArray<uint64_t>())    \
  _(DMABufModifiersARGB, ArrayOfuint64_t, nsTArray<uint64_t>())    \
  _(WebRenderRequiresHardwareDriver, bool, false)                  \
  _(SupportsThreadsafeGL, bool, false)                             \
  _(AllowWebGPU, bool, false)                                      \
  _(AllowWebGPUExternalTexture, bool, false)                       \
  _(UseVP8HwDecode, bool, false)                                   \
  _(UseVP8HwEncode, bool, false)                                   \
  _(UseVP9HwDecode, bool, false)                                   \
  _(UseVP9HwEncode, bool, false)                                   \
  _(UseAV1HwDecode, bool, false)                                   \
  _(UseAV1HwEncode, bool, false)                                   \
  _(UseH264HwDecode, bool, false)                                  \
  _(UseH264HwEncode, bool, false)                                  \
  _(HasWebrtcH264Hw, bool, false)                                  \
  _(UseHEVCHwDecode, bool, false)                                  \
  _(UseHEVCHwEncode, bool, false)                                  \
  _(HwDecodedVideoZeroCopy, bool, false)                           \
  _(UseWMFHWDWM, bool, false)                                      \
  _(UseDMABufSurfaceExport, bool, true)                            \
  _(ReuseDecoderDevice, bool, false)                               \
  _(UseCanvasRenderThread, bool, false)                            \
  _(AllowBackdropFilter, bool, true)                               \
  _(WebglOopAsyncPresentForceSync, bool, true)                     \
  _(UseAcceleratedCanvas2D, bool, false)                           \
  _(AllowSoftwareWebRenderOGL, bool, false)                        \
  _(WebglUseHardware, bool, true)                                  \
  _(WebRenderOverlayVpAutoHDR, bool, false)                        \
  _(WebRenderOverlayVpSuperResolution, bool, false)                \
  _(AllowWebGPUPresentWithoutReadback, bool, false)                \
  _(GPUProcessEnabled, bool, false)                                \
  _(DMABufModifiersP010, ArrayOfuint64_t, nsTArray<uint64_t>())    \
  _(DMABufModifiersNV12, ArrayOfuint64_t, nsTArray<uint64_t>())    \
  _(AllowGLNorm16Textures, bool, false)                            \
  _(WebRenderLayerCompositorDCompTexture, bool, false)             \
  _(WebRenderOverlayHDR, bool, false)                              \
  _(UseWebRenderDCompositionTextureOverlayWin, bool, false)        \
  _(VideoHDR, bool, false)                                         \
  _(AdapterVendorID, nsCString, nsCString())                       \
  _(AdapterDeviceID, nsCString, nsCString())                       \
  _(AdapterDriverVersion, nsCString, nsCString())                  \
  _(ForceAImageReaderAndroid, bool, false)                         \
  _(AllowMetalAngleWebGL, bool, false)                             \
  _(CanUseVulkanHardwareVideoDecoding, bool, false)                \
  GFX_VARS_LIST_ANDROID(_)                                         \
/* Add new entries above this line. */

// Some graphics settings are computed on the UI process and must be
// communicated to content and GPU processes. gfxVars helps facilitate
// this. Its function is similar to StaticPrefs, except rather than hold
// user preferences, it holds dynamically computed values.
//
// Each variable in GFX_VARS_LIST exposes the following static methods:
//
//    const DataType& CxxName();
//    void SetCxxName(const DataType& aValue);
//
// Note that the setter may only be called in the UI process; a gfxVar must be
// a variable that is determined in the UI process and pushed to child
// processes.
class gfxVars final {
 public:
  // These values will be used during the Initialize() call if set.  Any
  // updates that come before initialization will get added to this array.
  static void SetValuesForInitialize(
      const nsTArray<GfxVarUpdate>& aInitUpdates);

  static void Initialize();
  static void Shutdown();

  static void ApplyUpdate(const nsTArray<GfxVarUpdate>& aUpdate);
  static void AddReceiver(gfxVarReceiver* aReceiver);
  static void RemoveReceiver(gfxVarReceiver* aReceiver);

  // Return a list of updates for all variables with non-default values.
  static nsTArray<GfxVarUpdate> FetchNonDefaultVars();

 private:
  template <typename U>
  static U CloneVarValue(const U& aValue) {
    return aValue;
  }

  template <typename U>
  static nsTArray<U> CloneVarValue(const nsTArray<U>& aValue) {
    return aValue.Clone();
  }

 public:
  // Each variable must expose Set and Get methods for IPDL.
  class VarBase {
   public:
    VarBase();
    virtual void SetValue(const GfxVarValue& aValue) = 0;
    virtual void GetValue(GfxVarValue* aOutValue) = 0;
    virtual bool HasDefaultValue() const = 0;
    size_t Index() const { return mIndex; }

   private:
    size_t mIndex;
  };

  // Whether the gfxVars singleton instance has been initialized. Most gfx code
  // doesn't need to check this, but code that can potentially run before
  // gfxPlatform initialization can use this to check whether gfxVars are
  // available yet.
  static bool IsInitialized() { return sInstance != nullptr; }

 private:
  friend class gfxVarsCollectUpdates;
  static void StartCollectingUpdates();
  static void StopCollectingUpdates();

  static StaticAutoPtr<gfxVars> sInstance;
  static StaticAutoPtr<nsTArray<VarBase*>> sVarList;

  template <typename T, T Default(), T GetFrom(const GfxVarValue& aValue)>
  class VarImpl final : public VarBase {
   public:
    VarImpl() : mValue(Default()) {}
    void SetValue(const GfxVarValue& aValue) override {
      mValue = GetFrom(aValue);
      if (mListener) {
        mListener();
      }
    }
    void GetValue(GfxVarValue* aOutValue) override {
      *aOutValue = GfxVarValue(mValue);
    }
    bool HasDefaultValue() const override { return mValue == Default(); }
    const T& Get() const { return mValue; }

    // Return true if the value changed, false otherwise.
    bool Set(const T& aValue) {
      MOZ_ASSERT(XRE_IsParentProcess());
      if (mValue == aValue) {
        return false;
      }
      mValue = CloneVarValue(aValue);
      if (mListener) {
        mListener();
      }
      return true;
    }

    void SetListener(const std::function<void()>& aListener) {
      // Each gfxVar exposes a single-slot listener: a second registration
      // would silently overwrite the first, breaking whatever consumer the
      // first listener belonged to. Catch an accidental second consumer in
      // diagnostic builds (debug / Nightly) rather than producing silent
      // misbehaviour. If a future use case legitimately needs more than one
      // listener per variable, change this storage to a list.
      MOZ_DIAGNOSTIC_ASSERT(!mListener,
                            "gfxVar already has a listener; only one "
                            "consumer is supported per variable.");
      mListener = aListener;
    }

   private:
    T mValue;
    std::function<void()> mListener;
  };

#define GFX_VAR_DECL(CxxName, DataType, DefaultValue)                          \
 private:                                                                      \
  static DataType Get##CxxName##Default() { return DefaultValue; }             \
  static DataType Get##CxxName##From(const GfxVarValue& aValue) {              \
    return CloneVarValue(aValue.get_##DataType());                             \
  }                                                                            \
  VarImpl<DataType, Get##CxxName##Default, Get##CxxName##From> mVar##CxxName;  \
                                                                               \
 public:                                                                       \
  static const DataType& CxxName() { return sInstance->mVar##CxxName.Get(); }  \
  static DataType Get##CxxName##OrDefault() {                                  \
    if (!sInstance) {                                                          \
      return DefaultValue;                                                     \
    }                                                                          \
    return CloneVarValue(sInstance->mVar##CxxName.Get());                      \
  }                                                                            \
  static void Set##CxxName(const DataType& aValue) {                           \
    if (sInstance->mVar##CxxName.Set(aValue)) {                                \
      sInstance->NotifyReceivers(&sInstance->mVar##CxxName);                   \
    }                                                                          \
  }                                                                            \
                                                                               \
  static void Set##CxxName##Listener(const std::function<void()>& aListener) { \
    sInstance->mVar##CxxName.SetListener(aListener);                           \
  }

  using ArrayOfuint64_t = nsTArray<uint64_t>;
  using MediaCodecsSupported = media::MediaCodecsSupported;
  using DrmSchemeSupport = media::DrmSchemeSupport;

  GFX_VARS_LIST(GFX_VAR_DECL)
#undef GFX_VAR_DECL

 private:
  gfxVars();

  void NotifyReceivers(VarBase* aVar);

 private:
  nsTArray<gfxVarReceiver*> mReceivers;
};

#undef GFX_VARS_LIST
#undef GFX_VARS_LIST_ANDROID

// Helper class that batches changes to gfxVars into a single update to minimize
// churn for receivers. This is particularly useful for the media processes
// which reconfigure themselves when gfxVars updates come in.
class MOZ_STACK_CLASS gfxVarsCollectUpdates final {
 public:
  gfxVarsCollectUpdates() { gfxVars::StartCollectingUpdates(); }
  ~gfxVarsCollectUpdates() { gfxVars::StopCollectingUpdates(); }
};

}  // namespace mozilla::gfx

#endif  // mozilla_gfx_config_gfxVars_h
