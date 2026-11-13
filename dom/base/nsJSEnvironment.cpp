/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsJSEnvironment.h"

#include "mozilla/EventDispatcher.h"
#include "mozilla/HoldDropJSObjects.h"
#include "nsAtom.h"
#include "nsCOMPtr.h"
#include "nsCRT.h"
#include "nsContentUtils.h"
#include "nsCycleCollector.h"
#include "nsDOMCID.h"
#include "nsDOMJSUtils.h"
#include "nsError.h"
#include "nsIConsoleService.h"
#include "nsIContent.h"
#include "nsIDocShell.h"
#include "nsIDocShellTreeItem.h"
#include "nsIInterfaceRequestor.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsIObserverService.h"
#include "nsIScriptGlobalObject.h"
#include "nsIScriptObjectPrincipal.h"
#include "nsISupportsPrimitives.h"
#include "nsITimer.h"
#include "nsIXPConnect.h"
#include "nsJSUtils.h"
#include "nsPIDOMWindow.h"
#include "nsPIDOMWindowInlines.h"
#include "nsPresContext.h"
#include "nsReadableUtils.h"
#include "nsServiceManagerUtils.h"
#include "nsTextFormatter.h"
#include "nsXPCOMCIDInternal.h"
#ifdef XP_WIN
#  include <process.h>
#  define getpid _getpid
#else
#  include <unistd.h>  // for getpid()
#endif
#include "AccessCheck.h"
#include "CCGCScheduler.h"
#include "WrapperFactory.h"
#include "js/Array.h"               // JS::NewArrayObject
#include "js/PropertyAndElement.h"  // JS_DefineProperty
#include "js/PropertySpec.h"
#include "js/SliceBudget.h"
#include "js/Wrapper.h"
#include "jsapi.h"
#include "mozilla/Attributes.h"
#include "mozilla/AutoRestore.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/ContentEvents.h"
#include "mozilla/CycleCollectedJSContext.h"
#include "mozilla/CycleCollectedJSRuntime.h"
#include "mozilla/CycleCollectorStats.h"
#include "mozilla/EventStateManager.h"
#include "mozilla/Logging.h"
#include "mozilla/MainThreadIdlePeriod.h"
#include "mozilla/Preferences.h"
#include "mozilla/PresShell.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/ProfilerMarkers.h"
#include "mozilla/SchedulerGroup.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPrefs_javascript.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/TaskController.h"
#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/CanvasRenderingContext2DBinding.h"
#include "mozilla/dom/DOMException.h"
#include "mozilla/dom/DOMExceptionBinding.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/ErrorEvent.h"
#include "mozilla/dom/FetchUtil.h"
#include "mozilla/dom/RootedDictionary.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/SerializedStackHolder.h"
#include "mozilla/dom/TimeoutHandler.h"
#include "mozilla/dom/TimeoutManager.h"
#include "mozilla/glean/DomMetrics.h"
#include "nsCycleCollectionNoteRootCallback.h"
#include "nsGlobalWindowInner.h"
#include "nsGlobalWindowOuter.h"
#include "nsIArray.h"
#include "nsJSPrincipals.h"
#include "nsRefreshDriver.h"
#include "prthread.h"
#include "xpcpublic.h"
#if defined(MOZ_MEMORY)
#  include "mozmemory.h"
#endif

using namespace mozilla;
using namespace mozilla::dom;

// Thank you Microsoft!
#ifdef CompareString
#  undef CompareString
#endif

static JS::GCSliceCallback sPrevGCSliceCallback;

static bool sIncrementalCC = false;

static bool sIsInitialized;
static bool sShuttingDown;

static CCGCScheduler* sScheduler = nullptr;
static std::aligned_storage_t<sizeof(*sScheduler)> sSchedulerStorage;

// Cache a pointer to the main thread's statistics struct.
static CycleCollectorStats* sCCStats = nullptr;

static const char* ProcessNameForCollectorLog() {
  return XRE_GetProcessType() == GeckoProcessType_Default ? "default"
                                                          : "content";
}

namespace xpc {

// This handles JS Exceptions (via ExceptionStackOrNull), DOM and XPC
// Exceptions, and arbitrary values that were associated with a stack by the
// JS engine when they were thrown, as specified by exceptionStack.
//
// Note that the returned stackObj and stackGlobal are _not_ wrapped into the
// compartment of exceptionValue.
void FindExceptionStackForConsoleReport(
    nsPIDOMWindowInner* win, JS::Handle<JS::Value> exceptionValue,
    JS::Handle<JSObject*> exceptionStack, JS::MutableHandle<JSObject*> stackObj,
    JS::MutableHandle<JSObject*> stackGlobal) {
  stackObj.set(nullptr);
  stackGlobal.set(nullptr);

  if (!exceptionValue.isObject()) {
    // Use the stack provided by the JS engine, if available. This will not be
    // a wrapper.
    if (exceptionStack) {
      stackObj.set(exceptionStack);
      stackGlobal.set(JS::GetNonCCWObjectGlobal(exceptionStack));
    }
    return;
  }

  if (win && win->AsGlobal()->IsDying()) {
    // Pretend like we have no stack, so we don't end up keeping the global
    // alive via the stack.
    return;
  }

  JS::RootingContext* rcx = RootingCx();
  JS::Rooted<JSObject*> exceptionObject(rcx, &exceptionValue.toObject());
  if (JSObject* excStack = JS::ExceptionStackOrNull(exceptionObject)) {
    // At this point we know exceptionObject is a possibly-wrapped
    // js::ErrorObject that has excStack as stack. excStack might also be a CCW,
    // but excStack must be same-compartment with the unwrapped ErrorObject.
    // Return the ErrorObject's global as stackGlobal. This matches what we do
    // in the ErrorObject's |.stack| getter and ensures stackObj and stackGlobal
    // are same-compartment.
    JSObject* unwrappedException = js::UncheckedUnwrap(exceptionObject);
    stackObj.set(excStack);
    stackGlobal.set(JS::GetNonCCWObjectGlobal(unwrappedException));
    return;
  }

  // It is not a JS Exception, try DOM Exception.
  RefPtr<Exception> exception;
  UNWRAP_OBJECT(DOMException, exceptionObject, exception);
  if (!exception) {
    // Not a DOM Exception, try XPC Exception.
    UNWRAP_OBJECT(Exception, exceptionObject, exception);
    if (!exception) {
      // As above, use the stack provided by the JS engine, if available.
      if (exceptionStack) {
        stackObj.set(exceptionStack);
        stackGlobal.set(JS::GetNonCCWObjectGlobal(exceptionStack));
      }
      return;
    }
  }

  nsCOMPtr<nsIStackFrame> stack = exception->GetLocation();
  if (!stack) {
    return;
  }
  JS::Rooted<JS::Value> value(rcx);
  stack->GetNativeSavedFrame(&value);
  if (value.isObject()) {
    stackObj.set(&value.toObject());
    MOZ_ASSERT(JS::IsUnwrappedSavedFrame(stackObj));
    stackGlobal.set(JS::GetNonCCWObjectGlobal(stackObj));
    return;
  }
}

} /* namespace xpc */

static TimeDuration GetCollectionTimeDelta() {
  static TimeStamp sFirstCollectionTime;
  TimeStamp now = TimeStamp::Now();
  if (sFirstCollectionTime) {
    return now - sFirstCollectionTime;
  }
  sFirstCollectionTime = now;
  return TimeDuration();
}

class nsJSEnvironmentObserver final : public nsIObserver {
  ~nsJSEnvironmentObserver() = default;

 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER
};

NS_IMPL_ISUPPORTS(nsJSEnvironmentObserver, nsIObserver)

NS_IMETHODIMP
nsJSEnvironmentObserver::Observe(nsISupports* aSubject, const char* aTopic,
                                 const char16_t* aData) {
  if (!nsCRT::strcmp(aTopic, "memory-pressure")) {
    if (StaticPrefs::javascript_options_gc_on_memory_pressure()) {
      if (sShuttingDown) {
        // Don't GC/CC if we're already shutting down.
        return NS_OK;
      }
      nsDependentString data(aData);
      if (data.EqualsLiteral("low-memory-ongoing")) {
        // Don't GC/CC if we are in an ongoing low-memory state since its very
        // slow and it likely won't help us anyway.
        return NS_OK;
      }
      if (data.EqualsLiteral("heap-minimize")) {
        // heap-minimize notifiers expect this to run synchronously
        nsJSContext::DoLowMemoryGC();
        return NS_OK;
      }
      if (data.EqualsLiteral("low-memory")) {
        nsJSContext::SetLowMemoryState(true);
      }
      // Asynchronously GC.
      nsJSContext::LowMemoryGC();
    }
  } else if (!nsCRT::strcmp(aTopic, "memory-pressure-stop")) {
    nsJSContext::SetLowMemoryState(false);
  } else if (!nsCRT::strcmp(aTopic, "user-interaction-inactive")) {
    sScheduler->UserIsInactive();
  } else if (!nsCRT::strcmp(aTopic, "user-interaction-active")) {
    sScheduler->UserIsActive();
  } else if (!nsCRT::strcmp(aTopic, "quit-application") ||
             !nsCRT::strcmp(aTopic, NS_XPCOM_SHUTDOWN_OBSERVER_ID) ||
             !nsCRT::strcmp(aTopic, "content-child-will-shutdown")) {
    sShuttingDown = true;
    sScheduler->Shutdown();
  }

  return NS_OK;
}

/****************************************************************
 ************************** AutoFree ****************************
 ****************************************************************/

class AutoFree {
 public:
  explicit AutoFree(void* aPtr) : mPtr(aPtr) {}
  ~AutoFree() {
    if (mPtr) free(mPtr);
  }
  void Invalidate() { mPtr = nullptr; }

 private:
  void* mPtr;
};

// A utility function for script languages to call.  Although it looks small,
// the use of nsIDocShell and nsPresContext triggers a huge number of
// dependencies that most languages would not otherwise need.
// XXXmarkh - This function is mis-placed!
bool NS_HandleScriptError(nsIScriptGlobalObject* aScriptGlobal,
                          const ErrorEventInit& aErrorEventInit,
                          nsEventStatus* aStatus) {
  bool called = false;
  nsCOMPtr<nsPIDOMWindowInner> win(do_QueryInterface(aScriptGlobal));
  nsIDocShell* docShell = win ? win->GetDocShell() : nullptr;
  if (docShell) {
    RefPtr<nsPresContext> presContext = docShell->GetPresContext();

    static int32_t errorDepth;  // Recursion prevention
    ++errorDepth;

    if (errorDepth < 2) {
      // Dispatch() must be synchronous for the recursion block
      // (errorDepth) to work.
      RefPtr<ErrorEvent> event = ErrorEvent::Constructor(
          nsGlobalWindowInner::Cast(win), u"error"_ns, aErrorEventInit);
      event->SetTrusted(true);

      // MOZ_KnownLive due to bug 1506441
      EventDispatcher::DispatchDOMEvent(
          MOZ_KnownLive(nsGlobalWindowInner::Cast(win)), nullptr, event,
          presContext, aStatus);
      called = true;
    }
    --errorDepth;
  }
  return called;
}

class ScriptErrorEvent : public Runnable {
 public:
  ScriptErrorEvent(nsPIDOMWindowInner* aWindow, JS::RootingContext* aRootingCx,
                   xpc::ErrorReport* aReport, JS::Handle<JS::Value> aError,
                   JS::Handle<JSObject*> aErrorStack)
      : mozilla::Runnable("ScriptErrorEvent"),
        mWindow(aWindow),
        mReport(aReport),
        mError(aRootingCx, aError),
        mErrorStack(aRootingCx, aErrorStack) {}

  // TODO: Convert this to MOZ_CAN_RUN_SCRIPT (bug 1415230, bug 1535398)
  MOZ_CAN_RUN_SCRIPT_BOUNDARY NS_IMETHOD Run() override {
    nsEventStatus status = nsEventStatus_eIgnore;
    nsCOMPtr<nsPIDOMWindowInner> win = mWindow;
    MOZ_ASSERT(win);
    MOZ_ASSERT(NS_IsMainThread());
    // First, notify the DOM that we have a script error, but only if
    // our window is still the current inner.
    JS::RootingContext* rootingCx = RootingCx();
    if (win->IsCurrentInnerWindow() && win->GetDocShell() &&
        !sHandlingScriptError) {
      AutoRestore<bool> recursionGuard(sHandlingScriptError);
      sHandlingScriptError = true;

      RefPtr<nsPresContext> presContext = win->GetDocShell()->GetPresContext();

      RootedDictionary<ErrorEventInit> init(rootingCx);
      init.mCancelable = true;
      init.mFilename = mReport->mFileName;
      init.mBubbles = true;

      constexpr auto xoriginMsg = u"Script error."_ns;
      if (!mReport->mIsMuted) {
        init.mMessage = mReport->mErrorMsg;
        init.mLineno = mReport->mLineNumber;
        init.mColno = mReport->mColumn;
        init.mError = mError;
      } else {
        NS_WARNING("Not same origin error!");
        init.mMessage = xoriginMsg;
        init.mLineno = 0;
      }

      RefPtr<ErrorEvent> event = ErrorEvent::Constructor(
          nsGlobalWindowInner::Cast(win), u"error"_ns, init);
      event->SetTrusted(true);

      // MOZ_KnownLive due to bug 1506441
      EventDispatcher::DispatchDOMEvent(
          MOZ_KnownLive(nsGlobalWindowInner::Cast(win)), nullptr, event,
          presContext, &status);
    }

    if (status != nsEventStatus_eConsumeNoDefault) {
      JS::Rooted<JSObject*> stack(rootingCx);
      JS::Rooted<JSObject*> stackGlobal(rootingCx);
      xpc::FindExceptionStackForConsoleReport(win, mError, mErrorStack, &stack,
                                              &stackGlobal);
      JS::Rooted<Maybe<JS::Value>> exception(rootingCx, Some(mError));
      nsGlobalWindowInner* inner = nsGlobalWindowInner::Cast(win);
      mReport->LogToConsoleWithStack(inner, exception, stack, stackGlobal);
    }

    return NS_OK;
  }

 private:
  nsCOMPtr<nsPIDOMWindowInner> mWindow;
  RefPtr<xpc::ErrorReport> mReport;
  JS::PersistentRooted<JS::Value> mError;
  JS::PersistentRooted<JSObject*> mErrorStack;

  static bool sHandlingScriptError;
};

bool ScriptErrorEvent::sHandlingScriptError = false;

// This temporarily lives here to avoid code churn. It will go away entirely
// soon.
namespace xpc {

void DispatchScriptErrorEvent(nsPIDOMWindowInner* win,
                              JS::RootingContext* rootingCx,
                              xpc::ErrorReport* xpcReport,
                              JS::Handle<JS::Value> exception,
                              JS::Handle<JSObject*> exceptionStack) {
  nsContentUtils::AddScriptRunner(MakeAndAddRef<ScriptErrorEvent>(
      win, rootingCx, xpcReport, exception, exceptionStack));
}

} /* namespace xpc */

#ifdef DEBUG
// A couple of useful functions to call when you're debugging.
nsGlobalWindowInner* JSObject2Win(JSObject* obj) {
  return xpc::WindowOrNull(obj);
}

template <typename T>
void PrintWinURI(T* win) {
  if (!win) {
    printf("No window passed in.\n");
    return;
  }

  nsCOMPtr<Document> doc = win->GetExtantDoc();
  if (!doc) {
    printf("No document in the window.\n");
    return;
  }

  nsIURI* uri = doc->GetDocumentURI();
  if (!uri) {
    printf("Document doesn't have a URI.\n");
    return;
  }

  printf("%s\n", uri->GetSpecOrDefault().get());
}

void PrintWinURIInner(nsGlobalWindowInner* aWin) { return PrintWinURI(aWin); }

void PrintWinURIOuter(nsGlobalWindowOuter* aWin) { return PrintWinURI(aWin); }

template <typename T>
void PrintWinCodebase(T* win) {
  if (!win) {
    printf("No window passed in.\n");
    return;
  }

  nsIPrincipal* prin = win->GetPrincipal();
  if (!prin) {
    printf("Window doesn't have principals.\n");
    return;
  }
  if (prin->IsSystemPrincipal()) {
    printf("No URI, it's the system principal.\n");
    return;
  }
  nsCString spec;
  prin->GetAsciiSpec(spec);
  printf("%s\n", spec.get());
}

void PrintWinCodebaseInner(nsGlobalWindowInner* aWin) {
  return PrintWinCodebase(aWin);
}

void PrintWinCodebaseOuter(nsGlobalWindowOuter* aWin) {
  return PrintWinCodebase(aWin);
}

void DumpString(const nsAString& str) {
  printf("%s\n", NS_ConvertUTF16toUTF8(str).get());
}
#endif

nsJSContext::nsJSContext(bool aGCOnDestruction,
                         nsIScriptGlobalObject* aGlobalObject)
    : mWindowProxy(nullptr),
      mGCOnDestruction(aGCOnDestruction),
      mGlobalObjectRef(aGlobalObject) {
  EnsureStatics();

  mProcessingScriptTag = false;
  HoldJSObjects(this);
}

nsJSContext::~nsJSContext() {
  mGlobalObjectRef = nullptr;

  Destroy();
}

void nsJSContext::Destroy() {
  if (mGCOnDestruction) {
    sScheduler->PokeGC(JS::GCReason::NSJSCONTEXT_DESTROY, mWindowProxy);
  }

  DropJSObjects(this);
}

// QueryInterface implementation for nsJSContext
NS_IMPL_CYCLE_COLLECTION_CLASS(nsJSContext)

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN(nsJSContext)
  NS_IMPL_CYCLE_COLLECTION_TRACE_JS_MEMBER_CALLBACK(mWindowProxy)
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(nsJSContext)
  tmp->mGCOnDestruction = false;
  tmp->mWindowProxy = nullptr;
  tmp->Destroy();
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mGlobalObjectRef)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(nsJSContext)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mGlobalObjectRef)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsJSContext)
  NS_INTERFACE_MAP_ENTRY(nsIScriptContext)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsJSContext)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsJSContext)

#ifdef DEBUG
bool AtomIsEventHandlerName(nsAtom* aName) {
  const char16_t* name = aName->GetUTF16String();

  const char16_t* cp;
  char16_t c;
  for (cp = name; *cp != '\0'; ++cp) {
    c = *cp;
    if ((c < 'A' || c > 'Z') && (c < 'a' || c > 'z')) return false;
  }

  return true;
}
#endif

nsIScriptGlobalObject* nsJSContext::GetGlobalObject() {
  // Note: this could probably be simplified somewhat more; see bug 974327
  // comments 1 and 3.
  if (!mWindowProxy) {
    return nullptr;
  }

  MOZ_ASSERT(mGlobalObjectRef);
  return mGlobalObjectRef;
}

nsresult nsJSContext::SetProperty(JS::Handle<JSObject*> aTarget,
                                  const char* aPropName, nsISupports* aArgs) {
  AutoJSAPI jsapi;
  if (NS_WARN_IF(!jsapi.Init(GetGlobalObject()))) {
    return NS_ERROR_FAILURE;
  }
  JSContext* cx = jsapi.cx();

  JS::RootedVector<JS::Value> args(cx);

  JS::Rooted<JSObject*> global(cx, GetWindowProxy());
  nsresult rv = ConvertSupportsTojsvals(cx, aArgs, global, &args);
  NS_ENSURE_SUCCESS(rv, rv);

  // got the arguments, now attach them.

  for (uint32_t i = 0; i < args.length(); ++i) {
    if (!JS_WrapValue(cx, args[i])) {
      return NS_ERROR_FAILURE;
    }
  }

  JS::Rooted<JSObject*> array(cx, JS::NewArrayObject(cx, args));
  if (!array) {
    return NS_ERROR_FAILURE;
  }

  return JS_DefineProperty(cx, aTarget, aPropName, array, 0) ? NS_OK
                                                             : NS_ERROR_FAILURE;
}

nsresult nsJSContext::ConvertSupportsTojsvals(
    JSContext* aCx, nsISupports* aArgs, JS::Handle<JSObject*> aScope,
    JS::MutableHandleVector<JS::Value> aArgsOut) {
  nsresult rv = NS_OK;

  // If the array implements nsIJSArgArray, copy the contents and return.
  nsCOMPtr<nsIJSArgArray> fastArray = do_QueryInterface(aArgs);
  if (fastArray) {
    uint32_t argc;
    JS::Value* argv;
    rv = fastArray->GetArgs(&argc, reinterpret_cast<void**>(&argv));
    if (NS_SUCCEEDED(rv) && !aArgsOut.append(argv, argc)) {
      rv = NS_ERROR_OUT_OF_MEMORY;
    }
    return rv;
  }

  // Take the slower path converting each item.
  // Handle only nsIArray and nsIVariant.  nsIArray is only needed for
  // SetProperty('arguments', ...);

  nsIXPConnect* xpc = nsContentUtils::XPConnect();
  NS_ENSURE_TRUE(xpc, NS_ERROR_UNEXPECTED);

  if (!aArgs) return NS_OK;
  uint32_t argCount;
  // This general purpose function may need to convert an arg array
  // (window.arguments, event-handler args) and a generic property.
  nsCOMPtr<nsIArray> argsArray(do_QueryInterface(aArgs));

  if (argsArray) {
    rv = argsArray->GetLength(&argCount);
    NS_ENSURE_SUCCESS(rv, rv);
    if (argCount == 0) return NS_OK;
  } else {
    argCount = 1;  // the nsISupports which is not an array
  }

  // Use the caller's auto guards to release and unroot.
  if (!aArgsOut.resize(argCount)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  if (argsArray) {
    for (uint32_t argCtr = 0; argCtr < argCount && NS_SUCCEEDED(rv); argCtr++) {
      nsCOMPtr<nsISupports> arg;
      JS::MutableHandle<JS::Value> thisVal = aArgsOut[argCtr];
      argsArray->QueryElementAt(argCtr, NS_GET_IID(nsISupports),
                                getter_AddRefs(arg));
      if (!arg) {
        thisVal.setNull();
        continue;
      }
      nsCOMPtr<nsIVariant> variant(do_QueryInterface(arg));
      if (variant != nullptr) {
        rv = xpc->VariantToJS(aCx, aScope, variant, thisVal);
      } else {
        // And finally, support the nsISupportsPrimitives supplied
        // by the AppShell.  It generally will pass only strings, but
        // as we have code for handling all, we may as well use it.
        rv = AddSupportsPrimitiveTojsvals(aCx, arg, thisVal.address());
        if (rv == NS_ERROR_NO_INTERFACE) {
          // something else - probably an event object or similar -
          // just wrap it.
#ifdef DEBUG
          // but first, check its not another nsISupportsPrimitive, as
          // these are now deprecated for use with script contexts.
          nsCOMPtr<nsISupportsPrimitive> prim(do_QueryInterface(arg));
          NS_ASSERTION(prim == nullptr,
                       "Don't pass nsISupportsPrimitives - use nsIVariant!");
#endif
          JSAutoRealm ar(aCx, aScope);
          rv = nsContentUtils::WrapNative(aCx, arg, thisVal);
        }
      }
    }
  } else {
    nsCOMPtr<nsIVariant> variant = do_QueryInterface(aArgs);
    if (variant) {
      rv = xpc->VariantToJS(aCx, aScope, variant, aArgsOut[0]);
    } else {
      NS_ERROR("Not an array, not an interface?");
      rv = NS_ERROR_UNEXPECTED;
    }
  }
  return rv;
}

// This really should go into xpconnect somewhere...
nsresult nsJSContext::AddSupportsPrimitiveTojsvals(JSContext* aCx,
                                                   nsISupports* aArg,
                                                   JS::Value* aArgv) {
  MOZ_ASSERT(aArg, "Empty arg");

  nsCOMPtr<nsISupportsPrimitive> argPrimitive(do_QueryInterface(aArg));
  if (!argPrimitive) return NS_ERROR_NO_INTERFACE;

  uint16_t type;
  argPrimitive->GetType(&type);

  switch (type) {
    case nsISupportsPrimitive::TYPE_CSTRING: {
      nsCOMPtr<nsISupportsCString> p(do_QueryInterface(argPrimitive));
      NS_ENSURE_TRUE(p, NS_ERROR_UNEXPECTED);

      nsAutoCString data;

      p->GetData(data);

      JSString* str = ::JS_NewStringCopyN(aCx, data.get(), data.Length());
      NS_ENSURE_TRUE(str, NS_ERROR_OUT_OF_MEMORY);

      aArgv->setString(str);

      break;
    }
    case nsISupportsPrimitive::TYPE_STRING: {
      nsCOMPtr<nsISupportsString> p(do_QueryInterface(argPrimitive));
      NS_ENSURE_TRUE(p, NS_ERROR_UNEXPECTED);

      nsAutoString data;

      p->GetData(data);

      // cast is probably safe since wchar_t and char16_t are expected
      // to be equivalent; both unsigned 16-bit entities
      JSString* str = ::JS_NewUCStringCopyN(aCx, data.get(), data.Length());
      NS_ENSURE_TRUE(str, NS_ERROR_OUT_OF_MEMORY);

      aArgv->setString(str);
      break;
    }
    case nsISupportsPrimitive::TYPE_PRBOOL: {
      nsCOMPtr<nsISupportsPRBool> p(do_QueryInterface(argPrimitive));
      NS_ENSURE_TRUE(p, NS_ERROR_UNEXPECTED);

      bool data;

      p->GetData(&data);

      aArgv->setBoolean(data);

      break;
    }
    case nsISupportsPrimitive::TYPE_PRUINT8: {
      nsCOMPtr<nsISupportsPRUint8> p(do_QueryInterface(argPrimitive));
      NS_ENSURE_TRUE(p, NS_ERROR_UNEXPECTED);

      uint8_t data;

      p->GetData(&data);

      aArgv->setInt32(data);

      break;
    }
    case nsISupportsPrimitive::TYPE_PRUINT16: {
      nsCOMPtr<nsISupportsPRUint16> p(do_QueryInterface(argPrimitive));
      NS_ENSURE_TRUE(p, NS_ERROR_UNEXPECTED);

      uint16_t data;

      p->GetData(&data);

      aArgv->setInt32(data);

      break;
    }
    case nsISupportsPrimitive::TYPE_PRUINT32: {
      nsCOMPtr<nsISupportsPRUint32> p(do_QueryInterface(argPrimitive));
      NS_ENSURE_TRUE(p, NS_ERROR_UNEXPECTED);

      uint32_t data;

      p->GetData(&data);

      aArgv->setInt32(data);

      break;
    }
    case nsISupportsPrimitive::TYPE_CHAR: {
      nsCOMPtr<nsISupportsChar> p(do_QueryInterface(argPrimitive));
      NS_ENSURE_TRUE(p, NS_ERROR_UNEXPECTED);

      char data;

      p->GetData(&data);

      JSString* str = ::JS_NewStringCopyN(aCx, &data, 1);
      NS_ENSURE_TRUE(str, NS_ERROR_OUT_OF_MEMORY);

      aArgv->setString(str);

      break;
    }
    case nsISupportsPrimitive::TYPE_PRINT16: {
      nsCOMPtr<nsISupportsPRInt16> p(do_QueryInterface(argPrimitive));
      NS_ENSURE_TRUE(p, NS_ERROR_UNEXPECTED);

      int16_t data;

      p->GetData(&data);

      aArgv->setInt32(data);

      break;
    }
    case nsISupportsPrimitive::TYPE_PRINT32: {
      nsCOMPtr<nsISupportsPRInt32> p(do_QueryInterface(argPrimitive));
      NS_ENSURE_TRUE(p, NS_ERROR_UNEXPECTED);

      int32_t data;

      p->GetData(&data);

      aArgv->setInt32(data);

      break;
    }
    case nsISupportsPrimitive::TYPE_FLOAT: {
      nsCOMPtr<nsISupportsFloat> p(do_QueryInterface(argPrimitive));
      NS_ENSURE_TRUE(p, NS_ERROR_UNEXPECTED);

      float data;

      p->GetData(&data);

      aArgv->setNumber(data);

      break;
    }
    case nsISupportsPrimitive::TYPE_DOUBLE: {
      nsCOMPtr<nsISupportsDouble> p(do_QueryInterface(argPrimitive));
      NS_ENSURE_TRUE(p, NS_ERROR_UNEXPECTED);

      double data;

      p->GetData(&data);

      aArgv->setNumber(data);

      break;
    }
    case nsISupportsPrimitive::TYPE_INTERFACE_POINTER: {
      nsCOMPtr<nsISupportsInterfacePointer> p(do_QueryInterface(argPrimitive));
      NS_ENSURE_TRUE(p, NS_ERROR_UNEXPECTED);

      nsCOMPtr<nsISupports> data;
      nsIID* iid = nullptr;

      p->GetData(getter_AddRefs(data));
      p->GetDataIID(&iid);
      NS_ENSURE_TRUE(iid, NS_ERROR_UNEXPECTED);

      AutoFree iidGuard(iid);  // Free iid upon destruction.

      JS::Rooted<JSObject*> scope(aCx, GetWindowProxy());
      JS::Rooted<JS::Value> v(aCx);
      JSAutoRealm ar(aCx, scope);
      nsresult rv = nsContentUtils::WrapNative(aCx, data, iid, &v);
      NS_ENSURE_SUCCESS(rv, rv);

      *aArgv = v;

      break;
    }
    case nsISupportsPrimitive::TYPE_ID:
    case nsISupportsPrimitive::TYPE_PRUINT64:
    case nsISupportsPrimitive::TYPE_PRINT64:
    case nsISupportsPrimitive::TYPE_PRTIME: {
      NS_WARNING("Unsupported primitive type used");
      aArgv->setNull();
      break;
    }
    default: {
      NS_WARNING("Unknown primitive type used");
      aArgv->setNull();
      break;
    }
  }
  return NS_OK;
}

nsresult nsJSContext::InitClasses(JS::Handle<JSObject*> aGlobalObj) {
  AutoJSAPI jsapi;
  jsapi.Init();
  JSContext* cx = jsapi.cx();
  JSAutoRealm ar(cx, aGlobalObj);

  return NS_OK;
}

bool nsJSContext::GetProcessingScriptTag() { return mProcessingScriptTag; }

void nsJSContext::SetProcessingScriptTag(bool aFlag) {
  mProcessingScriptTag = aFlag;
}

// static
void nsJSContext::SetLowMemoryState(bool aState) {
  JSContext* cx = danger::GetJSContext();
  JS::SetLowMemoryState(cx, aState);
}

static void GarbageCollectImpl(JS::GCReason aReason,
                               nsJSContext::IsShrinking aShrinking,
                               const JS::SliceBudget& aBudget) {
  AUTO_PROFILER_LABEL_DYNAMIC_CSTR_NONSENSITIVE(
      "nsJSContext::GarbageCollectNow", GCCC, JS::ExplainGCReason(aReason));

  bool wantIncremental = !aBudget.isUnlimited();

  // We use danger::GetJSContext() since AutoJSAPI will assert if the current
  // thread's context is null (such as during shutdown).
  JSContext* cx = danger::GetJSContext();

  if (!nsContentUtils::XPConnect() || !cx) {
    return;
  }

  if (sScheduler->InIncrementalGC() && wantIncremental) {
    // We're in the middle of incremental GC. Do another slice.
    JS::PrepareForIncrementalGC(cx);
    JS::IncrementalGCSlice(cx, aReason, aBudget);
    return;
  }

  JS::GCOptions options = aShrinking == nsJSContext::ShrinkingGC
                              ? JS::GCOptions::Shrink
                              : JS::GCOptions::Normal;

  if (!wantIncremental || aReason == JS::GCReason::FULL_GC_TIMER) {
    sScheduler->SetNeedsFullGC();
  }

  if (sScheduler->NeedsFullGC()) {
    JS::PrepareForFullGC(cx);
  }

  if (wantIncremental) {
    // Incremental GC slices will be triggered by the GC Runner. If one doesn't
    // already exist, create it in the GC_SLICE_END callback for the first
    // slice being executed here.
    JS::StartIncrementalGC(cx, options, aReason, aBudget);
  } else {
    JS::NonIncrementalGC(cx, options, aReason);
  }
}

// static
void nsJSContext::GarbageCollectNow(JS::GCReason aReason,
                                    IsShrinking aShrinking) {
  GarbageCollectImpl(aReason, aShrinking, JS::SliceBudget::unlimited());
}

// static
void nsJSContext::RunIncrementalGCSlice(JS::GCReason aReason,
                                        IsShrinking aShrinking,
                                        JS::SliceBudget& aBudget) {
  AUTO_PROFILER_LABEL_RELEVANT_FOR_JS("Incremental GC", GCCC);
  GarbageCollectImpl(aReason, aShrinking, aBudget);
}

static void FinishAnyIncrementalGC() {
  AUTO_PROFILER_LABEL("FinishAnyIncrementalGC", GCCC);

  if (sScheduler->InIncrementalGC()) {
    AutoJSAPI jsapi;
    jsapi.Init();

    // We're in the middle of an incremental GC, so finish it.
    JS::PrepareForIncrementalGC(jsapi.cx());
    JS::FinishIncrementalGC(jsapi.cx(), JS::GCReason::CC_FORCED);
  }
}

static void FireForgetSkippable(bool aRemoveChildless, TimeStamp aDeadline) {
  TimeStamp startTimeStamp = TimeStamp::Now();
  FinishAnyIncrementalGC();

  JS::SliceBudget budget =
      sScheduler->ComputeForgetSkippableBudget(startTimeStamp, aDeadline);
  bool earlyForgetSkippable = sScheduler->IsEarlyForgetSkippable();
  nsCycleCollector_forgetSkippable(startTimeStamp, budget, !aDeadline.IsNull(),
                                   aRemoveChildless, earlyForgetSkippable);
  TimeStamp now = TimeStamp::Now();
  sScheduler->NoteForgetSkippableComplete(now,
                                          nsCycleCollector_suspectedCount());

  TimeDuration duration = now - startTimeStamp;
  if (duration.ToSeconds()) {
    TimeDuration idleDuration;
    if (!aDeadline.IsNull()) {
      if (aDeadline < now) {
        // This slice overflowed the idle period.
        if (aDeadline > startTimeStamp) {
          idleDuration = aDeadline - startTimeStamp;
        }
      } else {
        idleDuration = duration;
      }
    }

    uint32_t percent =
        uint32_t(idleDuration.ToSeconds() / duration.ToSeconds() * 100);
    glean::dom::forget_skippable_during_idle.AccumulateSingleSample(percent);
  }
}

static void MaybeLogStats(const CycleCollectorResults& aResults,
                          uint32_t aCleanups) {
  if (!StaticPrefs::javascript_options_mem_log() && !sCCStats->mFile) {
    return;
  }

  TimeDuration delta = GetCollectionTimeDelta();

  nsCString mergeMsg;
  if (aResults.mMergedZones) {
    mergeMsg.AssignLiteral(" merged");
  }

  nsCString gcMsg;
  if (aResults.mForcedGC) {
    gcMsg.AssignLiteral(", forced a GC");
  }

  const char16_t* kFmt =
      u"CC(T+%.1f)[%s-%i] max pause: %.fms, total time: %.fms, slices: %lu, "
      u"suspected: %lu, visited: %lu RCed and %lu%s GCed, collected: %lu "
      u"RCed and %lu GCed (%lu|%lu|%lu waiting for GC)%s\n"
      u"ForgetSkippable %lu times before CC, min: %.f ms, max: %.f ms, avg: "
      u"%.f ms, total: %.f ms, max sync: %.f ms, removed: %lu";
  nsString msg;
  nsTextFormatter::ssprintf(
      msg, kFmt, delta.ToMicroseconds() / PR_USEC_PER_SEC,
      ProcessNameForCollectorLog(), getpid(),
      sCCStats->mMaxSliceTime.ToMilliseconds(),
      sCCStats->mTotalSliceTime.ToMilliseconds(), aResults.mNumSlices,
      sCCStats->mSuspected, aResults.mVisitedRefCounted, aResults.mVisitedGCed,
      mergeMsg.get(), aResults.mFreedRefCounted, aResults.mFreedGCed,
      sScheduler->mCCollectedWaitingForGC,
      sScheduler->mCCollectedZonesWaitingForGC,
      sScheduler->mLikelyShortLivingObjectsNeedingGC, gcMsg.get(),
      sCCStats->mForgetSkippableBeforeCC,
      sCCStats->mMinForgetSkippableTime.ToMilliseconds(),
      sCCStats->mMaxForgetSkippableTime.ToMilliseconds(),
      sCCStats->mTotalForgetSkippableTime.ToMilliseconds() / aCleanups,
      sCCStats->mTotalForgetSkippableTime.ToMilliseconds(),
      sCCStats->mMaxSkippableDuration.ToMilliseconds(),
      sCCStats->mRemovedPurples);
  if (StaticPrefs::javascript_options_mem_log()) {
    nsCOMPtr<nsIConsoleService> cs =
        do_GetService(NS_CONSOLESERVICE_CONTRACTID);
    if (cs) {
      cs->LogStringMessage(msg.get());
    }
  }
  if (sCCStats->mFile) {
    fprintf(sCCStats->mFile, "%s\n", NS_ConvertUTF16toUTF8(msg).get());
  }
}

static void MaybeNotifyStats(const CycleCollectorResults& aResults,
                             TimeDuration aCCNowDuration, uint32_t aCleanups) {
  if (!StaticPrefs::javascript_options_mem_notify()) {
    return;
  }

  const char16_t* kJSONFmt =
      u"{ \"timestamp\": %llu, "
      u"\"duration\": %.f, "
      u"\"max_slice_pause\": %.f, "
      u"\"total_slice_pause\": %.f, "
      u"\"max_finish_gc_duration\": %.f, "
      u"\"max_sync_skippable_duration\": %.f, "
      u"\"suspected\": %lu, "
      u"\"visited\": { "
      u"\"RCed\": %lu, "
      u"\"GCed\": %lu }, "
      u"\"collected\": { "
      u"\"RCed\": %lu, "
      u"\"GCed\": %lu }, "
      u"\"waiting_for_gc\": %lu, "
      u"\"zones_waiting_for_gc\": %lu, "
      u"\"short_living_objects_waiting_for_gc\": %lu, "
      u"\"forced_gc\": %d, "
      u"\"forget_skippable\": { "
      u"\"times_before_cc\": %lu, "
      u"\"min\": %.f, "
      u"\"max\": %.f, "
      u"\"avg\": %.f, "
      u"\"total\": %.f, "
      u"\"removed\": %lu } "
      u"}";

  nsString json;
  nsTextFormatter::ssprintf(
      json, kJSONFmt, PR_Now(), aCCNowDuration.ToMilliseconds(),
      sCCStats->mMaxSliceTime.ToMilliseconds(),
      sCCStats->mTotalSliceTime.ToMilliseconds(),
      sCCStats->mMaxGCDuration.ToMilliseconds(),
      sCCStats->mMaxSkippableDuration.ToMilliseconds(), sCCStats->mSuspected,
      aResults.mVisitedRefCounted, aResults.mVisitedGCed,
      aResults.mFreedRefCounted, aResults.mFreedGCed,
      sScheduler->mCCollectedWaitingForGC,
      sScheduler->mCCollectedZonesWaitingForGC,
      sScheduler->mLikelyShortLivingObjectsNeedingGC, aResults.mForcedGC,
      sCCStats->mForgetSkippableBeforeCC,
      sCCStats->mMinForgetSkippableTime.ToMilliseconds(),
      sCCStats->mMaxForgetSkippableTime.ToMilliseconds(),
      sCCStats->mTotalForgetSkippableTime.ToMilliseconds() / aCleanups,
      sCCStats->mTotalForgetSkippableTime.ToMilliseconds(),
      sCCStats->mRemovedPurples);
  nsCOMPtr<nsIObserverService> observerService =
      mozilla::services::GetObserverService();
  if (observerService) {
    observerService->NotifyObservers(nullptr, "cycle-collection-statistics",
                                     json.get());
  }
}

// static
void nsJSContext::CycleCollectNow(CCReason aReason,
                                  nsICycleCollectorListener* aListener) {
  if (!NS_IsMainThread()) {
    return;
  }

  AUTO_PROFILER_LABEL("nsJSContext::CycleCollectNow", GCCC);

  PrepareForCycleCollectionSlice(aReason, TimeStamp());
  nsCycleCollector_collect(aReason, aListener);
  sCCStats->AfterCycleCollectionSlice();
}

// static
void nsJSContext::PrepareForCycleCollectionSlice(CCReason aReason,
                                                 TimeStamp aDeadline) {
  TimeStamp beginTime = TimeStamp::Now();

  // Before we begin the cycle collection, make sure there is no active GC.
  TimeStamp afterGCTime;
  if (sScheduler->InIncrementalGC()) {
    FinishAnyIncrementalGC();
    afterGCTime = TimeStamp::Now();
  }

  if (!sScheduler->IsCollectingCycles()) {
    sCCStats->PrepareForCycleCollection(beginTime);
    sScheduler->NoteCCBegin();
  }

  sCCStats->AfterPrepareForCycleCollectionSlice(aDeadline, beginTime,
                                                afterGCTime);
}

// static
void nsJSContext::RunCycleCollectorSlice(CCReason aReason,
                                         TimeStamp aDeadline) {
  if (!NS_IsMainThread()) {
    return;
  }

  PrepareForCycleCollectionSlice(aReason, aDeadline);

  // Decide how long we want to budget for this slice.
  if (sIncrementalCC) {
    bool preferShorterSlices;
    JS::SliceBudget budget = sScheduler->ComputeCCSliceBudget(
        aDeadline, sCCStats->mBeginTime, sCCStats->mEndSliceTime,
        TimeStamp::Now(), &preferShorterSlices);
    nsCycleCollector_collectSlice(budget, aReason, preferShorterSlices);
  } else {
    JS::SliceBudget budget = JS::SliceBudget::unlimited();
    nsCycleCollector_collectSlice(budget, aReason, false);
  }

  sCCStats->AfterCycleCollectionSlice();
}

// static
void nsJSContext::RunCycleCollectorWorkSlice(int64_t aWorkBudget) {
  if (!NS_IsMainThread()) {
    return;
  }

  AUTO_PROFILER_LABEL("nsJSContext::RunCycleCollectorWorkSlice", GCCC);

  PrepareForCycleCollectionSlice(CCReason::API, TimeStamp());

  JS::SliceBudget budget = JS::SliceBudget(JS::WorkBudget(aWorkBudget));
  nsCycleCollector_collectSlice(budget, CCReason::API);

  sCCStats->AfterCycleCollectionSlice();
}

void nsJSContext::ClearMaxCCSliceTime() {
  sCCStats->mMaxSliceTimeSinceClear = TimeDuration();
}

uint32_t nsJSContext::GetMaxCCSliceTimeSinceClear() {
  return sCCStats->mMaxSliceTimeSinceClear.ToMilliseconds();
}

// static
void nsJSContext::BeginCycleCollectionCallback(CCReason aReason) {
  MOZ_ASSERT(NS_IsMainThread());

  TimeStamp startTime = TimeStamp::Now();
  sCCStats->PrepareForCycleCollection(startTime);

  // Run forgetSkippable synchronously to reduce the size of the CC graph. This
  // is particularly useful if we recently finished a GC.
  if (sScheduler->IsEarlyForgetSkippable()) {
    while (sScheduler->IsEarlyForgetSkippable()) {
      FireForgetSkippable(false, TimeStamp());
    }
    sCCStats->AfterSyncForgetSkippable(startTime);
  }

  if (sShuttingDown) {
    return;
  }

  sScheduler->InitCCRunnerStateMachine(
      mozilla::CCGCScheduler::CCRunnerState::CycleCollecting, aReason);
  sScheduler->EnsureCCRunner(kICCIntersliceDelay, kIdleICCSliceBudget);
}

// static
void nsJSContext::EndCycleCollectionCallback(
    const CycleCollectorResults& aResults) {
  MOZ_ASSERT(NS_IsMainThread());

  sScheduler->KillCCRunner();

  // Update timing information for the current slice before we log it, if
  // we previously called PrepareForCycleCollectionSlice(). During shutdown
  // CCs, this won't happen.
  sCCStats->AfterCycleCollectionSlice();

  TimeStamp endCCTimeStamp = TimeStamp::Now();
  MOZ_ASSERT(endCCTimeStamp >= sCCStats->mBeginTime);
  TimeDuration ccNowDuration = endCCTimeStamp - sCCStats->mBeginTime;
  TimeStamp prevCCEnd = sScheduler->GetLastCCEndTime();

  sScheduler->NoteCCEnd(aResults, endCCTimeStamp);

  // Log information about the CC via telemetry, JSON and the console.

  sCCStats->SendTelemetry(ccNowDuration, prevCCEnd);

  uint32_t cleanups = std::max(sCCStats->mForgetSkippableBeforeCC, 1u);

  MaybeLogStats(aResults, cleanups);

  MaybeNotifyStats(aResults, ccNowDuration, cleanups);

  // Update global state to indicate we have just run a cycle collection.
  sCCStats->Clear();

  // If we need a GC after this CC (typically because lots of GCed objects or
  // zones have been collected in the CC), schedule it.

  if (sScheduler->NeedsGCAfterCC()) {
    MOZ_ASSERT(
        TimeDuration::FromMilliseconds(
            StaticPrefs::javascript_options_gc_delay()) > kMaxICCDuration,
        "A max duration ICC shouldn't reduce GC delay to 0");

    TimeDuration delay;
    if (sScheduler->PreferFasterCollection()) {
      // If we collected lots of objects, trigger the next GC sooner so that
      // GC can cut JS-to-native edges and native objects can be then deleted.
      delay = TimeDuration::FromMilliseconds(
          StaticPrefs::javascript_options_gc_delay_interslice());
    } else {
      delay = TimeDuration::FromMilliseconds(
                  StaticPrefs::javascript_options_gc_delay()) -
              std::min(ccNowDuration, kMaxICCDuration);
    }

    sScheduler->PokeGC(JS::GCReason::CC_FINISHED, nullptr, delay);
  }
#if defined(MOZ_MEMORY)
  else if (
      StaticPrefs::
          dom_memory_foreground_content_processes_have_larger_page_cache()) {
    if (auto* tc = TaskController::Get()) {
      tc->RequestMemoryCleanup("CC completed");
    } else {
      jemalloc_free_dirty_pages();
    }
  }
#endif
}

bool CCGCScheduler::CCRunnerFired(TimeStamp aDeadline) {
  AUTO_PROFILER_LABEL_RELEVANT_FOR_JS("Incremental CC", GCCC);

  if (!aDeadline) {
    mCurrentCollectionHasSeenNonIdle = true;
  } else if (mPreferFasterCollection) {
    // We found some idle time, try to utilize that a bit more given that
    // we're in a mode where idle time is rare.
    aDeadline = aDeadline + TimeDuration::FromMilliseconds(5);
  }

  bool didDoWork = false;

  // The CC/GC scheduler (sScheduler) decides what action(s) to take during
  // this invocation of the CC runner.
  //
  // This may be zero, one, or multiple actions. (Zero is when CC is blocked by
  // incremental GC, or when the scheduler determined that a CC is no longer
  // needed.) Loop until the scheduler finishes this invocation by returning
  // `Yield` in step.mYield.
  CCRunnerStep step;
  do {
    step = sScheduler->AdvanceCCRunner(aDeadline, TimeStamp::Now(),
                                       nsCycleCollector_suspectedCount());
    switch (step.mAction) {
      case CCRunnerAction::None:
        break;

      case CCRunnerAction::MinorGC:
        JS::MaybeRunNurseryCollection(CycleCollectedJSRuntime::Get()->Runtime(),
                                      step.mParam.mReason);
        sScheduler->NoteMinorGCEnd();
        break;

      case CCRunnerAction::ForgetSkippable:
        // 'Forget skippable' only, then end this invocation.
        FireForgetSkippable(bool(step.mParam.mRemoveChildless), aDeadline);
        break;

      case CCRunnerAction::CleanupContentUnbinder:
        // Clear content unbinder before the first actual CC slice.
        Element::ClearContentUnbinder();
        break;

      case CCRunnerAction::CleanupDeferred:
        // and if time still permits, perform deferred deletions.
        nsCycleCollector_doDeferredDeletion();
        break;

      case CCRunnerAction::CycleCollect:
        // Cycle collection slice.
        nsJSContext::RunCycleCollectorSlice(step.mParam.mCCReason, aDeadline);
        break;

      case CCRunnerAction::StopRunning:
        // End this CC, either because we have run a cycle collection slice, or
        // because a CC is no longer needed.
        sScheduler->KillCCRunner();
        break;
    }

    if (step.mAction != CCRunnerAction::None) {
      didDoWork = true;
    }
  } while (step.mYield == CCRunnerYield::Continue);

  return didDoWork;
}

// static
bool nsJSContext::HasHadCleanupSinceLastGC() {
  return sScheduler->IsEarlyForgetSkippable(1);
}

// static
void nsJSContext::RunNextCollectorTimer(JS::GCReason aReason,
                                        mozilla::TimeStamp aDeadline) {
  sScheduler->RunNextCollectorTimer(aReason, aDeadline);
}

// static
void nsJSContext::MaybeRunNextCollectorSlice(nsIDocShell* aDocShell,
                                             JS::GCReason aReason) {
  if (!aDocShell || !XRE_IsContentProcess()) {
    return;
  }

  BrowsingContext* bc = aDocShell->GetBrowsingContext();
  if (!bc) {
    return;
  }

  BrowsingContext* root = bc->Top();
  if (bc == root) {
    // We don't want to run collectors when loading the top level page.
    return;
  }

  nsIDocShell* rootDocShell = root->GetDocShell();
  if (!rootDocShell) {
    return;
  }

  Document* rootDocument = rootDocShell->GetDocument();
  if (!rootDocument ||
      rootDocument->GetReadyStateEnum() != Document::READYSTATE_COMPLETE ||
      rootDocument->IsInBackgroundWindow()) {
    return;
  }

  if (!sScheduler->IsUserActive() &&
      (sScheduler->InIncrementalGC() || sScheduler->IsCollectingCycles())) {
    Maybe<TimeStamp> next = nsRefreshDriver::GetNextTickHint();
    if (next.isSome()) {
      // Try to not delay the next RefreshDriver tick, so give a reasonable
      // deadline for collectors.
      sScheduler->RunNextCollectorTimer(aReason, next.value());
    }
  }

  nsCOMPtr<nsIDocShell> shell = aDocShell;
  NS_DispatchToCurrentThreadQueue(
      NS_NewRunnableFunction("nsJSContext::MaybeRunNextCollectorSlice",
                             [shell] {
                               nsIDocShell::BusyFlags busyFlags =
                                   nsIDocShell::BUSY_FLAGS_NONE;
                               shell->GetBusyFlags(&busyFlags);
                               if (busyFlags == nsIDocShell::BUSY_FLAGS_NONE) {
                                 return;
                               }

                               // In order to improve performance on the next
                               // page, run a minor GC. The 16ms limit ensures
                               // it isn't called all the time if there are for
                               // example multiple iframes loading at the same
                               // time.
                               JS::RunNurseryCollection(
                                   CycleCollectedJSRuntime::Get()->Runtime(),
                                   JS::GCReason::PREPARE_FOR_PAGELOAD,
                                   mozilla::TimeDuration::FromMilliseconds(16));
                             }),
      EventQueuePriority::Idle);
}

// static
void nsJSContext::PokeGC(JS::GCReason aReason, JSObject* aObj,
                         TimeDuration aDelay) {
  sScheduler->PokeGC(aReason, aObj, aDelay);
}

// static
void nsJSContext::MaybePokeGC() {
  if (sShuttingDown) {
    return;
  }

  JSRuntime* rt = CycleCollectedJSRuntime::Get()->Runtime();
  JS::GCReason reason = JS::WantEagerMinorGC(rt);
  if (reason != JS::GCReason::NO_REASON) {
    MOZ_ASSERT(reason == JS::GCReason::EAGER_NURSERY_COLLECTION);
    sScheduler->PokeMinorGC(reason);
  }

  // Bug 1772638: For now, only do eager minor GCs. Eager major GCs regress some
  // benchmarks. Hopefully that will be worked out and this will check for
  // whether an eager major GC is needed.
}

void nsJSContext::DoLowMemoryGC() {
  if (sShuttingDown) {
    return;
  }
  nsJSContext::GarbageCollectNow(JS::GCReason::MEM_PRESSURE,
                                 nsJSContext::ShrinkingGC);
  nsJSContext::CycleCollectNow(CCReason::MEM_PRESSURE);
  if (sScheduler->NeedsGCAfterCC()) {
    nsJSContext::GarbageCollectNow(JS::GCReason::MEM_PRESSURE,
                                   nsJSContext::ShrinkingGC);
  }
}

// static
void nsJSContext::LowMemoryGC() {
  RefPtr<CCGCScheduler::MayGCPromise> mbPromise =
      CCGCScheduler::MayGCNow(JS::GCReason::MEM_PRESSURE);
  if (!mbPromise) {
    // Normally when the promise is null it means that IPC failed, that probably
    // means that something bad happened, don't bother with the GC.
    return;
  }
  mbPromise->Then(
      GetMainThreadSerialEventTarget(), __func__,
      [](bool aIgnored) { DoLowMemoryGC(); },
      [](mozilla::ipc::ResponseRejectReason r) {});
}

// static
void nsJSContext::MaybePokeCC() {
  sScheduler->MaybePokeCC(TimeStamp::NowLoRes(),
                          nsCycleCollector_suspectedCount());
}

static void DOMGCSliceCallback(JSContext* aCx, JS::GCProgress aProgress,
                               const JS::GCDescription& aDesc) {
  NS_ASSERTION(NS_IsMainThread(), "GCs must run on the main thread");

  static TimeStamp sCurrentGCStartTime;

  switch (aProgress) {
    case JS::GC_CYCLE_BEGIN: {
      // Prevent cycle collections and shrinking during incremental GC.
      sScheduler->NoteGCBegin(aDesc.reason_);
      sCurrentGCStartTime = TimeStamp::Now();
      break;
    }

    case JS::GC_CYCLE_END: {
      TimeDuration delta = GetCollectionTimeDelta();

      if (StaticPrefs::javascript_options_mem_log()) {
        nsString gcstats;
        gcstats.Adopt(aDesc.formatSummaryMessage(aCx));
        nsAutoString prefix;
        nsTextFormatter::ssprintf(prefix, u"GC(T+%.1f)[%s-%i] ",
                                  delta.ToSeconds(),
                                  ProcessNameForCollectorLog(), getpid());
        nsString msg = prefix + gcstats;
        nsCOMPtr<nsIConsoleService> cs =
            do_GetService(NS_CONSOLESERVICE_CONTRACTID);
        if (cs) {
          cs->LogStringMessage(msg.get());
        }
      }

      sScheduler->NoteGCEnd();

      // May need to kill the GC runner
      sScheduler->KillGCRunner();

      nsJSContext::MaybePokeCC();

#if defined(MOZ_MEMORY)
      bool freeDirty = false;
#endif
      if (aDesc.isZone_) {
        sScheduler->PokeFullGC();
      } else {
#if defined(MOZ_MEMORY)
        freeDirty = true;
#endif
        sScheduler->SetNeedsFullGC(false);
        sScheduler->KillFullGCTimer();
      }

      if (sScheduler->IsCCNeeded(TimeStamp::Now(),
                                 nsCycleCollector_suspectedCount()) !=
          CCReason::NO_REASON) {
#if defined(MOZ_MEMORY)
        // We're likely to free the dirty pages after CC.
        freeDirty = false;
#endif
        nsCycleCollector_dispatchDeferredDeletion();
      }

      MOZ_ASSERT(sCurrentGCStartTime);
      glean::dom::gc_in_progress.ProcessGet().AccumulateRawDuration(
          TimeStamp::Now() - sCurrentGCStartTime);

#if defined(MOZ_MEMORY)
      if (freeDirty &&
          StaticPrefs::
              dom_memory_foreground_content_processes_have_larger_page_cache()) {
        if (auto* tc = TaskController::Get()) {
          tc->RequestMemoryCleanup("GC completed");
        } else {
          jemalloc_free_dirty_pages();
        }
      }
#endif
      break;
    }

    case JS::GC_SLICE_BEGIN:
      break;

    case JS::GC_SLICE_END:
      sScheduler->NoteGCSliceEnd(aDesc.lastSliceStart(aCx),
                                 aDesc.lastSliceEnd(aCx));

      if (sShuttingDown) {
        sScheduler->KillGCRunner();
      } else {
        // If incremental GC wasn't triggered by GCTimerFired, we may not have a
        // runner to ensure all the slices are handled. So, create the runner
        // here.
        sScheduler->EnsureOrResetGCRunner();
      }

      if (sScheduler->IsCCNeeded(TimeStamp::Now(),
                                 nsCycleCollector_suspectedCount()) !=
          CCReason::NO_REASON) {
        nsCycleCollector_dispatchDeferredDeletion();
      }

      if (StaticPrefs::javascript_options_mem_log()) {
        nsString gcstats;
        gcstats.Adopt(aDesc.formatSliceMessage(aCx));
        nsAutoString prefix;
        nsTextFormatter::ssprintf(prefix, u"[%s-%i] ",
                                  ProcessNameForCollectorLog(), getpid());
        nsString msg = prefix + gcstats;
        nsCOMPtr<nsIConsoleService> cs =
            do_GetService(NS_CONSOLESERVICE_CONTRACTID);
        if (cs) {
          cs->LogStringMessage(msg.get());
        }
      }

      break;

    default:
      MOZ_CRASH("Unexpected GCProgress value");
  }

  if (sPrevGCSliceCallback) {
    (*sPrevGCSliceCallback)(aCx, aProgress, aDesc);
  }
}

void nsJSContext::SetWindowProxy(JS::Handle<JSObject*> aWindowProxy) {
  mWindowProxy = aWindowProxy;
}

JSObject* nsJSContext::GetWindowProxy() { return mWindowProxy; }

void nsJSContext::LikelyShortLivingObjectCreated() {
  ++sScheduler->mLikelyShortLivingObjectsNeedingGC;
}

void mozilla::dom::StartupJSEnvironment() {
  // initialize all our statics, so that we can restart XPCOM
  sIsInitialized = false;
  sShuttingDown = false;
  sCCStats = CycleCollectorStats::Get();
}

static void SetGCParameter(JSGCParamKey aParam, uint32_t aValue) {
  AutoJSAPI jsapi;
  jsapi.Init();
  JS_SetGCParameter(jsapi.cx(), aParam, aValue);
}

static void ResetGCParameter(JSGCParamKey aParam) {
  AutoJSAPI jsapi;
  jsapi.Init();
  JS_ResetGCParameter(jsapi.cx(), aParam);
}

static void SetMemoryPrefChangedCallbackMB(const char* aPrefName,
                                           void* aClosure) {
  int32_t prefMB = Preferences::GetInt(aPrefName, -1);
  // handle overflow and negative pref values
  CheckedInt<int32_t> prefB = CheckedInt<int32_t>(prefMB) * 1024 * 1024;
  if (prefB.isValid() && prefB.value() >= 0) {
    SetGCParameter((JSGCParamKey)(uintptr_t)aClosure, prefB.value());
  } else {
    ResetGCParameter((JSGCParamKey)(uintptr_t)aClosure);
  }
}

static void SetMemoryNurseryPrefChangedCallback(const char* aPrefName,
                                                void* aClosure) {
  int32_t prefKB = Preferences::GetInt(aPrefName, -1);
  // handle overflow and negative pref values
  CheckedInt<int32_t> prefB = CheckedInt<int32_t>(prefKB) * 1024;
  if (prefB.isValid() && prefB.value() >= 0) {
    SetGCParameter((JSGCParamKey)(uintptr_t)aClosure, prefB.value());
  } else {
    ResetGCParameter((JSGCParamKey)(uintptr_t)aClosure);
  }
}

static void SetMemoryPrefChangedCallbackInt(const char* aPrefName,
                                            void* aClosure) {
  int32_t pref = Preferences::GetInt(aPrefName, -1);
  // handle overflow and negative pref values
  if (pref >= 0 && pref < 10000) {
    SetGCParameter((JSGCParamKey)(uintptr_t)aClosure, pref);
  } else {
    ResetGCParameter((JSGCParamKey)(uintptr_t)aClosure);
  }
}

static void SetMemoryPrefChangedCallbackBool(const char* aPrefName,
                                             void* aClosure) {
  bool pref = Preferences::GetBool(aPrefName);
  SetGCParameter((JSGCParamKey)(uintptr_t)aClosure, pref);
}

static void SetMemoryGCSliceTimePrefChangedCallback(const char* aPrefName,
                                                    void* aClosure) {
  int32_t pref = Preferences::GetInt(aPrefName, -1);
  // handle overflow and negative pref values
  if (pref > 0 && pref < 100000) {
    sScheduler->SetActiveIntersliceGCBudget(
        TimeDuration::FromMilliseconds(pref));
    SetGCParameter(JSGC_SLICE_TIME_BUDGET_MS, pref);
  } else {
    ResetGCParameter(JSGC_SLICE_TIME_BUDGET_MS);
  }
}

static void SetIncrementalCCPrefChangedCallback(const char* aPrefName,
                                                void* aClosure) {
  bool pref = Preferences::GetBool(aPrefName);
  sIncrementalCC = pref;
}

class JSDispatchableRunnable final : public Runnable {
  ~JSDispatchableRunnable() { MOZ_ASSERT(!mDispatchable); }

 public:
  explicit JSDispatchableRunnable(
      js::UniquePtr<JS::Dispatchable>&& aDispatchable)
      : mozilla::Runnable("JSDispatchableRunnable"),
        mDispatchable(std::move(aDispatchable)) {
    MOZ_ASSERT(mDispatchable);
  }

 protected:
  NS_IMETHOD Run() override {
    MOZ_ASSERT(NS_IsMainThread());

    AutoJSAPI jsapi;
    jsapi.Init();

    JS::Dispatchable::MaybeShuttingDown maybeShuttingDown =
        sShuttingDown ? JS::Dispatchable::ShuttingDown
                      : JS::Dispatchable::NotShuttingDown;

    JS::Dispatchable::Run(jsapi.cx(), std::move(mDispatchable),
                          maybeShuttingDown);
    // mDispatchable is no longer valid after this point.

    return NS_OK;
  }

 private:
  js::UniquePtr<JS::Dispatchable> mDispatchable;
};

static bool DelayedDispatchToEventLoop(
    void* closure, js::UniquePtr<JS::Dispatchable>&& aDispatchable,
    uint32_t aDelay) {
  MOZ_ASSERT(!closure);

  // Unlike DispatchToEventLoop, this is used exclusively on the Main Thread.
  MOZ_ASSERT(NS_IsMainThread());

  nsIGlobalObject* global = GetCurrentGlobal();

  TimeoutManager* timeoutManager = global->GetTimeoutManager();
  if (timeoutManager) {
    JSContext* cx = nsContentUtils::GetCurrentJSContext();
    RefPtr<TimeoutHandler> handler =
        new DelayedJSDispatchableHandler(cx, std::move(aDispatchable));

    int32_t handle;
    timeoutManager->SetTimeout(handler, aDelay, /* aIsInterval */ false,
                               Timeout::Reason::eJSTimeout, &handle);
  } else {
    // Currently only used for waitAsync timeout implementation.
    // We end up in this branch if the global does not have a
    // timeout manager (for example, no innerWindow global).
    // In this case, we reuse the ReleaseFailedTask machinery to
    // cancel the pending associated notify task.
    JS::Dispatchable::ReleaseFailedTask(std::move(aDispatchable));
    return false;
  }

  return true;
}

static bool DispatchToEventLoop(
    void* closure, js::UniquePtr<JS::Dispatchable>&& aDispatchable) {
  MOZ_ASSERT(!closure);

  // This callback may execute either on the main thread or a random JS-internal
  // helper thread. This callback can be called during shutdown so we cannot
  // simply NS_DispatchToMainThread. Failure during shutdown is expected and
  // properly handled by the JS engine.

  nsCOMPtr<nsIEventTarget> mainTarget = GetMainThreadSerialEventTarget();
  if (!mainTarget) {
    // if we have not transfered ownership of the dispatchable to the
    // dispatchable runnable, release it here, so that the JS engine will
    // handle deleting it on JS context shutdown.
    JS::Dispatchable::ReleaseFailedTask(std::move(aDispatchable));
    return false;
  }

  RefPtr<JSDispatchableRunnable> r =
      new JSDispatchableRunnable(std::move(aDispatchable));
  MOZ_ALWAYS_SUCCEEDS(mainTarget->Dispatch(r.forget(), NS_DISPATCH_NORMAL));
  return true;
}

static bool ConsumeStream(JSContext* aCx, JS::Handle<JSObject*> aObj,
                          JS::MimeType aMimeType,
                          JS::StreamConsumer* aConsumer) {
  return FetchUtil::StreamResponseToJS(aCx, aObj, aMimeType, aConsumer,
                                       nullptr);
}

static JS::SliceBudget CreateGCSliceBudget(JS::GCReason aReason,
                                           int64_t aMillis) {
  IdleTaskManager* manager = TaskController::Get()->GetIdleTaskManager();
  bool isIdle = bool(manager->State().GetCachedIdleDeadline());
  return sScheduler->CreateGCSliceBudget(
      mozilla::TimeDuration::FromMilliseconds(aMillis), isIdle, false);
}

void nsJSContext::EnsureStatics() {
  if (sIsInitialized) {
    if (!nsContentUtils::XPConnect()) {
      MOZ_CRASH();
    }
    return;
  }

  // Let's make sure that our main thread is the same as the xpcom main thread.
  MOZ_ASSERT(NS_IsMainThread());

  sScheduler =
      new (&sSchedulerStorage) CCGCScheduler();  // Reset the scheduler state.

  AutoJSAPI jsapi;
  jsapi.Init();

  sPrevGCSliceCallback = JS::SetGCSliceCallback(jsapi.cx(), DOMGCSliceCallback);

  JS::SetCreateGCSliceBudgetCallback(jsapi.cx(), CreateGCSliceBudget);

  JS::InitAsyncTaskCallbacks(jsapi.cx(), DispatchToEventLoop,
                             DelayedDispatchToEventLoop, nullptr, nullptr,
                             nullptr);

  JS::InitConsumeStreamCallback(jsapi.cx(), ConsumeStream,
                                FetchUtil::ReportJSStreamError);

  // Set these global xpconnect options...
  Preferences::RegisterCallbackAndCall(SetMemoryPrefChangedCallbackMB,
                                       "javascript.options.mem.max",
                                       (void*)JSGC_MAX_BYTES);
  Preferences::RegisterCallbackAndCall(SetMemoryNurseryPrefChangedCallback,
                                       "javascript.options.mem.nursery.min_kb",
                                       (void*)JSGC_MIN_NURSERY_BYTES);
  Preferences::RegisterCallbackAndCall(SetMemoryNurseryPrefChangedCallback,
                                       "javascript.options.mem.nursery.max_kb",
                                       (void*)JSGC_MAX_NURSERY_BYTES);

  Preferences::RegisterCallbackAndCall(SetMemoryPrefChangedCallbackBool,
                                       "javascript.options.mem.gc_per_zone",
                                       (void*)JSGC_PER_ZONE_GC_ENABLED);

  Preferences::RegisterCallbackAndCall(SetMemoryPrefChangedCallbackBool,
                                       "javascript.options.mem.gc_incremental",
                                       (void*)JSGC_INCREMENTAL_GC_ENABLED);

  Preferences::RegisterCallbackAndCall(SetMemoryPrefChangedCallbackBool,
                                       "javascript.options.mem.gc_generational",
                                       (void*)JSGC_NURSERY_ENABLED);

  Preferences::RegisterCallbackAndCall(SetMemoryPrefChangedCallbackBool,
                                       "javascript.options.mem.gc_compacting",
                                       (void*)JSGC_COMPACTING_ENABLED);

#ifdef NIGHTLY_BUILD
  Preferences::RegisterCallbackAndCall(
      SetMemoryPrefChangedCallbackBool,
      "javascript.options.mem.gc_experimental_semispace_nursery",
      (void*)JSGC_SEMISPACE_NURSERY_ENABLED);
#endif

  Preferences::RegisterCallbackAndCall(
      SetMemoryPrefChangedCallbackBool,
      "javascript.options.mem.gc_parallel_marking",
      (void*)JSGC_PARALLEL_MARKING_ENABLED);

  Preferences::RegisterCallbackAndCall(
      SetMemoryPrefChangedCallbackInt,
      "javascript.options.mem.gc_parallel_marking_threshold_mb",
      (void*)JSGC_PARALLEL_MARKING_THRESHOLD_MB);

  Preferences::RegisterCallbackAndCall(
      SetMemoryPrefChangedCallbackInt,
      "javascript.options.mem.gc_max_parallel_marking_threads",
      (void*)JSGC_MAX_MARKING_THREADS);

#ifdef JS_GC_CONCURRENT_MARKING
  Preferences::RegisterCallbackAndCall(
      SetMemoryPrefChangedCallbackBool,
      "javascript.options.mem.gc_experimental_concurrent_marking",
      (void*)JSGC_CONCURRENT_MARKING_ENABLED);
#endif

  Preferences::RegisterCallbackAndCall(
      SetMemoryGCSliceTimePrefChangedCallback,
      "javascript.options.mem.gc_incremental_slice_ms");

  Preferences::RegisterCallbackAndCall(
      SetMemoryPrefChangedCallbackBool,
      "javascript.options.mem.incremental_weakmap",
      (void*)JSGC_INCREMENTAL_WEAKMAP_ENABLED);

  Preferences::RegisterCallbackAndCall(
      SetMemoryPrefChangedCallbackInt,
      "javascript.options.mem.gc_high_frequency_time_limit_ms",
      (void*)JSGC_HIGH_FREQUENCY_TIME_LIMIT);

  Preferences::RegisterCallbackAndCall(
      SetMemoryPrefChangedCallbackInt,
      "javascript.options.mem.gc_low_frequency_heap_growth",
      (void*)JSGC_LOW_FREQUENCY_HEAP_GROWTH);

  Preferences::RegisterCallbackAndCall(
      SetMemoryPrefChangedCallbackInt,
      "javascript.options.mem.gc_high_frequency_large_heap_growth",
      (void*)JSGC_HIGH_FREQUENCY_LARGE_HEAP_GROWTH);

  Preferences::RegisterCallbackAndCall(
      SetMemoryPrefChangedCallbackInt,
      "javascript.options.mem.gc_high_frequency_small_heap_growth",
      (void*)JSGC_HIGH_FREQUENCY_SMALL_HEAP_GROWTH);

  Preferences::RegisterCallbackAndCall(
      SetMemoryPrefChangedCallbackBool,
      "javascript.options.mem.gc_balanced_heap_limits",
      (void*)JSGC_BALANCED_HEAP_LIMITS_ENABLED);

  Preferences::RegisterCallbackAndCall(
      SetMemoryPrefChangedCallbackInt,
      "javascript.options.mem.gc_heap_growth_factor",
      (void*)JSGC_HEAP_GROWTH_FACTOR);

  Preferences::RegisterCallbackAndCall(
      SetMemoryPrefChangedCallbackInt,
      "javascript.options.mem.gc_small_heap_size_max_mb",
      (void*)JSGC_SMALL_HEAP_SIZE_MAX);

  Preferences::RegisterCallbackAndCall(
      SetMemoryPrefChangedCallbackInt,
      "javascript.options.mem.gc_large_heap_size_min_mb",
      (void*)JSGC_LARGE_HEAP_SIZE_MIN);

  Preferences::RegisterCallbackAndCall(
      SetMemoryPrefChangedCallbackInt,
      "javascript.options.mem.gc_allocation_threshold_mb",
      (void*)JSGC_ALLOCATION_THRESHOLD);

  Preferences::RegisterCallbackAndCall(
      SetMemoryPrefChangedCallbackInt,
      "javascript.options.mem.gc_malloc_threshold_base_mb",
      (void*)JSGC_MALLOC_THRESHOLD_BASE);

  Preferences::RegisterCallbackAndCall(
      SetMemoryPrefChangedCallbackInt,
      "javascript.options.mem.gc_small_heap_incremental_limit",
      (void*)JSGC_SMALL_HEAP_INCREMENTAL_LIMIT);
  Preferences::RegisterCallbackAndCall(
      SetMemoryPrefChangedCallbackInt,
      "javascript.options.mem.gc_large_heap_incremental_limit",
      (void*)JSGC_LARGE_HEAP_INCREMENTAL_LIMIT);

  Preferences::RegisterCallbackAndCall(
      SetMemoryPrefChangedCallbackInt,
      "javascript.options.mem.gc_urgent_threshold_mb",
      (void*)JSGC_URGENT_THRESHOLD_MB);

  Preferences::RegisterCallbackAndCall(SetIncrementalCCPrefChangedCallback,
                                       "dom.cycle_collector.incremental");

  Preferences::RegisterCallbackAndCall(
      SetMemoryPrefChangedCallbackInt,
      "javascript.options.mem.gc_min_empty_chunk_count",
      (void*)JSGC_MIN_EMPTY_CHUNK_COUNT);

  Preferences::RegisterCallbackAndCall(
      SetMemoryPrefChangedCallbackInt,
      "javascript.options.mem.gc_helper_thread_ratio",
      (void*)JSGC_HELPER_THREAD_RATIO);

  Preferences::RegisterCallbackAndCall(
      SetMemoryPrefChangedCallbackInt,
      "javascript.options.mem.gc_max_helper_threads",
      (void*)JSGC_MAX_HELPER_THREADS);

  Preferences::RegisterCallbackAndCall(
      SetMemoryPrefChangedCallbackInt,
      "javascript.options.mem.nursery_eager_collection_threshold_kb",
      (void*)JSGC_NURSERY_EAGER_COLLECTION_THRESHOLD_KB);

  Preferences::RegisterCallbackAndCall(
      SetMemoryPrefChangedCallbackInt,
      "javascript.options.mem.nursery_eager_collection_threshold_percent",
      (void*)JSGC_NURSERY_EAGER_COLLECTION_THRESHOLD_PERCENT);

  Preferences::RegisterCallbackAndCall(
      SetMemoryPrefChangedCallbackInt,
      "javascript.options.mem.nursery_eager_collection_timeout_ms",
      (void*)JSGC_NURSERY_EAGER_COLLECTION_TIMEOUT_MS);

  Preferences::RegisterCallbackAndCall(
      SetMemoryPrefChangedCallbackInt,
      "javascript.options.mem.nursery_max_time_goal_ms",
      (void*)JSGC_NURSERY_MAX_TIME_GOAL_MS);

  nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
  if (!obs) {
    MOZ_CRASH();
  }

  nsIObserver* observer = new nsJSEnvironmentObserver();
  obs->AddObserver(observer, "memory-pressure", false);
  obs->AddObserver(observer, "user-interaction-inactive", false);
  obs->AddObserver(observer, "user-interaction-active", false);
  obs->AddObserver(observer, "quit-application", false);
  obs->AddObserver(observer, NS_XPCOM_SHUTDOWN_OBSERVER_ID, false);
  obs->AddObserver(observer, "content-child-will-shutdown", false);

  sIsInitialized = true;
}

void mozilla::dom::ShutdownJSEnvironment() {
  sShuttingDown = true;
  sScheduler->Shutdown();
  sCCStats = nullptr;
}

AsyncErrorReporter::AsyncErrorReporter(xpc::ErrorReport* aReport)
    : Runnable("dom::AsyncErrorReporter"), mReport(aReport) {}

void AsyncErrorReporter::SerializeStack(JSContext* aCx,
                                        JS::Handle<JSObject*> aStack) {
  mStackHolder = std::make_unique<SerializedStackHolder>();
  mStackHolder->SerializeMainThreadOrWorkletStack(aCx, aStack);
}

void AsyncErrorReporter::SetException(JSContext* aCx,
                                      JS::Handle<JS::Value> aException) {
  MOZ_ASSERT(NS_IsMainThread());
  mException.init(aCx, aException);
  mHasException = true;
}

NS_IMETHODIMP AsyncErrorReporter::Run() {
  AutoJSAPI jsapi;
  // We're only using this context to deserialize a stack to report to the
  // console, so the scope we use doesn't matter. Stack frame filtering happens
  // based on the principal encoded into the frame and the caller compartment,
  // not the compartment of the frame object, and the console reporting code
  // will not be using our context, and therefore will not care what compartment
  // it has entered.
  DebugOnly<bool> ok = jsapi.Init(xpc::PrivilegedJunkScope());
  MOZ_ASSERT(ok, "Problem with system global?");
  JSContext* cx = jsapi.cx();
  JS::Rooted<JSObject*> stack(cx);
  JS::Rooted<JSObject*> stackGlobal(cx);
  if (mStackHolder) {
    stack = mStackHolder->ReadStack(cx);
    if (stack) {
      stackGlobal = JS::CurrentGlobalOrNull(cx);
    }
  }

  JS::Rooted<Maybe<JS::Value>> exception(cx, Nothing());
  if (mHasException) {
    MOZ_ASSERT(NS_IsMainThread());
    exception = Some(mException);
    // Remove our reference to the exception.
    mException.setUndefined();
    mHasException = false;
  }

  mReport->LogToConsoleWithStack(nullptr, exception, stack, stackGlobal);
  return NS_OK;
}

// A fast-array class for JS.  This class supports both nsIJSScriptArray and
// nsIArray.  If it is JS itself providing and consuming this class, all work
// can be done via nsIJSScriptArray, and avoid the conversion of elements
// to/from nsISupports.
// When consumed by non-JS (eg, another script language), conversion is done
// on-the-fly.
class nsJSArgArray final : public nsIJSArgArray {
 public:
  nsJSArgArray(JSContext* aContext, uint32_t argc, const JS::Value* argv,
               nsresult* prv);

  // nsISupports
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS_FINAL
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS_AMBIGUOUS(nsJSArgArray,
                                                         nsIJSArgArray)

  // nsIArray
  NS_DECL_NSIARRAY

  // nsIJSArgArray
  nsresult GetArgs(uint32_t* argc, void** argv) override;

  void ReleaseJSObjects();

 protected:
  ~nsJSArgArray();
  JSContext* mContext;
  JS::Heap<JS::Value>* mArgv;
  uint32_t mArgc;
};

nsJSArgArray::nsJSArgArray(JSContext* aContext, uint32_t argc,
                           const JS::Value* argv, nsresult* prv)
    : mContext(aContext), mArgv(nullptr), mArgc(argc) {
  // copy the array - we don't know its lifetime, and ours is tied to xpcom
  // refcounting.
  if (argc) {
    mArgv = new (fallible) JS::Heap<JS::Value>[argc];
    if (!mArgv) {
      *prv = NS_ERROR_OUT_OF_MEMORY;
      return;
    }
  }

  // Callers are allowed to pass in a null argv even for argc > 0. They can
  // then use GetArgs to initialize the values.
  if (argv) {
    for (uint32_t i = 0; i < argc; ++i) mArgv[i] = argv[i];
  }

  if (argc > 0) {
    mozilla::HoldJSObjects(this);
  }

  *prv = NS_OK;
}

nsJSArgArray::~nsJSArgArray() { ReleaseJSObjects(); }

void nsJSArgArray::ReleaseJSObjects() {
  delete[] mArgv;

  if (mArgc > 0) {
    mArgc = 0;
    mozilla::DropJSObjects(this);
  }
}

// QueryInterface implementation for nsJSArgArray
NS_IMPL_CYCLE_COLLECTION_CLASS(nsJSArgArray)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(nsJSArgArray)
  tmp->ReleaseJSObjects();
NS_IMPL_CYCLE_COLLECTION_UNLINK_END
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(nsJSArgArray)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN(nsJSArgArray)
  if (tmp->mArgv) {
    for (uint32_t i = 0; i < tmp->mArgc; ++i) {
      NS_IMPL_CYCLE_COLLECTION_TRACE_JS_MEMBER_CALLBACK(mArgv[i])
    }
  }
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsJSArgArray)
  NS_INTERFACE_MAP_ENTRY(nsIArray)
  NS_INTERFACE_MAP_ENTRY(nsIJSArgArray)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIJSArgArray)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsJSArgArray)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsJSArgArray)

nsresult nsJSArgArray::GetArgs(uint32_t* argc, void** argv) {
  *argv = (void*)mArgv;
  *argc = mArgc;
  return NS_OK;
}

// nsIArray impl
NS_IMETHODIMP nsJSArgArray::GetLength(uint32_t* aLength) {
  *aLength = mArgc;
  return NS_OK;
}

NS_IMETHODIMP nsJSArgArray::QueryElementAt(uint32_t index, const nsIID& uuid,
                                           void** result) {
  *result = nullptr;
  if (index >= mArgc) return NS_ERROR_INVALID_ARG;

  if (uuid.Equals(NS_GET_IID(nsIVariant)) ||
      uuid.Equals(NS_GET_IID(nsISupports))) {
    // Have to copy a Heap into a Rooted to work with it.
    JS::Rooted<JS::Value> val(mContext, mArgv[index]);
    return nsContentUtils::XPConnect()->JSToVariant(mContext, val,
                                                    (nsIVariant**)result);
  }
  NS_WARNING("nsJSArgArray only handles nsIVariant");
  return NS_ERROR_NO_INTERFACE;
}

NS_IMETHODIMP nsJSArgArray::IndexOf(uint32_t startIndex, nsISupports* element,
                                    uint32_t* _retval) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP nsJSArgArray::ScriptedEnumerate(const nsIID& aElemIID,
                                              uint8_t aArgc,
                                              nsISimpleEnumerator** aResult) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP nsJSArgArray::EnumerateImpl(const nsID& aEntryIID,
                                          nsISimpleEnumerator** _retval) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

// The factory function
nsresult NS_CreateJSArgv(JSContext* aContext, uint32_t argc,
                         const JS::Value* argv, nsIJSArgArray** aArray) {
  nsresult rv;
  nsCOMPtr<nsIJSArgArray> ret = new nsJSArgArray(aContext, argc, argv, &rv);
  if (NS_FAILED(rv)) {
    return rv;
  }
  ret.forget(aArray);
  return NS_OK;
}
