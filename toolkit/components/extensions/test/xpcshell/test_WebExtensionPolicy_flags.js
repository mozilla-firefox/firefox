"use strict";

// Tests WebExtensionPolicy.ACCESS_* flags with canAccessURI / canAccessDocument.

const { ContentPage } = ChromeUtils.importESModule(
  "resource://testing-common/XPCShellContentUtils.sys.mjs"
);

const { newURI } = Services.io;
const ssm = Services.scriptSecurityManager;

const {
  ACCESS_REQUIRE_EXPLICIT_DOMAIN,
  ACCESS_WITHOUT_HOST_PERMISSIONS,
  ACCESS_IGNORE_RESTRICTIONS,
  ACCESS_FILE_WITH_OPT_IN,
  ACCESS_FILE_ALWAYS,
  ACCESS_OWN_EXTENSION,
  ACCESS_OTHER_EXTENSIONS,
} = WebExtensionPolicy;

const OWN_UUID = "8a3c1d20-0000-4000-8000-000000000001";
const OTHER_UUID = "8a3c1d20-0000-4000-8000-000000000002";

const HTTP_URI = newURI("http://allowed.example.com/page");
const HTTPS_URI = newURI("https://allowed.example.com/page");
const EXPLICIT_URI = newURI("https://exact.tld/page");
const RESTRICTED_URI = newURI("https://restricted/page");
const FILE_URI = newURI("file:///tmp/file.txt");
const WS_URI = newURI("ws://allowed.example.com/socket");
const DATA_URI = newURI("data:text/html,hello");
const ABOUT_BLANK_URI = newURI("about:blank");
const ABOUT_ADDONS_URI = newURI("about:addons");
const RESOURCE_URI = newURI("resource://gre/modules/Services.sys.mjs");
const VIEW_SOURCE_URI = newURI("view-source:https://allowed.example.com/page");
const EXT_OWN_URI = newURI(`moz-extension://${OWN_UUID}/page.html`);
const EXT_OTHER_URI = newURI(`moz-extension://${OTHER_UUID}/page.html`);

Services.prefs.setCharPref(
  "extensions.webextensions.restrictedDomains",
  "restricted"
);

const server = AddonTestUtils.createHttpServer({
  hosts: ["allowed.example.com", "other.example.com", "restricted"],
});
server.registerPathHandler("/page", (request, response) => {
  response.setHeader("Content-Type", "text/html");
  response.write("page");
});
server.registerPathHandler("/sandboxed", (request, response) => {
  response.setHeader("Content-Type", "text/html");
  response.setHeader("Content-Security-Policy", "sandbox allow-scripts");
  response.write("sandboxed");
});
server.registerPathHandler("/sandboxed_to_blank", (request, response) => {
  response.setHeader("Content-Type", "text/html");
  response.setHeader("Content-Security-Policy", "sandbox allow-scripts");
  response.write(`<script>location.href = "about:blank";</script>`);
});

function contentPrincipal(uri) {
  return ssm.createContentPrincipal(uri, {});
}

function nullPrincipal(precursorOrigin) {
  let nullWithPrecursor = ssm.JSONToPrincipal(
    `{"0":{"0":"moz-nullprincipal:{4db87754-8ce2-4dc9-b1b5-4ed8f6a85334}?${precursorOrigin}"}}`
  );
  if (!nullWithPrecursor.precursorPrincipal) {
    throw new Error(`Unexpected: cannot set precursor to: ${precursorOrigin}`);
  }
  return nullWithPrecursor;
}

function makePolicy({
  allowedOrigins = new MatchPatternSet(["<all_urls>", "https://exact.tld/*"]),
} = {}) {
  return new WebExtensionPolicy({
    id: "flags@test",
    mozExtensionHostname: OWN_UUID,
    baseURL: "file:///path/to/ext/on/disk/",
    localizeCallback(str) {
      return str;
    },
    allowedOrigins,
    permissions: [],
    webAccessibleResources: [],
  });
}

let gExtension;

async function assertCanAccessDocument(
  contentPage,
  flags,
  expected,
  description
) {
  if (!gExtension) {
    gExtension = ExtensionTestUtils.loadExtension({
      manifest: {
        host_permissions: ["*://allowed.example.com/*", "*://restricted/*"],
      },
    });
    await gExtension.startup();
    registerCleanupFunction(() => gExtension.unload());
  }
  const policy = WebExtensionPolicy.getByID(gExtension.id);
  equal(
    policy.canAccessDocument(
      contentPage.browsingContext.currentWindowGlobal,
      flags
    ),
    expected,
    `${description} (WindowGlobalParent)`
  );
  equal(
    await contentPage.spawn([gExtension.id, flags], (id, flags) =>
      content.WebExtensionPolicy.getByID(id).canAccessDocument(
        content.windowGlobalChild,
        flags
      )
    ),
    expected,
    `${description} (WindowGlobalChild)`
  );
}

add_task(function test_default_no_flags() {
  const policy = makePolicy();
  ok(policy.canAccessURI(HTTPS_URI), "https matched by host permissions");
  ok(policy.canAccessURI(EXPLICIT_URI), "https matched by explicit pattern");
  ok(policy.canAccessURI(WS_URI), "ws is part of <all_urls>");
  ok(policy.canAccessURI(DATA_URI), "data: is part of <all_urls>");
  ok(!policy.canAccessURI(RESTRICTED_URI), "restricted domain is denied");
  ok(!policy.canAccessURI(FILE_URI), "file: is denied without a file: flag");
  ok(!policy.canAccessURI(ABOUT_BLANK_URI), "about: is not matched");
  ok(!policy.canAccessURI(RESOURCE_URI), "resource: is not matched");
  ok(!policy.canAccessURI(VIEW_SOURCE_URI), "view-source: is not matched");
  policy.active = false;
});

add_task(function test_explicit_domain() {
  const policy = makePolicy();
  ok(
    policy.canAccessURI(EXPLICIT_URI, ACCESS_REQUIRE_EXPLICIT_DOMAIN),
    "a pattern without a subdomain wildcard satisfies ACCESS_REQUIRE_EXPLICIT_DOMAIN"
  );
  ok(
    !policy.canAccessURI(HTTPS_URI, ACCESS_REQUIRE_EXPLICIT_DOMAIN),
    "<all_urls> does not satisfy ACCESS_REQUIRE_EXPLICIT_DOMAIN"
  );
  policy.active = false;
});

add_task(function test_ignore_restrictions() {
  const policy = makePolicy();
  ok(
    policy.canAccessURI(RESTRICTED_URI, ACCESS_IGNORE_RESTRICTIONS),
    "restricted domain is allowed with ACCESS_IGNORE_RESTRICTIONS"
  );

  policy.guardSets = [
    new ExtensionGuardSet({
      deny: ["https://allowed.example.com/*"],
      source: "enterprise-global",
    }),
  ];
  ok(!policy.canAccessURI(HTTPS_URI), "guarded url is denied");
  ok(
    policy.canAccessURI(HTTPS_URI, ACCESS_IGNORE_RESTRICTIONS),
    "guarded url is allowed with ACCESS_IGNORE_RESTRICTIONS"
  );
  policy.active = false;
});

add_task(function test_file_flags() {
  const policy = makePolicy();
  ok(
    !policy.canAccessURI(FILE_URI, ACCESS_FILE_WITH_OPT_IN),
    "ACCESS_FILE_WITH_OPT_IN denies without the opt-in permission"
  );
  ok(
    policy.canAccessURI(FILE_URI, ACCESS_FILE_ALWAYS),
    "ACCESS_FILE_ALWAYS allows file: without the opt-in permission"
  );

  policy.permissions = ["internal:fileSchemeAllowed"];
  ok(
    policy.canAccessURI(FILE_URI, ACCESS_FILE_WITH_OPT_IN),
    "ACCESS_FILE_WITH_OPT_IN allows file: with the opt-in permission"
  );
  ok(
    !policy.canAccessURI(FILE_URI),
    "file: is still denied without a file: flag after opting in"
  );
  policy.active = false;
});

add_task(function test_moz_extension_flags() {
  const policy = makePolicy();
  ok(
    !policy.canAccessURI(EXT_OWN_URI),
    "own moz-extension: denied without a flag"
  );
  ok(
    !policy.canAccessURI(EXT_OTHER_URI),
    "other moz-extension: denied without a flag"
  );
  ok(
    policy.canAccessURI(EXT_OWN_URI, ACCESS_OWN_EXTENSION),
    "own moz-extension: allowed by ACCESS_OWN_EXTENSION"
  );
  ok(
    !policy.canAccessURI(EXT_OTHER_URI, ACCESS_OWN_EXTENSION),
    "other moz-extension: not allowed by ACCESS_OWN_EXTENSION"
  );
  ok(
    policy.canAccessURI(EXT_OTHER_URI, ACCESS_OTHER_EXTENSIONS),
    "other moz-extension: allowed by ACCESS_OTHER_EXTENSIONS"
  );
  ok(
    !policy.canAccessURI(EXT_OWN_URI, ACCESS_OTHER_EXTENSIONS),
    "own moz-extension: not allowed by ACCESS_OTHER_EXTENSIONS"
  );
  ok(
    policy.canAccessURI(
      EXT_OWN_URI,
      ACCESS_OWN_EXTENSION | ACCESS_OTHER_EXTENSIONS
    ),
    "own moz-extension: allowed when both extension flags are set"
  );

  // The moz-extension: flag is enforced before guard sets, because it would
  // not make sense for a guard set to be able to prevent an extension from
  // accessing its own resources.
  policy.guardSets = [
    new ExtensionGuardSet({
      deny: [`moz-extension://${OWN_UUID}/*`],
      source: "enterprise-global",
    }),
  ];
  ok(
    policy.canAccessURI(EXT_OWN_URI, ACCESS_OWN_EXTENSION),
    "own moz-extension: cannot be guarded, allowed by ACCESS_OWN_EXTENSION"
  );
  policy.active = false;
});

add_task(function test_without_host_permissions() {
  // Extension without any host permissions, so when canAccessURI matches, it
  // is due to the scheme/URL being matched by the baseline set of permitted
  // schemes (in <all_urls>).
  const policy = makePolicy({ allowedOrigins: new MatchPatternSet([]) });

  ok(
    policy.canAccessURI(HTTPS_URI, ACCESS_WITHOUT_HOST_PERMISSIONS),
    "https: matched by baseline"
  );
  ok(
    policy.canAccessURI(WS_URI, ACCESS_WITHOUT_HOST_PERMISSIONS),
    "ws: matched by baseline"
  );
  ok(
    policy.canAccessURI(DATA_URI, ACCESS_WITHOUT_HOST_PERMISSIONS),
    "data: matched by baseline"
  );
  ok(
    !policy.canAccessURI(ABOUT_BLANK_URI, ACCESS_WITHOUT_HOST_PERMISSIONS),
    "about: not matched"
  );
  ok(
    !policy.canAccessURI(RESOURCE_URI, ACCESS_WITHOUT_HOST_PERMISSIONS),
    "resource: not matched"
  );
  ok(
    !policy.canAccessURI(VIEW_SOURCE_URI, ACCESS_WITHOUT_HOST_PERMISSIONS),
    "view-source: not matched"
  );

  // The baseline does not bypass the checks that precede it.
  ok(
    !policy.canAccessURI(RESTRICTED_URI, ACCESS_WITHOUT_HOST_PERMISSIONS),
    "restricted domain is denied despite the baseline containing https:"
  );
  ok(
    policy.canAccessURI(
      RESTRICTED_URI,
      ACCESS_WITHOUT_HOST_PERMISSIONS | ACCESS_IGNORE_RESTRICTIONS
    ),
    "restricted domain is allowed with ACCESS_IGNORE_RESTRICTIONS"
  );
  ok(
    !policy.canAccessURI(FILE_URI, ACCESS_WITHOUT_HOST_PERMISSIONS),
    "file: is denied without a file: flag"
  );
  ok(
    policy.canAccessURI(
      FILE_URI,
      ACCESS_WITHOUT_HOST_PERMISSIONS | ACCESS_FILE_ALWAYS
    ),
    "file: is allowed with ACCESS_FILE_ALWAYS"
  );
  ok(
    !policy.canAccessURI(EXT_OWN_URI, ACCESS_WITHOUT_HOST_PERMISSIONS),
    "own moz-extension: not matched by baseline"
  );
  ok(
    policy.canAccessURI(
      EXT_OWN_URI,
      ACCESS_WITHOUT_HOST_PERMISSIONS | ACCESS_OWN_EXTENSION
    ),
    "own moz-extension: allowed by ACCESS_OWN_EXTENSION despite baseline"
  );

  // ACCESS_WITHOUT_HOST_PERMISSIONS matches against `<all_urls>`, while
  // ACCESS_REQUIRE_EXPLICIT_DOMAIN ignores wildcard domains such as
  // `<all_urls>`. Enforcing both strictly would never match, so a caller might
  // as well not call canAccessURI. Therefore we ignore
  // ACCESS_REQUIRE_EXPLICIT_DOMAIN if ACCESS_WITHOUT_HOST_PERMISSIONS is set.
  ok(
    policy.canAccessURI(
      HTTPS_URI,
      ACCESS_WITHOUT_HOST_PERMISSIONS | ACCESS_REQUIRE_EXPLICIT_DOMAIN
    ),
    "ACCESS_REQUIRE_EXPLICIT_DOMAIN has no effect when host permissions are skipped"
  );

  policy.active = false;
});

// Given a content principal, its URI is checked, like canAccessURI.
add_task(async function test_canAccessDocument_content_principal() {
  const contentPage = await ExtensionTestUtils.loadContentPage(
    "http://allowed.example.com/page"
  );
  await assertCanAccessDocument(
    contentPage,
    0,
    true,
    "page is matched by host permissions"
  );

  await contentPage.loadURL("http://other.example.com/page");
  await assertCanAccessDocument(
    contentPage,
    0,
    false,
    "other.example.com is not matched"
  );
  await assertCanAccessDocument(
    contentPage,
    ACCESS_WITHOUT_HOST_PERMISSIONS,
    true,
    "other.example.com is allowed with ACCESS_WITHOUT_HOST_PERMISSIONS"
  );

  await contentPage.close();
});

// Restricted domains are denied, even if they are in host permissions.
add_task(async function test_canAccessDocument_restricted_domain() {
  const contentPage = await ExtensionTestUtils.loadContentPage(
    "http://restricted/page"
  );
  await assertCanAccessDocument(
    contentPage,
    0,
    false,
    "restricted domain is denied despite host permissions"
  );
  await contentPage.close();
});

// A view-source: document has a content principal whose URI is the
// view-source: URL, which is not matched by <all_urls>.
add_task(async function test_canAccessDocument_view_source() {
  const contentPage = await ExtensionTestUtils.loadContentPage(
    "view-source:http://allowed.example.com/page"
  );
  await assertCanAccessDocument(
    contentPage,
    ACCESS_WITHOUT_HOST_PERMISSIONS,
    false,
    "view-source: is not matched, even with ACCESS_WITHOUT_HOST_PERMISSIONS"
  );
  await contentPage.close();
});

// Given a null principal, the document URL is checked if it is non-opaque or
// if there is no precursor.
add_task(async function test_canAccessDocument_null_principal() {
  // A precursorless null principal falls back to the document URL, and
  // <all_urls> matches data:.
  const contentPage = await ExtensionTestUtils.loadContentPage(
    "data:text/html,data"
  );
  const { documentPrincipal } = contentPage.browsingContext.currentWindowGlobal;
  ok(
    documentPrincipal.isNullPrincipal,
    "Sanity check: top-level data: has a null principal"
  );
  ok(
    !documentPrincipal.precursorPrincipal,
    "Sanity check: top-level data: has no precursor"
  );
  await assertCanAccessDocument(
    contentPage,
    ACCESS_WITHOUT_HOST_PERMISSIONS,
    true,
    "top-level data: is allowed with ACCESS_WITHOUT_HOST_PERMISSIONS"
  );

  await contentPage.loadURL("http://allowed.example.com/sandboxed");
  ok(
    contentPage.browsingContext.currentWindowGlobal.documentPrincipal
      .isNullPrincipal,
    "Sanity check: CSP sandbox gives a null principal"
  );
  await assertCanAccessDocument(
    contentPage,
    0,
    true,
    "null principal (CSP sandbox) is matched by its URL"
  );

  await contentPage.close();
});

// Given a null principal for about:blank, its precursor is checked.
add_task(async function test_canAccessDocument_about_blank_precursor() {
  const contentPage = await ExtensionTestUtils.loadContentPage(
    "http://allowed.example.com/sandboxed_to_blank"
  );
  await TestUtils.waitForCondition(
    () =>
      contentPage.browsingContext.currentWindowGlobal?.documentURI.spec ===
      "about:blank",
    "Waiting for navigation to about:blank"
  );
  equal(
    contentPage.browsingContext.currentWindowGlobal.documentPrincipal
      .precursorPrincipal?.origin,
    "http://allowed.example.com",
    "Sanity check: about:blank has a null principal with precursor"
  );
  await assertCanAccessDocument(
    contentPage,
    0,
    true,
    "about:blank is matched by its precursor"
  );

  // Unlike a precursorless about:blank, ACCESS_WITHOUT_HOST_PERMISSIONS does
  // not allow this.
  await contentPage.loadURL("http://restricted/sandboxed_to_blank");
  await TestUtils.waitForCondition(
    () =>
      contentPage.browsingContext.currentWindowGlobal?.documentURI.spec ===
      "about:blank",
    "Waiting for navigation to about:blank"
  );
  equal(
    contentPage.browsingContext.currentWindowGlobal.documentPrincipal
      .precursorPrincipal?.origin,
    "http://restricted",
    "Sanity check: about:blank has a null principal with precursor"
  );
  await assertCanAccessDocument(
    contentPage,
    ACCESS_WITHOUT_HOST_PERMISSIONS,
    false,
    "about:blank with restricted precursor is denied, even with ACCESS_WITHOUT_HOST_PERMISSIONS"
  );

  await contentPage.close();
});

// Like content scripts (bug 2034938 comment 10), matches top-level about:blank.
add_task(async function test_canAccessDocument_top_level_about_blank() {
  const contentPage = await ExtensionTestUtils.loadContentPage("about:blank");
  const { documentPrincipal } = contentPage.browsingContext.currentWindowGlobal;
  ok(
    documentPrincipal.isNullPrincipal,
    "Sanity check: about:blank has a null principal"
  );
  ok(
    !documentPrincipal.precursorPrincipal,
    "Sanity check: about:blank has no precursor"
  );
  await assertCanAccessDocument(
    contentPage,
    0,
    false,
    "top-level about:blank is not matched"
  );
  await assertCanAccessDocument(
    contentPage,
    ACCESS_WITHOUT_HOST_PERMISSIONS,
    true,
    "top-level about:blank is allowed with ACCESS_WITHOUT_HOST_PERMISSIONS"
  );
  await contentPage.close();
});

// Like content scripts (bug 1901894), does not match uncommitted about:blank.
add_task(async function test_canAccessDocument_initial_about_blank() {
  // Unlike loadContentPage, a ContentPage that has not navigated yet still has
  // its uncommitted initial about:blank.
  const contentPage = new ContentPage();
  await contentPage.browserReady;
  const initialWindowGlobal = contentPage.browsingContext.currentWindowGlobal;
  ok(
    initialWindowGlobal.isUncommittedInitialDocument,
    "Sanity check: about:blank is the uncommitted initial document (WindowGlobalParent)"
  );
  ok(
    await contentPage.spawn(
      [],
      () => content.document.isUncommittedInitialDocument
    ),
    "Sanity check: about:blank is the uncommitted initial document (WindowGlobalChild)"
  );
  ok(
    initialWindowGlobal.documentPrincipal.isNullPrincipal,
    "Sanity check: uncommitted about:blank has a null principal"
  );
  ok(
    !initialWindowGlobal.documentPrincipal.precursorPrincipal,
    "Sanity check: uncommitted about:blank has no precursor"
  );
  await assertCanAccessDocument(
    contentPage,
    0,
    false,
    "uncommitted about:blank is not matched"
  );
  await assertCanAccessDocument(
    contentPage,
    ACCESS_WITHOUT_HOST_PERMISSIONS,
    false,
    "uncommitted about:blank is denied, even with ACCESS_WITHOUT_HOST_PERMISSIONS"
  );
  await contentPage.close();
});

add_task(async function test_access_restricted_scheme_resource() {
  // Although we could also use makePolicy to mint a policy, we are
  // intentionally creating a full real extension to also exercise the
  // manifest validation logic, to make sure that this test checks something
  // that can realistically be reached by a real extension.
  const extension = ExtensionTestUtils.loadExtension({
    isPrivileged: true,
    manifest: {
      // Uncomment MV3 to turn into regression test for bug 1910965:
      // manifest_version: 3,
      host_permissions: ["resource://pdf.js/*", "about:blank"],
      permissions: ["mozillaAddons"],
    },
  });
  await extension.startup();
  const policy = extension.extension.policy;

  const resourcePdfUri = newURI("resource://pdf.js/web/viewer.html");

  ok(!policy.canAccessURI(RESOURCE_URI), "resource: not in host permissions");
  ok(policy.canAccessURI(resourcePdfUri), "resource: in host permissions");

  // ACCESS_WITHOUT_HOST_PERMISSIONS has a well-defined set of supported
  // schemes, and the edge case of restricted schemes for privileged
  // extensions is not accounted for.
  ok(
    !policy.canAccessURI(resourcePdfUri, ACCESS_WITHOUT_HOST_PERMISSIONS),
    "resource: never matched with ACCESS_WITHOUT_HOST_PERMISSIONS"
  );

  ok(policy.canAccessURI(ABOUT_BLANK_URI), "about:blank in host permissions");
  ok(!policy.canAccessURI(ABOUT_ADDONS_URI), "about: not in host permissions");

  await extension.unload();
});

// Although WebExtensionPolicy.getURLForMatching does not take any flags, it is
// still in this file because it backs the canAccessDocument method.
add_task(function test_getURLForMatching() {
  function getURLForMatching(principal, documentURI) {
    return WebExtensionPolicy.getURLForMatching(principal, documentURI)?.spec;
  }

  // Document URLs matching <all_urls> that may be content principals.
  const baselineDocumentUris = [
    HTTP_URI,
    HTTPS_URI,
    // ws/wss/ftp cannot be loaded as documents so they are not listed here.
    FILE_URI,
    // data:-URLs not listed here because it does not have content principal.
  ];

  // Document URLs that don't match <all_urls> but are content principals.
  const otherDocumentUrls = [
    RESOURCE_URI, // Only available to privileged extensions.
    VIEW_SOURCE_URI,
    EXT_OWN_URI,
  ];

  // Document URLs that may be null principals.
  const allDocumentNullUrls = [
    ...baselineDocumentUris,
    ...otherDocumentUrls,
    DATA_URI,
  ];

  // Document URLs whose URL is more expressive than their principal URL.
  const nonOpaqueDocumentUrls = [
    HTTP_URI,
    HTTPS_URI,
    VIEW_SOURCE_URI,
    FILE_URI,
  ];

  // A dummy URI different than any of the others above.
  const unrelatedUri = EXPLICIT_URI;

  for (const uri of [...baselineDocumentUris, ...otherDocumentUrls]) {
    equal(
      uri.spec,
      getURLForMatching(contentPrincipal(uri), uri),
      "getURLForMatching: content principal returns itself"
    );
    equal(
      uri.spec,
      getURLForMatching(contentPrincipal(uri), unrelatedUri),
      "getURLForMatching: given content principal, documentURI is ignored"
    );
  }

  const nullNoPrecursor = ssm.createNullPrincipal({});
  for (const uri of allDocumentNullUrls) {
    equal(
      uri.spec,
      getURLForMatching(nullNoPrecursor, uri),
      "getURLForMatching: null principal without precursor returns documentURI"
    );
    const nullWithPrecursor = nullPrincipal(
      // Some URLs cannot be precursors themselves, use a different one.
      (uri === DATA_URI || uri === VIEW_SOURCE_URI ? HTTPS_URI : uri).prePath
    );
    if (nonOpaqueDocumentUrls.includes(uri)) {
      equal(
        uri.spec,
        getURLForMatching(nullWithPrecursor, uri),
        "getURLForMatching: null principal with non-opaque documentURI returns documentURI"
      );
    } else {
      equal(
        nullWithPrecursor.precursorPrincipal.URI.spec,
        getURLForMatching(nullWithPrecursor, uri),
        "getURLForMatching: null principal with precursor returns precursor"
      );
    }
  }
});
