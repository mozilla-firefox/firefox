/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_StyleSheet_h
#define mozilla_StyleSheet_h

#include "mozilla/Assertions.h"
#include "mozilla/CORSMode.h"
#include "mozilla/MozPromise.h"
#include "mozilla/RefPtr.h"
#include "mozilla/ServoBindingTypes.h"
#include "mozilla/ServoTypes.h"
#include "mozilla/StyleSheetInfo.h"
#include "mozilla/dom/CSSStyleSheetBinding.h"
#include "mozilla/dom/SRIMetadata.h"
#include "nsICSSLoaderObserver.h"
#include "nsIPrincipal.h"
#include "nsProxyRelease.h"
#include "nsStringFwd.h"
#include "nsWrapperCache.h"

class nsIGlobalObject;
class nsINode;
class nsIPrincipal;
struct StyleLockedCssRules;
class nsIReferrerInfo;

namespace mozilla {

class ServoCSSRuleList;
class ServoStyleSet;
struct StyleLockedDeclarationBlock;

using StyleSheetParsePromise = MozPromise</* Dummy */ bool,
                                          /* Dummy */ bool,
                                          /* IsExclusive = */ true>;

enum class StyleRuleChangeKind : uint32_t;
enum class StyleNonLocalUriDependency : uint8_t;

struct StyleRuleChange {
  StyleRuleChange() = delete;
  MOZ_IMPLICIT StyleRuleChange(StyleRuleChangeKind aKind) : mKind(aKind) {}
  // Only relevant for Kind::*Declarations.
  StyleRuleChange(StyleRuleChangeKind aKind,
                  const StyleLockedDeclarationBlock* aOldBlock,
                  const StyleLockedDeclarationBlock* aNewBlock)
      : mKind(aKind), mOldBlock(aOldBlock), mNewBlock(aNewBlock) {}

  const StyleRuleChangeKind mKind;
  // mOldBlock and mNewBlock can be the same object.
  const StyleLockedDeclarationBlock* const mOldBlock = nullptr;
  const StyleLockedDeclarationBlock* const mNewBlock = nullptr;
};

namespace css {
class GroupRule;
class Loader;
class LoaderReusableStyleSheets;
class Rule;
class SheetLoadData;
using SheetLoadDataHolder = nsMainThreadPtrHolder<SheetLoadData>;
}  // namespace css

namespace dom {
class CSSImportRule;
class CSSRuleList;
class DocumentOrShadowRoot;
class MediaList;
class ShadowRoot;
struct CSSStyleSheetInit;
}  // namespace dom

enum class StyleSheetState : uint8_t {
  // Whether the sheet is disabled. Sheets can be made disabled via CSSOM, or
  // via alternate links and such.
  Disabled = 1 << 0,
  // Whether the sheet is complete. The sheet is complete if it's finished
  // loading. See StyleSheet::SetComplete.
  Complete = 1 << 1,
  // Whether we've forced a unique inner. StyleSheet objects share an 'inner'
  // StyleSheetInfo object if they share URL, CORS mode, etc.
  //
  // See the Loader's `mCompleteSheets` and `mLoadingSheets`.
  ForcedUniqueInner = 1 << 2,
  // Whether this stylesheet has suffered any modification to the rules via
  // CSSOM.
  ModifiedRules = 1 << 3,
  // Same flag, but devtools clears it in some specific situations.
  //
  // Used to control whether devtools shows the rule in its authored form or
  // not.
  ModifiedRulesForDevtools = 1 << 4,
  // Whether modifications to the sheet are currently disallowed.
  // This flag is set during the async Replace() function to ensure
  // that the sheet is not modified until the promise is resolved.
  ModificationDisallowed = 1 << 5,
};

MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS(StyleSheetState)

class StyleSheet final : public nsICSSLoaderObserver, public nsWrapperCache {
  StyleSheet(const StyleSheet& aCopy, StyleSheet* aParentSheetToUse,
             dom::DocumentOrShadowRoot* aDocOrShadowRootToUse,
             dom::Document* aConstructorDocToUse);

  virtual ~StyleSheet();

  using State = StyleSheetState;

 public:
  StyleSheet(StyleOrigin, CORSMode, const dom::SRIMetadata& aIntegrity);

  static already_AddRefed<StyleSheet> Constructor(const dom::GlobalObject&,
                                                  const dom::CSSStyleSheetInit&,
                                                  ErrorResult&);

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(StyleSheet)

  static already_AddRefed<StyleSheet> CreateConstructedSheet(
      dom::Document& aConstructorDocument, nsIURI* aBaseURI,
      const dom::CSSStyleSheetInit& aOptions, ErrorResult& aError);

  already_AddRefed<StyleSheet> CreateEmptyChildSheet(
      already_AddRefed<dom::MediaList> aMediaList) const;

  bool HasRules() const;

  // Parses a stylesheet. The load data argument corresponds to the
  // SheetLoadData for this stylesheet.
  // NOTE: ParseSheet can run synchronously or asynchronously
  //       based on the result of `AllowParallelParse`
  RefPtr<StyleSheetParsePromise> ParseSheet(
      css::Loader&, const nsACString& aBytes,
      const RefPtr<css::SheetLoadDataHolder>& aLoadData);

  // Common code that needs to be called after servo finishes parsing. This is
  // shared between the parallel and sequential paths.
  void FinishAsyncParse(already_AddRefed<StyleStylesheetContents>);

  // Similar to `ParseSheet`, but guarantees that
  // parsing will be performed synchronously.
  // NOTE: ParseSheet can still run synchronously.
  //       This is not a strict alternative.
  //
  // The load data may be null sometimes.
  void ParseSheetSync(
      css::Loader* aLoader, const nsACString& aBytes,
      css::SheetLoadData* aLoadData,
      css::LoaderReusableStyleSheets* aReusableSheets = nullptr);

  void ReparseSheet(const nsACString& aInput, ErrorResult& aRv);

  const StyleStylesheetContents* RawContents() const {
    return Inner().mContents;
  }

  const StyleUseCounters* UseCounters() const;
  void PropagateUseCountersTo(dom::Document*) const;

  // Whether our original contents may be using relative URIs.
  StyleNonLocalUriDependency OriginalContentsUriDependency() const;

  URLExtraData* URLData() const { return mURLData.get(); }

  // nsICSSLoaderObserver interface
  NS_IMETHOD StyleSheetLoaded(StyleSheet* aSheet, bool aWasDeferred,
                              nsresult aStatus) final;

  // Internal GetCssRules methods which do not have security check and
  // completeness check.
  ServoCSSRuleList* GetCssRulesInternal();

  // Returns the stylesheet's Servo origin as a StyleOrigin value.
  StyleOrigin GetOrigin() const;

  void SetOwningNode(nsINode* aOwningNode) { mOwningNode = aOwningNode; }

  dom::CSSStyleSheetParsingMode ParsingModeDOM();

  /**
   * Whether the sheet is complete.
   */
  bool IsComplete() const { return bool(mState & State::Complete); }

  void SetComplete();

  void SetEnabled(bool aEnabled) { SetDisabled(!aEnabled); }

  // Whether the sheet is for an inline <style> element.
  bool IsInline() const { return !GetOriginalURI(); }
  /**
   * Get the URI this sheet was originally loaded from, if any. Can return null.
   */
  nsIURI* GetOriginalURI() const { return mOriginalSheetURI; }
  nsIURI* GetBaseURI() const;

  void SetURIs(nsIURI* aOriginalSheetURI, nsIURI* aBaseURI,
               nsIReferrerInfo* aReferrerInfo, nsIPrincipal* aPrincipal);

  void SetOriginClean(bool aValue) { Inner().mOriginClean = aValue; }
  bool IsOriginClean() const { return Inner().mOriginClean; }

  /**
   * Whether the sheet is applicable.  A sheet that is not applicable
   * should never be inserted into a style set.  A sheet may not be
   * applicable for a variety of reasons including being disabled and
   * being incomplete.
   */
  bool IsApplicable() const { return !Disabled() && IsComplete(); }

  already_AddRefed<StyleSheet> Clone(
      StyleSheet* aCloneParent,
      dom::DocumentOrShadowRoot* aCloneDocumentOrShadowRoot) const;

  /**
   * Creates a clone of the adopted style sheet as though it were constructed
   * by aConstructorDocument. This should only be used for printing.
   */
  already_AddRefed<StyleSheet> CloneAdoptedSheet(
      dom::Document& aConstructorDocument) const;

  bool HasForcedUniqueInner() const {
    return bool(mState & State::ForcedUniqueInner);
  }

  bool HasModifiedRules() const { return bool(mState & State::ModifiedRules); }

  bool HasModifiedRulesForDevtools() const {
    return bool(mState & State::ModifiedRulesForDevtools);
  }

  bool HasUniqueInner() const { return Inner().mSheets.Length() == 1; }

  void AssertHasUniqueInner() const { MOZ_ASSERT(HasUniqueInner()); }

  void EnsureUniqueInner();

  // Returns the DocumentOrShadowRoot* that owns us, if any.
  //
  // TODO(emilio): Maybe rename to GetOwner*() or such? Might be
  // confusing with nsINode::OwnerDoc and such.
  dom::DocumentOrShadowRoot* GetAssociatedDocumentOrShadowRoot() const;

  // Whether this stylesheet is kept alive by the associated or constructor
  // document somehow, and thus at least has the same lifetime as
  // GetAssociatedDocument().
  dom::Document* GetKeptAliveByDocument() const;

  // If this is a constructed style sheet, return mConstructorDocument.
  // Otherwise return the document we're associated to,
  // via mDocumentOrShadowRoot.
  //
  // Non-null iff GetAssociatedDocumentOrShadowRoot is non-null.
  dom::Document* GetAssociatedDocument() const;

  void SetAssociatedDocumentOrShadowRoot(dom::DocumentOrShadowRoot*);
  void ClearAssociatedDocumentOrShadowRoot() {
    SetAssociatedDocumentOrShadowRoot(nullptr);
  }

  nsINode* GetOwnerNode() const { return mOwningNode; }

  nsINode* GetOwnerNodeOfOutermostSheet() const {
    return OutermostSheet().GetOwnerNode();
  }

  StyleSheet* GetParentSheet() const { return mParentSheet; }

  void AddReferencingRule(dom::CSSImportRule& aRule) {
    MOZ_ASSERT(!mReferencingRules.Contains(&aRule));
    mReferencingRules.AppendElement(&aRule);
  }

  void RemoveReferencingRule(dom::CSSImportRule& aRule) {
    MOZ_ASSERT(mReferencingRules.Contains(&aRule));
    mReferencingRules.RemoveElement(&aRule);
  }

  // Note that when exposed to script, this should always have a <= 1 length.
  // CSSImportRule::GetStyleSheetForBindings takes care of that.
  dom::CSSImportRule* GetOwnerRule() const {
    return mReferencingRules.SafeElementAt(0);
  }

  void AppendStyleSheet(StyleSheet&);

  // Append a stylesheet to the child list without calling WillDirty.
  void AppendStyleSheetSilently(StyleSheet&);

  const nsTArray<RefPtr<StyleSheet>>& ChildSheets() const {
#ifdef DEBUG
    for (StyleSheet* child : Inner().mChildren) {
      MOZ_ASSERT(child->GetParentSheet());
      MOZ_ASSERT(child->GetParentSheet()->mInner == mInner);
    }
#endif
    return Inner().mChildren;
  }

  // Principal() never returns a null pointer.
  nsIPrincipal* Principal() const;
  void SetTitle(const nsAString& aTitle) { mTitle = aTitle; }
  void SetMedia(already_AddRefed<dom::MediaList> aMedia);

  // Get this style sheet's CORS mode
  CORSMode GetCORSMode() const { return Inner().mCORSMode; }

  // Get this style sheet's ReferrerInfo
  nsIReferrerInfo* GetReferrerInfo() const;

  // Get this style sheet's integrity metadata
  void GetIntegrity(dom::SRIMetadata& aResult) const {
    aResult = Inner().mIntegrity;
  }

  size_t SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const;
#if defined(DEBUG) || defined(MOZ_LAYOUT_DEBUGGER)
  void List(FILE* aOut = stdout, int32_t aIndex = 0);
#endif

  // WebIDL StyleSheet API
  void GetType(nsAString& aType);
  void GetHref(nsAString& aHref, ErrorResult& aRv);
  // GetOwnerNode is defined above.
  StyleSheet* GetParentStyleSheet() const { return GetParentSheet(); }
  void GetTitle(nsAString& aTitle);
  dom::MediaList* Media();
  bool Disabled() const { return bool(mState & State::Disabled); }
  void SetDisabled(bool aDisabled);

  void GetSourceMapURL(nsACString&);
  void SetSourceMapURL(nsCString&&);
  void GetSourceURL(nsACString& aSourceURL);

  // WebIDL CSSStyleSheet API
  // Can't be inline because we can't include ImportRule here.  And can't be
  // called GetOwnerRule because that would be ambiguous with the ImportRule
  // version.
  already_AddRefed<css::Rule> GetDOMOwnerRule() const;
  dom::CSSRuleList* GetCssRules(nsIPrincipal& aSubjectPrincipal, ErrorResult&);
  uint32_t InsertRule(const nsACString& aRule, uint32_t aIndex,
                      nsIPrincipal& aSubjectPrincipal, ErrorResult& aRv);
  void DeleteRule(uint32_t aIndex, nsIPrincipal& aSubjectPrincipal,
                  ErrorResult& aRv);
  int32_t AddRule(const nsACString& aSelector, const nsACString& aBlock,
                  const dom::Optional<uint32_t>& aIndex,
                  nsIPrincipal& aSubjectPrincipal, ErrorResult& aRv);
  already_AddRefed<dom::Promise> Replace(const nsACString& aText, ErrorResult&);
  void ReplaceSync(const nsACString& aText, ErrorResult&);
  bool ModificationDisallowed() const {
    return bool(mState & State::ModificationDisallowed);
  }

  // Called before and after the asynchronous Replace() function
  // to disable/re-enable modification while there is a pending promise.
  void SetModificationDisallowed(bool aDisallowed) {
    MOZ_ASSERT(IsConstructed());
    MOZ_ASSERT(!IsReadOnly());
    if (aDisallowed) {
      mState |= State::ModificationDisallowed;
      // Sheet will be re-set to complete when its rules are replaced
      mState &= ~State::Complete;
      if (!Disabled()) {
        ApplicableStateChanged(false);
      }
    } else {
      mState &= ~State::ModificationDisallowed;
    }
  }

  // True if the sheet was created through the Constructable StyleSheets API
  bool IsConstructed() const { return !!mConstructorDocument; }

  // Whether this sheet is directly associated to the given doc or shadow root.
  // That means that it's a non-imported regular, adopted, or additional style
  // sheet, and thus that it should be in the top level list of sheets for that
  // subtree. It can be cheaper than walking the whole list of stylesheets.
  bool IsDirectlyAssociatedTo(dom::DocumentOrShadowRoot&) const;

  // Ture if the sheet's constructor document matches the given document
  bool ConstructorDocumentMatches(const dom::Document& aDocument) const {
    return mConstructorDocument == &aDocument;
  }

  // Add a document or shadow root to the list of adopters.
  // Adopters will be notified when styles are changed.
  void AddAdopter(dom::DocumentOrShadowRoot& aAdopter) {
    // MOZ_ASSERT(IsConstructed());
    MOZ_ASSERT(!mAdopters.Contains(&aAdopter));
    mAdopters.AppendElement(&aAdopter);
  }

  // Remove a document or shadow root from the list of adopters.
  void RemoveAdopter(dom::DocumentOrShadowRoot& aAdopter) {
    // Cannot assert IsConstructed() because this can run after unlink.
    mAdopters.RemoveElement(&aAdopter);
  }

  const nsTArray<dom::DocumentOrShadowRoot*>& SelfOrAncestorAdopters() const {
    return OutermostSheet().mAdopters;
  }

  // WebIDL miscellaneous bits
  inline dom::ParentObject GetParentObject() const;
  JSObject* WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto) final;

  // Changes to sheets should be after a WillDirty call.
  void WillDirty();

  // Called when a rule changes from CSSOM.
  //
  // FIXME(emilio): This shouldn't allow null, but MediaList doesn't know about
  // its owning media rule, plus it's used for the stylesheet media itself.
  void RuleChanged(css::Rule*, const StyleRuleChange&);

  void AddStyleSet(ServoStyleSet* aStyleSet);
  void DropStyleSet(ServoStyleSet* aStyleSet);

  nsresult DeleteRuleFromGroup(css::GroupRule* aGroup, uint32_t aIndex);
  nsresult InsertRuleIntoGroup(const nsACString& aRule, css::GroupRule* aGroup,
                               uint32_t aIndex);

  // Find the ID of the owner inner window.
  uint64_t FindOwningWindowInnerID() const;

  // Copy the contents of this style sheet into the shared memory buffer managed
  // by aBuilder.  Returns the pointer into the buffer that the sheet contents
  // were stored at.  (The returned pointer is to an Arc<Locked<Rules>> value,
  // or null, with a filled in aErrorMessage, on failure.)
  const StyleLockedCssRules* ToShared(StyleSharedMemoryBuilder* aBuilder,
                                      nsCString& aErrorMessage);

  // Sets the contents of this style sheet to the specified aSharedRules
  // pointer, which must be a pointer somewhere in the aSharedMemory buffer
  // as previously returned by a ToShared() call.
  void SetSharedContents(const StyleLockedCssRules* aSharedRules);

  // Whether this style sheet should not allow any modifications.
  //
  // This is true for any User Agent sheets once they are complete.
  bool IsReadOnly() const;

  // Removes a stylesheet from its parent sheet child list, if any.
  void RemoveFromParent();

  // Resolves mReplacePromise with this sheet.
  void MaybeResolveReplacePromise();

  // Rejects mReplacePromise with a NetworkError.
  void MaybeRejectReplacePromise();

  // Gets the relevant global if exists.
  nsISupports* GetRelevantGlobal() const;

  // Blocks/Unblocks resolution of parse promise
  void BlockParsePromise() {
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
    uint32_t count =
#endif
        ++mAsyncParseBlockers;
    MOZ_DIAGNOSTIC_ASSERT(count);
  }

  void UnblockParsePromise() {
    uint32_t count = --mAsyncParseBlockers;
    MOZ_DIAGNOSTIC_ASSERT(count != UINT32_MAX);
    if (!count && !mParsePromise.IsEmpty()) {
      mParsePromise.Resolve(true, __func__);
    }
  }

 private:
  void SetModifiedRules() {
    mState |= State::ModifiedRules | State::ModifiedRulesForDevtools;
  }

  const StyleSheet& OutermostSheet() const {
    const auto* current = this;
    while (current->mParentSheet) {
      MOZ_ASSERT(!current->mDocumentOrShadowRoot,
                 "Shouldn't be set on child sheets");
      MOZ_ASSERT(!current->mConstructorDocument,
                 "Shouldn't be set on child sheets");
      current = current->mParentSheet;
    }
    return *current;
  }

  StyleSheetInfo& Inner() {
    MOZ_ASSERT(mInner);
    return *mInner;
  }

  const StyleSheetInfo& Inner() const {
    MOZ_ASSERT(mInner);
    return *mInner;
  }

  // Check if the rules are available for read and write.
  // It does the security check as well as whether the rules have been
  // completely loaded. aRv will have an exception set if this function
  // returns false.
  bool AreRulesAvailable(nsIPrincipal& aSubjectPrincipal, ErrorResult& aRv);

 protected:
  // Internal methods which do not have security check and completeness check.
  uint32_t InsertRuleInternal(const nsACString& aRule, uint32_t aIndex,
                              ErrorResult&);
  void DeleteRuleInternal(uint32_t aIndex, ErrorResult&);
  nsresult InsertRuleIntoGroupInternal(const nsACString& aRule,
                                       css::GroupRule* aGroup, uint32_t aIndex);

  // Take the recently cloned sheets from the `@import` rules, and reparent them
  // correctly to `aPrimarySheet`.
  void FixUpAfterInnerClone();

  // aFromClone says whether this comes from a clone of the stylesheet (and thus
  // we should also fix up the wrappers for the individual rules in the rule
  // lists).
  void FixUpRuleListAfterContentsChangeIfNeeded(bool aFromClone = false);

  void DropRuleList();

  // Called when a rule is removed from the sheet from CSSOM.
  void RuleAdded(css::Rule&);

  // Called when a rule is added to the sheet from CSSOM.
  void RuleRemoved(css::Rule&);

  // Called when a stylesheet is cloned.
  void StyleSheetCloned(StyleSheet&);

  // Notifies that the applicable state changed.
  // aApplicable is the value that we expect to get from IsApplicable().
  // assertion will fail if the expectation does not match reality.
  void ApplicableStateChanged(bool aApplicable);

  void LastRelease();

  // Return success if the subject principal subsumes the principal of our
  // inner, error otherwise.  This will also succeed if access is allowed by
  // CORS.  In that case, it will set the principal of the inner to the
  // subject principal.
  void SubjectSubsumesInnerPrincipal(nsIPrincipal& aSubjectPrincipal,
                                     ErrorResult& aRv);

  // Drop our reference to mMedia
  void DropMedia();
  // Unlink our inner, if needed, for cycle collection.
  void UnlinkInner();
  // Traverse our inner, if needed, for cycle collection
  void TraverseInner(nsCycleCollectionTraversalCallback&);

  // Return whether the given @import rule has pending child sheet.
  static bool RuleHasPendingChildSheet(css::Rule* aRule);

  StyleSheet* mParentSheet;  // weak ref

  RefPtr<dom::Document> mConstructorDocument;

  // Will be set in the Replace() function and resolved/rejected by the
  // sheet once its rules have been replaced and the sheet is complete again.
  RefPtr<dom::Promise> mReplacePromise;

  nsString mTitle;

  // weak ref; parents maintain this for their children
  dom::DocumentOrShadowRoot* mDocumentOrShadowRoot;
  nsINode* mOwningNode = nullptr;                   // weak ref
  nsTArray<dom::CSSImportRule*> mReferencingRules;  // weak ref

  RefPtr<dom::MediaList> mMedia;

  RefPtr<URLExtraData> mURLData;
  RefPtr<nsIURI> mOriginalSheetURI;
  State mState;

  Atomic<uint32_t, ReleaseAcquire> mAsyncParseBlockers{0};

  // Core information we get from parsed sheets, which are shared amongst
  // StyleSheet clones.
  //
  // Always nonnull until LastRelease().
  StyleSheetInfo* mInner;

  nsTArray<ServoStyleSet*> mStyleSets;

  RefPtr<ServoCSSRuleList> mRuleList;

  MozPromiseHolder<StyleSheetParsePromise> mParsePromise;

  nsTArray<dom::DocumentOrShadowRoot*> mAdopters;

  // Make StyleSheetInfo and subclasses into friends so they can use
  // ChildSheetListBuilder.
  friend struct StyleSheetInfo;
};

}  // namespace mozilla

#endif  // mozilla_StyleSheet_h
