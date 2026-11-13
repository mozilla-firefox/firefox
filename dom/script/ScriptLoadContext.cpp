/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ScriptLoadContext.h"

#include "GeckoProfiler.h"
#include "ModuleLoadRequest.h"
#include "js/SourceText.h"
#include "js/loader/LoadContextBase.h"
#include "js/loader/ModuleLoadRequest.h"
#include "mozilla/HoldDropJSObjects.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/Utf8.h"  // mozilla::Utf8Unit
#include "mozilla/dom/Document.h"
#include "nsContentUtils.h"
#include "nsICacheInfoChannel.h"
#include "nsIClassOfService.h"
#include "nsISupportsPriority.h"

namespace mozilla::dom {

//////////////////////////////////////////////////////////////
// ScriptLoadContext
//////////////////////////////////////////////////////////////

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(ScriptLoadContext)
NS_INTERFACE_MAP_END_INHERITING(JS::loader::LoadContextBase)

NS_IMPL_CYCLE_COLLECTION_CLASS(ScriptLoadContext)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(ScriptLoadContext,
                                                JS::loader::LoadContextBase)
  tmp->MaybeCancelOffThreadScript();
  tmp->MaybeUnblockOnload();
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mScriptElement);
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(ScriptLoadContext,
                                                  JS::loader::LoadContextBase)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mLoadBlockedDocument)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mScriptElement);
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_ADDREF_INHERITED(ScriptLoadContext, JS::loader::LoadContextBase)
NS_IMPL_RELEASE_INHERITED(ScriptLoadContext, JS::loader::LoadContextBase)

ScriptLoadContext::ScriptLoadContext(
    nsIScriptElement* aScriptElement /* = nullptr */,
    const nsAString& aSourceText /* = VoidString() */)
    : JS::loader::LoadContextBase(JS::loader::ContextKind::Window),
      mScriptMode(ScriptMode::eBlocking),
      mScriptFromHead(false),
      mIsInline(true),
      mInDeferList(false),
      mInAsyncList(false),
      mIsNonAsyncScriptInserted(false),
      mIsXSLT(false),
      mInCompilingList(false),
      mWasCompiledOMT(false),
      mIsPreload(false),
      mIsCoalescedModulePreload(false),
      mWasAbortedByNavigation(false),
      mUnreportedPreloadError(NS_OK),
      mLineNo(1),
      mColumnNo(0),
      mClassificationFlags({0, 0}),
      mScriptElement(aScriptElement),
      mSourceText(aSourceText) {}

ScriptLoadContext::~ScriptLoadContext() {
  MOZ_ASSERT(NS_IsMainThread());

  // A request can be abandoned after off-thread compilation completes but
  // before execution steals the result.
  MaybeCancelOffThreadScript();
  MOZ_DIAGNOSTIC_ASSERT(!mCompileOrDecodeTask);

  mRequest = nullptr;

  MaybeUnblockOnload();
}

void ScriptLoadContext::BlockOnload(Document* aDocument) {
  MOZ_ASSERT(!mLoadBlockedDocument);
  aDocument->BlockOnload();
  mLoadBlockedDocument = aDocument;
}

void ScriptLoadContext::MaybeUnblockOnload() {
  if (mLoadBlockedDocument) {
    mLoadBlockedDocument->UnblockOnload(false);
    mLoadBlockedDocument = nullptr;
  }
}

void ScriptLoadContext::NotifyPreloadCoalescingResult() {
  MOZ_ASSERT(mIsCoalescedModulePreload);

  if (HasStopped()) {
    return;
  }

  MOZ_ASSERT(!Channel());

  JS::loader::ModuleLoadRequest* request = mRequest->AsModuleRequest();
  MOZ_ASSERT(request->IsTopLevel());

  if (request->mModuleScript) {
    // Fetching produced a module script, even if it has a parse error.
    NotifyStop(NS_OK);
    MOZ_ASSERT(HasStopped());
  } else if (request->IsFinished()) {
    // The fetch failed, or the request was canceled before it finished. Either
    // way there will be no module script, so this is the element's last chance
    // to hear about the load.
    NotifyStop(NS_ERROR_FAILURE);
    MOZ_ASSERT(HasStopped());
  } else {
    // The top-level module is still being fetched.
    // The result of the fetch will be notified via
    // ModuleLoaderBase::ResumeWaitingRequests or Cancel.
    MOZ_ASSERT(request->IsFetching());
  }
}

void ScriptLoadContext::MaybeCancelOffThreadScript() {
  MOZ_ASSERT(NS_IsMainThread());

  if (!mCompileOrDecodeTask) {
    return;
  }

  // Nulling the task makes OffThreadCompilationCompleteTask discard the result.
  mCompileOrDecodeTask->Cancel();
  mCompileOrDecodeTask = nullptr;

  MaybeUnblockOnload();
}

void ScriptLoadContext::SetScriptMode(bool aDeferAttr, bool aAsyncAttr,
                                      bool aLinkPreload) {
  if (aLinkPreload) {
    mScriptMode = ScriptMode::eLinkPreload;
  } else if (aAsyncAttr) {
    mScriptMode = ScriptMode::eAsync;
  } else if (aDeferAttr || mRequest->IsModuleRequest()) {
    mScriptMode = ScriptMode::eDeferred;
  } else {
    mScriptMode = ScriptMode::eBlocking;
  }
}

// static
void ScriptLoadContext::PrioritizeAsPreload(nsIChannel* aChannel) {
  if (nsCOMPtr<nsIClassOfService> cos = do_QueryInterface(aChannel)) {
    cos->AddClassFlags(nsIClassOfService::Unblocked);
  }
  if (nsCOMPtr<nsISupportsPriority> sp = do_QueryInterface(aChannel)) {
    sp->AdjustPriority(nsISupportsPriority::PRIORITY_HIGHEST);
  }
}

bool ScriptLoadContext::IsPreload() const {
  if (mRequest->IsModuleRequest() &&
      mRequest->AsModuleRequest()->IsStaticImport()) {
    JS::loader::ModuleLoadRequest* root =
        mRequest->AsModuleRequest()->GetRootModule();
    return root->GetScriptLoadContext()->IsPreload();
  }

  MOZ_ASSERT_IF(mIsPreload, !HasScriptElement());
  return mIsPreload;
}

bool ScriptLoadContext::CompileStarted() const {
  return mRequest->IsCompiling() || (mRequest->IsFinished() && mWasCompiledOMT);
}

bool ScriptLoadContext::HasScriptElement() const { return !!mScriptElement; }

void ScriptLoadContext::GetInlineScriptText(nsAString& aText) const {
  MOZ_ASSERT(mIsInline);
  if (mSourceText.IsVoid()) {
    // Lazily retrieve the text of inline script, see bug 1376651.
    mScriptElement->GetScriptText(aText);
  } else {
    aText.Append(mSourceText);
  }
}

void ScriptLoadContext::GetHintCharset(nsAString& aCharset) const {
  MOZ_ASSERT(mScriptElement);
  mScriptElement->GetScriptCharset(aCharset);
}

uint32_t ScriptLoadContext::GetScriptLineNumber() const {
  if (mScriptElement) {
    return mScriptElement->GetScriptLineNumber();
  }
  return 0;
}

JS::ColumnNumberOneOrigin ScriptLoadContext::GetScriptColumnNumber() const {
  if (mScriptElement) {
    return mScriptElement->GetScriptColumnNumber();
  }
  return JS::ColumnNumberOneOrigin();
}

void ScriptLoadContext::BeginEvaluatingTopLevel() const {
  MOZ_ASSERT(mScriptElement);
  mScriptElement->BeginEvaluating();
}

void ScriptLoadContext::EndEvaluatingTopLevel() const {
  MOZ_ASSERT(mScriptElement);
  mScriptElement->EndEvaluating();
}

void ScriptLoadContext::UnblockParser() const {
  MOZ_ASSERT(mScriptElement);
  mScriptElement->UnblockParser();
}

void ScriptLoadContext::ContinueParserAsync() const {
  MOZ_ASSERT(mScriptElement);
  mScriptElement->ContinueParserAsync();
}

Document* ScriptLoadContext::GetScriptOwnerDocument() const {
  nsCOMPtr<nsIContent> scriptContent(do_QueryInterface(mScriptElement));
  MOZ_ASSERT(scriptContent);
  return scriptContent->OwnerDoc();
}

void ScriptLoadContext::SetIsLoadRequest(nsIScriptElement* aElement) {
  MOZ_ASSERT(aElement);
  MOZ_ASSERT(!HasScriptElement());
  MOZ_ASSERT(IsPreload());
  mScriptElement = aElement;
  mIsPreload = false;
}

void ScriptLoadContext::GetProfilerLabel(nsACString& aOutString) {
  if (!profiler_is_active()) {
    aOutString.Append("<script> element");
    return;
  }
  aOutString.Append("<script");
  if (IsAsyncScript()) {
    aOutString.Append(" async");
  } else if (IsDeferredScript()) {
    aOutString.Append(" defer");
  }
  if (mRequest->IsModuleRequest()) {
    aOutString.Append(" type=\"module\"");
  }

  nsAutoCString url;
  if (mRequest->URI()) {
    mRequest->URI()->GetAsciiSpec(url);
  } else {
    url = "<unknown>";
  }

  if (mIsInline) {
    if (GetParserCreated() != NOT_FROM_PARSER) {
      aOutString.Append("> inline at line ");
      aOutString.AppendInt(mLineNo);
      aOutString.Append(" of ");
    } else {
      aOutString.Append("> inline (dynamically created) in ");
    }
    aOutString.Append(url);
  } else {
    aOutString.Append(" src=\"");
    aOutString.Append(url);
    aOutString.Append("\">");
  }
}

already_AddRefed<JS::Stencil> ScriptLoadContext::StealOffThreadResult(
    JSContext* aCx, JS::InstantiationStorage* aInstantiationStorage) {
  RefPtr<CompileOrDecodeTask> compileOrDecodeTask =
      mCompileOrDecodeTask.forget();

  StencilCompileOrDecodeTask* task =
      compileOrDecodeTask->AsStencilCompileOrDecodeTask();
  RefPtr<JS::Stencil> stencil = task->StealResult(aCx, aInstantiationStorage);

  if (mRequest->IsRetrievedAsSerializedStencil()) {
    mRequest->RestoreSRIAndSerializedStencil(
        task->TakeSRIAndSerializedStencil());
  }

  return stencil.forget();
}

bool ScriptLoadContext::StealOffThreadWasmResult(
    JSContext* aCx, JS::MutableHandle<JSObject*> aModuleOut) {
  RefPtr<CompileOrDecodeTask> compileOrDecodeTask =
      mCompileOrDecodeTask.forget();

  return compileOrDecodeTask->AsWasmCompileTask()->StealResult(aCx, aModuleOut);
}

}  // namespace mozilla::dom
