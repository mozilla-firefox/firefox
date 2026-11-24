/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/DebugOnly.h"
#include "mozilla/Sprintf.h"

#include "gc/GC.h"
#include "gc/PublicIterators.h"
#include "jit/BaselineJIT.h"
#include "jit/JitcodeMap.h"
#include "jit/JitRuntime.h"
#include "jit/JSJitFrameIter.h"
#include "jit/PerfSpewer.h"
#include "js/experimental/SourceHook.h"
#include "vm/FrameIter.h"  // js::OnlyJSJitFrameIter
#include "vm/JitActivation.h"
#include "vm/JSScript.h"
#include "vm/MutexIDs.h"

#include "gc/Marking-inl.h"
#include "jit/JSJitFrameIter-inl.h"
#include "vm/GeckoProfiler-inl.h"

using namespace js;
using mozilla::Utf8Unit;

GeckoProfilerThread::GeckoProfilerThread()
    : profilingStack_(nullptr), profilingStackIfEnabled_(nullptr) {}

GeckoProfilerRuntime::GeckoProfilerRuntime(JSRuntime* rt)
    : rt(rt),
      scriptSources_(mutexid::GeckoProfilerScriptSources),
      slowAssertions(false),
      enabled_(false),
      eventMarker_(nullptr),
      intervalMarker_(nullptr),
      flowMarker_(nullptr),
      terminatingFlowMarker_(nullptr) {
  MOZ_ASSERT(rt != nullptr);
}

void GeckoProfilerThread::setProfilingStack(ProfilingStack* profilingStack,
                                            bool enabled) {
  profilingStack_ = profilingStack;
  profilingStackIfEnabled_ = enabled ? profilingStack : nullptr;
}

void GeckoProfilerRuntime::setEventMarker(void (*fn)(mozilla::MarkerCategory,
                                                     const char*,
                                                     const char*)) {
  eventMarker_ = fn;
}

void GeckoProfilerRuntime::setIntervalMarker(void (*fn)(
    mozilla::MarkerCategory, const char*, mozilla::TimeStamp, const char*)) {
  intervalMarker_ = fn;
}

void GeckoProfilerRuntime::setFlowMarker(void (*fn)(mozilla::MarkerCategory,
                                                    const char*, uint64_t)) {
  flowMarker_ = fn;
}

void GeckoProfilerRuntime::setTerminatingFlowMarker(
    void (*fn)(mozilla::MarkerCategory, const char*, uint64_t)) {
  terminatingFlowMarker_ = fn;
}

bool GeckoProfilerRuntime::insertScriptSource(ScriptSource* scriptSource) {
  MOZ_ASSERT(scriptSource);
  auto guard = scriptSources_.writeLock();
  if (!enabled_) {
    return true;
  }

  return guard->put(scriptSource);
}

// Get a pointer to the top-most profiling frame, given the exit frame pointer.
static jit::JitFrameLayout* GetTopProfilingJitFrame(jit::JitActivation* act) {
  // If there is no exit frame set, just return.
  if (!act->hasExitFP()) {
    return nullptr;
  }

  // Skip wasm frames that might be in the way.
  OnlyJSJitFrameIter iter(act);
  if (iter.done()) {
    return nullptr;
  }

  // Skip if the activation has no JS frames. This can happen if there's only a
  // TrampolineNative frame because these are skipped by the profiling frame
  // iterator.
  jit::JSJitProfilingFrameIterator jitIter(
      (jit::CommonFrameLayout*)iter.frame().fp());
  if (jitIter.done()) {
    return nullptr;
  }

  return jitIter.framePtr();
}

void GeckoProfilerRuntime::enable(bool enabled) {
  JSContext* cx = rt->mainContextFromAnyThread();
  MOZ_ASSERT(cx->geckoProfiler().infraInstalled());

  if (enabled_ == enabled) {
    return;
  }

  /*
   * Ensure all future generated code will be instrumented, or that all
   * currently instrumented code is discarded
   */
  ReleaseAllJITCode(rt->gcContext());

  // This function is called when the Gecko profiler makes a new Sampler
  // (and thus, a new circular buffer). Set all current entries in the
  // JitcodeGlobalTable as expired and reset the buffer range start.
  if (rt->hasJitRuntime() && rt->jitRuntime()->hasJitcodeGlobalTable()) {
    rt->jitRuntime()->getJitcodeGlobalTable()->setAllEntriesAsExpired();
  }
  rt->setProfilerSampleBufferRangeStart(0);

  // Ensure that lastProfilingFrame is null for the main thread.
  if (cx->jitActivation) {
    cx->jitActivation->setLastProfilingFrame(nullptr);
    cx->jitActivation->setLastProfilingCallSite(nullptr);
  }

  enabled_ = enabled;

  scriptSources_.writeLock()->clear();

  /* Toggle Gecko Profiler-related jumps on baseline jitcode.
   * The call to |ReleaseAllJITCode| above will release most baseline jitcode,
   * but not jitcode for scripts with active frames on the stack.  These scripts
   * need to have their profiler state toggled so they behave properly.
   */
  jit::ToggleBaselineProfiling(cx, enabled);

  // Update lastProfilingFrame to point to the top-most JS jit-frame currently
  // on stack.
  if (cx->jitActivation) {
    // Walk through all activations, and set their lastProfilingFrame
    // appropriately.
    if (enabled) {
      jit::JitActivation* jitActivation = cx->jitActivation;
      while (jitActivation) {
        auto* lastProfilingFrame = GetTopProfilingJitFrame(jitActivation);
        jitActivation->setLastProfilingFrame(lastProfilingFrame);
        jitActivation->setLastProfilingCallSite(nullptr);
        jitActivation = jitActivation->prevJitActivation();
      }
    } else {
      jit::JitActivation* jitActivation = cx->jitActivation;
      while (jitActivation) {
        jitActivation->setLastProfilingFrame(nullptr);
        jitActivation->setLastProfilingCallSite(nullptr);
        jitActivation = jitActivation->prevJitActivation();
      }
    }
  }

  // WebAssembly code does not need to be released, but profiling string
  // labels have to be generated so that they are available during async
  // profiling stack iteration.
  for (RealmsIter r(rt); !r.done(); r.next()) {
    r->wasm.ensureProfilingLabels(enabled);
  }

#ifdef JS_STRUCTURED_SPEW
  // Enable the structured spewer if the environment variable is set.
  if (enabled) {
    cx->spewer().enableSpewing();
  } else {
    cx->spewer().disableSpewing();
  }
#endif
}

/* Lookup the string for the function/script, creating one if necessary */
const char* GeckoProfilerRuntime::profileString(JSContext* cx,
                                                BaseScript* script) {
  JS::Zone* zone = script->zone();
  if (!zone->profilerStrings) {
    auto map = cx->make_unique<JS::WeakCache<ProfileStringMap>>(zone);
    if (!map) {
      return nullptr;
    }
    zone->profilerStrings = std::move(map);
  }

  ProfileStringMap& map = zone->profilerStrings->get();
  ProfileStringMap::AddPtr ptr = map.lookupForAdd(script);

  if (!ptr) {
    UniqueChars str = allocProfileString(cx, script);
    if (!str) {
      return nullptr;
    }
    MOZ_ASSERT(script->hasBytecode());
    if (!map.add(ptr, script, std::move(str))) {
      ReportOutOfMemory(cx);
      return nullptr;
    }
  }

  return ptr->value().get();
}

void GeckoProfilerRuntime::markEvent(const char* event, const char* details,
                                     JS::ProfilingCategoryPair jsPair) {
  MOZ_ASSERT(enabled());
  if (eventMarker_) {
    JS::AutoSuppressGCAnalysis nogc;
    mozilla::MarkerCategory category(
        static_cast<mozilla::baseprofiler::ProfilingCategoryPair>(jsPair));
    eventMarker_(category, event, details);
  }
}

void GeckoProfilerRuntime::markInterval(const char* event,
                                        mozilla::TimeStamp start,
                                        const char* details,
                                        JS::ProfilingCategoryPair jsPair) {
  MOZ_ASSERT(enabled());
  if (intervalMarker_) {
    JS::AutoSuppressGCAnalysis nogc;
    mozilla::MarkerCategory category(
        static_cast<mozilla::baseprofiler::ProfilingCategoryPair>(jsPair));
    intervalMarker_(category, event, start, details);
  }
}

void GeckoProfilerRuntime::markFlow(const char* markerName, uint64_t flowId,
                                    JS::ProfilingCategoryPair jsPair) {
  MOZ_ASSERT(enabled());
  if (flowMarker_) {
    JS::AutoSuppressGCAnalysis nogc;
    mozilla::MarkerCategory category(
        static_cast<mozilla::baseprofiler::ProfilingCategoryPair>(jsPair));
    flowMarker_(category, markerName, flowId);
  }
}

void GeckoProfilerRuntime::markTerminatingFlow(
    const char* markerName, uint64_t flowId, JS::ProfilingCategoryPair jsPair) {
  MOZ_ASSERT(enabled());
  if (terminatingFlowMarker_) {
    JS::AutoSuppressGCAnalysis nogc;
    mozilla::MarkerCategory category(
        static_cast<mozilla::baseprofiler::ProfilingCategoryPair>(jsPair));
    terminatingFlowMarker_(category, markerName, flowId);
  }
}

bool GeckoProfilerThread::enter(JSContext* cx, JSScript* script) {
  const char* dynamicString =
      cx->runtime()->geckoProfiler().profileString(cx, script);
  if (dynamicString == nullptr) {
    return false;
  }

  if (!cx->runtime()->geckoProfiler().insertScriptSource(
          script->scriptSource())) {
    ReportOutOfMemory(cx);
    return false;
  }

#ifdef DEBUG
  // In debug builds, assert the JS profiling stack frames already on the
  // stack have a non-null pc. Only look at the top frames to avoid quadratic
  // behavior.
  uint32_t sp = profilingStack_->stackPointer;
  if (sp > 0 && sp - 1 < profilingStack_->stackCapacity()) {
    size_t start = (sp > 4) ? sp - 4 : 0;
    for (size_t i = start; i < sp - 1; i++) {
      MOZ_ASSERT_IF(profilingStack_->frames[i].isJsFrame(),
                    profilingStack_->frames[i].pc());
    }
  }
#endif

  profilingStack_->pushJsFrame(
      "", dynamicString, script, script->code(),
      script->realm()->creationOptions().profilerRealmID(),
      script->scriptSource()->id());
  return true;
}

void GeckoProfilerThread::exit(JSContext* cx, JSScript* script) {
  profilingStack_->pop();

#ifdef DEBUG
  /* Sanity check to make sure push/pop balanced */
  uint32_t sp = profilingStack_->stackPointer;
  if (sp < profilingStack_->stackCapacity()) {
    JSRuntime* rt = script->runtimeFromMainThread();
    const char* dynamicString = rt->geckoProfiler().profileString(cx, script);
    /* Can't fail lookup because we should already be in the set */
    MOZ_ASSERT(dynamicString);

    // Bug 822041
    if (!profilingStack_->frames[sp].isJsFrame()) {
      fprintf(stderr, "--- ABOUT TO FAIL ASSERTION ---\n");
      fprintf(stderr, " frames=%p size=%u/%u\n", (void*)profilingStack_->frames,
              profilingStack_->stackPointer.load(),
              profilingStack_->stackCapacity());
      for (int32_t i = sp; i >= 0; i--) {
        ProfilingStackFrame& frame = profilingStack_->frames[i];
        if (frame.isJsFrame()) {
          fprintf(stderr, "  [%d] JS %s\n", i, frame.dynamicString());
        } else {
          fprintf(stderr, "  [%d] Label %s\n", i, frame.dynamicString());
        }
      }
    }

    ProfilingStackFrame& frame = profilingStack_->frames[sp];
    MOZ_ASSERT(frame.isJsFrame());
    MOZ_ASSERT(frame.script() == script);
    MOZ_ASSERT(strcmp((const char*)frame.dynamicString(), dynamicString) == 0);
  }
#endif
}

/*
 * Serializes the script/function pair into a "descriptive string" which is
 * allowed to fail. This function cannot trigger a GC because it could finalize
 * some scripts, resize the hash table of profile strings, and invalidate the
 * AddPtr held while invoking allocProfileString.
 */
/* static */
UniqueChars GeckoProfilerRuntime::allocProfileString(JSContext* cx,
                                                     BaseScript* script) {
  // Note: this profiler string is regexp-matched by
  // profiler code. Most recently at
  // https://github.com/firefox-devtools/profiler/blob/8f4935823ec06507c3125d4c6c1e78eef31361f3/src/profile-logic/process-profile.ts#L407-L415

  // If the script has a function, try calculating its name.
  JSAtom* name = nullptr;
  size_t nameLength = 0;
  JSFunction* func = script->function();
  if (func && func->fullDisplayAtom()) {
    name = func->fullDisplayAtom();
    nameLength = JS::GetDeflatedUTF8StringLength(name);
  }

  // Calculate filename length. We cap this to a reasonable limit to avoid
  // performance impact of strlen/alloc/memcpy.
  constexpr size_t MaxFilenameLength = 200;
  const char* filenameStr = script->filename() ? script->filename() : "(null)";
  size_t filenameLength = js_strnlen(filenameStr, MaxFilenameLength);

  // Calculate line + column length.
  bool hasLineAndColumn = false;
  size_t lineAndColumnLength = 0;
  char lineAndColumnStr[30];
  if (name || script->isFunction() || script->isForEval()) {
    lineAndColumnLength =
        SprintfLiteral(lineAndColumnStr, "%u:%u", script->lineno(),
                       script->column().oneOriginValue());
    hasLineAndColumn = true;
  }

  // Full profile string for scripts with functions is:
  //      FuncName (FileName:Lineno:Column)
  // Full profile string for scripts without functions is:
  //      FileName:Lineno:Column
  // Full profile string for scripts without functions and without lines is:
  //      FileName

  // Calculate full string length.
  size_t fullLength = 0;
  if (name) {
    MOZ_ASSERT(hasLineAndColumn);
    fullLength = nameLength + 2 + filenameLength + 1 + lineAndColumnLength + 1;
  } else if (hasLineAndColumn) {
    fullLength = filenameLength + 1 + lineAndColumnLength;
  } else {
    fullLength = filenameLength;
  }

  // Allocate string.
  UniqueChars str(cx->pod_malloc<char>(fullLength + 1));
  if (!str) {
    return nullptr;
  }

  size_t cur = 0;

  // Fill string with function name if needed.
  if (name) {
    mozilla::DebugOnly<size_t> written = JS::DeflateStringToUTF8Buffer(
        name, mozilla::Span(str.get() + cur, nameLength));
    MOZ_ASSERT(written == nameLength);
    cur += nameLength;
    str[cur++] = ' ';
    str[cur++] = '(';
  }

  // Fill string with filename chars.
  memcpy(str.get() + cur, filenameStr, filenameLength);
  cur += filenameLength;

  // Fill line + column chars.
  if (hasLineAndColumn) {
    str[cur++] = ':';
    memcpy(str.get() + cur, lineAndColumnStr, lineAndColumnLength);
    cur += lineAndColumnLength;
  }

  // Terminal ')' if necessary.
  if (name) {
    str[cur++] = ')';
  }

  MOZ_ASSERT(cur == fullLength);
  str[cur] = 0;

  return str;
}

void GeckoProfilerThread::trace(JSTracer* trc) {
  if (profilingStack_) {
    size_t size = profilingStack_->stackSize();
    for (size_t i = 0; i < size; i++) {
      profilingStack_->frames[i].trace(trc);
    }
  }
}

size_t GeckoProfilerRuntime::stringsCount() {
  size_t count = 0;
  for (AllZonesIter zone(rt); !zone.done(); zone.next()) {
    if (zone->profilerStrings) {
      count += zone->profilerStrings->get().count();
    }
  }
  return count;
}

void GeckoProfilerRuntime::stringsReset() {
  for (AllZonesIter zone(rt); !zone.done(); zone.next()) {
    if (zone->profilerStrings) {
      zone->profilerStrings->get().clear();
    }
  }
}

// Get all script sources as a list of ProfilerJSSourceData.
js::ProfilerJSSources GeckoProfilerRuntime::getProfilerScriptSources(
    bool gatherSourceText) {
  js::ProfilerJSSources result;

  auto guard = scriptSources_.readLock();
  for (auto iter = guard->iter(); !iter.done(); iter.next()) {
    const RefPtr<ScriptSource>& scriptSource = iter.get();
    MOZ_ASSERT(scriptSource);

    ScriptSource::DataReader sourceReader(scriptSource);

    uint32_t sourceId = scriptSource->id();

    // Get filename for all source types. Create single copy to be moved.
    const char* filename = scriptSource->filename();
    size_t filenameLen = 0;
    JS::UniqueChars filenameCopy;
    if (filename) {
      filenameLen = strlen(filename);
      filenameCopy.reset(static_cast<char*>(js_malloc(filenameLen + 1)));
      if (filenameCopy) {
        strcpy(filenameCopy.get(), filename);
      }
    }

    // Get the start line and column for inline scripts.
    uint32_t startLine = scriptSource->startLine();
    uint32_t startColumn = scriptSource->startColumn().oneOriginValue();

    // Get sourceMapURL for all source types. Create single copy to be moved.
    const char16_t* sourceMapURL = nullptr;
    size_t sourceMapURLLen = 0;
    JS::UniqueTwoByteChars sourceMapURLCopy;
    if (scriptSource->hasSourceMapURL()) {
      sourceMapURL = scriptSource->sourceMapURL();
      sourceMapURLLen = js_strlen(sourceMapURL);
      sourceMapURLCopy.reset(static_cast<char16_t*>(
          js_malloc((sourceMapURLLen + 1) * sizeof(char16_t))));
      if (sourceMapURLCopy) {
        js_memcpy(sourceMapURLCopy.get(), sourceMapURL,
                  sourceMapURLLen * sizeof(char16_t));
        sourceMapURLCopy[sourceMapURLLen] = 0;
      } else {
        sourceMapURLLen = 0;
      }
    }

    // If not gathering source text, just store metadata
    if (!gatherSourceText) {
      (void)result.emplaceBack(sourceId, std::move(filenameCopy), filenameLen,
                               startLine, startColumn,
                               std::move(sourceMapURLCopy), sourceMapURLLen);
      continue;
    }

    if (sourceReader.isRetrievable()) {
      (void)result.append(ProfilerJSSourceData::CreateRetrievableFile(
          sourceId, std::move(filenameCopy), filenameLen, startLine,
          startColumn, std::move(sourceMapURLCopy), sourceMapURLLen));
      continue;
    }

    if (!sourceReader.hasSourceText()) {
      (void)result.emplaceBack(sourceId, std::move(filenameCopy), filenameLen,
                               startLine, startColumn,
                               std::move(sourceMapURLCopy), sourceMapURLLen);
      continue;
    }

    size_t sourceLength = sourceReader->length();
    if (sourceLength == 0) {
      (void)result.emplaceBack(sourceId, JS::UniqueTwoByteChars(), 0,
                               std::move(filenameCopy), filenameLen, startLine,
                               startColumn, std::move(sourceMapURLCopy),
                               sourceMapURLLen);
      continue;
    }

    SubstringCharsResult sourceResult(JS::UniqueChars(nullptr));
    size_t charsLength = 0;

    if (scriptSource->shouldUnwrapEventHandlerBody()) {
      sourceResult =
          sourceReader->functionBodyStringChars(scriptSource, &charsLength);

      if (charsLength == 0) {
        (void)result.emplaceBack(sourceId, JS::UniqueTwoByteChars(), 0,
                                 std::move(filenameCopy), filenameLen,
                                 startLine, startColumn,
                                 std::move(sourceMapURLCopy), sourceMapURLLen);
        continue;
      }
    } else {
      sourceResult = sourceReader->substringChars(0, sourceLength);
      charsLength = sourceLength;
    }

    // Convert SubstringCharsResult to ProfilerJSSourceData.
    // Note: The returned buffers are NOT null-terminated. The length is
    // tracked separately in charsLength and passed to ProfilerJSSourceData.
    if (sourceResult.is<JS::UniqueChars>()) {
      auto& utf8Chars = sourceResult.as<JS::UniqueChars>();
      if (!utf8Chars) {
        continue;
      }
      (void)result.emplaceBack(sourceId, std::move(utf8Chars), charsLength,
                               std::move(filenameCopy), filenameLen, startLine,
                               startColumn, std::move(sourceMapURLCopy),
                               sourceMapURLLen);
    } else {
      auto& utf16Chars = sourceResult.as<JS::UniqueTwoByteChars>();
      if (!utf16Chars) {
        continue;
      }
      (void)result.emplaceBack(sourceId, std::move(utf16Chars), charsLength,
                               std::move(filenameCopy), filenameLen, startLine,
                               startColumn, std::move(sourceMapURLCopy),
                               sourceMapURLLen);
    }
  }

  return result;
}

void ProfilingStackFrame::trace(JSTracer* trc) {
  if (isJsFrame()) {
    JSScript* s = rawScript();
    TraceRoot(trc, &s, "ProfilingStackFrame script");
    spOrScript = s;
  }
}

GeckoProfilerBaselineOSRMarker::GeckoProfilerBaselineOSRMarker(
    JSContext* cx, bool hasProfilerFrame)
    : profiler(&cx->geckoProfiler()) {
  if (!hasProfilerFrame || !cx->runtime()->geckoProfiler().enabled()) {
    profiler = nullptr;
    return;
  }

  uint32_t sp = profiler->profilingStack_->stackPointer;
  if (sp >= profiler->profilingStack_->stackCapacity()) {
    profiler = nullptr;
    return;
  }

  spBefore_ = sp;
  if (sp == 0) {
    return;
  }

  ProfilingStackFrame& frame = profiler->profilingStack_->frames[sp - 1];
  MOZ_ASSERT(!frame.isOSRFrame());
  frame.setIsOSRFrame(true);
}

GeckoProfilerBaselineOSRMarker::~GeckoProfilerBaselineOSRMarker() {
  if (profiler == nullptr) {
    return;
  }

  uint32_t sp = profiler->stackPointer();
  MOZ_ASSERT(spBefore_ == sp);
  if (sp == 0) {
    return;
  }

  ProfilingStackFrame& frame = profiler->stack()[sp - 1];
  MOZ_ASSERT(frame.isOSRFrame());
  frame.setIsOSRFrame(false);
}

JS_PUBLIC_API JSScript* ProfilingStackFrame::script() const {
  MOZ_ASSERT(isJsFrame());
  auto* script = reinterpret_cast<JSScript*>(spOrScript.operator void*());
  if (!script) {
    return nullptr;
  }

  // While profiling is suppressed the script pointer may be unsafe to use, as
  // it could be in the middle of being relocated by a compacting GC (although
  // it's still OK to get the runtime from it). A suppression site that
  // guarantees scripts are not moving (currently only minor GC) opts in to
  // allowing access here so the profiler can still resolve line/column
  // attribution for the affected frames.
  //
  // The returned script is only safe to use for tenured data (e.g. its
  // filename, line/column via pc()). It may still be reachable during a minor
  // GC, so nursery-allocated objects hanging off it, notably its JSFunction,
  // must not be dereferenced here. Go through function(), which keeps the
  // broader isProfilerSamplingEnabled() guard.
  JSContext* cx = script->runtimeFromAnyThread()->mainContextFromAnyThread();
  if (!cx->isProfilerSamplingEnabled() && !cx->allowProfilerScriptAccess()) {
    return nullptr;
  }

  MOZ_ASSERT(!IsForwarded(script));
  return script;
}

JS_PUBLIC_API JSFunction* ProfilingStackFrame::function() const {
  JSScript* script = this->script();
  if (!script) {
    return nullptr;
  }
  // JSFunctions can live in the nursery and so may move during minor GC.
  // Fall back to the broader sample-suppression flag here so callers don't
  // dereference a function pointer while objects are being relocated.
  JSContext* cx = script->runtimeFromAnyThread()->mainContextFromAnyThread();
  if (!cx->isProfilerSamplingEnabled()) {
    return nullptr;
  }
  return script->function();
}

JS_PUBLIC_API jsbytecode* ProfilingStackFrame::pc() const {
  MOZ_ASSERT(isJsFrame());
  if (pcOffsetIfJS_ == NullPCOffset) {
    return nullptr;
  }

  JSScript* script = this->script();
  return script ? script->offsetToPC(pcOffsetIfJS_) : nullptr;
}

/* static */
int32_t ProfilingStackFrame::pcToOffset(JSScript* aScript, jsbytecode* aPc) {
  return aPc ? aScript->pcToOffset(aPc) : NullPCOffset;
}

void ProfilingStackFrame::setPC(jsbytecode* pc) {
  MOZ_ASSERT(isJsFrame());
  JSScript* script = this->script();
  // This should not be called while script access is suppressed (see
  // script()).
  MOZ_ASSERT(script);
  pcOffsetIfJS_ = pcToOffset(script, pc);
}

JS_PUBLIC_API uint32_t ProfilingStackFrame::sourceId() const {
  return sourceId_;
}

JS_PUBLIC_API void js::SetContextProfilingStack(
    JSContext* cx, ProfilingStack* profilingStack) {
  cx->geckoProfiler().setProfilingStack(
      profilingStack, cx->runtime()->geckoProfiler().enabled());
}

JS_PUBLIC_API void js::EnableContextProfilingStack(JSContext* cx,
                                                   bool enabled) {
  // Toggling leaves the engine transiently half-initialized (e.g. a Baseline
  // Interpreter frame whose JitScript has no profiler label string yet), so the
  // SamplerThread must not walk this thread's JS stack until we're done.
  AutoSuppressProfilerSampling suppressSampling(cx);
  cx->geckoProfiler().enable(enabled);
  cx->runtime()->geckoProfiler().enable(enabled);
}

JS_PUBLIC_API void js::RegisterContextProfilerMarkers(
    JSContext* cx,
    void (*eventMarker)(mozilla::MarkerCategory, const char*, const char*),
    void (*intervalMarker)(mozilla::MarkerCategory, const char*,
                           mozilla::TimeStamp, const char*),
    void (*flowMarker)(mozilla::MarkerCategory, const char*, uint64_t),
    void (*terminatingFlowMarker)(mozilla::MarkerCategory, const char*,
                                  uint64_t)) {
  MOZ_ASSERT(cx->runtime()->geckoProfiler().enabled());
  cx->runtime()->geckoProfiler().setEventMarker(eventMarker);
  cx->runtime()->geckoProfiler().setIntervalMarker(intervalMarker);
  cx->runtime()->geckoProfiler().setFlowMarker(flowMarker);
  cx->runtime()->geckoProfiler().setTerminatingFlowMarker(
      terminatingFlowMarker);
}

JS_PUBLIC_API js::ProfilerJSSources js::GetProfilerScriptSources(
    JSRuntime* rt, bool gatherSourceText) {
  return rt->geckoProfiler().getProfilerScriptSources(gatherSourceText);
}

JS_PUBLIC_API ProfilerJSSourceData
js::RetrieveProfilerSourceContent(JSContext* cx, const char* filename) {
  MOZ_ASSERT(filename && strlen(filename));
  if (!cx) {
    return ProfilerJSSourceData();  // Return unavailable
  }

  // Check if source hook is available
  if (!cx->runtime()->sourceHook.ref()) {
    return ProfilerJSSourceData();  // Return unavailable
  }

  size_t sourceLength = 0;
  char* utf8Source = nullptr;

  bool loadSuccess = cx->runtime()->sourceHook->load(
      cx, filename, nullptr, &utf8Source, &sourceLength);

  if (!loadSuccess) {
    // Clear the pending exception that have been set by the source hook.
    JS_ClearPendingException(cx);
    return ProfilerJSSourceData();  // Return unavailable
  }

  if (utf8Source) {
    return ProfilerJSSourceData(JS::UniqueChars(utf8Source), sourceLength);
  }

  // Hook returned success but no source data. Return unavailable.
  return ProfilerJSSourceData();
}

AutoSuppressProfilerSampling::AutoSuppressProfilerSampling(
    JSContext* cx, ProfilerScriptAccess scriptAccess)
    : cx_(cx),
      previouslyEnabled_(cx->isProfilerSamplingEnabled()),
      previousScriptAccess_(cx->allowProfilerScriptAccess()) {
  if (previouslyEnabled_) {
    cx_->disableProfilerSampling();
  }
  cx_->setAllowProfilerScriptAccess(scriptAccess ==
                                    ProfilerScriptAccess::Allow);
}

AutoSuppressProfilerSampling::~AutoSuppressProfilerSampling() {
  if (previouslyEnabled_) {
    cx_->enableProfilerSampling();
  }
  cx_->setAllowProfilerScriptAccess(previousScriptAccess_);
}

namespace JS {

// clang-format off

// ProfilingSubcategory_X:
// One enum for each category X, listing that category's subcategories. This
// allows the sProfilingCategoryInfo macro construction below to look up a
// per-category index for a subcategory.
#define SUBCATEGORY_ENUMS_BEGIN_CATEGORY(name, labelAsString, color) \
  enum class ProfilingSubcategory_##name : uint32_t {
#define SUBCATEGORY_ENUMS_SUBCATEGORY(category, name, labelAsString) \
    name,
#define SUBCATEGORY_ENUMS_END_CATEGORY \
  };
MOZ_PROFILING_CATEGORY_LIST(SUBCATEGORY_ENUMS_BEGIN_CATEGORY,
                            SUBCATEGORY_ENUMS_SUBCATEGORY,
                            SUBCATEGORY_ENUMS_END_CATEGORY)
#undef SUBCATEGORY_ENUMS_BEGIN_CATEGORY
#undef SUBCATEGORY_ENUMS_SUBCATEGORY
#undef SUBCATEGORY_ENUMS_END_CATEGORY

// sProfilingCategoryPairInfo:
// A list of ProfilingCategoryPairInfos with the same order as
// ProfilingCategoryPair, which can be used to map a ProfilingCategoryPair to
// its information.
#define CATEGORY_INFO_BEGIN_CATEGORY(name, labelAsString, color)
#define CATEGORY_INFO_SUBCATEGORY(category, name, labelAsString) \
  {ProfilingCategory::category,                                  \
   uint32_t(ProfilingSubcategory_##category::name), labelAsString},
#define CATEGORY_INFO_END_CATEGORY
const ProfilingCategoryPairInfo sProfilingCategoryPairInfo[] = {
  MOZ_PROFILING_CATEGORY_LIST(CATEGORY_INFO_BEGIN_CATEGORY,
                              CATEGORY_INFO_SUBCATEGORY,
                              CATEGORY_INFO_END_CATEGORY)
};
#undef CATEGORY_INFO_BEGIN_CATEGORY
#undef CATEGORY_INFO_SUBCATEGORY
#undef CATEGORY_INFO_END_CATEGORY

// clang-format on

JS_PUBLIC_API const ProfilingCategoryPairInfo& GetProfilingCategoryPairInfo(
    ProfilingCategoryPair aCategoryPair) {
  static_assert(
      std::size(sProfilingCategoryPairInfo) ==
          uint32_t(ProfilingCategoryPair::COUNT),
      "sProfilingCategoryPairInfo and ProfilingCategory need to have the "
      "same order and the same length");

  uint32_t categoryPairIndex = uint32_t(aCategoryPair);
  MOZ_RELEASE_ASSERT(categoryPairIndex <=
                     uint32_t(ProfilingCategoryPair::LAST));
  return sProfilingCategoryPairInfo[categoryPairIndex];
}

}  // namespace JS
