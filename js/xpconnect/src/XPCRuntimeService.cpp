/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/IndexedDatabaseManager.h"
#include "mozilla/dom/WebIDLGlobalNameHash.h"
#include "mozilla/ipc/BackgroundUtils.h"
#include "mozilla/ipc/PBackgroundSharedTypes.h"
#include "mozilla/net/CookieJarSettings.h"
#include "mozilla/Result.h"

#include "nsContentUtils.h"
#include "SystemGlobal.h"
#include "xpc_make_class.h"
#include "xpcprivate.h"

#include "js/loader/ModuleLoaderBase.h"

using namespace mozilla::dom;

NS_IMPL_ISUPPORTS(SystemGlobal, nsIXPCScriptable, nsIGlobalObject, nsIClassInfo,
                  nsIScriptObjectPrincipal, nsISupportsWeakReference)

SystemGlobal::SystemGlobal()
    : mAgentClusterId(nsID::GenerateUUID()),
      mPrincipal(nsContentUtils::GetSystemPrincipal()),
      mCookieJarSettings(mozilla::net::CookieJarSettings::Create(mPrincipal)),
      mWrapper(nullptr) {}

SystemGlobal::~SystemGlobal() = default;

void SystemGlobal::InitModuleLoader(
    JS::loader::ModuleLoaderBase* aModuleLoader) {
  MOZ_ASSERT(!mModuleLoader);
  mModuleLoader = aModuleLoader;
}
// XXX(nika): It appears we don't have support for mayresolve hooks in
// nsIXPCScriptable, and I don't really want to add it because I'd rather just
// kill nsIXPCScriptable alltogether, so we don't use it here.

// The nsIXPCScriptable map declaration that will generate stubs for us...
#define XPC_MAP_CLASSNAME SystemGlobal
#define XPC_MAP_QUOTED_CLASSNAME "SystemGlobal"
#define XPC_MAP_FLAGS                                               \
  (XPC_SCRIPTABLE_WANT_RESOLVE | XPC_SCRIPTABLE_WANT_NEWENUMERATE | \
   XPC_SCRIPTABLE_WANT_FINALIZE | XPC_SCRIPTABLE_WANT_PRECREATE |   \
   XPC_SCRIPTABLE_USE_JSSTUB_FOR_ADDPROPERTY |                      \
   XPC_SCRIPTABLE_USE_JSSTUB_FOR_DELPROPERTY |                      \
   XPC_SCRIPTABLE_DONT_ENUM_QUERY_INTERFACE |                       \
   XPC_SCRIPTABLE_IS_GLOBAL_OBJECT |                                \
   XPC_SCRIPTABLE_DONT_REFLECT_INTERFACE_NAMES)
#include "xpc_map_end.h" /* This will #undef the above */

JSObject* SystemGlobal::GetGlobalJSObject() {
  if (mWrapper) {
    return mWrapper->GetFlatJSObject();
  }
  return nullptr;
}

JSObject* SystemGlobal::GetGlobalJSObjectPreserveColor() const {
  if (mWrapper) {
    return mWrapper->GetFlatJSObjectPreserveColor();
  }
  return nullptr;
}

void SystemGlobal::SetGlobalObject(JSObject* global) {
  nsISupports* p = XPCWrappedNative::Get(global);
  MOZ_ASSERT(p);
  mWrapper = static_cast<XPCWrappedNative*>(p);
}

NS_IMETHODIMP
SystemGlobal::Resolve(nsIXPConnectWrappedNative* wrapper, JSContext* cx,
                      JSObject* objArg, jsid idArg, bool* resolvedp,
                      bool* _retval) {
  JS::RootedObject obj(cx, objArg);
  JS::RootedId id(cx, idArg);
  *_retval =
      WebIDLGlobalNameHash::ResolveForSystemGlobal(cx, obj, id, resolvedp);
  if (!*_retval) {
    return NS_ERROR_FAILURE;
  }

  if (*resolvedp) {
    return NS_OK;
  }

  XPCJSContext* xpccx = XPCJSContext::Get();
  if (id == xpccx->GetStringID(XPCJSContext::IDX_FETCH)) {
    *_retval = xpc::SandboxCreateFetch(cx, obj);
    if (!*_retval) {
      return NS_ERROR_FAILURE;
    }
    *resolvedp = true;
  } else if (id == xpccx->GetStringID(XPCJSContext::IDX_CRYPTO)) {
    *_retval = xpc::SandboxCreateCrypto(cx, obj);
    if (!*_retval) {
      return NS_ERROR_FAILURE;
    }
    *resolvedp = true;
  } else if (id == xpccx->GetStringID(XPCJSContext::IDX_INDEXEDDB)) {
    *_retval = IndexedDatabaseManager::DefineIndexedDB(cx, obj);
    if (!*_retval) {
      return NS_ERROR_FAILURE;
    }
    *resolvedp = true;
  } else if (id == xpccx->GetStringID(XPCJSContext::IDX_STRUCTUREDCLONE)) {
    *_retval = xpc::SandboxCreateStructuredClone(cx, obj);
    if (!*_retval) {
      return NS_ERROR_FAILURE;
    }
    *resolvedp = true;
  } else if (id == xpccx->GetStringID(XPCJSContext::IDX_LOCKS)) {
    *_retval = xpc::SandboxCreateLocks(cx, obj);
    if (!*_retval) {
      return NS_ERROR_FAILURE;
    }
    *resolvedp = true;
  }

  return NS_OK;
}

NS_IMETHODIMP
SystemGlobal::NewEnumerate(nsIXPConnectWrappedNative* wrapper, JSContext* cx,
                           JSObject* objArg,
                           JS::MutableHandleIdVector properties,
                           bool enumerableOnly, bool* _retval) {
  JS::RootedObject obj(cx, objArg);

  XPCJSContext* xpccx = XPCJSContext::Get();
  if (!properties.append(xpccx->GetStringID(XPCJSContext::IDX_FETCH)) ||
      !properties.append(xpccx->GetStringID(XPCJSContext::IDX_CRYPTO)) ||
      !properties.append(xpccx->GetStringID(XPCJSContext::IDX_INDEXEDDB)) ||
      !properties.append(
          xpccx->GetStringID(XPCJSContext::IDX_STRUCTUREDCLONE)) ||
      !properties.append(xpccx->GetStringID(XPCJSContext::IDX_LOCKS))) {
    return NS_ERROR_FAILURE;
  }

  *_retval = WebIDLGlobalNameHash::NewEnumerateSystemGlobal(cx, obj, properties,
                                                            enumerableOnly);
  return *_retval ? NS_OK : NS_ERROR_FAILURE;
}

/***************************************************************************/
NS_IMETHODIMP
SystemGlobal::GetInterfaces(nsTArray<nsIID>& aArray) {
  aArray = nsTArray<nsIID>{NS_GET_IID(nsIXPCScriptable),
                           NS_GET_IID(nsIScriptObjectPrincipal)};
  return NS_OK;
}

NS_IMETHODIMP
SystemGlobal::GetScriptableHelper(nsIXPCScriptable** retval) {
  nsCOMPtr<nsIXPCScriptable> scriptable = this;
  scriptable.forget(retval);
  return NS_OK;
}

NS_IMETHODIMP
SystemGlobal::GetContractID(nsACString& aContractID) {
  aContractID.SetIsVoid(true);
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
SystemGlobal::GetClassDescription(nsACString& aClassDescription) {
  aClassDescription.AssignLiteral("SystemGlobal");
  return NS_OK;
}

NS_IMETHODIMP
SystemGlobal::GetClassID(nsCID** aClassID) {
  *aClassID = nullptr;
  return NS_OK;
}

NS_IMETHODIMP
SystemGlobal::GetFlags(uint32_t* aFlags) {
  *aFlags = 0;
  return NS_OK;
}

NS_IMETHODIMP
SystemGlobal::GetClassIDNoAlloc(nsCID* aClassIDNoAlloc) {
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
SystemGlobal::Finalize(nsIXPConnectWrappedNative* wrapper, JS::GCContext* gcx,
                       JSObject* obj) {
  nsCOMPtr<nsIGlobalObject> bsp(do_QueryInterface(wrapper->Native()));
  MOZ_ASSERT(bsp);
  static_cast<SystemGlobal*>(bsp.get())->ForgetGlobalObject();
  return NS_OK;
}

NS_IMETHODIMP
SystemGlobal::PreCreate(nsISupports* nativeObj, JSContext* cx,
                        JSObject* globalObj, JSObject** parentObj) {
  // We do the same trick here as for WindowSH. Return the js global
  // as parent, so XPConenct can find the right scope and the wrapper
  // that already exists.
  nsCOMPtr<nsIGlobalObject> global(do_QueryInterface(nativeObj));
  MOZ_ASSERT(global, "nativeObj not a global object!");

  JSObject* jsglobal = global->GetGlobalJSObject();
  if (jsglobal) {
    *parentObj = jsglobal;
  }
  return NS_OK;
}

mozilla::Result<mozilla::ipc::PrincipalInfo, nsresult>
SystemGlobal::GetStorageKey() {
  MOZ_ASSERT(NS_IsMainThread());

  mozilla::ipc::PrincipalInfo principalInfo;
  nsresult rv = PrincipalToPrincipalInfo(mPrincipal, &principalInfo);
  if (NS_FAILED(rv)) {
    return mozilla::Err(rv);
  }

  MOZ_ASSERT(principalInfo.type() ==
             mozilla::ipc::PrincipalInfo::TSystemPrincipalInfo);

  return std::move(principalInfo);
}
