/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef mozilla_dom_InspectorUtils_h
#define mozilla_dom_InspectorUtils_h

#include "Units.h"
#include "mozilla/RefPtr.h"
#include "mozilla/dom/InspectorUtilsBindingFwd.h"
#include "nsTArray.h"

class nsAtom;
class nsINode;
namespace mozilla::dom {
class Range;
}  // namespace mozilla::dom

namespace mozilla {
class ErrorResult;
class StyleSheet;
namespace css {
class Rule;
}  // namespace css
namespace dom {
class BrowsingContext;
enum class InspectorPropertyType : uint8_t;
class CharacterData;
class Document;
class Element;
class GlobalObject;
class InspectorFontFace;
class NodeList;
class OwningCSSRuleOrInspectorDeclaration;
}  // namespace dom
}  // namespace mozilla

namespace mozilla::dom {
/**
 * A collection of utility methods for use by devtools.
 */
class InspectorUtils {
 public:
  static void GetAllStyleSheets(GlobalObject& aGlobal, Document& aDocument,
                                bool aDocumentOnly,
                                nsTArray<RefPtr<StyleSheet>>& aResult);
  static void GetMatchingCSSRules(
      GlobalObject& aGlobal, Element& aElement, const nsAString& aPseudo,
      bool aIncludeVisitedStyle, bool aWithStartingStyle,
      nsTArray<OwningCSSRuleOrInspectorDeclaration>& aResult);

  /**
   * Get the line number of a rule.
   *
   * @param aRule The rule.
   * @return The rule's line number.  Line numbers are 1-based.
   */
  static uint32_t GetRuleLine(GlobalObject& aGlobal, css::Rule& aRule);

  /**
   * Get the column number of a rule.
   *
   * @param aRule The rule.
   * @return The rule's column number.  Column numbers are 1-based.
   */
  static uint32_t GetRuleColumn(GlobalObject& aGlobal, css::Rule& aRule);

  /**
   * Like getRuleLine, but if the rule is in a <style> element,
   * returns a line number relative to the start of the element.
   *
   * @param aRule the rule to examine
   * @return the line number of the rule, possibly relative to the
   *         <style> element
   */
  static uint32_t GetRelativeRuleLine(GlobalObject& aGlobal, css::Rule& aRule);

  static void GetRuleIndex(GlobalObject& aGlobal, css::Rule& aRule,
                           nsTArray<uint32_t>& aResult);

  static bool HasRulesModifiedByCSSOM(GlobalObject& aGlobal,
                                      StyleSheet& aSheet);

  static void GetStyleSheetRuleCountAndAtRules(
      GlobalObject& aGlobal, StyleSheet& aSheet,
      InspectorStyleSheetRuleCountAndAtRulesResult& aResult);

  // Utilities for working with CSS properties
  //
  // Returns true if the string names a property that is inherited by default.
  static bool IsInheritedProperty(GlobalObject& aGlobal, Document& aDocument,
                                  const nsACString& aPropertyName);

  // Get a list of all our supported property names.  Optionally
  // property aliases included.
  static void GetCSSPropertyNames(GlobalObject& aGlobal,
                                  const PropertyNamesOptions& aOptions,
                                  nsTArray<nsString>& aResult);

  // Get a list of all properties controlled by preference, as well as
  // their corresponding preference names.
  static void GetCSSPropertyPrefs(GlobalObject& aGlobal,
                                  nsTArray<PropertyPref>& aResult);

  // Get a list of all valid keywords and colors for aProperty.
  static void GetCSSValuesForProperty(GlobalObject& aGlobal,
                                      const nsACString& aPropertyName,
                                      nsTArray<nsString>& aResult,
                                      ErrorResult& aRv);

  // Get a list of all the CSS wide keywords.
  static void GetCSSWideKeywords(GlobalObject& aGlobal,
                                 nsTArray<nsString>& aResult);

  // Utilities for working with CSS colors
  static void RgbToColorName(GlobalObject& aGlobal, uint8_t aR, uint8_t aG,
                             uint8_t aB, nsACString& aResult);

  static void RgbToNearestColorName(GlobalObject&, float aR, float aG, float aB,
                                    InspectorNearestColor& aResult);

  static void RgbToHsv(GlobalObject&, float aR, float aG, float aB,
                       nsTArray<float>& aResult);

  static void HsvToRgb(GlobalObject&, float aH, float aS, float aV,
                       nsTArray<float>& aResult);

  static float RelativeLuminance(GlobalObject&, float aR, float aG, float aB);

  // Convert a given CSS color string to rgba. Returns null on failure or an
  // InspectorRGBATuple on success.
  //
  // NOTE: Converting a color to RGBA may be lossy when converting from some
  // formats e.g. CMYK.
  static void ColorToRGBA(GlobalObject&, const nsACString& aColorString,
                          Nullable<InspectorRGBATuple>& aResult);

  // Convert a given CSS color string to another color space.
  static void ColorTo(GlobalObject&, const nsACString& aFromColor,
                      const nsACString& aToColorSpace,
                      Nullable<InspectorColorToResult>& aResult);

  // Check whether a given color is a valid CSS color.
  static bool IsValidCSSColor(GlobalObject& aGlobal,
                              const nsACString& aColorString);

  // Check whether a given string is a valid CSS <image> value.
  static bool IsValidCSSImage(GlobalObject& aGlobal,
                              const nsACString& aImageString);

  // Utilities for obtaining information about a CSS property.

  // Get a list of the longhands corresponding to the given CSS property.  If
  // the property is a longhand already, just returns the property itself.
  // Throws on unsupported property names.
  static void GetSubpropertiesForCSSProperty(GlobalObject& aGlobal,
                                             const nsACString& aProperty,
                                             nsTArray<nsString>& aResult,
                                             ErrorResult& aRv);

  // Check whether a given CSS property is a shorthand.  Throws on unsupported
  // property names.
  static bool CssPropertyIsShorthand(GlobalObject& aGlobal,
                                     const nsACString& aProperty,
                                     ErrorResult& aRv);

  // Check whether values of the given type are valid values for the property.
  // For shorthands, checks whether there's a corresponding longhand property
  // that accepts values of this type.  Throws on unsupported properties or
  // unknown types.
  static bool CssPropertySupportsType(GlobalObject& aGlobal,
                                      const nsACString& aProperty,
                                      InspectorPropertyType, ErrorResult& aRv);

  static bool Supports(GlobalObject&, const nsACString& aDeclaration,
                       const SupportsOptions&);

  static bool IsIgnorableWhitespace(GlobalObject& aGlobalObject,
                                    CharacterData& aDataNode) {
    return IsIgnorableWhitespace(aDataNode);
  }
  static bool IsIgnorableWhitespace(CharacterData& aDataNode);

  // Returns the "parent" of a node.  The parent of a document node is the
  // frame/iframe containing that document.  aShowingAnonymousContent says
  // whether we are showing anonymous content.
  static nsINode* GetParentForNode(nsINode& aNode,
                                   bool aShowingAnonymousContent);
  static nsINode* GetParentForNode(GlobalObject& aGlobalObject, nsINode& aNode,
                                   bool aShowingAnonymousContent) {
    return GetParentForNode(aNode, aShowingAnonymousContent);
  }

  static void GetChildrenForNode(GlobalObject&, nsINode& aNode,
                                 bool aShowingAnonymousContent,
                                 bool aIncludeAssignedNodes,
                                 nsTArray<RefPtr<nsINode>>& aResult) {
    return GetChildrenForNode(aNode, aShowingAnonymousContent,
                              aIncludeAssignedNodes,
                              /* aIncludeSubdocuments = */ true, aResult);
  }
  static void GetChildrenForNode(nsINode& aNode, bool aShowingAnonymousContent,
                                 bool aIncludeAssignedNodes,
                                 bool aIncludeSubdocuments,
                                 nsTArray<RefPtr<nsINode>>& aResult);

  /**
   * Setting and removing content state on an element. Both these functions
   * call EventStateManager::SetContentState internally; the difference is
   * that for the remove case we simply pass in nullptr for the element.
   * Use them accordingly.
   *
   * When removing the active state, you may optionally also clear the active
   * document as well by setting aClearActiveDocument
   *
   * @return Returns true if the state was set successfully. See more details
   * in EventStateManager.h SetContentState.
   */
  static bool SetContentState(GlobalObject& aGlobal, Element& aElement,
                              uint64_t aState, ErrorResult& aRv);
  static bool RemoveContentState(GlobalObject& aGlobal, Element& aElement,
                                 uint64_t aState, bool aClearActiveDocument,
                                 ErrorResult& aRv);
  static uint64_t GetContentState(GlobalObject& aGlobal, Element& aElement);

  static void GetUsedFontFaces(GlobalObject& aGlobal, dom::Range& aRange,
                               uint32_t aMaxRanges,  // max number of ranges to
                                                     // record for each face
                               bool aSkipCollapsedWhitespace,
                               nsTArray<UniquePtr<InspectorFontFace>>& aResult,
                               ErrorResult& aRv);

  /**
   * Get the names of all the supported pseudo-elements.
   * Pseudo-elements which are only accepted in UA style sheets are
   * not included.
   */
  static void GetCSSPseudoElementNames(GlobalObject&,
                                       nsTArray<nsString>& aResult);

  // pseudo-class style locking methods. aPseudoClass must be a valid
  // pseudo-class selector string, e.g. ":hover". ":any-link" and
  // non-event-state pseudo-classes are ignored. aEnabled sets whether the
  // psuedo-class should be locked to on or off.
  static void AddPseudoClassLock(GlobalObject&, Element&,
                                 const nsAString& aPseudoClass, bool aEnabled);
  static void RemovePseudoClassLock(GlobalObject&, Element&,
                                    const nsAString& aPseudoClass);
  static bool HasPseudoClassLock(GlobalObject&, Element&,
                                 const nsAString& aPseudoClass);
  static void ClearPseudoClassLocks(GlobalObject&, Element&);

  static bool IsElementThemed(GlobalObject&, Element&);

  static bool IsUsedColorSchemeDark(GlobalObject&, Element&);

  static Element* ContainingBlockOf(GlobalObject&, Element&);

  static bool IsBlockContainer(GlobalObject&, Element&);

  static void GetBlockLineCounts(GlobalObject&, Element&,
                                 Nullable<nsTArray<uint32_t>>& aResult);

  MOZ_CAN_RUN_SCRIPT
  static already_AddRefed<NodeList> GetOverflowingChildrenOfElement(
      GlobalObject& aGlobal, Element& element);

  /**
   * Parse CSS and update the style sheet in place.
   *
   * @param DOMCSSStyleSheet aSheet
   * @param UTF8String aInput
   *        The new source string for the style sheet.
   */
  static void ParseStyleSheet(GlobalObject& aGlobal, StyleSheet& aSheet,
                              const nsACString& aInput, ErrorResult& aRv);

  /**
   * Check if the provided name can be custom element name.
   */
  static bool IsCustomElementName(GlobalObject&, const nsAString& aName,
                                  const nsAString& aNamespaceURI);

  /**
   * Get the names of registered Highlights
   */
  static void GetRegisteredCssHighlights(GlobalObject& aGlobal,
                                         Document& aDocument, bool aActiveOnly,
                                         nsTArray<nsString>& aResult);
  /**
   * Get registered CSS properties (via CSS.registerProperty or @property)
   */
  static void GetCSSRegisteredProperties(
      GlobalObject& aGlobal, Document& aDocument,
      nsTArray<InspectorCSSPropertyDefinition>& aResult);

  /**
   * Get a single registered CSS property
   */
  static void GetCSSRegisteredProperty(
      GlobalObject& aGlobal, Document& aDocument, const nsACString& aName,
      Nullable<InspectorCSSPropertyDefinition>& aResult);

  /**
   * Returns whether or not a CSS property value is valid for the passed syntax
   */
  static bool ValueMatchesSyntax(GlobalObject&, Document& aDocument,
                                 const nsACString& aProperty,
                                 const nsACString& aValue,
                                 const nsACString& aSyntax);

  /**
   * Get the rule body text within aInitialText
   */
  static void GetRuleBodyText(GlobalObject&, const nsACString& aInitialText,
                              nsACString& aBodyText);

  /**
   * Replace the rule body text in aStyleSheetText at passed line and column
   */
  static void ReplaceBlockRuleBodyTextInStylesheet(
      GlobalObject&, const nsACString& aStyleSheetText, uint32_t aLine,
      uint32_t aColumn, const nsACString& aNewBodyText,
      nsACString& aNewStyleSheetText);

  static void SetVerticalClipping(GlobalObject&, BrowsingContext* aContext,
                                  mozilla::CSSCoord aOffset);
  static void SetDynamicToolbarMaxHeight(GlobalObject&,
                                         BrowsingContext* aContext,
                                         mozilla::CSSCoord aHeight);
  static uint16_t GetGridContainerType(GlobalObject&, Element&);
  static void GetAnchorFor(GlobalObject&, Element&, const nsAString& aName,
                           Nullable<InspectorAnchorElement>&);
  static void GetAnchorNamesFor(GlobalObject& aGlobal, Element&,
                                nsTArray<nsString>& aResult);
  static void GetComputationStepsSupportedCSSFunctions(
      GlobalObject& aGlobal, nsTArray<nsCString>& aResult);
  static void GetComputationSteps(GlobalObject& aGlobal,
                                  const nsACString& aProperty,
                                  const nsACString& aExpression, Element&,
                                  const nsAString& aPseudo,
                                  nsTArray<nsCString>& aResult);
  static void GetSubstitutedValue(GlobalObject& aGlobal,
                                  const nsACString& aProperty,
                                  const nsACString& aExpression, Element&,
                                  const nsAString& aPseudo,
                                  nsACString& aResult);
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_InspectorUtils_h
