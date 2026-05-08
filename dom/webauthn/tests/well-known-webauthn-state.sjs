/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

// Sets the shared state used by webauthn-wellknown.sjs.
// Call with ?state=<value> to configure the next well-known response.

function handleRequest(request, response) {
  let params = new URLSearchParams(request.queryString);
  let state = params.get("state") || "valid";
  setSharedState("webauthn-related-origin-state", state);
  response.setStatusLine(request.httpVersion, 200, "OK");
  response.setHeader("Content-Type", "text/plain");
  response.write("OK");
}
