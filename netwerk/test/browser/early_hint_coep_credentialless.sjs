"use strict";

const COOKIE_NAME = "eh_coep_cookie";
const SCRIPT_URL =
  "https://test1.example.com/browser/netwerk/test/browser/early_hint_coep_credentialless.sjs?mode=script";

function handleRequest(request, response) {
  let qs = new URLSearchParams(request.queryString);
  let mode = qs.get("mode");

  if (mode === "reset") {
    setSharedState("ehCoepCount", JSON.stringify({ hinted: 0, normal: 0 }));
    response.setHeader("Content-Type", "application/json", false);
    response.write("{}");
    return;
  }

  if (mode === "count") {
    response.setHeader("Content-Type", "application/json", false);
    response.write(getSharedState("ehCoepCount"));
    return;
  }

  if (mode === "setcookie") {
    response.setHeader(
      "Set-Cookie",
      `${COOKIE_NAME}=1; Secure; SameSite=None; Path=/`,
      false
    );
    response.setHeader("Content-Type", "text/plain", false);
    response.write("cookie set");
    return;
  }

  if (mode === "script") {
    let hinted =
      request.hasHeader("X-Moz") && request.getHeader("X-Moz") === "early hint";
    let count = JSON.parse(getSharedState("ehCoepCount"));
    count[hinted ? "hinted" : "normal"] += 1;
    setSharedState("ehCoepCount", JSON.stringify(count));

    let hasCookie =
      request.hasHeader("Cookie") &&
      request.getHeader("Cookie").includes(COOKIE_NAME);
    response.setHeader("Content-Type", "application/javascript", false);
    response.setHeader("Cache-Control", "no-store", false);
    response.write(
      `window.scriptResult = "${hasCookie ? "COOKIE" : "NO_COOKIE"}";`
    );
    return;
  }

  // The main document, optionally sending a 103 preloading the cross-origin
  // script and a COEP: credentialless header in the final response.
  let hinted = qs.get("hinted") === "1";
  let coep = qs.get("coep") === "1";
  let body = `<!DOCTYPE html>
    <html>
    <head>
    <script src="${SCRIPT_URL}"></script>
    </head>
    <body></body>
    </html>`;

  response.seizePower();
  if (hinted) {
    response.write("HTTP/1.1 103 Early Hint\r\n");
    response.write(`Link: <${SCRIPT_URL}>; rel=preload; as=script\r\n`);
    response.write("\r\n");
  }
  response.write("HTTP/1.1 200 OK\r\n");
  if (coep) {
    response.write("Cross-Origin-Embedder-Policy: credentialless\r\n");
  }
  response.write("Content-Type: text/html;charset=utf-8\r\n");
  response.write("Cache-Control: no-cache\r\n");
  response.write(`Content-Length: ${body.length}\r\n`);
  response.write("\r\n");
  response.write(body);
  response.finish();
}
