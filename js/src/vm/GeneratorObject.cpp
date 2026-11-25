/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "vm/GeneratorObject.h"

#include "frontend/ParserAtom.h"
#ifdef DEBUG
#  include "js/friend/DumpFunctions.h"  // js::DumpObject, js::DumpValue
#endif
#include "js/PropertySpec.h"
#include "vm/AsyncFunction.h"
#include "vm/AsyncIteration.h"
#include "vm/BytecodeUtil.h"   // js::SuspendPCForAfterYield
#include "vm/FunctionFlags.h"  // js::FunctionFlags
#include "vm/GlobalObject.h"
#include "vm/Interpreter.h"  // js::GeneratorResumeState, js::RunScript
#include "vm/JSObject.h"
#include "vm/PlainObject.h"  // js::PlainObject

#include "debugger/DebugAPI-inl.h"
#include "vm/Stack-inl.h"

using namespace js;

AbstractGeneratorObject* AbstractGeneratorObject::create(
    JSContext* cx, HandleFunction callee, HandleScript script,
    HandleObject environmentChain, Handle<ArgumentsObject*> argsObject) {
  Rooted<AbstractGeneratorObject*> genObj(cx);
  if (!callee->isAsync()) {
    genObj = GeneratorObject::create(cx, callee);
  } else if (callee->isGenerator()) {
    genObj = AsyncGeneratorObject::create(cx, callee);
  } else {
    genObj = AsyncFunctionGeneratorObject::create(cx, callee);
  }
  if (!genObj) {
    return nullptr;
  }

  genObj->setCallee(*callee);
  genObj->setEnvironmentChain(*environmentChain);
  if (argsObject) {
    genObj->setArgsObj(*argsObject.get());
  }

  ArrayObject* stack = NewDenseFullyAllocatedArray(cx, script->nslots());
  if (!stack) {
    return nullptr;
  }

  genObj->setStackStorage(*stack);

  // Note: This assumes that a Warp frame cannot be the target of
  //       the debugger, as we do not call OnNewGenerator.
  return genObj;
}

JSObject* AbstractGeneratorObject::createFromFrame(JSContext* cx,
                                                   AbstractFramePtr frame) {
  MOZ_ASSERT(frame.isGeneratorFrame());
  MOZ_ASSERT(!frame.isConstructing());

  if (frame.isModuleFrame()) {
    return createModuleGenerator(cx, frame);
  }

  RootedFunction fun(cx, frame.callee());
  Rooted<ArgumentsObject*> maybeArgs(
      cx, frame.script()->needsArgsObj() ? &frame.argsObj() : nullptr);
  RootedObject environmentChain(cx, frame.environmentChain());

  RootedScript script(cx, frame.script());
  Rooted<AbstractGeneratorObject*> genObj(
      cx, AbstractGeneratorObject::create(cx, fun, script, environmentChain,
                                          maybeArgs));
  if (!genObj) {
    return nullptr;
  }

  if (!DebugAPI::onNewGenerator(cx, frame, genObj)) {
    return nullptr;
  }

  return genObj;
}

JSObject* AbstractGeneratorObject::createModuleGenerator(
    JSContext* cx, AbstractFramePtr frame) {
  Rooted<ModuleObject*> module(cx, frame.script()->module());
  Rooted<AbstractGeneratorObject*> genObj(cx);
  genObj = AsyncFunctionGeneratorObject::create(cx, module);
  if (!genObj) {
    return nullptr;
  }

  genObj->setModule(*module);
  genObj->setEnvironmentChain(*frame.environmentChain());

  ArrayObject* stack =
      NewDenseFullyAllocatedArray(cx, module->script()->nslots());
  if (!stack) {
    return nullptr;
  }

  genObj->setStackStorage(*stack);

  if (!DebugAPI::onNewGenerator(cx, frame, genObj)) {
    return nullptr;
  }

  return genObj;
}

void AbstractGeneratorObject::trace(JSTracer* trc) {
  DebugAPI::traceGeneratorFrame(trc, this);
}

bool AbstractGeneratorObject::suspend(JSContext* cx, HandleObject obj,
                                      AbstractFramePtr frame,
                                      const jsbytecode* pc, unsigned nvalues) {
  MOZ_ASSERT(JSOp(*pc) == JSOp::InitialYield || JSOp(*pc) == JSOp::Yield ||
             JSOp(*pc) == JSOp::AwaitThenYield || JSOp(*pc) == JSOp::Await);

  auto genObj = obj.as<AbstractGeneratorObject>();
  MOZ_ASSERT(!genObj->hasStackStorage() || genObj->isStackStorageEmpty());
  MOZ_ASSERT_IF(JSOp(*pc) == JSOp::Await, genObj->script()->isAsync());
  MOZ_ASSERT_IF(JSOp(*pc) == JSOp::Yield, genObj->script()->isGenerator());
  MOZ_ASSERT_IF(JSOp(*pc) == JSOp::AwaitThenYield,
                genObj->script()->isGenerator() && genObj->script()->isAsync());

  if (nvalues > 0) {
    ArrayObject* stack = nullptr;
    MOZ_ASSERT(genObj->hasStackStorage());
    stack = &genObj->stackStorage();
    MOZ_ASSERT(stack->getDenseCapacity() >= nvalues);
    if (!frame.saveGeneratorSlots(cx, nvalues, stack)) {
      return false;
    }
  }

  genObj->setResumeIndex(pc);
  genObj->setEnvironmentChain(*frame.environmentChain());
  return true;
}

#ifdef DEBUG
void AbstractGeneratorObject::dump() const {
  fprintf(stderr, "(AbstractGeneratorObject*) %p {\n", (void*)this);
  if (isModuleGenerator()) {
    fprintf(stderr, "  module: (ModuleObject*) %p,\n", (void*)&module());
  } else if (isClosed()) {
    fprintf(stderr, "  callee: <closed>,\n");
  } else {
    fprintf(stderr, "  callee: (JSFunction*) %p,\n", (void*)&callee());
  }
  fprintf(stderr, "  environmentChain: (JSObject*) %p,\n",
          (void*)&environmentChain());
  if (hasArgsObj()) {
    fprintf(stderr, "  argsObj: Some((ArgumentsObject*) %p),\n",
            (void*)&argsObj());
  } else {
    fprintf(stderr, "  argsObj: None,\n");
  }
  if (hasStackStorage()) {
    fprintf(stderr, "  stackStorage: Some(ArrayObject {\n");
    ArrayObject& stack = stackStorage();
    uint32_t denseLen = uint32_t(stack.getDenseInitializedLength());
    fprintf(stderr, "    denseInitializedLength: %u\n,", denseLen);
    uint32_t len = stack.length();
    fprintf(stderr, "    length: %u\n,", len);
    fprintf(stderr, "    data: [\n");
    const Value* elements = getDenseElements();
    for (uint32_t i = 0; i < std::max(len, denseLen); i++) {
      fprintf(stderr, "      [%u]: ", i);
      js::DumpValue(elements[i]);
    }
    fprintf(stderr, "    ],\n");
    fprintf(stderr, "  }),\n");
  } else {
    fprintf(stderr, "  stackStorage: None\n");
  }
  if (isSuspended()) {
    fprintf(stderr, "  resumeIndex: Some(%u),\n", resumeIndex());
  } else {
    fprintf(stderr, "  resumeIndex: None, /* (not suspended) */\n");
  }
  fprintf(stderr, "}\n");
}
#endif

void AbstractGeneratorObject::finalSuspend(JSContext* cx, HandleObject obj) {
  auto* genObj = &obj->as<AbstractGeneratorObject>();
  MOZ_ASSERT(genObj->isRunning());
  genObj->setClosed(cx);
}

static AbstractGeneratorObject* GetGeneratorObjectForCall(JSContext* cx,
                                                          CallObject& callObj) {
  // The ".generator" binding is always present and always "aliased".
  mozilla::Maybe<PropertyInfo> prop =
      callObj.lookup(cx, cx->names().dot_generator_);
  if (prop.isNothing()) {
    return nullptr;
  }
  Value genValue = callObj.getSlot(prop->slot());

  // If the `Generator; SetAliasedVar ".generator"; InitialYield` bytecode
  // sequence has not run yet, genValue is undefined.
  return genValue.isObject()
             ? &genValue.toObject().as<AbstractGeneratorObject>()
             : nullptr;
}

AbstractGeneratorObject* js::GetGeneratorObjectForModule(ModuleObject* module) {
  ModuleEnvironmentObject* moduleEnv = module->environment();
  if (!moduleEnv) {
    return nullptr;
  }

  PropertyName* name =
      module->runtimeFromMainThread()->commonNames->dot_generator_;
  mozilla::Maybe<PropertyInfo> prop = moduleEnv->lookupPure(name);
  if (prop.isNothing()) {
    return nullptr;
  }

  Value genValue = moduleEnv->getRawSlot(prop->slot());
  return genValue.isObject()
             ? &genValue.toObject().as<AbstractGeneratorObject>()
             : nullptr;
}

AbstractGeneratorObject* js::GetGeneratorObjectForFrame(
    JSContext* cx, AbstractFramePtr frame) {
  cx->check(frame);
  MOZ_ASSERT(frame.isGeneratorFrame());

  if (frame.isModuleFrame()) {
    return GetGeneratorObjectForModule(frame.script()->module());
  }
  if (!frame.hasInitialEnvironment()) {
    return nullptr;
  }

  return GetGeneratorObjectForCall(cx, frame.callObj());
}

AbstractGeneratorObject* js::GetGeneratorObjectForEnvironment(
    JSContext* cx, HandleObject env) {
  auto* call = CallObject::find(env);
  return call ? GetGeneratorObjectForCall(cx, *call) : nullptr;
}

void AbstractGeneratorObject::resume(JSContext* cx,
                                     InterpreterActivation& activation,
                                     Handle<AbstractGeneratorObject*> genObj,
                                     HandleValue arg,
                                     GeneratorResumeKind resumeKind) {
  MOZ_ASSERT(genObj->isSuspended());

  InterpreterFrame* fp = activation.regs().fp();
  fp->setResumingGenerator();

  uint32_t resumeIndex = genObj->resumeIndex();
  genObj->setRunning();

  // Initialize the resume args (ResumeFrameArgs) stored after the formals (for
  // function frames) or immediately before the frame (for module frames).
  MOZ_ASSERT_IF(fp->isFunctionFrame(), fp->numActualArgs() == 0);
  ResumeFrameArgs::init(fp->resumeArgs(), arg, ObjectValue(*genObj), resumeKind,
                        resumeIndex);

  if (genObj->hasArgsObj()) {
    fp->initArgsObj(genObj->argsObj());
  }

  JSScript* script = fp->script();
  if (genObj->hasStackStorage() && !genObj->isStackStorageEmpty()) {
    ArrayObject* storage = &genObj->stackStorage();
    uint32_t len = storage->getDenseInitializedLength();
    fp->restoreGeneratorSlots(storage);
    activation.regs().sp += len - script->nfixed();
    storage->setDenseInitializedLength(0);
  }

  uint32_t offset = script->resumeOffsets()[resumeIndex];
  activation.regs().pc = script->offsetToPC(offset);

  // Push arg and resumeKind Values on the generator's stack.
  activation.regs().sp += 2;
  MOZ_ASSERT(activation.regs().spForStackDepth(activation.regs().stackDepth()));
  activation.regs().sp[-2] = arg;
  activation.regs().sp[-1] = Int32Value(int32_t(resumeKind));
}

bool js::ResumeGenerator(JSContext* cx, Handle<AbstractGeneratorObject*> genObj,
                         HandleValue value, GeneratorResumeKind kind,
                         MutableHandleValue result) {
  MOZ_ASSERT(genObj->isSuspended());
  MOZ_ASSERT(cx->realm() == genObj->script()->realm());

  GeneratorResumeState state(cx, genObj, value, kind, result);
  return RunScript(cx, state);
}

GeneratorObject* GeneratorObject::create(JSContext* cx, HandleFunction fun) {
  MOZ_ASSERT(fun->isGenerator() && !fun->isAsync());

  // In general, we have to look up the prototype for the instance here
  // (although it would be nice if we could avoid it somehow; see bug 906600.)
  // Self-hosted generators are only used to implement iterator helpers, which
  // are specified as abstract closures. They always use the default prototype
  // instead of reading it from the constructor.
  // See https://tc39.es/ecma262/#sec-createiteratorfromclosure.
  RootedTuple<Value, JSObject*> roots(cx);
  RootedField<Value, 0> pval(roots);
  if (!fun->isSelfHostedBuiltin() &&
      !GetProperty(cx, fun, fun, cx->names().prototype, &pval)) {
    return nullptr;
  }
  RootedField<JSObject*, 1> proto(roots,
                                  pval.isObject() ? &pval.toObject() : nullptr);
  if (!proto) {
    proto = GlobalObject::getOrCreateGeneratorObjectPrototype(cx, cx->global());
    if (!proto) {
      return nullptr;
    }
  }
  return NewObjectWithGivenProto<GeneratorObject>(cx, proto);
}

const JSClass GeneratorObject::class_ = {
    "Generator",
    JSCLASS_HAS_RESERVED_SLOTS(GeneratorObject::RESERVED_SLOTS),
    &classOps_,
};

const JSClassOps GeneratorObject::classOps_ = {
    .trace = CallTraceMethod<AbstractGeneratorObject>,
};

static const JSFunctionSpec generator_methods[] = {
    JS_SELF_HOSTED_FN("next", "GeneratorNext", 1, 0),
    JS_SELF_HOSTED_FN("throw", "GeneratorThrow", 1, 0),
    JS_SELF_HOSTED_FN("return", "GeneratorReturn", 1, 0),
    JS_FS_END,
};

JSObject* js::NewTenuredObjectWithFunctionPrototype(
    JSContext* cx, Handle<GlobalObject*> global) {
  RootedObject proto(cx, &cx->global()->getFunctionPrototype());
  return NewPlainObjectWithProto(cx, proto, {.newKind = TenuredObject});
}

static JSObject* CreateGeneratorFunction(JSContext* cx, JSProtoKey key) {
  RootedObject proto(cx, &cx->global()->getFunctionConstructor());
  Handle<PropertyName*> name = cx->names().GeneratorFunction;
  return NewFunctionWithProto(cx, Generator, 1, FunctionFlags::NATIVE_CTOR,
                              nullptr, name, proto, gc::AllocKind::FUNCTION,
                              TenuredObject);
}

static JSObject* CreateGeneratorFunctionPrototype(JSContext* cx,
                                                  JSProtoKey key) {
  return NewTenuredObjectWithFunctionPrototype(cx, cx->global());
}

static bool GeneratorFunctionClassFinish(JSContext* cx,
                                         HandleObject genFunction,
                                         HandleObject genFunctionProto) {
  Handle<GlobalObject*> global = cx->global();

  // Change the "constructor" property to non-writable before adding any other
  // properties, so it's still the last property and can be modified without a
  // dictionary-mode transition.
  MOZ_ASSERT(genFunctionProto->as<NativeObject>().getLastProperty().key() ==
             NameToId(cx->names().constructor));
  MOZ_ASSERT(!genFunctionProto->as<NativeObject>().inDictionaryMode());

  RootedValue genFunctionVal(cx, ObjectValue(*genFunction));
  if (!DefineDataProperty(cx, genFunctionProto, cx->names().constructor,
                          genFunctionVal, JSPROP_READONLY)) {
    return false;
  }
  MOZ_ASSERT(!genFunctionProto->as<NativeObject>().inDictionaryMode());

  RootedObject iteratorProto(
      cx, GlobalObject::getOrCreateIteratorPrototype(cx, global));
  if (!iteratorProto) {
    return false;
  }

  RootedObject genObjectProto(cx, GlobalObject::createBlankPrototypeInheriting(
                                      cx, &PlainObject::class_, iteratorProto));
  if (!genObjectProto) {
    return false;
  }
  if (!DefinePropertiesAndFunctions(cx, genObjectProto, nullptr,
                                    generator_methods) ||
      !DefineToStringTag(cx, genObjectProto, cx->names().Generator)) {
    return false;
  }

  if (!LinkConstructorAndPrototype(cx, genFunctionProto, genObjectProto,
                                   JSPROP_READONLY, JSPROP_READONLY) ||
      !DefineToStringTag(cx, genFunctionProto, cx->names().GeneratorFunction)) {
    return false;
  }

  global->setGeneratorObjectPrototype(genObjectProto);

  return true;
}

static const ClassSpec GeneratorFunctionClassSpec = {
    CreateGeneratorFunction,
    CreateGeneratorFunctionPrototype,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    GeneratorFunctionClassFinish,
    ClassSpec::DontDefineConstructor,
};

const JSClass js::GeneratorFunctionClass = {
    "GeneratorFunction",
    0,
    JS_NULL_CLASS_OPS,
    &GeneratorFunctionClassSpec,
};

const Value& AbstractGeneratorObject::getUnaliasedLocal(uint32_t slot) const {
  MOZ_ASSERT(isSuspended());
  MOZ_ASSERT(hasStackStorage());
  MOZ_ASSERT(slot < script()->nfixed());
  return stackStorage().getDenseElement(slot);
}

void AbstractGeneratorObject::setUnaliasedLocal(uint32_t slot,
                                                const Value& value) {
  MOZ_ASSERT(isSuspended());
  MOZ_ASSERT(hasStackStorage());
  MOZ_ASSERT(slot < script()->nfixed());
  return stackStorage().setDenseElement(slot, value);
}

void AbstractGeneratorObject::setClosed(JSContext* cx) {
  // If the top-level await generator is suspended in
  // ModuleObject::onTopLevelEvaluationFinished, clear the SCRIPT_SLOT since
  // the generator is closed.
  if (isModuleGenerator() && module().status() == ModuleStatus::Evaluated) {
    module().setReservedSlotTyped(ModuleObject::SCRIPT_SLOT, UndefinedValue());
  }

  setFixedSlotTyped(CALLEE_OR_MODULE_SLOT, NullValue());
  setFixedSlotTyped(ENV_CHAIN_SLOT, NullValue());
  setFixedSlotTyped(ARGS_OBJ_SLOT, NullValue());
  setFixedSlotTyped(STACK_STORAGE_SLOT, NullValue());
  setFixedSlotTyped(RESUME_INDEX_SLOT, NullValue());

  DebugAPI::onGeneratorClosed(cx, this);
}

JSOp AbstractGeneratorObject::suspendOp() const {
  MOZ_RELEASE_ASSERT(isSuspended());

  JSScript* script = this->script();
  jsbytecode* code = script->code();
  uint32_t nextOffset = script->resumeOffsets()[resumeIndex()];
  MOZ_RELEASE_ASSERT(JSOp(code[nextOffset]) == JSOp::AfterYield);

  return JSOp(*SuspendPCForAfterYield(code + nextOffset));
}

template <>
bool JSObject::is<js::AbstractGeneratorObject>() const {
  return is<GeneratorObject>() || is<AsyncFunctionGeneratorObject>() ||
         is<AsyncGeneratorObject>();
}

GeneratorResumeKind js::ParserAtomToResumeKind(
    frontend::TaggedParserAtomIndex atom) {
  if (atom == frontend::TaggedParserAtomIndex::WellKnown::next()) {
    return GeneratorResumeKind::Next;
  }
  if (atom == frontend::TaggedParserAtomIndex::WellKnown::throw_()) {
    return GeneratorResumeKind::Throw;
  }
  MOZ_ASSERT(atom == frontend::TaggedParserAtomIndex::WellKnown::return_());
  return GeneratorResumeKind::Return;
}

JSAtom* js::ResumeKindToAtom(JSContext* cx, GeneratorResumeKind kind) {
  switch (kind) {
    case GeneratorResumeKind::Next:
      return cx->names().next;

    case GeneratorResumeKind::Throw:
      return cx->names().throw_;

    case GeneratorResumeKind::Return:
      return cx->names().return_;
  }
  MOZ_CRASH("Invalid resume kind");
}
