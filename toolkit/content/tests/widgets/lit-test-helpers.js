/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { TestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TestUtils.sys.mjs"
);
const { BrowserTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/BrowserTestUtils.sys.mjs"
);

/**
 * A collection of helper functions for writing tests using Lit. These methods
 * will almost always need to be used in a specific order to first import Lit,
 * then provide template code, then render the template code:
 *
 * let testHelpers = new LitTestHelpers();
 *
 * add_setup(async function setup() {
 *   let { html } = await testHelpers.setupLit();
 *   let templateFn = (attrs, children) => html`
 *     <my-element ${attrs}>${children}</my-element>
 *   `;
 *   testHelpers.setupTests({ templateFn });
 * });
 *
 * add_task(async function testMyElement() {
 *   let renderTarget = await testHelpers.renderTemplate();
 *   let myElement = renderTarget.firstElementChild;
 *   ...
 * });
 */
class LitTestHelpers {
  /**
   * Imports the Lit library and exposes it for use in test helpers.
   *
   * @returns {object} The Lit library for use in consuming tests.
   */
  async setupLit() {
    let lit = await import("chrome://global/content/vendor/lit.all.mjs");
    ({ html: this.html, render: this.render } = lit);
    this.SpreadDirective = class extends lit.Directive {
      render() {
        return lit.nothing;
      }
      update(part, [attrs]) {
        for (let [key, value] of Object.entries(attrs)) {
          part.element.setAttribute(key, value);
        }
        return lit.noChange;
      }
    };
    this.PropertySpreadDirective = class extends lit.Directive {
      render() {
        return lit.nothing;
      }
      update(part, [attrs]) {
        for (let [key, value] of Object.entries(attrs)) {
          part[key] = value;
        }
        return lit.noChange;
      }
    };
    this.spread = lit.directive(this.SpreadDirective);
    this.propertySpread = lit.directive(this.PropertySpreadDirective);
    return lit;
  }

  /**
   * Sets up data used in test helpers and creates the DOM element that test
   * templates get rendered into.
   *
   * @param {object} [templateFn] - Template function to render the element and
   *     any associated markup. When called it will receive two positional
   *     argument `attrs` which should be applied to the element under test, and
   *     `children` which should be added as a child of the element under test.
   *     e.g. `(attrs, children) => <my-element ${attrs}>${children}</my-element>`
   */
  setupTests({ templateFn }) {
    this.templateFn = (args = {}, children) =>
      templateFn(this.spread(args), children);
    this.renderTarget = document.createElement("div");
    document.body.append(this.renderTarget);
  }

  /**
   * Renders a template for use in tests.
   *
   * @param {object} [template] - Optional template to render specific markup.
   * @returns {object} DOM node containing the rendered template elements.
   */
  async renderTemplate(template = this.templateFn()) {
    // Clear out any previously rendered DOM content.
    this.render(this.html``, this.renderTarget);
    this.render(template, this.renderTarget);
    await this.renderTarget.firstElementChild.updateComplete;
    return this.renderTarget;
  }
}

/**
 * Helper and test functions that are useful when writing tests for reusable
 * input elements such as moz-checkbox, moz-select, moz-radio, etc.
 */
class InputTestHelpers extends LitTestHelpers {
  /**
   * Sets up helpers that can be used to verify the events emitted from custom
   * input elements.
   *
   * @returns {object} Event test helper functions.
   */
  getInputEventHelpers() {
    let seenEvents = [];
    let { activatedProperty } = this;

    function trackEvent(event) {
      let reactiveProps = event.target.constructor.properties;
      seenEvents.push({
        type: event.type,
        value: event.target.value,
        localName: event.currentTarget.localName,
        ...(reactiveProps.hasOwnProperty(activatedProperty) && {
          [activatedProperty]: event.target[activatedProperty],
        }),
      });
    }
    function verifyEvents(expectedEvents) {
      is(
        seenEvents.length,
        expectedEvents.length,
        "Input elements emit the expected number of events."
      );
      expectedEvents.forEach((eventInfo, index) => {
        let seenEventInfo = seenEvents[index];
        is(seenEventInfo.type, eventInfo.type, "Event type is correct.");
        is(
          seenEventInfo.value,
          eventInfo.value,
          "Event target value is correct."
        );
        is(
          seenEventInfo.localName,
          eventInfo.localName,
          "Event is emitted from the correct element."
        );
        if (activatedProperty) {
          is(
            seenEventInfo[activatedProperty],
            eventInfo[activatedProperty],
            `Event ${activatedProperty} state is correct.`
          );
        }
      });
      // Reset seen events after verifying.
      seenEvents = [];
    }
    return { seenEvents, trackEvent, verifyEvents };
  }

  /**
   * Runs through a collection of tests of properties that should be common to
   * all reusable moz- input elements.
   *
   * @param {string} elementName - HTML tag of the element under test.
   */
  async testCommonInputProperties(elementName) {
    await this.verifyLabel(elementName);
    await this.verifyName(elementName);
    await this.verifyValue(elementName);
    await this.verifyIcon(elementName);
    await this.verifyDisabled(elementName);
    await this.verifyDescription(elementName);
    await this.verifySupportPage(elementName);
    await this.verifyAccesskey(elementName);
    await this.verifyNoWhitespace(elementName);
    if (this.activatedProperty) {
      await this.verifyActivated(elementName);
      await this.verifyNestedFields(elementName);
    }
  }

  /**
   * Verifies input element label property is settable and dynamic.
   *
   * @param {string} selector - HTML tag of the element under test.
   */
  async verifyLabel(selector) {
    const INITIAL_LABEL = "This is a label.";
    const NEW_LABEL = "Testing...";

    let labelTemplate = this.templateFn({
      value: "label",
      label: INITIAL_LABEL,
    });
    let renderTarget = await this.renderTemplate(labelTemplate);
    let firstInput = renderTarget.querySelector(selector);

    is(
      firstInput.labelEl.innerText.trim(),
      INITIAL_LABEL,
      "Input label text is set."
    );

    firstInput.label = NEW_LABEL;
    await firstInput.updateComplete;
    is(
      firstInput.labelEl.innerText.trim(),
      NEW_LABEL,
      "Input label text is updated."
    );
  }

  /**
   * Verifies input element name property is settable and dynamic.
   *
   * @param {string} selector - HTML tag of the element under test.
   */
  async verifyName(selector) {
    const INITIAL_NAME = "name";
    const NEW_NAME = "new-name";

    let renderTarget = await this.renderTemplate();
    let firstInput = renderTarget.querySelector(selector);

    firstInput.name = INITIAL_NAME;
    await firstInput.updateComplete;
    is(firstInput.inputEl.name, INITIAL_NAME, "Input name is set.");

    firstInput.name = NEW_NAME;
    await firstInput.updateComplete;
    is(firstInput.inputEl.name, NEW_NAME, "Input name is updated.");
  }

  /**
   * Verifies input element value property is settable and dynamic.
   *
   * @param {string} selector - HTML tag of the element under test.
   */
  async verifyValue(selector) {
    const INITIAL_VALUE = "value";
    const NEW_VALUE = "new value";

    let valueTemplate = this.templateFn({
      label: "Testing value",
      value: INITIAL_VALUE,
    });
    let renderTarget = await this.renderTemplate(valueTemplate);
    let firstInput = renderTarget.querySelector(selector);

    is(firstInput.inputEl.value, INITIAL_VALUE, "Input value is set.");
    firstInput.value = NEW_VALUE;
    await firstInput.updateComplete;
    is(firstInput.inputEl.value, NEW_VALUE, "Input value is updated.");
  }

  /**
   * Verifies input element can display and icon.
   *
   * @param {string} selector - HTML tag of the element under test.
   */
  async verifyIcon(selector) {
    const ICON_SRC = "chrome://global/skin/icons/edit-copy.svg";

    let iconTemplate = this.templateFn({
      value: "icon",
      label: "Testing icon",
      iconsrc: ICON_SRC,
    });

    let renderTarget = await this.renderTemplate(iconTemplate);
    let firstInput = renderTarget.querySelector(selector);

    ok(firstInput.icon, "Input displays an icon.");
    is(
      firstInput.icon.getAttribute("src"),
      "chrome://global/skin/icons/edit-copy.svg",
      "Input icon uses the expected source."
    );

    firstInput.iconSrc = null;
    await firstInput.updateComplete;
    ok(!firstInput.icon, "Input icon can be removed.");
  }

  /**
   * Verifies it is possible to disable and re-enable input element.
   *
   * @param {string} selector - HTML tag of the element under test.
   */
  async verifyDisabled(selector) {
    let renderTarget = await this.renderTemplate();
    let firstInput = renderTarget.querySelector(selector);

    ok(!firstInput.disabled, "Input is enabled on initial render.");

    firstInput.disabled = true;
    await firstInput.updateComplete;

    ok(firstInput.disabled, "Input is disabled.");
    ok(firstInput.inputEl.disabled, "Disabled state is propagated.");

    firstInput.disabled = false;
    await firstInput.updateComplete;
    ok(!firstInput.disabled, "Input can be re-enabled.");
    ok(!firstInput.inputEl.disabled, "Disabled state is propagated.");
  }

  /**
   * Verifies different methods for providing a description to the input element.
   *
   * @param {string} selector - HTML tag of the element under test.
   */
  async verifyDescription(selector) {
    const ATTR_DESCRIPTION = "This description is set via an attribute.";
    const SLOTTED_DESCRIPTION = "This description is set via a slot.";

    let templatesArgs = [
      [{ description: ATTR_DESCRIPTION }],
      [{}, this.html`<span slot="description">${SLOTTED_DESCRIPTION}</span>`],
      [
        { description: ATTR_DESCRIPTION },
        this.html`<span slot="description">${SLOTTED_DESCRIPTION}</span>`,
      ],
    ];

    let renderTarget = await this.renderTemplate(
      templatesArgs.map(args => this.templateFn(...args))
    );
    let [firstInput, secondInput, thirdInput] =
      renderTarget.querySelectorAll(selector);

    is(
      firstInput.inputEl.getAttribute("aria-describedby"),
      "description",
      "Input is described by the description element."
    );
    is(
      firstInput.descriptionEl.innerText,
      ATTR_DESCRIPTION,
      "Description element has the expected text when set via an attribute."
    );

    let slottedText = secondInput.descriptionEl
      .querySelector("slot[name=description]")
      .assignedElements()[0].innerText;
    is(
      secondInput.inputEl.getAttribute("aria-describedby"),
      "description",
      "Input is described by the description element."
    );
    is(
      slottedText,
      SLOTTED_DESCRIPTION,
      "Description element has the expected text when set via a slot."
    );

    is(
      thirdInput.inputEl.getAttribute("aria-describedby"),
      "description",
      "Input is described by the description element."
    );
    is(
      thirdInput.descriptionEl.innerText,
      ATTR_DESCRIPTION,
      "Attribute text takes precedence over slotted text."
    );
  }

  /**
   * Verifies different methods for providing a support link for the input element.
   *
   * @param {string} selector - HTML tag of the element under test.
   */
  async verifySupportPage(selector) {
    const LEARN_MORE_TEXT = "Learn more";
    const CUSTOM_TEXT = "Help me!";

    let templatesArgs = [
      [{ "support-page": "test-page", label: "A label" }],
      [
        { label: "A label" },
        this.html`<a slot="support-link" href="www.example.com">Help me!</a>`,
      ],
    ];

    let renderTarget = await this.renderTemplate(
      templatesArgs.map(args => this.templateFn(...args))
    );
    let [firstInput, secondInput] = renderTarget.querySelectorAll(selector);

    let getSupportLink = () =>
      firstInput.shadowRoot.querySelector("a[is=moz-support-link]");
    let supportLink = getSupportLink();

    await BrowserTestUtils.waitForMutationCondition(
      supportLink,
      { childList: true },
      () => supportLink.textContent.trim()
    );

    ok(
      supportLink,
      "Support link is rendered when supportPage attribute is set."
    );
    ok(
      supportLink.href.includes("test-page"),
      "Support link href points to the expected SUMO page."
    );
    is(
      supportLink.innerText,
      LEARN_MORE_TEXT,
      "Support link uses the default label text."
    );
    is(
      supportLink.previousElementSibling.localName,
      "label",
      "Support link is rendered next to the label by default."
    );

    firstInput.description = "some description text";
    await firstInput.updateComplete;

    is(
      getSupportLink().parentElement.id,
      "description",
      "Support link is rendered in the description if a description is present."
    );

    let getSlottedSupportLink = () =>
      secondInput.shadowRoot
        .querySelector("slot[name=support-link]")
        .assignedElements()[0];
    let slottedSupportLink = getSlottedSupportLink();

    ok(
      slottedSupportLink,
      "Links can also be rendered using the support-link slot."
    );
    ok(
      slottedSupportLink.href.includes("www.example.com"),
      "Slotted link uses the expected url."
    );
    is(
      slottedSupportLink.innerText,
      CUSTOM_TEXT,
      "Slotted link uses non-default label text."
    );
    is(
      slottedSupportLink.assignedSlot.previousElementSibling.localName,
      "label",
      "Slotted support link is rendered next to the label by default."
    );

    let slottedDescriptionPresent = BrowserTestUtils.waitForMutationCondition(
      secondInput,
      { childList: true, subtree: true },
      () =>
        secondInput.descriptionEl
          .querySelector("slot[name='description']")
          .assignedElements().length
    );

    let description = document.createElement("span");
    description.textContent = "I'm a slotted description.";
    description.slot = "description";
    secondInput.append(description);
    await slottedDescriptionPresent;

    is(
      getSlottedSupportLink().assignedSlot.parentElement.id,
      "description",
      "Support link is rendered in the slotted description if a slotted description is present."
    );
  }

  /**
   * Verifies the accesskey behavior of the input element.
   *
   * @param {string} selector - HTML tag of the element under test.
   */
  async verifyAccesskey(selector) {
    const UNIQUE_ACCESS_KEY = "t";
    const SHARED_ACCESS_KEY = "d";
    let { activatedProperty } = this;

    let attrs = [
      { value: "first", label: "First", accesskey: UNIQUE_ACCESS_KEY },
      { value: "second", label: "Second", accesskey: SHARED_ACCESS_KEY },
      { value: "third", label: "Third", accesskey: SHARED_ACCESS_KEY },
    ];
    let accesskeyTemplate = this.html`${attrs.map(a => this.templateFn(a))}`;

    let renderTarget = await this.renderTemplate(accesskeyTemplate);
    let [firstInput, secondInput, thirdInput] =
      renderTarget.querySelectorAll(selector);

    // Validate that activating a unique accesskey focuses and checks the input.
    firstInput.blur();
    isnot(document.activeElement, firstInput, "First input is not focused.");
    isnot(
      firstInput.shadowRoot.activeElement,
      firstInput.inputEl,
      "Input element is not focused."
    );
    if (activatedProperty) {
      ok(!firstInput[activatedProperty], `Input is not ${activatedProperty}.`);
    }

    synthesizeKey(
      UNIQUE_ACCESS_KEY,
      navigator.platform.includes("Mac")
        ? { altKey: true, ctrlKey: true }
        : { altKey: true, shiftKey: true }
    );

    is(
      document.activeElement,
      firstInput,
      "Input receives focus after accesskey is pressed."
    );
    is(
      firstInput.shadowRoot.activeElement,
      firstInput.inputEl,
      "Input element is focused after accesskey is pressed."
    );
    if (activatedProperty) {
      ok(
        firstInput[activatedProperty],
        `Input is ${activatedProperty} after accesskey is pressed.`
      );
    }

    // Validate that activating a shared accesskey toggles focus between inputs.
    synthesizeKey(
      SHARED_ACCESS_KEY,
      navigator.platform.includes("Mac")
        ? { altKey: true, ctrlKey: true }
        : { altKey: true, shiftKey: true }
    );

    is(
      document.activeElement,
      secondInput,
      "Focus moves to the input with the shared accesskey."
    );
    if (activatedProperty) {
      ok(
        !secondInput[activatedProperty],
        `Second input is not ${activatedProperty}.`
      );
    }

    synthesizeKey(
      SHARED_ACCESS_KEY,
      navigator.platform.includes("Mac")
        ? { altKey: true, ctrlKey: true }
        : { altKey: true, shiftKey: true }
    );

    is(
      document.activeElement,
      thirdInput,
      "Focus cycles between inputs with the same accesskey."
    );
    if (activatedProperty) {
      ok(
        !thirdInput[activatedProperty],
        `Third input is not ${activatedProperty}.`
      );
    }
  }

  /**
   * Verifies the activated state of the input element.
   *
   * @param {string} selector - HTML tag of the element under test.
   */
  async verifyActivated(selector) {
    let renderTarget = await this.renderTemplate();
    let firstInput = renderTarget.querySelector(selector);
    let { activatedProperty } = this;

    ok(
      !firstInput.inputEl[activatedProperty] && !firstInput[activatedProperty],
      `Input is not ${activatedProperty} on initial render.`
    );

    firstInput[activatedProperty] = true;
    await firstInput.updateComplete;

    ok(firstInput[activatedProperty], `Input is ${activatedProperty}.`);
    ok(
      firstInput.inputEl[activatedProperty] ||
        firstInput.inputEl.getAttribute(`aria-${activatedProperty}`) == "true",
      `${activatedProperty} state is propagated.`
    );

    // Reset state so that the radio input doesn't
    // give a false negative
    firstInput[activatedProperty] = false;
    await firstInput.updateComplete;

    synthesizeMouseAtCenter(firstInput.inputEl, {});
    await firstInput.updateComplete;

    ok(
      firstInput[activatedProperty],
      `Input is ${activatedProperty} via mouse.`
    );
    ok(
      firstInput.inputEl[activatedProperty] ||
        firstInput.inputEl.getAttribute(`aria-${activatedProperty}`) == "true",
      `${activatedProperty} state is propagated.`
    );
  }

  /**
   * Verifies that whitespace isn't getting added via different parts of the
   * template as it will be visible in the rendered markup.
   *
   * @param {string} selector - HTML tag of the element under test.
   */
  async verifyNoWhitespace(selector) {
    let whitespaceTemplate = this.templateFn({
      label: "label",
      "support-page": "test",
      "icon-src": "chrome://global/skin/icons/edit-copy.svg",
    });
    let renderTarget = await this.renderTemplate(whitespaceTemplate);
    let firstInput = renderTarget.querySelector(selector);

    if (firstInput.constructor.inputLayout == "block") {
      return;
    }

    function isWhitespaceTextNode(node) {
      return node.nodeType == Node.TEXT_NODE && !/[^\s]/.exec(node.nodeValue);
    }

    ok(
      !isWhitespaceTextNode(firstInput.inputEl.previousSibling),
      "Input element is not preceded by whitespace."
    );
    ok(
      !isWhitespaceTextNode(firstInput.inputEl.nextSibling),
      "Input element is not followed by whitespace."
    );

    let labelContent = firstInput.labelEl.querySelector(".text");
    ok(
      !isWhitespaceTextNode(labelContent.previousSibling),
      "Label text is not preceded by whitespace."
    );

    // Usually labelContent won't be followed by anything, but adding this check
    // ensures the whitespace doesn't accidentally get re-added
    if (labelContent.nextSibling) {
      ok(
        !isWhitespaceTextNode(labelContent.nextSibling),
        "Label content is not followed by whitespace."
      );
    }

    let containsWhitespace = false;
    for (let node of labelContent.childNodes) {
      if (isWhitespaceTextNode(node)) {
        containsWhitespace = true;
        break;
      }
    }
    ok(
      !containsWhitespace,
      "Label content doesn't contain any extra whitespace."
    );
  }

  async testTextBasedInputEvents(selector) {
    let { trackEvent, verifyEvents } = this.getInputEventHelpers();
    let target = await this.renderTemplate();
    let input = target.querySelector(selector);

    input.addEventListener("click", trackEvent);
    input.addEventListener("change", trackEvent);
    input.addEventListener("input", trackEvent);

    const TEST_STRING = "mozilla!";
    synthesizeMouseAtCenter(input.inputEl, {});
    sendString(TEST_STRING);
    input.blur();
    await TestUtils.waitForTick();

    // Verify that we emit input events for each char of the string,
    // and that change events are fired when the input loses focus.
    verifyEvents([
      { type: "click", localName: selector, value: "" },
      ...Array.from(TEST_STRING).map((char, i) => ({
        type: "input",
        localName: selector,
        value: TEST_STRING.substring(0, i + 1),
      })),
      { type: "change", localName: selector, value: TEST_STRING },
    ]);
  }

  async verifyAriaLabel(selector) {
    const ARIA_LABEL = "I'm not visible";
    let ariaLabelTemplate = this.templateFn({
      value: "default",
      "aria-label": ARIA_LABEL,
    });
    let renderTarget = await this.renderTemplate(ariaLabelTemplate);
    let input = renderTarget.querySelector(selector);

    let labelText = input.shadowRoot.querySelector(".text");
    ok(!labelText, "No visible label text is rendered.");
    ok(
      !input.getAttribute("aria-label"),
      "aria-label is not set on the outer element."
    );
    is(
      input.inputEl.getAttribute("aria-label"),
      ARIA_LABEL,
      "The aria-label is set on the input element."
    );
  }

  /**
   * Verifies the behavior of nested elements for inputs that support nesting.
   *
   * @param {string} selector - HTML tag of the element under test.
   */
  async verifyNestedFields(selector) {
    let { activatedProperty } = this;
    let nestedTemplate = this.templateFn({
      label: "label",
      value: "foo",
      [activatedProperty]: true,
    });
    let renderTarget = await this.renderTemplate(nestedTemplate);
    let parentInput = renderTarget.querySelector(selector);
    let nestedEls = [];
    let disabledEls = [];

    async function waitForUpdateComplete() {
      await parentInput.updateComplete;
      await Promise.all(nestedEls.map(el => el.updateComplete));
    }

    // Create elements we expect to use in the nested slot.
    let nestedCheckbox = document.createElement("moz-checkbox");
    nestedCheckbox.slot = "nested";
    nestedCheckbox.label = "nested checkbox";
    nestedCheckbox.checked = true;
    nestedCheckbox.value = "one";
    nestedEls.push(nestedCheckbox);

    let nestedBoxButton = document.createElement("moz-box-button");
    nestedBoxButton.slot = "nested";
    nestedBoxButton.label = "nested box button";
    nestedEls.push(nestedBoxButton);

    let nestedSelect = document.createElement("moz-select");
    nestedSelect.slot = "nested";
    nestedSelect.label = "nested select";
    nestedSelect.value = "two";
    nestedEls.push(nestedSelect);

    let options = ["one", "two", "three"].map(val => {
      let option = document.createElement("moz-option");
      option.value = val;
      option.label = val;
      return option;
    });
    nestedSelect.append(...options);

    parentInput.append(...nestedEls);

    let nestedDisabledCheckbox = document.createElement("moz-checkbox");
    nestedDisabledCheckbox.slot = "nested";
    nestedDisabledCheckbox.label = "nested disabled checkbox";
    nestedDisabledCheckbox.disabled = true;
    parentInput.append(nestedDisabledCheckbox);
    disabledEls.push(nestedDisabledCheckbox);

    let nestedDisabledRadioGroup = document.createElement("moz-radio-group");
    nestedDisabledRadioGroup.slot = "nested";
    nestedDisabledRadioGroup.label = "nested disabled radio group";
    nestedDisabledRadioGroup.disabled = true;
    parentInput.append(nestedDisabledRadioGroup);

    let nestedDisabledRadioOptions = ["one", "two", "three"].map(val => {
      let nestedDisabledRadioOption = document.createElement("moz-radio");
      nestedDisabledRadioOption.label = `nested disabled radio ${val}`;
      nestedDisabledRadioOption.value = val;
      nestedDisabledRadioOption.disabled = val == "two";
      nestedDisabledRadioGroup.append(nestedDisabledRadioOption);
      return nestedDisabledRadioOption;
    });
    disabledEls.push(...nestedDisabledRadioOptions);

    await waitForUpdateComplete();

    // Test the initial state when parent input is enabled and activated.
    nestedEls.forEach(nestedEl => {
      is(
        (nestedEl.inputEl ?? nestedEl.buttonEl).disabled,
        false,
        `The nested ${nestedEl.localName} element is enabled when the parent input is activated.`
      );
    });
    disabledEls.forEach(disabledEl => {
      is(
        disabledEl.inputEl.disabled,
        true,
        `The nested disabled ${disabledEl.localName} element is disabled when the parent input is activated.`
      );
    });

    // Deactivate the parent input.
    parentInput[activatedProperty] = false;
    await waitForUpdateComplete();

    nestedEls.forEach(nestedEl => {
      is(
        (nestedEl.inputEl ?? nestedEl.buttonEl).disabled,
        true,
        `The nested ${nestedEl.localName} element is disabled when the parent input is deactivated.`
      );
    });
    disabledEls.forEach(disabledEl => {
      is(
        disabledEl.inputEl.disabled,
        true,
        `The nested disabled ${disabledEl.localName} element is disabled when the parent input is deactivated.`
      );
    });

    // Create another nested element to slot in later.
    let anotherNestedCheckbox = nestedCheckbox.cloneNode(true);
    anotherNestedCheckbox.label = "I get slotted later";
    nestedEls.push(anotherNestedCheckbox);

    // Verify that additional slotted els get the correct disabled state.
    let checkboxSlotted = BrowserTestUtils.waitForEvent(
      parentInput.shadowRoot.querySelector(".nested"),
      "slotchange"
    );
    parentInput.append(anotherNestedCheckbox);
    await checkboxSlotted;
    is(
      anotherNestedCheckbox.inputEl.disabled,
      true,
      "Newly slotted checkbox is initially disabled."
    );

    // Reactivate the parent input.
    parentInput[activatedProperty] = true;
    await waitForUpdateComplete();

    nestedEls.forEach(nestedEl => {
      is(
        (nestedEl.inputEl ?? nestedEl.buttonEl).disabled,
        false,
        `The nested ${nestedEl.localName} element is enabled when the parent input is reactivated.`
      );
    });
    disabledEls.forEach(disabledEl => {
      is(
        disabledEl.inputEl.disabled,
        true,
        `The nested disabled ${disabledEl.localName} element is disabled when the parent input is reactivated.`
      );
    });

    // Verify multiple layers of nested fields work as expected.
    let deeplyNestedCheckbox = nestedCheckbox.cloneNode(true);
    deeplyNestedCheckbox.label = "I'm deeply nested";
    nestedEls.push(deeplyNestedCheckbox);

    let firstNestedCheckbox = nestedEls[0];
    checkboxSlotted = BrowserTestUtils.waitForEvent(
      firstNestedCheckbox.shadowRoot.querySelector(".nested"),
      "slotchange"
    );
    firstNestedCheckbox.append(deeplyNestedCheckbox);
    await checkboxSlotted;
    is(
      deeplyNestedCheckbox.inputEl.disabled,
      false,
      "Deeply nested checkbox is initially enabled."
    );

    // Disabled the parent input.
    parentInput.disabled = true;
    await waitForUpdateComplete();

    nestedEls.forEach(nestedEl => {
      is(
        (nestedEl.inputEl ?? nestedEl.buttonEl).disabled,
        true,
        `The nested ${nestedEl.localName} element is disabled when parent input is disabled.`
      );
    });
    disabledEls.forEach(disabledEl => {
      is(
        disabledEl.inputEl.disabled,
        true,
        `The nested disabled ${disabledEl.localName} element is disabled when the parent input is disabled.`
      );
    });

    // Reenable the parent input.
    parentInput.disabled = false;
    await waitForUpdateComplete();

    nestedEls.forEach(nestedEl => {
      is(
        (nestedEl.inputEl ?? nestedEl.buttonEl).disabled,
        false,
        `The nested ${nestedEl.localName} element is enabled when parent input is reenabled.`
      );
    });
    disabledEls.forEach(disabledEl => {
      is(
        disabledEl.inputEl.disabled,
        true,
        `The nested disabled ${disabledEl.localName} element is disabled when the parent input is reenabled.`
      );
    });
  }

  /**
   * Verifies it is possible to set readonly attribute to the input element and remove it.
   *
   * @param {string} selector - HTML tag of the element under test.
   */
  async verifyReadonly(selector) {
    const INITIAL_VALUE = "value";
    const NEW_VALUE = "new value";
    let renderTarget = await this.renderTemplate();
    let firstInput = renderTarget.querySelector(selector);

    async function enterInputValue(inputComponent, inputValue) {
      synthesizeMouseAtCenter(inputComponent.inputEl, {});
      sendString(inputValue);
      inputComponent.blur();
      await TestUtils.waitForTick();
    }

    ok(
      !firstInput.readonly,
      "Input has a readonly property set to false on initial render."
    );
    is(
      firstInput.inputEl.value,
      "",
      "The initial value of the input element is an empty string."
    );

    await enterInputValue(firstInput, INITIAL_VALUE);
    is(
      firstInput.inputEl.value,
      INITIAL_VALUE,
      "The value of the input element has changed."
    );

    firstInput.readonly = true;
    await firstInput.updateComplete;

    ok(firstInput.readonly, "Input is readonly.");
    ok(firstInput.inputEl.readOnly, "Readonly state is propagated.");

    await enterInputValue(firstInput, NEW_VALUE);
    is(
      firstInput.inputEl.value,
      INITIAL_VALUE,
      "The value of the input element hasn't changed."
    );

    firstInput.readonly = false;
    await firstInput.updateComplete;

    ok(
      !firstInput.readonly,
      "Input has a readonly property set to false again."
    );
    ok(!firstInput.inputEl.readOnly, "Readonly state is propagated.");
  }
}
