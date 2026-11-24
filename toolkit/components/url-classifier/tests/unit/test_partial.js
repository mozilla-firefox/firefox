/**
 * DummyCompleter() lets tests easily specify the results of a partial
 * hash completion request.
 */
function DummyCompleter() {
  this.fragments = {};
  this.queries = [];
  this.tableName = "test-phish-simple";
}

DummyCompleter.prototype = {
  QueryInterface: ChromeUtils.generateQI(["nsIUrlClassifierHashCompleter"]),

  complete(partialHash, gethashUrl, tableName, useOHTTP, cb) {
    this.queries.push(partialHash);
    var fragments = this.fragments;
    var self = this;
    var doCallback = function () {
      if (self.alwaysFail) {
        cb.completionFinished(Cr.NS_ERROR_FAILURE);
        return;
      }
      if (fragments[partialHash]) {
        for (var i = 0; i < fragments[partialHash].length; i++) {
          var chunkId = fragments[partialHash][i][0];
          var hash = fragments[partialHash][i][1];
          cb.completionV2(hash, self.tableName, chunkId);
        }
      }
      cb.completionFinished(0);
    };
    executeSoon(doCallback);
  },

  getHash(fragment) {
    var data = new TextEncoder().encode(fragment);
    var ch = Cc["@mozilla.org/security/hash;1"].createInstance(
      Ci.nsICryptoHash
    );
    ch.init(ch.SHA256);
    ch.update(data, data.length);
    var hash = ch.finish(false);
    return hash.slice(0, 32);
  },

  addFragment(chunkId, fragment) {
    this.addHash(chunkId, this.getHash(fragment));
  },

  // This method allows the caller to generate complete hashes that match the
  // prefix of a real fragment, but have different complete hashes.
  addConflict(chunkId, fragment) {
    var realHash = this.getHash(fragment);
    var invalidHash = this.getHash("blah blah blah blah blah");
    this.addHash(chunkId, realHash.slice(0, 4) + invalidHash.slice(4, 32));
  },

  addHash(chunkId, hash) {
    var partial = hash.slice(0, 4);
    if (this.fragments[partial]) {
      this.fragments[partial].push([chunkId, hash]);
    } else {
      this.fragments[partial] = [[chunkId, hash]];
    }
  },

  compareQueries(fragments) {
    var expectedQueries = [];
    for (let i = 0; i < fragments.length; i++) {
      expectedQueries.push(this.getHash(fragments[i]).slice(0, 4));
    }
    Assert.equal(this.queries.length, expectedQueries.length);
    expectedQueries.sort();
    this.queries.sort();
    for (let i = 0; i < this.queries.length; i++) {
      Assert.equal(this.queries[i], expectedQueries[i]);
    }
  },
};

function setupCompleter(table, hits, conflicts) {
  var completer = new DummyCompleter();
  completer.tableName = table;
  for (let i = 0; i < hits.length; i++) {
    let chunkId = hits[i][0];
    let fragments = hits[i][1];
    for (let j = 0; j < fragments.length; j++) {
      completer.addFragment(chunkId, fragments[j]);
    }
  }
  for (let i = 0; i < conflicts.length; i++) {
    let chunkId = conflicts[i][0];
    let fragments = conflicts[i][1];
    for (let j = 0; j < fragments.length; j++) {
      completer.addConflict(chunkId, fragments[j]);
    }
  }

  dbservice.setHashCompleter(table, completer);

  return completer;
}

function installCompleter(table, fragments, conflictFragments) {
  return setupCompleter(table, fragments, conflictFragments);
}

function installFailingCompleter(table) {
  var completer = setupCompleter(table, [], []);
  completer.alwaysFail = true;
  return completer;
}

// Helper assertion for checking dummy completer queries
gAssertions.completerQueried = function (data, cb) {
  var completer = data[0];
  completer.compareQueries(data[1]);
  cb();
};

function doTest(updates, assertions) {
  doUpdateTest(updates, assertions, runNextTest, updateError);
}

// Test an add of two partial urls to a fresh database
function testPartialAdds() {
  var addUrls = ["foo.com/a", "foo.com/b", "bar.com/c"];
  var update = buildPhishingUpdate([{ chunkNum: 1, urls: addUrls }], 4);

  var completer = installCompleter("test-phish-simple", [[1, addUrls]], []);

  var assertions = {
    tableData: "test-phish-simple;a:1",
    urlsExist: addUrls,
    completerQueried: [completer, addUrls],
  };

  doTest([update], assertions);
}

function testPartialAddsWithConflicts() {
  var addUrls = ["foo.com/a", "foo.com/b", "bar.com/c"];
  var update = buildPhishingUpdate([{ chunkNum: 1, urls: addUrls }], 4);

  // Each result will have both a real match and a conflict
  var completer = installCompleter(
    "test-phish-simple",
    [[1, addUrls]],
    [[1, addUrls]]
  );

  var assertions = {
    tableData: "test-phish-simple;a:1",
    urlsExist: addUrls,
    completerQueried: [completer, addUrls],
  };

  doTest([update], assertions);
}

// Test whether the fragmenting code does not cause duplicated completions
function testFragments() {
  var addUrls = ["foo.com/a/b/c", "foo.net/", "foo.com/c/"];
  var update = buildPhishingUpdate([{ chunkNum: 1, urls: addUrls }], 4);

  var completer = installCompleter("test-phish-simple", [[1, addUrls]], []);

  var assertions = {
    tableData: "test-phish-simple;a:1",
    urlsExist: addUrls,
    completerQueried: [completer, addUrls],
  };

  doTest([update], assertions);
}

// Test http://code.google.com/p/google-safe-browsing/wiki/Protocolv2Spec
// section 6.2 example 1
function testSpecFragments() {
  var probeUrls = ["a.b.c/1/2.html?param=1"];

  var addUrls = [
    "a.b.c/1/2.html",
    "a.b.c/",
    "a.b.c/1/",
    "b.c/1/2.html?param=1",
    "b.c/1/2.html",
    "b.c/",
    "b.c/1/",
    "a.b.c/1/2.html?param=1",
  ];

  var update = buildPhishingUpdate([{ chunkNum: 1, urls: addUrls }], 4);

  var completer = installCompleter("test-phish-simple", [[1, addUrls]], []);

  var assertions = {
    tableData: "test-phish-simple;a:1",
    urlsExist: probeUrls,
    completerQueried: [completer, addUrls],
  };

  doTest([update], assertions);
}

// Test http://code.google.com/p/google-safe-browsing/wiki/Protocolv2Spec
// section 6.2 example 2
function testMoreSpecFragments() {
  var probeUrls = ["a.b.c.d.e.f.g/1.html"];

  var addUrls = [
    "a.b.c.d.e.f.g/1.html",
    "a.b.c.d.e.f.g/",
    "c.d.e.f.g/1.html",
    "c.d.e.f.g/",
    "d.e.f.g/1.html",
    "d.e.f.g/",
    "e.f.g/1.html",
    "e.f.g/",
    "f.g/1.html",
    "f.g/",
  ];

  var update = buildPhishingUpdate([{ chunkNum: 1, urls: addUrls }], 4);

  var completer = installCompleter("test-phish-simple", [[1, addUrls]], []);

  var assertions = {
    tableData: "test-phish-simple;a:1",
    urlsExist: probeUrls,
    completerQueried: [completer, addUrls],
  };

  doTest([update], assertions);
}

function testFalsePositives() {
  var addUrls = ["foo.com/a", "foo.com/b", "bar.com/c"];
  var update = buildPhishingUpdate([{ chunkNum: 1, urls: addUrls }], 4);

  // Each result will have no matching complete hashes and a non-matching
  // conflict
  var completer = installCompleter("test-phish-simple", [], [[1, addUrls]]);

  var assertions = {
    tableData: "test-phish-simple;a:1",
    urlsDontExist: addUrls,
    completerQueried: [completer, addUrls],
  };

  doTest([update], assertions);
}

function testEmptyCompleter() {
  var addUrls = ["foo.com/a", "foo.com/b", "bar.com/c"];
  var update = buildPhishingUpdate([{ chunkNum: 1, urls: addUrls }], 4);

  // Completer will never return full hashes
  var completer = installCompleter("test-phish-simple", [], []);

  var assertions = {
    tableData: "test-phish-simple;a:1",
    urlsDontExist: addUrls,
    completerQueried: [completer, addUrls],
  };

  doTest([update], assertions);
}

function testCompleterFailure() {
  var addUrls = ["foo.com/a", "foo.com/b", "bar.com/c"];
  var update = buildPhishingUpdate([{ chunkNum: 1, urls: addUrls }], 4);

  // Completer will never return full hashes
  var completer = installFailingCompleter("test-phish-simple");

  var assertions = {
    tableData: "test-phish-simple;a:1",
    urlsDontExist: addUrls,
    completerQueried: [completer, addUrls],
  };

  doTest([update], assertions);
}

function testMixedSizesSameDomain() {
  var add1Urls = ["foo.com/a"];
  var add2Urls = ["foo.com/b"];

  var update1 = buildPhishingUpdate([{ chunkNum: 1, urls: add1Urls }], 4);
  var update2 = buildPhishingUpdate([{ chunkNum: 2, urls: add2Urls }], 32);

  // We should only need to complete the partial hashes
  var completer = installCompleter("test-phish-simple", [[1, add1Urls]], []);

  var assertions = {
    tableData: "test-phish-simple;a:1-2",
    // both urls should match...
    urlsExist: add1Urls.concat(add2Urls),
    // ... but the completer should only be queried for the partial entry
    completerQueried: [completer, add1Urls],
  };

  doTest([update1, update2], assertions);
}

function testMixedSizesDifferentDomains() {
  var add1Urls = ["foo.com/a"];
  var add2Urls = ["bar.com/b"];

  var update1 = buildPhishingUpdate([{ chunkNum: 1, urls: add1Urls }], 4);
  var update2 = buildPhishingUpdate([{ chunkNum: 2, urls: add2Urls }], 32);

  // We should only need to complete the partial hashes
  var completer = installCompleter("test-phish-simple", [[1, add1Urls]], []);

  var assertions = {
    tableData: "test-phish-simple;a:1-2",
    // both urls should match...
    urlsExist: add1Urls.concat(add2Urls),
    // ... but the completer should only be queried for the partial entry
    completerQueried: [completer, add1Urls],
  };

  doTest([update1, update2], assertions);
}

function testInvalidHashSize() {
  var addUrls = ["foo.com/a", "foo.com/b", "bar.com/c"];
  var update = buildPhishingUpdate([{ chunkNum: 1, urls: addUrls }], 12); // only 4 and 32 are legal hash sizes

  var addUrls2 = ["zaz.com/a", "xyz.com/b"];
  var update2 = buildPhishingUpdate([{ chunkNum: 2, urls: addUrls2 }], 4);

  installCompleter("test-phish-simple", [[1, addUrls]], []);

  var assertions = {
    tableData: "test-phish-simple;a:2",
    urlsDontExist: addUrls,
  };

  // A successful update will trigger an error
  doUpdateTest([update2, update], assertions, updateError, runNextTest);
}

function testWrongTable() {
  var addUrls = ["foo.com/a"];
  var update = buildPhishingUpdate([{ chunkNum: 1, urls: addUrls }], 4);
  var completer = installCompleter(
    "test-malware-simple", // wrong table
    [[1, addUrls]],
    []
  );

  // The above installCompleter installs the completer for test-malware-simple,
  // we want it to be used for test-phish-simple too.
  dbservice.setHashCompleter("test-phish-simple", completer);

  var assertions = {
    tableData: "test-phish-simple;a:1",
    // The urls were added as phishing urls, but the completer is claiming
    // that they are malware urls, and we trust the completer in this case.
    // The result will be discarded, so we can only check for non-existence.
    urlsDontExist: addUrls,
    // Make sure the completer was actually queried.
    completerQueried: [completer, addUrls],
  };

  doUpdateTest(
    [update],
    assertions,
    function () {
      // Give the dbservice a chance to (not) cache the result.
      do_timeout(3000, function () {
        // The miss earlier will have caused a miss to be cached.
        // Resetting the completer does not count as an update,
        // so we will not be probed again.
        var newCompleter = installCompleter(
          "test-malware-simple",
          [[1, addUrls]],
          []
        );
        dbservice.setHashCompleter("test-phish-simple", newCompleter);

        var assertions1 = {
          urlsDontExist: addUrls,
        };
        checkAssertions(assertions1, runNextTest);
      });
    },
    updateError
  );
}

function setupCachedResults(addUrls, part2) {
  var update = buildPhishingUpdate([{ chunkNum: 1, urls: addUrls }], 4);

  var completer = installCompleter("test-phish-simple", [[1, addUrls]], []);

  var assertions = {
    tableData: "test-phish-simple;a:1",
    // Request the add url.  This should cause the completion to be cached.
    urlsExist: addUrls,
    // Make sure the completer was actually queried.
    completerQueried: [completer, addUrls],
  };

  doUpdateTest(
    [update],
    assertions,
    function () {
      // Give the dbservice a chance to cache the result.
      do_timeout(3000, part2);
    },
    updateError
  );
}

function testCachedResults() {
  setupCachedResults(["foo.com/a"], function () {
    // This is called after setupCachedResults().  Verify that
    // checking the url again does not cause a completer request.

    // install a new completer, this one should never be queried.
    var newCompleter = installCompleter("test-phish-simple", [[1, []]], []);

    var assertions = {
      urlsExist: ["foo.com/a"],
      completerQueried: [newCompleter, []],
    };
    checkAssertions(assertions, runNextTest);
  });
}

function testCachedResultsWithSub() {
  setupCachedResults(["foo.com/a"], function () {
    // install a new completer, this one should never be queried.
    var newCompleter = installCompleter("test-phish-simple", [[1, []]], []);

    var removeUpdate = buildPhishingUpdate(
      [{ chunkNum: 2, chunkType: "s", urls: ["1:foo.com/a"] }],
      4
    );

    var assertions = {
      urlsDontExist: ["foo.com/a"],
      completerQueried: [newCompleter, []],
    };

    doTest([removeUpdate], assertions);
  });
}

function testCachedResultsWithExpire() {
  setupCachedResults(["foo.com/a"], function () {
    // install a new completer, this one should never be queried.
    var newCompleter = installCompleter("test-phish-simple", [[1, []]], []);

    var expireUpdate = "n:1000\ni:test-phish-simple\nad:1\n";

    var assertions = {
      urlsDontExist: ["foo.com/a"],
      completerQueried: [newCompleter, []],
    };
    doTest([expireUpdate], assertions);
  });
}

function testCachedResultsFailure() {
  var existUrls = ["foo.com/a"];
  setupCachedResults(existUrls, function () {
    // This is called after setupCachedResults().  Verify that
    // checking the url again does not cause a completer request.

    // install a new completer, this one should never be queried.
    var newCompleter = installCompleter("test-phish-simple", [[1, []]], []);

    var assertions = {
      urlsExist: existUrls,
      completerQueried: [newCompleter, []],
    };

    checkAssertions(assertions, function () {
      // Apply the update. The cached completes should be gone.
      doErrorUpdate(
        "test-phish-simple,test-malware-simple",
        function () {
          // Now the completer gets queried again.
          var newCompleter2 = installCompleter(
            "test-phish-simple",
            [[1, existUrls]],
            []
          );
          var assertions2 = {
            tableData: "test-phish-simple;a:1",
            urlsExist: existUrls,
            completerQueried: [newCompleter2, existUrls],
          };
          checkAssertions(assertions2, runNextTest);
        },
        updateError
      );
    });
  });
}

function testErrorList() {
  var addUrls = ["foo.com/a", "foo.com/b", "bar.com/c"];
  var update = buildPhishingUpdate([{ chunkNum: 1, urls: addUrls }], 4);
  // The update failure should will kill the completes, so the above
  // must be a prefix to get any hit at all past the update failure.

  var completer = installCompleter("test-phish-simple", [[1, addUrls]], []);

  var assertions = {
    tableData: "test-phish-simple;a:1",
    urlsExist: addUrls,
    // These are complete urls, and will only be completed if the
    // list is stale.
    completerQueried: [completer, addUrls],
  };

  // Apply the update.
  doStreamUpdate(
    update,
    function () {
      // Now the test-phish-simple and test-malware-simple tables are marked
      // as fresh.  Fake an update failure to mark them stale.
      doErrorUpdate(
        "test-phish-simple,test-malware-simple",
        function () {
          // Now the lists should be marked stale.  Check assertions.
          checkAssertions(assertions, runNextTest);
        },
        updateError
      );
    },
    updateError
  );
}

// Verify that different lists (test-phish-simple,
// test-malware-simple) maintain their freshness separately.
function testErrorListIndependent() {
  var phishUrls = ["phish.com/a"];
  var malwareUrls = ["attack.com/a"];
  var update = buildPhishingUpdate([{ chunkNum: 1, urls: phishUrls }], 4);
  // These have to persist past the update failure, so they must be prefixes,
  // not completes.

  update += buildMalwareUpdate([{ chunkNum: 2, urls: malwareUrls }], 32);

  var completer = installCompleter("test-phish-simple", [[1, phishUrls]], []);

  var assertions = {
    tableData: "test-malware-simple;a:2\ntest-phish-simple;a:1",
    urlsExist: phishUrls,
    malwareUrlsExist: malwareUrls,
    // Only this phishing urls should be completed, because only the phishing
    // urls will be stale.
    completerQueried: [completer, phishUrls],
  };

  // Apply the update.
  doStreamUpdate(
    update,
    function () {
      // Now the test-phish-simple and test-malware-simple tables are
      // marked as fresh.  Fake an update failure to mark *just*
      // phishing data as stale.
      doErrorUpdate(
        "test-phish-simple",
        function () {
          // Now the lists should be marked stale.  Check assertions.
          checkAssertions(assertions, runNextTest);
        },
        updateError
      );
    },
    updateError
  );
}

function run_test() {
  runTests([
    testPartialAdds,
    testPartialAddsWithConflicts,
    testFragments,
    testSpecFragments,
    testMoreSpecFragments,
    testFalsePositives,
    testEmptyCompleter,
    testCompleterFailure,
    testMixedSizesSameDomain,
    testMixedSizesDifferentDomains,
    testInvalidHashSize,
    testWrongTable,
    testCachedResults,
    testCachedResultsWithSub,
    testCachedResultsWithExpire,
    testCachedResultsFailure,
    testErrorList,
    testErrorListIndependent,
  ]);
}

do_test_pending();
