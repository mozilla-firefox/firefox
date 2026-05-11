/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "builtin/ModuleObject.h"

#include "mozilla/DebugOnly.h"
#include "mozilla/ScopeExit.h"

#include "builtin/Promise.h"
#include "builtin/SelfHostingDefines.h"
#include "frontend/ParseNode.h"
#include "frontend/ParserAtom.h"  // TaggedParserAtomIndex, ParserAtomsTable, ParserAtom
#include "frontend/SharedContext.h"
#include "frontend/Stencil.h"
#include "gc/GCContext.h"
#include "gc/Tracer.h"
#include "js/ColumnNumber.h"  // JS::ColumnNumberOneOrigin, JS::LimitedColumnNumberOneOrigin
#include "js/friend/ErrorMessages.h"  // JSMSG_*
#include "js/Modules.h"  // JS::GetModulePrivate, JS::ModuleDynamicImportHook, JS::ModuleType
#include "vm/EqualityOperations.h"  // js::SameValue
#include "vm/Interpreter.h"    // Execute, Lambda, ReportRuntimeLexicalError
#include "vm/ModuleBuilder.h"  // js::ModuleBuilder
#include "vm/Modules.h"
#include "vm/PlainObject.h"    // js::PlainObject
#include "vm/PromiseObject.h"  // js::PromiseObject
#include "vm/SharedStencil.h"  // js::GCThingIndex

#include "gc/GCContext-inl.h"
#include "vm/EnvironmentObject-inl.h"  // EnvironmentObject::setAliasedBinding
#include "vm/JSObject-inl.h"
#include "vm/JSScript-inl.h"
#include "vm/List-inl.h"
#include "vm/NativeObject-inl.h"

using namespace js;

using mozilla::Maybe;
using mozilla::Nothing;
using mozilla::Some;
using mozilla::Span;

static_assert(ModuleStatus::New < ModuleStatus::Unlinked &&
                  ModuleStatus::Unlinked < ModuleStatus::Linking &&
                  ModuleStatus::Linking < ModuleStatus::Linked &&
                  ModuleStatus::Linked < ModuleStatus::Evaluating &&
                  ModuleStatus::Evaluating < ModuleStatus::EvaluatingAsync &&
                  ModuleStatus::EvaluatingAsync < ModuleStatus::Evaluated &&
                  ModuleStatus::Evaluated < ModuleStatus::Evaluated_Error,
              "Module statuses are ordered incorrectly");

static Value StringOrNullValue(JSString* maybeString) {
  return maybeString ? StringValue(maybeString) : NullValue();
}

static Value ModuleTypeToValue(JS::ModuleType moduleType) {
  static_assert(size_t(JS::ModuleType::Limit) <= INT32_MAX);
  return Int32Value(int32_t(moduleType));
}

static JS::ModuleType ValueToModuleType(const Value& value) {
  int32_t i = value.toInt32();
  MOZ_ASSERT(i >= 0 && i <= int32_t(JS::ModuleType::Limit));
  return static_cast<JS::ModuleType>(i);
}

static Value ImportPhaseToValue(ImportPhase phase) {
  static_assert(size_t(ImportPhase::Limit) <= INT32_MAX);
  return Int32Value(int32_t(phase));
}

static ImportPhase ValueToImportPhase(const Value& value) {
  int32_t i = value.toInt32();
  MOZ_ASSERT(i >= 0 && i <= int32_t(ImportPhase::Limit));
  return static_cast<ImportPhase>(i);
}

#define DEFINE_ATOM_ACCESSOR_METHOD(cls, name, slot) \
  JSAtom* cls::name() const {                        \
    Value value = getReservedSlot(slot);             \
    return &value.toString()->asAtom();              \
  }

#define DEFINE_ATOM_OR_NULL_ACCESSOR_METHOD(cls, name, slot) \
  JSAtom* cls::name() const {                                \
    Value value = getReservedSlot(slot);                     \
    if (value.isNull()) {                                    \
      return nullptr;                                        \
    }                                                        \
    return &value.toString()->asAtom();                      \
  }

///////////////////////////////////////////////////////////////////////////
// ImportEntry

ImportEntry::ImportEntry(Handle<ModuleRequestObject*> moduleRequest,
                         Handle<JSAtom*> maybeImportName,
                         Handle<JSAtom*> localName, uint32_t lineNumber,
                         JS::ColumnNumberOneOrigin columnNumber)
    : moduleRequest_(moduleRequest),
      importName_(maybeImportName),
      localName_(localName),
      lineNumber_(lineNumber),
      columnNumber_(columnNumber) {}

void ImportEntry::trace(JSTracer* trc) {
  TraceEdge(trc, &moduleRequest_, "ImportEntry::moduleRequest_");
  TraceEdge(trc, &importName_, "ImportEntry::importName_");
  TraceEdge(trc, &localName_, "ImportEntry::localName_");
}

///////////////////////////////////////////////////////////////////////////
// ExportEntry

ExportEntry::ExportEntry(Handle<JSAtom*> maybeExportName,
                         Handle<ModuleRequestObject*> moduleRequest,
                         Handle<JSAtom*> maybeImportName,
                         Handle<JSAtom*> maybeLocalName, uint32_t lineNumber,
                         JS::ColumnNumberOneOrigin columnNumber)
    : exportName_(maybeExportName),
      moduleRequest_(moduleRequest),
      importName_(maybeImportName),
      localName_(maybeLocalName),
      lineNumber_(lineNumber),
      columnNumber_(columnNumber) {
  // Line and column numbers are optional for export entries since direct
  // entries are checked at parse time.
}

void ExportEntry::trace(JSTracer* trc) {
  TraceEdge(trc, &exportName_, "ExportEntry::exportName_");
  TraceEdge(trc, &moduleRequest_, "ExportEntry::moduleRequest_");
  TraceEdge(trc, &importName_, "ExportEntry::importName_");
  TraceEdge(trc, &localName_, "ExportEntry::localName_");
}

///////////////////////////////////////////////////////////////////////////
// RequestedModule

/* static */
RequestedModule::RequestedModule(Handle<ModuleRequestObject*> moduleRequest,
                                 uint32_t lineNumber,
                                 JS::ColumnNumberOneOrigin columnNumber)
    : moduleRequest_(moduleRequest),
      lineNumber_(lineNumber),
      columnNumber_(columnNumber) {}

void RequestedModule::trace(JSTracer* trc) {
  TraceEdge(trc, &moduleRequest_, "RequestedModule::moduleRequest_");
}

///////////////////////////////////////////////////////////////////////////
// ResolvedBindingObject

/* static */ const JSClass ResolvedBindingObject::class_ = {
    "ResolvedBinding",
    JSCLASS_HAS_RESERVED_SLOTS(ResolvedBindingObject::SlotCount),
};

ModuleObject* ResolvedBindingObject::module() const {
  Value value = getReservedSlot(ModuleSlot);
  return &value.toObject().as<ModuleObject>();
}

JSAtom* ResolvedBindingObject::bindingName() const {
  Value value = getReservedSlot(BindingNameSlot);
  return &value.toString()->asAtom();
}

/* static */
bool ResolvedBindingObject::isInstance(HandleValue value) {
  return value.isObject() && value.toObject().is<ResolvedBindingObject>();
}

/* static */
ResolvedBindingObject* ResolvedBindingObject::create(
    JSContext* cx, Handle<ModuleObject*> module, Handle<JSAtom*> bindingName) {
  ResolvedBindingObject* self =
      NewObjectWithGivenProto<ResolvedBindingObject>(cx, nullptr);
  if (!self) {
    return nullptr;
  }

  self->initReservedSlot(ModuleSlot, ObjectValue(*module));
  self->initReservedSlot(BindingNameSlot, StringValue(bindingName));
  return self;
}

///////////////////////////////////////////////////////////////////////////
// ImportAttribute

ImportAttribute::ImportAttribute(Handle<JSAtom*> key, Handle<JSString*> value)
    : key_(key), value_(value) {}

void ImportAttribute::trace(JSTracer* trc) {
  TraceEdge(trc, &key_, "ImportAttribute::key_");
  TraceEdge(trc, &value_, "ImportAttribute::value_");
}

///////////////////////////////////////////////////////////////////////////
// ModuleRequestObject
/* static */ const JSClass ModuleRequestObject::class_ = {
    "ModuleRequest",
    JSCLASS_HAS_RESERVED_SLOTS(ModuleRequestObject::SlotCount),
};

DEFINE_ATOM_OR_NULL_ACCESSOR_METHOD(ModuleRequestObject, specifier,
                                    SpecifierSlot)

JS::ModuleType ModuleRequestObject::moduleType() const {
  return ValueToModuleType(getReservedSlot(ModuleTypeSlot));
}

ImportPhase ModuleRequestObject::phase() const {
  return ValueToImportPhase(getReservedSlot(PhaseSlot));
}

static bool GetModuleType(JSContext* cx,
                          Handle<ImportAttributeVector> maybeAttributes,
                          JS::ModuleType& moduleType) {
  for (const ImportAttribute& importAttribute : maybeAttributes) {
    if (importAttribute.key() == cx->names().type) {
      Rooted<JSLinearString*> typeStr(
          cx, importAttribute.value()->ensureLinear(cx));
      if (!typeStr) {
        return false;
      }

      if (js::EqualStrings(typeStr, cx->names().json)) {
        moduleType = JS::ModuleType::JSON;
      } else if (js::EqualStrings(typeStr, cx->names().css)) {
        moduleType = JS::ModuleType::CSS;
      }
#ifdef NIGHTLY_BUILD
      else if (JS::Prefs::experimental_import_bytes() &&
               js::EqualStrings(typeStr, cx->names().bytes)) {
        moduleType = JS::ModuleType::Bytes;
      } else if (JS::Prefs::experimental_import_text() &&
                 js::EqualStrings(typeStr, cx->names().text)) {
        moduleType = JS::ModuleType::Text;
      }
#endif
      else {
        moduleType = JS::ModuleType::Unknown;
      }

      return true;
    }
  }

  moduleType = JS::ModuleType::JavaScript;
  return true;
}

/* static */
bool ModuleRequestObject::isInstance(HandleValue value) {
  return value.isObject() && value.toObject().is<ModuleRequestObject>();
}

/* static */
ModuleRequestObject* ModuleRequestObject::create(
    JSContext* cx, Handle<JSAtom*> specifier,
    Handle<ImportAttributeVector> maybeAttributes, ImportPhase phase) {
  JS::ModuleType moduleType = JS::ModuleType::JavaScript;
  if (!GetModuleType(cx, maybeAttributes, moduleType)) {
    return nullptr;
  }

  return create(cx, specifier, moduleType, phase);
}

/* static */
ModuleRequestObject* ModuleRequestObject::create(JSContext* cx,
                                                 Handle<JSAtom*> specifier,
                                                 JS::ModuleType moduleType,
                                                 ImportPhase phase) {
  ModuleRequestObject* self =
      NewObjectWithGivenProto<ModuleRequestObject>(cx, nullptr);
  if (!self) {
    return nullptr;
  }

  self->initReservedSlot(SpecifierSlot, StringOrNullValue(specifier));
  self->initReservedSlot(ModuleTypeSlot, ModuleTypeToValue(moduleType));
  self->initReservedSlot(PhaseSlot, ImportPhaseToValue(phase));

  return self;
}

void ModuleRequestObject::setFirstUnsupportedAttributeKey(Handle<JSAtom*> key) {
  initReservedSlot(FirstUnsupportedAttributeKeySlot, StringOrNullValue(key));
}

bool ModuleRequestObject::hasFirstUnsupportedAttributeKey() const {
  return !getReservedSlot(FirstUnsupportedAttributeKeySlot).isNullOrUndefined();
}

JSAtom* ModuleRequestObject::getFirstUnsupportedAttributeKey() const {
  if (!hasFirstUnsupportedAttributeKey()) {
    return nullptr;
  }
  return &getReservedSlot(FirstUnsupportedAttributeKeySlot)
              .toString()
              ->asAtom();
}

///////////////////////////////////////////////////////////////////////////
// IndirectBindingMap

IndirectBindingMap::Binding::Binding(ModuleEnvironmentObject* environment,
                                     jsid targetName, PropertyInfo prop)
    : environment(environment),
#ifdef DEBUG
      targetName(targetName),
#endif
      prop(prop) {
}

void IndirectBindingMap::trace(JSTracer* trc) {
  if (!map_) {
    return;
  }

  for (auto iter = map_->modIter(); !iter.done(); iter.next()) {
    Binding& b = iter.get().value();
    TraceEdge(trc, &b.environment, "module bindings environment");
#ifdef DEBUG
    TraceEdge(trc, &b.targetName, "module bindings target name");
#endif
    mozilla::DebugOnly<jsid> prev(iter.get().key());
    TraceEdge(trc, &iter.get().mutableKey(), "module bindings binding name");
    MOZ_ASSERT(iter.get().key() == prev);
  }
}

bool IndirectBindingMap::put(JSContext* cx, HandleId name,
                             Handle<ModuleEnvironmentObject*> environment,
                             HandleId targetName) {
  if (!map_) {
    map_.emplace(cx->zone());
  }

  mozilla::Maybe<PropertyInfo> prop = environment->lookup(cx, targetName);
  MOZ_ASSERT(prop.isSome());
  if (!map_->put(name, Binding(environment, targetName, *prop))) {
    ReportOutOfMemory(cx);
    return false;
  }

  return true;
}

bool IndirectBindingMap::lookup(jsid name, ModuleEnvironmentObject** envOut,
                                mozilla::Maybe<PropertyInfo>* propOut) const {
  if (!map_) {
    return false;
  }

  auto ptr = map_->lookup(name);
  if (!ptr) {
    return false;
  }

  const Binding& binding = ptr->value();
  MOZ_ASSERT(binding.environment);
  MOZ_ASSERT(
      binding.environment->containsPure(binding.targetName, binding.prop));
  *envOut = binding.environment;
  *propOut = Some(binding.prop);
  return true;
}

///////////////////////////////////////////////////////////////////////////
// ModuleNamespaceObject

/* static */
constexpr ModuleNamespaceObject::ProxyHandler
    ModuleNamespaceObject::proxyHandler;

/* static */
bool ModuleNamespaceObject::isInstance(HandleValue value) {
  return value.isObject() && value.toObject().is<ModuleNamespaceObject>();
}

/* static */
ModuleNamespaceObject* ModuleNamespaceObject::create(
    JSContext* cx, Handle<ModuleObject*> module,
    MutableHandle<UniquePtr<ExportNameVector>> exports,
    MutableHandle<UniquePtr<IndirectBindingMap>> bindings) {
  RootedValue priv(cx, ObjectValue(*module));
  ProxyOptions options;
  options.setLazyProto(true);

  RootedObject object(
      cx, NewProxyObject(cx, &proxyHandler, priv, nullptr, options));
  if (!object) {
    return nullptr;
  }

  SetProxyReservedSlot(object, ExportsSlot,
                       PrivateValue(exports.get().release()));
  AddCellMemory(object, sizeof(ExportNameVector), MemoryUse::ModuleExports);

  SetProxyReservedSlot(object, BindingsSlot,
                       PrivateValue(bindings.get().release()));
  AddCellMemory(object, sizeof(IndirectBindingMap),
                MemoryUse::ModuleBindingMap);

  return &object->as<ModuleNamespaceObject>();
}

ModuleObject& ModuleNamespaceObject::module() {
  return GetProxyPrivate(this).toObject().as<ModuleObject>();
}

const ExportNameVector& ModuleNamespaceObject::exports() const {
  Value value = GetProxyReservedSlot(this, ExportsSlot);
  auto* exports = static_cast<ExportNameVector*>(value.toPrivate());
  MOZ_ASSERT(exports);
  return *exports;
}

ExportNameVector& ModuleNamespaceObject::mutableExports() {
  // Get a non-const reference for tracing/destruction. Do not actually mutate
  // this vector!  This would be incorrect without adding barriers.
  return const_cast<ExportNameVector&>(exports());
}

IndirectBindingMap& ModuleNamespaceObject::bindings() {
  Value value = GetProxyReservedSlot(this, BindingsSlot);
  auto* bindings = static_cast<IndirectBindingMap*>(value.toPrivate());
  MOZ_ASSERT(bindings);
  return *bindings;
}

bool ModuleNamespaceObject::hasExports() const {
  // Exports may not be present if we hit OOM in initialization.
  return !GetProxyReservedSlot(this, ExportsSlot).isUndefined();
}

bool ModuleNamespaceObject::hasBindings() const {
  // Import bindings may not be present if we hit OOM in initialization.
  return !GetProxyReservedSlot(this, BindingsSlot).isUndefined();
}

bool ModuleNamespaceObject::addBinding(JSContext* cx,
                                       Handle<JSAtom*> exportedName,
                                       Handle<ModuleObject*> targetModule,
                                       Handle<JSAtom*> targetName) {
  Rooted<ModuleEnvironmentObject*> environment(
      cx, &targetModule->initialEnvironment());
  RootedId exportedNameId(cx, AtomToId(exportedName));
  RootedId targetNameId(cx, AtomToId(targetName));
  return bindings().put(cx, exportedNameId, environment, targetNameId);
}

constexpr char ModuleNamespaceObject::ProxyHandler::family = 0;

bool ModuleNamespaceObject::ProxyHandler::getPrototype(
    JSContext* cx, HandleObject proxy, MutableHandleObject protop) const {
  protop.set(nullptr);
  return true;
}

bool ModuleNamespaceObject::ProxyHandler::setPrototype(
    JSContext* cx, HandleObject proxy, HandleObject proto,
    ObjectOpResult& result) const {
  if (!proto) {
    return result.succeed();
  }
  return result.failCantSetProto();
}

bool ModuleNamespaceObject::ProxyHandler::getPrototypeIfOrdinary(
    JSContext* cx, HandleObject proxy, bool* isOrdinary,
    MutableHandleObject protop) const {
  *isOrdinary = false;
  return true;
}

bool ModuleNamespaceObject::ProxyHandler::setImmutablePrototype(
    JSContext* cx, HandleObject proxy, bool* succeeded) const {
  *succeeded = true;
  return true;
}

bool ModuleNamespaceObject::ProxyHandler::isExtensible(JSContext* cx,
                                                       HandleObject proxy,
                                                       bool* extensible) const {
  *extensible = false;
  return true;
}

bool ModuleNamespaceObject::ProxyHandler::preventExtensions(
    JSContext* cx, HandleObject proxy, ObjectOpResult& result) const {
  result.succeed();
  return true;
}

bool ModuleNamespaceObject::ProxyHandler::getOwnPropertyDescriptor(
    JSContext* cx, HandleObject proxy, HandleId id,
    MutableHandle<mozilla::Maybe<PropertyDescriptor>> desc) const {
  Rooted<ModuleNamespaceObject*> ns(cx, &proxy->as<ModuleNamespaceObject>());
  if (id.isSymbol()) {
    if (id.isWellKnownSymbol(JS::SymbolCode::toStringTag)) {
      desc.set(Some(PropertyDescriptor::Data(StringValue(cx->names().Module))));
      return true;
    }

    desc.reset();
    return true;
  }

  const IndirectBindingMap& bindings = ns->bindings();
  ModuleEnvironmentObject* env;
  mozilla::Maybe<PropertyInfo> prop;
  if (!bindings.lookup(id, &env, &prop)) {
    // Not found.
    desc.reset();
    return true;
  }

  RootedValue value(cx, env->getSlot(prop->slot()));
  if (value.isMagic(JS_UNINITIALIZED_LEXICAL)) {
    ReportRuntimeLexicalError(cx, JSMSG_UNINITIALIZED_LEXICAL, id);
    return false;
  }

  desc.set(
      Some(PropertyDescriptor::Data(value, {JS::PropertyAttribute::Enumerable,
                                            JS::PropertyAttribute::Writable})));
  return true;
}

static bool ValidatePropertyDescriptor(
    JSContext* cx, Handle<PropertyDescriptor> desc, bool expectedWritable,
    bool expectedEnumerable, bool expectedConfigurable,
    HandleValue expectedValue, ObjectOpResult& result) {
  if (desc.isAccessorDescriptor()) {
    return result.fail(JSMSG_CANT_REDEFINE_PROP);
  }

  if (desc.hasWritable() && desc.writable() != expectedWritable) {
    return result.fail(JSMSG_CANT_REDEFINE_PROP);
  }

  if (desc.hasEnumerable() && desc.enumerable() != expectedEnumerable) {
    return result.fail(JSMSG_CANT_REDEFINE_PROP);
  }

  if (desc.hasConfigurable() && desc.configurable() != expectedConfigurable) {
    return result.fail(JSMSG_CANT_REDEFINE_PROP);
  }

  if (desc.hasValue()) {
    bool same;
    if (!SameValue(cx, desc.value(), expectedValue, &same)) {
      return false;
    }
    if (!same) {
      return result.fail(JSMSG_CANT_REDEFINE_PROP);
    }
  }

  return result.succeed();
}

bool ModuleNamespaceObject::ProxyHandler::defineProperty(
    JSContext* cx, HandleObject proxy, HandleId id,
    Handle<PropertyDescriptor> desc, ObjectOpResult& result) const {
  if (id.isSymbol()) {
    if (id.isWellKnownSymbol(JS::SymbolCode::toStringTag)) {
      RootedValue value(cx, StringValue(cx->names().Module));
      return ValidatePropertyDescriptor(cx, desc, false, false, false, value,
                                        result);
    }
    return result.fail(JSMSG_CANT_DEFINE_PROP_OBJECT_NOT_EXTENSIBLE);
  }

  const IndirectBindingMap& bindings =
      proxy->as<ModuleNamespaceObject>().bindings();
  ModuleEnvironmentObject* env;
  mozilla::Maybe<PropertyInfo> prop;
  if (!bindings.lookup(id, &env, &prop)) {
    return result.fail(JSMSG_CANT_DEFINE_PROP_OBJECT_NOT_EXTENSIBLE);
  }

  RootedValue value(cx, env->getSlot(prop->slot()));
  if (value.isMagic(JS_UNINITIALIZED_LEXICAL)) {
    ReportRuntimeLexicalError(cx, JSMSG_UNINITIALIZED_LEXICAL, id);
    return false;
  }

  return ValidatePropertyDescriptor(cx, desc, true, true, false, value, result);
}

bool ModuleNamespaceObject::ProxyHandler::has(JSContext* cx, HandleObject proxy,
                                              HandleId id, bool* bp) const {
  Rooted<ModuleNamespaceObject*> ns(cx, &proxy->as<ModuleNamespaceObject>());
  if (id.isSymbol()) {
    *bp = id.isWellKnownSymbol(JS::SymbolCode::toStringTag);
    return true;
  }

  *bp = ns->bindings().has(id);
  return true;
}

bool ModuleNamespaceObject::ProxyHandler::get(JSContext* cx, HandleObject proxy,
                                              HandleValue receiver, HandleId id,
                                              MutableHandleValue vp) const {
  Rooted<ModuleNamespaceObject*> ns(cx, &proxy->as<ModuleNamespaceObject>());
  if (id.isSymbol()) {
    if (id.isWellKnownSymbol(JS::SymbolCode::toStringTag)) {
      vp.setString(cx->names().Module);
      return true;
    }

    vp.setUndefined();
    return true;
  }

  ModuleEnvironmentObject* env;
  mozilla::Maybe<PropertyInfo> prop;
  if (!ns->bindings().lookup(id, &env, &prop)) {
    vp.setUndefined();
    return true;
  }

  RootedValue value(cx, env->getSlot(prop->slot()));
  if (value.isMagic(JS_UNINITIALIZED_LEXICAL)) {
    ReportRuntimeLexicalError(cx, JSMSG_UNINITIALIZED_LEXICAL, id);
    return false;
  }

  vp.set(value);
  return true;
}

bool ModuleNamespaceObject::ProxyHandler::set(JSContext* cx, HandleObject proxy,
                                              HandleId id, HandleValue v,
                                              HandleValue receiver,
                                              ObjectOpResult& result) const {
  return result.failReadOnly();
}

bool ModuleNamespaceObject::ProxyHandler::delete_(
    JSContext* cx, HandleObject proxy, HandleId id,
    ObjectOpResult& result) const {
  Rooted<ModuleNamespaceObject*> ns(cx, &proxy->as<ModuleNamespaceObject>());
  if (id.isSymbol()) {
    if (id.isWellKnownSymbol(JS::SymbolCode::toStringTag)) {
      return result.failCantDelete();
    }

    return result.succeed();
  }

  if (ns->bindings().has(id)) {
    return result.failCantDelete();
  }

  return result.succeed();
}

bool ModuleNamespaceObject::ProxyHandler::ownPropertyKeys(
    JSContext* cx, HandleObject proxy, MutableHandleIdVector props) const {
  Rooted<ModuleNamespaceObject*> ns(cx, &proxy->as<ModuleNamespaceObject>());
  uint32_t count = ns->exports().length();
  if (!props.reserve(props.length() + count + 1)) {
    return false;
  }

  for (JSAtom* atom : ns->exports()) {
    props.infallibleAppend(AtomToId(atom));
  }
  props.infallibleAppend(
      PropertyKey::Symbol(cx->wellKnownSymbols().toStringTag));

  return true;
}

void ModuleNamespaceObject::ProxyHandler::trace(JSTracer* trc,
                                                JSObject* proxy) const {
  auto& self = proxy->as<ModuleNamespaceObject>();

  if (self.hasExports()) {
    self.mutableExports().trace(trc);
  }

  if (self.hasBindings()) {
    self.bindings().trace(trc);
  }
}

void ModuleNamespaceObject::ProxyHandler::finalize(JS::GCContext* gcx,
                                                   JSObject* proxy) const {
  auto& self = proxy->as<ModuleNamespaceObject>();

  if (self.hasExports()) {
    gcx->delete_(proxy, &self.mutableExports(), MemoryUse::ModuleExports);
  }

  if (self.hasBindings()) {
    gcx->delete_(proxy, &self.bindings(), MemoryUse::ModuleBindingMap);
  }
}

// https://tc39.es/ecma262/#sec-IncrementModuleAsyncEvaluationCount
// 9.6.3 IncrementModuleAsyncEvaluationCount()
static uint32_t IncrementModuleAsyncEvaluationCount(JSRuntime* rt) {
  if (rt->pendingAsyncModuleEvaluations == 0) {
    // From the spec NOTE:
    //   An implementation may unobservably reset [[ModuleAsyncEvaluationCount]]
    //   to 0 whenever there are no pending modules.
    rt->moduleAsyncEvaluatingPostOrder = 0;
  }

  uint32_t ordinal = rt->moduleAsyncEvaluatingPostOrder;
  MOZ_ASSERT(ordinal != ASYNC_EVALUATING_POST_ORDER_DONE);
  MOZ_ASSERT(ordinal != ASYNC_EVALUATING_POST_ORDER_UNSET);
  MOZ_ASSERT(ordinal < ASYNC_EVALUATING_POST_ORDER_MAX_VALUE);
  rt->moduleAsyncEvaluatingPostOrder++;

  MOZ_ASSERT(rt->pendingAsyncModuleEvaluations < MAX_UINT32);
  rt->pendingAsyncModuleEvaluations++;

  return ordinal;
}

bool AsyncEvaluationOrder::isUnset() const {
  return value == ASYNC_EVALUATING_POST_ORDER_UNSET;
}

bool AsyncEvaluationOrder::isDone() const {
  return value == ASYNC_EVALUATING_POST_ORDER_DONE;
}

bool AsyncEvaluationOrder::isInteger() const {
  return value <= ASYNC_EVALUATING_POST_ORDER_MAX_VALUE;
}

uint32_t AsyncEvaluationOrder::get() const {
  MOZ_ASSERT(isInteger());
  return value;
}

void AsyncEvaluationOrder::set(JSRuntime* rt) {
  MOZ_ASSERT(isUnset());
  value = IncrementModuleAsyncEvaluationCount(rt);
}

void AsyncEvaluationOrder::setDone(JSRuntime* rt) {
  MOZ_ASSERT(isInteger());
  MOZ_ASSERT(rt->pendingAsyncModuleEvaluations > 0);
  rt->pendingAsyncModuleEvaluations--;
  value = ASYNC_EVALUATING_POST_ORDER_DONE;
}

#ifdef ENABLE_SOURCE_PHASE_IMPORTS
///////////////////////////////////////////////////////////////////////////
// AbstractModuleSourceObject

// https://tc39.es/proposal-source-phase-imports/#sec-%abstractmodulesource%-constructor
static bool AbstractModuleSourceConstructor(JSContext* cx, unsigned argc,
                                            Value* vp) {
  JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                            JSMSG_ABSTRACT_MODULE_SOURCE_CTOR);
  return false;
}

// https://tc39.es/proposal-source-phase-imports/#sec-get-%abstractmodulesource%.prototype.@@tostringtag
static bool AbstractModuleSource_toStringTagGetter(JSContext* cx, unsigned argc,
                                                   Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // Step 2. If O is not an Object, return undefined.
  if (!args.thisv().isObject()) {
    args.rval().setUndefined();
    return true;
  }

  // Step 1. Let O be the this value.
  JSObject* obj = &args.thisv().toObject();

  // Step 3. Let module be HostGetModuleSourceModuleRecord(O).
  // Step 4. If module is not-a-source, return undefined.
  // NOTE: All current modules are `not-a-source`, with the
  // exception of the `<module source>` module used in test262,
  // which is an instance of <ModuleSourceObject>.
  if (!obj->is<ModuleSourceObject>()) {
    args.rval().setUndefined();
    return true;
  }

  MOZ_ASSERT(
      JS::Prefs::experimental_source_phase_imports_test262_module_source());

  // Step 5. Let name be module.GetModuleSourceKind().
  JSAtom* name = cx->names().Module;

  // Step 6. Assert: name is a String.
  // (not applicable in our implementation)

  // Step 7. Return name.
  args.rval().setString(name);
  return true;
}

static const JSPropertySpec abstract_module_source_proto_accessors[] = {
    JS_SYM_GET(toStringTag, AbstractModuleSource_toStringTagGetter, 0),
    JS_PS_END,
};

static JSObject* CreateAbstractModuleSourcePrototype(JSContext* cx,
                                                     JSProtoKey key) {
  return GlobalObject::createBlankPrototype(
      cx, cx->global(), &AbstractModuleSourceObject::class_);
}

static const ClassSpec AbstractModuleSourceObjectClassSpec = {
    GenericCreateConstructor<AbstractModuleSourceConstructor, 0,
                             gc::AllocKind::FUNCTION>,
    CreateAbstractModuleSourcePrototype,
    nullptr,
    nullptr,
    nullptr,
    abstract_module_source_proto_accessors,
    nullptr,
    ClassSpec::DontDefineConstructor,
};

/* static */ const JSClass AbstractModuleSourceObject::class_ = {
    "AbstractModuleSource",
    JSCLASS_HAS_CACHED_PROTO(JSProto_AbstractModuleSource),
    JS_NULL_CLASS_OPS,
    &AbstractModuleSourceObjectClassSpec,
};

///////////////////////////////////////////////////////////////////////////
// ModuleSourceObject

/* static */ const JSClass ModuleSourceObject::class_ = {
    "ModuleSource",
};

/* static */
bool ModuleSourceObject::isInstance(HandleValue value) {
  return value.isObject() && value.toObject().is<ModuleSourceObject>();
}

/* static */
ModuleSourceObject* ModuleSourceObject::create(JSContext* cx) {
  RootedObject proto(
      cx, GlobalObject::getOrCreatePrototype(cx, JSProto_AbstractModuleSource));
  if (!proto) {
    return nullptr;
  }
  return NewObjectWithGivenProto<ModuleSourceObject>(cx, proto);
}
#endif

///////////////////////////////////////////////////////////////////////////
// SyntheticModuleFields

// The fields of a synthetic module record, as described in:
// https://tc39.es/proposal-json-modules/#sec-synthetic-module-records
class js::SyntheticModuleFields {
 public:
  ExportNameVector exportNames;

 public:
  void trace(JSTracer* trc);
};

void SyntheticModuleFields::trace(JSTracer* trc) { exportNames.trace(trc); }

///////////////////////////////////////////////////////////////////////////
// CyclicModuleFields

// The fields of a cyclic module record, as described in:
// https://tc39.es/ecma262/#sec-cyclic-module-records
class js::CyclicModuleFields {
 public:
  ModuleStatus status = ModuleStatus::New;

  bool hasTopLevelAwait : 1;

 private:
  // Flag bits that determine whether other fields are present.
  bool hasDfsAncestorIndex : 1;
  bool hasPendingAsyncDependencies : 1;

  // Fields whose presence is conditional on the flag bits above.
  uint32_t dfsAncestorIndex = 0;
  uint32_t pendingAsyncDependencies = 0;

  // Fields describing the layout of exportEntries.
  uint32_t indirectExportEntriesStart = 0;
  uint32_t starExportEntriesStart = 0;

 public:
  HeapPtr<Value> evaluationError;
  HeapPtr<JSObject*> metaObject;
  HeapPtr<ScriptSourceObject*> scriptSourceObject;
  RequestedModuleVector requestedModules;
  LoadedModuleMap loadedModules;
  ImportEntryVector importEntries;
  ExportEntryVector exportEntries;
  IndirectBindingMap importBindings;
  UniquePtr<FunctionDeclarationVector> functionDeclarations;
  AsyncEvaluationOrder asyncEvaluationOrder;
  HeapPtr<PromiseObject*> topLevelCapability;
  HeapPtr<ListObject*> asyncParentModules;
  HeapPtr<ModuleObject*> cycleRoot;

 public:
  CyclicModuleFields();

  void trace(JSTracer* trc);

  void initExportEntries(MutableHandle<ExportEntryVector> allEntries,
                         uint32_t localExportCount,
                         uint32_t indirectExportCount,
                         uint32_t starExportCount);
  Span<const ExportEntry> localExportEntries() const;
  Span<const ExportEntry> indirectExportEntries() const;
  Span<const ExportEntry> starExportEntries() const;

  void setDfsAncestorIndex(uint32_t index);
  Maybe<uint32_t> maybeDfsAncestorIndex() const;
  void clearDfsAncestorIndex();

  void setPendingAsyncDependencies(uint32_t newValue);
  Maybe<uint32_t> maybePendingAsyncDependencies() const;
};

CyclicModuleFields::CyclicModuleFields()
    : hasTopLevelAwait(false),
      hasDfsAncestorIndex(false),
      hasPendingAsyncDependencies(false) {}

void CyclicModuleFields::trace(JSTracer* trc) {
  TraceEdge(trc, &evaluationError, "CyclicModuleFields::evaluationError");
  TraceEdge(trc, &metaObject, "CyclicModuleFields::metaObject");
  TraceEdge(trc, &scriptSourceObject, "CyclicModuleFields::scriptSourceObject");
  requestedModules.trace(trc);
  loadedModules.trace(trc);
  importEntries.trace(trc);
  exportEntries.trace(trc);
  importBindings.trace(trc);
  TraceEdge(trc, &topLevelCapability, "CyclicModuleFields::topLevelCapability");
  TraceEdge(trc, &asyncParentModules, "CyclicModuleFields::asyncParentModules");
  TraceEdge(trc, &cycleRoot, "CyclicModuleFields::cycleRoot");
}

void CyclicModuleFields::initExportEntries(
    MutableHandle<ExportEntryVector> allEntries, uint32_t localExportCount,
    uint32_t indirectExportCount, uint32_t starExportCount) {
  MOZ_ASSERT(allEntries.length() ==
             localExportCount + indirectExportCount + starExportCount);

  exportEntries = std::move(allEntries.get());
  indirectExportEntriesStart = localExportCount;
  starExportEntriesStart = indirectExportEntriesStart + indirectExportCount;
}

Span<const ExportEntry> CyclicModuleFields::localExportEntries() const {
  MOZ_ASSERT(indirectExportEntriesStart <= exportEntries.length());
  return Span(exportEntries.begin(),
              exportEntries.begin() + indirectExportEntriesStart);
}

Span<const ExportEntry> CyclicModuleFields::indirectExportEntries() const {
  MOZ_ASSERT(indirectExportEntriesStart <= starExportEntriesStart);
  MOZ_ASSERT(starExportEntriesStart <= exportEntries.length());
  return Span(exportEntries.begin() + indirectExportEntriesStart,
              exportEntries.begin() + starExportEntriesStart);
}

Span<const ExportEntry> CyclicModuleFields::starExportEntries() const {
  MOZ_ASSERT(starExportEntriesStart <= exportEntries.length());
  return Span(exportEntries.begin() + starExportEntriesStart,
              exportEntries.end());
}

void CyclicModuleFields::setDfsAncestorIndex(uint32_t index) {
  dfsAncestorIndex = index;
  hasDfsAncestorIndex = true;
}

Maybe<uint32_t> CyclicModuleFields::maybeDfsAncestorIndex() const {
  return hasDfsAncestorIndex ? Some(dfsAncestorIndex) : Nothing();
}

void CyclicModuleFields::clearDfsAncestorIndex() {
  dfsAncestorIndex = 0;
  hasDfsAncestorIndex = false;
}

void CyclicModuleFields::setPendingAsyncDependencies(uint32_t newValue) {
  pendingAsyncDependencies = newValue;
  hasPendingAsyncDependencies = true;
}

Maybe<uint32_t> CyclicModuleFields::maybePendingAsyncDependencies() const {
  return hasPendingAsyncDependencies ? Some(pendingAsyncDependencies)
                                     : Nothing();
}

///////////////////////////////////////////////////////////////////////////
// ModuleObject

/* static */ const JSClassOps ModuleObject::classOps_ = {
    nullptr,                 // addProperty
    nullptr,                 // delProperty
    nullptr,                 // enumerate
    nullptr,                 // newEnumerate
    nullptr,                 // resolve
    nullptr,                 // mayResolve
    ModuleObject::finalize,  // finalize
    nullptr,                 // call
    nullptr,                 // construct
    ModuleObject::trace,     // trace
};

/* static */ const JSClass ModuleObject::class_ = {
    "Module",
    JSCLASS_HAS_RESERVED_SLOTS(ModuleObject::SlotCount) |
        JSCLASS_BACKGROUND_FINALIZE,
    &ModuleObject::classOps_,
};

/* static */
bool ModuleObject::isInstance(HandleValue value) {
  return value.isObject() && value.toObject().is<ModuleObject>();
}

bool ModuleObject::hasCyclicModuleFields() const {
  bool result = !getReservedSlot(CyclicModuleFieldsSlot).isUndefined();
  MOZ_ASSERT_IF(result, !hasSyntheticModuleFields());
  return result;
}

CyclicModuleFields* ModuleObject::cyclicModuleFields() {
  MOZ_ASSERT(hasCyclicModuleFields());
  void* ptr = getReservedSlot(CyclicModuleFieldsSlot).toPrivate();
  MOZ_ASSERT(ptr);
  return static_cast<CyclicModuleFields*>(ptr);
}
const CyclicModuleFields* ModuleObject::cyclicModuleFields() const {
  return const_cast<ModuleObject*>(this)->cyclicModuleFields();
}

Span<const RequestedModule> ModuleObject::requestedModules() const {
  return cyclicModuleFields()->requestedModules;
}

Span<const ImportEntry> ModuleObject::importEntries() const {
  return cyclicModuleFields()->importEntries;
}

Span<const ExportEntry> ModuleObject::localExportEntries() const {
  return cyclicModuleFields()->localExportEntries();
}

Span<const ExportEntry> ModuleObject::indirectExportEntries() const {
  return cyclicModuleFields()->indirectExportEntries();
}

Span<const ExportEntry> ModuleObject::starExportEntries() const {
  return cyclicModuleFields()->starExportEntries();
}

const ExportNameVector& ModuleObject::syntheticExportNames() const {
  return syntheticModuleFields()->exportNames;
}

void ModuleObject::initFunctionDeclarations(
    UniquePtr<FunctionDeclarationVector> decls) {
  cyclicModuleFields()->functionDeclarations = std::move(decls);
}

/* static */
ModuleObject* ModuleObject::create(JSContext* cx) {
  Rooted<UniquePtr<CyclicModuleFields>> fields(cx);
  fields = cx->make_unique<CyclicModuleFields>();
  if (!fields) {
    return nullptr;
  }

  ModuleObject* self = NewObjectWithGivenProto<ModuleObject>(cx, nullptr);
  if (!self) {
    return nullptr;
  }

  InitReservedSlot(self, CyclicModuleFieldsSlot, fields.release(),
                   MemoryUse::ModuleCyclicFields);

  return self;
}

/* static */
ModuleObject* ModuleObject::createSynthetic(
    JSContext* cx, MutableHandle<ExportNameVector> exportNames) {
  Rooted<UniquePtr<SyntheticModuleFields>> syntheticFields(cx);
  syntheticFields = cx->make_unique<SyntheticModuleFields>();
  if (!syntheticFields) {
    return nullptr;
  }

  ModuleObject* self = NewObjectWithGivenProto<ModuleObject>(cx, nullptr);
  if (!self) {
    return nullptr;
  }

  InitReservedSlot(self, SyntheticModuleFieldsSlot, syntheticFields.release(),
                   MemoryUse::ModuleSyntheticFields);

  self->syntheticModuleFields()->exportNames = std::move(exportNames.get());

  return self;
}

/* static */
void ModuleObject::finalize(JS::GCContext* gcx, JSObject* obj) {
  ModuleObject* self = &obj->as<ModuleObject>();
  if (self->hasCyclicModuleFields()) {
    gcx->delete_(obj, self->cyclicModuleFields(),
                 MemoryUse::ModuleCyclicFields);
  }
  if (self->hasSyntheticModuleFields()) {
    gcx->delete_(obj, self->syntheticModuleFields(),
                 MemoryUse::ModuleSyntheticFields);
  }
}

ModuleEnvironmentObject& ModuleObject::initialEnvironment() const {
  Value value = getReservedSlot(EnvironmentSlot);
  return value.toObject().as<ModuleEnvironmentObject>();
}

ModuleEnvironmentObject* ModuleObject::environment() const {
  // Note that this it's valid to call this even if there was an error
  // evaluating the module.

  // According to the spec the environment record is created during linking, but
  // we create it earlier than that.
  if (status() < ModuleStatus::Linked) {
    return nullptr;
  }

  return &initialEnvironment();
}

IndirectBindingMap& ModuleObject::importBindings() {
  return cyclicModuleFields()->importBindings;
}

ModuleNamespaceObject* ModuleObject::namespace_() {
  Value value = getReservedSlot(NamespaceSlot);
  if (value.isUndefined()) {
    return nullptr;
  }
  return &value.toObject().as<ModuleNamespaceObject>();
}

ScriptSourceObject* ModuleObject::scriptSourceObject() const {
  return cyclicModuleFields()->scriptSourceObject;
}

#ifdef ENABLE_SOURCE_PHASE_IMPORTS
ModuleSourceObject* ModuleObject::moduleSource() const {
  Value value = getReservedSlot(ModuleSourceSlot);
  if (value.isUndefined()) {
    return nullptr;
  }
  return &value.toObject().as<ModuleSourceObject>();
}
#endif

void ModuleObject::initAsyncSlots(JSContext* cx, bool hasTopLevelAwait,
                                  Handle<ListObject*> asyncParentModules) {
  cyclicModuleFields()->hasTopLevelAwait = hasTopLevelAwait;
  cyclicModuleFields()->asyncParentModules = asyncParentModules;
}

void ModuleObject::initScriptSlots(HandleScript script) {
  MOZ_ASSERT(script);
  MOZ_ASSERT(script->sourceObject());
  MOZ_ASSERT(script->filename());
  initReservedSlot(ScriptSlot, PrivateGCThingValue(script));
  cyclicModuleFields()->scriptSourceObject = script->sourceObject();
}

#ifdef ENABLE_SOURCE_PHASE_IMPORTS
void ModuleObject::initModuleSourceSlot(
    Handle<ModuleSourceObject*> moduleSource) {
  initReservedSlot(ModuleSourceSlot, ObjectValue(*moduleSource));
}
#endif

void ModuleObject::setInitialEnvironment(
    Handle<ModuleEnvironmentObject*> initialEnvironment) {
  initReservedSlot(EnvironmentSlot, ObjectValue(*initialEnvironment));
}

void ModuleObject::initImportExportData(
    MutableHandle<RequestedModuleVector> requestedModules,
    MutableHandle<ImportEntryVector> importEntries,
    MutableHandle<ExportEntryVector> exportEntries, uint32_t localExportCount,
    uint32_t indirectExportCount, uint32_t starExportCount) {
  cyclicModuleFields()->requestedModules = std::move(requestedModules.get());
  cyclicModuleFields()->importEntries = std::move(importEntries.get());
  cyclicModuleFields()->initExportEntries(exportEntries, localExportCount,
                                          indirectExportCount, starExportCount);
}

/* static */
bool ModuleObject::Freeze(JSContext* cx, Handle<ModuleObject*> self) {
  return FreezeObject(cx, self);
}

#ifdef DEBUG
/* static */ inline bool ModuleObject::AssertFrozen(
    JSContext* cx, Handle<ModuleObject*> self) {
  bool frozen = false;
  if (!TestIntegrityLevel(cx, self, IntegrityLevel::Frozen, &frozen)) {
    return false;
  }
  MOZ_ASSERT(frozen);

  return true;
}
#endif

JSScript* ModuleObject::maybeScript() const {
  Value value = getReservedSlot(ScriptSlot);
  if (value.isUndefined()) {
    return nullptr;
  }
  BaseScript* script = value.toGCThing()->as<BaseScript>();
  MOZ_ASSERT(script->hasBytecode(),
             "Module scripts should always have bytecode");
  return script->asJSScript();
}

JSScript* ModuleObject::script() const {
  JSScript* ptr = maybeScript();
  MOZ_RELEASE_ASSERT(ptr);
  return ptr;
}

const char* ModuleObject::filename() const {
  // The ScriptSlot will be cleared once the module is evaluated, so we try to
  // get the filename from cyclicModuleFields().

  // TODO: Bug 1885483: Provide filename for JSON modules
  if (!hasCyclicModuleFields()) {
    return "(JSON module)";
  }
  return cyclicModuleFields()->scriptSourceObject->source()->filename();
}

static inline void AssertValidModuleStatus(ModuleStatus status) {
  MOZ_ASSERT(status >= ModuleStatus::New &&
             status <= ModuleStatus::Evaluated_Error);
}

ModuleStatus ModuleObject::status() const {
  // Always return `ModuleStatus::Evaluated` so we can assert a module's status
  // without checking which kind it is, even though synthetic modules don't have
  // this field according to the spec.
  if (hasSyntheticModuleFields()) {
    return ModuleStatus::Evaluated;
  }

  ModuleStatus status = cyclicModuleFields()->status;
  AssertValidModuleStatus(status);

  if (status == ModuleStatus::Evaluated_Error) {
    return ModuleStatus::Evaluated;
  }

  return status;
}

void ModuleObject::setStatus(ModuleStatus newStatus) {
  AssertValidModuleStatus(newStatus);

  // Note that under OOM conditions we can fail the module linking process even
  // after modules have been marked as linked.
  MOZ_ASSERT((status() <= ModuleStatus::Linked &&
              (newStatus == ModuleStatus::Unlinked ||
               newStatus == ModuleStatus::New)) ||
                 newStatus > status(),
             "New module status inconsistent with current status");

  cyclicModuleFields()->status = newStatus;
}

bool ModuleObject::hasTopLevelAwait() const {
  return cyclicModuleFields()->hasTopLevelAwait;
}

AsyncEvaluationOrder& ModuleObject::asyncEvaluationOrder() {
  return cyclicModuleFields()->asyncEvaluationOrder;
}

AsyncEvaluationOrder const& ModuleObject::asyncEvaluationOrder() const {
  return cyclicModuleFields()->asyncEvaluationOrder;
}

Maybe<uint32_t> ModuleObject::maybeDfsAncestorIndex() const {
  return cyclicModuleFields()->maybeDfsAncestorIndex();
}

uint32_t ModuleObject::dfsAncestorIndex() const {
  return maybeDfsAncestorIndex().value();
}

void ModuleObject::setDfsAncestorIndex(uint32_t index) {
  cyclicModuleFields()->setDfsAncestorIndex(index);
}

void ModuleObject::clearDfsAncestorIndex() {
  cyclicModuleFields()->clearDfsAncestorIndex();
}

PromiseObject* ModuleObject::maybeTopLevelCapability() const {
  return cyclicModuleFields()->topLevelCapability;
}

PromiseObject* ModuleObject::topLevelCapability() const {
  PromiseObject* capability = maybeTopLevelCapability();
  MOZ_RELEASE_ASSERT(capability);
  return capability;
}

// static
PromiseObject* ModuleObject::createTopLevelCapability(
    JSContext* cx, Handle<ModuleObject*> module) {
  MOZ_ASSERT(!module->maybeTopLevelCapability());

  Rooted<PromiseObject*> resultPromise(cx, CreatePromiseObjectForAsync(cx));
  if (!resultPromise) {
    return nullptr;
  }

  module->setInitialTopLevelCapability(resultPromise);
  return resultPromise;
}

void ModuleObject::setInitialTopLevelCapability(
    Handle<PromiseObject*> capability) {
  cyclicModuleFields()->topLevelCapability = capability;
}

ListObject* ModuleObject::asyncParentModules() const {
  return cyclicModuleFields()->asyncParentModules;
}

bool ModuleObject::appendAsyncParentModule(JSContext* cx,
                                           Handle<ModuleObject*> self,
                                           Handle<ModuleObject*> parent) {
  Rooted<Value> parentValue(cx, ObjectValue(*parent));
  return self->asyncParentModules()->append(cx, parentValue);
}

Maybe<uint32_t> ModuleObject::maybePendingAsyncDependencies() const {
  return cyclicModuleFields()->maybePendingAsyncDependencies();
}

uint32_t ModuleObject::pendingAsyncDependencies() const {
  return maybePendingAsyncDependencies().value();
}

void ModuleObject::setPendingAsyncDependencies(uint32_t newValue) {
  cyclicModuleFields()->setPendingAsyncDependencies(newValue);
}

void ModuleObject::setCycleRoot(ModuleObject* cycleRoot) {
  cyclicModuleFields()->cycleRoot = cycleRoot;
}

ModuleObject* ModuleObject::getCycleRoot() const {
  MOZ_RELEASE_ASSERT(cyclicModuleFields()->cycleRoot);
  return cyclicModuleFields()->cycleRoot;
}

bool ModuleObject::hasCycleRoot() const {
  return bool(cyclicModuleFields()->cycleRoot);
}

LoadedModuleMap& ModuleObject::loadedModules() {
  return cyclicModuleFields()->loadedModules;
}

const LoadedModuleMap& ModuleObject::loadedModules() const {
  return cyclicModuleFields()->loadedModules;
}

bool ModuleObject::hasSyntheticModuleFields() const {
  bool result = !getReservedSlot(SyntheticModuleFieldsSlot).isUndefined();
  MOZ_ASSERT_IF(result, !hasCyclicModuleFields());
  return result;
}

SyntheticModuleFields* ModuleObject::syntheticModuleFields() {
  MOZ_ASSERT(!hasCyclicModuleFields());
  void* ptr = getReservedSlot(SyntheticModuleFieldsSlot).toPrivate();
  MOZ_ASSERT(ptr);
  return static_cast<SyntheticModuleFields*>(ptr);
}
const SyntheticModuleFields* ModuleObject::syntheticModuleFields() const {
  return const_cast<ModuleObject*>(this)->syntheticModuleFields();
}

bool ModuleObject::hasTopLevelCapability() const {
  return cyclicModuleFields()->topLevelCapability;
}

bool ModuleObject::hadEvaluationError() const {
  if (hasSyntheticModuleFields()) {
    return false;
  }

  ModuleStatus fullStatus = cyclicModuleFields()->status;
  return fullStatus == ModuleStatus::Evaluated_Error;
}

void ModuleObject::setEvaluationError(HandleValue newValue) {
  MOZ_ASSERT(status() != ModuleStatus::Unlinked &&
             status() != ModuleStatus::New);
  MOZ_ASSERT(!hadEvaluationError());

  cyclicModuleFields()->status = ModuleStatus::Evaluated_Error;
  cyclicModuleFields()->evaluationError = newValue;

  MOZ_ASSERT(status() == ModuleStatus::Evaluated);
  MOZ_ASSERT(hadEvaluationError());
}

Value ModuleObject::maybeEvaluationError() const {
  return cyclicModuleFields()->evaluationError;
}

Value ModuleObject::evaluationError() const {
  MOZ_ASSERT(hadEvaluationError());
  return maybeEvaluationError();
}

JSObject* ModuleObject::metaObject() const {
  return cyclicModuleFields()->metaObject;
}

void ModuleObject::setMetaObject(JSObject* obj) {
  MOZ_ASSERT(obj);
  MOZ_ASSERT(!metaObject());
  cyclicModuleFields()->metaObject = obj;
}

#ifdef DEBUG
void ModuleObject::setPreload(bool isPreload) {
  setReservedSlot(PreloadSlot, BooleanValue(isPreload));
}

bool ModuleObject::isPreload() const {
  return getReservedSlot(PreloadSlot).toBoolean();
}
#endif

/* static */
void ModuleObject::trace(JSTracer* trc, JSObject* obj) {
  ModuleObject& module = obj->as<ModuleObject>();
  if (module.hasCyclicModuleFields()) {
    module.cyclicModuleFields()->trace(trc);
  }
  if (module.hasSyntheticModuleFields()) {
    module.syntheticModuleFields()->trace(trc);
  }
}

/* static */
bool ModuleObject::instantiateFunctionDeclarations(JSContext* cx,
                                                   Handle<ModuleObject*> self) {
#ifdef DEBUG
  MOZ_ASSERT(self->status() == ModuleStatus::Linking);
  if (!AssertFrozen(cx, self)) {
    return false;
  }
#endif
  // |self| initially manages this vector.
  UniquePtr<FunctionDeclarationVector>& funDecls =
      self->cyclicModuleFields()->functionDeclarations;
  if (!funDecls) {
    JS_ReportErrorASCII(
        cx, "Module function declarations have already been instantiated");
    return false;
  }

  Rooted<ModuleEnvironmentObject*> env(cx, &self->initialEnvironment());
  RootedObject obj(cx);
  RootedValue value(cx);
  RootedFunction fun(cx);
  Rooted<PropertyName*> name(cx);

  for (GCThingIndex funIndex : *funDecls) {
    fun.set(self->script()->getFunction(funIndex));
    obj = Lambda(cx, fun, env);
    if (!obj) {
      return false;
    }

    name = fun->fullExplicitName()->asPropertyName();
    value = ObjectValue(*obj);
    if (!SetProperty(cx, env, name, value)) {
      return false;
    }
  }

  // Free the vector, now its contents are no longer needed.
  funDecls.reset();

  return true;
}

/* static */
bool ModuleObject::execute(JSContext* cx, Handle<ModuleObject*> self) {
#ifdef DEBUG
  MOZ_ASSERT(self->status() == ModuleStatus::Evaluating ||
             self->status() == ModuleStatus::EvaluatingAsync ||
             self->status() == ModuleStatus::Evaluated);
  MOZ_ASSERT(!self->hadEvaluationError());
  if (!AssertFrozen(cx, self)) {
    return false;
  }
#endif

  RootedScript script(cx, self->script());

  auto guardA = mozilla::MakeScopeExit([&] {
    if (self->hasTopLevelAwait()) {
      // Handled in AsyncModuleExecutionFulfilled and
      // AsyncModuleExecutionRejected.
      return;
    }
    ModuleObject::onTopLevelEvaluationFinished(self);
  });

  Rooted<ModuleEnvironmentObject*> env(cx, self->environment());
  if (!env) {
    JS_ReportErrorASCII(cx,
                        "Module declarations have not yet been instantiated");
    return false;
  }

  Rooted<Value> ignored(cx);
  return Execute(cx, script, env, &ignored);
}

/* static */
void ModuleObject::onTopLevelEvaluationFinished(ModuleObject* module) {
  // ScriptSlot is used by debugger to access environments during evaluating
  // the top-level script.
  // Clear the reference at exit to prevent us keeping this alive unnecessarily.
  module->setReservedSlot(ScriptSlot, UndefinedValue());
}

/* static */
ModuleNamespaceObject* ModuleObject::createNamespace(
    JSContext* cx, Handle<ModuleObject*> self,
    MutableHandle<UniquePtr<ExportNameVector>> exports) {
  MOZ_ASSERT(!self->namespace_());

  Rooted<UniquePtr<IndirectBindingMap>> bindings(cx);
  bindings = cx->make_unique<IndirectBindingMap>();
  if (!bindings) {
    return nullptr;
  }

  auto* ns = ModuleNamespaceObject::create(cx, self, exports, &bindings);
  if (!ns) {
    return nullptr;
  }

  self->initReservedSlot(NamespaceSlot, ObjectValue(*ns));
  return ns;
}

/* static */
bool ModuleObject::createEnvironment(JSContext* cx,
                                     Handle<ModuleObject*> self) {
  Rooted<ModuleEnvironmentObject*> env(
      cx, ModuleEnvironmentObject::create(cx, self));
  if (!env) {
    return false;
  }

  self->setInitialEnvironment(env);
  return true;
}

/*static*/
bool ModuleObject::createSyntheticEnvironment(JSContext* cx,
                                              Handle<ModuleObject*> self,
                                              JS::HandleVector<Value> values) {
  Rooted<ModuleEnvironmentObject*> env(
      cx, ModuleEnvironmentObject::createSynthetic(cx, self));
  if (!env) {
    return false;
  }

  // We expect one property per synthetic value plus one for the *namespace*
  // binding.
  MOZ_ASSERT(env->shape()->propMapLength() == values.length() + 1);

  for (uint32_t i = 0; i < values.length(); i++) {
    env->setAliasedBinding(env->firstSyntheticValueSlot() + i, values[i]);
  }

  self->setInitialEnvironment(env);

  return true;
}

///////////////////////////////////////////////////////////////////////////
// GraphLoadingStateRecordObject

GraphLoadingStateRecord::GraphLoadingStateRecord(
    JS::LoadModuleResolvedCallback resolved,
    JS::LoadModuleRejectedCallback rejected)
    : resolved(resolved), rejected(rejected) {}

void GraphLoadingStateRecord::trace(JSTracer* trc) { visited.trace(trc); }

/* static */
const JSClass GraphLoadingStateRecordObject::class_ = {
    "GraphLoadingStateRecordObject",
    JSCLASS_HAS_RESERVED_SLOTS(GraphLoadingStateRecordObject::SlotCount) |
        JSCLASS_BACKGROUND_FINALIZE,
    &GraphLoadingStateRecordObject::classOps_,
};
static_assert(GraphLoadingStateRecordObject::StateSlot == 0);

/* static */
const JSClassOps GraphLoadingStateRecordObject::classOps_ = {
    nullptr,                                  // addProperty
    nullptr,                                  // delProperty
    nullptr,                                  // enumerate
    nullptr,                                  // newEnumerate
    nullptr,                                  // resolve
    nullptr,                                  // mayResolve
    GraphLoadingStateRecordObject::finalize,  // finalize
    nullptr,                                  // call
    nullptr,                                  // construct
    GraphLoadingStateRecordObject::trace,     // trace
};

/* static */
GraphLoadingStateRecordObject* GraphLoadingStateRecordObject::create(
    JSContext* cx, bool isLoading, uint32_t pendingModulesCount,
    JS::LoadModuleResolvedCallback resolved,
    JS::LoadModuleRejectedCallback rejected, Handle<Value> hostDefined) {
  Rooted<GraphLoadingStateRecordObject*> self(
      cx, NewObjectWithGivenProto<GraphLoadingStateRecordObject>(cx, nullptr));
  if (!self) {
    return nullptr;
  }

  auto* state = cx->new_<GraphLoadingStateRecord>(resolved, rejected);
  if (!state) {
    ReportOutOfMemory(cx);
    return nullptr;
  }

  InitReservedSlot(self, StateSlot, state, MemoryUse::GraphLoadingStateRecord);
  self->initReservedSlot(IsLoadingSlot, Int32Value(isLoading));
  self->initReservedSlot(PendingModulesCountSlot,
                         Int32Value(pendingModulesCount));
  self->initReservedSlot(HostDefinedSlot, hostDefined);
  return self;
}

/* static */
GraphLoadingStateRecordObject* GraphLoadingStateRecordObject::create(
    JSContext* cx, bool isLoading, uint32_t pendingModulesCount,
    Handle<PromiseObject*> promise, Handle<Value> hostDefined) {
  Rooted<GraphLoadingStateRecordObject*> self(
      cx, NewObjectWithGivenProto<GraphLoadingStateRecordObject>(cx, nullptr));
  if (!self) {
    return nullptr;
  }

  auto* state = cx->new_<GraphLoadingStateRecord>();
  if (!state) {
    ReportOutOfMemory(cx);
    return nullptr;
  }

  InitReservedSlot(self, StateSlot, state, MemoryUse::GraphLoadingStateRecord);
  self->initReservedSlot(PromiseSlot, ObjectValue(*promise));
  self->initReservedSlot(IsLoadingSlot, Int32Value(isLoading));
  self->initReservedSlot(PendingModulesCountSlot,
                         Int32Value(pendingModulesCount));
  self->initReservedSlot(HostDefinedSlot, hostDefined);
  return self;
}

VisitedModuleSet& GraphLoadingStateRecordObject::visited() {
  GraphLoadingStateRecord* state = static_cast<GraphLoadingStateRecord*>(
      getReservedSlot(StateSlot).toPrivate());
  MOZ_ASSERT(state);
  return state->visited;
}

PromiseObject* GraphLoadingStateRecordObject::promise() {
  if (getReservedSlot(PromiseSlot).isUndefined()) {
    return nullptr;
  }
  return &getReservedSlot(PromiseSlot).toObject().as<PromiseObject>();
}

bool GraphLoadingStateRecordObject::isLoading() {
  return getReservedSlot(IsLoadingSlot).toInt32();
}

void GraphLoadingStateRecordObject::setIsLoading(bool isLoading) {
  setReservedSlot(IsLoadingSlot, Int32Value(isLoading));
}

uint32_t GraphLoadingStateRecordObject::pendingModulesCount() {
  return getReservedSlot(PendingModulesCountSlot).toInt32();
}

void GraphLoadingStateRecordObject::setPendingModulesCount(uint32_t count) {
  setReservedSlot(PendingModulesCountSlot, Int32Value(count));
}

Value GraphLoadingStateRecordObject::hostDefined() {
  return getReservedSlot(HostDefinedSlot);
}

bool GraphLoadingStateRecordObject::resolved(
    JSContext* cx, JS::Handle<JS::Value> hostDefined) {
  if (promise()) {
    Rooted<PromiseObject*> promiseObj(cx, promise());
    return AsyncFunctionReturned(cx, promiseObj, UndefinedHandleValue);
  }

  GraphLoadingStateRecord* state = static_cast<GraphLoadingStateRecord*>(
      getReservedSlot(StateSlot).toPrivate());
  MOZ_ASSERT(state);
  MOZ_ASSERT(state->resolved);
  return state->resolved(cx, hostDefined);
}

bool GraphLoadingStateRecordObject::rejected(JSContext* cx,
                                             JS::Handle<JS::Value> hostDefined,
                                             Handle<JS::Value> error) {
  if (promise()) {
    Rooted<PromiseObject*> promiseObj(cx, promise());
    return AsyncFunctionThrown(cx, promiseObj, error);
  }

  GraphLoadingStateRecord* state = static_cast<GraphLoadingStateRecord*>(
      getReservedSlot(StateSlot).toPrivate());
  MOZ_ASSERT(state);
  MOZ_ASSERT(state->rejected);
  return state->rejected(cx, hostDefined, error);
}

/* static */
void GraphLoadingStateRecordObject::finalize(JS::GCContext* gcx,
                                             JSObject* obj) {
  auto* self = &obj->as<GraphLoadingStateRecordObject>();
  Value stateValue = self->getReservedSlot(StateSlot);
  if (!stateValue.isUndefined()) {
    auto* state = static_cast<GraphLoadingStateRecord*>(stateValue.toPrivate());
    gcx->delete_(obj, state, MemoryUse::GraphLoadingStateRecord);
  }
}

/* static */
void GraphLoadingStateRecordObject::trace(JSTracer* trc, JSObject* obj) {
  GraphLoadingStateRecordObject* self =
      &obj->as<GraphLoadingStateRecordObject>();
  Value stateValue = self->getReservedSlot(StateSlot);
  if (!stateValue.isUndefined()) {
    GraphLoadingStateRecord* state =
        static_cast<GraphLoadingStateRecord*>(stateValue.toPrivate());
    state->trace(trc);
  }
}

///////////////////////////////////////////////////////////////////////////
// ModuleBuilder

ModuleBuilder::ModuleBuilder(FrontendContext* fc,
                             const frontend::EitherParser& eitherParser)
    : fc_(fc),
      eitherParser_(eitherParser),
      requestedModuleIndexes_(fc),
      importEntries_(fc),
      exportEntries_(fc),
      exportNames_(fc) {}

bool ModuleBuilder::noteFunctionDeclaration(FrontendContext* fc,
                                            uint32_t funIndex) {
  if (!functionDecls_.emplaceBack(funIndex)) {
    js::ReportOutOfMemory(fc);
    return false;
  }
  return true;
}

void ModuleBuilder::noteAsync(frontend::StencilModuleMetadata& metadata) {
  metadata.isAsync = true;
}

bool ModuleBuilder::buildTables(frontend::StencilModuleMetadata& metadata) {
  // https://tc39.es/ecma262/#sec-parsemodule
  // 15.2.1.17.1 ParseModule, Steps 4-11.

  // Step 4.
  metadata.moduleRequests = std::move(moduleRequests_);
  metadata.requestedModules = std::move(requestedModules_);

  // Step 5.
  if (!metadata.importEntries.reserve(importEntries_.count())) {
    js::ReportOutOfMemory(fc_);
    return false;
  }
  for (auto iter = importEntries_.iter(); !iter.done(); iter.next()) {
    frontend::StencilModuleEntry& entry = iter.get().value();
    metadata.importEntries.infallibleAppend(entry);
  }

  // Steps 6-11.
  for (const frontend::StencilModuleEntry& exp : exportEntries_) {
    if (!exp.moduleRequest) {
      frontend::StencilModuleEntry* importEntry = importEntryFor(exp.localName);
      if (!importEntry) {
        if (!metadata.localExportEntries.append(exp)) {
          js::ReportOutOfMemory(fc_);
          return false;
        }
      } else {
        // All names should have already been marked as used-by-stencil.
        if (!importEntry->importName) {
          // This is a re-export of an imported module namespace object.
          auto entry = frontend::StencilModuleEntry::exportNamespaceFromEntry(
              importEntry->moduleRequest, exp.exportName, exp.lineno,
              exp.column);
          if (!metadata.indirectExportEntries.append(entry)) {
            js::ReportOutOfMemory(fc_);
            return false;
          }
        } else {
          auto entry = frontend::StencilModuleEntry::exportFromEntry(
              importEntry->moduleRequest, importEntry->importName,
              exp.exportName, exp.lineno, exp.column);
          if (!metadata.indirectExportEntries.append(entry)) {
            js::ReportOutOfMemory(fc_);
            return false;
          }
        }
      }
    } else if (!exp.importName && !exp.exportName) {
      if (!metadata.starExportEntries.append(exp)) {
        js::ReportOutOfMemory(fc_);
        return false;
      }
    } else {
      if (!metadata.indirectExportEntries.append(exp)) {
        js::ReportOutOfMemory(fc_);
        return false;
      }
    }
  }

  return true;
}

void ModuleBuilder::finishFunctionDecls(
    frontend::StencilModuleMetadata& metadata) {
  metadata.functionDecls = std::move(functionDecls_);
}

bool frontend::StencilModuleMetadata::createModuleRequestObjects(
    JSContext* cx, CompilationAtomCache& atomCache,
    MutableHandle<ModuleRequestVector> output) const {
  if (!output.reserve(moduleRequests.length())) {
    ReportOutOfMemory(cx);
    return false;
  }

  Rooted<ModuleRequestObject*> object(cx);
  for (const StencilModuleRequest& request : moduleRequests) {
    object = createModuleRequestObject(cx, atomCache, request);
    if (!object) {
      return false;
    }

    output.infallibleEmplaceBack(object);
  }

  return true;
}

ModuleRequestObject* frontend::StencilModuleMetadata::createModuleRequestObject(
    JSContext* cx, CompilationAtomCache& atomCache,
    const StencilModuleRequest& request) const {
  uint32_t numberOfAttributes = request.attributes.length();

  Rooted<ImportAttributeVector> attributes(cx);
  if (numberOfAttributes > 0) {
    if (!attributes.reserve(numberOfAttributes)) {
      ReportOutOfMemory(cx);
      return nullptr;
    }

    Rooted<JSAtom*> attributeKey(cx);
    Rooted<JSAtom*> attributeValue(cx);
    for (uint32_t j = 0; j < numberOfAttributes; ++j) {
      attributeKey = atomCache.getExistingAtomAt(cx, request.attributes[j].key);
      attributeValue =
          atomCache.getExistingAtomAt(cx, request.attributes[j].value);

      attributes.infallibleEmplaceBack(attributeKey, attributeValue);
    }
  }

  Rooted<JSAtom*> specifier(cx,
                            atomCache.getExistingAtomAt(cx, request.specifier));
  MOZ_ASSERT(specifier);

  Rooted<ModuleRequestObject*> moduleRequestObject(
      cx,
#ifdef ENABLE_SOURCE_PHASE_IMPORTS
      ModuleRequestObject::create(cx, specifier, attributes, request.phase));
#else
      ModuleRequestObject::create(cx, specifier, attributes));
#endif
  if (!moduleRequestObject) {
    return nullptr;
  }

  if (request.firstUnsupportedAttributeKey) {
    Rooted<JSAtom*> unsupportedAttributeKey(
        cx,
        atomCache.getExistingAtomAt(cx, request.firstUnsupportedAttributeKey));
    moduleRequestObject->setFirstUnsupportedAttributeKey(
        unsupportedAttributeKey);
  }

  return moduleRequestObject;
}

bool frontend::StencilModuleMetadata::createImportEntries(
    JSContext* cx, CompilationAtomCache& atomCache,
    Handle<ModuleRequestVector> moduleRequests,
    MutableHandle<ImportEntryVector> output) const {
  if (!output.reserve(importEntries.length())) {
    ReportOutOfMemory(cx);
    return false;
  }

  for (const StencilModuleEntry& entry : importEntries) {
    Rooted<ModuleRequestObject*> moduleRequest(cx);
    moduleRequest = moduleRequests[entry.moduleRequest.value()].get();
    MOZ_ASSERT(moduleRequest);

    Rooted<JSAtom*> localName(cx);
    if (entry.localName) {
      localName = atomCache.getExistingAtomAt(cx, entry.localName);
      MOZ_ASSERT(localName);
    }

    Rooted<JSAtom*> importName(cx);
    if (entry.importName) {
      importName = atomCache.getExistingAtomAt(cx, entry.importName);
      MOZ_ASSERT(importName);
    }

    MOZ_ASSERT(!entry.exportName);

    output.infallibleEmplaceBack(moduleRequest, importName, localName,
                                 entry.lineno, entry.column);
  }

  return true;
}

bool frontend::StencilModuleMetadata::createExportEntries(
    JSContext* cx, frontend::CompilationAtomCache& atomCache,
    Handle<ModuleRequestVector> moduleRequests,
    const frontend::StencilModuleMetadata::EntryVector& input,
    MutableHandle<ExportEntryVector> output) const {
  if (!output.reserve(output.length() + input.length())) {
    ReportOutOfMemory(cx);
    return false;
  }

  for (const frontend::StencilModuleEntry& entry : input) {
    Rooted<JSAtom*> exportName(cx);
    if (entry.exportName) {
      exportName = atomCache.getExistingAtomAt(cx, entry.exportName);
      MOZ_ASSERT(exportName);
    }

    Rooted<ModuleRequestObject*> moduleRequestObject(cx);
    if (entry.moduleRequest) {
      moduleRequestObject = moduleRequests[entry.moduleRequest.value()].get();
      MOZ_ASSERT(moduleRequestObject);
    }

    Rooted<JSAtom*> localName(cx);
    if (entry.localName) {
      localName = atomCache.getExistingAtomAt(cx, entry.localName);
      MOZ_ASSERT(localName);
    }

    Rooted<JSAtom*> importName(cx);
    if (entry.importName) {
      importName = atomCache.getExistingAtomAt(cx, entry.importName);
      MOZ_ASSERT(importName);
    }

    output.infallibleEmplaceBack(exportName, moduleRequestObject, importName,
                                 localName, entry.lineno, entry.column);
  }

  return true;
}

bool frontend::StencilModuleMetadata::createRequestedModules(
    JSContext* cx, CompilationAtomCache& atomCache,
    Handle<ModuleRequestVector> moduleRequests,
    MutableHandle<RequestedModuleVector> output) const {
  if (!output.reserve(requestedModules.length())) {
    ReportOutOfMemory(cx);
    return false;
  }

  for (const frontend::StencilModuleEntry& entry : requestedModules) {
    Rooted<ModuleRequestObject*> moduleRequest(cx);
    moduleRequest = moduleRequests[entry.moduleRequest.value()].get();
    MOZ_ASSERT(moduleRequest);

    MOZ_ASSERT(!entry.localName);
    MOZ_ASSERT(!entry.importName);
    MOZ_ASSERT(!entry.exportName);

    output.infallibleEmplaceBack(moduleRequest, entry.lineno, entry.column);
  }

  return true;
}

// Use StencilModuleMetadata data to fill in ModuleObject
bool frontend::StencilModuleMetadata::initModule(
    JSContext* cx, FrontendContext* fc,
    frontend::CompilationAtomCache& atomCache,
    JS::Handle<ModuleObject*> module) const {
  Rooted<ModuleRequestVector> moduleRequestsVector(cx);
  if (!createModuleRequestObjects(cx, atomCache, &moduleRequestsVector)) {
    return false;
  }

  Rooted<RequestedModuleVector> requestedModulesVector(cx);
  if (!createRequestedModules(cx, atomCache, moduleRequestsVector,
                              &requestedModulesVector)) {
    return false;
  }

  Rooted<ImportEntryVector> importEntriesVector(cx);
  if (!createImportEntries(cx, atomCache, moduleRequestsVector,
                           &importEntriesVector)) {
    return false;
  }

  Rooted<ExportEntryVector> exportEntriesVector(cx);
  if (!createExportEntries(cx, atomCache, moduleRequestsVector,
                           localExportEntries, &exportEntriesVector)) {
    return false;
  }
  if (!createExportEntries(cx, atomCache, moduleRequestsVector,
                           indirectExportEntries, &exportEntriesVector)) {
    return false;
  }
  if (!createExportEntries(cx, atomCache, moduleRequestsVector,
                           starExportEntries, &exportEntriesVector)) {
    return false;
  }

  // Copy the vector of declarations to the ModuleObject.
  auto functionDeclsCopy = MakeUnique<FunctionDeclarationVector>();
  if (!functionDeclsCopy || !functionDeclsCopy->appendAll(functionDecls)) {
    js::ReportOutOfMemory(fc);
    return false;
  }
  module->initFunctionDeclarations(std::move(functionDeclsCopy));

  Rooted<ListObject*> asyncParentModulesList(cx, ListObject::create(cx));
  if (!asyncParentModulesList) {
    return false;
  }

  module->initAsyncSlots(cx, isAsync, asyncParentModulesList);

  module->initImportExportData(
      &requestedModulesVector, &importEntriesVector, &exportEntriesVector,
      localExportEntries.length(), indirectExportEntries.length(),
      starExportEntries.length());

  return true;
}

bool ModuleBuilder::processAttributes(frontend::StencilModuleRequest& request,
                                      frontend::ListNode* attributeList) {
  using namespace js::frontend;

  for (ParseNode* attributeItem : attributeList->contents()) {
    BinaryNode* attribute = &attributeItem->as<BinaryNode>();
    MOZ_ASSERT(attribute->isKind(ParseNodeKind::ImportAttribute));

    auto key = attribute->left()->as<NameNode>().atom();
    markUsedByStencil(key);

    // Note: This should be driven by a host hook
    // (HostGetSupportedImportAttributes), however the infrastructure of said
    // host hook is deeply unclear, and so right now embedders will not have
    // the ability to alter or extend the set of supported attributes.
    // See https://bugzilla.mozilla.org/show_bug.cgi?id=1840723.
    if (key == TaggedParserAtomIndex::WellKnown::type()) {
      auto value = attribute->right()->as<NameNode>().atom();
      markUsedByStencil(value);

      StencilModuleImportAttribute attributeStencil(key, value);
      if (!request.attributes.append(attributeStencil)) {
        js::ReportOutOfMemory(fc_);
        return false;
      }
    } else {
      if (!request.firstUnsupportedAttributeKey) {
        request.firstUnsupportedAttributeKey = key;
      }
    }
  }

  return true;
}

bool ModuleBuilder::processImport(frontend::BinaryNode* importNode) {
  using namespace js::frontend;

  MOZ_ASSERT(importNode->isKind(ParseNodeKind::ImportDecl)
#ifdef ENABLE_SOURCE_PHASE_IMPORTS
             || importNode->isKind(ParseNodeKind::ImportSourceDecl)
#endif
  );

  auto* moduleRequest = &importNode->right()->as<BinaryNode>();
  MOZ_ASSERT(moduleRequest->isKind(ParseNodeKind::ImportModuleRequest));

  auto* moduleSpec = &moduleRequest->left()->as<NameNode>();
  MOZ_ASSERT(moduleSpec->isKind(ParseNodeKind::StringExpr));

  auto specifier = moduleSpec->atom();

#ifdef ENABLE_SOURCE_PHASE_IMPORTS
  if (importNode->isKind(ParseNodeKind::ImportSourceDecl)) {
    auto* localNameNode = &importNode->left()->as<NameNode>();
    MOZ_ASSERT(localNameNode->isKind(ParseNodeKind::Name));

    MaybeModuleRequestIndex moduleRequestIndex =
        appendModuleRequest(specifier, nullptr, ImportPhase::Source);
    if (!moduleRequestIndex.isSome()) {
      return false;
    }

    if (!maybeAppendRequestedModule(moduleRequestIndex, moduleSpec)) {
      return false;
    }

    auto localName = localNameNode->atom();
    markUsedByStencil(localName);

    uint32_t line;
    JS::LimitedColumnNumberOneOrigin column;
    eitherParser_.computeLineAndColumn(localNameNode->pn_pos.begin, &line,
                                       &column);

    auto entry = StencilModuleEntry::importNamespaceEntry(
        moduleRequestIndex, localName, line, JS::ColumnNumberOneOrigin(column));

    return importEntries_.put(localName, entry);
  }
#endif

  auto* specList = &importNode->left()->as<ListNode>();
  MOZ_ASSERT(specList->isKind(ParseNodeKind::ImportSpecList));

  auto* attributeList = &moduleRequest->right()->as<ListNode>();
  MOZ_ASSERT(attributeList->isKind(ParseNodeKind::ImportAttributeList));

  MaybeModuleRequestIndex moduleRequestIndex =
      appendModuleRequest(specifier, attributeList);
  if (!moduleRequestIndex.isSome()) {
    return false;
  }

  if (!maybeAppendRequestedModule(moduleRequestIndex, moduleSpec)) {
    return false;
  }

  for (ParseNode* item : specList->contents()) {
    uint32_t line;
    JS::LimitedColumnNumberOneOrigin column;
    eitherParser_.computeLineAndColumn(item->pn_pos.begin, &line, &column);

    StencilModuleEntry entry;
    TaggedParserAtomIndex localName;
    if (item->isKind(ParseNodeKind::ImportSpec)) {
      auto* spec = &item->as<BinaryNode>();

      auto* importNameNode = &spec->left()->as<NameNode>();
      auto* localNameNode = &spec->right()->as<NameNode>();

      auto importName = importNameNode->atom();
      localName = localNameNode->atom();

      markUsedByStencil(localName);
      markUsedByStencil(importName);
      entry = StencilModuleEntry::importEntry(
          moduleRequestIndex, localName, importName, line,
          JS::ColumnNumberOneOrigin(column));
    } else {
      MOZ_ASSERT(item->isKind(ParseNodeKind::ImportNamespaceSpec));
      auto* spec = &item->as<UnaryNode>();

      auto* localNameNode = &spec->kid()->as<NameNode>();

      localName = localNameNode->atom();

      markUsedByStencil(localName);
      entry = StencilModuleEntry::importNamespaceEntry(
          moduleRequestIndex, localName, line,
          JS::ColumnNumberOneOrigin(column));
    }

    if (!importEntries_.put(localName, entry)) {
      return false;
    }
  }

  return true;
}

bool ModuleBuilder::processExport(frontend::ParseNode* exportNode) {
  using namespace js::frontend;

  MOZ_ASSERT(exportNode->isKind(ParseNodeKind::ExportStmt) ||
             exportNode->isKind(ParseNodeKind::ExportDefaultStmt));

  bool isDefault = exportNode->isKind(ParseNodeKind::ExportDefaultStmt);
  ParseNode* kid = isDefault ? exportNode->as<BinaryNode>().left()
                             : exportNode->as<UnaryNode>().kid();

  if (isDefault && exportNode->as<BinaryNode>().right()) {
    // This is an export default containing an expression.
    auto localName = TaggedParserAtomIndex::WellKnown::default_();
    auto exportName = TaggedParserAtomIndex::WellKnown::default_();
    return appendExportEntry(exportName, localName);
  }

  switch (kid->getKind()) {
    case ParseNodeKind::ExportSpecList: {
      MOZ_ASSERT(!isDefault);
      for (ParseNode* item : kid->as<ListNode>().contents()) {
        BinaryNode* spec = &item->as<BinaryNode>();
        MOZ_ASSERT(spec->isKind(ParseNodeKind::ExportSpec));

        NameNode* localNameNode = &spec->left()->as<NameNode>();
        NameNode* exportNameNode = &spec->right()->as<NameNode>();

        auto localName = localNameNode->atom();
        auto exportName = exportNameNode->atom();

        if (!appendExportEntry(exportName, localName, spec)) {
          return false;
        }
      }
      break;
    }

    case ParseNodeKind::ClassDecl: {
      const ClassNode& cls = kid->as<ClassNode>();
      MOZ_ASSERT(cls.names());
      auto localName = cls.names()->innerBinding()->atom();
      auto exportName =
          isDefault ? TaggedParserAtomIndex::WellKnown::default_() : localName;
      if (!appendExportEntry(exportName, localName)) {
        return false;
      }
      break;
    }

    case ParseNodeKind::VarStmt:
    case ParseNodeKind::ConstDecl:
    case ParseNodeKind::LetDecl: {
      for (ParseNode* binding : kid->as<ListNode>().contents()) {
        if (binding->isKind(ParseNodeKind::AssignExpr)) {
          binding = binding->as<AssignmentNode>().left();
        } else {
          MOZ_ASSERT(binding->isKind(ParseNodeKind::Name));
        }

        if (binding->isKind(ParseNodeKind::Name)) {
          auto localName = binding->as<NameNode>().atom();
          auto exportName = isDefault
                                ? TaggedParserAtomIndex::WellKnown::default_()
                                : localName;
          if (!appendExportEntry(exportName, localName)) {
            return false;
          }
        } else if (binding->isKind(ParseNodeKind::ArrayExpr)) {
          if (!processExportArrayBinding(&binding->as<ListNode>())) {
            return false;
          }
        } else {
          MOZ_ASSERT(binding->isKind(ParseNodeKind::ObjectExpr));
          if (!processExportObjectBinding(&binding->as<ListNode>())) {
            return false;
          }
        }
      }
      break;
    }

    case ParseNodeKind::Function: {
      FunctionBox* box = kid->as<FunctionNode>().funbox();
      MOZ_ASSERT(!box->isArrow());
      auto localName = box->explicitName();
      auto exportName =
          isDefault ? TaggedParserAtomIndex::WellKnown::default_() : localName;
      if (!appendExportEntry(exportName, localName)) {
        return false;
      }
      break;
    }

    default:
      MOZ_CRASH("Unexpected parse node");
  }

  return true;
}

bool ModuleBuilder::processExportBinding(frontend::ParseNode* binding) {
  using namespace js::frontend;

  if (binding->isKind(ParseNodeKind::Name)) {
    auto name = binding->as<NameNode>().atom();
    return appendExportEntry(name, name);
  }

  if (binding->isKind(ParseNodeKind::ArrayExpr)) {
    return processExportArrayBinding(&binding->as<ListNode>());
  }

  MOZ_ASSERT(binding->isKind(ParseNodeKind::ObjectExpr));
  return processExportObjectBinding(&binding->as<ListNode>());
}

bool ModuleBuilder::processExportArrayBinding(frontend::ListNode* array) {
  using namespace js::frontend;

  MOZ_ASSERT(array->isKind(ParseNodeKind::ArrayExpr));

  for (ParseNode* node : array->contents()) {
    if (node->isKind(ParseNodeKind::Elision)) {
      continue;
    }

    if (node->isKind(ParseNodeKind::Spread)) {
      node = node->as<UnaryNode>().kid();
    } else if (node->isKind(ParseNodeKind::AssignExpr)) {
      node = node->as<AssignmentNode>().left();
    }

    if (!processExportBinding(node)) {
      return false;
    }
  }

  return true;
}

bool ModuleBuilder::processExportObjectBinding(frontend::ListNode* obj) {
  using namespace js::frontend;

  MOZ_ASSERT(obj->isKind(ParseNodeKind::ObjectExpr));

  for (ParseNode* node : obj->contents()) {
    MOZ_ASSERT(node->isKind(ParseNodeKind::MutateProto) ||
               node->isKind(ParseNodeKind::PropertyDefinition) ||
               node->isKind(ParseNodeKind::Shorthand) ||
               node->isKind(ParseNodeKind::Spread));

    ParseNode* target;
    if (node->isKind(ParseNodeKind::Spread)) {
      target = node->as<UnaryNode>().kid();
    } else {
      if (node->isKind(ParseNodeKind::MutateProto)) {
        target = node->as<UnaryNode>().kid();
      } else {
        target = node->as<BinaryNode>().right();
      }

      if (target->isKind(ParseNodeKind::AssignExpr)) {
        target = target->as<AssignmentNode>().left();
      }
    }

    if (!processExportBinding(target)) {
      return false;
    }
  }

  return true;
}

bool ModuleBuilder::processExportFrom(frontend::BinaryNode* exportNode) {
  using namespace js::frontend;

  MOZ_ASSERT(exportNode->isKind(ParseNodeKind::ExportFromStmt));

  auto* specList = &exportNode->left()->as<ListNode>();
  MOZ_ASSERT(specList->isKind(ParseNodeKind::ExportSpecList));

  auto* moduleRequest = &exportNode->right()->as<BinaryNode>();
  MOZ_ASSERT(moduleRequest->isKind(ParseNodeKind::ImportModuleRequest));

  auto* moduleSpec = &moduleRequest->left()->as<NameNode>();
  MOZ_ASSERT(moduleSpec->isKind(ParseNodeKind::StringExpr));

  auto* attributeList = &moduleRequest->right()->as<ListNode>();
  MOZ_ASSERT(attributeList->isKind(ParseNodeKind::ImportAttributeList));

  auto specifier = moduleSpec->atom();
  MaybeModuleRequestIndex moduleRequestIndex =
      appendModuleRequest(specifier, attributeList);
  if (!moduleRequestIndex.isSome()) {
    return false;
  }

  if (!maybeAppendRequestedModule(moduleRequestIndex, moduleSpec)) {
    return false;
  }

  for (ParseNode* spec : specList->contents()) {
    uint32_t line;
    JS::LimitedColumnNumberOneOrigin column;
    eitherParser_.computeLineAndColumn(spec->pn_pos.begin, &line, &column);

    StencilModuleEntry entry;
    if (spec->isKind(ParseNodeKind::ExportSpec)) {
      auto* importNameNode = &spec->as<BinaryNode>().left()->as<NameNode>();
      auto* exportNameNode = &spec->as<BinaryNode>().right()->as<NameNode>();

      auto importName = importNameNode->atom();
      auto exportName = exportNameNode->atom();
      MOZ_ASSERT(exportNames_.has(exportName));

      markUsedByStencil(importName);
      markUsedByStencil(exportName);
      entry = StencilModuleEntry::exportFromEntry(
          moduleRequestIndex, importName, exportName, line,
          JS::ColumnNumberOneOrigin(column));
    } else if (spec->isKind(ParseNodeKind::ExportNamespaceSpec)) {
      auto* exportNameNode = &spec->as<UnaryNode>().kid()->as<NameNode>();

      auto exportName = exportNameNode->atom();
      MOZ_ASSERT(exportNames_.has(exportName));

      markUsedByStencil(exportName);
      entry = StencilModuleEntry::exportNamespaceFromEntry(
          moduleRequestIndex, exportName, line,
          JS::ColumnNumberOneOrigin(column));
    } else {
      MOZ_ASSERT(spec->isKind(ParseNodeKind::ExportBatchSpecStmt));

      entry = StencilModuleEntry::exportBatchFromEntry(
          moduleRequestIndex, line, JS::ColumnNumberOneOrigin(column));
    }

    if (!exportEntries_.append(entry)) {
      return false;
    }
  }

  return true;
}

frontend::StencilModuleEntry* ModuleBuilder::importEntryFor(
    frontend::TaggedParserAtomIndex localName) const {
  MOZ_ASSERT(localName);
  auto ptr = importEntries_.lookup(localName);
  if (!ptr) {
    return nullptr;
  }

  return &ptr->value();
}

ModuleBuilder::NoteExportedNameResult ModuleBuilder::noteExportedName(
    frontend::TaggedParserAtomIndex name) {
  MOZ_ASSERT(name);
  auto addPtr = exportNames_.lookupForAdd(name);
  if (addPtr) {
    return NoteExportedNameResult::AlreadyDeclared;
  }
  if (!exportNames_.add(addPtr, name)) {
    return NoteExportedNameResult::OutOfMemory;
  }
  return NoteExportedNameResult::Success;
}

bool ModuleBuilder::appendExportEntry(
    frontend::TaggedParserAtomIndex exportName,
    frontend::TaggedParserAtomIndex localName, frontend::ParseNode* node) {
  MOZ_ASSERT(exportNames_.has(exportName));

  uint32_t line = 0;
  JS::LimitedColumnNumberOneOrigin column;
  if (node) {
    eitherParser_.computeLineAndColumn(node->pn_pos.begin, &line, &column);
  }

  markUsedByStencil(localName);
  markUsedByStencil(exportName);
  auto entry = frontend::StencilModuleEntry::exportAsEntry(
      localName, exportName, line, JS::ColumnNumberOneOrigin(column));
  return exportEntries_.append(entry);
}

frontend::MaybeModuleRequestIndex ModuleBuilder::appendModuleRequest(
    frontend::TaggedParserAtomIndex specifier,
    frontend::ListNode* attributeList, ImportPhase phase) {
  markUsedByStencil(specifier);
  auto request = frontend::StencilModuleRequest(specifier);

#ifdef ENABLE_SOURCE_PHASE_IMPORTS
  request.phase = phase;
  if (phase == ImportPhase::Evaluation)
#endif
  {
    if (!processAttributes(request, attributeList)) {
      return MaybeModuleRequestIndex();
    }
  }

  if (auto ptr = moduleRequestIndexes_.lookup(request)) {
    return MaybeModuleRequestIndex(ptr->value());
  }

  uint32_t index = moduleRequests_.length();
  if (!moduleRequests_.append(request) ||
      !moduleRequestIndexes_.put(request, index)) {
    js::ReportOutOfMemory(fc_);
    return MaybeModuleRequestIndex();
  }

  return MaybeModuleRequestIndex(index);
}

bool ModuleBuilder::maybeAppendRequestedModule(
    MaybeModuleRequestIndex moduleRequest, frontend::ParseNode* node) {
  uint32_t index = moduleRequest.value();
  if (requestedModuleIndexes_.has(index)) {
    return true;
  }

  uint32_t line;
  JS::LimitedColumnNumberOneOrigin column;
  eitherParser_.computeLineAndColumn(node->pn_pos.begin, &line, &column);

  auto entry = frontend::StencilModuleEntry::requestedModule(
      moduleRequest, line, JS::ColumnNumberOneOrigin(column));

  if (!requestedModules_.append(entry)) {
    js::ReportOutOfMemory(fc_);
    return false;
  }

  return requestedModuleIndexes_.put(index);
}

void ModuleBuilder::markUsedByStencil(frontend::TaggedParserAtomIndex name) {
  // Imported/exported identifiers must be atomized.
  eitherParser_.parserAtoms().markUsedByStencil(
      name, frontend::ParserAtom::Atomize::Yes);
}

JSObject* js::GetOrCreateModuleMetaObject(JSContext* cx,
                                          HandleObject moduleArg) {
  Handle<ModuleObject*> module = moduleArg.as<ModuleObject>();
  if (JSObject* obj = module->metaObject()) {
    return obj;
  }

  RootedObject metaObject(cx, NewPlainObjectWithProto(cx, nullptr));
  if (!metaObject) {
    return nullptr;
  }

  JS::ModuleMetadataHook func = cx->runtime()->moduleMetadataHook;
  if (!func) {
    JS_ReportErrorASCII(cx, "Module metadata hook not set");
    return nullptr;
  }

  if (!func(cx, module, metaObject)) {
    return nullptr;
  }

  module->setMetaObject(metaObject);

  return metaObject;
}

bool ModuleObject::topLevelCapabilityResolve(JSContext* cx,
                                             Handle<ModuleObject*> module) {
  RootedValue rval(cx);
  Rooted<PromiseObject*> promise(cx, module->topLevelCapability());
  return AsyncFunctionReturned(cx, promise, rval);
}

bool ModuleObject::topLevelCapabilityReject(JSContext* cx,
                                            Handle<ModuleObject*> module,
                                            HandleValue error) {
  Rooted<PromiseObject*> promise(cx, module->topLevelCapability());
  return AsyncFunctionThrown(cx, promise, error);
}
