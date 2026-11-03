"use strict";

const { ChromeMigrationUtils } = ChromeUtils.importESModule(
  "moz-src:///browser/components/migration/ChromeMigrationUtils.sys.mjs"
);

// Setup chrome user data path for all platforms.
ChromeMigrationUtils.getDataPath = () => {
  return Promise.resolve(
    do_get_file("Library/Application Support/Google/Chrome/").path
  );
};

add_task(async function test_getExtensionList_function() {
  let extensionList = await ChromeMigrationUtils.getExtensionList("Default");
  Assert.equal(
    extensionList.length,
    2,
    "There should be 2 extensions installed."
  );
  Assert.deepEqual(
    extensionList.find(extension => extension.id == "fake-extension-1"),
    {
      id: "fake-extension-1",
      name: "Fake Extension 1",
      description: "It is the description of fake extension 1.",
    },
    "First extension should match expectations."
  );
  Assert.deepEqual(
    extensionList.find(extension => extension.id == "fake-extension-2"),
    {
      id: "fake-extension-2",
      name: "Fake Extension 2",
      // There is no description in the `manifest.json` file of this extension.
      description: null,
    },
    "Second extension should match expectations."
  );
});

add_task(async function test_getExtensionInformation_function() {
  let extension = await ChromeMigrationUtils.getExtensionInformation(
    "fake-extension-1",
    "Default"
  );
  Assert.deepEqual(
    extension,
    {
      id: "fake-extension-1",
      name: "Fake Extension 1",
      description: "It is the description of fake extension 1.",
    },
    "Should get the extension information correctly."
  );
});

add_task(async function test_getLocaleString_function() {
  let name = await ChromeMigrationUtils._getLocaleString(
    "__MSG_name__",
    "en_US",
    "fake-extension-1",
    "Default"
  );
  Assert.deepEqual(
    name,
    "Fake Extension 1",
    "The value of __MSG_name__ locale key is Fake Extension 1."
  );
});

add_task(async function test_isExtensionInstalled_function() {
  let isInstalled = await ChromeMigrationUtils.isExtensionInstalled(
    "fake-extension-1",
    "Default"
  );
  Assert.ok(isInstalled, "The fake-extension-1 extension should be installed.");
});

add_task(async function test_getLastUsedProfileId_function() {
  let profileId = await ChromeMigrationUtils.getLastUsedProfileId();
  Assert.equal(
    profileId,
    "Default",
    "The last used profile ID should be Default."
  );
});

add_task(function test_mergeBookmarkChildren_empty() {
  Assert.deepEqual(
    ChromeMigrationUtils.mergeBookmarkChildren([]),
    [],
    "No inputs should give an empty result."
  );
  Assert.deepEqual(
    ChromeMigrationUtils.mergeBookmarkChildren([undefined, null, []]),
    [],
    "Missing or empty children arrays should be skipped."
  );
});

add_task(function test_mergeBookmarkChildren_single() {
  let children = [
    { type: "url", name: "A", url: "https://a.example/" },
    {
      type: "folder",
      name: "F",
      children: [{ type: "url", name: "B", url: "https://b.example/" }],
    },
    { type: "folder", name: "NoChildren" },
  ];
  Assert.deepEqual(
    ChromeMigrationUtils.mergeBookmarkChildren([undefined, children]),
    [
      { type: "url", name: "A", url: "https://a.example/" },
      {
        type: "folder",
        name: "F",
        children: [{ type: "url", name: "B", url: "https://b.example/" }],
      },
      { type: "folder", name: "NoChildren", children: [] },
    ],
    "A single input should be passed through unchanged (modulo children)."
  );
});

add_task(function test_mergeBookmarkChildren_merges_folders() {
  let local = [
    { type: "url", name: "Local", url: "https://local.example/" },
    {
      type: "folder",
      name: "Shared",
      children: [
        { type: "url", name: "L1", url: "https://l1.example/" },
        {
          type: "folder",
          name: "Nested",
          children: [{ type: "url", name: "LN", url: "https://ln.example/" }],
        },
      ],
    },
    { type: "folder", name: "LocalOnly", children: [] },
  ];
  let account = [
    {
      type: "folder",
      name: "Shared",
      children: [
        {
          type: "folder",
          name: "Nested",
          children: [{ type: "url", name: "AN", url: "https://an.example/" }],
        },
        { type: "url", name: "A1", url: "https://a1.example/" },
      ],
    },
    { type: "url", name: "Account", url: "https://account.example/" },
    { type: "folder", name: "AccountOnly", children: [] },
  ];
  let localCopy = structuredClone(local);
  let accountCopy = structuredClone(account);

  Assert.deepEqual(
    ChromeMigrationUtils.mergeBookmarkChildren([local, account]),
    [
      { type: "url", name: "Local", url: "https://local.example/" },
      {
        type: "folder",
        name: "Shared",
        children: [
          { type: "url", name: "L1", url: "https://l1.example/" },
          {
            type: "folder",
            name: "Nested",
            children: [
              { type: "url", name: "LN", url: "https://ln.example/" },
              { type: "url", name: "AN", url: "https://an.example/" },
            ],
          },
          { type: "url", name: "A1", url: "https://a1.example/" },
        ],
      },
      { type: "folder", name: "LocalOnly", children: [] },
      { type: "url", name: "Account", url: "https://account.example/" },
      { type: "folder", name: "AccountOnly", children: [] },
    ],
    "Same-named folders should be merged recursively, keeping first-seen order."
  );
  Assert.deepEqual(local, localCopy, "First input should not be modified.");
  Assert.deepEqual(
    account,
    accountCopy,
    "Second input should not be modified."
  );
});

add_task(function test_mergeBookmarkChildren_keeps_duplicate_urls() {
  let bookmark = { type: "url", name: "Dup", url: "https://dup.example/" };
  Assert.deepEqual(
    ChromeMigrationUtils.mergeBookmarkChildren([[bookmark], [bookmark]]),
    [bookmark, bookmark],
    "URL items are concatenated without de-duplication."
  );
});

add_task(function test_chromeTransitionToPlaces() {
  const { TRANSITIONS } = PlacesUtils.history;
  // Chrome transition qualifiers
  const PAGE_TRANSITION_CHAIN_START = 0x10000000;
  const PAGE_TRANSITION_CHAIN_END = 0x20000000;
  const intCases = [
    [0, 10, TRANSITIONS.LINK],
    [1, 9, TRANSITIONS.TYPED],
    [2, 8, TRANSITIONS.BOOKMARK],
    [3, 7, TRANSITIONS.EMBED],
    [4, 6, TRANSITIONS.FRAMED_LINK],
    [5, 5, TRANSITIONS.TYPED],
    [6, 4, TRANSITIONS.LINK],
    [7, 3, TRANSITIONS.LINK],
    [8, 2, TRANSITIONS.RELOAD],
    [9, 1, TRANSITIONS.TYPED],
    [10, 0, TRANSITIONS.TYPED],
  ];
  const nullCases = [
    [null, 0, TRANSITIONS.LINK],
    [null, 3, TRANSITIONS.TYPED],
  ];
  const qualifierCases = intCases.map(([chromeTransition, ...rest]) => [
    chromeTransition | PAGE_TRANSITION_CHAIN_START | PAGE_TRANSITION_CHAIN_END,
    ...rest,
  ]);

  for (let [chromeTransition, typedCount, expected] of [
    ...intCases,
    ...nullCases,
    ...qualifierCases,
  ]) {
    Assert.equal(
      ChromeMigrationUtils.chromeTransitionToPlaces(
        chromeTransition,
        typedCount
      ),
      expected,
      `Chrome transition ${chromeTransition} with typed_count ${typedCount}`
    );
  }
});
