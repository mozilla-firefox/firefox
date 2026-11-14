"use strict";

const profileDir = do_get_profile();

const { ContextualIdentityService } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/contextualidentity/ContextualIdentityService.sys.mjs"
);

const TOPIC = "contextual-identity-site-association-changed";
const MAX_USER_CONTEXT_ID = -1 >>> 0;

let gCounter = 0;
function freshService() {
  let path = PathUtils.join(
    profileDir.path,
    `test-site-associations-${gCounter++}.json`
  );
  return {
    cis: ContextualIdentityService.createNewInstanceForTesting(path),
    path,
  };
}

function recordNotifications(callback) {
  let observed = [];
  let observer = subject => observed.push(subject.wrappedJSObject);
  Services.obs.addObserver(observer, TOPIC);
  try {
    callback();
  } finally {
    Services.obs.removeObserver(observer, TOPIC);
  }
  return observed;
}

// set / get / remove and their notifications.
add_task(function basic() {
  let { cis } = freshService();

  equal(cis.getSiteAssociation("example.com"), 0, "No association by default");

  let added = recordNotifications(() =>
    cis.setSiteAssociation("example.com", 2)
  );
  equal(cis.getSiteAssociation("example.com"), 2, "Association was set");
  equal(added.length, 1, "One notification on set");
  equal(added[0].site, "example.com", "Notified host matches");
  equal(added[0].userContextId, 2, "Notified userContextId matches");

  let noop = recordNotifications(() =>
    cis.setSiteAssociation("example.com", 2)
  );
  equal(noop.length, 0, "Re-setting the same association is a no-op");

  let removed = recordNotifications(() =>
    cis.removeSiteAssociation("example.com")
  );
  equal(cis.getSiteAssociation("example.com"), 0, "Association was removed");
  equal(removed.length, 1, "One notification on remove");
  equal(removed[0].site, "example.com", "Notified host matches");
  equal(removed[0].userContextId, null, "userContextId is null on removal");

  let missing = recordNotifications(() =>
    cis.removeSiteAssociation("example.com")
  );
  equal(missing.length, 0, "Removing a missing association notifies nothing");
});

// A host belongs to a single container: re-associating moves it.
add_task(function oneToOne() {
  let { cis } = freshService();

  cis.setSiteAssociation("example.com", 2);
  cis.setSiteAssociation("example.com", 3);

  equal(cis.getSiteAssociation("example.com"), 3, "Association moved to 3");
  Assert.deepEqual(
    cis.getSiteAssociations(2),
    [],
    "Container 2 no longer has the host"
  );
  Assert.deepEqual(
    cis.getSiteAssociations(3),
    [{ site: "example.com", userContextId: 3 }],
    "Container 3 owns the host"
  );
});

// Hosts are normalized: lowercased and IDN-encoded.
add_task(function normalization() {
  let { cis } = freshService();

  cis.setSiteAssociation("EXAMPLE.COM", 2);
  equal(cis.getSiteAssociation("example.com"), 2, "Case is normalized");

  cis.setSiteAssociation("bücher.example", 3);
  let stored = cis.getSiteAssociations(3);
  equal(stored.length, 1, "IDN host stored");
  ok(stored[0].site.includes("xn--"), "IDN host stored as punycode");
  equal(
    cis.getSiteAssociation("BÜCHER.example"),
    3,
    "IDN lookup matches regardless of case"
  );

  Assert.throws(
    () => cis.setSiteAssociation("", 2),
    /Invalid site/,
    "Empty site is rejected"
  );

  Assert.throws(
    () => cis.setSiteAssociation("  example.com  ", 2),
    /Invalid site/,
    "Site with whitespace is rejected"
  );

  equal(
    cis.getSiteAssociation("example .com"),
    0,
    "Lookup of a site with whitespace does not match"
  );

  equal(
    recordNotifications(() => cis.removeSiteAssociation("  example.com  "))
      .length,
    0,
    "Removal of a site with whitespace is a no-op"
  );

  for (let invalid of [
    "example.com:8080",
    "example.com/path",
    "user@example.com",
    "exa%mple.com",
    "[::1]",
    "*",
    "*.example.com",
  ]) {
    Assert.throws(
      () => cis.setSiteAssociation(invalid, 2),
      /Invalid site/,
      `${invalid} is not a valid host`
    );
    equal(
      cis.getSiteAssociation(invalid),
      0,
      `Lookup of ${invalid} does not match`
    );
    equal(
      recordNotifications(() => cis.removeSiteAssociation(invalid)).length,
      0,
      `Removal of ${invalid} is a no-op`
    );
    equal(cis.normalizeSite(invalid), null, `normalizeSite rejects ${invalid}`);
  }

  equal(
    cis.normalizeSite("EXAMPLE.com"),
    "example.com",
    "normalizeSite lowercases a host"
  );
  equal(
    cis.normalizeSite("BÜCHER.example"),
    "xn--bcher-kva.example",
    "normalizeSite encodes an IDN host"
  );
});

// Associating to an unknown or non-public container is rejected.
add_task(function unknownContainer() {
  let { cis } = freshService();

  Assert.throws(
    () => cis.setSiteAssociation("example.com", 99999),
    /unknown container/,
    "Unknown container is rejected"
  );
  equal(
    cis.getSiteAssociation("example.com"),
    0,
    "Nothing was stored for the rejected call"
  );

  let thumbnailId = cis.getPrivateIdentity(
    "userContextIdInternal.thumbnail"
  ).userContextId;
  for (let internalId of [MAX_USER_CONTEXT_ID, thumbnailId]) {
    Assert.throws(
      () => cis.setSiteAssociation("example.com", internalId),
      /unknown container/,
      `Internal container ${internalId} is rejected`
    );
    equal(
      cis.getSiteAssociation("example.com"),
      0,
      `Nothing was stored for internal container ${internalId}`
    );
  }

  for (let invalid of ["2", 2.5, NaN, null, undefined]) {
    Assert.throws(
      () => cis.setSiteAssociation("example.com", invalid),
      /Invalid container id/,
      `${invalid} is not a valid container id`
    );
    equal(
      cis.getSiteAssociation("example.com"),
      0,
      `Nothing was stored for container id ${invalid}`
    );
  }
});

// getSiteAssociations with and without a filter.
add_task(function query() {
  let { cis } = freshService();

  cis.setSiteAssociation("a.example", 2);
  cis.setSiteAssociation("b.example", 2);
  cis.setSiteAssociation("c.example", 3);

  equal(cis.getSiteAssociations().length, 3, "All associations returned");
  equal(
    cis.getSiteAssociations(2).length,
    2,
    "Filtered associations for container 2"
  );
  equal(
    cis.getSiteAssociations(3).length,
    1,
    "Filtered associations for container 3"
  );
});

// Removing a container purges its associations and notifies once per host.
add_task(function purgeOnContainerRemoval() {
  let { cis } = freshService();

  cis.setSiteAssociation("a.example", 2);
  cis.setSiteAssociation("b.example", 2);
  cis.setSiteAssociation("c.example", 3);

  let removed = recordNotifications(() => cis.remove(2));

  equal(cis.getSiteAssociation("a.example"), 0, "a.example purged");
  equal(cis.getSiteAssociation("b.example"), 0, "b.example purged");
  equal(cis.getSiteAssociation("c.example"), 3, "Other container untouched");

  equal(removed.length, 2, "One notification per purged host");
  Assert.deepEqual(
    removed.map(r => r.site).sort(),
    ["a.example", "b.example"],
    "Purged hosts were notified"
  );
  ok(
    removed.every(r => r.userContextId === null),
    "Purge notifications carry a null userContextId"
  );
});

// Associations survive a save/load round-trip via containers.json.
add_task(async function persistence() {
  let { cis, path } = freshService();

  cis.setSiteAssociation("example.com", 2);
  cis.setSiteAssociation("work.example", 2);
  await cis._saver.finalize();

  let reloaded = ContextualIdentityService.createNewInstanceForTesting(path);
  equal(reloaded.getSiteAssociation("example.com"), 2, "example.com reloaded");
  equal(
    reloaded.getSiteAssociation("work.example"),
    2,
    "work.example reloaded"
  );

  let raw = await IOUtils.readJSON(path);
  Assert.deepEqual(
    raw.siteAssociations,
    { "example.com": 2, "work.example": 2 },
    "siteAssociations stored as a top-level host->userContextId map"
  );
});

// A profile predating site associations loads with no associations.
add_task(async function backwardCompatibility() {
  let path = PathUtils.join(
    profileDir.path,
    "test-site-associations-legacy.json"
  );
  await IOUtils.writeJSON(path, {
    version: 5,
    lastUserContextId: 4,
    identities: [
      { userContextId: 2, public: true, icon: "briefcase", color: "orange" },
    ],
  });

  let cis = ContextualIdentityService.createNewInstanceForTesting(path);
  Assert.deepEqual(
    cis.getSiteAssociations(),
    [],
    "Legacy file without siteAssociations loads with no associations"
  );

  cis.setSiteAssociation("example.com", 2);
  equal(
    cis.getSiteAssociation("example.com"),
    2,
    "Associations can still be added to a legacy profile"
  );
});

// The nsISiteContainerService XPCOM facet is backed by the same singleton.
add_task(function nativeInterface() {
  let svc = Cc["@mozilla.org/site-container-service;1"].getService(
    Ci.nsISiteContainerService
  );

  ContextualIdentityService.setSiteAssociation("native.example", 2);

  equal(svc.lookup("native.example"), 2, "lookup reflects CIS state");
  equal(svc.lookup("NATIVE.EXAMPLE"), 2, "lookup normalizes the host");
  equal(svc.lookup("missing.example"), 0, "Unknown host returns 0");

  let uri = Services.io.newURI("https://native.example/some/path?q=1");
  equal(
    svc.containerForNavigation(uri, 7),
    2,
    "containerForNavigation returns the association"
  );

  let other = Services.io.newURI("https://missing.example/");
  equal(
    svc.containerForNavigation(other, 7),
    7,
    "containerForNavigation falls back to the baseline"
  );

  ContextualIdentityService.removeSiteAssociation("native.example");
  equal(svc.lookup("native.example"), 0, "Association removed via CIS is gone");
});
