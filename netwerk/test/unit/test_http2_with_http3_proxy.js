// test HTTP/2 with a HTTP/3 prooxy

"use strict";

/* import-globals-from http2_test_common.js */

const { Http3ProxyFilter } = ChromeUtils.importESModule(
  "resource://testing-common/NodeServer.sys.mjs"
);

let concurrent_channels = [];
let loadGroup;
let serverPort;
let pps = Cc["@mozilla.org/network/protocol-proxy-service;1"].getService();
let proxyHost;
let proxyPort;
let proxyFilter;

add_setup(async function setup() {
  do_get_profile();
  serverPort = await start_http2_test_server();

  let certdb = Cc["@mozilla.org/security/x509certdb;1"].getService(
    Ci.nsIX509CertDB
  );
  addCertFromFile(certdb, "proxy-ca.pem", "CTu,u,u");

  Services.prefs.setBoolPref("network.proxy.allow_hijacking_localhost", true);
  Services.prefs.setBoolPref("network.http.http3.enable", true);
  Services.prefs.setBoolPref("network.http.http2.enabled", true);
  Services.prefs.setBoolPref("network.http.altsvc.enabled", true);
  Services.prefs.setCharPref(
    "network.dns.localDomains",
    "foo.example.com, alt1.example.com, bar.example.com"
  );
  Services.prefs.setBoolPref(
    "network.cookieJarSettings.unblocked_for_testing",
    true
  );

  loadGroup = Cc["@mozilla.org/network/load-group;1"].createInstance(
    Ci.nsILoadGroup
  );

  Services.prefs.setStringPref(
    "services.settings.server",
    `data:,#remote-settings-dummy/v1`
  );

  proxyHost = "alt1.example.com";
  proxyPort = (await create_masque_proxy_server()).masqueProxyPort;
  Assert.notEqual(proxyPort, null);
  Assert.notEqual(proxyPort, "");
  proxyFilter = new Http3ProxyFilter(
    proxyHost,
    proxyPort,
    0,
    "/.well-known/masque/udp/{target_host}/{target_port}/",
    ""
  );
  pps.registerFilter(proxyFilter, 10);

  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("network.http.http3.enable");
    Services.prefs.clearUserPref("network.dns.localDomains");
    Services.prefs.clearUserPref("network.proxy.allow_hijacking_localhost");
    Services.prefs.clearUserPref(
      "network.http.http3.alt-svc-mapping-for-testing"
    );
    pps.unregisterFilter(proxyFilter);
  });
});

// hack - the header test resets the multiplex object on the server,
// so make sure header is always run before the multiplex test.
//
// make sure post_big runs first to test race condition in restarting
// a stalled stream when a SETTINGS frame arrives
add_task(async function do_test_http2_post_big() {
  const { httpProxyConnectResponseCode } = await test_http2_post_big(
    serverPort,
    "foo.example.com"
  );
  Assert.equal(httpProxyConnectResponseCode, 200);
});

add_task(async function do_test_http2_basic() {
  const { httpProxyConnectResponseCode } = await test_http2_basic(
    serverPort,
    "foo.example.com"
  );
  Assert.equal(httpProxyConnectResponseCode, 200);
});

add_task(async function do_test_http2_concurrent() {
  const { httpProxyConnectResponseCode } = await test_http2_concurrent(
    concurrent_channels,
    serverPort,
    "foo.example.com"
  );
  Assert.equal(httpProxyConnectResponseCode, 200);
});

add_task(async function do_test_http2_concurrent_post() {
  const { httpProxyConnectResponseCode } = await test_http2_concurrent_post(
    concurrent_channels,
    serverPort,
    "foo.example.com"
  );
  Assert.equal(httpProxyConnectResponseCode, 200);
});

add_task(async function do_test_http2_basic_unblocked_dep() {
  const { httpProxyConnectResponseCode } = await test_http2_basic_unblocked_dep(
    serverPort,
    "foo.example.com"
  );
  Assert.equal(httpProxyConnectResponseCode, 200);
});

add_task(async function do_test_http2_doubleheader() {
  const { httpProxyConnectResponseCode } = await test_http2_doubleheader(
    serverPort,
    "foo.example.com"
  );
  Assert.equal(httpProxyConnectResponseCode, 200);
});

add_task(async function do_test_http2_xhr() {
  await test_http2_xhr(serverPort, "foo.example.com");
});

add_task(async function do_test_http2_header() {
  const { httpProxyConnectResponseCode } = await test_http2_header(
    serverPort,
    "foo.example.com"
  );
  Assert.equal(httpProxyConnectResponseCode, 200);
});

add_task(async function do_test_http2_invalid_response_header_name_spaces() {
  const { httpProxyConnectResponseCode } =
    await test_http2_invalid_response_header(
      serverPort,
      "name_spaces",
      "foo.example.com"
    );
  Assert.equal(httpProxyConnectResponseCode, 200);
});

add_task(
  async function do_test_http2_invalid_response_header_value_line_feed() {
    const { httpProxyConnectResponseCode } =
      await test_http2_invalid_response_header(
        serverPort,
        "value_line_feed",
        "foo.example.com"
      );
    Assert.equal(httpProxyConnectResponseCode, 200);
  }
);

add_task(
  async function do_test_http2_invalid_response_header_value_carriage_return() {
    const { httpProxyConnectResponseCode } =
      await test_http2_invalid_response_header(
        serverPort,
        "value_carriage_return",
        "foo.example.com"
      );
    Assert.equal(httpProxyConnectResponseCode, 200);
  }
);

add_task(async function do_test_http2_invalid_response_header_value_null() {
  const { httpProxyConnectResponseCode } =
    await test_http2_invalid_response_header(
      serverPort,
      "value_null",
      "foo.example.com"
    );
  Assert.equal(httpProxyConnectResponseCode, 200);
});

add_task(async function do_test_http2_cookie_crumbling() {
  const { httpProxyConnectResponseCode } = await test_http2_cookie_crumbling(
    serverPort,
    "foo.example.com"
  );
  Assert.equal(httpProxyConnectResponseCode, 200);
});

add_task(async function do_test_http2_multiplex() {
  var values = await test_http2_multiplex(serverPort, "foo.example.com");
  Assert.equal(values[0].httpProxyConnectResponseCode, 200);
  Assert.equal(values[1].httpProxyConnectResponseCode, 200);
  Assert.notEqual(values[0].streamID, values[1].streamID);
}).skip(); // TODO: bug 1998062

add_task(async function do_test_http2_big() {
  const { httpProxyConnectResponseCode } = await test_http2_big(
    serverPort,
    "foo.example.com"
  );
  Assert.equal(httpProxyConnectResponseCode, 200);
});

add_task(async function do_test_http2_huge_suspended() {
  const { httpProxyConnectResponseCode } = await test_http2_huge_suspended(
    serverPort,
    "foo.example.com"
  );
  Assert.equal(httpProxyConnectResponseCode, 200);
});

add_task(async function do_test_http2_post() {
  const { httpProxyConnectResponseCode } = await test_http2_post(
    serverPort,
    "foo.example.com"
  );
  Assert.equal(httpProxyConnectResponseCode, 200);
});

add_task(async function do_test_http2_empty_post() {
  const { httpProxyConnectResponseCode } = await test_http2_empty_post(
    serverPort,
    "foo.example.com"
  );
  Assert.equal(httpProxyConnectResponseCode, 200);
});

add_task(async function do_test_http2_patch() {
  const { httpProxyConnectResponseCode } = await test_http2_patch(
    serverPort,
    "foo.example.com"
  );
  Assert.equal(httpProxyConnectResponseCode, 200);
});

add_task(async function do_test_http2_blocking_download() {
  const { httpProxyConnectResponseCode } = await test_http2_blocking_download(
    serverPort,
    "foo.example.com"
  );
  Assert.equal(httpProxyConnectResponseCode, 200);
});

add_task(async function do_test_http2_illegalhpacksoft() {
  const { httpProxyConnectResponseCode } = await test_http2_illegalhpacksoft(
    serverPort,
    "foo.example.com"
  );
  Assert.equal(httpProxyConnectResponseCode, 200);
});

add_task(async function do_test_http2_illegalhpackhard() {
  const { httpProxyConnectResponseCode } = await test_http2_illegalhpackhard(
    serverPort,
    "foo.example.com"
  );
  Assert.equal(httpProxyConnectResponseCode, 200);
});

add_task(async function do_test_http2_folded_header() {
  const { httpProxyConnectResponseCode } = await test_http2_folded_header(
    loadGroup,
    serverPort,
    "foo.example.com"
  );
  Assert.equal(httpProxyConnectResponseCode, 200);
});

add_task(async function do_test_http2_empty_data() {
  const { httpProxyConnectResponseCode } = await test_http2_empty_data(
    serverPort,
    "foo.example.com"
  );
  Assert.equal(httpProxyConnectResponseCode, 200);
});

add_task(async function do_test_http2_continuation_stream_zero() {
  const { httpProxyConnectResponseCode } =
    await test_http2_continuation_stream_zero(serverPort, "foo.example.com");
  Assert.equal(httpProxyConnectResponseCode, 200);
});

add_task(async function do_test_http2_status_phrase() {
  const { httpProxyConnectResponseCode } = await test_http2_status_phrase(
    serverPort,
    "foo.example.com"
  );
  Assert.equal(httpProxyConnectResponseCode, 200);
});

add_task(async function do_test_http2_h11required_stream() {
  // Add new tests above here - best to add new tests before h1
  // streams get too involved
  // These next two must always come in this order
  const { httpProxyConnectResponseCode } = await test_http2_h11required_stream(
    serverPort,
    "foo.example.com"
  );
  Assert.equal(httpProxyConnectResponseCode, 200);
});

add_task(async function do_test_http2_h11required_session() {
  const { httpProxyConnectResponseCode } = await test_http2_h11required_session(
    serverPort,
    "foo.example.com"
  );
  Assert.equal(httpProxyConnectResponseCode, 200);
});

add_task(async function do_test_http2_retry_rst() {
  const { httpProxyConnectResponseCode } = await test_http2_retry_rst(
    serverPort,
    "foo.example.com"
  );
  Assert.equal(httpProxyConnectResponseCode, 200);
});

add_task(async function do_test_http2_wrongsuite_tls13() {
  const { httpProxyConnectResponseCode } = await test_http2_wrongsuite_tls13(
    serverPort,
    "foo.example.com"
  );
  Assert.equal(httpProxyConnectResponseCode, 200);
});

add_task(async function do_test_http2_continuations_over_max_response_limit() {
  const { httpProxyConnectResponseCode } =
    await test_http2_continuations_over_max_response_limit(
      loadGroup,
      serverPort,
      "foo.example.com"
    );
  Assert.equal(httpProxyConnectResponseCode, 200);
});
