/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Need profile dir to store the key / cert
do_get_profile();
// Ensure PSM is initialized
Cc["@mozilla.org/psm;1"].getService(Ci.nsISupports);

const { MockRegistrar } = ChromeUtils.importESModule(
  "resource://testing-common/MockRegistrar.sys.mjs"
);
const certOverrideService = Cc[
  "@mozilla.org/security/certoverride;1"
].getService(Ci.nsICertOverrideService);
const socketTransportService = Cc[
  "@mozilla.org/network/socket-transport-service;1"
].getService(Ci.nsISocketTransportService);

const prefs = Services.prefs;

function areCertsEqual(certA, certB) {
  let derA = certA.getRawDER();
  let derB = certB.getRawDER();
  if (derA.length != derB.length) {
    return false;
  }
  for (let i = 0; i < derA.length; i++) {
    if (derA[i] != derB[i]) {
      return false;
    }
  }
  return true;
}

function startServer(
  cert,
  expectingPeerCert,
  clientCertificateConfig,
  expectedVersion,
  expectedVersionStr
) {
  let tlsServer = Cc["@mozilla.org/network/tls-server-socket;1"].createInstance(
    Ci.nsITLSServerSocket
  );
  tlsServer.init(-1, true, -1);
  tlsServer.serverCert = cert;

  let input, output;

  let listener = {
    onSocketAccepted(socket, transport) {
      info("Accept TLS client connection");
      let connectionInfo = transport.securityCallbacks.getInterface(
        Ci.nsITLSServerConnectionInfo
      );
      connectionInfo.setSecurityObserver(listener);
      input = transport.openInputStream(0, 0, 0);
      output = transport.openOutputStream(0, 0, 0);
    },
    onHandshakeDone(socket, status) {
      info("TLS handshake done");
      if (expectingPeerCert) {
        ok(!!status.peerCert, "Has peer cert");
        ok(
          areCertsEqual(status.peerCert, cert),
          "Peer cert matches expected cert"
        );
      } else {
        ok(!status.peerCert, "No peer cert (as expected)");
      }

      equal(
        status.tlsVersionUsed,
        expectedVersion,
        "Using " + expectedVersionStr
      );
      let expectedCipher;
      if (expectedVersion >= 772) {
        expectedCipher = "TLS_AES_128_GCM_SHA256";
      } else {
        expectedCipher = "TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256";
      }
      equal(status.cipherName, expectedCipher, "Using expected cipher");
      equal(status.keyLength, 128, "Using 128-bit key");
      equal(status.macLength, 128, "Using 128-bit MAC");

      input.asyncWait(
        {
          onInputStreamReady(input1) {
            NetUtil.asyncCopy(input1, output);
          },
        },
        0,
        0,
        Services.tm.currentThread
      );
    },
    onStopListening() {
      info("onStopListening");
      input.close();
      output.close();
    },
  };

  tlsServer.setSessionTickets(false);
  tlsServer.setRequestClientCertificate(clientCertificateConfig);

  tlsServer.asyncListen(listener);

  return tlsServer;
}

function storeCertOverride(port, cert) {
  certOverrideService.rememberValidityOverride(
    "127.0.0.1",
    port,
    {},
    cert,
    true
  );
}

function startClient(port, sendClientCert, expectingAlert, tlsVersion) {
  gClientAuthDialogService.selectCertificate = sendClientCert;
  let SSL_ERROR_BASE = Ci.nsINSSErrorsService.NSS_SSL_ERROR_BASE;
  let SSL_ERROR_BAD_CERT_ALERT = SSL_ERROR_BASE + 17;
  let SSL_ERROR_RX_CERTIFICATE_REQUIRED_ALERT = SSL_ERROR_BASE + 181;
  let transport = socketTransportService.createTransport(
    ["ssl"],
    "127.0.0.1",
    port,
    null,
    null
  );
  let input;
  let output;

  let inputDeferred = Promise.withResolvers();
  let outputDeferred = Promise.withResolvers();

  let handler = {
    onTransportStatus(transport1, status) {
      if (status === Ci.nsISocketTransport.STATUS_CONNECTED_TO) {
        output.asyncWait(handler, 0, 0, Services.tm.currentThread);
      }
    },

    onInputStreamReady(input1) {
      try {
        let data = NetUtil.readInputStreamToString(input1, input1.available());
        equal(data, "HELLO", "Echoed data received");
        input1.close();
        output.close();
        ok(!expectingAlert, "No cert alert expected");
        inputDeferred.resolve();
      } catch (e) {
        let errorCode = -1 * (e.result & 0xffff);
        if (expectingAlert) {
          if (
            tlsVersion == Ci.nsITLSClientStatus.TLS_VERSION_1_2 &&
            errorCode == SSL_ERROR_BAD_CERT_ALERT
          ) {
            info("Got bad cert alert as expected for tls 1.2");
            input1.close();
            output.close();
            inputDeferred.resolve();
            return;
          }
          if (
            tlsVersion == Ci.nsITLSClientStatus.TLS_VERSION_1_3 &&
            errorCode == SSL_ERROR_RX_CERTIFICATE_REQUIRED_ALERT
          ) {
            info("Got cert required alert as expected for tls 1.3");
            input1.close();
            output.close();
            inputDeferred.resolve();
            return;
          }
        }
        inputDeferred.reject(e);
      }
    },

    onOutputStreamReady(output1) {
      try {
        output1.write("HELLO", 5);
        info("Output to server written");
        outputDeferred.resolve();
        input = transport.openInputStream(0, 0, 0);
        input.asyncWait(handler, 0, 0, Services.tm.currentThread);
      } catch (e) {
        let errorCode = -1 * (e.result & 0xffff);
        if (errorCode == SSL_ERROR_BAD_CERT_ALERT) {
          info("Server doesn't like client cert");
        }
        outputDeferred.reject(e);
      }
    },
  };

  transport.setEventSink(handler, Services.tm.currentThread);
  output = transport.openOutputStream(0, 0, 0);

  return Promise.all([inputDeferred.promise, outputDeferred.promise]);
}

// Replace the UI dialog that prompts the user to pick a client certificate.
const gClientAuthDialogService = {
  _selectCertificate: false,

  set selectCertificate(value) {
    this._selectCertificate = value;
  },

  chooseCertificate(hostname, certArray, loadContext, caNames, callback) {
    if (this._selectCertificate) {
      callback.certificateChosen(certArray[0], false);
    } else {
      callback.certificateChose(null, false);
    }
  },

  QueryInterface: ChromeUtils.generateQI([Ci.nsIClientAuthDialogService]),
};

const ClientAuthDialogServiceContractID =
  "@mozilla.org/security/ClientAuthDialogService;1";
MockRegistrar.register(
  ClientAuthDialogServiceContractID,
  gClientAuthDialogService
);

const tests = [
  {
    expectingPeerCert: true,
    clientCertificateConfig: Ci.nsITLSServerSocket.REQUIRE_ALWAYS,
    sendClientCert: true,
    expectingAlert: false,
  },
  {
    expectingPeerCert: true,
    clientCertificateConfig: Ci.nsITLSServerSocket.REQUIRE_ALWAYS,
    sendClientCert: false,
    expectingAlert: true,
  },
  {
    expectingPeerCert: true,
    clientCertificateConfig: Ci.nsITLSServerSocket.REQUEST_ALWAYS,
    sendClientCert: true,
    expectingAlert: false,
  },
  {
    expectingPeerCert: false,
    clientCertificateConfig: Ci.nsITLSServerSocket.REQUEST_ALWAYS,
    sendClientCert: false,
    expectingAlert: false,
  },
  {
    expectingPeerCert: false,
    clientCertificateConfig: Ci.nsITLSServerSocket.REQUEST_NEVER,
    sendClientCert: true,
    expectingAlert: false,
  },
  {
    expectingPeerCert: false,
    clientCertificateConfig: Ci.nsITLSServerSocket.REQUEST_NEVER,
    sendClientCert: false,
    expectingAlert: false,
  },
];

const versions = [
  {
    prefValue: 3,
    version: Ci.nsITLSClientStatus.TLS_VERSION_1_2,
    versionStr: "TLS 1.2",
  },
  {
    prefValue: 4,
    version: Ci.nsITLSClientStatus.TLS_VERSION_1_3,
    versionStr: "TLS 1.3",
  },
];

add_task(async function () {
  let cert = await getTestServerCertificate();
  ok(!!cert, "Got self-signed cert");
  for (let v of versions) {
    prefs.setIntPref("security.tls.version.max", v.prefValue);
    for (let t of tests) {
      let server = startServer(
        cert,
        t.expectingPeerCert,
        t.clientCertificateConfig,
        v.version,
        v.versionStr
      );
      storeCertOverride(server.port, cert);
      await startClient(
        server.port,
        t.sendClientCert,
        t.expectingAlert,
        v.version
      );
      server.close();
    }
  }
});

add_task(async function test_tcpsocket_certificate_error() {
  const server = startServer(
    await getTestServerCertificate(),
    false,
    Ci.nsITLSServerSocket.REQUEST_NEVER
  );
  try {
    const socket = new TCPSocket("127.0.0.1", server.port, {
      useSecureTransport: true,
    });
    socket.onopen = () =>
      Assert.ok(false, "An invalid certificate must not open");
    const error = await new Promise(resolve => (socket.onerror = resolve));
    Assert.equal(error.message, "SecurityCertificate");
  } finally {
    server.close();
  }
});

registerCleanupFunction(function () {
  prefs.clearUserPref("security.tls.version.max");
});
