"use strict";

const { newURI } = Services.io;

const aboutNewtabURI = newURI("about:newtab");
const aboutHomeURI = newURI("about:home");
const aboutBlankURI = newURI("about:blank");

function makeWebExtensionPolicy({
  id,
  uuid,
  isPrivileged,
  webAccessibleResources = [],
}) {
  let policy = new WebExtensionPolicy({
    id,
    mozExtensionHostname: uuid,
    baseURL: "file:///foo/",
    manifestVersion: 3,
    isPrivileged,
    localizeCallback() {},
    allowedOrigins: new MatchPatternSet([]),
    permissions: [],
    webAccessibleResources,
  });
  policy.active = true;
  return policy;
}

add_task(async function test_aboutnewtab_access_to_privileged_extension() {
  let policy = makeWebExtensionPolicy({
    id: "privileged@test",
    uuid: "b1f1c1f4-4b7a-4a3a-9b3a-7a1c8f0a1a11",
    isPrivileged: true,
  });

  ok(
    policy.sourceMayAccessPath(aboutNewtabURI, "/ext-asset.html"),
    "about:newtab should have access to privileged extension resources without them being listed as web accessible"
  );
  ok(
    policy.sourceMayAccessPath(aboutHomeURI, "/ext-asset.html"),
    "about:home should have access to privileged extension resources without them being listed as web accessible"
  );
  ok(
    !policy.sourceMayAccessPath(aboutBlankURI, "/ext-asset.html"),
    "Other about: pages should not have access to non-'web accessible' extension resources"
  );

  policy.active = false;
});

add_task(async function test_aboutnewtab_access_to_unprivileged_extension() {
  // moz-extension:// subframes of privilegedabout pages are only isolated in
  // the extension process when remote extensions are enabled (Bug 2070189),
  // otherwise they would run in the privilegedabout process.
  const expectAllowed = WebExtensionPolicy.useRemoteWebExtensions;
  info(`Testing with useRemoteWebExtensions=${expectAllowed}`);

  let policy = makeWebExtensionPolicy({
    id: "unprivileged@test",
    uuid: "2e6c8e2a-9b3e-4f7a-8a3a-1a2b3c4d5e6f",
    isPrivileged: false,
  });
  let policyWithWebAccessibleResources = makeWebExtensionPolicy({
    id: "unprivileged-with-war@test",
    uuid: "3f7d9e2a-1b3e-4f7a-8a3a-1a2b3c4d5e70",
    isPrivileged: false,
    webAccessibleResources: [{ resources: [new MatchGlob("/ext-asset.html")] }],
  });

  for (let uri of [aboutNewtabURI, aboutHomeURI]) {
    equal(
      policy.sourceMayAccessPath(uri, "/ext-asset.html"),
      expectAllowed,
      `${uri.spec} access to non-'web accessible' resources from non-privileged extensions`
    );
    equal(
      policyWithWebAccessibleResources.sourceMayAccessPath(
        uri,
        "/ext-asset.html"
      ),
      expectAllowed,
      `${uri.spec} access to web accessible resources from non-privileged extensions`
    );
  }
  ok(
    !policy.sourceMayAccessPath(aboutBlankURI, "/ext-asset.html"),
    "Other about: pages should not have access to non-'web accessible' resources from non-privileged extensions"
  );

  policy.active = false;
  policyWithWebAccessibleResources.active = false;
});
