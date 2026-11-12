"use strict";

const server = createHttpServer({
  hosts: ["dummy", "restricted", "yes", "no", "maybe", "cookietest"],
});
server.registerPathHandler("/echoheaders", (req, res) => {
  res.setHeader("Content-Type", "application/json");
  const headers = Object.create(null);
  for (const nameSupports of req.headers) {
    const name = nameSupports.QueryInterface(Ci.nsISupportsString).data;
    // httpd.js automatically concats headers with ",", but in some cases it
    // stores them separately, joined with "\n".
    // https://searchfox.org/mozilla-central/rev/c1180ea13e73eb985a49b15c0d90e977a1aa919c/netwerk/test/httpserver/httpd.js#5271-5286
    const values = req.getHeader(name).split("\n");
    headers[name] = values.length === 1 ? values[0] : values;
  }

  // Only keep custom headers, so that the test expectations does not have to
  // enumerate all headers of interest.
  function dropDefaultHeader(name) {
    if (!(name in headers)) {
      Assert.ok(false, `Header unexpectedly not found: ${name}`);
    }
    delete headers[name];
  }
  dropDefaultHeader("host");
  dropDefaultHeader("user-agent");
  dropDefaultHeader("accept");
  dropDefaultHeader("accept-language");
  dropDefaultHeader("accept-encoding");
  dropDefaultHeader("connection");
  dropDefaultHeader("priority");

  res.write(JSON.stringify(headers));
});

server.registerPathHandler("/host", (req, res) => {
  res.write(req.getHeader("Host"));
});

server.registerPathHandler("/csptest", (req, res) => {
  res.setHeader("Access-Control-Allow-Origin", "*");
  res.write("EXPECTED_RESPONSE_FOR /csp test");
});
server.registerPathHandler("/csp", (req, res) => {
  // Inserting the ";" just in case something somehow merges the headers by ","
  // (e.g. to "bla,; default-src http://yes http://maybe ;,bla").
  // This ensures that the server-set "default-src" CSP is not somehow mangled.
  res.setHeader(
    "Content-Security-Policy",
    "; default-src http://yes http://maybe ;"
  );
});

server.registerPathHandler("/responseheadersFixture", (req, res) => {
  res.setHeader("a", "server_a");
  res.setHeader("b", "server_b");
  res.setHeader("c", "server_c");
  res.setHeader("d", "server_d");
  res.setHeader("e", "server_e");
  // www-authenticate and proxy-authenticate are among the few headers where
  // the test server (httpd.js) allows multiple header lines instead of
  // automatically concatenating them with ",":
  // https://searchfox.org/mozilla-central/rev/a4a41aafa80bf38f6e456238a60781fed46f9d08/netwerk/test/httpserver/httpd.js#5280
  res.setHeader("www-authenticate", "first_line");
  res.setHeader("www-authenticate", "second_line", /* merge */ true);
  res.setHeader("proxy-authenticate", "first_line");
  res.setHeader("proxy-authenticate", "second_line", /* merge */ true);
});

server.registerPathHandler("/setcookie", (req, res) => {
  // set-cookie is also allowed to span multiple lines.
  res.setHeader("Set-Cookie", "food=yummy; max-age=999");
  res.setHeader("Set-Cookie", "second=serving; max-age=999", /* merge */ true);
  res.write(req.hasHeader("Cookie") ? req.getHeader("Cookie") : "");
});
server.registerPathHandler("/empty", () => {});

add_setup(() => {
  Services.prefs.setBoolPref("extensions.dnr.enabled", true);

  // The restrictedDomains pref should be set early, because the pref is read
  // only once (on first use) by WebExtensionPolicy::IsRestrictedURI.
  Services.prefs.setCharPref(
    "extensions.webextensions.restrictedDomains",
    "restricted"
  );
});

// This function is serialized and called in the context of the test extension's
// background page. dnrTestUtils is passed to the background function.
function makeDnrTestUtils() {
  const dnrTestUtils = {};
  async function fetchAsJson(url, options) {
    let res = await fetch(url, options);
    let txt = await res.text();
    try {
      return JSON.parse(txt);
    } catch (e) {
      return txt;
    }
  }
  Object.assign(dnrTestUtils, {
    fetchAsJson,
  });
  return dnrTestUtils;
}

async function runAsDNRExtension({
  background,
  manifest,
  unloadTestAtEnd = true,
}) {
  let extension = ExtensionTestUtils.loadExtension({
    background: `(${background})((${makeDnrTestUtils})())`,
    allowInsecureRequests: true,
    manifest: {
      manifest_version: 3,
      permissions: ["declarativeNetRequest"],
      host_permissions: ["<all_urls>"],
      granted_host_permissions: true,
      ...manifest,
    },
    temporarilyInstalled: true, // <-- for granted_host_permissions
  });
  await extension.startup();
  await extension.awaitFinish();
  if (unloadTestAtEnd) {
    await extension.unload();
  }
  return extension;
}

add_task(async function modifyHeaders_requestHeaders() {
  await runAsDNRExtension({
    background: async dnrTestUtils => {
      const { fetchAsJson } = dnrTestUtils;
      await browser.declarativeNetRequest.updateSessionRules({
        addRules: [
          {
            id: 1,
            condition: { urlFilter: "set_twice" },
            action: {
              type: "modifyHeaders",
              requestHeaders: [
                { operation: "set", header: "a", value: "a-first" },
                // second set should be ignored after set.
                { operation: "set", header: "a", value: "a-second" },
              ],
            },
          },
          {
            id: 2,
            condition: { urlFilter: "set_and_remove" },
            action: {
              type: "modifyHeaders",
              requestHeaders: [
                { operation: "set", header: "b", value: "b-value" },
                // remove should be ignored after set.
                { operation: "remove", header: "b" },
              ],
            },
          },
          {
            id: 3,
            condition: { urlFilter: "remove_and_set" },
            action: {
              type: "modifyHeaders",
              requestHeaders: [
                { operation: "remove", header: "c" },
                // set should be ignored after remove.
                { operation: "set", header: "c", value: "c-value" },
                // append should be ignored after remove.
                { operation: "append", header: "c", value: "c-appended" },
              ],
            },
          },
          {
            id: 4,
            condition: { urlFilter: "remove_only" },
            action: {
              type: "modifyHeaders",
              requestHeaders: [{ operation: "remove", header: "d" }],
            },
          },
          {
            id: 5,
            condition: { urlFilter: "append_twice" },
            action: {
              type: "modifyHeaders",
              requestHeaders: [
                { operation: "append", header: "e", value: "e-first" },
                { operation: "append", header: "e", value: "e-second" },
              ],
            },
          },
          {
            id: 6,
            condition: { urlFilter: "set_and_append" },
            action: {
              type: "modifyHeaders",
              requestHeaders: [
                { operation: "set", header: "f", value: "f-first" },
                { operation: "append", header: "f", value: "f-second" },
              ],
            },
          },
        ],
      });

      browser.test.assertDeepEq(
        { existing: "header" },
        await fetchAsJson(
          "http://dummy/echoheaders?not_matching_any_dnr_rule",
          { headers: { existing: "header" } }
        ),
        "Sanity check: should echo original headers without matching DNR rules"
      );

      // Tests set_twice rule:

      browser.test.assertDeepEq(
        { a: "a-first" },
        await fetchAsJson("http://dummy/echoheaders?set_twice"),
        "only the first header should be used when set twice"
      );
      browser.test.assertDeepEq(
        { a: "a-first" },
        await fetchAsJson("http://dummy/echoheaders?set_twice", {
          headers: { a: "original" },
        }),
        "original header should be overwritten by DNR"
      );

      // Tests set_and_remove rule:

      browser.test.assertDeepEq(
        { b: "b-value" },
        await fetchAsJson("http://dummy/echoheaders?set_and_remove"),
        "after setting a header, remove should be ignored"
      );
      browser.test.assertDeepEq(
        { b: "b-value" },
        await fetchAsJson("http://dummy/echoheaders?set_and_remove", {
          headers: { b: "original" },
        }),
        "after overwriting a header, remove should be ignored"
      );

      // Tests remove_and_set rule:

      browser.test.assertDeepEq(
        { start: "START", end: "end" },
        await fetchAsJson("http://dummy/echoheaders?remove_and_set", {
          headers: { start: "START", c: "remove me", end: "end" },
        }),
        "after removing a header, remove should be ignored"
      );
      browser.test.assertDeepEq(
        {},
        await fetchAsJson("http://dummy/echoheaders?remove_and_set"),
        "after a remove op (despite no existing header), set should be ignored"
      );

      // Tests remove_only rule:

      browser.test.assertDeepEq(
        {},
        await fetchAsJson("http://dummy/echoheaders?remove_only", {
          headers: { d: "remove me please" },
        }),
        "should remove header"
      );

      // Tests append_twice rule:

      browser.test.assertDeepEq(
        { e: "original, e-first, e-second" },
        await fetchAsJson("http://dummy/echoheaders?append_twice", {
          headers: { e: "original" },
        }),
        "should append headers"
      );
      browser.test.assertDeepEq(
        { e: "e-first, e-second" },
        await fetchAsJson("http://dummy/echoheaders?append_twice"),
        "should append headers if there are no existing ones yet"
      );

      // Tests set_and_append rule:

      browser.test.assertDeepEq(
        { f: "f-first, f-second" },
        await fetchAsJson("http://dummy/echoheaders?set_and_append", {
          headers: { f: "original" },
        }),
        "should overwrite and append headers"
      );

      // All rules together:

      browser.test.assertDeepEq(
        {
          a: "a-first",
          b: "b-value",
          e: "olde, e-first, e-second",
          f: "f-first, f-second",
          extra: "",
        },
        await fetchAsJson(
          "http://dummy/echoheaders?set_twice,set_and_remove,remove_and_set,remove_only,append_twice,set_and_append",
          {
            headers: {
              a: "olda",
              b: "oldb",
              c: "oldc",
              d: "oldd",
              e: "olde",
              f: "oldf",
              extra: "",
            },
          }
        ),
        "modifyHeaders actions from multiple rules should all apply"
      );

      browser.test.notifyPass();
    },
  });
});

// Host header is restricted, for details see bug 1467523.
add_task(async function requestHeaders_set_host_header() {
  async function background() {
    const makeModifyHostRule = (id, urlFilter, value) => ({
      id,
      condition: { urlFilter },
      action: {
        type: "modifyHeaders",
        requestHeaders: [{ operation: "set", header: "Host", value }],
      },
    });
    await browser.declarativeNetRequest.updateSessionRules({
      addRules: [
        makeModifyHostRule(1, "yes_host_permissions", "yes"),
        makeModifyHostRule(2, "no_host_permissions", "no"),
        makeModifyHostRule(3, "restricted_domain", "restricted"),
      ],
    });

    browser.test.assertEq(
      "yes",
      await (await fetch("http://dummy/host?yes_host_permissions")).text(),
      "Host header value allowed if extension has permission for new value"
    );

    browser.test.assertEq(
      "dummy",
      await (await fetch("http://dummy/host?no_host_permissions")).text(),
      "Host header value ignored if extension misses permission for new value"
    );

    browser.test.assertEq(
      "dummy",
      await (await fetch("http://dummy/host?restricted_domain")).text(),
      "Host header value ignored if new host is in restrictedDomains"
    );

    browser.test.notifyPass();
  }
  const { messages } = await promiseConsoleOutput(async () => {
    await runAsDNRExtension({
      manifest: {
        // Note: host_permissions without "*://no/*".
        host_permissions: ["*://dummy/*", "*://yes/*", "*://restricted/*"],
      },
      background,
    });
  });

  AddonTestUtils.checkMessages(messages, {
    expected: [
      {
        message:
          /Failed to apply modifyHeaders action to header "Host" \(DNR rule id 2 from ruleset "_session"\): Error: Unable to set host header, url missing from permissions\./,
      },
      {
        message:
          /Failed to apply modifyHeaders action to header "Host" \(DNR rule id 3 from ruleset "_session"\): Error: Unable to set host header to restricted url\./,
      },
    ],
  });
});

add_task(async function requestHeaders_set_host_header_multiple_extensions() {
  async function background() {
    const hostHeaderValue = browser.runtime.getManifest().name;

    await browser.declarativeNetRequest.updateSessionRules({
      addRules: [
        {
          id: 1,
          condition: { resourceTypes: ["xmlhttprequest"] },
          action: {
            type: "modifyHeaders",
            requestHeaders: [
              { operation: "set", header: "Host", value: hostHeaderValue },
              // Add a unique header for each request to verify that the
              // extension can still modify other headers despite failure to
              // modify the host header.
              { operation: "set", header: hostHeaderValue, value: "setbydnr" },
            ],
          },
        },
      ],
    });
    browser.test.notifyPass();
  }
  // Precedence is in install order, most recent first.
  // While this extension is permitted to change Host to "maybe", it has a lower
  // precedence than extensionWithPermissionAndHigherPrecedence.
  let extensionWithPermissionButLowerPrecedence = await runAsDNRExtension({
    unloadTestAtEnd: false,
    manifest: {
      name: "maybe",
      host_permissions: ["*://dummy/*", "*://maybe/*"],
    },
    background,
  });
  // This extension is permitted to change Host to "yes".
  let extensionWithPermissionAndHigherPrecedence = await runAsDNRExtension({
    unloadTestAtEnd: false,
    manifest: { name: "yes", host_permissions: ["*://dummy/*", "*://yes/*"] },
    background,
  });
  // While this extension has the highest precedence by install order, it does
  // not have permission to change "Host" to "no".
  let extensionWithoutPermissionForHostHeader = await runAsDNRExtension({
    unloadTestAtEnd: false,
    manifest: { name: "no", host_permissions: ["*://dummy/*"] },
    background,
  });

  Assert.equal(
    await ExtensionTestUtils.fetch("http://dummy/", "http://dummy/host"),
    "yes",
    "Host header changedby the most recently installed extension with the right permission"
  );

  const { messages, result } = await promiseConsoleOutput(() =>
    ExtensionTestUtils.fetch("http://dummy/", "http://dummy/echoheaders")
  );
  Assert.equal(
    result,
    `{"referer":"http://dummy/","no":"setbydnr","yes":"setbydnr","maybe":"setbydnr"}`,
    "Host header changedby the most recently installed extension with the right permission"
  );
  AddonTestUtils.checkMessages(messages, {
    expected: [
      {
        message:
          /Failed to apply modifyHeaders action to header "Host" \(DNR rule id 1 from ruleset "_session"\): Error: Unable to set host header, url missing from permissions\./,
      },
    ],
  });

  await extensionWithPermissionButLowerPrecedence.unload();
  await extensionWithPermissionAndHigherPrecedence.unload();
  await extensionWithoutPermissionForHostHeader.unload();
});

add_task(async function modifyHeaders_responseHeaders() {
  await runAsDNRExtension({
    background: async () => {
      await browser.declarativeNetRequest.updateSessionRules({
        addRules: [
          {
            id: 1,
            condition: { urlFilter: "/responseheadersFixture" },
            action: {
              type: "modifyHeaders",
              responseHeaders: [
                { operation: "set", header: "a", value: "a-first" },
                // remove after set should be ignored:
                { operation: "remove", header: "a" },
                // Second set should be ignored:
                { operation: "set", header: "a", value: "a-second" },
                // But append is permitted:
                { operation: "append", header: "a", value: "a-third" },
                // Another append is allowed too:
                { operation: "append", header: "a", value: "a-fourth" },
                // An unrelated set is accepted:
                { operation: "set", header: "b", value: "b-dnr" },
                // An unrelated remove is also accepted:
                { operation: "remove", header: "c" },
                // An unrelated append is also accepted:
                { operation: "append", header: "d", value: "d-dnr" },
                // The server also sends the "e" header, we don't touch that.

                // The server sends the www-authenticate header on two lines,
                // which should be removed.
                { operation: "remove", header: "www-authenticate" },
                // The server also sends the proxy-authenticate header on two
                // lines, but we don't touch that.
              ],
            },
          },
        ],
      });

      let { headers } = await fetch("http://dummy/responseheadersFixture");
      browser.test.assertEq(
        "a-first, a-third, a-fourth",
        headers.get("a"),
        "a set, ignored set + remove, 2x append"
      );
      browser.test.assertEq("b-dnr", headers.get("b"), "b set");
      browser.test.assertEq(null, headers.get("c"), "c removed");
      browser.test.assertEq("server_d, d-dnr", headers.get("d"), "d appended");
      browser.test.assertEq("server_e", headers.get("e"), "e not touched");
      browser.test.assertEq(
        null,
        headers.get("www-authenticate"),
        "multi-line www-authenticate header removed"
      );

      // Multi-line http headers cannot be tested through fetch/Headers. This is
      // a known limitation of that API, see e.g. note about Set-Cookie in the
      // fetch spec - https://fetch.spec.whatwg.org/#headers-class
      browser.test.assertEq(
        null, // Note: null because Headers does not see multi-line headers.
        headers.get("proxy-authenticate"),
        "multi-line proxy-authenticate header kept (but fetch cannot see it)"
      );

      // XMLHttpRequest can return multi-line values, so we use that instead.
      const xhr = new XMLHttpRequest();
      await new Promise(r => {
        xhr.onloadend = r;
        xhr.open("GET", "http://dummy/responseheadersFixture?xhr");
        xhr.send();
      });
      browser.test.assertEq(
        null,
        xhr.getResponseHeader("www-authenticate"),
        "multi-line www-authenticate header removed"
      );
      browser.test.assertEq(
        "first_line\nsecond_line",
        xhr.getResponseHeader("proxy-authenticate"),
        "multi-line proxy-authenticate header kept (seen through XHR)"
      );

      browser.test.notifyPass();
    },
  });
});

add_task(async function responseHeaders_set_content_security_policy_header() {
  let extension = await runAsDNRExtension({
    unloadTestAtEnd: false,
    background: async () => {
      // By default, a DNR condition excludes the main frame. But to verify that
      // the CSP works, we have to modify the CSP header of a document request.
      const resourceTypes = ["main_frame"];
      await browser.declarativeNetRequest.updateSessionRules({
        addRules: [
          {
            id: 1,
            condition: { resourceTypes, urlFilter: "/csp?remove" },
            action: {
              type: "modifyHeaders",
              responseHeaders: [
                { operation: "remove", header: "Content-Security-Policy" },
              ],
            },
          },
          {
            id: 2,
            condition: { resourceTypes, urlFilter: "/csp?append_to_server" },
            action: {
              type: "modifyHeaders",
              responseHeaders: [
                {
                  operation: "append",
                  header: "Content-Security-Policy",
                  // Server has "default-src http://yes http://maybe". When
                  // multiple CSP header lines are present, all policies should
                  // be enforced, thus "http://no" below should be ignored, and
                  // the "http://maybe" from the server be ignored.
                  value: "connect-src http://YES http://not-maybe http://no",
                },
              ],
            },
          },
          {
            id: 3,
            condition: { resourceTypes, urlFilter: "/csp?set_and_append" },
            action: {
              type: "modifyHeaders",
              responseHeaders: [
                {
                  operation: "set",
                  header: "Content-Security-Policy",
                  value: "connect-src 1-of-2 http://yes http://maybe",
                },
                {
                  operation: "append",
                  header: "Content-Security-Policy",
                  value: "connect-src 2-of-2 http://yes",
                },
              ],
            },
          },
        ],
      });

      browser.test.notifyPass();
    },
  });

  async function testFetchAndCSP(url) {
    info(`testFetchAndCSP: ${url}`);
    let contentPage = await ExtensionTestUtils.loadContentPage(url);
    let cspTestResults = await contentPage.spawn([], async () => {
      const { document } = content;
      async function doFetchAndCheckCSP(url) {
        const cspTestResult = { url, violatedCSP: [] };
        let cspListener;
        let cspEventPromise = new Promise(resolve => {
          cspListener = e => {
            cspTestResult.violatedCSP.push(e.originalPolicy);
            // A CSP violation results in an event for each violated policy,
            // dispatched after each other. Post a macrotask to ensure that all
            // violations are caught.
            content.setTimeout(resolve, 0);
          };
        });
        document.addEventListener("securitypolicyviolation", cspListener);
        try {
          let res = await content.fetch(url);
          let responseText = await res.text();
          if (responseText !== "EXPECTED_RESPONSE_FOR /csp test") {
            cspTestResult.unexpectedResponseText = responseText;
          }
          // No await cspEventPromise, because we are not expecting any errors.
          // If there was any CSP violation, we would have ended in catch.
        } catch (e) {
          dump(`\nFailed to fetch ${url}, waiting for CSP report/event.\n`);
          await cspEventPromise;
        }
        document.removeEventListener("securitypolicyviolation", cspListener);
        return cspTestResult;
      }

      return {
        yes: await doFetchAndCheckCSP("http://yes/csptest"),
        maybe: await doFetchAndCheckCSP("http://maybe/csptest"),
        no: await doFetchAndCheckCSP("http://no/csptest"),
      };
    });
    await contentPage.close();
    return cspTestResults;
  }

  // Note: this is derived from the server's policy. The server sends a bit more
  // in the Content-Security-Policy header (i.e. ";"), but the normalized form
  // is as follows.
  const SERVER_DEFAULT_CSP = "default-src http://yes http://maybe";

  // First, sanity check:
  Assert.deepEqual(
    await testFetchAndCSP("http://dummy/csp"),
    {
      yes: { url: "http://yes/csptest", violatedCSP: [] },
      maybe: { url: "http://maybe/csptest", violatedCSP: [] },
      no: { url: "http://no/csptest", violatedCSP: [SERVER_DEFAULT_CSP] },
    },
    "Sanity check: Server sends CSP that only allows requests to http://yes."
  );

  Assert.deepEqual(
    await testFetchAndCSP("http://dummy/csp?remove"),
    {
      yes: { url: "http://yes/csptest", violatedCSP: [] },
      maybe: { url: "http://maybe/csptest", violatedCSP: [] },
      no: { url: "http://no/csptest", violatedCSP: [] },
    },
    "DNR remove CSP: results in no requests blocked by CSP"
  );

  Assert.deepEqual(
    {
      yes: { url: "http://yes/csptest", violatedCSP: [] },
      maybe: {
        url: "http://maybe/csptest",
        violatedCSP: [
          // This value was appended by DNR (with upper-case "http://YES", but
          // the normalized form should be lowercase "http://yes"), and notably
          // the "yes" request above should still pass.
          "connect-src http://yes http://not-maybe http://no",
        ],
      },
      no: { url: "http://no/csptest", violatedCSP: [SERVER_DEFAULT_CSP] },
    },
    await testFetchAndCSP("http://dummy/csp?append_to_server"),
    "DNR append CSP: should enforce CSP of server and DNR"
  );

  Assert.deepEqual(
    await testFetchAndCSP("http://dummy/csp?set_and_append"),
    {
      yes: { url: "http://yes/csptest", violatedCSP: [] },
      maybe: {
        url: "http://maybe/csptest",
        violatedCSP: [
          // Note: "http://" is before 2-of-2 due to bug 1804145.
          "connect-src http://2-of-2 http://yes",
        ],
      },
      no: {
        url: "http://no/csptest",
        violatedCSP: [
          // Note: "http://" is before 1-of-2 and 2-of-2 due to bug 1804145.
          "connect-src http://1-of-2 http://yes http://maybe",
          "connect-src http://2-of-2 http://yes",
        ],
      },
    },
    "DNR set + append CSP: should enforce both CSPs from DNR"
  );

  await extension.unload();
});

// Set-Cookie is special because it may span multiple lines. This test tests a
// combination of requestHeaders/responseHeaders and that the DNR-set cookies
// are really working, i.e. visible to server and/or modifying the client's
// cookie jar.
add_task(async function requestHeaders_and_responseHeaders_cookies() {
  let extension = await runAsDNRExtension({
    unloadTestAtEnd: false,
    background: async () => {
      // By default, a DNR condition excludes the main frame. But this test uses
      // a document load to verify that cookie header modifications (if any) are
      // reflected in document.cookie.
      const resourceTypes = ["main_frame"];

      await browser.declarativeNetRequest.updateSessionRules({
        addRules: [
          {
            id: 1,
            condition: { resourceTypes, urlFilter: "dnr_resp_drop_cookie" },
            action: {
              type: "modifyHeaders",
              responseHeaders: [{ operation: "remove", header: "set-cookie" }],
            },
          },
          {
            id: 2,
            condition: { resourceTypes, urlFilter: "dnr_resp_set_cookie" },
            action: {
              type: "modifyHeaders",
              responseHeaders: [
                {
                  operation: "set",
                  header: "set-cookie",
                  value: "dnr_res=set; max-age=999",
                },
              ],
            },
          },
          {
            id: 3,
            condition: { resourceTypes, urlFilter: "dnr_set_cookie_to_req" },
            action: {
              type: "modifyHeaders",
              requestHeaders: [
                { operation: "set", header: "cookie", value: "dnr_req=1" },
              ],
            },
          },
          {
            id: 4,
            condition: {
              resourceTypes,
              urlFilter: "dnr_append_cookie_to_req_and_res",
            },
            action: {
              type: "modifyHeaders",
              requestHeaders: [
                // Just for extra coverage, mix upper/lower case.
                { operation: "append", header: "Cookie", value: "DNR_APP=1" },
                { operation: "append", header: "cookie", value: "DNR_app=2" },
              ],
              responseHeaders: [
                {
                  operation: "append",
                  header: "set-cookie",
                  value: "dnr_res=appended; max-age=999",
                },
              ],
            },
          },
          {
            id: 5,
            condition: {
              resourceTypes,
              urlFilter: "dnr_set_server_cookies_expired",
            },
            action: {
              type: "modifyHeaders",
              responseHeaders: [
                {
                  operation: "set",
                  header: "set-cookie",
                  value: "food=deletedbydnr; second=deletedbydnr; max-age=-1",
                },
                {
                  operation: "append",
                  header: "set-cookie",
                  value: "second=deletedbydnr; max-age=-1",
                },
              ],
            },
          },
          {
            id: 6,
            condition: {
              resourceTypes,
              urlFilter: "dnr_resp_append_expired_cookie",
            },
            action: {
              type: "modifyHeaders",
              responseHeaders: [
                {
                  operation: "append",
                  header: "set-cookie",
                  value: "dnr_res=deleteme; max-age=-1",
                },
              ],
            },
          },
        ],
      });

      browser.test.notifyPass();
    },
  });

  async function loadPageAndGetCookies(pathAndQuery) {
    const url = `http://cookietest${pathAndQuery}`;
    info(`loadPageAndGetCookies: ${url}`);
    let contentPage = await ExtensionTestUtils.loadContentPage(url);
    let res = await contentPage.spawn([], () => {
      const { document } = content;
      const sortCookies = s => s.split("; ").sort().join("; ");
      return {
        // Server at /setcookie echos value of Cookie request header.
        serverSeenCookies: sortCookies(document.body.textContent),
        clientSeenCookies: sortCookies(document.cookie),
      };
    });
    await contentPage.close();
    return res;
  }

  Assert.deepEqual(
    { serverSeenCookies: "", clientSeenCookies: "" },
    await loadPageAndGetCookies("/setcookie?dnr_resp_drop_cookie"),
    "Set-Cookie from server ignored due to DNR (remove Set-Cookie)"
  );
  Assert.deepEqual(
    {
      serverSeenCookies: "",
      clientSeenCookies: "dnr_res=set",
    },
    await loadPageAndGetCookies("/setcookie?dnr_resp_set_cookie"),
    "Set-Cookie from server overwritten by DNR (set Set-Cookie)"
  );
  Assert.deepEqual(
    {
      // No cookies from previous request + request-specific cookie from DNR.
      serverSeenCookies: "dnr_req=1",
      // Notably, "dnr_req=1" should be missing from clientSeenCookies, because
      // it is added in the request, so only seen by the server. Only cookies
      // set by Set-Cookie are persisted/seen by the client.
      clientSeenCookies: "dnr_res=set; food=yummy; second=serving",
    },
    await loadPageAndGetCookies("/setcookie?dnr_set_cookie_to_req"),
    "Cookie req header from DNR, shadows existing client-generated Cookie header"
  );
  Assert.deepEqual(
    {
      // Cookies from previous request + request-specific cookies from DNR.
      serverSeenCookies:
        "DNR_APP=1; DNR_app=2; dnr_res=set; food=yummy; second=serving",
      // NDR_APP and DNR_app are notably missing. dnr_res was modified by DNR,
      // because an appended cookie with the same name overwrites existing one.
      clientSeenCookies: "dnr_res=appended; food=yummy; second=serving",
    },
    await loadPageAndGetCookies("/setcookie?dnr_append_cookie_to_req_and_res"),
    "Cookie req header from DNR, merged with existing client cookies; Set-Cookie from server merged with DNR (append Set-Cookie)"
  );
  Assert.deepEqual(
    {
      // Cookies from previous request (not changed by DNR):
      serverSeenCookies: "dnr_res=appended; food=yummy; second=serving",
      // Server cookies removed, only previously added DNR cookie sticks:
      clientSeenCookies: "dnr_res=appended",
    },
    await loadPageAndGetCookies("/setcookie?dnr_set_server_cookies_expired"),
    "Set-Cookie from server expired by DNR (set Set-Cookie + expire server cookies)"
  );
  Assert.deepEqual(
    {
      // Cookies from previous request (not changed by DNR):
      serverSeenCookies: "dnr_res=appended",
      // Cookies from server; because we used "append", they should merge, and
      // expire the previous DNR cookie, and create the server-set cookies.
      clientSeenCookies: "food=yummy; second=serving",
    },
    await loadPageAndGetCookies("/setcookie?dnr_resp_append_expired_cookie"),
    "Set-Cookie from server merged with DNR (append Set-Cookie + expire dnr_res)"
  );
  // We've already tested dnr_set_server_cookies_expired before, now we're just
  // cleaning up.
  Assert.deepEqual(
    {
      serverSeenCookies: "food=yummy; second=serving",
      clientSeenCookies: "",
    },
    await loadPageAndGetCookies("/setcookie?dnr_set_server_cookies_expired"),
    "DNR cleared remaining cookies (set Set-Cookie + expire server cookies)"
  );

  await extension.unload();
});

// This test confirms the effective modifyHeaders actions if multiple extensions
// have matching modifyHeaders rules. Only one extension is allowed to modify
// headers.
add_task(async function modifyHeaders_multiple_extensions() {
  async function background() {
    const extName = browser.runtime.getManifest().name;
    function makeModifyHeadersRule(id, operation, headerName) {
      const urlFilter = `${extName}_${operation}_${headerName}`;
      let value;
      if (operation !== "remove") {
        // Use the urlFilter as value so that it's obvious which rule added it.
        value = urlFilter;
      }
      return {
        id,
        condition: { urlFilter },
        action: {
          type: "modifyHeaders",
          // As the logic of responseHeaders and requestHeaders is shared, it
          // suffices to only check responseHeaders here.
          responseHeaders: [{ operation, header: headerName, value }],
        },
      };
    }
    await browser.declarativeNetRequest.updateSessionRules({
      addRules: [
        makeModifyHeadersRule(1, "set", "a"),
        makeModifyHeadersRule(2, "remove", "a"),
        makeModifyHeadersRule(3, "append", "a"),
        makeModifyHeadersRule(4, "set", "b"),
        makeModifyHeadersRule(5, "remove", "b"),
        makeModifyHeadersRule(6, "append", "b"),
      ],
    });
    browser.test.notifyPass();
  }

  // Cross-extension rule precedence is in the order of extension installation.
  const prioTwoExtension = await runAsDNRExtension({
    manifest: { name: "prioTwo" },
    background,
    unloadTestAtEnd: false,
  });
  const prioOneExtension = await runAsDNRExtension({
    manifest: { name: "prioOne" },
    background,
    unloadTestAtEnd: false,
  });

  let contentPage =
    await ExtensionTestUtils.loadContentPage("http://dummy/empty");
  async function checkHeaderActionResult(query, expectedHeaders, description) {
    const url = `/responseheadersFixture?${query}`;
    const result = await contentPage.spawn([url], async url => {
      const res = await content.fetch(url);
      return {
        a: res.headers.get("a"),
        b: res.headers.get("b"),
      };
    });
    Assert.deepEqual(
      result,
      expectedHeaders,
      `${description} - Expected headers for ${url}`
    );
  }

  await checkHeaderActionResult(
    "",
    {
      a: "server_a",
      b: "server_b",
    },
    "Sanity check: headers should be unmodified without matching DNR rules"
  );

  // First: verify that "set" is only permitted if there are no other extensions
  // that have already modified the header. Note that this requirement already
  // holds for actions within one extension, so they should still be enforced
  // for modifyHeaders actions from multiple extensions.
  await checkHeaderActionResult(
    "prioOne_set_a,prioTwo_set_a,prioTwo_set_b",
    {
      a: "prioOne_set_a",
      b: "prioTwo_set_b",
    },
    "set should only be allowed if no other extension has set a header"
  );
  await checkHeaderActionResult(
    "prioOne_remove_a,prioTwo_set_a,prioTwo_set_b",
    {
      a: null,
      b: "prioTwo_set_b",
    },
    "set should only be allowed if no other extension has removed the header"
  );
  await checkHeaderActionResult(
    "prioOne_append_a,prioTwo_set_a,prioTwo_set_b",
    {
      a: "server_a, prioOne_append_a",
      b: "prioTwo_set_b",
    },
    "set should only be allowed if no other extension has appended the header"
  );

  // The "remove" operation is not logically conflicting, let's confirm that it
  // works as usual.
  await checkHeaderActionResult(
    "prioOne_remove_a,prioTwo_remove_a,prioTwo_remove_b",
    {
      a: null,
      b: null,
    },
    "remove should work, regardless of the number of extensions that use it"
  );

  // While an extension can specify multiple "append" operations, only one
  // extension should be able to use it. Another extension is still allowed to
  // modify an unrelated, not-yet-modified header.
  await checkHeaderActionResult(
    "prioOne_append_a,prioTwo_append_a,prioTwo_append_b",
    {
      a: "server_a, prioOne_append_a",
      b: "server_b, prioTwo_append_b",
    },
    "Only one extension may modify a specific header"
  );

  await contentPage.close();
  await prioOneExtension.unload();
  await prioTwoExtension.unload();
});
