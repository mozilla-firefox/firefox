/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Resolve the test directory so the resource: page has a content principal.
const kTestRoot = getResolvedURI(getRootDirectory(gTestPath)).spec;
const kTestPage =
  "resource://test-module-cors-doc/file_resource_module_cors.html";

const gResProto = Services.io
  .getProtocolHandler("resource")
  .QueryInterface(Ci.nsISubstitutingProtocolHandler);

add_setup(async function () {
  // Use separate resource: origins for the document and imported module.
  info(`resource: substitutions point at ${kTestRoot}`);
  for (const root of ["test-module-cors-doc", "test-module-cors-lib"]) {
    gResProto.setSubstitution(root, Services.io.newURI(kTestRoot));
  }

  registerCleanupFunction(() => {
    for (const root of ["test-module-cors-doc", "test-module-cors-lib"]) {
      gResProto.setSubstitution(root, null);
    }
  });
});

add_task(async function test_resource_document_module_loads() {
  await BrowserTestUtils.withNewTab(kTestPage, async browser => {
    const results = JSON.parse(
      await SpecialPowers.spawn(browser, [], async () => {
        await ContentTaskUtils.waitForCondition(
          () => content.wrappedJSObject.moduleLoadResults,
          "the test page finished its module loads"
        );
        return content.wrappedJSObject.moduleLoadResults;
      })
    );

    is(
      results.resourceCrossOrigin,
      "resource-dep",
      "a resource: document can load a module from another resource: origin"
    );
    is(
      results.chromeWidget,
      "blocked",
      "a resource: document cannot load a chrome: module"
    );
    ok(
      !results.customElementDefined,
      "the widget from chrome: did not get defined"
    );
    is(
      results.httpWithCORS,
      "http-dep",
      "an http module served with CORS headers still loads"
    );
    is(
      results.httpWithoutCORS,
      "blocked",
      "an http module served without CORS headers is still blocked"
    );
  });
});

// The CORS exception must not bypass the existing moz-src: load restriction.
add_task(async function test_moz_src_is_not_reachable() {
  const secMan = Services.scriptSecurityManager;
  const principal = secMan.createContentPrincipal(
    Services.io.newURI("resource://test-module-cors-doc/file.html"),
    {}
  );

  let result = Cr.NS_OK;
  try {
    secMan.checkLoadURIWithPrincipal(
      principal,
      Services.io.newURI("moz-src:///toolkit/actors/AboutPDFChild.sys.mjs"),
      Ci.nsIScriptSecurityManager.ALLOW_CHROME,
      0
    );
  } catch (e) {
    result = e.result;
  }

  is(
    result,
    Cr.NS_ERROR_DOM_BAD_URI,
    "a resource: principal cannot reach moz-src: even with ALLOW_CHROME"
  );
});

add_task(async function test_pdfjs_can_reach_chrome() {
  const secMan = Services.scriptSecurityManager;
  const target = Services.io.newURI(
    "chrome://global/content/elements/moz-message-bar.mjs"
  );
  const check = spec => {
    try {
      secMan.checkLoadURIWithPrincipal(
        secMan.createContentPrincipal(Services.io.newURI(spec), {}),
        target,
        Ci.nsIScriptSecurityManager.ALLOW_CHROME |
          Ci.nsIScriptSecurityManager.DONT_REPORT_ERRORS,
        0
      );
      return Cr.NS_OK;
    } catch (e) {
      return e.result;
    }
  };
  is(
    check("resource://pdf.js/web/viewer.html"),
    Cr.NS_OK,
    "pdf.js can load chrome: modules"
  );
  is(
    check("resource://test-module-cors-doc/file.html"),
    Cr.NS_ERROR_DOM_BAD_URI,
    "other resource: principals cannot load chrome: modules"
  );
});
