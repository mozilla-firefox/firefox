/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.utils

import android.content.pm.PackageInfo
import android.content.pm.PackageManager
import androidx.core.content.edit
import io.mockk.every
import io.mockk.spyk
import io.mockk.verify
import java.io.File
import java.util.Calendar
import mozilla.components.concept.engine.Engine.HttpsOnlyMode.DISABLED
import mozilla.components.concept.engine.Engine.HttpsOnlyMode.ENABLED
import mozilla.components.concept.engine.Engine.HttpsOnlyMode.ENABLED_PRIVATE_ONLY
import mozilla.components.feature.sitepermissions.SitePermissionsRules
import mozilla.components.feature.sitepermissions.SitePermissionsRules.Action.ALLOWED
import mozilla.components.feature.sitepermissions.SitePermissionsRules.Action.ASK_TO_ALLOW
import mozilla.components.feature.sitepermissions.SitePermissionsRules.Action.BLOCKED
import mozilla.components.feature.sitepermissions.SitePermissionsRules.AutoplayAction
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.components.toolbar.ToolbarPosition
import org.mozilla.fenix.nimbus.AutomotiveSensitiveData
import org.mozilla.fenix.nimbus.DefaultBrowserPrompt
import org.mozilla.fenix.nimbus.FakeNimbusEventStore
import org.mozilla.fenix.nimbus.FxNimbus
import org.mozilla.fenix.nimbus.HomescreenEdgeToEdgeBackground
import org.mozilla.fenix.settings.PhoneFeature
import org.mozilla.fenix.settings.ShortcutType
import org.mozilla.fenix.settings.deletebrowsingdata.DeleteBrowsingDataOnQuitType
import org.mozilla.fenix.wallpapers.Wallpaper
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf

private const val TOU_VERSION = 5

@RunWith(RobolectricTestRunner::class)
class SettingsTest {

    lateinit var settings: Settings

    private val defaultPermissions =
        SitePermissionsRules(
            camera = ASK_TO_ALLOW,
            location = ASK_TO_ALLOW,
            microphone = ASK_TO_ALLOW,
            notification = ASK_TO_ALLOW,
            autoplayAudible = AutoplayAction.BLOCKED,
            autoplayInaudible = AutoplayAction.ALLOWED,
            persistentStorage = ASK_TO_ALLOW,
            mediaKeySystemAccess = ASK_TO_ALLOW,
            crossOriginStorageAccess = ASK_TO_ALLOW,
            localDeviceAccess = ASK_TO_ALLOW,
            localNetworkAccess = ASK_TO_ALLOW,
        )

    @Before
    fun setUp() {
        settings = Settings(testContext)
    }

    @Test
    fun launchLinksInPrivateTab() {
        // When just created
        // Then
        assertFalse(settings.openLinksInAPrivateTab)

        // When
        settings.openLinksInAPrivateTab = true

        // Then
        assertTrue(settings.openLinksInAPrivateTab)
    }

    @Test
    fun shouldReturnToBrowser() {
        // When just created
        // Then
        assertFalse(settings.shouldReturnToBrowser)

        // When
        settings.shouldReturnToBrowser = true

        // Then
        assertTrue(settings.shouldReturnToBrowser)
    }

    @Test
    fun clearDataOnQuit() {
        // When just created
        // Then
        assertFalse(settings.shouldDeleteBrowsingDataOnQuit)

        // When
        settings.shouldDeleteBrowsingDataOnQuit = true

        // Then
        assertTrue(settings.shouldDeleteBrowsingDataOnQuit)

        // When
        settings.shouldDeleteBrowsingDataOnQuit = false

        // Then
        assertFalse(settings.shouldDeleteBrowsingDataOnQuit)
    }

    @Test
    fun clearAnyDataOnQuit() {
        // When just created
        // Then
        assertFalse(settings.shouldDeleteAnyDataOnQuit())

        // When
        settings.setDeleteDataOnQuit(DeleteBrowsingDataOnQuitType.TABS, true)

        // Then
        assertTrue(settings.shouldDeleteAnyDataOnQuit())

        // When
        settings.setDeleteDataOnQuit(DeleteBrowsingDataOnQuitType.PERMISSIONS, true)

        // Then
        assertTrue(settings.shouldDeleteAnyDataOnQuit())

        // When
        settings.setDeleteDataOnQuit(DeleteBrowsingDataOnQuitType.TABS, false)
        settings.setDeleteDataOnQuit(DeleteBrowsingDataOnQuitType.PERMISSIONS, false)

        // Then
        assertFalse(settings.shouldDeleteAnyDataOnQuit())
    }

    @Test
    fun defaultSearchEngineName() {
        // When just created
        // Then
        assertEquals("", settings.defaultSearchEngineName)

        // When
        settings.defaultSearchEngineName = "Mozilla"

        // Then
        assertEquals("Mozilla", settings.defaultSearchEngineName)
    }

    @Test
    fun defaultWallpaperIsEdgeToEdgeWhenEdgeToEdgeFeatureEnabled() {
        FxNimbus.features.homescreenEdgeToEdgeBackground.withCachedValue(HomescreenEdgeToEdgeBackground(enabled = true))
        val settings = Settings(testContext)

        // Then
        assertEquals(Wallpaper.EdgeToEdge.name, settings.currentWallpaperName)
    }

    @Test
    fun defaultWallpaperIsDefaultWhenEdgeToEdgeDisabled() {
        FxNimbus.features.homescreenEdgeToEdgeBackground.withCachedValue(
            HomescreenEdgeToEdgeBackground(enabled = false)
        )
        val settings = Settings(testContext)

        // Then
        assertEquals(Wallpaper.Default.name, settings.currentWallpaperName)
    }

    @Test
    fun isRemoteDebuggingEnabled() {
        // When just created
        // Then
        assertFalse(settings.isRemoteDebuggingEnabled)
    }

    @Test
    fun canShowCfrTest() {
        // When just created
        // Then
        assertEquals(0L, settings.lastCfrShownTimeInMillis)
        assertTrue(settings.canShowCfr)

        // When
        settings.lastCfrShownTimeInMillis = System.currentTimeMillis()

        // Then
        assertFalse(settings.canShowCfr)
    }

    @Test
    fun isTelemetryEnabled() {
        // When just created
        // Then
        assertTrue(settings.isTelemetryEnabled)
    }

    @Test
    fun showLoginsDialogWarningSync() {
        // When just created
        // Then
        assertEquals(0, settings.loginsSecureWarningSyncCount.value)

        // When
        settings.incrementShowLoginsSecureWarningSyncCount()

        // Then
        assertEquals(1, settings.loginsSecureWarningSyncCount.value)
    }

    @Test
    fun shouldShowLoginsDialogWarningSync() {
        // When just created
        // Then
        assertTrue(settings.shouldShowSecurityPinWarningSync)

        // When
        settings.incrementShowLoginsSecureWarningSyncCount()

        // Then
        assertFalse(settings.shouldShowSecurityPinWarningSync)
    }

    @Test
    fun showLoginsDialogWarning() {
        // When just created
        // Then
        assertEquals(0, settings.secureWarningCount.value)

        // When
        settings.incrementSecureWarningCount()

        // Then
        assertEquals(1, settings.secureWarningCount.value)
    }

    @Test
    fun shouldShowLoginsDialogWarning() {
        // When just created
        // Then
        assertTrue(settings.shouldShowSecurityPinWarning)

        // When
        settings.incrementSecureWarningCount()

        // Then
        assertFalse(settings.shouldShowSecurityPinWarning)
    }

    @Test
    fun shouldUseLightTheme() {
        // When just created
        // Then
        assertFalse(settings.shouldUseLightTheme)

        // When
        settings.shouldUseLightTheme = true

        // Then
        assertTrue(settings.shouldUseLightTheme)
    }

    @Test
    fun shouldManuallyCloseTabs() {
        // When just created
        // Then
        assertTrue(settings.manuallyCloseTabs)

        // When
        settings.manuallyCloseTabs = false

        // Then
        assertFalse(settings.manuallyCloseTabs)
    }

    @Test
    fun getTabTimeout() {
        // When just created
        // Then
        assertTrue(settings.manuallyCloseTabs)
        assertEquals(Long.MAX_VALUE, settings.getTabTimeout())

        // When
        settings.manuallyCloseTabs = false
        settings.closeTabsAfterOneDay = true

        // Then
        assertEquals(Settings.ONE_DAY_MS, settings.getTabTimeout())

        // When
        settings.closeTabsAfterOneDay = false
        settings.closeTabsAfterOneWeek = true

        // Then
        assertEquals(Settings.ONE_WEEK_MS, settings.getTabTimeout())

        // When
        settings.closeTabsAfterOneWeek = false
        settings.closeTabsAfterOneMonth = true

        // Then
        assertEquals(Settings.ONE_MONTH_MS, settings.getTabTimeout())
    }

    @Test
    fun shouldUseAutoSize() {
        // When just created
        // Then
        assertTrue(settings.shouldUseAutoSize)

        // When
        settings.shouldUseAutoSize = false

        // Then
        assertFalse(settings.shouldUseAutoSize)
    }

    @Test
    fun shouldAutofill() {
        // When just created
        // Then
        assertTrue(settings.shouldAutofillLogins)

        // When
        settings.shouldAutofillLogins = false

        // Then
        assertFalse(settings.shouldAutofillLogins)
    }

    @Test
    fun `GIVEN Android Automotive WHEN reading the autofill settings THEN they are all disabled by default`() {
        shadowOf(testContext.packageManager).setSystemFeature(PackageManager.FEATURE_AUTOMOTIVE, true)
        val automotiveSettings = Settings(testContext)

        assertFalse(automotiveSettings.isAutofillSupported)
        assertFalse(automotiveSettings.shouldPromptToSaveLogins)
        assertFalse(automotiveSettings.shouldAutofillLogins)
        assertFalse(automotiveSettings.shouldAutofillCreditCardDetails)
        assertFalse(automotiveSettings.shouldAutofillAddressDetails)
    }

    @Test
    fun `GIVEN Android Automotive and the sensitive data feature is enabled WHEN reading the autofill settings THEN they are all enabled by default`() {
        shadowOf(testContext.packageManager).setSystemFeature(PackageManager.FEATURE_AUTOMOTIVE, true)
        FxNimbus.features.automotiveSensitiveData.withCachedValue(AutomotiveSensitiveData(enabled = true))
        val automotiveSettings = Settings(testContext)

        assertTrue(automotiveSettings.isAutofillSupported)
        assertTrue(automotiveSettings.shouldPromptToSaveLogins)
        assertTrue(automotiveSettings.shouldAutofillLogins)
        assertTrue(automotiveSettings.shouldAutofillCreditCardDetails)
        assertTrue(automotiveSettings.shouldAutofillAddressDetails)

        FxNimbus.features.automotiveSensitiveData.withCachedValue(null)
    }

    @Test
    fun fontSizeFactor() {
        // When just created
        // Then
        assertEquals(1f, settings.fontSizeFactor)

        // When
        settings.fontSizeFactor = 2f

        // Then
        assertEquals(2f, settings.fontSizeFactor)
    }

    @Test
    fun shouldShowClipboardSuggestion() {
        // When just created
        // Then
        assertTrue(settings.shouldShowClipboardSuggestions)
    }

    @Test
    fun shouldShowHistorySuggestions() {
        // When just created
        // Then
        assertTrue(settings.shouldShowHistorySuggestions)
    }

    @Test
    fun shouldShowBookmarkSuggestions() {
        // When just created
        // Then
        assertTrue(settings.shouldShowBookmarkSuggestions)
    }

    @Test
    fun shouldUseDarkTheme() {
        // When just created
        // Then
        assertFalse(settings.shouldUseDarkTheme)
    }

    @Test
    fun shouldUseOledTheme() {
        // When just created
        // Then
        assertFalse(settings.shouldUseOledTheme)

        // When
        settings.shouldUseOledTheme = true

        // Then
        assertTrue(settings.shouldUseOledTheme)
    }

    @Test
    fun shouldFollowDeviceTheme() {
        // When just created
        // Then
        assertFalse(settings.shouldFollowDeviceTheme)

        // When
        settings.shouldFollowDeviceTheme = true

        // Then
        assertTrue(settings.shouldFollowDeviceTheme)
    }

    @Test
    fun shouldUseTrackingProtection() {
        // When
        // Then
        assertTrue(settings.shouldUseTrackingProtection)

        // When
        settings.shouldUseTrackingProtection = false

        // Then
        assertFalse(settings.shouldUseTrackingProtection)
    }

    @Test
    fun shouldSetOpenInAppOpened() {
        // When
        // Then
        assertFalse(settings.openInAppOpened)

        // When
        settings.openInAppOpened = true

        // Then
        assertTrue(settings.openInAppOpened)
    }

    @Test
    fun shouldSetInstallPwaOpened() {
        // When
        // Then
        assertFalse(settings.installPwaOpened)

        // When
        settings.installPwaOpened = true

        // Then
        assertTrue(settings.installPwaOpened)
    }

    @Test
    fun shouldUseTrackingProtectionStrict() {
        // When
        // Then
        assertFalse(settings.useStrictTrackingProtection)
    }

    @Test
    fun shouldUseAutoBatteryTheme() {
        // When just created
        // Then
        assertFalse(settings.shouldUseAutoBatteryTheme)
    }

    @Test
    fun showSearchSuggestions() {
        // When just created
        // Then
        assertTrue(settings.shouldShowSearchSuggestions)
    }

    @Test
    fun sitePermissionsPhoneFeatureCameraAction() {
        // When just created
        // Then
        assertEquals(
            ASK_TO_ALLOW,
            settings.getSitePermissionsPhoneFeatureAction(PhoneFeature.CAMERA),
        )

        // When
        settings.setSitePermissionsPhoneFeatureAction(PhoneFeature.CAMERA, BLOCKED)

        // Then
        assertEquals(BLOCKED, settings.getSitePermissionsPhoneFeatureAction(PhoneFeature.CAMERA))
    }

    @Test
    fun sitePermissionsPhoneFeatureMicrophoneAction() {
        // When just created
        // Then
        assertEquals(
            ASK_TO_ALLOW,
            settings.getSitePermissionsPhoneFeatureAction(PhoneFeature.MICROPHONE),
        )

        // When
        settings.setSitePermissionsPhoneFeatureAction(PhoneFeature.MICROPHONE, BLOCKED)

        // Then
        assertEquals(
            BLOCKED,
            settings.getSitePermissionsPhoneFeatureAction(PhoneFeature.MICROPHONE),
        )
    }

    @Test
    fun sitePermissionsPhoneFeatureNotificationAction() {
        // When just created
        // Then
        assertEquals(
            ASK_TO_ALLOW,
            settings.getSitePermissionsPhoneFeatureAction(PhoneFeature.NOTIFICATION),
        )

        // When
        settings.setSitePermissionsPhoneFeatureAction(PhoneFeature.NOTIFICATION, BLOCKED)

        // Then
        assertEquals(
            BLOCKED,
            settings.getSitePermissionsPhoneFeatureAction(PhoneFeature.NOTIFICATION),
        )
    }

    @Test
    fun sitePermissionsPhoneFeatureLocation() {
        // When just created
        // Then
        assertEquals(
            ASK_TO_ALLOW,
            settings.getSitePermissionsPhoneFeatureAction(PhoneFeature.LOCATION),
        )

        // When
        settings.setSitePermissionsPhoneFeatureAction(PhoneFeature.LOCATION, BLOCKED)

        // Then
        assertEquals(BLOCKED, settings.getSitePermissionsPhoneFeatureAction(PhoneFeature.LOCATION))
    }

    @Test
    fun getSitePermissionsCustomSettingsRules_default() {
        // When just created
        // Then
        assertEquals(
            defaultPermissions,
            settings.getSitePermissionsCustomSettingsRules(),
        )
    }

    @Test
    fun getSitePermissionsCustomSettingsRules_camera() {
        // When
        settings.setSitePermissionsPhoneFeatureAction(PhoneFeature.CAMERA, BLOCKED)

        // Then
        assertEquals(
            defaultPermissions.copy(camera = BLOCKED),
            settings.getSitePermissionsCustomSettingsRules(),
        )
    }

    @Test
    fun getSitePermissionsCustomSettingsRules_notification() {
        // When
        settings.setSitePermissionsPhoneFeatureAction(PhoneFeature.NOTIFICATION, BLOCKED)

        // Then
        assertEquals(
            defaultPermissions.copy(notification = BLOCKED),
            settings.getSitePermissionsCustomSettingsRules(),
        )
    }

    @Test
    fun getSitePermissionsCustomSettingsRules_location() {
        // When
        settings.setSitePermissionsPhoneFeatureAction(PhoneFeature.LOCATION, BLOCKED)

        // Then
        assertEquals(
            defaultPermissions.copy(location = BLOCKED),
            settings.getSitePermissionsCustomSettingsRules(),
        )
    }

    @Test
    fun getSitePermissionsCustomSettingsRules_microphone() {
        // When
        settings.setSitePermissionsPhoneFeatureAction(PhoneFeature.MICROPHONE, BLOCKED)

        // Then
        assertEquals(
            defaultPermissions.copy(microphone = BLOCKED),
            settings.getSitePermissionsCustomSettingsRules(),
        )
    }

    @Test
    fun getSitePermissionsCustomSettingsRules_localDeviceAccess() {
        // When
        settings.setSitePermissionsPhoneFeatureAction(PhoneFeature.LOCAL_DEVICE_ACCESS, BLOCKED)

        // Then
        assertEquals(
            defaultPermissions.copy(localDeviceAccess = BLOCKED),
            settings.getSitePermissionsCustomSettingsRules(),
        )
    }

    @Test
    fun getSitePermissionsCustomSettingsRules_localNetworkAccess() {
        // When
        settings.setSitePermissionsPhoneFeatureAction(PhoneFeature.LOCAL_NETWORK_ACCESS, BLOCKED)

        // Then
        assertEquals(
            defaultPermissions.copy(localNetworkAccess = BLOCKED),
            settings.getSitePermissionsCustomSettingsRules(),
        )
    }

    @Test
    fun getSitePermissionsCustomSettingsRules_autoplayAudible() {
        settings.setSitePermissionsPhoneFeatureAction(PhoneFeature.AUTOPLAY_AUDIBLE, ALLOWED)

        assertEquals(
            defaultPermissions.copy(autoplayAudible = AutoplayAction.ALLOWED),
            settings.getSitePermissionsCustomSettingsRules(),
        )
    }

    @Test
    fun getSitePermissionsCustomSettingsRules_autoplayInaudible() {
        settings.setSitePermissionsPhoneFeatureAction(PhoneFeature.AUTOPLAY_INAUDIBLE, ALLOWED)

        assertEquals(
            defaultPermissions.copy(autoplayInaudible = AutoplayAction.ALLOWED),
            settings.getSitePermissionsCustomSettingsRules(),
        )
    }

    @Test
    fun getSitePermissionsCustomSettingsRules_autoplay_defaults() {
        val settings = Settings(testContext)

        assertEquals(
            AutoplayAction.BLOCKED,
            settings.getSitePermissionsCustomSettingsRules().autoplayAudible,
        )

        assertEquals(
            AutoplayAction.ALLOWED,
            settings.getSitePermissionsCustomSettingsRules().autoplayInaudible,
        )
    }

    @Test
    fun getSitePermissionsCustomSettingsRules_persistentStorage() {
        settings.setSitePermissionsPhoneFeatureAction(PhoneFeature.PERSISTENT_STORAGE, ALLOWED)

        assertEquals(
            defaultPermissions.copy(persistentStorage = ALLOWED),
            settings.getSitePermissionsCustomSettingsRules(),
        )

        settings.setSitePermissionsPhoneFeatureAction(PhoneFeature.PERSISTENT_STORAGE, BLOCKED)

        assertEquals(
            defaultPermissions.copy(persistentStorage = BLOCKED),
            settings.getSitePermissionsCustomSettingsRules(),
        )
    }

    @Test
    fun getSitePermissionsCustomSettingsRules_crossOriginStorageAccess() {
        settings.setSitePermissionsPhoneFeatureAction(PhoneFeature.CROSS_ORIGIN_STORAGE_ACCESS, ALLOWED)

        assertEquals(
            defaultPermissions.copy(crossOriginStorageAccess = ALLOWED),
            settings.getSitePermissionsCustomSettingsRules(),
        )

        settings.setSitePermissionsPhoneFeatureAction(PhoneFeature.CROSS_ORIGIN_STORAGE_ACCESS, BLOCKED)

        assertEquals(
            defaultPermissions.copy(crossOriginStorageAccess = BLOCKED),
            settings.getSitePermissionsCustomSettingsRules(),
        )
    }

    @Test
    fun getSitePermissionsCustomSettingsRules_mediaKeySystemAccess() {
        settings.setSitePermissionsPhoneFeatureAction(PhoneFeature.MEDIA_KEY_SYSTEM_ACCESS, ALLOWED)

        assertEquals(
            defaultPermissions.copy(mediaKeySystemAccess = ALLOWED),
            settings.getSitePermissionsCustomSettingsRules(),
        )

        settings.setSitePermissionsPhoneFeatureAction(PhoneFeature.MEDIA_KEY_SYSTEM_ACCESS, BLOCKED)

        assertEquals(
            defaultPermissions.copy(mediaKeySystemAccess = BLOCKED),
            settings.getSitePermissionsCustomSettingsRules(),
        )
    }

    @Test
    fun overrideAmoCollection() {
        // When just created
        // Then
        assertEquals("", settings.overrideAmoCollection)
        assertFalse(settings.amoCollectionOverrideConfigured())

        // When
        settings.overrideAmoCollection = "testCollection"

        // Then
        assertEquals("testCollection", settings.overrideAmoCollection)
        assertTrue(settings.amoCollectionOverrideConfigured())
    }

    @Test
    fun overrideAmoUser() {
        // When just created
        // Then
        assertEquals("", settings.overrideAmoUser)
        assertFalse(settings.amoCollectionOverrideConfigured())

        // When
        settings.overrideAmoUser = "testAmoUser"

        // Then
        assertEquals("testAmoUser", settings.overrideAmoUser)
        assertTrue(settings.amoCollectionOverrideConfigured())
    }

    @Test
    fun `GIVEN startOnHomeAlways is selected WHEN calling shouldStartOnHome THEN return true`() {
        settings.alwaysOpenTheHomepageWhenOpeningTheApp = true
        settings.alwaysOpenTheLastTabWhenOpeningTheApp = false
        settings.openHomepageAfterFourHoursOfInactivity = false

        assertTrue(settings.shouldStartOnHome())
    }

    @Test
    fun `GIVEN startOnHomeNever is selected WHEN calling shouldStartOnHome THEN return be false`() {
        settings.alwaysOpenTheLastTabWhenOpeningTheApp = true
        settings.alwaysOpenTheHomepageWhenOpeningTheApp = false
        settings.openHomepageAfterFourHoursOfInactivity = false

        assertFalse(settings.shouldStartOnHome())
    }

    @Test
    fun `GIVEN startOnHomeAfterFourHours is selected after four hours of inactivity WHEN calling shouldStartOnHome THEN return true`() {
        val localSetting = spyk(settings)
        val now = Calendar.getInstance()

        localSetting.openHomepageAfterFourHoursOfInactivity = true
        localSetting.alwaysOpenTheLastTabWhenOpeningTheApp = false
        localSetting.alwaysOpenTheHomepageWhenOpeningTheApp = false

        now.timeInMillis = System.currentTimeMillis()
        localSetting.lastBrowseActivity = now.timeInMillis
        now.add(Calendar.HOUR, 4)

        every { localSetting.timeNowInMillis() } returns now.timeInMillis

        assertTrue(localSetting.shouldStartOnHome())
    }

    @Test
    fun `GIVEN startOnHomeAfterFourHours is selected and with recent activity WHEN calling shouldStartOnHome THEN return false`() {
        val localSetting = spyk(settings)
        val now = System.currentTimeMillis()

        localSetting.openHomepageAfterFourHoursOfInactivity = true
        localSetting.alwaysOpenTheLastTabWhenOpeningTheApp = false
        localSetting.alwaysOpenTheHomepageWhenOpeningTheApp = false

        localSetting.lastBrowseActivity = now

        every { localSetting.timeNowInMillis() } returns now

        assertFalse(localSetting.shouldStartOnHome())
    }

    @Test
    fun inactiveTabsAreEnabled() {
        // When just created
        // Then
        assertTrue(settings.inactiveTabsAreEnabled)
    }

    @Test
    fun `GIVEN shouldShowInactiveTabsAutoCloseDialog WHEN the dialog has been dismissed before THEN no show the dialog`() {
        val settings = spyk(settings)
        every { settings.hasInactiveTabsAutoCloseDialogBeenDismissed } returns true

        assertFalse(settings.shouldShowInactiveTabsAutoCloseDialog(20))
    }

    @Test
    fun `GIVEN shouldShowInactiveTabsAutoCloseDialog WHEN the inactive tabs are less than the minimum THEN no show the dialog`() {
        assertFalse(settings.shouldShowInactiveTabsAutoCloseDialog(19))
    }

    @Test
    fun `GIVEN shouldShowInactiveTabsAutoCloseDialog WHEN closeTabsAfterOneMonth is already selected THEN no show the dialog`() {
        val settings = spyk(settings)
        every { settings.closeTabsAfterOneMonth } returns true

        assertFalse(settings.shouldShowInactiveTabsAutoCloseDialog(19))
    }

    @Test
    fun `GIVEN shouldShowInactiveTabsAutoCloseDialog WHEN the dialog has not been dismissed, with more inactive tabs than the queried and closeTabsAfterOneMonth not set THEN show the dialog`() {
        val settings = spyk(settings)
        every { settings.closeTabsAfterOneMonth } returns false
        every { settings.hasInactiveTabsAutoCloseDialogBeenDismissed } returns false

        assertTrue(settings.shouldShowInactiveTabsAutoCloseDialog(20))
    }

    @Test
    fun `GIVEN feature is disabled, hasUserBeenOnboarded is true THEN shouldShowOnboarding returns false`() {
        val settings = spyk(settings)

        val actual =
            settings.shouldShowOnboarding(
                featureEnabled = false,
                hasUserBeenOnboarded = true,
            )

        assertFalse(actual)
    }

    @Test
    fun `GIVEN feature is enabled, hasUserBeenOnboarded is true THEN shouldShowOnboarding returns false`() {
        val settings = spyk(settings)

        val actual =
            settings.shouldShowOnboarding(
                featureEnabled = true,
                hasUserBeenOnboarded = true,
            )

        assertFalse(actual)
    }

    @Test
    fun `GIVEN feature is enabled, hasUserBeenOnboarded is false THEN shouldShowOnboarding returns true`() {
        val settings = spyk(settings)

        val actual =
            settings.shouldShowOnboarding(
                featureEnabled = true,
                hasUserBeenOnboarded = false,
            )

        assertTrue(actual)
    }

    @Test
    fun `GIVEN should not show by default WHEN enablePersistentOnboarding is true THEN shouldShowOnboarding returns true`() {
        val settings = spyk(settings)
        every { settings.enablePersistentOnboarding } returns true

        val actual =
            settings.shouldShowOnboarding(
                featureEnabled = false,
                hasUserBeenOnboarded = true,
            )

        assertTrue(actual)
    }

    @Test
    fun `GIVEN a benchmark build WHEN onboarding is not forced THEN shouldShowOnboarding returns false`() {
        val settings = Settings(appContext = testContext, isBenchmarkBuild = true)

        assertFalse(
            settings.shouldShowOnboarding(
                featureEnabled = true,
                hasUserBeenOnboarded = false,
                forceOnboardingForBenchmark = false,
            )
        )
    }

    @Test
    fun `GIVEN a benchmark build WHEN onboarding is forced THEN shouldShowOnboarding returns true`() {
        val settings = Settings(appContext = testContext, isBenchmarkBuild = true)

        assertTrue(
            settings.shouldShowOnboarding(
                featureEnabled = true,
                hasUserBeenOnboarded = false,
                forceOnboardingForBenchmark = true,
            )
        )
    }

    @Test
    fun `GIVEN a benchmark build WHEN onboarding is forced and user is already onboarded THEN shouldShowOnboarding returns false`() {
        val settings = Settings(appContext = testContext, isBenchmarkBuild = true)

        assertFalse(
            settings.shouldShowOnboarding(
                featureEnabled = true,
                hasUserBeenOnboarded = true,
                forceOnboardingForBenchmark = true,
            )
        )
    }

    @Test
    fun `GIVEN Https-only mode is disabled THEN the engine mode is HttpsOnlyMode#DISABLED`() {
        settings.shouldUseHttpsOnly = false

        val result = settings.getHttpsOnlyMode()

        assertEquals(DISABLED, result)
    }

    @Test
    fun `GIVEN Https-only mode is enabled THEN the engine mode is HttpsOnlyMode#ENABLED`() {
        settings.shouldUseHttpsOnly = true

        val result = settings.getHttpsOnlyMode()

        assertEquals(ENABLED, result)
    }

    @Test
    fun `GIVEN Https-only mode is enabled for all tabs THEN the engine mode is HttpsOnlyMode#ENABLED`() {
        settings.apply {
            shouldUseHttpsOnly = true
            shouldUseHttpsOnlyInAllTabs = true
        }

        val result = settings.getHttpsOnlyMode()

        assertEquals(ENABLED, result)
    }

    @Test
    fun `GIVEN Https-only mode is enabled for only private tabs THEN the engine mode is HttpsOnlyMode#ENABLED_PRIVATE_ONLY`() {
        settings.apply {
            shouldUseHttpsOnly = true
            shouldUseHttpsOnlyInPrivateTabsOnly = true
        }

        val result = settings.getHttpsOnlyMode()

        assertEquals(ENABLED_PRIVATE_ONLY, result)
    }

    @Test
    fun `GIVEN unset user preferences THEN https-only is disabled`() {
        assertFalse(settings.shouldUseHttpsOnly)
    }

    @Test
    fun `GIVEN unset user preferences THEN https-only is enabled for all tabs`() {
        assertTrue(settings.shouldUseHttpsOnlyInAllTabs)
    }

    @Test
    fun `GIVEN unset user preferences THEN https-only is disabled for private tabs`() {
        assertFalse(settings.shouldUseHttpsOnlyInPrivateTabsOnly)
    }

    @Test
    fun `GIVEN open links in apps setting THEN return the correct display string`() {
        settings.openLinksInExternalApp = "pref_key_open_links_in_apps_always"
        settings.lastKnownMode = BrowsingMode.Normal
        assertEquals(settings.getOpenLinksInAppsString(), "Always")

        settings.openLinksInExternalApp = "pref_key_open_links_in_apps_ask"
        assertEquals(settings.getOpenLinksInAppsString(), "Ask before opening")

        settings.openLinksInExternalApp = "pref_key_open_links_in_apps_never"
        assertEquals(settings.getOpenLinksInAppsString(), "Never")

        settings.openLinksInExternalApp = "pref_key_open_links_in_apps_always"
        settings.lastKnownMode = BrowsingMode.Private
        assertEquals(settings.getOpenLinksInAppsString(), "Ask before opening")

        settings.openLinksInExternalApp = "pref_key_open_links_in_apps_ask"
        assertEquals(settings.getOpenLinksInAppsString(), "Ask before opening")

        settings.openLinksInExternalApp = "pref_key_open_links_in_apps_never"
        assertEquals(settings.getOpenLinksInAppsString(), "Never")
    }

    @Test
    fun `GIVEN a written integer value for pref_key_search_widget_installed WHEN reading searchWidgetInstalled THEN do not throw a ClassCastException`() {
        val expectedInt = 5
        val oldPrefKey = "pref_key_search_widget_installed"

        settings.preferences.edit().putInt(oldPrefKey, expectedInt).apply()

        try {
            assertEquals(expectedInt, settings.preferences.getInt(oldPrefKey, 0))
            assertFalse(settings.searchWidgetInstalled)
        } catch (e: ClassCastException) {
            fail("Unexpected ClassCastException")
        }
    }

    @Test
    fun `GIVEN previously stored pref_key_search_widget_installed value WHEN calling migrateSearchWidgetInstalledIfNeeded THEN migrate the value`() {
        val expectedInt = 5
        val oldPrefKey = "pref_key_search_widget_installed"

        settings.preferences.edit().putInt(oldPrefKey, expectedInt).apply()

        assertEquals(expectedInt, settings.preferences.getInt(oldPrefKey, 0))
        assertFalse(settings.searchWidgetInstalled)

        settings.migrateSearchWidgetInstalledPrefIfNeeded()

        assertTrue(settings.searchWidgetInstalled)
    }

    @Test
    fun `GIVEN none previously stored pref_key_search_widget_installed value WHEN calling migrateSearchWidgetInstalledIfNeeded THEN migration should not happen`() {
        val oldPrefKey = "pref_key_search_widget_installed"
        val expectedDefaultValue = 0
        val storedValue = settings.preferences.getInt(oldPrefKey, expectedDefaultValue)

        assertEquals(expectedDefaultValue, storedValue)

        settings.migrateSearchWidgetInstalledPrefIfNeeded()

        assertEquals(expectedDefaultValue, settings.preferences.getInt(oldPrefKey, expectedDefaultValue))
        assertFalse(settings.searchWidgetInstalled)
    }

    @Test
    fun `GIVEN previously stored pref_key_search_widget_installed value is Boolean WHEN calling migrateSearchWidgetInstalledIfNeeded THEN crash should not happen`() {
        val oldPrefKey = "pref_key_search_widget_installed"
        settings.preferences.edit().putBoolean(oldPrefKey, false).apply()

        settings.migrateSearchWidgetInstalledPrefIfNeeded()
        assertFalse(settings.searchWidgetInstalled)
    }

    @Test
    fun `GIVEN the legacy pocket database has not been deleted WHEN deleteLegacyPocketDatabaseIfNeeded is called THEN the database is deleted`() {
        val context = spyk(testContext)
        val settings = Settings(context)

        settings.deletePocketDatabaseIfNeeded()

        verify { context.deleteDatabase("pocket_recommendations") }
    }

    @Test
    fun `GIVEN the legacy pocket database was already deleted WHEN deleteLegacyPocketDatabaseIfNeeded is called again THEN the database is not deleted a second time`() {
        val context = spyk(testContext)
        val settings = Settings(context)

        settings.deletePocketDatabaseIfNeeded()
        settings.deletePocketDatabaseIfNeeded()

        verify(exactly = 1) { context.deleteDatabase("pocket_recommendations") }
    }

    @Test
    fun `GIVEN an existing report site domains datastore WHEN deleteReportSiteDomainsDataStoreIfNeeded is called THEN the datastore is deleted`() {
        val settings = Settings(testContext)
        val dataStoreFile = File(testContext.filesDir, "datastore/report_site_domains_preferences.preferences_pb")
        dataStoreFile.parentFile?.mkdirs()
        dataStoreFile.createNewFile()
        assertTrue(dataStoreFile.exists())

        settings.deleteReportSiteDomainsDataStoreIfNeeded()

        assertFalse(dataStoreFile.exists())
    }

    @Test
    fun `GIVEN the report site domains datastore was already deleted WHEN deleteReportSiteDomainsDataStoreIfNeeded is called again THEN the datastore is not deleted a second time`() {
        val settings = Settings(testContext)
        val dataStoreFile = File(testContext.filesDir, "datastore/report_site_domains_preferences.preferences_pb")
        dataStoreFile.parentFile?.mkdirs()
        dataStoreFile.createNewFile()

        settings.deleteReportSiteDomainsDataStoreIfNeeded()
        assertFalse(dataStoreFile.exists())

        dataStoreFile.createNewFile()
        settings.deleteReportSiteDomainsDataStoreIfNeeded()
        assertTrue(dataStoreFile.exists())
    }

    @Test
    fun `GIVEN the tab strip is enabled at the top WHEN checking if it should be shown at the top THEN return true`() {
        settings.isTabStripEnabled = true
        settings.shouldUseBottomTabStrip = false

        assertTrue(settings.shouldShowTabStripAtTop)
        assertFalse(settings.shouldShowTabStripAtBottom)
    }

    @Test
    fun `GIVEN the tab strip is enabled at the bottom WHEN checking if it should be shown at the bottom THEN return true`() {
        settings.isTabStripEnabled = true
        settings.shouldUseBottomTabStrip = true

        assertFalse(settings.shouldShowTabStripAtTop)
        assertTrue(settings.shouldShowTabStripAtBottom)
    }

    @Test
    fun `GIVEN the tab strip is disabled WHEN checking where it should be shown THEN return false`() {
        settings.isTabStripEnabled = false

        settings.shouldUseBottomTabStrip = false
        assertFalse(settings.shouldShowTabStripAtTop)
        assertFalse(settings.shouldShowTabStripAtBottom)

        settings.shouldUseBottomTabStrip = true
        assertFalse(settings.shouldShowTabStripAtTop)
        assertFalse(settings.shouldShowTabStripAtBottom)
    }

    @Test
    fun `GIVEN top composable toolbar is enabled WHEN querying the toolbar height THEN get the height of the composable toolbar`() {
        val settings = spyk(settings)
        every { settings.toolbarPosition } returns ToolbarPosition.TOP

        assertEquals(64, settings.getBrowserToolbarHeight(testContext))
    }

    @Test
    fun `GIVEN bottom composable toolbar is enabled and navigation bar is disabled WHEN querying the toolbar height THEN get the height of the composable toolbar`() {
        val settings = spyk(settings)
        every { settings.toolbarPosition } returns ToolbarPosition.BOTTOM
        every { settings.shouldUseExpandedToolbar } returns false

        assertEquals(64, settings.getBrowserToolbarHeight(testContext))
    }

    @Test
    fun `GIVEN bottom composable toolbar is enabled and navigation bar is enabled WHEN querying the toolbar height THEN get the height of the composable toolbar`() {
        testContext.resources.configuration.apply {
            screenHeightDp = 481
            screenWidthDp = 599
        }
        val settings = spyk(settings)
        every { settings.toolbarPosition } returns ToolbarPosition.BOTTOM
        every { settings.shouldUseExpandedToolbar } returns true

        assertEquals(56, settings.getBrowserToolbarHeight(testContext))
    }

    @Test
    fun `GIVEN bottom composable toolbar is enabled and navigation bar is enabled but window is not tall enough WHEN querying the toolbar height THEN get the height of the composable toolbar`() {
        testContext.resources.configuration.apply {
            screenHeightDp = 479
            screenWidthDp = 599
        }
        val settings = spyk(settings)

        every { settings.toolbarPosition } returns ToolbarPosition.BOTTOM
        every { settings.shouldUseExpandedToolbar } returns true

        assertEquals(64, settings.getBrowserToolbarHeight(testContext))
    }

    @Test
    fun `GIVEN bottom composable toolbar is enabled and navigation bar is enabled but window is too wide WHEN querying the toolbar height THEN get the height of the composable toolbar`() {
        testContext.resources.configuration.apply {
            screenHeightDp = 481
            screenWidthDp = 601
        }
        val settings = spyk(settings)

        every { settings.toolbarPosition } returns ToolbarPosition.BOTTOM
        every { settings.shouldUseExpandedToolbar } returns true

        assertEquals(64, settings.getBrowserToolbarHeight(testContext))
    }

    @Test
    fun `GIVEN the conditions to show a prompt are not met WHEN checking prompt eligibility THEN shouldShowSetAsDefaultPrompt is false`() {
        settings.numberOfSetAsDefaultPromptShownTimes = 0
        settings.lastSetAsDefaultPromptShownTimeInMillis = System.currentTimeMillis()
        settings.coldStartsBetweenSetAsDefaultPrompts = 5
        settings.seventhDayOnboardingCompletedTimestamp = 1L

        assertFalse(settings.shouldShowSetAsDefaultPrompt())
    }

    @Test
    fun `GIVEN the prompt has been shown maximum times WHEN checking prompt eligibility THEN shouldShowSetAsDefaultPrompt is false`() {
        settings.numberOfSetAsDefaultPromptShownTimes =
            3 // Maximum number of times the prompt can be shown based on the design criteria
        settings.lastSetAsDefaultPromptShownTimeInMillis = 0L
        settings.coldStartsBetweenSetAsDefaultPrompts = 5
        settings.seventhDayOnboardingCompletedTimestamp = 1L

        assertFalse(settings.shouldShowSetAsDefaultPrompt())
    }

    @Test
    fun `GIVEN the time since last prompt is too short WHEN checking prompt eligibility THEN shouldShowSetAsDefaultPrompt is false`() {
        settings.numberOfSetAsDefaultPromptShownTimes = 1
        settings.lastSetAsDefaultPromptShownTimeInMillis = System.currentTimeMillis() - 1000
        settings.coldStartsBetweenSetAsDefaultPrompts = 5
        settings.seventhDayOnboardingCompletedTimestamp = 1L

        assertFalse(settings.shouldShowSetAsDefaultPrompt())
    }

    @Test
    fun `GIVEN not enough cold starts WHEN checking prompt eligibility THEN shouldShowSetAsDefaultPrompt is false`() {
        settings.numberOfSetAsDefaultPromptShownTimes = 1
        settings.lastSetAsDefaultPromptShownTimeInMillis = 0L
        settings.coldStartsBetweenSetAsDefaultPrompts = 1
        settings.seventhDayOnboardingCompletedTimestamp = 1L

        assertFalse(settings.shouldShowSetAsDefaultPrompt())
    }

    @Test
    fun `GIVEN enough cold starts and conditions met WHEN checking prompt eligibility THEN shouldShowSetAsDefaultPrompt is true`() {
        settings.numberOfSetAsDefaultPromptShownTimes = 1
        settings.lastSetAsDefaultPromptShownTimeInMillis = 0L
        settings.coldStartsBetweenSetAsDefaultPrompts = 5 // More than required cold starts
        settings.seventhDayOnboardingCompletedTimestamp = 1L

        assertTrue(settings.shouldShowSetAsDefaultPrompt())
    }

    @Test
    fun `GIVEN other conditions are valid WHEN the default browser prompt is disabled THEN shouldShowSetAsDefaultPrompt is false`() {
        settings.numberOfSetAsDefaultPromptShownTimes = 1
        settings.lastSetAsDefaultPromptShownTimeInMillis = 0L
        settings.coldStartsBetweenSetAsDefaultPrompts = 5 // More than required cold starts
        settings.seventhDayOnboardingCompletedTimestamp = 1L

        assertFalse(settings.shouldShowSetAsDefaultPrompt(DefaultBrowserPrompt(enabled = false)))
    }

    @Test
    fun `GIVEN other conditions are valid WHEN the days between prompts is null THEN the value is ignored`() {
        settings.numberOfSetAsDefaultPromptShownTimes = 1
        settings.lastSetAsDefaultPromptShownTimeInMillis = System.currentTimeMillis()
        settings.coldStartsBetweenSetAsDefaultPrompts = 5
        settings.seventhDayOnboardingCompletedTimestamp = 1L

        assertTrue(settings.shouldShowSetAsDefaultPrompt(DefaultBrowserPrompt(daysBetweenPrompts = null)))
    }

    @Test
    fun `GIVEN other conditions are valid WHEN max prompts shown is null THEN the value is ignored`() {
        settings.numberOfSetAsDefaultPromptShownTimes = 10
        settings.lastSetAsDefaultPromptShownTimeInMillis = 0L
        settings.coldStartsBetweenSetAsDefaultPrompts = 5
        settings.seventhDayOnboardingCompletedTimestamp = 1L

        assertTrue(settings.shouldShowSetAsDefaultPrompt(DefaultBrowserPrompt(maxPromptsShown = null)))
    }

    @Test
    fun `GIVEN other conditions are valid WHEN cold starts between prompts is null THEN the value is ignored`() {
        settings.numberOfSetAsDefaultPromptShownTimes = 1
        settings.lastSetAsDefaultPromptShownTimeInMillis = 0L
        settings.coldStartsBetweenSetAsDefaultPrompts = 0
        settings.seventhDayOnboardingCompletedTimestamp = 1L

        assertTrue(settings.shouldShowSetAsDefaultPrompt(DefaultBrowserPrompt(coldStartsBetweenPrompts = null)))
    }

    @Test
    fun `GIVEN other conditions are valid WHEN continuous onboarding is in progress THEN shouldShowSetAsDefaultPrompt is false`() {
        settings.numberOfSetAsDefaultPromptShownTimes = 1
        settings.lastSetAsDefaultPromptShownTimeInMillis = 0L
        settings.coldStartsBetweenSetAsDefaultPrompts = 5
        settings.continuousOnboardingFeatureEnabled = true
        settings.seventhDayOnboardingCompletedTimestamp = -1L

        assertFalse(settings.shouldShowSetAsDefaultPrompt())
    }

    @Test
    fun `GIVEN other conditions are valid WHEN continuous onboarding is disabled THEN shouldShowSetAsDefaultPrompt is true`() {
        settings.numberOfSetAsDefaultPromptShownTimes = 1
        settings.lastSetAsDefaultPromptShownTimeInMillis = 0L
        settings.coldStartsBetweenSetAsDefaultPrompts = 5
        settings.continuousOnboardingFeatureEnabled = false
        settings.seventhDayOnboardingCompletedTimestamp = -1L

        assertTrue(settings.shouldShowSetAsDefaultPrompt())
    }

    @Test
    fun `GIVEN day 7 onboarding is not completed THEN continuousOnboardingCompleted is false`() {
        settings.seventhDayOnboardingCompletedTimestamp = -1L

        assertFalse(settings.continuousOnboardingCompleted)
    }

    @Test
    fun `GIVEN day 7 onboarding is completed THEN continuousOnboardingCompleted is true`() {
        settings.seventhDayOnboardingCompletedTimestamp = 1L

        assertTrue(settings.continuousOnboardingCompleted)
    }

    @Test
    fun `GIVEN previously stored pref_key_last_review_prompt_shown_time value WHEN calling migrateLastReviewPromptTimePrefIfNeeded THEN migrate the value`() {
        val oldKey = "pref_key_last_review_prompt_shown_time"
        val lastReviewPromptTimeInMillis = 300_000L
        val timeNowInMillis = 500_000L
        val eventStore = FakeNimbusEventStore()

        val settings = spyk(settings)
        every { settings.timeNowInMillis() } returns timeNowInMillis

        settings.preferences.edit { putLong(oldKey, lastReviewPromptTimeInMillis) }

        assertEquals(lastReviewPromptTimeInMillis, settings.preferences.getLong(oldKey, 0))
        eventStore.assertNoPastEvents()

        settings.migrateLastReviewPromptTimePrefIfNeeded(eventStore)

        assertFalse(settings.preferences.contains(oldKey))
        eventStore.assertSinglePastEventEquals(
            eventId = "review_prompt_shown",
            secondsAgo = (timeNowInMillis - lastReviewPromptTimeInMillis) / 1000,
        )
    }

    @Test
    fun `GIVEN none previously stored pref_key_last_review_prompt_shown_time value WHEN calling migrateLastReviewPromptTimePrefIfNeeded THEN migration should not happen`() {
        val oldKey = "pref_key_last_review_prompt_shown_time"
        val eventStore = FakeNimbusEventStore()

        assertFalse(settings.preferences.contains(oldKey))
        eventStore.assertNoPastEvents()

        settings.migrateLastReviewPromptTimePrefIfNeeded(eventStore)

        eventStore.assertNoPastEvents()
    }

    @Test
    fun `GIVEN previously stored pref_key_last_review_prompt_shown_time value is a String WHEN calling migrateLastReviewPromptTimePrefIfNeeded THEN crash should not happen`() {
        val oldKey = "pref_key_last_review_prompt_shown_time"
        val eventStore = FakeNimbusEventStore()

        settings.preferences.edit { putString(oldKey, "something unexpected") }
        eventStore.assertNoPastEvents()

        settings.migrateLastReviewPromptTimePrefIfNeeded(eventStore)

        assertFalse(settings.preferences.contains(oldKey))
        eventStore.assertNoPastEvents()
    }

    @Test
    fun `WHEN user has accepted the ToU THEN termsOfUseAcceptedTimeInMillis returns the app installed time`() {
        val installTime = 12345L
        val settings =
            Settings(
                appContext = testContext,
                packageName = "test",
                packageManagerCompatHelper =
                    FakePackageManagerCompatHelper(
                        packageInfo = PackageInfo().apply { firstInstallTime = installTime }
                    ),
            )
        settings.hasAcceptedTermsOfService = true

        val result = settings.termsOfUseAcceptedTimeInMillis

        assertEquals(installTime, result)
    }

    @Test
    fun `WHEN user has not accepted the ToU THEN termsOfUseAcceptedTimeInMillis returns 0L`() {
        val installTime = 12345L
        val settings =
            Settings(
                appContext = testContext,
                packageName = "test",
                packageManagerCompatHelper =
                    FakePackageManagerCompatHelper(
                        packageInfo = PackageInfo().apply { firstInstallTime = installTime }
                    ),
            )
        settings.hasAcceptedTermsOfService = false

        val result = settings.termsOfUseAcceptedTimeInMillis

        assertEquals(0L, result)
    }

    @Test
    fun `WHEN this is a benchmark build and screen capture and screenshots are not allowed in private mode THEN shouldSecureModeBeOverridden is true`() {
        val settings = Settings(appContext = testContext, isBenchmarkBuild = true)
        settings.allowScreenshotsInPrivateMode = false
        settings.allowScreenCaptureInSecureScreens = false

        assertTrue(settings.shouldSecureModeBeOverridden)
    }

    @Test
    fun `WHEN this is not a benchmark build and screen capture and screenshots are not allowed in private mode THEN shouldSecureModeBeOverridden is false`() {
        val settings = Settings(appContext = testContext, isBenchmarkBuild = false)
        settings.allowScreenshotsInPrivateMode = false
        settings.allowScreenCaptureInSecureScreens = false

        assertFalse(settings.shouldSecureModeBeOverridden)
    }

    @Test
    fun `WHEN this is not a benchmark build and screenshots in private mode are allowed and screen capture in private mode is not allowed THEN shouldSecureModeBeOverridden is true`() {
        val settings = Settings(appContext = testContext, isBenchmarkBuild = false)
        settings.allowScreenshotsInPrivateMode = true
        settings.allowScreenCaptureInSecureScreens = false

        assertTrue(settings.shouldSecureModeBeOverridden)
    }

    @Test
    fun `WHEN this is not a benchmark build and screenshots in private mode is not allowed and screen capture in private mode is allowed THEN shouldSecureModeBeOverridden is true`() {
        val settings = Settings(appContext = testContext, isBenchmarkBuild = false)
        settings.allowScreenCaptureInSecureScreens = true
        settings.allowScreenshotsInPrivateMode = false

        assertTrue(settings.shouldSecureModeBeOverridden)
    }

    @Test
    fun `WHEN user has accepted the ToU THEN termsOfUseAcceptedVersion returns the ToU version`() {
        settings.hasAcceptedTermsOfService = true

        val result = settings.termsOfUseAcceptedVersion
        assertEquals(TOU_VERSION, result)
    }

    @Test
    fun `WHEN user has not accepted the ToU THEN termsOfUseAcceptedVersion returns 0`() {
        settings.hasAcceptedTermsOfService = false

        val result = settings.termsOfUseAcceptedVersion
        assertEquals(0, result)
    }

    @Test
    fun `WHEN no preference is stored THEN delete download behavior should be ASK_WHEN_DELETING`() {
        settings.preferences.edit {
            remove("pref_key_downloads_delete_behavior_v2")
        }

        val result = settings.deleteDownloadBehavior
        assertEquals(Settings.DeleteDownloadBehavior.ASK_WHEN_DELETING, result)
    }

    @Test
    fun `WHEN old cleanup file preference is DELETE_FROM_DEVICE THEN delete behavior should be ASK_WHEN_DELETING`() {
        // Bug 2002334 introduced a new key for the download deletion behavior.
        // We want to make sure that the settings is ASK_WHEN_DELETING after the migration from version using the old
        // preference.
        settings.preferences.edit {
            putBoolean("pref_key_downloads_clean_up_files_automatically", true)
        }

        val result = settings.deleteDownloadBehavior
        assertEquals(Settings.DeleteDownloadBehavior.ASK_WHEN_DELETING, result)
    }

    @Test
    fun `WHEN old download delete behavior preference is DELETE_FROM_DEVICE THEN delete behavior should be ASK_WHEN_DELETING`() {
        // Bug 2041355 rotated the preference key for the deletion behavior. The settings should be ASK_WHEN_DELETING if
        // the new key doesn't exist in the preference, no matter what was the value stored with the old key
        settings.preferences.edit {
            putInt("pref_key_downloads_delete_behavior", Settings.DeleteDownloadBehavior.DELETE_FROM_DEVICE.value)
            remove("pref_key_downloads_delete_behavior_v2")
        }

        val result = settings.deleteDownloadBehavior
        assertEquals(Settings.DeleteDownloadBehavior.ASK_WHEN_DELETING, result)
    }

    @Test
    fun `WHEN old download delete behavior preference is REMOVE_FROM_HISTORY THEN delete behavior should be ASK_WHEN_DELETING`() {
        // Bug 2041355 rotated the preference key for the deletion behavior. The settings should be ASK_WHEN_DELETING if
        // the new key doesn't exist in the preference, no matter what was the value stored with the old key
        settings.preferences.edit {
            putInt("pref_key_downloads_delete_behavior", Settings.DeleteDownloadBehavior.REMOVE_FROM_HISTORY.value)
            remove("pref_key_downloads_delete_behavior_v2")
        }

        val result = settings.deleteDownloadBehavior
        assertEquals(Settings.DeleteDownloadBehavior.ASK_WHEN_DELETING, result)
    }

    @Test
    fun `WHEN tab strip is disabled THEN activeSimpleToolbarShortcutKey reads the simple toolbar key`() {
        settings.isTabStripEnabled = false
        settings.toolbarSimpleShortcutKey = ShortcutType.NEW_TAB.value
        settings.toolbarTabStripShortcutKey = ShortcutType.BOOKMARK.value

        assertEquals(ShortcutType.NEW_TAB.value, settings.activeSimpleToolbarShortcutKey)
    }

    @Test
    fun `WHEN tab strip is enabled THEN activeSimpleToolbarShortcutKey reads the tab strip key`() {
        settings.isTabStripEnabled = true
        settings.toolbarSimpleShortcutKey = ShortcutType.NEW_TAB.value
        settings.toolbarTabStripShortcutKey = ShortcutType.BOOKMARK.value

        assertEquals(ShortcutType.BOOKMARK.value, settings.activeSimpleToolbarShortcutKey)
    }

    @Test
    fun `WHEN download deletion behavior preference is read THEN it should read from the new preference`() {
        // Bug 2041355 rotated the preference key for the deletion behavior.
        // We want to make sure we are getting the value from the right preference.
        settings.preferences.edit {
            putInt("pref_key_downloads_delete_behavior", Settings.DeleteDownloadBehavior.DELETE_FROM_DEVICE.value)
            putInt("pref_key_downloads_delete_behavior_v2", Settings.DeleteDownloadBehavior.REMOVE_FROM_HISTORY.value)
        }

        val result = settings.deleteDownloadBehavior
        assertEquals(Settings.DeleteDownloadBehavior.REMOVE_FROM_HISTORY, result)
    }

    @Test
    fun `WHEN recordLastBrowseActivity is called THEN it stores the injected current time`() {
        val fixedTime = 1234L
        val settings = Settings(testContext, currentTimeMillis = { fixedTime })

        settings.recordLastBrowseActivity()

        assertEquals(fixedTime, settings.lastBrowseActivity)
    }

    @Test
    fun `GIVEN a fresh profile WHEN reading the Power Saving Mode preferences THEN both are false`() {
        assertFalse(settings.powerSavingModeAutoEnabled)
        assertFalse(settings.powerSavingModeManuallyEnabled)
    }

    @Test
    fun `GIVEN Power Saving Mode is enabled manually WHEN it is set to follow the OS THEN it is no longer enabled manually`() {
        settings.powerSavingModeManuallyEnabled = true

        settings.powerSavingModeAutoEnabled = true

        assertTrue(settings.powerSavingModeAutoEnabled)
        assertFalse(settings.powerSavingModeManuallyEnabled)
    }

    @Test
    fun `GIVEN Power Saving Mode follows the OS WHEN it is enabled manually THEN it no longer follows the OS`() {
        settings.powerSavingModeAutoEnabled = true

        settings.powerSavingModeManuallyEnabled = true

        assertTrue(settings.powerSavingModeManuallyEnabled)
        assertFalse(settings.powerSavingModeAutoEnabled)
    }

    @Test
    fun `WHEN reading menu customization preferences THEN default values are false`() {
        assertFalse(settings.isMenuCustomizationEnabled)
        assertFalse(settings.isMenuCustomizationScreenEnabled)
    }

    @Test
    fun `GIVEN menu customization preferences WHEN modified THEN reflect stored values`() {
        settings.isMenuCustomizationEnabled = true
        settings.isMenuCustomizationScreenEnabled = true

        assertTrue(settings.isMenuCustomizationEnabled)
        assertTrue(settings.isMenuCustomizationScreenEnabled)

        settings.isMenuCustomizationScreenEnabled = false

        assertFalse(settings.isMenuCustomizationScreenEnabled)
    }
}
