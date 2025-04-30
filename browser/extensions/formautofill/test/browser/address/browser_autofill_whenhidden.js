/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const FORM_MARKUP = `
  <html><body><form>
    First Name: <input id="fname">
    Last Name: <input id="lname">
    Address: <input id="address">
    <div id="extraArea">
    <p>City: <input id="city"></p>
    <p>Phone: <input id="phone"></p>
      <select id="country" autocomplete="country">
        <option value="">Select a country</option>
        <option value="Germany">Germany</option>
        <option value="Canada">Canada</option>
        <option value="United States">United States</option>
        <option value="France">France</option>
      </select>
    </div>
  </form></body></html>
`;

async function compareFillStates(browser, expectedStates, desc) {
  let actualStates = await SpecialPowers.spawn(browser, [], async function () {
    return ["fname", "lname", "address", "city", "phone", "country"].map(
      name => {
        let field = content.document.getElementById(name);
        let item = { name, autofillState: field.autofillState };
        if (field.value) {
          item.value = field.value;
        } else {
          item.previewValue = field.previewValue;
        }
        return item;
      }
    );
  });

  Assert.deepEqual(actualStates, expectedStates, desc);
}

function hideShowBlock(browser, hide) {
  return SpecialPowers.spawn(browser, [hide ? "none" : ""], display => {
    let extraArea = content.document.getElementById("extraArea");
    extraArea.style.display = display;
    extraArea.clientWidth; // flush layout
  });
}

async function clearAutofill(browser, desc) {
  await openPopupOn(browser, "#fname");
  let items = getDisplayedPopupItems(browser);
  is(
    items[0].getAttribute("ac-label"),
    "Clear Autofill Form",
    "clear form " + desc
  );
  await BrowserTestUtils.synthesizeKey("VK_DOWN", {}, browser);
  await BrowserTestUtils.synthesizeKey("VK_RETURN", {}, browser);
  await closePopup(browser);

  await compareFillStates(
    browser,
    [
      { name: "fname", previewValue: "", autofillState: "" },
      { name: "lname", previewValue: "", autofillState: "" },
      { name: "address", previewValue: "", autofillState: "" },
      { name: "city", previewValue: "", autofillState: "" },
      { name: "phone", previewValue: "", autofillState: "" },
      { name: "country", previewValue: "", autofillState: "" },
    ],
    "after clear"
  );
}

add_setup(async function init() {
  await setStorage(TEST_ADDRESS_1);

  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "data:text/html," + FORM_MARKUP
  );

  // Don't make the test cases depend on waiting out the refill timeouts
  await SpecialPowers.pushPrefEnv({
    set: [
      ["extensions.formautofill.heuristics.refillOnSiteClearingFields", false],
    ],
  });

  registerCleanupFunction(async () => {
    gBrowser.removeTab(tab);
    await removeAllRecords();
  });
});

// Select an item from the autocomplete menu and verify its autofill state.
add_task(async function check_autofill_verify_state() {
  let browser = gBrowser.selectedBrowser;

  await openPopupOn(browser, "#fname");
  await BrowserTestUtils.synthesizeKey("VK_DOWN", {}, browser);
  await SpecialPowers.spawn(browser, [], () => {
    return ContentTaskUtils.waitForCondition(() => {
      return content.document.getElementById("fname").previewValue == "John";
    }, "Preview happens");
  });

  await compareFillStates(
    browser,
    [
      { name: "fname", previewValue: "John", autofillState: "preview" },
      { name: "lname", previewValue: "Smith", autofillState: "preview" },
      {
        name: "address",
        previewValue: "32 Vassar Street MIT Room 32-G524",
        autofillState: "preview",
      },
      { name: "city", previewValue: "Cambridge", autofillState: "preview" },
      { name: "phone", previewValue: "6172535702", autofillState: "preview" },
      {
        name: "country",
        previewValue: "United States",
        autofillState: "preview",
      },
    ],
    "during preview"
  );

  await BrowserTestUtils.synthesizeKey("VK_RETURN", {}, browser);
  await closePopup(browser);
  await waitForAutofill(browser, "#fname", "John");

  await compareFillStates(
    browser,
    [
      { name: "fname", value: "John", autofillState: "autofill" },
      { name: "lname", value: "Smith", autofillState: "autofill" },
      {
        name: "address",
        value: "32 Vassar Street MIT Room 32-G524",
        autofillState: "autofill",
      },
      { name: "city", value: "Cambridge", autofillState: "autofill" },
      { name: "phone", value: "6172535702", autofillState: "autofill" },
      { name: "country", value: "United States", autofillState: "autofill" },
    ],
    "after filled"
  );

  await clearAutofill(browser, "basic autofill");
});

// This test verifies that modifying an <input> field and a <select> field
// that has been autofilled, updates the autofill state afterwards. In addition,
// check that the autofill state is updated correctly if the fields are hidden.
add_task(async function check_autofill_in_hidden_formfields() {
  let browser = gBrowser.selectedBrowser;

  for (let hideExtras of [false, true]) {
    info(
      "Tests where the form fields are " + (hideExtras ? "visible" : "hidden")
    );

    for (let testIndex = 1; testIndex <= 3; testIndex++) {
      await openPopupOn(browser, "#fname");
      await BrowserTestUtils.synthesizeKey("VK_DOWN", {}, browser);
      await BrowserTestUtils.synthesizeKey("VK_RETURN", {}, browser);
      await closePopup(browser);

      if (hideExtras) {
        await hideShowBlock(browser, true);
      }

      // Modify the last name <input>
      let autofillState = await SpecialPowers.spawn(
        browser,
        [testIndex],
        async testIndexChild => {
          let field = content.document.getElementById("lname");

          // Three different tests are performed.
          switch (testIndexChild) {
            case 1:
              info("Assign a value of an <input> using the value property");
              field.value = "Davidson";
              break;
            case 2:
              info("Adjust the value of an <input> using setUserInput");
              field.setUserInput("Robson");
              break;
            case 3:
              info(
                "Adjust the value by focusing the <input> and pressing a key"
              );
              field.setSelectionRange(5, 5);
              field.focus();
              EventUtils.synthesizeKey("a", {}, content);
              break;
          }

          return field.autofillState;
        }
      );
      is(
        autofillState,
        testIndex == 1 ? "autofill" : "",
        "input autofill state after change " +
          "input " +
          testIndex +
          " hide: " +
          hideExtras
      );

      // Reset the fields again.
      if (hideExtras) {
        await hideShowBlock(browser, false);
      }

      await SpecialPowers.spawn(browser, [], () => {
        content.document.getElementById("lname").value = "";
      });

      await clearAutofill(
        browser,
        "input " + testIndex + " hide: " + hideExtras
      );
    }

    // Modify the country <select>
    for (let testIndex = 1; testIndex <= 2; testIndex++) {
      await openPopupOn(browser, "#fname");
      await BrowserTestUtils.synthesizeKey("VK_DOWN", {}, browser);
      await BrowserTestUtils.synthesizeKey("VK_RETURN", {}, browser);
      await closePopup(browser);

      if (hideExtras) {
        await hideShowBlock(browser, true);
      }

      let popupShownPromise =
        testIndex == 3
          ? BrowserTestUtils.waitForSelectPopupShown(window)
          : null;

      let autofillState = await SpecialPowers.spawn(
        browser,
        [testIndex],
        async function (testIndexChild) {
          let field = content.document.getElementById("country");

          // Two different tests are performed.
          switch (testIndexChild) {
            case 1:
              info("Adjust the selectedIndex property of a <select>");
              field.selectedIndex = 4;
              break;
            case 2:
              info("Adjust the value property of a <select>");
              field.value = "Canada";
              break;
            case 3:
              field.showPicker();
              break;
          }
        }
      );

      if (testIndex == 3) {
        let popup = await popupShownPromise;
        await EventUtils.synthesizeKey("VK_DOWN", {}, browser);

        let popuphiddenPromise = BrowserTestUtils.waitForPopupEvent(
          popup,
          "popuphidden"
        );
        await EventUtils.synthesizeKey("VK_RETURN", {}, browser);
        await popuphiddenPromise;
      }

      autofillState = await SpecialPowers.spawn(browser, [], () => {
        return content.document.getElementById("country").autofillState;
      });
      is(
        autofillState,
        "",
        "select autofill state after change " +
          "select " +
          testIndex +
          " hide: " +
          hideExtras
      );

      // Reset the fields again.
      if (hideExtras) {
        await hideShowBlock(browser, false);
      }

      await SpecialPowers.spawn(browser, [], () => {
        content.document.getElementById("country").selectedIndex = -1;
      });

      await clearAutofill(
        browser,
        "select " + testIndex + " hide: " + hideExtras
      );
    }
  }
});
