/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { CombinedProvider } = ChromeUtils.importESModule(
  "resource://gre/modules/EnterprisePoliciesParent.sys.mjs"
);

// A minimal stand-in for a real policies provider. CombinedProvider only
// reads the `policies` and `failed` members of the providers it is given.
function fakeProvider(policies, failed = false) {
  return { policies, failed };
}

function combine(...providers) {
  let combined = new CombinedProvider();
  for (let provider of providers) {
    combined.push(provider);
  }
  combined.mergePolicies();
  return combined;
}

add_task(async function test_single_provider() {
  let combined = combine(fakeProvider({ PolicyA: 1 }));

  deepEqual(
    combined.policies,
    { PolicyA: 1 },
    "A single provider's policies pass through unchanged"
  );
  ok(combined.hasPolicies, "Combined provider has policies");
});

add_task(async function test_disjoint_providers_are_unioned() {
  let combined = combine(
    fakeProvider({ PolicyA: 1 }),
    fakeProvider({ PolicyB: 2 })
  );

  deepEqual(
    combined.policies,
    { PolicyA: 1, PolicyB: 2 },
    "Non-overlapping policies from every provider are merged together"
  );
});

add_task(async function test_later_provider_wins_on_conflict() {
  // Providers are pushed in increasing order of precedence, so the one added
  // last (the platform provider) wins on conflicting keys.
  let combined = combine(
    fakeProvider({ Shared: "json", OnlyJson: true }),
    fakeProvider({ Shared: "platform", OnlyPlatform: true })
  );

  deepEqual(
    combined.policies,
    { Shared: "platform", OnlyJson: true, OnlyPlatform: true },
    "The higher-precedence provider overrides conflicting top-level policies"
  );
});

add_task(async function test_precedence_chain_across_three_providers() {
  let combined = combine(
    fakeProvider({ Shared: "lowest" }),
    fakeProvider({ Shared: "middle" }),
    fakeProvider({ Shared: "highest" })
  );

  equal(
    combined.policies.Shared,
    "highest",
    "The last-pushed provider wins across a chain of providers"
  );
});

add_task(async function test_merge_is_top_level_only() {
  // A conflicting top-level policy is replaced wholesale; the objects are not
  // deep-merged, so sub-keys from the lower-precedence provider do not survive.
  let combined = combine(
    fakeProvider({ Nested: { fromJson: true, shared: "json" } }),
    fakeProvider({ Nested: { shared: "platform" } })
  );

  deepEqual(
    combined.policies.Nested,
    { shared: "platform" },
    "Merging happens only at the top level; nested keys are not combined"
  );
});

add_task(async function test_no_failure_when_no_provider_failed() {
  ok(
    !combine(fakeProvider({}), fakeProvider({})).failed,
    "Combined provider does not fail when no provider failed"
  );
});

add_task(async function test_failed_provider_with_no_policies_fails() {
  // A provider failed and the merge produced no policies, so there is nothing
  // to fall back on: the engine must surface the failure (e.g. a broken
  // policies.json with no platform policies).
  let combined = combine(fakeProvider(null, true), fakeProvider({}));

  ok(!combined.hasPolicies, "No policies survived the merge");
  ok(
    combined.failed,
    "A failed provider fails the engine when left policyless"
  );
});

add_task(async function test_failed_provider_masked_by_policies() {
  // One provider failed, but a higher-precedence provider supplied policies.
  // The failure is ignored so the surviving policies still apply (e.g. a
  // broken policies.json alongside valid platform/GPO policies).
  let combined = combine(
    fakeProvider(null, true),
    fakeProvider({ PlatformPolicy: true })
  );

  ok(combined.hasPolicies, "The non-failed provider's policies survive");
  ok(
    !combined.failed,
    "A failed provider does not fail the engine when policies are present"
  );
});

add_task(async function test_empty_providers_have_no_policies() {
  let combined = combine(fakeProvider({}), fakeProvider({}));

  ok(
    !combined.hasPolicies,
    "Combining only empty providers yields no effective policies"
  );
});
