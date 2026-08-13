ChromeUtils.defineESModuleGetters(this, {
  FormHistory: "resource://gre/modules/FormHistory.sys.mjs",
  SearchTestUtils: "resource://testing-common/SearchTestUtils.sys.mjs",
});

SearchTestUtils.init(this);

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
