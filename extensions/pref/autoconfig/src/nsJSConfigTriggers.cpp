/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsJSConfigTriggers.h"

#include "jsapi.h"
#include "nsIXPConnect.h"
#include "nsCOMPtr.h"
#include "nsString.h"
#include "nspr.h"
#include "mozilla/NullPrincipal.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/ChromeUtilsBinding.h"
#include "nsContentUtils.h"
#include "nsJSPrincipals.h"
#include "nsIScriptError.h"
#include "js/PropertyAndElement.h"  // JS_DefineProperty
#include "js/Wrapper.h"
#include "mozilla/Utf8.h"

extern mozilla::LazyLogModule MCD;
using mozilla::AutoSafeJSContext;
using mozilla::IsUtf8;
using mozilla::NullPrincipal;
using mozilla::dom::AutoJSAPI;

//*****************************************************************************

constinit static JS::PersistentRooted<JSObject*> autoconfigSystemSb;
constinit static JS::PersistentRooted<JSObject*> autoconfigSb;
bool sandboxEnabled;

nsresult CentralizedAdminPrefManagerInit(bool aSandboxEnabled) {
  // If the sandbox is already created, no need to create it again.
  if (autoconfigSb.initialized()) return NS_OK;

  sandboxEnabled = aSandboxEnabled;

  // Grab XPConnect.
  nsCOMPtr<nsIXPConnect> xpc = nsIXPConnect::XPConnect();

  // Grab the system principal.
  nsCOMPtr<nsIPrincipal> principal;
  nsContentUtils::GetSecurityManager()->GetSystemPrincipal(
      getter_AddRefs(principal));

  // Create a sandbox.
  AutoSafeJSContext cx;
  JS::Rooted<JSObject*> sandbox(cx);
  nsresult rv = xpc->CreateSandbox(cx, principal, sandbox.address());
  NS_ENSURE_SUCCESS(rv, rv);

  // Unwrap, store and root the sandbox.
  NS_ENSURE_STATE(sandbox);
  autoconfigSystemSb.init(cx, js::UncheckedUnwrap(sandbox));

  // Create an unprivileged sandbox.
  principal = NullPrincipal::CreateWithoutOriginAttributes();
  rv = xpc->CreateSandbox(cx, principal, sandbox.address());
  NS_ENSURE_SUCCESS(rv, rv);

  autoconfigSb.init(cx, js::UncheckedUnwrap(sandbox));

  // Define gSandbox on system sandbox.
  JSAutoRealm ar(cx, autoconfigSystemSb);

  JS::Rooted<JS::Value> value(cx, JS::ObjectValue(*sandbox));

  if (!JS_WrapValue(cx, &value) ||
      !JS_DefineProperty(cx, autoconfigSystemSb, "gSandbox", value,
                         JSPROP_ENUMERATE)) {
    return NS_ERROR_FAILURE;
  }

  // Define ChromeUtils for ChromeUtils.import.
  if (!mozilla::dom::ChromeUtils_Binding::CreateAndDefineOnGlobal(cx)) {
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

nsresult CentralizedAdminPrefManagerFinish() {
  if (autoconfigSb.initialized()) {
    AutoSafeJSContext cx;
    autoconfigSb.reset();
    autoconfigSystemSb.reset();
    JS_MaybeGC(cx);
  }
  return NS_OK;
}

nsresult EvaluateAdminConfigScript(const char* js_buffer, size_t length,
                                   const char* filename, bool globalContext,
                                   bool callbacks, bool skipFirstLine,
                                   bool isPrivileged) {
  if (!sandboxEnabled) {
    isPrivileged = true;
  }
  return EvaluateAdminConfigScript(
      isPrivileged ? autoconfigSystemSb : autoconfigSb, js_buffer, length,
      filename, globalContext, callbacks, skipFirstLine);
}

nsresult EvaluateAdminConfigScript(JS::Handle<JSObject*> sandbox,
                                   const char* js_buffer, size_t length,
                                   const char* filename, bool /*globalContext*/,
                                   bool /*callbacks*/, bool skipFirstLine) {
  if (skipFirstLine) {
    /* In order to protect the privacy of the JavaScript preferences file
     * from loading by the browser, we make the first line unparseable
     * by JavaScript. We must skip that line here before executing
     * the JavaScript code.
     */
    unsigned int i = 0;
    while (i < length) {
      char c = js_buffer[i++];
      if (c == '\r') {
        if (js_buffer[i] == '\n') i++;
        break;
      }
      if (c == '\n') break;
    }

    length -= i;
    js_buffer += i;
  }

  // Grab XPConnect.
  nsCOMPtr<nsIXPConnect> xpc = nsIXPConnect::XPConnect();

  AutoJSAPI jsapi;
  if (!jsapi.Init(sandbox)) {
    return NS_ERROR_UNEXPECTED;
  }
  JSContext* cx = jsapi.cx();

  nsAutoCString script(js_buffer, length);
  JS::Rooted<JS::Value> v(cx);

  nsString convertedScript;
  bool isUTF8 = IsUtf8(script);
  if (isUTF8) {
    CopyUTF8toUTF16(script, convertedScript);
  } else {
    nsContentUtils::ReportToConsoleNonLocalized(
        nsLiteralString(
            u"Your AutoConfig file is ASCII. Please convert it to UTF-8."),
        nsIScriptError::warningFlag, "autoconfig"_ns, nullptr);
    /* If the length is 0, the conversion failed. Fallback to ASCII */
    convertedScript = NS_ConvertASCIItoUTF16(script);
  }
  {
    JSAutoRealm ar(cx, autoconfigSystemSb);
    JS::Rooted<JS::Value> value(cx, JS::BooleanValue(isUTF8));
    if (!JS_DefineProperty(cx, autoconfigSystemSb, "gIsUTF8", value,
                           JSPROP_ENUMERATE)) {
      return NS_ERROR_UNEXPECTED;
    }
  }
  nsresult rv =
      xpc->EvalInSandboxObject(convertedScript, filename, cx, sandbox, &v);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}
