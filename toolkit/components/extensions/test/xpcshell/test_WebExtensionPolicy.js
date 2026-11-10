"use strict";

const { newURI } = Services.io;

add_task(async function test_WebExtensionPolicy() {
  const id = "foo@bar.baz";
  const uuid = "ca9d3f23-125c-4b24-abfc-1ca2692b0610";

  const baseURL = "file:///foo/";
  const mozExtURL = `moz-extension://${uuid}/`;
  const mozExtURI = newURI(mozExtURL);

  let policy = new WebExtensionPolicy({
    id,
    mozExtensionHostname: uuid,
    baseURL,

    localizeCallback(str) {
      return `<${str}>`;
    },

    allowedOrigins: new MatchPatternSet(["http://foo.bar/", "*://*.baz/"], {
      ignorePath: true,
    }),
    permissions: ["<all_urls>"],
    webAccessibleResources: [
      {
        resources: ["/foo/*", "/bar.baz"].map(glob => new MatchGlob(glob)),
      },
    ],
  });

  equal(policy.active, false, "Active attribute should initially be false");

  // GetURL

  equal(
    policy.getURL(),
    mozExtURL,
    "getURL() should return the correct root URL"
  );
  equal(
    policy.getURL("path/foo.html"),
    `${mozExtURL}path/foo.html`,
    "getURL(path) should return the correct URL"
  );

  // Permissions

  deepEqual(
    policy.permissions,
    ["<all_urls>"],
    "Initial permissions should be correct"
  );

  ok(
    policy.hasPermission("<all_urls>"),
    "hasPermission should match existing permission"
  );
  ok(
    !policy.hasPermission("history"),
    "hasPermission should not match nonexistent permission"
  );

  Assert.throws(
    () => {
      policy.permissions[0] = "foo";
    },
    TypeError,
    "Permissions array should be frozen"
  );

  policy.permissions = ["history"];
  deepEqual(
    policy.permissions,
    ["history"],
    "Permissions should be updateable as a set"
  );

  ok(
    policy.hasPermission("history"),
    "hasPermission should match existing permission"
  );
  ok(
    !policy.hasPermission("<all_urls>"),
    "hasPermission should not match nonexistent permission"
  );

  // Origins

  ok(
    policy.canAccessURI(newURI("http://foo.bar/quux")),
    "Should be able to access permitted URI"
  );
  ok(
    policy.canAccessURI(newURI("https://x.baz/foo")),
    "Should be able to access permitted URI"
  );

  ok(
    !policy.canAccessURI(newURI("https://foo.bar/quux")),
    "Should not be able to access non-permitted URI"
  );

  policy.allowedOrigins = new MatchPatternSet(["https://foo.bar/"], {
    ignorePath: true,
  });

  ok(
    policy.canAccessURI(newURI("https://foo.bar/quux")),
    "Should be able to access updated permitted URI"
  );
  ok(
    !policy.canAccessURI(newURI("https://x.baz/foo")),
    "Should not be able to access removed permitted URI"
  );

  // Web-accessible resources

  ok(
    policy.isWebAccessiblePath("/foo/bar"),
    "Web-accessible glob should be web-accessible"
  );
  ok(
    policy.isWebAccessiblePath("/bar.baz"),
    "Web-accessible path should be web-accessible"
  );
  ok(
    !policy.isWebAccessiblePath("/bar.baz/quux"),
    "Non-web-accessible path should not be web-accessible"
  );

  ok(
    policy.sourceMayAccessPath(mozExtURI, "/bar.baz"),
    "Web-accessible path should be web-accessible to self"
  );

  // Localization

  equal(
    policy.localize("foo"),
    "<foo>",
    "Localization callback should work as expected"
  );

  // Protocol and lookups.

  let proto = Services.io
    .getProtocolHandler("moz-extension", uuid)
    .QueryInterface(Ci.nsISubstitutingProtocolHandler);

  deepEqual(
    WebExtensionPolicy.getActiveExtensions(),
    [],
    "Should have no active extensions"
  );
  equal(
    WebExtensionPolicy.getByID(id),
    null,
    "ID lookup should not return extension when not active"
  );
  equal(
    WebExtensionPolicy.getByHostname(uuid),
    null,
    "Hostname lookup should not return extension when not active"
  );
  Assert.throws(
    () => proto.resolveURI(mozExtURI),
    /NS_ERROR_NOT_AVAILABLE/,
    "URL should not resolve when not active"
  );

  policy.active = true;
  equal(policy.active, true, "Active attribute should be updated");

  let exts = WebExtensionPolicy.getActiveExtensions();
  equal(exts.length, 1, "Should have one active extension");
  equal(exts[0], policy, "Should have the correct active extension");

  equal(
    WebExtensionPolicy.getByID(id),
    policy,
    "ID lookup should return extension when active"
  );
  equal(
    WebExtensionPolicy.getByHostname(uuid),
    policy,
    "Hostname lookup should return extension when active"
  );

  equal(
    proto.resolveURI(mozExtURI),
    baseURL,
    "URL should resolve correctly while active"
  );

  policy.active = false;
  equal(policy.active, false, "Active attribute should be updated");

  deepEqual(
    WebExtensionPolicy.getActiveExtensions(),
    [],
    "Should have no active extensions"
  );
  equal(
    WebExtensionPolicy.getByID(id),
    null,
    "ID lookup should not return extension when not active"
  );
  equal(
    WebExtensionPolicy.getByHostname(uuid),
    null,
    "Hostname lookup should not return extension when not active"
  );
  Assert.throws(
    () => proto.resolveURI(mozExtURI),
    /NS_ERROR_NOT_AVAILABLE/,
    "URL should not resolve when not active"
  );

  // Conflicting policies.

  // This asserts in debug builds, so only test in non-debug builds.
  if (!AppConstants.DEBUG) {
    policy.active = true;

    let attrs = [
      { id, uuid },
      { id, uuid: "d916886c-cfdf-482e-b7b1-d7f5b0facfa5" },
      { id: "foo@quux", uuid },
    ];

    // eslint-disable-next-line no-shadow
    for (let { id, uuid } of attrs) {
      let policy2 = new WebExtensionPolicy({
        id,
        mozExtensionHostname: uuid,
        baseURL: "file://bar/",

        localizeCallback() {},

        allowedOrigins: new MatchPatternSet([]),
      });

      Assert.throws(
        () => {
          policy2.active = true;
        },
        /NS_ERROR_UNEXPECTED/,
        `Should not be able to activate conflicting policy: ${id} ${uuid}`
      );
    }

    policy.active = false;
  }
});

// mozExtensionHostname is normalized to lower case when using
// policy.getURL whereas using policy.getByHostname does
// not.  Tests below will fail without case insensitive
// comparisons in ExtensionPolicyService
add_task(async function test_WebExtensionPolicy_case_sensitivity() {
  const id = "policy-case@mochitest";
  const uuid = "BAD93A23-125C-4B24-ABFC-1CA2692B0610";

  const baseURL = "file:///foo/";
  const mozExtURL = `moz-extension://${uuid}/`;
  const mozExtURI = newURI(mozExtURL);

  let policy = new WebExtensionPolicy({
    id: id,
    mozExtensionHostname: uuid,
    baseURL,
    localizeCallback() {},
    allowedOrigins: new MatchPatternSet([]),
    permissions: ["<all_urls>"],
  });
  policy.active = true;

  equal(
    WebExtensionPolicy.getByHostname(uuid)?.mozExtensionHostname,
    policy.mozExtensionHostname,
    "Hostname lookup should match policy"
  );

  equal(
    WebExtensionPolicy.getByHostname(uuid.toLowerCase())?.mozExtensionHostname,
    policy.mozExtensionHostname,
    "Hostname lookup should match policy"
  );

  equal(policy.getURL(), mozExtURI.spec, "Urls should match policy");
  ok(
    policy.sourceMayAccessPath(mozExtURI, "/bar.baz"),
    "Extension path should be accessible to self"
  );

  policy.active = false;
});

add_task(async function test_WebExtensionPolicy_V3() {
  const id = "foo@bar.baz";
  const uuid = "ca9d3f23-125c-4b24-abfc-1ca2692b0610";
  const id2 = "foo-2@bar.baz";
  const uuid2 = "89383c45-7db4-4999-83f7-f4cc246372cd";
  const id3 = "foo-3@bar.baz";
  const uuid3 = "56652231-D7E2-45D1-BDBD-BD3BFF80927E";

  const baseURL = "file:///foo/";
  const mozExtURL = `moz-extension://${uuid}/`;
  const mozExtURI = newURI(mozExtURL);
  const fooSite = newURI("http://foo.bar/");
  const exampleSite = newURI("https://example.com/");

  let policy = new WebExtensionPolicy({
    id,
    mozExtensionHostname: uuid,
    baseURL,
    manifestVersion: 3,

    localizeCallback(str) {
      return `<${str}>`;
    },

    allowedOrigins: new MatchPatternSet(["http://foo.bar/", "*://*.baz/"], {
      ignorePath: true,
    }),
    permissions: ["<all_urls>"],
    webAccessibleResources: [
      {
        resources: ["/foo/*", "/bar.baz"].map(glob => new MatchGlob(glob)),
        matches: ["http://foo.bar/"],
        extension_ids: [id3],
      },
      {
        resources: ["/foo.bar.baz"].map(glob => new MatchGlob(glob)),
        extension_ids: ["*"],
      },
    ],
  });
  policy.active = true;
  equal(
    WebExtensionPolicy.getByHostname(uuid),
    policy,
    "Hostname lookup should match policy"
  );

  let policy2 = new WebExtensionPolicy({
    id: id2,
    mozExtensionHostname: uuid2,
    baseURL,
    localizeCallback() {},
    allowedOrigins: new MatchPatternSet([]),
    permissions: ["<all_urls>"],
  });
  policy2.active = true;
  equal(
    WebExtensionPolicy.getByHostname(uuid2),
    policy2,
    "Hostname lookup should match policy"
  );

  let policy3 = new WebExtensionPolicy({
    id: id3,
    mozExtensionHostname: uuid3,
    baseURL,
    localizeCallback() {},
    allowedOrigins: new MatchPatternSet([]),
    permissions: ["<all_urls>"],
  });
  policy3.active = true;
  equal(
    WebExtensionPolicy.getByHostname(uuid3),
    policy3,
    "Hostname lookup should match policy"
  );

  ok(
    policy.isWebAccessiblePath("/bar.baz"),
    "Web-accessible path should be web-accessible"
  );
  ok(
    !policy.isWebAccessiblePath("/bar.baz/quux"),
    "Non-web-accessible path should not be web-accessible"
  );
  // Extension can always access itself
  ok(
    policy.sourceMayAccessPath(mozExtURI, "/bar.baz"),
    "Web-accessible path should be accessible to self"
  );
  ok(
    policy.sourceMayAccessPath(mozExtURI, "/foo.bar.baz"),
    "Web-accessible path should be accessible to self"
  );

  ok(
    !policy.sourceMayAccessPath(newURI(`https://${uuid}/`), "/bar.baz"),
    "Web-accessible path should not be accessible due to scheme mismatch"
  );

  // non-matching site cannot access url
  ok(
    policy.sourceMayAccessPath(fooSite, "/bar.baz"),
    "Web-accessible path should be accessible to foo.bar site"
  );
  ok(
    !policy.sourceMayAccessPath(fooSite, "/foo.bar.baz"),
    "Web-accessible path should not be accessible to foo.bar site"
  );

  // non-matching site cannot access url
  ok(
    !policy.sourceMayAccessPath(exampleSite, "/bar.baz"),
    "Web-accessible path should not be accessible to example.com"
  );
  ok(
    !policy.sourceMayAccessPath(exampleSite, "/foo.bar.baz"),
    "Web-accessible path should not be accessible to example.com"
  );

  let extURI = newURI(policy2.getURL(""));
  ok(
    !policy.sourceMayAccessPath(extURI, "/bar.baz"),
    "Web-accessible path should not be accessible to other extension"
  );
  ok(
    policy.sourceMayAccessPath(extURI, "/foo.bar.baz"),
    "Web-accessible path should be accessible to other extension"
  );

  extURI = newURI(policy3.getURL(""));
  ok(
    policy.sourceMayAccessPath(extURI, "/bar.baz"),
    "Web-accessible path should be accessible to other extension"
  );
  ok(
    policy.sourceMayAccessPath(extURI, "/foo.bar.baz"),
    "Web-accessible path should be accessible to other extension"
  );

  policy.active = false;
  policy2.active = false;
  policy3.active = false;
});

add_task(async function test_WebExtensionPolicy_registerContentScripts() {
  const id = "foo@bar.baz";
  const uuid = "77a7b9d3-e73c-4cf3-97fb-1824868fe00f";

  const id2 = "foo-2@bar.baz";
  const uuid2 = "89383c45-7db4-4999-83f7-f4cc246372cd";

  const baseURL = "file:///foo/";

  const mozExtURL = `moz-extension://${uuid}/`;
  const mozExtURL2 = `moz-extension://${uuid2}/`;

  let policy = new WebExtensionPolicy({
    id,
    mozExtensionHostname: uuid,
    baseURL,
    localizeCallback() {},
    allowedOrigins: new MatchPatternSet([]),
    permissions: ["<all_urls>"],
  });

  let policy2 = new WebExtensionPolicy({
    id: id2,
    mozExtensionHostname: uuid2,
    baseURL,
    localizeCallback() {},
    allowedOrigins: new MatchPatternSet([]),
    permissions: ["<all_urls>"],
  });

  let script1 = new WebExtensionContentScript(policy, {
    run_at: "document_end",
    js: [`${mozExtURL}/registered-content-script.js`],
    matches: new MatchPatternSet(["http://localhost/data/*"]),
  });

  let script2 = new WebExtensionContentScript(policy, {
    run_at: "document_end",
    css: [`${mozExtURL}/registered-content-style.css`],
    matches: new MatchPatternSet(["http://localhost/data/*"]),
  });

  let script3 = new WebExtensionContentScript(policy2, {
    run_at: "document_end",
    css: [`${mozExtURL2}/registered-content-style.css`],
    matches: new MatchPatternSet(["http://localhost/data/*"]),
  });

  deepEqual(
    policy.contentScripts,
    [],
    "The policy contentScripts is initially empty"
  );

  policy.registerContentScript(script1);

  deepEqual(
    policy.contentScripts,
    [script1],
    "script1 has been added to the policy contentScripts"
  );

  Assert.throws(
    () => policy.registerContentScript(script1),
    e => e.result == Cr.NS_ERROR_ILLEGAL_VALUE,
    "Got the expected NS_ERROR_ILLEGAL_VALUE when trying to register a script more than once"
  );

  Assert.throws(
    () => policy.registerContentScript(script3),
    e => e.result == Cr.NS_ERROR_ILLEGAL_VALUE,
    "Got the expected NS_ERROR_ILLEGAL_VALUE when trying to register a script related to " +
      "a different extension"
  );

  Assert.throws(
    () => policy.unregisterContentScript(script3),
    e => e.result == Cr.NS_ERROR_ILLEGAL_VALUE,
    "Got the expected NS_ERROR_ILLEGAL_VALUE when trying to unregister a script related to " +
      "a different extension"
  );

  deepEqual(
    policy.contentScripts,
    [script1],
    "script1 has not been added twice"
  );

  policy.registerContentScript(script2);

  deepEqual(
    policy.contentScripts,
    [script1, script2],
    "script2 has the last item of the policy contentScripts array"
  );

  policy.unregisterContentScript(script1);

  deepEqual(
    policy.contentScripts,
    [script2],
    "script1 has been removed from the policy contentscripts"
  );

  Assert.throws(
    () => policy.unregisterContentScript(script1),
    e => e.result == Cr.NS_ERROR_ILLEGAL_VALUE,
    "Got the expected NS_ERROR_ILLEGAL_VALUE when trying to unregister a script more than once"
  );

  deepEqual(
    policy.contentScripts,
    [script2],
    "the policy contentscripts is unmodified when unregistering an unknown contentScript"
  );

  policy.unregisterContentScript(script2);

  deepEqual(
    policy.contentScripts,
    [],
    "script2 has been removed from the policy contentScripts"
  );
});

add_task(async function test_WebExtensionPolicy_static_themes_resources() {
  const uuid = "0e7ae607-b5b3-4204-9838-c2138c14bc3c";
  const mozExtURL = `moz-extension://${uuid}/`;
  const mozExtURI = newURI(mozExtURL);

  let policy = new WebExtensionPolicy({
    id: "test-extension@mochitest",
    mozExtensionHostname: uuid,
    baseURL: "file:///foo/foo/",
    localizeCallback() {},
    allowedOrigins: new MatchPatternSet([]),
    permissions: [],
  });
  policy.active = true;

  let staticThemePolicy = new WebExtensionPolicy({
    id: "statictheme@bar.baz",
    mozExtensionHostname: "164d05dc-b45b-4731-aefc-7c1691bae9a4",
    baseURL: "file:///static_theme/",
    type: "theme",
    allowedOrigins: new MatchPatternSet([]),
    localizeCallback() {},
  });

  staticThemePolicy.active = true;

  ok(
    staticThemePolicy.sourceMayAccessPath(mozExtURI, "/someresource.ext"),
    "Active extensions should be allowed to access the static themes resources"
  );

  policy.active = false;

  ok(
    !staticThemePolicy.sourceMayAccessPath(mozExtURI, "/someresource.ext"),
    "Disabled extensions should be disallowed the static themes resources"
  );

  ok(
    !staticThemePolicy.sourceMayAccessPath(
      Services.io.newURI("http://example.com"),
      "/someresource.ext"
    ),
    "Web content should be disallowed the static themes resources"
  );

  staticThemePolicy.active = false;
});

add_task(async function test_guardSets() {
  const id = "applied-policies@test";
  const uuid = "deadbeef-0000-0000-0000-000000000001";
  const baseURL = "file:///ext/";

  let policy = new WebExtensionPolicy({
    id,
    mozExtensionHostname: uuid,
    baseURL,
    localizeCallback(str) {
      return str;
    },
    allowedOrigins: new MatchPatternSet([]),
  });
  policy.active = true;

  // Initially empty.
  deepEqual(policy.guardSets, [], "guardSets starts empty");
  strictEqual(
    policy.checkGuarded(newURI("https://example.com/")),
    null,
    "checkGuarded returns null when no guards set"
  );

  // Set guards.
  const globalGuard = new ExtensionGuardSet({
    deny: ["https://*.example.com/*"],
    source: "enterprise-global",
  });
  const perExtGuard = new ExtensionGuardSet({
    deny: ["https://*.example.org/*", "http://internal/"],
    source: "enterprise-per-extension",
  });
  policy.guardSets = [globalGuard, perExtGuard];

  const roundTripped = policy.guardSets;
  equal(roundTripped.length, 2, "guardSets round-trips with correct length");
  equal(
    roundTripped[0].source,
    "enterprise-global",
    "first guard source round-trips"
  );
  equal(
    roundTripped[1].source,
    "enterprise-per-extension",
    "second guard source round-trips"
  );

  // checkGuarded returns source of first matching guard.
  strictEqual(
    policy.checkGuarded(newURI("https://example.com/page")),
    "enterprise-global",
    "Returns source of first matching guard"
  );
  strictEqual(
    policy.checkGuarded(newURI("https://other.example.org/page")),
    "enterprise-per-extension",
    "Returns source of second matching guard"
  );
  strictEqual(
    policy.checkGuarded(newURI("http://internal/")),
    "enterprise-per-extension",
    "Returns source for internal URL"
  );
  strictEqual(
    policy.checkGuarded(newURI("https://unrelated.example/")),
    null,
    "Returns null when no guard matches"
  );

  // First match wins when guards overlap.
  const overlappingGuard = new ExtensionGuardSet({
    deny: ["https://*.example.com/*"],
    source: "enterprise-per-extension",
  });
  policy.guardSets = [globalGuard, overlappingGuard];
  strictEqual(
    policy.checkGuarded(newURI("https://example.com/page")),
    "enterprise-global",
    "First matching guard wins"
  );

  // except overrides deny.
  const guardWithExcept = new ExtensionGuardSet({
    deny: ["https://*.example.com/*"],
    except: ["https://allowed.example.com/*"],
    source: "enterprise-global",
  });
  policy.guardSets = [guardWithExcept];
  strictEqual(
    policy.checkGuarded(newURI("https://blocked.example.com/")),
    "enterprise-global",
    "Returns source for URL in deny but not in except"
  );
  strictEqual(
    policy.checkGuarded(newURI("https://allowed.example.com/")),
    null,
    "Returns null for URL covered by except"
  );

  // Clearing the list.
  policy.guardSets = [];
  strictEqual(
    policy.checkGuarded(newURI("https://example.com/page")),
    null,
    "Returns null after clearing guardSets"
  );

  policy.active = false;
});

add_task(async function test_canAccessURI_with_guardSets() {
  const policy = new WebExtensionPolicy({
    id: "guard-test@bar.baz",
    mozExtensionHostname: "guard-test-uuid",
    baseURL: "file:///guard-test/",
    localizeCallback(str) {
      return str;
    },
    allowedOrigins: new MatchPatternSet([
      "https://*.example.com/*",
      "https://*.example.org/*",
    ]),
    permissions: [],
    webAccessibleResources: [],
  });
  policy.active = true;

  ok(
    policy.canAccessURI(newURI("https://example.com/page")),
    "canAccessURI returns true before any guardSets"
  );

  const guard = new ExtensionGuardSet({
    deny: ["https://*.example.com/*"],
    source: "enterprise-global",
  });
  policy.guardSets = [guard];

  ok(
    !policy.canAccessURI(newURI("https://example.com/page")),
    "canAccessURI returns false when URI is denied by a guard"
  );
  ok(
    policy.canAccessURI(newURI("https://other.example.org/page")),
    "canAccessURI returns true for URI not covered by the guard"
  );

  const guardWithExcept = new ExtensionGuardSet({
    deny: ["https://*.example.com/*"],
    except: ["https://other.example.com/*"],
    source: "enterprise-global",
  });
  policy.guardSets = [guardWithExcept];

  ok(
    !policy.canAccessURI(newURI("https://example.com/page")),
    "canAccessURI returns false for domain in deny but not in except"
  );
  ok(
    policy.canAccessURI(newURI("https://other.example.com/page")),
    "canAccessURI returns true for domain covered by except"
  );

  policy.guardSets = [];
  ok(
    policy.canAccessURI(newURI("https://example.com/page")),
    "canAccessURI returns true again after clearing guardSets"
  );

  policy.active = false;
});
