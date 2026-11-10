/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CSSEditUtils.h"

#include "ChangeStyleTransaction.h"
#include "EditorDOMAPIWrapper.h"
#include "HTMLEditHelpers.h"
#include "HTMLEditor.h"
#include "HTMLEditUtils.h"

#include "mozilla/Assertions.h"
#include "mozilla/DeclarationBlock.h"
#include "mozilla/mozalloc.h"
#include "mozilla/Preferences.h"
#include "mozilla/ServoCSSParser.h"
#include "mozilla/StaticPrefs_browser.h"
#include "mozilla/StaticPrefs_editor.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Element.h"
#include "nsAString.h"
#include "nsCOMPtr.h"
#include "nsCSSProps.h"
#include "nsColor.h"
#include "nsComputedDOMStyle.h"
#include "nsDebug.h"
#include "nsDependentSubstring.h"
#include "nsError.h"
#include "nsGkAtoms.h"
#include "nsAtom.h"
#include "nsIContent.h"
#include "nsICSSDeclaration.h"
#include "nsINode.h"
#include "nsISupportsImpl.h"
#include "nsISupportsUtils.h"
#include "nsLiteralString.h"
#include "nsPIDOMWindow.h"
#include "nsReadableUtils.h"
#include "nsString.h"
#include "nsStringFwd.h"
#include "nsStringIterator.h"
#include "nsStyledElement.h"
#include "nsUnicharUtils.h"

namespace mozilla {

using namespace dom;

static void ProcessBValue(const nsAString* aInputString,
                          nsAString& aOutputString,
                          const char* /*aDefaultValueString*/,
                          const char* /*aPrependString*/,
                          const char* /*aAppendString*/) {
  if (aInputString && aInputString->EqualsLiteral("-moz-editor-invert-value")) {
    aOutputString.AssignLiteral("normal");
  } else {
    aOutputString.AssignLiteral("bold");
  }
}

static void ProcessDefaultValue(const nsAString* /*aInputString*/,
                                nsAString& aOutputString,
                                const char* aDefaultValueString,
                                const char* /*aPrependString*/,
                                const char* /*aAppendString*/) {
  CopyASCIItoUTF16(MakeStringSpan(aDefaultValueString), aOutputString);
}

static void ProcessSameValue(const nsAString* aInputString,
                             nsAString& aOutputString,
                             const char* /*aDefaultValueString*/,
                             const char* /*aPrependString*/,
                             const char* /*aAppendString*/) {
  if (aInputString) {
    aOutputString.Assign(*aInputString);
  } else
    aOutputString.Truncate();
}

static void ProcessExtendedValue(const nsAString* aInputString,
                                 nsAString& aOutputString,
                                 const char* /*aDefaultValueString*/,
                                 const char* aPrependString,
                                 const char* aAppendString) {
  aOutputString.Truncate();
  if (aInputString) {
    if (aPrependString) {
      AppendASCIItoUTF16(MakeStringSpan(aPrependString), aOutputString);
    }
    aOutputString.Append(*aInputString);
    if (aAppendString) {
      AppendASCIItoUTF16(MakeStringSpan(aAppendString), aOutputString);
    }
  }
}

static void ProcessLengthValue(const nsAString* aInputString,
                               nsAString& aOutputString,
                               const char* /*aDefaultValueString*/,
                               const char* /*aPrependString*/,
                               const char* /*aAppendString*/) {
  aOutputString.Truncate();
  if (aInputString) {
    aOutputString.Append(*aInputString);
    if (-1 == aOutputString.FindChar(char16_t('%'))) {
      aOutputString.AppendLiteral("px");
    }
  }
}

static void ProcessListStyleTypeValue(const nsAString* aInputString,
                                      nsAString& aOutputString,
                                      const char* /*aDefaultValueString*/,
                                      const char* /*aPrependString*/,
                                      const char* /*aAppendString*/) {
  aOutputString.Truncate();
  if (aInputString) {
    if (aInputString->EqualsLiteral("1")) {
      aOutputString.AppendLiteral("decimal");
    } else if (aInputString->EqualsLiteral("a")) {
      aOutputString.AppendLiteral("lower-alpha");
    } else if (aInputString->EqualsLiteral("A")) {
      aOutputString.AppendLiteral("upper-alpha");
    } else if (aInputString->EqualsLiteral("i")) {
      aOutputString.AppendLiteral("lower-roman");
    } else if (aInputString->EqualsLiteral("I")) {
      aOutputString.AppendLiteral("upper-roman");
    } else if (aInputString->EqualsLiteral("square") ||
               aInputString->EqualsLiteral("circle") ||
               aInputString->EqualsLiteral("disc")) {
      aOutputString.Append(*aInputString);
    }
  }
}

static void ProcessMarginLeftValue(const nsAString* aInputString,
                                   nsAString& aOutputString,
                                   const char* /*aDefaultValueString*/,
                                   const char* /*aPrependString*/,
                                   const char* /*aAppendString*/) {
  aOutputString.Truncate();
  if (aInputString) {
    if (aInputString->EqualsLiteral("center") ||
        aInputString->EqualsLiteral("-moz-center")) {
      aOutputString.AppendLiteral("auto");
    } else if (aInputString->EqualsLiteral("right") ||
               aInputString->EqualsLiteral("-moz-right")) {
      aOutputString.AppendLiteral("auto");
    } else {
      aOutputString.AppendLiteral("0px");
    }
  }
}

static void ProcessMarginRightValue(const nsAString* aInputString,
                                    nsAString& aOutputString,
                                    const char* /*aDefaultValueString*/,
                                    const char* /*aPrependString*/,
                                    const char* /*aAppendString*/) {
  aOutputString.Truncate();
  if (aInputString) {
    if (aInputString->EqualsLiteral("center") ||
        aInputString->EqualsLiteral("-moz-center")) {
      aOutputString.AppendLiteral("auto");
    } else if (aInputString->EqualsLiteral("left") ||
               aInputString->EqualsLiteral("-moz-left")) {
      aOutputString.AppendLiteral("auto");
    } else {
      aOutputString.AppendLiteral("0px");
    }
  }
}

#define CSS_EQUIV_TABLE_NONE {CSSEditUtils::eCSSEditableProperty_NONE, 0}

const CSSEditUtils::CSSEquivTable boldEquivTable[] = {
    {CSSEditUtils::eCSSEditableProperty_font_weight, true, false, ProcessBValue,
     nullptr, nullptr, nullptr},
    CSS_EQUIV_TABLE_NONE};

const CSSEditUtils::CSSEquivTable italicEquivTable[] = {
    {CSSEditUtils::eCSSEditableProperty_font_style, true, false,
     ProcessDefaultValue, "italic", nullptr, nullptr},
    CSS_EQUIV_TABLE_NONE};

const CSSEditUtils::CSSEquivTable underlineEquivTable[] = {
    {CSSEditUtils::eCSSEditableProperty_text_decoration, true, false,
     ProcessDefaultValue, "underline", nullptr, nullptr},
    CSS_EQUIV_TABLE_NONE};

const CSSEditUtils::CSSEquivTable strikeEquivTable[] = {
    {CSSEditUtils::eCSSEditableProperty_text_decoration, true, false,
     ProcessDefaultValue, "line-through", nullptr, nullptr},
    CSS_EQUIV_TABLE_NONE};

const CSSEditUtils::CSSEquivTable ttEquivTable[] = {
    {CSSEditUtils::eCSSEditableProperty_font_family, true, false,
     ProcessDefaultValue, "monospace", nullptr, nullptr},
    CSS_EQUIV_TABLE_NONE};

const CSSEditUtils::CSSEquivTable fontColorEquivTable[] = {
    {CSSEditUtils::eCSSEditableProperty_color, true, false, ProcessSameValue,
     nullptr, nullptr, nullptr},
    CSS_EQUIV_TABLE_NONE};

const CSSEditUtils::CSSEquivTable fontFaceEquivTable[] = {
    {CSSEditUtils::eCSSEditableProperty_font_family, true, false,
     ProcessSameValue, nullptr, nullptr, nullptr},
    CSS_EQUIV_TABLE_NONE};

const CSSEditUtils::CSSEquivTable fontSizeEquivTable[] = {
    {CSSEditUtils::eCSSEditableProperty_font_size, true, false,
     ProcessSameValue, nullptr, nullptr, nullptr},
    CSS_EQUIV_TABLE_NONE};

const CSSEditUtils::CSSEquivTable bgcolorEquivTable[] = {
    {CSSEditUtils::eCSSEditableProperty_background_color, true, false,
     ProcessSameValue, nullptr, nullptr, nullptr},
    CSS_EQUIV_TABLE_NONE};

const CSSEditUtils::CSSEquivTable backgroundImageEquivTable[] = {
    {CSSEditUtils::eCSSEditableProperty_background_image, true, true,
     ProcessExtendedValue, nullptr, "url(", ")"},
    CSS_EQUIV_TABLE_NONE};

const CSSEditUtils::CSSEquivTable textColorEquivTable[] = {
    {CSSEditUtils::eCSSEditableProperty_color, true, false, ProcessSameValue,
     nullptr, nullptr, nullptr},
    CSS_EQUIV_TABLE_NONE};

const CSSEditUtils::CSSEquivTable borderEquivTable[] = {
    {CSSEditUtils::eCSSEditableProperty_border, true, false,
     ProcessExtendedValue, nullptr, nullptr, "px solid"},
    CSS_EQUIV_TABLE_NONE};

const CSSEditUtils::CSSEquivTable textAlignEquivTable[] = {
    {CSSEditUtils::eCSSEditableProperty_text_align, true, false,
     ProcessSameValue, nullptr, nullptr, nullptr},
    CSS_EQUIV_TABLE_NONE};

const CSSEditUtils::CSSEquivTable captionAlignEquivTable[] = {
    {CSSEditUtils::eCSSEditableProperty_caption_side, true, false,
     ProcessSameValue, nullptr, nullptr, nullptr},
    CSS_EQUIV_TABLE_NONE};

const CSSEditUtils::CSSEquivTable verticalAlignEquivTable[] = {
    {CSSEditUtils::eCSSEditableProperty_vertical_align, true, false,
     ProcessSameValue, nullptr, nullptr, nullptr},
    CSS_EQUIV_TABLE_NONE};

const CSSEditUtils::CSSEquivTable nowrapEquivTable[] = {
    {CSSEditUtils::eCSSEditableProperty_whitespace, true, false,
     ProcessDefaultValue, "nowrap", nullptr, nullptr},
    CSS_EQUIV_TABLE_NONE};

const CSSEditUtils::CSSEquivTable widthEquivTable[] = {
    {CSSEditUtils::eCSSEditableProperty_width, true, false, ProcessLengthValue,
     nullptr, nullptr, nullptr},
    CSS_EQUIV_TABLE_NONE};

const CSSEditUtils::CSSEquivTable heightEquivTable[] = {
    {CSSEditUtils::eCSSEditableProperty_height, true, false, ProcessLengthValue,
     nullptr, nullptr, nullptr},
    CSS_EQUIV_TABLE_NONE};

const CSSEditUtils::CSSEquivTable listStyleTypeEquivTable[] = {
    {CSSEditUtils::eCSSEditableProperty_list_style_type, true, true,
     ProcessListStyleTypeValue, nullptr, nullptr, nullptr},
    CSS_EQUIV_TABLE_NONE};

const CSSEditUtils::CSSEquivTable tableAlignEquivTable[] = {
    {CSSEditUtils::eCSSEditableProperty_text_align, false, false,
     ProcessDefaultValue, "left", nullptr, nullptr},
    {CSSEditUtils::eCSSEditableProperty_margin_left, true, false,
     ProcessMarginLeftValue, nullptr, nullptr, nullptr},
    {CSSEditUtils::eCSSEditableProperty_margin_right, true, false,
     ProcessMarginRightValue, nullptr, nullptr, nullptr},
    CSS_EQUIV_TABLE_NONE};

const CSSEditUtils::CSSEquivTable hrAlignEquivTable[] = {
    {CSSEditUtils::eCSSEditableProperty_margin_left, true, false,
     ProcessMarginLeftValue, nullptr, nullptr, nullptr},
    {CSSEditUtils::eCSSEditableProperty_margin_right, true, false,
     ProcessMarginRightValue, nullptr, nullptr, nullptr},
    CSS_EQUIV_TABLE_NONE};

#undef CSS_EQUIV_TABLE_NONE

// static
bool CSSEditUtils::IsCSSEditableStyle(const Element& aElement,
                                      const EditorElementStyle& aStyle) {
  return CSSEditUtils::IsCSSEditableStyle(*aElement.NodeInfo()->NameAtom(),
                                          aStyle);
}

// static
bool CSSEditUtils::IsCSSEditableStyle(const nsAtom& aTagName,
                                      const EditorElementStyle& aStyle) {
  nsStaticAtom* const htmlProperty =
      aStyle.IsInlineStyle() ? aStyle.AsInlineStyle().mHTMLProperty : nullptr;
  nsAtom* const attributeOrStyle = aStyle.IsInlineStyle()
                                       ? aStyle.AsInlineStyle().mAttribute.get()
                                       : aStyle.Style();

  // HTML inline styles <b>, <i>, <tt> (chrome only), <u>, <strike>, <font
  // color> and <font style>.
  if (nsGkAtoms::b == htmlProperty || nsGkAtoms::i == htmlProperty ||
      nsGkAtoms::tt == htmlProperty || nsGkAtoms::u == htmlProperty ||
      nsGkAtoms::strike == htmlProperty ||
      (nsGkAtoms::font == htmlProperty &&
       (attributeOrStyle == nsGkAtoms::color ||
        attributeOrStyle == nsGkAtoms::face))) {
    return true;
  }

  // ALIGN attribute on elements supporting it
  if (attributeOrStyle == nsGkAtoms::align &&
      (&aTagName == nsGkAtoms::div || &aTagName == nsGkAtoms::p ||
       &aTagName == nsGkAtoms::h1 || &aTagName == nsGkAtoms::h2 ||
       &aTagName == nsGkAtoms::h3 || &aTagName == nsGkAtoms::h4 ||
       &aTagName == nsGkAtoms::h5 || &aTagName == nsGkAtoms::h6 ||
       &aTagName == nsGkAtoms::td || &aTagName == nsGkAtoms::th ||
       &aTagName == nsGkAtoms::table || &aTagName == nsGkAtoms::hr ||
       // For the above, why not use
       // HTMLEditUtils::SupportsAlignAttr?
       // It also checks for tbody, tfoot, thead.
       // Let's add the following elements here even
       // if "align" has a different meaning for them
       &aTagName == nsGkAtoms::legend || &aTagName == nsGkAtoms::caption)) {
    return true;
  }

  if (attributeOrStyle == nsGkAtoms::valign &&
      (&aTagName == nsGkAtoms::col || &aTagName == nsGkAtoms::colgroup ||
       &aTagName == nsGkAtoms::tbody || &aTagName == nsGkAtoms::td ||
       &aTagName == nsGkAtoms::th || &aTagName == nsGkAtoms::tfoot ||
       &aTagName == nsGkAtoms::thead || &aTagName == nsGkAtoms::tr)) {
    return true;
  }

  // attributes TEXT, BACKGROUND and BGCOLOR on <body>
  if (&aTagName == nsGkAtoms::body &&
      (attributeOrStyle == nsGkAtoms::text ||
       attributeOrStyle == nsGkAtoms::background ||
       attributeOrStyle == nsGkAtoms::bgcolor)) {
    return true;
  }

  // attribute BGCOLOR on other elements
  if (attributeOrStyle == nsGkAtoms::bgcolor) {
    return true;
  }

  // attributes HEIGHT, WIDTH and NOWRAP on <td> and <th>
  if ((&aTagName == nsGkAtoms::td || &aTagName == nsGkAtoms::th) &&
      (attributeOrStyle == nsGkAtoms::height ||
       attributeOrStyle == nsGkAtoms::width ||
       attributeOrStyle == nsGkAtoms::nowrap)) {
    return true;
  }

  // attributes HEIGHT and WIDTH on <table>
  if (&aTagName == nsGkAtoms::table && (attributeOrStyle == nsGkAtoms::height ||
                                        attributeOrStyle == nsGkAtoms::width)) {
    return true;
  }

  // attributes SIZE and WIDTH on <hr>
  if (&aTagName == nsGkAtoms::hr && (attributeOrStyle == nsGkAtoms::size ||
                                     attributeOrStyle == nsGkAtoms::width)) {
    return true;
  }

  // attribute TYPE on <ol>, <ul> and <li>
  if (attributeOrStyle == nsGkAtoms::type &&
      (&aTagName == nsGkAtoms::ol || &aTagName == nsGkAtoms::ul ||
       &aTagName == nsGkAtoms::li)) {
    return true;
  }

  if (&aTagName == nsGkAtoms::img && (attributeOrStyle == nsGkAtoms::border ||
                                      attributeOrStyle == nsGkAtoms::width ||
                                      attributeOrStyle == nsGkAtoms::height)) {
    return true;
  }

  // other elements that we can align using CSS even if they
  // can't carry the html ALIGN attribute
  if (attributeOrStyle == nsGkAtoms::align &&
      (&aTagName == nsGkAtoms::ul || &aTagName == nsGkAtoms::ol ||
       &aTagName == nsGkAtoms::dl || &aTagName == nsGkAtoms::li ||
       &aTagName == nsGkAtoms::dd || &aTagName == nsGkAtoms::dt ||
       &aTagName == nsGkAtoms::address || &aTagName == nsGkAtoms::pre)) {
    return true;
  }

  return false;
}

// The lowest level above the transaction; adds the CSS declaration
// "aProperty : aValue" to the inline styles carried by aStyledElement

// static
nsresult CSSEditUtils::SetCSSPropertyInternal(HTMLEditor& aHTMLEditor,
                                              nsStyledElement& aStyledElement,
                                              nsAtom& aProperty,
                                              const nsAString& aValue,
                                              bool aSuppressTxn) {
  const RefPtr<ChangeStyleTransaction> transaction =
      ChangeStyleTransaction::Create(aHTMLEditor, aStyledElement, aProperty,
                                     aValue);
  if (aSuppressTxn) {
    nsresult rv = transaction->DoTransaction();
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "ChangeStyleTransaction::DoTransaction() failed");
    return rv;
  }
  nsresult rv = aHTMLEditor.DoTransactionInternal(transaction);
  if (NS_WARN_IF(aHTMLEditor.Destroyed())) {
    return NS_ERROR_EDITOR_DESTROYED;
  }
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::DoTransactionInternal() failed");
  return rv;
}

// static
nsresult CSSEditUtils::SetCSSPropertyPixelsWithTransaction(
    HTMLEditor& aHTMLEditor, nsStyledElement& aStyledElement, nsAtom& aProperty,
    int32_t aIntValue) {
  nsAutoString s;
  s.AppendInt(aIntValue);
  nsresult rv = SetCSSPropertyWithTransaction(aHTMLEditor, aStyledElement,
                                              aProperty, s + u"px"_ns);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "CSSEditUtils::SetCSSPropertyWithTransaction() failed");
  return rv;
}

// static
nsresult CSSEditUtils::SetCSSPropertyPixelsWithoutTransaction(
    HTMLEditor& aHTMLEditor, nsStyledElement& aStyledElement,
    const nsAtom& aProperty, int32_t aIntValue) {
  nsAutoCString propertyNameString;
  aProperty.ToUTF8String(propertyNameString);

  nsAutoCString s;
  s.AppendInt(aIntValue);
  s.AppendLiteral("px");

  nsresult rv = AutoCSSDeclarationAPIWrapper(aHTMLEditor, aStyledElement)
                    .SetProperty(propertyNameString, s, EmptyCString());
  if (NS_FAILED(rv)) {
    NS_WARNING("AutoCSSDeclarationAPIWrapper::SetProperty() failed");
    return rv;
  }

  return NS_OK;
}

// The lowest level above the transaction; removes the value aValue from the
// list of values specified for the CSS property aProperty, or totally remove
// the declaration if this property accepts only one value

// static
nsresult CSSEditUtils::RemoveCSSPropertyInternal(
    HTMLEditor& aHTMLEditor, nsStyledElement& aStyledElement, nsAtom& aProperty,
    const nsAString& aValue, bool aSuppressTxn) {
  const RefPtr<ChangeStyleTransaction> transaction =
      ChangeStyleTransaction::CreateToRemove(aHTMLEditor, aStyledElement,
                                             aProperty, aValue);
  if (aSuppressTxn) {
    nsresult rv = transaction->DoTransaction();
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "ChangeStyleTransaction::DoTransaction() failed");
    return rv;
  }
  nsresult rv = aHTMLEditor.DoTransactionInternal(transaction);
  if (NS_WARN_IF(aHTMLEditor.Destroyed())) {
    return NS_ERROR_EDITOR_DESTROYED;
  }
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::DoTransactionInternal() failed");
  return rv;
}

// static
nsresult CSSEditUtils::GetSpecifiedProperty(nsIContent& aContent,
                                            nsAtom& aCSSProperty,
                                            nsAString& aValue) {
  nsresult rv =
      GetSpecifiedCSSInlinePropertyBase(aContent, aCSSProperty, aValue);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "CSSEditUtils::GeSpecifiedCSSInlinePropertyBase() failed");
  return rv;
}

// static
nsresult CSSEditUtils::GetComputedProperty(nsIContent& aContent,
                                           nsAtom& aCSSProperty,
                                           nsAString& aValue) {
  nsresult rv =
      GetComputedCSSInlinePropertyBase(aContent, aCSSProperty, aValue);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "CSSEditUtils::GetComputedCSSInlinePropertyBase() failed");
  return rv;
}

// static
nsresult CSSEditUtils::GetComputedCSSInlinePropertyBase(nsIContent& aContent,
                                                        nsAtom& aCSSProperty,
                                                        nsAString& aValue) {
  aValue.Truncate();

  RefPtr<Element> element = aContent.GetAsElementOrParentElement();
  if (NS_WARN_IF(!element)) {
    return NS_ERROR_INVALID_ARG;
  }

  // Get the all the computed css styles attached to the element node
  RefPtr<nsComputedDOMStyle> computedDOMStyle = GetComputedStyle(element);
  if (NS_WARN_IF(!computedDOMStyle)) {
    return NS_ERROR_INVALID_ARG;
  }

  // from these declarations, get the one we want and that one only
  //
  // FIXME(bug 1606994): nsAtomCString copies, we should just keep around the
  // property id.
  //
  // FIXME: Maybe we can avoid copying aValue too, though it's no worse than
  // what we used to do.
  nsAutoCString value;
  computedDOMStyle->GetPropertyValue(nsAtomCString(&aCSSProperty), value);
  CopyUTF8toUTF16(value, aValue);
  return NS_OK;
}

// static
nsresult CSSEditUtils::GetSpecifiedCSSInlinePropertyBase(nsIContent& aContent,
                                                         nsAtom& aCSSProperty,
                                                         nsAString& aValue) {
  aValue.Truncate();

  RefPtr<Element> element = aContent.GetAsElementOrParentElement();
  if (NS_WARN_IF(!element)) {
    return NS_ERROR_INVALID_ARG;
  }

  RefPtr decl = element->GetInlineStyleDeclaration();
  if (!decl) {
    return NS_OK;
  }

  // FIXME: Same comments as above.
  NonCustomCSSPropertyId prop =
      nsCSSProps::LookupProperty(nsAtomCString(&aCSSProperty));
  MOZ_ASSERT(prop != eCSSProperty_UNKNOWN);

  nsAutoCString value;
  Servo_DeclarationBlock_GetPropertyValueByNonCustomId(decl, prop, &value);
  CopyUTF8toUTF16(value, aValue);
  return NS_OK;
}

// static
already_AddRefed<nsComputedDOMStyle> CSSEditUtils::GetComputedStyle(
    Element* aElement) {
  MOZ_ASSERT(aElement);

  Document* document = aElement->GetComposedDoc();
  if (NS_WARN_IF(!document)) {
    return nullptr;
  }

  RefPtr<nsComputedDOMStyle> computedDOMStyle = NS_NewComputedDOMStyle(
      aElement, u""_ns, document, nsComputedDOMStyle::StyleType::All,
      IgnoreErrors());
  return computedDOMStyle.forget();
}

// remove the CSS style "aProperty : aPropertyValue" and possibly remove the
// whole node if it is a span and if its only attribute is _moz_dirty

// static
Result<EditorDOMPoint, nsresult>
CSSEditUtils::RemoveCSSInlineStyleWithTransaction(
    HTMLEditor& aHTMLEditor, nsStyledElement& aStyledElement, nsAtom* aProperty,
    const nsAString& aPropertyValue) {
  // remove the property from the style attribute
  nsresult rv = RemoveCSSPropertyWithTransaction(aHTMLEditor, aStyledElement,
                                                 *aProperty, aPropertyValue);
  if (NS_FAILED(rv)) {
    NS_WARNING("CSSEditUtils::RemoveCSSPropertyWithTransaction() failed");
    return Err(rv);
  }

  if (!aStyledElement.IsHTMLElement(nsGkAtoms::span) ||
      HTMLEditUtils::ElementHasAttribute(aStyledElement)) {
    return EditorDOMPoint();
  }

  Result<EditorDOMPoint, nsresult> unwrapStyledElementResult =
      aHTMLEditor.RemoveContainerWithTransaction(aStyledElement);
  NS_WARNING_ASSERTION(unwrapStyledElementResult.isOk(),
                       "HTMLEditor::RemoveContainerWithTransaction() failed");
  return unwrapStyledElementResult;
}

// Get the default browser background color if we need it for
// GetCSSBackgroundColorState

// static
void CSSEditUtils::GetDefaultBackgroundColor(nsAString& aColor) {
  aColor.AssignLiteral("#ffffff");  // Default to white

  if (MOZ_UNLIKELY(StaticPrefs::editor_use_custom_colors())) {
    DebugOnly<nsresult> rv =
        Preferences::GetString("editor.background_color", aColor);
    // XXX Why don't you validate the pref value?
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "failed to get editor.background_color");
    return;
  }

  if (StaticPrefs::browser_display_document_color_use() != 2) {
    return;
  }

  DebugOnly<nsresult> rv =
      Preferences::GetString("browser.display.background_color", aColor);
  // XXX Why don't you validate the pref value?
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "failed to get browser.display.background_color");
}

// static
void CSSEditUtils::ParseLength(const nsAString& aString, float* aValue,
                               nsAtom** aUnit) {
  if (aString.IsEmpty()) {
    *aValue = 0;
    *aUnit = NS_Atomize(aString).take();
    return;
  }

  nsAString::const_iterator iter;
  aString.BeginReading(iter);

  float a = 10.0f, b = 1.0f, value = 0;
  int8_t sign = 1;
  int32_t i = 0, j = aString.Length();
  char16_t c;
  bool floatingPointFound = false;
  c = *iter;
  if (char16_t('-') == c) {
    sign = -1;
    iter++;
    i++;
  } else if (char16_t('+') == c) {
    iter++;
    i++;
  }
  while (i < j) {
    c = *iter;
    if ((char16_t('0') == c) || (char16_t('1') == c) || (char16_t('2') == c) ||
        (char16_t('3') == c) || (char16_t('4') == c) || (char16_t('5') == c) ||
        (char16_t('6') == c) || (char16_t('7') == c) || (char16_t('8') == c) ||
        (char16_t('9') == c)) {
      value = (value * a) + (b * (c - char16_t('0')));
      b = b / 10 * a;
    } else if (!floatingPointFound && (char16_t('.') == c)) {
      floatingPointFound = true;
      a = 1.0f;
      b = 0.1f;
    } else
      break;
    iter++;
    i++;
  }
  *aValue = value * sign;
  *aUnit = NS_Atomize(StringTail(aString, j - i)).take();
}

// static
nsStaticAtom* CSSEditUtils::GetCSSPropertyAtom(
    nsCSSEditableProperty aProperty) {
  switch (aProperty) {
    case eCSSEditableProperty_background_color:
      return nsGkAtoms::background_color;
    case eCSSEditableProperty_background_image:
      return nsGkAtoms::background_image;
    case eCSSEditableProperty_border:
      return nsGkAtoms::border;
    case eCSSEditableProperty_caption_side:
      return nsGkAtoms::caption_side;
    case eCSSEditableProperty_color:
      return nsGkAtoms::color;
    case eCSSEditableProperty_float:
      return nsGkAtoms::_float;
    case eCSSEditableProperty_font_family:
      return nsGkAtoms::font_family;
    case eCSSEditableProperty_font_size:
      return nsGkAtoms::font_size;
    case eCSSEditableProperty_font_style:
      return nsGkAtoms::font_style;
    case eCSSEditableProperty_font_weight:
      return nsGkAtoms::font_weight;
    case eCSSEditableProperty_height:
      return nsGkAtoms::height;
    case eCSSEditableProperty_list_style_type:
      return nsGkAtoms::list_style_type;
    case eCSSEditableProperty_margin_left:
      return nsGkAtoms::marginLeft;
    case eCSSEditableProperty_margin_right:
      return nsGkAtoms::marginRight;
    case eCSSEditableProperty_text_align:
      return nsGkAtoms::textAlign;
    case eCSSEditableProperty_text_decoration:
      return nsGkAtoms::text_decoration;
    case eCSSEditableProperty_vertical_align:
      return nsGkAtoms::vertical_align;
    case eCSSEditableProperty_whitespace:
      return nsGkAtoms::white_space;
    case eCSSEditableProperty_width:
      return nsGkAtoms::width;
    case eCSSEditableProperty_NONE:
      // intentionally empty
      return nullptr;
  }
  MOZ_ASSERT_UNREACHABLE("Got unknown property");
  return nullptr;
}

// static
void CSSEditUtils::GetCSSDeclarations(
    const CSSEquivTable* aEquivTable, const nsAString* aValue,
    HandlingFor aHandlingFor, nsTArray<CSSDeclaration>& aOutCSSDeclarations) {
  // clear arrays
  aOutCSSDeclarations.Clear();

  // if we have an input value, let's use it
  nsAutoString value, lowerCasedValue;
  if (aValue) {
    value.Assign(*aValue);
    lowerCasedValue.Assign(*aValue);
    ToLowerCase(lowerCasedValue);
  }

  for (size_t index = 0;; index++) {
    const nsCSSEditableProperty cssProperty = aEquivTable[index].cssProperty;
    if (!cssProperty) {
      break;
    }
    if (aHandlingFor == HandlingFor::SettingStyle ||
        aEquivTable[index].gettable) {
      nsAutoString cssValue, cssPropertyString;
      // find the equivalent css value for the index-th property in
      // the equivalence table
      (*aEquivTable[index].processValueFunctor)(
          (aHandlingFor == HandlingFor::SettingStyle ||
           aEquivTable[index].caseSensitiveValue)
              ? &value
              : &lowerCasedValue,
          cssValue, aEquivTable[index].defaultValue,
          aEquivTable[index].prependValue, aEquivTable[index].appendValue);
      nsStaticAtom* const propertyAtom = GetCSSPropertyAtom(cssProperty);
      if (MOZ_LIKELY(propertyAtom)) {
        aOutCSSDeclarations.AppendElement(
            CSSDeclaration{*propertyAtom, cssValue});
      }
    }
  }
}

// static
void CSSEditUtils::GetCSSDeclarations(
    Element& aElement, const EditorElementStyle& aStyle,
    const nsAString* aValue, HandlingFor aHandlingFor,
    nsTArray<CSSDeclaration>& aOutCSSDeclarations) {
  nsStaticAtom* const htmlProperty =
      aStyle.IsInlineStyle() ? aStyle.AsInlineStyle().mHTMLProperty : nullptr;
  const RefPtr<nsAtom> attributeOrStyle =
      aStyle.IsInlineStyle() ? aStyle.AsInlineStyle().mAttribute
                             : aStyle.Style();

  const auto* equivTable = [&]() -> const CSSEditUtils::CSSEquivTable* {
    if (nsGkAtoms::b == htmlProperty) {
      return boldEquivTable;
    }
    if (nsGkAtoms::i == htmlProperty) {
      return italicEquivTable;
    }
    if (nsGkAtoms::u == htmlProperty) {
      return underlineEquivTable;
    }
    if (nsGkAtoms::strike == htmlProperty) {
      return strikeEquivTable;
    }
    if (nsGkAtoms::tt == htmlProperty) {
      return ttEquivTable;
    }
    if (!attributeOrStyle) {
      return nullptr;
    }
    if (nsGkAtoms::font == htmlProperty) {
      if (attributeOrStyle == nsGkAtoms::color) {
        return fontColorEquivTable;
      }
      if (attributeOrStyle == nsGkAtoms::face) {
        return fontFaceEquivTable;
      }
      if (attributeOrStyle == nsGkAtoms::size) {
        return fontSizeEquivTable;
      }
      MOZ_ASSERT(attributeOrStyle == nsGkAtoms::bgcolor);
    }
    if (attributeOrStyle == nsGkAtoms::bgcolor) {
      return bgcolorEquivTable;
    }
    if (attributeOrStyle == nsGkAtoms::background) {
      return backgroundImageEquivTable;
    }
    if (attributeOrStyle == nsGkAtoms::text) {
      return textColorEquivTable;
    }
    if (attributeOrStyle == nsGkAtoms::border) {
      return borderEquivTable;
    }
    if (attributeOrStyle == nsGkAtoms::align) {
      if (aElement.IsHTMLElement(nsGkAtoms::table)) {
        return tableAlignEquivTable;
      }
      if (aElement.IsHTMLElement(nsGkAtoms::hr)) {
        return hrAlignEquivTable;
      }
      if (aElement.IsAnyOfHTMLElements(nsGkAtoms::legend, nsGkAtoms::caption)) {
        return captionAlignEquivTable;
      }
      return textAlignEquivTable;
    }
    if (attributeOrStyle == nsGkAtoms::valign) {
      return verticalAlignEquivTable;
    }
    if (attributeOrStyle == nsGkAtoms::nowrap) {
      return nowrapEquivTable;
    }
    if (attributeOrStyle == nsGkAtoms::width) {
      return widthEquivTable;
    }
    if (attributeOrStyle == nsGkAtoms::height ||
        (aElement.IsHTMLElement(nsGkAtoms::hr) &&
         attributeOrStyle == nsGkAtoms::size)) {
      return heightEquivTable;
    }
    if (attributeOrStyle == nsGkAtoms::type &&
        aElement.IsAnyOfHTMLElements(nsGkAtoms::ol, nsGkAtoms::ul,
                                     nsGkAtoms::li)) {
      return listStyleTypeEquivTable;
    }
    return nullptr;
  }();
  if (equivTable) {
    GetCSSDeclarations(equivTable, aValue, aHandlingFor, aOutCSSDeclarations);
  }
}

// Add to aNode the CSS inline style equivalent to HTMLProperty/aAttribute/
// aValue for the node, and return in aCount the number of CSS properties set
// by the call.  The Element version returns aCount instead.
Result<size_t, nsresult> CSSEditUtils::SetCSSEquivalentToStyle(
    WithTransaction aWithTransaction, HTMLEditor& aHTMLEditor,
    nsStyledElement& aStyledElement, const EditorElementStyle& aStyleToSet,
    const nsAString* aValue) {
  MOZ_DIAGNOSTIC_ASSERT(aStyleToSet.IsCSSSettable(aStyledElement));

  // we can apply the styles only if the node is an element and if we have
  // an equivalence for the requested HTML style in this implementation

  // Find the CSS equivalence to the HTML style
  AutoTArray<CSSDeclaration, 4> cssDeclarations;
  GetCSSDeclarations(aStyledElement, aStyleToSet, aValue,
                     HandlingFor::SettingStyle, cssDeclarations);

  // set the individual CSS inline styles
  for (const CSSDeclaration& cssDeclaration : cssDeclarations) {
    nsresult rv = SetCSSPropertyInternal(
        aHTMLEditor, aStyledElement, MOZ_KnownLive(cssDeclaration.mProperty),
        cssDeclaration.mValue, aWithTransaction == WithTransaction::No);
    if (NS_FAILED(rv)) {
      NS_WARNING("CSSEditUtils::SetCSSPropertyInternal() failed");
      return Err(rv);
    }
  }
  return cssDeclarations.Length();
}

// static
nsresult CSSEditUtils::RemoveCSSEquivalentToStyle(
    WithTransaction aWithTransaction, HTMLEditor& aHTMLEditor,
    nsStyledElement& aStyledElement, const EditorElementStyle& aStyleToRemove,
    const nsAString* aValue) {
  MOZ_DIAGNOSTIC_ASSERT(aStyleToRemove.IsCSSRemovable(aStyledElement));

  // we can apply the styles only if the node is an element and if we have
  // an equivalence for the requested HTML style in this implementation

  // Find the CSS equivalence to the HTML style
  AutoTArray<CSSDeclaration, 4> cssDeclarations;
  GetCSSDeclarations(aStyledElement, aStyleToRemove, aValue,
                     HandlingFor::RemovingStyle, cssDeclarations);

  // remove the individual CSS inline styles
  for (const CSSDeclaration& cssDeclaration : cssDeclarations) {
    nsresult rv = RemoveCSSPropertyInternal(
        aHTMLEditor, aStyledElement, MOZ_KnownLive(cssDeclaration.mProperty),
        cssDeclaration.mValue, aWithTransaction == WithTransaction::No);
    if (NS_FAILED(rv)) {
      NS_WARNING("CSSEditUtils::RemoveCSSPropertyWithoutTransaction() failed");
      return rv;
    }
  }
  return NS_OK;
}

// static
nsresult CSSEditUtils::GetComputedCSSEquivalentTo(
    Element& aElement, const EditorElementStyle& aStyle, nsAString& aOutValue) {
  return GetCSSEquivalentTo(aElement, aStyle, aOutValue, StyleType::Computed);
}

// static
nsresult CSSEditUtils::GetCSSEquivalentTo(Element& aElement,
                                          const EditorElementStyle& aStyle,
                                          nsAString& aOutValue,
                                          StyleType aStyleType) {
  MOZ_ASSERT_IF(aStyle.IsInlineStyle(),
                !aStyle.AsInlineStyle().IsStyleToClearAllInlineStyles());
  MOZ_DIAGNOSTIC_ASSERT(aStyle.IsCSSSettable(aElement) ||
                        aStyle.IsCSSRemovable(aElement));

  aOutValue.Truncate();
  AutoTArray<CSSDeclaration, 4> cssDeclarations;
  GetCSSDeclarations(aElement, aStyle, nullptr, HandlingFor::GettingStyle,
                     cssDeclarations);
  nsAutoString valueString;
  for (const CSSDeclaration& cssDeclaration : cssDeclarations) {
    valueString.Truncate();
    // retrieve the specified/computed value of the property
    if (aStyleType == StyleType::Computed) {
      nsresult rv = GetComputedCSSInlinePropertyBase(
          aElement, MOZ_KnownLive(cssDeclaration.mProperty), valueString);
      if (NS_FAILED(rv)) {
        NS_WARNING("CSSEditUtils::GetComputedCSSInlinePropertyBase() failed");
        return rv;
      }
    } else {
      nsresult rv = GetSpecifiedCSSInlinePropertyBase(
          aElement, cssDeclaration.mProperty, valueString);
      if (NS_FAILED(rv)) {
        NS_WARNING("CSSEditUtils::GetSpecifiedCSSInlinePropertyBase() failed");
        return rv;
      }
    }
    // append the value to aOutValue (possibly with a leading white-space)
    if (!aOutValue.IsEmpty()) {
      aOutValue.Append(HTMLEditUtils::kSpace);
    }
    aOutValue.Append(valueString);
  }
  return NS_OK;
}

// Does the node aContent (or its parent, if it's not an element node) have a
// CSS style equivalent to the HTML style
// aHTMLProperty/aAttribute/valueString? The value of aStyleType controls
// the styles we retrieve: specified or computed. The return value aIsSet is
// true if the CSS styles are set.
//
// The nsIContent variant returns aIsSet instead of using an out parameter, and
// does not modify aValue.

// static
Result<bool, nsresult> CSSEditUtils::IsComputedCSSEquivalentTo(
    const HTMLEditor& aHTMLEditor, nsIContent& aContent,
    const EditorInlineStyle& aStyle, nsAString& aInOutValue) {
  return IsCSSEquivalentTo(aHTMLEditor, aContent, aStyle, aInOutValue,
                           StyleType::Computed);
}

// static
Result<bool, nsresult> CSSEditUtils::IsSpecifiedCSSEquivalentTo(
    const HTMLEditor& aHTMLEditor, nsIContent& aContent,
    const EditorInlineStyle& aStyle, nsAString& aInOutValue) {
  return IsCSSEquivalentTo(aHTMLEditor, aContent, aStyle, aInOutValue,
                           StyleType::Specified);
}

// static
Result<bool, nsresult> CSSEditUtils::IsCSSEquivalentTo(
    const HTMLEditor& aHTMLEditor, nsIContent& aContent,
    const EditorInlineStyle& aStyle, nsAString& aInOutValue,
    StyleType aStyleType) {
  MOZ_ASSERT(!aStyle.IsStyleToClearAllInlineStyles());

  nsAutoString htmlValueString(aInOutValue);
  bool isSet = false;
  // FYI: Cannot use InclusiveAncestorsOfType here because
  //      GetCSSEquivalentTo() may flush pending notifications.
  for (RefPtr<Element> element = aContent.GetAsElementOrParentElement();
       element; element = element->GetParentElement()) {
    nsCOMPtr<nsINode> parentNode = element->GetParentNode();
    aInOutValue.Assign(htmlValueString);
    // get the value of the CSS equivalent styles
    nsresult rv = GetCSSEquivalentTo(*element, aStyle, aInOutValue, aStyleType);
    if (NS_WARN_IF(aHTMLEditor.Destroyed())) {
      return Err(NS_ERROR_EDITOR_DESTROYED);
    }
    if (NS_FAILED(rv)) {
      NS_WARNING(
          "CSSEditUtils::GetCSSEquivalentToHTMLInlineStyleSetInternal() "
          "failed");
      return Err(rv);
    }
    if (NS_WARN_IF(parentNode != element->GetParentNode())) {
      return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
    }

    // early way out if we can
    if (aInOutValue.IsEmpty()) {
      return isSet;
    }

    if (nsGkAtoms::b == aStyle.mHTMLProperty) {
      if (aInOutValue.EqualsLiteral("bold")) {
        isSet = true;
      } else if (aInOutValue.EqualsLiteral("normal")) {
        isSet = false;
      } else if (aInOutValue.EqualsLiteral("bolder")) {
        isSet = true;
        aInOutValue.AssignLiteral("bold");
      } else {
        int32_t weight = 0;
        nsresult rvIgnored;
        nsAutoString value(aInOutValue);
        weight = value.ToInteger(&rvIgnored);
        NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                             "nsAString::ToInteger() failed, but ignored");
        if (400 < weight) {
          isSet = true;
          aInOutValue.AssignLiteral(u"bold");
        } else {
          isSet = false;
          aInOutValue.AssignLiteral(u"normal");
        }
      }
    } else if (nsGkAtoms::i == aStyle.mHTMLProperty) {
      if (aInOutValue.EqualsLiteral(u"italic") ||
          aInOutValue.EqualsLiteral(u"oblique")) {
        isSet = true;
      }
    } else if (nsGkAtoms::u == aStyle.mHTMLProperty) {
      isSet = ChangeStyleTransaction::ValueIncludes(
          NS_ConvertUTF16toUTF8(aInOutValue), "underline"_ns);
    } else if (nsGkAtoms::strike == aStyle.mHTMLProperty) {
      isSet = ChangeStyleTransaction::ValueIncludes(
          NS_ConvertUTF16toUTF8(aInOutValue), "line-through"_ns);
    } else if ((nsGkAtoms::font == aStyle.mHTMLProperty &&
                aStyle.mAttribute == nsGkAtoms::color) ||
               aStyle.mAttribute == nsGkAtoms::bgcolor) {
      isSet = htmlValueString.IsEmpty() ||
              HTMLEditUtils::IsSameCSSColorValue(htmlValueString, aInOutValue);
    } else if (nsGkAtoms::tt == aStyle.mHTMLProperty) {
      isSet = StringBeginsWith(aInOutValue, u"monospace"_ns);
    } else if (nsGkAtoms::font == aStyle.mHTMLProperty &&
               aStyle.mAttribute == nsGkAtoms::face) {
      if (!htmlValueString.IsEmpty()) {
        const char16_t commaSpace[] = {char16_t(','), HTMLEditUtils::kSpace, 0};
        const char16_t comma[] = {char16_t(','), 0};
        htmlValueString.ReplaceSubstring(commaSpace, comma);
        nsAutoString valueStringNorm(aInOutValue);
        valueStringNorm.ReplaceSubstring(commaSpace, comma);
        isSet = htmlValueString.Equals(valueStringNorm,
                                       nsCaseInsensitiveStringComparator);
      } else {
        isSet = true;
      }
      return isSet;
    } else if (aStyle.IsStyleOfFontSize()) {
      if (htmlValueString.IsEmpty()) {
        return true;
      }
      switch (nsContentUtils::ParseLegacyFontSize(htmlValueString)) {
        case 1:
          return aInOutValue.EqualsLiteral("x-small");
        case 2:
          return aInOutValue.EqualsLiteral("small");
        case 3:
          return aInOutValue.EqualsLiteral("medium");
        case 4:
          return aInOutValue.EqualsLiteral("large");
        case 5:
          return aInOutValue.EqualsLiteral("x-large");
        case 6:
          return aInOutValue.EqualsLiteral("xx-large");
        case 7:
          return aInOutValue.EqualsLiteral("xxx-large");
      }
      return false;
    } else if (aStyle.mAttribute == nsGkAtoms::align) {
      isSet = true;
    } else {
      return false;
    }

    if (!htmlValueString.IsEmpty() &&
        htmlValueString.Equals(aInOutValue,
                               nsCaseInsensitiveStringComparator)) {
      isSet = true;
    }

    if (htmlValueString.EqualsLiteral(u"-moz-editor-invert-value")) {
      isSet = !isSet;
    }

    if (isSet) {
      return true;
    }

    if (!aStyle.IsStyleOfTextDecoration(
            EditorInlineStyle::IgnoreSElement::Yes)) {
      return isSet;
    }

    // Unfortunately, the value of the text-decoration property is not
    // inherited. that means that we have to look at ancestors of node to see
    // if they are underlined.
  }
  return isSet;
}

// static
Result<bool, nsresult> CSSEditUtils::HaveComputedCSSEquivalentStyles(
    const HTMLEditor& aHTMLEditor, nsIContent& aContent,
    const EditorInlineStyle& aStyle) {
  return HaveCSSEquivalentStyles(aHTMLEditor, aContent, aStyle,
                                 StyleType::Computed);
}

// static
Result<bool, nsresult> CSSEditUtils::HaveSpecifiedCSSEquivalentStyles(
    const HTMLEditor& aHTMLEditor, nsIContent& aContent,
    const EditorInlineStyle& aStyle) {
  return HaveCSSEquivalentStyles(aHTMLEditor, aContent, aStyle,
                                 StyleType::Specified);
}

// static
Result<bool, nsresult> CSSEditUtils::HaveCSSEquivalentStyles(
    const HTMLEditor& aHTMLEditor, nsIContent& aContent,
    const EditorInlineStyle& aStyle, StyleType aStyleType) {
  MOZ_ASSERT(!aStyle.IsStyleToClearAllInlineStyles());

  // FYI: Unfortunately, we cannot use InclusiveAncestorsOfType here
  //      because GetCSSEquivalentTo() may flush pending notifications.
  nsAutoString valueString;
  for (RefPtr<Element> element = aContent.GetAsElementOrParentElement();
       element; element = element->GetParentElement()) {
    nsCOMPtr<nsINode> parentNode = element->GetParentNode();
    // get the value of the CSS equivalent styles
    nsresult rv = GetCSSEquivalentTo(*element, aStyle, valueString, aStyleType);
    if (NS_WARN_IF(aHTMLEditor.Destroyed())) {
      return Err(NS_ERROR_EDITOR_DESTROYED);
    }
    if (NS_FAILED(rv)) {
      NS_WARNING(
          "CSSEditUtils::GetCSSEquivalentToHTMLInlineStyleSetInternal() "
          "failed");
      return Err(rv);
    }
    if (NS_WARN_IF(parentNode != element->GetParentNode())) {
      return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
    }

    if (!valueString.IsEmpty()) {
      return true;
    }

    if (!aStyle.IsStyleOfTextDecoration(
            EditorInlineStyle::IgnoreSElement::Yes)) {
      return false;
    }

    // Unfortunately, the value of the text-decoration property is not
    // inherited.
    // that means that we have to look at ancestors of node to see if they
    // are underlined.
  }

  return false;
}

// ElementsSameStyle compares two elements and checks if they have the same
// specified CSS declarations in the STYLE attribute
// The answer is always negative if at least one of them carries an ID or a
// class

// static
bool CSSEditUtils::DoStyledElementsHaveSameStyle(
    nsStyledElement& aStyledElement, nsStyledElement& aOtherStyledElement) {
  if (aStyledElement.HasAttr(nsGkAtoms::id) ||
      aOtherStyledElement.HasAttr(nsGkAtoms::id)) {
    // at least one of the spans carries an ID ; suspect a CSS rule applies to
    // it and refuse to merge the nodes
    return false;
  }

  nsAutoString firstClass, otherClass;
  bool isElementClassSet =
      aStyledElement.GetAttr(nsGkAtoms::_class, firstClass);
  bool isOtherElementClassSet = aOtherStyledElement.GetAttr(
      kNameSpaceID_None, nsGkAtoms::_class, otherClass);
  if (isElementClassSet && isOtherElementClassSet) {
    // both spans carry a class, let's compare them
    if (!firstClass.Equals(otherClass)) {
      // WARNING : technically, the comparison just above is questionable :
      // from a pure HTML/CSS point of view class="a b" is NOT the same than
      // class="b a" because a CSS rule could test the exact value of the class
      // attribute to be "a b" for instance ; from a user's point of view, a
      // wysiwyg editor should probably NOT make any difference. CSS people
      // need to discuss this issue before any modification.
      return false;
    }
  } else if (isElementClassSet || isOtherElementClassSet) {
    // one span only carries a class, early way out
    return false;
  }

  // XXX If `GetPropertyValue()` won't run script, we can stop using
  //     nsCOMPtr here.
  nsCOMPtr<nsICSSDeclaration> firstCSSDecl = aStyledElement.Style();
  if (!firstCSSDecl) {
    NS_WARNING("nsStyledElement::Style() failed");
    return false;
  }
  nsCOMPtr<nsICSSDeclaration> otherCSSDecl = aOtherStyledElement.Style();
  if (!otherCSSDecl) {
    NS_WARNING("nsStyledElement::Style() failed");
    return false;
  }

  const uint32_t firstLength = firstCSSDecl->Length();
  const uint32_t otherLength = otherCSSDecl->Length();
  if (firstLength != otherLength) {
    // early way out if we can
    return false;
  }

  if (!firstLength) {
    // no inline style !
    return true;
  }

  for (uint32_t i = 0; i < firstLength; i++) {
    nsAutoCString firstValue, otherValue;
    nsAutoCString propertyNameString;
    firstCSSDecl->Item(i, propertyNameString);
    firstCSSDecl->GetPropertyValue(propertyNameString, firstValue);
    otherCSSDecl->GetPropertyValue(propertyNameString, otherValue);
    // FIXME: We need to handle all properties whose values are color.
    // However, it's too expensive if we keep using string property names.
    if (propertyNameString.EqualsLiteral("color") ||
        propertyNameString.EqualsLiteral("background-color")) {
      if (!HTMLEditUtils::IsSameCSSColorValue(firstValue, otherValue)) {
        return false;
      }
    } else if (!firstValue.Equals(otherValue)) {
      return false;
    }
  }
  for (uint32_t i = 0; i < otherLength; i++) {
    nsAutoCString firstValue, otherValue;
    nsAutoCString propertyNameString;
    otherCSSDecl->Item(i, propertyNameString);
    otherCSSDecl->GetPropertyValue(propertyNameString, otherValue);
    firstCSSDecl->GetPropertyValue(propertyNameString, firstValue);
    // FIXME: We need to handle all properties whose values are color.
    // However, it's too expensive if we keep using string property names.
    if (propertyNameString.EqualsLiteral("color") ||
        propertyNameString.EqualsLiteral("background-color")) {
      if (!HTMLEditUtils::IsSameCSSColorValue(firstValue, otherValue)) {
        return false;
      }
    } else if (!firstValue.Equals(otherValue)) {
      return false;
    }
  }

  return true;
}

}  // namespace mozilla
