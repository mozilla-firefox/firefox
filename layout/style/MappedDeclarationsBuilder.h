/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Representation of a declaration block used for attribute mapping */

#ifndef mozilla_MappedDeclarationsBuilder_h
#define mozilla_MappedDeclarationsBuilder_h

#include "NonCustomCSSPropertyId.h"
#include "mozilla/CSSPropertyId.h"
#include "mozilla/FontPropertyTypes.h"
#include "mozilla/ServoBindingTypes.h"
#include "mozilla/ServoBindings.h"
#include "mozilla/dom/Element.h"
#include "nsCSSValue.h"
#include "nsColor.h"

class nsAttrValue;

namespace mozilla {

// This provides a convenient interface for attribute mappers
// (MapAttributesIntoRule) to modify the presentation attribute declaration
// block for a given element.
class MOZ_STACK_CLASS MappedDeclarationsBuilder final {
 public:
  explicit MappedDeclarationsBuilder(
      dom::Element& aElement, dom::Document& aDoc,
      StyleLockedDeclarationBlock* aDecls = nullptr)
      : mDocument(aDoc), mElement(aElement), mDecls(aDecls) {
    if (mDecls) {
      Servo_DeclarationBlock_Clear(mDecls);
    }
  }

  ~MappedDeclarationsBuilder() {
    MOZ_ASSERT(!mDecls, "Forgot to take the block?");
  }

  dom::Document& Document() { return mDocument; }

  already_AddRefed<StyleLockedDeclarationBlock> TakeDeclarationBlock() {
    return mDecls.forget();
  }

  // Check if we already contain a certain longhand
  bool PropertyIsSet(NonCustomCSSPropertyId aId) const {
    CSSPropertyId id{aId};
    return mDecls && Servo_DeclarationBlock_HasProperty(mDecls, &id);
  }

  // Set a property to an identifier (string)
  void SetIdentStringValue(NonCustomCSSPropertyId aId, const nsString& aValue) {
    RefPtr<nsAtom> atom = NS_AtomizeMainThread(aValue);
    SetIdentAtomValue(aId, atom);
  }

  void SetIdentStringValueIfUnset(NonCustomCSSPropertyId aId,
                                  const nsString& aValue) {
    if (!PropertyIsSet(aId)) {
      SetIdentStringValue(aId, aValue);
    }
  }

  void SetIdentAtomValue(NonCustomCSSPropertyId aId, nsAtom* aValue);

  void SetIdentAtomValueIfUnset(NonCustomCSSPropertyId aId, nsAtom* aValue) {
    if (!PropertyIsSet(aId)) {
      SetIdentAtomValue(aId, aValue);
    }
  }

  // Set a property to a keyword (usually NS_STYLE_* or StyleFoo::*)
  void SetKeywordValue(NonCustomCSSPropertyId aId, int32_t aValue) {
    Servo_DeclarationBlock_SetKeywordValue(&EnsureDecls(), aId, aValue);
  }

  void SetKeywordValueIfUnset(NonCustomCSSPropertyId aId, int32_t aValue) {
    if (!PropertyIsSet(aId)) {
      SetKeywordValue(aId, aValue);
    }
  }

  template <typename T, typename = std::enable_if_t<std::is_enum_v<T>>>
  void SetKeywordValue(NonCustomCSSPropertyId aId, T aValue) {
    static_assert(EnumTypeFitsWithin<T, int32_t>::value,
                  "aValue must be an enum that fits within 32 bits");
    SetKeywordValue(aId, static_cast<int32_t>(aValue));
  }
  template <typename T, typename = std::enable_if_t<std::is_enum_v<T>>>
  void SetKeywordValueIfUnset(NonCustomCSSPropertyId aId, T aValue) {
    static_assert(EnumTypeFitsWithin<T, int32_t>::value,
                  "aValue must be an enum that fits within 32 bits");
    SetKeywordValueIfUnset(aId, static_cast<int32_t>(aValue));
  }

  // Set a property to an integer value
  void SetIntValue(NonCustomCSSPropertyId aId, int32_t aValue) {
    Servo_DeclarationBlock_SetIntValue(&EnsureDecls(), aId, aValue);
  }

  // Set "math-depth: <integer>" or "math-depth: add(<integer>)"
  void SetMathDepthValue(int32_t aValue, bool aIsRelative) {
    Servo_DeclarationBlock_SetMathDepthValue(&EnsureDecls(), aValue,
                                             aIsRelative);
  }

  // Set "counter-reset: list-item <integer>".  If aIsReversed is true then
  // "list-item" instead becomes "reversed(list-item)".
  void SetCounterResetListItem(int32_t aValue, bool aIsReversed) {
    Servo_DeclarationBlock_SetCounterResetListItem(&EnsureDecls(), aValue,
                                                   aIsReversed);
  }

  // Set "counter-set: list-item <integer>".
  void SetCounterSetListItem(int32_t aValue) {
    Servo_DeclarationBlock_SetCounterSetListItem(&EnsureDecls(), aValue);
  }

  // Set a property to a pixel value
  void SetPixelValue(NonCustomCSSPropertyId aId, float aValue) {
    Servo_DeclarationBlock_SetPixelValue(&EnsureDecls(), aId, aValue);
  }

  void SetPixelValueIfUnset(NonCustomCSSPropertyId aId, float aValue) {
    if (!PropertyIsSet(aId)) {
      SetPixelValue(aId, aValue);
    }
  }

  void SetLengthValue(NonCustomCSSPropertyId aId, const nsCSSValue& aValue) {
    MOZ_ASSERT(aValue.IsLengthUnit());
    Servo_DeclarationBlock_SetLengthValue(
        &EnsureDecls(), aId, aValue.GetFloatValue(), aValue.GetUnit());
  }

  // Set a property to a percent value
  void SetPercentValue(NonCustomCSSPropertyId aId, float aValue) {
    Servo_DeclarationBlock_SetPercentValue(&EnsureDecls(), aId, aValue);
  }

  void SetPercentValueIfUnset(NonCustomCSSPropertyId aId, float aValue) {
    if (!PropertyIsSet(aId)) {
      SetPercentValue(aId, aValue);
    }
  }

  // Set a property to `auto`
  void SetAutoValue(NonCustomCSSPropertyId aId) {
    Servo_DeclarationBlock_SetAutoValue(&EnsureDecls(), aId);
  }

  void SetAutoValueIfUnset(NonCustomCSSPropertyId aId) {
    if (!PropertyIsSet(aId)) {
      SetAutoValue(aId);
    }
  }

  // Set a property to `currentcolor`
  void SetCurrentColor(NonCustomCSSPropertyId aId) {
    Servo_DeclarationBlock_SetCurrentColor(&EnsureDecls(), aId);
  }

  void SetCurrentColorIfUnset(NonCustomCSSPropertyId aId) {
    if (!PropertyIsSet(aId)) {
      SetCurrentColor(aId);
    }
  }

  // Set a property to an RGBA nscolor value
  void SetColorValue(NonCustomCSSPropertyId aId, nscolor aValue) {
    Servo_DeclarationBlock_SetColorValue(&EnsureDecls(), aId, aValue);
  }

  void SetColorValueIfUnset(NonCustomCSSPropertyId aId, nscolor aValue) {
    if (!PropertyIsSet(aId)) {
      SetColorValue(aId, aValue);
    }
  }

  // Set font-family to a string
  void SetFontFamily(const nsACString& aValue) {
    Servo_DeclarationBlock_SetFontFamily(&EnsureDecls(), &aValue);
  }

  // Add a quirks-mode override to the decoration color of elements nested in
  // <a>
  void SetTextDecorationColorOverride() {
    Servo_DeclarationBlock_SetTextDecorationColorOverride(&EnsureDecls());
  }

  void SetBackgroundImage(const nsAttrValue& value);

  void SetAspectRatio(float aWidth, float aHeight) {
    Servo_DeclarationBlock_SetAspectRatio(&EnsureDecls(), aWidth, aHeight);
  }

  const nsAttrValue* GetAttr(nsAtom* aName) {
    MOZ_ASSERT(mElement.IsNoNamespaceAttrMapped(aName));
    return mElement.GetParsedAttr(aName);
  }

  const nsAttrValue* GetAttr(int32_t aNamespaceID, nsAtom* aName) {
    MOZ_ASSERT(mElement.IsAttrMapped(aNamespaceID, aName));
    return mElement.GetParsedAttr(aName, aNamespaceID);
  }

 private:
  StyleLockedDeclarationBlock& EnsureDecls() {
    if (!mDecls) {
      mDecls = Servo_DeclarationBlock_CreateEmpty().Consume();
    }
    return *mDecls;
  }

  dom::Document& mDocument;
  dom::Element& mElement;
  RefPtr<StyleLockedDeclarationBlock> mDecls;
};

}  // namespace mozilla

#endif
