"use strict";

// This test file verifies that the declarativeNetRequest API can modify
// network requests as expected without the presence of the webRequest API. See
// test_ext_dnr_webRequest.js for the interaction between webRequest and DNR.

add_setup(() => {
  Services.prefs.setBoolPref("extensions.dnr.enabled", true);
});

const server = createHttpServer({
  hosts: ["example.com", "example.net", "example.org", "redir", "dummy"],
});
server.registerPathHandler("/cors_202", (req, res) => {
  res.setStatusLine(req.httpVersion, 202, "Accepted");
  // The extensions in this test have minimal permissions, so grant CORS to
  // allow them to read the response without host permissions.
  res.setHeader("Access-Control-Allow-Origin", "*");
  res.setHeader("Access-Control-Max-Age", "0");
  res.write("cors_response");
});
server.registerPathHandler("/never_reached", (req, res) => {
  Assert.ok(false, "Server should never have been reached");
  res.setHeader("Access-Control-Allow-Origin", "*");
  res.setHeader("Access-Control-Max-Age", "0");
});
let gPreflightCount = 0;
server.registerPathHandler("/preflight_count", (req, res) => {
  res.setHeader("Access-Control-Allow-Origin", "*");
  res.setHeader("Access-Control-Max-Age", "0");
  res.setHeader("Access-Control-Allow-Methods", "NONSIMPLE");
  if (req.method === "OPTIONS") {
    ++gPreflightCount;
  } else {
    // CORS Preflight considers 2xx to be successful. To rule out inadvertent
    // server opt-in to CORS, respond with a non-2xx response.
    res.setStatusLine(req.httpVersion, 418, "I'm a teapot");
    res.write(`count=${gPreflightCount}`);
  }
});
server.registerPathHandler("/", (req, res) => {
  res.setHeader("Access-Control-Allow-Origin", "*");
  res.setHeader("Access-Control-Max-Age", "0");
  res.write("Dummy page");
});

async function contentFetch(initiatorURL, url, options) {
  let contentPage = await ExtensionTestUtils.loadContentPage(initiatorURL);
  // Sanity check: that the initiator is as specified, and not redirected.
  Assert.equal(
    await contentPage.spawn([], () => content.document.URL),
    initiatorURL,
    `Expected document load at: ${initiatorURL}`
  );
  let result = await contentPage.spawn([{ url, options }], async args => {
    try {
      let req = await content.fetch(args.url, args.options);
      return {
        status: req.status,
        url: req.url,
        body: await req.text(),
      };
    } catch (e) {
      return { error: e.message };
    }
  });
  await contentPage.close();
  return result;
}

async function checkCanFetchFromOtherExtension() {
  let otherExtension = ExtensionTestUtils.loadExtension({
    async background() {
      let req = await fetch("http://example.com/cors_202", { method: "get" });
      browser.test.assertEq(202, req.status, "not blocked by other extension");
      browser.test.assertEq("cors_response", await req.text());
      browser.test.sendMessage("other_extension_done");
    },
  });
  await otherExtension.startup();
  await otherExtension.awaitMessage("other_extension_done");
  await otherExtension.unload();
}

add_task(async function block_request_with_dnr() {
  async function background() {
    await browser.declarativeNetRequest.updateSessionRules({
      addRules: [
        {
          id: 1,
          condition: { requestMethods: ["get"] },
          action: { type: "block" },
        },
        {
          id: 2,
          condition: { requestMethods: ["head"] },
          action: { type: "allow" },
        },
      ],
    });
    {
      // Request not matching DNR.
      let req = await fetch("http://example.com/cors_202", { method: "post" });
      browser.test.assertEq(202, req.status, "allowed without DNR rule");
      browser.test.assertEq("cors_response", await req.text());
    }
    {
      // Request with "allow" DNR action.
      let req = await fetch("http://example.com/cors_202", { method: "head" });
      browser.test.assertEq(202, req.status, "allowed by DNR rule");
      browser.test.assertEq("", await req.text(), "no response for HEAD");
    }

    // Request with "block" DNR action.
    await browser.test.assertRejects(
      fetch("http://example.com/never_reached", { method: "get" }),
      "NetworkError when attempting to fetch resource.",
      "blocked by DNR rule"
    );

    browser.test.sendMessage("tested_dnr_block");
  }
  let extension = ExtensionTestUtils.loadExtension({
    allowInsecureRequests: true,
    background,
    manifest: {
      manifest_version: 3,
      permissions: ["declarativeNetRequest"],
    },
  });
  await extension.startup();
  await extension.awaitMessage("tested_dnr_block");

  // DNR should not only work with requests within the extension, but also from
  // web pages.
  Assert.deepEqual(
    await contentFetch("http://dummy/", "http://example.com/never_reached"),
    { error: "NetworkError when attempting to fetch resource." },
    "Blocked by DNR with declarativeNetRequestWithHostAccess"
  );

  // declarativeNetRequest does not allow extensions to block requests from
  // other extensions.
  await checkCanFetchFromOtherExtension();

  await extension.unload();
});

// Verifies that the "declarativeNetRequestWithHostAccess" permission can only
// block if it has permission for the initiator.
add_task(async function block_with_declarativeNetRequestWithHostAccess() {
  let extension = ExtensionTestUtils.loadExtension({
    async background() {
      await browser.declarativeNetRequest.updateSessionRules({
        addRules: [{ id: 1, condition: {}, action: { type: "block" } }],
      });
      browser.test.sendMessage("dnr_registered");
    },
    temporarilyInstalled: true, // Needed for granted_host_permissions
    allowInsecureRequests: true,
    manifest: {
      manifest_version: 3,
      granted_host_permissions: true,
      host_permissions: ["<all_urls>"],
      permissions: ["declarativeNetRequestWithHostAccess"],
    },
  });
  await extension.startup();
  await extension.awaitMessage("dnr_registered");

  // Initiator "http://dummy" does match "<all_urls>", so DNR rule should apply.
  Assert.deepEqual(
    await contentFetch("http://dummy/", "http://example.com/never_reached"),
    { error: "NetworkError when attempting to fetch resource." },
    "Blocked by DNR with declarativeNetRequestWithHostAccess"
  );

  // Extensions cannot have permissions for another extension and therefore the
  // DNR rule never applies.
  await checkCanFetchFromOtherExtension();

  await extension.unload();
});

add_task(async function block_in_sandboxed_extension_page() {
  const filesWithSandbox = {
    "page_with_sandbox.html": `<!DOCTYPE html><meta charset="utf-8">
        <script src="page_with_sandbox.js"></script>
        <iframe src="sandbox.html" sandbox="allow-scripts"></iframe>
      `,
    "page_with_sandbox.js": () => {
      // Sent by sandbox.js:
      window.onmessage = e => {
        browser.test.assertEq("null", e.origin, "Sender has opaque origin");
        browser.test.sendMessage("fetch_result", e.data);
      };
    },
    "sandbox.html": `<script src="sandbox.js"></script>`,
    "sandbox.js": async () => {
      try {
        // Note that the test server responds with CORS headers, so we should
        // be able to fetch this URL:
        await fetch("http://example.com/?fetch_by_sandbox");
        parent.postMessage("FETCH_ALLOWED", "*");
      } catch (e) {
        // The only way for this to fail in this test is when DNR blocks it.
        parent.postMessage("FETCH_BLOCKED", "*");
      }
    },
  };
  async function checkFetchInSandboxedExtensionPage(ext) {
    let contentPage = await ExtensionTestUtils.loadContentPage(
      `moz-extension://${ext.uuid}/page_with_sandbox.html`
    );
    let result = await ext.awaitMessage("fetch_result");
    await contentPage.close();
    return result;
  }

  let extension = ExtensionTestUtils.loadExtension({
    async background() {
      await browser.declarativeNetRequest.updateSessionRules({
        addRules: [{ id: 1, condition: {}, action: { type: "block" } }],
      });
      browser.test.sendMessage("dnr_registered");
    },
    allowInsecureRequests: true,
    manifest: {
      manifest_version: 3,
      permissions: ["declarativeNetRequest"],
    },
    files: filesWithSandbox,
  });
  await extension.startup();
  await extension.awaitMessage("dnr_registered");

  Assert.equal(
    await checkFetchInSandboxedExtensionPage(extension),
    "FETCH_BLOCKED",
    "DNR blocks request from sandboxed page in own extension"
  );

  let otherExtension = ExtensionTestUtils.loadExtension({
    files: filesWithSandbox,
  });
  await otherExtension.startup();

  // Note: In Firefox, webRequest can intercept requests from opaque origins
  // opened by other extensions. In contrast, that is not the case in Chrome.
  Assert.equal(
    await checkFetchInSandboxedExtensionPage(otherExtension),
    "FETCH_BLOCKED",
    "DNR can block request from sandboxed page in other extension"
  );

  await extension.unload();

  // As a sanity check, to verify that the tests above do not always return
  // FETCH_BLOCKED, run a test case that returns FETCH_ALLOWED:
  Assert.equal(
    await checkFetchInSandboxedExtensionPage(otherExtension),
    "FETCH_ALLOWED",
    "DNR does not affect sandboxed extensions after unloading the DNR extension"
  );

  await otherExtension.unload();
});

// Verifies that upgradeScheme works.
// The HttpServer helper does not support https (bug 1742061), so in this
// test we just verify whether the upgrade has been attempted. Coverage that
// verifies that the upgraded request completes is in:
// toolkit/components/extensions/test/mochitest/test_ext_dnr_upgradeScheme.html
add_task(async function upgradeScheme_declarativeNetRequestWithHostAccess() {
  let extension = ExtensionTestUtils.loadExtension({
    async background() {
      await browser.declarativeNetRequest.updateSessionRules({
        addRules: [
          {
            id: 1,
            condition: { excludedRequestDomains: ["dummy"] },
            action: { type: "upgradeScheme" },
          },
          {
            id: 2,
            // HttpServer does not support https (bug 1742061).
            // As a work-around, we just redirect the https:-request to http.
            condition: { urlFilter: "|https:*" },
            action: {
              type: "redirect",
              redirect: { url: "http://dummy/cors_202?from_https" },
            },
            // The upgradeScheme and redirect actions have equal precedence. To
            // make sure that the redirect action is executed when both rules
            // match, we assign a higher priority to the redirect action.
            priority: 2,
          },
        ],
      });

      let req = await fetch("http://redir/never_reached");
      browser.test.assertEq(
        "http://dummy/cors_202?from_https",
        req.url,
        "upgradeScheme upgraded to https"
      );
      browser.test.assertEq("cors_response", await req.text());

      browser.test.sendMessage("tested_dnr_upgradeScheme");
    },
    temporarilyInstalled: true, // Needed for granted_host_permissions.
    allowInsecureRequests: true,
    manifest: {
      manifest_version: 3,
      granted_host_permissions: true,
      host_permissions: ["*://dummy/*", "*://redir/*"],
      permissions: ["declarativeNetRequestWithHostAccess"],
    },
  });
  await extension.startup();
  await extension.awaitMessage("tested_dnr_upgradeScheme");

  // Request to same-origin subresource, which should be upgraded.
  Assert.equal(
    (await contentFetch("http://redir/", "http://redir/never_reached")).url,
    "http://dummy/cors_202?from_https",
    "upgradeScheme + host access should upgrade (same-origin request)"
  );

  // Request to cross-origin subresource, which should be upgraded.
  // Note: after the upgrade, a cross-origin redirect happens. Internally, we
  // reflect the Origin request header in the Access-Control-Allow-Origin (ACAO)
  // response header, to ensure that the request is accepted by CORS. See
  // https://github.com/w3c/webappsec-upgrade-insecure-requests/issues/32
  Assert.equal(
    (await contentFetch("http://dummy/", "http://redir/never_reached")).url,
    "http://dummy/cors_202?from_https",
    "upgradeScheme + host access should upgrade (cross-origin request)"
  );

  // The DNR extension does not have example.net in host_permissions.
  const urlNoHostPerms = "http://example.net/cors_202?missing_host_permission";
  Assert.equal(
    (await contentFetch("http://dummy/", urlNoHostPerms)).url,
    urlNoHostPerms,
    "upgradeScheme not matched when extension lacks host access"
  );

  await extension.unload();
});

add_task(async function redirect_request_with_dnr() {
  async function background() {
    await browser.declarativeNetRequest.updateSessionRules({
      addRules: [
        {
          id: 1,
          condition: {
            requestDomains: ["example.com"],
            requestMethods: ["get"],
          },
          action: {
            type: "redirect",
            redirect: {
              url: "http://example.net/cors_202?1",
            },
          },
        },
        {
          id: 2,
          // Note: extension does not have example.org host permission.
          condition: { requestDomains: ["example.org"] },
          action: {
            type: "redirect",
            redirect: {
              url: "http://example.net/cors_202?2",
            },
          },
        },
      ],
    });
    // The extension only has example.com permission, but the redirects to
    // example.net are still due to the CORS headers from the server.
    {
      // Simple GET request.
      let req = await fetch("http://example.com/never_reached");
      browser.test.assertEq(202, req.status, "redirected by DNR (simple)");
      browser.test.assertEq("http://example.net/cors_202?1", req.url);
      browser.test.assertEq("cors_response", await req.text());
    }
    {
      // GeT request should be matched despite having a different case.
      let req = await fetch("http://example.com/never_reached", {
        method: "GeT",
      });
      browser.test.assertEq(202, req.status, "redirected by DNR (GeT)");
      browser.test.assertEq("http://example.net/cors_202?1", req.url);
      browser.test.assertEq("cors_response", await req.text());
    }
    {
      // Host permission missing for request, request not redirected by DNR.
      // Response is readable due to the CORS response headers from the server.
      let req = await fetch("http://example.org/cors_202?noredir");
      browser.test.assertEq(202, req.status, "not redirected by DNR");
      browser.test.assertEq("http://example.org/cors_202?noredir", req.url);
      browser.test.assertEq("cors_response", await req.text());
    }

    browser.test.notifyPass();
  }
  let extension = ExtensionTestUtils.loadExtension({
    background,
    temporarilyInstalled: true, // Needed for granted_host_permissions
    allowInsecureRequests: true,
    manifest: {
      manifest_version: 3,
      granted_host_permissions: true,
      host_permissions: ["*://example.com/*"],
      permissions: ["declarativeNetRequest"],
    },
  });
  await extension.startup();
  await extension.awaitFinish();

  let otherExtension = ExtensionTestUtils.loadExtension({
    async background() {
      // The DNR extension has permissions for example.com, but not for this
      // extension. Therefore the "redirect" action should not apply.
      let req = await fetch("http://example.com/cors_202?other_ext");
      browser.test.assertEq(202, req.status, "not redirected by DNR");
      browser.test.assertEq("http://example.com/cors_202?other_ext", req.url);
      browser.test.assertEq("cors_response", await req.text());
      browser.test.sendMessage("other_extension_done");
    },
  });
  await otherExtension.startup();
  await otherExtension.awaitMessage("other_extension_done");
  await otherExtension.unload();

  await extension.unload();
});

// Verifies that DNR redirects requiring a CORS preflight behave as expected.
add_task(async function redirect_request_with_dnr_cors_preflight() {
  // Most other test tasks only test requests within the test extension. This
  // test intentionally triggers requests outside the extension, to make sure
  // that the usual CORS mechanisms is triggered (instead of exceptions from
  // host permissions).
  async function background() {
    await browser.declarativeNetRequest.updateSessionRules({
      addRules: [
        {
          id: 1,
          condition: {
            requestDomains: ["redir"],
            excludedRequestMethods: ["options"],
          },
          action: {
            type: "redirect",
            redirect: {
              url: "http://example.com/preflight_count",
            },
          },
        },
        {
          id: 2,
          condition: {
            requestDomains: ["example.net"],
            excludedRequestMethods: ["nonsimple"], // note: redirects "options"
          },
          action: {
            type: "redirect",
            redirect: {
              url: "http://example.com/preflight_count",
            },
          },
        },
      ],
    });
    let req = await fetch("http://redir/never_reached", {
      method: "NONSIMPLE",
    });
    // Extension has permission for "redir", but not for the redirect target.
    // The request is non-simple (see below for explanation of non-simple), so
    // a preflight (OPTIONS) request to /preflight_count is expected before the
    // redirection target is requested.
    browser.test.assertEq(
      "count=1",
      await req.text(),
      "Got preflight before redirect target because of missing host_permissions"
    );

    browser.test.sendMessage("continue_preflight_tests");
  }

  let extension = ExtensionTestUtils.loadExtension({
    background,
    temporarilyInstalled: true, // Needed for granted_host_permissions
    allowInsecureRequests: true,
    manifest: {
      manifest_version: 3,
      granted_host_permissions: true,
      // "redir" and "example.net" are needed to allow redirection of these.
      // "dummy" is needed to redirect requests initiated from http://dummy.
      host_permissions: ["*://redir/*", "*://example.net/*", "*://dummy/*"],
      permissions: ["declarativeNetRequest"],
    },
  });
  gPreflightCount = 0;
  await extension.startup();
  await extension.awaitMessage("continue_preflight_tests");
  gPreflightCount = 0; // value already checked before continue_preflight_tests.

  // Simple request (i.e. without preflight requirement), that's redirected to
  // another URL by the DNR rule. The redirect should be accepted, and in
  // particular not be blocked by the same-origin policy. The redirect target
  // (/preflight_count) is readable due to the CORS headers from the server.
  Assert.deepEqual(
    await contentFetch("http://dummy/", "http://redir/never_reached"),
    // count=0: A simple request does not trigger a preflight (OPTIONS) request.
    { status: 418, url: "http://example.com/preflight_count", body: "count=0" },
    "Simple request should not have a preflight."
  );

  // Any request method other than "GET", "POST" and "PUT" (e.g "NONSIMPLE") is
  // a non-simple request that triggers a preflight request ("OPTIONS").
  //
  // Usually, this happens (without extension-triggered redirects):
  // 1. NONSIMPLE /never_reached : is started, but does NOT hit the server yet.
  // 2. OPTIONS /never_reached + Access-Control-Request-Method: NONSIMPLE
  // 3. NONSIMPLE /never_reached : reaches the server if allowed by OPTIONS.
  //
  // With an extension-initiated redirect to /preflight_count:
  // 1. NONSIMPLE /never_reached : is started, but does not hit the server yet.
  // 2. extension redirects to /preflight_count
  // 3. OPTIONS /preflight_count + Access-Control-Request-Method: NONSIMPLE
  //    - This is because the redirect preserves the request method/body/etc.
  // 4. NONSIMPLE /preflight_count : reaches the server if allowed by OPTIONS.
  Assert.deepEqual(
    await contentFetch("http://dummy/", "http://redir/never_reached", {
      method: "NONSIMPLE",
    }),
    // Due to excludedRequestMethods: ["options"], the preflight for the
    // redirect target is not intercepted, so the server sees a preflight.
    { status: 418, url: "http://example.com/preflight_count", body: "count=1" },
    "Initial URL redirected, redirection target has preflight"
  );
  gPreflightCount = 0;

  // The "example.net" rule has "excludedRequestMethods": ["nonsimple"], so the
  // initial "NONSIMPLE" request is not immediately redirected. Therefore the
  // preflight request happens. This OPTIONS request is matched by the DNR rule
  // and redirected to /preflight_count. While preflight_count offers a very
  // permissive preflight response, it is not even fetched:
  // Only a 2xx HTTP status is considered a valid response to a pre-flight.
  // A redirect is like a 3xx HTTP status, so the whole request is rejected,
  // and the redirect is not followed for the OPTIONS request.
  Assert.deepEqual(
    await contentFetch("http://dummy/", "http://example.net/never_reached", {
      method: "NONSIMPLE",
    }),
    { error: "NetworkError when attempting to fetch resource." },
    "Redirect of preflight request (OPTIONS) should be a CORS failure"
  );

  Assert.equal(gPreflightCount, 0, "Preflight OPTIONS has been intercepted");

  await extension.unload();
});

// Tests that DNR redirect rules can be chained.
add_task(async function redirect_request_with_dnr_multiple_hops() {
  async function background() {
    // Set up redirects from example.com up until dummy.
    let hosts = ["example.com", "example.net", "example.org", "redir", "dummy"];
    let rules = [];
    for (let i = 1; i < hosts.length; ++i) {
      const from = hosts[i - 1];
      const to = hosts[i];
      const end = hosts.length - 1 === i;
      rules.push({
        id: i,
        condition: { requestDomains: [from] },
        action: {
          type: "redirect",
          redirect: {
            // All intermediate redirects should never hit the server, but the
            // last one should..
            url: end ? `http://${to}/?end` : `http://${to}/never_reached`,
          },
        },
      });
    }
    await browser.declarativeNetRequest.updateSessionRules({ addRules: rules });
    let req = await fetch("http://example.com/never_reached");
    browser.test.assertEq(200, req.status, "redirected by DNR (multiple)");
    browser.test.assertEq("http://dummy/?end", req.url, "Last URL in chain");
    browser.test.assertEq("Dummy page", await req.text());

    browser.test.notifyPass();
  }
  let extension = ExtensionTestUtils.loadExtension({
    background,
    temporarilyInstalled: true, // Needed for granted_host_permissions
    allowInsecureRequests: true,
    manifest: {
      manifest_version: 3,
      granted_host_permissions: true,
      host_permissions: ["*://*/*"], // matches all in the |hosts| list.
      permissions: ["declarativeNetRequest"],
    },
  });
  await extension.startup();
  await extension.awaitFinish();

  // Test again, but without special extension permissions to verify that DNR
  // redirects pass CORS checks.
  Assert.deepEqual(
    await contentFetch("http://dummy/", "http://redir/never_reached"),
    { status: 200, url: "http://dummy/?end", body: "Dummy page" },
    "Multiple redirects by DNR, requested from web origin."
  );

  await extension.unload();
});

add_task(async function redirect_request_with_dnr_with_redirect_loop() {
  async function background() {
    await browser.declarativeNetRequest.updateSessionRules({
      addRules: [
        {
          id: 1,
          // requestMethods is mutually exclusive with the other rule.
          condition: { regexFilter: "^(.+)$", requestMethods: ["post"] },
          action: {
            type: "redirect",
            redirect: {
              // Appends "?loop" to the request URL
              regexSubstitution: "\\1?loop",
            },
          },
        },
        {
          id: 2,
          // requestMethods is mutually exclusive with the other rule.
          condition: { requestDomains: ["redir"], requestMethods: ["get"] },
          action: {
            type: "redirect",
            redirect: {
              // Despite redirect.url matching the condition, the redirect loop
              // should be caught because of the obvious fact that the URL did
              // not change.
              url: "http://redir/cors_202?loop",
            },
          },
        },
      ],
    });

    // Redirect where the redirect URL changes at every redirect.
    await browser.test.assertRejects(
      fetch("http://redir/cors_202?loop", { method: "post" }),
      "NetworkError when attempting to fetch resource.",
      "Redirect loop caught (redirect target differs at every redirect)"
    );

    async function assertRedirect(url, expected, description) {
      // method: "get" could only match rule 2.
      let res = await fetch(url);
      browser.test.assertDeepEq(
        expected,
        { status: res.status, url: res.url, redirected: res.redirected },
        description
      );
    }

    // Redirect with initially a different URL.
    await assertRedirect(
      "http://redir/never_reached?",
      { status: 202, url: "http://redir/cors_202?loop", redirected: true },
      "Redirect loop caught (initially different URL)"
    );

    // Redirect where redirect is exactly the same URL as requested.
    await assertRedirect(
      "http://redir/cors_202?loop",
      { status: 202, url: "http://redir/cors_202?loop", redirected: false },
      "Redirect loop caught (redirect target same as initial URL)"
    );

    browser.test.notifyPass();
  }
  let extension = ExtensionTestUtils.loadExtension({
    background,
    temporarilyInstalled: true, // Needed for granted_host_permissions
    allowInsecureRequests: true,
    manifest: {
      manifest_version: 3,
      granted_host_permissions: true,
      host_permissions: ["*://redir/*"],
      permissions: ["declarativeNetRequest"],
    },
  });
  await extension.startup();
  await extension.awaitFinish();
  await extension.unload();
});

// Tests that redirect to extensionPath works, provided that the initiator is
// either the extension itself, or in host_permissions. Moreover, the requested
// resource must match a web_accessible_resources entry for both the initiator
// AND the pre-redirect URL.
add_task(async function redirect_request_with_dnr_to_extensionPath() {
  async function background() {
    await browser.declarativeNetRequest.updateSessionRules({
      addRules: [
        {
          id: 1,
          condition: { requestDomains: ["redir"], requestMethods: ["post"] },
          action: {
            type: "redirect",
            redirect: {
              extensionPath: "/war.txt?1",
            },
          },
        },
        {
          id: 2,
          condition: { requestDomains: ["redir"], requestMethods: ["put"] },
          action: {
            type: "redirect",
            redirect: {
              extensionPath: "/nonwar.txt?2",
            },
          },
        },
      ],
    });
    {
      let req = await fetch("http://redir/never_reached", { method: "post" });
      browser.test.assertEq(200, req.status, "redirected to extensionPath");
      browser.test.assertEq(`${location.origin}/war.txt?1`, req.url);
      browser.test.assertEq("war_ext_res", await req.text());
    }
    // Redirects to extensionPath that is not in web_accessible_resources.
    // While the initiator (extension) would be allowed to read the resource
    // due to it being same-origin, the pre-redirect URL (http://redir) is not
    // matching web_accessible_resources[].matches, so the load is rejected.
    //
    // This behavior differs from Chrome (e.g. at least in Chrome 109) that
    // does allow the load to complete. Extensions who really care about
    // exposing a web-accessible resource to the world can just put an all_urls
    // pattern in web_accessible_resources[].matches.
    await browser.test.assertRejects(
      fetch("http://redir/never_reached", { method: "put" }),
      "NetworkError when attempting to fetch resource.",
      "Redirect to nowar.txt, but pre-redirect host is not in web_accessible_resources[].matches"
    );

    browser.test.notifyPass();
  }
  let extension = ExtensionTestUtils.loadExtension({
    background,
    temporarilyInstalled: true, // Needed for granted_host_permissions
    allowInsecureRequests: true,
    manifest: {
      manifest_version: 3,
      granted_host_permissions: true,
      host_permissions: ["*://redir/*", "*://dummy/*"],
      permissions: ["declarativeNetRequest"],
      web_accessible_resources: [
        // *://redir/* is in matches, because that is the pre-redirect host.
        // *://dummy/* is in matches, because that is an initiator below.
        { resources: ["war.txt"], matches: ["*://redir/*", "*://dummy/*"] },
        // without "matches", this is almost equivalent to not being listed in
        // web_accessible_resources at all. This entry is listed here to verify
        // that the presence of extension_ids does not somehow allow a request
        // with an extension initiator to complete.
        { resources: ["nonwar.txt"], extension_ids: ["*"] },
      ],
    },
    files: {
      "war.txt": "war_ext_res",
      "nonwar.txt": "non_war_ext_res",
    },
  });
  await extension.startup();
  await extension.awaitFinish();
  const extPrefix = `moz-extension://${extension.uuid}`;

  // Request from origin in host_permissions, for web-accessible resource.
  Assert.deepEqual(
    await contentFetch(
      "http://dummy/", // <-- Matching web_accessible_resources[].matches
      "http://redir/never_reached", // <-- With matching host_permissions
      { method: "post" }
    ),
    { status: 200, url: `${extPrefix}/war.txt?1`, body: "war_ext_res" },
    "Should have got redirect to web_accessible_resources (war.txt)"
  );

  // Request from origin in host_permissions, for non-web-accessible resource.
  let { messages } = await promiseConsoleOutput(async () => {
    Assert.deepEqual(
      await contentFetch(
        "http://dummy/", // <-- Matching web_accessible_resources[].matches
        "http://redir/never_reached", // <-- With matching host_permissions
        { method: "put" }
      ),
      { error: "NetworkError when attempting to fetch resource." },
      "Redirect to nowar.txt, without matching web_accessible_resources[].matches"
    );
  });
  const EXPECTED_SECURITY_ERROR = `Content at http://redir/never_reached may not load or link to ${extPrefix}/nonwar.txt?2.`;
  Assert.equal(
    messages.filter(m => m.message.includes(EXPECTED_SECURITY_ERROR)).length,
    1,
    `Should log SecurityError: ${EXPECTED_SECURITY_ERROR}`
  );

  // Request from origin not in host_permissions. DNR rule should not apply.
  Assert.deepEqual(
    await contentFetch(
      "http://dummy/", // <-- Matching web_accessible_resources[].matches
      "http://example.com/cors_202", // <-- NOT in host_permissions
      { method: "post" }
    ),
    { status: 202, url: "http://example.com/cors_202", body: "cors_response" },
    "Extension should not have redirected, due to lack of host permissions"
  );

  await extension.unload();
});
