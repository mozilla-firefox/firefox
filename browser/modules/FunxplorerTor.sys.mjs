/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { FileUtils } from "resource://gre/modules/FileUtils.sys.mjs";
import {
  FunxplorerVpn,
  applySocksProxy,
  captureBrowserProxy,
  restoreBrowserProxy,
} from "resource:///modules/FunxplorerVpn.sys.mjs";

const TOPIC = "funxplorer-tor-changed";
const SOCKS_HOST_PREF = "funxplorer.tor.socksHost";
const SOCKS_PORT_PREF = "funxplorer.tor.socksPort";
const ENABLED_PREF = "funxplorer.tor.enabled";
const BINARY_PREF = "funxplorer.tor.binary";

let gProcess = null;
let gStartedProcess = false;
let gQuitObserver = false;

function socksHost() {
  return Services.prefs.getStringPref(SOCKS_HOST_PREF, "127.0.0.1");
}

function socksPort() {
  return Services.prefs.getIntPref(SOCKS_PORT_PREF, 9050);
}

function fileFromPath(path) {
  try {
    let file = new FileUtils.File(path);
    return file.exists() ? file : null;
  } catch {
    return null;
  }
}

function candidateBinaries() {
  let paths = [];
  let configured = Services.prefs.getStringPref(BINARY_PREF, "");
  if (configured) {
    paths.push(configured);
  }
  if (AppConstants.platform == "win") {
    let home = Services.dirsvc.get("Home", Ci.nsIFile).path;
    let localAppData = Services.env.get("LOCALAPPDATA");
    paths.push(
      home + "\\Desktop\\Tor Browser\\Browser\\TorBrowser\\Tor\\tor.exe",
      home +
        "\\OneDrive\\Desktop\\Tor Browser\\Browser\\TorBrowser\\Tor\\tor.exe"
    );
    if (localAppData) {
      paths.push(
        localAppData + "\\Tor Browser\\Browser\\TorBrowser\\Tor\\tor.exe"
      );
    }
    paths.push(
      "C:\\Program Files\\Tor Browser\\Browser\\TorBrowser\\Tor\\tor.exe",
      "C:\\Program Files (x86)\\Tor Browser\\Browser\\TorBrowser\\Tor\\tor.exe"
    );
  } else if (AppConstants.platform == "macosx") {
    paths.push(
      "/Applications/Tor Browser.app/Contents/MacOS/Tor/tor.real",
      "/usr/local/bin/tor",
      "/opt/homebrew/bin/tor"
    );
  } else {
    paths.push("/usr/bin/tor", "/usr/local/bin/tor");
  }
  return paths;
}

function findTorBinary() {
  for (let path of candidateBinaries()) {
    let file = fileFromPath(path);
    if (file) {
      return file;
    }
  }
  return null;
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

async function waitForSocks(host, port, timeoutMs) {
  let start = Date.now();
  while (Date.now() - start < timeoutMs) {
    if (await canConnect(host, port)) {
      return true;
    }
    await new Promise(resolve => setTimeout(resolve, 300));
  }
  return false;
}

async function startTor(port, vpn) {
  let binary = findTorBinary();
  if (!binary) {
    return {
      ok: false,
      error:
        "Tor was not found. Install Tor Browser or set funxplorer.tor.binary to tor.exe.",
    };
  }
  let dataDir = PathUtils.join(PathUtils.profileDir, "tor-funtor-data");
  await IOUtils.makeDirectory(dataDir, { ignoreExisting: true });
  let process = Cc["@mozilla.org/process/util;1"].createInstance(Ci.nsIProcess);
  process.init(binary);
  let args = [
    "--DataDirectory",
    dataDir,
    "SocksPort",
    "127.0.0.1:" + port,
    "HTTPSProxy",
    vpn.host + ":" + vpn.port,
    "HTTPSProxyAuthenticator",
    vpn.username + ":" + vpn.password,
    "--quiet",
  ];
  process.runw(false, args, args.length);
  gProcess = process;
  gStartedProcess = true;
  return { ok: true };
}

function stopTor() {
  if (gStartedProcess && gProcess) {
    try {
      if (gProcess.isRunning) {
        gProcess.kill();
      }
    } catch {}
  }
  gProcess = null;
  gStartedProcess = false;
}

function ensureQuitObserver() {
  if (gQuitObserver) {
    return;
  }
  gQuitObserver = true;
  Services.obs.addObserver(() => {
    FunxplorerTor.disable({ restoring: false, quitting: true });
  }, "quit-application");
}

export const FunxplorerTor = {
  TOPIC,

  get enabled() {
    return Services.prefs.getBoolPref(ENABLED_PREF, false);
  },

  async toggle() {
    return this.enabled ? this.disable() : this.enable();
  },

  async enable() {
    ensureQuitObserver();
    if (this.enabled) {
      return { ok: true };
    }
    if (FunxplorerVpn.enabled) {
      FunxplorerVpn.disable();
    }
    let vpn = await FunxplorerVpn.pickEndpoint();
    if (!vpn.ok) {
      return vpn;
    }
    let host = socksHost();
    let port = socksPort();
    if (!(gStartedProcess && (await canConnect(host, port)))) {
      stopTor();
      let started = await startTor(port, vpn);
      if (!started.ok) {
        return started;
      }
      let ready = await waitForSocks(host, port, 25000);
      if (!ready) {
        stopTor();
        return {
          ok: false,
          error: "FUNTOR started but the SOCKS port is not ready.",
        };
      }
    }
    captureBrowserProxy();
    applySocksProxy(host, port);
    Services.prefs.setBoolPref(ENABLED_PREF, true);
    Services.obs.notifyObservers(null, TOPIC, "on");
    return { ok: true };
  },

  disable({ quitting = false } = {}) {
    if (!this.enabled && !gStartedProcess) {
      return { ok: true };
    }
    stopTor();
    restoreBrowserProxy();
    Services.prefs.setBoolPref(ENABLED_PREF, false);
    if (!quitting) {
      Services.obs.notifyObservers(null, TOPIC, "off");
    }
    return { ok: true };
  },
};
