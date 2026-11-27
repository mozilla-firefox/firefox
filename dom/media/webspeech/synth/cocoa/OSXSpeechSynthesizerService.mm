/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "OSXSpeechSynthesizerService.h"
#include "mozilla/Assertions.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/DataMutex.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/dom/nsSpeechTask.h"
#include "mozilla/dom/nsSynthVoiceRegistry.h"
#include "nsCocoaUtils.h"
#include "nsISupports.h"
#include "nsIThread.h"
#include "nsObjCExceptions.h"
#include "nsServiceManagerUtils.h"
#include "nsThreadUtils.h"
#include "nsXULAppAPI.h"

#import <AVFoundation/AVFoundation.h>
#import <Cocoa/Cocoa.h>

@class SpeechDelegate;

// We can escape the default delimiters ("[[" and "]]") by temporarily
// changing the delimiters just before they appear, and changing them back
// just after.
#define DLIM_ESCAPE_START "[[dlim (( ))]]"
#define DLIM_ESCAPE_END "((dlim [[ ]]))"

using namespace mozilla;

class SpeechTaskCallback;

class SpeechTaskCallbackBridge final {
 public:
  explicit SpeechTaskCallbackBridge(SpeechTaskCallback* aCallback)
      : mCallback(aCallback, "SpeechTaskCallbackBridge::mCallback") {}

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(SpeechTaskCallbackBridge)

  DataMutex<SpeechTaskCallback*> mCallback;

 private:
  ~SpeechTaskCallbackBridge() = default;
};

class SpeechTaskCallback final : public nsISpeechTaskCallback {
 public:
  SpeechTaskCallback(nsISpeechTask* aTask, AVSpeechSynthesizer* aSynth,
                     const nsTArray<size_t>& aOffsets);

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS_FINAL
  NS_DECL_CYCLE_COLLECTION_CLASS_AMBIGUOUS(SpeechTaskCallback,
                                           nsISpeechTaskCallback)

  NS_DECL_NSISPEECHTASKCALLBACK

  void OnWillSpeakWord(uint32_t aIndex, uint32_t aLength);
  void OnError(uint32_t aIndex);
  void OnDidFinishSpeaking();

 private:
  virtual ~SpeechTaskCallback();

  float GetTimeDurationFromStart();

  nsCOMPtr<nsISpeechTask> mTask;
  AVSpeechSynthesizer* mSpeechSynthesizer;
  RefPtr<SpeechTaskCallbackBridge> mBridge;
  SpeechDelegate* mDelegate;
  TimeStamp mStartingTime;
  uint32_t mCurrentIndex;
  nsTArray<size_t> mOffsets;
};

@interface SpeechDelegate : NSObject <AVSpeechSynthesizerDelegate> {
 @private
  RefPtr<SpeechTaskCallbackBridge> mBridge;
}

- (id)initWithBridge:(SpeechTaskCallbackBridge*)aBridge;
@end

@implementation SpeechDelegate
- (id)initWithBridge:(SpeechTaskCallbackBridge*)aBridge {
  self = [super init];
  if (self) {
    mBridge = aBridge;
  }
  return self;
}

- (void)speechSynthesizer:(AVSpeechSynthesizer*)aSender
    willSpeakRangeOfSpeechString:(NSRange)aRange
                       utterance:(AVSpeechUtterance*)aUtterance {
  RefPtr<SpeechTaskCallbackBridge> bridge = mBridge;
  uint32_t location = static_cast<uint32_t>(aRange.location);
  uint32_t length = static_cast<uint32_t>(aRange.length);
  NS_DispatchToMainThread(NS_NewRunnableFunction(
      "SpeechDelegate::willSpeakRangeOfSpeechString",
      [bridge = std::move(bridge), location, length]() {
        MOZ_ASSERT(NS_IsMainThread());
        RefPtr<SpeechTaskCallback> callback;
        {
          auto locked = bridge->mCallback.Lock();
          callback = *locked;
        }
        if (callback) {
          callback->OnWillSpeakWord(location, length);
        }
      }));
}

- (void)speechSynthesizer:(AVSpeechSynthesizer*)aSender
    didFinishSpeechUtterance:(AVSpeechUtterance*)aUtterance {
  RefPtr<SpeechTaskCallbackBridge> bridge = mBridge;
  NS_DispatchToMainThread(NS_NewRunnableFunction(
      "SpeechDelegate::didFinishSpeechUtterance",
      [bridge = std::move(bridge)]() {
        MOZ_ASSERT(NS_IsMainThread());
        RefPtr<SpeechTaskCallback> callback;
        {
          auto locked = bridge->mCallback.Lock();
          callback = *locked;
        }
        if (callback) {
          callback->OnDidFinishSpeaking();
        }
      }));
}

- (void)speechSynthesizer:(AVSpeechSynthesizer*)aSender
    didCancelSpeechUtterance:(AVSpeechUtterance*)aUtterance {
  [self speechSynthesizer:aSender didFinishSpeechUtterance:aUtterance];
}

@end

NS_IMPL_CYCLE_COLLECTION(SpeechTaskCallback, mTask);

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(SpeechTaskCallback)
  NS_INTERFACE_MAP_ENTRY(nsISpeechTaskCallback)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsISpeechTaskCallback)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(SpeechTaskCallback)
NS_IMPL_CYCLE_COLLECTING_RELEASE(SpeechTaskCallback)

SpeechTaskCallback::SpeechTaskCallback(nsISpeechTask* aTask,
                                       AVSpeechSynthesizer* aSynth,
                                       const nsTArray<size_t>& aOffsets)
    : mTask(aTask),
      mSpeechSynthesizer(aSynth),
      mBridge(MakeRefPtr<SpeechTaskCallbackBridge>(this)),
      mCurrentIndex(0),
      mOffsets(aOffsets.Clone()) {
  MOZ_ASSERT(NS_IsMainThread());
  mDelegate = [[SpeechDelegate alloc] initWithBridge:mBridge];
  [mSpeechSynthesizer setDelegate:mDelegate];
  mStartingTime = TimeStamp::Now();
}

SpeechTaskCallback::~SpeechTaskCallback() {
  MOZ_ASSERT(NS_IsMainThread());
  if (mBridge) {
    auto locked = mBridge->mCallback.Lock();
    *locked = nullptr;
  }
  [mSpeechSynthesizer setDelegate:nil];
  [mSpeechSynthesizer stopSpeakingAtBoundary:AVSpeechBoundaryImmediate];
  [mDelegate release];
  [mSpeechSynthesizer release];
}

NS_IMETHODIMP
SpeechTaskCallback::OnCancel() {
  MOZ_ASSERT(NS_IsMainThread());
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  [mSpeechSynthesizer stopSpeakingAtBoundary:AVSpeechBoundaryImmediate];
  return NS_OK;

  NS_OBJC_END_TRY_BLOCK_RETURN(NS_ERROR_FAILURE);
}

NS_IMETHODIMP
SpeechTaskCallback::OnPause() {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  [mSpeechSynthesizer pauseSpeakingAtBoundary:AVSpeechBoundaryImmediate];
  if (!mTask) {
    // When calling pause() on child process, it may not receive end event
    // from chrome process yet.
    return NS_ERROR_FAILURE;
  }
  mTask->DispatchPause(GetTimeDurationFromStart(), mCurrentIndex);
  return NS_OK;

  NS_OBJC_END_TRY_BLOCK_RETURN(NS_ERROR_FAILURE);
}

NS_IMETHODIMP
SpeechTaskCallback::OnResume() {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  [mSpeechSynthesizer continueSpeaking];
  if (!mTask) {
    // When calling resume() on child process, it may not receive end event
    // from chrome process yet.
    return NS_ERROR_FAILURE;
  }
  mTask->DispatchResume(GetTimeDurationFromStart(), mCurrentIndex);
  return NS_OK;

  NS_OBJC_END_TRY_BLOCK_RETURN(NS_ERROR_FAILURE);
}

NS_IMETHODIMP
SpeechTaskCallback::OnVolumeChanged(float aVolume) {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  // XXX: You cannot change the volume mid-utterance.

  return NS_OK;

  NS_OBJC_END_TRY_BLOCK_RETURN(NS_ERROR_FAILURE);
}

float SpeechTaskCallback::GetTimeDurationFromStart() {
  TimeDuration duration = TimeStamp::Now() - mStartingTime;
  return duration.ToSeconds();
}

void SpeechTaskCallback::OnWillSpeakWord(uint32_t aIndex, uint32_t aLength) {
  MOZ_ASSERT(NS_IsMainThread());
  RefPtr<SpeechTaskCallback> self(this);
  mCurrentIndex = aIndex < mOffsets.Length() ? mOffsets[aIndex] : mCurrentIndex;
  nsCOMPtr<nsISpeechTask> task = mTask;
  if (!task) {
    return;
  }
  task->DispatchBoundary(u"word"_ns, GetTimeDurationFromStart(), mCurrentIndex,
                         aLength, 1);
}

void SpeechTaskCallback::OnError(uint32_t aIndex) {
  MOZ_ASSERT(NS_IsMainThread());
  RefPtr<SpeechTaskCallback> self(this);
  nsCOMPtr<nsISpeechTask> task = std::move(mTask);
  if (!task) {
    return;
  }
  [mSpeechSynthesizer setDelegate:nil];
  task->DispatchError(GetTimeDurationFromStart(), aIndex);
}

void SpeechTaskCallback::OnDidFinishSpeaking() {
  MOZ_ASSERT(NS_IsMainThread());
  // Hold a strong reference across DispatchEnd(), which clears
  // nsSpeechTask::mCallback and may otherwise destroy |this| synchronously.
  RefPtr<SpeechTaskCallback> self(this);
  [mSpeechSynthesizer setDelegate:nil];
  nsCOMPtr<nsISpeechTask> task = std::move(mTask);
  if (!task) {
    return;
  }
  task->DispatchEnd(GetTimeDurationFromStart(), mCurrentIndex);
}

namespace mozilla {
namespace dom {

struct OSXVoice {
  OSXVoice() : mIsDefault(false) {}

  nsString mUri;
  nsString mName;
  nsString mLocale;
  bool mIsDefault;
};

class RegisterVoicesRunnable final : public Runnable {
 public:
  RegisterVoicesRunnable(OSXSpeechSynthesizerService* aSpeechService,
                         nsTArray<OSXVoice>& aList)
      : Runnable("RegisterVoicesRunnable"),
        mSpeechService(aSpeechService),
        mVoices(aList) {}

  NS_IMETHOD Run() override;

 private:
  ~RegisterVoicesRunnable() override = default;

  // This runnable always use sync mode.  It is unnecesarry to reference object
  OSXSpeechSynthesizerService* mSpeechService;
  nsTArray<OSXVoice>& mVoices;
};

NS_IMETHODIMP
RegisterVoicesRunnable::Run() {
  nsresult rv;
  nsCOMPtr<nsISynthVoiceRegistry> registry =
      do_GetService(NS_SYNTHVOICEREGISTRY_CONTRACTID, &rv);
  if (!registry) {
    return rv;
  }

  for (const OSXVoice& voice : mVoices) {
    rv = registry->AddVoice(mSpeechService, voice.mUri, voice.mName,
                            voice.mLocale, true, false);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      continue;
    }

    if (voice.mIsDefault) {
      registry->SetDefaultVoice(voice.mUri, true);
    }
  }

  registry->NotifyVoicesChanged();

  return NS_OK;
}

class EnumVoicesRunnable final : public Runnable {
 public:
  explicit EnumVoicesRunnable(OSXSpeechSynthesizerService* aSpeechService)
      : Runnable("EnumVoicesRunnable"), mSpeechService(aSpeechService) {}

  NS_IMETHOD Run() override;

 private:
  ~EnumVoicesRunnable() override = default;

  RefPtr<OSXSpeechSynthesizerService> mSpeechService;
};

NS_IMETHODIMP
EnumVoicesRunnable::Run() {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  AutoTArray<OSXVoice, 64> list;

  NSArray* voices = [[AVSpeechSynthesisVoice speechVoices]
      filteredArrayUsingPredicate:[NSPredicate predicateWithBlock:^BOOL(
                                                   id voice,
                                                   NSDictionary* bindings) {
        if (@available(macOS 14.0, *)) {
          // Filter out novelty voices
          if ([voice voiceTraits] ==
              AVSpeechSynthesisVoiceTraitIsNoveltyVoice) {
            return NO;
          }
        }

        // Filter out eloquence voices since they don't seem to work.
        return ![[voice identifier] containsString:@"com.apple.eloquence"];
      }]];
  AVSpeechSynthesisVoice* defualtVoice = [AVSpeechSynthesisVoice
      voiceWithLanguage:[AVSpeechSynthesisVoice currentLanguageCode]];

  for (AVSpeechSynthesisVoice* voice in voices) {
    OSXVoice item;

    nsAutoString identifier;
    nsCocoaUtils::GetStringForNSString([voice identifier], identifier);

    nsCocoaUtils::GetStringForNSString([voice name], item.mName);

    nsCocoaUtils::GetStringForNSString([voice language], item.mLocale);
    item.mLocale.ReplaceChar('_', '-');

    item.mUri.AssignLiteral("urn:moz-tts:osx:");
    item.mUri.Append(identifier);

    if ([[voice identifier] isEqualToString:[defualtVoice identifier]]) {
      item.mIsDefault = true;
    }

    list.AppendElement(item);
  }

  RefPtr<RegisterVoicesRunnable> runnable =
      new RegisterVoicesRunnable(mSpeechService, list);
  NS_DispatchAndSpinEventLoopUntilComplete("EnumVoicesRunnable"_ns,
                                           GetMainThreadSerialEventTarget(),
                                           runnable.forget());

  return NS_OK;

  NS_OBJC_END_TRY_BLOCK_RETURN(NS_ERROR_FAILURE);
}

StaticRefPtr<OSXSpeechSynthesizerService>
    OSXSpeechSynthesizerService::sSingleton;

NS_INTERFACE_MAP_BEGIN(OSXSpeechSynthesizerService)
  NS_INTERFACE_MAP_ENTRY(nsISpeechService)
  NS_INTERFACE_MAP_ENTRY(nsIObserver)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsISpeechService)
NS_INTERFACE_MAP_END

NS_IMPL_ADDREF(OSXSpeechSynthesizerService)
NS_IMPL_RELEASE(OSXSpeechSynthesizerService)

OSXSpeechSynthesizerService::OSXSpeechSynthesizerService()
    : mInitialized(false) {}

bool OSXSpeechSynthesizerService::Init() {
  if (Preferences::GetBool("media.webspeech.synth.test") ||
      !StaticPrefs::media_webspeech_synth_enabled()) {
    // When test is enabled, we shouldn't add OS backend (Bug 1160844)
    return false;
  }

  nsCOMPtr<nsIThread> thread;
  if (NS_FAILED(NS_NewNamedThread("SpeechWorker", getter_AddRefs(thread)))) {
    return false;
  }

  // Get all the voices and register in the SynthVoiceRegistry
  nsCOMPtr<nsIRunnable> runnable = new EnumVoicesRunnable(this);
  thread->Dispatch(runnable, NS_DISPATCH_NORMAL);

  mInitialized = true;
  return true;
}

NS_IMETHODIMP
OSXSpeechSynthesizerService::Speak(const nsAString& aText,
                                   const nsAString& aUri, float aVolume,
                                   float aRate, float aPitch,
                                   nsISpeechTask* aTask) {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  MOZ_ASSERT(StringBeginsWith(aUri, u"urn:moz-tts:osx:"_ns),
             "OSXSpeechSynthesizerService doesn't allow this voice URI");

  AVSpeechSynthesizer* synth = [[AVSpeechSynthesizer alloc] init];

  nsAutoString escapedText;
  // We need to map the the offsets from the given text to the escaped text.
  // The index of the offsets array is the position in the escaped text,
  // the element value is the position in the user-supplied text.
  nsTArray<size_t> offsets;
  offsets.SetCapacity(aText.Length());

  // This loop looks for occurances of "[[" or "]]", escapes them, and
  // populates the offsets array to supply a map to the original offsets.
  for (size_t i = 0; i < aText.Length(); i++) {
    if (aText.Length() > i + 1 && ((aText[i] == ']' && aText[i + 1] == ']') ||
                                   (aText[i] == '[' && aText[i + 1] == '['))) {
      escapedText.AppendLiteral(DLIM_ESCAPE_START);
      offsets.AppendElements(strlen(DLIM_ESCAPE_START));
      escapedText.Append(aText[i]);
      offsets.AppendElement(i);
      escapedText.Append(aText[++i]);
      offsets.AppendElement(i);
      escapedText.AppendLiteral(DLIM_ESCAPE_END);
      offsets.AppendElements(strlen(DLIM_ESCAPE_END));
    } else {
      escapedText.Append(aText[i]);
      offsets.AppendElement(i);
    }
  }

  RefPtr<SpeechTaskCallback> callback =
      new SpeechTaskCallback(aTask, synth, offsets);
  nsresult rv = aTask->Setup(callback);
  NS_ENSURE_SUCCESS(rv, rv);

  NSString* text = nsCocoaUtils::ToNSString(escapedText);

  AVSpeechUtterance* utterance =
      [AVSpeechUtterance speechUtteranceWithString:text];

  // strlen("urn:moz-tts:osx:") == 16
  NSString* identifier = nsCocoaUtils::ToNSString(Substring(aUri, 16));
  AVSpeechSynthesisVoice* voice =
      [AVSpeechSynthesisVoice voiceWithIdentifier:identifier];

  [utterance setVoice:voice];

  [utterance setPitchMultiplier:aPitch];

  [utterance setRate:[utterance rate] * aRate];

  [utterance setVolume:aVolume];

  [synth speakUtterance:utterance];

  aTask->DispatchStart();
  return NS_OK;

  NS_OBJC_END_TRY_BLOCK_RETURN(NS_ERROR_FAILURE);
}

NS_IMETHODIMP
OSXSpeechSynthesizerService::Observe(nsISupports* aSubject, const char* aTopic,
                                     const char16_t* aData) {
  return NS_OK;
}

OSXSpeechSynthesizerService* OSXSpeechSynthesizerService::GetInstance() {
  MOZ_ASSERT(NS_IsMainThread());
  if (XRE_GetProcessType() != GeckoProcessType_Default) {
    return nullptr;
  }

  if (!sSingleton) {
    RefPtr<OSXSpeechSynthesizerService> speechService =
        new OSXSpeechSynthesizerService();
    if (speechService->Init()) {
      sSingleton = speechService;
      ClearOnShutdown(&sSingleton);
    }
  }
  return sSingleton;
}

already_AddRefed<OSXSpeechSynthesizerService>
OSXSpeechSynthesizerService::GetInstanceForService() {
  RefPtr<OSXSpeechSynthesizerService> speechService = GetInstance();
  return speechService.forget();
}

}  // namespace dom
}  // namespace mozilla
