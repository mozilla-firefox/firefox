"use strict";

const profileDir = do_get_profile();

const { ContextualIdentityService } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/contextualidentity/ContextualIdentityService.sys.mjs"
);

const TEST_STORE_FILE_PATH = PathUtils.join(
  profileDir.path,
  "test-containers.json"
);

// Test the containers JSON file migrations.
add_task(async function migratedFile() {
  // Let's create a file that has to be migrated.
  const oldFileData = {
    version: 2,
    lastUserContextId: 6,
    identities: [
      {
        userContextId: 1,
        public: true,
        icon: "fingerprint",
        color: "blue",
        l10nID: "userContextPersonal.label",
        accessKey: "userContextPersonal.accesskey",
      },
      {
        userContextId: 2,
        public: true,
        icon: "briefcase",
        color: "orange",
        l10nID: "userContextWork.label",
        accessKey: "userContextWork.accesskey",
      },
      {
        userContextId: 3,
        public: true,
        icon: "dollar",
        color: "green",
        l10nID: "userContextBanking.label",
        accessKey: "userContextBanking.accesskey",
      },
      {
        userContextId: 4,
        public: true,
        icon: "cart",
        color: "pink",
        l10nID: "userContextShopping.label",
        accessKey: "userContextShopping.accesskey",
      },
      {
        userContextId: 5,
        public: false,
        icon: "",
        color: "",
        name: "userContextIdInternal.thumbnail",
        accessKey: "",
      },
      {
        userContextId: 6,
        public: true,
        icon: "cart",
        color: "ping",
        name: "Custom user-created identity",
      },
    ],
  };

  await IOUtils.writeJSON(TEST_STORE_FILE_PATH, oldFileData, {
    tmpPath: TEST_STORE_FILE_PATH + ".tmp",
  });

  let cis =
    ContextualIdentityService.createNewInstanceForTesting(TEST_STORE_FILE_PATH);
  ok(!!cis, "We have our instance of ContextualIdentityService");

  // Check that the custom user-created identity exists.

  const expectedPublicLength = oldFileData.identities.filter(
    identity => identity.public
  ).length;
  const publicIdentities = cis.getPublicIdentities();
  const oldLastIdentity =
    oldFileData.identities[oldFileData.identities.length - 1];
  const customUserCreatedIdentity = publicIdentities
    .filter(identity => identity.name === oldLastIdentity.name)
    .pop();

  equal(
    publicIdentities.length,
    expectedPublicLength,
    "We should have the expected number of public identities"
  );
  ok(!!customUserCreatedIdentity, "Got the custom user-created identity");

  Assert.deepEqual(
    cis.getPublicUserContextIds(),
    cis.getPublicIdentities().map(identity => identity.userContextId),
    "getPublicUserContextIds has matching user context IDs"
  );

  // Check that the reserved userContextIdInternal.webextStorageLocal identity exists.

  const webextStorageLocalPrivateId =
    ContextualIdentityService._defaultIdentities
      .filter(
        identity => identity.name === "userContextIdInternal.webextStorageLocal"
      )
      .pop().userContextId;

  const privWebExtStorageLocal = cis.getPrivateIdentity(
    "userContextIdInternal.webextStorageLocal"
  );
  equal(
    privWebExtStorageLocal && privWebExtStorageLocal.userContextId,
    webextStorageLocalPrivateId,
    "We should have the default userContextIdInternal.webextStorageLocal private identity"
  );

  // Check that all StringBundle references are replaced by Fluent references.

  equal(
    cis
      .getPublicIdentities()
      .filter(identity => identity.l10nID || identity.accessKey).length,
    0,
    "No StringBundle l10nID or accessKey should be set"
  );
  equal(
    cis.getPublicIdentities().filter(identity => identity.l10nId).length,
    0,
    "No Fluent id should be stored on the identities"
  );
  checkDefaultLabels(cis);
});

// A version 6 file carries the default identities' Fluent ids from before
// Bug 2071753 renamed them; version 8 stops storing them.
add_task(async function migratedFileV6() {
  const path = PathUtils.join(profileDir.path, "test-containers-v6.json");
  const oldFileData = {
    version: 6,
    lastUserContextId: 5,
    identities: [
      {
        userContextId: 1,
        public: true,
        icon: "fingerprint",
        color: "blue",
        l10nId: "user-context-personal",
      },
      {
        userContextId: 2,
        public: true,
        icon: "briefcase",
        color: "orange",
        l10nId: "user-context-work",
      },
      {
        userContextId: 3,
        public: true,
        icon: "dollar",
        color: "green",
        l10nId: "user-context-banking",
      },
      {
        userContextId: 4,
        public: true,
        icon: "cart",
        color: "pink",
        l10nId: "user-context-shopping",
      },
      {
        userContextId: 5,
        public: true,
        icon: "cart",
        color: "pink",
        name: "Custom user-created identity",
      },
    ],
  };
  await IOUtils.writeJSON(path, oldFileData, { tmpPath: path + ".tmp" });

  let cis = ContextualIdentityService.createNewInstanceForTesting(path);
  checkDefaultLabels(cis);
  equal(
    cis.getUserContextLabel(5),
    "Custom user-created identity",
    "User-created identity keeps its name"
  );

  await cis.save();
  equal(
    (await IOUtils.readJSON(path)).version,
    cis.LAST_CONTAINERS_JSON_VERSION,
    "Migrated file is saved with the current version"
  );
});

// A version 7 file carries the default identities' Fluent ids; version 8
// drops them, and they come from the code's default identities instead.
add_task(async function migratedFileV7() {
  const path = PathUtils.join(profileDir.path, "test-containers-v7.json");
  await IOUtils.writeJSON(
    path,
    {
      version: 7,
      lastUserContextId: 5,
      identities: [
        {
          userContextId: 1,
          public: true,
          icon: "fingerprint",
          color: "blue",
          l10nId: "user-context-personal2",
        },
        {
          userContextId: 2,
          public: true,
          icon: "briefcase",
          color: "orange",
          l10nId: "user-context-work2",
        },
        {
          userContextId: 3,
          public: true,
          icon: "dollar",
          color: "green",
          l10nId: "user-context-banking2",
        },
        {
          userContextId: 4,
          public: true,
          icon: "cart",
          color: "pink",
          name: "Renamed default",
        },
        {
          userContextId: 5,
          public: true,
          icon: "cart",
          color: "pink",
          name: "Custom user-created identity",
        },
      ],
    },
    { tmpPath: path + ".tmp" }
  );

  let cis = ContextualIdentityService.createNewInstanceForTesting(path);
  Assert.deepEqual(
    [1, 2, 3, 4, 5].map(id => cis.getUserContextLabel(id)),
    [
      "Personal",
      "Work",
      "Banking",
      "Renamed default",
      "Custom user-created identity",
    ],
    "Labels resolve for every identity"
  );

  await cis.save();
  let saved = await IOUtils.readJSON(path);
  equal(saved.version, 8, "Migrated file is saved as version 8");
  ok(
    saved.identities.every(identity => !("l10nId" in identity)),
    "Saved file stores no Fluent ids"
  );
});

function checkDefaultLabels(cis) {
  Assert.deepEqual(
    [1, 2, 3, 4].map(id => cis.getUserContextLabel(id)),
    ["Personal", "Work", "Banking", "Shopping"],
    "Default identities resolve to their labels"
  );
}
