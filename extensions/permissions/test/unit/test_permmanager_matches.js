/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

var attrs;

function matches_always(perm, principals) {
  principals.forEach(principal => {
    Assert.ok(
      perm.matches(principal, true),
      "perm: " + perm.principal.origin + ", princ: " + principal.origin
    );
    Assert.ok(
      perm.matches(principal, false),
      "perm: " + perm.principal.origin + ", princ: " + principal.origin
    );
  });
}

function matches_weak(perm, principals) {
  principals.forEach(principal => {
    Assert.ok(
      !perm.matches(principal, true),
      "perm: " + perm.principal.origin + ", princ: " + principal.origin
    );
    Assert.ok(
      perm.matches(principal, false),
      "perm: " + perm.principal.origin + ", princ: " + principal.origin
    );
  });
}

function matches_never(perm, principals) {
  principals.forEach(principal => {
    Assert.ok(
      !perm.matches(principal, true),
      "perm: " + perm.principal.origin + ", princ: " + principal.origin
    );
    Assert.ok(
      !perm.matches(principal, false),
      "perm: " + perm.principal.origin + ", princ: " + principal.origin
    );
  });
}

function run_matches_test(isolateByUserContext) {
  // initialize the permission manager service
  let pm = Services.perms;

  let secMan = Services.scriptSecurityManager;

  // Add some permissions
  let uri0 = NetUtil.newURI("http://google.com/search?q=foo#hashtag");
  let uri1 = NetUtil.newURI("http://hangouts.google.com/subdir");
  let uri2 = NetUtil.newURI("http://google.org/");
  let uri3 = NetUtil.newURI("https://google.com/some/random/subdirectory");
  let uri4 = NetUtil.newURI("https://hangouts.google.com/#!/hangout");
  let uri5 = NetUtil.newURI("http://google.com:8096/");

  let uri0_n = secMan.createContentPrincipal(uri0, {});
  let uri1_n = secMan.createContentPrincipal(uri1, {});
  let uri2_n = secMan.createContentPrincipal(uri2, {});
  let uri3_n = secMan.createContentPrincipal(uri3, {});
  let uri4_n = secMan.createContentPrincipal(uri4, {});
  let uri5_n = secMan.createContentPrincipal(uri5, {});

  attrs = { userContextId: 1 };
  let uri0_1 = secMan.createContentPrincipal(uri0, attrs);
  let uri1_1 = secMan.createContentPrincipal(uri1, attrs);
  let uri2_1 = secMan.createContentPrincipal(uri2, attrs);
  let uri3_1 = secMan.createContentPrincipal(uri3, attrs);
  let uri4_1 = secMan.createContentPrincipal(uri4, attrs);
  let uri5_1 = secMan.createContentPrincipal(uri5, attrs);

  attrs = { firstPartyDomain: "cnn.com" };
  let uri0_cnn = secMan.createContentPrincipal(uri0, attrs);
  let uri1_cnn = secMan.createContentPrincipal(uri1, attrs);
  let uri2_cnn = secMan.createContentPrincipal(uri2, attrs);
  let uri3_cnn = secMan.createContentPrincipal(uri3, attrs);
  let uri4_cnn = secMan.createContentPrincipal(uri4, attrs);
  let uri5_cnn = secMan.createContentPrincipal(uri5, attrs);

  pm.addFromPrincipal(uri0_n, "test/matches", pm.ALLOW_ACTION);
  let perm_n = pm.getPermissionObject(uri0_n, "test/matches", true);
  pm.addFromPrincipal(uri0_1, "test/matches", pm.ALLOW_ACTION);
  let perm_1 = pm.getPermissionObject(uri0_1, "test/matches", true);
  pm.addFromPrincipal(uri0_cnn, "test/matches", pm.ALLOW_ACTION);
  let perm_cnn = pm.getPermissionObject(uri0_cnn, "test/matches", true);

  let others_n = [uri2_n, uri3_n, uri4_n, uri5_n];
  let others_1 = [uri2_1, uri3_1, uri4_1, uri5_1];
  let others_cnn = [uri2_cnn, uri3_cnn, uri4_cnn, uri5_cnn];
  let all_n = [uri0_n, uri1_n, ...others_n];
  let all_1 = [uri0_1, uri1_1, ...others_1];
  let all_cnn = [uri0_cnn, uri1_cnn, ...others_cnn];

  if (isolateByUserContext) {
    matches_always(perm_n, [uri0_n]);
    matches_weak(perm_n, [uri1_n]);
    matches_never(perm_n, [...others_n, ...all_1, ...all_cnn]);

    matches_always(perm_1, [uri0_1]);
    matches_weak(perm_1, [uri1_1]);
    matches_never(perm_1, [...all_n, ...others_1, ...all_cnn]);
  } else {
    for (let perm of [perm_n, perm_1]) {
      matches_always(perm, [uri0_n, uri0_1]);
      matches_weak(perm, [uri1_n, uri1_1]);
      matches_never(perm, [...others_n, ...others_1, ...all_cnn]);
    }
  }

  matches_always(perm_cnn, [uri0_cnn]);
  matches_weak(perm_cnn, [uri1_cnn]);
  matches_never(perm_cnn, [...all_n, ...all_1, ...others_cnn]);

  // Clean up!
  pm.removeAll();
}

add_task(
  { pref_set: [["permissions.isolateBy.userContext", true]] },
  function test_matches_isolated_by_user_context() {
    run_matches_test(true);
  }
);

add_task(
  { pref_set: [["permissions.isolateBy.userContext", false]] },
  function test_matches_not_isolated_by_user_context() {
    run_matches_test(false);
  }
);
