/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.intent

import android.app.Activity
import android.content.Intent
import androidx.core.net.toUri
import androidx.navigation.NavController
import io.mockk.Called
import io.mockk.every
import io.mockk.mockk
import io.mockk.verify
import mozilla.appservices.places.BookmarkRoot
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.prompt.ShareData
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.BrowserDirection
import org.mozilla.fenix.BuildConfig.DEEP_LINK_SCHEME
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.NavGraphDirections
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.components.accounts.FenixFxAEntryPoint
import org.mozilla.fenix.utils.Settings
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class HomeDeepLinkIntentProcessorTest {
    private lateinit var activity: HomeActivity
    private lateinit var navController: NavController
    private lateinit var out: Intent
    private lateinit var processorHome: HomeDeepLinkIntentProcessor
    private val settings: Settings = mockk(relaxed = true)

    @Before
    fun setup() {
        activity = mockk(relaxed = true)
        navController = mockk(relaxed = true)
        out = mockk()
        processorHome = HomeDeepLinkIntentProcessor(activity, ::showAddSearchWidgetPrompt)
    }

    @Test
    fun `do not process blank intents`() {
        assertFalse(processorHome.process(Intent(), navController, out, settings))

        verify { activity wasNot Called }
        verify { navController wasNot Called }
        verify { out wasNot Called }
    }

    @Test
    fun `return true if scheme is fenix`() {
        assertTrue(processorHome.process(testIntent("test"), navController, out, settings))

        verify { activity wasNot Called }
        verify { navController wasNot Called }
        verify { out wasNot Called }
    }

    @Test
    fun `return true if scheme is a fenix variant`() {
        assertTrue(processorHome.process(testIntent("fenix-beta://test"), navController, out, settings))

        verify { activity wasNot Called }
        verify { navController wasNot Called }
        verify { out wasNot Called }
    }

    @Test
    fun `process home deep link`() {
        assertTrue(processorHome.process(testIntent("home"), navController, out, settings))

        verify { activity wasNot Called }
        verify { navController.navigate(NavGraphDirections.actionGlobalHome()) }
        verify { out wasNot Called }
    }

    @Test
    fun `process urls_bookmarks deep link`() {
        assertTrue(processorHome.process(testIntent("urls_bookmarks"), navController, out, settings))

        verify { navController.navigate(NavGraphDirections.actionGlobalBookmarkFragment(BookmarkRoot.Root.id)) }
        verify { out wasNot Called }
    }

    @Test
    fun `process urls_history deep link`() {
        assertTrue(processorHome.process(testIntent("urls_history"), navController, out, settings))

        verify { navController.navigate(NavGraphDirections.actionGlobalHistoryFragment()) }
        verify { out wasNot Called }
    }

    @Test
    fun `process home_collections deep link`() {
        assertTrue(processorHome.process(testIntent("home_collections"), navController, out, settings))

        verify { navController.navigate(NavGraphDirections.actionGlobalHome()) }
        verify { out wasNot Called }
    }

    @Test
    fun `process settings deep link`() {
        assertTrue(processorHome.process(testIntent("settings"), navController, out, settings))

        verify { activity wasNot Called }
        verify { navController.navigate(NavGraphDirections.actionGlobalSettingsFragment()) }
        verify { out wasNot Called }
    }

    @Test
    fun `process turn_on_sync deep link`() {
        assertTrue(processorHome.process(testIntent("turn_on_sync"), navController, out, settings))

        verify { activity wasNot Called }
        verify {
            navController.navigate(
                NavGraphDirections.actionGlobalTurnOnSync(
                    entrypoint = FenixFxAEntryPoint.DeepLink,
                ),
            )
        }
        verify { out wasNot Called }
    }

    @Test
    fun `process settings_search_engine deep link`() {
        assertTrue(processorHome.process(testIntent("settings_search_engine"), navController, out, settings))

        verify { activity wasNot Called }
        verify { navController.navigate(NavGraphDirections.actionGlobalSearchEngineFragment()) }
        verify { out wasNot Called }
    }

    @Test
    fun `process settings_accessibility deep link`() {
        assertTrue(processorHome.process(testIntent("settings_accessibility"), navController, out, settings))

        verify { activity wasNot Called }
        verify { navController.navigate(NavGraphDirections.actionGlobalAccessibilityFragment()) }
        verify { out wasNot Called }
    }

    @Test
    fun `process settings_delete_browsing_data deep link`() {
        assertTrue(processorHome.process(testIntent("settings_delete_browsing_data"), navController, out, settings))

        verify { activity wasNot Called }
        verify { navController.navigate(NavGraphDirections.actionGlobalDeleteBrowsingDataFragment()) }
        verify { out wasNot Called }
    }

    @Test
    fun `process settings_addon_manager deep link`() {
        assertTrue(processorHome.process(testIntent("settings_addon_manager"), navController, out, settings))

        verify { navController.navigate(NavGraphDirections.actionGlobalAddonsManagementFragment()) }
        verify { out wasNot Called }
    }

    @Test
    fun `process settings_logins deep link`() {
        assertTrue(processorHome.process(testIntent("settings_logins"), navController, out, settings))

        verify { navController.navigate(NavGraphDirections.actionGlobalSavedLoginsAuthFragment()) }
        verify { out wasNot Called }
    }

    @Test
    fun `process settings_tracking_protection deep link`() {
        assertTrue(processorHome.process(testIntent("settings_tracking_protection"), navController, out, settings))

        verify { navController.navigate(NavGraphDirections.actionGlobalTrackingProtectionFragment()) }
        verify { out wasNot Called }
    }

    @Test
    fun `process settings_privacy deep link`() {
        assertTrue(processorHome.process(testIntent("settings_privacy"), navController, out, settings))

        verify { navController.navigate(NavGraphDirections.actionGlobalSettingsFragment()) }
        verify { out wasNot Called }
    }

    @Test
    fun `process enable_private_browsing deep link`() {
        assertTrue(processorHome.process(testIntent("enable_private_browsing"), navController, out, settings))

        verify { activity.browsingModeManager.mode = BrowsingMode.Private }
        verify { navController.navigate(NavGraphDirections.actionGlobalHome()) }
        verify { out wasNot Called }
    }

    @Test
    fun `process open deep link`() {
        assertTrue(processorHome.process(testIntent("open"), navController, out, settings))

        verify { activity wasNot Called }
        verify { navController wasNot Called }
        verify { out wasNot Called }

        assertTrue(processorHome.process(testIntent("open?url=test"), navController, out, settings))

        verify { activity wasNot Called }
        verify { navController wasNot Called }
        verify { out wasNot Called }

        assertTrue(processorHome.process(testIntent("open?url=https%3A%2F%2Fwww.example.org%2F"), navController, out, settings))

        verify {
            @Suppress("DEPRECATION")
            activity.openToBrowserAndLoad(
                "https://www.example.org/",
                newTab = true,
                from = BrowserDirection.FromGlobal,
                flags = EngineSession.LoadUrlFlags.external(),
            )
        }
        verify { navController wasNot Called }
        verify { out wasNot Called }
    }

    @Test
    fun `process share_sheet deep link`() {
        assertTrue(processorHome.process(testIntent("share_sheet"), navController, out, settings))

        verify { navController wasNot Called }
        verify { out wasNot Called }

        assertTrue(processorHome.process(testIntent("share_sheet?url=test"), navController, out, settings))

        verify { navController wasNot Called }
        verify { out wasNot Called }

        assertTrue(processorHome.process(testIntent("share_sheet?url=https%3A%2F%2Fexample.com&title=TestTitle&text=TestText&subject=TestSubject"), navController, out, settings))

        verify {
            navController.navigate(
                directions = match {
                    with(it.arguments) {
                        getBoolean("showPage") == false &&
                            getParcelableArray("data", ShareData::class.java)?.get(0)?.url == "https://example.com" &&
                            getString("sessionId") == null &&
                            getString("shareSubject") == "TestSubject"
                    }
                },
            )
        }
        verify { out wasNot Called }
    }

    @Test
    fun `process invalid open deep link`() {
        val invalidProcessor = HomeDeepLinkIntentProcessor(activity)

        assertTrue(invalidProcessor.process(testIntent("open"), navController, out, settings))

        verify { activity wasNot Called }
        verify { navController wasNot Called }
        verify { out wasNot Called }

        assertTrue(invalidProcessor.process(testIntent("open?url=open?url=https%3A%2F%2Fwww.example.org%2F"), navController, out, settings))

        verify { activity wasNot Called }
        verify { navController wasNot Called }
        verify { out wasNot Called }
    }

    @Test
    fun `process settings_notifications deep link`() {
        assertTrue(processorHome.process(testIntent("settings_notifications"), navController, out, settings))

        verify { navController wasNot Called }
        verify { out wasNot Called }
        verify { activity.startActivity(any()) }
    }

    @Test
    fun `process settings_wallpapers deep link`() {
        assertTrue(processorHome.process(testIntent("settings_wallpapers"), navController, out, settings))

        verify { navController.navigate(NavGraphDirections.actionGlobalWallpaperSettingsFragment()) }
        verify { out wasNot Called }
    }

    @Test
    fun `process install_search_widget deep link`() {
        assertTrue(processorHome.process(testIntent("install_search_widget"), navController, out, settings))

        verify { showAddSearchWidgetPrompt(activity) }
        verify { navController wasNot Called }
        verify { out wasNot Called }
    }

    @Test
    fun `process settings_private_browsing deep link`() {
        assertTrue(processorHome.process(testIntent("settings_private_browsing"), navController, out, settings))

        verify { navController.navigate(NavGraphDirections.actionGlobalPrivateBrowsingFragment()) }
        verify { out wasNot Called }
    }

    @Test
    fun `process settings_app_icon deep link`() {
        assertTrue(processorHome.process(testIntent("settings_app_icon"), navController, out, settings))

        verify { navController.navigate(NavGraphDirections.actionGlobalAppIconSelectionFragment()) }
        verify { out wasNot Called }
    }

    @Test
    fun `process settings_ai_controls deep link`() {
        assertTrue(processorHome.process(testIntent("settings_ai_controls"), navController, out, settings))

        verify { activity wasNot Called }
        verify { navController.navigate(NavGraphDirections.actionGlobalAiControlsFragment()) }
        verify { out wasNot Called }
    }

    private fun testIntent(uri: String) = Intent("", "$DEEP_LINK_SCHEME://$uri".toUri())

    private fun showAddSearchWidgetPrompt(activity: Activity) {
        println("$activity add search widget prompt was shown ")
    }
}
