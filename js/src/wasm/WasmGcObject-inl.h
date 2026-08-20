/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef wasm_WasmGcObject_inl_h
#define wasm_WasmGcObject_inl_h

#include "wasm/WasmGcObject.h"

#include "mozilla/Attributes.h"
#include "mozilla/DebugOnly.h"

#include "util/Memory.h"

#include "gc/Nursery-inl.h"
#include "gc/ObjectKind-inl.h"
#include "vm/JSContext-inl.h"

//=========================================================================
// WasmStructObject inlineable allocation methods

// Maximum size of trailer block to allocate directly in the nursery.
//
// For objects that die in the nursery, direct nursery allocation is faster and
// is better for cache locality. For objects that survive, direct nursery
// allocation incurs the overhead of copying the data. This parameter should be
// chosen to balance these based on the expected allocation sizes and tenuring
// rates in workloads we care about.
//
// This is set to a lower value than the default (Nursery::MaxNurseryBufferSize)
// because we tend to get higher tenuring rates in Wasm GC benchmarks.
static constexpr size_t MaxNurseryTrailerSize = 256;
static_assert(MaxNurseryTrailerSize < js::gc::ChunkSize);

namespace js {

/* static */
template <bool ZeroFields>
MOZ_ALWAYS_INLINE WasmStructObject* WasmStructObject::createStructIL(
    JSContext* cx, wasm::TypeDefInstanceData* typeDefData,
    gc::AllocSite* allocSite, js::gc::Heap initialHeap) {
  // It is up to our caller to ensure that `typeDefData` refers to a type that
  // doesn't need OOL storage.
  MOZ_ASSERT(typeDefData->cached.strukt.totalSizeOOL == 0);

  MOZ_ASSERT(IsWasmGcObjectClass(typeDefData->clasp));
  MOZ_ASSERT(!typeDefData->clasp->isNativeObject());
  MOZ_ASSERT(!IsFinalizedKind(typeDefData->cached.strukt.allocKind));

  AutoSetNewObjectMetadata metadata(cx);
  debugCheckNewObject(typeDefData->shape, typeDefData->cached.strukt.allocKind,
                      initialHeap);

  mozilla::DebugOnly<const wasm::TypeDef*> typeDef = typeDefData->typeDef;
  MOZ_ASSERT(typeDef->kind() == wasm::TypeDefKind::Struct);

  // This doesn't need to be rooted, since all we do with it prior to
  // return is to zero out the fields (and then only if ZeroFields is true).
  WasmStructObject* structObj = (WasmStructObject*)cx->newCell<WasmGcObject>(
      typeDefData->cached.strukt.allocKind, initialHeap, typeDefData->clasp,
      allocSite);
  if (MOZ_UNLIKELY(!structObj)) {
    ReportOutOfMemory(cx);
    return nullptr;
  }

  structObj->initShape(typeDefData->shape);
  structObj->superTypeVector_ = typeDefData->superTypeVector;
  if constexpr (ZeroFields) {
    size_t headerSize = typeDefData->cached.strukt.payloadOffsetIL;
    memset((uint8_t*)structObj + headerSize, 0,
           typeDefData->cached.strukt.totalSizeIL - headerSize);
  }

  MOZ_ASSERT(typeDefData->clasp->shouldDelayMetadataBuilder());
  cx->realm()->setObjectPendingMetadata(structObj);

  js::gc::gcprobes::CreateObject(structObj);
  probes::CreateObject(cx, structObj);

  return structObj;
}

/* static */
template <bool ZeroFields>
MOZ_ALWAYS_INLINE WasmStructObject* WasmStructObject::createStructOOL(
    JSContext* cx, wasm::TypeDefInstanceData* typeDefData,
    gc::AllocSite* allocSite, js::gc::Heap initialHeap) {
  // It is up to our caller to ensure that `typeDefData` refers to a type that
  // needs OOL storage.
  MOZ_ASSERT(typeDefData->cached.strukt.totalSizeOOL > 0);

  MOZ_ASSERT(IsWasmGcObjectClass(typeDefData->clasp));
  MOZ_ASSERT(!typeDefData->clasp->isNativeObject());
  MOZ_ASSERT(!IsFinalizedKind(typeDefData->cached.strukt.allocKind));

  AutoSetNewObjectMetadata metadata(cx);
  debugCheckNewObject(typeDefData->shape, typeDefData->cached.strukt.allocKind,
                      initialHeap);

  mozilla::DebugOnly<const wasm::TypeDef*> typeDef = typeDefData->typeDef;
  MOZ_ASSERT(typeDef->kind() == wasm::TypeDefKind::Struct);

  uint32_t outlineBytes = typeDefData->cached.strukt.totalSizeOOL;

  // This doesn't need to be Rooted because the AllocateCellBuffer call that
  // follows can't trigger GC.
  auto* structObj = (WasmStructObject*)cx->newCell<WasmGcObject>(
      typeDefData->cached.strukt.allocKind, initialHeap, typeDefData->clasp,
      allocSite);
  if (MOZ_UNLIKELY(!structObj)) {
    ReportOutOfMemory(cx);
    return nullptr;
  }

  structObj->initShape(typeDefData->shape);
  structObj->superTypeVector_ = typeDefData->superTypeVector;

  uint8_t* outlineData = AllocateCellBuffer<uint8_t>(
      cx, structObj, outlineBytes, MaxNurseryTrailerSize);
  if (MOZ_UNLIKELY(!outlineData)) {
    // AllocateCellBuffer will have called ReportOutOfMemory(cx) itself,
    // so no need to do that here.
    size_t headerSize = typeDefData->cached.strukt.payloadOffsetIL;
    memset((uint8_t*)structObj + headerSize, 0,
           typeDefData->cached.strukt.totalSizeIL - headerSize);
    structObj->setOOLPointer(typeDefData, nullptr);
    return nullptr;
  }

  // Initialize the inline and outline data fields
  if constexpr (ZeroFields) {
    size_t headerSize = typeDefData->cached.strukt.payloadOffsetIL;
    memset((uint8_t*)structObj + headerSize, 0,
           typeDefData->cached.strukt.totalSizeIL - headerSize);
    memset(outlineData, 0, outlineBytes);
  }

  structObj->setOOLPointer(typeDefData, outlineData);

  MOZ_ASSERT(typeDefData->clasp->shouldDelayMetadataBuilder());
  cx->realm()->setObjectPendingMetadata(structObj);

  js::gc::gcprobes::CreateObject(structObj);
  probes::CreateObject(cx, structObj);

  return structObj;
}

//=========================================================================
// WasmArrayObject inlineable allocation methods

/* static */
inline gc::AllocKind WasmArrayObject::allocKindForOOL() {
  gc::AllocKind allocKind =
      gc::GetGCObjectKindForBytes(sizeof(WasmArrayObject));
  return gc::GetFinalizedAllocKindForClass(allocKind, &WasmArrayObject::class_);
}

/* static */
inline gc::AllocKind WasmArrayObject::allocKindForIL(uint32_t arrayDataBytes) {
  gc::AllocKind allocKind =
      gc::GetGCObjectKindForBytes(sizeof(WasmArrayObject) + arrayDataBytes);
  return gc::GetFinalizedAllocKindForClass(allocKind, &WasmArrayObject::class_);
}

inline gc::AllocKind WasmArrayObject::allocKind() const {
  if (isDataInline()) {
    // numElements_ was validated to not overflow when constructing this object
    uint32_t storageBytes = calcArrayDataBytesUnchecked(
        typeDef().arrayType().elementType().size(), numElements_);
    return allocKindForIL(storageBytes);
  }

  return allocKindForOOL();
}

/* static */
template <bool ZeroFields>
MOZ_ALWAYS_INLINE WasmArrayObject* WasmArrayObject::createArrayOOL(
    JSContext* cx, wasm::TypeDefInstanceData* typeDefData,
    js::gc::AllocSite* allocSite, js::gc::Heap initialHeap,
    uint32_t numElements, uint32_t arrayDataBytes) {
  STATIC_ASSERT_WASMARRAYELEMENTS_NUMELEMENTS_IS_U32;

  MOZ_ASSERT(IsWasmGcObjectClass(typeDefData->clasp));
  MOZ_ASSERT(!typeDefData->clasp->isNativeObject());
  gc::AllocKind allocKind = allocKindForOOL();
  AutoSetNewObjectMetadata metadata(cx);
  debugCheckNewObject(typeDefData->shape, allocKind, initialHeap);

  mozilla::DebugOnly<const wasm::TypeDef*> typeDef = typeDefData->typeDef;
  MOZ_ASSERT(typeDef->kind() == wasm::TypeDefKind::Array);

  // This routine is for large arrays with out-of-line data only. For small
  // arrays use createArrayIL.
  MOZ_ASSERT(arrayDataBytes > WasmArrayObject_MaxInlineBytes);

  // Ensured by WasmArrayObject::createArray.
  MOZ_ASSERT(arrayDataBytes <= uint32_t(wasm::MaxArrayPayloadBytes));

  // This doesn't need to be Rooted because the AllocateCellBuffer call that
  // follows can't trigger GC.
  auto* arrayObj = (WasmArrayObject*)cx->newCell<WasmGcObject>(
      allocKind, initialHeap, typeDefData->clasp, allocSite);
  if (MOZ_UNLIKELY(!arrayObj)) {
    ReportOutOfMemory(cx);
    return nullptr;
  }

  arrayObj->initShape(typeDefData->shape);
  arrayObj->superTypeVector_ = typeDefData->superTypeVector;

  uint8_t* oolAlloc = AllocateCellBuffer<uint8_t>(
      cx, arrayObj, sizeof(OOLDataHeader) + arrayDataBytes,
      MaxNurseryTrailerSize);
  if (MOZ_UNLIKELY(!oolAlloc)) {
    // AllocateCellBuffer will have called ReportOutOfMemory(cx) itself.

    // Initialize the failed array as an inline array to avoid issues with a
    // null data pointer.
    arrayObj->numElements_ = 0;
    arrayObj->data_ = arrayObj->inlineArrayData<uint8_t>();
    MOZ_ASSERT(arrayObj->isDataInline());
    return nullptr;
  }

  OOLDataHeader* oolHeader = (OOLDataHeader*)oolAlloc;
  new (oolHeader) OOLDataHeader();
  uint8_t* oolData = WasmArrayObject::oolDataHeaderToDataPointer(oolHeader);

  arrayObj->numElements_ = numElements;
  arrayObj->data_ = oolData;
  if constexpr (ZeroFields) {
    MOZ_ASSERT(arrayDataBytes >=
               numElements * typeDefData->cached.array.elemSize);
    memset(arrayObj->data_, 0, arrayDataBytes);
  }

  MOZ_ASSERT(!arrayObj->isDataInline());

  MOZ_ASSERT(typeDefData->clasp->shouldDelayMetadataBuilder());
  cx->realm()->setObjectPendingMetadata(arrayObj);

  js::gc::gcprobes::CreateObject(arrayObj);
  probes::CreateObject(cx, arrayObj);

  return arrayObj;
}

template WasmArrayObject* WasmArrayObject::createArrayOOL<true>(
    JSContext* cx, wasm::TypeDefInstanceData* typeDefData,
    js::gc::AllocSite* allocSite, js::gc::Heap initialHeap,
    uint32_t numElements, uint32_t storageBytes);
template WasmArrayObject* WasmArrayObject::createArrayOOL<false>(
    JSContext* cx, wasm::TypeDefInstanceData* typeDefData,
    js::gc::AllocSite* allocSite, js::gc::Heap initialHeap,
    uint32_t numElements, uint32_t storageBytes);

/* static */
template <bool ZeroFields>
MOZ_ALWAYS_INLINE WasmArrayObject* WasmArrayObject::createArrayIL(
    JSContext* cx, wasm::TypeDefInstanceData* typeDefData,
    js::gc::AllocSite* allocSite, js::gc::Heap initialHeap,
    uint32_t numElements, uint32_t arrayDataBytes) {
  STATIC_ASSERT_WASMARRAYELEMENTS_NUMELEMENTS_IS_U32;

  MOZ_ASSERT(IsWasmGcObjectClass(typeDefData->clasp));
  MOZ_ASSERT(!typeDefData->clasp->isNativeObject());
  AutoSetNewObjectMetadata metadata(cx);
  gc::AllocKind allocKind = allocKindForIL(arrayDataBytes);
  debugCheckNewObject(typeDefData->shape, allocKind, initialHeap);

  mozilla::DebugOnly<const wasm::TypeDef*> typeDef = typeDefData->typeDef;
  MOZ_ASSERT(typeDef->kind() == wasm::TypeDefKind::Array);

  MOZ_ASSERT(arrayDataBytes <= WasmArrayObject_MaxInlineBytes);

  // There's no need for `arrayObj` to be rooted, since the only thing we're
  // going to do is fill in some bits of it, then return it.
  WasmArrayObject* arrayObj = (WasmArrayObject*)cx->newCell<WasmGcObject>(
      allocKind, initialHeap, typeDefData->clasp, allocSite);
  if (MOZ_UNLIKELY(!arrayObj)) {
    ReportOutOfMemory(cx);
    return nullptr;
  }

  arrayObj->initShape(typeDefData->shape);
  arrayObj->superTypeVector_ = typeDefData->superTypeVector;
  arrayObj->numElements_ = numElements;
  arrayObj->data_ = arrayObj->inlineArrayData<uint8_t>();

  if constexpr (ZeroFields) {
    MOZ_ASSERT(arrayDataBytes >=
               numElements * typeDefData->cached.array.elemSize);
    if (numElements > 0) {
      memset(arrayObj->data_, 0, arrayDataBytes);
    }
  }

  MOZ_ASSERT(arrayObj->isDataInline());

  MOZ_ASSERT(typeDefData->clasp->shouldDelayMetadataBuilder());
  cx->realm()->setObjectPendingMetadata(arrayObj);

  js::gc::gcprobes::CreateObject(arrayObj);
  probes::CreateObject(cx, arrayObj);

  return arrayObj;
}

template WasmArrayObject* WasmArrayObject::createArrayIL<true>(
    JSContext* cx, wasm::TypeDefInstanceData* typeDefData,
    js::gc::AllocSite* allocSite, js::gc::Heap initialHeap,
    uint32_t numElements, uint32_t arrayDataBytes);
template WasmArrayObject* WasmArrayObject::createArrayIL<false>(
    JSContext* cx, wasm::TypeDefInstanceData* typeDefData,
    js::gc::AllocSite* allocSite, js::gc::Heap initialHeap,
    uint32_t numElements, uint32_t arrayDataBytes);

/* static */
template <bool ZeroFields>
MOZ_ALWAYS_INLINE WasmArrayObject* WasmArrayObject::createArray(
    JSContext* cx, wasm::TypeDefInstanceData* typeDefData,
    js::gc::AllocSite* allocSite, js::gc::Heap initialHeap,
    uint32_t numElements) {
  MOZ_ASSERT(typeDefData->cached.array.elemSize ==
             typeDefData->typeDef->arrayType().elementType().size());
  mozilla::CheckedUint32 arrayDataBytes = calcArrayDataBytesChecked(
      typeDefData->cached.array.elemSize, numElements);
  if (!arrayDataBytes.isValid() ||
      arrayDataBytes.value() > uint32_t(wasm::MaxArrayPayloadBytes)) {
    js::ReportOversizedAllocation(cx, JSMSG_WASM_ARRAY_IMP_LIMIT);
    wasm::MarkPendingExceptionAsTrap(cx);
    return nullptr;
  }

  if (arrayDataBytes.value() <= WasmArrayObject_MaxInlineBytes) {
    return createArrayIL<ZeroFields>(cx, typeDefData, allocSite, initialHeap,
                                     numElements, arrayDataBytes.value());
  }

  return createArrayOOL<ZeroFields>(cx, typeDefData, allocSite, initialHeap,
                                    numElements, arrayDataBytes.value());
}

template WasmArrayObject* WasmArrayObject::createArray<true>(
    JSContext* cx, wasm::TypeDefInstanceData* typeDefData,
    js::gc::AllocSite* allocSite, js::gc::Heap initialHeap,
    uint32_t numElements);
template WasmArrayObject* WasmArrayObject::createArray<false>(
    JSContext* cx, wasm::TypeDefInstanceData* typeDefData,
    js::gc::AllocSite* allocSite, js::gc::Heap initialHeap,
    uint32_t numElements);

}  // namespace js

#endif /* wasm_WasmGcObject_inl_h */
