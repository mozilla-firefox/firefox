/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/gfx/gfxConfigManager.h"

#include "gfxConfig.h"
#include "gfxPlatform.h"
#include "mozilla/Components.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/StaticPrefs_layers.h"
#include "mozilla/gfx/gfxVars.h"
#include "nsIGfxInfo.h"
#include "nsPrintfCString.h"
#include "nsXULAppAPI.h"

#ifdef XP_WIN
#  include "mozilla/gfx/DeviceManagerDx.h"
#  include "mozilla/gfx/DisplayConfigWindows.h"
#endif

namespace mozilla {
namespace gfx {

void gfxConfigManager::Init() {
  MOZ_ASSERT(XRE_IsParentProcess());

  EmplaceUserPref("gfx.webrender.compositor", mWrCompositorEnabled);
  mWrLayerCompositorEnabled = StaticPrefs::gfx_webrender_layer_compositor();
  mWrForceEnabled = gfxPlatform::WebRenderPrefEnabled();
  mWrSoftwareForceEnabled = StaticPrefs::gfx_webrender_software_AtStartup();
  mWrCompositorForceEnabled =
      StaticPrefs::gfx_webrender_compositor_force_enabled_AtStartup();
  mWrForcePartialPresent =
      StaticPrefs::gfx_webrender_force_partial_present_AtStartup();
  mWrPartialPresent =
      StaticPrefs::gfx_webrender_max_partial_present_rects_AtStartup() > 0;
  EmplaceUserPref(StaticPrefs::GetPrefName_gfx_webrender_program_binary_disk(),
                  mWrShaderCache);
  mWrOptimizedShaders =
      StaticPrefs::gfx_webrender_use_optimized_shaders_AtStartup();
  mWrScissoredCacheClearsEnabled =
      StaticPrefs::gfx_webrender_scissored_cache_clears_enabled_AtStartup();
  mWrScissoredCacheClearsForceEnabled = StaticPrefs::
      gfx_webrender_scissored_cache_clears_force_enabled_AtStartup();
  EmplaceUserPref(StaticPrefs::GetPrefName_gfx_webrender_enable_angle(),
                  mWrAngleEnabled);
#ifdef XP_WIN
  mWrRequireAngle = StaticPrefs::gfx_webrender_require_angle_AtStartup();
  mWrDCompWinEnabled = StaticPrefs::gfx_webrender_dcomp_win_enabled_AtStartup();
#endif

  mWrEnvForceEnabled = gfxPlatform::WebRenderEnvvarEnabled();

#ifdef XP_WIN
  DeviceManagerDx::Get()->CheckHardwareStretchingSupport(mHwStretchingSupport);
  mScaledResolution = HasScaledResolution();
  mWrCompositorDCompRequired = true;
#else
  ++mHwStretchingSupport.mBoth;
#endif

#ifdef MOZ_WIDGET_GTK
  mDisableHwCompositingNoWr = true;
#endif

#ifdef NIGHTLY_BUILD
  mIsNightly = true;
#endif
#ifdef EARLY_BETA_OR_EARLIER
  mIsEarlyBetaOrEarlier = true;
#endif
  mSafeMode = gfxPlatform::InSafeMode();

  mGfxInfo = components::GfxInfo::Service();

  mFeatureWr = &gfxConfig::GetFeature(Feature::WEBRENDER);
  mFeatureWrCompositor = &gfxConfig::GetFeature(Feature::WEBRENDER_COMPOSITOR);
  mFeatureWrAngle = &gfxConfig::GetFeature(Feature::WEBRENDER_ANGLE);
  mFeatureWrDComp = &gfxConfig::GetFeature(Feature::WEBRENDER_DCOMP_PRESENT);
  mFeatureWrPartial = &gfxConfig::GetFeature(Feature::WEBRENDER_PARTIAL);
  mFeatureWrShaderCache =
      &gfxConfig::GetFeature(Feature::WEBRENDER_SHADER_CACHE);
  mFeatureWrOptimizedShaders =
      &gfxConfig::GetFeature(Feature::WEBRENDER_OPTIMIZED_SHADERS);
  mFeatureWrScissoredCacheClears =
      &gfxConfig::GetFeature(Feature::WEBRENDER_SCISSORED_CACHE_CLEARS);

  mFeatureHwCompositing = &gfxConfig::GetFeature(Feature::HW_COMPOSITING);
#ifdef XP_WIN
  mFeatureD3D11HwAngle = &gfxConfig::GetFeature(Feature::D3D11_HW_ANGLE);
  mFeatureWrAngleBackend = mFeatureD3D11HwAngle;
  mFeatureD3D11Compositing = &gfxConfig::GetFeature(Feature::D3D11_COMPOSITING);
#elif defined(XP_MACOSX)
  mFeatureMetalAngle = &gfxConfig::GetFeature(Feature::WEBRENDER_ANGLE_METAL);
  mFeatureWrAngleBackend = mFeatureMetalAngle;
#elif defined(MOZ_WIDGET_ANDROID)
  mFeatureGlAngle = &gfxConfig::GetFeature(Feature::WEBRENDER_ANGLE_GL);
  mFeatureWrAngleBackend = mFeatureGlAngle;
#endif
  mFeatureGPUProcess = &gfxConfig::GetFeature(Feature::GPU_PROCESS);
  mFeatureGLNorm16Textures =
      &gfxConfig::GetFeature(Feature::GL_NORM16_TEXTURES);
}

void gfxConfigManager::EmplaceUserPref(const char* aPrefName,
                                       Maybe<bool>& aValue) {
  if (Preferences::HasUserValue(aPrefName)) {
    aValue.emplace(Preferences::GetBool(aPrefName, false));
  }
}

void gfxConfigManager::ConfigureFromBlocklist(long aFeature,
                                              FeatureState* aFeatureState) {
  MOZ_ASSERT(aFeatureState);

  nsCString blockId;
  int32_t status;
  if (!NS_SUCCEEDED(mGfxInfo->GetFeatureStatus(aFeature, blockId, &status))) {
    aFeatureState->Disable(FeatureStatus::BlockedNoGfxInfo, "gfxInfo is broken",
                           "FEATURE_FAILURE_NO_GFX_INFO"_ns);
    return;
  }

  switch (status) {
    case nsIGfxInfo::FEATURE_STATUS_OK:
    case nsIGfxInfo::FEATURE_ALLOW_ALWAYS:
      break;
    case nsIGfxInfo::FEATURE_ALLOW_QUALIFIED:
      MOZ_ASSERT_UNREACHABLE("Allowing only qualified, but needs experiment?");
      break;
    case nsIGfxInfo::FEATURE_DENIED:
      aFeatureState->Disable(FeatureStatus::Denied, "Not on allowlist",
                             blockId);
      break;
    default:
      aFeatureState->Disable(FeatureStatus::Blocklisted,
                             "Blocklisted by gfxInfo", blockId);
      break;
  }
}

void gfxConfigManager::ConfigureWebRender() {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(mFeatureWr);
  MOZ_ASSERT(mFeatureWrCompositor);
  MOZ_ASSERT(mFeatureWrAngle);
  MOZ_ASSERT(mFeatureWrDComp);
  MOZ_ASSERT(mFeatureWrPartial);
  MOZ_ASSERT(mFeatureWrShaderCache);
  MOZ_ASSERT(mFeatureWrOptimizedShaders);
  MOZ_ASSERT(mFeatureWrScissoredCacheClears);
  MOZ_ASSERT(mFeatureHwCompositing);
  MOZ_ASSERT(mFeatureGPUProcess);
  MOZ_ASSERT(mFeatureGLNorm16Textures);

  // Initialize WebRender native compositor usage
  mFeatureWrCompositor->SetDefaultFromPref("gfx.webrender.compositor", true,
                                           false, mWrCompositorEnabled);

  if (mWrCompositorForceEnabled) {
    mFeatureWrCompositor->UserForceEnable("Force enabled by pref");
  }

  ConfigureFromBlocklist(nsIGfxInfo::FEATURE_WEBRENDER_COMPOSITOR,
                         mFeatureWrCompositor);

  // Disable native compositor when hardware stretching is not supported. It is
  // for avoiding a problem like Bug 1618370.
  // XXX Is there a better check for Bug 1618370?
  // Skip this restriction when the layer compositor is enabled.
  if (!mWrLayerCompositorEnabled && !mHwStretchingSupport.IsFullySupported() &&
      mScaledResolution) {
    nsPrintfCString failureId(
        "FEATURE_FAILURE_NO_HARDWARE_STRETCHING_B%uW%uF%uN%uE%u",
        mHwStretchingSupport.mBoth, mHwStretchingSupport.mWindowOnly,
        mHwStretchingSupport.mFullScreenOnly, mHwStretchingSupport.mNone,
        mHwStretchingSupport.mError);
    mFeatureWrCompositor->Disable(FeatureStatus::Unavailable,
                                  "No hardware stretching support", failureId);
  }

  mFeatureWr->EnableByDefault();

  // envvar works everywhere; note that we need this for testing in CI.
  // Prior to bug 1523788, the `prefEnabled` check was only done on Nightly,
  // so as to prevent random users from easily enabling WebRender on
  // unqualified hardware in beta/release.
  if (mWrSoftwareForceEnabled) {
    mFeatureWr->UserDisable("User force-enabled software WR",
                            "FEATURE_FAILURE_USER_FORCE_ENABLED_SW_WR"_ns);
  } else if (mWrEnvForceEnabled) {
    mFeatureWr->UserForceEnable("Force enabled by envvar");
  } else if (mWrForceEnabled) {
    mFeatureWr->UserForceEnable("Force enabled by pref");
  }

  ConfigureFromBlocklist(nsIGfxInfo::FEATURE_WEBRENDER, mFeatureWr);

  // HW_COMPOSITING being disabled implies interfacing with the GPU might break
  if (!mFeatureHwCompositing->IsEnabled()) {
    mFeatureWr->ForceDisable(FeatureStatus::UnavailableNoHwCompositing,
                             "Hardware compositing is disabled",
                             "FEATURE_FAILURE_WEBRENDER_NEED_HWCOMP"_ns);
  }

  if (mSafeMode) {
    mFeatureWr->ForceDisable(FeatureStatus::UnavailableInSafeMode,
                             "Safe-mode is enabled",
                             "FEATURE_FAILURE_SAFE_MODE"_ns);
  }

  if (mFeatureMetalAngle) {
    mFeatureMetalAngle->EnableByDefault();
    ConfigureFromBlocklist(nsIGfxInfo::FEATURE_WEBRENDER_ANGLE_METAL,
                           mFeatureMetalAngle);
  }

  if (mFeatureGlAngle) {
    mFeatureGlAngle->EnableByDefault();
    ConfigureFromBlocklist(nsIGfxInfo::FEATURE_WEBRENDER_ANGLE_GL,
                           mFeatureGlAngle);
  }

  mFeatureWrAngle->SetDefaultFromPref(
      StaticPrefs::GetPrefName_gfx_webrender_enable_angle(), true,
      StaticPrefs::GetPrefDefault_gfx_webrender_enable_angle(),
      mWrAngleEnabled);
  if (!mFeatureWrAngleBackend) {
    mFeatureWrAngle->Disable(FeatureStatus::Unavailable, "OS not supported",
                             "FEATURE_FAILURE_OS_NOT_SUPPORTED"_ns);
  } else if (!mFeatureWrAngleBackend->IsEnabled()) {
    mFeatureWrAngle->ForceDisable(FeatureStatus::UnavailableNoAngle,
                                  "ANGLE backend is disabled",
                                  mFeatureWrAngleBackend->GetFailureId());
  }

  if (mWrRequireAngle && mFeatureWr->IsEnabled() &&
      !mFeatureWrAngle->IsEnabled()) {
    // Ensure we disable WebRender if ANGLE is unavailable and it is required.
    mFeatureWr->ForceDisable(FeatureStatus::UnavailableNoAngle,
                             "ANGLE is disabled",
                             mFeatureWrAngle->GetFailureId());
  }

  if (!mFeatureWr->IsEnabled() && mDisableHwCompositingNoWr) {
    if (mFeatureHwCompositing->IsEnabled()) {
      // Hardware compositing should be disabled by default if we aren't using
      // WebRender. We had to check if it is enabled at all, because it may
      // already have been forced disabled (e.g. safe mode, headless). It may
      // still be forced on by the user, and if so, this should have no effect.
      mFeatureHwCompositing->Disable(FeatureStatus::Blocked,
                                     "Acceleration blocked by platform", ""_ns);
    }
  }

  mFeatureWrDComp->EnableByDefault();

  if (!mWrDCompWinEnabled) {
    mFeatureWrDComp->UserDisable("User disabled via pref",
                                 "FEATURE_FAILURE_DCOMP_PREF_DISABLED"_ns);
  }

  ConfigureFromBlocklist(nsIGfxInfo::FEATURE_WEBRENDER_DCOMP, mFeatureWrDComp);

  if (!mFeatureGPUProcess->IsEnabled()) {
    mFeatureWrDComp->Disable(FeatureStatus::Unavailable, "Requires GPU process",
                             "FEATURE_FAILURE_NO_GPU_PROCESS"_ns);
  }

  mFeatureWrDComp->MaybeSetFailed(
      mFeatureWr->IsEnabled(), FeatureStatus::Unavailable, "Requires WebRender",
      "FEATURE_FAILURE_DCOMP_NOT_WR"_ns);
  mFeatureWrDComp->MaybeSetFailed(mFeatureWrAngle->IsEnabled(),
                                  FeatureStatus::Unavailable, "Requires ANGLE",
                                  "FEATURE_FAILURE_DCOMP_NOT_ANGLE"_ns);

  if (!mFeatureWrDComp->IsEnabled() && mWrCompositorDCompRequired) {
    mFeatureWrCompositor->ForceDisable(FeatureStatus::Unavailable,
                                       "No DirectComposition usage",
                                       mFeatureWrDComp->GetFailureId());
  }

  // Initialize WebRender partial present config.
  // Partial present applies to the window and to OS layer swapchains, but not
  // to the tiles of the native compositor.
  mFeatureWrPartial->SetDefault(mWrPartialPresent, FeatureStatus::Disabled,
                                "User disabled via pref");
  if (mWrForcePartialPresent) {
    mFeatureWrPartial->UserForceEnable("Force enabled by pref");
  }

  ConfigureFromBlocklist(nsIGfxInfo::FEATURE_WEBRENDER_PARTIAL_PRESENT,
                         mFeatureWrPartial);

  mFeatureWrShaderCache->SetDefaultFromPref(
      StaticPrefs::GetPrefName_gfx_webrender_program_binary_disk(), true,
      StaticPrefs::GetPrefDefault_gfx_webrender_program_binary_disk(),
      mWrShaderCache);
  ConfigureFromBlocklist(nsIGfxInfo::FEATURE_WEBRENDER_SHADER_CACHE,
                         mFeatureWrShaderCache);
  if (!mFeatureWr->IsEnabled()) {
    mFeatureWrShaderCache->ForceDisable(FeatureStatus::Unavailable,
                                        "WebRender disabled",
                                        "FEATURE_FAILURE_WR_DISABLED"_ns);
  }

  mFeatureWrOptimizedShaders->EnableByDefault();
  if (!mWrOptimizedShaders) {
    mFeatureWrOptimizedShaders->UserDisable("User disabled via pref",
                                            "FEATURE_FAILURE_PREF_DISABLED"_ns);
  }
  ConfigureFromBlocklist(nsIGfxInfo::FEATURE_WEBRENDER_OPTIMIZED_SHADERS,
                         mFeatureWrOptimizedShaders);
  if (!mFeatureWr->IsEnabled()) {
    mFeatureWrOptimizedShaders->ForceDisable(FeatureStatus::Unavailable,
                                             "WebRender disabled",
                                             "FEATURE_FAILURE_WR_DISABLED"_ns);
  }

  mFeatureWrScissoredCacheClears->SetDefault(mWrScissoredCacheClearsEnabled,
                                             FeatureStatus::Disabled,
                                             "User disabled via pref");
  if (mWrScissoredCacheClearsForceEnabled) {
    mFeatureWrScissoredCacheClears->UserForceEnable("Force enabled by pref");
  }
  ConfigureFromBlocklist(nsIGfxInfo::FEATURE_WEBRENDER_SCISSORED_CACHE_CLEARS,
                         mFeatureWrScissoredCacheClears);

  mFeatureGLNorm16Textures->EnableByDefault();
  ConfigureFromBlocklist(nsIGfxInfo::FEATURE_GL_NORM16_TEXTURES,
                         mFeatureGLNorm16Textures);
}

}  // namespace gfx
}  // namespace mozilla
