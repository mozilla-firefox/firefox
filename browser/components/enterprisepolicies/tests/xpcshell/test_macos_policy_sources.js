/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { macOSPoliciesParser } = ChromeUtils.importESModule(
  "resource://gre/modules/policies/macOSPoliciesParser.sys.mjs"
);

function read({ managed = {}, system = {}, user = {} }, options) {
  return macOSPoliciesParser.readPolicies(
    { readPolicySources: () => ({ managed, system, user }) },
    options
  );
}

add_task(function test_user_only() {
  deepEqual(
    read({ user: { EnterprisePoliciesEnabled: true, DisableFormHistory: 1 } }),
    { policies: {}, userPolicies: { DisableFormHistory: 1 } },
    "User policies are returned separately"
  );
});

add_task(function test_disabled_returns_null() {
  equal(
    read({ user: { DisableFormHistory: 1 } }),
    null,
    "Policies are ignored without EnterprisePoliciesEnabled"
  );
  equal(
    read({ user: { EnterprisePoliciesEnabled: false, DisableFormHistory: 1 } }),
    null,
    "Policies are ignored when EnterprisePoliciesEnabled is false"
  );
});

add_task(function test_sources_kept_apart() {
  deepEqual(
    read({
      managed: { EnterprisePoliciesEnabled: true, DontCheckDefaultBrowser: 1 },
      system: { DisableFormHistory: 1 },
      user: { DontCheckDefaultBrowser: 0, DisablePocket: 1 },
    }),
    {
      policies: { DontCheckDefaultBrowser: 1, DisableFormHistory: 1 },
      userPolicies: { DontCheckDefaultBrowser: 0, DisablePocket: 1 },
    },
    "Managed and system policies are combined, user policies kept apart"
  );
});

add_task(function test_user_cannot_disable_privileged_policies() {
  equal(
    read({
      system: { EnterprisePoliciesEnabled: false, DontCheckDefaultBrowser: 1 },
      user: { EnterprisePoliciesEnabled: true, DisableFormHistory: 1 },
    }),
    null,
    "A privileged EnterprisePoliciesEnabled value wins over the user's"
  );
  deepEqual(
    read({
      managed: { EnterprisePoliciesEnabled: true, DontCheckDefaultBrowser: 1 },
      user: { EnterprisePoliciesEnabled: false },
    }),
    { policies: { DontCheckDefaultBrowser: 1 }, userPolicies: {} },
    "The user can't turn off privileged policies"
  );
});

add_task(function test_managed_beats_system() {
  deepEqual(
    read({
      managed: { EnterprisePoliciesEnabled: true, DontCheckDefaultBrowser: 1 },
      system: {
        EnterprisePoliciesEnabled: false,
        DontCheckDefaultBrowser: 0,
        DisableFormHistory: 1,
      },
    }).policies,
    { DontCheckDefaultBrowser: 1, DisableFormHistory: 1 },
    "Managed values win over system values on collision"
  );
});

add_task(function test_user_disable_user_policies_dropped() {
  deepEqual(
    read({
      user: {
        EnterprisePoliciesEnabled: true,
        DisableMacOSUserPolicies: 1,
        DisableFormHistory: 1,
      },
    }).userPolicies,
    { DisableFormHistory: 1 },
    "DisableMacOSUserPolicies is never taken from the user"
  );
});

add_task(function test_flattened_keys_unflatten() {
  deepEqual(
    read({
      managed: {
        EnterprisePoliciesEnabled: true,
        Homepage__URL: "https://www.example.com",
        Homepage__Locked: 1,
      },
      system: { Homepage__Additional__0: "https://www.example.org" },
      user: { UserMessaging__SkipOnboarding: 1 },
    }),
    {
      policies: { Homepage: { URL: "https://www.example.com", Locked: 1 } },
      userPolicies: { UserMessaging: { SkipOnboarding: 1 } },
    },
    "Flattened keys unflatten and managed replaces the whole system policy"
  );
});

add_task(function test_unparseable_source_skipped() {
  // A flat key and a flattened key for the same policy make unflatten() throw.
  deepEqual(
    read({
      system: { EnterprisePoliciesEnabled: true, DontCheckDefaultBrowser: 1 },
      user: { Homepage: 1, Homepage__URL: "https://www.example.com" },
    }),
    { policies: { DontCheckDefaultBrowser: 1 }, userPolicies: {} },
    "A user source that can't be parsed is skipped"
  );
});

add_task(function test_disallowed_user_only() {
  equal(
    read(
      { user: { EnterprisePoliciesEnabled: true, DisableFormHistory: 1 } },
      { allowUserPolicies: false }
    ),
    null,
    "User policies are never used when user policies aren't allowed"
  );
});

add_task(function test_disallowed_system_and_user() {
  deepEqual(
    read(
      {
        system: { EnterprisePoliciesEnabled: true, DontCheckDefaultBrowser: 1 },
        user: { DontCheckDefaultBrowser: 0, DisableFormHistory: 1 },
      },
      { allowUserPolicies: false }
    ),
    { policies: { DontCheckDefaultBrowser: 1 }, userPolicies: null },
    "Only system policies apply when user policies aren't allowed"
  );
  equal(
    read(
      {
        system: { DontCheckDefaultBrowser: 1 },
        user: { EnterprisePoliciesEnabled: true },
      },
      { allowUserPolicies: false }
    ),
    null,
    "EnterprisePoliciesEnabled from the user is ignored when user policies aren't allowed"
  );
});

add_task(function test_explicitly_allowed_user_only() {
  deepEqual(
    read(
      { user: { EnterprisePoliciesEnabled: true, DisableFormHistory: 1 } },
      { allowUserPolicies: true }
    ),
    { policies: {}, userPolicies: { DisableFormHistory: 1 } },
    "User policies are returned when allowed"
  );
});
