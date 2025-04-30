"use strict";

const override = Cc["@mozilla.org/network/native-dns-override;1"].getService(
  Ci.nsINativeDNSResolverOverride
);
const defaultOriginAttributes = {};
const mainThread = Services.tm.currentThread;

class Listener {
  constructor() {
    this.promise = new Promise(resolve => {
      this.resolve = resolve;
    });
  }

  onLookupComplete(inRequest, inRecord, inStatus) {
    this.resolve([inRequest, inRecord, inStatus]);
  }

  async firstAddress() {
    let all = await this.addresses();
    if (all.length) {
      return all[0];
    }

    return undefined;
  }

  async addresses() {
    let [, inRecord] = await this.promise;
    let addresses = [];
    if (!inRecord) {
      return addresses; // returns []
    }
    inRecord.QueryInterface(Ci.nsIDNSAddrRecord);
    while (inRecord.hasMore()) {
      addresses.push(inRecord.getNextAddrAsString());
    }
    return addresses;
  }

  then() {
    return this.promise.then.apply(this.promise, arguments);
  }
}
Listener.prototype.QueryInterface = ChromeUtils.generateQI(["nsIDNSListener"]);

const DOMAIN = "example.org";
const OTHER = "example.com";

add_setup(async function setup() {
  trr_test_setup();

  registerCleanupFunction(async () => {
    trr_clear_prefs();
  });
});

add_task(async function test_bad_IPs() {
  Assert.throws(
    () => override.addIPOverride(DOMAIN, DOMAIN),
    /NS_ERROR_UNEXPECTED/,
    "Should throw if input is not an IP address"
  );
  Assert.throws(
    () => override.addIPOverride(DOMAIN, ""),
    /NS_ERROR_UNEXPECTED/,
    "Should throw if input is not an IP address"
  );
  Assert.throws(
    () => override.addIPOverride(DOMAIN, " "),
    /NS_ERROR_UNEXPECTED/,
    "Should throw if input is not an IP address"
  );
  Assert.throws(
    () => override.addIPOverride(DOMAIN, "1-2-3-4"),
    /NS_ERROR_UNEXPECTED/,
    "Should throw if input is not an IP address"
  );
});

add_task(async function test_ipv4() {
  let listener = new Listener();
  override.addIPOverride(DOMAIN, "1.2.3.4");
  Services.dns.asyncResolve(
    DOMAIN,
    Ci.nsIDNSService.RESOLVE_TYPE_DEFAULT,
    0,
    null,
    listener,
    mainThread,
    defaultOriginAttributes
  );
  Assert.equal(await listener.firstAddress(), "1.2.3.4");

  Services.dns.clearCache(false);
  override.clearOverrides();
});

add_task(async function test_ipv6() {
  let listener = new Listener();
  override.addIPOverride(DOMAIN, "fe80::6a99:9b2b:6ccc:6e1b");
  Services.dns.asyncResolve(
    DOMAIN,
    Ci.nsIDNSService.RESOLVE_TYPE_DEFAULT,
    0,
    null,
    listener,
    mainThread,
    defaultOriginAttributes
  );
  Assert.equal(await listener.firstAddress(), "fe80::6a99:9b2b:6ccc:6e1b");

  Services.dns.clearCache(false);
  override.clearOverrides();
});

add_task(async function test_clearOverrides() {
  let listener = new Listener();
  override.addIPOverride(DOMAIN, "1.2.3.4");
  Services.dns.asyncResolve(
    DOMAIN,
    Ci.nsIDNSService.RESOLVE_TYPE_DEFAULT,
    0,
    null,
    listener,
    mainThread,
    defaultOriginAttributes
  );
  Assert.equal(await listener.firstAddress(), "1.2.3.4");

  Services.dns.clearCache(false);
  override.clearOverrides();

  listener = new Listener();
  Services.dns.asyncResolve(
    DOMAIN,
    Ci.nsIDNSService.RESOLVE_TYPE_DEFAULT,
    0,
    null,
    listener,
    mainThread,
    defaultOriginAttributes
  );
  Assert.notEqual(await listener.firstAddress(), "1.2.3.4");

  await new Promise(resolve => do_timeout(1000, resolve));
  Services.dns.clearCache(false);
  override.clearOverrides();
});

add_task(async function test_clearHostOverride() {
  override.addIPOverride(DOMAIN, "2.2.2.2");
  override.addIPOverride(OTHER, "2.2.2.2");
  override.clearHostOverride(DOMAIN);
  let listener = new Listener();
  Services.dns.asyncResolve(
    DOMAIN,
    Ci.nsIDNSService.RESOLVE_TYPE_DEFAULT,
    0,
    null,
    listener,
    mainThread,
    defaultOriginAttributes
  );

  Assert.notEqual(await listener.firstAddress(), "2.2.2.2");

  listener = new Listener();
  Services.dns.asyncResolve(
    OTHER,
    Ci.nsIDNSService.RESOLVE_TYPE_DEFAULT,
    0,
    null,
    listener,
    mainThread,
    defaultOriginAttributes
  );
  Assert.equal(await listener.firstAddress(), "2.2.2.2");

  // Note: this test will use the actual system resolver. On windows we do a
  // second async call to the system libraries to get the TTL values, which
  // keeps the record alive after the onLookupComplete()
  // We need to wait for a bit, until the second call is finished before we
  // can clear the cache to make sure we evict everything.
  // If the next task ever starts failing, with an IP that is not in this
  // file, then likely the timeout is too small.
  await new Promise(resolve => do_timeout(1000, resolve));
  Services.dns.clearCache(false);
  override.clearOverrides();
});

add_task(async function test_multiple_IPs() {
  override.addIPOverride(DOMAIN, "2.2.2.2");
  override.addIPOverride(DOMAIN, "1.1.1.1");
  override.addIPOverride(DOMAIN, "::1");
  override.addIPOverride(DOMAIN, "fe80::6a99:9b2b:6ccc:6e1b");
  let listener = new Listener();
  Services.dns.asyncResolve(
    DOMAIN,
    Ci.nsIDNSService.RESOLVE_TYPE_DEFAULT,
    0,
    null,
    listener,
    mainThread,
    defaultOriginAttributes
  );
  Assert.deepEqual(await listener.addresses(), [
    "2.2.2.2",
    "1.1.1.1",
    "::1",
    "fe80::6a99:9b2b:6ccc:6e1b",
  ]);

  Services.dns.clearCache(false);
  override.clearOverrides();
});

add_task(async function test_address_family_flags() {
  override.addIPOverride(DOMAIN, "2.2.2.2");
  override.addIPOverride(DOMAIN, "1.1.1.1");
  override.addIPOverride(DOMAIN, "::1");
  override.addIPOverride(DOMAIN, "fe80::6a99:9b2b:6ccc:6e1b");
  let listener = new Listener();
  Services.dns.asyncResolve(
    DOMAIN,
    Ci.nsIDNSService.RESOLVE_TYPE_DEFAULT,
    Ci.nsIDNSService.RESOLVE_DISABLE_IPV4,
    null,
    listener,
    mainThread,
    defaultOriginAttributes
  );
  Assert.deepEqual(await listener.addresses(), [
    "::1",
    "fe80::6a99:9b2b:6ccc:6e1b",
  ]);

  listener = new Listener();
  Services.dns.asyncResolve(
    DOMAIN,
    Ci.nsIDNSService.RESOLVE_TYPE_DEFAULT,
    Ci.nsIDNSService.RESOLVE_DISABLE_IPV6,
    null,
    listener,
    mainThread,
    defaultOriginAttributes
  );
  Assert.deepEqual(await listener.addresses(), ["2.2.2.2", "1.1.1.1"]);

  Services.dns.clearCache(false);
  override.clearOverrides();
});

add_task(async function test_cname_flag() {
  override.addIPOverride(DOMAIN, "2.2.2.2");
  let listener = new Listener();
  Services.dns.asyncResolve(
    DOMAIN,
    Ci.nsIDNSService.RESOLVE_TYPE_DEFAULT,
    0,
    null,
    listener,
    mainThread,
    defaultOriginAttributes
  );
  let [, inRecord] = await listener;
  inRecord.QueryInterface(Ci.nsIDNSAddrRecord);
  Assert.throws(
    () => inRecord.canonicalName,
    /NS_ERROR_NOT_AVAILABLE/,
    "No canonical name flag"
  );
  Assert.equal(inRecord.getNextAddrAsString(), "2.2.2.2");

  listener = new Listener();
  Services.dns.asyncResolve(
    DOMAIN,
    Ci.nsIDNSService.RESOLVE_TYPE_DEFAULT,
    Ci.nsIDNSService.RESOLVE_CANONICAL_NAME,
    null,
    listener,
    mainThread,
    defaultOriginAttributes
  );
  [, inRecord] = await listener;
  inRecord.QueryInterface(Ci.nsIDNSAddrRecord);
  Assert.equal(inRecord.canonicalName, DOMAIN, "No canonical name specified");
  Assert.equal(inRecord.getNextAddrAsString(), "2.2.2.2");

  Services.dns.clearCache(false);
  override.clearOverrides();

  override.addIPOverride(DOMAIN, "2.2.2.2");
  override.setCnameOverride(DOMAIN, OTHER);
  listener = new Listener();
  Services.dns.asyncResolve(
    DOMAIN,
    Ci.nsIDNSService.RESOLVE_TYPE_DEFAULT,
    Ci.nsIDNSService.RESOLVE_CANONICAL_NAME,
    null,
    listener,
    mainThread,
    defaultOriginAttributes
  );
  [, inRecord] = await listener;
  inRecord.QueryInterface(Ci.nsIDNSAddrRecord);
  Assert.equal(inRecord.canonicalName, OTHER, "Must have correct CNAME");
  Assert.equal(inRecord.getNextAddrAsString(), "2.2.2.2");

  Services.dns.clearCache(false);
  override.clearOverrides();
});

add_task(async function test_nxdomain() {
  override.addIPOverride(DOMAIN, "N/A");
  let listener = new Listener();
  Services.dns.asyncResolve(
    DOMAIN,
    Ci.nsIDNSService.RESOLVE_TYPE_DEFAULT,
    Ci.nsIDNSService.RESOLVE_CANONICAL_NAME,
    null,
    listener,
    mainThread,
    defaultOriginAttributes
  );

  let [, , inStatus] = await listener;
  equal(inStatus, Cr.NS_ERROR_UNKNOWN_HOST);
});

function makeChan(url) {
  let chan = NetUtil.newChannel({
    uri: url,
    loadUsingSystemPrincipal: true,
    contentPolicyType: Ci.nsIContentPolicy.TYPE_DOCUMENT,
  }).QueryInterface(Ci.nsIHttpChannel);
  return chan;
}

function channelOpenPromise(chan, flags) {
  return new Promise(resolve => {
    function finish(req, buffer) {
      resolve([req, buffer]);
    }
    chan.asyncOpen(new ChannelListener(finish, null, flags));
  });
}

function hexToUint8Array(hex) {
  return new Uint8Array(hex.match(/.{1,2}/g).map(byte => parseInt(byte, 16)));
}

add_task(
  {
    skip_if: () =>
      mozinfo.os == "win" ||
      mozinfo.os == "android" ||
      mozinfo.socketprocess_networking,
  },
  async function test_https_record_override() {
    let trrServer = new TRRServer();
    await trrServer.start();
    registerCleanupFunction(async () => {
      await trrServer.stop();
    });

    await trrServer.registerDoHAnswers("service.com", "HTTPS", {
      answers: [
        {
          name: "service.com",
          ttl: 55,
          type: "HTTPS",
          flush: false,
          data: {
            priority: 1,
            name: ".",
            values: [
              { key: "alpn", value: ["h2", "h3"] },
              { key: "no-default-alpn" },
              { key: "port", value: 8888 },
              { key: "ipv4hint", value: "1.2.3.4" },
              { key: "echconfig", value: "123..." },
              { key: "ipv6hint", value: "::1" },
              { key: "odoh", value: "456..." },
            ],
          },
        },
        {
          name: "service.com",
          ttl: 55,
          type: "HTTPS",
          flush: false,
          data: {
            priority: 2,
            name: "test.com",
            values: [
              { key: "alpn", value: "h2" },
              { key: "ipv4hint", value: ["1.2.3.4", "5.6.7.8"] },
              { key: "echconfig", value: "abc..." },
              { key: "ipv6hint", value: ["::1", "fe80::794f:6d2c:3d5e:7836"] },
              { key: "odoh", value: "def..." },
            ],
          },
        },
      ],
    });

    let chan = makeChan(
      `https://foo.example.com:${trrServer.port()}/dnsAnswer?host=service.com&type=HTTPS`
    );
    let [, buf] = await channelOpenPromise(chan);
    let rawBuffer = hexToUint8Array(buf);

    override.addHTTPSRecordOverride("service.com", rawBuffer, rawBuffer.length);

    Services.prefs.setBoolPref("network.dns.native_https_query", true);
    registerCleanupFunction(async () => {
      Services.prefs.clearUserPref("network.dns.native_https_query");
      Services.prefs.clearUserPref("network.trr.excluded-domains");
    });

    let listener = new Listener();
    Services.dns.asyncResolve(
      "service.com",
      Ci.nsIDNSService.RESOLVE_TYPE_HTTPSSVC,
      0,
      null,
      listener,
      mainThread,
      defaultOriginAttributes
    );

    let [, inRecord, inStatus] = await listener;
    equal(inStatus, Cr.NS_OK);
    Assert.ok(
      !inRecord.QueryInterface(Ci.nsIDNSHTTPSSVCRecord).IsTRR(),
      "resolved by Native"
    );
    let answer = inRecord.QueryInterface(Ci.nsIDNSHTTPSSVCRecord).records;
    equal(answer[0].priority, 1);
    equal(answer[0].name, "service.com");
    Assert.deepEqual(
      answer[0].values[0].QueryInterface(Ci.nsISVCParamAlpn).alpn,
      ["h2", "h3"],
      "got correct answer"
    );
    Assert.ok(
      answer[0].values[1].QueryInterface(Ci.nsISVCParamNoDefaultAlpn),
      "got correct answer"
    );
    Assert.equal(
      answer[0].values[2].QueryInterface(Ci.nsISVCParamPort).port,
      8888,
      "got correct answer"
    );
    Assert.equal(
      answer[0].values[3].QueryInterface(Ci.nsISVCParamIPv4Hint).ipv4Hint[0]
        .address,
      "1.2.3.4",
      "got correct answer"
    );
    Assert.equal(
      answer[0].values[4].QueryInterface(Ci.nsISVCParamEchConfig).echconfig,
      "123...",
      "got correct answer"
    );
    Assert.equal(
      answer[0].values[5].QueryInterface(Ci.nsISVCParamIPv6Hint).ipv6Hint[0]
        .address,
      "::1",
      "got correct answer"
    );
    Assert.equal(
      answer[0].values[6].QueryInterface(Ci.nsISVCParamODoHConfig).ODoHConfig,
      "456...",
      "got correct answer"
    );

    Assert.equal(answer[1].priority, 2);
    Assert.equal(answer[1].name, "test.com");
    Assert.equal(answer[1].values.length, 5);
    Assert.deepEqual(
      answer[1].values[0].QueryInterface(Ci.nsISVCParamAlpn).alpn,
      ["h2"],
      "got correct answer"
    );
    Assert.equal(
      answer[1].values[1].QueryInterface(Ci.nsISVCParamIPv4Hint).ipv4Hint[0]
        .address,
      "1.2.3.4",
      "got correct answer"
    );
    Assert.equal(
      answer[1].values[1].QueryInterface(Ci.nsISVCParamIPv4Hint).ipv4Hint[1]
        .address,
      "5.6.7.8",
      "got correct answer"
    );
    Assert.equal(
      answer[1].values[2].QueryInterface(Ci.nsISVCParamEchConfig).echconfig,
      "abc...",
      "got correct answer"
    );
    Assert.equal(
      answer[1].values[3].QueryInterface(Ci.nsISVCParamIPv6Hint).ipv6Hint[0]
        .address,
      "::1",
      "got correct answer"
    );
    Assert.equal(
      answer[1].values[3].QueryInterface(Ci.nsISVCParamIPv6Hint).ipv6Hint[1]
        .address,
      "fe80::794f:6d2c:3d5e:7836",
      "got correct answer"
    );
    Assert.equal(
      answer[1].values[4].QueryInterface(Ci.nsISVCParamODoHConfig).ODoHConfig,
      "def...",
      "got correct answer"
    );

    // Adding "service.com" into excluded-domains should fail
    // native HTTPS query.
    Services.prefs.setCharPref("network.trr.excluded-domains", "service.com");
    listener = new Listener();
    try {
      Services.dns.asyncResolve(
        "service.com",
        Ci.nsIDNSService.RESOLVE_TYPE_HTTPSSVC,
        0,
        null,
        listener,
        mainThread,
        defaultOriginAttributes
      );
      Assert.ok(false, "asyncResolve should fail");
    } catch (e) {
      Assert.equal(e.result, Cr.NS_ERROR_UNKNOWN_HOST);
    }
  }
);
