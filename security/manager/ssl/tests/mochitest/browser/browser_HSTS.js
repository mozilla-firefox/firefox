/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Tests that HTTP Strict Transport Security (HSTS) headers are noted as appropriate.

// Register a cleanup function to clear all accumulated HSTS state when this
// test is done.
add_setup(async function register_cleanup() {
  registerCleanupFunction(() => {
    let sss = Cc["@mozilla.org/ssservice;1"].getService(
      Ci.nsISiteSecurityService
    );
    sss.clearAll();
  });
});

// In the absense of HSTS information, no upgrade should happen.
add_task(async function test_no_hsts_information_no_upgrade() {
  let httpUrl =
    getRootDirectory(gTestPath).replace(
      "chrome://mochitests/content",
      "http://example.com"
    ) + "some_content.html";
  await BrowserTestUtils.openNewForegroundTab(gBrowser, httpUrl);
  Assert.equal(gBrowser.selectedBrowser.currentURI.scheme, "http");
  gBrowser.removeCurrentTab();
});

// Visit a secure site that sends an HSTS header to set up the rest of the
// test.
add_task(async function see_hsts_header() {
  let setHstsUrl =
    getRootDirectory(gTestPath).replace(
      "chrome://mochitests/content",
      "https://example.com"
    ) + "hsts_headers.sjs";
  await BrowserTestUtils.openNewForegroundTab(gBrowser, setHstsUrl);
  gBrowser.removeCurrentTab();
});

// Given a known HSTS host, future http navigations to that domain will be
// upgraded.
add_task(async function test_http_upgrade() {
  let httpUrl =
    getRootDirectory(gTestPath).replace(
      "chrome://mochitests/content",
      "http://example.com"
    ) + "some_content.html";
  await BrowserTestUtils.openNewForegroundTab(gBrowser, httpUrl);
  Assert.equal(gBrowser.selectedBrowser.currentURI.scheme, "https");
  gBrowser.removeCurrentTab();
});

// http navigations to unrelated hosts should not be upgraded.
add_task(async function test_unrelated_domain_no_upgrade() {
  let differentHttpUrl =
    getRootDirectory(gTestPath).replace(
      "chrome://mochitests/content",
      "http://example.org"
    ) + "some_content.html";
  await BrowserTestUtils.openNewForegroundTab(gBrowser, differentHttpUrl);
  Assert.equal(gBrowser.selectedBrowser.currentURI.scheme, "http");
  gBrowser.removeCurrentTab();
});

// http navigations in private contexts shouldn't use information from
// non-private contexts, so no upgrade should occur.
add_task(async function test_private_window_no_upgrade() {
  await SpecialPowers.pushPrefEnv({
    set: [["dom.security.https_first_pbm", false]],
  });
  let privateWindow = OpenBrowserWindow({ private: true });
  await BrowserTestUtils.firstBrowserLoaded(privateWindow, false);
  let url =
    getRootDirectory(gTestPath).replace(
      "chrome://mochitests/content",
      "http://example.com"
    ) + "some_content.html";
  await BrowserTestUtils.openNewForegroundTab(privateWindow.gBrowser, url);
  Assert.equal(
    privateWindow.gBrowser.selectedBrowser.currentURI.scheme,
    "http"
  );
  privateWindow.gBrowser.removeCurrentTab();
  privateWindow.close();
});

// Since the header didn't specify "includeSubdomains", visiting a subdomain
// should not result in an upgrade.
add_task(async function test_subdomain_no_upgrade() {
  let subdomainHttpUrl =
    getRootDirectory(gTestPath).replace(
      "chrome://mochitests/content",
      "http://test1.example.com"
    ) + "some_content.html";
  await BrowserTestUtils.openNewForegroundTab(gBrowser, subdomainHttpUrl);
  Assert.equal(gBrowser.selectedBrowser.currentURI.scheme, "http");
  gBrowser.removeCurrentTab();
});

// Now visit a secure site that sends an HSTS header that also includes subdomains.
add_task(async function see_hsts_header_include_subdomains() {
  let setHstsUrl =
    getRootDirectory(gTestPath).replace(
      "chrome://mochitests/content",
      "https://example.com"
    ) + "hsts_headers.sjs?includeSubdomains";
  await BrowserTestUtils.openNewForegroundTab(gBrowser, setHstsUrl);
  gBrowser.removeCurrentTab();
});

// Now visiting a subdomain should result in an upgrade.
add_task(async function test_subdomain_upgrade() {
  let subdomainHttpUrl =
    getRootDirectory(gTestPath).replace(
      "chrome://mochitests/content",
      "http://test1.example.com"
    ) + "some_content.html";
  await BrowserTestUtils.openNewForegroundTab(gBrowser, subdomainHttpUrl);
  Assert.equal(gBrowser.selectedBrowser.currentURI.scheme, "https");
  gBrowser.removeCurrentTab();
});

// Visiting a subdomain with https should result in an https URL (this isn't an
// upgrade - this test is essentially a consistency check).
add_task(async function test_already_https() {
  let subdomainHttpsUrl =
    getRootDirectory(gTestPath).replace(
      "chrome://mochitests/content",
      "https://test2.example.com"
    ) + "some_content.html";
  await BrowserTestUtils.openNewForegroundTab(gBrowser, subdomainHttpsUrl);
  Assert.equal(gBrowser.selectedBrowser.currentURI.scheme, "https");
  gBrowser.removeCurrentTab();
});

// Test that subresources are upgraded.
add_task(async function test_iframe_upgrade() {
  let framedUrl =
    getRootDirectory(gTestPath).replace(
      "chrome://mochitests/content",
      "https://example.com"
    ) + "some_content_framed.html";
  await BrowserTestUtils.openNewForegroundTab(gBrowser, framedUrl);
  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], async function () {
    await ContentTaskUtils.waitForCondition(() => {
      let frame = content.document.getElementById("frame");
      if (frame) {
        return frame.baseURI.startsWith("https://");
      }
      return false;
    });
  });
  gBrowser.removeCurrentTab();
});

// Clear state.
add_task(async function clear_hsts_state() {
  let sss = Cc["@mozilla.org/ssservice;1"].getService(
    Ci.nsISiteSecurityService
  );
  sss.clearAll();
});

// Make sure this test is valid.
add_task(async function test_no_hsts_information_no_upgrade_again() {
  let httpUrl =
    getRootDirectory(gTestPath).replace(
      "chrome://mochitests/content",
      "http://example.com"
    ) + "some_content.html";
  await BrowserTestUtils.openNewForegroundTab(gBrowser, httpUrl);
  Assert.equal(gBrowser.selectedBrowser.currentURI.scheme, "http");
  gBrowser.removeCurrentTab();
});

// Visit a site with an iframe that loads first-party content that sends an
// HSTS header. The header should be heeded because it's first-party.
add_task(async function see_hsts_header_in_framed_first_party_context() {
  let framedUrl =
    getRootDirectory(gTestPath).replace(
      "chrome://mochitests/content",
      "https://example.com"
    ) + "hsts_headers_framed.html";
  await BrowserTestUtils.openNewForegroundTab(gBrowser, framedUrl);
  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], async function () {
    await ContentTaskUtils.waitForCondition(() => {
      return content.document.getElementById("done");
    });
  });
  gBrowser.removeCurrentTab();
});

// Check that the framed, first-party header was heeded.
add_task(async function test_http_upgrade_after_framed_first_party_header() {
  let httpUrl =
    getRootDirectory(gTestPath).replace(
      "chrome://mochitests/content",
      "http://example.com"
    ) + "some_content.html";
  await BrowserTestUtils.openNewForegroundTab(gBrowser, httpUrl);
  Assert.equal(gBrowser.selectedBrowser.currentURI.scheme, "https");
  gBrowser.removeCurrentTab();
});

// Visit a site with an iframe that loads third-party content that sends an
// HSTS header. The header should be ignored because it's third-party.
add_task(async function see_hsts_header_in_third_party_context() {
  let framedUrl =
    getRootDirectory(gTestPath).replace(
      "chrome://mochitests/content",
      "https://example.com"
    ) + "hsts_headers_framed.html?third-party";
  await BrowserTestUtils.openNewForegroundTab(gBrowser, framedUrl);
  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], async function () {
    await ContentTaskUtils.waitForCondition(() => {
      return content.document.getElementById("done");
    });
  });
  gBrowser.removeCurrentTab();
});

// Since the HSTS header was not received in a first-party context, no upgrade
// should occur.
add_task(async function test_no_upgrade_for_third_party_header() {
  let url =
    getRootDirectory(gTestPath).replace(
      "chrome://mochitests/content",
      "http://example.org"
    ) + "some_content.html";
  await BrowserTestUtils.openNewForegroundTab(gBrowser, url);
  Assert.equal(gBrowser.selectedBrowser.currentURI.scheme, "http");
  gBrowser.removeCurrentTab();
});

// Clear state again.
add_task(async function clear_hsts_state_again() {
  let sss = Cc["@mozilla.org/ssservice;1"].getService(
    Ci.nsISiteSecurityService
  );
  sss.clearAll();
});

// HSTS information encountered in private contexts should not be used in
// non-private contexts.
add_task(
  async function test_no_upgrade_for_HSTS_information_from_private_window() {
    await SpecialPowers.pushPrefEnv({
      set: [["dom.security.https_first_pbm", false]],
    });
    let privateWindow = OpenBrowserWindow({ private: true });
    await BrowserTestUtils.firstBrowserLoaded(privateWindow, false);
    let setHstsUrl =
      getRootDirectory(gTestPath).replace(
        "chrome://mochitests/content",
        "https://example.com"
      ) + "hsts_headers.sjs";
    await BrowserTestUtils.openNewForegroundTab(
      privateWindow.gBrowser,
      setHstsUrl
    );
    privateWindow.gBrowser.removeCurrentTab();

    let httpUrl =
      getRootDirectory(gTestPath).replace(
        "chrome://mochitests/content",
        "http://example.com"
      ) + "some_content.html";
    await BrowserTestUtils.openNewForegroundTab(gBrowser, httpUrl);
    Assert.equal(gBrowser.selectedBrowser.currentURI.scheme, "http");
    gBrowser.removeCurrentTab();

    privateWindow.close();
  }
);
