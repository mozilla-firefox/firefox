/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "L10nMutations.h"

#include "DOMLocalization.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/intl/Localization.h"
#include "nsRefreshDriver.h"
#include "nsThreadManager.h"

using namespace mozilla;
using namespace mozilla::intl;
using namespace mozilla::dom;

NS_IMPL_CYCLE_COLLECTION_CLASS(L10nMutations)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(L10nMutations)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mPendingElements)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mPendingElementsHash)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(L10nMutations)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mPendingElements)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mPendingElementsHash)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(L10nMutations)
  NS_INTERFACE_MAP_ENTRY(nsIMutationObserver)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(L10nMutations)
NS_IMPL_CYCLE_COLLECTING_RELEASE(L10nMutations)

L10nMutations::L10nMutations(DOMLocalization* aDOMLocalization)
    : mDOMLocalization(aDOMLocalization) {
  mObserving = true;
}

L10nMutations::~L10nMutations() {
  StopRefreshObserver();
  MOZ_ASSERT(!mDOMLocalization,
             "DOMLocalization<-->L10nMutations cycle should be broken.");
}

void L10nMutations::AttributeChanged(Element* aElement, int32_t aNameSpaceID,
                                     nsAtom* aAttribute, AttrModType,
                                     const nsAttrValue* aOldValue) {
  if (!mObserving) {
    return;
  }

  if (aNameSpaceID == kNameSpaceID_None &&
      (aAttribute == nsGkAtoms::datal10nid ||
       aAttribute == nsGkAtoms::datal10nargs)) {
    if (IsInRoots(aElement)) {
      L10nElementChanged(aElement);
    }
  }
}

void L10nMutations::ContentAppended(nsIContent* aChild,
                                    const ContentAppendInfo&) {
  if (!mObserving) {
    return;
  }

  if (!IsInRoots(aChild)) {
    return;
  }

  Sequence<OwningNonNull<Element>> elements;
  for (nsIContent* node = aChild; node; node = node->GetNextSibling()) {
    if (node->IsElement()) {
      DOMLocalization::GetTranslatables(*node, elements, IgnoreErrors());
    }
  }

  for (auto& elem : elements) {
    L10nElementChanged(elem);
  }
}

void L10nMutations::ContentInserted(nsIContent* aChild,
                                    const ContentInsertInfo&) {
  if (!mObserving) {
    return;
  }

  if (!aChild->IsElement()) {
    return;
  }
  Element* elem = aChild->AsElement();

  if (!IsInRoots(elem)) {
    return;
  }

  Sequence<OwningNonNull<Element>> elements;
  DOMLocalization::GetTranslatables(*aChild, elements, IgnoreErrors());

  for (auto& elem : elements) {
    L10nElementChanged(elem);
  }
}

void L10nMutations::ContentWillBeRemoved(nsIContent* aChild,
                                         const ContentRemoveInfo&) {
  if (!mObserving || mPendingElements.IsEmpty()) {
    return;
  }

  Element* elem = Element::FromNode(*aChild);
  if (!elem || !IsInRoots(elem)) {
    return;
  }

  Sequence<OwningNonNull<Element>> elements;
  DOMLocalization::GetTranslatables(*aChild, elements, IgnoreErrors());

  for (auto& elem : elements) {
    if (mPendingElementsHash.EnsureRemoved(elem)) {
      mPendingElements.RemoveElement(elem);
    }
  }

  if (!HasPendingMutations()) {
    nsContentUtils::AddScriptRunner(NewRunnableMethod(
        "MaybeFirePendingTranslationsFinished", this,
        &L10nMutations::MaybeFirePendingTranslationsFinished));
  }
}

void L10nMutations::L10nElementChanged(Element* aElement) {
  const bool wasEmpty = mPendingElements.IsEmpty();

  if (mPendingElementsHash.EnsureInserted(aElement)) {
    mPendingElements.AppendElement(aElement);
  }

  if (!wasEmpty) {
    return;
  }

  if (!mRefreshDriver) {
    StartRefreshObserver();
  }

  if (!mBlockingLoad && mDOMLocalization->IsDocumentL10n()) {
    Document* doc = GetDocument();
    if (doc && doc->GetReadyStateEnum() != Document::READYSTATE_COMPLETE) {
      doc->BlockOnload();
      mBlockingLoad = true;
    }
  }

  if (mBlockingLoad && !mPendingBlockingLoadFlush) {
    // We want to make sure we flush translations and don't block the load
    // indefinitely (and, in fact, that we do it rather soon, even if the
    // refresh driver is not ticking yet).
    //
    // In some platforms (mainly Wayland) the load of the main document
    // causes vsync to start running and start ticking the refresh driver,
    // so we can't rely on the refresh driver ticking yet.
    RefPtr<nsIRunnable> task =
        NewRunnableMethod("FlushPendingTranslationsBeforeLoad", this,
                          &L10nMutations::FlushPendingTranslationsBeforeLoad);
    nsThreadManager::get().DispatchDirectTaskToCurrentThread(task);
    mPendingBlockingLoadFlush = true;
  }
}

void L10nMutations::PauseObserving() { mObserving = false; }

void L10nMutations::ResumeObserving() { mObserving = true; }

void L10nMutations::WillRefresh(mozilla::TimeStamp aTime) {
  StopRefreshObserver();
  FlushPendingTranslations();
}

/**
 * The handler for the `TranslateElements` promise used to turn
 * a potential rejection into a console warning.
 **/
class L10nMutationFinalizationHandler final : public PromiseNativeHandler {
 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS_FINAL
  NS_DECL_CYCLE_COLLECTION_CLASS(L10nMutationFinalizationHandler)

  explicit L10nMutationFinalizationHandler(L10nMutations* aMutations,
                                           nsIGlobalObject* aGlobal)
      : mMutations(aMutations), mGlobal(aGlobal) {}

  MOZ_CAN_RUN_SCRIPT void Settled() {
    if (RefPtr mutations = mMutations) {
      mutations->PendingPromiseSettled();
    }
  }

  MOZ_CAN_RUN_SCRIPT void ResolvedCallback(JSContext* aCx,
                                           JS::Handle<JS::Value> aValue,
                                           ErrorResult& aRv) override {
    Settled();
  }

  MOZ_CAN_RUN_SCRIPT void RejectedCallback(JSContext* aCx,
                                           JS::Handle<JS::Value> aValue,
                                           ErrorResult& aRv) override {
    nsTArray<nsCString> errors{
        "[dom/l10n] Errors during l10n mutation frame."_ns,
    };
    MaybeReportErrorsToGecko(errors, IgnoreErrors(), mGlobal);
    Settled();
  }

 private:
  ~L10nMutationFinalizationHandler() = default;

  RefPtr<L10nMutations> mMutations;
  nsCOMPtr<nsIGlobalObject> mGlobal;
};

NS_IMPL_CYCLE_COLLECTION(L10nMutationFinalizationHandler, mGlobal, mMutations)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(L10nMutationFinalizationHandler)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(L10nMutationFinalizationHandler)
NS_IMPL_CYCLE_COLLECTING_RELEASE(L10nMutationFinalizationHandler)

void L10nMutations::FlushPendingTranslationsBeforeLoad() {
  MOZ_ASSERT(mPendingBlockingLoadFlush);
  mPendingBlockingLoadFlush = false;
  FlushPendingTranslations();
}

void L10nMutations::FlushPendingTranslations() {
  if (!mDOMLocalization) {
    return;
  }

  nsTArray<OwningNonNull<Element>> elements;
  for (auto& elem : mPendingElements) {
    if (elem->HasAttr(nsGkAtoms::datal10nid)) {
      elements.AppendElement(*elem);
    }
  }

  mPendingElementsHash.Clear();
  mPendingElements.Clear();

  RefPtr<Promise> promise =
      mDOMLocalization->TranslateElements(elements, IgnoreErrors());
  if (promise && promise->State() == Promise::PromiseState::Pending) {
    mPendingPromises++;
    auto l10nMutationFinalizationHandler =
        MakeRefPtr<L10nMutationFinalizationHandler>(
            this, mDOMLocalization->GetParentObject());
    promise->AppendNativeHandler(l10nMutationFinalizationHandler);
  }

  MaybeFirePendingTranslationsFinished();
}

void L10nMutations::PendingPromiseSettled() {
  MOZ_DIAGNOSTIC_ASSERT(mPendingPromises);
  mPendingPromises--;
  MaybeFirePendingTranslationsFinished();
}

void L10nMutations::MaybeFirePendingTranslationsFinished() {
  if (HasPendingMutations()) {
    return;
  }

  RefPtr doc = GetDocument();
  if (NS_WARN_IF(!doc)) {
    return;
  }

  if (mBlockingLoad) {
    mBlockingLoad = false;
    doc->UnblockOnload(false);
  }
  nsContentUtils::DispatchEventOnlyToChrome(
      doc, doc, u"L10nMutationsFinished"_ns, CanBubble::eNo, Cancelable::eNo,
      Composed::eNo, nullptr);
}

void L10nMutations::Disconnect() {
  StopRefreshObserver();
  mDOMLocalization = nullptr;
}

Document* L10nMutations::GetDocument() const {
  if (!mDOMLocalization) {
    return nullptr;
  }
  auto* innerWindow = mDOMLocalization->GetParentObject()->GetAsInnerWindow();
  if (!innerWindow) {
    return nullptr;
  }
  return innerWindow->GetExtantDoc();
}

void L10nMutations::StartRefreshObserver() {
  if (!mDOMLocalization || mRefreshDriver) {
    return;
  }
  if (Document* doc = GetDocument()) {
    if (nsPresContext* ctx = doc->GetPresContext()) {
      mRefreshDriver = ctx->RefreshDriver();
    }
  }

  // If we can't start the refresh driver, it means
  // that the presContext is not available yet.
  // In that case, we'll trigger the flush of pending
  // elements in Document::CreatePresShell.
  if (mRefreshDriver) {
    mRefreshDriver->AddRefreshObserver(this, FlushType::Style,
                                       "L10n mutations");
  } else {
    NS_WARNING("[l10n][mutations] Failed to start a refresh observer.");
  }
}

void L10nMutations::StopRefreshObserver() {
  if (mRefreshDriver) {
    mRefreshDriver->RemoveRefreshObserver(this, FlushType::Style);
    mRefreshDriver = nullptr;
  }
}

void L10nMutations::OnCreatePresShell() {
  StopRefreshObserver();
  if (!mPendingElements.IsEmpty()) {
    StartRefreshObserver();
  }
}

bool L10nMutations::IsInRoots(nsINode* aNode) {
  // If the root of the mutated element is in the light DOM,
  // we know it must be covered by our observer directly.
  //
  // Otherwise, we need to check if its subtree root is the same
  // as any of the `DOMLocalization::mRoots` subtree roots.
  nsINode* root = aNode->SubtreeRoot();

  // If element is in light DOM, it must be covered by one of
  // the DOMLocalization roots to end up here.
  MOZ_ASSERT_IF(!root->IsShadowRoot(),
                mDOMLocalization->SubtreeRootInRoots(root));

  return !root->IsShadowRoot() || mDOMLocalization->SubtreeRootInRoots(root);
}
