/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix

import android.content.Intent
import android.os.Bundle
import android.view.KeyEvent
import androidx.fragment.app.Fragment
import androidx.fragment.app.FragmentManager
import androidx.navigation.NavController
import androidx.navigation.NavDirections
import androidx.navigation.NavOptions
import io.mockk.Runs
import io.mockk.every
import io.mockk.just
import io.mockk.mockk
import io.mockk.slot
import io.mockk.spyk
import io.mockk.verify
import io.mockk.verifyOrder
import kotlin.test.assertNotNull
import mozilla.components.browser.state.state.ActiveOptionsPage
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.WebExtensionState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.utils.toSafeIntent
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.Metrics
import org.mozilla.fenix.GleanMetrics.NativeShareSheet
import org.mozilla.fenix.HomeActivity.Companion.handleMenuKeyPress
import org.mozilla.fenix.browser.BrowserFragment
import org.mozilla.fenix.browser.BrowserFragmentDirections
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.browser.browsingmode.BrowsingModeManager
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.menu.MenuAccessPoint
import org.mozilla.fenix.components.share.QR_CODE_URI_KEY
import org.mozilla.fenix.components.share.SEND_TO_DEVICES_ACTION
import org.mozilla.fenix.components.share.SendToDevicesDialogFragment
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.getIntentSource
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.mozilla.fenix.helpers.perf.TestStrictModeManager
import org.mozilla.fenix.home.HomeFragment
import org.mozilla.fenix.home.HomeFragmentDirections
import org.mozilla.fenix.utils.Settings
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
class HomeActivityTest {
    @get:Rule val gleanTestRule = FenixGleanTestRule(testContext)

    private lateinit var activity: HomeActivity
    private lateinit var appStore: AppStore
    private lateinit var settings: Settings

    @Before
    fun setup() {
        activity = spyk(HomeActivity())
        settings = mockk(relaxed = true)
        appStore = mockk(relaxed = true)

        every { testContext.components.settings } returns settings
        every { testContext.components.appStore } returns appStore
    }

    private fun assertNoPromptWasShown() {
        assertNull(Metrics.setAsDefaultBrowserNativePromptShown.testGetValue())
        verify(exactly = 0) { settings.setAsDefaultPromptCalled() }
        verify(exactly = 0) { activity.showSetDefaultBrowserPrompt() }
    }

    @Test
    fun getIntentSource() {
        val launcherIntent =
            Intent(Intent.ACTION_MAIN)
                .apply {
                    addCategory(Intent.CATEGORY_LAUNCHER)
                }
                .toSafeIntent()
        assertEquals("APP_ICON", activity.getIntentSource(launcherIntent))

        val viewIntent = Intent(Intent.ACTION_VIEW).toSafeIntent()
        assertEquals("LINK", activity.getIntentSource(viewIntent))

        val otherIntent = Intent().toSafeIntent()
        assertNull(activity.getIntentSource(otherIntent))
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
        val startingIntent =
            Intent().apply {
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
        every { activity.openToBrowser(any(), any()) } just Runs

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
        every { activity.openToBrowser(any(), any()) } just Runs

        activity.browsingModeManager = browsingModeManager
        activity.navigateToBrowserOnColdStart()

        verify(exactly = 0) { activity.openToBrowser(BrowserDirection.FromGlobal, null) }
    }

    @Test
    fun `isActivityColdStarted returns false for null savedInstanceState and not launched from history`() {
        val startingIntent =
            Intent().apply {
                flags = flags or Intent.FLAG_ACTIVITY_LAUNCHED_FROM_HISTORY
            }

        assertFalse(activity.isActivityColdStarted(startingIntent, Bundle()))
    }

    @Test
    fun `GIVEN the user has been away for a long time WHEN the user opens the app THEN do start on home`() {
        every { testContext.components.strictMode } returns TestStrictModeManager()
        val settings: Settings = mockk()
        val startingIntent =
            Intent().apply {
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
        val startingIntent =
            Intent().apply {
                action = Intent.ACTION_VIEW
            }
        every { settings.shouldStartOnHome() } returns true
        every { activity.getSettings() } returns settings
        every { activity.applicationContext } returns testContext

        assertFalse(activity.shouldStartOnHome(startingIntent))
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

    @Config(sdk = [34])
    @Test
    fun `GIVEN native Android share sheet is supported WHEN handleNewIntent is called with QR code URI THEN qr_code_tapped telemetry is recorded`() {
        val fragmentManager =
            mockk<FragmentManager>(relaxed = true) {
                every { findFragmentByTag(any()) } returns null
            }
        every { activity.supportFragmentManager } returns fragmentManager
        every { activity.applicationContext } returns testContext

        val intent =
            Intent().apply {
                putExtra(QR_CODE_URI_KEY, "content://cache/qr_code.png")
            }

        assertNull(NativeShareSheet.qrCodeTapped.testGetValue())

        activity.handleNewIntent(intent)

        assertNotNull(NativeShareSheet.qrCodeTapped.testGetValue())
    }

    @Config(sdk = [33])
    @Test
    fun `GIVEN native Android share sheet is not supported WHEN handleNewIntent is called with QR code URI THEN qr_code_tapped telemetry is not recorded`() {
        val fragmentManager =
            mockk<FragmentManager>(relaxed = true) {
                every { findFragmentByTag(any()) } returns null
            }
        every { activity.supportFragmentManager } returns fragmentManager
        every { activity.applicationContext } returns testContext

        val intent =
            Intent().apply {
                putExtra(QR_CODE_URI_KEY, "content://cache/qr_code.png")
            }

        assertNull(NativeShareSheet.qrCodeTapped.testGetValue())

        activity.handleNewIntent(intent)

        assertNull(NativeShareSheet.qrCodeTapped.testGetValue())
    }

    @Test
    fun `GIVEN deferred intent processing is registered WHEN handleNewIntent is called THEN the intent is stored as the activity intent and not processed`() {
        every { activity.applicationContext } returns testContext
        every { testContext.components.fenixOnboarding.registerOnboardingCompletedListener(any(), any()) } just Runs
        val intent = Intent(Intent.ACTION_VIEW)

        activity.registerDeferredIntentProcessing()
        activity.handleNewIntent(intent)

        assertSame(intent, activity.intent)
    }

    @Test
    fun `WHEN onboarding completes THEN the activity intent is processed and intents are no longer deferred`() {
        // Capture the completion callback so the test can invoke it later.
        val onboardingCompleted = slot<() -> Unit>()

        val browsingModeManager: BrowsingModeManager = mockk(relaxed = true)
        val intent = Intent(Intent.ACTION_VIEW).apply { putExtra(HomeActivity.PRIVATE_BROWSING_MODE, true) }
        every { activity.applicationContext } returns testContext
        every {
            testContext.components.fenixOnboarding.registerOnboardingCompletedListener(
                any(),
                capture(onboardingCompleted),
            )
        } just Runs
        every { activity.intent } returns intent
        every { activity.processIntent(intent) } returns true
        activity.browsingModeManager = browsingModeManager

        // Register the callback that should run when onboarding completes.
        activity.registerDeferredIntentProcessing()
        // Confirm registration alone does not process an intent.
        verify(exactly = 0) { activity.processIntent(any()) }
        assertTrue(activity.isDeferringIntentsUntilOnboardingCompletes)

        // Simulate onboarding completing by invoking the captured callback.
        onboardingCompleted.captured()

        // Confirm the browsing mode is updated before the intent is processed.
        verifyOrder {
            browsingModeManager.updateMode(intent)
            activity.processIntent(intent)
        }
        assertFalse(activity.isDeferringIntentsUntilOnboardingCompletes)
    }

    @Test
    fun `GIVEN signed out WHEN a send to device intent arrives THEN go to sign in and remember the tab`() {
        val fragmentManager =
            mockk<FragmentManager>(relaxed = true) {
                every { findFragmentByTag(any()) } returns null
            }
        every { activity.supportFragmentManager } returns fragmentManager
        every { activity.applicationContext } returns testContext
        every { testContext.components.backgroundServices.accountManager.authenticatedAccount() } returns null
        every { activity.navigateToSignInForSendTab() } just Runs

        val intent =
            Intent().apply {
                action = SEND_TO_DEVICES_ACTION
                putStringArrayListExtra(SendToDevicesDialogFragment.EXTRA_URLS, arrayListOf("https://mozilla.org"))
            }

        activity.handleNewIntent(intent)

        verify { activity.navigateToSignInForSendTab() }
        assertEquals(
            HomeActivity.PendingSendToDevicesTab(listOf("https://mozilla.org"), emptyList(), false),
            activity.pendingSendToDevicesTab,
        )
        verify(exactly = 0) { fragmentManager.beginTransaction() }
    }

    @Test
    fun `GIVEN signed in WHEN a send to device intent arrives THEN show the dialog straight away`() {
        val fragmentManager =
            mockk<FragmentManager>(relaxed = true) {
                every { findFragmentByTag(any()) } returns null
            }
        every { activity.supportFragmentManager } returns fragmentManager
        every { activity.applicationContext } returns testContext
        every { testContext.components.backgroundServices.accountManager.authenticatedAccount() } returns
            mockk(relaxed = true)
        every { activity.navigateToSignInForSendTab() } just Runs
        every { activity.showSendToDevicesDialog(any(), any(), any()) } just Runs

        val intent =
            Intent().apply {
                action = SEND_TO_DEVICES_ACTION
                putStringArrayListExtra(SendToDevicesDialogFragment.EXTRA_URLS, arrayListOf("https://mozilla.org"))
            }

        activity.handleNewIntent(intent)

        verify(exactly = 0) { activity.navigateToSignInForSendTab() }
        verify { activity.showSendToDevicesDialog(listOf("https://mozilla.org"), any(), any()) }
    }

    @Test
    fun `GIVEN a pending tab and a signed in account WHEN resuming THEN show the dialog and clear the pending tab`() {
        every { activity.components.backgroundServices.accountManager.authenticatedAccount() } returns
            mockk(relaxed = true)
        every { activity.supportFragmentManager } returns
            mockk(relaxed = true) {
                every { isStateSaved } returns false
            }
        every { activity.showSendToDevicesDialog(any(), any(), any()) } just Runs
        activity.pendingSendToDevicesTab =
            HomeActivity.PendingSendToDevicesTab(listOf("https://mozilla.org"), listOf("Mozilla"), false)

        activity.showPendingSendToDevicesIfPossible()

        verify { activity.showSendToDevicesDialog(listOf("https://mozilla.org"), listOf("Mozilla"), false) }
        assertNull(activity.pendingSendToDevicesTab)
    }

    @Test
    fun `GIVEN a pending tab but no account WHEN resuming THEN keep the tab and show nothing`() {
        every { activity.components.backgroundServices.accountManager.authenticatedAccount() } returns null
        every { activity.showSendToDevicesDialog(any(), any(), any()) } just Runs
        val pending = HomeActivity.PendingSendToDevicesTab(listOf("https://mozilla.org"), listOf("Mozilla"), false)
        activity.pendingSendToDevicesTab = pending

        activity.showPendingSendToDevicesIfPossible()

        verify(exactly = 0) { activity.showSendToDevicesDialog(any(), any(), any()) }
        assertEquals(pending, activity.pendingSendToDevicesTab)
    }

    @Test
    fun `GIVEN a pending tab and a signed in account but saved state WHEN resuming THEN keep the tab and show nothing`() {
        every { activity.components.backgroundServices.accountManager.authenticatedAccount() } returns
            mockk(relaxed = true)
        every { activity.supportFragmentManager } returns
            mockk(relaxed = true) {
                every { isStateSaved } returns true
            }
        every { activity.showSendToDevicesDialog(any(), any(), any()) } just Runs
        val pending = HomeActivity.PendingSendToDevicesTab(listOf("https://mozilla.org"), listOf("Mozilla"), false)
        activity.pendingSendToDevicesTab = pending

        activity.showPendingSendToDevicesIfPossible()

        verify(exactly = 0) { activity.showSendToDevicesDialog(any(), any(), any()) }
        assertEquals(pending, activity.pendingSendToDevicesTab)
    }

    @Test
    fun `GIVEN the user entered sign in WHEN they leave it without an account THEN drop the pending tab`() {
        every { activity.components.backgroundServices.accountManager.authenticatedAccount() } returns null
        activity.pendingSendToDevicesTab =
            HomeActivity.PendingSendToDevicesTab(listOf("https://mozilla.org"), listOf("Mozilla"), false)

        activity.onSendTabSignInDestinationChanged(R.id.turnOnSyncFragment)
        activity.onSendTabSignInDestinationChanged(R.id.browserFragment)

        assertNull(activity.pendingSendToDevicesTab)
    }

    @Test
    fun `GIVEN a pending tab WHEN still moving within the sign in flow THEN keep the pending tab`() {
        every { activity.components.backgroundServices.accountManager.authenticatedAccount() } returns null
        val pending = HomeActivity.PendingSendToDevicesTab(listOf("https://mozilla.org"), listOf("Mozilla"), false)
        activity.pendingSendToDevicesTab = pending

        activity.onSendTabSignInDestinationChanged(R.id.turnOnSyncFragment)
        activity.onSendTabSignInDestinationChanged(R.id.pairFragment)

        assertEquals(pending, activity.pendingSendToDevicesTab)
    }

    @Test
    fun `GIVEN a pending tab WHEN leaving sign in but now authenticated THEN keep the tab for the observer to handle`() {
        every { activity.components.backgroundServices.accountManager.authenticatedAccount() } returns
            mockk(relaxed = true)
        val pending = HomeActivity.PendingSendToDevicesTab(listOf("https://mozilla.org"), listOf("Mozilla"), false)
        activity.pendingSendToDevicesTab = pending

        activity.onSendTabSignInDestinationChanged(R.id.turnOnSyncFragment)
        activity.onSendTabSignInDestinationChanged(R.id.browserFragment)

        assertEquals(pending, activity.pendingSendToDevicesTab)
    }

    @Test
    fun `GIVEN a pending tab WHEN a destination changes before sign in is entered THEN keep the pending tab`() {
        every { activity.components.backgroundServices.accountManager.authenticatedAccount() } returns null
        val pending = HomeActivity.PendingSendToDevicesTab(listOf("https://mozilla.org"), listOf("Mozilla"), false)
        activity.pendingSendToDevicesTab = pending

        activity.onSendTabSignInDestinationChanged(R.id.browserFragment)

        assertEquals(pending, activity.pendingSendToDevicesTab)
    }

    @Test
    fun `GIVEN a pending tab in saved state and a signed in account WHEN restoring THEN the tab is restored`() {
        every { activity.components.backgroundServices.accountManager.authenticatedAccount() } returns
            mockk(relaxed = true)
        val pending = HomeActivity.PendingSendToDevicesTab(listOf("https://mozilla.org"), listOf("Mozilla"), false)
        val savedInstanceState =
            Bundle().apply {
                putParcelable(HomeActivity.PENDING_SEND_TO_DEVICES_TAB_KEY, pending)
            }

        activity.restorePendingSendToDevicesTab(savedInstanceState)

        assertEquals(pending, activity.pendingSendToDevicesTab)
    }

    @Test
    fun `GIVEN no saved state WHEN restoring THEN there is no pending tab`() {
        activity.restorePendingSendToDevicesTab(null)

        assertNull(activity.pendingSendToDevicesTab)
    }

    @Test
    fun `GIVEN active options page belongs to an extension WHEN creating open options page directions THEN return directions`() {
        val activeOptionsPage =
            ActiveOptionsPage(
                instanceId = "instanceId",
                url = "moz-extension://extensionId/options.html",
                name = "Test extension",
            )
        val extension =
            WebExtensionState(
                id = "extensionId",
                activeOptionsPage = activeOptionsPage,
            )
        val browserStore = BrowserStore(BrowserState(extensions = mapOf(extension.id to extension)))
        every { activity.applicationContext } returns testContext
        every { testContext.components.core.store } returns browserStore

        val directions = activity.createOpenOptionsPageDirections(activeOptionsPage)

        assertEquals(R.id.action_global_webExtensionActionOptionsPageFragment, directions?.actionId)
        assertEquals(activeOptionsPage.url, directions?.arguments?.getString("optionsPageUrl"))
        assertEquals(activeOptionsPage.name, directions?.arguments?.getString("webExtensionName"))
        assertEquals(extension.id, directions?.arguments?.getString("webExtensionId"))
    }

    @Test
    fun `GIVEN active options page does not belong to an extension WHEN creating open options page directions THEN return null`() {
        val activeOptionsPage =
            ActiveOptionsPage(
                instanceId = "instanceId",
                url = "moz-extension://extensionId/options.html",
                name = "Test extension",
            )
        val browserStore = BrowserStore(BrowserState())
        every { activity.applicationContext } returns testContext
        every { testContext.components.core.store } returns browserStore

        assertNull(activity.createOpenOptionsPageDirections(activeOptionsPage))
    }

    @Test
    fun `GIVEN the user is in the extension management UI WHEN opening an options page THEN suppress the request and clear activeOptionsPage`() {
        val activeOptionsPage =
            ActiveOptionsPage(
                instanceId = "instanceId",
                url = "moz-extension://extensionId/options.html",
                name = "Test extension",
            )
        val extension =
            WebExtensionState(
                id = "extensionId",
                activeOptionsPage = activeOptionsPage,
            )
        val browserStore = BrowserStore(BrowserState(extensions = mapOf(extension.id to extension)))
        every { activity.applicationContext } returns testContext
        every { testContext.components.core.store } returns browserStore

        val suppressed =
            activity.suppressOptionsPageInAddonManagement(
                currentDestinationId = R.id.installedAddonDetailsFragment,
                activeOptionsPage = activeOptionsPage,
            )

        assertTrue(suppressed)
        assertNull(browserStore.state.extensions[extension.id]?.activeOptionsPage)
    }

    @Test
    fun `GIVEN the user is on an addon options page WHEN opening an options page THEN suppress the request and clear activeOptionsPage`() {
        val activeOptionsPage =
            ActiveOptionsPage(
                instanceId = "instanceId",
                url = "moz-extension://extensionId/options.html",
                name = "Test extension",
            )
        val extension =
            WebExtensionState(
                id = "extensionId",
                activeOptionsPage = activeOptionsPage,
            )
        val browserStore = BrowserStore(BrowserState(extensions = mapOf(extension.id to extension)))
        every { activity.applicationContext } returns testContext
        every { testContext.components.core.store } returns browserStore

        val suppressed =
            activity.suppressOptionsPageInAddonManagement(
                currentDestinationId = R.id.addonInternalSettingsFragment,
                activeOptionsPage = activeOptionsPage,
            )

        assertTrue(suppressed)
        assertNull(browserStore.state.extensions[extension.id]?.activeOptionsPage)
    }

    @Test
    fun `GIVEN the user is not in the extension management UI WHEN opening an options page THEN do not suppress the request`() {
        val activeOptionsPage =
            ActiveOptionsPage(
                instanceId = "instanceId",
                url = "moz-extension://extensionId/options.html",
                name = "Test extension",
            )
        val extension =
            WebExtensionState(
                id = "extensionId",
                activeOptionsPage = activeOptionsPage,
            )
        val browserStore = BrowserStore(BrowserState(extensions = mapOf(extension.id to extension)))
        every { activity.applicationContext } returns testContext
        every { testContext.components.core.store } returns browserStore

        val suppressed =
            activity.suppressOptionsPageInAddonManagement(
                currentDestinationId = R.id.browserFragment,
                activeOptionsPage = activeOptionsPage,
            )

        assertFalse(suppressed)
        assertEquals(
            activeOptionsPage,
            browserStore.state.extensions[extension.id]?.activeOptionsPage,
        )
    }

    @Test
    fun `GIVEN home is shown and menu customization is enabled WHEN the menu key is pressed THEN open the customizable menu`() {
        val navController = mockNavController(R.id.homeFragment)

        assertTrue(handleMenuKeyPress(menuKeyDown(), mockk<HomeFragment>(), navController, true))

        verify {
            navController.navigate(HomeFragmentDirections.actionHomeFragmentToMenuFragment(MenuAccessPoint.Home), null)
        }
    }

    @Test
    fun `GIVEN home is shown and menu customization is disabled WHEN the menu key is pressed THEN open the legacy menu`() {
        val navController = mockNavController(R.id.homeFragment)

        assertTrue(handleMenuKeyPress(menuKeyDown(), mockk<HomeFragment>(), navController, false))

        verify {
            navController.navigate(NavGraphDirections.actionGlobalMenuDialogFragment(MenuAccessPoint.Home), null)
        }
    }

    @Test
    fun `GIVEN the browser is shown and menu customization is enabled WHEN the menu key is pressed THEN open the customizable menu`() {
        val navController = mockNavController(R.id.browserFragment)

        assertTrue(handleMenuKeyPress(menuKeyDown(), mockk<BrowserFragment>(), navController, true))

        verify {
            navController.navigate(
                BrowserFragmentDirections.actionBrowserFragmentToMenuFragment(MenuAccessPoint.Browser),
                null,
            )
        }
    }

    @Test
    fun `GIVEN the browser is shown and menu customization is disabled WHEN the menu key is pressed THEN open the legacy menu`() {
        val navController = mockNavController(R.id.browserFragment)

        assertTrue(handleMenuKeyPress(menuKeyDown(), mockk<BrowserFragment>(), navController, false))

        verify {
            navController.navigate(NavGraphDirections.actionGlobalMenuDialogFragment(MenuAccessPoint.Browser), null)
        }
    }

    @Test
    fun `GIVEN the menu key is held down WHEN repeated presses arrive THEN consume them without opening the menu`() {
        val navController = mockNavController(R.id.homeFragment)

        assertTrue(handleMenuKeyPress(menuKeyDown(repeatCount = 1), mockk<HomeFragment>(), navController, true))

        verify(exactly = 0) { navController.navigate(any<NavDirections>(), any<NavOptions>()) }
    }

    @Test
    fun `GIVEN the menu is already open WHEN the menu key is pressed THEN consume the key press without opening the menu again`() {
        val navController = mockNavController(R.id.menuDialogFragment)

        assertTrue(handleMenuKeyPress(menuKeyDown(), mockk<HomeFragment>(), navController, false))

        verify(exactly = 0) { navController.navigate(any<NavDirections>(), any<NavOptions>()) }
    }

    @Test
    fun `GIVEN neither home nor the browser is shown WHEN the menu key is pressed THEN leave the key press unhandled`() {
        val navController = mockNavController(R.id.settingsFragment)

        assertFalse(handleMenuKeyPress(menuKeyDown(), mockk<Fragment>(), navController, true))

        verify(exactly = 0) { navController.navigate(any<NavDirections>(), any<NavOptions>()) }
    }

    private fun menuKeyDown(repeatCount: Int = 0) =
        KeyEvent(0L, 0L, KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_MENU, repeatCount)

    private fun mockNavController(currentDestinationId: Int): NavController =
        mockk(relaxed = true) {
            every { currentDestination } returns mockk(relaxed = true) { every { id } returns currentDestinationId }
        }
}
