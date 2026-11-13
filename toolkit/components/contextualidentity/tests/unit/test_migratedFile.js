"use strict";

const profileDir = do_get_profile();

const { ContextualIdentityService } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/contextualidentity/ContextualIdentityService.sys.mjs"
);

const TEST_STORE_FILE_PATH = PathUtils.join(
  profileDir.path,
  "test-containers.json"
);

const MAX_USER_CONTEXT_ID = 4294967295;

// Writes `data` to its own store file and returns a service instance with the
// migrated data already loaded.
async function loadFixture(name, data) {
  const path = PathUtils.join(profileDir.path, `test-containers-${name}.json`);
  await IOUtils.writeJSON(path, data, { tmpPath: path + ".tmp" });

  const cis = ContextualIdentityService.createNewInstanceForTesting(path);
  // getPublicUserContextIds() and friends do not hydrate on their own.
  cis.ensureDataReady();
  return cis;
}

// The four default identities as they were stored before version 5, when the
// labels still referenced the StringBundle.
function stringBundleDefaults() {
  return [
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
  ];
}

// The same four identities from version 5 on, using Fluent identifiers.
function fluentDefaults() {
  return [
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
  ];
}

function thumbnailIdentity({ legacy = false } = {}) {
  const identity = {
    userContextId: 5,
    public: false,
    icon: "",
    color: "",
    name: "userContextIdInternal.thumbnail",
  };
  if (legacy) {
    identity.accessKey = "";
  }
  return identity;
}

function webextStorageLocalIdentity({ legacy = false } = {}) {
  const identity = {
    userContextId: MAX_USER_CONTEXT_ID,
    public: false,
    icon: "",
    color: "",
    name: "userContextIdInternal.webextStorageLocal",
  };
  if (legacy) {
    identity.accessKey = "";
  }
  return identity;
}

function publicIdentityNamed(cis, name) {
  return cis.getPublicIdentities().find(identity => identity.name === name);
}

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
    ContextualIdentityService.getPrivateIdentity(
      "userContextIdInternal.webextStorageLocal"
    ).userContextId;

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
  equal(
    saved.version,
    ContextualIdentityService.LAST_CONTAINERS_JSON_VERSION,
    "Migrated file is saved with the current version"
  );
  ok(
    saved.identities.every(identity => !("l10nId" in identity)),
    "Saved file stores no Fluent ids"
  );
});

// Version 1 has no migration path: the data is discarded.
add_task(async function migrateVersion1() {
  const cis = await loadFixture("v1", {
    version: 1,
    lastUserContextId: 6,
    identities: [
      ...stringBundleDefaults(),
      thumbnailIdentity({ legacy: true }),
      {
        userContextId: 6,
        public: true,
        icon: "gift",
        color: "purple",
        name: "Custom user-created identity",
      },
    ],
  });

  equal(
    cis.getPublicIdentities().length,
    4,
    "Version 1 data is discarded and the default identities are restored"
  );
  ok(
    !publicIdentityNamed(cis, "Custom user-created identity"),
    "The user-created identity is not carried over"
  );
  ok(
    !!cis.getPrivateIdentity("userContextIdInternal.webextStorageLocal"),
    "The reserved extension storage identity is present"
  );
});

add_task(async function migrateVersion3() {
  const cis = await loadFixture("v3", {
    version: 3,
    lastUserContextId: 6,
    identities: [
      ...stringBundleDefaults(),
      thumbnailIdentity({ legacy: true }),
      {
        userContextId: 6,
        public: true,
        icon: "gift",
        color: "purple",
        name: "Custom user-created identity",
      },
    ],
  });

  // 3 -> 4 adds the reserved extension storage identity.
  const webextStorageLocal = cis.getPrivateIdentity(
    "userContextIdInternal.webextStorageLocal"
  );
  equal(
    webextStorageLocal && webextStorageLocal.userContextId,
    MAX_USER_CONTEXT_ID,
    "3 -> 4 adds the reserved extension storage identity"
  );

  // 4 -> 5 replaces the StringBundle references with Fluent ones, and 7 -> 8
  // drops them in favour of the code's default identities.
  equal(
    cis
      .getPublicIdentities()
      .filter(identity => identity.l10nID || identity.accessKey).length,
    0,
    "No StringBundle l10nID or accessKey is left behind"
  );
  equal(
    cis.getPublicIdentities().filter(identity => identity.l10nId).length,
    0,
    "No Fluent id is stored on the identities"
  );
  checkDefaultLabels(cis);

  equal(
    cis.getPublicIdentities().length,
    5,
    "The user-created identity is preserved"
  );

  Assert.deepEqual(
    cis.getSiteAssociations(),
    [],
    "Data predating site associations migrates to an empty set"
  );

  // The migrated file is rewritten at the current version.
  await cis.save();
  const migrated = await IOUtils.readJSON(cis._path);
  equal(
    migrated.version,
    cis.LAST_CONTAINERS_JSON_VERSION,
    "The migrated file is stored at the current version"
  );
});

add_task(async function migrateVersion4() {
  const cis = await loadFixture("v4", {
    version: 4,
    lastUserContextId: 6,
    identities: [
      ...stringBundleDefaults(),
      thumbnailIdentity({ legacy: true }),
      webextStorageLocalIdentity({ legacy: true }),
      {
        userContextId: 6,
        public: true,
        icon: "gift",
        color: "purple",
        name: "Custom user-created identity",
      },
    ],
  });

  // The reserved identity is already stored, so 3 -> 4 must not run again.
  equal(
    cis
      .getPrivateUserContextIds()
      .filter(userContextId => userContextId === MAX_USER_CONTEXT_ID).length,
    1,
    "The reserved extension storage identity is not duplicated"
  );

  equal(
    cis
      .getPublicIdentities()
      .filter(identity => identity.l10nID || identity.accessKey).length,
    0,
    "No StringBundle l10nID or accessKey is left behind"
  );
  equal(
    cis.getPublicIdentities().filter(identity => identity.l10nId).length,
    0,
    "No Fluent id is stored on the identities"
  );
  checkDefaultLabels(cis);
});

add_task(async function migrateVersion5() {
  const cis = await loadFixture("v5", {
    version: 5,
    lastUserContextId: 7,
    identities: [
      ...fluentDefaults(),
      thumbnailIdentity(),
      webextStorageLocalIdentity(),
      {
        userContextId: 6,
        public: true,
        icon: "fence",
        color: "turquoise",
        name: "Aliased to cyan",
      },
      {
        userContextId: 7,
        public: true,
        icon: "fence",
        color: "toolbar",
        name: "Aliased to gray",
      },
    ],
  });

  // 5 -> 6 rewrites the refreshed color names to their canonical form.
  equal(
    publicIdentityNamed(cis, "Aliased to cyan").color,
    "cyan",
    "5 -> 6 resolves the turquoise alias"
  );
  equal(
    publicIdentityNamed(cis, "Aliased to gray").color,
    "gray",
    "5 -> 6 resolves the toolbar alias"
  );
  equal(
    cis.getPublicIdentities().find(identity => identity.userContextId === 1)
      .color,
    "blue",
    "Canonical color names are left untouched"
  );
});

add_task(async function migrateVersion6SiteAssociations() {
  const cis = await loadFixture("v6-site-associations", {
    version: 6,
    lastUserContextId: 6,
    identities: [
      ...fluentDefaults(),
      thumbnailIdentity(),
      webextStorageLocalIdentity(),
      {
        userContextId: 6,
        public: true,
        icon: "gift",
        color: "purple",
        name: "Custom user-created identity",
      },
    ],
    siteAssociations: { "example.org": 1, "example.com": 6 },
  });

  equal(
    cis.getPublicIdentities().length,
    5,
    "All the public identities are loaded"
  );
  equal(
    publicIdentityNamed(cis, "Custom user-created identity").color,
    "purple",
    "The user-created identity is loaded unchanged"
  );

  equal(
    cis.getSiteAssociation("example.org"),
    1,
    "Site associations are loaded"
  );
  equal(
    cis.getSiteAssociation("example.com"),
    6,
    "Site associations are loaded"
  );
  equal(
    cis.getSiteAssociation("unassociated.example"),
    0,
    "An unassociated host resolves to the default container"
  );
});

// A file written by a newer Firefox is discarded rather than misparsed.
add_task(async function unknownVersion() {
  const cis = await loadFixture("unknown", {
    version: ContextualIdentityService.LAST_CONTAINERS_JSON_VERSION + 1,
    lastUserContextId: 6,
    identities: [
      ...fluentDefaults(),
      thumbnailIdentity(),
      webextStorageLocalIdentity(),
      {
        userContextId: 6,
        public: true,
        icon: "gift",
        color: "purple",
        name: "Custom user-created identity",
      },
    ],
    siteAssociations: { "example.org": 1 },
  });

  equal(
    cis.getPublicIdentities().length,
    4,
    "An unknown version is discarded and the default identities are restored"
  );
  ok(
    !publicIdentityNamed(cis, "Custom user-created identity"),
    "The user-created identity is not carried over"
  );
  Assert.deepEqual(
    cis.getSiteAssociations(),
    [],
    "Site associations are discarded along with the rest"
  );
});

function checkDefaultLabels(cis) {
  Assert.deepEqual(
    [1, 2, 3, 4].map(id => cis.getUserContextLabel(id)),
    ["Personal", "Work", "Banking", "Shopping"],
    "Default identities resolve to their labels"
  );
}
