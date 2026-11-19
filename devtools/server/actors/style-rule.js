/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { Actor } = require("resource://devtools/shared/protocol.js");
const {
  styleRuleSpec,
} = require("resource://devtools/shared/specs/style-rule.js");

const {
  InspectorCSSParserWrapper,
} = require("resource://devtools/shared/css/lexer.js");
const {
  getRuleText,
  getTextAtLineColumn,
} = require("resource://devtools/server/actors/stylesheets/style-utils.js");

const {
  style: { ELEMENT_STYLE, PRES_HINTS },
} = require("resource://devtools/shared/constants.js");

loader.lazyRequireGetter(
  this,
  "CssLogic",
  "resource://devtools/server/actors/inspector/css-logic.js",
  true
);
loader.lazyRequireGetter(
  this,
  "getNodeDisplayName",
  "resource://devtools/server/actors/inspector/utils.js",
  true
);
loader.lazyRequireGetter(
  this,
  "SharedCssLogic",
  "resource://devtools/shared/inspector/css-logic.js"
);
loader.lazyRequireGetter(
  this,
  "isCssPropertyKnown",
  "resource://devtools/server/actors/css-properties.js",
  true
);
loader.lazyRequireGetter(
  this,
  "getInactiveCssDataForProperty",
  "resource://devtools/server/actors/utils/inactive-property-helper.js",
  true
);
loader.lazyRequireGetter(
  this,
  "parseNamedDeclarations",
  "resource://devtools/shared/css/parsing-utils.js",
  true
);
loader.lazyRequireGetter(
  this,
  ["UPDATE_PRESERVING_RULES", "UPDATE_GENERAL"],
  "resource://devtools/server/actors/stylesheets/stylesheets-manager.js",
  true
);
loader.lazyRequireGetter(
  this,
  "DocumentWalker",
  "devtools/server/actors/inspector/document-walker",
  true
);

const XHTML_NS = "http://www.w3.org/1999/xhtml";

/**
 * An actor that represents a CSS style object on the protocol.
 *
 * We slightly flatten the CSSOM for this actor, it represents
 * both the CSSRule and CSSStyle objects in one actor.  For nodes
 * (which have a CSSStyle but no CSSRule) we create a StyleRuleActor
 * with a special rule type (100).
 */
class StyleRuleActor extends Actor {
  /**
   *
   * @param {object} options
   * @param {PageStyleActor} options.pageStyle
   * @param {CSSStyleRule|Element} options.item
   * @param {boolean} options.userAdded: Optional boolean to distinguish rules added by the user.
   * @param {string} options.pseudoElement An optional pseudo-element type in cases when
   *        the CSS rule applies to a pseudo-element.
   */
  constructor({ pageStyle, item, userAdded = false, pseudoElement = null }) {
    super(pageStyle.conn, styleRuleSpec);
    this.pageStyle = pageStyle;
    this.rawStyle = item.style;
    this._userAdded = userAdded;
    this._pseudoElements = new Set();
    this._pseudoElement = pseudoElement;
    if (pseudoElement) {
      this._pseudoElements.add(pseudoElement);
    }
    this._parentSheet = null;
    // Parsed CSS declarations from this.form().declarations used to check CSS property
    // names and values before tracking changes. Using cached values instead of accessing
    // this.form().declarations on demand because that would cause needless re-parsing.
    this._declarations = [];

    this._pendingDeclarationChanges = [];
    this._failedToGetRuleText = false;

    if (CSSRule.isInstance(item)) {
      this.type = item.type;
      this.ruleClassName = ChromeUtils.getClassName(item);

      this.rawRule = item;
      this._computeRuleIndex();
      if (this.#isRuleSupported() && this.rawRule.parentStyleSheet) {
        this.line = InspectorUtils.getRelativeRuleLine(this.rawRule);
        this.column = InspectorUtils.getRuleColumn(this.rawRule);
        this._parentSheet = this.rawRule.parentStyleSheet;
      }
    } else if (item.declarationOrigin === "pres-hints") {
      this.type = PRES_HINTS;
      this.ruleClassName = PRES_HINTS;
      this.rawNode = item;
      this.rawRule = {
        style: item.style,
        toString() {
          return "[element attribute styles " + this.style + "]";
        },
      };
    } else {
      // Fake a rule
      this.type = ELEMENT_STYLE;
      this.ruleClassName = ELEMENT_STYLE;
      this.rawNode = item;
      this.rawRule = {
        style: item.style,
        toString() {
          return "[element rule " + this.style + "]";
        },
      };
    }
  }

  destroy() {
    if (!this.rawStyle) {
      return;
    }
    super.destroy();
    this.rawStyle = null;
    this.pageStyle = null;
    this.rawNode = null;
    this.rawRule = null;
    this._declarations = null;
    if (this._pseudoElements) {
      this._pseudoElements.clear();
      this._pseudoElements = null;
    }
  }

  // Objects returned by this actor are owned by the PageStyleActor
  // to which this rule belongs.
  get marshallPool() {
    return this.pageStyle;
  }

  // True if this rule supports as-authored styles, meaning that the
  // rule text can be rewritten using setRuleText.
  get canSetRuleText() {
    if (this.type === ELEMENT_STYLE) {
      // Element styles are always editable.
      return true;
    }
    if (!this._parentSheet) {
      return false;
    }
    if (InspectorUtils.hasRulesModifiedByCSSOM(this._parentSheet)) {
      // If a rule has been modified via CSSOM, then we should fall back to
      // non-authored editing.
      // https://bugzilla.mozilla.org/show_bug.cgi?id=1224121
      return false;
    }
    return true;
  }

  /**
   * Return an array with StyleRuleActor instances for each of this rule's ancestor rules
   * (@media, @supports, @keyframes, etc) obtained by recursively reading rule.parentRule.
   * If the rule has no ancestors, return an empty array.
   *
   * @return {Array}
   */
  get ancestorRules() {
    const ancestors = [];
    let rule = this.rawRule;

    while (rule.parentRule) {
      ancestors.unshift(this.pageStyle.styleRef(rule.parentRule));
      rule = rule.parentRule;
    }

    return ancestors;
  }

  /**
   * Return an object with information about this rule used for tracking changes.
   * It will be decorated with information about a CSS change before being tracked.
   *
   * It contains:
   * - the rule selector (or generated selectror for inline styles)
   * - the rule's host stylesheet (or element for inline styles)
   * - the rule's ancestor rules (@media, @supports, @keyframes), if any
   * - the rule's position within its ancestor tree, if any
   *
   * @return {object}
   */
  get metadata() {
    const data = {};
    data.id = this.actorID;
    // Collect information about the rule's ancestors (@media, @supports, @keyframes, parent rules).
    // Used to show context for this change in the UI and to match the rule for undo/redo.
    data.ancestors = this.ancestorRules.map(rule => {
      const ancestorData = {
        id: rule.actorID,
        // Array with the indexes of this rule and its ancestors within the CSS rule tree.
        ruleIndex: rule._ruleIndex,
      };

      // Rule type as human-readable string (ex: "@media", "@supports", "@keyframes")
      const typeName = SharedCssLogic.getCSSAtRuleTypeName(rule.rawRule);
      if (typeName) {
        ancestorData.typeName = typeName;
      }

      // Conditions of @container, @media and @supports rules (ex: "min-width: 1em")
      if (rule.rawRule.conditionText !== undefined) {
        ancestorData.conditionText = rule.rawRule.conditionText;
      }

      // Name of @keyframes rule; referenced by the animation-name CSS property.
      if (rule.rawRule.name !== undefined) {
        ancestorData.name = rule.rawRule.name;
      }

      // Selector of individual @keyframe rule within a @keyframes rule (ex: 0%, 100%).
      if (rule.rawRule.keyText !== undefined) {
        ancestorData.keyText = rule.rawRule.keyText;
      }

      // Selector of the rule; might be useful in case for nested rules
      if (rule.rawRule.selectorText !== undefined) {
        ancestorData.selectorText = rule.rawRule.selectorText;
      }

      return ancestorData;
    });

    // For changes in element style attributes, generate a unique selector.
    if (this.type === ELEMENT_STYLE && this.rawNode) {
      // findCssSelector() fails on XUL documents. Catch and silently ignore that error.
      try {
        data.selector = SharedCssLogic.findCssSelector(this.rawNode);
      } catch (err) {}

      data.source = {
        type: "element",
        // Used to differentiate between elements which match the same generated selector
        // but live in different documents (ex: host document and iframe).
        href: this.rawNode.baseURI,
        // Element style attributes don't have a rule index; use the generated selector.
        index: data.selector,
        // Whether the element lives in a different frame than the host document.
        isFramed: this.rawNode.documentGlobal !== this.pageStyle.ownerWindow,
      };

      const nodeActor = this.pageStyle.walker.getNode(this.rawNode);
      if (nodeActor) {
        data.source.id = nodeActor.actorID;
      }

      data.ruleIndex = 0;
    } else {
      data.selector =
        this.ruleClassName === "CSSKeyframeRule"
          ? this.rawRule.keyText
          : this.rawRule.selectorText;
      // Used to differentiate between changes to rules with identical selectors.
      data.ruleIndex = this._ruleIndex;

      const sheet = this._parentSheet;
      const inspectorActor = this.pageStyle.inspector;
      const resourceId =
        this.pageStyle.styleSheetsManager.getStyleSheetResourceId(sheet);
      data.source = {
        // Inline stylesheets have a null href; Use window URL instead.
        type: sheet.href ? "stylesheet" : "inline",
        href: sheet.href || inspectorActor.window.location.toString(),
        id: resourceId,
        // Whether the stylesheet lives in a different frame than the host document.
        isFramed: inspectorActor.window !== inspectorActor.window.top,
      };
    }

    return data;
  }

  /**
   * Returns true if the pseudo element anonymous node (e.g. ::before, ::marker, …) is selected.
   * Returns false if a non pseudo element node is selected and we're looking into its pseudo
   * elements rules (i.e. this is for the "Pseudo-elements" section in the Rules view")
   */
  get isPseudoElementAnonymousNodeSelected() {
    if (!this._pseudoElement) {
      return false;
    }

    // `this._pseudoElement` is the returned value by getNodeDisplayName, i.e that does
    // differ from this.pageStyle.selectedElement.implementedPseudoElement (e.g. for
    // view transition element, it will be `::view-transition-group(root)`, while
    // implementedPseudoElement will be `::view-transition-group`).
    return (
      this._pseudoElement === getNodeDisplayName(this.pageStyle.selectedElement)
    );
  }

  /**
   * StyleRuleActor is spawned once per CSS Rule, but will be refreshed based on the
   * currently selected DOM Element, which is updated when PageStyleActor.getApplied
   * is called.
   */
  get currentlySelectedElement() {
    let { selectedElement } = this.pageStyle;
    // If we're not handling a pseudo element, or if the pseudo element node
    // (e.g. ::before, ::marker, …) is the one selected in the markup view, we can
    // directly return selected element.
    if (!this._pseudoElement || this.isPseudoElementAnonymousNodeSelected) {
      return selectedElement;
    }

    // Otherwise we are selecting the pseudo element "parent" (binding), and we need to
    // walk down the tree from `selectedElement` to find the pseudo element.

    // FIXME: ::view-transition pseudo elements don't have a _moz_generated_content_ prefixed
    // nodename, but have specific type and name attribute.
    // At the moment this isn't causing any issues because we don't display the view
    // transition rules in the pseudo element section, but this should be fixed in Bug 1998345.
    const pseudo = this._pseudoElement.replaceAll(":", "");
    const nodeName = `_moz_generated_content_${pseudo}`;

    if (selectedElement.nodeName !== nodeName) {
      const walker = new DocumentWalker(
        selectedElement,
        selectedElement.documentGlobal
      );

      for (let next = walker.firstChild(); next; next = walker.nextSibling()) {
        if (next.nodeName === nodeName) {
          selectedElement = next;
          break;
        }
      }
    }

    return selectedElement;
  }

  get currentlySelectedElementComputedStyle() {
    if (!this._pseudoElement) {
      return this.pageStyle.cssLogic.computedStyle;
    }

    const { selectedElement } = this.pageStyle;

    return selectedElement.documentGlobal.getComputedStyle(
      selectedElement,
      // If we are selecting the pseudo element parent, we need to pass the pseudo element
      // to getComputedStyle to actually get the computed style of the pseudo element.
      !this.isPseudoElementAnonymousNodeSelected ? this._pseudoElement : null
    );
  }

  get pseudoElements() {
    return this._pseudoElements;
  }

  addPseudo(pseudoElement) {
    this._pseudoElements.add(pseudoElement);
  }

  getDocument(sheet) {
    if (!sheet.associatedDocument) {
      throw new Error(
        "Failed trying to get the document of an invalid stylesheet"
      );
    }
    return sheet.associatedDocument;
  }

  toString() {
    return "[StyleRuleActor for " + this.rawRule + "]";
  }

  // eslint-disable-next-line complexity
  form() {
    const form = {
      actor: this.actorID,
      type: this.type,
      className: this.ruleClassName,
      line: this.line || undefined,
      column: this.column,
      traits: {
        // Indicates whether StyleRuleActor implements and can use the setRuleText method.
        // It cannot use it if the stylesheet was programmatically mutated via the CSSOM.
        canSetRuleText: this.canSetRuleText,
      },
    };

    // This rule was manually added by the user and may be automatically focused by the frontend.
    if (this._userAdded) {
      form.userAdded = true;
    }

    form.ancestorData = this._getAncestorDataForForm();

    if (this._parentSheet) {
      form.parentStyleSheet =
        this.pageStyle.styleSheetsManager.getStyleSheetResourceId(
          this._parentSheet
        );
    }

    // One tricky thing here is that other methods in this actor must
    // ensure that authoredText has been set before |form| is called.
    // This has to be treated specially, for now, because we cannot
    // synchronously compute the authored text, but |form| also cannot
    // return a promise.  See bug 1205868.
    form.authoredText = this.authoredText;
    form.cssText = this._getCssText();

    switch (this.ruleClassName) {
      case "CSSNestedDeclarations":
        form.isNestedDeclarations = true;
        form.selectors = [];
        form.selectorsSpecificity = [];
        break;
      case "CSSStyleRule": {
        form.selectors = [];
        form.selectorsSpecificity = [];

        for (let i = 0, len = this.rawRule.selectorCount; i < len; i++) {
          form.selectors.push(this.rawRule.selectorTextAt(i));
          form.selectorsSpecificity.push(
            this.rawRule.selectorSpecificityAt(
              i,
              /* desugared, so we get the actual specificity */ true
            )
          );
        }

        // Only add the property when there are elements in the array to save up on serialization.
        const selectorWarnings = this.rawRule.getSelectorWarnings();
        if (selectorWarnings.length) {
          form.selectorWarnings = selectorWarnings;
        }
        break;
      }
      case ELEMENT_STYLE: {
        // Elements don't have a parent stylesheet, and therefore
        // don't have an associated URI.  Provide a URI for
        // those.
        const doc = this.rawNode.ownerDocument;
        form.href = doc.location ? doc.location.href : "";
        form.authoredText = this.rawNode.getAttribute("style");
        break;
      }
      case PRES_HINTS:
        form.href = "";
        break;
      case "CSSCharsetRule":
        form.encoding = this.rawRule.encoding;
        break;
      case "CSSImportRule":
        form.href = this.rawRule.href;
        break;
      case "CSSKeyframesRule":
      case "CSSPositionTryRule":
        form.name = this.rawRule.name;
        break;
      case "CSSKeyframeRule":
        form.keyText = this.rawRule.keyText || "";
        break;
    }

    // Parse the text into a list of declarations so the client doesn't have to
    // and so that we can safely determine if a declaration is valid rather than
    // have the client guess it.
    if (form.authoredText || form.cssText) {
      const declarations = this.parseRuleDeclarations({
        parseComments: true,
      });
      const el = this.currentlySelectedElement;
      const style = this.currentlySelectedElementComputedStyle;

      // Whether the stylesheet is a user-agent stylesheet. This affects the
      // validity of some properties and property values.
      const userAgent =
        this._parentSheet &&
        SharedCssLogic.isAgentStylesheet(this._parentSheet);
      // Whether the stylesheet is a chrome stylesheet. Ditto.
      //
      // Note that chrome rules are also enabled in user sheets, see
      // ParserContext::chrome_rules_enabled().
      //
      // https://searchfox.org/mozilla-central/rev/919607a3610222099fbfb0113c98b77888ebcbfb/servo/components/style/parser.rs#164
      const chrome = (() => {
        if (!this._parentSheet) {
          return false;
        }
        if (SharedCssLogic.isUserStylesheet(this._parentSheet)) {
          return true;
        }
        if (this._parentSheet.href) {
          return this._parentSheet.href.startsWith("chrome:");
        }
        return el && el.ownerDocument.documentURI.startsWith("chrome:");
      })();
      // Whether the document is in quirks mode. This affects whether stuff
      // like `width: 10` is valid.
      const quirks =
        !userAgent && el && el.ownerDocument.compatMode == "BackCompat";
      const supportsOptions = { userAgent, chrome, quirks };

      const targetDocument =
        this.pageStyle.inspector.targetActor.window.document;
      let registeredProperties;

      form.declarations = declarations.map(decl => {
        // InspectorUtils.supports only supports the 1-arg version, but that's
        // what we want to do anyways so that we also accept !important in the
        // value.
        decl.isValid =
          // Always consider pres hints styles declarations valid. We need this because
          // in some cases we might get quirks declarations for which we serialize the
          // value to something meaningful for the user, but that can't be actually set.
          // (e.g. for <table> in quirks mode, we get a `color: -moz-inherit-from-body-quirk`)
          // In such case InspectorUtils.supports() would return false, but that would be
          // odd to show "invalid" pres hints declaration in the UI.
          this.ruleClassName === PRES_HINTS ||
          (InspectorUtils.supports(
            `${decl.name}:${decl.value}`,
            supportsOptions
          ) &&
            // !important values are not valid in @position-try and @keyframes
            // TODO: We might extend InspectorUtils.supports to take the actual rule
            // so we wouldn't have to hardcode this, but this does come with some
            // challenges (see Bug 2004379).
            !(
              decl.priority === "important" &&
              (this.ruleClassName === "CSSPositionTryRule" ||
                this.ruleClassName === "CSSKeyframesRule")
            ));
        const inactiveCssData = getInactiveCssDataForProperty(
          el,
          style,
          this.rawRule,
          decl.name
        );
        if (inactiveCssData !== null) {
          decl.inactiveCssData = inactiveCssData;
        }

        // Check property name. All valid CSS properties support "initial" as a value.
        decl.isNameValid =
          // InspectorUtils.supports can be costly, don't call it when the declaration
          // is a CSS variable, it should always be valid
          decl.isCustomProperty ||
          InspectorUtils.supports(`${decl.name}:initial`, supportsOptions);

        if (decl.isCustomProperty) {
          decl.computedValue = style.getPropertyValue(decl.name);

          // If the variable is a registered property, we check if the variable is
          // invalid at computed-value time (e.g. if the declaration value matches
          // the `syntax` defined in the registered property)
          if (!registeredProperties) {
            registeredProperties =
              InspectorUtils.getCSSRegisteredProperties(targetDocument);
          }
          const registeredProperty = registeredProperties.find(
            prop => prop.name === decl.name
          );
          if (registeredProperty) {
            const declarationValue = decl.value;
            let substitutedValue = null;
            if (
              declarationValue.includes("var(") ||
              declarationValue.includes("attr(") ||
              declarationValue.includes("env(")
            ) {
              substitutedValue = InspectorUtils.getSubstitutedValue(
                decl.name,
                declarationValue,
                this.pageStyle.selectedElement,
                this._pseudoElement
              );
            }

            if (
              !InspectorUtils.valueMatchesSyntax(
                targetDocument,
                decl.name,
                substitutedValue || declarationValue,
                registeredProperty.syntax
              )
            ) {
              // if the value doesn't match the syntax, it's invalid
              decl.invalidAtComputedValueTime = true;
              // pass the syntax down to the client so it can easily be used in a warning message
              decl.syntax = registeredProperty.syntax;
            }
          }

          // We only compute `inherits` for css variable declarations.
          // For "regular" declaration, we use `CssPropertiesFront.isInherited`,
          // which doesn't depend on the state of the document (a given property will
          // always have the same isInherited value).
          // CSS variables on the other hand can be registered custom properties (e.g.,
          // `@property`/`CSS.registerProperty`), with a `inherits` definition that can
          // be true or false.
          // As such custom properties can be registered at any time during the page
          // lifecycle, we always recompute the `inherits` information for CSS variables.
          decl.inherits = InspectorUtils.isInheritedProperty(
            this.pageStyle.inspector.window.document,
            decl.name
          );
        }

        return decl;
      });

      // We have computed the new `declarations` array, before forgetting about
      // the old declarations compute the CSS changes for pending modifications
      // applied by the user. Comparing the old and new declarations arrays
      // ensures we only rely on values understood by the engine and not authored
      // values. See Bug 1590031.
      this._pendingDeclarationChanges.forEach(change =>
        this.logDeclarationChange(change, declarations, this._declarations)
      );
      this._pendingDeclarationChanges = [];

      // Cache parsed declarations so we don't needlessly re-parse authoredText every time
      // we need to check previous property names and values when tracking changes.
      this._declarations = declarations;
    }

    return form;
  }

  /**
   * Return the rule cssText if applicable, null otherwise
   *
   * @returns {string | null}
   */
  _getCssText() {
    switch (this.ruleClassName) {
      case "CSSNestedDeclarations":
      case "CSSPositionTryRule":
      case "CSSStyleRule":
      case ELEMENT_STYLE:
      case PRES_HINTS:
        return this.rawStyle.cssText || "";
      case "CSSKeyframesRule":
      case "CSSKeyframeRule":
        return this.rawRule.cssText;
    }
    return null;
  }

  /**
   * Parse the rule declarations from its text.
   *
   * @param {object} options
   * @param {boolean} options.parseComments
   * @returns {Array} @see parseNamedDeclarations
   */
  parseRuleDeclarations({ parseComments }) {
    const authoredText =
      this.ruleClassName === ELEMENT_STYLE
        ? this.rawNode.getAttribute("style")
        : this.authoredText;

    // authoredText may be an empty string when deleting all properties; it's ok to use.
    const cssText =
      typeof authoredText === "string" ? authoredText : this._getCssText();
    if (!cssText) {
      return [];
    }

    return parseNamedDeclarations(isCssPropertyKnown, cssText, parseComments);
  }

  /**
   *
   * @returns {Array<object>} ancestorData: An array of ancestor item data
   */
  _getAncestorDataForForm() {
    const ancestorData = [];

    // We don't want to compute ancestor rules for keyframe rule, as they can only be
    // in @keyframes rules.
    if (this.ruleClassName === "CSSKeyframeRule") {
      return ancestorData;
    }

    // Go through all ancestor so we can build an array of all the media queries and
    // layers this rule is in.
    for (const ancestorRule of this.ancestorRules) {
      const rawRule = ancestorRule.rawRule;
      const ruleClassName = ChromeUtils.getClassName(rawRule);
      const type = SharedCssLogic.CSSAtRuleClassNameType[ruleClassName];

      if (ruleClassName === "CSSMediaRule" && rawRule.media?.length) {
        ancestorData.push({
          type,
          value: Array.from(rawRule.media).join(", "),
        });
      } else if (ruleClassName === "CSSLayerBlockRule") {
        ancestorData.push({
          // we need the actorID so we can uniquely identify nameless layers on the client
          actorID: ancestorRule.actorID,
          type,
          value: rawRule.name,
        });
      } else if (ruleClassName === "CSSContainerRule") {
        ancestorData.push({
          type,
          // send the array of conditions (e.g. their name and query instead of conditionText)
          // so the client has more flexibility to display the information.
          conditions: Array.from(rawRule.conditions).map((condition, i) => ({
            containerName: condition.name,
            containerQuery: condition.query,
            matched: rawRule.queryConditionMatchesElement(
              this.currentlySelectedElement,
              i
            ),
            hasContainer: !!rawRule.queryContainerFor(
              this.currentlySelectedElement,
              i
            ),
          })),
        });
      } else if (ruleClassName === "CSSSupportsRule") {
        ancestorData.push({
          type,
          conditionText: rawRule.conditionText,
        });
      } else if (ruleClassName === "CSSScopeRule") {
        ancestorData.push({
          type,
          start: rawRule.start,
          end: rawRule.end,
        });
      } else if (
        ruleClassName === "CSSStartingStyleRule" ||
        ruleClassName === "CSSAppearanceBaseRule"
      ) {
        ancestorData.push({
          type,
        });
      } else if (rawRule.selectorText) {
        // All the previous cases where about at-rules; this one is for regular rule
        // that are ancestors because CSS nesting was used.
        // In such case, we want to return the selectorText so it can be displayed in the UI.
        const ancestor = {
          type,
          selectors: CssLogic.getSelectors(rawRule),
        };

        // Only add the property when there are elements in the array to save up on serialization.
        const selectorWarnings = rawRule.getSelectorWarnings();
        if (selectorWarnings.length) {
          ancestor.selectorWarnings = selectorWarnings;
        }

        ancestorData.push(ancestor);
      }
    }

    if (this._parentSheet) {
      // Loop through all parent stylesheets to get the whole list of @import rules.
      let rule = this.rawRule;
      while ((rule = rule.parentStyleSheet?.ownerRule)) {
        // If the rule is in a imported stylesheet with a specified layer
        if (rule.layerName !== null) {
          // Put the item at the top of the ancestor data array, as we're going up
          // in the stylesheet hierarchy, and we want to display ancestor rules in the
          // orders they're applied.
          ancestorData.unshift({
            type: "layer",
            value: rule.layerName,
          });
        }

        // If the rule is in a imported stylesheet with specified media/supports conditions
        if (rule.media?.mediaText || rule.supportsText) {
          const parts = [];
          if (rule.supportsText) {
            parts.push(`supports(${rule.supportsText})`);
          }

          if (rule.media?.mediaText) {
            parts.push(rule.media.mediaText);
          }

          // Put the item at the top of the ancestor data array, as we're going up
          // in the stylesheet hierarchy, and we want to display ancestor rules in the
          // orders they're applied.
          ancestorData.unshift({
            type: "import",
            value: parts.join(" "),
          });
        }
      }
    }
    return ancestorData;
  }

  /**
   * Send an event notifying that the location of the rule has
   * changed.
   *
   * @param {number} line the new line number
   * @param {number} column the new column number
   */
  _notifyLocationChanged(line, column) {
    this.emit("location-changed", line, column);
  }

  /**
   * Compute the index of this actor's raw rule in its parent style
   * sheet.  The index is a vector where each element is the index of
   * a given CSS rule in its parent.  A vector is used to support
   * nested rules.
   */
  _computeRuleIndex() {
    const index = InspectorUtils.getRuleIndex(this.rawRule);
    this._ruleIndex = index.length ? index : null;
  }

  /**
   * Get the rule corresponding to |this._ruleIndex| from the given
   * style sheet.
   *
   * @param  {DOMStyleSheet} sheet
   *         The style sheet.
   * @return {CSSStyleRule} the rule corresponding to
   * |this._ruleIndex|
   */
  _getRuleFromIndex(parentSheet) {
    let currentRule = null;
    for (const i of this._ruleIndex) {
      if (currentRule === null) {
        currentRule = parentSheet.cssRules[i];
      } else {
        currentRule = currentRule.cssRules.item(i);
      }
    }
    return currentRule;
  }

  /**
   * Called from PageStyle actor _onStylesheetUpdated.
   */
  onStyleApplied(kind) {
    if (kind === UPDATE_GENERAL) {
      // A general change means that the rule actors are invalidated, nothing
      // to do here.
      return;
    }

    if (this._ruleIndex) {
      // The sheet was updated by this actor, in a way that preserves
      // the rules.  Now, recompute our new rule from the style sheet,
      // so that we aren't left with a reference to a dangling rule.
      const oldRule = this.rawRule;
      const oldActor = this.pageStyle.refMap.get(oldRule);
      this.rawRule = this._getRuleFromIndex(this._parentSheet);
      if (oldActor) {
        // Also tell the page style so that future calls to _styleRef
        // return the same StyleRuleActor.
        this.pageStyle.updateStyleRef(oldRule, this.rawRule, this);
      }
      const line = InspectorUtils.getRelativeRuleLine(this.rawRule);
      const column = InspectorUtils.getRuleColumn(this.rawRule);
      if (line !== this.line || column !== this.column) {
        this._notifyLocationChanged(line, column);
      }
      this.line = line;
      this.column = column;
    }
  }

  #SUPPORTED_RULES_CLASSNAMES = new Set([
    "CSSContainerRule",
    "CSSKeyframeRule",
    "CSSKeyframesRule",
    "CSSLayerBlockRule",
    "CSSMediaRule",
    "CSSNestedDeclarations",
    "CSSPositionTryRule",
    "CSSStyleRule",
    "CSSSupportsRule",
  ]);

  #isRuleSupported() {
    // this.rawRule might not be an actual CSSRule (e.g. when this represent an element style),
    // and in such case, ChromeUtils.getClassName will throw
    try {
      const ruleClassName = ChromeUtils.getClassName(this.rawRule);
      return this.#SUPPORTED_RULES_CLASSNAMES.has(ruleClassName);
    } catch (e) {}

    return false;
  }

  /**
   * Return a promise that resolves to the authored form of a rule's
   * text, if available.  If the authored form is not available, the
   * returned promise simply resolves to the empty string.  If the
   * authored form is available, this also sets |this.authoredText|.
   * The authored text will include invalid and otherwise ignored
   * properties.
   *
   * @param {boolean} skipCache
   *        If a value for authoredText was previously found and cached,
   *        ignore it and parse the stylehseet again. The authoredText
   *        may be outdated if a descendant of this rule has changed.
   */
  async getAuthoredCssText(skipCache = false) {
    if (!this.canSetRuleText || !this.#isRuleSupported()) {
      return "";
    }

    if (!skipCache) {
      if (this._failedToGetRuleText) {
        return "";
      }
      if (typeof this.authoredText === "string") {
        return this.authoredText;
      }
    }

    try {
      if (this.ruleClassName == "CSSNestedDeclarations") {
        throw new Error("getRuleText doesn't deal well with bare declarations");
      }
      const resourceId =
        this.pageStyle.styleSheetsManager.getStyleSheetResourceId(
          this._parentSheet
        );
      const cssText =
        await this.pageStyle.styleSheetsManager.getText(resourceId);
      const text = getRuleText(cssText, this.line, this.column);
      // Cache the result on the rule actor to avoid parsing again next time
      this._failedToGetRuleText = false;
      this.authoredText = text;
    } catch (e) {
      this._failedToGetRuleText = true;
      this.authoredText = undefined;
      return "";
    }
    return this.authoredText;
  }

  /**
   * Return a promise that resolves to the complete cssText of the rule as authored.
   *
   * Unlike |getAuthoredCssText()|, which only returns the contents of the rule, this
   * method includes the CSS selectors and at-rules (@media, @supports, @keyframes, etc.)
   *
   * If the rule type is unrecongized, the promise resolves to an empty string.
   * If the rule is an element inline style, the promise resolves with the generated
   * selector that uniquely identifies the element and with the rule body consisting of
   * the element's style attribute.
   *
   * @return {string}
   */
  async getRuleText() {
    // Bail out if the rule is not supported or not an element inline style.
    if (!this.#isRuleSupported(true) && this.type !== ELEMENT_STYLE) {
      return "";
    }

    let ruleBodyText;
    let selectorText;

    // For element inline styles, use the style attribute and generated unique selector.
    if (this.type === ELEMENT_STYLE) {
      ruleBodyText = this.rawNode.getAttribute("style");
      selectorText = this.metadata.selector;
    } else {
      // Get the rule's authored text and skip any cached value.
      ruleBodyText = await this.getAuthoredCssText(true);

      const resourceId =
        this.pageStyle.styleSheetsManager.getStyleSheetResourceId(
          this._parentSheet
        );
      const stylesheetText =
        await this.pageStyle.styleSheetsManager.getText(resourceId);

      const [start, end] = getSelectorOffsets(
        stylesheetText,
        this.line,
        this.column
      );
      selectorText = stylesheetText.substring(start, end);
    }

    const text = `${selectorText} {${ruleBodyText}}`;
    const { result } = SharedCssLogic.prettifyCSS(text);
    return result;
  }

  /**
   * Set the contents of the rule.  This rewrites the rule in the
   * stylesheet and causes it to be re-evaluated.
   *
   * @param {string} newText
   *        The new text of the rule
   * @param {Array} modifications
   *        Array with modifications applied to the rule. Contains objects like:
   *        {
   *          type: "set",
   *          index: <number>,
   *          name: <string>,
   *          value: <string>,
   *          priority: <optional string>
   *        }
   *         or
   *        {
   *          type: "remove",
   *          index: <number>,
   *          name: <string>,
   *        }
   * @returns the rule with updated properties
   */
  async setRuleText(newText, modifications = []) {
    if (!this.canSetRuleText) {
      throw new Error("invalid call to setRuleText");
    }

    if (this.type === ELEMENT_STYLE) {
      // For element style rules, set the node's style attribute.
      this.rawNode.setAttributeDevtools("style", newText);
    } else {
      const resourceId =
        this.pageStyle.styleSheetsManager.getStyleSheetResourceId(
          this._parentSheet
        );

      const sheetText =
        await this.pageStyle.styleSheetsManager.getText(resourceId);
      const cssText = InspectorUtils.replaceBlockRuleBodyTextInStylesheet(
        sheetText,
        this.line,
        this.column,
        newText
      );

      if (typeof cssText !== "string") {
        throw new Error(
          "Error in InspectorUtils.replaceBlockRuleBodyTextInStylesheet"
        );
      }

      // setStyleSheetText will parse the stylesheet which can be costly, so only do it
      // if the text has actually changed.
      if (sheetText !== newText) {
        await this.pageStyle.styleSheetsManager.setStyleSheetText(
          resourceId,
          cssText,
          { kind: UPDATE_PRESERVING_RULES }
        );
      }
    }

    this.authoredText = newText;
    await this.updateAncestorRulesAuthoredText();
    this.pageStyle.refreshObservedRules(this.ancestorRules);

    // Add processed modifications to the _pendingDeclarationChanges array,
    // they will be emitted as CSS_CHANGE resources once `declarations` have
    // been re-computed in `form`.
    this._pendingDeclarationChanges.push(...modifications);

    // Returning this updated actor over the protocol will update its corresponding front
    // and any references to it.
    return this;
  }

  /**
   * Update the authored text of the ancestor rules. This should be called when setting
   * the authored text of a (nested) rule, so all the references are properly updated.
   */
  async updateAncestorRulesAuthoredText() {
    return Promise.all(
      this.ancestorRules.map(rule => rule.getAuthoredCssText(true))
    );
  }

  /**
   * Modify a rule's properties. Passed an array of modifications:
   * {
   *   type: "set",
   *   index: <number>,
   *   name: <string>,
   *   value: <string>,
   *   priority: <optional string>
   * }
   *  or
   * {
   *   type: "remove",
   *   index: <number>,
   *   name: <string>,
   * }
   *
   * @returns the rule with updated properties
   */
  modifyProperties(modifications) {
    // Use a fresh element for each call to this function to prevent side
    // effects that pop up based on property values that were already set on the
    // element.
    let document;
    if (this.rawNode) {
      document = this.rawNode.ownerDocument;
    } else {
      let parentStyleSheet = this._parentSheet;
      while (parentStyleSheet.ownerRule) {
        parentStyleSheet = parentStyleSheet.ownerRule.parentStyleSheet;
      }

      document = this.getDocument(parentStyleSheet);
    }

    const tempElement = document.createElementNS(XHTML_NS, "div");

    for (const mod of modifications) {
      if (mod.type === "set") {
        tempElement.style.setProperty(mod.name, mod.value, mod.priority || "");
        this.rawStyle.setProperty(
          mod.name,
          tempElement.style.getPropertyValue(mod.name),
          mod.priority || ""
        );
      } else if (mod.type === "remove" || mod.type === "disable") {
        this.rawStyle.removeProperty(mod.name);
      }
    }

    this.pageStyle.refreshObservedRules(this.ancestorRules);

    // Add processed modifications to the _pendingDeclarationChanges array,
    // they will be emitted as CSS_CHANGE resources once `declarations` have
    // been re-computed in `form`.
    this._pendingDeclarationChanges.push(...modifications);

    return this;
  }

  /**
   * Helper function for modifySelector, inserts the new
   * rule with the new selector into the parent style sheet and removes the
   * current rule. Returns the newly inserted css rule or null if the rule is
   * unsuccessfully inserted to the parent style sheet.
   *
   * @param {string} value
   *        The new selector value
   * @param {boolean} editAuthored
   *        True if the selector should be updated by editing the
   *        authored text; false if the selector should be updated via
   *        CSSOM.
   *
   * @returns {CSSRule}
   *        The new CSS rule added
   */
  async _addNewSelector(value, editAuthored) {
    const rule = this.rawRule;
    const parentStyleSheet = this._parentSheet;

    // We know the selector modification is ok, so if the client asked
    // for the authored text to be edited, do it now.
    if (editAuthored) {
      const document = this.getDocument(this._parentSheet);
      try {
        document.querySelector(value);
      } catch (e) {
        return null;
      }

      const resourceId =
        this.pageStyle.styleSheetsManager.getStyleSheetResourceId(
          this._parentSheet
        );
      let authoredText =
        await this.pageStyle.styleSheetsManager.getText(resourceId);

      const [startOffset, endOffset] = getSelectorOffsets(
        authoredText,
        this.line,
        this.column
      );
      authoredText =
        authoredText.substring(0, startOffset) +
        value +
        authoredText.substring(endOffset);

      await this.pageStyle.styleSheetsManager.setStyleSheetText(
        resourceId,
        authoredText,
        { kind: UPDATE_PRESERVING_RULES }
      );
    } else {
      // We retrieve the parent of the rule, which can be a regular stylesheet, but also
      // another rule, in case the underlying rule is nested.
      // If the rule is nested in another rule, we need to use its parent rule to "edit" it.
      // If the rule has no parent rules, we can simply use the stylesheet.
      const parent = this.rawRule.parentRule || parentStyleSheet;
      const cssRules = parent.cssRules;
      const cssText = rule.cssText;
      const selectorText = rule.selectorText;

      for (let i = 0; i < cssRules.length; i++) {
        if (rule === cssRules.item(i)) {
          try {
            // Inserts the new style rule into the current style sheet and
            // delete the current rule
            const ruleText = cssText.slice(selectorText.length).trim();
            parent.insertRule(value + " " + ruleText, i);
            parent.deleteRule(i + 1);
            break;
          } catch (e) {
            // The selector could be invalid, or the rule could fail to insert.
            return null;
          }
        }
      }
    }

    await this.updateAncestorRulesAuthoredText();

    return this._getRuleFromIndex(parentStyleSheet);
  }

  /**
   * Take an object with instructions to modify a CSS declaration and log an object with
   * normalized metadata which describes the change in the context of this rule.
   *
   * @param {object} change
   *        Data about a modification to a declaration. @see |modifyProperties()|
   * @param {object} newDeclarations
   *        The current declarations array to get the latest values, names...
   * @param {object} oldDeclarations
   *        The previous declarations array to use to fetch old values, names...
   */
  logDeclarationChange(change, newDeclarations, oldDeclarations) {
    // Position of the declaration within its rule.
    const index = change.index;
    // Destructure properties from the previous CSS declaration at this index, if any,
    // to new variable names to indicate the previous state.
    let {
      value: prevValue,
      name: prevName,
      priority: prevPriority,
      commentOffsets,
    } = oldDeclarations[index] || {};

    const { value: currentValue, name: currentName } =
      newDeclarations[index] || {};
    // A declaration is disabled if it has a `commentOffsets` array.
    // Here we type coerce the value to a boolean with double-bang (!!)
    const prevDisabled = !!commentOffsets;
    // Append the "!important" string if defined in the previous priority flag.
    prevValue =
      prevValue && prevPriority ? `${prevValue} !important` : prevValue;

    const data = this.metadata;

    switch (change.type) {
      case "set": {
        data.type = prevValue ? "declaration-add" : "declaration-update";
        // If `change.newName` is defined, use it because the property is being renamed.
        // Otherwise, a new declaration is being created or the value of an existing
        // declaration is being updated. In that case, use the currentName computed
        // by the engine.
        const changeName = currentName || change.name;
        const name = change.newName ? change.newName : changeName;
        // Append the "!important" string if defined in the incoming priority flag.

        const changeValue = currentValue || change.value;
        const newValue = change.priority
          ? `${changeValue} !important`
          : changeValue;

        // Reuse the previous value string, when the property is renamed.
        // Otherwise, use the incoming value string.
        const value = change.newName ? prevValue : newValue;

        data.add = [{ property: name, value, index }];
        // If there is a previous value, log its removal together with the previous
        // property name. Using the previous name handles the case for renaming a property
        // and is harmless when updating an existing value (the name stays the same).
        if (prevValue) {
          data.remove = [{ property: prevName, value: prevValue, index }];
        } else {
          data.remove = null;
        }

        // When toggling a declaration from OFF to ON, if not renaming the property,
        // do not mark the previous declaration for removal, otherwise the add and
        // remove operations will cancel each other out when tracked. Tracked changes
        // have no context of "disabled", only "add" or remove, like diffs.
        if (prevDisabled && !change.newName && prevValue === newValue) {
          data.remove = null;
        }

        break;
      }

      case "remove":
        data.type = "declaration-remove";
        data.add = null;
        data.remove = [{ property: change.name, value: prevValue, index }];
        break;

      case "disable":
        data.type = "declaration-disable";
        data.add = null;
        data.remove = [{ property: change.name, value: prevValue, index }];
        break;
    }

    this.pageStyle.inspector.targetActor.emit("track-css-change", data);
  }

  /**
   * Helper method for tracking CSS changes. Logs the change of this rule's selector as
   * two operations: a removal using the old selector and an addition using the new one.
   *
   * @param {string} oldSelector
   *        This rule's previous selector.
   * @param {string} newSelector
   *        This rule's new selector.
   */
  logSelectorChange(oldSelector, newSelector) {
    this.pageStyle.inspector.targetActor.emit("track-css-change", {
      ...this.metadata,
      type: "selector-remove",
      add: null,
      remove: null,
      selector: oldSelector,
    });

    this.pageStyle.inspector.targetActor.emit("track-css-change", {
      ...this.metadata,
      type: "selector-add",
      add: null,
      remove: null,
      selector: newSelector,
    });
  }

  /**
   * Modify the current rule's selector by inserting a new rule with the new
   * selector value and removing the current rule.
   *
   * Returns information about the new rule and applied style
   * so that consumers can immediately display the new rule, whether or not the
   * selector matches the current element without having to refresh the whole
   * list.
   *
   * @param {DOMNode} node
   *        The current selected element
   * @param {string} value
   *        The new selector value
   * @param {boolean} editAuthored
   *        True if the selector should be updated by editing the
   *        authored text; false if the selector should be updated via
   *        CSSOM.
   * @returns {Promise<object>}
   *        Returns an object that contains the applied style properties of the
   *        new rule and a boolean indicating whether or not the new selector
   *        matches the current selected element
   */
  async modifySelector(node, value, editAuthored = false) {
    if (this.type === ELEMENT_STYLE || this.rawRule.selectorText === value) {
      return { ruleProps: null, isMatching: true };
    }

    // The rule's previous selector is lost after calling _addNewSelector(). Save it now.
    const oldValue = this.rawRule.selectorText;
    const newCssRule = await this._addNewSelector(value, editAuthored);

    if (editAuthored && newCssRule) {
      this.logSelectorChange(oldValue, value);
      const style = this.pageStyle.styleRef(newCssRule);
      // See the comment in |form| to understand this.
      await style.getAuthoredCssText();
    }

    let entries = null;
    let isMatching = false;

    if (newCssRule) {
      const ruleEntry = this.pageStyle.findEntryMatchingRule(node, newCssRule);
      if (ruleEntry) {
        entries = this.pageStyle.getAppliedProps(node, [ruleEntry], {
          matchedSelectors: true,
        });
      } else {
        entries = this.pageStyle.getNewAppliedProps(node, newCssRule);
      }

      isMatching = entries.some(
        ruleProp => !!ruleProp.matchedSelectorIndexes.length
      );
    }

    const result = { isMatching };
    if (entries) {
      result.ruleProps = { entries };
    }

    return result;
  }

  /**
   * Get the eligible query container for a given @container rule and a given node
   *
   * @param {number} ancestorRuleIndex: The index of the @container rule in this.ancestorRules
   * @param {NodeActor} nodeActor: The nodeActor for which we want to retrieve the query container
   * @param {number} conditionIndex: The index of <container-condition> in the @container rule
   * @returns {object} An object with the following properties:
   *          - node: {NodeActor|null} The nodeActor representing the query container,
   *            null if none were found
   *          - containerName: {string} The computed `containerType` value of the query container
   *            when there's one, or the rule's condition `name`.
   *          - containerType: {string} The computed `containerType` value of the query container
   *          - queryFeatures: {array<object>} An array of the properties used in the
   *            query and their values.
   */
  getQueryContainerForNode(ancestorRuleIndex, nodeActor, conditionIndex) {
    const ancestorRule = this.ancestorRules[ancestorRuleIndex];
    if (!ancestorRule) {
      console.error(
        `Couldn't not find an ancestor rule at index ${ancestorRuleIndex}`
      );
      return { node: null };
    }

    const containerEl = ancestorRule.rawRule.queryContainerFor(
      nodeActor.rawNode,
      conditionIndex
    );
    const condition = ancestorRule.rawRule.conditions[conditionIndex];

    // queryContainerFor returns null when the container name wasn't find in any ancestor,
    // which can happen for rules with multiple conditions.
    if (!containerEl) {
      return {
        node: null,
        containerName: condition?.name,
      };
    }

    const computedStyle = CssLogic.getComputedStyle(containerEl);

    return {
      node: this.pageStyle.walker.getNode(containerEl),
      containerType: computedStyle.containerType,
      containerName:
        computedStyle.containerName !== "none"
          ? computedStyle.containerName
          : null,
      queryFeatures: this.#getFeaturesForContainerQueryConditions({
        condition,
        computedStyle,
        containerEl,
      }),
    };
  }

  /**
   * Get the different features (i.e. the properties used in the condition) of a given
   * container query condition.
   *
   * @param {object} param
   * @param {object} param.condition
   * @param {object} param.computedStyle
   * @param {Element} param.containerEl
   * @returns Array<object>
   */
  #getFeaturesForContainerQueryConditions({
    condition,
    computedStyle,
    containerEl,
  }) {
    const queryFeatures = [];
    // We only want to send unique type/name/value entries (e.g. if a given variable is
    // mentioned twice, we should only send it once).
    const addQueryFeature = (type, name, value) => {
      if (
        queryFeatures.some(
          // we don't need to check for `value`, as type/name should represent a unique
          // attribute/variable/property , and we'll always get the same value for a
          // given (type|name) pair.
          feature => feature.name === name && feature.type === type
        )
      ) {
        return;
      }
      queryFeatures.push({ type, name, value });
    };
    const parser = new InspectorCSSParser(condition.query);
    let token;
    const stack = [];
    while ((token = parser.nextToken())) {
      const lastStack = stack.at(-1);
      if (token.tokenType === "Function") {
        stack.push({
          tokenType: token.tokenType,
          functionName: token.value.toLowerCase(),
        });
        continue;
      }
      if (token.tokenType === "ParenthesisBlock") {
        stack.push({ tokenType: token.tokenType });
        continue;
      }
      if (token.tokenType === "CloseParenthesis") {
        stack.pop();
        continue;
      }

      if (token.tokenType === "Ident") {
        let ident = token.text;
        if (ident === "and" || ident === "or" || ident === "not") {
          continue;
        }

        let propertyValue = computedStyle.getPropertyValue(ident);

        // if we're in style() or var() and the ident starts with "--", it's most likely
        // a reference to a CSS variable, so let's get its value
        if (
          lastStack.tokenType === "Function" &&
          (lastStack.functionName === "style" ||
            lastStack.functionName === "var") &&
          ident.startsWith("--")
        ) {
          // the variable name is the first ident after the function token
          if (!lastStack.varNameFound) {
            // we want to return the ident if the variable is not defined, so we can display
            // specific text for it
            addQueryFeature(
              "var",
              ident,
              // Use computedStyle.hasLonghandProperty(ident), so we can return an empty
              // string for empty variables (e.g. `--x: ;`), and null when the variables isn't set.
              computedStyle.hasLonghandProperty(ident) ? propertyValue : null
            );
            lastStack.varNameFound = true;
          }
          continue;
        }
        if (
          lastStack.tokenType === "Function" &&
          lastStack.functionName === "attr"
        ) {
          // the attribute name is the first ident after the function token
          if (!lastStack.attrNameFound) {
            // we want to return the attribute if it's not defined, so we can display
            // specific text for it
            addQueryFeature("attr", ident, containerEl.getAttribute(ident));
            lastStack.attrNameFound = true;
          }
          continue;
        }
        if (lastStack.tokenType === "ParenthesisBlock") {
          // if the value for the ident wasn't found, we may have some legacy syntax
          // features (e.g. `max-height: 100px`, `min-width: 800px`)
          if (!propertyValue) {
            // Try to remove the leading min/max so we get the actual property name
            ident = ident.replace(/(min|max)-/, "");
            propertyValue = computedStyle.getPropertyValue(ident);
          }

          // If we couldn't still get the value, don't include anything for that ident
          if (!propertyValue) {
            continue;
          }

          // if we're not directly in a function, we might have regular size features,
          // e.g. (inline-size > 1px and height < 80px). Let's return the value for those.
          addQueryFeature("size", ident, propertyValue);
        }
      }
    }

    return queryFeatures;
  }

  /**
   * Using the latest computed style applicable to the selected element,
   * check the states of declarations in this CSS rule.
   *
   * If any have changed their used/unused state, potentially as a result of changes in
   * another rule, fire a "rule-updated" event with this rule actor in its latest state.
   *
   * @param {boolean} forceRefresh: Set to true to emit "rule-updated", even if the state
   *        of the declarations didn't change.
   */
  maybeRefresh(forceRefresh) {
    let hasChanged = false;

    const el = this.currentlySelectedElement;
    const style = this.currentlySelectedElementComputedStyle;

    for (const decl of this._declarations) {
      const inactiveCssData = getInactiveCssDataForProperty(
        el,
        style,
        this.rawRule,
        decl.name
      );

      if (decl.inactiveCssData?.msgId !== inactiveCssData?.msgId) {
        if (inactiveCssData) {
          decl.inactiveCssData = inactiveCssData;
        } else {
          delete decl.inactiveCssData;
        }
        hasChanged = true;
      }
    }

    if (hasChanged || forceRefresh) {
      // ⚠️ IMPORTANT ⚠️
      // When an event is emitted via the protocol with the StyleRuleActor as payload, the
      // corresponding StyleRuleFront will be automatically updated under the hood.
      // Therefore, when the client looks up properties on the front reference it already
      // has, it will get the latest values set on the actor, not the ones it originally
      // had when the front was created. The client is not required to explicitly replace
      // its previous front reference to the one it receives as this event's payload.
      // The client doesn't even need to explicitly listen for this event.
      // The update of the front happens automatically.
      this.emit("rule-updated", this);
    }
  }

  /**
   * Get the computation steps from a given expression to its final "computed" value.
   * e.g. `max(0, min(20, 100))` should return:
   * [
   *   `max(0, min(20, 100))`,
   *   `max(0, 20)`,
   *   `20`,
   * ]
   *
   * @param {string} property: The CSS property the expression is applied to
   * @param {string} expression: The CSS expression to be explained
   * @param {string} pseudo: An optional pseudo-element type in cases when the CSS
   *        rule applies to a pseudo-element.
   * @param {NodeActor} inheritedNode: An optional node the expression is applied to.
   *        If not passed, this.currentlySelectedElement will be used instead.
   * @returns Array<string>
   */
  getCssExplainersData(property, expression, pseudo, inheritedNode) {
    let element = inheritedNode?.rawNode || this.currentlySelectedElement;
    // If we have a pseudo element, we want to pass its binding element
    // to the InspectorUtils method
    if (element.implementedPseudoElement) {
      element =
        SharedCssLogic.getBindingElementAndPseudo(element).bindingElement;
    }
    return InspectorUtils.getComputationSteps(
      property,
      expression,
      element,
      pseudo
    );
  }
}
exports.StyleRuleActor = StyleRuleActor;

/**
 * Compute the start and end offsets of a rule's selector text, given
 * the CSS text and the line and column at which the rule begins.
 *
 * @param {string} initialText
 * @param {number} line (1-indexed)
 * @param {number} column (1-indexed)
 * @return {Array} An array with two elements: [startOffset, endOffset].
 *                 The elements mark the bounds in |initialText| of
 *                 the CSS rule's selector.
 */
function getSelectorOffsets(initialText, line, column) {
  if (typeof line === "undefined" || typeof column === "undefined") {
    throw new Error("Location information is missing");
  }

  const { offset: textOffset, text } = getTextAtLineColumn(
    initialText,
    line,
    column
  );
  const lexer = new InspectorCSSParserWrapper(text);

  // Search forward for the opening brace.
  let endOffset;
  let token;
  while ((token = lexer.nextToken())) {
    if (token.tokenType === "CurlyBracketBlock") {
      if (endOffset === undefined) {
        break;
      }
      return [textOffset, textOffset + endOffset];
    }
    // Preserve comments and whitespace just before the "{".
    if (token.tokenType !== "Comment" && token.tokenType !== "WhiteSpace") {
      endOffset = token.endOffset;
    }
  }

  throw new Error("could not find bounds of rule");
}
