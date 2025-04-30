/* Any copyright is dedicated to the Public Domain.
https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { ExtensionTestCommon } = ChromeUtils.importESModule(
  "resource://testing-common/ExtensionTestCommon.sys.mjs"
);
const { PERMISSION_L10N, permissionToL10nId } = ChromeUtils.importESModule(
  "resource://gre/modules/ExtensionPermissionMessages.sys.mjs"
);

Services.prefs.setBoolPref(
  "extensions.dataCollectionPermissions.enabled",
  true
);

const getExtension = async extensionData => {
  const extension = ExtensionTestCommon.generate(extensionData);
  ExtensionTestUtils.failOnSchemaWarnings(false);
  await extension.loadManifest();
  ExtensionTestUtils.failOnSchemaWarnings(true);

  return extension;
};

add_task(async function test_none_is_exclusive() {
  const extension = await getExtension({
    manifest: {
      browser_specific_settings: {
        gecko: {
          data_collection_permissions: {
            required: ["none", "locationInfo"],
          },
        },
      },
    },
  });

  Assert.ok(
    extension.warnings[0]?.includes(
      `Data collection permission "none" is ignored because other data collection permissions have been specified. ` +
        `Either remove "none" from the required list, or do not include other required data collection permissions.`
    ),
    `Expected a warning about "none" being listed with other perms`
  );
  Assert.deepEqual(
    extension.manifest.applications.gecko.data_collection_permissions.required,
    ["locationInfo"],
    `Expected "none" to have been filtered out from the normalized property value`
  );
  Assert.deepEqual(
    Array.from(extension.dataCollectionPermissions.values()),
    ["locationInfo"],
    "Got the expected data collection permissions in the extension.dataCollectionPermissions set"
  );

  await extension.cleanupGeneratedFile();
});

add_task(async function test_none_cannot_be_optional() {
  const extension = await getExtension({
    manifest: {
      browser_specific_settings: {
        gecko: {
          data_collection_permissions: {
            optional: ["none"],
          },
        },
      },
    },
  });

  Assert.ok(
    extension.warnings[0]?.includes(
      "Error processing browser_specific_settings.gecko.data_collection_permissions.optional.0: " +
        `Value "none" must either: be one of`
    ),
    `Expected a warning about "none" being unknown in optional data collection permissions`
  );
  Assert.deepEqual(
    extension.manifest.applications.gecko.data_collection_permissions.optional,
    [],
    `Expected "none" to have been filtered out from the normalized property value`
  );

  await extension.cleanupGeneratedFile();
});

add_task(async function test_required_permissions() {
  for (const [data_collection_permissions, expected] of [
    [{}, []],
    [
      {
        required: [],
        optional: [],
      },
      [],
    ],
    [
      {
        required: ["none"],
      },
      ["none"],
    ],
    [
      {
        optional: ["healthInfo"],
      },
      [],
    ],
    [
      {
        required: ["locationInfo"],
        optional: ["healthInfo"],
      },
      ["locationInfo"],
    ],
    [
      {
        required: ["bookmarksInfo", "invalid"],
      },
      ["bookmarksInfo"],
    ],
    [
      {
        optional: ["technicalAndInteraction"],
      },
      [],
    ],
  ]) {
    const extension = await getExtension({
      manifest: {
        browser_specific_settings: {
          gecko: { data_collection_permissions },
        },
      },
    });

    deepEqual(
      extension.getRequiredPermissions(),
      {
        origins: [],
        permissions: [],
        data_collection: expected,
      },
      "Expected data collection permissions"
    );

    await extension.cleanupGeneratedFile();
  }
});

add_task(async function test_requested_permissions() {
  for (const [data_collection_permissions, expected] of [
    [{}, []],
    [
      {
        required: [],
        optional: [],
      },
      [],
    ],
    [
      {
        required: ["none"],
      },
      [],
    ],
    [
      {
        required: ["locationInfo"],
        optional: ["healthInfo"],
      },
      [],
    ],
    [
      {
        optional: ["technicalAndInteraction"],
      },
      // Only this data collection permission is requested at install time so far.
      ["technicalAndInteraction"],
    ],
  ]) {
    const extension = await getExtension({
      manifest: {
        browser_specific_settings: {
          gecko: { data_collection_permissions },
        },
      },
    });

    const permissions = extension.getRequestedPermissions();
    deepEqual(
      permissions,
      {
        origins: [],
        permissions: [],
        data_collection: expected,
      },
      "Expected data collection permissions"
    );

    await extension.cleanupGeneratedFile();
  }
});

add_task(async function test_permissions_have_localization_strings() {
  for (const perm of Schemas.getPermissionNames([
    "CommonDataCollectionPermission",
    "DataCollectionPermission",
    "OptionalDataCollectionPermission",
  ])) {
    if (perm === "none") {
      let str = await PERMISSION_L10N.formatValue(
        "webext-perms-description-data-none"
      );
      ok(str.length, `Found localization string for '${perm}'`);
    } else {
      let permId = permissionToL10nId(perm);
      let str = await PERMISSION_L10N.formatValue(permId);
      ok(str.length, `Found long localization string for '${perm}'`);

      permId = permissionToL10nId(perm, /* short */ true);
      str = await PERMISSION_L10N.formatValue(permId);
      ok(str.length, `Found short localization string for '${perm}'`);
    }
  }
});
