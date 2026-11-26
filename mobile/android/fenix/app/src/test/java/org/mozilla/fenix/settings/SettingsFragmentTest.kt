/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings

import androidx.fragment.app.FragmentActivity
import androidx.lifecycle.lifecycleScope
import androidx.navigation.NavController
import androidx.navigation.NavDestination
import androidx.navigation.NavDirections
import androidx.navigation.Navigation
import androidx.preference.Preference
import io.mockk.every
import io.mockk.mockk
import io.mockk.verify
import java.io.IOException
import kotlin.test.assertNotNull
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.runTest
import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.concept.fetch.Client
import mozilla.components.feature.ipprotection.store.IPProtectionStore
import mozilla.components.feature.ipprotection.store.state.EligibilityStatus
import mozilla.components.feature.ipprotection.store.state.IPProtectionState
import mozilla.components.service.fxa.manager.FxaAccountManager
import mozilla.components.service.fxrelay.eligibility.Eligible
import mozilla.components.service.fxrelay.eligibility.Ineligible
import mozilla.components.service.fxrelay.eligibility.RelayEligibilityStore
import mozilla.components.service.fxrelay.eligibility.RelayState
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.R
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.getPreferenceKey
import org.mozilla.fenix.settings.account.AccountUiView
import org.mozilla.fenix.utils.Settings
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.android.controller.ActivityController

@RunWith(RobolectricTestRunner::class)
class SettingsFragmentTest {
    private val settingsFragment = SettingsFragment()
    private lateinit var activityController: ActivityController<FragmentActivity>

    @Before
    fun setup() {
        // Mock client for fetching account avatar
        val client = mockk<Client>()
        every { client.fetch(any()) } throws IOException("test")

        every { testContext.components.core.engine.profiler } returns mockk(relaxed = true)
        every { testContext.components.core.client } returns client
        every { testContext.components.settings } returns mockk(relaxed = true)
        every { testContext.components.core.store.state.translationEngine } returns mockk(relaxed = true)
        every { testContext.components.addonManager } returns mockk(relaxed = true)
        every { testContext.components.analytics } returns mockk(relaxed = true)
        every { testContext.components.backgroundServices } returns mockk(relaxed = true)

        activityController = Robolectric.buildActivity(FragmentActivity::class.java).create()
        val activity = activityController.get()
        activity.supportFragmentManager
            .beginTransaction()
            .add(android.R.id.content, settingsFragment, "test")
            .commitNow()
    }

    @Test
    fun `account and error card visibility follows account state in new settings`() {
        val accountManager: FxaAccountManager = mockk(relaxed = true)
        val accountUi =
            AccountUiView(
                settingsFragment,
                settingsFragment.lifecycleScope,
                accountManager,
                testContext.components.core.client,
            )
        val account = settingsFragment.requirePreference<Preference>(R.string.pref_key_account)
        val card = settingsFragment.requirePreference<Preference>(R.string.pref_key_account_error_card_auth)
        val legacyError = settingsFragment.requirePreference<Preference>(R.string.pref_key_account_auth_error)
        val signInPromo = settingsFragment.requirePreference<Preference>(R.string.pref_key_sign_in_promo)
        every { testContext.components.settings.accountSettingsNewUi } returns true
        every { accountManager.authenticatedAccount() } returns mockk()
        every { accountManager.accountNeedsReauth() } returns false

        accountUi.updateAccountUIState(testContext, null)
        assertTrue(account.isVisible)
        assertFalse(card.isVisible)
        assertFalse(signInPromo.isVisible)

        every { accountManager.accountNeedsReauth() } returns true
        accountUi.updateAccountUIState(testContext, null)
        assertTrue(account.isVisible)
        assertTrue(card.isVisible)
        assertFalse(legacyError.isVisible)

        every { accountManager.authenticatedAccount() } returns null
        every { accountManager.accountNeedsReauth() } returns false
        accountUi.updateAccountUIState(testContext, null)
        assertFalse(account.isVisible)
        assertFalse(card.isVisible)
        assertTrue(signInPromo.isVisible)
    }

    @Test
    fun `legacy settings show the original authentication error row`() {
        val accountManager: FxaAccountManager = mockk(relaxed = true)
        every { accountManager.authenticatedAccount() } returns mockk()
        every { accountManager.accountNeedsReauth() } returns true
        every { testContext.components.settings.accountSettingsNewUi } returns false
        val accountUi =
            AccountUiView(
                settingsFragment,
                settingsFragment.lifecycleScope,
                accountManager,
                testContext.components.core.client,
            )

        accountUi.updateAccountUIState(testContext, null)

        assertFalse(settingsFragment.requirePreference<Preference>(R.string.pref_key_account).isVisible)
        assertFalse(settingsFragment.requirePreference<Preference>(R.string.pref_key_account_error_card_auth).isVisible)
        assertTrue(settingsFragment.requirePreference<Preference>(R.string.pref_key_account_auth_error).isVisible)
    }

    @Test
    fun `account row opens settings and authentication errors open recovery`() {
        val navController: NavController = mockk(relaxed = true)
        val destination: NavDestination = mockk()
        every { destination.id } returns R.id.settingsFragment
        every { navController.currentDestination } returns destination
        Navigation.setViewNavController(activityController.get().findViewById(android.R.id.content), navController)
        activityController.start()
        val directions = mutableListOf<NavDirections>()

        settingsFragment.onPreferenceTreeClick(settingsFragment.requirePreference(R.string.pref_key_account))
        settingsFragment.onPreferenceTreeClick(
            settingsFragment.requirePreference(R.string.pref_key_account_error_card_auth)
        )
        verify(exactly = 2) { navController.navigate(capture(directions)) }
        assertEquals(R.id.action_settingsFragment_to_accountSettingsFragment, directions[0].actionId)
        assertEquals(R.id.action_settingsFragment_to_accountProblemFragment, directions[1].actionId)
        assertEquals(
            SettingsFragmentDirections.actionSettingsFragmentToAccountProblemFragment(
                org.mozilla.fenix.components.accounts.FenixFxAEntryPoint.SettingsMenu
            ),
            directions[1],
        )
    }

    @OptIn(ExperimentalCoroutinesApi::class) // advanceUntilIdle
    @Test
    fun `Add-on collection override pref is visible if debug menu active and feature is enabled`() = runTest {
        val settingsFragment = SettingsFragment()
        val activity = Robolectric.buildActivity(FragmentActivity::class.java).create().get()

        activity.supportFragmentManager.beginTransaction().add(settingsFragment, "test").commitNow()

        advanceUntilIdle()

        val preferenceAmoCollectionOverride =
            settingsFragment.findPreference<Preference>(
                settingsFragment.getPreferenceKey(R.string.pref_key_override_amo_collection)
            )

        settingsFragment.setupAmoCollectionOverridePreference(
            mockk(relaxed = true),
            true,
        )
        assertNotNull(preferenceAmoCollectionOverride)
        assertFalse(preferenceAmoCollectionOverride.isVisible)

        val settings: Settings = mockk(relaxed = true)
        every { settings.showSecretDebugMenuThisSession } returns true
        settingsFragment.setupAmoCollectionOverridePreference(
            settings,
            true,
        )
        assertTrue(preferenceAmoCollectionOverride.isVisible)
    }

    @OptIn(ExperimentalCoroutinesApi::class) // advanceUntilIdle
    @Test
    fun `Install add-on from file pref is visible if debug menu active and feature is enabled`() = runTest {
        val settingsFragment = SettingsFragment()
        val activity = Robolectric.buildActivity(FragmentActivity::class.java).create().get()

        activity.supportFragmentManager.beginTransaction().add(settingsFragment, "test").commitNow()

        advanceUntilIdle()

        val preference =
            settingsFragment.findPreference<Preference>(
                settingsFragment.getPreferenceKey(R.string.pref_key_install_local_addon)
            )

        settingsFragment.setupInstallAddonFromFilePreference(mockk(relaxed = true))
        assertNotNull(preference)
        assertFalse(preference.isVisible)

        val settings: Settings = mockk(relaxed = true)

        every { settings.showSecretDebugMenuThisSession } returns true
        settingsFragment.setupInstallAddonFromFilePreference(settings)
        assertTrue(preference.isVisible)
    }

    @OptIn(ExperimentalCoroutinesApi::class) // advanceUntilIdle
    @Test
    fun `Add-on collection override pref is visible if already configured and feature is enabled`() = runTest {
        val settingsFragment = SettingsFragment()
        val activity = Robolectric.buildActivity(FragmentActivity::class.java).create().get()

        activity.supportFragmentManager.beginTransaction().add(settingsFragment, "test").commitNow()

        advanceUntilIdle()

        val preferenceAmoCollectionOverride =
            settingsFragment.findPreference<Preference>(
                settingsFragment.getPreferenceKey(R.string.pref_key_override_amo_collection)
            )

        settingsFragment.setupAmoCollectionOverridePreference(
            mockk(relaxed = true),
            true,
        )
        assertNotNull(preferenceAmoCollectionOverride)
        assertFalse(preferenceAmoCollectionOverride.isVisible)

        val settings: Settings = mockk(relaxed = true)
        every { settings.showSecretDebugMenuThisSession } returns false

        every { settings.amoCollectionOverrideConfigured() } returns false
        settingsFragment.setupAmoCollectionOverridePreference(
            settings,
            true,
        )
        assertFalse(preferenceAmoCollectionOverride.isVisible)

        every { settings.amoCollectionOverrideConfigured() } returns true
        settingsFragment.setupAmoCollectionOverridePreference(
            settings,
            true,
        )
        assertTrue(preferenceAmoCollectionOverride.isVisible)
    }

    @OptIn(ExperimentalCoroutinesApi::class) // advanceUntilIdle
    @Test
    fun `Add-on collection override pref is not visible if feature is disabled`() = runTest {
        val settingsFragment = SettingsFragment()
        val activity = Robolectric.buildActivity(FragmentActivity::class.java).create().get()

        activity.supportFragmentManager.beginTransaction().add(settingsFragment, "test").commitNow()

        advanceUntilIdle()

        val preferenceAmoCollectionOverride =
            settingsFragment.findPreference<Preference>(
                settingsFragment.getPreferenceKey(R.string.pref_key_override_amo_collection)
            )

        val settings: Settings = mockk(relaxed = true)
        settingsFragment.setupAmoCollectionOverridePreference(
            settings,
            false,
        )
        assertNotNull(preferenceAmoCollectionOverride)
        assertFalse(preferenceAmoCollectionOverride.isVisible)

        every { settings.showSecretDebugMenuThisSession } returns true
        every { settings.amoCollectionOverrideConfigured() } returns true
        settingsFragment.setupAmoCollectionOverridePreference(
            settings,
            false,
        )
        assertFalse(preferenceAmoCollectionOverride.isVisible)
    }

    @Test
    fun `GIVEN notifications are not allowed THEN set the appropriate summary to notification preferences`() {
        val notificationPreference = settingsFragment.requirePreference<Preference>(R.string.pref_key_notifications)
        val summary = testContext.getString(R.string.notifications_not_allowed_summary)

        assertTrue(notificationPreference.summary.isNullOrEmpty())

        settingsFragment.setupNotificationPreference(false)

        assertEquals(summary, notificationPreference.summary)
    }

    @Test
    fun `GIVEN notifications are allowed THEN set the appropriate summary to notification preferences`() {
        val notificationPreference = settingsFragment.requirePreference<Preference>(R.string.pref_key_notifications)
        val summary = testContext.getString(R.string.notifications_allowed_summary)

        assertTrue(notificationPreference.summary.isNullOrEmpty())

        settingsFragment.setupNotificationPreference(true)

        assertEquals(summary, notificationPreference.summary)
    }

    @Test
    fun `GIVEN the opening screen setting is set to homepage after four hours THEN set the appropriate summary to homepage preference`() {
        val homepagePreference = settingsFragment.requirePreference<Preference>(R.string.pref_key_home)
        every { testContext.components.settings.alwaysOpenTheHomepageWhenOpeningTheApp } returns false
        every { testContext.components.settings.openHomepageAfterFourHoursOfInactivity } returns true
        every { testContext.components.settings.alwaysOpenTheLastTabWhenOpeningTheApp } returns false
        assertTrue(homepagePreference.summary.isNullOrEmpty())
        val summary = testContext.getString(R.string.opening_screen_after_four_hours_of_inactivity_summary)

        settingsFragment.setupHomepagePreference(testContext.components.settings)

        assertEquals(summary, homepagePreference.summary)
    }

    @Test
    fun `GIVEN the opening screen setting is set to last tab THEN set the appropriate summary to homepage preference`() {
        val homepagePreference = settingsFragment.requirePreference<Preference>(R.string.pref_key_home)
        every { testContext.components.settings.alwaysOpenTheHomepageWhenOpeningTheApp } returns false
        every { testContext.components.settings.openHomepageAfterFourHoursOfInactivity } returns false
        every { testContext.components.settings.alwaysOpenTheLastTabWhenOpeningTheApp } returns true
        assertTrue(homepagePreference.summary.isNullOrEmpty())
        val summary = testContext.getString(R.string.opening_screen_last_tab_summary)

        settingsFragment.setupHomepagePreference(testContext.components.settings)

        assertEquals(summary, homepagePreference.summary)
    }

    @Test
    fun `GIVEN the opening screen setting is set to homepage THEN set the appropriate summary to homepage preference`() {
        val homepagePreference = settingsFragment.requirePreference<Preference>(R.string.pref_key_home)
        every { testContext.components.settings.alwaysOpenTheHomepageWhenOpeningTheApp } returns true
        every { testContext.components.settings.openHomepageAfterFourHoursOfInactivity } returns false
        every { testContext.components.settings.alwaysOpenTheLastTabWhenOpeningTheApp } returns false
        assertTrue(homepagePreference.summary.isNullOrEmpty())
        val summary = testContext.getString(R.string.opening_screen_homepage_summary)

        settingsFragment.setupHomepagePreference(testContext.components.settings)

        assertEquals(summary, homepagePreference.summary)
    }

    @Test
    fun `WHEN a custom search engine is set as default THEN it's name is set as summary for search preference`() {
        val searchEngineName = "MySearchEngine"
        val searchPreference = settingsFragment.requirePreference<Preference>(R.string.pref_key_search_settings)

        assertTrue(searchPreference.summary.isNullOrEmpty())

        settingsFragment.setupSearchPreference(searchEngineName)

        assertEquals(searchEngineName, searchPreference.summary)
    }

    @Test
    fun `GIVEN the tracking protection preference is set to custom THEN set the appropriate summary`() {
        val trackingProtectionPreference =
            settingsFragment.requirePreference<Preference>(R.string.pref_key_tracking_protection_settings)
        every { testContext.components.settings.shouldUseTrackingProtection } returns true
        every { testContext.components.settings.useStandardTrackingProtection } returns false
        every { testContext.components.settings.useStrictTrackingProtection } returns false
        every { testContext.components.settings.useCustomTrackingProtection } returns true
        assertTrue(trackingProtectionPreference.summary.isNullOrEmpty())
        val summary = testContext.getString(R.string.tracking_protection_custom)

        settingsFragment.setupTrackingProtectionPreference(testContext.components.settings)

        assertEquals(summary, trackingProtectionPreference.summary)
    }

    @Test
    fun `GIVEN the tracking protection preference is set to strict THEN set the appropriate summary`() {
        val trackingProtectionPreference =
            settingsFragment.requirePreference<Preference>(R.string.pref_key_tracking_protection_settings)
        every { testContext.components.settings.shouldUseTrackingProtection } returns true
        every { testContext.components.settings.useStandardTrackingProtection } returns false
        every { testContext.components.settings.useStrictTrackingProtection } returns true
        every { testContext.components.settings.useCustomTrackingProtection } returns false
        assertTrue(trackingProtectionPreference.summary.isNullOrEmpty())
        val summary = testContext.getString(R.string.tracking_protection_strict)

        settingsFragment.setupTrackingProtectionPreference(testContext.components.settings)

        assertEquals(summary, trackingProtectionPreference.summary)
    }

    @Test
    fun `GIVEN the tracking protection preference is set to standard THEN set the appropriate summary`() {
        val trackingProtectionPreference =
            settingsFragment.requirePreference<Preference>(R.string.pref_key_tracking_protection_settings)
        every { testContext.components.settings.shouldUseTrackingProtection } returns true
        every { testContext.components.settings.useStandardTrackingProtection } returns true
        every { testContext.components.settings.useStrictTrackingProtection } returns false
        every { testContext.components.settings.useCustomTrackingProtection } returns false
        assertTrue(trackingProtectionPreference.summary.isNullOrEmpty())
        val summary = testContext.getString(R.string.tracking_protection_standard)

        settingsFragment.setupTrackingProtectionPreference(testContext.components.settings)

        assertEquals(summary, trackingProtectionPreference.summary)
    }

    @Test
    fun `GIVEN the tracking protection preference is disabled THEN set the appropriate summary`() {
        val trackingProtectionPreference =
            settingsFragment.requirePreference<Preference>(R.string.pref_key_tracking_protection_settings)
        every { testContext.components.settings.shouldUseTrackingProtection } returns false
        assertTrue(trackingProtectionPreference.summary.isNullOrEmpty())
        val summary = testContext.getString(R.string.tracking_protection_off)

        settingsFragment.setupTrackingProtectionPreference(testContext.components.settings)

        assertEquals(summary, trackingProtectionPreference.summary)
    }

    @Test
    fun `GIVEN the HttpsOnly is set to private tabs THEN set the appropriate preference summary`() {
        val httpsOnlyPreference =
            settingsFragment.findPreference<Preference>(
                settingsFragment.getPreferenceKey(R.string.pref_key_https_only_settings)
            )!!
        every { testContext.components.settings.shouldUseHttpsOnly } returns true
        every { testContext.components.settings.shouldUseHttpsOnlyInPrivateTabsOnly } returns true
        every { testContext.components.settings.shouldUseHttpsOnlyInAllTabs } returns false
        assertTrue(httpsOnlyPreference.summary.isNullOrEmpty())
        val summary = testContext.getString(R.string.preferences_https_only_on_private)

        settingsFragment.setupHttpsOnlyPreferences(testContext.components.settings)

        assertEquals(summary, httpsOnlyPreference.summary)
    }

    @Test
    fun `GIVEN the HttpsOnly is set to all tabs THEN set the appropriate preference summary`() {
        val httpsOnlyPreference =
            settingsFragment.findPreference<Preference>(
                settingsFragment.getPreferenceKey(R.string.pref_key_https_only_settings)
            )!!
        every { testContext.components.settings.shouldUseHttpsOnly } returns true
        every { testContext.components.settings.shouldUseHttpsOnlyInAllTabs } returns true
        every { testContext.components.settings.shouldUseHttpsOnlyInPrivateTabsOnly } returns false
        assertTrue(httpsOnlyPreference.summary.isNullOrEmpty())
        val summary = testContext.getString(R.string.preferences_https_only_on_all)

        settingsFragment.setupHttpsOnlyPreferences(testContext.components.settings)

        assertEquals(summary, httpsOnlyPreference.summary)
    }

    @Test
    fun `GIVEN the HttpsOnly is disabled THEN set the appropriate preference summary`() {
        val httpsOnlyPreference =
            settingsFragment.findPreference<Preference>(
                settingsFragment.getPreferenceKey(R.string.pref_key_https_only_settings)
            )!!
        every { testContext.components.settings.shouldUseHttpsOnly } returns false
        assertTrue(httpsOnlyPreference.summary.isNullOrEmpty())
        val summary = testContext.getString(R.string.preferences_https_only_off)

        settingsFragment.setupHttpsOnlyPreferences(testContext.components.settings)

        assertEquals(summary, httpsOnlyPreference.summary)
    }

    @Test
    fun `GIVEN an account observer WHEN the fragment is visible THEN register it for updates`() {
        val accountManager: FxaAccountManager = mockk(relaxed = true)
        every { testContext.components.backgroundServices.accountManager } returns accountManager

        settingsFragment.onStart()

        verify { accountManager.register(settingsFragment.accountObserver, settingsFragment, true) }
    }

    @Test
    fun `GIVEN an account observer WHEN the fragment stops being visible THEN unregister it for updates`() {
        val accountManager: FxaAccountManager = mockk(relaxed = true)
        every { testContext.components.backgroundServices.accountManager } returns accountManager

        settingsFragment.onStop()

        verify { accountManager.unregister(settingsFragment.accountObserver) }
    }

    @Test
    fun `GIVEN email mask feature is available for the user WHEN relay eligibility is Eligible THEN preference is visible`() {
        val preference = settingsFragment.requirePreference<Preference>(R.string.pref_key_email_masks)
        every { testContext.components.settings.isEmailMaskFeatureEnabled } returns true
        every { testContext.components.relayEligibilityStore } returns
            RelayEligibilityStore(RelayState(Eligible.Premium))

        settingsFragment.setupEmailMaskPreference(testContext.components.settings, testContext.components)

        assertTrue(preference.isVisible)
    }

    @Test
    fun `GIVEN email mask feature is available for the user WHEN relay eligibility is Ineligible THEN preference is hidden`() {
        val preference = settingsFragment.requirePreference<Preference>(R.string.pref_key_email_masks)
        every { testContext.components.settings.isEmailMaskFeatureEnabled } returns true
        every { testContext.components.relayEligibilityStore } returns
            RelayEligibilityStore(RelayState(Ineligible.FirefoxAccountNotLoggedIn))

        settingsFragment.setupEmailMaskPreference(testContext.components.settings, testContext.components)

        assertFalse(preference.isVisible)
    }

    @Test
    fun `GIVEN email mask feature is not available for the user WHEN relay eligibility is Eligible THEN preference is hidden`() {
        val preference = settingsFragment.requirePreference<Preference>(R.string.pref_key_email_masks)
        every { testContext.components.settings.isEmailMaskFeatureEnabled } returns false
        every { testContext.components.relayEligibilityStore } returns
            RelayEligibilityStore(RelayState(Eligible.Premium))

        settingsFragment.setupEmailMaskPreference(testContext.components.settings, testContext.components)

        assertFalse(preference.isVisible)
    }

    @Test
    fun `GIVEN email mask feature is not available for the user WHEN relay eligibility is Ineligible THEN preference is hidden`() {
        val preference = settingsFragment.requirePreference<Preference>(R.string.pref_key_email_masks)
        every { testContext.components.settings.isEmailMaskFeatureEnabled } returns false
        every { testContext.components.relayEligibilityStore } returns
            RelayEligibilityStore(RelayState(Ineligible.FirefoxAccountNotLoggedIn))

        settingsFragment.setupEmailMaskPreference(testContext.components.settings, testContext.components)

        assertFalse(preference.isVisible)
    }

    @OptIn(ExperimentalAndroidComponentsApi::class)
    @Test
    fun `WHEN ip protection feature is available for the user THEN preference is visible`() {
        val preference = settingsFragment.requirePreference<Preference>(R.string.pref_key_ip_protection_settings)

        settingsFragment.setupIPProtectionPreferences(
            ipProtectionStore = IPProtectionStore(initialState = IPProtectionState(EligibilityStatus.Eligible))
        )

        assertTrue(preference.isVisible)
    }

    @OptIn(ExperimentalAndroidComponentsApi::class)
    @Test
    fun `WHEN ip protection feature is not available for the user THEN preference is hidden`() {
        val preference = settingsFragment.requirePreference<Preference>(R.string.pref_key_ip_protection_settings)

        settingsFragment.setupIPProtectionPreferences(
            ipProtectionStore = IPProtectionStore(initialState = IPProtectionState(EligibilityStatus.UnsupportedRegion))
        )

        assertFalse(preference.isVisible)
    }
}
