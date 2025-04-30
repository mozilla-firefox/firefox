"use strict";

const { AddonManager } = ChromeUtils.importESModule(
  "resource://gre/modules/AddonManager.sys.mjs"
);
const { ExtensionPermissions } = ChromeUtils.importESModule(
  "resource://gre/modules/ExtensionPermissions.sys.mjs"
);

ChromeUtils.defineESModuleGetters(this, {
  ExtensionParent: "resource://gre/modules/ExtensionParent.sys.mjs",
});

AddonTestUtils.init(this);
AddonTestUtils.overrideCertDB();
AddonTestUtils.createAppInfo(
  "xpcshell@tests.mozilla.org",
  "XPCShell",
  "1",
  "42"
);

// TODO: Bug 1960273 - Update this test and remove this pref set when we enable
// the data collection permissions on all channels.
Services.prefs.setBoolPref(
  "extensions.dataCollectionPermissions.enabled",
  false
);

let OptionalPermissions;

add_setup(async () => {
  // FOG needs a profile and to be initialized.
  do_get_profile();
  Services.fog.initializeFOG();
  Services.fog.testResetFOG();

  // Bug 1646182: Force ExtensionPermissions to run in rkv mode, the legacy
  // storage mode will run in xpcshell-legacy-ep.toml
  await ExtensionPermissions._uninit();

  Services.prefs.setBoolPref(
    "extensions.webextOptionalPermissionPrompts",
    false
  );
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("extensions.webextOptionalPermissionPrompts");
  });
  await AddonTestUtils.promiseStartupManager();
  AddonTestUtils.usePrivilegedSignatures = false;

  // We want to get a list of optional permissions prior to loading an extension,
  // so we'll get ExtensionParent to do that for us.
  await ExtensionParent.apiManager.lazyInit();

  // These permissions have special behaviors and/or are not mapped directly to an
  // api namespace.  They will have their own tests for specific behavior.
  let ignore = [
    "activeTab",
    "clipboardRead",
    "clipboardWrite",
    "declarativeNetRequestFeedback",
    "devtools",
    "downloads.open",
    "geolocation",
    "management",
    "menus.overrideContext",
    "nativeMessaging",
    "scripting",
    "search",
    "tabHide",
    "tabs",
    "trialML",
    "webRequestAuthProvider",
    "webRequestBlocking",
    "webRequestFilterResponse",
    "webRequestFilterResponse.serviceWorkerScript",
  ];
  // "OptionalOnlyPermission" is not included in the list below, because a test
  // below tries to request all permissions at once. That is not supported when
  // optional-only. See test_ext_permissions_optional_only.js instead.
  OptionalPermissions = Schemas.getPermissionNames([
    "OptionalPermission",
    "OptionalPermissionNoPrompt",
  ]).filter(n => !ignore.includes(n));
});

add_task(async function test_api_on_permissions_changed() {
  async function background() {
    let manifest = browser.runtime.getManifest();
    let permObj = { permissions: manifest.optional_permissions, origins: [] };

    function verifyPermissions(enabled) {
      for (let perm of manifest.optional_permissions) {
        browser.test.assertEq(
          enabled,
          !!browser[perm],
          `${perm} API is ${
            enabled ? "injected" : "removed"
          } after permission request`
        );
      }
    }

    browser.permissions.onAdded.addListener(details => {
      browser.test.assertEq(
        JSON.stringify(details.permissions),
        JSON.stringify(manifest.optional_permissions),
        "expected permissions added"
      );
      verifyPermissions(true);
      browser.test.sendMessage("added");
    });

    browser.permissions.onRemoved.addListener(details => {
      browser.test.assertEq(
        JSON.stringify(details.permissions),
        JSON.stringify(manifest.optional_permissions),
        "expected permissions removed"
      );
      verifyPermissions(false);
      browser.test.sendMessage("removed");
    });

    browser.test.onMessage.addListener((msg, enabled) => {
      if (msg === "request") {
        browser.permissions.request(permObj);
      } else if (msg === "verify_access") {
        verifyPermissions(enabled);
        browser.test.sendMessage("verified");
      } else if (msg === "revoke") {
        browser.permissions.remove(permObj);
      }
    });
  }

  let extension = ExtensionTestUtils.loadExtension({
    background,
    manifest: {
      optional_permissions: OptionalPermissions,
    },
    useAddonManager: "permanent",
  });
  await extension.startup();

  function addPermissions() {
    extension.sendMessage("request");
    return extension.awaitMessage("added");
  }

  function removePermissions() {
    extension.sendMessage("revoke");
    return extension.awaitMessage("removed");
  }

  function verifyPermissions(enabled) {
    extension.sendMessage("verify_access", enabled);
    return extension.awaitMessage("verified");
  }

  await withHandlingUserInput(extension, async () => {
    await addPermissions();
    await removePermissions();
    await addPermissions();
  });

  // reset handlingUserInput for the restart
  extensionHandlers.delete(extension);

  // Verify access on restart
  await AddonTestUtils.promiseRestartManager();
  await extension.awaitBackgroundStarted();
  await verifyPermissions(true);

  await withHandlingUserInput(extension, async () => {
    await removePermissions();
  });

  // Add private browsing to be sure it doesn't come through.
  let permObj = {
    permissions: OptionalPermissions.concat("internal:privateBrowsingAllowed"),
    origins: [],
  };

  // enable the permissions while the addon is running
  await ExtensionPermissions.add(extension.id, permObj, extension.extension);
  await extension.awaitMessage("added");
  await verifyPermissions(true);

  // disable the permissions while the addon is running
  await ExtensionPermissions.remove(extension.id, permObj, extension.extension);
  await extension.awaitMessage("removed");
  await verifyPermissions(false);

  // Add private browsing to test internal permission.  If it slips through,
  // we would get an error for an additional added message.
  await ExtensionPermissions.add(
    extension.id,
    { permissions: ["internal:privateBrowsingAllowed"], origins: [] },
    extension.extension
  );

  // disable the addon and re-test revoking permissions.
  await withHandlingUserInput(extension, async () => {
    await addPermissions();
  });
  let addon = await AddonManager.getAddonByID(extension.id);
  await addon.disable();
  await ExtensionPermissions.remove(extension.id, permObj);
  await addon.enable();
  await extension.awaitStartup();

  await verifyPermissions(false);
  let perms = await ExtensionPermissions.get(extension.id);
  equal(perms.permissions.length, 0, "no permissions on startup");

  await extension.unload();
});

add_task(async function test_geo_permissions() {
  async function background() {
    const permObj = { permissions: ["geolocation"] };
    browser.test.onMessage.addListener(async msg => {
      if (msg === "request") {
        await browser.permissions.request(permObj);
      } else if (msg === "remove") {
        await browser.permissions.remove(permObj);
      }
      let result = await browser.permissions.contains(permObj);
      browser.test.sendMessage("done", result);
    });
  }

  let extension = ExtensionTestUtils.loadExtension({
    background,
    manifest: {
      browser_specific_settings: { gecko: { id: "geo-test@test" } },
      optional_permissions: ["geolocation"],
    },
    useAddonManager: "permanent",
  });
  await extension.startup();

  let policy = WebExtensionPolicy.getByID(extension.id);
  let principal = policy.extension.principal;
  equal(
    Services.perms.testPermissionFromPrincipal(principal, "geo"),
    Services.perms.UNKNOWN_ACTION,
    "geolocation not allowed on install"
  );

  await withHandlingUserInput(extension, async () => {
    extension.sendMessage("request");
    ok(await extension.awaitMessage("done"), "permission granted");
    equal(
      Services.perms.testPermissionFromPrincipal(principal, "geo"),
      Services.perms.ALLOW_ACTION,
      "geolocation allowed after requested"
    );

    extension.sendMessage("remove");
    ok(!(await extension.awaitMessage("done")), "permission revoked");

    equal(
      Services.perms.testPermissionFromPrincipal(principal, "geo"),
      Services.perms.UNKNOWN_ACTION,
      "geolocation not allowed after removed"
    );

    // re-grant to test update removal
    extension.sendMessage("request");
    ok(await extension.awaitMessage("done"), "permission granted");
    equal(
      Services.perms.testPermissionFromPrincipal(principal, "geo"),
      Services.perms.ALLOW_ACTION,
      "geolocation allowed after re-requested"
    );
  });

  // We should not have geo permission after this upgrade.
  await extension.upgrade({
    manifest: {
      browser_specific_settings: { gecko: { id: "geo-test@test" } },
    },
    useAddonManager: "permanent",
  });

  equal(
    Services.perms.testPermissionFromPrincipal(principal, "geo"),
    Services.perms.UNKNOWN_ACTION,
    "geolocation not allowed after upgrade"
  );

  await extension.unload();
});

add_task(async function test_browserSetting_permissions() {
  async function background() {
    const permObj = { permissions: ["browserSettings"] };
    browser.test.onMessage.addListener(async msg => {
      if (msg === "request") {
        await browser.permissions.request(permObj);
        await browser.browserSettings.cacheEnabled.set({ value: false });
      } else if (msg === "remove") {
        await browser.permissions.remove(permObj);
      }
      browser.test.sendMessage("done");
    });
  }

  function cacheIsEnabled() {
    return (
      Services.prefs.getBoolPref("browser.cache.disk.enable") &&
      Services.prefs.getBoolPref("browser.cache.memory.enable")
    );
  }

  let extension = ExtensionTestUtils.loadExtension({
    background,
    manifest: {
      optional_permissions: ["browserSettings"],
    },
    useAddonManager: "permanent",
  });
  await extension.startup();
  ok(cacheIsEnabled(), "setting is not set after startup");

  await withHandlingUserInput(extension, async () => {
    extension.sendMessage("request");
    await extension.awaitMessage("done");
    ok(!cacheIsEnabled(), "setting was set after request");

    extension.sendMessage("remove");
    await extension.awaitMessage("done");
    ok(cacheIsEnabled(), "setting is reset after remove");

    extension.sendMessage("request");
    await extension.awaitMessage("done");
    ok(!cacheIsEnabled(), "setting was set after request");
  });

  await ExtensionPermissions._uninit();
  extensionHandlers.delete(extension);
  await AddonTestUtils.promiseRestartManager();
  await extension.awaitBackgroundStarted();

  await withHandlingUserInput(extension, async () => {
    extension.sendMessage("remove");
    await extension.awaitMessage("done");
    ok(cacheIsEnabled(), "setting is reset after remove");
  });

  await extension.unload();
});

add_task(async function test_privacy_permissions() {
  async function background() {
    const permObj = { permissions: ["privacy"] };
    browser.test.onMessage.addListener(async msg => {
      if (msg === "request") {
        await browser.permissions.request(permObj);
        await browser.privacy.websites.trackingProtectionMode.set({
          value: "always",
        });
      } else if (msg === "remove") {
        await browser.permissions.remove(permObj);
      }
      browser.test.sendMessage("done");
    });
  }

  function hasSetting() {
    return Services.prefs.getBoolPref("privacy.trackingprotection.enabled");
  }

  let extension = ExtensionTestUtils.loadExtension({
    background,
    manifest: {
      optional_permissions: ["privacy"],
    },
    useAddonManager: "permanent",
  });
  await extension.startup();
  ok(!hasSetting(), "setting is not set after startup");

  await withHandlingUserInput(extension, async () => {
    extension.sendMessage("request");
    await extension.awaitMessage("done");
    ok(hasSetting(), "setting was set after request");

    extension.sendMessage("remove");
    await extension.awaitMessage("done");
    ok(!hasSetting(), "setting is reset after remove");

    extension.sendMessage("request");
    await extension.awaitMessage("done");
    ok(hasSetting(), "setting was set after request");
  });

  await ExtensionPermissions._uninit();
  extensionHandlers.delete(extension);
  await AddonTestUtils.promiseRestartManager();
  await extension.awaitBackgroundStarted();

  await withHandlingUserInput(extension, async () => {
    extension.sendMessage("remove");
    await extension.awaitMessage("done");
    ok(!hasSetting(), "setting is reset after remove");
  });

  await extension.unload();
});

add_task(
  { pref_set: [["extensions.eventPages.enabled", true]] },
  async function test_permissions_event_page() {
    let extension = ExtensionTestUtils.loadExtension({
      useAddonManager: "permanent",
      manifest: {
        optional_permissions: ["privacy"],
        background: { persistent: false },
      },
      background() {
        browser.permissions.onAdded.addListener(details => {
          browser.test.sendMessage("added", details);
        });

        browser.permissions.onRemoved.addListener(details => {
          browser.test.sendMessage("removed", details);
        });
      },
    });

    await extension.startup();
    let events = ["onAdded", "onRemoved"];
    for (let event of events) {
      assertPersistentListeners(extension, "permissions", event, {
        primed: false,
      });
    }

    await extension.terminateBackground();
    for (let event of events) {
      assertPersistentListeners(extension, "permissions", event, {
        primed: true,
      });
    }

    let permObj = {
      permissions: ["privacy"],
      origins: [],
    };

    // enable the permissions while the background is stopped
    await ExtensionPermissions.add(
      extension.id,
      // Prevent `permObj` from being mutated.
      { ...permObj },
      extension.extension
    );
    let details = await extension.awaitMessage("added");
    Assert.deepEqual(permObj, details, "got added event");

    // Restart and test that permission removal wakes the background.
    await AddonTestUtils.promiseRestartManager();
    await extension.awaitStartup();

    for (let event of events) {
      assertPersistentListeners(extension, "permissions", event, {
        primed: true,
      });
    }

    // remove the permissions while the background is stopped
    await ExtensionPermissions.remove(
      extension.id,
      // Prevent `permObj` from being mutated.
      { ...permObj },
      extension.extension
    );

    details = await extension.awaitMessage("removed");
    Assert.deepEqual(permObj, details, "got removed event");

    await extension.unload();
  }
);

add_task(
  {
    pref_set: [
      ["extensions.background.idle.timeout", 100],
      ["extensions.webextOptionalPermissionPrompts", true],
    ],
  },
  async function test_permissions_request_idle_suspend() {
    info("Test that permissions.request() keeps bg page alive.");

    let pr = Glean.extensionsCounters.eventPageIdleResult.permissions_request;
    let before = pr.testGetValue() ?? 0;
    info(`Glean value permissions_request before: ${before}`);

    async function background() {
      browser.test.onMessage.addListener(async () => {
        browser.test.log("Calling permissions.request().");
        await browser.permissions.request({ permissions: ["browserSettings"] });
        browser.test.log("permissions.request() resolved.");
        browser.test.sendMessage("done");
      });
    }

    let extension = ExtensionTestUtils.loadExtension({
      manifest: {
        manifest_version: 3,
        optional_permissions: ["browserSettings"],
      },
      background,
    });
    await extension.startup();

    function obs(subject, _topic) {
      info("Waiting 200ms, normally the bg page would idle out.");
      // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
      setTimeout(() => subject.wrappedJSObject.resolve(true), 200);
    }

    info("Setup permissions prompt observer.");
    Services.obs.addObserver(obs, "webextension-optional-permission-prompt");

    await withHandlingUserInput(extension, async () => {
      extension.sendMessage("request");
      await extension.awaitMessage("done");
    });
    info("permissions.request() resolved successfully, bg page not suspended.");

    let count = pr.testGetValue() - before;
    // Because this "counter" measures time in increments of
    // background.idle.timeout, we know it should take at least 200ms,
    // but we need to leave slack of up to 500ms for slow test runs.
    ok(count >= 2 && count <= 5, `permissions_request counter: ${count}.`);

    Services.obs.removeObserver(obs, "webextension-optional-permission-prompt");
    await extension.unload();
  }
);

add_task(
  { pref_set: [["extensions.dataCollectionPermissions.enabled", true]] },
  async function test_getAll_with_data_collection() {
    async function background() {
      browser.test.onMessage.addListener(async msg => {
        browser.test.assertEq("getAll", msg, "expected correct message");
        const permissions = await browser.permissions.getAll();
        browser.test.sendMessage("all", permissions);
      });

      browser.permissions.onAdded.addListener(details => {
        browser.test.sendMessage("added", details);
      });

      browser.permissions.onRemoved.addListener(details => {
        browser.test.sendMessage("removed", details);
      });

      browser.test.sendMessage("ready");
    }

    const extension = ExtensionTestUtils.loadExtension({
      manifest: {
        manifest_version: 3,
        permissions: ["bookmarks"],
        browser_specific_settings: {
          gecko: {
            data_collection_permissions: {
              // "none" shouldn't be added to the list of data collection
              // permissions returned by `getAll()`.
              required: ["none"],
              optional: ["technicalAndInteraction", "locationInfo"],
            },
          },
        },
      },
      background,
    });
    await extension.startup();
    await extension.awaitMessage("ready");

    const getAllAndVerifyDataCollection = async expected => {
      extension.sendMessage("getAll");
      const permissions = await extension.awaitMessage("all");
      Assert.deepEqual(
        permissions,
        {
          permissions: ["bookmarks"],
          origins: [],
          data_collection: expected,
        },
        "expected permissions with data collection"
      );
    };

    // Pretend the T&I permission was granted at install time.
    let perms = {
      permissions: [],
      origins: [],
      data_collection: ["technicalAndInteraction"],
    };
    await ExtensionPermissions.add(extension.id, perms, extension.extension);
    let added = await extension.awaitMessage("added");
    Assert.deepEqual(
      added,
      {
        permissions: [],
        origins: [],
        data_collection: ["technicalAndInteraction"],
      },
      "expected new permissions granted"
    );
    await getAllAndVerifyDataCollection(["technicalAndInteraction"]);

    // Grant another optional data collection permission.
    perms = {
      permissions: [],
      origins: [],
      data_collection: ["technicalAndInteraction", "locationInfo"],
    };
    await ExtensionPermissions.add(extension.id, perms, extension.extension);
    added = await extension.awaitMessage("added");
    Assert.deepEqual(
      added,
      {
        permissions: [],
        origins: [],
        data_collection: ["locationInfo"],
      },
      "expected new permissions granted"
    );
    await getAllAndVerifyDataCollection([
      "technicalAndInteraction",
      "locationInfo",
    ]);

    // Revoke all optional data collection permissions.
    await ExtensionPermissions.remove(extension.id, perms, extension.extension);
    const removed = await extension.awaitMessage("removed");
    Assert.deepEqual(
      removed,
      {
        permissions: [],
        origins: [],
        data_collection: ["technicalAndInteraction", "locationInfo"],
      },
      "expected permissions revoked"
    );
    await getAllAndVerifyDataCollection([]);

    await extension.unload();
  }
);

add_task(
  { pref_set: [["extensions.dataCollectionPermissions.enabled", true]] },
  async function test_getAll_with_required_data_collection() {
    async function background() {
      browser.test.onMessage.addListener(async msg => {
        browser.test.assertEq("getAll", msg, "expected correct message");
        const permissions = await browser.permissions.getAll();
        browser.test.sendMessage("all", permissions);
      });

      browser.test.sendMessage("ready");
    }

    const extension = ExtensionTestUtils.loadExtension({
      manifest: {
        manifest_version: 3,
        permissions: ["bookmarks"],
        browser_specific_settings: {
          gecko: {
            data_collection_permissions: {
              required: ["bookmarksInfo"],
              optional: ["technicalAndInteraction", "locationInfo"],
            },
          },
        },
      },
      background,
    });
    await extension.startup();
    await extension.awaitMessage("ready");

    extension.sendMessage("getAll");
    let permissions = await extension.awaitMessage("all");
    Assert.deepEqual(
      permissions,
      {
        permissions: ["bookmarks"],
        origins: [],
        data_collection: ["bookmarksInfo"],
      },
      "expected permissions with required data collection"
    );

    let perms = {
      permissions: [],
      origins: [],
      data_collection: ["technicalAndInteraction"],
    };
    await ExtensionPermissions.add(extension.id, perms, extension.extension);
    extension.sendMessage("getAll");
    permissions = await extension.awaitMessage("all");
    Assert.deepEqual(
      permissions,
      {
        permissions: ["bookmarks"],
        origins: [],
        data_collection: ["bookmarksInfo", "technicalAndInteraction"],
      },
      "expected permissions with newly added data collection"
    );

    await extension.unload();
  }
);

add_task(
  { pref_set: [["extensions.dataCollectionPermissions.enabled", true]] },
  async function test_request_with_data_collection() {
    async function background() {
      browser.test.onMessage.addListener(async msg => {
        if (msg === "request-good") {
          await browser.permissions.request({
            data_collection: ["technicalAndInteraction"],
          });

          const permissions = await browser.permissions.getAll();
          browser.test.sendMessage(`${msg}:done`, permissions);
          return;
        }

        if (msg === "request-invalid") {
          try {
            browser.permissions.request({
              data_collection: ["invalid-permission"],
            });
            browser.test.fail("expected error");
          } catch (err) {
            browser.test.assertTrue(
              /Error processing data_collection.0: Value "invalid-permission" must either:/.test(
                err.message
              ),
              "expected error"
            );
          }
          browser.test.sendMessage(`${msg}:done`);
          return;
        }

        if (msg === "request-bad") {
          await browser.test.assertRejects(
            browser.permissions.request({
              data_collection: ["healthInfo"],
            }),
            /Cannot request data collection permission healthInfo since it was not declared in data_collection_permissions.optional/,
            "Expected rejection"
          );
          browser.test.sendMessage(`${msg}:done`);
          return;
        }

        browser.test.fail(`Got unexpected msg "${msg}"`);
      });

      browser.test.sendMessage("ready");
    }

    const extension = ExtensionTestUtils.loadExtension({
      manifest: {
        manifest_version: 2,
        browser_specific_settings: {
          gecko: {
            data_collection_permissions: {
              optional: ["technicalAndInteraction", "locationInfo"],
            },
          },
        },
      },
      background,
    });
    await extension.startup();
    await extension.awaitMessage("ready");

    await withHandlingUserInput(extension, async () => {
      extension.sendMessage("request-bad");
      await extension.awaitMessage("request-bad:done");
    });

    await withHandlingUserInput(extension, async () => {
      extension.sendMessage("request-invalid");
      await extension.awaitMessage("request-invalid:done");
    });

    let permissions;
    await withHandlingUserInput(extension, async () => {
      extension.sendMessage("request-good");
      permissions = await extension.awaitMessage("request-good:done");
    });
    Assert.deepEqual(
      permissions,
      {
        permissions: [],
        origins: [],
        data_collection: ["technicalAndInteraction"],
      },
      "expected permissions with data collection"
    );

    // Reequest the same permission again, which should be already granted.
    await withHandlingUserInput(extension, async () => {
      extension.sendMessage("request-good");
      permissions = await extension.awaitMessage("request-good:done");
    });
    Assert.deepEqual(
      permissions,
      {
        permissions: [],
        origins: [],
        data_collection: ["technicalAndInteraction"],
      },
      "expected permissions with data collection"
    );

    await extension.unload();
  }
);

add_task(
  { pref_set: [["extensions.dataCollectionPermissions.enabled", true]] },
  async function test_contains_data_collection() {
    async function background() {
      browser.test.onMessage.addListener(async (msg, arg) => {
        if (msg === "contains") {
          const result = await browser.permissions.contains(arg);
          browser.test.sendMessage(`${msg}:done`, result);
          return;
        }

        if (msg === "request") {
          await browser.permissions.request(arg);
          browser.test.sendMessage(`${msg}:done`);
          return;
        }

        if (msg === "remove") {
          await browser.permissions.remove(arg);
          browser.test.sendMessage(`${msg}:done`);
          return;
        }

        browser.test.fail(`Got unexpected msg "${msg}"`);
      });

      browser.test.sendMessage("ready");
    }

    const extension = ExtensionTestUtils.loadExtension({
      manifest: {
        manifest_version: 2,
        optional_permissions: ["bookmarks", "http://*.mozilla.org/*"],
        browser_specific_settings: {
          gecko: {
            data_collection_permissions: {
              optional: ["technicalAndInteraction", "locationInfo"],
            },
          },
        },
      },
      background,
    });
    await extension.startup();
    await extension.awaitMessage("ready");

    // A list of permission objects with various combination of api/data
    // collection permissions and origins. This will be used in different
    // assertions below.
    const PERMS = [
      { permissions: ["bookmarks"] },
      { origins: ["http://*.mozilla.org/*"] },
      { permissions: ["bookmarks"], origins: ["http://*.mozilla.org/*"] },
      { data_collection: ["technicalAndInteraction"] },
      { data_collection: ["locationInfo"] },
      { data_collection: ["technicalAndInteraction", "locationInfo"] },
      {
        permissions: ["bookmarks"],
        data_collection: ["technicalAndInteraction", "locationInfo"],
      },
      {
        permissions: ["bookmarks"],
        data_collection: ["locationInfo"],
      },
      {
        permissions: ["bookmarks"],
        origins: ["http://*.mozilla.org/*"],
        data_collection: ["technicalAndInteraction", "locationInfo"],
      },
    ];

    let result;
    for (const perms of PERMS) {
      extension.sendMessage("contains", perms);
      result = await extension.awaitMessage("contains:done");
      ok(!result, "Expected permission to not be granted");
    }

    info("request a single data collection permission");
    await withHandlingUserInput(extension, async () => {
      extension.sendMessage("request", {
        data_collection: ["technicalAndInteraction"],
      });
      await extension.awaitMessage("request:done");
    });

    extension.sendMessage("contains", {
      data_collection: ["technicalAndInteraction"],
    });
    result = await extension.awaitMessage("contains:done");
    ok(result, "Expected permission to be granted");

    extension.sendMessage("contains", {
      permissions: ["bookmarks"],
      data_collection: ["technicalAndInteraction"],
    });
    result = await extension.awaitMessage("contains:done");
    ok(!result, "Expected false because bookmarks isn't granted");

    info("request an API permission");
    await withHandlingUserInput(extension, async () => {
      extension.sendMessage("request", {
        permissions: ["bookmarks"],
      });
      await extension.awaitMessage("request:done");
    });

    extension.sendMessage("contains", {
      permissions: ["bookmarks"],
      data_collection: ["technicalAndInteraction"],
    });
    result = await extension.awaitMessage("contains:done");
    ok(result, "Expected permissions to be granted");

    extension.sendMessage("contains", {
      origins: ["http://*.mozilla.org/*"],
      data_collection: ["technicalAndInteraction"],
    });
    result = await extension.awaitMessage("contains:done");
    ok(!result, "Expected false because origin isn't granted");

    info("remove data collection permission");
    extension.sendMessage("remove", {
      data_collection: ["technicalAndInteraction"],
    });
    await extension.awaitMessage("remove:done");

    extension.sendMessage("contains", {
      data_collection: ["technicalAndInteraction"],
    });
    result = await extension.awaitMessage("contains:done");
    ok(!result, "Expected permission to not be granted");

    info("request all optional permissions");
    await withHandlingUserInput(extension, async () => {
      extension.sendMessage("request", {
        permissions: ["bookmarks"],
        origins: ["http://*.mozilla.org/*"],
        data_collection: ["technicalAndInteraction", "locationInfo"],
      });
      await extension.awaitMessage("request:done");
    });

    for (const perms of PERMS) {
      extension.sendMessage("contains", perms);
      result = await extension.awaitMessage("contains:done");
      ok(result, "Expected permission to be granted");
    }

    info("remove all");
    extension.sendMessage("remove", {
      permissions: ["bookmarks"],
      origins: ["http://*.mozilla.org/*"],
      data_collection: ["technicalAndInteraction", "locationInfo"],
    });
    await extension.awaitMessage("remove:done");

    for (const perms of PERMS) {
      extension.sendMessage("contains", perms);
      result = await extension.awaitMessage("contains:done");
      ok(!result, "Expected permission to not be granted");
    }

    await extension.unload();
  }
);

// This test verifies that data collection permissions are taken into account when
// an extension is installed before the `extensions.dataCollectionPermissions.enabled`
// pref is enabled.
add_task(
  { pref_set: [["extensions.dataCollectionPermissions.enabled", false]] },
  async function test_database_updated_for_data_collection() {
    const extension = ExtensionTestUtils.loadExtension({
      manifest: {
        manifest_version: 2,
        browser_specific_settings: {
          gecko: {
            id: "@updated-db-ext",
            data_collection_permissions: {
              required: ["locationInfo"],
            },
          },
        },
      },
      useAddonManager: "permanent",
    });
    await extension.startup();

    const addon = await AddonManager.getAddonByID(extension.id);
    ok(addon, "Expected add-on wrapper");
    Assert.deepEqual(
      addon.userPermissions,
      { permissions: [], origins: [], data_collection: [] },
      "Expected no permissions"
    );

    // Flip the pref.
    let observePromise = TestUtils.topicObserved(
      "xpi-provider:database-updated"
    );
    Services.prefs.setBoolPref(
      "extensions.dataCollectionPermissions.enabled",
      true
    );
    await observePromise;
    Assert.deepEqual(
      addon.userPermissions,
      { permissions: [], origins: [], data_collection: ["locationInfo"] },
      "Expected data collection permissions"
    );

    // Flip the pref back to `false`. We do not rebuild the DB in this case.
    Services.prefs.setBoolPref(
      "extensions.dataCollectionPermissions.enabled",
      false
    );
    Assert.deepEqual(
      addon.userPermissions,
      { permissions: [], origins: [], data_collection: ["locationInfo"] },
      "Expected data collection permissions"
    );

    await extension.unload();
  }
);
