/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

// See also browser/base/content/test/newtab/.

const { NewTabUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/NewTabUtils.sys.mjs"
);
const { PlacesTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PlacesTestUtils.sys.mjs"
);
const { PlacesUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/PlacesUtils.sys.mjs"
);

// 0 days ago, 1 visit. This is to mimic the expectation of the original test,
// which is that a single visit should not be filtered when running tests.
const FRECENCY_THRESHOLD = PlacesUtils.history.pageFrecencyThreshold(
  0,
  1,
  false
);

// const SEARCH_SHORTCUTS_EXPERIMENT_PREF =
//   "browser.newtabpage.activity-stream.improvesearch.topSiteSearchShortcuts";
// Services.prefs.setBoolPref(SEARCH_SHORTCUTS_EXPERIMENT_PREF, false);

// use time at the start of the tests, changing it inside timeDaysAgo()
// may cause tiny time differences, which break expected sql ordering
const TIME_NOW = new Date().getTime();

// utility function to compute past timestamp in microseconds
function timeDaysAgo(numDays) {
  return (TIME_NOW - numDays * 24 * 60 * 60 * 1000) * 1000;
}

// tests that timestamp falls within 10 days of now
function isVisitDateOK(timestampMS) {
  let range = 10 * 24 * 60 * 60 * 1000;
  return Math.abs(Date.now() - timestampMS) < range;
}

// a set up function to prep the activity stream provider
function setUpActivityStreamTest() {
  return (async function () {
    await PlacesUtils.history.clear();
    await PlacesUtils.bookmarks.eraseEverything();
    let faviconExpiredPromise = new Promise(resolve => {
      Services.obs.addObserver(resolve, "places-favicons-expired");
    });
    PlacesUtils.favicons.expireAllFavicons();
    await faviconExpiredPromise;
  })();
}

function do_check_links(actualLinks, expectedLinks) {
  Assert.ok(Array.isArray(actualLinks));
  Assert.equal(actualLinks.length, expectedLinks.length);
  for (let i = 0; i < expectedLinks.length; i++) {
    let expected = expectedLinks[i];
    let actual = actualLinks[i];
    Assert.equal(actual.url, expected.url);
    Assert.equal(actual.title, expected.title);
    Assert.equal(actual.frecency, expected.frecency);
    Assert.equal(actual.lastVisitDate, expected.lastVisitDate);
  }
}

function makeLinks(frecRangeStart, frecRangeEnd, step) {
  let links = [];
  // Remember, links are ordered by frecency descending.
  for (let i = frecRangeEnd; i > frecRangeStart; i -= step) {
    links.push(makeLink(i));
  }
  return links;
}

function makeLink(frecency) {
  return {
    url: "http://example" + frecency + ".com/",
    title: "My frecency is " + frecency,
    frecency,
    lastVisitDate: 0,
  };
}

// A small 1x1 test png
const image1x1 =
  "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAAAAAA6fptVAAAACklEQVQI12NgAAAAAgAB4iG8MwAAAABJRU5ErkJggg==";

function getBookmarksSize() {
  return NewTabUtils.activityStreamProvider.executePlacesQuery(
    "SELECT count(*) FROM moz_bookmarks WHERE type = :type",
    { params: { type: PlacesUtils.bookmarks.TYPE_BOOKMARK } }
  );
}

function getHistorySize() {
  return NewTabUtils.activityStreamProvider.executePlacesQuery(
    "SELECT count(*) FROM moz_places WHERE hidden = 0 AND last_visit_date NOT NULL"
  );
}

async function getFrecencyForUrl(url) {
  let db = await PlacesUtils.promiseDBConnection();
  let rows = await db.executeCached(
    `
    SELECT frecency
    FROM moz_places
    WHERE url = :url
    `,
    {
      url,
    }
  );
  Assert.equal(rows?.length, 1, "Should have found a result.");
  return rows[0].getResultByName("frecency");
}

add_task(async function validCacheMidPopulation() {
  let expectedLinks = makeLinks(0, 3, 1);

  let provider = new TestProvider(done => done(expectedLinks));
  provider.maxNumLinks = expectedLinks.length;

  NewTabUtils.initWithoutProviders();
  NewTabUtils.links.addProvider(provider);
  let promise = new Promise(resolve =>
    NewTabUtils.links.populateCache(resolve)
  );

  // isTopSiteGivenProvider() and getProviderLinks() should still return results
  // even when cache is empty or being populated.
  Assert.ok(!NewTabUtils.isTopSiteGivenProvider("example1.com", provider));
  do_check_links(NewTabUtils.getProviderLinks(provider), []);

  await promise;

  // Once the cache is populated, we get the expected results
  Assert.ok(NewTabUtils.isTopSiteGivenProvider("example1.com", provider));
  do_check_links(NewTabUtils.getProviderLinks(provider), expectedLinks);
  NewTabUtils.links.removeProvider(provider);
});

add_task(async function onLinkChangedMidPopulation() {
  let expectedLinks = makeLinks(0, 3, 1);

  let provider = new TestProvider(done => done(expectedLinks));
  provider.maxNumLinks = expectedLinks.length;

  NewTabUtils.initWithoutProviders();
  NewTabUtils.links.addProvider(provider);
  let promise = new Promise(resolve =>
    NewTabUtils.links.populateCache(resolve)
  );

  // onLinkChanged() may fire while the cache is being populated, before
  // linkMap and friends exist. It should silently ignore the change rather
  // than throwing.
  provider.notifyLinkChanged({ url: expectedLinks[0].url, title: "changed" });

  await promise;
  NewTabUtils.links.removeProvider(provider);
});

add_task(async function notifyLinkDelete() {
  let expectedLinks = makeLinks(0, 3, 1);

  let provider = new TestProvider(done => done(expectedLinks));
  provider.maxNumLinks = expectedLinks.length;

  NewTabUtils.initWithoutProviders();
  NewTabUtils.links.addProvider(provider);
  await new Promise(resolve => NewTabUtils.links.populateCache(resolve));

  do_check_links(NewTabUtils.links.getLinks(), expectedLinks);

  // Remove a link.
  let removedLink = expectedLinks[2];
  provider.notifyLinkChanged(removedLink, 2, true);
  let links = NewTabUtils.links._providers.get(provider);

  // Check that sortedLinks is correctly updated.
  do_check_links(NewTabUtils.links.getLinks(), expectedLinks.slice(0, 2));

  // Check that linkMap is accurately updated.
  Assert.equal(links.linkMap.size, 2);
  Assert.ok(links.linkMap.get(expectedLinks[0].url));
  Assert.ok(links.linkMap.get(expectedLinks[1].url));
  Assert.ok(!links.linkMap.get(removedLink.url));

  // Check that siteMap is correctly updated.
  Assert.equal(links.siteMap.size, 2);
  Assert.ok(links.siteMap.has(NewTabUtils.extractSite(expectedLinks[0].url)));
  Assert.ok(links.siteMap.has(NewTabUtils.extractSite(expectedLinks[1].url)));
  Assert.ok(!links.siteMap.has(NewTabUtils.extractSite(removedLink.url)));

  NewTabUtils.links.removeProvider(provider);
});

add_task(async function populatePromise() {
  let count = 0;
  let expectedLinks = makeLinks(0, 10, 2);

  let getLinksFcn = async function (callback) {
    // Should not be calling getLinksFcn twice
    count++;
    Assert.equal(count, 1);
    await Promise.resolve();
    callback(expectedLinks);
  };

  let provider = new TestProvider(getLinksFcn);

  NewTabUtils.initWithoutProviders();
  NewTabUtils.links.addProvider(provider);

  NewTabUtils.links.populateProviderCache(provider, () => {});
  NewTabUtils.links.populateProviderCache(provider, () => {
    do_check_links(NewTabUtils.links.getLinks(), expectedLinks);
    NewTabUtils.links.removeProvider(provider);
  });
});

add_task(async function isTopSiteGivenProvider() {
  let expectedLinks = makeLinks(0, 10, 2);

  // The lowest 2 frecencies have the same base domain.
  expectedLinks[expectedLinks.length - 2].url =
    expectedLinks[expectedLinks.length - 1].url + "Test";

  let provider = new TestProvider(done => done(expectedLinks));
  provider.maxNumLinks = expectedLinks.length;

  NewTabUtils.initWithoutProviders();
  NewTabUtils.links.addProvider(provider);
  await new Promise(resolve => NewTabUtils.links.populateCache(resolve));

  Assert.equal(
    NewTabUtils.isTopSiteGivenProvider("example2.com", provider),
    true
  );
  Assert.equal(
    NewTabUtils.isTopSiteGivenProvider("example1.com", provider),
    false
  );

  // Push out frecency 2 because the maxNumLinks is reached when adding frecency 3
  let newLink = makeLink(3);
  provider.notifyLinkChanged(newLink);

  // There is still a frecent url with example2 domain, so it's still frecent.
  Assert.equal(
    NewTabUtils.isTopSiteGivenProvider("example3.com", provider),
    true
  );
  Assert.equal(
    NewTabUtils.isTopSiteGivenProvider("example2.com", provider),
    true
  );

  // Push out frecency 3
  newLink = makeLink(5);
  provider.notifyLinkChanged(newLink);

  // Push out frecency 4
  newLink = makeLink(9);
  provider.notifyLinkChanged(newLink);

  // Our count reached 0 for the example2.com domain so it's no longer a frecent site.
  Assert.equal(
    NewTabUtils.isTopSiteGivenProvider("example5.com", provider),
    true
  );
  Assert.equal(
    NewTabUtils.isTopSiteGivenProvider("example2.com", provider),
    false
  );

  NewTabUtils.links.removeProvider(provider);
});

add_task(async function multipleProviders() {
  // Make each provider generate NewTabUtils.links.maxNumLinks links to check
  // that no more than maxNumLinks are actually returned in the merged list.
  let evenLinks = makeLinks(0, 2 * NewTabUtils.links.maxNumLinks, 2);
  let evenProvider = new TestProvider(done => done(evenLinks));
  let oddLinks = makeLinks(0, 2 * NewTabUtils.links.maxNumLinks - 1, 2);
  let oddProvider = new TestProvider(done => done(oddLinks));

  NewTabUtils.initWithoutProviders();
  NewTabUtils.links.addProvider(evenProvider);
  NewTabUtils.links.addProvider(oddProvider);

  await new Promise(resolve => NewTabUtils.links.populateCache(resolve));

  let links = NewTabUtils.links.getLinks();
  let expectedLinks = makeLinks(
    NewTabUtils.links.maxNumLinks,
    2 * NewTabUtils.links.maxNumLinks,
    1
  );
  Assert.equal(links.length, NewTabUtils.links.maxNumLinks);
  do_check_links(links, expectedLinks);

  NewTabUtils.links.removeProvider(evenProvider);
  NewTabUtils.links.removeProvider(oddProvider);
});

add_task(async function changeLinks() {
  let expectedLinks = makeLinks(0, 20, 2);
  let provider = new TestProvider(done => done(expectedLinks));

  NewTabUtils.initWithoutProviders();
  NewTabUtils.links.addProvider(provider);

  await new Promise(resolve => NewTabUtils.links.populateCache(resolve));

  do_check_links(NewTabUtils.links.getLinks(), expectedLinks);

  // Notify of a new link.
  let newLink = makeLink(19);
  expectedLinks.splice(1, 0, newLink);
  provider.notifyLinkChanged(newLink);
  do_check_links(NewTabUtils.links.getLinks(), expectedLinks);

  // Notify of a link that's changed sort criteria.
  newLink.frecency = 17;
  expectedLinks.splice(1, 1);
  expectedLinks.splice(2, 0, newLink);
  provider.notifyLinkChanged({
    url: newLink.url,
    frecency: 17,
  });
  do_check_links(NewTabUtils.links.getLinks(), expectedLinks);

  // Notify of a link that's changed title.
  newLink.title = "My frecency is now 17";
  provider.notifyLinkChanged({
    url: newLink.url,
    title: newLink.title,
  });
  do_check_links(NewTabUtils.links.getLinks(), expectedLinks);

  // Notify of a new link again, but this time make it overflow maxNumLinks.
  provider.maxNumLinks = expectedLinks.length;
  newLink = makeLink(21);
  expectedLinks.unshift(newLink);
  expectedLinks.pop();
  Assert.equal(expectedLinks.length, provider.maxNumLinks); // Sanity check.
  provider.notifyLinkChanged(newLink);
  do_check_links(NewTabUtils.links.getLinks(), expectedLinks);

  // Notify of many links changed.
  expectedLinks = makeLinks(0, 3, 1);
  provider.notifyManyLinksChanged();

  // Since _populateProviderCache() is async, we must wait until the provider's
  // populate promise has been resolved.
  await NewTabUtils.links._providers.get(provider).populatePromise;

  // NewTabUtils.links will now repopulate its cache
  do_check_links(NewTabUtils.links.getLinks(), expectedLinks);

  NewTabUtils.links.removeProvider(provider);
});

add_task(async function oneProviderAlreadyCached() {
  let links1 = makeLinks(0, 10, 1);
  let provider1 = new TestProvider(done => done(links1));

  NewTabUtils.initWithoutProviders();
  NewTabUtils.links.addProvider(provider1);

  await new Promise(resolve => NewTabUtils.links.populateCache(resolve));
  do_check_links(NewTabUtils.links.getLinks(), links1);

  let links2 = makeLinks(10, 20, 1);
  let provider2 = new TestProvider(done => done(links2));
  NewTabUtils.links.addProvider(provider2);

  await new Promise(resolve => NewTabUtils.links.populateCache(resolve));
  do_check_links(NewTabUtils.links.getLinks(), links2.concat(links1));

  NewTabUtils.links.removeProvider(provider1);
  NewTabUtils.links.removeProvider(provider2);
});

add_task(async function newLowRankedLink() {
  // Init a provider with 10 links and make its maximum number also 10.
  let links = makeLinks(0, 10, 1);
  let provider = new TestProvider(done => done(links));
  provider.maxNumLinks = links.length;

  NewTabUtils.initWithoutProviders();
  NewTabUtils.links.addProvider(provider);

  await new Promise(resolve => NewTabUtils.links.populateCache(resolve));
  do_check_links(NewTabUtils.links.getLinks(), links);

  // Notify of a new link that's low-ranked enough not to make the list.
  let newLink = makeLink(0);
  provider.notifyLinkChanged(newLink);
  do_check_links(NewTabUtils.links.getLinks(), links);

  // Notify about the new link's title change.
  provider.notifyLinkChanged({
    url: newLink.url,
    title: "a new title",
  });
  do_check_links(NewTabUtils.links.getLinks(), links);

  NewTabUtils.links.removeProvider(provider);
});

add_task(async function extractSite() {
  // All these should extract to the same site
  [
    "mozilla.org",
    "m.mozilla.org",
    "mobile.mozilla.org",
    "www.mozilla.org",
    "www3.mozilla.org",
  ].forEach(host => {
    let url = "http://" + host;
    Assert.equal(
      NewTabUtils.extractSite(url),
      "mozilla.org",
      "extracted same " + host
    );
  });

  // All these should extract to the same subdomain
  ["bugzilla.mozilla.org", "www.bugzilla.mozilla.org"].forEach(host => {
    let url = "http://" + host;
    Assert.equal(
      NewTabUtils.extractSite(url),
      "bugzilla.mozilla.org",
      "extracted eTLD+2 " + host
    );
  });

  // All these should not extract to the same site
  [
    "bugzilla.mozilla.org",
    "bug123.bugzilla.mozilla.org",
    "too.many.levels.bugzilla.mozilla.org",
    "m2.mozilla.org",
    "mobile30.mozilla.org",
    "ww.mozilla.org",
    "ww2.mozilla.org",
    "wwwww.mozilla.org",
    "wwwww50.mozilla.org",
    "wwws.mozilla.org",
    "secure.mozilla.org",
    "secure10.mozilla.org",
    "many.levels.deep.mozilla.org",
    "just.check.in",
    "192.168.0.1",
    "localhost",
  ].forEach(host => {
    let url = "http://" + host;
    Assert.notEqual(
      NewTabUtils.extractSite(url),
      "mozilla.org",
      "extracted diff " + host
    );
  });

  // All these should not extract to the same site
  [
    "about:blank",
    "file:///Users/user/file",
    "chrome://browser/something",
    "ftp://ftp.mozilla.org/",
  ].forEach(url => {
    Assert.notEqual(
      NewTabUtils.extractSite(url),
      "mozilla.org",
      "extracted diff url " + url
    );
  });
});

add_task(async function faviconBytesToDataURI() {
  let tests = [
    [{ favicon: "bar".split("").map(s => s.charCodeAt(0)), mimeType: "foo" }],
    [
      {
        favicon: "bar".split("").map(s => s.charCodeAt(0)),
        mimeType: "foo",
        xxyy: "quz",
      },
    ],
  ];
  let provider = NewTabUtils.activityStreamProvider;

  for (let test of tests) {
    let clone = JSON.parse(JSON.stringify(test));
    delete clone[0].mimeType;
    clone[0].favicon = `data:foo;base64,${btoa("bar")}`;
    let result = provider._faviconBytesToDataURI(test);
    Assert.deepEqual(
      JSON.stringify(clone),
      JSON.stringify(result),
      "favicon converted to data uri"
    );
  }
});

add_task(async function addFavicons() {
  await setUpActivityStreamTest();
  let provider = NewTabUtils.activityStreamProvider;

  // start by passing in a bad uri and check that we get a null favicon back
  let links = [{ url: "mozilla.com" }];
  await provider._addFavicons(links);
  Assert.equal(
    links[0].favicon,
    null,
    "Got a null favicon because we passed in a bad url"
  );
  Assert.equal(
    links[0].mimeType,
    null,
    "Got a null mime type because we passed in a bad url"
  );
  Assert.equal(
    links[0].faviconSize,
    null,
    "Got a null favicon size because we passed in a bad url"
  );

  // now fix the url and try again - this time we get good favicon data back
  // a 1x1 favicon as a data URI of mime type image/png
  const base64URL = image1x1;
  links[0].url = "https://mozilla.com";

  let visit = [
    {
      uri: links[0].url,
      visitDate: timeDaysAgo(0),
      transition: PlacesUtils.history.TRANSITION_TYPED,
    },
  ];
  await PlacesTestUtils.addVisits(visit);

  let faviconData = new Map();
  faviconData.set("https://mozilla.com", `${base64URL}#tippytop`);
  await PlacesTestUtils.addFavicons(faviconData);

  await provider._addFavicons(links);
  Assert.equal(
    links[0].mimeType,
    "image/png",
    "Got the right mime type before deleting it"
  );
  Assert.equal(
    links[0].faviconLength,
    links[0].favicon.length,
    "Got the right length for the byte array"
  );
  Assert.equal(
    provider._faviconBytesToDataURI(links)[0].favicon,
    base64URL,
    "Got the right favicon"
  );
  Assert.equal(
    links[0].faviconSize,
    1,
    "Got the right favicon size (width and height of favicon)"
  );
  Assert.equal(links[0].faviconRef, "tippytop", "Got the favicon url ref");

  // Check with http version of the link that doesn't have its own
  const nonHttps = [{ url: links[0].url.replace("https", "http") }];
  await provider._addFavicons(nonHttps);
  Assert.equal(
    provider._faviconBytesToDataURI(nonHttps)[0].favicon,
    base64URL,
    "Got the same favicon"
  );
  Assert.equal(
    nonHttps[0].faviconLength,
    links[0].faviconLength,
    "Got the same favicon length"
  );
  Assert.equal(
    nonHttps[0].faviconSize,
    links[0].faviconSize,
    "Got the same favicon size"
  );
  Assert.equal(
    nonHttps[0].mimeType,
    links[0].mimeType,
    "Got the same mime type"
  );
});

add_task(async function getHighlights() {
  const addMetadata = url =>
    PlacesUtils.history.update({
      description: "desc",
      previewImageURL: "https://image/",
      url,
    });

  await setUpActivityStreamTest();

  let provider = NewTabUtils.activityStreamLinks;
  let links = await provider.getHighlights();
  Assert.equal(links.length, 0, "empty history yields empty links");

  // Add bookmarks
  const now = Date.now();
  const oldSeconds = 24 * 60 * 60; // 1 day old
  let bookmarks = [
    {
      dateAdded: new Date(now - oldSeconds * 1000),
      parentGuid: PlacesUtils.bookmarks.unfiledGuid,
      title: "foo",
      url: "https://mozilla1.com/dayOld",
    },
    {
      parentGuid: PlacesUtils.bookmarks.unfiledGuid,
      title: "foo",
      url: "https://mozilla1.com/nowNew",
    },
  ];
  for (let placeInfo of bookmarks) {
    await PlacesUtils.bookmarks.insert(placeInfo);
  }

  links = await provider.getHighlights();
  Assert.equal(
    links.length,
    0,
    "adding bookmarks without visits doesn't yield more links"
  );

  // Add a history visit
  let testURI = "http://mozilla.com/";
  await PlacesTestUtils.addVisits(testURI);

  links = await provider.getHighlights();
  Assert.equal(
    links.length,
    0,
    "adding visits without metadata doesn't yield more links"
  );

  // Add bookmark visits
  for (let placeInfo of bookmarks) {
    await PlacesTestUtils.addVisits(placeInfo.url);
  }

  links = await provider.getHighlights();
  Assert.equal(links.length, 2, "adding visits to bookmarks yields more links");
  Assert.equal(
    links[0].url,
    bookmarks[1].url,
    "first bookmark is younger bookmark"
  );
  Assert.equal(links[0].type, "bookmark", "first bookmark is bookmark");
  Assert.ok(links[0].date_added, "got a date_added for the bookmark");
  Assert.equal(
    links[1].url,
    bookmarks[0].url,
    "second bookmark is older bookmark"
  );
  Assert.equal(links[1].type, "bookmark", "second bookmark is bookmark");
  Assert.ok(links[1].date_added, "got a date_added for the bookmark");

  // Add metadata to history
  await addMetadata(testURI);

  links = await provider.getHighlights();
  Assert.equal(links.length, 3, "adding metadata yield more links");
  Assert.equal(links[0].url, bookmarks[1].url, "still have younger bookmark");
  Assert.equal(links[1].url, bookmarks[0].url, "still have older bookmark");
  Assert.equal(links[2].url, testURI, "added visit corresponds to added url");
  Assert.equal(links[2].type, "history", "added visit is history");

  links = await provider.getHighlights({ numItems: 2 });
  Assert.equal(links.length, 2, "limited to 2 items");
  Assert.equal(links[0].url, bookmarks[1].url, "still have younger bookmark");
  Assert.equal(links[1].url, bookmarks[0].url, "still have older bookmark");

  links = await provider.getHighlights({ excludeHistory: true });
  Assert.equal(links.length, 2, "only have bookmarks");
  Assert.equal(links[0].url, bookmarks[1].url, "still have younger bookmark");
  Assert.equal(links[1].url, bookmarks[0].url, "still have older bookmark");

  links = await provider.getHighlights({ excludeBookmarks: true });
  Assert.equal(links.length, 1, "only have history");
  Assert.equal(links[0].url, testURI, "only have the history now");

  links = await provider.getHighlights({
    excludeBookmarks: true,
    excludeHistory: true,
  });
  Assert.equal(links.length, 0, "requested nothing, get nothing");

  links = await provider.getHighlights({ bookmarkSecondsAgo: oldSeconds / 2 });
  Assert.equal(links.length, 2, "old bookmark filtered out with");
  Assert.equal(links[0].url, bookmarks[1].url, "still have newer bookmark");
  Assert.equal(links[1].url, testURI, "still have the history");

  // Add a visit and metadata to the older bookmark
  await PlacesTestUtils.addVisits(bookmarks[0].url);
  await addMetadata(bookmarks[0].url);

  links = await provider.getHighlights({ bookmarkSecondsAgo: oldSeconds / 2 });
  Assert.equal(links.length, 3, "old bookmark returns as history");
  Assert.equal(links[0].url, bookmarks[1].url, "still have newer bookmark");
  Assert.equal(
    links[1].url,
    bookmarks[0].url,
    "old bookmark now is newer history"
  );
  Assert.equal(links[1].type, "history", "old bookmark now is history");
  Assert.equal(links[2].url, testURI, "still have the history");

  // Bookmark the history item
  await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    title: "now a bookmark",
    url: testURI,
  });

  links = await provider.getHighlights();
  Assert.equal(
    links.length,
    3,
    "a visited bookmark doesn't appear as bookmark and history"
  );
  Assert.equal(
    links[0].url,
    testURI,
    "history is now the first, i.e., most recent, bookmark"
  );
  Assert.equal(links[0].type, "bookmark", "was history now bookmark");
  Assert.ok(links[0].date_added, "got a date_added for the now bookmark");
  Assert.equal(
    links[1].url,
    bookmarks[1].url,
    "still have younger bookmark now second"
  );
  Assert.equal(
    links[2].url,
    bookmarks[0].url,
    "still have older bookmark now third"
  );

  // Test the `withFavicons` option.
  await PlacesTestUtils.addFavicons(new Map([[testURI, image1x1]]));
  links = await provider.getHighlights({ withFavicons: true });
  Assert.equal(links.length, 3, "We're not expecting a change in links");
  Assert.equal(links[0].favicon, image1x1, "Link 1 should contain a favicon");
  Assert.equal(links[1].favicon, null, "Link 2 has no favicon data");
  Assert.equal(links[2].favicon, null, "Link 3 has no favicon data");
});

add_task(async function getTopFrecentSites() {
  await setUpActivityStreamTest();

  let provider = NewTabUtils.activityStreamLinks;
  let links = await provider.getTopSites({
    topsiteFrecency: FRECENCY_THRESHOLD,
  });
  Assert.equal(links.length, 0, "empty history yields empty links");

  // add a visit
  let testURI = "http://mozilla.com/";
  await PlacesTestUtils.addVisits(testURI);

  links = await provider.getTopSites();
  Assert.equal(
    links.length,
    0,
    "adding a single visit doesn't exceed default threshold"
  );

  links = await provider.getTopSites({ topsiteFrecency: FRECENCY_THRESHOLD });
  Assert.equal(links.length, 1, "adding a visit yields a link");
  Assert.equal(links[0].url, testURI, "added visit corresponds to added url");
});

add_task(
  {
    skip_if: () =>
      AppConstants.MOZ_APP_NAME == "thunderbird" ||
      Services.prefs.getBoolPref(
        "browser.topsites.useRemoteSetting"
      ) /* see bug 1664502 */,
  },
  async function getTopFrecentSites_improveSearch() {
    await setUpActivityStreamTest();
    const SEARCH_SHORTCUTS_EXPERIMENT_PREF =
      "browser.newtabpage.activity-stream.improvesearch.topSiteSearchShortcuts";
    Services.prefs.setBoolPref(SEARCH_SHORTCUTS_EXPERIMENT_PREF, true);

    let testURI = "https://www.amazon.com?search=tv";
    await PlacesTestUtils.addVisits(testURI);

    let provider = NewTabUtils.activityStreamLinks;
    let links = await provider.getTopSites({
      topsiteFrecency: FRECENCY_THRESHOLD,
    });
    Assert.equal(
      links.length,
      1,
      "sanity check that we got the link from top sites"
    );
    Assert.equal(
      links[0].url,
      "https://amazon.com",
      "the amazon site was converted to generic search shortcut site"
    );

    Services.prefs.setBoolPref(SEARCH_SHORTCUTS_EXPERIMENT_PREF, false);
  }
);

add_task(async function getTopFrecentSites_no_dedup() {
  await setUpActivityStreamTest();

  let provider = NewTabUtils.activityStreamLinks;
  let links = await provider.getTopSites({
    topsiteFrecency: FRECENCY_THRESHOLD,
  });
  Assert.equal(links.length, 0, "empty history yields empty links");

  // Add a visits in reverse order they will be returned in when not deduped.
  let testURIs = [
    { uri: "http://www.mozilla.com/" },
    { uri: "http://mozilla.com/" },
  ];
  await PlacesTestUtils.addVisits(testURIs);

  links = await provider.getTopSites();
  Assert.equal(
    links.length,
    0,
    "adding a single visit doesn't exceed default threshold"
  );

  links = await provider.getTopSites({ topsiteFrecency: FRECENCY_THRESHOLD });
  Assert.equal(links.length, 1, "adding a visit yields a link");
  // Plain domain is returned when deduped.
  Assert.equal(
    links[0].url,
    testURIs[1].uri,
    "added visit corresponds to added url"
  );

  links = await provider.getTopSites({
    topsiteFrecency: FRECENCY_THRESHOLD,
    onePerDomain: false,
  });
  Assert.equal(links.length, 2, "adding a visit yields a link");
  Assert.equal(
    links[0].url,
    testURIs[1].uri,
    "added visit corresponds to added url"
  );
  Assert.equal(
    links[1].url,
    testURIs[0].uri,
    "added visit corresponds to added url"
  );
});

add_task(async function getTopFrecentSites_dedupeWWW() {
  await setUpActivityStreamTest();

  let provider = NewTabUtils.activityStreamLinks;

  let links = await provider.getTopSites({
    topsiteFrecency: FRECENCY_THRESHOLD,
  });
  Assert.equal(links.length, 0, "empty history yields empty links");

  // add a visit without www
  let testURI = "http://mozilla.com/";
  await PlacesTestUtils.addVisits(testURI);
  let frecency1 = await getFrecencyForUrl(testURI);

  // add a visit with www
  testURI = "http://www.mozilla.com/";
  await PlacesTestUtils.addVisits(testURI);
  let frecency2 = await getFrecencyForUrl(testURI);

  // Test combined frecency score
  links = await provider.getTopSites({ topsiteFrecency: FRECENCY_THRESHOLD });
  Assert.equal(links.length, 1, "adding both www. and no-www. yields one link");
  Assert.equal(
    links[0].frecency,
    frecency1 + frecency2,
    "frecency scores are combined"
  );

  // add another page visit with www and without www
  let noWWW = "http://mozilla.com/page";
  await PlacesTestUtils.addVisits(noWWW);
  let noWWWFrecency = await getFrecencyForUrl(noWWW);
  let withWWW = "http://www.mozilla.com/page";
  await PlacesTestUtils.addVisits(withWWW);
  let withWWWFrecency = await getFrecencyForUrl(withWWW);
  links = await provider.getTopSites({ topsiteFrecency: FRECENCY_THRESHOLD });
  Assert.equal(links.length, 1, "adding both www. and no-www. yields one link");
  Assert.equal(
    links[0].frecency,
    noWWWFrecency + withWWWFrecency,
    "frecency scores are combined ignoring extra pages"
  );

  // add another visit with www
  await PlacesTestUtils.addVisits(withWWW);
  withWWWFrecency = await getFrecencyForUrl(withWWW);
  links = await provider.getTopSites({ topsiteFrecency: FRECENCY_THRESHOLD });
  Assert.equal(links.length, 1, "still yields one link");
  Assert.equal(links[0].url, withWWW, "more frecent www link is used");
  Assert.equal(
    links[0].frecency,
    noWWWFrecency + withWWWFrecency,
    "frecency scores are combined ignoring extra pages"
  );

  // add a couple more visits to the no-www page
  await PlacesTestUtils.addVisits(noWWW);
  await PlacesTestUtils.addVisits(noWWW);
  noWWWFrecency = await getFrecencyForUrl(noWWW);
  links = await provider.getTopSites({ topsiteFrecency: FRECENCY_THRESHOLD });
  Assert.equal(links.length, 1, "still yields one link");
  Assert.equal(links[0].url, noWWW, "now more frecent no-www link is used");
  Assert.equal(
    links[0].frecency,
    noWWWFrecency + withWWWFrecency,
    "frecency scores are combined ignoring extra pages"
  );
});

add_task(async function getTopFrencentSites_maxLimit() {
  await setUpActivityStreamTest();

  let provider = NewTabUtils.activityStreamLinks;

  // add many visits
  const MANY_LINKS = 20;
  for (let i = 0; i < MANY_LINKS; i++) {
    let testURI = `http://mozilla${i}.com`;
    await PlacesTestUtils.addVisits(testURI);
  }

  let links = await provider.getTopSites({
    topsiteFrecency: FRECENCY_THRESHOLD,
  });
  Assert.less(
    links.length,
    MANY_LINKS,
    "query default limited to less than many"
  );
  Assert.greater(links.length, 6, "query default to more than visible count");
});

add_task(async function getTopFrencentSites_allowedProtocols() {
  await setUpActivityStreamTest();

  let provider = NewTabUtils.activityStreamLinks;

  // add a visit from a file:// site
  let testURI = "file:///some/file/path.png";
  await PlacesTestUtils.addVisits(testURI);

  let links = await provider.getTopSites({
    topsiteFrecency: FRECENCY_THRESHOLD,
  });
  Assert.equal(links.length, 0, "don't get sites with the file:// protocol");

  // now add a site with an allowed protocol
  testURI = "http://www.mozilla.com";
  await PlacesTestUtils.addVisits(testURI);

  links = await provider.getTopSites({ topsiteFrecency: FRECENCY_THRESHOLD });
  Assert.equal(links.length, 1, "http:// is an allowed protocol");

  // and just to be sure, add a visit to a site with ftp:// protocol
  testURI = "ftp://bad/example";
  await PlacesTestUtils.addVisits(testURI);

  links = await provider.getTopSites({ topsiteFrecency: FRECENCY_THRESHOLD });
  Assert.equal(
    links.length,
    1,
    "we still only accept http:// and https:// for top sites"
  );

  // add a different allowed protocol
  testURI = "https://https";
  await PlacesTestUtils.addVisits(testURI);

  links = await provider.getTopSites({ topsiteFrecency: FRECENCY_THRESHOLD });
  Assert.equal(
    links.length,
    2,
    "we now accept both http:// and https:// for top sites"
  );
});

add_task(async function getTopFrecentSites_order() {
  await setUpActivityStreamTest();

  let provider = NewTabUtils.activityStreamLinks;
  let { TRANSITION_TYPED } = PlacesUtils.history;

  let timeEarlier = timeDaysAgo(0);
  let timeLater = timeDaysAgo(2);

  let visits = [
    // frecency 200
    {
      uri: "https://mozilla1.com/0",
      visitDate: timeEarlier,
      transition: TRANSITION_TYPED,
    },
    // sort by url, frecency 200
    {
      uri: "https://mozilla2.com/1",
      visitDate: timeEarlier,
      transition: TRANSITION_TYPED,
    },
    // sort by last visit date, frecency 200
    {
      uri: "https://mozilla3.com/2",
      visitDate: timeLater,
      transition: TRANSITION_TYPED,
    },
    // sort by frecency, frecency 10
    { uri: "https://mozilla0.com/", visitDate: timeLater },
  ];

  let links = await provider.getTopSites({ topsiteFrecency: 0 });
  Assert.equal(links.length, 0, "empty history yields empty links");

  // map of page url to favicon url
  let faviconData = new Map();
  faviconData.set("https://mozilla3.com/2", image1x1);

  await PlacesTestUtils.addVisits(visits);
  await PlacesTestUtils.addFavicons(faviconData);

  links = await provider.getTopSites({ topsiteFrecency: 0 });
  Assert.equal(
    links.length,
    visits.length,
    "number of links added is the same as obtain by getTopFrecentSites"
  );

  // first link doesn't have a favicon
  Assert.equal(
    links[0].url,
    visits[0].uri,
    "links are obtained in the expected order"
  );
  Assert.equal(null, links[0].favicon, "favicon data is stored as expected");
  Assert.ok(
    isVisitDateOK(links[0].lastVisitDate),
    "visit date within expected range"
  );

  // second link doesn't have a favicon
  Assert.equal(
    links[1].url,
    visits[1].uri,
    "links are obtained in the expected order"
  );
  Assert.equal(null, links[1].favicon, "favicon data is stored as expected");
  Assert.ok(
    isVisitDateOK(links[1].lastVisitDate),
    "visit date within expected range"
  );

  // third link should have the favicon data that we added
  Assert.equal(
    links[2].url,
    visits[2].uri,
    "links are obtained in the expected order"
  );
  Assert.equal(
    faviconData.get(links[2].url),
    links[2].favicon,
    "favicon data is stored as expected"
  );
  Assert.ok(
    isVisitDateOK(links[2].lastVisitDate),
    "visit date within expected range"
  );

  // fourth link doesn't have a favicon
  Assert.equal(
    links[3].url,
    visits[3].uri,
    "links are obtained in the expected order"
  );
  Assert.equal(null, links[3].favicon, "favicon data is stored as expected");
  Assert.ok(
    isVisitDateOK(links[3].lastVisitDate),
    "visit date within expected range"
  );
});

add_task(async function getTopFrecentSites_hideWithSearchParam() {
  await setUpActivityStreamTest();

  let pref = "browser.newtabpage.activity-stream.hideTopSitesWithSearchParam";
  let provider = NewTabUtils.activityStreamLinks;

  // This maps URL search params to objects describing whether a URL with those
  // params is expected to be included in the returned links. Each object maps
  // from effective hide-with search params to whether the URL is expected to be
  // included.
  let tests = {
    "": {
      "": true,
      test: true,
      "test=": true,
      "test=hide": true,
      nomatch: true,
      "nomatch=": true,
      "nomatch=hide": true,
    },
    test: {
      "": true,
      test: false,
      "test=": false,
      "test=hide": true,
      nomatch: true,
      "nomatch=": true,
      "nomatch=hide": true,
    },
    "test=hide": {
      "": true,
      test: false,
      "test=": true,
      "test=hide": false,
      nomatch: true,
      "nomatch=": true,
      "nomatch=hide": true,
    },
    "test=foo&test=hide": {
      "": true,
      test: false,
      "test=": true,
      "test=hide": false,
      nomatch: true,
      "nomatch=": true,
      "nomatch=hide": true,
    },
  };

  for (let [urlParams, expected] of Object.entries(tests)) {
    for (let prefValue of Object.keys(expected)) {
      info(
        "Running test: " + JSON.stringify({ urlParams, prefValue, expected })
      );

      // Add a visit to a URL with search params `urlParams`.
      let url = new URL("http://example.com/");
      url.search = urlParams;
      await PlacesTestUtils.addVisits(url);

      // Set the pref to `prefValue`.
      Services.prefs.setCharPref(pref, prefValue);

      // Call `getTopSites()` with all the test values for `hideWithSearchParam`
      // plus undefined. When `hideWithSearchParam` is undefined, the pref value
      // should be used. Otherwise it should override the pref.
      for (let hideWithSearchParam of [undefined, ...Object.keys(expected)]) {
        info(
          "Calling getTopSites() with hideWithSearchParam: " +
            JSON.stringify(hideWithSearchParam)
        );

        let options = { topsiteFrecency: FRECENCY_THRESHOLD };
        if (hideWithSearchParam !== undefined) {
          options = { ...options, hideWithSearchParam };
        }
        let links = await provider.getTopSites(options);

        let effectiveHideWithParam =
          hideWithSearchParam === undefined ? prefValue : hideWithSearchParam;
        if (expected[effectiveHideWithParam]) {
          Assert.equal(links.length, 1, "One link returned");
          Assert.equal(links[0].url, url.toString(), "Expected link returned");
        } else {
          Assert.equal(links.length, 0, "No links returned");
        }
      }

      await PlacesUtils.history.clear();
    }
  }

  Services.prefs.clearUserPref(pref);
});

add_task(async function activitySteamProvider_deleteHistoryLink() {
  await setUpActivityStreamTest();

  let provider = NewTabUtils.activityStreamLinks;

  let { TRANSITION_TYPED } = PlacesUtils.history;

  let visits = [
    // frecency 200
    {
      uri: "https://mozilla1.com/0",
      visitDate: timeDaysAgo(1),
      transition: TRANSITION_TYPED,
    },
    // sort by url, frecency 200
    { uri: "https://mozilla2.com/1", visitDate: timeDaysAgo(0) },
  ];

  let size = await getHistorySize();
  Assert.equal(size, 0, "empty history has size 0");

  await PlacesTestUtils.addVisits(visits);

  size = await getHistorySize();
  Assert.equal(size, 2, "expected history size");

  // delete a link
  let deleted = await provider.deleteHistoryEntry("https://mozilla2.com/1");
  Assert.equal(deleted, true, "link is deleted");

  // ensure that there's only one link left
  size = await getHistorySize();
  Assert.equal(size, 1, "expected history size");

  // pin the link and delete it
  const linkToPin = { url: "https://mozilla1.com/0" };
  NewTabUtils.pinnedLinks.pin(linkToPin, 0);

  // sanity check that the correct link was pinned
  Assert.equal(
    NewTabUtils.pinnedLinks.links.length,
    1,
    "added a link to pinned sites"
  );
  Assert.equal(
    NewTabUtils.pinnedLinks.isPinned(linkToPin),
    true,
    "pinned the correct link"
  );

  // delete the pinned link and ensure it was both deleted from history and unpinned
  deleted = await provider.deleteHistoryEntry("https://mozilla1.com/0");
  size = await getHistorySize();
  Assert.equal(deleted, true, "link is deleted");
  Assert.equal(size, 0, "expected history size");
  Assert.equal(
    NewTabUtils.pinnedLinks.links.length,
    0,
    "unpinned the deleted link"
  );
});

add_task(async function activityStream_deleteBookmark() {
  await setUpActivityStreamTest();

  let provider = NewTabUtils.activityStreamLinks;
  let bookmarks = [
    {
      url: "https://mozilla1.com/0",
      parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    },
    {
      url: "https://mozilla1.com/1",
      parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    },
  ];

  let bookmarksSize = await getBookmarksSize();
  Assert.equal(bookmarksSize, 0, "empty bookmarks yields 0 size");

  for (let placeInfo of bookmarks) {
    await PlacesUtils.bookmarks.insert(placeInfo);
  }

  bookmarksSize = await getBookmarksSize();
  Assert.equal(bookmarksSize, 2, "size 2 for 2 bookmarks added");

  let bookmarkGuid = await new Promise(resolve =>
    PlacesUtils.bookmarks.fetch({ url: bookmarks[0].url }, bookmark =>
      resolve(bookmark.guid)
    )
  );
  await provider.deleteBookmark(bookmarkGuid);
  Assert.strictEqual(
    await PlacesUtils.bookmarks.fetch(bookmarkGuid),
    null,
    "the bookmark should no longer be found"
  );
  bookmarksSize = await getBookmarksSize();
  Assert.equal(bookmarksSize, 1, "size 1 after deleting");
});

add_task(async function activityStream_blockedURLs() {
  await setUpActivityStreamTest();

  let provider = NewTabUtils.activityStreamLinks;
  NewTabUtils.blockedLinks.addObserver(provider);

  let { TRANSITION_TYPED } = PlacesUtils.history;

  let timeToday = timeDaysAgo(0);
  let timeEarlier = timeDaysAgo(2);

  let visits = [
    {
      uri: "https://example1.com/",
      visitDate: timeToday,
      transition: TRANSITION_TYPED,
    },
    {
      uri: "https://example2.com/",
      visitDate: timeToday,
      transition: TRANSITION_TYPED,
    },
    {
      uri: "https://example3.com/",
      visitDate: timeEarlier,
      transition: TRANSITION_TYPED,
    },
    {
      uri: "https://example4.com/",
      visitDate: timeEarlier,
      transition: TRANSITION_TYPED,
    },
  ];
  await PlacesTestUtils.addVisits(visits);
  await PlacesUtils.bookmarks.insert({
    url: "https://example5.com/",
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
  });

  let sizeQueryResult;

  // bookmarks
  sizeQueryResult = await getBookmarksSize();
  Assert.equal(sizeQueryResult, 1, "got the correct bookmark size");
});

add_task(async function activityStream_getTotalBookmarksCount() {
  await setUpActivityStreamTest();

  let provider = NewTabUtils.activityStreamProvider;
  let bookmarks = [
    {
      url: "https://mozilla1.com/0",
      parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    },
    {
      url: "https://mozilla1.com/1",
      parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    },
  ];

  let bookmarksSize = await provider.getTotalBookmarksCount();
  Assert.equal(
    bookmarksSize,
    0,
    ".getTotalBookmarksCount() returns 0 for an empty bookmarks table"
  );

  for (const bookmark of bookmarks) {
    await PlacesUtils.bookmarks.insert(bookmark);
  }

  bookmarksSize = await provider.getTotalBookmarksCount();
  Assert.equal(
    bookmarksSize,
    2,
    ".getTotalBookmarksCount() returns 2 after 2 bookmarks are inserted"
  );
});

function TestProvider(getLinksFn) {
  this.getLinks = getLinksFn;
  this._observers = new Set();
}

TestProvider.prototype = {
  addObserver(observer) {
    this._observers.add(observer);
  },
  notifyLinkChanged(link, index = -1, deleted = false) {
    this._notifyObservers("onLinkChanged", link, index, deleted);
  },
  notifyManyLinksChanged() {
    this._notifyObservers("onManyLinksChanged");
  },
  _notifyObservers() {
    let observerMethodName = arguments[0];
    let args = Array.prototype.slice.call(arguments, 1);
    args.unshift(this);
    for (let obs of this._observers) {
      if (obs[observerMethodName]) {
        obs[observerMethodName].apply(NewTabUtils.links, args);
      }
    }
  },
};

const puny = "xn--kpry57d";
const idn = "台灣";

add_task(async function test_shortURL() {
  Assert.equal(
    NewTabUtils.shortURL({ url: false }),
    "",
    "Should return a blank string if url is falsey"
  );
  Assert.equal(
    NewTabUtils.shortURL({ url: "" }),
    "",
    "Should return a blank string if url is empty"
  );
  Assert.equal(
    NewTabUtils.shortURL({}),
    "",
    "Should return a blank string if url is missing"
  );

  const invalidURLs = [
    true,
    "something",
    "http:",
    "http::double",
    "http://badport:65536/",
  ];
  invalidURLs.forEach(url =>
    Assert.equal(
      NewTabUtils.shortURL({ url }),
      url,
      `Should return the invalid url as-is: ${url}`
    )
  );

  Assert.equal(
    NewTabUtils.shortURL({ url: "http://com.blah.com" }),
    "com.blah",
    "Should remove the eTLD"
  );

  Assert.equal(
    NewTabUtils.shortURL({ url: `http://${puny}.blah.com` }),
    `${idn}.blah`,
    "Should convert punycode to IDN"
  );

  Assert.equal(
    NewTabUtils.shortURL({ url: "http://bar.com" }),
    "bar",
    "Should get the hostname from .url"
  );

  Assert.equal(
    NewTabUtils.shortURL({ url: "http://foo.www.com" }),
    "foo.www",
    "Should not strip out www if not the first subdomain"
  );

  Assert.equal(
    NewTabUtils.shortURL({ url: "HTTP://FOO.COM" }),
    "foo",
    "Should convert to lowercase"
  );

  Assert.equal(
    NewTabUtils.shortURL({ url: "http://foo.com:8888" }),
    "foo",
    "Should not include the port"
  );

  Assert.equal(
    NewTabUtils.shortURL({ url: "file:///foo/bar.txt" }),
    "/foo/bar.txt",
    "Should return the path for file: URLs"
  );

  Assert.equal(
    NewTabUtils.shortURL({ url: "about:newtab" }),
    "newtab",
    "Should return the content for about: URLs"
  );

  Assert.equal(
    NewTabUtils.shortURL({ url: "about:" }),
    "about:",
    "Should fall back to full URL as a last resort"
  );
});

add_task(async function test_shortHostname() {
  Assert.equal(
    NewTabUtils.shortHostname(""),
    "",
    "Should return a blank string if hostname is empty"
  );

  const invalidHostnames = ["something/something", "http:", "http::double"];
  invalidHostnames.forEach(host =>
    Assert.equal(
      NewTabUtils.shortHostname(host),
      host,
      `Should return the input for invalid hostnames: ${host}`
    )
  );

  Assert.equal(
    NewTabUtils.shortHostname("com.blah.com"),
    "com.blah",
    "Should remove the eTLD"
  );

  Assert.equal(
    NewTabUtils.shortHostname("foo.com/bar"),
    "foo",
    "Should remove eTLD and path"
  );

  Assert.equal(
    NewTabUtils.shortHostname("http://foo.com/"),
    "http://foo",
    "Should remove eTLD and keep protocol"
  );

  Assert.equal(
    NewTabUtils.shortHostname(`${puny}.blah.com`),
    `${idn}.blah`,
    "Should convert punycode to IDN"
  );

  Assert.equal(
    NewTabUtils.shortHostname("foo.www.com"),
    "foo.www",
    "Should not strip out www if not the first subdomain"
  );

  Assert.equal(
    NewTabUtils.shortHostname("FOO.COM"),
    "foo",
    "Should convert to lowercase"
  );

  Assert.equal(
    NewTabUtils.shortHostname("127.0.0.1"),
    "127.0.0.1",
    "Should return hostname for IP address"
  );

  Assert.equal(
    NewTabUtils.shortHostname("www.gov.uk"),
    "gov.uk",
    "Should return the eTLD for www-only non-eTLD"
  );

  Assert.equal(
    NewTabUtils.shortHostname(`www.${puny}`),
    idn,
    "Should return IDN for www-only non-eTLD"
  );
});

add_task(async function getUserMonthlyActivity() {
  await PlacesUtils.history.clear();
  let localDate = date =>
    `${date.getFullYear()}-${String(date.getMonth() + 1).padStart(2, "0")}-${String(date.getDate()).padStart(2, "0")}`;
  let today = new Date();
  let threeDaysAgo = new Date(today);
  threeDaysAgo.setDate(today.getDate() - 3);
  let fortyDaysAgo = new Date(today);
  fortyDaysAgo.setDate(today.getDate() - 40);
  await PlacesTestUtils.addVisits([
    { uri: "https://today1.example.com/", visitDate: today * 1000 },
    { uri: "https://today2.example.com/", visitDate: today * 1000 },
    { uri: "https://recent.example.com/", visitDate: threeDaysAgo * 1000 },
    { uri: "https://old.example.com/", visitDate: fortyDaysAgo * 1000 },
  ]);

  Assert.deepEqual(
    await NewTabUtils.activityStreamProvider.getUserMonthlyActivity(),
    [
      [1, localDate(threeDaysAgo)],
      [2, localDate(today)],
    ],
    "Visits are counted per local day, ignoring visits older than 27 days"
  );

  await PlacesUtils.history.clear();
});
