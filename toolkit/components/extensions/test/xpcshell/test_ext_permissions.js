"use strict";

const { AddonManager } = ChromeUtils.importESModule(
  "resource://gre/modules/AddonManager.sys.mjs"
);
const { permissionToL10nId } = ChromeUtils.importESModule(
  "resource://gre/modules/ExtensionPermissionMessages.sys.mjs"
);
const { ExtensionPermissions } = ChromeUtils.importESModule(
  "resource://gre/modules/ExtensionPermissions.sys.mjs"
);

const WITH_INSTALL_PROMPT = [
  ["extensions.originControls.grantByDefault", true],
];
const NO_INSTALL_PROMPT = [["extensions.originControls.grantByDefault", false]];

// TODO: Bug 1960273 - Update this test and remove this pref set when we enable
// the data collection permissions on all channels.
Services.prefs.setBoolPref(
  "extensions.dataCollectionPermissions.enabled",
  false
);

// ExtensionParent.sys.mjs is being imported lazily because when it is imported Services.appinfo will be
// retrieved and cached (as a side-effect of Schemas.sys.mjs being imported), and so Services.appinfo
// will not be returning the version set by AddonTestUtils.createAppInfo and this test will
// fail on non-nightly builds (because the cached appinfo.version will be undefined and
// AddonManager startup will fail).
ChromeUtils.defineESModuleGetters(this, {
  ExtensionParent: "resource://gre/modules/ExtensionParent.sys.mjs",
});

const l10n = new Localization([
  "toolkit/global/extensions.ftl",
  "toolkit/global/extensionPermissions.ftl",
  "branding/brand.ftl",
]);
// Localization resources need to be first iterated outside a test
l10n.formatValue("webext-perms-sideload-text");

AddonTestUtils.init(this);
AddonTestUtils.overrideCertDB();
AddonTestUtils.createAppInfo(
  "xpcshell@tests.mozilla.org",
  "XPCShell",
  "1",
  "42"
);

// This error is thrown in an xpcshell test on Android because we don't have
// the native part so we ignore it.
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
});

add_task(
  {
    skip_if: () => ExtensionPermissions._useLegacyStorageBackend,
  },
  async function test_permissions_rkv_recovery_rename() {
    const databaseDir = await makeRkvDatabaseDir(
      "extension-store-permissions",
      {
        mockCorrupted: true,
      }
    );
    const res = await ExtensionPermissions.get("@testextension");
    Assert.deepEqual(
      res,
      { permissions: [], origins: [], data_collection: [] },
      "Expect ExtensionPermissions get promise to be resolved"
    );
    Assert.ok(
      await IOUtils.exists(
        PathUtils.join(databaseDir, "data.safe.bin.corrupt")
      ),
      "Expect corrupt file to be found"
    );
  }
);

add_task(async function test_permissions_on_startup() {
  let extensionId = "@permissionTest";
  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      browser_specific_settings: {
        gecko: { id: extensionId },
      },
      permissions: ["tabs"],
    },
    useAddonManager: "permanent",
    async background() {
      let perms = await browser.permissions.getAll();
      browser.test.sendMessage("permissions", perms);
    },
  });
  let adding = {
    permissions: ["internal:privateBrowsingAllowed"],
    origins: [],
  };
  await extension.startup();
  let perms = await extension.awaitMessage("permissions");
  equal(perms.permissions.length, 1, "one permission");
  equal(perms.permissions[0], "tabs", "internal permission not present");

  const { StartupCache } = ExtensionParent;

  // StartupCache.permissions will not contain the extension permissions.
  let manifestData = await StartupCache.permissions.get(extensionId, () => {
    return { permissions: [], origins: [] };
  });
  equal(manifestData.permissions.length, 0, "no permission");

  perms = await ExtensionPermissions.get(extensionId);
  equal(perms.permissions.length, 0, "no permissions");
  await ExtensionPermissions.add(extensionId, adding);

  // Restart the extension and re-test the permissions.
  await ExtensionPermissions._uninit();
  await AddonTestUtils.promiseRestartManager();
  let restarted = extension.awaitMessage("permissions");
  await extension.awaitStartup();
  perms = await restarted;

  manifestData = await StartupCache.permissions.get(extensionId, () => {
    return { permissions: [], origins: [] };
  });
  deepEqual(
    manifestData.permissions,
    adding.permissions,
    "StartupCache.permissions contains permission"
  );

  equal(perms.permissions.length, 1, "one permission");
  equal(perms.permissions[0], "tabs", "internal permission not present");
  let added = await ExtensionPermissions._get(extensionId);
  deepEqual(added, adding, "permissions were retained");

  await extension.unload();
});

async function test_permissions({
  manifest_version,
  granted_host_permissions,
  useAddonManager,
  expectAllGranted,
  useOptionalHostPermissions,
}) {
  const REQUIRED_PERMISSIONS = ["downloads"];
  const REQUIRED_ORIGINS = ["*://site.com/", "*://*.domain.com/"];
  const REQUIRED_ORIGINS_EXPECTED = expectAllGranted
    ? ["*://site.com/*", "*://*.domain.com/*"]
    : [];

  const OPTIONAL_PERMISSIONS = ["idle", "clipboardWrite"];
  const OPTIONAL_ORIGINS = [
    "http://optionalsite.com/",
    "https://*.optionaldomain.com/",
  ];
  const OPTIONAL_ORIGINS_NORMALIZED = [
    "http://optionalsite.com/*",
    "https://*.optionaldomain.com/*",
  ];

  function background() {
    browser.test.onMessage.addListener(async (method, arg) => {
      if (method == "getAll") {
        let perms = await browser.permissions.getAll();
        let url = browser.runtime.getURL("*");
        perms.origins = perms.origins.filter(i => i != url);
        browser.test.sendMessage("getAll.result", perms);
      } else if (method == "contains") {
        let result = await browser.permissions.contains(arg);
        browser.test.sendMessage("contains.result", result);
      } else if (method == "request") {
        try {
          let result = await browser.permissions.request(arg);
          browser.test.sendMessage("request.result", {
            status: "success",
            result,
          });
        } catch (err) {
          browser.test.sendMessage("request.result", {
            status: "error",
            message: err.message,
          });
        }
      } else if (method == "remove") {
        let result = await browser.permissions.remove(arg);
        browser.test.sendMessage("remove.result", result);
      }
    });
  }

  let optional_permissions = OPTIONAL_PERMISSIONS;
  let optional_host_permissions = undefined;
  if (useOptionalHostPermissions) {
    optional_host_permissions = OPTIONAL_ORIGINS;
  } else {
    optional_permissions = optional_permissions.concat(OPTIONAL_ORIGINS);
  }

  let extension = ExtensionTestUtils.loadExtension({
    background,
    manifest: {
      manifest_version,
      permissions: REQUIRED_PERMISSIONS,
      host_permissions: REQUIRED_ORIGINS,
      optional_permissions,
      optional_host_permissions,
      granted_host_permissions,
    },
    useAddonManager,
  });

  await extension.startup();

  function call(method, arg) {
    extension.sendMessage(method, arg);
    return extension.awaitMessage(`${method}.result`);
  }

  let result = await call("getAll");
  deepEqual(result.permissions, REQUIRED_PERMISSIONS);
  deepEqual(result.origins, REQUIRED_ORIGINS_EXPECTED);

  for (let perm of REQUIRED_PERMISSIONS) {
    result = await call("contains", { permissions: [perm] });
    equal(result, true, `contains() returns true for fixed permission ${perm}`);
  }
  for (let origin of REQUIRED_ORIGINS) {
    result = await call("contains", { origins: [origin] });
    equal(
      result,
      expectAllGranted,
      `contains() returns true for fixed origin ${origin}`
    );
  }

  // None of the optional permissions should be available yet
  for (let perm of OPTIONAL_PERMISSIONS) {
    result = await call("contains", { permissions: [perm] });
    equal(result, false, `contains() returns false for permission ${perm}`);
  }
  for (let origin of OPTIONAL_ORIGINS) {
    result = await call("contains", { origins: [origin] });
    equal(result, false, `contains() returns false for origin ${origin}`);
  }

  result = await call("contains", {
    permissions: [...REQUIRED_PERMISSIONS, ...OPTIONAL_PERMISSIONS],
  });
  equal(
    result,
    false,
    "contains() returns false for a mix of available and unavailable permissions"
  );

  let perm = OPTIONAL_PERMISSIONS[0];
  result = await call("request", { permissions: [perm] });
  equal(
    result.status,
    "error",
    "request() fails if not called from an event handler"
  );
  ok(
    /request may only be called from a user input handler/.test(result.message),
    "error message for calling request() outside an event handler is reasonable"
  );
  result = await call("contains", { permissions: [perm] });
  equal(
    result,
    false,
    "Permission requested outside an event handler was not granted"
  );

  await withHandlingUserInput(extension, async () => {
    result = await call("request", { permissions: ["notifications"] });
    equal(
      result.status,
      "error",
      "request() for permission not in optional_permissions should fail"
    );
    ok(
      /since it was not declared in optional_permissions/.test(result.message),
      "error message for undeclared optional_permission is reasonable"
    );

    // Check request() when the prompt is canceled.
    optionalPermissionsPromptHandler.acceptPrompt = false;
    result = await call("request", { permissions: [perm] });
    equal(result.status, "success", "request() returned cleanly");
    equal(
      result.result,
      false,
      "request() returned false for rejected permission"
    );

    result = await call("contains", { permissions: [perm] });
    equal(result, false, "Rejected permission was not granted");

    // Call request() and accept the prompt
    optionalPermissionsPromptHandler.acceptPrompt = true;
    let allOptional = {
      permissions: OPTIONAL_PERMISSIONS,
      origins: OPTIONAL_ORIGINS,
    };
    result = await call("request", allOptional);
    equal(result.status, "success", "request() returned cleanly");
    equal(
      result.result,
      true,
      "request() returned true for accepted permissions"
    );

    // Verify that requesting a permission/origin in the wrong field fails
    let originsAsPerms = {
      permissions: OPTIONAL_ORIGINS,
    };
    let permsAsOrigins = {
      origins: OPTIONAL_PERMISSIONS,
    };

    result = await call("request", originsAsPerms);
    equal(
      result.status,
      "error",
      "Requesting an origin as a permission should fail"
    );
    ok(
      /Type error for parameter permissions \(Error processing permissions/.test(
        result.message
      ),
      "Error message for origin as permission is reasonable"
    );

    result = await call("request", permsAsOrigins);
    equal(
      result.status,
      "error",
      "Requesting a permission as an origin should fail"
    );
    ok(
      /Type error for parameter permissions \(Error processing origins/.test(
        result.message
      ),
      "Error message for permission as origin is reasonable"
    );
  });

  let allPermissions = {
    permissions: [...REQUIRED_PERMISSIONS, ...OPTIONAL_PERMISSIONS],
    origins: [...REQUIRED_ORIGINS_EXPECTED, ...OPTIONAL_ORIGINS_NORMALIZED],
  };

  result = await call("getAll");
  deepEqual(
    result,
    allPermissions,
    "getAll() returns required and runtime requested permissions"
  );

  result = await call("contains", allPermissions);
  equal(
    result,
    true,
    "contains() returns true for runtime requested permissions"
  );

  async function restart() {
    if (useAddonManager === "permanent") {
      await AddonTestUtils.promiseRestartManager();
    } else {
      // Manually reload for temporarily loaded.
      await extension.addon.reload();
    }
    await extension.awaitBackgroundStarted();
  }

  // Restart extension, verify permissions are still present.
  await restart();

  result = await call("getAll");
  deepEqual(
    result,
    allPermissions,
    "Runtime requested permissions are still present after restart"
  );

  // Check remove()
  result = await call("remove", { permissions: OPTIONAL_PERMISSIONS });
  equal(result, true, "remove() succeeded");

  let perms = {
    permissions: REQUIRED_PERMISSIONS,
    origins: [...REQUIRED_ORIGINS_EXPECTED, ...OPTIONAL_ORIGINS_NORMALIZED],
  };

  result = await call("getAll");
  deepEqual(result, perms, "Expected permissions remain after removing some");

  result = await call("remove", { origins: OPTIONAL_ORIGINS });
  equal(result, true, "remove() succeeded");

  perms.origins = REQUIRED_ORIGINS_EXPECTED;
  result = await call("getAll");
  deepEqual(result, perms, "Back to default permissions after removing more");

  if (granted_host_permissions && expectAllGranted) {
    // Check that all (granted) host permissions in MV3 can be revoked.

    result = await call("remove", { origins: REQUIRED_ORIGINS });
    equal(result, true, "remove() succeeded");
    perms.origins = [];

    result = await call("getAll");
    deepEqual(
      result,
      perms,
      "Expected only api permissions remain after removing all origins in mv3."
    );
  }

  // Clear cache to confirm same result after rebuilding it (after an update).
  await ExtensionParent.StartupCache.clearAddonData(extension.id);

  // Restart again, verify optional permissions state is still preserved.
  await restart();

  result = await call("getAll");
  deepEqual(result, perms, "Expected the same permissions after restart.");

  await extension.unload();
}

add_task(function test_normal_mv2() {
  return test_permissions({
    manifest_version: 2,
    useAddonManager: "permanent",
    expectAllGranted: true,
  });
});

add_task(function test_normal_mv3_noInstallPrompt() {
  return runWithPrefs(NO_INSTALL_PROMPT, () =>
    test_permissions({
      manifest_version: 3,
      useAddonManager: "permanent",
      expectAllGranted: false,
    })
  );
});

add_task(function test_normal_mv3() {
  return runWithPrefs(WITH_INSTALL_PROMPT, () =>
    test_permissions({
      manifest_version: 3,
      useAddonManager: "permanent",
      expectAllGranted: true,
    })
  );
});

add_task(function test_granted_for_temporary_mv3() {
  return test_permissions({
    manifest_version: 3,
    granted_host_permissions: true,
    useAddonManager: "temporary",
    expectAllGranted: true,
  });
});

add_task(function test_granted_only_for_privileged_mv3() {
  return runWithPrefs(NO_INSTALL_PROMPT, async () => {
    try {
      // For permanent non-privileged, granted_host_permissions does nothing.
      await test_permissions({
        manifest_version: 3,
        granted_host_permissions: true,
        useAddonManager: "permanent",
        expectAllGranted: false,
      });

      // Make extensions loaded with addon manager privileged.
      AddonTestUtils.usePrivilegedSignatures = true;

      await test_permissions({
        manifest_version: 3,
        granted_host_permissions: true,
        useAddonManager: "permanent",
        expectAllGranted: true,
      });
    } finally {
      AddonTestUtils.usePrivilegedSignatures = false;
    }
  });
});

add_task(function test_mv3_optional_host_permissions() {
  return runWithPrefs(WITH_INSTALL_PROMPT, () =>
    test_permissions({
      manifest_version: 3,
      useAddonManager: "permanent",
      useOptionalHostPermissions: true,
      expectAllGranted: true,
    })
  );
});

add_task(async function test_startup() {
  async function background() {
    browser.test.onMessage.addListener(async perms => {
      await browser.permissions.request(perms);
      browser.test.sendMessage("requested");
    });

    let all = await browser.permissions.getAll();
    let url = browser.runtime.getURL("*");
    all.origins = all.origins.filter(i => i != url);
    browser.test.sendMessage("perms", all);
  }

  const PERMS1 = {
    permissions: ["clipboardRead", "tabs"],
  };
  const PERMS2 = {
    origins: ["https://site2.com/*"],
  };

  let extension1 = ExtensionTestUtils.loadExtension({
    background,
    manifest: {
      optional_permissions: PERMS1.permissions,
    },
    useAddonManager: "permanent",
  });
  let extension2 = ExtensionTestUtils.loadExtension({
    background,
    manifest: {
      optional_permissions: PERMS2.origins,
    },
    useAddonManager: "permanent",
  });

  await extension1.startup();
  await extension2.startup();

  let perms = await extension1.awaitMessage("perms");
  perms = await extension2.awaitMessage("perms");

  await withHandlingUserInput(extension1, async () => {
    extension1.sendMessage(PERMS1);
    await extension1.awaitMessage("requested");
  });

  await withHandlingUserInput(extension2, async () => {
    extension2.sendMessage(PERMS2);
    await extension2.awaitMessage("requested");
  });

  // Restart everything, and force the permissions store to be
  // re-read on startup
  await ExtensionPermissions._uninit();
  await AddonTestUtils.promiseRestartManager();
  await extension1.awaitStartup();
  await extension2.awaitStartup();

  async function checkPermissions(extension, permissions) {
    perms = await extension.awaitMessage("perms");
    let expect = Object.assign({ permissions: [], origins: [] }, permissions);
    deepEqual(perms, expect, "Extension got correct permissions on startup");
  }

  await checkPermissions(extension1, PERMS1);
  await checkPermissions(extension2, PERMS2);

  await extension1.unload();
  await extension2.unload();
});

// Test that we don't prompt for permissions an extension already has.
async function test_alreadyGranted({ manifest_version }) {
  const REQUIRED_PERMISSIONS = ["geolocation"];
  const REQUIRED_ORIGINS = [
    "*://required-host.com/",
    "*://*.required-domain.com/",
  ];
  const OPTIONAL_PERMISSIONS = [
    ...REQUIRED_PERMISSIONS,
    ...REQUIRED_ORIGINS,
    "clipboardRead",
    "*://optional-host.com/",
    "*://*.optional-domain.com/",
  ];

  function pageScript() {
    browser.test.onMessage.addListener(async (msg, arg) => {
      if (msg == "request") {
        let result = await browser.permissions.request(arg);
        browser.test.sendMessage("request.result", result);
      } else if (msg == "remove") {
        let result = await browser.permissions.remove(arg);
        browser.test.sendMessage("remove.result", result);
      } else if (msg == "close") {
        window.close();
      }
    });

    browser.test.sendMessage("page-ready");
  }

  let extension = ExtensionTestUtils.loadExtension({
    background() {
      browser.test.sendMessage("ready", browser.runtime.getURL("page.html"));
    },

    manifest: {
      manifest_version,
      permissions: REQUIRED_PERMISSIONS,
      host_permissions: REQUIRED_ORIGINS,
      optional_permissions: OPTIONAL_PERMISSIONS,
      granted_host_permissions: true,
    },
    temporarilyInstalled: true,
    startupReason: "ADDON_INSTALL",
    files: {
      "page.html": `<html><head>
          <script src="page.js"><\/script>
        </head></html>`,

      "page.js": pageScript,
    },
  });

  await extension.startup();

  await withHandlingUserInput(extension, async () => {
    let url = await extension.awaitMessage("ready");
    let page = await ExtensionTestUtils.loadContentPage(url, { extension });
    await extension.awaitMessage("page-ready");

    async function checkRequest(arg, expectPrompt, msg) {
      optionalPermissionsPromptHandler.sawPrompt = false;
      extension.sendMessage("request", arg);
      let result = await extension.awaitMessage("request.result");
      ok(result, "request() call succeeded");
      equal(
        optionalPermissionsPromptHandler.sawPrompt,
        expectPrompt,
        `Got ${expectPrompt ? "" : "no "}permission prompt for ${msg}`
      );
    }

    await checkRequest(
      { permissions: ["geolocation"] },
      false,
      "required permission from manifest"
    );
    await checkRequest(
      { origins: ["http://required-host.com/"] },
      false,
      "origin permission from manifest"
    );
    await checkRequest(
      { origins: ["http://host.required-domain.com/"] },
      false,
      "wildcard origin permission from manifest"
    );

    await checkRequest(
      { permissions: ["clipboardRead"] },
      true,
      "optional permission"
    );
    await checkRequest(
      { permissions: ["clipboardRead"] },
      false,
      "already granted optional permission"
    );

    await checkRequest(
      { origins: ["http://optional-host.com/"] },
      true,
      "optional origin"
    );
    await checkRequest(
      { origins: ["http://optional-host.com/"] },
      false,
      "already granted origin permission"
    );

    await checkRequest(
      { origins: ["http://*.optional-domain.com/"] },
      true,
      "optional wildcard origin"
    );
    await checkRequest(
      { origins: ["http://*.optional-domain.com/"] },
      false,
      "already granted optional wildcard origin"
    );
    await checkRequest(
      { origins: ["http://host.optional-domain.com/"] },
      false,
      "host matching optional wildcard origin"
    );
    await page.close();
  });

  await extension.unload();
}
add_task(async function test_alreadyGranted_mv2() {
  return test_alreadyGranted({ manifest_version: 2 });
});
add_task(function test_alreadyGranted_mv3_noInstallPrompt() {
  return runWithPrefs(NO_INSTALL_PROMPT, () =>
    test_alreadyGranted({ manifest_version: 3 })
  );
});
add_task(function test_alreadyGranted_mv3() {
  return runWithPrefs(WITH_INSTALL_PROMPT, () =>
    test_alreadyGranted({ manifest_version: 3 })
  );
});

// IMPORTANT: Do not change these lists without review from a Web Extensions peer!

const GRANTED_WITHOUT_USER_PROMPT = [
  "activeTab",
  "activityLog",
  "alarms",
  "cookies",
  "declarativeNetRequestWithHostAccess",
  "dns",
  "idle",
  "mozillaAddons",
  "networkStatus",
  "scripting",
  "storage",
  "telemetry",
  "theme",
  "unlimitedStorage",
  "webRequest",
  "webRequestAuthProvider",
  "webRequestBlocking",
  "webRequestFilterResponse",
  "webRequestFilterResponse.serviceWorkerScript",
];

if (AppConstants.platform == "android") {
  GRANTED_WITHOUT_USER_PROMPT.push(
    "geckoViewAddons",
    "nativeMessagingFromContent"
  );
} else if (AppConstants.MOZ_APP_NAME == "thunderbird") {
  // TODO: Update GRANTED_WITHOUT_USER_PROMPT accordingly for Thunderbird.
} else {
  GRANTED_WITHOUT_USER_PROMPT.push(
    "captivePortal",
    "contextMenus",
    "contextualIdentities",
    "geckoProfiler",
    "identity",
    "menus",
    "menus.overrideContext",
    "normandyAddonStudy",
    "search",
    "tabGroups"
  );
}
GRANTED_WITHOUT_USER_PROMPT.sort();

add_task(async function test_permissions_have_localization_strings() {
  let noPromptNames = Schemas.getPermissionNames([
    "PermissionNoPrompt",
    "OptionalPermissionNoPrompt",
    "PermissionPrivileged",
  ]);
  Assert.deepEqual(
    GRANTED_WITHOUT_USER_PROMPT,
    noPromptNames,
    "List of no-prompt permissions is correct."
  );

  for (const perm of Schemas.getPermissionNames()) {
    const permId = permissionToL10nId(perm);
    if (permId) {
      const str = await l10n.formatValue(permId);
      ok(str.length, `Found localization string for '${perm}' permission`);
    } else {
      ok(
        GRANTED_WITHOUT_USER_PROMPT.includes(perm),
        `Permission '${perm}' intentionally granted without prompting the user`
      );
    }
  }
});

// Check <all_urls> used as an optional API permission.
add_task(async function test_optional_all_urls() {
  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      optional_permissions: ["<all_urls>"],
    },

    background() {
      browser.test.onMessage.addListener(async () => {
        let before = !!browser.tabs.captureVisibleTab;
        let granted = await browser.permissions.request({
          origins: ["<all_urls>"],
        });
        let after = !!browser.tabs.captureVisibleTab;

        browser.test.sendMessage("results", [before, granted, after]);
      });
    },
  });

  await extension.startup();

  await withHandlingUserInput(extension, async () => {
    extension.sendMessage("request");
    let [before, granted, after] = await extension.awaitMessage("results");

    equal(
      before,
      false,
      "captureVisibleTab() unavailable before optional permission request()"
    );
    equal(granted, true, "request() for optional permissions granted");
    equal(
      after,
      true,
      "captureVisibleTab() available after optional permission request()"
    );
  });

  await extension.unload();
});

// Check when content_script match patterns are treated as optional origins.
async function test_content_script_is_optional({
  manifest_version,
  expectGranted,
}) {
  function background() {
    browser.test.onMessage.addListener(async (msg, arg) => {
      if (msg == "request") {
        try {
          let result = await browser.permissions.request(arg);
          browser.test.sendMessage("result", result);
        } catch (e) {
          browser.test.sendMessage("result", e.message);
        }
      }
      if (msg === "getAll") {
        let result = await browser.permissions.getAll(arg);
        browser.test.sendMessage("granted", result);
      }
    });
  }

  const CS_ORIGIN = "https://test2.example.com/*";

  let extension = ExtensionTestUtils.loadExtension({
    background,
    manifest: {
      manifest_version,
      content_scripts: [
        {
          matches: [CS_ORIGIN],
          js: [],
        },
      ],
    },
  });

  await extension.startup();

  extension.sendMessage("getAll");
  let initial = await extension.awaitMessage("granted");
  if (manifest_version < 3 || !expectGranted) {
    deepEqual(initial.origins, [], "Nothing granted on install.");
  } else {
    deepEqual(initial.origins, [CS_ORIGIN], "CS origin granted on install.");
  }

  await withHandlingUserInput(extension, async () => {
    extension.sendMessage("request", {
      permissions: [],
      origins: [CS_ORIGIN],
    });
    let result = await extension.awaitMessage("result");
    if (manifest_version < 3) {
      equal(
        result,
        `Cannot request origin permission for ${CS_ORIGIN} since it was not declared in the manifest`,
        "Content script match pattern is not a requestable optional origin in MV2"
      );
    } else {
      equal(result, true, "request() for optional permissions succeeded");
    }
  });

  extension.sendMessage("getAll");
  let granted = await extension.awaitMessage("granted");
  deepEqual(
    granted.origins,
    manifest_version < 3 ? [] : [CS_ORIGIN],
    "Granted content script origin in MV3."
  );

  await extension.unload();
}

add_task(async function test_content_script_is_optional_mv2() {
  await test_content_script_is_optional({ manifest_version: 2 });
});
add_task(function test_content_script_is_optional_mv3_noInstallPrompt() {
  return runWithPrefs(NO_INSTALL_PROMPT, () =>
    test_content_script_is_optional({
      manifest_version: 3,
      expectGranted: false,
    })
  );
});
add_task(function test_content_script_is_optional_mv3() {
  return runWithPrefs(WITH_INSTALL_PROMPT, () =>
    test_content_script_is_optional({
      manifest_version: 3,
      expectGranted: true,
    })
  );
});

// Check that optional permissions are not included in update prompts
async function test_permissions_prompt({
  manifest_version,
  expectInitialGranted,
}) {
  function background() {
    browser.test.onMessage.addListener(async (msg, arg) => {
      if (msg == "request") {
        let result = await browser.permissions.request(arg);
        browser.test.sendMessage("result", result);
      }
      if (msg === "getAll") {
        let result = await browser.permissions.getAll(arg);
        browser.test.sendMessage("granted", result);
      }
    });
  }

  let extension = ExtensionTestUtils.loadExtension({
    background,
    manifest: {
      name: "permissions test",
      description: "permissions test",
      manifest_version,
      version: "1.0",

      permissions: ["tabs"],
      host_permissions: ["https://test1.example.com/*"],
      optional_permissions: ["clipboardWrite", "<all_urls>"],

      content_scripts: [
        {
          matches: ["https://test2.example.com/*"],
          js: [],
        },
      ],
    },
    useAddonManager: "permanent",
  });

  await extension.startup();

  await withHandlingUserInput(extension, async () => {
    extension.sendMessage("request", {
      permissions: ["clipboardWrite"],
      origins: ["https://test2.example.com/*"],
    });
    let result = await extension.awaitMessage("result");
    equal(result, true, "request() for optional permissions succeeded");
  });

  if (!expectInitialGranted) {
    await withHandlingUserInput(extension, async () => {
      extension.sendMessage("request", {
        origins: ["https://test1.example.com/*"],
      });
      let result = await extension.awaitMessage("result");
      equal(result, true, "request() for host_permissions in mv3 succeeded");
    });
  }

  // These permissions are promptable and available on Android and Desktop.
  const PERMS = ["browsingData", "tabs"];
  const ORIGINS = ["https://test1.example.com/*", "https://test3.example.com/"];
  let xpi = AddonTestUtils.createTempWebExtensionFile({
    background,
    manifest: {
      name: "permissions test",
      description: "permissions test",
      manifest_version,
      version: "2.0",

      browser_specific_settings: { gecko: { id: extension.id } },

      permissions: PERMS,
      host_permissions: ORIGINS,
      optional_permissions: ["clipboardWrite", "<all_urls>"],
    },
  });

  let install = await AddonManager.getInstallForFile(xpi);

  let perminfo;
  install.promptHandler = info => {
    perminfo = info;
    return Promise.resolve();
  };

  await AddonTestUtils.promiseCompleteInstall(install);
  await extension.awaitStartup();

  notEqual(perminfo, undefined, "Permission handler was invoked");
  let perms = perminfo.addon.userPermissions;
  deepEqual(
    perms.permissions,
    PERMS,
    "Update details includes only manifest api permissions"
  );
  deepEqual(
    perms.origins,
    manifest_version < 3 ? ORIGINS : [],
    "Update details includes only manifest origin permissions"
  );

  let EXPECTED = ["https://test1.example.com/*", "https://test2.example.com/*"];
  if (manifest_version < 3) {
    EXPECTED.push("https://test3.example.com/*");
  }

  extension.sendMessage("getAll");
  let granted = await extension.awaitMessage("granted");
  deepEqual(
    granted.origins.sort(),
    EXPECTED,
    "Granted origins persisted after update."
  );

  await extension.unload();
}
add_task(async function test_permissions_prompt_mv2() {
  return test_permissions_prompt({
    manifest_version: 2,
    expectInitialGranted: true,
  });
});
add_task(function test_permissions_prompt_mv3_noInstallPrompt() {
  return runWithPrefs(NO_INSTALL_PROMPT, () =>
    test_permissions_prompt({
      manifest_version: 3,
      expectInitialGranted: false,
    })
  );
});
add_task(async function test_permissions_prompt_mv3() {
  return runWithPrefs(WITH_INSTALL_PROMPT, () =>
    test_permissions_prompt({
      manifest_version: 3,
      expectInitialGranted: true,
    })
  );
});

// Check that internal permissions can not be set and are not returned by the API.
add_task(async function test_internal_permissions() {
  function background() {
    browser.test.onMessage.addListener(async (method, arg) => {
      try {
        if (method == "getAll") {
          let perms = await browser.permissions.getAll();
          browser.test.sendMessage("getAll.result", perms);
        } else if (method == "contains") {
          let result = await browser.permissions.contains(arg);
          browser.test.sendMessage("contains.result", {
            status: "success",
            result,
          });
        } else if (method == "request") {
          let result = await browser.permissions.request(arg);
          browser.test.sendMessage("request.result", {
            status: "success",
            result,
          });
        } else if (method == "remove") {
          let result = await browser.permissions.remove(arg);
          browser.test.sendMessage("remove.result", result);
        }
      } catch (err) {
        browser.test.sendMessage(`${method}.result`, {
          status: "error",
          message: err.message,
        });
      }
    });
  }

  let extension = ExtensionTestUtils.loadExtension({
    background,
    manifest: {
      name: "permissions test",
      description: "permissions test",
      manifest_version: 2,
      version: "1.0",
      permissions: [],
    },
    useAddonManager: "permanent",
    incognitoOverride: "spanning",
  });

  let perm = "internal:privateBrowsingAllowed";

  await extension.startup();

  function call(method, arg) {
    extension.sendMessage(method, arg);
    return extension.awaitMessage(`${method}.result`);
  }

  let result = await call("getAll");
  ok(!result.permissions.includes(perm), "internal not returned");

  result = await call("contains", { permissions: [perm] });
  ok(
    /Type error for parameter permissions \(Error processing permissions/.test(
      result.message
    ),
    `Unable to check for internal permission: ${result.message}`
  );

  result = await call("remove", { permissions: [perm] });
  ok(
    /Type error for parameter permissions \(Error processing permissions/.test(
      result.message
    ),
    `Unable to remove for internal permission ${result.message}`
  );

  await withHandlingUserInput(extension, async () => {
    result = await call("request", {
      permissions: [perm],
      origins: [],
    });
    ok(
      /Type error for parameter permissions \(Error processing permissions/.test(
        result.message
      ),
      `Unable to request internal permission ${result.message}`
    );
  });

  await extension.unload();
});

add_task(function test_normalizeOptional() {
  const optional1 = {
    origins: ["*://site.com/", "*://*.domain.com/"],
    permissions: ["downloads", "tabs"],
  };

  function normalize(perms, optional) {
    perms = { origins: [], permissions: [], ...perms };
    optional = { origins: [], permissions: [], ...optional };
    return ExtensionPermissions.normalizeOptional(perms, optional);
  }

  normalize({ origins: ["http://site.com/"] }, optional1);
  normalize({ origins: ["https://site.com/"] }, optional1);
  normalize({ origins: ["*://blah.domain.com/"] }, optional1);
  normalize({ permissions: ["downloads", "tabs"] }, optional1);

  Assert.throws(
    () => normalize({ origins: ["http://www.example.com/"] }, optional1),
    /was not declared in the manifest/
  );
  Assert.throws(
    () => normalize({ permissions: ["proxy"] }, optional1),
    /was not declared in optional_permissions/
  );

  const optional2 = {
    origins: ["<all_urls>", "*://*/*"],
    permissions: ["idle", "clipboardWrite"],
  };

  normalize({ origins: ["http://site.com/"] }, optional2);
  normalize({ origins: ["https://site.com/"] }, optional2);
  normalize({ origins: ["*://blah.domain.com/"] }, optional2);
  normalize({ permissions: ["idle", "clipboardWrite"] }, optional2);

  let perms = normalize({ origins: ["<all_urls>"] }, optional2);
  equal(
    perms.origins.sort().join(),
    optional2.origins.sort().join(),
    `Expect both "all sites" permissions`
  );
});

add_task(async function test_onAdded_all_urls() {
  let extension = ExtensionTestUtils.loadExtension({
    background() {
      browser.test.onMessage.addListener(async () => {
        let result = await browser.permissions.request({
          permissions: [],
          origins: ["<all_urls>"],
        });
        browser.test.sendMessage("result", result);
      });
      browser.permissions.onAdded.addListener(async permissions => {
        browser.test.sendMessage("onAdded", permissions);
      });
      browser.test.sendMessage("ready");
    },
    manifest: {
      optional_permissions: ["<all_urls>"],
    },
  });

  await extension.startup();
  await extension.awaitMessage("ready");

  await withHandlingUserInput(extension, async () => {
    optionalPermissionsPromptHandler.acceptPrompt = true;
    extension.sendMessage("request");
    let result = await extension.awaitMessage("result");
    equal(result, true, "request() for optional permissions succeeded");
  });

  let perms = await extension.awaitMessage("onAdded");
  equal(perms.origins.join(), "<all_urls>", "Got expected origins.");
  equal(perms.permissions.join(), "", "Not expecting api permissions.");

  await extension.unload();
});

add_task(async function test_add_data_collection() {
  let extensionId = "@data-collection-test";
  // Set up store with existing permissions, without data collection.
  await ExtensionPermissions._getStore().put(extensionId, {
    permissions: ["bookmarks"],
    origins: [],
  });

  // Verify that the store has only two properties.
  let perms = await ExtensionPermissions._getStore().get(extensionId);
  Assert.deepEqual(
    perms,
    { permissions: ["bookmarks"], origins: [] },
    "expected permissions without data collection"
  );

  // Add a new data collection permission.
  await ExtensionPermissions.add(extensionId, {
    permissions: [],
    origins: [],
    data_collection: ["technicalAndInteraction"],
  });
  // Expect the data permission to be added, even if there was none in the store.
  perms = await ExtensionPermissions.get(extensionId);
  Assert.deepEqual(
    perms,
    {
      permissions: ["bookmarks"],
      origins: [],
      data_collection: ["technicalAndInteraction"],
    },
    "expected permissions with data collection"
  );
});

add_task(async function test_remove_data_collection() {
  let extensionId = "@data-collection-test";
  // Set up store with existing permissions, without data collection.
  await ExtensionPermissions._getStore().put(extensionId, {
    permissions: ["bookmarks"],
    origins: [],
  });

  // Verify that the store has only two properties.
  let perms = await ExtensionPermissions._getStore().get(extensionId);
  Assert.deepEqual(
    perms,
    { permissions: ["bookmarks"], origins: [] },
    "expected permissions without data collection"
  );

  // Remove a permission and a data permission even if that isn't supposed to
  // be possible. This is needed to verify that loading permissions from the
  // store without data collection won't break anything.
  await ExtensionPermissions.remove(extensionId, {
    permissions: ["bookmarks"],
    origins: [],
    data_collection: ["technicalAndInteraction"],
  });
  // Expect the permission to be removed without side effect.
  perms = await ExtensionPermissions.get(extensionId);
  Assert.deepEqual(
    perms,
    {
      permissions: [],
      origins: [],
      data_collection: [],
    },
    "expected permissions with data collection"
  );
});
