/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

add_task(async function () {
  const URL = URL_ROOT_COM_SSL + "storage-indexeddb-iframe.html";

  // open tab
  await openTabAndSetupStorage(URL);
  const doc = gPanelWindow.document;

  // check that host appears in the storage tree
  checkTree(doc, ["indexedDB", "https://example.com"]);
  // check the table for values
  await selectTreeItem([
    "indexedDB",
    "https://example.com",
    "db (default)",
    "store",
  ]);
  checkStorageData("foo", JSON.stringify({ key: "foo", value: "bar" }));

  // check that host appears in the storage tree
  checkTree(doc, ["indexedDB", "https://example.net"]);
  // check the table for values
  await selectTreeItem([
    "indexedDB",
    "https://example.net",
    "db (default)",
    "store",
  ]);
  checkStorageData("lorem", JSON.stringify({ key: "lorem", value: "ipsum" }));

  info("Add new data to the iframe DB");
  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], async function () {
    const iframe = content.document.querySelector("iframe");
    return SpecialPowers.spawn(iframe, [], async function () {
      return new Promise(resolve => {
        const request = content.window.indexedDB.open("db", 1);
        request.onsuccess = event => {
          const db = event.target.result;
          const transaction = db.transaction(["store"], "readwrite");
          const addRequest = transaction
            .objectStore("store")
            .add({ key: "hello", value: "world" });
          addRequest.onsuccess = () => {
            db.close();
            resolve();
          };
        };
      });
    });
  });

  info("Refreshing table");
  doc.querySelector("#refresh-button").click();

  info("Check that table has new row");
  await waitUntil(() =>
    hasStorageData("hello", JSON.stringify({ key: "hello", value: "world" }))
  );
  ok(true, "Table has the new data");
});
