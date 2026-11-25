/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "vm/TypedArrayObject.h"

#include "mozilla/Casting.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/EndianUtils.h"
#include "mozilla/FloatingPoint.h"
#include "mozilla/IntegerTypeTraits.h"
#include "mozilla/Likely.h"
#include "mozilla/PodOperations.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/SIMD.h"
#include "mozilla/TextUtils.h"

#include <algorithm>
#include <charconv>
#include <iterator>
#include <limits>
#include <numeric>
#include <string.h>
#include <string_view>

#include "vm/TypedArrayObject-inl.h"
#if !defined(XP_WIN) && !defined(__wasi__)
#  include <sys/mman.h>
#endif
#include <type_traits>

#include "jstypes.h"

#include "builtin/Array.h"
#include "builtin/DataViewObject.h"
#include "builtin/Number.h"
#include "gc/Barrier.h"
#include "gc/MaybeRooted.h"
#include "jit/InlinableNatives.h"
#include "jit/TrampolineNatives.h"
#include "js/Conversions.h"
#include "js/experimental/TypedData.h"  // JS_GetArrayBufferViewType, JS_GetTypedArray{Length,ByteOffset,ByteLength}, JS_IsTypedArrayObject
#include "js/friend/ErrorMessages.h"    // js::GetErrorMessage, JSMSG_*
#include "js/PropertySpec.h"
#include "js/ScalarType.h"  // JS::Scalar::Type
#include "js/UniquePtr.h"
#include "js/Wrapper.h"
#include "util/DifferentialTesting.h"
#include "util/StringBuilder.h"
#include "util/Text.h"
#include "util/WindowsWrapper.h"
#include "vm/ArrayBufferObject.h"
#include "vm/EqualityOperations.h"
#include "vm/Float16.h"
#include "vm/FunctionFlags.h"  // js::FunctionFlags
#include "vm/GlobalObject.h"
#include "vm/Interpreter.h"
#include "vm/JSContext.h"
#include "vm/JSObject.h"
#include "vm/SelfHosting.h"
#include "vm/SharedMem.h"
#include "vm/Uint8Clamped.h"
#include "vm/WrapperObject.h"

#include "builtin/Sorting-inl.h"
#include "gc/Nursery-inl.h"
#include "vm/ArrayBufferObject-inl.h"
#include "vm/Compartment-inl.h"
#include "vm/GeckoProfiler-inl.h"
#include "vm/NativeObject-inl.h"
#include "vm/StringType-inl.h"

using namespace js;

using mozilla::IsAsciiDigit;

/*
 * TypedArrayObject
 *
 * The non-templated base class for the specific typed implementations.
 * This class holds all the member variables that are used by
 * the subclasses.
 */

bool TypedArrayObject::convertValue(JSContext* cx, HandleValue v,
                                    MutableHandleValue result) const {
  switch (type()) {
    case Scalar::BigInt64:
    case Scalar::BigUint64: {
      BigInt* bi = ToBigInt(cx, v);
      if (!bi) {
        return false;
      }
      result.setBigInt(bi);
      return true;
    }
    case Scalar::Int8:
    case Scalar::Uint8:
    case Scalar::Int16:
    case Scalar::Uint16:
    case Scalar::Int32:
    case Scalar::Uint32:
    case Scalar::Float16:
    case Scalar::Float32:
    case Scalar::Float64:
    case Scalar::Uint8Clamped: {
      double num;
      if (!ToNumber(cx, v, &num)) {
        return false;
      }
      result.setNumber(num);
      return true;
    }
    case Scalar::MaxTypedArrayViewType:
    case Scalar::Int64:
    case Scalar::Simd128:
      MOZ_CRASH("Unsupported TypedArray type");
  }
  MOZ_ASSERT_UNREACHABLE("Invalid scalar type");
  return false;
}

static bool IsTypedArrayObject(HandleValue v) {
  return v.isObject() && v.toObject().is<TypedArrayObject>();
}

static bool IsUint8ArrayObject(HandleValue v) {
  return IsTypedArrayObject(v) &&
         v.toObject().as<TypedArrayObject>().type() == Scalar::Uint8;
}

/* static */
bool TypedArrayObject::ensureHasBuffer(JSContext* cx,
                                       Handle<TypedArrayObject*> typedArray) {
  if (typedArray->hasBuffer()) {
    return true;
  }

  MOZ_ASSERT(typedArray->is<FixedLengthTypedArrayObject>(),
             "Resizable and immutable TypedArrays always use an ArrayBuffer");

  auto tarray = HandleObject(typedArray).as<FixedLengthTypedArrayObject>();

  size_t byteLength = tarray->byteLength();

  AutoRealm ar(cx, tarray);

  ArrayBufferObject* buffer;
  if (tarray->hasMallocedElements(cx)) {
    // Allocate a new array buffer and transfer our malloced buffer to it
    // without copying.
    buffer =
        ArrayBufferObject::createFromTypedArrayMallocedElements(cx, tarray);
    if (!buffer) {
      return false;
    }
  } else {
    buffer = ArrayBufferObject::createZeroed(cx, byteLength);
    if (!buffer) {
      return false;
    }

    // tarray is not shared, because if it were it would have a buffer.
    memcpy(buffer->dataPointer(), tarray->dataPointerUnshared(), byteLength);

    // If the object is in the nursery, the buffer will be freed by the next
    // nursery GC. Free the data slot pointer if the object has no inline data.
    //
    // Note: we checked hasMallocedElements above, but allocating the array
    // buffer object might have triggered a GC and this can malloc typed array
    // elements if the typed array was in the nursery.
    if (tarray->isTenured() && tarray->hasMallocedElements(cx)) {
      size_t nbytes = RoundUp(byteLength, sizeof(Value));
      js_free(tarray->elements());
      RemoveCellMemory(tarray, nbytes, MemoryUse::TypedArrayElements);
    }

    tarray->setFixedSlotTyped(TypedArrayObject::DATA_SLOT,
                              PrivateValue(buffer->dataPointer()));
  }

  MOZ_ASSERT(tarray->elements() == buffer->dataPointer());

  buffer->pinLength(tarray->isLengthPinned());

  // Attaching the first view to an array buffer is infallible.
  MOZ_ALWAYS_TRUE(buffer->addView(cx, tarray));

  tarray->setFixedSlotTyped(TypedArrayObject::BUFFER_SLOT,
                            ObjectValue(*buffer));

  return true;
}

#ifdef DEBUG
void FixedLengthTypedArrayObject::assertZeroLengthArrayData() const {
  if (length() == 0 && !hasBuffer()) {
    uint8_t* end = fixedData(FixedLengthTypedArrayObject::FIXED_DATA_START);
    MOZ_ASSERT(end[0] == ZeroLengthArrayData);
  }
}
#endif

void FixedLengthTypedArrayObject::finalize(JS::GCContext* gcx, JSObject* obj) {
  MOZ_ASSERT(!IsInsideNursery(obj));
  auto* curObj = &obj->as<FixedLengthTypedArrayObject>();

  // Template objects or discarded objects (which didn't have enough room
  // for inner elements) don't have anything to free.
  if (!curObj->elementsRaw()) {
    return;
  }

  curObj->assertZeroLengthArrayData();

  // Typed arrays with a buffer object do not need to be free'd
  if (curObj->hasBuffer()) {
    return;
  }

  // Free the data slot pointer if it does not point into the old JSObject.
  if (!curObj->hasInlineElements()) {
    size_t nbytes = RoundUp(curObj->byteLength(), sizeof(Value));
    gcx->free_(obj, curObj->elements(), nbytes, MemoryUse::TypedArrayElements);
  }
}

/* static */
size_t FixedLengthTypedArrayObject::objectMoved(JSObject* obj, JSObject* old) {
  auto* newObj = &obj->as<FixedLengthTypedArrayObject>();
  const auto* oldObj = &old->as<FixedLengthTypedArrayObject>();
  MOZ_ASSERT(newObj->elementsRaw() == oldObj->elementsRaw());

  // Typed arrays with a buffer object do not need an update.
  if (oldObj->hasBuffer()) {
    return 0;
  }

  if (!IsInsideNursery(old)) {
    // Update the data slot pointer if it points to the old JSObject.
    if (oldObj->hasInlineElements()) {
      newObj->setInlineElements();
    }

    return 0;
  }

  void* buf = oldObj->elements();

  // Discarded objects (which didn't have enough room for inner elements) don't
  // have any data to move.
  if (!buf) {
    return 0;
  }

  Nursery& nursery = obj->runtimeFromMainThread()->gc.nursery();

  // Determine if we can use inline data for the target array. If this is
  // possible, the nursery will have picked an allocation size that is large
  // enough.
  size_t nbytes = oldObj->byteLength();
  bool canUseDirectForward = nbytes >= sizeof(uintptr_t);

  constexpr size_t headerSize = dataOffset() + sizeof(HeapSlot);

  gc::AllocKind allocKind = oldObj->allocKindForTenure();
  MOZ_ASSERT_IF(obj->isTenured(), obj->asTenured().getAllocKind() == allocKind);
  MOZ_ASSERT_IF(nbytes == 0,
                headerSize + sizeof(uint8_t) <= GetGCKindBytes(allocKind));

  if (nursery.isInside(buf) &&
      headerSize + nbytes <= GetGCKindBytes(allocKind)) {
    MOZ_ASSERT(oldObj->hasInlineElements());
#ifdef DEBUG
    if (nbytes == 0) {
      uint8_t* output =
          newObj->fixedData(FixedLengthTypedArrayObject::FIXED_DATA_START);
      output[0] = ZeroLengthArrayData;
    }
#endif
    newObj->setInlineElements();
    mozilla::PodCopy(newObj->elements(), oldObj->elements(), nbytes);

    // Set a forwarding pointer for the element buffers in case they were
    // preserved on the stack by Ion.
    nursery.setForwardingPointerWhileTenuring(
        oldObj->elements(), newObj->elements(), canUseDirectForward);

    return 0;
  }

  // Non-inline allocations are rounded up.
  nbytes = RoundUp(nbytes, sizeof(Value));

  Nursery::WasBufferMoved result =
      nursery.maybeMoveNurseryOrMallocBufferOnPromotion(
          &buf, newObj, nbytes, nbytes, MemoryUse::TypedArrayElements,
          ArrayBufferContentsArena);
  if (result == Nursery::BufferMoved) {
    newObj->setReservedSlotTyped(DATA_SLOT, PrivateValue(buf));

    // Set a forwarding pointer for the element buffers in case they were
    // preserved on the stack by Ion.
    nursery.setForwardingPointerWhileTenuring(
        oldObj->elements(), newObj->elements(), canUseDirectForward);

    return nbytes;
  }

  return 0;
}

bool FixedLengthTypedArrayObject::hasInlineElements() const {
  return elements() ==
             this->fixedData(FixedLengthTypedArrayObject::FIXED_DATA_START) &&
         byteLength() <= FixedLengthTypedArrayObject::INLINE_BUFFER_LIMIT;
}

void FixedLengthTypedArrayObject::setInlineElements() {
  char* dataSlot = reinterpret_cast<char*>(this) + dataOffset();
  *reinterpret_cast<void**>(dataSlot) =
      this->fixedData(FixedLengthTypedArrayObject::FIXED_DATA_START);
}

bool FixedLengthTypedArrayObject::hasMallocedElements(JSContext* cx) const {
  return !hasInlineElements() && !cx->nursery().isInside(elements());
}

/* Helper clamped uint8_t type */

uint32_t js::ClampDoubleToUint8(const double x) {
  // Not < so that NaN coerces to 0
  if (!(x > 0)) {
    return 0;
  }

  if (x >= 255) {
    return 255;
  }

  // Convert with truncation.
  uint8_t y = uint8_t(x);

  // Now |y| is rounded toward zero. We want rounded to nearest, ties to even.
  double r = x - double(y);

  // It was a tie (since the difference is exactly 0.5). Round up if the number
  // is odd.
  if (r == 0.5) {
    return y + (y & 1);
  }

  // Round up if truncation incorrectly rounded down.
  return y + (r > 0.5);
}

static void ReportOutOfBounds(JSContext* cx, TypedArrayObject* typedArray) {
  if (typedArray->hasDetachedBuffer()) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_TYPED_ARRAY_DETACHED);
  } else {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_TYPED_ARRAY_RESIZED_BOUNDS);
  }
}

namespace {

template <class TypedArrayType>
static TypedArrayType* NewTypedArrayObject(JSContext* cx, const JSClass* clasp,
                                           HandleObject proto,
                                           gc::AllocKind allocKind,
                                           gc::Heap heap) {
  MOZ_ASSERT(proto);
  allocKind = gc::GetFinalizedAllocKindForClass(allocKind, clasp);

  static_assert(std::is_same_v<TypedArrayType, FixedLengthTypedArrayObject> ||
                std::is_same_v<TypedArrayType, ResizableTypedArrayObject> ||
                std::is_same_v<TypedArrayType, ImmutableTypedArrayObject>);

  // Fixed length typed arrays can store data inline so we only use fixed slots
  // to cover the reserved slots, ignoring the AllocKind.
  MOZ_ASSERT(ClassCanHaveFixedData(clasp));
  constexpr size_t nfixed = TypedArrayType::RESERVED_SLOTS;
  static_assert(nfixed <= NativeObject::MAX_FIXED_SLOTS);
  static_assert(!std::is_same_v<TypedArrayType, FixedLengthTypedArrayObject> ||
                nfixed == FixedLengthTypedArrayObject::FIXED_DATA_START);

  ObjectFlags objectFlags;
  if constexpr (std::is_same_v<TypedArrayType, ImmutableTypedArrayObject>) {
    // Immutable elements are non-configurable, non-writable data properties.
    objectFlags.setFlag(ObjectFlag::NeedsProxyGetSetResultValidation);
  }

  Rooted<SharedShape*> shape(
      cx,
      SharedShape::getInitialShape(cx, clasp, cx->realm(), AsTaggedProto(proto),
                                   nfixed, objectFlags));
  if (!shape) {
    return nullptr;
  }

  return NativeObject::create<TypedArrayType>(cx, allocKind, heap, shape);
}

template <typename NativeType>
class FixedLengthTypedArrayObjectTemplate;

template <typename NativeType>
class ResizableTypedArrayObjectTemplate;

template <typename NativeType>
class ImmutableTypedArrayObjectTemplate;

template <typename NativeType>
class TypedArrayObjectTemplate {
  friend class js::TypedArrayObject;

  using FixedLengthTypedArray = FixedLengthTypedArrayObjectTemplate<NativeType>;
  using ResizableTypedArray = ResizableTypedArrayObjectTemplate<NativeType>;
  using ImmutableTypedArray = ImmutableTypedArrayObjectTemplate<NativeType>;
  using AutoLength = ArrayBufferViewObject::AutoLength;

  static constexpr auto ByteLengthLimit = TypedArrayObject::ByteLengthLimit;
  static constexpr auto INLINE_BUFFER_LIMIT =
      FixedLengthTypedArrayObject::INLINE_BUFFER_LIMIT;

 public:
  static constexpr Scalar::Type ArrayTypeID() {
    return TypeIDOfType<NativeType>::id;
  }
  static constexpr JSProtoKey protoKey() {
    return TypeIDOfType<NativeType>::protoKey;
  }

  static constexpr size_t BYTES_PER_ELEMENT = sizeof(NativeType);

  static JSObject* createPrototype(JSContext* cx, JSProtoKey key) {
    Handle<GlobalObject*> global = cx->global();
    RootedObject typedArrayProto(
        cx, GlobalObject::getOrCreateTypedArrayPrototype(cx, global));
    if (!typedArrayProto) {
      return nullptr;
    }

    const JSClass* clasp = TypedArrayObject::protoClassForType(ArrayTypeID());
    return GlobalObject::createBlankPrototypeInheriting(cx, clasp,
                                                        typedArrayProto);
  }

  static JSObject* createConstructor(JSContext* cx, JSProtoKey key) {
    Handle<GlobalObject*> global = cx->global();
    RootedFunction ctorProto(
        cx, GlobalObject::getOrCreateTypedArrayConstructor(cx, global));
    if (!ctorProto) {
      return nullptr;
    }

    JSFunction* fun = NewFunctionWithProto(
        cx, class_constructor, 3, FunctionFlags::NATIVE_CTOR, nullptr,
        ClassName(key, cx), ctorProto, gc::AllocKind::FUNCTION, TenuredObject);

    if (fun) {
      fun->setJitInfo(&jit::JitInfo_TypedArrayConstructor);
    }

    return fun;
  }

  static bool convertValue(JSContext* cx, HandleValue v, NativeType* result);

  static TypedArrayObject* makeTypedArrayWithTemplate(
      JSContext* cx, TypedArrayObject* templateObj, HandleObject array) {
    MOZ_ASSERT(!IsWrapper(array));
    MOZ_ASSERT(!array->is<ArrayBufferObjectMaybeShared>());

    return fromArray(cx, array);
  }

  static TypedArrayObject* makeTypedArrayWithTemplate(
      JSContext* cx, TypedArrayObject* templateObj, HandleObject arrayBuffer,
      HandleValue byteOffsetValue, HandleValue lengthValue) {
    MOZ_ASSERT(!IsWrapper(arrayBuffer));
    MOZ_ASSERT(arrayBuffer->is<ArrayBufferObjectMaybeShared>());

    uint64_t byteOffset, length;
    if (!byteOffsetAndLength(cx, byteOffsetValue, lengthValue, &byteOffset,
                             &length)) {
      return nullptr;
    }

    return fromBufferSameCompartment(
        cx, arrayBuffer.as<ArrayBufferObjectMaybeShared>(), byteOffset, length,
        nullptr);
  }

  // ES2026 draft rev 6d71ca0e2dbf1c0cfb87b5eb7a83cf7c76591561
  // 23.2.5.1 TypedArray ( ...args )
  static bool class_constructor(JSContext* cx, unsigned argc, Value* vp) {
    AutoJSConstructorProfilerEntry pseudoFrame(cx, "[TypedArray]");
    CallArgs args = CallArgsFromVp(argc, vp);

    // Step 1.
    if (!ThrowIfNotConstructing(cx, args, "typed array")) {
      return false;
    }

    // Steps 2-6.
    JSObject* obj = create(cx, args);
    if (!obj) {
      return false;
    }
    args.rval().setObject(*obj);
    return true;
  }

 private:
  static JSObject* create(JSContext* cx, const CallArgs& args) {
    MOZ_ASSERT(args.isConstructing());

    // Steps 5 and 6.c.
    if (args.length() == 0 || !args[0].isObject()) {
      // Step 6.c.ii.
      uint64_t len;
      if (!ToIndex(cx, args.get(0), JSMSG_BAD_ARRAY_LENGTH, &len)) {
        return nullptr;
      }

      // Steps 5.a and 6.c.iii.
      RootedObject proto(cx);
      if (!GetPrototypeFromBuiltinConstructor(cx, args, protoKey(), &proto)) {
        return nullptr;
      }

      return fromLength(cx, len, proto);
    }

    RootedObject dataObj(cx, &args[0].toObject());

    // Step 6.b.i.
    // 23.2.5.1.1 AllocateTypedArray, step 1.
    RootedObject proto(cx);
    if (!GetPrototypeFromBuiltinConstructor(cx, args, protoKey(), &proto)) {
      return nullptr;
    }

    // Steps 6.b.ii and 6.b.iv.
    if (!UncheckedUnwrap(dataObj)->is<ArrayBufferObjectMaybeShared>()) {
      return fromArray(cx, dataObj, proto);
    }

    // Steps 6.b.iii.1-2.
    // 23.2.5.1.3 InitializeTypedArrayFromArrayBuffer, steps 2 and 4.
    uint64_t byteOffset, length;
    if (!byteOffsetAndLength(cx, args.get(1), args.get(2), &byteOffset,
                             &length)) {
      return nullptr;
    }

    // Step 6.b.iii.3.
    if (dataObj->is<ArrayBufferObjectMaybeShared>()) {
      auto buffer = dataObj.as<ArrayBufferObjectMaybeShared>();
      return fromBufferSameCompartment(cx, buffer, byteOffset, length, proto);
    }
    return fromBufferWrapped(cx, dataObj, byteOffset, length, proto);
  }

  // ES2026 draft rev 6d71ca0e2dbf1c0cfb87b5eb7a83cf7c76591561
  // 23.2.5.1.3 InitializeTypedArrayFromArrayBuffer ( O, buffer, byteOffset,
  // length ) Steps 2-3 and 5.
  static bool byteOffsetAndLength(JSContext* cx, HandleValue byteOffsetValue,
                                  HandleValue lengthValue, uint64_t* byteOffset,
                                  uint64_t* length) {
    // Steps 2-3.
    *byteOffset = 0;
    if (!byteOffsetValue.isUndefined()) {
      // Step 2.
      if (!ToIndex(cx, byteOffsetValue, byteOffset)) {
        return false;
      }

      // Step 3.
      if (*byteOffset % BYTES_PER_ELEMENT != 0) {
        JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                  JSMSG_TYPED_ARRAY_CONSTRUCT_OFFSET_BOUNDS,
                                  Scalar::name(ArrayTypeID()),
                                  Scalar::byteSizeString(ArrayTypeID()));
        return false;
      }
    }

    // Step 5.
    *length = UINT64_MAX;
    if (!lengthValue.isUndefined()) {
      if (!ToIndex(cx, lengthValue, length)) {
        return false;
      }
    }

    return true;
  }

  // ES2026 draft rev 6d71ca0e2dbf1c0cfb87b5eb7a83cf7c76591561
  // 23.2.5.1.3 InitializeTypedArrayFromArrayBuffer ( O, buffer, byteOffset,
  // length ) Steps 6-9.
  static bool computeAndCheckLength(
      JSContext* cx, Handle<ArrayBufferObjectMaybeShared*> bufferMaybeUnwrapped,
      uint64_t byteOffset, uint64_t lengthIndex, size_t* length,
      AutoLength* autoLength) {
    MOZ_ASSERT(byteOffset % BYTES_PER_ELEMENT == 0);
    MOZ_ASSERT(byteOffset < uint64_t(DOUBLE_INTEGRAL_PRECISION_LIMIT));
    MOZ_ASSERT_IF(lengthIndex != UINT64_MAX,
                  lengthIndex < uint64_t(DOUBLE_INTEGRAL_PRECISION_LIMIT));

    // Step 6.
    if (bufferMaybeUnwrapped->isDetached()) {
      JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                JSMSG_TYPED_ARRAY_DETACHED);
      return false;
    }

    // Step 7.
    size_t bufferByteLength = bufferMaybeUnwrapped->byteLength();
    MOZ_ASSERT(bufferByteLength <= ByteLengthLimit);

    // Steps 8-9.
    size_t len;
    if (lengthIndex == UINT64_MAX) {
      // Steps 8.a and 9.a.iii.
      //
      // Check if |byteOffset| valid.
      if (byteOffset > bufferByteLength) {
        JS_ReportErrorNumberASCII(
            cx, GetErrorMessage, nullptr,
            JSMSG_TYPED_ARRAY_CONSTRUCT_OFFSET_LENGTH_BOUNDS,
            Scalar::name(ArrayTypeID()));
        return false;
      }

      // Steps 8.b-c.
      //
      // Resizable buffers without an explicit length are auto-length.
      if (bufferMaybeUnwrapped->isResizable()) {
        *length = 0;
        *autoLength = AutoLength::Yes;
        return true;
      }

      // Step 9.a.i.
      if (bufferByteLength % BYTES_PER_ELEMENT != 0) {
        // The given byte array doesn't map exactly to
        // |BYTES_PER_ELEMENT * N|
        JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                  JSMSG_TYPED_ARRAY_CONSTRUCT_OFFSET_MISALIGNED,
                                  Scalar::name(ArrayTypeID()),
                                  Scalar::byteSizeString(ArrayTypeID()));
        return false;
      }

      // Step 9.a.ii.
      size_t newByteLength = bufferByteLength - size_t(byteOffset);
      len = newByteLength / BYTES_PER_ELEMENT;
    } else {
      // Step 9.b.i.
      uint64_t newByteLength = lengthIndex * BYTES_PER_ELEMENT;

      // Step 9.b.ii.
      if (byteOffset + newByteLength > bufferByteLength) {
        // |byteOffset + newByteLength| is too big for the arraybuffer
        JS_ReportErrorNumberASCII(
            cx, GetErrorMessage, nullptr,
            JSMSG_TYPED_ARRAY_CONSTRUCT_ARRAY_LENGTH_BOUNDS,
            Scalar::name(ArrayTypeID()));
        return false;
      }

      len = size_t(lengthIndex);
    }

    // Steps 9.c-d.
    MOZ_ASSERT(len <= ByteLengthLimit / BYTES_PER_ELEMENT);
    *length = len;
    *autoLength = AutoLength::No;
    return true;
  }

  // ES2026 draft rev 6d71ca0e2dbf1c0cfb87b5eb7a83cf7c76591561
  // 23.2.5.1.3 InitializeTypedArrayFromArrayBuffer ( O, buffer, byteOffset,
  // length ) Steps 6-12.
  static TypedArrayObject* fromBufferSameCompartment(
      JSContext* cx, Handle<ArrayBufferObjectMaybeShared*> buffer,
      uint64_t byteOffset, uint64_t lengthIndex, HandleObject proto) {
    cx->releaseCheck(buffer);
    // Steps 6-9.
    size_t length = 0;
    auto autoLength = AutoLength::No;
    if (!computeAndCheckLength(cx, buffer, byteOffset, lengthIndex, &length,
                               &autoLength)) {
      return nullptr;
    }

    // Steps 10-12.
    if (buffer->isResizable()) {
      return ResizableTypedArray::makeInstance(cx, buffer, byteOffset, length,
                                               autoLength, proto);
    }
    if (buffer->isImmutable()) {
      return ImmutableTypedArray::makeInstance(cx, buffer, byteOffset, length,
                                               proto);
    }
    return FixedLengthTypedArray::makeInstance(cx, buffer, byteOffset, length,
                                               proto);
  }

  // Create a TypedArray object in another compartment.
  //
  // ES6 supports creating a TypedArray in global A (using global A's
  // TypedArray constructor) backed by an ArrayBuffer created in global B.
  //
  // Our TypedArrayObject implementation doesn't support a TypedArray in
  // compartment A backed by an ArrayBuffer in compartment B. So in this
  // case, we create the TypedArray in B (!) and return a cross-compartment
  // wrapper.
  //
  // Extra twist: the spec says the new TypedArray's [[Prototype]] must be
  // A's TypedArray.prototype. So even though we're creating the TypedArray
  // in B, its [[Prototype]] must be (a cross-compartment wrapper for) the
  // TypedArray.prototype in A.
  static JSObject* fromBufferWrapped(JSContext* cx, HandleObject bufobj,
                                     uint64_t byteOffset, uint64_t lengthIndex,
                                     HandleObject proto) {
    JSObject* unwrapped = CheckedUnwrapStatic(bufobj);
    if (!unwrapped) {
      ReportAccessDenied(cx);
      return nullptr;
    }

    if (!unwrapped->is<ArrayBufferObjectMaybeShared>()) {
      JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                JSMSG_TYPED_ARRAY_BAD_ARGS);
      return nullptr;
    }

    Rooted<ArrayBufferObjectMaybeShared*> unwrappedBuffer(cx);
    unwrappedBuffer = &unwrapped->as<ArrayBufferObjectMaybeShared>();

    size_t length = 0;
    auto autoLength = AutoLength::No;
    if (!computeAndCheckLength(cx, unwrappedBuffer, byteOffset, lengthIndex,
                               &length, &autoLength)) {
      return nullptr;
    }

    // Make sure to get the [[Prototype]] for the created typed array from
    // this compartment.
    RootedObject protoRoot(cx, proto);
    if (!protoRoot) {
      protoRoot = GlobalObject::getOrCreatePrototype(cx, protoKey());
      if (!protoRoot) {
        return nullptr;
      }
    }

    RootedObject typedArray(cx);
    {
      JSAutoRealm ar(cx, unwrappedBuffer);

      RootedObject wrappedProto(cx, protoRoot);
      if (!cx->compartment()->wrap(cx, &wrappedProto)) {
        return nullptr;
      }

      if (unwrappedBuffer->isResizable()) {
        typedArray = ResizableTypedArray::makeInstance(
            cx, unwrappedBuffer, byteOffset, length, autoLength, wrappedProto);
      } else if (unwrappedBuffer->isImmutable()) {
        typedArray = ImmutableTypedArray::makeInstance(
            cx, unwrappedBuffer, byteOffset, length, wrappedProto);
      } else {
        typedArray = FixedLengthTypedArray::makeInstance(
            cx, unwrappedBuffer, byteOffset, length, wrappedProto);
      }
      if (!typedArray) {
        return nullptr;
      }
    }

    if (!cx->compartment()->wrap(cx, &typedArray)) {
      return nullptr;
    }

    return typedArray;
  }

 public:
  static JSObject* fromBuffer(JSContext* cx, HandleObject bufobj,
                              size_t byteOffset, int64_t lengthInt) {
    if (byteOffset % BYTES_PER_ELEMENT != 0) {
      JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                JSMSG_TYPED_ARRAY_CONSTRUCT_OFFSET_BOUNDS,
                                Scalar::name(ArrayTypeID()),
                                Scalar::byteSizeString(ArrayTypeID()));
      return nullptr;  // invalid byteOffset
    }

    uint64_t lengthIndex = lengthInt >= 0 ? uint64_t(lengthInt) : UINT64_MAX;
    if (bufobj->is<ArrayBufferObjectMaybeShared>()) {
      auto buffer = bufobj.as<ArrayBufferObjectMaybeShared>();
      return fromBufferSameCompartment(cx, buffer, byteOffset, lengthIndex,
                                       nullptr);
    }
    return fromBufferWrapped(cx, bufobj, byteOffset, lengthIndex, nullptr);
  }

  static TypedArrayObject* fromBuffer(
      JSContext* cx, Handle<ArrayBufferObjectMaybeShared*> buffer,
      size_t byteOffset) {
    MOZ_ASSERT(byteOffset % BYTES_PER_ELEMENT == 0);
    return fromBufferSameCompartment(cx, buffer, byteOffset, UINT64_MAX,
                                     nullptr);
  }

  static TypedArrayObject* fromBuffer(
      JSContext* cx, Handle<ArrayBufferObjectMaybeShared*> buffer,
      size_t byteOffset, size_t length) {
    MOZ_ASSERT(byteOffset % BYTES_PER_ELEMENT == 0);
    return fromBufferSameCompartment(cx, buffer, byteOffset, length, nullptr);
  }

  static bool maybeCreateArrayBuffer(JSContext* cx, uint64_t count,
                                     MutableHandle<ArrayBufferObject*> buffer) {
    if (count > ByteLengthLimit / BYTES_PER_ELEMENT) {
      JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                JSMSG_BAD_ARRAY_LENGTH);
      return false;
    }
    size_t byteLength = count * BYTES_PER_ELEMENT;

    MOZ_ASSERT(byteLength <= ByteLengthLimit);
    static_assert(INLINE_BUFFER_LIMIT % BYTES_PER_ELEMENT == 0,
                  "ArrayBuffer inline storage shouldn't waste any space");

    if (byteLength <= INLINE_BUFFER_LIMIT) {
      // The array's data can be inline, and the buffer created lazily.
      return true;
    }

    ArrayBufferObject* buf = ArrayBufferObject::createZeroed(cx, byteLength);
    if (!buf) {
      return false;
    }

    buffer.set(buf);
    return true;
  }

  // ES2023 draft rev cf86f1cdc28e809170733d74ea64fd0f3dd79f78
  // 23.2.5.1.1 AllocateTypedArray ( constructorName, newTarget, defaultProto [
  // , length ] )
  static TypedArrayObject* fromLength(JSContext* cx, uint64_t nelements,
                                      HandleObject proto = nullptr,
                                      gc::Heap heap = gc::Heap::Default) {
    Rooted<ArrayBufferObject*> buffer(cx);
    if (!maybeCreateArrayBuffer(cx, nelements, &buffer)) {
      return nullptr;
    }

    return FixedLengthTypedArray::makeInstance(cx, buffer, 0, nelements, proto,
                                               heap);
  }

  static TypedArrayObject* fromArray(JSContext* cx, HandleObject other,
                                     HandleObject proto = nullptr);

  static TypedArrayObject* fromTypedArray(JSContext* cx, HandleObject other,
                                          bool isWrapped, HandleObject proto);

  static TypedArrayObject* fromObject(JSContext* cx, HandleObject other,
                                      HandleObject proto);

  static const NativeType getIndex(TypedArrayObject* tarray, size_t index) {
    MOZ_ASSERT(index < tarray->length().valueOr(0));
    return jit::AtomicOperations::loadSafeWhenRacy(
        tarray->dataPointerEither().cast<NativeType*>() + index);
  }

  static void setIndex(TypedArrayObject& tarray, size_t index, NativeType val) {
    MOZ_ASSERT(index < tarray.length().valueOr(0));
    jit::AtomicOperations::storeSafeWhenRacy(
        tarray.dataPointerEither().cast<NativeType*>() + index, val);
  }

  static bool getElement(JSContext* cx, TypedArrayObject* tarray, size_t index,
                         MutableHandleValue val);
  static bool getElementPure(TypedArrayObject* tarray, size_t index, Value* vp);

  static bool setElement(JSContext* cx, Handle<TypedArrayObject*> obj,
                         uint64_t index, HandleValue v, ObjectOpResult& result);
};

template <typename NativeType>
class FixedLengthTypedArrayObjectTemplate
    : public FixedLengthTypedArrayObject,
      public TypedArrayObjectTemplate<NativeType> {
  friend class js::TypedArrayObject;

  using TypedArrayTemplate = TypedArrayObjectTemplate<NativeType>;

 public:
  using TypedArrayTemplate::ArrayTypeID;
  using TypedArrayTemplate::BYTES_PER_ELEMENT;
  using TypedArrayTemplate::protoKey;

  static inline const JSClass* instanceClass() {
    static_assert(ArrayTypeID() <
                  std::size(TypedArrayObject::fixedLengthClasses));
    return &TypedArrayObject::fixedLengthClasses[ArrayTypeID()];
  }

  static FixedLengthTypedArrayObject* newBuiltinClassInstance(
      JSContext* cx, gc::AllocKind allocKind, gc::Heap heap) {
    RootedObject proto(cx, GlobalObject::getOrCreatePrototype(cx, protoKey()));
    if (!proto) {
      return nullptr;
    }
    return NewTypedArrayObject<FixedLengthTypedArrayObject>(
        cx, instanceClass(), proto, allocKind, heap);
  }

  static FixedLengthTypedArrayObject* makeProtoInstance(
      JSContext* cx, HandleObject proto, gc::AllocKind allocKind) {
    MOZ_ASSERT(proto);
    return NewTypedArrayObject<FixedLengthTypedArrayObject>(
        cx, instanceClass(), proto, allocKind, gc::Heap::Default);
  }

  static FixedLengthTypedArrayObject* makeInstance(
      JSContext* cx, Handle<ArrayBufferObjectMaybeShared*> buffer,
      size_t byteOffset, size_t len, HandleObject proto,
      gc::Heap heap = gc::Heap::Default) {
    MOZ_ASSERT(len <= ByteLengthLimit / BYTES_PER_ELEMENT);

    gc::AllocKind allocKind =
        buffer ? gc::GetGCObjectKind(instanceClass())
               : AllocKindForLazyBuffer(len * BYTES_PER_ELEMENT);

    AutoSetNewObjectMetadata metadata(cx);
    FixedLengthTypedArrayObject* obj;
    if (proto) {
      obj = makeProtoInstance(cx, proto, allocKind);
    } else {
      obj = newBuiltinClassInstance(cx, allocKind, heap);
    }
    if (!obj || !obj->init(cx, buffer, byteOffset, len, BYTES_PER_ELEMENT)) {
      return nullptr;
    }

    return obj;
  }

  static FixedLengthTypedArrayObject* makeTemplateObject(JSContext* cx,
                                                         int32_t len) {
    MOZ_ASSERT(len >= 0);
    size_t nbytes;
    MOZ_ALWAYS_TRUE(CalculateAllocSize<NativeType>(len, &nbytes));
    bool fitsInline = nbytes <= INLINE_BUFFER_LIMIT;
    gc::AllocKind allocKind = !fitsInline ? gc::GetGCObjectKind(instanceClass())
                                          : AllocKindForLazyBuffer(nbytes);
    MOZ_ASSERT(allocKind >= gc::GetGCObjectKind(instanceClass()));

    AutoSetNewObjectMetadata metadata(cx);

    auto* tarray = newBuiltinClassInstance(cx, allocKind, gc::Heap::Tenured);
    if (!tarray) {
      return nullptr;
    }

    initTypedArraySlots(tarray, len);

    // Template objects don't need memory for their elements, since there
    // won't be any elements to store.
    MOZ_ASSERT(tarray->getReservedSlotTyped(DATA_SLOT).isUndefined());

    return tarray;
  }

  static FixedLengthTypedArrayObject* fromDetachedBuffer(
      JSContext* cx, Handle<ArrayBufferObject*> buffer,
      gc::Heap heap = gc::Heap::Default) {
    MOZ_ASSERT(buffer->isDetached());

    gc::AllocKind allocKind = gc::GetGCObjectKind(instanceClass());

    AutoSetNewObjectMetadata metadata(cx);
    auto* obj = newBuiltinClassInstance(cx, allocKind, heap);
    if (!obj) {
      return nullptr;
    }

    // Normal construction doesn't allow creating a new TypedArray with an
    // already detached ArrayBuffer. Initialize all slots as if a TypedArrray
    // had been created with a non-detached buffer and the buffer was detached
    // later.
    obj->initFixedSlotTyped(BUFFER_SLOT, ObjectValue(*buffer));
    obj->initFixedSlotTyped(LENGTH_SLOT, PrivateValue(size_t(0)));
    obj->initFixedSlotTyped(BYTEOFFSET_SLOT, PrivateValue(size_t(0)));
    obj->initFixedSlotTyped(DATA_SLOT, UndefinedValue());

    return obj;
  }

  static void initTypedArraySlots(FixedLengthTypedArrayObject* tarray,
                                  int32_t len) {
    MOZ_ASSERT(len >= 0);
    tarray->initFixedSlotTyped(TypedArrayObject::BUFFER_SLOT, JS::FalseValue());
    tarray->initFixedSlotTyped(TypedArrayObject::LENGTH_SLOT,
                               PrivateValue(len));
    tarray->initFixedSlotTyped(TypedArrayObject::BYTEOFFSET_SLOT,
                               PrivateValue(size_t(0)));

#ifdef DEBUG
    if (len == 0) {
      uint8_t* output =
          tarray->fixedData(FixedLengthTypedArrayObject::FIXED_DATA_START);
      output[0] = TypedArrayObject::ZeroLengthArrayData;
    }
#endif
  }

  static void initTypedArrayData(FixedLengthTypedArrayObject* tarray, void* buf,
                                 size_t nbytes, gc::AllocKind allocKind) {
    if (buf) {
      InitReservedSlot(tarray, TypedArrayObject::DATA_SLOT.index(), buf, nbytes,
                       MemoryUse::TypedArrayElements);
    } else {
#ifdef DEBUG
      constexpr size_t dataOffset = ArrayBufferViewObject::dataOffset();
      constexpr size_t offset = dataOffset + sizeof(HeapSlot);
      MOZ_ASSERT(offset + nbytes <= GetGCKindBytes(allocKind));
#endif

      void* data = tarray->fixedData(FIXED_DATA_START);
      tarray->initReservedSlotTyped(DATA_SLOT, PrivateValue(data));
      memset(data, 0, nbytes);
    }
  }

  static FixedLengthTypedArrayObject* makeTypedArrayWithTemplate(
      JSContext* cx, TypedArrayObject* templateObj, int32_t len) {
    if (len < 0 || size_t(len) > ByteLengthLimit / BYTES_PER_ELEMENT) {
      JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                JSMSG_BAD_ARRAY_LENGTH);
      return nullptr;
    }

    size_t nbytes = size_t(len) * BYTES_PER_ELEMENT;
    MOZ_ASSERT(nbytes <= ByteLengthLimit);

    bool fitsInline = nbytes <= INLINE_BUFFER_LIMIT;

    AutoSetNewObjectMetadata metadata(cx);

    gc::AllocKind allocKind = !fitsInline ? gc::GetGCObjectKind(instanceClass())
                                          : AllocKindForLazyBuffer(nbytes);
    MOZ_ASSERT(templateObj->getClass() == instanceClass());

    RootedObject proto(cx, templateObj->staticPrototype());
    auto* obj = makeProtoInstance(cx, proto, allocKind);
    if (!obj) {
      return nullptr;
    }

    initTypedArraySlots(obj, len);

    void* buf = nullptr;
    if (!fitsInline) {
      MOZ_ASSERT(len > 0);

      nbytes = RoundUp(nbytes, sizeof(Value));
      buf = cx->nursery().allocateZeroedBuffer(obj, nbytes,
                                               js::ArrayBufferContentsArena);
      if (!buf) {
        ReportOutOfMemory(cx);
        return nullptr;
      }
    }

    initTypedArrayData(obj, buf, nbytes, allocKind);

    return obj;
  }
};

template <typename NativeType>
class ResizableTypedArrayObjectTemplate
    : public ResizableTypedArrayObject,
      public TypedArrayObjectTemplate<NativeType> {
  friend class js::TypedArrayObject;

  using TypedArrayTemplate = TypedArrayObjectTemplate<NativeType>;

 public:
  using TypedArrayTemplate::ArrayTypeID;
  using TypedArrayTemplate::BYTES_PER_ELEMENT;
  using TypedArrayTemplate::protoKey;

  static inline const JSClass* instanceClass() {
    static_assert(ArrayTypeID() <
                  std::size(TypedArrayObject::resizableClasses));
    return &TypedArrayObject::resizableClasses[ArrayTypeID()];
  }

  static ResizableTypedArrayObject* newBuiltinClassInstance(
      JSContext* cx, gc::AllocKind allocKind, gc::Heap heap) {
    RootedObject proto(cx, GlobalObject::getOrCreatePrototype(cx, protoKey()));
    if (!proto) {
      return nullptr;
    }
    return NewTypedArrayObject<ResizableTypedArrayObject>(
        cx, instanceClass(), proto, allocKind, heap);
  }

  static ResizableTypedArrayObject* makeProtoInstance(JSContext* cx,
                                                      HandleObject proto,
                                                      gc::AllocKind allocKind) {
    MOZ_ASSERT(proto);
    return NewTypedArrayObject<ResizableTypedArrayObject>(
        cx, instanceClass(), proto, allocKind, gc::Heap::Default);
  }

  static ResizableTypedArrayObject* makeInstance(
      JSContext* cx, Handle<ArrayBufferObjectMaybeShared*> buffer,
      size_t byteOffset, size_t len, AutoLength autoLength,
      HandleObject proto) {
    MOZ_ASSERT(buffer);
    MOZ_ASSERT(buffer->isResizable());
    MOZ_ASSERT(!buffer->isDetached());
    MOZ_ASSERT(autoLength == AutoLength::No || len == 0,
               "length is zero for 'auto' length views");
    MOZ_ASSERT(len <= ByteLengthLimit / BYTES_PER_ELEMENT);

    gc::AllocKind allocKind = gc::GetGCObjectKind(instanceClass());

    AutoSetNewObjectMetadata metadata(cx);
    ResizableTypedArrayObject* obj;
    if (proto) {
      obj = makeProtoInstance(cx, proto, allocKind);
    } else {
      obj = newBuiltinClassInstance(cx, allocKind, gc::Heap::Default);
    }
    if (!obj || !obj->initResizable(cx, buffer, byteOffset, len,
                                    BYTES_PER_ELEMENT, autoLength)) {
      return nullptr;
    }

    return obj;
  }

  static ResizableTypedArrayObject* makeTemplateObject(JSContext* cx) {
    gc::AllocKind allocKind = gc::GetGCObjectKind(instanceClass());

    AutoSetNewObjectMetadata metadata(cx);

    auto* tarray = newBuiltinClassInstance(cx, allocKind, gc::Heap::Tenured);
    if (!tarray) {
      return nullptr;
    }

    tarray->initFixedSlotTyped(TypedArrayObject::BUFFER_SLOT, JS::FalseValue());
    tarray->initFixedSlotTyped(TypedArrayObject::LENGTH_SLOT,
                               PrivateValue(size_t(0)));
    tarray->initFixedSlotTyped(TypedArrayObject::BYTEOFFSET_SLOT,
                               PrivateValue(size_t(0)));
    tarray->initFixedSlotTyped(AUTO_LENGTH_SLOT, BooleanValue(false));
    tarray->initFixedSlotTyped(ResizableTypedArrayObject::INITIAL_LENGTH_SLOT,
                               PrivateValue(size_t(0)));
    tarray->initFixedSlotTyped(
        ResizableTypedArrayObject::INITIAL_BYTE_OFFSET_SLOT,
        PrivateValue(size_t(0)));

    // Template objects don't need memory for their elements, since there
    // won't be any elements to store.
    MOZ_ASSERT(tarray->getReservedSlotTyped(DATA_SLOT).isUndefined());

    return tarray;
  }
};

template <typename NativeType>
class ImmutableTypedArrayObjectTemplate
    : public ImmutableTypedArrayObject,
      public TypedArrayObjectTemplate<NativeType> {
  friend class js::TypedArrayObject;

  using TypedArrayTemplate = TypedArrayObjectTemplate<NativeType>;

 public:
  using TypedArrayTemplate::ArrayTypeID;
  using TypedArrayTemplate::BYTES_PER_ELEMENT;
  using TypedArrayTemplate::protoKey;

  static inline const JSClass* instanceClass() {
    static_assert(ArrayTypeID() <
                  std::size(TypedArrayObject::immutableClasses));
    return &TypedArrayObject::immutableClasses[ArrayTypeID()];
  }

  static ImmutableTypedArrayObject* newBuiltinClassInstance(
      JSContext* cx, gc::AllocKind allocKind, gc::Heap heap) {
    RootedObject proto(cx, GlobalObject::getOrCreatePrototype(cx, protoKey()));
    if (!proto) {
      return nullptr;
    }
    return NewTypedArrayObject<ImmutableTypedArrayObject>(
        cx, instanceClass(), proto, allocKind, heap);
  }

  static ImmutableTypedArrayObject* makeProtoInstance(JSContext* cx,
                                                      HandleObject proto,
                                                      gc::AllocKind allocKind) {
    MOZ_ASSERT(proto);
    return NewTypedArrayObject<ImmutableTypedArrayObject>(
        cx, instanceClass(), proto, allocKind, gc::Heap::Default);
  }

  static ImmutableTypedArrayObject* makeInstance(
      JSContext* cx, Handle<ArrayBufferObjectMaybeShared*> buffer,
      size_t byteOffset, size_t len, HandleObject proto) {
    MOZ_ASSERT(buffer);
    MOZ_ASSERT(buffer->isImmutable());
    MOZ_ASSERT(!buffer->isDetached());
    MOZ_ASSERT(len <= ByteLengthLimit / BYTES_PER_ELEMENT);

    gc::AllocKind allocKind = gc::GetGCObjectKind(instanceClass());

    AutoSetNewObjectMetadata metadata(cx);
    ImmutableTypedArrayObject* obj;
    if (proto) {
      obj = makeProtoInstance(cx, proto, allocKind);
    } else {
      obj = newBuiltinClassInstance(cx, allocKind, gc::Heap::Default);
    }
    if (!obj || !obj->init(cx, buffer, byteOffset, len, BYTES_PER_ELEMENT)) {
      return nullptr;
    }

    return obj;
  }

  static ImmutableTypedArrayObject* makeTemplateObject(JSContext* cx) {
    gc::AllocKind allocKind = gc::GetGCObjectKind(instanceClass());

    AutoSetNewObjectMetadata metadata(cx);

    auto* tarray = newBuiltinClassInstance(cx, allocKind, gc::Heap::Tenured);
    if (!tarray) {
      return nullptr;
    }

    tarray->initFixedSlotTyped(TypedArrayObject::BUFFER_SLOT, JS::FalseValue());
    tarray->initFixedSlotTyped(TypedArrayObject::LENGTH_SLOT,
                               PrivateValue(size_t(0)));
    tarray->initFixedSlotTyped(TypedArrayObject::BYTEOFFSET_SLOT,
                               PrivateValue(size_t(0)));

    // Template objects don't need memory for their elements, since there
    // won't be any elements to store.
    MOZ_ASSERT(tarray->getReservedSlotTyped(DATA_SLOT).isUndefined());

    return tarray;
  }
};

template <typename NativeType>
bool TypedArrayObjectTemplate<NativeType>::convertValue(JSContext* cx,
                                                        HandleValue v,
                                                        NativeType* result) {
  double d;
  if (!ToNumber(cx, v, &d)) {
    return false;
  }

  if constexpr (!std::numeric_limits<NativeType>::is_integer) {
    if (js::SupportDifferentialTesting()) {
      // See the comment in ElementSpecific::doubleToNative.
      d = JS::CanonicalizeNaN(d);
    }
  }

  // Assign based on characteristics of the destination type
  *result = ConvertNumber<NativeType>(d);
  return true;
}

template <>
bool TypedArrayObjectTemplate<int64_t>::convertValue(JSContext* cx,
                                                     HandleValue v,
                                                     int64_t* result) {
  JS_TRY_VAR_OR_RETURN_FALSE(cx, *result, ToBigInt64(cx, v));
  return true;
}

template <>
bool TypedArrayObjectTemplate<uint64_t>::convertValue(JSContext* cx,
                                                      HandleValue v,
                                                      uint64_t* result) {
  JS_TRY_VAR_OR_RETURN_FALSE(cx, *result, ToBigUint64(cx, v));
  return true;
}

/**
 * 10.4.5.16 TypedArraySetElement ( O, index, value )
 *
 * ES2025 draft rev ac21460fedf4b926520b06c9820bdbebad596a8b
 */
template <typename NativeType>
/* static */ bool TypedArrayObjectTemplate<NativeType>::setElement(
    JSContext* cx, Handle<TypedArrayObject*> obj, uint64_t index, HandleValue v,
    ObjectOpResult& result) {
  MOZ_ASSERT(!obj->is<ImmutableTypedArrayObject>());

  // Steps 1-2.
  NativeType nativeValue;
  if (!convertValue(cx, v, &nativeValue)) {
    return false;
  }

  // Step 3.
  if (index < obj->length().valueOr(0)) {
    MOZ_ASSERT(!obj->hasDetachedBuffer(),
               "detaching an array buffer sets the length to zero");
    TypedArrayObjectTemplate<NativeType>::setIndex(*obj, index, nativeValue);
  }

  // Step 4.
  return result.succeed();
}

} /* anonymous namespace */

TypedArrayObject* js::NewTypedArrayWithTemplateAndLength(
    JSContext* cx, HandleObject templateObj, int32_t len) {
  MOZ_ASSERT(templateObj->is<TypedArrayObject>());
  TypedArrayObject* tobj = &templateObj->as<TypedArrayObject>();

  switch (tobj->type()) {
#define CREATE_TYPED_ARRAY(_, T, N)                                            \
  case Scalar::N:                                                              \
    return FixedLengthTypedArrayObjectTemplate<T>::makeTypedArrayWithTemplate( \
        cx, tobj, len);
    JS_FOR_EACH_TYPED_ARRAY(CREATE_TYPED_ARRAY)
#undef CREATE_TYPED_ARRAY
    default:
      MOZ_CRASH("Unsupported TypedArray type");
  }
}

TypedArrayObject* js::NewTypedArrayWithTemplateAndArray(
    JSContext* cx, HandleObject templateObj, HandleObject array) {
  MOZ_ASSERT(templateObj->is<TypedArrayObject>());
  TypedArrayObject* tobj = &templateObj->as<TypedArrayObject>();

  switch (tobj->type()) {
#define CREATE_TYPED_ARRAY(_, T, N)                                          \
  case Scalar::N:                                                            \
    return TypedArrayObjectTemplate<T>::makeTypedArrayWithTemplate(cx, tobj, \
                                                                   array);
    JS_FOR_EACH_TYPED_ARRAY(CREATE_TYPED_ARRAY)
#undef CREATE_TYPED_ARRAY
    default:
      MOZ_CRASH("Unsupported TypedArray type");
  }
}

TypedArrayObject* js::NewTypedArrayWithTemplateAndBuffer(
    JSContext* cx, HandleObject templateObj, HandleObject arrayBuffer,
    HandleValue byteOffset, HandleValue length) {
  MOZ_ASSERT(templateObj->is<TypedArrayObject>());
  TypedArrayObject* tobj = &templateObj->as<TypedArrayObject>();

  switch (tobj->type()) {
#define CREATE_TYPED_ARRAY(_, T, N)                                 \
  case Scalar::N:                                                   \
    return TypedArrayObjectTemplate<T>::makeTypedArrayWithTemplate( \
        cx, tobj, arrayBuffer, byteOffset, length);
    JS_FOR_EACH_TYPED_ARRAY(CREATE_TYPED_ARRAY)
#undef CREATE_TYPED_ARRAY
    default:
      MOZ_CRASH("Unsupported TypedArray type");
  }
}

TypedArrayObject* js::NewUint8ArrayWithLength(JSContext* cx, int32_t len,
                                              gc::Heap heap) {
  return TypedArrayObjectTemplate<uint8_t>::fromLength(cx, len, nullptr, heap);
}

template <typename T>
/* static */ TypedArrayObject* TypedArrayObjectTemplate<T>::fromArray(
    JSContext* cx, HandleObject other, HandleObject proto /* = nullptr */) {
  // Allow nullptr proto for FriendAPI methods, which don't care about
  // subclassing.
  if (other->is<TypedArrayObject>()) {
    return fromTypedArray(cx, other, /* wrapped= */ false, proto);
  }

  if (other->is<WrapperObject>() &&
      UncheckedUnwrap(other)->is<TypedArrayObject>()) {
    return fromTypedArray(cx, other, /* wrapped= */ true, proto);
  }

  return fromObject(cx, other, proto);
}

// ES2023 draft rev cf86f1cdc28e809170733d74ea64fd0f3dd79f78
// 23.2.5.1 TypedArray ( ...args )
// 23.2.5.1.2 InitializeTypedArrayFromTypedArray ( O, srcArray )
template <typename T>
/* static */ TypedArrayObject* TypedArrayObjectTemplate<T>::fromTypedArray(
    JSContext* cx, HandleObject other, bool isWrapped, HandleObject proto) {
  MOZ_ASSERT_IF(!isWrapped, other->is<TypedArrayObject>());
  MOZ_ASSERT_IF(isWrapped, other->is<WrapperObject>() &&
                               UncheckedUnwrap(other)->is<TypedArrayObject>());

  Rooted<TypedArrayObject*> srcArray(cx);
  if (!isWrapped) {
    srcArray = &other->as<TypedArrayObject>();
  } else {
    srcArray = other->maybeUnwrapAs<TypedArrayObject>();
    if (!srcArray) {
      ReportAccessDenied(cx);
      return nullptr;
    }
  }

  // InitializeTypedArrayFromTypedArray, step 1. (Skipped)

  // InitializeTypedArrayFromTypedArray, step 2.
  auto srcLength = srcArray->length();
  if (!srcLength) {
    ReportOutOfBounds(cx, srcArray);
    return nullptr;
  }

  // InitializeTypedArrayFromTypedArray, steps 3-7. (Skipped)

  // InitializeTypedArrayFromTypedArray, step 8.
  size_t elementLength = *srcLength;

  // InitializeTypedArrayFromTypedArray, step 9. (Skipped)

  // InitializeTypedArrayFromTypedArray, step 10.a. (Partial)
  // InitializeTypedArrayFromTypedArray, step 11.a.
  Rooted<ArrayBufferObject*> buffer(cx);
  if (!maybeCreateArrayBuffer(cx, elementLength, &buffer)) {
    return nullptr;
  }

  // InitializeTypedArrayFromTypedArray, step 11.b.
  if (Scalar::isBigIntType(ArrayTypeID()) !=
      Scalar::isBigIntType(srcArray->type())) {
    JS_ReportErrorNumberASCII(
        cx, GetErrorMessage, nullptr, JSMSG_TYPED_ARRAY_NOT_COMPATIBLE,
        srcArray->getClass()->name,
        TypedArrayObject::fixedLengthClasses[ArrayTypeID()].name);
    return nullptr;
  }

  // Step 6.b.i.
  // InitializeTypedArrayFromTypedArray, steps 12-15.
  Rooted<TypedArrayObject*> obj(cx, FixedLengthTypedArray::makeInstance(
                                        cx, buffer, 0, elementLength, proto));
  if (!obj) {
    return nullptr;
  }

  MOZ_RELEASE_ASSERT(!srcArray->hasDetachedBuffer());

  // InitializeTypedArrayFromTypedArray, steps 10.a. (Remaining parts)
  // InitializeTypedArrayFromTypedArray, steps 11.c-f.
  MOZ_ASSERT(!obj->isSharedMemory());
  if (srcArray->isSharedMemory()) {
    if (!ElementSpecific<T, SharedOps>::setFromTypedArray(
            obj, elementLength, srcArray, elementLength, 0)) {
      MOZ_ASSERT_UNREACHABLE(
          "setFromTypedArray can only fail for overlapping buffers");
      return nullptr;
    }
  } else {
    if (!ElementSpecific<T, UnsharedOps>::setFromTypedArray(
            obj, elementLength, srcArray, elementLength, 0)) {
      MOZ_ASSERT_UNREACHABLE(
          "setFromTypedArray can only fail for overlapping buffers");
      return nullptr;
    }
  }

  // Step 6.b.v.
  return obj;
}

// ES2023 draft rev cf86f1cdc28e809170733d74ea64fd0f3dd79f78
// 23.2.5.1 TypedArray ( ...args )
// 23.2.5.1.4 InitializeTypedArrayFromList ( O, values )
// 23.2.5.1.5 InitializeTypedArrayFromArrayLike ( O, arrayLike )
template <typename T>
/* static */ TypedArrayObject* TypedArrayObjectTemplate<T>::fromObject(
    JSContext* cx, HandleObject other, HandleObject proto) {
  // Steps 1-4 and 6.a (Already performed in caller).

  // Steps 6.b.i (Allocation deferred until later).

  // Steps 6.b.ii-iii. (Not applicable)

  // Step 6.b.iv.

  // Fast path when iterable is a packed array using the default iterator.
  if (IsArrayWithDefaultIterator<MustBePacked::Yes>(other, cx)) {
    // Steps 6.b.iv.2-3. (We don't need to call IterableToList for the fast
    // path).
    Handle<ArrayObject*> array = other.as<ArrayObject>();

    // InitializeTypedArrayFromList, step 1.
    size_t len = array->getDenseInitializedLength();

    // InitializeTypedArrayFromList, step 2.
    Rooted<ArrayBufferObject*> buffer(cx);
    if (!maybeCreateArrayBuffer(cx, len, &buffer)) {
      return nullptr;
    }

    // Steps 6.b.i.
    Rooted<FixedLengthTypedArrayObject*> obj(
        cx, FixedLengthTypedArray::makeInstance(cx, buffer, 0, len, proto));
    if (!obj) {
      return nullptr;
    }

    // InitializeTypedArrayFromList, steps 3-4.
    MOZ_ASSERT(!obj->isSharedMemory());
    if (!ElementSpecific<T, UnsharedOps>::initFromIterablePackedArray(cx, obj,
                                                                      array)) {
      return nullptr;
    }

    // InitializeTypedArrayFromList, step 5. (The assertion isn't applicable for
    // the fast path).

    // Step 6.b.v.
    return obj;
  }

  // Step 6.b.iv.1 (Assertion; implicit in our implementation).

  // Step 6.b.iv.2.
  RootedValue callee(cx);
  RootedId iteratorId(cx, PropertyKey::Symbol(cx->wellKnownSymbols().iterator));
  if (!GetProperty(cx, other, other, iteratorId, &callee)) {
    return nullptr;
  }

  // Steps 6.b.iv.3-4.
  RootedObject arrayLike(cx);
  if (!callee.isNullOrUndefined()) {
    // Throw if other[Symbol.iterator] isn't callable.
    if (!callee.isObject() || !callee.toObject().isCallable()) {
      RootedValue otherVal(cx, ObjectValue(*other));
      UniqueChars bytes =
          DecompileValueGenerator(cx, JSDVG_SEARCH_STACK, otherVal, nullptr);
      if (!bytes) {
        return nullptr;
      }
      JS_ReportErrorNumberUTF8(cx, GetErrorMessage, nullptr, JSMSG_NOT_ITERABLE,
                               bytes.get());
      return nullptr;
    }

    FixedInvokeArgs<2> args2(cx);
    args2[0].setObject(*other);
    args2[1].set(callee);

    // Step 6.b.iv.3.a.
    RootedValue rval(cx);
    if (!CallSelfHostedFunction(cx, cx->names().IterableToList,
                                UndefinedHandleValue, args2, &rval)) {
      return nullptr;
    }

    // Step 6.b.iv.3.b (Implemented below).
    arrayLike = &rval.toObject();
  } else {
    // Step 4.a is an assertion: object is not an Iterator. Testing this is
    // literally the very last thing we did, so we don't assert here.

    // Step 4.b (Implemented below).
    arrayLike = other;
  }

  // We implement InitializeTypedArrayFromList in terms of
  // InitializeTypedArrayFromArrayLike.

  // InitializeTypedArrayFromArrayLike, step 1.
  uint64_t len;
  if (!GetLengthProperty(cx, arrayLike, &len)) {
    return nullptr;
  }

  // InitializeTypedArrayFromArrayLike, step 2.
  Rooted<ArrayBufferObject*> buffer(cx);
  if (!maybeCreateArrayBuffer(cx, len, &buffer)) {
    return nullptr;
  }

  MOZ_ASSERT(len <= ByteLengthLimit / BYTES_PER_ELEMENT);

  // Steps 6.b.i.
  Rooted<TypedArrayObject*> obj(
      cx, FixedLengthTypedArray::makeInstance(cx, buffer, 0, len, proto));
  if (!obj) {
    return nullptr;
  }

  // InitializeTypedArrayFromArrayLike, steps 3-4.
  MOZ_ASSERT(!obj->isSharedMemory());
  if (!ElementSpecific<T, UnsharedOps>::setFromNonTypedArray(cx, obj, arrayLike,
                                                             len)) {
    return nullptr;
  }

  // Step 6.b.v.
  return obj;
}

static bool TypedArrayConstructor(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                            JSMSG_TYPED_ARRAY_CALL_OR_CONSTRUCT,
                            args.isConstructing() ? "construct" : "call");
  return false;
}

template <typename T>
static bool GetTemplateObjectForLength(JSContext* cx, int32_t length,
                                       MutableHandle<TypedArrayObject*> res) {
  size_t len = size_t(std::max(length, 0));

  size_t nbytes;
  if (!js::CalculateAllocSize<T>(len, &nbytes) ||
      nbytes > TypedArrayObject::ByteLengthLimit) {
    return true;
  }

  res.set(FixedLengthTypedArrayObjectTemplate<T>::makeTemplateObject(cx, len));
  return !!res;
}

template <typename T>
static TypedArrayObject* GetTemplateObjectForBuffer(
    JSContext* cx, Handle<ArrayBufferObjectMaybeShared*> buffer) {
  if (buffer->isResizable()) {
    return ResizableTypedArrayObjectTemplate<T>::makeTemplateObject(cx);
  }

  if (buffer->isImmutable()) {
    return ImmutableTypedArrayObjectTemplate<T>::makeTemplateObject(cx);
  }

  // We don't use the template's length in the object case, so we can create
  // the template typed array with an initial length of zero.
  uint32_t len = 0;

  return FixedLengthTypedArrayObjectTemplate<T>::makeTemplateObject(cx, len);
}

template <typename T>
static TypedArrayObject* GetTemplateObjectForBufferView(
    JSContext* cx, Handle<TypedArrayObject*> bufferView) {
  if (bufferView->is<ResizableTypedArrayObject>()) {
    return ResizableTypedArrayObjectTemplate<T>::makeTemplateObject(cx);
  }

  if (bufferView->is<ImmutableTypedArrayObject>()) {
    return ImmutableTypedArrayObjectTemplate<T>::makeTemplateObject(cx);
  }

  // We don't use the template's length in the object case, so we can create
  // the template typed array with an initial length of zero.
  uint32_t len = 0;

  return FixedLengthTypedArrayObjectTemplate<T>::makeTemplateObject(cx, len);
}

template <typename T>
static TypedArrayObject* GetTemplateObjectForArrayLike(
    JSContext* cx, Handle<JSObject*> arrayLike) {
  MOZ_ASSERT(!arrayLike->is<ArrayBufferObjectMaybeShared>(),
             "Use GetTemplateObjectForBuffer for array buffer objects");
  MOZ_ASSERT(!IsWrapper(arrayLike), "Wrappers not supported");

  // We don't use the template's length in the object case, so we can create
  // the template typed array with an initial length of zero.
  uint32_t len = 0;

  return FixedLengthTypedArrayObjectTemplate<T>::makeTemplateObject(cx, len);
}

/* static */ bool TypedArrayObject::GetTemplateObjectForLength(
    JSContext* cx, Scalar::Type type, int32_t length,
    MutableHandle<TypedArrayObject*> res) {
  MOZ_ASSERT(!res);

  switch (type) {
#define CREATE_TYPED_ARRAY_TEMPLATE(_, T, N) \
  case Scalar::N:                            \
    return ::GetTemplateObjectForLength<T>(cx, length, res);
    JS_FOR_EACH_TYPED_ARRAY(CREATE_TYPED_ARRAY_TEMPLATE)
#undef CREATE_TYPED_ARRAY_TEMPLATE
    default:
      MOZ_CRASH("Unsupported TypedArray type");
  }
}

/* static */ TypedArrayObject* TypedArrayObject::GetTemplateObjectForBuffer(
    JSContext* cx, Scalar::Type type,
    Handle<ArrayBufferObjectMaybeShared*> buffer) {
  switch (type) {
#define CREATE_TYPED_ARRAY_TEMPLATE(_, T, N) \
  case Scalar::N:                            \
    return ::GetTemplateObjectForBuffer<T>(cx, buffer);
    JS_FOR_EACH_TYPED_ARRAY(CREATE_TYPED_ARRAY_TEMPLATE)
#undef CREATE_TYPED_ARRAY_TEMPLATE
    default:
      MOZ_CRASH("Unsupported TypedArray type");
  }
}

/* static */ TypedArrayObject* TypedArrayObject::GetTemplateObjectForBufferView(
    JSContext* cx, Handle<TypedArrayObject*> bufferView) {
  switch (bufferView->type()) {
#define CREATE_TYPED_ARRAY_TEMPLATE(_, T, N) \
  case Scalar::N:                            \
    return ::GetTemplateObjectForBufferView<T>(cx, bufferView);
    JS_FOR_EACH_TYPED_ARRAY(CREATE_TYPED_ARRAY_TEMPLATE)
#undef CREATE_TYPED_ARRAY_TEMPLATE
    default:
      MOZ_CRASH("Unsupported TypedArray type");
  }
}

/* static */ TypedArrayObject* TypedArrayObject::GetTemplateObjectForArrayLike(
    JSContext* cx, Scalar::Type type, Handle<JSObject*> arrayLike) {
  // We don't support wrappers, because of the complicated interaction between
  // wrapped ArrayBuffers and TypedArrays, see |fromBufferWrapped()|.
  MOZ_ASSERT(!IsWrapper(arrayLike));

  switch (type) {
#define CREATE_TYPED_ARRAY_TEMPLATE(_, T, N) \
  case Scalar::N:                            \
    return ::GetTemplateObjectForArrayLike<T>(cx, arrayLike);
    JS_FOR_EACH_TYPED_ARRAY(CREATE_TYPED_ARRAY_TEMPLATE)
#undef CREATE_TYPED_ARRAY_TEMPLATE
    default:
      MOZ_CRASH("Unsupported TypedArray type");
  }
}

static bool LengthGetterImpl(JSContext* cx, const CallArgs& args) {
  auto* tarr = &args.thisv().toObject().as<TypedArrayObject>();
  args.rval().setNumber(tarr->length().valueOr(0));
  return true;
}

static bool TypedArray_lengthGetter(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsTypedArrayObject, LengthGetterImpl>(cx, args);
}

static bool ByteOffsetGetterImpl(JSContext* cx, const CallArgs& args) {
  auto* tarr = &args.thisv().toObject().as<TypedArrayObject>();
  args.rval().setNumber(tarr->byteOffset().valueOr(0));
  return true;
}

static bool TypedArray_byteOffsetGetter(JSContext* cx, unsigned argc,
                                        Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsTypedArrayObject, ByteOffsetGetterImpl>(cx,
                                                                        args);
}

static bool ByteLengthGetterImpl(JSContext* cx, const CallArgs& args) {
  auto* tarr = &args.thisv().toObject().as<TypedArrayObject>();
  args.rval().setNumber(tarr->byteLength().valueOr(0));
  return true;
}

static bool TypedArray_byteLengthGetter(JSContext* cx, unsigned argc,
                                        Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsTypedArrayObject, ByteLengthGetterImpl>(cx,
                                                                        args);
}

static bool BufferGetterImpl(JSContext* cx, const CallArgs& args) {
  MOZ_ASSERT(IsTypedArrayObject(args.thisv()));
  Rooted<TypedArrayObject*> tarray(
      cx, &args.thisv().toObject().as<TypedArrayObject>());
  if (!TypedArrayObject::ensureHasBuffer(cx, tarray)) {
    return false;
  }
  args.rval().set(tarray->bufferValue());
  return true;
}

static bool TypedArray_bufferGetter(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsTypedArrayObject, BufferGetterImpl>(cx, args);
}

// ES2019 draft rev fc9ecdcd74294d0ca3146d4b274e2a8e79565dc3
// 22.2.3.32 get %TypedArray%.prototype [ @@toStringTag ]
static bool TypedArray_toStringTagGetter(JSContext* cx, unsigned argc,
                                         Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // Steps 1-2.
  if (!args.thisv().isObject()) {
    args.rval().setUndefined();
    return true;
  }

  JSObject* obj = CheckedUnwrapStatic(&args.thisv().toObject());
  if (!obj) {
    ReportAccessDenied(cx);
    return false;
  }

  // Step 3.
  if (!obj->is<TypedArrayObject>()) {
    args.rval().setUndefined();
    return true;
  }

  // Steps 4-6.
  JSProtoKey protoKey = StandardProtoKeyOrNull(obj);
  MOZ_ASSERT(protoKey);

  args.rval().setString(ClassName(protoKey, cx));
  return true;
}

/* static */ const JSPropertySpec TypedArrayObject::protoAccessors[] = {
    JS_INLINABLE_PSG("length", TypedArray_lengthGetter, 0, TypedArrayLength),
    JS_PSG("buffer", TypedArray_bufferGetter, 0),
    JS_INLINABLE_PSG("byteLength", TypedArray_byteLengthGetter, 0,
                     TypedArrayByteLength),
    JS_INLINABLE_PSG("byteOffset", TypedArray_byteOffsetGetter, 0,
                     TypedArrayByteOffset),
    JS_SYM_GET(toStringTag, TypedArray_toStringTagGetter, 0),
    JS_PS_END,
};

template <typename T>
static inline bool SetFromTypedArray(TypedArrayObject* target,
                                     size_t targetLength,
                                     TypedArrayObject* source,
                                     size_t sourceLength, size_t offset,
                                     size_t sourceOffset = 0) {
  // WARNING: |source| may be an unwrapped typed array from a different
  // compartment. Proceed with caution!

  if (target->isSharedMemory() || source->isSharedMemory()) {
    return ElementSpecific<T, SharedOps>::setFromTypedArray(
        target, targetLength, source, sourceLength, offset, sourceOffset);
  }
  return ElementSpecific<T, UnsharedOps>::setFromTypedArray(
      target, targetLength, source, sourceLength, offset, sourceOffset);
}

template <typename T>
static inline bool SetFromNonTypedArray(JSContext* cx,
                                        Handle<TypedArrayObject*> target,
                                        HandleObject source, size_t len,
                                        size_t offset) {
  MOZ_ASSERT(!source->is<TypedArrayObject>(), "use SetFromTypedArray");

  if (target->isSharedMemory()) {
    return ElementSpecific<T, SharedOps>::setFromNonTypedArray(
        cx, target, source, len, offset);
  }
  return ElementSpecific<T, UnsharedOps>::setFromNonTypedArray(
      cx, target, source, len, offset);
}

// ES2023 draft rev 22cc56ab08fcab92a865978c0aa5c6f1d8ce250f
// 23.2.3.24.1 SetTypedArrayFromTypedArray ( target, targetOffset, source )
static bool SetTypedArrayFromTypedArray(JSContext* cx,
                                        Handle<TypedArrayObject*> target,
                                        double targetOffset,
                                        size_t targetLength,
                                        Handle<TypedArrayObject*> source) {
  // WARNING: |source| may be an unwrapped typed array from a different
  // compartment. Proceed with caution!

  MOZ_ASSERT(targetOffset >= 0);

  // Steps 1-3. (Performed in caller.)
  MOZ_ASSERT(!target->hasDetachedBuffer());

  // Steps 4-5.
  auto sourceLength = source->length();
  if (!sourceLength) {
    ReportOutOfBounds(cx, source);
    return false;
  }

  // Steps 13-14 (Split into two checks to provide better error messages).
  if (targetOffset > targetLength) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr, JSMSG_BAD_INDEX);
    return false;
  }

  // Step 14 (Cont'd).
  size_t offset = size_t(targetOffset);
  if (*sourceLength > targetLength - offset) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_SOURCE_ARRAY_TOO_LONG);
    return false;
  }

  // Step 15.
  if (Scalar::isBigIntType(target->type()) !=
      Scalar::isBigIntType(source->type())) {
    JS_ReportErrorNumberASCII(
        cx, GetErrorMessage, nullptr, JSMSG_TYPED_ARRAY_NOT_COMPATIBLE,
        source->getClass()->name, target->getClass()->name);
    return false;
  }

  // Steps 6-12, 16-24.
  switch (target->type()) {
#define SET_FROM_TYPED_ARRAY(_, T, N)                                      \
  case Scalar::N:                                                          \
    if (!SetFromTypedArray<T>(target, targetLength, source, *sourceLength, \
                              offset)) {                                   \
      ReportOutOfMemory(cx);                                               \
      return false;                                                        \
    }                                                                      \
    break;
    JS_FOR_EACH_TYPED_ARRAY(SET_FROM_TYPED_ARRAY)
#undef SET_FROM_TYPED_ARRAY
    default:
      MOZ_CRASH("Unsupported TypedArray type");
  }

  return true;
}

// ES2023 draft rev 22cc56ab08fcab92a865978c0aa5c6f1d8ce250f
// 23.2.3.24.1 SetTypedArrayFromArrayLike ( target, targetOffset, source )
static bool SetTypedArrayFromArrayLike(JSContext* cx,
                                       Handle<TypedArrayObject*> target,
                                       double targetOffset, size_t targetLength,
                                       HandleObject src) {
  MOZ_ASSERT(targetOffset >= 0);

  // Steps 1-2. (Performed in caller.)
  MOZ_ASSERT(target->length().isSome());

  // Steps 3-4. (Performed in caller.)

  // Step 5.
  uint64_t srcLength;
  if (!GetLengthProperty(cx, src, &srcLength)) {
    return false;
  }

  // Steps 6-7 (Split into two checks to provide better error messages).
  if (targetOffset > targetLength) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr, JSMSG_BAD_INDEX);
    return false;
  }

  // Step 7 (Cont'd).
  size_t offset = size_t(targetOffset);
  if (srcLength > targetLength - offset) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_SOURCE_ARRAY_TOO_LONG);
    return false;
  }

  MOZ_ASSERT(srcLength <= targetLength);

  // Steps 8-9.
  if (srcLength > 0) {
    switch (target->type()) {
#define SET_FROM_NON_TYPED_ARRAY(_, T, N)                             \
  case Scalar::N:                                                     \
    if (!SetFromNonTypedArray<T>(cx, target, src, srcLength, offset)) \
      return false;                                                   \
    break;
      JS_FOR_EACH_TYPED_ARRAY(SET_FROM_NON_TYPED_ARRAY)
#undef SET_FROM_NON_TYPED_ARRAY
      default:
        MOZ_CRASH("Unsupported TypedArray type");
    }
  }

  // Step 10.
  return true;
}

// ES2023 draft rev 22cc56ab08fcab92a865978c0aa5c6f1d8ce250f
// 23.2.3.24 %TypedArray%.prototype.set ( source [ , offset ] )
// 23.2.3.24.1 SetTypedArrayFromTypedArray ( target, targetOffset, source )
// 23.2.3.24.2 SetTypedArrayFromArrayLike ( target, targetOffset, source )
static bool TypedArray_set(JSContext* cx, const CallArgs& args) {
  MOZ_ASSERT(IsTypedArrayObject(args.thisv()));

  // Steps 1-3 (Validation performed as part of CallNonGenericMethod).
  Rooted<TypedArrayObject*> target(
      cx, &args.thisv().toObject().as<TypedArrayObject>());

  // Additional step from Immutable ArrayBuffer proposal.
  if (target->is<ImmutableTypedArrayObject>()) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_ARRAYBUFFER_IMMUTABLE);
    return false;
  }

  // Steps 4-5.
  double targetOffset = 0;
  if (args.length() > 1) {
    // Step 4.
    if (!ToInteger(cx, args[1], &targetOffset)) {
      return false;
    }

    // Step 5.
    if (targetOffset < 0) {
      JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr, JSMSG_BAD_INDEX);
      return false;
    }
  }

  // 23.2.3.24.1, steps 1-2.
  // 23.2.3.24.2, steps 1-2.
  auto targetLength = target->length();
  if (!targetLength) {
    ReportOutOfBounds(cx, target);
    return false;
  }

  // 23.2.3.24.2, step 4. (23.2.3.24.1 only applies if args[0] is a typed
  // array, so it doesn't make a difference there to apply ToObject here.)
  RootedObject src(cx, ToObject(cx, args.get(0)));
  if (!src) {
    return false;
  }

  Rooted<TypedArrayObject*> srcTypedArray(cx);
  {
    JSObject* obj = CheckedUnwrapStatic(src);
    if (!obj) {
      ReportAccessDenied(cx);
      return false;
    }

    if (obj->is<TypedArrayObject>()) {
      srcTypedArray = &obj->as<TypedArrayObject>();
    }
  }

  // Steps 6-7.
  if (srcTypedArray) {
    if (!SetTypedArrayFromTypedArray(cx, target, targetOffset, *targetLength,
                                     srcTypedArray)) {
      return false;
    }
  } else {
    if (!SetTypedArrayFromArrayLike(cx, target, targetOffset, *targetLength,
                                    src)) {
      return false;
    }
  }

  // Step 8.
  args.rval().setUndefined();
  return true;
}

static bool TypedArray_set(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsTypedArrayObject, TypedArray_set>(cx, args);
}

static bool TypedArraySet(TypedArrayObject* target, TypedArrayObject* source,
                          intptr_t offset) {
  MOZ_ASSERT(offset >= 0);

  size_t targetLength = target->length().valueOr(0);
  size_t sourceLength = source->length().valueOr(0);

  switch (target->type()) {
#define SET_FROM_TYPED_ARRAY(_, T, N)                                       \
  case Scalar::N:                                                           \
    return SetFromTypedArray<T>(target, targetLength, source, sourceLength, \
                                size_t(offset));
    JS_FOR_EACH_TYPED_ARRAY(SET_FROM_TYPED_ARRAY)
#undef SET_FROM_TYPED_ARRAY
    default:
      break;
  }
  MOZ_CRASH("Unsupported TypedArray type");
}

bool js::TypedArraySet(JSContext* cx, TypedArrayObject* target,
                       TypedArrayObject* source, intptr_t offset) {
  if (!::TypedArraySet(target, source, offset)) {
    ReportOutOfMemory(cx);
    return false;
  }
  return true;
}

void js::TypedArraySetInfallible(TypedArrayObject* target,
                                 TypedArrayObject* source, intptr_t offset) {
  AutoUnsafeCallWithABI unsafe;

  MOZ_ALWAYS_TRUE(::TypedArraySet(target, source, offset));
}

static bool TypedArraySetFromSubarray(TypedArrayObject* target,
                                      TypedArrayObject* source, intptr_t offset,
                                      intptr_t sourceOffset,
                                      intptr_t sourceLength) {
  MOZ_ASSERT(offset >= 0);
  MOZ_ASSERT(sourceOffset >= 0);
  MOZ_ASSERT(sourceLength >= 0);

  size_t targetLength = target->length().valueOr(0);

  switch (target->type()) {
#define SET_FROM_TYPED_ARRAY(_, T, N)                                 \
  case Scalar::N:                                                     \
    return SetFromTypedArray<T>(target, targetLength, source,         \
                                size_t(sourceLength), size_t(offset), \
                                size_t(sourceOffset));
    JS_FOR_EACH_TYPED_ARRAY(SET_FROM_TYPED_ARRAY)
#undef SET_FROM_TYPED_ARRAY
    default:
      break;
  }
  MOZ_CRASH("Unsupported TypedArray type");
}

bool js::TypedArraySetFromSubarray(JSContext* cx, TypedArrayObject* target,
                                   TypedArrayObject* source, intptr_t offset,
                                   intptr_t sourceOffset,
                                   intptr_t sourceLength) {
  if (!::TypedArraySetFromSubarray(target, source, offset, sourceOffset,
                                   sourceLength)) {
    ReportOutOfMemory(cx);
    return false;
  }
  return true;
}

void js::TypedArraySetFromSubarrayInfallible(TypedArrayObject* target,
                                             TypedArrayObject* source,
                                             intptr_t offset,
                                             intptr_t sourceOffset,
                                             intptr_t sourceLength) {
  AutoUnsafeCallWithABI unsafe;

  MOZ_ALWAYS_TRUE(::TypedArraySetFromSubarray(target, source, offset,
                                              sourceOffset, sourceLength));
}

// ES2020 draft rev dc1e21c454bd316810be1c0e7af0131a2d7f38e9
// 22.2.3.5 %TypedArray%.prototype.copyWithin ( target, start [ , end ] )
static bool TypedArray_copyWithin(JSContext* cx, const CallArgs& args) {
  MOZ_ASSERT(IsTypedArrayObject(args.thisv()));

  // Steps 1-2.
  Rooted<TypedArrayObject*> tarray(
      cx, &args.thisv().toObject().as<TypedArrayObject>());

  auto arrayLength = tarray->length();
  if (!arrayLength) {
    ReportOutOfBounds(cx, tarray);
    return false;
  }

  // Additional step from Immutable ArrayBuffer proposal.
  if (tarray->is<ImmutableTypedArrayObject>()) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_ARRAYBUFFER_IMMUTABLE);
    return false;
  }

  // Step 3.
  size_t len = *arrayLength;

  // Steps 4-5.
  size_t to = 0;
  if (args.hasDefined(0)) {
    if (!ToIntegerIndex(cx, args[0], len, &to)) {
      return false;
    }
  }

  // Steps 6-7.
  size_t from = 0;
  if (args.hasDefined(1)) {
    if (!ToIntegerIndex(cx, args[1], len, &from)) {
      return false;
    }
  }

  // Steps 8-9.
  size_t final_ = len;
  if (args.hasDefined(2)) {
    if (!ToIntegerIndex(cx, args[2], len, &final_)) {
      return false;
    }
  }

  // Step 10.
  MOZ_ASSERT(to <= len);
  size_t count;
  if (from <= final_) {
    count = std::min(final_ - from, len - to);
  } else {
    count = 0;
  }

  // Step 11.
  //
  // Note that this copies elements effectively by memmove, *not* in
  // step 11's specified order.  This is unobservable, even when the underlying
  // buffer is a SharedArrayBuffer instance, because the access is unordered and
  // therefore is allowed to have data races.

  if (count == 0) {
    args.rval().setObject(*tarray);
    return true;
  }

  // Reacquire the length because side-effects may have detached or resized the
  // array buffer.
  arrayLength = tarray->length();
  if (!arrayLength) {
    ReportOutOfBounds(cx, tarray);
    return false;
  }

  // Recompute the bounds if the current length is smaller.
  if (*arrayLength < len) {
    MOZ_ASSERT(to + count <= len);
    MOZ_ASSERT(from + count <= len);

    len = *arrayLength;

    // Don't copy any bytes if either index is no longer in-bounds.
    if (to >= len || from >= len) {
      args.rval().setObject(*tarray);
      return true;
    }

    // Restrict |count| to not copy any bytes after the end of the array.
    count = std::min(count, std::min(len - to, len - from));
    MOZ_ASSERT(count > 0);
  }

  // Don't multiply by |tarray->bytesPerElement()| in case the compiler can't
  // strength-reduce multiplication by 1/2/4/8 into the equivalent shift.
  const size_t ElementShift = TypedArrayShift(tarray->type());

  MOZ_ASSERT((SIZE_MAX >> ElementShift) > to);
  size_t byteDest = to << ElementShift;

  MOZ_ASSERT((SIZE_MAX >> ElementShift) > from);
  size_t byteSrc = from << ElementShift;

  MOZ_ASSERT((SIZE_MAX >> ElementShift) >= count);
  size_t byteSize = count << ElementShift;

#ifdef DEBUG
  {
    size_t viewByteLength = len << ElementShift;
    MOZ_ASSERT(byteSize <= viewByteLength);
    MOZ_ASSERT(byteDest < viewByteLength);
    MOZ_ASSERT(byteSrc < viewByteLength);
    MOZ_ASSERT(byteDest <= viewByteLength - byteSize);
    MOZ_ASSERT(byteSrc <= viewByteLength - byteSize);
  }
#endif

  if (tarray->isSharedMemory()) {
    auto data = SharedOps::extract(tarray).cast<uint8_t*>();
    SharedOps::memmove(data + byteDest, data + byteSrc, byteSize);
  } else {
    auto data = UnsharedOps::extract(tarray).cast<uint8_t*>();
    UnsharedOps::memmove(data + byteDest, data + byteSrc, byteSize);
  }

  args.rval().setObject(*tarray);
  return true;
}

static bool TypedArray_copyWithin(JSContext* cx, unsigned argc, Value* vp) {
  AutoJSMethodProfilerEntry pseudoFrame(cx, "[TypedArray].prototype",
                                        "copyWithin");
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsTypedArrayObject, TypedArray_copyWithin>(cx,
                                                                         args);
}

template <typename ExternalType, typename NativeType>
static bool TypedArrayJoinKernel(JSContext* cx,
                                 Handle<TypedArrayObject*> tarray, size_t len,
                                 Handle<JSLinearString*> sep,
                                 JSStringBuilder& sb) {
  // Steps 7-8.
  for (size_t k = 0; k < len; k++) {
    if (!CheckForInterrupt(cx)) {
      return false;
    }

    // Step 8.a.
    if (k > 0 && sep->length() > 0 && !sb.append(sep)) {
      return false;
    }

    // Step 8.b-c.
    auto element = TypedArrayObjectTemplate<NativeType>::getIndex(tarray, k);
    if constexpr (std::numeric_limits<NativeType>::is_integer) {
      // Plus one to include the largest number and plus one for the sign.
      constexpr size_t MaximumLength =
          std::numeric_limits<NativeType>::digits10 + 1 +
          std::numeric_limits<NativeType>::is_signed;

      char str[MaximumLength] = {};
      auto result = std::to_chars(str, std::end(str),
                                  static_cast<ExternalType>(element), 10);
      MOZ_ASSERT(result.ec == std::errc());

      size_t strlen = result.ptr - str;
      if (!sb.append(str, strlen)) {
        return false;
      }
    } else {
      ToCStringBuf cbuf;
      size_t strlen;
      char* str = NumberToCString(&cbuf, static_cast<double>(element), &strlen);
      if (!sb.append(str, strlen)) {
        return false;
      }
    }
  }
  return true;
}

/**
 * %TypedArray%.prototype.join ( separator )
 *
 * ES2025 draft rev c4042979a7cdd96b663ffcc43aeee90af8d7a576
 */
static bool TypedArray_join(JSContext* cx, const CallArgs& args) {
  MOZ_ASSERT(IsTypedArrayObject(args.thisv()));

  // Steps 1-3.
  Rooted<TypedArrayObject*> tarray(
      cx, &args.thisv().toObject().as<TypedArrayObject>());

  auto arrayLength = tarray->length();
  if (!arrayLength) {
    ReportOutOfBounds(cx, tarray);
    return false;
  }
  size_t len = *arrayLength;

  // Steps 4-5.
  Rooted<JSLinearString*> sep(cx);
  if (args.hasDefined(0)) {
    JSString* s = ToString<CanGC>(cx, args[0]);
    if (!s) {
      return false;
    }

    sep = s->ensureLinear(cx);
    if (!sep) {
      return false;
    }
  } else {
    sep = cx->names().comma_;
  }

  // Steps 6-9 (When the length is zero, directly return the empty string).
  if (len == 0) {
    args.rval().setString(cx->emptyString());
    return true;
  }

  // Step 6.
  JSStringBuilder sb(cx);
  if (sep->hasTwoByteChars() && !sb.ensureTwoByteChars()) {
    return false;
  }

  // Reacquire the length because side-effects may have detached or resized the
  // array buffer.
  size_t actualLength = std::min(len, tarray->length().valueOr(0));

  // The string representation of each element has at least one character.
  auto res = mozilla::CheckedInt<uint32_t>(actualLength);

  // The separator will be added |length - 1| times, reserve space for that so
  // that we don't have to unnecessarily grow the buffer.
  size_t seplen = sep->length();
  if (seplen > 0) {
    if (len > UINT32_MAX) {
      ReportAllocationOverflow(cx);
      return false;
    }
    res += mozilla::CheckedInt<uint32_t>(seplen) * (uint32_t(len) - 1);
  }
  if (!res.isValid()) {
    ReportAllocationOverflow(cx);
    return false;
  }
  if (!sb.reserve(res.value())) {
    return false;
  }

  switch (tarray->type()) {
#define TYPED_ARRAY_JOIN(ExternalType, NativeType, Name) \
  case Scalar::Name:                                     \
    if (!TypedArrayJoinKernel<ExternalType, NativeType>( \
            cx, tarray, actualLength, sep, sb)) {        \
      return false;                                      \
    }                                                    \
    break;
    JS_FOR_EACH_TYPED_ARRAY(TYPED_ARRAY_JOIN)
#undef TYPED_ARRAY_JOIN
    default:
      MOZ_CRASH("Unsupported TypedArray type");
  }

  for (size_t k = actualLength; k < len; k++) {
    if (!CheckForInterrupt(cx)) {
      return false;
    }

    // Step 8.a.
    if (k > 0 && !sb.append(sep)) {
      return false;
    }

    // Steps 8.b-c. (Not applicable)
  }

  // Step 9.
  JSString* str = sb.finishString();
  if (!str) {
    return false;
  }

  args.rval().setString(str);
  return true;
}

/**
 * %TypedArray%.prototype.join ( separator )
 *
 * ES2025 draft rev c4042979a7cdd96b663ffcc43aeee90af8d7a576
 */
static bool TypedArray_join(JSContext* cx, unsigned argc, Value* vp) {
  AutoJSMethodProfilerEntry pseudoFrame(cx, "[TypedArray].prototype", "join");
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsTypedArrayObject, TypedArray_join>(cx, args);
}

template <typename Ops, typename NativeType>
static int64_t TypedArrayIndexOfNaive(TypedArrayObject* tarray, size_t k,
                                      size_t len, NativeType searchElement) {
  MOZ_RELEASE_ASSERT(k < len);
  MOZ_RELEASE_ASSERT(len <= tarray->length().valueOr(0));

  SharedMem<NativeType*> data =
      Ops::extract(tarray).template cast<NativeType*>();
  for (size_t i = k; i < len; i++) {
    NativeType element = Ops::load(data + i);
    if (element == searchElement) {
      return int64_t(i);
    }
  }
  return -1;
}

template <typename NativeType>
static int64_t TypedArrayIndexOfSIMD(TypedArrayObject* tarray, size_t k,
                                     size_t len, NativeType searchElement) {
  MOZ_RELEASE_ASSERT(k < len);
  MOZ_RELEASE_ASSERT(len <= tarray->length().valueOr(0));

  if constexpr (sizeof(NativeType) == 1) {
    auto* data = UnsharedOps::extract(tarray).cast<char*>().unwrapUnshared();
    auto* ptr = mozilla::SIMD::memchr8(
        data + k, mozilla::BitwiseCast<char>(searchElement), len - k);
    if (!ptr) {
      return -1;
    }
    return int64_t(ptr - data);
  } else if constexpr (sizeof(NativeType) == 2) {
    auto* data =
        UnsharedOps::extract(tarray).cast<char16_t*>().unwrapUnshared();
    auto* ptr = mozilla::SIMD::memchr16(
        data + k, mozilla::BitwiseCast<char16_t>(searchElement), len - k);
    if (!ptr) {
      return -1;
    }
    return int64_t(ptr - data);
  } else if constexpr (sizeof(NativeType) == 4) {
    auto* data =
        UnsharedOps::extract(tarray).cast<uint32_t*>().unwrapUnshared();
    auto* ptr = mozilla::SIMD::memchr32(
        data + k, mozilla::BitwiseCast<uint32_t>(searchElement), len - k);
    if (!ptr) {
      return -1;
    }
    return int64_t(ptr - data);
  } else {
    static_assert(sizeof(NativeType) == 8);

    auto* data =
        UnsharedOps::extract(tarray).cast<uint64_t*>().unwrapUnshared();
    auto* ptr = mozilla::SIMD::memchr64(
        data + k, mozilla::BitwiseCast<uint64_t>(searchElement), len - k);
    if (!ptr) {
      return -1;
    }
    return int64_t(ptr - data);
  }
}

template <typename ExternalType, typename NativeType>
static typename std::enable_if_t<!std::numeric_limits<NativeType>::is_integer,
                                 int64_t>
TypedArrayIndexOf(TypedArrayObject* tarray, size_t k, size_t len,
                  const Value& searchElement) {
  if (!searchElement.isNumber()) {
    return -1;
  }

  double d = searchElement.toNumber();
  NativeType e = NativeType(d);

  // Return early if the search element is not representable using |NativeType|
  // or if it's NaN.
  if (double(e) != d) {
    return -1;
  }
  MOZ_ASSERT(!std::isnan(d));

  if (tarray->isSharedMemory()) {
    return TypedArrayIndexOfNaive<SharedOps>(tarray, k, len, e);
  }
  if (e == NativeType(0)) {
    // Can't use bitwise comparison when searching for ±0.
    return TypedArrayIndexOfNaive<UnsharedOps>(tarray, k, len, e);
  }
  return TypedArrayIndexOfSIMD(tarray, k, len, e);
}

template <typename ExternalType, typename NativeType>
static typename std::enable_if_t<std::numeric_limits<NativeType>::is_integer &&
                                     sizeof(NativeType) < 8,
                                 int64_t>
TypedArrayIndexOf(TypedArrayObject* tarray, size_t k, size_t len,
                  const Value& searchElement) {
  if (!searchElement.isNumber()) {
    return -1;
  }

  int64_t d;
  if (searchElement.isInt32()) {
    d = searchElement.toInt32();
  } else {
    if (!mozilla::NumberEqualsInt64(searchElement.toDouble(), &d)) {
      return -1;
    }
  }

  // Ensure search element is representable using |ExternalType|, which implies
  // it can be represented using |NativeType|.
  mozilla::CheckedInt<ExternalType> checked{d};
  if (!checked.isValid()) {
    return -1;
  }
  NativeType e = static_cast<NativeType>(checked.value());

  if (tarray->isSharedMemory()) {
    return TypedArrayIndexOfNaive<SharedOps>(tarray, k, len, e);
  }
  return TypedArrayIndexOfSIMD(tarray, k, len, e);
}

template <typename ExternalType, typename NativeType>
static typename std::enable_if_t<std::is_same_v<NativeType, int64_t>, int64_t>
TypedArrayIndexOf(TypedArrayObject* tarray, size_t k, size_t len,
                  const Value& searchElement) {
  if (!searchElement.isBigInt()) {
    return -1;
  }

  int64_t e;
  if (!BigInt::isInt64(searchElement.toBigInt(), &e)) {
    return -1;
  }

  if (tarray->isSharedMemory()) {
    return TypedArrayIndexOfNaive<SharedOps>(tarray, k, len, e);
  }
  return TypedArrayIndexOfSIMD(tarray, k, len, e);
}

template <typename ExternalType, typename NativeType>
static typename std::enable_if_t<std::is_same_v<NativeType, uint64_t>, int64_t>
TypedArrayIndexOf(TypedArrayObject* tarray, size_t k, size_t len,
                  const Value& searchElement) {
  if (!searchElement.isBigInt()) {
    return -1;
  }

  uint64_t e;
  if (!BigInt::isUint64(searchElement.toBigInt(), &e)) {
    return -1;
  }

  if (tarray->isSharedMemory()) {
    return TypedArrayIndexOfNaive<SharedOps>(tarray, k, len, e);
  }
  return TypedArrayIndexOfSIMD(tarray, k, len, e);
}

/**
 * %TypedArray%.prototype.indexOf ( searchElement [ , fromIndex ] )
 *
 * ES2025 draft rev c4042979a7cdd96b663ffcc43aeee90af8d7a576
 */
static bool TypedArray_indexOf(JSContext* cx, const CallArgs& args) {
  MOZ_ASSERT(IsTypedArrayObject(args.thisv()));

  // Steps 1-3.
  Rooted<TypedArrayObject*> tarray(
      cx, &args.thisv().toObject().as<TypedArrayObject>());

  auto arrayLength = tarray->length();
  if (!arrayLength) {
    ReportOutOfBounds(cx, tarray);
    return false;
  }
  size_t len = *arrayLength;

  // Step 4.
  if (len == 0) {
    args.rval().setInt32(-1);
    return true;
  }

  // Steps 5-10.
  size_t k = 0;
  if (args.hasDefined(1)) {
    // Steps 5-6.
    if (!ToIntegerIndex(cx, args[1], len, &k)) {
      return false;
    }

    // Reacquire the length because side-effects may have detached or resized
    // the array buffer.
    len = std::min(len, tarray->length().valueOr(0));

    // Return early if |k| exceeds the current length.
    if (k >= len) {
      args.rval().setInt32(-1);
      return true;
    }
  }
  MOZ_ASSERT(k < len);

  // Steps 11-12.
  int64_t result;
  switch (tarray->type()) {
#define TYPED_ARRAY_INDEXOF(ExternalType, NativeType, Name)              \
  case Scalar::Name:                                                     \
    result = TypedArrayIndexOf<ExternalType, NativeType>(tarray, k, len, \
                                                         args.get(0));   \
    break;
    JS_FOR_EACH_TYPED_ARRAY(TYPED_ARRAY_INDEXOF)
#undef TYPED_ARRAY_INDEXOF
    default:
      MOZ_CRASH("Unsupported TypedArray type");
  }
  MOZ_ASSERT_IF(result >= 0, uint64_t(result) < len);
  MOZ_ASSERT_IF(result < 0, result == -1);

  args.rval().setNumber(result);
  return true;
}

/**
 * %TypedArray%.prototype.indexOf ( searchElement [ , fromIndex ] )
 *
 * ES2025 draft rev c4042979a7cdd96b663ffcc43aeee90af8d7a576
 */
static bool TypedArray_indexOf(JSContext* cx, unsigned argc, Value* vp) {
  AutoJSMethodProfilerEntry pseudoFrame(cx, "[TypedArray].prototype",
                                        "indexOf");
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsTypedArrayObject, TypedArray_indexOf>(cx, args);
}

template <typename Ops, typename NativeType>
static int64_t TypedArrayLastIndexOf(TypedArrayObject* tarray, size_t k,
                                     size_t len, NativeType searchElement) {
  MOZ_RELEASE_ASSERT(k < len);
  MOZ_RELEASE_ASSERT(len <= tarray->length().valueOr(0));

  SharedMem<NativeType*> data =
      Ops::extract(tarray).template cast<NativeType*>();
  for (size_t i = k + 1; i > 0;) {
    NativeType element = Ops::load(data + --i);
    if (element == searchElement) {
      return int64_t(i);
    }
  }
  return -1;
}

template <typename ExternalType, typename NativeType>
static typename std::enable_if_t<!std::numeric_limits<NativeType>::is_integer,
                                 int64_t>
TypedArrayLastIndexOf(TypedArrayObject* tarray, size_t k, size_t len,
                      const Value& searchElement) {
  if (!searchElement.isNumber()) {
    return -1;
  }

  double d = searchElement.toNumber();
  NativeType e = NativeType(d);

  // Return early if the search element is not representable using |NativeType|
  // or if it's NaN.
  if (double(e) != d) {
    return -1;
  }
  MOZ_ASSERT(!std::isnan(d));

  if (tarray->isSharedMemory()) {
    return TypedArrayLastIndexOf<SharedOps>(tarray, k, len, e);
  }
  return TypedArrayLastIndexOf<UnsharedOps>(tarray, k, len, e);
}

template <typename ExternalType, typename NativeType>
static typename std::enable_if_t<std::numeric_limits<NativeType>::is_integer &&
                                     sizeof(NativeType) < 8,
                                 int64_t>
TypedArrayLastIndexOf(TypedArrayObject* tarray, size_t k, size_t len,
                      const Value& searchElement) {
  if (!searchElement.isNumber()) {
    return -1;
  }

  int64_t d;
  if (searchElement.isInt32()) {
    d = searchElement.toInt32();
  } else {
    if (!mozilla::NumberEqualsInt64(searchElement.toDouble(), &d)) {
      return -1;
    }
  }

  // Ensure search element is representable using |ExternalType|, which implies
  // it can be represented using |NativeType|.
  mozilla::CheckedInt<ExternalType> checked{d};
  if (!checked.isValid()) {
    return -1;
  }
  NativeType e = static_cast<NativeType>(checked.value());

  if (tarray->isSharedMemory()) {
    return TypedArrayLastIndexOf<SharedOps>(tarray, k, len, e);
  }
  return TypedArrayLastIndexOf<UnsharedOps>(tarray, k, len, e);
}

template <typename ExternalType, typename NativeType>
static typename std::enable_if_t<std::is_same_v<NativeType, int64_t>, int64_t>
TypedArrayLastIndexOf(TypedArrayObject* tarray, size_t k, size_t len,
                      const Value& searchElement) {
  if (!searchElement.isBigInt()) {
    return -1;
  }

  int64_t e;
  if (!BigInt::isInt64(searchElement.toBigInt(), &e)) {
    return -1;
  }

  if (tarray->isSharedMemory()) {
    return TypedArrayLastIndexOf<SharedOps>(tarray, k, len, e);
  }
  return TypedArrayLastIndexOf<UnsharedOps>(tarray, k, len, e);
}

template <typename ExternalType, typename NativeType>
static typename std::enable_if_t<std::is_same_v<NativeType, uint64_t>, int64_t>
TypedArrayLastIndexOf(TypedArrayObject* tarray, size_t k, size_t len,
                      const Value& searchElement) {
  if (!searchElement.isBigInt()) {
    return -1;
  }

  uint64_t e;
  if (!BigInt::isUint64(searchElement.toBigInt(), &e)) {
    return -1;
  }

  if (tarray->isSharedMemory()) {
    return TypedArrayLastIndexOf<SharedOps>(tarray, k, len, e);
  }
  return TypedArrayLastIndexOf<UnsharedOps>(tarray, k, len, e);
}

/**
 * %TypedArray%.prototype.lastIndexOf ( searchElement [ , fromIndex ] )
 *
 * ES2025 draft rev c4042979a7cdd96b663ffcc43aeee90af8d7a576
 */
static bool TypedArray_lastIndexOf(JSContext* cx, const CallArgs& args) {
  MOZ_ASSERT(IsTypedArrayObject(args.thisv()));

  // Steps 1-3.
  Rooted<TypedArrayObject*> tarray(
      cx, &args.thisv().toObject().as<TypedArrayObject>());

  auto arrayLength = tarray->length();
  if (!arrayLength) {
    ReportOutOfBounds(cx, tarray);
    return false;
  }
  size_t len = *arrayLength;

  // Step 4.
  if (len == 0) {
    args.rval().setInt32(-1);
    return true;
  }

  // Steps 5-8.
  size_t k = len - 1;
  if (args.length() > 1) {
    // Step 5.
    double fromIndex;
    if (!ToInteger(cx, args[1], &fromIndex)) {
      return false;
    }

    // Steps 6-8.
    if (fromIndex >= 0) {
      k = size_t(std::min(fromIndex, double(len - 1)));
    } else {
      double d = double(len) + fromIndex;
      if (d < 0) {
        args.rval().setInt32(-1);
        return true;
      }
      k = size_t(d);
    }
    MOZ_ASSERT(k < len);

    // Reacquire the length because side-effects may have detached or resized
    // the array buffer.
    size_t currentLength = tarray->length().valueOr(0);

    // Restrict the search index and length if the new length is smaller.
    if (currentLength < len) {
      // Return early if the new length is zero.
      if (currentLength == 0) {
        args.rval().setInt32(-1);
        return true;
      }

      // Otherwise just restrict |k| and |len| to the current length.
      k = std::min(k, currentLength - 1);
      len = currentLength;
    }
  }
  MOZ_ASSERT(k < len);

  // Steps 9-10.
  int64_t result;
  switch (tarray->type()) {
#define TYPED_ARRAY_LASTINDEXOF(ExternalType, NativeType, Name)              \
  case Scalar::Name:                                                         \
    result = TypedArrayLastIndexOf<ExternalType, NativeType>(tarray, k, len, \
                                                             args.get(0));   \
    break;
    JS_FOR_EACH_TYPED_ARRAY(TYPED_ARRAY_LASTINDEXOF)
#undef TYPED_ARRAY_LASTINDEXOF
    default:
      MOZ_CRASH("Unsupported TypedArray type");
  }
  MOZ_ASSERT_IF(result >= 0, uint64_t(result) < len);
  MOZ_ASSERT_IF(result < 0, result == -1);

  args.rval().setNumber(result);
  return true;
}

/**
 * %TypedArray%.prototype.lastIndexOf ( searchElement [ , fromIndex ] )
 *
 * ES2025 draft rev c4042979a7cdd96b663ffcc43aeee90af8d7a576
 */
static bool TypedArray_lastIndexOf(JSContext* cx, unsigned argc, Value* vp) {
  AutoJSMethodProfilerEntry pseudoFrame(cx, "[TypedArray].prototype",
                                        "lastIndexOf");
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsTypedArrayObject, TypedArray_lastIndexOf>(cx,
                                                                          args);
}

template <typename T>
static inline bool IsNaN(T num) {
  if constexpr (std::is_same_v<T, float16>) {
    return num != num;
  } else {
    // Static analysis complains when using self-comparison for built-in types,
    // so we have to use `std::isnan`.
    return std::isnan(num);
  }
}

template <typename Ops, typename NativeType>
static int64_t TypedArrayIncludesNaN(TypedArrayObject* tarray, size_t k,
                                     size_t len) {
  MOZ_RELEASE_ASSERT(k < len);
  MOZ_RELEASE_ASSERT(len <= tarray->length().valueOr(0));

  SharedMem<NativeType*> data =
      Ops::extract(tarray).template cast<NativeType*>();
  for (size_t i = k; i < len; i++) {
    NativeType element = Ops::load(data + i);
    if (IsNaN(element)) {
      return int64_t(i);
    }
  }
  return -1;
}

template <typename ExternalType, typename NativeType>
static typename std::enable_if_t<!std::numeric_limits<NativeType>::is_integer,
                                 int64_t>
TypedArrayIncludes(TypedArrayObject* tarray, size_t k, size_t len,
                   const Value& searchElement) {
  if (searchElement.isDouble() && std::isnan(searchElement.toDouble())) {
    if (tarray->isSharedMemory()) {
      return TypedArrayIncludesNaN<SharedOps, NativeType>(tarray, k, len);
    }
    return TypedArrayIncludesNaN<UnsharedOps, NativeType>(tarray, k, len);
  }

  // Delegate to TypedArrayIndexOf if not NaN.
  return TypedArrayIndexOf<ExternalType, NativeType>(tarray, k, len,
                                                     searchElement);
}

template <typename ExternalType, typename NativeType>
static typename std::enable_if_t<std::numeric_limits<NativeType>::is_integer,
                                 int64_t>
TypedArrayIncludes(TypedArrayObject* tarray, size_t k, size_t len,
                   const Value& searchElement) {
  // Delegate to TypedArrayIndexOf for integer types.
  return TypedArrayIndexOf<ExternalType, NativeType>(tarray, k, len,
                                                     searchElement);
}

/**
 * %TypedArray%.prototype.includes ( searchElement [ , fromIndex ] )
 *
 * ES2025 draft rev c4042979a7cdd96b663ffcc43aeee90af8d7a576
 */
static bool TypedArray_includes(JSContext* cx, const CallArgs& args) {
  MOZ_ASSERT(IsTypedArrayObject(args.thisv()));

  // Steps 1-3.
  Rooted<TypedArrayObject*> tarray(
      cx, &args.thisv().toObject().as<TypedArrayObject>());

  auto arrayLength = tarray->length();
  if (!arrayLength) {
    ReportOutOfBounds(cx, tarray);
    return false;
  }
  size_t len = *arrayLength;

  // Step 4.
  if (len == 0) {
    args.rval().setBoolean(false);
    return true;
  }

  // Steps 5-10.
  size_t k = 0;
  if (args.hasDefined(1)) {
    if (!ToIntegerIndex(cx, args[1], len, &k)) {
      return false;
    }

    // Reacquire the length because side-effects may have detached or resized
    // the array buffer.
    size_t currentLength = tarray->length().valueOr(0);

    // Contrary to `indexOf`, `includes` doesn't perform `HasProperty`, so we
    // have to handle the case when the current length is smaller than the
    // original length.
    if (currentLength < len) {
      // Accessing an element beyond the typed array length returns `undefined`,
      // so return `true` iff the search element is `undefined`.
      if (k < len && args[0].isUndefined()) {
        args.rval().setBoolean(true);
        return true;
      }

      // Otherwise just restrict |len| to the current length.
      len = currentLength;
    }

    // Return early if |k| exceeds the current length.
    if (k >= len) {
      args.rval().setBoolean(false);
      return true;
    }
  }
  MOZ_ASSERT(k < len);

  // Steps 11-12.
  int64_t result;
  switch (tarray->type()) {
#define TYPED_ARRAY_INCLUDES(ExternalType, NativeType, Name)              \
  case Scalar::Name:                                                      \
    result = TypedArrayIncludes<ExternalType, NativeType>(tarray, k, len, \
                                                          args.get(0));   \
    break;
    JS_FOR_EACH_TYPED_ARRAY(TYPED_ARRAY_INCLUDES)
#undef TYPED_ARRAY_INCLUDES
    default:
      MOZ_CRASH("Unsupported TypedArray type");
  }
  MOZ_ASSERT_IF(result >= 0, uint64_t(result) < len);
  MOZ_ASSERT_IF(result < 0, result == -1);

  args.rval().setBoolean(result >= 0);
  return true;
}

/**
 * %TypedArray%.prototype.includes ( searchElement [ , fromIndex ] )
 *
 * ES2025 draft rev c4042979a7cdd96b663ffcc43aeee90af8d7a576
 */
static bool TypedArray_includes(JSContext* cx, unsigned argc, Value* vp) {
  AutoJSMethodProfilerEntry pseudoFrame(cx, "[TypedArray].prototype",
                                        "includes");
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsTypedArrayObject, TypedArray_includes>(cx,
                                                                       args);
}

template <typename Ops, typename NativeType>
static void TypedArrayFillLoop(TypedArrayObject* tarray, NativeType value,
                               size_t startIndex, size_t endIndex) {
  MOZ_RELEASE_ASSERT(startIndex <= endIndex);
  MOZ_RELEASE_ASSERT(endIndex <= tarray->length().valueOr(0));

  SharedMem<NativeType*> data =
      Ops::extract(tarray).template cast<NativeType*>();
  for (size_t i = startIndex; i < endIndex; i++) {
    Ops::store(data + i, value);
  }
}

template <typename NativeType>
static void TypedArrayFillStdMemset(TypedArrayObject* tarray, uint8_t value,
                                    size_t startIndex, size_t endIndex) {
  MOZ_RELEASE_ASSERT(startIndex <= endIndex);
  MOZ_RELEASE_ASSERT(endIndex <= tarray->length().valueOr(0));

  SharedMem<uint8_t*> data = UnsharedOps::extract(tarray).cast<uint8_t*>();
  std::memset(data.unwrapUnshared() + startIndex * sizeof(NativeType), value,
              (endIndex - startIndex) * sizeof(NativeType));
}

template <typename NativeType>
static void TypedArrayFillAtomicMemset(TypedArrayObject* tarray, uint8_t value,
                                       size_t startIndex, size_t endIndex) {
  MOZ_RELEASE_ASSERT(startIndex <= endIndex);
  MOZ_RELEASE_ASSERT(endIndex <= tarray->length().valueOr(0));

  SharedMem<uint8_t*> data = SharedOps::extract(tarray).cast<uint8_t*>();
  jit::AtomicOperations::memsetSafeWhenRacy(
      data + startIndex * sizeof(NativeType), value,
      (endIndex - startIndex) * sizeof(NativeType));
}

template <typename NativeType>
static NativeType ConvertToNativeType(const Value& value) {
  if constexpr (!std::numeric_limits<NativeType>::is_integer) {
    double d = value.toNumber();

    if (js::SupportDifferentialTesting()) {
      // See the comment in ElementSpecific::doubleToNative.
      d = JS::CanonicalizeNaN(d);
    }

    return ConvertNumber<NativeType>(d);
  } else if constexpr (std::is_same_v<NativeType, int64_t>) {
    return BigInt::toInt64(value.toBigInt());
  } else if constexpr (std::is_same_v<NativeType, uint64_t>) {
    return BigInt::toUint64(value.toBigInt());
  } else {
    return ConvertNumber<NativeType>(value.toNumber());
  }
}

template <typename NativeType>
static void TypedArrayFill(TypedArrayObject* tarray, NativeType val,
                           size_t startIndex, size_t endIndex) {
  using UnsignedT =
      typename mozilla::UnsignedStdintTypeForSize<sizeof(NativeType)>::Type;
  UnsignedT bits = mozilla::BitwiseCast<UnsignedT>(val);

  // Duplicate the LSB to check if we can call memset.
  UnsignedT pattern;
  std::memset(&pattern, uint8_t(bits), sizeof(UnsignedT));

  if (tarray->isSharedMemory()) {
    // To prevent teared writes, only use memset on shared memory when copying
    // single bytes.
    if (bits == pattern && sizeof(NativeType) == 1) {
      TypedArrayFillAtomicMemset<NativeType>(tarray, uint8_t(bits), startIndex,
                                             endIndex);
    } else {
      TypedArrayFillLoop<SharedOps>(tarray, val, startIndex, endIndex);
    }
  } else {
    if (bits == pattern) {
      TypedArrayFillStdMemset<NativeType>(tarray, uint8_t(bits), startIndex,
                                          endIndex);
    } else {
      TypedArrayFillLoop<UnsharedOps>(tarray, val, startIndex, endIndex);
    }
  }
}

template <typename NativeType>
static void TypedArrayFill(TypedArrayObject* tarray, const Value& value,
                           size_t startIndex, size_t endIndex) {
  NativeType val = ConvertToNativeType<NativeType>(value);
  TypedArrayFill(tarray, val, startIndex, endIndex);
}

/**
 * %TypedArray%.prototype.fill ( value [ , start [ , end ] ] )
 *
 * ES2025 draft rev c4042979a7cdd96b663ffcc43aeee90af8d7a576
 */
static bool TypedArray_fill(JSContext* cx, const CallArgs& args) {
  MOZ_ASSERT(IsTypedArrayObject(args.thisv()));

  // Steps 1-2.
  Rooted<TypedArrayObject*> tarray(
      cx, &args.thisv().toObject().as<TypedArrayObject>());

  auto arrayLength = tarray->length();
  if (!arrayLength) {
    ReportOutOfBounds(cx, tarray);
    return false;
  }

  // Additional step from Immutable ArrayBuffer proposal.
  if (tarray->is<ImmutableTypedArrayObject>()) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_ARRAYBUFFER_IMMUTABLE);
    return false;
  }

  // Step 3
  size_t len = *arrayLength;

  // Steps 4-5.
  Rooted<Value> value(cx);
  if (!tarray->convertValue(cx, args.get(0), &value)) {
    return false;
  }

  // Steps 6-9
  size_t startIndex = 0;
  if (args.hasDefined(1)) {
    if (!ToIntegerIndex(cx, args[1], len, &startIndex)) {
      return false;
    }
  }

  // Steps 10-13.
  size_t endIndex = len;
  if (args.hasDefined(2)) {
    if (!ToIntegerIndex(cx, args[2], len, &endIndex)) {
      return false;
    }
  }

  // Steps 14-16.
  //
  // Reacquire the length because side-effects may have detached or resized
  // the array buffer.
  arrayLength = tarray->length();
  if (!arrayLength) {
    ReportOutOfBounds(cx, tarray);
    return false;
  }
  len = *arrayLength;

  // Step 17.
  endIndex = std::min(endIndex, len);

  // Steps 18-19.
  if (startIndex < endIndex) {
    switch (tarray->type()) {
#define TYPED_ARRAY_FILL(_, NativeType, Name)                              \
  case Scalar::Name:                                                       \
    TypedArrayFill<NativeType>(tarray, value.get(), startIndex, endIndex); \
    break;
      JS_FOR_EACH_TYPED_ARRAY(TYPED_ARRAY_FILL)
#undef TYPED_ARRAY_FILL
      default:
        MOZ_CRASH("Unsupported TypedArray type");
    }
  }

  // Step 20.
  args.rval().setObject(*tarray);
  return true;
}

/**
 * %TypedArray%.prototype.fill ( value [ , start [ , end ] ] )
 *
 * ES2025 draft rev c4042979a7cdd96b663ffcc43aeee90af8d7a576
 */
static bool TypedArray_fill(JSContext* cx, unsigned argc, Value* vp) {
  AutoJSMethodProfilerEntry pseudoFrame(cx, "[TypedArray].prototype", "fill");
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsTypedArrayObject, TypedArray_fill>(cx, args);
}

// Test if `ConvertNumber<To, From>` can be instantiated.
//
// For example `ConvertNumber<int64_t, double>` can't be instantiated. This is
// checked through static assertions in `ConvertNumber`.
//
// As a further optimization also avoid generating unreachable code, like for
// example `ConvertNumber<double, float>`.
template <typename To, typename From>
static constexpr bool IsValidForConvertNumber() {
  if constexpr (!std::numeric_limits<From>::is_integer) {
    return !std::numeric_limits<To>::is_integer && sizeof(From) >= sizeof(To);
  } else if constexpr (sizeof(From) == sizeof(int64_t)) {
    return std::numeric_limits<To>::is_integer && sizeof(From) == sizeof(To);
  } else {
    return std::numeric_limits<To>::is_integer && sizeof(From) >= sizeof(To);
  }
}

template <typename T>
static void TypedArrayFillFromJit(TypedArrayObject* obj, T fillValue,
                                  intptr_t start, intptr_t end) {
  if constexpr (!std::numeric_limits<T>::is_integer) {
    MOZ_ASSERT(Scalar::isFloatingType(obj->type()));
  } else if constexpr (std::is_same_v<T, int64_t>) {
    MOZ_ASSERT(Scalar::isBigIntType(obj->type()));
  } else {
    static_assert(std::is_same_v<T, int32_t>);
    MOZ_ASSERT(!Scalar::isFloatingType(obj->type()));
    MOZ_ASSERT(!Scalar::isBigIntType(obj->type()));
  }
  MOZ_ASSERT(!obj->hasDetachedBuffer());
  MOZ_ASSERT(!obj->is<ImmutableTypedArrayObject>());
  MOZ_ASSERT(!obj->is<ResizableTypedArrayObject>());

  size_t length = obj->length().valueOr(0);
  size_t startIndex = ToIntegerIndex(start, length);
  size_t endIndex = ToIntegerIndex(end, length);

  // Return early if the fill range is empty.
  if (startIndex >= endIndex) {
    return;
  }

  switch (obj->type()) {
#define TYPED_ARRAY_FILL(_, NativeType, Name)                               \
  case Scalar::Name:                                                        \
    if constexpr (IsValidForConvertNumber<NativeType, T>()) {               \
      TypedArrayFill<NativeType>(obj, ConvertNumber<NativeType>(fillValue), \
                                 startIndex, endIndex);                     \
      return;                                                               \
    }                                                                       \
    break;
    JS_FOR_EACH_TYPED_ARRAY(TYPED_ARRAY_FILL)
#undef TYPED_ARRAY_FILL
    default:
      MOZ_CRASH("Unsupported TypedArray type");
  }
  MOZ_CRASH("Unexpected invalid number conversion");
}

void js::TypedArrayFillInt32(TypedArrayObject* obj, int32_t fillValue,
                             intptr_t start, intptr_t end) {
  AutoUnsafeCallWithABI unsafe;
  TypedArrayFillFromJit(obj, fillValue, start, end);
}

void js::TypedArrayFillDouble(TypedArrayObject* obj, double fillValue,
                              intptr_t start, intptr_t end) {
  AutoUnsafeCallWithABI unsafe;
  TypedArrayFillFromJit(obj, fillValue, start, end);
}

void js::TypedArrayFillFloat32(TypedArrayObject* obj, float fillValue,
                               intptr_t start, intptr_t end) {
  AutoUnsafeCallWithABI unsafe;
  TypedArrayFillFromJit(obj, fillValue, start, end);
}

void js::TypedArrayFillInt64(TypedArrayObject* obj, int64_t fillValue,
                             intptr_t start, intptr_t end) {
  AutoUnsafeCallWithABI unsafe;
  TypedArrayFillFromJit(obj, fillValue, start, end);
}

void js::TypedArrayFillBigInt(TypedArrayObject* obj, BigInt* fillValue,
                              intptr_t start, intptr_t end) {
  AutoUnsafeCallWithABI unsafe;
  TypedArrayFillFromJit(obj, BigInt::toInt64(fillValue), start, end);
}

template <typename Ops, typename NativeType>
static void TypedArrayReverse(TypedArrayObject* tarray, size_t len) {
  MOZ_RELEASE_ASSERT(len > 0);
  MOZ_RELEASE_ASSERT(len <= tarray->length().valueOr(0));

  SharedMem<NativeType*> lower =
      Ops::extract(tarray).template cast<NativeType*>();
  SharedMem<NativeType*> upper = lower + (len - 1);
  for (; lower < upper; lower++, upper--) {
    NativeType lowerValue = Ops::load(lower);
    NativeType upperValue = Ops::load(upper);

    Ops::store(lower, upperValue);
    Ops::store(upper, lowerValue);
  }
}

template <typename NativeType>
static void TypedArrayReverse(TypedArrayObject* tarray, size_t len) {
  if (tarray->isSharedMemory()) {
    TypedArrayReverse<SharedOps, NativeType>(tarray, len);
  } else {
    TypedArrayReverse<UnsharedOps, NativeType>(tarray, len);
  }
}

/**
 * %TypedArray%.prototype.reverse ( )
 *
 * ES2025 draft rev c4042979a7cdd96b663ffcc43aeee90af8d7a576
 */
static bool TypedArray_reverse(JSContext* cx, const CallArgs& args) {
  MOZ_ASSERT(IsTypedArrayObject(args.thisv()));

  // Steps 1-2.
  Rooted<TypedArrayObject*> tarray(
      cx, &args.thisv().toObject().as<TypedArrayObject>());

  auto arrayLength = tarray->length();
  if (!arrayLength) {
    ReportOutOfBounds(cx, tarray);
    return false;
  }

  // Additional step from Immutable ArrayBuffer proposal.
  if (tarray->is<ImmutableTypedArrayObject>()) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_ARRAYBUFFER_IMMUTABLE);
    return false;
  }

  // Step 3.
  size_t len = *arrayLength;

  // Steps 4-6.
  if (len > 1) {
    switch (tarray->type()) {
#define TYPED_ARRAY_REVERSE(_, NativeType, Name) \
  case Scalar::Name:                             \
    TypedArrayReverse<NativeType>(tarray, len);  \
    break;
      JS_FOR_EACH_TYPED_ARRAY(TYPED_ARRAY_REVERSE)
#undef TYPED_ARRAY_REVERSE
      default:
        MOZ_CRASH("Unsupported TypedArray type");
    }
  }

  // Step 7.
  args.rval().setObject(*tarray);
  return true;
}

/**
 * %TypedArray%.prototype.reverse ( )
 *
 * ES2025 draft rev c4042979a7cdd96b663ffcc43aeee90af8d7a576
 */
static bool TypedArray_reverse(JSContext* cx, unsigned argc, Value* vp) {
  AutoJSMethodProfilerEntry pseudoFrame(cx, "[TypedArray].prototype",
                                        "reverse");
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsTypedArrayObject, TypedArray_reverse>(cx, args);
}

/**
 * TypedArrayCreateSameType ( exemplar, argumentList )
 *
 * ES2025 draft rev c4042979a7cdd96b663ffcc43aeee90af8d7a576
 */
static TypedArrayObject* TypedArrayCreateSameType(
    JSContext* cx, Handle<TypedArrayObject*> exemplar, size_t length) {
  switch (exemplar->type()) {
#define TYPED_ARRAY_CREATE(_, NativeType, Name) \
  case Scalar::Name:                            \
    return TypedArrayObjectTemplate<NativeType>::fromLength(cx, length);
    JS_FOR_EACH_TYPED_ARRAY(TYPED_ARRAY_CREATE)
#undef TYPED_ARRAY_CREATE
    default:
      MOZ_CRASH("Unsupported TypedArray type");
  }
}

/**
 * TypedArrayCreateSameType ( exemplar, argumentList )
 *
 * ES2025 draft rev c4042979a7cdd96b663ffcc43aeee90af8d7a576
 */
static TypedArrayObject* TypedArrayCreateSameType(
    JSContext* cx, Handle<TypedArrayObject*> exemplar,
    Handle<ArrayBufferObjectMaybeShared*> buffer, size_t byteOffset) {
  switch (exemplar->type()) {
#define TYPED_ARRAY_CREATE(_, NativeType, Name)                         \
  case Scalar::Name:                                                    \
    return TypedArrayObjectTemplate<NativeType>::fromBuffer(cx, buffer, \
                                                            byteOffset);
    JS_FOR_EACH_TYPED_ARRAY(TYPED_ARRAY_CREATE)
#undef TYPED_ARRAY_CREATE
    default:
      MOZ_CRASH("Unsupported TypedArray type");
  }
}

/**
 * TypedArrayCreateSameType ( exemplar, argumentList )
 *
 * ES2025 draft rev c4042979a7cdd96b663ffcc43aeee90af8d7a576
 */
static TypedArrayObject* TypedArrayCreateSameType(
    JSContext* cx, Handle<TypedArrayObject*> exemplar,
    Handle<ArrayBufferObjectMaybeShared*> buffer, size_t byteOffset,
    size_t length) {
  switch (exemplar->type()) {
#define TYPED_ARRAY_CREATE(_, NativeType, Name)              \
  case Scalar::Name:                                         \
    return TypedArrayObjectTemplate<NativeType>::fromBuffer( \
        cx, buffer, byteOffset, length);
    JS_FOR_EACH_TYPED_ARRAY(TYPED_ARRAY_CREATE)
#undef TYPED_ARRAY_CREATE
    default:
      MOZ_CRASH("Unsupported TypedArray type");
  }
}

template <typename NativeType>
static void TypedArrayCopyElements(TypedArrayObject* source,
                                   TypedArrayObject* target, size_t length) {
  MOZ_ASSERT(source->type() == target->type());
  MOZ_ASSERT(!target->isSharedMemory());
  MOZ_ASSERT(length > 0);
  MOZ_RELEASE_ASSERT(length <= source->length().valueOr(0));
  MOZ_RELEASE_ASSERT(length <= target->length().valueOr(0));

  auto dest = UnsharedOps::extract(target).cast<NativeType*>();
  if (source->isSharedMemory()) {
    auto src = SharedOps::extract(source).cast<NativeType*>();
    SharedOps::podCopy(dest, src, length);
  } else {
    auto src = UnsharedOps::extract(source).cast<NativeType*>();
    UnsharedOps::podCopy(dest, src, length);
  }
}

static void TypedArrayCopyElements(TypedArrayObject* source,
                                   TypedArrayObject* target, size_t length) {
  switch (source->type()) {
#define TYPED_ARRAY_COPY_ELEMENTS(_, NativeType, Name) \
  case Scalar::Name:                                   \
    return TypedArrayCopyElements<NativeType>(source, target, length);
    JS_FOR_EACH_TYPED_ARRAY(TYPED_ARRAY_COPY_ELEMENTS)
#undef TYPED_ARRAY_COPY_ELEMENTS
    default:
      MOZ_CRASH("Unsupported TypedArray type");
  }
}

/**
 * %TypedArray%.prototype.toReversed ( )
 *
 * ES2025 draft rev c4042979a7cdd96b663ffcc43aeee90af8d7a576
 */
static bool TypedArray_toReversed(JSContext* cx, const CallArgs& args) {
  MOZ_ASSERT(IsTypedArrayObject(args.thisv()));

  // Steps 1-3.
  Rooted<TypedArrayObject*> tarray(
      cx, &args.thisv().toObject().as<TypedArrayObject>());

  auto arrayLength = tarray->length();
  if (!arrayLength) {
    ReportOutOfBounds(cx, tarray);
    return false;
  }
  size_t length = *arrayLength;

  // Step 4.
  TypedArrayObject* result = TypedArrayCreateSameType(cx, tarray, length);
  if (!result) {
    return false;
  }

  // Steps 5-6.
  if (length > 0) {
    TypedArrayCopyElements(tarray, result, length);

    switch (result->type()) {
#define TYPED_ARRAY_TOREVERSED(_, NativeType, Name)             \
  case Scalar::Name:                                            \
    TypedArrayReverse<UnsharedOps, NativeType>(result, length); \
    break;
      JS_FOR_EACH_TYPED_ARRAY(TYPED_ARRAY_TOREVERSED)
#undef TYPED_ARRAY_TOREVERSED
      default:
        MOZ_CRASH("Unsupported TypedArray type");
    }
  }

  // Step 7.
  args.rval().setObject(*result);
  return true;
}

/**
 * %TypedArray%.prototype.toReversed ( )
 *
 * ES2025 draft rev c4042979a7cdd96b663ffcc43aeee90af8d7a576
 */
static bool TypedArray_toReversed(JSContext* cx, unsigned argc, Value* vp) {
  AutoJSMethodProfilerEntry pseudoFrame(cx, "[TypedArray].prototype",
                                        "toReversed");
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsTypedArrayObject, TypedArray_toReversed>(cx,
                                                                         args);
}

template <typename Ops, typename NativeType>
static void TypedArraySetElement(TypedArrayObject* tarray, size_t index,
                                 const Value& value) {
  MOZ_RELEASE_ASSERT(index < tarray->length().valueOr(0));

  NativeType val = ConvertToNativeType<NativeType>(value);

  SharedMem<NativeType*> data =
      Ops::extract(tarray).template cast<NativeType*>();
  Ops::store(data + index, val);
}

/**
 * %TypedArray%.prototype.with ( index, value )
 *
 * ES2025 draft rev c4042979a7cdd96b663ffcc43aeee90af8d7a576
 */
static bool TypedArray_with(JSContext* cx, const CallArgs& args) {
  MOZ_ASSERT(IsTypedArrayObject(args.thisv()));

  // Steps 1-3.
  Rooted<TypedArrayObject*> tarray(
      cx, &args.thisv().toObject().as<TypedArrayObject>());

  auto arrayLength = tarray->length();
  if (!arrayLength) {
    ReportOutOfBounds(cx, tarray);
    return false;
  }
  size_t len = *arrayLength;

  // Step 4.
  double relativeIndex;
  if (!ToInteger(cx, args.get(0), &relativeIndex)) {
    return false;
  }

  // Steps 5-6.
  double actualIndex;
  if (relativeIndex >= 0) {
    actualIndex = relativeIndex;
  } else {
    actualIndex = double(len) + relativeIndex;
  }

  // Steps 7-8.
  Rooted<Value> value(cx);
  if (!tarray->convertValue(cx, args.get(1), &value)) {
    return false;
  }

  // Reacquire the length because side-effects may have detached or resized
  // the array buffer.
  size_t currentLength = tarray->length().valueOr(0);

  // Step 9. (Inlined IsValidIntegerIndex)
  if (actualIndex < 0 || actualIndex >= double(currentLength)) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr, JSMSG_BAD_INDEX);
    return false;
  }
  MOZ_ASSERT(currentLength > 0);

  // Step 10.
  Rooted<TypedArrayObject*> result(cx,
                                   TypedArrayCreateSameType(cx, tarray, len));
  if (!result) {
    return false;
  }

  // Steps 11-12.
  if (len > 0) {
    // Start with copying all elements from |tarray|.
    TypedArrayCopyElements(tarray, result, std::min(len, currentLength));

    // Set the replacement value.
    if (actualIndex < double(len)) {
      switch (result->type()) {
#define TYPED_ARRAY_SET_ELEMENT(_, NativeType, Name)                           \
  case Scalar::Name:                                                           \
    TypedArraySetElement<UnsharedOps, NativeType>(result, size_t(actualIndex), \
                                                  value);                      \
    break;
        JS_FOR_EACH_TYPED_ARRAY(TYPED_ARRAY_SET_ELEMENT)
#undef TYPED_ARRAY_SET_ELEMENT
        default:
          MOZ_CRASH("Unsupported TypedArray type");
      }
    }

    // Fill the remaining elements with `undefined`.
    if (currentLength < len) {
      if (!result->convertValue(cx, UndefinedHandleValue, &value)) {
        return false;
      }

      switch (result->type()) {
#define TYPED_ARRAY_FILL(_, NativeType, Name)                            \
  case Scalar::Name:                                                     \
    TypedArrayFill<NativeType>(result, value.get(), currentLength, len); \
    break;
        JS_FOR_EACH_TYPED_ARRAY(TYPED_ARRAY_FILL)
#undef TYPED_ARRAY_FILL
        default:
          MOZ_CRASH("Unsupported TypedArray type");
      }
    }
  }

  // Step 13.
  args.rval().setObject(*result);
  return true;
}

/**
 * %TypedArray%.prototype.with ( index, value )
 *
 * ES2025 draft rev c4042979a7cdd96b663ffcc43aeee90af8d7a576
 */
static bool TypedArray_with(JSContext* cx, unsigned argc, Value* vp) {
  AutoJSMethodProfilerEntry pseudoFrame(cx, "[TypedArray].prototype", "with");
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsTypedArrayObject, TypedArray_with>(cx, args);
}

/**
 * TypedArrayCreateFromConstructor ( constructor, argumentList )
 */
template <typename... Args>
static TypedArrayObject* TypedArrayCreateFromConstructor(
    JSContext* cx, Handle<JSObject*> constructor, Args... args) {
  // Step 1.
  Rooted<JSObject*> resultObj(cx);
  {
    auto toNumberOrObjectValue = [](auto v) {
      if constexpr (std::is_arithmetic_v<decltype(v)>) {
        return NumberValue(v);
      } else {
        return ObjectValue(*v);
      }
    };

    FixedConstructArgs<sizeof...(args)> cargs(cx);

    size_t i = 0;
    ((cargs[i].set(toNumberOrObjectValue(args)), i++), ...);

    Rooted<Value> ctorVal(cx, ObjectValue(*constructor));
    if (!Construct(cx, ctorVal, cargs, ctorVal, &resultObj)) {
      return nullptr;
    }
  }

  // Step 2.
  auto* unwrapped = resultObj->maybeUnwrapIf<TypedArrayObject>();
  if (!unwrapped) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_NON_TYPED_ARRAY_RETURNED);
    return nullptr;
  }

  auto resultLength = unwrapped->length();
  if (!resultLength) {
    ReportOutOfBounds(cx, unwrapped);
    return nullptr;
  }

  // Step 3. (Assertion not applicable in our implementation.)

  // Step 4.
  if constexpr (sizeof...(args) == 1) {
    // Use nested if-statements because GCC miscompiles `decltype((args, ...))`.
    auto length = (args, ...);
    if constexpr (std::is_arithmetic_v<decltype(length)>) {
      // Additional step from <https://tc39.es/proposal-immutable-arraybuffer>.
      if (unwrapped->is<ImmutableTypedArrayObject>()) {
        JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                  JSMSG_ARRAYBUFFER_IMMUTABLE);
        return nullptr;
      }

      // Steps 4.a-b. (Performed above)

      // Step 4.c.
      if (*resultLength < length) {
        ToCStringBuf lengthBuf;
        ToCStringBuf resultLengthBuf;
        JS_ReportErrorNumberASCII(
            cx, GetErrorMessage, nullptr, JSMSG_SHORT_TYPED_ARRAY_RETURNED,
            NumberToCString(&lengthBuf, length),
            NumberToCString(&resultLengthBuf, *resultLength));
        return nullptr;
      }
    }
  }

  // Step 5.
  return unwrapped;
}

static bool HasBuiltinTypedArraySpecies(TypedArrayObject* obj, JSContext* cx) {
  // Ensure `%TypedArray%.prototype.constructor` and `%TypedArray%[@@species]`
  // haven't been mutated. Ensure concrete `TypedArray.prototype.constructor`
  // and the prototype of `TypedArray.prototype` haven't been mutated.
  if (!cx->realm()->realmFuses.optimizeTypedArraySpeciesFuse.intact()) {
    return false;
  }

  auto protoKey = StandardProtoKeyOrNull(obj);

  // Ensure |obj|'s prototype is the actual concrete TypedArray.prototype.
  auto* proto = cx->global()->maybeGetPrototype(protoKey);
  if (!proto || obj->staticPrototype() != proto) {
    return false;
  }

  // Fail if |obj| has an own `constructor` property.
  if (obj->containsPure(cx->names().constructor)) {
    return false;
  }

  return true;
}

static bool IsTypedArraySpecies(JSContext* cx, JSFunction* species) {
  return IsSelfHostedFunctionWithName(species,
                                      cx->names().dollar_TypedArraySpecies_);
}

/**
 * TypedArraySpeciesCreate ( exemplar, argumentList )
 *
 * ES2026 draft rev 60c4df0b65e1c2e6b6581a5bc08a9311223be1da
 */
template <typename... Args>
static TypedArrayObject* TypedArraySpeciesCreateImpl(
    JSContext* cx, Handle<TypedArrayObject*> exemplar, Args... args) {
  if (HasBuiltinTypedArraySpecies(exemplar, cx)) {
    return TypedArrayCreateSameType(cx, exemplar, args...);
  }

  // Step 1.
  auto ctorKey = StandardProtoKeyOrNull(exemplar);
  Rooted<JSObject*> defaultCtor(
      cx, GlobalObject::getOrCreateConstructor(cx, ctorKey));
  if (!defaultCtor) {
    return nullptr;
  }

  // Steps 1-2.
  Rooted<JSObject*> constructor(
      cx, SpeciesConstructor(cx, exemplar, defaultCtor, IsTypedArraySpecies));
  if (!constructor) {
    return nullptr;
  }

  if (constructor == defaultCtor) {
    return TypedArrayCreateSameType(cx, exemplar, args...);
  }

  // Step 3.
  auto* unwrappedResult =
      TypedArrayCreateFromConstructor(cx, constructor, args...);
  if (!unwrappedResult) {
    return nullptr;
  }

  // Step 4.
  if (Scalar::isBigIntType(exemplar->type()) !=
      Scalar::isBigIntType(unwrappedResult->type())) {
    JS_ReportErrorNumberASCII(
        cx, GetErrorMessage, nullptr, JSMSG_TYPED_ARRAY_NOT_COMPATIBLE,
        exemplar->getClass()->name, unwrappedResult->getClass()->name);
    return nullptr;
  }

  // Step 5.
  return unwrappedResult;
}

/**
 * TypedArraySpeciesCreate ( exemplar, argumentList )
 *
 * ES2026 draft rev 60c4df0b65e1c2e6b6581a5bc08a9311223be1da
 */
static TypedArrayObject* TypedArraySpeciesCreate(
    JSContext* cx, Handle<TypedArrayObject*> exemplar, size_t length) {
  return TypedArraySpeciesCreateImpl(cx, exemplar, length);
}

/**
 * TypedArraySpeciesCreate ( exemplar, argumentList )
 *
 * ES2026 draft rev 60c4df0b65e1c2e6b6581a5bc08a9311223be1da
 */
static TypedArrayObject* TypedArraySpeciesCreate(
    JSContext* cx, Handle<TypedArrayObject*> exemplar,
    Handle<ArrayBufferObjectMaybeShared*> buffer, size_t byteOffset) {
  return TypedArraySpeciesCreateImpl(cx, exemplar, buffer, byteOffset);
}

/**
 * TypedArraySpeciesCreate ( exemplar, argumentList )
 *
 * ES2026 draft rev 60c4df0b65e1c2e6b6581a5bc08a9311223be1da
 */
static TypedArrayObject* TypedArraySpeciesCreate(
    JSContext* cx, Handle<TypedArrayObject*> exemplar,
    Handle<ArrayBufferObjectMaybeShared*> buffer, size_t byteOffset,
    size_t length) {
  return TypedArraySpeciesCreateImpl(cx, exemplar, buffer, byteOffset, length);
}

static void TypedArrayBitwiseSlice(TypedArrayObject* source, size_t startIndex,
                                   size_t count, TypedArrayObject* target) {
  MOZ_ASSERT(CanUseBitwiseCopy(target->type(), source->type()));
  MOZ_ASSERT(!source->hasDetachedBuffer());
  MOZ_ASSERT(!target->hasDetachedBuffer());
  MOZ_ASSERT(!target->is<ImmutableTypedArrayObject>());
  MOZ_ASSERT(count > 0);
  MOZ_ASSERT(startIndex + count <= source->length().valueOr(0));
  MOZ_ASSERT(count <= target->length().valueOr(0));

  size_t elementSize = TypedArrayElemSize(source->type());
  MOZ_ASSERT(elementSize == TypedArrayElemSize(target->type()));

  SharedMem<uint8_t*> sourceData =
      source->dataPointerEither().cast<uint8_t*>() + startIndex * elementSize;

  SharedMem<uint8_t*> targetData = target->dataPointerEither().cast<uint8_t*>();

  size_t byteLength = count * elementSize;

  // The same-type case requires exact copying preserving the bit-level encoding
  // of the source data, so use memcpy if possible. If source and target are the
  // same buffer, we can't use memcpy (or memmove), because the specification
  // requires sequential copying of the values. This case is only possible if a
  // @@species constructor created a specifically crafted typed array. It won't
  // happen in normal code and hence doesn't need to be optimized.
  if (!TypedArrayObject::sameBuffer(source, target)) {
    if (source->isSharedMemory() || target->isSharedMemory()) {
      jit::AtomicOperations::memcpySafeWhenRacy(targetData, sourceData,
                                                byteLength);
    } else {
      std::memcpy(targetData.unwrapUnshared(), sourceData.unwrapUnshared(),
                  byteLength);
    }
  } else {
    for (; byteLength > 0; byteLength--) {
      jit::AtomicOperations::storeSafeWhenRacy(
          targetData++, jit::AtomicOperations::loadSafeWhenRacy(sourceData++));
    }
  }
}

template <typename NativeType>
static double TypedArraySliceCopySlowGet(TypedArrayObject* tarray,
                                         size_t index) {
  return static_cast<double>(
      TypedArrayObjectTemplate<NativeType>::getIndex(tarray, index));
}

template <typename NativeType>
static void TypedArraySliceCopySlowSet(TypedArrayObject* tarray, size_t index,
                                       double value) {
  if constexpr (!std::numeric_limits<NativeType>::is_integer) {
    if (js::SupportDifferentialTesting()) {
      // See the comment in ElementSpecific::doubleToNative.
      value = JS::CanonicalizeNaN(value);
    }
  }
  TypedArrayObjectTemplate<NativeType>::setIndex(
      *tarray, index, ConvertNumber<NativeType>(value));
}

template <>
void TypedArraySliceCopySlowSet<int64_t>(TypedArrayObject* tarray, size_t index,
                                         double value) {
  // Specialization because ConvertNumber doesn't allow double to int64_t.
  MOZ_CRASH("unexpected set with int64_t");
}

template <>
void TypedArraySliceCopySlowSet<uint64_t>(TypedArrayObject* tarray,
                                          size_t index, double value) {
  // Specialization because ConvertNumber doesn't allow double to uint64_t.
  MOZ_CRASH("unexpected set with uint64_t");
}

static void TypedArraySliceCopySlow(TypedArrayObject* source, size_t startIndex,
                                    size_t count, TypedArrayObject* target) {
  MOZ_ASSERT(!CanUseBitwiseCopy(target->type(), source->type()));
  MOZ_ASSERT(!source->hasDetachedBuffer());
  MOZ_ASSERT(!target->hasDetachedBuffer());
  MOZ_ASSERT(!target->is<ImmutableTypedArrayObject>());
  MOZ_ASSERT(count > 0);
  MOZ_ASSERT(startIndex + count <= source->length().valueOr(0));
  MOZ_ASSERT(count <= target->length().valueOr(0));

  static_assert(
      CanUseBitwiseCopy(Scalar::BigInt64, Scalar::BigUint64) &&
          CanUseBitwiseCopy(Scalar::BigUint64, Scalar::BigInt64),
      "BigInt contents, even if sign is different, can be copied bitwise");

  MOZ_ASSERT(!Scalar::isBigIntType(target->type()) &&
             !Scalar::isBigIntType(source->type()));

  // Step 14.h.i.
  size_t n = 0;

  // Step 14.h.ii.
  size_t k = startIndex;

  // Step 14.h.iii.
  while (n < count) {
    // Step 14.h.iii.1. (Not applicable)

    // Step 14.h.iii.2.
    double value;
    switch (source->type()) {
#define GET_ELEMENT(_, T, N)                          \
  case Scalar::N:                                     \
    value = TypedArraySliceCopySlowGet<T>(source, k); \
    break;
      JS_FOR_EACH_TYPED_ARRAY(GET_ELEMENT)
#undef GET_ELEMENT
      case Scalar::MaxTypedArrayViewType:
      case Scalar::Int64:
      case Scalar::Simd128:
        break;
    }

    // Step 14.h.iii.3.
    switch (target->type()) {
#define SET_ELEMENT(_, T, N)                         \
  case Scalar::N:                                    \
    TypedArraySliceCopySlowSet<T>(target, n, value); \
    break;
      JS_FOR_EACH_TYPED_ARRAY(SET_ELEMENT)
#undef SET_ELEMENT
      case Scalar::MaxTypedArrayViewType:
      case Scalar::Int64:
      case Scalar::Simd128:
        break;
    }

    // Step 14.h.iii.4.
    k += 1;

    // Step 14.h.iii.5.
    n += 1;
  }
}

/**
 * %TypedArray%.prototype.slice ( start, end )
 *
 * ES2026 draft rev 60c4df0b65e1c2e6b6581a5bc08a9311223be1da
 */
static bool TypedArray_slice(JSContext* cx, const CallArgs& args) {
  MOZ_ASSERT(IsTypedArrayObject(args.thisv()));

  // Steps 1-3.
  Rooted<TypedArrayObject*> tarray(
      cx, &args.thisv().toObject().as<TypedArrayObject>());

  auto arrayLength = tarray->length();
  if (!arrayLength) {
    ReportOutOfBounds(cx, tarray);
    return false;
  }
  size_t len = *arrayLength;

  // Steps 4-7.
  size_t startIndex = 0;
  if (args.hasDefined(0)) {
    if (!ToIntegerIndex(cx, args[0], len, &startIndex)) {
      return false;
    }
  }

  // Steps 8-11.
  size_t endIndex = len;
  if (args.hasDefined(1)) {
    if (!ToIntegerIndex(cx, args[1], len, &endIndex)) {
      return false;
    }
  }

  // Step 12.
  size_t count = endIndex >= startIndex ? endIndex - startIndex : 0;

  // Step 13.
  Rooted<TypedArrayObject*> unwrappedResult(
      cx, TypedArraySpeciesCreate(cx, tarray, count));
  if (!unwrappedResult) {
    return false;
  }

  // Additional step from <https://tc39.es/proposal-immutable-arraybuffer>.
  MOZ_ASSERT(!unwrappedResult->is<ImmutableTypedArrayObject>());

  // Step 14.
  if (count > 0) {
    // Steps 14.a-b.
    auto arrayLength = tarray->length();
    if (!arrayLength) {
      ReportOutOfBounds(cx, tarray);
      return false;
    }

    // Step 14.c.
    endIndex = std::min(endIndex, *arrayLength);

    // Step 14.d.
    count = endIndex >= startIndex ? endIndex - startIndex : 0;

    // Copy if updated |count| is still non-zero.
    if (count > 0) {
      // Step 14.e.
      auto srcType = tarray->type();

      // Step 14.f.
      auto targetType = unwrappedResult->type();

      // Steps 14.g-h.
      //
      // The specification requires us to perform bitwise copying when |result|
      // and |tarray| have the same type. Additionally, as an optimization, we
      // can also perform bitwise copying when both types have compatible
      // bit-level representations.
      if (MOZ_LIKELY(CanUseBitwiseCopy(targetType, srcType))) {
        TypedArrayBitwiseSlice(tarray, startIndex, count, unwrappedResult);
      } else {
        TypedArraySliceCopySlow(tarray, startIndex, count, unwrappedResult);
      }
    }
  }

  // Step 15.
  if (MOZ_LIKELY(cx->compartment() == unwrappedResult->compartment())) {
    args.rval().setObject(*unwrappedResult);
  } else {
    Rooted<JSObject*> wrappedResult(cx, unwrappedResult);
    if (!cx->compartment()->wrap(cx, &wrappedResult)) {
      return false;
    }
    args.rval().setObject(*wrappedResult);
  }
  return true;
}

/**
 * %TypedArray%.prototype.slice ( start, end )
 *
 * ES2026 draft rev 60c4df0b65e1c2e6b6581a5bc08a9311223be1da
 */
static bool TypedArray_slice(JSContext* cx, unsigned argc, Value* vp) {
  AutoJSMethodProfilerEntry pseudoFrame(cx, "[TypedArray].prototype", "slice");
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsTypedArrayObject, TypedArray_slice>(cx, args);
}

/**
 * %TypedArray%.prototype.subarray ( start, end )
 *
 * ES2026 draft rev 60c4df0b65e1c2e6b6581a5bc08a9311223be1da
 */
static bool TypedArray_subarray(JSContext* cx, const CallArgs& args) {
  MOZ_ASSERT(IsTypedArrayObject(args.thisv()));

  // Steps 1-3.
  Rooted<TypedArrayObject*> tarray(
      cx, &args.thisv().toObject().as<TypedArrayObject>());

  // Step 4.
  if (!TypedArrayObject::ensureHasBuffer(cx, tarray)) {
    return false;
  }
  Rooted<ArrayBufferObjectMaybeShared*> buffer(cx, tarray->bufferEither());

  // Steps 5-7.
  size_t srcLength = tarray->length().valueOr(0);

  // Step 13.
  //
  // Reordered because otherwise it'd be observable that we reset
  // the byteOffset to zero when the underlying array buffer gets detached.
  size_t srcByteOffset = tarray->byteOffsetMaybeOutOfBounds();

  // Steps 8-11.
  size_t startIndex = 0;
  if (args.hasDefined(0)) {
    if (!ToIntegerIndex(cx, args[0], srcLength, &startIndex)) {
      return false;
    }
  }

  // Step 12.
  size_t elementSize = TypedArrayElemSize(tarray->type());

  // Step 14.
  size_t beginByteOffset = srcByteOffset + (startIndex * elementSize);

  // Steps 15-16.
  TypedArrayObject* unwrappedResult;
  if (!args.hasDefined(1) && tarray->is<ResizableTypedArrayObject>() &&
      tarray->as<ResizableTypedArrayObject>().isAutoLength()) {
    // Step 15.a.
    unwrappedResult =
        TypedArraySpeciesCreate(cx, tarray, buffer, beginByteOffset);
  } else {
    // Steps 16.a-d.
    size_t endIndex = srcLength;
    if (args.hasDefined(1)) {
      if (!ToIntegerIndex(cx, args[1], srcLength, &endIndex)) {
        return false;
      }
    }

    // Step 16.e.
    size_t newLength = endIndex >= startIndex ? endIndex - startIndex : 0;

    // Step 16.f.
    unwrappedResult =
        TypedArraySpeciesCreate(cx, tarray, buffer, beginByteOffset, newLength);
  }
  if (!unwrappedResult) {
    return false;
  }

  // Step 17.
  if (MOZ_LIKELY(cx->compartment() == unwrappedResult->compartment())) {
    args.rval().setObject(*unwrappedResult);
  } else {
    Rooted<JSObject*> wrappedResult(cx, unwrappedResult);
    if (!cx->compartment()->wrap(cx, &wrappedResult)) {
      return false;
    }
    args.rval().setObject(*wrappedResult);
  }
  return true;
}

/**
 * %TypedArray%.prototype.subarray ( start, end )
 *
 * ES2026 draft rev 60c4df0b65e1c2e6b6581a5bc08a9311223be1da
 */
static bool TypedArray_subarray(JSContext* cx, unsigned argc, Value* vp) {
  AutoJSMethodProfilerEntry pseudoFrame(cx, "[TypedArray].prototype",
                                        "subarray");
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsTypedArrayObject, TypedArray_subarray>(cx,
                                                                       args);
}

TypedArrayObject* js::TypedArraySubarray(JSContext* cx,
                                         Handle<TypedArrayObject*> obj,
                                         intptr_t start, intptr_t end) {
  MOZ_ASSERT(!obj->hasDetachedBuffer());
  MOZ_ASSERT(!obj->is<ResizableTypedArrayObject>());

  size_t srcLength = obj->length().valueOr(0);

  size_t startIndex = ToIntegerIndex(start, srcLength);
  size_t endIndex = ToIntegerIndex(end, srcLength);

  size_t newLength = endIndex >= startIndex ? endIndex - startIndex : 0;

  return TypedArraySubarrayWithLength(cx, obj, startIndex, newLength);
}

TypedArrayObject* js::TypedArraySubarrayWithLength(
    JSContext* cx, Handle<TypedArrayObject*> obj, intptr_t start,
    intptr_t length) {
  MOZ_ASSERT(!obj->hasDetachedBuffer());
  MOZ_ASSERT(!obj->is<ResizableTypedArrayObject>());
  MOZ_RELEASE_ASSERT(start >= 0);
  MOZ_RELEASE_ASSERT(length >= 0);
  MOZ_RELEASE_ASSERT(size_t(start) + size_t(length) <=
                     obj->length().valueOr(0));

  if (!TypedArrayObject::ensureHasBuffer(cx, obj)) {
    return nullptr;
  }
  Rooted<ArrayBufferObjectMaybeShared*> buffer(cx, obj->bufferEither());

  size_t srcByteOffset = obj->byteOffset().valueOr(0);
  size_t elementSize = TypedArrayElemSize(obj->type());
  size_t beginByteOffset = srcByteOffset + (start * elementSize);

  auto* result =
      TypedArrayCreateSameType(cx, obj, buffer, beginByteOffset, length);

  // Other exceptions aren't allowed, because TypedArraySubarray is a
  // recoverable operation.
  MOZ_ASSERT_IF(!result, cx->isThrowingOutOfMemory());

  return result;
}

static auto* TypedArrayFromDetachedBuffer(JSContext* cx,
                                          Handle<TypedArrayObject*> obj) {
  MOZ_ASSERT(obj->hasDetachedBuffer());

  Rooted<ArrayBufferObject*> buffer(cx, obj->bufferUnshared());

  switch (obj->type()) {
#define TYPED_ARRAY_CREATE(_, NativeType, Name) \
  case Scalar::Name:                            \
    return FixedLengthTypedArrayObjectTemplate< \
        NativeType>::fromDetachedBuffer(cx, buffer);
    JS_FOR_EACH_TYPED_ARRAY(TYPED_ARRAY_CREATE)
#undef TYPED_ARRAY_CREATE
    default:
      MOZ_CRASH("Unsupported TypedArray type");
  }
}

TypedArrayObject* js::TypedArraySubarrayRecover(JSContext* cx,
                                                Handle<TypedArrayObject*> obj,
                                                intptr_t start,
                                                intptr_t length) {
  MOZ_RELEASE_ASSERT(!obj->is<ResizableTypedArrayObject>());
  MOZ_RELEASE_ASSERT(start >= 0);
  MOZ_RELEASE_ASSERT(length >= 0);

  // Special case: The buffer was detached after calling `subarray`. This case
  // can only happen when recovering a TypedArraySubarray allocation.
  if (obj->hasDetachedBuffer()) {
    return TypedArrayFromDetachedBuffer(cx, obj);
  }
  return TypedArraySubarrayWithLength(cx, obj, start, length);
}

static UniqueChars QuoteString(JSContext* cx, char16_t ch) {
  Sprinter sprinter(cx);
  if (!sprinter.init()) {
    return nullptr;
  }

  StringEscape esc{};
  js::EscapePrinter ep(sprinter, esc);
  ep.putChar(ch);

  return sprinter.release();
}

namespace Hex {
static constexpr int8_t InvalidChar = -1;

static constexpr auto DecodeTable() {
  std::array<int8_t, 256> result = {};

  // Initialize all elements to InvalidChar.
  for (auto& e : result) {
    e = InvalidChar;
  }

  // Map the ASCII hexadecimal characters to their values.
  for (uint8_t i = 0; i < 128; ++i) {
    if (mozilla::IsAsciiHexDigit(char(i))) {
      result[i] = mozilla::AsciiAlphanumericToNumber(char(i));
    }
  }

  return result;
}

static constexpr auto Table = DecodeTable();
}  // namespace Hex

/**
 * FromHex ( string [ , maxLength ] )
 *
 * https://tc39.es/proposal-arraybuffer-base64/spec/#sec-fromhex
 */
template <typename Ops, typename CharT>
static size_t FromHex(const CharT* chars, size_t length,
                      TypedArrayObject* tarray) {
  auto data = Ops::extract(tarray).template cast<uint8_t*>();

  static_assert(std::size(Hex::Table) == 256,
                "can access decode table using Latin-1 character");

  auto decodeChar = [&](CharT ch) -> int32_t {
    if constexpr (sizeof(CharT) == 1) {
      return Hex::Table[ch];
    } else {
      return ch <= 255 ? Hex::Table[ch] : Hex::InvalidChar;
    }
  };

  auto decode2Chars = [&](const CharT* chars) {
    return (decodeChar(chars[0]) << 4) | (decodeChar(chars[1]) << 0);
  };

  auto decode4Chars = [&](const CharT* chars) {
    return (decodeChar(chars[2]) << 12) | (decodeChar(chars[3]) << 8) |
           (decodeChar(chars[0]) << 4) | (decodeChar(chars[1]) << 0);
  };

  // Step 4.
  size_t index = 0;

  // Step 5. (Checked in caller.)
  MOZ_ASSERT(length % 2 == 0);

  // Process eight characters per loop iteration.
  if (length >= 8) {
    // Align |data| to uint32_t.
    if (MOZ_UNLIKELY(data.unwrapValue() & 3)) {
      // Performs at most three iterations until |data| is aligned, reading up
      // to six characters.
      while (data.unwrapValue() & 3) {
        // Step 6.a and 6.d.
        uint32_t byte = decode2Chars(chars + index);

        // Step 6.b.
        if (MOZ_UNLIKELY(int32_t(byte) < 0)) {
          return index;
        }
        MOZ_ASSERT(byte <= 0xff);

        // Step 6.c.
        index += 2;

        // Step 6.e.
        Ops::store(data++, uint8_t(byte));
      }
    }

    auto data32 = data.template cast<uint32_t*>();

    // Step 6.
    size_t lastValidIndex = length - 8;
    while (index <= lastValidIndex) {
      // Steps 6.a and 6.d.
      uint32_t word1 = decode4Chars(chars + index);

      // Step 6.b.
      if (MOZ_UNLIKELY(int32_t(word1) < 0)) {
        break;
      }
      MOZ_ASSERT(word1 <= 0xffff);

      // Step 6.a and 6.d.
      uint32_t word2 = decode4Chars(chars + index + 4);

      // Step 6.b.
      if (MOZ_UNLIKELY(int32_t(word2) < 0)) {
        break;
      }
      MOZ_ASSERT(word2 <= 0xffff);

      // Step 6.c.
      index += 4 * 2;

      // Step 6.e.
      //
      // The word was constructed in little-endian order, so in the unlikely
      // case of a big-endian system we have to swap it.
      uint32_t word =
          mozilla::NativeEndian::swapFromLittleEndian((word2 << 16) | word1);
      Ops::store(data32++, word);
    }

    data = data32.template cast<uint8_t*>();
  }

  // Step 6.
  while (index < length) {
    // Step 6.a and 6.d.
    uint32_t byte = decode2Chars(chars + index);

    // Step 6.b.
    if (MOZ_UNLIKELY(int32_t(byte) < 0)) {
      return index;
    }
    MOZ_ASSERT(byte <= 0xff);

    // Step 6.c.
    index += 2;

    // Step 6.e.
    Ops::store(data++, uint8_t(byte));
  }

  // Step 7.
  return index;
}

/**
 * FromHex ( string [ , maxLength ] )
 *
 * https://tc39.es/proposal-arraybuffer-base64/spec/#sec-fromhex
 */
template <typename Ops>
static size_t FromHex(JSLinearString* linear, size_t length,
                      TypedArrayObject* tarray) {
  JS::AutoCheckCannotGC nogc;
  if (linear->hasLatin1Chars()) {
    return FromHex<Ops>(linear->latin1Chars(nogc), length, tarray);
  }
  return FromHex<Ops>(linear->twoByteChars(nogc), length, tarray);
}

/**
 * FromHex ( string [ , maxLength ] )
 *
 * https://tc39.es/proposal-arraybuffer-base64/spec/#sec-fromhex
 */
static bool FromHex(JSContext* cx, JSString* string, size_t maxLength,
                    TypedArrayObject* tarray) {
  MOZ_ASSERT(tarray->type() == Scalar::Uint8);

  // The underlying buffer must neither be detached nor shrunk. (It may have
  // been grown when it's a growable shared buffer and a concurrent thread
  // resized the buffer.)
  MOZ_ASSERT(!tarray->hasDetachedBuffer());
  MOZ_ASSERT(tarray->length().valueOr(0) >= maxLength);

  // Step 1. (Not applicable in our implementation.)

  // Step 2.
  //
  // Each byte is encoded in two characters.
  size_t readLength = maxLength * 2;
  MOZ_ASSERT(readLength <= string->length());

  // Step 3. (Not applicable in our implementation.)

  JSLinearString* linear = string->ensureLinear(cx);
  if (!linear) {
    return false;
  }

  // Steps 4 and 6-7.
  size_t index;
  if (tarray->isSharedMemory()) {
    index = FromHex<SharedOps>(linear, readLength, tarray);
  } else {
    index = FromHex<UnsharedOps>(linear, readLength, tarray);
  }
  if (MOZ_UNLIKELY(index < readLength)) {
    char16_t c0 = linear->latin1OrTwoByteChar(index);
    char16_t c1 = linear->latin1OrTwoByteChar(index + 1);
    char16_t ch = !mozilla::IsAsciiHexDigit(c0) ? c0 : c1;
    if (auto str = QuoteString(cx, ch)) {
      JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                JSMSG_TYPED_ARRAY_BAD_HEX_DIGIT, str.get());
    }
    return false;
  }
  return true;
}

namespace Base64 {
static constexpr int8_t InvalidChar = -1;

static constexpr auto DecodeTable(const char (&alphabet)[65]) {
  std::array<int8_t, 256> result = {};

  // Initialize all elements to InvalidChar.
  for (auto& e : result) {
    e = InvalidChar;
  }

  // Map the base64 characters to their values.
  for (uint8_t i = 0; i < 64; ++i) {
    result[alphabet[i]] = i;
  }

  return result;
}
}  // namespace Base64

namespace Base64::Encode {
static constexpr const char Base64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static_assert(std::char_traits<char>::length(Base64) == 64);

static constexpr const char Base64Url[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
static_assert(std::char_traits<char>::length(Base64Url) == 64);
}  // namespace Base64::Encode

namespace Base64::Decode {
static constexpr auto Base64 = DecodeTable(Base64::Encode::Base64);
static_assert(Base64.size() == 256,
              "256 elements to allow access through Latin-1 characters");

static constexpr auto Base64Url = DecodeTable(Base64::Encode::Base64Url);
static_assert(Base64Url.size() == 256,
              "256 elements to allow access through Latin-1 characters");
}  // namespace Base64::Decode

enum class Alphabet {
  /**
   * Standard base64 alphabet.
   */
  Base64,

  /**
   * URL and filename safe base64 alphabet.
   */
  Base64Url,
};

enum class LastChunkHandling {
  /**
   * Allow partial chunks at the end of the input.
   */
  Loose,

  /**
   * Disallow partial chunks at the end of the input.
   */
  Strict,

  /**
   * Stop before partial chunks at the end of the input.
   */
  StopBeforePartial,
};

enum class Base64Error {
  None,
  BadChar,
  BadCharAfterPadding,
  IncompleteChunk,
  MissingPadding,
  ExtraBits,
};

struct Base64Result {
  Base64Error error;
  size_t index;
  size_t written;

  bool isError() const { return error != Base64Error::None; }

  static auto Ok(size_t index, size_t written) {
    return Base64Result{Base64Error::None, index, written};
  }

  static auto Error(Base64Error error) {
    MOZ_ASSERT(error != Base64Error::None);
    return Base64Result{error, 0, 0};
  }

  static auto ErrorAt(Base64Error error, size_t index) {
    MOZ_ASSERT(error != Base64Error::None);
    return Base64Result{error, index, 0};
  }
};

static void ReportBase64Error(JSContext* cx, Base64Result result,
                              JSLinearString* string) {
  MOZ_ASSERT(result.isError());
  switch (result.error) {
    case Base64Error::None:
      break;
    case Base64Error::BadChar:
      if (auto str =
              QuoteString(cx, string->latin1OrTwoByteChar(result.index))) {
        JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                  JSMSG_TYPED_ARRAY_BAD_BASE64_CHAR, str.get());
      }
      return;
    case Base64Error::BadCharAfterPadding:
      if (auto str =
              QuoteString(cx, string->latin1OrTwoByteChar(result.index))) {
        JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                  JSMSG_TYPED_ARRAY_BAD_BASE64_AFTER_PADDING,
                                  str.get());
      }
      return;
    case Base64Error::IncompleteChunk:
      JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                JSMSG_TYPED_ARRAY_BAD_INCOMPLETE_CHUNK);
      return;
    case Base64Error::MissingPadding:
      JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                JSMSG_TYPED_ARRAY_MISSING_BASE64_PADDING);
      return;
    case Base64Error::ExtraBits:
      JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                JSMSG_TYPED_ARRAY_EXTRA_BASE64_BITS);
      return;
  }
  MOZ_CRASH("unexpected base64 error");
}

/**
 * FromBase64 ( string, alphabet, lastChunkHandling [ , maxLength ] )
 *
 * https://tc39.es/proposal-arraybuffer-base64/spec/#sec-frombase64
 */
template <class Ops, typename CharT>
static auto FromBase64(const CharT* chars, size_t length, Alphabet alphabet,
                       LastChunkHandling lastChunkHandling,
                       SharedMem<uint8_t*> data, size_t maxLength) {
  const SharedMem<uint8_t*> dataBegin = data;
  const SharedMem<uint8_t*> dataEnd = data + maxLength;

  auto canAppend = [&](size_t n) { return data + n <= dataEnd; };

  auto written = [&]() { return data.unwrap() - dataBegin.unwrap(); };

  // DecodeBase64Chunk ( chunk [ , throwOnExtraBits ] )
  //
  // Encode a complete base64 chunk.
  auto decodeChunk = [&](uint32_t chunk) {
    MOZ_ASSERT(chunk <= 0xffffff);
    MOZ_ASSERT(canAppend(3));
    Ops::store(data++, uint8_t(chunk >> 16));
    Ops::store(data++, uint8_t(chunk >> 8));
    Ops::store(data++, uint8_t(chunk));
  };

  // DecodeBase64Chunk ( chunk [ , throwOnExtraBits ] )
  //
  // Encode a three element partial base64 chunk.
  auto decodeChunk3 = [&](uint32_t chunk) {
    MOZ_ASSERT(chunk <= 0x3ffff);
    MOZ_ASSERT(canAppend(2));
    Ops::store(data++, uint8_t(chunk >> 10));
    Ops::store(data++, uint8_t(chunk >> 2));
  };

  // DecodeBase64Chunk ( chunk [ , throwOnExtraBits ] )
  //
  // Encode a two element partial base64 chunk.
  auto decodeChunk2 = [&](uint32_t chunk) {
    MOZ_ASSERT(chunk <= 0xfff);
    MOZ_ASSERT(canAppend(1));
    Ops::store(data++, uint8_t(chunk >> 4));
  };

  // DecodeBase64Chunk ( chunk [ , throwOnExtraBits ] )
  //
  // Encode a partial base64 chunk.
  auto decodePartialChunk = [&](uint32_t chunk, uint32_t chunkLength) {
    MOZ_ASSERT(chunkLength == 2 || chunkLength == 3);
    chunkLength == 2 ? decodeChunk2(chunk) : decodeChunk3(chunk);
  };

  // Steps 1-2. (Not applicable in our implementation.)

  // Step 3.
  if (maxLength == 0) {
    return Base64Result::Ok(0, 0);
  }
  MOZ_ASSERT(canAppend(1), "can append at least one byte if maxLength > 0");

  // Step 8.
  //
  // Current string index.
  size_t index = 0;

  // Step 9. (Passed as parameter)

  static_assert(std::size(Base64::Decode::Base64) == 256 &&
                    std::size(Base64::Decode::Base64Url) == 256,
                "can access decode tables using Latin-1 character");

  const auto& decode = alphabet == Alphabet::Base64 ? Base64::Decode::Base64
                                                    : Base64::Decode::Base64Url;

  auto decodeChar = [&](CharT ch) -> int32_t {
    if constexpr (sizeof(CharT) == 1) {
      return decode[ch];
    } else {
      return ch <= 255 ? decode[ch] : Base64::InvalidChar;
    }
  };

  auto decode4Chars = [&](const CharT* chars) {
    return (decodeChar(chars[0]) << 18) | (decodeChar(chars[1]) << 12) |
           (decodeChar(chars[2]) << 6) | (decodeChar(chars[3]));
  };

  // Initial loop to process only full chunks. Doesn't perform any error
  // reporting and expects that at least four characters can be read per loop
  // iteration and that the output has enough space for a decoded chunk.
  if (length >= 4) {
    size_t lastValidIndex = length - 4;
    while (canAppend(3) && index <= lastValidIndex) {
      // Fast path: Read four consecutive characters.

      // Step 10.a. (Performed in slow path.)

      // Step 10.b. (Moved out of loop.)

      // Steps 10.c and 10.e-g.
      uint32_t chunk = decode4Chars(chars + index);

      // Steps 10.h-i. (Not applicable in this loop.)

      // Steps 10.d and 10.j-l.
      if (MOZ_LIKELY(int32_t(chunk) >= 0)) {
        // Step 10.j-l.
        decodeChunk(chunk);

        // Step 10.d.
        index += 4;
        continue;
      }

      // Slow path: Read four characters, ignoring whitespace.

      // Steps 10.a and 10.b.
      CharT part[4];
      size_t i = index;
      size_t j = 0;
      while (i < length && j < 4) {
        auto ch = chars[i++];

        // Step 10.a.
        if (mozilla::IsAsciiWhitespace(ch)) {
          continue;
        }

        // Step 10.c.
        part[j++] = ch;
      }

      // Steps 10.d-l.
      if (MOZ_LIKELY(j == 4)) {
        // Steps 10.e-g.
        uint32_t chunk = decode4Chars(part);

        // Steps 10.h-i. (Not applicable in this loop.)

        // Steps 10.d and 10.j-l.
        if (MOZ_LIKELY(int32_t(chunk) >= 0)) {
          // Step 10.j-l.
          decodeChunk(chunk);

          // Step 10.d.
          index = i;
          continue;
        }
      }

      // Padding or invalid characters, or end of input. The next loop will
      // process any characters left in the input.
      break;
    }

    // Step 10.b.ii.
    if (index == length) {
      return Base64Result::Ok(length, written());
    }

    // Step 10.l.v. (Reordered)
    if (!canAppend(1)) {
      MOZ_ASSERT(written() > 0);
      return Base64Result::Ok(index, written());
    }
  }

  // Step 4.
  //
  // String index after the last fully read base64 chunk.
  size_t read = index;

  // Step 5. (Not applicable in our implementation.)

  // Step 6.
  //
  // Current base64 chunk, a uint24 number.
  uint32_t chunk = 0;

  // Step 7.
  //
  // Current base64 chunk length, in the range [0..4].
  size_t chunkLength = 0;

  // Step 10.
  for (; index < length; index++) {
    // Step 10.c. (Reordered)
    auto ch = chars[index];

    // Step 10.a.
    if (mozilla::IsAsciiWhitespace(ch)) {
      continue;
    }

    // Step 10.b. (Moved out of loop.)

    // Step 10.d. (Performed in for-loop step.)

    // Step 10.e.
    if (ch == '=') {
      break;
    }

    // Steps 10.f-g.
    uint32_t value = decodeChar(ch);
    if (MOZ_UNLIKELY(int32_t(value) < 0)) {
      return Base64Result::ErrorAt(Base64Error::BadChar, index);
    }
    MOZ_ASSERT(value <= 0x7f);

    // Step 10.h. (Not applicable in our implementation.)

    // Step 10.i.
    if (chunkLength > 1 && !canAppend(chunkLength)) {
      return Base64Result::Ok(read, written());
    }

    // Step 10.j.
    chunk = (chunk << 6) | value;

    // Step 10.k.
    chunkLength += 1;

    // Step 10.l. (Full chunks are processed in the initial loop.)
    MOZ_ASSERT(chunkLength < 4);
  }

  // Step 10.b.
  if (index == length) {
    // Step 10.b.i.
    if (chunkLength > 0) {
      // Step 10.b.i.1.
      if (lastChunkHandling == LastChunkHandling::StopBeforePartial) {
        return Base64Result::Ok(read, written());
      }

      // Steps 10.b.i.2-3.
      if (lastChunkHandling == LastChunkHandling::Loose) {
        // Step 10.b.i.2.a.
        if (chunkLength == 1) {
          return Base64Result::Error(Base64Error::IncompleteChunk);
        }
        MOZ_ASSERT(chunkLength == 2 || chunkLength == 3);

        // Step 10.b.i.2.b.
        decodePartialChunk(chunk, chunkLength);
      } else {
        // Step 10.b.i.3.a.
        MOZ_ASSERT(lastChunkHandling == LastChunkHandling::Strict);

        // Step 10.b.i.3.b.
        return Base64Result::Error(Base64Error::IncompleteChunk);
      }
    }

    // Step 10.b.ii.
    return Base64Result::Ok(length, written());
  }

  // Step 10.e.
  MOZ_ASSERT(index < length);
  MOZ_ASSERT(chars[index] == '=');

  // Step 10.e.i.
  if (chunkLength < 2) {
    return Base64Result::Error(Base64Error::IncompleteChunk);
  }
  MOZ_ASSERT(chunkLength == 2 || chunkLength == 3);

  // Step 10.e.ii. (Inlined SkipAsciiWhitespace)
  while (++index < length) {
    auto ch = chars[index];
    if (!mozilla::IsAsciiWhitespace(ch)) {
      break;
    }
  }

  // Step 10.e.iii.
  if (chunkLength == 2) {
    // Step 10.e.iii.1.
    if (index == length) {
      // Step 10.e.iii.1.a.
      if (lastChunkHandling == LastChunkHandling::StopBeforePartial) {
        return Base64Result::Ok(read, written());
      }

      // Step 10.e.iii.1.b.
      return Base64Result::Error(Base64Error::MissingPadding);
    }

    // Step 10.e.iii.2.
    auto ch = chars[index];

    // Step 10.e.iii.3.
    if (ch == '=') {
      // Step 10.e.iii.3.a. (Inlined SkipAsciiWhitespace)
      while (++index < length) {
        auto ch = chars[index];
        if (!mozilla::IsAsciiWhitespace(ch)) {
          break;
        }
      }
    }
  }

  // Step 10.e.iv.
  if (index < length) {
    return Base64Result::ErrorAt(Base64Error::BadCharAfterPadding, index);
  }

  // Steps 10.e.v-vi.
  if (lastChunkHandling == LastChunkHandling::Strict) {
    uint32_t extraBitsMask = chunkLength == 2 ? 0xf : 0x3;
    if ((chunk & extraBitsMask) != 0) {
      return Base64Result::Error(Base64Error::ExtraBits);
    }
  }

  // Step 10.e.vii.
  decodePartialChunk(chunk, chunkLength);

  // Step 10.e.viii.
  return Base64Result::Ok(length, written());
}

/**
 * FromBase64 ( string, alphabet, lastChunkHandling [ , maxLength ] )
 *
 * https://tc39.es/proposal-arraybuffer-base64/spec/#sec-frombase64
 */
template <class Ops>
static auto FromBase64(JSLinearString* string, Alphabet alphabet,
                       LastChunkHandling lastChunkHandling,
                       SharedMem<uint8_t*> data, size_t maxLength) {
  JS::AutoCheckCannotGC nogc;
  if (string->hasLatin1Chars()) {
    return FromBase64<Ops>(string->latin1Chars(nogc), string->length(),
                           alphabet, lastChunkHandling, data, maxLength);
  }
  return FromBase64<Ops>(string->twoByteChars(nogc), string->length(), alphabet,
                         lastChunkHandling, data, maxLength);
}

/**
 * FromBase64 ( string, alphabet, lastChunkHandling [ , maxLength ] )
 *
 * https://tc39.es/proposal-arraybuffer-base64/spec/#sec-frombase64
 */
static auto FromBase64(JSLinearString* string, Alphabet alphabet,
                       LastChunkHandling lastChunkHandling,
                       TypedArrayObject* tarray, size_t maxLength) {
  MOZ_ASSERT(tarray->type() == Scalar::Uint8);

  // The underlying buffer must neither be detached nor shrunk. (It may have
  // been grown when it's a growable shared buffer and a concurrent thread
  // resized the buffer.)
  MOZ_ASSERT(!tarray->hasDetachedBuffer());
  MOZ_ASSERT(tarray->length().valueOr(0) >= maxLength);

  auto data = tarray->dataPointerEither().cast<uint8_t*>();

  if (tarray->isSharedMemory()) {
    return FromBase64<SharedOps>(string, alphabet, lastChunkHandling, data,
                                 maxLength);
  }
  return FromBase64<UnsharedOps>(string, alphabet, lastChunkHandling, data,
                                 maxLength);
}

/**
 * Allocate uint8_t array for base64 decoding, followed by transformation
 * to TypedArrayObject.
 */
class MOZ_NON_PARAM Uint8Buffer {
  static constexpr size_t InlineLength =
      FixedLengthTypedArrayObject::INLINE_BUFFER_LIMIT;

  uint8_t inlineBuf_[InlineLength];
  UniquePtr<uint8_t[], JS::FreePolicy> ownedBuf_;

  size_t length_ = 0;

 public:
  size_t length() { return length_; };

  uint8_t* data() { return ownedBuf_ ? ownedBuf_.get() : inlineBuf_; };

  /**
   * Allocate uint8_t array for base64 decoding. If the length is small enough
   * use the inlineBuf.
   */
  bool maybeAlloc(JSContext* cx, size_t length);

  /**
   * Reallocate uint8_t array to a smaller length with some acceptable threshold
   * to not reallocate. If the inlineBuf is used do not reallocate.
   */
  bool maybeRealloc(JSContext* cx, size_t newLength);

  /**
   * Transform the allocated uint8 array into a TypedArrayObject and return it.
   * If it does not exist (i.e., the inline array) then create a new
   * TypedArrayObject and copy the contents of the inlineBuf to it.
   */
  TypedArrayObject* toTypedArrayObject(JSContext* cx);
};

bool Uint8Buffer::maybeAlloc(JSContext* cx, size_t length) {
  length_ = length;
  if (length <= InlineLength) {
    return true;
  }

  ownedBuf_ =
      cx->make_pod_arena_array<uint8_t>(js::ArrayBufferContentsArena, length);
  return !!ownedBuf_;
}

bool Uint8Buffer::maybeRealloc(JSContext* cx, size_t newLength) {
  MOZ_ASSERT(newLength <= length_);
  if (length_ <= InlineLength) {
    length_ = newLength;
    return true;
  }
  MOZ_ASSERT(ownedBuf_);

  if (newLength <= InlineLength) {
    std::copy_n(ownedBuf_.get(), newLength, inlineBuf_);
    ownedBuf_ = nullptr;
    length_ = newLength;
    return true;
  }

  // The initial byte size estimation is based on the complete string length,
  // so it includes trailing padding and interspersed whitespace characters.
  // Common base64 variants split lines at 64 characters (RFC 1421, RFC 7468) or
  // 76 characters (RFC 2045, RFC 9580), which can introduce ~4% whitespace. To
  // avoid reallocating for these common base64 formats, small over-allocations
  // are deemed acceptable. We allow up to 6.25% wasted memory instead of 4%,
  // because the former can easily be computed with a single shift operation.
  //
  // Also don't bother shrinking the allocation unless at least 80 bytes will be
  // saved, which is a somewhat arbitrary number.
  constexpr size_t minBytesToReclaim = 80;

  size_t overAllocation = length_ - newLength;

  if (overAllocation < minBytesToReclaim || overAllocation <= length_ / 16) {
    length_ = newLength;
    return true;
  }

  uint8_t* oldOwnedBuf = ownedBuf_.release();
  uint8_t* newOwnedBuf = cx->pod_arena_realloc<uint8_t>(
      js::ArrayBufferContentsArena, oldOwnedBuf, length_, newLength);

  if (!newOwnedBuf) {
    js_free(oldOwnedBuf);
    return false;
  }

  ownedBuf_ = UniquePtr<uint8_t[], JS::FreePolicy>(newOwnedBuf);
  length_ = newLength;
  return true;
}

TypedArrayObject* Uint8Buffer::toTypedArrayObject(JSContext* cx) {
  if (!ownedBuf_) {
    TypedArrayObject* tarray =
        TypedArrayObjectTemplate<uint8_t>::fromLength(cx, length_);
    if (!tarray) {
      return nullptr;
    }

    auto target = SharedMem<uint8_t*>::unshared(tarray->dataPointerUnshared());
    auto source = SharedMem<uint8_t*>::unshared(inlineBuf_);
    UnsharedOps::podCopy(target, source, length_);

    return tarray;
  }

  auto bufferContents =
      ArrayBufferObject::BufferContents::createMallocedArrayBufferContentsArena(
          ownedBuf_.get());

  Rooted<ArrayBufferObject*> buffer(
      cx, ArrayBufferObject::createForContents(cx, length_, bufferContents));
  if (!buffer) {
    return nullptr;
  }

  // If and only if an ArrayBuffer is successfully created, ownership of
  // |ownedBuf_| is transferred to the new ArrayBuffer.
  (void)ownedBuf_.release();

  return TypedArrayObjectTemplate<uint8_t>::fromBuffer(cx, buffer, 0, length_);
}

/**
 * FromBase64 ( string, alphabet, lastChunkHandling [ , maxLength ] )
 *
 * https://tc39.es/proposal-arraybuffer-base64/spec/#sec-frombase64
 */
static auto FromBase64(JSLinearString* string, Alphabet alphabet,
                       LastChunkHandling lastChunkHandling,
                       Uint8Buffer& bytes) {
  auto data = SharedMem<uint8_t*>::unshared(bytes.data());
  size_t maxLength = bytes.length();
  return FromBase64<UnsharedOps>(string, alphabet, lastChunkHandling, data,
                                 maxLength);
}

/**
 * Uint8Array.fromBase64 ( string [ , options ] )
 * Uint8Array.prototype.setFromBase64 ( string [ , options ] )
 * Uint8Array.prototype.toBase64 ( [ options ] )
 *
 * Helper to retrieve the "alphabet" option.
 */
static bool GetAlphabetOption(JSContext* cx, Handle<JSObject*> options,
                              Alphabet* result) {
  Rooted<Value> value(cx);
  if (!GetProperty(cx, options, options, cx->names().alphabet, &value)) {
    return false;
  }

  if (value.isUndefined()) {
    *result = Alphabet::Base64;
    return true;
  }

  if (!value.isString()) {
    return ReportValueError(cx, JSMSG_UNEXPECTED_TYPE, JSDVG_IGNORE_STACK,
                            value, nullptr, "not a string");
  }

  auto* linear = value.toString()->ensureLinear(cx);
  if (!linear) {
    return false;
  }

  if (StringEqualsLiteral(linear, "base64")) {
    *result = Alphabet::Base64;
    return true;
  }

  if (StringEqualsLiteral(linear, "base64url")) {
    *result = Alphabet::Base64Url;
    return true;
  }

  JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                            JSMSG_TYPED_ARRAY_BAD_BASE64_ALPHABET);
  return false;
}

/**
 * Uint8Array.fromBase64 ( string [ , options ] )
 * Uint8Array.prototype.setFromBase64 ( string [ , options ] )
 *
 * Helper to retrieve the "lastChunkHandling" option.
 */
static bool GetLastChunkHandlingOption(JSContext* cx, Handle<JSObject*> options,
                                       LastChunkHandling* result) {
  Rooted<Value> value(cx);
  if (!GetProperty(cx, options, options, cx->names().lastChunkHandling,
                   &value)) {
    return false;
  }

  if (value.isUndefined()) {
    *result = LastChunkHandling::Loose;
    return true;
  }

  if (!value.isString()) {
    return ReportValueError(cx, JSMSG_UNEXPECTED_TYPE, JSDVG_IGNORE_STACK,
                            value, nullptr, "not a string");
  }

  auto* linear = value.toString()->ensureLinear(cx);
  if (!linear) {
    return false;
  }

  if (StringEqualsLiteral(linear, "loose")) {
    *result = LastChunkHandling::Loose;
    return true;
  }

  if (StringEqualsLiteral(linear, "strict")) {
    *result = LastChunkHandling::Strict;
    return true;
  }

  if (StringEqualsLiteral(linear, "stop-before-partial")) {
    *result = LastChunkHandling::StopBeforePartial;
    return true;
  }

  JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                            JSMSG_TYPED_ARRAY_BAD_BASE64_LAST_CHUNK_HANDLING);
  return false;
}

enum class OmitPadding : bool { No, Yes };

/**
 * Uint8Array.prototype.toBase64 ( [ options ] )
 *
 * Helper to retrieve the "omitPadding" option.
 */
static bool GetOmitPaddingOption(JSContext* cx, Handle<JSObject*> options,
                                 OmitPadding* result) {
  Rooted<Value> value(cx);
  if (!GetProperty(cx, options, options, cx->names().omitPadding, &value)) {
    return false;
  }

  *result = static_cast<OmitPadding>(JS::ToBoolean(value));
  return true;
}

/**
 * Uint8Array.fromBase64 ( string [ , options ] )
 *
 * https://tc39.es/proposal-arraybuffer-base64/spec/#sec-uint8array.frombase64
 */
static bool uint8array_fromBase64(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // Step 1.
  if (!args.get(0).isString()) {
    return ReportValueError(cx, JSMSG_UNEXPECTED_TYPE, JSDVG_SEARCH_STACK,
                            args.get(0), nullptr, "not a string");
  }
  Rooted<JSString*> string(cx, args[0].toString());

  // Steps 2-9.
  auto alphabet = Alphabet::Base64;
  auto lastChunkHandling = LastChunkHandling::Loose;
  if (args.hasDefined(1)) {
    // Step 2. (Inlined GetOptionsObject)
    Rooted<JSObject*> options(
        cx, RequireObjectArg(cx, "options", "fromBase64", args[1]));
    if (!options) {
      return false;
    }

    // Steps 3-6.
    if (!GetAlphabetOption(cx, options, &alphabet)) {
      return false;
    }

    // Steps 7-9.
    if (!GetLastChunkHandlingOption(cx, options, &lastChunkHandling)) {
      return false;
    }
  }

  // Compute the output byte length. Four input characters are decoded into
  // three bytes, so the output length can't be larger than ⌈length × 3/4⌉.
  auto outLength = mozilla::CheckedInt<size_t>{string->length()};
  outLength += 3;
  outLength /= 4;
  outLength *= 3;
  MOZ_ASSERT(outLength.isValid(), "can't overflow");

  static_assert(JSString::MAX_LENGTH <= TypedArrayObject::ByteLengthLimit,
                "string length doesn't exceed maximum typed array length");

  // Step 10.
  Uint8Buffer bytes;
  if (!bytes.maybeAlloc(cx, outLength.value())) {
    return false;
  }

  JSLinearString* linear = string->ensureLinear(cx);
  if (!linear) {
    return false;
  }

  auto result = FromBase64(linear, alphabet, lastChunkHandling, bytes);
  if (MOZ_UNLIKELY(result.isError())) {
    ReportBase64Error(cx, result, linear);
    return false;
  }
  MOZ_ASSERT(result.index <= linear->length());
  MOZ_ASSERT(result.written <= bytes.length());

  // Step 11.
  size_t resultLength = result.written;

  // Step 12-13.
  if (!bytes.maybeRealloc(cx, resultLength)) {
    return false;
  }

  TypedArrayObject* tarray = bytes.toTypedArrayObject(cx);
  if (!tarray) {
    return false;
  }

  // Step 14.
  args.rval().setObject(*tarray);
  return true;
}

/**
 * Uint8Array.fromHex ( string )
 *
 * https://tc39.es/proposal-arraybuffer-base64/spec/#sec-uint8array.fromhex
 */
static bool uint8array_fromHex(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // Step 1.
  if (!args.get(0).isString()) {
    return ReportValueError(cx, JSMSG_UNEXPECTED_TYPE, JSDVG_SEARCH_STACK,
                            args.get(0), nullptr, "not a string");
  }
  Rooted<JSString*> string(cx, args[0].toString());

  // FromHex, step 2.
  size_t stringLength = string->length();

  // FromHex, step 3.
  if (stringLength % 2 != 0) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_TYPED_ARRAY_BAD_HEX_STRING_LENGTH);
    return false;
  }

  // Step 4. (Reordered)
  size_t resultLength = stringLength / 2;

  // Step 5. (Reordered)
  Rooted<TypedArrayObject*> tarray(
      cx, TypedArrayObjectTemplate<uint8_t>::fromLength(cx, resultLength));
  if (!tarray) {
    return false;
  }

  // Steps 2-3.
  if (!FromHex(cx, string, resultLength, tarray)) {
    return false;
  }

  // Step 6.
  args.rval().setObject(*tarray);
  return true;
}

/**
 * Uint8Array.prototype.setFromBase64 ( string [ , options ] )
 *
 * https://tc39.es/proposal-arraybuffer-base64/spec/#sec-uint8array.prototype.setfrombase64
 */
static bool uint8array_setFromBase64(JSContext* cx, const CallArgs& args) {
  Rooted<TypedArrayObject*> tarray(
      cx, &args.thisv().toObject().as<TypedArrayObject>());

  // Additional step from Immutable ArrayBuffer proposal.
  if (tarray->is<ImmutableTypedArrayObject>()) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_ARRAYBUFFER_IMMUTABLE);
    return false;
  }

  // Step 3.
  if (!args.get(0).isString()) {
    return ReportValueError(cx, JSMSG_UNEXPECTED_TYPE, JSDVG_SEARCH_STACK,
                            args.get(0), nullptr, "not a string");
  }
  Rooted<JSString*> string(cx, args[0].toString());

  // Steps 4-11.
  auto alphabet = Alphabet::Base64;
  auto lastChunkHandling = LastChunkHandling::Loose;
  if (args.hasDefined(1)) {
    // Step 2. (Inlined GetOptionsObject)
    Rooted<JSObject*> options(
        cx, RequireObjectArg(cx, "options", "setFromBase64", args[1]));
    if (!options) {
      return false;
    }

    // Steps 3-6.
    if (!GetAlphabetOption(cx, options, &alphabet)) {
      return false;
    }

    // Steps 7-9.
    if (!GetLastChunkHandlingOption(cx, options, &lastChunkHandling)) {
      return false;
    }
  }

  // Steps 12-14.
  auto length = tarray->length();
  if (!length) {
    ReportOutOfBounds(cx, tarray);
    return false;
  }

  // Steps 15-18.
  size_t readLength = 0;
  size_t written = 0;
  if (*length > 0) {
    JSLinearString* linear = string->ensureLinear(cx);
    if (!linear) {
      return false;
    }

    auto result =
        FromBase64(linear, alphabet, lastChunkHandling, tarray, *length);
    if (MOZ_UNLIKELY(result.isError())) {
      ReportBase64Error(cx, result, linear);
      return false;
    }
    MOZ_ASSERT(result.index <= linear->length());
    MOZ_ASSERT(result.written <= *length);

    readLength = result.index;
    written = result.written;
  }

  // Steps 19-21. (Not applicable in our implementation.)

  // Step 22.
  Rooted<PlainObject*> result(cx, NewPlainObject(cx));
  if (!result) {
    return false;
  }

  // Step 23.
  Rooted<Value> readValue(cx, NumberValue(readLength));
  if (!DefineDataProperty(cx, result, cx->names().read, readValue)) {
    return false;
  }

  // Step 24.
  Rooted<Value> writtenValue(cx, NumberValue(written));
  if (!DefineDataProperty(cx, result, cx->names().written, writtenValue)) {
    return false;
  }

  // Step 25.
  args.rval().setObject(*result);
  return true;
}

/**
 * Uint8Array.prototype.setFromBase64 ( string [ , options ] )
 *
 * https://tc39.es/proposal-arraybuffer-base64/spec/#sec-uint8array.prototype.setfrombase64
 */
static bool uint8array_setFromBase64(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // Steps 1-2.
  return CallNonGenericMethod<IsUint8ArrayObject, uint8array_setFromBase64>(
      cx, args);
}

/**
 * Uint8Array.prototype.setFromHex ( string )
 *
 * https://tc39.es/proposal-arraybuffer-base64/spec/#sec-uint8array.prototype.setfromhex
 */
static bool uint8array_setFromHex(JSContext* cx, const CallArgs& args) {
  Rooted<TypedArrayObject*> tarray(
      cx, &args.thisv().toObject().as<TypedArrayObject>());

  // Additional step from Immutable ArrayBuffer proposal.
  if (tarray->is<ImmutableTypedArrayObject>()) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_ARRAYBUFFER_IMMUTABLE);
    return false;
  }

  // Step 3.
  if (!args.get(0).isString()) {
    return ReportValueError(cx, JSMSG_UNEXPECTED_TYPE, JSDVG_SEARCH_STACK,
                            args.get(0), nullptr, "not a string");
  }
  Rooted<JSString*> string(cx, args[0].toString());

  // Steps 4-6.
  auto byteLength = tarray->length();
  if (!byteLength) {
    ReportOutOfBounds(cx, tarray);
    return false;
  }

  // FromHex, step 2.
  size_t stringLength = string->length();

  // FromHex, step 3.
  if (stringLength % 2 != 0) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_TYPED_ARRAY_BAD_HEX_STRING_LENGTH);
    return false;
  }

  // |string| encodes |stringLength / 2| bytes, but we can't write more bytes
  // than there is space in |tarray|.
  size_t maxLength = std::min(*byteLength, stringLength / 2);

  // Steps 7-9.
  if (!FromHex(cx, string, maxLength, tarray)) {
    return false;
  }

  // Steps 10-13. (Not applicable in our implementation.)

  // Step 14.
  Rooted<PlainObject*> result(cx, NewPlainObject(cx));
  if (!result) {
    return false;
  }

  // Step 15.
  //
  // Each byte is encoded in two characters, so the number of read characters is
  // exactly twice as large as |maxLength|.
  Rooted<Value> readValue(cx, NumberValue(maxLength * 2));
  if (!DefineDataProperty(cx, result, cx->names().read, readValue)) {
    return false;
  }

  // Step 16.
  //
  // |maxLength| was constrained to the number of bytes written on success.
  Rooted<Value> writtenValue(cx, NumberValue(maxLength));
  if (!DefineDataProperty(cx, result, cx->names().written, writtenValue)) {
    return false;
  }

  // Step 17.
  args.rval().setObject(*result);
  return true;
}

/**
 * Uint8Array.prototype.setFromHex ( string )
 *
 * https://tc39.es/proposal-arraybuffer-base64/spec/#sec-uint8array.prototype.setfromhex
 */
static bool uint8array_setFromHex(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // Steps 1-2.
  return CallNonGenericMethod<IsUint8ArrayObject, uint8array_setFromHex>(cx,
                                                                         args);
}

template <typename Ops>
static void ToBase64(TypedArrayObject* tarray, size_t length, Alphabet alphabet,
                     OmitPadding omitPadding, mozilla::Range<Latin1Char> out) {
  const auto& base64Chars = alphabet == Alphabet::Base64
                                ? Base64::Encode::Base64
                                : Base64::Encode::Base64Url;

  auto encode = [&base64Chars](uint32_t value) {
    return base64Chars[value & 0x3f];
  };

  auto outPtr = out.begin();

  auto append = [&](char ch) { *outPtr++ = ch; };

  auto appendN = [&]<size_t N>(const char (&s)[N]) {
    auto* dest = outPtr.get();

    // Increment before writing to assert we're still in range.
    outPtr += N;
    std::memcpy(dest, s, N);
  };

  // Our implementation directly converts the bytes to their string
  // representation instead of first collecting them into an intermediate list.
  auto data = Ops::extract(tarray).template cast<uint8_t*>();
  auto toRead = length;

  if (toRead >= 12) {
    // Align |data| to uint32_t.
    if (MOZ_UNLIKELY(data.unwrapValue() & 3)) {
      // Performs at most three iterations until |data| is aligned, reading up
      // to nine bytes.
      while (data.unwrapValue() & 3) {
        // Combine three input bytes into a single uint24 value.
        auto byte0 = Ops::load(data++);
        auto byte1 = Ops::load(data++);
        auto byte2 = Ops::load(data++);
        auto u24 = (uint32_t(byte0) << 16) | (uint32_t(byte1) << 8) | byte2;

        // Encode the uint24 value as base64.
        char chars[] = {
            encode(u24 >> 18),
            encode(u24 >> 12),
            encode(u24 >> 6),
            encode(u24 >> 0),
        };
        appendN(chars);

        MOZ_ASSERT(toRead >= 3);
        toRead -= 3;
      }
    }

    auto data32 = data.template cast<uint32_t*>();
    for (; toRead >= 12; toRead -= 12) {
      // Read three 32-bit words.
      auto word0 = mozilla::NativeEndian::swapToBigEndian(Ops::load(data32++));
      auto word1 = mozilla::NativeEndian::swapToBigEndian(Ops::load(data32++));
      auto word2 = mozilla::NativeEndian::swapToBigEndian(Ops::load(data32++));

      // Split into four uint24 values.
      auto u24_0 = word0 >> 8;
      auto u24_1 = (word0 << 16) | (word1 >> 16);
      auto u24_2 = (word1 << 8) | (word2 >> 24);
      auto u24_3 = word2;

      // Encode the uint24 values as base64 and write in blocks of eight
      // characters.
      char chars1[] = {
          encode(u24_0 >> 18), encode(u24_0 >> 12),
          encode(u24_0 >> 6),  encode(u24_0 >> 0),

          encode(u24_1 >> 18), encode(u24_1 >> 12),
          encode(u24_1 >> 6),  encode(u24_1 >> 0),
      };
      appendN(chars1);

      char chars2[] = {
          encode(u24_2 >> 18), encode(u24_2 >> 12),
          encode(u24_2 >> 6),  encode(u24_2 >> 0),

          encode(u24_3 >> 18), encode(u24_3 >> 12),
          encode(u24_3 >> 6),  encode(u24_3 >> 0),
      };
      appendN(chars2);
    }
    data = data32.template cast<uint8_t*>();
  }

  for (; toRead >= 3; toRead -= 3) {
    // Combine three input bytes into a single uint24 value.
    auto byte0 = Ops::load(data++);
    auto byte1 = Ops::load(data++);
    auto byte2 = Ops::load(data++);
    auto u24 = (uint32_t(byte0) << 16) | (uint32_t(byte1) << 8) | byte2;

    // Encode the uint24 value as base64.
    char chars[] = {
        encode(u24 >> 18),
        encode(u24 >> 12),
        encode(u24 >> 6),
        encode(u24 >> 0),
    };
    appendN(chars);
  }

  // Trailing two and one element bytes are optionally padded with '='.
  if (toRead == 2) {
    // Combine two input bytes into a single uint24 value.
    auto byte0 = Ops::load(data++);
    auto byte1 = Ops::load(data++);
    auto u24 = (uint32_t(byte0) << 16) | (uint32_t(byte1) << 8);

    // Encode the uint24 value as base64, optionally including padding.
    append(encode(u24 >> 18));
    append(encode(u24 >> 12));
    append(encode(u24 >> 6));
    if (omitPadding == OmitPadding::No) {
      append('=');
    }
  } else if (toRead == 1) {
    // Combine one input byte into a single uint24 value.
    auto byte0 = Ops::load(data++);
    auto u24 = uint32_t(byte0) << 16;

    // Encode the uint24 value as base64, optionally including padding.
    append(encode(u24 >> 18));
    append(encode(u24 >> 12));
    if (omitPadding == OmitPadding::No) {
      append('=');
      append('=');
    }
  } else {
    MOZ_ASSERT(toRead == 0);
  }

  MOZ_ASSERT(outPtr == out.end(), "all characters were written");
}

/**
 * Uint8Array.prototype.toBase64 ( [ options ] )
 *
 * https://tc39.es/proposal-arraybuffer-base64/spec/#sec-uint8array.prototype.tobase64
 */
static bool uint8array_toBase64(JSContext* cx, const CallArgs& args) {
  Rooted<TypedArrayObject*> tarray(
      cx, &args.thisv().toObject().as<TypedArrayObject>());

  // Steps 3-7.
  auto alphabet = Alphabet::Base64;
  auto omitPadding = OmitPadding::No;
  if (args.hasDefined(0)) {
    // Step 3. (Inlined GetOptionsObject)
    Rooted<JSObject*> options(
        cx, RequireObjectArg(cx, "options", "toBase64", args[0]));
    if (!options) {
      return false;
    }

    // Steps 4-6.
    if (!GetAlphabetOption(cx, options, &alphabet)) {
      return false;
    }

    // Step 7.
    if (!GetOmitPaddingOption(cx, options, &omitPadding)) {
      return false;
    }
  }

  // Step 8. (Partial)
  auto length = tarray->length();
  if (!length) {
    ReportOutOfBounds(cx, tarray);
    return false;
  }

  // Compute the output string length. Three input bytes are encoded as four
  // characters, so the output length is ⌈length × 4/3⌉. When omitting padding,
  // the output length is length + ⌈length / 3⌉.
  auto outLength = mozilla::CheckedInt<size_t>{*length};
  outLength += 2;
  outLength /= 3;
  if (omitPadding == OmitPadding::No) {
    outLength *= 4;
  } else {
    outLength += *length;
  }
  if (!outLength.isValid() || outLength.value() > JSString::MAX_LENGTH) {
    ReportAllocationOverflow(cx);
    return false;
  }

  StringChars<Latin1Char> chars(cx);
  if (!chars.maybeAlloc(cx, outLength.value())) {
    return false;
  }

  // Steps 9-10.
  {
    JS::AutoCheckCannotGC nogc;
    mozilla::Range<Latin1Char> r(chars.data(nogc), outLength.value());

    if (tarray->isSharedMemory()) {
      ToBase64<SharedOps>(tarray, *length, alphabet, omitPadding, r);
    } else {
      ToBase64<UnsharedOps>(tarray, *length, alphabet, omitPadding, r);
    }
  }

  // Step 11.
  auto* str = chars.toStringDontDeflate<CanGC>(cx, outLength.value());
  if (!str) {
    return false;
  }

  args.rval().setString(str);
  return true;
}

/**
 * Uint8Array.prototype.toBase64 ( [ options ] )
 *
 * https://tc39.es/proposal-arraybuffer-base64/spec/#sec-uint8array.prototype.tobase64
 */
static bool uint8array_toBase64(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // Steps 1-2.
  return CallNonGenericMethod<IsUint8ArrayObject, uint8array_toBase64>(cx,
                                                                       args);
}

template <typename Ops>
static void ToHex(TypedArrayObject* tarray, size_t length,
                  mozilla::Range<Latin1Char> out) {
  // NB: Lower case hex digits.
  static constexpr char HexDigits[] = "0123456789abcdef";
  static_assert(std::char_traits<char>::length(HexDigits) == 16);

  auto outPtr = out.begin();

  auto appendN = [&]<size_t N>(const char (&s)[N]) {
    auto* dest = outPtr.get();

    // Increment before writing to assert we're still in range.
    outPtr += N;
    std::memcpy(dest, s, N);
  };

  // Process multiple bytes per loop to reduce number of calls to
  // infallibleAppend. Choose four bytes because tested compilers can optimize
  // this amount of bytes into a single write operation.
  constexpr size_t BYTES_PER_LOOP = 4;

  size_t alignedLength = length & ~(BYTES_PER_LOOP - 1);

  // Steps 3 and 5.
  //
  // Our implementation directly converts the bytes to their string
  // representation instead of first collecting them into an intermediate list.
  auto data = Ops::extract(tarray).template cast<uint8_t*>();
  for (size_t index = 0; index < alignedLength;) {
    char chars[BYTES_PER_LOOP * 2];

    for (size_t i = 0; i < BYTES_PER_LOOP; ++i) {
      auto byte = Ops::load(data + index++);
      chars[i * 2 + 0] = HexDigits[byte >> 4];
      chars[i * 2 + 1] = HexDigits[byte & 0xf];
    }

    appendN(chars);
  }

  // Write the remaining characters.
  for (size_t index = alignedLength; index < length;) {
    char chars[2];

    auto byte = Ops::load(data + index++);
    chars[0] = HexDigits[byte >> 4];
    chars[1] = HexDigits[byte & 0xf];

    appendN(chars);
  }

  MOZ_ASSERT(outPtr == out.end(), "all characters were written");
}

template <>
void ToHex<UnsharedOps>(TypedArrayObject* tarray, size_t length,
                        mozilla::Range<Latin1Char> out) {
  // Convert to lower case hex digit.
  //
  // Doesn't use a lookup table to make it optimizable for the auto-vectorizer.
  // The auto-vectorizer can't inline through SharedOps, so this code path is
  // only used for UnsharedOps.
  //
  // https://lemire.me/blog/2026/02/02/converting-data-to-hexadecimal-outputs-quickly/
  auto toLowerHex = [](uint8_t x) -> char {
    static_assert('a' - '9' == 40);
    return x + '0' + ((x > 9) * 39);
  };

  auto outPtr = out.begin();

  // Steps 3 and 5.
  //
  // Our implementation directly converts the bytes to their string
  // representation instead of first collecting them into an intermediate list.
  auto data = UnsharedOps::extract(tarray).template cast<uint8_t*>();
  for (size_t index = 0; index < length;) {
    auto byte = UnsharedOps::load(data + index++);
    *outPtr++ = toLowerHex(byte >> 4);
    *outPtr++ = toLowerHex(byte & 0xf);
  }

  MOZ_ASSERT(outPtr == out.end(), "all characters were written");
}

/**
 * Uint8Array.prototype.toHex ( )
 *
 * https://tc39.es/proposal-arraybuffer-base64/spec/#sec-uint8array.prototype.tohex
 */
static bool uint8array_toHex(JSContext* cx, const CallArgs& args) {
  Rooted<TypedArrayObject*> tarray(
      cx, &args.thisv().toObject().as<TypedArrayObject>());

  // Step 3. (Partial)
  auto length = tarray->length();
  if (!length) {
    ReportOutOfBounds(cx, tarray);
    return false;
  }

  // |length| is limited by |ByteLengthLimit|, which ensures that multiplying it
  // by two won't overflow.
  static_assert(TypedArrayObject::ByteLengthLimit <=
                std::numeric_limits<size_t>::max() / 2);
  MOZ_ASSERT(*length <= TypedArrayObject::ByteLengthLimit);

  // Compute the output string length. Each byte is encoded as two characters,
  // so the output length is exactly twice as large as |length|.
  size_t outLength = *length * 2;
  if (outLength > JSString::MAX_LENGTH) {
    ReportAllocationOverflow(cx);
    return false;
  }

  // Step 4.
  StringChars<Latin1Char> chars(cx);
  if (!chars.maybeAlloc(cx, outLength)) {
    return false;
  }

  // Steps 3 and 5.
  {
    JS::AutoCheckCannotGC nogc;
    mozilla::Range<Latin1Char> r(chars.data(nogc), outLength);

    if (tarray->isSharedMemory()) {
      ToHex<SharedOps>(tarray, *length, r);
    } else {
      ToHex<UnsharedOps>(tarray, *length, r);
    }
  }

  // Step 6.
  auto* str = chars.toStringDontDeflate<CanGC>(cx, outLength);
  if (!str) {
    return false;
  }

  args.rval().setString(str);
  return true;
}

/**
 * Uint8Array.prototype.toHex ( )
 *
 * https://tc39.es/proposal-arraybuffer-base64/spec/#sec-uint8array.prototype.tohex
 */
static bool uint8array_toHex(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // Steps 1-2.
  return CallNonGenericMethod<IsUint8ArrayObject, uint8array_toHex>(cx, args);
}

/* static */ const JSFunctionSpec TypedArrayObject::protoFunctions[] = {
    JS_INLINABLE_FN("subarray", TypedArray_subarray, 2, 0, TypedArraySubarray),
    JS_INLINABLE_FN("set", TypedArray_set, 1, 0, TypedArraySet),
    JS_FN("copyWithin", TypedArray_copyWithin, 2, 0),
    JS_SELF_HOSTED_FN("every", "TypedArrayEvery", 1, 0),
    JS_INLINABLE_FN("fill", TypedArray_fill, 1, 0, TypedArrayFill),
    JS_SELF_HOSTED_FN("filter", "TypedArrayFilter", 1, 0),
    JS_SELF_HOSTED_FN("find", "TypedArrayFind", 1, 0),
    JS_SELF_HOSTED_FN("findIndex", "TypedArrayFindIndex", 1, 0),
    JS_SELF_HOSTED_FN("findLast", "TypedArrayFindLast", 1, 0),
    JS_SELF_HOSTED_FN("findLastIndex", "TypedArrayFindLastIndex", 1, 0),
    JS_SELF_HOSTED_FN("forEach", "TypedArrayForEach", 1, 0),
    JS_FN("indexOf", TypedArray_indexOf, 1, 0),
    JS_FN("join", TypedArray_join, 1, 0),
    JS_FN("lastIndexOf", TypedArray_lastIndexOf, 1, 0),
    JS_SELF_HOSTED_FN("map", "TypedArrayMap", 1, 0),
    JS_SELF_HOSTED_FN("reduce", "TypedArrayReduce", 1, 0),
    JS_SELF_HOSTED_FN("reduceRight", "TypedArrayReduceRight", 1, 0),
    JS_FN("reverse", TypedArray_reverse, 0, 0),
    JS_FN("slice", TypedArray_slice, 2, 0),
    JS_SELF_HOSTED_FN("some", "TypedArraySome", 1, 0),
    JS_TRAMPOLINE_FN("sort", TypedArrayObject::sort, 1, 0, TypedArraySort),
    JS_SELF_HOSTED_FN("entries", "TypedArrayEntries", 0, 0),
    JS_SELF_HOSTED_FN("keys", "TypedArrayKeys", 0, 0),
    JS_SELF_HOSTED_FN("values", "$TypedArrayValues", 0, 0),
    JS_SELF_HOSTED_SYM_FN(iterator, "$TypedArrayValues", 0, 0),
    JS_FN("includes", TypedArray_includes, 1, 0),
    JS_SELF_HOSTED_FN("toString", "ArrayToString", 0, 0),
    JS_SELF_HOSTED_FN("toLocaleString", "TypedArrayToLocaleString", 2, 0),
    JS_SELF_HOSTED_FN("at", "TypedArrayAt", 1, 0),
    JS_FN("toReversed", TypedArray_toReversed, 0, 0),
    JS_SELF_HOSTED_FN("toSorted", "TypedArrayToSorted", 1, 0),
    JS_FN("with", TypedArray_with, 2, 0),
    JS_FS_END,
};

/* static */ const JSFunctionSpec TypedArrayObject::staticFunctions[] = {
    JS_SELF_HOSTED_FN("from", "TypedArrayStaticFrom", 3, 0),
    JS_SELF_HOSTED_FN("of", "TypedArrayStaticOf", 0, 0),
    JS_FS_END,
};

/* static */ const JSPropertySpec TypedArrayObject::staticProperties[] = {
    JS_SELF_HOSTED_SYM_GET(species, "$TypedArraySpecies", 0),
    JS_PS_END,
};

static JSObject* CreateSharedTypedArrayPrototype(JSContext* cx,
                                                 JSProtoKey key) {
  return GlobalObject::createBlankPrototype(
      cx, cx->global(), &TypedArrayObject::sharedTypedArrayPrototypeClass);
}

static const ClassSpec TypedArrayObjectSharedTypedArrayPrototypeClassSpec = {
    GenericCreateConstructor<TypedArrayConstructor, 0, gc::AllocKind::FUNCTION>,
    CreateSharedTypedArrayPrototype,
    TypedArrayObject::staticFunctions,
    TypedArrayObject::staticProperties,
    TypedArrayObject::protoFunctions,
    TypedArrayObject::protoAccessors,
    GenericFinishInit<WhichHasRealmFuseProperty::ProtoAndCtor>,
    ClassSpec::DontDefineConstructor,
};

/* static */ const JSClass TypedArrayObject::sharedTypedArrayPrototypeClass = {
    "TypedArrayPrototype",
    JSCLASS_HAS_CACHED_PROTO(JSProto_TypedArray),
    JS_NULL_CLASS_OPS,
    &TypedArrayObjectSharedTypedArrayPrototypeClassSpec,
};

namespace {

// This default implementation is only valid for integer types less
// than 32-bits in size.
template <typename NativeType>
bool TypedArrayObjectTemplate<NativeType>::getElementPure(
    TypedArrayObject* tarray, size_t index, Value* vp) {
  static_assert(sizeof(NativeType) < 4,
                "this method must only handle NativeType values that are "
                "always exact int32_t values");

  *vp = Int32Value(getIndex(tarray, index));
  return true;
}

// We need to specialize for floats and other integer types.
template <>
bool TypedArrayObjectTemplate<int32_t>::getElementPure(TypedArrayObject* tarray,
                                                       size_t index,
                                                       Value* vp) {
  *vp = Int32Value(getIndex(tarray, index));
  return true;
}

template <>
bool TypedArrayObjectTemplate<uint32_t>::getElementPure(
    TypedArrayObject* tarray, size_t index, Value* vp) {
  uint32_t val = getIndex(tarray, index);
  *vp = NumberValue(val);
  return true;
}

template <>
bool TypedArrayObjectTemplate<float16>::getElementPure(TypedArrayObject* tarray,
                                                       size_t index,
                                                       Value* vp) {
  float16 f16 = getIndex(tarray, index);

  // The value may be a non-canonical NaN. DoubleValue canonicalizes it to
  // preserve the invariant that only canonical NaNs are stored in Values.
  *vp = DoubleValue(static_cast<double>(f16));
  return true;
}

template <>
bool TypedArrayObjectTemplate<float>::getElementPure(TypedArrayObject* tarray,
                                                     size_t index, Value* vp) {
  float val = getIndex(tarray, index);
  double dval = val;

  // The value may be a non-canonical NaN. DoubleValue canonicalizes it to
  // preserve the invariant that only canonical NaNs are stored in Values.
  *vp = DoubleValue(dval);
  return true;
}

template <>
bool TypedArrayObjectTemplate<double>::getElementPure(TypedArrayObject* tarray,
                                                      size_t index, Value* vp) {
  double val = getIndex(tarray, index);

  // The value may be a non-canonical NaN. DoubleValue canonicalizes it to
  // preserve the invariant that only canonical NaNs are stored in Values.
  *vp = DoubleValue(val);
  return true;
}

template <>
bool TypedArrayObjectTemplate<int64_t>::getElementPure(TypedArrayObject* tarray,
                                                       size_t index,
                                                       Value* vp) {
  return false;
}

template <>
bool TypedArrayObjectTemplate<uint64_t>::getElementPure(
    TypedArrayObject* tarray, size_t index, Value* vp) {
  return false;
}
} /* anonymous namespace */

namespace {

template <typename NativeType>
bool TypedArrayObjectTemplate<NativeType>::getElement(JSContext* cx,
                                                      TypedArrayObject* tarray,
                                                      size_t index,
                                                      MutableHandleValue val) {
  MOZ_ALWAYS_TRUE(getElementPure(tarray, index, val.address()));
  return true;
}

template <>
bool TypedArrayObjectTemplate<int64_t>::getElement(JSContext* cx,
                                                   TypedArrayObject* tarray,
                                                   size_t index,
                                                   MutableHandleValue val) {
  int64_t n = getIndex(tarray, index);
  BigInt* res = BigInt::createFromInt64(cx, n);
  if (!res) {
    return false;
  }
  val.setBigInt(res);
  return true;
}

template <>
bool TypedArrayObjectTemplate<uint64_t>::getElement(JSContext* cx,
                                                    TypedArrayObject* tarray,
                                                    size_t index,
                                                    MutableHandleValue val) {
  uint64_t n = getIndex(tarray, index);
  BigInt* res = BigInt::createFromUint64(cx, n);
  if (!res) {
    return false;
  }
  val.setBigInt(res);
  return true;
}
} /* anonymous namespace */

namespace js {

template <>
bool TypedArrayObject::getElement<CanGC>(JSContext* cx, size_t index,
                                         MutableHandleValue val) {
  switch (type()) {
#define GET_ELEMENT(_, T, N) \
  case Scalar::N:            \
    return TypedArrayObjectTemplate<T>::getElement(cx, this, index, val);
    JS_FOR_EACH_TYPED_ARRAY(GET_ELEMENT)
#undef GET_ELEMENT
    case Scalar::MaxTypedArrayViewType:
    case Scalar::Int64:
    case Scalar::Simd128:
      break;
  }

  MOZ_CRASH("Unknown TypedArray type");
}

template <>
bool TypedArrayObject::getElement<NoGC>(
    JSContext* cx, size_t index,
    typename MaybeRooted<Value, NoGC>::MutableHandleType vp) {
  return getElementPure(index, vp.address());
}

}  // namespace js

bool TypedArrayObject::getElementPure(size_t index, Value* vp) {
  switch (type()) {
#define GET_ELEMENT_PURE(_, T, N) \
  case Scalar::N:                 \
    return TypedArrayObjectTemplate<T>::getElementPure(this, index, vp);
    JS_FOR_EACH_TYPED_ARRAY(GET_ELEMENT_PURE)
#undef GET_ELEMENT
    case Scalar::MaxTypedArrayViewType:
    case Scalar::Int64:
    case Scalar::Simd128:
      break;
  }

  MOZ_CRASH("Unknown TypedArray type");
}

/* static */
bool TypedArrayObject::getElements(JSContext* cx,
                                   Handle<TypedArrayObject*> tarray,
                                   size_t length, Value* vp) {
  MOZ_ASSERT(length <= tarray->length().valueOr(0));
  MOZ_ASSERT_IF(length > 0, !tarray->hasDetachedBuffer());

  switch (tarray->type()) {
#define GET_ELEMENTS(_, T, N)                                               \
  case Scalar::N:                                                           \
    for (size_t i = 0; i < length; ++i, ++vp) {                             \
      if (!TypedArrayObjectTemplate<T>::getElement(                         \
              cx, tarray, i, MutableHandleValue::fromMarkedLocation(vp))) { \
        return false;                                                       \
      }                                                                     \
    }                                                                       \
    return true;
    JS_FOR_EACH_TYPED_ARRAY(GET_ELEMENTS)
#undef GET_ELEMENTS
    case Scalar::MaxTypedArrayViewType:
    case Scalar::Int64:
    case Scalar::Simd128:
      break;
  }

  MOZ_CRASH("Unknown TypedArray type");
}

/***
 *** JS impl
 ***/

/*
 * TypedArrayObject boilerplate
 */

static const JSClassOps TypedArrayClassOps = {
    .finalize = FixedLengthTypedArrayObject::finalize,
    .trace = ArrayBufferViewObject::trace,
};

static const JSClassOps ResizableTypedArrayClassOps = {
    .trace = ArrayBufferViewObject::trace,
};

static const JSClassOps ImmutableTypedArrayClassOps = {
    .trace = ArrayBufferViewObject::trace,
};

static const ClassExtension TypedArrayClassExtension = {
    FixedLengthTypedArrayObject::objectMoved,  // objectMovedOp
};

static const JSPropertySpec
    static_prototype_properties[Scalar::MaxTypedArrayViewType][2] = {
#define IMPL_TYPED_ARRAY_PROPERTIES(ExternalType, NativeType, Name)        \
  {                                                                        \
      JS_INT32_PS("BYTES_PER_ELEMENT",                                     \
                  TypedArrayObjectTemplate<NativeType>::BYTES_PER_ELEMENT, \
                  JSPROP_READONLY | JSPROP_PERMANENT),                     \
      JS_PS_END,                                                           \
  },

        JS_FOR_EACH_TYPED_ARRAY(IMPL_TYPED_ARRAY_PROPERTIES)
#undef IMPL_TYPED_ARRAY_PROPERTIES
};

static const JSFunctionSpec uint8array_static_methods[] = {
    JS_FN("fromBase64", uint8array_fromBase64, 1, 0),
    JS_FN("fromHex", uint8array_fromHex, 1, 0),
    JS_FS_END,
};

static const JSFunctionSpec uint8array_methods[] = {
    JS_FN("setFromBase64", uint8array_setFromBase64, 1, 0),
    JS_FN("setFromHex", uint8array_setFromHex, 1, 0),
    JS_FN("toBase64", uint8array_toBase64, 0, 0),
    JS_FN("toHex", uint8array_toHex, 0, 0),
    JS_FS_END,
};

static constexpr const JSFunctionSpec* TypedArrayStaticMethods(
    Scalar::Type type) {
  if (type == Scalar::Uint8) {
    return uint8array_static_methods;
  }
  return nullptr;
}

static constexpr const JSFunctionSpec* TypedArrayMethods(Scalar::Type type) {
  if (type == Scalar::Uint8) {
    return uint8array_methods;
  }
  return nullptr;
}

static const ClassSpec
    TypedArrayObjectClassSpecs[Scalar::MaxTypedArrayViewType] = {
#define IMPL_TYPED_ARRAY_CLASS_SPEC(ExternalType, NativeType, Name) \
  {                                                                 \
      TypedArrayObjectTemplate<NativeType>::createConstructor,      \
      TypedArrayObjectTemplate<NativeType>::createPrototype,        \
      TypedArrayStaticMethods(Scalar::Type::Name),                  \
      static_prototype_properties[Scalar::Type::Name],              \
      TypedArrayMethods(Scalar::Type::Name),                        \
      static_prototype_properties[Scalar::Type::Name],              \
      GenericFinishInit<WhichHasRealmFuseProperty::ProtoAndCtor>,   \
      JSProto_TypedArray,                                           \
  },

        JS_FOR_EACH_TYPED_ARRAY(IMPL_TYPED_ARRAY_CLASS_SPEC)
#undef IMPL_TYPED_ARRAY_CLASS_SPEC
};

// Class definitions for fixed length, resizable, and immutable typed arrays.
// Stored into a 2-dimensional array to ensure the classes are in contiguous
// memory.
const JSClass TypedArrayObject::anyClasses[3][Scalar::MaxTypedArrayViewType] = {
    // Class definitions for fixed length typed arrays.
    {
#define IMPL_TYPED_ARRAY_CLASS(ExternalType, NativeType, Name)             \
  {                                                                        \
      #Name "Array",                                                       \
      JSCLASS_HAS_RESERVED_SLOTS(TypedArrayObject::RESERVED_SLOTS) |       \
          JSCLASS_HAS_CACHED_PROTO(JSProto_##Name##Array) |                \
          JSCLASS_DELAY_METADATA_BUILDER | JSCLASS_SKIP_NURSERY_FINALIZE | \
          JSCLASS_BACKGROUND_FINALIZE,                                     \
      &TypedArrayClassOps,                                                 \
      &TypedArrayObjectClassSpecs[Scalar::Type::Name],                     \
      &TypedArrayClassExtension,                                           \
  },

        JS_FOR_EACH_TYPED_ARRAY(IMPL_TYPED_ARRAY_CLASS)
#undef IMPL_TYPED_ARRAY_CLASS
    },

    // Class definitions for immutable typed arrays.
    {
#define IMPL_TYPED_ARRAY_CLASS(ExternalType, NativeType, Name)                \
  {                                                                           \
      #Name "Array",                                                          \
      JSCLASS_HAS_RESERVED_SLOTS(ImmutableTypedArrayObject::RESERVED_SLOTS) | \
          JSCLASS_HAS_CACHED_PROTO(JSProto_##Name##Array) |                   \
          JSCLASS_DELAY_METADATA_BUILDER,                                     \
      &ImmutableTypedArrayClassOps,                                           \
      &TypedArrayObjectClassSpecs[Scalar::Type::Name],                        \
      JS_NULL_CLASS_EXT,                                                      \
  },

        JS_FOR_EACH_TYPED_ARRAY(IMPL_TYPED_ARRAY_CLASS)
#undef IMPL_TYPED_ARRAY_CLASS
    },

    // Class definitions for resizable typed arrays.
    {
#define IMPL_TYPED_ARRAY_CLASS(ExternalType, NativeType, Name)                \
  {                                                                           \
      #Name "Array",                                                          \
      JSCLASS_HAS_RESERVED_SLOTS(ResizableTypedArrayObject::RESERVED_SLOTS) | \
          JSCLASS_HAS_CACHED_PROTO(JSProto_##Name##Array) |                   \
          JSCLASS_DELAY_METADATA_BUILDER,                                     \
      &ResizableTypedArrayClassOps,                                           \
      &TypedArrayObjectClassSpecs[Scalar::Type::Name],                        \
      JS_NULL_CLASS_EXT,                                                      \
  },

        JS_FOR_EACH_TYPED_ARRAY(IMPL_TYPED_ARRAY_CLASS)
#undef IMPL_TYPED_ARRAY_CLASS
    },
};

// The various typed array prototypes are supposed to 1) be normal objects,
// 2) stringify to "[object <name of constructor>]", and 3) (Gecko-specific)
// be xrayable.  The first and second requirements mandate (in the absence of
// @@toStringTag) a custom class.  The third requirement mandates that each
// prototype's class have the relevant typed array's cached JSProtoKey in them.
// Thus we need one class with cached prototype per kind of typed array, with a
// delegated ClassSpec.
//
// Actually ({}).toString.call(Uint8Array.prototype) should throw, because
// Uint8Array.prototype lacks the the typed array internal slots.  (Same as
// with %TypedArray%.prototype.)  It's not clear this is desirable (see
// above), but it's what we've always done, so keep doing it till we
// implement @@toStringTag or ES6 changes.
const JSClass TypedArrayObject::protoClasses[Scalar::MaxTypedArrayViewType] = {
#define IMPL_TYPED_ARRAY_PROTO_CLASS(ExternalType, NativeType, Name) \
  {                                                                  \
      #Name "Array.prototype",                                       \
      JSCLASS_HAS_CACHED_PROTO(JSProto_##Name##Array),               \
      JS_NULL_CLASS_OPS,                                             \
      &TypedArrayObjectClassSpecs[Scalar::Type::Name],               \
  },

    JS_FOR_EACH_TYPED_ARRAY(IMPL_TYPED_ARRAY_PROTO_CLASS)
#undef IMPL_TYPED_ARRAY_PROTO_CLASS
};

bool js::IsTypedArrayConstructor(const JSObject* obj) {
#define CHECK_TYPED_ARRAY_CONSTRUCTOR(_, T, N)                                 \
  if (IsNativeFunction(obj, TypedArrayObjectTemplate<T>::class_constructor)) { \
    return true;                                                               \
  }
  JS_FOR_EACH_TYPED_ARRAY(CHECK_TYPED_ARRAY_CONSTRUCTOR)
#undef CHECK_TYPED_ARRAY_CONSTRUCTOR
  return false;
}

bool js::IsTypedArrayConstructor(HandleValue v, Scalar::Type type) {
  return IsNativeFunction(v, TypedArrayConstructorNative(type));
}

JSNative js::TypedArrayConstructorNative(Scalar::Type type) {
#define TYPED_ARRAY_CONSTRUCTOR_NATIVE(_, T, N)            \
  if (type == Scalar::N) {                                 \
    return TypedArrayObjectTemplate<T>::class_constructor; \
  }
  JS_FOR_EACH_TYPED_ARRAY(TYPED_ARRAY_CONSTRUCTOR_NATIVE)
#undef TYPED_ARRAY_CONSTRUCTOR_NATIVE

  MOZ_CRASH("unexpected typed array type");
}

Scalar::Type js::TypedArrayConstructorType(const JSFunction* fun) {
  if (!fun->isNativeFun()) {
    return Scalar::MaxTypedArrayViewType;
  }

#define CHECK_TYPED_ARRAY_CONSTRUCTOR(_, T, N)                           \
  if (fun->native() == TypedArrayObjectTemplate<T>::class_constructor) { \
    return Scalar::N;                                                    \
  }
  JS_FOR_EACH_TYPED_ARRAY(CHECK_TYPED_ARRAY_CONSTRUCTOR)
#undef CHECK_TYPED_ARRAY_CONSTRUCTOR

  return Scalar::MaxTypedArrayViewType;
}

bool js::IsBufferSource(JSContext* cx, JSObject* object, bool allowShared,
                        bool allowResizable, SharedMem<uint8_t*>* dataPointer,
                        size_t* byteLength, bool* isShared) {
  if (object->is<TypedArrayObject>()) {
    Rooted<TypedArrayObject*> view(cx, &object->as<TypedArrayObject>());
    if (!allowShared && view->isSharedMemory()) {
      return false;
    }
    if (!allowResizable && JS::IsResizableArrayBufferView(object)) {
      return false;
    }
    // Ensure the pointer we pass out won't move as long as you properly root
    // it. This is only needed for non-shared memory.
    if (!view->isSharedMemory() &&
        !ArrayBufferViewObject::ensureNonInline(cx, view)) {
      return false;
    }
    *dataPointer = view->dataPointerEither().cast<uint8_t*>();
    *byteLength = view->byteLength().valueOr(0);
    if (isShared) {
      *isShared = view->isSharedMemory();
    }
    return true;
  }

  if (object->is<DataViewObject>()) {
    Rooted<DataViewObject*> view(cx, &object->as<DataViewObject>());
    if (!allowShared && view->isSharedMemory()) {
      return false;
    }
    if (!allowResizable && JS::IsResizableArrayBufferView(object)) {
      return false;
    }
    // Ensure the pointer we pass out won't move as long as you properly root
    // it. This is only needed for non-shared memory.
    if (!view->isSharedMemory() &&
        !ArrayBufferViewObject::ensureNonInline(cx, view)) {
      return false;
    }
    *dataPointer = view->dataPointerEither().cast<uint8_t*>();
    *byteLength = view->byteLength().valueOr(0);
    if (isShared) {
      *isShared = view->isSharedMemory();
    }
    return true;
  }

  if (object->is<ArrayBufferObject>()) {
    Rooted<ArrayBufferObject*> buffer(cx, &object->as<ArrayBufferObject>());
    if (!allowResizable && buffer->isResizable()) {
      return false;
    }
    // Ensure the pointer we pass out won't move as long as you properly root
    // it.
    if (!ArrayBufferObject::ensureNonInline(cx, buffer)) {
      return false;
    }
    *dataPointer = buffer->dataPointerEither();
    *byteLength = buffer->byteLength();
    if (isShared) {
      *isShared = false;
    }
    return true;
  }

  if (allowShared && object->is<SharedArrayBufferObject>()) {
    SharedArrayBufferObject& buffer = object->as<SharedArrayBufferObject>();
    if (!allowResizable && buffer.isResizable()) {
      return false;
    }
    // This will always be locked and out of line.
    *dataPointer = buffer.dataPointerShared();
    *byteLength = buffer.byteLength();
    if (isShared) {
      *isShared = true;
    }
    return true;
  }

  return false;
}

template <typename CharT>
static inline bool StringIsInfinity(mozilla::Range<const CharT> s) {
  static constexpr std::string_view Infinity = "Infinity";

  // Compilers optimize this to one |cmp| instruction on x64 resp. two for x86,
  // when the input is a Latin-1 string, because the string "Infinity" is
  // exactly eight characters long, so it can be represented as a single uint64
  // value.
  return s.length() == Infinity.length() &&
         EqualChars(s.begin().get(), Infinity.data(), Infinity.length());
}

template <typename CharT>
static inline bool StringIsNaN(mozilla::Range<const CharT> s) {
  static constexpr std::string_view NaN = "NaN";

  // "NaN" is not as nicely optimizable as "Infinity", but oh well.
  return s.length() == NaN.length() &&
         EqualChars(s.begin().get(), NaN.data(), NaN.length());
}

template <typename CharT>
static mozilla::Maybe<uint64_t> StringToTypedArrayIndexSlow(
    mozilla::Range<const CharT> s) {
  const mozilla::RangedPtr<const CharT> start = s.begin();
  const mozilla::RangedPtr<const CharT> end = s.end();

  const CharT* actualEnd;
  double result = js_strtod(start.get(), end.get(), &actualEnd);

  // The complete string must have been parsed.
  if (actualEnd != end.get()) {
    return mozilla::Nothing();
  }

  // Now convert it back to a string.
  ToCStringBuf cbuf;
  size_t cstrlen;
  const char* cstr = js::NumberToCString(&cbuf, result, &cstrlen);
  MOZ_ASSERT(cstr);

  // Both strings must be equal for a canonical numeric index string.
  if (s.length() != cstrlen || !EqualChars(start.get(), cstr, cstrlen)) {
    return mozilla::Nothing();
  }

  // Directly perform IsInteger() check and encode negative and non-integer
  // indices as OOB.
  // See 9.4.5.2 [[HasProperty]], steps 3.b.iii and 3.b.v.
  // See 9.4.5.3 [[DefineOwnProperty]], steps 3.b.i and 3.b.iii.
  // See 9.4.5.8 IntegerIndexedElementGet, steps 5 and 8.
  // See 9.4.5.9 IntegerIndexedElementSet, steps 6 and 9.
  if (result < 0 || !IsInteger(result)) {
    return mozilla::Some(UINT64_MAX);
  }

  // Anything equals-or-larger than 2^53 is definitely OOB, encode it
  // accordingly so that the cast to uint64_t is well defined.
  if (result >= DOUBLE_INTEGRAL_PRECISION_LIMIT) {
    return mozilla::Some(UINT64_MAX);
  }

  // The string is an actual canonical numeric index.
  return mozilla::Some(result);
}

template <typename CharT>
mozilla::Maybe<uint64_t> js::StringToTypedArrayIndex(
    mozilla::Range<const CharT> s) {
  mozilla::RangedPtr<const CharT> cp = s.begin();
  const mozilla::RangedPtr<const CharT> end = s.end();

  MOZ_ASSERT(cp < end, "caller must check for empty strings");

  bool negative = false;
  if (*cp == '-') {
    negative = true;
    if (++cp == end) {
      return mozilla::Nothing();
    }
  }

  if (!IsAsciiDigit(*cp)) {
    // Check for "NaN", "Infinity", or "-Infinity".
    if ((!negative && StringIsNaN<CharT>({cp, end})) ||
        StringIsInfinity<CharT>({cp, end})) {
      return mozilla::Some(UINT64_MAX);
    }
    return mozilla::Nothing();
  }

  uint32_t digit = AsciiDigitToNumber(*cp++);

  // Don't allow leading zeros.
  if (digit == 0 && cp != end) {
    // The string may be of the form "0.xyz". The exponent form isn't possible
    // when the string starts with "0".
    if (*cp == '.') {
      return StringToTypedArrayIndexSlow(s);
    }
    return mozilla::Nothing();
  }

  uint64_t index = digit;

  for (; cp < end; cp++) {
    if (!IsAsciiDigit(*cp)) {
      // Take the slow path when the string has fractional parts or an exponent.
      if (*cp == '.' || *cp == 'e') {
        return StringToTypedArrayIndexSlow(s);
      }
      return mozilla::Nothing();
    }

    digit = AsciiDigitToNumber(*cp);

    static_assert(
        uint64_t(DOUBLE_INTEGRAL_PRECISION_LIMIT) < (UINT64_MAX - 10) / 10,
        "2^53 is way below UINT64_MAX, so |10 * index + digit| can't overflow");

    index = 10 * index + digit;

    // Also take the slow path when the string is larger-or-equals 2^53.
    if (index >= uint64_t(DOUBLE_INTEGRAL_PRECISION_LIMIT)) {
      return StringToTypedArrayIndexSlow(s);
    }
  }

  if (negative) {
    return mozilla::Some(UINT64_MAX);
  }
  return mozilla::Some(index);
}

template mozilla::Maybe<uint64_t> js::StringToTypedArrayIndex(
    mozilla::Range<const char16_t> s);

template mozilla::Maybe<uint64_t> js::StringToTypedArrayIndex(
    mozilla::Range<const Latin1Char> s);

bool js::SetTypedArrayElement(JSContext* cx, Handle<TypedArrayObject*> obj,
                              uint64_t index, HandleValue v,
                              ObjectOpResult& result) {
  switch (obj->type()) {
#define SET_TYPED_ARRAY_ELEMENT(_, T, N) \
  case Scalar::N:                        \
    return TypedArrayObjectTemplate<T>::setElement(cx, obj, index, v, result);
    JS_FOR_EACH_TYPED_ARRAY(SET_TYPED_ARRAY_ELEMENT)
#undef SET_TYPED_ARRAY_ELEMENT
    case Scalar::MaxTypedArrayViewType:
    case Scalar::Int64:
    case Scalar::Simd128:
      break;
  }

  MOZ_CRASH("Unsupported TypedArray type");
}

// ES2025 draft rev ac21460fedf4b926520b06c9820bdbebad596a8b
// 10.4.5.3 [[DefineOwnProperty]], step 1.b.
bool js::DefineTypedArrayElement(JSContext* cx, Handle<TypedArrayObject*> obj,
                                 uint64_t index,
                                 Handle<PropertyDescriptor> desc,
                                 ObjectOpResult& result) {
  // These are all substeps of 1.b.

  // Step i.
  if (index >= obj->length().valueOr(0)) {
    if (obj->hasDetachedBuffer()) {
      return result.fail(JSMSG_TYPED_ARRAY_DETACHED);
    }
    return result.fail(JSMSG_DEFINE_BAD_INDEX);
  }

  // TypedArray elements are modifiable unless the backing buffer is immutable.
  bool modifiable = !obj->is<ImmutableTypedArrayObject>();

  // Step ii.
  if (desc.hasConfigurable() && desc.configurable() != modifiable) {
    return result.fail(JSMSG_CANT_REDEFINE_PROP);
  }

  // Step iii.
  if (desc.hasEnumerable() && !desc.enumerable()) {
    return result.fail(JSMSG_CANT_REDEFINE_PROP);
  }

  // Step iv.
  if (desc.isAccessorDescriptor()) {
    return result.fail(JSMSG_CANT_REDEFINE_PROP);
  }

  // Step v.
  if (desc.hasWritable() && desc.writable() != modifiable) {
    return result.fail(JSMSG_CANT_REDEFINE_PROP);
  }

  // Step vi.
  if (desc.hasValue()) {
    if (modifiable) {
      return SetTypedArrayElement(cx, obj, index, desc.value(), result);
    }

    Rooted<Value> currentValue(cx);
    if (!obj->getElement<CanGC>(cx, index, &currentValue)) {
      return false;
    }

    bool sameValue;
    if (!SameValue(cx, desc.value(), currentValue, &sameValue)) {
      return false;
    }

    if (!sameValue) {
      return result.fail(JSMSG_CANT_REDEFINE_PROP);
    }
  }

  // Step vii.
  return result.succeed();
}

template <typename T, typename U>
static constexpr typename std::enable_if_t<
    std::numeric_limits<T>::is_integer && !std::numeric_limits<T>::is_signed, U>
UnsignedSortValue(U val) {
  return val;
}

template <typename T, typename U>
static constexpr typename std::enable_if_t<
    std::numeric_limits<T>::is_integer && std::numeric_limits<T>::is_signed, U>
UnsignedSortValue(U val) {
  // Flip sign bit.
  return val ^ static_cast<U>(std::numeric_limits<T>::min());
}

template <typename T, typename UnsignedT>
static constexpr
    typename std::enable_if_t<!std::numeric_limits<T>::is_integer, UnsignedT>
    UnsignedSortValue(UnsignedT val) {
  // Flip sign bit for positive numbers; flip all bits for negative numbers,
  // except negative NaNs.
  using FloatingPoint = mozilla::FloatingPoint<T>;
  static_assert(std::is_same_v<typename FloatingPoint::Bits, UnsignedT>,
                "FloatingPoint::Bits matches the unsigned int representation");

  // FF80'0000 is negative infinity, (FF80'0000, FFFF'FFFF] are all NaNs with
  // the sign-bit set (and the equivalent holds for double and float16 values).
  // So any value larger than negative infinity is a negative NaN.
  constexpr UnsignedT NegativeInfinity = mozilla::InfinityBits<T, 1>::value;
  if (val > NegativeInfinity) {
    return val;
  }
  if (val & FloatingPoint::kSignBit) {
    return ~val;
  }
  return val ^ FloatingPoint::kSignBit;
}

template <typename T, typename U>
static constexpr
    typename std::enable_if_t<std::numeric_limits<T>::is_integer, U>
    ToCountingSortKey(U val) {
  return UnsignedSortValue<T, U>(val);
}

template <typename T, typename U>
static constexpr
    typename std::enable_if_t<std::numeric_limits<T>::is_integer, U>
    FromCountingSortKey(U val) {
  // ToCountingSortKey for integers is a self-inverse function.
  return ToCountingSortKey<T, U>(val);
}

/**
 * UnsignedSortValue for floating point values isn't reversible, so we use a
 * different implementation for counting sort to keep NaN payloads unmodified
 * for consistency with TypedArrayRadixSort and TypedArrayStdSort.
 *
 * Mapping overview:
 * - Largest -NaN      = FFFF -> FFFF
 * - Smallest -NaN     = FC01 -> FC01
 * - Negative infinity = FC00 -> 0000
 * - Negative zero     = 8000 -> 7C00
 * - Positive zero     = 0000 -> 7C01
 * - Positive infinity = 7C00 -> F801
 * - Smallest +NaN     = 7C01 -> F802
 * - Largest +NaN      = 7FFF -> FC00
 */
template <typename T, typename U>
static constexpr typename std::enable_if_t<std::is_same_v<T, js::float16>, U>
ToCountingSortKey(U val) {
  using FloatingPoint = mozilla::FloatingPoint<T>;
  static_assert(std::is_same_v<typename FloatingPoint::Bits, U>,
                "FloatingPoint::Bits matches the unsigned int representation");

  constexpr U PositiveInfinity = mozilla::InfinityBits<T, 0>::value;
  constexpr U NegativeInfinity = mozilla::InfinityBits<T, 1>::value;

  // Any value larger than negative infinity is a negative NaN. Place those at
  // the very end.
  if (val > NegativeInfinity) {
    return val;
  }

  // Map negative values, starting at negative infinity which is mapped to zero.
  if (val & FloatingPoint::kSignBit) {
    return NegativeInfinity - val;
  }

  // Map positive values right after the last negative value (negative zero).
  return val + (PositiveInfinity + 1);
}

/**
 * Reverse the mapping from ToCountingSortKey.
 */
template <typename T, typename U>
static constexpr typename std::enable_if_t<std::is_same_v<T, js::float16>, U>
FromCountingSortKey(U val) {
  using FloatingPoint = mozilla::FloatingPoint<T>;
  static_assert(std::is_same_v<typename FloatingPoint::Bits, U>,
                "FloatingPoint::Bits matches the unsigned int representation");

  constexpr U PositiveInfinity = mozilla::InfinityBits<T, 0>::value;
  constexpr U NegativeInfinity = mozilla::InfinityBits<T, 1>::value;

  // Negative NaN are unchanged.
  if (val > NegativeInfinity) {
    return val;
  }

  // Any value larger than 0x7C00 was a positive number, including positive NaN.
  if (val > PositiveInfinity) {
    return val - (PositiveInfinity + 1);
  }

  // Any other value was a negative number, excluding negative NaN.
  return NegativeInfinity - val;
}

template <typename T>
static typename std::enable_if_t<std::numeric_limits<T>::is_integer>
TypedArrayStdSort(SharedMem<void*> data, size_t length) {
  T* unwrapped = data.cast<T*>().unwrapUnshared();
  std::sort(unwrapped, unwrapped + length);
}

template <typename T>
static typename std::enable_if_t<!std::numeric_limits<T>::is_integer>
TypedArrayStdSort(SharedMem<void*> data, size_t length) {
  // Sort on the unsigned representation for performance reasons.
  using UnsignedT =
      typename mozilla::UnsignedStdintTypeForSize<sizeof(T)>::Type;
  UnsignedT* unwrapped = data.cast<UnsignedT*>().unwrapUnshared();
  std::sort(unwrapped, unwrapped + length, [](UnsignedT x, UnsignedT y) {
    constexpr auto SortValue = UnsignedSortValue<T, UnsignedT>;
    return SortValue(x) < SortValue(y);
  });
}

template <typename T, typename Ops>
static typename std::enable_if_t<std::is_same_v<Ops, UnsharedOps>, bool>
TypedArrayStdSort(JSContext* cx, TypedArrayObject* typedArray, size_t length) {
  TypedArrayStdSort<T>(typedArray->dataPointerEither(), length);
  return true;
}

template <typename T, typename Ops>
static typename std::enable_if_t<std::is_same_v<Ops, SharedOps>, bool>
TypedArrayStdSort(JSContext* cx, TypedArrayObject* typedArray, size_t length) {
  // Always create a copy when sorting shared memory backed typed arrays to
  // ensure concurrent write accesses doesn't lead to UB when calling std::sort.
  auto ptr = cx->make_pod_array<T>(length);
  if (!ptr) {
    return false;
  }
  SharedMem<T*> unshared = SharedMem<T*>::unshared(ptr.get());
  SharedMem<T*> data = typedArray->dataPointerShared().cast<T*>();

  Ops::podCopy(unshared, data, length);

  TypedArrayStdSort<T>(unshared.template cast<void*>(), length);

  Ops::podCopy(data, unshared, length);

  return true;
}

template <typename T, typename Ops>
static bool TypedArrayCountingSort(JSContext* cx, TypedArrayObject* typedArray,
                                   size_t length) {
  // Determined by performance testing.
  if (length <= 64) {
    return TypedArrayStdSort<T, Ops>(cx, typedArray, length);
  }

  // Map signed values onto the unsigned range when storing in buffer.
  using UnsignedT =
      typename mozilla::UnsignedStdintTypeForSize<sizeof(T)>::Type;

  constexpr size_t InlineStorage = sizeof(T) == 1 ? 256 : 0;
  Vector<size_t, InlineStorage> buffer(cx);
  if (!buffer.resize(size_t(std::numeric_limits<UnsignedT>::max()) + 1)) {
    return false;
  }

  SharedMem<UnsignedT*> data =
      typedArray->dataPointerEither().cast<UnsignedT*>();

  // Populate the buffer.
  for (size_t i = 0; i < length; i++) {
    UnsignedT val = ToCountingSortKey<T, UnsignedT>(Ops::load(data + i));
    buffer[val]++;
  }

  // Traverse the buffer in order and write back elements to array.
  UnsignedT val = UnsignedT(-1);  // intentional overflow on first increment
  for (size_t i = 0; i < length;) {
    size_t j;
    do {
      j = buffer[++val];
    } while (j == 0);

    // Invariant: sum(buffer[val:]) == length-i
    MOZ_ASSERT(j <= length - i);

    for (; j > 0; j--) {
      Ops::store(data + i++, FromCountingSortKey<T, UnsignedT>(val));
    }
  }

  return true;
}

template <typename T, typename U, typename Ops>
static void SortByColumn(SharedMem<U*> data, size_t length, SharedMem<U*> aux,
                         uint8_t col) {
  static_assert(std::is_unsigned_v<U>, "SortByColumn sorts on unsigned values");
  static_assert(std::is_same_v<Ops, UnsharedOps>,
                "SortByColumn only works on unshared data");

  // |counts| is used to compute the starting index position for each key.
  // Letting counts[0] always be 0, simplifies the transform step below.
  // Example:
  //
  // Computing frequency counts for the input [1 2 1] gives:
  //      0 1 2 3 ... (keys)
  //      0 0 2 1     (frequencies)
  //
  // Transforming frequencies to indexes gives:
  //      0 1 2 3 ... (keys)
  //      0 0 2 3     (indexes)

  constexpr size_t R = 256;

  // Initialize all entries to zero.
  size_t counts[R + 1] = {};

  const auto ByteAtCol = [col](U x) {
    U y = UnsignedSortValue<T, U>(x);
    return static_cast<uint8_t>(y >> (col * 8));
  };

  // Compute frequency counts.
  for (size_t i = 0; i < length; i++) {
    U val = Ops::load(data + i);
    uint8_t b = ByteAtCol(val);
    counts[b + 1]++;
  }

  // Transform counts to indices.
  std::partial_sum(std::begin(counts), std::end(counts), std::begin(counts));

  // Distribute
  for (size_t i = 0; i < length; i++) {
    U val = Ops::load(data + i);
    uint8_t b = ByteAtCol(val);
    size_t j = counts[b]++;
    MOZ_ASSERT(j < length,
               "index is in bounds when |data| can't be modified concurrently");
    UnsharedOps::store(aux + j, val);
  }

  // Copy back
  Ops::podCopy(data, aux, length);
}

template <typename T, typename Ops>
static bool TypedArrayRadixSort(JSContext* cx, TypedArrayObject* typedArray,
                                size_t length) {
  // Determined by performance testing.
  constexpr size_t StdSortMinCutoff = sizeof(T) == 2 ? 64 : 256;

  // Radix sort uses O(n) additional space, limit this space to 64 MB.
  constexpr size_t StdSortMaxCutoff = (64 * 1024 * 1024) / sizeof(T);

  if constexpr (sizeof(T) == 2) {
    // Radix sort uses |n * sizeof(T)| additional space, whereas counting sort
    // uses |65536 * sizeof(size_t)| additional space. When the additional
    // space needed for radix sort exceeds the space needed for counting sort,
    // switch over to counting sort.
    //
    // It's faster to switch slightly earlier, so subtract a constant from the
    // exact value. This constant (2048) was determined by performance testing.
    constexpr size_t CountingSortMaxCutoff =
        65536 * (sizeof(size_t) / sizeof(T)) - 2048;
    static_assert(CountingSortMaxCutoff < StdSortMaxCutoff);

    if (length >= CountingSortMaxCutoff) {
      return TypedArrayCountingSort<T, Ops>(cx, typedArray, length);
    }
  }

  if (length <= StdSortMinCutoff || length >= StdSortMaxCutoff) {
    return TypedArrayStdSort<T, Ops>(cx, typedArray, length);
  }

  using UnsignedT =
      typename mozilla::UnsignedStdintTypeForSize<sizeof(T)>::Type;

  auto ptr = cx->make_zeroed_pod_array<UnsignedT>(length);
  if (!ptr) {
    return false;
  }
  SharedMem<UnsignedT*> aux = SharedMem<UnsignedT*>::unshared(ptr.get());

  SharedMem<UnsignedT*> data =
      typedArray->dataPointerEither().cast<UnsignedT*>();

  // Always create a copy when sorting shared memory backed typed arrays to
  // ensure concurrent write accesses don't lead to computing bad indices.
  SharedMem<UnsignedT*> unshared;
  SharedMem<UnsignedT*> shared;
  UniquePtr<UnsignedT[], JS::FreePolicy> ptrUnshared;
  if constexpr (std::is_same_v<Ops, SharedOps>) {
    ptrUnshared = cx->make_pod_array<UnsignedT>(length);
    if (!ptrUnshared) {
      return false;
    }
    unshared = SharedMem<UnsignedT*>::unshared(ptrUnshared.get());
    shared = data;

    Ops::podCopy(unshared, shared, length);

    data = unshared;
  }

  for (uint8_t col = 0; col < sizeof(UnsignedT); col++) {
    SortByColumn<T, UnsignedT, UnsharedOps>(data, length, aux, col);
  }

  if constexpr (std::is_same_v<Ops, SharedOps>) {
    Ops::podCopy(shared, unshared, length);
  }

  return true;
}

using TypedArraySortFn = bool (*)(JSContext*, TypedArrayObject*, size_t length);

template <typename T, typename Ops>
static constexpr typename std::enable_if_t<sizeof(T) == 1, TypedArraySortFn>
TypedArraySort() {
  return TypedArrayCountingSort<T, Ops>;
}

template <typename T, typename Ops>
static constexpr typename std::enable_if_t<sizeof(T) == 2 || sizeof(T) == 4,
                                           TypedArraySortFn>
TypedArraySort() {
  return TypedArrayRadixSort<T, Ops>;
}

template <typename T, typename Ops>
static constexpr typename std::enable_if_t<sizeof(T) == 8, TypedArraySortFn>
TypedArraySort() {
  return TypedArrayStdSort<T, Ops>;
}

static bool TypedArraySortWithoutComparator(JSContext* cx,
                                            TypedArrayObject* typedArray,
                                            size_t len) {
  bool isShared = typedArray->isSharedMemory();
  switch (typedArray->type()) {
#define SORT(_, T, N)                                               \
  case Scalar::N:                                                   \
    if (isShared) {                                                 \
      if (!TypedArraySort<T, SharedOps>()(cx, typedArray, len)) {   \
        return false;                                               \
      }                                                             \
    } else {                                                        \
      if (!TypedArraySort<T, UnsharedOps>()(cx, typedArray, len)) { \
        return false;                                               \
      }                                                             \
    }                                                               \
    break;
    JS_FOR_EACH_TYPED_ARRAY(SORT)
#undef SORT
    default:
      MOZ_CRASH("Unsupported TypedArray type");
  }
  return true;
}

static MOZ_ALWAYS_INLINE bool TypedArraySortPrologue(JSContext* cx,
                                                     Handle<Value> thisv,
                                                     Handle<Value> comparefn,
                                                     ArraySortData* d,
                                                     bool* done) {
  // https://tc39.es/ecma262/#sec-%typedarray%.prototype.sort
  // 23.2.3.29 %TypedArray%.prototype.sort ( comparefn )

  // Step 1.
  if (MOZ_UNLIKELY(!comparefn.isUndefined() && !IsCallable(comparefn))) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_BAD_TYPEDARRAY_SORT_ARG);
    return false;
  }

  // Steps 2-3.
  Rooted<TypedArrayObject*> tarrayUnwrapped(
      cx, UnwrapAndTypeCheckValue<TypedArrayObject>(cx, thisv, [cx, &thisv]() {
        JS_ReportErrorNumberUTF8(cx, GetErrorMessage, nullptr,
                                 JSMSG_INCOMPATIBLE_METHOD, "sort", "method",
                                 InformalValueTypeName(thisv));
      }));
  if (!tarrayUnwrapped) {
    return false;
  }
  auto arrayLength = tarrayUnwrapped->length();
  if (!arrayLength) {
    ReportOutOfBounds(cx, tarrayUnwrapped);
    return false;
  }

  // Additional step from Immutable ArrayBuffer proposal.
  if (tarrayUnwrapped->is<ImmutableTypedArrayObject>()) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_ARRAYBUFFER_IMMUTABLE);
    return false;
  }

  // Step 4.
  size_t len = *arrayLength;

  // Arrays with less than two elements remain unchanged when sorted.
  if (len <= 1) {
    d->setReturnValue(&thisv.toObject());
    *done = true;
    return true;
  }

  // Fast path for sorting without a comparator.
  if (comparefn.isUndefined()) {
    if (!TypedArraySortWithoutComparator(cx, tarrayUnwrapped, len)) {
      return false;
    }
    d->setReturnValue(&thisv.toObject());
    *done = true;
    return true;
  }

  // Ensure length * 2 (used below) doesn't overflow UINT32_MAX.
  if (MOZ_UNLIKELY(len > UINT32_MAX / 2)) {
    ReportAllocationOverflow(cx);
    return false;
  }

  // Merge sort requires extra scratch space.
  bool needsScratchSpace = len > ArraySortData::InsertionSortMaxLength;

  Rooted<ArraySortData::ValueVector> vec(cx);
  if (MOZ_UNLIKELY(!vec.resize(needsScratchSpace ? (2 * len) : len))) {
    ReportOutOfMemory(cx);
    return false;
  }

  // Copy elements to JS Value vector.
  if (!TypedArrayObject::getElements(cx, tarrayUnwrapped, len, vec.begin())) {
    return false;
  }

  d->init(&thisv.toObject(), &comparefn.toObject(), std::move(vec.get()), len,
          len);

  // Continue in ArraySortData::sortTypedArrayWithComparator.
  MOZ_ASSERT(!*done);
  return true;
}

// Copies sorted elements back to the typed array.
template <typename T, typename Ops>
static void StoreSortedElements(TypedArrayObject* tarray, Value* elements,
                                size_t len) {
  SharedMem<T*> data = tarray->dataPointerEither().cast<T*>();
  for (size_t i = 0; i < len; i++) {
    T val;
    if constexpr (!std::numeric_limits<T>::is_integer) {
      val = elements[i].toDouble();
    } else if constexpr (std::is_same_v<T, int64_t>) {
      val = BigInt::toInt64(elements[i].toBigInt());
    } else if constexpr (std::is_same_v<T, uint64_t>) {
      val = BigInt::toUint64(elements[i].toBigInt());
    } else if constexpr (std::is_same_v<T, uint32_t>) {
      val = uint32_t(elements[i].toNumber());
    } else {
      val = elements[i].toInt32();
    }
    Ops::store(data + i, val);
  }
}

// static
ArraySortResult ArraySortData::sortTypedArrayWithComparator(ArraySortData* d) {
  ArraySortResult result =
      sortWithComparatorShared<ArraySortKind::TypedArray>(d);
  if (result != ArraySortResult::Done) {
    return result;
  }

  // Copy sorted elements to the typed array.
  JSContext* cx = d->cx();
  Rooted<TypedArrayObject*> tarrayUnwrapped(
      cx, UnwrapAndDowncastObject<TypedArrayObject>(cx, d->obj_));
  if (MOZ_UNLIKELY(!tarrayUnwrapped)) {
    return ArraySortResult::Failure;
  }

  auto length = tarrayUnwrapped->length();
  if (MOZ_LIKELY(length)) {
    size_t len = std::min<size_t>(*length, d->denseLen);
    Value* elements = d->list;
    bool isShared = tarrayUnwrapped->isSharedMemory();
    switch (tarrayUnwrapped->type()) {
#define SORT(_, T, N)                                                      \
  case Scalar::N:                                                          \
    if (isShared) {                                                        \
      StoreSortedElements<T, SharedOps>(tarrayUnwrapped, elements, len);   \
    } else {                                                               \
      StoreSortedElements<T, UnsharedOps>(tarrayUnwrapped, elements, len); \
    }                                                                      \
    break;
      JS_FOR_EACH_TYPED_ARRAY(SORT)
#undef SORT
      default:
        MOZ_CRASH("Unsupported TypedArray type");
    }
  }

  d->freeMallocData();
  d->setReturnValue(d->obj_);
  return ArraySortResult::Done;
}

// https://tc39.es/ecma262/#sec-%typedarray%.prototype.sort
// 23.2.3.29 %TypedArray%.prototype.sort ( comparefn )
// static
bool TypedArrayObject::sort(JSContext* cx, unsigned argc, Value* vp) {
  AutoJSMethodProfilerEntry pseudoFrame(cx, "[TypedArray].prototype", "sort");
  CallArgs args = CallArgsFromVp(argc, vp);

  // If we have a comparator argument, use the JIT trampoline implementation
  // instead. This avoids a performance cliff (especially with large arrays)
  // because C++ => JIT calls are much slower than Trampoline => JIT calls.
  if (args.hasDefined(0) && jit::IsBaselineInterpreterEnabled() &&
      !jit::TooManyActualArguments(args.length())) {
    return CallTrampolineNativeJitCode(
        cx, jit::TrampolineNative::TypedArraySort, args);
  }

  Rooted<ArraySortData> data(cx, cx);

  // On all return paths other than ArraySortData::sortTypedArrayWithComparator
  // returning Done, we call freeMallocData to not fail debug assertions. This
  // matches the JIT trampoline where we can't rely on C++ destructors.
  auto freeData =
      mozilla::MakeScopeExit([&]() { data.get().freeMallocData(); });

  bool done = false;
  if (!TypedArraySortPrologue(cx, args.thisv(), args.get(0), data.address(),
                              &done)) {
    return false;
  }
  if (done) {
    args.rval().set(data.get().returnValue());
    return true;
  }

  FixedInvokeArgs<2> callArgs(cx);
  Rooted<Value> rval(cx);

  while (true) {
    ArraySortResult res =
        ArraySortData::sortTypedArrayWithComparator(data.address());
    switch (res) {
      case ArraySortResult::Failure:
        return false;

      case ArraySortResult::Done:
        freeData.release();
        args.rval().set(data.get().returnValue());
        return true;

      case ArraySortResult::CallJS:
      case ArraySortResult::CallJSSameRealmNoUnderflow:
        MOZ_ASSERT(data.get().comparatorThisValue().isUndefined());
        MOZ_ASSERT(&args[0].toObject() == data.get().comparator());
        callArgs[0].set(data.get().comparatorArg(0));
        callArgs[1].set(data.get().comparatorArg(1));
        if (!js::Call(cx, args[0], UndefinedHandleValue, callArgs, &rval)) {
          return false;
        }
        data.get().setComparatorReturnValue(rval);
        break;
    }
  }
}

ArraySortResult js::TypedArraySortFromJit(
    JSContext* cx, jit::TrampolineNativeFrameLayout* frame) {
  AutoJSMethodProfilerEntry pseudoFrame(cx, "[TypedArray].prototype", "sort");
  // Initialize the ArraySortData class stored in the trampoline frame.
  void* dataUninit = frame->getFrameData<ArraySortData>();
  auto* data = new (dataUninit) ArraySortData(cx);

  Rooted<Value> thisv(cx, frame->thisv());
  Rooted<Value> comparefn(cx);
  if (frame->numActualArgs() > 0) {
    comparefn = frame->actualArgs()[0];
  }

  bool done = false;
  if (!TypedArraySortPrologue(cx, thisv, comparefn, data, &done)) {
    return ArraySortResult::Failure;
  }
  if (done) {
    data->freeMallocData();
    return ArraySortResult::Done;
  }

  return ArraySortData::sortTypedArrayWithComparator(data);
}

/* JS Public API */

#define IMPL_TYPED_ARRAY_JSAPI_CONSTRUCTORS(ExternalType, NativeType, Name)   \
  JS_PUBLIC_API JSObject* JS_New##Name##Array(JSContext* cx,                  \
                                              size_t nelements) {             \
    return TypedArrayObjectTemplate<NativeType>::fromLength(cx, nelements);   \
  }                                                                           \
                                                                              \
  JS_PUBLIC_API JSObject* JS_New##Name##ArrayFromArray(JSContext* cx,         \
                                                       HandleObject other) {  \
    return TypedArrayObjectTemplate<NativeType>::fromArray(cx, other);        \
  }                                                                           \
                                                                              \
  JS_PUBLIC_API JSObject* JS_New##Name##ArrayWithBuffer(                      \
      JSContext* cx, HandleObject arrayBuffer, size_t byteOffset,             \
      int64_t length) {                                                       \
    return TypedArrayObjectTemplate<NativeType>::fromBuffer(                  \
        cx, arrayBuffer, byteOffset, length);                                 \
  }                                                                           \
                                                                              \
  JS_PUBLIC_API JSObject* js::Unwrap##Name##Array(JSObject* obj) {            \
    obj = obj->maybeUnwrapIf<TypedArrayObject>();                             \
    if (!obj) {                                                               \
      return nullptr;                                                         \
    }                                                                         \
    const JSClass* clasp = obj->getClass();                                   \
    if (clasp != FixedLengthTypedArrayObjectTemplate<                         \
                     NativeType>::instanceClass() &&                          \
        clasp !=                                                              \
            ResizableTypedArrayObjectTemplate<NativeType>::instanceClass() && \
        clasp !=                                                              \
            ImmutableTypedArrayObjectTemplate<NativeType>::instanceClass()) { \
      return nullptr;                                                         \
    }                                                                         \
    return obj;                                                               \
  }                                                                           \
                                                                              \
  JS_PUBLIC_API ExternalType* JS_Get##Name##ArrayLengthAndData(               \
      JSObject* obj, size_t* length, bool* isSharedMemory,                    \
      const JS::AutoRequireNoGC& nogc) {                                      \
    TypedArrayObject* tarr = obj->maybeUnwrapAs<TypedArrayObject>();          \
    if (!tarr) {                                                              \
      return nullptr;                                                         \
    }                                                                         \
    mozilla::Span<ExternalType> span =                                        \
        JS::TypedArray<JS::Scalar::Name>::fromObject(tarr).getData(           \
            isSharedMemory, nogc);                                            \
    *length = span.Length();                                                  \
    return span.data();                                                       \
  }                                                                           \
                                                                              \
  JS_PUBLIC_API ExternalType* JS_Get##Name##ArrayData(                        \
      JSObject* obj, bool* isSharedMemory, const JS::AutoRequireNoGC& nogc) { \
    size_t length;                                                            \
    return JS_Get##Name##ArrayLengthAndData(obj, &length, isSharedMemory,     \
                                            nogc);                            \
  }                                                                           \
  JS_PUBLIC_API JSObject* JS_GetObjectAs##Name##Array(                        \
      JSObject* obj, size_t* length, bool* isShared, ExternalType** data) {   \
    obj = js::Unwrap##Name##Array(obj);                                       \
    if (!obj) {                                                               \
      return nullptr;                                                         \
    }                                                                         \
    TypedArrayObject* tarr = &obj->as<TypedArrayObject>();                    \
    *length = tarr->length().valueOr(0);                                      \
    *isShared = tarr->isSharedMemory();                                       \
    *data = static_cast<ExternalType*>(tarr->dataPointerEither().unwrap(      \
        /*safe - caller sees isShared flag*/));                               \
    return obj;                                                               \
  }

JS_FOR_EACH_TYPED_ARRAY(IMPL_TYPED_ARRAY_JSAPI_CONSTRUCTORS)
#undef IMPL_TYPED_ARRAY_JSAPI_CONSTRUCTORS

JS_PUBLIC_API bool JS_IsTypedArrayObject(JSObject* obj) {
  return obj->canUnwrapAs<TypedArrayObject>();
}

JS_PUBLIC_API size_t JS_GetTypedArrayLength(JSObject* obj) {
  TypedArrayObject* tarr = obj->maybeUnwrapAs<TypedArrayObject>();
  if (!tarr) {
    return 0;
  }
  return tarr->length().valueOr(0);
}

JS_PUBLIC_API size_t JS_GetTypedArrayByteOffset(JSObject* obj) {
  TypedArrayObject* tarr = obj->maybeUnwrapAs<TypedArrayObject>();
  if (!tarr) {
    return 0;
  }
  return tarr->byteOffset().valueOr(0);
}

JS_PUBLIC_API size_t JS_GetTypedArrayByteLength(JSObject* obj) {
  TypedArrayObject* tarr = obj->maybeUnwrapAs<TypedArrayObject>();
  if (!tarr) {
    return 0;
  }
  return tarr->byteLength().valueOr(0);
}

JS_PUBLIC_API bool JS_GetTypedArraySharedness(JSObject* obj) {
  TypedArrayObject* tarr = obj->maybeUnwrapAs<TypedArrayObject>();
  if (!tarr) {
    return false;
  }
  return tarr->isSharedMemory();
}

JS_PUBLIC_API JS::Scalar::Type JS_GetArrayBufferViewType(JSObject* obj) {
  ArrayBufferViewObject* view = obj->maybeUnwrapAs<ArrayBufferViewObject>();
  if (!view) {
    return Scalar::MaxTypedArrayViewType;
  }

  if (view->is<TypedArrayObject>()) {
    return view->as<TypedArrayObject>().type();
  }
  if (view->is<DataViewObject>()) {
    return Scalar::MaxTypedArrayViewType;
  }
  MOZ_CRASH("invalid ArrayBufferView type");
}

namespace JS {

const JSClass* const TypedArray_base::fixedLengthClasses =
    TypedArrayObject::fixedLengthClasses;
const JSClass* const TypedArray_base::immutableClasses =
    TypedArrayObject::immutableClasses;
const JSClass* const TypedArray_base::resizableClasses =
    TypedArrayObject::resizableClasses;

#define INSTANTIATE(ExternalType, NativeType, Name) \
  template class TypedArray<JS::Scalar::Name>;
JS_FOR_EACH_TYPED_ARRAY(INSTANTIATE)
#undef INSTANTIATE

JS::ArrayBufferOrView JS::ArrayBufferOrView::unwrap(JSObject* maybeWrapped) {
  if (!maybeWrapped) {
    return JS::ArrayBufferOrView(nullptr);
  }
  auto* ab = maybeWrapped->maybeUnwrapIf<ArrayBufferObjectMaybeShared>();
  if (ab) {
    return ArrayBufferOrView::fromObject(ab);
  }

  return ArrayBufferView::unwrap(maybeWrapped);
}

bool JS::ArrayBufferOrView::isDetached() const {
  MOZ_ASSERT(obj);
  if (obj->is<ArrayBufferObjectMaybeShared>()) {
    return obj->as<ArrayBufferObjectMaybeShared>().isDetached();
  } else {
    return obj->as<ArrayBufferViewObject>().hasDetachedBuffer();
  }
}

bool JS::ArrayBufferOrView::isResizable() const {
  MOZ_ASSERT(obj);
  if (obj->is<ArrayBufferObjectMaybeShared>()) {
    return obj->as<ArrayBufferObjectMaybeShared>().isResizable();
  } else {
    return obj->as<ArrayBufferViewObject>().hasResizableBuffer();
  }
}

bool JS::ArrayBufferOrView::isImmutable() const {
  MOZ_ASSERT(obj);
  if (obj->is<ArrayBufferObjectMaybeShared>()) {
    return obj->as<ArrayBufferObjectMaybeShared>().isImmutable();
  } else {
    return obj->as<ArrayBufferViewObject>().hasImmutableBuffer();
  }
}

JS::TypedArray_base JS::TypedArray_base::fromObject(JSObject* unwrapped) {
  if (unwrapped && unwrapped->is<TypedArrayObject>()) {
    return TypedArray_base(unwrapped);
  }
  return TypedArray_base(nullptr);
}

// Template getData function for TypedArrays, implemented here because
// it requires internal APIs.
template <JS::Scalar::Type EType>
typename mozilla::Span<typename TypedArray<EType>::DataType>
TypedArray<EType>::getData(bool* isSharedMemory, const AutoRequireNoGC&) {
  using ExternalType = TypedArray<EType>::DataType;
  if (!obj) {
    return nullptr;
  }
  TypedArrayObject* tarr = &obj->as<TypedArrayObject>();
  MOZ_ASSERT(tarr);
  *isSharedMemory = tarr->isSharedMemory();
  return {static_cast<ExternalType*>(tarr->dataPointerEither().unwrap(
              /*safe - caller sees isShared*/)),
          tarr->length().valueOr(0)};
};

// Force the method defined above to actually be instantianted in this
// compilation unit and emitted into the object file, since otherwise a binary
// could include the header file and emit an undefined symbol that would not be
// satisfied by the linker. (This happens with opt gtest, at least. In a DEBUG
// build, the header contains a call to this function so it will always be
// emitted.)
#define INSTANTIATE_GET_DATA(a, b, Name)                                  \
  template mozilla::Span<typename TypedArray<JS::Scalar::Name>::DataType> \
  TypedArray<JS::Scalar::Name>::getData(bool* isSharedMemory,             \
                                        const AutoRequireNoGC&);
JS_FOR_EACH_TYPED_ARRAY(INSTANTIATE_GET_DATA)
#undef INSTANTIATE_GET_DATA

} /* namespace JS */
