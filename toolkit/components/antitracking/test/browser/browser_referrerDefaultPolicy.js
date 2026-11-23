"use strict";

requestLongerTimeout(2);

Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/browser/base/content/test/browser-general/head.js",
  this
);

add_setup(function () {
  registerCleanupFunction(clearSiteTestData);
});

async function openAWindow(usePrivate) {
  info("Creating a new " + (usePrivate ? "private" : "normal") + " window");
  let win = OpenBrowserWindow({ private: usePrivate });
  await TestUtils.topicObserved(
    "browser-delayed-startup-finished",
    subject => subject == win
  ).then(() => win);
  await BrowserTestUtils.firstBrowserLoaded(win);
  return win;
}

async function testOnWindowBody(win, expectedReferrer, rp) {
  let browser = win.gBrowser;
  let tab = browser.selectedTab;
  let b = browser.getBrowserForTab(tab);
  await BrowserTestUtils.loadURIString({
    browser: tab.linkedBrowser,
    uriString: TEST_TOP_PAGE,
  });

  info("Loading tracking scripts and tracking images");
  let { iframeReferrer, refreshReferrer } = await SpecialPowers.spawn(
    b,
    [{ rp }],
    async function ({ rp }) {
      {
        let src = content.document.createElement("script");
        let p = new content.Promise(resolve => {
          src.onload = resolve;
        });
        content.document.body.appendChild(src);
        if (rp) {
          src.referrerPolicy = rp;
        }
        src.src =
          "https://tracking.example.org/browser/toolkit/components/antitracking/test/browser/referrer.sjs?what=script";
        await p;
      }

      {
        let img = content.document.createElement("img");
        let p = new content.Promise(resolve => {
          img.onload = resolve;
        });
        content.document.body.appendChild(img);
        if (rp) {
          img.referrerPolicy = rp;
        }
        img.src =
          "https://tracking.example.org/browser/toolkit/components/antitracking/test/browser/referrer.sjs?what=image";
        await p;
      }

      let iframeReferrer;
      {
        let iframe = content.document.createElement("iframe");
        const loaded = ContentTaskUtils.waitForEvent(iframe, "load");
        if (rp) {
          iframe.referrerPolicy = rp;
        }
        iframe.src =
          "https://tracking.example.org/browser/toolkit/components/antitracking/test/browser/referrer.sjs?what=iframe";
        content.document.body.appendChild(iframe);
        await loaded;

        const messageEvent = ContentTaskUtils.waitForEvent(content, "message");
        iframe.contentWindow.postMessage("ping", "*");
        iframeReferrer = (await messageEvent).data;
      }

      let refreshReferrer;
      {
        let iframe = content.document.createElement("iframe");
        {
          let { promise: p, resolve: iframeLoaded } =
            content.Promise.withResolvers();
          let loadCount = 0;
          iframe.onload = () => {
            loadCount++;
            if (loadCount === 1) {
              if (rp) {
                let policyNode = iframe.contentDocument.createElement("meta");
                policyNode.name = "referrer";
                policyNode.content = rp;
                iframe.contentDocument.body.appendChild(policyNode);
              }
              let refresh = iframe.contentDocument.createElement("meta");
              refresh.httpEquiv = "refresh";
              refresh.content =
                "0;https://tracking.example.org/browser/toolkit/components/antitracking/test/browser/referrer.sjs?what=iframe";
              iframe.contentDocument.body.appendChild(refresh);
            } else if (loadCount === 2) {
              iframeLoaded();
            }
          };
          iframe.src = content.location;
          content.document.body.appendChild(iframe);
          await p;
        }

        let { promise: p, resolve: messageReceived } =
          content.Promise.withResolvers();
        content.onmessage = event => {
          messageReceived(event.data);
        };

        iframe.contentWindow.postMessage("ping", "*");
        refreshReferrer = await p;
      }
      return { iframeReferrer, refreshReferrer };
    }
  );

  is(
    iframeReferrer,
    expectedReferrer,
    "The correct iframe referrer must be read from DOM"
  );
  is(
    refreshReferrer,
    expectedReferrer,
    "The correct refresh referrer must be read from DOM"
  );

  await fetch(
    "https://tracking.example.org/browser/toolkit/components/antitracking/test/browser/referrer.sjs?result&what=script"
  )
    .then(r => r.text())
    .then(text => {
      is(text, expectedReferrer, "We sent the correct Referer header");
    });

  await fetch(
    "https://tracking.example.org/browser/toolkit/components/antitracking/test/browser/referrer.sjs?result&what=image"
  )
    .then(r => r.text())
    .then(text => {
      is(text, expectedReferrer, "We sent the correct Referer header");
    });

  await fetch(
    "https://tracking.example.org/browser/toolkit/components/antitracking/test/browser/referrer.sjs?result&what=iframe"
  )
    .then(r => r.text())
    .then(text => {
      is(text, expectedReferrer, "We sent the correct Referer header");
    });
}

async function closeAWindow(win) {
  await BrowserTestUtils.closeWindow(win);
}

let gRecording = true;
let gScenarios = [];
let gRPs = [];
let gTests = { private: [], nonPrivate: [] };
const kPBPref = "network.http.referer.defaultPolicy.trackers.pbmode";
const kNonPBPref = "network.http.referer.defaultPolicy.trackers";

function recordScenario(isPrivate, expectedReferrer, rp) {
  if (!gRPs.includes(rp)) {
    gRPs.push(rp);
  }
  gScenarios.push({
    private: isPrivate,
    expectedReferrer,
    rp,
    pbPref: Services.prefs.getIntPref(kPBPref),
    nonPBPref: Services.prefs.getIntPref(kNonPBPref),
  });
}

async function testOnWindow(isPrivate, expectedReferrer, rp) {
  if (gRecording) {
    recordScenario(isPrivate, expectedReferrer, rp);
  }
}

function compileScenarios() {
  let keys = { false: [], true: [] };
  for (let s of gScenarios) {
    let key = {
      rp: s.rp,
      pbPref: s.pbPref,
      nonPBPref: s.nonPBPref,
    };
    let skip = false;
    for (let k of keys[s.private]) {
      if (
        key.rp == k.rp &&
        key.pbPref == k.pbPref &&
        key.nonPBPref == k.nonPBPref
      ) {
        skip = true;
        break;
      }
    }
    if (!skip) {
      keys[s.private].push(key);
      gTests[s.private ? "private" : "nonPrivate"].push({
        rp: s.rp,
        pbPref: s.pbPref,
        nonPBPref: s.nonPBPref,
        expectedReferrer: s.expectedReferrer,
      });
    }
  }

  // Verify that all scenarios are checked
  let counter = 1;
  for (let s of gScenarios) {
    let checked = false;
    for (let tt in gTests) {
      let isPrivate = tt == "private";
      for (let t of gTests[tt]) {
        if (
          isPrivate == s.private &&
          t.rp == s.rp &&
          t.pbPref == s.pbPref &&
          t.nonPBPref == s.nonPBPref &&
          t.expectedReferrer == s.expectedReferrer
        ) {
          checked = true;
          break;
        }
      }
    }
    ok(checked, `Scenario number ${counter++} checked`);
  }
}

async function executeTests() {
  compileScenarios();

  gRecording = false;
  for (let mode in gTests) {
    info(`Open a ${mode} window`);
    let win = await openAWindow(mode == "private");
    while (gTests[mode].length) {
      let test = gTests[mode].shift();
      info(`Running test ${test.toSource()}`);

      await SpecialPowers.pushPrefEnv({
        set: [
          ["network.http.referer.defaultPolicy.trackers", test.nonPBPref],
          ["network.http.referer.defaultPolicy.trackers.pbmode", test.pbPref],
          ["dom.security.https_first_pbm", false],
          ["network.http.referer.disallowCrossSiteRelaxingDefault", false],
          ["network.http.referer.sendFromRefresh", true], // Enable sending a Referer header when navigating from a Refresh.
        ],
      });

      await testOnWindowBody(win, test.expectedReferrer, test.rp);
    }
    await closeAWindow(win);
  }

  Services.prefs.clearUserPref(kPBPref);
  Services.prefs.clearUserPref(kNonPBPref);
}

function pn(name, isPrivate) {
  return isPrivate ? name + ".pbmode" : name;
}

async function testOnNoReferrer(isPrivate) {
  // no-referrer pref when no-referrer is forced
  await SpecialPowers.pushPrefEnv({
    set: [[pn("network.http.referer.defaultPolicy.trackers", isPrivate), 3]],
  });
  await testOnWindow(isPrivate, "", "no-referrer");

  // strict-origin-when-cross-origin pref when no-referrer is forced
  await SpecialPowers.pushPrefEnv({
    set: [[pn("network.http.referer.defaultPolicy.trackers", isPrivate), 2]],
  });
  await testOnWindow(isPrivate, "", "no-referrer");

  // same-origin pref when no-referrer is forced
  await SpecialPowers.pushPrefEnv({
    set: [[pn("network.http.referer.defaultPolicy.trackers", isPrivate), 1]],
  });
  await testOnWindow(isPrivate, "", "no-referrer");

  // no-referrer pref when no-referrer is forced
  await SpecialPowers.pushPrefEnv({
    set: [[pn("network.http.referer.defaultPolicy.trackers", isPrivate), 0]],
  });
  await testOnWindow(isPrivate, "", "no-referrer");
}

async function testOnSameOrigin(isPrivate) {
  // same-origin pref when same-origin is forced
  await SpecialPowers.pushPrefEnv({
    set: [[pn("network.http.referer.defaultPolicy.trackers", isPrivate), 3]],
  });
  await testOnWindow(isPrivate, "", "same-origin");

  // strict-origin-when-cross-origin pref when same-origin is forced
  await SpecialPowers.pushPrefEnv({
    set: [[pn("network.http.referer.defaultPolicy.trackers", isPrivate), 2]],
  });
  await testOnWindow(isPrivate, "", "same-origin");

  // same-origin pref when same-origin is forced
  await SpecialPowers.pushPrefEnv({
    set: [[pn("network.http.referer.defaultPolicy.trackers", isPrivate), 1]],
  });
  await testOnWindow(isPrivate, "", "same-origin");

  // same-origin pref when same-origin is forced
  await SpecialPowers.pushPrefEnv({
    set: [[pn("network.http.referer.defaultPolicy.trackers", isPrivate), 0]],
  });
  await testOnWindow(isPrivate, "", "same-origin");
}

async function testOnNoReferrerWhenDowngrade(isPrivate) {
  // The setting referrer policy will be ignored if it is
  // no-referrer-when-downgrade in private mode. It will fallback to the default
  // value.
  //
  // The pref 'network.http.referer.disallowCrossSiteRelaxingDefault.pbmode'
  // controls this behavior in private mode.

  // no-referrer-when-downgrade pref when no-referrer-when-downgrade is forced
  await SpecialPowers.pushPrefEnv({
    set: [[pn("network.http.referer.defaultPolicy.trackers", isPrivate), 3]],
  });
  await testOnWindow(isPrivate, TEST_TOP_PAGE, "no-referrer-when-downgrade");

  // strict-origin-when-cross-origin pref when no-referrer-when-downgrade is forced
  await SpecialPowers.pushPrefEnv({
    set: [[pn("network.http.referer.defaultPolicy.trackers", isPrivate), 2]],
  });
  if (isPrivate) {
    await testOnWindow(isPrivate, TEST_DOMAIN, "no-referrer-when-downgrade");
  } else {
    await testOnWindow(isPrivate, TEST_TOP_PAGE, "no-referrer-when-downgrade");
  }

  // same-origin pref when no-referrer-when-downgrade is forced
  await SpecialPowers.pushPrefEnv({
    set: [[pn("network.http.referer.defaultPolicy.trackers", isPrivate), 1]],
  });
  if (isPrivate) {
    await testOnWindow(isPrivate, "", "no-referrer-when-downgrade");
  } else {
    await testOnWindow(isPrivate, TEST_TOP_PAGE, "no-referrer-when-downgrade");
  }

  // no-referrer pref when no-referrer-when-downgrade is forced
  await SpecialPowers.pushPrefEnv({
    set: [[pn("network.http.referer.defaultPolicy.trackers", isPrivate), 0]],
  });
  if (isPrivate) {
    await testOnWindow(isPrivate, "", "no-referrer-when-downgrade");
  } else {
    await testOnWindow(isPrivate, TEST_TOP_PAGE, "no-referrer-when-downgrade");
  }
}

async function testOnOrigin(isPrivate) {
  // origin pref when origin is forced
  await SpecialPowers.pushPrefEnv({
    set: [[pn("network.http.referer.defaultPolicy.trackers", isPrivate), 3]],
  });
  await testOnWindow(isPrivate, TEST_DOMAIN, "origin");

  // strict-origin pref when origin is forced
  await SpecialPowers.pushPrefEnv({
    set: [[pn("network.http.referer.defaultPolicy.trackers", isPrivate), 2]],
  });
  await testOnWindow(isPrivate, TEST_DOMAIN, "origin");

  // same-origin pref when origin is forced
  await SpecialPowers.pushPrefEnv({
    set: [[pn("network.http.referer.defaultPolicy.trackers", isPrivate), 1]],
  });
  await testOnWindow(isPrivate, TEST_DOMAIN, "origin");

  // no-referrer pref when origin is forced
  await SpecialPowers.pushPrefEnv({
    set: [[pn("network.http.referer.defaultPolicy.trackers", isPrivate), 0]],
  });
  await testOnWindow(isPrivate, TEST_DOMAIN, "origin");
}

async function testOnStrictOrigin(isPrivate) {
  // strict-origin pref when strict-origin is forced
  await SpecialPowers.pushPrefEnv({
    set: [[pn("network.http.referer.defaultPolicy.trackers", isPrivate), 3]],
  });
  await testOnWindow(isPrivate, TEST_DOMAIN, "strict-origin");

  // strict-origin pref when strict-origin is forced
  await SpecialPowers.pushPrefEnv({
    set: [[pn("network.http.referer.defaultPolicy.trackers", isPrivate), 2]],
  });
  await testOnWindow(isPrivate, TEST_DOMAIN, "strict-origin");

  // same-origin pref when strict-origin is forced
  await SpecialPowers.pushPrefEnv({
    set: [[pn("network.http.referer.defaultPolicy.trackers", isPrivate), 1]],
  });
  await testOnWindow(isPrivate, TEST_DOMAIN, "strict-origin");

  // no-referrer pref when strict-origin is forced
  await SpecialPowers.pushPrefEnv({
    set: [[pn("network.http.referer.defaultPolicy.trackers", isPrivate), 0]],
  });
  await testOnWindow(isPrivate, TEST_DOMAIN, "strict-origin");
}

async function testOnOriginWhenCrossOrigin(isPrivate) {
  // The setting referrer policy will be ignored if it is
  // origin-when-cross-origin in private mode. It will fallback to the default
  // value. The pref controls this behavior mentioned above.

  // no-referrer-when-downgrade pref when origin-when-cross-origin is forced
  await SpecialPowers.pushPrefEnv({
    set: [[pn("network.http.referer.defaultPolicy.trackers", isPrivate), 3]],
  });
  if (isPrivate) {
    await testOnWindow(isPrivate, TEST_TOP_PAGE, "origin-when-cross-origin");
  } else {
    await testOnWindow(isPrivate, TEST_DOMAIN, "origin-when-cross-origin");
  }

  // strict-origin-when-cross-origin pref when origin-when-cross-origin is forced
  await SpecialPowers.pushPrefEnv({
    set: [[pn("network.http.referer.defaultPolicy.trackers", isPrivate), 2]],
  });
  await testOnWindow(isPrivate, TEST_DOMAIN, "origin-when-cross-origin");

  // same-origin pref when origin-when-cross-origin is forced
  await SpecialPowers.pushPrefEnv({
    set: [[pn("network.http.referer.defaultPolicy.trackers", isPrivate), 1]],
  });
  if (isPrivate) {
    await testOnWindow(isPrivate, "", "origin-when-cross-origin");
  } else {
    await testOnWindow(isPrivate, TEST_DOMAIN, "origin-when-cross-origin");
  }

  // no-referrer pref when origin-when-cross-origin is forced
  await SpecialPowers.pushPrefEnv({
    set: [[pn("network.http.referer.defaultPolicy.trackers", isPrivate), 0]],
  });
  if (isPrivate) {
    await testOnWindow(isPrivate, "", "origin-when-cross-origin");
  } else {
    await testOnWindow(isPrivate, TEST_DOMAIN, "origin-when-cross-origin");
  }
}

async function testOnStrictOriginWhenCrossOrigin(isPrivate) {
  // origin-when-cross-origin pref when strict-origin-when-cross-origin is forced
  await SpecialPowers.pushPrefEnv({
    set: [[pn("network.http.referer.defaultPolicy.trackers", isPrivate), 3]],
  });
  await testOnWindow(isPrivate, TEST_DOMAIN, "strict-origin-when-cross-origin");

  // strict-origin-when-cross-origin pref when strict-origin-when-cross-origin is forced
  await SpecialPowers.pushPrefEnv({
    set: [[pn("network.http.referer.defaultPolicy.trackers", isPrivate), 2]],
  });
  await testOnWindow(isPrivate, TEST_DOMAIN, "strict-origin-when-cross-origin");

  // same-origin pref when strict-origin-when-cross-origin is forced
  await SpecialPowers.pushPrefEnv({
    set: [[pn("network.http.referer.defaultPolicy.trackers", isPrivate), 1]],
  });
  await testOnWindow(isPrivate, TEST_DOMAIN, "strict-origin-when-cross-origin");

  // no-referrer pref when strict-origin-when-cross-origin is forced
  await SpecialPowers.pushPrefEnv({
    set: [[pn("network.http.referer.defaultPolicy.trackers", isPrivate), 0]],
  });
  await testOnWindow(isPrivate, TEST_DOMAIN, "strict-origin-when-cross-origin");
}

async function testOnUnsafeUrl(isPrivate) {
  // The setting referrer policy will be ignored if it is unsafe in private
  // mode. It will fallback to the default value. The pref controls this
  // behavior mentioned above.

  // no-referrer-when-downgrade pref when unsafe-url is forced
  await SpecialPowers.pushPrefEnv({
    set: [[pn("network.http.referer.defaultPolicy.trackers", isPrivate), 3]],
  });
  await testOnWindow(isPrivate, TEST_TOP_PAGE, "unsafe-url");

  // strict-origin-when-cross-origin pref when unsafe-url is forced
  await SpecialPowers.pushPrefEnv({
    set: [[pn("network.http.referer.defaultPolicy.trackers", isPrivate), 2]],
  });
  if (isPrivate) {
    await testOnWindow(isPrivate, TEST_DOMAIN, "unsafe-url");
  } else {
    await testOnWindow(isPrivate, TEST_TOP_PAGE, "unsafe-url");
  }

  // same-origin pref when unsafe-url is forced
  await SpecialPowers.pushPrefEnv({
    set: [[pn("network.http.referer.defaultPolicy.trackers", isPrivate), 1]],
  });
  if (isPrivate) {
    await testOnWindow(isPrivate, "", "unsafe-url");
  } else {
    await testOnWindow(isPrivate, TEST_TOP_PAGE, "unsafe-url");
  }

  // no-referrer pref when unsafe-url is forced
  await SpecialPowers.pushPrefEnv({
    set: [[pn("network.http.referer.defaultPolicy.trackers", isPrivate), 0]],
  });
  if (isPrivate) {
    await testOnWindow(isPrivate, "", "unsafe-url");
  } else {
    await testOnWindow(isPrivate, TEST_TOP_PAGE, "unsafe-url");
  }
}

add_task(async function () {
  info("Starting referrer default policy test");

  await SpecialPowers.flushPrefEnv();
  await SpecialPowers.pushPrefEnv({
    set: [
      [
        "network.cookie.cookieBehavior",
        Ci.nsICookieService.BEHAVIOR_REJECT_TRACKER,
      ],
      [
        "network.cookie.cookieBehavior.pbmode",
        Ci.nsICookieService.BEHAVIOR_REJECT_TRACKER,
      ],
      ["network.http.referer.defaultPolicy", 3],
      ["privacy.trackingprotection.enabled", false],
      ["privacy.trackingprotection.pbmode.enabled", false],
      ["privacy.trackingprotection.annotate_channels", true],
      ["network.http.referer.sendFromRefresh", true], // Enable sending a Referer header when navigating from a Refresh.
    ],
  });

  // no-referrer-when-downgrade
  await SpecialPowers.pushPrefEnv({
    set: [["network.http.referer.defaultPolicy.trackers", 3]],
  });
  await testOnWindow(false, TEST_TOP_PAGE, null);

  // strict-origin-when-cross-origin
  await SpecialPowers.pushPrefEnv({
    set: [["network.http.referer.defaultPolicy.trackers", 2]],
  });
  await testOnWindow(false, TEST_DOMAIN, null);

  // same-origin
  await SpecialPowers.pushPrefEnv({
    set: [["network.http.referer.defaultPolicy.trackers", 1]],
  });
  await testOnWindow(false, "", null);

  // no-referrer
  await SpecialPowers.pushPrefEnv({
    set: [["network.http.referer.defaultPolicy.trackers", 0]],
  });
  await testOnWindow(false, "", null);

  // override with no-referrer
  await testOnNoReferrer(false);

  // override with same-origin
  await testOnSameOrigin(false);

  // override with no-referrer-when-downgrade
  await testOnNoReferrerWhenDowngrade(false);

  // override with origin
  await testOnOrigin(false);

  // override with strict-origin
  await testOnStrictOrigin(false);

  // override with origin-when-cross-origin
  await testOnOriginWhenCrossOrigin(false);

  // override with strict-origin-when-cross-origin
  await testOnStrictOriginWhenCrossOrigin(false);

  // override with unsafe-url
  await testOnUnsafeUrl(false);

  // Reset the pref.
  Services.prefs.clearUserPref("network.http.referer.defaultPolicy.trackers");

  // no-referrer-when-downgrade
  await SpecialPowers.pushPrefEnv({
    set: [
      // Set both prefs, because if we only set the trackers pref, then the PB
      // mode default policy pref (2) would apply!
      ["network.http.referer.defaultPolicy.pbmode", 3],
      ["network.http.referer.defaultPolicy.trackers.pbmode", 3],
    ],
  });
  await testOnWindow(true, TEST_TOP_PAGE, null);

  // strict-origin-when-cross-origin
  await SpecialPowers.pushPrefEnv({
    set: [["network.http.referer.defaultPolicy.trackers.pbmode", 2]],
  });
  await testOnWindow(true, TEST_DOMAIN, null);

  // same-origin
  await SpecialPowers.pushPrefEnv({
    set: [["network.http.referer.defaultPolicy.trackers.pbmode", 1]],
  });
  await testOnWindow(true, "", null);

  // no-referrer
  await SpecialPowers.pushPrefEnv({
    set: [["network.http.referer.defaultPolicy.trackers.pbmode", 0]],
  });
  await testOnWindow(true, "", null);

  // override with no-referrer
  await testOnNoReferrer(true);

  // override with same-origin
  await testOnSameOrigin(true);

  // override with no-referrer-when-downgrade
  await testOnNoReferrerWhenDowngrade(true);

  // override with origin
  await testOnOrigin(true);

  // override with strict-origin
  await testOnStrictOrigin(true);

  // override with origin-when-cross-origin
  await testOnOriginWhenCrossOrigin(true);

  // override with strict-origin-when-cross-origin
  await testOnStrictOriginWhenCrossOrigin(true);

  // override with unsafe-url
  await testOnUnsafeUrl(true);

  // Reset the pref.
  Services.prefs.clearUserPref(
    "network.http.referer.defaultPolicy.trackers.pbmode"
  );
});

add_task(async function () {
  await UrlClassifierTestUtils.addTestTrackers();

  await executeTests();

  UrlClassifierTestUtils.cleanupTestTrackers();
});
