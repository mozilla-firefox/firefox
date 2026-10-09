/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FontFaceSet.h"

#include "gfxFontConstants.h"
#include "gfxFontSrcPrincipal.h"
#include "gfxFontSrcURI.h"
#include "gfxFontUtils.h"
#include "FontPreloader.h"
#include "mozilla/css/Loader.h"
#include "mozilla/dom/CSSFontFaceRule.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/FontFaceImpl.h"
#include "mozilla/dom/FontFaceSetBinding.h"
#include "mozilla/dom/FontFaceSetDocumentImpl.h"
#include "mozilla/dom/FontFaceSetWorkerImpl.h"
#include "mozilla/dom/FontFaceSetIterator.h"
#include "mozilla/dom/FontFaceSetLoadEvent.h"
#include "mozilla/dom/FontFaceSetLoadEventBinding.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/FontPropertyTypes.h"
#include "mozilla/AsyncEventDispatcher.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/Logging.h"
#include "mozilla/Preferences.h"
#include "mozilla/PresShell.h"
#include "mozilla/PresShellInlines.h"
#include "mozilla/ServoBindings.h"
#include "mozilla/ServoCSSParser.h"
#include "mozilla/ServoStyleSet.h"
#include "mozilla/ServoUtils.h"
#include "mozilla/Sprintf.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/Telemetry.h"
#include "mozilla/LoadInfo.h"
#include "nsComponentManagerUtils.h"
#include "nsContentPolicyUtils.h"
#include "nsContentUtils.h"
#include "nsDeviceContext.h"
#include "nsFontFaceLoader.h"
#include "nsIConsoleService.h"
#include "nsIContentPolicy.h"
#include "nsIDocShell.h"
#include "mozilla/dom/Document.h"
#include "nsILoadContext.h"
#include "nsINetworkPredictor.h"
#include "nsIPrincipal.h"
#include "nsIWebNavigation.h"
#include "nsNetUtil.h"
#include "nsIInputStream.h"
#include "nsLayoutUtils.h"
#include "nsPresContext.h"
#include "nsPrintfCString.h"
#include "nsUTF8Utils.h"
#include "nsDOMNavigationTiming.h"
#include "ReferrerInfo.h"

using namespace mozilla;
using namespace mozilla::css;
using namespace mozilla::dom;

NS_IMPL_CYCLE_COLLECTION_CLASS(FontFaceSet)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(FontFaceSet,
                                                  DOMEventTargetHelper)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_RAWPTR(mImpl->GetDocument());
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mReady);
  for (size_t i = 0; i < tmp->mRuleFaces.Length(); i++) {
    NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mRuleFaces[i].mFontFace);
  }
  for (size_t i = 0; i < tmp->mNonRuleFaces.Length(); i++) {
    NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mNonRuleFaces[i].mFontFace);
  }
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(FontFaceSet,
                                                DOMEventTargetHelper)
  tmp->Destroy();
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mReady);
  for (size_t i = 0; i < tmp->mRuleFaces.Length(); i++) {
    NS_IMPL_CYCLE_COLLECTION_UNLINK(mRuleFaces[i].mFontFace);
  }
  for (size_t i = 0; i < tmp->mNonRuleFaces.Length(); i++) {
    NS_IMPL_CYCLE_COLLECTION_UNLINK(mNonRuleFaces[i].mFontFace);
  }
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_ADDREF_INHERITED(FontFaceSet, DOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(FontFaceSet, DOMEventTargetHelper)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(FontFaceSet)
NS_INTERFACE_MAP_END_INHERITING(DOMEventTargetHelper)

FontFaceSet::FontFaceSet(nsIGlobalObject* aParent)
    : DOMEventTargetHelper(aParent) {}

FontFaceSet::~FontFaceSet() {
  // Assert that we don't drop any FontFaceSet objects during a Servo traversal,
  // since PostTraversalTask objects can hold raw pointers to FontFaceSets.
  MOZ_ASSERT(!gfxFontUtils::IsInServoTraversal());

  Destroy();
}

/* static */ bool FontFaceSet::IsEnabled() {
  if (NS_IsMainThread()) {
    return StaticPrefs::layout_css_font_loading_api_enabled();
  }
  return StaticPrefs::layout_css_font_loading_api_workers_enabled();
}

/* static */ already_AddRefed<FontFaceSet> FontFaceSet::CreateForDocument(
    dom::Document* aDocument) {
  RefPtr<FontFaceSet> set = new FontFaceSet(aDocument->GetScopeObject());
  RefPtr<FontFaceSetDocumentImpl> impl =
      new FontFaceSetDocumentImpl(set, aDocument);
  set->mImpl = impl;
  impl->Initialize();
  return set.forget();
}

/* static */ already_AddRefed<FontFaceSet> FontFaceSet::CreateForWorker(
    nsIGlobalObject* aParent, WorkerPrivate* aWorkerPrivate) {
  RefPtr<FontFaceSet> set = new FontFaceSet(aParent);
  RefPtr<FontFaceSetWorkerImpl> impl = new FontFaceSetWorkerImpl(set);
  set->mImpl = impl;
  if (NS_WARN_IF(!impl->Initialize(aWorkerPrivate))) {
    return nullptr;
  }
  return set.forget();
}

JSObject* FontFaceSet::WrapObject(JSContext* aContext,
                                  JS::Handle<JSObject*> aGivenProto) {
  return FontFaceSet_Binding::Wrap(aContext, this, aGivenProto);
}

void FontFaceSet::Destroy() { mImpl->Destroy(); }

already_AddRefed<Promise> FontFaceSet::Load(JSContext* aCx,
                                            const nsACString& aFont,
                                            const nsAString& aText,
                                            ErrorResult& aRv) {
  FlushUserFontSet();

  nsTArray<RefPtr<Promise>> promises;

  nsTArray<RefPtr<FontFace>> faces;
  {
    nsTArray<FontFace*> weakFaces;
    mImpl->FindMatchingFontFaces(aFont, aText, weakFaces, aRv);
    if (aRv.Failed()) {
      return nullptr;
    }
    if (!faces.AppendElements(weakFaces, fallible)) {
      aRv.Throw(NS_ERROR_FAILURE);
      return nullptr;
    }
  }

  for (FontFace* f : faces) {
    RefPtr<Promise> promise = f->Load(aRv);
    if (aRv.Failed()) {
      return nullptr;
    }
    if (!promises.AppendElement(promise, fallible)) {
      aRv.Throw(NS_ERROR_FAILURE);
      return nullptr;
    }
  }

  return Promise::All(aCx, promises, aRv);
}

bool FontFaceSet::Check(const nsACString& aFont, const nsAString& aText,
                        ErrorResult& aRv) {
  FlushUserFontSet();

  nsTArray<FontFace*> faces;
  mImpl->FindMatchingFontFaces(aFont, aText, faces, aRv);
  if (aRv.Failed()) {
    return false;
  }

  for (FontFace* f : faces) {
    if (f->Status() != FontFaceLoadStatus::Loaded) {
      return false;
    }
  }

  return true;
}

bool FontFaceSet::ReadyPromiseIsPending() const {
  return mReady ? mReady->State() == Promise::PromiseState::Pending
                : !mResolveLazilyCreatedReadyPromise;
}

Promise* FontFaceSet::GetReady(ErrorResult& aRv) {
  mImpl->EnsureReady();

  if (!mReady) {
    nsCOMPtr<nsIGlobalObject> global = GetParentObject();
    mReady = Promise::Create(global, aRv);
    if (!mReady) {
      aRv.Throw(NS_ERROR_FAILURE);
      return nullptr;
    }
    if (mResolveLazilyCreatedReadyPromise) {
      mReady->MaybeResolve(this);
      mResolveLazilyCreatedReadyPromise = false;
    }
  }

  return mReady;
}

FontFaceSetLoadStatus FontFaceSet::Status() { return mImpl->Status(); }

#ifdef DEBUG
bool FontFaceSet::HasRuleFontFace(FontFace* aFontFace) {
  for (size_t i = 0; i < mRuleFaces.Length(); i++) {
    if (mRuleFaces[i].mFontFace == aFontFace) {
      return true;
    }
  }
  return false;
}
#endif

void FontFaceSet::Add(FontFace& aFontFace, ErrorResult& aRv) {
  FlushUserFontSet();

  FontFaceImpl* fontImpl = aFontFace.GetImpl();
  MOZ_ASSERT(fontImpl);

  if (!mImpl->Add(fontImpl, aRv)) {
    return;
  }

  MOZ_ASSERT(!aRv.Failed());

#ifdef DEBUG
  for (const FontFaceRecord& rec : mNonRuleFaces) {
    MOZ_ASSERT(rec.mFontFace != &aFontFace,
               "FontFace should not occur in mNonRuleFaces twice");
  }
#endif

  FontFaceRecord* rec = mNonRuleFaces.AppendElement();
  rec->mFontFace = &aFontFace;
  rec->mOrigin = Nothing();
  rec->mLoadEventShouldFire =
      fontImpl->Status() == FontFaceLoadStatus::Unloaded ||
      fontImpl->Status() == FontFaceLoadStatus::Loading;
}

void FontFaceSet::Clear() {
  nsTArray<FontFaceRecord> oldRecords = std::move(mNonRuleFaces);
  mImpl->Clear();
}

bool FontFaceSet::Delete(FontFace& aFontFace) {
  // Hold onto a strong reference to make sure that when we remove FontFace from
  // the list, the FontFaceImpl does not get freed right away. We need to check
  // the FontFaceSetImpl first.
  RefPtr<FontFaceImpl> fontImpl = aFontFace.GetImpl();
  MOZ_ASSERT(fontImpl);

  // Ensure that we remove from mNonRuleFaces first. This is important so that
  // when we check to see if all of the fonts have finished loading, the list in
  // FontFaceSet and FontFaceSetImpl match.
  bool removed = false;
  for (size_t i = 0; i < mNonRuleFaces.Length(); i++) {
    if (mNonRuleFaces[i].mFontFace == &aFontFace) {
      mNonRuleFaces.RemoveElementAt(i);
      removed = true;
      break;
    }
  }

  if (!mImpl->Delete(fontImpl)) {
    MOZ_ASSERT(!removed, "Missing rule present in Impl!");
  } else {
    MOZ_ASSERT(removed, "Rule present but missing in Impl!");
  }

  return removed;
}

bool FontFaceSet::HasAvailableFontFace(FontFace* aFontFace) {
  return aFontFace->GetImpl()->IsInFontFaceSet(mImpl);
}

bool FontFaceSet::Has(FontFace& aFontFace) {
  FlushUserFontSet();

  return HasAvailableFontFace(&aFontFace);
}

FontFace* FontFaceSet::GetFontFaceAt(uint32_t aIndex) {
  FlushUserFontSet();

  if (aIndex < mRuleFaces.Length()) {
    auto& entry = mRuleFaces[aIndex];
    if (entry.mOrigin.value() != StyleOrigin::Author) {
      return nullptr;
    }
    return entry.mFontFace;
  }

  aIndex -= mRuleFaces.Length();
  if (aIndex < mNonRuleFaces.Length()) {
    return mNonRuleFaces[aIndex].mFontFace;
  }

  return nullptr;
}

uint32_t FontFaceSet::Size() {
  FlushUserFontSet();

  // Web IDL objects can only expose array index properties up to INT32_MAX.

  size_t total = mNonRuleFaces.Length();
  for (const auto& entry : mRuleFaces) {
    if (entry.mOrigin.value() == StyleOrigin::Author) {
      ++total;
    }
  }
  return std::min<size_t>(total, INT32_MAX);
}

uint32_t FontFaceSet::SizeIncludingNonAuthorOrigins() {
  FlushUserFontSet();

  // Web IDL objects can only expose array index properties up to INT32_MAX.

  size_t total = mRuleFaces.Length() + mNonRuleFaces.Length();
  return std::min<size_t>(total, INT32_MAX);
}

already_AddRefed<FontFaceSetIterator> FontFaceSet::Entries() {
  RefPtr<FontFaceSetIterator> it = new FontFaceSetIterator(this, true);
  return it.forget();
}

already_AddRefed<FontFaceSetIterator> FontFaceSet::Values() {
  RefPtr<FontFaceSetIterator> it = new FontFaceSetIterator(this, false);
  return it.forget();
}

void FontFaceSet::ForEach(JSContext* aCx, FontFaceSetForEachCallback& aCallback,
                          JS::Handle<JS::Value> aThisArg, ErrorResult& aRv) {
  JS::Rooted<JS::Value> thisArg(aCx, aThisArg);
  for (size_t i = 0; i < SizeIncludingNonAuthorOrigins(); i++) {
    RefPtr<FontFace> face = GetFontFaceAt(i);
    if (!face) {
      // The font at index |i| is a non-Author origin font, which we shouldn't
      // expose per spec.
      continue;
    }
    aCallback.Call(thisArg, *face, *face, *this, aRv);
    if (aRv.Failed()) {
      return;
    }
  }
}

bool FontFaceSet::UpdateRules(const nsTArray<nsFontFaceRuleContainer>& aRules) {
  // The impl object handles the callbacks for recreating the mRulesFaces array.
  nsTArray<FontFaceRecord> oldRecords = std::move(mRuleFaces);
  return mImpl->UpdateRules(aRules);
}

void FontFaceSet::InsertRuleFontFace(FontFace* aFontFace, StyleOrigin aOrigin) {
  MOZ_ASSERT(!HasRuleFontFace(aFontFace));

  FontFaceRecord* rec = mRuleFaces.AppendElement();
  rec->mFontFace = aFontFace;
  rec->mOrigin = Some(aOrigin);
  rec->mLoadEventShouldFire =
      aFontFace->Status() == FontFaceLoadStatus::Unloaded ||
      aFontFace->Status() == FontFaceLoadStatus::Loading;
}

void FontFaceSet::DidRefresh() { mImpl->CheckLoadingFinished(); }

void FontFaceSet::DispatchLoadingEventAndReplaceReadyPromise() {
  gfxFontUtils::AssertSafeThreadOrServoFontMetricsLocked();

  if (ServoStyleSet* set = gfxFontUtils::CurrentServoStyleSet()) {
    // See comments in Gecko_GetFontMetrics.
    //
    // We can't just dispatch the runnable below if we're not on the main
    // thread, since it needs to take a strong reference to the FontFaceSet,
    // and being a DOM object, FontFaceSet doesn't support thread-safe
    // refcounting.  (Also, the Promise object creation must be done on
    // the main thread.)
    set->AppendTask(
        PostTraversalTask::DispatchLoadingEventAndReplaceReadyPromise(
            do_AddRef(mImpl)));
    return;
  }

  (new AsyncEventDispatcher(this, u"loading"_ns, CanBubble::eNo))
      ->PostDOMEvent();

  if (IsEnabled()) {
    if (mReady && mReady->State() != Promise::PromiseState::Pending) {
      if (GetParentObject()) {
        ErrorResult rv;
        mReady = Promise::Create(GetParentObject(), rv);
      }
    }

    // We may previously have been in a state where all fonts had finished
    // loading and we'd set mResolveLazilyCreatedReadyPromise to make sure that
    // if we lazily create mReady for a consumer that we resolve it before
    // returning it.  We're now loading fonts, so we need to clear that flag.
    mResolveLazilyCreatedReadyPromise = false;
  }
}

void FontFaceSet::MaybeResolve() {
  if (mReady) {
    mReady->MaybeResolve(this);
  } else {
    mResolveLazilyCreatedReadyPromise = true;
  }

  // Now dispatch the loadingdone/loadingerror events.
  nsTArray<OwningNonNull<FontFace>> loaded;
  nsTArray<OwningNonNull<FontFace>> failed;

  auto checkStatus = [&](nsTArray<FontFaceRecord>& faces) -> void {
    for (auto& face : faces) {
      if (!face.mLoadEventShouldFire) {
        continue;
      }
      FontFace* f = face.mFontFace;
      switch (f->Status()) {
        case FontFaceLoadStatus::Unloaded:
          break;
        case FontFaceLoadStatus::Loaded:
          loaded.AppendElement(*f);
          face.mLoadEventShouldFire = false;
          break;
        case FontFaceLoadStatus::Error:
          failed.AppendElement(*f);
          face.mLoadEventShouldFire = false;
          break;
        case FontFaceLoadStatus::Loading:
          // We should've returned above at MightHavePendingFontLoads()!
        case FontFaceLoadStatus::EndGuard_:
          MOZ_ASSERT_UNREACHABLE("unexpected FontFaceLoadStatus");
          break;
      }
    }
  };

  checkStatus(mRuleFaces);
  checkStatus(mNonRuleFaces);

  DispatchLoadingFinishedEvent(u"loadingdone"_ns, std::move(loaded));

  if (!failed.IsEmpty()) {
    DispatchLoadingFinishedEvent(u"loadingerror"_ns, std::move(failed));
  }
}

void FontFaceSet::DispatchLoadingFinishedEvent(
    const nsAString& aType, nsTArray<OwningNonNull<FontFace>>&& aFontFaces) {
  FontFaceSetLoadEventInit init;
  init.mBubbles = false;
  init.mCancelable = false;
  init.mFontfaces = std::move(aFontFaces);
  RefPtr<FontFaceSetLoadEvent> event =
      FontFaceSetLoadEvent::Constructor(this, aType, init);
  (new AsyncEventDispatcher(this, event))->PostDOMEvent();
}

void FontFaceSet::FlushUserFontSet() { mImpl->FlushUserFontSet(); }

void FontFaceSet::RefreshStandardFontLoadPrincipal() {
  MOZ_ASSERT(NS_IsMainThread());
  mImpl->RefreshStandardFontLoadPrincipal();
}

void FontFaceSet::CopyNonRuleFacesTo(FontFaceSet* aFontFaceSet) const {
  for (const FontFaceRecord& rec : mNonRuleFaces) {
    IgnoredErrorResult rv;
    RefPtr<FontFace> f = rec.mFontFace;
    aFontFaceSet->Add(*f, rv);
    MOZ_ASSERT(!rv.Failed());
  }
}

#undef LOG_ENABLED
#undef LOG
