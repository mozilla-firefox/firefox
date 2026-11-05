"use strict";

// Some linux WMs can't draw alpha-transparent rounded corners, so the panel
// falls back to square corners regardless of Nova (see the -moz-platform and
// -moz-gtk-csd-transparency-available checks in popup.css and
// extension-popup-panel.css).
const isLinuxWithoutCSDTransparency =
  AppConstants.platform == "linux" &&
  !window.matchMedia("(-moz-gtk-csd-transparency-available)").matches;

async function testPopupBorderRadius({ uiDensity }) {
  let extension = ExtensionTestUtils.loadExtension({
    background() {
      browser.tabs.query({ active: true, currentWindow: true }, tabs => {
        browser.pageAction.show(tabs[0].id);
      });
    },

    manifest: {
      browser_action: {
        default_popup: "popup.html",
        default_area: "navbar",
        browser_style: false,
      },

      page_action: {
        default_popup: "popup.html",
        browser_style: false,
      },
    },

    files: {
      "popup.html": `<!DOCTYPE html>
        <html>
          <head><meta charset="utf-8"></head>
          <body style="width: 100px; height: 100px;"></body>
        </html>`,
    },
  });

  await extension.startup();

  let widget = getBrowserActionWidget(extension);

  function getExpectedRadius() {
    let defaultRadius = "24px";

    const density = Services.prefs.getIntPref("browser.uidensity", 0);
    if (density == 1) {
      // The border radius is different with compact density
      defaultRadius = "18px";
    }

    // If the panel is embedded in subview (in practive we always disallow
    // subviews since the extensions action button can't be overflowed in
    // the default overfow panel), or Firefox is running on a Linux WMs that
    // can't draw alpha-transparent rounded corners, then the panel radius
    // is expected to be 0.
    return !widget.disallowSubView || isLinuxWithoutCSDTransparency
      ? "0px"
      : defaultRadius;
  }

  async function testPanel(browser, standAlone = true) {
    let panel = getPanelForNode(browser);
    let arrowContent = panel.panelContent;

    let panelStyle = getComputedStyle(arrowContent);
    is(panelStyle.overflow, "clip", "overflow is clipped");

    let stack = browser.parentNode;
    let viewNode = stack.parentNode === panel ? browser : stack.parentNode;
    let viewStyle = getComputedStyle(viewNode);
    let expectedRadius = getExpectedRadius();

    let props = [
      "borderTopLeftRadius",
      "borderTopRightRadius",
      "borderBottomRightRadius",
      "borderBottomLeftRadius",
    ];

    let bodyStyle = await SpecialPowers.spawn(
      browser,
      [props],
      async function (props) {
        let bodyStyle = content.getComputedStyle(content.document.body);

        return new Map(props.map(prop => [prop, bodyStyle[prop]]));
      }
    );

    for (let prop of props) {
      if (standAlone) {
        is(
          viewStyle[prop],
          panelStyle[prop],
          `Panel and view ${prop} should be the same (${expectedRadius})`
        );
        is(
          bodyStyle.get(prop),
          panelStyle[prop],
          `Panel and body ${prop} should be the same (${expectedRadius})`
        );
      } else {
        is(
          viewStyle[prop],
          expectedRadius,
          `View node ${prop} should be ${expectedRadius}`
        );
        is(
          bodyStyle.get(prop),
          expectedRadius,
          `Body node ${prop} should be ${expectedRadius}`
        );
      }
    }
  }

  await SpecialPowers.pushPrefEnv({
    set: [["browser.uidensity", uiDensity]],
  });

  {
    info(`Test stand-alone browserAction popup with density ${uiDensity}`);

    clickBrowserAction(extension);
    let browser = await awaitExtensionPanel(extension);
    await testPanel(browser);
    await closeBrowserAction(extension);
  }

  {
    info(`Test menu panel browserAction popup with density ${uiDensity}`);

    CustomizableUI.addWidgetToArea(widget.id, getCustomizableUIPanelID());

    clickBrowserAction(extension);
    let browser = await awaitExtensionPanel(extension);
    await testPanel(browser, false);
    await closeBrowserAction(extension);
  }

  {
    info(`Test pageAction popup with density ${uiDensity}`);

    clickPageAction(extension);
    let browser = await awaitExtensionPanel(extension);
    await testPanel(browser);
    await closePageAction(extension);
  }

  await SpecialPowers.popPrefEnv();
  await extension.unload();
}

// Test both normal and compact density, which may use different radii.
add_task(async function test_popup_border_radius_normal_density() {
  await testPopupBorderRadius({ uiDensity: 0 });
});

add_task(async function test_popup_border_radius_compact_density() {
  await testPopupBorderRadius({ uiDensity: 1 });
});
