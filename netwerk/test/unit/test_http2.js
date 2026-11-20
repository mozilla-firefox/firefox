/* import-globals-from http2_test_common.js */

var concurrent_channels = [];

var loadGroup;
var serverPort;

add_setup(async function setup() {
  do_get_profile();
  serverPort = await start_http2_test_server();

  Services.prefs.setIntPref("network.http.speculative-parallel-limit", 0);

  Services.prefs.setBoolPref("network.http.http2.enabled", true);
  Services.prefs.setBoolPref("network.http.altsvc.enabled", true);
  Services.prefs.setCharPref(
    "network.dns.localDomains",
    "foo.example.com, bar.example.com"
  );
  Services.prefs.setBoolPref(
    "network.cookieJarSettings.unblocked_for_testing",
    true
  );

  loadGroup = Cc["@mozilla.org/network/load-group;1"].createInstance(
    Ci.nsILoadGroup
  );
});

registerCleanupFunction(() => {
  Services.prefs.clearUserPref("network.http.speculative-parallel-limit");
  Services.prefs.clearUserPref("network.http.http2.enabled");
  Services.prefs.clearUserPref("network.http.altsvc.enabled");
  Services.prefs.clearUserPref("network.dns.localDomains");
  Services.prefs.clearUserPref(
    "network.cookieJarSettings.unblocked_for_testing"
  );
});

// hack - the header test resets the multiplex object on the server,
// so make sure header is always run before the multiplex test.
//
// make sure post_big runs first to test race condition in restarting
// a stalled stream when a SETTINGS frame arrives
add_task(async function do_test_http2_post_big() {
  const { httpProxyConnectResponseCode } =
    await test_http2_post_big(serverPort);
  Assert.equal(httpProxyConnectResponseCode, -1);
});

add_task(async function do_test_http2_basic() {
  const { httpProxyConnectResponseCode } = await test_http2_basic(serverPort);
  Assert.equal(httpProxyConnectResponseCode, -1);
});

add_task(async function do_test_http2_concurrent() {
  const { httpProxyConnectResponseCode } = await test_http2_concurrent(
    concurrent_channels,
    serverPort
  );
  Assert.equal(httpProxyConnectResponseCode, -1);
});

add_task(async function do_test_http2_concurrent_post() {
  const { httpProxyConnectResponseCode } = await test_http2_concurrent_post(
    concurrent_channels,
    serverPort
  );
  Assert.equal(httpProxyConnectResponseCode, -1);
});

add_task(async function do_test_http2_basic_unblocked_dep() {
  const { httpProxyConnectResponseCode } =
    await test_http2_basic_unblocked_dep(serverPort);
  Assert.equal(httpProxyConnectResponseCode, -1);
});

add_task(async function do_test_http2_nospdy() {
  const { httpProxyConnectResponseCode } = await test_http2_nospdy(serverPort);
  Assert.equal(httpProxyConnectResponseCode, -1);
});

add_task(async function do_test_http2_doubleheader() {
  const { httpProxyConnectResponseCode } =
    await test_http2_doubleheader(serverPort);
  Assert.equal(httpProxyConnectResponseCode, -1);
});

add_task(async function do_test_http2_xhr() {
  await test_http2_xhr(serverPort);
});

add_task(async function do_test_http2_header() {
  const { httpProxyConnectResponseCode } = await test_http2_header(serverPort);
  Assert.equal(httpProxyConnectResponseCode, -1);
});

add_task(async function do_test_http2_invalid_response_header_name_spaces() {
  const { httpProxyConnectResponseCode } =
    await test_http2_invalid_response_header(serverPort, "name_spaces");
  Assert.equal(httpProxyConnectResponseCode, -1);
});

add_task(
  async function do_test_http2_invalid_response_header_value_line_feed() {
    const { httpProxyConnectResponseCode } =
      await test_http2_invalid_response_header(serverPort, "value_line_feed");
    Assert.equal(httpProxyConnectResponseCode, -1);
  }
);

add_task(
  async function do_test_http2_invalid_response_header_value_carriage_return() {
    const { httpProxyConnectResponseCode } =
      await test_http2_invalid_response_header(
        serverPort,
        "value_carriage_return"
      );
    Assert.equal(httpProxyConnectResponseCode, -1);
  }
);

add_task(async function do_test_http2_invalid_response_header_value_null() {
  const { httpProxyConnectResponseCode } =
    await test_http2_invalid_response_header(serverPort, "value_null");
  Assert.equal(httpProxyConnectResponseCode, -1);
});

add_task(async function do_test_http2_cookie_crumbling() {
  const { httpProxyConnectResponseCode } =
    await test_http2_cookie_crumbling(serverPort);
  Assert.equal(httpProxyConnectResponseCode, -1);
});

add_task(async function do_test_http2_multiplex() {
  var values = await test_http2_multiplex(serverPort);
  Assert.equal(values[0].httpProxyConnectResponseCode, -1);
  Assert.equal(values[1].httpProxyConnectResponseCode, -1);
  Assert.notEqual(values[0].streamID, values[1].streamID);
});

add_task(async function do_test_http2_big() {
  const { httpProxyConnectResponseCode } = await test_http2_big(serverPort);
  Assert.equal(httpProxyConnectResponseCode, -1);
});

add_task(async function do_test_http2_huge_suspended() {
  const { httpProxyConnectResponseCode } =
    await test_http2_huge_suspended(serverPort);
  Assert.equal(httpProxyConnectResponseCode, -1);
});

add_task(async function do_test_http2_post() {
  const { httpProxyConnectResponseCode } = await test_http2_post(serverPort);
  Assert.equal(httpProxyConnectResponseCode, -1);
});

add_task(async function do_test_http2_empty_post() {
  const { httpProxyConnectResponseCode } =
    await test_http2_empty_post(serverPort);
  Assert.equal(httpProxyConnectResponseCode, -1);
});

add_task(async function do_test_http2_patch() {
  const { httpProxyConnectResponseCode } = await test_http2_patch(serverPort);
  Assert.equal(httpProxyConnectResponseCode, -1);
});

add_task(async function do_test_http2_blocking_download() {
  const { httpProxyConnectResponseCode } =
    await test_http2_blocking_download(serverPort);
  Assert.equal(httpProxyConnectResponseCode, -1);
});

add_task(async function do_test_http2_illegalhpacksoft() {
  const { httpProxyConnectResponseCode } =
    await test_http2_illegalhpacksoft(serverPort);
  Assert.equal(httpProxyConnectResponseCode, -1);
});

add_task(async function do_test_http2_illegalhpackhard() {
  const { httpProxyConnectResponseCode } =
    await test_http2_illegalhpackhard(serverPort);
  Assert.equal(httpProxyConnectResponseCode, -1);
});

add_task(async function do_test_http2_folded_header() {
  const { httpProxyConnectResponseCode } = await test_http2_folded_header(
    loadGroup,
    serverPort
  );
  Assert.equal(httpProxyConnectResponseCode, -1);
});

add_task(async function do_test_http2_empty_data() {
  const { httpProxyConnectResponseCode } =
    await test_http2_empty_data(serverPort);
  Assert.equal(httpProxyConnectResponseCode, -1);
});

add_task(async function do_test_http2_continuation_stream_zero() {
  const { httpProxyConnectResponseCode } =
    await test_http2_continuation_stream_zero(serverPort);
  Assert.equal(httpProxyConnectResponseCode, -1);
});

add_task(async function do_test_http2_status_phrase() {
  const { httpProxyConnectResponseCode } =
    await test_http2_status_phrase(serverPort);
  Assert.equal(httpProxyConnectResponseCode, -1);
});

add_task(async function do_test_http2_h11required_stream() {
  // Add new tests above here - best to add new tests before h1
  // streams get too involved
  // These next two must always come in this order
  const { httpProxyConnectResponseCode } =
    await test_http2_h11required_stream(serverPort);
  Assert.equal(httpProxyConnectResponseCode, -1);
});

add_task(async function do_test_http2_h11required_session() {
  const { httpProxyConnectResponseCode } =
    await test_http2_h11required_session(serverPort);
  Assert.equal(httpProxyConnectResponseCode, -1);
});

add_task(async function do_test_http2_retry_rst() {
  const { httpProxyConnectResponseCode } =
    await test_http2_retry_rst(serverPort);
  Assert.equal(httpProxyConnectResponseCode, -1);
});

add_task(async function do_test_http2_unknown_rst() {
  const { httpProxyConnectResponseCode } =
    await test_http2_unknown_rst(serverPort);
  Assert.equal(httpProxyConnectResponseCode, -1);
});

add_task(async function do_test_http2_wrongsuite_tls12() {
  const { httpProxyConnectResponseCode } =
    await test_http2_wrongsuite_tls12(serverPort);
  Assert.equal(httpProxyConnectResponseCode, -1);
});

add_task(async function do_test_http2_wrongsuite_tls13() {
  const { httpProxyConnectResponseCode } =
    await test_http2_wrongsuite_tls13(serverPort);
  Assert.equal(httpProxyConnectResponseCode, -1);
});

add_task(async function do_test_http2_continuations_over_max_response_limit() {
  const { httpProxyConnectResponseCode } =
    await test_http2_continuations_over_max_response_limit(
      loadGroup,
      serverPort
    );
  Assert.equal(httpProxyConnectResponseCode, -1);
});
