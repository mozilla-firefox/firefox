/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_setup(async () => {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["extensions.formautofill.addresses.supported", "on"],
      ["extensions.formautofill.creditCards.supported", "on"],
      ["extensions.formautofill.heuristics.detectDynamicFormChanges", true],
    ],
  });
});

const verifyCurrentIdentifiedFields = async (browser, expectedFields) => {
  const actor =
    browser.browsingContext.currentWindowGlobal.getActor("FormAutofill");
  let actualFields = Array.from(actor.sectionsByRootId.values()).flat();
  verifySectionFieldDetails(actualFields, expectedFields);
};

const expectedAddressFieldsExcludingAdditionalFields = [
  {
    fields: [
      { fieldName: "name" },
      { fieldName: "email" },
      { fieldName: "tel" },
      { fieldName: "country" },
    ],
  },
];

const expectedAddressFieldsIncludingAdditionalFields = [
  {
    fields: [
      { fieldName: "name" },
      { fieldName: "email" },
      { fieldName: "tel" },
      { fieldName: "country" },
      { fieldName: "street-address" },
      { fieldName: "address-level1" },
      { fieldName: "address-level2" },
      { fieldName: "postal-code" },
    ],
  },
];

const expectedCreditCardFieldsExcludingAdditionalFields = [
  {
    fields: [{ fieldName: "cc-number" }],
  },
];

const expectedCreditCardFieldsIncludingAdditionalFields = [
  {
    fields: [
      { fieldName: "cc-number" },
      { fieldName: "cc-name" },
      { fieldName: "cc-exp-month" },
      { fieldName: "cc-exp-year" },
    ],
  },
];

/**
 * Tests that the identified fields are updated correctly during dynamic form changes
 *
 * The form changes can be of two types (see FormAutofillHeuristics.FORM_CHANGE_REASON):
 *          1. An element changes its visibility state,
 *            e.g. visible element becomes invisible or vice versa
 *          2. The form/document adds or removes nodes
 *
 * Both form changes should make FormAutofillChild consider triggering another identification process.
 * This method tests that we identifiy the correct fields in three different scenarios:
 *          1. Focusing on a field which triggers a first identification process
 *          2. Filling a field that dispatches a change event which makes the site
 *             change the form, which triggers a second field identification process
 *          3. Clearing the value from the field, which reverts the previous form change
 *             and triggers a third field identification process
 *
 * @param {string} documentPath
 * @param {string} elementIdToFill
 * @param {object[]} identifiedFieldsExcludingExtraFields
 * @param {object[]} identifiedFieldsIncludingExtraFields
 */
const verifyIdentifiedFieldsDuringFormChange = async (
  documentPath,
  elementIdToFill,
  identifiedFieldsExcludingExtraFields,
  identifiedFieldsIncludingExtraFields
) => {
  await BrowserTestUtils.withNewTab(documentPath, async browser => {
    const fieldDetectionCompletedBeforeFormChangePromise =
      getFieldDetectionCompletedPromiseResolver();

    info("Focusing on a form field to trigger the identification process");
    await SpecialPowers.spawn(browser, [elementIdToFill], elementId => {
      const field = content.document.getElementById(elementId);
      // Focus event invokes the first field identification process
      field.focus();
    });

    info("Waiting for initial fieldDetectionCompleted notification");
    await fieldDetectionCompletedBeforeFormChangePromise;

    info("Checking that additional fields are not identified yet");
    await verifyCurrentIdentifiedFields(
      browser,
      identifiedFieldsExcludingExtraFields
    );

    const fieldDetectionCompletedIncludingAdditionalFieldsPromise =
      getFieldDetectionCompletedPromiseResolver();

    info("Simulating user input so that additional field nodes are added.");
    await SpecialPowers.spawn(browser, [elementIdToFill], elementId => {
      let field = content.document.getElementById(elementId);
      field.setUserInput("dummyValue");
    });

    // A "form-changed" event was dispatched, triggering another field identification process
    info("Waiting for another fieldDetectionCompleted notification");
    await fieldDetectionCompletedIncludingAdditionalFieldsPromise;

    info("Checking that additional address fields are also identied.");
    await verifyCurrentIdentifiedFields(
      browser,
      identifiedFieldsIncludingExtraFields
    );

    const fieldDetectionCompletedExcludingAdditionalFieldsPromise =
      getFieldDetectionCompletedPromiseResolver();

    info("Clearing user input, so that additional fields are removed again.");
    await SpecialPowers.spawn(browser, [elementIdToFill], elementId => {
      let field = content.document.getElementById(elementId);
      field.focus();
      field.setUserInput("");
    });

    // A "form-changed" event was dispatched, triggering another field identification process
    info("Waiting for another fieldIdentified notification");
    await fieldDetectionCompletedExcludingAdditionalFieldsPromise;

    info("Checking that additional fields are removed from identified fields");
    await verifyCurrentIdentifiedFields(
      browser,
      identifiedFieldsExcludingExtraFields
    );
  });
};

/**
 * Tests that the correct address fields are identified in a form after "form-changed"
 * events are dispatched with reasons "nodes-added" and "nodes-removed"
 */
add_task(
  async function correct_address_fields_identified_in_form_during_form_changes_due_to_node_mutations() {
    await verifyIdentifiedFieldsDuringFormChange(
      FORMS_WITH_DYNAMIC_FORM_CHANGE,
      "country-node-addition",
      expectedAddressFieldsExcludingAdditionalFields,
      expectedAddressFieldsIncludingAdditionalFields
    );
  }
);

/**
 * Tests that the correct credit card fields are identified in a form after "form-changed"
 * events are dispatched with reasons "nodes-added" and "nodes-removed"
 */
add_task(
  async function correct_credit_card_fields_identified_in_form_during_form_changes_due_to_node_mutations() {
    await verifyIdentifiedFieldsDuringFormChange(
      FORMS_WITH_DYNAMIC_FORM_CHANGE,
      "cc-number-node-addition",
      expectedCreditCardFieldsExcludingAdditionalFields,
      expectedCreditCardFieldsIncludingAdditionalFields
    );
  }
);

/**
 * Tests that the correct address fields are identified in a form after "form-changed"
 * events are dispatched with reasons "visible-element-became-invisible" and "invisible-element-became-visible"
 */
add_task(
  async function correct_address_fields_identified_in_form_during_form_changes_due_to_element_visibility_change() {
    await verifyIdentifiedFieldsDuringFormChange(
      FORMS_WITH_DYNAMIC_FORM_CHANGE,
      "country-visibility-change",
      expectedAddressFieldsExcludingAdditionalFields,
      expectedAddressFieldsIncludingAdditionalFields
    );
  }
);

/**
 * Tests that the correct credit card fields are identified in a form after "form-changed"
 * events are dispatched with reasons "visible-element-became-invisible" and "invisible-element-became-visible"
 */
add_task(
  async function correct_credit_card_fields_identified_in_form_during_form_changes_due_to_element_visibility_change() {
    await verifyIdentifiedFieldsDuringFormChange(
      FORMS_WITH_DYNAMIC_FORM_CHANGE,
      "cc-number-visibility-change",
      expectedCreditCardFieldsExcludingAdditionalFields,
      expectedCreditCardFieldsIncludingAdditionalFields
    );
  }
);

/**
 * Tests that the correct fields are identified in a document (form-less) after "form-changed"
 * events are dispatched with reasons "nodes-added" and "nodes-removed"
 */
add_task(
  async function correct_fields_identified_in_formless_document_during_form_changes_due_to_node_mutations() {
    await verifyIdentifiedFieldsDuringFormChange(
      FORMLESS_FIELDS_WITH_DYNAMIC_FORM_CHANGE_AFTER_NODE_MUTATIONS,
      "country-node-addition",
      expectedAddressFieldsExcludingAdditionalFields,
      expectedAddressFieldsIncludingAdditionalFields
    );
  }
);

/**
 * Tests that the correct fields are identified in a document (form-less) after "form-changed"
 * events are dispatched with reasons "visible-element-became-invisible" and "invisible-element-became-visible"
 */
add_task(
  async function correct_fields_identified_in_formless_document_during_form_changes_due_to_element_visibility_change() {
    await verifyIdentifiedFieldsDuringFormChange(
      FORMLESS_FIELDS_WITH_DYNAMIC_FORM_CHANGE_AFTER_VISIBILITY_STATE_CHANGE,
      "country-visibility-change",
      expectedAddressFieldsExcludingAdditionalFields,
      expectedAddressFieldsIncludingAdditionalFields
    );
  }
);

/**
 * Tests that fields added together with other nodes in a single mutation are
 * identified, even when the first added node isn't an element.
 */
add_task(async function fields_identified_when_added_in_a_single_mutation() {
  const html = `
    <form id="form">
      <input id="name" autocomplete="name">
      <input id="email" autocomplete="email">
      <input id="tel" autocomplete="tel">
    </form>
    <script>
      document.getElementById("email").addEventListener("input", () => {
        const fields = ["street-address", "address-level2", "postal-code"].map(
          autocomplete => {
            const input = document.createElement("input");
            input.autocomplete = autocomplete;
            return input;
          }
        );
        document
          .getElementById("form")
          .append(document.createTextNode(" "), ...fields);
      });
    </script>`;

  await BrowserTestUtils.withNewTab(
    "https://example.org/document-builder.sjs?html=" + encodeURIComponent(html),
    async browser => {
      let fieldDetectionCompleted = getFieldDetectionCompletedPromiseResolver();
      await SpecialPowers.spawn(browser, [], () => {
        content.document.getElementById("email").focus();
      });
      await fieldDetectionCompleted;
      await verifyCurrentIdentifiedFields(browser, [
        {
          fields: [
            { fieldName: "name" },
            { fieldName: "email" },
            { fieldName: "tel" },
          ],
        },
      ]);

      fieldDetectionCompleted = getFieldDetectionCompletedPromiseResolver();
      await SpecialPowers.spawn(browser, [], () => {
        content.document.getElementById("email").setUserInput("a@b.c");
      });
      await fieldDetectionCompleted;
      await verifyCurrentIdentifiedFields(browser, [
        {
          fields: [
            { fieldName: "name" },
            { fieldName: "email" },
            { fieldName: "tel" },
            { fieldName: "street-address" },
            { fieldName: "address-level2" },
            { fieldName: "postal-code" },
          ],
        },
      ]);
    }
  );
});

/**
 * Tests that added fields with child nodes, such as a <select> with options or
 * a <textarea> with text, are identified.
 */
add_task(async function fields_with_child_nodes_identified_when_added() {
  const html = `
    <form id="form">
      <input id="name" autocomplete="name">
      <input id="email" autocomplete="email">
      <input id="tel" autocomplete="tel">
    </form>
    <script>
      document.getElementById("email").addEventListener("input", () => {
        const wrapper = document.createElement("div");
        wrapper.innerHTML = \`
          <textarea autocomplete="street-address">1 Main St</textarea>
          <select autocomplete="country">
            <option value="US">United States</option>
            <option value="CA">Canada</option>
          </select>\`;
        document.getElementById("form").append(wrapper);
      });
    </script>`;

  await BrowserTestUtils.withNewTab(
    "https://example.org/document-builder.sjs?html=" + encodeURIComponent(html),
    async browser => {
      let fieldDetectionCompleted = getFieldDetectionCompletedPromiseResolver();
      await SpecialPowers.spawn(browser, [], () => {
        content.document.getElementById("email").focus();
      });
      await fieldDetectionCompleted;

      fieldDetectionCompleted = getFieldDetectionCompletedPromiseResolver();
      await SpecialPowers.spawn(browser, [], () => {
        content.document.getElementById("email").setUserInput("a@b.c");
      });
      await fieldDetectionCompleted;
      await verifyCurrentIdentifiedFields(browser, [
        {
          fields: [
            { fieldName: "name" },
            { fieldName: "email" },
            { fieldName: "tel" },
            { fieldName: "street-address" },
            { fieldName: "country" },
          ],
        },
      ]);
    }
  );
});

/**
 * Tests that new form-less fields are identified when all the previously
 * identified fields were removed.
 */
add_task(async function formless_fields_identified_after_all_fields_replaced() {
  const html = `
    <div id="container">
      <input id="name" autocomplete="name">
      <input id="email" autocomplete="email">
      <input id="tel" autocomplete="tel">
    </div>
    <script>
      document.getElementById("email").addEventListener("input", () => {
        const container = document.getElementById("container");
        container.textContent = "";
        for (const autocomplete of [
          "street-address",
          "address-level2",
          "postal-code",
        ]) {
          const input = document.createElement("input");
          input.autocomplete = autocomplete;
          container.append(input);
        }
      });
    </script>`;

  await BrowserTestUtils.withNewTab(
    "https://example.org/document-builder.sjs?html=" + encodeURIComponent(html),
    async browser => {
      let fieldDetectionCompleted = getFieldDetectionCompletedPromiseResolver();
      await SpecialPowers.spawn(browser, [], () => {
        content.document.getElementById("email").focus();
      });
      await fieldDetectionCompleted;
      await verifyCurrentIdentifiedFields(browser, [
        {
          fields: [
            { fieldName: "name" },
            { fieldName: "email" },
            { fieldName: "tel" },
          ],
        },
      ]);

      fieldDetectionCompleted = getFieldDetectionCompletedPromiseResolver();
      await SpecialPowers.spawn(browser, [], () => {
        content.document.getElementById("email").setUserInput("a@b.c");
      });
      await fieldDetectionCompleted;
      await verifyCurrentIdentifiedFields(browser, [
        {
          fields: [
            { fieldName: "street-address" },
            { fieldName: "address-level2" },
            { fieldName: "postal-code" },
          ],
        },
      ]);
    }
  );
});
