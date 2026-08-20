/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Sharable code and data for wrapper around JSObjects. */

#include "mozilla/Attributes.h"
#include "mozilla/Components.h"
#include "mozilla/dom/AutoEntryScript.h"
#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/ChromeUtilsBinding.h"
#include "mozilla/dom/DOMException.h"
#include "mozilla/dom/DOMExceptionBinding.h"
#include "mozilla/dom/MozQueryInterface.h"

#include "AccessCheck.h"
#include "jsapi.h"
#include "jsfriendapi.h"
#include "nsArrayEnumerator.h"
#include "nsINamed.h"
#include "nsIScriptError.h"
#include "nsJSUtils.h"
#include "nsPrintfCString.h"
#include "nsWrapperCache.h"
#include "xpcprivate.h"

#include "js/CallAndConstruct.h"  // JS_CallFunctionValue
#include "js/Object.h"            // JS::GetClass
#include "js/Printf.h"
#include "js/PropertyAndElement.h"  // JS_Enumerate, JS_GetProperty, JS_GetPropertyById, JS_HasProperty, JS_HasPropertyById, JS_SetProperty, JS_SetPropertyById

using namespace xpc;
using namespace JS;
using namespace mozilla;
using namespace mozilla::dom;

bool AutoScriptEvaluate::StartEvaluating(HandleObject scope) {
  MOZ_ASSERT(!mEvaluated,
             "AutoScriptEvaluate::Evaluate should only be called once");

  if (!mJSContext) {
    return true;
  }

  mEvaluated = true;

  mAutoRealm.emplace(mJSContext, scope);

  // Saving the exception state keeps us from interfering with another script
  // that may also be running on this context.  This occurred first with the
  // js debugger, as described in
  // http://bugzilla.mozilla.org/show_bug.cgi?id=88130 but presumably could
  // show up in any situation where a script calls into a wrapped js component
  // on the same context, while the context has a nonzero exception state.
  mState.emplace(mJSContext);

  return true;
}

AutoScriptEvaluate::~AutoScriptEvaluate() {
  if (!mJSContext || !mEvaluated) {
    return;
  }
  mState->restore();
}

// It turns out that some errors may be not worth reporting. So, this
// function is factored out to manage that.
bool xpc_IsReportableErrorCode(nsresult code) {
  if (NS_SUCCEEDED(code)) {
    return false;
  }

  switch (code) {
    // Error codes that we don't want to report as errors...
    // These generally indicate bad interface design AFAIC.
    case NS_ERROR_FACTORY_REGISTER_AGAIN:
    case NS_BASE_STREAM_WOULD_BLOCK:
      return false;
    default:
      return true;
  }
}

// A little stack-based RAII class to help management of the XPCJSContext
// PendingResult.
class MOZ_STACK_CLASS AutoSavePendingResult {
 public:
  explicit AutoSavePendingResult(XPCJSContext* xpccx) : mXPCContext(xpccx) {
    // Save any existing pending result and reset to NS_OK for this invocation.
    mSavedResult = xpccx->GetPendingResult();
    xpccx->SetPendingResult(NS_OK);
  }
  ~AutoSavePendingResult() { mXPCContext->SetPendingResult(mSavedResult); }

 private:
  XPCJSContext* mXPCContext;
  nsresult mSavedResult;
};

// static
const nsXPTInterfaceInfo* nsXPCWrappedJS::GetInterfaceInfo(REFNSIID aIID) {
  const nsXPTInterfaceInfo* info = nsXPTInterfaceInfo::ByIID(aIID);
  if (!info || info->IsBuiltinClass()) {
    return nullptr;
  }

  return info;
}

// static
JSObject* nsXPCWrappedJS::CallQueryInterfaceOnJSObject(JSContext* cx,
                                                       JSObject* jsobjArg,
                                                       HandleObject scope,
                                                       REFNSIID aIID) {
  js::AssertSameCompartment(scope, jsobjArg);

  RootedObject jsobj(cx, jsobjArg);
  RootedValue arg(cx);
  RootedValue retval(cx);
  RootedObject retObj(cx);
  RootedValue fun(cx);

  // In bug 503926, we added a security check to make sure that we don't
  // invoke content QI functions. In the modern world, this is probably
  // unnecessary, because invoking QI involves passing an IID object to
  // content, which will fail. But we do a belt-and-suspenders check to
  // make sure that content can never trigger the rat's nest of code below.
  // Once we completely turn off XPConnect for the web, this can definitely
  // go away.
  if (!AccessCheck::isChrome(jsobj) ||
      !AccessCheck::isChrome(js::UncheckedUnwrap(jsobj))) {
    return nullptr;
  }

  // OK, it looks like we'll be calling into JS code.
  AutoScriptEvaluate scriptEval(cx);

  // XXX we should install an error reporter that will send reports to
  // the JS error console service.
  if (!scriptEval.StartEvaluating(scope)) {
    return nullptr;
  }

  // check upfront for the existence of the function property
  HandleId funid =
      XPCJSRuntime::Get()->GetStringID(XPCJSContext::IDX_QUERY_INTERFACE);
  if (!JS_GetPropertyById(cx, jsobj, funid, &fun) || fun.isPrimitive()) {
    return nullptr;
  }

  dom::MozQueryInterface* mozQI = nullptr;
  if (NS_SUCCEEDED(UNWRAP_OBJECT(MozQueryInterface, &fun, mozQI))) {
    if (mozQI->QueriesTo(aIID)) {
      return jsobj.get();
    }
    return nullptr;
  }

  if (!xpc::ID2JSValue(cx, aIID, &arg)) {
    return nullptr;
  }

  // Throwing NS_NOINTERFACE is the prescribed way to fail QI from JS. It is
  // not an exception that is ever worth reporting, but we don't want to eat
  // all exceptions either.

  bool success =
      JS_CallFunctionValue(cx, jsobj, fun, HandleValueArray(arg), &retval);
  if (!success && JS_IsExceptionPending(cx)) {
    RootedValue jsexception(cx, NullValue());

    if (JS_GetPendingException(cx, &jsexception)) {
      if (jsexception.isObject()) {
        // XPConnect may have constructed an object to represent a
        // C++ QI failure. See if that is the case.
        JS::Rooted<JSObject*> exceptionObj(cx, &jsexception.toObject());
        Exception* e = nullptr;
        UNWRAP_OBJECT(Exception, &exceptionObj, e);

        if (e && e->GetResult() == NS_NOINTERFACE) {
          JS_ClearPendingException(cx);
        }
      } else if (jsexception.isNumber()) {
        nsresult rv;
        // JS often throws an nsresult.
        if (jsexception.isDouble())
          // Visual Studio 9 doesn't allow casting directly from
          // a double to an enumeration type, contrary to
          // 5.2.9(10) of C++11, so add an intermediate cast.
          rv = (nsresult)(uint32_t)(jsexception.toDouble());
        else
          rv = (nsresult)(jsexception.toInt32());

        if (rv == NS_NOINTERFACE) JS_ClearPendingException(cx);
      }
    }
  } else if (!success) {
    NS_WARNING("QI hook ran OOMed - this is probably a bug!");
  }

  if (success) success = JS_ValueToObject(cx, retval, &retObj);

  return success ? retObj.get() : nullptr;
}

/***************************************************************************/

namespace {

class WrappedJSNamed final : public nsINamed {
  nsCString mName;

  ~WrappedJSNamed() = default;

 public:
  NS_DECL_ISUPPORTS

  explicit WrappedJSNamed(const nsACString& aName) : mName(aName) {}

  NS_IMETHOD GetName(nsACString& aName) override {
    aName = mName;
    aName.AppendLiteral(":JS");
    return NS_OK;
  }
};

NS_IMPL_ISUPPORTS(WrappedJSNamed, nsINamed)

}  // anonymous namespace

/***************************************************************************/

// static
nsresult nsXPCWrappedJS::DelegatedQueryInterface(REFNSIID aIID,
                                                 void** aInstancePtr) {
  if (aIID.Equals(NS_GET_IID(nsIXPConnectJSObjectHolder))) {
    // This needs to call NS_ADDREF directly instead of using nsCOMPtr<>,
    // because the latter does a QI in an assert, and we're already in a QI, so
    // it would cause infinite recursion.
    NS_ADDREF(this);
    *aInstancePtr = (void*)static_cast<nsIXPConnectJSObjectHolder*>(this);
    return NS_OK;
  }

  // Ensure that we are asking for a non-builtinclass interface, and avoid even
  // setting up our AutoEntryScript if we are.  Don't bother doing that check
  // if our IID is nsISupports: we know that's not builtinclass, and we QI to
  // it a _lot_.
  if (!aIID.Equals(NS_GET_IID(nsISupports))) {
    const nsXPTInterfaceInfo* info = nsXPTInterfaceInfo::ByIID(aIID);
    if (!info || info->IsBuiltinClass()) {
      MOZ_ASSERT(!aIID.Equals(NS_GET_IID(nsISupportsWeakReference)),
                 "Later code for nsISupportsWeakReference is being skipped");
      MOZ_ASSERT(!aIID.Equals(NS_GET_IID(nsISimpleEnumerator)),
                 "Later code for nsISimpleEnumerator is being skipped");
      MOZ_ASSERT(!aIID.Equals(NS_GET_IID(nsINamed)),
                 "Later code for nsINamed is being skipped");
      *aInstancePtr = nullptr;
      return NS_NOINTERFACE;
    }
  }

  MOZ_ASSERT(!aIID.Equals(NS_GET_IID(nsWrapperCache)),
             "Where did we get non-builtinclass interface info for this??");

  // QI on an XPCWrappedJS can run script, so we need an AutoEntryScript.
  // This is inherently Gecko-specific.
  // We check both nativeGlobal and nativeGlobal->GetGlobalJSObject() even
  // though we have derived nativeGlobal from the JS global, because we know
  // there are cases where this can happen. See bug 1094953.
  RootedObject obj(RootingCx(), GetJSObject());
  nsIGlobalObject* nativeGlobal = NativeGlobal(js::UncheckedUnwrap(obj));
  NS_ENSURE_TRUE(nativeGlobal, NS_ERROR_FAILURE);
  NS_ENSURE_TRUE(nativeGlobal->HasJSGlobal(), NS_ERROR_FAILURE);

  AutoAllowLegacyScriptExecution exemption;

  AutoEntryScript aes(nativeGlobal, "XPCWrappedJS QueryInterface",
                      /* aIsMainThread = */ true);
  XPCCallContext ccx(aes.cx());
  if (!ccx.IsValid()) {
    *aInstancePtr = nullptr;
    return NS_NOINTERFACE;
  }

  // We now need to enter the realm of the actual JSObject* we are pointing at.
  // But that may be a cross-compartment wrapper and therefore not have a
  // well-defined realm, so enter the realm of the global that we grabbed back
  // when we started pointing to our JSObject*.
  RootedObject objScope(RootingCx(), GetJSObjectGlobal());
  JSAutoRealm ar(aes.cx(), objScope);

  // We support nsISupportsWeakReference iff the root wrapped JSObject
  // claims to support it in its QueryInterface implementation.
  if (aIID.Equals(NS_GET_IID(nsISupportsWeakReference))) {
    // We only want to expose one implementation from our aggregate.
    nsXPCWrappedJS* root = GetRootWrapper();
    RootedObject rootScope(ccx, root->GetJSObjectGlobal());

    // Fail if JSObject doesn't claim support for nsISupportsWeakReference
    if (!root->IsValid() || !CallQueryInterfaceOnJSObject(
                                ccx, root->GetJSObject(), rootScope, aIID)) {
      *aInstancePtr = nullptr;
      return NS_NOINTERFACE;
    }

    NS_ADDREF(root);
    *aInstancePtr = (void*)static_cast<nsISupportsWeakReference*>(root);
    return NS_OK;
  }

  // If we're asked to QI to nsISimpleEnumerator and the wrapped object does not
  // have a QueryInterface method, assume it is a JS iterator, and wrap it into
  // an equivalent nsISimpleEnumerator.
  if (aIID.Equals(NS_GET_IID(nsISimpleEnumerator))) {
    bool found;
    XPCJSContext* xpccx = ccx.GetContext();
    if (JS_HasPropertyById(aes.cx(), obj,
                           xpccx->GetStringID(xpccx->IDX_QUERY_INTERFACE),
                           &found) &&
        !found) {
      nsresult rv;
      nsCOMPtr<nsIJSEnumerator> jsEnum;
      if (!XPCConvert::JSObject2NativeInterface(
              aes.cx(), getter_AddRefs(jsEnum), obj,
              &NS_GET_IID(nsIJSEnumerator), nullptr, &rv)) {
        return rv;
      }
      nsCOMPtr<nsISimpleEnumerator> res = new XPCWrappedJSIterator(jsEnum);
      res.forget(aInstancePtr);
      return NS_OK;
    }
  }

  // Checks for any existing wrapper explicitly constructed for this iid.
  // This includes the current wrapper. This also deals with the
  // nsISupports case (for which it returns mRoot).
  // Also check if asking for an interface from which one of our wrappers
  // inherits.
  if (nsXPCWrappedJS* sibling = FindOrFindInherited(aIID)) {
    NS_ADDREF(sibling);
    *aInstancePtr = sibling->GetXPTCStub();
    return NS_OK;
  }

  // Check if the desired interface is a function interface. If so, we don't
  // want to QI, because the function almost certainly doesn't have a
  // QueryInterface property, and doesn't need one.
  const nsXPTInterfaceInfo* info = nsXPTInterfaceInfo::ByIID(aIID);
  if (info && info->IsFunction()) {
    RefPtr<nsXPCWrappedJS> wrapper;
    nsresult rv =
        nsXPCWrappedJS::GetNewOrUsed(ccx, obj, aIID, getter_AddRefs(wrapper));

    // Do the same thing we do for the "check for any existing wrapper" case
    // above.
    if (NS_SUCCEEDED(rv) && wrapper) {
      *aInstancePtr = wrapper.forget().take()->GetXPTCStub();
    }
    return rv;
  }

  // else we do the more expensive stuff...

  // check if the JSObject claims to implement this interface
  RootedObject jsobj(ccx,
                     CallQueryInterfaceOnJSObject(ccx, obj, objScope, aIID));
  if (jsobj) {
    // We can't use XPConvert::JSObject2NativeInterface() here
    // since that can find a XPCWrappedNative directly on the
    // proto chain, and we don't want that here. We need to find
    // the actual JS object that claimed it supports the interface
    // we're looking for or we'll potentially bypass security
    // checks etc by calling directly through to a native found on
    // the prototype chain.
    //
    // Instead, simply do the nsXPCWrappedJS part of
    // XPConvert::JSObject2NativeInterface() here to make sure we
    // get a new (or used) nsXPCWrappedJS.
    RefPtr<nsXPCWrappedJS> wrapper;
    nsresult rv =
        nsXPCWrappedJS::GetNewOrUsed(ccx, jsobj, aIID, getter_AddRefs(wrapper));
    if (NS_SUCCEEDED(rv) && wrapper) {
      // We need to go through the QueryInterface logic to make
      // this return the right thing for the various 'special'
      // interfaces; e.g.  nsISimpleEnumerator.
      rv = wrapper->QueryInterface(aIID, aInstancePtr);
      return rv;
    }
  }

  // If we're asked to QI to nsINamed, we pretend that this is possible. We'll
  // try to return a name that makes sense for the wrapped JS value.
  if (aIID.Equals(NS_GET_IID(nsINamed))) {
    nsCString name = GetFunctionName(ccx, obj);
    RefPtr<WrappedJSNamed> named = new WrappedJSNamed(name);
    *aInstancePtr = named.forget().take();
    return NS_OK;
  }

  // else...
  // no can do
  *aInstancePtr = nullptr;
  return NS_NOINTERFACE;
}

// static
JSObject* nsXPCWrappedJS::GetRootJSObject(JSContext* cx, JSObject* aJSObjArg) {
  RootedObject aJSObj(cx, aJSObjArg);
  RootedObject global(cx, JS::CurrentGlobalOrNull(cx));
  JSObject* result =
      CallQueryInterfaceOnJSObject(cx, aJSObj, global, NS_GET_IID(nsISupports));
  if (!result) {
    result = aJSObj;
  }
  return js::UncheckedUnwrap(result);
}

// static
bool nsXPCWrappedJS::GetArraySizeFromParam(const nsXPTMethodInfo* method,
                                           const nsXPTType& type,
                                           nsXPTCMiniVariant* nativeParams,
                                           uint32_t* result) {
  if (type.Tag() != nsXPTType::T_LEGACY_ARRAY &&
      type.Tag() != nsXPTType::T_PSTRING_SIZE_IS &&
      type.Tag() != nsXPTType::T_PWSTRING_SIZE_IS) {
    *result = 0;
    return true;
  }

  uint8_t argnum = type.ArgNum();
  const nsXPTParamInfo& param = method->Param(argnum);

  // This should be enforced by the xpidl compiler, but it's not.
  // See bug 695235.
  if (param.Type().Tag() != nsXPTType::T_U32) {
    return false;
  }

  // If the length is passed indirectly (as an outparam), dereference by an
  // extra level.
  if (param.IsIndirect()) {
    *result = *(uint32_t*)nativeParams[argnum].val.p;
  } else {
    *result = nativeParams[argnum].val.u32;
  }
  return true;
}

// static
bool nsXPCWrappedJS::GetInterfaceTypeFromParam(const nsXPTMethodInfo* method,
                                               const nsXPTType& type,
                                               nsXPTCMiniVariant* nativeParams,
                                               nsID* result) {
  result->Clear();

  const nsXPTType& inner = type.InnermostType();
  if (inner.Tag() == nsXPTType::T_INTERFACE) {
    // Directly get IID from nsXPTInterfaceInfo.
    if (!inner.GetInterface()) {
      return false;
    }

    *result = inner.GetInterface()->IID();
  } else if (inner.Tag() == nsXPTType::T_INTERFACE_IS) {
    // Get IID from a passed parameter.
    const nsXPTParamInfo& param = method->Param(inner.ArgNum());
    if (param.Type().Tag() != nsXPTType::T_NSID &&
        param.Type().Tag() != nsXPTType::T_NSIDPTR) {
      return false;
    }

    void* ptr = nativeParams[inner.ArgNum()].val.p;

    // If our IID is passed as a pointer outparameter, an extra level of
    // dereferencing is required.
    if (ptr && param.Type().Tag() == nsXPTType::T_NSIDPTR &&
        param.IsIndirect()) {
      ptr = *(nsID**)ptr;
    }

    if (!ptr) {
      return false;
    }

    *result = *(nsID*)ptr;
  }
  return true;
}

// static
void nsXPCWrappedJS::CleanupOutparams(const nsXPTMethodInfo* info,
                                      nsXPTCMiniVariant* nativeParams,
                                      bool inOutOnly, uint8_t count) {
  // clean up any 'out' params handed in
  for (uint8_t i = 0; i < count; i++) {
    const nsXPTParamInfo& param = info->Param(i);
    if (!param.IsOut()) {
      continue;
    }

    MOZ_ASSERT(param.IsIndirect(), "Outparams are always indirect");

    // Don't try to clear optional out params that are not set.
    if (param.IsOptional() && !nativeParams[i].val.p) {
      continue;
    }

    // Call 'CleanupValue' on parameters which we know to be initialized:
    //  1. Complex parameters (initialized by caller)
    //  2. 'inout' parameters (initialized by caller)
    //  3. 'out' parameters when 'inOutOnly' is 'false' (initialized by us)
    //
    // We skip non-complex 'out' parameters before the call, as they may
    // contain random junk.
    if (param.Type().IsComplex() || param.IsIn() || !inOutOnly) {
      uint32_t arrayLen = 0;
      if (!GetArraySizeFromParam(info, param.Type(), nativeParams, &arrayLen)) {
        continue;
      }

      xpc::CleanupValue(param.Type(), nativeParams[i].val.p, arrayLen);
    }

    // Ensure our parameters are in a clean state. Complex values are always
    // handled by CleanupValue, and others have a valid null representation.
    if (!param.Type().IsComplex()) {
      param.Type().ZeroValue(nativeParams[i].val.p);
    }
  }
}

nsresult nsXPCWrappedJS::CheckForException(XPCCallContext& ccx,
                                           AutoEntryScript& aes,
                                           HandleObject aObj,
                                           const char* aPropertyName,
                                           const char* anInterfaceName,
                                           Exception* aSyntheticException) {
  JSContext* cx = ccx.GetJSContext();
  MOZ_ASSERT(cx == aes.cx());
  RefPtr<Exception> xpc_exception = aSyntheticException;
  /* this one would be set by our error reporter */

  XPCJSContext* xpccx = ccx.GetContext();

  // Get this right away in case we do something below to cause JS code
  // to run.
  nsresult pending_result = xpccx->GetPendingResult();

  RootedValue js_exception(cx);
  bool is_js_exception = JS_GetPendingException(cx, &js_exception);

  /* JS might throw an exception whether the reporter was called or not */
  if (is_js_exception) {
    if (!xpc_exception) {
      XPCConvert::JSValToXPCException(cx, &js_exception, anInterfaceName,
                                      aPropertyName,
                                      getter_AddRefs(xpc_exception));
    }

    /* cleanup and set failed even if we can't build an exception */
    if (!xpc_exception) {
      xpccx->SetPendingException(nullptr);  // XXX necessary?
    }
  }

  // Clear the pending exception now, because xpc_exception might be JS-
  // implemented, so invoking methods on it might re-enter JS, which we can't
  // do with an exception on the stack.
  aes.ClearException();

  if (xpc_exception) {
    nsresult e_result = xpc_exception->GetResult();
    // Figure out whether or not we should report this exception.
    bool reportable = xpc_IsReportableErrorCode(e_result);
    if (reportable) {
      // Ugly special case for GetInterface. It's "special" in the
      // same way as QueryInterface in that a failure is not
      // exceptional and shouldn't be reported. We have to do this
      // check here instead of in xpcwrappedjs (like we do for QI) to
      // avoid adding extra code to all xpcwrappedjs objects.
      if (e_result == NS_ERROR_NO_INTERFACE &&
          !strcmp(anInterfaceName, "nsIInterfaceRequestor") &&
          !strcmp(aPropertyName, "getInterface")) {
        reportable = false;
      }

      // More special case, see bug 877760.
      if (e_result == NS_ERROR_XPC_JSOBJECT_HAS_NO_FUNCTION_NAMED) {
        reportable = false;
      }
    }

    // Try to use the error reporter set on the context to handle this
    // error if it came from a JS exception.
    if (reportable && is_js_exception) {
      // Note that we cleared the exception above, so we need to set it again,
      // just so that we can tell the JS engine to pass it back to us via the
      // error reporting callback. This is all very dumb.
      JS_SetPendingException(cx, js_exception);

      // Enter the unwrapped object's realm. This is the realm that was used to
      // enter the AutoEntryScript.
      JSAutoRealm ar(cx, js::UncheckedUnwrap(aObj));
      aes.ReportException();
      reportable = false;
    }

    if (reportable) {
      if (nsJSUtils::DumpEnabled()) {
        static const char line[] =
            "************************************************************\n";
        static const char preamble[] =
            "* Call to xpconnect wrapped JSObject produced this error:  *\n";
        static const char cant_get_text[] =
            "FAILED TO GET TEXT FROM EXCEPTION\n";

        fputs(line, stdout);
        fputs(preamble, stdout);
        nsCString text;
        xpc_exception->ToString(cx, text);
        if (!text.IsEmpty()) {
          fputs(text.get(), stdout);
          fputs("\n", stdout);
        } else
          fputs(cant_get_text, stdout);
        fputs(line, stdout);
      }

      // Log the exception to the JS Console, so that users can do
      // something with it.
      nsCOMPtr<nsIConsoleService> consoleService(
          components::Console::Service());
      if (consoleService) {
        nsCOMPtr<nsIScriptError> scriptError =
            do_QueryInterface(xpc_exception->GetData());

        if (!scriptError) {
          // No luck getting one from the exception, so
          // try to cook one up.
          scriptError = components::ScriptError::Create();
          if (scriptError) {
            nsCString newMessage;
            xpc_exception->ToString(cx, newMessage);
            // try to get filename, lineno from the first
            // stack frame location.
            int32_t lineNumber = 0;
            nsAutoCString sourceName;

            nsCOMPtr<nsIStackFrame> location = xpc_exception->GetLocation();
            if (location) {
              // Get line number.
              lineNumber = location->GetLineNumber(cx);

              // get a filename.
              location->GetFilename(cx, sourceName);
            }

            nsresult rv = scriptError->InitWithWindowID(
                NS_ConvertUTF8toUTF16(newMessage), sourceName, lineNumber, 0, 0,
                "XPConnect JavaScript",
                nsJSUtils::GetCurrentlyRunningCodeInnerWindowID(cx));
            if (NS_FAILED(rv)) {
              scriptError = nullptr;
            }

            rv = scriptError->InitSourceId(location->GetSourceId(cx));
            if (NS_FAILED(rv)) {
              scriptError = nullptr;
            }
          }
        }
        if (nullptr != scriptError) {
          consoleService->LogMessage(scriptError);
        }
      }
    }
    // Whether or not it passes the 'reportable' test, it might
    // still be an error and we have to do the right thing here...
    if (NS_FAILED(e_result)) {
      xpccx->SetPendingException(xpc_exception);
      return e_result;
    }
  } else {
    // see if JS code signaled failure result without throwing exception
    if (NS_FAILED(pending_result)) {
      return pending_result;
    }
  }
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsXPCWrappedJS::CallMethod(uint16_t methodIndex, const nsXPTMethodInfo* info,
                           nsXPTCMiniVariant* nativeParams) {
  // Do a release-mode assert against accessing nsXPCWrappedJS off-main-thread.
  MOZ_RELEASE_ASSERT(NS_IsMainThread(),
                     "nsXPCWrappedJS::CallMethod called off main thread");

  if (!IsValid()) {
    return NS_ERROR_UNEXPECTED;
  }

  // We need to reject an attempt to call a non-reflectable method before
  // we do anything like AutoEntryScript which might allocate in the JS engine,
  // because the method isn't marked with JS_HAZ_CAN_RUN_SCRIPT, and we want
  // to be able to take advantage of that in the GC hazard analysis.
  if (!info->IsReflectable()) {
    return NS_ERROR_FAILURE;
  }

  Value* sp = nullptr;
  Value* argv = nullptr;
  uint8_t i;
  nsresult retval = NS_ERROR_FAILURE;
  bool success;
  bool readyToDoTheCall = false;
  nsID param_iid;
  bool foundDependentParam;

  // We're about to call into script via an XPCWrappedJS, so we need an
  // AutoEntryScript. This is probably Gecko-specific at this point, and
  // definitely will be when we turn off XPConnect for the web.
  RootedObject obj(RootingCx(), GetJSObject());
  nsIGlobalObject* nativeGlobal = NativeGlobal(js::UncheckedUnwrap(obj));

  AutoAllowLegacyScriptExecution exemption;

  AutoEntryScript aes(nativeGlobal, "XPCWrappedJS method call",
                      /* aIsMainThread = */ true);
  XPCCallContext ccx(aes.cx());
  if (!ccx.IsValid()) {
    return retval;
  }

  JSContext* cx = ccx.GetJSContext();

  if (!cx) {
    return NS_ERROR_FAILURE;
  }

  // We now need to enter the realm of the actual JSObject* we are pointing at.
  // But that may be a cross-compartment wrapper and therefore not have a
  // well-defined realm, so enter the realm of the global that we grabbed back
  // when we started pointing to our JSObject*.
  RootedObject scope(cx, GetJSObjectGlobal());
  JSAutoRealm ar(cx, scope);

  const nsXPTInterfaceInfo* interfaceInfo = GetInfo();
  JS::RootedId id(cx);
  const char* name = info->NameOrDescription();
  if (!info->GetId(cx, id.get())) {
    return NS_ERROR_FAILURE;
  }

  // [optional_argc] has a different calling convention, which we don't
  // support for JS-implemented components.
  if (info->WantsOptArgc()) {
    const char* str =
        "IDL methods marked with [optional_argc] may not "
        "be implemented in JS";
    // Throw and warn for good measure.
    JS_ReportErrorASCII(cx, "%s", str);
    NS_WARNING(str);
    return CheckForException(ccx, aes, obj, name, interfaceInfo->Name());
  }

  RootedValue fval(cx);
  RootedObject thisObj(cx, obj);

  RootedValueVector args(cx);
  AutoScriptEvaluate scriptEval(cx);

  XPCJSRuntime* xpcrt = XPCJSRuntime::Get();
  XPCJSContext* xpccx = ccx.GetContext();
  AutoSavePendingResult apr(xpccx);

  // XXX ASSUMES that retval is last arg. The xpidl compiler ensures this.
  uint8_t paramCount = info->ParamCount();
  uint8_t argc = paramCount;
  if (info->HasRetval()) {
    argc -= 1;
  }

  if (!scriptEval.StartEvaluating(scope)) {
    goto pre_call_clean_up;
  }

  xpccx->SetPendingException(nullptr);

  // We use js_Invoke so that the gcthings we use as args will be rooted by
  // the engine as we do conversions and prepare to do the function call.

  // setup stack

  // if this isn't a function call then we don't need to push extra stuff
  if (!(info->IsSetter() || info->IsGetter())) {
    // We get fval before allocating the stack to avoid gc badness that can
    // happen if the GetProperty call leaves our request and the gc runs
    // while the stack we allocate contains garbage.

    // If the interface is marked as a [function] then we will assume that
    // our JSObject is a function and not an object with a named method.

    // In the xpidl [function] case we are making sure now that the
    // JSObject is callable. If it is *not* callable then we silently
    // fallback to looking up the named property...
    // (because jst says he thinks this fallback is 'The Right Thing'.)
    //
    // In the normal (non-function) case we just lookup the property by
    // name and as long as the object has such a named property we go ahead
    // and try to make the call. If it turns out the named property is not
    // a callable object then the JS engine will throw an error and we'll
    // pass this along to the caller as an exception/result code.

    fval = ObjectValue(*obj);
    if (!interfaceInfo->IsFunction() ||
        JS_TypeOfValue(ccx, fval) != JSTYPE_FUNCTION) {
      if (!JS_GetPropertyById(cx, obj, id, &fval)) {
        goto pre_call_clean_up;
      }
      // XXX We really want to factor out the error reporting better and
      // specifically report the failure to find a function with this name.
      // This is what we do below if the property is found but is not a
      // function. We just need to factor better so we can get to that
      // reporting path from here.

      thisObj = obj;
    }
  }

  if (!args.resize(argc)) {
    retval = NS_ERROR_OUT_OF_MEMORY;
    goto pre_call_clean_up;
  }

  argv = args.begin();
  sp = argv;

  // build the args
  // NB: This assignment *looks* wrong because we haven't yet called our
  // function. However, we *have* already entered the compartmen that we're
  // about to call, and that's the global that we want here. In other words:
  // we're trusting the JS engine to come up with a good global to use for
  // our object (whatever it was).
  for (i = 0; i < argc; i++) {
    const nsXPTParamInfo& param = info->Param(i);
    const nsXPTType& type = param.GetType();
    uint32_t array_count;
    RootedValue val(cx, NullValue());

    // Verify that null was not passed for a non-optional 'out' param.
    if (param.IsOut() && !nativeParams[i].val.p && !param.IsOptional()) {
      retval = NS_ERROR_INVALID_ARG;
      goto pre_call_clean_up;
    }

    if (param.IsIn()) {
      const void* pv;
      if (param.IsIndirect()) {
        pv = nativeParams[i].val.p;
      } else {
        pv = &nativeParams[i];
      }

      if (!GetInterfaceTypeFromParam(info, type, nativeParams, &param_iid) ||
          !GetArraySizeFromParam(info, type, nativeParams, &array_count))
        goto pre_call_clean_up;

      if (!XPCConvert::NativeData2JS(cx, &val, pv, type, &param_iid,
                                     array_count, nullptr))
        goto pre_call_clean_up;
    }

    if (param.IsOut()) {
      // create an 'out' object
      RootedObject out_obj(cx, NewOutObject(cx));
      if (!out_obj) {
        retval = NS_ERROR_OUT_OF_MEMORY;
        goto pre_call_clean_up;
      }

      if (param.IsIn()) {
        if (!JS_SetPropertyById(cx, out_obj,
                                xpcrt->GetStringID(XPCJSContext::IDX_VALUE),
                                val)) {
          goto pre_call_clean_up;
        }
      }
      *sp++ = JS::ObjectValue(*out_obj);
    } else
      *sp++ = val;
  }

  readyToDoTheCall = true;

pre_call_clean_up:
  // clean up any 'out' params handed in
  CleanupOutparams(info, nativeParams, /* inOutOnly = */ true, paramCount);

  if (!readyToDoTheCall) {
    return retval;
  }

  // do the deed - note exceptions

  MOZ_ASSERT(!aes.HasException());

  RefPtr<Exception> syntheticException;
  RootedValue rval(cx);
  if (info->IsGetter()) {
    success = JS_GetProperty(cx, obj, name, &rval);
  } else if (info->IsSetter()) {
    rval = *argv;
    success = JS_SetProperty(cx, obj, name, rval);
  } else {
    if (!fval.isPrimitive()) {
      success = JS_CallFunctionValue(cx, thisObj, fval, args, &rval);
    } else {
      // The property was not an object so can't be a function.
      // Let's build and 'throw' an exception.

      static const nsresult code = NS_ERROR_XPC_JSOBJECT_HAS_NO_FUNCTION_NAMED;
      static const char format[] = "%s \"%s\"";
      const char* msg;
      UniqueChars sz;

      if (nsXPCException::NameAndFormatForNSResult(code, nullptr, &msg) &&
          msg) {
        sz = JS_smprintf(format, msg, name);
      }

      XPCConvert::ConstructException(
          code, sz.get(), interfaceInfo->Name(), name, nullptr,
          getter_AddRefs(syntheticException), nullptr, nullptr);
      success = false;
    }
  }

  if (!success) {
    return CheckForException(ccx, aes, obj, name, interfaceInfo->Name(),
                             syntheticException);
  }

  xpccx->SetPendingException(nullptr);  // XXX necessary?

  // convert out args and result
  // NOTE: this is the total number of native params, not just the args
  // Convert independent params only.
  // When we later convert the dependent params (if any) we will know that
  // the params upon which they depend will have already been converted -
  // regardless of ordering.

  foundDependentParam = false;
  for (i = 0; i < paramCount; i++) {
    const nsXPTParamInfo& param = info->Param(i);
    MOZ_ASSERT(!param.IsShared(), "[shared] implies [noscript]!");
    if (!param.IsOut() || !nativeParams[i].val.p) {
      continue;
    }

    const nsXPTType& type = param.GetType();
    if (type.IsDependent()) {
      foundDependentParam = true;
      continue;
    }

    RootedValue val(cx);

    if (&param == info->GetRetval()) {
      val = rval;
    } else if (argv[i].isPrimitive()) {
      break;
    } else {
      RootedObject obj(cx, &argv[i].toObject());
      if (!JS_GetPropertyById(
              cx, obj, xpcrt->GetStringID(XPCJSContext::IDX_VALUE), &val)) {
        break;
      }
    }

    // setup allocator and/or iid

    const nsXPTType& inner = type.InnermostType();
    if (inner.Tag() == nsXPTType::T_INTERFACE) {
      if (!inner.GetInterface()) {
        break;
      }
      param_iid = inner.GetInterface()->IID();
    }

    MOZ_ASSERT(param.IsIndirect(), "outparams are always indirect");
    if (!XPCConvert::JSData2Native(cx, nativeParams[i].val.p, val, type,
                                   &param_iid, 0, nullptr))
      break;
  }

  // if any params were dependent, then we must iterate again to convert them.
  if (foundDependentParam && i == paramCount) {
    for (i = 0; i < paramCount; i++) {
      const nsXPTParamInfo& param = info->Param(i);
      if (!param.IsOut()) {
        continue;
      }

      const nsXPTType& type = param.GetType();
      if (!type.IsDependent()) {
        continue;
      }

      RootedValue val(cx);
      uint32_t array_count;

      if (&param == info->GetRetval()) {
        val = rval;
      } else {
        RootedObject obj(cx, &argv[i].toObject());
        if (!JS_GetPropertyById(
                cx, obj, xpcrt->GetStringID(XPCJSContext::IDX_VALUE), &val)) {
          break;
        }
      }

      // setup allocator and/or iid

      if (!GetInterfaceTypeFromParam(info, type, nativeParams, &param_iid) ||
          !GetArraySizeFromParam(info, type, nativeParams, &array_count))
        break;

      MOZ_ASSERT(param.IsIndirect(), "outparams are always indirect");
      if (!XPCConvert::JSData2Native(cx, nativeParams[i].val.p, val, type,
                                     &param_iid, array_count, nullptr))
        break;
    }
  }

  if (i != paramCount) {
    // We didn't manage all the result conversions!
    // We have to cleanup any junk that *did* get converted.
    CleanupOutparams(info, nativeParams, /* inOutOnly = */ false, i);
  } else {
    // set to whatever the JS code might have set as the result
    retval = xpccx->GetPendingResult();
  }

  return retval;
}

static const JSClass XPCOutParamClass = {"XPCOutParam", 0, JS_NULL_CLASS_OPS};

bool xpc::IsOutObject(JSContext* cx, JSObject* obj) {
  return JS::GetClass(obj) == &XPCOutParamClass;
}

JSObject* xpc::NewOutObject(JSContext* cx) {
  return JS_NewObject(cx, &XPCOutParamClass);
}

// static
void nsXPCWrappedJS::DebugDumpInterfaceInfo(const nsXPTInterfaceInfo* aInfo,
                                            int16_t depth) {
#ifdef DEBUG
  depth--;
  XPC_LOG_ALWAYS(("nsXPTInterfaceInfo @ %p = ", aInfo));
  XPC_LOG_INDENT();
  const char* name = aInfo->Name();
  XPC_LOG_ALWAYS(("interface name is %s", name));
  auto iid = aInfo->IID().ToString();
  XPC_LOG_ALWAYS(("IID number is %s", iid.get()));
  XPC_LOG_ALWAYS(("InterfaceInfo @ %p", aInfo));
  uint16_t methodCount = 0;
  if (depth) {
    XPC_LOG_INDENT();
    XPC_LOG_ALWAYS(("parent @ %p", aInfo->GetParent()));
    methodCount = aInfo->MethodCount();
    XPC_LOG_ALWAYS(("MethodCount = %d", methodCount));
    XPC_LOG_ALWAYS(("ConstantCount = %d", aInfo->ConstantCount()));
    XPC_LOG_OUTDENT();
  }
  XPC_LOG_ALWAYS(("method count = %d", methodCount));
  if (depth && methodCount) {
    depth--;
    XPC_LOG_INDENT();
    for (uint16_t i = 0; i < methodCount; i++) {
      XPC_LOG_ALWAYS(("Method %d is %s%s", i,
                      aInfo->Method(i).IsReflectable() ? "" : " NOT ",
                      "reflectable"));
    }
    XPC_LOG_OUTDENT();
    depth++;
  }
  XPC_LOG_OUTDENT();
#endif
}
