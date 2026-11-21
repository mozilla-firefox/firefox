/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jsapi-tests/tests.h"

#include "builtin/TestingFunctions.h"
#include "js/ArrayBuffer.h"  // JS::{IsArrayBufferObject,GetArrayBufferLengthAndData,NewExternalArrayBuffer}
#include "js/GlobalObject.h"        // JS_NewGlobalObject
#include "js/PropertyAndElement.h"  // JS_GetProperty, JS_SetProperty
#include "js/StructuredClone.h"

using namespace js;

#ifdef DEBUG
// Skip test, since it will abort with an assert in buf->Init(7).
#else
BEGIN_TEST(testStructuredClone_invalidLength) {
  auto buf = js::MakeUnique<JSStructuredCloneData>(
      JS::StructuredCloneScope::DifferentProcess);
  CHECK(buf);
  CHECK(buf->Init(7));
  RootedValue clone(cx);
  JS::CloneDataPolicy policy;
  CHECK(!JS_ReadStructuredClone(cx, *buf,
                                JS::StructuredCloneScope::DifferentProcess,
                                &clone, policy, nullptr, nullptr));
  return true;
}
END_TEST(testStructuredClone_invalidLength)
#endif

BEGIN_TEST(testStructuredClone_object) {
  JS::RootedObject g1(cx, createGlobal());
  JS::RootedObject g2(cx, createGlobal());
  CHECK(g1);
  CHECK(g2);

  JS::RootedValue v1(cx);

  {
    JSAutoRealm ar(cx, g1);
    JS::RootedValue prop(cx, JS::Int32Value(1337));

    JS::RootedObject obj(cx, JS_NewPlainObject(cx));
    v1 = JS::ObjectOrNullValue(obj);
    CHECK(v1.isObject());
    CHECK(JS_SetProperty(cx, obj, "prop", prop));
  }

  {
    JSAutoRealm ar(cx, g2);
    JS::RootedValue v2(cx);

    CHECK(JS_StructuredClone(cx, v1, &v2, nullptr, nullptr));
    CHECK(v2.isObject());
    JS::RootedObject obj(cx, &v2.toObject());

    JS::RootedValue prop(cx);
    CHECK(JS_GetProperty(cx, obj, "prop", &prop));
    CHECK(prop.isInt32());
    CHECK(&v1.toObject() != obj);
    CHECK_EQUAL(prop.toInt32(), 1337);
  }

  return true;
}
END_TEST(testStructuredClone_object)

BEGIN_TEST(testStructuredClone_string) {
  JS::RootedObject g1(cx, createGlobal());
  JS::RootedObject g2(cx, createGlobal());
  CHECK(g1);
  CHECK(g2);

  JS::RootedValue v1(cx);

  {
    JSAutoRealm ar(cx, g1);
    JS::RootedValue prop(cx, JS::Int32Value(1337));

    v1 = JS::StringValue(JS_NewStringCopyZ(cx, "Hello World!"));
    CHECK(v1.isString());
    CHECK(v1.toString());
  }

  {
    JSAutoRealm ar(cx, g2);
    JS::RootedValue v2(cx);

    CHECK(JS_StructuredClone(cx, v1, &v2, nullptr, nullptr));
    CHECK(v2.isString());
    CHECK(v2.toString());

    JS::RootedValue expected(
        cx, JS::StringValue(JS_NewStringCopyZ(cx, "Hello World!")));
    CHECK_SAME(v2, expected);
  }

  return true;
}
END_TEST(testStructuredClone_string)

BEGIN_TEST(testStructuredClone_externalArrayBuffer) {
  ExternalData data("One two three four");
  auto dataPointer = data.pointer();
  JS::RootedObject g1(cx, createGlobal());
  JS::RootedObject g2(cx, createGlobal());
  CHECK(g1);
  CHECK(g2);

  JS::RootedValue v1(cx);

  {
    JSAutoRealm ar(cx, g1);

    JS::RootedObject obj(
        cx, JS::NewExternalArrayBuffer(cx, data.len(), std::move(dataPointer)));
    CHECK(!data.wasFreed());

    v1 = JS::ObjectOrNullValue(obj);
    CHECK(v1.isObject());
  }

  {
    JSAutoRealm ar(cx, g2);
    JS::RootedValue v2(cx);

    CHECK(JS_StructuredClone(cx, v1, &v2, nullptr, nullptr));
    CHECK(v2.isObject());

    JS::RootedObject obj(cx, &v2.toObject());
    CHECK(&v1.toObject() != obj);

    size_t len;
    bool isShared;
    uint8_t* clonedData;
    JS::GetArrayBufferLengthAndData(obj, &len, &isShared, &clonedData);

    // The contents of the two array buffers should be equal, but not the
    // same pointer.
    CHECK_EQUAL(len, data.len());
    CHECK(clonedData != data.contents());
    CHECK(strcmp(reinterpret_cast<char*>(clonedData), data.asString()) == 0);
    CHECK(!data.wasFreed());
  }

  // GC the array buffer before data goes out of scope
  v1.setNull();
  JS_GC(cx);
  JS_GC(cx);  // Trigger another to wait for background finalization to end

  CHECK(data.wasFreed());

  return true;
}
END_TEST(testStructuredClone_externalArrayBuffer)

BEGIN_TEST(testStructuredClone_externalArrayBufferDifferentThreadOrProcess) {
  CHECK(testStructuredCloneCopy(JS::StructuredCloneScope::SameProcess));
  CHECK(testStructuredCloneCopy(JS::StructuredCloneScope::DifferentProcess));
  return true;
}

bool testStructuredCloneCopy(JS::StructuredCloneScope scope) {
  ExternalData data("One two three four");
  auto dataPointer = data.pointer();
  JS::RootedObject buffer(
      cx, JS::NewExternalArrayBuffer(cx, data.len(), std::move(dataPointer)));
  CHECK(buffer);
  CHECK(!data.wasFreed());

  JS::RootedValue v1(cx, JS::ObjectValue(*buffer));
  JS::RootedValue v2(cx);
  CHECK(clone(scope, v1, &v2));
  JS::RootedObject bufferOut(cx, v2.toObjectOrNull());
  CHECK(bufferOut);
  CHECK(JS::IsArrayBufferObject(bufferOut));

  size_t len;
  bool isShared;
  uint8_t* clonedData;
  JS::GetArrayBufferLengthAndData(bufferOut, &len, &isShared, &clonedData);

  // Cloning should copy the data, so the contents of the two array buffers
  // should be equal, but not the same pointer.
  CHECK_EQUAL(len, data.len());
  CHECK(clonedData != data.contents());
  CHECK(strcmp(reinterpret_cast<char*>(clonedData), data.asString()) == 0);
  CHECK(!data.wasFreed());

  buffer = nullptr;
  bufferOut = nullptr;
  v1.setNull();
  v2.setNull();
  JS_GC(cx);
  JS_GC(cx);
  CHECK(data.wasFreed());

  return true;
}

bool clone(JS::StructuredCloneScope scope, JS::HandleValue v1,
           JS::MutableHandleValue v2) {
  JSStructuredCloneData data(scope);
  CHECK(JS_WriteStructuredClone(cx, v1, &data, scope, JS::CloneDataPolicy(),
                                nullptr, nullptr, JS::UndefinedHandleValue));
  CHECK(JS_ReadStructuredClone(cx, data, scope, v2, JS::CloneDataPolicy(),
                               nullptr, nullptr));
  return true;
}
END_TEST(testStructuredClone_externalArrayBufferDifferentThreadOrProcess)

struct StructuredCloneTestPrincipals final : public JSPrincipals {
  uint32_t rank;

  explicit StructuredCloneTestPrincipals(uint32_t rank, int32_t rc = 1)
      : rank(rank) {
    this->refcount = rc;
  }

  bool write(JSContext* cx, JSStructuredCloneWriter* writer) override {
    return JS_WriteUint32Pair(writer, rank, 0);
  }

  bool isSystemPrincipal() override { return true; }
  bool isAddonPrincipal() override { return true; }

  static bool read(JSContext* cx, JSStructuredCloneReader* reader,
                   JSPrincipals** outPrincipals) {
    uint32_t rank;
    uint32_t unused;
    if (!JS_ReadUint32Pair(reader, &rank, &unused)) {
      return false;
    }

    *outPrincipals = new StructuredCloneTestPrincipals(rank);
    return !!*outPrincipals;
  }

  static void destroy(JSPrincipals* p) {
    auto p1 = static_cast<StructuredCloneTestPrincipals*>(p);
    delete p1;
  }

  static uint32_t getRank(JSPrincipals* p) {
    if (!p) {
      return 0;
    }
    return static_cast<StructuredCloneTestPrincipals*>(p)->rank;
  }

  static bool subsumes(JSPrincipals* a, JSPrincipals* b) {
    return getRank(a) > getRank(b);
  }

  static JSSecurityCallbacks securityCallbacks;

  static StructuredCloneTestPrincipals testPrincipals;
};

JSSecurityCallbacks StructuredCloneTestPrincipals::securityCallbacks = {
    nullptr,  // contentSecurityPolicyAllows
    nullptr,  // codeForEvalGets
    subsumes};

BEGIN_TEST(testStructuredClone_SavedFrame) {
  JS_SetSecurityCallbacks(cx,
                          &StructuredCloneTestPrincipals::securityCallbacks);
  JS_InitDestroyPrincipalsCallback(cx, StructuredCloneTestPrincipals::destroy);
  JS_InitReadPrincipalsCallback(cx, StructuredCloneTestPrincipals::read);

  auto testPrincipals = new StructuredCloneTestPrincipals(42, 0);
  CHECK(testPrincipals);

  auto DONE = (JSPrincipals*)0xDEADBEEF;

  struct {
    const char* name;
    JSPrincipals* principals;
  } principalsToTest[] = {
      {"IsSystem", &js::ReconstructedSavedFramePrincipals::IsSystem},
      {"IsNotSystem", &js::ReconstructedSavedFramePrincipals::IsNotSystem},
      {"testPrincipals", testPrincipals},
      {"nullptr principals", nullptr},
      {"DONE", DONE}};

  const char* FILENAME = "filename.js";

  for (auto* pp = principalsToTest; pp->principals != DONE; pp++) {
    fprintf(stderr, "Testing with principals '%s'\n", pp->name);

    JS::RealmOptions options;
    JS::RootedObject g(cx,
                       JS_NewGlobalObject(cx, getGlobalClass(), pp->principals,
                                          JS::FireOnNewGlobalHook, options));
    CHECK(g);
    JSAutoRealm ar(cx, g);

    CHECK(js::DefineTestingFunctions(cx, g, false, false));

    JS::RootedValue srcVal(cx);
    CHECK(
        evaluate("(function one() {                      \n"   // 1
                 "  return (function two() {             \n"   // 2
                 "    return (function three() {         \n"   // 3
                 "      return saveStack();              \n"   // 4
                 "    }());                              \n"   // 5
                 "  }());                                \n"   // 6
                 "}());                                  \n",  // 7
                 FILENAME, 1, &srcVal));

    CHECK(srcVal.isObject());
    JS::RootedObject srcObj(cx, &srcVal.toObject());

    CHECK(srcObj->is<js::SavedFrame>());
    JS::Rooted<js::SavedFrame*> srcFrame(cx, &srcObj->as<js::SavedFrame>());

    CHECK(srcFrame->getPrincipals() == pp->principals);

    JS::RootedValue destVal(cx);
    CHECK(JS_StructuredClone(cx, srcVal, &destVal, nullptr, nullptr));

    CHECK(destVal.isObject());
    JS::RootedObject destObj(cx, &destVal.toObject());

    CHECK(destObj->is<js::SavedFrame>());
    JS::Handle<js::SavedFrame*> destFrame = destObj.as<js::SavedFrame>();

    size_t framesCopied = 0;
    for (JS::Handle<js::SavedFrame*> f :
         js::SavedFrame::RootedRange(cx, destFrame)) {
      framesCopied++;

      CHECK(f != srcFrame);

      if (pp->principals == testPrincipals) {
        // We shouldn't get a pointer to the same
        // StructuredCloneTestPrincipals instance since we should have
        // serialized and then deserialized it into a new instance.
        CHECK(f->getPrincipals() != pp->principals);

        // But it should certainly have the same rank.
        CHECK(StructuredCloneTestPrincipals::getRank(f->getPrincipals()) ==
              StructuredCloneTestPrincipals::getRank(pp->principals));
      } else {
        // For our singleton principals, we should always get the same
        // pointer back.
        CHECK(js::ReconstructedSavedFramePrincipals::is(pp->principals) ||
              pp->principals == nullptr);
        CHECK(f->getPrincipals() == pp->principals);
      }

      CHECK(EqualStrings(f->getSource(), srcFrame->getSource()));
      CHECK(f->getLine() == srcFrame->getLine());
      CHECK(f->getColumn() == srcFrame->getColumn());
      CHECK(EqualStrings(f->getFunctionDisplayName(),
                         srcFrame->getFunctionDisplayName()));

      srcFrame = srcFrame->getParent();
    }

    // Four function frames + one global frame.
    CHECK(framesCopied == 4);
  }

  return true;
}
END_TEST(testStructuredClone_SavedFrame)

BEGIN_TEST(testStructuredClone_peekUint32Pair) {
  static const JSClass PeekTestClass = {"PeekTestObject", 0};

  JSStructuredCloneCallbacks callbacks = {};

  callbacks.write = [](JSContext* cx, JSStructuredCloneWriter* w,
                       JS::HandleObject obj, bool* sameProcessScopeRequired,
                       void* closure) {
    return JS_WriteUint32Pair(w, JS_SCTAG_USER_MIN, 0) &&
           JS_WriteUint32Pair(w, 1, 2) && JS_WriteUint32Pair(w, 3, 4);
  };

  // A mismatch should return nullptr making JS_ReadStructuredClone fail.
  callbacks.read = [](JSContext* cx, JSStructuredCloneReader* r,
                      const JS::CloneDataPolicy& policy, uint32_t tag,
                      uint32_t data, void* closure) -> JSObject* {
    uint32_t a, b;

    if (!JS_PeekUint32Pair(r, &a, &b) || a != 1 || b != 2) {
      return nullptr;
    }

    if (!JS_PeekUint32Pair(r, &a, &b) || a != 1 || b != 2) {
      return nullptr;
    }

    if (!JS_ReadUint32Pair(r, &a, &b) || a != 1 || b != 2) {
      return nullptr;
    }
    if (!JS_PeekUint32Pair(r, &a, &b) || a != 3 || b != 4) {
      return nullptr;
    }
    if (!JS_ReadUint32Pair(r, &a, &b) || a != 3 || b != 4) {
      return nullptr;
    }

    return JS_NewPlainObject(cx);
  };

  const auto scope = JS::StructuredCloneScope::DifferentProcess;

  JS::RootedObject obj(cx, JS_NewObject(cx, &PeekTestClass));
  CHECK(obj);
  JS::RootedValue src(cx, JS::ObjectValue(*obj));

  JSStructuredCloneData data(scope);
  CHECK(JS_WriteStructuredClone(cx, src, &data, scope, JS::CloneDataPolicy(),
                                &callbacks, nullptr, JS::UndefinedHandleValue));

  JS::RootedValue dst(cx);
  CHECK(JS_ReadStructuredClone(cx, data, scope, &dst, JS::CloneDataPolicy(),
                               &callbacks, nullptr));
  CHECK(dst.isObject());

  return true;
}
END_TEST(testStructuredClone_peekUint32Pair)
