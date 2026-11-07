/* Any copyright is dedicated to the Public Domain.
 http://creativecommons.org/publicdomain/zero/1.0/ */
/* eslint no-unused-vars: [2, {"vars": "local"}] */

"use strict";

// Import the inspector's head.js first (which itself imports shared-head.js).
Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/devtools/client/inspector/test/head.js",
  this
);

var {
  getInplaceEditorForSpan: inplaceEditor,
} = require("resource://devtools/client/shared/inplace-editor.js");

const {
  COMPATIBILITY_TOOLTIP_MESSAGE,
} = require("resource://devtools/client/inspector/rules/constants.js");

const ROOT_TEST_DIR = getRootDirectory(gTestPath);

const STYLE_INSPECTOR_L10N = new LocalizationHelper(
  "devtools/shared/locales/styleinspector.properties"
);

const InactiveCssTooltipHelper = require("resource://devtools/client/shared/widgets/tooltip/inactive-css-tooltip-helper.js");
const CssCompatibilityTooltipHelper = require("resource://devtools/client/shared/widgets/tooltip/css-compatibility-tooltip-helper.js");

/**
 * When a tooltip is closed, this ends up "commiting" the value changed within
 * the tooltip (e.g. the color in case of a colorpicker) which, in turn, ends up
 * setting the value of the corresponding css property in the rule-view.
 * Use this function to close the tooltip and make sure the test waits for the
 * ruleview-changed event.
 *
 * @param {SwatchBasedEditorTooltip} editorTooltip
 * @param {CSSRuleView} view
 */
async function hideTooltipAndWaitForRuleViewChanged(editorTooltip, view) {
  const onModifications = view.once("property-value-updated");
  const onHidden = editorTooltip.tooltip.once("hidden");
  editorTooltip.hide();
  await onModifications;
  await onHidden;
}

/**
 * Polls a given generator function waiting for it to return true.
 *
 * @param {Function} validatorFn
 *        A validator generator function that returns a boolean.
 *        This is called every few milliseconds to check if the result is true.
 *        When it is true, the promise resolves.
 * @param {string} name
 *        Optional name of the test. This is used to generate
 *        the success and failure messages.
 * @return a promise that resolves when the function returned true or rejects
 * if the timeout is reached
 */
var waitForSuccess = async function (validatorFn, desc = "untitled") {
  let i = 0;
  while (true) {
    info("Checking: " + desc);
    if (await validatorFn()) {
      ok(true, "Success: " + desc);
      break;
    }
    i++;
    if (i > 10) {
      ok(false, "Failure: " + desc);
      break;
    }
    await new Promise(r => setTimeout(r, 200));
  }
};

/**
 * Simulate a color change in a given color picker tooltip, and optionally wait
 * for a given element in the page to have its style changed as a result.
 * Note that this function assumes that the colorpicker popup is already open
 * and it won't close it after having selected the new color.
 *
 * @param {RuleView} ruleView
 *        The related rule view instance
 * @param {SwatchColorPickerTooltip} colorPicker
 * @param {Array} newRgba
 *        The new color to be set [r, g, b, a]
 * @param {object} expectedChange
 *        Optional object that needs the following props:
 *          - {String} selector The selector to the element in the page that
 *            will have its style changed.
 *          - {String} name The style name that will be changed
 *          - {String} value The expected style value
 * The style will be checked like so: getComputedStyle(element)[name] === value
 */
var simulateColorPickerChange = async function (
  ruleView,
  colorPicker,
  newRgba,
  expectedChange
) {
  let onComputedStyleChanged;
  if (expectedChange) {
    const { selector, name, value } = expectedChange;
    onComputedStyleChanged = waitForComputedStyleProperty(
      selector,
      null,
      name,
      value
    );
  }
  const onRuleViewChanged = ruleView.once("ruleview-changed");
  info("Getting the spectrum colorpicker object");
  const spectrum = colorPicker.spectrum;
  info("Setting the new color");
  spectrum.rgb = newRgba;
  info("Applying the change");
  spectrum.updateUI();
  spectrum.onChange();
  info("Waiting for rule-view to update");
  await onRuleViewChanged;

  if (expectedChange) {
    info("Waiting for the style to be applied on the page");
    await onComputedStyleChanged;
  }
};

/**
 * Open the color picker popup for a given property in a given rule and
 * simulate a color change. Optionally wait for a given element in the page to
 * have its style changed as a result.
 *
 * @param {RuleView} view
 *        The related rule view instance
 * @param {number} ruleIndex
 *        Which rule to target in the rule view
 * @param {number} propIndex
 *        Which property to target in the rule
 * @param {Array} newRgba
 *        The new color to be set [r, g, b, a]
 * @param {object} expectedChange
 *        Optional object that needs the following props:
 *          - {String} selector The selector to the element in the page that
 *            will have its style changed.
 *          - {String} name The style name that will be changed
 *          - {String} value The expected style value
 * The style will be checked like so: getComputedStyle(element)[name] === value
 */
var openColorPickerAndSelectColor = async function (
  view,
  ruleIndex,
  propIndex,
  newRgba,
  expectedChange
) {
  const ruleEditor = getRuleViewRuleEditorAt(view, ruleIndex);
  const propEditor = ruleEditor.rule.textProps[propIndex].editor;
  const swatch = propEditor.valueSpan.querySelector(".inspector-colorswatch");
  const cPicker = view.tooltips.getTooltip("colorPicker");

  info("Opening the colorpicker by clicking the color swatch");
  const onColorPickerReady = cPicker.once("ready");
  swatch.click();
  await onColorPickerReady;

  await simulateColorPickerChange(view, cPicker, newRgba, expectedChange);

  return { propEditor, swatch, cPicker };
};

/**
 * Open the cubicbezier popup for a given property in a given rule and
 * simulate a curve change. Optionally wait for a given element in the page to
 * have its style changed as a result.
 *
 * @param {RuleView} view
 *        The related rule view instance
 * @param {number} ruleIndex
 *        Which rule to target in the rule view
 * @param {number} propIndex
 *        Which property to target in the rule
 * @param {Array} coords
 *        The new coordinates to be used, e.g. [0.1, 2, 0.9, -1]
 * @param {object} expectedChange
 *        Optional object that needs the following props:
 *          - {String} selector The selector to the element in the page that
 *            will have its style changed.
 *          - {String} name The style name that will be changed
 *          - {String} value The expected style value
 * The style will be checked like so: getComputedStyle(element)[name] === value
 */
var openCubicBezierAndChangeCoords = async function (
  view,
  ruleIndex,
  propIndex,
  coords,
  expectedChange
) {
  const ruleEditor = getRuleViewRuleEditorAt(view, ruleIndex);
  const propEditor = ruleEditor.rule.textProps[propIndex].editor;
  const swatch = propEditor.valueSpan.querySelector(".inspector-bezierswatch");
  const bezierTooltip = view.tooltips.getTooltip("cubicBezier");

  info("Opening the cubicBezier by clicking the swatch");
  const onBezierWidgetReady = bezierTooltip.once("ready");
  swatch.click();
  await onBezierWidgetReady;

  const widget = await bezierTooltip.widget;

  info("Simulating a change of curve in the widget");
  const onRuleViewChanged = view.once("ruleview-changed");
  widget.coordinates = coords;
  await onRuleViewChanged;

  if (expectedChange) {
    info("Waiting for the style to be applied on the page");
    const { selector, name, value } = expectedChange;
    await waitForComputedStyleProperty(selector, null, name, value);
  }

  return { propEditor, swatch, bezierTooltip };
};

/**
 * Simulate adding a new property in an existing rule in the rule-view.
 *
 * @param {CssRuleView} view
 *        The instance of the rule-view panel
 * @param {number} ruleIndex
 *        The index of the rule to use.
 * @param {string} name
 *        The name for the new property
 * @param {string} value
 *        The value for the new property
 * @param {object=} options
 * @param {string=} options.commitValueWith
 *        Which key should be used to commit the new value. VK_TAB is used by
 *        default, but tests might want to use another key to test cancelling
 *        for exemple.
 *        If set to null, no keys will be hit, so the input will still be focused
 *        at the end of this function
 * @param {boolean=} options.blurNewProperty
 *        After the new value has been added, a new property would have been
 *        focused. This parameter is true by default, and that causes the new
 *        property to be blurred. Set to false if you don't want this.
 * @return {TextProperty} The instance of the TextProperty that was added
 */
var addProperty = async function (
  view,
  ruleIndex,
  name,
  value,
  { commitValueWith = "VK_TAB", blurNewProperty = true } = {}
) {
  info("Adding new property " + name + ":" + value + " to rule " + ruleIndex);

  const ruleEditor = getRuleViewRuleEditorAt(view, ruleIndex);
  let editor = await focusNewRuleViewProperty(ruleEditor);
  const numOfProps = ruleEditor.rule.textProps.length;

  const onMutations = new Promise(r => {
    // If the rule index is 0, then we are updating the rule for the "element"
    // selector in the rule view.
    // This rule is actually updating the style attribute of the element, and
    // therefore we can expect mutations.
    // For any other rule index, no mutation should be created, we can resolve
    // immediately.
    if (ruleIndex !== 0) {
      r();
    }

    // Use CSS.escape for the name in order to match the logic at
    // devtools/client/fronts/inspector/rule-rewriter.js
    // This leads to odd values in the style attribute and might change in the
    // future. See https://bugzilla.mozilla.org/show_bug.cgi?id=1765943
    const expectedAttributeValue = `${CSS.escape(name)}: ${value}`;
    view.inspector.walker.on(
      "mutations",
      function onWalkerMutations(mutations) {
        // Wait until we receive a mutation which updates the style attribute
        // with the expected value.
        const receivedLastMutation = mutations.some(
          mut =>
            mut.attributeName === "style" &&
            mut.newValue.includes(expectedAttributeValue)
        );
        if (receivedLastMutation) {
          view.inspector.walker.off("mutations", onWalkerMutations);
          r();
        }
      }
    );
  });

  info(`Adding name "${name}"`);
  editor.input.value = name;
  is(
    editor.input.getAttribute("aria-label"),
    "New property name",
    "New property name input has expected aria-label"
  );

  const onNameAdded = view.once("ruleview-changed");
  EventUtils.synthesizeKey("VK_TAB", {}, view.styleWindow);
  await onNameAdded;

  // Focus has moved to the value inplace-editor automatically.
  editor = inplaceEditor(view.styleDocument.activeElement);
  const textProps = ruleEditor.rule.textProps;
  const textProp = textProps.at(-1);

  is(
    ruleEditor.rule.textProps.length,
    numOfProps + 1,
    "A new test property was added"
  );
  is(
    editor,
    inplaceEditor(textProp.editor.valueSpan),
    "The inplace editor appeared for the value"
  );

  ok(
    !!editor.input.getAttribute("aria-labelledby"),
    "The value input has an aria-labelledby attribute…"
  );
  is(
    editor.input.getAttribute("aria-labelledby"),
    textProp.editor.nameSpan.id,
    "…which references the property name input"
  );

  // At first, we have an empty input and in most of the case, that might show the
  // autocomplete popup
  if (editor.cssProperties.getValues(name).length) {
    info("Wait for the popup to open");
    await waitFor(() => editor.popup.isOpen);
  }

  info(`Setting the value for "${name}": "${value}"`);
  // Setting the input value schedules a preview to be shown in 10ms which
  // triggers a ruleview-changed event (see bug 1209295).
  if (value) {
    const onAfterSuggest = editor.once("after-suggest");

    // Setting the input value to the wanted value minus the last char…
    editor.input.value = value.substring(0, value.length - 1);
    // …so we can actually simulate typing the last char, which will trigger autocomplete
    // code in the InplaceEditor. Attempts to simulate typing the whole string was triggering
    // too many events that wouldn't be ideal to track here.
    EventUtils.sendString(value.at(-1), view.styleWindow);
    await onAfterSuggest;
    const onPreview = view.once("ruleview-changed");
    // Flush debounce so it does trigger the preview code
    view.debounce.flush();
    info("Waiting for rule view updates after setting the value");
    await onPreview;
  }

  if (commitValueWith === null) {
    return textProp;
  }

  const onModifications = view.once("property-value-updated");
  EventUtils.synthesizeKey(commitValueWith, {}, view.styleWindow);
  info("Waiting for property value update after commit key is pressed");
  await onModifications;

  info(
    "Waiting for DOM mutations in case the property was added to the element style"
  );
  await onMutations;

  const prop = textProps.at(-1);
  is(prop.name, name, "The new/last property name has the expected name");
  is(
    prop.value + (prop.priority ? " !" + prop.priority : ""),
    value,
    "The new/last property value has the expected value"
  );

  if (blurNewProperty) {
    view.styleDocument.activeElement.blur();
  }

  return textProp;
};

/**
 * Change the name of a property in a rule in the rule-view.
 *
 * @param {CssRuleView} view
 *        The instance of the rule-view panel.
 * @param {TextProperty} textProp
 *        The instance of the TextProperty to be changed.
 * @param {string} name
 *        The new property name.
 */
var renameProperty = async function (view, textProp, name) {
  await focusEditableField(view, textProp.editor.nameSpan);

  const onNameDone = view.once("ruleview-changed");
  info(`Rename the property to ${name}`);
  EventUtils.sendString(name, view.styleWindow);
  EventUtils.synthesizeKey("VK_RETURN", {}, view.styleWindow);
  info("Wait for property name.");
  await onNameDone;

  if (
    !Services.prefs.getBoolPref("devtools.inspector.rule-view.focusNextOnEnter")
  ) {
    return;
  }

  // Renaming the property auto-advances the focus to the value input. Exiting without
  // committing will still fire a change event. @see TextPropertyEditor.#onValueDone().
  // Wait for that event too before proceeding.
  const onValueDone = view.once("property-value-updated");
  EventUtils.synthesizeKey("VK_ESCAPE", {}, view.styleWindow);
  await onValueDone;
};

/**
 * Simulate removing a property from an existing rule in the rule-view.
 *
 * @param {CssRuleView} view
 *        The instance of the rule-view panel
 * @param {TextProperty} textProp
 *        The instance of the TextProperty to be removed
 * @param {boolean} blurNewProperty
 *        After the property has been removed, a new property would have been
 *        focused. This parameter is true by default, and that causes the new
 *        property to be blurred. Set to false if you don't want this.
 */
var removeProperty = async function (view, textProp, blurNewProperty = true) {
  await focusEditableField(view, textProp.editor.nameSpan);

  const onModifications = view.once("ruleview-changed");
  info("Deleting the property name now");
  EventUtils.synthesizeKey("VK_DELETE", {}, view.styleWindow);
  EventUtils.synthesizeKey("VK_TAB", {}, view.styleWindow);
  await onModifications;

  if (blurNewProperty) {
    view.styleDocument.activeElement.blur();
  }
};

/**
 * Simulate clicking the enable/disable checkbox next to a property in a rule.
 *
 * @param {CssRuleView} view
 *        The instance of the rule-view panel
 * @param {TextProperty} textProp
 *        The instance of the TextProperty to be enabled/disabled
 */
var togglePropStatus = async function (view, textProp) {
  const onRuleViewRefreshed = view.once("ruleview-changed");
  textProp.editor.enable.click();
  await onRuleViewRefreshed;
};

/**
 * Create a new rule by clicking on the "add rule" button.
 * This will leave the selector inplace-editor active.
 *
 * @param {InspectorPanel} inspector
 *        The instance of InspectorPanel currently loaded in the toolbox
 * @param {CssRuleView} view
 *        The instance of the rule-view panel
 * @returns {Rule} a promise that resolves the new model Rule after the rule has
 *          been added
 */
async function addNewRule(inspector, view) {
  const onNewRuleAdded = view.once("new-rule-added");
  info("Adding the new rule using the button");
  view.addRuleButton.click();

  info("Waiting for new-rule-added event…");
  const rule = await onNewRuleAdded;
  info("…received new-rule-added");

  return rule;
}

/**
 * Create a new rule by clicking on the "add rule" button, dismiss the editor field and
 * verify that the selector is correct.
 *
 * @param {InspectorPanel} inspector
 *        The instance of InspectorPanel currently loaded in the toolbox
 * @param {CssRuleView} view
 *        The instance of the rule-view panel
 * @param {string} expectedSelector
 *        The value we expect the selector to have
 * @param {number} expectedIndex
 *        The index we expect the rule to have in the rule-view
 * @returns {Rule} a promise that resolves the new model Rule after the rule has
 *          been added
 */
async function addNewRuleAndDismissEditor(
  inspector,
  view,
  expectedSelector,
  expectedIndex
) {
  const rule = await addNewRule(inspector, view);

  info("Getting the new rule at index " + expectedIndex);
  const ruleEditor = getRuleViewRuleEditorAt(view, expectedIndex);
  const editor = ruleEditor.selectorText.ownerDocument.activeElement;
  is(
    editor.value,
    expectedSelector,
    "The editor for the new selector has the correct value: " + expectedSelector
  );

  info("Pressing escape to leave the editor");
  EventUtils.synthesizeKey("KEY_Escape");

  is(
    ruleEditor.selectorText.textContent,
    expectedSelector,
    "The new selector has the correct text: " + expectedSelector
  );

  return rule;
}

/**
 * Simulate a sequence of non-character keys (return, escape, tab) and wait for
 * a given element to receive the focus.
 *
 * @param {CssRuleView} view
 *        The instance of the rule-view panel
 * @param {DOMNode} element
 *        The element that should be focused
 * @param {Array} keys
 *        Array of non-character keys, the part that comes after "DOM_VK_" eg.
 *        "RETURN", "ESCAPE"
 * @return a promise that resolves after the element received the focus
 */
async function sendKeysAndWaitForFocus(view, element, keys) {
  const onFocus = once(element, "focus", true);
  for (const key of keys) {
    EventUtils.sendKey(key, view.styleWindow);
  }
  await onFocus;
}

/**
 * Wait for a markupmutation event on the inspector that is for a style modification.
 *
 * @param {InspectorPanel} inspector
 * @return {Promise}
 */
function waitForStyleModification(inspector) {
  return new Promise(function (resolve) {
    function checkForStyleModification(mutations) {
      for (const mutation of mutations) {
        if (
          mutation.type === "attributes" &&
          mutation.attributeName === "style"
        ) {
          inspector.off("markupmutation", checkForStyleModification);
          resolve();
          return;
        }
      }
    }
    inspector.on("markupmutation", checkForStyleModification);
  });
}

/**
 * Click on the icon next to the selector of a CSS rule in the Rules view
 * to toggle the selector highlighter. If a selector highlighter is not already visible
 * for the given selector, wait for it to be shown. Otherwise, wait for it to be hidden.
 *
 * @param {CssRuleView} view
 *        The instance of the Rules view
 * @param {string} selectorText
 *        The selector of the CSS rule to look for
 * @param {number} index
 *        If there are more CSS rules with the same selector, use this index
 *        to determine which one should be retrieved. Defaults to 0 (first)
 */
async function clickSelectorIcon(view, selectorText, index = 0) {
  const { inspector } = view;
  const rule = getRuleViewRule(view, selectorText, index);

  info(`Waiting for icon to be available for selector: ${selectorText}`);
  const icon = await waitFor(() => {
    return rule.querySelector(".js-toggle-selector-highlighter");
  });

  // Grab the actual selector associated with the matched icon.
  // For inline styles, the CSS rule with the "element" selector actually points to
  // a generated unique selector, for example: "div:nth-child(1)".
  // The selector highlighter is invoked with this unique selector.
  // Continuing to use selectorText ("element") would fail some of the checks below.
  const selector = icon.dataset.computedSelector;

  const { waitForHighlighterTypeShown, waitForHighlighterTypeHidden } =
    getHighlighterTestHelpers(inspector);

  // If there is an active selector highlighter, get its configuration options.
  // Will be undefined if there isn't an active selector highlighter.
  const options = inspector.highlighters.getOptionsForActiveHighlighter(
    inspector.highlighters.TYPES.SELECTOR
  );

  // If there is already a highlighter visible for this selector,
  // wait for hidden event. Otherwise, wait for shown event.
  const waitForEvent =
    options?.selector === selector
      ? waitForHighlighterTypeHidden(inspector.highlighters.TYPES.SELECTOR)
      : waitForHighlighterTypeShown(inspector.highlighters.TYPES.SELECTOR);

  // Boolean flag whether we waited for a highlighter shown event
  const waitedForShown = options?.selector !== selector;

  info(`Click the icon for selector: ${selectorText}`);
  icon.scrollIntoView();
  EventUtils.synthesizeMouseAtCenter(icon, {}, view.styleWindow);

  // Promise resolves with event data from either highlighter shown or hidden event.
  const data = await waitForEvent;
  return { ...data, isShown: waitedForShown };
}
/**
 * Toggle one of the checkboxes inside the class-panel. Resolved after the DOM mutation
 * has been recorded.
 *
 * @param {CssRuleView} view The rule-view instance.
 * @param {string} name The class name to find the checkbox.
 */
async function toggleClassPanelCheckBox(view, name) {
  info(`Clicking on checkbox for class ${name}`);
  const checkBox = [
    ...view.classPanel.querySelectorAll("[type=checkbox]"),
  ].find(box => {
    return box.dataset.name === name;
  });

  const onMutation = view.inspector.once("markupmutation");
  checkBox.click();
  info("Waiting for a markupmutation as a result of toggling this class");
  await onMutation;
}

/**
 * Verify the content of the class-panel.
 *
 * @param {CssRuleView} view The rule-view instance
 * @param {Array} classes The list of expected classes. Each item in this array is an
 * object with the following properties: {name: {String}, state: {Boolean}}
 */
function checkClassPanelContent(view, classes) {
  const checkBoxNodeList = view.classPanel.querySelectorAll("[type=checkbox]");
  is(
    checkBoxNodeList.length,
    classes.length,
    "The panel contains the expected number of checkboxes"
  );

  for (let i = 0; i < classes.length; i++) {
    is(
      checkBoxNodeList[i].dataset.name,
      classes[i].name,
      `Checkbox ${i} has the right class name`
    );
    is(
      checkBoxNodeList[i].checked,
      classes[i].state,
      `Checkbox ${i} has the right state`
    );
  }
}

/**
 * Opens the eyedropper from the colorpicker tooltip
 * by selecting the colorpicker and then selecting the eyedropper icon
 *
 * @param {view} ruleView
 * @param {swatch} color swatch of a particular property
 */
async function openEyedropper(view, swatch) {
  const tooltip = view.tooltips.getTooltip("colorPicker").tooltip;

  info("Click on the swatch");
  const onColorPickerReady = view.tooltips
    .getTooltip("colorPicker")
    .once("ready");
  EventUtils.synthesizeMouseAtCenter(swatch, {}, swatch.documentGlobal);
  await onColorPickerReady;

  const dropperButton = tooltip.container.querySelector("#eyedropper-button");

  info("Click on the eyedropper icon");
  const onOpened = tooltip.once("eyedropper-opened");
  dropperButton.click();
  await onOpened;
}

/**
 * Gets a set of declarations for a rule index.
 *
 * @param {ruleView} view
 *        The rule-view instance.
 * @param {number} ruleIndex
 *        The index we expect the rule to have in the rule-view.
 *        (the index consider hidden rules when their container is collapsed)
 * @param {boolean} addCompatibilityData
 *        Optional argument to add compatibility dat with the property data
 *
 * @returns A Promise that resolves with a Map containing stringified property declarations e.g.
 *          [
 *            {
 *              "color:red":
 *                {
 *                  propertyName: "color",
 *                  propertyValue: "red",
 *                  warning: "This won't work",
 *                  used: true,
 *                  compatibilityData: {
 *                    isCompatible: true,
 *                  },
 *                }
 *            },
 *            ...
 *          ]
 */
async function getPropertiesForRuleIndex(
  view,
  ruleIndex,
  addCompatibilityData = false
) {
  const declaration = new Map();
  const ruleEditor = getRuleViewRuleEditorAt(view, ruleIndex);

  for (const currProp of ruleEditor?.rule?.textProps || []) {
    const icon = currProp.editor.inactiveCssState;
    const unused = currProp.editor.element.classList.contains("inactive-css");

    let compatibilityData;
    let compatibilityIcon;
    if (addCompatibilityData) {
      compatibilityData = await currProp.isCompatible();
      compatibilityIcon = currProp.editor.compatibilityState;
    }

    declaration.set(`${currProp.name}:${currProp.value}`, {
      propertyName: currProp.name,
      propertyValue: currProp.value,
      icon,
      data: currProp.getInactiveCssData(),
      warning: unused,
      used: !unused,
      ...(addCompatibilityData
        ? {
            compatibilityData,
            compatibilityIcon,
          }
        : {}),
    });
  }

  return declaration;
}

/**
 * Toggle a declaration disabled or enabled.
 *
 * @param {ruleView} view
 *        The rule-view instance
 * @param {number} ruleIndex
 *        The index of the CSS rule where we can find the declaration to be
 *        toggled.
 * @param {object} declaration
 *        An object representing the declaration e.g. { color: "red" }.
 */
async function toggleDeclaration(view, ruleIndex, declaration) {
  const textProp = getTextProperty(view, ruleIndex, declaration);
  const [[name, value]] = Object.entries(declaration);
  const dec = `${name}:${value}`;
  ok(textProp, `Declaration "${dec}" found`);

  const newStatus = textProp.enabled ? "disabled" : "enabled";
  info(`Toggling declaration "${dec}" of rule ${ruleIndex} to ${newStatus}`);

  await togglePropStatus(view, textProp);
  info("Toggled successfully.");
}

/**
 * Update a declaration from a CSS rule in the Rules view
 * by changing its property name, property value or both.
 *
 * @param {RuleView} view
 *        Instance of RuleView.
 * @param {number} ruleIndex
 *        The index of the CSS rule where to find the declaration.
 * @param {object} declaration
 *        An object representing the target declaration e.g. { color: red }.
 * @param {object} newDeclaration
 *        An object representing the desired updated declaration e.g. { display: none }.
 */
async function updateDeclaration(
  view,
  ruleIndex,
  declaration,
  newDeclaration = {}
) {
  const textProp = getTextProperty(view, ruleIndex, declaration);
  const [[name, value]] = Object.entries(declaration);
  const [[newName, newValue]] = Object.entries(newDeclaration);

  if (newName && name !== newName) {
    info(
      `Updating declaration ${name}:${value};
      Changing ${name} to ${newName}`
    );
    await renameProperty(view, textProp, newName);
  }

  if (newValue && value !== newValue) {
    info(
      `Updating declaration ${name}:${value};
      Changing ${value} to ${newValue}`
    );
    await setProperty(view, textProp, newValue);
  }
}

/**
 * Check whether the given CSS declaration is compatible or not
 *
 * @param {ruleView} view
 *        The rule-view instance.
 * @param {number} ruleIndex
 *        The index we expect the rule to have in the rule-view.
 * @param {object} declaration
 *        An object representing the declaration e.g. { color: "red" }.
 * @param {object} options
 * @param {string | undefined} options.expected
 *        Expected message ID for the given incompatible property.
 * If the expected message is not specified (undefined), the given declaration
 * is inferred as cross-browser compatible and is tested for same.
 * @param {string | null | undefined} options.expectedLearnMoreUrl
 *        Expected learn more link. Pass `null` to check that no "Learn more" link is displayed.
 */
async function checkDeclarationCompatibility(
  view,
  ruleIndex,
  declaration,
  { expected, expectedLearnMoreUrl }
) {
  const declarations = await getPropertiesForRuleIndex(view, ruleIndex, true);
  const [[name, value]] = Object.entries(declaration);
  const dec = `${name}:${value}`;
  const { compatibilityData } = declarations.get(dec);

  is(
    !expected,
    compatibilityData.isCompatible,
    `"${dec}" has the correct compatibility status in the payload`
  );

  is(compatibilityData.msgId, expected, `"${dec}" has expected message ID`);

  if (expected) {
    await checkInteractiveTooltip(
      view,
      "compatibility-tooltip",
      ruleIndex,
      declaration
    );
  }

  if (expectedLearnMoreUrl !== undefined) {
    // Show the tooltip
    const tooltip = view.tooltips.getTooltip("interactiveTooltip");
    const onTooltipReady = tooltip.once("shown");
    const { compatibilityIcon } = declarations.get(dec);
    await view.tooltips.onInteractiveTooltipTargetHover(compatibilityIcon);
    tooltip.show(compatibilityIcon);
    await onTooltipReady;

    const learnMoreEl = tooltip.panel.querySelector(".link");
    if (expectedLearnMoreUrl === null) {
      ok(!learnMoreEl, `"${dec}" has no "Learn more" link`);
    } else {
      ok(learnMoreEl, `"${dec}" has a "Learn more" link`);

      const { link } = await simulateLinkClick(learnMoreEl);
      is(
        link,
        expectedLearnMoreUrl,
        `Click on ${dec} "Learn more" link navigates user to expected url`
      );
    }

    // Hide the tooltip.
    const onTooltipHidden = tooltip.once("hidden");
    tooltip.hide();
    await onTooltipHidden;
  }
}

/**
 * Check that a declaration is marked inactive and that it has the expected
 * warning.
 *
 * @param {ruleView} view
 *        The rule-view instance.
 * @param {number} ruleIndex
 *        The index we expect the rule to have in the rule-view.
 * @param {object} declaration
 *        An object representing the declaration e.g. { color: "red" }.
 * @param {string} expectedMsgId
 *        The expected inactive CSS msgId that should be displayed in the inactive CSS tooltip
 */
async function checkDeclarationIsInactive(
  view,
  ruleIndex,
  declaration,
  expectedMsgId
) {
  const declarations = await getPropertiesForRuleIndex(view, ruleIndex);
  const [[name, value]] = Object.entries(declaration);
  const dec = `${name}:${value}`;
  const { used, warning, icon, data } = declarations.get(dec);

  ok(!used, `"${dec}" is inactive`);
  ok(warning, `"${dec}" has a warning`);
  ok(
    icon.classList.contains("ruleview-inactive-css-warning"),
    "Icon has expected icon"
  );
  is(icon.hidden, false, "Icon is visible");
  is(data.msgId, expectedMsgId, `"${dec}" has expected inactive CSS msgId`);

  await checkInteractiveTooltip(
    view,
    "inactive-css-tooltip",
    ruleIndex,
    declaration
  );
}

/**
 * Check that a declaration is marked active.
 *
 * @param {ruleView} view
 *        The rule-view instance.
 * @param {number | Array} ruleIndex
 *        The index we expect the rule to have in the rule-view. If an array, the first
 *        item is the children index in the rule view, and the second item is the child
 *        node index in the retrieved rule view element. This is helpful to select rules
 *        inside the pseudo element section.
 * @param {object} declaration
 *        An object representing the declaration e.g. { color: "red" }.
 */
async function checkDeclarationIsActive(view, ruleIndex, declaration) {
  const declarations = await getPropertiesForRuleIndex(view, ruleIndex);
  const [[name, value]] = Object.entries(declaration);
  const dec = `${name}:${value}`;
  const { used, warning } = declarations.get(dec);

  ok(used, `${dec} is active`);
  ok(!warning, `${dec} has no warning`);
}

/**
 * Check that a tooltip contains the correct value.
 *
 * @param {ruleView} view
 *        The rule-view instance.
 *  @param {string} type
 *        The interactive tooltip type being tested.
 * @param {number} ruleIndex
 *        The index we expect the rule to have in the rule-view.
 * @param {object} declaration
 *        An object representing the declaration e.g. { color: "red" }.
 */
async function checkInteractiveTooltip(view, type, ruleIndex, declaration) {
  // Get the declaration
  const declarations = await getPropertiesForRuleIndex(
    view,
    ruleIndex,
    type === "compatibility-tooltip"
  );
  const [[name, value]] = Object.entries(declaration);
  const dec = `${name}:${value}`;

  // Get the relevant icon and tooltip payload data
  let icon;
  let data;
  if (type === "inactive-css-tooltip") {
    ({ icon, data } = declarations.get(dec));
  } else {
    const { compatibilityIcon, compatibilityData } = declarations.get(dec);
    icon = compatibilityIcon;
    data = compatibilityData;
  }

  // Get the tooltip.
  const tooltip = view.tooltips.getTooltip("interactiveTooltip");

  // Get the necessary tooltip helper to fetch the Fluent template.
  let tooltipHelper;
  if (type === "inactive-css-tooltip") {
    tooltipHelper = new InactiveCssTooltipHelper(data, tooltip);
  } else if (type === "compatibility-tooltip") {
    tooltipHelper = new CssCompatibilityTooltipHelper();
  } else {
    throw new Error(`Unsupported "${type}" tooltip`);
  }

  // Get the HTML template.
  const template = tooltipHelper.getTemplate(data, tooltip);

  // Translate the template using Fluent.
  const { doc } = tooltip;
  await doc.l10n.translateFragment(template);

  // Get the expected HTML content of the now translated template.
  const expected = template.firstElementChild.outerHTML;

  // Show the tooltip for the correct icon.
  const onTooltipReady = tooltip.once("shown");
  await view.tooltips.onInteractiveTooltipTargetHover(icon);
  tooltip.show(icon);
  await onTooltipReady;

  // Get the tooltip's actual HTML content.
  const actual = tooltip.panel.firstElementChild.outerHTML;

  // Hide the tooltip.
  const onTooltipHidden = tooltip.once("hidden");
  tooltip.hide();
  await onTooltipHidden;

  // Finally, check the values.
  is(actual, expected, "Tooltip contains the correct value.");
}

/**
 * CSS compatibility test runner.
 *
 * @param {ruleView} view
 *        The rule-view instance.
 * @param {InspectorPanel} inspector
 *        The instance of InspectorPanel currently loaded in the toolbox.
 * @param {Array} tests
 *        An array of test object for this method to consume e.g.
 *          [
 *            {
 *              selector: "#flex-item",
 *              rules: [
 *                // Rule Index: 0
 *                {
 *                  // If the object doesn't include the "expected"
 *                  // key, we consider the declaration as
 *                  // cross-browser compatible and test for same
 *                  color: { value: "green" },
 *                },
 *                // Rule Index: 1
 *                {
 *                  cursor:
 *                  {
 *                    value: "grab",
 *                    expected: INCOMPATIBILITY_TOOLTIP_MESSAGE.default,
 *                    expectedLearnMoreUrl: "https://developer.mozilla.org/en-US/docs/Web/CSS/Reference/Properties/cursor",
 *                  },
 *                },
 *              ],
 *            },
 *            ...
 *          ]
 */
async function runCSSCompatibilityTests(view, inspector, tests) {
  for (const test of tests) {
    if (test.selector) {
      await selectNode(test.selector, inspector);
    }

    for (const [ruleIndex, rules] of test.rules.entries()) {
      for (const rule in rules) {
        await checkDeclarationCompatibility(
          view,
          ruleIndex,
          {
            [rule]: rules[rule].value,
          },
          {
            expected: rules[rule].expected,
            expectedLearnMoreUrl: rules[rule].expectedLearnMoreUrl,
          }
        );
      }
    }
  }
}

/**
 * Inactive CSS test runner.
 *
 * @param {ruleView} view
 *        The rule-view instance.
 * @param {InspectorPanel} inspector
 *        The instance of InspectorPanel currently loaded in the toolbox.
 * @param {Array} tests
 *        An array of test object for this method to consume e.g.
 *          [
 *            {
 *              selector: "#flex-item",
 *              // or
 *              selectNode: (inspector) => { // custom select logic }
 *              activeDeclarations: [
 *                {
 *                  declarations: {
 *                    "order": "2",
 *                  },
 *                  ruleIndex: 0,
 *                },
 *                {
 *                  declarations: {
 *                    "flex-basis": "auto",
 *                    "flex-grow": "1",
 *                    "flex-shrink": "1",
 *                  },
 *                  ruleIndex: 1,
 *                },
 *              ],
 *              inactiveDeclarations: [
 *                {
 *                  declaration: {
 *                    "flex-direction": "row",
 *                  },
 *                  ruleIndex: [1, 0],
 *                  msgId: "inactive-css-not-grid-or-flex-container-or-multicol-container",
 *                },
 *              ],
 *            },
 *            ...
 *          ]
 */
async function runInactiveCSSTests(view, inspector, tests) {
  for (const test of tests) {
    if (test.selector) {
      await selectNode(test.selector, inspector);
    } else if (typeof test.selectNode === "function") {
      await test.selectNode(inspector);
    }

    if (test.activeDeclarations) {
      info("Checking whether declarations are marked as used.");

      for (const activeDeclarations of test.activeDeclarations) {
        for (const [name, value] of Object.entries(
          activeDeclarations.declarations
        )) {
          await checkDeclarationIsActive(view, activeDeclarations.ruleIndex, {
            [name]: value,
          });
        }
      }
    }

    if (test.inactiveDeclarations) {
      info("Checking that declarations are unused and have a warning.");

      for (const inactiveDeclaration of test.inactiveDeclarations) {
        await checkDeclarationIsInactive(
          view,
          inactiveDeclaration.ruleIndex,
          inactiveDeclaration.declaration,
          inactiveDeclaration.msgId
        );
      }
    }
  }
}

/**
 * Return the checkbox element from the Rules view corresponding
 * to the given pseudo-class.
 *
 * @param  {object} view
 *         Instance of RuleView.
 * @param  {string} pseudo
 *         Pseudo-class, like :hover, :active, :focus, etc.
 * @return {HTMLElement}
 */
function getPseudoClassCheckbox(view, pseudo) {
  return view.pseudoClassCheckboxes.filter(
    checkbox => checkbox.value === pseudo
  )[0];
}

/**
 * Open the emulation panel.
 *
 * @param {RuleView} view
 *        Instance of RuleView.
 */
async function openEmulationPanel(view) {
  info("Check that the toggle button exists");
  const button = view.inspector.panelDoc.getElementById(
    "emulation-panel-toggle"
  );
  ok(button, "The emulation panel toggle button exists");
  is(view.emulationToggle, button, "The rule-view refers to the right element");
  is(
    view.inspector.panelDoc.getElementById(
      button.getAttribute("aria-controls")
    ),
    view.emulationPanel,
    "The emulation panel toggle button has valid aria-controls attribute"
  );

  await assertEmulationPanelClosed(view);

  info("Toggle the emulation panel open");
  view.emulationToggle.click();
  await assertEmulationPanelOpened(view);
}

/**
 * Check that the emulation panel is opened.
 *
 * @param {RuleView} view
 *        Instance of RuleView.
 */
async function assertEmulationPanelOpened(view) {
  info("Check the opened state of the emulation panel");
  ok(!view.emulationPanel.inert, "Emulation panel is not inert");
  ok(!view.emulationPanel.hidden, "Emulation panel opened");
  is(
    view.emulationToggle.getAttribute("aria-pressed"),
    "true",
    "The toggle button is pressed"
  );
}

/**
 * Check that the emulation panel is closed.
 *
 * @param {RuleView} view
 *        Instance of RuleView.
 */
async function assertEmulationPanelClosed(view) {
  info("Check the closed state of the emulation panel");
  ok(view.emulationPanel.inert, "Emulation panel is inert");
  ok(view.emulationPanel.hidden, "Emulation panel hidden");
  is(
    view.emulationToggle.getAttribute("aria-pressed"),
    "false",
    "The toggle button is not pressed"
  );
}

/**
 * Check that the CSS variable output has the expected class name and data attribute.
 *
 * @param {RulesView} view
 *        The RulesView instance.
 * @param {string} selector
 *        Selector name for a rule. (e.g. "div", "div::before" and ".sample" etc);
 * @param {string} propertyName
 *        Property name (e.g. "color" and "padding-top" etc);
 * @param {string} expectedClassName
 *        The class name the variable should have.
 * @param {string} expectedDatasetValue
 *        The variable data attribute value.
 */
function checkCSSVariableOutput(
  view,
  selector,
  propertyName,
  expectedClassName,
  expectedDatasetValue
) {
  const target = getRuleViewProperty(
    view,
    selector,
    propertyName
  ).valueSpan.querySelector(`.${expectedClassName}`);

  ok(target, "The target element should exist");
  is(target.dataset.variable, expectedDatasetValue);
}

/**
 * Return specific rule ancestor data element (i.e. the one containing @layer / @media
 * information) from the Rules view
 *
 * @param {RulesView} view
 *        The RulesView instance.
 * @param {number} ruleIndex
 * @returns {HTMLElement}
 */
function getRuleViewAncestorRulesDataElementByIndex(view, ruleIndex) {
  return view.styleDocument
    .querySelectorAll(`.ruleview-rule`)
    [ruleIndex]?.querySelector(`.ruleview-rule-ancestor-data`);
}

/**
 * Return specific rule ancestor data text from the Rules view.
 * Will return something like "@layer topLayer\n@media screen\n@layer".
 *
 * @param {RulesView} view
 *        The RulesView instance.
 * @param {number} ruleIndex
 * @returns {string}
 */
function getRuleViewAncestorRulesDataTextByIndex(view, ruleIndex) {
  return getRuleViewAncestorRulesDataElementByIndex(view, ruleIndex)?.innerText;
}

/**
 * Runs a sequence of tests against the provided property editor.
 *
 * @param {TextPropertyEditor} propertyEditor
 *     The TextPropertyEditor instance to test.
 * @param {RuleView} view
 *     The RuleView which owns the propertyEditor.
 * @param {Array<object>} test
 *     The array of tests to run.
 */
async function runIncrementTest(propertyEditor, view, tests) {
  propertyEditor.valueSpan.scrollIntoView();
  const editor = await focusEditableField(view, propertyEditor.valueSpan);
  const initialValue = editor.property.value;

  for (const testIndex in tests) {
    await testIncrement(editor, view, tests[testIndex], testIndex);
  }

  // Blur the field to put back the UI in its initial state (and avoid pending
  // requests when the test ends).
  const onRuleViewChanged = view.once(
    initialValue !== editor.property.value
      ? "ruleview-changed"
      : "property-value-updated"
  );
  EventUtils.synthesizeKey("VK_ESCAPE", {}, view.styleWindow);
  view.debounce.flush();
  await onRuleViewChanged;
}

/**
 * Individual test runner for increment tests used via runIncrementTest in
 * browser_rules_edit-property-increments.js and similar tests.
 *
 * Will attempt to increment the value of the provided inplace editor based on
 * the test options provided.
 *
 * @param {InplaceEditor} editor
 *     The InplaceEditor instance to test.
 * @param {RuleView} view
 *     The RuleView which owns the editor.
 * @param {object} test
 * @param {boolean=} test.alt
 *     Whether alt should be depressed.
 * @param {boolean=} test.ctrl
 *     Whether ctrl should be depressed.
 * @param {number=} test.deltaX
 *     Only relevant if test.wheel=true, value of the wheel delta on the horizontal axis.
 * @param {number=} test.deltaY
 *     Only relevant if test.wheel=true, value of the wheel delta on the vertical axis.
 * @param {boolean=} test.down
 *     For key increment tests, whether this should simulate pressing the down
 *     arrow, or the up arrow. down, pagedown and pageup are mutually exclusive.
 * @param {string} test.end
 *     The expected value at the end of the test.
 * @param {boolean=} test.pagedown
 *     For key increment tests, whether this should simulate pressing the
 *     pagedown key. down, pagedown and pageup are mutually exclusive.
 * @param {boolean=} test.pageup
 *     For key increment tests, whether this should simulate pressing the
 *     pageup key. down, pagedown and pageup are mutually exclusive.
 * @param {boolean=} test.selectAll
 *     Whether all the input text should be selected. You can also specify a
 *     range with test.selection.
 * @param {Array<number>=} test.selection
 *     An array of two numbers which corresponds to the initial selection range.
 * @param {boolean=} test.shift
 *     Whether shift should be depressed.
 * @param {string} test.start
 *     The input value at the beginning of the test.
 * @param {boolean=} test.wheel
 *     True if the test should use wheel events to increment the value.
 * @param {number} testIndex
 *     The test index, used for logging.
 */
async function testIncrement(editor, view, test, testIndex) {
  editor.input.value = test.start;
  const input = editor.input;

  if (test.selectAll) {
    input.select();
  } else if (test.selection) {
    input.setSelectionRange(test.selection[0], test.selection[1]);
  }

  is(input.value, test.start, "Value initialized at " + test.start);

  const onRuleViewChanged = view.once("ruleview-changed");

  let smallIncrementKey = { ctrlKey: test.ctrl };
  if (AppConstants.platform === "macosx") {
    smallIncrementKey = { altKey: test.alt };
  }

  const options = {
    shiftKey: test.shift,
    ...smallIncrementKey,
  };

  if (test.wheel) {
    // If test.wheel is true, we should increment the value using the wheel.
    const onWheel = once(input, "wheel");
    input.dispatchEvent(
      new view.styleWindow.WheelEvent("wheel", {
        deltaX: test.deltaX,
        deltaY: test.deltaY,
        deltaMode: 0,
        ...options,
      })
    );
    await onWheel;
  } else {
    let key;
    key = test.down ? "VK_DOWN" : "VK_UP";
    if (test.pageDown) {
      key = "VK_PAGE_DOWN";
    } else if (test.pageUp) {
      key = "VK_PAGE_UP";
    }
    const onKeyUp = once(input, "keyup");
    EventUtils.synthesizeKey(key, options, view.styleWindow);

    await onKeyUp;
  }

  // Only expect a change if the value actually changed!
  if (test.start !== test.end) {
    view.debounce.flush();
    await onRuleViewChanged;
  }

  is(input.value, test.end, `[Test ${testIndex}] Value changed to ${test.end}`);
}

function getSmallIncrementKey() {
  if (AppConstants.platform === "macosx") {
    return { alt: true };
  }
  return { ctrl: true };
}

/**
 * Check that the rule view has the expected content
 *
 * @param {RuleView} view
 * @param {object[]} expectedElements
 * @param {string} expectedElements[].selector - The expected selector of the rule. Wrap
 *        unmatched selector with `~~` characters (e.g. "div, ~~unmatched~~")
 * @param {boolean} expectedElements[].selectorEditable - Whether or not the selector can
 *        be edited. Defaults to true.
 * @param {boolean} expectedElements[].hasSelectorHighlighterButton - Whether or not a
 *        selector highlighter button is visible. Defaults to true.
 * @param {string[]|null} expectedElements[].ancestorRulesData - An array of the parent
 *        selectors of the rule, with their indentations and the opening brace.
 *        e.g. for the following rule `html { body { span {} } }`, for the `span` rule,
 *        you should pass:
 *        [
 *          `html {`,
 *          `  & body {`,
 *        ]
 *        Pass `null` if the rule doesn't have a parent rule.
 * @param {boolean|undefined} expectedElements[].inherited - Is the rule an inherited one.
 *        Defaults to false.
 * @param {object[]} expectedElements[].declarations - The expected declarations of the rule.
 * @param {object[]} expectedElements[].declarations[].name - The name of the declaration.
 * @param {object[]} expectedElements[].declarations[].value - The value of the declaration.
 * @param {boolean|undefined} expectedElements[].declarations[].overridden - Is the declaration
 *        overridden by another the declaration. Defaults to false.
 * @param {boolean|undefined} expectedElements[].declarations[].enabled - Is the declaration
 *        enabled (i.e. the checkbox next to it is checked). Defaults to true.
 * @param {boolean|undefined} expectedElements[].declarations[].valid - Is the declaration valid.
 *        Defaults to true.
 * @param {boolean|undefined} expectedElements[].declarations[].dirty - Is the declaration dirty,
 *        i.e. was it added/modified by the user (should have a left green border).
 *        Defaults to false
 * @param {boolean|undefined} expectedElements[].declarations[].inactiveCSS - Is the declaration
 *        inactive.
 * @param {string} expectedElements[].header - If we're expecting a header (Inherited from,
 *        Pseudo-elements, …), the text of said header.
 * @param {string[]} expectedElements[].highlighted - List of highlighted text (when using filter input).
 * @param {string|undefined} message - Optional string to be displayed as prefix of any test log message,
 *        in order to help distinguish multiple calls made to this helper.
 */
function checkRuleViewContent(view, expectedElements, message = "") {
  const prefix = message ? `[${message}] - ` : "";

  const elementsInView = _getRuleViewElements(view);
  is(
    elementsInView.length,
    expectedElements.length,
    `${prefix}All expected elements are displayed`
  );

  expectedElements.forEach((expectedElement, i) => {
    info(`Checking element #${i}: ${expectedElement.selector}`);

    const elementInView = elementsInView[i];

    if (expectedElement.header) {
      is(
        elementInView.getAttribute("role"),
        "heading",
        `${prefix}Element #${i} is a header`
      );
      is(
        elementInView.textContent,
        expectedElement.header,
        `${prefix}Expected header text for element #${i}`
      );
      return;
    }

    const selector = [
      ...elementInView.querySelectorAll(
        // Get the selector parts (.ruleview-selector)
        ".ruleview-selectors-container .ruleview-selector," +
          // as well as the `element` "fake" selector
          ".ruleview-selectors-container.alternative-selector," +
          // and read-only selectors
          `.ruleview-selectors-container.uneditable-selector`
      ),
    ]
      .map(selectorEl => {
        let selectorPart = selectorEl.textContent;
        if (selectorEl.classList.contains("unmatched")) {
          selectorPart = `~~${selectorPart}~~`;
        }
        return selectorPart;
      })
      .join(", ");
    is(
      selector,
      expectedElement.selector,
      `${prefix}Expected selector for element #${i}`
    );
    is(
      elementInView.querySelector(
        `.ruleview-selectors-container:not(.uneditable-selector)`
      ) !== null,
      expectedElement.selectorEditable ?? true,
      `${prefix}Selector for element #${i} (${selector}) ${(expectedElement.selectorEditable ?? true) ? "is" : "isn't"} editable`
    );
    is(
      elementInView.querySelector(`.ruleview-selectorhighlighter`) !== null,
      expectedElement.hasSelectorHighlighterButton ?? true,
      `${prefix}Element #${i} (${selector}) ${(expectedElement.hasSelectorHighlighterButton ?? true) ? "has" : "does not have"} a selector highlighter button`
    );

    const ancestorData = elementInView.querySelector(
      `.ruleview-rule-ancestor-data`
    );
    if (expectedElement.ancestorRulesData == null) {
      is(
        ancestorData,
        null,
        `${prefix}No ancestor rules data displayed for ${selector}`
      );
    } else {
      is(
        ancestorData.innerText,
        expectedElement.ancestorRulesData.join("\n"),
        `${prefix}Expected ancestor rules data displayed for ${selector}`
      );
    }

    const isInherited = elementInView.matches(".ruleview-rule-inherited");
    is(
      isInherited,
      expectedElement.inherited ?? false,
      `${prefix}Element #${i} ("${selector}") is ${expectedElement.inherited ? "inherited" : "not inherited"}`
    );

    const highlightedElements = Array.from(
      elementInView.querySelectorAll(".ruleview-highlight")
    ).map(el => el.textContent.trim());
    Assert.deepEqual(
      highlightedElements,
      expectedElement.highlighted ?? [],
      "The expected elements are highlighted"
    );

    // Bail out if callsite didn't provide any declarations
    // we then ignore checking anything around declarations
    if (!("declarations" in expectedElement)) {
      info(`Ignore declarations for ${selector}`);
      return;
    }

    const ruleViewPropertyElements =
      elementInView.querySelectorAll(".ruleview-property");
    is(
      ruleViewPropertyElements.length,
      expectedElement.declarations.length,
      `${prefix}Got the expected number of declarations for expected element #${i} (${selector})`
    );
    ruleViewPropertyElements.forEach((ruleViewPropertyElement, j) => {
      const [propName, propValue] = Array.from(
        ruleViewPropertyElement.querySelectorAll(
          ".ruleview-propertyname, .ruleview-propertyvalue"
        )
      );

      const expectedDeclaration = expectedElement.declarations[j];
      is(
        propName.innerText,
        expectedDeclaration?.name,
        `${prefix}Got expected property name`
      );
      if (propName.innerText !== expectedDeclaration?.name) {
        // We don't have the expected property name, don't run the other assertions to
        // avoid spamming the output
        return;
      }

      const value = propValue ? propValue.innerText : "";
      is(
        value,
        expectedDeclaration?.value,
        `${prefix}Got expected property value`
      );
      const propPrefix = `${prefix}Element #${i} ("${selector}") declaration #${j} ("${propName.innerText}: ${value}") is `;
      is(
        ruleViewPropertyElement.classList.contains("ruleview-overridden"),
        !!expectedDeclaration?.overridden,
        `${propPrefix}${expectedDeclaration?.overridden ? "overridden" : "not overridden"} `
      );
      const expectedEnabled = expectedDeclaration?.enabled ?? true;
      is(
        ruleViewPropertyElement.querySelector("input.ruleview-enableproperty")
          ?.checked || false,
        expectedEnabled,
        `${propPrefix}${expectedEnabled ? "enabled" : "disabled"} `
      );
      is(
        ruleViewPropertyElement.classList.contains("inactive-css"),
        !!expectedDeclaration?.inactiveCSS,
        `${propPrefix}${expectedDeclaration?.inactiveCSS ? "inactive" : "not inactive"} `
      );
      const isWarningIconDisplayed = !!ruleViewPropertyElement.querySelector(
        ".ruleview-warning:not([hidden])"
      );
      const expectedValid = expectedDeclaration?.valid ?? true;
      is(
        !isWarningIconDisplayed,
        expectedValid,
        `${propPrefix}${expectedValid ? "valid" : "invalid"}`
      );
      is(
        !!ruleViewPropertyElement.hasAttribute("dirty"),
        !!expectedDeclaration?.dirty,
        `${propPrefix}${expectedDeclaration?.dirty ? "dirty" : "not dirty"}`
      );
    });
  });
}

/**
 * Check whether the current preferred color scheme is dark.
 *
 * @return {Promise} A promise that resolves to a boolean indicating whether the current preferred color scheme is dark.
 */
function getCurrentPrefersDark() {
  return SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => {
    const { matches } = content.matchMedia("(prefers-color-scheme: dark)");
    return matches;
  });
}

/**
 * Check whether the current prefers-reduced-motion setting is "reduce".
 *
 * @return {Promise} A promise that resolves to a boolean indicating whether the current prefers-reduced-motion setting is "reduce".
 */
function getCurrentPrefersReducedMotionReduce() {
  return SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => {
    const { matches } = content.matchMedia("(prefers-reduced-motion: reduce)");
    return matches;
  });
}

/**
 * Get the rule view elements for checkRuleViewContent
 *
 * @param {RuleView} view
 * @returns {Element[]}
 */
function _getRuleViewElements(view) {
  const elementsInView = [];
  for (const el of view.element.children) {
    if (el.id == "registered-properties-container") {
      // We don't check @property content for now
      continue;
    }

    // We should either have headers or containers as children
    if (el.classList.contains("ruleview-header")) {
      // Only ignore "This element" hidden header (and not collapsed containers)
      if (el.hidden) {
        continue;
      }
      elementsInView.push(el);
    } else {
      // Gather all the children of containers (e.g. Pseudo-element, @keyframe, …)
      elementsInView.push(...el.children);
    }
  }
  return elementsInView;
}

function getUnusedVariableButton(view, index) {
  return view.styleDocument
    .querySelectorAll(".ruleview-rule")
    [index].querySelector(".ruleview-show-unused-custom-css-properties");
}

/**
 * Assert the number and label of all containers headers displayed in the rule view.
 * (pseudo elements, this element, keyframes, inherited,...)
 *
 * @param {RuleView} view
 * @param {Array<string>} expected
 *        List of labels for all the expected containers.
 * @return {Iterator<DOMElement>}
 *        List of header DOM Elements.
 */
function assertRuleViewHeaders(view, expected) {
  const headers = view.element.querySelectorAll(
    ".ruleview-header:not([hidden])"
  );

  is(
    headers.length,
    expected.length,
    "There are " + expected.length + " rule view headers"
  );

  let i = 0;
  for (const header of headers) {
    is(
      header.textContent,
      expected[i],
      "Correct " + header.textContent + " header"
    );
    i++;
  }

  return headers;
}

/**
 * Expand the pseudo element container.
 * This assumes it was collapsed and allows interacting with pseudo element rules.
 *
 * @param {RuleView} view
 */
function expandPseudoElementContainer(view) {
  info("Expand the pseudo element section");
  const pseudoElementToggle = view.styleDocument.querySelector(
    `[aria-controls="pseudo-elements-container"]`
  );
  // sanity check
  is(
    pseudoElementToggle.ariaExpanded,
    "false",
    "pseudo element section is collapsed at first"
  );
  pseudoElementToggle.click();
  is(
    pseudoElementToggle.ariaExpanded,
    "true",
    "pseudo element section is now expanded"
  );
}

/**
 * Edit a given rule with a new selector.
 *
 * @param {RuleView} view
 * @param {RuleEditor} ruleEditor
 * @param {string} newSelector
 */
async function editSelectorForRuleEditor(view, ruleEditor, newSelector) {
  info(
    `Editing rule selector from "${ruleEditor.selectorText.textContent}" to "${newSelector}"`
  );
  let editor = null;
  const { activeElement } = ruleEditor.selectorText.ownerDocument;
  // The selector input may already be focused if we just created a new rule
  if (inplaceEditor(ruleEditor.selectorText)?.input == activeElement) {
    editor = inplaceEditor(ruleEditor.selectorText);
  } else {
    info("Focusing an existing selector name in the rule-view");
    editor = await focusEditableField(view, ruleEditor.selectorText);
    is(
      inplaceEditor(ruleEditor.selectorText),
      editor,
      "The selector editor got focused"
    );
  }

  info("Entering a new selector name and committing");
  editor.input.value = newSelector;

  info("Entering the commit key");
  const onRuleViewChanged = once(view, "ruleview-changed");
  const onInvalidSelector = once(view, "ruleview-invalid-selector");
  // If we change the selector in a way that cause the selected element
  // to be destroyed, the parent element will be selected
  // and none of the other event will be fired
  const onNewElementSelected = view.inspector.selection.once("new-node-front");
  EventUtils.synthesizeKey("KEY_Enter");

  info("Waiting for rule view to update");
  await Promise.race([
    onRuleViewChanged,
    onInvalidSelector,
    onNewElementSelected,
  ]);
}
