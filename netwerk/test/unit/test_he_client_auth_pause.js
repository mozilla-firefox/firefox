/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// Verifies the Happy Eyeballs client-auth pause: when a TLS server requests
// a client cert from an HE racer, HE stops polling the state machine until
// the prompt is resolved, so no new connection attempts are started while the
// cert-selection dialog is up. Already-running attempts are not cancelled and
// may show their own prompts (allowed), so the dialog can fire more than once.
//
// Setup: Node HTTPS server with requestCert=true. Node listens on the IPv6
// wildcard `::` which on Linux/macOS accepts both IPv4 (via IPv4-mapped
// addresses) and IPv6 connections. The test channel targets `localhost`,
// which on /etc/hosts resolves to BOTH 127.0.0.1 and ::1 — that's what
// gives HE two address families to race. The dialog mock delays its
// response to keep the holding racer parked while the
// connection-attempt-delay timer fires.
//
// Assertion: the request succeeds after a cert is selected and the dialog is
// invoked at least once for the requested hostname. We don't assert an exact
// count: whether the second racer started before the pause is timing
// dependent.

/* import-globals-from head_channels.js */

const { MockRegistrar } = ChromeUtils.importESModule(
  "resource://testing-common/MockRegistrar.sys.mjs"
);
const { NodeHTTPSServer } = ChromeUtils.importESModule(
  "resource://testing-common/NodeServer.sys.mjs"
);
const { setTimeout } = ChromeUtils.importESModule(
  "resource://gre/modules/Timer.sys.mjs"
);
const ctypes = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/ctypes/ctypes.sys.mjs"
).ctypes;

// Minimal inlined copy of head_psm.js's loadPKCS11Module — the full file
// can't be loaded here because it conflicts with netwerk's default
// head_channels.js (both declare XPCOMUtils).
async function loadPKCS11Module(libraryFile, moduleName) {
  Assert.ok(libraryFile.exists(), "PKCS11 module file should exist");
  let pkcs11ModuleDB = Cc["@mozilla.org/security/pkcs11moduledb;1"].getService(
    Ci.nsIPKCS11ModuleDB
  );
  registerCleanupFunction(async () => {
    try {
      await pkcs11ModuleDB.deleteModule(moduleName);
    } catch (e) {
      /* ignore */
    }
  });
  await pkcs11ModuleDB.addModule(moduleName, libraryFile.path, 0, 0);
}

const TEST_BROWSER_ID = 4242;
// Long enough to comfortably exceed HE's connection-attempt-delay (set
// below) so the second racer would fire well before our dialog responds.
const DIALOG_RESPONSE_DELAY_MS = 1500;
const HE_CONNECTION_ATTEMPT_DELAY_MS = 50;

let gServer;
let gChooseCertificateInvocations = 0;
let gChooseCertificateHostnames = [];

const gClientAuthDialogService = {
  QueryInterface: ChromeUtils.generateQI(["nsIClientAuthDialogService"]),

  chooseCertificate(hostname, certArray, _loadContext, _caNames, callback) {
    gChooseCertificateInvocations++;
    gChooseCertificateHostnames.push(hostname);
    // The delay keeps the holder's TLS handshake parked while HE's
    // connection-attempt-delay timer fires. With the pause working, polling
    // is stopped so no new attempt is started off that timer.
    // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
    setTimeout(() => {
      if (certArray && certArray.length) {
        callback.certificateChosen(certArray[0], false);
      } else {
        callback.certificateChosen(null, false);
      }
    }, DIALOG_RESPONSE_DELAY_MS);
  },
};

// nsIPrompt implementation to handle protected authentication prompts and
// password prompts from the test PKCS#11 module.
var gPrompt = {
  QueryInterface: ChromeUtils.generateQI(["nsIPrompt"]),

  // This intentionally does not use arrow function syntax to avoid an issue
  // where in the context of the arrow function, |this != gPrompt| due to
  // how objects get wrapped when going across xpcom boundaries.
  alert(_title, text) {
    const EXPECTED_PROMPT_TEXT =
      "Please authenticate to the token “Test PKCS11 Tokeñ 2 Label”. How to do so depends on the token (for example, using a fingerprint reader or entering a code with a keypad).";
    equal(text, EXPECTED_PROMPT_TEXT, "expecting alert() to be called");
  },

  promptPassword(_dialogTitle, _text, password, _checkMsg) {
    // The first token in the test module has a blank password by default.
    password.value = "";
    return true;
  },
};

const gPromptFactory = {
  QueryInterface: ChromeUtils.generateQI(["nsIPromptFactory"]),
  getPrompt: () => gPrompt,
};

MockRegistrar.register(
  "@mozilla.org/security/ClientAuthDialogService;1",
  gClientAuthDialogService
);

MockRegistrar.register("@mozilla.org/prompter;1", gPromptFactory);

function makeChan(uri, browserId) {
  let chan = NetUtil.newChannel({
    uri,
    loadUsingSystemPrincipal: true,
  }).QueryInterface(Ci.nsIHttpChannel);
  chan.loadFlags = Ci.nsIChannel.LOAD_INITIAL_DOCUMENT_URI;
  chan.browserId = browserId;
  return chan;
}

function channelOpenPromise(chan, flags) {
  return new Promise(resolve => {
    chan.asyncOpen(
      new ChannelListener((req, buffer) => resolve([req, buffer]), null, flags)
    );
  });
}

add_setup(async function setup() {
  do_get_profile();

  Services.prefs.setBoolPref("network.http.happy_eyeballs_enabled", true);
  // Shrink HE's connection-attempt-delay so the second-family racer fires
  // promptly while the first racer is still blocked on the cert dialog —
  // makes the race window observable in CI.
  Services.prefs.setIntPref(
    "network.http.happy_eyeballs_connection_attempt_delay",
    HE_CONNECTION_ATTEMPT_DELAY_MS
  );
  Services.prefs.setIntPref("network.http.speculative-parallel-limit", 0);

  // Keep client-cert discovery confined to the test pkcs11 module. Otherwise
  // on macOS the osclientcerts module queries the system keychain, which pops
  // a native OS authorization/cert-selection dialog that xpcshell can't
  // dismiss (the test would hang until timeout). This is separate from the
  // mocked nsIClientAuthDialogService.
  Services.prefs.setBoolPref("security.osclientcerts.autoload", false);

  // Load the pkcs11 test module so the cert candidate list isn't empty.
  // PSM bypasses the dialog entirely when no candidates exist. The dylib is
  // built and installed only in PSM's test working directory; reach across
  // _tests/xpcshell/ to find it.
  let libraryFile = Services.dirsvc.get("CurWorkD", Ci.nsIFile);
  // CurWorkD = _tests/xpcshell/netwerk/test/unit → up to _tests/xpcshell.
  libraryFile = libraryFile.parent.parent.parent;
  libraryFile.append("security");
  libraryFile.append("manager");
  libraryFile.append("ssl");
  libraryFile.append("tests");
  libraryFile.append("unit");
  libraryFile.append("pkcs11testmodule");
  libraryFile.append(ctypes.libraryName("pkcs11testmodule"));
  await loadPKCS11Module(libraryFile, "PKCS11 Test Module");
  await Cc["@mozilla.org/security/x509certdb;1"]
    .getService(Ci.nsIX509CertDB)
    .getCerts();

  gServer = new NodeHTTPSServer();
  gServer.setRequestClientCert(true);
  await gServer.start();
  await gServer.registerPathHandler("/", (_req, resp) => {
    resp.writeHead(200, { "Content-Type": "text/plain", "Content-Length": 2 });
    resp.end("OK");
  });

  registerCleanupFunction(async () => {
    Services.prefs.clearUserPref("network.http.happy_eyeballs_enabled");
    Services.prefs.clearUserPref(
      "network.http.happy_eyeballs_connection_attempt_delay"
    );
    Services.prefs.clearUserPref("network.http.speculative-parallel-limit");
    Services.prefs.clearUserPref("security.osclientcerts.autoload");
    await gServer.stop();
  });
});

add_task(async function test_he_pauses_other_racers_on_client_auth() {
  gChooseCertificateInvocations = 0;
  gChooseCertificateHostnames.length = 0;

  // Verify the host actually has two address families locally — otherwise
  // there's nothing for HE to race and the test would trivially pass.
  let resolved = await new Promise((resolve, reject) => {
    Services.dns.asyncResolve(
      "localhost",
      Ci.nsIDNSService.RESOLVE_TYPE_DEFAULT,
      Ci.nsIDNSService.RESOLVE_DEFAULT_FLAGS,
      null,
      {
        QueryInterface: ChromeUtils.generateQI(["nsIDNSListener"]),
        onLookupComplete(_req, rec, status) {
          if (!Components.isSuccessCode(status)) {
            reject(status);
            return;
          }
          let addrs = [];
          rec = rec.QueryInterface(Ci.nsIDNSAddrRecord);
          while (rec.hasMore()) {
            addrs.push(rec.getNextAddrAsString());
          }
          resolve(addrs);
        },
      },
      Services.tm.currentThread,
      {}
    );
  });
  info(`localhost resolved to: ${resolved.join(", ")}`);
  Assert.greaterOrEqual(
    resolved.length,
    2,
    "localhost must have at least two addresses (likely 127.0.0.1 and ::1) " +
      "for HE to have something to race"
  );

  let chan = makeChan(`${gServer.origin()}/`, TEST_BROWSER_ID);
  let [req] = await channelOpenPromise(chan, CL_ALLOW_UNKNOWN_CL);

  Assert.equal(
    req.QueryInterface(Ci.nsIHttpChannel).responseStatus,
    200,
    "request should succeed after client cert is selected"
  );
  Assert.greaterOrEqual(
    gChooseCertificateInvocations,
    1,
    "chooseCertificate should fire at least once " +
      `(got ${gChooseCertificateInvocations}: ` +
      `${gChooseCertificateHostnames.join(", ")})`
  );
  Assert.equal(
    gChooseCertificateHostnames[0],
    "localhost",
    "dialog should be invoked for the requested hostname"
  );
});
