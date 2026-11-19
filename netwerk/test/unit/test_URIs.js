/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// Run by: cd objdir;  make -C netwerk/test/ xpcshell-tests
// or: cd objdir; make SOLO_FILE="test_URIs.js" -C netwerk/test/ check-one

// See also test_URIs2.js.

// Relevant RFCs: 1738, 1808, 2396, 3986 (newer than the code)
// http://greenbytes.de/tech/webdav/rfc3986.html#rfc.section.5.4
// http://greenbytes.de/tech/tc/uris/

// TEST DATA
// ---------
var gTests = [
  {
    spec: "about:blank",
    scheme: "about",
    prePath: "about:",
    pathQueryRef: "blank",
    ref: "",
    nsIURL: false,
    nsINestedURI: true,
    immutable: true,
  },
  {
    spec: "about:foobar",
    scheme: "about",
    prePath: "about:",
    pathQueryRef: "foobar",
    ref: "",
    nsIURL: false,
    nsINestedURI: false,
    immutable: true,
  },
  {
    spec: "chrome://foobar/somedir/somefile.xml",
    scheme: "chrome",
    prePath: "chrome://foobar",
    pathQueryRef: "/somedir/somefile.xml",
    ref: "",
    nsIURL: true,
    nsINestedURI: false,
    immutable: true,
  },
  {
    spec: "data:text/html;charset=utf-8,<html></html>",
    scheme: "data",
    prePath: "data:",
    pathQueryRef: "text/html;charset=utf-8,<html></html>",
    ref: "",
    nsIURL: false,
    nsINestedURI: false,
  },
  {
    spec: "data:text/html;charset=utf-8,<html>\r\n\t</html>",
    scheme: "data",
    prePath: "data:",
    pathQueryRef: "text/html;charset=utf-8,<html></html>",
    ref: "",
    nsIURL: false,
    nsINestedURI: false,
  },
  {
    spec: "data:text/plain,hello%20world",
    scheme: "data",
    prePath: "data:",
    pathQueryRef: "text/plain,hello%20world",
    ref: "",
    nsIURL: false,
    nsINestedURI: false,
  },
  {
    spec: "data:text/plain,hello world",
    scheme: "data",
    prePath: "data:",
    pathQueryRef: "text/plain,hello world",
    ref: "",
    nsIURL: false,
    nsINestedURI: false,
  },
  {
    spec: "file:///dir/afile",
    scheme: "data",
    prePath: "data:",
    pathQueryRef: "text/plain,2",
    ref: "",
    relativeURI: "data:te\nxt/plain,2",
    nsIURL: false,
    nsINestedURI: false,
  },
  {
    spec: "file://",
    scheme: "file",
    prePath: "file://",
    pathQueryRef: "/",
    ref: "",
    nsIURL: true,
    nsINestedURI: false,
  },
  {
    spec: "file:///",
    scheme: "file",
    prePath: "file://",
    pathQueryRef: "/",
    ref: "",
    nsIURL: true,
    nsINestedURI: false,
  },
  {
    spec: "file:///myFile.html",
    scheme: "file",
    prePath: "file://",
    pathQueryRef: "/myFile.html",
    ref: "",
    nsIURL: true,
    nsINestedURI: false,
  },
  {
    spec: "file:///dir/afile",
    scheme: "file",
    prePath: "file://",
    pathQueryRef: "/dir/data/text/plain,2",
    ref: "",
    relativeURI: "data/text/plain,2",
    nsIURL: true,
    nsINestedURI: false,
  },
  {
    spec: "file:///dir/dir2/",
    scheme: "file",
    prePath: "file://",
    pathQueryRef: "/dir/dir2/data/text/plain,2",
    ref: "",
    relativeURI: "data/text/plain,2",
    nsIURL: true,
    nsINestedURI: false,
  },
  {
    spec: "ftp://ftp.mozilla.org/pub/mozilla.org/README",
    scheme: "ftp",
    prePath: "ftp://ftp.mozilla.org",
    pathQueryRef: "/pub/mozilla.org/README",
    ref: "",
    nsIURL: true,
    nsINestedURI: false,
  },
  {
    spec: "ftp://foo:bar@ftp.mozilla.org:100/pub/mozilla.org/README",
    scheme: "ftp",
    prePath: "ftp://foo:bar@ftp.mozilla.org:100",
    port: 100,
    username: "foo",
    password: "bar",
    pathQueryRef: "/pub/mozilla.org/README",
    ref: "",
    nsIURL: true,
    nsINestedURI: false,
  },
  {
    spec: "ftp://foo:@ftp.mozilla.org:100/pub/mozilla.org/README",
    scheme: "ftp",
    prePath: "ftp://foo@ftp.mozilla.org:100",
    port: 100,
    username: "foo",
    password: "",
    pathQueryRef: "/pub/mozilla.org/README",
    ref: "",
    nsIURL: true,
    nsINestedURI: false,
  },
  //Bug 706249
  {
    spec: "gopher://mozilla.org/",
    scheme: "gopher",
    prePath: "gopher://mozilla.org",
    pathQueryRef: "/",
    ref: "",
    nsIURL: false,
    nsINestedURI: false,
  },
  {
    spec: "http://www.example.com/",
    scheme: "http",
    prePath: "http://www.example.com",
    pathQueryRef: "/",
    ref: "",
    nsIURL: true,
    nsINestedURI: false,
  },
  {
    spec: "http://www.exa\nmple.com/",
    scheme: "http",
    prePath: "http://www.example.com",
    pathQueryRef: "/",
    ref: "",
    nsIURL: true,
    nsINestedURI: false,
  },
  {
    spec: "http://10.32.4.239/",
    scheme: "http",
    prePath: "http://10.32.4.239",
    host: "10.32.4.239",
    pathQueryRef: "/",
    ref: "",
    nsIURL: true,
    nsINestedURI: false,
  },
  {
    spec: "http://[::192.9.5.5]/ipng",
    scheme: "http",
    prePath: "http://[::c009:505]",
    host: "::c009:505",
    pathQueryRef: "/ipng",
    ref: "",
    nsIURL: true,
    nsINestedURI: false,
  },
  {
    spec: "http://[FEDC:BA98:7654:3210:FEDC:BA98:7654:3210]:8888/index.html",
    scheme: "http",
    prePath: "http://[fedc:ba98:7654:3210:fedc:ba98:7654:3210]:8888",
    host: "fedc:ba98:7654:3210:fedc:ba98:7654:3210",
    port: 8888,
    pathQueryRef: "/index.html",
    ref: "",
    nsIURL: true,
    nsINestedURI: false,
  },
  {
    spec: "http://bar:foo@www.mozilla.org:8080/pub/mozilla.org/README.html",
    scheme: "http",
    prePath: "http://bar:foo@www.mozilla.org:8080",
    port: 8080,
    username: "bar",
    password: "foo",
    host: "www.mozilla.org",
    pathQueryRef: "/pub/mozilla.org/README.html",
    ref: "",
    nsIURL: true,
    nsINestedURI: false,
  },
  {
    spec: "jar:resource://!/",
    scheme: "jar",
    prePath: "jar:",
    pathQueryRef: "resource:///!/",
    ref: "",
    nsIURL: true,
    nsINestedURI: true,
  },
  {
    spec: "jar:resource://gre/chrome.toolkit.jar!/",
    scheme: "jar",
    prePath: "jar:",
    pathQueryRef: "resource://gre/chrome.toolkit.jar!/",
    ref: "",
    nsIURL: true,
    nsINestedURI: true,
  },
  {
    spec: "mailto:webmaster@mozilla.com",
    scheme: "mailto",
    prePath: "mailto:",
    pathQueryRef: "webmaster@mozilla.com",
    ref: "",
    nsIURL: false,
    nsINestedURI: false,
  },
  {
    spec: "javascript:new Date()",
    scheme: "javascript",
    prePath: "javascript:",
    pathQueryRef: "new Date()",
    ref: "",
    nsIURL: false,
    nsINestedURI: false,
  },
  {
    spec: "blob:123456",
    scheme: "blob",
    prePath: "blob:",
    pathQueryRef: "123456",
    ref: "",
    nsIURL: false,
    nsINestedURI: false,
    immutable: true,
  },
  {
    spec: "place:sort=8&maxResults=10",
    scheme: "place",
    prePath: "place:",
    pathQueryRef: "sort=8&maxResults=10",
    ref: "",
    nsIURL: false,
    nsINestedURI: false,
  },
  {
    spec: "resource://gre/",
    scheme: "resource",
    prePath: "resource://gre",
    pathQueryRef: "/",
    ref: "",
    nsIURL: true,
    nsINestedURI: false,
  },
  {
    spec: "resource://gre/components/",
    scheme: "resource",
    prePath: "resource://gre",
    pathQueryRef: "/components/",
    ref: "",
    nsIURL: true,
    nsINestedURI: false,
  },

  // Adding more? Consider adding to test_URIs2.js instead, so that neither
  // test runs for *too* long, risking timeouts on slow platforms.
];

var gHashSuffixes = ["#", "#myRef", "#myRef?a=b", "#myRef#", "#myRef#x:yz"];

// TEST HELPER FUNCTIONS
// ---------------------
function do_info(text, stack) {
  if (!stack) {
    stack = Components.stack.caller;
  }

  dump(
    "\n" +
      "TEST-INFO | " +
      stack.filename +
      " | [" +
      stack.name +
      " : " +
      stack.lineNumber +
      "] " +
      text +
      "\n"
  );
}

// Checks that the URIs satisfy equals(), in both possible orderings.
// Also checks URI.equalsExceptRef(), because equal URIs should also be equal
// when we ignore the ref.
//
// The third argument is optional. If the client passes a third argument
// (e.g. todo_check_true), we'll use that in lieu of ok.
function do_check_uri_eq(aURI1, aURI2, aCheckTrueFunc = ok) {
  do_info("(uri equals check: '" + aURI1.spec + "' == '" + aURI2.spec + "')");
  aCheckTrueFunc(aURI1.equals(aURI2));
  do_info("(uri equals check: '" + aURI2.spec + "' == '" + aURI1.spec + "')");
  aCheckTrueFunc(aURI2.equals(aURI1));

  // (Only take the extra step of testing 'equalsExceptRef' when we expect the
  // URIs to really be equal.  In 'todo' cases, the URIs may or may not be
  // equal when refs are ignored - there's no way of knowing in general.)
  if (aCheckTrueFunc == ok) {
    do_check_uri_eqExceptRef(aURI1, aURI2, aCheckTrueFunc);
  }
}

// Checks that the URIs satisfy equalsExceptRef(), in both possible orderings.
//
// The third argument is optional. If the client passes a third argument
// (e.g. todo_check_true), we'll use that in lieu of ok.
function do_check_uri_eqExceptRef(aURI1, aURI2, aCheckTrueFunc = ok) {
  do_info(
    "(uri equalsExceptRef check: '" + aURI1.spec + "' == '" + aURI2.spec + "')"
  );
  aCheckTrueFunc(aURI1.equalsExceptRef(aURI2));
  do_info(
    "(uri equalsExceptRef check: '" + aURI2.spec + "' == '" + aURI1.spec + "')"
  );
  aCheckTrueFunc(aURI2.equalsExceptRef(aURI1));
}

// Checks that the given property on aURI matches the corresponding property
// in the test bundle (or matches some function of that corresponding property,
// if aTestFunctor is passed in).
function do_check_property(aTest, aURI, aPropertyName, aTestFunctor) {
  if (aTest[aPropertyName]) {
    var expectedVal = aTestFunctor
      ? aTestFunctor(aTest[aPropertyName])
      : aTest[aPropertyName];

    do_info(
      "testing " +
        aPropertyName +
        " of " +
        (aTestFunctor ? "modified '" : "'") +
        aTest.spec +
        "' is '" +
        expectedVal +
        "'"
    );
    Assert.equal(aURI[aPropertyName], expectedVal);
  }
}

// Test that a given URI parses correctly into its various components.
function do_test_uri_basic(aTest) {
  var URI;

  do_info(
    "Basic tests for " +
      aTest.spec +
      " relative URI: " +
      (aTest.relativeURI === undefined ? "(none)" : aTest.relativeURI)
  );

  try {
    URI = NetUtil.newURI(aTest.spec);
  } catch (e) {
    do_info("Caught error on parse of" + aTest.spec + " Error: " + e.result);
    if (aTest.fail) {
      Assert.equal(e.result, aTest.result);
      return;
    }
    do_throw(e.result);
  }

  if (aTest.relativeURI) {
    var relURI;

    try {
      relURI = Services.io.newURI(aTest.relativeURI, null, URI);
    } catch (e) {
      do_info(
        "Caught error on Relative parse of " +
          aTest.spec +
          " + " +
          aTest.relativeURI +
          " Error: " +
          e.result
      );
      if (aTest.relativeFail) {
        Assert.equal(e.result, aTest.relativeFail);
        return;
      }
      do_throw(e.result);
    }
    do_info(
      "relURI.pathQueryRef = " +
        relURI.pathQueryRef +
        ", was " +
        URI.pathQueryRef
    );
    URI = relURI;
    do_info("URI.pathQueryRef now = " + URI.pathQueryRef);
  }

  // Sanity-check
  do_info("testing " + aTest.spec + " equals a clone of itself");
  do_check_uri_eq(URI, URI.mutate().finalize());
  do_check_uri_eqExceptRef(URI, URI.mutate().setRef("").finalize());
  do_info("testing " + aTest.spec + " instanceof nsIURL");
  Assert.equal(URI instanceof Ci.nsIURL, aTest.nsIURL);
  do_info("testing " + aTest.spec + " instanceof nsINestedURI");
  Assert.equal(URI instanceof Ci.nsINestedURI, aTest.nsINestedURI);

  do_info(
    "testing that " +
      aTest.spec +
      " throws or returns false " +
      "from equals(null)"
  );
  // XXXdholbert At some point it'd probably be worth making this behavior
  // (throwing vs. returning false) consistent across URI implementations.
  var threw = false;
  var isEqualToNull;
  try {
    isEqualToNull = URI.equals(null);
  } catch (e) {
    threw = true;
  }
  Assert.ok(threw || !isEqualToNull);

  // Check the various components
  do_check_property(aTest, URI, "scheme");
  do_check_property(aTest, URI, "prePath");
  do_check_property(aTest, URI, "pathQueryRef");
  do_check_property(aTest, URI, "query");
  do_check_property(aTest, URI, "ref");
  do_check_property(aTest, URI, "port");
  do_check_property(aTest, URI, "username");
  do_check_property(aTest, URI, "password");
  do_check_property(aTest, URI, "host");
  do_check_property(aTest, URI, "specIgnoringRef");

  do_info("testing hasRef");
  Assert.equal(URI.hasRef, !!aTest.ref, "URI.hasRef is correct");
  do_info("testing hasUserPass");
  Assert.equal(
    URI.hasUserPass,
    !!aTest.username || !!aTest.password,
    "URI.hasUserPass is correct"
  );
}

// Test that a given URI parses correctly when we add a given ref to the end
function do_test_uri_with_hash_suffix(aTest, aSuffix) {
  do_info("making sure caller is using suffix that starts with '#'");
  Assert.equal(aSuffix[0], "#");

  var origURI = NetUtil.newURI(aTest.spec);
  var testURI;

  if (aTest.relativeURI) {
    try {
      origURI = Services.io.newURI(aTest.relativeURI, null, origURI);
    } catch (e) {
      do_info(
        "Caught error on Relative parse of " +
          aTest.spec +
          " + " +
          aTest.relativeURI +
          " Error: " +
          e.result
      );
      return;
    }
    try {
      testURI = Services.io.newURI(aSuffix, null, origURI);
    } catch (e) {
      do_info(
        "Caught error adding suffix to " +
          aTest.spec +
          " + " +
          aTest.relativeURI +
          ", suffix " +
          aSuffix +
          " Error: " +
          e.result
      );
      return;
    }
  } else {
    testURI = NetUtil.newURI(aTest.spec + aSuffix);
  }

  do_info(
    "testing " +
      aTest.spec +
      " with '" +
      aSuffix +
      "' appended " +
      "equals a clone of itself"
  );
  do_check_uri_eq(testURI, testURI.mutate().finalize());

  do_info(
    "testing " +
      aTest.spec +
      " doesn't equal self with '" +
      aSuffix +
      "' appended"
  );

  Assert.ok(!origURI.equals(testURI));

  do_info(
    "testing " +
      aTest.spec +
      " is equalExceptRef to self with '" +
      aSuffix +
      "' appended"
  );
  do_check_uri_eqExceptRef(origURI, testURI);

  Assert.equal(testURI.hasRef, true);

  if (!origURI.ref) {
    // These tests fail if origURI has a ref
    do_info(
      "testing setRef('') on " +
        testURI.spec +
        " is equal to no-ref version but not equal to ref version"
    );
    var cloneNoRef = testURI.mutate().setRef("").finalize(); // we used to clone here.
    do_info("cloneNoRef: " + cloneNoRef.spec + " hasRef: " + cloneNoRef.hasRef);
    do_info("testURI: " + testURI.spec + " hasRef: " + testURI.hasRef);
    do_check_uri_eq(cloneNoRef, origURI);
    Assert.ok(!cloneNoRef.equals(testURI));

    do_info(
      "testing cloneWithNewRef on " +
        testURI.spec +
        " with an empty ref is equal to no-ref version but not equal to ref version"
    );
    var cloneNewRef = testURI.mutate().setRef("").finalize();
    do_check_uri_eq(cloneNewRef, origURI);
    do_check_uri_eq(cloneNewRef, cloneNoRef);
    Assert.ok(!cloneNewRef.equals(testURI));

    do_info(
      "testing cloneWithNewRef on " +
        origURI.spec +
        " with the same new ref is equal to ref version and not equal to no-ref version"
    );
    cloneNewRef = origURI.mutate().setRef(aSuffix).finalize();
    do_check_uri_eq(cloneNewRef, testURI);
    Assert.ok(cloneNewRef.equals(testURI));
  }

  do_check_property(aTest, testURI, "scheme");
  do_check_property(aTest, testURI, "prePath");
  if (!origURI.ref) {
    // These don't work if it's a ref already because '+' doesn't give the right result
    do_check_property(aTest, testURI, "pathQueryRef", function (aStr) {
      return aStr + aSuffix;
    });
    do_check_property(aTest, testURI, "ref", function () {
      return aSuffix.substr(1);
    });
  }
}

// Tests various ways of setting & clearing a ref on a URI.
function do_test_mutate_ref(aTest, aSuffix) {
  do_info("making sure caller is using suffix that starts with '#'");
  Assert.equal(aSuffix[0], "#");

  var refURIWithSuffix = NetUtil.newURI(aTest.spec + aSuffix);
  var refURIWithoutSuffix = NetUtil.newURI(aTest.spec);

  var testURI = NetUtil.newURI(aTest.spec);

  // First: Try setting .ref to our suffix
  do_info(
    "testing that setting .ref on " +
      aTest.spec +
      " to '" +
      aSuffix +
      "' does what we expect"
  );
  testURI = testURI.mutate().setRef(aSuffix).finalize();
  do_check_uri_eq(testURI, refURIWithSuffix);
  do_check_uri_eqExceptRef(testURI, refURIWithoutSuffix);

  // Now try setting .ref but leave off the initial hash (expect same result)
  var suffixLackingHash = aSuffix.substr(1);
  if (suffixLackingHash) {
    // (skip this our suffix was *just* a #)
    do_info(
      "testing that setting .ref on " +
        aTest.spec +
        " to '" +
        suffixLackingHash +
        "' does what we expect"
    );
    testURI = testURI.mutate().setRef(suffixLackingHash).finalize();
    do_check_uri_eq(testURI, refURIWithSuffix);
    do_check_uri_eqExceptRef(testURI, refURIWithoutSuffix);
  }

  // Now, clear .ref (should get us back the original spec)
  do_info(
    "testing that clearing .ref on " + testURI.spec + " does what we expect"
  );
  testURI = testURI.mutate().setRef("").finalize();
  do_check_uri_eq(testURI, refURIWithoutSuffix);
  do_check_uri_eqExceptRef(testURI, refURIWithSuffix);

  if (!aTest.relativeURI) {
    // TODO: These tests don't work as-is for relative URIs.

    // Now try setting .spec directly (including suffix) and then clearing .ref
    var specWithSuffix = aTest.spec + aSuffix;
    do_info(
      "testing that setting spec to " +
        specWithSuffix +
        " and then clearing ref does what we expect"
    );

    testURI = testURI.mutate().setSpec(specWithSuffix).setRef("").finalize();
    do_check_uri_eq(testURI, refURIWithoutSuffix);
    do_check_uri_eqExceptRef(testURI, refURIWithSuffix);

    // XXX nsIJARURI throws an exception in SetPath(), so skip it for next part.
    if (!(testURI instanceof Ci.nsIJARURI)) {
      // Now try setting .pathQueryRef directly (including suffix) and then clearing .ref
      // (same as above, but with now with .pathQueryRef instead of .spec)
      testURI = NetUtil.newURI(aTest.spec);

      var pathWithSuffix = aTest.pathQueryRef + aSuffix;
      do_info(
        "testing that setting path to " +
          pathWithSuffix +
          " and then clearing ref does what we expect"
      );
      testURI = testURI
        .mutate()
        .setPathQueryRef(pathWithSuffix)
        .setRef("")
        .finalize();
      do_check_uri_eq(testURI, refURIWithoutSuffix);
      do_check_uri_eqExceptRef(testURI, refURIWithSuffix);

      // Also: make sure that clearing .pathQueryRef also clears .ref
      testURI = testURI.mutate().setPathQueryRef(pathWithSuffix).finalize();
      do_info(
        "testing that clearing path from " +
          pathWithSuffix +
          " also clears .ref"
      );
      testURI = testURI.mutate().setPathQueryRef("").finalize();
      Assert.equal(testURI.ref, "");
    }
  }
}

// Check that changing nested/about URIs works correctly.
add_task(function check_nested_mutations() {
  // nsNestedAboutURI
  let uri1 = Services.io.newURI("about:blank#");
  let uri2 = Services.io.newURI("about:blank");
  let uri3 = uri1.mutate().setRef("").finalize();
  do_check_uri_eq(uri3, uri2);
  uri3 = uri2.mutate().setRef("#").finalize();
  do_check_uri_eq(uri3, uri1);

  uri1 = Services.io.newURI("about:blank?something");
  uri2 = Services.io.newURI("about:blank");
  uri3 = uri1.mutate().setQuery("").finalize();
  do_check_uri_eq(uri3, uri2);
  uri3 = uri2.mutate().setQuery("something").finalize();
  do_check_uri_eq(uri3, uri1);

  uri1 = Services.io.newURI("about:blank?query#ref");
  uri2 = Services.io.newURI("about:blank");
  uri3 = uri1.mutate().setPathQueryRef("blank").finalize();
  do_check_uri_eq(uri3, uri2);
  uri3 = uri2.mutate().setPathQueryRef("blank?query#ref").finalize();
  do_check_uri_eq(uri3, uri1);

  // nsSimpleNestedURI
  uri1 = Services.io.newURI("view-source:http://example.com/path#");
  uri2 = Services.io.newURI("view-source:http://example.com/path");
  uri3 = uri1.mutate().setRef("").finalize();
  do_check_uri_eq(uri3, uri2);
  uri3 = uri2.mutate().setRef("#").finalize();
  do_check_uri_eq(uri3, uri1);

  uri1 = Services.io.newURI("view-source:http://example.com/path?something");
  uri2 = Services.io.newURI("view-source:http://example.com/path");
  uri3 = uri1.mutate().setQuery("").finalize();
  do_check_uri_eq(uri3, uri2);
  uri3 = uri2.mutate().setQuery("something").finalize();
  do_check_uri_eq(uri3, uri1);

  uri1 = Services.io.newURI("view-source:http://example.com/path?query#ref");
  uri2 = Services.io.newURI("view-source:http://example.com/path");
  uri3 = uri1.mutate().setPathQueryRef("path").finalize();
  do_check_uri_eq(uri3, uri2);
  uri3 = uri2.mutate().setPathQueryRef("path?query#ref").finalize();
  do_check_uri_eq(uri3, uri1);

  uri1 = Services.io.newURI("view-source:about:blank#");
  uri2 = Services.io.newURI("view-source:about:blank");
  uri3 = uri1.mutate().setRef("").finalize();
  do_check_uri_eq(uri3, uri2);
  uri3 = uri2.mutate().setRef("#").finalize();
  do_check_uri_eq(uri3, uri1);

  uri1 = Services.io.newURI("view-source:about:blank?something");
  uri2 = Services.io.newURI("view-source:about:blank");
  uri3 = uri1.mutate().setQuery("").finalize();
  do_check_uri_eq(uri3, uri2);
  uri3 = uri2.mutate().setQuery("something").finalize();
  do_check_uri_eq(uri3, uri1);

  uri1 = Services.io.newURI("view-source:about:blank?query#ref");
  uri2 = Services.io.newURI("view-source:about:blank");
  uri3 = uri1.mutate().setPathQueryRef("blank").finalize();
  do_check_uri_eq(uri3, uri2);
  uri3 = uri2.mutate().setPathQueryRef("blank?query#ref").finalize();
  do_check_uri_eq(uri3, uri1);
});

add_task(function check_space_escaping() {
  let uri = Services.io.newURI("data:text/plain,hello%20world#space hash");
  Assert.equal(uri.spec, "data:text/plain,hello%20world#space%20hash");
  uri = Services.io.newURI("data:text/plain,hello%20world#space%20hash");
  Assert.equal(uri.spec, "data:text/plain,hello%20world#space%20hash");
  uri = Services.io.newURI("data:text/plain,hello world#space%20hash");
  Assert.equal(uri.spec, "data:text/plain,hello world#space%20hash");
  uri = Services.io.newURI("data:text/plain,hello world#space hash");
  Assert.equal(uri.spec, "data:text/plain,hello world#space%20hash");
  uri = Services.io.newURI("http://example.com/test path#test path");
  uri = Services.io.newURI("http://example.com/test%20path#test%20path");
});

add_task(function check_space_with_query_and_ref() {
  let url = Services.io.newURI("data:space");
  Assert.equal(url.spec, "data:space");

  url = Services.io.newURI("data:space ?");
  Assert.equal(url.spec, "data:space ?");
  url = Services.io.newURI("data:space #");
  Assert.equal(url.spec, "data:space #");

  url = Services.io.newURI("data:space?");
  Assert.equal(url.spec, "data:space?");
  url = Services.io.newURI("data:space#");
  Assert.equal(url.spec, "data:space#");

  url = Services.io.newURI("data:space ?query");
  Assert.equal(url.spec, "data:space ?query");
  url = url.mutate().setQuery("").finalize();
  Assert.equal(url.spec, "data:space");

  url = Services.io.newURI("data:space #ref");
  Assert.equal(url.spec, "data:space #ref");
  url = url.mutate().setRef("").finalize();
  Assert.equal(url.spec, "data:space");

  url = Services.io.newURI("data:space?query#ref");
  Assert.equal(url.spec, "data:space?query#ref");
  url = url.mutate().setRef("").finalize();
  Assert.equal(url.spec, "data:space?query");
  url = url.mutate().setQuery("").finalize();
  Assert.equal(url.spec, "data:space");

  url = Services.io.newURI("data:space#ref?query");
  Assert.equal(url.spec, "data:space#ref?query");
  url = url.mutate().setQuery("").finalize();
  Assert.equal(url.spec, "data:space#ref?query");
  url = url.mutate().setRef("").finalize();
  Assert.equal(url.spec, "data:space");

  url = Services.io.newURI("data:space ?query#ref");
  Assert.equal(url.spec, "data:space ?query#ref");
  url = url.mutate().setRef("").finalize();
  Assert.equal(url.spec, "data:space ?query");
  url = url.mutate().setQuery("").finalize();
  Assert.equal(url.spec, "data:space");

  url = Services.io.newURI("data:space #ref?query");
  Assert.equal(url.spec, "data:space #ref?query");
  url = url.mutate().setQuery("").finalize();
  Assert.equal(url.spec, "data:space #ref?query");
  url = url.mutate().setRef("").finalize();
  Assert.equal(url.spec, "data:space");
});

add_task(function check_schemeIsNull() {
  let uri = Services.io.newURI("data:text/plain,aaa");
  Assert.ok(!uri.schemeIs(null));
  uri = Services.io.newURI("http://example.com");
  Assert.ok(!uri.schemeIs(null));
  uri = Services.io.newURI("dummyscheme://example.com");
  Assert.ok(!uri.schemeIs(null));
  uri = Services.io.newURI("jar:resource://gre/chrome.toolkit.jar!/");
  Assert.ok(!uri.schemeIs(null));
  uri = Services.io.newURI("moz-icon://.unknown?size=32");
  Assert.ok(!uri.schemeIs(null));
});

// Check that characters in the query of moz-extension aren't improperly unescaped (Bug 1547882)
add_task(function check_mozextension_query() {
  let uri = Services.io.newURI(
    "moz-extension://a7d1572e-3beb-4d93-a920-c408fa09e8ea/_source/holding.html"
  );
  uri = uri
    .mutate()
    .setQuery("u=https%3A%2F%2Fnews.ycombinator.com%2F")
    .finalize();
  Assert.equal(uri.query, "u=https%3A%2F%2Fnews.ycombinator.com%2F");
  uri = Services.io.newURI(
    "moz-extension://a7d1572e-3beb-4d93-a920-c408fa09e8ea/_source/holding.html?u=https%3A%2F%2Fnews.ycombinator.com%2F"
  );
  Assert.equal(
    uri.spec,
    "moz-extension://a7d1572e-3beb-4d93-a920-c408fa09e8ea/_source/holding.html?u=https%3A%2F%2Fnews.ycombinator.com%2F"
  );
  Assert.equal(uri.query, "u=https%3A%2F%2Fnews.ycombinator.com%2F");
});

add_task(function check_resolve() {
  let base = Services.io.newURI("http://example.com");
  let uri = Services.io.newURI("tel::+371 27028456", "utf-8", base);
  Assert.equal(uri.spec, "tel::+371 27028456");
});

add_task(function test_extra_protocols() {
  // dweb://
  let url = Services.io.newURI("dweb://example.com/test");
  Assert.equal(url.host, "example.com");

  // dat://
  url = Services.io.newURI(
    "dat://41f8a987cfeba80a037e51cc8357d513b62514de36f2f9b3d3eeec7a8fb3b5a5/"
  );
  Assert.equal(
    url.host,
    "41f8a987cfeba80a037e51cc8357d513b62514de36f2f9b3d3eeec7a8fb3b5a5"
  );
  url = Services.io.newURI("dat://example.com/test");
  Assert.equal(url.host, "example.com");

  // ipfs://
  url = Services.io.newURI(
    "ipfs://bafybeiccfclkdtucu6y4yc5cpr6y3yuinr67svmii46v5cfcrkp47ihehy/frontend/license.txt"
  );
  Assert.equal(url.scheme, "ipfs");
  Assert.equal(
    url.host,
    "bafybeiccfclkdtucu6y4yc5cpr6y3yuinr67svmii46v5cfcrkp47ihehy"
  );
  Assert.equal(url.filePath, "/frontend/license.txt");

  // ipns://
  url = Services.io.newURI("ipns://peerdium.gozala.io/index.html");
  Assert.equal(url.scheme, "ipns");
  Assert.equal(url.host, "peerdium.gozala.io");
  Assert.equal(url.filePath, "/index.html");

  // ssb://
  url = Services.io.newURI("ssb://scuttlebutt.nz/index.html");
  Assert.equal(url.scheme, "ssb");
  Assert.equal(url.host, "scuttlebutt.nz");
  Assert.equal(url.filePath, "/index.html");

  // wtp://
  url = Services.io.newURI(
    "wtp://951ead31d09e4049fc1f21f137e233dd0589fcbd/blog/vim-tips/"
  );
  Assert.equal(url.scheme, "wtp");
  Assert.equal(url.host, "951ead31d09e4049fc1f21f137e233dd0589fcbd");
  Assert.equal(url.filePath, "/blog/vim-tips/");
});

// TEST MAIN FUNCTION
// ------------------
add_task(function mainTest() {
  // UTF-8 check - From bug 622981
  // ASCII
  let base = Services.io.newURI("http://example.org/xenia?");
  let resolved = Services.io.newURI("?x", null, base);
  let expected = Services.io.newURI("http://example.org/xenia?x");
  do_info(
    "Bug 662981: ACSII - comparing " + resolved.spec + " and " + expected.spec
  );
  Assert.ok(resolved.equals(expected));

  // UTF-8 character "è"
  // Bug 622981 was triggered by an empty query string
  base = Services.io.newURI("http://example.org/xènia?");
  resolved = Services.io.newURI("?x", null, base);
  expected = Services.io.newURI("http://example.org/xènia?x");
  do_info(
    "Bug 662981: UTF8 - comparing " + resolved.spec + " and " + expected.spec
  );
  Assert.ok(resolved.equals(expected));

  gTests.forEach(function (aTest) {
    // Check basic URI functionality
    do_test_uri_basic(aTest);

    if (!aTest.fail) {
      // Try adding various #-prefixed strings to the ends of the URIs
      gHashSuffixes.forEach(function (aSuffix) {
        do_test_uri_with_hash_suffix(aTest, aSuffix);
        if (!aTest.immutable) {
          do_test_mutate_ref(aTest, aSuffix);
        }
      });

      // For URIs that we couldn't mutate above due to them being immutable:
      // Now we check that they're actually immutable.
      if (aTest.immutable) {
        Assert.ok(aTest.immutable);
      }
    }
  });
});

function check_round_trip_serialization(spec) {
  dump(`checking ${spec}\n`);
  let uri = Services.io.newURI(spec);
  let str = serialize_to_escaped_string(uri);
  let other = deserialize_from_escaped_string(str).QueryInterface(Ci.nsIURI);
  equal(other.spec, uri.spec);
}

add_task(function test_iconURI_serialization() {
  // URIs taken from test_moz_icon_uri.js

  let tests = [
    "moz-icon://foo.html?contentType=bar&size=button&state=normal",
    "moz-icon://foo.html?size=3",
    "moz-icon://stock/foo",
    "moz-icon:file://foo.txt",
    "moz-icon://file://foo.txt",
  ];

  tests.forEach(str => check_round_trip_serialization(str));
});

add_task(function test_jarURI_serialization() {
  check_round_trip_serialization("jar:http://example.com/bar.jar!/");
});

add_task(async function round_trip_invalid_ace_label() {
  // This is well-formed punycode, but an invalid ACE label due to hyphens in
  // positions 3 & 4 and trailing hyphen. (Punycode-decode yields "xn--d淾-")
  let uri = Services.io.newURI("http://xn--xn--d--fg4n/");
  Assert.equal(uri.spec, "http://xn--xn--d--fg4n/");

  // ASCII domains never fail IDNA, even with invalid punycode.
  // https://url.spec.whatwg.org/#concept-domain-to-ascii
  uri = Services.io.newURI("http://a.b.c.XN--pokxncvks");
  Assert.equal(uri.spec, "http://a.b.c.xn--pokxncvks/");
  Assert.equal(uri.displayHost, "a.b.c.xn--pokxncvks");

  let tests = [
    ["xn--ab-j1t", "xn--ab-j1t"],
    ["XN--A.pt", "xn--a.pt"],
    ["xn--", "xn--"],
    ["xn--ASCII-", "xn--ascii-"],
    ["xn--1ug.example", "xn--1ug.example"],
    ["xn--ls8h=", "xn--ls8h="],
  ];
  for (let [input, expected] of tests) {
    uri = Services.io.newURI(`https://${input}/x`);
    Assert.equal(uri.asciiHost, expected, input);
    Assert.equal(uri.host, expected, input);
    Assert.equal(uri.spec, `https://${expected}/x`, input);
  }

  // Forbidden domain code points still fail.
  Assert.throws(() => {
    Services.io.newURI("http://xn--a%5E/");
  }, /NS_ERROR_MALFORMED_URI/);
});

add_task(async function test_bug1875119() {
  let uri1 = Services.io.newURI("file:///path");
  let uri2 = Services.io.newURI("resource://test/bla");
  // type of uri2 is still SubstitutingURL which overrides the implementation of ResolveFile,
  // but it's scheme is now file.
  // See https://bugzilla.mozilla.org/show_bug.cgi?id=1876483 to disallow this
  uri2 = uri2.mutate().setSpec("file:///path2").finalize();
  // NOTE: this test was originally expecting `uri1.equals(uri2)` to be throwing
  // a NS_NOINTERFACE error (instead of hitting a crash) when the new test landed
  // as part of Bug 1875119, then as a side-effect of the fix applied by Bug 1926106
  // the expected behavior is for the call to not raise an NS_NOINTERFACE error anymore
  // but to be returning false instead (and so the test has been adjusted accordingly).
  Assert.ok(!uri1.equals(uri2), "Expect uri1.equals(uri2) to be false");
});

add_task(async function test_file_url_equality_does_not_resolve_files() {
  const pairs = [
    ["file:///foo/bar/", "file:///foo/bar"],
    ["file:///foo/b%61r", "file:///foo/bar"],
    ["jar:file:///foo/bar.jar/!/x", "jar:file:///foo/bar.jar!/x"],
    ["jar:file:///foo/b%61r.jar!/x", "jar:file:///foo/bar.jar!/x"],
  ];
  for (let [a, b] of pairs) {
    let uri1 = Services.io.newURI(a);
    let uri2 = Services.io.newURI(b);
    Assert.ok(!uri1.equals(uri2), `${a} != ${b}`);
    Assert.ok(!uri2.equals(uri1), `${b} != ${a}`);
    Assert.ok(!uri1.equalsExceptRef(uri2), `${a} != ${b} (except ref)`);
  }
});

add_task(async function test_bug1843717() {
  // Make sure file path normalization on windows
  // doesn't affect the hash of the URL.
  let base = Services.io.newURI("file:///abc\\def/");
  let uri = Services.io.newURI("foo\\bar#x\\y", null, base);
  Assert.equal(uri.spec, "file:///abc/def/foo/bar#x\\y");
  uri = Services.io.newURI("foo\\bar#xy", null, base);
  Assert.equal(uri.spec, "file:///abc/def/foo/bar#xy");
  uri = Services.io.newURI("foo\\bar#", null, base);
  Assert.equal(uri.spec, "file:///abc/def/foo/bar#");
});

add_task(async function test_bug1874118() {
  let base = Services.io.newURI("file:///tmp/mock/path");
  let uri = Services.io.newURI("file:c:\\\\foo\\\\bar.html", null, base);
  Assert.equal(uri.spec, "file:///c://foo//bar.html");

  base = Services.io.newURI("file:///tmp/mock/path");
  uri = Services.io.newURI("file:c|\\\\foo\\\\bar.html", null, base);
  Assert.equal(uri.spec, "file:///c://foo//bar.html");

  base = Services.io.newURI("file:///C:/");
  uri = Services.io.newURI("..", null, base);
  Assert.equal(uri.spec, "file:///C:/");

  base = Services.io.newURI("file:///");
  uri = Services.io.newURI("C|/hello/../../", null, base);
  Assert.equal(uri.spec, "file:///C:/");

  base = Services.io.newURI("file:///");
  uri = Services.io.newURI("/C:/../../", null, base);
  Assert.equal(uri.spec, "file:///C:/");

  base = Services.io.newURI("file:///");
  uri = Services.io.newURI("/C://../../", null, base);
  Assert.equal(uri.spec, "file:///C:/");

  base = Services.io.newURI("file:///tmp/mock/path");
  uri = Services.io.newURI("C|/foo/bar", null, base);
  Assert.equal(uri.spec, "file:///C:/foo/bar");

  base = Services.io.newURI("file:///tmp/mock/path");
  uri = Services.io.newURI("/C|/foo/bar", null, base);
  Assert.equal(uri.spec, "file:///C:/foo/bar");
});

add_task(async function test_bug1911529() {
  let testcases = [
    [
      "https://github.com/coder/coder/edit/main/docs/./enterprise.md",
      "https://github.com/coder/coder/edit/main/docs/enterprise.md",
      "enterprise",
    ],
    ["https://domain.com/.", "https://domain.com/", ""],
    ["https://domain.com/%2e", "https://domain.com/", ""],
    ["https://domain.com/%2e%2E", "https://domain.com/", ""],
    ["https://domain.com/%2e%2E/.", "https://domain.com/", ""],
    ["https://domain.com/./test.md", "https://domain.com/test.md", "test"],
    [
      "https://domain.com/dir/sub/%2e%2e/%2e/test.md",
      "https://domain.com/dir/test.md",
      "test",
    ],
    ["https://domain.com/dir/..", "https://domain.com/", ""],
  ];

  for (let t of testcases) {
    let uri = Services.io.newURI(t[0]);
    let uri2 = Services.io.newURI(t[1]);
    Assert.ok(uri.equals(uri2), `${uri} must equal ${uri2}`);
    Assert.equal(t[2], uri.QueryInterface(Ci.nsIURL).fileBaseName);
  }
});

add_task(async function test_bug1939493() {
  let uri = Services.io.newURI("resource:///components/");
  uri = uri
    .mutate()
    .setUserPass("")
    .setUsername("")
    .setPassword("")
    .setPort(-1)
    .finalize();
  Assert.equal(uri.spec, "resource:///components/");

  Assert.throws(() => {
    uri = uri.mutate().setUserPass("a:b").finalize();
  }, /NS_ERROR_UNEXPECTED/);
  Assert.throws(() => {
    uri = uri.mutate().setUsername("a").finalize();
  }, /NS_ERROR_UNEXPECTED/);
  Assert.throws(() => {
    uri = uri.mutate().setPassword("b").finalize();
  }, /NS_ERROR_UNEXPECTED/);
  Assert.throws(() => {
    uri = uri.mutate().setPort(10).finalize();
  }, /NS_ERROR_UNEXPECTED/);

  // username, password and port doesn't really make sense for a resource URL
  // but they still behave as regular URLs so this should be valid.
  uri = uri.mutate().setHost("gre").finalize();
  Assert.equal(uri.spec, "resource://gre/components/");
  uri = uri.mutate().setUserPass("a:b").finalize();
  Assert.equal(uri.spec, "resource://a:b@gre/components/");
  uri = uri.mutate().setUsername("user").finalize();
  Assert.equal(uri.spec, "resource://user:b@gre/components/");
  uri = uri.mutate().setPassword("pass").finalize();
  Assert.equal(uri.spec, "resource://user:pass@gre/components/");
  uri = uri.mutate().setPort(10).finalize();
  Assert.equal(uri.spec, "resource://user:pass@gre:10/components/");

  // Clearing the host should fail, as there are still user, port, pass in play.
  Assert.throws(() => {
    uri = uri.mutate().setHost("").finalize();
  }, /NS_ERROR_MALFORMED_URI/);

  uri = uri
    .mutate()
    .setUserPass("")
    .setUsername("")
    .setPassword("")
    .setPort(-1)
    .setHost("")
    .finalize();

  Assert.equal(uri.spec, "resource:///components/");
});
