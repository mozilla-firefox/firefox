/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DocumentL10n.h"

#include "mozilla/ScopeExit.h"
#include "mozilla/dom/AutoEntryScript.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentL10nBinding.h"
#include "nsContentUtils.h"
#include "nsIContentSink.h"

using namespace mozilla;
using namespace mozilla::intl;
using namespace mozilla::dom;

NS_IMPL_CYCLE_COLLECTION_CLASS(DocumentL10n)
NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(DocumentL10n, DOMLocalization)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mDocument)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mReady)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mContentSink)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_UNLINK_END
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(DocumentL10n, DOMLocalization)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mDocument)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mReady)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mContentSink)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_ADDREF_INHERITED(DocumentL10n, DOMLocalization)
NS_IMPL_RELEASE_INHERITED(DocumentL10n, DOMLocalization)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(DocumentL10n)
NS_INTERFACE_MAP_END_INHERITING(DOMLocalization)

/* static */
RefPtr<DocumentL10n> DocumentL10n::Create(Document* aDocument, bool aSync) {
  RefPtr<DocumentL10n> l10n = new DocumentL10n(aDocument, aSync);

  IgnoredErrorResult rv;
  l10n->mReady = Promise::Create(l10n->mGlobal, rv);
  if (NS_WARN_IF(rv.Failed())) {
    return nullptr;
  }

  return l10n.forget();
}

RefPtr<DocumentL10n> DocumentL10n::Create(Document* aDocument, bool aSync,
                                          const nsTArray<nsCString>& aLocales) {
  RefPtr<DocumentL10n> l10n = new DocumentL10n(aDocument, aSync, aLocales);

  IgnoredErrorResult rv;
  l10n->mReady = Promise::Create(l10n->mGlobal, rv);
  if (NS_WARN_IF(rv.Failed())) {
    return nullptr;
  }

  return l10n.forget();
}

DocumentL10n::DocumentL10n(Document* aDocument, bool aSync)
    : DOMLocalization(aDocument->GetScopeObject(), aSync),
      mDocument(aDocument),
      mState(DocumentL10nState::Constructed) {
  mContentSink = do_QueryInterface(aDocument->GetCurrentContentSink());
  mIsDocumentL10n = true;
}

DocumentL10n::DocumentL10n(Document* aDocument, bool aSync,
                           const nsTArray<nsCString>& aLocales)
    : DOMLocalization(aDocument->GetScopeObject(), aSync, aLocales),
      mDocument(aDocument),
      mState(DocumentL10nState::Constructed) {
  mContentSink = do_QueryInterface(aDocument->GetCurrentContentSink());
}

JSObject* DocumentL10n::WrapObject(JSContext* aCx,
                                   JS::Handle<JSObject*> aGivenProto) {
  return DocumentL10n_Binding::Wrap(aCx, this, aGivenProto);
}

class L10nReadyHandler final : public PromiseNativeHandler {
 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS_FINAL
  NS_DECL_CYCLE_COLLECTION_CLASS(L10nReadyHandler)

  explicit L10nReadyHandler(Promise* aPromise, DocumentL10n* aDocumentL10n)
      : mPromise(aPromise), mDocumentL10n(aDocumentL10n) {}

  void ResolvedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                        ErrorResult& aRv) override {
    mDocumentL10n->InitialTranslationCompleted();
    mPromise->MaybeResolveWithUndefined();
  }

  void RejectedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                        ErrorResult& aRv) override {
    mDocumentL10n->InitialTranslationCompleted();

    nsTArray<nsCString> errors{
        "[dom/l10n] Could not complete initial document translation."_ns,
    };
    IgnoredErrorResult rv;
    MaybeReportErrorsToGecko(errors, rv, mDocumentL10n->GetParentObject());

    /**
     * We resolve the mReady here even if we encountered failures, because
     * there is nothing actionable for the user pending on `mReady` to do here
     * and we don't want to incentivized consumers of this API to plan the
     * same pending operation for resolve and reject scenario.
     *
     * Additionally, without it, the stderr received "uncaught promise
     * rejection" warning, which is noisy and not-actionable.
     *
     * So instead, we just resolve and report errors.
     *
     * However, in automated tests we do reject so that errors with missing
     * messages and resources can be caught.
     */
    if (xpc::IsInAutomation()) {
      mPromise->MaybeRejectWithClone(aCx, aValue);
    } else {
      mPromise->MaybeResolveWithUndefined();
    }
  }

 private:
  ~L10nReadyHandler() = default;

  RefPtr<Promise> mPromise;
  RefPtr<DocumentL10n> mDocumentL10n;
};

NS_IMPL_CYCLE_COLLECTION(L10nReadyHandler, mPromise, mDocumentL10n)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(L10nReadyHandler)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(L10nReadyHandler)
NS_IMPL_CYCLE_COLLECTING_RELEASE(L10nReadyHandler)

void DocumentL10n::TriggerInitialTranslation() {
  MOZ_ASSERT(nsContentUtils::IsSafeToRunScript());
  if (mState >= DocumentL10nState::InitialTranslationTriggered) {
    return;
  }
  if (!mReady) {
    // If we don't have `mReady` it means that we are in shutdown mode.
    // See bug 1687118 for details.
    InitialTranslationCompleted();
    return;
  }

  auto errorExit = MakeScopeExit([&] {
    InitialTranslationCompleted();
    mReady->MaybeRejectWithUndefined();
  });

  AutoAllowLegacyScriptExecution exemption;

  AutoTArray<RefPtr<Promise>, 2> promises;

  IgnoredErrorResult rv;
  promises.AppendElement(TranslateDocument(rv));
  if (NS_WARN_IF(rv.Failed())) {
    return;
  }
  promises.AppendElement(TranslateRoots(rv));
  if (NS_WARN_IF(rv.Failed())) {
    return;
  }
  Element* documentElement = mDocument->GetDocumentElement();
  if (!documentElement) {
    return;
  }

  DOMLocalization::ConnectRoot(*documentElement);

  AutoEntryScript aes(mGlobal, "DocumentL10n InitialTranslation");
  RefPtr<Promise> promise = Promise::All(aes.cx(), promises, rv);
  if (NS_WARN_IF(rv.Failed())) {
    return;
  }

  errorExit.release();

  if (promise->State() == Promise::PromiseState::Resolved) {
    // If the promise is already resolved, we can fast-track
    // to initial translation completed.
    InitialTranslationCompleted();
    mReady->MaybeResolveWithUndefined();
  } else {
    RefPtr<PromiseNativeHandler> l10nReadyHandler =
        new L10nReadyHandler(mReady, this);
    promise->AppendNativeHandler(l10nReadyHandler);

    mState = DocumentL10nState::InitialTranslationTriggered;
  }
}

already_AddRefed<Promise> DocumentL10n::TranslateDocument(ErrorResult& aRv) {
  MOZ_ASSERT(mState == DocumentL10nState::Constructed,
             "This method should be called only from Constructed state.");
  RefPtr<Promise> promise = Promise::Create(mGlobal, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  Element* elem = mDocument->GetDocumentElement();
  if (!elem) {
    promise->MaybeRejectWithUndefined();
    return promise.forget();
  }

  // 1. Collect all localizable elements.
  Sequence<OwningNonNull<Element>> elements;
  GetTranslatables(*elem, elements, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    promise->MaybeRejectWithUndefined();
    return promise.forget();
  }

  RefPtr<nsXULPrototypeDocument> proto = mDocument->GetPrototype();
  // Check if the document has a prototype that may cache translated elements,
  // and if so remove the elements that were translated from their prototype and
  // unmodified.
  if (proto) {
    elements.RemoveElementsBy([](Element* aElement) {
      return aElement->IsElementTranslatedFromPrototype();
    });
  }
  promise = TranslateElements(elements, proto, aRv);
  return promise.forget();
}

void DocumentL10n::InitialTranslationCompleted() {
  if (mState >= DocumentL10nState::Ready) {
    return;
  }

  Element* documentElement = mDocument->GetDocumentElement();
  if (documentElement) {
    SetRootInfo(documentElement);
  }

  mState = DocumentL10nState::Ready;

  RefPtr<Document> doc = mDocument;
  doc->InitialTranslationCompleted();

  // In XUL scenario contentSink is nullptr.
  if (mContentSink) {
    nsCOMPtr<nsIContentSink> sink = mContentSink.forget();
    sink->InitialTranslationCompleted();
  }

  // From now on, the state of Localization is unconditionally
  // async.
  SetAsync();
}

void DocumentL10n::ConnectRoot(nsINode& aNode, bool aTranslate,
                               ErrorResult& aRv) {
  if (aTranslate) {
    if (mState >= DocumentL10nState::InitialTranslationTriggered) {
      RefPtr<Promise> promise = TranslateFragment(aNode, aRv);
    }
  }
  DOMLocalization::ConnectRoot(aNode);
}

Promise* DocumentL10n::Ready() { return mReady; }

void DocumentL10n::OnCreatePresShell() { mMutations->OnCreatePresShell(); }
