/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsTreeSanitizer_h_
#define nsTreeSanitizer_h_

#include "mozilla/StaticPtr.h"
#include "mozilla/dom/NameSpaceConstants.h"
#include "mozilla/dom/StaticAtomSet.h"
#include "nsAtom.h"
#include "nsHashKeys.h"
#include "nsHashtablesFwd.h"
#include "nsIPrincipal.h"
#include "nsTArray.h"

class nsIContent;
class nsIGlobalObject;
class nsINode;

namespace mozilla {
class DeclarationBlock;
class ErrorResult;
enum class StyleSanitizationKind : uint8_t;
}  // namespace mozilla

namespace mozilla::dom {
class DocumentFragment;
class Element;
}  // namespace mozilla::dom

/**
 * See the documentation of nsIParserUtils::sanitize for documentation
 * about the default behavior and the configuration options of this sanitizer.
 */
class nsTreeSanitizer {
 public:
  /**
   * The constructor.
   *
   * @param aFlags Flags from nsIParserUtils
   */
  explicit nsTreeSanitizer(uint32_t aFlags = 0);

  static void InitializeStatics();
  static void ReleaseStatics();

  /**
   * Sanitizes a disconnected DOM fragment freshly obtained from a parser.
   * The fragment must have just come from a parser so that it can't have
   * mutation event listeners set on it.
   */
  void Sanitize(mozilla::dom::DocumentFragment* aFragment);

  /**
   * Sanitizes a disconnected (not in a docshell) document freshly obtained
   * from a parser. The document must not be embedded in a docshell and must
   * not have had a chance to get mutation event listeners attached to it.
   * The root element must be <html>.
   */
  void Sanitize(mozilla::dom::Document* aDocument);

  /**
   * Removes conditional CSS from this subtree.
   */
  static void RemoveConditionalCSSFromSubtree(nsINode* aRoot);

 private:
  /**
   * Whether <style> and style="" are allowed.
   */
  bool mAllowStyles;

  /**
   * Whether comment nodes are allowed.
   */
  bool mAllowComments;

  /**
   * Whether HTML <font>, <center>, bgcolor="", etc., are dropped.
   */
  bool mDropNonCSSPresentation;

  /**
   * Whether to remove forms and form controls (excluding fieldset/legend).
   */
  bool mDropForms;

  /**
   * Whether only cid: embeds are allowed.
   */
  bool mCidEmbedsOnly;

  /**
   * Whether to drop <img>, <video>, <audio> and <svg>.
   */
  bool mDropMedia;

  /**
   * Whether we are sanitizing a full document (as opposed to a fragment).
   */
  bool mFullDocument;

  /**
   * Whether we should notify to the console for anything that's stripped.
   */
  bool mLogRemovals;

  void SanitizeChildren(nsINode* aRoot);

  /**
   * Queries if an element must be replaced with its children.
   * @param aNamespace the namespace of the element the question is about
   * @param aLocal the local name of the element the question is about
   * @return true if the element must be replaced with its children and
   *         false if the element is to be kept
   */
  bool MustFlatten(int32_t aNamespace, nsAtom* aLocal);

  /**
   * Queries if an element including its children must be removed.
   * @param aNamespace the namespace of the element the question is about
   * @param aLocal the local name of the element the question is about
   * @param aElement the element node itself for inspecting attributes
   * @return true if the element and its children must be removed and
   *         false if the element is to be kept
   */
  bool MustPrune(int32_t aNamespace, nsAtom* aLocal,
                 mozilla::dom::Element* aElement);

  /**
   * Checks if a given local name (for an attribute) is on the given list
   * of URL attribute names.
   * @param aURLs the list of URL attribute names
   * @param aLocalName the name to search on the list
   * @return true if aLocalName is on the aURLs list and false otherwise
   */
  bool IsURL(const nsStaticAtom* const* aURLs, nsAtom* aLocalName);

  /**
   * Struct for what attributes and their values are allowed.
   */
  struct AllowedAttributes {
    // The whitelist of permitted local names to use.
    mozilla::dom::StaticAtomSet* mNames = nullptr;
    // The local names of URL-valued attributes for URL checking.
    const nsStaticAtom* const* mURLs = nullptr;
    // Whether XLink attributes are allowed.
    bool mXLink = false;
    // Whether the style attribute is allowed.
    bool mStyle = false;
    // Whether to leave the value of the src attribute unsanitized.
    bool mDangerousSrc = false;
  };

  /**
   * Removes dangerous attributes from the element. If the style attribute
   * is allowed, its value is sanitized. The values of URL attributes are
   * sanitized, except src isn't sanitized when it is allowed to remain
   * potentially dangerous.
   *
   * @param aElement the element whose attributes should be sanitized
   * @param aAllowed options for sanitizing attributes
   */
  void SanitizeAttributes(mozilla::dom::Element* aElement,
                          AllowedAttributes aAllowed);

  /**
   * Remove the named URL attribute from the element if the URL fails a
   * security check.
   *
   * @param aElement the element whose attribute to possibly modify
   * @param aNamespace the namespace of the URL attribute
   * @param aLocalName the local name of the URL attribute
   * @param aFragmentsOnly allows same-document references only
   * @return true if the attribute was removed and false otherwise
   */
  bool SanitizeURL(mozilla::dom::Element* aElement, int32_t aNamespace,
                   nsAtom* aLocalName, bool aFragmentsOnly = false);

  /**
   * Checks a style rule for the presence of the 'binding' CSS property and
   * removes that property from the rule.
   *
   * @param aDeclaration The style declaration to check
   * @return true if the rule was modified and false otherwise
   */
  bool SanitizeStyleDeclaration(mozilla::DeclarationBlock* aDeclaration);

  /**
   * Sanitizes an inline style element (an HTML or SVG <style>).
   *
   * Returns whether the style has changed.
   */
  static bool SanitizeInlineStyle(mozilla::dom::Element*,
                                  mozilla::StyleSanitizationKind);

  /**
   * Removes all attributes from an element node.
   */
  static void RemoveAllAttributes(mozilla::dom::Element* aElement);

  /**
   * Removes all attributes from the descendants of an element but not from
   * the element itself.
   */
  static void RemoveAllAttributesFromDescendants(mozilla::dom::Element*);

  /**
   * Log a Console Service message to indicate we removed something.
   * If you pass an element and/or attribute, their information will
   * be appended to the message.
   *
   * @param aMessage   the basic message to log.
   * @param aDocument  the base document we're modifying
   *                   (used for the error message)
   * @param aElement   optional, the element being removed or modified.
   * @param aAttribute optional, the attribute being removed or modified.
   */
  void LogMessage(const char* aMessage, mozilla::dom::Document* aDoc,
                  mozilla::dom::Element* aElement = nullptr,
                  nsAtom* aAttr = nullptr);

  /**
   * The whitelist of HTML elements.
   */
  static mozilla::StaticAutoPtr<mozilla::dom::StaticAtomSet> sElementsHTML;

  /**
   * The whitelist of non-presentational HTML attributes.
   */
  static mozilla::StaticAutoPtr<mozilla::dom::StaticAtomSet> sAttributesHTML;

  /**
   * The whitelist of presentational HTML attributes.
   */
  static mozilla::StaticAutoPtr<mozilla::dom::StaticAtomSet>
      sPresAttributesHTML;

  /**
   * The whitelist of SVG elements.
   */
  static mozilla::StaticAutoPtr<mozilla::dom::StaticAtomSet> sElementsSVG;

  /**
   * The whitelist of SVG attributes.
   */
  static mozilla::StaticAutoPtr<mozilla::dom::StaticAtomSet> sAttributesSVG;

  /**
   * The whitelist of SVG elements.
   */
  static mozilla::StaticAutoPtr<mozilla::dom::StaticAtomSet> sElementsMathML;

  /**
   * The whitelist of MathML attributes.
   */
  static mozilla::StaticAutoPtr<mozilla::dom::StaticAtomSet> sAttributesMathML;

  /**
   * Reusable null principal for URL checks.
   */
  static mozilla::StaticRefPtr<nsIPrincipal> sNullPrincipal;
};

#endif  // nsTreeSanitizer_h_
