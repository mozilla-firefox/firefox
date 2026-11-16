/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_l10n_DOMLocalization_h
#define mozilla_dom_l10n_DOMLocalization_h

#include "mozilla/dom/DOMLocalizationBinding.h"
#include "mozilla/dom/L10nMutations.h"
#include "mozilla/dom/L10nOverlaysBinding.h"
#include "mozilla/dom/LocalizationBinding.h"
#include "mozilla/dom/PromiseNativeHandler.h"
#include "mozilla/intl/L10nRegistry.h"
#include "mozilla/intl/Localization.h"
#include "nsTHashSet.h"
#include "nsXULPrototypeDocument.h"

// XXX Avoid including this here by moving function bodies to the cpp file
#include "nsINode.h"

namespace mozilla::dom {

class Element;
class L10nMutations;

class DOMLocalization : public intl::Localization {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(DOMLocalization, Localization)

  void Destroy();

  static already_AddRefed<DOMLocalization> Constructor(
      const dom::GlobalObject& aGlobal,
      const dom::Sequence<dom::OwningUTF8StringOrResourceId>& aResourceIds,
      bool aIsSync,
      const dom::Optional<dom::NonNull<intl::L10nRegistry>>& aRegistry,
      const dom::Optional<dom::Sequence<nsCString>>& aLocales,
      ErrorResult& aRv);

  JSObject* WrapObject(JSContext*, JS::Handle<JSObject*> aGivenProto) override;

  bool HasPendingMutations() const;
  bool IsDocumentL10n() const { return mIsDocumentL10n; }

  /**
   * DOMLocalization API
   *
   * Methods documentation in DOMLocalization.webidl
   */

  void ConnectRoot(nsINode& aNode);
  void DisconnectRoot(nsINode& aNode);

  void PauseObserving();
  void ResumeObserving();

  void SetAttributes(JSContext* aCx, Element& aElement, const nsAString& aId,
                     const Optional<JS::Handle<JSObject*>>& aArgs,
                     ErrorResult& aRv);
  void GetAttributes(Element& aElement, L10nIdArgs& aResult, ErrorResult& aRv);

  void SetArgs(JSContext* aCx, Element& aElement,
               const Optional<JS::Handle<JSObject*>>& aArgs, ErrorResult& aRv);

  already_AddRefed<Promise> TranslateFragment(nsINode& aNode, ErrorResult& aRv);

  already_AddRefed<Promise> TranslateElements(
      const nsTArray<OwningNonNull<Element>>& aElements, ErrorResult& aRv);
  already_AddRefed<Promise> TranslateElements(
      const nsTArray<OwningNonNull<Element>>& aElements,
      nsXULPrototypeDocument* aProto, ErrorResult& aRv);

  already_AddRefed<Promise> TranslateRoots(ErrorResult& aRv);

  /**
   * Helper methods
   */

  /**
   * Accumulates all translatable elements (ones containing
   * a `data-l10n-id` attribute) from under a node into
   * a list of elements.
   */
  static void GetTranslatables(nsINode& aNode,
                               Sequence<OwningNonNull<Element>>& aElements,
                               ErrorResult& aRv);

  /**
   * Sets the root information such as locale and direction.
   */
  static void SetRootInfo(Element* aElement);

  /**
   * Applies l10n translations on translatable elements.
   *
   * `aRequestId` identifies the TranslateElements call the translations
   * were formatted for. An element that already shows a translation from a
   * later call is left alone.
   *
   * If `aProto` gets passed, it'll be used to cache
   * the localized elements.
   *
   * Result is `true` if all translations were applied
   * successfully, and `false` otherwise.
   */
  bool ApplyTranslations(nsTArray<nsCOMPtr<Element>>& aElements,
                         nsTArray<Nullable<L10nMessage>>& aTranslations,
                         nsXULPrototypeDocument* aProto, uint64_t aRequestId,
                         ErrorResult& aRv);

  // `aSubtreeRoot` must itself be a subtree root, i.e. the result of
  // `nsINode::SubtreeRoot()`.
  bool SubtreeRootInRoots(nsINode* aSubtreeRoot) {
    MOZ_ASSERT(aSubtreeRoot->SubtreeRoot() == aSubtreeRoot);
    // A connected root that is its own subtree root - which every ShadowRoot
    // is - answers this without walking the set.
    if (mRoots.Contains(aSubtreeRoot)) {
      return true;
    }
    for (const auto* key : mRoots) {
      nsINode* subtreeRoot = key->SubtreeRoot();
      if (subtreeRoot == aSubtreeRoot) {
        return true;
      }
    }
    return false;
  }

  DOMLocalization(nsIGlobalObject* aGlobal, bool aSync);
  DOMLocalization(nsIGlobalObject* aGlobal, bool aIsSync,
                  const intl::ffi::LocalizationRc* aRaw);
  DOMLocalization(nsIGlobalObject* aGlobal, bool aSync,
                  const nsTArray<nsCString>& aLocales);

 protected:
  virtual ~DOMLocalization();
  void OnChange() override;
  void DisconnectMutations();
  void DisconnectRoots();
  void ReportL10nOverlaysErrors(nsTArray<L10nOverlaysError>& aErrors);
  void ConvertStringToL10nArgs(const nsCString& aL10nId, const nsString& aInput,
                               intl::L10nArgs& aRetVal, ErrorResult& aRv);

  RefPtr<L10nMutations> mMutations;
  nsTHashSet<RefPtr<nsINode>> mRoots;
  bool mIsDocumentL10n = false;
};

}  // namespace mozilla::dom

#endif
