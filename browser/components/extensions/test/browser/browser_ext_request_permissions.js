"use strict";

ChromeUtils.defineESModuleGetters(this, {
  PERMISSION_L10N: "resource://gre/modules/ExtensionPermissionMessages.sys.mjs",
});

// This test case verifies that `permissions.request()` resolves in the
// expected order.
add_task(async function test_permissions_prompt() {
  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      optional_permissions: ["history", "bookmarks"],
    },
    background: async () => {
      let hiddenTab = await browser.tabs.create({
        url: browser.runtime.getURL("hidden.html"),
        active: false,
      });

      await browser.tabs.create({
        url: browser.runtime.getURL("active.html"),
        active: true,
      });

      browser.test.onMessage.addListener(async msg => {
        if (msg === "activate-hiddenTab") {
          await browser.tabs.update(hiddenTab.id, { active: true });

          browser.test.sendMessage("activate-hiddenTab-ok");
        }
      });
    },
    files: {
      "active.html": `<!DOCTYPE html><script src="active.js"></script>`,
      "active.js": async () => {
        browser.test.onMessage.addListener(async msg => {
          if (msg === "request-perms-activeTab") {
            let granted = await new Promise(resolve => {
              browser.test.withHandlingUserInput(() => {
                resolve(
                  browser.permissions.request({ permissions: ["history"] })
                );
              });
            });
            browser.test.assertTrue(granted, "permission request succeeded");

            browser.test.sendMessage("request-perms-activeTab-ok");
          }
        });

        browser.test.sendMessage("activeTab-ready");
      },
      "hidden.html": `<!DOCTYPE html><script src="hidden.js"></script>`,
      "hidden.js": async () => {
        let resolved = false;

        browser.test.onMessage.addListener(async msg => {
          if (msg === "request-perms-hiddenTab") {
            let granted = await new Promise(resolve => {
              browser.test.withHandlingUserInput(() => {
                resolve(
                  browser.permissions.request({ permissions: ["bookmarks"] })
                );
              });
            });
            browser.test.assertTrue(granted, "permission request succeeded");

            resolved = true;

            browser.test.sendMessage("request-perms-hiddenTab-ok");
          } else if (msg === "hiddenTab-read-state") {
            browser.test.sendMessage("hiddenTab-state-value", resolved);
          }
        });

        browser.test.sendMessage("hiddenTab-ready");
      },
    },
  });
  await extension.startup();

  await extension.awaitMessage("activeTab-ready");
  await extension.awaitMessage("hiddenTab-ready");

  // Call request() on a hidden window.
  extension.sendMessage("request-perms-hiddenTab");

  let requestPromptForActiveTab = promisePopupNotificationShown(
    "addon-webext-permissions"
  ).then(panel => {
    panel.button.click();
  });

  // Call request() in the current window.
  extension.sendMessage("request-perms-activeTab");
  await requestPromptForActiveTab;
  await extension.awaitMessage("request-perms-activeTab-ok");

  // Check that initial request() is still pending.
  extension.sendMessage("hiddenTab-read-state");
  ok(
    !(await extension.awaitMessage("hiddenTab-state-value")),
    "initial request is pending"
  );

  let requestPromptForHiddenTab = promisePopupNotificationShown(
    "addon-webext-permissions"
  ).then(panel => {
    panel.button.click();
  });

  extension.sendMessage("activate-hiddenTab");
  await extension.awaitMessage("activate-hiddenTab-ok");
  await requestPromptForHiddenTab;
  await extension.awaitMessage("request-perms-hiddenTab-ok");

  extension.sendMessage("hiddenTab-read-state");
  ok(
    await extension.awaitMessage("hiddenTab-state-value"),
    "initial request is resolved"
  );

  // The extension tabs are automatically closed upon unload.
  await extension.unload();
});

// NOTE: more tests covering the full domains list are part of the separate
// test case covering the full domains list when this dialog is being used
// as the addon install prompt (the test case part of the AOM mochitests
// and named testInstallDialogShowsFullDomainsList).
add_task(async function testOptionalPermissionsDialogShowsFullDomainsList() {
  const createTestExtension = ({
    id,
    domainsListLength = 0,
    optional_permissions = [],
  }) =>
    ExtensionTestUtils.loadExtension({
      manifest: {
        // Set the generated id as a name to make it easier to recognize the test case
        // from dialog screenshots (e.g. in the screenshot captured when the test hits
        // a failure).
        name: id,
        version: "1.0",
        browser_specific_settings: {
          gecko: { id },
        },
        optional_permissions: optional_permissions.concat(
          new Array(domainsListLength).fill("examplehost").map((v, i) => {
            return `*://${v}${i}.com/*`;
          })
        ),
      },
      files: {
        "extpage.html": `<!DOCTYPE html><script src="extpage.js"></script>`,
        "extpage.js"() {
          browser.test.onMessage.addListener(async msg => {
            if (msg !== "optional-origins:request") {
              browser.test.fail(`Got unexpected test message ${msg}`);
              return;
            }

            const { optional_permissions } = browser.runtime.getManifest();
            const permissions = optional_permissions.filter(
              p => !p.startsWith("*://")
            );
            const origins = optional_permissions.filter(p =>
              p.startsWith("*://")
            );
            browser.test.withHandlingUserInput(() => {
              browser.permissions.request({
                permissions,
                origins,
              });
              browser.test.sendMessage("optional-origins:requested");
            });
          });
          browser.test.sendMessage("extpage:loaded");
        },
      },
    });

  const assertNoDomainsList = popupContentEl => {
    const domainsListEl = popupContentEl.querySelector(
      ".webext-perm-domains-list"
    );
    Assert.ok(!domainsListEl, "Expect no domains list element to be found");
  };

  const assertOneDomainPermission = hostPermStringEl => {
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
  };

  const assertMultipleDomainsPermission = (
    domainsListEl,
    domainsListLength
  ) => {
    // The permission string associated to XUL label element can be reached as labelEl.value.
    Assert.equal(
      domainsListEl.previousElementSibling.value,
      PERMISSION_L10N.formatValueSync(
        "webext-perms-host-description-multiple-domains",
        {
          domainCount: domainsListLength,
        }
      ),
      `Got the expected host permission string on extension with ${this.domainsListLength} granted domain`
    );
    Assert.deepEqual(
      Array.from(domainsListEl.querySelectorAll("li")).map(
        el => el.textContent
      ),
      new Array(domainsListLength)
        .fill("examplehost")
        .map((v, i) => `${v}${i}.com`),
      "Got the expected domains listed in the domains list element"
    );
  };

  const TEST_CASES = [
    {
      msg: "Test request API permission and no origins",
      id: "api-and-no-domains@test-ext",
      optional_permissions: ["history"],
      domainsListLength: 0,
      verifyDialog(popupContentEl) {
        assertNoDomainsList(popupContentEl);
      },
    },
    {
      msg: "Test request access to a single domain",
      id: "single-domain@test-ext",
      optional_permissions: [],
      domainsListLength: 1,
      verifyDialog(popupContentEl) {
        assertNoDomainsList(popupContentEl);
        // This will fail if there are other unexpected permission strings
        // listed in the permissions list.
        assertOneDomainPermission(popupContentEl.permsListEl);
      },
    },
    {
      msg: "Test request API permission and access to a single domain",
      id: "api-and-single-domain@test-ext",
      optional_permissions: ["history"],
      domainsListLength: 1,
      verifyDialog(popupContentEl) {
        assertNoDomainsList(popupContentEl);
        assertOneDomainPermission(
          popupContentEl.permsListEl.querySelector("li:first-child")
        );
      },
    },
    {
      msg: "Test request access to multiple domains",
      id: "multiple-domains@test-ext",
      optional_permissions: [],
      domainsListLength: 10,
      verifyDialog(popupContentEl) {
        const domainsListEl = popupContentEl.permsListEl.querySelector(
          ".webext-perm-domains-list"
        );
        Assert.ok(domainsListEl, "Expect domains list element to be found");
        assertMultipleDomainsPermission(domainsListEl, this.domainsListLength);
      },
    },
    {
      msg: "Test request API permision and access to multiple domains",
      id: "api-and-multiple-domains@test-ext",
      optional_permissions: ["history"],
      domainsListLength: 10,
      verifyDialog(popupContentEl) {
        const domainsListEl = popupContentEl.permsListEl.querySelector(
          ".webext-perm-domains-list"
        );
        Assert.ok(domainsListEl, "Expect domains list element to be found");
        assertMultipleDomainsPermission(domainsListEl, this.domainsListLength);
      },
    },
  ];

  for (const testCase of TEST_CASES) {
    info(testCase.msg);
    const extension = createTestExtension(testCase);

    await extension.startup();

    let extPageURL = `moz-extension://${extension.uuid}/extpage.html`;

    await BrowserTestUtils.withNewTab(extPageURL, async () => {
      let promiseRequestDisalog = promisePopupNotificationShown(
        "addon-webext-permissions"
      );
      await extension.awaitMessage("extpage:loaded");
      extension.sendMessage("optional-origins:request");
      await extension.awaitMessage("optional-origins:requested");
      const popupContentEl = await promiseRequestDisalog;
      testCase.verifyDialog(popupContentEl);
    });

    await extension.unload();
  }
});

add_task(async function testOptionalPermissionsDialogWithDataCollection() {
  await SpecialPowers.pushPrefEnv({
    set: [["extensions.dataCollectionPermissions.enabled", true]],
  });

  const createTestExtension = ({
    id,
    optional_permissions = undefined,
    data_collection_permissions = undefined,
  }) => {
    return ExtensionTestUtils.loadExtension({
      manifest: {
        // Set the generated id as a name to make it easier to recognize the
        // test case from dialog screenshots (e.g. in the screenshot captured
        // when the test hits a failure).
        name: id,
        version: "1.0",
        optional_permissions,
        browser_specific_settings: {
          gecko: { id, data_collection_permissions },
        },
      },
      files: {
        "extpage.html": `<!DOCTYPE html><script src="extpage.js"></script>`,
        "extpage.js"() {
          browser.test.onMessage.addListener(async msg => {
            if (msg !== "request-perms") {
              browser.test.fail(`Got unexpected test message ${msg}`);
              return;
            }

            const {
              browser_specific_settings: {
                gecko: { data_collection_permissions },
              },
              optional_permissions,
            } = browser.runtime.getManifest();

            let perms = {
              data_collection: data_collection_permissions.optional,
            };
            if (optional_permissions.length) {
              perms.permissions = optional_permissions;
            }

            browser.test.withHandlingUserInput(() => {
              browser.permissions.request(perms);
              browser.test.sendMessage("perms-requested");
            });
          });

          browser.test.sendMessage("ready");
        },
      },
    });
  };

  const TEST_CASES = [
    {
      title: "With an optional data collection permission",
      data_collection_permissions: {
        optional: ["healthInfo"],
      },
      verifyDialog(popupContentEl, { extensionId }) {
        Assert.equal(
          popupContentEl.querySelector(".popup-notification-description")
            .textContent,
          PERMISSION_L10N.formatValueSync(
            "webext-perms-optional-data-collection-only-text",
            { extension: extensionId }
          ),
          "Expected header string without perms"
        );

        Assert.equal(
          popupContentEl.permsListEl.childElementCount,
          1,
          "Expected a single entry in the list"
        );
        Assert.equal(
          popupContentEl.permsListEl.textContent,
          PERMISSION_L10N.formatValueSync(
            "webext-perms-description-data-some-optional",
            {
              permissions: "health information",
            }
          ),
          "Expected formatted data collection permission string"
        );
        Assert.ok(
          popupContentEl.hasAttribute("learnmoreurl"),
          "Expected a learn more link"
        );
      },
    },
    {
      title: "With multiple optional data collection permissions",
      data_collection_permissions: {
        optional: ["healthInfo", "bookmarksInfo"],
      },
      verifyDialog(popupContentEl, { extensionId }) {
        Assert.equal(
          popupContentEl.querySelector(".popup-notification-description")
            .textContent,
          PERMISSION_L10N.formatValueSync(
            "webext-perms-optional-data-collection-only-text",
            { extension: extensionId }
          ),
          "Expected header string without perms"
        );

        Assert.equal(
          popupContentEl.permsListEl.childElementCount,
          1,
          "Expected a single entry in the list"
        );
        Assert.equal(
          popupContentEl.permsListEl.textContent,
          PERMISSION_L10N.formatValueSync(
            "webext-perms-description-data-some-optional",
            {
              permissions: "health information, bookmarks",
            }
          ),
          "Expected formatted data collection permission string"
        );
        Assert.ok(
          popupContentEl.hasAttribute("learnmoreurl"),
          "Expected a learn more link"
        );
      },
    },
    {
      title: "With technical and interaction data",
      data_collection_permissions: {
        optional: ["technicalAndInteraction"],
      },
      verifyDialog(popupContentEl, { extensionId }) {
        Assert.equal(
          popupContentEl.querySelector(".popup-notification-description")
            .textContent,
          PERMISSION_L10N.formatValueSync(
            "webext-perms-optional-data-collection-only-text",
            { extension: extensionId }
          ),
          "Expected header string without perms"
        );

        Assert.equal(
          popupContentEl.permsListEl.childElementCount,
          1,
          "Expected a single entry in the list"
        );
        Assert.equal(
          popupContentEl.permsListEl.textContent,
          PERMISSION_L10N.formatValueSync(
            "webext-perms-description-data-some-optional",
            {
              permissions: "technical and interaction data",
            }
          ),
          "Expected formatted data collection permission string"
        );
        Assert.ok(
          popupContentEl.hasAttribute("learnmoreurl"),
          "Expected a learn more link"
        );
      },
    },
    {
      title: "With optional API and data collection permissions",
      optional_permissions: ["bookmarks"],
      data_collection_permissions: {
        optional: ["bookmarksInfo"],
      },
      verifyDialog(popupContentEl, { extensionId }) {
        Assert.equal(
          popupContentEl.querySelector(".popup-notification-description")
            .textContent,
          PERMISSION_L10N.formatValueSync(
            "webext-perms-optional-data-collection-text",
            { extension: extensionId }
          ),
          "Expected header string with perms"
        );

        Assert.equal(
          popupContentEl.permsListEl.childElementCount,
          2,
          "Expected two entries in the list"
        );
        Assert.equal(
          popupContentEl.permsListEl.firstChild.textContent,
          PERMISSION_L10N.formatValueSync("webext-perms-description-bookmarks"),
          "Expected formatted data collection permission string"
        );
        Assert.equal(
          popupContentEl.permsListEl.lastChild.textContent,
          PERMISSION_L10N.formatValueSync(
            "webext-perms-description-data-some-optional",
            {
              permissions: "bookmarks",
            }
          ),
          "Expected formatted data collection permission string"
        );
        Assert.ok(
          popupContentEl.hasAttribute("learnmoreurl"),
          "Expected a learn more link"
        );
      },
    },
    {
      title: "With non-promptable API and optional data collection permission",
      optional_permissions: ["webRequest"],
      data_collection_permissions: {
        optional: ["healthInfo"],
      },
      verifyDialog(popupContentEl, { extensionId }) {
        Assert.equal(
          popupContentEl.querySelector(".popup-notification-description")
            .textContent,
          PERMISSION_L10N.formatValueSync(
            "webext-perms-optional-data-collection-only-text",
            { extension: extensionId }
          ),
          "Expected header string without perms"
        );

        // We expect a single entry because `webRequest` is non-promptable.
        Assert.equal(
          popupContentEl.permsListEl.childElementCount,
          1,
          "Expected a single entry in the list"
        );
        Assert.equal(
          popupContentEl.permsListEl.textContent,
          PERMISSION_L10N.formatValueSync(
            "webext-perms-description-data-some-optional",
            {
              permissions: "health information",
            }
          ),
          "Expected formatted data collection permission string"
        );
        Assert.ok(
          popupContentEl.hasAttribute("learnmoreurl"),
          "Expected a learn more link"
        );
      },
    },
  ];

  for (const {
    title,
    optional_permissions,
    data_collection_permissions,
    verifyDialog,
  } of TEST_CASES) {
    info(title);

    const extensionId = `@${title.toLowerCase().replaceAll(/[^\w]+/g, "-")}`;
    const extension = createTestExtension({
      id: extensionId,
      optional_permissions,
      data_collection_permissions,
    });
    await extension.startup();

    let extPageURL = `moz-extension://${extension.uuid}/extpage.html`;
    await BrowserTestUtils.withNewTab(extPageURL, async () => {
      let promiseRequestDisalog = promisePopupNotificationShown(
        "addon-webext-permissions"
      );
      await extension.awaitMessage("ready");
      extension.sendMessage("request-perms");
      await extension.awaitMessage("perms-requested");
      const popupContentEl = await promiseRequestDisalog;
      verifyDialog(popupContentEl, { extensionId });
    });

    await extension.unload();
  }

  await SpecialPowers.popPrefEnv();
});

add_task(
  async function testOptionalPermissionsDialogWithDataCollectionAlreadyGranted() {
    await SpecialPowers.pushPrefEnv({
      set: [["extensions.dataCollectionPermissions.enabled", true]],
    });

    const extension = ExtensionTestUtils.loadExtension({
      manifest: {
        version: "1.0",
        browser_specific_settings: {
          gecko: {
            data_collection_permissions: {
              optional: ["healthInfo"],
            },
          },
        },
      },
      files: {
        "extpage.html": `<!DOCTYPE html><script src="extpage.js"></script>`,
        "extpage.js"() {
          browser.test.onMessage.addListener(async msg => {
            if (msg !== "request-perms") {
              browser.test.fail(`Got unexpected test message ${msg}`);
              return;
            }

            browser.test.withHandlingUserInput(async () => {
              await browser.permissions.request({
                data_collection: ["healthInfo"],
              });
              browser.test.sendMessage("perms-requested");
            });
          });

          browser.test.sendMessage("ready");
        },
      },
    });
    await extension.startup();

    let extPageURL = `moz-extension://${extension.uuid}/extpage.html`;
    await BrowserTestUtils.withNewTab(extPageURL, async () => {
      let promiseRequestDisalog = promisePopupNotificationShown(
        "addon-webext-permissions"
      ).then(panel => {
        // Grant the permission.
        panel.button.click();
      });

      await extension.awaitMessage("ready");
      extension.sendMessage("request-perms");
      await extension.awaitMessage("perms-requested");
      await promiseRequestDisalog;

      extension.sendMessage("request-perms");
      await extension.awaitMessage("perms-requested");
    });

    await extension.unload();
    await SpecialPowers.popPrefEnv();
  }
);
