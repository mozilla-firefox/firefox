/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const TOPIC = "funxplorer-vpn-changed";
const ENABLED_PREF = "funxplorer.vpn.enabled";
const LOCATION_PREF = "funxplorer.vpn.location";
const USER_PREF = "funxplorer.vpn.username";
const PASS_PREF = "funxplorer.vpn.password";

const PROXY_PREFS = [
  "network.proxy.type",
  "network.proxy.http",
  "network.proxy.http_port",
  "network.proxy.ssl",
  "network.proxy.ssl_port",
  "network.proxy.socks",
  "network.proxy.socks_port",
  "network.proxy.socks_version",
  "network.proxy.socks_remote_dns",
  "network.proxy.share_proxy_settings",
  "network.proxy.autoconfig_url",
  "network.proxy.no_proxies_on",
  "network.dns.blockDotOnion",
  "media.peerconnection.ice.proxy_only",
];

const FREE_LOCATIONS = {
  ams: {
    city: "Amsterdam",
    hosts: [
      { hostname: "free-amsterdam-https-5.cloudflingcdn.com", port: 443 },
      { hostname: "free-amsterdam-https-1.cloudburstcdn.com", port: 443 },
      { hostname: "free-amsterdam-https-6.cloudflingcdn.com", port: 443 },
      { hostname: "free-amsterdam-https-3.cloudflaracdn.com", port: 443 },
      { hostname: "free-amsterdam-https-4.cloudflaracdn.com", port: 443 },
      { hostname: "free-amsterdam-https-2.cloudflingcdn.com", port: 443 },
    ],
  },
  sgp: {
    city: "Singapore",
    hosts: [
      { hostname: "free-singapore-https-3.weathercloudapp.com", port: 443 },
      { hostname: "free-singapore-https-2.cloudtimecdn.com", port: 443 },
      { hostname: "free-singapore-https-1.cloudburstcdn.com", port: 443 },
      { hostname: "free-singapore-https-4.cloudflingcdn.com", port: 443 },
    ],
  },
  lax: {
    city: "Los Angeles",
    hosts: [
      { hostname: "free-los-angeles-https-3.cloudflaracdn.com", port: 443 },
      { hostname: "free-los-angeles-https-1.cloudburstcdn.com", port: 443 },
      { hostname: "free-los-angeles-https-2.cloudtimecdn.com", port: 443 },
      { hostname: "free-los-angeles-https-4.cloudflaracdn.com", port: 443 },
      { hostname: "free-los-angeles-https-5.cloudflingcdn.com", port: 443 },
      { hostname: "usa-west-free-https-1.weathercloudapp.com", port: 443 },
    ],
  },
};

const DIRECT_HOSTS = [
  "localhost",
  "127.0.0.1",
  "1vpn.org",
  "1vpn.co",
  "onevpn.com",
  "cloudlogcdn.com",
  "cloudflaircdn.com",
];

let gSavedProxy = null;
let gQuitObserver = false;

function username() {
  return Services.prefs.getStringPref(USER_PREF, "a2epfq5ugq0u");
}

function password() {
  return Services.prefs.getStringPref(PASS_PREF, "ptkx3fqg6v7n");
}

function locationId() {
  let id = Services.prefs.getStringPref(LOCATION_PREF, "ams");
  return FREE_LOCATIONS[id] ? id : "ams";
}

function canConnect(host, port, timeoutSec = 2) {
  return new Promise(resolve => {
    let transport;
    let stream;
    let finished = false;
    let finish = ok => {
      if (finished) {
        return;
      }
      finished = true;
      try {
        stream?.close();
      } catch {}
      try {
        transport?.close(Cr.NS_OK);
      } catch {}
      resolve(ok);
    };
    try {
      transport = Services.socketTransportService.createTransport(
        [],
        host,
        port,
        null,
        null
      );
      transport.setTimeout(Ci.nsISocketTransport.TIMEOUT_CONNECT, timeoutSec);
      stream = transport.openOutputStream(0, 0, 0);
      stream.asyncWait(
        {
          onOutputStreamReady() {
            finish(true);
          },
        },
        0,
        0,
        Services.tm.currentThread
      );
    } catch {
      finish(false);
      return;
    }
    setTimeout(() => finish(false), timeoutSec * 1000 + 250);
  });
}

function readPref(name) {
  if (!Services.prefs.prefHasUserValue(name)) {
    return null;
  }
  let type = Services.prefs.getPrefType(name);
  return {
    type,
    value:
      type == Services.prefs.PREF_INT
        ? Services.prefs.getIntPref(name)
        : type == Services.prefs.PREF_BOOL
          ? Services.prefs.getBoolPref(name)
          : Services.prefs.getStringPref(name),
  };
}

export function captureBrowserProxy() {
  if (gSavedProxy) {
    return;
  }
  gSavedProxy = {};
  for (let name of PROXY_PREFS) {
    gSavedProxy[name] = readPref(name);
  }
}

export function restoreBrowserProxy() {
  if (!gSavedProxy) {
    Services.prefs.clearUserPref("network.proxy.type");
    Services.prefs.clearUserPref("network.proxy.autoconfig_url");
    Services.prefs.clearUserPref("network.proxy.socks");
    Services.prefs.clearUserPref("network.proxy.socks_port");
    Services.prefs.clearUserPref("network.proxy.http");
    Services.prefs.clearUserPref("network.proxy.ssl");
    return;
  }
  for (let [name, saved] of Object.entries(gSavedProxy)) {
    if (!saved) {
      Services.prefs.clearUserPref(name);
      continue;
    }
    if (saved.type == Services.prefs.PREF_INT) {
      Services.prefs.setIntPref(name, saved.value);
    } else if (saved.type == Services.prefs.PREF_BOOL) {
      Services.prefs.setBoolPref(name, saved.value);
    } else {
      Services.prefs.setStringPref(name, saved.value);
    }
  }
  gSavedProxy = null;
}

export function applySocksProxy(host, port) {
  Services.prefs.setIntPref("network.proxy.type", 1);
  Services.prefs.clearUserPref("network.proxy.autoconfig_url");
  Services.prefs.setCharPref("network.proxy.socks", host);
  Services.prefs.setIntPref("network.proxy.socks_port", port);
  Services.prefs.setIntPref("network.proxy.socks_version", 5);
  Services.prefs.setBoolPref("network.proxy.socks_remote_dns", true);
  Services.prefs.setBoolPref("network.proxy.share_proxy_settings", false);
  Services.prefs.setCharPref("network.proxy.http", "");
  Services.prefs.setCharPref("network.proxy.ssl", "");
  Services.prefs.setIntPref("network.proxy.http_port", 0);
  Services.prefs.setIntPref("network.proxy.ssl_port", 0);
  Services.prefs.setBoolPref("network.dns.blockDotOnion", false);
  Services.prefs.setBoolPref("media.peerconnection.ice.proxy_only", true);
}

async function storeProxyLogin(host, port) {
  let origin = "moz-proxy://" + host + ":" + port;
  let realm = host + ":" + port;
  try {
    let existing = await Services.logins.searchLoginsAsync({ origin });
    for (let login of existing) {
      Services.logins.removeLogin(login);
    }
  } catch {}
  let login = Cc["@mozilla.org/login-manager/loginInfo;1"].createInstance(
    Ci.nsILoginInfo
  );
  login.init(origin, null, realm, username(), password(), "", "");
  try {
    await Services.logins.addLoginAsync(login);
  } catch {
    try {
      Services.logins.addLogin(login);
    } catch {}
  }
}

function pacForHosts(hosts) {
  let list = hosts
    .map(host => "HTTPS " + host.hostname + ":" + host.port)
    .join("; ");
  let skip = DIRECT_HOSTS.map(
    host => 'dnsDomainIs(host, "' + host + '") || host == "' + host + '"'
  ).join(" || ");
  return (
    "function FindProxyForURL(url, host) {\n" +
    "  if (shExpMatch(host, '*.local') || " +
    skip +
    ") { return 'DIRECT'; }\n" +
    '  return "' +
    list +
    '";\n' +
    "}\n"
  );
}

async function applyPac(hosts) {
  for (let host of hosts) {
    await storeProxyLogin(host.hostname, host.port);
  }
  let pacPath = PathUtils.join(PathUtils.profileDir, "funvpn.pac");
  await IOUtils.writeUTF8(pacPath, pacForHosts(hosts));
  Services.prefs.setCharPref(
    "network.proxy.autoconfig_url",
    PathUtils.toFileURI(pacPath)
  );
  Services.prefs.setIntPref("network.proxy.type", 2);
  Services.prefs.setCharPref(
    "network.proxy.no_proxies_on",
    "localhost, 127.0.0.1, ::1"
  );
  Services.prefs.setBoolPref("media.peerconnection.ice.proxy_only", true);
  try {
    await fetch("https://cloudflaircdn.com/proxy_auth/");
  } catch {}
}

function ensureQuitObserver() {
  if (gQuitObserver) {
    return;
  }
  gQuitObserver = true;
  Services.obs.addObserver(() => {
    FunxplorerVpn.disable({ quitting: true });
  }, "quit-application");
}

export const FunxplorerVpn = {
  TOPIC,

  get enabled() {
    return Services.prefs.getBoolPref(ENABLED_PREF, false);
  },

  async pickEndpoint() {
    let order = [locationId(), "ams", "sgp", "lax"];
    let seen = new Set();
    for (let id of order) {
      if (seen.has(id) || !FREE_LOCATIONS[id]) {
        continue;
      }
      seen.add(id);
      let hosts = [...FREE_LOCATIONS[id].hosts].sort(() => Math.random() - 0.5);
      for (let host of hosts) {
        if (await canConnect(host.hostname, host.port)) {
          return {
            ok: true,
            host: host.hostname,
            port: host.port,
            username: username(),
            password: password(),
            hosts,
            city: FREE_LOCATIONS[id].city,
          };
        }
      }
    }
    return {
      ok: false,
      error: "FUNVPN could not reach a 1VPN server.",
    };
  },

  async toggle() {
    return this.enabled ? this.disable() : this.enable();
  },

  async enable() {
    ensureQuitObserver();
    if (this.enabled) {
      return { ok: true };
    }
    let { FunxplorerTor } = ChromeUtils.importESModule(
      "resource:///modules/FunxplorerTor.sys.mjs"
    );
    if (FunxplorerTor.enabled) {
      FunxplorerTor.disable();
    }
    let endpoint = await this.pickEndpoint();
    if (!endpoint.ok) {
      return endpoint;
    }
    captureBrowserProxy();
    await applyPac(endpoint.hosts);
    Services.prefs.setBoolPref(ENABLED_PREF, true);
    Services.obs.notifyObservers(null, TOPIC, "on");
    return { ok: true, city: endpoint.city };
  },

  disable({ quitting = false } = {}) {
    if (!this.enabled) {
      return { ok: true };
    }
    restoreBrowserProxy();
    Services.prefs.setBoolPref(ENABLED_PREF, false);
    if (!quitting) {
      Services.obs.notifyObservers(null, TOPIC, "off");
    }
    return { ok: true };
  },
};
