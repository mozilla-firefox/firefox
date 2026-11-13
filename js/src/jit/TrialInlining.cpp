/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/TrialInlining.h"

#include "mozilla/DebugOnly.h"

#include "gc/Zone.h"
#include "jit/BaselineCacheIRCompiler.h"
#include "jit/BaselineFrame.h"
#include "jit/BaselineIC.h"
#include "jit/BytecodeAnalysis.h"
#include "jit/CacheIRCloner.h"
#include "jit/CacheIRHealth.h"
#include "jit/CacheIRWriter.h"
#include "jit/InlineScriptTree.h"
#include "jit/Ion.h"  // TooManyFormalArguments
#include "jit/StubFolding.h"

#include "vm/BytecodeLocation-inl.h"

using mozilla::Maybe;

namespace js {
namespace jit {

bool DoTrialInlining(JSContext* cx, BaselineFrame* frame) {
  RootedScript script(cx, frame->script());
  ICScript* icScript = frame->icScript();
  bool isRecursive = icScript->depth() > 0;

#ifdef JS_CACHEIR_SPEW
  if (cx->spewer().enabled(cx, script, SpewChannel::CacheIRHealthReport)) {
    for (uint32_t i = 0; i < icScript->numICEntries(); i++) {
      ICEntry& entry = icScript->icEntry(i);
      ICFallbackStub* fallbackStub = icScript->fallbackStub(i);

      // If the IC is megamorphic or generic, then we have already
      // spewed the IC report on transition.
      if (!(uint8_t(fallbackStub->state().mode()) > 0)) {
        jit::ICStub* stub = entry.firstStub();
        bool sawNonZeroCount = false;
        while (!stub->isFallback()) {
          uint32_t count = stub->enteredCount();
          if (count > 0 && sawNonZeroCount) {
            CacheIRHealth cih;
            cih.healthReportForIC(cx, &entry, fallbackStub, script,
                                  SpewContext::TrialInlining);
            break;
          }

          if (count > 0 && !sawNonZeroCount) {
            sawNonZeroCount = true;
          }

          stub = stub->toCacheIRStub()->next();
        }
      }
    }
  }
#endif

  if (!script->canIonCompile()) {
    return true;
  }

  // Don't do trial inlining in scripts that are too large.
  if (JitOptions.limitScriptSize &&
      script->length() > JitOptions.ionMaxScriptSize) {
    // Baseline should elide trial inlining calls if the script is big.
    MOZ_ASSERT(frame->runningInInterpreter());
    return true;
  }

  // Maximum trial inlining depth for ICScripts. This is smaller than
  // InlineScriptTree::MaxDepth because we can reach the bigger number if we are
  // doing monomorphic inlining using shared IC data.
  const uint32_t MaxICScriptDepth = 4;
  static_assert(MaxICScriptDepth <= InlineScriptTree::MaxDepth,
                "Trial inlining depth must not exceed max inlining depth");
  if (icScript->depth() > MaxICScriptDepth) {
    return true;
  }

  InliningRoot* root = isRecursive ? icScript->inliningRoot()
                                   : script->jitScript()->inliningRoot();
  if (JitSpewEnabled(JitSpew_WarpTrialInlining)) {
    // Eagerly create the inlining root when it's used in the spew output.
    if (!root) {
      MOZ_ASSERT(!isRecursive);
      root = script->jitScript()->getOrCreateInliningRoot(cx, script);
      if (!root) {
        return false;
      }
    }
    UniqueChars funName;
    if (script->function() && script->function()->fullDisplayAtom()) {
      funName =
          AtomToPrintableString(cx, script->function()->fullDisplayAtom());
    }

    JitSpew(
        JitSpew_WarpTrialInlining,
        "Trial inlining for %s script '%s' (%s:%u:%u (%p)) (inliningRoot=%p)",
        (isRecursive ? "inner" : "outer"),
        funName ? funName.get() : "<unnamed>", script->filename(),
        script->lineno(), script->column().oneOriginValue(), frame->script(),
        root);
    JitSpewIndent spewIndent(JitSpew_WarpTrialInlining);
  }

  TrialInliner inliner(cx, script, icScript);
  return inliner.tryInlining();
}

TrialInliner::TrialInliner(JSContext* cx, HandleScript script,
                           ICScript* icScript)
    : cx_(cx), script_(script), icScript_(icScript) {}

void TrialInliner::cloneSharedPrefix(ICCacheIRStub* stub,
                                     const uint8_t* endOfPrefix,
                                     CacheIRWriter& writer) {
  CacheIRReader reader(stub->stubInfo());
  CacheIRCloner cloner(stub);
  while (reader.currentPosition() < endOfPrefix) {
    CacheOp op = reader.readOp();
    cloner.cloneOp(op, reader, writer);
  }
}

bool TrialInliner::replaceICStub(ICEntry& entry, ICFallbackStub* fallback,
                                 CacheIRWriter& writer, CacheKind kind) {
  MOZ_ASSERT(fallback->trialInliningState() == TrialInliningState::Candidate);
  writer.setTrialInliningState(TrialInliningState::Inlined);

  // Note: AttachBaselineCacheIRStubLocked never throws an exception.
  MaybeMarkingLock lock;
  ICAttachResult result = AttachBaselineCacheIRStubLocked(
      cx(), writer, kind, script_, icScript_, fallback,
      DiscardExistingStubs::Yes, "TrialInline", lock);
  if (result == ICAttachResult::Attached) {
    MOZ_ASSERT(fallback->trialInliningState() == TrialInliningState::Inlined);
    return true;
  }

  if (!lock.isSome()) {
    lock.emplace(cx()->zone(), icScript_->markingLock());
  }
  MOZ_ASSERT(fallback->trialInliningState() == TrialInliningState::Candidate);
  icScript_->removeInlinedChild(fallback->pcOffset());

  if (result == ICAttachResult::OOM) {
    ReportOutOfMemory(cx());
    return false;
  }

  // We failed to attach a new IC stub due to CacheIR size limits. Disable trial
  // inlining for this location and return true.
  MOZ_ASSERT(result == ICAttachResult::TooLarge);
  fallback->setTrialInliningState(TrialInliningState::Failure);
  return true;
}

ICCacheIRStub* TrialInliner::maybeSingleStub(const ICEntry& entry) {
  // Look for a single non-fallback stub followed by stubs with entered-count 0.
  // Allow one optimized stub before the fallback stub to support the
  // CallIRGenerator::emitCalleeGuard optimization where we first try a
  // GuardSpecificFunction guard before falling back to GuardFunctionHasScript.
  ICStub* stub = entry.firstStub();
  if (stub->isFallback()) {
    return nullptr;
  }
  ICStub* next = stub->toCacheIRStub()->next();
  if (next->enteredCount() != 0) {
    return nullptr;
  }

  ICFallbackStub* fallback = nullptr;
  if (next->isFallback()) {
    fallback = next->toFallbackStub();
  } else {
    ICStub* nextNext = next->toCacheIRStub()->next();
    if (!nextNext->isFallback() || nextNext->enteredCount() != 0) {
      return nullptr;
    }
    fallback = nextNext->toFallbackStub();
  }

  if (fallback->trialInliningState() != TrialInliningState::Candidate) {
    return nullptr;
  }

  return stub->toCacheIRStub();
}

Maybe<InlinableOpData> FindInlinableOpData(ICCacheIRStub* stub,
                                           BytecodeLocation loc) {
  if (loc.isInvokeOp()) {
    Maybe<InlinableCallData> call = FindInlinableCallData(stub);
    if (call.isSome()) {
      return call;
    }
  }
  if (loc.isGetPropOp() || loc.isGetElemOp()) {
    Maybe<InlinableGetterData> getter = FindInlinableGetterData(stub);
    if (getter.isSome()) {
      return getter;
    }
  }
  if (loc.isSetPropOp()) {
    Maybe<InlinableSetterData> setter = FindInlinableSetterData(stub);
    if (setter.isSome()) {
      return setter;
    }
  }
  return mozilla::Nothing();
}

// We currently only inline bound functions with no bound arguments or
// new.target.
// TODO (bug 2066335): Support bound functions with up to MaxInlineBoundArgs
// arguments.
static bool CanInlineBoundCall(uint32_t numBoundArgs, CallFlags flags) {
  return numBoundArgs == 0 && !flags.isConstructing();
}

Maybe<InlinableCallData> FindInlinableCallData(ICCacheIRStub* stub) {
  Maybe<InlinableCallData> data;

  const CacheIRStubInfo* stubInfo = stub->stubInfo();
  const uint8_t* stubData = stub->stubDataStart();

  ObjOperandId calleeGuardOperand;
  CallFlags flags;
  JSScript* targetScript = nullptr;
  JSFunction* targetFunction = nullptr;

  CacheIRReader reader(stubInfo);
  while (reader.more()) {
    const uint8_t* opStart = reader.currentPosition();

    CacheOp op = reader.readOp();
    CacheIROpInfo opInfo = CacheIROpInfos[size_t(op)];
    uint32_t argLength = opInfo.argLength;
    mozilla::DebugOnly<const uint8_t*> argStart = reader.currentPosition();

    switch (op) {
      case CacheOp::GuardSpecificFunction: {
        // If we see a guard for a scripted function, remember which
        // operand we are guarding.
        MOZ_ASSERT(data.isNothing());
        auto args = reader.argsForGuardSpecificFunction();
        uint32_t targetOffset = args.expectedOffset;
        uintptr_t rawFunction =
            stubInfo->getStubRawWord(stubData, targetOffset);
        JSFunction* function = reinterpret_cast<JSFunction*>(rawFunction);
        if (function->hasBytecode()) {
          calleeGuardOperand = args.funId;
          targetScript = function->nonLazyScript();
          targetFunction = function;
        }
        break;
      }
      case CacheOp::GuardFunctionScript: {
        MOZ_ASSERT(data.isNothing());
        auto args = reader.argsForGuardFunctionScript();
        calleeGuardOperand = args.objId;
        uint32_t targetOffset = args.expectedOffset;
        uintptr_t rawScript = stubInfo->getStubRawWord(stubData, targetOffset);
        targetScript = reinterpret_cast<JSScript*>(rawScript);
        break;
      }
      case CacheOp::CallScriptedFunction: {
        // If we see a call, check if `callee` is the previously guarded
        // operand. If it is, we know the target and can inline.
        auto args = reader.argsForCallScriptedFunction();
        flags = args.flags;
        MOZ_ASSERT(args.argcFixed <= MaxUnrolledArgCopy);

        if (args.calleeId == calleeGuardOperand) {
          MOZ_ASSERT(args.argcId.id() == 0);
          MOZ_ASSERT(data.isNothing());
          data.emplace();
          data->endOfSharedPrefix = opStart;
          data->calleeOperand = args.calleeId;
        }
        break;
      }
      case CacheOp::CallInlinedFunction: {
        auto args = reader.argsForCallInlinedFunction();
        flags = args.flags;
        MOZ_ASSERT(args.argcFixed <= MaxUnrolledArgCopy);

        if (args.calleeId == calleeGuardOperand) {
          MOZ_ASSERT(args.argcId.id() == 0);
          MOZ_ASSERT(data.isNothing());
          data.emplace();
          data->endOfSharedPrefix = opStart;
          data->calleeOperand = args.calleeId;
          uintptr_t rawICScript =
              stubInfo->getStubRawWord(stubData, args.icScriptOffset);
          data->icScript = reinterpret_cast<ICScript*>(rawICScript);
        }
        break;
      }
      case CacheOp::CallBoundScriptedFunction: {
        auto args = reader.argsForCallBoundScriptedFunction();
        flags = args.flags;

        if (args.targetId == calleeGuardOperand) {
          MOZ_ASSERT(args.argcId.id() == 0);
          MOZ_ASSERT(data.isNothing());
          if (!CanInlineBoundCall(args.numBoundArgs, args.flags)) {
            return mozilla::Nothing();
          }
          data.emplace();
          data->endOfSharedPrefix = opStart;
          data->isBound = true;
          data->calleeOperand = args.calleeId;
          data->boundTargetOperand = args.targetId;
          data->numBoundArgs = args.numBoundArgs;
        }
        break;
      }
      case CacheOp::CallInlinedBoundFunction: {
        auto args = reader.argsForCallInlinedBoundFunction();
        flags = args.flags;

        if (args.targetId == calleeGuardOperand) {
          MOZ_ASSERT(args.argcId.id() == 0);
          MOZ_ASSERT(data.isNothing());
          MOZ_ASSERT(CanInlineBoundCall(args.numBoundArgs, args.flags));
          data.emplace();
          data->endOfSharedPrefix = opStart;
          data->isBound = true;
          data->calleeOperand = args.calleeId;
          data->boundTargetOperand = args.targetId;
          data->numBoundArgs = args.numBoundArgs;
          uintptr_t rawICScript =
              stubInfo->getStubRawWord(stubData, args.icScriptOffset);
          data->icScript = reinterpret_cast<ICScript*>(rawICScript);
        }
        break;
      }
      default:
        if (!opInfo.transpile) {
          return mozilla::Nothing();
        }
        MOZ_ASSERT(data.isNothing());
        reader.skip(argLength);
        break;
    }
    MOZ_ASSERT(argStart + argLength == reader.currentPosition());
  }

  if (data.isSome()) {
    // Warp only supports inlining Standard and FunCall calls.
    if (flags.getArgFormat() != CallFlags::Standard &&
        flags.getArgFormat() != CallFlags::FunCall) {
      return mozilla::Nothing();
    }
    data->callFlags = flags;
    data->target = targetScript;
    data->targetFunction = targetFunction;
  }
  return data;
}

Maybe<InlinableGetterData> FindInlinableGetterData(ICCacheIRStub* stub) {
  Maybe<InlinableGetterData> data;

  const CacheIRStubInfo* stubInfo = stub->stubInfo();
  const uint8_t* stubData = stub->stubDataStart();

  ObjOperandId maybeCalleeOperand;
  JSScript* targetScript = nullptr;
  JSFunction* targetFunction = nullptr;

  CacheIRReader reader(stubInfo);
  while (reader.more()) {
    const uint8_t* opStart = reader.currentPosition();

    CacheOp op = reader.readOp();
    CacheIROpInfo opInfo = CacheIROpInfos[size_t(op)];
    uint32_t argLength = opInfo.argLength;
    mozilla::DebugOnly<const uint8_t*> argStart = reader.currentPosition();

    switch (op) {
      case CacheOp::LoadObject: {
        // If we load a constant object, remember it in case it's the callee.
        auto [resultOperand, objOffset] = reader.argsForLoadObject();
        uintptr_t rawObject = stubInfo->getStubRawWord(stubData, objOffset);
        JSObject* object = reinterpret_cast<JSObject*>(rawObject);
        if (object->is<JSFunction>() &&
            object->as<JSFunction>().hasBytecode()) {
          maybeCalleeOperand = resultOperand;
          targetFunction = &object->as<JSFunction>();
          targetScript = targetFunction->nonLazyScript();
        }
        break;
      }
      case CacheOp::GuardFunctionScript: {
        MOZ_ASSERT(data.isNothing());
        auto args = reader.argsForGuardFunctionScript();
        maybeCalleeOperand = args.objId;
        uint32_t targetOffset = args.expectedOffset;
        uintptr_t rawScript = stubInfo->getStubRawWord(stubData, targetOffset);
        targetScript = reinterpret_cast<JSScript*>(rawScript);
        break;
      }
      case CacheOp::CallScriptedGetterResult: {
        auto args = reader.argsForCallScriptedGetterResult();
        ObjOperandId calleeOperand = args.calleeId;

        if (maybeCalleeOperand == calleeOperand) {
          data.emplace();
          data->target = targetScript;
          data->targetFunction = targetFunction;
          data->receiverOperand = args.receiverId;
          data->calleeOperand = calleeOperand;
          data->sameRealm = args.sameRealm;
          data->endOfSharedPrefix = opStart;
        }
        break;
      }
      case CacheOp::CallInlinedGetterResult: {
        auto args = reader.argsForCallInlinedGetterResult();
        ObjOperandId calleeOperand = args.calleeId;
        uintptr_t rawICScript =
            stubInfo->getStubRawWord(stubData, args.icScriptOffset);

        if (maybeCalleeOperand == calleeOperand) {
          data.emplace();
          data->target = targetScript;
          data->targetFunction = targetFunction;
          data->receiverOperand = args.receiverId;
          data->calleeOperand = calleeOperand;
          data->icScript = reinterpret_cast<ICScript*>(rawICScript);
          data->sameRealm = args.sameRealm;
          data->endOfSharedPrefix = opStart;
        }
        break;
      }
      default:
        if (!opInfo.transpile) {
          return mozilla::Nothing();
        }
        MOZ_ASSERT(data.isNothing());
        reader.skip(argLength);
        break;
    }
    MOZ_ASSERT(argStart + argLength == reader.currentPosition());
  }

  return data;
}

Maybe<InlinableSetterData> FindInlinableSetterData(ICCacheIRStub* stub) {
  Maybe<InlinableSetterData> data;

  const CacheIRStubInfo* stubInfo = stub->stubInfo();
  const uint8_t* stubData = stub->stubDataStart();

  ObjOperandId maybeCalleeOperand;
  JSScript* targetScript = nullptr;
  JSFunction* targetFunction = nullptr;

  CacheIRReader reader(stubInfo);
  while (reader.more()) {
    const uint8_t* opStart = reader.currentPosition();

    CacheOp op = reader.readOp();
    CacheIROpInfo opInfo = CacheIROpInfos[size_t(op)];
    uint32_t argLength = opInfo.argLength;
    mozilla::DebugOnly<const uint8_t*> argStart = reader.currentPosition();

    switch (op) {
      case CacheOp::LoadObject: {
        // If we load a constant object, remember it in case it's the callee.
        auto [resultOperand, objOffset] = reader.argsForLoadObject();
        uintptr_t rawObject = stubInfo->getStubRawWord(stubData, objOffset);
        JSObject* object = reinterpret_cast<JSObject*>(rawObject);
        if (object->is<JSFunction>() &&
            object->as<JSFunction>().hasBytecode()) {
          maybeCalleeOperand = resultOperand;
          targetFunction = &object->as<JSFunction>();
          targetScript = targetFunction->nonLazyScript();
        }
        break;
      }
      case CacheOp::GuardFunctionScript: {
        MOZ_ASSERT(data.isNothing());
        auto args = reader.argsForGuardFunctionScript();
        maybeCalleeOperand = args.objId;
        uint32_t targetOffset = args.expectedOffset;
        uintptr_t rawScript = stubInfo->getStubRawWord(stubData, targetOffset);
        targetScript = reinterpret_cast<JSScript*>(rawScript);
        break;
      }
      case CacheOp::CallScriptedSetter: {
        auto args = reader.argsForCallScriptedSetter();
        ObjOperandId calleeOperand = args.calleeId;

        if (maybeCalleeOperand == calleeOperand) {
          data.emplace();
          data->target = targetScript;
          data->targetFunction = targetFunction;
          data->receiverOperand = args.receiverId;
          data->calleeOperand = calleeOperand;
          data->rhsOperand = args.rhsId;
          data->sameRealm = args.sameRealm;
          data->endOfSharedPrefix = opStart;
        }
        break;
      }
      case CacheOp::CallInlinedSetter: {
        auto args = reader.argsForCallInlinedSetter();
        ObjOperandId calleeOperand = args.calleeId;
        uintptr_t rawICScript =
            stubInfo->getStubRawWord(stubData, args.icScriptOffset);

        if (maybeCalleeOperand == calleeOperand) {
          data.emplace();
          data->target = targetScript;
          data->targetFunction = targetFunction;
          data->receiverOperand = args.receiverId;
          data->calleeOperand = calleeOperand;
          data->rhsOperand = args.rhsId;
          data->icScript = reinterpret_cast<ICScript*>(rawICScript);
          data->sameRealm = args.sameRealm;
          data->endOfSharedPrefix = opStart;
        }
        break;
      }
      default:
        if (!opInfo.transpile) {
          return mozilla::Nothing();
        }
        MOZ_ASSERT(data.isNothing());
        reader.skip(argLength);
        break;
    }
    MOZ_ASSERT(argStart + argLength == reader.currentPosition());
  }

  return data;
}

// Return the maximum number of actual arguments that will be passed to the
// target function. This may be an overapproximation, for example when inlining
// js::fun_call we may omit an argument.
static uint32_t GetMaxCalleeNumActuals(BytecodeLocation loc) {
  switch (loc.getOp()) {
    case JSOp::GetProp:
    case JSOp::GetElem:
      // Getters do not pass arguments.
      return 0;

    case JSOp::SetProp:
    case JSOp::StrictSetProp:
      // Setters pass 1 argument.
      return 1;

    case JSOp::Call:
    case JSOp::CallContent:
    case JSOp::CallIgnoresRv:
    case JSOp::CallIter:
    case JSOp::CallContentIter:
    case JSOp::New:
    case JSOp::NewContent:
    case JSOp::SuperCall:
      return loc.getCallArgc();

    default:
      MOZ_CRASH("Unsupported op");
  }
}

/*static*/
bool TrialInliner::IsValidInliningOp(JSOp op) {
  switch (op) {
    case JSOp::GetProp:
    case JSOp::GetElem:
    case JSOp::SetProp:
    case JSOp::StrictSetProp:
    case JSOp::Call:
    case JSOp::CallContent:
    case JSOp::CallIgnoresRv:
    case JSOp::CallIter:
    case JSOp::CallContentIter:
    case JSOp::New:
    case JSOp::NewContent:
    case JSOp::SuperCall:
      return true;
    default:
      break;
  }
  return false;
}

/*static*/
bool TrialInliner::canInline(JSContext* cx, JSScript* script,
                             HandleScript caller, BytecodeLocation loc) {
  if (!script->hasJitScript()) {
    JitSpew(JitSpew_WarpTrialInlining, "SKIP: no JIT script");
    return false;
  }
  if (script->uninlineable()) {
    JitSpew(JitSpew_WarpTrialInlining, "SKIP: uninlineable flag");
    return false;
  }
  if (script->isGenerator() || script->isAsync()) {
    JitSpew(JitSpew_WarpTrialInlining, "SKIP: generator or async function");
    return false;
  }
  if (!script->canIonCompile()) {
    JitSpew(JitSpew_WarpTrialInlining, "SKIP: can't ion-compile");
    return false;
  }
  if (script->isDebuggee()) {
    JitSpew(JitSpew_WarpTrialInlining, "SKIP: is debuggee");
    return false;
  }
  // Don't inline cross-realm calls. We depend on this when discarding JIT code
  // per realm.
  if (script->realm() != caller->realm()) {
    JitSpew(JitSpew_WarpTrialInlining, "SKIP: cross-realm call");
    return false;
  }
  if (JitOptions.onlyInlineSelfHosted && !script->selfHosted()) {
    JitSpew(JitSpew_WarpTrialInlining, "SKIP: only inlining self hosted");
    return false;
  }
  if (!IsValidInliningOp(loc.getOp())) {
    JitSpew(JitSpew_WarpTrialInlining, "SKIP: non inlineable op");
    return false;
  }

  uint32_t maxCalleeNumActuals = GetMaxCalleeNumActuals(loc);
  if (maxCalleeNumActuals > ArgumentsObject::MaxInlinedArgs) {
    if (script->needsArgsObj()) {
      JitSpew(JitSpew_WarpTrialInlining,
              "SKIP: needs arguments object with %u actual args (maximum %u)",
              maxCalleeNumActuals, ArgumentsObject::MaxInlinedArgs);
      return false;
    }
    // The GetArgument(n) intrinsic in self-hosted code uses MGetInlinedArgument
    // too, so the same limit applies.
    if (script->usesArgumentsIntrinsics()) {
      JitSpew(JitSpew_WarpTrialInlining,
              "SKIP: uses GetArgument(i) with %u actual args (maximum %u)",
              maxCalleeNumActuals, ArgumentsObject::MaxInlinedArgs);
      return false;
    }
  }

  if (script->function() &&
      TooManyFormalArguments(script->function()->nargs())) {
    JitSpew(JitSpew_WarpTrialInlining, "SKIP: Too many formal arguments: %u",
            unsigned(script->function()->nargs()));
    return false;
  }

  if (TooManyFormalArguments(maxCalleeNumActuals)) {
    JitSpew(JitSpew_WarpTrialInlining, "SKIP: argc too large: %u",
            unsigned(loc.getCallArgc()));
    return false;
  }

  if (!script->hasBaselineScript() &&
      !script->jitScript()->ranBytecodeAnalysis()) {
    // If we don't have a baseline script, then we maybe haven't done
    // bytecode analysis yet. It's possible that the script is
    // uninlineable or can't be Ion compiled. Do bytecode analysis now.
    TempAllocator temp(&cx->tempLifoAlloc());
    BytecodeAnalysis analysis(temp, script);
    if (!analysis.init()) {
      JitSpew(JitSpew_WarpTrialInlining, "SKIP: OOM in bytecode analysis");
      cx->recoverFromOutOfMemory();
      return false;
    }
    bool result = true;
    if (analysis.isInliningDisabled()) {
      JitSpew(JitSpew_WarpTrialInlining, "SKIP: uninlineable flag");
      script->setUninlineable();
      result = false;
    }
    if (analysis.isIonDisabled()) {
      JitSpew(JitSpew_WarpTrialInlining, "SKIP: can't ion-compile");
      script->disableIon();
      result = false;
    }
    script->jitScript()->setRanBytecodeAnalysis();
    return result;
  }

  return true;
}

static bool ShouldUseMonomorphicInlining(JSScript* targetScript) {
  switch (JitOptions.monomorphicInlining) {
    case UseMonomorphicInlining::Default:
      // Use heuristics below.
      break;
    case UseMonomorphicInlining::Always:
      return true;
    case UseMonomorphicInlining::Never:
      return false;
  }

  JitScript* jitScript = targetScript->jitScript();
  ICScript* icScript = jitScript->icScript();

  // Check for any ICs which are not monomorphic. The observation here is that
  // trial inlining can help us a lot in cases where it lets us further
  // specialize a script. But if it's already monomorphic, it's unlikely that
  // we will see significant specialization wins from trial inlining, so we
  // can use a cheaper and simpler inlining strategy.
  for (size_t i = 0; i < icScript->numICEntries(); i++) {
    ICEntry& entry = icScript->icEntry(i);
    ICFallbackStub* fallback = icScript->fallbackStub(i);
    if (fallback->enteredCount() > 0 ||
        fallback->state().mode() != ICState::Mode::Specialized) {
      return false;
    }

    ICStub* firstStub = entry.firstStub();
    if (firstStub != fallback) {
      for (ICStub* next = firstStub->toCacheIRStub()->next(); next;
           next = next->maybeNext()) {
        if (next->enteredCount() != 0) {
          return false;
        }
      }
    }
  }

  return true;
}

TrialInliningDecision TrialInliner::getInliningDecision(JSScript* targetScript,
                                                        ICCacheIRStub* stub,
                                                        BytecodeLocation loc) {
#ifdef JS_JITSPEW
  if (JitSpewEnabled(JitSpew_WarpTrialInlining)) {
    UniqueChars funName;
    if (targetScript->function()) {
      if (JSAtom* atom = targetScript->function()->maybePartialDisplayAtom()) {
        funName = AtomToPrintableString(cx(), atom);
      }
    }

    JitSpew(JitSpew_WarpTrialInlining,
            "Inlining candidate JSOp::%s (offset=%u): callee script '%s' "
            "(%s:%u:%u)",
            CodeName(loc.getOp()), loc.bytecodeToOffset(script_),
            funName ? funName.get() : "<unnamed>", targetScript->filename(),
            targetScript->lineno(), targetScript->column().oneOriginValue());
    JitSpewIndent spewIndent(JitSpew_WarpTrialInlining);
  }
#endif

  if (!canInline(cx(), targetScript, script_, loc)) {
    return TrialInliningDecision::NoInline;
  }

  // Don't inline (direct) recursive calls. This still allows recursion if
  // called through another function (f => g => f).
  if (script_ == targetScript) {
    JitSpew(JitSpew_WarpTrialInlining, "SKIP: recursion");
    return TrialInliningDecision::NoInline;
  }

  // Don't inline if the callee has a loop that was hot enough to enter Warp
  // via OSR. This helps prevent getting stuck in Baseline code for a long time.
  if (targetScript->jitScript()->hadIonOSR()) {
    JitSpew(JitSpew_WarpTrialInlining, "SKIP: had OSR");
    return TrialInliningDecision::NoInline;
  }

  // Ensure the total bytecode size does not exceed ionMaxScriptSize.
  size_t newTotalSize =
      inliningRootTotalBytecodeSize() + targetScript->length();
  if (newTotalSize > JitOptions.ionMaxScriptSize) {
    JitSpew(JitSpew_WarpTrialInlining, "SKIP: total size too big");
    return TrialInliningDecision::NoInline;
  }

  uint32_t entryCount = stub->enteredCount();
  if (entryCount < JitOptions.inliningEntryThreshold) {
    JitSpew(JitSpew_WarpTrialInlining, "SKIP: Entry count is %u (minimum %u)",
            unsigned(entryCount), unsigned(JitOptions.inliningEntryThreshold));
    return TrialInliningDecision::NoInline;
  }

  if (!JitOptions.isSmallFunction(targetScript)) {
    if (!targetScript->isInlinableLargeFunction()) {
      JitSpew(JitSpew_WarpTrialInlining, "SKIP: Length is %u (maximum %u)",
              unsigned(targetScript->length()),
              unsigned(JitOptions.smallFunctionMaxBytecodeLength));
      return TrialInliningDecision::NoInline;
    }

    JitSpew(JitSpew_WarpTrialInlining,
            "INFO: Ignored length (%u) of InlinableLargeFunction",
            unsigned(targetScript->length()));
  }

  // Decide between trial inlining or monomorphic inlining.
  if (!ShouldUseMonomorphicInlining(targetScript)) {
    return TrialInliningDecision::Inline;
  }

  JitSpewIndent spewIndent(JitSpew_WarpTrialInlining);
  JitSpew(JitSpew_WarpTrialInlining, "SUCCESS: Inlined monomorphically");
  return TrialInliningDecision::MonomorphicInline;
}

ICScript* TrialInliner::createInlinedICScript(JSScript* targetScript,
                                              BytecodeLocation loc) {
  MOZ_ASSERT(targetScript->hasJitScript());

  InliningRoot* root = getOrCreateInliningRoot();
  if (!root) {
    return nullptr;
  }

  // We don't have to check for overflow here because we have already
  // successfully allocated an ICScript with this number of entries
  // when creating the JitScript for the target function, and we
  // checked for overflow then.
  uint32_t fallbackStubsOffset =
      sizeof(ICScript) + targetScript->numICEntries() * sizeof(ICEntry);
  uint32_t allocSize = fallbackStubsOffset +
                       targetScript->numICEntries() * sizeof(ICFallbackStub);

  void* raw = cx()->pod_malloc<uint8_t>(allocSize);
  MOZ_ASSERT(uintptr_t(raw) % alignof(ICScript) == 0);
  if (!raw) {
    return nullptr;
  }

  uint32_t initialWarmUpCount = JitOptions.trialInliningInitialWarmUpCount;

  uint32_t depth = icScript_->depth() + 1;
  UniquePtr<ICScript> inlinedICScript(
      new (raw) ICScript(initialWarmUpCount, fallbackStubsOffset, allocSize,
                         depth, targetScript->length(), root));

  inlinedICScript->initICEntries(cx(), targetScript);

  uint32_t pcOffset = loc.bytecodeToOffset(script_);
  ICScript* result = inlinedICScript.get();
  if (!icScript_->addInlinedChild(cx(), std::move(inlinedICScript), pcOffset)) {
    return nullptr;
  }
  MOZ_ASSERT(result->numICEntries() == targetScript->numICEntries());

  if (targetScript->needsFunctionEnvironmentObjects()) {
    gc::AutoMarkingLock lock(cx()->zone(), icScript_->markingLock());
    result->ensureEnvAllocSite(root->owningScript(), lock);
  }

  root->addToTotalBytecodeSize(targetScript->length());

  JitSpewIndent spewIndent(JitSpew_WarpTrialInlining);
  JitSpew(JitSpew_WarpTrialInlining,
          "SUCCESS: Outer ICScript: %p Inner ICScript: %p", icScript_, result);

  return result;
}

bool TrialInliner::maybeInlineCall(ICEntry& entry, ICFallbackStub* fallback,
                                   BytecodeLocation loc) {
  ICCacheIRStub* stub = maybeSingleStub(entry);
  if (!stub) {
#ifdef JS_JITSPEW
    if (fallback->numOptimizedStubs() > 1) {
      JitSpew(JitSpew_WarpTrialInlining,
              "Inlining candidate JSOp::%s (offset=%u):", CodeName(loc.getOp()),
              fallback->pcOffset());
      JitSpewIndent spewIndent(JitSpew_WarpTrialInlining);
      JitSpew(JitSpew_WarpTrialInlining, "SKIP: Polymorphic (%u stubs)",
              (unsigned)fallback->numOptimizedStubs());
    }
#endif
    return true;
  }

  MOZ_ASSERT(!icScript_->hasInlinedChild(fallback->pcOffset()));

  // Look for a CallScriptedFunction with a known target.
  Maybe<InlinableCallData> data = FindInlinableCallData(stub);
  if (data.isNothing()) {
    return true;
  }

  MOZ_ASSERT(!data->icScript);

  TrialInliningDecision inlining = getInliningDecision(data->target, stub, loc);
  // Decide whether to inline the target.
  if (inlining == TrialInliningDecision::NoInline) {
    return true;
  }

  if (inlining == TrialInliningDecision::MonomorphicInline) {
    fallback->setTrialInliningState(TrialInliningState::MonomorphicInlined);
    return true;
  }

  ICScript* newICScript = createInlinedICScript(data->target, loc);
  if (!newICScript) {
    return false;
  }

  CacheIRWriter writer(cx());
  Int32OperandId argcId(writer.setInputOperandId(0));
  cloneSharedPrefix(stub, data->endOfSharedPrefix, writer);

  if (data->isBound) {
    writer.callInlinedBoundFunction(
        data->calleeOperand, data->boundTargetOperand, argcId, data->callFlags,
        newICScript, data->numBoundArgs);
  } else {
    writer.callInlinedFunction(data->calleeOperand, argcId, newICScript,
                               data->callFlags,
                               ClampFixedArgc(loc.getCallArgc()));
  }

  return replaceICStub(entry, fallback, writer, CacheKind::Call);
}

bool TrialInliner::maybeInlineGetter(ICEntry& entry, ICFallbackStub* fallback,
                                     BytecodeLocation loc, CacheKind kind) {
  ICCacheIRStub* stub = maybeSingleStub(entry);
  if (!stub) {
    return true;
  }

  MOZ_ASSERT(!icScript_->hasInlinedChild(fallback->pcOffset()));

  Maybe<InlinableGetterData> data = FindInlinableGetterData(stub);
  if (data.isNothing()) {
    return true;
  }

  MOZ_ASSERT(!data->icScript);

  TrialInliningDecision inlining = getInliningDecision(data->target, stub, loc);
  // Decide whether to inline the target.
  if (inlining == TrialInliningDecision::NoInline) {
    return true;
  }

  if (inlining == TrialInliningDecision::MonomorphicInline) {
    fallback->setTrialInliningState(TrialInliningState::MonomorphicInlined);
    return true;
  }

  ICScript* newICScript = createInlinedICScript(data->target, loc);
  if (!newICScript) {
    return false;
  }

  CacheIRWriter writer(cx());
  ValOperandId valId(writer.setInputOperandId(0));
  if (kind == CacheKind::GetElem) {
    // Register the key operand.
    writer.setInputOperandId(1);
  }
  cloneSharedPrefix(stub, data->endOfSharedPrefix, writer);

  writer.callInlinedGetterResult(data->receiverOperand, data->calleeOperand,
                                 data->target->function(), newICScript,
                                 data->sameRealm);

  return replaceICStub(entry, fallback, writer, kind);
}

bool TrialInliner::maybeInlineSetter(ICEntry& entry, ICFallbackStub* fallback,
                                     BytecodeLocation loc, CacheKind kind) {
  ICCacheIRStub* stub = maybeSingleStub(entry);
  if (!stub) {
    return true;
  }

  MOZ_ASSERT(!icScript_->hasInlinedChild(fallback->pcOffset()));

  Maybe<InlinableSetterData> data = FindInlinableSetterData(stub);
  if (data.isNothing()) {
    return true;
  }

  MOZ_ASSERT(!data->icScript);

  TrialInliningDecision inlining = getInliningDecision(data->target, stub, loc);
  // Decide whether to inline the target.
  if (inlining == TrialInliningDecision::NoInline) {
    return true;
  }

  if (inlining == TrialInliningDecision::MonomorphicInline) {
    fallback->setTrialInliningState(TrialInliningState::MonomorphicInlined);
    return true;
  }

  ICScript* newICScript = createInlinedICScript(data->target, loc);
  if (!newICScript) {
    return false;
  }

  CacheIRWriter writer(cx());
  ValOperandId objValId(writer.setInputOperandId(0));
  ValOperandId rhsValId(writer.setInputOperandId(1));
  cloneSharedPrefix(stub, data->endOfSharedPrefix, writer);

  writer.callInlinedSetter(data->receiverOperand, data->calleeOperand,
                           data->target->function(), data->rhsOperand,
                           newICScript, data->sameRealm);

  return replaceICStub(entry, fallback, writer, kind);
}

bool TrialInliner::tryInlining() {
  uint32_t numICEntries = icScript_->numICEntries();
  BytecodeLocation startLoc = script_->location();

  for (uint32_t icIndex = 0; icIndex < numICEntries; icIndex++) {
    ICEntry& entry = icScript_->icEntry(icIndex);
    ICFallbackStub* fallback = icScript_->fallbackStub(icIndex);

    if (!TryFoldingStubs(cx(), fallback, script_, icScript_)) {
      return false;
    }

    BytecodeLocation loc =
        startLoc + BytecodeLocationOffset(fallback->pcOffset());
    JSOp op = loc.getOp();
    switch (op) {
      case JSOp::Call:
      case JSOp::CallContent:
      case JSOp::CallIgnoresRv:
      case JSOp::CallIter:
      case JSOp::CallContentIter:
      case JSOp::New:
      case JSOp::NewContent:
      case JSOp::SuperCall:
        if (!maybeInlineCall(entry, fallback, loc)) {
          return false;
        }
        break;
      case JSOp::GetProp:
        if (!maybeInlineGetter(entry, fallback, loc, CacheKind::GetProp)) {
          return false;
        }
        break;
      case JSOp::GetElem:
        if (!maybeInlineGetter(entry, fallback, loc, CacheKind::GetElem)) {
          return false;
        }
        break;
      case JSOp::SetProp:
      case JSOp::StrictSetProp:
        if (!maybeInlineSetter(entry, fallback, loc, CacheKind::SetProp)) {
          return false;
        }
        break;
      default:
        break;
    }
  }

  return true;
}

InliningRoot* TrialInliner::maybeGetInliningRoot() const {
  if (auto* root = icScript_->inliningRoot()) {
    return root;
  }

  MOZ_ASSERT(!icScript_->isInlined());
  return script_->jitScript()->inliningRoot();
}

InliningRoot* TrialInliner::getOrCreateInliningRoot() {
  if (auto* root = maybeGetInliningRoot()) {
    return root;
  }
  return script_->jitScript()->getOrCreateInliningRoot(cx(), script_);
}

size_t TrialInliner::inliningRootTotalBytecodeSize() const {
  if (auto* root = maybeGetInliningRoot()) {
    return root->totalBytecodeSize();
  }
  return script_->length();
}

bool InliningRoot::addInlinedScript(UniquePtr<ICScript> icScript) {
  return inlinedScripts_.append(std::move(icScript));
}

void InliningRoot::trace(JSTracer* trc) {
  TraceEdge(trc, &owningScript_, "inlining-root-owning-script");
  for (auto& inlinedScript : inlinedScripts_) {
    inlinedScript->trace(trc);
  }
}

bool InliningRoot::traceWeak(JSTracer* trc) {
  bool allSurvived = true;
  for (auto& inlinedScript : inlinedScripts_) {
    if (!inlinedScript->traceWeak(trc)) {
      allSurvived = false;
    }
  }
  return allSurvived;
}

void InliningRoot::purgeInactiveICScripts() {
  mozilla::DebugOnly<uint32_t> totalSize = owningScript_->length();

  for (auto& inlinedScript : inlinedScripts_) {
    if (inlinedScript->active()) {
      totalSize += inlinedScript->bytecodeSize();
    } else {
      MOZ_ASSERT(inlinedScript->bytecodeSize() < totalBytecodeSize_);
      totalBytecodeSize_ -= inlinedScript->bytecodeSize();
    }
  }

  MOZ_ASSERT(totalBytecodeSize_ == totalSize);

  Zone* zone = owningScript_->zone();

  inlinedScripts_.eraseIf([zone](auto& inlinedScript) {
    if (inlinedScript->active()) {
      return false;
    }
    inlinedScript->prepareForDestruction(zone);
    return true;
  });
}

}  // namespace jit
}  // namespace js
