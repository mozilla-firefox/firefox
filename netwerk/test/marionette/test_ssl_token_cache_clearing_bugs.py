# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Each test injects a synthetic token via nsISSLTokensCacheTest, triggers an
# operation that must not evict it, and asserts the count is unchanged.
# No TLS server required.

import os
import sys
import time
from pathlib import Path

from marionette_harness import MarionetteTestCase

# add this directory to the path
sys.path.append(os.path.dirname(__file__))

from ssl_tokens_cache_mixin import SSLTokensCacheMixin

NORMAL_KEY = "test.example.com:443"
PBM_KEY = "test.example.com:443^privateBrowsingId=1"
HOST_A_KEY = "hostA.example.com:443"
HOST_B_KEY = "other.example.com:443"
HOST_A_CONTAINER_KEY = "hostA.example.com:443^userContextId=1"

# A non-cipher security.* pref: changing it must not clear the token cache.
INNOCENT_SECURITY_PREF = "security.certerrors.mitm.priming.enabled"

_PUT_SCRIPT = """
const [key, resolve] = arguments;
const c = Cc["@mozilla.org/network/ssl-tokens-cache;1"].getService(Ci.nsISSLTokensCacheTest);
c.putSSLTokenForTest(key);
resolve(c.countSSLTokens());
"""

_COUNT_SCRIPT = """
const [resolve] = arguments;
resolve(Cc["@mozilla.org/network/ssl-tokens-cache;1"].getService(Ci.nsISSLTokensCacheTest).countSSLTokens());
"""


class SSLTokenCacheClearingBugsTestCase(SSLTokensCacheMixin, MarionetteTestCase):
    """Various operations must not incorrectly evict cached TLS session tokens."""

    def setUp(self):
        super().setUp()
        self.marionette.set_context("chrome")
        self.marionette.execute_script(
            "Cc['@mozilla.org/network/ssl-tokens-cache;1']"
            ".getService(Ci.nsISSLTokensCache)"
            ".clearSSLExternalAndInternalSessionCache();"
        )

    def _put_token(self, key=NORMAL_KEY):
        return self.marionette.execute_async_script(_PUT_SCRIPT, script_args=(key,))

    def _count(self):
        return self.marionette.execute_async_script(_COUNT_SCRIPT)

    def _fire_last_pb_context_exited(self):
        self.marionette.execute_script(
            "Services.obs.notifyObservers(null, 'last-pb-context-exited', null);"
        )

    def test_direct_clear_actually_clears(self):
        """Sanity: clearSSLExternalAndInternalSessionCache() empties the cache."""
        self.assertEqual(self._put_token(), 1)
        self.marionette.execute_script(
            "Cc['@mozilla.org/network/ssl-tokens-cache;1']"
            ".getService(Ci.nsISSLTokensCache)"
            ".clearSSLExternalAndInternalSessionCache();"
        )
        self.assertEqual(self._count(), 0)

    def test_noop_certoverride_call_preserves_tokens(self):
        """setDisableAllSecurityChecks(false) when already false must not clear tokens."""
        self.assertEqual(self._put_token(), 1)
        self.marionette.execute_script(
            "Cc['@mozilla.org/security/certoverride;1']"
            "  .getService(Ci.nsICertOverrideService)"
            "  .setDisableAllSecurityChecksAndLetAttackersInterceptMyData(false);"
        )
        self.assertEqual(self._count(), 1, "no-op call must not clear the cache")

    def test_last_pb_context_exited_preserves_normal_browsing_tokens(self):
        """last-pb-context-exited must not evict normal-browsing tokens."""
        self.assertEqual(self._put_token(NORMAL_KEY), 1)
        self._fire_last_pb_context_exited()
        self.assertEqual(self._count(), 1)

    def test_last_pb_context_exited_removes_pbm_tokens(self):
        """last-pb-context-exited must remove private-browsing tokens."""
        self.assertEqual(self._put_token(PBM_KEY), 1)
        self._fire_last_pb_context_exited()
        self.assertEqual(self._count(), 0)

    def test_last_pb_context_exited_is_scoped_to_pbm_only(self):
        """last-pb-context-exited with both token kinds: only PBM is removed."""
        self._put_token(NORMAL_KEY)
        self._put_token(PBM_KEY)
        self.assertEqual(self._count(), 2)
        self._fire_last_pb_context_exited()
        self.assertEqual(self._count(), 1)

    def test_application_background_writes_token_file(self):
        """application-background must write the token file to disk."""
        # set_pref fires ReconcilePersistence(), registering the
        # application-background observer.
        self.marionette.set_pref("network.ssl_tokens_cache_persistence", True)
        cache_file = Path(self.marionette.profile_path) / "ssl_tokens_cache.sqlite"

        self.assertEqual(self._put_token(), 1)
        self.marionette.execute_script(
            "Services.obs.notifyObservers(null, 'application-background', null);"
        )

        self.assertGreaterEqual(
            self.wait_for_cache_rows(cache_file, 1),
            1,
            "token must be written to ssl_tokens_cache.sqlite after "
            "application-background",
        )

        # Disabling persistence must delete the file immediately.
        self.marionette.set_pref("network.ssl_tokens_cache_persistence", False)
        deadline = time.monotonic() + 5
        while cache_file.exists() and time.monotonic() < deadline:
            time.sleep(0.25)
        self.assertFalse(
            cache_file.exists(),
            "file must be deleted when persistence is disabled",
        )

    def test_non_cipher_security_pref_change_preserves_tokens(self):
        """Changing a non-cipher security.* pref must not evict tokens."""
        self.assertEqual(self._put_token(), 1)
        original = self.marionette.execute_script(
            f"return Services.prefs.getBoolPref('{INNOCENT_SECURITY_PREF}', false);"
        )
        self.marionette.set_pref(INNOCENT_SECURITY_PREF, not original)
        try:
            self.assertEqual(
                self._count(),
                1,
                f"{INNOCENT_SECURITY_PREF} change must not clear cache",
            )
        finally:
            self.marionette.set_pref(INNOCENT_SECURITY_PREF, original)

    def test_forget_client_auth_for_one_host_preserves_other_tokens(self):
        """ForgetRememberedDecision for host A must not evict tokens for host B."""
        self.assertEqual(self._put_token(HOST_B_KEY), 1)
        self.marionette.execute_script(
            """
            const cars = Cc["@mozilla.org/security/clientAuthRememberService;1"]
              .getService(Ci.nsIClientAuthRememberService);
            try {
              cars.forgetRememberedDecision("hostA.example.com,,");
            } catch (_) {}
            """
        )
        self.assertEqual(self._count(), 1)

    def test_delete_decisions_by_host_preserves_other_tokens(self):
        """DeleteDecisionsByHost for host A must not evict tokens for host B."""
        self.assertEqual(self._put_token(HOST_B_KEY), 1)
        self.marionette.execute_script(
            """
            Cc["@mozilla.org/security/clientAuthRememberService;1"]
              .getService(Ci.nsIClientAuthRememberService)
              .deleteDecisionsByHost("hostA.example.com", {});
            """
        )
        self.assertEqual(self._count(), 1)

    def test_delete_decisions_by_host_evicts_other_containers(self):
        """DeleteDecisionsByHost for host A must evict host A's tokens in
        every container, not just the caller's exact origin attributes."""
        self.assertEqual(self._put_token(HOST_A_CONTAINER_KEY), 1)
        self.marionette.execute_script(
            """
            Cc["@mozilla.org/security/clientAuthRememberService;1"]
              .getService(Ci.nsIClientAuthRememberService)
              .deleteDecisionsByHost("hostA.example.com", {});
            """
        )
        self.assertEqual(self._count(), 0)

    def test_forget_client_auth_key_without_oa_suffix_evicts_all_containers(
        self,
    ):
        """forgetRememberedDecision with a key that has no OA suffix (e.g. a
        malformed/legacy key) must not narrow eviction to the default OA."""
        self.assertEqual(self._put_token(HOST_A_CONTAINER_KEY), 1)
        self.marionette.execute_script(
            """
            Cc["@mozilla.org/security/clientAuthRememberService;1"]
              .getService(Ci.nsIClientAuthRememberService)
              .forgetRememberedDecision("hostA.example.com");
            """
        )
        self.assertEqual(self._count(), 0)
