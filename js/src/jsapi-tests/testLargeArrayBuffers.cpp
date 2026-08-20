/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "js/ArrayBuffer.h"
#include "js/ArrayBufferMaybeShared.h"
#include "js/experimental/TypedData.h"
#include "js/SharedArrayBuffer.h"
#include "jsapi-tests/tests.h"

using namespace js;

BEGIN_TEST(testLargeArrayBuffers) {
#ifdef JS_64BIT
  constexpr size_t nbytes = size_t(6) * 1024 * 1024 * 1024;  // 6 GB.

  RootedObject buffer(cx, JS::NewArrayBuffer(cx, nbytes));
  CHECK(buffer);
  CHECK(JS::IsArrayBufferObject(buffer));

  size_t length;
  bool isShared;
  uint8_t* data;

  // ArrayBuffer
  {
    CHECK_EQUAL(JS::GetArrayBufferByteLength(buffer), nbytes);

    JS::GetArrayBufferLengthAndData(buffer, &length, &isShared, &data);
    CHECK_EQUAL(length, nbytes);

    length = 0;
    JS::GetArrayBufferMaybeSharedLengthAndData(buffer, &length, &isShared,
                                               &data);
    CHECK_EQUAL(length, nbytes);

    length = 0;
    CHECK(GetObjectAsArrayBuffer(buffer, &length, &data));
    CHECK_EQUAL(length, nbytes);
  }

  // New Uint8Array
  {
    RootedObject tarr(cx, JS_NewUint8Array(cx, nbytes));
    CHECK(JS_IsTypedArrayObject(tarr));
    CHECK_EQUAL(JS_GetArrayBufferViewByteOffset(tarr), 0u);
    CHECK_EQUAL(JS_GetArrayBufferViewByteLength(tarr), nbytes);
    CHECK_EQUAL(JS_GetTypedArrayByteOffset(tarr), 0u);
    CHECK_EQUAL(JS_GetTypedArrayByteLength(tarr), nbytes);
    CHECK_EQUAL(JS_GetTypedArrayLength(tarr), nbytes);

    length = 0;
    js::GetArrayBufferViewLengthAndData(tarr, &length, &isShared, &data);
    CHECK_EQUAL(length, nbytes);

    length = 0;
    JS::AutoCheckCannotGC nogc(cx);
    auto ta = JS::Uint8Array::fromObject(tarr);
    CHECK(!ta.isDetached());
    CHECK(!ta.isResizable());
    CHECK(!ta.isImmutable());
    mozilla::Span<uint8_t> span = ta.getData(&isShared, nogc);
    CHECK_EQUAL(span.data(), reinterpret_cast<uint8_t*>(data));
    CHECK_EQUAL(span.Length(), nbytes);

    length = 0;
    CHECK(JS_GetObjectAsArrayBufferView(tarr, &length, &isShared, &data));
    CHECK_EQUAL(length, nbytes);
  }

  // Int16Array
  {
    RootedObject tarr(cx,
                      JS_NewInt16ArrayWithBuffer(cx, buffer, 0, nbytes / 2));
    CHECK(JS_IsTypedArrayObject(tarr));
    CHECK_EQUAL(JS_GetArrayBufferViewByteOffset(tarr), 0u);
    CHECK_EQUAL(JS_GetArrayBufferViewByteLength(tarr), nbytes);
    CHECK_EQUAL(JS_GetTypedArrayByteOffset(tarr), 0u);
    CHECK_EQUAL(JS_GetTypedArrayByteLength(tarr), nbytes);
    CHECK_EQUAL(JS_GetTypedArrayLength(tarr), nbytes / 2);

    length = 0;
    js::GetArrayBufferViewLengthAndData(tarr, &length, &isShared, &data);
    CHECK_EQUAL(length, nbytes);

    length = 0;
    JS::AutoCheckCannotGC nogc(cx);
    auto ta = JS::Int16Array::fromObject(tarr);
    CHECK(!ta.isDetached());
    CHECK(!ta.isResizable());
    CHECK(!ta.isImmutable());
    mozilla::Span<short> span = ta.getData(&isShared, nogc);
    CHECK_EQUAL(span.data(), reinterpret_cast<short*>(data));
    CHECK_EQUAL(span.Length(), nbytes / 2);

    length = 0;
    CHECK(JS_GetObjectAsArrayBufferView(tarr, &length, &isShared, &data));
    CHECK_EQUAL(length, nbytes);
  }

  // DataView
  {
    RootedObject dv(cx, JS_NewDataView(cx, buffer, 0, nbytes - 10));
    CHECK(JS_IsArrayBufferViewObject(dv));
    CHECK_EQUAL(JS_GetArrayBufferViewByteOffset(dv), 0u);
    CHECK_EQUAL(JS_GetArrayBufferViewByteLength(dv), nbytes - 10);

    length = 0;
    js::GetArrayBufferViewLengthAndData(dv, &length, &isShared, &data);
    CHECK_EQUAL(length, nbytes - 10);

    length = 0;
    CHECK(JS_GetObjectAsArrayBufferView(dv, &length, &isShared, &data));
    CHECK_EQUAL(length, nbytes - 10);
  }

  // Int8Array with large byteOffset.
  {
    RootedObject tarr(cx,
                      JS_NewInt8ArrayWithBuffer(cx, buffer, nbytes - 200, 32));
    CHECK(JS_IsTypedArrayObject(tarr));
    CHECK_EQUAL(JS_GetArrayBufferViewByteOffset(tarr), nbytes - 200);
    CHECK_EQUAL(JS_GetArrayBufferViewByteLength(tarr), 32u);
    CHECK_EQUAL(JS_GetTypedArrayByteOffset(tarr), nbytes - 200);
    CHECK_EQUAL(JS_GetTypedArrayByteLength(tarr), 32u);
    CHECK_EQUAL(JS_GetTypedArrayLength(tarr), 32u);
  }

  // DataView with large byteOffset.
  {
    RootedObject dv(cx, JS_NewDataView(cx, buffer, nbytes - 200, 32));
    CHECK(JS_IsArrayBufferViewObject(dv));
    CHECK_EQUAL(JS_GetArrayBufferViewByteOffset(dv), nbytes - 200);
    CHECK_EQUAL(JS_GetArrayBufferViewByteLength(dv), 32u);
  }
#endif

  return true;
}
END_TEST(testLargeArrayBuffers)

BEGIN_TEST(testLargeSharedArrayBuffers) {
#ifdef JS_64BIT
  constexpr size_t nbytes = size_t(5) * 1024 * 1024 * 1024;  // 5 GB.

  RootedObject buffer(cx, JS::NewSharedArrayBuffer(cx, nbytes));
  CHECK(buffer);
  CHECK(JS::IsSharedArrayBufferObject(buffer));
  CHECK_EQUAL(GetSharedArrayBufferByteLength(buffer), nbytes);

  size_t length;
  bool isShared;
  uint8_t* data;
  JS::GetSharedArrayBufferLengthAndData(buffer, &length, &isShared, &data);
  CHECK_EQUAL(length, nbytes);
#endif

  return true;
}
END_TEST(testLargeSharedArrayBuffers)
