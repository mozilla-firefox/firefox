/* This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

// How to run this file:
// 1. [obtain firefox source code]
// 2. [build/obtain firefox binaries]
// 3. run `[path to]/run-mozilla.sh [path to]/xpcshell [path to]/getHSTSPreloadlist.js [absolute path to]/nsSTSPreloadlist.inc'
// Note: Running this file outputs a new nsSTSPreloadlist.inc in the current
//       working directory.

var gSSService = Cc["@mozilla.org/ssservice;1"].getService(
  Ci.nsISiteSecurityService
);

const { FileUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/FileUtils.sys.mjs"
);

const SOURCE =
  "https://raw.githubusercontent.com/chromium/chromium/main/net/http/transport_security_state_static.json";
const TOOL_SOURCE =
  "https://hg.mozilla.org/mozilla-central/file/default/taskcluster/docker/periodic-updates/scripts/getHSTSPreloadList.js";
const OUTPUT = "nsSTSPreloadList.inc";
const MINIMUM_REQUIRED_MAX_AGE = 60 * 60 * 24 * 7 * 18;
const MAX_CONCURRENT_REQUESTS = 500;
const MAX_RETRIES = 1;
const REQUEST_TIMEOUT = 30 * 1000;
const ERROR_NONE = "no error";
const ERROR_CONNECTING_TO_HOST = "could not connect to host";
const ERROR_NO_HSTS_HEADER = "did not receive HSTS header";
const ERROR_MAX_AGE_TOO_LOW = "max-age too low: ";
const HEADER = `/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*****************************************************************************/
/* This is an automatically generated file. If you're not                    */
/* nsSiteSecurityService.cpp, you shouldn't be #including it.                */
/*****************************************************************************/

#include <stdint.h>
`;

const GPERF_DELIM = "%%\n";

async function download() {
  var resp = await fetch(SOURCE);
  if (resp.status != 200) {
    throw new Error(
      "ERROR: problem downloading '" + SOURCE + "': status " + resp.status
    );
  }

  let text = await resp.text();
  // we have to filter out '//' comments, while not mangling the json
  let result = text.replace(/^(\s*)?\/\/[^\n]*\n/gm, "");
  let data = null;
  try {
    data = JSON.parse(result);
  } catch (e) {
    throw new Error(`ERROR: could not parse data from '${SOURCE}': ${e}`);
  }
  return data;
}

function getHosts(rawdata) {
  let hosts = [];

  if (!rawdata || !rawdata.entries) {
    throw new Error(
      "ERROR: source data not formatted correctly: 'entries' not found"
    );
  }

  for (let entry of rawdata.entries) {
    if (entry.mode && entry.mode == "force-https") {
      if (entry.name) {
        // We trim the entry name here to avoid malformed URI exceptions when we
        // later try to connect to the domain.
        entry.name = entry.name.trim();
        entry.retries = MAX_RETRIES;
        // We prefer the camelCase variable to the JSON's snake case version
        entry.includeSubdomains = entry.include_subdomains;
        hosts.push(entry);
      } else {
        throw new Error("ERROR: entry not formatted correctly: no name found");
      }
    }
  }

  return hosts;
}

function processStsHeader(host, header, status, securityInfo) {
  let maxAge = {
    value: 0,
  };
  let includeSubdomains = {
    value: false,
  };
  let error = ERROR_NONE;
  if (
    header != null &&
    securityInfo != null &&
    securityInfo.overridableErrorCategory ==
      Ci.nsITransportSecurityInfo.ERROR_UNSET
  ) {
    try {
      let uri = Services.io.newURI("https://" + host.name);
      gSSService.processHeader(uri, header, {}, maxAge, includeSubdomains);
    } catch (e) {
      dump(
        "ERROR: could not process header '" +
          header +
          "' from " +
          host.name +
          ": " +
          e +
          "\n"
      );
      error = e;
    }
  } else if (status == 0) {
    error = ERROR_CONNECTING_TO_HOST;
  } else {
    error = ERROR_NO_HSTS_HEADER;
  }

  if (error == ERROR_NONE && maxAge.value < MINIMUM_REQUIRED_MAX_AGE) {
    error = ERROR_MAX_AGE_TOO_LOW;
  }

  return {
    name: host.name,
    maxAge: maxAge.value,
    includeSubdomains: includeSubdomains.value,
    error,
    retries: host.retries - 1,
    forceInclude: host.forceInclude,
  };
}

// RedirectAndAuthStopper prevents redirects and HTTP authentication
function RedirectAndAuthStopper() {}

RedirectAndAuthStopper.prototype = {
  // nsIChannelEventSink
  asyncOnChannelRedirect(oldChannel, newChannel, flags, callback) {
    throw Components.Exception("", Cr.NS_ERROR_ENTITY_CHANGED);
  },

  // nsIAuthPrompt2
  promptAuth(channel, level, authInfo) {
    return false;
  },

  asyncPromptAuth(channel, callback, context, level, authInfo) {
    throw Components.Exception("", Cr.NS_ERROR_NOT_IMPLEMENTED);
  },

  getInterface(iid) {
    return this.QueryInterface(iid);
  },

  QueryInterface: ChromeUtils.generateQI([
    "nsIChannelEventSink",
    "nsIAuthPrompt2",
  ]),
};

function fetchstatus(host) {
  return new Promise((resolve, reject) => {
    let xhr = new XMLHttpRequest();
    let uri = "https://" + host.name + "/";

    xhr.open("head", uri, true);
    xhr.setRequestHeader("X-Automated-Tool", TOOL_SOURCE);
    xhr.timeout = REQUEST_TIMEOUT;

    let errorHandler = () => {
      dump("ERROR: exception making request to " + host.name + "\n");
      resolve(
        processStsHeader(
          host,
          null,
          xhr.status,
          xhr.channel && xhr.channel.securityInfo
        )
      );
    };

    xhr.onerror = errorHandler;
    xhr.ontimeout = errorHandler;
    xhr.onabort = errorHandler;

    xhr.onload = () => {
      let header = xhr.getResponseHeader("strict-transport-security");
      resolve(
        processStsHeader(host, header, xhr.status, xhr.channel.securityInfo)
      );
    };

    xhr.channel.notificationCallbacks = new RedirectAndAuthStopper();
    xhr.send();
  });
}

async function getHSTSStatus(host) {
  do {
    host = await fetchstatus(host);
  } while (shouldRetry(host));
  return host;
}

function compareHSTSStatus(a, b) {
  if (a.name > b.name) {
    return 1;
  }
  if (a.name < b.name) {
    return -1;
  }
  return 0;
}

function writeTo(string, fos) {
  fos.write(string, string.length);
}

// Determines and returns a string representing a declaration of when this
// preload list should no longer be used.
// This is the current time plus MINIMUM_REQUIRED_MAX_AGE.
function getExpirationTimeString() {
  let now = new Date();
  let nowMillis = now.getTime();
  // MINIMUM_REQUIRED_MAX_AGE is in seconds, so convert to milliseconds
  let expirationMillis = nowMillis + MINIMUM_REQUIRED_MAX_AGE * 1000;
  let expirationMicros = expirationMillis * 1000;
  return (
    "const PRTime gPreloadListExpirationTime = INT64_C(" +
    expirationMicros +
    ");\n"
  );
}

function shouldRetry(response) {
  return (
    response.error != ERROR_NO_HSTS_HEADER &&
    response.error != ERROR_MAX_AGE_TOO_LOW &&
    response.error != ERROR_NONE &&
    response.retries > 0
  );
}

// Copied from browser/components/migration/MigrationUtils.sys.mjs
function spinResolve(promise) {
  if (!(promise instanceof Promise)) {
    return promise;
  }
  let done = false;
  let result = null;
  let error = null;
  promise
    .catch(e => {
      error = e;
    })
    .then(r => {
      result = r;
      done = true;
    });

  Services.tm.spinEventLoopUntil(
    "getHSTSPreloadList.js:spinResolve",
    () => done
  );
  if (error) {
    throw error;
  } else {
    return result;
  }
}

async function probeHSTSStatuses(inHosts) {
  let totalLength = inHosts.length;
  dump("Examining " + totalLength + " hosts.\n");

  // Make requests in batches of MAX_CONCURRENT_REQUESTS. Otherwise, we have
  // too many in-flight requests and the time it takes to process them causes
  // them all to time out.
  let allResults = [];
  while (inHosts.length) {
    let promises = [];
    for (let i = 0; i < MAX_CONCURRENT_REQUESTS && inHosts.length; i++) {
      let host = inHosts.shift();
      promises.push(getHSTSStatus(host));
    }
    let results = await Promise.all(promises);
    let progress = (
      (100 * (totalLength - inHosts.length)) /
      totalLength
    ).toFixed(2);
    dump(progress + "% done\n");
    allResults = allResults.concat(results);
  }

  dump("HSTS Probe received " + allResults.length + " statuses.\n");
  return allResults;
}

function readCurrentList(filename) {
  var currentHosts = {};
  var file = Cc["@mozilla.org/file/local;1"].createInstance(Ci.nsIFile);
  file.initWithPath(filename);
  var fis = Cc["@mozilla.org/network/file-input-stream;1"].createInstance(
    Ci.nsILineInputStream
  );
  fis.init(file, -1, -1, Ci.nsIFileInputStream.CLOSE_ON_EOF);
  var line = {};

  // While we generate entries matching the latest version format,
  // we still need to be able to read entries in the previous version formats
  // for bootstrapping a latest version preload list from a previous version
  // preload list. Hence these regexes.
  const entryRegexes = [
    /([^,]+), (0|1)/, // v3
    / {2}\/\* "([^"]*)", (true|false) \*\//, // v2
    / {2}{ "([^"]*)", (true|false) },/, // v1
  ];

  while (fis.readLine(line)) {
    let match;
    entryRegexes.find(r => {
      match = r.exec(line.value);
      return match;
    });
    if (match) {
      currentHosts[match[1]] = match[2] == "1" || match[2] == "true";
    }
  }
  return currentHosts;
}

function combineLists(newHosts, currentHosts) {
  let newHostsSet = new Set();

  for (let newHost of newHosts) {
    newHostsSet.add(newHost.name);
  }

  for (let currentHost in currentHosts) {
    if (!newHostsSet.has(currentHost)) {
      newHosts.push({ name: currentHost, retries: MAX_RETRIES });
    }
  }
}

const TEST_ENTRIES = [
  {
    name: "includesubdomains.preloaded.test",
    includeSubdomains: true,
  },
  {
    name: "includesubdomains2.preloaded.test",
    includeSubdomains: true,
  },
  {
    name: "noincludesubdomains.preloaded.test",
    includeSubdomains: false,
  },
];

function deleteTestHosts(currentHosts) {
  for (let testEntry of TEST_ENTRIES) {
    delete currentHosts[testEntry.name];
  }
}

function getTestHosts() {
  let hosts = [];
  for (let testEntry of TEST_ENTRIES) {
    hosts.push({
      name: testEntry.name,
      maxAge: MINIMUM_REQUIRED_MAX_AGE,
      includeSubdomains: testEntry.includeSubdomains,
      error: ERROR_NONE,
      // This deliberately doesn't have a value for `retries` (because we should
      // never attempt to connect to this host).
      forceInclude: true,
    });
  }
  return hosts;
}

async function insertHosts(inoutHostList, inAddedHosts) {
  for (let host of inAddedHosts) {
    inoutHostList.push(host);
  }
}

function filterForcedInclusions(inHosts, outNotForced, outForced) {
  // Apply our filters (based on policy today) to determine which entries
  // will be included without being checked (forced); the others will be
  // checked using active probing.
  for (let host of inHosts) {
    if (
      host.policy == "google" ||
      host.policy == "public-suffix" ||
      host.policy == "public-suffix-requested"
    ) {
      host.forceInclude = true;
      host.error = ERROR_NONE;
      outForced.push(host);
    } else {
      outNotForced.push(host);
    }
  }
}

function output(statuses) {
  dump("INFO: Writing output to " + OUTPUT + "\n");
  try {
    let file = new FileUtils.File(
      PathUtils.join(Services.dirsvc.get("CurWorkD", Ci.nsIFile).path, OUTPUT)
    );
    let fos = FileUtils.openSafeFileOutputStream(file);
    writeTo(HEADER, fos);
    writeTo(getExpirationTimeString(), fos);

    writeTo(GPERF_DELIM, fos);

    for (let status of statuses) {
      let includeSubdomains = status.includeSubdomains ? 1 : 0;
      writeTo(status.name + ", " + includeSubdomains + "\n", fos);
    }

    writeTo(GPERF_DELIM, fos);
    FileUtils.closeSafeFileOutputStream(fos);
    dump("finished writing output file\n");
  } catch (e) {
    dump("ERROR: problem writing output to '" + OUTPUT + "': " + e + "\n");
    throw e;
  }
}

function errorToString(status) {
  return status.error == ERROR_MAX_AGE_TOO_LOW
    ? status.error + status.maxAge
    : status.error;
}

async function main(args) {
  if (args.length != 1) {
    throw new Error(
      "Usage: getHSTSPreloadList.js <absolute path to current nsSTSPreloadList.inc>"
    );
  }

  // get the current preload list
  let currentHosts = readCurrentList(args[0]);
  // delete any hosts we use in tests so we don't actually connect to them
  deleteTestHosts(currentHosts);
  // disable the current preload list so it won't interfere with requests we make
  Services.prefs.setBoolPref(
    "network.stricttransportsecurity.preloadlist",
    false
  );
  Services.prefs.setBoolPref("network.http.http3.enable", false);
  // download and parse the raw json file from the Chromium source
  let rawdata = await download();
  // get just the hosts with mode: "force-https"
  let hosts = getHosts(rawdata);
  // add hosts in the current list to the new list (avoiding duplicates)
  combineLists(hosts, currentHosts);

  // Don't contact hosts that are forced to be included anyway
  let hostsToContact = [];
  let forcedHosts = [];
  filterForcedInclusions(hosts, hostsToContact, forcedHosts);

  // Initialize the final status list
  let hstsStatuses = [];
  // Add the hosts we use in tests
  dump("Adding test hosts\n");
  insertHosts(hstsStatuses, getTestHosts());
  // Add in the hosts that are forced
  dump("Adding forced hosts\n");
  insertHosts(hstsStatuses, forcedHosts);

  let total = await probeHSTSStatuses(hostsToContact)
    .then(function (probedStatuses) {
      return hstsStatuses.concat(probedStatuses);
    })
    .then(function (statuses) {
      return statuses.sort(compareHSTSStatus);
    })
    .then(function (statuses) {
      for (let status of statuses) {
        // If we've encountered an error for this entry (other than the site not
        // sending an HSTS header), be safe and don't remove it from the list
        // (given that it was already on the list).
        if (
          !status.forceInclude &&
          status.error != ERROR_NONE &&
          status.error != ERROR_NO_HSTS_HEADER &&
          status.error != ERROR_MAX_AGE_TOO_LOW &&
          status.name in currentHosts
        ) {
          // dump("INFO: error connecting to or processing " + status.name + " - using previous status on list\n");
          status.maxAge = MINIMUM_REQUIRED_MAX_AGE;
          status.includeSubdomains = currentHosts[status.name];
        }
      }
      return statuses;
    })
    .then(function (statuses) {
      // Filter out entries we aren't including.
      var includedStatuses = statuses.filter(function (status) {
        if (status.maxAge < MINIMUM_REQUIRED_MAX_AGE && !status.forceInclude) {
          // dump("INFO: " + status.name + " NOT ON the preload list\n");
          return false;
        }

        // dump("INFO: " + status.name + " ON the preload list (includeSubdomains: " + status.includeSubdomains + ")\n");
        if (status.forceInclude && status.error != ERROR_NONE) {
          dump(
            status.name +
              ": " +
              errorToString(status) +
              " (error ignored - included regardless)\n"
          );
        }
        return true;
      });
      return includedStatuses;
    });

  // Write the output file
  output(total);

  dump("HSTS probing all done\n");
}

// arguments is a global within xpcshell
spinResolve(main(arguments));
