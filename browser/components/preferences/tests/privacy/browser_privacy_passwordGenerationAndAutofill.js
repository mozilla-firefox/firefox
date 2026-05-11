/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

add_task(async function initialState() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.settings-redesign.enabled", false]],
  });
  // check pref permutations to verify the UI opens in the correct state
  const prefTests = [
    {
      initialPrefs: [
        ["signon.rememberSignons", true],
        ["signon.generation.available", true],
        ["signon.generation.enabled", true],
        ["signon.autofillForms", true],
      ],
      expected: "checked",
    },
    {
      initialPrefs: [
        ["signon.rememberSignons", true],
        ["signon.generation.available", true],
        ["signon.generation.enabled", false],
        ["signon.autofillForms", false],
      ],
      expected: "unchecked",
    },
    {
      initialPrefs: [
        ["signon.rememberSignons", true],
        ["signon.generation.available", false],
        ["signon.generation.enabled", false],
      ],
      expected: "hidden",
    },
    {
      initialPrefs: [
        ["signon.rememberSignons", false],
        ["signon.generation.available", true],
        ["signon.generation.enabled", true],
        ["signon.autofillForms", true],
      ],
      expected: "disabled",
    },
  ];
  for (let test of prefTests) {
    // set initial pref values
    info("initialState, testing with: " + JSON.stringify(test));
    await SpecialPowers.pushPrefEnv({ set: test.initialPrefs });

    // open about:privacy in a tab
    // verify expected conditions
    await BrowserTestUtils.withNewTab(
      {
        gBrowser,
        url: "about:preferences#privacy",
      },
      async function (browser) {
        let doc = browser.contentDocument;
        let generatePasswordsCheckbox = doc.getElementById("generatePasswords");
        let autofillFormsCheckbox = doc.getElementById(
          "passwordAutofillCheckbox"
        );
        doc.getElementById("passwordSettings").scrollIntoView();

        info("initialState, assert on expected state:" + test.expected);
        switch (test.expected) {
          case "hidden":
            is_element_hidden(
              generatePasswordsCheckbox,
              "#generatePasswords checkbox is hidden"
            );
            break;
          case "checked":
            is_element_visible(
              generatePasswordsCheckbox,
              "#generatePasswords checkbox is visible"
            );
            ok(
              generatePasswordsCheckbox.checked,
              "#generatePasswords checkbox is checked"
            );
            ok(
              autofillFormsCheckbox.checked,
              "#passwordAutofillCheckbox is checked"
            );
            break;
          case "unchecked":
            ok(
              !generatePasswordsCheckbox.checked,
              "#generatePasswords checkbox is un-checked"
            );
            ok(
              !autofillFormsCheckbox.checked,
              "#passwordAutofillCheckbox is un-checked"
            );
            break;
          case "disabled":
            ok(
              generatePasswordsCheckbox.disabled,
              "#generatePasswords checkbox is disabled"
            );
            ok(
              autofillFormsCheckbox.disabled,
              "#passwordAutofillCheckbox is disabled"
            );
            break;
          default:
            ok(false, "Unknown expected state: " + test.expected);
        }
      }
    );
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function toggleGenerationEnabled() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.settings-redesign.enabled", false]],
  });
  // clicking the checkbox should toggle the pref
  SpecialPowers.pushPrefEnv({
    set: [
      ["signon.generation.available", true],
      ["signon.generation.enabled", false],
      ["signon.rememberSignons", true],
    ],
  });

  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: "about:preferences#privacy",
    },
    async function (browser) {
      let doc = browser.contentDocument;
      let checkbox = doc.getElementById("generatePasswords");

      info("waiting for the browser to have focus");
      await SimpleTest.promiseFocus(browser);
      let prefChanged = TestUtils.waitForPrefChange(
        "signon.generation.enabled"
      );

      // the preferences "Search" bar obscures the checkbox if we scrollIntoView and try to click on it
      // so use keyboard events instead
      checkbox.focus();
      is(doc.activeElement, checkbox, "checkbox is focused");
      EventUtils.synthesizeKey(" ");

      info("waiting for pref to change");
      await prefChanged;
      ok(checkbox.checked, "#generatePasswords checkbox is checked");
      ok(
        Services.prefs.getBoolPref("signon.generation.enabled"),
        "enabled pref is now true"
      );
    }
  );
  await SpecialPowers.popPrefEnv();
});

add_task(async function toggleRememberSignon() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.settings-redesign.enabled", false]],
  });
  // toggling rememberSignons checkbox should make generation checkbox disabled
  SpecialPowers.pushPrefEnv({
    set: [
      ["signon.generation.available", true],
      ["signon.generation.enabled", true],
      ["signon.rememberSignons", true],
    ],
  });

  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: "about:preferences#privacy",
    },
    async function (browser) {
      let doc = browser.contentDocument;
      let checkbox = doc.getElementById("savePasswords");
      let generationCheckbox = doc.getElementById("generatePasswords");

      ok(
        !generationCheckbox.disabled,
        "generation checkbox is not initially disabled"
      );

      info("waiting for the browser to have focus");
      await SimpleTest.promiseFocus(browser);
      let prefChanged = TestUtils.waitForPrefChange("signon.rememberSignons");

      // the preferences "Search" bar obscures the checkbox if we scrollIntoView and try to click on it
      // so use keyboard events instead
      checkbox.focus();
      is(doc.activeElement, checkbox, "checkbox is focused");
      EventUtils.synthesizeKey(" ");

      info("waiting for pref to change");
      await prefChanged;
      ok(!checkbox.checked, "#savePasswords checkbox is un-checked");
      ok(generationCheckbox.disabled, "generation checkbox becomes disabled");
    }
  );
  await SpecialPowers.popPrefEnv();
});

// Settings redesign tests

add_task(async function initialStateNew() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.settings-redesign.enabled", true]],
  });
  // check pref permutations to verify the UI opens in the correct state
  const prefTests = [
    {
      initialPrefs: [
        ["signon.rememberSignons", true],
        ["signon.generation.available", true],
        ["signon.generation.enabled", true],
        ["signon.autofillForms", true],
      ],
      expected: "checked",
    },
    {
      initialPrefs: [
        ["signon.rememberSignons", true],
        ["signon.generation.available", true],
        ["signon.generation.enabled", false],
        ["signon.autofillForms", false],
      ],
      expected: "unchecked",
    },
    {
      initialPrefs: [
        ["signon.rememberSignons", true],
        ["signon.generation.available", false],
        ["signon.generation.enabled", false],
      ],
      expected: "hidden",
    },
    {
      initialPrefs: [
        ["signon.rememberSignons", false],
        ["signon.generation.available", true],
        ["signon.generation.enabled", true],
        ["signon.autofillForms", true],
      ],
      expected: "disabled",
    },
  ];
  for (let test of prefTests) {
    // set initial pref values
    info("initialState, testing with: " + JSON.stringify(test));
    await SpecialPowers.pushPrefEnv({ set: test.initialPrefs });

    // open about:privacy in a tab
    // verify expected conditions
    await BrowserTestUtils.withNewTab(
      {
        gBrowser,
        url: "about:preferences#passwordsAutofill",
      },
      async function (browser) {
        let doc = browser.contentDocument;
        let generatePasswordsCheckbox = doc
          .getElementById("suggestStrongPasswords")
          .shadowRoot.querySelector("input");
        let autofillFormsCheckbox = doc
          .getElementById("fillUsernameAndPasswords")
          .shadowRoot.querySelector("input");
        doc.getElementById("passwordsGroup").scrollIntoView();

        info("initialState, assert on expected state:" + test.expected);
        switch (test.expected) {
          case "hidden":
            is_element_hidden(
              generatePasswordsCheckbox,
              "#generatePasswords checkbox is hidden"
            );
            break;
          case "checked":
            is_element_visible(
              generatePasswordsCheckbox,
              "#generatePasswords checkbox is visible"
            );
            ok(
              generatePasswordsCheckbox.checked,
              "#generatePasswords checkbox is checked"
            );
            ok(
              autofillFormsCheckbox.checked,
              "#passwordAutofillCheckbox is checked"
            );
            break;
          case "unchecked":
            ok(
              !generatePasswordsCheckbox.checked,
              "#generatePasswords checkbox is un-checked"
            );
            ok(
              !autofillFormsCheckbox.checked,
              "#passwordAutofillCheckbox is un-checked"
            );
            break;
          case "disabled":
            ok(
              generatePasswordsCheckbox.disabled,
              "#generatePasswords checkbox is disabled"
            );
            ok(
              autofillFormsCheckbox.disabled,
              "#passwordAutofillCheckbox is disabled"
            );
            break;
          default:
            ok(false, "Unknown expected state: " + test.expected);
        }
      }
    );
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function toggleGenerationEnabledNew() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.settings-redesign.enabled", true]],
  });
  // clicking the checkbox should toggle the pref
  SpecialPowers.pushPrefEnv({
    set: [
      ["signon.generation.available", true],
      ["signon.generation.enabled", false],
      ["signon.rememberSignons", true],
    ],
  });

  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: "about:preferences#passwordsAutofill",
    },
    async function (browser) {
      let doc = browser.contentDocument;
      let checkbox = doc.getElementById("suggestStrongPasswords");

      info("waiting for the browser to have focus");
      await SimpleTest.promiseFocus(browser);
      let prefChanged = TestUtils.waitForPrefChange(
        "signon.generation.enabled"
      );

      // the preferences "Search" bar obscures the checkbox if we scrollIntoView and try to click on it
      // so use keyboard events instead
      checkbox.focus();
      is(doc.activeElement, checkbox, "checkbox is focused");
      EventUtils.synthesizeKey(" ");

      info("waiting for pref to change");
      await prefChanged;
      ok(checkbox.checked, "#generatePasswords checkbox is checked");
      ok(
        Services.prefs.getBoolPref("signon.generation.enabled"),
        "enabled pref is now true"
      );
    }
  );
  await SpecialPowers.popPrefEnv();
});

add_task(async function toggleRememberSignonNew() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.settings-redesign.enabled", true]],
  });
  // toggling rememberSignons checkbox should make generation checkbox disabled
  SpecialPowers.pushPrefEnv({
    set: [
      ["signon.generation.available", true],
      ["signon.generation.enabled", true],
      ["signon.rememberSignons", true],
    ],
  });

  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: "about:preferences#passwordsAutofill",
    },
    async function (browser) {
      let doc = browser.contentDocument;
      let checkbox = doc.getElementById("savePasswords");
      let generationCheckbox = doc
        .getElementById("suggestStrongPasswords")
        .shadowRoot.querySelector("input");

      ok(
        !generationCheckbox.disabled,
        "generation checkbox is not initially disabled"
      );

      info("waiting for the browser to have focus");
      await SimpleTest.promiseFocus(browser);
      let prefChanged = TestUtils.waitForPrefChange("signon.rememberSignons");

      checkbox.click();

      info("waiting for pref to change");
      await prefChanged;
      ok(!checkbox.checked, "#savePasswords checkbox is un-checked");
      await BrowserTestUtils.waitForCondition(
        () => generationCheckbox.disabled,
        "generation checkbox becomes disabled",
        200
      );
    }
  );
  await SpecialPowers.popPrefEnv();
});
