/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/StyleSheet.h"
#include "mozilla/Assertions.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/ComputedStyleInlines.h"
#include "mozilla/css/ErrorReporter.h"
#include "mozilla/css/GroupRule.h"
#include "mozilla/dom/CSSImportRule.h"
#include "mozilla/dom/CSSRuleList.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/MediaList.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/ReferrerInfo.h"
#include "mozilla/dom/ShadowRoot.h"
#include "mozilla/dom/ShadowRootBinding.h"
#include "mozilla/NullPrincipal.h"
#include "mozilla/ServoBindings.h"
#include "mozilla/ServoCSSRuleList.h"
#include "mozilla/ServoStyleSet.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/StyleSheetInlines.h"
#include "mozilla/css/SheetLoadData.h"

#include "mozAutoDocUpdate.h"
#include "SheetLoadData.h"

namespace mozilla {

using namespace dom;

StyleSheet::StyleSheet(css::SheetParsingMode aParsingMode, CORSMode aCORSMode,
                       const dom::SRIMetadata& aIntegrity)
    : mParentSheet(nullptr),
      mRelevantGlobal(nullptr),
      mConstructorDocument(nullptr),
      mDocumentOrShadowRoot(nullptr),
      mParsingMode(aParsingMode),
      mState(static_cast<State>(0)),
      mInner(new StyleSheetInfo(aCORSMode, aIntegrity, aParsingMode)) {
  mInner->AddSheet(this);
}

StyleSheet::StyleSheet(const StyleSheet& aCopy, StyleSheet* aParentSheetToUse,
                       dom::DocumentOrShadowRoot* aDocOrShadowRootToUse,
                       dom::Document* aConstructorDocToUse)
    : mParentSheet(aParentSheetToUse),
      mRelevantGlobal(nullptr),
      mConstructorDocument(aConstructorDocToUse),
      mTitle(aCopy.mTitle),
      mDocumentOrShadowRoot(aDocOrShadowRootToUse),
      mParsingMode(aCopy.mParsingMode),
      mState(aCopy.mState),
      // Shallow copy, but concrete subclasses will fix up.
      mInner(aCopy.mInner) {
  MOZ_ASSERT(!aConstructorDocToUse || aCopy.IsConstructed());
  MOZ_ASSERT(!aConstructorDocToUse || !aDocOrShadowRootToUse,
             "Should never have both of these together.");
  MOZ_ASSERT(mInner, "Should only copy StyleSheets with an mInner.");
  mInner->AddSheet(this);
  // CSSOM's been there, force full copy now.
  if (HasForcedUniqueInner()) {
    MOZ_ASSERT(IsComplete(),
               "Why have rules been accessed on an incomplete sheet?");
    EnsureUniqueInner();
    // But CSSOM hasn't been on _this_ stylesheet yet, so no need to clone
    // ourselves.
    mState &= ~(State::ForcedUniqueInner | State::ModifiedRules |
                State::ModifiedRulesForDevtools);
  }

  if (aCopy.mMedia) {
    // XXX This is wrong; we should be keeping @import rules and
    // sheets in sync!
    mMedia = aCopy.mMedia->Clone();
  }
}

/* static */
// https://wicg.github.io/construct-stylesheets/#dom-cssstylesheet-cssstylesheet
already_AddRefed<StyleSheet> StyleSheet::Constructor(
    const dom::GlobalObject& aGlobal, const dom::CSSStyleSheetInit& aOptions,
    ErrorResult& aRv) {
  nsCOMPtr<nsPIDOMWindowInner> window =
      do_QueryInterface(aGlobal.GetAsSupports());

  if (!window) {
    aRv.ThrowNotSupportedError("Not supported when there is no document");
    return nullptr;
  }

  Document* constructorDocument = window->GetExtantDoc();
  if (!constructorDocument) {
    aRv.ThrowNotSupportedError("Not supported when there is no document");
    return nullptr;
  }

  // 1. Construct a sheet and set its properties (see spec).
  auto sheet =
      MakeRefPtr<StyleSheet>(css::SheetParsingMode::eAuthorSheetFeatures,
                             CORSMode::CORS_NONE, dom::SRIMetadata());

  // baseURL not yet in the spec. Implemented based on the following discussion:
  // https://github.com/WICG/construct-stylesheets/issues/95#issuecomment-594217180
  RefPtr<nsIURI> baseURI;
  if (!aOptions.mBaseURL.WasPassed()) {
    baseURI = constructorDocument->GetBaseURI();
  } else {
    nsresult rv = NS_NewURI(getter_AddRefs(baseURI), aOptions.mBaseURL.Value(),
                            nullptr, constructorDocument->GetBaseURI());
    if (NS_FAILED(rv)) {
      aRv.ThrowNotAllowedError(
          "Constructed style sheets must have a valid base URL");
      return nullptr;
    }
  }

  nsIURI* sheetURI = constructorDocument->GetDocumentURI();
  nsIURI* originalURI = nullptr;
  sheet->SetURIs(sheetURI, originalURI, baseURI);

  sheet->SetPrincipal(constructorDocument->NodePrincipal());
  auto referrerInfo = MakeRefPtr<ReferrerInfo>(*constructorDocument);
  sheet->SetReferrerInfo(referrerInfo);
  sheet->mConstructorDocument = constructorDocument;
  if (constructorDocument) {
    sheet->mRelevantGlobal = constructorDocument->GetParentObject();
  }

  // 2. Set the sheet's media according to aOptions.
  if (aOptions.mMedia.IsUTF8String()) {
    sheet->SetMedia(MediaList::Create(aOptions.mMedia.GetAsUTF8String()));
  } else {
    sheet->SetMedia(aOptions.mMedia.GetAsMediaList()->Clone());
  }

  // 3. Set the sheet's disabled flag according to aOptions.
  sheet->SetDisabled(aOptions.mDisabled);
  sheet->SetComplete();

  // 4. Return sheet.
  return sheet.forget();
}

StyleSheet::~StyleSheet() {
  MOZ_ASSERT(!mInner, "Inner should have been dropped in LastRelease");
}

bool StyleSheet::HasRules() const {
  return Servo_StyleSheet_HasRules(Inner().mContents);
}

Document* StyleSheet::GetAssociatedDocument() const {
  auto* associated = GetAssociatedDocumentOrShadowRoot();
  return associated ? associated->AsNode().OwnerDoc() : nullptr;
}

dom::DocumentOrShadowRoot* StyleSheet::GetAssociatedDocumentOrShadowRoot()
    const {
  const StyleSheet& outer = OutermostSheet();
  if (outer.mDocumentOrShadowRoot) {
    return outer.mDocumentOrShadowRoot;
  }
  if (outer.IsConstructed()) {
    return outer.mConstructorDocument;
  }
  return nullptr;
}

Document* StyleSheet::GetKeptAliveByDocument() const {
  const StyleSheet& outer = OutermostSheet();
  if (outer.mDocumentOrShadowRoot) {
    return outer.mDocumentOrShadowRoot->AsNode().GetComposedDoc();
  }
  if (outer.IsConstructed()) {
    for (DocumentOrShadowRoot* adopter : outer.mAdopters) {
      MOZ_ASSERT(adopter->AsNode().OwnerDoc() == outer.mConstructorDocument);
      if (adopter->AsNode().IsInComposedDoc()) {
        return outer.mConstructorDocument.get();
      }
    }
  }
  return nullptr;
}

void StyleSheet::LastRelease() {
  MOZ_DIAGNOSTIC_ASSERT(mAdopters.IsEmpty(),
                        "Should have no adopters at time of destruction.");

  if (mInner) {
    MOZ_ASSERT(mInner->mSheets.Contains(this), "Our mInner should include us.");
    mInner->RemoveSheet(this);
    mInner = nullptr;
  }

  DropMedia();
  DropRuleList();
}

void StyleSheet::UnlinkInner() {
  if (!mInner) {
    return;
  }

  // We can only have a cycle through our inner if we have a unique inner,
  // because otherwise there are no JS wrappers for anything in the inner.
  if (mInner->mSheets.Length() != 1) {
    mInner->RemoveSheet(this);
    mInner = nullptr;
    return;
  }

  for (StyleSheet* child : ChildSheets()) {
    MOZ_ASSERT(child->mParentSheet == this, "We have a unique inner!");
    child->mParentSheet = nullptr;
  }
  Inner().mChildren.Clear();
}

void StyleSheet::TraverseInner(nsCycleCollectionTraversalCallback& cb) {
  if (!mInner) {
    return;
  }

  for (StyleSheet* child : ChildSheets()) {
    if (child->mParentSheet == this) {
      NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "child sheet");
      cb.NoteXPCOMChild(child);
    }
  }
}

// QueryInterface implementation for StyleSheet
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(StyleSheet)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsICSSLoaderObserver)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(StyleSheet)
// We want to disconnect from our inner as soon as our refcount drops to zero,
// without waiting for async deletion by the cycle collector.  Otherwise we
// might end up cloning the inner if someone mutates another sheet that shares
// it with us, even though there is only one such sheet and we're about to go
// away.  This situation arises easily with sheet preloading.
NS_IMPL_CYCLE_COLLECTING_RELEASE_WITH_LAST_RELEASE(StyleSheet, LastRelease())

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(StyleSheet)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(StyleSheet)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mMedia)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mRuleList)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mRelevantGlobal)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mConstructorDocument)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mReplacePromise)
  tmp->TraverseInner(cb);
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(StyleSheet)
  tmp->DropMedia();
  tmp->UnlinkInner();
  tmp->DropRuleList();
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mRelevantGlobal)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mConstructorDocument)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mReplacePromise)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

dom::CSSStyleSheetParsingMode StyleSheet::ParsingModeDOM() {
#define CHECK_MODE(X, Y)                            \
  static_assert(                                    \
      static_cast<int>(X) == static_cast<int>(Y),   \
      "mozilla::dom::CSSStyleSheetParsingMode and " \
      "mozilla::css::SheetParsingMode should have identical values");

  CHECK_MODE(dom::CSSStyleSheetParsingMode::Agent, css::eAgentSheetFeatures);
  CHECK_MODE(dom::CSSStyleSheetParsingMode::User, css::eUserSheetFeatures);
  CHECK_MODE(dom::CSSStyleSheetParsingMode::Author, css::eAuthorSheetFeatures);

#undef CHECK_MODE

  return static_cast<dom::CSSStyleSheetParsingMode>(mParsingMode);
}

void StyleSheet::SetComplete() {
  // HasForcedUniqueInner() is okay if the sheet is constructed, because
  // constructed sheets are always unique and they may be set to complete
  // multiple times if their rules are replaced via Replace()
  MOZ_ASSERT(IsConstructed() || !HasForcedUniqueInner(),
             "Can't complete a sheet that's already been forced unique.");
  MOZ_ASSERT(!IsComplete(), "Already complete?");
  mState |= State::Complete;
  if (!Disabled()) {
    ApplicableStateChanged(true);
  }
  MaybeResolveReplacePromise();
}

void StyleSheet::ApplicableStateChanged(bool aApplicable) {
  MOZ_ASSERT(aApplicable == IsApplicable());
  auto Notify = [this](DocumentOrShadowRoot& target) {
    nsINode& node = target.AsNode();
    if (ShadowRoot* shadow = ShadowRoot::FromNode(node)) {
      shadow->StyleSheetApplicableStateChanged(*this);
    } else {
      node.AsDocument()->StyleSheetApplicableStateChanged(*this);
    }
  };

  const StyleSheet& sheet = OutermostSheet();
  if (sheet.mDocumentOrShadowRoot) {
    Notify(*sheet.mDocumentOrShadowRoot);
  }

  if (sheet.mConstructorDocument) {
    Notify(*sheet.mConstructorDocument);
  }

  for (DocumentOrShadowRoot* adopter : sheet.mAdopters) {
    MOZ_ASSERT(adopter, "adopters should never be null");
    if (adopter != sheet.mConstructorDocument) {
      Notify(*adopter);
    }
  }
}

void StyleSheet::SetDisabled(bool aDisabled) {
  if (IsReadOnly()) {
    return;
  }

  if (aDisabled == Disabled()) {
    return;
  }

  if (aDisabled) {
    mState |= State::Disabled;
  } else {
    mState &= ~State::Disabled;
  }

  if (IsComplete()) {
    ApplicableStateChanged(!aDisabled);
  }
}

void StyleSheet::SetURLExtraData() {
  Inner().mURLData =
      new URLExtraData(GetBaseURI(), GetReferrerInfo(), Principal());
}

nsISupports* StyleSheet::GetRelevantGlobal() const {
  const StyleSheet& outer = OutermostSheet();
  return outer.mRelevantGlobal;
}

StyleSheetInfo::StyleSheetInfo(CORSMode aCORSMode,
                               const SRIMetadata& aIntegrity,
                               css::SheetParsingMode aParsingMode)
    : mPrincipal(NullPrincipal::CreateWithoutOriginAttributes()),
      mCORSMode(aCORSMode),
      mReferrerInfo(new ReferrerInfo(nullptr)),
      mIntegrity(aIntegrity),
      mContents(Servo_StyleSheet_Empty(aParsingMode).Consume()),
      mURLData(URLExtraData::Dummy())
#ifdef DEBUG
      ,
      mPrincipalSet(false)
#endif
{
  if (!mPrincipal) {
    MOZ_CRASH("NullPrincipal::Init failed");
  }
  MOZ_COUNT_CTOR(StyleSheetInfo);
}

StyleSheetInfo::StyleSheetInfo(StyleSheetInfo& aCopy, StyleSheet* aPrimarySheet)
    : mSheetURI(aCopy.mSheetURI),
      mOriginalSheetURI(aCopy.mOriginalSheetURI),
      mBaseURI(aCopy.mBaseURI),
      mPrincipal(aCopy.mPrincipal),
      mCORSMode(aCopy.mCORSMode),
      mReferrerInfo(aCopy.mReferrerInfo),
      mIntegrity(aCopy.mIntegrity),
      // We don't rebuild the child because we're making a copy without
      // children.
      mSourceMapURL(aCopy.mSourceMapURL),
      mSourceMapURLFromComment(aCopy.mSourceMapURLFromComment),
      mSourceURL(aCopy.mSourceURL),
      mContents(Servo_StyleSheet_Clone(aCopy.mContents.get(), aPrimarySheet)
                    .Consume()),
      mURLData(aCopy.mURLData)
#ifdef DEBUG
      ,
      mPrincipalSet(aCopy.mPrincipalSet)
#endif
{
  AddSheet(aPrimarySheet);

  // Our child list is fixed up by our parent.
  MOZ_COUNT_CTOR(StyleSheetInfo);
}

StyleSheetInfo::~StyleSheetInfo() { MOZ_COUNT_DTOR(StyleSheetInfo); }

StyleSheetInfo* StyleSheetInfo::CloneFor(StyleSheet* aPrimarySheet) {
  return new StyleSheetInfo(*this, aPrimarySheet);
}

MOZ_DEFINE_MALLOC_SIZE_OF(ServoStyleSheetMallocSizeOf)
MOZ_DEFINE_MALLOC_ENCLOSING_SIZE_OF(ServoStyleSheetMallocEnclosingSizeOf)

size_t StyleSheetInfo::SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const {
  size_t n = aMallocSizeOf(this);

  n += Servo_StyleSheet_SizeOfIncludingThis(
      ServoStyleSheetMallocSizeOf, ServoStyleSheetMallocEnclosingSizeOf,
      mContents);

  return n;
}

void StyleSheetInfo::AddSheet(StyleSheet* aSheet) {
  mSheets.AppendElement(aSheet);
}

void StyleSheetInfo::RemoveSheet(StyleSheet* aSheet) {
  // Fix up the parent pointer in children lists.
  StyleSheet* newParent =
      aSheet == mSheets[0] ? mSheets.SafeElementAt(1) : mSheets[0];
  for (StyleSheet* child : mChildren) {
    MOZ_ASSERT(child->mParentSheet);
    MOZ_ASSERT(child->mParentSheet->mInner == this);
    if (child->mParentSheet == aSheet) {
      child->mParentSheet = newParent;
    }
  }

  if (1 == mSheets.Length()) {
    NS_ASSERTION(aSheet == mSheets.ElementAt(0), "bad parent");
    delete this;
    return;
  }

  mSheets.RemoveElement(aSheet);
}

void StyleSheet::GetType(nsAString& aType) { aType.AssignLiteral("text/css"); }

void StyleSheet::GetHref(nsAString& aHref, ErrorResult& aRv) {
  if (nsIURI* sheetURI = Inner().mOriginalSheetURI) {
    nsAutoCString str;
    nsresult rv = sheetURI->GetSpec(str);
    if (NS_FAILED(rv)) {
      aRv.Throw(rv);
      return;
    }
    CopyUTF8toUTF16(str, aHref);
  } else {
    SetDOMStringToNull(aHref);
  }
}

void StyleSheet::GetTitle(nsAString& aTitle) {
  // From https://drafts.csswg.org/cssom/#dom-stylesheet-title:
  //
  //    The title attribute must return the title or null if title is the empty
  //    string.
  //
  if (!mTitle.IsEmpty()) {
    aTitle.Assign(mTitle);
  } else {
    SetDOMStringToNull(aTitle);
  }
}

void StyleSheet::WillDirty() {
  MOZ_ASSERT(!IsReadOnly());

  if (IsComplete()) {
    EnsureUniqueInner();
  }
}

void StyleSheet::AddStyleSet(ServoStyleSet* aStyleSet) {
  MOZ_DIAGNOSTIC_ASSERT(!mStyleSets.Contains(aStyleSet),
                        "style set already registered");
  mStyleSets.AppendElement(aStyleSet);
}

void StyleSheet::DropStyleSet(ServoStyleSet* aStyleSet) {
  bool found = mStyleSets.RemoveElement(aStyleSet);
  MOZ_DIAGNOSTIC_ASSERT(found, "didn't find style set");
#ifndef MOZ_DIAGNOSTIC_ASSERT_ENABLED
  Unused << found;
#endif
}

// NOTE(emilio): Composed doc and containing shadow root are set in child sheets
// too, so no need to do it for each ancestor.
#define NOTIFY(function_, args_)                                          \
  do {                                                                    \
    StyleSheet* current = this;                                           \
    do {                                                                  \
      for (ServoStyleSet* set : current->mStyleSets) {                    \
        set->function_ args_;                                             \
      }                                                                   \
      if (auto* docOrShadow = current->mDocumentOrShadowRoot) {           \
        if (auto* shadow = ShadowRoot::FromNode(docOrShadow->AsNode())) { \
          shadow->function_ args_;                                        \
        } else {                                                          \
          docOrShadow->AsNode().AsDocument()->function_ args_;            \
        }                                                                 \
      }                                                                   \
      for (auto* adopter : mAdopters) {                                   \
        if (auto* shadow = ShadowRoot::FromNode(adopter->AsNode())) {     \
          shadow->function_ args_;                                        \
        } else {                                                          \
          adopter->AsNode().AsDocument()->function_ args_;                \
        }                                                                 \
      }                                                                   \
      current = current->mParentSheet;                                    \
    } while (current);                                                    \
  } while (0)

void StyleSheet::EnsureUniqueInner() {
  MOZ_ASSERT(mInner->mSheets.Length() != 0, "unexpected number of outers");

  if (IsReadOnly()) {
    // Sheets that can't be modified don't need a unique inner.
    return;
  }

  mState |= State::ForcedUniqueInner;

  if (HasUniqueInner()) {
    // already unique
    return;
  }

  StyleSheetInfo* clone = mInner->CloneFor(this);
  MOZ_ASSERT(clone);

  mInner->RemoveSheet(this);
  mInner = clone;

  // Fixup the child lists and parent links in the Servo sheet. This is done
  // here instead of in StyleSheetInner::CloneFor, because it's just more
  // convenient to do so instead.
  FixUpAfterInnerClone();

  // let our containing style sets know that if we call
  // nsPresContext::EnsureSafeToHandOutCSSRules we will need to restyle the
  // document
  NOTIFY(SheetCloned, (*this));
}

// WebIDL CSSStyleSheet API

dom::CSSRuleList* StyleSheet::GetCssRules(nsIPrincipal& aSubjectPrincipal,
                                          ErrorResult& aRv) {
  if (!AreRulesAvailable(aSubjectPrincipal, aRv)) {
    return nullptr;
  }
  return GetCssRulesInternal();
}

void StyleSheet::GetSourceMapURL(nsAString& aSourceMapURL) {
  if (mInner->mSourceMapURL.IsEmpty()) {
    aSourceMapURL = mInner->mSourceMapURLFromComment;
  } else {
    aSourceMapURL = mInner->mSourceMapURL;
  }
}

void StyleSheet::SetSourceMapURL(const nsAString& aSourceMapURL) {
  mInner->mSourceMapURL = aSourceMapURL;
}

void StyleSheet::SetSourceMapURLFromComment(
    const nsAString& aSourceMapURLFromComment) {
  mInner->mSourceMapURLFromComment = aSourceMapURLFromComment;
}

void StyleSheet::GetSourceURL(nsAString& aSourceURL) {
  aSourceURL = mInner->mSourceURL;
}

void StyleSheet::SetSourceURL(const nsAString& aSourceURL) {
  mInner->mSourceURL = aSourceURL;
}

already_AddRefed<css::Rule> StyleSheet::GetDOMOwnerRule() const {
  return do_AddRef(GetOwnerRule());
}

// https://drafts.csswg.org/cssom/#dom-cssstylesheet-insertrule
// https://wicg.github.io/construct-stylesheets/#dom-cssstylesheet-insertrule
uint32_t StyleSheet::InsertRule(const nsACString& aRule, uint32_t aIndex,
                                nsIPrincipal& aSubjectPrincipal,
                                ErrorResult& aRv) {
  if (IsReadOnly() || !AreRulesAvailable(aSubjectPrincipal, aRv)) {
    return 0;
  }

  if (ModificationDisallowed()) {
    aRv.ThrowNotAllowedError(
        "This method can only be called on "
        "modifiable style sheets");
    return 0;
  }

  return InsertRuleInternal(aRule, aIndex, aRv);
}

// https://drafts.csswg.org/cssom/#dom-cssstylesheet-deleterule
// https://wicg.github.io/construct-stylesheets/#dom-cssstylesheet-deleterule
void StyleSheet::DeleteRule(uint32_t aIndex, nsIPrincipal& aSubjectPrincipal,
                            ErrorResult& aRv) {
  if (IsReadOnly() || !AreRulesAvailable(aSubjectPrincipal, aRv)) {
    return;
  }

  if (ModificationDisallowed()) {
    return aRv.ThrowNotAllowedError(
        "This method can only be called on "
        "modifiable style sheets");
  }

  return DeleteRuleInternal(aIndex, aRv);
}

int32_t StyleSheet::AddRule(const nsACString& aSelector,
                            const nsACString& aBlock,
                            const Optional<uint32_t>& aIndex,
                            nsIPrincipal& aSubjectPrincipal, ErrorResult& aRv) {
  if (IsReadOnly() || !AreRulesAvailable(aSubjectPrincipal, aRv)) {
    return -1;
  }

  nsAutoCString rule;
  rule.Append(aSelector);
  rule.AppendLiteral(" { ");
  if (!aBlock.IsEmpty()) {
    rule.Append(aBlock);
    rule.Append(' ');
  }
  rule.Append('}');

  auto index =
      aIndex.WasPassed() ? aIndex.Value() : GetCssRulesInternal()->Length();

  InsertRuleInternal(rule, index, aRv);
  // Always return -1.
  return -1;
}

void StyleSheet::MaybeResolveReplacePromise() {
  MOZ_ASSERT(!!mReplacePromise == ModificationDisallowed());
  if (!mReplacePromise) {
    return;
  }

  SetModificationDisallowed(false);
  mReplacePromise->MaybeResolve(this);
  mReplacePromise = nullptr;
}

void StyleSheet::MaybeRejectReplacePromise() {
  MOZ_ASSERT(!!mReplacePromise == ModificationDisallowed());
  if (!mReplacePromise) {
    return;
  }

  SetModificationDisallowed(false);
  mReplacePromise->MaybeRejectWithNetworkError(
      "@import style sheet load failed");
  mReplacePromise = nullptr;
}

// https://wicg.github.io/construct-stylesheets/#dom-cssstylesheet-replace
already_AddRefed<dom::Promise> StyleSheet::Replace(const nsACString& aText,
                                                   ErrorResult& aRv) {
  nsIGlobalObject* globalObject = nullptr;
  const StyleSheet& outer = OutermostSheet();
  if (outer.mRelevantGlobal) {
    globalObject = outer.mRelevantGlobal;
  } else if (Document* doc = outer.GetAssociatedDocument()) {
    globalObject = doc->GetScopeObject();
  }

  RefPtr<dom::Promise> promise = dom::Promise::Create(globalObject, aRv);
  if (!promise) {
    return nullptr;
  }

  // Step 1 and 4 are variable declarations

  // 2.1 Check if sheet is constructed, else reject promise.
  if (!IsConstructed()) {
    promise->MaybeRejectWithNotAllowedError(
        "This method can only be called on "
        "constructed style sheets");
    return promise.forget();
  }

  // 2.2 Check if sheet is modifiable, else throw.
  if (ModificationDisallowed()) {
    promise->MaybeRejectWithNotAllowedError(
        "This method can only be called on "
        "modifiable style sheets");
    return promise.forget();
  }

  // 3. Disallow modifications until finished.
  SetModificationDisallowed(true);

  // TODO(emilio, 1642227): Should constructable stylesheets notify global
  // observers (i.e., set mMustNotify to true)?
  auto* loader = mConstructorDocument->CSSLoader();
  auto loadData = MakeRefPtr<css::SheetLoadData>(
      loader, nullptr, this, /* aSyncLoad */ false,
      css::Loader::UseSystemPrincipal::No, css::StylePreloadKind::None,
      /* aPreloadEncoding */ nullptr,
      /* aObserver */ nullptr, mConstructorDocument->NodePrincipal(),
      GetReferrerInfo());

  // In parallel
  // 5.1 Parse aText into rules.
  // 5.2 Load import rules, throw NetworkError if failed.
  // 5.3 Set sheet's rules to new rules.
  nsCOMPtr<nsISerialEventTarget> target =
      mConstructorDocument->EventTargetFor(TaskCategory::Other);
  loadData->mIsBeingParsed = true;
  MOZ_ASSERT(!mReplacePromise);
  mReplacePromise = promise;
  ParseSheet(*loader, aText, *loadData)
      ->Then(
          target, __func__,
          [loadData] { loadData->SheetFinishedParsingAsync(); },
          [] { MOZ_CRASH("This MozPromise should never be rejected."); });

  // 6. Return the promise
  return promise.forget();
}

// https://wicg.github.io/construct-stylesheets/#dom-cssstylesheet-replacesync
void StyleSheet::ReplaceSync(const nsACString& aText, ErrorResult& aRv) {
  // Step 1 is a variable declaration

  // 2.1 Check if sheet is constructed, else throw.
  if (!IsConstructed()) {
    return aRv.ThrowNotAllowedError(
        "Can only be called on constructed style sheets");
  }

  // 2.2 Check if sheet is modifiable, else throw.
  if (ModificationDisallowed()) {
    return aRv.ThrowNotAllowedError(
        "Can only be called on modifiable style sheets");
  }

  // 3. Parse aText into rules.
  // 4. If rules contain @imports, skip them and continue parsing.
  auto* loader = mConstructorDocument->CSSLoader();
  SetURLExtraData();
  RefPtr<const StyleStylesheetContents> rawContent =
      Servo_StyleSheet_FromUTF8Bytes(
          loader, this,
          /* load_data = */ nullptr, &aText, mParsingMode, Inner().mURLData,
          /* line_number_offset = */ 0,
          mConstructorDocument->GetCompatibilityMode(),
          /* reusable_sheets = */ nullptr,
          mConstructorDocument->GetStyleUseCounters(),
          StyleAllowImportRules::No, StyleSanitizationKind::None,
          /* sanitized_output = */ nullptr)
          .Consume();

  // 5. Set sheet's rules to the new rules.
  Inner().mContents = std::move(rawContent);
  FixUpRuleListAfterContentsChangeIfNeeded();
  FinishParse();
  RuleChanged(nullptr, StyleRuleChangeKind::Generic);
}

nsresult StyleSheet::DeleteRuleFromGroup(css::GroupRule* aGroup,
                                         uint32_t aIndex) {
  NS_ENSURE_ARG_POINTER(aGroup);
  NS_ASSERTION(IsComplete(), "No deleting from an incomplete sheet!");
  RefPtr<css::Rule> rule = aGroup->GetStyleRuleAt(aIndex);
  NS_ENSURE_TRUE(rule, NS_ERROR_ILLEGAL_VALUE);

  // check that the rule actually belongs to this sheet!
  if (this != rule->GetStyleSheet()) {
    return NS_ERROR_INVALID_ARG;
  }

  if (IsReadOnly()) {
    return NS_OK;
  }

  WillDirty();

  nsresult result = aGroup->DeleteStyleRuleAt(aIndex);
  NS_ENSURE_SUCCESS(result, result);

  rule->DropReferences();

  RuleRemoved(*rule);
  return NS_OK;
}

void StyleSheet::RuleAdded(css::Rule& aRule) {
  SetModifiedRules();
  NOTIFY(RuleAdded, (*this, aRule));
}

void StyleSheet::RuleRemoved(css::Rule& aRule) {
  SetModifiedRules();
  NOTIFY(RuleRemoved, (*this, aRule));
}

void StyleSheet::RuleChanged(css::Rule* aRule, StyleRuleChangeKind aKind) {
  MOZ_ASSERT(!aRule || HasUniqueInner(),
             "Shouldn't have mutated a shared sheet");
  SetModifiedRules();
  NOTIFY(RuleChanged, (*this, aRule, aKind));
}

// nsICSSLoaderObserver implementation
NS_IMETHODIMP
StyleSheet::StyleSheetLoaded(StyleSheet* aSheet, bool aWasDeferred,
                             nsresult aStatus) {
  if (!aSheet->GetParentSheet()) {
    return NS_OK;  // ignore if sheet has been detached already
  }
  MOZ_ASSERT(this == aSheet->GetParentSheet(),
             "We are being notified of a sheet load for a sheet that is not "
             "our child!");
  if (NS_FAILED(aStatus)) {
    return NS_OK;
  }

  MOZ_ASSERT(aSheet->GetOwnerRule());
  NOTIFY(ImportRuleLoaded, (*aSheet->GetOwnerRule(), *aSheet));
  return NS_OK;
}

#undef NOTIFY

nsresult StyleSheet::InsertRuleIntoGroup(const nsACString& aRule,
                                         css::GroupRule* aGroup,
                                         uint32_t aIndex) {
  NS_ASSERTION(IsComplete(), "No inserting into an incomplete sheet!");
  // check that the group actually belongs to this sheet!
  if (this != aGroup->GetStyleSheet()) {
    return NS_ERROR_INVALID_ARG;
  }

  if (IsReadOnly()) {
    return NS_OK;
  }

  if (ModificationDisallowed()) {
    return NS_ERROR_DOM_NOT_ALLOWED_ERR;
  }

  WillDirty();

  nsresult result = InsertRuleIntoGroupInternal(aRule, aGroup, aIndex);
  NS_ENSURE_SUCCESS(result, result);
  RuleAdded(*aGroup->GetStyleRuleAt(aIndex));
  return NS_OK;
}

uint64_t StyleSheet::FindOwningWindowInnerID() const {
  uint64_t windowID = 0;
  if (Document* doc = GetAssociatedDocument()) {
    windowID = doc->InnerWindowID();
  }

  if (windowID == 0 && mOwningNode) {
    windowID = mOwningNode->OwnerDoc()->InnerWindowID();
  }

  RefPtr<css::Rule> ownerRule;
  if (windowID == 0 && (ownerRule = GetDOMOwnerRule())) {
    RefPtr<StyleSheet> sheet = ownerRule->GetStyleSheet();
    if (sheet) {
      windowID = sheet->FindOwningWindowInnerID();
    }
  }

  if (windowID == 0 && mParentSheet) {
    windowID = mParentSheet->FindOwningWindowInnerID();
  }

  return windowID;
}

void StyleSheet::RemoveFromParent() {
  if (!mParentSheet) {
    return;
  }

  MOZ_ASSERT(mParentSheet->ChildSheets().Contains(this));
  mParentSheet->Inner().mChildren.RemoveElement(this);
  mParentSheet = nullptr;
}

void StyleSheet::SubjectSubsumesInnerPrincipal(nsIPrincipal& aSubjectPrincipal,
                                               ErrorResult& aRv) {
  StyleSheetInfo& info = Inner();

  if (aSubjectPrincipal.Subsumes(info.mPrincipal)) {
    return;
  }

  // Allow access only if CORS mode is not NONE and the security flag
  // is not turned off.
  if (GetCORSMode() == CORS_NONE && !nsContentUtils::BypassCSSOMOriginCheck()) {
    aRv.ThrowSecurityError("Not allowed to access cross-origin stylesheet");
    return;
  }

  // Now make sure we set the principal of our inner to the subjectPrincipal.
  // We do this because we're in a situation where the caller would not normally
  // be able to access the sheet, but the sheet has opted in to being read.
  // Unfortunately, that means it's also opted in to being _edited_, and if the
  // caller now makes edits to the sheet we want the resulting resource loads,
  // if any, to look as if they are coming from the caller's principal, not the
  // original sheet principal.
  //
  // That means we need a unique inner, of course.  But we don't want to do that
  // if we're not complete yet.  Luckily, all the callers of this method throw
  // anyway if not complete, so we can just do that here too.
  if (!IsComplete()) {
    aRv.ThrowInvalidAccessError(
        "Not allowed to access still-loading stylesheet");
    return;
  }

  WillDirty();

  info.mPrincipal = &aSubjectPrincipal;
}

bool StyleSheet::AreRulesAvailable(nsIPrincipal& aSubjectPrincipal,
                                   ErrorResult& aRv) {
  // Rules are not available on incomplete sheets.
  if (!IsComplete()) {
    aRv.ThrowInvalidAccessError(
        "Can't access rules of still-loading style sheet");
    return false;
  }
  //-- Security check: Only scripts whose principal subsumes that of the
  //   style sheet can access rule collections.
  SubjectSubsumesInnerPrincipal(aSubjectPrincipal, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return false;
  }
  return true;
}

void StyleSheet::SetAssociatedDocumentOrShadowRoot(
    DocumentOrShadowRoot* aDocOrShadowRoot) {
  MOZ_ASSERT(!IsConstructed());
  MOZ_ASSERT(!mParentSheet || !aDocOrShadowRoot,
             "Shouldn't be set on child sheets");

  // not ref counted
  mDocumentOrShadowRoot = aDocOrShadowRoot;

  if (Document* doc = GetAssociatedDocument()) {
    MOZ_ASSERT(!mRelevantGlobal);
    mRelevantGlobal = doc->GetScopeObject();
  }
}

void StyleSheet::AppendStyleSheet(StyleSheet& aSheet) {
  WillDirty();
  AppendStyleSheetSilently(aSheet);
}

void StyleSheet::AppendStyleSheetSilently(StyleSheet& aSheet) {
  MOZ_ASSERT(!IsReadOnly());

  Inner().mChildren.AppendElement(&aSheet);

  // This is not reference counted. Our parent tells us when
  // it's going away.
  aSheet.mParentSheet = this;
}

size_t StyleSheet::SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const {
  size_t n = 0;
  n += aMallocSizeOf(this);

  // We want to measure the inner with only one of the children, and it makes
  // sense for it to be the latest as it is the most likely to be reachable.
  if (Inner().mSheets.LastElement() == this) {
    n += Inner().SizeOfIncludingThis(aMallocSizeOf);
  }

  // Measurement of the following members may be added later if DMD finds it
  // is worthwhile:
  // - mTitle
  // - mMedia
  // - mStyleSets
  // - mRuleList

  return n;
}

#if defined(DEBUG) || defined(MOZ_LAYOUT_DEBUGGER)
void StyleSheet::List(FILE* aOut, int32_t aIndent) {
  for (StyleSheet* child : ChildSheets()) {
    child->List(aOut, aIndent);
  }

  nsCString line;
  for (int i = 0; i < aIndent; ++i) {
    line.AppendLiteral("  ");
  }

  line.AppendLiteral("/* ");

  nsCString url;
  GetSheetURI()->GetSpec(url);
  if (url.IsEmpty()) {
    line.AppendLiteral("(no URL)");
  } else {
    line.Append(url);
  }

  line.AppendLiteral(" (");

  switch (GetOrigin()) {
    case StyleOrigin::UserAgent:
      line.AppendLiteral("User Agent");
      break;
    case StyleOrigin::User:
      line.AppendLiteral("User");
      break;
    case StyleOrigin::Author:
      line.AppendLiteral("Author");
      break;
  }

  if (mMedia) {
    nsAutoCString buffer;
    mMedia->GetText(buffer);

    if (!buffer.IsEmpty()) {
      line.AppendLiteral(", ");
      line.Append(buffer);
    }
  }

  line.AppendLiteral(") */");

  fprintf_stderr(aOut, "%s\n\n", line.get());

  nsCString newlineIndent;
  newlineIndent.Append('\n');
  for (int i = 0; i < aIndent; ++i) {
    newlineIndent.AppendLiteral("  ");
  }

  ServoCSSRuleList* ruleList = GetCssRulesInternal();
  for (uint32_t i = 0, len = ruleList->Length(); i < len; ++i) {
    css::Rule* rule = ruleList->GetRule(i);

    nsAutoCString cssText;
    rule->GetCssText(cssText);
    cssText.ReplaceSubstring("\n"_ns, newlineIndent);
    fprintf_stderr(aOut, "%s\n", cssText.get());
  }

  if (ruleList->Length() != 0) {
    fprintf_stderr(aOut, "\n");
  }
}
#endif

void StyleSheet::SetMedia(already_AddRefed<dom::MediaList> aMedia) {
  mMedia = aMedia;
  if (mMedia) {
    mMedia->SetStyleSheet(this);
  }
}

void StyleSheet::DropMedia() {
  if (mMedia) {
    mMedia->SetStyleSheet(nullptr);
    mMedia = nullptr;
  }
}

dom::MediaList* StyleSheet::Media() {
  if (!mMedia) {
    mMedia = dom::MediaList::Create(EmptyCString());
    mMedia->SetStyleSheet(this);
  }

  return mMedia;
}

// nsWrapperCache

JSObject* StyleSheet::WrapObject(JSContext* aCx,
                                 JS::Handle<JSObject*> aGivenProto) {
  return dom::CSSStyleSheet_Binding::Wrap(aCx, this, aGivenProto);
}

void StyleSheet::FixUpRuleListAfterContentsChangeIfNeeded(bool aFromClone) {
  if (!mRuleList) {
    return;
  }

  RefPtr<StyleLockedCssRules> rules =
      Servo_StyleSheet_GetRules(Inner().mContents.get()).Consume();
  mRuleList->SetRawContents(std::move(rules), aFromClone);
}

void StyleSheet::FixUpAfterInnerClone() {
  MOZ_ASSERT(Inner().mSheets.Length() == 1, "Should've just cloned");
  MOZ_ASSERT(Inner().mSheets[0] == this);
  MOZ_ASSERT(Inner().mChildren.IsEmpty());

  FixUpRuleListAfterContentsChangeIfNeeded(/* aFromClone = */ true);

  RefPtr<StyleLockedCssRules> rules =
      Servo_StyleSheet_GetRules(Inner().mContents.get()).Consume();
  uint32_t index = 0;
  while (true) {
    uint32_t line, column;  // Actually unused.
    RefPtr<StyleLockedImportRule> import =
        Servo_CssRules_GetImportRuleAt(rules, index, &line, &column).Consume();
    if (!import) {
      // Note that only @charset rules come before @import rules, and @charset
      // rules are parsed but skipped, so we can stop iterating as soon as we
      // find something that isn't an @import rule.
      break;
    }
    auto* sheet = const_cast<StyleSheet*>(Servo_ImportRule_GetSheet(import));
    MOZ_ASSERT(sheet);
    AppendStyleSheetSilently(*sheet);
    index++;
  }
}

already_AddRefed<StyleSheet> StyleSheet::CreateEmptyChildSheet(
    already_AddRefed<dom::MediaList> aMediaList) const {
  RefPtr<StyleSheet> child =
      new StyleSheet(ParsingMode(), CORSMode::CORS_NONE, SRIMetadata());

  child->mMedia = aMediaList;
  return child.forget();
}

// We disable parallel stylesheet parsing if any of the following three
// conditions hold:
//
// (1) The pref is off.
// (2) The browser is recording CSS errors (which parallel parsing can't
//     handle).
// (3) The stylesheet is a chrome stylesheet, since those can use
//     -moz-bool-pref, which needs to access the pref service, which is not
//     threadsafe.
static bool AllowParallelParse(css::Loader& aLoader, URLExtraData* aUrlData) {
  // If the browser is recording CSS errors, we need to use the sequential path
  // because the parallel path doesn't support that.
  Document* doc = aLoader.GetDocument();
  if (doc && css::ErrorReporter::ShouldReportErrors(*doc)) {
    return false;
  }

  // If this is a chrome stylesheet, it might use -moz-bool-pref, which needs to
  // access the pref service, which is not thread-safe. We could probably expose
  // the relevant booleans as thread-safe var caches if we needed to, but
  // parsing chrome stylesheets in parallel is unlikely to be a win anyway.
  //
  // Note that UA stylesheets can also use -moz-bool-pref, but those are always
  // parsed sync.
  if (aUrlData->ChromeRulesEnabled()) {
    return false;
  }

  return true;
}

RefPtr<StyleSheetParsePromise> StyleSheet::ParseSheet(
    css::Loader& aLoader, const nsACString& aBytes,
    css::SheetLoadData& aLoadData) {
  MOZ_ASSERT(mParsePromise.IsEmpty());
  RefPtr<StyleSheetParsePromise> p = mParsePromise.Ensure(__func__);
  if (!aLoadData.ShouldDefer()) {
    mParsePromise.SetTaskPriority(nsIRunnablePriority::PRIORITY_RENDER_BLOCKING,
                                  __func__);
  }
  SetURLExtraData();

  // @import rules are disallowed due to this decision:
  // https://github.com/WICG/construct-stylesheets/issues/119#issuecomment-588352418
  // We may allow @import rules again in the future.
  auto allowImportRules =
      IsConstructed() ? StyleAllowImportRules::No : StyleAllowImportRules::Yes;
  const bool shouldRecordCounters =
      aLoader.GetDocument() && aLoader.GetDocument()->GetStyleUseCounters();
  if (!AllowParallelParse(aLoader, Inner().mURLData)) {
    UniquePtr<StyleUseCounters> counters;
    if (shouldRecordCounters) {
      counters.reset(Servo_UseCounters_Create());
    }

    RefPtr<StyleStylesheetContents> contents =
        Servo_StyleSheet_FromUTF8Bytes(
            &aLoader, this, &aLoadData, &aBytes, mParsingMode, Inner().mURLData,
            aLoadData.mLineNumber, aLoadData.mCompatMode,
            /* reusable_sheets = */ nullptr, counters.get(), allowImportRules,
            StyleSanitizationKind::None,
            /* sanitized_output = */ nullptr)
            .Consume();
    FinishAsyncParse(contents.forget(), std::move(counters));
  } else {
    auto holder = MakeRefPtr<css::SheetLoadDataHolder>(__func__, &aLoadData);
    Servo_StyleSheet_FromUTF8BytesAsync(
        holder, Inner().mURLData, &aBytes, mParsingMode, aLoadData.mLineNumber,
        aLoadData.mCompatMode, shouldRecordCounters, allowImportRules);
  }

  return p;
}

void StyleSheet::FinishAsyncParse(
    already_AddRefed<StyleStylesheetContents> aSheetContents,
    UniquePtr<StyleUseCounters> aUseCounters) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!mParsePromise.IsEmpty());
  Inner().mContents = aSheetContents;
  Inner().mUseCounters = std::move(aUseCounters);
  FinishParse();
  mParsePromise.Resolve(true, __func__);
}

void StyleSheet::ParseSheetSync(
    css::Loader* aLoader, const nsACString& aBytes,
    css::SheetLoadData* aLoadData, uint32_t aLineNumber,
    css::LoaderReusableStyleSheets* aReusableSheets) {
  const nsCompatibility compatMode = [&] {
    if (aLoadData) {
      return aLoadData->mCompatMode;
    }
    if (aLoader) {
      return aLoader->CompatMode(css::StylePreloadKind::None);
    }
    return eCompatibility_FullStandards;
  }();

  const StyleUseCounters* useCounters =
      aLoader && aLoader->GetDocument()
          ? aLoader->GetDocument()->GetStyleUseCounters()
          : nullptr;

  auto allowImportRules =
      IsConstructed() ? StyleAllowImportRules::No : StyleAllowImportRules::Yes;

  SetURLExtraData();
  Inner().mContents =
      Servo_StyleSheet_FromUTF8Bytes(
          aLoader, this, aLoadData, &aBytes, mParsingMode, Inner().mURLData,
          aLineNumber, compatMode, aReusableSheets, useCounters,
          allowImportRules, StyleSanitizationKind::None,
          /* sanitized_output = */ nullptr)
          .Consume();

  FinishParse();
}

void StyleSheet::FinishParse() {
  nsString sourceMapURL;
  Servo_StyleSheet_GetSourceMapURL(Inner().mContents, &sourceMapURL);
  SetSourceMapURLFromComment(sourceMapURL);

  nsString sourceURL;
  Servo_StyleSheet_GetSourceURL(Inner().mContents, &sourceURL);
  SetSourceURL(sourceURL);
}

void StyleSheet::ReparseSheet(const nsACString& aInput, ErrorResult& aRv) {
  if (!IsComplete()) {
    return aRv.ThrowInvalidAccessError("Cannot reparse still-loading sheet");
  }

  // Allowing to modify UA sheets is dangerous (in the sense that C++ code
  // relies on rules in those sheets), plus they're probably going to be shared
  // across processes in which case this is directly a no-go.
  if (IsReadOnly()) {
    return;
  }

  // Hold strong ref to the CSSLoader in case the document update
  // kills the document
  RefPtr<css::Loader> loader;
  if (Document* doc = GetAssociatedDocument()) {
    loader = doc->CSSLoader();
    NS_ASSERTION(loader, "Document with no CSS loader!");
  } else {
    loader = new css::Loader;
  }

  WillDirty();

  // cache child sheets to reuse
  css::LoaderReusableStyleSheets reusableSheets;
  for (StyleSheet* child : ChildSheets()) {
    if (child->GetOriginalURI()) {
      reusableSheets.AddReusableSheet(child);
    }
  }

  // Clean up child sheets list.
  for (StyleSheet* child : ChildSheets()) {
    child->mParentSheet = nullptr;
  }
  Inner().mChildren.Clear();

  uint32_t lineNumber = 1;
  if (auto* linkStyle = LinkStyle::FromNodeOrNull(mOwningNode)) {
    lineNumber = linkStyle->GetLineNumber();
  }

  // Notify to the stylesets about the old rules going away.
  {
    ServoCSSRuleList* ruleList = GetCssRulesInternal();
    MOZ_ASSERT(ruleList);

    uint32_t ruleCount = ruleList->Length();
    for (uint32_t i = 0; i < ruleCount; ++i) {
      css::Rule* rule = ruleList->GetRule(i);
      MOZ_ASSERT(rule);
      RuleRemoved(*rule);
    }

    // We need to clear the rule list here (rather than after parsing) because
    // ParseSheetSync may reuse child sheets, which would cause us to end up
    // with a wrong mChilden array.
    ruleList->SetRawContents(nullptr, /* aFromClone = */ false);
  }

  ParseSheetSync(loader, aInput, /* aLoadData = */ nullptr, lineNumber,
                 &reusableSheets);

  FixUpRuleListAfterContentsChangeIfNeeded();

  // Notify the stylesets about the new rules.
  {
    // Get the rule list (which will need to be regenerated after ParseSheet).
    ServoCSSRuleList* ruleList = GetCssRulesInternal();
    MOZ_ASSERT(ruleList);

    uint32_t ruleCount = ruleList->Length();
    for (uint32_t i = 0; i < ruleCount; ++i) {
      css::Rule* rule = ruleList->GetRule(i);
      MOZ_ASSERT(rule);
      RuleAdded(*rule);
    }
  }

  // Our rules are no longer considered modified for devtools.
  mState &= ~State::ModifiedRulesForDevtools;
}

void StyleSheet::DropRuleList() {
  if (mRuleList) {
    mRuleList->DropReferences();
    mRuleList = nullptr;
  }
}

already_AddRefed<StyleSheet> StyleSheet::Clone(
    StyleSheet* aCloneParent,
    dom::DocumentOrShadowRoot* aCloneDocumentOrShadowRoot) const {
  MOZ_ASSERT(!IsConstructed(),
             "Cannot create a non-constructed sheet from a constructed sheet");
  RefPtr<StyleSheet> clone =
      new StyleSheet(*this, aCloneParent, aCloneDocumentOrShadowRoot,
                     /* aConstructorDocToUse */ nullptr);
  return clone.forget();
}

already_AddRefed<StyleSheet> StyleSheet::CloneAdoptedSheet(
    Document& aConstructorDocument) const {
  MOZ_ASSERT(IsConstructed(),
             "Cannot create a constructed sheet from a non-constructed sheet");
  MOZ_ASSERT(aConstructorDocument.IsStaticDocument(),
             "Should never clone adopted sheets for a non-static document");
  RefPtr<StyleSheet> clone = new StyleSheet(*this,
                                            /* aParentSheetToUse */ nullptr,
                                            /* aDocOrShadowRootToUse */ nullptr,
                                            &aConstructorDocument);
  return clone.forget();
}

ServoCSSRuleList* StyleSheet::GetCssRulesInternal() {
  if (!mRuleList) {
    // TODO(emilio): This should go away, but we need to fix the CC setup for
    // @import rules first, see bug 1719963.
    EnsureUniqueInner();

    RefPtr<StyleLockedCssRules> rawRules =
        Servo_StyleSheet_GetRules(Inner().mContents).Consume();
    MOZ_ASSERT(rawRules);
    mRuleList = new ServoCSSRuleList(rawRules.forget(), this, nullptr);
  }
  return mRuleList;
}

uint32_t StyleSheet::InsertRuleInternal(const nsACString& aRule,
                                        uint32_t aIndex, ErrorResult& aRv) {
  MOZ_ASSERT(!IsReadOnly());
  MOZ_ASSERT(!ModificationDisallowed());

  // Ensure mRuleList is constructed.
  GetCssRulesInternal();

  aRv = mRuleList->InsertRule(aRule, aIndex);
  if (aRv.Failed()) {
    return 0;
  }

  // XXX We may not want to get the rule when stylesheet change event
  // is not enabled.
  css::Rule* rule = mRuleList->GetRule(aIndex);
  RuleAdded(*rule);

  return aIndex;
}

void StyleSheet::DeleteRuleInternal(uint32_t aIndex, ErrorResult& aRv) {
  MOZ_ASSERT(!IsReadOnly());
  MOZ_ASSERT(!ModificationDisallowed());

  // Ensure mRuleList is constructed.
  GetCssRulesInternal();
  if (aIndex >= mRuleList->Length()) {
    aRv.ThrowIndexSizeError(
        nsPrintfCString("Cannot delete rule at index %u"
                        " because the number of rules is only %u",
                        aIndex, mRuleList->Length()));
    return;
  }

  // Hold a strong ref to the rule so it doesn't die when we remove it
  // from the list. XXX We may not want to hold it if stylesheet change
  // event is not enabled.
  RefPtr<css::Rule> rule = mRuleList->GetRule(aIndex);
  aRv = mRuleList->DeleteRule(aIndex);
  if (!aRv.Failed()) {
    RuleRemoved(*rule);
  }
}

nsresult StyleSheet::InsertRuleIntoGroupInternal(const nsACString& aRule,
                                                 css::GroupRule* aGroup,
                                                 uint32_t aIndex) {
  MOZ_ASSERT(!IsReadOnly());

  ServoCSSRuleList* rules = aGroup->GetCssRules();
  MOZ_ASSERT(rules && rules->GetParentRule() == aGroup);
  return rules->InsertRule(aRule, aIndex);
}

StyleOrigin StyleSheet::GetOrigin() const {
  return Servo_StyleSheet_GetOrigin(Inner().mContents);
}

void StyleSheet::SetSharedContents(const StyleLockedCssRules* aSharedRules) {
  MOZ_ASSERT(!IsComplete());

  SetURLExtraData();

  Inner().mContents =
      Servo_StyleSheet_FromSharedData(Inner().mURLData, aSharedRules).Consume();

  // Don't call FinishParse(), since that tries to set source map URLs,
  // which we don't have.
}

const StyleLockedCssRules* StyleSheet::ToShared(
    StyleSharedMemoryBuilder* aBuilder, nsCString& aErrorMessage) {
  // Assert some things we assume when creating a StyleSheet using shared
  // memory.
  MOZ_ASSERT(GetReferrerInfo()->ReferrerPolicy() == ReferrerPolicy::_empty);
  MOZ_ASSERT(GetReferrerInfo()->GetSendReferrer());
  MOZ_ASSERT(!nsCOMPtr<nsIURI>(GetReferrerInfo()->GetComputedReferrer()));
  MOZ_ASSERT(GetCORSMode() == CORS_NONE);
  MOZ_ASSERT(Inner().mIntegrity.IsEmpty());
  MOZ_ASSERT(Principal()->IsSystemPrincipal());

  const StyleLockedCssRules* rules = Servo_SharedMemoryBuilder_AddStylesheet(
      aBuilder, Inner().mContents, &aErrorMessage);

#ifdef DEBUG
  if (!rules) {
    // Print the ToShmem error message so that developers know what to fix.
    printf_stderr("%s\n", aErrorMessage.get());
    MOZ_CRASH("UA style sheet contents failed shared memory requirements");
  }
#endif

  return rules;
}

bool StyleSheet::IsReadOnly() const {
  return IsComplete() && GetOrigin() == StyleOrigin::UserAgent;
}

}  // namespace mozilla
