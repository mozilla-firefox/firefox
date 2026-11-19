/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "OpenVRSession.h"

#include <fstream>
#include <numbers>

#include "mozilla/ClearOnShutdown.h"
#include "mozilla/JSONStringWriteFuncs.h"
#include "mozilla/StaticPrefs_dom.h"
#include "nsIThread.h"
#include "nsString.h"

#if defined(XP_WIN)
#  include <d3d11.h>

#  include "mozilla/gfx/DeviceManagerDx.h"
#elif defined(XP_MACOSX)
#  include "mozilla/gfx/MacIOSurface.h"
#endif

#if !defined(XP_WIN)
#  include <sys/stat.h>  // for umask()
#endif

#include "OpenVRCosmosMapper.h"
#include "OpenVRDefaultMapper.h"
#include "OpenVRKnucklesMapper.h"
#include "OpenVRViveMapper.h"
#include "binding/OpenVRCosmosBinding.h"
#include "binding/OpenVRKnucklesBinding.h"
#include "binding/OpenVRViveBinding.h"
#include "mozilla/dom/GamepadBinding.h"
#include "mozilla/dom/GamepadEventTypes.h"
#if defined(XP_WIN)  // Windows Mixed Reality is only available in Windows.
#  include "OpenVRWMRMapper.h"
#  include "binding/OpenVRWMRBinding.h"
#endif

#include "VRParent.h"
#include "VRProcessChild.h"
#include "VRThread.h"

#if !defined(M_PI)
#  define M_PI 3.14159265358979323846264338327950288
#endif

#define BTN_MASK_FROM_ID(_id) ::vr::ButtonMaskFromId(vr::EVRButtonId::_id)

// Haptic feedback is updated every 5ms, as this is
// the minimum period between new haptic pulse requests.
// Effectively, this results in a pulse width modulation
// with an interval of 5ms.  Through experimentation, the
// maximum duty cycle was found to be about 3.9ms
const uint32_t kVRHapticUpdateInterval = 5;

using namespace mozilla::gfx;

namespace mozilla::gfx {

namespace {

class ControllerManifestFile {
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(ControllerManifestFile)

 public:
  static already_AddRefed<ControllerManifestFile> CreateManifest() {
    RefPtr<ControllerManifestFile> manifest = new ControllerManifestFile();
    return manifest.forget();
  }

  bool IsExisting() {
    if (mFileName.IsEmpty() || !std::ifstream(mFileName.get()).good()) {
      return false;
    }
    return true;
  }

  void SetFileName(const char* aName) { mFileName = aName; }

  const char* GetFileName() const { return mFileName.get(); }

 private:
  ControllerManifestFile() = default;

  ~ControllerManifestFile() {
    if (!mFileName.IsEmpty() && remove(mFileName.get()) != 0) {
      MOZ_ASSERT(false, "Delete controller manifest file failed.");
    }
    mFileName = "";
  }

  nsCString mFileName;
};

// We wanna keep these temporary files existing
// until Firefox is closed instead of following OpenVRSession's lifetime.
StaticRefPtr<ControllerManifestFile> sCosmosBindingFile;
StaticRefPtr<ControllerManifestFile> sKnucklesBindingFile;
StaticRefPtr<ControllerManifestFile> sViveBindingFile;
#if defined(XP_WIN)
StaticRefPtr<ControllerManifestFile> sWMRBindingFile;
#endif
StaticRefPtr<ControllerManifestFile> sControllerActionFile;

dom::GamepadHand GetControllerHandFromControllerRole(OpenVRHand aRole) {
  dom::GamepadHand hand;
  switch (aRole) {
    case OpenVRHand::None:
      hand = dom::GamepadHand::_empty;
      break;
    case OpenVRHand::Left:
      hand = dom::GamepadHand::Left;
      break;
    case OpenVRHand::Right:
      hand = dom::GamepadHand::Right;
      break;
    default:
      hand = dom::GamepadHand::_empty;
      MOZ_ASSERT(false);
      break;
  }

  return hand;
}

bool FileIsExisting(const nsCString& aPath) {
  if (aPath.IsEmpty() || !std::ifstream(aPath.get()).good()) {
    return false;
  }
  return true;
}

};  // anonymous namespace

#if defined(XP_WIN)
bool GenerateTempFileName(nsCString& aPath) {
  TCHAR tempPathBuffer[MAX_PATH];
  TCHAR tempFileName[MAX_PATH];

  // Gets the temp path env string (no guarantee it's a valid path).
  DWORD dwRetVal = GetTempPath(MAX_PATH, tempPathBuffer);
  if (dwRetVal > MAX_PATH || (dwRetVal == 0)) {
    NS_WARNING("OpenVR - Creating temp path failed.");
    return false;
  }

  // Generates a temporary file name.
  UINT uRetVal = GetTempFileName(tempPathBuffer,  // directory for tmp files
                                 TEXT("mozvr"),   // temp file name prefix
                                 0,               // create unique name
                                 tempFileName);   // buffer for name
  if (uRetVal == 0) {
    NS_WARNING("OpenVR - Creating temp file failed.");
    return false;
  }

  aPath.Assign(NS_ConvertUTF16toUTF8(tempFileName));
  return true;
}
#else
bool GenerateTempFileName(nsCString& aPath) {
  const char tmp[] = "/tmp/mozvrXXXXXX";
  char fileName[PATH_MAX];

  strcpy(fileName, tmp);
  const mode_t prevMask = umask(S_IXUSR | S_IRWXO | S_IRWXG);
  const int fd = mkstemp(fileName);
  umask(prevMask);
  if (fd == -1) {
    NS_WARNING(nsPrintfCString("OpenVR - Creating temp file failed: %s",
                               strerror(errno))
                   .get());
    return false;
  }
  close(fd);

  aPath.Assign(fileName);
  return true;
}
#endif  // defined(XP_WIN)

OpenVRSession::OpenVRSession()
    : mVRSystem(nullptr),
      mVRChaperone(nullptr),
      mVRCompositor(nullptr),
      mHapticPulseRemaining{},
      mHapticPulseIntensity{},
      mIsWindowsMR(false),
      mControllerHapticStateMutex(
          "OpenVRSession::mControllerHapticStateMutex") {
  std::fill_n(mControllerDeviceIndex, kVRControllerMaxCount, OpenVRHand::None);
}

OpenVRSession::~OpenVRSession() {
  mActionsetFirefox = ::vr::k_ulInvalidActionSetHandle;
  Shutdown();
}

bool OpenVRSession::Initialize(mozilla::gfx::VRSystemState& aSystemState,
                               bool aDetectRuntimesOnly) {
  if (StaticPrefs::dom_vr_puppet_enabled()) {
    // Ensure that tests using the VR Puppet do not find real hardware
    return false;
  }
  if (!StaticPrefs::dom_vr_enabled() || !StaticPrefs::dom_vr_openvr_enabled()) {
    return false;
  }
  if (mVRSystem != nullptr) {
    // Already initialized
    return true;
  }
  if (!::vr::VR_IsRuntimeInstalled()) {
    return false;
  }
  if (aDetectRuntimesOnly) {
    aSystemState.displayState.capabilityFlags |=
        VRDisplayCapabilityFlags::Cap_ImmersiveVR;
    return false;
  }
  if (!::vr::VR_IsHmdPresent()) {
    return false;
  }

  ::vr::HmdError err;

  ::vr::VR_Init(&err, ::vr::EVRApplicationType::VRApplication_Scene);
  if (err) {
    return false;
  }

  mVRSystem = (::vr::IVRSystem*)::vr::VR_GetGenericInterface(
      ::vr::IVRSystem_Version, &err);
  if (err || !mVRSystem) {
    Shutdown();
    return false;
  }
  mVRChaperone = (::vr::IVRChaperone*)::vr::VR_GetGenericInterface(
      ::vr::IVRChaperone_Version, &err);
  if (err || !mVRChaperone) {
    Shutdown();
    return false;
  }
  mVRCompositor = (::vr::IVRCompositor*)::vr::VR_GetGenericInterface(
      ::vr::IVRCompositor_Version, &err);
  if (err || !mVRCompositor) {
    Shutdown();
    return false;
  }

#if defined(XP_WIN)
  if (!CreateD3DObjects()) {
    Shutdown();
    return false;
  }

#endif

  // Configure coordinate system
  mVRCompositor->SetTrackingSpace(::vr::TrackingUniverseSeated);

  if (!InitState(aSystemState)) {
    Shutdown();
    return false;
  }
  if (!SetupContollerActions()) {
    return false;
  }

  // Succeeded
  return true;
}

// "actions": [] Action paths must take the form: "/actions/<action
// set>/in|out/<action>"
#define CreateControllerAction(hand, name, type) \
  ControllerAction("/actions/firefox/in/" #hand "Hand_" #name, #type)
#define CreateControllerOutAction(hand, name, type) \
  ControllerAction("/actions/firefox/out/" #hand "Hand_" #name, #type)

bool OpenVRSession::SetupContollerActions() {
  if (!vr::VRInput()) {
    NS_WARNING("OpenVR - vr::VRInput() is null.");
    return false;
  }

  // Check if this device binding file has been created.
  // If it didn't exist yet, create a new temp file.
  nsCString controllerAction;
  nsCString viveManifest;
  nsCString WMRManifest;
  nsCString knucklesManifest;
  nsCString cosmosManifest;

  // Getting / Generating manifest file paths.
  if (StaticPrefs::dom_vr_process_enabled_AtStartup()) {
    VRParent* vrParent = VRProcessChild::GetVRParent();
    nsCString output;

    if (vrParent->GetOpenVRControllerActionPath(&output)) {
      controllerAction = output;
    }

    if (vrParent->GetOpenVRControllerManifestPath(VRControllerType::HTCVive,
                                                  &output)) {
      viveManifest = output;
    }
    if (!viveManifest.Length() || !FileIsExisting(viveManifest)) {
      if (!GenerateTempFileName(viveManifest)) {
        return false;
      }
      OpenVRViveBinding viveBinding;
      std::ofstream viveBindingFile(viveManifest.get());
      if (viveBindingFile.is_open()) {
        viveBindingFile << viveBinding.binding;
        viveBindingFile.close();
      }
    }

#if defined(XP_WIN)
    if (vrParent->GetOpenVRControllerManifestPath(VRControllerType::MSMR,
                                                  &output)) {
      WMRManifest = output;
    }
    if (!WMRManifest.Length() || !FileIsExisting(WMRManifest)) {
      if (!GenerateTempFileName(WMRManifest)) {
        return false;
      }
      OpenVRWMRBinding WMRBinding;
      std::ofstream WMRBindingFile(WMRManifest.get());
      if (WMRBindingFile.is_open()) {
        WMRBindingFile << WMRBinding.binding;
        WMRBindingFile.close();
      }
    }
#endif
    if (vrParent->GetOpenVRControllerManifestPath(VRControllerType::ValveIndex,
                                                  &output)) {
      knucklesManifest = output;
    }
    if (!knucklesManifest.Length() || !FileIsExisting(knucklesManifest)) {
      if (!GenerateTempFileName(knucklesManifest)) {
        return false;
      }
      OpenVRKnucklesBinding knucklesBinding;
      std::ofstream knucklesBindingFile(knucklesManifest.get());
      if (knucklesBindingFile.is_open()) {
        knucklesBindingFile << knucklesBinding.binding;
        knucklesBindingFile.close();
      }
    }
    if (vrParent->GetOpenVRControllerManifestPath(
            VRControllerType::HTCViveCosmos, &output)) {
      cosmosManifest = std::move(output);
    }
    if (!cosmosManifest.Length() || !FileIsExisting(cosmosManifest)) {
      if (!GenerateTempFileName(cosmosManifest)) {
        return false;
      }
      OpenVRCosmosBinding cosmosBinding;
      std::ofstream cosmosBindingFile(cosmosManifest.get());
      if (cosmosBindingFile.is_open()) {
        cosmosBindingFile << cosmosBinding.binding;
        cosmosBindingFile.close();
      }
    }
  } else {
    // Without using VR process
    if (!sControllerActionFile) {
      sControllerActionFile = ControllerManifestFile::CreateManifest();
      NS_DispatchToMainThread(NS_NewRunnableFunction(
          "ClearOnShutdown ControllerManifestFile",
          []() { ClearOnShutdown(&sControllerActionFile); }));
    }
    controllerAction = sControllerActionFile->GetFileName();

    if (!sViveBindingFile) {
      sViveBindingFile = ControllerManifestFile::CreateManifest();
      NS_DispatchToMainThread(
          NS_NewRunnableFunction("ClearOnShutdown ControllerManifestFile",
                                 []() { ClearOnShutdown(&sViveBindingFile); }));
    }
    if (!sViveBindingFile->IsExisting()) {
      nsCString viveBindingPath;
      if (!GenerateTempFileName(viveBindingPath)) {
        return false;
      }
      sViveBindingFile->SetFileName(viveBindingPath.get());
      OpenVRViveBinding viveBinding;
      std::ofstream viveBindingFile(sViveBindingFile->GetFileName());
      if (viveBindingFile.is_open()) {
        viveBindingFile << viveBinding.binding;
        viveBindingFile.close();
      }
    }
    viveManifest = sViveBindingFile->GetFileName();

    if (!sKnucklesBindingFile) {
      sKnucklesBindingFile = ControllerManifestFile::CreateManifest();
      NS_DispatchToMainThread(NS_NewRunnableFunction(
          "ClearOnShutdown ControllerManifestFile",
          []() { ClearOnShutdown(&sKnucklesBindingFile); }));
    }
    if (!sKnucklesBindingFile->IsExisting()) {
      nsCString knucklesBindingPath;
      if (!GenerateTempFileName(knucklesBindingPath)) {
        return false;
      }
      sKnucklesBindingFile->SetFileName(knucklesBindingPath.get());
      OpenVRKnucklesBinding knucklesBinding;
      std::ofstream knucklesBindingFile(sKnucklesBindingFile->GetFileName());
      if (knucklesBindingFile.is_open()) {
        knucklesBindingFile << knucklesBinding.binding;
        knucklesBindingFile.close();
      }
    }
    knucklesManifest = sKnucklesBindingFile->GetFileName();

    if (!sCosmosBindingFile) {
      sCosmosBindingFile = ControllerManifestFile::CreateManifest();
      NS_DispatchToMainThread(NS_NewRunnableFunction(
          "ClearOnShutdown ControllerManifestFile",
          []() { ClearOnShutdown(&sCosmosBindingFile); }));
    }
    if (!sCosmosBindingFile->IsExisting()) {
      nsCString cosmosBindingPath;
      if (!GenerateTempFileName(cosmosBindingPath)) {
        return false;
      }
      sCosmosBindingFile->SetFileName(cosmosBindingPath.get());
      OpenVRCosmosBinding cosmosBinding;
      std::ofstream cosmosBindingFile(sCosmosBindingFile->GetFileName());
      if (cosmosBindingFile.is_open()) {
        cosmosBindingFile << cosmosBinding.binding;
        cosmosBindingFile.close();
      }
    }
    cosmosManifest = sCosmosBindingFile->GetFileName();
#if defined(XP_WIN)
    if (!sWMRBindingFile) {
      sWMRBindingFile = ControllerManifestFile::CreateManifest();
      NS_DispatchToMainThread(
          NS_NewRunnableFunction("ClearOnShutdown ControllerManifestFile",
                                 []() { ClearOnShutdown(&sWMRBindingFile); }));
    }
    if (!sWMRBindingFile->IsExisting()) {
      nsCString WMRBindingPath;
      if (!GenerateTempFileName(WMRBindingPath)) {
        return false;
      }
      sWMRBindingFile->SetFileName(WMRBindingPath.get());
      OpenVRWMRBinding WMRBinding;
      std::ofstream WMRBindingFile(sWMRBindingFile->GetFileName());
      if (WMRBindingFile.is_open()) {
        WMRBindingFile << WMRBinding.binding;
        WMRBindingFile.close();
      }
    }
    WMRManifest = sWMRBindingFile->GetFileName();
#endif
  }
  // End of Getting / Generating manifest file paths.

  // Setup controller actions.
  ControllerInfo leftContollerInfo;
  leftContollerInfo.mActionPose = CreateControllerAction(L, pose, pose);
  leftContollerInfo.mActionTrackpad_Analog =
      CreateControllerAction(L, trackpad_analog, vector2);
  leftContollerInfo.mActionTrackpad_Pressed =
      CreateControllerAction(L, trackpad_pressed, boolean);
  leftContollerInfo.mActionTrackpad_Touched =
      CreateControllerAction(L, trackpad_touched, boolean);
  leftContollerInfo.mActionTrigger_Value =
      CreateControllerAction(L, trigger_value, vector1);
  leftContollerInfo.mActionGrip_Pressed =
      CreateControllerAction(L, grip_pressed, boolean);
  leftContollerInfo.mActionGrip_Touched =
      CreateControllerAction(L, grip_touched, boolean);
  leftContollerInfo.mActionMenu_Pressed =
      CreateControllerAction(L, menu_pressed, boolean);
  leftContollerInfo.mActionMenu_Touched =
      CreateControllerAction(L, menu_touched, boolean);
  leftContollerInfo.mActionSystem_Pressed =
      CreateControllerAction(L, system_pressed, boolean);
  leftContollerInfo.mActionSystem_Touched =
      CreateControllerAction(L, system_touched, boolean);
  leftContollerInfo.mActionA_Pressed =
      CreateControllerAction(L, A_pressed, boolean);
  leftContollerInfo.mActionA_Touched =
      CreateControllerAction(L, A_touched, boolean);
  leftContollerInfo.mActionB_Pressed =
      CreateControllerAction(L, B_pressed, boolean);
  leftContollerInfo.mActionB_Touched =
      CreateControllerAction(L, B_touched, boolean);
  leftContollerInfo.mActionThumbstick_Analog =
      CreateControllerAction(L, thumbstick_analog, vector2);
  leftContollerInfo.mActionThumbstick_Pressed =
      CreateControllerAction(L, thumbstick_pressed, boolean);
  leftContollerInfo.mActionThumbstick_Touched =
      CreateControllerAction(L, thumbstick_touched, boolean);
  leftContollerInfo.mActionFingerIndex_Value =
      CreateControllerAction(L, finger_index_value, vector1);
  leftContollerInfo.mActionFingerMiddle_Value =
      CreateControllerAction(L, finger_middle_value, vector1);
  leftContollerInfo.mActionFingerRing_Value =
      CreateControllerAction(L, finger_ring_value, vector1);
  leftContollerInfo.mActionFingerPinky_Value =
      CreateControllerAction(L, finger_pinky_value, vector1);
  leftContollerInfo.mActionBumper_Pressed =
      CreateControllerAction(L, bumper_pressed, boolean);
  leftContollerInfo.mActionHaptic =
      CreateControllerOutAction(L, haptic, vibration);

  ControllerInfo rightContollerInfo;
  rightContollerInfo.mActionPose = CreateControllerAction(R, pose, pose);
  rightContollerInfo.mActionTrackpad_Analog =
      CreateControllerAction(R, trackpad_analog, vector2);
  rightContollerInfo.mActionTrackpad_Pressed =
      CreateControllerAction(R, trackpad_pressed, boolean);
  rightContollerInfo.mActionTrackpad_Touched =
      CreateControllerAction(R, trackpad_touched, boolean);
  rightContollerInfo.mActionTrigger_Value =
      CreateControllerAction(R, trigger_value, vector1);
  rightContollerInfo.mActionGrip_Pressed =
      CreateControllerAction(R, grip_pressed, boolean);
  rightContollerInfo.mActionGrip_Touched =
      CreateControllerAction(R, grip_touched, boolean);
  rightContollerInfo.mActionMenu_Pressed =
      CreateControllerAction(R, menu_pressed, boolean);
  rightContollerInfo.mActionMenu_Touched =
      CreateControllerAction(R, menu_touched, boolean);
  rightContollerInfo.mActionSystem_Pressed =
      CreateControllerAction(R, system_pressed, boolean);
  rightContollerInfo.mActionSystem_Touched =
      CreateControllerAction(R, system_touched, boolean);
  rightContollerInfo.mActionA_Pressed =
      CreateControllerAction(R, A_pressed, boolean);
  rightContollerInfo.mActionA_Touched =
      CreateControllerAction(R, A_touched, boolean);
  rightContollerInfo.mActionB_Pressed =
      CreateControllerAction(R, B_pressed, boolean);
  rightContollerInfo.mActionB_Touched =
      CreateControllerAction(R, B_touched, boolean);
  rightContollerInfo.mActionThumbstick_Analog =
      CreateControllerAction(R, thumbstick_analog, vector2);
  rightContollerInfo.mActionThumbstick_Pressed =
      CreateControllerAction(R, thumbstick_pressed, boolean);
  rightContollerInfo.mActionThumbstick_Touched =
      CreateControllerAction(R, thumbstick_touched, boolean);
  rightContollerInfo.mActionFingerIndex_Value =
      CreateControllerAction(R, finger_index_value, vector1);
  rightContollerInfo.mActionFingerMiddle_Value =
      CreateControllerAction(R, finger_middle_value, vector1);
  rightContollerInfo.mActionFingerRing_Value =
      CreateControllerAction(R, finger_ring_value, vector1);
  rightContollerInfo.mActionFingerPinky_Value =
      CreateControllerAction(R, finger_pinky_value, vector1);
  rightContollerInfo.mActionBumper_Pressed =
      CreateControllerAction(R, bumper_pressed, boolean);
  rightContollerInfo.mActionHaptic =
      CreateControllerOutAction(R, haptic, vibration);

  mControllerHand[OpenVRHand::Left] = std::move(leftContollerInfo);
  mControllerHand[OpenVRHand::Right] = std::move(rightContollerInfo);

  if (!controllerAction.Length() || !FileIsExisting(controllerAction)) {
    if (!GenerateTempFileName(controllerAction)) {
      return false;
    }
    JSONStringWriteFunc<nsCString> actionData;
    JSONWriter actionWriter(actionData);
    actionWriter.Start();

    actionWriter.StringProperty("version",
                                "0.1.0");  // TODO: adding a version check.
    // "default_bindings": []
    actionWriter.StartArrayProperty("default_bindings");

    auto SetupActionWriterByControllerType = [&](const char* aType,
                                                 const nsCString& aManifest) {
      actionWriter.StartObjectElement();
      actionWriter.StringProperty("controller_type", MakeStringSpan(aType));
      actionWriter.StringProperty("binding_url", aManifest);
      actionWriter.EndObject();
    };
    SetupActionWriterByControllerType("vive_controller", viveManifest);
    SetupActionWriterByControllerType("knuckles", knucklesManifest);
    SetupActionWriterByControllerType("vive_cosmos_controller", cosmosManifest);
#if defined(XP_WIN)
    SetupActionWriterByControllerType("holographic_controller", WMRManifest);
#endif
    actionWriter.EndArray();  // End "default_bindings": []

    actionWriter.StartArrayProperty("actions");

    for (auto& controller : mControllerHand) {
      auto SetActionsToWriter = [&](const ControllerAction& aAction) {
        actionWriter.StartObjectElement();
        actionWriter.StringProperty("name", aAction.name);
        actionWriter.StringProperty("type", aAction.type);
        actionWriter.EndObject();
      };

      SetActionsToWriter(controller.mActionPose);
      SetActionsToWriter(controller.mActionTrackpad_Analog);
      SetActionsToWriter(controller.mActionTrackpad_Pressed);
      SetActionsToWriter(controller.mActionTrackpad_Touched);
      SetActionsToWriter(controller.mActionTrigger_Value);
      SetActionsToWriter(controller.mActionGrip_Pressed);
      SetActionsToWriter(controller.mActionGrip_Touched);
      SetActionsToWriter(controller.mActionMenu_Pressed);
      SetActionsToWriter(controller.mActionMenu_Touched);
      SetActionsToWriter(controller.mActionSystem_Pressed);
      SetActionsToWriter(controller.mActionSystem_Touched);
      SetActionsToWriter(controller.mActionA_Pressed);
      SetActionsToWriter(controller.mActionA_Touched);
      SetActionsToWriter(controller.mActionB_Pressed);
      SetActionsToWriter(controller.mActionB_Touched);
      SetActionsToWriter(controller.mActionThumbstick_Analog);
      SetActionsToWriter(controller.mActionThumbstick_Pressed);
      SetActionsToWriter(controller.mActionThumbstick_Touched);
      SetActionsToWriter(controller.mActionFingerIndex_Value);
      SetActionsToWriter(controller.mActionFingerMiddle_Value);
      SetActionsToWriter(controller.mActionFingerRing_Value);
      SetActionsToWriter(controller.mActionFingerPinky_Value);
      SetActionsToWriter(controller.mActionBumper_Pressed);
      SetActionsToWriter(controller.mActionHaptic);
    }
    actionWriter.EndArray();  // End "actions": []
    actionWriter.End();

    std::ofstream actionfile(controllerAction.get());
    if (actionfile.is_open()) {
      actionfile << actionData.StringCRef().get();
      actionfile.close();
    }
  }

  vr::EVRInputError err =
      vr::VRInput()->SetActionManifestPath(controllerAction.get());
  if (err != vr::VRInputError_None) {
    NS_WARNING("OpenVR - SetActionManifestPath failed.");
    return false;
  }
  // End of setup controller actions.

  // Notify the parent process these manifest files are already been recorded.
  if (StaticPrefs::dom_vr_process_enabled_AtStartup()) {
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "SendOpenVRControllerActionPathToParent",
        [controllerAction = std::move(controllerAction),
         viveManifest = std::move(viveManifest),
         WMRManifest = std::move(WMRManifest),
         knucklesManifest = std::move(knucklesManifest),
         cosmosManifest = std::move(cosmosManifest)]() {
          VRParent* vrParent = VRProcessChild::GetVRParent();
          (void)vrParent->SendOpenVRControllerActionPathToParent(
              controllerAction);
          (void)vrParent->SendOpenVRControllerManifestPathToParent(
              VRControllerType::HTCVive, viveManifest);
          (void)vrParent->SendOpenVRControllerManifestPathToParent(
              VRControllerType::MSMR, WMRManifest);
          (void)vrParent->SendOpenVRControllerManifestPathToParent(
              VRControllerType::ValveIndex, knucklesManifest);
          (void)vrParent->SendOpenVRControllerManifestPathToParent(
              VRControllerType::HTCViveCosmos, cosmosManifest);
        }));
  } else {
    sControllerActionFile->SetFileName(controllerAction.get());
  }

  return true;
}

#if defined(XP_WIN)
bool OpenVRSession::CreateD3DObjects() {
  RefPtr<ID3D11Device> device = gfx::DeviceManagerDx::Get()->GetVRDevice();
  if (!device) {
    return false;
  }
  if (!CreateD3DContext(device)) {
    return false;
  }
  return true;
}
#endif

void OpenVRSession::Shutdown() {
  StopHapticTimer();
  StopHapticThread();
  if (mVRSystem || mVRCompositor || mVRChaperone) {
    ::vr::VR_Shutdown();
    mVRCompositor = nullptr;
    mVRChaperone = nullptr;
    mVRSystem = nullptr;
  }
}

bool OpenVRSession::InitState(VRSystemState& aSystemState) {
  VRDisplayState& state = aSystemState.displayState;
  strncpy(state.displayName.data(), "OpenVR HMD", kVRDisplayNameMaxLen);
  state.eightCC = GFX_VR_EIGHTCC('O', 'p', 'e', 'n', 'V', 'R', ' ', ' ');
  state.isConnected =
      mVRSystem->IsTrackedDeviceConnected(::vr::k_unTrackedDeviceIndex_Hmd);
  state.isMounted = false;
  state.capabilityFlags =
      (VRDisplayCapabilityFlags)((int)VRDisplayCapabilityFlags::Cap_None |
                                 (int)
                                     VRDisplayCapabilityFlags::Cap_Orientation |
                                 (int)VRDisplayCapabilityFlags::Cap_Position |
                                 (int)VRDisplayCapabilityFlags::Cap_External |
                                 (int)VRDisplayCapabilityFlags::Cap_Present |
                                 (int)VRDisplayCapabilityFlags::
                                     Cap_StageParameters |
                                 (int)
                                     VRDisplayCapabilityFlags::Cap_ImmersiveVR);
  state.blendMode = VRDisplayBlendMode::Opaque;
  state.reportsDroppedFrames = true;

  ::vr::ETrackedPropertyError err;
  bool bHasProximitySensor = mVRSystem->GetBoolTrackedDeviceProperty(
      ::vr::k_unTrackedDeviceIndex_Hmd, ::vr::Prop_ContainsProximitySensor_Bool,
      &err);
  if (err == ::vr::TrackedProp_Success && bHasProximitySensor) {
    state.capabilityFlags =
        (VRDisplayCapabilityFlags)((int)state.capabilityFlags |
                                   (int)VRDisplayCapabilityFlags::
                                       Cap_MountDetection);
  }

  uint32_t w, h;
  mVRSystem->GetRecommendedRenderTargetSize(&w, &h);
  state.eyeResolution.width = w;
  state.eyeResolution.height = h;
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

void OpenVRSession::UpdateStageParameters(VRDisplayState& aState) {
  float sizeX = 0.0f;
  float sizeZ = 0.0f;
  if (mVRChaperone->GetPlayAreaSize(&sizeX, &sizeZ)) {
    ::vr::HmdMatrix34_t t =
        mVRSystem->GetSeatedZeroPoseToStandingAbsoluteTrackingPose();
    aState.stageSize.width = sizeX;
    aState.stageSize.height = sizeZ;

    aState.sittingToStandingTransform[0] = t.m[0][0];
    aState.sittingToStandingTransform[1] = t.m[1][0];
    aState.sittingToStandingTransform[2] = t.m[2][0];
    aState.sittingToStandingTransform[3] = 0.0f;

    aState.sittingToStandingTransform[4] = t.m[0][1];
    aState.sittingToStandingTransform[5] = t.m[1][1];
    aState.sittingToStandingTransform[6] = t.m[2][1];
    aState.sittingToStandingTransform[7] = 0.0f;

    aState.sittingToStandingTransform[8] = t.m[0][2];
    aState.sittingToStandingTransform[9] = t.m[1][2];
    aState.sittingToStandingTransform[10] = t.m[2][2];
    aState.sittingToStandingTransform[11] = 0.0f;

    aState.sittingToStandingTransform[12] = t.m[0][3];
    aState.sittingToStandingTransform[13] = t.m[1][3];
    aState.sittingToStandingTransform[14] = t.m[2][3];
    aState.sittingToStandingTransform[15] = 1.0f;
  } else {
    // If we fail, fall back to reasonable defaults.
    // 1m x 1m space, 0.75m high in seated position
    aState.stageSize.width = 1.0f;
    aState.stageSize.height = 1.0f;

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
    aState.sittingToStandingTransform[13] = 0.75f;
    aState.sittingToStandingTransform[14] = 0.0f;
    aState.sittingToStandingTransform[15] = 1.0f;
  }
}

void OpenVRSession::UpdateEyeParameters(VRSystemState& aState) {
  // This must be called every frame in order to
  // account for continuous adjustments to ipd.
  gfx::Matrix4x4 headToEyeTransforms[2];

  for (uint32_t eye = 0; eye < 2; ++eye) {
    ::vr::HmdMatrix34_t eyeToHead =
        mVRSystem->GetEyeToHeadTransform(static_cast<::vr::Hmd_Eye>(eye));
    aState.displayState.eyeTranslation[eye].x = eyeToHead.m[0][3];
    aState.displayState.eyeTranslation[eye].y = eyeToHead.m[1][3];
    aState.displayState.eyeTranslation[eye].z = eyeToHead.m[2][3];

    float left, right, up, down;
    mVRSystem->GetProjectionRaw(static_cast<::vr::Hmd_Eye>(eye), &left, &right,
                                &up, &down);
    aState.displayState.eyeFOV[eye].upDegrees =
        atan(-up) * 180.0 / std::numbers::pi;
    aState.displayState.eyeFOV[eye].rightDegrees =
        atan(right) * 180.0 / std::numbers::pi;
    aState.displayState.eyeFOV[eye].downDegrees =
        atan(down) * 180.0 / std::numbers::pi;
    aState.displayState.eyeFOV[eye].leftDegrees =
        atan(-left) * 180.0 / std::numbers::pi;

    Matrix4x4 pose;
    // NOTE! eyeToHead.m is a 3x4 matrix, not 4x4.  But
    // because of its arrangement, we can copy the 12 elements in and
    // then transpose them to the right place.
    memcpy(&pose._11, &eyeToHead.m, sizeof(eyeToHead.m));
    pose.Transpose();
    pose.Invert();
    headToEyeTransforms[eye] = pose;
  }
  aState.sensorState.CalcViewMatrices(headToEyeTransforms);
}

void OpenVRSession::UpdateHeadsetPose(VRSystemState& aState) {
  const uint32_t posesSize = ::vr::k_unTrackedDeviceIndex_Hmd + 1;
  ::vr::TrackedDevicePose_t poses[posesSize];
  // Note: We *must* call WaitGetPoses in order for any rendering to happen at
  // all.
  mVRCompositor->WaitGetPoses(poses, posesSize, nullptr, 0);

  ::vr::Compositor_FrameTiming timing;
  timing.m_nSize = sizeof(::vr::Compositor_FrameTiming);
  if (mVRCompositor->GetFrameTiming(&timing)) {
    aState.sensorState.timestamp = timing.m_flSystemTimeInSeconds;
  } else {
    // This should not happen, but log it just in case
    fprintf(stderr, "OpenVR - IVRCompositor::GetFrameTiming failed");
  }

  if (poses[::vr::k_unTrackedDeviceIndex_Hmd].bDeviceIsConnected &&
      poses[::vr::k_unTrackedDeviceIndex_Hmd].bPoseIsValid &&
      poses[::vr::k_unTrackedDeviceIndex_Hmd].eTrackingResult ==
          ::vr::TrackingResult_Running_OK) {
    const ::vr::TrackedDevicePose_t& pose =
        poses[::vr::k_unTrackedDeviceIndex_Hmd];

    gfx::Matrix4x4 m;
    // NOTE! mDeviceToAbsoluteTracking is a 3x4 matrix, not 4x4.  But
    // because of its arrangement, we can copy the 12 elements in and
    // then transpose them to the right place.  We do this so we can
    // pull out a Quaternion.
    memcpy(&m._11, &pose.mDeviceToAbsoluteTracking,
           sizeof(pose.mDeviceToAbsoluteTracking));
    m.Transpose();

    gfx::Quaternion rot;
    rot.SetFromRotationMatrix(m);

    aState.sensorState.flags =
        (VRDisplayCapabilityFlags)((int)aState.sensorState.flags |
                                   (int)VRDisplayCapabilityFlags::
                                       Cap_Orientation);
    aState.sensorState.pose.orientation[0] = rot.x;
    aState.sensorState.pose.orientation[1] = rot.y;
    aState.sensorState.pose.orientation[2] = rot.z;
    aState.sensorState.pose.orientation[3] = rot.w;
    aState.sensorState.pose.angularVelocity[0] = pose.vAngularVelocity.v[0];
    aState.sensorState.pose.angularVelocity[1] = pose.vAngularVelocity.v[1];
    aState.sensorState.pose.angularVelocity[2] = pose.vAngularVelocity.v[2];

    aState.sensorState.flags =
        (VRDisplayCapabilityFlags)((int)aState.sensorState.flags |
                                   (int)VRDisplayCapabilityFlags::Cap_Position);
    aState.sensorState.pose.position[0] = m._41;
    aState.sensorState.pose.position[1] = m._42;
    aState.sensorState.pose.position[2] = m._43;
    aState.sensorState.pose.linearVelocity[0] = pose.vVelocity.v[0];
    aState.sensorState.pose.linearVelocity[1] = pose.vVelocity.v[1];
    aState.sensorState.pose.linearVelocity[2] = pose.vVelocity.v[2];
  }
}

void OpenVRSession::EnumerateControllers(VRSystemState& aState) {
  MOZ_RELEASE_ASSERT(mVRSystem);

  MutexAutoLock lock(mControllerHapticStateMutex);

  bool controllerPresent[kVRControllerMaxCount] = {false};
  uint32_t stateIndex = 0;
  mActionsetFirefox = vr::k_ulInvalidActionSetHandle;
  VRControllerType controllerType = VRControllerType::_empty;

  if (vr::VRInput()->GetActionSetHandle(
          "/actions/firefox", &mActionsetFirefox) != vr::VRInputError_None) {
    return;
  }

  for (int8_t handIndex = 0; handIndex < OpenVRHand::Total; ++handIndex) {
    if (handIndex == OpenVRHand::Left) {
      if (vr::VRInput()->GetInputSourceHandle(
              "/user/hand/left", &mControllerHand[OpenVRHand::Left].mSource) !=
          vr::VRInputError_None) {
        continue;
      }
    } else if (handIndex == OpenVRHand::Right) {
      if (vr::VRInput()->GetInputSourceHandle(
              "/user/hand/right",
              &mControllerHand[OpenVRHand::Right].mSource) !=
          vr::VRInputError_None) {
        continue;
      }
    } else {
      MOZ_RELEASE_ASSERT(false, "Unknown OpenVR hand type.");
    }

    vr::InputOriginInfo_t originInfo;
    if (vr::VRInput()->GetOriginTrackedDeviceInfo(
            mControllerHand[handIndex].mSource, &originInfo,
            sizeof(originInfo)) == vr::VRInputError_None &&
        originInfo.trackedDeviceIndex != vr::k_unTrackedDeviceIndexInvalid &&
        mVRSystem->IsTrackedDeviceConnected(originInfo.trackedDeviceIndex)) {
      const ::vr::ETrackedDeviceClass deviceType =
          mVRSystem->GetTrackedDeviceClass(originInfo.trackedDeviceIndex);
      if (deviceType != ::vr::TrackedDeviceClass_Controller &&
          deviceType != ::vr::TrackedDeviceClass_GenericTracker) {
        continue;
      }

      if (mControllerDeviceIndex[stateIndex] != handIndex) {
        VRControllerState& controllerState = aState.controllerState[stateIndex];

        // Get controllers' action handles.
        auto SetActionsToWriter = [&](ControllerAction& aAction) {
          vr::VRInput()->GetActionHandle(aAction.name.get(), &aAction.handle);
        };

        SetActionsToWriter(mControllerHand[handIndex].mActionPose);
        SetActionsToWriter(mControllerHand[handIndex].mActionHaptic);
        SetActionsToWriter(mControllerHand[handIndex].mActionTrackpad_Analog);
        SetActionsToWriter(mControllerHand[handIndex].mActionTrackpad_Pressed);
        SetActionsToWriter(mControllerHand[handIndex].mActionTrackpad_Touched);
        SetActionsToWriter(mControllerHand[handIndex].mActionTrigger_Value);
        SetActionsToWriter(mControllerHand[handIndex].mActionGrip_Pressed);
        SetActionsToWriter(mControllerHand[handIndex].mActionGrip_Touched);
        SetActionsToWriter(mControllerHand[handIndex].mActionMenu_Pressed);
        SetActionsToWriter(mControllerHand[handIndex].mActionMenu_Touched);
        SetActionsToWriter(mControllerHand[handIndex].mActionSystem_Pressed);
        SetActionsToWriter(mControllerHand[handIndex].mActionSystem_Touched);
        SetActionsToWriter(mControllerHand[handIndex].mActionA_Pressed);
        SetActionsToWriter(mControllerHand[handIndex].mActionA_Touched);
        SetActionsToWriter(mControllerHand[handIndex].mActionB_Pressed);
        SetActionsToWriter(mControllerHand[handIndex].mActionB_Touched);
        SetActionsToWriter(mControllerHand[handIndex].mActionThumbstick_Analog);
        SetActionsToWriter(
            mControllerHand[handIndex].mActionThumbstick_Pressed);
        SetActionsToWriter(
            mControllerHand[handIndex].mActionThumbstick_Touched);
        SetActionsToWriter(mControllerHand[handIndex].mActionFingerIndex_Value);
        SetActionsToWriter(
            mControllerHand[handIndex].mActionFingerMiddle_Value);
        SetActionsToWriter(mControllerHand[handIndex].mActionFingerRing_Value);
        SetActionsToWriter(mControllerHand[handIndex].mActionFingerPinky_Value);
        SetActionsToWriter(mControllerHand[handIndex].mActionBumper_Pressed);

        nsCString deviceId;
        VRControllerType contrlType = VRControllerType::_empty;
        GetControllerDeviceId(deviceType, originInfo.trackedDeviceIndex,
                              deviceId, contrlType);
        // Controllers should be the same type with one VR display.
        MOZ_ASSERT(controllerType == contrlType ||
                   controllerType == VRControllerType::_empty);
        controllerType = contrlType;
        strncpy(controllerState.controllerName.data(), deviceId.BeginReading(),
                controllerState.controllerName.size());
        controllerState.numHaptics = kNumOpenVRHaptics;
        controllerState.targetRayMode = gfx::TargetRayMode::TrackedPointer;
        controllerState.type = controllerType;
      }
      controllerPresent[stateIndex] = true;
      mControllerDeviceIndex[stateIndex] = static_cast<OpenVRHand>(handIndex);
      ++stateIndex;
    }
  }

  // Clear out entries for disconnected controllers
  for (uint32_t stateIndex = 0; stateIndex < kVRControllerMaxCount;
       stateIndex++) {
    if (!controllerPresent[stateIndex] &&
        mControllerDeviceIndex[stateIndex] != OpenVRHand::None) {
      mControllerDeviceIndex[stateIndex] = OpenVRHand::None;
      memset(&aState.controllerState[stateIndex], 0, sizeof(VRControllerState));
    }
  }

  // Create controller mapper
  if (controllerType != VRControllerType::_empty) {
    switch (controllerType) {
      case VRControllerType::HTCVive:
        mControllerMapper = std::make_unique<OpenVRViveMapper>();
        break;
      case VRControllerType::HTCViveCosmos:
        mControllerMapper = std::make_unique<OpenVRCosmosMapper>();
        break;
#if defined(XP_WIN)
      case VRControllerType::MSMR:
        mControllerMapper = std::make_unique<OpenVRWMRMapper>();
        break;
#endif
      case VRControllerType::ValveIndex:
        mControllerMapper = std::make_unique<OpenVRKnucklesMapper>();
        break;
      default:
        mControllerMapper = std::make_unique<OpenVRDefaultMapper>();
        NS_WARNING("Undefined controller type");
        break;
    }
  }
}

void OpenVRSession::UpdateControllerButtons(VRSystemState& aState) {
  MOZ_RELEASE_ASSERT(mVRSystem);

  for (uint32_t stateIndex = 0; stateIndex < kVRControllerMaxCount;
       ++stateIndex) {
    const OpenVRHand role = mControllerDeviceIndex[stateIndex];
    if (role == OpenVRHand::None) {
      continue;
    }
    VRControllerState& controllerState = aState.controllerState[stateIndex];
    controllerState.hand = GetControllerHandFromControllerRole(role);
    mControllerMapper->UpdateButtons(controllerState, mControllerHand[role]);
    SetControllerSelectionAndSqueezeFrameId(
        controllerState, aState.displayState.lastSubmittedFrameId);
  }
}

void OpenVRSession::UpdateControllerPoses(VRSystemState& aState) {
  MOZ_RELEASE_ASSERT(mVRSystem);

  for (uint32_t stateIndex = 0; stateIndex < kVRControllerMaxCount;
       ++stateIndex) {
    const OpenVRHand role = mControllerDeviceIndex[stateIndex];
    if (role == OpenVRHand::None) {
      continue;
    }
    VRControllerState& controllerState = aState.controllerState[stateIndex];
    vr::InputPoseActionData_t poseData;
    if (vr::VRInput()->GetPoseActionDataRelativeToNow(
            mControllerHand[role].mActionPose.handle,
            vr::TrackingUniverseSeated, 0, &poseData, sizeof(poseData),
            vr::k_ulInvalidInputValueHandle) != vr::VRInputError_None ||
        !poseData.bActive || !poseData.pose.bPoseIsValid) {
      controllerState.isOrientationValid = false;
      controllerState.isPositionValid = false;
    } else {
      const ::vr::TrackedDevicePose_t& pose = poseData.pose;
      if (pose.bDeviceIsConnected) {
        controllerState.flags =
            (dom::GamepadCapabilityFlags::Cap_Orientation |
             dom::GamepadCapabilityFlags::Cap_Position |
             dom::GamepadCapabilityFlags::Cap_GripSpacePosition);
      } else {
        controllerState.flags = dom::GamepadCapabilityFlags::Cap_None;
      }
      if (pose.bPoseIsValid &&
          pose.eTrackingResult == ::vr::TrackingResult_Running_OK) {
        gfx::Matrix4x4 m;

        // NOTE! mDeviceToAbsoluteTracking is a 3x4 matrix, not 4x4.  But
        // because of its arrangement, we can copy the 12 elements in and
        // then transpose them to the right place.  We do this so we can
        // pull out a Quaternion.
        memcpy(&m.components, &pose.mDeviceToAbsoluteTracking,
               sizeof(pose.mDeviceToAbsoluteTracking));
        m.Transpose();

        gfx::Quaternion rot;
        rot.SetFromRotationMatrix(m);

        controllerState.pose.orientation[0] = rot.x;
        controllerState.pose.orientation[1] = rot.y;
        controllerState.pose.orientation[2] = rot.z;
        controllerState.pose.orientation[3] = rot.w;
        controllerState.pose.angularVelocity[0] = pose.vAngularVelocity.v[0];
        controllerState.pose.angularVelocity[1] = pose.vAngularVelocity.v[1];
        controllerState.pose.angularVelocity[2] = pose.vAngularVelocity.v[2];
        controllerState.pose.angularAcceleration[0] = 0.0f;
        controllerState.pose.angularAcceleration[1] = 0.0f;
        controllerState.pose.angularAcceleration[2] = 0.0f;
        controllerState.isOrientationValid = true;

        controllerState.pose.position[0] = m._41;
        controllerState.pose.position[1] = m._42;
        controllerState.pose.position[2] = m._43;
        controllerState.pose.linearVelocity[0] = pose.vVelocity.v[0];
        controllerState.pose.linearVelocity[1] = pose.vVelocity.v[1];
        controllerState.pose.linearVelocity[2] = pose.vVelocity.v[2];
        controllerState.pose.linearAcceleration[0] = 0.0f;
        controllerState.pose.linearAcceleration[1] = 0.0f;
        controllerState.pose.linearAcceleration[2] = 0.0f;
        controllerState.isPositionValid = true;

        // Calculate its target ray space by shifting degrees in x-axis
        // for ergonomic.
        const float kPointerAngleDegrees = -0.698;  // 40 degrees.
        gfx::Matrix4x4 rayMtx(m);
        rayMtx.RotateX(kPointerAngleDegrees);
        gfx::Quaternion rayRot;
        rayRot.SetFromRotationMatrix(rayMtx);

        controllerState.targetRayPose = controllerState.pose;
        controllerState.targetRayPose.orientation[0] = rayRot.x;
        controllerState.targetRayPose.orientation[1] = rayRot.y;
        controllerState.targetRayPose.orientation[2] = rayRot.z;
        controllerState.targetRayPose.orientation[3] = rayRot.w;
        controllerState.targetRayPose.position[0] = rayMtx._41;
        controllerState.targetRayPose.position[1] = rayMtx._42;
        controllerState.targetRayPose.position[2] = rayMtx._43;
      }
    }
  }
}

void OpenVRSession::GetControllerDeviceId(
    ::vr::ETrackedDeviceClass aDeviceType,
    ::vr::TrackedDeviceIndex_t aDeviceIndex, nsCString& aId,
    VRControllerType& aControllerType) {
  switch (aDeviceType) {
    case ::vr::TrackedDeviceClass_Controller: {
      ::vr::ETrackedPropertyError err;
      uint32_t requiredBufferLen;
      bool isFound = false;
      char charBuf[128];
      requiredBufferLen = mVRSystem->GetStringTrackedDeviceProperty(
          aDeviceIndex, ::vr::Prop_RenderModelName_String, charBuf, 128, &err);
      if (requiredBufferLen > 128) {
        MOZ_CRASH("Larger than the buffer size.");
      }
      MOZ_ASSERT(requiredBufferLen && err == ::vr::TrackedProp_Success);
      nsCString deviceId(charBuf);
      if (deviceId.Find("vr_controller_vive") != kNotFound) {
        aId.AssignLiteral("OpenVR Gamepad");
        isFound = true;
        aControllerType = VRControllerType::HTCVive;
      } else if (deviceId.Find("knuckles") != kNotFound ||
                 deviceId.Find("valve_controller_knu") != kNotFound) {
        aId.AssignLiteral("OpenVR Knuckles");
        isFound = true;
        aControllerType = VRControllerType::ValveIndex;
      } else if (deviceId.Find("vive_cosmos_controller") != kNotFound) {
        aId.AssignLiteral("OpenVR Cosmos");
        isFound = true;
        aControllerType = VRControllerType::HTCViveCosmos;
      }
      if (!isFound) {
        requiredBufferLen = mVRSystem->GetStringTrackedDeviceProperty(
            aDeviceIndex, ::vr::Prop_SerialNumber_String, charBuf, 128, &err);
        if (requiredBufferLen > 128) {
          MOZ_CRASH("Larger than the buffer size.");
        }
        MOZ_ASSERT(requiredBufferLen && err == ::vr::TrackedProp_Success);
        deviceId.Assign(charBuf);
        if (deviceId.Find("MRSOURCE") != kNotFound) {
          aId.AssignLiteral("Spatial Controller (Spatial Interaction Source) ");
          mIsWindowsMR = true;
          isFound = true;
          aControllerType = VRControllerType::MSMR;
        }
      }
      if (!isFound) {
        aId.AssignLiteral("OpenVR Undefined");
        aControllerType = VRControllerType::_empty;
      }
      break;
    }
    case ::vr::TrackedDeviceClass_GenericTracker: {
      aId.AssignLiteral("OpenVR Tracker");
      aControllerType = VRControllerType::_empty;
      break;
    }
    default:
      MOZ_ASSERT(false);
      break;
  }
}

void OpenVRSession::StartFrame(mozilla::gfx::VRSystemState& aSystemState) {
  UpdateHeadsetPose(aSystemState);
  UpdateEyeParameters(aSystemState);
  EnumerateControllers(aSystemState);

  vr::VRActiveActionSet_t actionSet = {0};
  actionSet.ulActionSet = mActionsetFirefox;
  vr::VRInput()->UpdateActionState(&actionSet, sizeof(actionSet), 1);
  UpdateControllerButtons(aSystemState);
  UpdateControllerPoses(aSystemState);
  UpdateTelemetry(aSystemState);
}

void OpenVRSession::ProcessEvents(mozilla::gfx::VRSystemState& aSystemState) {
  bool isHmdPresent = ::vr::VR_IsHmdPresent();
  if (!isHmdPresent) {
    mShouldQuit = true;
  }

  ::vr::VREvent_t event;
  while (mVRSystem && mVRSystem->PollNextEvent(&event, sizeof(event))) {
    switch (event.eventType) {
      case ::vr::VREvent_TrackedDeviceUserInteractionStarted:
        if (event.trackedDeviceIndex == ::vr::k_unTrackedDeviceIndex_Hmd) {
          aSystemState.displayState.isMounted = true;
        }
        break;
      case ::vr::VREvent_TrackedDeviceUserInteractionEnded:
        if (event.trackedDeviceIndex == ::vr::k_unTrackedDeviceIndex_Hmd) {
          aSystemState.displayState.isMounted = false;
        }
        break;
      case ::vr::EVREventType::VREvent_TrackedDeviceActivated:
        if (event.trackedDeviceIndex == ::vr::k_unTrackedDeviceIndex_Hmd) {
          aSystemState.displayState.isConnected = true;
        }
        break;
      case ::vr::EVREventType::VREvent_TrackedDeviceDeactivated:
        if (event.trackedDeviceIndex == ::vr::k_unTrackedDeviceIndex_Hmd) {
          aSystemState.displayState.isConnected = false;
        }
        break;
      case ::vr::EVREventType::VREvent_DriverRequestedQuit:
      case ::vr::EVREventType::VREvent_Quit:
      // When SteamVR runtime haven't been launched before viewing VR,
      // SteamVR will send a VREvent_ProcessQuit event. It will tell the parent
      // process to shutdown the VR process, and we need to avoid it.
      // case ::vr::EVREventType::VREvent_ProcessQuit:
      case ::vr::EVREventType::VREvent_QuitAcknowledged:
        mShouldQuit = true;
        break;
      default:
        // ignore
        break;
    }
  }
}

#if defined(XP_WIN)
bool OpenVRSession::SubmitFrame(
    const mozilla::gfx::VRLayer_Stereo_Immersive& aLayer,
    ID3D11Texture2D* aTexture) {
  return SubmitFrame((void*)aTexture, ::vr::ETextureType::TextureType_DirectX,
                     aLayer.leftEyeRect, aLayer.rightEyeRect);
}
#elif defined(XP_MACOSX)
bool OpenVRSession::SubmitFrame(
    const mozilla::gfx::VRLayer_Stereo_Immersive& aLayer,
    const VRLayerTextureHandle& aTexture) {
  return SubmitFrame(aTexture, ::vr::ETextureType::TextureType_IOSurface,
                     aLayer.leftEyeRect, aLayer.rightEyeRect);
}
#endif

bool OpenVRSession::SubmitFrame(const VRLayerTextureHandle& aTextureHandle,
                                ::vr::ETextureType aTextureType,
                                const VRLayerEyeRect& aLeftEyeRect,
                                const VRLayerEyeRect& aRightEyeRect) {
  ::vr::Texture_t tex;
#if defined(XP_MACOSX)
  // We get aTextureHandle from get_SurfaceDescriptorMacIOSurface() at
  // VRDisplayExternal. scaleFactor and opaque are skipped because they always
  // are 1.0 and false.
  RefPtr<MacIOSurface> surf = MacIOSurface::LookupSurface(
      aTextureHandle, gfx::YUVColorSpace::Identity, gfx::TransferFunction::SRGB,
      MacIOSurface::AllowAlpha::No);
  if (!surf) {
    NS_WARNING("OpenVRSession::SubmitFrame failed to get a MacIOSurface");
    return false;
  }

  CFTypeRefPtr<IOSurfaceRef> ioSurface = surf->GetIOSurfaceRef();
  tex.handle = (void*)ioSurface.get();
#else
  tex.handle = aTextureHandle;
#endif
  tex.eType = aTextureType;
  tex.eColorSpace = ::vr::EColorSpace::ColorSpace_Auto;

  ::vr::VRTextureBounds_t bounds;
  bounds.uMin = aLeftEyeRect.x;
  bounds.vMin = 1.0 - aLeftEyeRect.y;
  bounds.uMax = aLeftEyeRect.x + aLeftEyeRect.width;
  bounds.vMax = 1.0 - (aLeftEyeRect.y + aLeftEyeRect.height);

  ::vr::EVRCompositorError err;
  err = mVRCompositor->Submit(::vr::EVREye::Eye_Left, &tex, &bounds);
  if (err != ::vr::EVRCompositorError::VRCompositorError_None) {
    printf_stderr("OpenVR Compositor Submit() failed.\n");
  }

  bounds.uMin = aRightEyeRect.x;
  bounds.vMin = 1.0 - aRightEyeRect.y;
  bounds.uMax = aRightEyeRect.x + aRightEyeRect.width;
  bounds.vMax = 1.0 - (aRightEyeRect.y + aRightEyeRect.height);

  err = mVRCompositor->Submit(::vr::EVREye::Eye_Right, &tex, &bounds);
  if (err != ::vr::EVRCompositorError::VRCompositorError_None) {
    printf_stderr("OpenVR Compositor Submit() failed.\n");
  }

  mVRCompositor->PostPresentHandoff();
  return true;
}

void OpenVRSession::StopPresentation() {
  mVRCompositor->ClearLastSubmittedFrame();

  ::vr::Compositor_CumulativeStats stats;
  mVRCompositor->GetCumulativeStats(&stats,
                                    sizeof(::vr::Compositor_CumulativeStats));
}

bool OpenVRSession::StartPresentation() { return true; }

void OpenVRSession::VibrateHaptic(uint32_t aControllerIdx,
                                  uint32_t aHapticIndex, float aIntensity,
                                  float aDuration) {
  MutexAutoLock lock(mControllerHapticStateMutex);

  // Initilize the haptic thread when the first time to do vibration.
  if (!mHapticThread) {
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "OpenVRSession::StartHapticThread", [this]() { StartHapticThread(); }));
  }
  if (aHapticIndex >= kNumOpenVRHaptics ||
      aControllerIdx >= kVRControllerMaxCount) {
    return;
  }

  const OpenVRHand role = mControllerDeviceIndex[aControllerIdx];
  if (role == OpenVRHand::None) {
    return;
  }
  mHapticPulseRemaining[aControllerIdx][aHapticIndex] = aDuration;
  mHapticPulseIntensity[aControllerIdx][aHapticIndex] = aIntensity;
}

void OpenVRSession::StartHapticThread() {
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  if (!mHapticThread) {
    mHapticThread = new VRThread("VR_OpenVR_Haptics"_ns);
  }
  mHapticThread->Start();
  StartHapticTimer();
}

void OpenVRSession::StopHapticThread() {
  if (mHapticThread) {
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "mHapticThread::Shutdown",
        [thread = mHapticThread]() { thread->Shutdown(); }));
    mHapticThread = nullptr;
  }
}

void OpenVRSession::StartHapticTimer() {
  if (!mHapticTimer && mHapticThread) {
    mLastHapticUpdate = TimeStamp();
    mHapticTimer = NS_NewTimer();
    nsCOMPtr<nsIThread> thread = mHapticThread->GetThread();
    mHapticTimer->SetTarget(thread);
    mHapticTimer->InitWithNamedFuncCallback(
        HapticTimerCallback, this, kVRHapticUpdateInterval,
        nsITimer::TYPE_REPEATING_PRECISE_CAN_SKIP,
        "OpenVRSession::HapticTimerCallback"_ns);
  }
}

void OpenVRSession::StopHapticTimer() {
  if (mHapticTimer) {
    mHapticTimer->Cancel();
    mHapticTimer = nullptr;
  }
}

/*static*/
void OpenVRSession::HapticTimerCallback(nsITimer* aTimer, void* aClosure) {
  /**
   * It is safe to use the pointer passed in aClosure to reference the
   * OpenVRSession object as the timer is canceled in OpenVRSession::Shutdown,
   * which is called by the OpenVRSession destructor, guaranteeing
   * that this function runs if and only if the VRManager object is valid.
   */
  OpenVRSession* self = static_cast<OpenVRSession*>(aClosure);
  MOZ_RELEASE_ASSERT(self);
  self->UpdateHaptics();
}

void OpenVRSession::UpdateHaptics() {
  MOZ_RELEASE_ASSERT(mHapticThread->GetThread() == NS_GetCurrentThread());
  MOZ_RELEASE_ASSERT(mVRSystem);

  MutexAutoLock lock(mControllerHapticStateMutex);

  TimeStamp now = TimeStamp::Now();
  if (mLastHapticUpdate.IsNull()) {
    mLastHapticUpdate = now;
    return;
  }
  float deltaTime = (float)(now - mLastHapticUpdate).ToSeconds();
  mLastHapticUpdate = now;

  for (uint32_t stateIndex = 0; stateIndex < kVRControllerMaxCount;
       ++stateIndex) {
    const OpenVRHand role = mControllerDeviceIndex[stateIndex];
    if (role == OpenVRHand::None) {
      continue;
    }
    for (uint32_t hapticIdx = 0; hapticIdx < kNumOpenVRHaptics; hapticIdx++) {
      float intensity = mHapticPulseIntensity[stateIndex][hapticIdx];
      float duration = mHapticPulseRemaining[stateIndex][hapticIdx];
      if (duration <= 0.0f || intensity <= 0.0f) {
        continue;
      }
      vr::VRInput()->TriggerHapticVibrationAction(
          mControllerHand[role].mActionHaptic.handle, 0.0f, deltaTime, 4.0f,
          intensity > 1.0f ? 1.0f : intensity, vr::k_ulInvalidInputValueHandle);

      duration -= deltaTime;
      if (duration < 0.0f) {
        duration = 0.0f;
      }
      mHapticPulseRemaining[stateIndex][hapticIdx] = duration;
    }
  }
}

void OpenVRSession::StopVibrateHaptic(uint32_t aControllerIdx) {
  MutexAutoLock lock(mControllerHapticStateMutex);
  if (aControllerIdx >= kVRControllerMaxCount) {
    return;
  }
  for (int iHaptic = 0; iHaptic < kNumOpenVRHaptics; iHaptic++) {
    mHapticPulseRemaining[aControllerIdx][iHaptic] = 0.0f;
  }
}

void OpenVRSession::StopAllHaptics() {
  MutexAutoLock lock(mControllerHapticStateMutex);
  for (auto& controller : mHapticPulseRemaining) {
    for (auto& haptic : controller) {
      haptic = 0.0f;
    }
  }
}

void OpenVRSession::UpdateTelemetry(VRSystemState& aSystemState) {
  ::vr::Compositor_CumulativeStats stats;
  mVRCompositor->GetCumulativeStats(&stats,
                                    sizeof(::vr::Compositor_CumulativeStats));
  aSystemState.displayState.droppedFrameCount = stats.m_nNumReprojectedFrames;
}

}  // namespace mozilla::gfx
