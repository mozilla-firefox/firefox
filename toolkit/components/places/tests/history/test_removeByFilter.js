"use strict";

/*
This test will ideally test the following cases
(each with and without a callback associated with it)
  Case A: Tests which should remove pages (Positives)
    Case A 1: Page has multiple visits both in/out of timeframe, all get deleted
    Case A 2: Page has single uri, removed by host
    Case A 3: Page has random subhost, with same host, removed by wildcard
    Case A 4: Page is localhost and localhost:port, removed by host
    Case A 5: Page is a `file://` type address, removed by empty host
    Case A 6: Page has a uri with a trailing dot
    Case A 7: Page is an IPv6 address with and without port, removed by host
    Cases A 1,2,3 will be tried with and without bookmarks added (which prevent page deletion)
  Case B: Tests in which no pages are removed (Inverses)
    Case B 1 (inverse): Page has no visits in timeframe, and nothing is deleted
    Case B 2: Page has single uri, not removed since hostname is different
    Case B 3: Page has multiple subhosts, not removed since wildcard doesn't match
  Case C: Combinations tests
    Case C 1: Single hostname, multiple visits, at least one in timeframe and hostname
    Case C 2: Random subhosts, multiple visits, at least one in timeframe and hostname-wildcard
*/

add_task(async function test_removeByFilter() {
  // Cleanup
  await PlacesUtils.history.clear();
  await PlacesUtils.bookmarks.eraseEverything();

  // Adding a witness URI
  let witnessURI = Services.io.newURI(
    "http://witnessmozilla.org/test_browserhistory/test_removeByFilter" +
      Math.random()
  );
  await PlacesTestUtils.addVisits(witnessURI);
  Assert.ok(
    await PlacesTestUtils.isPageInDB(witnessURI),
    "Witness URI is in database"
  );

  let removeByFilterTester = async function (
    visits,
    filter,
    checkBeforeRemove,
    checkAfterRemove,
    useCallback,
    bookmarkedUri
  ) {
    // Add visits for URIs
    await PlacesTestUtils.addVisits(visits);
    if (
      bookmarkedUri !== null &&
      visits.map(v => v.uri).includes(bookmarkedUri)
    ) {
      await PlacesUtils.bookmarks.insert({
        parentGuid: PlacesUtils.bookmarks.unfiledGuid,
        url: bookmarkedUri,
        title: "test bookmark",
      });
    }
    await checkBeforeRemove();

    // Take care of any observers (due to bookmarks)
    let { placesEventListener, promiseObserved } =
      getObserverPromise(bookmarkedUri);
    if (placesEventListener) {
      PlacesObservers.addListener(
        ["page-title-changed", "history-cleared", "page-removed"],
        placesEventListener
      );
    }
    // Perfom delete operation on database
    let removed = false;
    if (useCallback) {
      // The amount of callbacks will be the unique URIs to remove from the database
      let netCallbacksRequired = new Set(visits.map(v => v.uri)).size;
      removed = await PlacesUtils.history.removeByFilter(filter, pageInfo => {
        Assert.ok(
          PlacesUtils.validatePageInfo(pageInfo, false),
          "pageInfo should follow a basic format"
        );
        Assert.greater(
          netCallbacksRequired,
          0,
          "Callback called as many times as required"
        );
        netCallbacksRequired--;
      });
    } else {
      removed = await PlacesUtils.history.removeByFilter(filter);
    }
    await checkAfterRemove();
    await promiseObserved;
    if (placesEventListener) {
      await PlacesUtils.bookmarks.eraseEverything();
      PlacesObservers.removeListener(
        ["page-title-changed", "history-cleared", "page-removed"],
        placesEventListener
      );
    }
    Assert.ok(
      await PlacesTestUtils.isPageInDB(witnessURI),
      "Witness URI is still in database"
    );
    return removed;
  };

  const remoteUriList = [
    "http://mozilla.org/test_browserhistory/test_removeByFilter/" +
      Math.random(),
    "http://subdomain1.mozilla.org/test_browserhistory/test_removeByFilter/" +
      Math.random(),
    "http://subdomain2.mozilla.org/test_browserhistory/test_removeByFilter/" +
      Math.random(),
  ];
  const localhostUriList = [
    "http://localhost:4500/" + Math.random(),
    "http://localhost/" + Math.random(),
  ];
  const fileUriList = ["file:///home/user/files" + Math.random()];
  const trailingDotUriList = ["http://example.com./" + Math.random()];
  const ipv6UriList = [
    "http://[::1]/" + Math.random(),
    "http://[::1]:8080/" + Math.random(),
  ];
  const title = "Title " + Math.random();
  let sameHostVisits = [
    {
      uri: remoteUriList[0],
      title,
      visitDate: new Date(2005, 1, 1) * 1000,
    },
    {
      uri: remoteUriList[0],
      title,
      visitDate: new Date(2005, 3, 3) * 1000,
    },
    {
      uri: remoteUriList[0],
      title,
      visitDate: new Date(2007, 1, 1) * 1000,
    },
  ];
  let randomHostVisits = [
    {
      uri: remoteUriList[0],
      title,
      visitDate: new Date(2005, 1, 1) * 1000,
    },
    {
      uri: remoteUriList[1],
      title,
      visitDate: new Date(2005, 3, 3) * 1000,
    },
    {
      uri: remoteUriList[2],
      title,
      visitDate: new Date(2007, 1, 1) * 1000,
    },
  ];
  let localhostVisits = [
    {
      uri: localhostUriList[0],
      title,
    },
    {
      uri: localhostUriList[1],
      title,
    },
  ];
  let fileVisits = [
    {
      uri: fileUriList[0],
      title,
    },
  ];
  let trailingDotVisits = [
    {
      uri: trailingDotUriList[0],
      title,
    },
  ];
  let ipv6Visits = ipv6UriList.map(uri => ({ uri, title }));
  let assertInDB = async function (aUri) {
    Assert.ok(await PlacesTestUtils.isPageInDB(aUri));
  };
  let assertNotInDB = async function (aUri) {
    Assert.ok(!(await PlacesTestUtils.isPageInDB(aUri)));
  };
  for (let callbackUse of [true, false]) {
    // Case A Positives
    for (let bookmarkUse of [true, false]) {
      let bookmarkedUri = () => undefined;
      let checkableArray = arr => arr;
      let checkClosure = assertNotInDB;
      if (bookmarkUse) {
        bookmarkedUri = arr => arr[0];
        checkableArray = arr => arr.slice(1);
        checkClosure = function () {};
      }
      // Case A 1: Dates
      await removeByFilterTester(
        sameHostVisits,
        { beginDate: new Date(2004, 1, 1), endDate: new Date(2006, 1, 1) },
        () => assertInDB(remoteUriList[0]),
        () => checkClosure(remoteUriList[0]),
        callbackUse,
        bookmarkedUri(remoteUriList)
      );
      // Case A 2: Single Sub-host
      await removeByFilterTester(
        sameHostVisits,
        { host: "mozilla.org" },
        () => assertInDB(remoteUriList[0]),
        () => checkClosure(remoteUriList[0]),
        callbackUse,
        bookmarkedUri(remoteUriList)
      );
      // Case A 3: Multiple subhost
      await removeByFilterTester(
        randomHostVisits,
        { host: ".mozilla.org" },
        async () => {
          for (let uri of remoteUriList) {
            await assertInDB(uri);
          }
        },
        async () => {
          for (let uri of checkableArray(remoteUriList)) {
            await checkClosure(uri);
          }
        },
        callbackUse,
        bookmarkedUri(remoteUriList)
      );
    }

    // Case A 4: Localhost
    await removeByFilterTester(
      localhostVisits,
      { host: "localhost" },
      async () => {
        for (let uri of localhostUriList) {
          await assertInDB(uri);
        }
      },
      async () => {
        for (let uri of localhostUriList) {
          await assertNotInDB(uri);
        }
      },
      callbackUse
    );
    // Case A 5: Local Files
    await removeByFilterTester(
      fileVisits,
      { host: "." },
      async () => {
        for (let uri of fileUriList) {
          await assertInDB(uri);
        }
      },
      async () => {
        for (let uri of fileUriList) {
          await assertNotInDB(uri);
        }
      },
      callbackUse
    );
    // Case A 6: Trailing Dot Hostname
    await removeByFilterTester(
      trailingDotVisits,
      { host: "example.com." },
      async () => {
        for (let uri of trailingDotUriList) {
          await assertInDB(uri);
        }
      },
      async () => {
        for (let uri of trailingDotUriList) {
          await assertNotInDB(uri);
        }
      },
      callbackUse
    );
    // Case A 7: IPv6
    await removeByFilterTester(
      ipv6Visits,
      { host: "::1" },
      async () => {
        for (let uri of ipv6UriList) {
          await assertInDB(uri);
        }
      },
      async () => {
        for (let uri of ipv6UriList) {
          await assertNotInDB(uri);
        }
      },
      callbackUse
    );

    // Case B: Tests which do not remove anything (inverses)
    // Case B 1: Date
    await removeByFilterTester(
      sameHostVisits,
      { beginDate: new Date(2001, 1, 1), endDate: new Date(2002, 1, 1) },
      () => assertInDB(remoteUriList[0]),
      () => assertInDB(remoteUriList[0]),
      callbackUse
    );
    // Case B 2 : Single subhost
    await removeByFilterTester(
      sameHostVisits,
      { host: "notthere.org" },
      () => assertInDB(remoteUriList[0]),
      () => assertInDB(remoteUriList[0]),
      callbackUse
    );
    // Case B 3 : Multiple subhosts
    await removeByFilterTester(
      randomHostVisits,
      { host: ".notthere.org" },
      async () => {
        for (let uri of remoteUriList) {
          await assertInDB(uri);
        }
      },
      async () => {
        for (let uri of remoteUriList) {
          await assertInDB(uri);
        }
      },
      callbackUse
    );
    // Case B 4 : invalid local subhost
    await removeByFilterTester(
      randomHostVisits,
      { host: ".org" },
      async () => {
        for (let uri of remoteUriList) {
          await assertInDB(uri);
        }
      },
      async () => {
        for (let uri of remoteUriList) {
          await assertInDB(uri);
        }
      },
      callbackUse
    );

    // Case C: Combination Cases
    // Case C 1: single subhost
    await removeByFilterTester(
      sameHostVisits,
      {
        host: "mozilla.org",
        beginDate: new Date(2004, 1, 1),
        endDate: new Date(2006, 1, 1),
      },
      () => assertInDB(remoteUriList[0]),
      () => assertNotInDB(remoteUriList[0]),
      callbackUse
    );
    // Case C 2: multiple subhost
    await removeByFilterTester(
      randomHostVisits,
      {
        host: ".mozilla.org",
        beginDate: new Date(2005, 1, 1),
        endDate: new Date(2017, 1, 1),
      },
      async () => {
        for (let uri of remoteUriList) {
          await assertInDB(uri);
        }
      },
      async () => {
        for (let uri of remoteUriList) {
          await assertNotInDB(uri);
        }
      },
      callbackUse
    );
  }
});

// Test various error cases
add_task(async function test_error_cases() {
  Assert.throws(
    () => PlacesUtils.history.removeByFilter(),
    /TypeError: Expected a filter/
  );
  Assert.throws(
    () => PlacesUtils.history.removeByFilter("obviously, not a filter"),
    /TypeError: Expected a filter/
  );
  Assert.throws(
    () => PlacesUtils.history.removeByFilter({}),
    /TypeError: Expected a non-empty filter/
  );
  Assert.throws(
    () => PlacesUtils.history.removeVisitsByFilter({ beginDate: "now" }),
    /TypeError: Expected a valid Date/
  );
  Assert.throws(
    () => PlacesUtils.history.removeByFilter({ beginDate: Date.now() }),
    /TypeError: Expected a valid Date/
  );
  Assert.throws(
    () => PlacesUtils.history.removeByFilter({ beginDate: new Date(NaN) }),
    /TypeError: Expected a valid Date/
  );
  Assert.throws(
    () =>
      PlacesUtils.history.removeByFilter(
        { beginDate: new Date() },
        "obviously, not a callback"
      ),
    /TypeError: Invalid function/
  );
  Assert.throws(
    () =>
      PlacesUtils.history.removeByFilter({
        beginDate: new Date(1000),
        endDate: new Date(0),
      }),
    /TypeError: `beginDate` should be at least as old/
  );
  Assert.throws(
    () => PlacesUtils.history.removeByFilter({ host: "#" }),
    /TypeError: Expected well formed hostname string for/
  );
  Assert.throws(
    () => PlacesUtils.history.removeByFilter({ host: "www..org" }),
    /TypeError: Expected well formed hostname string for/
  );
  Assert.throws(
    () => PlacesUtils.history.removeByFilter({ host: {} }),
    /TypeError: `host` should be a string/
  );
  Assert.throws(
    () => PlacesUtils.history.removeByFilter({ host: "*.mozilla.org" }),
    /TypeError: Expected well formed hostname string for/
  );
  Assert.throws(
    () => PlacesUtils.history.removeByFilter({ host: "*" }),
    /TypeError: Expected well formed hostname string for/
  );
  Assert.throws(
    () => PlacesUtils.history.removeByFilter({ host: "(local files)" }),
    /TypeError: Expected well formed hostname string for/
  );
  Assert.throws(
    () => PlacesUtils.history.removeByFilter({ host: "" }),
    /TypeError: Expected a non-empty filter/
  );
});

add_task(async function test_chunking() {
  await PlacesUtils.history.clear();
  await PlacesUtils.bookmarks.eraseEverything();

  info("Insert many visited pages");
  let pages = [];
  for (let i = 1; i <= 550; i++) {
    let visits = [
      {
        date: new Date(Date.now() - (86400 + i) * 1000),
        transition: PlacesUtils.history.TRANSITIONS.TYPED,
      },
    ];
    pages.push(
      {
        url: `http://example.com/${i}`,
        title: `Page ${i}`,
        visits,
      },
      {
        url: `http://subdomain.example.com/${i}`,
        title: `Subdomain ${i}`,
        visits,
      }
    );
  }
  await PlacesUtils.history.insertMany(pages);

  info("Remove all visited pages");
  await PlacesUtils.history.removeByFilter({
    host: ".example.com",
  });
});

// Helper functions

function getObserverPromise(bookmarkedUri) {
  if (!bookmarkedUri) {
    return { promiseObserved: Promise.resolve() };
  }
  let placesEventListener;
  let promiseObserved = new Promise((resolve, reject) => {
    placesEventListener = events => {
      for (const event of events) {
        switch (event.type) {
          case "page-title-changed": {
            reject(new Error("Unexpected page-title-changed event happens"));
            break;
          }
          case "history-cleared": {
            reject(new Error("Unexpected history-cleared event happens"));
            break;
          }
          case "page-removed": {
            if (event.isRemovedFromStore) {
              Assert.notEqual(
                event.url,
                bookmarkedUri,
                "Bookmarked URI should not be deleted"
              );
            } else {
              Assert.equal(
                event.isPartialVisistsRemoval,
                false,
                "Observing page-removed deletes all visits"
              );
              Assert.equal(
                event.url,
                bookmarkedUri,
                "Bookmarked URI should have all visits removed but not the page itself"
              );
            }
            resolve();
            break;
          }
        }
      }
    };
  });
  return { placesEventListener, promiseObserved };
}
