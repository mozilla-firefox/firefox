"use strict";

/* import-globals-from http3_common.js */

// Max concurent stream number in neqo is 100.
// Openning 120 streams will test queuing of streams.
let number_of_parallel_requests = 120;
let h3Route;
let httpsOrigin;
let h3AltSvc;
let h3ServerDomain;

add_setup(async function () {
  ({ h3Route, httpsOrigin, h3AltSvc, h3ServerDomain } =
    await setup_http3_with_proxy());
});

add_task(async function test_https_alt_svc() {
  await waitForHttp3Route(httpsOrigin + "http3-test", h3Route, h3AltSvc, {
    delayMs: 500,
  });
});

add_task(async function test_multiple_requests() {
  await do_test_multiple_requests(
    number_of_parallel_requests,
    h3Route,
    httpsOrigin
  );
});

add_task(async function test_request_cancelled_by_server() {
  await do_test_request_cancelled_by_server(h3Route, httpsOrigin);
});

add_task(async function test_stream_cancelled_by_necko() {
  await do_test_stream_cancelled_by_necko(h3Route, httpsOrigin);
});

add_task(async function test_multiple_request_one_is_cancelled() {
  await do_test_multiple_request_one_is_cancelled(
    number_of_parallel_requests,
    h3Route,
    httpsOrigin
  );
});

add_task(async function test_multiple_request_one_is_cancelled_by_necko() {
  await do_test_multiple_request_one_is_cancelled_by_necko(
    number_of_parallel_requests,
    h3Route,
    httpsOrigin
  );
});

add_task(async function test_post() {
  await do_test_post(httpsOrigin, h3Route);
});

add_task(async function test_patch() {
  await do_test_patch(httpsOrigin, h3Route);
});

add_task(async function test_slow_receiver() {
  await do_test_slow_receiver(httpsOrigin, h3Route);
});

// Uploads enough data through the connect-udp tunnel to fill the outer
// connection's datagram queue.
add_task(async function test_large_post_backpressure() {
  await do_test_large_post_backpressure(httpsOrigin, h3Route, h3ServerDomain);
});

// This test should be at the end, because it will close http3
// connection and the transaction will switch to already existing http2
// connection.
// TODO: Bug 1582667 should try to fix issue with connection being closed.
add_task(async function test_version_fallback() {
  await do_test_version_fallback(httpsOrigin);
});
