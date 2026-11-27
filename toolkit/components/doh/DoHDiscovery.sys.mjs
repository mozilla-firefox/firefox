/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Implements RFC 9462 Discovery of Designated Resolvers (DDR).
 *
 * A DoH server designated by the network's unencrypted (Do53) resolver may
 * only be used after verify() confirms, with a test query to the designated
 * resolver, that its TLS certificate covers one of the unencrypted resolver IP
 * addresses.
 */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  NetUtil: "resource://gre/modules/NetUtil.sys.mjs",
  clearTimeout: "resource://gre/modules/Timer.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
});

// Any domain works; the answer itself is not used.
const VERIFY_HOST = "firefox.com";

const VERIFY_TIMEOUT_MS = 5000;

const DNS_HEADER_SIZE = 12;

const DNS_RCODE_NOERROR = 0;
const DNS_RCODE_NXDOMAIN = 3;

// Returns the base64url-encoded RFC 8484 "dns" parameter for an A query for
// VERIFY_HOST.
function encodeTestQuery() {
  // ID 0, RD set, one question.
  let bytes = [0, 0, 0x01, 0x00, 0, 1, 0, 0, 0, 0, 0, 0];
  for (let label of VERIFY_HOST.split(".")) {
    bytes.push(label.length, ...Array.from(label, c => c.charCodeAt(0)));
  }
  // Root label, QTYPE A, QCLASS IN.
  bytes.push(0, 0, 1, 0, 1);
  return ChromeUtils.base64URLEncode(new Uint8Array(bytes), { pad: false });
}

// Whether a test query response is a DNS answer, even one without addresses
// for VERIFY_HOST: a resolver filtering it is still a working resolver.
function isDNSAnswer(aChannel, aStatus, aBody) {
  if (
    !Components.isSuccessCode(aStatus) ||
    aChannel.responseStatus != 200 ||
    aChannel.contentType != "application/dns-message" ||
    aBody.length < DNS_HEADER_SIZE
  ) {
    return false;
  }
  let isResponse = aBody.charCodeAt(2) & 0x80;
  let rcode = aBody.charCodeAt(3) & 0x0f;
  return (
    isResponse && (rcode == DNS_RCODE_NOERROR || rcode == DNS_RCODE_NXDOMAIN)
  );
}

export const DoHDiscovery = {
  /**
   * Verifies a designated resolver (RFC 9462 section 4.2): its TLS
   * certificate must cover one of resolverAddrs, and it must answer a test
   * query. Resolves to true if the resolver can be used.
   *
   * @param {object} discovered
   *   The designated resolver.
   * @param {string} discovered.uri
   *   The DoH URI of the designated resolver.
   * @param {string[]} discovered.resolverAddrs
   *   The IP addresses of the unencrypted resolvers that designated it.
   * @returns {Promise<boolean>}
   */
  async verify({ uri, resolverAddrs }) {
    if (!resolverAddrs?.length) {
      return false;
    }

    let outcome = await this._testQuery(uri, resolverAddrs);
    return outcome == "success";
  },

  // Sends an RFC 8484 GET query for VERIFY_HOST to the designated resolver at
  // aURI, checking its certificate against aResolverAddrs before reading the
  // answer. Resolves to "no_connection", "no_cert_match", "query_failed" or
  // "success".
  _testQuery(aURI, aResolverAddrs) {
    return new Promise(resolve => {
      let separator = aURI.includes("?") ? "&" : "?";
      let chan;
      try {
        chan = lazy.NetUtil.newChannel({
          uri: `${aURI}${separator}dns=${encodeTestQuery()}`,
          loadUsingSystemPrincipal: true,
          contentPolicyType: Ci.nsIContentPolicy.TYPE_OTHER,
        }).QueryInterface(Ci.nsIHttpChannel);
      } catch (e) {
        resolve("no_connection");
        return;
      }
      chan.loadFlags |=
        Ci.nsIRequest.LOAD_ANONYMOUS |
        Ci.nsIRequest.LOAD_BYPASS_CACHE |
        Ci.nsIRequest.INHIBIT_CACHING;
      chan.setRequestHeader("Accept", "application/dns-message", false);
      // Resolve the designated resolver's name with the network's resolver,
      // which designated it.
      chan.setTRRMode(Ci.nsIRequest.TRR_DISABLED_MODE);
      // The certificate check only covers the server at aURI.
      chan.redirectionLimit = 0;

      let certMatched = false;
      let certRejected = false;
      let body = "";
      let timer = lazy.setTimeout(
        () => chan.cancel(Cr.NS_ERROR_NET_TIMEOUT),
        VERIFY_TIMEOUT_MS
      );

      let listener = {
        onStartRequest(request) {
          if (!Components.isSuccessCode(request.status)) {
            return;
          }
          let cert = request.QueryInterface(Ci.nsIChannel).securityInfo
            ?.serverCert;
          try {
            certMatched =
              !!cert &&
              Services.dns.isDesignatedResolverCert(cert, aResolverAddrs);
          } catch (e) {
            console.error(`DoHDiscovery: certificate check failed: ${e}`);
          }
          if (!certMatched) {
            certRejected = true;
            request.cancel(Cr.NS_BINDING_ABORTED);
          }
        },
        onDataAvailable(request, stream, offset, count) {
          let data = lazy.NetUtil.readInputStreamToString(stream, count);
          // Only the DNS header is needed.
          if (body.length < DNS_HEADER_SIZE) {
            body += data.slice(0, DNS_HEADER_SIZE - body.length);
          }
        },
        onStopRequest(request, status) {
          lazy.clearTimeout(timer);
          if (certRejected) {
            resolve("no_cert_match");
          } else if (!certMatched) {
            resolve("no_connection");
          } else if (isDNSAnswer(chan, status, body)) {
            resolve("success");
          } else {
            resolve("query_failed");
          }
        },
        QueryInterface: ChromeUtils.generateQI([
          "nsIStreamListener",
          "nsIRequestObserver",
        ]),
      };

      try {
        chan.asyncOpen(listener);
      } catch (e) {
        // e.g. a port that Necko doesn't allow connecting to.
        lazy.clearTimeout(timer);
        resolve("no_connection");
      }
    });
  },
};
