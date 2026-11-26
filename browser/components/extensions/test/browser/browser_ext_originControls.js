"use strict";

requestLongerTimeout(2);

const { AddonTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/AddonTestUtils.sys.mjs"
);

const { ExtensionPermissions, QuarantinedDomains } = ChromeUtils.importESModule(
  "resource://gre/modules/ExtensionPermissions.sys.mjs"
);

AddonTestUtils.initMochitest(this);

loadTestSubscript("head_unified_extensions.js");

const RED =
  "iVBORw0KGgoAAAANSUhEUgAAANwAAADcCAYAAAAbWs+BAAAABmJLR0QA/wD/AP+gvaeTAAAACXBIWXMAAAsTAAALEwEAmpwYAAAAB3RJTUUH4gIUARQAHY8+4wAAApBJREFUeNrt3cFqAjEUhlEjvv8rXzciiiBGk/He5JxdN2U649dY+KmnEwAAAAAv2uMXEeGOwERntwAEB4IDBAeCAwQHggPBAYIDwQGCA8GB4ADBgeAAwYHgAMGB4EBwgOCgpkuKq2it/r8Li2hbvGKqP6s/PycnHHv9YvSWEgQHCA4EBwgOBAeCAwQHggMEByXM+QRUE6D3suwuPafDn5MTDg50KXnVPSdxa54y/oYDwQGCA8EBggPBAYIDwYHggBE+X5rY3Y3Tey97Nn2eU+rnlGfaZa6Ft5SA4EBwgOBAcCA4QHAgOEBwIDjgZu60y1xrDPtIJxwgOBAcIDgQHAgOEBwIDhAcCA4EBwgOBAcIDgQHCA4EB4IDBAeCAwQHggPBAYIDwQGCA8GB4ADBgeAAwYHgAMGB4GADcz9y2McIgxMOBAeCAwQHggMEB4IDwQGCA8EBggPBATdP6+KIGPRdW7i1LCFi6ALfCQfeUoLgAMGB4ADBgeBAcIDgQHCA4CCdOVvK7quwveQgg7eRTjjwlhIQHAgOBAcIDgQHCA4EB4IDBAfl5dhSdl+17SX3F22rdLlOOBAcCA4QHAgOEBwIDgQHCA4EBwgO0qm5pez6Ce0uSym2jXTCgeAAwYHgQHCA4EBwgOBAcCA4QHBQ3vpbyu47Yns51OLbSCccCA4QHAgOBAcIDgQHCA4EB4ID5jDt+vkObjgFM9dywoHgAMGB4EBwgOBAcIDgQHAgOEBwsA5bysPveMLtpW2kEw4EBwgOBAcIDgQHggMEB4IDBAeCg33ZUqZ/Ql9sL20jnXCA4EBwIDhAcCA4QHAgOBAcIDgQHNOZai3DlhKccCA4QHAgOEBwIDgQHCA4AAAAAGA1VyxaWIohrgXFAAAAAElFTkSuQmCC";

const l10n = new Localization(
  ["branding/brand.ftl", "browser/extensionsUI.ftl"],
  true
);

add_setup(async function setup() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["test.wait300msAfterTabSwitch", true],
      ["extensions.originControls.grantByDefault", false],
    ],
  });

  // Reset CustomizableUI state to prevent failures in tests that
  // follow this one in the same toml file (due to two test files
  // using the same extension id but don't expect the extension
  // action button to inherit a previous CustomizableUI state).
  //
  // TODO: it may be worth considering if we should make sure this
  // is done implicitly for all mochitests? (eg. like we do move
  // the mouse to avoid this kind of unexpected interactions between
  // different mochitest files running on the same browser instance).
  registerCleanupFunction(async () => {
    await CustomizableUI.reset();
  });
});

async function makeExtension({
  useAddonManager = "temporary",
  manifest_version = 3,
  id,
  permissions,
  host_permissions,
  optional_host_permissions,
  optional_permissions,
  content_scripts,
  granted,
  default_area,
}) {
  info(
    `Loading extension ` +
      JSON.stringify({
        id,
        permissions,
        optional_permissions,
        host_permissions,
        optional_host_permissions,
        granted,
      })
  );

  let manifest = {
    name: `Test extension ${id}`,
    manifest_version,
    browser_specific_settings: { gecko: { id } },
    permissions,
    optional_permissions,
    host_permissions,
    optional_host_permissions,
    content_scripts,
    action: {
      default_popup: "popup.html",
      default_area: default_area || "navbar",
    },
    icons: {
      16: "red.png",
    },
  };
  if (manifest_version < 3) {
    manifest.browser_action = manifest.action;
    delete manifest.action;
  }

  let ext = ExtensionTestUtils.loadExtension({
    manifest,

    useAddonManager,

    async background() {
      browser.permissions.onAdded.addListener(({ origins }) => {
        browser.test.sendMessage("granted", origins.join());
      });
      browser.permissions.onRemoved.addListener(({ origins }) => {
        browser.test.sendMessage("revoked", origins.join());
      });

      browser.runtime.onInstalled.addListener(async () => {
        if (browser.menus) {
          let submenu = browser.menus.create({
            id: "parent",
            title: "submenu",
            contexts: ["action"],
          });
          browser.menus.create({
            id: "child1",
            title: "child1",
            parentId: submenu,
          });
          await new Promise(resolve => {
            browser.menus.create(
              {
                id: "child2",
                title: "child2",
                parentId: submenu,
              },
              resolve
            );
          });
        }

        browser.test.sendMessage("ready");
      });
    },

    files: {
      "red.png": imageBufferFromDataURI(RED),
      "popup.html": `<!DOCTYPE html><meta charset=utf-8>Test Popup`,
    },
  });

  if (granted) {
    info("Granting initial permissions.");
    await ExtensionPermissions.add(id, { permissions: [], origins: granted });
  }

  await ext.startup();
  await ext.awaitMessage("ready");
  return ext;
}

async function testQuarantinePopup(popup, extensionId) {
  const addonName = WebExtensionPolicy.getByID(extensionId).name;
  let [title, line1, line2] = await l10n.formatMessages([
    {
      id: "webext-quarantine-confirmation-title",
      args: { addonName },
    },
    "webext-quarantine-confirmation-line-1",
    "webext-quarantine-confirmation-line-2",
  ]);
  let [titleEl, , helpEl] = popup.querySelectorAll("description");

  ok(popup.getAttribute("icon").endsWith("/red.png"), "Correct icon.");

  is(title.value, titleEl.textContent, "Correct title.");
  is(line1.value + "\n\n" + line2.value, helpEl.textContent, "Correct lines.");
}

async function testOriginControls(
  extension,
  { contextMenuId },
  {
    items,
    selected,
    click,
    granted,
    revoked,
    attention,
    quarantined,
    allowQuarantine,
  }
) {
  info(
    `Testing ${extension.id} on ${gBrowser.currentURI.spec} with contextMenuId=${contextMenuId}.`
  );

  // The button's attention state is updated in a requestAnimationFrame
  // callback (see updateButton in ext-browserAction.js), and native macOS
  // context menus can open before it runs.
  await promiseAnimationFrame();

  let buttonOrWidget;
  let menu;
  let nextMenuItemClassName;

  switch (contextMenuId) {
    case "toolbar-context-menu": {
      let target = `#${CSS.escape(makeWidgetId(extension.id))}-BAP`;
      buttonOrWidget = document
        .querySelector(target)
        .closest("toolbaritem.unified-extensions-item");
      menu = await openChromeContextMenu(contextMenuId, target);
      nextMenuItemClassName = "customize-context-manageExtension";
      break;
    }

    case "unified-extensions-context-menu":
      await openExtensionsPanel();
      buttonOrWidget = getUnifiedExtensionsItem(extension.id);
      menu = await openUnifiedExtensionsContextMenu(extension.id);
      nextMenuItemClassName = "unified-extensions-context-menu-pin-to-toolbar";
      break;

    default:
      throw new Error(`unexpected context menu "${contextMenuId}"`);
  }

  let doc = menu.ownerDocument;
  let visibleOriginItems = menu.querySelectorAll(
    ":is(menuitem, menuseparator):not([hidden])"
  );

  info("Check expected menu items.");
  for (let i = 0; i < items.length; i++) {
    let l10n = doc.l10n.getAttributes(visibleOriginItems[i]);
    Assert.deepEqual(
      l10n,
      items[i],
      `Visible menu item ${i} has correct l10n attrs.`
    );

    let checked = visibleOriginItems[i].hasAttribute("checked");
    is(i === selected, checked, `Expected checked value for item ${i}.`);
  }

  if (items.length) {
    is(
      visibleOriginItems[items.length].nodeName,
      "menuseparator",
      "Found separator."
    );
    is(
      visibleOriginItems[items.length + 1].className,
      nextMenuItemClassName,
      "All items accounted for."
    );
  }

  is(
    buttonOrWidget.hasAttribute("attention"),
    !!attention,
    "Expected attention badge before clicking."
  );

  Assert.deepEqual(
    document.l10n.getAttributes(
      buttonOrWidget.querySelector(".unified-extensions-item-action-button")
    ),
    {
      // eslint-disable-next-line no-nested-ternary
      id: attention
        ? quarantined
          ? "origin-controls-toolbar-button-quarantined"
          : "origin-controls-toolbar-button-permission-needed"
        : "origin-controls-toolbar-button",
      args: {
        extensionTitle: `Test extension ${extension.id}`,
      },
    },
    "Correct l10n message."
  );

  let itemToClick;
  if (click) {
    itemToClick = visibleOriginItems[click];
  }

  let quarantinePopup;
  if (itemToClick && quarantined) {
    quarantinePopup = promisePopupNotificationShown("addon-webext-permissions");
  }

  // Clicking a menu item of the unified extensions context menu should close
  // the unified extensions panel automatically.
  let panelHidden =
    itemToClick && contextMenuId === "unified-extensions-context-menu"
      ? BrowserTestUtils.waitForEvent(document, "popuphidden", true)
      : Promise.resolve();

  await closeChromeContextMenu(contextMenuId, itemToClick);
  await panelHidden;

  // When there is no menu item to close, we should manually close the unified
  // extensions panel because simply closing the context menu will not close
  // it.
  if (!itemToClick && contextMenuId === "unified-extensions-context-menu") {
    await closeExtensionsPanel();
  }

  if (granted) {
    info("Waiting for the permissions.onAdded event.");
    let host = await extension.awaitMessage("granted");
    is(host, granted.join(), "Expected host permission granted.");
  }
  if (revoked) {
    info("Waiting for the permissions.onRemoved event.");
    let host = await extension.awaitMessage("revoked");
    is(host, revoked.join(), "Expected host permission revoked.");
  }

  if (quarantinePopup) {
    let popup = await quarantinePopup;
    await testQuarantinePopup(popup, extension.id);

    if (allowQuarantine) {
      // Wait for the AOM onPropertyChanged event emitted when the quarantineIgnoredByUser
      // AddonWrapper property has been changed as expected by clicking the primary button
      // on the popup (Needed to prevent intermittent failures due to a race between the
      // internals reacting to the quarantine popup button and the assertions checking that
      // the context menu items has been refreshed to reflect the updated access to the
      // quarantined websites granted to the specific extension being tested).
      const promiseQuarantineAllowed = AddonTestUtils.promiseAddonEvent(
        "onPropertyChanged",
        (addon, changedProps) =>
          addon.id === extension.id &&
          changedProps.includes("quarantineIgnoredByUser")
      );
      popup.button.click();
      await promiseQuarantineAllowed;
    } else {
      popup.secondaryButton.click();
    }
  }
}

// Move the widget to the toolbar or the addons panel (if Unified Extensions
// is enabled) or the overflow panel otherwise.
function moveWidget(ext, pinToToolbar = false) {
  let area = pinToToolbar
    ? CustomizableUI.AREA_NAVBAR
    : CustomizableUI.AREA_ADDONS;
  let widgetId = `${makeWidgetId(ext.id)}-browser-action`;
  CustomizableUI.addWidgetToArea(widgetId, area);
}

const originControlsInContextMenu = async options => {
  // Has no permissions.
  let ext1 = await makeExtension({ id: "ext1@test" });

  // Has activeTab and (ungranted) example.com permissions.
  let ext2 = await makeExtension({
    id: "ext2@test",
    permissions: ["activeTab"],
    host_permissions: ["*://example.com/*"],
    useAddonManager: "permanent",
  });

  // Has ungranted <all_urls>, and granted example.com.
  let ext3 = await makeExtension({
    id: "ext3@test",
    host_permissions: ["<all_urls>"],
    granted: ["*://example.com/*"],
    useAddonManager: "permanent",
  });

  // Has granted <all_urls>.
  let ext4 = await makeExtension({
    id: "ext4@test",
    host_permissions: ["<all_urls>"],
    optional_host_permissions: ["<all_urls>"],
    granted: ["<all_urls>"],
    useAddonManager: "permanent",
  });

  // MV2 extension with an <all_urls> content script and activeTab.
  let ext5 = await makeExtension({
    manifest_version: 2,
    id: "ext5@test",
    permissions: ["activeTab"],
    content_scripts: [
      {
        matches: ["<all_urls>"],
        css: [],
      },
    ],
    useAddonManager: "permanent",
  });

  let ext6 = await makeExtension({
    id: "ext6@test",
    optional_host_permissions: ["<all_urls>"],
    useAddonManager: "permanent",
  });

  let ext7 = await makeExtension({
    id: "ext7@test",
    optional_permissions: ["*://example.com/*"],
    useAddonManager: "permanent",
  });

  // MV3 with two separate "http://*/*" and "https://*/*" host_permissions
  // (used to cover Bug 1856383 with explicit test assertions).
  let ext8 = await makeExtension({
    id: "ext8@test",
    host_permissions: ["http://*/*", "https://*/*"],
    granted: ["http://*/*", "https://*/*"],
    useAddonManager: "permanent",
  });

  // Add an extension always visible in the extensions panel.
  let ext_last = await makeExtension({
    id: "ext_last@test",
    default_area: "menupanel",
  });

  let extensions = [ext1, ext2, ext3, ext4, ext5, ext6, ext7, ext8, ext_last];

  let unifiedButton;
  if (options.contextMenuId === "unified-extensions-context-menu") {
    moveWidget(ext1, false);
    moveWidget(ext2, false);
    moveWidget(ext3, false);
    moveWidget(ext4, false);
    moveWidget(ext5, false);
    moveWidget(ext6, false);
    moveWidget(ext7, false);
    moveWidget(ext8, false);
    unifiedButton = document.querySelector("#unified-extensions-button");
  } else {
    // TestVerify runs this again in the same Firefox instance, so move the
    // widgets back to the toolbar for testing outside the unified extensions
    // panel.
    moveWidget(ext1, true);
    moveWidget(ext2, true);
    moveWidget(ext3, true);
    moveWidget(ext4, true);
    moveWidget(ext5, true);
    moveWidget(ext6, true);
    moveWidget(ext7, true);
    moveWidget(ext8, true);
  }

  const NO_ACCESS = { id: "origin-controls-no-access", args: null };
  const QUARANTINED = {
    id: "origin-controls-quarantined-status",
    args: null,
  };
  const ALLOW_QUARANTINED = {
    id: "origin-controls-quarantined-allow",
    args: null,
  };
  const ACCESS_OPTIONS = { id: "origin-controls-options", args: null };
  const ALL_SITES = { id: "origin-controls-option-all-domains", args: null };
  const WHEN_CLICKED = {
    id: "origin-controls-option-when-clicked",
    args: null,
  };

  const UNIFIED_NO_ATTENTION = { id: "unified-extensions-button", args: null };
  const UNIFIED_ATTENTION = {
    id: "unified-extensions-button-permissions-needed",
    args: null,
  };
  const UNIFIED_QUARANTINED = {
    id: "unified-extensions-button-quarantined",
    args: null,
  };

  await BrowserTestUtils.withNewTab("about:blank", async () => {
    await testOriginControls(ext1, options, { items: [NO_ACCESS] });
    await testOriginControls(ext2, options, { items: [NO_ACCESS] });
    await testOriginControls(ext3, options, { items: [NO_ACCESS] });
    await testOriginControls(ext4, options, { items: [NO_ACCESS] });
    await testOriginControls(ext5, options, { items: [] });
    await testOriginControls(ext6, options, { items: [NO_ACCESS] });
    await testOriginControls(ext7, options, { items: [NO_ACCESS] });

    if (unifiedButton) {
      ok(
        !unifiedButton.hasAttribute("attention"),
        "No extension will have attention indicator on about:blank."
      );
      Assert.deepEqual(
        document.l10n.getAttributes(unifiedButton),
        UNIFIED_NO_ATTENTION,
        "Unified button has no permissions needed tooltip."
      );
    }
  });

  await BrowserTestUtils.withNewTab("http://mochi.test:8888/", async () => {
    const ALWAYS_ON = {
      id: "origin-controls-option-always-on",
      args: { domain: "mochi.test" },
    };

    await testOriginControls(ext1, options, { items: [NO_ACCESS] });

    // Has activeTab.
    await testOriginControls(ext2, options, {
      items: [ACCESS_OPTIONS, WHEN_CLICKED],
      selected: 1,
      attention: false,
    });

    // Could access mochi.test when clicked.
    await testOriginControls(ext3, options, {
      items: [ACCESS_OPTIONS, WHEN_CLICKED, ALWAYS_ON],
      selected: 1,
      attention: true,
    });

    // Has <all_urls> granted at install time.
    await testOriginControls(ext4, options, {
      items: [ACCESS_OPTIONS, ALL_SITES],
      selected: 1,
      attention: false,
    });

    // Revoke the all_urls permission, then verify that we show the attention indicator
    // for the  <all_urls> listed as host_permissions even when the same <all_urls>
    // is also listed as optional_host_permissions.
    const promiseRevokedPermission = ext4.awaitMessage("revoked");
    await ExtensionPermissions.remove(
      ext4.id,
      { permissions: ["<all_urls>"], origins: ["<all_urls>"] },
      WebExtensionPolicy.getByID(ext4.id).extension
    );
    is(
      await promiseRevokedPermission,
      "<all_urls>",
      "Expected ext4 <all_urls> permission to be revoked"
    );
    await testOriginControls(ext4, options, {
      items: [ACCESS_OPTIONS, WHEN_CLICKED, ALWAYS_ON],
      selected: 1,
      attention: true,
    });

    // Grant back the <all_urls> permission as it was originally
    // granted at install time.
    const promiseGrantedPermission = ext4.awaitMessage("granted");
    await ExtensionPermissions.add(
      ext4.id,
      { permissions: [], origins: ["<all_urls>"] },
      WebExtensionPolicy.getByID(ext4.id).extension
    );
    is(
      await promiseGrantedPermission,
      "<all_urls>",
      "Expected ext4 <all_urls> permission to be granted"
    );

    // MV2 extension, has no origin controls, and never flags for attention.
    await testOriginControls(ext5, options, { items: [], attention: false });

    // MV3 with <all_urls> in optional_host_permissions not already granted.
    await testOriginControls(ext6, options, {
      items: [ACCESS_OPTIONS, WHEN_CLICKED, ALWAYS_ON],
      selected: 1,
      attention: false,
    });

    // MV3 with "*://example.com/*" in optional_permissions not already granted.
    await testOriginControls(ext7, options, { items: [NO_ACCESS] });

    // MV3 with two separate "http://*/*" and "https://*/*" host_permissions
    // already granted.
    // The two permissions combined are expected to be showing the ALL_SITES
    // message as with a single "<all_urls>" or "*://*/*" host permission
    // (Bug 1856383).
    await testOriginControls(ext8, options, {
      items: [ACCESS_OPTIONS, ALL_SITES],
      selected: 1,
      attention: false,
    });

    if (unifiedButton) {
      ok(
        unifiedButton.hasAttribute("attention"),
        "Both ext2 and ext3 are WHEN_CLICKED for example.com, so show attention indicator."
      );
      Assert.deepEqual(
        document.l10n.getAttributes(unifiedButton),
        UNIFIED_ATTENTION,
        "UEB has permissions needed tooltip."
      );
    }
  });

  info("Testing again with mochi.test now quarantined.");
  await SpecialPowers.pushPrefEnv({
    set: [
      ["extensions.quarantinedDomains.enabled", true],
      ["extensions.quarantinedDomains.list", "mochi.test"],
    ],
  });

  // Reset quarantined state between test runs.
  QuarantinedDomains.setUserAllowedAddonIdPref(ext2.id, false);
  QuarantinedDomains.setUserAllowedAddonIdPref(ext4.id, false);
  QuarantinedDomains.setUserAllowedAddonIdPref(ext5.id, false);
  QuarantinedDomains.setUserAllowedAddonIdPref(ext6.id, false);
  QuarantinedDomains.setUserAllowedAddonIdPref(ext8.id, false);

  await BrowserTestUtils.withNewTab("http://mochi.test:8888/", async () => {
    const ALWAYS_ON = {
      id: "origin-controls-option-always-on",
      args: { domain: "mochi.test" },
    };

    await testOriginControls(ext1, options, {
      items: [NO_ACCESS],
      attention: false,
    });

    await testOriginControls(ext2, options, {
      items: [QUARANTINED, ALLOW_QUARANTINED],
      attention: true,
      quarantined: true,
      click: 1,
      allowQuarantine: false,
    });
    // Still quarantined.
    await testOriginControls(ext2, options, {
      items: [QUARANTINED, ALLOW_QUARANTINED],
      attention: true,
      quarantined: true,
      click: 1,
      allowQuarantine: true,
    });
    // Not quarantined anymore.
    await testOriginControls(ext2, options, {
      items: [ACCESS_OPTIONS, WHEN_CLICKED],
      selected: 1,
      attention: false,
      quarantined: false,
    });

    await testOriginControls(ext3, options, {
      items: [QUARANTINED, ALLOW_QUARANTINED],
      attention: true,
      quarantined: true,
    });

    await testOriginControls(ext4, options, {
      items: [QUARANTINED, ALLOW_QUARANTINED],
      attention: true,
      quarantined: true,
      click: 1,
      allowQuarantine: true,
    });
    await testOriginControls(ext4, options, {
      items: [ACCESS_OPTIONS, ALL_SITES],
      selected: 1,
      attention: false,
      quarantined: false,
    });

    // MV2 normally don't have controls, but we always show for quarantined.
    await testOriginControls(ext5, options, {
      items: [QUARANTINED, ALLOW_QUARANTINED],
      attention: true,
      quarantined: true,
      click: 1,
      allowQuarantine: true,
    });
    await testOriginControls(ext5, options, {
      items: [],
      attention: false,
      quarantined: false,
    });

    await testOriginControls(ext6, options, {
      items: [QUARANTINED, ALLOW_QUARANTINED],
      attention: true,
      quarantined: true,
      click: 1,
      allowQuarantine: true,
    });
    await testOriginControls(ext6, options, {
      items: [ACCESS_OPTIONS, WHEN_CLICKED, ALWAYS_ON],
      selected: 1,
      attention: false,
      quarantined: false,
    });

    await testOriginControls(ext7, options, {
      items: [NO_ACCESS],
      attention: false,
    });

    await testOriginControls(ext8, options, {
      items: [QUARANTINED, ALLOW_QUARANTINED],
      attention: true,
      quarantined: true,
      click: 1,
      allowQuarantine: true,
    });
    await testOriginControls(ext8, options, {
      items: [ACCESS_OPTIONS, ALL_SITES],
      selected: 1,
      attention: false,
      quarantined: false,
    });

    if (unifiedButton) {
      ok(unifiedButton.hasAttribute("attention"), "Expected attention UI");
      Assert.deepEqual(
        document.l10n.getAttributes(unifiedButton),
        UNIFIED_QUARANTINED,
        "Expected attention tooltip text for quarantined domains"
      );
    }
  });

  if (unifiedButton) {
    extensions.forEach(extension =>
      moveWidget(extension, /* pinToToolbar */ true)
    );

    await BrowserTestUtils.withNewTab("http://mochi.test:8888/", async () => {
      ok(unifiedButton.hasAttribute("attention"), "Expected attention UI");
      Assert.deepEqual(
        document.l10n.getAttributes(unifiedButton),
        UNIFIED_QUARANTINED,
        "Expected attention tooltip text for quarantined domains"
      );

      await openExtensionsPanel();

      const messages = getMessageBars();
      Assert.equal(messages.length, 1, "expected a message");
      const supportLink = messages[0].querySelector("a");
      Assert.equal(
        supportLink.getAttribute("support-page"),
        "quarantined-domains",
        "Expected the correct support page ID"
      );

      await closeExtensionsPanel();
    });

    extensions.forEach(extension =>
      moveWidget(extension, /* pinToToolbar */ false)
    );
  }

  await SpecialPowers.popPrefEnv();

  await BrowserTestUtils.withNewTab("http://example.com/", async () => {
    const ALWAYS_ON = {
      id: "origin-controls-option-always-on",
      args: { domain: "example.com" },
    };

    await testOriginControls(ext1, options, { items: [NO_ACCESS] });

    // Click already selected options, expect no permission changes.
    await testOriginControls(ext2, options, {
      items: [ACCESS_OPTIONS, WHEN_CLICKED, ALWAYS_ON],
      selected: 1,
      click: 1,
      // This extension has the activeTab permission, but the current tab
      // has a tab url that matches one of the extension host permission
      // which is not granted yet and so we expect the attention badge
      // to be shown.
      attention: true,
    });
    await testOriginControls(ext3, options, {
      items: [ACCESS_OPTIONS, WHEN_CLICKED, ALWAYS_ON],
      selected: 2,
      click: 2,
      attention: false,
    });
    await testOriginControls(ext4, options, {
      items: [ACCESS_OPTIONS, ALL_SITES],
      selected: 1,
      click: 1,
      attention: false,
    });

    await testOriginControls(ext5, options, { items: [], attention: false });

    await testOriginControls(ext6, options, {
      items: [ACCESS_OPTIONS, WHEN_CLICKED, ALWAYS_ON],
      selected: 1,
      click: 1,
      // This MV3 extension has <all_urls> host permission,
      // but only listed in the optional_host_permissions manifest properties,
      // and so we expect the attention indicator to not be shown.
      attention: false,
    });

    await testOriginControls(ext7, options, {
      items: [ACCESS_OPTIONS, WHEN_CLICKED, ALWAYS_ON],
      selected: 1,
      click: 1,
      // This MV3 extension has "*://example.com/" host permission,
      // but only listed in the optional_permissions manifest properties
      // and so we expect the attention indicator to not be shown.
      attention: false,
    });

    if (unifiedButton) {
      ok(
        unifiedButton.hasAttribute("attention"),
        "ext2 is WHEN_CLICKED and showing the attention indicator for example.com"
      );
      Assert.deepEqual(
        document.l10n.getAttributes(unifiedButton),
        UNIFIED_ATTENTION,
        "UEB attention due to ext2 extension showing an attention indicator."
      );
    }

    // Click the other option, expect example.com permission granted/revoked.
    await testOriginControls(ext2, options, {
      items: [ACCESS_OPTIONS, WHEN_CLICKED, ALWAYS_ON],
      selected: 1,
      click: 2,
      granted: ["*://example.com/*"],
      attention: true,
    });
    if (unifiedButton) {
      ok(
        !unifiedButton.hasAttribute("attention"),
        "Bot ext2 and ext3 are ALWAYS_ON for example.com, so no attention indicator."
      );
      Assert.deepEqual(
        document.l10n.getAttributes(unifiedButton),
        UNIFIED_NO_ATTENTION,
        "Unified button has no permissions needed tooltip."
      );
    }

    await testOriginControls(ext3, options, {
      items: [ACCESS_OPTIONS, WHEN_CLICKED, ALWAYS_ON],
      selected: 2,
      click: 1,
      revoked: ["*://example.com/*"],
      attention: false,
    });
    if (unifiedButton) {
      ok(
        unifiedButton.hasAttribute("attention"),
        "ext3 is now WHEN_CLICKED for example.com, show attention indicator."
      );
      Assert.deepEqual(
        document.l10n.getAttributes(unifiedButton),
        UNIFIED_ATTENTION,
        "UEB attention for only one extension."
      );
    }

    // Other option is now selected.
    await testOriginControls(ext2, options, {
      items: [ACCESS_OPTIONS, WHEN_CLICKED, ALWAYS_ON],
      selected: 2,
      attention: false,
    });
    await testOriginControls(ext3, options, {
      items: [ACCESS_OPTIONS, WHEN_CLICKED, ALWAYS_ON],
      selected: 1,
      attention: true,
    });

    if (unifiedButton) {
      ok(
        unifiedButton.hasAttribute("attention"),
        "Still showing the attention indicator."
      );
      Assert.deepEqual(
        document.l10n.getAttributes(unifiedButton),
        UNIFIED_ATTENTION,
        "UEB attention for only one extension."
      );
    }
  });

  // Regression test for Bug 1861002.
  const addonListener = {
    registered: false,
    onPropertyChanged(addon, changedProps) {
      ok(
        addon,
        `onPropertyChanged should not be called without an AddonWrapper for changed properties: ${changedProps}`
      );
    },
  };
  AddonManager.addAddonListener(addonListener);
  addonListener.registered = true;
  const unregisterAddonListener = () => {
    if (!addonListener.registered) {
      return;
    }
    AddonManager.removeAddonListener(addonListener);
    addonListener.registered = false;
  };
  registerCleanupFunction(unregisterAddonListener);

  let { messages } = await AddonTestUtils.promiseConsoleOutput(async () => {
    await Promise.all(extensions.map(e => e.unload()));
  });

  unregisterAddonListener();

  AddonTestUtils.checkMessages(
    messages,
    {
      forbidden: [
        {
          message:
            /AddonListener threw exception when calling onPropertyChanged/,
        },
      ],
    },
    "Expect no exception raised from AddonListener onPropertyChanged callbacks"
  );
};

add_task(async function originControls_in_browserAction_contextMenu() {
  await originControlsInContextMenu({ contextMenuId: "toolbar-context-menu" });
});

add_task(async function originControls_in_unifiedExtensions_contextMenu() {
  await originControlsInContextMenu({
    contextMenuId: "unified-extensions-context-menu",
  });
});

add_task(async function test_attention_dot_when_pinning_extension() {
  const extension = await makeExtension({
    permissions: ["activeTab"],
    host_permissions: ["*://mochi.test/*"],
  });
  await extension.startup();

  await ExtensionPermissions.remove(
    extension.id,
    { origins: ["*://mochi.test/*"], permissions: [] },
    extension.extension
  );

  const unifiedButton = document.querySelector("#unified-extensions-button");
  const extensionWidgetID = AppUiTestInternals.getBrowserActionWidgetId(
    extension.id
  );
  const extensionWidget =
    CustomizableUI.getWidget(extensionWidgetID).forWindow(window).node;

  await BrowserTestUtils.withNewTab("http://mochi.test:8888/", async () => {
    // The extensions should be placed in the navbar by default so we do not
    // expect an attention dot on the Unifed Extensions Button (UEB), only on
    // the extension (widget) itself.
    ok(
      !unifiedButton.hasAttribute("attention"),
      "expected no attention attribute on the UEB"
    );
    ok(
      extensionWidget.hasAttribute("attention"),
      "expected attention attribute on the extension widget"
    );

    // Open the context menu of the extension and unpin the extension.
    let contextMenu = await openChromeContextMenu(
      "toolbar-context-menu",
      `#${CSS.escape(extensionWidgetID)}`
    );
    let pinToToolbar = contextMenu.querySelector(
      ".customize-context-pinToToolbar"
    );
    ok(pinToToolbar, "expected a 'Pin to Toolbar' menu item");
    // Passing the `pinToToolbar` item to `closeChromeContextMenu()` will
    // activate it before closing the context menu.
    await closeChromeContextMenu(contextMenu.id, pinToToolbar);

    ok(
      unifiedButton.hasAttribute("attention"),
      "expected attention attribute on the UEB"
    );
    // We still expect the attention dot on the extension.
    ok(
      extensionWidget.hasAttribute("attention"),
      "expected attention attribute on the extension widget"
    );

    // Now let's open the unified extensions panel, and pin the same extension
    // to the toolbar, which should hide the attention dot on the UEB again.
    await openExtensionsPanel();
    contextMenu = await openUnifiedExtensionsContextMenu(extension.id);
    pinToToolbar = contextMenu.querySelector(
      ".unified-extensions-context-menu-pin-to-toolbar"
    );
    ok(pinToToolbar, "expected a 'Pin to Toolbar' menu item");
    const hidden = BrowserTestUtils.waitForEvent(
      gUnifiedExtensions.panel,
      "popuphidden",
      true
    );
    contextMenu.activateItem(pinToToolbar);
    await hidden;

    ok(
      !unifiedButton.hasAttribute("attention"),
      "expected no attention attribute on the UEB"
    );
    // We still expect the attention dot on the extension.
    ok(
      extensionWidget.hasAttribute("attention"),
      "expected attention attribute on the extension widget"
    );
  });

  await extension.unload();
});

async function testWithSubmenu(menu, nextItemClassName) {
  function expectMenuItems() {
    info("Checking expected menu items.");
    let [submenu, sep1, ocMessage, sep2, next] = menu.children;

    is(submenu.tagName, "menu", "First item is a submenu.");
    is(submenu.label, "submenu", "Submenu has the expected label.");
    is(sep1.tagName, "menuseparator", "Second item is a separator.");

    let l10n = menu.ownerDocument.l10n.getAttributes(ocMessage);
    is(ocMessage.tagName, "menuitem", "Third is origin controls message.");
    is(l10n.id, "origin-controls-no-access", "Expected l10n id.");

    is(sep2.tagName, "menuseparator", "Fourth item is a separator.");
    is(next.className, nextItemClassName, "All items accounted for.");
  }

  const [submenu] = menu.children;
  const popup = submenu.querySelector("menupopup");

  // Open and close the submenu repeatedly a few times.
  for (let i = 0; i < 3; i++) {
    expectMenuItems();

    const popupShown = BrowserTestUtils.waitForEvent(popup, "popupshown");
    submenu.openMenu(true);
    await popupShown;

    expectMenuItems();

    const popupHidden = BrowserTestUtils.waitForEvent(popup, "popuphidden");
    submenu.openMenu(false);
    await popupHidden;
  }

  menu.hidePopup();
}

add_task(async function test_originControls_with_submenus() {
  let extension = await makeExtension({
    id: "submenus@test",
    permissions: ["menus"],
  });

  await BrowserTestUtils.withNewTab("about:blank", async () => {
    info(`Testing with submenus.`);
    moveWidget(extension, true);
    let target = `#${CSS.escape(makeWidgetId(extension.id))}-BAP`;

    await testWithSubmenu(
      await openChromeContextMenu("toolbar-context-menu", target),
      "customize-context-manageExtension"
    );

    info(`Testing with submenus inside extensions panel.`);
    moveWidget(extension, false);
    await openExtensionsPanel();

    await testWithSubmenu(
      await openUnifiedExtensionsContextMenu(extension.id),
      "unified-extensions-context-menu-pin-to-toolbar"
    );

    await closeExtensionsPanel();
  });

  await extension.unload();
});
