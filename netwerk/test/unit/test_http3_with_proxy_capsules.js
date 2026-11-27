"use strict";

/* import-globals-from http3_common.js */

// Similar to test_http3_with_proxy.js, but disables QUIC DATAGRAM support in
// the MASQUE proxy, so the inner connection's packets travel as HTTP Datagram
// capsules.

let h3Route;
let httpsOrigin;
let h3AltSvc;
let h3ServerDomain;

add_setup(async function () {
  ({ h3Route, httpsOrigin, h3AltSvc, h3ServerDomain } =
    await setup_http3_with_proxy({ MOZ_HTTP3_MASQUE_CAPSULES_ONLY: "1" }));
});

add_task(async function test_https_alt_svc() {
  await waitForHttp3Route(httpsOrigin + "http3-test", h3Route, h3AltSvc, {
    delayMs: 500,
  });
});

// Uploads enough data through the connect-udp tunnel to exhaust the request
// stream's flow-control window.
add_task(async function test_large_post_backpressure_capsules() {
  await do_test_large_post_backpressure(httpsOrigin, h3Route, h3ServerDomain);
});
