/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/Exceptions.h"

#include "WorkerPrivate.h"
#include "XPCWrapper.h"
#include "js/ColumnNumber.h"  // JS::TaggedColumnNumberOneOrigin
#include "js/RootingAPI.h"
#include "js/SavedFrameAPI.h"
#include "js/TypeDecls.h"
#include "jsapi.h"
#include "mozilla/CycleCollectedJSContext.h"
#include "mozilla/HoldDropJSObjects.h"
#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/DOMException.h"
#include "mozilla/dom/ScriptSettings.h"
#include "nsContentUtils.h"
#include "nsJSPrincipals.h"
#include "nsPIDOMWindow.h"
#include "nsServiceManagerUtils.h"
#include "nsThreadUtils.h"
#include "xpcpublic.h"

namespace mozilla::dom {

// Throw the given exception value if it's safe.  If it's not safe, then
// synthesize and throw a new exception value for NS_ERROR_UNEXPECTED.  The
// incoming value must be in the compartment of aCx.  This function guarantees
// that an exception is pending on aCx when it returns.
static void ThrowExceptionValueIfSafe(JSContext* aCx,
                                      JS::Handle<JS::Value> exnVal,
                                      Exception* aOriginalException) {
  MOZ_ASSERT(aOriginalException);

  if (!exnVal.isObject()) {
    JS_SetPendingException(aCx, exnVal);
    return;
  }

  JS::Rooted<JSObject*> exnObj(aCx, &exnVal.toObject());
  MOZ_ASSERT(js::IsObjectInContextCompartment(exnObj, aCx),
             "exnObj needs to be in the right compartment for the "
             "CheckedUnwrapDynamic thing to make sense");

  // aCx's current Realm is where we're throwing, so using it in the
  // CheckedUnwrapDynamic check makes sense.
  if (js::CheckedUnwrapDynamic(exnObj, aCx)) {
    // This is an object we're allowed to work with, so just go ahead and throw
    // it.
    JS_SetPendingException(aCx, exnVal);
    return;
  }

  // We could probably Throw(aCx, NS_ERROR_UNEXPECTED) here, and it would do the
  // right thing due to there not being an existing exception on the runtime at
  // this point, but it's clearer to explicitly do the thing we want done.  This
  // is also why we don't just call ThrowExceptionObject on the Exception we
  // create: it would do the right thing, but that fact is not obvious.
  RefPtr<Exception> syntheticException = CreateException(NS_ERROR_UNEXPECTED);
  JS::Rooted<JS::Value> syntheticVal(aCx);
  if (!GetOrCreateDOMReflector(aCx, syntheticException, &syntheticVal)) {
    return;
  }
  MOZ_ASSERT(
      syntheticVal.isObject() && !js::IsWrapper(&syntheticVal.toObject()),
      "Must have a reflector here, not a wrapper");
  JS_SetPendingException(aCx, syntheticVal);
}

void ThrowExceptionObject(JSContext* aCx, Exception* aException) {
  JS::Rooted<JS::Value> thrown(aCx);

  // If we stored the original thrown JS value in the exception
  // (see XPCConvert::ConstructException) and we are in a web context
  // (i.e., not chrome), rethrow the original value. This only applies to JS
  // implemented components so we only need to check for this on the main
  // thread.
  if (NS_IsMainThread() && !nsContentUtils::IsCallerChrome() &&
      aException->StealJSVal(thrown.address())) {
    // Now check for the case when thrown is a number which matches
    // aException->GetResult().  This would indicate that what actually got
    // thrown was an nsresult value.  In that situation, we should go back
    // through dom::Throw with that nsresult value, because it will make sure to
    // create the right sort of Exception or DOMException, with the right
    // global.
    if (thrown.isNumber()) {
      nsresult exceptionResult = aException->GetResult();
      if (double(exceptionResult) == thrown.toNumber()) {
        Throw(aCx, exceptionResult);
        return;
      }
    }
    if (!JS_WrapValue(aCx, &thrown)) {
      return;
    }
    ThrowExceptionValueIfSafe(aCx, thrown, aException);
    return;
  }

  if (!GetOrCreateDOMReflector(aCx, aException, &thrown)) {
    return;
  }

  ThrowExceptionValueIfSafe(aCx, thrown, aException);
}

bool Throw(JSContext* aCx, nsresult aRv, const nsACString& aMessage) {
  if (aRv == NS_ERROR_UNCATCHABLE_EXCEPTION) {
    // Nuke any existing exception on aCx, to make sure we're uncatchable.
    JS_ClearPendingException(aCx);
    return false;
  }

  if (JS_IsExceptionPending(aCx)) {
    // Don't clobber the existing exception.
    return false;
  }

  CycleCollectedJSContext* context = CycleCollectedJSContext::Get();
  RefPtr<Exception> existingException = context->GetPendingException();
  // Make sure to clear the pending exception now.  Either we're going to reuse
  // it (and we already grabbed it), or we plan to throw something else and this
  // pending exception is no longer relevant.
  context->SetPendingException(nullptr);

  // Ignore the pending exception if we have a non-default message passed in.
  if (aMessage.IsEmpty() && existingException) {
    if (aRv == existingException->GetResult()) {
      // Reuse the existing exception.
      ThrowExceptionObject(aCx, existingException);
      return false;
    }
  }

  RefPtr<Exception> finalException = CreateException(aRv, aMessage);
  MOZ_ASSERT(finalException);

  ThrowExceptionObject(aCx, finalException);
  return false;
}

void ThrowAndReport(nsPIDOMWindowInner* aWindow, nsresult aRv) {
  MOZ_ASSERT(aRv != NS_ERROR_UNCATCHABLE_EXCEPTION,
             "Doesn't make sense to report uncatchable exceptions!");
  AutoJSAPI jsapi;
  if (NS_WARN_IF(!jsapi.Init(aWindow))) {
    return;
  }

  Throw(jsapi.cx(), aRv);
}

already_AddRefed<Exception> CreateException(nsresult aRv,
                                            const nsACString& aMessage) {
  // Do we use DOM exceptions for this error code?
  switch (NS_ERROR_GET_MODULE(aRv)) {
    case NS_ERROR_MODULE_DOM:
    case NS_ERROR_MODULE_SVG:
    case NS_ERROR_MODULE_DOM_FILE:
    case NS_ERROR_MODULE_DOM_XPATH:
    case NS_ERROR_MODULE_DOM_INDEXEDDB:
    case NS_ERROR_MODULE_DOM_FILEHANDLE:
    case NS_ERROR_MODULE_DOM_ANIM:
    case NS_ERROR_MODULE_DOM_PUSH:
    case NS_ERROR_MODULE_DOM_MEDIA:
    case NS_ERROR_MODULE_DOM_SERIAL:
      if (aMessage.IsEmpty()) {
        return DOMException::Create(aRv);
      }
      return DOMException::Create(aRv, aMessage);
    default:
      break;
  }

  // If not, use the default.
  RefPtr<Exception> exception =
      new Exception(aMessage, aRv, ""_ns, nullptr, nullptr);
  return exception.forget();
}

already_AddRefed<nsIStackFrame> GetCurrentJSStack(int32_t aMaxDepth) {
  // is there a current context available?
  JSContext* cx = nsContentUtils::GetCurrentJSContext();

  if (!cx || !js::GetContextRealm(cx)) {
    return nullptr;
  }

  static const unsigned MAX_FRAMES = 100;
  if (aMaxDepth < 0) {
    aMaxDepth = MAX_FRAMES;
  }

  JS::StackCapture captureMode =
      aMaxDepth == 0 ? JS::StackCapture(JS::AllFrames())
                     : JS::StackCapture(JS::MaxFrames(aMaxDepth));

  return dom::exceptions::CreateStack(cx, std::move(captureMode));
}

namespace exceptions {

class JSStackFrame final : public nsIStackFrame, public xpc::JSStackFrameBase {
 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS_FINAL
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS(JSStackFrame)
  NS_DECL_NSISTACKFRAME

  // aStack must not be null.
  explicit JSStackFrame(JS::Handle<JSObject*> aStack);

 private:
  virtual ~JSStackFrame();

  void Clear() override { mStack = nullptr; }

  // Remove this frame from the per-realm list of live frames,
  // and clear out the stack pointer.
  void UnregisterAndClear();

  JS::Heap<JSObject*> mStack;
  nsString mFormattedStack;

  nsCOMPtr<nsIStackFrame> mCaller;
  nsCOMPtr<nsIStackFrame> mAsyncCaller;
  nsCString mFilename;
  nsString mFunname;
  nsString mAsyncCause;
  int32_t mSourceId;
  int32_t mLineno;
  int32_t mColNo;

  bool mFilenameInitialized;
  bool mFunnameInitialized;
  bool mSourceIdInitialized;
  bool mLinenoInitialized;
  bool mColNoInitialized;
  bool mAsyncCauseInitialized;
  bool mAsyncCallerInitialized;
  bool mCallerInitialized;
  bool mFormattedStackInitialized;
};

JSStackFrame::JSStackFrame(JS::Handle<JSObject*> aStack)
    : mStack(aStack),
      mSourceId(0),
      mLineno(0),
      mColNo(0),
      mFilenameInitialized(false),
      mFunnameInitialized(false),
      mSourceIdInitialized(false),
      mLinenoInitialized(false),
      mColNoInitialized(false),
      mAsyncCauseInitialized(false),
      mAsyncCallerInitialized(false),
      mCallerInitialized(false),
      mFormattedStackInitialized(false) {
  MOZ_ASSERT(mStack);
  MOZ_ASSERT(JS::IsUnwrappedSavedFrame(mStack));

  mozilla::HoldJSObjects(this);

  xpc::RegisterJSStackFrame(js::GetNonCCWObjectRealm(aStack), this);
}

JSStackFrame::~JSStackFrame() {
  UnregisterAndClear();
  mozilla::DropJSObjects(this);
}

void JSStackFrame::UnregisterAndClear() {
  if (!mStack) {
    return;
  }

  xpc::UnregisterJSStackFrame(js::GetNonCCWObjectRealm(mStack.unbarrieredGet()),
                              this);
  Clear();
}

NS_IMPL_CYCLE_COLLECTION_CLASS(JSStackFrame)
NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(JSStackFrame)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mCaller)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mAsyncCaller)
  tmp->UnregisterAndClear();
NS_IMPL_CYCLE_COLLECTION_UNLINK_END
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(JSStackFrame)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mCaller)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mAsyncCaller)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END
NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN(JSStackFrame)
  NS_IMPL_CYCLE_COLLECTION_TRACE_JS_MEMBER_CALLBACK(mStack)
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(JSStackFrame)
NS_IMPL_CYCLE_COLLECTING_RELEASE(JSStackFrame)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(JSStackFrame)
  NS_INTERFACE_MAP_ENTRY(nsIStackFrame)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

// Helper method to determine the JSPrincipals* to pass to JS SavedFrame APIs.
//
// @argument aStack the stack we're working with; must be non-null.
// @argument [out] aCanCache whether we can use cached JSStackFrame values.
static JSPrincipals* GetPrincipalsForStackGetter(JSContext* aCx,
                                                 JS::Handle<JSObject*> aStack,
                                                 bool* aCanCache) {
  MOZ_ASSERT(JS::IsUnwrappedSavedFrame(aStack));

  JSPrincipals* currentPrincipals =
      JS::GetRealmPrincipals(js::GetContextRealm(aCx));
  JSPrincipals* stackPrincipals =
      JS::GetRealmPrincipals(js::GetNonCCWObjectRealm(aStack));

  // Fast path for when the principals are equal. This check is also necessary
  // for workers: no nsIPrincipal there so we can't use the code below.
  if (currentPrincipals == stackPrincipals) {
    *aCanCache = true;
    return stackPrincipals;
  }

  MOZ_ASSERT(NS_IsMainThread());

  if (nsJSPrincipals::get(currentPrincipals)
          ->Subsumes(nsJSPrincipals::get(stackPrincipals))) {
    // The current principals subsume the stack's principals. In this case use
    // the stack's principals: the idea is that this way devtools code that's
    // asking an exception object for a stack to display will end up with the
    // stack the web developer would see via doing .stack in a web page, with
    // Firefox implementation details excluded.

    // Because we use the stack's principals and don't rely on the current
    // context realm, we can use cached values.
    *aCanCache = true;
    return stackPrincipals;
  }

  // The stack was captured in more-privileged code, so use the less privileged
  // principals. Don't use cached values because we don't want these values to
  // depend on the current realm/principals.
  *aCanCache = false;
  return currentPrincipals;
}

// Helper method to get the value of a stack property, if it's not already
// cached.  This will make sure we skip the cache if the property value depends
// on the (current) context's realm/principals.
//
// @argument aStack the stack we're working with; must be non-null.
// @argument aPropGetter the getter function to call.
// @argument aIsCached whether we've cached this property's value before.
//
// @argument [out] aCanCache whether the value can get cached.
// @argument [out] aUseCachedValue if true, just use the cached value.
// @argument [out] aValue the value we got from the stack.
template <typename ReturnType, typename GetterOutParamType>
static void GetValueIfNotCached(
    JSContext* aCx, const JS::Heap<JSObject*>& aStack,
    JS::SavedFrameResult (*aPropGetter)(JSContext*, JSPrincipals*,
                                        JS::Handle<JSObject*>,
                                        GetterOutParamType,
                                        JS::SavedFrameSelfHosted),
    bool aIsCached, bool* aCanCache, bool* aUseCachedValue, ReturnType aValue) {
  MOZ_ASSERT(aStack);
  MOZ_ASSERT(JS::IsUnwrappedSavedFrame(aStack));

  JS::Rooted<JSObject*> stack(aCx, aStack);

  JSPrincipals* principals = GetPrincipalsForStackGetter(aCx, stack, aCanCache);
  if (*aCanCache && aIsCached) {
    *aUseCachedValue = true;
    return;
  }

  *aUseCachedValue = false;

  aPropGetter(aCx, principals, stack, aValue,
              JS::SavedFrameSelfHosted::Exclude);
}

NS_IMETHODIMP JSStackFrame::GetFilenameXPCOM(JSContext* aCx,
                                             nsACString& aFilename) {
  GetFilename(aCx, aFilename);
  return NS_OK;
}

void JSStackFrame::GetFilename(JSContext* aCx, nsACString& aFilename) {
  if (!mStack) {
    aFilename.Truncate();
    return;
  }

  JS::Rooted<JSString*> filename(aCx);
  bool canCache = false, useCachedValue = false;
  GetValueIfNotCached(aCx, mStack, JS::GetSavedFrameSource,
                      mFilenameInitialized, &canCache, &useCachedValue,
                      &filename);
  if (useCachedValue) {
    aFilename = mFilename;
    return;
  }

  nsAutoJSCString str;
  if (!str.init(aCx, filename)) {
    JS_ClearPendingException(aCx);
    aFilename.Truncate();
    return;
  }
  aFilename = str;

  if (canCache) {
    mFilename = str;
    mFilenameInitialized = true;
  }
}

NS_IMETHODIMP
JSStackFrame::GetNameXPCOM(JSContext* aCx, nsAString& aFunction) {
  GetName(aCx, aFunction);
  return NS_OK;
}

void JSStackFrame::GetName(JSContext* aCx, nsAString& aFunction) {
  if (!mStack) {
    aFunction.Truncate();
    return;
  }

  JS::Rooted<JSString*> name(aCx);
  bool canCache = false, useCachedValue = false;
  GetValueIfNotCached(aCx, mStack, JS::GetSavedFrameFunctionDisplayName,
                      mFunnameInitialized, &canCache, &useCachedValue, &name);

  if (useCachedValue) {
    aFunction = mFunname;
    return;
  }

  if (name) {
    nsAutoJSString str;
    if (!str.init(aCx, name)) {
      JS_ClearPendingException(aCx);
      aFunction.Truncate();
      return;
    }
    aFunction = str;
  } else {
    aFunction.SetIsVoid(true);
  }

  if (canCache) {
    mFunname = aFunction;
    mFunnameInitialized = true;
  }
}

int32_t JSStackFrame::GetSourceId(JSContext* aCx) {
  if (!mStack) {
    return 0;
  }

  uint32_t id;
  bool canCache = false, useCachedValue = false;
  GetValueIfNotCached(aCx, mStack, JS::GetSavedFrameSourceId,
                      mSourceIdInitialized, &canCache, &useCachedValue, &id);

  if (useCachedValue) {
    return mSourceId;
  }

  if (canCache) {
    mSourceId = id;
    mSourceIdInitialized = true;
  }

  return id;
}

NS_IMETHODIMP
JSStackFrame::GetSourceIdXPCOM(JSContext* aCx, int32_t* aSourceId) {
  *aSourceId = GetSourceId(aCx);
  return NS_OK;
}

int32_t JSStackFrame::GetLineNumber(JSContext* aCx) {
  if (!mStack) {
    return 0;
  }

  uint32_t line;
  bool canCache = false, useCachedValue = false;
  GetValueIfNotCached(aCx, mStack, JS::GetSavedFrameLine, mLinenoInitialized,
                      &canCache, &useCachedValue, &line);

  if (useCachedValue) {
    return mLineno;
  }

  if (canCache) {
    mLineno = line;
    mLinenoInitialized = true;
  }

  return line;
}

NS_IMETHODIMP
JSStackFrame::GetLineNumberXPCOM(JSContext* aCx, int32_t* aLineNumber) {
  *aLineNumber = GetLineNumber(aCx);
  return NS_OK;
}

int32_t JSStackFrame::GetColumnNumber(JSContext* aCx) {
  if (!mStack) {
    return 0;
  }

  JS::TaggedColumnNumberOneOrigin col;
  bool canCache = false, useCachedValue = false;
  GetValueIfNotCached(aCx, mStack, JS::GetSavedFrameColumn, mColNoInitialized,
                      &canCache, &useCachedValue, &col);

  if (useCachedValue) {
    return mColNo;
  }

  if (canCache) {
    mColNo = col.oneOriginValue();
    mColNoInitialized = true;
  }

  return col.oneOriginValue();
}

NS_IMETHODIMP
JSStackFrame::GetColumnNumberXPCOM(JSContext* aCx, int32_t* aColumnNumber) {
  *aColumnNumber = GetColumnNumber(aCx);
  return NS_OK;
}

NS_IMETHODIMP
JSStackFrame::GetAsyncCauseXPCOM(JSContext* aCx, nsAString& aAsyncCause) {
  GetAsyncCause(aCx, aAsyncCause);
  return NS_OK;
}

void JSStackFrame::GetAsyncCause(JSContext* aCx, nsAString& aAsyncCause) {
  if (!mStack) {
    aAsyncCause.Truncate();
    return;
  }

  JS::Rooted<JSString*> asyncCause(aCx);
  bool canCache = false, useCachedValue = false;
  GetValueIfNotCached(aCx, mStack, JS::GetSavedFrameAsyncCause,
                      mAsyncCauseInitialized, &canCache, &useCachedValue,
                      &asyncCause);

  if (useCachedValue) {
    aAsyncCause = mAsyncCause;
    return;
  }

  if (asyncCause) {
    nsAutoJSString str;
    if (!str.init(aCx, asyncCause)) {
      JS_ClearPendingException(aCx);
      aAsyncCause.Truncate();
      return;
    }
    aAsyncCause = str;
  } else {
    aAsyncCause.SetIsVoid(true);
  }

  if (canCache) {
    mAsyncCause = aAsyncCause;
    mAsyncCauseInitialized = true;
  }
}

NS_IMETHODIMP
JSStackFrame::GetAsyncCallerXPCOM(JSContext* aCx,
                                  nsIStackFrame** aAsyncCaller) {
  *aAsyncCaller = GetAsyncCaller(aCx).take();
  return NS_OK;
}

already_AddRefed<nsIStackFrame> JSStackFrame::GetAsyncCaller(JSContext* aCx) {
  if (!mStack) {
    return nullptr;
  }

  JS::Rooted<JSObject*> asyncCallerObj(aCx);
  bool canCache = false, useCachedValue = false;
  GetValueIfNotCached(aCx, mStack, JS::GetSavedFrameAsyncParent,
                      mAsyncCallerInitialized, &canCache, &useCachedValue,
                      &asyncCallerObj);

  if (useCachedValue) {
    nsCOMPtr<nsIStackFrame> asyncCaller = mAsyncCaller;
    return asyncCaller.forget();
  }

  nsCOMPtr<nsIStackFrame> asyncCaller =
      asyncCallerObj ? new JSStackFrame(asyncCallerObj) : nullptr;

  if (canCache) {
    mAsyncCaller = asyncCaller;
    mAsyncCallerInitialized = true;
  }

  return asyncCaller.forget();
}

NS_IMETHODIMP
JSStackFrame::GetCallerXPCOM(JSContext* aCx, nsIStackFrame** aCaller) {
  *aCaller = GetCaller(aCx).take();
  return NS_OK;
}

already_AddRefed<nsIStackFrame> JSStackFrame::GetCaller(JSContext* aCx) {
  if (!mStack) {
    return nullptr;
  }

  JS::Rooted<JSObject*> callerObj(aCx);
  bool canCache = false, useCachedValue = false;
  GetValueIfNotCached(aCx, mStack, JS::GetSavedFrameParent, mCallerInitialized,
                      &canCache, &useCachedValue, &callerObj);

  if (useCachedValue) {
    nsCOMPtr<nsIStackFrame> caller = mCaller;
    return caller.forget();
  }

  nsCOMPtr<nsIStackFrame> caller =
      callerObj ? new JSStackFrame(callerObj) : nullptr;

  if (canCache) {
    mCaller = caller;
    mCallerInitialized = true;
  }

  return caller.forget();
}

NS_IMETHODIMP
JSStackFrame::GetFormattedStackXPCOM(JSContext* aCx, nsAString& aStack) {
  GetFormattedStack(aCx, aStack);
  return NS_OK;
}

void JSStackFrame::GetFormattedStack(JSContext* aCx, nsAString& aStack) {
  if (!mStack) {
    aStack.Truncate();
    return;
  }

  // Sadly we can't use GetValueIfNotCached here, because our getter
  // returns bool, not JS::SavedFrameResult.  Maybe it's possible to
  // make the templates more complicated to deal, but in the meantime
  // let's just inline GetValueIfNotCached here.

  JS::Rooted<JSObject*> stack(aCx, mStack);

  bool canCache;
  JSPrincipals* principals = GetPrincipalsForStackGetter(aCx, stack, &canCache);
  if (canCache && mFormattedStackInitialized) {
    aStack = mFormattedStack;
    return;
  }

  JS::Rooted<JSString*> formattedStack(aCx);
  if (!JS::BuildStackString(aCx, principals, stack, &formattedStack)) {
    JS_ClearPendingException(aCx);
    aStack.Truncate();
    return;
  }

  nsAutoJSString str;
  if (!str.init(aCx, formattedStack)) {
    JS_ClearPendingException(aCx);
    aStack.Truncate();
    return;
  }

  aStack = str;

  if (canCache) {
    mFormattedStack = str;
    mFormattedStackInitialized = true;
  }
}

NS_IMETHODIMP JSStackFrame::GetNativeSavedFrame(
    JS::MutableHandle<JS::Value> aSavedFrame) {
  aSavedFrame.setObjectOrNull(mStack);
  return NS_OK;
}

NS_IMETHODIMP
JSStackFrame::ToStringXPCOM(JSContext* aCx, nsACString& _retval) {
  ToString(aCx, _retval);
  return NS_OK;
}

void JSStackFrame::ToString(JSContext* aCx, nsACString& _retval) {
  _retval.Truncate();

  nsCString filename;
  GetFilename(aCx, filename);

  if (filename.IsEmpty()) {
    filename.AssignLiteral("<unknown filename>");
  }

  nsString funname;
  GetName(aCx, funname);

  if (funname.IsEmpty()) {
    funname.AssignLiteral("<TOP_LEVEL>");
  }

  int32_t lineno = GetLineNumber(aCx);

  static const char format[] = "JS frame :: %s :: %s :: line %d";
  _retval.AppendPrintf(format, filename.get(),
                       NS_ConvertUTF16toUTF8(funname).get(), lineno);
}

already_AddRefed<nsIStackFrame> CreateStack(JSContext* aCx,
                                            JS::StackCapture&& aCaptureMode) {
  JS::Rooted<JSObject*> stack(aCx);
  if (!JS::CaptureCurrentStack(aCx, &stack, std::move(aCaptureMode))) {
    return nullptr;
  }

  return CreateStack(aCx, stack);
}

already_AddRefed<nsIStackFrame> CreateStack(JSContext* aCx,
                                            JS::Handle<JSObject*> aStack) {
  if (aStack) {
    return MakeAndAddRef<JSStackFrame>(aStack);
  }
  return nullptr;
}

}  // namespace exceptions
}  // namespace mozilla::dom
