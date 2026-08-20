/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"
#include "js/Value.h"
#include "jsapi.h"
#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/UnderlyingSinkCallbackHelpers.h"
#include "mozilla/dom/WebTransportError.h"
#include "nsError.h"
#include "xpcpublic.h"

using namespace mozilla;
using namespace mozilla::dom;

namespace {

// Exposes the protected BuildErrorStatus() so tests can exercise the default
// abort-reason mapping of WritableStreamToOutputAlgorithms directly.
class DefaultOutputAlgorithms final : public WritableStreamToOutputAlgorithms {
 public:
  DefaultOutputAlgorithms()
      : WritableStreamToOutputAlgorithms(nullptr, nullptr) {}

  using WritableStreamToOutputAlgorithms::BuildErrorStatus;

 protected:
  ~DefaultOutputAlgorithms() override = default;
};

// Overrides BuildErrorStatus() the way a real subclass (e.g. WebSerial) would,
// to confirm the extracted virtual is actually consulted.
class OverridingOutputAlgorithms final
    : public WritableStreamToOutputAlgorithms {
 public:
  OverridingOutputAlgorithms()
      : WritableStreamToOutputAlgorithms(nullptr, nullptr) {}

  nsresult BuildErrorStatus(JSContext*,
                            const Optional<JS::Handle<JS::Value>>&) override {
    return NS_ERROR_DOM_SERIAL_PARITY_ERROR;
  }

  nsresult CallBuildErrorStatus(
      JSContext* aCx, const Optional<JS::Handle<JS::Value>>& aReason) {
    return BuildErrorStatus(aCx, aReason);
  }

 protected:
  ~OverridingOutputAlgorithms() override = default;
};

}  // namespace

// With no abort reason the default mapping falls back to the generic
// WebTransport code.
TEST(WritableStreamToOutput, BuildErrorStatusNoReason)
{
  RefPtr<DefaultOutputAlgorithms> algorithms = new DefaultOutputAlgorithms();
  Optional<JS::Handle<JS::Value>> noReason;
  EXPECT_EQ(algorithms->BuildErrorStatus(nullptr, noReason),
            NS_ERROR_WEBTRANSPORT_CODE_BASE);
}

// A non-object reason is not a WebTransportError, so it also falls back.
TEST(WritableStreamToOutput, BuildErrorStatusNonObjectReason)
{
  AutoJSAPI jsapi;
  MOZ_ALWAYS_TRUE(jsapi.Init(xpc::PrivilegedJunkScope()));
  JSContext* cx = jsapi.cx();

  RefPtr<DefaultOutputAlgorithms> algorithms = new DefaultOutputAlgorithms();
  Optional<JS::Handle<JS::Value>> reason(cx, JS::Int32Value(42));
  EXPECT_EQ(algorithms->BuildErrorStatus(cx, reason),
            NS_ERROR_WEBTRANSPORT_CODE_BASE);
}

// A plain object reason (that does not unwrap to a WebTransportError) falls
// back to the generic code as well.
TEST(WritableStreamToOutput, BuildErrorStatusPlainObjectReason)
{
  AutoJSAPI jsapi;
  MOZ_ALWAYS_TRUE(jsapi.Init(xpc::PrivilegedJunkScope()));
  JSContext* cx = jsapi.cx();
  JS::Rooted<JSObject*> obj(cx, JS_NewPlainObject(cx));
  ASSERT_TRUE(obj);

  RefPtr<DefaultOutputAlgorithms> algorithms = new DefaultOutputAlgorithms();
  Optional<JS::Handle<JS::Value>> reason(cx, JS::ObjectValue(*obj));
  EXPECT_EQ(algorithms->BuildErrorStatus(cx, reason),
            NS_ERROR_WEBTRANSPORT_CODE_BASE);
}

// A WebTransportError reason with no streamErrorCode (e.g. a session-level
// error) must not crash trying to unwrap a null Nullable<uint8_t>. See
// bug 1991126.
TEST(WritableStreamToOutput, BuildErrorStatusNullStreamErrorCode)
{
  AutoJSAPI jsapi;
  MOZ_ALWAYS_TRUE(jsapi.Init(xpc::PrivilegedJunkScope()));
  JSContext* cx = jsapi.cx();

  RefPtr<WebTransportError> error = new WebTransportError(
      "session error"_ns, WebTransportErrorSource::Session);
  JS::Rooted<JSObject*> obj(cx, error->WrapObject(cx, nullptr));
  ASSERT_TRUE(obj);

  RefPtr<DefaultOutputAlgorithms> algorithms = new DefaultOutputAlgorithms();
  Optional<JS::Handle<JS::Value>> reason(cx, JS::ObjectValue(*obj));
  EXPECT_EQ(algorithms->BuildErrorStatus(cx, reason),
            NS_ERROR_WEBTRANSPORT_CODE_BASE);
}

// A subclass can substitute its own status mapping.
TEST(WritableStreamToOutput, BuildErrorStatusSubclassOverride)
{
  RefPtr<OverridingOutputAlgorithms> algorithms =
      new OverridingOutputAlgorithms();
  Optional<JS::Handle<JS::Value>> noReason;
  EXPECT_EQ(algorithms->CallBuildErrorStatus(nullptr, noReason),
            NS_ERROR_DOM_SERIAL_PARITY_ERROR);
}
