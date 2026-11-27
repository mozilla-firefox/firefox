/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Implements RFC 9462 Discovery of Designated Resolvers (DDR).
 *
 * We query the network's unencrypted (Do53) resolver for SVCB records at the
 * special name "_dns.resolver.arpa". A ServiceMode record advertising an HTTP
 * ALPN (h2/h3) and an RFC 9461 dohpath describes a DoH server designated by the
 * current network. The returned DoH URI is only used after the heuristics pass
 * and after verify() confirms, with a test query to the designated resolver,
 * that its TLS certificate covers one of the unencrypted resolver IP addresses.
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

// Whether aStatus is a TLS handshake or certificate validation failure.
function isTLSError(aStatus) {
  try {
    Cc["@mozilla.org/nss_errors_service;1"]
      .getService(Ci.nsINSSErrorsService)
      .getErrorClass(aStatus);
    return true;
  } catch (e) {
    return false;
  }
}

// RFC 9462: the special-use name queried with the SVCB (64) type.
const DDR_QNAME = "_dns.resolver.arpa";

// ALPN protocol identifiers we consider to indicate a DoH server.
const DOH_ALPNS = ["h2", "h3"];

function alpnIsDoH(alpnList) {
  return alpnList.some(alpn => DOH_ALPNS.includes(alpn));
}

// Extracts the port, alpn list and dohpath from an nsISVCBRecord's SvcParams.
function extractParams(record) {
  let port = 443;
  let alpn = [];
  let dohPath = "";

  // SvcParamKey values from HTTPSSVC.h: Alpn = 1, Port = 3.
  const SvcParamKeyAlpn = 1;
  const SvcParamKeyPort = 3;

  for (let param of record.values) {
    switch (param.type) {
      case SvcParamKeyAlpn:
        try {
          alpn = param.QueryInterface(Ci.nsISVCParamAlpn).alpn;
        } catch (e) {}
        break;
      case SvcParamKeyPort:
        try {
          port = param.QueryInterface(Ci.nsISVCParamPort).port;
        } catch (e) {}
        break;
      default:
        break;
    }
  }

  try {
    dohPath = record.dohPath;
  } catch (e) {}

  return { port, alpn, dohPath };
}

// Turns an RFC 9461 dohpath URI template into the { path, query } Necko expects
// for network.trr.uri, or returns null if it can't be used. RFC 9461 requires a
// relative template starting with "/" and containing a "dns" variable. For GET
// requests Necko appends "dns=<base64url>" as the last query parameter, so only
// templates ending in a "{?dns}" expansion (no other query) or a "{&dns}"
// expansion (after a literal query) can be expressed.
function parseDohPath(dohPath) {
  if (!dohPath.startsWith("/")) {
    return null;
  }
  let literal;
  if (dohPath.endsWith("{?dns}")) {
    literal = dohPath.slice(0, -"{?dns}".length);
    if (literal.includes("?")) {
      return null;
    }
  } else if (dohPath.endsWith("{&dns}")) {
    literal = dohPath.slice(0, -"{&dns}".length);
    if (!literal.includes("?") || literal.endsWith("?")) {
      return null;
    }
  } else {
    return null;
  }
  if (/[{}#]/.test(literal)) {
    return null;
  }
  let [path, query = ""] = literal.split(/\?(.*)/s);
  return { path, query };
}

// Builds the DoH URI for a designated resolver, or returns null if the
// TargetName isn't a usable host for one.
function buildDoHURI(target, port, { path, query }) {
  // RFC 9460: a ServiceMode TargetName of "." means the owner name, which here
  // is _dns.resolver.arpa itself.
  let host = target.replace(/\.$/, "").toLowerCase();
  if (!host || host == DDR_QNAME) {
    return null;
  }

  let uri;
  try {
    uri = Services.io
      .newURI("https://localhost")
      .mutate()
      .setHost(host)
      .setPort(port == 443 ? -1 : port)
      .setPathQueryRef(query ? `${path}?${query}` : path)
      .finalize();
  } catch (e) {
    return null;
  }
  // The TargetName is raw DNS name data that may not be a valid host, and the
  // path must not be normalized into something else.
  if (
    uri.asciiHost != host ||
    uri.port != (port == 443 ? -1 : port) ||
    uri.filePath != path ||
    uri.query != query
  ) {
    return null;
  }
  return uri.spec;
}

export const DoHDiscovery = {
  /**
   * Runs DDR discovery. Resolves to { uri, resolverAddrs } for the highest
   * priority usable designated DoH resolver, or null if none was found.
   */
  async discover() {
    Glean.networking.dohDdrDiscovery.total.add(1);

    if (!Services.dns.nativeHTTPSEnabled) {
      Glean.networking.dohDdrResult.no_native_support.add(1);
      return null;
    }

    let record = await this._resolveSVCB(DDR_QNAME);
    if (!record) {
      Glean.networking.dohDdrResult.no_svcb.add(1);
      return null;
    }
    Glean.networking.dohDdrDiscovery.svcb_result.add(1);

    let httpsRec;
    try {
      httpsRec = record.QueryInterface(Ci.nsIDNSHTTPSSVCRecord);
    } catch (e) {
      Glean.networking.dohDdrResult.no_dohpath.add(1);
      return null;
    }

    let resolverAddrs = [];
    try {
      resolverAddrs = httpsRec.resolverAddrs;
    } catch (e) {}

    // records are already sorted by ascending priority by the decoder.
    for (let svcb of httpsRec.records) {
      // AliasMode records have priority 0 and are not usable here.
      if (svcb.priority == 0) {
        continue;
      }

      let { port, alpn, dohPath } = extractParams(svcb);
      if (!dohPath || !alpnIsDoH(alpn)) {
        continue;
      }

      let parsedPath = parseDohPath(dohPath);
      if (!parsedPath) {
        continue;
      }

      let uri = buildDoHURI(svcb.name, port, parsedPath);
      if (!uri) {
        continue;
      }

      Glean.networking.dohDdrDiscovery.dohpath.add(1);
      // The terminal outcome for this attempt is recorded by verify(), or by
      // the caller if it doesn't run it.
      return { uri, resolverAddrs };
    }

    Glean.networking.dohDdrResult.no_dohpath.add(1);
    return null;
  },

  /**
   * Verifies a resolver returned by discover() (RFC 9462 section 4.2): its
   * TLS certificate must cover one of resolverAddrs, and it must answer a test
   * query. Records the outcome of the DDR attempt and resolves to true if the
   * resolver can be used.
   *
   * @param {object} discovered
   *   The result of discover().
   * @param {string} discovered.uri
   *   The DoH URI of the designated resolver.
   * @param {string[]} discovered.resolverAddrs
   *   The IP addresses of the unencrypted resolvers that designated it.
   * @returns {Promise<boolean>}
   */
  async verify({ uri, resolverAddrs }) {
    if (!resolverAddrs?.length) {
      Glean.networking.dohDdrResult.no_resolver_addrs.add(1);
      return false;
    }

    let outcome = await this._testQuery(uri, resolverAddrs);
    if (outcome != "no_connection") {
      Glean.networking.dohDdrDiscovery.connection.add(1);
    }
    if (outcome == "query_failed" || outcome == "success") {
      Glean.networking.dohDdrDiscovery.cert_match.add(1);
    }
    Glean.networking.dohDdrResult[outcome].add(1);
    return outcome == "success";
  },

  // Sends an RFC 8484 GET query for VERIFY_HOST to the designated resolver at
  // aURI, checking its certificate against aResolverAddrs before reading the
  // answer. Resolves to the doh_ddr_result label of the outcome.
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
          // A refused redirect still comes from the designated resolver, so
          // check its certificate to classify the attempt.
          if (
            !Components.isSuccessCode(request.status) &&
            request.status != Cr.NS_ERROR_REDIRECT_LOOP
          ) {
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
            resolve(isTLSError(status) ? "tls_failed" : "no_connection");
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

  _resolveSVCB(host) {
    return new Promise(resolve => {
      let listener = {
        onLookupComplete(request, rec, status) {
          if (!Components.isSuccessCode(status) || !rec) {
            resolve(null);
            return;
          }
          resolve(rec);
        },
        QueryInterface: ChromeUtils.generateQI(["nsIDNSListener"]),
      };

      // Query the local (Do53) resolver directly: bypass TRR and the cache.
      let flags =
        Ci.nsIDNSService.RESOLVE_DISABLE_TRR |
        Ci.nsIDNSService.RESOLVE_BYPASS_CACHE;

      try {
        Services.dns.asyncResolve(
          host,
          Ci.nsIDNSService.RESOLVE_TYPE_SVCB,
          flags,
          null,
          listener,
          Services.tm.mainThreadEventTarget,
          {}
        );
      } catch (e) {
        console.error(`DoHDiscovery: asyncResolve failed: ${e.message}`);
        resolve(null);
      }
    });
  },
};
