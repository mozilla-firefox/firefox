"use strict";

const { ArcMigrationUtils, ArcMigrationUtilsClass } =
  ChromeUtils.importESModule(
    "moz-src:///browser/components/migration/ArcMigrationUtils.sys.mjs"
  );

const { ArcProfileMigrator, ChromeProfileMigrator } =
  ChromeUtils.importESModule(
    "moz-src:///browser/components/migration/ChromeProfileMigrator.sys.mjs"
  );

const { ChromeMigrationUtils } = ChromeUtils.importESModule(
  "moz-src:///browser/components/migration/ChromeMigrationUtils.sys.mjs"
);

const { MigratorBase } = ChromeUtils.importESModule(
  "moz-src:///browser/components/migration/MigratorBase.sys.mjs"
);

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

const { MockFilePicker } = ChromeUtils.importESModule(
  "resource://testing-common/MockFilePicker.sys.mjs"
);

function pairs(objects) {
  return objects.flatMap(o => [o.id, o]);
}

function tab(id, parentID, url, title, customTitle = null) {
  return {
    id,
    parentID,
    childrenIds: [],
    title: customTitle,
    data: { tab: { savedURL: url, savedTitle: title } },
  };
}

function createSidebar() {
  let items = [
    { id: "work-pinned", childrenIds: ["w1", "folder", "split", "arc"] },
    { id: "work-unpinned", childrenIds: ["w-today"] },
    tab("w1", "work-pinned", "https://example.com/mail", "Mail"),
    {
      id: "folder",
      parentID: "work-pinned",
      childrenIds: ["f1", "missing"],
      title: "Docs",
      data: { list: {} },
    },
    tab("f1", "folder", "https://example.com/doc", "Doc", "My Doc"),
    {
      id: "split",
      parentID: "work-pinned",
      childrenIds: ["s1", "s2"],
      data: { splitView: {} },
    },
    tab("s1", "split", "https://example.com/left", "Left"),
    tab("s2", "split", "https://example.com/right", "Right"),
    tab("arc", "work-pinned", "arc://settings", "Settings"),
    tab("w-today", "work-unpinned", "https://example.com/today", "Today"),
    { id: "personal-pinned", childrenIds: ["p1"] },
    tab("p1", "personal-pinned", "https://example.org/", "Personal"),
    { id: "empty-pinned", childrenIds: [] },
    { id: "default-favorites", childrenIds: ["fav1", "fav2"] },
    tab(
      "fav1",
      "default-favorites",
      "https://example.com/calendar",
      "Calendar"
    ),
    tab("fav2", "default-favorites", "arc://boosts", "Boosts"),
    { id: "profile1-favorites", childrenIds: ["fav3"] },
    tab("fav3", "profile1-favorites", "https://example.org/fav", "Fav"),
  ];
  let spaces = [
    {
      id: "work",
      title: "Work",
      profile: { default: true },
      containerIDs: ["pinned", "work-pinned", "unpinned", "work-unpinned"],
    },
    {
      id: "personal",
      title: "",
      profile: { custom: { _0: { directoryBasename: "Profile 1" } } },
      containerIDs: ["pinned", "personal-pinned"],
    },
    {
      id: "empty",
      title: "Empty",
      profile: { default: true },
      containerIDs: ["pinned", "empty-pinned"],
    },
  ];
  return {
    sidebar: {
      containers: [
        { littleBrowser: { _0: "little-window" } },
        {
          spaces: pairs([
            {
              id: "little",
              title: "Little",
              profile: { default: true },
              containerIDs: ["pinned", "little-pinned"],
            },
          ]),
          items: pairs([
            { id: "little-pinned", childrenIds: ["l1"] },
            tab("l1", "little-pinned", "https://example.net/", "Little"),
          ]),
          topAppsContainerIDs: [],
        },
        { global: {} },
        {
          spaces: pairs(spaces),
          items: pairs(items),
          topAppsContainerIDs: [
            { default: true },
            "default-favorites",
            { custom: { _0: { directoryBasename: "Profile 1" } } },
            "profile1-favorites",
          ],
        },
      ],
    },
  };
}

async function writeArcData(arcDir, sidebar = createSidebar()) {
  let userDataDir = PathUtils.join(arcDir, "User Data");
  await IOUtils.makeDirectory(PathUtils.join(userDataDir, "Default"), {
    createAncestors: true,
  });
  await IOUtils.writeJSON(PathUtils.join(userDataDir, "Local State"), {
    profile: { info_cache: { Default: { name: "Default" } } },
  });
  let sidebarPath = PathUtils.join(arcDir, "StorableSidebar.json");
  if (typeof sidebar === "string") {
    await IOUtils.writeUTF8(sidebarPath, sidebar);
  } else {
    await IOUtils.writeJSON(sidebarPath, sidebar);
  }
  return userDataDir;
}

add_task(async function test_convertSidebarToBookmarks() {
  let sidebar = createSidebar();

  Assert.deepEqual(
    await ArcMigrationUtils.convertSidebarToBookmarks(sidebar, "Default"),
    [
      { url: "https://example.com/calendar", title: "Calendar" },
      {
        type: PlacesUtils.bookmarks.TYPE_FOLDER,
        title: "Work",
        children: [
          { url: "https://example.com/mail", title: "Mail" },
          {
            type: PlacesUtils.bookmarks.TYPE_FOLDER,
            title: "Docs",
            children: [{ url: "https://example.com/doc", title: "My Doc" }],
          },
          { url: "https://example.com/left", title: "Left" },
          { url: "https://example.com/right", title: "Right" },
        ],
      },
    ],
    "Default profile gets its Favorites and the Work space's pinned tabs, but nothing from Little Arc"
  );

  Assert.deepEqual(
    await ArcMigrationUtils.convertSidebarToBookmarks(sidebar, "Profile 1"),
    [
      { url: "https://example.org/fav", title: "Fav" },
      {
        type: PlacesUtils.bookmarks.TYPE_FOLDER,
        title: "Space 1",
        children: [{ url: "https://example.org/", title: "Personal" }],
      },
    ],
    "Favorites are per profile and untitled spaces are numbered per profile"
  );

  Assert.deepEqual(
    await ArcMigrationUtils.convertSidebarToBookmarks({}, "Default"),
    []
  );
  Assert.deepEqual(
    await ArcMigrationUtils.convertSidebarToBookmarks(sidebar, "Profile 2"),
    []
  );
});

add_task(async function test_parseArcSidebar() {
  let plan = await ArcMigrationUtils.parseArcSidebar(createSidebar());
  Assert.deepEqual(
    plan,
    {
      favorites: [
        {
          type: "tab",
          url: "https://example.com/calendar",
          title: "Calendar",
          profile: "Default",
        },
        {
          type: "tab",
          url: "https://example.org/fav",
          title: "Fav",
          profile: "Profile 1",
        },
      ],
      spaces: [
        {
          id: "work",
          profile: "Default",
          title: "Work",
          items: [
            { type: "tab", url: "https://example.com/mail", title: "Mail" },
            {
              type: "folder",
              title: "Docs",
              items: [
                {
                  type: "tab",
                  url: "https://example.com/doc",
                  title: "My Doc",
                },
              ],
            },
            { type: "tab", url: "https://example.com/left", title: "Left" },
            { type: "tab", url: "https://example.com/right", title: "Right" },
          ],
        },
        {
          id: "personal",
          profile: "Profile 1",
          title: "Space 1",
          items: [
            { type: "tab", url: "https://example.org/", title: "Personal" },
          ],
        },
        { id: "empty", profile: "Default", title: "Empty", items: [] },
      ],
    },
    "The plan has every profile's Favorites and spaces, without Little Arc"
  );

  Assert.deepEqual(
    ArcMigrationUtils.filterArcPlan(plan),
    plan,
    "Without a profile the whole plan is kept"
  );
  Assert.deepEqual(
    ArcMigrationUtils.filterArcPlan(plan, "Profile 1"),
    { favorites: [plan.favorites[1]], spaces: [plan.spaces[1]] },
    "A profile keeps only its own Favorites and spaces"
  );

  let sidebar = createSidebar();
  let items = sidebar.sidebar.containers[3].items;
  items.find(i => i.id === "default-favorites").childrenIds.push("fav-folder");
  items.push(
    "fav-folder",
    {
      id: "fav-folder",
      parentID: "default-favorites",
      childrenIds: ["fav-nested"],
      title: "Folder",
      data: { list: {} },
    },
    "fav-nested",
    tab("fav-nested", "fav-folder", "https://example.com/nested", "Nested")
  );
  Assert.deepEqual(
    (await ArcMigrationUtils.parseArcSidebar(sidebar)).favorites.map(
      f => f.url
    ),
    [
      "https://example.com/calendar",
      "https://example.com/nested",
      "https://example.org/fav",
    ],
    "Folders among the Favorites are flattened"
  );
});

add_task(async function test_parseArcSpace_override() {
  /**
   * Adds the space id to each space and names untitled spaces, like a browser
   * that reads more from Arc.
   */
  class ExtendedArcMigrationUtils extends ArcMigrationUtilsClass {
    async parseArcSpace(space, items, options) {
      return {
        ...(await super.parseArcSpace(space, items, options)),
        extra: space.id,
      };
    }

    async parseArcSpaceTitle(space, number) {
      return space.title || `Untitled ${number}`;
    }
  }
  let plan = await new ExtendedArcMigrationUtils().parseArcSidebar(
    createSidebar()
  );
  Assert.deepEqual(
    plan.spaces.map(s => [s.title, s.extra]),
    [
      ["Work", "work"],
      ["Untitled 1", "personal"],
      ["Empty", "empty"],
    ],
    "Subclasses can add their own data and titles to each space"
  );
});

add_task(async function test_parseArcSpace_defaultNumber() {
  let space = { id: "untitled", title: "", profile: { default: {} } };
  Assert.equal(
    (await ArcMigrationUtils.parseArcSpace(space, [])).title,
    "Space 1",
    "Untitled spaces are numbered 1 without options"
  );
  Assert.equal(
    (await ArcMigrationUtils.parseArcSpace(space, [], { number: 3 })).title,
    "Space 3"
  );
});

add_task(function test_normalizeArcUrl() {
  Assert.equal(
    ArcMigrationUtils.normalizeArcUrl(new URL("https://example.com/a")),
    "https://example.com/a",
    "URL objects are accepted"
  );
  Assert.equal(
    ArcMigrationUtils.normalizeArcUrl(" https://example.com/b "),
    "https://example.com/b",
    "Strings are trimmed"
  );
  Assert.equal(ArcMigrationUtils.normalizeArcUrl("arc://settings"), null);
  Assert.equal(ArcMigrationUtils.normalizeArcUrl("not a url"), null);
  Assert.equal(ArcMigrationUtils.normalizeArcUrl(5), null);
});

add_task(async function test_convertSidebarToBookmarks_malformed() {
  let malformed = [
    null,
    "sidebar",
    { sidebar: { containers: {} } },
    { sidebar: { containers: "containers" } },
    { sidebar: { containers: [{ spaces: [], items: [5, "a", null] }] } },
    { sidebar: { containers: ["global", { spaces: [], items: [] }] } },
    { sidebar: { containers: [{ global: {} }] } },
    { sidebar: { containers: [{ global: {} }, { spaces: {}, items: [] }] } },
    {
      sidebar: {
        containers: [
          { littleBrowser: {} },
          { spaces: [], items: [] },
          { global: {} },
          "global",
        ],
      },
    },
  ];
  for (let json of malformed) {
    Assert.deepEqual(
      await ArcMigrationUtils.convertSidebarToBookmarks(json, "Default"),
      [],
      `Nothing is converted from ${JSON.stringify(json)}`
    );
  }

  let items = [
    { id: "pinned", childrenIds: ["number", "bad", "nested", "ok"] },
    {
      id: "number",
      title: 5,
      data: { tab: { savedURL: 123, savedTitle: {} } },
    },
    tab("bad", "pinned", "https://exa mple.com/", "Bad"),
    {
      id: "nested",
      childrenIds: { 0: "cycle" },
      data: { list: {} },
      title: "Nested",
    },
    { id: "ok", childrenIds: 5, data: "tab", title: ["Not a title"] },
    {
      id: "other-pinned",
      childrenIds: [
        "ftp",
        "javascript",
        "empty-folder",
        "loop",
        "ftp",
        "fork",
        "number-title",
        "array-title-folder",
      ],
    },
    tab("number-title", "other-pinned", "https://example.com/n", "Saved", 5),
    {
      id: "array-title-folder",
      childrenIds: ["in-array-folder"],
      data: { list: {} },
      title: ["Not a title"],
    },
    tab("in-array-folder", "array-title-folder", "https://example.com/f", "F"),
    tab("ftp", "other-pinned", " ftp://example.com/file ", null),
    tab("javascript", "other-pinned", "javascript:alert(1)", "JS"),
    {
      id: "empty-folder",
      childrenIds: ["data-url"],
      data: { list: {} },
      title: "Empty",
    },
    tab("data-url", "empty-folder", "data:text/html,hi", "Data"),
    { id: "loop", childrenIds: ["loop-child"], data: { list: {} } },
    { id: "loop-child", childrenIds: ["loop"], data: { list: {} } },
    { id: "fork", childrenIds: ["fork-a", "fork-a"], data: { list: {} } },
    { id: "fork-a", childrenIds: ["fork", "fork"], data: { list: {} } },
  ];
  let spaces = [
    { id: "a", title: 7, profile: { default: true }, containerIDs: {} },
    {
      id: "b",
      title: null,
      profile: { default: true },
      containerIDs: ["pinned", "pinned"],
    },
    {
      id: "c",
      profile: { default: true },
      containerIDs: ["pinned", "other-pinned"],
    },
  ];
  let sidebar = {
    sidebar: {
      containers: [
        { global: {} },
        {
          spaces: pairs(spaces),
          items: pairs(items),
          topAppsContainerIDs: "favorites",
        },
      ],
    },
  };

  Assert.deepEqual(
    await ArcMigrationUtils.convertSidebarToBookmarks(sidebar, "Default"),
    [
      {
        type: PlacesUtils.bookmarks.TYPE_FOLDER,
        title: "Space 3",
        children: [
          { url: "ftp://example.com/file", title: "" },
          { url: "https://example.com/n", title: "Saved" },
          {
            type: PlacesUtils.bookmarks.TYPE_FOLDER,
            title: "",
            children: [{ url: "https://example.com/f", title: "F" }],
          },
        ],
      },
    ],
    "Malformed entries, invalid URLs and empty folders are skipped, and non-string titles are ignored"
  );
});

add_task(async function test_malformedSidebarFile() {
  let arcDir = await IOUtils.createUniqueDirectory(
    PathUtils.tempDir,
    "test-arc-malformed"
  );
  registerCleanupFunction(() => IOUtils.remove(arcDir, { recursive: true }));

  let sandbox = sinon.createSandbox();
  registerCleanupFunction(() => sandbox.restore());

  for (let sidebar of ["{not json", { sidebar: { containers: {} } }]) {
    let userDataDir = await writeArcData(arcDir, sidebar);
    sandbox.stub(ChromeMigrationUtils, "getDataPath").resolves(userDataDir);
    sandbox
      .stub(ChromeProfileMigrator.prototype, "getResources")
      .resolves([{ type: MigrationUtils.resourceTypes.HISTORY }]);

    let migrator = new ArcProfileMigrator();
    Assert.ok(
      await migrator.isSourceAvailable(),
      "Arc history is still offered with a malformed sidebar file"
    );
    let resources = await migrator.getResources({ id: "Default" });
    Assert.ok(
      !resources.some(r => r.type === MigrationUtils.resourceTypes.BOOKMARKS),
      "No bookmarks are offered from a malformed sidebar file"
    );
    sandbox.restore();
  }
});

add_task(async function test_folderSelection() {
  let arcDir = await IOUtils.createUniqueDirectory(
    PathUtils.tempDir,
    "test-arc-permissions"
  );
  registerCleanupFunction(() => IOUtils.remove(arcDir, { recursive: true }));
  let userDataDir = await writeArcData(arcDir);

  let sandbox = sinon.createSandbox();
  registerCleanupFunction(() => sandbox.restore());
  sandbox.stub(MigratorBase, "needsFolderSelectionToReadData").returns(true);
  sandbox.stub(MigrationUtils, "canGetPermissionsOnPlatform").resolves(true);
  sandbox.stub(ChromeMigrationUtils, "getDataPath").resolves(userDataDir);

  MockFilePicker.init();
  registerCleanupFunction(() => MockFilePicker.cleanup());

  let migrator = new ArcProfileMigrator();
  Assert.ok(
    !(await migrator.hasPermissions()),
    "No permissions before the folder is selected"
  );

  let displayDirectoryPath;
  MockFilePicker.showCallback = () => {
    displayDirectoryPath = MockFilePicker.displayDirectory?.path;
    MockFilePicker.useDirectory([arcDir]);
  };
  MockFilePicker.returnValue = MockFilePicker.returnOK;
  Assert.ok(await migrator.getPermissions(), "Got permissions");
  Assert.equal(
    PathUtils.normalize(displayDirectoryPath),
    PathUtils.normalize(arcDir),
    "The file picker opens in the Arc folder that contains the sidebar file"
  );
  Assert.equal(
    PathUtils.normalize(await migrator._getChromeUserDataPathIfExists()),
    PathUtils.normalize(userDataDir),
    "Selecting the Arc folder maps back to its user data folder"
  );

  let resources = await migrator.getResources({ id: "Default" });
  Assert.ok(
    resources.some(r => r.type === MigrationUtils.resourceTypes.BOOKMARKS),
    "Spaces are offered after the Arc folder was selected"
  );

  sandbox.restore();
});

add_task(async function test_getLastUsedDate() {
  let arcDir = await IOUtils.createUniqueDirectory(
    PathUtils.tempDir,
    "test-arc-last-used"
  );
  registerCleanupFunction(() => IOUtils.remove(arcDir, { recursive: true }));
  let userDataDir = await writeArcData(arcDir);
  let sidebarPath = PathUtils.join(arcDir, "StorableSidebar.json");

  let sandbox = sinon.createSandbox();
  registerCleanupFunction(() => sandbox.restore());
  sandbox.stub(ChromeMigrationUtils, "getDataPath").resolves(userDataDir);
  let chromiumDate = new Date("2024-06-01T00:00:00Z");
  sandbox
    .stub(ChromeProfileMigrator.prototype, "getLastUsedDate")
    .resolves(chromiumDate);

  let sidebarDate = new Date("2025-01-01T00:00:00Z");
  await IOUtils.setModificationTime(sidebarPath, sidebarDate.getTime());
  Assert.equal(
    (await new ArcProfileMigrator().getLastUsedDate()).getTime(),
    sidebarDate.getTime(),
    "A sidebar file newer than the Chromium data is used"
  );

  await IOUtils.setModificationTime(
    sidebarPath,
    new Date("2023-01-01T00:00:00Z").getTime()
  );
  Assert.equal(
    (await new ArcProfileMigrator().getLastUsedDate()).getTime(),
    chromiumDate.getTime(),
    "Chromium data newer than the sidebar file is used"
  );

  await IOUtils.remove(sidebarPath);
  Assert.equal(
    (await new ArcProfileMigrator().getLastUsedDate()).getTime(),
    chromiumDate.getTime(),
    "Without a sidebar file the Chromium date is used"
  );

  sandbox.restore();
});

add_task(function test_telemetrySourceId() {
  Assert.equal(MigrationUtils.getSourceIdForTelemetry("arc"), 15);
});

add_task(async function test_migrateSpaces() {
  let rootDir = do_get_tempdir();
  let arcDir;
  if (AppConstants.platform === "macosx") {
    registerFakePath("ULibDir", rootDir);
    arcDir = PathUtils.join(rootDir.path, "Application Support", "Arc");
  } else {
    registerFakePath("LocalAppData", rootDir);
    arcDir = PathUtils.join(
      rootDir.path,
      "Packages",
      "TheBrowserCompany.Arc_somepublisherhash",
      "LocalCache",
      "Local",
      "Arc"
    );
  }
  let userDataDir = await writeArcData(arcDir);
  await IOUtils.writeJSON(PathUtils.join(userDataDir, "Default", "Bookmarks"), {
    roots: {
      bookmark_bar: {
        children: [
          { type: "url", url: "https://example.net/bar", name: "Bar" },
        ],
      },
      other: {
        children: [
          { type: "url", url: "https://example.net/other", name: "Other" },
        ],
      },
    },
  });

  if (AppConstants.platform === "win") {
    // Decoys: a sibling package and a matching package without Arc data,
    // which sorts before the real one.
    for (let name of ["Other.App_abc", "TheBrowserCompany.Arc_aaaa"]) {
      await IOUtils.makeDirectory(
        PathUtils.join(rootDir.path, "Packages", name)
      );
    }
  }
  Assert.equal(
    await ChromeMigrationUtils.getDataPath("Arc"),
    userDataDir,
    "Arc user data directory is found"
  );

  let migrator = await MigrationUtils.getMigrator("arc");
  Assert.ok(migrator.enabled, "Arc migrator is enabled in the wizard");
  Assert.ok(await migrator.isSourceAvailable(), "Arc source is available");

  let profiles = await migrator.getSourceProfiles();
  Assert.deepEqual(profiles, [{ id: "Default", name: "Default" }]);

  await promiseMigration(
    migrator,
    MigrationUtils.resourceTypes.BOOKMARKS,
    profiles[0]
  );

  let tree = await PlacesUtils.promiseBookmarksTree(
    PlacesUtils.bookmarks.toolbarGuid
  );
  Assert.ok(
    tree.children.some(c => c.uri === "https://example.com/calendar"),
    "Favorites were imported as toolbar bookmarks"
  );
  let work = tree.children.find(c => c.title === "Work");
  Assert.ok(work, "Work space was imported as a toolbar folder");
  Assert.deepEqual(
    work.children.map(c => c.title),
    ["Mail", "Docs", "Left", "Right"]
  );
  Assert.equal(work.children[1].children[0].uri, "https://example.com/doc");
  Assert.ok(
    !tree.children.some(c => c.uri === "https://example.net/bar"),
    "The Chromium Bookmarks file is not imported"
  );

  let unfiled = await PlacesUtils.promiseBookmarksTree(
    PlacesUtils.bookmarks.unfiledGuid
  );
  Assert.ok(
    !unfiled.children?.some(c => c.uri === "https://example.net/other"),
    "The Chromium Bookmarks file is not imported"
  );
});
