# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from marionette_harness import MarionetteTestCase

# add this directory to the path
sys.path.append(os.path.dirname(__file__))

from ssl_tokens_cache_mixin import SSLTokensCacheMixin

ZERORTT_PORT = 8443
H1_HOST = "0rtt-accept-h1.example.com"
H2_HOST = "0rtt-accept-h2.example.com"

PBM_OA = {"privateBrowsingId": 1}

# Opens an HTTPS request and returns {status, resumed, protocol}.
# Must run in chrome context (needs Cc/Ci and securityInfo access).
OPEN_CONNECTION_SCRIPT = """
const [url, originAttributes, resolve] = arguments;
const { NetUtil } = ChromeUtils.importESModule(
  "resource://gre/modules/NetUtil.sys.mjs"
);
const channel = NetUtil.newChannel({ uri: url, loadUsingSystemPrincipal: true })
                       .QueryInterface(Ci.nsIHttpChannel);
channel.loadFlags |= Ci.nsIRequest.LOAD_BYPASS_CACHE |
                     Ci.nsIRequest.INHIBIT_CACHING;
if (originAttributes) {
  channel.loadInfo.originAttributes = originAttributes;
}

channel.asyncOpen({
  QueryInterface: ChromeUtils.generateQI(["nsIStreamListener",
                                          "nsIRequestObserver"]),
  onStartRequest() {},
  onDataAvailable(_req, stream, _offset, count) {
    const bs = Cc["@mozilla.org/binaryinputstream;1"].createInstance(
      Ci.nsIBinaryInputStream
    );
    bs.setInputStream(stream);
    bs.readBytes(count);
  },
  onStopRequest(_req, _status) {
    let status = null;
    let resumed = false;
    let protocol = null;
    try { status = channel.responseStatus; } catch (_) {}
    try { protocol = channel.protocolVersion; } catch (_) {}
    try { resumed = channel.securityInfo.resumed; } catch (_) {}
    resolve({ status, resumed, protocol });
  },
});
"""

# Returns the number of SSL tokens in the in-process cache.
# Must run in chrome context.
TOKEN_COUNT_SCRIPT = """
const [resolve] = arguments;
const cache = Cc["@mozilla.org/network/ssl-tokens-cache;1"].getService(
  Ci.nsISSLTokensCacheTest
);
resolve(cache.countSSLTokens());
"""

# Returns the non-empty network.ssl_token_resumption cells as
# {"key/outcome": count}. Must run in chrome context.
TOKEN_RESUMPTION_SCRIPT = """
const counts = {};
for (const key of ["tcp_session", "tcp_restored", "h3_session", "h3_restored"]) {
  for (const outcome of ["resumed", "not_resumed", "rejected", "failed"]) {
    const value = Glean.network.sslTokenResumption.get(key, outcome).testGetValue();
    if (value !== null) {
      counts[`${key}/${outcome}`] = value;
    }
  }
}
return counts;
"""

CLEAR_HOST_SCRIPT = """
const [host, resolve] = arguments;
Services.clearData.deleteDataFromHost(
  host,
  true,
  Ci.nsIClearDataService.CLEAR_TLS_TOKEN_CACHE,
  { onDataDeleted: failedFlags => resolve(failedFlags) }
);
"""

_COMMON_PREFS = {
    "network.dns.disableIPv6": True,
    # Tokens are single-use, so extra connection attempts would consume them.
    "network.http.happy_eyeballs_enabled": False,
    "network.http.speculative-parallel-limit": 0,
    "privacy.sanitize.sanitizeOnShutdown": False,
    "privacy.clearOnShutdown.cache": False,
}


def _find_binary(marionette, name):
    """Return the path to a test utility binary, or None if not found."""
    exe = name + (".exe" if sys.platform in ("win32", "cygwin") else "")

    # Local build: on macOS the Firefox binary lives in the app bundle
    # (dist/NightlyDebug.app/Contents/MacOS/firefox); test utilities live
    # in dist/bin alongside the dylibs.
    ff_bin_dir = os.path.dirname(marionette.bin)
    if sys.platform == "darwin" and ".app/Contents/MacOS" in ff_bin_dir:
        dist_dir = os.path.dirname(os.path.dirname(os.path.dirname(ff_bin_dir)))
        candidate = os.path.join(dist_dir, "bin", exe)
    else:
        candidate = os.path.join(ff_bin_dir, exe)

    if os.path.isfile(candidate):
        return candidate

    # CI / test archive: walk up the directory tree to find bin/<exe>.
    # TEST_HARNESS_BINS land in bin/ at the test-archive root, which may
    # be several levels above the test file.
    d = Path(os.path.dirname(__file__))
    while d != d.parent:
        candidate = d / "bin" / exe
        if candidate.is_file():
            return str(candidate)
        d = d.parent

    return None


def _server_env(marionette):
    env = os.environ.copy()
    lib_dir = os.path.dirname(marionette.bin)
    if sys.platform == "darwin":
        env["DYLD_LIBRARY_PATH"] = lib_dir
    elif sys.platform.startswith("linux"):
        env["LD_LIBRARY_PATH"] = lib_dir
    elif sys.platform in ("win32", "cygwin"):
        env["PATH"] = os.path.pathsep.join([lib_dir, env.get("PATH", "")])
    return env


class TLSTokenRestartTests(SSLTokensCacheMixin):
    """Restart tests shared by all protocols; subclasses start the server."""

    HOST = None
    PORT = None
    # Another host on the same server, used as a control when clearing data.
    CONTROL_HOST = None
    # Delay after server start before the server accepts 0-RTT.
    ANTI_REPLAY_DELAY = 0

    def setUp(self):
        super().setUp()
        self.tmpdir = tempfile.mkdtemp()
        try:
            self.start_server()
            self.server_started = time.monotonic()
            self.marionette.set_prefs({
                **_COMMON_PREFS,
                **self.client_prefs(),
                "network.ssl_tokens_cache_persistence": True,
            })
            self.cache_file = (
                Path(self.marionette.profile_path) / "ssl_tokens_cache.sqlite"
            )
            self._import_ca_cert(self.ca_pem())
        except Exception:
            # tearDown() doesn't run when setUp() fails.
            self.stop_server()
            shutil.rmtree(self.tmpdir, ignore_errors=True)
            raise

    def tearDown(self):
        try:
            self.stop_server()
        finally:
            shutil.rmtree(self.tmpdir, ignore_errors=True)
            self.marionette.restart(in_app=False, clean=True)
            super().tearDown()

    def start_server(self):
        raise NotImplementedError

    def stop_server(self):
        raise NotImplementedError

    def client_prefs(self):
        return {}

    def ca_pem(self):
        raise NotImplementedError

    def url(self, host=None):
        return f"https://{host or self.HOST}:{self.PORT}/"

    def expected_protocol(self, host):
        raise NotImplementedError

    def reset_early_data_counts(self):
        pass

    def early_data_counts(self):
        """{"early": n, "std": m} request counts, or None if not reported."""
        return None

    def _import_ca_cert(self, ca_pem_path):
        ca_pem = Path(ca_pem_path).read_text()
        b64 = "".join(
            line for line in ca_pem.splitlines() if not line.startswith("-----")
        )
        with self.marionette.using_context("chrome"):
            self.marionette.execute_script(
                """
                const [b64] = arguments;
                const certdb = Cc["@mozilla.org/security/x509certdb;1"].getService(
                  Ci.nsIX509CertDB
                );
                certdb.addCertFromBase64(b64, "CTu,u,u");
                """,
                script_args=(b64,),
            )

    def _open_connection(self, host=None, origin_attributes=None):
        with self.marionette.using_context("chrome"):
            return self.marionette.execute_async_script(
                OPEN_CONNECTION_SCRIPT,
                script_args=(self.url(host), origin_attributes),
                script_timeout=30_000,
            )

    def _reset_telemetry(self):
        with self.marionette.using_context("chrome"):
            self.marionette.execute_script("Services.fog.testResetFOG();")

    def _token_resumption(self):
        with self.marionette.using_context("chrome"):
            return self.marionette.execute_script(TOKEN_RESUMPTION_SCRIPT)

    def fetch(self, host=None, origin_attributes=None):
        r = self._open_connection(host, origin_attributes)
        self.assertIsNotNone(r, "Connection script must resolve")
        self.assertEqual(r["status"], 200, f"Request to {self.url(host)} failed")
        self.assertEqual(r["protocol"], self.expected_protocol(host or self.HOST))
        return r

    def close_connections(self):
        """Closes all connections; the test servers handle one at a time."""
        with self.marionette.using_context("chrome"):
            self.marionette.execute_script(
                'Services.obs.notifyObservers(null, "net:cancel-all-connections");'
            )

    def _token_count(self):
        with self.marionette.using_context("chrome"):
            return self.marionette.execute_async_script(TOKEN_COUNT_SCRIPT)

    def _wait_for_tokens(self, min_count=1, timeout=30):
        count = self._wait_for(self._token_count, lambda c: c >= min_count, 0, timeout)
        return count >= min_count

    def _host_keys(self, host=None):
        tag = f"{host or self.HOST}:{self.PORT}"
        return [k for k in (self.cache_keys(self.cache_file) or []) if tag in k]

    def _wait_for_host_rows(self, present, host=None, timeout=5):
        """Polls until rows for host exist (or are gone); returns their keys."""
        return self._wait_for(
            lambda: self._host_keys(host),
            lambda keys: bool(keys) == present,
            [],
            timeout,
        )

    def _wait_for_anti_replay(self):
        remaining = self.server_started + self.ANTI_REPLAY_DELAY - time.monotonic()
        if remaining > 0:
            time.sleep(remaining)

    def first_connection(self, host=None, origin_attributes=None):
        """Full handshake whose token lands in the in-memory cache."""
        before = self._token_count()
        self._reset_telemetry()
        r = self.fetch(host, origin_attributes)
        self.assertFalse(r["resumed"], "First connection should not resume")
        self.assertEqual(self._token_resumption(), {})
        self.assertTrue(
            self._wait_for_tokens(before + 1),
            "SSL token must be cached after the first handshake",
        )
        self.close_connections()

    def assert_resumes(self, host=None):
        self._wait_for_anti_replay()
        self.reset_early_data_counts()
        self._reset_telemetry()
        r = self.fetch(host)
        self.assertTrue(r["resumed"], "Connection must resume via the disk token")
        self.assertEqual(self._token_resumption(), {"tcp_restored/resumed": 1})
        counts = self.early_data_counts()
        if counts is not None:
            self.assertEqual(
                counts, {"early": 1, "std": 0}, "Request must be accepted 0-RTT"
            )
        self.close_connections()

    def assert_does_not_resume(self, host=None, origin_attributes=None):
        self._wait_for_anti_replay()
        self.reset_early_data_counts()
        self._reset_telemetry()
        r = self.fetch(host, origin_attributes)
        self.assertFalse(r["resumed"], "Connection must not resume")
        self.assertEqual(self._token_resumption(), {})
        counts = self.early_data_counts()
        if counts is not None:
            self.assertEqual(counts, {"early": 0, "std": 1})
        self.close_connections()

    def disable_persistence(self):
        """Turns persistence off; ReconcilePersistence() clears the in-memory
        cache and deletes the DB file."""
        self.marionette.set_pref("network.ssl_tokens_cache_persistence", False)
        self.assertTrue(
            self.wait_for_cache_file_removed(self.cache_file),
            "DB file must be deleted",
        )

    def restart_and_check_persisted(self, hosts):
        """Restarts, so the shutdown write persists the tokens, and checks
        that each host has a row and its token is reloaded."""
        self.marionette.restart(in_app=True, clean=False)
        for host in hosts:
            self.assertTrue(
                self._wait_for_host_rows(True, host),
                f"ssl_tokens_cache.sqlite must contain a row for {host}",
            )
        self.assertTrue(
            self._wait_for_tokens(len(hosts)),
            "Tokens must be reloaded from disk after restart",
        )

    def test_resumption_after_restart(self):
        self.first_connection()
        self.restart_and_check_persisted([self.HOST])
        self.assert_resumes()

    def test_no_resumption_without_persistence(self):
        self.disable_persistence()

        self.first_connection()
        self.marionette.restart(in_app=True, clean=False)

        self.assertFalse(
            self.cache_file.exists(),
            "ssl_tokens_cache.sqlite must NOT exist when persistence is disabled",
        )
        self.assertEqual(
            self._token_count(),
            0,
            "No tokens should be reloaded when persistence was disabled",
        )
        self.assert_does_not_resume()

    def test_no_resumption_after_persistence_disabled(self):
        self.first_connection()
        self.restart_and_check_persisted([self.HOST])

        self.disable_persistence()
        self.assertEqual(self._token_count(), 0, "In-memory cache must be cleared")

        self.marionette.restart(in_app=True, clean=False)
        self.assertFalse(self.cache_file.exists(), "DB file must stay deleted")
        self.assertEqual(self._token_count(), 0, "No tokens must be reloaded")
        self.assert_does_not_resume()

    def test_private_browsing_token_not_persisted(self):
        r = self.fetch(origin_attributes=PBM_OA)
        self.assertFalse(r["resumed"], "First connection should not resume")
        self.close_connections()
        # Private TCP connections disable session tickets (nsNSSIOLayer.cpp).
        self.assertEqual(self._token_count(), 0, "No private token must be cached")
        self.marionette.restart(in_app=True, clean=False)
        self.assertEqual(
            self.cache_keys(self.cache_file),
            [],
            "Private browsing token must not be persisted",
        )
        self.assertEqual(self._token_count(), 0, "No tokens must be reloaded")
        self.assert_does_not_resume(origin_attributes=PBM_OA)

    def test_no_resumption_after_clearing_site_data(self):
        hosts = [host for host in (self.HOST, self.CONTROL_HOST) if host]
        for host in hosts:
            self.first_connection(host)
        self.restart_and_check_persisted(hosts)

        with self.marionette.using_context("chrome"):
            failed = self.marionette.execute_async_script(
                CLEAR_HOST_SCRIPT, script_args=(self.HOST,)
            )
        self.assertEqual(failed, 0, "Clearing TLS tokens must not fail")
        self.assertEqual(
            self._wait_for_host_rows(False), [], "Cleared host's rows must be deleted"
        )
        if self.CONTROL_HOST:
            self.assertTrue(
                self._host_keys(self.CONTROL_HOST), "Other host's rows must remain"
            )

        self.marionette.restart(in_app=True, clean=False)
        self.assertEqual(self._host_keys(), [], "Cleared host's rows must not return")
        self.assert_does_not_resume()
        if self.CONTROL_HOST:
            self.assert_resumes(self.CONTROL_HOST)


class _CallbackHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        server = self.server
        with server.lock:
            if self.path == "/callback/request/early":
                server.counts["early"] += 1
            elif self.path == "/callback/request/std":
                server.counts["std"] += 1
        self.send_response(200)
        self.send_header("Content-Length", "0")
        self.end_headers()

    def log_message(self, *args):
        pass


class ZeroRttServerMixin:
    """Runs ZeroRttAcceptServer, which reports per request whether it arrived
    as accepted 0-RTT early data."""

    PORT = ZERORTT_PORT
    # The server negotiates a single ALPN per host.
    PROTOCOLS = {H1_HOST: "http/1.1", H2_HOST: "h2"}
    # NSS anti-replay window is 1 s and starts at server start.
    ANTI_REPLAY_DELAY = 1.5

    def start_server(self):
        src_base = Path(os.path.dirname(__file__))
        for fname in (
            "test-ca.pem",
            "default-ee.pem",
            "default-ee.key",
            "test-int.pem",
        ):
            (Path(self.tmpdir) / fname).write_bytes((src_base / fname).read_bytes())

        server_bin = _find_binary(self.marionette, "ZeroRttAcceptServer")
        if not server_bin:
            self.skipTest("ZeroRttAcceptServer binary not found")

        self.callback_server = ThreadingHTTPServer(("127.0.0.1", 0), _CallbackHandler)
        self.callback_server.lock = threading.Lock()
        self.reset_early_data_counts()
        threading.Thread(target=self.callback_server.serve_forever, daemon=True).start()

        env = _server_env(self.marionette)
        env["MOZ_TLS_SERVER_0RTT"] = "1"
        env["MOZ_ZERORTT_ACCEPT_CALLBACK_PORT"] = str(
            self.callback_server.server_address[1]
        )
        # Launched from Python so the server outlives in-app restarts.
        self.server = subprocess.Popen(
            [server_bin, f"sql:{self.tmpdir}", str(os.getpid())], env=env
        )

        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
                s.settimeout(1)
                if s.connect_ex(("127.0.0.1", self.PORT)) == 0:
                    break
            time.sleep(0.25)
        else:
            self.fail("ZeroRttAcceptServer did not start in time")

    def stop_server(self):
        try:
            if getattr(self, "server", None):
                self.server.terminate()
                try:
                    self.server.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    self.server.kill()
                    self.server.wait()
        finally:
            if getattr(self, "callback_server", None):
                self.callback_server.shutdown()
                self.callback_server.server_close()

    def client_prefs(self):
        return {
            "network.dns.localDomains": f"{H1_HOST},{H2_HOST}",
            "network.http.http3.enable": False,
        }

    def ca_pem(self):
        return Path(self.tmpdir) / "test-ca.pem"

    def expected_protocol(self, host):
        return self.PROTOCOLS[host]

    def reset_early_data_counts(self):
        with self.callback_server.lock:
            self.callback_server.counts = {"early": 0, "std": 0}

    def early_data_counts(self):
        with self.callback_server.lock:
            return dict(self.callback_server.counts)


class TLSTokenResumptionH1TestCase(
    ZeroRttServerMixin, TLSTokenRestartTests, MarionetteTestCase
):
    HOST = H1_HOST
    CONTROL_HOST = H2_HOST


class TLSTokenResumptionH2TestCase(
    ZeroRttServerMixin, TLSTokenRestartTests, MarionetteTestCase
):
    HOST = H2_HOST
    CONTROL_HOST = H1_HOST
