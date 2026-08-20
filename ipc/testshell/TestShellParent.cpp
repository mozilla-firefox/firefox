/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TestShellParent.h"

/* This must occur *after* TestShellParent.h to avoid typedefs conflicts. */
#include "jsfriendapi.h"
#include "js/CallAndConstruct.h"  // JS_CallFunctionValue

#include "mozilla/dom/AutoEntryScript.h"

using namespace mozilla;
using mozilla::ipc::PTestShellCommandParent;
using mozilla::ipc::TestShellCommandParent;
using mozilla::ipc::TestShellParent;

void TestShellParent::ActorDestroy(ActorDestroyReason aWhy) {
  // Implement me! Bug 1005177
}

PTestShellCommandParent* TestShellParent::AllocPTestShellCommandParent(
    const nsAString& aCommand) {
  return new TestShellCommandParent();
}

bool TestShellParent::DeallocPTestShellCommandParent(
    PTestShellCommandParent* aActor) {
  delete aActor;
  return true;
}

bool TestShellParent::CommandDone(TestShellCommandParent* command,
                                  const nsAString& aResponse) {
  // XXX what should happen if the callback fails?
  /*bool ok = */ command->RunCallback(aResponse);
  command->ReleaseCallback();

  return true;
}

bool TestShellCommandParent::SetCallback(JSContext* aCx,
                                         const JS::Value& aCallback) {
  if (!mCallback.initialized()) {
    mCallback.init(aCx, aCallback);
    return true;
  }

  mCallback = aCallback;

  return true;
}

bool TestShellCommandParent::RunCallback(const nsAString& aResponse) {
  NS_ENSURE_TRUE(mCallback.isObject(), false);

  MOZ_RELEASE_ASSERT(js::IsFunctionObject(&mCallback.toObject()));

  // We're about to run script via JS_CallFunctionValue, so we need an
  // AutoEntryScript. This is just for testing and not in any spec.
  dom::AutoEntryScript aes(&mCallback.toObject(), "TestShellCommand");
  JSContext* cx = aes.cx();
  JS::Rooted<JSObject*> global(cx, JS::CurrentGlobalOrNull(cx));

  JSString* str =
      JS_NewUCStringCopyN(cx, aResponse.BeginReading(), aResponse.Length());
  NS_ENSURE_TRUE(str, false);

  JS::Rooted<JS::Value> strVal(cx, JS::StringValue(str));

  JS::Rooted<JS::Value> rval(cx);
  JS::Rooted<JS::Value> callback(cx, mCallback);
  bool ok = JS_CallFunctionValue(cx, global, callback,
                                 JS::HandleValueArray(strVal), &rval);
  NS_ENSURE_TRUE(ok, false);

  return true;
}

void TestShellCommandParent::ReleaseCallback() { mCallback.reset(); }

bool TestShellCommandParent::ExecuteCallback(const nsAString& aResponse) {
  return mozilla::ipc::ActorCast<TestShellParent>(Manager())->CommandDone(
      this, aResponse);
}

void TestShellCommandParent::ActorDestroy(ActorDestroyReason why) {
  if (why == AbnormalShutdown) {
    ExecuteCallback(u""_ns);
  }
}
