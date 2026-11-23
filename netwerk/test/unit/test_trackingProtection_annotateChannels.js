"use strict";

const { UrlClassifierTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/UrlClassifierTestUtils.sys.mjs"
);
const { HttpServer } = ChromeUtils.importESModule(
  "resource://testing-common/httpd.sys.mjs"
);

// This test supports both e10s and non-e10s mode. In non-e10s mode, this test
// drives itself by creating a profile directory, setting up the URL classifier
// test tables and adjusting the prefs which are necessary to do the testing.
// In e10s mode however, some of these operations such as creating a profile
// directory, setting up the URL classifier test tables and setting prefs
// aren't supported in the content process, so we split the test into two
// parts, the part testing the normal priority case by setting both prefs to
// false (test_trackingProtection_annotateChannels_wrap1.js), and the part
// testing the lowest priority case by setting both prefs to true
// (test_trackingProtection_annotateChannels_wrap2.js).  These wrapper scripts
// are also in charge of creating the profile directory and setting up the URL
// classifier test tables.
//
// Below where we need to take different actions based on the process type we're
// in, we use runtime.processType to take the correct actions.

const runtime = Services.appinfo;
if (runtime.processType == runtime.PROCESS_TYPE_DEFAULT) {
  do_get_profile();
}

const defaultTopWindowURI = NetUtil.newURI("http://www.example.com/");

function listener(tracking, priority, throttleable, nextTest) {
  this._tracking = tracking;
  this._priority = priority;
  this._throttleable = throttleable;
  this._nextTest = nextTest;
}
listener.prototype = {
  onStartRequest(request) {
    Assert.equal(
      request
        .QueryInterface(Ci.nsIClassifiedChannel)
        .isThirdPartyTrackingResource(),
      this._tracking,
      "tracking flag"
    );
    Assert.equal(
      request.QueryInterface(Ci.nsISupportsPriority).priority,
      this._priority,
      "channel priority"
    );
    if (runtime.processType == runtime.PROCESS_TYPE_DEFAULT && this._tracking) {
      Assert.equal(
        !!(
          request.QueryInterface(Ci.nsIClassOfService).classFlags &
          Ci.nsIClassOfService.Throttleable
        ),
        this._throttleable,
        "throttleable flag"
      );
    }
    request.cancel(Cr.NS_ERROR_ABORT);
    this._nextTest();
  },
  onDataAvailable: () => {},
  onStopRequest: () => {},
};

var httpServer;
var normalOrigin, trackingOrigin;
var testPriorityMap;
var currentTest;
// When this test is running in e10s mode, the parent process is in charge of
// setting the prefs for us, so here we merely read our prefs, and if they have
// been set we skip the normal priority test and only test the lowest priority
// case, and if it they have not been set we skip the lowest priority test and
// only test the normal priority case.
// In non-e10s mode, both of these will remain false and we adjust the prefs
// ourselves and test both of the cases in one go.
var skipNormalPriority = false,
  skipLowestPriority = false;

function setup_test() {
  httpServer = new HttpServer();
  httpServer.start(-1);
  httpServer.identity.setPrimary(
    "http",
    "tracking.example.org",
    httpServer.identity.primaryPort
  );
  httpServer.identity.add(
    "http",
    "example.org",
    httpServer.identity.primaryPort
  );
  normalOrigin = "http://localhost:" + httpServer.identity.primaryPort;
  trackingOrigin =
    "http://tracking.example.org:" + httpServer.identity.primaryPort;

  if (runtime.processType == runtime.PROCESS_TYPE_CONTENT) {
    if (
      Services.prefs.getBoolPref(
        "privacy.trackingprotection.annotate_channels"
      ) &&
      Services.prefs.getBoolPref(
        "privacy.trackingprotection.lower_network_priority"
      )
    ) {
      skipNormalPriority = true;
    } else {
      skipLowestPriority = true;
    }
  }

  runTests();
}

function doPriorityTest() {
  if (!testPriorityMap.length) {
    runTests();
    return;
  }

  currentTest = testPriorityMap.shift();

  // Let's be explicit about what we're testing!
  Assert.ok(
    "loadingPrincipal" in currentTest,
    "check for incomplete test case"
  );
  Assert.ok("topWindowURI" in currentTest, "check for incomplete test case");

  var channel = makeChannel(
    currentTest.path,
    currentTest.loadingPrincipal,
    currentTest.topWindowURI
  );
  channel.asyncOpen(
    new listener(
      currentTest.expectedTracking,
      currentTest.expectedPriority,
      currentTest.expectedThrottleable,
      doPriorityTest
    )
  );
}

function makeChannel(path, loadingPrincipal, topWindowURI) {
  var chan;

  if (loadingPrincipal) {
    chan = NetUtil.newChannel({
      uri: path,
      loadingPrincipal,
      securityFlags: Ci.nsILoadInfo.SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
      contentPolicyType: Ci.nsIContentPolicy.TYPE_OTHER,
    });
  } else {
    chan = NetUtil.newChannel({
      uri: path,
      loadUsingSystemPrincipal: true,
    });
  }
  chan.QueryInterface(Ci.nsIHttpChannel);
  chan.requestMethod = "GET";
  if (topWindowURI) {
    chan
      .QueryInterface(Ci.nsIHttpChannelInternal)
      .setTopWindowURIIfUnknown(topWindowURI);
  }
  return chan;
}

var tests = [
  // Create the HTTP server.
  setup_test,

  // Add the test table into tracking protection table.
  function addTestTrackers() {
    if (runtime.processType == runtime.PROCESS_TYPE_DEFAULT) {
      UrlClassifierTestUtils.addTestTrackers().then(() => {
        runTests();
      });
    } else {
      runTests();
    }
  },

  // Annotations OFF, normal loading principal, topWinURI of example.com
  // => trackers should not be de-prioritized
  function setupAnnotationsOff() {
    if (skipNormalPriority) {
      runTests();
      return;
    }
    if (runtime.processType == runtime.PROCESS_TYPE_DEFAULT) {
      Services.prefs.setBoolPref(
        "privacy.trackingprotection.annotate_channels",
        false
      );
      Services.prefs.setBoolPref(
        "privacy.trackingprotection.lower_network_priority",
        false
      );
    }
    var principal =
      Services.scriptSecurityManager.createContentPrincipalFromOrigin(
        normalOrigin
      );
    testPriorityMap = [
      {
        path: normalOrigin + "/innocent.css",
        loadingPrincipal: principal,
        topWindowURI: defaultTopWindowURI,
        expectedTracking: false,
        expectedPriority: Ci.nsISupportsPriority.PRIORITY_NORMAL,
        expectedThrottleable: false, // ignored since tracking==false
      },
      {
        path: normalOrigin + "/innocent.js",
        loadingPrincipal: principal,
        topWindowURI: defaultTopWindowURI,
        expectedTracking: false,
        expectedPriority: Ci.nsISupportsPriority.PRIORITY_NORMAL,
        expectedThrottleable: false, // ignored since tracking==false
      },
      {
        path: trackingOrigin + "/evil.css",
        loadingPrincipal: principal,
        topWindowURI: defaultTopWindowURI,
        expectedTracking: false,
        expectedPriority: Ci.nsISupportsPriority.PRIORITY_NORMAL,
        expectedThrottleable: false, // ignored since tracking==false
      },
      {
        path: trackingOrigin + "/evil.js",
        loadingPrincipal: principal,
        topWindowURI: defaultTopWindowURI,
        expectedTracking: false,
        expectedPriority: Ci.nsISupportsPriority.PRIORITY_NORMAL,
        expectedThrottleable: false, // ignored since tracking==false
      },
    ];
    // We add the doPriorityTest test here so that it only gets injected in the
    // test list if we're not skipping over this test.
    tests.unshift(doPriorityTest);
    runTests();
  },

  // Annotations ON, normal loading principal, topWinURI of example.com
  // => trackers should be de-prioritized
  function setupAnnotationsOn() {
    if (skipLowestPriority) {
      runTests();
      return;
    }
    if (runtime.processType == runtime.PROCESS_TYPE_DEFAULT) {
      Services.prefs.setBoolPref(
        "privacy.trackingprotection.annotate_channels",
        true
      );
      Services.prefs.setBoolPref(
        "privacy.trackingprotection.lower_network_priority",
        true
      );
    }
    var principal =
      Services.scriptSecurityManager.createContentPrincipalFromOrigin(
        normalOrigin
      );
    testPriorityMap = [
      {
        path: normalOrigin + "/innocent.css",
        loadingPrincipal: principal,
        topWindowURI: defaultTopWindowURI,
        expectedTracking: false,
        expectedPriority: Ci.nsISupportsPriority.PRIORITY_NORMAL,
        expectedThrottleable: false, // ignored since tracking==false
      },
      {
        path: normalOrigin + "/innocent.js",
        loadingPrincipal: principal,
        topWindowURI: defaultTopWindowURI,
        expectedTracking: false,
        expectedPriority: Ci.nsISupportsPriority.PRIORITY_NORMAL,
        expectedThrottleable: false, // ignored since tracking==false
      },
      {
        path: trackingOrigin + "/evil.css",
        loadingPrincipal: principal,
        topWindowURI: defaultTopWindowURI,
        expectedTracking: true,
        expectedPriority: Ci.nsISupportsPriority.PRIORITY_LOWEST,
        expectedThrottleable: true,
      },
      {
        path: trackingOrigin + "/evil.js",
        loadingPrincipal: principal,
        topWindowURI: defaultTopWindowURI,
        expectedTracking: true,
        expectedPriority: Ci.nsISupportsPriority.PRIORITY_LOWEST,
        expectedThrottleable: true,
      },
    ];
    // We add the doPriorityTest test here so that it only gets injected in the
    // test list if we're not skipping over this test.
    tests.unshift(doPriorityTest);
    runTests();
  },

  // Annotations ON, system loading principal, topWinURI of example.com
  // => trackers should not be de-prioritized
  function setupAnnotationsOnSystemPrincipal() {
    if (skipLowestPriority) {
      runTests();
      return;
    }
    if (runtime.processType == runtime.PROCESS_TYPE_DEFAULT) {
      Services.prefs.setBoolPref(
        "privacy.trackingprotection.annotate_channels",
        true
      );
      Services.prefs.setBoolPref(
        "privacy.trackingprotection.lower_network_priority",
        true
      );
    }
    testPriorityMap = [
      {
        path: normalOrigin + "/innocent.css",
        loadingPrincipal: null, // system principal
        topWindowURI: defaultTopWindowURI,
        expectedTracking: false,
        expectedPriority: Ci.nsISupportsPriority.PRIORITY_NORMAL,
        expectedThrottleable: false, // ignored since tracking==false
      },
      {
        path: normalOrigin + "/innocent.js",
        loadingPrincipal: null, // system principal
        topWindowURI: defaultTopWindowURI,
        expectedTracking: false,
        expectedPriority: Ci.nsISupportsPriority.PRIORITY_NORMAL,
        expectedThrottleable: false, // ignored since tracking==false
      },
      {
        path: trackingOrigin + "/evil.css",
        loadingPrincipal: null, // system principal
        topWindowURI: defaultTopWindowURI,
        expectedTracking: false,
        expectedPriority: Ci.nsISupportsPriority.PRIORITY_NORMAL,
        expectedThrottleable: false,
      },
      {
        path: trackingOrigin + "/evil.js",
        loadingPrincipal: null, // system principal
        topWindowURI: defaultTopWindowURI,
        expectedTracking: false,
        expectedPriority: Ci.nsISupportsPriority.PRIORITY_NORMAL,
        expectedThrottleable: true,
      },
    ];
    // We add the doPriorityTest test here so that it only gets injected in the
    // test list if we're not skipping over this test.
    tests.unshift(doPriorityTest);
    runTests();
  },

  function cleanUp() {
    do_test_pending();
    httpServer.stop(do_test_finished);
    if (runtime.processType == runtime.PROCESS_TYPE_DEFAULT) {
      do_test_pending();
      UrlClassifierTestUtils.cleanupTestTrackers().finally(do_test_finished);
    }
    runTests();
  },
];

function runTests() {
  if (!tests.length) {
    do_test_finished();
    return;
  }

  var test = tests.shift();
  test();
}

function run_test() {
  runTests();
  do_test_pending();
}
