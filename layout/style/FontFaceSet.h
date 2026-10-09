/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_FontFaceSet_h
#define mozilla_dom_FontFaceSet_h

#include "mozilla/dom/FontFace.h"
#include "mozilla/dom/FontFaceSetBinding.h"
#include "mozilla/dom/FontFaceSetImpl.h"
#include "mozilla/DOMEventTargetHelper.h"
#include "nsICSSLoaderObserver.h"
#include "nsIDOMEventListener.h"

class nsFontFaceLoader;
class nsIPrincipal;
class nsIGlobalObject;

namespace mozilla {
class PostTraversalTask;
class SharedFontList;
namespace dom {
class Promise;
class WorkerPrivate;
}  // namespace dom
}  // namespace mozilla

namespace mozilla::dom {

class FontFaceSet final : public DOMEventTargetHelper {
  friend class mozilla::PostTraversalTask;

 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(FontFaceSet, DOMEventTargetHelper)

  static bool IsEnabled();

  static bool IsEnabled(JSContext* aCx, JSObject* aObj) { return IsEnabled(); }

  static already_AddRefed<FontFaceSet> CreateForDocument(
      dom::Document* aDocument);

  static already_AddRefed<FontFaceSet> CreateForWorker(
      nsIGlobalObject* aParent, WorkerPrivate* aWorkerPrivate);

  virtual JSObject* WrapObject(JSContext* aCx,
                               JS::Handle<JSObject*> aGivenProto) override;

  bool UpdateRules(const nsTArray<nsFontFaceRuleContainer>& aRules);

  /**
   * Notification method called by the nsPresContext to indicate that the
   * refresh driver ticked and flushed style and layout.
   * were just flushed.
   */
  void DidRefresh();

  void FlushUserFontSet();

  void RefreshStandardFontLoadPrincipal();

  void CopyNonRuleFacesTo(FontFaceSet* aFontFaceSet) const;

  void CacheFontLoadability() { mImpl->CacheFontLoadability(); }

  FontFaceSetImpl* GetImpl() const { return mImpl; }

  // -- Web IDL --------------------------------------------------------------

  IMPL_EVENT_HANDLER(loading)
  IMPL_EVENT_HANDLER(loadingdone)
  IMPL_EVENT_HANDLER(loadingerror)
  already_AddRefed<dom::Promise> Load(JSContext* aCx, const nsACString& aFont,
                                      const nsAString& aText, ErrorResult& aRv);
  bool Check(const nsACString& aFont, const nsAString& aText, ErrorResult& aRv);
  dom::Promise* GetReady(ErrorResult& aRv);
  dom::FontFaceSetLoadStatus Status();

  void Add(FontFace& aFontFace, ErrorResult& aRv);
  void Clear();
  bool Delete(FontFace& aFontFace);
  bool Has(FontFace& aFontFace);
  /**
   * This returns the number of Author origin fonts only.
   * (see also SizeIncludingNonAuthorOrigins() below)
   */
  uint32_t Size();
  already_AddRefed<dom::FontFaceSetIterator> Entries();
  already_AddRefed<dom::FontFaceSetIterator> Values();
  MOZ_CAN_RUN_SCRIPT
  void ForEach(JSContext* aCx, FontFaceSetForEachCallback& aCallback,
               JS::Handle<JS::Value> aThisArg, ErrorResult& aRv);

  /**
   * Unlike Size(), this returns the size including non-Author origin fonts.
   */
  uint32_t SizeIncludingNonAuthorOrigins();

  void MaybeResolve();

  void DispatchLoadingFinishedEvent(
      const nsAString& aType, nsTArray<OwningNonNull<FontFace>>&& aFontFaces);

  void DispatchLoadingEventAndReplaceReadyPromise();
  void DispatchCheckLoadingFinishedAfterDelay();

  // Whether mReady is pending, or would be when created.
  bool ReadyPromiseIsPending() const;

  void InsertRuleFontFace(FontFace* aFontFace, StyleOrigin aOrigin);

 private:
  friend mozilla::dom::FontFaceSetIterator;  // needs GetFontFaceAt()

  explicit FontFaceSet(nsIGlobalObject* aParent);
  ~FontFaceSet();

  /**
   * Returns whether the given FontFace is currently "in" the FontFaceSet.
   */
  bool HasAvailableFontFace(FontFace* aFontFace);

  /**
   * Removes any listeners and observers.
   */
  void Destroy();

  /**
   * Returns the font at aIndex if it's an Author origin font, or nullptr
   * otherwise.
   */
  FontFace* GetFontFaceAt(uint32_t aIndex);

  // Note: if you add new cycle collected objects to FontFaceRecord,
  // make sure to update FontFaceSet's cycle collection macros
  // accordingly.
  struct FontFaceRecord {
    RefPtr<FontFace> mFontFace;
    Maybe<StyleOrigin> mOrigin;  // only relevant for mRuleFaces entries

    // When true, indicates that when finished loading, the FontFace should be
    // included in the subsequent loadingdone/loadingerror event fired at the
    // FontFaceSet.
    bool mLoadEventShouldFire;
  };

#ifdef DEBUG
  bool HasRuleFontFace(FontFace* aFontFace);
#endif

  // The underlying implementation for FontFaceSet.
  RefPtr<FontFaceSetImpl> mImpl;

  // A Promise that is fulfilled once all of the FontFace objects
  // in mRuleFaces and mNonRuleFaces that started or were loading at the
  // time the Promise was created have finished loading.  It is rejected if
  // any of those fonts failed to load.  mReady is replaced with
  // a new Promise object whenever mReady is settled and another
  // FontFace in mRuleFaces or mNonRuleFaces starts to load.
  // Note that mReady is created lazily when GetReady() is called.
  RefPtr<dom::Promise> mReady;
  // Whether the ready promise must be resolved when it's created.
  bool mResolveLazilyCreatedReadyPromise = false;

  // The @font-face rule backed FontFace objects in the FontFaceSet.
  nsTArray<FontFaceRecord> mRuleFaces;

  // The non rule backed FontFace objects that have been added to this
  // FontFaceSet.
  nsTArray<FontFaceRecord> mNonRuleFaces;
};

}  // namespace mozilla::dom

#endif  // !defined(mozilla_dom_FontFaceSet_h)
