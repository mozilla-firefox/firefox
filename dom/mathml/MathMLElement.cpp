/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/MathMLElement.h"

#include "mozilla/FocusModel.h"
#include "mozilla/dom/BindContext.h"
#include "mozilla/ArrayUtils.h"
#include "mozilla/EventListenerManager.h"
#include "mozilla/StaticPrefs_mathml.h"
#include "mozilla/TextUtils.h"
#include "nsGkAtoms.h"
#include "nsIContentInlines.h"
#include "nsITableCellLayout.h"  // for MAX_COLSPAN / MAX_ROWSPAN
#include "nsCSSValue.h"
#include "nsStyleConsts.h"
#include "mozilla/dom/Document.h"
#include "nsPresContext.h"
#include "nsIScriptError.h"
#include "nsContentUtils.h"
#include "nsIURI.h"

#include "mozilla/EventDispatcher.h"
#include "mozilla/MappedDeclarationsBuilder.h"
#include "mozilla/dom/MathMLElementBinding.h"

using namespace mozilla;
using namespace mozilla::dom;

//----------------------------------------------------------------------
// nsISupports methods:

NS_IMPL_ISUPPORTS_INHERITED(MathMLElement, MathMLElementBase, Link)

static nsresult ReportLengthParseError(const nsString& aValue,
                                       Document* aDocument) {
  AutoTArray<nsString, 1> arg = {aValue};
  return nsContentUtils::ReportToConsole(
      nsIScriptError::errorFlag, "MathML"_ns, aDocument,
      nsContentUtils::eMATHML_PROPERTIES, "LengthParsingError", arg);
}

static nsresult ReportParseErrorNoTag(const nsString& aValue, nsAtom* aAtom,
                                      Document& aDocument) {
  AutoTArray<nsString, 2> argv = {aValue, nsDependentAtomString(aAtom)};
  return nsContentUtils::ReportToConsole(
      nsIScriptError::errorFlag, "MathML"_ns, &aDocument,
      nsContentUtils::eMATHML_PROPERTIES, "AttributeParsingErrorNoTag", argv);
}

MathMLElement::MathMLElement(
    already_AddRefed<mozilla::dom::NodeInfo>& aNodeInfo)
    : MathMLElementBase(std::move(aNodeInfo)), Link(this) {}

MathMLElement::MathMLElement(
    already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo)
    : MathMLElementBase(std::move(aNodeInfo)), Link(this) {}

nsresult MathMLElement::BindToTree(BindContext& aContext, nsINode& aParent) {
  nsresult rv = MathMLElementBase::BindToTree(aContext, aParent);
  NS_ENSURE_SUCCESS(rv, rv);

  Link::BindToTree(aContext);

  // Set the bit in the document for telemetry.
  if (Document* doc = aContext.GetComposedDoc()) {
    doc->SetUseCounter(eUseCounter_custom_MathMLUsed);
  }

  return rv;
}

void MathMLElement::UnbindFromTree(UnbindContext& aContext) {
  MathMLElementBase::UnbindFromTree(aContext);
  // Without removing the link state we risk a dangling pointer in the
  // mStyledLinks hashtable
  Link::UnbindFromTree();
}

bool MathMLElement::ParseAttribute(int32_t aNamespaceID, nsAtom* aAttribute,
                                   const nsAString& aValue,
                                   nsIPrincipal* aMaybeScriptedPrincipal,
                                   nsAttrValue& aResult) {
  MOZ_ASSERT(IsMathMLElement());

  if (aNamespaceID == kNameSpaceID_None) {
    if (aAttribute == nsGkAtoms::mathcolor ||
        aAttribute == nsGkAtoms::mathbackground) {
      return aResult.ParseColor(aValue);
    }
    if (aAttribute == nsGkAtoms::tabindex) {
      return aResult.ParseIntValue(aValue);
    }
    if (mNodeInfo->Equals(nsGkAtoms::mtd)) {
      if (aAttribute == nsGkAtoms::columnspan) {
        aResult.ParseClampedNonNegativeInt(aValue, 1, 1, MAX_COLSPAN);
        return true;
      }
      if (aAttribute == nsGkAtoms::rowspan) {
        aResult.ParseClampedNonNegativeInt(aValue, 1, 0, MAX_ROWSPAN);
        return true;
      }
    }
  }

  return MathMLElementBase::ParseAttribute(aNamespaceID, aAttribute, aValue,
                                           aMaybeScriptedPrincipal, aResult);
}

// https://mathml-refresh.github.io/mathml-core/#global-attributes
static Element::MappedAttributeEntry sGlobalAttributes[] = {
    {nsGkAtoms::dir},
    {nsGkAtoms::mathbackground},
    {nsGkAtoms::mathcolor},
    {nsGkAtoms::mathsize},
    {nsGkAtoms::scriptlevel},
    {nsGkAtoms::displaystyle},
    {nullptr}};

bool MathMLElement::IsAttributeMapped(const nsAtom* aAttribute) const {
  MOZ_ASSERT(IsMathMLElement());

  static const MappedAttributeEntry* const globalMap[] = {sGlobalAttributes};

  return FindAttributeDependence(aAttribute, globalMap) ||
         ((!StaticPrefs::mathml_legacy_mathvariant_attribute_disabled() ||
           mNodeInfo->Equals(nsGkAtoms::mi)) &&
          aAttribute == nsGkAtoms::mathvariant) ||
         (mNodeInfo->Equals(nsGkAtoms::mtable) &&
          aAttribute == nsGkAtoms::width);
}

nsMapRuleToAttributesFunc MathMLElement::GetAttributeMappingFunction() const {
  if (mNodeInfo->Equals(nsGkAtoms::mtable)) {
    return &MapMTableAttributesInto;
  }
  if (StaticPrefs::mathml_legacy_mathvariant_attribute_disabled() &&
      mNodeInfo->Equals(nsGkAtoms::mi)) {
    return &MapMiAttributesInto;
  }
  return &MapGlobalMathMLAttributesInto;
}

/* static */
bool MathMLElement::ParseNamedSpaceValue(const nsString& aString,
                                         nsCSSValue& aCSSValue, uint32_t aFlags,
                                         const Document& aDocument) {
  if (StaticPrefs::mathml_mathspace_names_disabled()) {
    return false;
  }
  int32_t i = 0;
  // See if it is one of the 'namedspace' (ranging -7/18em, -6/18, ... 7/18em)
  if (aString.EqualsLiteral("veryverythinmathspace")) {
    i = 1;
  } else if (aString.EqualsLiteral("verythinmathspace")) {
    i = 2;
  } else if (aString.EqualsLiteral("thinmathspace")) {
    i = 3;
  } else if (aString.EqualsLiteral("mediummathspace")) {
    i = 4;
  } else if (aString.EqualsLiteral("thickmathspace")) {
    i = 5;
  } else if (aString.EqualsLiteral("verythickmathspace")) {
    i = 6;
  } else if (aString.EqualsLiteral("veryverythickmathspace")) {
    i = 7;
  } else if (aFlags & PARSE_ALLOW_NEGATIVE) {
    if (aString.EqualsLiteral("negativeveryverythinmathspace")) {
      i = -1;
    } else if (aString.EqualsLiteral("negativeverythinmathspace")) {
      i = -2;
    } else if (aString.EqualsLiteral("negativethinmathspace")) {
      i = -3;
    } else if (aString.EqualsLiteral("negativemediummathspace")) {
      i = -4;
    } else if (aString.EqualsLiteral("negativethickmathspace")) {
      i = -5;
    } else if (aString.EqualsLiteral("negativeverythickmathspace")) {
      i = -6;
    } else if (aString.EqualsLiteral("negativeveryverythickmathspace")) {
      i = -7;
    }
  }
  if (0 != i) {
    AutoTArray<nsString, 1> params;
    params.AppendElement(aString);
    aDocument.WarnOnceAbout(
        dom::DeprecatedOperations::eMathML_DeprecatedMathSpaceValue2, false,
        params);
    aCSSValue.SetFloatValue(float(i) / float(18), eCSSUnit_EM);
    return true;
  }

  return false;
}

// The REC says:
//
// "Most presentation elements have attributes that accept values representing
// lengths to be used for size, spacing or similar properties. The syntax of a
// length is specified as
//
// number | number unit | namedspace
//
// There should be no space between the number and the unit of a length."
//
// "A trailing '%' represents a percent of the default value. The default
// value, or how it is obtained, is listed in the table of attributes for each
// element. [...] A number without a unit is intepreted as a multiple of the
// default value."
//
// "The possible units in MathML are:
//
// Unit Description
// em   an em (font-relative unit traditionally used for horizontal lengths)
// ex   an ex (font-relative unit traditionally used for vertical lengths)
// px   pixels, or size of a pixel in the current display
// in   inches (1 inch = 2.54 centimeters)
// cm   centimeters
// mm   millimeters
// pt   points (1 point = 1/72 inch)
// pc   picas (1 pica = 12 points)
// %    percentage of default value"
//
// The numbers are defined that way:
// - unsigned-number: "a string of decimal digits with up to one decimal point
//   (U+002E), representing a non-negative terminating decimal number (a type of
//   rational number)"
// - number: "an optional prefix of '-' (U+002D), followed by an unsigned
//   number, representing a terminating decimal number (a type of rational
//   number)"
//
/* static */
// XXXfredw: Deprecate legacy MathML syntax and use the CSS parser instead.
// See https://github.com/mathml-refresh/mathml/issues/63
bool MathMLElement::ParseNumericValue(const nsString& aString,
                                      nsCSSValue& aCSSValue, uint32_t aFlags,
                                      Document* aDocument) {
  nsAutoString str(aString);
  str.CompressWhitespace();  // aString is const in this code...

  int32_t stringLength = str.Length();
  if (!stringLength) {
    if (!(aFlags & PARSE_SUPPRESS_WARNINGS)) {
      ReportLengthParseError(aString, aDocument);
    }
    return false;
  }

  if (aDocument && ParseNamedSpaceValue(str, aCSSValue, aFlags, *aDocument)) {
    return true;
  }

  nsAutoString number, unit;

  // see if the negative sign is there
  int32_t i = 0;
  char16_t c = str[0];
  if (c == '-') {
    number.Append(c);
    i++;
  }

  // Gather up characters that make up the number
  bool gotDot = false;
  for (; i < stringLength; i++) {
    c = str[i];
    if (gotDot && c == '.') {
      if (!(aFlags & PARSE_SUPPRESS_WARNINGS)) {
        ReportLengthParseError(aString, aDocument);
      }
      return false;  // two dots encountered
    } else if (c == '.')
      gotDot = true;
    else if (!IsAsciiDigit(c)) {
      str.Right(unit, stringLength - i);
      // some authors leave blanks before the unit, but that shouldn't
      // be allowed, so don't CompressWhitespace on 'unit'.
      break;
    }
    number.Append(c);
  }
  if (gotDot && str[i - 1] == '.') {
    if (!(aFlags & PARSE_SUPPRESS_WARNINGS)) {
      ReportLengthParseError(aString, aDocument);
    }
    return false;  // Number ending with a dot.
  }

  // Convert number to floating point
  nsresult errorCode;
  float floatValue = number.ToFloat(&errorCode);
  if (NS_FAILED(errorCode)) {
    if (!(aFlags & PARSE_SUPPRESS_WARNINGS)) {
      ReportLengthParseError(aString, aDocument);
    }
    return false;
  }
  if (floatValue < 0 && !(aFlags & PARSE_ALLOW_NEGATIVE)) {
    if (!(aFlags & PARSE_SUPPRESS_WARNINGS)) {
      ReportLengthParseError(aString, aDocument);
    }
    return false;
  }

  nsCSSUnit cssUnit;
  if (unit.IsEmpty()) {
    // We are supposed to have a unit, but there isn't one.
    // If the value is 0 we can just call it "pixels" otherwise
    // this is illegal.
    if (floatValue != 0.0) {
      if (!(aFlags & PARSE_SUPPRESS_WARNINGS)) {
        ReportLengthParseError(aString, aDocument);
      }
      return false;
    }
    cssUnit = eCSSUnit_Pixel;
  } else if (unit.EqualsLiteral("%")) {
    aCSSValue.SetPercentValue(floatValue / 100.0f);
    return true;
  } else if (unit.LowerCaseEqualsLiteral("em"))
    cssUnit = eCSSUnit_EM;
  else if (unit.LowerCaseEqualsLiteral("ex"))
    cssUnit = eCSSUnit_XHeight;
  else if (unit.LowerCaseEqualsLiteral("px"))
    cssUnit = eCSSUnit_Pixel;
  else if (unit.LowerCaseEqualsLiteral("in"))
    cssUnit = eCSSUnit_Inch;
  else if (unit.LowerCaseEqualsLiteral("cm"))
    cssUnit = eCSSUnit_Centimeter;
  else if (unit.LowerCaseEqualsLiteral("mm"))
    cssUnit = eCSSUnit_Millimeter;
  else if (unit.LowerCaseEqualsLiteral("pt"))
    cssUnit = eCSSUnit_Point;
  else if (unit.LowerCaseEqualsLiteral("pc"))
    cssUnit = eCSSUnit_Pica;
  else if (unit.LowerCaseEqualsLiteral("q"))
    cssUnit = eCSSUnit_Quarter;
  else {  // unexpected unit
    if (!(aFlags & PARSE_SUPPRESS_WARNINGS)) {
      ReportLengthParseError(aString, aDocument);
    }
    return false;
  }

  aCSSValue.SetFloatValue(floatValue, cssUnit);
  return true;
}

void MathMLElement::MapMTableAttributesInto(
    MappedDeclarationsBuilder& aBuilder) {
  // width
  //
  // "Specifies the desired width of the entire table and is intended for
  // visual user agents. When the value is a percentage value, the value is
  // relative to the horizontal space a MathML renderer has available for the
  // math element. When the value is "auto", the MathML renderer should
  // calculate the table width from its contents using whatever layout
  // algorithm it chooses. "
  //
  // values: "auto" | length
  // default: auto
  //
  if (!aBuilder.PropertyIsSet(eCSSProperty_width)) {
    const nsAttrValue* value = aBuilder.GetAttr(nsGkAtoms::width);
    nsCSSValue width;
    // This does not handle auto and unitless values
    if (value && value->Type() == nsAttrValue::eString) {
      ParseNumericValue(value->GetStringValue(), width, 0,
                        &aBuilder.Document());
      if (width.GetUnit() == eCSSUnit_Percent) {
        aBuilder.SetPercentValue(eCSSProperty_width, width.GetPercentValue());
      } else if (width.GetUnit() != eCSSUnit_Null) {
        aBuilder.SetLengthValue(eCSSProperty_width, width);
      }
    }
  }
  MapGlobalMathMLAttributesInto(aBuilder);
}

void MathMLElement::MapMiAttributesInto(MappedDeclarationsBuilder& aBuilder) {
  // mathvariant
  // https://w3c.github.io/mathml-core/#dfn-mathvariant
  if (!aBuilder.PropertyIsSet(eCSSProperty_text_transform)) {
    const nsAttrValue* value = aBuilder.GetAttr(nsGkAtoms::mathvariant);
    if (value && value->Type() == nsAttrValue::eString) {
      auto str = value->GetStringValue();
      str.CompressWhitespace();
      if (value->GetStringValue().LowerCaseEqualsASCII("normal")) {
        aBuilder.SetKeywordValue(eCSSProperty_text_transform,
                                 StyleTextTransform::NONE._0);
      }
    }
  }
  MapGlobalMathMLAttributesInto(aBuilder);
}

// Helper consteval function, similar in spirit to memmem(3).
// It is only meant to be used at compile-time and uses a naive algorithm for
// maintainability. The impact on compile time should be negligible given the
// input size, and there's no runtime cost.
template <uint8_t N, uint8_t M>
static constexpr uint8_t cmemmemi(const char (&needle)[N],
                                  const char (&haystack)[M]) {
  static_assert(M > N, "needle larger than haystack");
  for (uint8_t i = 0; i < M - N; ++i) {
    for (uint8_t j = 0; j < N; ++j) {
      if (needle[j] != haystack[i + j]) {
        break;
      }
      if (needle[j] == '\0') {
        return i;
      }
    }
  }
  // Trigger an illegal access in the parent array at compile time.
  return std::numeric_limits<uint8_t>::max();
}

void MathMLElement::MapGlobalMathMLAttributesInto(
    MappedDeclarationsBuilder& aBuilder) {
  // scriptlevel
  // https://w3c.github.io/mathml-core/#dfn-scriptlevel
  const nsAttrValue* value = aBuilder.GetAttr(nsGkAtoms::scriptlevel);
  if (value && value->Type() == nsAttrValue::eString &&
      !aBuilder.PropertyIsSet(eCSSProperty_math_depth)) {
    auto str = value->GetStringValue();
    // FIXME: Should we remove whitespace trimming?
    // See https://github.com/w3c/mathml/issues/122
    str.CompressWhitespace();
    if (str.Length() > 0) {
      nsresult errorCode;
      int32_t intValue = str.ToInteger(&errorCode);
      bool reportParseError = true;
      if (NS_SUCCEEDED(errorCode)) {
        char16_t ch = str.CharAt(0);
        bool isRelativeScriptLevel = (ch == '+' || ch == '-');
        // ToInteger is not very strict, check this is really <unsigned>.
        reportParseError = false;
        for (uint32_t i = isRelativeScriptLevel ? 1 : 0; i < str.Length();
             i++) {
          if (!IsAsciiDigit(str.CharAt(i))) {
            reportParseError = true;
            break;
          }
        }
        if (!reportParseError) {
          aBuilder.SetMathDepthValue(intValue, isRelativeScriptLevel);
        }
      }
      if (reportParseError) {
        ReportParseErrorNoTag(str, nsGkAtoms::scriptlevel, aBuilder.Document());
      }
    }
  }

  // mathsize
  // https://w3c.github.io/mathml-core/#dfn-mathsize
  value = aBuilder.GetAttr(nsGkAtoms::mathsize);
  if (value && value->Type() == nsAttrValue::eString &&
      !aBuilder.PropertyIsSet(eCSSProperty_font_size)) {
    auto str = value->GetStringValue();
    nsCSSValue fontSize;
    ParseNumericValue(str, fontSize, 0, nullptr);
    if (fontSize.GetUnit() == eCSSUnit_Percent) {
      aBuilder.SetPercentValue(eCSSProperty_font_size,
                               fontSize.GetPercentValue());
    } else if (fontSize.GetUnit() != eCSSUnit_Null) {
      aBuilder.SetLengthValue(eCSSProperty_font_size, fontSize);
    }
  }

  if (!StaticPrefs::mathml_legacy_mathvariant_attribute_disabled()) {
    // mathvariant
    //
    // "Specifies the logical class of the token. Note that this class is more
    // than styling, it typically conveys semantic intent;"
    //
    // values: "normal" | "bold" | "italic" | "bold-italic" | "double-struck" |
    // "bold-fraktur" | "script" | "bold-script" | "fraktur" | "sans-serif" |
    // "bold-sans-serif" | "sans-serif-italic" | "sans-serif-bold-italic" |
    // "monospace" | "initial" | "tailed" | "looped" | "stretched"
    // default: normal (except on <mi>)
    //
    value = aBuilder.GetAttr(nsGkAtoms::mathvariant);
    if (value && value->Type() == nsAttrValue::eString &&
        !aBuilder.PropertyIsSet(eCSSProperty__moz_math_variant)) {
      auto str = value->GetStringValue();
      str.CompressWhitespace();

      // Instead of a big table that holds all sizes, store a compressed version
      // with offset, taking advantage of common suffixes.
      //
      // naive approach:
      //     sizeof(char_table)
      //   = |size| x |max-length|
      //   = 19 x 23
      //   = 437
      //
      // offset approach:
      //     sizeof(offset_table) + sizeof(compressed_table)
      //   = |size| x |sizeof(uint8_t)| + 151
      //   = 19 x 1 + 151
      //   = 170

      static constexpr const char compressed_sizes[] =
          "normal\0"
          "bold\0"
          "bold-script\0"
          "double-struck\0"
          "bold-fraktur\0"
          "bold-sans-serif\0"
          "sans-serif-italic\0"
          "sans-serif-bold-italic\0"
          "monospace\0"
          "initial\0"
          "tailed\0"
          "looped\0"
          "stretched\0";

      static constexpr uint8_t value_indices[] = {
          cmemmemi("normal", compressed_sizes),
          cmemmemi("bold", compressed_sizes),
          cmemmemi("italic", compressed_sizes),
          cmemmemi("bold-italic", compressed_sizes),
          cmemmemi("script", compressed_sizes),
          cmemmemi("bold-script", compressed_sizes),
          cmemmemi("fraktur", compressed_sizes),
          cmemmemi("double-struck", compressed_sizes),
          cmemmemi("bold-fraktur", compressed_sizes),
          cmemmemi("sans-serif", compressed_sizes),
          cmemmemi("bold-sans-serif", compressed_sizes),
          cmemmemi("sans-serif-italic", compressed_sizes),
          cmemmemi("sans-serif-bold-italic", compressed_sizes),
          cmemmemi("monospace", compressed_sizes),
          cmemmemi("initial", compressed_sizes),
          cmemmemi("tailed", compressed_sizes),
          cmemmemi("looped", compressed_sizes),
          cmemmemi("stretched", compressed_sizes),
      };

      for (size_t i = 0; i < std::size(value_indices); ++i) {
        if (str.LowerCaseEqualsASCII(&compressed_sizes[value_indices[i]])) {
          // Convert the index to an enum. We skip the "none" style thus the
          // + 1.
          StyleMathVariant value = (StyleMathVariant)(i + 1);
          if (value != StyleMathVariant::Normal) {
            // Warn about deprecated mathvariant attribute values. Strictly
            // speaking, we should also warn about mathvariant="normal" if the
            // element is not an <mi>. However this would require exposing the
            // tag name via aBuilder. Moreover, this use case is actually to
            // revert the effect of a non-normal mathvariant value on an
            // ancestor element, which should consequently have already
            // triggered a warning.
            AutoTArray<nsString, 1> params;
            params.AppendElement(str);
            aBuilder.Document().WarnOnceAbout(
                dom::DeprecatedOperations::eMathML_DeprecatedMathVariant, false,
                params);
          }
          aBuilder.SetKeywordValue(eCSSProperty__moz_math_variant, value);
          break;
        }
      }
    }
  }

  // mathbackground
  // https://w3c.github.io/mathml-core/#dfn-mathbackground
  value = aBuilder.GetAttr(nsGkAtoms::mathbackground);
  if (value) {
    nscolor color;
    if (value->GetColorValue(color)) {
      aBuilder.SetColorValueIfUnset(eCSSProperty_background_color, color);
    }
  }

  // mathcolor
  // https://w3c.github.io/mathml-core/#dfn-mathcolor
  value = aBuilder.GetAttr(nsGkAtoms::mathcolor);
  nscolor color;
  if (value && value->GetColorValue(color)) {
    aBuilder.SetColorValueIfUnset(eCSSProperty_color, color);
  }

  // dir
  // https://w3c.github.io/mathml-core/#dfn-dir
  value = aBuilder.GetAttr(nsGkAtoms::dir);
  if (value && value->Type() == nsAttrValue::eString &&
      !aBuilder.PropertyIsSet(eCSSProperty_direction)) {
    auto str = value->GetStringValue();
    static const char dirs[][4] = {"ltr", "rtl"};
    static const StyleDirection dirValues[std::size(dirs)] = {
        StyleDirection::Ltr, StyleDirection::Rtl};
    for (uint32_t i = 0; i < std::size(dirs); ++i) {
      if (str.LowerCaseEqualsASCII(dirs[i])) {
        aBuilder.SetKeywordValue(eCSSProperty_direction, dirValues[i]);
        break;
      }
    }
  }

  // displaystyle
  // https://mathml-refresh.github.io/mathml-core/#dfn-displaystyle
  value = aBuilder.GetAttr(nsGkAtoms::displaystyle);
  if (value && value->Type() == nsAttrValue::eString &&
      !aBuilder.PropertyIsSet(eCSSProperty_math_style)) {
    auto str = value->GetStringValue();
    static const char displaystyles[][6] = {"false", "true"};
    static const StyleMathStyle mathStyle[std::size(displaystyles)] = {
        StyleMathStyle::Compact, StyleMathStyle::Normal};
    for (uint32_t i = 0; i < std::size(displaystyles); ++i) {
      if (str.LowerCaseEqualsASCII(displaystyles[i])) {
        aBuilder.SetKeywordValue(eCSSProperty_math_style, mathStyle[i]);
        break;
      }
    }
  }
}

void MathMLElement::GetEventTargetParent(EventChainPreVisitor& aVisitor) {
  Element::GetEventTargetParent(aVisitor);

  GetEventTargetParentForLinks(aVisitor);
}

nsresult MathMLElement::PostHandleEvent(EventChainPostVisitor& aVisitor) {
  return PostHandleEventForLinks(aVisitor);
}

NS_IMPL_ELEMENT_CLONE(MathMLElement)

void MathMLElement::SetIncrementScriptLevel(bool aIncrementScriptLevel,
                                            bool aNotify) {
  NS_ASSERTION(aNotify, "We always notify!");
  if (aIncrementScriptLevel) {
    AddStates(ElementState::INCREMENT_SCRIPT_LEVEL);
  } else {
    RemoveStates(ElementState::INCREMENT_SCRIPT_LEVEL);
  }
}

int32_t MathMLElement::TabIndexDefault() { return IsLink() ? 0 : -1; }

// XXX Bug 1586011: Share logic with other element classes.
Focusable MathMLElement::IsFocusableWithoutStyle(IsFocusableFlags) {
  if (!IsInComposedDoc() || IsInDesignMode()) {
    // In designMode documents we only allow focusing the document.
    return {};
  }

  int32_t tabIndex = TabIndex();
  if (!IsLink()) {
    // If a tabindex is specified at all we're focusable
    if (GetTabIndexAttrValue().isSome()) {
      return {true, tabIndex};
    }
    return {};
  }

  if (!OwnerDoc()->LinkHandlingEnabled()) {
    return {};
  }

  // Links that are in an editable region should never be focusable, even if
  // they are in a contenteditable="false" region.
  if (nsContentUtils::IsNodeInEditableRegion(this)) {
    return {};
  }

  if (!FocusModel::IsTabFocusable(TabFocusableType::Links)) {
    tabIndex = -1;
  }

  return {true, tabIndex};
}

already_AddRefed<nsIURI> MathMLElement::GetHrefURI() const {
  // MathML href
  // The REC says: "When user agents encounter MathML elements with both href
  // and xlink:href attributes, the href attribute should take precedence."
  const nsAttrValue* href = mAttrs.GetAttr(nsGkAtoms::href, kNameSpaceID_None);
  if (!href) {
    return nullptr;
  }
  // Get absolute URI
  nsAutoString hrefStr;
  href->ToString(hrefStr);
  nsCOMPtr<nsIURI> hrefURI;
  nsContentUtils::NewURIWithDocumentCharset(getter_AddRefs(hrefURI), hrefStr,
                                            OwnerDoc(), GetBaseURI());
  return hrefURI.forget();
}

bool MathMLElement::IsEventAttributeNameInternal(nsAtom* aName) {
  // The intent is to align MathML event attributes on HTML5, so the flag
  // EventNameType_HTML is used here.
  return nsContentUtils::IsEventAttributeName(aName, EventNameType_HTML);
}

void MathMLElement::BeforeSetAttr(int32_t aNamespaceID, nsAtom* aName,
                                  const nsAttrValue* aValue, bool aNotify) {
  if (aNamespaceID == kNameSpaceID_None) {
    if (!aValue && IsEventAttributeName(aName)) {
      if (EventListenerManager* manager = GetExistingListenerManager()) {
        manager->RemoveEventHandler(GetEventNameForAttr(aName));
      }
    }
  }

  return MathMLElementBase::BeforeSetAttr(aNamespaceID, aName, aValue, aNotify);
}

void MathMLElement::AfterSetAttr(int32_t aNameSpaceID, nsAtom* aName,
                                 const nsAttrValue* aValue,
                                 const nsAttrValue* aOldValue,
                                 nsIPrincipal* aSubjectPrincipal,
                                 bool aNotify) {
  // It is important that this be done after the attribute is set/unset.
  // We will need the updated attribute value because notifying the document
  // that content states have changed will call IntrinsicState, which will try
  // to get updated information about the visitedness from Link.
  if (aName == nsGkAtoms::href && aNameSpaceID == kNameSpaceID_None) {
    Link::ResetLinkState(aNotify, aValue || Link::ElementHasHref());
  }

  if (aNameSpaceID == kNameSpaceID_None) {
    if (IsEventAttributeName(aName) && aValue) {
      MOZ_ASSERT(aValue->Type() == nsAttrValue::eString,
                 "Expected string value for script body");
      SetEventHandler(GetEventNameForAttr(aName), aValue->GetStringValue());
    }
  }

  return MathMLElementBase::AfterSetAttr(aNameSpaceID, aName, aValue, aOldValue,
                                         aSubjectPrincipal, aNotify);
}

JSObject* MathMLElement::WrapNode(JSContext* aCx,
                                  JS::Handle<JSObject*> aGivenProto) {
  return MathMLElement_Binding::Wrap(aCx, this, aGivenProto);
}
