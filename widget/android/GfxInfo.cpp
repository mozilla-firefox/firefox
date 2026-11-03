/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GfxInfo.h"

#include "AndroidBuild.h"
#include "GLContext.h"
#include "GLContextProvider.h"
#include "mozilla/Preferences.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/java/HardwareCodecCapabilityUtilsWrappers.h"
#include "nsExceptionHandler.h"
#include "nsHashKeys.h"
#include "nsServiceManagerUtils.h"
#include "nsUnicharUtils.h"
#include "nsVersionComparator.h"
#include "prenv.h"

namespace mozilla {
namespace widget {

class GfxInfo::GLStrings {
  nsCString mVendor;
  nsCString mRenderer;
  nsCString mVersion;
  nsTArray<nsCString> mExtensions;
  bool mReady;

  // Pref names to use for caching. The OS build fingerprint is used to check
  // whether the cached values are still valid, and the others store the values
  // themselves.
  constexpr static const char* kCachePrefNameFingerprint =
      "gfxinfo.gl-strings.build-fingerprint";
  constexpr static const char* kCachePrefNameVendor =
      "gfxinfo.gl-strings.vendor";
  constexpr static const char* kCachePrefNameRenderer =
      "gfxinfo.gl-strings.renderer";
  constexpr static const char* kCachePrefNameVersion =
      "gfxinfo.gl-strings.version";
  constexpr static const char* kCachePrefNameExtensions =
      "gfxinfo.gl-strings.extensions";

 public:
  GLStrings() : mReady(false) {}

  const nsCString& Vendor() {
    EnsureInitialized();
    return mVendor;
  }

  // This spoofed value wins, even if the environment variable
  // MOZ_GFX_SPOOF_GL_VENDOR was set.
  void SpoofVendor(const nsCString& s) { mVendor = s; }

  const nsCString& Renderer() {
    EnsureInitialized();
    return mRenderer;
  }

  // This spoofed value wins, even if the environment variable
  // MOZ_GFX_SPOOF_GL_RENDERER was set.
  void SpoofRenderer(const nsCString& s) { mRenderer = s; }

  const nsCString& Version() {
    EnsureInitialized();
    return mVersion;
  }

  // This spoofed value wins, even if the environment variable
  // MOZ_GFX_SPOOF_GL_VERSION was set.
  void SpoofVersion(const nsCString& s) { mVersion = s; }

  const nsTArray<nsCString>& Extensions() {
    EnsureInitialized();
    return mExtensions;
  }

  void EnsureInitialized() {
    if (mReady) {
      return;
    }

    // First, attempt to spoof the strings from environment variables so they
    // take precedence over the real values. But don't overwrite any values
    // already set by the Spoof*() functions.
    if (mVendor.IsEmpty()) {
      const char* spoofedVendor = PR_GetEnv("MOZ_GFX_SPOOF_GL_VENDOR");
      if (spoofedVendor) {
        mVendor.Assign(spoofedVendor);
      }
    }
    if (mRenderer.IsEmpty()) {
      const char* spoofedRenderer = PR_GetEnv("MOZ_GFX_SPOOF_GL_RENDERER");
      if (spoofedRenderer) {
        mRenderer.Assign(spoofedRenderer);
      }
    }
    if (mVersion.IsEmpty()) {
      const char* spoofedVersion = PR_GetEnv("MOZ_GFX_SPOOF_GL_VERSION");
      if (spoofedVersion) {
        mVersion.Assign(spoofedVersion);
      }
    }

    // Next we attempt to initialize the strings from values cached by a
    // previous run, to avoid expensive GL context creation during startup. The
    // OS build fingerprint is used to ensure the cache is still valid.
    const nsCString fingerprint = java::sdk::Build::FINGERPRINT()->ToCString();
    nsCString cachedFingerprint;
    Preferences::GetCString(kCachePrefNameFingerprint, cachedFingerprint);

    if (fingerprint == cachedFingerprint) {
      if (mVendor.IsEmpty()) {
        Preferences::GetCString(kCachePrefNameVendor, mVendor);
      }
      if (mRenderer.IsEmpty()) {
        Preferences::GetCString(kCachePrefNameRenderer, mRenderer);
      }
      if (mVersion.IsEmpty()) {
        Preferences::GetCString(kCachePrefNameVersion, mVersion);
      }
      if (mExtensions.IsEmpty()) {
        nsCString extensions;
        Preferences::GetCString(kCachePrefNameExtensions, extensions);
        for (auto extension : extensions.Split(' ')) {
          mExtensions.AppendElement(extension);
        }
      }

      mReady = true;
      return;
    }

    // If the cache is invalid, query Android's system GLES implementation
    // before choosing Gecko's backend, without initializing bundled ANGLE.
    RefPtr<gl::GLContext> gl;
    nsCString discardFailureId;
    gl = gl::GLContextProviderEGL::CreateHeadlessNative(
        {gl::CreateContextFlags::REQUIRE_COMPAT_PROFILE}, &discardFailureId);

    if (!gl || !gl->MakeCurrent()) {
      // Setting mReady to true here means that we won't retry. Everything will
      // remain blocklisted forever. Ideally, we would like to update that once
      // any GLContext is successfully created, like the compositor's GLContext.
      mReady = true;
      return;
    }

    if (mVendor.IsEmpty()) {
      mVendor.Assign(gl->VendorString());
    }
    if (mRenderer.IsEmpty()) {
      mRenderer.Assign(gl->RendererString());
    }
    if (mVersion.IsEmpty()) {
      mVersion.Assign(gl->VersionString());
    }
    if (mExtensions.IsEmpty()) {
      mExtensions = gl->ExtensionStrings().Clone();
    }

    // Cache the GL strings so we can avoid expensive GL Context
    // initialization on next launch. Make sure we use the actual values
    // obtained from GL rather than any spoofed values.
    CacheStrings(gl->VendorString(), gl->RendererString(), gl->VersionString(),
                 gl->ExtensionStrings());

    mReady = true;
  }

  void CacheStrings(const nsCString& aVendor, const nsCString& aRenderer,
                    const nsCString& aVersion,
                    Span<const nsCString> aExtensions) {
    const nsCString fingerprint = java::sdk::Build::FINGERPRINT()->ToCString();

    Preferences::SetCString(kCachePrefNameVendor, aVendor);
    Preferences::SetCString(kCachePrefNameRenderer, aRenderer);
    Preferences::SetCString(kCachePrefNameVersion, aVersion);
    Preferences::SetCString(kCachePrefNameExtensions,
                            StringJoin(" "_ns, aExtensions));
    // Save the fingerprint last so that if we're killed part way through
    // writing these prefs the next launch doesn't attempt to use partially
    // cached data.
    Preferences::SetCString(kCachePrefNameFingerprint, fingerprint);
  }
};

#ifdef DEBUG
NS_IMPL_ISUPPORTS_INHERITED(GfxInfo, GfxInfoBase, nsIGfxInfoDebug)
#endif

GfxInfo::GfxInfo()
    : mInitialized(false),
      mGLStrings(new GLStrings),
      mOSVersionInteger(0),
      mSDKVersion(0) {}

GfxInfo::~GfxInfo() {}

nsresult GfxInfo::GetDWriteEnabled(bool* aEnabled) { return NS_ERROR_FAILURE; }

nsresult GfxInfo::GetHasBattery(bool* aHasBattery) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
GfxInfo::GetDWriteVersion(nsAString& aDwriteVersion) {
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
GfxInfo::GetCleartypeParameters(nsAString& aCleartypeParams) {
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
GfxInfo::GetWindowProtocol(nsAString& aWindowProtocol) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
GfxInfo::GetTestType(nsAString& aTestType) { return NS_ERROR_NOT_IMPLEMENTED; }

void GfxInfo::EnsureInitialized() {
  if (mInitialized) return;

  if (!jni::IsAvailable()) {
    gfxWarning() << "JNI missing during initialization";
    return;
  }

  jni::String::LocalRef model = java::sdk::Build::MODEL();
  mModel = model->ToString();
  mAdapterDescription.AppendPrintf("Model: %s",
                                   NS_LossyConvertUTF16toASCII(mModel).get());

  jni::String::LocalRef product = java::sdk::Build::PRODUCT();
  mProduct = product->ToString();
  mAdapterDescription.AppendPrintf(", Product: %s",
                                   NS_LossyConvertUTF16toASCII(mProduct).get());

  jni::String::LocalRef manufacturer =
      mozilla::java::sdk::Build::MANUFACTURER();
  mManufacturer = manufacturer->ToString();
  mAdapterDescription.AppendPrintf(
      ", Manufacturer: %s", NS_LossyConvertUTF16toASCII(mManufacturer).get());

  mSDKVersion = java::sdk::Build::VERSION::SDK_INT();
  jni::String::LocalRef hardware = java::sdk::Build::HARDWARE();
  mHardware = hardware->ToString();
  mAdapterDescription.AppendPrintf(
      ", Hardware: %s", NS_LossyConvertUTF16toASCII(mHardware).get());

  jni::String::LocalRef release = java::sdk::Build::VERSION::RELEASE();
  mOSVersion = release->ToCString();

  mOSVersionInteger = 0;
  char a[5], b[5], c[5], d[5];
  SplitDriverVersion(mOSVersion.get(), a, b, c, d);
  uint8_t na = atoi(a);
  uint8_t nb = atoi(b);
  uint8_t nc = atoi(c);
  uint8_t nd = atoi(d);

  mOSVersionInteger = (uint32_t(na) << 24) | (uint32_t(nb) << 16) |
                      (uint32_t(nc) << 8) | uint32_t(nd);

  mOSVersionEx.Parse(mOSVersion);

  mAdapterDescription.AppendPrintf(
      ", OpenGL: %s -- %s -- %s", mGLStrings->Vendor().get(),
      mGLStrings->Renderer().get(), mGLStrings->Version().get());

  AddCrashReportAnnotations();
  mInitialized = true;
}

NS_IMETHODIMP
GfxInfo::GetAdapterDescription(nsAString& aAdapterDescription) {
  EnsureInitialized();
  aAdapterDescription = NS_ConvertASCIItoUTF16(mAdapterDescription);
  return NS_OK;
}

NS_IMETHODIMP
GfxInfo::GetAdapterDescription2(nsAString& aAdapterDescription) {
  EnsureInitialized();
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
GfxInfo::GetAdapterRAM(uint32_t* aAdapterRAM) {
  EnsureInitialized();
  *aAdapterRAM = 0;
  return NS_OK;
}

NS_IMETHODIMP
GfxInfo::GetAdapterRAM2(uint32_t* aAdapterRAM) {
  EnsureInitialized();
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
GfxInfo::GetAdapterDriver(nsAString& aAdapterDriver) {
  EnsureInitialized();
  aAdapterDriver.Truncate();
  return NS_OK;
}

NS_IMETHODIMP
GfxInfo::GetAdapterDriver2(nsAString& aAdapterDriver) {
  EnsureInitialized();
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
GfxInfo::GetAdapterDriverVendor(nsAString& aAdapterDriverVendor) {
  EnsureInitialized();
  aAdapterDriverVendor.Truncate();
  return NS_OK;
}

NS_IMETHODIMP
GfxInfo::GetAdapterDriverVendor2(nsAString& aAdapterDriverVendor) {
  EnsureInitialized();
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
GfxInfo::GetAdapterDriverVersion(nsAString& aAdapterDriverVersion) {
  EnsureInitialized();
  aAdapterDriverVersion = NS_ConvertASCIItoUTF16(mGLStrings->Version());
  return NS_OK;
}

NS_IMETHODIMP
GfxInfo::GetAdapterDriverVersion2(nsAString& aAdapterDriverVersion) {
  EnsureInitialized();
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
GfxInfo::GetAdapterDriverDate(nsAString& aAdapterDriverDate) {
  EnsureInitialized();
  aAdapterDriverDate.Truncate();
  return NS_OK;
}

NS_IMETHODIMP
GfxInfo::GetAdapterDriverDate2(nsAString& aAdapterDriverDate) {
  EnsureInitialized();
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
GfxInfo::GetAdapterVendorID(nsAString& aAdapterVendorID) {
  EnsureInitialized();
  aAdapterVendorID = NS_ConvertASCIItoUTF16(mGLStrings->Vendor());
  return NS_OK;
}

NS_IMETHODIMP
GfxInfo::GetAdapterVendorID2(nsAString& aAdapterVendorID) {
  EnsureInitialized();
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
GfxInfo::GetAdapterDeviceID(nsAString& aAdapterDeviceID) {
  EnsureInitialized();
  aAdapterDeviceID = NS_ConvertASCIItoUTF16(mGLStrings->Renderer());
  return NS_OK;
}

NS_IMETHODIMP
GfxInfo::GetAdapterDeviceID2(nsAString& aAdapterDeviceID) {
  EnsureInitialized();
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
GfxInfo::GetAdapterSubsysID(nsAString& aAdapterSubsysID) {
  EnsureInitialized();
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
GfxInfo::GetAdapterSubsysID2(nsAString& aAdapterSubsysID) {
  EnsureInitialized();
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
GfxInfo::GetIsGPU2Active(bool* aIsGPU2Active) {
  EnsureInitialized();
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
GfxInfo::GetDrmRenderDevice(nsACString& aDrmRenderDevice) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

void GfxInfo::AddCrashReportAnnotations() {
  CrashReporter::RecordAnnotationNSCString(
      CrashReporter::Annotation::AdapterVendorID, mGLStrings->Vendor());
  CrashReporter::RecordAnnotationNSCString(
      CrashReporter::Annotation::AdapterDeviceID, mGLStrings->Renderer());
  CrashReporter::RecordAnnotationNSCString(
      CrashReporter::Annotation::AdapterDriverVersion, mGLStrings->Version());
}

const nsTArray<RefPtr<GfxDriverInfo>>& GfxInfo::GetGfxDriverInfo() {
  if (sDriverInfo->IsEmpty()) {
    APPEND_TO_DRIVER_BLOCKLIST2(
        OperatingSystem::Android, DeviceFamily::All,
        nsIGfxInfo::FEATURE_OPENGL_LAYERS, nsIGfxInfo::FEATURE_STATUS_OK,
        DRIVER_COMPARISON_IGNORED, GfxDriverInfo::allDriverVersions,
        "FEATURE_OK_FORCE_OPENGL");
  }

  return *sDriverInfo;
}

nsresult GfxInfo::GetFeatureStatusImpl(
    int32_t aFeature, int32_t* aStatus, nsAString& aSuggestedDriverVersion,
    const nsTArray<RefPtr<GfxDriverInfo>>& aDriverInfo, nsACString& aFailureId,
    OperatingSystem* aOS /* = nullptr */) {
  NS_ENSURE_ARG_POINTER(aStatus);
  aSuggestedDriverVersion.SetIsVoid(true);
  *aStatus = nsIGfxInfo::FEATURE_STATUS_UNKNOWN;
  OperatingSystem os = OperatingSystem::Android;
  if (aOS) *aOS = os;

  if (sShutdownOccurred) {
    return NS_OK;
  }

  // OpenGL layers are never blocklisted on Android.
  // This early return is so we avoid potentially slow
  // GLStrings initialization on startup when we initialize GL layers.
  if (aFeature == nsIGfxInfo::FEATURE_OPENGL_LAYERS) {
    *aStatus = nsIGfxInfo::FEATURE_STATUS_OK;
    return NS_OK;
  }

  EnsureInitialized();

  if (mGLStrings->Vendor().IsEmpty() || mGLStrings->Renderer().IsEmpty()) {
    if (OnlyAllowFeatureOnKnownConfig(aFeature)) {
      *aStatus = nsIGfxInfo::FEATURE_BLOCKED_DEVICE;
      aFailureId = "FEATURE_FAILURE_EMPTY_VENDOR_OR_RENDERER";
    } else {
      *aStatus = nsIGfxInfo::FEATURE_STATUS_OK;
    }
    return NS_OK;
  }

  // Don't evaluate special cases when evaluating the downloaded blocklist.
  if (aDriverInfo.IsEmpty()) {
    if (aFeature == nsIGfxInfo::FEATURE_CANVAS2D_ACCELERATION) {
      if (mGLStrings->Renderer().Find("Vivante GC1000") != -1) {
        // Blocklist Vivante GC1000. See bug 1248183.
        *aStatus = nsIGfxInfo::FEATURE_BLOCKED_DEVICE;
        aFailureId = "FEATURE_FAILED_CANVAS_2D_HW";
      } else {
        *aStatus = nsIGfxInfo::FEATURE_STATUS_OK;
      }
      return NS_OK;
    }

    if (aFeature == FEATURE_WEBGL_OPENGL) {
      if (mGLStrings->Renderer().Find("Adreno 200") != -1 ||
          mGLStrings->Renderer().Find("Adreno 205") != -1) {
        *aStatus = nsIGfxInfo::FEATURE_BLOCKED_DEVICE;
        aFailureId = "FEATURE_FAILURE_ADRENO_20x";
        return NS_OK;
      }

      if (mHardware.EqualsLiteral("ville")) {
        *aStatus = nsIGfxInfo::FEATURE_BLOCKED_DEVICE;
        aFailureId = "FEATURE_FAILURE_VILLE";
        return NS_OK;
      }
    }

    if (aFeature == FEATURE_WEBRTC_HW_ACCELERATION_ENCODE) {
      if (jni::IsAvailable()) {
        *aStatus = WebRtcHwVp8EncodeSupported();
        aFailureId = "FEATURE_FAILURE_WEBRTC_ENCODE";
        return NS_OK;
      }
    }
    if (aFeature == FEATURE_WEBRTC_HW_ACCELERATION_DECODE) {
      if (jni::IsAvailable()) {
        *aStatus = WebRtcHwVp8DecodeSupported();
        aFailureId = "FEATURE_FAILURE_WEBRTC_DECODE";
        return NS_OK;
      }
    }
    if (aFeature == FEATURE_WEBRTC_HW_ACCELERATION_H264) {
      if (jni::IsAvailable()) {
        *aStatus = WebRtcHwH264Supported();
        aFailureId = "FEATURE_FAILURE_WEBRTC_H264";
        return NS_OK;
      }
    }
    if (aFeature == FEATURE_VP8_HW_DECODE ||
        aFeature == FEATURE_VP9_HW_DECODE) {
      NS_LossyConvertUTF16toASCII model(mModel);
      bool isBlocked =
          // GIFV crash, see bug 1232911.
          model.Equals("GT-N8013", nsCaseInsensitiveCStringComparator);

      if (isBlocked) {
        *aStatus = nsIGfxInfo::FEATURE_BLOCKED_DEVICE;
        aFailureId = "FEATURE_FAILURE_VPx";
      } else {
        *aStatus = nsIGfxInfo::FEATURE_STATUS_OK;
      }
      return NS_OK;
    }

    if (aFeature == FEATURE_WEBRENDER) {
      const bool isMali4xx =
          mGLStrings->Renderer().LowerCaseFindASCII("mali-4") >= 0;

      const bool isPowerVrG6110 =
          mGLStrings->Renderer().LowerCaseFindASCII("powervr rogue g6110") >= 0;

      const bool isVivanteGC7000UL =
          mGLStrings->Renderer().LowerCaseFindASCII("vivante gc7000ul") >= 0;

      const bool isPowerVrFenceSyncCrash =
          (mGLStrings->Renderer().LowerCaseFindASCII("powervr rogue g6200") >=
               0 ||
           mGLStrings->Renderer().LowerCaseFindASCII("powervr rogue g6430") >=
               0 ||
           mGLStrings->Renderer().LowerCaseFindASCII("powervr rogue gx6250") >=
               0) &&
          (mGLStrings->Version().Find("3283119") >= 0 ||
           mGLStrings->Version().Find("3443629") >= 0 ||
           mGLStrings->Version().Find("3573678") >= 0 ||
           mGLStrings->Version().Find("3830101") >= 0);

      if (isMali4xx) {
        // Mali 4xx does not support GLES 3.
        *aStatus = nsIGfxInfo::FEATURE_BLOCKED_DEVICE;
        aFailureId = "FEATURE_FAILURE_NO_GLES_3";
      } else if (isPowerVrG6110) {
        // Blocked on PowerVR Rogue G6110 due to bug 1742986 and bug 1717863.
        *aStatus = nsIGfxInfo::FEATURE_BLOCKED_DEVICE;
        aFailureId = "FEATURE_FAILURE_POWERVR_G6110";
      } else if (isVivanteGC7000UL) {
        // Blocked on Vivante GC7000UL due to bug 1719327.
        *aStatus = nsIGfxInfo::FEATURE_BLOCKED_DEVICE;
        aFailureId = "FEATURE_FAILURE_VIVANTE_GC7000UL";
      } else if (isPowerVrFenceSyncCrash) {
        // Blocked on various PowerVR GPUs due to bug 1773128.
        *aStatus = nsIGfxInfo::FEATURE_BLOCKED_DEVICE;
        aFailureId = "FEATURE_FAILURE_POWERVR_FENCE_SYNC_CRASH";
      } else {
        *aStatus = nsIGfxInfo::FEATURE_STATUS_OK;
      }
      return NS_OK;
    }

    if (aFeature == FEATURE_WEBRENDER_SCISSORED_CACHE_CLEARS) {
      // Emulator with SwiftShader is buggy when attempting to clear picture
      // cache textures with a scissor rect set.
      const bool isEmulatorSwiftShader =
          mGLStrings->Renderer().Find(
              "Android Emulator OpenGL ES Translator (Google SwiftShader)") >=
          0;
      if (isEmulatorSwiftShader) {
        *aStatus = nsIGfxInfo::FEATURE_BLOCKED_DEVICE;
        aFailureId = "FEATURE_FAILURE_BUG_1603515";
      } else {
        *aStatus = nsIGfxInfo::FEATURE_STATUS_OK;
      }
      return NS_OK;
    }

    if (aFeature == FEATURE_WEBRENDER_SHADER_CACHE) {
      // Program binaries are known to be buggy on Adreno 3xx. While we haven't
      // encountered any correctness or stability issues with them, loading them
      // fails more often than not, so is a waste of time. Better to just not
      // even attempt to cache them. See bug 1615574.
      const bool isAdreno3xx =
          mGLStrings->Renderer().LowerCaseFindASCII("adreno (tm) 3") >= 0;
      if (isAdreno3xx) {
        *aStatus = nsIGfxInfo::FEATURE_BLOCKED_DEVICE;
        aFailureId = "FEATURE_FAILURE_ADRENO_3XX";
      } else {
        *aStatus = nsIGfxInfo::FEATURE_STATUS_OK;
      }
    }

    if (aFeature == FEATURE_WEBRENDER_OPTIMIZED_SHADERS) {
      // Optimized shaders result in completely broken rendering on some Mali-T
      // devices. We have seen this on T6xx, T7xx, and T8xx on android versions
      // up to 5.1, and on T6xx on versions up to android 7.1. As a precaution
      // disable for all Mali-T regardless of version. See bug 1689064 and bug
      // 1707283 for details.
      const bool isMaliT =
          mGLStrings->Renderer().LowerCaseFindASCII("mali-t") >= 0;
      if (isMaliT) {
        *aStatus = nsIGfxInfo::FEATURE_BLOCKED_DEVICE;
        aFailureId = "FEATURE_FAILURE_BUG_1689064";
      } else {
        *aStatus = nsIGfxInfo::FEATURE_STATUS_OK;
      }
      return NS_OK;
    }

    if (aFeature == FEATURE_WEBRENDER_PARTIAL_PRESENT) {
      // Block partial present on some devices due to rendering issues.
      // On Mali-Txxx due to bug 1680087 and bug 1707815.
      // On Adreno 3xx GPUs due to bug 1695771.
      const bool isMaliT =
          mGLStrings->Renderer().LowerCaseFindASCII("mali-t") >= 0;
      const bool isAdreno3xx =
          mGLStrings->Renderer().LowerCaseFindASCII("adreno (tm) 3") >= 0;
      if (isMaliT || isAdreno3xx) {
        *aStatus = nsIGfxInfo::FEATURE_BLOCKED_DEVICE;
        aFailureId = "FEATURE_FAILURE_BUG_1680087_1695771_1707815";
      } else {
        *aStatus = nsIGfxInfo::FEATURE_STATUS_OK;
      }
      return NS_OK;
    }
  }

  if (aFeature == FEATURE_GL_SWIZZLE) {
    // Swizzling appears to be buggy on PowerVR Rogue devices with webrender.
    // See bug 1704783.
    const bool isPowerVRRogue =
        mGLStrings->Renderer().LowerCaseFindASCII("powervr rogue") >= 0;
    if (isPowerVRRogue) {
      *aStatus = nsIGfxInfo::FEATURE_BLOCKED_DEVICE;
      aFailureId = "FEATURE_FAILURE_POWERVR_ROGUE";
    } else {
      *aStatus = nsIGfxInfo::FEATURE_STATUS_OK;
    }
    return NS_OK;
  }

  if (aFeature == FEATURE_WEBGPU) {
    // Ensure WebGPU is disabled by default on Android until it is better
    // tested.
    *aStatus = nsIGfxInfo::FEATURE_BLOCKED_DEVICE;
    aFailureId = "FEATURE_FAILURE_WEBGPU_ANDROID";
    return NS_OK;
  }

  if (aFeature == FEATURE_GL_NORM16_TEXTURES) {
    const auto& extensions = mGLStrings->Extensions();
    // GL_EXT_texture_norm16 is required for GL_R16, GL_RG16, etc texture
    // formats on OpenGL ES.
    if (!extensions.Contains("GL_EXT_texture_norm16")) {
      *aStatus = nsIGfxInfo::FEATURE_BLOCKED_DEVICE;
      aFailureId = "FEATURE_FAILURE_MISSING_EXTENSION";
    } else {
      *aStatus = nsIGfxInfo::FEATURE_STATUS_OK;
    }
  }

  return GfxInfoBase::GetFeatureStatusImpl(
      aFeature, aStatus, aSuggestedDriverVersion, aDriverInfo, aFailureId, &os);
}

static nsCString FeatureCacheOsVerPrefName(int32_t aFeature) {
  nsCString osPrefName;
  osPrefName.AppendASCII("gfxinfo.cache.");
  osPrefName.AppendInt(aFeature);
  osPrefName.AppendASCII(".osver");
  return osPrefName;
}

static nsCString FeatureCacheAppVerPrefName(int32_t aFeature) {
  nsCString osPrefName;
  osPrefName.AppendASCII("gfxinfo.cache.");
  osPrefName.AppendInt(aFeature);
  osPrefName.AppendASCII(".appver");
  return osPrefName;
}

static nsCString FeatureCacheValuePrefName(int32_t aFeature) {
  nsCString osPrefName;
  osPrefName.AppendASCII("gfxinfo.cache.");
  osPrefName.AppendInt(aFeature);
  osPrefName.AppendASCII(".value");
  return osPrefName;
}

static bool GetCachedFeatureVal(int32_t aFeature, uint32_t aExpectedOsVer,
                                const nsCString& aCurrentAppVer,
                                int32_t& aOutStatus) {
  uint32_t osVer = 0;
  nsresult rv =
      Preferences::GetUint(FeatureCacheOsVerPrefName(aFeature).get(), &osVer);
  if (NS_FAILED(rv) || osVer != aExpectedOsVer) {
    return false;
  }
  // Bug 1804287 requires we invalidate cached values for new builds to allow
  // for code changes to modify the features support.
  nsAutoCString cachedAppVersion;
  rv = Preferences::GetCString(FeatureCacheAppVerPrefName(aFeature).get(),
                               cachedAppVersion);
  if (NS_FAILED(rv) || !aCurrentAppVer.Equals(cachedAppVersion)) {
    return false;
  }
  int32_t status = 0;
  rv = Preferences::GetInt(FeatureCacheValuePrefName(aFeature).get(), &status);
  if (NS_FAILED(rv)) {
    return false;
  }
  aOutStatus = status;
  return true;
}

static void SetCachedFeatureVal(int32_t aFeature, uint32_t aOsVer,
                                const nsCString& aCurrentAppVer,
                                int32_t aStatus) {
  // Ignore failures; not much we can do anyway.
  Preferences::SetUint(FeatureCacheOsVerPrefName(aFeature).get(), aOsVer);
  Preferences::SetCString(FeatureCacheAppVerPrefName(aFeature).get(),
                          aCurrentAppVer);
  Preferences::SetInt(FeatureCacheValuePrefName(aFeature).get(), aStatus);
}

int32_t GfxInfo::WebRtcHwVp8EncodeSupported() {
  MOZ_ASSERT(jni::IsAvailable());

  // The Android side of this calculation is very slow, so we cache the result
  // in preferences, invalidating if the OS version changes.

  int32_t status = 0;
  const auto& currentAppVersion = GfxInfoBase::GetApplicationVersion();
  if (GetCachedFeatureVal(FEATURE_WEBRTC_HW_ACCELERATION_ENCODE,
                          mOSVersionInteger, currentAppVersion, status)) {
    return status;
  }

  status = java::HardwareCodecCapabilityUtils::HasHWVP8(true)
               ? nsIGfxInfo::FEATURE_STATUS_OK
               : nsIGfxInfo::FEATURE_BLOCKED_DEVICE;

  SetCachedFeatureVal(FEATURE_WEBRTC_HW_ACCELERATION_ENCODE, mOSVersionInteger,
                      currentAppVersion, status);

  return status;
}

int32_t GfxInfo::WebRtcHwVp8DecodeSupported() {
  MOZ_ASSERT(jni::IsAvailable());

  // The Android side of this caclulation is very slow, so we cache the result
  // in preferences, invalidating if the OS version changes.

  int32_t status = 0;
  const auto& appVersion = GfxInfoBase::GetApplicationVersion();
  if (GetCachedFeatureVal(FEATURE_WEBRTC_HW_ACCELERATION_DECODE,
                          mOSVersionInteger, appVersion, status)) {
    return status;
  }

  status = java::HardwareCodecCapabilityUtils::HasHWVP8(false)
               ? nsIGfxInfo::FEATURE_STATUS_OK
               : nsIGfxInfo::FEATURE_BLOCKED_DEVICE;

  SetCachedFeatureVal(FEATURE_WEBRTC_HW_ACCELERATION_DECODE, mOSVersionInteger,
                      appVersion, status);

  return status;
}

int32_t GfxInfo::WebRtcHwH264Supported() {
  MOZ_ASSERT(jni::IsAvailable());

  // The Android side of this calculation is very slow, so we cache the result
  // in preferences, invalidating if the OS version changes.

  int32_t status = 0;
  const auto& currentAppVersion = GfxInfoBase::GetApplicationVersion();
  if (GetCachedFeatureVal(FEATURE_WEBRTC_HW_ACCELERATION_H264,
                          mOSVersionInteger, currentAppVersion, status)) {
    return status;
  }

  status = java::HardwareCodecCapabilityUtils::HasHWH264()
               ? nsIGfxInfo::FEATURE_STATUS_OK
               : nsIGfxInfo::FEATURE_BLOCKED_DEVICE;

  SetCachedFeatureVal(FEATURE_WEBRTC_HW_ACCELERATION_H264, mOSVersionInteger,
                      currentAppVersion, status);

  return status;
}

#ifdef DEBUG

// Implement nsIGfxInfoDebug

NS_IMETHODIMP GfxInfo::SpoofVendorID(const nsAString& aVendorID) {
  mGLStrings->SpoofVendor(NS_LossyConvertUTF16toASCII(aVendorID));
  return NS_OK;
}

NS_IMETHODIMP GfxInfo::SpoofDeviceID(const nsAString& aDeviceID) {
  mGLStrings->SpoofRenderer(NS_LossyConvertUTF16toASCII(aDeviceID));
  return NS_OK;
}

NS_IMETHODIMP GfxInfo::SpoofDriverVersion(const nsAString& aDriverVersion) {
  mGLStrings->SpoofVersion(NS_LossyConvertUTF16toASCII(aDriverVersion));
  return NS_OK;
}

NS_IMETHODIMP GfxInfo::SpoofOSVersion(uint32_t aVersion) {
  EnsureInitialized();
  mOSVersion = aVersion;
  return NS_OK;
}

NS_IMETHODIMP GfxInfo::SpoofOSVersionEx(uint32_t aMajor, uint32_t aMinor,
                                        uint32_t aBuild, uint32_t aRevision) {
  EnsureInitialized();
  mOSVersionEx = GfxVersionEx(aMajor, aMinor, aBuild, aRevision);
  return NS_OK;
}

#endif

nsString GfxInfo::Model() {
  EnsureInitialized();
  return mModel;
}

nsString GfxInfo::Hardware() {
  EnsureInitialized();
  return mHardware;
}

nsString GfxInfo::Product() {
  EnsureInitialized();
  return mProduct;
}

nsString GfxInfo::Manufacturer() {
  EnsureInitialized();
  return mManufacturer;
}

uint32_t GfxInfo::OperatingSystemVersion() {
  EnsureInitialized();
  return mOSVersionInteger;
}

GfxVersionEx GfxInfo::OperatingSystemVersionEx() {
  EnsureInitialized();
  return mOSVersionEx;
}

void GfxInfo::ReportGLStrings(gfx::GfxInfoGLStrings&& aStrings) {
  EnsureInitialized();
  // Sanity check the provided strings match our cached values.
  if (aStrings.vendor() != mGLStrings->Vendor() ||
      aStrings.renderer() != mGLStrings->Renderer() ||
      aStrings.version() != mGLStrings->Version() ||
      aStrings.extensions() != mGLStrings->Extensions()) {
    gfxCriticalNoteOnce << "Received unexpected GLStrings: "
                        << aStrings.vendor().get() << ", "
                        << aStrings.renderer().get() << ", "
                        << aStrings.version().get();

    // Update the cache if they differ.
    mGLStrings->CacheStrings(aStrings.vendor(), aStrings.renderer(),
                             aStrings.version(), aStrings.extensions());
  }
}

}  // namespace widget
}  // namespace mozilla
