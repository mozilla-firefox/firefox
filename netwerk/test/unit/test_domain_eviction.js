/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

// Test that domain eviction occurs when the cookies per base domain limit is
// reached, and that expired cookies are evicted before live cookies.

"use strict";

var test_generator = do_run_test();

function run_test() {
  do_test_pending();
  do_run_generator(test_generator);
}

function continue_test() {
  do_run_generator(test_generator);
}

function* do_run_test() {
  // Set quotaPerHost to maxPerHost, so there is only one cookie
  // will be evicted everytime.
  Services.prefs.setIntPref("network.cookie.quotaPerHost", 50);
  // Set the base domain limit to 50 so we have a known value.
  Services.prefs.setIntPref("network.cookie.maxPerHost", 50);

  let futureExpiry = Math.floor(Date.now() / 1000 + 1000);

  // test eviction under the 50 cookies per base domain limit. this means
  // that cookies for foo.com and bar.foo.com should count toward this limit,
  // while cookies for baz.com should not. there are several tests we perform
  // to make sure the base domain logic is working correctly.

  // 1) simplest case: set 100 cookies for "foo.bar" and make sure 50 survive.
  setCookies("foo.bar", 100, futureExpiry);
  Assert.equal(countCookies("foo.bar", "foo.bar"), 50);

  // 2) set cookies for different subdomains of "foo.baz", and an unrelated
  // domain, and make sure all 50 within the "foo.baz" base domain are counted.
  setCookies("foo.baz", 10, futureExpiry);
  setCookies(".foo.baz", 10, futureExpiry);
  setCookies("bar.foo.baz", 10, futureExpiry);
  setCookies("baz.bar.foo.baz", 10, futureExpiry);
  setCookies("unrelated.domain", 50, futureExpiry);
  Assert.equal(countCookies("foo.baz", "baz.bar.foo.baz"), 40);
  setCookies("foo.baz", 20, futureExpiry);
  Assert.equal(countCookies("foo.baz", "baz.bar.foo.baz"), 50);

  // 3) ensure cookies are evicted by order of lastAccessed time, if the
  // limit on cookies per base domain is reached.
  setCookies("horse.radish", 10, futureExpiry);

  // Wait a while, to make sure the first batch of cookies is older than
  // the second (timer resolution varies on different platforms).
  do_timeout(100, continue_test);
  yield;

  setCookies("tasty.horse.radish", 50, futureExpiry);
  Assert.equal(countCookies("horse.radish", "horse.radish"), 50);

  for (let cookie of Services.cookies.cookies) {
    if (cookie.host == "horse.radish") {
      do_throw("cookies not evicted by lastAccessed order");
    }
  }

  // Test that expired cookies for a domain are evicted before live ones.
  let shortExpiry = Math.floor(Date.now() / 1000 + 2);
  setCookies("captchart.com", 49, futureExpiry);
  Services.cookies.add(
    "captchart.com",
    "",
    "test100",
    "eviction",
    false,
    false,
    false,
    shortExpiry,
    {},
    Ci.nsICookie.SAMESITE_NONE,
    Ci.nsICookie.SCHEME_HTTPS
  );
  do_timeout(2100, continue_test);
  yield;

  Assert.equal(countCookies("captchart.com", "captchart.com"), 50);
  Services.cookies.add(
    "captchart.com",
    "",
    "test200",
    "eviction",
    false,
    false,
    false,
    futureExpiry,
    {},
    Ci.nsICookie.SAMESITE_NONE,
    Ci.nsICookie.SCHEME_HTTPS
  );
  Assert.equal(countCookies("captchart.com", "captchart.com"), 50);

  for (let cookie of Services.cookies.getCookiesFromHost("captchart.com", {})) {
    Assert.ok(cookie.expiry == futureExpiry);
  }

  do_finish_generator_test(test_generator);
}

// set 'aNumber' cookies with host 'aHost', with distinct names.
function setCookies(aHost, aNumber, aExpiry) {
  for (let i = 0; i < aNumber; ++i) {
    Services.cookies.add(
      aHost,
      "",
      "test" + i,
      "eviction",
      false,
      false,
      false,
      aExpiry,
      {},
      Ci.nsICookie.SAMESITE_NONE,
      Ci.nsICookie.SCHEME_HTTPS
    );
  }
}

// count how many cookies are within domain 'aBaseDomain', using three
// independent interface methods on nsICookieManager:
// 1) 'cookies', an array of all cookies;
// 2) 'countCookiesFromHost', which returns the number of cookies within the
//    base domain of 'aHost',
// 3) 'getCookiesFromHost', which returns an array of 2).
function countCookies(aBaseDomain, aHost) {
  // count how many cookies are within domain 'aBaseDomain' using the cookies
  // array.
  let cookies = [];
  for (let cookie of Services.cookies.cookies) {
    if (
      cookie.host.length >= aBaseDomain.length &&
      cookie.host.slice(cookie.host.length - aBaseDomain.length) == aBaseDomain
    ) {
      cookies.push(cookie);
    }
  }

  // confirm the count using countCookiesFromHost and getCookiesFromHost.
  let result = cookies.length;
  Assert.equal(
    Services.cookies.countCookiesFromHost(aBaseDomain),
    cookies.length
  );
  Assert.equal(Services.cookies.countCookiesFromHost(aHost), cookies.length);

  for (let cookie of Services.cookies.getCookiesFromHost(aHost, {})) {
    if (
      cookie.host.length >= aBaseDomain.length &&
      cookie.host.slice(cookie.host.length - aBaseDomain.length) == aBaseDomain
    ) {
      let found = false;
      for (let i = 0; i < cookies.length; ++i) {
        if (cookies[i].host == cookie.host && cookies[i].name == cookie.name) {
          found = true;
          cookies.splice(i, 1);
          break;
        }
      }

      if (!found) {
        do_throw("cookie " + cookie.name + " not found in master cookies");
      }
    } else {
      do_throw(
        "cookie host " + cookie.host + " not within domain " + aBaseDomain
      );
    }
  }

  Assert.equal(cookies.length, 0);

  return result;
}
