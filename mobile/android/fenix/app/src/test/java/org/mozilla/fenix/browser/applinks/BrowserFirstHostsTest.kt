/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.browser.applinks

import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.R
import org.mozilla.fenix.utils.Settings
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class BrowserFirstHostsTest {

    private lateinit var settings: Settings
    private val enabledFeatureHelper =
        object : BrowserFirstFeatureHelper {
            override val enabled = true
        }

    private val youtubeUrl = "https://youtube.com/watch?v=1"
    private val otherUrl = "https://example.com"

    @Before
    fun setup() {
        settings = Settings(testContext)
    }

    private fun hosts(helper: BrowserFirstFeatureHelper) = BrowserFirstHosts(settings, helper)

    @Test
    fun `GIVEN each browser-first host WHEN isBrowserFirstHost is called THEN it returns true with and without a www prefix`() {
        val hosts = hosts(enabledFeatureHelper)
        val browserFirstHosts =
            listOf(
                "youtube.com",
                "music.youtube.com",
                "youtu.be",
                "youtubekids.com",
            )

        browserFirstHosts.forEach { host ->
            assertTrue(host, hosts.isBrowserFirstHost("https://$host"))
            assertTrue("www.$host", hosts.isBrowserFirstHost("https://www.$host"))
        }
    }

    @Test
    fun `GIVEN a host with an m or mobile prefix WHEN isBrowserFirstHost is called THEN the prefix is ignored`() {
        val hosts = hosts(enabledFeatureHelper)

        assertTrue(hosts.isBrowserFirstHost("https://m.youtube.com"))
        assertTrue(hosts.isBrowserFirstHost("https://mobile.youtube.com"))
    }

    @Test
    fun `GIVEN a non browser-first host WHEN isBrowserFirstHost is called THEN it returns false`() {
        assertFalse(hosts(enabledFeatureHelper).isBrowserFirstHost("https://youtube.com.extra.com"))
    }

    @Test
    fun `GIVEN a non browser-first host url WHEN it is evaluated THEN it defers to the global behavior`() {
        val hosts = hosts(enabledFeatureHelper)

        assertNull(hosts.launchInApp(otherUrl))
        assertNull(hosts.shouldPrompt(otherUrl))
    }

    @Test
    fun `GIVEN the preference is never WHEN a YouTube url is evaluated THEN it stays in the browser without a prompt`() {
        settings.openLinksInYoutube = testContext.getString(R.string.pref_key_open_links_in_youtube_never)

        val hosts = hosts(enabledFeatureHelper)

        assertEquals(false, hosts.launchInApp(youtubeUrl))
        assertEquals(false, hosts.shouldPrompt(youtubeUrl))
    }

    @Test
    fun `GIVEN the preference is always WHEN a YouTube url is evaluated THEN it launches without a prompt`() {
        settings.openLinksInYoutube = testContext.getString(R.string.pref_key_open_links_in_youtube_always)

        val hosts = hosts(enabledFeatureHelper)

        assertEquals(true, hosts.launchInApp(youtubeUrl))
        assertEquals(false, hosts.shouldPrompt(youtubeUrl))
    }

    @Test
    fun `GIVEN the preference is ask WHEN a YouTube url is evaluated THEN it launches with a prompt`() {
        settings.openLinksInYoutube = testContext.getString(R.string.pref_key_open_links_in_youtube_ask)

        val hosts = hosts(enabledFeatureHelper)

        assertEquals(true, hosts.launchInApp(youtubeUrl))
        assertEquals(true, hosts.shouldPrompt(youtubeUrl))
    }
}
