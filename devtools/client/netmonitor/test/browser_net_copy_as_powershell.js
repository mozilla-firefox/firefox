/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Test the Copy as PowerShell command
 */
add_task(async function () {
  const { tab, monitor } = await initNetMonitor(HTTPS_CURL_URL, {
    requestCount: 1,
  });

  info("Starting test... ");
  info("Test powershell command for GET request without any cookies");
  await performRequest("GET");
  await testClipboardContentForRecentRequest(`Invoke-WebRequest -UseBasicParsing -Uri "https://example.com/browser/devtools/client/netmonitor/test/sjs_simple-test-server.sjs" \`
-UserAgent "${navigator.userAgent}" \`
-Headers @{
"Accept" = "*/*"
  "Accept-Language" = "en-US"
  "Accept-Encoding" = "gzip, deflate, br, zstd"
  "X-Custom-Header-1" = "Custom value"
  "X-Custom-Header-2" = "8.8.8.8"
  "X-Custom-Header-3" = "Mon, 3 Mar 2014 11:11:11 GMT"
  "Referer" = "https://example.com/browser/devtools/client/netmonitor/test/html_copy-as-curl.html"
  "Sec-Fetch-Dest" = "empty"
  "Sec-Fetch-Mode" = "cors"
  "Sec-Fetch-Site" = "same-origin"
  "Priority" = "i"
  "Pragma" = "no-cache"
  "Cache-Control" = "no-cache"
}`);

  info("Test powershell command for GET request with cookies");
  await performRequest("GET");
  await testClipboardContentForRecentRequest(`$session = New-Object Microsoft.PowerShell.Commands.WebRequestSession
$session.Cookies.Add((New-Object System.Net.Cookie("bob", "true", "/", "example.com")))
$session.Cookies.Add((New-Object System.Net.Cookie("tom", "cool", "/", "example.com")))
Invoke-WebRequest -UseBasicParsing -Uri "https://example.com/browser/devtools/client/netmonitor/test/sjs_simple-test-server.sjs" \`
-WebSession $session \`
-UserAgent "${navigator.userAgent}" \`
-Headers @{
"Accept" = "*/*"
  "Accept-Language" = "en-US"
  "Accept-Encoding" = "gzip, deflate, br, zstd"
  "X-Custom-Header-1" = "Custom value"
  "X-Custom-Header-2" = "8.8.8.8"
  "X-Custom-Header-3" = "Mon, 3 Mar 2014 11:11:11 GMT"
  "Referer" = "https://example.com/browser/devtools/client/netmonitor/test/html_copy-as-curl.html"
  "Sec-Fetch-Dest" = "empty"
  "Sec-Fetch-Mode" = "cors"
  "Sec-Fetch-Site" = "same-origin"
  "Priority" = "i"
  "Pragma" = "no-cache"
  "Cache-Control" = "no-cache"
}`);

  // disable sending idempotency header for POST request
  await pushPref("network.http.idempotencyKey.enabled", false);
  info("Test powershell command for POST request with post body");
  await performRequest("POST", "Plaintext value as a payload");
  await testClipboardContentForRecentRequest(`$session = New-Object Microsoft.PowerShell.Commands.WebRequestSession
$session.Cookies.Add((New-Object System.Net.Cookie("bob", "true", "/", "example.com")))
$session.Cookies.Add((New-Object System.Net.Cookie("tom", "cool", "/", "example.com")))
Invoke-WebRequest -UseBasicParsing -Uri "https://example.com/browser/devtools/client/netmonitor/test/sjs_simple-test-server.sjs" \`
-Method "POST" \`
-WebSession $session \`
-UserAgent "${navigator.userAgent}" \`
-Headers @{
"Accept" = "*/*"
  "Accept-Language" = "en-US"
  "Accept-Encoding" = "gzip, deflate, br, zstd"
  "X-Custom-Header-1" = "Custom value"
  "X-Custom-Header-2" = "8.8.8.8"
  "X-Custom-Header-3" = "Mon, 3 Mar 2014 11:11:11 GMT"
  "Origin" = "https://example.com"
  "Referer" = "https://example.com/browser/devtools/client/netmonitor/test/html_copy-as-curl.html"
  "Sec-Fetch-Dest" = "empty"
  "Sec-Fetch-Mode" = "cors"
  "Sec-Fetch-Site" = "same-origin"
  "Priority" = "i"
  "Pragma" = "no-cache"
  "Cache-Control" = "no-cache"
} \`
-ContentType "text/plain;charset=UTF-8" \`
-Body "Plaintext value as a payload"`);

  info(
    "Test powershell command for POST request with post body which contains ASCII non printing characters"
  );
  await performRequest("POST", `TAB character included in payload \t`);
  await testClipboardContentForRecentRequest(`$session = New-Object Microsoft.PowerShell.Commands.WebRequestSession
$session.Cookies.Add((New-Object System.Net.Cookie("bob", "true", "/", "example.com")))
$session.Cookies.Add((New-Object System.Net.Cookie("tom", "cool", "/", "example.com")))
Invoke-WebRequest -UseBasicParsing -Uri "https://example.com/browser/devtools/client/netmonitor/test/sjs_simple-test-server.sjs" \`
-Method "POST" \`
-WebSession $session \`
-UserAgent "${navigator.userAgent}" \`
-Headers @{
"Accept" = "*/*"
  "Accept-Language" = "en-US"
  "Accept-Encoding" = "gzip, deflate, br, zstd"
  "X-Custom-Header-1" = "Custom value"
  "X-Custom-Header-2" = "8.8.8.8"
  "X-Custom-Header-3" = "Mon, 3 Mar 2014 11:11:11 GMT"
  "Origin" = "https://example.com"
  "Referer" = "https://example.com/browser/devtools/client/netmonitor/test/html_copy-as-curl.html"
  "Sec-Fetch-Dest" = "empty"
  "Sec-Fetch-Mode" = "cors"
  "Sec-Fetch-Site" = "same-origin"
  "Priority" = "i"
  "Pragma" = "no-cache"
  "Cache-Control" = "no-cache"
} \`
-ContentType "text/plain;charset=UTF-8" \`
-Body ([System.Text.Encoding]::UTF8.GetBytes("TAB character included in payload $([char]9)"))`);

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

  async function testClipboardContentForRecentRequest(expectedClipboardText) {
    const { document } = monitor.panelWin;

    const items = document.querySelectorAll(".request-list-item");
    clickOnRequestRow(items[items.length - 1]);
    EventUtils.sendMouseEvent(
      { type: "contextmenu" },
      document.querySelectorAll(".request-list-item")[0]
    );

    /* Ensure that the copy as fetch option is always visible */
    is(
      !!getContextMenuItem(monitor, "request-list-context-copy-as-powershell"),
      true,
      'The "Copy as PowerShell" context menu item should not be hidden on windows'
    );

    await waitForClipboardPromise(
      async function setup() {
        await selectContextMenuItem(
          monitor,
          "request-list-context-copy-as-powershell"
        );
      },
      function validate(result) {
        if (typeof result !== "string") {
          return false;
        }
        return expectedClipboardText == result;
      }
    );

    info(
      "Clipboard contains a powershell command for item " + (items.length - 1)
    );
  }
});
