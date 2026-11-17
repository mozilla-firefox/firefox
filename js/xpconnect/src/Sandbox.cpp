/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * The Components.Sandbox object.
 */

#include "mozilla/dom/AbortControllerBinding.h"
#include "mozilla/dom/AutoEntryScript.h"
#include "mozilla/dom/BindingCallContext.h"
#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/BlobBinding.h"
#include "mozilla/dom/cache/CacheStorage.h"
#include "mozilla/dom/ChromeUtilsBinding.h"
#include "mozilla/dom/CSSBinding.h"
#include "mozilla/dom/CSSPositionTryDescriptorsBinding.h"
#include "mozilla/dom/CSSRuleBinding.h"
#include "mozilla/dom/DirectoryBinding.h"
#include "mozilla/dom/DocumentBinding.h"
#include "mozilla/dom/DOMExceptionBinding.h"
#include "mozilla/dom/DOMParserBinding.h"
#include "mozilla/dom/DOMTokenListBinding.h"
#include "mozilla/dom/ElementBinding.h"
#include "mozilla/dom/ElementInternalsBinding.h"
#include "mozilla/dom/EventBinding.h"
#include "mozilla/dom/Exceptions.h"
#include "mozilla/dom/Fetch.h"
#include "mozilla/dom/FileBinding.h"
#include "mozilla/dom/HeadersBinding.h"
#include "mozilla/dom/IndexedDatabaseManager.h"
#include "mozilla/dom/InspectorUtilsBinding.h"
#include "mozilla/dom/IOUtilsBinding.h"
#include "mozilla/dom/LockManager.h"
#include "mozilla/dom/MessageChannelBinding.h"
#include "mozilla/dom/MessagePortBinding.h"
#include "mozilla/dom/MIDIInputMapBinding.h"
#include "mozilla/dom/MIDIOutputMapBinding.h"
#include "mozilla/dom/ModuleLoader.h"
#include "mozilla/dom/NodeBinding.h"
#include "mozilla/dom/NodeFilterBinding.h"
#include "mozilla/dom/PathUtilsBinding.h"
#include "mozilla/dom/PerformanceBinding.h"
#include "mozilla/dom/PromiseBinding.h"
#include "mozilla/dom/PromiseDebuggingBinding.h"
#include "mozilla/dom/RangeBinding.h"
#include "mozilla/dom/ReadableStreamBinding.h"
#include "mozilla/dom/RequestBinding.h"
#include "mozilla/dom/ResponseBinding.h"
#include "mozilla/Result.h"

#include "AccessCheck.h"
#include "Crypto.h"
#include "ExpandedPrincipal.h"
#include "jsapi.h"
#include "jsfriendapi.h"
#include "nsContentUtils.h"
#include "nsGlobalWindowInner.h"
#include "nsIException.h"  // for nsIStackFrame
#include "nsIScriptContext.h"
#include "nsIScriptObjectPrincipal.h"
#include "nsIURI.h"
#include "nsJSUtils.h"
#include "nsNetUtil.h"
#include "nsPIDOMWindowInlines.h"
#include "WrapperFactory.h"
#include "xpc_make_class.h"
#include "xpcprivate.h"
#include "XPCWrapper.h"

#include "js/Array.h"             // JS::GetArrayLength, JS::IsArrayObject
#include "js/CallAndConstruct.h"  // JS::Call, JS::IsCallable
#include "js/CharacterEncoding.h"
#include "js/CompilationAndEvaluation.h"
#include "js/Object.h"  // JS::GetClass, JS::GetCompartment, JS::GetReservedSlot
#include "js/PropertyAndElement.h"  // JS_DefineFunction, JS_DefineFunctions, JS_DefineProperty, JS_GetElement, JS_GetProperty, JS_HasProperty, JS_SetProperty, JS_SetPropertyById
#include "js/PropertyDescriptor.h"  // JS::PropertyDescriptor, JS_GetOwnPropertyDescriptorById, JS_GetPropertyDescriptorById
#include "js/PropertySpec.h"
#include "js/Proxy.h"
#include "js/SourceText.h"
#include "js/StructuredClone.h"
#ifdef MOZ_WEBRTC
#  include "mozilla/dom/RTCIdentityProviderRegistrar.h"
#endif
#include "mozilla/BasePrincipal.h"
#include "mozilla/DeferredFinalize.h"
#include "mozilla/dom/FileReaderBinding.h"
#include "mozilla/dom/FormDataBinding.h"
#include "mozilla/dom/nsCSPContext.h"
#include "mozilla/dom/PolicyContainer.h"
#include "mozilla/dom/ScriptLoader.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/SelectionBinding.h"
#include "mozilla/dom/StorageManager.h"
#include "mozilla/dom/StorageManagerBinding.h"
#include "mozilla/dom/TextDecoderBinding.h"
#include "mozilla/dom/TextEncoderBinding.h"
#include "mozilla/dom/TrustedHTML.h"
#include "mozilla/dom/TrustedScript.h"
#include "mozilla/dom/TrustedScriptURL.h"
#include "mozilla/dom/URLBinding.h"
#include "mozilla/dom/URLSearchParamsBinding.h"
#include "mozilla/dom/WebSocketBinding.h"
#include "mozilla/dom/WindowBinding.h"
#include "mozilla/dom/XMLHttpRequest.h"
#include "mozilla/dom/XMLSerializerBinding.h"
#include "mozilla/ExtensionPolicyService.h"
#include "mozilla/ipc/BackgroundUtils.h"
#include "mozilla/ipc/PBackgroundSharedTypes.h"
#include "mozilla/Maybe.h"
#include "mozilla/NullPrincipal.h"
#include "mozilla/ResultExtensions.h"
#include "mozilla/StaticPrefs_extensions.h"

using namespace mozilla;
using namespace mozilla::dom;
using namespace JS;
using namespace JS::loader;
using namespace xpc;

using mozilla::dom::DestroyProtoAndIfaceCache;
using mozilla::dom::IndexedDatabaseManager;

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(SandboxPrivate)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(SandboxPrivate)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
  NS_IMPL_CYCLE_COLLECTION_UNLINK_WEAK_REFERENCE
  NS_IMPL_CYCLE_COLLECTION_UNLINK_WEAK_PTR
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mModuleLoader)
  tmp->UnlinkObjectsInGlobal();
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(SandboxPrivate)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mModuleLoader)
  tmp->TraverseObjectsInGlobal(cb);
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(SandboxPrivate)
NS_IMPL_CYCLE_COLLECTING_RELEASE(SandboxPrivate)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(SandboxPrivate)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIScriptObjectPrincipal)
  NS_INTERFACE_MAP_ENTRY(nsIScriptObjectPrincipal)
  NS_INTERFACE_MAP_ENTRY(nsIGlobalObject)
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
NS_INTERFACE_MAP_END

class nsXPCComponents_utils_Sandbox : public nsIXPCComponents_utils_Sandbox,
                                      public nsIXPCScriptable {
 public:
  // Aren't macros nice?
  NS_DECL_ISUPPORTS
  NS_DECL_NSIXPCCOMPONENTS_UTILS_SANDBOX
  NS_DECL_NSIXPCSCRIPTABLE

 public:
  nsXPCComponents_utils_Sandbox();

 private:
  virtual ~nsXPCComponents_utils_Sandbox();

  static nsresult CallOrConstruct(nsIXPConnectWrappedNative* wrapper,
                                  JSContext* cx, HandleObject obj,
                                  const CallArgs& args, bool* _retval);
};

already_AddRefed<nsIXPCComponents_utils_Sandbox> xpc::NewSandboxConstructor() {
  nsCOMPtr<nsIXPCComponents_utils_Sandbox> sbConstructor =
      new nsXPCComponents_utils_Sandbox();
  return sbConstructor.forget();
}

static bool SandboxDump(JSContext* cx, unsigned argc, Value* vp) {
  if (!nsJSUtils::DumpEnabled()) {
    return true;
  }

  CallArgs args = CallArgsFromVp(argc, vp);

  if (args.length() == 0) {
    return true;
  }

  RootedString str(cx, ToString(cx, args[0]));
  if (!str) {
    return false;
  }

  JS::UniqueChars utf8str = JS_EncodeStringToUTF8(cx, str);
  char* cstr = utf8str.get();
  if (!cstr) {
    return false;
  }

#if defined(XP_MACOSX)
  // Be nice and convert all \r to \n.
  char* c = cstr;
  char* cEnd = cstr + strlen(cstr);
  while (c < cEnd) {
    if (*c == '\r') {
      *c = '\n';
    }
    c++;
  }
#endif
  MOZ_LOG(nsContentUtils::DOMDumpLog(), mozilla::LogLevel::Debug,
          ("[Sandbox.Dump] %s", cstr));
#ifdef ANDROID
  __android_log_write(ANDROID_LOG_INFO, "GeckoDump", cstr);
#endif
  fputs(cstr, stdout);
  fflush(stdout);
  args.rval().setBoolean(true);
  return true;
}

static bool SandboxDebug(JSContext* cx, unsigned argc, Value* vp) {
#ifdef DEBUG
  return SandboxDump(cx, argc, vp);
#else
  return true;
#endif
}

static bool SandboxImport(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  if (args.length() < 1 || args[0].isPrimitive()) {
    XPCThrower::Throw(NS_ERROR_INVALID_ARG, cx);
    return false;
  }

  RootedString funname(cx);
  if (args.length() > 1) {
    // Use the second parameter as the function name.
    funname = ToString(cx, args[1]);
    if (!funname) {
      return false;
    }
  } else {
    // NB: funobj must only be used to get the JSFunction out.
    RootedObject funobj(cx, &args[0].toObject());
    if (js::IsProxy(funobj)) {
      funobj = XPCWrapper::UnsafeUnwrapSecurityWrapper(funobj);
    }

    JSAutoRealm ar(cx, funobj);

    RootedValue funval(cx, ObjectValue(*funobj));
    JS::Rooted<JSFunction*> fun(cx, JS_ValueToFunction(cx, funval));
    if (!fun) {
      XPCThrower::Throw(NS_ERROR_INVALID_ARG, cx);
      return false;
    }

    // Use the actual function name as the name.
    if (!JS_GetFunctionId(cx, fun, &funname)) {
      return false;
    }
    if (!funname) {
      XPCThrower::Throw(NS_ERROR_INVALID_ARG, cx);
      return false;
    }
  }
  RootedId id(cx);
  if (!JS_StringToId(cx, funname, &id) || !JS_WrapId(cx, &id)) {
    return false;
  }

  // We need to resolve the this object, because this function is used
  // unbound and should still work and act on the original sandbox.

  RootedObject thisObject(cx);
  if (!args.computeThis(cx, &thisObject)) {
    return false;
  }

  if (!JS_SetPropertyById(cx, thisObject, id, args[0])) {
    return false;
  }

  args.rval().setUndefined();
  return true;
}

bool xpc::SandboxCreateCrypto(JSContext* cx, JS::Handle<JSObject*> obj) {
  MOZ_ASSERT(JS_IsGlobalObject(obj));

  nsIGlobalObject* native = xpc::NativeGlobal(obj);
  MOZ_ASSERT(native);

  dom::Crypto* crypto = new dom::Crypto(native);
  JS::RootedObject wrapped(cx, crypto->WrapObject(cx, nullptr));
  return JS_DefineProperty(cx, obj, "crypto", wrapped, JSPROP_ENUMERATE);
}

#ifdef MOZ_WEBRTC
static bool SandboxCreateRTCIdentityProvider(JSContext* cx,
                                             JS::HandleObject obj) {
  MOZ_ASSERT(JS_IsGlobalObject(obj));

  nsCOMPtr<nsIGlobalObject> nativeGlobal = xpc::NativeGlobal(obj);
  MOZ_ASSERT(nativeGlobal);

  dom::RTCIdentityProviderRegistrar* registrar =
      new dom::RTCIdentityProviderRegistrar(nativeGlobal);
  JS::RootedObject wrapped(cx, registrar->WrapObject(cx, nullptr));
  return JS_DefineProperty(cx, obj, "rtcIdentityProvider", wrapped,
                           JSPROP_ENUMERATE);
}
#endif

static bool SandboxFetch(JSContext* cx, JS::HandleObject scope,
                         const CallArgs& args) {
  if (args.length() < 1) {
    JS_ReportErrorASCII(cx, "fetch requires at least 1 argument");
    return false;
  }

  BindingCallContext callCx(cx, "fetch");
  RequestOrUTF8String request;
  if (!request.Init(callCx, args[0], "Argument 1")) {
    return false;
  }
  RootedDictionary<dom::RequestInit> options(cx);
  if (!options.Init(callCx, args.hasDefined(1) ? args[1] : JS::NullHandleValue,
                    "Argument 2", false)) {
    return false;
  }
  nsCOMPtr<nsIGlobalObject> global = xpc::NativeGlobal(scope);
  if (!global) {
    return false;
  }
  dom::CallerType callerType = nsContentUtils::IsSystemCaller(cx)
                                   ? dom::CallerType::System
                                   : dom::CallerType::NonSystem;
  ErrorResult rv;
  RefPtr<dom::Promise> response = FetchRequest(
      global, Constify(request), Constify(options), callerType, rv);
  if (rv.MaybeSetPendingException(cx)) {
    return false;
  }

  args.rval().setObject(*response->PromiseObj());
  return true;
}

static bool SandboxFetchPromise(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  RootedObject scope(cx, JS::CurrentGlobalOrNull(cx));
  if (SandboxFetch(cx, scope, args)) {
    return true;
  }
  return ConvertExceptionToPromise(cx, args.rval());
}

bool xpc::SandboxCreateFetch(JSContext* cx, JS::Handle<JSObject*> obj) {
  MOZ_ASSERT(JS_IsGlobalObject(obj));

  return JS_DefineFunction(cx, obj, "fetch", SandboxFetchPromise, 2, 0) &&
         Request_Binding::CreateAndDefineOnGlobal(cx) &&
         Response_Binding::CreateAndDefineOnGlobal(cx) &&
         Headers_Binding::CreateAndDefineOnGlobal(cx);
}

static bool SandboxCreateStorage(JSContext* cx, JS::HandleObject obj) {
  MOZ_ASSERT(JS_IsGlobalObject(obj));

  nsIGlobalObject* native = xpc::NativeGlobal(obj);
  MOZ_ASSERT(native);

  if (!StorageManager_Binding::CreateAndDefineOnGlobal(cx)) {
    return false;
  }

  dom::StorageManager* storageManager = new dom::StorageManager(native);
  JS::RootedObject wrapped(cx, storageManager->WrapObject(cx, nullptr));
  return JS_DefineProperty(cx, obj, "storage", wrapped, JSPROP_ENUMERATE);
}

// Prior to bug 2013389, the following DOM objects would be structured-cloned
// into the inner window's realm when `structuredClone` is called from an
// extension content script. All other objects would remain within the content
// script's realm. This method is used to retain this historic behaviour.
//
// See bug 2017797 for discussion about this behaviour.
static bool LegacyShouldCloneIntoWindow(JS::Handle<JSObject*> obj) {
  return IS_INSTANCE_OF(Blob, obj) || IS_INSTANCE_OF(Directory, obj) ||
         IS_INSTANCE_OF(FileList, obj) || IS_INSTANCE_OF(FormData, obj) ||
         IS_INSTANCE_OF(ImageBitmap, obj) || IS_INSTANCE_OF(VideoFrame, obj) ||
         IS_INSTANCE_OF(EncodedVideoChunk, obj) ||
         IS_INSTANCE_OF(AudioData, obj) ||
         IS_INSTANCE_OF(EncodedAudioChunk, obj) ||
#ifdef MOZ_WEBRTC
         IS_INSTANCE_OF(RTCEncodedVideoFrame, obj) ||
         IS_INSTANCE_OF(RTCEncodedAudioFrame, obj) ||
         IS_INSTANCE_OF(RTCDataChannel, obj) ||
#endif
         IS_INSTANCE_OF(MessagePort, obj) ||
         IS_INSTANCE_OF(OffscreenCanvas, obj) ||
         IS_INSTANCE_OF(ReadableStream, obj) ||
         IS_INSTANCE_OF(WritableStream, obj) ||
         IS_INSTANCE_OF(TransformStream, obj);
}

static bool SandboxStructuredClone(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  if (!args.requireAtLeast(cx, "structuredClone", 1)) {
    return false;
  }

  RootedDictionary<dom::StructuredSerializeOptions> options(cx);
  if (!options.Init(cx, args.hasDefined(1) ? args[1] : JS::NullHandleValue,
                    "Argument 2", false)) {
    return false;
  }

  // NOTE: A spec-compliant structuredClone should determine & use the relevant
  // global instead of the current global.
  nsCOMPtr<nsIGlobalObject> global = CurrentNativeGlobal(cx);
  if (!global) {
    JS_ReportErrorASCII(cx, "structuredClone: Missing global");
    return false;
  }

  // If this is a content script, we may want to clone into that window instead.
  // See the comment on LegacyShouldCloneIntoWindow for details.
  if (IsWebExtensionContentScriptSandbox(global->GetGlobalJSObject()) &&
      StaticPrefs::extensions_webextensions_legacyStructuredCloneBehavior() &&
      args[0].isObject()) {
    JS::Rooted<JSObject*> obj(cx, &args[0].toObject());
    if (LegacyShouldCloneIntoWindow(obj)) {
      RefPtr<nsGlobalWindowInner> window =
          SandboxWindowOrNull(global->GetGlobalJSObject(), cx);
      if (window) {
        global = window;
      }
    }
  }

  JS::Rooted<JS::Value> result(cx);
  ErrorResult rv;
  nsContentUtils::StructuredClone(cx, global, args[0], options, &result, rv);
  if (rv.MaybeSetPendingException(cx)) {
    return false;
  }

  // Because we specified a custom `global`, the returned value may not be in
  // our realm.
  if (!mozilla::dom::MaybeWrapValue(cx, &result)) {
    return false;
  }

  MOZ_ASSERT_IF(result.isGCThing(),
                !JS::GCThingIsMarkedGray(result.toGCCellPtr()));
  args.rval().set(result);
  return true;
}

bool xpc::SandboxCreateStructuredClone(JSContext* cx, HandleObject obj) {
  MOZ_ASSERT(JS_IsGlobalObject(obj));

  return JS_DefineFunction(cx, obj, "structuredClone", SandboxStructuredClone,
                           1, 0);
}

bool xpc::SandboxCreateLocks(JSContext* cx, JS::Handle<JSObject*> obj) {
  MOZ_ASSERT(JS_IsGlobalObject(obj));

  nsIGlobalObject* native = xpc::NativeGlobal(obj);
  MOZ_ASSERT(native);

  RefPtr<dom::LockManager> lockManager = dom::LockManager::Create(*native);
  JS::RootedObject wrapped(cx, lockManager->WrapObject(cx, nullptr));
  return JS_DefineProperty(cx, obj, "locks", wrapped, JSPROP_ENUMERATE);
}

static bool SandboxIsProxy(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  if (args.length() < 1) {
    JS_ReportErrorASCII(cx, "Function requires at least 1 argument");
    return false;
  }
  if (!args[0].isObject()) {
    args.rval().setBoolean(false);
    return true;
  }

  RootedObject obj(cx, &args[0].toObject());
  // CheckedUnwrapStatic is OK here, since we only care about whether
  // it's a scripted proxy and the things CheckedUnwrapStatic fails on
  // are not.
  obj = js::CheckedUnwrapStatic(obj);
  if (!obj) {
    args.rval().setBoolean(false);
    return true;
  }

  args.rval().setBoolean(js::IsScriptedProxy(obj));
  return true;
}

/*
 * Expected type of the arguments and the return value:
 * function exportFunction(function funToExport,
 *                         object targetScope,
 *                         [optional] object options)
 */
static bool SandboxExportFunction(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  if (args.length() < 2) {
    JS_ReportErrorASCII(cx, "Function requires at least 2 arguments");
    return false;
  }

  RootedValue options(cx, args.length() > 2 ? args[2] : UndefinedValue());
  return ExportFunction(cx, args[0], args[1], options, args.rval());
}

static bool SandboxCreateObjectIn(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  if (args.length() < 1) {
    JS_ReportErrorASCII(cx, "Function requires at least 1 argument");
    return false;
  }

  RootedObject optionsObj(cx);
  bool calledWithOptions = args.length() > 1;
  if (calledWithOptions) {
    if (!args[1].isObject()) {
      JS_ReportErrorASCII(
          cx, "Expected the 2nd argument (options) to be an object");
      return false;
    }
    optionsObj = &args[1].toObject();
  }

  CreateObjectInOptions options(cx, optionsObj);
  if (calledWithOptions && !options.Parse()) {
    return false;
  }

  return xpc::CreateObjectIn(cx, args[0], options, args.rval());
}

static bool SandboxCloneInto(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  if (args.length() < 2) {
    JS_ReportErrorASCII(cx, "Function requires at least 2 arguments");
    return false;
  }

  RootedValue options(cx, args.length() > 2 ? args[2] : UndefinedValue());
  return xpc::CloneInto(cx, args[0], args[1], options, args.rval());
}

static void sandbox_finalize(JS::GCContext* gcx, JSObject* obj) {
  SandboxPrivate* priv = SandboxPrivate::GetPrivate(obj);
  if (!priv) {
    // priv can be null if CreateSandboxObject fails in the middle.
    return;
  }

  priv->ForgetGlobalObject(obj);
  DestroyProtoAndIfaceCache(obj);
  DeferredFinalize(static_cast<nsIScriptObjectPrincipal*>(priv));
}

static size_t sandbox_moved(JSObject* obj, JSObject* old) {
  // Note that this hook can be called before the private pointer is set. In
  // this case the SandboxPrivate will not exist yet, so there is nothing to
  // do.
  SandboxPrivate* priv = SandboxPrivate::GetPrivate(obj);
  if (!priv) {
    return 0;
  }

  return priv->ObjectMoved(obj, old);
}

#define XPCONNECT_SANDBOX_CLASS_METADATA_SLOT \
  (XPCONNECT_GLOBAL_EXTRA_SLOT_OFFSET)

static const JSClassOps SandboxClassOps = {
    .newEnumerate = JS_NewEnumerateStandardClasses,
    .resolve = JS_ResolveStandardClass,
    .mayResolve = JS_MayResolveStandardClass,
    .finalize = sandbox_finalize,
    .trace = JS_GlobalObjectTraceHook,
};

static const js::ClassExtension SandboxClassExtension = {
    sandbox_moved,  // objectMovedOp
};

static const JSClass SandboxClass = {
    "Sandbox",
    XPCONNECT_GLOBAL_FLAGS_WITH_EXTRA_SLOTS(1) | JSCLASS_FOREGROUND_FINALIZE,
    &SandboxClassOps,
    JS_NULL_CLASS_SPEC,
    &SandboxClassExtension,
    JS_NULL_OBJECT_OPS};

static const JSFunctionSpec SandboxFunctions[] = {
    JS_FN("dump", SandboxDump, 1, 0), JS_FN("debug", SandboxDebug, 1, 0),
    JS_FN("importFunction", SandboxImport, 1, 0), JS_FS_END};

bool xpc::IsSandbox(JSObject* obj) {
  const JSClass* clasp = JS::GetClass(obj);
  return clasp == &SandboxClass;
}

/***************************************************************************/
nsXPCComponents_utils_Sandbox::nsXPCComponents_utils_Sandbox() = default;

nsXPCComponents_utils_Sandbox::~nsXPCComponents_utils_Sandbox() = default;

NS_IMPL_QUERY_INTERFACE(nsXPCComponents_utils_Sandbox,
                        nsIXPCComponents_utils_Sandbox, nsIXPCScriptable)

NS_IMPL_ADDREF(nsXPCComponents_utils_Sandbox)
NS_IMPL_RELEASE(nsXPCComponents_utils_Sandbox)

// We use the nsIXPScriptable macros to generate lots of stuff for us.
#define XPC_MAP_CLASSNAME nsXPCComponents_utils_Sandbox
#define XPC_MAP_QUOTED_CLASSNAME "nsXPCComponents_utils_Sandbox"
#define XPC_MAP_FLAGS (XPC_SCRIPTABLE_WANT_CALL | XPC_SCRIPTABLE_WANT_CONSTRUCT)
#include "xpc_map_end.h" /* This #undef's the above. */

class SandboxProxyHandler : public js::Wrapper {
 public:
  constexpr SandboxProxyHandler() : js::Wrapper(0) {}

  virtual bool getOwnPropertyDescriptor(
      JSContext* cx, JS::Handle<JSObject*> proxy, JS::Handle<jsid> id,
      JS::MutableHandle<Maybe<JS::PropertyDescriptor>> desc) const override;

  // We just forward the high-level methods to the BaseProxyHandler versions
  // which implement them in terms of lower-level methods.
  virtual bool has(JSContext* cx, JS::Handle<JSObject*> proxy,
                   JS::Handle<jsid> id, bool* bp) const override;
  virtual bool get(JSContext* cx, JS::Handle<JSObject*> proxy,
                   JS::HandleValue receiver, JS::Handle<jsid> id,
                   JS::MutableHandle<JS::Value> vp) const override;
  virtual bool set(JSContext* cx, JS::Handle<JSObject*> proxy,
                   JS::Handle<jsid> id, JS::Handle<JS::Value> v,
                   JS::Handle<JS::Value> receiver,
                   JS::ObjectOpResult& result) const override;

  virtual bool hasOwn(JSContext* cx, JS::Handle<JSObject*> proxy,
                      JS::Handle<jsid> id, bool* bp) const override;
  virtual bool getOwnEnumerablePropertyKeys(
      JSContext* cx, JS::Handle<JSObject*> proxy,
      JS::MutableHandleIdVector props) const override;
  virtual bool enumerate(JSContext* cx, JS::Handle<JSObject*> proxy,
                         JS::MutableHandleIdVector props) const override;

 private:
  // Implements the custom getPropertyDescriptor behavior. If the getOwn
  // argument is true we only look for "own" properties.
  bool getPropertyDescriptorImpl(
      JSContext* cx, JS::Handle<JSObject*> proxy, JS::Handle<jsid> id,
      bool getOwn, JS::MutableHandle<Maybe<JS::PropertyDescriptor>> desc) const;
};

static const SandboxProxyHandler sandboxProxyHandler;

namespace xpc {

bool IsSandboxPrototypeProxy(JSObject* obj) {
  return js::IsProxy(obj) && js::GetProxyHandler(obj) == &sandboxProxyHandler;
}

bool IsWebExtensionContentScriptSandbox(JSObject* obj) {
  return IsSandbox(obj) &&
         CompartmentPrivate::Get(obj)->isWebExtensionContentScript;
}

}  // namespace xpc

// A proxy handler that lets us wrap callables and invoke them with
// the correct this object, while forwarding all other operations down
// to them directly.
class SandboxCallableProxyHandler : public js::Wrapper {
 public:
  constexpr SandboxCallableProxyHandler() : js::Wrapper(0) {}

  virtual bool call(JSContext* cx, JS::Handle<JSObject*> proxy,
                    const JS::CallArgs& args) const override;

  static const size_t SandboxProxySlot = 0;

  static inline JSObject* getSandboxProxy(JS::Handle<JSObject*> proxy) {
    return &js::GetProxyReservedSlot(proxy, SandboxProxySlot).toObject();
  }
};

static const SandboxCallableProxyHandler sandboxCallableProxyHandler;

bool SandboxCallableProxyHandler::call(JSContext* cx,
                                       JS::Handle<JSObject*> proxy,
                                       const JS::CallArgs& args) const {
  // We forward the call to our underlying callable.

  // Get our SandboxProxyHandler proxy.
  RootedObject sandboxProxy(cx, getSandboxProxy(proxy));
  MOZ_ASSERT(js::IsProxy(sandboxProxy) &&
             js::GetProxyHandler(sandboxProxy) == &sandboxProxyHandler);

  // The global of the sandboxProxy is the sandbox global, and the
  // target object is the original proto.
  RootedObject sandboxGlobal(cx, JS::GetNonCCWObjectGlobal(sandboxProxy));
  MOZ_ASSERT(IsSandbox(sandboxGlobal));

  // If our this object is the sandbox global, we call with this set to the
  // original proto instead.
  //
  // There are two different ways we can compute |this|. If we use
  // JS_THIS_VALUE, we'll get the bonafide |this| value as passed by the
  // caller, which may be undefined if a global function was invoked without
  // an explicit invocant. If we use JS_THIS or JS_THIS_OBJECT, the |this|
  // in |vp| will be coerced to the global, which is not the correct
  // behavior in ES5 strict mode. And we have no way to compute strictness
  // here.
  //
  // The naive approach is simply to use JS_THIS_VALUE here. If |this| was
  // explicit, we can remap it appropriately. If it was implicit, then we
  // leave it as undefined, and let the callee sort it out. Since the callee
  // is generally in the same compartment as its global (eg the Window's
  // compartment, not the Sandbox's), the callee will generally compute the
  // correct |this|.
  //
  // However, this breaks down in the Xray case. If the sandboxPrototype
  // is an Xray wrapper, then we'll end up reifying the native methods in
  // the Sandbox's scope, which means that they'll compute |this| to be the
  // Sandbox, breaking old-style XPC_WN_CallMethod methods.
  //
  // Luckily, the intent of Xrays is to provide a vanilla view of a foreign
  // DOM interface, which means that we don't care about script-enacted
  // strictness in the prototype's home compartment. Indeed, since DOM
  // methods are always non-strict, we can just assume non-strict semantics
  // if the sandboxPrototype is an Xray Wrapper, which lets us appropriately
  // remap |this|.
  bool isXray = WrapperFactory::IsXrayWrapper(sandboxProxy);
  RootedValue thisVal(cx, args.thisv());
  if (isXray) {
    RootedObject thisObject(cx);
    if (!args.computeThis(cx, &thisObject)) {
      return false;
    }
    thisVal.setObject(*thisObject);
  }

  if (thisVal == ObjectValue(*sandboxGlobal)) {
    thisVal = ObjectValue(*js::GetProxyTargetObject(sandboxProxy));
  }

  RootedValue func(cx, js::GetProxyPrivate(proxy));
  return JS::Call(cx, thisVal, func, args, args.rval());
}

/*
 * Wrap a callable such that if we're called with oldThisObj as the
 * "this" we will instead call it with newThisObj as the this.
 */
static JSObject* WrapCallable(JSContext* cx, HandleObject callable,
                              HandleObject sandboxProtoProxy) {
  MOZ_ASSERT(JS::IsCallable(callable));
  // Our proxy is wrapping the callable.  So we need to use the
  // callable as the private.  We put the given sandboxProtoProxy in
  // an extra slot, and our call() hook depends on that.
  MOZ_ASSERT(js::IsProxy(sandboxProtoProxy) &&
             js::GetProxyHandler(sandboxProtoProxy) == &sandboxProxyHandler);

  RootedValue priv(cx, ObjectValue(*callable));
  // We want to claim to have the same proto as our wrapped callable, so set
  // ourselves up with a lazy proto.
  js::ProxyOptions options;
  options.setLazyProto(true);
  JSObject* obj = js::NewProxyObject(cx, &sandboxCallableProxyHandler, priv,
                                     nullptr, options);
  if (obj) {
    js::SetProxyReservedSlot(obj, SandboxCallableProxyHandler::SandboxProxySlot,
                             ObjectValue(*sandboxProtoProxy));
  }

  return obj;
}

bool WrapAccessorFunction(JSContext* cx, MutableHandleObject accessor,
                          HandleObject sandboxProtoProxy) {
  if (!accessor) {
    return true;
  }

  accessor.set(WrapCallable(cx, accessor, sandboxProtoProxy));
  return !!accessor;
}

static bool IsMaybeWrappedDOMConstructor(JSObject* obj) {
  // We really care about the underlying object here, which might be wrapped in
  // cross-compartment wrappers.  CheckedUnwrapStatic is fine, since we just
  // care whether it's a DOM constructor.
  obj = js::CheckedUnwrapStatic(obj);
  if (!obj) {
    return false;
  }

  return dom::IsDOMConstructor(obj);
}

bool SandboxProxyHandler::getPropertyDescriptorImpl(
    JSContext* cx, JS::Handle<JSObject*> proxy, JS::Handle<jsid> id,
    bool getOwn, MutableHandle<Maybe<PropertyDescriptor>> desc_) const {
  JS::RootedObject obj(cx, wrappedObject(proxy));

  MOZ_ASSERT(JS::GetCompartment(obj) == JS::GetCompartment(proxy));

  if (getOwn) {
    if (!JS_GetOwnPropertyDescriptorById(cx, obj, id, desc_)) {
      return false;
    }
  } else {
    Rooted<JSObject*> holder(cx);
    if (!JS_GetPropertyDescriptorById(cx, obj, id, desc_, &holder)) {
      return false;
    }
  }

  if (desc_.isNothing()) {
    return true;
  }

  Rooted<PropertyDescriptor> desc(cx, *desc_);

  // Now fix up the getter/setter/value as needed.
  if (desc.hasGetter() && !WrapAccessorFunction(cx, desc.getter(), proxy)) {
    return false;
  }
  if (desc.hasSetter() && !WrapAccessorFunction(cx, desc.setter(), proxy)) {
    return false;
  }
  if (desc.hasValue() && desc.value().isObject()) {
    RootedObject val(cx, &desc.value().toObject());
    if (JS::IsCallable(val) &&
        // Don't wrap DOM constructors: they don't care about the "this"
        // they're invoked with anyway, being constructors.  And if we wrap
        // them here we break invariants like Node == Node and whatnot.
        !IsMaybeWrappedDOMConstructor(val)) {
      val = WrapCallable(cx, val, proxy);
      if (!val) {
        return false;
      }
      desc.value().setObject(*val);
    }
  }

  desc_.set(Some(desc.get()));
  return true;
}

bool SandboxProxyHandler::getOwnPropertyDescriptor(
    JSContext* cx, JS::Handle<JSObject*> proxy, JS::Handle<jsid> id,
    MutableHandle<Maybe<PropertyDescriptor>> desc) const {
  return getPropertyDescriptorImpl(cx, proxy, id, /* getOwn = */ true, desc);
}

/*
 * Reuse the BaseProxyHandler versions of the derived traps that are implemented
 * in terms of the fundamental traps.
 */

bool SandboxProxyHandler::has(JSContext* cx, JS::Handle<JSObject*> proxy,
                              JS::Handle<jsid> id, bool* bp) const {
  // This uses JS_GetPropertyDescriptorById for backward compatibility.
  Rooted<Maybe<PropertyDescriptor>> desc(cx);
  if (!getPropertyDescriptorImpl(cx, proxy, id, /* getOwn = */ false, &desc)) {
    return false;
  }

  *bp = desc.isSome();
  return true;
}
bool SandboxProxyHandler::hasOwn(JSContext* cx, JS::Handle<JSObject*> proxy,
                                 JS::Handle<jsid> id, bool* bp) const {
  return BaseProxyHandler::hasOwn(cx, proxy, id, bp);
}

bool SandboxProxyHandler::get(JSContext* cx, JS::Handle<JSObject*> proxy,
                              JS::Handle<JS::Value> receiver,
                              JS::Handle<jsid> id,
                              JS::MutableHandle<Value> vp) const {
  // This uses JS_GetPropertyDescriptorById for backward compatibility.
  Rooted<Maybe<PropertyDescriptor>> desc(cx);
  if (!getPropertyDescriptorImpl(cx, proxy, id, /* getOwn = */ false, &desc)) {
    return false;
  }

  if (desc.isNothing()) {
    vp.setUndefined();
    return true;
  } else {
    desc->assertComplete();
  }

  // Everything after here follows [[Get]] for ordinary objects.
  if (desc->isDataDescriptor()) {
    vp.set(desc->value());
    return true;
  }

  MOZ_ASSERT(desc->isAccessorDescriptor());
  RootedObject getter(cx, desc->getter());

  if (!getter) {
    vp.setUndefined();
    return true;
  }

  return Call(cx, receiver, getter, HandleValueArray::empty(), vp);
}

bool SandboxProxyHandler::set(JSContext* cx, JS::Handle<JSObject*> proxy,
                              JS::Handle<jsid> id, JS::Handle<Value> v,
                              JS::Handle<Value> receiver,
                              JS::ObjectOpResult& result) const {
  return BaseProxyHandler::set(cx, proxy, id, v, receiver, result);
}

bool SandboxProxyHandler::getOwnEnumerablePropertyKeys(
    JSContext* cx, JS::Handle<JSObject*> proxy,
    MutableHandleIdVector props) const {
  return BaseProxyHandler::getOwnEnumerablePropertyKeys(cx, proxy, props);
}

bool SandboxProxyHandler::enumerate(JSContext* cx, JS::Handle<JSObject*> proxy,
                                    JS::MutableHandleIdVector props) const {
  return BaseProxyHandler::enumerate(cx, proxy, props);
}

bool xpc::GlobalProperties::Parse(JSContext* cx, JS::HandleObject obj) {
  uint32_t length;
  bool ok = JS::GetArrayLength(cx, obj, &length);
  NS_ENSURE_TRUE(ok, false);
  for (uint32_t i = 0; i < length; i++) {
    RootedValue nameValue(cx);
    ok = JS_GetElement(cx, obj, i, &nameValue);
    NS_ENSURE_TRUE(ok, false);
    if (!nameValue.isString()) {
      JS_ReportErrorASCII(cx, "Property names must be strings");
      return false;
    }
    JSLinearString* nameStr = JS_EnsureLinearString(cx, nameValue.toString());
    if (!nameStr) {
      return false;
    }

    if (JS_LinearStringEqualsLiteral(nameStr, "AbortController")) {
      AbortController = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "Blob")) {
      Blob = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "ChromeUtils")) {
      ChromeUtils = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "CSS")) {
      CSS = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr,
                                            "CSSPositionTryDescriptors")) {
      CSSPositionTryDescriptors = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "CSSRule")) {
      CSSRule = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "CustomStateSet")) {
      CustomStateSet = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "Document")) {
      Document = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "Directory")) {
      Directory = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "DOMException")) {
      DOMException = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "DOMParser")) {
      DOMParser = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "DOMTokenList")) {
      DOMTokenList = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "Element")) {
      Element = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "Event")) {
      Event = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "File")) {
      File = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "FileReader")) {
      FileReader = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "FormData")) {
      FormData = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "Headers")) {
      Headers = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "IOUtils")) {
      IOUtils = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "InspectorCSSParser")) {
      InspectorCSSParser = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "InspectorUtils")) {
      InspectorUtils = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "MessageChannel")) {
      MessageChannel = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "MIDIInputMap")) {
      MIDIInputMap = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "MIDIOutputMap")) {
      MIDIOutputMap = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "Node")) {
      Node = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "NodeFilter")) {
      NodeFilter = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "PathUtils")) {
      PathUtils = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "Performance")) {
      Performance = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "PromiseDebugging")) {
      PromiseDebugging = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "Range")) {
      Range = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "Selection")) {
      Selection = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "TextDecoder")) {
      TextDecoder = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "TextEncoder")) {
      TextEncoder = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "TrustedHTML")) {
      TrustedHTML = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "TrustedScript")) {
      TrustedScript = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "TrustedScriptURL")) {
      TrustedScriptURL = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "URL")) {
      URL = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "URLSearchParams")) {
      URLSearchParams = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "XMLHttpRequest")) {
      XMLHttpRequest = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "WebSocket")) {
      WebSocket = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "Window")) {
      Window = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "XMLSerializer")) {
      XMLSerializer = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "ReadableStream")) {
      ReadableStream = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "atob")) {
      atob = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "btoa")) {
      btoa = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "caches")) {
      caches = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "crypto")) {
      crypto = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "fetch")) {
      fetch = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "storage")) {
      storage = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "structuredClone")) {
      structuredClone = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "locks")) {
      locks = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "indexedDB")) {
      indexedDB = true;
    } else if (JS_LinearStringEqualsLiteral(nameStr, "isSecureContext")) {
      isSecureContext = true;
#ifdef MOZ_WEBRTC
    } else if (JS_LinearStringEqualsLiteral(nameStr, "rtcIdentityProvider")) {
      rtcIdentityProvider = true;
#endif
    } else {
      RootedString nameStr(cx, nameValue.toString());
      JS::UniqueChars name = JS_EncodeStringToUTF8(cx, nameStr);
      if (!name) {
        return false;
      }

      JS_ReportErrorUTF8(cx, "Unknown property name: %s", name.get());
      return false;
    }
  }
  return true;
}

bool xpc::GlobalProperties::Define(JSContext* cx, JS::HandleObject obj) {
  MOZ_ASSERT(js::GetContextCompartment(cx) == JS::GetCompartment(obj));
  // Properties will be exposed to System automatically but not to Sandboxes
  // if |[Exposed=System]| is specified.
  // This function holds common properties not exposed automatically but able
  // to be requested either in |Cu.importGlobalProperties| or
  // |wantGlobalProperties| of a sandbox.

#define DEFINE_WEBIDL_INTERFACE_OR_NAMESPACE(_iface)                     \
  if ((_iface) && !dom::_iface##_Binding::CreateAndDefineOnGlobal(cx)) { \
    return false;                                                        \
  }

  DEFINE_WEBIDL_INTERFACE_OR_NAMESPACE(AbortController)
  DEFINE_WEBIDL_INTERFACE_OR_NAMESPACE(ChromeUtils)
  DEFINE_WEBIDL_INTERFACE_OR_NAMESPACE(Blob)
  DEFINE_WEBIDL_INTERFACE_OR_NAMESPACE(CSS)
  DEFINE_WEBIDL_INTERFACE_OR_NAMESPACE(CSSPositionTryDescriptors)
  DEFINE_WEBIDL_INTERFACE_OR_NAMESPACE(CSSRule)
  DEFINE_WEBIDL_INTERFACE_OR_NAMESPACE(CustomStateSet)
  DEFINE_WEBIDL_INTERFACE_OR_NAMESPACE(Directory)
  DEFINE_WEBIDL_INTERFACE_OR_NAMESPACE(Document)
  DEFINE_WEBIDL_INTERFACE_OR_NAMESPACE(DOMException)
  DEFINE_WEBIDL_INTERFACE_OR_NAMESPACE(DOMParser)
  DEFINE_WEBIDL_INTERFACE_OR_NAMESPACE(DOMTokenList)
  DEFINE_WEBIDL_INTERFACE_OR_NAMESPACE(Element)
  DEFINE_WEBIDL_INTERFACE_OR_NAMESPACE(Event)
  DEFINE_WEBIDL_INTERFACE_OR_NAMESPACE(File)
  DEFINE_WEBIDL_INTERFACE_OR_NAMESPACE(FileReader)
  DEFINE_WEBIDL_INTERFACE_OR_NAMESPACE(FormData)
  DEFINE_WEBIDL_INTERFACE_OR_NAMESPACE(Headers)
  DEFINE_WEBIDL_INTERFACE_OR_NAMESPACE(IOUtils)
  DEFINE_WEBIDL_INTERFACE_OR_NAMESPACE(InspectorCSSParser)
  DEFINE_WEBIDL_INTERFACE_OR_NAMESPACE(InspectorUtils)
  DEFINE_WEBIDL_INTERFACE_OR_NAMESPACE(MessageChannel)
  if (MessageChannel && !MessagePort_Binding::CreateAndDefineOnGlobal(cx)) {
    return false;
  }
  DEFINE_WEBIDL_INTERFACE_OR_NAMESPACE(MIDIInputMap)
  DEFINE_WEBIDL_INTERFACE_OR_NAMESPACE(MIDIOutputMap)
  DEFINE_WEBIDL_INTERFACE_OR_NAMESPACE(Node)
  DEFINE_WEBIDL_INTERFACE_OR_NAMESPACE(NodeFilter)
  DEFINE_WEBIDL_INTERFACE_OR_NAMESPACE(PathUtils)
  DEFINE_WEBIDL_INTERFACE_OR_NAMESPACE(Performance)
  DEFINE_WEBIDL_INTERFACE_OR_NAMESPACE(PromiseDebugging)
  DEFINE_WEBIDL_INTERFACE_OR_NAMESPACE(Range)
  DEFINE_WEBIDL_INTERFACE_OR_NAMESPACE(ReadableStream)
  DEFINE_WEBIDL_INTERFACE_OR_NAMESPACE(Selection)
  DEFINE_WEBIDL_INTERFACE_OR_NAMESPACE(TextDecoder)
  DEFINE_WEBIDL_INTERFACE_OR_NAMESPACE(TextEncoder)
  DEFINE_WEBIDL_INTERFACE_OR_NAMESPACE(TrustedHTML)
  DEFINE_WEBIDL_INTERFACE_OR_NAMESPACE(TrustedScript)
  DEFINE_WEBIDL_INTERFACE_OR_NAMESPACE(TrustedScriptURL)
  DEFINE_WEBIDL_INTERFACE_OR_NAMESPACE(URL)
  DEFINE_WEBIDL_INTERFACE_OR_NAMESPACE(URLSearchParams)
  DEFINE_WEBIDL_INTERFACE_OR_NAMESPACE(WebSocket)
  DEFINE_WEBIDL_INTERFACE_OR_NAMESPACE(Window)
  DEFINE_WEBIDL_INTERFACE_OR_NAMESPACE(XMLHttpRequest)
  DEFINE_WEBIDL_INTERFACE_OR_NAMESPACE(XMLSerializer)

#undef DEFINE_WEBIDL_INTERFACE_OR_NAMESPACE

  if (atob && !JS_DefineFunction(cx, obj, "atob", Atob, 1, 0)) return false;

  if (btoa && !JS_DefineFunction(cx, obj, "btoa", Btoa, 1, 0)) return false;

  if (caches && !dom::cache::CacheStorage::DefineCachesForSandbox(cx, obj)) {
    return false;
  }

  if (crypto && !SandboxCreateCrypto(cx, obj)) {
    return false;
  }

  if (fetch && !SandboxCreateFetch(cx, obj)) {
    return false;
  }

  if (storage && !SandboxCreateStorage(cx, obj)) {
    return false;
  }

  if (structuredClone && !SandboxCreateStructuredClone(cx, obj)) {
    return false;
  }

  if (locks && !SandboxCreateLocks(cx, obj)) {
    return false;
  }

  // Note that isSecureContext here doesn't mean the context is actually secure
  // - just that the caller wants the property defined
  if (isSecureContext) {
    bool hasSecureContext = IsSecureContextOrObjectIsFromSecureContext(cx, obj);
    JS::Rooted<JS::Value> secureJsValue(cx, JS::BooleanValue(hasSecureContext));
    return JS_DefineProperty(cx, obj, "isSecureContext", secureJsValue,
                             JSPROP_ENUMERATE);
  }

#ifdef MOZ_WEBRTC
  if (rtcIdentityProvider && !SandboxCreateRTCIdentityProvider(cx, obj)) {
    return false;
  }
#endif

  return true;
}

bool xpc::GlobalProperties::DefineInXPCComponents(JSContext* cx,
                                                  JS::HandleObject obj) {
  if (indexedDB && !IndexedDatabaseManager::DefineIndexedDB(cx, obj))
    return false;

  return Define(cx, obj);
}

bool xpc::GlobalProperties::DefineInSandbox(JSContext* cx,
                                            JS::HandleObject obj) {
  MOZ_ASSERT(IsSandbox(obj));
  MOZ_ASSERT(js::GetContextCompartment(cx) == JS::GetCompartment(obj));

  if (indexedDB && !(IndexedDatabaseManager::ResolveSandboxBinding(cx) &&
                     IndexedDatabaseManager::DefineIndexedDB(cx, obj)))
    return false;

  return Define(cx, obj);
}

nsresult SetSandboxCSP(nsISupports* prinOrSop, const nsAString& cspString) {
  nsCOMPtr<nsIPrincipal> principal = do_QueryInterface(prinOrSop);
  if (!principal) {
    return NS_ERROR_INVALID_ARG;
  }
  auto* basePrin = BasePrincipal::Cast(principal);
  if (!basePrin->Is<ExpandedPrincipal>()) {
    return NS_ERROR_INVALID_ARG;
  }
  auto* expanded = basePrin->As<ExpandedPrincipal>();

  nsCOMPtr<nsIContentSecurityPolicy> csp;

  // The choice of self-uri (self-origin) to use in a Sandbox depends on the
  // use case. For now, we default to a non-existing URL because there is no
  // use case that requires 'self' to have a particular value. Consumers can
  // always specify the URL explicitly instead of 'self'. Besides, the CSP
  // enforcement in a Sandbox is barely implemented, except for eval()-like
  // execution.
  //
  // moz-extension:-resources are never blocked by CSP because the protocol is
  // registered with URI_IS_LOCAL_RESOURCE and subjectToCSP in nsCSPService.cpp
  // therefore allows the load. This matches the CSP spec, which explicitly
  // states that CSP should not interfere with addons. Because of this, we do
  // not need to set selfURI to the real moz-extension:-URL, even in cases
  // where we want to restrict all scripts except for moz-extension:-URLs.
  nsCOMPtr<nsIURI> selfURI;
  MOZ_TRY(NS_NewURI(getter_AddRefs(selfURI), "moz-extension://dummy"_ns));

#ifdef MOZ_DEBUG
  // Bug 1548468: Move CSP off ExpandedPrincipal
  expanded->GetCsp(getter_AddRefs(csp));
  if (csp) {
    uint32_t count = 0;
    csp->GetPolicyCount(&count);
    if (count > 0) {
      // Ensure that the policy was not already added.
      nsAutoString parsedPolicyStr;
      for (uint32_t i = 0; i < count; i++) {
        csp->GetPolicyString(i, parsedPolicyStr);
        MOZ_ASSERT(!parsedPolicyStr.Equals(cspString));
      }
    }
  }
#endif

  // Create a clone of the expanded principal to be used for the call to
  // SetRequestContextWithPrincipal (to prevent the CSP and expanded
  // principal instances to keep each other alive indefinitely, see
  // Bug 1741600).
  //
  // This may not be necessary anymore once Bug 1548468 will move CSP
  // off ExpandedPrincipal.
  RefPtr<ExpandedPrincipal> clonedPrincipal = ExpandedPrincipal::Create(
      expanded->AllowList(), expanded->OriginAttributesRef());
  MOZ_ASSERT(clonedPrincipal);

  csp = new nsCSPContext();
  MOZ_TRY(
      csp->SetRequestContextWithPrincipal(clonedPrincipal, selfURI, ""_ns, 0));

  MOZ_TRY(csp->AppendPolicy(cspString, false, false));

  expanded->SetCsp(csp);
  return NS_OK;
}

nsresult xpc::CreateSandboxObject(JSContext* cx, MutableHandleValue vp,
                                  nsISupports* prinOrSop,
                                  SandboxOptions& options) {
  // Create the sandbox global object
  nsCOMPtr<nsIPrincipal> principal = do_QueryInterface(prinOrSop);
  nsCOMPtr<nsIGlobalObject> obj = do_QueryInterface(prinOrSop);
  if (obj) {
    nsGlobalWindowInner* window =
        WindowOrNull(js::UncheckedUnwrap(obj->GetGlobalJSObject(), false));
    // If we have a secure context window inherit from it's parent
    if (window && window->IsSecureContext()) {
      options.forceSecureContext = true;
    }
  }
  if (!principal) {
    nsCOMPtr<nsIScriptObjectPrincipal> sop = do_QueryInterface(prinOrSop);
    if (sop) {
      principal = sop->GetPrincipal();
    } else {
      RefPtr<NullPrincipal> nullPrin =
          NullPrincipal::CreateWithoutOriginAttributes();
      principal = nullPrin;
    }
  }
  MOZ_ASSERT(principal);

  nsGlobalWindowInner* windowOfProto = nullptr;
  if (options.proto) {
    RootedObject unwrappedProto(cx, js::UncheckedUnwrap(options.proto, false));
    if (principal->Subsumes(nsContentUtils::ObjectPrincipal(unwrappedProto))) {
      windowOfProto = WindowGlobalOrNull(unwrappedProto);
    }
  }

  RefPtr<nsGlobalWindowInner> associatedWindow;
  if (options.associatedWindow) {
    associatedWindow = WindowOrNull(js::UncheckedUnwrap(
        options.associatedWindow, /* stopAtWindowProxy = */ false));
    if (!associatedWindow) {
      JS_ReportErrorASCII(cx, "associatedWindow must be a Window");
      return NS_ERROR_INVALID_ARG;
    }
  }

  JS::RealmOptions realmOptions;

  auto& creationOptions = realmOptions.creationOptions();

  bool isSystemPrincipal = principal->IsSystemPrincipal();

  if (isSystemPrincipal) {
    options.forceSecureContext = true;
  }

  // If we are able to see [SecureContext] API code
  if (options.forceSecureContext) {
    creationOptions.setSecureContext(true);
  }

  xpc::SetPrefableRealmOptions(realmOptions);
  if (!isSystemPrincipal &&
      (!windowOfProto || !windowOfProto->CrossOriginIsolated())) {
    creationOptions.setDefineSharedArrayBufferConstructor(false);
  }
  if (options.sameZoneAs) {
    creationOptions.setNewCompartmentInExistingZone(
        js::UncheckedUnwrap(options.sameZoneAs));
  } else if (options.freshZone) {
    creationOptions.setNewCompartmentAndZone();
  } else if (isSystemPrincipal && !options.invisibleToDebugger &&
             !options.freshCompartment) {
    // Use a shared system compartment for system-principal sandboxes that don't
    // require invisibleToDebugger (this is a compartment property, see bug
    // 1482215).
    creationOptions.setExistingCompartment(xpc::PrivilegedJunkScope());
  } else {
    creationOptions.setNewCompartmentInSystemZone();
  }

  bool freezeBuiltins = isSystemPrincipal;
  if (options.freezeBuiltins.isSome()) {
    freezeBuiltins = options.freezeBuiltins.value();
  }
  if (freezeBuiltins) {
    creationOptions.setFreezeBuiltins(true);
  }

  if (options.alwaysUseFdlibm) {
    creationOptions.setAlwaysUseFdlibm(true);
  }

  creationOptions.setInvisibleToDebugger(options.invisibleToDebugger)
      .setTrace(TraceXPCGlobal);

  realmOptions.behaviors().setDiscardSource(options.discardSource);

  if (isSystemPrincipal) {
    realmOptions.behaviors().setClampAndJitterTime(false);
  }

  if (obj) {
    nsGlobalWindowInner* window =
        WindowOrNull(js::UncheckedUnwrap(obj->GetGlobalJSObject(), false));
    if (window) {
      const nsCString& localeOverride =
          window->GetBrowsingContext()->Top()->GetLanguageOverride();
      if (!localeOverride.IsEmpty()) {
        realmOptions.behaviors().setLocaleOverride(
            PromiseFlatCString(localeOverride).get());
      }

      const nsAString& timezoneOverride =
          window->GetBrowsingContext()->Top()->GetTimezoneOverride();
      if (!timezoneOverride.IsEmpty()) {
        realmOptions.behaviors().setTimeZoneOverride(
            NS_ConvertUTF16toUTF8(timezoneOverride).get());
      }
    }
  }

  const JSClass* clasp = &SandboxClass;

  RootedObject sandbox(
      cx, xpc::CreateGlobalObject(cx, clasp, principal, realmOptions));
  if (!sandbox) {
    return NS_ERROR_FAILURE;
  }

  // Use exclusive expandos for non-system-principal sandboxes.
  bool hasExclusiveExpandos = !isSystemPrincipal;

  // Set up the wantXrays flag, which indicates whether xrays are desired even
  // for same-origin access.
  //
  // This flag has historically been ignored for chrome sandboxes due to
  // quirks in the wrapping implementation that have now been removed. Indeed,
  // same-origin Xrays for chrome->chrome access seems a bit superfluous.
  // Arguably we should just flip the default for chrome and still honor the
  // flag, but such a change would break code in subtle ways for minimal
  // benefit. So we just switch it off here.
  bool wantXrays = AccessCheck::isChrome(sandbox) ? false : options.wantXrays;

  if (creationOptions.compartmentSpecifier() ==
      JS::CompartmentSpecifier::ExistingCompartment) {
    // Make sure the compartment we're reusing has flags that match what we
    // would set on a new compartment.
    CompartmentPrivate* priv = CompartmentPrivate::Get(sandbox);
    MOZ_RELEASE_ASSERT(priv->allowWaivers == options.allowWaivers);
    MOZ_RELEASE_ASSERT(priv->isWebExtensionContentScript ==
                       options.isWebExtensionContentScript);
    MOZ_RELEASE_ASSERT(priv->isUAWidgetCompartment == options.isUAWidgetScope);
    MOZ_RELEASE_ASSERT(priv->hasExclusiveExpandos == hasExclusiveExpandos);
    MOZ_RELEASE_ASSERT(priv->wantXrays == wantXrays);
  } else {
    CompartmentPrivate* priv = CompartmentPrivate::Get(sandbox);
    priv->allowWaivers = options.allowWaivers;
    priv->isWebExtensionContentScript = options.isWebExtensionContentScript;
    priv->isUAWidgetCompartment = options.isUAWidgetScope;
    priv->hasExclusiveExpandos = hasExclusiveExpandos;
    priv->wantXrays = wantXrays;
  }

  {
    JSAutoRealm ar(cx, sandbox);

    // This creates a SandboxPrivate and passes ownership of it to |sandbox|.
    SandboxPrivate::Create(principal, sandbox);

    if (associatedWindow) {
      SandboxPrivate::GetPrivate(sandbox)->SetAssociatedWindow(
          associatedWindow);
    }

    // Ensure |Object.prototype| is instantiated before prototype-
    // splicing below.
    if (!JS::GetRealmObjectPrototype(cx)) {
      return NS_ERROR_XPC_UNEXPECTED;
    }

    if (options.proto) {
      bool ok = JS_WrapObject(cx, &options.proto);
      if (!ok) {
        return NS_ERROR_XPC_UNEXPECTED;
      }

      // Now check what sort of thing we've got in |proto|, and figure out
      // if we need a SandboxProxyHandler.
      //
      // Note that, in the case of a window, we can't require that the
      // Sandbox subsumes the prototype, because we have to hold our
      // reference to it via an outer window, and the window may navigate
      // at any time. So we have to handle that case separately.
      bool useSandboxProxy =
          !!WindowOrNull(js::UncheckedUnwrap(options.proto, false));
      if (!useSandboxProxy) {
        // We just wrapped options.proto into the compartment of whatever Realm
        // is on the cx, so use that same realm for the CheckedUnwrapDynamic
        // call.
        JSObject* unwrappedProto =
            js::CheckedUnwrapDynamic(options.proto, cx, false);
        if (!unwrappedProto) {
          JS_ReportErrorASCII(cx, "Sandbox must subsume sandboxPrototype");
          return NS_ERROR_INVALID_ARG;
        }
        const JSClass* unwrappedClass = JS::GetClass(unwrappedProto);
        useSandboxProxy = unwrappedClass->isWrappedNative() ||
                          mozilla::dom::IsDOMClass(unwrappedClass);
      }

      if (useSandboxProxy) {
        // Wrap it up in a proxy that will do the right thing in terms
        // of this-binding for methods.
        RootedValue priv(cx, ObjectValue(*options.proto));
        options.proto =
            js::NewProxyObject(cx, &sandboxProxyHandler, priv, nullptr);
        if (!options.proto) {
          return NS_ERROR_OUT_OF_MEMORY;
        }
      }

      ok = JS_SetPrototype(cx, sandbox, options.proto);
      if (!ok) {
        return NS_ERROR_XPC_UNEXPECTED;
      }
    }

    bool allowComponents = principal->IsSystemPrincipal();
    if (options.wantComponents && allowComponents) {
      if (!ObjectScope(sandbox)->AttachComponentsObject(cx)) {
        return NS_ERROR_XPC_UNEXPECTED;
      }

      if (!ObjectScope(sandbox)->AttachJSServices(cx)) {
        return NS_ERROR_XPC_UNEXPECTED;
      }
    }

    if (!XPCNativeWrapper::AttachNewConstructorObject(cx, sandbox)) {
      return NS_ERROR_XPC_UNEXPECTED;
    }

    if (!JS_DefineFunctions(cx, sandbox, SandboxFunctions)) {
      return NS_ERROR_XPC_UNEXPECTED;
    }

    if (options.wantExportHelpers &&
        (!JS_DefineFunction(cx, sandbox, "exportFunction",
                            SandboxExportFunction, 3, 0) ||
         !JS_DefineFunction(cx, sandbox, "createObjectIn",
                            SandboxCreateObjectIn, 2, 0) ||
         !JS_DefineFunction(cx, sandbox, "cloneInto", SandboxCloneInto, 3, 0) ||
         !JS_DefineFunction(cx, sandbox, "isProxy", SandboxIsProxy, 1, 0)))
      return NS_ERROR_XPC_UNEXPECTED;

    if (!options.globalProperties.DefineInSandbox(cx, sandbox)) {
      return NS_ERROR_XPC_UNEXPECTED;
    }
  }

  // We handle the case where the context isn't in a compartment for the
  // benefit of UnprivilegedJunkScope().
  vp.setObject(*sandbox);
  if (js::GetContextCompartment(cx) && !JS_WrapValue(cx, vp)) {
    return NS_ERROR_UNEXPECTED;
  }

  // Set the location information for the new global, so that tools like
  // about:memory may use that information
  xpc::SetLocationForGlobal(sandbox, options.sandboxName);

  nsresult rv = xpc::SetSandboxMetadata(cx, sandbox, options.metadata);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  JSAutoRealm ar(cx, sandbox);
  JS_FireOnNewGlobalObject(cx, sandbox);

  return NS_OK;
}

NS_IMETHODIMP
nsXPCComponents_utils_Sandbox::Call(nsIXPConnectWrappedNative* wrapper,
                                    JSContext* cx, JSObject* objArg,
                                    const CallArgs& args, bool* _retval) {
  RootedObject obj(cx, objArg);
  return CallOrConstruct(wrapper, cx, obj, args, _retval);
}

NS_IMETHODIMP
nsXPCComponents_utils_Sandbox::Construct(nsIXPConnectWrappedNative* wrapper,
                                         JSContext* cx, JSObject* objArg,
                                         const CallArgs& args, bool* _retval) {
  RootedObject obj(cx, objArg);
  return CallOrConstruct(wrapper, cx, obj, args, _retval);
}

/*
 * For sandbox constructor the first argument can be a URI string in which case
 * we use the related Content Principal for the sandbox.
 */
bool ParsePrincipal(JSContext* cx, HandleString contentUrl,
                    const OriginAttributes& aAttrs, nsIPrincipal** principal) {
  MOZ_ASSERT(principal);
  MOZ_ASSERT(contentUrl);
  nsCOMPtr<nsIURI> uri;
  nsAutoJSString contentStr;
  NS_ENSURE_TRUE(contentStr.init(cx, contentUrl), false);
  nsresult rv = NS_NewURI(getter_AddRefs(uri), contentStr);
  if (NS_FAILED(rv)) {
    JS_ReportErrorASCII(cx, "Creating URI from string failed");
    return false;
  }

  // We could allow passing in the app-id and browser-element info to the
  // sandbox constructor. But creating a sandbox based on a string is a
  // deprecated API so no need to add features to it.
  nsCOMPtr<nsIPrincipal> prin =
      BasePrincipal::CreateContentPrincipal(uri, aAttrs);
  prin.forget(principal);

  if (!*principal) {
    JS_ReportErrorASCII(cx, "Creating Principal from URI failed");
    return false;
  }
  return true;
}

/*
 * For sandbox constructor the first argument can be a principal object or
 * a script object principal (Document, Window).
 */
static bool GetPrincipalOrSOP(JSContext* cx, HandleObject from,
                              nsISupports** out) {
  MOZ_ASSERT(out);
  *out = nullptr;

  // We might have a Window here, so need ReflectorToISupportsDynamic
  nsCOMPtr<nsISupports> native = ReflectorToISupportsDynamic(from, cx);

  if (nsCOMPtr<nsIScriptObjectPrincipal> sop = do_QueryInterface(native)) {
    sop.forget(out);
    return true;
  }

  nsCOMPtr<nsIPrincipal> principal = do_QueryInterface(native);
  principal.forget(out);
  NS_ENSURE_TRUE(*out, false);

  return true;
}

/*
 * The first parameter of the sandbox constructor might be an array of
 * principals, either in string format or actual objects (see GetPrincipalOrSOP)
 */
static bool GetExpandedPrincipal(JSContext* cx, HandleObject arrayObj,
                                 const SandboxOptions& options,
                                 nsIExpandedPrincipal** out) {
  MOZ_ASSERT(out);
  uint32_t length;

  if (!JS::GetArrayLength(cx, arrayObj, &length)) {
    return false;
  }
  if (!length) {
    // We need a whitelist of principals or uri strings to create an
    // expanded principal, if we got an empty array or something else
    // report error.
    JS_ReportErrorASCII(cx, "Expected an array of URI strings");
    return false;
  }

  nsTArray<nsCOMPtr<nsIPrincipal>> allowedDomains(length);
  allowedDomains.SetLength(length);

  // If an originAttributes option has been specified, we will use that as the
  // OriginAttribute of all of the string arguments passed to this function.
  // Otherwise, we will use the OriginAttributes of a principal or SOP object
  // in the array, if any.  If no such object is present, and all we have are
  // strings, then we will use a default OriginAttribute.
  // Otherwise, we will use the origin attributes of the passed object(s). If
  // more than one object is specified, we ensure that the OAs match.
  Maybe<OriginAttributes> attrs;
  if (options.originAttributes) {
    attrs.emplace();
    JS::RootedValue val(cx, JS::ObjectValue(*options.originAttributes));
    if (!attrs->Init(cx, val)) {
      // The originAttributes option, if specified, must be valid!
      JS_ReportErrorASCII(cx, "Expected a valid OriginAttributes object");
      return false;
    }
  }

  // Now we go over the array in two passes.  In the first pass, we ignore
  // strings, and only process objects.  Assuming that no originAttributes
  // option has been passed, if we encounter a principal or SOP object, we
  // grab its OA and save it if it's the first OA encountered, otherwise
  // check to make sure that it is the same as the OA found before.
  // In the second pass, we ignore objects, and use the OA found in pass 0
  // (or the previously computed OA if we have obtained it from the options)
  // to construct content principals.
  //
  // The effective OA selected above will also be set as the OA of the
  // expanded principal object.

  // First pass:
  for (uint32_t i = 0; i < length; ++i) {
    RootedValue allowed(cx);
    if (!JS_GetElement(cx, arrayObj, i, &allowed)) {
      return false;
    }

    nsCOMPtr<nsIPrincipal> principal;
    if (allowed.isObject()) {
      // In case of object let's see if it's a Principal or a
      // ScriptObjectPrincipal.
      nsCOMPtr<nsISupports> prinOrSop;
      RootedObject obj(cx, &allowed.toObject());
      if (!GetPrincipalOrSOP(cx, obj, getter_AddRefs(prinOrSop))) {
        return false;
      }

      nsCOMPtr<nsIScriptObjectPrincipal> sop(do_QueryInterface(prinOrSop));
      principal = do_QueryInterface(prinOrSop);
      if (sop) {
        principal = sop->GetPrincipal();
      }
      NS_ENSURE_TRUE(principal, false);

      if (!options.originAttributes) {
        const OriginAttributes prinAttrs = principal->OriginAttributesRef();
        if (attrs.isNothing()) {
          attrs.emplace(prinAttrs);
        } else if (prinAttrs != attrs.ref()) {
          // If attrs is from a previously encountered principal in the
          // array, we need to ensure that it matches the OA of the
          // principal we have here.
          // If attrs comes from OriginAttributes, we don't need
          // this check.
          return false;
        }
      }

      // We do not allow ExpandedPrincipals to contain any system principals.
      bool isSystem = principal->IsSystemPrincipal();
      if (isSystem) {
        JS_ReportErrorASCII(
            cx, "System principal is not allowed in an expanded principal");
        return false;
      }
      allowedDomains[i] = principal;
    } else if (allowed.isString()) {
      // Skip any string arguments - we handle them in the next pass.
    } else {
      // Don't know what this is.
      return false;
    }
  }

  if (attrs.isNothing()) {
    // If no OriginAttributes was found in the first pass, fall back to a
    // default one.
    attrs.emplace();
  }

  // Second pass:
  for (uint32_t i = 0; i < length; ++i) {
    RootedValue allowed(cx);
    if (!JS_GetElement(cx, arrayObj, i, &allowed)) {
      return false;
    }

    nsCOMPtr<nsIPrincipal> principal;
    if (allowed.isString()) {
      // In case of string let's try to fetch a content principal from it.
      RootedString str(cx, allowed.toString());

      // attrs here is either a default OriginAttributes in case the
      // originAttributes option isn't specified, and no object in the array
      // provides a principal.  Otherwise it's either the forced principal, or
      // the principal found before, so we can use it here.
      if (!ParsePrincipal(cx, str, attrs.ref(), getter_AddRefs(principal))) {
        return false;
      }
      NS_ENSURE_TRUE(principal, false);
      allowedDomains[i] = principal;
    } else {
      MOZ_ASSERT(allowed.isObject());
    }
  }

  RefPtr<ExpandedPrincipal> result =
      ExpandedPrincipal::Create(allowedDomains, attrs.ref());
  result.forget(out);
  return true;
}

/*
 * Helper that tries to get a property from the options object.
 */
bool OptionsBase::ParseValue(const char* name, MutableHandleValue prop,
                             bool* aFound) {
  bool found;
  bool ok = JS_HasProperty(mCx, mObject, name, &found);
  NS_ENSURE_TRUE(ok, false);

  if (aFound) {
    *aFound = found;
  }

  if (!found) {
    return true;
  }

  return JS_GetProperty(mCx, mObject, name, prop);
}

/*
 * Helper that tries to get a boolean property from the options object.
 */
bool OptionsBase::ParseBoolean(const char* name, bool* prop) {
  MOZ_ASSERT(prop);
  RootedValue value(mCx);
  bool found;
  bool ok = ParseValue(name, &value, &found);
  NS_ENSURE_TRUE(ok, false);

  if (!found) {
    return true;
  }

  if (!value.isBoolean()) {
    JS_ReportErrorASCII(mCx, "Expected a boolean value for property %s", name);
    return false;
  }

  *prop = value.toBoolean();
  return true;
}

/*
 * Helper that tries to get an optional bool property from the options object.
 */
bool OptionsBase::ParseOptionalBoolean(const char* name, Maybe<bool>& prop) {
  RootedValue value(mCx);
  bool found;
  bool ok = ParseValue(name, &value, &found);
  NS_ENSURE_TRUE(ok, false);

  if (!found) {
    return true;
  }

  if (!value.isBoolean()) {
    JS_ReportErrorASCII(mCx, "Expected a boolean value for property %s", name);
    return false;
  }

  prop = Some(value.toBoolean());
  return true;
}

/*
 * Helper that tries to get an object property from the options object.
 */
bool OptionsBase::ParseObject(const char* name, MutableHandleObject prop) {
  RootedValue value(mCx);
  bool found;
  bool ok = ParseValue(name, &value, &found);
  NS_ENSURE_TRUE(ok, false);

  if (!found) {
    return true;
  }

  if (!value.isObject()) {
    JS_ReportErrorASCII(mCx, "Expected an object value for property %s", name);
    return false;
  }
  prop.set(&value.toObject());
  return true;
}

/*
 * Helper that tries to get an object property from the options object.
 */
bool OptionsBase::ParseJSString(const char* name, MutableHandleString prop) {
  RootedValue value(mCx);
  bool found;
  bool ok = ParseValue(name, &value, &found);
  NS_ENSURE_TRUE(ok, false);

  if (!found) {
    return true;
  }

  if (!value.isString()) {
    JS_ReportErrorASCII(mCx, "Expected a string value for property %s", name);
    return false;
  }
  prop.set(value.toString());
  return true;
}

/*
 * Helper that tries to get a string property from the options object.
 */
bool OptionsBase::ParseString(const char* name, nsCString& prop) {
  RootedValue value(mCx);
  bool found;
  bool ok = ParseValue(name, &value, &found);
  NS_ENSURE_TRUE(ok, false);

  if (!found) {
    return true;
  }

  if (!value.isString()) {
    JS_ReportErrorASCII(mCx, "Expected a string value for property %s", name);
    return false;
  }

  JS::UniqueChars tmp = JS_EncodeStringToLatin1(mCx, value.toString());
  NS_ENSURE_TRUE(tmp, false);
  prop.Assign(tmp.get(), strlen(tmp.get()));
  return true;
}

/*
 * Helper that tries to get a string property from the options object.
 */
bool OptionsBase::ParseString(const char* name, nsString& prop) {
  RootedValue value(mCx);
  bool found;
  bool ok = ParseValue(name, &value, &found);
  NS_ENSURE_TRUE(ok, false);

  if (!found) {
    return true;
  }

  if (!value.isString()) {
    JS_ReportErrorASCII(mCx, "Expected a string value for property %s", name);
    return false;
  }

  nsAutoJSString strVal;
  if (!strVal.init(mCx, value.toString())) {
    return false;
  }

  prop = strVal;
  return true;
}

/*
 * Helper that tries to get a string property from the options object.
 */
bool OptionsBase::ParseOptionalString(const char* name, Maybe<nsString>& prop) {
  RootedValue value(mCx);
  bool found;
  bool ok = ParseValue(name, &value, &found);
  NS_ENSURE_TRUE(ok, false);

  if (!found || value.isUndefined()) {
    return true;
  }

  if (!value.isString()) {
    JS_ReportErrorASCII(mCx, "Expected a string value for property %s", name);
    return false;
  }

  nsAutoJSString strVal;
  if (!strVal.init(mCx, value.toString())) {
    return false;
  }

  prop = Some(strVal);
  return true;
}

/*
 * Helper that tries to get jsid property from the options object.
 */
bool OptionsBase::ParseId(const char* name, MutableHandleId prop) {
  RootedValue value(mCx);
  bool found;
  bool ok = ParseValue(name, &value, &found);
  NS_ENSURE_TRUE(ok, false);

  if (!found) {
    return true;
  }

  return JS_ValueToId(mCx, value, prop);
}

/*
 * Helper that tries to get a uint32_t property from the options object.
 */
bool OptionsBase::ParseUInt32(const char* name, uint32_t* prop) {
  MOZ_ASSERT(prop);
  RootedValue value(mCx);
  bool found;
  bool ok = ParseValue(name, &value, &found);
  NS_ENSURE_TRUE(ok, false);

  if (!found) {
    return true;
  }

  if (!JS::ToUint32(mCx, value, prop)) {
    JS_ReportErrorASCII(mCx, "Expected a uint32_t value for property %s", name);
    return false;
  }

  return true;
}

/*
 * Helper that tries to get a list of DOM constructors and other helpers from
 * the options object.
 */
bool SandboxOptions::ParseGlobalProperties() {
  RootedValue value(mCx);
  bool found;
  bool ok = ParseValue("wantGlobalProperties", &value, &found);
  NS_ENSURE_TRUE(ok, false);
  if (!found) {
    return true;
  }

  if (!value.isObject()) {
    JS_ReportErrorASCII(mCx,
                        "Expected an array value for wantGlobalProperties");
    return false;
  }

  RootedObject ctors(mCx, &value.toObject());
  bool isArray;
  if (!JS::IsArrayObject(mCx, ctors, &isArray)) {
    return false;
  }
  if (!isArray) {
    JS_ReportErrorASCII(mCx,
                        "Expected an array value for wantGlobalProperties");
    return false;
  }

  return globalProperties.Parse(mCx, ctors);
}

/*
 * Helper that parsing the sandbox options object (from) and sets the fields of
 * the incoming options struct (options).
 */
bool SandboxOptions::Parse() {
  /* All option names must be ASCII-only. */
  bool ok = ParseObject("sandboxPrototype", &proto) &&
            ParseObject("associatedWindow", &associatedWindow) &&
            ParseBoolean("wantXrays", &wantXrays) &&
            ParseBoolean("allowWaivers", &allowWaivers) &&
            ParseBoolean("wantComponents", &wantComponents) &&
            ParseBoolean("wantExportHelpers", &wantExportHelpers) &&
            ParseBoolean("isWebExtensionContentScript",
                         &isWebExtensionContentScript) &&
            ParseBoolean("forceSecureContext", &forceSecureContext) &&
            ParseOptionalString("sandboxContentSecurityPolicy",
                                sandboxContentSecurityPolicy) &&
            ParseString("sandboxName", sandboxName) &&
            ParseObject("sameZoneAs", &sameZoneAs) &&
            ParseOptionalBoolean("freezeBuiltins", freezeBuiltins) &&
            ParseBoolean("freshCompartment", &freshCompartment) &&
            ParseBoolean("freshZone", &freshZone) &&
            ParseBoolean("invisibleToDebugger", &invisibleToDebugger) &&
            ParseBoolean("discardSource", &discardSource) &&
            ParseGlobalProperties() && ParseValue("metadata", &metadata) &&
            ParseUInt32("userContextId", &userContextId) &&
            ParseObject("originAttributes", &originAttributes) &&
            ParseBoolean("alwaysUseFdlibm", &alwaysUseFdlibm);
  if (!ok) {
    return false;
  }

  if (freshZone && sameZoneAs) {
    JS_ReportErrorASCII(mCx, "Cannot use both sameZoneAs and freshZone");
    return false;
  }

  return true;
}

static nsresult AssembleSandboxMemoryReporterName(JSContext* cx,
                                                  nsCString& sandboxName) {
  // Use a default name when the caller did not provide a sandboxName.
  if (sandboxName.IsEmpty()) {
    sandboxName = "[anonymous sandbox]"_ns;
  } else {
#ifndef DEBUG
    // Adding the caller location is fairly expensive, so in non-debug
    // builds, only add it if we don't have an explicit sandbox name.
    return NS_OK;
#endif
  }

  // Get the xpconnect native call context.
  XPCCallContext* cc = XPCJSContext::Get()->GetCallContext();
  NS_ENSURE_TRUE(cc, NS_ERROR_INVALID_ARG);

  // Get the current source info from xpc.
  nsCOMPtr<nsIStackFrame> frame = dom::GetCurrentJSStack();

  // Append the caller's location information.
  if (frame) {
    nsAutoCString location;
    frame->GetFilename(cx, location);
    int32_t lineNumber = frame->GetLineNumber(cx);

    sandboxName.AppendLiteral(" (from: ");
    sandboxName.Append(location);
    sandboxName.Append(':');
    sandboxName.AppendInt(lineNumber);
    sandboxName.Append(')');
  }

  return NS_OK;
}

// static
nsresult nsXPCComponents_utils_Sandbox::CallOrConstruct(
    nsIXPConnectWrappedNative* wrapper, JSContext* cx, HandleObject obj,
    const CallArgs& args, bool* _retval) {
  if (args.length() < 1) {
    return ThrowAndFail(NS_ERROR_XPC_NOT_ENOUGH_ARGS, cx, _retval);
  }

  nsresult rv;
  bool ok = false;
  bool calledWithOptions = args.length() > 1;
  if (calledWithOptions && !args[1].isObject()) {
    return ThrowAndFail(NS_ERROR_INVALID_ARG, cx, _retval);
  }

  RootedObject optionsObject(cx,
                             calledWithOptions ? &args[1].toObject() : nullptr);

  SandboxOptions options(cx, optionsObject);
  if (calledWithOptions && !options.Parse()) {
    return ThrowAndFail(NS_ERROR_INVALID_ARG, cx, _retval);
  }

  // Make sure to set up principals on the sandbox before initing classes.
  nsCOMPtr<nsIPrincipal> principal;
  nsCOMPtr<nsIExpandedPrincipal> expanded;
  nsCOMPtr<nsISupports> prinOrSop;

  if (args[0].isString()) {
    RootedString str(cx, args[0].toString());
    OriginAttributes attrs;
    if (options.originAttributes) {
      JS::RootedValue val(cx, JS::ObjectValue(*options.originAttributes));
      if (!attrs.Init(cx, val)) {
        // The originAttributes option, if specified, must be valid!
        JS_ReportErrorASCII(cx, "Expected a valid OriginAttributes object");
        return ThrowAndFail(NS_ERROR_INVALID_ARG, cx, _retval);
      }
    }
    attrs.mUserContextId = options.userContextId;
    ok = ParsePrincipal(cx, str, attrs, getter_AddRefs(principal));
    prinOrSop = principal;
  } else if (args[0].isObject()) {
    RootedObject obj(cx, &args[0].toObject());
    bool isArray;
    if (!JS::IsArrayObject(cx, obj, &isArray)) {
      ok = false;
    } else if (isArray) {
      if (options.userContextId != 0) {
        // We don't support passing a userContextId with an array.
        ok = false;
      } else {
        ok = GetExpandedPrincipal(cx, obj, options, getter_AddRefs(expanded));
        prinOrSop = expanded;
      }
    } else {
      ok = GetPrincipalOrSOP(cx, obj, getter_AddRefs(prinOrSop));
    }
  } else if (args[0].isNull()) {
    // Null means that we just pass prinOrSop = nullptr, and get an
    // NullPrincipal.
    ok = true;
  }

  if (!ok) {
    return ThrowAndFail(NS_ERROR_INVALID_ARG, cx, _retval);
  }

  if (options.sandboxContentSecurityPolicy.isSome()) {
    if (!expanded) {
      // CSP is currently stored on ExpandedPrincipal. If CSP moves off
      // ExpandedPrincipal (bug 1548468), then we can drop/relax this check.
      JS_ReportErrorASCII(cx,
                          "sandboxContentSecurityPolicy is currently only "
                          "supported with ExpandedPrincipals");
      return ThrowAndFail(NS_ERROR_INVALID_ARG, cx, _retval);
    }
    rv = SetSandboxCSP(prinOrSop, options.sandboxContentSecurityPolicy.value());
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
  }

  if (NS_FAILED(AssembleSandboxMemoryReporterName(cx, options.sandboxName))) {
    return ThrowAndFail(NS_ERROR_INVALID_ARG, cx, _retval);
  }

  if (options.metadata.isNullOrUndefined()) {
    // If the caller is running in a sandbox, inherit.
    RootedObject callerGlobal(cx, JS::GetScriptedCallerGlobal(cx));
    if (IsSandbox(callerGlobal)) {
      rv = GetSandboxMetadata(cx, callerGlobal, &options.metadata);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }
    }
  }

  rv = CreateSandboxObject(cx, args.rval(), prinOrSop, options);

  if (NS_FAILED(rv)) {
    return ThrowAndFail(rv, cx, _retval);
  }

  *_retval = true;
  return NS_OK;
}

nsresult xpc::EvalInSandbox(JSContext* cx, HandleObject sandboxArg,
                            const nsAString& source, const nsACString& filename,
                            int32_t lineNo, bool enforceFilenameRestrictions,
                            MutableHandleValue rval) {
  JS_AbortIfWrongThread(cx);
  rval.setUndefined();

  bool waiveXray = xpc::WrapperFactory::HasWaiveXrayFlag(sandboxArg);
  // CheckedUnwrapStatic is fine here, since we're checking for "is it a
  // sandbox".
  RootedObject sandbox(cx, js::CheckedUnwrapStatic(sandboxArg));
  if (!sandbox || !IsSandbox(sandbox)) {
    return NS_ERROR_INVALID_ARG;
  }

  SandboxPrivate* priv = SandboxPrivate::GetPrivate(sandbox);
  nsIScriptObjectPrincipal* sop = priv;
  MOZ_ASSERT(sop, "Invalid sandbox passed");
  nsCOMPtr<nsIPrincipal> prin = sop->GetPrincipal();
  NS_ENSURE_TRUE(prin, NS_ERROR_FAILURE);

  nsAutoCString filenameBuf;
  if (!filename.IsVoid() && filename.Length() != 0) {
    filenameBuf.Assign(filename);
  } else {
    // Default to the spec of the principal.
    nsresult rv = nsJSPrincipals::get(prin)->GetScriptLocation(filenameBuf);
    NS_ENSURE_SUCCESS(rv, rv);
    lineNo = 1;
  }

  // We create a separate cx to do the sandbox evaluation. Scope it.
  RootedValue v(cx, UndefinedValue());
  RootedValue exn(cx, UndefinedValue());
  bool ok = true;
  {
    // We're about to evaluate script, so make an AutoEntryScript.
    // This is clearly Gecko-specific and not in any spec.
    mozilla::dom::AutoEntryScript aes(priv, "XPConnect sandbox evaluation");
    JSContext* sandcx = aes.cx();
    JSAutoRealm ar(sandcx, sandbox);

    JS::CompileOptions options(sandcx);
    options.setFileAndLine(filenameBuf.get(), lineNo);
    options.setSkipFilenameValidation(!enforceFilenameRestrictions);
    MOZ_ASSERT(JS_IsGlobalObject(sandbox));

    const nsPromiseFlatString& flat = PromiseFlatString(source);

    JS::SourceText<char16_t> buffer;
    ok = buffer.init(sandcx, flat.get(), flat.Length(),
                     JS::SourceOwnership::Borrowed) &&
         JS::Evaluate(sandcx, options, buffer, &v);

    // If the sandbox threw an exception, grab it off the context.
    if (aes.HasException()) {
      if (!aes.StealException(&exn)) {
        return NS_ERROR_OUT_OF_MEMORY;
      }
    }
  }

  //
  // Alright, we're back on the caller's cx. If an error occured, try to
  // wrap and set the exception. Otherwise, wrap the return value.
  //

  if (!ok) {
    // If we end up without an exception, it was probably due to OOM along
    // the way, in which case we thow. Otherwise, wrap it.
    if (exn.isUndefined() || !JS_WrapValue(cx, &exn)) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    // Set the exception on our caller's cx.
    JS_SetPendingException(cx, exn);
    return NS_ERROR_FAILURE;
  }

  // Transitively apply Xray waivers if |sb| was waived.
  if (waiveXray) {
    ok = xpc::WrapperFactory::WaiveXrayAndWrap(cx, &v);
  } else {
    ok = JS_WrapValue(cx, &v);
  }
  NS_ENSURE_TRUE(ok, NS_ERROR_FAILURE);

  // Whew!
  rval.set(v);
  return NS_OK;
}

nsresult xpc::GetSandboxMetadata(JSContext* cx, HandleObject sandbox,
                                 MutableHandleValue rval) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(IsSandbox(sandbox));

  RootedValue metadata(cx);
  {
    JSAutoRealm ar(cx, sandbox);
    metadata =
        JS::GetReservedSlot(sandbox, XPCONNECT_SANDBOX_CLASS_METADATA_SLOT);
  }

  if (!JS_WrapValue(cx, &metadata)) {
    return NS_ERROR_UNEXPECTED;
  }

  rval.set(metadata);
  return NS_OK;
}

nsresult xpc::SetSandboxLocaleOverride(JSContext* cx, HandleObject sandbox,
                                       const char* locale) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(IsSandbox(sandbox));

  JS::SetRealmLocaleOverride(JS::GetObjectRealmOrNull(sandbox), locale);

  return NS_OK;
}

nsresult xpc::SetSandboxTimezoneOverride(JSContext* cx, HandleObject sandbox,
                                         const char* timezone) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(IsSandbox(sandbox));

  JS::SetRealmTimezoneOverride(JS::GetObjectRealmOrNull(sandbox), timezone);

  return NS_OK;
}

nsresult xpc::SetSandboxMetadata(JSContext* cx, HandleObject sandbox,
                                 HandleValue metadataArg) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(IsSandbox(sandbox));

  RootedValue metadata(cx);

  JSAutoRealm ar(cx, sandbox);
  if (!JS_StructuredClone(cx, metadataArg, &metadata, nullptr, nullptr)) {
    return NS_ERROR_UNEXPECTED;
  }

  JS_SetReservedSlot(sandbox, XPCONNECT_SANDBOX_CLASS_METADATA_SLOT, metadata);

  return NS_OK;
}

SandboxPrivate::SandboxPrivate(nsIPrincipal* principal)
    : mPrincipal(principal),
      mCookieJarSettings(mozilla::net::CookieJarSettings::Create(mPrincipal)) {}

SandboxPrivate::~SandboxPrivate() = default;

void SandboxPrivate::SetAssociatedWindow(nsPIDOMWindowInner* aWindow) {
  mAssociatedWindow = do_GetWeakReference(aWindow);
}

already_AddRefed<nsPIDOMWindowInner> SandboxPrivate::GetAssociatedWindow() {
  nsCOMPtr<nsPIDOMWindowInner> window = do_QueryReferent(mAssociatedWindow);
  return window.forget();
}

ModuleLoaderBase* SandboxPrivate::GetModuleLoader(JSContext* aCx) {
  if (mModuleLoader) {
    return mModuleLoader;
  }

  JSObject* object = GetGlobalJSObject();
  nsGlobalWindowInner* sandboxWindow = xpc::SandboxWindowOrNull(object, aCx);
  if (!sandboxWindow) {
    return nullptr;
  }

  ModuleLoader* mainModuleLoader =
      static_cast<ModuleLoader*>(sandboxWindow->GetModuleLoader(aCx));

  ScriptLoader* scriptLoader = mainModuleLoader->GetScriptLoader();

  ModuleLoader* moduleLoader =
      new ModuleLoader(scriptLoader, this, ModuleLoader::WebExtension);
  scriptLoader->RegisterContentScriptModuleLoader(moduleLoader);
  mModuleLoader = moduleLoader;

  return moduleLoader;
}

mozilla::Result<mozilla::ipc::PrincipalInfo, nsresult>
SandboxPrivate::GetStorageKey() {
  MOZ_ASSERT(NS_IsMainThread());

  mozilla::ipc::PrincipalInfo principalInfo;
  nsresult rv = PrincipalToPrincipalInfo(mPrincipal, &principalInfo);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return mozilla::Err(rv);
  }

  // Block expanded and null principals, let content and system through.
  if (principalInfo.type() !=
          mozilla::ipc::PrincipalInfo::TContentPrincipalInfo &&
      principalInfo.type() !=
          mozilla::ipc::PrincipalInfo::TSystemPrincipalInfo) {
    return Err(NS_ERROR_DOM_SECURITY_ERR);
  }

  return std::move(principalInfo);
}
