"use strict";

const { ExtensionPermissions } = ChromeUtils.importESModule(
  "resource://gre/modules/ExtensionPermissions.sys.mjs"
);

AddonTestUtils.init(this);
AddonTestUtils.overrideCertDB();
AddonTestUtils.createAppInfo(
  "xpcshell@tests.mozilla.org",
  "XPCShell",
  "1",
  "42"
);

// We intercept the "webextension-optional-permission-prompt" notification in
// optionalPermissionsPromptHandler.
//
// On desktop, this works because this is a xpcshell test environment where the
// usual browser UI is not loaded.
// On Android, ExtensionPromptObserver in GeckoViewWebExtension.sys.mjs handles
// this but due to this being a xpcshell test without native part, the handler
// rejects async with "No listener for GeckoView:WebExtension:OptionalPrompt".
// The optionalPermissionsPromptHandler is synchronous and will therefore
// always beat the ExtensionPromptObserver if any. Ignore the error:
PromiseTestUtils.allowMatchingRejectionsGlobally(
  /No listener for GeckoView:WebExtension:OptionalPrompt/
);

add_setup(async () => {
  // Bug 1646182: Force ExtensionPermissions to run in rkv mode, the legacy
  // storage mode will run in xpcshell-legacy-ep.toml
  await ExtensionPermissions._uninit();

  optionalPermissionsPromptHandler.init();

  await AddonTestUtils.promiseStartupManager();
  AddonTestUtils.usePrivilegedSignatures = false;

  // "userScripts" can only be in optional_permissions when supported:
  Services.prefs.setBoolPref("extensions.userScripts.mv3.enabled", true);
});

// Test that an optional-only permission in the "permissions" array is not
// accepted. Test in MV2 and MV3 to maximize coverage across all manifest
// versions, since the implementation has subtle differences.
async function test_optional_only_permission_in_permissions(manifest_version) {
  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      manifest_version,
      permissions: ["trialML", "webNavigation"],
    },
    background() {
      browser.test.onMessage.addListener(async msg => {
        if (msg === "check_permissions") {
          browser.test.assertEq(
            await browser.permissions.contains({ permissions: ["trialML"] }),
            false,
            "Optional-only permission not granted when in permissions[]."
          );
          browser.test.assertTrue(
            browser.webNavigation,
            "Other permission unaffected by refusal of optional-only permission"
          );
          browser.test.sendMessage("check_permissions:done");
          return;
        }
        if (msg === "request") {
          await browser.test.assertRejects(
            browser.permissions.request({ permissions: ["trialML"] }),
            "Cannot request permission trialML since it was not declared in optional_permissions",
            "Cannot request permission not listed in optional_permissions"
          );
          browser.test.sendMessage("request:done");
          return;
        }
        browser.test.fail(`Unexpected message: ${msg}`);
      });
    },
  });

  let { messages } = await promiseConsoleOutput(async () => {
    ExtensionTestUtils.failOnSchemaWarnings(false);
    await extension.startup();
    ExtensionTestUtils.failOnSchemaWarnings(true);
  });

  extension.sendMessage("check_permissions");
  await extension.awaitMessage("check_permissions:done");

  await withHandlingUserInput(extension, async () => {
    extension.sendMessage("request");
    await extension.awaitMessage("request:done");
    ok(!optionalPermissionsPromptHandler.sawPrompt, "Should not be prompted");
  });

  Assert.ok(
    !extension.extension.hasPermission("trialML"),
    "extension.hasPermission() of optional-only permission is false"
  );

  await extension.unload();

  // Note: the permission being rejected at parse time should be sufficient to
  // prevent the permission from appearing in extension UI. For additional test
  // coverage, the test_userScripts_cannot_be_install_time_permission test in
  // browser_permission_prompt_userScripts.js confirms that the "userScripts"
  // OptionalOnlyPermission is really hidden from install prompts.
  AddonTestUtils.checkMessages(messages, {
    expected: [
      {
        message:
          /Reading manifest: Warning processing permissions: Error processing permissions.0: Value "trialML" must either:/,
      },
    ],
  });
}

add_task(async function test_optional_only_permission_in_permissions_mv2() {
  await test_optional_only_permission_in_permissions(2);
});

add_task(async function test_optional_only_permission_in_permissions_mv3() {
  await test_optional_only_permission_in_permissions(3);
});

// Test that optional-only permissions can be in the "optional_permissions"
// array in the manifest, and that it can be granted and revoked as usual.
async function test_optional_only_in_optional_permissions(manifest_version) {
  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      manifest_version,
      optional_permissions: ["trialML"],
    },
    background() {
      browser.runtime.onInstalled.addListener(async () => {
        browser.test.assertEq(
          await browser.permissions.contains({ permissions: ["trialML"] }),
          false,
          "Optional-only permission not granted at first."
        );
        browser.test.sendMessage("installed");
      });
      browser.test.onMessage.addListener(async msg => {
        browser.test.assertEq("request", msg);
        let granted = await browser.permissions.request({
          permissions: ["trialML"],
        });
        browser.test.sendMessage("request:result", granted);
      });
    },
  });

  await extension.startup();

  await extension.awaitMessage("installed");

  info("Testing permissions.request() and cancel");
  optionalPermissionsPromptHandler.acceptPrompt = false;
  await withHandlingUserInput(extension, async () => {
    extension.sendMessage("request");
    let granted = await extension.awaitMessage("request:result");
    ok(optionalPermissionsPromptHandler.sawPrompt, "Got prompt");
    equal(granted, false, "Not granted when canceled");
  });

  info("Testing permissions.request() and accept");
  optionalPermissionsPromptHandler.acceptPrompt = true;
  await withHandlingUserInput(extension, async () => {
    extension.sendMessage("request");
    let granted = await extension.awaitMessage("request:result");
    ok(optionalPermissionsPromptHandler.sawPrompt, "Got prompt");
    equal(granted, true, "Granted when approved");
  });

  await extension.unload();
}

add_task(async function test_optional_only_in_optional_permissions_mv2() {
  await test_optional_only_in_optional_permissions(2);
});

add_task(async function test_optional_only_in_optional_permissions_mv3() {
  await test_optional_only_in_optional_permissions(3);
});

// By design, when an optional-only permission is specified, it cannot be
// requested together with other permissions. This test verifies that behavior.
add_task(
  {
    pref_set: [
      // Here we do not care about the prompt, only about the validation.
      ["extensions.webextOptionalPermissionPrompts", false],
      [
        // This pref controls the Cu.isInAutomation flag that is needed to use
        // browser.test.withHandlingUserInput in xpcshell tests (bug 1598804):
        "security.turn_off_all_security_so_that_viruses_can_take_over_this_computer",
        true,
      ],
    ],
  },
  async function at_most_one_optional_only_permission_in_request() {
    let extension = ExtensionTestUtils.loadExtension({
      manifest: {
        manifest_version: 3,
        optional_permissions: [
          // Note: cookies permission does not have a permission warning.
          "cookies",
          // Optional-only permission:
          "trialML",
          // Optional-only permission:
          "userScripts",
          // Regular permission with warning:
          "webNavigation",
        ],
        host_permissions: [
          // Host permission with warning:
          "https://example.com/*",
        ],
      },
      async background() {
        const ERROR_ONLY_ONE_PERM_ALLOWED =
          "Cannot request permission trialML with another permission";

        async function testPermissionsRequest(permissions) {
          return new Promise(resolve => {
            browser.test.withHandlingUserInput(() =>
              resolve(browser.permissions.request(permissions))
            );
          });
        }

        await browser.test.assertRejects(
          testPermissionsRequest({ permissions: ["webNavigation", "trialML"] }),
          ERROR_ONLY_ONE_PERM_ALLOWED,
          "Should reject regular permission + optional-only permission"
        );

        await browser.test.assertRejects(
          testPermissionsRequest({ permissions: ["trialML", "cookies"] }),
          ERROR_ONLY_ONE_PERM_ALLOWED,
          "Should reject optional-only permission + permission without warning"
        );

        await browser.test.assertRejects(
          testPermissionsRequest({ permissions: ["trialML", "webNavigation"] }),
          ERROR_ONLY_ONE_PERM_ALLOWED,
          "Should reject optional-only permissions + regular permission"
        );

        await browser.test.assertRejects(
          testPermissionsRequest({ permissions: ["trialML", "userScripts"] }),
          ERROR_ONLY_ONE_PERM_ALLOWED,
          "Should reject optional-only permissions (trialML, userScripts)"
        );

        // The UI logic (isUserScriptsRequest in ExtensionsUI.sys.mjs) assumes
        // that "userScripts" is the only permission if present in a permission
        // request. That is the case because "userScripts" is an
        // OptionalOnlyPermission, which the following check confirms:
        await browser.test.assertRejects(
          testPermissionsRequest({ permissions: ["userScripts", "trialML"] }),
          // Note: this is comparable to the ["trialML", "userScripts"] test
          // above, but the error message differs because the message includes
          // the first optional-only permission, "userScripts" in this case.
          "Cannot request permission userScripts with another permission",
          "Should reject optional-only permissions (userScripts, trialML)"
        );

        await browser.test.assertRejects(
          testPermissionsRequest({
            permissions: ["trialML"],
            origins: ["https://example.com/*"],
          }),
          ERROR_ONLY_ONE_PERM_ALLOWED,
          "Should reject optional-only permission + host permission"
        );

        await browser.test.assertRejects(
          // In theory we could detect that the same permission is listed twice
          // and recognize that it will only generate one permission warning.
          // Our implementation does not deduplicate, and there is no reason
          // for supporting it, so we just reject it.
          testPermissionsRequest({
            permissions: ["trialML", "trialML"],
            origins: ["https://example.com/*"],
          }),
          ERROR_ONLY_ONE_PERM_ALLOWED,
          "Should reject optional-only permission if it is listed twice"
        );

        browser.test.sendMessage("done");
      },
    });

    await extension.startup();
    await extension.awaitMessage("done");
    await extension.unload();
  }
);

add_task(
  {
    pref_set: [
      // Here we do not care about the prompt, only about the validation.
      ["extensions.webextOptionalPermissionPrompts", false],
      [
        // This pref controls the Cu.isInAutomation flag that is needed to use
        // browser.test.withHandlingUserInput in xpcshell tests (bug 1598804):
        "security.turn_off_all_security_so_that_viruses_can_take_over_this_computer",
        true,
      ],
      ["extensions.dataCollectionPermissions.enabled", true],
    ],
  },
  async function at_most_one_optional_only_permission_in_request_with_data_collection() {
    let extension = ExtensionTestUtils.loadExtension({
      manifest: {
        manifest_version: 3,
        optional_permissions: [
          // Optional-only permission:
          "trialML",
          // Optional-only permission:
          "userScripts",
        ],
        browser_specific_settings: {
          gecko: {
            data_collection_permissions: {
              optional: ["technicalAndInteraction"],
            },
          },
        },
      },
      async background() {
        async function testPermissionsRequest(permissions) {
          return new Promise(resolve => {
            browser.test.withHandlingUserInput(() =>
              resolve(browser.permissions.request(permissions))
            );
          });
        }

        await browser.test.assertRejects(
          testPermissionsRequest({
            permissions: ["trialML"],
            data_collection: ["technicalAndInteraction"],
          }),
          "Cannot request permission trialML with another permission",
          "Should reject data collection permission + optional-only permission"
        );

        await browser.test.assertRejects(
          testPermissionsRequest({
            permissions: ["userScripts"],
            data_collection: ["technicalAndInteraction"],
          }),
          "Cannot request permission userScripts with another permission",
          "Should reject data collection permission + optional-only permission"
        );

        browser.test.sendMessage("done");
      },
    });

    await extension.startup();
    await extension.awaitMessage("done");
    await extension.unload();
  }
);
