/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ConsoleUtils.h"

#include "ConsoleCommon.h"
#include "js/PropertyAndElement.h"  // JS_DefineProperty
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/NullPrincipal.h"
#include "mozilla/dom/ConsoleBinding.h"
#include "mozilla/dom/ConsoleInstanceBinding.h"
#include "mozilla/dom/RootedDictionary.h"
#include "mozilla/dom/ScriptSettings.h"
#include "nsContentUtils.h"
#include "nsIConsoleAPIStorage.h"
#include "nsIXPConnect.h"
#include "nsServiceManagerUtils.h"

namespace mozilla::dom {

namespace {

StaticRefPtr<ConsoleUtils> gConsoleUtilsService;

}

/* static */
ConsoleUtils* ConsoleUtils::GetOrCreate() {
  if (!gConsoleUtilsService) {
    MOZ_ASSERT(NS_IsMainThread());

    gConsoleUtilsService = new ConsoleUtils();
    ClearOnShutdown(&gConsoleUtilsService);
  }

  return gConsoleUtilsService;
}

ConsoleUtils::ConsoleUtils() = default;
ConsoleUtils::~ConsoleUtils() = default;

/* static */
void ConsoleUtils::ReportForServiceWorkerScope(const nsAString& aScope,
                                               const nsAString& aMessage,
                                               const nsACString& aFilename,
                                               uint32_t aLineNumber,
                                               uint32_t aColumnNumber,
                                               Level aLevel) {
  MOZ_RELEASE_ASSERT(NS_IsMainThread());

  RefPtr<ConsoleUtils> service = ConsoleUtils::GetOrCreate();
  if (NS_WARN_IF(!service)) {
    return;
  }

  service->ReportForServiceWorkerScopeInternal(
      aScope, aMessage, aFilename, aLineNumber, aColumnNumber, aLevel);
}

void ConsoleUtils::ReportForServiceWorkerScopeInternal(
    const nsAString& aScope, const nsAString& aMessage,
    const nsACString& aFilename, uint32_t aLineNumber, uint32_t aColumnNumber,
    Level aLevel) {
  MOZ_ASSERT(NS_IsMainThread());

  AutoJSAPI jsapi;
  jsapi.Init();

  JSContext* cx = jsapi.cx();

  ConsoleCommon::ClearException ce(cx);
  JS::Rooted<JSObject*> global(cx, GetOrCreateSandbox(cx));
  if (NS_WARN_IF(!global)) {
    return;
  }

  // The GetOrCreateSandbox call returns a proxy to the actual sandbox object.
  // We don't need a proxy here.
  global = js::UncheckedUnwrap(global);

  JSAutoRealm ar(cx, global);

  RootedDictionary<ConsoleEvent> event(cx);

  event.mID.Construct();
  event.mID.Value().SetAsString() = aScope;

  event.mInnerID.Construct();
  event.mInnerID.Value().SetAsString() = u"ServiceWorker"_ns;

  switch (aLevel) {
    case eLog:
      event.mLevel = u"log"_ns;
      break;

    case eWarning:
      event.mLevel = u"warn"_ns;
      break;

    case eError:
      event.mLevel = u"error"_ns;
      break;
  }

  event.mFilename = aFilename;
  event.mLineNumber = aLineNumber;
  event.mColumnNumber = aColumnNumber;
  event.mTimeStamp = JS_Now() / PR_USEC_PER_MSEC;
  event.mMicroSecondTimeStamp = JS_Now();

  JS::Rooted<JS::Value> messageValue(cx);
  if (!dom::ToJSValue(cx, aMessage, &messageValue)) {
    return;
  }

  event.mArguments.Construct();
  if (!event.mArguments.Value().AppendElement(messageValue, fallible)) {
    return;
  }

  nsCOMPtr<nsIConsoleAPIStorage> storage =
      do_GetService("@mozilla.org/consoleAPI-storage;1");

  if (NS_WARN_IF(!storage)) {
    return;
  }

  JS::Rooted<JS::Value> eventValue(cx);
  if (!ToJSValue(cx, event, &eventValue)) {
    return;
  }

  // This is a legacy property.
  JS::Rooted<JSObject*> eventObj(cx, &eventValue.toObject());
  if (NS_WARN_IF(!JS_DefineProperty(cx, eventObj, "wrappedJSObject", eventObj,
                                    JSPROP_ENUMERATE))) {
    return;
  }

  storage->RecordEvent(u"ServiceWorker"_ns, eventValue);
}

JSObject* ConsoleUtils::GetOrCreateSandbox(JSContext* aCx) {
  AssertIsOnMainThread();

  if (!mSandbox) {
    nsIXPConnect* xpc = nsContentUtils::XPConnect();
    MOZ_ASSERT(xpc, "This should never be null!");

    RefPtr<NullPrincipal> nullPrincipal =
        NullPrincipal::CreateWithoutOriginAttributes();

    JS::Rooted<JSObject*> sandbox(aCx);
    nsresult rv = xpc->CreateSandbox(aCx, nullPrincipal, sandbox.address());
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return nullptr;
    }

    mSandbox = new JSObjectHolder(aCx, sandbox);
  }

  return mSandbox->GetJSObject();
}

}  // namespace mozilla::dom
