/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { l10n } = require("resource://devtools/shared/inspector/css-logic.js");
const {
  PSEUDO_CLASSES,
  ELEMENT_SPECIFIC_PSEUDO_CLASSES,
} = require("resource://devtools/shared/css/constants.js");
const {
  style: { ELEMENT_STYLE, PRES_HINTS },
} = require("resource://devtools/shared/constants.js");
const Rule = require("resource://devtools/client/inspector/rules/models/rule.js");
const {
  InplaceEditor,
  editableField,
  editableItem,
} = require("resource://devtools/client/shared/inplace-editor.js");
const TextPropertyEditor = require("resource://devtools/client/inspector/rules/views/text-property-editor.js");
const {
  createChild,
  blurOnMultipleProperties,
  promiseWarn,
} = require("resource://devtools/client/inspector/shared/utils.js");
const {
  parseNamedDeclarations,
  parsePseudoClassesAndAttributes,
  SELECTOR_ATTRIBUTE,
  SELECTOR_ELEMENT,
  SELECTOR_PSEUDO_CLASS,
} = require("resource://devtools/shared/css/parsing-utils.js");
const EventEmitter = require("resource://devtools/shared/event-emitter.js");
const CssLogic = require("resource://devtools/shared/inspector/css-logic.js");

loader.lazyRequireGetter(
  this,
  "Tools",
  "resource://devtools/client/definitions.js",
  true
);
loader.lazyRequireGetter(
  this,
  "PluralForm",
  "resource://devtools/shared/plural-form.js",
  true
);

const STYLE_INSPECTOR_PROPERTIES =
  "devtools/shared/locales/styleinspector.properties";
const { LocalizationHelper } = require("resource://devtools/shared/l10n.js");
const STYLE_INSPECTOR_L10N = new LocalizationHelper(STYLE_INSPECTOR_PROPERTIES);

const COMPONENT_PROPERTIES = "devtools/client/locales/components.properties";
const COMPONENT_L10N = new LocalizationHelper(COMPONENT_PROPERTIES);

loader.lazyGetter(this, "NEW_PROPERTY_NAME_INPUT_LABEL", function () {
  return STYLE_INSPECTOR_L10N.getStr("rule.newPropertyName.label");
});

const UNUSED_CSS_PROPERTIES_HIDE_THRESHOLD = 10;
const INDENT_SIZE = 2;
const INDENT_STR = " ".repeat(INDENT_SIZE);

/**
 * RuleEditor is responsible for the following:
 *   Owns a Rule object and creates a list of TextPropertyEditors
 *     for its TextProperties.
 *   Manages creation of new text properties.
 */
class RuleEditor extends EventEmitter {
  /**
   * @param {CssRuleView} ruleView
   *        The CssRuleView containg the document holding this rule editor.
   * @param {Rule} rule
   *        The Rule object we're editing.
   * @param {object} options
   * @param {Set} options.elementsWithPendingClicks
   * @param {Function} options.onShowUnusedCustomCssProperties
   * @param {boolean} options.shouldHideUnusedCustomCssProperties
   */
  constructor(ruleView, rule, options = {}) {
    super();

    this.ruleView = ruleView;
    this.doc = this.ruleView.styleDocument;
    this.toolbox = this.ruleView.inspector.toolbox;
    this.telemetry = this.toolbox.telemetry;
    this.rule = rule;
    this.options = options;

    this.isEditable = rule.isEditable();
    // Flag that blocks updates of the selector and properties when it is
    // being edited
    this.isEditing = false;

    this.#abortController = new this.doc.defaultView.AbortController();
    const { signal } = this.#abortController;
    const baseEventConfig = { signal };
    this.rule.domRule.on(
      "location-changed",
      this.#locationChanged,
      baseEventConfig
    );
    this.toolbox.on("tool-registered", this.#onToolChanged, baseEventConfig);
    this.toolbox.on("tool-unregistered", this.#onToolChanged, baseEventConfig);

    this.#create();
  }

  #abortController;
  #unusedCssVariableDeclarations;
  #showUnusedCustomCssPropertiesButton;
  #unsubscribeSourceMap;
  #ruleViewIsEditing;

  destroy() {
    for (const prop of this.rule.textProps) {
      prop.editor?.destroy();
    }

    this.#unusedCssVariableDeclarations = null;

    if (this.#showUnusedCustomCssPropertiesButton) {
      this.#nullifyShowUnusedCustomCssProperties({ removeFromDom: false });
    }

    this.#abortController.abort();

    if (this.#unsubscribeSourceMap) {
      this.#unsubscribeSourceMap();
    }

    // Remove from the DOM so that CssRuleView can clear empty rule containers
    this.element.remove();
    this.element = null;
  }

  #sourceMapURLService;
  get sourceMapURLService() {
    if (!this.#sourceMapURLService) {
      // sourceMapURLService is a lazy getter in the toolbox.
      this.#sourceMapURLService = this.toolbox.sourceMapURLService;
    }

    return this.#sourceMapURLService;
  }

  get isSelectorEditable() {
    return (
      this.isEditable &&
      this.rule.domRule.type !== ELEMENT_STYLE &&
      this.rule.domRule.type !== CSSRule.KEYFRAME_RULE &&
      this.rule.domRule.className !== "CSSPositionTryRule"
    );
  }

  get showSelectorHighlighterButton() {
    return (
      this.rule.domRule.type !== CSSRule.KEYFRAME_RULE &&
      this.rule.domRule.className !== "CSSPositionTryRule"
    );
  }

  #create() {
    this.element = this.doc.createElement("div");
    this.element.className =
      "ruleview-rule devtools-monospace" +
      (this.rule.inherited ? " ruleview-rule-inherited" : "");
    this.element.dataset.ruleId = this.rule.domRule.actorID;
    this.element.setAttribute("uneditable", !this.isEditable);
    this.element.setAttribute("unmatched", this.rule.isUnmatched);
    this.element.setAttribute("role", "article");

    // This is used by tests
    this.element._ruleEditor = this;

    // Give a relative position for the inplace editor's measurement
    // span to be placed absolutely against.
    this.element.style.position = "relative";

    // Add the source link for supported rules. inline style and pres hints are not visible
    // in the StyleEditor, so don't show anything for such rule.
    if (
      this.rule.domRule.type !== ELEMENT_STYLE &&
      this.rule.domRule.type !== PRES_HINTS
    ) {
      this.source = createChild(this.element, "div", {
        class: "ruleview-rule-source theme-link",
      });
      const sourceLabel = this.doc.createElement("a");
      sourceLabel.classList.add("ruleview-rule-source-label");
      sourceLabel.addEventListener("click", this.#onSourceClick);
      this.source.appendChild(sourceLabel);
    }
    this.#updateSourceLink();
    this.#createAncestorSelectors();

    this.ruleviewCodeEl = createChild(this.element, "div", {
      class: "ruleview-code",
    });

    const header = createChild(this.ruleviewCodeEl, "div", {});

    createChild(header, "span", {
      class: "ruleview-rule-indent",
      textContent: INDENT_STR.repeat(this.rule.domRule.ancestorData.length),
    });

    this.selectorText = createChild(header, "span", {
      class: "ruleview-selectors-container",
      tabindex: this.isSelectorEditable ? "0" : "-1",
    });

    if (
      this.rule.domRule.type === ELEMENT_STYLE ||
      this.rule.domRule.type === PRES_HINTS
    ) {
      this.selectorText.classList.add("alternative-selector");
    }

    if (this.isSelectorEditable) {
      this.selectorText.addEventListener("click", event => {
        // Clicks within the selector shouldn't propagate any further.
        event.stopPropagation();
      });

      editableField({
        element: this.selectorText,
        done: this.#onSelectorDone,
        cssProperties: this.rule.cssProperties,
        // (Shift+)Tab will move the focus to the previous/next editable field (so property name,
        // or new property of the previous rule).
        focusEditableFieldAfterApply: true,
        focusEditableFieldContainerSelector: ".ruleview-rule",
        // We don't want Enter to trigger the next editable field, just to validate
        // what the user entered, close the editor, and focus the span so the user can
        // navigate with the keyboard as expected, unless the user has
        // devtools.inspector.rule-view.focusNextOnEnter set to true
        stopOnReturn: this.ruleView.inplaceEditorFocusNextOnEnter !== true,
      });
    } else {
      this.selectorText.classList.add("uneditable-selector");
    }

    if (this.rule.domRule.type !== CSSRule.KEYFRAME_RULE) {
      // This is a "normal" rule with a selector.
      let computedSelector = "";
      if (this.rule.domRule.selectors) {
        computedSelector = this.rule.domRule.computedSelector;
        // Otherwise, the rule is either inherited or inline, and selectors will
        // be computed on demand when the highlighter is requested.
      }

      if (this.showSelectorHighlighterButton) {
        const isHighlighted =
          this.ruleView.isSelectorHighlighted(computedSelector);
        // Handling of click events is delegated to CssRuleView.handleEvent()
        createChild(header, "button", {
          class:
            "ruleview-selectorhighlighter js-toggle-selector-highlighter" +
            (isHighlighted ? " highlighted" : ""),
          "aria-pressed": isHighlighted,
          // This is used in rules.js for the selector highlighter
          "data-computed-selector": computedSelector,
          title: l10n("rule.selectorHighlighter.tooltip"),
        });
      }
    }

    this.openBrace = createChild(header, "span", {
      class: "ruleview-ruleopen",
      textContent: " {",
    });

    // We can't use a proper "ol" as it will mess with selection copy text,
    // adding spaces on list item instead of the one we craft (.ruleview-rule-indent)
    this.propertyList = createChild(this.ruleviewCodeEl, "div", {
      class: "ruleview-propertylist",
      role: "list",
    });

    this.populate();

    this.closeBrace = createChild(this.ruleviewCodeEl, "div", {
      class: "ruleview-ruleclose",
      tabindex: this.isEditable ? "0" : "-1",
    });

    if (this.rule.domRule.ancestorData.length) {
      createChild(this.closeBrace, "span", {
        class: "ruleview-rule-indent",
        textContent: INDENT_STR.repeat(this.rule.domRule.ancestorData.length),
      });
    }
    this.closeBrace.append(this.doc.createTextNode("}"));

    if (this.rule.domRule.ancestorData.length) {
      let closingBracketsText = "";
      for (let i = this.rule.domRule.ancestorData.length - 1; i >= 0; i--) {
        if (i) {
          closingBracketsText += INDENT_STR.repeat(i);
        }
        closingBracketsText += "}\n";
      }
      createChild(this.ruleviewCodeEl, "div", {
        class: "ruleview-ancestor-ruleclose",
        textContent: closingBracketsText,
      });
    }

    if (this.isEditable) {
      // A newProperty editor should only be created when no editor was
      // previously displayed. Since the editors are cleared on blur,
      // check this.ruleview.isEditing on mousedown
      this.#ruleViewIsEditing = false;

      this.ruleviewCodeEl.addEventListener("mousedown", () => {
        this.#ruleViewIsEditing = this.ruleView.isEditing;
      });

      this.ruleviewCodeEl.addEventListener("click", () => {
        const selection = this.doc.defaultView.getSelection();
        if (selection.isCollapsed && !this.#ruleViewIsEditing) {
          this.newProperty();
        }
        // Cleanup the #ruleViewIsEditing flag
        this.#ruleViewIsEditing = false;
      });

      this.element.addEventListener("mousedown", () => {
        this.doc.defaultView.focus();
      });

      // Create a property editor when the close brace is clicked.
      editableItem({ element: this.closeBrace }, () => {
        this.newProperty();
      });
    }
  }

  /**
   * Create all the selector elements for the rule's parents rules
   */
  #createAncestorSelectors() {
    if (!this.rule.domRule.ancestorData.length) {
      return;
    }

    const ancestorsFrag = this.doc.createDocumentFragment();
    this.rule.domRule.ancestorData.forEach((ancestorData, index) => {
      const ancestorItem = this.doc.createElement("div");
      ancestorItem.setAttribute("role", "listitem");
      ancestorsFrag.append(ancestorItem);
      ancestorItem.setAttribute("data-ancestor-index", index);
      ancestorItem.classList.add("ruleview-rule-ancestor");
      if (ancestorData.type) {
        ancestorItem.classList.add(ancestorData.type);
      }

      // Indent each parent selector
      if (index) {
        createChild(ancestorItem, "span", {
          class: "ruleview-rule-indent",
          textContent: INDENT_STR.repeat(index),
        });
      }

      const selectorContainer = createChild(ancestorItem, "span", {
        class: "ruleview-rule-ancestor-selectorcontainer",
      });

      if (ancestorData.type == "container") {
        this.#createAncestorContainerQuerySelector(
          selectorContainer,
          ancestorData
        );
      } else if (ancestorData.type == "layer") {
        selectorContainer.append(
          this.doc.createTextNode(
            `@layer${ancestorData.value ? " " + ancestorData.value : ""}`
          )
        );
      } else if (ancestorData.type == "media") {
        selectorContainer.append(
          this.doc.createTextNode(`@media ${ancestorData.value}`)
        );
      } else if (ancestorData.type == "supports") {
        selectorContainer.append(
          this.doc.createTextNode(`@supports ${ancestorData.conditionText}`)
        );
      } else if (ancestorData.type == "import") {
        selectorContainer.append(
          this.doc.createTextNode(`@import ${ancestorData.value}`)
        );
      } else if (ancestorData.type == "scope") {
        let text = `@scope`;
        if (ancestorData.start) {
          text += ` (${ancestorData.start})`;

          if (ancestorData.end) {
            text += ` to (${ancestorData.end})`;
          }
        }
        selectorContainer.append(this.doc.createTextNode(text));
      } else if (ancestorData.type == "starting-style") {
        selectorContainer.append(this.doc.createTextNode(`@starting-style`));
      } else if (ancestorData.type == "appearance-base") {
        selectorContainer.append(this.doc.createTextNode(`@appearance-base`));
      } else if (ancestorData.selectors) {
        ancestorData.selectors.forEach((selector, i) => {
          if (i !== 0) {
            createChild(selectorContainer, "span", {
              class: "ruleview-selector-separator",
              textContent: ", ",
            });
          }

          const selectorEl = createChild(selectorContainer, "span", {
            class: "ruleview-selector",
            textContent: selector,
          });

          const warningsContainer = this.#createWarningsElementForSelector(
            i,
            ancestorData.selectorWarnings
          );
          if (warningsContainer) {
            selectorEl.append(warningsContainer);
          }
        });
      } else {
        // We shouldn't get here as `type` should only match to what can be set in
        // the StyleRuleActor form, but just in case, let's return an empty string.
        console.warn("Unknown ancestor data type:", ancestorData.type);
        return;
      }

      createChild(ancestorItem, "span", {
        class: "ruleview-ancestor-ruleopen",
        textContent: " {",
      });
    });

    // We can't use a proper "ol" as it will mess with selection copy text,
    // adding spaces on list item instead of the one we craft (.ruleview-rule-indent)
    this.ancestorDataEl = createChild(this.element, "div", {
      class: "ruleview-rule-ancestor-data",
      role: "list",
    });
    this.ancestorDataEl.append(ancestorsFrag);
  }

  /**
   * Create the selector for a given `@container` query rule
   *
   * @param {Element} selectorContainer: The container into which the container query selector
   *        should be added.
   * @param {object} containerQueryData
   * @param {Array<object>} containerQueryData.conditions
   */
  #createAncestorContainerQuerySelector(selectorContainer, containerQueryData) {
    const containerQueryEl = createChild(selectorContainer, "span", {
      class: "container-query",
    });
    containerQueryEl.append("@container ");

    for (
      let conditionIndex = 0;
      conditionIndex < containerQueryData.conditions.length;
      conditionIndex++
    ) {
      const { containerName, containerQuery, hasContainer, matched } =
        containerQueryData.conditions[conditionIndex];

      const containerConditionEl = createChild(containerQueryEl, "span", {
        class:
          "container-condition has-tooltip" +
          (!hasContainer || !matched ? " unmatched" : ""),
        "data-condition-index": conditionIndex,
      });

      if (containerName) {
        containerConditionEl.append(containerName);
      }

      // Add a space between the container name and the query so the title,
      // which is computed from the DOM, displays correctly.
      containerConditionEl.append((containerName ? " " : "") + containerQuery);

      if (conditionIndex !== containerQueryData.conditions.length - 1) {
        createChild(containerQueryEl, "span", {
          class: "ruleview-selector-separator",
          textContent: ", ",
        });
      }
    }
  }

  /**
   * Returns the selector warnings element, or null if selector at selectorIndex
   * does not have any warning.
   *
   * @param {Integer} selectorIndex: The index of the selector we want to create the
   *        warnings for
   * @param {Array<object>} selectorWarnings: An array of object of the following shape:
   *        - {Integer} index: The index of the selector this applies to
   *        - {String} kind: Identifies the warning
   * @returns {Element|null}
   */
  #createWarningsElementForSelector(selectorIndex, selectorWarnings) {
    if (!selectorWarnings) {
      return null;
    }

    const warningKinds = [];
    for (const { index, kind } of selectorWarnings) {
      if (index !== selectorIndex) {
        continue;
      }
      warningKinds.push(kind);
    }

    if (!warningKinds.length) {
      return null;
    }

    const warningsContainer = this.doc.createElement("div");
    warningsContainer.classList.add(
      "ruleview-selector-warnings",
      "has-tooltip"
    );

    warningsContainer.setAttribute(
      "data-selector-warning-kind",
      warningKinds.join(",")
    );

    if (warningKinds.includes("UnconstrainedHas")) {
      warningsContainer.classList.add("slow");
    }

    return warningsContainer;
  }

  /**
   * Called when a tool is registered or unregistered.
   */
  #onToolChanged = () => {
    if (!this.source) {
      return;
    }

    // When the source editor is registered, update the source links
    // to be clickable; and if it is unregistered, update the links to
    // be unclickable.
    if (this.toolbox.isToolRegistered("styleeditor")) {
      this.source.removeAttribute("unselectable");
    } else {
      this.source.setAttribute("unselectable", "true");
    }
  };

  /**
   * Event handler called when a property changes on the
   * StyleRuleActor.
   */
  #locationChanged = () => {
    this.#updateSourceLink();
  };

  #onSourceClick = e => {
    e.preventDefault();
    if (this.source.hasAttribute("unselectable")) {
      return;
    }

    const { inspector } = this.ruleView;
    if (Tools.styleEditor.isToolSupported(inspector.toolbox)) {
      inspector.toolbox.viewStyleSourceByResource(
        this.rule.sheet,
        this.rule.ruleLine,
        this.rule.ruleColumn
      );
    }
  };

  /**
   * Update the text of the source link to reflect whether we're showing
   * original sources or not.  This is a callback for
   * SourceMapURLService.subscribeByID, which see.
   *
   * @param {object | null} originalLocation
   *        The original position object (url/line/column) or null.
   */
  #updateLocation = originalLocation => {
    let displayURL = this.rule.sheet?.href;
    const constructed = this.rule.sheet?.constructed;
    let line = this.rule.ruleLine;
    if (originalLocation) {
      displayURL = originalLocation.url;
      line = originalLocation.line;
    }

    let sourceTextContent = CssLogic.shortSource({
      constructed,
      href: displayURL,
    });

    let displayLocation = displayURL ? displayURL : sourceTextContent;
    if (line > 0) {
      sourceTextContent += ":" + line;
      displayLocation += ":" + line;
    }
    const title = COMPONENT_L10N.getFormatStr(
      "frame.viewsourceinstyleeditor",
      displayLocation
    );

    const sourceLabel = this.element.querySelector(
      ".ruleview-rule-source-label"
    );
    sourceLabel.setAttribute("title", title);
    sourceLabel.setAttribute("href", displayURL);
    sourceLabel.textContent = sourceTextContent;
  };

  #updateSourceLink() {
    if (this.source) {
      if (this.rule.isSystem) {
        const sourceLabel = this.element.querySelector(
          ".ruleview-rule-source-label"
        );
        const uaLabel = STYLE_INSPECTOR_L10N.getStr("rule.userAgentStyles");
        sourceLabel.textContent = uaLabel + " " + this.rule.title;
        sourceLabel.setAttribute("href", this.rule.sheet?.href);
      } else {
        this.#updateLocation(null);
      }

      if (this.rule.sheet && !this.rule.isSystem) {
        // Only get the original source link if the rule isn't a system
        // rule and if it isn't an inline rule.
        if (this.#unsubscribeSourceMap) {
          this.#unsubscribeSourceMap();
        }
        this.#unsubscribeSourceMap = this.sourceMapURLService.subscribeByID(
          this.rule.sheet.resourceId,
          this.rule.ruleLine,
          this.rule.ruleColumn,
          this.#updateLocation
        );
      }
      // Set "unselectable" appropriately.
      this.#onToolChanged();
    }

    Promise.resolve().then(() => {
      this.emit("source-link-updated");
    });
  }

  /**
   * Update the rule editor with the contents of the rule.
   *
   * @param {boolean} reset
   *        True to completely reset the rule editor before populating.
   */
  populate(reset) {
    // Clear out existing viewers.
    this.selectorText.replaceChildren();

    // If selector text comes from a css rule, highlight selectors that
    // actually match.  For custom selector text (such as for the 'element'
    // style, just show the text directly.
    if (
      this.rule.domRule.type === ELEMENT_STYLE ||
      this.rule.domRule.type === PRES_HINTS
    ) {
      this.selectorText.textContent = this.rule.selectorText;
    } else if (this.rule.domRule.type === CSSRule.KEYFRAME_RULE) {
      this.selectorText.textContent = this.rule.domRule.keyText;
    } else if (this.rule.domRule.className === "CSSPositionTryRule") {
      this.selectorText.textContent = this.rule.domRule.name;
    } else {
      this.rule.domRule.selectors.forEach((selector, i) => {
        this.#populateSelector(selector, i);
      });
    }

    if (reset) {
      this.propertyList.replaceChildren();
    }

    this.#unusedCssVariableDeclarations =
      this.#getUnusedCssVariableDeclarations();
    const hideUnusedCssVariableDeclarations =
      this.#unusedCssVariableDeclarations.size >=
      // If the button was already displayed, hide unused variables if we have at least
      // one, even if it's less than the threshold
      (this.#showUnusedCustomCssPropertiesButton
        ? 1
        : UNUSED_CSS_PROPERTIES_HIDE_THRESHOLD);

    // If we won't hide any variable, clear the Set of unused variables as it's used in
    // updateUnusedCssVariables and we might do unnecessary computation if we still
    // track variables which are actually visible.
    if (!hideUnusedCssVariableDeclarations) {
      this.#unusedCssVariableDeclarations.clear();
    }

    for (const prop of this.rule.textProps) {
      if (hideUnusedCssVariableDeclarations && prop.isUnusedVariable) {
        continue;
      }
      if (!prop.editor && !prop.invisible) {
        const editor = new TextPropertyEditor(this, prop, {
          elementsWithPendingClicks: this.options.elementsWithPendingClicks,
        });
        this.propertyList.appendChild(editor.element);
      } else if (prop.editor) {
        // If an editor already existed, append it to the bottom now to make sure the
        // order of editors in the DOM follow the order of the rule's properties.
        this.propertyList.appendChild(prop.editor.element);
      }
    }

    if (hideUnusedCssVariableDeclarations) {
      if (!this.#showUnusedCustomCssPropertiesButton) {
        this.#showUnusedCustomCssPropertiesButton =
          this.doc.createElement("button");
        this.#showUnusedCustomCssPropertiesButton.classList.add(
          "devtools-button",
          "devtools-button-standalone",
          "ruleview-show-unused-custom-css-properties"
        );
        this.#showUnusedCustomCssPropertiesButton.addEventListener(
          "click",
          this.#onShowUnusedCustomCssPropertiesButtonClick
        );
      }
      this.ruleviewCodeEl.insertBefore(
        this.#showUnusedCustomCssPropertiesButton,
        this.closeBrace
      );
      this.#updateShowUnusedCustomCssPropertiesButtonText();
    } else if (this.#showUnusedCustomCssPropertiesButton) {
      this.#nullifyShowUnusedCustomCssProperties();
    }
  }

  updateUnusedCssVariables() {
    if (
      !this.#unusedCssVariableDeclarations ||
      !this.#unusedCssVariableDeclarations.size
    ) {
      return;
    }

    // Store the list of what used to be unused
    const previouslyUnused = Array.from(this.#unusedCssVariableDeclarations);
    // Then compute the list of unused variables again
    this.#unusedCssVariableDeclarations =
      this.#getUnusedCssVariableDeclarations();

    for (const prop of previouslyUnused) {
      if (this.#unusedCssVariableDeclarations.has(prop)) {
        continue;
      }

      // The prop wasn't used, but now is, so let's show it
      this.showUnusedCssVariable(prop, {
        updateButton: false,
      });
    }

    this.#updateShowUnusedCustomCssPropertiesButtonText();
  }

  /**
   * Create a TextPropertyEditor for TextProperty representing an unused CSS variable.
   *
   * @param {TextProperty} prop
   * @param {object} options
   * @param {boolean} options.updateButton
   * @returns {TextPropertyEditor|null} Returns null if passed TextProperty isn't found
   *          in the list of unused css variables
   */
  showUnusedCssVariable(prop, { updateButton = true } = {}) {
    if (prop.editor) {
      return null;
    }

    this.#unusedCssVariableDeclarations.delete(prop);

    const editor = new TextPropertyEditor(this, prop, {
      elementsWithPendingClicks: this.options.elementsWithPendingClicks,
    });
    const declarationIndex = this.rule.textProps.indexOf(prop);
    // We need to insert the editor according to its index in the list of declarations.
    // So let's try to find the prop which is placed higher and is visible
    let nextSibling;
    for (let i = declarationIndex + 1; i < this.rule.textProps.length; i++) {
      const currentProp = this.rule.textProps[i];
      if (currentProp.editor) {
        nextSibling = currentProp.editor.element;
        break;
      }
    }
    // If we couldn't find nextSibling, that means that no declaration with higher index
    // is visible, so we can put the newly visible property at the end
    this.propertyList.insertBefore(editor.element, nextSibling || null);

    if (updateButton) {
      this.#updateShowUnusedCustomCssPropertiesButtonText();
    }

    return editor;
  }

  /**
   * Returns a Set containing the list of unused CSS variable TextProperty which shouldn't
   * be visible.
   *
   * @returns {Set<TextProperty>}
   */
  #getUnusedCssVariableDeclarations() {
    const unusedCssVariableDeclarations = new Set();

    // No need to go through the declarations if we shouldn't hide unused custom properties
    if (!this.options.shouldHideUnusedCustomCssProperties) {
      return unusedCssVariableDeclarations;
    }

    // Compute a list of variables that will be visible, as there might be unused variables
    // that will be visible (e.g. if the user added one in the rules view)
    for (const prop of this.rule.textProps) {
      if (prop.isUnusedVariable) {
        unusedCssVariableDeclarations.add(prop);
      }
    }

    return unusedCssVariableDeclarations;
  }

  /**
   * Handle click on "Show X unused custom CSS properties" button
   *
   * @param {Event} e
   */
  #onShowUnusedCustomCssPropertiesButtonClick = e => {
    e.stopPropagation();

    this.#nullifyShowUnusedCustomCssProperties();

    for (const prop of this.#unusedCssVariableDeclarations) {
      if (!prop.invisible) {
        const editor = new TextPropertyEditor(this, prop, {
          elementsWithPendingClicks: this.options.elementsWithPendingClicks,
        });
        // Insert at the original declaration index
        this.propertyList.insertBefore(
          editor.element,
          this.propertyList.childNodes[this.rule.textProps.indexOf(prop)] ||
            null
        );
      }
    }
    if (typeof this.options.onShowUnusedCustomCssProperties === "function") {
      this.options.onShowUnusedCustomCssProperties();
    }
  };

  /**
   * Update the text for the "Show X unused custom CSS properties" button, or remove it
   * if there's no hidden custom properties anymore
   */
  #updateShowUnusedCustomCssPropertiesButtonText() {
    if (!this.#showUnusedCustomCssPropertiesButton) {
      return;
    }

    const unusedVariablesCount = this.#unusedCssVariableDeclarations.size;
    if (!unusedVariablesCount) {
      this.#nullifyShowUnusedCustomCssProperties();
      return;
    }

    const label = PluralForm.get(
      unusedVariablesCount,
      STYLE_INSPECTOR_L10N.getStr("rule.showUnusedCssVariable")
    ).replace("#1", unusedVariablesCount);

    this.#showUnusedCustomCssPropertiesButton.replaceChildren(label);
  }

  /**
   * Nullify this.#showUnusedCustomCssPropertiesButton, remove its click event handler
   * and remove it from the DOM if `removeFromDom` is set to true.
   *
   * @param {object} [options]
   * @param {boolean} [options.removeFromDom]
   *        Should the button be removed from the DOM (defaults to true)
   */
  #nullifyShowUnusedCustomCssProperties({ removeFromDom = true } = {}) {
    if (!this.#showUnusedCustomCssPropertiesButton) {
      return;
    }

    this.#showUnusedCustomCssPropertiesButton.removeEventListener(
      "click",
      this.#onShowUnusedCustomCssPropertiesButtonClick
    );

    if (removeFromDom) {
      this.#showUnusedCustomCssPropertiesButton.remove();
    }
    this.#showUnusedCustomCssPropertiesButton = null;
  }

  /**
   * Render a given rule selector in this.selectorText element
   *
   * @param {string} selector: The selector text to display
   * @param {number} selectorIndex: Its index in the rule
   */
  #populateSelector(selector, selectorIndex) {
    if (selectorIndex !== 0) {
      createChild(this.selectorText, "span", {
        class: "ruleview-selector-separator",
        textContent: ", ",
      });
    }

    const containerClass =
      "ruleview-selector " +
      (this.rule.matchedSelectorIndexes.includes(selectorIndex)
        ? "matched"
        : "unmatched");

    let selectorContainerTitle;
    if (
      typeof this.rule.selectorsSpecificity?.[selectorIndex] !== "undefined"
    ) {
      // The specificity that we get from the platform is a single number that we
      // need to format into the common `(x,y,z)` specificity string.
      const specificity = this.rule.selectorsSpecificity?.[selectorIndex];
      const a = Math.floor(specificity / (1024 * 1024));
      const b = Math.floor((specificity % (1024 * 1024)) / 1024);
      const c = specificity % 1024;
      selectorContainerTitle = STYLE_INSPECTOR_L10N.getFormatStr(
        "rule.selectorSpecificity.title",
        `(${a},${b},${c})`
      );
    }
    const selectorContainer = createChild(this.selectorText, "span", {
      class: containerClass,
      title: selectorContainerTitle,
    });

    const parsedSelector = parsePseudoClassesAndAttributes(selector);

    for (const selectorText of parsedSelector) {
      let selectorClass = "";

      switch (selectorText.type) {
        case SELECTOR_ATTRIBUTE:
          selectorClass = "ruleview-selector-attribute";
          break;
        case SELECTOR_ELEMENT:
          selectorClass = "ruleview-selector-element";
          break;
        case SELECTOR_PSEUDO_CLASS:
          selectorClass = [
            ...PSEUDO_CLASSES,
            ...Object.keys(ELEMENT_SPECIFIC_PSEUDO_CLASSES),
          ].some(pseudo => selectorText.value === pseudo)
            ? "ruleview-selector-pseudo-class-lock"
            : "ruleview-selector-pseudo-class";
          break;
        default:
          break;
      }

      createChild(selectorContainer, "span", {
        textContent: selectorText.value,
        class: selectorClass,
      });
    }

    const warningsContainer = this.#createWarningsElementForSelector(
      selectorIndex,
      this.rule.domRule.selectorWarnings
    );
    if (warningsContainer) {
      selectorContainer.append(warningsContainer);
    }
  }

  /**
   * Programatically add a new property to the rule.
   *
   * @param {string} name
   *        Property name.
   * @param {string} value
   *        Property value.
   * @param {string} priority
   *        Property priority.
   * @param {boolean} enabled
   *        True if the property should be enabled.
   * @param {TextProperty} siblingProp
   *        Optional, property next to which the new property will be added.
   * @return {TextProperty}
   *        The new property
   */
  addProperty(name, value, priority, enabled, siblingProp) {
    const prop = this.rule.createProperty(
      name,
      value,
      priority,
      enabled,
      siblingProp
    );
    const index = this.rule.textProps.indexOf(prop);
    const editor = new TextPropertyEditor(this, prop, {
      elementsWithPendingClicks: this.options.elementsWithPendingClicks,
    });

    // Insert this node before the DOM node that is currently at its new index
    // in the property list.  There is currently one less node in the DOM than
    // in the property list, so this causes it to appear after siblingProp.
    // If there is no node at its index, as is the case where this is the last
    // node being inserted, then this behaves as appendChild.
    this.propertyList.insertBefore(
      editor.element,
      this.propertyList.children[index]
    );

    return prop;
  }

  /**
   * Programatically add a list of new properties to the rule.  Focus the UI
   * to the proper location after adding (either focus the value on the
   * last property if it is empty, or create a new property and focus it).
   *
   * @param {Array} properties
   *        Array of properties, which are objects with this signature:
   *        {
   *          name: {string},
   *          value: {string},
   *          priority: {string}
   *        }
   * @param {TextProperty} siblingProp
   *        Optional, the property next to which all new props should be added.
   */
  addProperties(properties, siblingProp) {
    if (!properties || !properties.length) {
      return;
    }

    let lastProp = siblingProp;
    for (const p of properties) {
      const isCommented = Boolean(p.commentOffsets);
      const enabled = !isCommented;
      lastProp = this.addProperty(
        p.name,
        p.value,
        p.priority,
        enabled,
        lastProp
      );
    }

    // Either focus on the last value if incomplete, or start a new one.
    if (lastProp && lastProp.value.trim() === "") {
      lastProp.editor.valueSpan.click();
    } else {
      this.newProperty();
    }
  }

  /**
   * Create a text input for a property name.  If a non-empty property
   * name is given, we'll create a real TextProperty and add it to the
   * rule.
   */
  newProperty() {
    // If we're already creating a new property, ignore this.
    if (!this.closeBrace.hasAttribute("tabindex")) {
      return;
    }

    // While we're editing a new property, it doesn't make sense to start a second new
    // property editor, so disable focusing the close brace for now.
    this.closeBrace.removeAttribute("tabindex");
    // We also need to make the "Show Unused Variables" button non-focusable so hitting
    // Tab while focused in the new property editor will move the focus to the next rule
    // selector editor.
    if (this.#showUnusedCustomCssPropertiesButton) {
      this.#showUnusedCustomCssPropertiesButton.setAttribute("tabindex", "-1");
    }

    this.newPropItem = createChild(this.propertyList, "div", {
      class: "ruleview-property ruleview-newproperty",
      role: "listitem",
    });

    this.newPropSpan = createChild(this.newPropItem, "span", {
      class: "ruleview-propertyname",
      tabindex: "0",
    });

    this.multipleAddedProperties = null;

    this.editor = new InplaceEditor({
      element: this.newPropSpan,
      done: this.#onNewProperty,
      // (Shift+)Tab will move the focus to the previous/next editable field
      focusEditableFieldAfterApply: true,
      focusEditableFieldContainerSelector: ".ruleview-rule",
      destroy: this.#newPropertyDestroy,
      advanceChars: ":",
      contentType: InplaceEditor.CONTENT_TYPES.CSS_PROPERTY,
      popup: this.ruleView.popup,
      cssProperties: this.rule.cssProperties,
      inputAriaLabel: NEW_PROPERTY_NAME_INPUT_LABEL,
      getCssVariables: () =>
        this.rule.elementStyle.getAllCustomProperties(this.rule.pseudoElement),
    });

    // Auto-close the input if multiple rules get pasted into new property.
    this.editor.input.addEventListener(
      "paste",
      blurOnMultipleProperties(this.rule.cssProperties)
    );
  }

  /**
   * Called when the new property input has been dismissed.
   *
   * @param {string} value
   *        The value in the editor.
   * @param {boolean} commit
   *        True if the value should be committed.
   */
  #onNewProperty = (value, commit) => {
    if (!value || !commit) {
      return;
    }

    // parseDeclarations allows for name-less declarations, but in the present
    // case, we're creating a new declaration, it doesn't make sense to accept
    // these entries
    this.multipleAddedProperties = parseNamedDeclarations(
      this.rule.cssProperties.isKnown,
      value,
      true
    );

    // Blur the editor field now and deal with adding declarations later when
    // the field gets destroyed (see #newPropertyDestroy)
    this.editor.input.blur();

    this.telemetry.recordEvent("edit_rule", "ruleview");
  };

  /**
   * Called when the new property editor is destroyed.
   * This is where the properties (type TextProperty) are actually being
   * added, since we want to wait until after the inplace editor `destroy`
   * event has been fired to keep consistent UI state.
   */
  #newPropertyDestroy = () => {
    // We're done, make the close brace and "Show unused variable" button focusable again.
    this.closeBrace.setAttribute("tabindex", "0");
    if (this.#showUnusedCustomCssPropertiesButton) {
      this.#showUnusedCustomCssPropertiesButton.removeAttribute("tabindex");
    }

    this.propertyList.removeChild(this.newPropItem);
    delete this.newPropItem;
    delete this.newPropSpan;

    // If properties were added, we want to focus the proper element.
    // If the last new property has no value, focus the value on it.
    // Otherwise, start a new property and focus that field.
    if (this.multipleAddedProperties && this.multipleAddedProperties.length) {
      this.addProperties(this.multipleAddedProperties);
    }
  };

  /**
   * Called when the selector's inplace editor is closed.
   * Ignores the change if the user pressed escape, otherwise
   * commits it.
   *
   * @param {string} value
   *        The value contained in the editor.
   * @param {boolean} commit
   *        True if the change should be applied.
   * @param {number} direction
   *        The move focus direction number.
   */
  #onSelectorDone = async (value, commit, direction) => {
    if (
      !commit ||
      this.isEditing ||
      value === "" ||
      value === this.rule.selectorText
    ) {
      return;
    }

    const ruleView = this.ruleView;
    const elementStyle = ruleView.elementStyle;
    const element = elementStyle.element;

    this.isEditing = true;

    // Remove highlighter for the previous selector.
    const computedSelector = this.rule.domRule.computedSelector;
    if (this.ruleView.isSelectorHighlighted(computedSelector)) {
      await this.ruleView.toggleSelectorHighlighter(
        this.rule,
        computedSelector
      );
    }

    try {
      const response = await this.rule.domRule.modifySelector(element, value);

      // Modifying the selector might have removed the element (e.g. for pseudo element)
      if (!element.actorID) {
        return;
      }

      // We recompute the list of applied styles, because editing a
      // selector might cause this rule's position to change.
      const applied = await elementStyle.pageStyle.getApplied(element, {
        inherited: true,
        matchedSelectors: true,
        filter: elementStyle.showUserAgentStyles ? "ua" : undefined,
      });

      // The element might have been removed while we were trying to get the applied declarations
      if (!element.actorID) {
        return;
      }

      this.isEditing = false;

      const { ruleProps, isMatching } = response;
      if (!ruleProps) {
        // Notify for changes, even when nothing changes,
        // just to allow tests being able to track end of this request.
        ruleView.emit("ruleview-invalid-selector");
        return;
      }

      ruleProps.isUnmatched = !isMatching;
      const newRule = new Rule(elementStyle, ruleProps);
      const editor = new RuleEditor(ruleView, newRule, {
        elementsWithPendingClicks: new Set(),
      });
      newRule.editor = editor;
      const rules = elementStyle.rules;

      let newRuleIndex = applied.findIndex(r => r.rule == ruleProps.rule);
      const oldIndex = rules.indexOf(this.rule);

      // If the selector no longer matches, then we leave the rule in
      // the same relative position.
      if (newRuleIndex === -1) {
        newRuleIndex = oldIndex;
      }

      // Remove the old rule and insert the new rule.
      rules.splice(oldIndex, 1);
      rules.splice(newRuleIndex, 0, newRule);
      elementStyle.notifyChanged();
      elementStyle.onRuleUpdated();

      // We install the new editor in place of the old -- you might
      // think we would replicate the list-modification logic above,
      // but that is complicated due to the way the UI installs
      // pseudo-element rules and the like.
      this.element.parentNode.replaceChild(editor.element, this.element);

      // As the rules elements will be replaced, and given that the inplace-editor doesn't
      // wait for this `done` callback to be resolved, the focus management we do there
      // will be useless as this specific code will usually happen later (and the focused
      // element might be replaced).
      // Because of this, we need to handle setting the focus ourselves from here.
      editor.#moveSelectorFocus(direction);

      // Destroy the current editor as it got replaced by the fresh instance
      this.destroy();
    } catch (err) {
      this.isEditing = false;
      promiseWarn(err);
    }
  };

  /**
   * Handle moving the focus change after a Tab keypress in the selector inplace editor.
   *
   * @param {number} direction
   *        The move focus direction number.
   */
  #moveSelectorFocus(direction) {
    if (!direction || direction === Services.focus.MOVEFOCUS_BACKWARD) {
      return;
    }

    if (this.rule.textProps.length) {
      this.rule.textProps[0].editor.nameSpan.click();
    } else {
      this.propertyList.click();
    }
  }
}

module.exports = RuleEditor;
