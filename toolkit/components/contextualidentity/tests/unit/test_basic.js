"use strict";

const profileDir = do_get_profile();

const { ContextualIdentityService } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/contextualidentity/ContextualIdentityService.sys.mjs"
);

const TEST_STORE_FILE_PATH = PathUtils.join(
  profileDir.path,
  "test-containers.json"
);

let cis;

// Basic tests
add_task(function () {
  ok(!!ContextualIdentityService, "ContextualIdentityService exists");

  cis =
    ContextualIdentityService.createNewInstanceForTesting(TEST_STORE_FILE_PATH);
  ok(!!cis, "We have our instance of ContextualIdentityService");

  equal(cis.getPublicIdentities().length, 4, "By default, 4 containers.");
  equal(cis.getPublicIdentityFromId(0), null, "No identity with id 0");

  ok(!!cis.getPublicIdentityFromId(1), "Identity 1 exists");
  ok(!!cis.getPublicIdentityFromId(2), "Identity 2 exists");
  ok(!!cis.getPublicIdentityFromId(3), "Identity 3 exists");
  ok(!!cis.getPublicIdentityFromId(4), "Identity 4 exists");

  Assert.deepEqual(
    cis.getPublicUserContextIds(),
    cis.getPublicIdentities().map(ident => ident.userContextId),
    "getPublicUserContextIds has matching user context IDs"
  );
});

// Make sure we are not allowed to only use whitespaces as a container name
add_task(function () {
  Assert.throws(
    () =>
      cis.create(
        "\u0009\u000B\u000C\u0020\u00A0\uFEFF\u000A\u000D\u2028\u2029",
        "icon",
        "color"
      ),
    /Contextual identity names cannot contain only whitespace./,
    "Contextual identity names cannot contain only whitespace."
  );
});

add_task(function () {
  ok(!!cis.getPublicIdentityFromId(2), "Identity 2 exists");
  Assert.throws(
    () =>
      cis.update(
        2,
        "\u0009\u000B\u000C\u0020\u00A0\uFEFF\u000A\u000D\u2028\u2029",
        "icon",
        "color"
      ),
    /Contextual identity names cannot contain only whitespace./,
    "Contextual identity names cannot contain only whitespace."
  );
});

// Create a new identity
add_task(function () {
  equal(cis.getPublicIdentities().length, 4, "By default, 4 containers.");

  let identity = cis.create("New Container", "Icon", "Color");
  ok(!!identity, "New container created");
  equal(identity.name, "New Container", "Name matches");
  equal(identity.icon, "Icon", "Icon matches");
  equal(identity.color, "Color", "Color matches");

  equal(cis.getPublicIdentities().length, 5, "Expected 5 containers.");

  ok(!!cis.getPublicIdentityFromId(identity.userContextId), "Identity exists");
  equal(
    cis.getPublicIdentityFromId(identity.userContextId).name,
    "New Container",
    "Identity name is OK"
  );
  equal(
    cis.getPublicIdentityFromId(identity.userContextId).icon,
    "Icon",
    "Identity icon is OK"
  );
  equal(
    cis.getPublicIdentityFromId(identity.userContextId).color,
    "Color",
    "Identity color is OK"
  );
  equal(
    cis.getUserContextLabel(identity.userContextId),
    "New Container",
    "Identity label is OK"
  );

  Assert.deepEqual(
    cis.getPublicUserContextIds(),
    cis.getPublicIdentities().map(ident => ident.userContextId),
    "getPublicUserContextIds has matching user context IDs"
  );

  // Remove an identity
  equal(
    cis.remove(-1),
    false,
    "cis.remove() returns false if identity doesn't exist."
  );
  equal(cis.remove(1), true, "cis.remove() returns true if identity exists.");

  equal(cis.getPublicIdentities().length, 4, "Expected 4 containers.");
  Assert.deepEqual(
    cis.getPublicUserContextIds(),
    cis.getPublicIdentities().map(ident => ident.userContextId),
    "getPublicUserContextIds has matching user context IDs"
  );
});

// Reorder identities
add_task(function () {
  equal(cis.getPublicIdentities().length, 4, "By default, 4 containers.");
  // Get whatever the initial order is
  const [id0, id1, id2, id3] = cis.getPublicUserContextIds();

  ok(cis.move([id3], 0), "Moving one valid id");
  Assert.deepEqual(
    cis.getPublicUserContextIds(),
    [id3, id0, id1, id2],
    "Moving one valid ID works"
  );

  ok(cis.move([id1, id2], 1), "Moving several valid ids");
  Assert.deepEqual(
    cis.getPublicUserContextIds(),
    [id3, id1, id2, id0],
    "Moving several valid IDs works"
  );

  ok(!cis.move([100], 0), "Moving non-existing ids");
  Assert.deepEqual(
    cis.getPublicUserContextIds(),
    [id3, id1, id2, id0],
    "Moving only non-existing IDs leaves list unchanged"
  );

  ok(cis.move([100, id1], 1), "Moving non-existing and existing ids");
  Assert.deepEqual(
    cis.getPublicUserContextIds(),
    [id3, id1, id2, id0],
    "Moving existing and non-existing IDs ignores non-existing ones"
  );

  ok(cis.move([id1, 100], 1), "Moving existing and non-existing ids");
  Assert.deepEqual(
    cis.getPublicUserContextIds(),
    [id3, id1, id2, id0],
    "Moving existing and non-existing IDs ignores non-existing ones"
  );

  ok(cis.move([id2], -1), "Moving to -1");
  Assert.deepEqual(
    cis.getPublicUserContextIds(),
    [id3, id1, id0, id2],
    "Moving to -1 works"
  );

  ok(!cis.move([id3], -10), "Moving to other negative positions");
  Assert.deepEqual(
    cis.getPublicUserContextIds(),
    [id3, id1, id0, id2],
    "Moving to other negative positions leaves list unchanged"
  );

  ok(cis.move([id3], 3), "Moving to last public position");
  Assert.deepEqual(
    cis.getPublicUserContextIds(),
    [id1, id0, id2, id3],
    "Moving to last position correctly skips private context ids"
  );

  ok(cis.move([id1, id2], 1), "Moving past current position");
  Assert.deepEqual(
    cis.getPublicUserContextIds(),
    [id0, id1, id2, id3],
    "Target position is index in resulting list"
  );

  ok(cis.move([id2, id2], 2), "Moving duplicate ids");
  Assert.deepEqual(
    cis.getPublicUserContextIds(),
    [id0, id1, id2, id3],
    "Resulting list does not contain duplicate ids"
  );

  ok(!cis.move([], 2), "Moving empty list");
  Assert.deepEqual(
    cis.getPublicUserContextIds(),
    [id0, id1, id2, id3],
    "Resulting list does not contain duplicate ids"
  );
});

// Reordering identities notifies observers
add_task(function () {
  const [id0, id1] = cis.getPublicUserContextIds();

  let notified = 0;
  let observer = () => notified++;
  Services.obs.addObserver(observer, "contextual-identity-reordered");

  ok(cis.move([id1], 0), "Moving a valid id");
  equal(notified, 1, "A move that reorders identities notifies observers");

  ok(!cis.move([100], 0), "Moving a non-existing id");
  equal(notified, 1, "A move that changes nothing does not notify observers");

  ok(!cis.move([id0], -10), "Moving to an invalid position");
  equal(notified, 1, "A rejected move does not notify observers");

  ok(cis.move([id0], 0), "Restoring the initial order");
  equal(notified, 2, "The restoring move notifies observers");

  Services.obs.removeObserver(observer, "contextual-identity-reordered");
});

// Update an identity
add_task(function () {
  ok(!!cis.getPublicIdentityFromId(2), "Identity 2 exists");

  equal(
    cis.update(-1, "Container", "Icon", "Color"),
    false,
    "Update returns false if the identity doesn't exist"
  );

  equal(
    cis.update(2, "Container", "Icon", "Color"),
    true,
    "Update returns true if everything is OK"
  );

  ok(!!cis.getPublicIdentityFromId(2), "Identity exists");
  equal(
    cis.getPublicIdentityFromId(2).name,
    "Container",
    "Identity name is OK"
  );
  equal(cis.getPublicIdentityFromId(2).icon, "Icon", "Identity icon is OK");
  equal(cis.getPublicIdentityFromId(2).color, "Color", "Identity color is OK");
  equal(cis.getUserContextLabel(2), "Container", "Identity label is OK");
});
