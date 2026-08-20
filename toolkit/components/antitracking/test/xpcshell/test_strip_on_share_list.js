"use strict";

const stripperService = Cc[
  "@mozilla.org/url-query-string-stripper;1"
].getService(Ci.nsIURLQueryStringStripper);

function setup(testJson) {
  let stringifiedRules = [];
  Object.values(testJson).forEach(rule => {
    stringifiedRules.push(JSON.stringify(rule));
  });

  const observerService = stripperService.QueryInterface(
    Ci.nsIURLQueryStrippingListObserver
  );

  observerService.onStripOnShareUpdate(stringifiedRules);
}

let testJson = {
  global: {
    queryParams: ["utm_ad"],
    isGlobal: true,
  },
  example: {
    queryParams: ["test_2", "test_1"],
    origins: ["www.example.com"],
  },
  testparam: {
    queryParams: ["testparam"],
    origins: ["testparamexample.com"],
  },
  exampleNet: {
    queryParams: ["test_3", "test_4"],
    origins: ["www.example.net"],
  },
  exampleRoot: {
    queryParams: ["rootparam"],
    schemelessSites: ["example.com"],
  },
  exampleOrgUk: {
    queryParams: ["rootparam2"],
    schemelessSites: ["example.org.uk"],
  },
};

add_setup(async function () {
  Services.prefs.setBoolPref(
    "privacy.query_stripping.strip_on_share.enabled",
    true
  );

  setup(testJson);
});

// Test that strip on share list gets cleared between updates
add_task(async function test_striponshare_rules_cleared() {
  Assert.equal(
    stripperService.stripForCopyOrShare(
      Services.io.newURI("https://testparamexample.com/?testparam=1")
    ).spec,
    "https://testparamexample.com/"
  );

  let testJson2 = {
    example2: {
      queryParams: ["testparam"],
      origins: ["example2.com"],
    },
  };

  setup(testJson2);

  Assert.equal(
    stripperService.stripForCopyOrShare(
      Services.io.newURI("https://testparamexample.com/?testparam=1")
    ),
    null
  );
  Assert.equal(
    stripperService.stripForCopyOrShare(
      Services.io.newURI("https://example2.com/?testparam=1")
    ).spec,
    "https://example2.com/"
  );

  setup(testJson);
});

// Test schemelessSites rules
add_task(async function test_striponshare_schemeless_sites() {
  Assert.equal(
    stripperService.stripForCopyOrShare(
      Services.io.newURI("https://example.com/?rootParam=1")
    ).spec,
    "https://example.com/"
  );

  Assert.equal(
    stripperService.stripForCopyOrShare(
      Services.io.newURI("https://www.example.com/?rootParam=1")
    ).spec,
    "https://www.example.com/"
  );

  Assert.equal(
    stripperService.stripForCopyOrShare(
      Services.io.newURI("https://test1.example.com/?rootParam=1")
    ).spec,
    "https://test1.example.com/"
  );

  Assert.equal(
    stripperService.stripForCopyOrShare(
      Services.io.newURI("https://abc.test1.example.com/?rootParam=1")
    ).spec,
    "https://abc.test1.example.com/"
  );

  Assert.equal(
    stripperService.stripForCopyOrShare(
      Services.io.newURI("https://abc.test1.example.com/?rootParam2=1")
    ),
    null
  );

  Assert.equal(
    stripperService.stripForCopyOrShare(
      Services.io.newURI("https://abc.test1.example.org.uk/?rootParam2=1")
    ).spec,
    "https://abc.test1.example.org.uk/"
  );
});
