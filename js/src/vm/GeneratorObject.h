/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef vm_GeneratorObject_h
#define vm_GeneratorObject_h

#include "builtin/ModuleObject.h"
#include "builtin/SelfHostingDefines.h"
#include "js/Class.h"
#include "vm/ArgumentsObject.h"
#include "vm/ArrayObject.h"
#include "vm/BytecodeUtil.h"
#include "vm/GeneratorResumeKind.h"  // GeneratorResumeKind
#include "vm/JSObject.h"
#include "vm/Stack.h"

namespace js {

class InterpreterActivation;

namespace frontend {
class TaggedParserAtomIndex;
}

extern const JSClass GeneratorFunctionClass;

class AbstractGeneratorObject : public NativeObject {
 public:
  // Magic value stored in the resumeIndex slot when the generator is
  // running or closing. See the resumeIndex comment below.
  static const int32_t RESUME_INDEX_RUNNING = INT32_MAX;

  // Resume index of the initial yield. Used to identify the "suspended-start"
  // generator state.
  static const int32_t RESUME_INDEX_INITIAL_YIELD = 0;

  JS_DEFINE_TYPED_SLOT(0, CALLEE_OR_MODULE_SLOT, Object, Null, Undefined);
  JS_DEFINE_TYPED_SLOT(1, ENV_CHAIN_SLOT, Object, Null, Undefined);
  JS_DEFINE_TYPED_SLOT(2, ARGS_OBJ_SLOT, Object, Null, Undefined);
  JS_DEFINE_TYPED_SLOT(3, STACK_STORAGE_SLOT, Object, Null, Undefined);
  JS_DEFINE_TYPED_SLOT(4, RESUME_INDEX_SLOT, Int32, Null, Undefined);
  static constexpr uint32_t RESERVED_SLOTS = 5;

  static_assert(RESUME_INDEX_SLOT.index() == GENERATOR_RESUME_INDEX_SLOT,
                "RESUME_INDEX_SLOT must match self-hosting define for resume "
                "index slot.");

  static_assert(RESUME_INDEX_INITIAL_YIELD ==
                    GENERATOR_RESUME_INDEX_INITIAL_YIELD,
                "RESUME_INDEX_INITIAL_YIELD must match self-hosting define for "
                "resume index initial yield.");

 private:
  static JSObject* createModuleGenerator(JSContext* cx, AbstractFramePtr frame);

 public:
  static JSObject* createFromFrame(JSContext* cx, AbstractFramePtr frame);
  static AbstractGeneratorObject* create(JSContext* cx, HandleFunction callee,
                                         HandleScript script,
                                         HandleObject environmentChain,
                                         Handle<ArgumentsObject*> argsObject);

  static void resume(JSContext* cx, InterpreterActivation& activation,
                     Handle<AbstractGeneratorObject*> genObj, HandleValue arg,
                     GeneratorResumeKind resumeKind);

  static bool suspend(JSContext* cx, HandleObject obj, AbstractFramePtr frame,
                      const jsbytecode* pc, unsigned nvalues);

  static void finalSuspend(JSContext* cx, HandleObject obj);

  // True for an async module's top-level-await generator, whose
  // CALLEE_OR_MODULE_SLOT holds a ModuleObject rather than a callee function.
  bool isModuleGenerator() const {
    const Value& v = getFixedSlotTyped(CALLEE_OR_MODULE_SLOT);
    return v.isObject() && v.toObject().is<ModuleObject>();
  }

  JSFunction& callee() const {
    MOZ_ASSERT(!isModuleGenerator());
    return getFixedSlotTyped(CALLEE_OR_MODULE_SLOT).toObject().as<JSFunction>();
  }
  void setCallee(JSFunction& callee) {
    setFixedSlotTyped(CALLEE_OR_MODULE_SLOT, ObjectValue(callee));
  }

  ModuleObject& module() const {
    MOZ_ASSERT(isModuleGenerator());
    return getFixedSlotTyped(CALLEE_OR_MODULE_SLOT)
        .toObject()
        .as<ModuleObject>();
  }
  void setModule(ModuleObject& module) {
    setFixedSlotTyped(CALLEE_OR_MODULE_SLOT, ObjectValue(module));
  }

  JSScript* script() const {
    return isModuleGenerator() ? module().script() : callee().nonLazyScript();
  }

  JSObject& environmentChain() const {
    return getFixedSlotTyped(ENV_CHAIN_SLOT).toObject();
  }
  void setEnvironmentChain(JSObject& envChain) {
    setFixedSlotTyped(ENV_CHAIN_SLOT, ObjectValue(envChain));
  }

  bool hasArgsObj() const {
    return getFixedSlotTyped(ARGS_OBJ_SLOT).isObject();
  }
  ArgumentsObject& argsObj() const {
    return getFixedSlotTyped(ARGS_OBJ_SLOT).toObject().as<ArgumentsObject>();
  }
  void setArgsObj(ArgumentsObject& argsObj) {
    setFixedSlotTyped(ARGS_OBJ_SLOT, ObjectValue(argsObj));
  }

  bool hasStackStorage() const {
    return getFixedSlotTyped(STACK_STORAGE_SLOT).isObject();
  }
  bool isStackStorageEmpty() const {
    return stackStorage().getDenseInitializedLength() == 0;
  }
  ArrayObject& stackStorage() const {
    return getFixedSlotTyped(STACK_STORAGE_SLOT).toObject().as<ArrayObject>();
  }
  void setStackStorage(ArrayObject& stackStorage) {
    setFixedSlotTyped(STACK_STORAGE_SLOT, ObjectValue(stackStorage));
  }

  // Access stack storage. Requires `hasStackStorage() && isSuspended()`.
  // `slot` is the index of the desired local in the stack frame when this
  // generator is *not* suspended.
  const Value& getUnaliasedLocal(uint32_t slot) const;
  void setUnaliasedLocal(uint32_t slot, const Value& value);

  // The resumeIndex slot is abused for a few purposes.  It's undefined if
  // it hasn't been set yet (before the initial yield), and null if the
  // generator is closed. If the generator is running, the resumeIndex is
  // RESUME_INDEX_RUNNING.
  //
  // If the generator is suspended, it's the resumeIndex (stored as
  // JSOp::InitialYield/JSOp::Yield/JSOp::AwaitThenYield/JSOp::Await operand) of
  // the yield instruction that suspended the generator. The resumeIndex can be
  // mapped to the bytecode offset (interpreter) or to the native code offset
  // (JIT).

  bool isBeforeInitialYield() const {
    return getFixedSlotTyped(RESUME_INDEX_SLOT).isUndefined();
  }
  bool isRunning() const {
    return getFixedSlotTyped(RESUME_INDEX_SLOT) ==
           Int32Value(RESUME_INDEX_RUNNING);
  }
  bool isSuspended() const {
    // Note: also update MacroAssembler::branchIfNotSuspendedGenerator if this
    // changes.
    Value resumeIndex = getFixedSlotTyped(RESUME_INDEX_SLOT);
    return resumeIndex.isInt32() &&
           resumeIndex.toInt32() < RESUME_INDEX_RUNNING;
  }
  void setRunning() {
    MOZ_ASSERT(isSuspended());
    setFixedSlotTyped(RESUME_INDEX_SLOT, Int32Value(RESUME_INDEX_RUNNING));
  }
  void setResumeIndex(const jsbytecode* pc) {
    MOZ_ASSERT(JSOp(*pc) == JSOp::InitialYield || JSOp(*pc) == JSOp::Yield ||
               JSOp(*pc) == JSOp::AwaitThenYield || JSOp(*pc) == JSOp::Await);

    MOZ_ASSERT_IF(JSOp(*pc) == JSOp::InitialYield,
                  getFixedSlotTyped(RESUME_INDEX_SLOT).isUndefined());
    MOZ_ASSERT_IF(JSOp(*pc) != JSOp::InitialYield, isRunning());

    uint32_t resumeIndex = GET_UINT24(pc);
    MOZ_ASSERT(resumeIndex < uint32_t(RESUME_INDEX_RUNNING));

    setFixedSlotTyped(RESUME_INDEX_SLOT, Int32Value(resumeIndex));
    MOZ_ASSERT(isSuspended());
  }
  void setResumeIndex(int32_t resumeIndex) {
    setFixedSlotTyped(RESUME_INDEX_SLOT, Int32Value(resumeIndex));
  }
  uint32_t resumeIndex() const {
    MOZ_ASSERT(isSuspended());
    return getFixedSlotTyped(RESUME_INDEX_SLOT).toInt32();
  }
  bool isClosed() const {
    return getFixedSlotTyped(CALLEE_OR_MODULE_SLOT).isNull();
  }
  void setClosed(JSContext* cx);

  // Returns the JSOp that suspended this generator (for example JSOp::Yield or
  // JSOp::Await).
  JSOp suspendOp() const;

  void trace(JSTracer* trc);

  static size_t offsetOfCalleeOrModuleSlot() {
    return getFixedSlotOffsetTyped(CALLEE_OR_MODULE_SLOT);
  }
  static size_t offsetOfEnvironmentChainSlot() {
    return getFixedSlotOffsetTyped(ENV_CHAIN_SLOT);
  }
  static size_t offsetOfArgsObjSlot() {
    return getFixedSlotOffsetTyped(ARGS_OBJ_SLOT);
  }
  static size_t offsetOfResumeIndexSlot() {
    return getFixedSlotOffsetTyped(RESUME_INDEX_SLOT);
  }
  static size_t offsetOfStackStorageSlot() {
    return getFixedSlotOffsetTyped(STACK_STORAGE_SLOT);
  }

  static size_t calleeOrModuleSlot() { return CALLEE_OR_MODULE_SLOT.index(); }
  static size_t envChainSlot() { return ENV_CHAIN_SLOT.index(); }
  static size_t argsObjectSlot() { return ARGS_OBJ_SLOT.index(); }
  static size_t stackStorageSlot() { return STACK_STORAGE_SLOT.index(); }
  static size_t resumeIndexSlot() { return RESUME_INDEX_SLOT.index(); }

#ifdef DEBUG
  void dump() const;
#endif
};

class GeneratorObject : public AbstractGeneratorObject {
 public:
  enum { RESERVED_SLOTS = AbstractGeneratorObject::RESERVED_SLOTS };

  static const JSClass class_;
  static const JSClassOps classOps_;

  static GeneratorObject* create(JSContext* cx, HandleFunction fun);
};

/**
 * Resume a suspended generator, async function, async generator, or async
 * module.
 *
 * |cx| must already be in the generator's realm.
 */
bool ResumeGenerator(JSContext* cx, Handle<AbstractGeneratorObject*> genObj,
                     HandleValue value, GeneratorResumeKind kind,
                     MutableHandleValue result);

/**
 * Return the generator object associated with the given frame. The frame must
 * be a call frame for a generator.
 *
 * This may return nullptr at certain points in the generator lifecycle:
 *
 * - While a generator call evaluates default argument values and performs
 *   destructuring, which occurs before the generator object is created.
 *
 * - Between the `Generator` instruction and the `SetAliasedVar .generator`
 *   instruction, at which point the generator object does exist, but is held
 *   only on the stack, and not the `.generator` pseudo-variable this function
 *   consults.
 */
AbstractGeneratorObject* GetGeneratorObjectForFrame(JSContext* cx,
                                                    AbstractFramePtr frame);

/**
 * Return the top-level-await generator object of `module`, or nullptr if
 * `module` has no top-level await, or it is not linked yet, or its generator
 * object hasn't been created yet.
 */
AbstractGeneratorObject* GetGeneratorObjectForModule(ModuleObject* module);

/**
 * If `env` or any enclosing environment is a `CallObject` associated with a
 * generator object, return the generator.
 *
 * Otherwise `env` is not in a generator or async function, or the generator
 * object hasn't been created yet; return nullptr with no pending exception.
 */
AbstractGeneratorObject* GetGeneratorObjectForEnvironment(JSContext* cx,
                                                          HandleObject env);

GeneratorResumeKind ParserAtomToResumeKind(
    frontend::TaggedParserAtomIndex atom);
JSAtom* ResumeKindToAtom(JSContext* cx, GeneratorResumeKind kind);

}  // namespace js

template <>
bool JSObject::is<js::AbstractGeneratorObject>() const;

#endif /* vm_GeneratorObject_h */
