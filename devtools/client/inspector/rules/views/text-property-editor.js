/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  l10n,
  l10nFormatStr,
} = require("resource://devtools/shared/inspector/css-logic.js");
const {
  InplaceEditor,
  editableField,
} = require("resource://devtools/client/shared/inplace-editor.js");
const {
  createChild,
  appendText,
  advanceValidate,
  blurOnMultipleProperties,
} = require("resource://devtools/client/inspector/shared/utils.js");
const { throttle } = require("resource://devtools/shared/throttle.js");
const {
  style: { ELEMENT_STYLE },
} = require("resource://devtools/shared/constants.js");

loader.lazyRequireGetter(
  this,
  "openContentLink",
  "resource://devtools/client/shared/link.js",
  true
);
loader.lazyRequireGetter(
  this,
  ["parseDeclarations", "parseSingleValue"],
  "resource://devtools/shared/css/parsing-utils.js",
  true
);
loader.lazyRequireGetter(
  this,
  "findCssSelector",
  "resource://devtools/shared/inspector/css-logic.js",
  true
);
loader.lazyGetter(this, "PROPERTY_NAME_INPUT_LABEL", function () {
  return l10n("rule.propertyName.label");
});
loader.lazyGetter(this, "SHORTHAND_EXPANDER_TOOLTIP", function () {
  return l10n("rule.shorthandExpander.tooltip");
});

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  AppConstants: "resource://gre/modules/AppConstants.sys.mjs",
});

const HTML_NS = "http://www.w3.org/1999/xhtml";

const SHARED_SWATCH_CLASS = "inspector-swatch";
const COLOR_SWATCH_CLASS = "inspector-colorswatch";
const BEZIER_SWATCH_CLASS = "inspector-bezierswatch";
const LINEAR_EASING_SWATCH_CLASS = "inspector-lineareasingswatch";
const FILTER_SWATCH_CLASS = "inspector-filterswatch";
const ANGLE_SWATCH_CLASS = "inspector-angleswatch";
const FONT_FAMILY_CLASS = "ruleview-font-family";
const SHAPE_SWATCH_CLASS = "inspector-shapeswatch";

/*
 * An actionable element is an element which on click triggers a specific action
 * (e.g. shows a color tooltip, opens a link, …).
 */
const ACTIONABLE_ELEMENTS_SELECTORS = [
  `.${COLOR_SWATCH_CLASS}`,
  `.${BEZIER_SWATCH_CLASS}`,
  `.${LINEAR_EASING_SWATCH_CLASS}`,
  `.${FILTER_SWATCH_CLASS}`,
  `.${ANGLE_SWATCH_CLASS}`,
  "a",
];

/*
 * Speeds at which we update the value when the user is dragging its mouse
 * over a value.
 */
const SLOW_DRAGGING_SPEED = 0.1;
const DEFAULT_DRAGGING_SPEED = 1;
const FAST_DRAGGING_SPEED = 10;

// Deadzone in pixels where dragging should not update the value.
const DRAGGING_DEADZONE_DISTANCE = 5;

const DRAGGABLE_VALUE_CLASSNAME = "ruleview-propertyvalue-draggable";
const IS_DRAGGING_CLASSNAME = "ruleview-propertyvalue-dragging";

/**
 * TextPropertyEditor is responsible for the following:
 *   Owns a TextProperty object.
 *   Manages changes to the TextProperty.
 *   Can be expanded to display computed properties.
 *   Can mark a property disabled or enabled.
 *
 * @param {RuleEditor} ruleEditor
 *        The rule editor that owns this TextPropertyEditor.
 * @param {TextProperty} property
 *        The text property to edit.
 */
function TextPropertyEditor(ruleEditor, property) {
  this.ruleEditor = ruleEditor;
  this.ruleView = this.ruleEditor.ruleView;
  this.cssProperties = this.ruleView.cssProperties;
  this.doc = this.ruleEditor.doc;
  this.popup = this.ruleView.popup;
  this.prop = property;
  this.prop.editor = this;
  this.browserWindow = this.doc.defaultView.top;

  this._populatedComputed = false;
  this._hasPendingClick = false;
  this._clickedElementOptions = null;

  this.toolbox = this.ruleView.inspector.toolbox;
  this.telemetry = this.toolbox.telemetry;

  this._isDragging = false;
  this._capturingPointerId = null;
  this._hasDragged = false;
  this._draggingController = null;
  this._draggingValueCache = null;

  this.getGridlineNames = this.getGridlineNames.bind(this);
  this.update = this.update.bind(this);
  this.updatePropertyState = this.updatePropertyState.bind(this);
  this._onDraggablePreferenceChanged =
    this._onDraggablePreferenceChanged.bind(this);
  this._onEnableChanged = this._onEnableChanged.bind(this);
  this._onEnableClicked = this._onEnableClicked.bind(this);
  this._onExpandClicked = this._onExpandClicked.bind(this);
  this._onNameDone = this._onNameDone.bind(this);
  this._onStartEditing = this._onStartEditing.bind(this);
  this._onSwatchCommit = this._onSwatchCommit.bind(this);
  this._onSwatchPreview = this._onSwatchPreview.bind(this);
  this._onSwatchRevert = this._onSwatchRevert.bind(this);
  this._onValidate = this.ruleView.debounce(this._previewValue, 10, this);
  this._onValueDone = this._onValueDone.bind(this);

  this._draggingOnPointerDown = this._draggingOnPointerDown.bind(this);
  this._draggingOnMouseMove = throttle(this._draggingOnMouseMove, 30, this);
  this._draggingOnPointerUp = this._draggingOnPointerUp.bind(this);
  this._draggingOnKeydown = this._draggingOnKeydown.bind(this);

  this._create();
  this.update();
}

TextPropertyEditor.prototype = {
  /**
   * Boolean indicating if the name or value is being currently edited.
   */
  get editing() {
    return (
      !!(
        this.nameSpan.inplaceEditor ||
        this.valueSpan.inplaceEditor ||
        this.ruleView.tooltips.isEditing
      ) || this.popup.isOpen
    );
  },

  /**
   * Get the rule to the current text property
   */
  get rule() {
    return this.prop.rule;
  },

  // Exposed for tests.
  get _DRAGGING_DEADZONE_DISTANCE() {
    return DRAGGING_DEADZONE_DISTANCE;
  },

  /**
   * Create the property editor's DOM.
   */
  _create() {
    this.element = this.doc.createElementNS(HTML_NS, "div");
    this.element.setAttribute("role", "listitem");
    this.element.classList.add("ruleview-property");
    this.element.dataset.declarationId = this.prop.id;
    this.element._textPropertyEditor = this;

    this.container = createChild(this.element, "div", {
      class: "ruleview-propertycontainer",
    });

    const indent =
      ((this.ruleEditor.rule.domRule.ancestorData.length || 0) + 1) * 2;
    createChild(this.container, "span", {
      class: "ruleview-rule-indent clipboard-only",
      textContent: " ".repeat(indent),
    });

    // The enable checkbox will disable or enable the rule.
    this.enable = createChild(this.container, "input", {
      type: "checkbox",
      class: "ruleview-enableproperty",
      title: l10nFormatStr("rule.propertyToggle.label", this.prop.name),
    });

    this.nameContainer = createChild(this.container, "span", {
      class: "ruleview-namecontainer",
    });

    // Property name, editable when focused.  Property name
    // is committed when the editor is unfocused.
    this.nameSpan = createChild(this.nameContainer, "span", {
      class: "ruleview-propertyname theme-fg-color3",
      tabindex: this.ruleEditor.isEditable ? "0" : "-1",
      id: this.prop.id,
    });

    appendText(this.nameContainer, ": ");

    // Click to expand the computed properties of the text property.
    this.expander = createChild(this.container, "button", {
      "aria-expanded": "false",
      class: "ruleview-expander theme-twisty",
      title: SHORTHAND_EXPANDER_TOOLTIP,
    });
    this.expander.addEventListener("click", this._onExpandClicked, true);

    // Create a span that will hold the property and semicolon.
    // Use this span to create a slightly larger click target
    // for the value.
    this.valueContainer = createChild(this.container, "span", {
      class: "ruleview-propertyvaluecontainer",
    });

    // Property value, editable when focused.  Changes to the
    // property value are applied as they are typed, and reverted
    // if the user presses escape.
    this.valueSpan = createChild(this.valueContainer, "span", {
      class: "ruleview-propertyvalue theme-fg-color1",
      tabindex: this.ruleEditor.isEditable ? "0" : "-1",
    });

    // Storing the TextProperty on the elements for easy access
    // (for instance by the tooltip)
    this.valueSpan.textProperty = this.prop;
    this.nameSpan.textProperty = this.prop;

    appendText(this.valueContainer, ";");

    this.warning = createChild(this.container, "div", {
      class: "ruleview-warning",
      hidden: "",
      title: l10n("rule.warning.title"),
    });

    this.invalidAtComputedValueTimeWarning = createChild(
      this.container,
      "div",
      {
        class: "ruleview-invalid-at-computed-value-time-warning",
        hidden: "",
      }
    );

    this.unusedState = createChild(this.container, "div", {
      class: "ruleview-unused-warning",
      hidden: "",
    });

    this.compatibilityState = createChild(this.container, "div", {
      class: "ruleview-compatibility-warning",
      hidden: "",
    });

    // Filter button that filters for the current property name and is
    // displayed when the property is overridden by another rule.
    this.filterProperty = createChild(this.container, "button", {
      class: "ruleview-overridden-rule-filter",
      hidden: "",
      title: l10n("rule.filterProperty.title"),
    });

    this.filterProperty.addEventListener("click", event => {
      this.ruleEditor.ruleView.setFilterStyles("`" + this.prop.name + "`");
      event.stopPropagation();
    });

    // Holds the viewers for the computed properties.
    // will be populated in |_updateComputed|.
    this.computed = createChild(this.element, "ul", {
      class: "ruleview-computedlist",
    });

    // Holds the viewers for the overridden shorthand properties.
    // will be populated in |_updateShorthandOverridden|.
    this.shorthandOverridden = createChild(this.element, "ul", {
      class: "ruleview-overridden-items",
    });

    // Only bind event handlers if the rule is editable.
    if (this.ruleEditor.isEditable) {
      this.enable.addEventListener("click", this._onEnableClicked, true);
      this.enable.addEventListener("change", this._onEnableChanged, true);

      this.nameContainer.addEventListener("click", event => {
        // Clicks within the name shouldn't propagate any further.
        event.stopPropagation();

        // Forward clicks on nameContainer to the editable nameSpan
        if (event.target === this.nameContainer) {
          this.nameSpan.click();
        }
      });

      const getCssVariables = () =>
        this.rule.elementStyle.getAllCustomProperties(this.rule.pseudoElement);

      editableField({
        start: this._onStartEditing,
        element: this.nameSpan,
        done: this._onNameDone,
        destroy: this.updatePropertyState,
        advanceChars: ":",
        contentType: InplaceEditor.CONTENT_TYPES.CSS_PROPERTY,
        popup: this.popup,
        cssProperties: this.cssProperties,
        getCssVariables,
        // (Shift+)Tab will move the focus to the previous/next editable field (so property value
        // or new selector).
        focusEditableFieldAfterApply: true,
        focusEditableFieldContainerSelector: ".ruleview-rule",
        // We don't want Enter to trigger the next editable field, just to validate
        // what the user entered, close the editor, and focus the span so the user can
        // navigate with the keyboard as expected, unless the user has
        // devtools.inspector.rule-view.focusNextOnEnter set to true
        stopOnReturn: this.ruleView.inplaceEditorFocusNextOnEnter !== true,
        inputAriaLabel: PROPERTY_NAME_INPUT_LABEL,
      });

      // Auto blur name field on multiple CSS rules get pasted in.
      this.nameContainer.addEventListener(
        "paste",
        blurOnMultipleProperties(this.cssProperties)
      );

      this.valueContainer.addEventListener("click", event => {
        // Clicks within the value shouldn't propagate any further.
        event.stopPropagation();

        // Forward clicks on valueContainer to the editable valueSpan
        if (event.target === this.valueContainer) {
          this.valueSpan.click();
        }
      });

      // The mousedown event could trigger a blur event on nameContainer, which
      // will trigger a call to the update function. The update function clears
      // valueSpan's markup. Thus the regular click event does not bubble up, and
      // listener's callbacks are not called.
      // So we need to remember where the user clicks in order to re-trigger the click
      // after the valueSpan's markup is re-populated. We only need to track this for
      // valueSpan's child elements, because direct click on valueSpan will always
      // trigger a click event.
      this.valueSpan.addEventListener("mousedown", event => {
        const clickedEl = event.target;
        if (clickedEl === this.valueSpan) {
          return;
        }
        this._hasPendingClick = true;

        const matchedSelector = ACTIONABLE_ELEMENTS_SELECTORS.find(selector =>
          clickedEl.matches(selector)
        );
        if (matchedSelector) {
          const similarElements = [
            ...this.valueSpan.querySelectorAll(matchedSelector),
          ];
          this._clickedElementOptions = {
            selector: matchedSelector,
            index: similarElements.indexOf(clickedEl),
          };
        }
      });

      this.valueSpan.addEventListener("pointerup", () => {
        // if we have dragged, we will handle the pending click in _draggingOnPointerUp instead
        if (this._hasDragged) {
          return;
        }
        this._clickedElementOptions = null;
        this._hasPendingClick = false;
      });

      this.valueSpan.addEventListener("click", event => {
        const target = event.target;

        if (target.nodeName === "a") {
          event.stopPropagation();
          event.preventDefault();
          openContentLink(target.href);
        }
      });

      this.ruleView.on(
        "draggable-preference-updated",
        this._onDraggablePreferenceChanged
      );
      if (this._isDraggableProperty(this.prop)) {
        this._addDraggingCapability();
      }

      editableField({
        start: this._onStartEditing,
        element: this.valueSpan,
        done: this._onValueDone,
        destroy: this.update,
        validate: this._onValidate,
        advanceChars: advanceValidate,
        contentType: InplaceEditor.CONTENT_TYPES.CSS_VALUE,
        property: this.prop,
        defaultIncrement: this.prop.name === "opacity" ? 0.1 : 1,
        popup: this.popup,
        multiline: true,
        maxWidth: () => this.container.getBoundingClientRect().width,
        cssProperties: this.cssProperties,
        getCssVariables,
        getGridLineNames: this.getGridlineNames,
        showSuggestCompletionOnEmpty: true,
        // (Shift+)Tab will move the focus to the previous/next editable field (so property name,
        // or new property).
        focusEditableFieldAfterApply: true,
        focusEditableFieldContainerSelector: ".ruleview-rule",
        // We don't want Enter to trigger the next editable field, just to validate
        // what the user entered, close the editor, and focus the span so the user can
        // navigate with the keyboard as expected, unless the user has
        // devtools.inspector.rule-view.focusNextOnEnter set to true
        stopOnReturn: this.ruleView.inplaceEditorFocusNextOnEnter !== true,
        // Label the value input with the name span so screenreader users know what this
        // applies to.
        inputAriaLabelledBy: this.nameSpan.id,
      });
    }
  },

  /**
   * Get the grid line names of the grid that the currently selected element is
   * contained in.
   *
   * @return {Object} Contains the names of the cols and rows as arrays
   * {cols: [], rows: []}.
   */
  async getGridlineNames() {
    const gridLineNames = { cols: [], rows: [] };
    const layoutInspector =
      await this.ruleView.inspector.walker.getLayoutInspector();
    const gridFront = await layoutInspector.getCurrentGrid(
      this.ruleView.inspector.selection.nodeFront
    );

    if (gridFront) {
      const gridFragments = gridFront.gridFragments;

      for (const gridFragment of gridFragments) {
        for (const rowLine of gridFragment.rows.lines) {
          // We specifically ignore implicit line names created from implicitly named
          // areas. This is because showing implicit line names can be confusing for
          // designers who may have used a line name with "-start" or "-end" and created
          // an implicitly named grid area without meaning to.
          let gridArea;

          for (const name of rowLine.names) {
            const rowLineName =
              name.substring(0, name.lastIndexOf("-start")) ||
              name.substring(0, name.lastIndexOf("-end"));
            gridArea = gridFragment.areas.find(
              area => area.name === rowLineName
            );

            if (
              rowLine.type === "implicit" &&
              gridArea &&
              gridArea.type === "implicit"
            ) {
              continue;
            }
            gridLineNames.rows.push(name);
          }
        }

        for (const colLine of gridFragment.cols.lines) {
          let gridArea;

          for (const name of colLine.names) {
            const colLineName =
              name.substring(0, name.lastIndexOf("-start")) ||
              name.substring(0, name.lastIndexOf("-end"));
            gridArea = gridFragment.areas.find(
              area => area.name === colLineName
            );

            if (
              colLine.type === "implicit" &&
              gridArea &&
              gridArea.type === "implicit"
            ) {
              continue;
            }
            gridLineNames.cols.push(name);
          }
        }
      }
    }

    // Emit message for test files
    this.ruleView.inspector.emit("grid-line-names-updated");
    return gridLineNames;
  },

  /**
   * Get the path from which to resolve requests for this
   * rule's stylesheet.
   *
   * @return {String} the stylesheet's href.
   */
  get sheetHref() {
    const domRule = this.rule.domRule;
    if (domRule) {
      return domRule.href || domRule.nodeHref;
    }
    return undefined;
  },

  /**
   * Populate the span based on changes to the TextProperty.
   */
  // eslint-disable-next-line complexity
  update() {
    if (this.ruleView.isDestroyed) {
      return;
    }

    this.updatePropertyState();

    const name = this.prop.name;
    this.nameSpan.textContent = name;
    this.enable.setAttribute(
      "title",
      l10nFormatStr("rule.propertyToggle.label", name)
    );

    // Combine the property's value and priority into one string for
    // the value.
    const store = this.rule.elementStyle.store;
    let val = store.userProperties.getProperty(
      this.rule.domRule,
      name,
      this.prop.value
    );
    if (this.prop.priority) {
      val += " !" + this.prop.priority;
    }

    const propDirty = store.userProperties.contains(this.rule.domRule, name);

    if (propDirty) {
      this.element.setAttribute("dirty", "");
    } else {
      this.element.removeAttribute("dirty");
    }

    const outputParser = this.ruleView._outputParser;
    this.outputParserOptions = {
      angleClass: "ruleview-angle",
      angleSwatchClass: SHARED_SWATCH_CLASS + " " + ANGLE_SWATCH_CLASS,
      bezierClass: "ruleview-bezier",
      bezierSwatchClass: SHARED_SWATCH_CLASS + " " + BEZIER_SWATCH_CLASS,
      colorClass: "ruleview-color",
      colorSwatchClass: SHARED_SWATCH_CLASS + " " + COLOR_SWATCH_CLASS,
      filterClass: "ruleview-filter",
      filterSwatchClass: SHARED_SWATCH_CLASS + " " + FILTER_SWATCH_CLASS,
      flexClass: "inspector-flex js-toggle-flexbox-highlighter",
      gridClass: "inspector-grid js-toggle-grid-highlighter",
      linearEasingClass: "ruleview-lineareasing",
      linearEasingSwatchClass:
        SHARED_SWATCH_CLASS + " " + LINEAR_EASING_SWATCH_CLASS,
      shapeClass: "inspector-shape",
      shapeSwatchClass: SHAPE_SWATCH_CLASS,
      // Only ask the parser to convert colors to the default color type specified by the
      // user if the property hasn't been changed yet.
      useDefaultColorUnit: !propDirty,
      defaultColorUnit: this.ruleView.inspector.defaultColorUnit,
      urlClass: "theme-link",
      fontFamilyClass: FONT_FAMILY_CLASS,
      baseURI: this.sheetHref,
      unmatchedClass: "inspector-unmatched",
      matchedVariableClass: "inspector-variable",
      getVariableData: varName =>
        this.rule.elementStyle.getVariableData(
          varName,
          this.rule.pseudoElement
        ),
      inStartingStyleRule: this.rule.isInStartingStyle(),
    };

    if (this.rule.darkColorScheme !== undefined) {
      this.outputParserOptions.isDarkColorScheme = this.rule.darkColorScheme;
    }
    const frag = outputParser.parseCssProperty(
      name,
      val,
      this.outputParserOptions
    );

    // Save the initial value as the last committed value,
    // for restoring after pressing escape.
    if (!this.committed) {
      this.committed = {
        name,
        value: frag.textContent,
        priority: this.prop.priority,
      };
    }

    // Save focused element inside value span if one exists before wiping the innerHTML
    let focusedElSelector = null;
    if (this.valueSpan.contains(this.doc.activeElement)) {
      focusedElSelector = findCssSelector(this.doc.activeElement);
    }

    this.valueSpan.innerHTML = "";
    this.valueSpan.appendChild(frag);
    if (
      this.valueSpan.textProperty?.name === "grid-template-areas" &&
      this.isValid() &&
      (this.valueSpan.innerText.includes(`"`) ||
        this.valueSpan.innerText.includes(`'`))
    ) {
      this._formatGridTemplateAreasValue();
    }

    this.ruleView.emit("property-value-updated", {
      rule: this.prop.rule,
      property: name,
      value: val,
    });

    // Highlight the currently used font in font-family properties.
    // If we cannot find a match, highlight the first generic family instead.
    const fontFamilySpans = this.valueSpan.querySelectorAll(
      "." + FONT_FAMILY_CLASS
    );
    if (fontFamilySpans.length && this.prop.enabled && !this.prop.overridden) {
      this.rule.elementStyle
        .getUsedFontFamilies()
        .then(families => {
          for (const span of fontFamilySpans) {
            const authoredFont = span.textContent.toLowerCase();
            if (families.has(authoredFont)) {
              span.classList.add("used-font");
              // In case a font-family appears multiple time in the value, we only want
              // to highlight the first occurence.
              families.delete(authoredFont);
            }
          }

          this.ruleView.emit("font-highlighted", this.valueSpan);
        })
        .catch(e =>
          console.error("Could not get the list of font families", e)
        );
    }

    // Attach the color picker tooltip to the color swatches
    this._colorSwatchSpans = this.valueSpan.querySelectorAll(
      "." + COLOR_SWATCH_CLASS
    );
    if (this.ruleEditor.isEditable) {
      for (const span of this._colorSwatchSpans) {
        // Adding this swatch to the list of swatches our colorpicker
        // knows about
        this.ruleView.tooltips.getTooltip("colorPicker").addSwatch(span, {
          onShow: this._onStartEditing,
          onPreview: this._onSwatchPreview,
          onCommit: this._onSwatchCommit,
          onRevert: this._onSwatchRevert,
        });
        const title = l10n("rule.colorSwatch.tooltip");
        span.setAttribute("title", title);
        span.dataset.propertyName = this.nameSpan.textContent;
      }
    }

    // Attach the cubic-bezier tooltip to the bezier swatches
    this._bezierSwatchSpans = this.valueSpan.querySelectorAll(
      "." + BEZIER_SWATCH_CLASS
    );
    if (this.ruleEditor.isEditable) {
      for (const span of this._bezierSwatchSpans) {
        // Adding this swatch to the list of swatches our colorpicker
        // knows about
        this.ruleView.tooltips.getTooltip("cubicBezier").addSwatch(span, {
          onShow: this._onStartEditing,
          onPreview: this._onSwatchPreview,
          onCommit: this._onSwatchCommit,
          onRevert: this._onSwatchRevert,
        });
        const title = l10n("rule.bezierSwatch.tooltip");
        span.setAttribute("title", title);
      }
    }

    // Attach the linear easing tooltip to the linear easing swatches
    this._linearEasingSwatchSpans = this.valueSpan.querySelectorAll(
      "." + LINEAR_EASING_SWATCH_CLASS
    );
    if (this.ruleEditor.isEditable) {
      for (const span of this._linearEasingSwatchSpans) {
        // Adding this swatch to the list of swatches our colorpicker
        // knows about
        this.ruleView.tooltips
          .getTooltip("linearEaseFunction")
          .addSwatch(span, {
            onShow: this._onStartEditing,
            onPreview: this._onSwatchPreview,
            onCommit: this._onSwatchCommit,
            onRevert: this._onSwatchRevert,
          });
        span.setAttribute("title", l10n("rule.bezierSwatch.tooltip"));
      }
    }

    // Attach the filter editor tooltip to the filter swatch
    const span = this.valueSpan.querySelector("." + FILTER_SWATCH_CLASS);
    if (this.ruleEditor.isEditable) {
      if (span) {
        this.outputParserOptions.filterSwatch = true;

        this.ruleView.tooltips.getTooltip("filterEditor").addSwatch(
          span,
          {
            onShow: this._onStartEditing,
            onPreview: this._onSwatchPreview,
            onCommit: this._onSwatchCommit,
            onRevert: this._onSwatchRevert,
          },
          outputParser,
          this.outputParserOptions
        );
        const title = l10n("rule.filterSwatch.tooltip");
        span.setAttribute("title", title);
      }
    }

    this.angleSwatchSpans = this.valueSpan.querySelectorAll(
      "." + ANGLE_SWATCH_CLASS
    );
    if (this.ruleEditor.isEditable) {
      for (const angleSpan of this.angleSwatchSpans) {
        angleSpan.addEventListener("unit-change", this._onSwatchCommit);
        const title = l10n("rule.angleSwatch.tooltip");
        angleSpan.setAttribute("title", title);
      }
    }

    const nodeFront = this.ruleView.inspector.selection.nodeFront;

    const flexToggle = this.valueSpan.querySelector(".inspector-flex");
    if (flexToggle) {
      flexToggle.setAttribute("title", l10n("rule.flexToggle.tooltip"));
      flexToggle.setAttribute(
        "aria-pressed",
        this.ruleView.inspector.highlighters.getNodeForActiveHighlighter(
          this.ruleView.inspector.highlighters.TYPES.FLEXBOX
        ) === nodeFront
      );
    }

    const gridToggle = this.valueSpan.querySelector(".inspector-grid");
    if (gridToggle) {
      gridToggle.setAttribute("title", l10n("rule.gridToggle.tooltip"));
      gridToggle.setAttribute(
        "aria-pressed",
        this.ruleView.highlighters.gridHighlighters.has(nodeFront)
      );
      gridToggle.toggleAttribute(
        "disabled",
        !this.ruleView.highlighters.canGridHighlighterToggle(nodeFront)
      );
    }

    const shapeToggle = this.valueSpan.querySelector(".inspector-shapeswatch");
    if (shapeToggle) {
      const mode =
        "css" +
        name
          .split("-")
          .map(s => {
            return s[0].toUpperCase() + s.slice(1);
          })
          .join("");
      shapeToggle.setAttribute("data-mode", mode);
      shapeToggle.setAttribute("aria-pressed", false);
      shapeToggle.setAttribute("title", l10n("rule.shapeToggle.tooltip"));
    }

    // Now that we have updated the property's value, we might have a pending
    // click on the value container. If we do, we have to trigger a click event
    // on the right element.
    // If we are dragging, we don't need to handle the pending click
    if (this._hasPendingClick && !this._isDragging) {
      this._hasPendingClick = false;
      let elToClick;

      if (this._clickedElementOptions !== null) {
        const { selector, index } = this._clickedElementOptions;
        elToClick = this.valueSpan.querySelectorAll(selector)[index];

        this._clickedElementOptions = null;
      }

      if (!elToClick) {
        elToClick = this.valueSpan;
      }
      elToClick.click();
    }

    // Populate the computed styles and shorthand overridden styles.
    this._updateComputed();
    this._updateShorthandOverridden();

    // Update the rule property highlight.
    this.ruleView._updatePropertyHighlight(this);

    // Restore focus back to the element whose markup was recreated above.
    if (focusedElSelector) {
      const elementToFocus = this.doc.querySelector(focusedElSelector);
      if (elementToFocus) {
        elementToFocus.focus();
      }
    }
  },

  _onStartEditing() {
    this.element.classList.remove("ruleview-overridden");
    this.filterProperty.hidden = true;
    this.enable.style.visibility = "hidden";
    this.expander.style.display = "none";
  },

  get shouldShowComputedExpander() {
    // Only show the expander to reveal computed properties if:
    // - the computed properties are actually different from the current property (i.e
    //   these are longhands while the current property is the shorthand)
    // - all of the computed properties have defined values. In case the current property
    //   value contains CSS variables, then the computed properties will be missing and we
    //   want to avoid showing them.
    return (
      this.prop.computed.some(c => c.name !== this.prop.name) &&
      !this.prop.computed.every(c => !c.value)
    );
  },

  /**
   * Update the visibility of the enable checkbox, the warning indicator, the used
   * indicator and the filter property, as well as the overridden state of the property.
   */
  updatePropertyState() {
    if (this.prop.enabled) {
      this.enable.style.removeProperty("visibility");
    } else {
      this.enable.style.visibility = "visible";
    }

    this.enable.checked = this.prop.enabled;

    this.warning.title = !this.isNameValid()
      ? l10n("rule.warningName.title")
      : l10n("rule.warning.title");

    this.warning.hidden = this.editing || this.isValid();

    if (!this.editing && this.isInvalidAtComputedValueTime()) {
      this.invalidAtComputedValueTimeWarning.title = l10nFormatStr(
        "rule.warningInvalidAtComputedValueTime.title",
        `"${this.prop.getExpectedSyntax()}"`
      );
      this.invalidAtComputedValueTimeWarning.hidden = false;
    } else {
      this.invalidAtComputedValueTimeWarning.hidden = true;
    }

    this.filterProperty.hidden =
      this.editing ||
      !this.isValid() ||
      !this.prop.overridden ||
      this.ruleEditor.rule.isUnmatched;

    this.expander.style.display = this.shouldShowComputedExpander
      ? "inline-block"
      : "none";

    if (
      !this.editing &&
      (this.prop.overridden || !this.prop.enabled || !this.prop.isKnownProperty)
    ) {
      this.element.classList.add("ruleview-overridden");
    } else {
      this.element.classList.remove("ruleview-overridden");
    }

    this.updatePropertyUsedIndicator();
    this.updatePropertyCompatibilityIndicator();
  },

  updatePropertyUsedIndicator() {
    const { used } = this.prop.isUsed();

    if (this.editing || this.prop.overridden || !this.prop.enabled || used) {
      this.element.classList.remove("unused");
      this.unusedState.hidden = true;
    } else {
      this.element.classList.add("unused");
      this.unusedState.hidden = false;
    }
  },

  async updatePropertyCompatibilityIndicator() {
    const { isCompatible } = await this.prop.isCompatible();

    if (this.editing || isCompatible) {
      this.compatibilityState.hidden = true;
    } else {
      this.compatibilityState.hidden = false;
    }
  },

  /**
   * Update the indicator for computed styles. The computed styles themselves
   * are populated on demand, when they become visible.
   */
  _updateComputed() {
    this.computed.innerHTML = "";

    this.expander.style.display =
      !this.editing && this.shouldShowComputedExpander
        ? "inline-block"
        : "none";

    this._populatedComputed = false;
    if (this.expander.getAttribute("aria-expanded" === "true")) {
      this._populateComputed();
    }
  },

  /**
   * Populate the list of computed styles.
   */
  _populateComputed() {
    if (this._populatedComputed) {
      return;
    }
    this._populatedComputed = true;

    for (const computed of this.prop.computed) {
      // Don't bother to duplicate information already
      // shown in the text property.
      if (computed.name === this.prop.name) {
        continue;
      }

      // Store the computed style element for easy access when highlighting
      // styles
      computed.element = this._createComputedListItem(
        this.computed,
        computed,
        "ruleview-computed"
      );
    }
  },

  /**
   * Update the indicator for overridden shorthand styles. The shorthand
   * overridden styles themselves are populated on demand, when they
   * become visible.
   */
  _updateShorthandOverridden() {
    this.shorthandOverridden.innerHTML = "";

    this._populatedShorthandOverridden = false;
    this._populateShorthandOverridden();
  },

  /**
   * Populate the list of overridden shorthand styles.
   */
  _populateShorthandOverridden() {
    if (
      this._populatedShorthandOverridden ||
      this.prop.overridden ||
      !this.shouldShowComputedExpander
    ) {
      return;
    }
    this._populatedShorthandOverridden = true;

    for (const computed of this.prop.computed) {
      // Don't display duplicate information or show properties
      // that are completely overridden.
      if (computed.name === this.prop.name || !computed.overridden) {
        continue;
      }

      this._createComputedListItem(
        this.shorthandOverridden,
        computed,
        "ruleview-overridden-item"
      );
    }
  },

  /**
   * Creates and populates a list item with the computed CSS property.
   */
  _createComputedListItem(parentEl, computed, className) {
    const li = createChild(parentEl, "li", {
      class: className,
    });

    if (computed.overridden) {
      li.classList.add("ruleview-overridden");
    }

    const nameContainer = createChild(li, "span", {
      class: "ruleview-namecontainer",
    });

    createChild(nameContainer, "span", {
      class: "ruleview-propertyname theme-fg-color3",
      textContent: computed.name,
    });
    appendText(nameContainer, ": ");

    const outputParser = this.ruleView._outputParser;
    const frag = outputParser.parseCssProperty(computed.name, computed.value, {
      colorSwatchClass: "inspector-swatch inspector-colorswatch",
      urlClass: "theme-link",
      baseURI: this.sheetHref,
      fontFamilyClass: "ruleview-font-family",
    });

    // Store the computed property value that was parsed for output
    computed.parsedValue = frag.textContent;

    const propertyContainer = createChild(li, "span", {
      class: "ruleview-propertyvaluecontainer",
    });

    createChild(propertyContainer, "span", {
      class: "ruleview-propertyvalue theme-fg-color1",
      child: frag,
    });
    appendText(propertyContainer, ";");

    return li;
  },

  /**
   * Handle updates to the preference which disables/enables the feature to
   * edit size properties on drag.
   */
  _onDraggablePreferenceChanged() {
    if (this._isDraggableProperty(this.prop)) {
      this._addDraggingCapability();
    } else {
      this._removeDraggingCapacity();
    }
  },

  /**
   * Stop clicks propogating down the tree from the enable / disable checkbox.
   */
  _onEnableClicked(event) {
    event.stopPropagation();
  },

  /**
   * Handles clicks on the disabled property.
   */
  _onEnableChanged(event) {
    this.prop.setEnabled(this.enable.checked);
    event.stopPropagation();
    this.telemetry.recordEvent("edit_rule", "ruleview");
  },

  /**
   * Handles clicks on the computed property expander. If the computed list is
   * open due to user expanding or style filtering, collapse the computed list
   * and close the expander. Otherwise, add user-open attribute which is used to
   * expand the computed list and tracks whether or not the computed list is
   * expanded by manually by the user.
   */
  _onExpandClicked(event) {
    const isOpened =
      this.computed.hasAttribute("filter-open") ||
      this.computed.hasAttribute("user-open");

    this.expander.setAttribute("aria-expanded", !isOpened);
    if (isOpened) {
      this.computed.removeAttribute("filter-open");
      this.computed.removeAttribute("user-open");
      this.shorthandOverridden.hidden = false;
      this._populateShorthandOverridden();
    } else {
      this.computed.setAttribute("user-open", "");
      this.shorthandOverridden.hidden = true;
      this._populateComputed();
    }

    event.stopPropagation();
  },

  /**
   * Expands the computed list when a computed property is matched by the style
   * filtering. The filter-open attribute is used to track whether or not the
   * computed list was toggled opened by the filter.
   */
  expandForFilter() {
    if (!this.computed.hasAttribute("user-open")) {
      this.expander.setAttribute("aria-expanded", "true");
      this.computed.setAttribute("filter-open", "");
      this._populateComputed();
    }
  },

  /**
   * Collapses the computed list that was expanded by style filtering.
   */
  collapseForFilter() {
    this.computed.removeAttribute("filter-open");

    if (!this.computed.hasAttribute("user-open")) {
      this.expander.setAttribute("aria-expanded", "false");
    }
  },

  /**
   * Called when the property name's inplace editor is closed.
   * Ignores the change if the user pressed escape, otherwise
   * commits it.
   *
   * @param {String} value
   *        The value contained in the editor.
   * @param {Boolean} commit
   *        True if the change should be applied.
   * @param {Number} direction
   *        The move focus direction number.
   */
  _onNameDone(value, commit, direction) {
    const isNameUnchanged =
      (!commit && !this.ruleEditor.isEditing) || this.committed.name === value;
    if (this.prop.value && isNameUnchanged) {
      return;
    }

    this.telemetry.recordEvent("edit_rule", "ruleview");

    // Remove a property if the name is empty
    if (!value.trim()) {
      this.remove(direction);
      return;
    }

    const isVariable = value.startsWith("--");

    // Remove a property if:
    // - the property value is empty and is not a variable (empty variables are valid)
    // - and the property value is not about to be focused
    if (
      !this.prop.value &&
      !isVariable &&
      direction !== Services.focus.MOVEFOCUS_FORWARD
    ) {
      this.remove(direction);
      return;
    }

    // Adding multiple rules inside of name field overwrites the current
    // property with the first, then adds any more onto the property list.
    const properties = parseDeclarations(this.cssProperties.isKnown, value);

    if (properties.length) {
      this.prop.setName(properties[0].name);
      this.committed.name = this.prop.name;

      if (!this.prop.enabled) {
        this.prop.setEnabled(true);
      }

      if (properties.length > 1) {
        this.prop.setValue(properties[0].value, properties[0].priority);
        this.ruleEditor.addProperties(properties.slice(1), this.prop);
      }
    }
  },

  /**
   * Remove property from style and the editors from DOM.
   * Begin editing next or previous available property given the focus
   * direction.
   *
   * @param {Number} direction
   *        The move focus direction number.
   */
  remove(direction) {
    if (this._colorSwatchSpans && this._colorSwatchSpans.length) {
      for (const span of this._colorSwatchSpans) {
        this.ruleView.tooltips.getTooltip("colorPicker").removeSwatch(span);
      }
    }

    if (this.angleSwatchSpans && this.angleSwatchSpans.length) {
      for (const span of this.angleSwatchSpans) {
        span.removeEventListener("unit-change", this._onSwatchCommit);
      }
    }

    this.ruleView.off(
      "draggable-preference-updated",
      this._onDraggablePreferenceChanged
    );

    this.element.remove();
    this.ruleEditor.rule.editClosestTextProperty(this.prop, direction);
    this.nameSpan.textProperty = null;
    this.valueSpan.textProperty = null;
    this.prop.remove();
  },

  /**
   * Called when a value editor closes.  If the user pressed escape,
   * revert to the value this property had before editing.
   *
   * @param {String} value
   *        The value contained in the editor.
   * @param {Boolean} commit
   *        True if the change should be applied.
   * @param {Number} direction
   *        The move focus direction number.
   */
  _onValueDone(value = "", commit, direction) {
    const parsedProperties = this._getValueAndExtraProperties(value);
    const val = parseSingleValue(
      this.cssProperties.isKnown,
      parsedProperties.firstValue
    );
    const isValueUnchanged =
      (!commit && !this.ruleEditor.isEditing) ||
      (!parsedProperties.propertiesToAdd.length &&
        this.committed.value === val.value &&
        this.committed.priority === val.priority);

    const isVariable = this.prop.name.startsWith("--");

    // If the value is not empty (or is an empty variable) and unchanged,
    // revert the property back to its original value and enabled or disabled state
    if ((value.trim() || isVariable) && isValueUnchanged) {
      this.ruleEditor.rule.previewPropertyValue(
        this.prop,
        val.value,
        val.priority
      );
      this.rule.setPropertyEnabled(this.prop, this.prop.enabled);
      return;
    }

    // Check if unit of value changed to add dragging feature
    if (this._isDraggableProperty(val)) {
      this._addDraggingCapability();
    } else {
      this._removeDraggingCapacity();
    }

    this.telemetry.recordEvent("edit_rule", "ruleview");

    // First, set this property value (common case, only modified a property)
    this.prop.setValue(val.value, val.priority);

    if (!this.prop.enabled) {
      this.prop.setEnabled(true);
    }

    this.committed.value = this.prop.value;
    this.committed.priority = this.prop.priority;

    // If needed, add any new properties after this.prop.
    this.ruleEditor.addProperties(parsedProperties.propertiesToAdd, this.prop);

    // If the input value is empty and is not a variable (empty variables are valid),
    // and the focus is moving forward to the next editable field,
    // then remove the whole property.
    // A timeout is used here to accurately check the state, since the inplace
    // editor `done` and `destroy` events fire before the next editor
    // is focused.
    if (
      !value.trim() &&
      !isVariable &&
      direction !== Services.focus.MOVEFOCUS_BACKWARD
    ) {
      setTimeout(() => {
        if (!this.editing) {
          this.remove(direction);
        }
      }, 0);
    }
  },

  /**
   * Called when the swatch editor wants to commit a value change.
   */
  _onSwatchCommit() {
    this._onValueDone(this.valueSpan.textContent, true);
    this.update();
  },

  /**
   * Called when the swatch editor wants to preview a value change.
   */
  _onSwatchPreview() {
    this._previewValue(this.valueSpan.textContent);
  },

  /**
   * Called when the swatch editor closes from an ESC. Revert to the original
   * value of this property before editing.
   */
  _onSwatchRevert() {
    this._previewValue(this.prop.value, true);
    this.update();
  },

  /**
   * Parse a value string and break it into pieces, starting with the
   * first value, and into an array of additional properties (if any).
   *
   * Example: Calling with "red; width: 100px" would return
   * { firstValue: "red", propertiesToAdd: [{ name: "width", value: "100px" }] }
   *
   * @param {String} value
   *        The string to parse
   * @return {Object} An object with the following properties:
   *        firstValue: A string containing a simple value, like
   *                    "red" or "100px!important"
   *        propertiesToAdd: An array with additional properties, following the
   *                         parseDeclarations format of {name,value,priority}
   */
  _getValueAndExtraProperties(value) {
    // The inplace editor will prevent manual typing of multiple properties,
    // but we need to deal with the case during a paste event.
    // Adding multiple properties inside of value editor sets value with the
    // first, then adds any more onto the property list (below this property).
    let firstValue = value;
    let propertiesToAdd = [];

    const properties = parseDeclarations(this.cssProperties.isKnown, value);

    // Check to see if the input string can be parsed as multiple properties
    if (properties.length) {
      // Get the first property value (if any), and any remaining
      // properties (if any)
      if (!properties[0].name && properties[0].value) {
        firstValue = properties[0].value;
        propertiesToAdd = properties.slice(1);
      } else if (properties[0].name && properties[0].value) {
        // In some cases, the value could be a property:value pair
        // itself.  Join them as one value string and append
        // potentially following properties
        firstValue = properties[0].name + ": " + properties[0].value;
        propertiesToAdd = properties.slice(1);
      }
    }

    return {
      propertiesToAdd,
      firstValue,
    };
  },

  /**
   * Live preview this property, without committing changes.
   *
   * @param {String} value
   *        The value to set the current property to.
   * @param {Boolean} reverting
   *        True if we're reverting the previously previewed value
   */
  _previewValue(value, reverting = false) {
    // Since function call is debounced, we need to make sure we are still
    // editing, and any selector modifications have been completed
    if (!reverting && (!this.editing || this.ruleEditor.isEditing)) {
      return;
    }

    const val = parseSingleValue(this.cssProperties.isKnown, value);
    this.ruleEditor.rule.previewPropertyValue(
      this.prop,
      val.value,
      val.priority
    );
  },

  /**
   * Check if the event passed has a "small increment" modifier
   * Alt on macosx and ctrl on other OSs
   *
   * @param  {KeyboardEvent} event
   * @returns {Boolean}
   */
  _hasSmallIncrementModifier(event) {
    const modifier =
      lazy.AppConstants.platform === "macosx" ? "altKey" : "ctrlKey";
    return event[modifier] === true;
  },

  /**
   * Parses the value to check if it is a dimension
   * e.g. if the input is "128px" it will return an object like
   * { groups: { value: "128", unit: "px"}}
   *
   * @param  {String} value
   * @returns {Object|null}
   */
  _parseDimension(value) {
    // The regex handles values like +1, -1, 1e4, .4, 1.3e-4, 1.567
    const cssDimensionRegex =
      /^(?<value>[+-]?(\d*\.)?\d+(e[+-]?\d+)?)(?<unit>(%|[a-zA-Z]+))$/;
    return value.match(cssDimensionRegex);
  },

  /**
   * Check if a textProperty value is supported to add the dragging feature
   *
   * @param  {TextProperty} textProperty
   * @returns {Boolean}
   */
  _isDraggableProperty(textProperty) {
    // Check if the feature is explicitly disabled.
    if (!this.ruleView.draggablePropertiesEnabled) {
      return false;
    }
    // temporary way of fixing the bug when editing inline styles
    // otherwise the textPropertyEditor object is destroyed on each value edit
    // See Bug 1755024
    if (this.rule.domRule.type == ELEMENT_STYLE) {
      return false;
    }

    const nbValues = textProperty.value.split(" ").length;
    if (nbValues > 1) {
      // we do not support values like "1px solid red" yet
      // See 1755025
      return false;
    }

    const dimensionMatchObj = this._parseDimension(textProperty.value);
    return !!dimensionMatchObj;
  },

  _draggingOnPointerDown(event) {
    // We want to handle a drag during a mouse button is pressed.  So, we can
    // ignore pointer events which are caused by other devices.
    if (event.pointerType != "mouse") {
      return;
    }

    this._isDragging = true;
    this.valueSpan.setPointerCapture(event.pointerId);
    this._capturingPointerId = event.pointerId;
    this._draggingController = new AbortController();
    const { signal } = this._draggingController;

    // turn off user-select in CSS when we drag
    this.valueSpan.classList.add(IS_DRAGGING_CLASSNAME);

    const dimensionObj = this._parseDimension(this.prop.value);
    const { value, unit } = dimensionObj.groups;
    // `pointerdown.screenX` may be fractional value and we will compare it
    // with `mousemove.screenX` which is always integer value and the difference
    // will be used to compute the new value.  For making the difference always
    // integer, we need to convert it to integer value with the same as
    // MouseEvent does.
    // https://searchfox.org/mozilla-central/rev/7857ea04d142f2abc0d777085f9e54526c7cedf9/dom/events/MouseEvent.cpp#314
    // https://searchfox.org/mozilla-central/rev/7857ea04d142f2abc0d777085f9e54526c7cedf9/gfx/2d/Point.h#85-86
    const intScreenX = Math.floor(event.screenX + 0.5);
    this._draggingValueCache = {
      isInDeadzone: true,
      previousScreenX: intScreenX,
      value: parseFloat(value),
      unit,
    };

    // "pointermove" is fired when the button state is changed too.  Therefore,
    // we should listen to "mousemove" to handle the pointer position changes.
    this.valueSpan.addEventListener("mousemove", this._draggingOnMouseMove, {
      signal,
    });
    this.valueSpan.addEventListener("pointerup", this._draggingOnPointerUp, {
      signal,
    });
    this.valueSpan.addEventListener("keydown", this._draggingOnKeydown, {
      signal,
    });
  },

  _draggingOnMouseMove(event) {
    if (!this._isDragging) {
      return;
    }

    const { isInDeadzone, previousScreenX } = this._draggingValueCache;
    let deltaX = event.screenX - previousScreenX;

    // If `isInDeadzone` is still true, the user has not previously left the deadzone.
    if (isInDeadzone) {
      // If the mouse is still in the deadzone, bail out immediately.
      if (Math.abs(deltaX) < DRAGGING_DEADZONE_DISTANCE) {
        return;
      }

      // Otherwise, remove the DRAGGING_DEADZONE_DISTANCE from the current deltaX, so that
      // the value does not update too abruptly.
      deltaX =
        Math.sign(deltaX) * (Math.abs(deltaX) - DRAGGING_DEADZONE_DISTANCE);

      // Update the state to remember the user is out of the deadzone.
      this._draggingValueCache.isInDeadzone = false;
    }

    let draggingSpeed = DEFAULT_DRAGGING_SPEED;
    if (event.shiftKey) {
      draggingSpeed = FAST_DRAGGING_SPEED;
    } else if (this._hasSmallIncrementModifier(event)) {
      draggingSpeed = SLOW_DRAGGING_SPEED;
    }

    const delta = deltaX * draggingSpeed;
    this._draggingValueCache.previousScreenX = event.screenX;
    this._draggingValueCache.value += delta;

    if (delta == 0) {
      return;
    }

    const { value, unit } = this._draggingValueCache;
    // We use toFixed to avoid the case where value is too long, 9.00001px for example
    const roundedValue = Number.isInteger(value) ? value : value.toFixed(1);
    this.prop
      .setValue(roundedValue + unit, this.prop.priority)
      .then(() => this.ruleView.emitForTests("property-updated-by-dragging"));
    this._hasDragged = true;
  },

  _draggingOnPointerUp() {
    if (!this._isDragging) {
      return;
    }
    if (this._hasDragged) {
      this.committed.value = this.prop.value;
      this.prop.setEnabled(true);
    }
    this._onStopDragging();
  },

  _draggingOnKeydown(event) {
    if (event.key == "Escape") {
      this.prop.setValue(this.committed.value, this.committed.priority);
      this._onStopDragging();
      event.preventDefault();
    }
  },

  _onStopDragging() {
    // childHasDragged is used to stop the propagation of a click event when we
    // release the mouse in the ruleview.
    // The click event is not emitted when we have a pending click on the text property.
    if (this._hasDragged && !this._hasPendingClick) {
      this.ruleView.childHasDragged = true;
    }
    this._isDragging = false;
    this._hasDragged = false;
    this._draggingValueCache = null;
    if (this._capturingPointerId !== null) {
      this._capturingPointerId = null;
      try {
        this.valueSpan.releasePointerCapture(this._capturingPointerId);
      } catch (e) {
        // Ignore exception even if the pointerId has already been invalidated
        // before the capture has already been released implicitly.
      }
    }
    this.valueSpan.classList.remove(IS_DRAGGING_CLASSNAME);
    this._draggingController.abort();
  },

  /**
   * add event listeners to add the ability to modify any size value
   * by dragging the mouse horizontally
   */
  _addDraggingCapability() {
    if (this.valueSpan.classList.contains(DRAGGABLE_VALUE_CLASSNAME)) {
      return;
    }
    this.valueSpan.classList.add(DRAGGABLE_VALUE_CLASSNAME);
    this.valueSpan.addEventListener(
      "pointerdown",
      this._draggingOnPointerDown,
      { passive: true }
    );
  },

  _removeDraggingCapacity() {
    if (!this.valueSpan.classList.contains(DRAGGABLE_VALUE_CLASSNAME)) {
      return;
    }
    this._draggingController = null;
    this.valueSpan.classList.remove(DRAGGABLE_VALUE_CLASSNAME);
    this.valueSpan.removeEventListener(
      "pointerdown",
      this._draggingOnPointerDown,
      { passive: true }
    );
  },

  /**
   * Validate this property. Does it make sense for this value to be assigned
   * to this property name? This does not apply the property value
   *
   * @return {Boolean} true if the property name + value pair is valid, false otherwise.
   */
  isValid() {
    return this.prop.isValid();
  },

  /**
   * Validate the name of this property.
   * @return {Boolean} true if the property name is valid, false otherwise.
   */
  isNameValid() {
    return this.prop.isNameValid();
  },

  isInvalidAtComputedValueTime() {
    return this.prop.isInvalidAtComputedValueTime();
  },

  /**
   * Display grid-template-area value strings each on their own line
   * to display it in an ascii-art style matrix
   */
  _formatGridTemplateAreasValue() {
    this.valueSpan.classList.add("ruleview-propertyvalue-break-spaces");

    let quoteSymbolsUsed = [];

    const getQuoteSymbolsUsed = cssValue => {
      const regex = /\"|\'/g;
      const found = cssValue.match(regex);
      quoteSymbolsUsed = found.filter((_, i) => i % 2 === 0);
    };

    getQuoteSymbolsUsed(this.valueSpan.innerText);

    this.valueSpan.innerText = this.valueSpan.innerText
      .split('"')
      .filter(s => s !== "")
      .map(s => s.split("'"))
      .flat()
      .map(s => s.trim().replace(/\s+/g, " "))
      .filter(s => s.length)
      .map(line => line.split(" "))
      .map((line, i, lines) =>
        line.map((col, j) =>
          col.padEnd(Math.max(...lines.map(l => l[j].length)), " ")
        )
      )
      .map(
        (line, i) =>
          `\n${quoteSymbolsUsed[i]}` + line.join(" ") + quoteSymbolsUsed[i]
      )
      .join(" ");
  },
};

module.exports = TextPropertyEditor;
