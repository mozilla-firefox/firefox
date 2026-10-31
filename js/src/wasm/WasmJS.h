/*
 * Copyright 2016 Mozilla Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef wasm_js_h
#define wasm_js_h

#include "mozilla/HashTable.h"  // DefaultHasher
#include "mozilla/Maybe.h"      // mozilla::Maybe

#include <stdint.h>  // int32_t, int64_t, uint32_t
#include <utility>   // std::pair

#include "gc/Barrier.h"         // HeapPtr
#include "gc/ZoneAllocator.h"   // ZoneAllocPolicy
#include "js/AllocPolicy.h"     // SystemAllocPolicy
#include "js/Class.h"           // JSClassOps, ClassSpec
#include "js/CompileOptions.h"  // JS::ReadOnlyCompileOptions
#include "js/GCHashTable.h"     // GCHashMap, GCHashSet
#include "js/GCVector.h"        // GCVector
#include "js/PropertySpec.h"    // JSPropertySpec, JSFunctionSpec
#include "js/RootingAPI.h"      // StableCellHasher
#include "js/SweepingAPI.h"     // JS::WeakCache
#include "js/TypeDecls.h"  // HandleValue, HandleObject, MutableHandleObject, MutableHandleFunction
#include "js/Vector.h"  // JS::Vector
#include "js/WasmFeatures.h"
#include "vm/JSFunction.h"    // JSFunction
#include "vm/NativeObject.h"  // NativeObject
#include "wasm/WasmCodegenTypes.h"
#include "wasm/WasmConstants.h"
#include "wasm/WasmException.h"
#include "wasm/WasmExprType.h"
#include "wasm/WasmMemory.h"
#include "wasm/WasmModuleTypes.h"
#include "wasm/WasmTypeDecls.h"
#include "wasm/WasmValType.h"
#include "wasm/WasmValue.h"

class JSObject;
class JSTracer;
struct JSContext;

namespace JS {
class CallArgs;
class Value;
}  // namespace JS

namespace js {

class ArrayBufferObject;
class ArrayBufferObjectMaybeShared;
class JSStringBuilder;
class TypedArrayObject;
class WasmFunctionScope;
class WasmInstanceScope;
class WasmSharedArrayRawBuffer;

namespace wasm {

struct ImportValues;

// Compiles the given binary wasm module given the ArrayBufferObject
// and links the module's imports with the given import object.

[[nodiscard]] bool Eval(JSContext* cx, Handle<TypedArrayObject*> code,
                        HandleObject importObj,
                        MutableHandle<WasmInstanceObject*> instanceObj);

struct ImportValues;

// Extracts the various imports from the given import object into the given
// ImportValues structure while checking the imports against the given module.
// The resulting structure can be passed to WasmModule::instantiate.
[[nodiscard]] bool GetImports(JSContext* cx, const Module& module,
                              HandleObject importObj, ImportValues* imports);

// Each import is a (module specifier, export name) pair. Imports satisfied by a
// builtin module are omitted, as they are not resolved against other modules.
[[nodiscard]] bool GetModuleImports(
    JSContext* cx, HandleObject wasmModuleObject,
    MutableHandle<JS::StackGCVector<std::pair<JSString*, JSString*>>> imports);

// For testing cross-process (de)serialization, this pair of functions are
// responsible for, in the child process, compiling the given wasm bytecode
// to a wasm::Module that is serialized into the given byte array, and, in
// the parent process, deserializing the given byte array into a
// WebAssembly.Module object.

[[nodiscard]] bool CompileAndSerialize(JSContext* cx,
                                       const BytecodeSource& source,
                                       Bytes* serialized);

[[nodiscard]] bool DeserializeModule(JSContext* cx, const Bytes& serialized,
                                     MutableHandleObject module);

bool IsSharedWasmMemoryObject(JSObject* obj);

}  // namespace wasm

// The class of WebAssembly.Module. Each WasmModuleObject owns a
// wasm::Module. These objects are used as content-facing JS objects.

class WasmModuleObject : public NativeObject {
  static const unsigned MODULE_SLOT = 0;
  static const JSClassOps classOps_;
  static const ClassSpec classSpec_;
  static void finalize(JS::GCContext* gcx, JSObject* obj);
  static bool imports(JSContext* cx, unsigned argc, Value* vp);
  static bool exports(JSContext* cx, unsigned argc, Value* vp);
  static bool customSections(JSContext* cx, unsigned argc, Value* vp);

 public:
  static const unsigned RESERVED_SLOTS = 1;
  static const JSClass class_;
  static const JSClass& protoClass_;
  static const JSPropertySpec properties[];
  static const JSFunctionSpec methods[];
  static const JSFunctionSpec static_methods[];
  static bool construct(JSContext*, unsigned, Value*);

  static WasmModuleObject* create(JSContext* cx, const wasm::Module& module,
                                  HandleObject proto);
  const wasm::Module& module() const;
};

#ifdef ENABLE_WASM_COMPONENTS
// The class of WebAssembly.Component.
// TODO(wasm-cm): Leave a more descriptive comment. See WasmModuleObject for
// comparison.

class WasmComponentObject : public NativeObject {
  static const unsigned COMPONENT_SLOT = 0;
  static const JSClassOps classOps_;
  static const ClassSpec classSpec_;
  static void finalize(JS::GCContext* gcx, JSObject* obj);

 public:
  static const unsigned RESERVED_SLOTS = 1;
  static const JSClass class_;
  static const JSClass& protoClass_;
  static const JSPropertySpec properties[];
  static const JSFunctionSpec methods[];
  static const JSFunctionSpec static_methods[];
  static bool construct(JSContext*, unsigned, Value*);

  static WasmComponentObject* create(JSContext* cx,
                                     const wasm::Component& component,
                                     HandleObject proto);
  const wasm::Component& component() const;
};
#endif

// The class of WebAssembly.Global.  This wraps a storage location, and there is
// a per-agent one-to-one relationship between the WasmGlobalObject and the
// storage location (the Cell) it wraps: if a module re-exports an imported
// global, the imported and exported WasmGlobalObjects are the same, and if a
// module exports a global twice, the two exported WasmGlobalObjects are the
// same.

class WasmGlobalObject : public NativeObject {
  JS_DEFINE_TYPED_SLOT(0, MUTABLE_SLOT, Boolean);
  JS_DEFINE_TYPED_SLOT(1, VAL_SLOT, Private, Undefined);

  static const JSClassOps classOps_;
  static const ClassSpec classSpec_;
  static void finalize(JS::GCContext* gcx, JSObject* obj);
  static void trace(JSTracer* trc, JSObject* obj);

  static bool typeImpl(JSContext* cx, const CallArgs& args);
  static bool type(JSContext* cx, unsigned argc, Value* vp);

  static bool valueGetterImpl(JSContext* cx, const CallArgs& args);
  static bool valueGetter(JSContext* cx, unsigned argc, Value* vp);
  static bool valueSetterImpl(JSContext* cx, const CallArgs& args);
  static bool valueSetter(JSContext* cx, unsigned argc, Value* vp);

  wasm::HeapPtrVal& mutableVal();

 public:
  static const unsigned RESERVED_SLOTS = 2;
  static const JSClass class_;
  static const JSClass& protoClass_;
  static const JSPropertySpec properties[];
  static const JSFunctionSpec methods[];
  static const JSFunctionSpec static_methods[];
  static bool construct(JSContext*, unsigned, Value*);

  static WasmGlobalObject* create(JSContext* cx, wasm::HandleVal value,
                                  bool isMutable, HandleObject proto);
  bool isNewborn() { return getReservedSlotTyped(VAL_SLOT).isUndefined(); }

  bool isMutable() const;
  wasm::ValType type() const;
  const wasm::HeapPtrVal& val() const;
  void setVal(wasm::HandleVal value);
  void* addressOfCell() const;
};

// The class of WebAssembly.Instance. Each WasmInstanceObject owns a
// wasm::Instance. These objects are used as content-facing JS objects.

class WasmInstanceObject : public NativeObject {
  JS_DEFINE_TYPED_SLOT(0, INSTANCE_SLOT, Private, Undefined);
  JS_DEFINE_TYPED_SLOT(1, EXPORTS_OBJ_SLOT, Object, Undefined);
  JS_DEFINE_TYPED_SLOT(2, SCOPES_SLOT, Private);
  JS_DEFINE_TYPED_SLOT(3, INSTANCE_SCOPE_SLOT, PrivateGCThing, Undefined);
  JS_DEFINE_TYPED_SLOT(4, GLOBALS_SLOT, Private);

  static const JSClassOps classOps_;
  static const ClassSpec classSpec_;
  static bool exportsGetterImpl(JSContext* cx, const CallArgs& args);
  static bool exportsGetter(JSContext* cx, unsigned argc, Value* vp);
  bool isNewborn() const;
  static void finalize(JS::GCContext* gcx, JSObject* obj);
  static void trace(JSTracer* trc, JSObject* obj);

  // See the definition inside WasmJS.cpp.
  class UnspecifiedScopeMap;
  UnspecifiedScopeMap& scopes() const;

 public:
  static const unsigned RESERVED_SLOTS = 5;
  static const JSClass class_;
  static const JSClass& protoClass_;
  static const JSPropertySpec properties[];
  static const JSFunctionSpec methods[];
  static const JSFunctionSpec static_methods[];
  static bool construct(JSContext*, unsigned, Value*);

  static WasmInstanceObject* create(
      JSContext* cx, const RefPtr<const wasm::Code>& code,
      const wasm::DataSegmentVector& dataSegments,
      const wasm::ModuleElemSegmentVector& elemSegments,
      uint32_t instanceDataLength, Handle<WasmMemoryObjectVector> memories,
      Vector<RefPtr<wasm::Table>, 0, SystemAllocPolicy>&& tables,
      const JSObjectVector& funcImports, const wasm::GlobalDescVector& globals,
      const wasm::ValVector& globalImportValues,
      const WasmGlobalObjectVector& globalObjs,
      const WasmTagObjectVector& tagObjs, HandleObject proto,
      UniquePtr<wasm::DebugState> maybeDebug);
  void initExportsObj(JSObject& exportsObj);

  wasm::Instance& instance() const;
  JSObject& exportsObj() const;
  WasmFunctionScope* getExistingFunctionScope(uint32_t funcIndex) const;

  [[nodiscard]] static bool getExportedFunction(
      JSContext* cx, Handle<WasmInstanceObject*> instanceObj,
      uint32_t funcIndex, MutableHandleFunction fun);

  static WasmInstanceScope* getScope(JSContext* cx,
                                     Handle<WasmInstanceObject*> instanceObj);
  static WasmFunctionScope* getFunctionScope(
      JSContext* cx, Handle<WasmInstanceObject*> instanceObj,
      uint32_t funcIndex);

  using GlobalObjectVector =
      GCVector<HeapPtr<WasmGlobalObject*>, 0, CellAllocPolicy>;
  GlobalObjectVector& indirectGlobals() const;
};

// The class of WebAssembly.Memory. A WasmMemoryObject references an ArrayBuffer
// or SharedArrayBuffer object which owns the actual memory.

class WasmMemoryObject : public NativeObject {
  JS_DEFINE_TYPED_SLOT(0, BUFFER_SLOT, Object);
  JS_DEFINE_TYPED_SLOT(1, OBSERVERS_SLOT, Private, Undefined);
  JS_DEFINE_TYPED_SLOT(2, ISHUGE_SLOT, Boolean);
  static const JSClassOps classOps_;
  static const ClassSpec classSpec_;
  static void finalize(JS::GCContext* gcx, JSObject* obj);
  static bool bufferGetterImpl(JSContext* cx, const CallArgs& args);
  static bool bufferGetter(JSContext* cx, unsigned argc, Value* vp);
  static bool typeImpl(JSContext* cx, const CallArgs& args);
  static bool type(JSContext* cx, unsigned argc, Value* vp);
  static bool growImpl(JSContext* cx, const CallArgs& args);
  static bool grow(JSContext* cx, unsigned argc, Value* vp);
  static bool discardImpl(JSContext* cx, const CallArgs& args);
  static bool discard(JSContext* cx, unsigned argc, Value* vp);
  static uint64_t growShared(Handle<WasmMemoryObject*> memory, uint64_t delta);
  static bool toFixedLengthBufferImpl(JSContext* cx, const CallArgs& args);
  static bool toFixedLengthBuffer(JSContext* cx, unsigned argc, Value* vp);
  static bool toResizableBufferImpl(JSContext* cx, const CallArgs& args);
  static bool toResizableBuffer(JSContext* cx, unsigned argc, Value* vp);

  using InstanceSet = JS::WeakCache<GCHashSet<
      WeakHeapPtr<WasmInstanceObject*>,
      StableCellHasher<WeakHeapPtr<WasmInstanceObject*>>, CellAllocPolicy>>;
  bool hasObservers() const;
  InstanceSet& observers() const;
  InstanceSet* getOrCreateObservers(JSContext* cx);

  // The spec requires that the buffer property will create a new fixed length
  // SAB if the underlying raw buffer's length has changed. The method creates
  // new objects and updates the BUFFER_SLOT slot.
  static ArrayBufferObjectMaybeShared* refreshBuffer(
      JSContext* cx, Handle<WasmMemoryObject*> memoryObj,
      Handle<ArrayBufferObjectMaybeShared*> buffer);

 public:
  static const unsigned RESERVED_SLOTS = 3;
  static const JSClass class_;
  static const JSClass& protoClass_;
  static const JSPropertySpec properties[];
  static const JSFunctionSpec methods[];
  static const JSFunctionSpec memoryControlMethods[];
  static const JSFunctionSpec static_methods[];
  static bool construct(JSContext*, unsigned, Value*);

  static WasmMemoryObject* create(JSContext* cx,
                                  Handle<ArrayBufferObjectMaybeShared*> buffer,
                                  bool isHuge, HandleObject proto);

  // `buffer()` returns the current buffer object always.  If the buffer
  // represents shared memory then `buffer().byteLength()` never changes, and
  // in particular it may be a smaller value than that returned from
  // `volatileMemoryLength()` below.
  //
  // Generally, you do not want to call `buffer().byteLength()`, but to call
  // `volatileMemoryLength()`, instead.
  ArrayBufferObjectMaybeShared& buffer() const;

  // The current length of the memory in bytes. In the case of shared memory,
  // the length can change at any time.  Also note that this will acquire a lock
  // for shared memory, so do not call this from a signal handler.
  size_t volatileMemoryLength() const;

  // The current length of the memory in pages. See the comment for
  // `volatileMemoryLength` for details on why this is 'volatile'.
  wasm::Pages volatilePages() const;

  // The maximum length of the memory in pages. This is not 'volatile' in
  // contrast to the current length, as it cannot change for shared memories.
  wasm::Pages clampedMaxPages() const;
  mozilla::Maybe<wasm::Pages> sourceMaxPages() const;

  wasm::AddressType addressType() const;
  bool isShared() const;
  bool isHuge() const;
  bool movingGrowable() const;
  size_t boundsCheckLimit() const;
  wasm::PageSize pageSize() const;

  // If isShared() is true then obtain the underlying buffer object.
  WasmSharedArrayRawBuffer* sharedArrayRawBuffer() const;

  bool addMovingGrowObserver(JSContext* cx, WasmInstanceObject* instance);
  static uint64_t grow(Handle<WasmMemoryObject*> memory, uint64_t delta,
                       JSContext* cx);
  static void discard(Handle<WasmMemoryObject*> memory, uint64_t byteOffset,
                      uint64_t len, JSContext* cx);
};

// The class of WebAssembly.Table. A WasmTableObject holds a refcount on a
// wasm::Table, allowing a Table to be shared between multiple Instances
// (eventually between multiple threads).

class WasmTableObject : public NativeObject {
  JS_DEFINE_TYPED_SLOT(0, TABLE_SLOT, Private, Undefined);
  static const JSClassOps classOps_;
  static const ClassSpec classSpec_;
  bool isNewborn() const;
  static void finalize(JS::GCContext* gcx, JSObject* obj);
  static void trace(JSTracer* trc, JSObject* obj);
  static bool lengthGetterImpl(JSContext* cx, const CallArgs& args);
  static bool lengthGetter(JSContext* cx, unsigned argc, Value* vp);
  static bool typeImpl(JSContext* cx, const CallArgs& args);
  static bool type(JSContext* cx, unsigned argc, Value* vp);
  static bool getImpl(JSContext* cx, const CallArgs& args);
  static bool get(JSContext* cx, unsigned argc, Value* vp);
  static bool setImpl(JSContext* cx, const CallArgs& args);
  static bool set(JSContext* cx, unsigned argc, Value* vp);
  static bool growImpl(JSContext* cx, const CallArgs& args);
  static bool grow(JSContext* cx, unsigned argc, Value* vp);

 public:
  static const unsigned RESERVED_SLOTS = 1;
  static const JSClass class_;
  static const JSClass& protoClass_;
  static const JSPropertySpec properties[];
  static const JSFunctionSpec methods[];
  static const JSFunctionSpec static_methods[];
  static bool construct(JSContext*, unsigned, Value*);

  // Note that, after creation, a WasmTableObject's table() is not initialized
  // and must be initialized before use.

  static WasmTableObject* create(JSContext* cx, const wasm::TableType& type,
                                 HandleObject proto);
  wasm::Table& table() const;

  // Perform the standard `ToWebAssemblyValue` coercion on `value` and fill the
  // range [index, index + length) in the table. Callers are required to ensure
  // the range is within bounds. Returns false if the coercion failed.
  bool fillRange(JSContext* cx, uint32_t index, uint32_t length,
                 HandleValue value) const;
};

// The class of WebAssembly.Tag. This class is used to track exception tag
// types for exports and imports.

class WasmTagObject : public NativeObject {
  JS_DEFINE_TYPED_SLOT(0, TYPE_SLOT, Private);

  static const JSClassOps classOps_;
  static const ClassSpec classSpec_;
  static void finalize(JS::GCContext* gcx, JSObject* obj);
  static bool typeImpl(JSContext* cx, const CallArgs& args);
  static bool type(JSContext* cx, unsigned argc, Value* vp);

 public:
  static const unsigned RESERVED_SLOTS = 1;
  static const JSClass class_;
  static const JSClass& protoClass_;
  static const JSPropertySpec properties[];
  static const JSFunctionSpec methods[];
  static const JSFunctionSpec static_methods[];
  static bool construct(JSContext*, unsigned, Value*);

  static WasmTagObject* create(JSContext* cx,
                               const wasm::SharedTagType& tagType,
                               HandleObject proto);

  const wasm::TagType* tagType() const;
  const wasm::ValTypeVector& valueTypes() const;
};

// The class of WebAssembly.Exception. This class is used for
// representing exceptions thrown from Wasm in JS. (it is also used as
// the internal representation for exceptions in Wasm)

class WasmExceptionObject : public NativeObject {
  JS_DEFINE_TYPED_SLOT(0, TAG_SLOT, Object);
  JS_DEFINE_TYPED_SLOT(1, TYPE_SLOT, Private);
  JS_DEFINE_TYPED_SLOT(2, DATA_SLOT, Private, Undefined);
  JS_DEFINE_TYPED_SLOT(3, STACK_SLOT, Object, Null);

  static const JSClassOps classOps_;
  static const ClassSpec classSpec_;
  static void trace(JSTracer* trc, JSObject* obj);
  static void finalize(JS::GCContext* gcx, JSObject* obj);
  // Named isMethod instead of is to avoid name conflict.
  static bool isMethod(JSContext* cx, unsigned argc, Value* vp);
  static bool isImpl(JSContext* cx, const CallArgs& args);
  static bool getArg(JSContext* cx, unsigned argc, Value* vp);
  static bool getArgImpl(JSContext* cx, const CallArgs& args);
  static bool getStack(JSContext* cx, unsigned argc, Value* vp);
  static bool getStack_impl(JSContext* cx, const CallArgs& args);

  uint8_t* typedMem() const;
  [[nodiscard]] bool loadArg(JSContext* cx, size_t offset, wasm::ValType type,
                             MutableHandleValue vp) const;
  [[nodiscard]] bool initArg(JSContext* cx, size_t offset, wasm::ValType type,
                             HandleValue value);

  void initRefArg(size_t offset, wasm::AnyRef ref);
  wasm::AnyRef loadRefArg(size_t offset) const;

 public:
  static const unsigned RESERVED_SLOTS = 4;
  static const JSClass class_;
  static const JSClass& protoClass_;
  static const JSPropertySpec properties[];
  static const JSFunctionSpec methods[];
  static const JSFunctionSpec static_methods[];
  static bool construct(JSContext*, unsigned, Value*);

  static WasmExceptionObject* create(JSContext* cx, Handle<WasmTagObject*> tag,
                                     HandleObject stack, HandleObject proto);
  static WasmExceptionObject* wrapJSValue(JSContext* cx, HandleValue value);
  bool isNewborn() const;

  JSObject* stack() const;
  const wasm::TagType* tagType() const;
  WasmTagObject& tag() const;

  bool isWrappedJSValue() const;
  Value wrappedJSValue() const;

  // Returns the exception value that JS would see if this was thrown. This
  // will unwrap if `isWrappedJSValue`.
  Value toJSValue() {
    if (isWrappedJSValue()) {
      return wrappedJSValue();
    }
    return JS::ObjectValue(*this);
  }

  static size_t offsetOfData() {
    return NativeObject::getFixedSlotOffsetTyped(DATA_SLOT);
  }
};

// The class of the WebAssembly global namespace object.

class WasmNamespaceObject : public NativeObject {
 public:
  static const JSClass class_;
  JS_DEFINE_TYPED_SLOT(0, JS_VALUE_TAG_SLOT, Object, Null, Undefined);
#ifdef ENABLE_WASM_JSPI
  JS_DEFINE_TYPED_SLOT(1, JS_PROMISE_TAG_SLOT, Object, Null, Undefined);
#endif
  static constexpr uint32_t RESERVED_SLOTS = 2;

  WasmTagObject* wrappedJSValueTag() const {
    return &getReservedSlotTyped(JS_VALUE_TAG_SLOT)
                .toObjectOrNull()
                ->as<WasmTagObject>();
  }
  void setWrappedJSValueTag(WasmTagObject* tag) {
    return setReservedSlotTyped(JS_VALUE_TAG_SLOT, ObjectValue(*tag));
  }
#ifdef ENABLE_WASM_JSPI
  WasmTagObject* jsPromiseTag() const {
    return &getReservedSlotTyped(JS_PROMISE_TAG_SLOT)
                .toObjectOrNull()
                ->as<WasmTagObject>();
  }
  void setJSPromiseTag(WasmTagObject* tag) {
    return setReservedSlotTyped(JS_PROMISE_TAG_SLOT, ObjectValue(*tag));
  }
#endif

  static WasmNamespaceObject* getOrCreate(JSContext* cx);

 private:
  static const ClassSpec classSpec_;
};

extern const JSClass WasmFunctionClass;

bool IsWasmSuspendingObject(JSObject* obj);

#ifdef ENABLE_WASM_JSPI

class WasmSuspendingObject : public NativeObject {
 public:
  static const ClassSpec classSpec_;
  static const JSClass class_;
  static const JSClass& protoClass_;
  JS_DEFINE_TYPED_SLOT(0, WRAPPED_FN_SLOT, Object, Null);
  static const unsigned RESERVED_SLOTS = 1;
  static bool construct(JSContext*, unsigned, Value*);

  JSObject* wrappedFunction() const {
    return getReservedSlotTyped(WRAPPED_FN_SLOT).toObjectOrNull();
  }
  void initWrappedFunction(HandleObject fn) {
    return initReservedSlotTyped(WRAPPED_FN_SLOT, ObjectValue(*fn));
  }
};

JSObject* MaybeUnwrapSuspendingObject(JSObject* wrapper);
#endif

#ifdef ENABLE_WASM_COMPONENTS
// The class of WebAssembly.ComponentInstance. Each WasmComponentInstanceObject
// owns a wasm::ComponentInstance. These objects are used as content-facing JS
// objects.
class WasmComponentInstanceObject : public NativeObject {
  JS_DEFINE_TYPED_SLOT(0, INSTANCE_SLOT, Private, Undefined);

  static const JSClassOps classOps_;
  static const ClassSpec classSpec_;
  bool isNewborn() const;
  static void finalize(JS::GCContext* gcx, JSObject* obj);
  static void trace(JSTracer* trc, JSObject* obj);

 public:
  static const unsigned RESERVED_SLOTS = 1;
  static const JSClass class_;
  static const JSClass& protoClass_;
  static const JSPropertySpec properties[];
  static const JSFunctionSpec methods[];
  static const JSFunctionSpec static_methods[];
  static bool construct(JSContext*, unsigned, Value*);

  static WasmComponentInstanceObject* create(
      JSContext* cx, HandleObject proto,
      const RefPtr<const wasm::Component> component);

  wasm::ComponentInstance& instance() const;
};
#endif  // ENABLE_WASM_COMPONENTS

}  // namespace js

#endif  // wasm_js_h
