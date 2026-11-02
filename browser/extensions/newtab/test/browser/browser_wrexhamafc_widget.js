"use strict";

/* global ContentTaskUtils */

test_newtab({
  async before({ pushPrefs }) {
    await pushPrefs(
      [
        "browser.newtabpage.activity-stream.discoverystream.config",
        JSON.stringify({ collapsible: true, enabled: true }),
      ],
      ["browser.newtabpage.activity-stream.widgets.system.enabled", true],
      ["browser.newtabpage.activity-stream.widgets.enabled", true],
      // First, so the collapsed widgets row doesn't hide the card.
      ["browser.newtabpage.activity-stream.widgets.order", "wrexhamAFC"],
      [
        "browser.newtabpage.activity-stream.widgets.system.wrexhamAFC.enabled",
        true,
      ],
      ["browser.newtabpage.activity-stream.widgets.wrexhamAFC.enabled", true]
    );
  },

  test: async function test_wrexhamafc_widget_shell() {
    const card = await ContentTaskUtils.waitForCondition(
      () =>
        content.document.querySelector('moz-widget[widget-id="wrexhamAFC"]'),
      "Wait for the Wrexham AFC card"
    );

    const header = card.querySelector(":scope > moz-widget-header");
    Assert.ok(header.dataset.l10nId, "Header is localized");
    // moz-widget-header moves its title off the host and into its heading.
    await ContentTaskUtils.waitForCondition(
      () => header.shadowRoot?.querySelector("#heading")?.textContent.trim(),
      "Wait for the header to show the widget's name"
    );
    Assert.equal(
      header.shadowRoot.querySelector("#heading").textContent.trim(),
      "Wrexham AFC",
      "The header shows the widget's name"
    );
    Assert.ok(header.getAttribute("menu-label"), "Has a menu-label set");

    const icon = header.querySelector(':scope > img[slot="leading"]');
    await ContentTaskUtils.waitForCondition(
      () => icon.complete && icon.naturalWidth,
      "Wait for the crest to load"
    );

    content.document
      .querySelector(".personalize-button, .open-customization-button")
      .click();
    await ContentTaskUtils.waitForCondition(
      () =>
        content.document
          .querySelector(".customize-menu")
          .classList.contains("customize-animate-enter-done"),
      "Wait for the Customize panel to open"
    );
    await ContentTaskUtils.waitForCondition(
      () =>
        content.document.querySelector(
          "#widgets-management-panel moz-box-button"
        ),
      "Wait for the widgets button in the Customize panel"
    );
    content.document
      .querySelector("#widgets-management-panel moz-box-button")
      .click();

    const toggle = await ContentTaskUtils.waitForCondition(
      () => content.document.querySelector("#wrexhamAFC-toggle"),
      "Wait for the Wrexham AFC toggle in the widgets panel"
    );
    await ContentTaskUtils.waitForCondition(
      () => toggle.getAttribute("label"),
      "Wait for the toggle to be localized"
    );
    Assert.equal(
      toggle.getAttribute("label"),
      "Wrexham AFC",
      "The widgets panel lists Wrexham AFC"
    );
    Assert.ok(toggle.hasAttribute("pressed"), "Its toggle is switched on");
  },
});
