/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const flags = require("resource://devtools/shared/flags.js");
const { l10n } = require("resource://devtools/shared/inspector/css-logic.js");
const {
  style: { ELEMENT_STYLE },
} = require("resource://devtools/shared/constants.js");
const {
  PSEUDO_CLASSES,
} = require("resource://devtools/shared/css/constants.js");
const OutputParser = require("resource://devtools/client/shared/output-parser.js");
const { PrefObserver } = require("resource://devtools/client/shared/prefs.js");
const ElementStyle = require("resource://devtools/client/inspector/rules/models/element-style.js");
const RuleEditor = require("resource://devtools/client/inspector/rules/views/rule-editor.js");
const RegisteredPropertyEditor = require("resource://devtools/client/inspector/rules/views/registered-property-editor.js");
const TooltipsOverlay = require("resource://devtools/client/inspector/shared/tooltips-overlay.js");
const {
  createChild,
  promiseWarn,
} = require("resource://devtools/client/inspector/shared/utils.js");
const { debounce } = require("resource://devtools/shared/debounce.js");
const EventEmitter = require("resource://devtools/shared/event-emitter.js");

loader.lazyRequireGetter(
  this,
  ["flashElementOn", "flashElementOff"],
  "resource://devtools/client/inspector/markup/utils.js",
  true
);
loader.lazyRequireGetter(
  this,
  "ClassListPreviewer",
  "resource://devtools/client/inspector/rules/views/class-list-previewer.js"
);
loader.lazyRequireGetter(
  this,
  ["getNodeInfo", "getNodeCompatibilityInfo", "getRuleFromNode"],
  "resource://devtools/client/inspector/rules/utils/utils.js",
  true
);
loader.lazyRequireGetter(
  this,
  "StyleInspectorMenu",
  "resource://devtools/client/inspector/shared/style-inspector-menu.js"
);
loader.lazyRequireGetter(
  this,
  "AutocompletePopup",
  "resource://devtools/client/shared/autocomplete-popup.js"
);
loader.lazyRequireGetter(
  this,
  "KeyShortcuts",
  "resource://devtools/client/shared/key-shortcuts.js"
);
loader.lazyRequireGetter(
  this,
  "clipboardHelper",
  "resource://devtools/shared/platform/clipboard.js"
);

const HTML_NS = "http://www.w3.org/1999/xhtml";
const PREF_UA_STYLES = "devtools.inspector.showUserAgentStyles";
const PREF_DEFAULT_COLOR_UNIT = "devtools.defaultColorUnit";
const PREF_DRAGGABLE = "devtools.inspector.draggable_properties";
const PREF_INPLACE_EDITOR_FOCUS_NEXT_ON_ENTER =
  "devtools.inspector.rule-view.focusNextOnEnter";
const FILTER_CHANGED_TIMEOUT = 150;
// Removes the flash-out class from an element after 1 second.
const PROPERTY_FLASHING_DURATION = 1000;

// This is used to parse user input when filtering.
const FILTER_PROP_RE = /\s*([^:\s]*)\s*:\s*(.*?)\s*;?$/;
// This is used to parse the filter search value to see if the filter
// should be strict or not
const FILTER_STRICT_RE = /\s*`(.*?)`\s*$/;

const RULE_VIEW_HEADER_CLASSNAME = "ruleview-header";
const PSEUDO_ELEMENTS_CONTAINER_ID = "pseudo-elements-container";
const REGISTERED_PROPERTIES_CONTAINER_ID = "registered-properties-container";

/**
 * Our model looks like this:
 *
 * ElementStyle:
 *   Responsible for keeping track of which properties are overridden.
 *   Maintains a list of Rule objects that apply to the element.
 * Rule:
 *   Manages a single style declaration or rule.
 *   Responsible for applying changes to the properties in a rule.
 *   Maintains a list of TextProperty objects.
 * TextProperty:
 *   Manages a single property from the authoredText attribute of the
 *     relevant declaration.
 *   Maintains a list of computed properties that come from this
 *     property declaration.
 *   Changes to the TextProperty are sent to its related Rule for
 *     application.
 *
 * View hierarchy mostly follows the model hierarchy.
 *
 * CssRuleView:
 *   Owns an ElementStyle and creates a list of RuleEditors for its
 *    Rules.
 * RuleEditor:
 *   Owns a Rule object and creates a list of TextPropertyEditors
 *     for its TextProperties.
 *   Manages creation of new text properties.
 * TextPropertyEditor:
 *   Owns a TextProperty object.
 *   Manages changes to the TextProperty.
 *   Can be expanded to display computed properties.
 *   Can mark a property disabled or enabled.
 */

/**
 * CssRuleView is a view of the style rules and declarations that
 * apply to a given element.  After construction, the 'element'
 * property will be available with the user interface.
 *
 * @param {Inspector} inspector
 *        Inspector toolbox panel
 * @param {Document} document
 *        The document that will contain the rule view.
 * @param {Object} store
 *        The CSS rule view can use this object to store metadata
 *        that might outlast the rule view, particularly the current
 *        set of disabled properties.
 */
function CssRuleView(inspector, document, store) {
  EventEmitter.decorate(this);

  this.inspector = inspector;
  this.cssProperties = inspector.cssProperties;
  this.styleDocument = document;
  this.styleWindow = this.styleDocument.defaultView;
  this.store = store || {};

  // Allow tests to override debouncing behavior, as this can cause intermittents.
  this.debounce = debounce;

  // Variable used to stop the propagation of mouse events to children
  // when we are updating a value by dragging the mouse and we then release it
  this.childHasDragged = false;

  this._outputParser = new OutputParser(document, this.cssProperties);
  this._abortController = new this.styleWindow.AbortController();

  this._onAddRule = this._onAddRule.bind(this);
  this._onContextMenu = this._onContextMenu.bind(this);
  this._onCopy = this._onCopy.bind(this);
  this._onFilterStyles = this._onFilterStyles.bind(this);
  this._onClearSearch = this._onClearSearch.bind(this);
  this._onTogglePseudoClassPanel = this._onTogglePseudoClassPanel.bind(this);
  this._onTogglePseudoClass = this._onTogglePseudoClass.bind(this);
  this._onToggleClassPanel = this._onToggleClassPanel.bind(this);
  this._onToggleLightColorSchemeSimulation =
    this._onToggleLightColorSchemeSimulation.bind(this);
  this._onToggleDarkColorSchemeSimulation =
    this._onToggleDarkColorSchemeSimulation.bind(this);
  this._onTogglePrintSimulation = this._onTogglePrintSimulation.bind(this);
  this.highlightElementRule = this.highlightElementRule.bind(this);
  this.highlightProperty = this.highlightProperty.bind(this);
  this.refreshPanel = this.refreshPanel.bind(this);

  const doc = this.styleDocument;
  // Delegate bulk handling of events happening within the DOM tree of the Rules view
  // to this.handleEvent(). Listening on the capture phase of the event bubbling to be
  // able to stop event propagation on a case-by-case basis and prevent event target
  // ancestor nodes from handling them.
  this.styleDocument.addEventListener("click", this, { capture: true });
  this.element = doc.getElementById("ruleview-container-focusable");
  this.addRuleButton = doc.getElementById("ruleview-add-rule-button");
  this.searchField = doc.getElementById("ruleview-searchbox");
  this.searchClearButton = doc.getElementById("ruleview-searchinput-clear");
  this.pseudoClassPanel = doc.getElementById("pseudo-class-panel");
  this.pseudoClassToggle = doc.getElementById("pseudo-class-panel-toggle");
  this.classPanel = doc.getElementById("ruleview-class-panel");
  this.classToggle = doc.getElementById("class-panel-toggle");
  this.colorSchemeLightSimulationButton = doc.getElementById(
    "color-scheme-simulation-light-toggle"
  );
  this.colorSchemeDarkSimulationButton = doc.getElementById(
    "color-scheme-simulation-dark-toggle"
  );
  this.printSimulationButton = doc.getElementById("print-simulation-toggle");

  this._initSimulationFeatures();

  this.searchClearButton.hidden = true;

  this.onHighlighterShown = data =>
    this.handleHighlighterEvent("highlighter-shown", data);
  this.onHighlighterHidden = data =>
    this.handleHighlighterEvent("highlighter-hidden", data);
  this.inspector.highlighters.on("highlighter-shown", this.onHighlighterShown);
  this.inspector.highlighters.on(
    "highlighter-hidden",
    this.onHighlighterHidden
  );

  this.shortcuts = new KeyShortcuts({ window: this.styleWindow });
  this._onShortcut = this._onShortcut.bind(this);
  this.shortcuts.on("Escape", event => this._onShortcut("Escape", event));
  this.shortcuts.on("Return", event => this._onShortcut("Return", event));
  this.shortcuts.on("Space", event => this._onShortcut("Space", event));
  this.shortcuts.on("CmdOrCtrl+F", event =>
    this._onShortcut("CmdOrCtrl+F", event)
  );
  this.element.addEventListener("copy", this._onCopy);
  this.element.addEventListener("contextmenu", this._onContextMenu);
  this.addRuleButton.addEventListener("click", this._onAddRule);
  this.searchField.addEventListener("input", this._onFilterStyles);
  this.searchClearButton.addEventListener("click", this._onClearSearch);
  this.pseudoClassToggle.addEventListener(
    "click",
    this._onTogglePseudoClassPanel
  );
  this.classToggle.addEventListener("click", this._onToggleClassPanel);
  // The "change" event bubbles up from checkbox inputs nested within the panel container.
  this.pseudoClassPanel.addEventListener("change", this._onTogglePseudoClass);

  if (flags.testing) {
    // In tests, we start listening immediately to avoid having to simulate a mousemove.
    this.highlighters.addToView(this);
  } else {
    this.element.addEventListener(
      "mousemove",
      () => {
        this.highlighters.addToView(this);
      },
      { once: true }
    );
  }

  this._handlePrefChange = this._handlePrefChange.bind(this);
  this._handleUAStylePrefChange = this._handleUAStylePrefChange.bind(this);
  this._handleDefaultColorUnitPrefChange =
    this._handleDefaultColorUnitPrefChange.bind(this);
  this._handleDraggablePrefChange = this._handleDraggablePrefChange.bind(this);
  this._handleInplaceEditorFocusNextOnEnterPrefChange =
    this._handleInplaceEditorFocusNextOnEnterPrefChange.bind(this);

  this._prefObserver = new PrefObserver("devtools.");
  this._prefObserver.on(PREF_UA_STYLES, this._handleUAStylePrefChange);
  this._prefObserver.on(
    PREF_DEFAULT_COLOR_UNIT,
    this._handleDefaultColorUnitPrefChange
  );
  this._prefObserver.on(PREF_DRAGGABLE, this._handleDraggablePrefChange);
  // Initialize value of this.draggablePropertiesEnabled
  this._handleDraggablePrefChange();

  this._prefObserver.on(
    PREF_INPLACE_EDITOR_FOCUS_NEXT_ON_ENTER,
    this._handleInplaceEditorFocusNextOnEnterPrefChange
  );
  // Initialize value of this.inplaceEditorFocusNextOnEnter
  this._handleInplaceEditorFocusNextOnEnterPrefChange();

  this.pseudoClassCheckboxes = this._createPseudoClassCheckboxes();
  this.showUserAgentStyles = Services.prefs.getBoolPref(PREF_UA_STYLES);

  // Add the tooltips and highlighters to the view
  this.tooltips = new TooltipsOverlay(this);

  this.cssRegisteredPropertiesByTarget = new Map();
}

CssRuleView.prototype = {
  // The element that we're inspecting.
  _viewedElement: null,

  // Used for cancelling timeouts in the style filter.
  _filterChangedTimeout: null,

  // Empty, unconnected element of the same type as this node, used
  // to figure out how shorthand properties will be parsed.
  _dummyElement: null,

  get popup() {
    if (!this._popup) {
      // The popup will be attached to the toolbox document.
      this._popup = new AutocompletePopup(this.inspector.toolbox.doc, {
        autoSelect: true,
      });
    }

    return this._popup;
  },

  get classListPreviewer() {
    if (!this._classListPreviewer) {
      this._classListPreviewer = new ClassListPreviewer(
        this.inspector,
        this.classPanel
      );
    }

    return this._classListPreviewer;
  },

  get contextMenu() {
    if (!this._contextMenu) {
      this._contextMenu = new StyleInspectorMenu(this, { isRuleView: true });
    }

    return this._contextMenu;
  },

  // Get the dummy elemenet.
  get dummyElement() {
    return this._dummyElement;
  },

  // Get the highlighters overlay from the Inspector.
  get highlighters() {
    if (!this._highlighters) {
      // highlighters is a lazy getter in the inspector.
      this._highlighters = this.inspector.highlighters;
    }

    return this._highlighters;
  },

  // Get the filter search value.
  get searchValue() {
    return this.searchField.value.toLowerCase();
  },

  get rules() {
    return this._elementStyle ? this._elementStyle.rules : [];
  },

  get currentTarget() {
    return this.inspector.toolbox.target;
  },

  /**
   * Highlight/unhighlight all the nodes that match a given rule's selector
   * inside the document of the current selected node.
   * Only one selector can be highlighted at a time, so calling the method a
   * second time with a different rule will first unhighlight the previously
   * highlighted nodes.
   * Calling the method a second time with the same rule will just
   * unhighlight the highlighted nodes.
   *
   * @param {Rule} rule
   * @param {String} selector
   *        Elements matching this selector will be highlighted on the page.
   * @param {Boolean} highlightFromRulesSelector
   */
  async toggleSelectorHighlighter(
    rule,
    selector,
    highlightFromRulesSelector = true
  ) {
    if (this.isSelectorHighlighted(selector)) {
      await this.inspector.highlighters.hideHighlighterType(
        this.inspector.highlighters.TYPES.SELECTOR
      );
    } else {
      const options = {
        hideInfoBar: true,
        hideGuides: true,
        // we still pass the selector (which can be the StyleRuleFront#computedSelector)
        // even if highlightFromRulesSelector is set to true, as it's how we keep track
        // of which selector is highlighted.
        selector,
      };
      if (highlightFromRulesSelector) {
        options.ruleActorID = rule.domRule.actorID;
      }
      await this.inspector.highlighters.showHighlighterTypeForNode(
        this.inspector.highlighters.TYPES.SELECTOR,
        this.inspector.selection.nodeFront,
        options
      );
    }
  },

  isPanelVisible() {
    return (
      this.inspector.toolbox &&
      this.inspector.sidebar &&
      this.inspector.toolbox.currentToolId === "inspector" &&
      (this.inspector.sidebar.getCurrentTabID() == "ruleview" ||
        this.inspector.is3PaneModeEnabled)
    );
  },

  /**
   * Check whether a SelectorHighlighter is active for the given selector text.
   *
   * @param {String} selector
   * @return {Boolean}
   */
  isSelectorHighlighted(selector) {
    const options = this.inspector.highlighters.getOptionsForActiveHighlighter(
      this.inspector.highlighters.TYPES.SELECTOR
    );

    return options?.selector === selector;
  },

  /**
   * Delegate handler for events happening within the DOM tree of the Rules view.
   * Itself delegates to specific handlers by event type.
   *
   * Use this instead of attaching specific event handlers when:
   * - there are many elements with the same event handler (eases memory pressure)
   * - you want to avoid having to remove event handlers manually
   * - elements are added/removed from the DOM tree arbitrarily over time
   *
   * @param {MouseEvent|UIEvent} event
   */
  handleEvent(event) {
    if (this.childHasDragged) {
      this.childHasDragged = false;
      event.stopPropagation();
      return;
    }
    switch (event.type) {
      case "click":
        this.handleClickEvent(event);
        break;
      default:
    }
  },

  /**
   * Delegate handler for click events happening within the DOM tree of the Rules view.
   * Stop propagation of click event wrapping a CSS rule or CSS declaration to avoid
   * triggering the prompt to add a new CSS declaration or to edit the existing one.
   *
   * @param {MouseEvent} event
   */
  async handleClickEvent(event) {
    const target = event.target;

    // Handle click on the icon next to a CSS selector.
    if (target.classList.contains("js-toggle-selector-highlighter")) {
      event.stopPropagation();
      let selector = target.dataset.computedSelector;
      const highlightFromRulesSelector =
        !!selector && !target.dataset.isUniqueSelector;
      // dataset.computedSelector will be initially empty for inline styles (inherited or not)
      // Rules associated with a regular selector should have this data-attribute
      // set in devtools/client/inspector/rules/views/rule-editor.js
      const rule = getRuleFromNode(target, this._elementStyle);
      if (selector === "") {
        try {
          if (rule.inherited) {
            // This is an inline style from an inherited rule. Need to resolve the
            // unique selector from the node which this rule is inherited from.
            selector = await rule.inherited.getUniqueSelector();
          } else {
            // This is an inline style from the current node.
            selector =
              await this.inspector.selection.nodeFront.getUniqueSelector();
          }

          // Now that the selector was computed, we can store it for subsequent usage.
          target.dataset.computedSelector = selector;
          target.dataset.isUniqueSelector = true;
        } finally {
          // Could not resolve a unique selector for the inline style.
        }
      }

      this.toggleSelectorHighlighter(
        rule,
        selector,
        highlightFromRulesSelector
      );
    }

    // Handle click on swatches next to flex and inline-flex CSS properties
    if (target.classList.contains("js-toggle-flexbox-highlighter")) {
      event.stopPropagation();
      this.inspector.highlighters.toggleFlexboxHighlighter(
        this.inspector.selection.nodeFront,
        "rule"
      );
    }

    // Handle click on swatches next to grid CSS properties
    if (target.classList.contains("js-toggle-grid-highlighter")) {
      event.stopPropagation();
      this.inspector.highlighters.toggleGridHighlighter(
        this.inspector.selection.nodeFront,
        "rule"
      );
    }
  },

  /**
   * Delegate handler for highlighter events.
   *
   * This is the place to observe for highlighter events, check the highlighter type and
   * event name, then react to specific events, for example by modifying the DOM.
   *
   * @param {String} eventName
   *        Highlighter event name. One of: "highlighter-hidden", "highlighter-shown"
   * @param {Object} data
   *        Object with data associated with the highlighter event.
   */
  handleHighlighterEvent(eventName, data) {
    switch (data.type) {
      // Toggle the "highlighted" class on selector icons in the Rules view when
      // the SelectorHighlighter is shown/hidden for a certain CSS selector.
      case this.inspector.highlighters.TYPES.SELECTOR:
        {
          const selector = data?.options?.selector;
          if (!selector) {
            return;
          }

          const query = `.js-toggle-selector-highlighter[data-computed-selector='${selector}']`;
          for (const node of this.styleDocument.querySelectorAll(query)) {
            const isHighlighterDisplayed = eventName == "highlighter-shown";
            node.classList.toggle("highlighted", isHighlighterDisplayed);
            node.setAttribute("aria-pressed", isHighlighterDisplayed);
          }
        }
        break;

      // Toggle the "aria-pressed" attribute on swatches next to flex and inline-flex CSS properties
      // when the FlexboxHighlighter is shown/hidden for the currently selected node.
      case this.inspector.highlighters.TYPES.FLEXBOX:
        {
          const query = ".js-toggle-flexbox-highlighter";
          for (const node of this.styleDocument.querySelectorAll(query)) {
            node.setAttribute("aria-pressed", eventName == "highlighter-shown");
          }
        }
        break;

      // Toggle the "aria-pressed" class on swatches next to grid CSS properties
      // when the GridHighlighter is shown/hidden for the currently selected node.
      case this.inspector.highlighters.TYPES.GRID:
        {
          const query = ".js-toggle-grid-highlighter";
          for (const node of this.styleDocument.querySelectorAll(query)) {
            // From the Layout panel, we can toggle grid highlighters for nodes which are
            // not currently selected. The Rules view shows `display: grid` declarations
            // only for the selected node. Avoid mistakenly marking them as "active".
            if (data.nodeFront === this.inspector.selection.nodeFront) {
              node.setAttribute(
                "aria-pressed",
                eventName == "highlighter-shown"
              );
            }

            // When the max limit of grid highlighters is reached (default 3),
            // mark inactive grid swatches as disabled.
            node.toggleAttribute(
              "disabled",
              !this.inspector.highlighters.canGridHighlighterToggle(
                this.inspector.selection.nodeFront
              )
            );
          }
        }
        break;
    }
  },

  /**
   * Enables the print and color scheme simulation only for local and remote tab debugging.
   */
  async _initSimulationFeatures() {
    if (!this.inspector.commands.descriptorFront.isTabDescriptor) {
      return;
    }
    this.colorSchemeLightSimulationButton.removeAttribute("hidden");
    this.colorSchemeDarkSimulationButton.removeAttribute("hidden");
    this.printSimulationButton.removeAttribute("hidden");
    this.printSimulationButton.addEventListener(
      "click",
      this._onTogglePrintSimulation
    );
    this.colorSchemeLightSimulationButton.addEventListener(
      "click",
      this._onToggleLightColorSchemeSimulation
    );
    this.colorSchemeDarkSimulationButton.addEventListener(
      "click",
      this._onToggleDarkColorSchemeSimulation
    );
    const { rfpCSSColorScheme } = this.inspector.walker;
    if (rfpCSSColorScheme) {
      this.colorSchemeLightSimulationButton.setAttribute("disabled", true);
      this.colorSchemeDarkSimulationButton.setAttribute("disabled", true);
      console.warn("Color scheme simulation is disabled in RFP mode.");
    }
  },

  /**
   * Get the type of a given node in the rule-view
   *
   * @param {DOMNode} node
   *        The node which we want information about
   * @return {Object|null} containing the following props:
   * - type {String} One of the VIEW_NODE_XXX_TYPE const in
   *   client/inspector/shared/node-types.
   * - rule {Rule} The Rule object.
   * - value {Object} Depends on the type of the node.
   * Otherwise, returns null if the node isn't anything we care about.
   */
  getNodeInfo(node) {
    return getNodeInfo(node, this._elementStyle);
  },

  /**
   * Get the node's compatibility issues
   *
   * @param {DOMNode} node
   *        The node which we want information about
   * @return {Object|null} containing the following props:
   * - type {String} Compatibility issue type.
   * - property {string} The incompatible rule
   * - alias {Array} The browser specific alias of rule
   * - url {string} Link to MDN documentation
   * - deprecated {bool} True if the rule is deprecated
   * - experimental {bool} True if rule is experimental
   * - unsupportedBrowsers {Array} Array of unsupported browser
   * Otherwise, returns null if the node has cross-browser compatible CSS
   */
  async getNodeCompatibilityInfo(node) {
    const compatibilityInfo = await getNodeCompatibilityInfo(
      node,
      this._elementStyle
    );

    return compatibilityInfo;
  },

  /**
   * Context menu handler.
   */
  _onContextMenu(event) {
    if (
      event.originalTarget.closest("input[type=text]") ||
      event.originalTarget.closest("input:not([type])") ||
      event.originalTarget.closest("textarea")
    ) {
      return;
    }

    event.stopPropagation();
    event.preventDefault();

    this.contextMenu.show(event);
  },

  /**
   * Callback for copy event. Copy the selected text.
   *
   * @param {Event} event
   *        copy event object.
   */
  _onCopy(event) {
    if (event) {
      this.copySelection(event.target);
      event.preventDefault();
      event.stopPropagation();
    }
  },

  /**
   * Copy the current selection. The current target is necessary
   * if the selection is inside an input or a textarea
   *
   * @param {DOMNode} target
   *        DOMNode target of the copy action
   */
  copySelection(target) {
    try {
      let text = "";

      const nodeName = target?.nodeName;
      const targetType = target?.type;

      if (
        // The target can be the enable/disable rule checkbox here (See Bug 1680893).
        (nodeName === "input" && targetType !== "checkbox") ||
        nodeName == "textarea"
      ) {
        const start = Math.min(target.selectionStart, target.selectionEnd);
        const end = Math.max(target.selectionStart, target.selectionEnd);
        const count = end - start;
        text = target.value.substr(start, count);
      } else {
        text = this.styleWindow.getSelection().toString();

        // Remove any double newlines.
        text = text.replace(/(\r?\n)\r?\n/g, "$1");
      }

      clipboardHelper.copyString(text);
    } catch (e) {
      console.error(e);
    }
  },

  /**
   * Add a new rule to the current element.
   */
  async _onAddRule() {
    const elementStyle = this._elementStyle;
    const element = elementStyle.element;
    const pseudoClasses = element.pseudoClassLocks;

    // Clear the search input so the new rule is visible
    this._onClearSearch();

    this._focusNextUserAddedRule = true;
    this.pageStyle.addNewRule(element, pseudoClasses);
  },

  /**
   * Disables add rule button when needed
   */
  refreshAddRuleButtonState() {
    const shouldBeDisabled =
      !this._viewedElement ||
      !this.inspector.selection.isElementNode() ||
      this.inspector.selection.isAnonymousNode();
    this.addRuleButton.disabled = shouldBeDisabled;
  },

  /**
   * Return {Boolean} true if the rule view currently has an input
   * editor visible.
   */
  get isEditing() {
    return (
      this.tooltips.isEditing ||
      !!this.element.querySelectorAll(".styleinspector-propertyeditor").length
    );
  },

  _handleUAStylePrefChange() {
    this.showUserAgentStyles = Services.prefs.getBoolPref(PREF_UA_STYLES);
    this._handlePrefChange(PREF_UA_STYLES);
  },

  _handleDefaultColorUnitPrefChange() {
    this._handlePrefChange(PREF_DEFAULT_COLOR_UNIT);
  },

  _handleDraggablePrefChange() {
    this.draggablePropertiesEnabled = Services.prefs.getBoolPref(
      PREF_DRAGGABLE,
      false
    );
    // This event is consumed by text-property-editor instances in order to
    // update their draggable behavior. Preferences observer are costly, so
    // we are forwarding the preference update via the EventEmitter.
    this.emit("draggable-preference-updated");
  },

  _handleInplaceEditorFocusNextOnEnterPrefChange() {
    this.inplaceEditorFocusNextOnEnter = Services.prefs.getBoolPref(
      PREF_INPLACE_EDITOR_FOCUS_NEXT_ON_ENTER,
      false
    );
    this._handlePrefChange(PREF_INPLACE_EDITOR_FOCUS_NEXT_ON_ENTER);
  },

  _handlePrefChange(pref) {
    // Reselect the currently selected element
    const refreshOnPrefs = [
      PREF_UA_STYLES,
      PREF_DEFAULT_COLOR_UNIT,
      PREF_INPLACE_EDITOR_FOCUS_NEXT_ON_ENTER,
    ];
    if (this._viewedElement && refreshOnPrefs.includes(pref)) {
      this.selectElement(this._viewedElement, true);
    }
  },

  /**
   * Set the filter style search value.
   * @param {String} value
   *        The search value.
   */
  setFilterStyles(value = "") {
    this.searchField.value = value;
    this.searchField.focus();
    this._onFilterStyles();
  },

  /**
   * Called when the user enters a search term in the filter style search box.
   */
  _onFilterStyles() {
    if (this._filterChangedTimeout) {
      clearTimeout(this._filterChangedTimeout);
    }

    const filterTimeout = this.searchValue.length ? FILTER_CHANGED_TIMEOUT : 0;
    this.searchClearButton.hidden = this.searchValue.length === 0;

    this._filterChangedTimeout = setTimeout(() => {
      this.searchData = {
        searchPropertyMatch: FILTER_PROP_RE.exec(this.searchValue),
        searchPropertyName: this.searchValue,
        searchPropertyValue: this.searchValue,
        strictSearchValue: "",
        strictSearchPropertyName: false,
        strictSearchPropertyValue: false,
        strictSearchAllValues: false,
      };

      if (this.searchData.searchPropertyMatch) {
        // Parse search value as a single property line and extract the
        // property name and value. If the parsed property name or value is
        // contained in backquotes (`), extract the value within the backquotes
        // and set the corresponding strict search for the property to true.
        if (FILTER_STRICT_RE.test(this.searchData.searchPropertyMatch[1])) {
          this.searchData.strictSearchPropertyName = true;
          this.searchData.searchPropertyName = FILTER_STRICT_RE.exec(
            this.searchData.searchPropertyMatch[1]
          )[1];
        } else {
          this.searchData.searchPropertyName =
            this.searchData.searchPropertyMatch[1];
        }

        if (FILTER_STRICT_RE.test(this.searchData.searchPropertyMatch[2])) {
          this.searchData.strictSearchPropertyValue = true;
          this.searchData.searchPropertyValue = FILTER_STRICT_RE.exec(
            this.searchData.searchPropertyMatch[2]
          )[1];
        } else {
          this.searchData.searchPropertyValue =
            this.searchData.searchPropertyMatch[2];
        }

        // Strict search for stylesheets will match the property line regex.
        // Extract the search value within the backquotes to be used
        // in the strict search for stylesheets in _highlightStyleSheet.
        if (FILTER_STRICT_RE.test(this.searchValue)) {
          this.searchData.strictSearchValue = FILTER_STRICT_RE.exec(
            this.searchValue
          )[1];
        }
      } else if (FILTER_STRICT_RE.test(this.searchValue)) {
        // If the search value does not correspond to a property line and
        // is contained in backquotes, extract the search value within the
        // backquotes and set the flag to perform a strict search for all
        // the values (selector, stylesheet, property and computed values).
        const searchValue = FILTER_STRICT_RE.exec(this.searchValue)[1];
        this.searchData.strictSearchAllValues = true;
        this.searchData.searchPropertyName = searchValue;
        this.searchData.searchPropertyValue = searchValue;
        this.searchData.strictSearchValue = searchValue;
      }

      this._clearHighlight(this.element);
      this._clearRules();
      this._createEditors();

      this.inspector.emit("ruleview-filtered");

      this._filterChangeTimeout = null;
    }, filterTimeout);
  },

  /**
   * Called when the user clicks on the clear button in the filter style search
   * box. Returns true if the search box is cleared and false otherwise.
   */
  _onClearSearch() {
    if (this.searchField.value) {
      this.setFilterStyles("");
      return true;
    }

    return false;
  },

  destroy() {
    this.isDestroyed = true;
    this.clear();

    this._dummyElement = null;
    // off handlers must have the same reference as their on handlers
    this._prefObserver.off(PREF_UA_STYLES, this._handleUAStylePrefChange);
    this._prefObserver.off(
      PREF_DEFAULT_COLOR_UNIT,
      this._handleDefaultColorUnitPrefChange
    );
    this._prefObserver.off(PREF_DRAGGABLE, this._handleDraggablePrefChange);
    this._prefObserver.off(
      PREF_INPLACE_EDITOR_FOCUS_NEXT_ON_ENTER,
      this._handleInplaceEditorFocusNextOnEnterPrefChange
    );
    this._prefObserver.destroy();

    this._outputParser = null;

    if (this._classListPreviewer) {
      this._classListPreviewer.destroy();
      this._classListPreviewer = null;
    }

    if (this._contextMenu) {
      this._contextMenu.destroy();
      this._contextMenu = null;
    }

    if (this._highlighters) {
      this._highlighters.removeFromView(this);
      this._highlighters = null;
    }

    // Clean-up for simulations.
    this.colorSchemeLightSimulationButton.removeEventListener(
      "click",
      this._onToggleLightColorSchemeSimulation
    );
    this.colorSchemeDarkSimulationButton.removeEventListener(
      "click",
      this._onToggleDarkColorSchemeSimulation
    );
    this.printSimulationButton.removeEventListener(
      "click",
      this._onTogglePrintSimulation
    );

    this.colorSchemeLightSimulationButton = null;
    this.colorSchemeDarkSimulationButton = null;
    this.printSimulationButton = null;

    this.tooltips.destroy();

    // Remove bound listeners
    this._abortController.abort();
    this._abortController = null;
    this.shortcuts.destroy();
    this.styleDocument.removeEventListener("click", this, { capture: true });
    this.element.removeEventListener("copy", this._onCopy);
    this.element.removeEventListener("contextmenu", this._onContextMenu);
    this.addRuleButton.removeEventListener("click", this._onAddRule);
    this.searchField.removeEventListener("input", this._onFilterStyles);
    this.searchClearButton.removeEventListener("click", this._onClearSearch);
    this.pseudoClassPanel.removeEventListener(
      "change",
      this._onTogglePseudoClass
    );
    this.pseudoClassToggle.removeEventListener(
      "click",
      this._onTogglePseudoClassPanel
    );
    this.classToggle.removeEventListener("click", this._onToggleClassPanel);
    this.inspector.highlighters.off(
      "highlighter-shown",
      this.onHighlighterShown
    );
    this.inspector.highlighters.off(
      "highlighter-hidden",
      this.onHighlighterHidden
    );

    this.searchField = null;
    this.searchClearButton = null;
    this.pseudoClassPanel = null;
    this.pseudoClassToggle = null;
    this.pseudoClassCheckboxes = null;
    this.classPanel = null;
    this.classToggle = null;

    this.inspector = null;
    this.styleDocument = null;
    this.styleWindow = null;

    if (this.element.parentNode) {
      this.element.remove();
    }

    if (this._elementStyle) {
      this._elementStyle.destroy();
    }

    if (this._popup) {
      this._popup.destroy();
      this._popup = null;
    }
  },

  /**
   * Mark the view as selecting an element, disabling all interaction, and
   * visually clearing the view after a few milliseconds to avoid confusion
   * about which element's styles the rule view shows.
   */
  _startSelectingElement() {
    this.element.classList.add("non-interactive");
  },

  /**
   * Mark the view as no longer selecting an element, re-enabling interaction.
   */
  _stopSelectingElement() {
    this.element.classList.remove("non-interactive");
  },

  /**
   * Update the view with a new selected element.
   *
   * @param {NodeActor} element
   *        The node whose style rules we'll inspect.
   * @param {Boolean} allowRefresh
   *        Update the view even if the element is the same as last time.
   */
  selectElement(element, allowRefresh = false) {
    const refresh = this._viewedElement === element;
    if (refresh && !allowRefresh) {
      return Promise.resolve(undefined);
    }

    if (this._popup && this.popup.isOpen) {
      this.popup.hidePopup();
    }

    this.clear(false);
    this._viewedElement = element;

    this.clearPseudoClassPanel();
    this.refreshAddRuleButtonState();

    if (!this._viewedElement) {
      this._stopSelectingElement();
      this._clearRules();
      this._showEmpty();
      this.refreshPseudoClassPanel();
      if (this.pageStyle) {
        this.pageStyle.off("stylesheet-updated", this.refreshPanel);
        this.pageStyle = null;
      }
      return Promise.resolve(undefined);
    }

    this.pageStyle = element.inspectorFront.pageStyle;
    this.pageStyle.on("stylesheet-updated", this.refreshPanel);

    // To figure out how shorthand properties are interpreted by the
    // engine, we will set properties on a dummy element and observe
    // how their .style attribute reflects them as computed values.
    const dummyElementPromise = Promise.resolve(this.styleDocument)
      .then(document => {
        // ::before and ::after do not have a namespaceURI
        const namespaceURI =
          this.element.namespaceURI || document.documentElement.namespaceURI;
        this._dummyElement = document.createElementNS(
          namespaceURI,
          this.element.tagName
        );
      })
      .catch(promiseWarn);

    const elementStyle = new ElementStyle(
      element,
      this,
      this.store,
      this.pageStyle,
      this.showUserAgentStyles
    );
    this._elementStyle = elementStyle;

    this._startSelectingElement();

    return dummyElementPromise
      .then(() => {
        if (this._elementStyle === elementStyle) {
          return this._populate();
        }
        return undefined;
      })
      .then(() => {
        if (this._elementStyle === elementStyle) {
          if (!refresh) {
            this.element.scrollTop = 0;
          }
          this._stopSelectingElement();
          this._elementStyle.onChanged = () => {
            this._changed();
          };
        }
      })
      .catch(e => {
        if (this._elementStyle === elementStyle) {
          this._stopSelectingElement();
          this._clearRules();
        }
        console.error(e);
      });
  },

  /**
   * Update the rules for the currently highlighted element.
   */
  refreshPanel() {
    // Ignore refreshes when the panel is hidden, or during editing or when no element is selected.
    if (!this.isPanelVisible() || this.isEditing || !this._elementStyle) {
      return Promise.resolve(undefined);
    }

    // Repopulate the element style once the current modifications are done.
    const promises = [];
    for (const rule of this._elementStyle.rules) {
      if (rule._applyingModifications) {
        promises.push(rule._applyingModifications);
      }
    }

    return Promise.all(promises).then(() => {
      return this._populate();
    });
  },

  /**
   * Clear the pseudo class options panel by removing the checked and disabled
   * attributes for each checkbox.
   */
  clearPseudoClassPanel() {
    this.pseudoClassCheckboxes.forEach(checkbox => {
      checkbox.checked = false;
      checkbox.disabled = false;
    });
  },

  /**
   * For each item in PSEUDO_CLASSES, create a checkbox input element for toggling a
   * pseudo-class on the selected element and append it to the pseudo-class panel.
   *
   * Returns an array with the checkbox input elements for pseudo-classes.
   *
   * @return {Array}
   */
  _createPseudoClassCheckboxes() {
    const doc = this.styleDocument;
    const fragment = doc.createDocumentFragment();

    for (const pseudo of PSEUDO_CLASSES) {
      const label = doc.createElement("label");
      const checkbox = doc.createElement("input");
      checkbox.setAttribute("tabindex", "-1");
      checkbox.setAttribute("type", "checkbox");
      checkbox.setAttribute("value", pseudo);

      label.append(checkbox, pseudo);
      fragment.append(label);
    }

    this.pseudoClassPanel.append(fragment);
    return Array.from(
      this.pseudoClassPanel.querySelectorAll("input[type=checkbox]")
    );
  },

  /**
   * Update the pseudo class options for the currently highlighted element.
   */
  refreshPseudoClassPanel() {
    if (!this._elementStyle || !this.inspector.selection.isElementNode()) {
      this.pseudoClassCheckboxes.forEach(checkbox => {
        checkbox.disabled = true;
      });
      return;
    }

    const pseudoClassLocks = this._elementStyle.element.pseudoClassLocks;
    this.pseudoClassCheckboxes.forEach(checkbox => {
      checkbox.disabled = false;
      checkbox.checked = pseudoClassLocks.includes(checkbox.value);
    });
  },

  _populate() {
    const elementStyle = this._elementStyle;
    return this._elementStyle
      .populate()
      .then(() => {
        if (this._elementStyle !== elementStyle || this.isDestroyed) {
          return null;
        }

        this._clearRules();
        const onEditorsReady = this._createEditors();
        this.refreshPseudoClassPanel();

        // Notify anyone that cares that we refreshed.
        return onEditorsReady.then(() => {
          this.emit("ruleview-refreshed");
        }, console.error);
      })
      .catch(promiseWarn);
  },

  /**
   * Show the user that the rule view has no node selected.
   */
  _showEmpty() {
    if (this.styleDocument.getElementById("ruleview-no-results")) {
      return;
    }

    createChild(this.element, "div", {
      id: "ruleview-no-results",
      class: "devtools-sidepanel-no-result",
      textContent: l10n("rule.empty"),
    });
  },

  /**
   * Clear the rules.
   */
  _clearRules() {
    this.element.innerHTML = "";
  },

  /**
   * Clear the rule view.
   */
  clear(clearDom = true) {
    if (clearDom) {
      this._clearRules();
    }
    this._viewedElement = null;

    if (this._elementStyle) {
      this._elementStyle.destroy();
      this._elementStyle = null;
    }

    if (this.pageStyle) {
      this.pageStyle.off("stylesheet-updated", this.refreshPanel);
      this.pageStyle = null;
    }
  },

  /**
   * Called when the user has made changes to the ElementStyle.
   * Emits an event that clients can listen to.
   */
  _changed() {
    this.emit("ruleview-changed");
  },

  /**
   * Text for header that shows above rules for this element
   */
  get selectedElementLabel() {
    if (this._selectedElementLabel) {
      return this._selectedElementLabel;
    }
    this._selectedElementLabel = l10n("rule.selectedElement");
    return this._selectedElementLabel;
  },

  /**
   * Text for header that shows above rules for pseudo elements
   */
  get pseudoElementLabel() {
    if (this._pseudoElementLabel) {
      return this._pseudoElementLabel;
    }
    this._pseudoElementLabel = l10n("rule.pseudoElement");
    return this._pseudoElementLabel;
  },

  get showPseudoElements() {
    if (this._showPseudoElements === undefined) {
      this._showPseudoElements = Services.prefs.getBoolPref(
        "devtools.inspector.show_pseudo_elements"
      );
    }
    return this._showPseudoElements;
  },

  /**
   * Creates an expandable container in the rule view
   *
   * @param  {String} label
   *         The label for the container header
   * @param  {String} containerId
   *         The id that will be set on the container
   * @param  {Boolean} isPseudo
   *         Whether or not the container will hold pseudo element rules
   * @return {DOMNode} The container element
   */
  createExpandableContainer(label, containerId, isPseudo = false) {
    const header = this.styleDocument.createElementNS(HTML_NS, "div");
    header.classList.add(
      RULE_VIEW_HEADER_CLASSNAME,
      "ruleview-expandable-header"
    );
    header.setAttribute("role", "heading");

    const toggleButton = this.styleDocument.createElementNS(HTML_NS, "button");
    toggleButton.setAttribute(
      "title",
      l10n("rule.expandableContainerToggleButton.title")
    );
    toggleButton.setAttribute("aria-expanded", "true");
    toggleButton.setAttribute("aria-controls", containerId);

    const twisty = this.styleDocument.createElementNS(HTML_NS, "span");
    twisty.className = "ruleview-expander theme-twisty";

    toggleButton.append(twisty, this.styleDocument.createTextNode(label));
    header.append(toggleButton);

    const container = this.styleDocument.createElementNS(HTML_NS, "div");
    container.id = containerId;
    container.classList.add("ruleview-expandable-container");
    container.hidden = false;

    this.element.append(header, container);

    toggleButton.addEventListener("click", () => {
      this._toggleContainerVisibility(
        toggleButton,
        container,
        isPseudo,
        !this.showPseudoElements
      );
    });

    if (isPseudo) {
      this._toggleContainerVisibility(
        toggleButton,
        container,
        isPseudo,
        this.showPseudoElements
      );
    }

    return container;
  },

  /**
   * Create the `@property` expandable container
   *
   * @returns {Element}
   */
  createRegisteredPropertiesExpandableContainer() {
    const el = this.createExpandableContainer(
      "@property",
      REGISTERED_PROPERTIES_CONTAINER_ID
    );
    el.classList.add("registered-properties");
    return el;
  },

  /**
   * Return the RegisteredPropertyEditor element for a given property name
   *
   * @param {String} registeredPropertyName
   * @returns {Element|null}
   */
  getRegisteredPropertyElement(registeredPropertyName) {
    return this.styleDocument.querySelector(
      `#${REGISTERED_PROPERTIES_CONTAINER_ID} [data-name="${registeredPropertyName}"]`
    );
  },

  /**
   * Toggle the visibility of an expandable container
   *
   * @param  {DOMNode}  twisty
   *         Clickable toggle DOM Node
   * @param  {DOMNode}  container
   *         Expandable container DOM Node
   * @param  {Boolean}  isPseudo
   *         Whether or not the container will hold pseudo element rules
   * @param  {Boolean}  showPseudo
   *         Whether or not pseudo element rules should be displayed
   */
  _toggleContainerVisibility(toggleButton, container, isPseudo, showPseudo) {
    let isOpen = toggleButton.getAttribute("aria-expanded") === "true";

    if (isPseudo) {
      this._showPseudoElements = !!showPseudo;

      Services.prefs.setBoolPref(
        "devtools.inspector.show_pseudo_elements",
        this.showPseudoElements
      );

      container.hidden = !this.showPseudoElements;
      isOpen = !this.showPseudoElements;
    } else {
      container.hidden = !container.hidden;
    }

    toggleButton.setAttribute("aria-expanded", !isOpen);
  },

  /**
   * Creates editor UI for each of the rules in _elementStyle.
   */
  // eslint-disable-next-line complexity
  _createEditors() {
    // Run through the current list of rules, attaching
    // their editors in order.  Create editors if needed.
    let lastInherited = null;
    let lastinheritedSectionLabel = "";
    let seenNormalElement = false;
    let seenSearchTerm = false;
    const containers = new Map();

    if (!this._elementStyle.rules) {
      return Promise.resolve();
    }

    const editorReadyPromises = [];
    for (const rule of this._elementStyle.rules) {
      if (rule.domRule.system) {
        continue;
      }

      // Initialize rule editor
      if (!rule.editor) {
        rule.editor = new RuleEditor(this, rule);
        editorReadyPromises.push(rule.editor.once("source-link-updated"));
      }

      // Filter the rules and highlight any matches if there is a search input
      if (this.searchValue && this.searchData) {
        if (this.highlightRule(rule)) {
          seenSearchTerm = true;
        } else if (rule.domRule.type !== ELEMENT_STYLE) {
          continue;
        }
      }

      const isNonInheritedPseudo = !!rule.pseudoElement && !rule.inherited;

      // Only print header for this element if there are pseudo elements
      if (
        containers.has(PSEUDO_ELEMENTS_CONTAINER_ID) &&
        !seenNormalElement &&
        !rule.pseudoElement
      ) {
        seenNormalElement = true;
        const div = this.styleDocument.createElementNS(HTML_NS, "div");
        div.className = RULE_VIEW_HEADER_CLASSNAME;
        div.setAttribute("role", "heading");
        div.textContent = this.selectedElementLabel;
        this.element.appendChild(div);
      }

      const { inherited, inheritedSectionLabel } = rule;
      // We need to check both `inherited` (a NodeFront) and `inheritedSectionLabel` (string),
      // as element-backed pseudo element rules (e.g. `::details-content`) can have the same
      // `inherited` property as a regular rule (e.g. on `<details>`), but the element is
      // to be considered as a child of the binding element.
      // e.g. we want to have
      // This element
      // Inherited by details::details-content
      // Inherited by details
      if (
        inherited &&
        (inherited !== lastInherited ||
          inheritedSectionLabel !== lastinheritedSectionLabel)
      ) {
        const div = this.styleDocument.createElementNS(HTML_NS, "div");
        div.classList.add(
          RULE_VIEW_HEADER_CLASSNAME,
          "ruleview-header-inherited"
        );
        div.setAttribute("role", "heading");
        div.setAttribute("aria-level", "3");
        div.textContent = rule.inheritedSectionLabel;
        lastInherited = inherited;
        lastinheritedSectionLabel = inheritedSectionLabel;
        this.element.appendChild(div);
      }

      const keyframes = rule.keyframes;

      let containerKey = null;

      // Don't display inherited pseudo element rules (e.g. ::details-content) inside
      // the pseudo element container
      if (isNonInheritedPseudo) {
        containerKey = PSEUDO_ELEMENTS_CONTAINER_ID;
        if (!containers.has(containerKey)) {
          containers.set(
            containerKey,
            this.createExpandableContainer(
              this.pseudoElementLabel,
              containerKey,
              true
            )
          );
        }
      } else if (keyframes) {
        containerKey = keyframes;
        if (!containers.has(containerKey)) {
          containers.set(
            containerKey,
            this.createExpandableContainer(
              rule.keyframesName,
              `keyframes-container-${keyframes.name}`
            )
          );
        }
      }

      rule.editor.element.setAttribute("role", "article");
      const container = containers.get(containerKey);
      if (container) {
        container.appendChild(rule.editor.element);
      } else {
        this.element.appendChild(rule.editor.element);
      }

      // Automatically select the selector input when we are adding a user-added rule
      if (this._focusNextUserAddedRule && rule.domRule.userAdded) {
        this._focusNextUserAddedRule = null;
        rule.editor.selectorText.click();
        this.emitForTests("new-rule-added", rule);
      }
    }

    const targetRegisteredProperties =
      this.getRegisteredPropertiesForSelectedNodeTarget();
    if (targetRegisteredProperties?.size) {
      const registeredPropertiesContainer =
        this.createRegisteredPropertiesExpandableContainer();

      // Sort properties by their name, as we want to display them in alphabetical order
      const propertyDefinitions = Array.from(
        targetRegisteredProperties.values()
      ).sort((a, b) => (a.name < b.name ? -1 : 1));
      for (const propertyDefinition of propertyDefinitions) {
        const registeredPropertyEditor = new RegisteredPropertyEditor(
          this,
          propertyDefinition
        );

        registeredPropertiesContainer.appendChild(
          registeredPropertyEditor.element
        );
      }
    }

    const searchBox = this.searchField.parentNode;
    searchBox.classList.toggle(
      "devtools-searchbox-no-match",
      this.searchValue && !seenSearchTerm
    );

    return Promise.all(editorReadyPromises);
  },

  /**
   * Highlight rules that matches the filter search value and returns a
   * boolean indicating whether or not rules were highlighted.
   *
   * @param  {Rule} rule
   *         The rule object we're highlighting if its rule selectors or
   *         property values match the search value.
   * @return {Boolean} true if the rule was highlighted, false otherwise.
   */
  highlightRule(rule) {
    const isRuleSelectorHighlighted = this._highlightRuleSelector(rule);
    const isStyleSheetHighlighted = this._highlightStyleSheet(rule);
    const isAncestorRulesHighlighted = this._highlightAncestorRules(rule);
    let isHighlighted =
      isRuleSelectorHighlighted ||
      isStyleSheetHighlighted ||
      isAncestorRulesHighlighted;

    // Highlight search matches in the rule properties
    for (const textProp of rule.textProps) {
      if (!textProp.invisible && this._highlightProperty(textProp.editor)) {
        isHighlighted = true;
      }
    }

    return isHighlighted;
  },

  /**
   * Highlights the rule selector that matches the filter search value and
   * returns a boolean indicating whether or not the selector was highlighted.
   *
   * @param  {Rule} rule
   *         The Rule object.
   * @return {Boolean} true if the rule selector was highlighted,
   *         false otherwise.
   */
  _highlightRuleSelector(rule) {
    let isSelectorHighlighted = false;

    let selectorNodes = [...rule.editor.selectorText.childNodes];
    if (rule.domRule.type === CSSRule.KEYFRAME_RULE) {
      selectorNodes = [rule.editor.selectorText];
    } else if (rule.domRule.type === ELEMENT_STYLE) {
      selectorNodes = [];
    }

    // Highlight search matches in the rule selectors
    for (const selectorNode of selectorNodes) {
      const selector = selectorNode.textContent.toLowerCase();
      if (
        (this.searchData.strictSearchAllValues &&
          selector === this.searchData.strictSearchValue) ||
        (!this.searchData.strictSearchAllValues &&
          selector.includes(this.searchValue))
      ) {
        selectorNode.classList.add("ruleview-highlight");
        isSelectorHighlighted = true;
      }
    }

    return isSelectorHighlighted;
  },

  /**
   * Highlights the ancestor rules data (@media / @layer) that matches the filter search
   * value and returns a boolean indicating whether or not element was highlighted.
   *
   * @return {Boolean} true if the element was highlighted, false otherwise.
   */
  _highlightAncestorRules(rule) {
    const element = rule.editor.ancestorDataEl;
    if (!element) {
      return false;
    }

    const ancestorSelectors = element.querySelectorAll(
      ".ruleview-rule-ancestor-selectorcontainer"
    );

    let isHighlighted = false;
    for (const child of ancestorSelectors) {
      const dataText = child.innerText.toLowerCase();
      const matches = this.searchData.strictSearchValue
        ? dataText === this.searchData.strictSearchValue
        : dataText.includes(this.searchValue);
      if (matches) {
        isHighlighted = true;
        child.classList.add("ruleview-highlight");
      }
    }

    return isHighlighted;
  },

  /**
   * Highlights the stylesheet source that matches the filter search value and
   * returns a boolean indicating whether or not the stylesheet source was
   * highlighted.
   *
   * @return {Boolean} true if the stylesheet source was highlighted, false
   *         otherwise.
   */
  _highlightStyleSheet(rule) {
    const styleSheetSource = rule.title.toLowerCase();
    const isStyleSheetHighlighted = this.searchData.strictSearchValue
      ? styleSheetSource === this.searchData.strictSearchValue
      : styleSheetSource.includes(this.searchValue);

    if (isStyleSheetHighlighted) {
      rule.editor.source.classList.add("ruleview-highlight");
    }

    return isStyleSheetHighlighted;
  },

  /**
   * Highlights the rule properties and computed properties that match the
   * filter search value and returns a boolean indicating whether or not the
   * property or computed property was highlighted.
   *
   * @param  {TextPropertyEditor} editor
   *         The rule property TextPropertyEditor object.
   * @return {Boolean} true if the property or computed property was
   *         highlighted, false otherwise.
   */
  _highlightProperty(editor) {
    const isPropertyHighlighted = this._highlightRuleProperty(editor);
    const isComputedHighlighted = this._highlightComputedProperty(editor);

    // Expand the computed list if a computed property is highlighted and the
    // property rule is not highlighted
    if (
      !isPropertyHighlighted &&
      isComputedHighlighted &&
      !editor.computed.hasAttribute("user-open")
    ) {
      editor.expandForFilter();
    }

    return isPropertyHighlighted || isComputedHighlighted;
  },

  /**
   * Called when TextPropertyEditor is updated and updates the rule property
   * highlight.
   *
   * @param  {TextPropertyEditor} editor
   *         The rule property TextPropertyEditor object.
   */
  _updatePropertyHighlight(editor) {
    if (!this.searchValue || !this.searchData) {
      return;
    }

    this._clearHighlight(editor.element);

    if (this._highlightProperty(editor)) {
      this.searchField.classList.remove("devtools-style-searchbox-no-match");
    }
  },

  /**
   * Highlights the rule property that matches the filter search value
   * and returns a boolean indicating whether or not the property was
   * highlighted.
   *
   * @param  {TextPropertyEditor} editor
   *         The rule property TextPropertyEditor object.
   * @return {Boolean} true if the rule property was highlighted,
   *         false otherwise.
   */
  _highlightRuleProperty(editor) {
    // Get the actual property value displayed in the rule view
    const propertyName = editor.prop.name.toLowerCase();
    const propertyValue = editor.valueSpan.textContent.toLowerCase();

    return this._highlightMatches(
      editor.container,
      propertyName,
      propertyValue
    );
  },

  /**
   * Highlights the computed property that matches the filter search value and
   * returns a boolean indicating whether or not the computed property was
   * highlighted.
   *
   * @param  {TextPropertyEditor} editor
   *         The rule property TextPropertyEditor object.
   * @return {Boolean} true if the computed property was highlighted, false
   *         otherwise.
   */
  _highlightComputedProperty(editor) {
    let isComputedHighlighted = false;

    // Highlight search matches in the computed list of properties
    editor._populateComputed();
    for (const computed of editor.prop.computed) {
      if (computed.element) {
        // Get the actual property value displayed in the computed list
        const computedName = computed.name.toLowerCase();
        const computedValue = computed.parsedValue.toLowerCase();

        isComputedHighlighted = this._highlightMatches(
          computed.element,
          computedName,
          computedValue
        )
          ? true
          : isComputedHighlighted;
      }
    }

    return isComputedHighlighted;
  },

  /**
   * Helper function for highlightRules that carries out highlighting the given
   * element if the search terms match the property, and returns a boolean
   * indicating whether or not the search terms match.
   *
   * @param  {DOMNode} element
   *         The node to highlight if search terms match
   * @param  {String} propertyName
   *         The property name of a rule
   * @param  {String} propertyValue
   *         The property value of a rule
   * @return {Boolean} true if the given search terms match the property, false
   *         otherwise.
   */
  _highlightMatches(element, propertyName, propertyValue) {
    const {
      searchPropertyName,
      searchPropertyValue,
      searchPropertyMatch,
      strictSearchPropertyName,
      strictSearchPropertyValue,
      strictSearchAllValues,
    } = this.searchData;
    let matches = false;

    // If the inputted search value matches a property line like
    // `font-family: arial`, then check to make sure the name and value match.
    // Otherwise, just compare the inputted search string directly against the
    // name and value of the rule property.
    const hasNameAndValue =
      searchPropertyMatch && searchPropertyName && searchPropertyValue;
    const isMatch = (value, query, isStrict) => {
      return isStrict ? value === query : query && value.includes(query);
    };

    if (hasNameAndValue) {
      matches =
        isMatch(propertyName, searchPropertyName, strictSearchPropertyName) &&
        isMatch(propertyValue, searchPropertyValue, strictSearchPropertyValue);
    } else {
      matches =
        isMatch(
          propertyName,
          searchPropertyName,
          strictSearchPropertyName || strictSearchAllValues
        ) ||
        isMatch(
          propertyValue,
          searchPropertyValue,
          strictSearchPropertyValue || strictSearchAllValues
        );
    }

    if (matches) {
      element.classList.add("ruleview-highlight");
    }

    return matches;
  },

  /**
   * Clear all search filter highlights in the panel, and close the computed
   * list if toggled opened
   */
  _clearHighlight(element) {
    for (const el of element.querySelectorAll(".ruleview-highlight")) {
      el.classList.remove("ruleview-highlight");
    }

    for (const computed of element.querySelectorAll(
      ".ruleview-computedlist[filter-open]"
    )) {
      computed.parentNode._textPropertyEditor.collapseForFilter();
    }
  },

  /**
   * Called when the pseudo class panel button is clicked and toggles
   * the display of the pseudo class panel.
   */
  _onTogglePseudoClassPanel() {
    if (this.pseudoClassPanel.hidden) {
      this.showPseudoClassPanel();
    } else {
      this.hidePseudoClassPanel();
    }
  },

  showPseudoClassPanel() {
    this.hideClassPanel();

    this.pseudoClassToggle.setAttribute("aria-pressed", "true");
    this.pseudoClassCheckboxes.forEach(checkbox => {
      checkbox.setAttribute("tabindex", "0");
    });
    this.pseudoClassPanel.hidden = false;
  },

  hidePseudoClassPanel() {
    this.pseudoClassToggle.setAttribute("aria-pressed", "false");
    this.pseudoClassCheckboxes.forEach(checkbox => {
      checkbox.setAttribute("tabindex", "-1");
    });
    this.pseudoClassPanel.hidden = true;
  },

  /**
   * Called when a pseudo class checkbox is clicked and toggles
   * the pseudo class for the current selected element.
   */
  _onTogglePseudoClass(event) {
    const target = event.target;
    this.inspector.togglePseudoClass(target.value);
  },

  /**
   * Called when the class panel button is clicked and toggles the display of the class
   * panel.
   */
  _onToggleClassPanel() {
    if (this.classPanel.hidden) {
      this.showClassPanel();
    } else {
      this.hideClassPanel();
    }
  },

  showClassPanel() {
    this.hidePseudoClassPanel();

    this.classToggle.setAttribute("aria-pressed", "true");
    this.classPanel.hidden = false;

    this.classListPreviewer.focusAddClassField();
  },

  hideClassPanel() {
    this.classToggle.setAttribute("aria-pressed", "false");
    this.classPanel.hidden = true;
  },

  /**
   * Handle the keypress event in the rule view.
   */
  _onShortcut(name, event) {
    if (!event.target.closest("#sidebar-panel-ruleview")) {
      return;
    }

    if (name === "CmdOrCtrl+F") {
      this.searchField.focus();
      event.preventDefault();
    } else if (
      (name === "Return" || name === "Space") &&
      this.element.classList.contains("non-interactive")
    ) {
      event.preventDefault();
    } else if (
      name === "Escape" &&
      event.target === this.searchField &&
      this._onClearSearch()
    ) {
      // Handle the search box's keypress event. If the escape key is pressed,
      // clear the search box field.
      event.preventDefault();
      event.stopPropagation();
    }
  },

  async _onToggleLightColorSchemeSimulation() {
    const shouldSimulateLightScheme =
      this.colorSchemeLightSimulationButton.getAttribute("aria-pressed") !==
      "true";

    this.colorSchemeLightSimulationButton.setAttribute(
      "aria-pressed",
      shouldSimulateLightScheme
    );

    this.colorSchemeDarkSimulationButton.setAttribute("aria-pressed", "false");

    await this.inspector.commands.targetConfigurationCommand.updateConfiguration(
      {
        colorSchemeSimulation: shouldSimulateLightScheme ? "light" : null,
      }
    );
    // Refresh the current element's rules in the panel.
    this.refreshPanel();
  },

  async _onToggleDarkColorSchemeSimulation() {
    const shouldSimulateDarkScheme =
      this.colorSchemeDarkSimulationButton.getAttribute("aria-pressed") !==
      "true";

    this.colorSchemeDarkSimulationButton.setAttribute(
      "aria-pressed",
      shouldSimulateDarkScheme
    );

    this.colorSchemeLightSimulationButton.setAttribute("aria-pressed", "false");

    await this.inspector.commands.targetConfigurationCommand.updateConfiguration(
      {
        colorSchemeSimulation: shouldSimulateDarkScheme ? "dark" : null,
      }
    );
    // Refresh the current element's rules in the panel.
    this.refreshPanel();
  },

  async _onTogglePrintSimulation() {
    const enabled =
      this.printSimulationButton.getAttribute("aria-pressed") !== "true";
    this.printSimulationButton.setAttribute("aria-pressed", enabled);
    await this.inspector.commands.targetConfigurationCommand.updateConfiguration(
      {
        printSimulationEnabled: enabled,
      }
    );
    // Refresh the current element's rules in the panel.
    this.refreshPanel();
  },

  /**
   * Temporarily flash the given element.
   *
   * @param  {Element} element
   *         The element.
   */
  _flashElement(element) {
    flashElementOn(element, {
      backgroundClass: "theme-bg-contrast",
    });

    if (this._flashMutationTimer) {
      clearTimeout(this._removeFlashOutTimer);
      this._flashMutationTimer = null;
    }

    this._flashMutationTimer = setTimeout(() => {
      flashElementOff(element, {
        backgroundClass: "theme-bg-contrast",
      });

      // Emit "scrolled-to-property" for use by tests.
      this.emit("scrolled-to-element");
    }, PROPERTY_FLASHING_DURATION);
  },

  /**
   * Scrolls to the top of either the rule or declaration. The view will try to scroll to
   * the rule if both can fit in the viewport. If not, then scroll to the declaration.
   *
   * @param  {Element} rule
   *         The rule to scroll to.
   * @param  {Element|null} declaration
   *         Optional. The declaration to scroll to.
   * @param  {String} scrollBehavior
   *         Optional. The transition animation when scrolling. If prefers-reduced-motion
   *         system pref is set, then the scroll behavior will be overridden to "auto".
   */
  _scrollToElement(rule, declaration, scrollBehavior = "smooth") {
    let elementToScrollTo = rule;

    if (declaration) {
      const { offsetTop, offsetHeight } = declaration;
      // Get the distance between both the rule and declaration. If the distance is
      // greater than the height of the rule view, then only scroll to the declaration.
      const distance = offsetTop + offsetHeight - rule.offsetTop;

      if (this.element.parentNode.offsetHeight <= distance) {
        elementToScrollTo = declaration;
      }
    }

    // Ensure that smooth scrolling is disabled when the user prefers reduced motion.
    const win = elementToScrollTo.ownerGlobal;
    const reducedMotion = win.matchMedia("(prefers-reduced-motion)").matches;
    scrollBehavior = reducedMotion ? "auto" : scrollBehavior;

    elementToScrollTo.scrollIntoView({ behavior: scrollBehavior });
  },

  /**
   * Toggles the visibility of the pseudo element rule's container.
   */
  _togglePseudoElementRuleContainer() {
    const container = this.styleDocument.getElementById(
      PSEUDO_ELEMENTS_CONTAINER_ID
    );
    const toggle = this.styleDocument.querySelector(
      `[aria-controls="${PSEUDO_ELEMENTS_CONTAINER_ID}"]`
    );
    this._toggleContainerVisibility(toggle, container, true, true);
  },

  /**
   * Finds the rule with the matching actorID and highlights it.
   *
   * @param  {String} ruleId
   *         The actorID of the rule.
   */
  highlightElementRule(ruleId) {
    let scrollBehavior = "smooth";

    const rule = this.rules.find(r => r.domRule.actorID === ruleId);

    if (!rule) {
      return;
    }

    if (rule.domRule.actorID === ruleId) {
      // If using 2-Pane mode, then switch to the Rules tab first.
      if (!this.inspector.is3PaneModeEnabled) {
        this.inspector.sidebar.select("ruleview");
      }

      if (rule.pseudoElement.length && !this.showPseudoElements) {
        scrollBehavior = "auto";
        this._togglePseudoElementRuleContainer();
      }

      const {
        editor: { element },
      } = rule;

      // Scroll to the top of the rule and highlight it.
      this._scrollToElement(element, null, scrollBehavior);
      this._flashElement(element);
    }
  },

  /**
   * Finds the specified TextProperty name in the rule view. If found, scroll to and
   * flash the TextProperty.
   *
   * @param  {String} name
   *         The property name to scroll to and highlight.
   * @return {Boolean} true if the TextProperty name is found, and false otherwise.
   */
  highlightProperty(name) {
    for (const rule of this.rules) {
      for (const textProp of rule.textProps) {
        if (textProp.overridden || textProp.invisible || !textProp.enabled) {
          continue;
        }

        const {
          editor: { selectorText },
        } = rule;
        let scrollBehavior = "smooth";

        // First, search for a matching authored property.
        if (textProp.name === name) {
          // If using 2-Pane mode, then switch to the Rules tab first.
          if (!this.inspector.is3PaneModeEnabled) {
            this.inspector.sidebar.select("ruleview");
          }

          // If the property is being applied by a pseudo element rule, expand the pseudo
          // element list container.
          if (rule.pseudoElement.length && !this.showPseudoElements) {
            // Set the scroll behavior to "auto" to avoid timing issues between toggling
            // the pseudo element container and scrolling smoothly to the rule.
            scrollBehavior = "auto";
            this._togglePseudoElementRuleContainer();
          }

          // Scroll to the top of the property's rule so that both the property and its
          // rule are visible.
          this._scrollToElement(
            selectorText,
            textProp.editor.element,
            scrollBehavior
          );
          this._flashElement(textProp.editor.element);

          return true;
        }

        // If there is no matching property, then look in computed properties.
        for (const computed of textProp.computed) {
          if (computed.overridden) {
            continue;
          }

          if (computed.name === name) {
            if (!this.inspector.is3PaneModeEnabled) {
              this.inspector.sidebar.select("ruleview");
            }

            if (
              textProp.rule.pseudoElement.length &&
              !this.showPseudoElements
            ) {
              scrollBehavior = "auto";
              this._togglePseudoElementRuleContainer();
            }

            // Expand the computed list.
            textProp.editor.expandForFilter();

            this._scrollToElement(
              selectorText,
              computed.element,
              scrollBehavior
            );
            this._flashElement(computed.element);

            return true;
          }
        }
      }
    }

    return false;
  },

  /**
   * Returns a Map (keyed by name) of the registered
   * properties for the currently selected node document.
   *
   * @returns Map<String, Object>|null
   */
  getRegisteredPropertiesForSelectedNodeTarget() {
    return this.cssRegisteredPropertiesByTarget.get(
      this.inspector.selection.nodeFront.targetFront
    );
  },
};

class RuleViewTool {
  constructor(inspector, window) {
    this.inspector = inspector;
    this.document = window.document;

    this.view = new CssRuleView(this.inspector, this.document);

    this.refresh = this.refresh.bind(this);
    this.onDetachedFront = this.onDetachedFront.bind(this);
    this.onPanelSelected = this.onPanelSelected.bind(this);
    this.onDetachedFront = this.onDetachedFront.bind(this);
    this.onSelected = this.onSelected.bind(this);
    this.onViewRefreshed = this.onViewRefreshed.bind(this);

    this.#abortController = new window.AbortController();
    const { signal } = this.#abortController;
    const baseEventConfig = { signal };

    this.view.on("ruleview-refreshed", this.onViewRefreshed, baseEventConfig);
    this.inspector.selection.on(
      "detached-front",
      this.onDetachedFront,
      baseEventConfig
    );
    this.inspector.selection.on(
      "new-node-front",
      this.onSelected,
      baseEventConfig
    );
    this.inspector.selection.on("pseudoclass", this.refresh, baseEventConfig);
    this.inspector.ruleViewSideBar.on(
      "ruleview-selected",
      this.onPanelSelected,
      baseEventConfig
    );
    this.inspector.sidebar.on(
      "ruleview-selected",
      this.onPanelSelected,
      baseEventConfig
    );
    this.inspector.toolbox.on(
      "inspector-selected",
      this.onPanelSelected,
      baseEventConfig
    );
    this.inspector.styleChangeTracker.on(
      "style-changed",
      this.refresh,
      baseEventConfig
    );

    this.inspector.commands.resourceCommand.watchResources(
      [
        this.inspector.commands.resourceCommand.TYPES.DOCUMENT_EVENT,
        this.inspector.commands.resourceCommand.TYPES.STYLESHEET,
      ],
      {
        onAvailable: this.#onResourceAvailable,
        ignoreExistingResources: true,
      }
    );

    // We do want to get already existing registered properties, so we need to watch
    // them separately
    this.inspector.commands.resourceCommand
      .watchResources(
        [
          this.inspector.commands.resourceCommand.TYPES
            .CSS_REGISTERED_PROPERTIES,
        ],
        {
          onAvailable: this.#onResourceAvailable,
          onUpdated: this.#onResourceUpdated,
          onDestroyed: this.#onResourceDestroyed,
          ignoreExistingResources: false,
        }
      )
      .catch(e => {
        // watchResources is async and even making it's resulting promise part of
        // this.readyPromise still causes test failures, so simply ignore the rejection
        // if the view was already destroyed.
        if (!this.view) {
          return;
        }
        throw e;
      });

    // At the moment `readyPromise` is only consumed in tests (see `openRuleView`) to be
    // notified when the ruleview was first populated to match the initial selected node.
    this.readyPromise = this.onSelected();
  }

  #abortController;

  isPanelVisible() {
    if (!this.view) {
      return false;
    }
    return this.view.isPanelVisible();
  }

  onDetachedFront() {
    this.onSelected(false);
  }

  onSelected(selectElement = true) {
    // Ignore the event if the view has been destroyed, or if it's inactive.
    // But only if the current selection isn't null. If it's been set to null,
    // let the update go through as this is needed to empty the view on
    // navigation.
    if (!this.view) {
      return null;
    }

    const isInactive =
      !this.isPanelVisible() && this.inspector.selection.nodeFront;
    if (isInactive) {
      return null;
    }

    if (
      !this.inspector.selection.isConnected() ||
      !this.inspector.selection.isElementNode()
    ) {
      return this.view.selectElement(null);
    }

    if (!selectElement) {
      return null;
    }

    const done = this.inspector.updating("rule-view");
    return this.view
      .selectElement(this.inspector.selection.nodeFront)
      .then(done, done);
  }

  refresh() {
    if (this.isPanelVisible()) {
      this.view.refreshPanel();
    }
  }

  #onResourceAvailable = resources => {
    if (!this.inspector) {
      return;
    }

    let hasNewStylesheet = false;
    const addedRegisteredProperties = [];
    for (const resource of resources) {
      if (
        resource.resourceType ===
          this.inspector.commands.resourceCommand.TYPES.DOCUMENT_EVENT &&
        resource.name === "will-navigate"
      ) {
        this.view.cssRegisteredPropertiesByTarget.delete(resource.targetFront);
        if (resource.targetFront.isTopLevel) {
          this.clearUserProperties();
        }
        continue;
      }

      if (
        resource.resourceType ===
          this.inspector.commands.resourceCommand.TYPES.STYLESHEET &&
        // resource.isNew is only true when the stylesheet was added from DevTools,
        // for example when adding a rule in the rule view. In such cases, we're already
        // updating the rule view, so ignore those.
        !resource.isNew
      ) {
        hasNewStylesheet = true;
      }

      if (
        resource.resourceType ===
        this.inspector.commands.resourceCommand.TYPES.CSS_REGISTERED_PROPERTIES
      ) {
        if (
          !this.view.cssRegisteredPropertiesByTarget.has(resource.targetFront)
        ) {
          this.view.cssRegisteredPropertiesByTarget.set(
            resource.targetFront,
            new Map()
          );
        }
        this.view.cssRegisteredPropertiesByTarget
          .get(resource.targetFront)
          .set(resource.name, resource);
        // Only add properties from the same target as the selected node
        if (
          this.view.inspector.selection?.nodeFront?.targetFront ===
          resource.targetFront
        ) {
          addedRegisteredProperties.push(resource);
        }
      }
    }

    if (addedRegisteredProperties.length) {
      // Retrieve @property container
      let registeredPropertiesContainer =
        this.view.styleDocument.getElementById(
          REGISTERED_PROPERTIES_CONTAINER_ID
        );
      // create it if it didn't exist before
      if (!registeredPropertiesContainer) {
        registeredPropertiesContainer =
          this.view.createRegisteredPropertiesExpandableContainer();
      }

      // Then add all new registered properties
      const names = new Set();
      for (const propertyDefinition of addedRegisteredProperties) {
        const editor = new RegisteredPropertyEditor(
          this.view,
          propertyDefinition
        );
        names.add(propertyDefinition.name);

        // We need to insert the element at the right position so we keep the list of
        // properties alphabetically sorted.
        let referenceNode = null;
        for (const child of registeredPropertiesContainer.children) {
          if (child.getAttribute("data-name") > propertyDefinition.name) {
            referenceNode = child;
            break;
          }
        }
        registeredPropertiesContainer.insertBefore(
          editor.element,
          referenceNode
        );
      }

      // Finally, update textProps that might rely on those new properties
      this._updateElementStyleRegisteredProperties(names);
    }

    if (hasNewStylesheet) {
      this.refresh();
    }
  };

  #onResourceUpdated = updates => {
    const updatedProperties = [];
    for (const update of updates) {
      if (
        update.resource.resourceType ===
        this.inspector.commands.resourceCommand.TYPES.CSS_REGISTERED_PROPERTIES
      ) {
        const { resource } = update;
        if (
          !this.view.cssRegisteredPropertiesByTarget.has(resource.targetFront)
        ) {
          continue;
        }

        this.view.cssRegisteredPropertiesByTarget
          .get(resource.targetFront)
          .set(resource.name, resource);

        // Only consider properties from the same target as the selected node
        if (
          this.view.inspector.selection?.nodeFront?.targetFront ===
          resource.targetFront
        ) {
          updatedProperties.push(resource);
        }
      }
    }

    const names = new Set();
    if (updatedProperties.length) {
      const registeredPropertiesContainer =
        this.view.styleDocument.getElementById(
          REGISTERED_PROPERTIES_CONTAINER_ID
        );
      for (const resource of updatedProperties) {
        // Replace the existing registered property editor element with a new one,
        // so we don't have to compute which elements should be updated.
        const name = resource.name;
        const el = this.view.getRegisteredPropertyElement(name);
        const editor = new RegisteredPropertyEditor(this.view, resource);
        registeredPropertiesContainer.replaceChild(editor.element, el);

        names.add(resource.name);
      }
      // Finally, update textProps that might rely on those new properties
      this._updateElementStyleRegisteredProperties(names);
    }
  };

  #onResourceDestroyed = resources => {
    const destroyedPropertiesNames = new Set();
    for (const resource of resources) {
      if (
        resource.resourceType ===
        this.inspector.commands.resourceCommand.TYPES.CSS_REGISTERED_PROPERTIES
      ) {
        if (
          !this.view.cssRegisteredPropertiesByTarget.has(resource.targetFront)
        ) {
          continue;
        }

        const targetRegisteredProperties =
          this.view.cssRegisteredPropertiesByTarget.get(resource.targetFront);
        const resourceName = Array.from(
          targetRegisteredProperties.entries()
        ).find(
          ([_, propDef]) => propDef.resourceId === resource.resourceId
        )?.[0];
        if (!resourceName) {
          continue;
        }

        targetRegisteredProperties.delete(resourceName);

        // Only consider properties from the same target as the selected node
        if (
          this.view.inspector.selection?.nodeFront?.targetFront ===
          resource.targetFront
        ) {
          destroyedPropertiesNames.add(resourceName);
        }
      }
    }
    if (destroyedPropertiesNames.size > 0) {
      for (const name of destroyedPropertiesNames) {
        this.view.getRegisteredPropertyElement(name)?.remove();
      }
      // Finally, update textProps that were relying on those removed properties
      this._updateElementStyleRegisteredProperties(destroyedPropertiesNames);
    }
  };

  /**
   * Update rules that reference registered properties whose name is in the passed Set,
   * so the `var()` tooltip has up-to-date information.
   *
   * @param {Set<String>} registeredPropertyNames
   */
  _updateElementStyleRegisteredProperties(registeredPropertyNames) {
    if (!this.view._elementStyle) {
      return;
    }
    this.view._elementStyle.onRegisteredPropertiesChange(
      registeredPropertyNames
    );
  }

  clearUserProperties() {
    if (this.view && this.view.store && this.view.store.userProperties) {
      this.view.store.userProperties.clear();
    }
  }

  onPanelSelected() {
    if (this.inspector.selection.nodeFront === this.view._viewedElement) {
      this.refresh();
    } else {
      this.onSelected();
    }
  }

  onViewRefreshed() {
    this.inspector.emit("rule-view-refreshed");
  }

  destroy() {
    if (this.#abortController) {
      this.#abortController.abort();
    }

    this.inspector.commands.resourceCommand.unwatchResources(
      [
        this.inspector.commands.resourceCommand.TYPES.DOCUMENT_EVENT,
        this.inspector.commands.resourceCommand.TYPES.STYLESHEET,
      ],
      {
        onAvailable: this.#onResourceAvailable,
      }
    );

    this.inspector.commands.resourceCommand.unwatchResources(
      [this.inspector.commands.resourceCommand.TYPES.CSS_REGISTERED_PROPERTIES],
      {
        onAvailable: this.#onResourceAvailable,
        onUpdated: this.#onResourceUpdated,
        onDestroyed: this.#onResourceDestroyed,
      }
    );

    this.view.destroy();

    this.view =
      this.document =
      this.inspector =
      this.readyPromise =
      this.#abortController =
        null;
  }
}

exports.CssRuleView = CssRuleView;
exports.RuleViewTool = RuleViewTool;
