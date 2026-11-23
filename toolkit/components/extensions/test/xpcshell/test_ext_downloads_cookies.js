"use strict";

const { UrlClassifierTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/UrlClassifierTestUtils.sys.mjs"
);

// Value for network.cookie.cookieBehavior to reject all third-party cookies.
const { BEHAVIOR_REJECT_FOREIGN } = Ci.nsICookieService;

const server = createHttpServer({ hosts: ["example.net", "itisatracker.org"] });
server.registerPathHandler("/setcookies", (request, response) => {
  response.setHeader("Content-Type", "text/html; charset=utf-8", false);
  response.setHeader("Set-Cookie", "c_none=1; sameSite=none", true);
  response.setHeader("Set-Cookie", "c_lax=1; sameSite=lax", true);
  response.setHeader("Set-Cookie", "c_strict=1; sameSite=strict", true);
});

server.registerPathHandler("/download", (request, response) => {
  response.setStatusLine(request.httpVersion, 200, "OK");

  let cookies = request.hasHeader("Cookie") ? request.getHeader("Cookie") : "";
  // Assign the result through the MIME-type, to make it easier to read the
  // result via the downloads API.
  response.setHeader("Content-Type", `dummy/${encodeURIComponent(cookies)}`);
  // Response of length 7.
  response.write("1234567");
});

server.registerPathHandler("/redirect", (request, response) => {
  response.setStatusLine(request.httpVersion, 302, "Found");
  response.setHeader("Location", "/download");
});

function createDownloadTestExtension(extraPermissions = []) {
  return ExtensionTestUtils.loadExtension({
    manifest: {
      permissions: ["downloads", ...extraPermissions],
    },
    incognitoOverride: "spanning",
    background() {
      async function getCookiesForDownload(url) {
        let donePromise = new Promise(resolve => {
          browser.downloads.onChanged.addListener(async delta => {
            if (delta.state?.current === "complete") {
              resolve(delta.id);
            }
          });
        });
        // TODO bug 1653636: Remove this when the correct browsing mode is used.
        const incognito = browser.extension.inIncognitoContext;
        let downloadId = await browser.downloads.download({ url, incognito });
        browser.test.assertEq(await donePromise, downloadId, "got download");
        let [download] = await browser.downloads.search({ id: downloadId });
        browser.test.log(`Download results: ${JSON.stringify(download)}`);

        // Delete the file since we aren't interested in it.
        // TODO bug 1654819: On Windows the file may be recreated.
        await browser.downloads.removeFile(download.id);
        // Sanity check to verify that we got the result from /download.
        browser.test.assertEq(7, download.fileSize, "download succeeded");

        // The "/download" endpoint mirrors received cookies via Content-Type.
        let cookies = decodeURIComponent(download.mime.replace("dummy/", ""));
        return cookies;
      }

      browser.test.onMessage.addListener(async url => {
        browser.test.sendMessage("result", await getCookiesForDownload(url));
      });
    },
  });
}

async function downloadAndGetCookies(extension, url) {
  extension.sendMessage(url);
  return extension.awaitMessage("result");
}

add_task(async function setup() {
  const nsIFile = Ci.nsIFile;
  const downloadDir = FileUtils.getDir("TmpD", ["downloads"]);
  downloadDir.createUnique(nsIFile.DIRECTORY_TYPE, FileUtils.PERMS_DIRECTORY);
  Services.prefs.setIntPref("browser.download.folderList", 2);
  Services.prefs.setComplexValue("browser.download.dir", nsIFile, downloadDir);

  // Support sameSite=none despite the server using http instead of https.
  Services.prefs.setBoolPref(
    "network.cookie.sameSite.noneRequiresSecure",
    false
  );
  async function loadAndClose(url) {
    let contentPage = await ExtensionTestUtils.loadContentPage(url);
    await contentPage.close();
  }
  // Generate cookies for use in this test.
  await loadAndClose("http://example.net/setcookies");
  await loadAndClose("http://itisatracker.org/setcookies");

  await UrlClassifierTestUtils.addTestTrackers();
  registerCleanupFunction(async () => {
    await UrlClassifierTestUtils.cleanupTestTrackers();
    Services.cookies.removeAll();

    Services.prefs.clearUserPref("browser.download.folderList");
    Services.prefs.clearUserPref("browser.download.dir");

    downloadDir.remove(false);
  });
});

// Checks that (sameSite) cookies are included in download requests.
add_task(async function download_cookies_basic() {
  let extension = createDownloadTestExtension(["*://example.net/*"]);
  await extension.startup();

  equal(
    await downloadAndGetCookies(extension, "http://example.net/download"),
    "c_none=1; c_lax=1; c_strict=1",
    "Cookies for downloads.download with sameSite cookies"
  );

  equal(
    await downloadAndGetCookies(extension, "http://example.net/redirect"),
    "c_none=1; c_lax=1; c_strict=1",
    "Cookies for downloads.download with redirect"
  );

  await runWithPrefs(
    [["network.cookie.cookieBehavior", BEHAVIOR_REJECT_FOREIGN]],
    async () => {
      equal(
        await downloadAndGetCookies(extension, "http://example.net/download"),
        "c_none=1; c_lax=1; c_strict=1",
        "Cookies for downloads.download with all third-party cookies disabled"
      );
    }
  );

  await extension.unload();
});

// Checks that (sameSite) cookies are included even when tracking protection
// would block cookies from third-party requests.
add_task(async function download_cookies_from_tracker_url() {
  let extension = createDownloadTestExtension(["*://itisatracker.org/*"]);
  await extension.startup();

  equal(
    await downloadAndGetCookies(extension, "http://itisatracker.org/download"),
    "c_none=1; c_lax=1; c_strict=1",
    "Cookies for downloads.download of itisatracker.org"
  );

  await extension.unload();
});

// Checks that (sameSite) cookies are included even without host permissions.
add_task(async function download_cookies_without_host_permissions() {
  let extension = createDownloadTestExtension();
  await extension.startup();

  equal(
    await downloadAndGetCookies(extension, "http://example.net/download"),
    "c_none=1; c_lax=1; c_strict=1",
    "Cookies for downloads.download without host permissions"
  );

  equal(
    await downloadAndGetCookies(extension, "http://itisatracker.org/download"),
    "c_none=1; c_lax=1; c_strict=1",
    "Cookies for downloads.download of itisatracker.org"
  );

  await runWithPrefs(
    [["network.cookie.cookieBehavior", BEHAVIOR_REJECT_FOREIGN]],
    async () => {
      equal(
        await downloadAndGetCookies(extension, "http://example.net/download"),
        "c_none=1; c_lax=1; c_strict=1",
        "Cookies for downloads.download with all third-party cookies disabled"
      );
    }
  );

  await extension.unload();
});

// Checks that (sameSite) cookies from private browsing are included.
add_task(async function download_cookies_in_perma_private_browsing() {
  Services.prefs.setBoolPref("browser.privatebrowsing.autostart", true);
  Services.prefs.setBoolPref("dom.security.https_first_pbm", false);

  let extension = createDownloadTestExtension(["*://example.net/*"]);
  await extension.startup();

  equal(
    await downloadAndGetCookies(extension, "http://example.net/download"),
    "",
    "Initially no cookies in permanent private browsing mode"
  );

  let contentPage = await ExtensionTestUtils.loadContentPage(
    "http://example.net/setcookies",
    { privateBrowsing: true }
  );

  equal(
    await downloadAndGetCookies(extension, "http://example.net/download"),
    "c_none=1; c_lax=1; c_strict=1",
    "Cookies for downloads.download in perma-private-browsing mode"
  );

  await extension.unload();
  await contentPage.close();
  Services.prefs.clearUserPref("browser.privatebrowsing.autostart");
  Services.prefs.clearUserPref("dom.security.https_first_pbm");
});
