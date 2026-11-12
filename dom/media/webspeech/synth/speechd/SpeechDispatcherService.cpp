/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SpeechDispatcherService.h"

#include <math.h>

#include "mozilla/ClearOnShutdown.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/dom/nsSpeechTask.h"
#include "mozilla/dom/nsSynthVoiceRegistry.h"
#include "nsEscape.h"
#include "nsISupports.h"
#include "nsPrintfCString.h"
#include "nsReadableUtils.h"
#include "nsServiceManagerUtils.h"
#include "nsThreadUtils.h"
#include "nsXULAppAPI.h"
#include "prlink.h"

#define URI_PREFIX "urn:moz-tts:speechd:"

#define MAX_RATE static_cast<float>(2.5)
#define MIN_RATE static_cast<float>(0.5)

// Some structures for libspeechd
typedef enum {
  SPD_EVENT_BEGIN,
  SPD_EVENT_END,
  SPD_EVENT_INDEX_MARK,
  SPD_EVENT_CANCEL,
  SPD_EVENT_PAUSE,
  SPD_EVENT_RESUME
} SPDNotificationType;

typedef enum {
  SPD_BEGIN = 1,
  SPD_END = 2,
  SPD_INDEX_MARKS = 4,
  SPD_CANCEL = 8,
  SPD_PAUSE = 16,
  SPD_RESUME = 32,

  SPD_ALL = 0x3f
} SPDNotification;

typedef enum { SPD_MODE_SINGLE = 0, SPD_MODE_THREADED = 1 } SPDConnectionMode;

typedef void (*SPDCallback)(size_t msg_id, size_t client_id,
                            SPDNotificationType state);

typedef void (*SPDCallbackIM)(size_t msg_id, size_t client_id,
                              SPDNotificationType state, char* index_mark);

struct SPDConnection {
  SPDCallback callback_begin;
  SPDCallback callback_end;
  SPDCallback callback_cancel;
  SPDCallback callback_pause;
  SPDCallback callback_resume;
  SPDCallbackIM callback_im;

  /* partial, more private fields in structure */
};

struct SPDVoice {
  char* name;
  char* language;
  char* variant;
};

typedef enum {
  SPD_IMPORTANT = 1,
  SPD_MESSAGE = 2,
  SPD_TEXT = 3,
  SPD_NOTIFICATION = 4,
  SPD_PROGRESS = 5
} SPDPriority;

#define SPEECHD_FUNCTIONS                                           \
  FUNC(spd_open, SPDConnection*,                                    \
       (const char*, const char*, const char*, SPDConnectionMode))  \
  FUNC(spd_close, void, (SPDConnection*))                           \
  FUNC(spd_list_synthesis_voices, SPDVoice**, (SPDConnection*))     \
  FUNC(spd_say, int, (SPDConnection*, SPDPriority, const char*))    \
  FUNC(spd_cancel, int, (SPDConnection*))                           \
  FUNC(spd_set_volume, int, (SPDConnection*, int))                  \
  FUNC(spd_set_voice_rate, int, (SPDConnection*, int))              \
  FUNC(spd_set_voice_pitch, int, (SPDConnection*, int))             \
  FUNC(spd_set_synthesis_voice, int, (SPDConnection*, const char*)) \
  FUNC(spd_set_notification_on, int, (SPDConnection*, SPDNotification))

#define FUNC(name, type, params)      \
  typedef type(*_##name##_fn) params; \
  static _##name##_fn _##name;

SPEECHD_FUNCTIONS

#undef FUNC

#define spd_open _spd_open
#define spd_close _spd_close
#define spd_list_synthesis_voices _spd_list_synthesis_voices
#define spd_say _spd_say
#define spd_cancel _spd_cancel
#define spd_set_volume _spd_set_volume
#define spd_set_voice_rate _spd_set_voice_rate
#define spd_set_voice_pitch _spd_set_voice_pitch
#define spd_set_synthesis_voice _spd_set_synthesis_voice
#define spd_set_notification_on _spd_set_notification_on

static PRLibrary* speechdLib = nullptr;

typedef void (*nsSpeechDispatcherFunc)();
struct nsSpeechDispatcherDynamicFunction {
  const char* functionName;
  nsSpeechDispatcherFunc* function;
};

namespace mozilla::dom {

StaticRefPtr<SpeechDispatcherService> SpeechDispatcherService::sSingleton;

class SpeechDispatcherVoice {
 public:
  SpeechDispatcherVoice(const nsAString& aName, const nsAString& aLanguage)
      : mName(aName), mLanguage(aLanguage) {}

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(SpeechDispatcherVoice)

  // Voice name
  nsString mName;

  // Voice language, in BCP-47 syntax
  nsString mLanguage;

 private:
  ~SpeechDispatcherVoice() = default;
};

class SpeechDispatcherCallback final : public nsISpeechTaskCallback {
 public:
  SpeechDispatcherCallback(nsISpeechTask* aTask,
                           SpeechDispatcherService* aService)
      : mTask(aTask), mService(aService) {}

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS_FINAL
  NS_DECL_CYCLE_COLLECTION_CLASS_AMBIGUOUS(SpeechDispatcherCallback,
                                           nsISpeechTaskCallback)

  NS_DECL_NSISPEECHTASKCALLBACK

  bool OnSpeechEvent(SPDNotificationType state);

 private:
  ~SpeechDispatcherCallback() = default;

  // This pointer is used to dispatch events
  nsCOMPtr<nsISpeechTask> mTask;

  // By holding a strong reference to the service we guarantee that it won't be
  // destroyed before this runnable.
  RefPtr<SpeechDispatcherService> mService;

  TimeStamp mStartTime;
};

NS_IMPL_CYCLE_COLLECTION(SpeechDispatcherCallback, mTask);

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(SpeechDispatcherCallback)
  NS_INTERFACE_MAP_ENTRY(nsISpeechTaskCallback)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsISpeechTaskCallback)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(SpeechDispatcherCallback)
NS_IMPL_CYCLE_COLLECTING_RELEASE(SpeechDispatcherCallback)

NS_IMETHODIMP
SpeechDispatcherCallback::OnPause() {
  // XXX: Speech dispatcher does not pause immediately, but waits for the speech
  // to reach an index mark so that it could resume from that offset.
  // There is no support for word or sentence boundaries, so index marks would
  // only occur in explicit SSML marks, and we don't support that yet.
  // What in actuality happens, is that if you call spd_pause(), it will speak
  // the utterance in its entirety, dispatch an end event, and then put speechd
  // in a 'paused' state. Since it is after the utterance ended, we don't get
  // that state change, and our speech api is in an unrecoverable state.
  // So, since it is useless anyway, I am not implementing pause.
  return NS_OK;
}

NS_IMETHODIMP
SpeechDispatcherCallback::OnResume() {
  // XXX: Unsupported, see OnPause().
  return NS_OK;
}

NS_IMETHODIMP
SpeechDispatcherCallback::OnCancel() {
  if (spd_cancel(mService->mSpeechdClient) < 0) {
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

NS_IMETHODIMP
SpeechDispatcherCallback::OnVolumeChanged(float aVolume) {
  // XXX: This currently does not change the volume mid-utterance, but it
  // doesn't do anything bad either. So we could put this here with the hopes
  // that speechd supports this in the future.
  if (spd_set_volume(mService->mSpeechdClient,
                     static_cast<int>(aVolume * 100)) < 0) {
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

// CANCEL/END/PAUSE/RESUME can arrive before BEGIN (e.g. speak()+cancel()
// race with speech-dispatcher). mStartTime stays null in that case.
static double ElapsedSeconds(const TimeStamp& aStart) {
  return aStart.IsNull() ? 0.0 : (TimeStamp::Now() - aStart).ToSeconds();
}

bool SpeechDispatcherCallback::OnSpeechEvent(SPDNotificationType state) {
  bool remove = false;

  switch (state) {
    case SPD_EVENT_BEGIN:
      mStartTime = TimeStamp::Now();
      mTask->DispatchStart();
      break;

    case SPD_EVENT_PAUSE:
      mTask->DispatchPause(ElapsedSeconds(mStartTime), 0);
      break;

    case SPD_EVENT_RESUME:
      mTask->DispatchResume(ElapsedSeconds(mStartTime), 0);
      break;

    case SPD_EVENT_CANCEL:
    case SPD_EVENT_END:
      mTask->DispatchEnd(ElapsedSeconds(mStartTime), 0);
      remove = true;
      break;

    case SPD_EVENT_INDEX_MARK:
      // Not yet supported
      break;

    default:
      break;
  }

  return remove;
}

static void speechd_cb(size_t msg_id, size_t client_id,
                       SPDNotificationType state) {
  SpeechDispatcherService* service =
      SpeechDispatcherService::GetInstance(false);

  if (service) {
    NS_DispatchToMainThread(NewRunnableMethod<uint32_t, SPDNotificationType>(
        "dom::SpeechDispatcherService::EventNotify", service,
        &SpeechDispatcherService::EventNotify, static_cast<uint32_t>(msg_id),
        state));
  }
}

NS_INTERFACE_MAP_BEGIN(SpeechDispatcherService)
  NS_INTERFACE_MAP_ENTRY(nsISpeechService)
  NS_INTERFACE_MAP_ENTRY(nsIObserver)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIObserver)
NS_INTERFACE_MAP_END

NS_IMPL_ADDREF(SpeechDispatcherService)
NS_IMPL_RELEASE(SpeechDispatcherService)

SpeechDispatcherService::SpeechDispatcherService()
    : mInitialized(false), mSpeechdClient(nullptr) {}

void SpeechDispatcherService::Init() {
  if (!StaticPrefs::media_webspeech_synth_enabled() ||
      Preferences::GetBool("media.webspeech.synth.test")) {
    return;
  }

  // While speech dispatcher has a "threaded" mode, only spd_say() is async.
  // Since synchronous socket i/o could impact startup time, we do
  // initialization in a separate thread.
  DebugOnly<nsresult> rv =
      NS_NewNamedThread("speechd init", getter_AddRefs(mInitThread));
  MOZ_ASSERT(NS_SUCCEEDED(rv));
  rv = mInitThread->Dispatch(
      NewRunnableMethod("dom::SpeechDispatcherService::Setup", this,
                        &SpeechDispatcherService::Setup),
      NS_DISPATCH_NORMAL);
  MOZ_ASSERT(NS_SUCCEEDED(rv));
}

SpeechDispatcherService::~SpeechDispatcherService() {
  if (mInitThread) {
    mInitThread->Shutdown();
  }

  if (mSpeechdClient) {
    spd_close(mSpeechdClient);
  }
}

void SpeechDispatcherService::Setup() {
#define FUNC(name, type, params) {#name, (nsSpeechDispatcherFunc*)&_##name},
  static const nsSpeechDispatcherDynamicFunction kSpeechDispatcherSymbols[] = {
      SPEECHD_FUNCTIONS};
#undef FUNC

  MOZ_ASSERT(!mInitialized);

  speechdLib = PR_LoadLibrary("libspeechd.so.2");

  if (!speechdLib) {
    NS_WARNING("Failed to load speechd library");
    NotifyError(u"lib-missing"_ns);
    return;
  }

  if (!PR_FindFunctionSymbol(speechdLib, "spd_get_volume")) {
    // There is no version getter function, so we rely on a symbol that was
    // introduced in release 0.8.2 in order to check for ABI compatibility.
    NS_WARNING("Unsupported version of speechd detected");
    NotifyError(u"lib-too-old"_ns);
    return;
  }

  for (auto speechDispatcherSymbol : kSpeechDispatcherSymbols) {
    *speechDispatcherSymbol.function =
        PR_FindFunctionSymbol(speechdLib, speechDispatcherSymbol.functionName);

    if (!*speechDispatcherSymbol.function) {
      NS_WARNING(nsPrintfCString("Failed to find speechd symbol for'%s'",
                                 speechDispatcherSymbol.functionName)
                     .get());
      NotifyError(u"missing-symbol"_ns);
      return;
    }
  }

  mSpeechdClient =
      spd_open("firefox", "web speech api", "who", SPD_MODE_THREADED);
  if (!mSpeechdClient) {
    NS_WARNING("Failed to call spd_open");
    NotifyError(u"open-fail"_ns);
    return;
  }

  // Get all the voices from sapi and register in the SynthVoiceRegistry
  SPDVoice** list = spd_list_synthesis_voices(mSpeechdClient);

  mSpeechdClient->callback_begin = speechd_cb;
  mSpeechdClient->callback_end = speechd_cb;
  mSpeechdClient->callback_cancel = speechd_cb;
  mSpeechdClient->callback_pause = speechd_cb;
  mSpeechdClient->callback_resume = speechd_cb;

  spd_set_notification_on(mSpeechdClient, SPD_BEGIN);
  spd_set_notification_on(mSpeechdClient, SPD_END);
  spd_set_notification_on(mSpeechdClient, SPD_CANCEL);

  if (list != nullptr) {
    for (int i = 0; list[i]; i++) {
      nsAutoString uri;

      uri.AssignLiteral(URI_PREFIX);
      nsAutoCString name;
      NS_EscapeURL(list[i]->name, -1,
                   esc_OnlyNonASCII | esc_Spaces | esc_AlwaysCopy, name);
      uri.Append(NS_ConvertUTF8toUTF16(name));

      uri.AppendLiteral("?");

      nsAutoCString lang(list[i]->language);

      uri.Append(NS_ConvertUTF8toUTF16(lang));

      mVoices.InsertOrUpdate(uri, MakeRefPtr<SpeechDispatcherVoice>(
                                      NS_ConvertUTF8toUTF16(list[i]->name),
                                      NS_ConvertUTF8toUTF16(lang)));
    }
  }

  if (mVoices.Count() == 0) {
    NotifyError(u"no-voices"_ns);
  }

  NS_DispatchToMainThread(
      NewRunnableMethod("dom::SpeechDispatcherService::RegisterVoices", this,
                        &SpeechDispatcherService::RegisterVoices));

  // mInitialized = true;
}

// private methods

void SpeechDispatcherService::NotifyError(const nsString& aError) {
  if (!NS_IsMainThread()) {
    NS_DispatchToMainThread(NewRunnableMethod<const nsString>(
        "dom::SpeechDispatcherService::NotifyError", this,
        &SpeechDispatcherService::NotifyError, aError));
    return;
  }

  RefPtr<nsSynthVoiceRegistry> registry = nsSynthVoiceRegistry::GetInstance();
  DebugOnly<nsresult> rv = registry->NotifyVoicesError(aError);
}

void SpeechDispatcherService::RegisterVoices() {
  RefPtr<nsSynthVoiceRegistry> registry = nsSynthVoiceRegistry::GetInstance();
  for (const auto& entry : mVoices) {
    const RefPtr<SpeechDispatcherVoice>& voice = entry.GetData();

    // This service can only speak one utterance at a time, so we set
    // aQueuesUtterances to true in order to track global state and schedule
    // access to this service.
    DebugOnly<nsresult> rv =
        registry->AddVoice(this, entry.GetKey(), voice->mName, voice->mLanguage,
                           voice->mName.EqualsLiteral("default"), true);

    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "Failed to add voice");
  }

  mInitThread->Shutdown();
  mInitThread = nullptr;

  mInitialized = true;

  registry->NotifyVoicesChanged();
}

// nsIObserver

NS_IMETHODIMP
SpeechDispatcherService::Observe(nsISupports* aSubject, const char* aTopic,
                                 const char16_t* aData) {
  return NS_OK;
}

// nsISpeechService

// TODO: Support SSML
NS_IMETHODIMP
SpeechDispatcherService::Speak(const nsAString& aText, const nsAString& aUri,
                               float aVolume, float aRate, float aPitch,
                               nsISpeechTask* aTask) {
  if (NS_WARN_IF(!mInitialized)) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  RefPtr<SpeechDispatcherCallback> callback =
      new SpeechDispatcherCallback(aTask, this);

  bool found = false;
  SpeechDispatcherVoice* voice = mVoices.GetWeak(aUri, &found);

  if (NS_WARN_IF(!(found))) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  spd_set_synthesis_voice(mSpeechdClient,
                          NS_ConvertUTF16toUTF8(voice->mName).get());

  // We provide a volume of 0.0 to 1.0, speech-dispatcher expects 0 - 100.
  spd_set_volume(mSpeechdClient, static_cast<int>(aVolume * 100));

  // aRate is a value of 0.1 (0.1x) to 10 (10x) with 1 (1x) being normal rate.
  // speechd expects -100 to 100 with 0 being normal rate.
  float rate = 0;
  if (aRate > 1) {
    // Each step to 100 is logarithmically distributed up to 2.5x.
    rate = log10(std::min(aRate, MAX_RATE)) / log10(MAX_RATE) * 100;
  } else if (aRate < 1) {
    // Each step to -100 is logarithmically distributed down to 0.5x.
    rate = log10(std::max(aRate, MIN_RATE)) / log10(MIN_RATE) * -100;
  }

  spd_set_voice_rate(mSpeechdClient, static_cast<int>(rate));

  // We provide a pitch of 0 to 2 with 1 being the default.
  // speech-dispatcher expects -100 to 100 with 0 being default.
  spd_set_voice_pitch(mSpeechdClient, static_cast<int>((aPitch - 1) * 100));

  nsresult rv = aTask->Setup(callback);

  if (NS_FAILED(rv)) {
    return rv;
  }

  if (aText.Length()) {
    int msg_id = spd_say(mSpeechdClient, SPD_MESSAGE,
                         NS_ConvertUTF16toUTF8(aText).get());

    if (msg_id < 0) {
      return NS_ERROR_FAILURE;
    }

    mCallbacks.InsertOrUpdate(msg_id, std::move(callback));
  } else {
    // Speech dispatcher does not work well with empty strings.
    // In that case, don't send empty string to speechd,
    // and just emulate a speechd start and end event.
    NS_DispatchToMainThread(NewRunnableMethod<SPDNotificationType>(
        "dom::SpeechDispatcherCallback::OnSpeechEvent", callback,
        &SpeechDispatcherCallback::OnSpeechEvent, SPD_EVENT_BEGIN));

    NS_DispatchToMainThread(NewRunnableMethod<SPDNotificationType>(
        "dom::SpeechDispatcherCallback::OnSpeechEvent", callback,
        &SpeechDispatcherCallback::OnSpeechEvent, SPD_EVENT_END));
  }

  return NS_OK;
}

SpeechDispatcherService* SpeechDispatcherService::GetInstance(bool create) {
  if (XRE_GetProcessType() != GeckoProcessType_Default) {
    MOZ_ASSERT(
        false,
        "SpeechDispatcherService can only be started on main gecko process");
    return nullptr;
  }

  if (!sSingleton && create) {
    sSingleton = new SpeechDispatcherService();
    sSingleton->Init();
    ClearOnShutdown(&sSingleton);
  }

  return sSingleton;
}

already_AddRefed<SpeechDispatcherService>
SpeechDispatcherService::GetInstanceForService() {
  MOZ_ASSERT(NS_IsMainThread());
  RefPtr<SpeechDispatcherService> sapiService = GetInstance();
  return sapiService.forget();
}

void SpeechDispatcherService::EventNotify(uint32_t aMsgId, uint32_t aState) {
  SpeechDispatcherCallback* callback = mCallbacks.GetWeak(aMsgId);

  if (callback) {
    if (callback->OnSpeechEvent((SPDNotificationType)aState)) {
      mCallbacks.Remove(aMsgId);
    }
  }
}

}  // namespace mozilla::dom
