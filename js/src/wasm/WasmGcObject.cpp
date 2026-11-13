/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/DebugOnly.h"

#include "gc/Tracer.h"
#include "js/CharacterEncoding.h"
#include "js/friend/ErrorMessages.h"  // js::GetErrorMessage, JSMSG_*
#include "js/PropertySpec.h"
#include "js/ScalarType.h"  // js::Scalar::Type
#include "js/Vector.h"
#include "util/StringBuilder.h"
#include "vm/GlobalObject.h"
#include "vm/JSFunction.h"
#include "vm/JSObject.h"
#include "vm/PlainObject.h"  // js::PlainObject
#include "vm/PropertyResult.h"
#include "vm/Realm.h"
#include "vm/SelfHosting.h"
#include "vm/StringType.h"
#include "vm/TypedArrayObject.h"
#include "vm/Uint8Clamped.h"

#include "gc/BufferAllocator-inl.h"
#include "gc/GCContext-inl.h"  // GCContext::removeCellMemory
#include "gc/ObjectKind-inl.h"
#include "vm/JSContext-inl.h"
#include "wasm/WasmGcObject-inl.h"

using namespace js;
using namespace wasm;

// [SMDOC] Management of OOL storage areas for Wasm{Array,Struct}Object.
//
// WasmArrayObject always has its payload data stored in a block which is
// pointed to from the WasmArrayObject. The same is true for WasmStructObject in
// the case where the fields cannot fit in the object itself. These blocks are
// in some places referred to as "trailer blocks".
//
// These blocks are allocated in the same way as JSObects slots and element
// buffers, either using the GC's buffer allocator or directly in the nursery if
// they are small enough.
//
// They require the use of WasmArrayObject/WasmStructObject::obj_moved hooks to
// update the pointer and mark the allocation when the object gets tenured, and
// also update the pointer in case it is an internal pointer when the object is
// moved.
//
// The blocks are freed by the GC when no longer referenced.

//=========================================================================
// WasmGcObject

const ObjectOps WasmGcObject::objectOps_ = {
    WasmGcObject::obj_lookupProperty,            // lookupProperty
    WasmGcObject::obj_defineProperty,            // defineProperty
    WasmGcObject::obj_hasProperty,               // hasProperty
    WasmGcObject::obj_getProperty,               // getProperty
    WasmGcObject::obj_setProperty,               // setProperty
    WasmGcObject::obj_getOwnPropertyDescriptor,  // getOwnPropertyDescriptor
    WasmGcObject::obj_deleteProperty,            // deleteProperty
    nullptr,                                     // getElements
    nullptr,                                     // funToString
};

/* static */
bool WasmGcObject::obj_lookupProperty(JSContext* cx, HandleObject obj,
                                      HandleId id, MutableHandleObject objp,
                                      PropertyResult* propp) {
  objp.set(nullptr);
  propp->setNotFound();
  return true;
}

bool WasmGcObject::obj_defineProperty(JSContext* cx, HandleObject obj,
                                      HandleId id,
                                      Handle<PropertyDescriptor> desc,
                                      ObjectOpResult& result) {
  result.failReadOnly();
  return true;
}

bool WasmGcObject::obj_hasProperty(JSContext* cx, HandleObject obj, HandleId id,
                                   bool* foundp) {
  *foundp = false;
  return true;
}

bool WasmGcObject::obj_getProperty(JSContext* cx, HandleObject obj,
                                   HandleValue receiver, HandleId id,
                                   MutableHandleValue vp) {
  vp.setUndefined();
  return true;
}

bool WasmGcObject::obj_setProperty(JSContext* cx, HandleObject obj, HandleId id,
                                   HandleValue v, HandleValue receiver,
                                   ObjectOpResult& result) {
  JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                            JSMSG_WASM_MODIFIED_GC_OBJECT);
  return false;
}

bool WasmGcObject::obj_getOwnPropertyDescriptor(
    JSContext* cx, HandleObject obj, HandleId id,
    MutableHandle<mozilla::Maybe<PropertyDescriptor>> desc) {
  desc.reset();
  return true;
}

bool WasmGcObject::obj_deleteProperty(JSContext* cx, HandleObject obj,
                                      HandleId id, ObjectOpResult& result) {
  JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                            JSMSG_WASM_MODIFIED_GC_OBJECT);
  return false;
}

bool WasmGcObject::lookUpProperty(JSContext* cx, Handle<WasmGcObject*> obj,
                                  jsid id, WasmGcObject::PropOffset* offset,
                                  StorageType* type) {
  switch (obj->kind()) {
    case wasm::TypeDefKind::Struct: {
      const auto& structType = obj->typeDef().structType();
      uint32_t index;
      if (!IdIsIndex(id, &index)) {
        return false;
      }
      MOZ_ASSERT(structType.fields_.length() ==
                 structType.fieldAccessPaths_.length());
      if (index >= structType.fields_.length()) {
        JS_ReportErrorNumberUTF8(cx, GetErrorMessage, nullptr,
                                 JSMSG_WASM_OUT_OF_BOUNDS);
        return false;
      }
      offset->set(index);
      *type = structType.fields_[index].type;
      return true;
    }
    case wasm::TypeDefKind::Array: {
      const auto& arrayType = obj->typeDef().arrayType();

      uint32_t index;
      if (!IdIsIndex(id, &index)) {
        return false;
      }
      uint32_t numElements = obj->as<WasmArrayObject>().numElements_;
      if (index >= numElements) {
        JS_ReportErrorNumberUTF8(cx, GetErrorMessage, nullptr,
                                 JSMSG_WASM_OUT_OF_BOUNDS);
        return false;
      }
      uint64_t scaledIndex =
          uint64_t(index) * uint64_t(arrayType.elementType().size());
      if (scaledIndex >= uint64_t(UINT32_MAX)) {
        // It's unrepresentable as an WasmGcObject::PropOffset. Give up.
        JS_ReportErrorNumberUTF8(cx, GetErrorMessage, nullptr,
                                 JSMSG_WASM_OUT_OF_BOUNDS);
        return false;
      }
      offset->set(uint32_t(scaledIndex));
      *type = arrayType.elementType();
      return true;
    }
    default:
      MOZ_ASSERT_UNREACHABLE();
      return false;
  }
}

bool WasmGcObject::loadValue(JSContext* cx, Handle<WasmGcObject*> obj, jsid id,
                             MutableHandleValue vp) {
  WasmGcObject::PropOffset offset;
  StorageType type;
  if (!lookUpProperty(cx, obj, id, &offset, &type)) {
    return false;
  }

  if (!type.isExposable()) {
    JS_ReportErrorNumberUTF8(cx, GetErrorMessage, nullptr,
                             JSMSG_WASM_BAD_VAL_TYPE);
    return false;
  }

  if (obj->is<WasmStructObject>()) {
    // `offset` is the field index.
    WasmStructObject& structObj = obj->as<WasmStructObject>();
    MOZ_RELEASE_ASSERT(structObj.kind() == TypeDefKind::Struct);
    // The above call to `lookUpProperty` will reject a request for a struct
    // field whose index is out of range.  Hence the following will be safe
    // providing the FieldAccessPaths are correct.
    return ToJSValue(cx, structObj.fieldIndexToAddress(offset.get()), type, vp);
  }

  MOZ_ASSERT(obj->is<WasmArrayObject>());
  const WasmArrayObject& arrayObj = obj->as<WasmArrayObject>();
  return ToJSValue(cx, arrayObj.data_ + offset.get(), type, vp);
}

bool WasmGcObject::isRuntimeSubtypeOf(
    const wasm::TypeDef* parentTypeDef) const {
  return TypeDef::isSubTypeOf(&typeDef(), parentTypeDef);
}

bool WasmGcObject::obj_newEnumerate(JSContext* cx, HandleObject obj,
                                    MutableHandleIdVector properties,
                                    bool enumerableOnly) {
  return true;
}

static void WriteValTo(WasmGcObject* owner, const Val& val, StorageType ty,
                       void* dest) {
  switch (ty.kind()) {
    case StorageType::I8:
      *((uint8_t*)dest) = val.i32();
      break;
    case StorageType::I16:
      *((uint16_t*)dest) = val.i32();
      break;
    case StorageType::I32:
      *((uint32_t*)dest) = val.i32();
      break;
    case StorageType::I64:
      *((uint64_t*)dest) = val.i64();
      break;
    case StorageType::F32:
      *((float*)dest) = val.f32();
      break;
    case StorageType::F64:
      *((double*)dest) = val.f64();
      break;
    case StorageType::V128:
      *((V128*)dest) = val.v128();
      break;
    case StorageType::Ref:
      BarrieredSet(owner, dest, val.ref());
      break;
  }
}

//=========================================================================
// WasmArrayObject

/* static */
size_t js::WasmArrayObject::sizeOfExcludingThis() const {
  if (isDataInline()) {
    return 0;
  }
  OOLDataHeader* oolHeader = oolDataHeaderFromDataPointer(data_);
  if (!gc::IsGCPointerBufferAlloc(oolHeader)) {
    return 0;
  }

  return gc::GetAllocSize(zone(), oolHeader);
}

/* static */
void WasmArrayObject::obj_trace(JSTracer* trc, JSObject* object) {
  WasmArrayObject& arrayObj = object->as<WasmArrayObject>();
  if (!arrayObj.isDataInline()) {
    OOLDataHeader* oolHeader = oolDataHeaderFromDataPointer(arrayObj.data_);
    OOLDataHeader* prior = oolHeader;
    TraceBufferEdge(trc, &oolHeader, "WasmArrayObject storage");
    if (oolHeader != prior) {
      arrayObj.data_ = oolDataHeaderToDataPointer(oolHeader);
    }
  }

  const auto& typeDef = arrayObj.typeDef();
  const auto& arrayType = typeDef.arrayType();
  if (!arrayType.elementType().isRefRepr()) {
    return;
  }

  uint32_t numElements = arrayObj.numElements_;
  uint32_t elemSize = arrayType.elementType().size();
  for (uint32_t i = 0; i < numElements; i++) {
    AnyRef* elementPtr =
        reinterpret_cast<AnyRef*>(arrayObj.data_ + i * elemSize);
    if (!elementPtr->isNull()) {
      TraceManuallyBarrieredEdge(trc, elementPtr, "wasm-array-element");
    }
  }
}

/* static */
size_t WasmArrayObject::obj_moved(JSObject* objNew, JSObject* objOld) {
  // This gets called for array objects, both with and without OOL areas.
  // Dealing with the no-OOL case is simple.  Thereafter, the logic for the OOL
  // case is essentially the same as for WasmStructObject::obj_moved, since
  // that routine is only used for WasmStructObjects that have OOL storage.
  MOZ_ASSERT(objNew != objOld);

  WasmArrayObject& arrayNew = objNew->as<WasmArrayObject>();
  WasmArrayObject& arrayOld = objOld->as<WasmArrayObject>();

  const TypeDef* typeDefNew = &arrayNew.typeDef();
  mozilla::DebugOnly<const TypeDef*> typeDefOld = &arrayOld.typeDef();
  MOZ_ASSERT(typeDefNew->isArrayType());
  MOZ_ASSERT(typeDefOld == typeDefNew);

  // At this point, the object has been copied, but the OOL storage area, if
  // any, has not been copied, nor has the data_ pointer been updated.  Hence:
  MOZ_ASSERT(arrayNew.data_ == arrayOld.data_);

  if (arrayOld.isDataInline()) {
    // The old array had inline storage, which has been copied.  Fix up the
    // data pointer in the new array to point to it, and we're done.
    arrayNew.data_ = WasmArrayObject::addressOfInlineArrayData(&arrayNew);
    MOZ_ASSERT(arrayNew.isDataInline());
    return 0;
  }

  // The array has OOL storage.  This means the logic that follows is similar
  // to that for WasmStructObject::obj_moved, since that routine is only used
  // for WasmStructObjects that have OOL storage.

  bool newIsInNursery = IsInsideNursery(objNew);
  bool oldIsInNursery = IsInsideNursery(objOld);

  // Tenured -> Tenured
  if (!oldIsInNursery && !newIsInNursery) {
    // The object already was in the tenured heap and has merely been moved
    // somewhere else in the the tenured heap.  This isn't interesting to us.
    return 0;
  }

  // Tenured -> Nursery: this transition isn't possible.
  MOZ_RELEASE_ASSERT(oldIsInNursery);

  // Nursery -> Nursery and Nursery -> Tenured
  // The object is being moved, either within the nursery or from the nursery
  // to the tenured heap.  Either way, we have to ask the nursery if it wants
  // to move the OOL block too, and if so set up a forwarding record for it.

  // arrayNew.numElements_ was validated not to overflow when constructing
  // the array.
  size_t oolBlockSize = calcArrayDataBytesUnchecked(
      typeDefNew->arrayType().elementType().size(), arrayNew.numElements_);
  // Ensured by WasmArrayObject::createArrayOOL.
  MOZ_RELEASE_ASSERT(oolBlockSize <= size_t(MaxArrayPayloadBytes));
  oolBlockSize += sizeof(WasmArrayObject::OOLDataHeader);

  // Ask the nursery if it wants to relocate the OOL block, and if so capture
  // its new location in `oolHeaderNew`.  Note, at this point `arrayNew.data_`
  // has not been updated; hence the computation for `oolHeaderOld` is correct.
  OOLDataHeader* oolHeaderOld = oolDataHeaderFromDataPointer(arrayNew.data_);
  OOLDataHeader* oolHeaderNew = oolHeaderOld;
  Nursery& nursery = objNew->runtimeFromMainThread()->gc.nursery();
  nursery.maybeMoveBufferOnPromotion(&oolHeaderNew, objNew, oolBlockSize);

  if (oolHeaderNew != oolHeaderOld) {
    // The OOL block has been moved.  Fix up the data pointer in the new
    // object.
    arrayNew.data_ = oolDataHeaderToDataPointer(oolHeaderNew);
    // Set up forwarding for the OOL block.  Use direct forwarding.  Write the
    // address of the new OOL block to OOLDataHeader::word in the old OOL
    // block.  This will be later used by Instance::updateFrameForMovingGC. See
    // SMDOC on definition of WasmArrayObject.
    //
    // Note, > rather than >=, because the OOL block must be big enough to hold
    // the data header plus at least one byte of array data.
    MOZ_RELEASE_ASSERT(oolBlockSize > sizeof(OOLDataHeader));
    if (nursery.isInside(oolHeaderOld)) {
      // Store the forwarding word, with bit 0 set.
      MOZ_ASSERT((uintptr_t(oolHeaderNew) & 1) == 0);
      oolHeaderOld->word = uintptr_t(oolHeaderNew) | 1;
      oolHeaderNew->word = WasmArrayObject::OOLDataHeader_Magic;
    }
  }

  return 0;
}

void WasmArrayObject::storeVal(const Val& val, uint32_t itemIndex) {
  const ArrayType& arrayType = typeDef().arrayType();
  size_t elementSize = arrayType.elementType().size();
  MOZ_ASSERT(itemIndex < numElements_);
  uint8_t* data = data_ + elementSize * itemIndex;
  WriteValTo(this, val, arrayType.elementType(), data);
}

void WasmArrayObject::fillVal(const Val& val, uint32_t itemIndex,
                              uint32_t len) {
  const ArrayType& arrayType = typeDef().arrayType();
  size_t elementSize = arrayType.elementType().size();
  uint8_t* data = data_ + elementSize * itemIndex;
  MOZ_ASSERT(itemIndex <= numElements_ && len <= numElements_ - itemIndex);
  for (uint32_t i = 0; i < len; i++) {
    WriteValTo(this, val, arrayType.elementType(), data);
    data += elementSize;
  }
}

static const JSClassOps WasmArrayObjectClassOps = {
    .newEnumerate = WasmGcObject::obj_newEnumerate,
    .trace = WasmArrayObject::obj_trace,
};
static const ClassExtension WasmArrayObjectClassExt = {
    WasmArrayObject::obj_moved, /* objectMovedOp */
};
const JSClass WasmArrayObject::class_ = {
    "WasmArrayObject",
    JSClass::NON_NATIVE | JSCLASS_DELAY_METADATA_BUILDER,
    &WasmArrayObjectClassOps,
    JS_NULL_CLASS_SPEC,
    &WasmArrayObjectClassExt,
    &WasmGcObject::objectOps_,
};

//=========================================================================
// WasmStructObject

/* static */
size_t js::WasmStructObject::sizeOfExcludingThis() const {
  if (!hasOOLPointer()) {
    return 0;
  }
  const uint8_t* oolPointer = getOOLPointer();
  if (!gc::IsGCPointerBufferAlloc((void*)oolPointer)) {
    return 0;
  }

  return gc::GetAllocSize(zone(), oolPointer);
}

/* static */
bool WasmStructObject::getField(JSContext* cx, uint32_t index,
                                MutableHandle<Value> val) {
  const StructType& resultType = typeDef().structType();
  MOZ_ASSERT(index < resultType.fields_.length());
  const FieldType& field = resultType.fields_[index];
  StorageType ty = field.type.storageType();
  return ToJSValue(cx, fieldIndexToAddress(index), ty, val);
}

/* static */
uint8_t* WasmStructObject::fieldIndexToAddress(uint32_t fieldIndex) {
  const wasm::SuperTypeVector* stv = superTypeVector_;
  const wasm::TypeDef* typeDef = stv->typeDef();
  MOZ_ASSERT(typeDef->superTypeVector() == stv);
  const wasm::StructType& structType = typeDef->structType();
  const wasm::FieldAccessPathVector& fieldAccessPaths =
      structType.fieldAccessPaths_;
  MOZ_RELEASE_ASSERT(fieldIndex < fieldAccessPaths.length());
  wasm::FieldAccessPath path = fieldAccessPaths[fieldIndex];
  uint32_t ilOffset = path.ilOffset();
  MOZ_RELEASE_ASSERT(ilOffset != wasm::StructType::InvalidOffset);
  if (MOZ_LIKELY(!path.hasOOL())) {
    return (uint8_t*)this + ilOffset;
  }
  uint8_t* oolBlock = *(uint8_t**)((uint8_t*)this + ilOffset);
  uint32_t oolOffset = path.oolOffset();
  MOZ_RELEASE_ASSERT(oolOffset != wasm::StructType::InvalidOffset);
  return oolBlock + oolOffset;
}

/* static */
void WasmStructObject::obj_trace(JSTracer* trc, JSObject* object) {
  WasmStructObject& structObj = object->as<WasmStructObject>();

  const auto& structType = structObj.typeDef().structType();
  for (uint32_t offset : structType.inlineTraceOffsets_) {
    AnyRef* fieldPtr = reinterpret_cast<AnyRef*>((uint8_t*)&structObj + offset);
    if (!fieldPtr->isNull()) {
      TraceManuallyBarrieredEdge(trc, fieldPtr, "wasm-struct-field");
    }
  }
  if (MOZ_UNLIKELY(structType.totalSizeOOL_ > 0)) {
    uint8_t** addressOfOOLPtr = structObj.addressOfOOLPointer();
    // *addressOfOOLPtr may be null if the struct was only partially initialized
    // due to OOM during createStructOOL.
    if (MOZ_LIKELY(*addressOfOOLPtr)) {
      TraceBufferEdge(trc, addressOfOOLPtr, "WasmStructObject outline data");
      uint8_t* oolBase = *addressOfOOLPtr;
      for (uint32_t offset : structType.outlineTraceOffsets_) {
        AnyRef* fieldPtr = reinterpret_cast<AnyRef*>(oolBase + offset);
        if (!fieldPtr->isNull()) {
          TraceManuallyBarrieredEdge(trc, fieldPtr, "wasm-struct-field");
        }
      }
    }
  }
}

/* static */
size_t WasmStructObject::obj_moved(JSObject* objNew, JSObject* objOld) {
  // This gets called only for struct objects that have an OOL area.  Compare
  // WasmStructObjectInlineClassExt vs WasmStructObjectOutlineClassExt below.
  MOZ_ASSERT(objNew != objOld);

  WasmStructObject& structNew = objNew->as<WasmStructObject>();
  WasmStructObject& structOld = objOld->as<WasmStructObject>();
  MOZ_ASSERT(structNew.hasOOLPointer() && structOld.hasOOLPointer());

  const TypeDef* typeDefNew = &structNew.typeDef();
  mozilla::DebugOnly<const TypeDef*> typeDefOld = &structOld.typeDef();
  MOZ_ASSERT(typeDefNew == typeDefOld);
  MOZ_ASSERT(typeDefNew->isStructType());
  MOZ_ASSERT(typeDefOld == typeDefNew);

  // At this point, the object has been copied, but the OOL storage area has
  // not been copied, nor has the OOL pointer been updated.  Hence:
  MOZ_ASSERT(structNew.getOOLPointer() == structOld.getOOLPointer());

  bool newIsInNursery = IsInsideNursery(objNew);
  bool oldIsInNursery = IsInsideNursery(objOld);

  // Tenured -> Tenured
  if (!oldIsInNursery && !newIsInNursery) {
    // The object already was in the tenured heap and has merely been moved
    // somewhere else in the the tenured heap.  This isn't interesting to us.
    return 0;
  }

  // Tenured -> Nursery: this transition isn't possible.
  MOZ_RELEASE_ASSERT(oldIsInNursery);

  // Nursery -> Nursery and Nursery -> Tenured
  // The object is being moved, either within the nursery or from the nursery
  // to the tenured heap.  Either way, we have to ask the nursery if it wants
  // to move the OOL block too, and if so set up a forwarding record for it.

  const StructType& structType = typeDefNew->structType();
  uint32_t outlineBytes = structType.totalSizeOOL_;
  // These must always agree.
  MOZ_ASSERT((outlineBytes > 0) == structNew.hasOOLPointer());
  // Because the WasmStructObjectInlineClassExt doesn't reference this
  // method; only WasmStructObjectOutlineClassExt does.
  MOZ_ASSERT(outlineBytes > 0);

  // Ask the nursery if it wants to relocate the OOL area, and if so capture
  // its new location in `addressOfOOLPointerNew`.
  Nursery& nursery = structNew.runtimeFromMainThread()->gc.nursery();
  uint8_t** addressOfOOLPointerNew = structNew.addressOfOOLPointer();
  nursery.maybeMoveBufferOnPromotion(addressOfOOLPointerNew, objNew,
                                     outlineBytes);

  // Set up forwarding for the OOL area.  In order to be able to use direct
  // forwarding, the OOL data area needs to be at least one word long, so that
  // this call to setForwardingPointerWhileTenuring can write the forwarding
  // address directly at the start of the old OOL area.  This is ensured by
  // logic in StructType::init.  See also comments in
  // WasmArrayObject::obj_moved.  Note that because the first word of the OOL
  // area is overwritten, we must not access the area after this point, and in
  // particular not in Instance::updateFrameForMovingGC.
  uint8_t* oolPointerOld = structOld.getOOLPointer();
  uint8_t* oolPointerNew = structNew.getOOLPointer();
  MOZ_RELEASE_ASSERT(outlineBytes >= sizeof(uintptr_t));
  if (oolPointerOld != oolPointerNew) {
    nursery.setForwardingPointerWhileTenuring(oolPointerOld, oolPointerNew,
                                              /*direct=*/true);
  }

  return 0;
}

void WasmStructObject::storeVal(const Val& val, uint32_t fieldIndex) {
  const StructType& structType = typeDef().structType();
  MOZ_ASSERT(fieldIndex < structType.fields_.length());

  StorageType fieldType = structType.fields_[fieldIndex].type;
  uint8_t* data = fieldIndexToAddress(fieldIndex);

  WriteValTo(this, val, fieldType, data);
}

static const JSClassOps WasmStructObjectOutlineClassOps = {
    .newEnumerate = WasmGcObject::obj_newEnumerate,
    .trace = WasmStructObject::obj_trace,
};
static const ClassExtension WasmStructObjectOutlineClassExt = {
    WasmStructObject::obj_moved, /* objectMovedOp */
};
const JSClass WasmStructObject::classOutline_ = {
    "WasmStructObject",
    JSClass::NON_NATIVE | JSCLASS_DELAY_METADATA_BUILDER,
    &WasmStructObjectOutlineClassOps,
    JS_NULL_CLASS_SPEC,
    &WasmStructObjectOutlineClassExt,
    &WasmGcObject::objectOps_,
};

// Structs that only have inline data get a different class without a
// finalizer. This class should otherwise be identical to the class for
// structs with outline data.
static const JSClassOps WasmStructObjectInlineClassOps = {
    .newEnumerate = WasmGcObject::obj_newEnumerate,
    .trace = WasmStructObject::obj_trace,
};
static const ClassExtension WasmStructObjectInlineClassExt = {
    nullptr, /* objectMovedOp */
};
const JSClass WasmStructObject::classInline_ = {
    "WasmStructObject",
    JSClass::NON_NATIVE | JSCLASS_DELAY_METADATA_BUILDER,
    &WasmStructObjectInlineClassOps,
    JS_NULL_CLASS_SPEC,
    &WasmStructObjectInlineClassExt,
    &WasmGcObject::objectOps_,
};
