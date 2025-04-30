const { NetUtil } = ChromeUtils.importESModule(
  "resource://gre/modules/NetUtil.sys.mjs"
);

const BASE_HOST = "example.org";

const { CookieXPCShellUtils } = ChromeUtils.importESModule(
  "resource://testing-common/CookieXPCShellUtils.sys.mjs"
);

CookieXPCShellUtils.init(this);
CookieXPCShellUtils.createServer({ hosts: ["example.org"] });

add_task(async function test_basic_eviction() {
  do_get_profile();

  Services.prefs.setIntPref("network.cookie.staleThreshold", 0);
  Services.prefs.setIntPref("network.cookie.quotaPerHost", 3);
  Services.prefs.setIntPref("network.cookie.maxPerHost", 5);

  // We don't want to have CookieJarSettings blocking this test.
  Services.prefs.setBoolPref(
    "network.cookieJarSettings.unblocked_for_testing",
    true
  );

  const BASE_URI = Services.io.newURI("http://" + BASE_HOST);
  const FOO_PATH = Services.io.newURI("http://" + BASE_HOST + "/foo/");
  const BAR_PATH = Services.io.newURI("http://" + BASE_HOST + "/bar/");

  await setCookie("session_foo_path_1", null, "/foo", null, FOO_PATH);
  await setCookie("session_foo_path_2", null, "/foo", null, FOO_PATH);
  await setCookie("session_foo_path_3", null, "/foo", null, FOO_PATH);
  await setCookie("session_foo_path_4", null, "/foo", null, FOO_PATH);
  await setCookie("session_foo_path_5", null, "/foo", null, FOO_PATH);
  verifyCookies(
    [
      "session_foo_path_1",
      "session_foo_path_2",
      "session_foo_path_3",
      "session_foo_path_4",
      "session_foo_path_5",
    ],
    BASE_URI
  );

  // Check if cookies are evicted by creation time.
  await setCookie("session_foo_path_6", null, "/foo", null, FOO_PATH);
  verifyCookies(
    ["session_foo_path_4", "session_foo_path_5", "session_foo_path_6"],
    BASE_URI
  );

  await setCookie("session_bar_path_1", null, "/bar", null, BAR_PATH);
  await setCookie("session_bar_path_2", null, "/bar", null, BAR_PATH);

  verifyCookies(
    [
      "session_foo_path_4",
      "session_foo_path_5",
      "session_foo_path_6",
      "session_bar_path_1",
      "session_bar_path_2",
    ],
    BASE_URI
  );

  // Check if cookies are evicted by last accessed time.
  await CookieXPCShellUtils.getCookieStringFromDocument(FOO_PATH.spec);

  await setCookie("session_foo_path_7", null, "/foo", null, FOO_PATH);
  verifyCookies(
    ["session_foo_path_5", "session_foo_path_6", "session_foo_path_7"],
    BASE_URI
  );

  const EXPIRED_TIME = 3;

  await setCookie(
    "non_session_expired_foo_path_1",
    null,
    "/foo",
    EXPIRED_TIME,
    FOO_PATH
  );
  await setCookie(
    "non_session_expired_foo_path_2",
    null,
    "/foo",
    EXPIRED_TIME,
    FOO_PATH
  );
  verifyCookies(
    [
      "session_foo_path_5",
      "session_foo_path_6",
      "session_foo_path_7",
      "non_session_expired_foo_path_1",
      "non_session_expired_foo_path_2",
    ],
    BASE_URI
  );

  // Check if expired cookies are evicted first.
  await new Promise(resolve => do_timeout(EXPIRED_TIME * 1000, resolve));
  await setCookie("session_foo_path_8", null, "/foo", null, FOO_PATH);
  verifyCookies(
    ["session_foo_path_6", "session_foo_path_7", "session_foo_path_8"],
    BASE_URI
  );

  Services.cookies.removeAll();
});

// Verify that the given cookie names exist, and are ordered from least to most recently accessed
function verifyCookies(names, uri) {
  Assert.equal(Services.cookies.countCookiesFromHost(uri.host), names.length);
  let actual_cookies = [];
  for (let cookie of Services.cookies.getCookiesFromHost(uri.host, {})) {
    actual_cookies.push(cookie);
  }
  if (names.length != actual_cookies.length) {
    let left = names.filter(function (n) {
      return (
        actual_cookies.findIndex(function (c) {
          return c.name == n;
        }) == -1
      );
    });
    let right = actual_cookies
      .filter(function (c) {
        return (
          names.findIndex(function (n) {
            return c.name == n;
          }) == -1
        );
      })
      .map(function (c) {
        return c.name;
      });
    if (left.length) {
      info("unexpected cookies: " + left);
    }
    if (right.length) {
      info("expected cookies: " + right);
    }
  }
  Assert.equal(names.length, actual_cookies.length);
  actual_cookies.sort(function (a, b) {
    if (a.lastAccessed < b.lastAccessed) {
      return -1;
    }
    if (a.lastAccessed > b.lastAccessed) {
      return 1;
    }
    return 0;
  });
  for (var i = 0; i < names.length; i++) {
    Assert.equal(names[i], actual_cookies[i].name);
    Assert.equal(names[i].startsWith("session"), actual_cookies[i].isSession);
  }
}

var lastValue = 0;
function setCookie(name, domain, path, maxAge, url) {
  let value = name + "=" + ++lastValue;
  var s = "setting cookie " + value;
  if (domain) {
    value += "; Domain=" + domain;
    s += " (d=" + domain + ")";
  }
  if (path) {
    value += "; Path=" + path;
    s += " (p=" + path + ")";
  }
  if (maxAge) {
    value += "; Max-Age=" + maxAge;
    s += " (non-session)";
  } else {
    s += " (session)";
  }
  s += " for " + url.spec;
  info(s);

  let channel = NetUtil.newChannel({
    uri: url,
    loadUsingSystemPrincipal: true,
    contentPolicyType: Ci.nsIContentPolicy.TYPE_DOCUMENT,
  });

  Services.cookies.setCookieStringFromHttp(url, value, channel);

  return new Promise(function (resolve) {
    // Windows XP has low precision timestamps that cause our cookie eviction
    // algorithm to produce different results from other platforms. We work around
    // this by ensuring that there's a clear gap between each cookie update.
    do_timeout(10, resolve);
  });
}
