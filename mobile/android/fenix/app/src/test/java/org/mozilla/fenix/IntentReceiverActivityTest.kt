/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix

import android.app.Activity
import android.content.Intent
import android.content.Intent.FLAG_ACTIVITY_LAUNCHED_FROM_HISTORY
import androidx.browser.customtabs.CustomTabsIntent
import androidx.core.net.toUri
import androidx.test.espresso.intent.matcher.IntentMatchers.hasComponent
import androidx.test.espresso.intent.matcher.IntentMatchers.hasExtra
import androidx.test.espresso.intent.matcher.IntentMatchers.hasExtraWithKey
import io.mockk.Runs
import io.mockk.coEvery
import io.mockk.coVerify
import io.mockk.every
import io.mockk.just
import io.mockk.mockk
import io.mockk.spyk
import io.mockk.verify
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlinx.coroutines.test.runTest
import mozilla.components.feature.intent.processing.IntentProcessor
import mozilla.components.support.test.robolectric.testContext
import org.hamcrest.CoreMatchers.allOf
import org.hamcrest.CoreMatchers.not
import org.hamcrest.MatcherAssert.assertThat
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.Events
import org.mozilla.fenix.GleanMetrics.SyncAuth
import org.mozilla.fenix.components.IntentProcessorType
import org.mozilla.fenix.components.IntentProcessors
import org.mozilla.fenix.customtabs.ExternalAppBrowserActivity
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.mozilla.fenix.helpers.perf.TestStrictModeManager
import org.mozilla.fenix.home.intent.StartSearchIntentProcessor
import org.mozilla.fenix.settings.account.AuthIntentReceiverActivity
import org.mozilla.fenix.shortcut.NewTabShortcutIntentProcessor
import org.mozilla.fenix.shortcut.PasswordManagerIntentProcessor
import org.mozilla.fenix.utils.Settings
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf

@RunWith(RobolectricTestRunner::class)
class IntentReceiverActivityTest {

    private lateinit var settings: Settings
    private lateinit var intentProcessors: IntentProcessors

    @get:Rule val gleanTestRule = FenixGleanTestRule(testContext)

    @Before
    fun setup() {
        settings = mockk()
        intentProcessors = mockk()

        every { settings.openLinksInAPrivateTab } returns false
        every { settings.enableHomepageAsNewTab } returns false
        every { intentProcessors.intentProcessor } returns mockIntentProcessor()
        every { intentProcessors.privateIntentProcessor } returns mockIntentProcessor()
        every { intentProcessors.customTabIntentProcessor } returns mockIntentProcessor()
        every { intentProcessors.privateCustomTabIntentProcessor } returns mockIntentProcessor()
        every { intentProcessors.externalAppIntentProcessors } returns emptyList()
        every { intentProcessors.fennecPageShortcutIntentProcessor } returns mockIntentProcessor()
        every { intentProcessors.externalDeepLinkIntentProcessor } returns mockIntentProcessor()
        every { intentProcessors.webNotificationsIntentProcessor } returns mockIntentProcessor()
        every { intentProcessors.passwordManagerIntentProcessor } returns mockIntentProcessor()
        every { settings.isUserPairingCampaignAttributed } returns false

        coEvery { intentProcessors.intentProcessor.process(any()) } returns true
    }

    @Test
    fun `process intent with flag launched from history`() = runTest {
        val intent = Intent()
        intent.flags = FLAG_ACTIVITY_LAUNCHED_FROM_HISTORY
        assertNull(Events.openedLink.testGetValue())

        val activity = Robolectric.buildActivity(IntentReceiverActivity::class.java, intent).get()
        attachMocks(activity)
        activity.processIntent(intent)

        val shadow = shadowOf(activity)
        val actualIntent = shadow.peekNextStartedActivity()

        assertNotNull(Events.openedLink.testGetValue())
        assertEquals(HomeActivity::class.java.name, actualIntent.component?.className)
        assertEquals(true, actualIntent.flags == FLAG_ACTIVITY_LAUNCHED_FROM_HISTORY)
    }

    @Test
    fun `GIVEN a deeplink intent WHEN processing the intent THEN add the className HomeActivity`() = runTest {
        val uri = "${BuildConfig.DEEP_LINK_SCHEME}://settings_wallpapers".toUri()
        val intent = Intent("", uri)
        assertNull(Events.openedLink.testGetValue())

        coEvery { intentProcessors.intentProcessor.process(any()) } returns false
        coEvery { intentProcessors.externalDeepLinkIntentProcessor.process(any()) } returns true

        val activity = Robolectric.buildActivity(IntentReceiverActivity::class.java, intent).get()
        attachMocks(activity)
        activity.processIntent(intent)

        val shadow = shadowOf(activity)
        val actualIntent = shadow.peekNextStartedActivity()

        assertNotNull(Events.openedLink.testGetValue())
        assertEquals(HomeActivity::class.java.name, actualIntent.component?.className)
    }

    @Test
    fun `GIVEN homepage as new tab is disabled THEN process intent with action OPEN_PRIVATE_TAB and open to search`() =
        runTest {
            every { settings.enableHomepageAsNewTab } returns false

            val intent = Intent()
            intent.action = NewTabShortcutIntentProcessor.ACTION_OPEN_PRIVATE_TAB
            assertNull(Events.openedLink.testGetValue())

            coEvery { intentProcessors.intentProcessor.process(intent) } returns false
            coEvery { intentProcessors.customTabIntentProcessor.process(intent) } returns false
            val activity = Robolectric.buildActivity(IntentReceiverActivity::class.java, intent).get()
            attachMocks(activity)
            activity.processIntent(intent)

            val shadow = shadowOf(activity)
            val actualIntent = shadow.peekNextStartedActivity()

            assertNotNull(Events.openedLink.testGetValue())
            assertThat(
                actualIntent,
                allOf(
                    hasComponent(HomeActivity::class.java.name),
                    hasExtra(
                        HomeActivity.OPEN_TO_SEARCH,
                        StartSearchIntentProcessor.STATIC_SHORTCUT_NEW_PRIVATE_TAB,
                    ),
                    hasExtra(HomeActivity.PRIVATE_BROWSING_MODE, true),
                    hasExtra(HomeActivity.OPEN_TO_BROWSER, false),
                    not(hasExtraWithKey(HomeActivity.OPEN_TO_HOME)),
                ),
            )
        }

    @Test
    fun `GIVEN homepage as new tab is enabled THEN process intent with action OPEN_PRIVATE_TAB and open to home`() =
        runTest {
            every { settings.enableHomepageAsNewTab } returns true

            val intent = Intent()
            intent.action = NewTabShortcutIntentProcessor.ACTION_OPEN_PRIVATE_TAB
            assertNull(Events.openedLink.testGetValue())

            coEvery { intentProcessors.intentProcessor.process(intent) } returns false
            coEvery { intentProcessors.customTabIntentProcessor.process(intent) } returns false
            val activity = Robolectric.buildActivity(IntentReceiverActivity::class.java, intent).get()
            attachMocks(activity)
            activity.processIntent(intent)

            val shadow = shadowOf(activity)
            val actualIntent = shadow.peekNextStartedActivity()

            assertNotNull(Events.openedLink.testGetValue())
            assertThat(
                actualIntent,
                allOf(
                    hasComponent(HomeActivity::class.java.name),
                    hasExtra(HomeActivity.OPEN_TO_HOME, true),
                    hasExtra(HomeActivity.PRIVATE_BROWSING_MODE, true),
                    hasExtra(HomeActivity.OPEN_TO_BROWSER, false),
                    not(hasExtraWithKey(HomeActivity.OPEN_TO_SEARCH)),
                ),
            )
        }

    @Test
    fun `process intent with action OPEN_TAB`() = runTest {
        assertNull(Events.openedLink.testGetValue())
        val intent = Intent()
        intent.action = NewTabShortcutIntentProcessor.ACTION_OPEN_TAB

        val activity = Robolectric.buildActivity(IntentReceiverActivity::class.java, intent).get()
        attachMocks(activity)
        activity.processIntent(intent)

        val shadow = shadowOf(activity)
        val actualIntent = shadow.peekNextStartedActivity()

        assertEquals(HomeActivity::class.java.name, actualIntent.component?.className)
        assertEquals(false, actualIntent.getBooleanExtra(HomeActivity.PRIVATE_BROWSING_MODE, false))
        assertNotNull(Events.openedLink.testGetValue())
    }

    @Test
    fun `process intent starts Activity`() = runTest {
        assertNull(Events.openedLink.testGetValue())
        val intent = Intent()
        val activity = Robolectric.buildActivity(IntentReceiverActivity::class.java, intent).get()
        attachMocks(activity)
        activity.processIntent(intent)

        val shadow = shadowOf(activity)
        val actualIntent = shadow.peekNextStartedActivity()

        assertEquals(HomeActivity::class.java.name, actualIntent.component?.className)
        assertEquals(true, actualIntent.getBooleanExtra(HomeActivity.OPEN_TO_BROWSER, true))
        assertNotNull(Events.openedLink.testGetValue())
    }

    @Test
    fun `process intent with launchLinksInPrivateTab set to true`() = runTest {
        assertNull(Events.openedLink.testGetValue())

        every { settings.openLinksInAPrivateTab } returns true

        coEvery { intentProcessors.intentProcessor.process(any()) } returns false
        coEvery { intentProcessors.privateIntentProcessor.process(any()) } returns true

        val intent = Intent()
        val activity = Robolectric.buildActivity(IntentReceiverActivity::class.java, intent).get()
        attachMocks(activity)
        activity.processIntent(intent)

        val shadow = shadowOf(activity)
        val actualIntent = shadow.peekNextStartedActivity()

        val normalProcessor = intentProcessors.intentProcessor
        verify(exactly = 0) { normalProcessor.process(intent) }
        verify { intentProcessors.privateIntentProcessor.process(intent) }
        assertEquals(HomeActivity::class.java.name, actualIntent.component?.className)
        assertTrue(actualIntent.getBooleanExtra(HomeActivity.PRIVATE_BROWSING_MODE, false))
        assertNotNull(Events.openedLink.testGetValue())
    }

    @Test
    fun `process intent with launchLinksInPrivateTab set to false`() = runTest {
        assertNull(Events.openedLink.testGetValue())
        val intent = Intent()

        val activity = Robolectric.buildActivity(IntentReceiverActivity::class.java, intent).get()
        attachMocks(activity)
        activity.processIntent(intent)

        coVerify(exactly = 0) { intentProcessors.privateIntentProcessor.process(intent) }
        coVerify { intentProcessors.intentProcessor.process(intent) }
        assertNotNull(Events.openedLink.testGetValue())
    }

    @Test
    fun `process intent with launchLinksInPrivateTab set to false but with external flag`() = runTest {
        assertNull(Events.openedLink.testGetValue())

        coEvery { intentProcessors.intentProcessor.process(any()) } returns false
        coEvery { intentProcessors.privateIntentProcessor.process(any()) } returns true

        val intent = Intent()
        intent.putExtra(HomeActivity.PRIVATE_BROWSING_MODE, true)

        val activity = Robolectric.buildActivity(IntentReceiverActivity::class.java, intent).get()
        attachMocks(activity)
        activity.processIntent(intent)

        val shadow = shadowOf(activity)
        val actualIntent = shadow.peekNextStartedActivity()

        val normalProcessor = intentProcessors.intentProcessor
        verify(exactly = 0) { normalProcessor.process(intent) }
        verify { intentProcessors.privateIntentProcessor.process(intent) }
        assertEquals(HomeActivity::class.java.name, actualIntent.component?.className)
        assertTrue(actualIntent.getBooleanExtra(HomeActivity.PRIVATE_BROWSING_MODE, false))
        assertNotNull(Events.openedLink.testGetValue())
    }

    @Test
    fun `process custom tab intent`() = runTest {
        assertNull(Events.openedLink.testGetValue())
        val intent = Intent()
        coEvery { intentProcessors.intentProcessor.process(intent) } returns false
        coEvery { intentProcessors.customTabIntentProcessor.process(intent) } returns true
        assertNull(Events.openedLink.testGetValue())

        val activity = Robolectric.buildActivity(IntentReceiverActivity::class.java, intent).get()
        attachMocks(activity)
        activity.processIntent(intent)

        coVerify(exactly = 0) { intentProcessors.privateCustomTabIntentProcessor.process(intent) }
        coVerify { intentProcessors.customTabIntentProcessor.process(intent) }

        assertEquals(ExternalAppBrowserActivity::class.java.name, intent.component!!.className)
        assertTrue(intent.getBooleanExtra(HomeActivity.OPEN_TO_BROWSER, false))
        assertNotNull(Events.openedLink.testGetValue())
    }

    @Test
    fun `process private custom tab intent`() = runTest {
        assertNull(Events.openedLink.testGetValue())
        every { settings.openLinksInAPrivateTab } returns true

        val intent = Intent()
        coEvery { intentProcessors.privateCustomTabIntentProcessor.process(intent) } returns true

        val activity = Robolectric.buildActivity(IntentReceiverActivity::class.java, intent).get()
        attachMocks(activity)
        activity.processIntent(intent)

        val normalProcessor = intentProcessors.customTabIntentProcessor
        coVerify(exactly = 0) { normalProcessor.process(intent) }
        coVerify { intentProcessors.privateCustomTabIntentProcessor.process(intent) }

        assertEquals(ExternalAppBrowserActivity::class.java.name, intent.component!!.className)
        assertTrue(intent.getBooleanExtra(HomeActivity.OPEN_TO_BROWSER, false))
        assertNotNull(Events.openedLink.testGetValue())
    }

    @Test
    fun `process web notifications click intent`() {
        val intent = Intent()
        every { intentProcessors.webNotificationsIntentProcessor.process(intent) } returns true
        val activity = spyk(Robolectric.buildActivity(IntentReceiverActivity::class.java, intent).get())
        attachMocks(activity)
        every { activity.launch(any(), any()) } just Runs
        activity.processIntent(intent)

        verify { intentProcessors.webNotificationsIntentProcessor.process(intent) }
        verify { activity.launch(intent, IntentProcessorType.NEW_TAB) }
    }

    @Test
    fun `process intent with action OPEN_PASSWORD_MANAGER`() = runTest {
        val intent = Intent()
        intent.action = PasswordManagerIntentProcessor.ACTION_OPEN_PASSWORD_MANAGER

        val activity = Robolectric.buildActivity(IntentReceiverActivity::class.java, intent).get()
        attachMocks(activity)
        activity.processIntent(intent)

        val shadow = shadowOf(activity)
        val actualIntent = shadow.peekNextStartedActivity()

        assertEquals(HomeActivity::class.java.name, actualIntent.component?.className)
    }

    @Test
    fun `GIVEN a pairing link intent WHEN processing the intent THEN pairingLinkOpened is recorded`() = runTest {
        val intent =
            Intent(
                Intent.ACTION_VIEW,
                "https://accounts.firefox.com/pair#channel_id=test_id&channel_key=test_key&v=2".toUri(),
            )
        assertNull(SyncAuth.pairingLinkOpened.testGetValue())

        val activity = Robolectric.buildActivity(IntentReceiverActivity::class.java, intent).get()
        attachMocks(activity)
        activity.processIntent(intent)

        val snapshot = SyncAuth.pairingLinkOpened.testGetValue()

        assertEquals(1, snapshot?.size)
        assertEquals("false", snapshot?.single()?.extra?.get("campaign_attributed"))
    }

    @Test
    fun `GIVEN a pairing link intent WHEN processing the intent THEN it opens in the auth custom tab instead of a tab`() =
        runTest {
            val url = "https://accounts.firefox.com/pair#channel_id=test_id&channel_key=test_key&v=2"
            val intent = Intent(Intent.ACTION_VIEW, url.toUri())

            val activity = Robolectric.buildActivity(IntentReceiverActivity::class.java, intent).get()
            attachMocks(activity)
            activity.processIntent(intent)

            val actualIntent = shadowOf(activity).peekNextStartedActivity()
            assertThat(actualIntent, hasComponent(AuthIntentReceiverActivity::class.java.name))
            assertThat(actualIntent, hasExtraWithKey(CustomTabsIntent.EXTRA_SESSION))
            assertEquals(url, actualIntent.dataString)
            assertTrue(activity.isFinishing)
            assertNull(Events.openedLink.testGetValue())
            coVerify(exactly = 0) { intentProcessors.intentProcessor.process(any()) }
            coVerify(exactly = 0) { intentProcessors.customTabIntentProcessor.process(any()) }
        }

    @Test
    fun `GIVEN a pairing link intent and a pairing campaign install WHEN processing the intent THEN pairingLinkOpened is recorded as campaign attributed`() =
        runTest {
            every { settings.isUserPairingCampaignAttributed } returns true
            val intent =
                Intent(
                    Intent.ACTION_VIEW,
                    "https://accounts.firefox.com/pair#channel_id=test_id&channel_key=test_key&v=2".toUri(),
                )
            assertNull(SyncAuth.pairingLinkOpened.testGetValue())

            val activity = Robolectric.buildActivity(IntentReceiverActivity::class.java, intent).get()
            attachMocks(activity)
            activity.processIntent(intent)

            val snapshot = SyncAuth.pairingLinkOpened.testGetValue()

            assertEquals(1, snapshot?.size)
            assertEquals("true", snapshot?.single()?.extra?.get("campaign_attributed"))
        }

    @Test
    fun `GIVEN a non-pairing link intent WHEN processing the intent THEN pairingLinkOpened is not recorded`() =
        runTest {
            val intent =
                Intent(
                    Intent.ACTION_VIEW,
                    "https://accounts.firefox.com/signin".toUri(),
                )

            val activity = Robolectric.buildActivity(IntentReceiverActivity::class.java, intent).get()
            attachMocks(activity)
            activity.processIntent(intent)

            assertNull(SyncAuth.pairingLinkOpened.testGetValue())
        }

    @Test
    fun `GIVEN a production pairing link intent WHEN isFxaPairingLink is called THEN it returns true`() {
        assertTrue(
            isFxaPairingLink("https://accounts.firefox.com/pair#channel_id=test_id&channel_key=test_key&v=2".toUri())
        )
    }

    @Test
    fun `GIVEN a stage pairing link intent WHEN isFxaPairingLink is called THEN it returns true`() {
        assertTrue(
            isFxaPairingLink(
                "https://accounts.stage.mozaws.net/pair#channel_id=test_id&channel_key=test_key&v=2".toUri()
            )
        )
    }

    @Test
    fun `GIVEN a non-pairing link intent WHEN isFxaPairingLink is called THEN it returns false`() {
        assertFalse(isFxaPairingLink("https://example.com".toUri()))
    }

    @Test
    fun `GIVEN a pairing link intent with an unexpected scheme WHEN isFxaPairingLink is called THEN it returns false`() {
        assertFalse(
            isFxaPairingLink("http://accounts.firefox.com/pair#channel_id=test_id&channel_key=test_key&v=2".toUri())
        )
    }

    @Test
    fun `GIVEN a pairing link intent with an unexpected path WHEN isFxaPairingLink is called THEN it returns false`() {
        assertFalse(isFxaPairingLink("https://accounts.firefox.com/signin".toUri()))
    }

    private fun attachMocks(activity: Activity) {
        every { activity.components.settings } returns settings
        every { activity.components.analytics } returns mockk(relaxed = true)
        every { activity.components.core } returns mockk(relaxed = true)
        every { activity.components.intentProcessors } returns intentProcessors
        every { activity.components.strictMode } returns TestStrictModeManager()
    }

    private inline fun <reified T : IntentProcessor> mockIntentProcessor(): T {
        return mockk {
            coEvery { process(any()) } returns false
        }
    }
}
