/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SerialPermissionRequest.h"

#include "SerialLogging.h"
#include "js/PropertyAndElement.h"
#include "js/Wrapper.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/JSONStringWriteFuncs.h"
#include "mozilla/JSONWriter.h"
#include "mozilla/RandomNum.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/RootedDictionary.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/WindowGlobalParent.h"
#include "nsContentPermissionHelper.h"
#include "nsContentUtils.h"
#include "nsIArray.h"
#include "nsThreadUtils.h"
#include "xpcpublic.h"

namespace mozilla::dom {

constexpr uint32_t kPermissionDeniedBaseDelayMs = 3000;
constexpr uint32_t kPermissionDeniedMaxRandomDelayMs = 10000;

NS_IMPL_ISUPPORTS(SerialPermissionRequest, nsIContentPermissionRequest)

SerialPermissionRequest::SerialPermissionRequest(
    WindowGlobalParent* aWindowGlobalParent, bool aAutoselect,
    nsTArray<IPCSerialPortInfo>&& aPorts)
    : ContentPermissionRequestBaseNoWindow(
          aWindowGlobalParent->DocumentPrincipal(),
          aWindowGlobalParent->TopWindowContext()->DocumentPrincipal(),
          "serial"_ns,
          // The content process already rejects requestPort() without transient
          // user activation (Serial::RequestPort step 3), so the parent-side
          // request can report true without re-querying WindowContext (whose
          // transient- activation state is only tracked in-process and would
          // assert here).
          true,
          // The parent process does not have a permission delegate handler
          // available (that lives on the document) so we conservatively report
          // false.
          false),
      mWindowGlobalParent(aWindowGlobalParent),
      mAutoselect(aAutoselect),
      mPorts(std::move(aPorts)) {
  MOZ_ASSERT(mWindowGlobalParent->DocumentPrincipal());
}

SerialPermissionRequest::~SerialPermissionRequest() {
  // Defensive: if no path settled the promise, fall back to InternalError so
  // the consumer always sees a decision.
  mPromiseHolder.RejectIfExists(RequestPortReason::InternalError, __func__);
}

bool SerialPermissionRequest::IsSitePermAllow() const {
  return nsContentUtils::IsSitePermAllow(Principal(), "serial"_ns);
}

bool SerialPermissionRequest::IsSitePermDeny() const {
  return nsContentUtils::IsSitePermDeny(Principal(), "serial"_ns);
}

bool SerialPermissionRequest::ShouldShowAddonGate() const {
  nsIPrincipal* principal = Principal();
  return StaticPrefs::dom_webserial_gated() &&
         StaticPrefs::dom_sitepermsaddon_provider_enabled() &&
         !IsSitePermAllow() && !principal->GetIsLoopbackHost() &&
         !principal->SchemeIs("file");
}

RefPtr<SerialChooserPromise> SerialPermissionRequest::Run() {
  AssertIsOnMainThread();

  RefPtr<SerialChooserPromise> promise = mPromiseHolder.Ensure(__func__);

  if (!IsSitePermAllow()) {
    if (IsSitePermDeny()) {
      CancelWithRandomizedDelay(RequestPortReason::AddonDenied);
      return promise;
    }
    if (nsContentUtils::IsSitePermDeny(Principal(), "install"_ns) &&
        StaticPrefs::dom_sitepermsaddon_provider_enabled() &&
        !Principal()->GetIsLoopbackHost()) {
      CancelWithRandomizedDelay(RequestPortReason::AddonDenied);
      return promise;
    }
  }

  // Testing-only autoselect bypass: when the testing pref is on and gated
  // mode is off, we skip the prompt entirely and pick the first port. The
  // bypass is only honored when testing is enabled, so a malicious content
  // process cannot use the autoselect flag to skip the chooser in release.
  if (mAutoselect && StaticPrefs::dom_webserial_testing_enabled() &&
      !StaticPrefs::dom_webserial_gated()) {
    if (mPorts.IsEmpty()) {
      ResolveCancelled(RequestPortReason::UserCancelled);
      return promise;
    }
    ResolveWithPort(mPorts[0]);
    return promise;
  }

  if (NS_FAILED(DoPrompt())) {
    ResolveCancelled(RequestPortReason::InternalError);
  }
  return promise;
}

void SerialPermissionRequest::CancelWithRandomizedDelay(
    RequestPortReason aReason) {
  AssertIsOnMainThread();

  uint32_t randomDelayMS =
      xpc::IsInAutomation()
          ? 0
          : RandomUint64OrDie() % kPermissionDeniedMaxRandomDelayMs;
  auto delay = TimeDuration::FromMilliseconds(kPermissionDeniedBaseDelayMs +
                                              randomDelayMS);
  NS_NewTimerWithCallback(
      getter_AddRefs(mCancelTimer),
      [self = RefPtr{this}, aReason](auto) { self->ResolveCancelled(aReason); },
      delay, nsITimer::TYPE_ONE_SHOT,
      "SerialPermissionRequest::CancelWithRandomizedDelay"_ns);
}

nsresult SerialPermissionRequest::DoPrompt() {
  return nsContentPermissionUtils::AskPermission(this, /* aWindow */ nullptr);
}

void SerialPermissionRequest::ResolveWithPort(const IPCSerialPortInfo& aPort) {
  mCancelTimer = nullptr;
  MOZ_LOG(gWebSerialLog, LogLevel::Info,
          ("    friendlyName: %s",
           NS_ConvertUTF16toUTF8(aPort.friendlyName()).get()));
  if (aPort.usbVendorId().isSome()) {
    MOZ_LOG(gWebSerialLog, LogLevel::Info,
            ("    usbVendorId: 0x%04x", aPort.usbVendorId().value()));
  }
  if (aPort.usbProductId().isSome()) {
    MOZ_LOG(gWebSerialLog, LogLevel::Info,
            ("    usbProductId: 0x%04x", aPort.usbProductId().value()));
  }

  mPromiseHolder.ResolveIfExists(aPort, __func__);
}

void SerialPermissionRequest::ResolveCancelled(RequestPortReason aReason) {
  mCancelTimer = nullptr;
  mPromiseHolder.RejectIfExists(aReason, __func__);
}

void SerialPermissionRequest::GetPermissionOptions(
    nsTArray<nsString>& aOptions) const {
  MOZ_LOG(
      gWebSerialLog, LogLevel::Info,
      ("SerialPermissionRequest::GetPermissionOptions called with %zu ports",
       mPorts.Length()));

  // Pass the autoselect flag to the JS UI so that it can auto-confirm the
  // chooser when testing+gated is in use.
  if (mAutoselect && StaticPrefs::dom_webserial_testing_enabled()) {
    aOptions.AppendElement(u"{\"__autoselect__\":true}"_ns);
  }

  size_t index = 0;
  for (const auto& port : mPorts) {
    nsCString utf8Json;
    JSONStringRefWriteFunc writeFunc(utf8Json);
    JSONWriter writer(writeFunc, JSONWriter::SingleLineStyle);

    NS_ConvertUTF16toUTF8 utf8Path(port.path());
    NS_ConvertUTF16toUTF8 utf8FriendlyName(port.friendlyName());

    MOZ_LOG(gWebSerialLog, LogLevel::Debug,
            ("  Port[%zu]: path=%s, friendlyName=%s, vid=0x%04x, pid=0x%04x",
             index, utf8Path.get(), utf8FriendlyName.get(),
             port.usbVendorId().valueOr(0), port.usbProductId().valueOr(0)));

    writer.StartObjectElement();
    writer.StringProperty("path", MakeStringSpan(utf8Path.get()));
    writer.StringProperty("friendlyName",
                          MakeStringSpan(utf8FriendlyName.get()));
    writer.IntProperty("usbVendorId", port.usbVendorId().valueOr(0));
    writer.IntProperty("usbProductId", port.usbProductId().valueOr(0));
    writer.EndObject();

    nsString utf16Json;
    CopyUTF8toUTF16(utf8Json, utf16Json);
    aOptions.AppendElement(std::move(utf16Json));
    ++index;
  }
}

NS_IMETHODIMP
SerialPermissionRequest::GetElement(Element** aElement) {
  NS_ENSURE_ARG_POINTER(aElement);
  RefPtr<Element> element = mWindowGlobalParent->GetRootOwnerElement();
  element.forget(aElement);
  return NS_OK;
}

NS_IMETHODIMP
SerialPermissionRequest::Cancel() {
  AssertIsOnMainThread();
  mCancelTimer = nullptr;

  RequestPortReason reason = RequestPortReason::UserCancelled;
  if (ShouldShowAddonGate()) {
    reason = RequestPortReason::AddonDenied;
  }

  ResolveCancelled(reason);
  return NS_OK;
}

NS_IMETHODIMP
SerialPermissionRequest::Allow(JS::Handle<JS::Value> aChoices) {
  AssertIsOnMainThread();
  mCancelTimer = nullptr;

  if (!aChoices.isObject()) {
    ResolveCancelled(RequestPortReason::InternalError);
    return NS_OK;
  }

  // The choices object can come in any realm — chrome calls Allow() from JS.
  // Unwrap and enter its realm before reading properties.
  JS::Rooted<JSObject*> obj(RootingCx(), &aChoices.toObject());
  obj = js::CheckedUnwrapStatic(obj);
  if (!obj) {
    ResolveCancelled(RequestPortReason::InternalError);
    return NS_OK;
  }

  nsIGlobalObject* global = xpc::NativeGlobal(obj);
  if (!global) {
    ResolveCancelled(RequestPortReason::InternalError);
    return NS_OK;
  }

  AutoJSAPI jsapi;
  if (!jsapi.Init(global)) {
    ResolveCancelled(RequestPortReason::InternalError);
    return NS_OK;
  }
  JSContext* cx = jsapi.cx();

  JS::Rooted<JS::Value> serialVal(cx);
  if (!JS_GetProperty(cx, obj, "serial", &serialVal) || !serialVal.isString()) {
    ResolveCancelled(RequestPortReason::InternalError);
    return NS_OK;
  }

  nsAutoJSString choice;
  if (!choice.init(cx, serialVal)) {
    ResolveCancelled(RequestPortReason::InternalError);
    return NS_OK;
  }

  nsresult rv;
  int32_t selectedIndex = choice.ToInteger(&rv);
  if (NS_FAILED(rv) || selectedIndex < 0 ||
      static_cast<size_t>(selectedIndex) >= mPorts.Length()) {
    ResolveCancelled(RequestPortReason::InternalError);
    return NS_OK;
  }

  MOZ_LOG(gWebSerialLog, LogLevel::Info,
          ("  Selected port at index %d:", selectedIndex));
  ResolveWithPort(mPorts[selectedIndex]);
  return NS_OK;
}

}  // namespace mozilla::dom
