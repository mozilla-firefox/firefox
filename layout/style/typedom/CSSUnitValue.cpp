/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/CSSUnitValue.h"

#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/CSSPropertyId.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/RefPtr.h"
#include "mozilla/ServoStyleConsts.h"
#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/dom/CSSUnitValueBinding.h"

namespace mozilla::dom {

CSSUnitValue::CSSUnitValue(nsCOMPtr<nsISupports> aParent, double aValue,
                           const nsACString& aUnit)
    : CSSNumericValue(std::move(aParent), NumericValueType::UnitValue),
      mValue(aValue),
      mUnit(aUnit) {}

// static
RefPtr<CSSUnitValue> CSSUnitValue::Create(nsCOMPtr<nsISupports> aParent,
                                          const StyleUnitValue& aUnitValue) {
  return MakeRefPtr<CSSUnitValue>(std::move(aParent), aUnitValue.value,
                                  aUnitValue.unit);
}

JSObject* CSSUnitValue::WrapObject(JSContext* aCx,
                                   JS::Handle<JSObject*> aGivenProto) {
  return CSSUnitValue_Binding::Wrap(aCx, this, aGivenProto);
}

// start of CSSUnitValue Web IDL implementation

// https://drafts.css-houdini.org/css-typed-om-1/#dom-cssunitvalue-cssunitvalue
//
// static
already_AddRefed<CSSUnitValue> CSSUnitValue::Constructor(
    const GlobalObject& aGlobal, double aValue, const nsACString& aUnit,
    ErrorResult& aRv) {
  // XXX Units should be normalized to lowercase. The Typed OM spec doesn’t
  // state this explicitly, but WPT requires lowercase normalization and it
  // can also be deduced from the CSS Values spec. Besides fixing it here,
  // a spec issue may be needed to clarify this.

  // Step 1.

  // XXX A type should be created from unit and if that fails, the failure
  // should be propagated here

  // Step 2.

  return MakeAndAddRef<CSSUnitValue>(aGlobal.GetAsSupports(), aValue, aUnit);
}

double CSSUnitValue::Value() const { return mValue; }

void CSSUnitValue::SetValue(double aArg) { mValue = aArg; }

void CSSUnitValue::GetUnit(nsCString& aRetVal) const { aRetVal = mUnit; }

// end of CSSUnitValue Web IDL implementation

void CSSUnitValue::ToCssTextWithProperty(const CSSPropertyId& aPropertyId,
                                         nsACString& aDest) const {
  // XXX See:
  // https://drafts.css-houdini.org/css-typed-om-1/#create-an-internal-representation
  //
  // The current |isValueOutOfRange| check implements only a minimal subset of
  // the algorithm step that wraps out-of-range numeric values (e.g. negative
  // perspective) in a calc() expression to keep them representable.
  //
  // This is a temporary solution until a more robust mechanism is added for
  // handling range checks and value wrapping. The long-term plan is to move
  // this logic to a dedicated FromTyped trait or similar infrastructure that
  // can validate and construct internal representations in a property-aware
  // and fully spec-compliant manner. See bug 2005142
  const bool isValueOutOfRange = [](NonCustomCSSPropertyId aId, double aValue) {
    switch (aId) {
      case eCSSProperty_font_size_adjust:
      case eCSSProperty_font_stretch:
      case eCSSProperty_flex_grow:
      case eCSSProperty_flex_shrink:
      case eCSSProperty_stroke_miterlimit:
      case eCSSProperty_animation_duration:
      case eCSSProperty_animation_iteration_count:
      case eCSSProperty_background_size:
      case eCSSProperty_column_width:
      case eCSSProperty_flex_basis:
      case eCSSProperty_font_size:
      case eCSSProperty_line_height:
      case eCSSProperty_perspective:
      case eCSSProperty_stroke_dasharray:
      case eCSSProperty_stroke_width:
      case eCSSProperty_tab_size:
      case eCSSProperty_transition_duration:
      case eCSSProperty_column_gap:
      case eCSSProperty_row_gap:
      case eCSSProperty_max_block_size:
      case eCSSProperty_max_height:
      case eCSSProperty_max_inline_size:
      case eCSSProperty_max_width:
      case eCSSProperty_block_size:
      case eCSSProperty_height:
      case eCSSProperty_inline_size:
      case eCSSProperty_min_block_size:
      case eCSSProperty_min_height:
      case eCSSProperty_min_inline_size:
      case eCSSProperty_min_width:
      case eCSSProperty_width:
      case eCSSProperty_border_block_end_width:
      case eCSSProperty_border_block_start_width:
      case eCSSProperty_border_bottom_width:
      case eCSSProperty_border_inline_end_width:
      case eCSSProperty_border_inline_start_width:
      case eCSSProperty_border_left_width:
      case eCSSProperty_border_right_width:
      case eCSSProperty_border_top_width:
      case eCSSProperty_outline_width:
      case eCSSProperty_padding_block_end:
      case eCSSProperty_padding_block_start:
      case eCSSProperty_padding_bottom:
      case eCSSProperty_padding_inline_end:
      case eCSSProperty_padding_inline_start:
      case eCSSProperty_padding_left:
      case eCSSProperty_padding_right:
      case eCSSProperty_padding_top:
      case eCSSProperty_r:
      case eCSSProperty_shape_margin:
      case eCSSProperty_rx:
      case eCSSProperty_ry:
      case eCSSProperty_scroll_padding_block_end:
      case eCSSProperty_scroll_padding_block_start:
      case eCSSProperty_scroll_padding_bottom:
      case eCSSProperty_scroll_padding_inline_end:
      case eCSSProperty_scroll_padding_inline_start:
      case eCSSProperty_scroll_padding_left:
      case eCSSProperty_scroll_padding_right:
      case eCSSProperty_scroll_padding_top:
        return aValue < 0;

      case eCSSProperty_font_weight:
        return aValue < 1 || aValue > 1000;

      default:
        return false;
    }
  }(aPropertyId.mId, mValue);

  if (isValueOutOfRange) {
    aDest.Append("calc("_ns);
  }

  aDest.AppendFloat(mValue);

  if (mUnit.Equals("percent"_ns)) {
    aDest.Append("%"_ns);
  } else if (!mUnit.Equals("number"_ns)) {
    aDest.Append(mUnit);
  }

  if (isValueOutOfRange) {
    aDest.Append(")"_ns);
  }
}

StyleUnitValue CSSUnitValue::ToStyleUnitValue() const {
  return StyleUnitValue(mValue, StyleCssString(mUnit));
}

const CSSUnitValue& CSSNumericValue::GetAsCSSUnitValue() const {
  MOZ_DIAGNOSTIC_ASSERT(mNumericValueType == NumericValueType::UnitValue);

  return *static_cast<const CSSUnitValue*>(this);
}

CSSUnitValue& CSSNumericValue::GetAsCSSUnitValue() {
  MOZ_DIAGNOSTIC_ASSERT(mNumericValueType == NumericValueType::UnitValue);

  return *static_cast<CSSUnitValue*>(this);
}

}  // namespace mozilla::dom
