/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

trr_test_setup();
Services.prefs.setBoolPref("network.trr.add_additional_records", true);

registerCleanupFunction(async () => {
  trr_clear_prefs();
});

function makeChan(url) {
  let chan = NetUtil.newChannel({
    uri: url,
    loadUsingSystemPrincipal: true,
  }).QueryInterface(Ci.nsIHttpChannel);
  return chan;
}

function channelOpenPromise(chan) {
  return new Promise(resolve => {
    function finish(req, buffer) {
      resolve([req, buffer]);
    }
    chan.asyncOpen(new ChannelListener(finish));
  });
}

let trrServer = new TRRServer();
registerCleanupFunction(async () => {
  await trrServer.stop();
});
add_task(async function setup_server() {
  await trrServer.start();
  dump(`port = ${trrServer.port()}\n`);
  let chan = makeChan(`https://localhost:${trrServer.port()}/test?bla=some`);
  let [, resp] = await channelOpenPromise(chan);
  equal(resp, "<h1> 404 Path not found: /test</h1>");
});

add_task(async function test_parse_additional_section() {
  Services.dns.clearCache(true);
  Services.prefs.setIntPref("network.trr.mode", 3);
  Services.prefs.setCharPref(
    "network.trr.uri",
    `https://foo.example.com:${trrServer.port()}/dns-query`
  );

  await trrServer.registerDoHAnswers("something.foo", "A", {
    answers: [
      {
        name: "something.foo",
        ttl: 55,
        type: "A",
        flush: false,
        data: "1.2.3.4",
      },
    ],
    additionals: [
      {
        name: "else.foo",
        ttl: 55,
        type: "A",
        flush: false,
        data: "2.3.4.5",
      },
    ],
  });

  await new TRRDNSListener("something.foo", { expectedAnswer: "1.2.3.4" });
  await new TRRDNSListener("else.foo", { expectedAnswer: "2.3.4.5" });

  await trrServer.registerDoHAnswers("a.foo", "A", {
    // Make sure the request for b.foo is made before the response for a.foo
    // comes back.
    delay: 100,
    answers: [
      {
        name: "a.foo",
        ttl: 55,
        type: "A",
        flush: false,
        data: "1.2.3.4",
      },
    ],
    additionals: [
      {
        name: "b.foo",
        ttl: 55,
        type: "A",
        flush: false,
        data: "2.3.4.5",
      },
    ],
  });
  await trrServer.registerDoHAnswers("b.foo", "A", {
    answers: [
      {
        name: "b.foo",
        ttl: 55,
        type: "A",
        flush: false,
        data: "3.4.5.6",
      },
    ],
  });

  let req1 = new TRRDNSListener("a.foo", { expectedAnswer: "1.2.3.4" });

  // A request for b.foo will be in progress by the time we parse the additional
  // record. To keep things simple we don't end up saving the record, instead
  // we wait for the in-progress request to complete.
  let req2 = new TRRDNSListener("b.foo", { expectedAnswer: "3.4.5.6" });

  await Promise.all([req1, req2]);

  // IPv6 additional
  await trrServer.registerDoHAnswers("xyz.foo", "A", {
    answers: [
      {
        name: "xyz.foo",
        ttl: 55,
        type: "A",
        flush: false,
        data: "1.2.3.4",
      },
    ],
    additionals: [
      {
        name: "abc.foo",
        ttl: 55,
        type: "AAAA",
        flush: false,
        data: "::1:2:3:4",
      },
    ],
  });

  await new TRRDNSListener("xyz.foo", { expectedAnswer: "1.2.3.4" });
  await new TRRDNSListener("abc.foo", { expectedAnswer: "::1:2:3:4" });

  // IPv6 additional
  await trrServer.registerDoHAnswers("ipv6.foo", "AAAA", {
    answers: [
      {
        name: "ipv6.foo",
        ttl: 55,
        type: "AAAA",
        flush: false,
        data: "2001::a:b:c:d",
      },
    ],
    additionals: [
      {
        name: "def.foo",
        ttl: 55,
        type: "AAAA",
        flush: false,
        data: "::a:b:c:d",
      },
    ],
  });

  await new TRRDNSListener("ipv6.foo", { expectedAnswer: "2001::a:b:c:d" });
  await new TRRDNSListener("def.foo", { expectedAnswer: "::a:b:c:d" });

  // IPv6 additional
  await trrServer.registerDoHAnswers("ipv6b.foo", "AAAA", {
    answers: [
      {
        name: "ipv6b.foo",
        ttl: 55,
        type: "AAAA",
        flush: false,
        data: "2001::a:b:c:d",
      },
    ],
    additionals: [
      {
        name: "qqqq.foo",
        ttl: 55,
        type: "A",
        flush: false,
        data: "9.8.7.6",
      },
    ],
  });

  await new TRRDNSListener("ipv6b.foo", { expectedAnswer: "2001::a:b:c:d" });
  await new TRRDNSListener("qqqq.foo", { expectedAnswer: "9.8.7.6" });

  // Multiple IPs and multiple additional records
  await trrServer.registerDoHAnswers("multiple.foo", "A", {
    answers: [
      {
        name: "multiple.foo",
        ttl: 55,
        type: "A",
        flush: false,
        data: "9.9.9.9",
      },
    ],
    additionals: [
      {
        // Should be ignored, because it should be in the answer section
        name: "multiple.foo",
        ttl: 55,
        type: "A",
        flush: false,
        data: "1.1.1.1",
      },
      {
        // Is ignored, because it should be in the answer section
        name: "multiple.foo",
        ttl: 55,
        type: "AAAA",
        flush: false,
        data: "2001::a:b:c:d",
      },
      {
        name: "yuiop.foo",
        ttl: 55,
        type: "AAAA",
        flush: false,
        data: "2001::a:b:c:d",
      },
      {
        name: "yuiop.foo",
        ttl: 55,
        type: "A",
        flush: false,
        data: "1.2.3.4",
      },
    ],
  });

  let { inRecord } = await new TRRDNSListener("multiple.foo", {
    expectedAnswer: "9.9.9.9",
  });
  let IPs = [];
  inRecord.QueryInterface(Ci.nsIDNSAddrRecord);
  inRecord.rewind();
  while (inRecord.hasMore()) {
    IPs.push(inRecord.getNextAddrAsString());
  }
  equal(IPs.length, 1);
  equal(IPs[0], "9.9.9.9");
  IPs = [];
  ({ inRecord } = await new TRRDNSListener("yuiop.foo", {
    expectedSuccess: false,
  }));
  inRecord.QueryInterface(Ci.nsIDNSAddrRecord);
  inRecord.rewind();
  while (inRecord.hasMore()) {
    IPs.push(inRecord.getNextAddrAsString());
  }
  equal(IPs.length, 2);
  equal(IPs[0], "2001::a:b:c:d");
  equal(IPs[1], "1.2.3.4");
});

add_task(async function test_additional_after_resolve() {
  await trrServer.registerDoHAnswers("first.foo", "A", {
    answers: [
      {
        name: "first.foo",
        ttl: 55,
        type: "A",
        flush: false,
        data: "3.4.5.6",
      },
    ],
  });
  await new TRRDNSListener("first.foo", { expectedAnswer: "3.4.5.6" });

  await trrServer.registerDoHAnswers("second.foo", "A", {
    answers: [
      {
        name: "second.foo",
        ttl: 55,
        type: "A",
        flush: false,
        data: "1.2.3.4",
      },
    ],
    additionals: [
      {
        name: "first.foo",
        ttl: 55,
        type: "A",
        flush: false,
        data: "2.3.4.5",
      },
    ],
  });

  await new TRRDNSListener("second.foo", { expectedAnswer: "1.2.3.4" });
  await new TRRDNSListener("first.foo", { expectedAnswer: "2.3.4.5" });
});

// test for Bug - 1790075
// Crash was observed when a DNS (using TRR) reply contains an additional
// record field and this addditional record was previously unsuccessfully
// resolved
add_task(async function test_additional_cached_record_override() {
  Services.dns.clearCache(true);
  Services.prefs.setIntPref("network.trr.mode", 2);
  Services.prefs.setCharPref(
    "network.trr.uri",
    `https://foo.example.com:${trrServer.port()}/dns-query`
  );
  // After a native lookup, a second one is made to get the TTL. If it
  // completes after the additional record is saved, it overwrites it.
  Services.prefs.setBoolPref("network.dns.get-ttl", false);
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("network.dns.get-ttl");
  });

  await new TRRDNSListener("else.foo", { expectedAnswer: "127.0.0.1" });

  await trrServer.registerDoHAnswers("something.foo", "A", {
    answers: [
      {
        name: "something.foo",
        ttl: 55,
        type: "A",
        flush: false,
        data: "1.2.3.4",
      },
    ],
    additionals: [
      {
        name: "else.foo",
        ttl: 55,
        type: "A",
        flush: false,
        data: "2.3.4.5",
      },
    ],
  });

  await new TRRDNSListener("something.foo", { expectedAnswer: "1.2.3.4" });
  await new TRRDNSListener("else.foo", { expectedAnswer: "2.3.4.5" });
});

add_task(async function test_ipv6_disabled() {
  Services.prefs.setBoolPref("network.dns.disableIPv6", true);
  await trrServer.registerDoHAnswers("ipv6.foo", "A", {
    answers: [
      {
        name: "ipv6.foo",
        ttl: 55,
        type: "A",
        flush: false,
        data: "1.2.3.4",
      },
    ],
    additionals: [
      {
        name: "sub.ipv6.foo",
        ttl: 55,
        type: "AAAA",
        flush: false,
        data: "::1:2:3:4",
      },
    ],
  });

  await new TRRDNSListener("ipv6.foo", { expectedAnswer: "1.2.3.4" });
  await new TRRDNSListener("sub.ipv6.foo", { expectedSuccess: false });

  await trrServer.registerDoHAnswers("direct.ipv6.foo", "AAAA", {
    answers: [
      {
        name: "direct.ipv6.foo",
        ttl: 55,
        type: "AAAA",
        flush: false,
        data: "2001::a:b:c:d",
      },
    ],
  });

  await new TRRDNSListener("direct.ipv6.foo", { expectedSuccess: false });

  Services.prefs.setBoolPref("network.dns.disableIPv6", false);
});
