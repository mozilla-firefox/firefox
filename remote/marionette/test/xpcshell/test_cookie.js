/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

const { cookie } = ChromeUtils.importESModule(
  "chrome://remote/content/marionette/cookie.sys.mjs"
);

cookie.manager = {
  cookies: [],

  add(
    domain,
    path,
    name,
    value,
    secure,
    httpOnly,
    session,
    expiry,
    originAttributes,
    sameSite
  ) {
    if (name === "fail") {
      throw new Error("An error occurred while adding cookie");
    }
    let newCookie = {
      host: domain,
      path,
      name,
      value,
      isSecure: secure,
      isHttpOnly: httpOnly,
      isSession: session,
      expiry,
      originAttributes,
      sameSite,
    };
    cookie.manager.cookies.push(newCookie);
  },

  remove(host, name, path) {
    for (let i = 0; i < this.cookies.length; ++i) {
      let candidate = this.cookies[i];
      if (
        candidate.host === host &&
        candidate.name === name &&
        candidate.path === path
      ) {
        return this.cookies.splice(i, 1);
      }
    }
    return false;
  },

  getCookiesFromHost(host) {
    let hostCookies = this.cookies.filter(
      c => c.host === host || c.host === "." + host
    );

    return hostCookies;
  },
};

add_task(function test_fromJSON() {
  // object
  for (let invalidType of ["foo", 42, true, [], null, undefined]) {
    Assert.throws(
      () => cookie.fromJSON(invalidType),
      /Expected "cookie" to be an object/
    );
  }

  // name and value
  for (let invalidType of [42, true, [], {}, null, undefined]) {
    Assert.throws(
      () => cookie.fromJSON({ name: invalidType }),
      /Expected cookie "name" to be a string/
    );
    Assert.throws(
      () => cookie.fromJSON({ name: "foo", value: invalidType }),
      /Expected cookie "value" to be a string/
    );
  }

  // domain
  for (let invalidType of [42, true, [], {}, null]) {
    let domainTest = {
      name: "foo",
      value: "bar",
      domain: invalidType,
    };
    Assert.throws(
      () => cookie.fromJSON(domainTest),
      /Expected cookie "domain" to be a string/
    );
  }
  let domainTest = {
    name: "foo",
    value: "bar",
    domain: "domain",
  };
  let parsedCookie = cookie.fromJSON(domainTest);
  equal(parsedCookie.domain, "domain");

  // path
  for (let invalidType of [42, true, [], {}, null]) {
    let pathTest = {
      name: "foo",
      value: "bar",
      path: invalidType,
    };
    Assert.throws(
      () => cookie.fromJSON(pathTest),
      /Expected cookie "path" to be a string/
    );
  }

  // secure
  for (let invalidType of ["foo", 42, [], {}, null]) {
    let secureTest = {
      name: "foo",
      value: "bar",
      secure: invalidType,
    };
    Assert.throws(
      () => cookie.fromJSON(secureTest),
      /Expected cookie "secure" to be a boolean/
    );
  }

  // httpOnly
  for (let invalidType of ["foo", 42, [], {}, null]) {
    let httpOnlyTest = {
      name: "foo",
      value: "bar",
      httpOnly: invalidType,
    };
    Assert.throws(
      () => cookie.fromJSON(httpOnlyTest),
      /Expected cookie "httpOnly" to be a boolean/
    );
  }

  // expiry
  for (let invalidType of [
    -1,
    Number.MAX_SAFE_INTEGER + 1,
    "foo",
    true,
    [],
    {},
    null,
  ]) {
    let expiryTest = {
      name: "foo",
      value: "bar",
      expiry: invalidType,
    };
    Assert.throws(
      () => cookie.fromJSON(expiryTest),
      /Expected cookie "expiry" to be a positive integer/
    );
  }

  // sameSite
  for (let invalidType of ["foo", 42, [], {}, null]) {
    const sameSiteTest = {
      name: "foo",
      value: "bar",
      sameSite: invalidType,
    };
    Assert.throws(
      () => cookie.fromJSON(sameSiteTest),
      /Expected cookie "sameSite" to be one of None,Lax,Strict/
    );
  }

  // bare requirements
  let bare = cookie.fromJSON({ name: "name", value: "value" });
  equal("name", bare.name);
  equal("value", bare.value);
  for (let missing of [
    "path",
    "secure",
    "httpOnly",
    "session",
    "expiry",
    "sameSite",
  ]) {
    ok(!bare.hasOwnProperty(missing));
  }

  // everything
  let full = cookie.fromJSON({
    name: "name",
    value: "value",
    domain: ".domain",
    path: "path",
    secure: true,
    httpOnly: true,
    expiry: 42,
    sameSite: "Lax",
  });
  equal("name", full.name);
  equal("value", full.value);
  equal(".domain", full.domain);
  equal("path", full.path);
  equal(true, full.secure);
  equal(true, full.httpOnly);
  equal(42, full.expiry);
  equal("Lax", full.sameSite);
});

add_task(function test_add() {
  cookie.manager.cookies = [];

  for (let invalidType of [42, true, [], {}, null, undefined]) {
    Assert.throws(
      () => cookie.add({ name: invalidType }),
      /Expected cookie "name" to be a string/
    );
    Assert.throws(
      () => cookie.add({ name: "name", value: invalidType }),
      /Expected cookie "value" to be a string/
    );
    Assert.throws(
      () => cookie.add({ name: "name", value: "value", domain: invalidType }),
      /Expected cookie "domain" to be a string/
    );
  }

  cookie.add({
    name: "name",
    value: "value",
    domain: "domain",
  });
  equal(1, cookie.manager.cookies.length);
  equal("name", cookie.manager.cookies[0].name);
  equal("value", cookie.manager.cookies[0].value);
  equal(".domain", cookie.manager.cookies[0].host);
  equal("/", cookie.manager.cookies[0].path);
  Assert.greater(
    cookie.manager.cookies[0].expiry,
    new Date(Date.now()).getTime() / 1000
  );

  cookie.add({
    name: "name2",
    value: "value2",
    domain: "domain2",
  });
  equal(2, cookie.manager.cookies.length);

  Assert.throws(() => {
    let biscuit = { name: "name3", value: "value3", domain: "domain3" };
    cookie.add(biscuit, { restrictToHost: "other domain" });
  }, /Cookies may only be set for the current domain/);

  cookie.add({
    name: "name4",
    value: "value4",
    domain: "my.domain:1234",
  });
  equal(".my.domain", cookie.manager.cookies[2].host);

  cookie.add({
    name: "name5",
    value: "value5",
    domain: "domain5",
    path: "/foo/bar",
  });
  equal("/foo/bar", cookie.manager.cookies[3].path);

  cookie.add({
    name: "name6",
    value: "value",
    domain: ".domain",
  });
  equal(".domain", cookie.manager.cookies[4].host);

  const sameSiteMap = new Map([
    ["None", Ci.nsICookie.SAMESITE_NONE],
    ["Lax", Ci.nsICookie.SAMESITE_LAX],
    ["Strict", Ci.nsICookie.SAMESITE_STRICT],
  ]);

  Array.from(sameSiteMap.keys()).forEach((entry, index) => {
    cookie.add({
      name: "name" + index,
      value: "value",
      domain: ".domain",
      sameSite: entry,
    });
    equal(sameSiteMap.get(entry), cookie.manager.cookies[5 + index].sameSite);
  });

  Assert.throws(() => {
    cookie.add({ name: "fail", value: "value6", domain: "domain6" });
  }, /UnableToSetCookieError/);
});

add_task(function test_remove() {
  cookie.manager.cookies = [];

  let crumble = {
    name: "test_remove",
    value: "value",
    domain: "domain",
    path: "/custom/path",
  };

  equal(0, cookie.manager.cookies.length);
  cookie.add(crumble);
  equal(1, cookie.manager.cookies.length);

  cookie.remove(crumble);
  equal(0, cookie.manager.cookies.length);
  equal(undefined, cookie.manager.cookies[0]);
});

add_task(function test_iter() {
  cookie.manager.cookies = [];
  let tomorrow = new Date();
  tomorrow.setHours(tomorrow.getHours() + 24);

  cookie.add({
    expiry: tomorrow,
    name: "0",
    value: "",
    domain: "foo.example.com",
  });
  cookie.add({
    expiry: tomorrow,
    name: "1",
    value: "",
    domain: "bar.example.com",
  });

  let fooCookies = [...cookie.iter("foo.example.com")];
  equal(1, fooCookies.length);
  equal(".foo.example.com", fooCookies[0].domain);
  equal(true, fooCookies[0].hasOwnProperty("expiry"));

  cookie.add({
    name: "aSessionCookie",
    value: "",
    domain: "session.com",
  });

  let sessionCookies = [...cookie.iter("session.com")];
  equal(1, sessionCookies.length);
  equal("aSessionCookie", sessionCookies[0].name);
  equal(false, sessionCookies[0].hasOwnProperty("expiry"));

  cookie.add({
    name: "2",
    value: "",
    domain: "samesite.example.com",
    sameSite: "Lax",
  });

  let sameSiteCookies = [...cookie.iter("samesite.example.com")];
  equal(1, sameSiteCookies.length);
  equal("Lax", sameSiteCookies[0].sameSite);
});
