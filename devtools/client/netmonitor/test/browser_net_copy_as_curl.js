/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests if Copy as cURL works.
 */

const POST_PAYLOAD = "Plaintext value as a payload";

add_task(async function () {
  const { tab, monitor } = await initNetMonitor(HTTPS_CURL_URL, {
    requestCount: 1,
  });
  // disable sending idempotency header for POST request
  Services.prefs.setBoolPref("network.http.idempotencyKey.enabled", false);
  info("Starting test... ");

  // Different quote chars are used for Windows and POSIX
  const QUOTE_WIN = '^"';
  const QUOTE_POSIX = "'";

  const isWin = Services.appinfo.OS === "WINNT";
  const testData = isWin
    ? [
        {
          menuItemId: "request-list-context-copy-as-curl-win",
          data: buildTestData(QUOTE_WIN),
        },
        {
          menuItemId: "request-list-context-copy-as-curl-posix",
          data: buildTestData(QUOTE_POSIX),
        },
      ]
    : [
        {
          menuItemId: "request-list-context-copy-as-curl",
          data: buildTestData(QUOTE_POSIX),
        },
      ];

  await testForPlatform(tab, monitor, testData);

  await teardown(monitor);
});

function buildTestData(QUOTE) {
  // Quote a string, escape the quotes inside the string
  function quote(str) {
    return QUOTE + str.replace(new RegExp(QUOTE, "g"), `\\${QUOTE}`) + QUOTE;
  }

  // Header param is formatted as -H "Header: value" or -H 'Header: value'
  function header(h) {
    return "-H " + quote(h);
  }

  // Construct the expected command
  const SIMPLE_BASE = ["curl " + quote(HTTPS_SIMPLE_SJS)];
  const SLOW_BASE = ["curl " + quote(HTTPS_SLOW_SJS)];
  const BASE_RESULT = [
    "--compressed",
    header("User-Agent: " + navigator.userAgent),
    header("Accept: */*"),
    header("Accept-Language: " + navigator.language),
    header("X-Custom-Header-1: Custom value"),
    header("X-Custom-Header-2: 8.8.8.8"),
    header("X-Custom-Header-3: Mon, 3 Mar 2014 11:11:11 GMT"),
    header("Referer: " + HTTPS_CURL_URL),
    header("Connection: keep-alive"),
    header("Pragma: no-cache"),
    header("Cache-Control: no-cache"),
    header("Sec-Fetch-Dest: empty"),
    header("Sec-Fetch-Mode: cors"),
    header("Sec-Fetch-Site: same-origin"),
  ];

  const COOKIE_PARTIAL_RESULT = [header("Cookie: bob=true; tom=cool")];

  const POST_PARTIAL_RESULT = [
    "-X",
    "POST",
    "--data-raw " + quote(POST_PAYLOAD),
    header("Content-Type: text/plain;charset=UTF-8"),
  ];
  const ORIGIN_RESULT = [header("Origin: https://example.com")];

  const HEAD_PARTIAL_RESULT = ["-I"];

  return {
    SIMPLE_BASE,
    SLOW_BASE,
    BASE_RESULT,
    COOKIE_PARTIAL_RESULT,
    POST_PAYLOAD,
    POST_PARTIAL_RESULT,
    ORIGIN_RESULT,
    HEAD_PARTIAL_RESULT,
  };
}

async function testForPlatform(tab, monitor, testData) {
  // GET request, no cookies (first request)
  await performRequest("GET");
  for (const test of testData) {
    await testClipboardContent(test.menuItemId, [
      ...test.data.SIMPLE_BASE,
      ...test.data.BASE_RESULT,
    ]);
  }
  // Check to make sure it is still OK after we view the response (bug#1452442)
  await selectIndexAndWaitForSourceEditor(monitor, 0);
  for (const test of testData) {
    await testClipboardContent(test.menuItemId, [
      ...test.data.SIMPLE_BASE,
      ...test.data.BASE_RESULT,
    ]);
  }

  // GET request, cookies set by previous response
  await performRequest("GET");
  for (const test of testData) {
    await testClipboardContent(test.menuItemId, [
      ...test.data.SIMPLE_BASE,
      ...test.data.BASE_RESULT,
      ...test.data.COOKIE_PARTIAL_RESULT,
    ]);
  }

  // Unfinished request (bug#1378464, bug#1420513)
  const waitSlow = waitForNetworkEvents(monitor, 0);
  await SpecialPowers.spawn(
    tab.linkedBrowser,
    [HTTPS_SLOW_SJS],
    async function (url) {
      content.wrappedJSObject.performRequest(url, "GET", null);
    }
  );
  await waitSlow;
  for (const test of testData) {
    await testClipboardContent(test.menuItemId, [
      ...test.data.SLOW_BASE,
      ...test.data.BASE_RESULT,
      ...test.data.COOKIE_PARTIAL_RESULT,
    ]);
  }

  // POST request
  await performRequest("POST", POST_PAYLOAD);
  for (const test of testData) {
    await testClipboardContent(test.menuItemId, [
      ...test.data.SIMPLE_BASE,
      ...test.data.BASE_RESULT,
      ...test.data.COOKIE_PARTIAL_RESULT,
      ...test.data.POST_PARTIAL_RESULT,
      ...test.data.ORIGIN_RESULT,
    ]);
  }

  // HEAD request
  await performRequest("HEAD");
  for (const test of testData) {
    await testClipboardContent(test.menuItemId, [
      ...test.data.SIMPLE_BASE,
      ...test.data.BASE_RESULT,
      ...test.data.COOKIE_PARTIAL_RESULT,
      ...test.data.HEAD_PARTIAL_RESULT,
    ]);
  }

  async function performRequest(method, payload) {
    const waitRequest = waitForNetworkEvents(monitor, 1);
    await SpecialPowers.spawn(
      tab.linkedBrowser,
      [
        {
          url: HTTPS_SIMPLE_SJS,
          method_: method,
          payload_: payload,
        },
      ],
      async function ({ url, method_, payload_ }) {
        content.wrappedJSObject.performRequest(url, method_, payload_);
      }
    );
    await waitRequest;
  }

  async function testClipboardContent(menuItemId, expectedResult) {
    const { document } = monitor.panelWin;

    const items = document.querySelectorAll(".request-list-item");
    const itemIndex = items.length - 1;
    EventUtils.sendMouseEvent({ type: "mousedown" }, items[itemIndex]);
    EventUtils.sendMouseEvent(
      { type: "contextmenu" },
      document.querySelectorAll(".request-list-item")[0]
    );

    /* Ensure that the copy as cURL option is always visible */
    is(
      !!getContextMenuItem(monitor, menuItemId),
      true,
      `The "Copy as cURL" context menu item "${menuItemId}" should not be hidden.`
    );

    await waitForClipboardPromise(
      async function setup() {
        await selectContextMenuItem(monitor, menuItemId);
      },
      function validate(result) {
        if (typeof result !== "string") {
          return false;
        }

        // Different setups may produce the same command, but with the
        // parameters in a different order in the commandline (which is fine).
        // Here we confirm that the commands are the same even in that case.

        // This monster regexp parses the command line into an array of arguments,
        // recognizing quoted args with matching quotes and escaped quotes inside:
        // [ "curl 'url'", "--standalone-arg", "-arg-with-quoted-string 'value\'s'" ]
        const matchRe = /[-A-Za-z1-9]+(?: ([\^\\"']+)(?:\\\1|.)*?\1)?/g;

        const actual = result.match(matchRe);
        // Must begin with the same "curl 'URL'" segment
        if (!actual || expectedResult[0] != actual[0]) {
          return false;
        }

        // Must match each of the params in the middle (headers)
        return (
          expectedResult.length === actual.length &&
          expectedResult.some(param => actual.includes(param))
        );
      }
    );

    info(
      `Clipboard contains a cURL command for item ${itemIndex} by "${menuItemId}"`
    );
  }
}
