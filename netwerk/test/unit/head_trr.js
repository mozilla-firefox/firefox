/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/* import-globals-from head_cache.js */
/* import-globals-from head_cookies.js */
/* import-globals-from head_channels.js */
/* globals require, __dirname, global, Buffer, process, setTimeout */

var {
  ADB: TRRADB,
  NodeHTTP2Server: TRRNodeHttp2Server,
  NodeServer: TRRNodeServer,
  NodeHTTPServer: TRRNodeHttpServer,
} = ChromeUtils.importESModule("resource://testing-common/NodeServer.sys.mjs");

const { AppConstants: TRRAppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);

/// Sets the TRR related prefs and adds the certificate we use for the HTTP2
/// server.
function trr_test_setup() {
  dump("start!\n");

  do_get_profile();

  Services.prefs.setBoolPref("network.http.http2.enabled", true);
  // the TRR server is on 127.0.0.1
  if (TRRAppConstants.platform == "android") {
    Services.prefs.setCharPref("network.trr.bootstrapAddr", "10.0.2.2");
  } else {
    Services.prefs.setCharPref("network.trr.bootstrapAddr", "127.0.0.1");
  }

  // make all native resolve calls "secretly" resolve localhost instead
  Services.prefs.setBoolPref("network.dns.native-is-localhost", true);

  // don't confirm that TRR is working, just go!
  Services.prefs.setCharPref("network.trr.confirmationNS", "skip");
  // some tests rely on the cache not being cleared on pref change.
  // we specifically test that this works
  Services.prefs.setBoolPref("network.trr.clear-cache-on-pref-change", false);

  // Turn off strict fallback mode and TRR retry for most tests,
  // it is tested specifically.
  Services.prefs.setBoolPref("network.trr.strict_native_fallback", false);
  Services.prefs.setBoolPref("network.trr.retry_on_recoverable_errors", false);

  // Turn off temp blocklist feature in tests. When enabled we may issue a
  // lookup to resolve a parent name when blocklisting, which may bleed into
  // and interfere with subsequent tasks.
  Services.prefs.setBoolPref("network.trr.temp_blocklist", false);

  // We intentionally don't set the TRR mode. Each test should set it
  // after setup in the first test.
}

/// Clears the prefs that we're likely to set while testing TRR code
function trr_clear_prefs() {
  Services.prefs.clearUserPref("network.trr.mode");
  Services.prefs.clearUserPref("network.trr.uri");
  Services.prefs.clearUserPref("network.trr.credentials");
  Services.prefs.clearUserPref("network.trr.allow-rfc1918");
  Services.prefs.clearUserPref("network.trr.useGET");
  Services.prefs.clearUserPref("network.trr.confirmationNS");
  Services.prefs.clearUserPref("network.trr.bootstrapAddr");
  Services.prefs.clearUserPref("network.trr.temp_blocklist_duration_sec");
  Services.prefs.clearUserPref("network.trr.request_timeout_ms");
  Services.prefs.clearUserPref("network.trr.request_timeout_mode_trronly_ms");
  Services.prefs.clearUserPref("network.trr.disable-ECS");
  Services.prefs.clearUserPref("network.trr.early-AAAA");
  Services.prefs.clearUserPref("network.trr.excluded-domains");
  Services.prefs.clearUserPref("network.trr.builtin-excluded-domains");
  Services.prefs.clearUserPref("network.trr.clear-cache-on-pref-change");
  Services.prefs.clearUserPref("captivedetect.canonicalURL");

  Services.prefs.clearUserPref("network.http.http2.enabled");
  Services.prefs.clearUserPref("network.dns.localDomains");
  Services.prefs.clearUserPref("network.dns.native-is-localhost");
  Services.prefs.clearUserPref(
    "network.trr.send_empty_accept-encoding_headers"
  );
  Services.prefs.clearUserPref("network.trr.strict_native_fallback");
  Services.prefs.clearUserPref("network.trr.temp_blocklist");
}

/// This class sends a DNS query and can be awaited as a promise to get the
/// response.
class TRRDNSListener {
  constructor(...args) {
    if (args.length < 2) {
      Assert.ok(false, "TRRDNSListener requires at least two arguments");
    }
    this.name = args[0];
    if (typeof args[1] == "object") {
      this.options = args[1];
    } else {
      this.options = {
        expectedAnswer: args[1],
        expectedSuccess: args[2] ?? true,
        delay: args[3],
        trrServer: args[4] ?? "",
        expectEarlyFail: args[5] ?? "",
        flags: args[6] ?? 0,
        type: args[7] ?? Ci.nsIDNSService.RESOLVE_TYPE_DEFAULT,
        port: args[8] ?? -1,
      };
    }
    this.expectedAnswer = this.options.expectedAnswer ?? undefined;
    this.expectedSuccess = this.options.expectedSuccess ?? true;
    this.delay = this.options.delay;
    this.promise = new Promise(resolve => {
      this.resolve = resolve;
    });
    this.type = this.options.type ?? Ci.nsIDNSService.RESOLVE_TYPE_DEFAULT;
    let trrServer = this.options.trrServer || "";
    let port = this.options.port || -1;

    // This may be called in a child process that doesn't have Services available.
    // eslint-disable-next-line mozilla/use-services
    const threadManager = Cc["@mozilla.org/thread-manager;1"].getService(
      Ci.nsIThreadManager
    );
    const currentThread = threadManager.currentThread;

    this.additionalInfo =
      trrServer == "" && port == -1
        ? null
        : Services.dns.newAdditionalInfo(trrServer, port);
    try {
      this.request = Services.dns.asyncResolve(
        this.name,
        this.type,
        this.options.flags || 0,
        this.additionalInfo,
        this,
        currentThread,
        this.options.originAttributes || {} // defaultOriginAttributes
      );
      Assert.ok(!this.options.expectEarlyFail, "asyncResolve ok");
    } catch (e) {
      Assert.ok(this.options.expectEarlyFail, "asyncResolve fail");
      this.resolve({ error: e });
    }
  }

  onLookupComplete(inRequest, inRecord, inStatus) {
    Assert.equal(
      inRequest,
      this.request,
      "Checking that this is the correct callback"
    );

    // If we don't expect success here, just resolve and the caller will
    // decide what to do with the results.
    if (!this.expectedSuccess) {
      this.resolve({ inRequest, inRecord, inStatus });
      return;
    }

    Assert.equal(inStatus, Cr.NS_OK, "Checking status");

    if (this.type != Ci.nsIDNSService.RESOLVE_TYPE_DEFAULT) {
      this.resolve({ inRequest, inRecord, inStatus });
      return;
    }

    inRecord.QueryInterface(Ci.nsIDNSAddrRecord);
    let answer = inRecord.getNextAddrAsString();
    Assert.equal(
      answer,
      this.expectedAnswer,
      `Checking result for ${this.name}`
    );
    inRecord.rewind(); // In case the caller also checks the addresses

    if (this.delay !== undefined) {
      Assert.greaterOrEqual(
        inRecord.trrFetchDurationNetworkOnly,
        this.delay,
        `the response should take at least ${this.delay}`
      );

      Assert.greaterOrEqual(
        inRecord.trrFetchDuration,
        this.delay,
        `the response should take at least ${this.delay}`
      );

      if (this.delay == 0) {
        // The response timing should be really 0
        Assert.equal(
          inRecord.trrFetchDurationNetworkOnly,
          0,
          `the response time should be 0`
        );

        Assert.equal(
          inRecord.trrFetchDuration,
          this.delay,
          `the response time should be 0`
        );
      }
    }

    this.resolve({ inRequest, inRecord, inStatus });
  }

  QueryInterface(aIID) {
    if (aIID.equals(Ci.nsIDNSListener) || aIID.equals(Ci.nsISupports)) {
      return this;
    }
    throw Components.Exception("", Cr.NS_ERROR_NO_INTERFACE);
  }

  // Implement then so we can await this as a promise.
  then() {
    return this.promise.then.apply(this.promise, arguments);
  }

  cancel(aStatus = Cr.NS_ERROR_ABORT) {
    Services.dns.cancelAsyncResolve(
      this.name,
      this.type,
      this.options.flags || 0,
      this.resolverInfo,
      this,
      aStatus,
      {}
    );
  }
}

// This is for reteriiving the raw bytes from a DNS answer.
function answerHandler(req, resp) {
  let searchParams = new URL(req.url, "http://example.com").searchParams;
  if (!searchParams.get("host")) {
    resp.writeHead(400);
    resp.end("Missing search parameter");
    return;
  }

  function processRequest(req1, resp1) {
    let domain = searchParams.get("host");
    let type = searchParams.get("type");
    let response = global.dns_query_answers[`${domain}/${type}`] || {};
    let buf = global.dnsPacket.encode({
      type: "response",
      id: 0,
      flags: 0,
      questions: [],
      answers: response.answers || [],
      additionals: response.additionals || [],
    });
    let writeResponse = (resp2, buf2) => {
      try {
        let data = buf2.toString("hex");
        resp2.setHeader("Content-Length", data.length);
        resp2.writeHead(200, { "Content-Type": "plain/text" });
        resp2.write(data);
        resp2.end("");
      } catch (e) {}
    };

    writeResponse(resp1, buf, response);
  }

  processRequest(req, resp);
}

/// This is the default handler for /dns-query
/// It implements basic functionality for parsing the DoH packet, then
/// queries global.dns_query_answers for available answers for the DNS query.
function trrQueryHandler(req, resp) {
  let requestBody = Buffer.from("");
  let method =
    req.method || req.headers[global.http2.constants.HTTP2_HEADER_METHOD];
  let contentLength = req.headers["content-length"];

  if (method == "POST") {
    req.on("data", chunk => {
      requestBody = Buffer.concat([requestBody, chunk]);
      if (requestBody.length == contentLength) {
        processRequest(req, resp, requestBody);
      }
    });
  } else if (method == "GET") {
    let searchParams = new URL(req.url, "http://example.com").searchParams;
    if (!searchParams.get("dns")) {
      resp.writeHead(400);
      resp.end("Missing dns parameter");
      return;
    }

    requestBody = Buffer.from(searchParams.get("dns"), "base64");
    processRequest(req, resp, requestBody);
  } else {
    // unexpected method.
    resp.writeHead(405);
    resp.end("Unexpected method");
  }

  function processRequest(req1, resp1, payload) {
    let dnsQuery = global.dnsPacket.decode(payload);
    let domain = dnsQuery.questions[0].name;
    let type = dnsQuery.questions[0].type;
    let response = global.dns_query_answers[`${domain}/${type}`] || {};
    let delay = response.delay || 0;
    let searchParams = new URL(req1.url, "http://example.com").searchParams;
    if (searchParams.get("conncycle")) {
      if (domain.startsWith("newconn")) {
        // If we haven't seen a req for this newconn name before,
        // or if we've seen one for the same name on the same port,
        // synthesize a timeout.
        if (
          !global.gDoHNewConnLog[domain] ||
          global.gDoHNewConnLog[domain] == req1.socket.remotePort
        ) {
          delay = 1000;
        }
        if (!global.gDoHNewConnLog[domain]) {
          global.gDoHNewConnLog[domain] = req1.socket.remotePort;
        }
      }
      global.gDoHPortsLog.push([domain, req1.socket.remotePort]);
    }

    if (!global.dns_query_counts[domain]) {
      global.dns_query_counts[domain] = {};
    }
    global.dns_query_counts[domain][type] =
      global.dns_query_counts[domain][type] + 1 || 1;

    let flags = global.dnsPacket.RECURSION_DESIRED;
    if (!response.answers && !response.flags) {
      flags |= 2; // SERVFAIL
    }
    flags |= response.flags || 0;
    let buf = global.dnsPacket.encode({
      type: "response",
      id: dnsQuery.id,
      flags,
      questions: dnsQuery.questions,
      answers: response.answers || [],
      additionals: response.additionals || [],
    });

    let writeResponse = (resp2, buf2, context) => {
      try {
        if (context.error) {
          // If the error is a valid HTTP response number just write it out.
          if (context.error < 600) {
            resp2.writeHead(context.error);
            resp2.end("Intentional error");
            return;
          }

          // Bigger error means force close the session
          req1.stream.session.close();
          return;
        }
        resp2.setHeader("Content-Length", buf2.length);
        resp2.writeHead(200, { "Content-Type": "application/dns-message" });
        resp2.write(buf2);
        resp2.end("");
      } catch (e) {}
    };

    if (delay) {
      // This function is handled within the httpserver where setTimeout is
      // available.
      // eslint-disable-next-line no-undef
      setTimeout(
        arg => {
          writeResponse(arg[0], arg[1], arg[2]);
        },
        delay,
        [resp1, buf, response]
      );
      return;
    }

    writeResponse(resp1, buf, response);
  }
}

function dohHandler(req, res) {
  let u = global.url.parse(req.url, true);

  function handleAuth() {
    // There's a Set-Cookie: header in the response for "/dns" , which this
    // request subsequently would include if the http channel wasn't
    // anonymous. Thus, if there's a cookie in this request, we know Firefox
    // mishaved. If there's not, we're fine.
    if (req.headers.cookie) {
      res.writeHead(403);
      res.end("cookie for me, not for you");
      return false;
    }
    if (req.headers.authorization != "user:password") {
      res.writeHead(401);
      res.end("bad boy!");
      return false;
    }

    return true;
  }

  function createDNSAnswer(response, packet, responseIP, requestPayload) {
    // This shuts down the connection so we can test if the client reconnects
    if (packet.questions.length && packet.questions[0].name == "closeme.com") {
      // response.stream.connection.close("INTERNAL_ERROR", response.stream.id);
      req.stream.session.close();
      return null;
    }

    let answers = [];

    // The HTTPS RR query name may be port-prefixed (e.g.
    // "_55861._https.test.httpsrr.redirect.com"). The SVCB TargetName must be
    // the actual host ("test.httpsrr.redirect.com"), so strip the
    // "_<port>._https." prefix.
    function svcbTargetName(name) {
      return name.replace(/^_[^.]+\._https\./, "");
    }

    if (u.query.httpssvc) {
      responseIP = "none";
      answers.push({
        name: packet.questions[0].name,
        type: packet.questions[0].type,
        ttl: 55,
        class: "IN",
        flush: false,
        data: {
          priority: 1,
          name: "h3pool",
          values: [
            { key: "alpn", value: ["h2", "h3"] },
            { key: "no-default-alpn" },
            { key: "port", value: 8888 },
            { key: "ipv4hint", value: "1.2.3.4" },
            { key: "echconfig", value: "123..." },
            { key: "ipv6hint", value: "::1" },
            { key: 30, value: "somelargestring" },
            { key: "odoh", value: "456..." },
          ],
        },
      });
      answers.push({
        name: packet.questions[0].name,
        type: packet.questions[0].type,
        ttl: 55,
        class: "IN",
        flush: false,
        data: {
          priority: 2,
          name: ".",
          values: [
            { key: "alpn", value: "h2" },
            { key: "ipv4hint", value: ["1.2.3.4", "5.6.7.8"] },
            { key: "echconfig", value: "abc..." },
            { key: "ipv6hint", value: ["::1", "2001:db8::1"] },
            { key: "odoh", value: "def..." },
          ],
        },
      });
      answers.push({
        name: packet.questions[0].name,
        type: packet.questions[0].type,
        ttl: 55,
        class: "IN",
        flush: false,
        data: {
          priority: 3,
          name: "hello",
          values: [],
        },
      });
    } else if (u.query.httpssvc_as_altsvc) {
      responseIP = "none";
      if (packet.questions[0].type == "HTTPS") {
        let priority = 1;
        // The query name may be port-prefixed (e.g. _8080._https.foo...) when
        // network.dns.port_prefixed_qname_https_rr is enabled, so match the
        // host as a suffix rather than exactly.
        if (packet.questions[0].name.endsWith("foo.notexisted.com")) {
          priority = 0;
        }
        answers.push({
          name: packet.questions[0].name,
          type: packet.questions[0].type,
          ttl: 55,
          class: "IN",
          flush: false,
          data: {
            priority,
            name: svcbTargetName(packet.questions[0].name),
            values: [
              { key: "alpn", value: "h2" },
              { key: "port", value: global.serverPort },
              { key: 30, value: "somelargestring" },
            ],
          },
        });
      } else {
        answers.push({
          name: packet.questions[0].name,
          type: "A",
          ttl: 55,
          flush: false,
          data: "127.0.0.1",
        });
      }
    } else if (u.query.httpssvc_use_iphint) {
      responseIP = "none";
      answers.push({
        name: packet.questions[0].name,
        type: "HTTPS",
        ttl: 55,
        class: "IN",
        flush: false,
        data: {
          priority: 1,
          name: ".",
          values: [
            { key: "alpn", value: "h2" },
            { key: "port", value: global.serverPort },
            { key: "ipv4hint", value: "127.0.0.1" },
          ],
        },
      });
    }

    if (packet.questions.length && packet.questions[0].name.endsWith(".pd")) {
      // Bug 1543811: test edns padding extension. Return whether padding was
      // included via the first half of the ip address (1.1 vs 2.2) and the
      // size of the request in the second half of the ip address allowing to
      // verify that the correct amount of padding was added.
      if (
        !!packet.additionals.length &&
        packet.additionals[0].type == "OPT" &&
        packet.additionals[0].options.some(o => o.type === "PADDING")
      ) {
        // add padding to the response, because the client must be able ignore it
        answers.push({
          name: ".",
          type: "PADDING",
          data: Buffer.from(
            // PADDING_PADDING_PADDING
            "50414444494e475f50414444494e475f50414444494e47",
            "hex"
          ),
        });
        responseIP =
          "1.1." +
          ((requestPayload.length >> 8) & 0xff) +
          "." +
          (requestPayload.length & 0xff);
      } else {
        responseIP =
          "2.2." +
          ((requestPayload.length >> 8) & 0xff) +
          "." +
          (requestPayload.length & 0xff);
      }
    }

    if (u.query.corruptedAnswer) {
      // DNS response header is 12 bytes, we check for this minimum length
      // at the start of decoding so this is the simplest way to force
      // a decode error.
      return "\xFF\xFF\xFF\xFF";
    }

    // Because we send two TRR requests (A and AAAA), skip the first two
    // requests when testing retry.
    if (u.query.retryOnDecodeFailure && global.gDoHRequestCount < 2) {
      global.gDoHRequestCount++;
      return "\xFF\xFF\xFF\xFF";
    }

    function responseData() {
      if (
        !!packet.questions.length &&
        packet.questions[0].name == "confirm.example.com" &&
        packet.questions[0].type == "NS"
      ) {
        return "ns.example.com";
      }

      return responseIP;
    }

    if (
      responseIP != "none" &&
      responseType(packet, responseIP) == packet.questions[0].type
    ) {
      answers.push({
        name: u.query.hostname ? u.query.hostname : packet.questions[0].name,
        ttl: 55,
        type: responseType(packet, responseIP),
        flush: false,
        data: responseData(),
      });
    }

    // for use with test_dns_by_type_resolve.js
    if (packet.questions[0].type == "TXT") {
      answers.push({
        name: packet.questions[0].name,
        type: packet.questions[0].type,
        ttl: 55,
        class: "IN",
        flush: false,
        data: Buffer.from(
          "62586B67646D39705932556761584D6762586B676347467A63336476636D513D",
          "hex"
        ),
      });
    }

    if (u.query.cnameloop) {
      answers.push({
        name: "cname.example.com",
        type: "CNAME",
        ttl: 55,
        class: "IN",
        flush: false,
        data: "pointing-elsewhere.example.com",
      });
    }

    if (req.headers["accept-language"] || req.headers["user-agent"]) {
      // If we get this header, don't send back any response. This should
      // cause the tests to fail. This is easier then actually sending back
      // the header value into test_trr.js
      answers = [];
    }

    let buf = global.dnsPacket.encode({
      type: "response",
      id: packet.id,
      flags: global.dnsPacket.RECURSION_DESIRED,
      questions: packet.questions,
      answers,
    });

    return buf;
  }

  function responseType(packet, responseIP) {
    if (
      !!packet.questions.length &&
      packet.questions[0].name == "confirm.example.com" &&
      packet.questions[0].type == "NS"
    ) {
      return "NS";
    }

    return global.ip.isV4Format(responseIP) ? "A" : "AAAA";
  }

  function getDelayFromPacket(packet, type) {
    let delay = 0;
    if (packet.questions[0].type == "A") {
      delay = parseInt(u.query.delayIPv4);
    } else if (packet.questions[0].type == "AAAA") {
      delay = parseInt(u.query.delayIPv6);
    }

    if (u.query.slowConfirm && type == "NS") {
      delay += 1000;
    }

    return delay;
  }

  function writeDNSResponse(response, buf, delay, contentType) {
    function writeResponse(resp, buffer) {
      resp.setHeader("Set-Cookie", "trackyou=yes; path=/; max-age=100000;");
      resp.setHeader("Content-Type", contentType);
      if (req.headers["accept-encoding"].includes("gzip")) {
        global.zlib.gzip(buffer, function (err, result) {
          resp.setHeader("Content-Encoding", "gzip");
          resp.setHeader("Content-Length", result.length);
          try {
            resp.writeHead(200);
            resp.end(result);
          } catch (e) {
            // connection was closed by the time we started writing.
          }
        });
      } else {
        const output = Buffer.from(buffer, "utf-8");
        resp.setHeader("Content-Length", output.length);
        try {
          resp.writeHead(200);
          resp.write(output);
          resp.end("");
        } catch (e) {
          // connection was closed by the time we started writing.
        }
      }
    }

    if (delay) {
      setTimeout(
        arg => {
          writeResponse(arg[0], arg[1]);
        },
        delay + 1,
        [response, buf]
      );
      return;
    }

    writeResponse(response, buf);
  }

  let responseIP = u.query.responseIP;
  if (!responseIP) {
    responseIP = "5.5.5.5";
  }

  let redirect = u.query.redirect;
  if (redirect) {
    responseIP = redirect;
    if (u.query.dns) {
      res.setHeader(
        "Location",
        "https://localhost:" +
          global.serverPort +
          "/doh?responseIP=" +
          responseIP +
          "&dns=" +
          u.query.dns
      );
    } else {
      res.setHeader(
        "Location",
        "https://localhost:" +
          global.serverPort +
          "/doh?responseIP=" +
          responseIP
      );
    }
    res.writeHead(307);
    res.end("");
    return;
  }

  if (u.query.auth) {
    if (!handleAuth()) {
      return;
    }
  }

  if (u.query.noResponse) {
    return;
  }

  if (u.query.push) {
    // push.example.org has AAAA entry 2018::2018
    let pcontent = global.dnsPacket.encode({
      id: 0,
      type: "response",
      flags: global.dnsPacket.RECURSION_DESIRED,
      questions: [{ name: "push.example.org", type: "AAAA", class: "IN" }],
      answers: [
        {
          name: "push.example.org",
          type: "AAAA",
          ttl: 55,
          class: "IN",
          flush: false,
          data: "2018::2018",
        },
      ],
    });
    let push = res.push({
      hostname: "foo.example.com:" + global.serverPort,
      port: global.serverPort,
      path: "/dns-pushed-response?dns=AAAAAAABAAAAAAAABHB1c2gHZXhhbXBsZQNvcmcAABwAAQ",
      method: "GET",
      headers: {
        accept: "application/dns-message",
      },
    });
    push.writeHead(200, {
      "content-type": "application/dns-message",
      pushed: "yes",
      "content-length": pcontent.length,
      "X-Connection-Http2": "yes",
    });
    push.end(pcontent);
  }

  let payload = Buffer.from("");

  function emitResponse(response, requestPayload, decodedPacket, delay) {
    let packet = decodedPacket || global.dnsPacket.decode(requestPayload);
    let answer = createDNSAnswer(response, packet, responseIP, requestPayload);
    if (!answer) {
      return;
    }
    writeDNSResponse(
      response,
      answer,
      delay || getDelayFromPacket(packet, responseType(packet, responseIP)),
      "application/dns-message"
    );
  }

  if (u.query.dns) {
    payload = Buffer.from(u.query.dns, "base64");
    emitResponse(res, payload);
    return;
  }

  req.on("data", function receiveData(chunk) {
    payload = Buffer.concat([payload, chunk]);
  });
  req.on("end", function finishedData() {
    // parload is empty when we send redirect response.
    if (payload.length) {
      let packet = global.dnsPacket.decode(payload);
      emitResponse(res, payload, packet);
    }
  });
}

function cnameHandler(req, res) {
  // asking for cname.example.com

  function createCNameContent(payload) {
    let packet = global.dnsPacket.decode(payload);
    if (
      packet.questions[0].name == "cname.example.com" &&
      packet.questions[0].type == "A"
    ) {
      return global.dnsPacket.encode({
        id: 0,
        type: "response",
        flags: global.dnsPacket.RECURSION_DESIRED,
        questions: [{ name: packet.questions[0].name, type: "A", class: "IN" }],
        answers: [
          {
            name: packet.questions[0].name,
            ttl: 55,
            type: "CNAME",
            flush: false,
            data: "pointing-elsewhere.example.com",
          },
        ],
      });
    }
    if (
      packet.questions[0].name == "pointing-elsewhere.example.com" &&
      packet.questions[0].type == "A"
    ) {
      return global.dnsPacket.encode({
        id: 0,
        type: "response",
        flags: global.dnsPacket.RECURSION_DESIRED,
        questions: [{ name: packet.questions[0].name, type: "A", class: "IN" }],
        answers: [
          {
            name: packet.questions[0].name,
            ttl: 55,
            type: "A",
            flush: false,
            data: "99.88.77.66",
          },
        ],
      });
    }

    return global.dnsPacket.encode({
      id: 0,
      type: "response",
      flags:
        global.dnsPacket.RECURSION_DESIRED |
        global.dnsPacket.rcodes.toRcode("NXDOMAIN"),
      questions: [
        {
          name: packet.questions[0].name,
          type: packet.questions[0].type,
          class: "IN",
        },
      ],
      answers: [],
    });
  }

  function emitResponse(response, payload) {
    let pcontent = createCNameContent(payload);
    response.setHeader("Content-Type", "application/dns-message");
    response.setHeader("Content-Length", pcontent.length);
    response.writeHead(200);
    response.write(pcontent);
    response.end("");
  }

  let payload = Buffer.from("");
  req.on("data", function receiveData(chunk) {
    payload = Buffer.concat([payload, chunk]);
  });
  req.on("end", function finishedData() {
    emitResponse(res, payload);
  });
}

function cnameAHandler(req, res) {
  function createCNameARecord() {
    // test23 asks for cname-a.example.com
    // this responds with a CNAME to here.example.com *and* an A record
    // for here.example.com
    let rContent;

    rContent = Buffer.from(
      "0000" +
        "0100" +
        "0001" + // QDCOUNT
        "0002" + // ANCOUNT
        "00000000" + // NSCOUNT + ARCOUNT
        "07636E616D652d61" + // cname-a
        "076578616D706C6503636F6D00" + // .example.com
        "00010001" + // question type (A) + question class (IN)
        // answer record 1
        "C00C" + // name pointer to cname-a.example.com
        "0005" + // type (CNAME)
        "0001" + // class
        "00000037" + // TTL
        "0012" + // RDLENGTH
        "0468657265" + // here
        "076578616D706C6503636F6D00" + // .example.com
        // answer record 2, the A entry for the CNAME above
        "0468657265" + // here
        "076578616D706C6503636F6D00" + // .example.com
        "0001" + // type (A)
        "0001" + // class
        "00000037" + // TTL
        "0004" + // RDLENGTH
        "09080706", // IPv4 address
      "hex"
    );

    return rContent;
  }

  let rContent = createCNameARecord();
  res.setHeader("Content-Type", "application/dns-message");
  res.setHeader("Content-Length", rContent.length);
  res.writeHead(200);
  res.write(rContent);
  res.end("");
}

function getRequestCount(domain, type) {
  if (!global.dns_query_counts[domain]) {
    return 0;
  }
  return global.dns_query_counts[domain][type] || 0;
}

// A convenient wrapper around NodeServer
class TRRServer extends TRRNodeHttp2Server {
  /// Starts the server
  /// @port - default 0
  ///    when provided, will attempt to listen on that port.
  async start(port = 0) {
    await super.start(port);
    await this.execute(`( () => {
      // key: string "name/type"
      // value: array [answer1, answer2]
      global.dns_query_answers = {};

      // key: domain
      // value: a map containing {key: type, value: number of requests}
      global.dns_query_counts = {};

      global.gDoHPortsLog = [];
      global.gDoHNewConnLog = {};
      global.gDoHRequestCount = 0;

      global.dnsPacket = require(\`\${__dirname}/../dns-packet\`);
      global.ip = require(\`\${__dirname}/../node_ip\`);
      global.http2 = require("http2");
      global.url = require("url");
      global.zlib = require("zlib");
    })()`);
    await this.registerPathHandler("/dns-query", trrQueryHandler);
    await this.registerPathHandler("/dnsAnswer", answerHandler);
    await this.registerPathHandler("/doh", dohHandler);
    await this.registerPathHandler("/reset-doh-request-count", (req, res) => {
      global.gDoHRequestCount = 0;
      res.setHeader("Content-Type", "text/plain");
      res.setHeader("Content-Length", "ok".length);
      res.writeHead(200);
      res.write("ok");
      res.end("");
    });
    await this.registerPathHandler("/", (req, res) => {
      if (req.httpVersionMajor === 2) {
        res.setHeader("X-Connection-Http2", "yes");
        res.setHeader("X-Http2-StreamId", "" + req.stream.id);
      } else {
        res.setHeader("X-Connection-Http2", "no");
      }
      res.setHeader("Content-Type", "text/plain");
      res.writeHead(404);
      res.end("");
    });
    await this.registerPathHandler("/dns-cname", cnameHandler);
    await this.registerPathHandler("/dns-cname-a", cnameAHandler);
    await this.registerPathHandler("/server-timing", (req, res) => {
      if (req.httpVersionMajor === 2) {
        res.setHeader("X-Connection-Http2", "yes");
        res.setHeader("X-Http2-StreamId", "" + req.stream.id);
      } else {
        res.setHeader("X-Connection-Http2", "no");
      }

      res.setHeader("Content-Type", "text/plain");
      res.setHeader("Content-Length", "12");
      res.setHeader("Trailer", "Server-Timing");
      res.setHeader(
        "Server-Timing",
        "metric; dur=123.4; desc=description, metric2; dur=456.78; desc=description1"
      );
      res.write("data reached");
      res.addTrailers({
        "Server-Timing":
          "metric3; dur=789.11; desc=description2, metric4; dur=1112.13; desc=description3",
      });
      res.end();
    });
    await this.registerPathHandler("/redirect_to_http", (req, res) => {
      let u = global.url.parse(req.url, true);
      res.setHeader(
        "Location",
        `http://test.httpsrr.redirect.com:${u.query.port}/redirect_to_http?port=${u.query.port}`
      );
      res.writeHead(307);
      res.end("");
    });
    await this.registerPathHandler("/origin_header", (req, res) => {
      if (req.httpVersionMajor === 2) {
        res.setHeader("X-Connection-Http2", "yes");
        res.setHeader("X-Http2-StreamId", "" + req.stream.id);
      } else {
        res.setHeader("X-Connection-Http2", "no");
      }

      let originHeader = req.headers.origin;
      res.setHeader("Content-Length", originHeader.length);
      res.setHeader("Content-Type", "text/plain");
      res.writeHead(200);
      res.write(originHeader);
      res.end();
    });

    await this.execute(getRequestCount);
    await this.execute(`global.serverPort = ${this.port()}`);
  }

  /// @name : string - name we're providing answers for. eg: foo.example.com
  /// @type : string - the DNS query type. eg: "A", "AAAA", "CNAME", etc
  /// @response : a map containing the response
  ///   answers: array of answers (hashmap) that dnsPacket can parse
  ///    eg: [{
  ///          name: "bar.example.com",
  ///          ttl: 55,
  ///          type: "A",
  ///          flush: false,
  ///          data: "1.2.3.4",
  ///        }]
  ///   additionals - array of answers (hashmap) to be added to the additional section
  ///   delay: int - if not 0 the response will be sent with after `delay` ms.
  ///   flags: int - flags to be set on the answer
  ///   error: int - HTTP status. If truthy then the response will send this status
  async registerDoHAnswers(name, type, response = {}) {
    let text = `global.dns_query_answers["${name}/${type}"] = ${JSON.stringify(
      response
    )}`;
    return this.execute(text);
  }

  async requestCount(domain, type) {
    return this.execute(`getRequestCount("${domain}", "${type}")`);
  }
}

class PlainHttpTRRServer extends TRRNodeHttpServer {
  /// Starts the server
  /// @port - default 0
  ///    when provided, will attempt to listen on that port.
  async start(port = 0) {
    await super.start(port);
    await this.execute(`( () => {
      // key: string "name/type"
      // value: array [answer1, answer2]
      global.dns_query_answers = {};

      // key: domain
      // value: a map containing {key: type, value: number of requests}
      global.dns_query_counts = {};

      global.gDoHPortsLog = [];
      global.gDoHNewConnLog = {};
      global.gDoHRequestCount = 0;

      global.dnsPacket = require(\`\${__dirname}/../dns-packet\`);
      global.ip = require(\`\${__dirname}/../node_ip\`);
      global.url = require("url");
      global.zlib = require("zlib");
    })()`);
    await this.registerPathHandler("/dns-query", trrQueryHandler);

    await this.execute(getRequestCount);
    await this.execute(`global.serverPort = ${this.port()}`);
  }

  /// @name : string - name we're providing answers for. eg: foo.example.com
  /// @type : string - the DNS query type. eg: "A", "AAAA", "CNAME", etc
  /// @response : a map containing the response
  ///   answers: array of answers (hashmap) that dnsPacket can parse
  ///    eg: [{
  ///          name: "bar.example.com",
  ///          ttl: 55,
  ///          type: "A",
  ///          flush: false,
  ///          data: "1.2.3.4",
  ///        }]
  ///   additionals - array of answers (hashmap) to be added to the additional section
  ///   delay: int - if not 0 the response will be sent with after `delay` ms.
  ///   flags: int - flags to be set on the answer
  ///   error: int - HTTP status. If truthy then the response will send this status
  async registerDoHAnswers(name, type, response = {}) {
    let text = `global.dns_query_answers["${name}/${type}"] = ${JSON.stringify(
      response
    )}`;
    return this.execute(text);
  }

  async requestCount(domain, type) {
    return this.execute(`getRequestCount("${domain}", "${type}")`);
  }
}

// Implements a basic HTTP2 proxy server
class TRRProxyCode {
  static async startServer(endServerPort) {
    const fs = require("fs");
    const options = {
      key: fs.readFileSync(__dirname + "/http2-cert.key"),
      cert: fs.readFileSync(__dirname + "/http2-cert.pem"),
    };

    const http2 = require("http2");
    global.proxy = http2.createSecureServer(options);
    this.setupProxy();
    global.endServerPort = endServerPort;

    return ADB.listenAndForwardPort(global.proxy, 0); // eslint-disable-line no-undef
  }

  static closeProxy() {
    global.proxy.closeSockets();
    return new Promise(resolve => {
      global.proxy.close(resolve);
    });
  }

  static proxyRequestCount() {
    return global.proxy_stream_count;
  }

  static setupProxy() {
    if (!global.proxy) {
      throw new Error("proxy is null");
    }

    global.proxy_stream_count = 0;

    // We need to track active connections so we can forcefully close keep-alive
    // connections when shutting down the proxy.
    global.proxy.socketIndex = 0;
    global.proxy.socketMap = {};
    global.proxy.on("connection", function (socket) {
      let index = global.proxy.socketIndex++;
      global.proxy.socketMap[index] = socket;
      socket.on("close", function () {
        delete global.proxy.socketMap[index];
      });
    });
    global.proxy.closeSockets = function () {
      for (let i in global.proxy.socketMap) {
        global.proxy.socketMap[i].destroy();
      }
    };

    global.proxy.on("stream", (stream, headers) => {
      if (headers[":method"] !== "CONNECT") {
        // Only accept CONNECT requests
        stream.respond({ ":status": 405 });
        stream.end();
        return;
      }
      global.proxy_stream_count++;
      const net = require("net");
      const socket = net.connect(global.endServerPort, "127.0.0.1", () => {
        try {
          stream.respond({ ":status": 200 });
          socket.pipe(stream);
          stream.pipe(socket);
        } catch (exception) {
          console.log(exception);
          stream.close();
        }
      });
      socket.on("error", error => {
        console.log(
          `Error connecting to HTTP/2 server from proxy during CONNECT: ${error}`
        );
        stream.close();
      });
    });
  }
}

class TRRProxy {
  // Starts the proxy
  async start(port) {
    info("TRRProxy start!");
    this.processId = await TRRNodeServer.fork();
    info("processid=" + this.processId);
    await this.execute(TRRADB);
    await this.execute(TRRProxyCode);
    this.port = await this.execute(`TRRProxyCode.startServer(${port})`);
    Assert.notEqual(this.port, null);
  }

  // Executes a command in the context of the node server
  async execute(command) {
    return TRRNodeServer.execute(this.processId, command);
  }

  // Stops the server
  async stop() {
    if (this.processId) {
      await TRRNodeServer.execute(this.processId, `TRRProxyCode.closeProxy()`);
      await TRRNodeServer.kill(this.processId);
    }
  }

  async request_count() {
    let data = await TRRNodeServer.execute(
      this.processId,
      `TRRProxyCode.proxyRequestCount()`
    );
    return parseInt(data);
  }
}
