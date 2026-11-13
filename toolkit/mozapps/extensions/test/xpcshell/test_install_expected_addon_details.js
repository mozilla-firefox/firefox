const {
  ERROR_INCORRECT_ID,
  ERROR_UNEXPECTED_ADDON_TYPE,
  ERROR_UNEXPECTED_ADDON_VERSION,
} = AddonManager;

const TEST_HOST = "example.com";

const testserver = createHttpServer({ hosts: [TEST_HOST] });

const ADDONS = {
  test_addon_v1: {
    manifest: {
      name: "Test",
      version: "1.0",
      browser_specific_settings: {
        gecko: { id: "test_addon@tests.mozilla.org" },
      },
    },
  },
  test_addon_v2: {
    manifest: {
      name: "Test",
      version: "2.0",
      browser_specific_settings: {
        gecko: { id: "test_addon@tests.mozilla.org" },
      },
    },
  },
};

const ADDON_URIS = {};

// The test extension uses an insecure update URL.
Services.prefs.setBoolPref(PREF_EM_CHECK_UPDATE_SECURITY, false);
Services.prefs.setBoolPref(PREF_EM_STRICT_COMPATIBILITY, false);

const profileDir = gProfD.clone();
profileDir.append("extensions");

add_setup(async function setup() {
  createAppInfo("xpcshell@tests.mozilla.org", "XPCShell", "1", "1.9.2");

  for (let [name, data] of Object.entries(ADDONS)) {
    const xpi = AddonTestUtils.createTempWebExtensionFile(data);
    const webPath = `/addons/${name}.xpi`;
    ADDON_URIS[name] = {
      file: Services.io.newFileURI(xpi),
      http: Services.io.newURI(`http://${TEST_HOST}${webPath}`),
    };
    testserver.registerFile(webPath, xpi);
  }

  await promiseStartupManager();

  // Create and configure the HTTP server.
  testserver.registerDirectory("/data/", do_get_file("data"));
  gPort = testserver.identity.primaryPort;
});

// Tests that the expected addon details are validated.
add_task(async function test_expected_addon_details_validated() {
  const url = ADDON_URIS.test_addon_v1.http.spec;

  await Assert.rejects(
    AddonManager.getInstallForURL(url, { version: 1.0 }),
    /version must be a string or null/
  );

  await Assert.rejects(
    AddonManager.getInstallForURL(url, { type: { foo: "bar" } }),
    /type must be a string or null/
  );

  await Assert.rejects(
    AddonManager.getInstallForURL(url, { id: true }),
    /ID must be a string or null/
  );
});

// Tests that the expected addon details are enforced when provided.
add_task(async function test_expected_addon_details_enforced() {
  const {
    name,
    version,
    browser_specific_settings: {
      gecko: { id },
    },
  } = ADDONS.test_addon_v2.manifest;

  const testCases = [
    ["No expected details", undefined],
    ["Correct version", { version }],
    ["Wrong version", { version: "wrong" }, ERROR_UNEXPECTED_ADDON_VERSION],
    ["Correct ID", { id }],
    ["Wrong ID", { id: "wrong" }, ERROR_INCORRECT_ID],
    ["Correct type", { type: "extension" }],
    ["Wrong type", { type: "wrong" }, ERROR_UNEXPECTED_ADDON_TYPE],
    ["All details correct", { type: "extension", id, version }],
    [
      "One detail wrong",
      { type: "wrong", id, version },
      ERROR_UNEXPECTED_ADDON_TYPE,
    ],
  ];

  for (const [description, expectedAddonDetails, expectedError] of testCases) {
    for (const upgrade of [true, false]) {
      for (const installType of ["http", "file"]) {
        // For the upgrade tests first install version 1.0.
        let initialInstall;
        if (upgrade) {
          initialInstall = await AddonManager.getInstallForURL(
            ADDON_URIS.test_addon_v1[installType].spec
          );
          await initialInstall.install();
          checkAddon(id, initialInstall, {
            version: "1.0",
          });
        }

        // Set things up for the version 2.0 install.
        const sourceURI = ADDON_URIS.test_addon_v2[installType];
        const descriptionPrefix = `[${description} - ${sourceURI.spec}${upgrade ? " (upgrade)" : ""}]: `;
        const expectInstallSuccess = !expectedError;
        const install = await AddonManager.getInstallForURL(
          sourceURI.spec,
          expectedAddonDetails
        );

        // Perform the test case.
        try {
          await install.install();
          equal(
            true,
            expectInstallSuccess,
            descriptionPrefix + "Installation should succeed"
          );
          checkAddon(id, install, {
            version,
            name,
            state: AddonManager.STATE_INSTALLED,
            sourceURI,
          });
        } catch (e) {
          equal(e.message, "Install failed: onDownloadFailed");
          equal(
            false,
            expectInstallSuccess,
            descriptionPrefix + "Installation should fail"
          );
          equal(
            install.error,
            expectedError,
            descriptionPrefix + "Installation error should be correct"
          );
        }

        // For a failed update, the version shouldn't have changed.
        const installedAddon = await AddonManager.getAddonByID(id);
        if (upgrade && !expectInstallSuccess) {
          checkAddon(id, installedAddon, {
            id,
            type: "extension",
            version: "1.0",
          });
        }

        // Cleanup.
        if (installedAddon) {
          await installedAddon.uninstall();
        }
      }
    }
  }
});
