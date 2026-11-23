/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

function assertHttpState(url, isAllowed) {
  let uri = Services.io.newURI(url);
  let siteUri = Services.io.newURI(
    Services.scriptSecurityManager.createContentPrincipal(uri, {})
      .siteOriginNoSuffix
  );

  Assert.equal(
    Services.policies.isAllowedForURI("http", siteUri),
    isAllowed,
    `Policy service should return the expected HTTP state for ${url} (site: ${siteUri})`
  );
}

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

add_setup(() => {
  do_get_profile();
});

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

add_task(async function test_httpsOnlyPolicy() {
  // Empty policies allow HTTP everywhere.
  await setupPolicyEngineWithJson({
    policies: {
      SitePolicies: [],
    },
  });

  assertHttpState("http://example.net/", true);
  assertHttpState("http://example.org/", true);
  assertHttpState("http://example.com/", true);

  // Simple match case: HttpsOnly blocks HTTP for matching sites.
  await setupPolicyEngineWithJson({
    policies: {
      SitePolicies: [
        {
          Match: ["*.example.com"],
          Policies: {
            HttpsOnly: true,
          },
        },
      ],
    },
  });

  assertHttpState("http://example.net/", true);
  assertHttpState("http://example.org/", true);
  assertHttpState("http://example.com/", false);
  assertHttpState("http://www.example.com/", false);
  assertHttpState("http://test.example.com/", false);

  // Multiple matches.
  await setupPolicyEngineWithJson({
    policies: {
      SitePolicies: [
        {
          Match: ["*.example.com", "*.example.org"],
          Policies: {
            HttpsOnly: true,
          },
        },
      ],
    },
  });

  assertHttpState("http://example.net/", true);
  assertHttpState("http://example.org/", false);
  assertHttpState("http://example.com/", false);

  // No match implies all sites; exceptions become the allowlist.
  await setupPolicyEngineWithJson({
    policies: {
      SitePolicies: [
        {
          Exceptions: ["*.example.com"],
          Policies: {
            HttpsOnly: true,
          },
        },
      ],
    },
  });

  assertHttpState("http://example.net/", false);
  assertHttpState("http://example.org/", false);
  assertHttpState("http://example.com/", true);

  // HttpsOnly: false explicitly allows HTTP.
  await setupPolicyEngineWithJson({
    policies: {
      SitePolicies: [
        {
          Match: ["*.example.com"],
          Policies: {
            HttpsOnly: false,
          },
        },
      ],
    },
  });

  assertHttpState("http://example.com/", true);

  // Earlier policies take precedence over later ones.
  await setupPolicyEngineWithJson({
    policies: {
      SitePolicies: [
        {
          Match: ["*.example.com"],
          Policies: {
            HttpsOnly: false,
          },
        },
        {
          Match: ["*.example.com", "*.example.org"],
          Policies: {
            HttpsOnly: true,
          },
        },
      ],
    },
  });

  assertHttpState("http://example.net/", true);
  assertHttpState("http://example.org/", false);
  assertHttpState("http://example.com/", true);

  // Both DisableJit and HttpsOnly can coexist in the same entry.
  await setupPolicyEngineWithJson({
    policies: {
      SitePolicies: [
        {
          Match: ["*.example.com"],
          Policies: {
            DisableJit: true,
            HttpsOnly: true,
          },
        },
      ],
    },
  });

  assertJitState("http://example.net/", true);
  assertJitState("http://example.com/", false);
  assertHttpState("http://example.net/", true);
  assertHttpState("http://example.com/", false);
});

function assertServiceWorkerState(url, isAllowed) {
  let uri = Services.io.newURI(url);
  let siteUri = Services.io.newURI(
    Services.scriptSecurityManager.createContentPrincipal(uri, {})
      .siteOriginNoSuffix
  );

  Assert.equal(
    Services.policies.isAllowedForURI("serviceworkers", siteUri),
    isAllowed,
    `Policy service should return the expected service worker state for ${url} (site: ${siteUri})`
  );
}

add_task(async function test_disableServiceWorkersPolicy() {
  // Empty policies allow service workers everywhere.
  await setupPolicyEngineWithJson({
    policies: {
      SitePolicies: [],
    },
  });

  assertServiceWorkerState("https://example.net/", true);
  assertServiceWorkerState("https://example.org/", true);
  assertServiceWorkerState("https://example.com/", true);

  // Simple match case.
  await setupPolicyEngineWithJson({
    policies: {
      SitePolicies: [
        {
          Match: ["*.example.com"],
          Policies: {
            DisableServiceWorkers: true,
          },
        },
      ],
    },
  });

  assertServiceWorkerState("https://example.net/", true);
  assertServiceWorkerState("https://example.org/", true);
  assertServiceWorkerState("https://example.com/", false);
  assertServiceWorkerState("https://sub.example.com/", false);

  // No match implies all sites, with Exceptions acting as an allowlist.
  await setupPolicyEngineWithJson({
    policies: {
      SitePolicies: [
        {
          Exceptions: ["*.example.com"],
          Policies: {
            DisableServiceWorkers: true,
          },
        },
      ],
    },
  });

  assertServiceWorkerState("https://example.net/", false);
  assertServiceWorkerState("https://example.com/", true);

  // DisableServiceWorkers: false explicitly allows.
  await setupPolicyEngineWithJson({
    policies: {
      SitePolicies: [
        {
          Policies: {
            DisableServiceWorkers: false,
          },
        },
      ],
    },
  });

  assertServiceWorkerState("https://example.net/", true);
  assertServiceWorkerState("https://example.com/", true);

  // DisableServiceWorkers coexists with DisableJit and HttpsOnly in same entry.
  await setupPolicyEngineWithJson({
    policies: {
      SitePolicies: [
        {
          Match: ["*.example.com"],
          Policies: {
            DisableJit: true,
            HttpsOnly: true,
            DisableServiceWorkers: true,
          },
        },
      ],
    },
  });

  assertServiceWorkerState("https://example.net/", true);
  assertServiceWorkerState("https://example.com/", false);
  assertJitState("https://example.net/", true);
  assertJitState("https://example.com/", false);
  assertHttpState("http://example.net/", true);
  assertHttpState("http://example.com/", false);
});

add_task(async function test_trailing_dot_hostname() {
  await setupPolicyEngineWithJson({
    policies: {
      SitePolicies: [
        {
          Match: ["*.example.com"],
          Policies: {
            DisableJit: true,
            HttpsOnly: true,
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

  Assert.equal(
    Services.policies.isAllowedForURI("http", bareUri),
    false,
    "Bare hostname should match and enforce HttpsOnly"
  );
  Assert.equal(
    Services.policies.isAllowedForURI("http", trailingDotUri),
    false,
    "Trailing-dot hostname should match and enforce HttpsOnly"
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

add_task(async function test_catch_all_skips_non_web_uris() {
  for (let policy of [{ Match: ["*"] }, { Exceptions: ["*.example.com"] }]) {
    await setupPolicyEngineWithJson({
      policies: {
        SitePolicies: [{ ...policy, Policies: { HttpsOnly: true } }],
      },
    });

    for (let url of [
      "about:blank",
      "about:robots",
      "chrome://browser/content/browser.xhtml",
      "file:///tmp/test.html",
      "moz-extension://12345678-1234-1234-1234-123456789abc/page.html",
    ]) {
      Assert.ok(
        Services.policies.isAllowedForURI("http", Services.io.newURI(url)),
        `Catch-all ${JSON.stringify(policy)} should not apply to ${url}`
      );
    }

    assertHttpState("http://example.net/", false);
    assertHttpState("data:text/html,example", false);
  }
});
