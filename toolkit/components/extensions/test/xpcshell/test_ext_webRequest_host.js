"use strict";

const HOSTS = new Set(["example.com", "example.org"]);

const server = createHttpServer({ hosts: HOSTS });

const BASE_URL = "http://example.com";
const FETCH_ORIGIN = "http://example.com/dummy";

server.registerPathHandler("/return_headers.sjs", (request, response) => {
  response.setHeader("Content-Type", "text/plain", false);

  let headers = {};
  for (let { data: header } of request.headers) {
    headers[header] = request.getHeader(header);
  }

  response.write(JSON.stringify(headers));
});

server.registerPathHandler("/dummy", (request, response) => {
  response.setStatusLine(request.httpVersion, 200, "OK");
  response.write("ok");
});

function getExtension(permission = "<all_urls>") {
  return ExtensionTestUtils.loadExtension({
    manifest: {
      permissions: ["webRequest", "webRequestBlocking", permission],
    },
    background() {
      browser.webRequest.onBeforeSendHeaders.addListener(
        details => {
          details.requestHeaders.push({ name: "Host", value: "example.org" });
          return { requestHeaders: details.requestHeaders };
        },
        { urls: ["<all_urls>"] },
        ["blocking", "requestHeaders"]
      );
    },
  });
}

add_task(async function test_host_header_accepted() {
  let extension = getExtension();
  await extension.startup();
  let headers = JSON.parse(
    await ExtensionTestUtils.fetch(
      FETCH_ORIGIN,
      `${BASE_URL}/return_headers.sjs`
    )
  );

  equal(headers.host, "example.org", "Host header was set on request");

  await extension.unload();
});

add_task(async function test_host_header_denied() {
  let extension = getExtension(`${BASE_URL}/`);

  await extension.startup();

  let headers = JSON.parse(
    await ExtensionTestUtils.fetch(
      FETCH_ORIGIN,
      `${BASE_URL}/return_headers.sjs`
    )
  );

  equal(headers.host, "example.com", "Host header was not set on request");

  await extension.unload();
});

add_task(async function test_host_header_guarded() {
  const { setEnterpriseGuards } = ChromeUtils.importESModule(
    "resource://gre/modules/ExtensionPermissions.sys.mjs"
  );
  setEnterpriseGuards({
    "*": { runtime_blocked_hosts: ["*://example.org/*"] },
  });

  let extension = getExtension();
  await extension.startup();

  let { messages, result } = await promiseConsoleOutput(() =>
    ExtensionTestUtils.fetch(FETCH_ORIGIN, `${BASE_URL}/return_headers.sjs`)
  );
  equal(JSON.parse(result).host, "example.com", "Host header was not set");
  AddonTestUtils.checkMessages(messages, {
    expected: [{ message: /Unable to set host header to guarded/ }],
  });

  await extension.unload();
  setEnterpriseGuards({});
});

add_task(async function test_host_header_restricted() {
  Services.prefs.setCharPref(
    "extensions.webextensions.restrictedDomains",
    "example.org"
  );
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("extensions.webextensions.restrictedDomains");
  });

  let extension = getExtension();

  await extension.startup();

  let headers = JSON.parse(
    await ExtensionTestUtils.fetch(
      FETCH_ORIGIN,
      `${BASE_URL}/return_headers.sjs`
    )
  );

  equal(headers.host, "example.com", "Host header was not set on request");

  await extension.unload();
});
