"use strict";

ChromeUtils.defineESModuleGetters(this, {
  TestUtils: "resource://testing-common/TestUtils.sys.mjs",
});

const server = createHttpServer({ hosts: ["example.com", "restricted"] });
server.registerPathHandler("/", (req, res) => {
  res.setHeader("Access-Control-Allow-Origin", "*");
  res.write("response from server");
});
server.registerPathHandler("/style_with_import.css", (req, res) => {
  res.setHeader("Content-Type", "text/css");
  res.setHeader("Access-Control-Allow-Origin", "*");
  res.write("@import url('http://example.com/imported.css');");
});
server.registerPathHandler("/imported.css", (req, res) => {
  res.setHeader("Content-Type", "text/css");
  res.setHeader("Access-Control-Allow-Origin", "*");
  res.write("imported_stylesheet_here { }");
});

add_setup(() => {
  Services.prefs.setBoolPref("extensions.dnr.enabled", true);
  // The restrictedDomains pref should be set early, because the pref is read
  // only once (on first use) by WebExtensionPolicy::IsRestrictedURI.
  Services.prefs.setCharPref(
    "extensions.webextensions.restrictedDomains",
    "restricted"
  );
});

async function startDNRExtension() {
  let extension = ExtensionTestUtils.loadExtension({
    async background() {
      await browser.declarativeNetRequest.updateSessionRules({
        addRules: [
          {
            id: 1,
            condition: { resourceTypes: ["xmlhttprequest", "stylesheet"] },
            action: { type: "block" },
          },
          {
            id: 2,
            condition: {
              urlFilter: "blockme",
              resourceTypes: ["main_frame", "sub_frame"],
            },
            action: { type: "block" },
          },
        ],
      });
      browser.test.sendMessage("dnr_registered");
    },
    manifest: {
      manifest_version: 3,
      permissions: ["declarativeNetRequest"],
    },
  });
  await extension.startup();
  await extension.awaitMessage("dnr_registered");
  return extension;
}

add_task(async function dnr_ignores_system_requests() {
  let extension = await startDNRExtension();
  Assert.equal(
    await (await fetch("http://example.com/")).text(),
    "response from server",
    "DNR should not block requests from system principal"
  );
  await extension.unload();
});

add_task(async function dnr_ignores_requests_to_restrictedDomains() {
  let extension = await startDNRExtension();
  Assert.equal(
    await ExtensionTestUtils.fetch("http://example.com/", "http://restricted/"),
    "response from server",
    "DNR should not block destination in restrictedDomains"
  );
  await extension.unload();
});

add_task(async function dnr_ignores_initiator_from_restrictedDomains() {
  let extension = await startDNRExtension();
  Assert.equal(
    await ExtensionTestUtils.fetch("http://restricted/", "http://example.com/"),
    "response from server",
    "DNR should not block requests initiated from a page in restrictedDomains"
  );
  await extension.unload();
});

add_task(async function dnr_ignores_navigation_to_restrictedDomains() {
  let extension = await startDNRExtension();
  let contentPage = await ExtensionTestUtils.loadContentPage(
    "http://restricted/?blockme"
  );
  await contentPage.spawn([], () => {
    const { document } = content;
    Assert.equal(document.URL, "http://restricted/?blockme", "Same URL");
    Assert.equal(document.body.textContent, "response from server", "body");
  });

  info("Navigating to a non-restricted sub_frame with restricted initiator");
  await contentPage.spawn([], async () => {
    const f = content.wrappedJSObject.document.createElement("iframe");
    await new Promise(resolve => {
      f.src = "http://example.com/?blockme2";
      f.onload = resolve;
      content.wrappedJSObject.document.body.append(f);
    });
  });
  const frameBC = contentPage.browsingContext.children[0];
  ok(frameBC, "Got frame");
  await contentPage.SpecialPowers.spawn(frameBC, [], () => {
    const { document } = content;
    Assert.equal(document.URL, "http://example.com/?blockme2", "Same URL");
    Assert.equal(document.body.textContent, "response from server", "body");
  });

  await contentPage.close();
  await extension.unload();
});

// In dnr_ignores_navigation_to_restrictedDomains we verified that DNR cannot
// block requests triggered from a principal with a restricted domain, for
// sub_frame requests. But for main_frame, we expect restricted domains to not
// be enforced because access check is based on loading principal, which is
// null for main_frame requests.
add_task(async function dnr_main_frame_not_affected_by_restricted_initiator() {
  // Unlike all other tests in this file, we use redirect rule, because it is
  // not easy to detect whether a main_frame request was blocked.
  let extension = ExtensionTestUtils.loadExtension({
    async background() {
      await browser.declarativeNetRequest.updateSessionRules({
        addRules: [
          {
            id: 1,
            condition: { urlFilter: "redirme", resourceTypes: ["main_frame"] },
            action: {
              type: "redirect",
              redirect: { transform: { query: "?dnrwashere" } },
            },
          },
        ],
      });
      browser.test.sendMessage("dnr_registered");
    },
    manifest: {
      permissions: ["declarativeNetRequest", "<all_urls>"],
    },
  });
  await extension.startup();
  await extension.awaitMessage("dnr_registered");

  let contentPage =
    await ExtensionTestUtils.loadContentPage("http://restricted/");
  info("Navigating to non-restricted main_frame with restricted initiator");
  await contentPage.spawn([], () => {
    content.wrappedJSObject.location = "http://example.com/?redirme";
  });
  await TestUtils.waitForCondition(
    () => contentPage.browser.documentURI.spec !== "http://restricted/",
    "Waiting for navigation to complete"
  );
  Assert.equal(
    contentPage.browser.documentURI.spec,
    "http://example.com/?dnrwashere",
    "restrictedDomains does not apply to initiator of main_frame requests"
  );

  await contentPage.close();
  await extension.unload();
});

// See test_ext_dnr_loadingPrincipal.js for the variant of this test that
// checks what host permissions are checked against with CSS @import.
add_task(async function dnr_ignores_css_import_at_restrictedDomains() {
  // CSS @import have triggeringPrincipal set to the URL of the stylesheet,
  // and the loadingPrincipal set to the web page. To verify that access is
  // indeed being restricted as expected, confirm that none of the stylesheet
  // requests are blocked by the DNR extension.
  let extension = await startDNRExtension();
  let contentPage =
    await ExtensionTestUtils.loadContentPage("http://restricted/");
  await contentPage.spawn([], async () => {
    // Use wrappedJSObject so that all operations below are with the principal
    // of the content instead of the system principal (from this ContentTask).
    const { document } = content.wrappedJSObject;
    const style = document.createElement("link");
    style.rel = "stylesheet";
    // Note: intentionally not at "http://restricted/" because we want to check
    // that subresources from a restricted domain are ignored by DNR..
    style.href = "http://example.com/style_with_import.css";
    style.crossOrigin = "anonymous";
    await new Promise(resolve => {
      info("Waiting for style sheet to load...");
      style.onload = resolve;
      document.head.append(style);
    });
    const importRule = style.sheet.cssRules[0];
    Assert.equal(
      importRule?.cssText,
      `@import url("http://example.com/imported.css");`,
      "Not blocked by DNR: Loaded style_with_import.css"
    );
    // Waiving Xrays here because we cannot read cssRules despite CORS because
    // that is not implemented for child stylesheets (loaded via @import):
    // https://searchfox.org/mozilla-central/rev/55d5c4b9dffe5e59eb6b019c1a930ec9ada47e10/layout/style/Loader.cpp#2052
    const importedStylesheet = Cu.unwaiveXrays(importRule.styleSheet);
    Assert.equal(
      importedStylesheet.cssRules[0]?.cssText,
      "imported_stylesheet_here { }",
      "Not blocked by DNR: Loaded import.css"
    );
  });
  await contentPage.close();
  await extension.unload();
});

add_task(
  { pref_set: [["extensions.dnr.feedback", true]] },
  async function testMatchOutcome_and_restrictedDomains() {
    let extension = ExtensionTestUtils.loadExtension({
      async background() {
        await browser.declarativeNetRequest.updateSessionRules({
          addRules: [
            {
              id: 1,
              condition: { excludedResourceTypes: [] },
              action: { type: "block" },
            },
          ],
        });
        const type = "other"; // matches the condition of the above rule.

        browser.test.assertDeepEq(
          { matchedRules: [] },
          await browser.declarativeNetRequest.testMatchOutcome({
            url: "http://restricted/",
            type,
          }),
          "testMatchOutcome ignores restricted url"
        );
        browser.test.assertDeepEq(
          { matchedRules: [] },
          await browser.declarativeNetRequest.testMatchOutcome({
            url: "http://example.com/",
            initiator: "http://restricted/",
            type,
          }),
          "testMatchOutcome ignores restricted initiator"
        );

        // Navigation requests can be matched without host permissions for the
        // initiator (bug 1825824), but other restrictions are still applied.
        //
        // But for consistency with real network requests, we do not check
        // access on "initiator" for main_frame requests. Real main_frame is
        // tested above in dnr_main_frame_not_affected_by_restricted_initiator.
        browser.test.assertDeepEq(
          { matchedRules: [{ ruleId: 1, rulesetId: "_session" }] },
          await browser.declarativeNetRequest.testMatchOutcome({
            url: "http://example.com/",
            initiator: "http://restricted/",
            type: "main_frame",
          }),
          "testMatchOutcome does not restrict initiator for main_frame"
        );
        browser.test.assertDeepEq(
          { matchedRules: [] },
          await browser.declarativeNetRequest.testMatchOutcome({
            url: "http://example.com/",
            initiator: "http://restricted/",
            type: "sub_frame",
          }),
          "testMatchOutcome restricts restricted initiator for sub_frame"
        );

        browser.test.sendMessage("done");
      },
      manifest: {
        manifest_version: 3,
        permissions: ["declarativeNetRequest", "declarativeNetRequestFeedback"],
      },
    });
    await extension.startup();
    await extension.awaitMessage("done");
    await extension.unload();
  }
);

add_task(
  // In debug builds, any attempt to load data:-URLs in the parent process
  // results in a crash or at least a logged error, via
  // nsContentSecurityUtils::ValidateScriptFilename.
  //
  // Xpcshell tests use loadFrameScript with data:-URLs, which could trigger the
  // above error / crash, when a page is loaded in the parent process.
  // For example, the following error message (or crash),
  // "InternalError: unsafe filename: data:text/javascript,//"
  // "Hit MOZ_CRASH(Blocking a script load data:text/javascript,// from file (None))"
  // is triggered because of the loadFrameScript call at
  // https://searchfox.org/mozilla-central/rev/11dbac7f64f509b78037465cbb4427ed71f8b565/testing/modules/XPCShellContentUtils.sys.mjs#308
  //
  // This test loads about:logo in the parent, because nsAboutRedirector.cpp
  // registers about:logo without nsIAboutModule::URI_MUST_LOAD_IN_CHILD.
  // When about:logo is loaded, the ContentPage test helper also triggers the
  // above error/crash at:
  // https://searchfox.org/mozilla-central/rev/11dbac7f64f509b78037465cbb4427ed71f8b565/testing/modules/XPCShellContentUtils.sys.mjs#224,242
  //
  // Opt out of the check/crash from ValidateScriptFilename:
  { pref_set: [["security.allow_parent_unrestricted_js_loads", true]] },
  async function non_system_request_with_disallowed_scheme() {
    let extension = await startDNRExtension();
    Assert.equal(
      await (await fetch("http://example.com/")).text(),
      "response from server",
      "DNR should not block requests from system principal"
    );
    // We are loading about:logo for the following reasons:
    // - It is a regular content principal, NOT a system principal.
    // - It is an about:-URL that resolves across all builds (part of toolkit/).
    // - It does not have a CSP (intentional - bug 1587417). That enables us to
    //   send a fetch() request below.
    let contentPage =
      await ExtensionTestUtils.loadContentPage("about:logo?blockme");
    await contentPage.spawn([], async () => {
      const { document } = content;
      // To make sure that the test does not pass trivially, we verify that it
      // is not the system principal (because dnr_ignores_system_requests
      // already tests that) and not a null principal (because that translates
      // to a void "initiator" in the DNR API, which would pass access checks).
      Assert.ok(
        document.nodePrincipal.isContentPrincipal,
        "about:logo has content principal (not system or NullPrincipal))"
      );
      Assert.equal(document.URL, "about:logo?blockme", "Same URL");
      Assert.equal(
        await (await content.fetch("http://example.com/")).text(),
        "response from server",
        "fetch() at about:logo not blocked by DNR"
      );
    });
    await contentPage.close();
    await extension.unload();
  }
);

add_task(
  { pref_set: [["extensions.dnr.feedback", true]] },
  async function testMatchOutcome_non_system_request_with_disallowed_scheme() {
    let extension = ExtensionTestUtils.loadExtension({
      async background() {
        await browser.declarativeNetRequest.updateSessionRules({
          addRules: [{ id: 1, condition: {}, action: { type: "block" } }],
        });
        const type = "other"; // matches the condition of the above rule.

        browser.test.assertDeepEq(
          { matchedRules: [] },
          await browser.declarativeNetRequest.testMatchOutcome({
            url: "about:logo",
            type,
          }),
          "testMatchOutcome ignores url with disallowed schema"
        );
        browser.test.assertDeepEq(
          { matchedRules: [] },
          await browser.declarativeNetRequest.testMatchOutcome({
            url: "http://example.com/",
            initiator: "about:logo",
            type,
          }),
          "testMatchOutcome ignores initiator with disallowed schema"
        );
        browser.test.sendMessage("done");
      },
      manifest: {
        manifest_version: 3,
        permissions: ["declarativeNetRequest", "declarativeNetRequestFeedback"],
      },
    });
    await extension.startup();
    await extension.awaitMessage("done");
    await extension.unload();
  }
);
