/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const FAVICON_DATA =
  "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAYAAAAf8/9hAAAABmJLR0QA/wD/AP+gvaeTAAAACXBIWXMAAAsTAAALEwEAmpwYAAAAB3RJTUUH3gwMDAsTBZbkNwAAAB1pVFh0Q29tbWVudAAAAAAAQ3JlYXRlZCB3aXRoIEdJTVBkLmUHAAABNElEQVQ4y8WSsU0DURBE3yyWIaAJaqAAN4DPSL6AlIACKIEOyJEgRsIgOOkiInJqgAKowNg7BHdn7MOksNl+zZ//dvbDf5cAiklp22BdVtXdeTEpDYDB9m1VzU6OJuVp2NdEQCaI96fH2YHG4+mDduKYNMYINTcjcGbXzQVDEAphG0k48zUsajIbnAiMIXThpW8EICE0RAK4dvoKg9NIcTiQ589otyHOZLnwqK5nLwBFUZ4igc3iM0d1ff8CMC6mZ6Ihiaqq3gi1aUAnArD00SW1fq5OLBg0ymYmSZsR2/t4e/rGyCLW0sbp3oq+yTYqVgytQWui2FS7XYF7GFprY921T4CNQt8zr47dNzCkIX7y/jBtH+v+RGMQrc828W8pApnZbmEVQp/Ae7BlOy2ttib81/UFc+WRWEbjckIAAAAASUVORK5CYII=";

const { BookmarksPolicies } = ChromeUtils.importESModule(
  "resource:///modules/policies/BookmarksPolicies.sys.mjs"
);

let CURRENT_POLICY;

const basePath = getRootDirectory(gTestPath).replace(
  "chrome://mochitests/content",
  "http://mochi.test:8888"
);

const BASE_POLICY = {
  policies: {
    DisplayBookmarksToolbar: true,
    Bookmarks: [
      {
        Title: "Bookmark 1",
        URL: "https://bookmark1.example.com/",
        Favicon: FAVICON_DATA,
      },
      {
        Title: "Bookmark 2",
        URL: "https://bookmark2.example.com/",
        Favicon: `${basePath}/favicon.svg`,
      },
      {
        Title: "Bookmark 3",
        URL: "https://bookmark3.example.com/",
        Folder: "Folder 1",
      },
      {
        Title: "Bookmark 4",
        URL: "https://bookmark4.example.com/",
        Placement: "menu",
      },
      {
        Title: "Bookmark 5",
        URL: "https://bookmark5.example.com/",
        Folder: "Folder 1",
      },
      {
        Title: "Bookmark 6",
        URL: "https://bookmark6.example.com/",
        Placement: "menu",
        Folder: "Folder 2",
      },
    ],
  },
};

/*
 * =================================
 * = HELPER FUNCTIONS FOR THE TEST =
 * =================================
 */
function deepClone(obj) {
  return JSON.parse(JSON.stringify(obj));
}

function findBookmarkInPolicy(bookmark) {
  // Find the entry in the given policy that corresponds
  // to this bookmark object from Places.
  for (let entry of CURRENT_POLICY.policies.Bookmarks) {
    if (entry.Title == bookmark.title) {
      return entry;
    }
  }
  return null;
}

async function promiseAllChangesMade({ itemsToAdd, itemsToRemove }) {
  return new Promise(resolve => {
    let listener = events => {
      for (const event of events) {
        switch (event.type) {
          case "bookmark-added":
            itemsToAdd--;
            if (itemsToAdd == 0 && itemsToRemove == 0) {
              PlacesUtils.observers.removeListener(
                ["bookmark-added", "bookmark-removed"],
                listener
              );
              resolve();
            }
            break;
          case "bookmark-removed":
            itemsToRemove--;
            if (itemsToAdd == 0 && itemsToRemove == 0) {
              PlacesUtils.observers.removeListener(
                ["bookmark-added", "bookmark-removed"],
                listener
              );
              resolve();
            }
            break;
        }
      }
    };
    PlacesUtils.observers.addListener(
      ["bookmark-added", "bookmark-removed"],
      listener
    );
  });
}

/*
 * ==================
 * = CHECK FUNCTION =
 * ==================
 *
 * Performs all the checks comparing what was given in
 * the policy JSON with what was retrieved from Places.
 */
async function check({ expectedNumberOfFolders }) {
  let bookmarks = [],
    folders = [];

  await PlacesUtils.bookmarks.fetch(
    { guidPrefix: BookmarksPolicies.BOOKMARK_GUID_PREFIX },
    r => {
      bookmarks.push(r);
    }
  );
  await PlacesUtils.bookmarks.fetch(
    { guidPrefix: BookmarksPolicies.FOLDER_GUID_PREFIX },
    r => {
      folders.push(r);
    }
  );

  let foldersToGuids = new Map();

  for (let folder of folders) {
    is(
      folder.type,
      PlacesUtils.bookmarks.TYPE_FOLDER,
      "Correct type for folder"
    );
    foldersToGuids.set(folder.title, folder.guid);
  }

  // For simplification and accuracy purposes, the number of expected
  // folders is manually specified in the test.
  is(
    folders.length,
    expectedNumberOfFolders,
    "Correct number of folders expected"
  );
  is(
    foldersToGuids.size,
    expectedNumberOfFolders,
    "There aren't two different folders with the same name"
  );

  is(
    CURRENT_POLICY.policies.Bookmarks.length,
    bookmarks.length,
    "The correct number of bookmarks exist"
  );

  for (let bookmark of bookmarks) {
    is(
      bookmark.type,
      PlacesUtils.bookmarks.TYPE_BOOKMARK,
      "Correct type for bookmark"
    );

    let entry = findBookmarkInPolicy(bookmark);

    is(bookmark.title, entry.Title, "Title matches");
    is(bookmark.url.href, entry.URL, "URL matches");

    let expectedPlacementGuid;
    if (entry.Folder) {
      expectedPlacementGuid = foldersToGuids.get(entry.Folder);
    } else {
      expectedPlacementGuid =
        entry.Placement == "menu"
          ? PlacesUtils.bookmarks.menuGuid
          : PlacesUtils.bookmarks.toolbarGuid;
    }

    is(bookmark.parentGuid, expectedPlacementGuid, "Correctly placed");
  }
}

/*
 * ================
 * = ACTUAL TESTS =
 * ================
 *
 * Note: the order of these tests is important, as we want to test not
 * only the end result of each configuration, but also the diff algorithm
 * that will add or remove bookmarks depending on how the policy changed.
 */

add_task(async function test_initial_bookmarks() {
  // Make a copy of the policy because we will be adding/removing entries from it
  CURRENT_POLICY = deepClone(BASE_POLICY);

  await Promise.all([
    promiseAllChangesMade({
      itemsToAdd: 8, // 6 bookmarks + 2 folders
      itemsToRemove: 0,
    }),
    setupPolicyEngineWithJson(CURRENT_POLICY),
  ]);

  await check({ expectedNumberOfFolders: 2 });
});

add_task(async function checkFavicon() {
  let bookmark1url = CURRENT_POLICY.policies.Bookmarks[0].URL;

  let result = await PlacesUtils.favicons.getFaviconForPage(
    Services.io.newURI(bookmark1url)
  );

  is(
    result.uri.spec,
    "fake-favicon-uri:" + bookmark1url,
    "Favicon URI is correct"
  );
  // data is an array of octets, which will be a bit hard to compare against
  // FAVICON_DATA, which is base64 encoded. Checking the expected length should
  // be good indication that this is working properly.
  is(result.rawData.length, 464, "Favicon data has the correct length");

  let faviconsExpiredNotification = TestUtils.topicObserved(
    "places-favicons-expired"
  );
  PlacesUtils.favicons.expireAllFavicons();
  await faviconsExpiredNotification;
});

add_task(async function checkNetworkFavicon() {
  let bookmarkURL = CURRENT_POLICY.policies.Bookmarks[1].URL;

  let result = await PlacesUtils.favicons.getFaviconForPage(
    Services.io.newURI(bookmarkURL)
  );

  is(result, null, "Favicon should not be loaded");
});

add_task(async function test_remove_Bookmark_2() {
  // Continuing from the previous policy:
  //
  // Remove the 2nd bookmark. It is inside "Folder 1", but that
  // folder won't be empty, so it must not be removed.
  CURRENT_POLICY.policies.Bookmarks.splice(3, 1);

  await Promise.all([
    promiseAllChangesMade({
      itemsToAdd: 0,
      itemsToRemove: 1, // 1 bookmark
    }),
    setupPolicyEngineWithJson(CURRENT_POLICY),
  ]);

  await check({ expectedNumberOfFolders: 2 });
});

add_task(async function test_remove_Bookmark_5() {
  // Continuing from the previous policy:
  //
  // Remove the last bookmark in the policy,
  // which means the "Folder 2" should also disappear
  CURRENT_POLICY.policies.Bookmarks.splice(-1, 1);

  await Promise.all([
    promiseAllChangesMade({
      itemsToAdd: 0,
      itemsToRemove: 2, // 1 bookmark and 1 folder
    }),
    setupPolicyEngineWithJson(CURRENT_POLICY),
  ]);

  await check({ expectedNumberOfFolders: 1 });
});

add_task(async function test_revert_to_original_policy() {
  CURRENT_POLICY = deepClone(BASE_POLICY);

  // Reverts to the original policy, which means that:
  // - "Bookmark 2"
  // - "Bookmark 5"
  // - "Folder 2"
  // should be recreated
  await Promise.all([
    promiseAllChangesMade({
      itemsToAdd: 3, // 2 bookmarks and 1 folder
      itemsToRemove: 0,
    }),
    setupPolicyEngineWithJson(CURRENT_POLICY),
  ]);

  await check({ expectedNumberOfFolders: 2 });
});

// Leave this one at the end, so that it cleans up any
// bookmarks created during this test.
add_task(async function test_empty_all_bookmarks() {
  CURRENT_POLICY = { policies: { Bookmarks: [] } };

  await Promise.all([
    promiseAllChangesMade({
      itemsToAdd: 0,
      itemsToRemove: 8, // 6 bookmarks and 2 folders
    }),
    setupPolicyEngineWithJson(CURRENT_POLICY),
  ]);

  check({ expectedNumberOfFolders: 0 });
});
