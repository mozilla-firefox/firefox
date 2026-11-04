"use strict";

/* global ContentTaskUtils */

/**
 * @backward-compat { version 160 }
 * The Wrexham AFC card is drawn by moz-widget, which Firefox ships from 160.
 */
if (Services.vc.compare(Services.appinfo.version, "160.0a1") < 0) {
  add_task(function test_wrexhamafc_widget_needs_moz_widget() {
    ok(true, "Skipped: moz-widget ships in Firefox 160");
  });
} else {
  test_newtab({
    async before({ pushPrefs }) {
      // Serve a manifest entry for Wrexham instead of fetching from Merino.
      const feed = AboutNewTab.activityStream.store.feeds.get(
        "feeds.framedwidgetsfeed"
      );
      const sandbox = sinon.createSandbox();
      const { manifest, loaded } = feed;
      registerCleanupFunction(() => {
        sandbox.restore();
        feed.manifest = manifest;
        feed.loaded = loaded;
        feed.update();
      });
      sandbox.stub(feed, "loadFrames").callsFake(async () => {
        feed.manifest = {
          widgets: [
            {
              id: "wrexhamAFC",
              enabled: true,
              url: "https://example.com/browser/browser/extensions/newtab/test/browser/blue_page.html",
            },
          ],
        };
        feed.update();
        feed.loaded = true;
      });
      await pushPrefs(
        [
          "browser.newtabpage.activity-stream.widgets.framed.allowedHostSuffixes",
          "example.com",
        ],
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
      await feed.loadFrames();
    },

    test: async function test_wrexhamafc_widget_shell() {
      const card = await ContentTaskUtils.waitForCondition(
        () =>
          content.document.querySelector('moz-widget[widget-id="wrexhamAFC"]'),
        "Wait for the Wrexham AFC card"
      );

      Assert.ok(
        card.getAttribute("src").split("#")[0].endsWith("/blue_page.html"),
        "The card loads the manifest's URL for Wrexham"
      );

      const header = card.querySelector(":scope > moz-widget-header");
      Assert.ok(header.dataset.l10nId, "Header is localized");
      // Fluent can translate the header before it upgrades, when it applies only
      // the title. Translating the upgraded header applies its menu-label too.
      await content.customElements.whenDefined("moz-widget-header");
      await Cu.waiveXrays(header).updateComplete;
      await content.document.l10n.translateElements([header]);
      await Cu.waiveXrays(header).updateComplete;
      // moz-widget-header moves its title off the host and into its heading.
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
}
