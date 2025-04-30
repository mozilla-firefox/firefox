/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

/*
 * Test Permission Popup for Sideloaded Extensions.
 */
const { AddonTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/AddonTestUtils.sys.mjs"
);
const ADDON_ID = "addon1@test.mozilla.org";
const CUSTOM_THEME_ID = "theme1@test.mozilla.org";
const DEFAULT_THEME_ID = "default-theme@mozilla.org";

ChromeUtils.defineESModuleGetters(this, {
  PERMISSION_L10N: "resource://gre/modules/ExtensionPermissionMessages.sys.mjs",
  ExtensionPermissions: "resource://gre/modules/ExtensionPermissions.sys.mjs",
});

AddonTestUtils.initMochitest(this);

const LABEL_FOR_TECHNICAL_AND_INTERACTION_DATA_CHECKBOX =
  PERMISSION_L10N.formatMessagesSync([
    "popup-notification-addon-technicalAndInteraction-checkbox",
  ])[0].attributes.find(attr => attr.name === "label").value;

function assertDisabledSideloadedExtensionElement(managerWindow, addonElement) {
  const doc = addonElement.ownerDocument;
  const toggleDisabled = addonElement.querySelector(
    '[action="toggle-disabled"]'
  );
  is(
    doc.l10n.getAttributes(toggleDisabled).id,
    "extension-enable-addon-button-label",
    "Addon toggle-disabled action has the enable label"
  );
  ok(!toggleDisabled.checked, "toggle-disable isn't checked");
}

function assertEnabledSideloadedExtensionElement(managerWindow, addonElement) {
  const doc = addonElement.ownerDocument;
  const toggleDisabled = addonElement.querySelector(
    '[action="toggle-disabled"]'
  );
  is(
    doc.l10n.getAttributes(toggleDisabled).id,
    "extension-enable-addon-button-label",
    "Addon toggle-disabled action has the enable label"
  );
  ok(!toggleDisabled.checked, "toggle-disable isn't checked");
}

function clickEnableExtension(addonElement) {
  addonElement.querySelector('[action="toggle-disabled"]').click();
}

// Test for bug 1647931
// Install a theme, enable it and then enable the default theme again
add_task(async function test_theme_enable() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["xpinstall.signatures.required", false],
      ["extensions.autoDisableScopes", 15],
    ],
  });

  let theme = {
    manifest: {
      browser_specific_settings: { gecko: { id: CUSTOM_THEME_ID } },
      name: "Theme 1",
      theme: {
        colors: {
          frame: "#000000",
          tab_background_text: "#ffffff",
        },
      },
    },
  };

  let xpi = AddonTestUtils.createTempWebExtensionFile(theme);
  await AddonTestUtils.manuallyInstall(xpi);

  let changePromise = new Promise(resolve =>
    ExtensionsUI.once("change", resolve)
  );
  ExtensionsUI._checkForSideloaded();
  await changePromise;

  // enable fresh installed theme
  let manager = await open_manager("addons://list/theme");
  let customTheme = getAddonCard(manager, CUSTOM_THEME_ID);
  clickEnableExtension(customTheme);

  // enable default theme again
  let defaultTheme = getAddonCard(manager, DEFAULT_THEME_ID);
  clickEnableExtension(defaultTheme);

  let addon = await AddonManager.getAddonByID(CUSTOM_THEME_ID);
  await close_manager(manager);
  await addon.uninstall();
});

// Loading extension by sideloading method
add_task(async function test_sideloaded_extension_permissions_prompt() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["xpinstall.signatures.required", false],
      ["extensions.autoDisableScopes", 15],
    ],
  });

  let options = {
    manifest: {
      browser_specific_settings: { gecko: { id: ADDON_ID } },
      name: "Test 1",
      permissions: ["history", "https://*/*"],
      icons: { 64: "foo-icon.png" },
    },
  };

  let xpi = AddonTestUtils.createTempWebExtensionFile(options);
  await AddonTestUtils.manuallyInstall(xpi);

  let changePromise = new Promise(resolve =>
    ExtensionsUI.once("change", resolve)
  );
  ExtensionsUI._checkForSideloaded();
  await changePromise;

  // Test click event on permission cancel option.
  let manager = await open_manager("addons://list/extension");
  let addon = getAddonCard(manager, ADDON_ID);

  Assert.notEqual(addon, null, "Found sideloaded addon in about:addons");

  assertDisabledSideloadedExtensionElement(manager, addon);

  let popupPromise = promisePopupNotificationShown("addon-webext-permissions");
  clickEnableExtension(addon);
  let panel = await popupPromise;

  ok(PopupNotifications.isPanelOpen, "Permission popup should be visible");
  panel.secondaryButton.click();
  ok(
    !PopupNotifications.isPanelOpen,
    "Permission popup should be closed / closing"
  );

  addon = await AddonManager.getAddonByID(ADDON_ID);
  ok(
    !addon.seen,
    "Seen flag should remain false after permissions are refused"
  );

  // Test click event on permission accept option.
  addon = getAddonCard(manager, ADDON_ID);
  Assert.notEqual(addon, null, "Found sideloaded addon in about:addons");

  assertEnabledSideloadedExtensionElement(manager, addon);

  popupPromise = promisePopupNotificationShown("addon-webext-permissions");
  clickEnableExtension(addon);
  panel = await popupPromise;

  ok(PopupNotifications.isPanelOpen, "Permission popup should be visible");

  let notificationPromise = acceptAppMenuNotificationWhenShown(
    "addon-installed",
    ADDON_ID
  );

  panel.button.click();
  ok(
    !PopupNotifications.isPanelOpen,
    "Permission popup should be closed / closing"
  );
  await notificationPromise;

  addon = await AddonManager.getAddonByID(ADDON_ID);
  ok(addon.seen, "Seen flag should be true after permissions are accepted");

  ok(!PopupNotifications.isPanelOpen, "Permission popup should not be visible");

  await close_manager(manager);
  await addon.uninstall();
});

add_task(async function testInstallDialogShowsFullDomainsList() {
  const createTestExtensionXPI = ({
    id,
    domainsListLength = 0,
    permissions = [],
    incognito = "spanning",
  }) =>
    AddonTestUtils.createTempWebExtensionFile({
      manifest: {
        // Set the generated id as a name to make it easier to recognize the test case
        // from dialog screenshots (e.g. in the screenshot captured when the test hits
        // a failure).
        name: id,
        version: "1.0",
        browser_specific_settings: {
          gecko: { id },
        },
        incognito,
        permissions: permissions.concat(
          new Array(domainsListLength).fill("examplehost").map((v, i) => {
            return `*://${v}${i}.com/*`;
          })
        ),
      },
    });

  const LONG_DOMAIN_NAME = `averylongdomainname.${new Array(40)
    .fill("x")
    .join("")}.com`;

  const assertPermsElVisibility = popupContentEl => {
    Assert.equal(
      BrowserTestUtils.isVisible(popupContentEl.permsListEl),
      true,
      "Expect the permissions list element to be visible"
    );
  };

  const assertNoDomainsList = popupContentEl => {
    const domainsListEl = popupContentEl.querySelector(
      ".webext-perm-domains-list"
    );
    Assert.ok(!domainsListEl, "Expect no domain list element to be found");
  };

  const TEST_CASES = [
    {
      msg: "Test install extension with no host permissions",
      id: "no-domains",
      domainsListLength: 0,
      verifyDialog(popupContentEl, noIncognitoCheckbox) {
        assertNoDomainsList(popupContentEl);
        Assert.equal(
          BrowserTestUtils.isHidden(popupContentEl.permsListEl),
          noIncognitoCheckbox,
          `Expect the permissions list element to be ${
            noIncognitoCheckbox ? "hidden" : "visible"
          }`
        );
      },
    },
    {
      msg: "Test install extension with one domain listed in host permissions",
      id: "one-domain",
      domainsListLength: 1,
      verifyDialog(popupContentEl) {
        assertPermsElVisibility(popupContentEl);
        assertNoDomainsList(popupContentEl);
        const hostPermStringEl = popupContentEl.permsListEl.querySelector(
          "li.webext-perm-granted"
        );
        Assert.ok(
          hostPermStringEl,
          "Expect one granted permission string element"
        );
        Assert.equal(
          hostPermStringEl.textContent,
          PERMISSION_L10N.formatValueSync(
            "webext-perms-host-description-one-domain",
            {
              domain: "examplehost0.com",
            }
          ),
          "Got the expected host permission string on extension with only one granted domain"
        );
        Assert.ok(
          BrowserTestUtils.isVisible(hostPermStringEl),
          "Expect the host permission string to be visible"
        );
      },
    },
    {
      msg: "Test install extension with less than 6 domains listed in host permissions",
      id: "few-domains",
      domainsListLength: 5,
      verifyDialog(popupContentEl) {
        assertPermsElVisibility(popupContentEl);
        const domainsListEl = popupContentEl.permsListEl.querySelector(
          ".webext-perm-domains-list"
        );

        Assert.ok(
          domainsListEl,
          "Expect domains list element to be found inside the permission list element"
        );
        Assert.ok(
          BrowserTestUtils.isVisible(domainsListEl),
          "Expect the domains list element to be visible"
        );
        Assert.equal(
          domainsListEl.scrollTopMax,
          0,
          "Expect domains list to not be scrollable (chromeOnly scrollTopMax set to 0)"
        );
        // The permission string associated to XUL label element can be reached as labelEl.value.
        Assert.equal(
          domainsListEl.previousElementSibling.value,
          PERMISSION_L10N.formatValueSync(
            "webext-perms-host-description-multiple-domains",
            {
              domainCount: this.domainsListLength,
            }
          ),
          `Got the expected host permission string on extension with ${this.domainsListLength} granted domain`
        );
        Assert.deepEqual(
          Array.from(domainsListEl.querySelectorAll("li")).map(
            el => el.textContent
          ),
          new Array(this.domainsListLength)
            .fill("examplehost")
            .map((v, i) => `${v}${i}.com`),
          "Got the expected domains listed in the domains list element"
        );
      },
    },
    {
      msg: "Test install extension with many domains listed in host permissions",
      id: "many-domains",
      domainsListLength: 20,
      verifyDialog(popupContentEl) {
        assertPermsElVisibility(popupContentEl);
        const domainsListEl = popupContentEl.permsListEl.querySelector(
          ".webext-perm-domains-list"
        );

        Assert.ok(
          domainsListEl,
          "Expect domains list element to be found inside the permission list element"
        );
        Assert.ok(
          BrowserTestUtils.isVisible(domainsListEl),
          "Expect the domains list element to be visible"
        );
        Assert.greater(
          domainsListEl.scrollTopMax,
          domainsListEl.clientHeight,
          "Expect domains list to be scrollable (chromeOnly scrollTopMax greater than clientHeight)"
        );
        // The permission string associated to XUL label element can be reached as labelEl.value.
        Assert.equal(
          domainsListEl.previousElementSibling.value,
          PERMISSION_L10N.formatValueSync(
            "webext-perms-host-description-multiple-domains",
            {
              domainCount: this.domainsListLength,
            }
          ),
          `Got the expected host permission string on extension with ${this.domainsListLength} granted domain`
        );
        Assert.deepEqual(
          Array.from(domainsListEl.querySelectorAll("li")).map(
            el => el.textContent
          ),
          new Array(this.domainsListLength)
            .fill("examplehost")
            .map((v, i) => `${v}${i}.com`),
          "Got the expected domains listed in the domains list element"
        );
      },
    },
    {
      msg: "Test text wrapping on a single long domain name",
      id: "one-long-domain",
      domainsListLength: 0,
      permissions: [`*://${LONG_DOMAIN_NAME}/*`],
      verifyDialog(popupContentEl) {
        assertPermsElVisibility(popupContentEl);
        const hostPermStringEl = popupContentEl.permsListEl.querySelector(
          "li.webext-perm-granted"
        );
        Assert.equal(
          hostPermStringEl.childNodes[0].nodeType,
          hostPermStringEl.TEXT_NODE,
          "Expect to have host permission element child to be a text node"
        );
        Assert.equal(
          hostPermStringEl.childNodes[0].nodeValue,
          PERMISSION_L10N.formatValueSync(
            "webext-perms-host-description-one-domain",
            {
              domain: LONG_DOMAIN_NAME,
            }
          ),
          "Got the expected host permission string set as the nextNode value"
        );
        Assert.deepEqual(
          // Calling getBoxQuads on the text node is expected to be returning
          // one DOMQuad instance for each line the text node has been broken
          // into (e.g. 3 DOMQuad instances when the long domain name forces
          // the text node to be broken over 3 lines).
          //
          // This check is asserting that none of the lines the text node has
          // been broken into has a width larger than the width of the parent
          // element.
          //
          // NOTE: this assertion is expected to hit a failure if
          // .webext-perm-granted is missing the overflow-wrap CSS rule.
          hostPermStringEl.childNodes[0]
            .getBoxQuads()
            .map(quad => quad.getBounds().width)
            .filter(width => {
              return width > hostPermStringEl.getBoundingClientRect().width;
            }),
          [],
          "The host permission text node should NOT overflow the parent element"
        );
      },
    },
    {
      msg: "Test text wrapping on long domain name in domains list",
      id: "one-long-domain-in-domains-list",
      domainsListLength: 10,
      permissions: [`*://${LONG_DOMAIN_NAME}/*`],
      verifyDialog(popupContentEl) {
        assertPermsElVisibility(popupContentEl);
        const domainsListEl = popupContentEl.permsListEl.querySelector(
          ".webext-perm-domains-list"
        );
        Assert.ok(
          domainsListEl,
          "Expect domains list element to be found inside the permission list element"
        );
        Assert.ok(
          BrowserTestUtils.isVisible(domainsListEl),
          "Expect the domains list element to be visible"
        );
        Assert.equal(
          domainsListEl.firstElementChild.childNodes[0].nodeType,
          domainsListEl.TEXT_NODE,
          "Found text node for the long domain name item part of the domain list item"
        );
        Assert.equal(
          domainsListEl.firstElementChild.childNodes[0].nodeValue,
          LONG_DOMAIN_NAME,
          "Got the expected domain name set on the first domain list item"
        );
        Assert.deepEqual(
          // This check is asserting that none of the lines the text node has
          // been broken into has a width larger than the width of the parent
          // element.
          //
          // NOTE: this assertion is expected to hit a failure if .webext-perm-domains-list
          // list items elements are overflowing (e.g. if it is not inheriting the
          // overflow-wrap CSS rule from its ascending notes and doesn't have one set on
          // its own).
          domainsListEl.firstElementChild.childNodes[0]
            .getBoxQuads()
            .map(quad => quad.getBounds().width)
            .filter(width => {
              return (
                width >
                domainsListEl.firstElementChild.getBoundingClientRect().width
              );
            }),
          [],
          "The domain name text node should NOT overflow the parent element"
        );
      },
    },
    {
      msg: "Test wildcard subdomains shown as single host permission",
      id: "with-wildcard-subdomains",
      domainsListLength: 0,
      permissions: ["*://*.example.com/*", "*://example.com/*"],
      verifyDialog(popupContentEl) {
        assertPermsElVisibility(popupContentEl);
        const hostPermStringEl = popupContentEl.permsListEl.querySelector(
          "li.webext-perm-granted"
        );
        Assert.equal(
          hostPermStringEl.textContent,
          PERMISSION_L10N.formatValueSync(
            "webext-perms-host-description-one-domain",
            {
              domain: "example.com",
            }
          ),
          "Expected *.example.com and example.com host permissions to be reported as a single domain permission string"
        );
      },
    },
    {
      msg: "Test wildcard subdomains in domains list",
      id: "with-wildcard-subdomains",
      domainsListLength: 0,
      permissions: [
        "*://*.example.com/*",
        "*://example.com/*`",
        "*://*.example.org/*",
        "*://example.org/*",
      ],
      verifyDialog(popupContentEl) {
        assertPermsElVisibility(popupContentEl);
        const domainsListEl = popupContentEl.permsListEl.querySelector(
          ".webext-perm-domains-list"
        );
        Assert.ok(
          domainsListEl,
          "Expect domains list element to be found inside the permission list element"
        );
        Assert.ok(
          BrowserTestUtils.isVisible(domainsListEl),
          "Expect the domains list element to be visible"
        );
        // Expect the domains list to only include 2 domains and the host permissions string
        // to reflect that as well.
        Assert.deepEqual(
          Array.from(domainsListEl.querySelectorAll("li")).map(
            el => el.textContent
          ),
          ["example.com", "example.org"],
          "Got the expected domains listed in the domains list element"
        );
        Assert.equal(
          domainsListEl.previousElementSibling.value,
          PERMISSION_L10N.formatValueSync(
            "webext-perms-host-description-multiple-domains",
            {
              domainCount: 2,
            }
          ),
          "Got the expected host permission string on extension with 2 granted domain"
        );
      },
    },
  ];

  for (const testCase of TEST_CASES) {
    // Repeat each test without and without the private browsing checkbox
    // (to test the dialog when the host permissions is expected to be part of a list
    // or to be the only permissions listed in the dialog).
    for (const incognito of ["spanning", "not_allowed"]) {
      const noIncognitoCheckbox = incognito === "not_allowed";
      info(
        `${testCase.msg} ${
          noIncognitoCheckbox ? "and no other permissions" : ""
        }`
      );
      const xpi = createTestExtensionXPI({
        ...testCase,
        id: `${testCase.id}-with-incognito-${incognito}@test-ext`,
        incognito,
      });

      await BrowserTestUtils.withNewTab("about:blank", async () => {
        const dialogPromise = promisePopupNotificationShown(
          "addon-webext-permissions"
        );

        gURLBar.value = xpi.path;
        gURLBar.focus();
        EventUtils.synthesizeKey("KEY_Enter");
        const popupContentEl = await dialogPromise;

        testCase.verifyDialog(popupContentEl, noIncognitoCheckbox);

        let popupHiddenPromise = BrowserTestUtils.waitForEvent(
          window.PopupNotifications.panel,
          "popuphidden"
        );
        // hide the panel (this simulates the user dismissing it)
        popupContentEl.closest("panel").hidePopup();
        await popupHiddenPromise;
      });
    }
  }
});

add_task(async function testInstallDialogShowsDataCollectionPermissions() {
  await SpecialPowers.pushPrefEnv({
    set: [["extensions.dataCollectionPermissions.enabled", true]],
  });

  const createTestExtensionXPI = ({
    id,
    manifest_version,
    incognito = undefined,
    permissions = undefined,
    data_collection_permissions = undefined,
  }) => {
    return AddonTestUtils.createTempWebExtensionFile({
      manifest: {
        manifest_version,
        // Set the generated id as a name to make it easier to recognize the
        // test case from dialog screenshots (e.g. in the screenshot captured
        // when the test hits a failure).
        name: id,
        version: "1.0",
        incognito,
        permissions,
        browser_specific_settings: {
          gecko: {
            id,
            data_collection_permissions,
          },
        },
      },
    });
  };

  const assertHeaderWithPerms = (popupContentEl, extensionId) => {
    Assert.equal(
      popupContentEl.querySelector(".popup-notification-description")
        .textContent,
      PERMISSION_L10N.formatValueSync(
        "webext-perms-header-unsigned-with-perms",
        // We set the extension name to the extension id in `createTestExtensionXPI`.
        { extension: extensionId }
      ),
      "Expected header string with perms"
    );
  };

  const assertHeaderNoPerms = (popupContentEl, extensionId) => {
    Assert.equal(
      popupContentEl.querySelector(".popup-notification-description")
        .textContent,
      PERMISSION_L10N.formatValueSync(
        "webext-perms-header-unsigned",
        // We set the extension name to the extension id in `createTestExtensionXPI`.
        { extension: extensionId }
      ),
      "Expected header string without perms"
    );
  };

  const TEST_CASES = [
    {
      title: "With no data collection and incognito not allowed",
      incognito: "not_allowed",
      verifyDialog(popupContentEl, { extensionId }) {
        assertHeaderNoPerms(popupContentEl, extensionId);
        Assert.equal(
          popupContentEl.permsListEl.childElementCount,
          0,
          "Expected no permissions"
        );
        Assert.ok(
          !popupContentEl.hasAttribute("learnmoreurl"),
          "Expected no learn more link"
        );
      },
    },
    // Make sure we have the incognito checkbox when no other permissions are
    // specified.
    {
      title: "With no data collection",
      verifyDialog(popupContentEl, { extensionId }) {
        assertHeaderNoPerms(popupContentEl, extensionId);
        Assert.equal(
          popupContentEl.permsListEl.childElementCount,
          1,
          "Expected a single permission in the list"
        );
        Assert.ok(
          popupContentEl.permsListEl.querySelector(
            "li.webext-perm-privatebrowsing > checkbox"
          ),
          "Expected private browsing checkbox"
        );
        Assert.ok(
          popupContentEl.hasAttribute("learnmoreurl"),
          "Expected a learn more link"
        );
      },
    },
    {
      title: "With required data collection",
      data_collection_permissions: {
        required: ["locationInfo"],
      },
      verifyDialog(popupContentEl, { extensionId }) {
        assertHeaderNoPerms(popupContentEl, extensionId);
        Assert.equal(
          popupContentEl.permsListEl.childElementCount,
          2,
          "Expected two permission entries in the list"
        );
        Assert.ok(
          popupContentEl.permsListEl.querySelector(
            "li.webext-data-collection-perm-granted"
          ),
          "Expected data collection item"
        );
        Assert.equal(
          popupContentEl.permsListEl.firstChild.textContent,
          PERMISSION_L10N.formatValueSync(
            "webext-perms-description-data-some",
            {
              permissions: "location",
            }
          ),
          "Expected formatted data collection permission string"
        );
        Assert.ok(
          popupContentEl.permsListEl.querySelector(
            "li.webext-perm-privatebrowsing > checkbox"
          ),
          "Expected private browsing checkbox"
        );
        Assert.ok(
          popupContentEl.hasAttribute("learnmoreurl"),
          "Expected a learn more link"
        );
      },
    },
    {
      title: "With multiple required data collection",
      data_collection_permissions: {
        required: ["locationInfo", "financialAndPaymentInfo", "websiteContent"],
      },
      verifyDialog(popupContentEl, { extensionId }) {
        assertHeaderNoPerms(popupContentEl, extensionId);
        Assert.equal(
          popupContentEl.permsListEl.childElementCount,
          2,
          "Expected two permission entries in the list"
        );
        Assert.equal(
          popupContentEl.permsListEl.firstChild.textContent,
          PERMISSION_L10N.formatValueSync(
            "webext-perms-description-data-some",
            {
              // We pass the result of the `Intl.ListFormat` here to verify its
              // output.
              permissions:
                "location, financial and payment information, website content",
            }
          ),
          "Expected formatted data collection permission string"
        );
        Assert.ok(
          popupContentEl.permsListEl.querySelector(
            "li.webext-perm-privatebrowsing > checkbox"
          ),
          "Expected private browsing checkbox"
        );
        Assert.ok(
          popupContentEl.hasAttribute("learnmoreurl"),
          "Expected a learn more link"
        );
      },
    },
    {
      title: "With explicit no data collection",
      data_collection_permissions: {
        required: ["none"],
      },
      verifyDialog(popupContentEl, { extensionId }) {
        assertHeaderNoPerms(popupContentEl, extensionId);
        Assert.equal(
          popupContentEl.permsListEl.childElementCount,
          2,
          "Expected two permission entries in the list"
        );
        Assert.equal(
          popupContentEl.permsListEl.firstChild.textContent,
          PERMISSION_L10N.formatValueSync("webext-perms-description-data-none"),
          "Expected formatted data collection permission string"
        );
        Assert.ok(
          popupContentEl.permsListEl.querySelector(
            "li.webext-perm-privatebrowsing > checkbox"
          ),
          "Expected private browsing checkbox"
        );
        Assert.ok(
          popupContentEl.hasAttribute("learnmoreurl"),
          "Expected a learn more link"
        );
      },
    },
    // This test verifies that we omit "none" when other required data
    // collection permissions are also defined.
    {
      title: "With required data collection and 'none'",
      // This test extension requires "none" with another data collection permission,
      // and so it is expected to log a manifest warning.
      expectManifestWarnings: true,
      data_collection_permissions: {
        required: ["none", "locationInfo"],
      },
      verifyDialog(popupContentEl, { extensionId }) {
        assertHeaderNoPerms(popupContentEl, extensionId);
        Assert.equal(
          popupContentEl.permsListEl.childElementCount,
          2,
          "Expected two permission entries in the list"
        );
        Assert.equal(
          popupContentEl.permsListEl.firstChild.textContent,
          PERMISSION_L10N.formatValueSync(
            "webext-perms-description-data-some",
            {
              permissions: "location",
            }
          ),
          "Expected formatted data collection permission string"
        );
        Assert.ok(
          popupContentEl.permsListEl.querySelector(
            "li.webext-perm-privatebrowsing > checkbox"
          ),
          "Expected private browsing checkbox"
        );
        Assert.ok(
          popupContentEl.hasAttribute("learnmoreurl"),
          "Expected a learn more link"
        );
      },
    },
    {
      title: "With optional data collection",
      data_collection_permissions: {
        optional: ["locationInfo"],
      },
      verifyDialog(popupContentEl, { extensionId }) {
        assertHeaderNoPerms(popupContentEl, extensionId);
        Assert.equal(
          popupContentEl.permsListEl.childElementCount,
          1,
          "Expected a single permission in the list"
        );
        Assert.ok(
          popupContentEl.permsListEl.querySelector(
            "li.webext-perm-privatebrowsing > checkbox"
          ),
          "Expected private browsing checkbox"
        );
        Assert.ok(
          popupContentEl.hasAttribute("learnmoreurl"),
          "Expected a learn more link"
        );
      },
    },
    // This test case verifies that we show a checkbox for the
    // `technicalAndInteraction` optional data collection permission.
    {
      title: "With technical and interaction data",
      data_collection_permissions: {
        optional: ["technicalAndInteraction"],
      },
      verifyDialog(popupContentEl, { extensionId }) {
        assertHeaderNoPerms(popupContentEl, extensionId);
        Assert.equal(
          popupContentEl.permsListEl.childElementCount,
          2,
          "Expected two permission entries in the list"
        );
        let checkbox = popupContentEl.permsListEl.querySelector(
          "li.webext-data-collection-perm-optional > checkbox"
        );
        Assert.ok(checkbox, "Expected technical and interaction checkbox");
        Assert.ok(checkbox.checked, "Expected checkbox to be checked");
        Assert.equal(
          popupContentEl.permsListEl.firstChild.textContent,
          LABEL_FOR_TECHNICAL_AND_INTERACTION_DATA_CHECKBOX,
          "Expected formatted data collection permission string"
        );
        Assert.ok(
          popupContentEl.permsListEl.querySelector(
            "li.webext-perm-privatebrowsing > checkbox"
          ),
          "Expected private browsing checkbox"
        );
        Assert.ok(
          popupContentEl.hasAttribute("learnmoreurl"),
          "Expected a learn more link"
        );
      },
    },
    {
      title: "With technical and interaction data and no private browsing",
      incognito: "not_allowed",
      data_collection_permissions: {
        optional: ["technicalAndInteraction"],
      },
      verifyDialog(popupContentEl, { extensionId }) {
        assertHeaderNoPerms(popupContentEl, extensionId);
        Assert.equal(
          popupContentEl.permsListEl.childElementCount,
          1,
          "Expected a single permission entry in the list"
        );
        Assert.ok(
          popupContentEl.permsListEl.querySelector(
            "li.webext-data-collection-perm-optional > checkbox"
          ),
          "Expected technical and interaction checkbox"
        );
        Assert.equal(
          popupContentEl.permsListEl.textContent,
          LABEL_FOR_TECHNICAL_AND_INTERACTION_DATA_CHECKBOX,
          "Expected formatted data collection permission string"
        );
        Assert.ok(
          popupContentEl.hasAttribute("learnmoreurl"),
          "Expected a learn more link"
        );
      },
    },
    {
      title: "No required data collection and technical and interaction data",
      data_collection_permissions: {
        required: ["none"],
        optional: ["technicalAndInteraction"],
      },
      verifyDialog(popupContentEl, { extensionId }) {
        assertHeaderNoPerms(popupContentEl, extensionId);
        Assert.equal(
          popupContentEl.permsListEl.childElementCount,
          3,
          "Expected three permission entries in the list"
        );
        Assert.equal(
          popupContentEl.permsListEl.firstChild.textContent,
          PERMISSION_L10N.formatValueSync("webext-perms-description-data-none"),
          "Expected formatted data collection permission string"
        );
        const checkboxEl = popupContentEl.permsListEl.querySelector(
          "li.webext-data-collection-perm-optional > checkbox"
        );
        Assert.ok(checkboxEl, "Expected technical and interaction checkbox");
        Assert.equal(
          checkboxEl.parentNode.textContent,
          LABEL_FOR_TECHNICAL_AND_INTERACTION_DATA_CHECKBOX,
          "Expected formatted data collection permission string"
        );
        Assert.ok(
          popupContentEl.permsListEl.querySelector(
            "li.webext-perm-privatebrowsing > checkbox"
          ),
          "Expected private browsing checkbox"
        );
        Assert.ok(
          popupContentEl.hasAttribute("learnmoreurl"),
          "Expected a learn more link"
        );
      },
    },
    {
      title: "With required permission and data collection",
      permissions: ["bookmarks"],
      data_collection_permissions: {
        required: ["bookmarksInfo"],
      },
      verifyDialog(popupContentEl, { extensionId }) {
        assertHeaderWithPerms(popupContentEl, extensionId);
        Assert.equal(
          popupContentEl.permsListEl.childElementCount,
          3,
          "Expected three permission entries in the list"
        );
        Assert.ok(
          popupContentEl.permsListEl.firstChild.classList.contains(
            "webext-perm-granted"
          ),
          "Expected first entry to be the required API permission"
        );
        Assert.ok(
          popupContentEl.permsListEl.querySelector(
            "li.webext-data-collection-perm-granted"
          ),
          "Expected data collection item"
        );
        // Make sure the data collection permission is the second child.
        Assert.equal(
          popupContentEl.permsListEl.childNodes[1].textContent,
          PERMISSION_L10N.formatValueSync(
            "webext-perms-description-data-some",
            {
              permissions: "bookmarks",
            }
          ),
          "Expected formatted data collection permission string"
        );
        Assert.ok(
          popupContentEl.permsListEl.querySelector(
            "li.webext-perm-privatebrowsing > checkbox"
          ),
          "Expected private browsing checkbox"
        );
        Assert.ok(
          popupContentEl.hasAttribute("learnmoreurl"),
          "Expected a learn more link"
        );
      },
    },
    {
      title:
        "With required permission and data collection, and optional collection",
      permissions: ["bookmarks"],
      data_collection_permissions: {
        required: ["bookmarksInfo"],
        optional: ["technicalAndInteraction"],
      },
      verifyDialog(popupContentEl, { extensionId }) {
        assertHeaderWithPerms(popupContentEl, extensionId);
        Assert.equal(
          popupContentEl.permsListEl.childElementCount,
          4,
          "Expected four permission entries in the list"
        );
        Assert.ok(
          popupContentEl.permsListEl.firstChild.classList.contains(
            "webext-perm-granted"
          ),
          "Expected first entry to be the required API permission"
        );
        Assert.ok(
          popupContentEl.permsListEl.querySelector(
            "li.webext-data-collection-perm-granted"
          ),
          "Expected data collection item"
        );
        // Make sure the data collection permission is the second child.
        Assert.equal(
          popupContentEl.permsListEl.childNodes[1].textContent,
          PERMISSION_L10N.formatValueSync(
            "webext-perms-description-data-some",
            {
              permissions: "bookmarks",
            }
          ),
          "Expected formatted data collection permission string"
        );
        // Make sure the T&I checkbox is listed after the permissions.
        const checkboxEl =
          popupContentEl.permsListEl.childNodes[2].querySelector("checkbox");
        Assert.ok(checkboxEl, "Expected technical and interaction checkbox");
        Assert.equal(
          checkboxEl.parentNode.textContent,
          LABEL_FOR_TECHNICAL_AND_INTERACTION_DATA_CHECKBOX,
          "Expected formatted data collection permission string"
        );
        // Make sure the incognito checkbox is the last item.
        Assert.ok(
          popupContentEl.permsListEl.childNodes[3].querySelector(
            "li.webext-perm-privatebrowsing > checkbox"
          ),
          "Expected private browsing checkbox"
        );
        Assert.ok(
          popupContentEl.hasAttribute("learnmoreurl"),
          "Expected a learn more link"
        );
      },
    },
  ];

  for (const manifest_version of [2, 3]) {
    for (const { title, verifyDialog, ...testCase } of TEST_CASES) {
      info(`MV${manifest_version} - ${title}`);

      const extensionId = `@${title.toLowerCase().replaceAll(/[^\w]+/g, "-")}`;
      const xpi = createTestExtensionXPI({
        id: extensionId,
        manifest_version,
        ...testCase,
      });

      await BrowserTestUtils.withNewTab("about:blank", async () => {
        const dialogPromise = promisePopupNotificationShown(
          "addon-webext-permissions"
        );

        if (testCase.expectManifestWarnings) {
          await ExtensionTestUtils.failOnSchemaWarnings(false);
        }
        gURLBar.value = xpi.path;
        gURLBar.focus();
        EventUtils.synthesizeKey("KEY_Enter");
        const popupContentEl = await dialogPromise;

        verifyDialog(popupContentEl, { extensionId });

        let popupHiddenPromise = BrowserTestUtils.waitForEvent(
          window.PopupNotifications.panel,
          "popuphidden"
        );
        // Hide the panel (this simulates the user dismissing it).
        popupContentEl.closest("panel").hidePopup();
        await popupHiddenPromise;
        if (testCase.expectManifestWarnings) {
          ExtensionTestUtils.failOnSchemaWarnings(true);
        }
      });
    }
  }

  await SpecialPowers.popPrefEnv();
});

add_task(async function testTechnicalAndInteractionData() {
  await SpecialPowers.pushPrefEnv({
    set: [["extensions.dataCollectionPermissions.enabled", true]],
  });

  const extensionId = "@test-id";
  const extension = AddonTestUtils.createTempWebExtensionFile({
    manifest: {
      version: "1.0",
      browser_specific_settings: {
        gecko: {
          id: extensionId,
          data_collection_permissions: {
            optional: ["technicalAndInteraction"],
          },
        },
      },
    },
  });

  let perms = await ExtensionPermissions.get(extensionId);
  Assert.deepEqual(
    perms,
    { permissions: [], origins: [], data_collection: [] },
    "Expected no permissions"
  );

  await BrowserTestUtils.withNewTab("about:blank", async () => {
    const dialogPromise = promisePopupNotificationShown(
      "addon-webext-permissions"
    );

    gURLBar.value = extension.path;
    gURLBar.focus();
    EventUtils.synthesizeKey("KEY_Enter");
    const popupContentEl = await dialogPromise;

    // Install the add-on.
    let notificationPromise = acceptAppMenuNotificationWhenShown(
      "addon-installed",
      extensionId
    );
    popupContentEl.button.click();
    await notificationPromise;

    perms = await ExtensionPermissions.get(extensionId);
    Assert.deepEqual(
      perms,
      {
        permissions: [],
        origins: [],
        data_collection: ["technicalAndInteraction"],
      },
      "Expected data collection permission"
    );

    const addon = await AddonManager.getAddonByID(extensionId);
    Assert.ok(addon, "Expected add-on");
    await addon.uninstall();
  });

  // Repeat but uncheck the checkbox this time.
  await BrowserTestUtils.withNewTab("about:blank", async () => {
    const dialogPromise = promisePopupNotificationShown(
      "addon-webext-permissions"
    );

    gURLBar.value = extension.path;
    gURLBar.focus();
    EventUtils.synthesizeKey("KEY_Enter");
    const popupContentEl = await dialogPromise;

    const checkboxEl = popupContentEl.permsListEl.querySelector(
      "li.webext-data-collection-perm-optional > checkbox"
    );
    checkboxEl.click();

    // Install the add-on.
    let notificationPromise = acceptAppMenuNotificationWhenShown(
      "addon-installed",
      extensionId
    );
    popupContentEl.button.click();
    await notificationPromise;

    perms = await ExtensionPermissions.get(extensionId);
    Assert.deepEqual(
      perms,
      {
        permissions: [],
        origins: [],
        data_collection: [],
      },
      "Expected no data collection permission"
    );

    const addon = await AddonManager.getAddonByID(extensionId);
    Assert.ok(addon, "Expected add-on");
    await addon.uninstall();
  });

  await SpecialPowers.popPrefEnv();
});
