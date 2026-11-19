/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef XP_WIN
#  error "Oculus support only available for Windows"
#endif

#include "OculusSession.h"

#include <d3d11.h>
#include <math.h>

#include "mozilla/SharedLibrary.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/dom/GamepadBinding.h"
#include "mozilla/dom/GamepadEventTypes.h"
#include "mozilla/gfx/DeviceManagerDx.h"
#include "mozilla/gfx/Logging.h"

/** XXX The DX11 objects and quad blitting could be encapsulated
 *    into a separate object if either Oculus starts supporting
 *     non-Windows platforms or the blit is needed by other HMD\
 *     drivers.
 *     Alternately, we could remove the extra blit for
 *     Oculus as well with some more refactoring.
 */

// See CompositorD3D11Shaders.h
namespace mozilla {
namespace layers {
struct ShaderBytes {
  const void* mData;
  size_t mLength;
};
extern ShaderBytes sRGBShader;
extern ShaderBytes sLayerQuadVS;
}  // namespace layers
}  // namespace mozilla

using namespace mozilla;
using namespace mozilla::gfx;
using namespace mozilla::layers;

namespace {

static pfn_ovr_Initialize ovr_Initialize = nullptr;
static pfn_ovr_Shutdown ovr_Shutdown = nullptr;
static pfn_ovr_GetLastErrorInfo ovr_GetLastErrorInfo = nullptr;
static pfn_ovr_GetVersionString ovr_GetVersionString = nullptr;
static pfn_ovr_TraceMessage ovr_TraceMessage = nullptr;
static pfn_ovr_IdentifyClient ovr_IdentifyClient = nullptr;
static pfn_ovr_GetHmdDesc ovr_GetHmdDesc = nullptr;
static pfn_ovr_GetTrackerCount ovr_GetTrackerCount = nullptr;
static pfn_ovr_GetTrackerDesc ovr_GetTrackerDesc = nullptr;
static pfn_ovr_Create ovr_Create = nullptr;
static pfn_ovr_Destroy ovr_Destroy = nullptr;
static pfn_ovr_GetSessionStatus ovr_GetSessionStatus = nullptr;
static pfn_ovr_IsExtensionSupported ovr_IsExtensionSupported = nullptr;
static pfn_ovr_EnableExtension ovr_EnableExtension = nullptr;
static pfn_ovr_SetTrackingOriginType ovr_SetTrackingOriginType = nullptr;
static pfn_ovr_GetTrackingOriginType ovr_GetTrackingOriginType = nullptr;
static pfn_ovr_RecenterTrackingOrigin ovr_RecenterTrackingOrigin = nullptr;
static pfn_ovr_SpecifyTrackingOrigin ovr_SpecifyTrackingOrigin = nullptr;
static pfn_ovr_ClearShouldRecenterFlag ovr_ClearShouldRecenterFlag = nullptr;
static pfn_ovr_GetTrackingState ovr_GetTrackingState = nullptr;
static pfn_ovr_GetDevicePoses ovr_GetDevicePoses = nullptr;
static pfn_ovr_GetTrackerPose ovr_GetTrackerPose = nullptr;
static pfn_ovr_GetInputState ovr_GetInputState = nullptr;
static pfn_ovr_GetConnectedControllerTypes ovr_GetConnectedControllerTypes =
    nullptr;
static pfn_ovr_GetTouchHapticsDesc ovr_GetTouchHapticsDesc = nullptr;
static pfn_ovr_SetControllerVibration ovr_SetControllerVibration = nullptr;
static pfn_ovr_SubmitControllerVibration ovr_SubmitControllerVibration =
    nullptr;
static pfn_ovr_GetControllerVibrationState ovr_GetControllerVibrationState =
    nullptr;
static pfn_ovr_TestBoundary ovr_TestBoundary = nullptr;
static pfn_ovr_TestBoundaryPoint ovr_TestBoundaryPoint = nullptr;
static pfn_ovr_SetBoundaryLookAndFeel ovr_SetBoundaryLookAndFeel = nullptr;
static pfn_ovr_ResetBoundaryLookAndFeel ovr_ResetBoundaryLookAndFeel = nullptr;
static pfn_ovr_GetBoundaryGeometry ovr_GetBoundaryGeometry = nullptr;
static pfn_ovr_GetBoundaryDimensions ovr_GetBoundaryDimensions = nullptr;
static pfn_ovr_GetBoundaryVisible ovr_GetBoundaryVisible = nullptr;
static pfn_ovr_RequestBoundaryVisible ovr_RequestBoundaryVisible = nullptr;
static pfn_ovr_GetTextureSwapChainLength ovr_GetTextureSwapChainLength =
    nullptr;
static pfn_ovr_GetTextureSwapChainCurrentIndex
    ovr_GetTextureSwapChainCurrentIndex = nullptr;
static pfn_ovr_GetTextureSwapChainDesc ovr_GetTextureSwapChainDesc = nullptr;
static pfn_ovr_CommitTextureSwapChain ovr_CommitTextureSwapChain = nullptr;
static pfn_ovr_DestroyTextureSwapChain ovr_DestroyTextureSwapChain = nullptr;
static pfn_ovr_DestroyMirrorTexture ovr_DestroyMirrorTexture = nullptr;
static pfn_ovr_GetFovTextureSize ovr_GetFovTextureSize = nullptr;
static pfn_ovr_GetRenderDesc2 ovr_GetRenderDesc2 = nullptr;
static pfn_ovr_WaitToBeginFrame ovr_WaitToBeginFrame = nullptr;
static pfn_ovr_BeginFrame ovr_BeginFrame = nullptr;
static pfn_ovr_EndFrame ovr_EndFrame = nullptr;
static pfn_ovr_SubmitFrame ovr_SubmitFrame = nullptr;
static pfn_ovr_GetPerfStats ovr_GetPerfStats = nullptr;
static pfn_ovr_ResetPerfStats ovr_ResetPerfStats = nullptr;
static pfn_ovr_GetPredictedDisplayTime ovr_GetPredictedDisplayTime = nullptr;
static pfn_ovr_GetTimeInSeconds ovr_GetTimeInSeconds = nullptr;
static pfn_ovr_GetBool ovr_GetBool = nullptr;
static pfn_ovr_SetBool ovr_SetBool = nullptr;
static pfn_ovr_GetInt ovr_GetInt = nullptr;
static pfn_ovr_SetInt ovr_SetInt = nullptr;
static pfn_ovr_GetFloat ovr_GetFloat = nullptr;
static pfn_ovr_SetFloat ovr_SetFloat = nullptr;
static pfn_ovr_GetFloatArray ovr_GetFloatArray = nullptr;
static pfn_ovr_SetFloatArray ovr_SetFloatArray = nullptr;
static pfn_ovr_GetString ovr_GetString = nullptr;
static pfn_ovr_SetString ovr_SetString = nullptr;
static pfn_ovr_GetExternalCameras ovr_GetExternalCameras = nullptr;
static pfn_ovr_SetExternalCameraProperties ovr_SetExternalCameraProperties =
    nullptr;

#ifdef XP_WIN
static pfn_ovr_CreateTextureSwapChainDX ovr_CreateTextureSwapChainDX = nullptr;
static pfn_ovr_GetTextureSwapChainBufferDX ovr_GetTextureSwapChainBufferDX =
    nullptr;
static pfn_ovr_CreateMirrorTextureDX ovr_CreateMirrorTextureDX = nullptr;
static pfn_ovr_GetMirrorTextureBufferDX ovr_GetMirrorTextureBufferDX = nullptr;
#endif

static pfn_ovr_CreateTextureSwapChainGL ovr_CreateTextureSwapChainGL = nullptr;
static pfn_ovr_GetTextureSwapChainBufferGL ovr_GetTextureSwapChainBufferGL =
    nullptr;
static pfn_ovr_CreateMirrorTextureGL ovr_CreateMirrorTextureGL = nullptr;
static pfn_ovr_GetMirrorTextureBufferGL ovr_GetMirrorTextureBufferGL = nullptr;

#ifdef HAVE_64BIT_BUILD
#  define BUILD_BITS 64
#else
#  define BUILD_BITS 32
#endif

#define OVR_PRODUCT_VERSION 1
#define OVR_MAJOR_VERSION 1
#define OVR_MINOR_VERSION 19

static const uint32_t kNumOculusButtons = 7;
static const uint32_t kNumOculusHaptcs = 1;
static const uint32_t kNumOculusAxes = 4;
ovrControllerType OculusControllerTypes[2] = {ovrControllerType_LTouch,
                                              ovrControllerType_RTouch};
const char* OculusControllerNames[2] = {"Oculus Touch (Left)",
                                        "Oculus Touch (Right)"};
dom::GamepadHand OculusControllerHand[2] = {dom::GamepadHand::Left,
                                            dom::GamepadHand::Right};

ovrButton OculusControllerButtons[2][kNumOculusButtons] = {
    {(ovrButton)0, (ovrButton)0, (ovrButton)0, ovrButton_LThumb, ovrButton_X,
     ovrButton_Y, (ovrButton)0},
    {(ovrButton)0, (ovrButton)0, (ovrButton)0, ovrButton_RThumb, ovrButton_A,
     ovrButton_B, (ovrButton)0},
};

ovrTouch OculusControllerTouches[2][kNumOculusButtons] = {
    {ovrTouch_LIndexTrigger, (ovrTouch)0, (ovrTouch)0, ovrTouch_LThumb,
     ovrTouch_X, ovrTouch_Y, ovrTouch_LThumbRest},
    {ovrTouch_RIndexTrigger, (ovrTouch)0, (ovrTouch)0, ovrTouch_RThumb,
     ovrTouch_A, ovrTouch_B, ovrTouch_RThumbRest},
};

void UpdateButton(const ovrInputState& aInputState, uint32_t aHandIdx,
                  uint32_t aButtonIdx, VRControllerState& aControllerState) {
  if (aInputState.Buttons & OculusControllerButtons[aHandIdx][aButtonIdx]) {
    aControllerState.buttonPressed |= ((uint64_t)1 << aButtonIdx);
    aControllerState.triggerValue[aButtonIdx] = 1.0f;
  } else {
    aControllerState.triggerValue[aButtonIdx] = 0.0f;
  }
  if (aInputState.Touches & OculusControllerTouches[aHandIdx][aButtonIdx]) {
    aControllerState.buttonTouched |= ((uint64_t)1 << aButtonIdx);
  }
}

VRFieldOfView FromFovPort(const ovrFovPort& aFOV) {
  VRFieldOfView fovInfo;
  fovInfo.leftDegrees = atan(aFOV.LeftTan) * 180.0 / M_PI;
  fovInfo.rightDegrees = atan(aFOV.RightTan) * 180.0 / M_PI;
  fovInfo.upDegrees = atan(aFOV.UpTan) * 180.0 / M_PI;
  fovInfo.downDegrees = atan(aFOV.DownTan) * 180.0 / M_PI;
  return fovInfo;
}

}  // anonymous namespace

namespace mozilla {
namespace gfx {

OculusSession::OculusSession()
    : VRSession(),
      mOvrLib(nullptr),
      mSession(nullptr),
      mInitFlags((ovrInitFlags)0),
      mTextureSet(nullptr),
      mQuadVS(nullptr),
      mQuadPS(nullptr),
      mLinearSamplerState(nullptr),
      mVSConstantBuffer(nullptr),
      mPSConstantBuffer(nullptr),
      mVertexBuffer(nullptr),
      mInputLayout(nullptr),
      mRemainingVibrateTime{},
      mHapticPulseIntensity{},
      mIsPresenting(false) {}

OculusSession::~OculusSession() { Shutdown(); }

bool OculusSession::Initialize(mozilla::gfx::VRSystemState& aSystemState,
                               bool aDetectRuntimesOnly) {
  if (StaticPrefs::dom_vr_puppet_enabled()) {
    // Ensure that tests using the VR Puppet do not find real hardware
    return false;
  }
  if (!StaticPrefs::dom_vr_enabled() || !StaticPrefs::dom_vr_oculus_enabled()) {
    return false;
  }

  if (aDetectRuntimesOnly) {
    if (LoadOvrLib()) {
      aSystemState.displayState.capabilityFlags |=
          VRDisplayCapabilityFlags::Cap_ImmersiveVR;
    }
    return false;
  }

  if (!CreateD3DObjects()) {
    return false;
  }
  if (!CreateShaders()) {
    return false;
  }
  // Ideally, we should move LoadOvrLib() up to the first line to avoid
  // unnecessary D3D objects creation. But it will cause a WPT fail in Win 7
  // debug.
  if (!LoadOvrLib()) {
    return false;
  }
  // We start off with an invisible session, then re-initialize
  // with visible session once WebVR content starts rendering.
  if (!ChangeVisibility(false)) {
    return false;
  }
  if (!InitState(aSystemState)) {
    return false;
  }

  mPresentationSize = IntSize(aSystemState.displayState.eyeResolution.width * 2,
                              aSystemState.displayState.eyeResolution.height);
  return true;
}

void OculusSession::UpdateVisibility() {
  // Do not immediately re-initialize with an invisible session after
  // the end of a VR presentation.  Waiting for the configured duraction
  // ensures that the user will not drop to Oculus Home during VR link
  // traversal.
  if (mIsPresenting) {
    // We are currently rendering immersive content.
    // Avoid interrupting the session
    return;
  }
  if (mInitFlags & ovrInit_Invisible) {
    // We are already invisible
    return;
  }
  if (mLastPresentationEnd.IsNull()) {
    // There has been no presentation yet
    return;
  }

  TimeDuration duration = TimeStamp::Now() - mLastPresentationEnd;
  TimeDuration timeout = TimeDuration::FromMilliseconds(
      StaticPrefs::dom_vr_oculus_present_timeout());
  if (timeout <= TimeDuration(0) || duration >= timeout) {
    if (!ChangeVisibility(false)) {
      gfxWarning() << "OculusSession::ChangeVisibility(false) failed";
    }
  }
}

void OculusSession::CoverTransitions() {
  // While content is loading or during immersive-mode link
  // traversal, we need to prevent the user from seeing the
  // last rendered frame.
  // We render black frames to cover up the transition.
  MOZ_RELEASE_ASSERT(mSession);
  if (mIsPresenting) {
    // We are currently rendering immersive content.
    // Avoid interrupting the session
    return;
  }

  if (mInitFlags & ovrInit_Invisible) {
    // We are invisible, nothing to cover up
    return;
  }

  // Render a black frame
  ovrLayerEyeFov layer;
  memset(&layer, 0, sizeof(layer));
  layer.Header.Type = ovrLayerType_Disabled;
  ovrLayerHeader* layers = &layer.Header;
  ovr_SubmitFrame(mSession, 0, nullptr, &layers, 1);
}

bool OculusSession::ChangeVisibility(bool bVisible) {
  ovrInitFlags flags =
      (ovrInitFlags)(ovrInit_RequestVersion | ovrInit_MixedRendering);
  if (StaticPrefs::dom_vr_oculus_invisible_enabled() && !bVisible) {
    flags = (ovrInitFlags)(flags | ovrInit_Invisible);
  }
  if (mInitFlags == flags) {
    // The new state is the same, nothing to do
    return true;
  }

  // Tear everything down
  StopRendering();
  StopSession();
  StopLib();

  // Start it back up
  if (!StartLib(flags)) {
    return false;
  }
  if (!StartSession()) {
    return false;
  }
  return true;
}

void OculusSession::Shutdown() {
  StopRendering();
  StopSession();
  StopLib();
  UnloadOvrLib();
  DestroyShaders();
}

void OculusSession::ProcessEvents(mozilla::gfx::VRSystemState& aSystemState) {
  if (!mSession) {
    return;
  }

  ovrSessionStatus status;
  if (OVR_SUCCESS(ovr_GetSessionStatus(mSession, &status))) {
    aSystemState.displayState.isConnected = status.HmdPresent;
    aSystemState.displayState.isMounted = status.HmdMounted;
    mShouldQuit = status.ShouldQuit;

  } else {
    aSystemState.displayState.isConnected = false;
    aSystemState.displayState.isMounted = false;
  }
  UpdateHaptics();
  UpdateVisibility();
  CoverTransitions();
}

void OculusSession::StartFrame(mozilla::gfx::VRSystemState& aSystemState) {
  UpdateHeadsetPose(aSystemState);
  UpdateEyeParameters(aSystemState);
  UpdateControllers(aSystemState);
  UpdateTelemetry(aSystemState);
  aSystemState.sensorState.inputFrameID++;
}

bool OculusSession::StartPresentation() {
  /**
   * XXX - We should resolve fail the promise returned by
   *       VRDisplay.requestPresent() when the DX11 resources fail allocation
   *       in VRDisplayOculus::StartPresentation().
   *       Bailing out here prevents the crash but content should be aware
   *       that frames are not being presented.
   *       See Bug 1299309.
   **/
  if (!ChangeVisibility(true)) {
    return false;
  }
  if (!StartRendering()) {
    StopRendering();
    return false;
  }
  mIsPresenting = true;
  return true;
}

void OculusSession::StopPresentation() {
  mLastPresentationEnd = TimeStamp::Now();
  mIsPresenting = false;
}

bool OculusSession::SubmitFrame(
    const mozilla::gfx::VRLayer_Stereo_Immersive& aLayer,
    ID3D11Texture2D* aTexture) {
  if (!IsPresentationReady()) {
    return false;
  }

  D3D11_TEXTURE2D_DESC textureDesc = {0};
  aTexture->GetDesc(&textureDesc);

  int currentRenderTarget = 0;
  ovrResult orv = ovr_GetTextureSwapChainCurrentIndex(mSession, mTextureSet,
                                                      &currentRenderTarget);
  if (orv != ovrSuccess) {
    NS_WARNING("ovr_GetTextureSwapChainCurrentIndex failed.");
    return false;
  }

  ID3D11RenderTargetView* view = mRTView[currentRenderTarget];

  float clear[] = {0.0f, 0.0f, 0.0f, 1.0f};
  mContext->ClearRenderTargetView(view, clear);
  mContext->OMSetRenderTargets(1, &view, nullptr);

  Matrix viewMatrix = Matrix::Translation(-1.0, 1.0);
  viewMatrix.PreScale(2.0f / float(textureDesc.Width),
                      2.0f / float(textureDesc.Height));
  viewMatrix.PreScale(1.0f, -1.0f);
  Matrix4x4 projection = Matrix4x4::From2D(viewMatrix);
  projection._33 = 0.0f;

  Matrix transform2d;
  gfx::Matrix4x4 transform = gfx::Matrix4x4::From2D(transform2d);

  D3D11_VIEWPORT viewport;
  viewport.MinDepth = 0.0f;
  viewport.MaxDepth = 1.0f;
  viewport.Width = textureDesc.Width;
  viewport.Height = textureDesc.Height;
  viewport.TopLeftX = 0;
  viewport.TopLeftY = 0;

  D3D11_RECT scissor;
  scissor.left = 0;
  scissor.right = textureDesc.Width;
  scissor.top = 0;
  scissor.bottom = textureDesc.Height;

  memcpy(&mVSConstants.layerTransform, &transform._11,
         sizeof(mVSConstants.layerTransform));
  memcpy(&mVSConstants.projection, &projection._11,
         sizeof(mVSConstants.projection));
  mVSConstants.renderTargetOffset[0] = 0.0f;
  mVSConstants.renderTargetOffset[1] = 0.0f;
  mVSConstants.layerQuad =
      Rect(0.0f, 0.0f, textureDesc.Width, textureDesc.Height);
  mVSConstants.textureCoords = Rect(0.0f, 1.0f, 1.0f, -1.0f);

  mPSConstants.layerOpacity[0] = 1.0f;

  ID3D11Buffer* vbuffer = mVertexBuffer;
  UINT vsize = sizeof(Vertex);
  UINT voffset = 0;
  mContext->IASetVertexBuffers(0, 1, &vbuffer, &vsize, &voffset);
  mContext->IASetIndexBuffer(nullptr, DXGI_FORMAT_R16_UINT, 0);
  mContext->IASetInputLayout(mInputLayout);
  mContext->RSSetViewports(1, &viewport);
  mContext->RSSetScissorRects(1, &scissor);
  mContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
  mContext->VSSetShader(mQuadVS, nullptr, 0);
  mContext->PSSetShader(mQuadPS, nullptr, 0);

  RefPtr<ID3D11ShaderResourceView> srView;
  HRESULT hr = mDevice->CreateShaderResourceView(aTexture, nullptr,
                                                 getter_AddRefs(srView));
  if (FAILED(hr)) {
    gfxWarning() << "Could not create shader resource view for Oculus: "
                 << hexa(hr);
    return false;
  }
  ID3D11ShaderResourceView* viewPtr = srView.get();
  mContext->PSSetShaderResources(0 /* 0 == TexSlot::RGB */, 1, &viewPtr);
  // XXX Use Constant from TexSlot in CompositorD3D11.cpp?

  ID3D11SamplerState* sampler = mLinearSamplerState;
  mContext->PSSetSamplers(0, 1, &sampler);

  if (!UpdateConstantBuffers()) {
    NS_WARNING("Failed to update constant buffers for Oculus");
    return false;
  }

  mContext->Draw(4, 0);

  orv = ovr_CommitTextureSwapChain(mSession, mTextureSet);
  if (orv != ovrSuccess) {
    NS_WARNING("ovr_CommitTextureSwapChain failed.");
    return false;
  }

  ovrLayerEyeFov layer;
  memset(&layer, 0, sizeof(layer));
  layer.Header.Type = ovrLayerType_EyeFov;
  layer.Header.Flags = 0;
  layer.ColorTexture[0] = mTextureSet;
  layer.ColorTexture[1] = nullptr;
  layer.Fov[0] = mFOVPort[0];
  layer.Fov[1] = mFOVPort[1];
  layer.Viewport[0].Pos.x = textureDesc.Width * aLayer.leftEyeRect.x;
  layer.Viewport[0].Pos.y = textureDesc.Height * aLayer.leftEyeRect.y;
  layer.Viewport[0].Size.w = textureDesc.Width * aLayer.leftEyeRect.width;
  layer.Viewport[0].Size.h = textureDesc.Height * aLayer.leftEyeRect.height;
  layer.Viewport[1].Pos.x = textureDesc.Width * aLayer.rightEyeRect.x;
  layer.Viewport[1].Pos.y = textureDesc.Height * aLayer.rightEyeRect.y;
  layer.Viewport[1].Size.w = textureDesc.Width * aLayer.rightEyeRect.width;
  layer.Viewport[1].Size.h = textureDesc.Height * aLayer.rightEyeRect.height;

  for (uint32_t i = 0; i < 2; ++i) {
    layer.RenderPose[i].Orientation.x = mFrameStartPose[i].Orientation.x;
    layer.RenderPose[i].Orientation.y = mFrameStartPose[i].Orientation.y;
    layer.RenderPose[i].Orientation.z = mFrameStartPose[i].Orientation.z;
    layer.RenderPose[i].Orientation.w = mFrameStartPose[i].Orientation.w;
    layer.RenderPose[i].Position.x = mFrameStartPose[i].Position.x;
    layer.RenderPose[i].Position.y = mFrameStartPose[i].Position.y;
    layer.RenderPose[i].Position.z = mFrameStartPose[i].Position.z;
  }

  ovrLayerHeader* layers = &layer.Header;
  orv = ovr_SubmitFrame(mSession, 0, nullptr, &layers, 1);
  // ovr_SubmitFrame will fail during the Oculus health and safety warning.
  // and will start succeeding once the warning has been dismissed by the user.

  if (!OVR_UNQUALIFIED_SUCCESS(orv)) {
    /**
     * We wish to throttle the framerate for any case that the rendered
     * result is not visible.  In some cases, such as during the Oculus
     * "health and safety warning", orv will be > 0 (OVR_SUCCESS but not
     * OVR_UNQUALIFIED_SUCCESS) and ovr_SubmitFrame will not block.
     * In this case, returning true would have resulted in an unthrottled
     * render loop hiting excessive frame rates and consuming resources.
     */
    return false;
  }

  return true;
}

bool OculusSession::LoadOvrLib() {
  if (mOvrLib) {
    // Already loaded, early exit
    return true;
  }
#if defined(_WIN32)
  nsTArray<nsString> libSearchPaths;
  nsString libName;
  nsString searchPath;

  for (;;) {
    UINT requiredLength = ::GetSystemDirectoryW(
        char16ptr_t(searchPath.BeginWriting()), searchPath.Length());
    if (!requiredLength) {
      break;
    }
    if (requiredLength < searchPath.Length()) {
      searchPath.Truncate(requiredLength);
      libSearchPaths.AppendElement(searchPath);
      break;
    }
    searchPath.SetLength(requiredLength);
  }
  libName.AppendPrintf("LibOVRRT%d_%d.dll", BUILD_BITS, OVR_PRODUCT_VERSION);

  // search the path/module dir
  libSearchPaths.InsertElementsAt(0, 1, u""_ns);

  // If the env var is present, we override libName
  if (_wgetenv(L"OVR_LIB_PATH")) {
    searchPath = _wgetenv(L"OVR_LIB_PATH");
    libSearchPaths.InsertElementsAt(0, 1, searchPath);
  }

  if (_wgetenv(L"OVR_LIB_NAME")) {
    libName = _wgetenv(L"OVR_LIB_NAME");
  }

  if (libName.IsEmpty()) {
    return false;
  }

  for (uint32_t i = 0; i < libSearchPaths.Length(); ++i) {
    nsString& libPath = libSearchPaths[i];
    nsString fullName;
    if (libPath.Length() == 0) {
      fullName.Assign(libName);
    } else {
      fullName.Assign(libPath + u"\\"_ns + libName);
    }

    mOvrLib = LoadLibraryWithFlags(fullName.get());
    if (mOvrLib) {
      break;
    }
  }
#else
#  error "Unsupported platform!"
#endif

  if (!mOvrLib) {
    return false;
  }

#define REQUIRE_FUNCTION(_x)                           \
  do {                                                 \
    *(void**)&_x = (void*)PR_FindSymbol(mOvrLib, #_x); \
    if (!_x) {                                         \
      printf_stderr(#_x " symbol missing\n");          \
      goto fail;                                       \
    }                                                  \
  } while (0)

  REQUIRE_FUNCTION(ovr_Initialize);
  REQUIRE_FUNCTION(ovr_Shutdown);
  REQUIRE_FUNCTION(ovr_GetLastErrorInfo);
  REQUIRE_FUNCTION(ovr_GetVersionString);
  REQUIRE_FUNCTION(ovr_TraceMessage);
  REQUIRE_FUNCTION(ovr_IdentifyClient);
  REQUIRE_FUNCTION(ovr_GetHmdDesc);
  REQUIRE_FUNCTION(ovr_GetTrackerCount);
  REQUIRE_FUNCTION(ovr_GetTrackerDesc);
  REQUIRE_FUNCTION(ovr_Create);
  REQUIRE_FUNCTION(ovr_Destroy);
  REQUIRE_FUNCTION(ovr_GetSessionStatus);
  REQUIRE_FUNCTION(ovr_IsExtensionSupported);
  REQUIRE_FUNCTION(ovr_EnableExtension);
  REQUIRE_FUNCTION(ovr_SetTrackingOriginType);
  REQUIRE_FUNCTION(ovr_GetTrackingOriginType);
  REQUIRE_FUNCTION(ovr_RecenterTrackingOrigin);
  REQUIRE_FUNCTION(ovr_SpecifyTrackingOrigin);
  REQUIRE_FUNCTION(ovr_ClearShouldRecenterFlag);
  REQUIRE_FUNCTION(ovr_GetTrackingState);
  REQUIRE_FUNCTION(ovr_GetDevicePoses);
  REQUIRE_FUNCTION(ovr_GetTrackerPose);
  REQUIRE_FUNCTION(ovr_GetInputState);
  REQUIRE_FUNCTION(ovr_GetConnectedControllerTypes);
  REQUIRE_FUNCTION(ovr_GetTouchHapticsDesc);
  REQUIRE_FUNCTION(ovr_SetControllerVibration);
  REQUIRE_FUNCTION(ovr_SubmitControllerVibration);
  REQUIRE_FUNCTION(ovr_GetControllerVibrationState);
  REQUIRE_FUNCTION(ovr_TestBoundary);
  REQUIRE_FUNCTION(ovr_TestBoundaryPoint);
  REQUIRE_FUNCTION(ovr_SetBoundaryLookAndFeel);
  REQUIRE_FUNCTION(ovr_ResetBoundaryLookAndFeel);
  REQUIRE_FUNCTION(ovr_GetBoundaryGeometry);
  REQUIRE_FUNCTION(ovr_GetBoundaryDimensions);
  REQUIRE_FUNCTION(ovr_GetBoundaryVisible);
  REQUIRE_FUNCTION(ovr_RequestBoundaryVisible);
  REQUIRE_FUNCTION(ovr_GetTextureSwapChainLength);
  REQUIRE_FUNCTION(ovr_GetTextureSwapChainCurrentIndex);
  REQUIRE_FUNCTION(ovr_GetTextureSwapChainDesc);
  REQUIRE_FUNCTION(ovr_CommitTextureSwapChain);
  REQUIRE_FUNCTION(ovr_DestroyTextureSwapChain);
  REQUIRE_FUNCTION(ovr_DestroyMirrorTexture);
  REQUIRE_FUNCTION(ovr_GetFovTextureSize);
  REQUIRE_FUNCTION(ovr_GetRenderDesc2);
  REQUIRE_FUNCTION(ovr_WaitToBeginFrame);
  REQUIRE_FUNCTION(ovr_BeginFrame);
  REQUIRE_FUNCTION(ovr_EndFrame);
  REQUIRE_FUNCTION(ovr_SubmitFrame);
  REQUIRE_FUNCTION(ovr_GetPerfStats);
  REQUIRE_FUNCTION(ovr_ResetPerfStats);
  REQUIRE_FUNCTION(ovr_GetPredictedDisplayTime);
  REQUIRE_FUNCTION(ovr_GetTimeInSeconds);
  REQUIRE_FUNCTION(ovr_GetBool);
  REQUIRE_FUNCTION(ovr_SetBool);
  REQUIRE_FUNCTION(ovr_GetInt);
  REQUIRE_FUNCTION(ovr_SetInt);
  REQUIRE_FUNCTION(ovr_GetFloat);
  REQUIRE_FUNCTION(ovr_SetFloat);
  REQUIRE_FUNCTION(ovr_GetFloatArray);
  REQUIRE_FUNCTION(ovr_SetFloatArray);
  REQUIRE_FUNCTION(ovr_GetString);
  REQUIRE_FUNCTION(ovr_SetString);
  REQUIRE_FUNCTION(ovr_GetExternalCameras);
  REQUIRE_FUNCTION(ovr_SetExternalCameraProperties);

#ifdef XP_WIN

  REQUIRE_FUNCTION(ovr_CreateTextureSwapChainDX);
  REQUIRE_FUNCTION(ovr_GetTextureSwapChainBufferDX);
  REQUIRE_FUNCTION(ovr_CreateMirrorTextureDX);
  REQUIRE_FUNCTION(ovr_GetMirrorTextureBufferDX);

#endif

  REQUIRE_FUNCTION(ovr_CreateTextureSwapChainGL);
  REQUIRE_FUNCTION(ovr_GetTextureSwapChainBufferGL);
  REQUIRE_FUNCTION(ovr_CreateMirrorTextureGL);
  REQUIRE_FUNCTION(ovr_GetMirrorTextureBufferGL);

#undef REQUIRE_FUNCTION

  return true;

fail:
  ovr_Initialize = nullptr;
  PR_UnloadLibrary(mOvrLib);
  mOvrLib = nullptr;
  return false;
}

void OculusSession::UnloadOvrLib() {
  if (mOvrLib) {
    PR_UnloadLibrary(mOvrLib);
    mOvrLib = nullptr;
  }
}

bool OculusSession::StartLib(ovrInitFlags aFlags) {
  if (mInitFlags == 0) {
    ovrInitParams params;
    memset(&params, 0, sizeof(params));
    params.Flags = aFlags;
    params.RequestedMinorVersion = OVR_MINOR_VERSION;
    params.LogCallback = nullptr;
    params.ConnectionTimeoutMS = 0;

    ovrResult orv = ovr_Initialize(&params);

    if (orv == ovrSuccess) {
      mInitFlags = aFlags;
    } else {
      return false;
    }
  }
  MOZ_RELEASE_ASSERT(mInitFlags == aFlags);
  return true;
}

void OculusSession::StopLib() {
  if (mInitFlags) {
    ovr_Shutdown();
    mInitFlags = (ovrInitFlags)0;
  }
}

bool OculusSession::StartSession() {
  // ovr_Create can be slow when no HMD is present and we wish
  // to keep the same oculus session when possible, so we detect
  // presence of an HMD with ovr_GetHmdDesc before calling ovr_Create
  ovrHmdDesc desc = ovr_GetHmdDesc(nullptr);
  if (desc.Type == ovrHmd_None) {
    // No HMD connected, destroy any existing session
    if (mSession) {
      ovr_Destroy(mSession);
      mSession = nullptr;
    }
    return false;
  }
  if (mSession != nullptr) {
    // HMD Detected and we already have a session, let's keep using it.
    return true;
  }

  // HMD Detected and we don't have a session yet,
  // try to create a new session
  ovrSession session;
  ovrGraphicsLuid luid;
  ovrResult orv = ovr_Create(&session, &luid);
  if (orv == ovrSuccess) {
    orv = ovr_SetTrackingOriginType(session, ovrTrackingOrigin_FloorLevel);
    if (orv != ovrSuccess) {
      NS_WARNING("ovr_SetTrackingOriginType failed.");
    }
    mSession = session;
    return true;
  }

  // Failed to create a session for the HMD
  return false;
}

void OculusSession::StopSession() {
  if (mSession) {
    ovr_Destroy(mSession);
    mSession = nullptr;
  }
}

bool OculusSession::CreateD3DObjects() {
  RefPtr<ID3D11Device> device = gfx::DeviceManagerDx::Get()->GetVRDevice();
  if (!device) {
    return false;
  }
  if (!CreateD3DContext(device)) {
    return false;
  }
  return true;
}

bool OculusSession::CreateShaders() {
  if (!mQuadVS) {
    if (FAILED(mDevice->CreateVertexShader(
            sLayerQuadVS.mData, sLayerQuadVS.mLength, nullptr, &mQuadVS))) {
      NS_WARNING("Failed to create vertex shader for Oculus");
      return false;
    }
  }

  if (!mQuadPS) {
    if (FAILED(mDevice->CreatePixelShader(sRGBShader.mData, sRGBShader.mLength,
                                          nullptr, &mQuadPS))) {
      NS_WARNING("Failed to create pixel shader for Oculus");
      return false;
    }
  }

  CD3D11_BUFFER_DESC cBufferDesc(sizeof(layers::VertexShaderConstants),
                                 D3D11_BIND_CONSTANT_BUFFER,
                                 D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE);

  if (!mVSConstantBuffer) {
    if (FAILED(mDevice->CreateBuffer(&cBufferDesc, nullptr,
                                     getter_AddRefs(mVSConstantBuffer)))) {
      NS_WARNING("Failed to vertex shader constant buffer for Oculus");
      return false;
    }
  }

  if (!mPSConstantBuffer) {
    cBufferDesc.ByteWidth = sizeof(layers::PixelShaderConstants);
    if (FAILED(mDevice->CreateBuffer(&cBufferDesc, nullptr,
                                     getter_AddRefs(mPSConstantBuffer)))) {
      NS_WARNING("Failed to pixel shader constant buffer for Oculus");
      return false;
    }
  }

  if (!mLinearSamplerState) {
    CD3D11_SAMPLER_DESC samplerDesc(D3D11_DEFAULT);
    if (FAILED(mDevice->CreateSamplerState(
            &samplerDesc, getter_AddRefs(mLinearSamplerState)))) {
      NS_WARNING("Failed to create sampler state for Oculus");
      return false;
    }
  }

  if (!mInputLayout) {
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
    };

    if (FAILED(mDevice->CreateInputLayout(
            layout, sizeof(layout) / sizeof(D3D11_INPUT_ELEMENT_DESC),
            sLayerQuadVS.mData, sLayerQuadVS.mLength,
            getter_AddRefs(mInputLayout)))) {
      NS_WARNING("Failed to create input layout for Oculus");
      return false;
    }
  }

  if (!mVertexBuffer) {
    Vertex vertices[] = {
        {{0.0, 0.0}}, {{1.0, 0.0}}, {{0.0, 1.0}}, {{1.0, 1.0}}};
    CD3D11_BUFFER_DESC bufferDesc(sizeof(vertices), D3D11_BIND_VERTEX_BUFFER);
    D3D11_SUBRESOURCE_DATA data;
    data.pSysMem = (void*)vertices;

    if (FAILED(mDevice->CreateBuffer(&bufferDesc, &data,
                                     getter_AddRefs(mVertexBuffer)))) {
      NS_WARNING("Failed to create vertex buffer for Oculus");
      return false;
    }
  }

  memset(&mVSConstants, 0, sizeof(mVSConstants));
  memset(&mPSConstants, 0, sizeof(mPSConstants));
  return true;
}

void OculusSession::DestroyShaders() {}

bool OculusSession::UpdateConstantBuffers() {
  HRESULT hr;
  D3D11_MAPPED_SUBRESOURCE resource;
  resource.pData = nullptr;

  hr = mContext->Map(mVSConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0,
                     &resource);
  if (FAILED(hr) || !resource.pData) {
    return false;
  }
  *(VertexShaderConstants*)resource.pData = mVSConstants;
  mContext->Unmap(mVSConstantBuffer, 0);
  resource.pData = nullptr;

  hr = mContext->Map(mPSConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0,
                     &resource);
  if (FAILED(hr) || !resource.pData) {
    return false;
  }
  *(PixelShaderConstants*)resource.pData = mPSConstants;
  mContext->Unmap(mPSConstantBuffer, 0);

  ID3D11Buffer* buffer = mVSConstantBuffer;
  mContext->VSSetConstantBuffers(0, 1, &buffer);
  buffer = mPSConstantBuffer;
  mContext->PSSetConstantBuffers(0, 1, &buffer);
  return true;
}

bool OculusSession::StartRendering() {
  if (!mTextureSet) {
    /**
     * The presentation format is determined by content, which describes the
     * left and right eye rectangles in the VRLayer.  The default, if no
     * coordinates are passed is to place the left and right eye textures
     * side-by-side within the buffer.
     *
     * XXX - An optimization would be to dynamically resize this buffer
     *       to accomodate sites that are choosing to render in a lower
     *       resolution or are using space outside of the left and right
     *       eye textures for other purposes.  (Bug 1291443)
     */

    ovrTextureSwapChainDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.Type = ovrTexture_2D;
    desc.ArraySize = 1;
    desc.Format = OVR_FORMAT_B8G8R8A8_UNORM_SRGB;
    desc.Width = mPresentationSize.width;
    desc.Height = mPresentationSize.height;
    desc.MipLevels = 1;
    desc.SampleCount = 1;
    desc.StaticImage = false;
    desc.MiscFlags = ovrTextureMisc_DX_Typeless;
    desc.BindFlags = ovrTextureBind_DX_RenderTarget;

    ovrResult orv =
        ovr_CreateTextureSwapChainDX(mSession, mDevice, &desc, &mTextureSet);
    if (orv != ovrSuccess) {
      NS_WARNING("ovr_CreateTextureSwapChainDX failed");
      return false;
    }

    int textureCount = 0;
    orv = ovr_GetTextureSwapChainLength(mSession, mTextureSet, &textureCount);
    if (orv != ovrSuccess) {
      NS_WARNING("ovr_GetTextureSwapChainLength failed");
      return false;
    }
    mTexture.SetLength(textureCount);
    mRTView.SetLength(textureCount);
    mSRV.SetLength(textureCount);
    for (int i = 0; i < textureCount; ++i) {
      ID3D11Texture2D* texture = nullptr;
      orv = ovr_GetTextureSwapChainBufferDX(mSession, mTextureSet, i,
                                            IID_PPV_ARGS(&texture));
      if (orv != ovrSuccess) {
        NS_WARNING("Failed to create Oculus texture swap chain.");
        return false;
      }

      RefPtr<ID3D11RenderTargetView> rtView;
      CD3D11_RENDER_TARGET_VIEW_DESC rtvDesc(D3D11_RTV_DIMENSION_TEXTURE2D,
                                             DXGI_FORMAT_B8G8R8A8_UNORM);
      HRESULT hr = mDevice->CreateRenderTargetView(texture, &rtvDesc,
                                                   getter_AddRefs(rtView));
      if (FAILED(hr)) {
        NS_WARNING(
            "Failed to create RenderTargetView for Oculus texture swap chain.");
        texture->Release();
        return false;
      }

      RefPtr<ID3D11ShaderResourceView> srv;
      CD3D11_SHADER_RESOURCE_VIEW_DESC srvDesc(D3D11_SRV_DIMENSION_TEXTURE2D,
                                               DXGI_FORMAT_B8G8R8A8_UNORM);
      hr = mDevice->CreateShaderResourceView(texture, &srvDesc,
                                             getter_AddRefs(srv));
      if (FAILED(hr)) {
        NS_WARNING(
            "Failed to create ShaderResourceView for Oculus texture swap "
            "chain.");
        texture->Release();
        return false;
      }

      mTexture[i] = texture;
      mRTView[i] = rtView;
      mSRV[i] = srv;
      texture->Release();
    }
  }
  return true;
}

bool OculusSession::IsPresentationReady() const {
  return mTextureSet != nullptr;
}

void OculusSession::StopRendering() {
  mSRV.Clear();
  mRTView.Clear();
  mTexture.Clear();

  if (mTextureSet && mSession) {
    ovr_DestroyTextureSwapChain(mSession, mTextureSet);
  }
  mTextureSet = nullptr;
  mIsPresenting = false;
}

bool OculusSession::InitState(VRSystemState& aSystemState) {
  VRDisplayState& state = aSystemState.displayState;
  strncpy(state.displayName.data(), "Oculus VR HMD", kVRDisplayNameMaxLen);
  state.isConnected = true;
  state.isMounted = false;

  ovrHmdDesc desc = ovr_GetHmdDesc(mSession);

  state.capabilityFlags = VRDisplayCapabilityFlags::Cap_None;
  if (desc.AvailableTrackingCaps & ovrTrackingCap_Orientation) {
    state.capabilityFlags |= VRDisplayCapabilityFlags::Cap_Orientation;
    state.capabilityFlags |= VRDisplayCapabilityFlags::Cap_AngularAcceleration;
  }
  if (desc.AvailableTrackingCaps & ovrTrackingCap_Position) {
    state.capabilityFlags |= VRDisplayCapabilityFlags::Cap_Position;
    state.capabilityFlags |= VRDisplayCapabilityFlags::Cap_LinearAcceleration;
    state.capabilityFlags |= VRDisplayCapabilityFlags::Cap_StageParameters;
  }
  state.capabilityFlags |= VRDisplayCapabilityFlags::Cap_External;
  state.capabilityFlags |= VRDisplayCapabilityFlags::Cap_MountDetection;
  state.capabilityFlags |= VRDisplayCapabilityFlags::Cap_Present;
  state.capabilityFlags |= VRDisplayCapabilityFlags::Cap_ImmersiveVR;
  state.blendMode = VRDisplayBlendMode::Opaque;
  state.reportsDroppedFrames = true;

  mFOVPort[VRDisplayState::Eye_Left] = desc.DefaultEyeFov[ovrEye_Left];
  mFOVPort[VRDisplayState::Eye_Right] = desc.DefaultEyeFov[ovrEye_Right];

  state.eyeFOV[VRDisplayState::Eye_Left] =
      FromFovPort(mFOVPort[VRDisplayState::Eye_Left]);
  state.eyeFOV[VRDisplayState::Eye_Right] =
      FromFovPort(mFOVPort[VRDisplayState::Eye_Right]);

  float pixelsPerDisplayPixel = 1.0;
  ovrSizei texSize[2];

  // get eye texture sizes
  for (uint32_t eye = 0; eye < VRDisplayState::NumEyes; eye++) {
    texSize[eye] = ovr_GetFovTextureSize(mSession, (ovrEyeType)eye,
                                         mFOVPort[eye], pixelsPerDisplayPixel);
  }

  // take the max of both for eye resolution
  state.eyeResolution.width = std::max(texSize[VRDisplayState::Eye_Left].w,
                                       texSize[VRDisplayState::Eye_Right].w);
  state.eyeResolution.height = std::max(texSize[VRDisplayState::Eye_Left].h,
                                        texSize[VRDisplayState::Eye_Right].h);
  state.nativeFramebufferScaleFactor = 1.0f;

  // default to an identity quaternion
  aSystemState.sensorState.pose.orientation[3] = 1.0f;

  UpdateStageParameters(state);
  UpdateEyeParameters(aSystemState);

  VRHMDSensorState& sensorState = aSystemState.sensorState;
  sensorState.flags =
      (VRDisplayCapabilityFlags)((int)
                                     VRDisplayCapabilityFlags::Cap_Orientation |
                                 (int)VRDisplayCapabilityFlags::Cap_Position);
  sensorState.pose.orientation[3] = 1.0f;  // Default to an identity quaternion

  return true;
}

void OculusSession::UpdateStageParameters(VRDisplayState& aState) {
  ovrVector3f playArea;
  ovrResult res =
      ovr_GetBoundaryDimensions(mSession, ovrBoundary_PlayArea, &playArea);
  if (res == ovrSuccess) {
    aState.stageSize.width = playArea.x;
    aState.stageSize.height = playArea.z;
  } else {
    // If we fail, fall back to reasonable defaults.
    // 1m x 1m space
    aState.stageSize.width = 1.0f;
    aState.stageSize.height = 1.0f;
  }

  float eyeHeight =
      ovr_GetFloat(mSession, OVR_KEY_EYE_HEIGHT, OVR_DEFAULT_EYE_HEIGHT);

  aState.sittingToStandingTransform[0] = 1.0f;
  aState.sittingToStandingTransform[1] = 0.0f;
  aState.sittingToStandingTransform[2] = 0.0f;
  aState.sittingToStandingTransform[3] = 0.0f;

  aState.sittingToStandingTransform[4] = 0.0f;
  aState.sittingToStandingTransform[5] = 1.0f;
  aState.sittingToStandingTransform[6] = 0.0f;
  aState.sittingToStandingTransform[7] = 0.0f;

  aState.sittingToStandingTransform[8] = 0.0f;
  aState.sittingToStandingTransform[9] = 0.0f;
  aState.sittingToStandingTransform[10] = 1.0f;
  aState.sittingToStandingTransform[11] = 0.0f;

  aState.sittingToStandingTransform[12] = 0.0f;
  aState.sittingToStandingTransform[13] = eyeHeight;
  aState.sittingToStandingTransform[14] = 0.0f;
  aState.sittingToStandingTransform[15] = 1.0f;
}

void OculusSession::UpdateEyeParameters(VRSystemState& aState) {
  if (!mSession) {
    return;
  }
  // This must be called every frame in order to
  // account for continuous adjustments to ipd.
  gfx::Matrix4x4 headToEyeTransforms[2];
  for (uint32_t eye = 0; eye < VRDisplayState::NumEyes; eye++) {
    // As of Oculus 1.17 SDK, we must use the ovr_GetRenderDesc2 function to
    // return the updated version of ovrEyeRenderDesc.  This is normally done by
    // the Oculus static lib shim, but we need to do this explicitly as we are
    // loading the Oculus runtime dll directly.
    ovrEyeRenderDesc renderDesc =
        ovr_GetRenderDesc2(mSession, (ovrEyeType)eye, mFOVPort[eye]);
    aState.displayState.eyeTranslation[eye].x =
        renderDesc.HmdToEyePose.Position.x;
    aState.displayState.eyeTranslation[eye].y =
        renderDesc.HmdToEyePose.Position.y;
    aState.displayState.eyeTranslation[eye].z =
        renderDesc.HmdToEyePose.Position.z;

    Matrix4x4 pose;
    pose.SetRotationFromQuaternion(
        gfx::Quaternion(-renderDesc.HmdToEyePose.Orientation.x,
                        -renderDesc.HmdToEyePose.Orientation.y,
                        -renderDesc.HmdToEyePose.Orientation.z,
                        renderDesc.HmdToEyePose.Orientation.w));
    pose.PreTranslate(renderDesc.HmdToEyePose.Position.x,
                      renderDesc.HmdToEyePose.Position.y,
                      renderDesc.HmdToEyePose.Position.z);
    pose.Invert();
    headToEyeTransforms[eye] = pose;
  }
  aState.sensorState.CalcViewMatrices(headToEyeTransforms);

  Matrix4x4 matView[2];
  memcpy(matView[0].components, aState.sensorState.leftViewMatrix.data(),
         sizeof(float) * 16);
  memcpy(matView[1].components, aState.sensorState.rightViewMatrix.data(),
         sizeof(float) * 16);

  for (uint32_t eye = 0; eye < VRDisplayState::NumEyes; eye++) {
    Point3D eyeTranslation;
    Quaternion eyeRotation;
    Point3D eyeScale;
    if (!matView[eye].Decompose(eyeTranslation, eyeRotation, eyeScale)) {
      NS_WARNING("Failed to decompose eye pose matrix for Oculus");
    }

    eyeRotation.Invert();
    mFrameStartPose[eye].Orientation.x = eyeRotation.x;
    mFrameStartPose[eye].Orientation.y = eyeRotation.y;
    mFrameStartPose[eye].Orientation.z = eyeRotation.z;
    mFrameStartPose[eye].Orientation.w = eyeRotation.w;
    mFrameStartPose[eye].Position.x = eyeTranslation.x;
    mFrameStartPose[eye].Position.y = eyeTranslation.y;
    mFrameStartPose[eye].Position.z = eyeTranslation.z;
  }
}

void OculusSession::UpdateHeadsetPose(VRSystemState& aState) {
  if (!mSession) {
    return;
  }
  double predictedFrameTime = 0.0f;
  if (StaticPrefs::dom_vr_poseprediction_enabled()) {
    // XXX We might need to call ovr_GetPredictedDisplayTime even if we don't
    // use the result. If we don't call it, the Oculus driver will spew out many
    // warnings...
    predictedFrameTime = ovr_GetPredictedDisplayTime(mSession, 0);
  }
  ovrTrackingState trackingState =
      ovr_GetTrackingState(mSession, predictedFrameTime, true);
  ovrPoseStatef& pose(trackingState.HeadPose);

  aState.sensorState.timestamp = pose.TimeInSeconds;

  if (trackingState.StatusFlags & ovrStatus_OrientationTracked) {
    aState.sensorState.flags |= VRDisplayCapabilityFlags::Cap_Orientation;

    aState.sensorState.pose.orientation[0] = pose.ThePose.Orientation.x;
    aState.sensorState.pose.orientation[1] = pose.ThePose.Orientation.y;
    aState.sensorState.pose.orientation[2] = pose.ThePose.Orientation.z;
    aState.sensorState.pose.orientation[3] = pose.ThePose.Orientation.w;

    aState.sensorState.pose.angularVelocity[0] = pose.AngularVelocity.x;
    aState.sensorState.pose.angularVelocity[1] = pose.AngularVelocity.y;
    aState.sensorState.pose.angularVelocity[2] = pose.AngularVelocity.z;

    aState.sensorState.flags |=
        VRDisplayCapabilityFlags::Cap_AngularAcceleration;

    aState.sensorState.pose.angularAcceleration[0] = pose.AngularAcceleration.x;
    aState.sensorState.pose.angularAcceleration[1] = pose.AngularAcceleration.y;
    aState.sensorState.pose.angularAcceleration[2] = pose.AngularAcceleration.z;
  } else {
    // default to an identity quaternion
    aState.sensorState.pose.orientation[3] = 1.0f;
  }

  if (trackingState.StatusFlags & ovrStatus_PositionTracked) {
    float eyeHeight =
        ovr_GetFloat(mSession, OVR_KEY_EYE_HEIGHT, OVR_DEFAULT_EYE_HEIGHT);
    aState.sensorState.flags |= VRDisplayCapabilityFlags::Cap_Position;

    aState.sensorState.pose.position[0] = pose.ThePose.Position.x;
    aState.sensorState.pose.position[1] = pose.ThePose.Position.y - eyeHeight;
    aState.sensorState.pose.position[2] = pose.ThePose.Position.z;

    aState.sensorState.pose.linearVelocity[0] = pose.LinearVelocity.x;
    aState.sensorState.pose.linearVelocity[1] = pose.LinearVelocity.y;
    aState.sensorState.pose.linearVelocity[2] = pose.LinearVelocity.z;

    aState.sensorState.flags |=
        VRDisplayCapabilityFlags::Cap_LinearAcceleration;

    aState.sensorState.pose.linearAcceleration[0] = pose.LinearAcceleration.x;
    aState.sensorState.pose.linearAcceleration[1] = pose.LinearAcceleration.y;
    aState.sensorState.pose.linearAcceleration[2] = pose.LinearAcceleration.z;
  }
  aState.sensorState.flags |= VRDisplayCapabilityFlags::Cap_External;
  aState.sensorState.flags |= VRDisplayCapabilityFlags::Cap_MountDetection;
  aState.sensorState.flags |= VRDisplayCapabilityFlags::Cap_Present;
}

void OculusSession::UpdateControllers(VRSystemState& aState) {
  if (!mSession) {
    return;
  }

  ovrInputState inputState;
  bool hasInputState = ovr_GetInputState(mSession, ovrControllerType_Touch,
                                         &inputState) == ovrSuccess;

  if (!hasInputState) {
    return;
  }

  EnumerateControllers(aState, inputState);
  UpdateControllerInputs(aState, inputState);
  UpdateControllerPose(aState, inputState);
}

void OculusSession::UpdateControllerPose(VRSystemState& aState,
                                         const ovrInputState& aInputState) {
  ovrTrackingState trackingState = ovr_GetTrackingState(mSession, 0.0, false);
  for (uint32_t handIdx = 0; handIdx < 2; handIdx++) {
    // Left Touch Controller will always be at index 0 and
    // and Right Touch Controller will always be at index 1
    VRControllerState& controllerState = aState.controllerState[handIdx];
    if (aInputState.ControllerType & OculusControllerTypes[handIdx]) {
      ovrPoseStatef& pose = trackingState.HandPoses[handIdx];
      bool bNewController = !(controllerState.flags &
                              dom::GamepadCapabilityFlags::Cap_Orientation);
      if (bNewController) {
        controllerState.flags |= dom::GamepadCapabilityFlags::Cap_Orientation;
        controllerState.flags |= dom::GamepadCapabilityFlags::Cap_Position;
        controllerState.flags |=
            dom::GamepadCapabilityFlags::Cap_AngularAcceleration;
        controllerState.flags |=
            dom::GamepadCapabilityFlags::Cap_LinearAcceleration;
        controllerState.flags |=
            dom::GamepadCapabilityFlags::Cap_GripSpacePosition;
      }

      if (bNewController || trackingState.HandStatusFlags[handIdx] &
                                ovrStatus_OrientationTracked) {
        controllerState.pose.orientation[0] = pose.ThePose.Orientation.x;
        controllerState.pose.orientation[1] = pose.ThePose.Orientation.y;
        controllerState.pose.orientation[2] = pose.ThePose.Orientation.z;
        controllerState.pose.orientation[3] = pose.ThePose.Orientation.w;
        controllerState.pose.angularVelocity[0] = pose.AngularVelocity.x;
        controllerState.pose.angularVelocity[1] = pose.AngularVelocity.y;
        controllerState.pose.angularVelocity[2] = pose.AngularVelocity.z;
        controllerState.pose.angularAcceleration[0] =
            pose.AngularAcceleration.x;
        controllerState.pose.angularAcceleration[1] =
            pose.AngularAcceleration.y;
        controllerState.pose.angularAcceleration[2] =
            pose.AngularAcceleration.z;
        controllerState.isOrientationValid = true;
      } else {
        controllerState.isOrientationValid = false;
      }
      if (bNewController ||
          trackingState.HandStatusFlags[handIdx] & ovrStatus_PositionTracked) {
        controllerState.pose.position[0] = pose.ThePose.Position.x;
        controllerState.pose.position[1] = pose.ThePose.Position.y;
        controllerState.pose.position[2] = pose.ThePose.Position.z;
        controllerState.pose.linearVelocity[0] = pose.LinearVelocity.x;
        controllerState.pose.linearVelocity[1] = pose.LinearVelocity.y;
        controllerState.pose.linearVelocity[2] = pose.LinearVelocity.z;
        controllerState.pose.linearAcceleration[0] = pose.LinearAcceleration.x;
        controllerState.pose.linearAcceleration[1] = pose.LinearAcceleration.y;
        controllerState.pose.linearAcceleration[2] = pose.LinearAcceleration.z;

        float eyeHeight =
            ovr_GetFloat(mSession, OVR_KEY_EYE_HEIGHT, OVR_DEFAULT_EYE_HEIGHT);
        controllerState.pose.position[1] -= eyeHeight;
        controllerState.isPositionValid = true;
      } else {
        controllerState.isPositionValid = false;
      }
      controllerState.targetRayPose = controllerState.pose;
    }
  }
}

void OculusSession::EnumerateControllers(VRSystemState& aState,
                                         const ovrInputState& aInputState) {
  for (uint32_t handIdx = 0; handIdx < 2; handIdx++) {
    // Left Touch Controller will always be at index 0 and
    // and Right Touch Controller will always be at index 1
    VRControllerState& controllerState = aState.controllerState[handIdx];
    if (aInputState.ControllerType & OculusControllerTypes[handIdx]) {
      // Touch Controller detected
      if (controllerState.controllerName[0] == '\0') {
        // Controller has been just enumerated
        strncpy(controllerState.controllerName.data(),
                OculusControllerNames[handIdx],
                controllerState.controllerName.size());
        controllerState.hand = OculusControllerHand[handIdx];
        controllerState.targetRayMode = gfx::TargetRayMode::TrackedPointer;
        controllerState.numButtons = kNumOculusButtons;
        controllerState.numAxes = kNumOculusAxes;
        controllerState.numHaptics = kNumOculusHaptcs;
        controllerState.type = VRControllerType::OculusTouch;
      }
    } else {
      // Touch Controller not detected
      if (controllerState.controllerName[0] != '\0') {
        // Clear any newly disconnected ontrollers
        memset(&controllerState, 0, sizeof(VRControllerState));
      }
    }
  }
}

void OculusSession::UpdateControllerInputs(VRSystemState& aState,
                                           const ovrInputState& aInputState) {
  const float triggerThreshold =
      StaticPrefs::dom_vr_controller_trigger_threshold();

  for (uint32_t handIdx = 0; handIdx < 2; handIdx++) {
    // Left Touch Controller will always be at index 0 and
    // and Right Touch Controller will always be at index 1
    VRControllerState& controllerState = aState.controllerState[handIdx];
    if (aInputState.ControllerType & OculusControllerTypes[handIdx]) {
      // Update Button States
      controllerState.buttonPressed = 0;
      controllerState.buttonTouched = 0;
      uint32_t buttonIdx = 0;

      // Button 0: Trigger
      VRSession::UpdateTrigger(controllerState, buttonIdx,
                               aInputState.IndexTrigger[handIdx],
                               triggerThreshold);
      ++buttonIdx;
      // Button 1: Grip
      VRSession::UpdateTrigger(controllerState, buttonIdx,
                               aInputState.HandTrigger[handIdx],
                               triggerThreshold);
      ++buttonIdx;
      // Button 2: a placeholder button for trackpad.
      UpdateButton(aInputState, handIdx, buttonIdx, controllerState);
      ++buttonIdx;
      // Button 3: Thumbstick
      UpdateButton(aInputState, handIdx, buttonIdx, controllerState);
      ++buttonIdx;
      // Button 4: A
      UpdateButton(aInputState, handIdx, buttonIdx, controllerState);
      ++buttonIdx;
      // Button 5: B
      UpdateButton(aInputState, handIdx, buttonIdx, controllerState);
      ++buttonIdx;
      // Button 6: ThumbRest
      UpdateButton(aInputState, handIdx, buttonIdx, controllerState);
      ++buttonIdx;

      MOZ_RELEASE_ASSERT(buttonIdx == kNumOculusButtons);

      // Update Thumbstick axis
      uint32_t axisIdx = 0;
      // Axis 0, 1: placeholder axes for trackpad.
      axisIdx += 2;

      // Axis 2, 3: placeholder axes for thumbstick.
      float axisValue = aInputState.Thumbstick[handIdx].x;
      if (abs(axisValue) < 0.0000009f) {
        axisValue = 0.0f;  // Clear noise signal
      }
      controllerState.axisValue[axisIdx] = axisValue;
      axisIdx++;

      // Note that y axis is intentionally inverted!
      axisValue = -aInputState.Thumbstick[handIdx].y;
      if (abs(axisValue) < 0.0000009f) {
        axisValue = 0.0f;  // Clear noise signal
      }
      controllerState.axisValue[axisIdx] = axisValue;
      axisIdx++;

      MOZ_RELEASE_ASSERT(axisIdx == kNumOculusAxes);
    }
    SetControllerSelectionAndSqueezeFrameId(
        controllerState, aState.displayState.lastSubmittedFrameId);
  }
}

void OculusSession::UpdateTelemetry(VRSystemState& aSystemState) {
  if (!mSession) {
    return;
  }
  ovrPerfStats perfStats;
  if (ovr_GetPerfStats(mSession, &perfStats) == ovrSuccess) {
    if (perfStats.FrameStatsCount) {
      aSystemState.displayState.droppedFrameCount =
          perfStats.FrameStats[0].AppDroppedFrameCount;
    }
  }
}

void OculusSession::VibrateHaptic(uint32_t aControllerIdx,
                                  uint32_t aHapticIndex, float aIntensity,
                                  float aDuration) {
  if (!mSession) {
    return;
  }

  if (aDuration <= 0.0f) {
    StopVibrateHaptic(aControllerIdx);
    return;
  }

  // Vibration amplitude in the [0.0, 1.0] range
  MOZ_RELEASE_ASSERT(aControllerIdx >= 0 && aControllerIdx <= 1);
  mHapticPulseIntensity[aControllerIdx] = aIntensity > 1.0 ? 1.0 : aIntensity;
  mRemainingVibrateTime[aControllerIdx] = aDuration;
  ovrControllerType hand = OculusControllerTypes[aControllerIdx];

  // The gamepad extensions API does not yet have independent control
  // of frequency and amplitude.  We are always sending 0.0f (160hz)
  // to the frequency argument.
  ovrResult result = ovr_SetControllerVibration(
      mSession, hand, 0.0f, mHapticPulseIntensity[aControllerIdx]);
  if (result != ovrSuccess) {
    // This may happen if called when not presenting.
    gfxWarning() << "ovr_SetControllerVibration failed.";
  }
}

void OculusSession::StopVibrateHaptic(uint32_t aControllerIdx) {
  if (!mSession) {
    return;
  }
  MOZ_RELEASE_ASSERT(aControllerIdx >= 0 && aControllerIdx <= 1);
  ovrControllerType hand = OculusControllerTypes[aControllerIdx];
  mRemainingVibrateTime[aControllerIdx] = 0.0f;
  mHapticPulseIntensity[aControllerIdx] = 0.0f;

  ovrResult result = ovr_SetControllerVibration(mSession, hand, 0.0f, 0.0f);
  if (result != ovrSuccess) {
    // This may happen if called when not presenting.
    gfxWarning() << "ovr_SetControllerVibration failed.";
  }
}

void OculusSession::StopAllHaptics() {
  // Left Oculus Touch
  StopVibrateHaptic(0);
  // Right Oculus Touch
  StopVibrateHaptic(1);
}

void OculusSession::UpdateHaptics() {
  if (!mSession) {
    return;
  }
  // The Oculus API and hardware takes at least 33ms to respond
  // to haptic state changes, so it is not beneficial to create
  // a dedicated haptic feedback thread and update multiple
  // times per frame.
  // If we wish to support more accurate effects with sub-frame timing,
  // we should use the buffered haptic feedback API's.

  TimeStamp now = TimeStamp::Now();
  if (mLastHapticUpdate.IsNull()) {
    mLastHapticUpdate = now;
    return;
  }
  float deltaTime = (float)(now - mLastHapticUpdate).ToSeconds();
  mLastHapticUpdate = now;
  for (int i = 0; i < 2; i++) {
    if (mRemainingVibrateTime[i] <= 0.0f) {
      continue;
    }
    mRemainingVibrateTime[i] -= deltaTime;
    ovrControllerType hand = OculusControllerTypes[i];
    if (mRemainingVibrateTime[i] > 0.0f) {
      ovrResult result = ovr_SetControllerVibration(mSession, hand, 0.0f,
                                                    mHapticPulseIntensity[i]);
      if (result != ovrSuccess) {
        // This may happen if called when not presenting.
        gfxWarning() << "ovr_SetControllerVibration failed.";
      }
    } else {
      StopVibrateHaptic(i);
    }
  }
}

}  // namespace gfx
}  // namespace mozilla
