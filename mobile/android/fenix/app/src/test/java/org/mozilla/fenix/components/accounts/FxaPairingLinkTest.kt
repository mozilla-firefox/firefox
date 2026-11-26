/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.accounts

import androidx.core.net.toUri
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.settings.account.AuthIntentReceiverActivity
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class FxaPairingLinkTest {

    @Test
    fun `WHEN the uri is on the given fxaServerUrl THEN isFxaPairingLink returns true`() {
        assertTrue(isFxaPairingLink("$FXA_SERVER_URL$PAIRING_PATH_WITH_CHANNEL".toUri(), FXA_SERVER_URL))
    }

    @Test
    fun `WHEN the uri is on a different server to fxaServerUrl THEN isFxaPairingLink returns false`() {
        assertFalse(
            isFxaPairingLink("https://accounts.stage.mozaws.net$PAIRING_PATH_WITH_CHANNEL".toUri(), FXA_SERVER_URL)
        )
        assertFalse(
            isFxaPairingLink(
                "https://accounts.firefox.com.example.com$PAIRING_PATH_WITH_CHANNEL".toUri(),
                FXA_SERVER_URL,
            )
        )
    }

    @Test
    fun `WHEN fxaServerUrl has a trailing slash THEN isFxaPairingLink still returns true`() {
        assertTrue(isFxaPairingLink("$FXA_SERVER_URL$PAIRING_PATH_WITH_CHANNEL".toUri(), "$FXA_SERVER_URL/"))
    }

    @Test
    fun `WHEN the uri host differs from fxaServerUrl only in case THEN isFxaPairingLink returns true`() {
        assertTrue(
            isFxaPairingLink(
                "https://Accounts.Firefox.com/pair#channel_id=test_id&channel_key=test_key&v=2".toUri(),
                FXA_SERVER_URL,
            )
        )
    }

    @Test
    fun `WHEN the uri scheme differs from fxaServerUrl only in case THEN isFxaPairingLink returns true`() {
        assertTrue(isFxaPairingLink("HTTPS://accounts.firefox.com/pair".toUri(), FXA_SERVER_URL))
    }

    @Test
    fun `WHEN the uri has query parameters THEN isFxaPairingLink returns true`() {
        assertTrue(isFxaPairingLink("https://accounts.firefox.com/pair?test=value".toUri(), FXA_SERVER_URL))
    }

    @Test
    fun `WHEN the uri port differs from fxaServerUrl THEN isFxaPairingLink returns false`() {
        assertFalse(isFxaPairingLink("https://accounts.firefox.com:8443/pair".toUri(), FXA_SERVER_URL))
    }

    @Test
    fun `WHEN the uri or fxaServerUrl spells out the default port THEN isFxaPairingLink returns true`() {
        assertTrue(isFxaPairingLink("https://accounts.firefox.com:443/pair".toUri(), FXA_SERVER_URL))
        assertTrue(isFxaPairingLink("https://accounts.firefox.com/pair".toUri(), "$FXA_SERVER_URL:443"))
    }

    @Test
    fun `WHEN the uri has no host THEN isFxaPairingLink returns false`() {
        assertFalse(isFxaPairingLink("/pair".toUri(), FXA_SERVER_URL))
    }

    @Test
    fun `WHEN the uri scheme differs from fxaServerUrl THEN isFxaPairingLink returns false`() {
        assertFalse(
            isFxaPairingLink(
                "http://accounts.firefox.com/pair#channel_id=test_id&channel_key=test_key&v=2".toUri(),
                FXA_SERVER_URL,
            )
        )
    }

    @Test
    fun `WHEN the uri path is not exactly the pairing path THEN isFxaPairingLink returns false`() {
        assertFalse(isFxaPairingLink("https://accounts.firefox.com".toUri(), FXA_SERVER_URL))
        assertFalse(isFxaPairingLink("https://accounts.firefox.com/pair/supp".toUri(), FXA_SERVER_URL))
        assertFalse(isFxaPairingLink("https://accounts.firefox.com/pairing".toUri(), FXA_SERVER_URL))
        assertFalse(isFxaPairingLink("https://accounts.firefox.com/pair/".toUri(), FXA_SERVER_URL))
        assertFalse(isFxaPairingLink("https://accounts.firefox.com/Pair".toUri(), FXA_SERVER_URL))
        assertFalse(isFxaPairingLink("https://accounts.firefox.com/signin".toUri(), FXA_SERVER_URL))
    }

    @Test
    fun `GIVEN a pairing link WHEN createPairingAuthCustomTabIntent is called THEN it opens the auth custom tab with embed=customtab appended`() {
        val intent = createPairingAuthCustomTabIntent(testContext, "$FXA_SERVER_URL$PAIRING_PATH_WITH_CHANNEL".toUri())

        assertEquals(AuthIntentReceiverActivity::class.java.name, intent.component?.className)
        assertEquals(
            "https://accounts.firefox.com/pair?embed=customtab#channel_id=test_id&channel_key=test_key&v=2",
            intent.dataString,
        )
    }

    @Test
    fun `GIVEN a pairing link with a query WHEN createPairingAuthCustomTabIntent is called THEN embed is appended after the existing query`() {
        val url = "https://accounts.firefox.com/pair?test=value#channel_id=test_id&channel_key=test_key&v=2"
        val intent = createPairingAuthCustomTabIntent(testContext, url.toUri())

        assertEquals(
            "https://accounts.firefox.com/pair?test=value&embed=customtab#channel_id=test_id&channel_key=test_key&v=2",
            intent.dataString,
        )
    }

    companion object {
        private const val FXA_SERVER_URL = "https://accounts.firefox.com"
        private const val PAIRING_PATH_WITH_CHANNEL = "/pair#channel_id=test_id&channel_key=test_key&v=2"
    }
}
