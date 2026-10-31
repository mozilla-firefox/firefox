/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ModuleLoadRequest.h"

#include "mozilla/DebugOnly.h"
#include "mozilla/dom/ScriptLoadContext.h"
#include "mozilla/HoldDropJSObjects.h"

#include "LoadContextBase.h"
#include "LoadedScript.h"
#include "ModuleLoaderBase.h"

#include "js/Modules.h"

namespace JS::loader {

#undef LOG
#define LOG(args)                                                           \
  MOZ_LOG(ModuleLoaderBase::gModuleLoaderBaseLog, mozilla::LogLevel::Debug, \
          args)

NS_IMPL_ISUPPORTS_CYCLE_COLLECTION_INHERITED_0(ModuleLoadRequest,
                                               ScriptLoadRequest)

NS_IMPL_CYCLE_COLLECTION_CLASS(ModuleLoadRequest)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(ModuleLoadRequest,
                                                ScriptLoadRequest)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mLoader, mRootModule, mModuleScript)
  tmp->ClearImport();
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(ModuleLoadRequest,
                                                  ScriptLoadRequest)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mLoader, mRootModule, mModuleScript)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN_INHERITED(ModuleLoadRequest,
                                               ScriptLoadRequest)
  NS_IMPL_CYCLE_COLLECTION_TRACE_JS_MEMBER_CALLBACK(mReferrerValue)
  NS_IMPL_CYCLE_COLLECTION_TRACE_JS_MEMBER_CALLBACK(mModuleRequestObj)
  NS_IMPL_CYCLE_COLLECTION_TRACE_JS_MEMBER_CALLBACK(mPayload)
NS_IMPL_CYCLE_COLLECTION_TRACE_END

ModuleLoadRequest::ModuleLoadRequest(
    ModuleType aModuleType, const mozilla::dom::SRIMetadata& aIntegrity,
    nsIURI* aReferrer, LoadContextBase* aContext, Kind aKind,
    ModuleLoaderBase* aLoader, ModuleLoadRequest* aRootModule)
    : ScriptLoadRequest(ScriptKind::eModule, aIntegrity, aReferrer, aContext,
                        nullptr),
      mKind(aKind),
      mModuleType(aModuleType),
      mIsDynamicImport(aKind == Kind::DynamicImport),
      mErroredLoadingImports(false),
      mLoader(aLoader),
      mRootModule(aRootModule) {
  MOZ_ASSERT(mLoader);
}

ModuleLoadRequest::~ModuleLoadRequest() {
  MOZ_ASSERT(mReferrerValue.isUndefined());
  MOZ_ASSERT(!mModuleRequestObj);
  MOZ_ASSERT(mPayload.isUndefined());

  DropJSObjects(this);
}

nsIGlobalObject* ModuleLoadRequest::GetGlobalObject() {
  return mLoader->GetGlobalObject();
}

bool ModuleLoadRequest::IsSourcePhaseRequest() const {
  // moduleRequest can be null if ClearImport() has been called.
  return mModuleRequestObj && JS::ModuleRequestIsSourcePhase(mModuleRequestObj);
}

bool ModuleLoadRequest::IsErrored() const {
  return !mModuleScript || mModuleScript->HasParseError();
}

void ModuleLoadRequest::SetReady() {
  MOZ_ASSERT(!IsFinished());

  // Mark a module as ready to execute. This means that this module and all it
  // dependencies have had their source loaded, parsed as a module and the
  // modules instantiated.

  ScriptLoadRequest::SetReady();
}

void ModuleLoadRequest::ModuleLoaded() {
  // A module that was found to be marked as fetching in the module map has now
  // been loaded.

  LOG(("ScriptLoadRequest (%p): Module loaded", this));

  if (IsCanceled()) {
    return;
  }

  MOZ_ASSERT(IsFetching());

  mModuleScript = mLoader->GetFetchedModule(ModuleMapKey(URI(), mModuleType));

  // A module script fetched during preload can be reused by a normal load whose
  // top-level request never matched a preload entry, so the preload-promotion
  // path never clears the module script's preload flag. Clear it here so the
  // shared module script reflects that it is now part of a normal load.
  MOZ_ASSERT(mModuleScript);
  if (!mLoadContext->IsPreload() && mModuleScript->ForPreload()) {
    mModuleScript->SetForPreload(false);
  }
}

void ModuleLoadRequest::ModuleErrored() {
  // Parse error, failure to resolve imported modules or error loading import.

  LOG(("ScriptLoadRequest (%p): Module errored", this));

  if (IsCanceled()) {
    return;
  }

  MOZ_ASSERT(!IsFinished());

  // Although the “error to rethrow” is only updated during static imports, a
  // module loaded via a dynamic import may have had its module script
  // previously fetched by a top-level module load or a static import, which
  // would have already set the “error to rethrow”. Therefore, if hasRethrow is
  // true, we do not assert that this request originates from a static import
  // or a top-level module load.
  mozilla::DebugOnly<bool> hasRethrow =
      mModuleScript && mModuleScript->HasErrorToRethrow();

  // When LoadRequestedModules fails, we will set error to rethrow to the module
  // script or call SetErroredLoadingImports() and then call ModuleErrored().
  MOZ_ASSERT(IsErrored() || hasRethrow || mErroredLoadingImports);

  if (IsFinished()) {
    // Cancelling an outstanding import will error this request.
    return;
  }

  SetReady();
  LoadFinished();
}

void ModuleLoadRequest::LoadFinished() {
  RefPtr<ModuleLoadRequest> request(this);
  if (IsDynamicImport()) {
    mLoader->RemoveDynamicImport(request);
  }

  mLoader->OnModuleLoadComplete(request);
}

void ModuleLoadRequest::NotifyModuleWaitFinished() {
  if (HasScriptLoadContext()) {
    GetScriptLoadContext()->NotifyModuleWaitFinished();
  }
}

void ModuleLoadRequest::SetImport(Handle<Value> aReferrer,
                                  Handle<JSObject*> aModuleRequestObj,
                                  Handle<Value> aPayload) {
  MOZ_ASSERT(mPayload.isUndefined());

  mReferrerValue = aReferrer;
  mModuleRequestObj = aModuleRequestObj;
  mPayload = aPayload;

  mozilla::HoldJSObjects(this);
}

void ModuleLoadRequest::ClearImport() {
  mReferrerValue = UndefinedValue();
  mModuleRequestObj = nullptr;
  mPayload = UndefinedValue();
}

}  // namespace JS::loader
