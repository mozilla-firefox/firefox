/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix

import android.content.Intent
import android.os.Bundle
import io.mockk.Runs
import io.mockk.every
import io.mockk.just
import io.mockk.mockk
import io.mockk.spyk
import io.mockk.verify
import mozilla.components.service.pocket.PocketStoriesService
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.utils.toSafeIntent
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.Metrics
import org.mozilla.fenix.HomeActivity.Companion.PRIVATE_BROWSING_MODE
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.browser.browsingmode.BrowsingModeManager
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.getIntentSource
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.mozilla.fenix.helpers.FenixRobolectricTestRunner
import org.mozilla.fenix.helpers.perf.TestStrictModeManager
import org.mozilla.fenix.utils.Settings

@RunWith(FenixRobolectricTestRunner::class)
class HomeActivityTest {
    @get:Rule
    val gleanTestRule = FenixGleanTestRule(testContext)

    private lateinit var activity: HomeActivity
    private lateinit var appStore: AppStore
    private lateinit var settings: Settings

    @Before
    fun setup() {
        activity = spyk(HomeActivity())
        settings = mockk(relaxed = true)
        appStore = mockk(relaxed = true)

        every { testContext.settings() } returns settings
        every { testContext.components.appStore } returns appStore
    }

    private fun assertNoPromptWasShown() {
        assertNull(Metrics.setAsDefaultBrowserNativePromptShown.testGetValue())
        verify(exactly = 0) { settings.setAsDefaultPromptCalled() }
        verify(exactly = 0) { activity.showSetDefaultBrowserPrompt() }
    }

    @Test
    fun getIntentSource() {
        val launcherIntent = Intent(Intent.ACTION_MAIN).apply {
            addCategory(Intent.CATEGORY_LAUNCHER)
        }.toSafeIntent()
        assertEquals("APP_ICON", activity.getIntentSource(launcherIntent))

        val viewIntent = Intent(Intent.ACTION_VIEW).toSafeIntent()
        assertEquals("LINK", activity.getIntentSource(viewIntent))

        val otherIntent = Intent().toSafeIntent()
        assertNull(activity.getIntentSource(otherIntent))
    }

    @Test
    fun `getModeFromIntentOrLastKnown returns mode from settings when intent does not set`() {
        every { testContext.settings() } returns Settings(testContext)
        every { activity.applicationContext } returns testContext
        testContext.settings().lastKnownMode = BrowsingMode.Private

        assertEquals(testContext.settings().lastKnownMode, activity.getModeFromIntentOrLastKnown(null))
    }

    @Test
    fun `getModeFromIntentOrLastKnown returns mode from intent when set`() {
        every { testContext.settings() } returns Settings(testContext)
        testContext.settings().lastKnownMode = BrowsingMode.Normal

        val intent = Intent()
        intent.putExtra(PRIVATE_BROWSING_MODE, true)

        assertNotEquals(testContext.settings().lastKnownMode, activity.getModeFromIntentOrLastKnown(intent))
        assertEquals(BrowsingMode.Private, activity.getModeFromIntentOrLastKnown(intent))
    }

    @Test
    fun `isActivityColdStarted returns true for null savedInstanceState and not launched from history`() {
        assertTrue(activity.isActivityColdStarted(Intent(), null))
    }

    @Test
    fun `isActivityColdStarted returns false for valid savedInstanceState and not launched from history`() {
        assertFalse(activity.isActivityColdStarted(Intent(), Bundle()))
    }

    @Test
    fun `isActivityColdStarted returns false for null savedInstanceState and launched from history`() {
        val startingIntent = Intent().apply {
            flags = flags or Intent.FLAG_ACTIVITY_LAUNCHED_FROM_HISTORY
        }

        assertFalse(activity.isActivityColdStarted(startingIntent, null))
    }

    @Test
    fun `navigateToBrowserOnColdStart in normal mode navigates to browser`() {
        val browsingModeManager: BrowsingModeManager = mockk()
        every { browsingModeManager.mode } returns BrowsingMode.Normal

        every { settings.shouldReturnToBrowser } returns true
        every { activity.components.settings.shouldReturnToBrowser } returns true
        every { activity.openToBrowser(any(), any()) } returns Unit

        activity.browsingModeManager = browsingModeManager
        activity.navigateToBrowserOnColdStart()

        verify(exactly = 1) { activity.openToBrowser(BrowserDirection.FromGlobal, null) }
    }

    @Test
    fun `navigateToBrowserOnColdStart in private mode does not navigate to browser`() {
        val browsingModeManager: BrowsingModeManager = mockk()
        every { browsingModeManager.mode } returns BrowsingMode.Private

        every { settings.shouldReturnToBrowser } returns true
        every { activity.components.settings.shouldReturnToBrowser } returns true
        every { activity.openToBrowser(any(), any()) } returns Unit

        activity.browsingModeManager = browsingModeManager
        activity.navigateToBrowserOnColdStart()

        verify(exactly = 0) { activity.openToBrowser(BrowserDirection.FromGlobal, null) }
    }

    @Test
    fun `isActivityColdStarted returns false for null savedInstanceState and not launched from history`() {
        val startingIntent = Intent().apply {
            flags = flags or Intent.FLAG_ACTIVITY_LAUNCHED_FROM_HISTORY
        }

        assertFalse(activity.isActivityColdStarted(startingIntent, Bundle()))
    }

    @Test
    fun `GIVEN the user has been away for a long time WHEN the user opens the app THEN do start on home`() {
        every { testContext.components.strictMode } returns TestStrictModeManager()
        val settings: Settings = mockk()
        val startingIntent = Intent().apply {
            action = Intent.ACTION_MAIN
        }
        every { activity.applicationContext } returns testContext

        every { settings.shouldStartOnHome() } returns true
        every { activity.getSettings() } returns settings

        assertTrue(activity.shouldStartOnHome(startingIntent))
    }

    @Test
    fun `GIVEN the user has been away for a long time WHEN opening a link THEN do not start on home`() {
        every { testContext.components.strictMode } returns TestStrictModeManager()
        val settings: Settings = mockk()
        val startingIntent = Intent().apply {
            action = Intent.ACTION_VIEW
        }
        every { settings.shouldStartOnHome() } returns true
        every { activity.getSettings() } returns settings
        every { activity.applicationContext } returns testContext

        assertFalse(activity.shouldStartOnHome(startingIntent))
    }

    @Test
    fun `WHEN Pocket sponsored stories profile is migrated to MARS API THEN delete the old Pocket profile`() {
        val pocketStoriesService: PocketStoriesService = mockk(relaxed = true)
        every { testContext.settings() } returns Settings(testContext)
        every { activity.applicationContext } returns testContext
        testContext.settings().hasPocketSponsoredStoriesProfileMigrated = false

        activity.migratePocketSponsoredStoriesProfile(pocketStoriesService)

        assertTrue(testContext.settings().hasPocketSponsoredStoriesProfileMigrated)

        verify {
            pocketStoriesService.deleteProfile()
        }
    }

    @Test
    fun `GIVEN all conditions met WHEN maybeShowSetAsDefaultBrowserPrompt is called THEN dispatch action and record metrics`() {
        every { activity.applicationContext } returns testContext
        every { testContext.components.strictMode } returns TestStrictModeManager()
        every { activity.showSetDefaultBrowserPrompt() } just Runs

        assertNull(Metrics.setAsDefaultBrowserNativePromptShown.testGetValue())

        activity.maybeShowSetAsDefaultBrowserPrompt(
            shouldShowSetAsDefaultPrompt = true,
            isDefaultBrowser = false,
            isTheCorrectBuildVersion = true,
        )

        verify { appStore.dispatch(AppAction.UpdateWasNativeDefaultBrowserPromptShown(true)) }
        assertNotNull(Metrics.setAsDefaultBrowserNativePromptShown.testGetValue())
        verify { settings.setAsDefaultPromptCalled() }
        verify { activity.showSetDefaultBrowserPrompt() }
    }

    @Test
    fun `GIVEN app is default browser WHEN maybeShowSetAsDefaultBrowserPrompt is called THEN do nothing`() {
        activity.maybeShowSetAsDefaultBrowserPrompt(
            shouldShowSetAsDefaultPrompt = true,
            isDefaultBrowser = true,
            isTheCorrectBuildVersion = true,
        )
        assertNoPromptWasShown()
    }

    @Test
    fun `GIVEN build version too low WHEN maybeShowSetAsDefaultBrowserPrompt is called THEN do nothing`() {
        activity.maybeShowSetAsDefaultBrowserPrompt(
            shouldShowSetAsDefaultPrompt = true,
            isDefaultBrowser = false,
            isTheCorrectBuildVersion = false,
        )
        assertNoPromptWasShown()
    }

    @Test
    fun `GIVEN should not show prompt WHEN maybeShowSetAsDefaultBrowserPrompt is called THEN do nothing`() {
        activity.maybeShowSetAsDefaultBrowserPrompt(
            shouldShowSetAsDefaultPrompt = false,
            isDefaultBrowser = false,
            isTheCorrectBuildVersion = true,
        )
        assertNoPromptWasShown()
    }
}
