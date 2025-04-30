/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ChromeUtils.h"

#include "JSOracleParent.h"
#include "ThirdPartyUtil.h"
#include "js/CallAndConstruct.h"  // JS::Call
#include "js/ColumnNumber.h"  // JS::TaggedColumnNumberOneOrigin, JS::ColumnNumberOneOrigin
#include "js/CharacterEncoding.h"
#include "js/Date.h"                // JS::IsISOStyleDate
#include "js/Object.h"              // JS::GetClass
#include "js/PropertyAndElement.h"  // JS_DefineProperty, JS_DefinePropertyById, JS_Enumerate, JS_GetProperty, JS_GetPropertyById, JS_SetProperty, JS_SetPropertyById, JS::IdVector
#include "js/PropertyDescriptor.h"  // JS::PropertyDescriptor, JS_GetOwnPropertyDescriptorById
#include "js/SavedFrameAPI.h"
#include "js/Value.h"  // JS::Value, JS::StringValue
#include "jsfriendapi.h"
#include "WrapperFactory.h"

#include "mozilla/Base64.h"
#include "mozilla/CycleCollectedJSRuntime.h"
#include "mozilla/ErrorNames.h"
#include "mozilla/EventStateManager.h"
#include "mozilla/FormAutofillNative.h"
#include "mozilla/IntentionalCrash.h"
#include "mozilla/PerfStats.h"
#include "mozilla/Preferences.h"
#include "mozilla/ProcInfo.h"
#include "mozilla/ResultExtensions.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/ScrollingMetrics.h"
#include "mozilla/SharedStyleSheetCache.h"
#include "mozilla/dom/SharedScriptCache.h"
#include "mozilla/SpinEventLoopUntil.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/IdleDeadline.h"
#include "mozilla/dom/InProcessParent.h"
#include "mozilla/dom/JSActorService.h"
#include "mozilla/dom/MediaSessionBinding.h"
#include "mozilla/dom/PBrowserParent.h"
#include "mozilla/dom/Performance.h"
#include "mozilla/dom/PopupBlocker.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/quota/QuotaManager.h"
#include "mozilla/dom/Record.h"
#include "mozilla/dom/ReportingHeader.h"
#include "mozilla/dom/UnionTypes.h"
#include "mozilla/dom/WindowBinding.h"  // For IdleRequestCallback/Options
#include "mozilla/dom/WindowGlobalParent.h"
#include "mozilla/dom/WorkerScope.h"
#include "mozilla/ipc/GeckoChildProcessHost.h"
#include "mozilla/ipc/UtilityProcessSandboxing.h"
#include "mozilla/ipc/UtilityProcessManager.h"
#include "mozilla/ipc/UtilityProcessHost.h"
#include "mozilla/net/UrlClassifierFeatureFactory.h"
#include "mozilla/RemoteDecoderManagerChild.h"
#include "mozilla/KeySystemConfig.h"
#include "mozilla/WheelHandlingHelper.h"
#include "nsIRFPTargetSetIDL.h"
#include "nsString.h"
#include "nsNativeTheme.h"
#include "nsThreadUtils.h"
#include "mozJSModuleLoader.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/ProfilerMarkers.h"
#include "nsDocShell.h"
#include "nsIException.h"
#include "VsyncSource.h"
#include "imgLoader.h"

#ifdef XP_UNIX
#  include <errno.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <poll.h>
#  include <sys/wait.h>

#  ifdef XP_LINUX
#    include <sys/prctl.h>
#  endif
#endif

#ifdef MOZ_WMF_CDM
#  include "mozilla/MFCDMParent.h"
#endif

#ifdef MOZ_WIDGET_ANDROID
#  include "mozilla/java/GeckoAppShellWrappers.h"
#endif

namespace mozilla::dom {

// Setup logging
extern mozilla::LazyLogModule gMlsLog;

/* static */
void ChromeUtils::NondeterministicGetWeakMapKeys(
    GlobalObject& aGlobal, JS::Handle<JS::Value> aMap,
    JS::MutableHandle<JS::Value> aRetval, ErrorResult& aRv) {
  if (!aMap.isObject()) {
    aRetval.setUndefined();
  } else {
    JSContext* cx = aGlobal.Context();
    JS::Rooted<JSObject*> objRet(cx);
    JS::Rooted<JSObject*> mapObj(cx, &aMap.toObject());
    if (!JS_NondeterministicGetWeakMapKeys(cx, mapObj, &objRet)) {
      aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
    } else {
      aRetval.set(objRet ? JS::ObjectValue(*objRet) : JS::UndefinedValue());
    }
  }
}

/* static */
void ChromeUtils::NondeterministicGetWeakSetKeys(
    GlobalObject& aGlobal, JS::Handle<JS::Value> aSet,
    JS::MutableHandle<JS::Value> aRetval, ErrorResult& aRv) {
  if (!aSet.isObject()) {
    aRetval.setUndefined();
  } else {
    JSContext* cx = aGlobal.Context();
    JS::Rooted<JSObject*> objRet(cx);
    JS::Rooted<JSObject*> setObj(cx, &aSet.toObject());
    if (!JS_NondeterministicGetWeakSetKeys(cx, setObj, &objRet)) {
      aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
    } else {
      aRetval.set(objRet ? JS::ObjectValue(*objRet) : JS::UndefinedValue());
    }
  }
}

/* static */
void ChromeUtils::Base64URLEncode(GlobalObject& aGlobal,
                                  const ArrayBufferViewOrArrayBuffer& aSource,
                                  const Base64URLEncodeOptions& aOptions,
                                  nsACString& aResult, ErrorResult& aRv) {
  auto paddingPolicy = aOptions.mPad ? Base64URLEncodePaddingPolicy::Include
                                     : Base64URLEncodePaddingPolicy::Omit;
  ProcessTypedArrays(
      aSource, [&](const Span<uint8_t>& aData, JS::AutoCheckCannotGC&&) {
        nsresult rv = mozilla::Base64URLEncode(aData.Length(), aData.Elements(),
                                               paddingPolicy, aResult);
        if (NS_WARN_IF(NS_FAILED(rv))) {
          aResult.Truncate();
          aRv.Throw(rv);
        }
      });
}

/* static */
void ChromeUtils::Base64URLDecode(GlobalObject& aGlobal,
                                  const nsACString& aString,
                                  const Base64URLDecodeOptions& aOptions,
                                  JS::MutableHandle<JSObject*> aRetval,
                                  ErrorResult& aRv) {
  Base64URLDecodePaddingPolicy paddingPolicy;
  switch (aOptions.mPadding) {
    case Base64URLDecodePadding::Require:
      paddingPolicy = Base64URLDecodePaddingPolicy::Require;
      break;

    case Base64URLDecodePadding::Ignore:
      paddingPolicy = Base64URLDecodePaddingPolicy::Ignore;
      break;

    case Base64URLDecodePadding::Reject:
      paddingPolicy = Base64URLDecodePaddingPolicy::Reject;
      break;

    default:
      aRv.Throw(NS_ERROR_INVALID_ARG);
      return;
  }
  FallibleTArray<uint8_t> data;
  nsresult rv = mozilla::Base64URLDecode(aString, paddingPolicy, data);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    aRv.Throw(rv);
    return;
  }

  JS::Rooted<JSObject*> buffer(
      aGlobal.Context(), ArrayBuffer::Create(aGlobal.Context(), data, aRv));
  if (aRv.Failed()) {
    return;
  }
  aRetval.set(buffer);
}

/* static */
void ChromeUtils::ReleaseAssert(GlobalObject& aGlobal, bool aCondition,
                                const nsAString& aMessage) {
  // If the condition didn't fail, which is the likely case, immediately return.
  if (MOZ_LIKELY(aCondition)) {
    return;
  }

  // Extract the current stack from the JS runtime to embed in the crash reason.
  nsAutoCString filename;
  uint32_t lineNo = 0;

  if (nsCOMPtr<nsIStackFrame> location = GetCurrentJSStack(1)) {
    location->GetFilename(aGlobal.Context(), filename);
    lineNo = location->GetLineNumber(aGlobal.Context());
  } else {
    filename.Assign("<unknown>"_ns);
  }

  // Convert to utf-8 for adding as the MozCrashReason.
  NS_ConvertUTF16toUTF8 messageUtf8(aMessage);

  // Actually crash.
  MOZ_CRASH_UNSAFE_PRINTF("Failed ChromeUtils.releaseAssert(\"%s\") @ %s:%u",
                          messageUtf8.get(), filename.get(), lineNo);
}

/* static */
void ChromeUtils::AddProfilerMarker(
    GlobalObject& aGlobal, const nsACString& aName,
    const ProfilerMarkerOptionsOrDouble& aOptions,
    const Optional<nsACString>& aText) {
  if (!profiler_thread_is_being_profiled_for_markers()) {
    return;
  }

  MarkerOptions options;

  MarkerCategory category = ::geckoprofiler::category::JS;

  DOMHighResTimeStamp startTime = 0;
  uint64_t innerWindowId = 0;
  if (aOptions.IsDouble()) {
    startTime = aOptions.GetAsDouble();
  } else {
    const ProfilerMarkerOptions& opt = aOptions.GetAsProfilerMarkerOptions();
    startTime = opt.mStartTime;
    innerWindowId = opt.mInnerWindowId;

    if (opt.mCaptureStack) {
      // If we will be capturing a stack, change the category of the
      // ChromeUtils.addProfilerMarker label automatically added by the webidl
      // binding from DOM to PROFILER so that this function doesn't appear in
      // the marker stack.
      JSContext* cx = aGlobal.Context();
      ProfilingStack* stack = js::GetContextProfilingStackIfEnabled(cx);
      if (MOZ_LIKELY(stack)) {
        uint32_t sp = stack->stackPointer;
        if (MOZ_LIKELY(sp > 0)) {
          js::ProfilingStackFrame& frame = stack->frames[sp - 1];
          if (frame.isLabelFrame() && "ChromeUtils"_ns.Equals(frame.label()) &&
              "addProfilerMarker"_ns.Equals(frame.dynamicString())) {
            frame.setLabelCategory(JS::ProfilingCategoryPair::PROFILER);
          }
        }
      }

      options.Set(MarkerStack::Capture());
    }
#define BEGIN_CATEGORY(name, labelAsString, color) \
  if (opt.mCategory.Equals(labelAsString)) {       \
    category = ::geckoprofiler::category::name;    \
  } else
#define SUBCATEGORY(supercategory, name, labelAsString)
#define END_CATEGORY
    MOZ_PROFILING_CATEGORY_LIST(BEGIN_CATEGORY, SUBCATEGORY, END_CATEGORY)
#undef BEGIN_CATEGORY
#undef SUBCATEGORY
#undef END_CATEGORY
    {
      category = ::geckoprofiler::category::OTHER;
    }
  }
  if (startTime) {
    RefPtr<Performance> performance;

    if (NS_IsMainThread()) {
      nsCOMPtr<nsPIDOMWindowInner> ownerWindow =
          do_QueryInterface(aGlobal.GetAsSupports());
      if (ownerWindow) {
        performance = ownerWindow->GetPerformance();
      }
    } else {
      JSContext* cx = aGlobal.Context();
      WorkerPrivate* workerPrivate = GetWorkerPrivateFromContext(cx);
      if (workerPrivate) {
        performance = workerPrivate->GlobalScope()->GetPerformance();
      }
    }

    if (performance) {
      options.Set(MarkerTiming::IntervalUntilNowFrom(
          performance->CreationTimeStamp() +
          TimeDuration::FromMilliseconds(startTime)));
    } else {
      options.Set(MarkerTiming::IntervalUntilNowFrom(
          TimeStamp::ProcessCreation() +
          TimeDuration::FromMilliseconds(startTime)));
    }
  }

  if (innerWindowId) {
    options.Set(MarkerInnerWindowId(innerWindowId));
  } else {
    options.Set(MarkerInnerWindowIdFromJSContext(aGlobal.Context()));
  }

  {
    AUTO_PROFILER_STATS(ChromeUtils_AddProfilerMarker);
    if (aText.WasPassed()) {
      profiler_add_marker(aName, category, std::move(options),
                          ::geckoprofiler::markers::TextMarker{},
                          aText.Value());
    } else {
      profiler_add_marker(aName, category, std::move(options));
    }
  }
}

/* static */
void ChromeUtils::GetXPCOMErrorName(GlobalObject& aGlobal, uint32_t aErrorCode,
                                    nsACString& aRetval) {
  GetErrorName((nsresult)aErrorCode, aRetval);
}

/* static */
void ChromeUtils::WaiveXrays(GlobalObject& aGlobal, JS::Handle<JS::Value> aVal,
                             JS::MutableHandle<JS::Value> aRetval,
                             ErrorResult& aRv) {
  JS::Rooted<JS::Value> value(aGlobal.Context(), aVal);
  if (!xpc::WrapperFactory::WaiveXrayAndWrap(aGlobal.Context(), &value)) {
    aRv.NoteJSContextException(aGlobal.Context());
  } else {
    aRetval.set(value);
  }
}

/* static */
void ChromeUtils::UnwaiveXrays(GlobalObject& aGlobal,
                               JS::Handle<JS::Value> aVal,
                               JS::MutableHandle<JS::Value> aRetval,
                               ErrorResult& aRv) {
  if (!aVal.isObject()) {
    aRetval.set(aVal);
    return;
  }

  JS::Rooted<JSObject*> obj(aGlobal.Context(),
                            js::UncheckedUnwrap(&aVal.toObject()));
  if (!JS_WrapObject(aGlobal.Context(), &obj)) {
    aRv.NoteJSContextException(aGlobal.Context());
  } else {
    aRetval.setObject(*obj);
  }
}

/* static */
void ChromeUtils::GetClassName(GlobalObject& aGlobal,
                               JS::Handle<JSObject*> aObj, bool aUnwrap,
                               nsAString& aRetval) {
  JS::Rooted<JSObject*> obj(aGlobal.Context(), aObj);
  if (aUnwrap) {
    obj = js::UncheckedUnwrap(obj, /* stopAtWindowProxy = */ false);
  }

  aRetval = NS_ConvertUTF8toUTF16(nsDependentCString(JS::GetClass(obj)->name));
}

/* static */
bool ChromeUtils::IsDOMObject(GlobalObject& aGlobal, JS::Handle<JSObject*> aObj,
                              bool aUnwrap) {
  JS::Rooted<JSObject*> obj(aGlobal.Context(), aObj);
  if (aUnwrap) {
    obj = js::UncheckedUnwrap(obj, /* stopAtWindowProxy = */ false);
  }

  return mozilla::dom::IsDOMObject(obj);
}

/* static */
bool ChromeUtils::IsISOStyleDate(GlobalObject& aGlobal,
                                 const nsACString& aStr) {
  // aStr is a UTF-8 string, however we can cast to JS::Latin1Chars
  // because JS::IsISOStyleDate handles ASCII only
  return JS::IsISOStyleDate(aGlobal.Context(),
                            JS::Latin1Chars(aStr.Data(), aStr.Length()));
}

/* static */
void ChromeUtils::ShallowClone(GlobalObject& aGlobal,
                               JS::Handle<JSObject*> aObj,
                               JS::Handle<JSObject*> aTarget,
                               JS::MutableHandle<JSObject*> aRetval,
                               ErrorResult& aRv) {
  JSContext* cx = aGlobal.Context();

  auto cleanup = MakeScopeExit([&]() { aRv.NoteJSContextException(cx); });

  JS::Rooted<JS::IdVector> ids(cx, JS::IdVector(cx));
  JS::RootedVector<JS::Value> values(cx);
  JS::RootedVector<jsid> valuesIds(cx);

  {
    // cx represents our current Realm, so it makes sense to use it for the
    // CheckedUnwrapDynamic call.  We do want CheckedUnwrapDynamic, in case
    // someone is shallow-cloning a Window.
    JS::Rooted<JSObject*> obj(cx, js::CheckedUnwrapDynamic(aObj, cx));
    if (!obj) {
      js::ReportAccessDenied(cx);
      return;
    }

    if (js::IsScriptedProxy(obj)) {
      JS_ReportErrorASCII(cx, "Shallow cloning a proxy object is not allowed");
      return;
    }

    JSAutoRealm ar(cx, obj);

    if (!JS_Enumerate(cx, obj, &ids) || !values.reserve(ids.length()) ||
        !valuesIds.reserve(ids.length())) {
      return;
    }

    JS::Rooted<Maybe<JS::PropertyDescriptor>> desc(cx);
    JS::Rooted<JS::PropertyKey> id(cx);
    for (jsid idVal : ids) {
      id = idVal;
      if (!JS_GetOwnPropertyDescriptorById(cx, obj, id, &desc)) {
        continue;
      }
      if (desc.isNothing() || desc->isAccessorDescriptor()) {
        continue;
      }
      valuesIds.infallibleAppend(id);
      values.infallibleAppend(desc->value());
    }
  }

  JS::Rooted<JSObject*> obj(cx);
  {
    Maybe<JSAutoRealm> ar;
    if (aTarget) {
      // Our target could be anything, so we want CheckedUnwrapDynamic here.
      // "cx" represents the current Realm when we were called from bindings, so
      // we can just use that.
      JS::Rooted<JSObject*> target(cx, js::CheckedUnwrapDynamic(aTarget, cx));
      if (!target) {
        js::ReportAccessDenied(cx);
        return;
      }
      ar.emplace(cx, target);
    }

    obj = JS_NewPlainObject(cx);
    if (!obj) {
      return;
    }

    JS::Rooted<JS::Value> value(cx);
    JS::Rooted<JS::PropertyKey> id(cx);
    for (uint32_t i = 0; i < valuesIds.length(); i++) {
      id = valuesIds[i];
      value = values[i];

      JS_MarkCrossZoneId(cx, id);
      if (!JS_WrapValue(cx, &value) ||
          !JS_SetPropertyById(cx, obj, id, value)) {
        return;
      }
    }
  }

  if (aTarget && !JS_WrapObject(cx, &obj)) {
    return;
  }

  cleanup.release();
  aRetval.set(obj);
}

namespace {
class IdleDispatchRunnable final : public IdleRunnable,
                                   public nsITimerCallback {
 public:
  NS_DECL_ISUPPORTS_INHERITED

  IdleDispatchRunnable(nsIGlobalObject* aParent, IdleRequestCallback& aCallback)
      : IdleRunnable("ChromeUtils::IdleDispatch"),
        mCallback(&aCallback),
        mParent(aParent) {}

  // MOZ_CAN_RUN_SCRIPT_BOUNDARY until Runnable::Run is MOZ_CAN_RUN_SCRIPT.
  // See bug 1535398.
  MOZ_CAN_RUN_SCRIPT_BOUNDARY NS_IMETHOD Run() override {
    if (mCallback) {
      CancelTimer();

      auto deadline = mDeadline - TimeStamp::ProcessCreation();

      ErrorResult rv;
      RefPtr<IdleDeadline> idleDeadline =
          new IdleDeadline(mParent, mTimedOut, deadline.ToMilliseconds());

      RefPtr<IdleRequestCallback> callback(std::move(mCallback));
      MOZ_ASSERT(!mCallback);
      callback->Call(*idleDeadline, "ChromeUtils::IdleDispatch handler");
      mParent = nullptr;
    }
    return NS_OK;
  }

  void SetDeadline(TimeStamp aDeadline) override { mDeadline = aDeadline; }

  NS_IMETHOD Notify(nsITimer* aTimer) override {
    mTimedOut = true;
    SetDeadline(TimeStamp::Now());
    return Run();
  }

  void SetTimer(uint32_t aDelay, nsIEventTarget* aTarget) override {
    MOZ_ASSERT(aTarget);
    MOZ_ASSERT(!mTimer);
    NS_NewTimerWithCallback(getter_AddRefs(mTimer), this, aDelay,
                            nsITimer::TYPE_ONE_SHOT, aTarget);
  }

 protected:
  virtual ~IdleDispatchRunnable() { CancelTimer(); }

 private:
  void CancelTimer() {
    if (mTimer) {
      mTimer->Cancel();
      mTimer = nullptr;
    }
  }

  RefPtr<IdleRequestCallback> mCallback;
  nsCOMPtr<nsIGlobalObject> mParent;

  nsCOMPtr<nsITimer> mTimer;

  TimeStamp mDeadline{};
  bool mTimedOut = false;
};

NS_IMPL_ISUPPORTS_INHERITED(IdleDispatchRunnable, IdleRunnable,
                            nsITimerCallback)
}  // anonymous namespace

/* static */
void ChromeUtils::IdleDispatch(const GlobalObject& aGlobal,
                               IdleRequestCallback& aCallback,
                               const IdleRequestOptions& aOptions,
                               ErrorResult& aRv) {
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());
  MOZ_ASSERT(global);

  auto runnable = MakeRefPtr<IdleDispatchRunnable>(global, aCallback);

  if (aOptions.mTimeout.WasPassed()) {
    aRv = NS_DispatchToCurrentThreadQueue(
        runnable.forget(), aOptions.mTimeout.Value(), EventQueuePriority::Idle);
  } else {
    aRv = NS_DispatchToCurrentThreadQueue(runnable.forget(),
                                          EventQueuePriority::Idle);
  }
}

static mozJSModuleLoader* GetModuleLoaderForCurrentGlobal(
    JSContext* aCx, const GlobalObject& aGlobal,
    Maybe<loader::NonSharedGlobalSyncModuleLoaderScope>&
        aMaybeSyncLoaderScope) {
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());

  if (mozJSModuleLoader::IsSharedSystemGlobal(global)) {
    return mozJSModuleLoader::Get();
  }
  if (mozJSModuleLoader::IsDevToolsLoaderGlobal(global)) {
    return mozJSModuleLoader::GetOrCreateDevToolsLoader(aCx);
  }

  if (loader::NonSharedGlobalSyncModuleLoaderScope::IsActive()) {
    mozJSModuleLoader* moduleloader =
        loader::NonSharedGlobalSyncModuleLoaderScope::ActiveLoader();

    if (!moduleloader->IsLoaderGlobal(global->GetGlobalJSObject())) {
      JS_ReportErrorASCII(aCx,
                          "global: \"current\" option cannot be used for "
                          "different global while other importESModule "
                          "with global: \"current\" is on the stack");
      return nullptr;
    }

    return moduleloader;
  }

  RefPtr targetModuleLoader = global->GetModuleLoader(aCx);
  if (!targetModuleLoader) {
    // Sandbox without associated window returns nullptr for GetModuleLoader.
    JS_ReportErrorASCII(aCx, "No ModuleLoader found for the current context");
    return nullptr;
  }

  if (targetModuleLoader->HasFetchingModules()) {
    if (!NS_IsMainThread()) {
      JS_ReportErrorASCII(aCx,
                          "ChromeUtils.importESModule cannot be used in worker "
                          "when there is ongoing dynamic import");
      return nullptr;
    }

    if (!mozilla::SpinEventLoopUntil(
            "importESModule for current global"_ns, [&]() -> bool {
              return !targetModuleLoader->HasFetchingModules();
            })) {
      JS_ReportErrorASCII(aCx, "Failed to wait for ongoing module requests");
      return nullptr;
    }
  }

  aMaybeSyncLoaderScope.emplace(aCx, global);
  return aMaybeSyncLoaderScope->ActiveLoader();
}

static mozJSModuleLoader* GetModuleLoaderForOptions(
    JSContext* aCx, const GlobalObject& aGlobal,
    const ImportESModuleOptionsDictionary& aOptions,
    Maybe<loader::NonSharedGlobalSyncModuleLoaderScope>&
        aMaybeSyncLoaderScope) {
  if (!aOptions.mGlobal.WasPassed()) {
    return mozJSModuleLoader::Get();
  }

  switch (aOptions.mGlobal.Value()) {
    case ImportESModuleTargetGlobal::Shared:
      return mozJSModuleLoader::Get();

    case ImportESModuleTargetGlobal::Devtools:
      return mozJSModuleLoader::GetOrCreateDevToolsLoader(aCx);

    case ImportESModuleTargetGlobal::Contextual: {
      if (!NS_IsMainThread()) {
        return GetModuleLoaderForCurrentGlobal(aCx, aGlobal,
                                               aMaybeSyncLoaderScope);
      }

      RefPtr devToolsModuleloader = mozJSModuleLoader::GetDevToolsLoader();
      if (devToolsModuleloader &&
          devToolsModuleloader->IsLoaderGlobal(aGlobal.Get())) {
        return mozJSModuleLoader::GetOrCreateDevToolsLoader(aCx);
      }
      return mozJSModuleLoader::Get();
    }

    case ImportESModuleTargetGlobal::Current:
      return GetModuleLoaderForCurrentGlobal(aCx, aGlobal,
                                             aMaybeSyncLoaderScope);

    default:
      MOZ_CRASH("Unknown ImportESModuleTargetGlobal");
  }
}

static bool ValidateImportOptions(
    JSContext* aCx, const GlobalObject& aGlobal,
    const ImportESModuleOptionsDictionary& aOptions) {
  if (!NS_IsMainThread() &&
      (!aOptions.mGlobal.WasPassed() ||
       (aOptions.mGlobal.Value() != ImportESModuleTargetGlobal::Current &&
        aOptions.mGlobal.Value() != ImportESModuleTargetGlobal::Contextual))) {
    JS_ReportErrorASCII(aCx,
                        "ChromeUtils.importESModule: Only { global: "
                        "\"current\" } and { global: \"contextual\" } options "
                        "are supported on worker");
    return false;
  }

  if (NS_IsMainThread()) {
    nsCOMPtr<nsIGlobalObject> global =
        do_QueryInterface(aGlobal.GetAsSupports());

    if (mozJSModuleLoader::IsDevToolsLoaderGlobal(global) &&
        !aOptions.mGlobal.WasPassed()) {
      JS_ReportErrorASCII(aCx,
                          "ChromeUtils.importESModule: global option is "
                          "required in DevTools distinct global");
      return false;
    }
  }

  return true;
}

/* static */
void ChromeUtils::ImportESModule(
    const GlobalObject& aGlobal, const nsAString& aResourceURI,
    const ImportESModuleOptionsDictionary& aOptions,
    JS::MutableHandle<JSObject*> aRetval, ErrorResult& aRv) {
  JSContext* cx = aGlobal.Context();

  if (!ValidateImportOptions(cx, aGlobal, aOptions)) {
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }

  Maybe<loader::NonSharedGlobalSyncModuleLoaderScope> maybeSyncLoaderScope;
  RefPtr<mozJSModuleLoader> moduleloader =
      GetModuleLoaderForOptions(cx, aGlobal, aOptions, maybeSyncLoaderScope);
  if (!moduleloader) {
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }

  NS_ConvertUTF16toUTF8 registryLocation(aResourceURI);

  AUTO_PROFILER_LABEL_DYNAMIC_NSCSTRING_NONSENSITIVE(
      "ChromeUtils::ImportESModule", OTHER, registryLocation);

  JS::Rooted<JSObject*> moduleNamespace(cx);
  nsresult rv =
      moduleloader->ImportESModule(cx, registryLocation, &moduleNamespace);
  if (NS_FAILED(rv)) {
    aRv.Throw(rv);
    return;
  }

  MOZ_ASSERT(!JS_IsExceptionPending(cx));

  if (!JS_WrapObject(cx, &moduleNamespace)) {
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }
  aRetval.set(moduleNamespace);

  if (maybeSyncLoaderScope) {
    maybeSyncLoaderScope->Finish();
  }
}

// An integer encoding for ImportESModuleOptionsDictionary, to pass the value
// to the lazy getters.
class EncodedOptions {
 public:
  explicit EncodedOptions(const ImportESModuleOptionsDictionary& aOptions) {
    if (aOptions.mGlobal.WasPassed()) {
      mValue = uint32_t(aOptions.mGlobal.Value()) + 1;
    } else {
      mValue = 0;
    }
  }

  explicit EncodedOptions(uint32_t aValue) : mValue(aValue) {}

  int32_t toInt32() const { return int32_t(mValue); }

  void DecodeInto(ImportESModuleOptionsDictionary& aOptions) {
    if (mValue == 0) {
      aOptions.mGlobal.Reset();
    } else {
      aOptions.mGlobal.Construct(ImportESModuleTargetGlobal(mValue - 1));
    }
  }

 private:
  uint32_t mValue = 0;
};

namespace lazy_getter {

// The property id of the getter.
// Used by all lazy getters.
static const size_t SLOT_ID = 0;

// The URI of the module to import.
// Used by ChromeUtils.defineESModuleGetters.
static const size_t SLOT_URI = 1;

// An array object that contians values for PARAM_INDEX_TARGET and
// PARAM_INDEX_LAMBDA.
// Used by ChromeUtils.defineLazyGetter.
static const size_t SLOT_PARAMS = 1;

// The EncodedOptions value.
// Used by ChromeUtils.defineESModuleGetters.
static const size_t SLOT_OPTIONS = 2;

static const size_t PARAM_INDEX_TARGET = 0;
static const size_t PARAM_INDEX_LAMBDA = 1;
static const size_t PARAMS_COUNT = 2;

static bool ExtractArgs(JSContext* aCx, JS::CallArgs& aArgs,
                        JS::MutableHandle<JSObject*> aCallee,
                        JS::MutableHandle<JSObject*> aThisObj,
                        JS::MutableHandle<jsid> aId) {
  aCallee.set(&aArgs.callee());

  JS::Handle<JS::Value> thisv = aArgs.thisv();
  if (!thisv.isObject()) {
    JS_ReportErrorASCII(aCx, "Invalid target object");
    return false;
  }

  aThisObj.set(&thisv.toObject());

  JS::Rooted<JS::Value> id(aCx,
                           js::GetFunctionNativeReserved(aCallee, SLOT_ID));
  MOZ_ALWAYS_TRUE(JS_ValueToId(aCx, id, aId));
  return true;
}

static bool JSLazyGetter(JSContext* aCx, unsigned aArgc, JS::Value* aVp) {
  JS::CallArgs args = JS::CallArgsFromVp(aArgc, aVp);

  JS::Rooted<JSObject*> callee(aCx);
  JS::Rooted<JSObject*> unused(aCx);
  JS::Rooted<jsid> id(aCx);
  if (!ExtractArgs(aCx, args, &callee, &unused, &id)) {
    return false;
  }

  JS::Rooted<JS::Value> paramsVal(
      aCx, js::GetFunctionNativeReserved(callee, SLOT_PARAMS));
  if (paramsVal.isUndefined()) {
    args.rval().setUndefined();
    return true;
  }
  // Avoid calling the lambda multiple times, in case of:
  //   * the getter function is retrieved from property descriptor and called
  //   * the lambda gets the property again
  //   * the getter function throws and accessed again
  js::SetFunctionNativeReserved(callee, SLOT_PARAMS, JS::UndefinedHandleValue);

  JS::Rooted<JSObject*> paramsObj(aCx, &paramsVal.toObject());

  JS::Rooted<JS::Value> targetVal(aCx);
  JS::Rooted<JS::Value> lambdaVal(aCx);
  if (!JS_GetElement(aCx, paramsObj, PARAM_INDEX_TARGET, &targetVal)) {
    return false;
  }
  if (!JS_GetElement(aCx, paramsObj, PARAM_INDEX_LAMBDA, &lambdaVal)) {
    return false;
  }

  JS::Rooted<JSObject*> targetObj(aCx, &targetVal.toObject());

  JS::Rooted<JS::Value> value(aCx);
  if (!JS::Call(aCx, targetObj, lambdaVal, JS::HandleValueArray::empty(),
                &value)) {
    return false;
  }

  if (!JS_DefinePropertyById(aCx, targetObj, id, value, JSPROP_ENUMERATE)) {
    return false;
  }

  args.rval().set(value);
  return true;
}

static bool DefineLazyGetter(JSContext* aCx, JS::Handle<JSObject*> aTarget,
                             JS::Handle<JS::Value> aName,
                             JS::Handle<JSObject*> aLambda) {
  JS::Rooted<JS::PropertyKey> id(aCx);
  if (!JS_ValueToId(aCx, aName, &id)) {
    return false;
  }

  JS::Rooted<JS::PropertyKey> funId(aCx);
  if (id.isAtom()) {
    funId = id;
  } else {
    // Don't care int and symbol cases.
    funId = JS::PropertyKey::NonIntAtom(JS_GetEmptyString(aCx));
  }

  JS::Rooted<JSObject*> getter(
      aCx, JS_GetFunctionObject(js::NewFunctionByIdWithReserved(
               aCx, JSLazyGetter, 0, 0, funId)));
  if (!getter) {
    JS_ReportOutOfMemory(aCx);
    return false;
  }

  JS::RootedVector<JS::Value> params(aCx);
  if (!params.resize(PARAMS_COUNT)) {
    return false;
  }
  params[PARAM_INDEX_TARGET].setObject(*aTarget);
  params[PARAM_INDEX_LAMBDA].setObject(*aLambda);
  JS::Rooted<JSObject*> paramsObj(aCx, JS::NewArrayObject(aCx, params));
  if (!paramsObj) {
    return false;
  }

  js::SetFunctionNativeReserved(getter, SLOT_ID, aName);
  js::SetFunctionNativeReserved(getter, SLOT_PARAMS,
                                JS::ObjectValue(*paramsObj));

  return JS_DefinePropertyById(aCx, aTarget, id, getter, nullptr,
                               JSPROP_ENUMERATE);
}

static bool ESModuleGetter(JSContext* aCx, unsigned aArgc, JS::Value* aVp) {
  JS::CallArgs args = JS::CallArgsFromVp(aArgc, aVp);

  JS::Rooted<JSObject*> callee(aCx);
  JS::Rooted<JSObject*> thisObj(aCx);
  JS::Rooted<jsid> id(aCx);
  if (!ExtractArgs(aCx, args, &callee, &thisObj, &id)) {
    return false;
  }

  JS::Rooted<JSString*> moduleURI(
      aCx, js::GetFunctionNativeReserved(callee, SLOT_URI).toString());
  JS::UniqueChars bytes = JS_EncodeStringToUTF8(aCx, moduleURI);
  if (!bytes) {
    return false;
  }
  nsDependentCString uri(bytes.get());

  JS::Rooted<JS::Value> value(aCx);
  EncodedOptions encodedOptions(
      js::GetFunctionNativeReserved(callee, SLOT_OPTIONS).toInt32());

  ImportESModuleOptionsDictionary options;
  encodedOptions.DecodeInto(options);

  GlobalObject global(aCx, callee);

  Maybe<loader::NonSharedGlobalSyncModuleLoaderScope> maybeSyncLoaderScope;
  RefPtr<mozJSModuleLoader> moduleloader =
      GetModuleLoaderForOptions(aCx, global, options, maybeSyncLoaderScope);
  if (!moduleloader) {
    return false;
  }

  JS::Rooted<JSObject*> moduleNamespace(aCx);
  nsresult rv = moduleloader->ImportESModule(aCx, uri, &moduleNamespace);
  if (NS_FAILED(rv)) {
    Throw(aCx, rv);
    return false;
  }

  // ESM's namespace is from the module's realm.
  {
    JSAutoRealm ar(aCx, moduleNamespace);
    if (!JS_GetPropertyById(aCx, moduleNamespace, id, &value)) {
      return false;
    }
  }
  if (!JS_WrapValue(aCx, &value)) {
    return false;
  }

  if (maybeSyncLoaderScope) {
    maybeSyncLoaderScope->Finish();
  }

  if (!JS_DefinePropertyById(aCx, thisObj, id, value, JSPROP_ENUMERATE)) {
    return false;
  }

  args.rval().set(value);
  return true;
}

static bool ESModuleSetter(JSContext* aCx, unsigned aArgc, JS::Value* aVp) {
  JS::CallArgs args = JS::CallArgsFromVp(aArgc, aVp);

  JS::Rooted<JSObject*> callee(aCx);
  JS::Rooted<JSObject*> thisObj(aCx);
  JS::Rooted<jsid> id(aCx);
  if (!ExtractArgs(aCx, args, &callee, &thisObj, &id)) {
    return false;
  }

  return JS_DefinePropertyById(aCx, thisObj, id, args.get(0), JSPROP_ENUMERATE);
}

static bool DefineESModuleGetter(JSContext* aCx, JS::Handle<JSObject*> aTarget,
                                 JS::Handle<JS::PropertyKey> aId,
                                 JS::Handle<JS::Value> aResourceURI,
                                 const EncodedOptions& encodedOptions) {
  JS::Rooted<JS::Value> idVal(aCx, JS::StringValue(aId.toString()));

  JS::Rooted<JS::Value> optionsVal(aCx,
                                   JS::Int32Value(encodedOptions.toInt32()));

  JS::Rooted<JSObject*> getter(
      aCx, JS_GetFunctionObject(js::NewFunctionByIdWithReserved(
               aCx, ESModuleGetter, 0, 0, aId)));

  JS::Rooted<JSObject*> setter(
      aCx, JS_GetFunctionObject(js::NewFunctionByIdWithReserved(
               aCx, ESModuleSetter, 0, 0, aId)));

  if (!getter || !setter) {
    JS_ReportOutOfMemory(aCx);
    return false;
  }

  js::SetFunctionNativeReserved(getter, SLOT_ID, idVal);
  js::SetFunctionNativeReserved(setter, SLOT_ID, idVal);

  js::SetFunctionNativeReserved(getter, SLOT_URI, aResourceURI);

  js::SetFunctionNativeReserved(getter, SLOT_OPTIONS, optionsVal);

  return JS_DefinePropertyById(aCx, aTarget, aId, getter, setter,
                               JSPROP_ENUMERATE);
}

}  // namespace lazy_getter

/* static */
void ChromeUtils::DefineLazyGetter(const GlobalObject& aGlobal,
                                   JS::Handle<JSObject*> aTarget,
                                   JS::Handle<JS::Value> aName,
                                   JS::Handle<JSObject*> aLambda,
                                   ErrorResult& aRv) {
  JSContext* cx = aGlobal.Context();
  if (!lazy_getter::DefineLazyGetter(cx, aTarget, aName, aLambda)) {
    aRv.NoteJSContextException(cx);
    return;
  }
}

/* static */
void ChromeUtils::DefineESModuleGetters(
    const GlobalObject& global, JS::Handle<JSObject*> target,
    JS::Handle<JSObject*> modules,
    const ImportESModuleOptionsDictionary& aOptions, ErrorResult& aRv) {
  JSContext* cx = global.Context();

  JS::Rooted<JS::IdVector> props(cx, JS::IdVector(cx));
  if (!JS_Enumerate(cx, modules, &props)) {
    aRv.NoteJSContextException(cx);
    return;
  }

  if (!ValidateImportOptions(cx, global, aOptions)) {
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }

  EncodedOptions encodedOptions(aOptions);

  JS::Rooted<JS::PropertyKey> prop(cx);
  JS::Rooted<JS::Value> resourceURIVal(cx);
  for (JS::PropertyKey tmp : props) {
    prop = tmp;

    if (!prop.isString()) {
      aRv.Throw(NS_ERROR_FAILURE);
      return;
    }

    if (!JS_GetPropertyById(cx, modules, prop, &resourceURIVal)) {
      aRv.NoteJSContextException(cx);
      return;
    }

    if (!lazy_getter::DefineESModuleGetter(cx, target, prop, resourceURIVal,
                                           encodedOptions)) {
      aRv.NoteJSContextException(cx);
      return;
    }
  }
}

#ifdef XP_UNIX
/* static */
void ChromeUtils::GetLibcConstants(const GlobalObject&,
                                   LibcConstants& aConsts) {
  aConsts.mEPERM.Construct(EPERM);
  aConsts.mEINTR.Construct(EINTR);
  aConsts.mEACCES.Construct(EACCES);
  aConsts.mEAGAIN.Construct(EAGAIN);
  aConsts.mEINVAL.Construct(EINVAL);
  aConsts.mENOSYS.Construct(ENOSYS);

  aConsts.mF_SETFD.Construct(F_SETFD);
  aConsts.mF_SETFL.Construct(F_SETFL);

  aConsts.mFD_CLOEXEC.Construct(FD_CLOEXEC);

  aConsts.mAT_EACCESS.Construct(AT_EACCESS);

  aConsts.mO_CREAT.Construct(O_CREAT);
  aConsts.mO_NONBLOCK.Construct(O_NONBLOCK);
  aConsts.mO_WRONLY.Construct(O_WRONLY);

  aConsts.mPOLLERR.Construct(POLLERR);
  aConsts.mPOLLHUP.Construct(POLLHUP);
  aConsts.mPOLLIN.Construct(POLLIN);
  aConsts.mPOLLNVAL.Construct(POLLNVAL);
  aConsts.mPOLLOUT.Construct(POLLOUT);

  aConsts.mWNOHANG.Construct(WNOHANG);

#  ifdef XP_LINUX
  aConsts.mPR_CAPBSET_READ.Construct(PR_CAPBSET_READ);
#  endif
}
#endif

/* static */
void ChromeUtils::OriginAttributesToSuffix(
    dom::GlobalObject& aGlobal, const dom::OriginAttributesDictionary& aAttrs,
    nsCString& aSuffix)

{
  OriginAttributes attrs(aAttrs);
  attrs.CreateSuffix(aSuffix);
}

/* static */
bool ChromeUtils::OriginAttributesMatchPattern(
    dom::GlobalObject& aGlobal, const dom::OriginAttributesDictionary& aAttrs,
    const dom::OriginAttributesPatternDictionary& aPattern) {
  OriginAttributes attrs(aAttrs);
  OriginAttributesPattern pattern(aPattern);
  return pattern.Matches(attrs);
}

/* static */
void ChromeUtils::CreateOriginAttributesFromOrigin(
    dom::GlobalObject& aGlobal, const nsAString& aOrigin,
    dom::OriginAttributesDictionary& aAttrs, ErrorResult& aRv) {
  OriginAttributes attrs;
  nsAutoCString suffix;
  if (!attrs.PopulateFromOrigin(NS_ConvertUTF16toUTF8(aOrigin), suffix)) {
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }
  aAttrs = attrs;
}

/* static */
void ChromeUtils::CreateOriginAttributesFromOriginSuffix(
    dom::GlobalObject& aGlobal, const nsAString& aSuffix,
    dom::OriginAttributesDictionary& aAttrs, ErrorResult& aRv) {
  OriginAttributes attrs;
  nsAutoCString suffix;
  if (!attrs.PopulateFromSuffix(NS_ConvertUTF16toUTF8(aSuffix))) {
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }
  aAttrs = attrs;
}

/* static */
void ChromeUtils::FillNonDefaultOriginAttributes(
    dom::GlobalObject& aGlobal, const dom::OriginAttributesDictionary& aAttrs,
    dom::OriginAttributesDictionary& aNewAttrs) {
  aNewAttrs = aAttrs;
}

/* static */
bool ChromeUtils::IsOriginAttributesEqual(
    dom::GlobalObject& aGlobal, const dom::OriginAttributesDictionary& aA,
    const dom::OriginAttributesDictionary& aB) {
  return IsOriginAttributesEqual(aA, aB);
}

/* static */
bool ChromeUtils::IsOriginAttributesEqual(
    const dom::OriginAttributesDictionary& aA,
    const dom::OriginAttributesDictionary& aB) {
  return aA == aB;
}

/* static */
void ChromeUtils::GetBaseDomainFromPartitionKey(dom::GlobalObject& aGlobal,
                                                const nsAString& aPartitionKey,
                                                nsAString& aBaseDomain,
                                                ErrorResult& aRv) {
  nsString scheme;
  nsString pkBaseDomain;
  int32_t port;
  bool ancestor;

  if (!mozilla::OriginAttributes::ParsePartitionKey(
          aPartitionKey, scheme, pkBaseDomain, port, ancestor)) {
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }

  aBaseDomain = pkBaseDomain;
}

/* static */
void ChromeUtils::GetPartitionKeyFromURL(dom::GlobalObject& aGlobal,
                                         const nsAString& aTopLevelUrl,
                                         const nsAString& aSubresourceUrl,
                                         const Optional<bool>& aForeignContext,
                                         nsAString& aPartitionKey,
                                         ErrorResult& aRv) {
  nsCOMPtr<nsIURI> topLevelURI;
  nsresult rv = NS_NewURI(getter_AddRefs(topLevelURI), aTopLevelUrl);
  if (NS_SUCCEEDED(rv) && topLevelURI->SchemeIs("chrome")) {
    rv = NS_ERROR_FAILURE;
  }
  if (NS_WARN_IF(NS_FAILED(rv))) {
    aPartitionKey.Truncate();
    aRv.Throw(rv);
    return;
  }

  bool foreignResource;
  bool fallback = false;
  if (!aSubresourceUrl.IsEmpty()) {
    nsCOMPtr<nsIURI> resourceURI;
    rv = NS_NewURI(getter_AddRefs(resourceURI), aSubresourceUrl);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      aPartitionKey.Truncate();
      aRv.Throw(rv);
      return;
    }

    ThirdPartyUtil* thirdPartyUtil = ThirdPartyUtil::GetInstance();
    if (!thirdPartyUtil) {
      aPartitionKey.Truncate();
      aRv.Throw(NS_ERROR_SERVICE_NOT_AVAILABLE);
      return;
    }

    rv = thirdPartyUtil->IsThirdPartyURI(topLevelURI, resourceURI,
                                         &foreignResource);
    if (NS_FAILED(rv)) {
      // we fallback to assuming the resource is foreign if there is an error
      foreignResource = true;
      fallback = true;
    }
  } else {
    // Assume we have a foreign resource if the resource was not provided
    foreignResource = true;
    fallback = true;
  }

  // aForeignContext is whether or not this is a foreign context.
  // foreignResource is whether or not the resource is cross-site to the top
  // level. So we need to validate that a false foreign context doesn't have a
  // same-site resource. That is impossible!
  if (aForeignContext.WasPassed() && !aForeignContext.Value() &&
      foreignResource && !fallback) {
    aPartitionKey.Truncate();
    aRv.Throw(nsresult::NS_ERROR_INVALID_ARG);
    return;
  }

  bool foreignByAncestorContext = aForeignContext.WasPassed() &&
                                  aForeignContext.Value() && !foreignResource;
  mozilla::OriginAttributes attrs;
  attrs.SetPartitionKey(topLevelURI, foreignByAncestorContext);
  aPartitionKey = attrs.mPartitionKey;
}

#ifdef NIGHTLY_BUILD
/* static */
void ChromeUtils::GetRecentJSDevError(GlobalObject& aGlobal,
                                      JS::MutableHandle<JS::Value> aRetval,
                                      ErrorResult& aRv) {
  aRetval.setUndefined();
  auto runtime = CycleCollectedJSRuntime::Get();
  MOZ_ASSERT(runtime);

  auto cx = aGlobal.Context();
  if (!runtime->GetRecentDevError(cx, aRetval)) {
    aRv.NoteJSContextException(cx);
    return;
  }
}

/* static */
void ChromeUtils::ClearRecentJSDevError(GlobalObject&) {
  auto runtime = CycleCollectedJSRuntime::Get();
  MOZ_ASSERT(runtime);

  runtime->ClearRecentDevError();
}
#endif  // NIGHTLY_BUILD

void ChromeUtils::ClearMessagingLayerSecurityStateByPrincipal(
    GlobalObject&, nsIPrincipal* aPrincipal, ErrorResult& aRv) {
  MOZ_LOG(gMlsLog, LogLevel::Debug,
          ("ClearMessagingLayerSecurityStateByPrincipal"));

  if (NS_WARN_IF(!aPrincipal)) {
    MOZ_LOG(gMlsLog, LogLevel::Error, ("Principal is null"));
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }

  // Get the profile directory
  nsCOMPtr<nsIFile> file;
  aRv = NS_GetSpecialDirectory("ProfD", getter_AddRefs(file));
  if (NS_WARN_IF(aRv.Failed())) {
    MOZ_LOG(gMlsLog, LogLevel::Error, ("Failed to get profile directory"));
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }

  // Append the 'mls' directory
  aRv = file->AppendNative("mls"_ns);
  if (NS_WARN_IF(aRv.Failed())) {
    MOZ_LOG(gMlsLog, LogLevel::Error,
            ("Failed to append 'mls' to directory path"));
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }

  bool exists;
  aRv = file->Exists(&exists);
  if (NS_WARN_IF(aRv.Failed())) {
    MOZ_LOG(gMlsLog, LogLevel::Error,
            ("Failed to check if 'mls' directory exists"));
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }

  // If the 'mls' directory does not exist, we exit early
  if (!exists) {
    MOZ_LOG(gMlsLog, LogLevel::Error, ("'mls' directory does not exist"));
    return;
  }

  // Get the storage origin key
  nsAutoCString originKey;
  aRv = aPrincipal->GetStorageOriginKey(originKey);
  if (NS_WARN_IF(aRv.Failed())) {
    MOZ_LOG(gMlsLog, LogLevel::Error, ("Failed to get storage origin key"));
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }

  // Get the origin attributes suffix
  nsAutoCString originAttrSuffix;
  aRv = aPrincipal->GetOriginSuffix(originAttrSuffix);
  if (NS_WARN_IF(aRv.Failed())) {
    MOZ_LOG(gMlsLog, LogLevel::Error,
            ("Failed to get origin attributes suffix"));
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }

  // Construct the full origin key
  nsAutoCString fullOriginKey = originKey + originAttrSuffix;

  // We append the full origin key to the file path
  aRv = file->AppendNative(fullOriginKey);
  if (NS_WARN_IF(aRv.Failed())) {
    MOZ_LOG(gMlsLog, LogLevel::Error,
            ("Failed to append full origin key to the file path"));
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }

  // Remove the directory recursively
  aRv = file->Remove(/* recursive */ true);
  if (NS_WARN_IF(aRv.Failed())) {
    MOZ_LOG(gMlsLog, LogLevel::Error,
            ("Failed to remove : %s", file->HumanReadablePath().get()));
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }

  MOZ_LOG(gMlsLog, LogLevel::Debug,
          ("Successfully cleared MLS state for principal"));
}

void ChromeUtils::ClearMessagingLayerSecurityStateBySite(
    GlobalObject&, const nsACString& aSchemelessSite,
    const dom::OriginAttributesPatternDictionary& aPattern, ErrorResult& aRv) {
  MOZ_LOG(gMlsLog, LogLevel::Debug, ("ClearMessagingLayerSecurityStateBySite"));

  // Get the profile directory
  nsCOMPtr<nsIFile> file;
  aRv = NS_GetSpecialDirectory("ProfD", getter_AddRefs(file));
  if (NS_WARN_IF(aRv.Failed())) {
    MOZ_LOG(gMlsLog, LogLevel::Error, ("Failed to get profile directory"));
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }

  // Append the 'mls' directory
  aRv = file->AppendNative("mls"_ns);
  if (NS_WARN_IF(aRv.Failed())) {
    MOZ_LOG(gMlsLog, LogLevel::Error,
            ("Failed to append 'mls' to directory path"));
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }

  bool exists;
  aRv = file->Exists(&exists);
  if (NS_WARN_IF(aRv.Failed())) {
    MOZ_LOG(gMlsLog, LogLevel::Error,
            ("Failed to check if 'mls' directory exists"));
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }

  // If the 'mls' directory does not exist, we exit early
  if (!exists) {
    MOZ_LOG(gMlsLog, LogLevel::Error, ("'mls' directory does not exist"));
    return;
  }

  // Check if the schemeless site is empty
  if (NS_WARN_IF(aSchemelessSite.IsEmpty())) {
    MOZ_LOG(gMlsLog, LogLevel::Error, ("Schemeless site is empty"));
    aRv.Throw(NS_ERROR_INVALID_ARG);
    return;
  }

  // Site pattern
  OriginAttributesPattern pattern(aPattern);

  // Partition pattern
  // This pattern is used to (additionally) clear state partitioned under
  // aSchemelessSite.
  OriginAttributesPattern partitionPattern = pattern;
  partitionPattern.mPartitionKeyPattern.Construct();
  partitionPattern.mPartitionKeyPattern.Value().mBaseDomain.Construct(
      NS_ConvertUTF8toUTF16(aSchemelessSite));

  // Reverse the base domain using the existing function
  nsAutoCString targetReversedBaseDomain(aSchemelessSite);
  std::reverse(targetReversedBaseDomain.BeginWriting(),
               targetReversedBaseDomain.EndWriting());

  MOZ_LOG(gMlsLog, LogLevel::Debug,
          ("Reversed base domain: %s", targetReversedBaseDomain.get()));

  // Enumerate files in the 'mls' directory
  nsCOMPtr<nsIDirectoryEnumerator> dirEnum;
  aRv = file->GetDirectoryEntries(getter_AddRefs(dirEnum));
  if (NS_WARN_IF(aRv.Failed())) {
    MOZ_LOG(gMlsLog, LogLevel::Error,
            ("Failed to get directory entries in 'mls' directory"));
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }

  // Iterate through all entries in the directory
  nsCOMPtr<nsIFile> entry;
  while (NS_SUCCEEDED(dirEnum->GetNextFile(getter_AddRefs(entry))) && entry) {
    nsAutoCString entryName;
    aRv = entry->GetNativeLeafName(entryName);
    if (NS_WARN_IF(aRv.Failed())) {
      MOZ_LOG(gMlsLog, LogLevel::Error,
              ("Failed to get native leaf name for entry"));
      continue;
    }

    // Find the position of .sqlite.enc or .key in the entry name
    int32_t sqliteEncPos = entryName.RFind(".sqlite.enc");
    int32_t keyPos = entryName.RFind(".key");

    // Remove the .sqlite.enc or .key suffix from the entryName
    if (sqliteEncPos != kNotFound) {
      entryName.SetLength(sqliteEncPos);
    } else if (keyPos != kNotFound) {
      entryName.SetLength(keyPos);
    }

    // Decode the entry name
    nsAutoCString decodedEntryName;
    aRv = mozilla::Base64Decode(entryName, decodedEntryName);
    if (NS_WARN_IF(aRv.Failed())) {
      MOZ_LOG(gMlsLog, LogLevel::Debug,
              ("Failed to decode entry name: %s", entryName.get()));
      continue;
    }

    // Find the origin attributes suffix in the entry name by taking the
    // value of the entry name after the ^ separator
    int32_t separatorPos = decodedEntryName.FindChar('^');

    // We extract the origin attributes suffix from the entry name
    nsAutoCString originSuffix;
    originSuffix.Assign(Substring(decodedEntryName, separatorPos));

    // Populate the origin attributes from the suffix
    OriginAttributes originAttrs;
    if (NS_WARN_IF(!originAttrs.PopulateFromSuffix(originSuffix))) {
      MOZ_LOG(gMlsLog, LogLevel::Error,
              ("Failed to populate origin attributes from suffix"));
      continue;
    }

    // Check if the entry name starts with the reversed base domain
    if (StringBeginsWith(decodedEntryName, targetReversedBaseDomain)) {
      MOZ_LOG(gMlsLog, LogLevel::Debug,
              ("Entry file: %s", entry->HumanReadablePath().get()));

      // If there is a valid origin attributes suffix, we remove the entry
      // only if it matches.
      if (pattern.Matches(originAttrs)) {
        aRv = entry->Remove(/* recursive */ false);
        if (NS_WARN_IF(aRv.Failed())) {
          MOZ_LOG(gMlsLog, LogLevel::Error,
                  ("Failed to remove file: %s", decodedEntryName.get()));
        }
        MOZ_LOG(gMlsLog, LogLevel::Debug,
                ("Removed file: %s", decodedEntryName.get()));
      }
    }

    // If there is a valid origin attributes suffix, we remove the entry
    // only if it matches. We are checking for state partitioned under
    // aSchemelessSite.
    if (partitionPattern.Matches(originAttrs)) {
      aRv = entry->Remove(/* recursive */ false);
      if (NS_WARN_IF(aRv.Failed())) {
        MOZ_LOG(gMlsLog, LogLevel::Error,
                ("Failed to remove file: %s", decodedEntryName.get()));
      }
      MOZ_LOG(gMlsLog, LogLevel::Debug,
              ("Removed file: %s", decodedEntryName.get()));
    }
  }

  // Close the directory enumerator
  dirEnum->Close();
}

void ChromeUtils::ClearMessagingLayerSecurityState(GlobalObject&,
                                                   ErrorResult& aRv) {
  MOZ_LOG(gMlsLog, LogLevel::Debug, ("ClearMessagingLayerSecurityState"));

  // Get the profile directory
  nsCOMPtr<nsIFile> file;
  aRv = NS_GetSpecialDirectory("ProfD", getter_AddRefs(file));
  if (NS_WARN_IF(aRv.Failed())) {
    MOZ_LOG(gMlsLog, LogLevel::Error, ("Failed to get profile directory"));
    return;
  }

  // Append the 'mls' directory
  aRv = file->AppendNative("mls"_ns);
  if (NS_WARN_IF(aRv.Failed())) {
    MOZ_LOG(gMlsLog, LogLevel::Error,
            ("Failed to append 'mls' to directory path"));
    return;
  }

  // Check if the directory exists
  bool exists;
  aRv = file->Exists(&exists);
  if (NS_WARN_IF(aRv.Failed() || !exists)) {
    MOZ_LOG(gMlsLog, LogLevel::Debug, ("'mls' directory does not exist"));
    return;
  }

  // Remove the MLS directory recursively
  aRv = file->Remove(/* recursive */ true);
  if (NS_WARN_IF(aRv.Failed())) {
    MOZ_LOG(gMlsLog, LogLevel::Error, ("Failed to remove MLS directory"));
    return;
  }

  // Log the directory path
  MOZ_LOG(gMlsLog, LogLevel::Debug,
          ("Deleted MLS directory: %s", file->HumanReadablePath().get()));

  // Recreate the MLS directory
  aRv = file->Create(nsIFile::DIRECTORY_TYPE, 0755);
  if (NS_WARN_IF(aRv.Failed())) {
    MOZ_LOG(gMlsLog, LogLevel::Error, ("Failed to recreate MLS directory"));
    return;
  }

  MOZ_LOG(gMlsLog, LogLevel::Debug, ("Successfully cleared all MLS state"));
}

void ChromeUtils::ClearResourceCache(
    GlobalObject& aGlobal, const dom::ClearResourceCacheOptions& aOptions,
    ErrorResult& aRv) {
  bool clearStyleSheet = false;
  bool clearScript = false;
  bool clearImage = false;

  if (aOptions.mTypes.WasPassed()) {
    for (const auto& type : aOptions.mTypes.Value()) {
      switch (type) {
        case ResourceCacheType::Stylesheet:
          clearStyleSheet = true;
          break;
        case ResourceCacheType::Script:
          clearScript = true;
          break;
        case ResourceCacheType::Image:
          clearImage = true;
          break;
      }
    }
  } else {
    clearStyleSheet = true;
    clearScript = true;
    clearImage = true;
  }

  int filterCount = 0;
  if (aOptions.mTarget.WasPassed()) {
    filterCount++;
  }
  if (aOptions.mPrincipal.WasPassed()) {
    filterCount++;
  }
  if (aOptions.mSchemelessSite.WasPassed()) {
    filterCount++;
  }
  if (aOptions.mUrl.WasPassed()) {
    filterCount++;
  }
  if (filterCount > 1) {
    aRv.ThrowInvalidStateError(
        "target, principal, schemelessSite, and url properties are mutually "
        "exclusive");
    return;
  }

  if (aOptions.mTarget.WasPassed()) {
    Maybe<bool> chrome;
    switch (aOptions.mTarget.Value()) {
      case ResourceCacheTarget::Chrome:
        chrome.emplace(true);
        break;
      case ResourceCacheTarget::Content:
        chrome.emplace(false);
        break;
    }

    if (clearStyleSheet) {
      SharedStyleSheetCache::Clear(chrome);
    }
    if (clearScript) {
      SharedScriptCache::Clear(chrome);
    }
    if (clearImage) {
      imgLoader::ClearCache(Nothing(), chrome);
    }
    return;
  }

  if (aOptions.mPrincipal.WasPassed()) {
    nsCOMPtr<nsIPrincipal> principal = aOptions.mPrincipal.Value().get();

    if (clearStyleSheet) {
      SharedStyleSheetCache::Clear(Nothing(), Some(principal));
    }
    if (clearScript) {
      SharedScriptCache::Clear(Nothing(), Some(principal));
    }
    if (clearImage) {
      imgLoader::ClearCache(Nothing(), Nothing(), Some(principal));
    }
    return;
  }

  if (aOptions.mSchemelessSite.WasPassed()) {
    nsCString schemelessSite(aOptions.mSchemelessSite.Value());
    mozilla::OriginAttributesPattern pattern(aOptions.mPattern);

    if (clearStyleSheet) {
      SharedStyleSheetCache::Clear(Nothing(), Nothing(), Some(schemelessSite),
                                   Some(pattern));
    }
    if (clearScript) {
      SharedScriptCache::Clear(Nothing(), Nothing(), Some(schemelessSite),
                               Some(pattern));
    }
    if (clearImage) {
      imgLoader::ClearCache(Nothing(), Nothing(), Nothing(),
                            Some(schemelessSite), Some(pattern));
    }
    return;
  }

  if (aOptions.mUrl.WasPassed()) {
    nsCString url(aOptions.mUrl.Value());

    if (clearStyleSheet) {
      SharedStyleSheetCache::Clear(Nothing(), Nothing(), Nothing(), Nothing(),
                                   Some(url));
    }
    if (clearScript) {
      SharedScriptCache::Clear(Nothing(), Nothing(), Nothing(), Nothing(),
                               Some(url));
    }
    if (clearImage) {
      imgLoader::ClearCache(Nothing(), Nothing(), Nothing(), Nothing(),
                            Nothing(), Some(url));
    }
    return;
  }

  if (clearStyleSheet) {
    SharedStyleSheetCache::Clear();
  }
  if (clearScript) {
    SharedScriptCache::Clear();
  }
  if (clearImage) {
    imgLoader::ClearCache();
  }
}

#define PROCTYPE_TO_WEBIDL_CASE(_procType, _webidl) \
  case mozilla::ProcType::_procType:                \
    return WebIDLProcType::_webidl

static WebIDLProcType ProcTypeToWebIDL(mozilla::ProcType aType) {
  // Max is the value of the last enum, not the length, so add one.
  static_assert(
      static_cast<size_t>(MaxContiguousEnumValue<WebIDLProcType>::value) ==
          static_cast<size_t>(ProcType::Max),
      "In order for this static cast to be okay, "
      "WebIDLProcType must match ProcType exactly");

  // These must match the similar ones in E10SUtils.sys.mjs, RemoteTypes.h,
  // ProcInfo.h and ChromeUtils.webidl
  switch (aType) {
    PROCTYPE_TO_WEBIDL_CASE(Web, Web);
    PROCTYPE_TO_WEBIDL_CASE(WebIsolated, WebIsolated);
    PROCTYPE_TO_WEBIDL_CASE(File, File);
    PROCTYPE_TO_WEBIDL_CASE(Extension, Extension);
    PROCTYPE_TO_WEBIDL_CASE(PrivilegedAbout, Privilegedabout);
    PROCTYPE_TO_WEBIDL_CASE(PrivilegedMozilla, Privilegedmozilla);
    PROCTYPE_TO_WEBIDL_CASE(WebCOOPCOEP, WithCoopCoep);
    PROCTYPE_TO_WEBIDL_CASE(WebServiceWorker, WebServiceWorker);
    PROCTYPE_TO_WEBIDL_CASE(Inference, Inference);

#define GECKO_PROCESS_TYPE(enum_value, enum_name, string_name, proc_typename, \
                           process_bin_type, procinfo_typename,               \
                           webidl_typename, allcaps_name)                     \
  PROCTYPE_TO_WEBIDL_CASE(procinfo_typename, webidl_typename);
#define SKIP_PROCESS_TYPE_CONTENT
#ifndef MOZ_ENABLE_FORKSERVER
#  define SKIP_PROCESS_TYPE_FORKSERVER
#endif  // MOZ_ENABLE_FORKSERVER
#include "mozilla/GeckoProcessTypes.h"
#undef SKIP_PROCESS_TYPE_CONTENT
#ifndef MOZ_ENABLE_FORKSERVER
#  undef SKIP_PROCESS_TYPE_FORKSERVER
#endif  // MOZ_ENABLE_FORKSERVER
#undef GECKO_PROCESS_TYPE

    PROCTYPE_TO_WEBIDL_CASE(Preallocated, Preallocated);
    PROCTYPE_TO_WEBIDL_CASE(Unknown, Unknown);
  }

  MOZ_ASSERT(false, "Unhandled case in ProcTypeToWebIDL");
  return WebIDLProcType::Unknown;
}

#undef PROCTYPE_TO_WEBIDL_CASE

/* static */
already_AddRefed<Promise> ChromeUtils::RequestProcInfo(GlobalObject& aGlobal,
                                                       ErrorResult& aRv) {
  // This function will use IPDL to enable threads info on macOS
  // see https://bugzilla.mozilla.org/show_bug.cgi?id=1529023
  if (!XRE_IsParentProcess()) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }
  // Prepare the JS promise that will hold our response.
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());
  MOZ_ASSERT(global);
  RefPtr<Promise> domPromise = Promise::Create(global, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }
  MOZ_ASSERT(domPromise);

  // Get a list of processes to examine and pre-fill them with available info.
  // Note that this is subject to race conditions: just because we have a
  // process in the list doesn't mean that the process will still be alive when
  // we attempt to get its information. Followup code MUST be able to fail
  // gracefully on some processes and still return whichever information is
  // available.

  // Get all the content parents.
  // Note that this array includes even the long dead content parents, so we
  // might have some garbage, especially with Fission.
  // SAFETY NOTE: `contentParents` is only valid if used synchronously.
  // Anything else and you may end up dealing with dangling pointers.
  nsTArray<ContentParent*> contentParents;
  ContentParent::GetAll(contentParents);

  // Prepare our background request.
  // We reserve one more slot for the browser process itself.
  nsTArray<ProcInfoRequest> requests(contentParents.Length() + 1);
  // Requesting process info for the browser process itself.
  requests.EmplaceBack(
      /* aPid = */ base::GetCurrentProcId(),
      /* aProcessType = */ ProcType::Browser,
      /* aOrigin = */ ""_ns,
      /* aWindowInfo = */ nsTArray<WindowInfo>(),
      /* aUtilityInfo = */ nsTArray<UtilityInfo>());

  // First handle non-ContentParent processes.
  mozilla::ipc::GeckoChildProcessHost::GetAll(
      [&requests](mozilla::ipc::GeckoChildProcessHost* aGeckoProcess) {
        base::ProcessId childPid = aGeckoProcess->GetChildProcessId();
        if (childPid == 0) {
          // Something went wrong with this process, it may be dead already,
          // fail gracefully.
          return;
        }
        mozilla::ProcType type = mozilla::ProcType::Unknown;

        switch (aGeckoProcess->GetProcessType()) {
          case GeckoProcessType::GeckoProcessType_Content: {
            // These processes are handled separately.
            return;
          }

#define GECKO_PROCESS_TYPE(enum_value, enum_name, string_name, proc_typename, \
                           process_bin_type, procinfo_typename,               \
                           webidl_typename, allcaps_name)                     \
  case GeckoProcessType::GeckoProcessType_##enum_name: {                      \
    type = mozilla::ProcType::procinfo_typename;                              \
    break;                                                                    \
  }
#define SKIP_PROCESS_TYPE_CONTENT
#ifndef MOZ_ENABLE_FORKSERVER
#  define SKIP_PROCESS_TYPE_FORKSERVER
#endif  // MOZ_ENABLE_FORKSERVER
#include "mozilla/GeckoProcessTypes.h"
#ifndef MOZ_ENABLE_FORKSERVER
#  undef SKIP_PROCESS_TYPE_FORKSERVER
#endif  // MOZ_ENABLE_FORKSERVER
#undef SKIP_PROCESS_TYPE_CONTENT
#undef GECKO_PROCESS_TYPE
          default:
            // Leave the default Unknown value in |type|.
            break;
        }

        // Attach utility actor information to the process.
        nsTArray<UtilityInfo> utilityActors;
        if (aGeckoProcess->GetProcessType() ==
            GeckoProcessType::GeckoProcessType_Utility) {
          RefPtr<mozilla::ipc::UtilityProcessManager> upm =
              mozilla::ipc::UtilityProcessManager::GetSingleton();
          if (!utilityActors.AppendElements(upm->GetActors(aGeckoProcess),
                                            fallible)) {
            NS_WARNING("Error adding actors");
            return;
          }
        }

        requests.EmplaceBack(
            /* aPid = */ childPid,
            /* aProcessType = */ type,
            /* aOrigin = */ ""_ns,
            /* aWindowInfo = */ nsTArray<WindowInfo>(),  // Without a
                                                         // ContentProcess, no
                                                         // DOM windows.
            /* aUtilityInfo = */ std::move(utilityActors),
            /* aChild = */ 0  // Without a ContentProcess, no ChildId.
#ifdef XP_DARWIN
            ,
            /* aChildTask = */ aGeckoProcess->GetChildTask()
#endif  // XP_DARWIN
        );
      });

  // Now handle ContentParents.
  for (const auto* contentParent : contentParents) {
    if (!contentParent || !contentParent->Process()) {
      // Presumably, the process is dead or dying.
      continue;
    }
    base::ProcessId pid = contentParent->Process()->GetChildProcessId();
    if (pid == 0) {
      // Presumably, the process is dead or dying.
      continue;
    }
    if (contentParent->Process()->GetProcessType() !=
        GeckoProcessType::GeckoProcessType_Content) {
      // We're probably racing against a process changing type.
      // We'll get it in the next call, skip it for the moment.
      continue;
    }

    // Since this code is executed synchronously on the main thread,
    // processes cannot die while we're in this loop.
    mozilla::ProcType type = mozilla::ProcType::Unknown;

    // Convert the remoteType into a ProcType.
    // Ideally, the remoteType should be strongly typed
    // upstream, this would make the conversion less brittle.
    const nsAutoCString remoteType(contentParent->GetRemoteType());
    if (StringBeginsWith(remoteType, FISSION_WEB_REMOTE_TYPE)) {
      // WARNING: Do not change the order, as
      // `DEFAULT_REMOTE_TYPE` is a prefix of
      // `FISSION_WEB_REMOTE_TYPE`.
      type = mozilla::ProcType::WebIsolated;
    } else if (StringBeginsWith(remoteType, SERVICEWORKER_REMOTE_TYPE)) {
      type = mozilla::ProcType::WebServiceWorker;
    } else if (StringBeginsWith(remoteType,
                                WITH_COOP_COEP_REMOTE_TYPE_PREFIX)) {
      type = mozilla::ProcType::WebCOOPCOEP;
    } else if (remoteType == FILE_REMOTE_TYPE) {
      type = mozilla::ProcType::File;
    } else if (remoteType == EXTENSION_REMOTE_TYPE) {
      type = mozilla::ProcType::Extension;
    } else if (remoteType == PRIVILEGEDABOUT_REMOTE_TYPE) {
      type = mozilla::ProcType::PrivilegedAbout;
    } else if (remoteType == PRIVILEGEDMOZILLA_REMOTE_TYPE) {
      type = mozilla::ProcType::PrivilegedMozilla;
    } else if (remoteType == PREALLOC_REMOTE_TYPE) {
      type = mozilla::ProcType::Preallocated;
    } else if (remoteType == INFERENCE_REMOTE_TYPE) {
      type = mozilla::ProcType::Inference;
    } else if (StringBeginsWith(remoteType, DEFAULT_REMOTE_TYPE)) {
      type = mozilla::ProcType::Web;
    } else {
      MOZ_CRASH_UNSAFE_PRINTF("Unknown remoteType '%s'", remoteType.get());
    }

    // By convention, everything after '=' is the origin.
    nsAutoCString origin;
    nsACString::const_iterator cursor;
    nsACString::const_iterator end;
    remoteType.BeginReading(cursor);
    remoteType.EndReading(end);
    if (FindCharInReadable('=', cursor, end)) {
      origin = Substring(++cursor, end);
    }

    // Attach DOM window information to the process.
    nsTArray<WindowInfo> windows;
    for (const auto& browserParentWrapperKey :
         contentParent->ManagedPBrowserParent()) {
      for (const auto& windowGlobalParentWrapperKey :
           browserParentWrapperKey->ManagedPWindowGlobalParent()) {
        // WindowGlobalParent is the only immediate subclass of
        // PWindowGlobalParent.
        auto* windowGlobalParent =
            static_cast<WindowGlobalParent*>(windowGlobalParentWrapperKey);

        nsString documentTitle;
        windowGlobalParent->GetDocumentTitle(documentTitle);
        WindowInfo* window = windows.EmplaceBack(
            fallible,
            /* aOuterWindowId = */ windowGlobalParent->OuterWindowId(),
            /* aDocumentURI = */ windowGlobalParent->GetDocumentURI(),
            /* aDocumentTitle = */ std::move(documentTitle),
            /* aIsProcessRoot = */ windowGlobalParent->IsProcessRoot(),
            /* aIsInProcess = */ windowGlobalParent->IsInProcess());
        if (!window) {
          aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
          return nullptr;
        }
      }
    }
    requests.EmplaceBack(
        /* aPid = */ pid,
        /* aProcessType = */ type,
        /* aOrigin = */ origin,
        /* aWindowInfo = */ std::move(windows),
        /* aUtilityInfo = */ nsTArray<UtilityInfo>(),
        /* aChild = */ contentParent->ChildID()
#ifdef XP_DARWIN
            ,
        /* aChildTask = */ contentParent->Process()->GetChildTask()
#endif  // XP_DARWIN
    );
  }

  // Now place background request.
  RefPtr<nsISerialEventTarget> target = global->SerialEventTarget();
  mozilla::GetProcInfo(std::move(requests))
      ->Then(
          target, __func__,
          [target,
           domPromise](const HashMap<base::ProcessId, ProcInfo>& aSysProcInfo) {
            ParentProcInfoDictionary parentInfo;
            if (aSysProcInfo.count() == 0) {
              // For some reason, we couldn't get *any* info.
              // Maybe a sandboxing issue?
              domPromise->MaybeReject(NS_ERROR_UNEXPECTED);
              return;
            }
            nsTArray<ChildProcInfoDictionary> childrenInfo(
                aSysProcInfo.count() - 1);
            for (auto iter = aSysProcInfo.iter(); !iter.done(); iter.next()) {
              const auto& sysProcInfo = iter.get().value();
              nsresult rv;
              if (sysProcInfo.type == ProcType::Browser) {
                rv = mozilla::CopySysProcInfoToDOM(sysProcInfo, &parentInfo);
                if (NS_FAILED(rv)) {
                  // Failing to copy? That's probably not something from we can
                  // (or should) try to recover gracefully.
                  domPromise->MaybeReject(NS_ERROR_OUT_OF_MEMORY);
                  return;
                }
                MOZ_ASSERT(sysProcInfo.childId == 0);
                MOZ_ASSERT(sysProcInfo.origin.IsEmpty());
              } else {
                mozilla::dom::ChildProcInfoDictionary* childInfo =
                    childrenInfo.AppendElement(fallible);
                if (!childInfo) {
                  domPromise->MaybeReject(NS_ERROR_OUT_OF_MEMORY);
                  return;
                }
                rv = mozilla::CopySysProcInfoToDOM(sysProcInfo, childInfo);
                if (NS_FAILED(rv)) {
                  domPromise->MaybeReject(NS_ERROR_OUT_OF_MEMORY);
                  return;
                }
                // Copy Firefox info.
                childInfo->mChildID = sysProcInfo.childId;
                childInfo->mOrigin = sysProcInfo.origin;
                childInfo->mType = ProcTypeToWebIDL(sysProcInfo.type);

                for (const auto& source : sysProcInfo.windows) {
                  auto* dest = childInfo->mWindows.AppendElement(fallible);
                  if (!dest) {
                    domPromise->MaybeReject(NS_ERROR_OUT_OF_MEMORY);
                    return;
                  }
                  dest->mOuterWindowId = source.outerWindowId;
                  dest->mDocumentURI = source.documentURI;
                  dest->mDocumentTitle = source.documentTitle;
                  dest->mIsProcessRoot = source.isProcessRoot;
                  dest->mIsInProcess = source.isInProcess;
                }

                if (sysProcInfo.type == ProcType::Utility) {
                  for (const auto& source : sysProcInfo.utilityActors) {
                    auto* dest =
                        childInfo->mUtilityActors.AppendElement(fallible);
                    if (!dest) {
                      domPromise->MaybeReject(NS_ERROR_OUT_OF_MEMORY);
                      return;
                    }

                    dest->mActorName = source.actorName;
                  }
                }
              }
            }

            // Attach the children to the parent.
            mozilla::dom::Sequence<mozilla::dom::ChildProcInfoDictionary>
                children(std::move(childrenInfo));
            parentInfo.mChildren = std::move(children);
            domPromise->MaybeResolve(parentInfo);
          },
          [domPromise](nsresult aRv) { domPromise->MaybeReject(aRv); });
  MOZ_ASSERT(domPromise);

  // sending back the promise instance
  return domPromise.forget();
}

/* static */
bool ChromeUtils::VsyncEnabled(GlobalObject& aGlobal) {
  return mozilla::gfx::VsyncSource::GetFastestVsyncRate().isSome();
}

void ChromeUtils::SetPerfStatsCollectionMask(GlobalObject& aGlobal,
                                             uint64_t aMask) {
  PerfStats::SetCollectionMask(static_cast<PerfStats::MetricMask>(aMask));
}

already_AddRefed<Promise> ChromeUtils::CollectPerfStats(GlobalObject& aGlobal,
                                                        ErrorResult& aRv) {
  // Creating a JS promise
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());
  MOZ_ASSERT(global);

  RefPtr<Promise> promise = Promise::Create(global, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  RefPtr<PerfStats::PerfStatsPromise> extPromise =
      PerfStats::CollectPerfStatsJSON();

  extPromise->Then(
      GetCurrentSerialEventTarget(), __func__,
      [promise](const nsCString& aResult) {
        promise->MaybeResolve(NS_ConvertUTF8toUTF16(aResult));
      },
      [promise](bool aValue) { promise->MaybeReject(NS_ERROR_FAILURE); });

  return promise.forget();
}

constexpr auto kSkipSelfHosted = JS::SavedFrameSelfHosted::Exclude;

/* static */
void ChromeUtils::GetCallerLocation(const GlobalObject& aGlobal,
                                    nsIPrincipal* aPrincipal,
                                    JS::MutableHandle<JSObject*> aRetval) {
  JSContext* cx = aGlobal.Context();

  auto* principals = nsJSPrincipals::get(aPrincipal);

  JS::StackCapture captureMode(JS::FirstSubsumedFrame(cx, principals));

  JS::Rooted<JSObject*> frame(cx);
  if (!JS::CaptureCurrentStack(cx, &frame, std::move(captureMode))) {
    JS_ClearPendingException(cx);
    aRetval.set(nullptr);
    return;
  }

  // FirstSubsumedFrame gets us a stack which stops at the first principal which
  // is subsumed by the given principal. That means that we may have a lot of
  // privileged frames that we don't care about at the top of the stack, though.
  // We need to filter those out to get the frame we actually want.
  aRetval.set(
      js::GetFirstSubsumedSavedFrame(cx, principals, frame, kSkipSelfHosted));
}

/* static */
void ChromeUtils::CreateError(const GlobalObject& aGlobal,
                              const nsAString& aMessage,
                              JS::Handle<JSObject*> aStack,
                              JS::MutableHandle<JSObject*> aRetVal,
                              ErrorResult& aRv) {
  if (aStack && !JS::IsMaybeWrappedSavedFrame(aStack)) {
    aRv.Throw(NS_ERROR_INVALID_ARG);
    return;
  }

  JSContext* cx = aGlobal.Context();

  auto cleanup = MakeScopeExit([&]() { aRv.NoteJSContextException(cx); });

  JS::Rooted<JSObject*> retVal(cx);
  {
    JS::Rooted<JSString*> fileName(cx, JS_GetEmptyString(cx));
    uint32_t line = 0;
    JS::TaggedColumnNumberOneOrigin column;

    Maybe<JSAutoRealm> ar;
    JS::Rooted<JSObject*> stack(cx);
    if (aStack) {
      stack = UncheckedUnwrap(aStack);
      ar.emplace(cx, stack);

      JSPrincipals* principals =
          JS::GetRealmPrincipals(js::GetContextRealm(cx));
      if (JS::GetSavedFrameLine(cx, principals, stack, &line) !=
              JS::SavedFrameResult::Ok ||
          JS::GetSavedFrameColumn(cx, principals, stack, &column) !=
              JS::SavedFrameResult::Ok ||
          JS::GetSavedFrameSource(cx, principals, stack, &fileName) !=
              JS::SavedFrameResult::Ok) {
        return;
      }
    }

    JS::Rooted<JSString*> message(cx);
    {
      JS::Rooted<JS::Value> msgVal(cx);
      if (!xpc::NonVoidStringToJsval(cx, aMessage, &msgVal)) {
        return;
      }
      message = msgVal.toString();
    }

    JS::Rooted<JS::Value> err(cx);
    if (!JS::CreateError(cx, JSEXN_ERR, stack, fileName, line,
                         JS::ColumnNumberOneOrigin(column.oneOriginValue()),
                         nullptr, message, JS::NothingHandleValue, &err)) {
      return;
    }

    MOZ_ASSERT(err.isObject());
    retVal = &err.toObject();
  }

  if (aStack && !JS_WrapObject(cx, &retVal)) {
    return;
  }

  cleanup.release();
  aRetVal.set(retVal);
}

/* static */
bool ChromeUtils::HasReportingHeaderForOrigin(GlobalObject& global,
                                              const nsAString& aOrigin,
                                              ErrorResult& aRv) {
  if (!XRE_IsParentProcess()) {
    aRv.Throw(NS_ERROR_FAILURE);
    return false;
  }

  return ReportingHeader::HasReportingHeaderForOrigin(
      NS_ConvertUTF16toUTF8(aOrigin));
}

/* static */
PopupBlockerState ChromeUtils::GetPopupControlState(GlobalObject& aGlobal) {
  switch (PopupBlocker::GetPopupControlState()) {
    case PopupBlocker::PopupControlState::openAllowed:
      return PopupBlockerState::OpenAllowed;

    case PopupBlocker::PopupControlState::openControlled:
      return PopupBlockerState::OpenControlled;

    case PopupBlocker::PopupControlState::openBlocked:
      return PopupBlockerState::OpenBlocked;

    case PopupBlocker::PopupControlState::openAbused:
      return PopupBlockerState::OpenAbused;

    case PopupBlocker::PopupControlState::openOverridden:
      return PopupBlockerState::OpenOverridden;

    default:
      MOZ_CRASH(
          "PopupBlocker::PopupControlState and PopupBlockerState are out of "
          "sync");
  }
}

/* static */
double ChromeUtils::LastExternalProtocolIframeAllowed(GlobalObject& aGlobal) {
  TimeStamp when = PopupBlocker::WhenLastExternalProtocolIframeAllowed();
  if (when.IsNull()) {
    return 0;
  }

  TimeDuration duration = TimeStamp::Now() - when;
  return duration.ToMilliseconds();
}

/* static */
void ChromeUtils::ResetLastExternalProtocolIframeAllowed(
    GlobalObject& aGlobal) {
  PopupBlocker::ResetLastExternalProtocolIframeAllowed();
}

/* static */
void ChromeUtils::EndWheelTransaction(GlobalObject& aGlobal) {
  // This allows us to end the current wheel transaction from the browser
  // chrome. We do not need to perform any checks before calling
  // EndTransaction(), as it should do nothing in the case that there is
  // no current wheel transaction.
  WheelTransaction::EndTransaction();
}

/* static */
void ChromeUtils::RegisterWindowActor(const GlobalObject& aGlobal,
                                      const nsACString& aName,
                                      const WindowActorOptions& aOptions,
                                      ErrorResult& aRv) {
  if (!XRE_IsParentProcess()) {
    aRv.ThrowNotAllowedError(
        "registerWindowActor() may only be called in the parent process");
    return;
  }

  RefPtr<JSActorService> service = JSActorService::GetSingleton();
  service->RegisterWindowActor(aName, aOptions, aRv);
}

/* static */
void ChromeUtils::UnregisterWindowActor(const GlobalObject& aGlobal,
                                        const nsACString& aName,
                                        ErrorResult& aRv) {
  if (!XRE_IsParentProcess()) {
    aRv.ThrowNotAllowedError(
        "unregisterWindowActor() may only be called in the parent process");
    return;
  }

  RefPtr<JSActorService> service = JSActorService::GetSingleton();
  service->UnregisterWindowActor(aName);
}

/* static */
void ChromeUtils::RegisterProcessActor(const GlobalObject& aGlobal,
                                       const nsACString& aName,
                                       const ProcessActorOptions& aOptions,
                                       ErrorResult& aRv) {
  if (!XRE_IsParentProcess()) {
    aRv.ThrowNotAllowedError(
        "registerProcessActor() may only be called in the parent process");
    return;
  }

  RefPtr<JSActorService> service = JSActorService::GetSingleton();
  service->RegisterProcessActor(aName, aOptions, aRv);
}

/* static */
void ChromeUtils::UnregisterProcessActor(const GlobalObject& aGlobal,
                                         const nsACString& aName,
                                         ErrorResult& aRv) {
  if (!XRE_IsParentProcess()) {
    aRv.ThrowNotAllowedError(
        "unregisterProcessActor() may only be called in the parent process");
    return;
  }

  RefPtr<JSActorService> service = JSActorService::GetSingleton();
  service->UnregisterProcessActor(aName);
}

/* static */
already_AddRefed<Promise> ChromeUtils::EnsureHeadlessContentProcess(
    const GlobalObject& aGlobal, const nsACString& aRemoteType,
    ErrorResult& aRv) {
  if (!XRE_IsParentProcess()) {
    aRv.ThrowNotAllowedError(
        "ensureHeadlessContentProcess() may only be called in the parent "
        "process");
    return nullptr;
  }

  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());
  RefPtr<Promise> promise = Promise::Create(global, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  ContentParent::GetNewOrUsedBrowserProcessAsync(aRemoteType)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [promise](UniqueContentParentKeepAlive&& aKeepAlive) {
            nsCOMPtr<nsIContentParentKeepAlive> jsKeepAlive =
                WrapContentParentKeepAliveForJS(std::move(aKeepAlive));
            promise->MaybeResolve(jsKeepAlive);
          },
          [promise](nsresult aError) { promise->MaybeReject(aError); });
  return promise.forget();
}

/* static */
bool ChromeUtils::IsClassifierBlockingErrorCode(GlobalObject& aGlobal,
                                                uint32_t aError) {
  return net::UrlClassifierFeatureFactory::IsClassifierBlockingErrorCode(
      static_cast<nsresult>(aError));
}

/* static */
void ChromeUtils::PrivateNoteIntentionalCrash(const GlobalObject& aGlobal,
                                              ErrorResult& aError) {
  if (XRE_IsContentProcess()) {
    NoteIntentionalCrash("tab");
    return;
  }
  aError.Throw(NS_ERROR_NOT_IMPLEMENTED);
}

/* static */
nsIDOMProcessChild* ChromeUtils::GetDomProcessChild(const GlobalObject&) {
  return nsIDOMProcessChild::GetSingleton();
}

/* static */
void ChromeUtils::GetAllDOMProcesses(
    GlobalObject& aGlobal, nsTArray<RefPtr<nsIDOMProcessParent>>& aParents,
    ErrorResult& aRv) {
  if (!XRE_IsParentProcess()) {
    aRv.ThrowNotAllowedError(
        "getAllDOMProcesses() may only be called in the parent process");
    return;
  }
  aParents.Clear();
  // Always add the parent process nsIDOMProcessParent first
  aParents.AppendElement(InProcessParent::Singleton());

  // Before adding nsIDOMProcessParent for all the content processes
  for (auto* cp : ContentParent::AllProcesses(ContentParent::eLive)) {
    aParents.AppendElement(cp);
  }
}

/* static */
void ChromeUtils::ConsumeInteractionData(
    GlobalObject& aGlobal, Record<nsString, InteractionData>& aInteractions,
    ErrorResult& aRv) {
  if (!XRE_IsParentProcess()) {
    aRv.ThrowNotAllowedError(
        "consumeInteractionData() may only be called in the parent "
        "process");
    return;
  }
  EventStateManager::ConsumeInteractionData(aInteractions);
}

already_AddRefed<Promise> ChromeUtils::CollectScrollingData(
    GlobalObject& aGlobal, ErrorResult& aRv) {
  // Creating a JS promise
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());
  MOZ_ASSERT(global);

  RefPtr<Promise> promise = Promise::Create(global, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  RefPtr<ScrollingMetrics::ScrollingMetricsPromise> extPromise =
      ScrollingMetrics::CollectScrollingMetrics();

  extPromise->Then(
      GetCurrentSerialEventTarget(), __func__,
      [promise](const std::tuple<uint32_t, uint32_t>& aResult) {
        InteractionData out = {};
        out.mInteractionTimeInMilliseconds = std::get<0>(aResult);
        out.mScrollingDistanceInPixels = std::get<1>(aResult);
        promise->MaybeResolve(out);
      },
      [promise](bool aValue) { promise->MaybeReject(NS_ERROR_FAILURE); });

  return promise.forget();
}

/* static */
void ChromeUtils::GetFormAutofillConfidences(
    GlobalObject& aGlobal, const Sequence<OwningNonNull<Element>>& aElements,
    nsTArray<FormAutofillConfidences>& aResults, ErrorResult& aRv) {
  FormAutofillNative::GetFormAutofillConfidences(aGlobal, aElements, aResults,
                                                 aRv);
}

bool ChromeUtils::IsDarkBackground(GlobalObject&, Element& aElement) {
  nsIFrame* f = aElement.GetPrimaryFrame(FlushType::Frames);
  if (!f) {
    return false;
  }
  return nsNativeTheme::IsDarkBackground(f);
}

double ChromeUtils::DateNow(GlobalObject&) { return JS_Now() / 1000.0; }

/* static */
void ChromeUtils::EnsureJSOracleStarted(GlobalObject&) {
  if (StaticPrefs::browser_opaqueResponseBlocking_javascriptValidator()) {
    JSOracleParent::WithJSOracle([](JSOracleParent* aParent) {});
  }
}

/* static */
unsigned ChromeUtils::AliveUtilityProcesses(const GlobalObject&) {
  const auto& utilityProcessManager =
      mozilla::ipc::UtilityProcessManager::GetIfExists();
  return utilityProcessManager ? utilityProcessManager->AliveProcesses() : 0;
}

/* static */
void ChromeUtils::GetAllPossibleUtilityActorNames(GlobalObject& aGlobal,
                                                  nsTArray<nsCString>& aNames) {
  aNames.Clear();
  for (UtilityActorName idlName :
       MakeWebIDLEnumeratedRange<WebIDLUtilityActorName>()) {
    aNames.AppendElement(GetEnumString(idlName));
  }
}

/* static */
bool ChromeUtils::ShouldResistFingerprinting(
    GlobalObject& aGlobal, JSRFPTarget aTarget,
    nsIRFPTargetSetIDL* aOverriddenFingerprintingSettings,
    const Optional<bool>& aIsPBM) {
  RFPTarget target;
  switch (aTarget) {
    case JSRFPTarget::RoundWindowSize:
      target = RFPTarget::RoundWindowSize;
      break;
    case JSRFPTarget::SiteSpecificZoom:
      target = RFPTarget::SiteSpecificZoom;
      break;
    case JSRFPTarget::CSSPrefersColorScheme:
      target = RFPTarget::CSSPrefersColorScheme;
      break;
    case JSRFPTarget::JSLocalePrompt:
      target = RFPTarget::JSLocalePrompt;
      break;
    default:
      MOZ_CRASH("Unhandled JSRFPTarget enum value");
  }

  bool isPBM = false;
  if (aIsPBM.WasPassed()) {
    isPBM = aIsPBM.Value();
  } else {
    nsCOMPtr<nsIGlobalObject> global =
        do_QueryInterface(aGlobal.GetAsSupports());
    if (global) {
      nsPIDOMWindowInner* win = global->GetAsInnerWindow();
      if (win) {
        nsIDocShell* docshell = win->GetDocShell();
        if (docshell) {
          nsDocShell::Cast(docshell)->GetUsePrivateBrowsing(&isPBM);
        }
      }
    }
  }

  Maybe<RFPTargetSet> overriddenFingerprintingSettings;
  if (aOverriddenFingerprintingSettings) {
    uint64_t low, hi;
    aOverriddenFingerprintingSettings->GetLow(&low);
    aOverriddenFingerprintingSettings->GetHigh(&hi);
    std::bitset<128> bitset;
    bitset |= hi;
    bitset <<= 64;
    bitset |= low;
    overriddenFingerprintingSettings.emplace(RFPTargetSet(bitset));
  }

  // This global object appears to be the global window, not for individual
  // sites so to exempt individual sites (instead of just PBM/Not-PBM windows)
  // more work would be needed to get the correct context.
  return nsRFPService::IsRFPEnabledFor(isPBM, target,
                                       overriddenFingerprintingSettings);
}

std::atomic<uint32_t> ChromeUtils::sDevToolsOpenedCount = 0;

/* static */
bool ChromeUtils::IsDevToolsOpened() {
  return ChromeUtils::sDevToolsOpenedCount > 0;
}

/* static */
bool ChromeUtils::IsDevToolsOpened(GlobalObject& aGlobal) {
  return ChromeUtils::IsDevToolsOpened();
}

/* static */
void ChromeUtils::NotifyDevToolsOpened(GlobalObject& aGlobal) {
  ChromeUtils::sDevToolsOpenedCount++;
}

/* static */
void ChromeUtils::NotifyDevToolsClosed(GlobalObject& aGlobal) {
  MOZ_ASSERT(ChromeUtils::sDevToolsOpenedCount >= 1);
  ChromeUtils::sDevToolsOpenedCount--;
}

/* static */
bool ChromeUtils::IsJSIdentifier(GlobalObject& aGlobal, const nsAString& aStr) {
  return JS_IsIdentifier(aStr.BeginReading(), aStr.Length());
}

#ifdef MOZ_WMF_CDM
/* static */
already_AddRefed<Promise> ChromeUtils::GetWMFContentDecryptionModuleInformation(
    GlobalObject& aGlobal, ErrorResult& aRv) {
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());
  MOZ_ASSERT(global);
  RefPtr<Promise> domPromise = Promise::Create(global, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }
  MOZ_ASSERT(domPromise);
  MFCDMService::GetAllKeySystemsCapabilities(domPromise);
  return domPromise.forget();
}
#endif

already_AddRefed<Promise> ChromeUtils::GetGMPContentDecryptionModuleInformation(
    GlobalObject& aGlobal, ErrorResult& aRv) {
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());
  MOZ_ASSERT(global);
  RefPtr<Promise> domPromise = Promise::Create(global, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }
  MOZ_ASSERT(domPromise);
  KeySystemConfig::GetGMPKeySystemConfigs(domPromise);
  return domPromise.forget();
}

void ChromeUtils::AndroidMoveTaskToBack(GlobalObject& aGlobal) {
#ifdef MOZ_WIDGET_ANDROID
  MOZ_RELEASE_ASSERT(XRE_IsParentProcess());
  java::GeckoAppShell::MoveTaskToBack();
#endif
}

already_AddRefed<nsIContentSecurityPolicy> ChromeUtils::CreateCSPFromHeader(
    GlobalObject& aGlobal, const nsAString& aHeader, nsIURI* aSelfURI,
    nsIPrincipal* aLoadingPrincipal, ErrorResult& aRv) {
  return CSP_CreateFromHeader(aHeader, aSelfURI, aLoadingPrincipal,
                             aRv);
}

}  // namespace mozilla::dom
