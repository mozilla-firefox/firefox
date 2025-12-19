/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/MIDIPermissionRequest.h"

#include "mozilla/BasePrincipal.h"
#include "mozilla/Preferences.h"
#include "mozilla/RandomNum.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/MIDIAccessManager.h"
#include "mozilla/dom/MIDIOptionsBinding.h"
#include "mozilla/ipc/BackgroundChild.h"
#include "mozilla/ipc/PBackgroundChild.h"
#include "nsContentUtils.h"
#include "nsIGlobalObject.h"

//-------------------------------------------------
// MIDI Permission Requests
//-------------------------------------------------

using namespace mozilla::dom;

NS_IMPL_CYCLE_COLLECTION_INHERITED(MIDIPermissionRequest,
                                   ContentPermissionRequestBase, mPromise)

NS_IMPL_QUERY_INTERFACE_CYCLE_COLLECTION_INHERITED(MIDIPermissionRequest,
                                                   ContentPermissionRequestBase,
                                                   nsIRunnable)

NS_IMPL_ADDREF_INHERITED(MIDIPermissionRequest, ContentPermissionRequestBase)
NS_IMPL_RELEASE_INHERITED(MIDIPermissionRequest, ContentPermissionRequestBase)

MIDIPermissionRequest::MIDIPermissionRequest(nsPIDOMWindowInner* aWindow,
                                             Promise* aPromise,
                                             const MIDIOptions& aOptions)
    : ContentPermissionRequestBase(
          aWindow->GetDoc()->NodePrincipal(), aWindow,
          ""_ns,  // We check prefs in a custom way here
          "midi"_ns),
      mPromise(aPromise),
      mNeedsSysex(aOptions.mSysex) {
  MOZ_ASSERT(aWindow);
  MOZ_ASSERT(aPromise, "aPromise should not be null!");
  MOZ_ASSERT(aWindow->GetDoc());
  mPrincipal = aWindow->GetDoc()->NodePrincipal();
  MOZ_ASSERT(mPrincipal);
}

NS_IMETHODIMP
MIDIPermissionRequest::GetTypes(nsIArray** aTypes) {
  NS_ENSURE_ARG_POINTER(aTypes);
  nsTArray<nsString> options;

  // The previous implementation made no differences between midi and
  // midi-sysex. The check on the SitePermsAddonProvider pref should be removed
  // at the same time as the old implementation.
  if (mNeedsSysex || !StaticPrefs::dom_sitepermsaddon_provider_enabled()) {
    options.AppendElement(u"sysex"_ns);
  }
  return nsContentPermissionUtils::CreatePermissionArray(mType, options,
                                                         aTypes);
}

NS_IMETHODIMP
MIDIPermissionRequest::Cancel() {
  mCancelTimer = nullptr;
  mPromise->MaybeRejectWithNotAllowedError(
      "WebMIDI requires a site permission add-on to activate");
  return NS_OK;
}

NS_IMETHODIMP
MIDIPermissionRequest::Allow(JS::Handle<JS::Value> aChoices) {
  MOZ_ASSERT(aChoices.isUndefined());
  MIDIAccessManager* mgr = MIDIAccessManager::Get();
  mgr->CreateMIDIAccess(mWindow, mNeedsSysex, mPromise);
  return NS_OK;
}

NS_IMETHODIMP
MIDIPermissionRequest::Run() {
  // If the testing flag is true, skip dialog
  if (Preferences::GetBool("midi.prompt.testing", false)) {
    bool allow =
        Preferences::GetBool("media.navigator.permission.disabled", false);
    if (allow) {
      Allow(JS::UndefinedHandleValue);
    } else {
      Cancel();
    }
    return NS_OK;
  }

  nsCString permName = "midi"_ns;
  // The previous implementation made no differences between midi and
  // midi-sysex. The check on the SitePermsAddonProvider pref should be removed
  // at the same time as the old implementation.
  if (mNeedsSysex || !StaticPrefs::dom_sitepermsaddon_provider_enabled()) {
    permName.Append("-sysex");
  }

  // First, check for an explicit allow/deny. Note that we want to support
  // granting a permission on the base domain and then using it on a subdomain,
  // which is why we use the non-"Exact" variants of these APIs. See bug
  // 1757218.
  if (nsContentUtils::IsSitePermAllow(mPrincipal, permName)) {
    Allow(JS::UndefinedHandleValue);
    return NS_OK;
  }

  if (nsContentUtils::IsSitePermDeny(mPrincipal, permName)) {
    CancelWithRandomizedDelay();
    return NS_OK;
  }

  // If the add-on is not installed, and sitepermsaddon provider not enabled,
  // auto-deny (except for localhost).
  if (StaticPrefs::dom_webmidi_gated() &&
      !StaticPrefs::dom_sitepermsaddon_provider_enabled() &&
      !nsContentUtils::HasSitePerm(mPrincipal, permName) &&
      !mPrincipal->GetIsLoopbackHost()) {
    CancelWithRandomizedDelay();
    return NS_OK;
  }

  // If sitepermsaddon provider is enabled and user denied install,
  // auto-deny (except for localhost, where we use a regular permission flow).
  if (StaticPrefs::dom_sitepermsaddon_provider_enabled() &&
      nsContentUtils::IsSitePermDeny(mPrincipal, "install"_ns) &&
      !mPrincipal->GetIsLoopbackHost()) {
    CancelWithRandomizedDelay();
    return NS_OK;
  }

  // Before we bother the user with a prompt, see if they have any devices. If
  // they don't, just report denial.
  MOZ_ASSERT(NS_IsMainThread());
  mozilla::ipc::PBackgroundChild* actor =
      mozilla::ipc::BackgroundChild::GetOrCreateForCurrentThread();
  if (NS_WARN_IF(!actor)) {
    return NS_ERROR_FAILURE;
  }
  RefPtr<MIDIPermissionRequest> self = this;
  actor->SendHasMIDIDevice(
      [=](bool aHasDevices) {
        MOZ_ASSERT(NS_IsMainThread());

        if (aHasDevices) {
          self->DoPrompt();
        } else {
          nsContentUtils::ReportToConsoleNonLocalized(
              u"Silently denying site request for MIDI access because no devices were detected. You may need to restart your browser after connecting a new device."_ns,
              nsIScriptError::infoFlag, "WebMIDI"_ns, mWindow->GetDoc());
          self->CancelWithRandomizedDelay();
        }
      },
      [=](auto) { self->CancelWithRandomizedDelay(); });

  return NS_OK;
}

// If the user has no MIDI devices, we automatically deny the request. To
// prevent sites from using timing attack to discern the existence of MIDI
// devices, we instrument silent denials with a randomized delay between 3
// and 13 seconds, which is intended to model the time the user might spend
// considering a prompt before denying it.
//
// Note that we set the random component of the delay to zero in automation
// to avoid unnecessarily increasing test end-to-end time.
void MIDIPermissionRequest::CancelWithRandomizedDelay() {
  MOZ_ASSERT(NS_IsMainThread());
  uint32_t baseDelayMS = 3 * 1000;
  uint32_t randomDelayMS =
      xpc::IsInAutomation() ? 0 : RandomUint64OrDie() % (10 * 1000);
  auto delay = TimeDuration::FromMilliseconds(baseDelayMS + randomDelayMS);
  RefPtr<MIDIPermissionRequest> self = this;
  NS_NewTimerWithCallback(
      getter_AddRefs(mCancelTimer), [=](auto) { self->Cancel(); }, delay,
      nsITimer::TYPE_ONE_SHOT,
      "MIDIPermissionRequest::CancelWithRandomizedDelay"_ns);
}

nsresult MIDIPermissionRequest::DoPrompt() {
  if (NS_FAILED(nsContentPermissionUtils::AskPermission(this, mWindow))) {
    Cancel();
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}
