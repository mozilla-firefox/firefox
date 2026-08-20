/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef builtin_ModuleObject_h
#define builtin_ModuleObject_h

#include "mozilla/HashTable.h"  // mozilla::{HashMap, DefaultHasher}
#include "mozilla/Maybe.h"      // mozilla::Maybe
#include "mozilla/Span.h"

#include <cstdint>   // UINT32_MAX
#include <stddef.h>  // size_t
#include <stdint.h>  // int32_t, uint32_t

#include "gc/Barrier.h"        // HeapPtr
#include "gc/ZoneAllocator.h"  // CellAllocPolicy
#include "js/Class.h"          // JSClass, ObjectOpResult
#include "js/ColumnNumber.h"   // JS::ColumnNumberOneOrigin
#include "js/GCVector.h"
#include "js/Id.h"  // jsid
#include "js/Modules.h"
#include "js/Proxy.h"       // BaseProxyHandler
#include "js/RootingAPI.h"  // Rooted, Handle, MutableHandle
#include "js/TypeDecls.h"  // HandleValue, HandleId, HandleObject, HandleScript, MutableHandleValue, MutableHandleIdVector, MutableHandleObject
#include "js/UniquePtr.h"  // UniquePtr
#include "vm/JSObject.h"   // JSObject
#include "vm/NativeObject.h"   // NativeObject
#include "vm/ProxyObject.h"    // ProxyObject
#include "vm/SharedStencil.h"  // FunctionDeclarationVector

class JSAtom;
class JSScript;
class JSTracer;

namespace JS {
class PropertyDescriptor;
class Value;
}  // namespace JS

namespace js {

class ArrayObject;
class CyclicModuleFields;
class SyntheticModuleFields;
class ListObject;
class ModuleEnvironmentObject;
class ModuleObject;
class PromiseObject;
class ScriptSourceObject;

class ImportAttribute {
  const HeapPtr<JSAtom*> key_;
  const HeapPtr<JSString*> value_;

 public:
  ImportAttribute(Handle<JSAtom*> key, Handle<JSString*> value);

  JSAtom* key() const { return key_; }
  JSString* value() const { return value_; }

  void trace(JSTracer* trc);
};

using ImportAttributeVector = GCVector<ImportAttribute, 0, SystemAllocPolicy>;

// https://tc39.es/proposal-source-phase-imports/#sec-modulerequest-record
enum class ImportPhase : uint8_t { Source, Evaluation, Limit };

// Possible value types of [[ImportName]] field in ImportEntry Records and
// ExportEntry Records.
// When the value type is not 'String', the [[ImportName]] field will be null;
// the value is recorded by importNameValueType_ instead.
//
// https://tc39.es/ecma262/#importentry-record
// https://tc39.es/ecma262/#exportentry-record
enum class ImportNameValueType : uint8_t {
  String,
  Namespace,
  Source,
  AllButDefault
};

class ModuleRequestObject : public NativeObject {
  enum {
    SpecifierSlotIndex = 0,
    FirstUnsupportedAttributeKeySlotIndex,
    ModuleTypeSlotIndex,
    PhaseSlotIndex,
    SlotCount
  };

  JS_DEFINE_TYPED_SLOT(SpecifierSlotIndex, SPECIFIER_SLOT, String, Null);
  JS_DEFINE_TYPED_SLOT(FirstUnsupportedAttributeKeySlotIndex,
                       FIRST_UNSUPPORTED_ATTRIBUTE_KEY_SLOT, String, Null,
                       Undefined);
  JS_DEFINE_TYPED_SLOT(ModuleTypeSlotIndex, MODULE_TYPE_SLOT, Int32);
  JS_DEFINE_TYPED_SLOT(PhaseSlotIndex, PHASE_SLOT, Int32);
  static constexpr uint32_t SLOT_COUNT = SlotCount;

 public:
  static const JSClass class_;
  static bool isInstance(HandleValue value);
  [[nodiscard]] static ModuleRequestObject* create(
      JSContext* cx, Handle<JSAtom*> specifier,
      Handle<ImportAttributeVector> maybeAttributes,
      ImportPhase phase = ImportPhase::Evaluation);
  [[nodiscard]] static ModuleRequestObject* create(
      JSContext* cx, Handle<JSAtom*> specifier, JS::ModuleType moduleType,
      ImportPhase phase = ImportPhase::Evaluation);

  JSAtom* specifier() const;
  JS::ModuleType moduleType() const;
  ImportPhase phase() const;

  // We process import attributes earlier in the process, but according to the
  // spec, we should error during module evaluation if we encounter an
  // unsupported attribute. We want to generate a nice error message, so we need
  // to keep track of the first unsupported key we encounter.
  void setFirstUnsupportedAttributeKey(Handle<JSAtom*> key);
  bool hasFirstUnsupportedAttributeKey() const;
  JSAtom* getFirstUnsupportedAttributeKey() const;
};

using ModuleRequestVector =
    GCVector<HeapPtr<ModuleRequestObject*>, 0, SystemAllocPolicy>;

class ImportEntry {
  const HeapPtr<ModuleRequestObject*> moduleRequest_;
  const HeapPtr<JSAtom*> importName_;
  const HeapPtr<JSAtom*> localName_;

  const ImportNameValueType importNameValueType_;

  // Line number (1-origin).
  const uint32_t lineNumber_;

  // Column number in UTF-16 code units.
  const JS::ColumnNumberOneOrigin columnNumber_;

 public:
  ImportEntry(Handle<ModuleRequestObject*> moduleRequest,
              Handle<JSAtom*> maybeImportName, Handle<JSAtom*> localName,
              ImportNameValueType importNameValueType, uint32_t lineNumber,
              JS::ColumnNumberOneOrigin columnNumber);

  ModuleRequestObject* moduleRequest() const { return moduleRequest_; }
  JSAtom* importName() const {
    MOZ_ASSERT_IF(importNameValueType_ != ImportNameValueType::String,
                  !importName_);
    return importName_;
  }
  JSAtom* localName() const { return localName_; }
  ImportNameValueType importNameValueType() const {
    MOZ_ASSERT_IF(importName_,
                  importNameValueType_ == ImportNameValueType::String);
    return importNameValueType_;
  }
  uint32_t lineNumber() const { return lineNumber_; }
  JS::ColumnNumberOneOrigin columnNumber() const { return columnNumber_; }

  void trace(JSTracer* trc);
};

using ImportEntryVector = GCVector<ImportEntry, 0, SystemAllocPolicy>;

class ExportEntry {
  const HeapPtr<JSAtom*> exportName_;
  const HeapPtr<ModuleRequestObject*> moduleRequest_;
  const HeapPtr<JSAtom*> importName_;
  const HeapPtr<JSAtom*> localName_;

  const ImportNameValueType importNameValueType_;

  // Line number (1-origin).
  const uint32_t lineNumber_;

  // Column number in UTF-16 code units.
  const JS::ColumnNumberOneOrigin columnNumber_;

 public:
  ExportEntry(Handle<JSAtom*> maybeExportName,
              Handle<ModuleRequestObject*> maybeModuleRequest,
              Handle<JSAtom*> maybeImportName, Handle<JSAtom*> maybeLocalName,
              ImportNameValueType importNameValueType, uint32_t lineNumber,
              JS::ColumnNumberOneOrigin columnNumber);
  JSAtom* exportName() const { return exportName_; }
  ModuleRequestObject* moduleRequest() const { return moduleRequest_; }
  JSAtom* importName() const {
    MOZ_ASSERT_IF(importNameValueType_ != ImportNameValueType::String,
                  !importName_);
    return importName_;
  }
  JSAtom* localName() const { return localName_; }
  ImportNameValueType importNameValueType() const {
    MOZ_ASSERT_IF(importName_,
                  importNameValueType_ == ImportNameValueType::String);
    return importNameValueType_;
  }
  uint32_t lineNumber() const { return lineNumber_; }
  JS::ColumnNumberOneOrigin columnNumber() const { return columnNumber_; }

  void trace(JSTracer* trc);
};

using ExportEntryVector = GCVector<ExportEntry, 0, SystemAllocPolicy>;

class RequestedModule {
  const HeapPtr<ModuleRequestObject*> moduleRequest_;

  // Line number (1-origin).
  const uint32_t lineNumber_;

  // Column number in UTF-16 code units.
  const JS::ColumnNumberOneOrigin columnNumber_;

 public:
  RequestedModule(Handle<ModuleRequestObject*> moduleRequest,
                  uint32_t lineNumber, JS::ColumnNumberOneOrigin columnNumber);
  ModuleRequestObject* moduleRequest() const { return moduleRequest_; }
  uint32_t lineNumber() const { return lineNumber_; }
  JS::ColumnNumberOneOrigin columnNumber() const { return columnNumber_; }

  void trace(JSTracer* trc);
};

using RequestedModuleVector = GCVector<RequestedModule, 0, SystemAllocPolicy>;

class ResolvedBindingObject : public NativeObject {
  enum { ModuleSlotIndex = 0, BindingNameSlotIndex, SlotCount };

  JS_DEFINE_TYPED_SLOT(ModuleSlotIndex, MODULE_SLOT, Object);
  JS_DEFINE_TYPED_SLOT(BindingNameSlotIndex, BINDING_NAME_SLOT, String);
  static constexpr uint32_t SLOT_COUNT = SlotCount;

 public:
  static const JSClass class_;
  static bool isInstance(HandleValue value);
  static ResolvedBindingObject* create(JSContext* cx,
                                       Handle<ModuleObject*> module,
                                       Handle<JSAtom*> bindingName);
  ModuleObject* module() const;
  JSAtom* bindingName() const;
};

class IndirectBindingMap {
 public:
  void trace(JSTracer* trc);

  bool put(JSContext* cx, HandleId name,
           Handle<ModuleEnvironmentObject*> environment, HandleId targetName);

  size_t count() const { return map_ ? map_->count() : 0; }

  bool has(jsid name) const { return map_ ? map_->has(name) : false; }

  bool lookup(jsid name, ModuleEnvironmentObject** envOut,
              mozilla::Maybe<PropertyInfo>* propOut) const;

  template <typename Func>
  void forEachExportedName(Func func) const {
    if (!map_) {
      return;
    }

    for (auto iter = map_->iter(); !iter.done(); iter.next()) {
      func(iter.get().key());
    }
  }

 private:
  struct Binding {
    Binding(ModuleEnvironmentObject* environment, jsid targetName,
            PropertyInfo prop);
    HeapPtr<ModuleEnvironmentObject*> environment;
#ifdef DEBUG
    HeapPtr<jsid> targetName;
#endif
    PropertyInfo prop;
  };

  using Map = mozilla::HashMap<PreBarriered<jsid>, Binding,
                               mozilla::DefaultHasher<PreBarriered<jsid>>,
                               CellAllocPolicy>;

  mozilla::Maybe<Map> map_;
};

// Vector of atoms representing the names exported from a module namespace.
//
// This is used both on the stack and in the heap.
using ExportNameVector = GCVector<HeapPtr<JSAtom*>, 0, SystemAllocPolicy>;

class ModuleNamespaceObject : public ProxyObject {
 public:
  enum ModuleNamespaceSlot { ExportsSlot = 0, BindingsSlot };

  static bool isInstance(HandleValue value);
  static ModuleNamespaceObject* create(
      JSContext* cx, Handle<ModuleObject*> module,
      MutableHandle<UniquePtr<ExportNameVector>> exports,
      MutableHandle<UniquePtr<IndirectBindingMap>> bindings);

  ModuleObject& module();
  const ExportNameVector& exports() const;
  IndirectBindingMap& bindings();

  bool addBinding(JSContext* cx, Handle<JSAtom*> exportedName,
                  Handle<ModuleObject*> targetModule,
                  Handle<JSAtom*> targetName);

 private:
  struct ProxyHandler : public BaseProxyHandler {
    constexpr ProxyHandler() : BaseProxyHandler(&family, false) {}

    bool getOwnPropertyDescriptor(
        JSContext* cx, HandleObject proxy, HandleId id,
        MutableHandle<mozilla::Maybe<PropertyDescriptor>> desc) const override;
    bool defineProperty(JSContext* cx, HandleObject proxy, HandleId id,
                        Handle<PropertyDescriptor> desc,
                        ObjectOpResult& result) const override;
    bool ownPropertyKeys(JSContext* cx, HandleObject proxy,
                         MutableHandleIdVector props) const override;
    bool delete_(JSContext* cx, HandleObject proxy, HandleId id,
                 ObjectOpResult& result) const override;
    bool getPrototype(JSContext* cx, HandleObject proxy,
                      MutableHandleObject protop) const override;
    bool setPrototype(JSContext* cx, HandleObject proxy, HandleObject proto,
                      ObjectOpResult& result) const override;
    bool getPrototypeIfOrdinary(JSContext* cx, HandleObject proxy,
                                bool* isOrdinary,
                                MutableHandleObject protop) const override;
    bool setImmutablePrototype(JSContext* cx, HandleObject proxy,
                               bool* succeeded) const override;

    bool preventExtensions(JSContext* cx, HandleObject proxy,
                           ObjectOpResult& result) const override;
    bool isExtensible(JSContext* cx, HandleObject proxy,
                      bool* extensible) const override;
    bool has(JSContext* cx, HandleObject proxy, HandleId id,
             bool* bp) const override;
    bool get(JSContext* cx, HandleObject proxy, HandleValue receiver,
             HandleId id, MutableHandleValue vp) const override;
    bool set(JSContext* cx, HandleObject proxy, HandleId id, HandleValue v,
             HandleValue receiver, ObjectOpResult& result) const override;

    void trace(JSTracer* trc, JSObject* proxy) const override;
    void finalize(JS::GCContext* gcx, JSObject* proxy) const override;

    static const char family;
  };

  bool hasBindings() const;
  bool hasExports() const;

  ExportNameVector& mutableExports();

 public:
  static const ProxyHandler proxyHandler;
};

// https://tc39.es/proposal-source-phase-imports/#sec-properties-of-the-%abstractmodulesource%-intrinsic-object
class AbstractModuleSourceObject : public NativeObject {
 public:
  static const JSClass class_;
};

// Value types of [[Status]] in a Cyclic Module Record
// https://tc39.es/ecma262/#table-cyclic-module-fields
enum class ModuleStatus : int8_t {
  New,
  Unlinked,
  Linking,
  Linked,
  Evaluating,
  EvaluatingAsync,
  Evaluated,

  // Sub-state of Evaluated with error value set.
  //
  // This is not returned from ModuleObject::status(); use hadEvaluationError()
  // to check this.
  Evaluated_Error
};

// Special values for CyclicModuleFields' asyncEvaluationOrderSlot field,
// which represents the AsyncEvaluationOrder field of cyclic module records.
//
// AsyncEvaluationOrder can have three states:
//  - a positive integer, represented by values <=
//    ASYNC_EVALUATING_POST_ORDER_MAX_VALUE
//  - ~unset~, represented by ASYNC_EVALUATING_POST_ORDER_UNSET
//  - ~done~, represented by ASYNC_EVALUATING_POST_ORDER_DONE
//
// See https://tc39.es/ecma262/#sec-cyclic-module-records for field defintion.
// See https://tc39.es/ecma262/#sec-async-module-execution-fulfilled for sort
// requirement.

// Value that the field is initially set to.
constexpr uint32_t ASYNC_EVALUATING_POST_ORDER_UNSET = UINT32_MAX;

// Value that the field is set to after being cleared.
constexpr uint32_t ASYNC_EVALUATING_POST_ORDER_DONE = UINT32_MAX - 1;

constexpr uint32_t ASYNC_EVALUATING_POST_ORDER_MAX_VALUE = UINT32_MAX - 2;

class AsyncEvaluationOrder {
 private:
  uint32_t value = ASYNC_EVALUATING_POST_ORDER_UNSET;

 public:
  bool isUnset() const;
  bool isInteger() const;
  bool isDone() const;

  uint32_t get() const;

  void set(JSRuntime* rt);
  void setDone(JSRuntime* rt);
};

// The map used by [[LoadedModules]] in Realm Record Fields, Script Record
// Fields, and additional fields of Cyclic Module Records.
// https://tc39.es/ecma262/#table-realm-record-fields
// https://tc39.es/ecma262/#table-script-records
// https://tc39.es/ecma262/#table-cyclic-module-fields
//
// For Import attributes proposal, this map maps from ModuleRequest records to
// Module records.
// https://tc39.es/proposal-import-attributes/#sec-cyclic-module-records
//
// TODO:
// Bug 1968874 : Implement [[LoadedModules]] in Realm Records and Script Records
using LoadedModuleMap =
    GCHashMap<HeapPtr<JSObject*>, HeapPtr<ModuleObject*>,
              StableCellHasher<HeapPtr<JSObject*>>, SystemAllocPolicy>;

// Currently, the ModuleObject class is used to represent both the Source Text
// Module Record and the Synthetic Module Record. Ideally, this is something
// that should be refactored to follow the same hierarchy as in the spec.
// TODO: See Bug 1880519.
class ModuleObject : public NativeObject {
 public:
  enum ModuleSlot {
    ScriptSlotIndex = 0,
    ModuleEnvironmentSlotIndex,
    NamespaceSlotIndex,
    CyclicModuleFieldsSlotIndex,
    SyntheticModuleFieldsSlotIndex,
#ifdef DEBUG
    PreloadSlotIndex,
#endif
    ModuleSourceSlotIndex,
    SlotCount
  };

  // Module fields including those for AbstractModuleRecords described by:
  // https://tc39.es/ecma262/#sec-abstract-module-records
  JS_DEFINE_TYPED_SLOT(ScriptSlotIndex, SCRIPT_SLOT, PrivateGCThing, Undefined);
  JS_DEFINE_TYPED_SLOT(ModuleEnvironmentSlotIndex, MODULE_ENVIRONMENT_SLOT,
                       Object);
  JS_DEFINE_TYPED_SLOT(NamespaceSlotIndex, NAMESPACE_SLOT, Object, Undefined);
  JS_DEFINE_TYPED_SLOT(CyclicModuleFieldsSlotIndex, CYCLIC_MODULE_FIELDS_SLOT,
                       Private, Undefined);
  // `SyntheticModuleFields` if a synthetic module. Otherwise `undefined`.
  JS_DEFINE_TYPED_SLOT(SyntheticModuleFieldsSlotIndex,
                       SYNTHETIC_MODULE_FIELDS_SLOT, Private, Undefined);
#ifdef DEBUG
  JS_DEFINE_TYPED_SLOT(PreloadSlotIndex, PRELOAD_SLOT, Boolean, Undefined);
#endif
  // Module Source object for source phase imports. Otherwise `undefined`.
  JS_DEFINE_TYPED_SLOT(ModuleSourceSlotIndex, MODULE_SOURCE_SLOT, Object,
                       Undefined);
  static constexpr uint32_t SLOT_COUNT = SlotCount;

  static const JSClass class_;

  static bool isInstance(HandleValue value);

  static ModuleObject* create(JSContext* cx);

  static ModuleObject* createSynthetic(
      JSContext* cx, MutableHandle<ExportNameVector> exportNames);

  // Initialize the slots on this object that are dependent on the script.
  [[nodiscard]] bool initScriptSlots(JSContext* cx, HandleScript script);
  void initModuleSourceSlot(HandleObject moduleSource);
  void initScriptSourceObject(ScriptSourceObject* sso);

  void setInitialEnvironment(
      Handle<ModuleEnvironmentObject*> initialEnvironment);

  void initFunctionDeclarations(UniquePtr<FunctionDeclarationVector> decls);
  void initImportExportData(
      MutableHandle<RequestedModuleVector> requestedModules,
      MutableHandle<ImportEntryVector> importEntries,
      MutableHandle<ExportEntryVector> exportEntries, uint32_t localExportCount,
      uint32_t indirectExportCount, uint32_t starExportCount);
  static bool Freeze(JSContext* cx, Handle<ModuleObject*> self);
#ifdef DEBUG
  static bool AssertFrozen(JSContext* cx, Handle<ModuleObject*> self);
#endif

  JSScript* maybeScript() const;
  JSScript* script() const;
  const char* filename() const;
  ModuleEnvironmentObject& initialEnvironment() const;
  ModuleEnvironmentObject* environment() const;
  ModuleNamespaceObject* namespace_();
  JSObject* moduleSource() const;
  bool isSourcePhaseModule() const { return moduleSource() != nullptr; }
  ModuleStatus status() const;
  mozilla::Maybe<uint32_t> maybeDfsAncestorIndex() const;
  uint32_t dfsAncestorIndex() const;
  bool hadEvaluationError() const;
  Value maybeEvaluationError() const;
  Value evaluationError() const;
  JSObject* metaObject() const;
  ScriptSourceObject* scriptSourceObject() const;
  mozilla::Span<const RequestedModule> requestedModules() const;
  mozilla::Span<const ImportEntry> importEntries() const;
  mozilla::Span<const ExportEntry> localExportEntries() const;
  mozilla::Span<const ExportEntry> indirectExportEntries() const;
  mozilla::Span<const ExportEntry> starExportEntries() const;
  const ExportNameVector& syntheticExportNames() const;

  IndirectBindingMap& importBindings();

  void setStatus(ModuleStatus newStatus);
  void setDfsAncestorIndex(uint32_t index);
  void clearDfsAncestorIndex();

  static PromiseObject* createTopLevelCapability(JSContext* cx,
                                                 Handle<ModuleObject*> module);
  bool hasTopLevelAwait() const;
  void setEvaluationError(HandleValue newValue);
  void setPendingAsyncDependencies(uint32_t newValue);
  void setInitialTopLevelCapability(Handle<PromiseObject*> capability);
  bool hasTopLevelCapability() const;
  PromiseObject* maybeTopLevelCapability() const;
  PromiseObject* topLevelCapability() const;
  ListObject* asyncParentModules() const;
  mozilla::Maybe<uint32_t> maybePendingAsyncDependencies() const;
  uint32_t pendingAsyncDependencies() const;
  AsyncEvaluationOrder& asyncEvaluationOrder();
  AsyncEvaluationOrder const& asyncEvaluationOrder() const;
  void setCycleRoot(ModuleObject* cycleRoot);
  ModuleObject* getCycleRoot() const;
  bool hasCycleRoot() const;
  bool hasCyclicModuleFields() const;
  bool hasSyntheticModuleFields() const;
  LoadedModuleMap& loadedModules();
  const LoadedModuleMap& loadedModules() const;

  static void onTopLevelEvaluationFinished(ModuleObject* module);

  static bool appendAsyncParentModule(JSContext* cx, Handle<ModuleObject*> self,
                                      Handle<ModuleObject*> parent);

  [[nodiscard]] static bool topLevelCapabilityResolve(
      JSContext* cx, Handle<ModuleObject*> module);
  [[nodiscard]] static bool topLevelCapabilityReject(
      JSContext* cx, Handle<ModuleObject*> module, HandleValue error);

  void setMetaObject(JSObject* obj);

  static bool instantiateFunctionDeclarations(JSContext* cx,
                                              Handle<ModuleObject*> self);

  static bool execute(JSContext* cx, Handle<ModuleObject*> self);

  static ModuleNamespaceObject* createNamespace(
      JSContext* cx, Handle<ModuleObject*> self,
      MutableHandle<UniquePtr<ExportNameVector>> exports);
  void clearNamespaceOnFailure();

  static bool createEnvironment(JSContext* cx, Handle<ModuleObject*> self);
  static bool createSyntheticEnvironment(JSContext* cx,
                                         Handle<ModuleObject*> self,
                                         JS::HandleVector<Value> values);
  static bool createWasmEnvironment(JSContext* cx, Handle<ModuleObject*> self);

  void initAsyncSlots(JSContext* cx, bool hasTopLevelAwait,
                      Handle<ListObject*> asyncParentModules);

#ifdef DEBUG
  void setPreload(bool isPreload);
  bool isPreload() const;
#endif

 private:
  static const JSClassOps classOps_;

  static void trace(JSTracer* trc, JSObject* obj);
  static void finalize(JS::GCContext* gcx, JSObject* obj);

  CyclicModuleFields* cyclicModuleFields();
  const CyclicModuleFields* cyclicModuleFields() const;

  SyntheticModuleFields* syntheticModuleFields();
  const SyntheticModuleFields* syntheticModuleFields() const;
};

using VisitedModuleSet =
    GCHashSet<HeapPtr<JSObject*>, StableCellHasher<HeapPtr<JSObject*>>,
              SystemAllocPolicy>;

// The fields of a GraphLoadingState Record, as described in:
// https://tc39.es/ecma262/#graphloadingstate-record
struct GraphLoadingStateRecord {
  GraphLoadingStateRecord() = default;
  GraphLoadingStateRecord(JS::LoadModuleResolvedCallback resolved,
                          JS::LoadModuleRejectedCallback rejected);

  void trace(JSTracer* trc);

  // [[Visited]] : a List of Cyclic Module Records
  VisitedModuleSet visited;

  JS::LoadModuleResolvedCallback resolved;
  JS::LoadModuleRejectedCallback rejected;
};

class GraphLoadingStateRecordObject : public NativeObject {
  JS_DEFINE_TYPED_SLOT(0, STATE_SLOT, Private, Undefined);
  JS_DEFINE_TYPED_SLOT(1, PROMISE_SLOT, Object, Undefined);
  JS_DEFINE_TYPED_SLOT(2, IS_LOADING_SLOT, Int32);
  JS_DEFINE_TYPED_SLOT(3, PENDING_MODULES_COUNT_SLOT, Int32);
  JS_DEFINE_UNTYPED_SLOT(4, HOST_DEFINED_SLOT);
  static constexpr uint32_t SLOT_COUNT = 5;

 public:
  static const JSClass class_;
  static const JSClassOps classOps_;

  [[nodiscard]] static GraphLoadingStateRecordObject* create(
      JSContext* cx, bool isLoading, uint32_t pendingModulesCount,
      JS::LoadModuleResolvedCallback resolved,
      JS::LoadModuleRejectedCallback rejected, Handle<Value> hostDefined);

  [[nodiscard]] static GraphLoadingStateRecordObject* create(
      JSContext* cx, bool isLoading, uint32_t pendingModulesCount,
      Handle<PromiseObject*> promise, Handle<Value> hostDefined);

  static void finalize(JS::GCContext* gcx, JSObject* obj);
  static void trace(JSTracer* trc, JSObject* obj);

  // [[PromiseCapability]] : a PromiseCapability Record
  PromiseObject* promise();

  // [[IsLoading]] : a Boolean
  bool isLoading();
  void setIsLoading(bool isLoading);

  // [[PendingModulesCount]] : a non-negative integer
  uint32_t pendingModulesCount();
  void setPendingModulesCount(uint32_t count);

  VisitedModuleSet& visited();

  Value hostDefined();

  bool resolved(JSContext* cx, JS::Handle<JS::Value> hostDefined);
  bool rejected(JSContext* cx, JS::Handle<JS::Value> hostDefined,
                Handle<JS::Value> error);
};

JSObject* GetOrCreateModuleMetaObject(JSContext* cx, HandleObject module);

JSObject* StartDynamicModuleImport(JSContext* cx, HandleScript script,
                                   HandleValue specifier, HandleValue options,
                                   ImportPhase phase);

}  // namespace js

template <>
inline bool JSObject::is<js::ModuleNamespaceObject>() const {
  return js::IsDerivedProxyObject(this,
                                  &js::ModuleNamespaceObject::proxyHandler);
}

#endif /* builtin_ModuleObject_h */
