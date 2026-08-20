ChromeUtils.defineESModuleGetters(this, {
  FormHistory: "resource://gre/modules/FormHistory.sys.mjs",
  NetErrorParent: "resource://gre/actors/NetErrorParent.sys.mjs",
  SEARCH_CTA_ACTIONS: "resource://gre/modules/URLKeywordAnalyzer.sys.mjs",
  SEARCH_CTA_REASONS: "resource://gre/modules/URLKeywordAnalyzer.sys.mjs",
  SearchTestUtils: "resource://testing-common/SearchTestUtils.sys.mjs",
  sinon: "resource://testing-common/Sinon.sys.mjs",
});

SearchTestUtils.init(this);

// Force the search CTA to accept the test's default engine as a supported
// general-purpose engine. installSearchExtension() creates addon engines, which
// report isGeneralPurposeEngine=false, so without this the CTA would be
// suppressed (bug 2055637). Auto-restored at the end of the test file.
function stubSearchCTASupportedEngine() {
  const sandbox = sinon.createSandbox();
  sandbox
    .stub(NetErrorParent.prototype, "isSupportedSearchEngine")
    .returns(true);
  registerCleanupFunction(() => sandbox.restore());
}

/**
 * Load a synthetic online dnsNotFound error page for an arbitrary failed URL,
 * including path, query string and fragment. loadNetErrorPage() only accepts a
 * bare host and only targets the primary window, so the search CTA tests need
 * this variant instead.
 *
 * The navigation runs from inside the content process because about:neterror
 * has to be reached the way a genuinely failed load reaches it, rather than by
 * a parent-initiated load.
 *
 * @param {string} failedURL The address the page reports as having failed.
 * @param {ChromeWindow} [win] Window to open the tab in, defaulting to the
 *   current one.
 * @returns {Promise<object>} An object with the new tab and its browser.
 */
async function loadDnsNotFoundPage(failedURL, win = window) {
  const tab = await BrowserTestUtils.openNewForegroundTab(
    win.gBrowser,
    "about:blank"
  );
  const browser = tab.linkedBrowser;
  const url = `about:neterror?e=dnsNotFound&u=${encodeURIComponent(failedURL)}`;
  const pageLoaded = BrowserTestUtils.waitForErrorPage(browser);
  SpecialPowers.spawn(browser, [url], errorUrl => {
    content.location = errorUrl;
  });
  await pageLoaded;
  return { browser, tab };
}

/**
 * Wait until the net-error-card in a browser has settled, so callers can assert
 * against a stable DOM instead of repeating waitForCondition. On a page where
 * the search CTA is eligible this also waits for the parent's CTA decision,
 * since the Search button only appears once that arrives. Fluent applies its
 * DOM overlays separately, so text inserted by an overlay (for example the
 * <strong> inside a hint) still needs its own wait.
 *
 * @param {MozBrowser|BrowsingContext} browser The browser showing the error
 *   page, or the BrowsingContext of the frame showing it.
 * @param {object} [options]
 * @param {string} [options.clickQuery] Name of a net-error-card static query,
 *   for example "reloadButton", to click once the card has settled.
 */
async function waitForSettledNetErrorCard(browser, { clickQuery = null } = {}) {
  await SpecialPowers.spawn(browser, [clickQuery], async query => {
    const card = await ContentTaskUtils.waitForCondition(
      () => content.document.querySelector("net-error-card")?.wrappedJSObject,
      "The net-error-card is present"
    );
    // Eligibility, not shouldShowSearchCTA(): a frame suppresses the CTA but
    // still awaits the parent's decision, and callers assert on that decision's
    // telemetry (bug 2063091).
    if (card.isSearchCTAEligible()) {
      await ContentTaskUtils.waitForCondition(
        () => card.searchCTAResolved,
        "The search CTA decision came back from the parent"
      );
    }
    await card.updateComplete;
    if (query) {
      card[query].click();
    }
  });
}

function getCertChainAsString(certBase64Array) {
  let certChain = "";
  for (let cert of certBase64Array) {
    certChain += getPEMString(cert);
  }
  return certChain;
}

function getPEMString(derb64) {
  // Wrap the Base64 string into lines of 64 characters,
  // with CRLF line breaks (as specified in RFC 1421).
  var wrapped = derb64.replace(/(\S{64}(?!$))/g, "$1\r\n");
  return (
    "-----BEGIN CERTIFICATE-----\r\n" +
    wrapped +
    "\r\n-----END CERTIFICATE-----\r\n"
  );
}

async function injectErrorPageFrame(tab, src, sandboxed) {
  let loadedPromise = BrowserTestUtils.browserLoaded(
    tab.linkedBrowser,
    true,
    null,
    true
  );

  await SpecialPowers.spawn(
    tab.linkedBrowser,
    [src, sandboxed],
    async function (frameSrc, frameSandboxed) {
      let iframe = content.document.createElement("iframe");
      iframe.src = frameSrc;
      if (frameSandboxed) {
        iframe.setAttribute("sandbox", "allow-scripts");
      }
      content.document.body.appendChild(iframe);
    }
  );

  await loadedPromise;
  await BrowserTestUtils.waitForPaintingUnsuppressed(
    tab.linkedBrowser.browsingContext.children[0]
  );
}

/**
 * Load a synthetic online dnsNotFound error page inside an iframe, for the
 * search CTA's not-top-level gate (bug 2063091). Content cannot link to
 * about:neterror, so we make an empty frame and navigate it with privileges.
 *
 * @param {string} failedURL The address the frame reports as having failed.
 * @returns {Promise<object>} The tab, its browser, and the frame's
 *   BrowsingContext. Assert against the frame rather than reaching through
 *   contentDocument, since the frame may live in its own process.
 */
async function loadDnsNotFoundFrame(failedURL) {
  const dummyPage =
    getRootDirectory(gTestPath).replace(
      "chrome://mochitests/content",
      "https://example.com"
    ) + "dummy_page.html";
  const tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, dummyPage);
  const browser = tab.linkedBrowser;

  await SpecialPowers.spawn(browser, [], async () => {
    content.document.body.appendChild(content.document.createElement("iframe"));
  });
  await TestUtils.waitForCondition(
    () => browser.browsingContext.children.length === 1,
    "The frame's BrowsingContext exists"
  );

  const url = `about:neterror?e=dnsNotFound&u=${encodeURIComponent(failedURL)}`;
  await SpecialPowers.spawn(browser.browsingContext.children[0], [url], u => {
    content.location = u;
  });
  // Re-read the BrowsingContext afterwards: navigating to the error page can
  // swap the frame into a different process, replacing the one spawned into.
  await TestUtils.waitForCondition(
    () => browser.browsingContext.children[0]?.currentURI?.spec === url,
    "The frame is showing the synthetic error page"
  );

  return { tab, browser, frame: browser.browsingContext.children[0] };
}

async function openErrorPage(src, useFrame, sandboxed) {
  let dummyPage =
    getRootDirectory(gTestPath).replace(
      "chrome://mochitests/content",
      "https://example.com"
    ) + "dummy_page.html";

  let tab;
  if (useFrame) {
    info("Loading cert error page in an iframe");
    tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, dummyPage);
    let errorCardReady = BrowserTestUtils.waitForContentEvent(
      tab.linkedBrowser,
      "AboutNetErrorLoad",
      false,
      null,
      true
    );
    await injectErrorPageFrame(tab, src, sandboxed);
    await errorCardReady;
  } else {
    let certErrorLoaded;
    tab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      () => {
        gBrowser.selectedTab = BrowserTestUtils.addTab(gBrowser, src);
        let browser = gBrowser.selectedBrowser;
        certErrorLoaded = BrowserTestUtils.waitForErrorPage(browser);
      },
      false
    );
    info("Loading and waiting for the cert error");
    await certErrorLoaded;
  }

  return tab;
}

function waitForCondition(condition, nextTest, errorMsg, retryTimes) {
  retryTimes = typeof retryTimes !== "undefined" ? retryTimes : 30;
  var tries = 0;
  var interval = setInterval(function () {
    if (tries >= retryTimes) {
      ok(false, errorMsg);
      moveOn();
    }
    var conditionPassed;
    try {
      conditionPassed = condition();
    } catch (e) {
      ok(false, e + "\n" + e.stack);
      conditionPassed = false;
    }
    if (conditionPassed) {
      moveOn();
    }
    tries++;
  }, 100);
  var moveOn = function () {
    clearInterval(interval);
    nextTest();
  };
}

async function waitForBookmarksToolbarVisibility({
  win = window,
  visible,
  message,
}) {
  let result = await TestUtils.waitForCondition(
    () => {
      let toolbar = win.document.getElementById("PersonalToolbar");
      return toolbar && (visible ? !toolbar.collapsed : toolbar.collapsed);
    },
    message ||
      "waiting for toolbar to become " + (visible ? "visible" : "hidden")
  );
  ok(result, message);
  return result;
}

function isBookmarksToolbarVisible(win = window) {
  let toolbar = win.document.getElementById("PersonalToolbar");
  return !toolbar.collapsed;
}

const setSecurityCertErrorsFeltPrivacyToTrue = async () =>
  await SpecialPowers.pushPrefEnv({
    set: [["security.certerrors.felt-privacy-v1", true]],
  });
const setSecurityCertErrorsFeltPrivacyToFalse = async () =>
  await SpecialPowers.pushPrefEnv({
    set: [["security.certerrors.felt-privacy-v1", false]],
  });

// -- TRR-only test helpers --

// resetTRRPrefs is set by loadTRRErrorPage() as a closure over the proxy type
// value captured at call time, avoiding shared mutable state.
let resetTRRPrefs = () => {
  throw new Error("resetTRRPrefs called before loadTRRErrorPage");
};

let _trrDnsOverrideSet = false;

async function loadTRRErrorPage() {
  const oldProxyType = Services.prefs.getIntPref("network.proxy.type");
  resetTRRPrefs = function () {
    Services.prefs.clearUserPref("network.trr.mode");
    Services.prefs.clearUserPref("network.dns.native-is-localhost");
    Services.prefs.setIntPref("network.proxy.type", oldProxyType);
  };
  registerCleanupFunction(resetTRRPrefs);

  // See bug 1831731: prevent real connections to the DoH endpoint.
  if (!_trrDnsOverrideSet) {
    Cc["@mozilla.org/network/native-dns-override;1"]
      .getService(Ci.nsINativeDNSResolverOverride)
      .addIPOverride("mozilla.cloudflare-dns.com", "127.0.0.1");
    _trrDnsOverrideSet = true;
  }

  Services.prefs.setBoolPref("network.dns.native-is-localhost", true);
  Services.prefs.setIntPref("network.trr.mode", Ci.nsIDNSService.MODE_TRRONLY);
  // Disable proxy, otherwise TRR isn't used for name resolution.
  Services.prefs.setIntPref("network.proxy.type", 0);

  Services.dns.clearCache(true);

  let browser;
  let pageLoaded;
  await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    () => {
      gBrowser.selectedTab = BrowserTestUtils.addTab(
        gBrowser,
        "https://does-not-exist.test"
      );
      browser = gBrowser.selectedBrowser;
      pageLoaded = BrowserTestUtils.waitForErrorPage(browser);
    },
    false
  );

  info("Loading and waiting for the TRR net error");
  await pageLoaded;
  return browser;
}

async function loadNetErrorPage(errorType, hostAndPort) {
  let browser, tab;
  const url = `about:neterror?e=${errorType}&u=http%3A%2F%2F${encodeURIComponent(hostAndPort)}%2F`;
  await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    () => {
      gBrowser.selectedTab = BrowserTestUtils.addTab(gBrowser, "about:blank");
      browser = gBrowser.selectedBrowser;
      tab = gBrowser.selectedTab;
    },
    false
  );
  const pageLoaded = BrowserTestUtils.waitForErrorPage(browser);
  SpecialPowers.spawn(browser, [url], errorUrl => {
    content.location = errorUrl;
  });
  await pageLoaded;
  return { browser, tab };
}
