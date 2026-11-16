/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_a11y_RemoteAccessible_h
#define mozilla_a11y_RemoteAccessible_h

#include "AccAttributes.h"
#include "LocalAccessible.h"
#include "mozilla/a11y/Accessible.h"
#include "mozilla/a11y/CacheConstants.h"
#include "mozilla/a11y/HyperTextAccessibleBase.h"
#include "mozilla/a11y/Role.h"
#include "nsIAccessibleText.h"
#include "nsIAccessibleTypes.h"
#include "nsISupportsImpl.h"
#include "nsRect.h"
#include "nsTArray.h"

namespace mozilla {
namespace a11y {

class Attribute;
class DocAccessibleParent;
class RemoteAccessible;
enum class RelationType;

/**
 * The class for an accessibility tree node that originated in the parent
 * process.
 */
class RemoteAccessible : public Accessible, public HyperTextAccessibleBase {
 public:
  // AddRef/Release are declared virtual (rather than using the plain
  // NS_INLINE_DECL_REFCOUNTING) so that DocAccessibleParent, which is a
  // RemoteAccessible subclass with its own nsISupports-based refcount (see
  // NS_DECL_ISUPPORTS in DocAccessibleParent.h), can override them with a
  // single implementation. Without this, a DocAccessibleParent stored in
  // another node's mChildren/mParent would be tracked by two independent,
  // disconnected refcounts.
  //
  // They're hand-written here rather than using
  // NS_INLINE_DECL_VIRTUAL_REFCOUNTING_WITH_DESTROY because that macro's
  // NS_ASSERT_OWNINGTHREAD assumes a single owning thread. On Android, the
  // RemoteAccessible tree is legitimately touched from both the main thread
  // and the Android UI thread, protected by
  // nsAccessibilityService::GetAndroidMonitor() rather than by restricting
  // access to a single thread. See AssertActiveThread in the .cpp for the
  // assertion this uses instead.
  NS_IMETHOD_(MozExternalRefCountType) AddRef(void) override;
  NS_IMETHOD_(MozExternalRefCountType) Release(void) override;

  virtual bool IsRemote() const override { return true; }

  void AddChildAt(uint32_t aIdx, RemoteAccessible* aChild) {
    mChildren.InsertElementAt(aIdx, aChild);
    UpdateChildIndexCache(static_cast<int32_t>(aIdx));
    if (IsHyperText()) {
      InvalidateCachedHyperTextOffsets();
    }
  }

  virtual uint32_t ChildCount() const override { return mChildren.Length(); }
  RemoteAccessible* RemoteChildAt(uint32_t aIdx) const {
    RemoteAccessible* child = mChildren.SafeElementAt(aIdx);
    MOZ_ASSERT(!child || child->mParent == this,
               "Child's parent should be this");
    return child;
  }
  RemoteAccessible* RemoteFirstChild() const {
    return mChildren.Length() ? mChildren[0].get() : nullptr;
  }
  RemoteAccessible* RemoteLastChild() const {
    return mChildren.Length() ? mChildren[mChildren.Length() - 1].get()
                              : nullptr;
  }
  RemoteAccessible* RemotePrevSibling() const {
    if (IsDoc()) {
      // The normal code path doesn't work for documents because the parent
      // might be a local OuterDoc, but IndexInParent() will return 0.
      // A document is always a single child of an OuterDoc anyway.
      return nullptr;
    }
    int32_t idx = IndexInParent();
    if (idx == -1) {
      return nullptr;  // No parent.
    }
    MOZ_ASSERT(RemoteParent());
    return idx > 0 ? RemoteParent()->mChildren[idx - 1].get() : nullptr;
  }
  RemoteAccessible* RemoteNextSibling() const {
    if (IsDoc()) {
      // The normal code path doesn't work for documents because the parent
      // might be a local OuterDoc, but IndexInParent() will return 0.
      // A document is always a single child of an OuterDoc anyway.
      return nullptr;
    }
    int32_t idx = IndexInParent();
    if (idx == -1) {
      return nullptr;  // No parent.
    }
    MOZ_ASSERT(idx >= 0);
    size_t newIdx = idx + 1;
    MOZ_ASSERT(RemoteParent());
    return newIdx < RemoteParent()->mChildren.Length()
               ? RemoteParent()->mChildren[newIdx].get()
               : nullptr;
  }

  // Accessible hierarchy method overrides

  virtual Accessible* Parent() const override { return RemoteParent(); }

  virtual Accessible* ChildAt(uint32_t aIndex) const override {
    return RemoteChildAt(aIndex);
  }

  virtual Accessible* NextSibling() const override {
    return RemoteNextSibling();
  }

  virtual Accessible* PrevSibling() const override {
    return RemotePrevSibling();
  }

  virtual int32_t IndexInParent() const override {
    MOZ_ASSERT(mParent || mIndexInParent == -1,
               "IndexInParent should be -1 if no parent");
    return mIndexInParent;
  }

  virtual uint32_t EmbeddedChildCount() override;
  virtual int32_t IndexOfEmbeddedChild(Accessible* aChild) override;
  virtual Accessible* EmbeddedChildAt(uint32_t aChildIdx) override;

  void Shutdown();

  void SetChildDoc(DocAccessibleParent* aChildDoc);
  void ClearChildDoc(DocAccessibleParent* aChildDoc);

  /**
   * Remove The given child.
   */
  void RemoveChild(RemoteAccessible* aChild) {
    mChildren.RemoveElement(aChild);
    MOZ_ASSERT(aChild->mIndexInParent != -1);
    UpdateChildIndexCache(aChild->mIndexInParent);
    aChild->mIndexInParent = -1;
    if (IsHyperText()) {
      InvalidateCachedHyperTextOffsets();
    }
  }

  /**
   * Return the proxy for the parent of the wrapped accessible.
   */
  RemoteAccessible* RemoteParent() const;

  LocalAccessible* OuterDocOfRemoteBrowser() const;

  /**
   * Get the native role of the accessible we're proxying.
   */
  virtual mozilla::a11y::role NativeRole() const override {
    return mNativeRole;
  }

  /**
   * Return true if this is an embedded object.
   */
  bool IsEmbeddedObject() const { return !IsText(); }

  virtual bool IsLink() const override {
    if (IsHTMLLink()) {
      // XXX: HTML links always return true for IsLink.
      return true;
    }

    if (IsText()) {
      return false;
    }

    if (Accessible* parent = Parent()) {
      return parent->IsHyperText();
    }

    return false;
  }

  virtual bool HasNumericValue() const override {
    // XXX: We combine the aria and native "has numeric value" field
    // when we serialize the local accessible into eNumericValue.
    return HasGenericType(eNumericValue);
  }

  // Methods that potentially access a cache.

  virtual ENameValueFlag Name(nsString& aName) const override final;
  virtual EDescriptionValueFlag Description(
      nsString& aDescription) const override;
  virtual void Value(nsString& aValue) const override;

  virtual double CurValue() const override;
  virtual double MinValue() const override;
  virtual double MaxValue() const override;
  virtual double Step() const override;
  virtual bool SetCurValue(double aValue) override;

  virtual Accessible* ChildAtPoint(
      int32_t aX, int32_t aY,
      LocalAccessible::EWhichChildAtPoint aWhichChild) override;

  virtual LayoutDeviceIntRect Bounds() const override;

  virtual nsRect BoundsInAppUnits() const override;

  virtual Relation RelationByType(RelationType aType) const override;

  virtual uint64_t State() override;

  virtual already_AddRefed<AccAttributes> Attributes() override;

  virtual nsAtom* TagName() const override;

  virtual already_AddRefed<nsAtom> DisplayStyle() const override;

  virtual float Opacity() const override;

  virtual WritingMode GetWritingMode() const override;

  virtual void LiveRegionAttributes(nsAString* aLive, nsAString* aRelevant,
                                    Maybe<bool>* aAtomic,
                                    nsAString* aBusy) const override;

  virtual Maybe<bool> ARIASelected() const override;

  virtual uint8_t ActionCount() const override;

  virtual void ActionNameAt(uint8_t aIndex, nsAString& aName) override;

  virtual bool DoAction(uint8_t aIndex) const override;

  virtual KeyBinding AccessKey() const override;

  virtual void SelectionRanges(nsTArray<TextRange>* aRanges) const override;

  MOZ_CAN_RUN_SCRIPT_BOUNDARY virtual bool RemoveFromSelection(
      int32_t aSelectionNum) override;

  virtual Maybe<int32_t> GetIntARIAAttr(nsAtom* aAttrName) const override;

  virtual bool GetStringARIAAttr(nsAtom* aAttrName,
                                 nsAString& aAttrValue) const override;

  virtual bool ARIAAttrValueIs(nsAtom* aAttrName,
                               nsAtom* aAttrValue) const override;

  virtual bool HasARIAAttr(nsAtom* aAttrName) const override;

  virtual void Language(nsAString& aLocale) override;

  //////////////////////////////////////////////////////////////////////////////
  // EditableTextAccessible

  MOZ_CAN_RUN_SCRIPT_BOUNDARY virtual void ReplaceText(
      const nsAString& aText) override;
  MOZ_CAN_RUN_SCRIPT_BOUNDARY virtual void InsertText(
      const nsAString& aText, int32_t aPosition) override;
  MOZ_CAN_RUN_SCRIPT_BOUNDARY virtual void CopyText(int32_t aStartPos,
                                                    int32_t aEndPos) override;
  MOZ_CAN_RUN_SCRIPT_BOUNDARY virtual void CutText(int32_t aStartPos,
                                                   int32_t aEndPos) override;
  MOZ_CAN_RUN_SCRIPT_BOUNDARY virtual void DeleteText(int32_t aStartPos,
                                                      int32_t aEndPos) override;
  MOZ_CAN_RUN_SCRIPT virtual void PasteText(int32_t aPosition) override;

  //////////////////////////////////////////////////////////////////////////////
  // SelectAccessible

  virtual void SelectedItems(nsTArray<Accessible*>* aItems) override;

  virtual uint32_t SelectedItemCount() override;

  virtual Accessible* GetSelectedItem(uint32_t aIndex) override;

  virtual bool IsItemSelected(uint32_t aIndex) override;

  virtual bool AddItemToSelection(uint32_t aIndex) override;

  virtual bool RemoveItemFromSelection(uint32_t aIndex) override;

  virtual bool SelectAll() override;

  virtual bool UnselectAll() override;

  virtual void TakeSelection() override;

  virtual void SetSelected(bool aSelect) override;

  // Methods that interact with content.

  virtual void TakeFocus() const override;
  virtual void ScrollTo(uint32_t aHow) const override;

  /**
   * Allow the platform to store a pointers worth of data on us.
   */
  uintptr_t GetWrapper() const { return mWrapper; }
  void SetWrapper(uintptr_t aWrapper) { mWrapper = aWrapper; }

  virtual uint64_t ID() const override { return mID; }

  /**
   * Return the document containing this proxy, or the proxy itself if it is a
   * document.
   */
  DocAccessibleParent* Document() const { return mDoc; }

  DocAccessibleParent* AsDoc() const { return IsDoc() ? mDoc : nullptr; }

  bool ApplyCache(CacheUpdateType aUpdateType, AccAttributes* aFields);

  void UpdateStateCache(uint64_t aState, bool aEnabled) {
    if (aState & kRemoteCalculatedStates) {
      return;
    }
    uint64_t state = 0;
    if (mCachedFields) {
      if (auto oldState =
              mCachedFields->GetAttribute<uint64_t>(CacheKey::State)) {
        state = *oldState;
      }
    } else {
      mCachedFields = new AccAttributes();
    }
    if (aEnabled) {
      state |= aState;
    } else {
      state &= ~aState;
    }
    mCachedFields->SetAttribute(CacheKey::State, state);
  }

  void InvalidateGroupInfo();

  virtual void AppendTextTo(nsAString& aText, uint32_t aStartOffset = 0,
                            uint32_t aLength = UINT32_MAX) override;

  virtual bool TableIsProbablyForLayout();

  /**
   * Iterates through each atom in kRelationTypeAtoms, checking to see
   * if it is present in aFields. If it is present (or if aFields contains
   * a DeleteEntry() for this atom) and mCachedFields is initialized,
   * fetches the old rel targets and removes their existing reverse relations
   * stored in mReverseRelations.
   * Returns an array of bools where the ith array entry corresponds
   * to whether or not the rel at the ith entry of kRelationTypeAtoms
   * requires a post-processing update.
   */
  nsTArray<bool> PreProcessRelations(AccAttributes* aFields);

  /**
   * Takes in the array returned from PreProcessRelations.
   * For each entry requiring an update, fetches the new relation
   * targets stored in mCachedFields and appropriately
   * updates their reverse relations in mReverseRelations.
   */
  void PostProcessRelations(const nsTArray<bool>& aToUpdate);

  /**
   * This method is called during shutdown, before we clear our
   * reverse rel map from the document's mReverseRelations cache.
   * Here, we traverse our reverse relations, removing our ID from
   * the corresponding forward relation's target list. This ensures
   * the stored forward relations do not reference defunct accessibles.
   */
  void PruneRelationsOnShutdown();

  uint32_t GetCachedTextLength();
  Maybe<const nsTArray<int32_t>&> GetCachedTextLines();
  nsRect GetCachedCharRect(int32_t aOffset);
  const AccAttributes* GetCachedTextAttributes();
  const AccAttributes* GetCachedARIAAttributes() const;

  nsString GetCachedHTMLNameAttribute() const;

  virtual HyperTextAccessibleBase* AsHyperTextBase() override {
    return IsHyperText() ? static_cast<HyperTextAccessibleBase*>(this)
                         : nullptr;
  }

  virtual TableAccessible* AsTable() override;
  virtual TableCellAccessible* AsTableCell() override;

  virtual void DOMNodeID(nsString& aID) const override;

  virtual void DOMNodeClass(nsString& aClass) const override;

  virtual int32_t HeadingLevel() const override;

  virtual void ScrollToPoint(uint32_t aScrollType, int32_t aX,
                             int32_t aY) override;

  virtual bool IsScrollable() const override;

  virtual bool IsPopover() const override;

  virtual bool HasPrimaryAction() const override;

  virtual bool HasCustomActions() const override;
  virtual bool IsEditable() const override;

  // HTMLMeterAccessible
  int32_t ValueRegion() const;

  // HyperTextAccessibleBase
  virtual already_AddRefed<AccAttributes> DefaultTextAttributes() override;

  virtual void ScrollSubstringToPoint(int32_t aStartOffset, int32_t aEndOffset,
                                      uint32_t aCoordinateType, int32_t aX,
                                      int32_t aY) override;

  virtual std::pair<mozilla::LayoutDeviceIntRect, nsIWidget*> GetCaretRect()
      override;

  /**
   * Invalidate cached HyperText offsets. This should be called whenever a
   * child is added or removed or the text of a text leaf child is changed.
   * Although GetChildOffset can either fully or partially invalidate the
   * offsets cache, calculating which offset to invalidate is not worthwhile
   * because a client might not even query offsets. This is in contrast to
   * LocalAccessible, where the offsets are always needed to fire text change
   * events. For RemoteAccessible, it's cheaper overall to just rebuild the
   * offsets cache when a client next needs it.
   */
  void InvalidateCachedHyperTextOffsets() {
    if (mCachedFields) {
      mCachedFields->Remove(CacheKey::HyperTextOffsets);
    }
  }

  size_t SizeOfIncludingThis(MallocSizeOf aMallocSizeOf);
  virtual size_t SizeOfExcludingThis(MallocSizeOf aMallocSizeOf);

 protected:
  virtual ~RemoteAccessible() {
    MOZ_ASSERT(!mWrapper);
    MOZ_COUNT_DTOR(RemoteAccessible);
  }

  RemoteAccessible(uint64_t aID, DocAccessibleParent* aDoc, role aRole,
                   AccType aType, AccGenericType aGenericTypes,
                   uint8_t aRoleMapEntryIndex)
      : Accessible(aType, aGenericTypes, aRoleMapEntryIndex),
        mParent(nullptr),
        mDoc(aDoc),
        mWrapper(0),
        mID(aID),
        mCachedFields(nullptr),
        mNativeRole(aRole) {
    MOZ_COUNT_CTOR(RemoteAccessible);
  }

  explicit RemoteAccessible(DocAccessibleParent* aThisAsDoc)
      : mParent(nullptr),
        mDoc(aThisAsDoc),
        mWrapper(0),
        mID(0),
        mCachedFields(nullptr),
        mNativeRole(roles::DOCUMENT) {
    mGenericTypes = eDocument | eHyperText;
    MOZ_COUNT_CTOR(RemoteAccessible);
  }

 protected:
  void SetParent(RemoteAccessible* aParent);
  Maybe<nsRect> RetrieveCachedBounds() const;
  LayoutDeviceIntRect ComputeBoundsFromContent() const;
  bool ApplyTransform(nsRect& aCumulativeBounds) const;
  bool ApplyScrollOffset(nsRect& aBounds, float aResolution) const;
  void ApplyCrossDocOffset(nsRect& aBounds) const;
  void ApplyVisualViewportOffset(nsRect& aBounds) const;
  LayoutDeviceIntRect BoundsWithOffset(
      Maybe<nsRect> aOffset, bool aBoundsAreForHittesting = false) const;
  bool IsFixedPos() const;
  bool IsOverflowHidden() const;

  /**
   * Returns true if an accessible's frame has no scrollable overflow, and
   * false otherwise.
   * Does not return true for partially clipped accessibles.
   */
  bool IsClipped() const;

  /**
   * Checks if our hittesting match has any clipped children and, if so
   * descends it and subsequent TEXT_CONTAINERs in search of a text leaf.
   * We do this because some sites use clipping to hide text that is only
   * visible to a11y, while displaying a visual version of the same text on
   * the web page. We want a hittest of the visible text to resolve to the
   * hidden, a11y-only text node.
   */
  RemoteAccessible* DoFuzzyHittesting();

  // This function is used exclusively for hit testing.
  bool ContainsPoint(int32_t aX, int32_t aY);

  virtual void ARIAGroupPosition(int32_t* aLevel, int32_t* aSetSize,
                                 int32_t* aPosInSet) const override;

  virtual AccGroupInfo* GetGroupInfo() const override;

  virtual AccGroupInfo* GetOrCreateGroupInfo() override;

  virtual void GetPositionAndSetSize(int32_t* aPosInSet,
                                     int32_t* aSetSize) override;
  nsAtom* GetPrimaryAction() const;

  virtual nsTArray<int32_t>& GetCachedHyperTextOffsets() override;

  nsTArray<Accessible*> LegendsOrCaptions() const;

  RemoteAccessible* LegendOrCaptionFor() const;

 private:
  /**
   * Update mIndexInParent on each child starting at aStartIdx up to the last
   * child. This should be called when a child is added or removed.
   */
  void UpdateChildIndexCache(int32_t aStartIdx) {
    int32_t count = static_cast<int32_t>(mChildren.Length());
    for (int32_t idx = aStartIdx; idx < count; ++idx) {
      mChildren[idx]->mIndexInParent = idx;
    }
  }

  // Asserts that it's safe for the calling thread to touch this
  // RemoteAccessible's refcount right now. See the comment on AddRef/Release.
  void AssertActiveThread() const;

  nsAutoRefCnt mRefCnt;

  RefPtr<RemoteAccessible> mParent;

  friend DocAccessibleParent;
  friend TextLeafPoint;
  friend HyperTextAccessibleBase;
  friend class xpcAccessible;
  friend class CachedTableCellAccessible;
#ifdef XP_WIN
  friend class MsaaAccessible;
#endif

  nsTArray<RefPtr<RemoteAccessible>> mChildren;
  DocAccessibleParent* mDoc;
  uintptr_t mWrapper;
  uint64_t mID;
  int32_t mIndexInParent = -1;

 protected:
  virtual const Accessible* Acc() const override { return this; }

  RefPtr<AccAttributes> mCachedFields;

  // XXX DocAccessibleParent gets to change this to change the role of
  // documents.
  role mNativeRole : 27;
};

////////////////////////////////////////////////////////////////////////////////
// RemoteAccessible downcasting method

inline RemoteAccessible* Accessible::AsRemote() {
  return IsRemote() ? static_cast<RemoteAccessible*>(this) : nullptr;
}

}  // namespace a11y
}  // namespace mozilla

#endif
