/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/**
 * A collection of utility methods for use by devtools.
 *
 * See InspectorUtils.h for documentation on these methods.
 */
[Func="nsContentUtils::IsCallerChromeOrFuzzingEnabled",
 Exposed=Window]
namespace InspectorUtils {
  // documentOnly tells whether user and UA sheets should get included.
  sequence<StyleSheet> getAllStyleSheets(Document document, optional boolean documentOnly = false);
  sequence<(CSSRule or InspectorDeclaration)> getMatchingCSSRules(
    Element element,
    optional [LegacyNullToEmptyString] DOMString pseudo = "",
    optional boolean relevantLinkVisited = false,
    optional boolean withStartingStyle = false);
  unsigned long getRuleLine(CSSRule rule);
  unsigned long getRuleColumn(CSSRule rule);
  unsigned long getRelativeRuleLine(CSSRule rule);
  sequence<unsigned long> getRuleIndex(CSSRule rule);
  boolean hasRulesModifiedByCSSOM(CSSStyleSheet sheet);
  // Get a flat list of specific at-rules (including nested ones) of a given stylesheet.
  // Useful for DevTools (StyleEditor at-rules sidebar) as this is faster than in JS
  // where we'd have a lot of proxy access overhead building the same list.
  InspectorStyleSheetRuleCountAndAtRulesResult getStyleSheetRuleCountAndAtRules(CSSStyleSheet sheet);
  boolean isInheritedProperty(Document document, UTF8String property);
  sequence<DOMString> getCSSPropertyNames(optional PropertyNamesOptions options = {});
  sequence<PropertyPref> getCSSPropertyPrefs();
  [Throws] sequence<DOMString> getCSSValuesForProperty(UTF8String property);
  sequence<DOMString> getCSSWideKeywords();
  UTF8String rgbToColorName(octet r, octet g, octet b);
  InspectorNearestColor rgbToNearestColorName(float r, float g, float b);
  sequence<float> rgbToHsv(float r, float g, float b);
  sequence<float> hsvToRgb(float h, float s, float v);
  float relativeLuminance(float r, float g, float b);
  InspectorRGBATuple? colorToRGBA(UTF8String colorString);
  InspectorColorToResult? colorTo(UTF8String fromColor, UTF8String toColorSpace);
  boolean isValidCSSColor(UTF8String colorString);
  boolean isValidCSSImage(UTF8String imageString);
  [Throws] sequence<DOMString> getSubpropertiesForCSSProperty(UTF8String property);
  [Throws] boolean cssPropertyIsShorthand(UTF8String property);

  [Throws] boolean cssPropertySupportsType(UTF8String property, InspectorPropertyType type);

  // A version of CSS.supports that allows you to set UA or chrome context.
  boolean supports(UTF8String conditionText, optional SupportsOptions options = {});

  boolean isIgnorableWhitespace(CharacterData dataNode);
  Node? getParentForNode(Node node, boolean showingAnonymousContent);
  sequence<Node> getChildrenForNode(Node node,
                                    boolean showingAnonymousContent,
                                    boolean includeAssignedNodes);
  [Throws] boolean setContentState(Element element, unsigned long long state);
  [Throws] boolean removeContentState(
      Element element,
      unsigned long long state,
      optional boolean clearActiveDocument = false);
  unsigned long long getContentState(Element element);

  // Get the font face(s) actually used to render the text in /range/,
  // as a collection of InspectorFontFace objects (below).
  // If /maxRanges/ is greater than zero, each InspectorFontFace will record
  // up to /maxRanges/ fragments of content that used the face, for the caller
  // to access via its .ranges attribute.
  [NewObject, Throws] sequence<InspectorFontFace> getUsedFontFaces(
      Range range,
      optional unsigned long maxRanges = 0,
      optional boolean skipCollapsedWhitespace = true);

  sequence<DOMString> getCSSPseudoElementNames();
  undefined addPseudoClassLock(Element element,
                               DOMString pseudoClass,
                               optional boolean enabled = true);
  undefined removePseudoClassLock(Element element, DOMString pseudoClass);
  boolean hasPseudoClassLock(Element element, DOMString pseudoClass);
  undefined clearPseudoClassLocks(Element element);
  [Throws] undefined parseStyleSheet(CSSStyleSheet sheet, UTF8String input);
  boolean isCustomElementName([LegacyNullToEmptyString] DOMString name,
                              DOMString? namespaceURI);

  boolean isElementThemed(Element element);

  boolean isUsedColorSchemeDark(Element element);

  Element? containingBlockOf(Element element);

  boolean isBlockContainer(Element element);

  // If the element is styled as display:block, returns an array of numbers giving
  // the number of lines in each fragment.
  // Returns null if the element is not a block.
  [NewObject] sequence<unsigned long>? getBlockLineCounts(Element element);

  [NewObject] NodeList getOverflowingChildrenOfElement(Element element);
  sequence<DOMString> getRegisteredCssHighlights(Document document, optional boolean activeOnly = false);
  sequence<InspectorCSSPropertyDefinition> getCSSRegisteredProperties(Document document);
  InspectorCSSPropertyDefinition? getCSSRegisteredProperty(Document document, UTF8String name);
  boolean valueMatchesSyntax(Document document, UTF8String property, UTF8String value, UTF8String syntax);

  // Get the first rule body text within initialText
  // Consider the following example:
  // p {
  //  line-height: 2em;
  //  color: blue;
  // }
  // Calling the function with the whole text above would return:
  // "line-height: 2em; color: blue;"
  // Returns null when opening curly bracket wasn't found in initialText
  UTF8String? getRuleBodyText(UTF8String initialText);

  // Returns string where the rule body text at passed line and column in styleSheetText
  // is replaced by newBodyText.
  UTF8String? replaceBlockRuleBodyTextInStylesheet(
    UTF8String styleSheetText,
    unsigned long line,
    unsigned long column,
    UTF8String newBodyText);

  // Update the amount of vertical space that is clipped or visibly obscured in
  // the bottom portion of the view. Tells gecko where to put bottom fixed
  // elements so they are fully visible. aOffset must be offset from the bottom
  // edge of the ICB and it's negative.
  // Examples for aOffset:
  // 0: Toolbar fully visible
  // -dynamicToolbarMaxHeight: Toolbar fully hidden (e.g. -40 for a 40px toolbar)
  //
  // Note: These functions must be called from the parent process.
  //
  // This interface may not be the clearest, but we want to match
  // what has been established by the GeckoView API
  [ChromeOnly] undefined setVerticalClipping(BrowsingContext? aContext,
                                             float aOffset);
  [ChromeOnly] undefined setDynamicToolbarMaxHeight(BrowsingContext? aContext,
                                             float aHeight);

  // This element is not a grid container.
  const unsigned short GRID_NONE = 0;
  // This element is a grid container, and might additionally be a subgrid.
  const unsigned short GRID_CONTAINER = 1;
  // This element is a subgrid in the row direction.
  const unsigned short GRID_SUBGRID_ROW = 2;
  // This element is a subgrid in the col direction.
  const unsigned short GRID_SUBGRID_COL = 4;
  // Returns a set of GRID_* flags based on whether the element is a grid
  // container or not.
  unsigned short getGridContainerType(Element aElement);

  // Given a DOM element, return the anchor named `anchorName`, or the default
  // anchor otherwise.
  InspectorAnchorElement? getAnchorFor(Element element, optional DOMString? anchorName = null);
  // Given a DOM element, return a list of valid anchor anchor names.
  sequence<DOMString> getAnchorNamesFor(Element element);


  // Returns the list of functions we support in getComputationSteps
  sequence<UTF8String> getComputationStepsSupportedCSSFunctions();

  // Given a "CSS expression" (e.g. `calc(12vw + 25%)`), return a list of string that represent
  // the steps from it to the computed value
  sequence<UTF8String> getComputationSteps(UTF8String property,
                                           UTF8String expression,
                                           Element element,
                                           optional [LegacyNullToEmptyString] DOMString pseudo = "");

  // Given a "CSS expression" (e.g. `var(--x, attr(data-y))`), returns an expression with
  // the substitution function calls actually substituted
  UTF8String? getSubstitutedValue(UTF8String property,
                                  UTF8String expression,
                                  Element element,
                                  optional [LegacyNullToEmptyString] DOMString pseudo = "");
};

enum InspectorAnchorType {
  "explicit",
  "popover",
  "pseudo-element",
};

dictionary InspectorAnchorElement {
  required Element element;
  required InspectorAnchorType type;
};

enum DeclarationOrigin {
  "user",
  "user-agent",
  "pres-hints",
  "style-attribute",
  "position-fallback",
  "animations",
  "transitions",
  "smil",
};

dictionary InspectorDeclaration {
  required CSSStyleDeclaration style;
  required DeclarationOrigin declarationOrigin;
};

dictionary SupportsOptions {
  boolean userAgent = false;
  boolean chrome = false;
  boolean quirks = false;
  CSSRule? rule = null;
};

dictionary PropertyNamesOptions {
  boolean includeAliases = false;
  boolean includeShorthands = true;
  boolean includeExperimentals = false;
};

dictionary PropertyPref {
  required DOMString name;
  required DOMString pref;
};

dictionary InspectorRGBATuple {
  /*
   * NOTE: This tuple is in the normal 0-255-sized RGB space but can be
   * fractional and may extend outside the 0-255 range.
   *
   * a is in the range 0 - 1.
   */
  double r = 0;
  double g = 0;
  double b = 0;
  double a = 1;
};

dictionary InspectorNearestColor {
  required UTF8String colorName;
  required boolean exact;
};

dictionary InspectorColorToResult {
  required DOMString color;
  required sequence<float> components;
  required boolean adjusted;
};

// Any update to this enum should probably also update
// devtools/shared/css/constants.js
enum InspectorPropertyType {
  "color",
  "gradient",
  "timing-function",
};

dictionary InspectorVariationAxis {
  required DOMString tag;
  required DOMString name;
  required float minValue;
  required float maxValue;
  required float defaultValue;
};

dictionary InspectorVariationValue {
  required DOMString axis;
  required float value;
};

dictionary InspectorVariationInstance {
  required DOMString name;
  required sequence<InspectorVariationValue> values;
};

dictionary InspectorFontFeature {
  required DOMString tag;
  required DOMString script;
  required DOMString languageSystem;
};

dictionary InspectorCSSPropertyDefinition {
  required UTF8String name;
  required UTF8String syntax;
  required boolean inherits;
  required UTF8String? initialValue;
  required boolean fromJS;
};

dictionary InspectorStyleSheetRuleCountAndAtRulesResult {
  required sequence<CSSRule> atRules;
  required unsigned long ruleCount;
};

[Func="nsContentUtils::IsCallerChromeOrFuzzingEnabled",
 Exposed=Window]
interface InspectorFontFace {
  // OpenType IDs for some common name strings; see OpenType spec for details.
  // (These may not always be present; and fonts may also contain names with
  // other arbitrary 16-bit IDs, e.g. for variation axes, feature names, etc.)
  const unsigned short NAME_ID_COPYRIGHT = 0;
  const unsigned short NAME_ID_FAMILY = 1;
  const unsigned short NAME_ID_SUBFAMILY = 2;
  const unsigned short NAME_ID_UNIQUE = 3;
  const unsigned short NAME_ID_FULL = 4;
  const unsigned short NAME_ID_VERSION = 5;
  const unsigned short NAME_ID_POSTSCRIPT = 6;
  const unsigned short NAME_ID_TRADEMARK = 7;
  const unsigned short NAME_ID_MANUFACTURER = 8;
  const unsigned short NAME_ID_DESIGNER = 9;
  const unsigned short NAME_ID_DESCRIPTION = 10;
  const unsigned short NAME_ID_VENDOR_URL = 11;
  const unsigned short NAME_ID_DESIGNER_URL = 12;
  const unsigned short NAME_ID_LICENSE = 13;
  const unsigned short NAME_ID_LICENSE_URL = 14;
  const unsigned short NAME_ID_TYPOGRAPHIC_FAMILY = 16;
  const unsigned short NAME_ID_TYPOGRAPHIC_SUBFAMILY = 17;
  const unsigned short NAME_ID_COMPATIBLE_FULL = 18;
  const unsigned short NAME_ID_SAMPLE_TEXT = 19;

  // An indication of how we found this font during font-matching.
  // Note that the same physical font may have been found in multiple ways within a range.
  readonly attribute boolean fromFontGroup;
  readonly attribute boolean fromLanguagePrefs;
  readonly attribute boolean fromSystemFallback;

  // available for all fonts
  readonly attribute DOMString name; // full font name as obtained from the font resource
  readonly attribute DOMString CSSFamilyName; // a family name that could be used in CSS font-family
                                              // (not necessarily the actual name that was used,
                                              // due to aliases, generics, localized names, etc)
  readonly attribute DOMString CSSGeneric; // CSS generic (serif, sans-serif, etc) that was mapped
                                           // to this font, if any (frequently empty!)

  DOMString getNameString(unsigned short id);  // Specified string from OpenType 'name' table.

  [NewObject,Throws] sequence<InspectorVariationAxis> getVariationAxes();
  [NewObject,Throws] sequence<InspectorVariationInstance> getVariationInstances();
  [NewObject,Throws] sequence<InspectorFontFeature> getFeatures();

  // A list of Ranges of text rendered with this face.
  // This will list the first /maxRanges/ ranges found when InspectorUtils.getUsedFontFaces
  // was called (so it will be empty unless a non-zero maxRanges argument was passed).
  // Note that this indicates how the document was rendered at the time of calling
  // getUsedFontFaces; it does not reflect any subsequent modifications, so if styles
  // have been modified since calling getUsedFontFaces, it may no longer be accurate.
  [Constant,Cached]  readonly attribute sequence<Range> ranges;

  // meaningful only when the font is a user font defined using @font-face
  readonly attribute CSSFontFaceRule? rule; // null if no associated @font-face rule
  readonly attribute long srcIndex; // index in the rule's src list, -1 if no @font-face rule
  readonly attribute DOMString URI; // empty string if not a downloaded font, i.e. local
  readonly attribute DOMString localName; // empty string  if not a src:local(...) rule
  readonly attribute DOMString format; // as per http://www.w3.org/TR/css3-webfonts/#referencing
  readonly attribute DOMString metadata; // XML metadata from WOFF file (if any)
};

dictionary InspectorCSSToken {
  // The token type.
  required UTF8String tokenType;

  // Text associated with the token.
  required UTF8String text;

  // Value of the token. Might differ from `text`:
  // - for `Function` tokens, text contains the opening paren, `value` does not (e.g. `var(` vs `var`)
  // - for `AtKeyword` tokens, text contains the leading `@`, `value` does not (e.g. `@media` vs `media`)
  // - for `Hash` and `IDHash` tokens, text contains the leading `#`, `value` does not (e.g. `#myid` vs `myid`)
  // - for `UnquotedUrl` tokens, text contains the `url(` parts, `value` only holds the url (e.g. `url(test.jpg)` vs `test.jpg`)
  // - for `QuotedString` tokens, text contains the wrapping quotes, `value` does not (e.g. `"hello"` vs `hello`)
  // - for `Comment` tokens, text contains leading `/*` and trailing `*/`, `value` does not (e.g. `/* yo */` vs ` yo `)
  required UTF8String? value;

  // Unit for Dimension tokens
  required UTF8String? unit;

  // Float value for Dimension, Number and Percentage tokens
  double? number = null;
};

/**
 * InspectorCSSParser is an interface to the CSS lexer. It tokenizes an
 * input stream and returns CSS tokens.
 */
[Func="nsContentUtils::IsCallerChromeOrFuzzingEnabled",
 Exposed=Window]
interface InspectorCSSParser {
  constructor(UTF8String text);

  /**
   * The line number of the most recently returned token.  Line
   * numbers are 0-based.
   */
  readonly attribute unsigned long lineNumber;

  /**
   * The column number of the most recently returned token.  Column
   * numbers are 1-based.
   */
  readonly attribute unsigned long columnNumber;

  /**
   * Return the next token, or null at EOF.
   */
  InspectorCSSToken? nextToken();
};
