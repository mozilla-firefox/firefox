/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * This module implements the heuristics used to determine whether to enable
 * or disable DoH on different networks. DoHController is responsible for running
 * these at startup and upon network changes.
 */
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "gNetworkLinkService",
  "@mozilla.org/network/network-link-service;1",
  "nsINetworkLinkService"
);

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "gParentalControlsService",
  "@mozilla.org/parental-controls-service;1",
  "nsIParentalControlsService"
);

ChromeUtils.defineESModuleGetters(lazy, {
  DoHConfigController: "resource://gre/modules/DoHConfig.sys.mjs",
});

const GLOBAL_CANARY = "use-application-dns.net.";

const NXDOMAIN_ERR = "NS_ERROR_UNKNOWN_HOST";

export const Heuristics = {
  // String constants used to indicate outcome of heuristics.
  ENABLE_DOH: "enable_doh",
  DISABLE_DOH: "disable_doh",

  async run() {
    // Run all the heuristics at the same time.
    let [safeSearchChecks, zscaler, canary] = await Promise.all([
      safeSearch(),
      zscalerCanary(),
      globalCanary(),
    ]);

    let platformChecks = await platform();
    let results = {
      google: safeSearchChecks.google,
      youtube: safeSearchChecks.youtube,
      zscalerCanary: zscaler,
      canary,
      browserParent: await parentalControls(),
      thirdPartyRoots: await thirdPartyRoots(),
      policy: await enterprisePolicy(),
      vpn: platformChecks.vpn,
      proxy: platformChecks.proxy,
      nrpt: platformChecks.nrpt,
      steeredProvider: "",
    };

    // If any of those were triggered, return the results immediately.
    if (Object.values(results).includes("disable_doh")) {
      return results;
    }

    // Check for provider steering only after the other heuristics have passed.
    results.steeredProvider = (await providerSteering()) || "";
    return results;
  },

  async checkEnterprisePolicy() {
    return enterprisePolicy();
  },

  // Test only
  async _setMockLinkService(mockLinkService) {
    this.mockLinkService = mockLinkService;
  },

  heuristicNameToSkipReason(heuristicName) {
    const namesToSkipReason = {
      google: Ci.nsITRRSkipReason.TRR_HEURISTIC_TRIPPED_GOOGLE_SAFESEARCH,
      youtube: Ci.nsITRRSkipReason.TRR_HEURISTIC_TRIPPED_YOUTUBE_SAFESEARCH,
      zscalerCanary: Ci.nsITRRSkipReason.TRR_HEURISTIC_TRIPPED_ZSCALER_CANARY,
      canary: Ci.nsITRRSkipReason.TRR_HEURISTIC_TRIPPED_CANARY,
      modifiedRoots: Ci.nsITRRSkipReason.TRR_HEURISTIC_TRIPPED_MODIFIED_ROOTS,
      browserParent:
        Ci.nsITRRSkipReason.TRR_HEURISTIC_TRIPPED_PARENTAL_CONTROLS,
      thirdPartyRoots:
        Ci.nsITRRSkipReason.TRR_HEURISTIC_TRIPPED_THIRD_PARTY_ROOTS,
      policy: Ci.nsITRRSkipReason.TRR_HEURISTIC_TRIPPED_ENTERPRISE_POLICY,
      vpn: Ci.nsITRRSkipReason.TRR_HEURISTIC_TRIPPED_VPN,
      proxy: Ci.nsITRRSkipReason.TRR_HEURISTIC_TRIPPED_PROXY,
      nrpt: Ci.nsITRRSkipReason.TRR_HEURISTIC_TRIPPED_NRPT,
    };

    let value = namesToSkipReason[heuristicName];
    if (value != undefined) {
      return value;
    }
    return Ci.nsITRRSkipReason.TRR_FAILED;
  },

  // Keep this in sync with the description of networking.doh_heuristics_result
  // defined in Scalars.yaml and the equivalent Glean metric in metrics.yaml.
  Telemetry: {
    incomplete: 0,
    pass: 1,
    optOut: 2,
    manuallyDisabled: 3,
    manuallyEnabled: 4,
    enterpriseDisabled: 5,
    enterprisePresent: 6,
    enterpriseEnabled: 7,
    vpn: 8,
    proxy: 9,
    nrpt: 10,
    browserParent: 11,
    modifiedRoots: 12,
    thirdPartyRoots: 13,
    google: 14,
    youtube: 15,
    zscalerCanary: 16,
    canary: 17,
    ignored: 18,

    heuristicNames() {
      return [
        "google",
        "youtube",
        "zscalerCanary",
        "canary",
        "browserParent",
        "thirdPartyRoots",
        "policy",
        "vpn",
        "proxy",
        "nrpt",
      ];
    },

    fromResults(results) {
      for (let label of Heuristics.Telemetry.heuristicNames()) {
        if (results[label] == Heuristics.DISABLE_DOH) {
          return Heuristics.Telemetry[label];
        }
      }
      return Heuristics.Telemetry.pass;
    },
  },
};

async function dnsLookup(hostname, resolveCanonicalName = false) {
  let lookupPromise = new Promise((resolve, reject) => {
    let request;
    let response = {
      addresses: [],
    };
    let listener = {
      onLookupComplete(inRequest, inRecord, inStatus) {
        if (inRequest === request) {
          if (!Components.isSuccessCode(inStatus)) {
            reject({ message: new Components.Exception("", inStatus).name });
            return;
          }
          inRecord.QueryInterface(Ci.nsIDNSAddrRecord);
          if (resolveCanonicalName) {
            try {
              response.canonicalName = inRecord.canonicalName;
            } catch (e) {
              // no canonicalName
            }
          }
          while (inRecord.hasMore()) {
            let addr = inRecord.getNextAddrAsString();
            // Sometimes there are duplicate records with the same ip.
            if (!response.addresses.includes(addr)) {
              response.addresses.push(addr);
            }
          }
          resolve(response);
        }
      },
    };
    let dnsFlags =
      Ci.nsIDNSService.RESOLVE_TRR_DISABLED_MODE |
      Ci.nsIDNSService.RESOLVE_DISABLE_IPV6 |
      Ci.nsIDNSService.RESOLVE_BYPASS_CACHE |
      Ci.nsIDNSService.RESOLVE_CANONICAL_NAME;
    try {
      request = Services.dns.asyncResolve(
        hostname,
        Ci.nsIDNSService.RESOLVE_TYPE_DEFAULT,
        dnsFlags,
        null,
        listener,
        null,
        {} /* defaultOriginAttributes */
      );
    } catch (e) {
      // handle exceptions such as offline mode.
      reject({ message: e.name });
    }
  });

  let addresses, canonicalName, err;

  try {
    let response = await lookupPromise;
    addresses = response.addresses;
    canonicalName = response.canonicalName;
  } catch (e) {
    addresses = [null];
    err = e.message;
  }

  return { addresses, canonicalName, err };
}

async function dnsListLookup(domainList) {
  let results = [];

  let resolutions = await Promise.all(
    domainList.map(domain => dnsLookup(domain))
  );
  for (let { addresses } of resolutions) {
    results = results.concat(addresses);
  }

  return results;
}

// TODO: Confirm the expected behavior when filtering is on
async function globalCanary() {
  let { addresses, err } = await dnsLookup(GLOBAL_CANARY);

  if (
    err === NXDOMAIN_ERR ||
    !addresses.length ||
    addresses.every(addr =>
      Services.io.hostnameIsLocalIPAddress(Services.io.newURI(`http://${addr}`))
    )
  ) {
    return "disable_doh";
  }

  return "enable_doh";
}

export async function parentalControls() {
  if (lazy.gParentalControlsService.parentalControlsEnabled) {
    return "disable_doh";
  }

  return "enable_doh";
}

async function thirdPartyRoots() {
  if (Cu.isInAutomation) {
    return "enable_doh";
  }

  let certdb = Cc["@mozilla.org/security/x509certdb;1"].getService(
    Ci.nsIX509CertDB
  );

  let hasThirdPartyRoots = await new Promise(resolve => {
    certdb.asyncHasThirdPartyRoots(resolve);
  });

  if (hasThirdPartyRoots) {
    return "disable_doh";
  }

  return "enable_doh";
}

async function enterprisePolicy() {
  if (!Services.policies) {
    return "no_policy_set";
  }

  if (Services.policies.status === Services.policies.ACTIVE) {
    let policies = Services.policies.getActivePolicies();

    if (!policies.hasOwnProperty("DNSOverHTTPS")) {
      // If DoH isn't in the policy, return that there is a policy (but no DoH specifics)
      return "policy_without_doh";
    }

    if (policies.DNSOverHTTPS.Enabled === true) {
      // If DoH is enabled in the policy, enable it
      return "enable_doh";
    }

    // If DoH is disabled in the policy, disable it
    return "disable_doh";
  }

  // Default return, meaning no policy related to DNSOverHTTPS
  return "no_policy_set";
}

async function safeSearch() {
  const providerList = [
    {
      name: "google",
      unfiltered: ["www.google.com.", "google.com."],
      safeSearch: ["forcesafesearch.google.com."],
    },
    {
      name: "youtube",
      unfiltered: [
        "www.youtube.com.",
        "m.youtube.com.",
        "youtubei.googleapis.com.",
        "youtube.googleapis.com.",
        "www.youtube-nocookie.com.",
      ],
      safeSearch: ["restrict.youtube.com.", "restrictmoderate.youtube.com."],
    },
  ];

  async function checkProvider(provider) {
    let [unfilteredAnswers, safeSearchAnswers] = await Promise.all([
      dnsListLookup(provider.unfiltered),
      dnsListLookup(provider.safeSearch),
    ]);

    // Given a provider, check if the answer for any safe search domain
    // matches the answer for any default domain
    for (let answer of safeSearchAnswers) {
      if (answer && unfilteredAnswers.includes(answer)) {
        return { name: provider.name, result: "disable_doh" };
      }
    }

    return { name: provider.name, result: "enable_doh" };
  }

  // Compare strict domain lookups to non-strict domain lookups.
  // Resolutions has a type of [{ name, result }]
  let resolutions = await Promise.all(
    providerList.map(provider => checkProvider(provider))
  );

  // Reduce that array entries into a single map
  return resolutions.reduce(
    (accumulator, check) => {
      accumulator[check.name] = check.result;
      return accumulator;
    },
    {} // accumulator
  );
}

async function zscalerCanary() {
  const ZSCALER_CANARY = "sitereview.zscaler.com.";

  let { addresses } = await dnsLookup(ZSCALER_CANARY);
  for (let address of addresses) {
    if (
      ["213.152.228.242", "199.168.151.251", "8.25.203.30"].includes(address)
    ) {
      // if sitereview.zscaler.com resolves to either one of the 3 IPs above,
      // Zscaler Shift service is in use, don't enable DoH
      return "disable_doh";
    }
  }

  return "enable_doh";
}

async function platform() {
  let platformChecks = {};

  let indications = Ci.nsINetworkLinkService.NONE_DETECTED;
  try {
    let linkService = lazy.gNetworkLinkService;
    if (Heuristics.mockLinkService) {
      linkService = Heuristics.mockLinkService;
    }
    indications = linkService.platformDNSIndications;
  } catch (e) {
    if (e.result != Cr.NS_ERROR_NOT_IMPLEMENTED) {
      console.error(e);
    }
  }

  platformChecks.vpn =
    indications & Ci.nsINetworkLinkService.VPN_DETECTED
      ? "disable_doh"
      : "enable_doh";
  platformChecks.proxy =
    indications & Ci.nsINetworkLinkService.PROXY_DETECTED
      ? "disable_doh"
      : "enable_doh";
  platformChecks.nrpt =
    indications & Ci.nsINetworkLinkService.NRPT_DETECTED
      ? "disable_doh"
      : "enable_doh";

  return platformChecks;
}

// Check if the network provides a DoH endpoint to use. Returns the name of the
// provider if the check is successful, else null. Currently we only support
// this for Comcast networks.
async function providerSteering() {
  if (!lazy.DoHConfigController.currentConfig.providerSteering.enabled) {
    return null;
  }
  const TEST_DOMAIN = "doh.test.";

  // Array of { name, canonicalName, uri } where name is an identifier for
  // telemetry, canonicalName is the expected CNAME when looking up doh.test,
  // and uri is the provider's DoH endpoint.
  let steeredProviders =
    lazy.DoHConfigController.currentConfig.providerSteering.providerList;

  if (!steeredProviders || !steeredProviders.length) {
    return null;
  }

  let { canonicalName, err } = await dnsLookup(TEST_DOMAIN, true);
  if (err || !canonicalName) {
    return null;
  }

  let provider = steeredProviders.find(p => {
    return p.canonicalName == canonicalName;
  });
  if (!provider || !provider.uri || !provider.id) {
    return null;
  }

  return provider;
}
