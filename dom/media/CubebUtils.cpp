/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CubebUtils.h"

#include "audio_thread_priority.h"
#include "mozilla/AbstractThread.h"
#include "mozilla/Components.h"
#include "mozilla/Logging.h"
#include "mozilla/Preferences.h"
#include "mozilla/SharedThreadPool.h"
#include "mozilla/Sprintf.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/UnderrunHandler.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/glean/DomMediaMetrics.h"
#include "base/process_util.h"
#include "mozilla/ipc/FileDescriptor.h"
#if defined(MOZ_SANDBOX)
#  include "mozilla/SandboxSettings.h"
#endif
#include <stdint.h>

#include <algorithm>

#include "nsAppRunner.h"
#include "nsContentUtils.h"
#include "nsDebug.h"
#include "nsIStringBundle.h"
#include "nsString.h"
#include "nsThreadUtils.h"
#include "prdtoa.h"
#ifdef MOZ_WIDGET_ANDROID
#  include "mozilla/java/GeckoAppShellWrappers.h"
#endif
#ifdef XP_WIN
#  include "mozilla/mscom/EnsureMTA.h"
#endif
#include <cmath>
#include <thread>

#include "CallbackThreadRegistry.h"
#include "CubebDeviceEnumerator.h"
#include "audioipc2_client_ffi_generated.h"
#include "audioipc2_server_ffi_generated.h"
#include "mozilla/StaticPrefs_media.h"

#if defined(ENABLE_TESTS) || defined(FUZZING)
#  define ENABLE_MOCK_CUBEB 1
#  include "MockCubeb.h"
#endif

#define AUDIOIPC_STACK_SIZE_DEFAULT (64 * 4096)

#define PREF_VOLUME_SCALE "media.volume_scale"
#define PREF_CUBEB_BACKEND "media.cubeb.backend"
#define PREF_CUBEB_OUTPUT_DEVICE "media.cubeb.output_device"
#define PREF_CUBEB_LATENCY_PLAYBACK "media.cubeb_latency_playback_ms"
#define PREF_CUBEB_LATENCY_MTG "media.cubeb_latency_mtg_frames"
// Allows to get something non-default for the preferred sample-rate, to allow
// troubleshooting in the field and testing.
#define PREF_CUBEB_FORCE_SAMPLE_RATE "media.cubeb.force_sample_rate"
#define PREF_CUBEB_LOGGING_LEVEL "logging.cubeb"
// Hidden pref used by tests to force failure to obtain cubeb context
#define PREF_CUBEB_FORCE_NULL_CONTEXT "media.cubeb.force_null_context"
#define PREF_CUBEB_FORCE_MOCK_CONTEXT "media.cubeb.force_mock_context"
#define PREF_CUBEB_OUTPUT_VOICE_ROUTING "media.cubeb.output_voice_routing"
#define PREF_CUBEB_SANDBOX "media.cubeb.sandbox"
#define PREF_AUDIOIPC_STACK_SIZE "media.audioipc.stack_size"
#define PREF_AUDIOIPC_SHM_AREA_SIZE "media.audioipc.shm_area_size"

#if defined(XP_LINUX) || defined(XP_MACOSX) || defined(XP_WIN)
#  define MOZ_CUBEB_REMOTING
#endif

namespace mozilla {

namespace {

using glean::media_audio::BackendLabel;

LazyLogModule gCubebLog("cubeb");

void CubebLogCallback(const char* aFmt, ...) {
  char buffer[1024];

  va_list arglist;
  va_start(arglist, aFmt);
  VsprintfLiteral(buffer, aFmt, arglist);
  MOZ_LOG(gCubebLog, LogLevel::Error, ("%s", buffer));
  va_end(arglist);
}

// This mutex protects the variables below.
StaticMutex sMutex;
enum class CubebState {
  Uninitialized = 0,
  Initialized,
  Shutdown
} sCubebState = CubebState::Uninitialized;
StaticRefPtr<CubebUtils::CubebHandle> sCubebHandle;
double sVolumeScale = 1.0;
uint32_t sCubebPlaybackLatencyInMilliseconds = 100;
uint32_t sCubebMTGLatencyInFrames = 512;
// If sCubebForcedSampleRate is zero, PreferredSampleRate will return the
// preferred sample-rate for the audio backend in use. Otherwise, it will be
// used as the preferred sample-rate.
Atomic<uint32_t> sCubebForcedSampleRate{0};
bool sCubebPlaybackLatencyPrefSet = false;
bool sCubebMTGLatencyPrefSet = false;
bool sAudioStreamInitEverSucceeded = false;
bool sCubebForceNullContext = false;
bool sRouteOutputAsVoice = false;
#ifdef MOZ_CUBEB_REMOTING
bool sCubebSandbox = false;
size_t sAudioIPCStackSize;
size_t sAudioIPCShmAreaSize;
#endif
StaticAutoPtr<char> sBrandName;
StaticAutoPtr<char> sCubebBackendName;
StaticAutoPtr<char> sCubebOutputDeviceName;
#ifdef MOZ_WIDGET_ANDROID
// Counts the number of time a request for switching to global "communication
// mode" has been received. If this is > 0, global communication mode is to be
// enabled. If it is 0, the global communication mode is to be disabled.
// This allows to correctly track the global behaviour to adopt accross
// asynchronous GraphDriver changes, on Android.
int sInCommunicationCount = 0;
#endif

const char kBrandBundleURL[] = "chrome://branding/locale/brand.properties";

MOZ_RUNINIT std::unordered_map<std::string, BackendLabel>
    kTelemetryBackendLabel = {
        {"audiounit", BackendLabel::eAudiounit},
        {"audiounit-rust", BackendLabel::eAudiounitRust},
        {"aaudio", BackendLabel::eAaudio},
        {"opensl", BackendLabel::eOpensl},
        {"wasapi", BackendLabel::eWasapi},
        {"winmm", BackendLabel::eWinmm},
        {"alsa", BackendLabel::eAlsa},
        {"jack", BackendLabel::eJack},
        {"oss", BackendLabel::eOss},
        {"pulse", BackendLabel::ePulse},
        {"pulse-rust", BackendLabel::ePulseRust},
        {"sndio", BackendLabel::eSndio},
        {"sun", BackendLabel::eSunaudio},
};

// Prefered samplerate, in Hz (characteristic of the hardware, mixer, platform,
// and API used).
//
// sMutex protects *initialization* of this, which must be performed from each
// thread before fetching, after which it is safe to fetch without holding the
// mutex because it is only written once per process execution (by the first
// initialization to complete).  Since the init must have been called on a
// given thread before fetching the value, it's guaranteed (via the mutex) that
// sufficient memory barriers have occurred to ensure the correct value is
// visible on the querying thread/CPU.
static Atomic<uint32_t> sPreferredSampleRate{0};

#ifdef MOZ_CUBEB_REMOTING
// AudioIPC server handle
void* sServerHandle = nullptr;

// Initialized during early startup, protected by sMutex.
StaticAutoPtr<ipc::FileDescriptor> sIPCConnection;

static bool StartAudioIPCServer() {
  if (sCubebSandbox) {
    audioipc2::AudioIpcServerInitParams initParams{};
    initParams.mThreadCreateCallback = [](const char* aName) {
      PROFILER_REGISTER_THREAD(aName);
    };
    initParams.mThreadDestroyCallback = []() { PROFILER_UNREGISTER_THREAD(); };

    sServerHandle = audioipc2::audioipc2_server_start(
        sBrandName, sCubebBackendName, &initParams);
  }
  return sServerHandle != nullptr;
}

static void ShutdownAudioIPCServer() {
  if (!sServerHandle) {
    return;
  }

  audioipc2::audioipc2_server_stop(sServerHandle);
  sServerHandle = nullptr;
}
#endif  // MOZ_CUBEB_REMOTING
}  // namespace

static const uint32_t CUBEB_NORMAL_LATENCY_MS = 100;
// Consevative default that can work on all platforms.
static const uint32_t CUBEB_NORMAL_LATENCY_FRAMES = 1024;

namespace CubebUtils {
nsCString ProcessingParamsToString(cubeb_input_processing_params aParams) {
  if (aParams == CUBEB_INPUT_PROCESSING_PARAM_NONE) {
    return "NONE"_ns;
  }
  nsCString str;
  for (auto p : {CUBEB_INPUT_PROCESSING_PARAM_ECHO_CANCELLATION,
                 CUBEB_INPUT_PROCESSING_PARAM_AUTOMATIC_GAIN_CONTROL,
                 CUBEB_INPUT_PROCESSING_PARAM_NOISE_SUPPRESSION,
                 CUBEB_INPUT_PROCESSING_PARAM_VOICE_ISOLATION}) {
    if (!(aParams & p)) {
      continue;
    }
    if (!str.IsEmpty()) {
      str.Append(" | ");
    }
    str.Append([&p] {
      switch (p) {
        case CUBEB_INPUT_PROCESSING_PARAM_NONE:
          // Handled above.
          MOZ_CRASH(
              "NONE is the absence of a param, thus not for logging here.");
        case CUBEB_INPUT_PROCESSING_PARAM_ECHO_CANCELLATION:
          return "AEC";
        case CUBEB_INPUT_PROCESSING_PARAM_AUTOMATIC_GAIN_CONTROL:
          return "AGC";
        case CUBEB_INPUT_PROCESSING_PARAM_NOISE_SUPPRESSION:
          return "NS";
        case CUBEB_INPUT_PROCESSING_PARAM_VOICE_ISOLATION:
          return "VOICE";
      }
      MOZ_ASSERT_UNREACHABLE("Unexpected input processing param");
      return "<Unknown input processing param>";
    }());
  }
  return str;
}

RefPtr<CubebHandle> GetCubebUnlocked();

void GetPrefAndSetString(const char* aPref, StaticAutoPtr<char>& aStorage) {
  nsAutoCString value;
  Preferences::GetCString(aPref, value);
  if (value.IsEmpty()) {
    aStorage = nullptr;
  } else {
    aStorage = new char[value.Length() + 1];
    PodCopy(aStorage.get(), value.get(), value.Length());
    aStorage[value.Length()] = 0;
  }
}

void PrefChanged(const char* aPref, void* aClosure) {
  if (strcmp(aPref, PREF_VOLUME_SCALE) == 0) {
    nsAutoCString value;
    Preferences::GetCString(aPref, value);
    StaticMutexAutoLock lock(sMutex);
    if (value.IsEmpty()) {
      sVolumeScale = 1.0;
    } else {
      sVolumeScale = std::max<double>(0, PR_strtod(value.get(), nullptr));
    }
  } else if (strcmp(aPref, PREF_CUBEB_LATENCY_PLAYBACK) == 0) {
    StaticMutexAutoLock lock(sMutex);
    // Arbitrary default stream latency of 100ms.  The higher this
    // value, the longer stream volume changes will take to become
    // audible.
    sCubebPlaybackLatencyPrefSet = Preferences::HasUserValue(aPref);
    uint32_t value = Preferences::GetUint(aPref, CUBEB_NORMAL_LATENCY_MS);
    sCubebPlaybackLatencyInMilliseconds =
        std::min<uint32_t>(std::max<uint32_t>(value, 1), 1000);
  } else if (strcmp(aPref, PREF_CUBEB_LATENCY_MTG) == 0) {
    StaticMutexAutoLock lock(sMutex);
    sCubebMTGLatencyPrefSet = Preferences::HasUserValue(aPref);
    uint32_t value = Preferences::GetUint(aPref, CUBEB_NORMAL_LATENCY_FRAMES);
    // 128 is the block size for the Web Audio API, which limits how low the
    // latency can be here.
    // We don't want to limit the upper limit too much, so that people can
    // experiment.
    sCubebMTGLatencyInFrames =
        std::min<uint32_t>(std::max<uint32_t>(value, 128), 1e6);
  } else if (strcmp(aPref, PREF_CUBEB_FORCE_SAMPLE_RATE) == 0) {
    StaticMutexAutoLock lock(sMutex);
    sCubebForcedSampleRate = Preferences::GetUint(aPref);
  } else if (strcmp(aPref, PREF_CUBEB_LOGGING_LEVEL) == 0) {
    LogLevel value =
        ToLogLevel(Preferences::GetInt(aPref, 0 /* LogLevel::Disabled */));
    if (value == LogLevel::Verbose) {
      cubeb_set_log_callback(CUBEB_LOG_VERBOSE, CubebLogCallback);
    } else if (value == LogLevel::Debug) {
      cubeb_set_log_callback(CUBEB_LOG_NORMAL, CubebLogCallback);
    } else if (value == LogLevel::Disabled) {
      cubeb_set_log_callback(CUBEB_LOG_DISABLED, nullptr);
    }
  } else if (strcmp(aPref, PREF_CUBEB_BACKEND) == 0) {
    StaticMutexAutoLock lock(sMutex);
    GetPrefAndSetString(aPref, sCubebBackendName);
  } else if (strcmp(aPref, PREF_CUBEB_OUTPUT_DEVICE) == 0) {
    StaticMutexAutoLock lock(sMutex);
    GetPrefAndSetString(aPref, sCubebOutputDeviceName);
  } else if (strcmp(aPref, PREF_CUBEB_FORCE_NULL_CONTEXT) == 0) {
    StaticMutexAutoLock lock(sMutex);
    sCubebForceNullContext = Preferences::GetBool(aPref, false);
    MOZ_LOG(gCubebLog, LogLevel::Verbose,
            ("%s: %s", PREF_CUBEB_FORCE_NULL_CONTEXT,
             sCubebForceNullContext ? "true" : "false"));
  }
#ifdef ENABLE_MOCK_CUBEB
  else if (strcmp(aPref, PREF_CUBEB_FORCE_MOCK_CONTEXT) == 0) {
    if (Preferences::GetBool(aPref, false)) {
      MockCubeb* mock = new MockCubeb();
      constexpr const char* kGroupIds[] = {"group_id_1", "group_id_2",
                                           "group_id_3"};
      {
        constexpr size_t kNumDevices = 3;
        constexpr const char* kDeviceIds[] = {"mock_input_1", "mock_input_2",
                                              "mock_input_3"};
        constexpr const char* kDeviceNames[] = {
            "Fake Audio Input 1", "Fake Audio Input 2 (PREFERRED)",
            "Fake Audio Input 3"};
        for (size_t i = 0; i < kNumDevices; ++i) {
          cubeb_device_info devinfo{
              .devid = (cubeb_devid)(i + 1),
              .device_id = kDeviceIds[i],
              .friendly_name = kDeviceNames[i],
              .group_id = kGroupIds[i],
              .vendor_name = "Mozilla",
              .type = CUBEB_DEVICE_TYPE_INPUT,
              .state = CUBEB_DEVICE_STATE_ENABLED,
              .preferred =
                  i == 1 ? CUBEB_DEVICE_PREF_ALL : CUBEB_DEVICE_PREF_NONE,
              .format = CUBEB_DEVICE_FMT_F32NE,
              .default_format = CUBEB_DEVICE_FMT_F32NE,
              .max_channels = 2,
              .default_rate = 44100,
              .max_rate = 44100,
              .min_rate = 16000,
              .latency_lo = 256,
              .latency_hi = 1024,
          };
          mock->AddDevice(devinfo);
        }
      }

      {
        constexpr size_t kNumDevices = 2;
        constexpr const char* kDeviceIds[] = {"mock_output_0", "mock_output_1"};
        constexpr const char* kDeviceNames[] = {
            "Fake Audio Output 1 (PREFERRED)", "Fake Audio Output 2"};
        for (size_t i = 0; i < kNumDevices; ++i) {
          cubeb_device_info devinfo{
              .devid = (cubeb_devid)(i + 1),
              .device_id = kDeviceIds[i],
              .friendly_name = kDeviceNames[i],
              .group_id = kGroupIds[i],
              .vendor_name = "Mozilla",
              .type = CUBEB_DEVICE_TYPE_OUTPUT,
              .state = CUBEB_DEVICE_STATE_ENABLED,
              .preferred =
                  i == 0 ? CUBEB_DEVICE_PREF_ALL : CUBEB_DEVICE_PREF_NONE,
              .format = CUBEB_DEVICE_FMT_F32NE,
              .default_format = CUBEB_DEVICE_FMT_F32NE,
              .max_channels = 2,
              .default_rate = 44100,
              .max_rate = 44100,
              .min_rate = 16000,
              .latency_lo = 256,
              .latency_hi = 1024,
          };
          mock->AddDevice(devinfo);
        }
      }
      ForceSetCubebContext(mock->AsCubebContext());
    } else {
      ForceUnsetCubebContext();
    }
  }
#endif
#ifdef MOZ_CUBEB_REMOTING
  else if (strcmp(aPref, PREF_CUBEB_SANDBOX) == 0) {
    StaticMutexAutoLock lock(sMutex);
    sCubebSandbox = Preferences::GetBool(aPref);
    MOZ_LOG(gCubebLog, LogLevel::Verbose,
            ("%s: %s", PREF_CUBEB_SANDBOX, sCubebSandbox ? "true" : "false"));
#  if defined(MOZ_SANDBOX)
    if (!sCubebSandbox && IsContentSandboxEnabled()) {
      sCubebSandbox = true;
      MOZ_LOG(gCubebLog, LogLevel::Error,
              ("%s: false, but content sandbox enabled - forcing true",
               PREF_CUBEB_SANDBOX));
    }
#  endif
  } else if (strcmp(aPref, PREF_AUDIOIPC_STACK_SIZE) == 0) {
    StaticMutexAutoLock lock(sMutex);
    sAudioIPCStackSize = Preferences::GetUint(PREF_AUDIOIPC_STACK_SIZE,
                                              AUDIOIPC_STACK_SIZE_DEFAULT);
  } else if (strcmp(aPref, PREF_AUDIOIPC_SHM_AREA_SIZE) == 0) {
    StaticMutexAutoLock lock(sMutex);
    sAudioIPCShmAreaSize = Preferences::GetUint(PREF_AUDIOIPC_SHM_AREA_SIZE);
  }
#endif
  else if (strcmp(aPref, PREF_CUBEB_OUTPUT_VOICE_ROUTING) == 0) {
    StaticMutexAutoLock lock(sMutex);
    sRouteOutputAsVoice = Preferences::GetBool(aPref);
    MOZ_LOG(gCubebLog, LogLevel::Verbose,
            ("%s: %s", PREF_CUBEB_OUTPUT_VOICE_ROUTING,
             sRouteOutputAsVoice ? "true" : "false"));
  }
}

bool GetFirstStream() {
  static bool sFirstStream = true;

  StaticMutexAutoLock lock(sMutex);
  bool result = sFirstStream;
  sFirstStream = false;
  return result;
}

double GetVolumeScale() {
  StaticMutexAutoLock lock(sMutex);
  return sVolumeScale;
}

RefPtr<CubebHandle> GetCubeb() {
  StaticMutexAutoLock lock(sMutex);
  return GetCubebUnlocked();
}

#ifdef ENABLE_MOCK_CUBEB
// This is only exported when running tests.
void ForceSetCubebContext(cubeb* aCubebContext) {
  RefPtr<CubebHandle> oldHandle;  // For release without sMutex
  {
    StaticMutexAutoLock lock(sMutex);
    oldHandle = sCubebHandle.forget();
    sCubebHandle = aCubebContext ? new CubebHandle(aCubebContext) : nullptr;
    sCubebState = CubebState::Initialized;
  }
  CubebDeviceEnumerator::Shutdown();
}

void ForceUnsetCubebContext() {
  RefPtr<CubebHandle> oldHandle;  // For release without sMutex
  StaticMutexAutoLock lock(sMutex);
  oldHandle = sCubebHandle.forget();
  sCubebState = CubebState::Uninitialized;
}
#endif

void SetInCommunication(bool aInCommunication) {
#ifdef MOZ_WIDGET_ANDROID
  StaticMutexAutoLock lock(sMutex);
  if (aInCommunication) {
    sInCommunicationCount++;
  } else {
    MOZ_ASSERT(sInCommunicationCount > 0);
    sInCommunicationCount--;
  }

  if (sInCommunicationCount == 1) {
    java::GeckoAppShell::SetCommunicationAudioModeOn(true);
  } else if (sInCommunicationCount == 0) {
    java::GeckoAppShell::SetCommunicationAudioModeOn(false);
  }
#endif
}

bool InitPreferredSampleRate() MOZ_REQUIRES(sMutex) {
  sMutex.AssertCurrentThreadOwns();
  if (sPreferredSampleRate != 0) {
    return true;
  }
#ifdef MOZ_WIDGET_ANDROID
  int rate = AndroidGetAudioOutputSampleRate();
  if (rate > 0) {
    sPreferredSampleRate = rate;
    return true;
  } else {
    return false;
  }
#else
  RefPtr<CubebHandle> handle = GetCubebUnlocked();
  if (!handle) {
    return false;
  }
  uint32_t rate;
  {
    StaticMutexAutoUnlock unlock(sMutex);
    if (cubeb_get_preferred_sample_rate(handle->Context(), &rate) != CUBEB_OK) {
      return false;
    }
  }
  sPreferredSampleRate = rate;
#endif
  MOZ_ASSERT(sPreferredSampleRate);
  return true;
}

uint32_t PreferredSampleRate(bool aShouldResistFingerprinting) {
  StaticMutexAutoLock lock(sMutex);
  if (sCubebForcedSampleRate) {
    return sCubebForcedSampleRate;
  }
  if (aShouldResistFingerprinting) {
    return 44100;
  }
  if (!InitPreferredSampleRate()) {
    return 44100;
  }
  MOZ_ASSERT(sPreferredSampleRate);
  return sPreferredSampleRate;
}

int CubebStreamInit(cubeb* context, cubeb_stream** stream,
                    char const* stream_name, cubeb_devid input_device,
                    cubeb_stream_params* input_stream_params,
                    cubeb_devid output_device,
                    cubeb_stream_params* output_stream_params,
                    uint32_t latency_frames, cubeb_data_callback data_callback,
                    cubeb_state_callback state_callback, void* user_ptr) {
  uint32_t ms = StaticPrefs::media_cubeb_slow_stream_init_ms();
  if (ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
  }
  cubeb_stream_params inputParamData;
  cubeb_stream_params outputParamData;
  cubeb_stream_params* inputParamPtr = input_stream_params;
  cubeb_stream_params* outputParamPtr = output_stream_params;
  if (input_stream_params && !output_stream_params) {
    inputParamData = *input_stream_params;
    inputParamData.rate = llround(
        static_cast<double>(StaticPrefs::media_cubeb_input_drift_factor()) *
        inputParamData.rate);
    MOZ_LOG(
        gCubebLog, LogLevel::Info,
        ("CubebStreamInit input stream rate %" PRIu32, inputParamData.rate));
    inputParamPtr = &inputParamData;
  } else if (output_stream_params && !input_stream_params) {
    outputParamData = *output_stream_params;
    outputParamData.rate = llround(
        static_cast<double>(StaticPrefs::media_cubeb_output_drift_factor()) *
        outputParamData.rate);
    MOZ_LOG(
        gCubebLog, LogLevel::Info,
        ("CubebStreamInit output stream rate %" PRIu32, outputParamData.rate));
    outputParamPtr = &outputParamData;
  }

  return cubeb_stream_init(
      context, stream, stream_name, input_device, inputParamPtr, output_device,
      outputParamPtr, latency_frames, data_callback, state_callback, user_ptr);
}

void InitBrandName() {
  if (sBrandName) {
    return;
  }
  nsAutoString brandName;
  nsCOMPtr<nsIStringBundleService> stringBundleService =
      mozilla::components::StringBundle::Service();
  if (stringBundleService) {
    nsCOMPtr<nsIStringBundle> brandBundle;
    nsresult rv = stringBundleService->CreateBundle(
        kBrandBundleURL, getter_AddRefs(brandBundle));
    if (NS_SUCCEEDED(rv)) {
      rv = brandBundle->GetStringFromName("brandShortName", brandName);
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rv) || mozilla::RunningGTest(),
          "Could not get the program name for a cubeb stream.");
    }
  }
  NS_LossyConvertUTF16toASCII ascii(brandName);
  sBrandName = new char[ascii.Length() + 1];
  PodCopy(sBrandName.get(), ascii.get(), ascii.Length());
  sBrandName[ascii.Length()] = 0;
}

#ifdef MOZ_CUBEB_REMOTING
void InitAudioIPCConnection() {
  MOZ_ASSERT(NS_IsMainThread());
  auto contentChild = dom::ContentChild::GetSingleton();
  auto promise = contentChild->SendCreateAudioIPCConnection();
  promise->Then(
      AbstractThread::MainThread(), __func__,
      [](dom::FileDescOrError&& aFD) {
        StaticMutexAutoLock lock(sMutex);
        MOZ_ASSERT(!sIPCConnection);
        if (aFD.type() == dom::FileDescOrError::Type::TFileDescriptor) {
          sIPCConnection = new ipc::FileDescriptor(std::move(aFD));
        } else {
          MOZ_LOG(gCubebLog, LogLevel::Error,
                  ("SendCreateAudioIPCConnection failed: invalid FD"));
        }
      },
      [](mozilla::ipc::ResponseRejectReason&& aReason) {
        MOZ_LOG(gCubebLog, LogLevel::Error,
                ("SendCreateAudioIPCConnection rejected: %d", int(aReason)));
      });
}
#endif

#ifdef MOZ_CUBEB_REMOTING
ipc::FileDescriptor CreateAudioIPCConnectionUnlocked(uint32_t aRemotePid) {
  MOZ_ASSERT(sCubebSandbox && XRE_IsParentProcess());
  if (!sServerHandle) {
    MOZ_LOG(gCubebLog, LogLevel::Debug, ("Starting cubeb server..."));
    if (!StartAudioIPCServer()) {
      MOZ_LOG(gCubebLog, LogLevel::Error, ("audioipc_server_start failed"));
      return ipc::FileDescriptor();
    }
  }
  MOZ_LOG(gCubebLog, LogLevel::Debug,
          ("%s: %d", PREF_AUDIOIPC_SHM_AREA_SIZE, (int)sAudioIPCShmAreaSize));
  MOZ_ASSERT(sServerHandle);
  ipc::FileDescriptor::PlatformHandleType rawFD;
  rawFD = audioipc2::audioipc2_server_new_client(sServerHandle, aRemotePid,
                                                 sAudioIPCShmAreaSize);
  ipc::FileDescriptor fd(rawFD);
  if (!fd.IsValid()) {
    MOZ_LOG(gCubebLog, LogLevel::Error, ("audioipc_server_new_client failed"));
    return ipc::FileDescriptor();
  }
  // Close rawFD since FileDescriptor's ctor cloned it.
  // TODO: Find cleaner cross-platform way to close rawFD.
#  ifdef XP_WIN
  CloseHandle(rawFD);
#  else
  close(rawFD);
#  endif
  return fd;
}
#endif

ipc::FileDescriptor CreateAudioIPCConnection(uint32_t aRemotePid) {
#ifdef MOZ_CUBEB_REMOTING
  StaticMutexAutoLock lock(sMutex);
  return CreateAudioIPCConnectionUnlocked(aRemotePid);
#else
  return ipc::FileDescriptor();
#endif
}

RefPtr<CubebHandle> GetCubebUnlocked() {
  sMutex.AssertCurrentThreadOwns();
  if (sCubebForceNullContext) {
    // Pref set such that we should return a null context
    MOZ_LOG(gCubebLog, LogLevel::Debug,
            ("%s: returning null context due to %s!", __func__,
             PREF_CUBEB_FORCE_NULL_CONTEXT));
    return nullptr;
  }
  if (sCubebState != CubebState::Uninitialized) {
    // If we have already passed the initialization point (below), just return
    // the current context, which may be null (e.g., after error or shutdown.)
    return sCubebHandle;
  }

  int rv = CUBEB_ERROR;
#ifdef MOZ_CUBEB_REMOTING
  MOZ_LOG(gCubebLog, LogLevel::Info,
          ("%s: %s", PREF_CUBEB_SANDBOX, sCubebSandbox ? "true" : "false"));

  if (sCubebSandbox) {
    if (XRE_IsParentProcess() && !sIPCConnection) {
      if (!sBrandName && NS_IsMainThread()) {
        InitBrandName();
      }
      // TODO: Don't use audio IPC when within the same process.
      auto fd = CreateAudioIPCConnectionUnlocked(base::GetCurrentProcId());
      if (fd.IsValid()) {
        sIPCConnection = new ipc::FileDescriptor(fd);
      }
    }
    if (NS_WARN_IF(!sIPCConnection)) {
      // Either the IPC connection failed to init or we're still waiting for
      // InitAudioIPCConnection to complete (bug 1454782).
      return nullptr;
    }

    MOZ_LOG(gCubebLog, LogLevel::Debug,
            ("%s: %d", PREF_AUDIOIPC_STACK_SIZE, (int)sAudioIPCStackSize));

    audioipc2::AudioIpcInitParams initParams{};
    initParams.mStackSize = sAudioIPCStackSize;
    initParams.mServerConnection =
        sIPCConnection->ClonePlatformHandle().release();
    initParams.mThreadCreateCallback = [](const char* aName) {
      PROFILER_REGISTER_THREAD(aName);
    };
    initParams.mThreadDestroyCallback = []() { PROFILER_UNREGISTER_THREAD(); };

    cubeb* temp = nullptr;
    rv = audioipc2::audioipc2_client_init(&temp, &initParams);
    if (temp) {
      sCubebHandle = new CubebHandle(temp);
    }
  } else {
#endif  // MOZ_CUBEB_REMOTING
    // When not using the audio IPC sandbox, cubeb_init below consumes
    // sBrandName directly. InitLibrary dispatches InitBrandName
    // asynchronously, so initialize it here if we are on the main thread and
    // it hasn't been set yet.
    if (!sBrandName && NS_IsMainThread()) {
      InitBrandName();
    }
#ifdef XP_WIN
    mozilla::mscom::EnsureMTA([&]() -> void {
#endif
      cubeb* temp = nullptr;
      rv = cubeb_init(&temp, sBrandName, sCubebBackendName);
      if (temp) {
        sCubebHandle = new CubebHandle(temp);
      }
#ifdef XP_WIN
    });
#endif
#ifdef MOZ_CUBEB_REMOTING
  }
  sIPCConnection = nullptr;
#endif  // MOZ_CUBEB_REMOTING
  NS_WARNING_ASSERTION(rv == CUBEB_OK, "Could not get a cubeb context.");
  sCubebState =
      (rv == CUBEB_OK) ? CubebState::Initialized : CubebState::Uninitialized;

  return sCubebHandle;
}

void ReportCubebBackendUsed() {
  RefPtr<CubebHandle> handle;
  {
    StaticMutexAutoLock lock(sMutex);
    sAudioStreamInitEverSucceeded = true;
    handle = sCubebHandle;
  }

  MOZ_RELEASE_ASSERT(handle.get());

  BackendLabel label = BackendLabel::eUnknown;
  auto backend =
      kTelemetryBackendLabel.find(cubeb_get_backend_id(handle->Context()));
  if (backend != kTelemetryBackendLabel.end()) {
    label = backend->second;
  }

  mozilla::glean::media_audio::backend.EnumGet(label).Add();
}

void ReportCubebStreamInitFailure(bool aIsFirst) {
  StaticMutexAutoLock lock(sMutex);
  if (!aIsFirst && !sAudioStreamInitEverSucceeded) {
    // This machine has no audio hardware, or it's in really bad shape, don't
    // send this info, since we want CUBEB_BACKEND_INIT_FAILURE_OTHER to detect
    // failures to open multiple streams in a process over time.
    return;
  }
  mozilla::glean::media_audio::init_failure
      .EnumGet(aIsFirst ? mozilla::glean::media_audio::InitFailureLabel::eFirst
                        : mozilla::glean::media_audio::InitFailureLabel::eOther)
      .Add();
}

uint32_t GetCubebPlaybackLatencyInMilliseconds() {
  StaticMutexAutoLock lock(sMutex);
  return sCubebPlaybackLatencyInMilliseconds;
}

bool CubebPlaybackLatencyPrefSet() {
  StaticMutexAutoLock lock(sMutex);
  return sCubebPlaybackLatencyPrefSet;
}

bool CubebMTGLatencyPrefSet() {
  StaticMutexAutoLock lock(sMutex);
  return sCubebMTGLatencyPrefSet;
}

uint32_t GetCubebMTGLatencyInFrames(cubeb_stream_params* params) {
  StaticMutexAutoLock lock(sMutex);
  if (sCubebMTGLatencyPrefSet) {
    MOZ_ASSERT(sCubebMTGLatencyInFrames > 0);
    return sCubebMTGLatencyInFrames;
  }

#ifdef MOZ_WIDGET_ANDROID
  int32_t frames = AndroidGetAudioOutputFramesPerBuffer();
  // Allow extra time until audioipc threads are scheduled with higher
  // priority (bug 1931080).  768 was not sufficient on a Samsung SM-A528B
  // when switching to the home screen.
  return std::max(1024, frames);
#else
  RefPtr<CubebHandle> handle = GetCubebUnlocked();
  if (!handle) {
    return sCubebMTGLatencyInFrames;  // default 512
  }
  uint32_t latency_frames = 0;
  int cubeb_result = CUBEB_OK;

  {
    StaticMutexAutoUnlock unlock(sMutex);
    cubeb_result =
        cubeb_get_min_latency(handle->Context(), params, &latency_frames);
  }

  if (cubeb_result != CUBEB_OK) {
    NS_WARNING("Could not get minimal latency from cubeb.");
    return sCubebMTGLatencyInFrames;  // default 512
  }
  return latency_frames;
#endif
}

static const char* gInitCallbackPrefs[] = {
    PREF_VOLUME_SCALE,
    PREF_CUBEB_OUTPUT_DEVICE,
    PREF_CUBEB_LATENCY_PLAYBACK,
    PREF_CUBEB_LATENCY_MTG,
    PREF_CUBEB_BACKEND,
    PREF_CUBEB_FORCE_SAMPLE_RATE,
    PREF_CUBEB_FORCE_NULL_CONTEXT,
    PREF_CUBEB_FORCE_MOCK_CONTEXT,
    PREF_CUBEB_SANDBOX,
    PREF_AUDIOIPC_STACK_SIZE,
    PREF_AUDIOIPC_SHM_AREA_SIZE,
    nullptr,
};

static const char* gCallbackPrefs[] = {
    // We don't want to call the callback on startup, because the pref is the
    // empty string by default ("", which means "logging disabled"). Because the
    // logging can be enabled via environment variables (MOZ_LOG="module:5"),
    // calling this callback on init would immediately re-disable the logging.
    PREF_CUBEB_LOGGING_LEVEL,
    nullptr,
};

void InitLibrary() {
  Preferences::RegisterCallbacksAndCall(PrefChanged, gInitCallbackPrefs);
  Preferences::RegisterCallbacks(PrefChanged, gCallbackPrefs);

  if (MOZ_LOG_TEST(gCubebLog, LogLevel::Verbose)) {
    cubeb_set_log_callback(CUBEB_LOG_VERBOSE, CubebLogCallback);
  } else if (MOZ_LOG_TEST(gCubebLog, LogLevel::Error)) {
    cubeb_set_log_callback(CUBEB_LOG_NORMAL, CubebLogCallback);
  }

#ifndef MOZ_WIDGET_ANDROID
  if (XRE_IsParentProcess()
#  ifdef MOZ_CUBEB_REMOTING
      || !sCubebSandbox
#  endif
  ) {
    NS_DispatchToMainThread(
        NS_NewRunnableFunction("CubebUtils::InitLibrary", &InitBrandName));
  }
#endif
#ifdef MOZ_CUBEB_REMOTING
  if (sCubebSandbox && XRE_IsContentProcess()) {
#  if defined(XP_LINUX) && !defined(MOZ_WIDGET_ANDROID)
    if (atp_set_real_time_limit(0, 48000)) {
      MOZ_LOG(gCubebLog, LogLevel::Warning,
              ("could not set real-time limit in CubebUtils::InitLibrary"));
    }
    InstallSoftRealTimeLimitHandler();
#  endif
    InitAudioIPCConnection();
  }
#endif

  // Ensure the CallbackThreadRegistry is not created in an audio callback by
  // creating it now.
  (void)CallbackThreadRegistry::Get();
}

void ShutdownLibrary() {
  Preferences::UnregisterCallbacks(PrefChanged, gInitCallbackPrefs);
  Preferences::UnregisterCallbacks(PrefChanged, gCallbackPrefs);

  cubeb_set_log_callback(CUBEB_LOG_DISABLED, nullptr);
  RefPtr<CubebHandle> trash;
  StaticMutexAutoLock lock(sMutex);
  trash = sCubebHandle.forget();
  sBrandName = nullptr;
  sCubebBackendName = nullptr;
  // This will ensure we don't try to re-create a context.
  sCubebState = CubebState::Shutdown;

  if (trash) {
    StaticMutexAutoUnlock unlock(sMutex);
    nsrefcnt count = trash.forget().take()->Release();
    MOZ_RELEASE_ASSERT(!count,
                       "ShutdownLibrary should be releasing the last reference "
                       "to the cubeb ctx!");
  }

#ifdef MOZ_CUBEB_REMOTING
  sIPCConnection = nullptr;
  ShutdownAudioIPCServer();
#endif
}

bool SandboxEnabled() {
#ifdef MOZ_CUBEB_REMOTING
  StaticMutexAutoLock lock(sMutex);
  return !!sCubebSandbox;
#else
  return false;
#endif
}

already_AddRefed<SharedThreadPool> GetCubebOperationThread() {
  RefPtr<SharedThreadPool> pool = SharedThreadPool::Get("CubebOperation", 1);
  const uint32_t kIdleThreadTimeoutMs = 2000;
  pool->SetIdleThreadMaximumTimeout(
      PR_MillisecondsToInterval(kIdleThreadTimeoutMs));
  return pool.forget();
}

uint32_t MaxNumberOfChannels() {
  RefPtr<CubebHandle> handle = GetCubeb();
  uint32_t maxNumberOfChannels;
  if (handle && cubeb_get_max_channel_count(handle->Context(),
                                            &maxNumberOfChannels) == CUBEB_OK) {
    return maxNumberOfChannels;
  }

  return 0;
}

void GetCurrentBackend(nsAString& aBackend) {
  RefPtr<CubebHandle> handle = GetCubeb();
  if (handle) {
    const char* backend = cubeb_get_backend_id(handle->Context());
    if (backend) {
      aBackend.AssignASCII(backend);
      return;
    }
  }
  aBackend.AssignLiteral("unknown");
}

char* GetForcedOutputDevice() {
  StaticMutexAutoLock lock(sMutex);
  return sCubebOutputDeviceName;
}

cubeb_stream_prefs GetDefaultStreamPrefs(cubeb_device_type aType) {
  cubeb_stream_prefs prefs = CUBEB_STREAM_PREF_NONE;
#ifdef XP_WIN
  if (StaticPrefs::media_cubeb_wasapi_raw() & static_cast<uint32_t>(aType)) {
    prefs |= CUBEB_STREAM_PREF_RAW;
  }
#endif
  return prefs;
}

bool RouteOutputAsVoice() { return sRouteOutputAsVoice; }

long datacb(cubeb_stream*, void*, const void*, void* out_buffer, long nframes) {
  PodZero(static_cast<float*>(out_buffer), nframes * 2);
  return nframes;
}

void statecb(cubeb_stream*, void*, cubeb_state) {}

bool EstimatedLatencyDefaultDevices(double* aMean, double* aStdDev,
                                    Side aSide) {
  RefPtr<CubebHandle> handle = GetCubeb();
  if (!handle) {
    MOZ_LOG(gCubebLog, LogLevel::Error, ("No cubeb context, bailing."));
    return false;
  }
  bool includeInput = aSide & Side::Input;
  bool includeOutput = aSide & Side::Output;
  nsTArray<double> latencies;
  // Create a cubeb stream with the correct latency and default input/output
  // devices (mono/stereo channels). Wait for two seconds, get the latency a few
  // times.
  int rv;
  uint32_t rate;
  uint32_t latencyFrames;
  rv = cubeb_get_preferred_sample_rate(handle->Context(), &rate);
  if (rv != CUBEB_OK) {
    MOZ_LOG(gCubebLog, LogLevel::Error, ("Could not get preferred rate"));
    return false;
  }

  cubeb_stream_params output_params;
  output_params.format = CUBEB_SAMPLE_FLOAT32NE;
  output_params.rate = rate;
  output_params.channels = 2;
  output_params.layout = CUBEB_LAYOUT_UNDEFINED;
  output_params.prefs = GetDefaultStreamPrefs(CUBEB_DEVICE_TYPE_OUTPUT);
  output_params.input_params = CUBEB_INPUT_PROCESSING_PARAM_NONE;

  latencyFrames = GetCubebMTGLatencyInFrames(&output_params);

  cubeb_stream_params input_params;
  input_params.format = CUBEB_SAMPLE_FLOAT32NE;
  input_params.rate = rate;
  input_params.channels = 1;
  input_params.layout = CUBEB_LAYOUT_UNDEFINED;
  input_params.prefs = GetDefaultStreamPrefs(CUBEB_DEVICE_TYPE_INPUT);
  input_params.input_params = CUBEB_INPUT_PROCESSING_PARAM_NONE;

  cubeb_stream* stm;
  rv = cubeb_stream_init(handle->Context(), &stm,
                         "about:support latency estimation", NULL,
                         &input_params, NULL, &output_params, latencyFrames,
                         datacb, statecb, NULL);
  if (rv != CUBEB_OK) {
    MOZ_LOG(gCubebLog, LogLevel::Error, ("Could not get init stream"));
    return false;
  }

  rv = cubeb_stream_start(stm);
  if (rv != CUBEB_OK) {
    MOZ_LOG(gCubebLog, LogLevel::Error, ("Could not start stream"));
    return false;
  }
  // +-2s
  for (uint32_t i = 0; i < 40; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    uint32_t inputLatency, outputLatency, rvIn, rvOut;
    rvOut = cubeb_stream_get_latency(stm, &outputLatency);
    if (rvOut) {
      MOZ_LOG(gCubebLog, LogLevel::Error, ("Could not get output latency"));
    }
    rvIn = cubeb_stream_get_input_latency(stm, &inputLatency);
    if (rvIn) {
      MOZ_LOG(gCubebLog, LogLevel::Error, ("Could not get input latency"));
    }
    if (rvIn != CUBEB_OK || rvOut != CUBEB_OK) {
      continue;
    }

    double latency = static_cast<double>((includeInput ? inputLatency : 0) +
                                         (includeOutput ? outputLatency : 0)) /
                     rate;
    latencies.AppendElement(latency);
  }
  rv = cubeb_stream_stop(stm);
  if (rv != CUBEB_OK) {
    MOZ_LOG(gCubebLog, LogLevel::Error, ("Could not stop the stream"));
  }

  *aMean = 0.0;
  *aStdDev = 0.0;
  double variance = 0.0;
  for (uint32_t i = 0; i < latencies.Length(); i++) {
    *aMean += latencies[i];
  }

  *aMean /= latencies.Length();

  for (uint32_t i = 0; i < latencies.Length(); i++) {
    variance += pow(latencies[i] - *aMean, 2.);
  }
  variance /= latencies.Length();

  *aStdDev = sqrt(variance);

  MOZ_LOG(gCubebLog, LogLevel::Debug,
          ("Default devices latency in seconds %lf (stddev: %lf)", *aMean,
           *aStdDev));

  cubeb_stream_destroy(stm);

  return true;
}

#ifdef MOZ_WIDGET_ANDROID
int32_t AndroidGetAudioOutputSampleRate() {
  if (java::GeckoAppShell::IsIsolatedProcess()) {
    return 44100;  // TODO: Remote value; will be handled in following patch.
  }

  int32_t sample_rate = java::GeckoAppShell::GetAudioOutputSampleRate();
  return sample_rate;
}
int32_t AndroidGetAudioOutputFramesPerBuffer() {
  if (java::GeckoAppShell::IsIsolatedProcess()) {
    return 512;  // TODO: Remote value; will be handled in following patch.
  }
  int32_t frames = java::GeckoAppShell::GetAudioOutputFramesPerBuffer();
  return frames;
}
#endif

}  // namespace CubebUtils
}  // namespace mozilla

#undef ENABLE_MOCK_CUBEB
