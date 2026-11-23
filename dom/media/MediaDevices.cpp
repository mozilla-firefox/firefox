/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/MediaDevices.h"

#include "AudioDeviceInfo.h"
#include "MediaEngine.h"
#include "MediaEngineFake.h"
#include "MediaTrackConstraints.h"
#include "mozilla/MediaManager.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/MediaDeviceInfo.h"
#include "mozilla/dom/MediaDevicesBinding.h"
#include "mozilla/dom/MediaStreamBinding.h"
#include "mozilla/dom/MediaTrackSupportedConstraintsBinding.h"
#include "mozilla/dom/NavigatorBinding.h"
#include "mozilla/dom/PermissionsPolicyUtils.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/WindowContext.h"
#include "mozilla/intl/Localization.h"
#include "nsContentUtils.h"
#include "nsGlobalWindowInner.h"
#include "nsINamed.h"
#include "nsIScriptGlobalObject.h"
#include "nsPIDOMWindow.h"
#include "nsPIDOMWindowInlines.h"
#include "nsQueryObject.h"

namespace mozilla::dom {

using ConstDeviceSetPromise = MediaManager::ConstDeviceSetPromise;
using LocalDeviceSetPromise = MediaManager::LocalDeviceSetPromise;
using LocalMediaDeviceSetRefCnt = MediaManager::LocalMediaDeviceSetRefCnt;
using MediaDeviceSetRefCnt = MediaManager::MediaDeviceSetRefCnt;
using mozilla::intl::Localization;

MediaDevices::MediaDevices(nsPIDOMWindowInner* aWindow)
    : DOMEventTargetHelper(aWindow), mDefaultOutputLabel(VoidString()) {}

MediaDevices::~MediaDevices() {
  MOZ_ASSERT(NS_IsMainThread());
  mDeviceChangeListener.DisconnectIfExists();
}

// No code needed, unless controlled by prefs, as
// MediaTrackSupportedConstraints members default to true.
void MediaDevices::GetSupportedConstraints(
    MediaTrackSupportedConstraints& aResult) {
  if (Preferences::GetBool("media.navigator.video.resize_mode.enabled")) {
    aResult.mResizeMode.Construct(true);
  }
}

already_AddRefed<Promise> MediaDevices::GetUserMedia(
    const MediaStreamConstraints& aConstraints, CallerType aCallerType,
    ErrorResult& aRv) {
  MOZ_ASSERT(NS_IsMainThread());
  // Get the relevant global for the promise from the wrapper cache because
  // DOMEventTargetHelper::GetOwnerWindow() returns null if the document is
  // unloaded.
  // We know the wrapper exists because it is being used for |this| from JS.
  // See https://github.com/heycam/webidl/issues/932 for why the relevant
  // global is used instead of the current global.
  nsCOMPtr<nsIGlobalObject> global = xpc::NativeGlobal(GetWrapper());
  // global is a window because MediaDevices is exposed only to Window.
  nsCOMPtr<nsPIDOMWindowInner> owner = do_QueryInterface(global);
  if (Document* doc = owner->GetExtantDoc()) {
    if (!owner->IsSecureContext()) {
      doc->SetUseCounter(eUseCounter_custom_GetUserMediaInsec);
    }
    Document* topDoc = doc->GetTopLevelContentDocumentIfSameProcess();
    IgnoredErrorResult ignored;
    if (topDoc && !topDoc->HasFocus(ignored)) {
      doc->SetUseCounter(eUseCounter_custom_GetUserMediaUnfocused);
    }
  }
  RefPtr<Promise> p = Promise::Create(global, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }
  /* If requestedMediaTypes is the empty set, return a promise rejected with a
   * TypeError. */
  if (!MediaManager::IsOn(aConstraints.mVideo) &&
      !MediaManager::IsOn(aConstraints.mAudio)) {
    p->MaybeRejectWithTypeError("audio and/or video is required");
    return p.forget();
  }
  /* If the relevant settings object's responsible document is NOT fully
   * active, return a promise rejected with a DOMException object whose name
   * attribute has the value "InvalidStateError". */
  if (!owner->IsFullyActive()) {
    p->MaybeRejectWithInvalidStateError("The document is not fully active.");
    return p.forget();
  }
  const OwningBooleanOrMediaTrackConstraints& video = aConstraints.mVideo;
  if (aCallerType != CallerType::System && video.IsMediaTrackConstraints()) {
    const Optional<nsString>& mediaSource =
        video.GetAsMediaTrackConstraints().mMediaSource;
    if (mediaSource.WasPassed() &&
        !mediaSource.Value().EqualsLiteral("camera")) {
      WindowContext* wc = owner->GetWindowContext();
      if (!wc || !wc->HasValidTransientUserGestureActivation()) {
        p->MaybeRejectWithInvalidStateError(
            "Display capture requires transient activation "
            "from a user gesture.");
        return p.forget();
      }
    }
  }
  RefPtr<MediaDevices> self(this);
  GetUserMedia(owner, aConstraints, aCallerType)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [this, self, p](RefPtr<DOMMediaStream>&& aStream) {
            if (!GetWindowIfCurrent()) {
              return;  // Leave Promise pending after navigation by design.
            }
            p->MaybeResolve(std::move(aStream));
          },
          [this, self, p](const RefPtr<MediaMgrError>& error) {
            nsPIDOMWindowInner* window = GetWindowIfCurrent();
            if (!window) {
              return;  // Leave Promise pending after navigation by design.
            }
            error->Reject(p);
          });
  return p.forget();
}

RefPtr<MediaDevices::StreamPromise> MediaDevices::GetUserMedia(
    nsPIDOMWindowInner* aWindow, const MediaStreamConstraints& aConstraints,
    CallerType aCallerType) {
  MOZ_ASSERT(NS_IsMainThread());
  bool haveFake = aConstraints.mFake.WasPassed() && aConstraints.mFake.Value();
  const OwningBooleanOrMediaTrackConstraints& video = aConstraints.mVideo;
  const OwningBooleanOrMediaTrackConstraints& audio = aConstraints.mAudio;
  bool isMicrophone =
      !haveFake &&
      (audio.IsBoolean()
           ? audio.GetAsBoolean()
           : !audio.GetAsMediaTrackConstraints().mMediaSource.WasPassed());
  bool isCamera =
      !haveFake &&
      (video.IsBoolean()
           ? video.GetAsBoolean()
           : !video.GetAsMediaTrackConstraints().mMediaSource.WasPassed());

  RefPtr<MediaDevices> self(this);
  return MediaManager::Get()
      ->GetUserMedia(aWindow, aConstraints, aCallerType)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [this, self, isMicrophone,
           isCamera](RefPtr<DOMMediaStream>&& aStream) {
            if (isMicrophone) {
              mCanExposeMicrophoneInfo = true;
            }
            if (isCamera) {
              mCanExposeCameraInfo = true;
            }
            return StreamPromise::CreateAndResolve(std::move(aStream),
                                                   __func__);
          },
          [](RefPtr<MediaMgrError>&& aError) {
            return StreamPromise::CreateAndReject(std::move(aError), __func__);
          });
}

already_AddRefed<Promise> MediaDevices::EnumerateDevices(ErrorResult& aRv) {
  MOZ_ASSERT(NS_IsMainThread());
  nsCOMPtr<nsIGlobalObject> global = xpc::NativeGlobal(GetWrapper());
  nsCOMPtr<nsPIDOMWindowInner> owner = do_QueryInterface(global);
  if (Document* doc = owner->GetExtantDoc()) {
    if (!owner->IsSecureContext()) {
      doc->SetUseCounter(eUseCounter_custom_EnumerateDevicesInsec);
    }
    Document* topDoc = doc->GetTopLevelContentDocumentIfSameProcess();
    IgnoredErrorResult ignored;
    if (topDoc && !topDoc->HasFocus(ignored)) {
      doc->SetUseCounter(eUseCounter_custom_EnumerateDevicesUnfocused);
    }
  }
  RefPtr<Promise> p = Promise::Create(global, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }
  mPendingEnumerateDevicesPromises.AppendElement(p);
  MaybeResumeDeviceExposure();
  return p.forget();
}

void MediaDevices::MaybeResumeDeviceExposure() {
  if (mPendingEnumerateDevicesPromises.IsEmpty() &&
      !mHaveUnprocessedDeviceListChange) {
    return;
  }
  nsPIDOMWindowInner* window = GetOwnerWindow();
  if (!window || !window->IsFullyActive()) {
    return;
  }
  if (!StaticPrefs::media_devices_unfocused_enabled()) {
    // Device list changes are not exposed to unfocused contexts because the
    // timing information would allow fingerprinting for content to identify
    // concurrent browsing, even when pages are in different containers.
    BrowsingContext* bc = window->GetBrowsingContext();
    if (!bc->IsActive() ||  // background tab or browser window fully obscured
        !bc->GetIsActiveBrowserWindow()) {  // browser window without focus
      return;
    }
  }
  bool shouldResistFingerprinting =
      window->AsGlobal()->ShouldResistFingerprinting(RFPTarget::MediaDevices);
  MediaManager::Get()->GetPhysicalDevices()->Then(
      GetCurrentSerialEventTarget(), __func__,
      [self = RefPtr(this), this,
       haveDeviceListChange = mHaveUnprocessedDeviceListChange,
       enumerateDevicesPromises = std::move(mPendingEnumerateDevicesPromises),
       shouldResistFingerprinting](
          RefPtr<const MediaDeviceSetRefCnt> aAllDevices) mutable {
        RefPtr<MediaDeviceSetRefCnt> exposedDevices =
            FilterExposedDevices(*aAllDevices);
        if (haveDeviceListChange && !shouldResistFingerprinting) {
          if (ShouldQueueDeviceChange(*exposedDevices)) {
            NS_DispatchToCurrentThread(NS_NewRunnableFunction(
                "devicechange", [self = RefPtr(this), this] {
                  DispatchTrustedEvent(u"devicechange"_ns);
                }));
          }
          mLastPhysicalDevices = std::move(aAllDevices);
        }
        if (!enumerateDevicesPromises.IsEmpty()) {
          ResumeEnumerateDevices(std::move(enumerateDevicesPromises),
                                 std::move(exposedDevices));
        }
      },
      [](RefPtr<MediaMgrError>&&) {
        MOZ_ASSERT_UNREACHABLE("GetPhysicalDevices does not reject");
      });
  mHaveUnprocessedDeviceListChange = false;
}

static bool IsLegacyMode(nsPIDOMWindowInner* window) {
  if (StaticPrefs::media_devices_enumerate_legacy_enabled()) {
    return true;
  }
  if (window->GetDocumentURI()) {
    nsAutoCString host;
    window->GetDocumentURI()->GetAsciiHost(host);
    if (media::HostnameInPref("media.devices.enumerate.legacy.allowlist",
                              host)) {
      return true;
    }
  }
  return false;
}

RefPtr<MediaDeviceSetRefCnt> MediaDevices::FilterExposedDevices(
    const MediaDeviceSet& aDevices) const {
  nsPIDOMWindowInner* window = GetOwnerWindow();
  RefPtr exposed = new MediaDeviceSetRefCnt();
  if (!window) {
    return exposed;  // Promises will be left pending
  }
  Document* doc = window->GetExtantDoc();
  if (!doc) {
    return exposed;
  }
  // Only expose devices which are allowed to use:
  // https://w3c.github.io/mediacapture-main/#dom-mediadevices-enumeratedevices
  bool dropMics =
      !PermissionsPolicyUtils::IsFeatureAllowed(doc, u"microphone"_ns);
  bool dropCams = !PermissionsPolicyUtils::IsFeatureAllowed(doc, u"camera"_ns);
  bool dropSpeakers =
      !Preferences::GetBool("media.setsinkid.enabled") ||
      !PermissionsPolicyUtils::IsFeatureAllowed(doc, u"speaker-selection"_ns);
  bool shouldResistFingerprinting =
      window->AsGlobal()->ShouldResistFingerprinting(RFPTarget::MediaDevices);
  bool legacy = IsLegacyMode(window);
  bool outputIsDefault = true;  // First output is the default.
  bool haveDefaultOutput = false;
  nsTHashSet<nsString> exposedMicrophoneGroupIds;
  for (const auto& device : aDevices) {
    switch (device->mKind) {
      case MediaDeviceKind::Audioinput:
        if (dropMics) {
          continue;
        }
        if (mCanExposeMicrophoneInfo) {
          exposedMicrophoneGroupIds.Insert(device->mRawGroupID);
        }
        if (!mCanExposeMicrophoneInfo && !legacy) {
          dropMics = true;
        }
        break;
      case MediaDeviceKind::Videoinput:
        if (dropCams) {
          continue;
        }
        if (!mCanExposeCameraInfo && !legacy) {
          dropCams = true;
        }
        break;
      case MediaDeviceKind::Audiooutput:
        if (dropSpeakers ||
            (!mExplicitlyGrantedAudioOutputRawIds.Contains(device->mRawID) &&
             (!mCanExposeMicrophoneInfo ||
              (shouldResistFingerprinting &&
               // Assumes aDevices order has microphones before speakers.
               !exposedMicrophoneGroupIds.Contains(device->mRawGroupID))))) {
          outputIsDefault = false;
          continue;
        }
        if (!haveDefaultOutput && !outputIsDefault) {
          // Insert a virtual default device so that the first enumerated
          // device is the default output.
          if (mDefaultOutputLabel.IsVoid()) {
            mDefaultOutputLabel.SetIsVoid(false);
            AutoTArray<nsCString, 1> resourceIds{"dom/media.ftl"_ns};
            RefPtr l10n = Localization::Create(resourceIds, /*sync*/ true);
            nsAutoCString translation;
            IgnoredErrorResult rv;
            l10n->FormatValueSync("default-audio-output-device-label"_ns,
                                  translation, rv);
            if (!rv.Failed()) {
              AppendUTF8toUTF16(translation, mDefaultOutputLabel);
            }
          }
          RefPtr info = new AudioDeviceInfo(
              nullptr, mDefaultOutputLabel, u""_ns, u""_ns,
              CUBEB_DEVICE_TYPE_OUTPUT, CUBEB_DEVICE_STATE_ENABLED,
              CUBEB_DEVICE_PREF_ALL, CUBEB_DEVICE_FMT_ALL,
              CUBEB_DEVICE_FMT_S16NE, 2, 44100, 44100, 44100, 128, 128);
          exposed->AppendElement(
              new MediaDevice(new MediaEngineFake(), info, u""_ns));
        }
        haveDefaultOutput = true;
        break;
        // Avoid `default:` so that `-Wswitch` catches missing
        // enumerators at compile time.
    }
    exposed->AppendElement(device);
  }

  if (doc->ShouldResistFingerprinting(RFPTarget::MediaDevices)) {
    // We expose a single device of each kind.
    // Legacy mode also achieves the same thing, except for speakers.
    nsTHashSet<MediaDeviceKind> seenKinds;

    for (uint32_t i = 0; i < exposed->Length(); i++) {
      RefPtr<mozilla::MediaDevice> device = exposed->ElementAt(i);
      if (seenKinds.Contains(device->mKind)) {
        exposed->RemoveElementAt(i);
        i--;
        continue;
      }
      seenKinds.Insert(device->mKind);
    }

    // We haven't seen at least one of each kind of device.
    // Audioinput, Videoinput, Audiooutput.
    // Insert fake devices.
    if (seenKinds.Count() != 3) {
      RefPtr fakeEngine = new MediaEngineFake();
      RefPtr fakeDevices = new MediaDeviceSetRefCnt();
      // The order in which we insert the fake devices is important.
      // Microphone is inserted first, then camera, then speaker.
      // If we haven't seen a microphone, insert a fake one.
      if (!seenKinds.Contains(MediaDeviceKind::Audioinput)) {
        fakeEngine->EnumerateDevices(MediaSourceEnum::Microphone,
                                     MediaSinkEnum::Other, fakeDevices);
        exposed->InsertElementAt(0, fakeDevices->LastElement());
      }
      // If we haven't seen a camera, insert a fake one.
      if (!seenKinds.Contains(MediaDeviceKind::Videoinput)) {
        fakeEngine->EnumerateDevices(MediaSourceEnum::Camera,
                                     MediaSinkEnum::Other, fakeDevices);
        exposed->InsertElementAt(1, fakeDevices->LastElement());
      }
      // If we haven't seen a speaker, insert a fake one.
      if (!seenKinds.Contains(MediaDeviceKind::Audiooutput) &&
          mCanExposeMicrophoneInfo) {
        RefPtr info = new AudioDeviceInfo(
            nullptr, u""_ns, u""_ns, u""_ns, CUBEB_DEVICE_TYPE_OUTPUT,
            CUBEB_DEVICE_STATE_ENABLED, CUBEB_DEVICE_PREF_ALL,
            CUBEB_DEVICE_FMT_ALL, CUBEB_DEVICE_FMT_S16NE, 2, 44100, 44100,
            44100, 128, 128);
        exposed->AppendElement(
            new MediaDevice(new MediaEngineFake(), info, u""_ns));
      }
    }
  }

  return exposed;
}

bool MediaDevices::CanExposeInfo(MediaDeviceKind aKind) const {
  switch (aKind) {
    case MediaDeviceKind::Audioinput:
      return mCanExposeMicrophoneInfo;
    case MediaDeviceKind::Videoinput:
      return mCanExposeCameraInfo;
    case MediaDeviceKind::Audiooutput:
      // Assumes caller has used FilterExposedDevices()
      return true;
      // Avoid `default:` so that `-Wswitch` catches missing enumerators at
      // compile time.
  }
  MOZ_ASSERT_UNREACHABLE("unexpected MediaDeviceKind");
  return false;
}

bool MediaDevices::ShouldQueueDeviceChange(
    const MediaDeviceSet& aExposedDevices) const {
  if (!mLastPhysicalDevices) {  // SetupDeviceChangeListener not complete
    return false;
  }
  RefPtr<MediaDeviceSetRefCnt> lastExposedDevices =
      FilterExposedDevices(*mLastPhysicalDevices);
  auto exposed = aExposedDevices.begin();
  auto exposedEnd = aExposedDevices.end();
  auto last = lastExposedDevices->begin();
  auto lastEnd = lastExposedDevices->end();
  // Lists from FilterExposedDevices may have multiple devices of the same
  // kind even when only a single anonymous device of that kind should be
  // exposed by enumerateDevices() (but multiple devices are currently exposed
  // - bug 1528042).  "devicechange" events are not queued when the number
  // of such devices changes but remains non-zero.
  while (exposed < exposedEnd && last < lastEnd) {
    // First determine whether there is at least one device of the same kind
    // in both `aExposedDevices` and `lastExposedDevices`.
    // A change between zero and non-zero numbers of microphone or camera
    // devices triggers a devicechange event even if that kind of device is
    // not yet exposed.
    MediaDeviceKind kind = (*exposed)->mKind;
    if (kind != (*last)->mKind) {
      return true;
    }
    // `exposed` and `last` have matching kind.
    if (CanExposeInfo(kind)) {
      // Queue "devicechange" if there has been any change in devices of this
      // exposed kind.  ID and kind uniquely identify a device.
      if ((*exposed)->mRawID != (*last)->mRawID) {
        return true;
      }
      ++exposed;
      ++last;
      continue;
    }
    // `aExposedDevices` and `lastExposedDevices` both have non-zero numbers
    // of devices of this unexposed kind.
    // Skip remaining devices of this kind because all devices of this kind
    // should be exposed as a single anonymous device.
    do {
      ++exposed;
    } while (exposed != exposedEnd && (*exposed)->mKind == kind);
    do {
      ++last;
    } while (last != lastEnd && (*last)->mKind == kind);
  }
  // Queue "devicechange" if the number of exposed devices differs.
  return exposed < exposedEnd || last < lastEnd;
}

void MediaDevices::ResumeEnumerateDevices(
    nsTArray<RefPtr<Promise>>&& aPromises,
    RefPtr<const MediaDeviceSetRefCnt> aExposedDevices) const {
  nsCOMPtr<nsPIDOMWindowInner> window = GetOwnerWindow();
  if (!window) {
    return;  // Leave Promise pending after navigation by design.
  }
  MediaManager::Get()
      ->AnonymizeDevices(window, std::move(aExposedDevices))
      ->Then(GetCurrentSerialEventTarget(), __func__,
             [self = RefPtr(this), this, promises = std::move(aPromises)](
                 const LocalDeviceSetPromise::ResolveOrRejectValue&
                     aLocalDevices) {
               nsPIDOMWindowInner* window = GetWindowIfCurrent();
               if (!window) {
                 return;  // Leave Promises pending after navigation by design.
               }
               for (const RefPtr<Promise>& promise : promises) {
                 if (aLocalDevices.IsReject()) {
                   aLocalDevices.RejectValue()->Reject(promise);
                 } else {
                   ResolveEnumerateDevicesPromise(
                       promise, *aLocalDevices.ResolveValue());
                 }
               }
             });
}

void MediaDevices::ResolveEnumerateDevicesPromise(
    Promise* aPromise, const LocalMediaDeviceSet& aDevices) const {
  nsCOMPtr<nsPIDOMWindowInner> window = GetOwnerWindow();
  auto windowId = window->WindowID();
  nsTArray<RefPtr<MediaDeviceInfo>> infos;
  bool legacy = IsLegacyMode(window);
  bool capturePermitted =
      legacy &&
      MediaManager::Get()->IsActivelyCapturingOrHasAPermission(windowId);

  for (const RefPtr<LocalMediaDevice>& device : aDevices) {
    bool exposeInfo = CanExposeInfo(device->Kind()) || legacy;
    bool exposeLabel = legacy ? capturePermitted : exposeInfo;
    infos.AppendElement(MakeRefPtr<MediaDeviceInfo>(
        exposeInfo ? device->mID : u""_ns, device->Kind(),
        exposeLabel ? device->mName : u""_ns,
        exposeInfo ? device->mGroupID : u""_ns));
  }
  aPromise->MaybeResolve(std::move(infos));
}

already_AddRefed<Promise> MediaDevices::GetDisplayMedia(
    const DisplayMediaStreamConstraints& aConstraints, CallerType aCallerType,
    ErrorResult& aRv) {
  nsCOMPtr<nsIGlobalObject> global = xpc::NativeGlobal(GetWrapper());
  RefPtr<Promise> p = Promise::Create(global, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }
  nsCOMPtr<nsPIDOMWindowInner> owner = do_QueryInterface(global);
  /* If the relevant global object of this does not have transient activation,
   * return a promise rejected with a DOMException object whose name attribute
   * has the value InvalidStateError. */
  WindowContext* wc = owner->GetWindowContext();
  if (!wc || !wc->HasValidTransientUserGestureActivation()) {
    p->MaybeRejectWithInvalidStateError(
        "getDisplayMedia requires transient activation from a user gesture.");
    return p.forget();
  }
  /* If constraints.video is false, return a promise rejected with a newly
   * created TypeError. */
  if (!MediaManager::IsOn(aConstraints.mVideo)) {
    p->MaybeRejectWithTypeError("video is required");
    return p.forget();
  }
  MediaStreamConstraints c;
  auto& vc = c.mVideo.SetAsMediaTrackConstraints();

  if (aConstraints.mVideo.IsMediaTrackConstraints()) {
    vc = aConstraints.mVideo.GetAsMediaTrackConstraints();
    /* If CS contains a member named advanced, return a promise rejected with
     * a newly created TypeError. */
    if (vc.mAdvanced.WasPassed()) {
      p->MaybeRejectWithTypeError("advanced not allowed");
      return p.forget();
    }
    auto getCLR = [](const auto& aCon) -> const ConstrainLongRange& {
      static ConstrainLongRange empty;
      return (aCon.WasPassed() && !aCon.Value().IsLong())
                 ? aCon.Value().GetAsConstrainLongRange()
                 : empty;
    };
    auto getCDR = [](auto&& aCon) -> const ConstrainDoubleRange& {
      static ConstrainDoubleRange empty;
      return (aCon.WasPassed() && !aCon.Value().IsDouble())
                 ? aCon.Value().GetAsConstrainDoubleRange()
                 : empty;
    };
    const auto& w = getCLR(vc.mWidth);
    const auto& h = getCLR(vc.mHeight);
    const auto& f = getCDR(vc.mFrameRate);
    /* If CS contains a member whose name specifies a constrainable property
     * applicable to display surfaces, and whose value in turn is a dictionary
     * containing a member named either min or exact, return a promise
     * rejected with a newly created TypeError. */
    if (w.mMin.WasPassed() || h.mMin.WasPassed() || f.mMin.WasPassed()) {
      p->MaybeRejectWithTypeError("min not allowed");
      return p.forget();
    }
    if (w.mExact.WasPassed() || h.mExact.WasPassed() || f.mExact.WasPassed()) {
      p->MaybeRejectWithTypeError("exact not allowed");
      return p.forget();
    }
    /* If CS contains a member whose name, failedConstraint specifies a
     * constrainable property, constraint, applicable to display surfaces, and
     * whose value in turn is a dictionary containing a member named max, and
     * that member's value in turn is less than the constrainable property's
     * floor value, then let failedConstraint be the name of the constraint,
     * let message be either undefined or an informative human-readable
     * message, and return a promise rejected with a new OverconstrainedError
     * created by calling OverconstrainedError(failedConstraint, message). */
    // We fail early without incurring a prompt, on known-to-fail constraint
    // values that don't reveal anything about the user's system.
    const char* badConstraint = nullptr;
    if (w.mMax.WasPassed() && w.mMax.Value() < 1) {
      badConstraint = "width";
    }
    if (h.mMax.WasPassed() && h.mMax.Value() < 1) {
      badConstraint = "height";
    }
    if (f.mMax.WasPassed() && f.mMax.Value() < 1) {
      badConstraint = "frameRate";
    }
    if (badConstraint) {
      p->MaybeReject(MakeRefPtr<dom::MediaStreamError>(
          owner, *MakeRefPtr<MediaMgrError>(
                     MediaMgrError::Name::OverconstrainedError, "",
                     NS_ConvertASCIItoUTF16(badConstraint))));
      return p.forget();
    }
  }
  /* If the relevant settings object's responsible document is NOT fully
   * active, return a promise rejected with a DOMException object whose name
   * attribute has the value "InvalidStateError". */
  if (!owner->IsFullyActive()) {
    p->MaybeRejectWithInvalidStateError("The document is not fully active.");
    return p.forget();
  }
  /* Consume user activation with this's relevant global object. */
  if (aCallerType != CallerType::System) {
    wc->ConsumeTransientUserGestureActivation();
  }
  // We ask for "screen" sharing.
  //
  // If this is a privileged call or permission is disabled, this gives us full
  // screen sharing by default, which is useful for internal testing.
  //
  // If this is a non-priviliged call, GetUserMedia() will change it to "window"
  // for us.
  vc.mMediaSource.Reset();
  vc.mMediaSource.Construct().AssignASCII(
      dom::GetEnumString(MediaSourceEnum::Screen));

  RefPtr<MediaDevices> self(this);
  MediaManager::Get()
      ->GetUserMedia(owner, c, aCallerType)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [this, self, p, aCallerType](RefPtr<DOMMediaStream>&& aStream) {
            nsPIDOMWindowInner* window = GetWindowIfCurrent();
            if (!window) {
              return;  // leave promise pending after navigation.
            }
            /* Set the last activation timestamp to the current high
             * resolution time. */
            if (WindowContext* wc = window->GetWindowContext();
                wc && aCallerType != CallerType::System) {
              wc->NotifyUserGestureActivation();
            }
            p->MaybeResolve(std::move(aStream));
          },
          [this, self, p](RefPtr<MediaMgrError>&& error) {
            nsPIDOMWindowInner* window = GetWindowIfCurrent();
            if (!window) {
              return;  // leave promise pending after navigation.
            }
            error->Reject(p);
          });
  return p.forget();
}

already_AddRefed<Promise> MediaDevices::SelectAudioOutput(
    const AudioOutputOptions& aOptions, CallerType aCallerType,
    ErrorResult& aRv) {
  nsCOMPtr<nsIGlobalObject> global = xpc::NativeGlobal(GetWrapper());
  RefPtr<Promise> p = Promise::Create(global, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }
  /* (This includes the expected user activation update of
   * https://github.com/w3c/mediacapture-output/issues/107)
   * If the relevant global object of this does not have transient activation,
   * return a promise rejected with a DOMException object whose name attribute
   * has the value InvalidStateError. */
  nsCOMPtr<nsPIDOMWindowInner> owner = do_QueryInterface(global);
  WindowContext* wc = owner->GetWindowContext();
  if (!wc || !wc->HasValidTransientUserGestureActivation()) {
    p->MaybeRejectWithInvalidStateError(
        "selectAudioOutput requires transient user activation.");
    return p.forget();
  }
  RefPtr<MediaDevices> self(this);
  MediaManager::Get()
      ->SelectAudioOutput(owner, aOptions, aCallerType)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [this, self, p](RefPtr<LocalMediaDevice> aDevice) {
            nsPIDOMWindowInner* window = GetWindowIfCurrent();
            if (!window) {
              return;  // Leave Promise pending after navigation by design.
            }
            MOZ_ASSERT(aDevice->Kind() == dom::MediaDeviceKind::Audiooutput);
            mExplicitlyGrantedAudioOutputRawIds.Insert(aDevice->RawID());
            p->MaybeResolve(
                MakeRefPtr<MediaDeviceInfo>(aDevice->mID, aDevice->Kind(),
                                            aDevice->mName, aDevice->mGroupID));
          },
          [this, self, p](const RefPtr<MediaMgrError>& error) {
            nsPIDOMWindowInner* window = GetWindowIfCurrent();
            if (!window) {
              return;  // Leave Promise pending after navigation by design.
            }
            error->Reject(p);
          });
  return p.forget();
}

static RefPtr<AudioDeviceInfo> CopyWithNullDeviceId(
    AudioDeviceInfo* aDeviceInfo) {
  MOZ_ASSERT(aDeviceInfo->Preferred());

  nsString vendor;
  aDeviceInfo->GetVendor(vendor);
  uint16_t type;
  aDeviceInfo->GetType(&type);
  uint16_t state;
  aDeviceInfo->GetState(&state);
  uint16_t pref;
  aDeviceInfo->GetPreferred(&pref);
  uint16_t supportedFormat;
  aDeviceInfo->GetSupportedFormat(&supportedFormat);
  uint16_t defaultFormat;
  aDeviceInfo->GetDefaultFormat(&defaultFormat);
  uint32_t maxChannels;
  aDeviceInfo->GetMaxChannels(&maxChannels);
  uint32_t defaultRate;
  aDeviceInfo->GetDefaultRate(&defaultRate);
  uint32_t maxRate;
  aDeviceInfo->GetMaxRate(&maxRate);
  uint32_t minRate;
  aDeviceInfo->GetMinRate(&minRate);
  uint32_t maxLatency;
  aDeviceInfo->GetMaxLatency(&maxLatency);
  uint32_t minLatency;
  aDeviceInfo->GetMinLatency(&minLatency);

  return MakeRefPtr<AudioDeviceInfo>(
      nullptr, aDeviceInfo->Name(), aDeviceInfo->GroupID(), vendor, type, state,
      pref, supportedFormat, defaultFormat, maxChannels, defaultRate, maxRate,
      minRate, maxLatency, minLatency);
}

RefPtr<MediaDevices::SinkInfoPromise> MediaDevices::GetSinkDevice(
    const nsString& aDeviceId) {
  MOZ_ASSERT(NS_IsMainThread());
  return MediaManager::Get()
      ->GetPhysicalDevices()
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [self = RefPtr(this), this,
           aDeviceId](RefPtr<const MediaDeviceSetRefCnt> aRawDevices) {
            nsCOMPtr<nsPIDOMWindowInner> window = GetOwnerWindow();
            if (!window) {
              return LocalDeviceSetPromise::CreateAndReject(
                  new MediaMgrError(MediaMgrError::Name::AbortError), __func__);
            }
            // Don't filter if matching the preferred device, because that may
            // not be exposed.
            RefPtr devices = aDeviceId.IsEmpty()
                                 ? std::move(aRawDevices)
                                 : FilterExposedDevices(*aRawDevices);
            return MediaManager::Get()->AnonymizeDevices(window,
                                                         std::move(devices));
          },
          [](RefPtr<MediaMgrError>&& reason) {
            MOZ_ASSERT_UNREACHABLE("GetPhysicalDevices does not reject");
            return RefPtr<LocalDeviceSetPromise>();
          })
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [aDeviceId](RefPtr<LocalMediaDeviceSetRefCnt> aDevices) {
            RefPtr<AudioDeviceInfo> outputInfo;
            // Check for a matching device.
            for (const RefPtr<LocalMediaDevice>& device : *aDevices) {
              if (device->Kind() != dom::MediaDeviceKind::Audiooutput) {
                continue;
              }
              if (aDeviceId.IsEmpty()) {
                MOZ_ASSERT(device->GetAudioDeviceInfo()->Preferred(),
                           "First Audiooutput should be preferred");
                return SinkInfoPromise::CreateAndResolve(
                    CopyWithNullDeviceId(device->GetAudioDeviceInfo()),
                    __func__);
              } else if (aDeviceId.Equals(device->mID)) {
                return SinkInfoPromise::CreateAndResolve(
                    device->GetAudioDeviceInfo(), __func__);
              }
            }
            /* If sinkId is not the empty string and does not match any audio
             * output device identified by the result that would be provided
             * by enumerateDevices(), reject p with a new DOMException whose
             * name is NotFoundError and abort these substeps. */
            return SinkInfoPromise::CreateAndReject(NS_ERROR_NOT_AVAILABLE,
                                                    __func__);
          },
          // aRejectMethod =
          [](RefPtr<MediaMgrError>&& aError) {
            return SinkInfoPromise::CreateAndReject(NS_ERROR_NOT_AVAILABLE,
                                                    __func__);
          });
}

NS_IMPL_ISUPPORTS_CYCLE_COLLECTION_INHERITED_0(MediaDevices,
                                               DOMEventTargetHelper)
NS_IMPL_CYCLE_COLLECTION_INHERITED(MediaDevices, DOMEventTargetHelper,
                                   mPendingEnumerateDevicesPromises)

void MediaDevices::OnDeviceChange() {
  MOZ_ASSERT(NS_IsMainThread());
  if (NS_FAILED(CheckCurrentGlobalCorrectness())) {
    // This is a ghost window, don't do anything.
    return;
  }

  mHaveUnprocessedDeviceListChange = true;
  MaybeResumeDeviceExposure();
}

mozilla::dom::EventHandlerNonNull* MediaDevices::GetOndevicechange() {
  return GetEventHandler(nsGkAtoms::ondevicechange);
}

void MediaDevices::SetupDeviceChangeListener() {
  if (mIsDeviceChangeListenerSetUp) {
    return;
  }

  nsPIDOMWindowInner* window = GetOwnerWindow();
  if (!window) {
    return;
  }

  mDeviceChangeListener = MediaManager::Get()->DeviceListChangeEvent().Connect(
      GetMainThreadSerialEventTarget(), this, &MediaDevices::OnDeviceChange);
  mIsDeviceChangeListenerSetUp = true;

  MediaManager::Get()->GetPhysicalDevices()->Then(
      GetCurrentSerialEventTarget(), __func__,
      [self = RefPtr(this), this](RefPtr<const MediaDeviceSetRefCnt> aDevices) {
        mLastPhysicalDevices = std::move(aDevices);
      },
      [](RefPtr<MediaMgrError>&& reason) {
        MOZ_ASSERT_UNREACHABLE("GetPhysicalDevices does not reject");
      });
}

void MediaDevices::SetOndevicechange(
    mozilla::dom::EventHandlerNonNull* aCallback) {
  SetEventHandler(nsGkAtoms::ondevicechange, aCallback);
}

void MediaDevices::EventListenerAdded(nsAtom* aType) {
  DOMEventTargetHelper::EventListenerAdded(aType);
  SetupDeviceChangeListener();
}

JSObject* MediaDevices::WrapObject(JSContext* aCx,
                                   JS::Handle<JSObject*> aGivenProto) {
  return MediaDevices_Binding::Wrap(aCx, this, aGivenProto);
}

}  // namespace mozilla::dom
