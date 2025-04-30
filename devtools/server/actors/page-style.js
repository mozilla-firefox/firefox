/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { Actor } = require("resource://devtools/shared/protocol.js");
const {
  pageStyleSpec,
} = require("resource://devtools/shared/specs/page-style.js");

const {
  LongStringActor,
} = require("resource://devtools/server/actors/string.js");
const TrackChangeEmitter = require("resource://devtools/server/actors/utils/track-change-emitter.js");

const {
  style: { ELEMENT_STYLE },
} = require("resource://devtools/shared/constants.js");

loader.lazyRequireGetter(
  this,
  "StyleRuleActor",
  "resource://devtools/server/actors/style-rule.js",
  true
);
loader.lazyRequireGetter(
  this,
  "getFontPreviewData",
  "resource://devtools/server/actors/utils/style-utils.js",
  true
);
loader.lazyRequireGetter(
  this,
  "CssLogic",
  "resource://devtools/server/actors/inspector/css-logic.js",
  true
);
loader.lazyRequireGetter(
  this,
  "SharedCssLogic",
  "resource://devtools/shared/inspector/css-logic.js"
);
loader.lazyRequireGetter(
  this,
  "getDefinedGeometryProperties",
  "resource://devtools/server/actors/highlighters/geometry-editor.js",
  true
);
loader.lazyRequireGetter(
  this,
  "UPDATE_GENERAL",
  "resource://devtools/server/actors/utils/stylesheets-manager.js",
  true
);

loader.lazyGetter(this, "PSEUDO_ELEMENTS", () => {
  return InspectorUtils.getCSSPseudoElementNames();
});
loader.lazyGetter(this, "FONT_VARIATIONS_ENABLED", () => {
  return Services.prefs.getBoolPref("layout.css.font-variations.enabled");
});

const NORMAL_FONT_WEIGHT = 400;
const BOLD_FONT_WEIGHT = 700;

/**
 * The PageStyle actor lets the client look at the styles on a page, as
 * they are applied to a given node.
 */
class PageStyleActor extends Actor {
  /**
   * Create a PageStyleActor.
   *
   * @param inspector
   *    The InspectorActor that owns this PageStyleActor.
   *
   * @constructor
   */
  constructor(inspector) {
    super(inspector.conn, pageStyleSpec);
    this.inspector = inspector;
    if (!this.inspector.walker) {
      throw Error(
        "The inspector's WalkerActor must be created before " +
          "creating a PageStyleActor."
      );
    }
    this.walker = inspector.walker;
    this.cssLogic = new CssLogic();

    // Stores the association of DOM objects -> actors
    this.refMap = new Map();

    // Latest node queried for its applied styles.
    this.selectedElement = null;

    // Maps document elements to style elements, used to add new rules.
    this.styleElements = new WeakMap();

    this.onFrameUnload = this.onFrameUnload.bind(this);

    this.inspector.targetActor.on("will-navigate", this.onFrameUnload);

    this._observedRules = [];
    this._styleApplied = this._styleApplied.bind(this);

    this.styleSheetsManager =
      this.inspector.targetActor.getStyleSheetsManager();

    this._onStylesheetUpdated = this._onStylesheetUpdated.bind(this);
    this.styleSheetsManager.on("stylesheet-updated", this._onStylesheetUpdated);
  }

  destroy() {
    if (!this.walker) {
      return;
    }
    super.destroy();
    this.inspector.targetActor.off("will-navigate", this.onFrameUnload);
    this.inspector = null;
    this.walker = null;
    this.refMap = null;
    this.selectedElement = null;
    this.cssLogic = null;
    this.styleElements = null;

    this._observedRules = [];
  }

  get ownerWindow() {
    return this.inspector.targetActor.window;
  }

  form() {
    // We need to use CSS from the inspected window in order to use CSS.supports() and
    // detect the right platform features from there.
    const CSS = this.inspector.targetActor.window.CSS;

    return {
      actor: this.actorID,
      traits: {
        // Whether the page supports values of font-stretch from CSS Fonts Level 4.
        fontStretchLevel4: CSS.supports("font-stretch: 100%"),
        // Whether the page supports values of font-style from CSS Fonts Level 4.
        fontStyleLevel4: CSS.supports("font-style: oblique 20deg"),
        // Whether getAllUsedFontFaces/getUsedFontFaces accepts the includeVariations
        // argument.
        fontVariations: FONT_VARIATIONS_ENABLED,
        // Whether the page supports values of font-weight from CSS Fonts Level 4.
        // font-weight at CSS Fonts Level 4 accepts values in increments of 1 rather
        // than 100. However, CSS.supports() returns false positives, so we guard with the
        // expected support of font-stretch at CSS Fonts Level 4.
        fontWeightLevel4:
          CSS.supports("font-weight: 1") && CSS.supports("font-stretch: 100%"),
      },
    };
  }

  /**
   * Called when a style sheet is updated.
   */
  _styleApplied(kind) {
    // No matter what kind of update is done, we need to invalidate
    // the keyframe cache.
    this.cssLogic.reset();
    if (kind === UPDATE_GENERAL) {
      this.emit("stylesheet-updated");
    }
  }

  /**
   * Return or create a StyleRuleActor for the given item.
   *
   * @param {CSSStyleRule|Element} item
   * @param {String} pseudoElement An optional pseudo-element type in cases when the CSS
   *        rule applies to a pseudo-element.
   * @param {Boolean} userAdded: Optional boolean to distinguish rules added by the user.
   * @return {StyleRuleActor} The newly created, or cached, StyleRuleActor for this item.
   */
  _styleRef(item, pseudoElement, userAdded = false) {
    if (this.refMap.has(item)) {
      return this.refMap.get(item);
    }
    const actor = new StyleRuleActor({
      pageStyle: this,
      item,
      userAdded,
      pseudoElement,
    });
    this.manage(actor);
    this.refMap.set(item, actor);

    return actor;
  }

  /**
   * Update the association between a StyleRuleActor and its
   * corresponding item.  This is used when a StyleRuleActor updates
   * as style sheet and starts using a new rule.
   *
   * @param oldItem The old association; either a CSSStyleRule or a
   *                DOM element.
   * @param item Either a CSSStyleRule or a DOM element.
   * @param actor a StyleRuleActor
   */
  updateStyleRef(oldItem, item, actor) {
    this.refMap.delete(oldItem);
    this.refMap.set(item, actor);
  }

  /**
   * Get the StyleRuleActor matching the given rule id or null if no match is found.
   *
   * @param  {String} ruleId
   *         Actor ID of the StyleRuleActor
   * @return {StyleRuleActor|null}
   */
  getRule(ruleId) {
    let match = null;

    for (const actor of this.refMap.values()) {
      if (actor.actorID === ruleId) {
        match = actor;
        continue;
      }
    }

    return match;
  }

  /**
   * Get the computed style for a node.
   *
   * @param NodeActor node
   * @param object options
   *   `filter`: A string filter that affects the "matched" handling.
   *     'user': Include properties from user style sheets.
   *     'ua': Include properties from user and user-agent sheets.
   *     Default value is 'ua'
   *   `markMatched`: true if you want the 'matched' property to be added
   *     when a computed property has been modified by a style included
   *     by `filter`.
   *   `onlyMatched`: true if unmatched properties shouldn't be included.
   *   `filterProperties`: An array of properties names that you would like
   *     returned.
   *
   * @returns a JSON blob with the following form:
   *   {
   *     "property-name": {
   *       value: "property-value",
   *       priority: "!important" <optional>
   *       matched: <true if there are matched selectors for this value>
   *     },
   *     ...
   *   }
   */
  getComputed(node, options) {
    const ret = Object.create(null);

    const filterProperties = Array.isArray(options.filterProperties)
      ? options.filterProperties
      : null;
    this.cssLogic.sourceFilter = options.filter || SharedCssLogic.FILTER.UA;
    this.cssLogic.highlight(node.rawNode);
    const computed = this.cssLogic.computedStyle || [];
    const targetDocument = this.inspector.targetActor.window.document;

    for (const name of computed) {
      if (filterProperties && !filterProperties.includes(name)) {
        continue;
      }
      ret[name] = {
        value: computed.getPropertyValue(name),
        priority: computed.getPropertyPriority(name) || undefined,
      };

      if (name.startsWith("--")) {
        const registeredProperty = InspectorUtils.getCSSRegisteredProperty(
          targetDocument,
          name
        );
        if (registeredProperty) {
          ret[name].registeredPropertyInitialValue =
            registeredProperty.initialValue;
          if (
            !InspectorUtils.valueMatchesSyntax(
              targetDocument,
              ret[name].value,
              registeredProperty.syntax
            )
          ) {
            ret[name].invalidAtComputedValueTime = true;
            ret[name].registeredPropertySyntax = registeredProperty.syntax;
          }
        }
      }
    }

    if (options.markMatched || options.onlyMatched) {
      const matched = this.cssLogic.hasMatchedSelectors(Object.keys(ret));
      for (const key in ret) {
        if (matched.has(key)) {
          ret[key].matched = options.markMatched ? true : undefined;
        } else if (options.onlyMatched) {
          delete ret[key];
        }
      }
    }

    return ret;
  }

  /**
   * Get all the fonts from a page.
   *
   * @param object options
   *   `includePreviews`: Whether to also return image previews of the fonts.
   *   `previewText`: The text to display in the previews.
   *   `previewFontSize`: The font size of the text in the previews.
   *
   * @returns object
   *   object with 'fontFaces', a list of fonts that apply to this node.
   */
  getAllUsedFontFaces(options) {
    const windows = this.inspector.targetActor.windows;
    let fontsList = [];
    for (const win of windows) {
      // Fall back to the documentElement for XUL documents.
      const node = win.document.body
        ? win.document.body
        : win.document.documentElement;
      fontsList = [...fontsList, ...this.getUsedFontFaces(node, options)];
    }

    return fontsList;
  }

  /**
   * Get the font faces used in an element.
   *
   * @param NodeActor node / actual DOM node
   *    The node to get fonts from.
   * @param object options
   *   `includePreviews`: Whether to also return image previews of the fonts.
   *   `previewText`: The text to display in the previews.
   *   `previewFontSize`: The font size of the text in the previews.
   *
   * @returns object
   *   object with 'fontFaces', a list of fonts that apply to this node.
   */
  getUsedFontFaces(node, options) {
    // node.rawNode is defined for NodeActor objects
    const actualNode = node.rawNode || node;
    const contentDocument = actualNode.ownerDocument;
    // We don't get fonts for a node, but for a range
    const rng = contentDocument.createRange();
    const isPseudoElement = Boolean(
      CssLogic.getBindingElementAndPseudo(actualNode).pseudo
    );
    if (isPseudoElement) {
      rng.selectNodeContents(actualNode);
    } else {
      rng.selectNode(actualNode);
    }
    const fonts = InspectorUtils.getUsedFontFaces(rng);
    const fontsArray = [];

    for (let i = 0; i < fonts.length; i++) {
      const font = fonts[i];
      const fontFace = {
        name: font.name,
        CSSFamilyName: font.CSSFamilyName,
        CSSGeneric: font.CSSGeneric || null,
        srcIndex: font.srcIndex,
        URI: font.URI,
        format: font.format,
        localName: font.localName,
        metadata: font.metadata,
        version: font.getNameString(InspectorFontFace.NAME_ID_VERSION),
        description: font.getNameString(InspectorFontFace.NAME_ID_DESCRIPTION),
        manufacturer: font.getNameString(
          InspectorFontFace.NAME_ID_MANUFACTURER
        ),
        vendorUrl: font.getNameString(InspectorFontFace.NAME_ID_VENDOR_URL),
        designer: font.getNameString(InspectorFontFace.NAME_ID_DESIGNER),
        designerUrl: font.getNameString(InspectorFontFace.NAME_ID_DESIGNER_URL),
        license: font.getNameString(InspectorFontFace.NAME_ID_LICENSE),
        licenseUrl: font.getNameString(InspectorFontFace.NAME_ID_LICENSE_URL),
        sampleText: font.getNameString(InspectorFontFace.NAME_ID_SAMPLE_TEXT),
      };

      // If this font comes from a @font-face rule
      if (font.rule) {
        const styleActor = new StyleRuleActor({
          pageStyle: this,
          item: font.rule,
        });
        this.manage(styleActor);
        fontFace.rule = styleActor;
        fontFace.ruleText = font.rule.cssText;
      }

      // Get the weight and style of this font for the preview and sort order
      let weight = NORMAL_FONT_WEIGHT,
        style = "";
      if (font.rule) {
        weight =
          font.rule.style.getPropertyValue("font-weight") || NORMAL_FONT_WEIGHT;
        if (weight == "bold") {
          weight = BOLD_FONT_WEIGHT;
        } else if (weight == "normal") {
          weight = NORMAL_FONT_WEIGHT;
        }
        style = font.rule.style.getPropertyValue("font-style") || "";
      }
      fontFace.weight = weight;
      fontFace.style = style;

      if (options.includePreviews) {
        const opts = {
          previewText: options.previewText,
          previewFontSize: options.previewFontSize,
          fontStyle: style,
          fontWeight: weight,
          fillStyle: options.previewFillStyle,
        };
        const { dataURL, size } = getFontPreviewData(
          font.CSSFamilyName,
          contentDocument,
          opts
        );
        fontFace.preview = {
          data: new LongStringActor(this.conn, dataURL),
          size,
        };
      }

      if (options.includeVariations && FONT_VARIATIONS_ENABLED) {
        fontFace.variationAxes = font.getVariationAxes();
        fontFace.variationInstances = font.getVariationInstances();
      }

      fontsArray.push(fontFace);
    }

    // @font-face fonts at the top, then alphabetically, then by weight
    fontsArray.sort(function (a, b) {
      return a.weight > b.weight ? 1 : -1;
    });
    fontsArray.sort(function (a, b) {
      if (a.CSSFamilyName == b.CSSFamilyName) {
        return 0;
      }
      return a.CSSFamilyName > b.CSSFamilyName ? 1 : -1;
    });
    fontsArray.sort(function (a, b) {
      if ((a.rule && b.rule) || (!a.rule && !b.rule)) {
        return 0;
      }
      return !a.rule && b.rule ? 1 : -1;
    });

    return fontsArray;
  }

  /**
   * Get a list of selectors that match a given property for a node.
   *
   * @param NodeActor node
   * @param string property
   * @param object options
   *   `filter`: A string filter that affects the "matched" handling.
   *     'user': Include properties from user style sheets.
   *     'ua': Include properties from user and user-agent sheets.
   *     Default value is 'ua'
   *
   * @returns a JSON object with the following form:
   *   {
   *     // An ordered list of rules that apply
   *     matched: [{
   *       rule: <rule actorid>,
   *       sourceText: <string>, // The source of the selector, relative
   *                             // to the node in question.
   *       selector: <string>, // the selector ID that matched
   *       value: <string>, // the value of the property
   *       status: <int>,
   *         // The status of the match - high numbers are better placed
   *         // to provide styling information:
   *         // 3: Best match, was used.
   *         // 2: Matched, but was overridden.
   *         // 1: Rule from a parent matched.
   *         // 0: Unmatched (never returned in this API)
   *     }, ...],
   *
   *     // The full form of any domrule referenced.
   *     rules: [ <domrule>, ... ], // The full form of any domrule referenced
   *
   *     // The full form of any sheets referenced.
   *     sheets: [ <domsheet>, ... ]
   *  }
   */
  getMatchedSelectors(node, property, options) {
    this.cssLogic.sourceFilter = options.filter || SharedCssLogic.FILTER.UA;
    this.cssLogic.highlight(node.rawNode);

    const rules = new Set();
    const matched = [];

    const targetDocument = this.inspector.targetActor.window.document;
    let registeredProperty;
    if (property.startsWith("--")) {
      registeredProperty = InspectorUtils.getCSSRegisteredProperty(
        targetDocument,
        property
      );
    }

    const propInfo = this.cssLogic.getPropertyInfo(property);
    for (const selectorInfo of propInfo.matchedSelectors) {
      const cssRule = selectorInfo.selector.cssRule;
      const domRule = cssRule.sourceElement || cssRule.domRule;

      const rule = this._styleRef(domRule);
      rules.add(rule);

      const match = {
        rule,
        sourceText: this.getSelectorSource(selectorInfo, node.rawNode),
        selector: selectorInfo.selector.text,
        name: selectorInfo.property,
        value: selectorInfo.value,
        status: selectorInfo.status,
      };
      if (
        registeredProperty &&
        !InspectorUtils.valueMatchesSyntax(
          targetDocument,
          match.value,
          registeredProperty.syntax
        )
      ) {
        match.invalidAtComputedValueTime = true;
        match.registeredPropertySyntax = registeredProperty.syntax;
      }
      matched.push(match);
    }

    return {
      matched,
      rules: [...rules],
    };
  }

  // Get a selector source for a CssSelectorInfo relative to a given
  // node.
  getSelectorSource(selectorInfo, relativeTo) {
    let result = selectorInfo.selector.text;
    if (selectorInfo.inlineStyle) {
      const source = selectorInfo.sourceElement;
      if (source === relativeTo) {
        result = "this";
      } else {
        result = CssLogic.getShortName(source);
      }
      result += ".style";
    }
    return result;
  }

  /**
   * Get the set of styles that apply to a given node.
   * @param NodeActor node
   * @param object options
   *   `filter`: A string filter that affects the "matched" handling.
   *     'user': Include properties from user style sheets.
   *     'ua': Include properties from user and user-agent sheets.
   *     Default value is 'ua'
   *   `inherited`: Include styles inherited from parent nodes.
   *   `matchedSelectors`: Include an array of specific selectors that
   *     caused this rule to match its node.
   *   `skipPseudo`: Exclude styles applied to pseudo elements of the provided node.
   */
  async getApplied(node, options) {
    // Clear any previous references to StyleRuleActor instances for CSS rules.
    // Assume the consumer has switched context to a new node and no longer
    // interested in state changes of previous rules.
    this._observedRules = [];
    this.selectedElement = node.rawNode;

    if (!node) {
      return { entries: [] };
    }

    this.cssLogic.highlight(node.rawNode);

    const entries = this.getAppliedProps(
      node,
      this._getAllElementRules(node, undefined, options),
      options
    );

    const entryRules = new Set();
    entries.forEach(entry => {
      entryRules.add(entry.rule);
    });

    await Promise.all(entries.map(entry => entry.rule.getAuthoredCssText()));

    // Reference to instances of StyleRuleActor for CSS rules matching the node.
    // Assume these are used by a consumer which wants to be notified when their
    // state or declarations change either directly or indirectly.
    this._observedRules = entryRules;

    return { entries };
  }

  _hasInheritedProps(style) {
    const doc = this.inspector.targetActor.window.document;
    return Array.prototype.some.call(style, prop =>
      InspectorUtils.isInheritedProperty(doc, prop)
    );
  }

  async isPositionEditable(node) {
    if (!node || node.rawNode.nodeType !== node.rawNode.ELEMENT_NODE) {
      return false;
    }

    const props = getDefinedGeometryProperties(node.rawNode);

    // Elements with only `width` and `height` are currently not considered
    // editable.
    return (
      props.has("top") ||
      props.has("right") ||
      props.has("left") ||
      props.has("bottom")
    );
  }

  /**
   * Helper function for getApplied, gets all the rules from a given
   * element. See getApplied for documentation on parameters.
   * @param NodeActor node
   * @param bool inherited
   * @param object options

   * @return Array The rules for a given element. Each item in the
   *               array has the following signature:
   *                - rule RuleActor
   *                - isSystem Boolean
   *                - inherited Boolean
   *                - pseudoElement String
   *                - darkColorScheme Boolean
   */
  _getAllElementRules(node, inherited, options) {
    const { bindingElement, pseudo } = CssLogic.getBindingElementAndPseudo(
      node.rawNode
    );
    const rules = [];

    if (!bindingElement || !bindingElement.style) {
      return rules;
    }

    const elementStyle = this._styleRef(
      bindingElement,
      // for inline style, we can't have a related pseudo element
      null
    );
    const showElementStyles = !inherited && !pseudo;
    const showInheritedStyles =
      inherited && this._hasInheritedProps(bindingElement.style);

    const rule = this._getRuleItem(elementStyle, node.rawNode, {
      pseudoElement: null,
      isSystem: false,
      inherited: false,
    });

    // First any inline styles
    if (showElementStyles) {
      rules.push(rule);
    }

    // Now any inherited styles
    if (showInheritedStyles) {
      rule.inherited = inherited;
      rules.push(rule);
    }

    // Add normal rules.  Typically this is passing in the node passed into the
    // function, unless if that node was ::before/::after.  In which case,
    // it will pass in the parentNode along with "::before"/"::after".
    this._getElementRules(bindingElement, pseudo, inherited, options).forEach(
      oneRule => {
        // The only case when there would be a pseudo here is
        // ::before/::after, and in this case we want to tell the
        // view that it belongs to the element (which is a
        // _moz_generated_content native anonymous element).
        oneRule.pseudoElement = null;
        rules.push(oneRule);
      }
    );

    // Now any pseudos.
    if (!pseudo && !options.skipPseudo) {
      const relevantPseudoElements = [];
      for (const readPseudo of PSEUDO_ELEMENTS) {
        if (!this._pseudoIsRelevant(bindingElement, readPseudo, inherited)) {
          continue;
        }

        // FIXME: Bug 1909173. Need to handle view transitions peudo-elements.
        if (readPseudo === "::highlight") {
          InspectorUtils.getRegisteredCssHighlights(
            this.inspector.targetActor.window.document,
            // only active
            true
          ).forEach(name => {
            relevantPseudoElements.push(`::highlight(${name})`);
          });
        } else {
          relevantPseudoElements.push(readPseudo);
        }
      }

      for (const readPseudo of relevantPseudoElements) {
        const pseudoRules = this._getElementRules(
          bindingElement,
          readPseudo,
          inherited,
          options
        );
        // inherited element backed pseudo element rules (e.g. `::details-content`) should
        // not be at the same "level" as rules inherited from the binding element (e.g. `<details>`),
        // so we need to put them before the "regular" rules.
        if (
          SharedCssLogic.ELEMENT_BACKED_PSEUDO_ELEMENTS.has(readPseudo) &&
          inherited
        ) {
          rules.unshift(...pseudoRules);
        } else {
          rules.push(...pseudoRules);
        }
      }
    }

    return rules;
  }

  /**
   * @param {DOMNode} rawNode
   * @param {StyleRuleActor} styleRuleActor
   * @param {Object} params
   * @param {Boolean} params.inherited
   * @param {Boolean} params.isSystem
   * @param {String|null} params.pseudoElement
   * @returns Object
   */
  _getRuleItem(rule, rawNode, { inherited, isSystem, pseudoElement }) {
    return {
      rule,
      pseudoElement,
      isSystem,
      inherited,
      // We can't compute the value for the whole document as the color scheme
      // can be set at the node level (e.g. with `color-scheme`)
      darkColorScheme: InspectorUtils.isUsedColorSchemeDark(rawNode),
    };
  }

  _nodeIsTextfieldLike(node) {
    if (node.nodeName == "TEXTAREA") {
      return true;
    }
    return (
      node.mozIsTextField &&
      (node.mozIsTextField(false) || node.type == "number")
    );
  }

  _nodeIsButtonLike(node) {
    if (node.nodeName == "BUTTON") {
      return true;
    }
    return (
      node.nodeName == "INPUT" &&
      ["submit", "color", "button"].includes(node.type)
    );
  }

  _nodeIsListItem(node) {
    const display = CssLogic.getComputedStyle(node).getPropertyValue("display");
    // This is written this way to handle `inline list-item` and such.
    return display.split(" ").includes("list-item");
  }

  // eslint-disable-next-line complexity
  _pseudoIsRelevant(node, pseudo, inherited = false) {
    switch (pseudo) {
      case "::after":
      case "::before":
      case "::first-letter":
      case "::first-line":
      case "::selection":
      case "::highlight":
      case "::target-text":
        return !inherited;
      case "::marker":
        return !inherited && this._nodeIsListItem(node);
      case "::backdrop":
        return !inherited && node.matches(":modal, :popover-open");
      case "::cue":
        return !inherited && node.nodeName == "VIDEO";
      case "::file-selector-button":
        return !inherited && node.nodeName == "INPUT" && node.type == "file";
      case "::details-content": {
        const isDetailsNode = node.nodeName == "DETAILS";
        if (!isDetailsNode) {
          return false;
        }

        if (!inherited) {
          return true;
        }

        // If we're getting rules on a parent element, we need to check if the selected
        // element is inside the ::details-content of node
        // We traverse the flattened parent tree until we find the <slot> that implements
        // the pseudo element, as it's easier to handle edge cases like nested <details>,
        // multiple <summary>, etc …
        let traversedNode = this.selectedElement;
        while (traversedNode) {
          if (
            // if we found the <slot> implementing the pseudo element
            traversedNode.implementedPseudoElement === "::details-content" &&
            // and its parent <details> element is the element we're evaluating
            traversedNode.flattenedTreeParentNode === node
          ) {
            // then include the ::details-content rules from that element
            return true;
          }
          // otherwise keep looking up the tree
          traversedNode = traversedNode.flattenedTreeParentNode;
        }

        return false;
      }
      case "::placeholder":
      case "::-moz-placeholder":
        return !inherited && this._nodeIsTextfieldLike(node);
      case "::-moz-focus-inner":
        return !inherited && this._nodeIsButtonLike(node);
      case "::-moz-meter-bar":
        return !inherited && node.nodeName == "METER";
      case "::-moz-progress-bar":
        return !inherited && node.nodeName == "PROGRESS";
      case "::-moz-color-swatch":
        return !inherited && node.nodeName == "INPUT" && node.type == "color";
      case "::-moz-range-progress":
      case "::-moz-range-thumb":
      case "::-moz-range-track":
      case "::slider-fill":
      case "::slider-thumb":
      case "::slider-track":
        return !inherited && node.nodeName == "INPUT" && node.type == "range";
      case "::view-transition":
      case "::view-transition-group":
      case "::view-transition-image-pair":
      case "::view-transition-old":
      case "::view-transition-new":
        // FIXME: Bug 1909173. Need to handle view transitions peudo-elements
        // for DevTools. For now we skip them.
        return false;
      default:
        console.error("Unhandled pseudo-element " + pseudo);
        return false;
    }
  }

  /**
   * Helper function for _getAllElementRules, returns the rules from a given
   * element. See getApplied for documentation on parameters.
   * @param DOMNode node
   * @param string pseudo
   * @param DOMNode inherited
   * @param object options
   *
   * @returns Array
   */
  _getElementRules(node, pseudo, inherited, options) {
    // we don't need to retrieve inherited starting style rules
    const includeStartingStyleRules = !inherited;
    const domRules = InspectorUtils.getMatchingCSSRules(
      node,
      pseudo,
      CssLogic.hasVisitedState(node),
      includeStartingStyleRules
    );

    if (!domRules) {
      return [];
    }

    const rules = [];

    const doc = this.inspector.targetActor.window.document;

    // getMatchingCSSRules returns ordered from least-specific to
    // most-specific.
    for (let i = domRules.length - 1; i >= 0; i--) {
      const domRule = domRules[i];

      const isSystem = SharedCssLogic.isAgentStylesheet(
        domRule.parentStyleSheet
      );

      if (isSystem && options.filter != SharedCssLogic.FILTER.UA) {
        continue;
      }

      if (inherited) {
        // Don't include inherited rules if none of its properties
        // are inheritable.
        const hasInherited = [...domRule.style].some(prop =>
          InspectorUtils.isInheritedProperty(doc, prop)
        );
        if (!hasInherited) {
          continue;
        }
      }

      const ruleActor = this._styleRef(domRule, pseudo);

      rules.push(
        this._getRuleItem(ruleActor, node, {
          inherited,
          isSystem,
          pseudoElement: pseudo,
        })
      );
    }
    return rules;
  }

  /**
   * Given a node and a CSS rule, walk up the DOM looking for a
   * matching element rule.  Return an array of all found entries, in
   * the form generated by _getAllElementRules.  Note that this will
   * always return an array of either zero or one element.
   *
   * @param {NodeActor} node the node
   * @param {CSSStyleRule} filterRule the rule to filter for
   * @return {Array} array of zero or one elements; if one, the element
   *                 is the entry as returned by _getAllElementRules.
   */
  findEntryMatchingRule(node, filterRule) {
    const options = { matchedSelectors: true, inherited: true };
    let entries = [];
    let parent = this.walker.parentNode(node);
    while (parent && parent.rawNode.nodeType != Node.DOCUMENT_NODE) {
      entries = entries.concat(
        this._getAllElementRules(parent, parent, options)
      );
      parent = this.walker.parentNode(parent);
    }

    return entries.filter(entry => entry.rule.rawRule === filterRule);
  }

  /**
   * Helper function for getApplied that fetches a set of style properties that
   * apply to the given node and associated rules
   * @param NodeActor node
   * @param object options
   *   `filter`: A string filter that affects the "matched" handling.
   *     'user': Include properties from user style sheets.
   *     'ua': Include properties from user and user-agent sheets.
   *     Default value is 'ua'
   *   `inherited`: Include styles inherited from parent nodes.
   *   `matchedSelectors`: Include an array of specific (desugared) selectors that
   *     caused this rule to match its node.
   *   `skipPseudo`: Exclude styles applied to pseudo elements of the provided node.
   * @param array entries
   *   List of appliedstyle objects that lists the rules that apply to the
   *   node. If adding a new rule to the stylesheet, only the new rule entry
   *   is provided and only the style properties that apply to the new
   *   rule is fetched.
   * @returns Array of rule entries that applies to the given node and its associated rules.
   */
  getAppliedProps(node, entries, options) {
    if (options.inherited) {
      let parent = this.walker.parentNode(node);
      while (parent && parent.rawNode.nodeType != Node.DOCUMENT_NODE) {
        entries = entries.concat(
          this._getAllElementRules(parent, parent, options)
        );
        parent = this.walker.parentNode(parent);
      }
    }

    if (options.matchedSelectors) {
      for (const entry of entries) {
        if (entry.rule.type === ELEMENT_STYLE) {
          continue;
        }

        const domRule = entry.rule.rawRule;
        const element = entry.inherited
          ? entry.inherited.rawNode
          : node.rawNode;

        const { bindingElement, pseudo } =
          CssLogic.getBindingElementAndPseudo(element);
        const relevantLinkVisited = CssLogic.hasVisitedState(bindingElement);
        entry.matchedSelectorIndexes = [];

        const len = domRule.selectorCount;
        for (let i = 0; i < len; i++) {
          if (
            domRule.selectorMatchesElement(
              i,
              bindingElement,
              pseudo,
              relevantLinkVisited
            )
          ) {
            entry.matchedSelectorIndexes.push(i);
          }
        }
      }
    }

    // Add all the keyframes rule associated with the element
    const computedStyle = this.cssLogic.computedStyle;
    if (computedStyle) {
      let animationNames = computedStyle.animationName.split(",");
      animationNames = animationNames.map(name => name.trim());

      if (animationNames) {
        // Traverse through all the available keyframes rule and add
        // the keyframes rule that matches the computed animation name
        for (const keyframesRule of this.cssLogic.keyframesRules) {
          if (!animationNames.includes(keyframesRule.name)) {
            continue;
          }

          for (const rule of keyframesRule.cssRules) {
            entries.push({
              rule: this._styleRef(rule),
              keyframes: this._styleRef(keyframesRule),
            });
          }
        }
      }
    }

    return entries;
  }

  /**
   * Get layout-related information about a node.
   * This method returns an object with properties giving information about
   * the node's margin, border, padding and content region sizes, as well
   * as information about the type of box, its position, z-index, etc...
   * @param {NodeActor} node
   * @param {Object} options The only available option is autoMargins.
   * If set to true, the element's margins will receive an extra check to see
   * whether they are set to "auto" (knowing that the computed-style in this
   * case would return "0px").
   * The returned object will contain an extra property (autoMargins) listing
   * all margins that are set to auto, e.g. {top: "auto", left: "auto"}.
   * @return {Object}
   */
  getLayout(node, options) {
    this.cssLogic.highlight(node.rawNode);

    const layout = {};

    // First, we update the first part of the box model view, with
    // the size of the element.

    const clientRect = node.rawNode.getBoundingClientRect();
    layout.width = parseFloat(clientRect.width.toPrecision(6));
    layout.height = parseFloat(clientRect.height.toPrecision(6));

    // We compute and update the values of margins & co.
    const style = CssLogic.getComputedStyle(node.rawNode);
    for (const prop of [
      "position",
      "top",
      "right",
      "bottom",
      "left",
      "margin-top",
      "margin-right",
      "margin-bottom",
      "margin-left",
      "padding-top",
      "padding-right",
      "padding-bottom",
      "padding-left",
      "border-top-width",
      "border-right-width",
      "border-bottom-width",
      "border-left-width",
      "z-index",
      "box-sizing",
      "display",
      "float",
      "line-height",
    ]) {
      layout[prop] = style.getPropertyValue(prop);
    }

    if (options.autoMargins) {
      layout.autoMargins = this.processMargins(this.cssLogic);
    }

    for (const i in this.map) {
      const property = this.map[i].property;
      this.map[i].value = parseFloat(style.getPropertyValue(property));
    }

    return layout;
  }

  /**
   * Find 'auto' margin properties.
   */
  processMargins(cssLogic) {
    const margins = {};

    for (const prop of ["top", "bottom", "left", "right"]) {
      const info = cssLogic.getPropertyInfo("margin-" + prop);
      const selectors = info.matchedSelectors;
      if (selectors && !!selectors.length && selectors[0].value == "auto") {
        margins[prop] = "auto";
      }
    }

    return margins;
  }

  /**
   * On page navigation, tidy up remaining objects.
   */
  onFrameUnload() {
    this.styleElements = new WeakMap();
  }

  _onStylesheetUpdated({ resourceId, updateKind, updates = {} }) {
    if (updateKind != "style-applied") {
      return;
    }
    const kind = updates.event.kind;
    // Duplicate refMap content before looping as onStyleApplied may mutate it
    for (const styleActor of [...this.refMap.values()]) {
      // Ignore StyleRuleActor that don't have a parent stylesheet.
      // i.e. actor whose type is ELEMENT_STYLE.
      if (!styleActor._parentSheet) {
        continue;
      }
      const resId = this.styleSheetsManager.getStyleSheetResourceId(
        styleActor._parentSheet
      );
      if (resId === resourceId) {
        styleActor.onStyleApplied(kind);
      }
    }
    this._styleApplied(kind);
  }

  /**
   * Helper function for adding a new rule and getting its applied style
   * properties
   * @param NodeActor node
   * @param CSSStyleRule rule
   * @returns Array containing its applied style properties
   */
  getNewAppliedProps(node, rule) {
    const ruleActor = this._styleRef(rule);
    return this.getAppliedProps(node, [{ rule: ruleActor }], {
      matchedSelectors: true,
    });
  }

  /**
   * Adds a new rule, and returns the new StyleRuleActor.
   * @param {NodeActor} node
   * @param {String} pseudoClasses The list of pseudo classes to append to the
   *        new selector.
   * @returns {StyleRuleActor} the new rule
   */
  async addNewRule(node, pseudoClasses) {
    let sheet = null;
    const doc = node.rawNode.ownerDocument;
    if (
      this.styleElements.has(doc) &&
      this.styleElements.get(doc).ownerNode?.isConnected
    ) {
      sheet = this.styleElements.get(doc);
    } else {
      sheet = await this.styleSheetsManager.addStyleSheet(doc);
      this.styleElements.set(doc, sheet);
    }

    const cssRules = sheet.cssRules;
    const rawNode = node.rawNode;
    const classes = [...rawNode.classList];

    let selector;
    if (rawNode.id) {
      selector = "#" + CSS.escape(rawNode.id);
    } else if (classes.length) {
      selector = "." + classes.map(c => CSS.escape(c)).join(".");
    } else {
      selector = rawNode.localName;
    }

    if (pseudoClasses && pseudoClasses.length) {
      selector += pseudoClasses.join("");
    }

    const index = sheet.insertRule(selector + " {}", cssRules.length);

    const resourceId = this.styleSheetsManager.getStyleSheetResourceId(sheet);
    let authoredText = await this.styleSheetsManager.getText(resourceId);
    authoredText += "\n" + selector + " {\n" + "}";
    await this.styleSheetsManager.setStyleSheetText(resourceId, authoredText);

    const cssRule = sheet.cssRules.item(index);
    const ruleActor = this._styleRef(cssRule, null, true);

    TrackChangeEmitter.trackChange({
      ...ruleActor.metadata,
      type: "rule-add",
      add: null,
      remove: null,
      selector,
    });

    return { entries: this.getNewAppliedProps(node, cssRule) };
  }

  /**
   * Cause all StyleRuleActor instances of observed CSS rules to check whether the
   * states of their declarations have changed.
   *
   * Observed rules are the latest rules returned by a call to PageStyleActor.getApplied()
   *
   * This is necessary because changes in one rule can cause the declarations in another
   * to not be applicable (inactive CSS). The observers of those rules should be notified.
   * Rules will fire a "rule-updated" event if any of their declarations changed state.
   *
   * Call this method whenever a CSS rule is mutated:
   * - a CSS declaration is added/changed/disabled/removed
   * - a selector is added/changed/removed
   *
   * @param {Array<StyleRuleActor>} rulesToForceRefresh: An array of rules that,
   *        if observed, should be refreshed even if the state of their declaration
   *        didn't change.
   */
  refreshObservedRules(rulesToForceRefresh) {
    for (const rule of this._observedRules) {
      const force = rulesToForceRefresh && rulesToForceRefresh.includes(rule);
      rule.maybeRefresh(force);
    }
  }

  /**
   * Get an array of existing attribute values in a node document.
   *
   * @param {String} search: A string to filter attribute value on.
   * @param {String} attributeType: The type of attribute we want to retrieve the values.
   * @param {Element} node: The element we want to get possible attributes for. This will
   *        be used to get the document where the search is happening.
   * @returns {Array<String>} An array of strings
   */
  getAttributesInOwnerDocument(search, attributeType, node) {
    if (!search) {
      throw new Error("search is mandatory");
    }

    // In a non-fission world, a node from an iframe shares the same `rootNode` as a node
    // in the top-level document. So here we need to retrieve the document from the node
    // in parameter in order to retrieve the right document.
    // This may change once we have a dedicated walker for every target in a tab, as we'll
    // be able to directly talk to the "right" walker actor.
    const targetDocument = node.rawNode.ownerDocument;

    // We store the result in a Set which will contain the attribute value
    const result = new Set();
    const lcSearch = search.toLowerCase();
    this._collectAttributesFromDocumentDOM(
      result,
      lcSearch,
      attributeType,
      targetDocument,
      node.rawNode
    );
    this._collectAttributesFromDocumentStyleSheets(
      result,
      lcSearch,
      attributeType,
      targetDocument
    );

    return Array.from(result).sort();
  }

  /**
   * Collect attribute values from the document DOM tree, matching the passed filter and
   * type, to the result Set.
   *
   * @param {Set<String>} result: A Set to which the results will be added.
   * @param {String} search: A string to filter attribute value on.
   * @param {String} attributeType: The type of attribute we want to retrieve the values.
   * @param {Document} targetDocument: The document the search occurs in.
   * @param {Node} currentNode: The current element rawNode
   */
  _collectAttributesFromDocumentDOM(
    result,
    search,
    attributeType,
    targetDocument,
    nodeRawNode
  ) {
    // In order to retrieve attributes from DOM elements in the document, we're going to
    // do a query on the root node using attributes selector, to directly get the elements
    // matching the attributes we're looking for.

    // For classes, we need something a bit different as the className we're looking
    // for might not be the first in the attribute value, meaning we can't use the
    // "attribute starts with X" selector.
    const attributeSelectorPositionChar = attributeType === "class" ? "*" : "^";
    const selector = `[${attributeType}${attributeSelectorPositionChar}=${search} i]`;

    const matchingElements = targetDocument.querySelectorAll(selector);

    for (const element of matchingElements) {
      if (element === nodeRawNode) {
        return;
      }
      // For class attribute, we need to add the elements of the classList that match
      // the filter string.
      if (attributeType === "class") {
        for (const cls of element.classList) {
          if (!result.has(cls) && cls.toLowerCase().startsWith(search)) {
            result.add(cls);
          }
        }
      } else {
        const { value } = element.attributes[attributeType];
        // For other attributes, we can directly use the attribute value.
        result.add(value);
      }
    }
  }

  /**
   * Collect attribute values from the document stylesheets, matching the passed filter
   * and type, to the result Set.
   *
   * @param {Set<String>} result: A Set to which the results will be added.
   * @param {String} search: A string to filter attribute value on.
   * @param {String} attributeType: The type of attribute we want to retrieve the values.
   *                       It only supports "class" and "id" at the moment.
   * @param {Document} targetDocument: The document the search occurs in.
   */
  _collectAttributesFromDocumentStyleSheets(
    result,
    search,
    attributeType,
    targetDocument
  ) {
    if (attributeType !== "class" && attributeType !== "id") {
      return;
    }

    // We loop through all the stylesheets and their rules, recursively so we can go through
    // nested rules, and then use the lexer to only get the attributes we're looking for.
    const traverseRules = ruleList => {
      for (const rule of ruleList) {
        this._collectAttributesFromRule(result, rule, search, attributeType);
        if (rule.cssRules) {
          traverseRules(rule.cssRules);
        }
      }
    };
    for (const styleSheet of targetDocument.styleSheets) {
      traverseRules(styleSheet.rules);
    }
  }

  /**
   * Collect attribute values from the rule, matching the passed filter and type, to the
   * result Set.
   *
   * @param {Set<String>} result: A Set to which the results will be added.
   * @param {Rule} rule: The rule the search occurs in.
   * @param {String} search: A string to filter attribute value on.
   * @param {String} attributeType: The type of attribute we want to retrieve the values.
   *                       It only supports "class" and "id" at the moment.
   */
  _collectAttributesFromRule(result, rule, search, attributeType) {
    const shouldRetrieveClasses = attributeType === "class";
    const shouldRetrieveIds = attributeType === "id";

    const { selectorText } = rule;
    // If there's no selectorText, or if the selectorText does not include the
    // filter, we can bail out.
    if (!selectorText || !selectorText.toLowerCase().includes(search)) {
      return;
    }

    // Check if we should parse the selectorText (do we need to check for class/id and
    // if so, does the selector contains class/id related chars).
    const parseForClasses =
      shouldRetrieveClasses &&
      selectorText.toLowerCase().includes(`.${search}`);
    const parseForIds =
      shouldRetrieveIds && selectorText.toLowerCase().includes(`#${search}`);

    if (!parseForClasses && !parseForIds) {
      return;
    }

    const lexer = new InspectorCSSParser(selectorText);
    let token;
    while ((token = lexer.nextToken())) {
      if (
        token.tokenType === "Delim" &&
        shouldRetrieveClasses &&
        token.text === "."
      ) {
        token = lexer.nextToken();
        if (
          token.tokenType === "Ident" &&
          token.text.toLowerCase().startsWith(search)
        ) {
          result.add(token.text);
        }
      }
      if (token.tokenType === "IDHash" && shouldRetrieveIds) {
        const idWithoutHash = token.value;
        if (idWithoutHash.startsWith(search)) {
          result.add(idWithoutHash);
        }
      }
    }
  }
}
exports.PageStyleActor = PageStyleActor;
