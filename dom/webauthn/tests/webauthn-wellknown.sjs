/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

// Serves the well-known response for WebAuthn related-origin tests.
// The response content is controlled by the shared state key "webauthn-related-origin-state",
// which is set by well-known-webauthn-state.sjs.

function handleRequest(request, response) {
  // When accessed directly as a redirect target (not via ^headers^), return a
  // valid well-known response without consulting shared state.
  if (request.queryString.includes("direct")) {
    response.setStatusLine(request.httpVersion, 200, "OK");
    response.setHeader("Content-Type", "application/json");
    response.write(JSON.stringify({ origins: ["https://example.com"] }));
    return;
  }

  // Intermediate HTTP hop for the http_to_https_redirect test: redirect back to
  // the HTTPS endpoint so the chain is https: -> http: -> https:.
  if (request.queryString.includes("http_to_https")) {
    response.setStatusLine(request.httpVersion, 302, "Found");
    response.setHeader(
      "Location",
      "https://example.org/tests/dom/webauthn/tests/webauthn-wellknown.sjs?direct"
    );
    return;
  }

  const state = getSharedState("webauthn-related-origin-state") || "valid";

  if (state === "timeout") {
    response.processAsync();
    // Never calls response.finish() — the XHR timeout closes the connection.
    return;
  }

  if (state === "not_found") {
    response.setStatusLine(request.httpVersion, 404, "Not Found");
    return;
  }

  if (state === "http_redirect") {
    response.setStatusLine(request.httpVersion, 302, "Found");
    response.setHeader(
      "Location",
      "http://example.org/tests/dom/webauthn/tests/webauthn-wellknown.sjs?direct" // eslint-disable-line @microsoft/sdl/no-insecure-url
    );
    return;
  }

  if (state === "http_to_https_redirect") {
    response.setStatusLine(request.httpVersion, 302, "Found");
    response.setHeader(
      "Location",
      "http://example.org/tests/dom/webauthn/tests/webauthn-wellknown.sjs?http_to_https" // eslint-disable-line @microsoft/sdl/no-insecure-url
    );
    return;
  }

  if (state === "https_same_origin_redirect") {
    response.setStatusLine(request.httpVersion, 302, "Found");
    response.setHeader(
      "Location",
      "https://example.org/tests/dom/webauthn/tests/webauthn-wellknown.sjs?direct"
    );
    return;
  }

  if (state === "https_cross_origin_redirect") {
    response.setStatusLine(request.httpVersion, 302, "Found");
    response.setHeader(
      "Location",
      "https://example.com/tests/dom/webauthn/tests/webauthn-wellknown.sjs?direct"
    );
    return;
  }

  response.setStatusLine(request.httpVersion, 200, "OK");

  if (state === "wrong_content_type") {
    response.setHeader("Content-Type", "text/plain");
    response.write(JSON.stringify({ origins: ["https://example.com"] }));
    return;
  }

  response.setHeader("Content-Type", "application/json");

  switch (state) {
    case "invalid_json":
      response.write("not valid json");
      break;
    case "origins_not_array":
      response.write(JSON.stringify({ origins: "https://example.com" }));
      break;
    case "origins_missing":
      response.write(JSON.stringify({ not_origins: [] }));
      break;
    case "caller_not_listed":
      response.write(JSON.stringify({ origins: ["https://example.net"] }));
      break;
    case "all_invalid_urls":
      response.write(
        JSON.stringify({
          origins: ["not-a-url", "javascript:alert(1)", "data:text/plain,x"],
        })
      );
      break;
    case "no_registrable_domain":
      // IP addresses, localhost, and bare TLDs have no registrable domain;
      // getBaseDomainFromHost throws for them (step 4.3 continue). Caller is
      // absent, so validation fails.
      response.write(
        JSON.stringify({
          origins: [
            "https://192.0.2.1/",
            "https://localhost/",
            "https://[::1]/",
            "https://com",
            "https://co.uk",
          ],
        })
      );
      break;
    case "mixed_no_registrable_domain":
      // Same entries as above but the valid caller origin is also present, so
      // the IP/localhost entries are skipped and validation succeeds.
      response.write(
        JSON.stringify({
          origins: [
            "https://192.0.2.1/",
            "https://localhost/",
            "https://[::1]/",
            "https://com",
            "https://co.uk",
            "https://example.com",
          ],
        })
      );
      break;
    case "max_labels_exceeded":
      // 6 distinct non-caller labels fill labelsSeen to the limit (5), so when
      // "https://example.com" is processed its label "example" is not in the
      // set and it gets skipped.
      response.write(
        JSON.stringify({
          origins: [
            "https://example1.com",
            "https://example2.com",
            "https://example3.com",
            "https://example4.com",
            "https://example5.com",
            "https://example6.com",
            "https://example.com",
          ],
        })
      );
      break;
    case "max_labels_exactly_five":
      // 4 distinct non-caller labels; caller occupies the 5th slot (labelsSeen
      // has room) so it is not skipped and the origin check succeeds.
      response.write(
        JSON.stringify({
          origins: [
            "https://example1.com",
            "https://example2.com",
            "https://example3.com",
            "https://example4.com",
            "https://example.com",
          ],
        })
      );
      break;
    case "label_seen_before_limit":
      // "sub.example.com" adds label "example" to labelsSeen early. After 4
      // more distinct labels fill labelsSeen to 5, "example5.com" is skipped
      // (new label), but "example.com" is not skipped because "example" is
      // already in labelsSeen.
      response.write(
        JSON.stringify({
          origins: [
            "https://sub.example.com",
            "https://example1.com",
            "https://example2.com",
            "https://example3.com",
            "https://example4.com",
            "https://example5.com",
            "https://example.com",
          ],
        })
      );
      break;
    case "mixed_valid_invalid":
      // Invalid URLs are skipped; the valid caller origin is found.
      response.write(
        JSON.stringify({
          origins: [
            "not-a-url",
            "javascript:alert(1)",
            "data:text/plain,x",
            "https://example.com",
            "invalid",
          ],
        })
      );
      break;
    case "multi_part_tld_label":
      // getBaseDomainFromHost("subdomain.example.co.uk") returns "example.co.uk";
      // slicing before the first dot gives label "example", not "example.co".
      // That label fills one labelsSeen slot. After 4 more labels the set is
      // full, but when "https://example.com" (also label "example") is
      // processed it is not skipped because "example" is already in labelsSeen.
      response.write(
        JSON.stringify({
          origins: [
            "https://subdomain.example.co.uk",
            "https://example1.com",
            "https://example2.com",
            "https://example3.com",
            "https://example4.com",
            "https://example.com",
          ],
        })
      );
      break;
    default:
      response.write(JSON.stringify({ origins: ["https://example.com"] }));
      break;
  }
}
