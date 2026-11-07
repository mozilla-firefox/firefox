/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const TEST_PAGE =
  "chrome://mochitests/content/browser/browser/components/aiwindow/ui/test/browser/test_smartwindow_option_select_page.html";

describe("smartwindow-option-select", () => {
  let tab;
  let browser;

  beforeEach(async () => {
    tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, TEST_PAGE);
    browser = tab.linkedBrowser;
    await SpecialPowers.spawn(browser, [], async () => {
      await content.customElements.whenDefined("smartwindow-option-select");
      const select = content.document.getElementById("test-select");
      select.options = [
        {
          value: "a",
          l10nId: "aiwindow-input-model-select-button-label-fast",
          buttonL10nId: "aiwindow-input-model-select-button-label-custom",
        },
        {
          value: "b",
          l10nId: "aiwindow-input-model-select-button-label-allpurpose",
          secondaryLabelL10nId: "aiwindow-input-model-select-button-label-fast",
          descriptionL10nId:
            "aiwindow-input-model-select-button-label-personal",
        },
      ];
      select.value = "a";
      await select.updateComplete;
    });
  });

  afterEach(() => {
    BrowserTestUtils.removeTab(tab);
  });

  it("exposes items as menuitemradio with aria-checked", async () => {
    await SpecialPowers.spawn(browser, [], async () => {
      const select = content.document.getElementById("test-select");
      const items = Array.from(
        select.shadowRoot.querySelectorAll(".option-item")
      );
      Assert.deepEqual(
        items.map(item => item.getAttribute("role")),
        ["menuitemradio", "menuitemradio"],
        "Items are menuitemradio"
      );
      Assert.deepEqual(
        items.map(item => item.getAttribute("aria-checked")),
        ["true", "false"],
        "Only the selected item is checked"
      );
    });
  });

  it("names items by label and badge and describes them by description", async () => {
    await SpecialPowers.spawn(browser, [], async () => {
      const select = content.document.getElementById("test-select");
      const items = Array.from(
        select.shadowRoot.querySelectorAll(".option-item")
      );
      Assert.deepEqual(
        items[1]
          .getAttribute("aria-labelledby")
          .split(" ")
          .map(id => select.shadowRoot.getElementById(id)),
        [
          items[1].querySelector(".option-item-label"),
          items[1].querySelector(".option-item-badge"),
        ],
        "Item is named by its label and its badge, not its description"
      );
      Assert.equal(
        select.shadowRoot.getElementById(
          items[1].getAttribute("aria-describedby")
        ),
        items[1].querySelector(".option-item-description"),
        "Description is exposed as the item's description"
      );
      Assert.ok(
        !items[0].hasAttribute("aria-describedby"),
        "No description reference without a description"
      );
    });
  });

  it("dispatches change and updates the checked item on selection", async () => {
    await SpecialPowers.spawn(browser, [], async () => {
      const select = content.document.getElementById("test-select");
      const items = () =>
        Array.from(select.shadowRoot.querySelectorAll(".option-item"));

      const changed = new Promise(resolve =>
        select.addEventListener("smartwindow-option-select:change", resolve, {
          once: true,
        })
      );
      items()[1].click();
      const event = await changed;
      await select.updateComplete;

      Assert.equal(event.detail.value, "b", "Event reports the new value");
      Assert.equal(select.value, "b", "Value updated");
      Assert.deepEqual(
        items().map(item => item.getAttribute("aria-checked")),
        ["false", "true"],
        "Checked state follows the selection"
      );
    });
  });

  it("does not dispatch change when the selected item is chosen", async () => {
    await SpecialPowers.spawn(browser, [], async () => {
      const select = content.document.getElementById("test-select");
      let fired = false;
      select.addEventListener("smartwindow-option-select:change", () => {
        fired = true;
      });
      select.shadowRoot.querySelector(".option-item").click();
      await select.updateComplete;
      Assert.ok(!fired, "No change event for the current value");
    });
  });

  it("opens from the keyboard and selects with the arrow keys and Enter", async () => {
    await SpecialPowers.spawn(browser, [], async () => {
      const select = content.document.getElementById("test-select");
      select.focus();
      Assert.equal(
        select.shadowRoot.activeElement,
        select.shadowRoot.querySelector("moz-button"),
        "Focus is delegated to the button"
      );
    });
    await BrowserTestUtils.synthesizeKey("KEY_Enter", {}, browser);
    await SpecialPowers.spawn(browser, [], async () => {
      const select = content.document.getElementById("test-select");
      await ContentTaskUtils.waitForCondition(
        () => select.shadowRoot.activeElement?.matches(".option-item"),
        "An option is focused once the menu is open"
      );
    });
    await BrowserTestUtils.synthesizeKey("KEY_ArrowDown", {}, browser);
    await SpecialPowers.spawn(browser, [], async () => {
      const select = content.document.getElementById("test-select");
      Assert.equal(
        select.shadowRoot.activeElement.dataset.value,
        "b",
        "ArrowDown moves focus to the next option"
      );
    });
    await BrowserTestUtils.synthesizeKey("KEY_Enter", {}, browser);
    await SpecialPowers.spawn(browser, [], async () => {
      const select = content.document.getElementById("test-select");
      await ContentTaskUtils.waitForCondition(
        () => select.value == "b",
        "Enter selects the focused option"
      );
    });
  });
});
