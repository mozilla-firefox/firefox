"use strict";

let server = createHttpServer({ hosts: ["example.com"] });
let downloadReqCount = 0;
server.registerPathHandler("/downloadtest", () => {
  ++downloadReqCount;
});

add_setup(async () => {
  let downloadDir = await IOUtils.createUniqueDirectory(
    Services.dirsvc.get("TmpD", Ci.nsIFile).path,
    "downloadDirForDnrDownloadTest"
  );
  info(`Using download directory ${downloadDir.path}`);

  Services.prefs.setIntPref("browser.download.folderList", 2);
  Services.prefs.setCharPref("browser.download.dir", downloadDir);

  registerCleanupFunction(async () => {
    Services.prefs.clearUserPref("browser.download.folderList");
    Services.prefs.clearUserPref("browser.download.dir");
    try {
      await IOUtils.remove(downloadDir);
    } catch (e) {
      info(`Failed to remove ${downloadDir} because: ${e}`);
      // Downloaded files should have been deleted by tests.
      // Clean up + report error otherwise.
      let children = await IOUtils.getChildren(downloadDir).catch(e => e);
      ok(false, `Unexpected files in downloadDir: ${children}`);
      await IOUtils.remove(downloadDir, { recursive: true });
    }
  });

  Services.prefs.setBoolPref("extensions.dnr.enabled", true);
});

// Test for Bug 1579911: Check that download requests created by the
// downloads.download API can be observed by extensions.
// The webRequest version is in test_ext_webRequest_download.js.
add_task(async function test_download_api_can_be_blocked_by_dnr() {
  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      manifest_version: 3,
      permissions: ["declarativeNetRequest", "downloads"],
      // No host_permissions here because neither the downloads nor the DNR API
      // require host permissions to download and/or block the request.
    },
    // Not needed, but to rule out downloads being blocked by CSP:
    allowInsecureRequests: true,
    background: async function () {
      await browser.declarativeNetRequest.updateSessionRules({
        addRules: [
          {
            id: 1,
            condition: { urlFilter: "|http://example.com/downloadtest" },
            action: { type: "block" },
          },
        ],
      });

      browser.downloads.onChanged.addListener(delta => {
        browser.test.assertEq(delta.state.current, "interrupted");
        browser.test.sendMessage("done");
      });

      await browser.downloads.download({
        url: "http://example.com/downloadtest",
        filename: "example.txt",
      });
    },
  });

  await extension.startup();
  await extension.awaitMessage("done");
  await extension.unload();

  Assert.equal(downloadReqCount, 0, "Did not expect any download requests");
});

// This tests verifies which domainType is expected to be set on requests
// triggered by an extension through the downloads API.
add_task(async function test_download_api_requests_domainType() {
  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      manifest_version: 3,
      permissions: ["declarativeNetRequest", "downloads"],
    },
    // Not needed, but to rule out downloads being blocked by CSP:
    allowInsecureRequests: true,
    background: async function () {
      await browser.declarativeNetRequest.updateSessionRules({
        addRules: [
          {
            id: 1,
            condition: {
              urlFilter: "|http://example.com/downloadtest",
              domainType: "thirdParty",
            },
            action: { type: "block" },
          },
        ],
      });
      browser.downloads.onChanged.addListener(delta => {
        browser.test.assertEq(delta.state.current, "interrupted");
        browser.test.sendMessage("download:done");
      });

      await browser.downloads.download({
        url: "http://example.com/downloadtest",
        filename: "example.txt",
      });
    },
  });

  downloadReqCount = 0;
  await extension.startup();
  await extension.awaitMessage("download:done");
  await extension.unload();
  Assert.equal(downloadReqCount, 0, "Did not expect any download requests");
});

add_task(async function test_download_api_ignores_dnr_from_other_extension() {
  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      manifest_version: 3,
      permissions: ["declarativeNetRequest"],
    },
    background: async function () {
      await browser.declarativeNetRequest.updateSessionRules({
        addRules: [
          {
            id: 1,
            condition: { urlFilter: "|http://example.com/downloadtest" },
            action: { type: "block" },
          },
        ],
      });

      browser.test.sendMessage("dnr_registered");
    },
  });

  let otherExtension = ExtensionTestUtils.loadExtension({
    manifest: {
      permissions: ["downloads"],
    },
    background: async function () {
      let downloadDonePromise = new Promise(resolve => {
        browser.downloads.onChanged.addListener(delta => {
          if (delta.state.current === "interrupted") {
            browser.test.fail("Download was unexpectedly interrupted");
            browser.test.notifyFail("done");
          } else if (delta.state.current === "complete") {
            resolve();
          }
        });
      });

      // This download should not have been interrupted by the other extension,
      // because declarativeNetRequest cannot match requests from other
      // extensions.
      let downloadId = await browser.downloads.download({
        url: "http://example.com/downloadtest",
        filename: "example_from_other_ext.txt",
      });
      await downloadDonePromise;
      browser.test.log("Download completed, removing file...");
      // TODO bug 1654819: On Windows the file may be recreated.
      await browser.downloads.removeFile(downloadId);
      browser.test.notifyPass("done");
    },
  });

  await extension.startup();
  await extension.awaitMessage("dnr_registered");

  await otherExtension.startup();
  await otherExtension.awaitFinish("done");
  await otherExtension.unload();
  await extension.unload();

  Assert.equal(downloadReqCount, 1, "Expected one download request");
  downloadReqCount = 0;
});
