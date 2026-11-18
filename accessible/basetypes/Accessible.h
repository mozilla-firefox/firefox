/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef Accessible_H_
#define Accessible_H_

#include "Units.h"
#include "mozilla/a11y/AccTypes.h"
#include "mozilla/a11y/Role.h"
#include "nsISupportsImpl.h"
#include "nsRect.h"
#include "nsStringFwd.h"

class nsAtom;
class nsStaticAtom;

struct nsRoleMapEntry;

class nsIURI;

namespace mozilla {
class WritingMode;

namespace a11y {

class AccAttributes;
class AccGroupInfo;
class HyperTextAccessibleBase;
class LocalAccessible;
class Relation;
enum class RelationType;
class RemoteAccessible;
class TableAccessible;
class TableCellAccessible;

/**
 * Name type flags.
 */
enum ENameValueFlag {
  /**
   * Name either
   *  a) present (not empty): !name.IsEmpty()
   *  b) no name (was missed): name.IsVoid()
   */
  eNameOK,

  /**
   * Name was computed from the subtree.
   */
  eNameFromSubtree,

  /**
   * Tooltip was used as a name.
   */
  eNameFromTooltip,

  /**
   * Labelling relations were used for the name.
   */
  eNameFromRelations,
};

/**
 * Description type flags.
 */
enum EDescriptionValueFlag {
  /**
   * Description either
   *  a) present (not empty): !description.IsEmpty()
   *  b) no description (was missed): IsVoid() or IsEmpty(),
   *     or NameAndDescription caching domain is not active.
   */
  eDescriptionOK,

  /**
   * Description was provided by ARIA (either aria-description or
   * aria-describedby)
   */
  eDescriptionFromARIA,
};

/**
 * Group position (level, position in set and set size).
 */
struct GroupPos {
  GroupPos() : level(0), posInSet(0), setSize(0) {}
  GroupPos(int32_t aLevel, int32_t aPosInSet, int32_t aSetSize)
      : level(aLevel), posInSet(aPosInSet), setSize(aSetSize) {}

  int32_t level;
  int32_t posInSet;
  int32_t setSize;
};

/**
 * Represent key binding associated with accessible (such as access key and
 * global keyboard shortcuts).
 */
class KeyBinding {
 public:
  /**
   * Modifier mask values.
   */
  static const uint32_t kShift = 1;
  static const uint32_t kControl = 2;
  static const uint32_t kAlt = 4;
  static const uint32_t kMeta = 8;

  static uint32_t AccelModifier();

  KeyBinding() : mKey(0), mModifierMask(0) {}
  KeyBinding(uint32_t aKey, uint32_t aModifierMask)
      : mKey(aKey), mModifierMask(aModifierMask) {}
  explicit KeyBinding(uint64_t aSerialized) : mSerialized(aSerialized) {}

  inline bool IsEmpty() const { return !mKey; }
  inline uint32_t Key() const { return mKey; }
  inline uint32_t ModifierMask() const { return mModifierMask; }

  /**
   * Serialize this KeyBinding to a uint64_t for use in the parent process
   * cache. This is simpler than custom IPDL serialization for this simple case.
   */
  uint64_t Serialize() { return mSerialized; }

  enum Format { ePlatformFormat, eAtkFormat };

  /**
   * Return formatted string for this key binding depending on the given format.
   */
  inline void ToString(nsAString& aValue,
                       Format aFormat = ePlatformFormat) const {
    aValue.Truncate();
    AppendToString(aValue, aFormat);
  }
  inline void AppendToString(nsAString& aValue,
                             Format aFormat = ePlatformFormat) const {
    if (mKey) {
      if (aFormat == ePlatformFormat) {
        ToPlatformFormat(aValue);
      } else {
        ToAtkFormat(aValue);
      }
    }
  }

 private:
  void ToPlatformFormat(nsAString& aValue) const;
  void ToAtkFormat(nsAString& aValue) const;

  union {
    struct {
      uint32_t mKey;
      uint32_t mModifierMask;
    };
    uint64_t mSerialized;
  };
};

/**
 * The base type for an accessibility tree node. Methods and attributes in this
 * class are available in both the content process and the parent process.
 * Overrides for these methods live primarily in LocalAccessible and
 * RemoteAccessibleBase.
 */
class Accessible {
 protected:
  Accessible();

  Accessible(AccType aType, AccGenericType aGenericTypes,
             uint8_t aRoleMapEntryIndex);

 public:
  // AddRef/Release are pure virtual so that RefPtr<Accessible> can be used
  // generically, while leaving the concrete refcounting implementation
  // to subclasses (cycle-collected for LocalAccessible, plain virtual for
  // RemoteAccessible).
  NS_INLINE_DECL_PURE_VIRTUAL_REFCOUNTING

  /**
   * Return an id for this Accessible which is unique within the document.
   * Use nsAccUtils::GetAccessibleByID to retrieve an Accessible given an id
   * returned from this method.
   */
  virtual uint64_t ID() const = 0;

  virtual Accessible* Parent() const = 0;

  virtual role NativeRole() const = 0;

  /**
   * Return child accessible at the given index.
   */
  virtual Accessible* ChildAt(uint32_t aIndex) const = 0;

  virtual Accessible* NextSibling() const = 0;
  virtual Accessible* PrevSibling() const = 0;

  virtual uint32_t ChildCount() const = 0;

  virtual int32_t IndexInParent() const = 0;

  /**
   * Returns the accessible's calculated, final, role.
   */
  role Role() const;

  bool HasChildren() const { return !!FirstChild(); }

  inline Accessible* FirstChild() const {
    return ChildCount() ? ChildAt(0) : nullptr;
  }

  inline Accessible* LastChild() const {
    uint32_t childCount = ChildCount();
    return childCount ? ChildAt(childCount - 1) : nullptr;
  }

  /**
   * Return true if this Accessible is before another Accessible in the tree.
   */
  bool IsBefore(const Accessible* aAcc) const;

  /**
   * A utility enum for controlling FindAncestorIf.
   *   Continue: this is not the desired ancestor node, keep searching
   *   Found:    this is the desired ancestor node
   *   NotFound: this is not the desired ancestor node, stop searching
   */
  enum class AncestorSearchOption { Continue, Found, NotFound };
  /**
   * Return a non-generic ancestor for which the given predicate returns
   * AncestorSearchOption::Found, if any exist. If none exist, return nullptr.
   * The predicate may choose to return options from AncestorSearchOption to
   * control the flow of the ancestor search.
   */
  template <typename Callable>
  Accessible* FindAncestorIf(Callable&& aPredicate) const {
    static_assert(
        std::is_same_v<std::invoke_result_t<Callable, const Accessible&>,
                       AncestorSearchOption>,
        "Given callable must return AncestorSearchOption.");
    static_assert(std::is_invocable_v<Callable, const Accessible&>,
                  "Given callable must accept const Accessible&.");
    Accessible* search = GetNonGenericParent();
    while (search) {
      const AncestorSearchOption option = aPredicate(*search);
      switch (option) {
        case AncestorSearchOption::Continue:
          search = search->GetNonGenericParent();
          continue;
        case AncestorSearchOption::Found:
          return search;
        case AncestorSearchOption::NotFound:
          return nullptr;
        default:
          MOZ_ASSERT(false, "Unhandled AncestorSearchOption");
          break;
      }
    }
    return nullptr;
  }

  bool IsAncestorOf(const Accessible* aAcc) const {
    for (const Accessible* parent = aAcc->Parent(); parent;
         parent = parent->Parent()) {
      if (parent == this) {
        return true;
      }
    }
    return false;
  }

  /**
   * Return the closest common ancestor of `this` and `aAcc`, potentially
   * including `this`. That is, if `aAcc` is `this` or a descendant, this method
   * will return `this`.
   */
  const Accessible* GetClosestCommonInclusiveAncestor(
      const Accessible* aAcc) const;

  Accessible* GetClosestCommonInclusiveAncestor(Accessible* aAcc) {
    const Accessible* common =
        const_cast<const Accessible*>(this)->GetClosestCommonInclusiveAncestor(
            aAcc);
    return const_cast<Accessible*>(common);
  }

  /**
   * Used by ChildAtPoint() method to get direct or deepest child at point.
   */
  enum class EWhichChildAtPoint { DirectChild, DeepestChild };

  virtual Accessible* ChildAtPoint(int32_t aX, int32_t aY,
                                   EWhichChildAtPoint aWhichChild) = 0;

  /**
   * Return the focused child if any.
   */
  virtual Accessible* FocusedChild();

  /**
   * Return ARIA role map if any.
   */
  const nsRoleMapEntry* ARIARoleMap() const;

  /**
   * Return true if ARIA role is specified on the element.
   */
  bool HasARIARole() const;
  bool IsARIARole(nsAtom* aARIARole) const;
  bool HasStrongARIARole() const;

  /**
   * Return true if the accessible belongs to the given accessible type.
   */
  bool HasGenericType(AccGenericType aType) const;

  /**
   * Return group position (level, position in set and set size).
   */
  virtual GroupPos GroupPosition();

  /**
   * Return embedded accessible children count.
   */
  virtual uint32_t EmbeddedChildCount() = 0;

  /**
   * Return embedded accessible child at the given index.
   */
  virtual Accessible* EmbeddedChildAt(uint32_t aIndex) = 0;

  /**
   * Return index of the given embedded accessible child.
   */
  virtual int32_t IndexOfEmbeddedChild(Accessible* aChild) = 0;

  // Methods that potentially access a cache.

  /*
   * Get the name of this accessible.
   */
  virtual ENameValueFlag Name(nsString& aName) const = 0;

  /*
   * Return true if the accessible name is empty.
   */
  bool NameIsEmpty() const;

  /*
   * Get the description of this accessible.
   */
  virtual EDescriptionValueFlag Description(nsString& aDescription) const = 0;

  /**
   * Get the value of this accessible.
   */
  virtual void Value(nsString& aValue) const = 0;

  virtual double CurValue() const = 0;
  virtual double MinValue() const = 0;
  virtual double MaxValue() const = 0;
  virtual double Step() const = 0;
  virtual bool SetCurValue(double aValue) = 0;

  /**
   * Return boundaries in screen coordinates in device pixels.
   */
  virtual LayoutDeviceIntRect Bounds() const = 0;

  /**
   * Return boundaries in screen coordinates in app units.
   */
  virtual nsRect BoundsInAppUnits() const = 0;

  /**
   * Return boundaries in screen coordinates in CSS pixels.
   */
  virtual nsIntRect BoundsInCSSPixels() const;

  /**
   * Returns text of accessible if accessible has text role otherwise empty
   * string.
   *
   * @param aText         [in] returned text of the accessible
   * @param aStartOffset  [in, optional] start offset inside of the accessible,
   *                        if missed entire text is appended
   * @param aLength       [in, optional] required length of text, if missed
   *                        then text from start offset till the end is appended
   */
  virtual void AppendTextTo(nsAString& aText, uint32_t aStartOffset = 0,
                            uint32_t aLength = UINT32_MAX) = 0;

  /**
   * Return all states of accessible (including ARIA states).
   */
  virtual uint64_t State() = 0;

  /**
   * Return the start offset of the embedded object within the parent
   * HyperTextAccessibleBase.
   */
  virtual uint32_t StartOffset();

  /**
   * Return the end offset of the link within the parent
   * HyperTextAccessibleBase.
   */
  virtual uint32_t EndOffset();

  /**
   * Return object attributes for the accessible.
   */
  virtual already_AddRefed<AccAttributes> Attributes() = 0;

  virtual already_AddRefed<nsAtom> DisplayStyle() const = 0;

  virtual float Opacity() const = 0;

  virtual WritingMode GetWritingMode() const = 0;

  /**
   * Get the live region attributes (if any) for this single Accessible. This
   * does not propagate attributes from ancestors. If any argument is null, that
   * attribute is not fetched.
   */
  virtual void LiveRegionAttributes(nsAString* aLive, nsAString* aRelevant,
                                    Maybe<bool>* aAtomic,
                                    nsAString* aBusy) const = 0;

  /**
   * Get the aria-selected state. aria-selected not being specified is not
   * always the same as aria-selected="false". If not specified, Nothing() will
   * be returned.
   */
  virtual Maybe<bool> ARIASelected() const = 0;

  LayoutDeviceIntSize Size() const;

  LayoutDeviceIntPoint Position(uint32_t aCoordType);

  virtual Maybe<int32_t> GetIntARIAAttr(nsAtom* aAttrName) const = 0;

  virtual bool GetStringARIAAttr(nsAtom* aAttrName,
                                 nsAString& aAttrValue) const = 0;

  virtual bool ARIAAttrValueIs(nsAtom* aAttrName, nsAtom* aAttrValue) const = 0;

  virtual bool HasARIAAttr(nsAtom* aAttrName) const = 0;

  /**
   * Get the relation of the given type.
   */
  virtual Relation RelationByType(RelationType aType) const = 0;

  /**
   * Get the language associated with the accessible.
   */
  virtual void Language(nsAString& aLocale) = 0;

  /**
   * Get the role of this Accessible as an ARIA role token. This might have been
   * set explicitly (e.g. role="button") or it might be implicit in native
   * markup (e.g. <button> returns "button").
   */
  nsStaticAtom* ComputedARIARole() const;

  // Methods that interact with content.

  virtual void TakeFocus() const = 0;

  /**
   * Scroll the accessible into view.
   */
  MOZ_CAN_RUN_SCRIPT
  virtual void ScrollTo(uint32_t aHow) const = 0;

  /**
   * Scroll the accessible to the given point.
   */
  virtual void ScrollToPoint(uint32_t aCoordinateType, int32_t aX,
                             int32_t aY) = 0;

  /**
   * Return true if the accessible is scrollable.
   */
  virtual bool IsScrollable() const = 0;

  /**
   * Return tag name of associated DOM node.
   */
  virtual nsAtom* TagName() const = 0;

  /**
   * Return a landmark role if applied.
   */
  nsStaticAtom* LandmarkRole() const;

  /**
   * Return the id of the DOM node this Accessible represents.
   */
  virtual void DOMNodeID(nsString& aID) const = 0;

  /**
   * Return the class of the DOM node this Accessible represents.
   */
  virtual void DOMNodeClass(nsString& aClass) const = 0;

  /**
   * Return the Heading Level this accessible represents.
   */
  virtual int32_t HeadingLevel() const = 0;

  //////////////////////////////////////////////////////////////////////////////
  // ActionAccessible

  /**
   * Return the number of actions that can be performed on this accessible.
   */
  virtual uint8_t ActionCount() const = 0;

  /**
   * Return action name at given index.
   */
  virtual void ActionNameAt(uint8_t aIndex, nsAString& aName) = 0;

  /**
   * Default to localized action name.
   */
  void ActionDescriptionAt(uint8_t aIndex, nsAString& aDescription) {
    nsAutoString name;
    ActionNameAt(aIndex, name);
    TranslateString(name, aDescription);
  }

  /**
   * Invoke the accessible action.
   */
  virtual bool DoAction(uint8_t aIndex) const = 0;

  /**
   * Return access key, such as Alt+D.
   */
  virtual KeyBinding AccessKey() const = 0;

  //////////////////////////////////////////////////////////////////////////////
  // SelectAccessible

  /**
   * Return an array of selected items.
   */
  virtual void SelectedItems(nsTArray<Accessible*>* aItems) = 0;

  /**
   * Return the number of selected items.
   */
  virtual uint32_t SelectedItemCount() = 0;

  /**
   * Return selected item at the given index.
   */
  virtual Accessible* GetSelectedItem(uint32_t aIndex) = 0;

  /**
   * Determine if item at the given index is selected.
   */
  virtual bool IsItemSelected(uint32_t aIndex) = 0;

  /**
   * Add item at the given index the selection. Return true if success.
   */
  virtual bool AddItemToSelection(uint32_t aIndex) = 0;

  /**
   * Remove item at the given index from the selection. Return if success.
   */
  virtual bool RemoveItemFromSelection(uint32_t aIndex) = 0;

  /**
   * Select all items. Return true if success.
   */
  virtual bool SelectAll() = 0;

  /**
   * Unselect all items. Return true if success.
   */
  virtual bool UnselectAll() = 0;

  virtual void TakeSelection() = 0;

  virtual void SetSelected(bool aSelect) = 0;

  // Type "is" methods

  bool IsAbbreviation() const { return mType == eHTMLAbbrevType; }

  bool IsDoc() const { return HasGenericType(eDocument); }

  bool IsTableRow() const { return HasGenericType(eTableRow); }

  bool IsTableCell() const {
    // The eTableCell type defined in the ARIA map is used in
    // nsAccessibilityService::CreateAccessible to specify when
    // ARIAGridCellAccessible should be used for object creation. However, an
    // invalid table structure might cause this class not to be used after all.
    // To make sure we're really dealing with a cell, only check the generic
    // type defined by the class, not the type defined in the ARIA map.
    return mGenericTypes & eTableCell;
  }

  bool IsTable() const { return HasGenericType(eTable); }

  /**
   * Return true for XUL trees and listboxes, which create their own cells
   * rather than using CachedTableAccessible.
   */
  bool IsCustomTable() const { return IsXULTree() || IsXULListbox(); }

  bool IsHyperText() const { return HasGenericType(eHyperText); }

  bool IsSelect() const { return HasGenericType(eSelect); }

  bool IsText() const { return mGenericTypes & eText; }

  bool IsImage() const { return mType == eImageType; }

  bool IsApplication() const { return mType == eApplicationType; }

  bool IsAlert() const { return HasGenericType(eAlert); }

  bool IsButton() const { return HasGenericType(eButton); }

  bool IsCombobox() const { return HasGenericType(eCombobox); }

  virtual bool IsLink() const = 0;

  /**
   * Return true if the used ARIA role (if any) allows the hypertext accessible
   * to expose text interfaces.
   */
  bool IsTextRole();

  virtual bool IsEditable() const = 0;

  bool IsEditableRoot() const;

  bool IsGenericHyperText() const { return mType == eHyperTextType; }

  bool IsHTMLBr() const { return mType == eHTMLBRType; }
  bool IsHTMLCaption() const { return mType == eHTMLCaptionType; }
  bool IsHTMLCombobox() const { return mType == eHTMLComboboxType; }
  bool IsHTMLFileInput() const { return mType == eHTMLFileInputType; }

  bool IsHTMLListItem() const { return mType == eHTMLLiType; }

  bool IsHTMLListBullet() const { return mType == eHTMLListBulletType; }

  bool IsHTMLLink() const { return mType == eHTMLLinkType; }

  bool IsHTMLOptGroup() const { return mType == eHTMLOptGroupType; }

  bool IsHTMLRadioButton() const { return mType == eHTMLRadioButtonType; }

  bool IsHTMLSpinner() const { return mType == eHTMLSpinnerType; }

  bool IsHTMLRange() const { return mType == eHTMLRangeType; }

  bool IsHTMLTable() const { return mType == eHTMLTableType; }
  bool IsHTMLTableCell() const { return mType == eHTMLTableCellType; }
  bool IsHTMLTableRow() const { return mType == eHTMLTableRowType; }

  bool IsImageMap() const { return mType == eImageMapType; }

  bool IsList() const { return HasGenericType(eList); }

  bool IsListControl() const { return HasGenericType(eListControl); }

  bool IsMenuButton() const { return HasGenericType(eMenuButton); }

  bool IsMenuPopup() const { return mType == eMenuPopupType; }

  bool IsOuterDoc() const { return mType == eOuterDocType; }

  bool IsProgress() const { return mType == eProgressType; }

  virtual bool IsPopover() const = 0;

  bool IsRoot() const { return mType == eRootType; }

  bool IsPassword() const { return mType == eHTMLTextPasswordFieldType; }

  bool IsTextLeaf() const { return mType == eTextLeafType; }

  bool IsXULLabel() const { return mType == eXULLabelType; }

  bool IsXULListbox() const { return mType == eXULListboxType; }

  bool IsXULListItem() const { return mType == eXULListItemType; }

  bool IsXULTabpanels() const { return mType == eXULTabpanelsType; }

  bool IsXULTooltip() const { return mType == eXULTooltipType; }

  bool IsXULTree() const { return mType == eXULTreeType; }

  bool IsAutoCompletePopup() const {
    return HasGenericType(eAutoCompletePopup);
  }

  bool IsTextField() const {
    return mType == eHTMLTextFieldType || mType == eHTMLTextPasswordFieldType ||
           mType == eHTMLSpinnerType;
  }

  bool IsDateTimeField() const { return mType == eHTMLDateTimeFieldType; }

  virtual bool HasNumericValue() const = 0;

  /**
   * Returns true if this is a generic container element that has no meaning on
   * its own. A section that is an ePub aria role is not generic.
   */
  bool IsGeneric() const;

  /**
   * Returns the nearest ancestor in the document which is not a generic
   * element.
   */
  Accessible* GetNonGenericParent() const {
    for (Accessible* parent = Parent(); parent; parent = parent->Parent()) {
      if (parent->IsDoc() || !parent->IsGeneric()) {
        return parent;
      }
    }
    return nullptr;
  }

  /**
   * Returns true if the accessible is non-interactive.
   */
  bool IsNonInteractive() const {
    if (IsGeneric()) {
      return true;
    }
    const role accRole = Role();
    return accRole == role::LANDMARK || accRole == role::REGION;
  }

  /**
   * Returns true if this accessible represents plain content without
   * interactive or semantic meaning (text, images, generic containers).
   */
  bool IsPlainContent() const {
    switch (Role()) {
      case roles::TEXT_LEAF:
      case roles::STATICTEXT:
      case roles::WHITESPACE:
      case roles::GRAPHIC:
      case roles::IMAGE_MAP:
      case roles::CANVAS:
      case roles::DIAGRAM:
      case roles::TEXT:
      case roles::TEXT_CONTAINER:
      case roles::SECTION:
      case roles::NOTHING:
      case roles::GROUPING:
        return true;
      default:
        return false;
    }
  }

  /**
   * Return true if the link is valid (e. g. points to a valid URL).
   */
  bool IsLinkValid();

  /**
   * Return the number of anchors within the link.
   */
  uint32_t AnchorCount();

  /**
   * Returns an anchor URI at the given index.
   */
  virtual already_AddRefed<nsIURI> AnchorURIAt(uint32_t aAnchorIndex) const;

  /**
   * Returns an anchor accessible at the given index.
   */
  Accessible* AnchorAt(uint32_t aAnchorIndex) const;

  // Remote/Local types

  virtual bool IsRemote() const = 0;
  RemoteAccessible* AsRemote();

  bool IsLocal() const { return !IsRemote(); }
  LocalAccessible* AsLocal();

  virtual HyperTextAccessibleBase* AsHyperTextBase() { return nullptr; }

  virtual TableAccessible* AsTable() { return nullptr; }
  virtual TableCellAccessible* AsTableCell() { return nullptr; }

#ifdef A11Y_LOG
  /**
   * Provide a human readable description of the accessible,
   * including memory address, role, name, DOM tag and DOM ID.
   */
  void DebugDescription(nsCString& aDesc) const;

  static void DebugPrint(const char* aPrefix, const Accessible* aAccessible);
#endif

  /**
   * Return the localized string for the given key.
   */
  static void TranslateString(const nsString& aKey, nsAString& aStringOut,
                              const nsTArray<nsString>& aParams = {});

  /*
   * Return calculated group level based on accessible hierarchy.
   *
   * @param aFast  [in] Don't climb up tree. Calculate level from aria and
   *                    roles.
   */
  virtual int32_t GetLevel(bool aFast) const;

  /**
   * Return true if accessible has a primary action directly related to it, like
   * "click", "activate", "press", "jump", "open", "close", etc. A non-primary
   * action would be a complementary one like "showlongdesc".
   * If an accessible has an action that is associated with an ancestor, it is
   * not a primary action either.
   */
  virtual bool HasPrimaryAction() const = 0;

  /**
   * Return true if this Accessible has custom actions, even if those actions
   * aren't currently available. Custom actions are secondary actions provided
   * by the author using associated elements (e.g. via aria-actions), in
   * contrast to actions provided by Gecko on the element itself (e.g. click).
   * Custom actions are queried using RelationByType(RelationType::ACTION).
   * However, there can be cases where there are associated custom actions, but
   * the target elements are hidden; e.g. because the origin element isn't
   * focused. The client might need to know there are actions even if it can't
   * currently query them. For this case, this function will return true, even
   * though RelationByType will return nothing.
   */
  virtual bool HasCustomActions() const = 0;

 protected:
  // Some abstracted group utility methods.

  /**
   * Get ARIA group attributes.
   */
  virtual void ARIAGroupPosition(int32_t* aLevel, int32_t* aSetSize,
                                 int32_t* aPosInSet) const = 0;

  /**
   * Return group info if there is an up-to-date version.
   */
  virtual AccGroupInfo* GetGroupInfo() const = 0;

  /**
   * Return group info or create and update.
   */
  virtual AccGroupInfo* GetOrCreateGroupInfo() = 0;

  /**
   * Calculate position in group and group size ('posinset' and 'setsize') based
   * on accessible hierarchy.
   *
   * @param  aPosInSet  [out] accessible position in the group
   * @param  aSetSize   [out] the group size
   */
  virtual void GetPositionAndSetSize(int32_t* aPosInSet, int32_t* aSetSize);

  /**
   * Return the nearest ancestor that has a primary action, or null.
   */
  const Accessible* ActionAncestor() const;

  /**
   * Apply states which are implied by other information common to both
   * LocalAccessible and RemoteAccessible.
   */
  void ApplyImplicitState(uint64_t& aState) const;

  /**
   * Return the minimum role that should be used as a last resort if the element
   * does not have a more specific role.
   */
  mozilla::a11y::role GetMinimumRole(mozilla::a11y::role aRole) const;

  /**
   * Given a role and an accessible's state, parentage, and other attributes
   * transform the role to reflect its correct aria role.
   */
  mozilla::a11y::role ARIATransformRole(mozilla::a11y::role aRole) const;

  AccGenericType GenericTypes() const {
    return static_cast<AccGenericType>(mGenericTypes);
  }

 private:
  static const uint8_t kTypeBits = 6;
  static const uint8_t kGenericTypesBits = 18;

  void StaticAsserts() const;

  /*
   * This function assumes that the current role is not valid. It searches for a
   * fallback role in the role attribute string, and returns it. If there is no
   * valid fallback role in the role attribute string, the function returns the
   * native role. The aRolesToSkip parameter will cause the function to skip any
   * roles found in the role attribute string when searching for the next valid
   * role.
   */
  role FindNextValidARIARole(
      std::initializer_list<nsStaticAtom*> aRolesToSkip) const;

 protected:
  AccType mType : kTypeBits;
  uint32_t mGenericTypes : kGenericTypesBits;
  uint8_t mRoleMapEntryIndex;

  friend class DocAccessibleChild;
  friend class AccGroupInfo;
};

}  // namespace a11y
}  // namespace mozilla

#endif
