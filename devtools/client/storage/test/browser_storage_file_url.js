/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Test to verify that various storage types work when using file:// URLs.

"use strict";

add_task(async function () {
  // Disable the Local Mode notification as it forces the toolbox to resize
  // which cause some unexpected updates in the storage panel.
  await pushPref("devtools.local-mode.noticed", true);
  await pushPref("network.cookie.blockFileScheme", false);

  const TESTPAGE = "storage-file-url.html";

  // We need to load TESTPAGE using a file:// path so we need to get that from
  // the current test path.
  const testPath = getResolvedURI(gTestPath);
  const dir = getChromeDir(testPath);

  // Then append TESTPAGE to the test path.
  dir.append(TESTPAGE);

  // Then generate a FileURI to ensure the path is valid.
  const uriString = Services.io.newFileURI(dir).spec;

  // Now we have a valid file:// URL pointing to TESTPAGE.
  await openTabAndSetupStorage(uriString);

  // uriString points to the test inside objdir e.g.
  // `/path/to/fx/objDir/_tests/testing/mochitest/browser/devtools/client/
  // storage/test/storage-file-url.html`.
  //
  // When opened in the browser this may resolve to a different path e.g.
  // `path/to/fx/repo/devtools/client/storage/test/storage-file-url.html`.
  //
  // The easiest way to get the actual path is to request it from the content
  // process.
  const browser = gBrowser.selectedBrowser;
  const actualPath = await SpecialPowers.spawn(browser, [], () => {
    return content.document.location.href;
  });

  const cookiePath = actualPath
    .substr(0, actualPath.lastIndexOf("/"))
    .replace(/file:\/\//g, "");
  await checkState([
    [
      ["cookies", actualPath],
      [
        getCookieId("test1", "", cookiePath),
        getCookieId("test2", "", cookiePath),
      ],
    ],
    [
      ["indexedDB", actualPath, "MyDatabase (default)", "MyObjectStore"],
      [12345, 54321, 67890, 98765],
    ],
    [
      ["localStorage", actualPath],
      ["test3", "test4"],
    ],
    [
      ["sessionStorage", actualPath],
      ["test5", "test6"],
    ],
  ]);
});
