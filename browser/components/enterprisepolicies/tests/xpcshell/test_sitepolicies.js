/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

function isJitDisabledForRemoteType(remoteType) {
  return (
    remoteType.endsWith("^disableJit=1") || remoteType.endsWith("&disableJit=1")
  );
}

function assertJitState(url, isAllowed) {
  let uri = Services.io.newURI(url);

  // Extract the site URI.
  let siteUri = Services.io.newURI(
    Services.scriptSecurityManager.createContentPrincipal(uri, {})
      .siteOriginNoSuffix
  );

  Assert.equal(
    Services.policies.isAllowedForURI("jit", siteUri),
    isAllowed,
    `Policy service should return the expected state for ${url} (site: ${siteUri})`
  );

  let remoteType = ChromeUtils.predictRemoteTypeForURI(uri, {
    useRemoteTabs: true,
    useRemoteSubframes: true,
    preferredRemoteType: "web",
  });

  Assert.equal(
    isJitDisabledForRemoteType(remoteType),
    !isAllowed,
    `Remote type should have the expected JIT state for ${url}`
  );
}

add_task(async function test_isAllowedForSite() {
  // Empty policies don't block anything
  await setupPolicyEngineWithJson({
    policies: {
      SitePolicies: [],
    },
  });

  assertJitState("http://example.net/", true);
  assertJitState("http://example.org/", true);
  assertJitState("http://example.com/", true);
  assertJitState("data:text/html,example", true);

  // Simple match case
  await setupPolicyEngineWithJson({
    policies: {
      SitePolicies: [
        {
          Match: ["*.example.com"],
          Policies: {
            DisableJit: true,
          },
        },
      ],
    },
  });

  assertJitState("http://example.net/", true);
  assertJitState("http://example.org/", true);
  assertJitState("http://example.com/", false);
  assertJitState("http://www.example.com/", false);
  assertJitState("http://test.example.com/", false);
  assertJitState("data:text/html,example", true);

  // Multiple match case
  await setupPolicyEngineWithJson({
    policies: {
      SitePolicies: [
        {
          Match: ["*.example.com", "*.example.org"],
          Policies: {
            DisableJit: true,
          },
        },
      ],
    },
  });

  assertJitState("http://example.net/", true);
  assertJitState("http://example.org/", false);
  assertJitState("http://example.com/", false);
  assertJitState("data:text/html,example", true);

  // Missing wildcard or being too specific still uses the base domain
  await setupPolicyEngineWithJson({
    policies: {
      SitePolicies: [
        {
          Match: ["example.com", "www.example.org"],
          Policies: {
            DisableJit: true,
          },
        },
      ],
    },
  });

  assertJitState("http://example.net/", true);
  assertJitState("http://example.org/", false);
  assertJitState("http://www.example.org/", false);
  assertJitState("http://test.example.org/", false);
  assertJitState("http://example.com/", false);
  assertJitState("http://www.example.com/", false);
  assertJitState("http://test.example.com/", false);
  assertJitState("data:text/html,example", true);

  // No match implies all sites
  await setupPolicyEngineWithJson({
    policies: {
      SitePolicies: [
        {
          Exceptions: ["*.example.com"],
          Policies: {
            DisableJit: true,
          },
        },
      ],
    },
  });

  assertJitState("http://example.net/", false);
  assertJitState("http://example.org/", false);
  assertJitState("http://example.com/", true);
  assertJitState("data:text/html,example", false);

  // Empty match implies all sites
  await setupPolicyEngineWithJson({
    policies: {
      SitePolicies: [
        {
          Match: [],
          Exceptions: ["*.example.com"],
          Policies: {
            DisableJit: true,
          },
        },
      ],
    },
  });

  assertJitState("http://example.net/", false);
  assertJitState("http://example.org/", false);
  assertJitState("http://example.com/", true);
  assertJitState("data:text/html,example", false);

  // Wildcard implies all sites
  await setupPolicyEngineWithJson({
    policies: {
      SitePolicies: [
        {
          Match: ["*"],
          Exceptions: ["*.example.com"],
          Policies: {
            DisableJit: true,
          },
        },
      ],
    },
  });

  assertJitState("http://example.net/", false);
  assertJitState("http://example.org/", false);
  assertJitState("http://example.com/", true);
  assertJitState("data:text/html,example", false);

  // Empty policies do nothing
  await setupPolicyEngineWithJson({
    policies: {
      SitePolicies: [
        {
          Match: ["*.example.com"],
          Policies: {},
        },
      ],
    },
  });

  assertJitState("http://example.net/", true);
  assertJitState("http://example.org/", true);
  assertJitState("http://example.com/", true);
  assertJitState("data:text/html,example", true);

  // Earlier policies take precedence over later ones.
  await setupPolicyEngineWithJson({
    policies: {
      SitePolicies: [
        {
          Match: ["*.example.com"],
          Policies: {
            DisableJit: false,
          },
        },
        {
          Match: ["*.example.com", "*.example.org"],
          Policies: {
            DisableJit: true,
          },
        },
      ],
    },
  });

  assertJitState("http://example.net/", true);
  assertJitState("http://example.org/", false);
  assertJitState("http://example.com/", true);
  assertJitState("data:text/html,example", true);

  // Earlier policies only take precedence if they include the relevant policy.
  await setupPolicyEngineWithJson({
    policies: {
      SitePolicies: [
        {
          Match: ["*.example.com"],
          Policies: {},
        },
        {
          Match: ["*.example.com", "*.example.org"],
          Policies: {
            DisableJit: true,
          },
        },
      ],
    },
  });

  assertJitState("http://example.net/", true);
  assertJitState("http://example.org/", false);
  assertJitState("http://example.com/", false);
  assertJitState("data:text/html,example", true);
});

add_task(async function test_trailing_dot_hostname() {
  await setupPolicyEngineWithJson({
    policies: {
      SitePolicies: [
        {
          Match: ["*.example.com"],
          Policies: {
            DisableJit: true,
          },
        },
      ],
    },
  });

  let bareUri = Services.io.newURI("https://www.example.com/page");
  let trailingDotUri = Services.io.newURI("https://www.example.com./page");

  Assert.equal(
    Services.policies.isAllowedForURI("jit", bareUri),
    false,
    "Bare hostname should match and disable JIT"
  );
  Assert.equal(
    Services.policies.isAllowedForURI("jit", trailingDotUri),
    false,
    "Trailing-dot hostname should match and disable JIT"
  );
});

add_task(async function test_trailing_dot_hostname_exceptions() {
  await setupPolicyEngineWithJson({
    policies: {
      SitePolicies: [
        {
          Match: ["*"],
          Exceptions: ["*.example.com"],
          Policies: {
            DisableJit: true,
          },
        },
      ],
    },
  });

  let trailingDotExceptionUri = Services.io.newURI(
    "https://www.example.com./page"
  );
  let trailingDotOtherUri = Services.io.newURI("https://www.example.net./page");

  Assert.equal(
    Services.policies.isAllowedForURI("jit", trailingDotExceptionUri),
    true,
    "Trailing-dot hostname in Exceptions should be unrestricted"
  );
  Assert.equal(
    Services.policies.isAllowedForURI("jit", trailingDotOtherUri),
    false,
    "Trailing-dot hostname not in Exceptions should be restricted"
  );
});
