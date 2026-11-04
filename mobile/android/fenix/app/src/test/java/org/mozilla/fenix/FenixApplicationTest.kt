/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix

import android.appwidget.AppWidgetManager
import android.content.ComponentName
import android.content.pm.PackageManager
import android.os.Build
import android.os.UserManager
import androidx.test.core.app.ApplicationProvider
import androidx.work.testing.WorkManagerTestInitHelper
import io.mockk.Runs
import io.mockk.every
import io.mockk.just
import io.mockk.mockk
import io.mockk.spyk
import io.mockk.verify
import kotlin.test.assertNotNull
import kotlinx.coroutines.runBlocking
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.SearchState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.ai.controls.AIFeatureBlock
import mozilla.components.concept.ai.controls.AIFeatureRegistry
import mozilla.components.concept.engine.Engine.HttpsOnlyMode
import mozilla.components.concept.engine.webextension.DisabledFlags
import mozilla.components.concept.engine.webextension.Metadata
import mozilla.components.concept.engine.webextension.WebExtension
import mozilla.components.feature.addons.migration.DefaultSupportedAddonsChecker
import mozilla.components.support.base.android.PowerManagerInfoProvider
import mozilla.components.support.test.robolectric.DefaultBrowserUtils
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.utils.BrowsersCache
import mozilla.components.support.utils.ext.packageManagerWrapper
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.Addons
import org.mozilla.fenix.GleanMetrics.Metrics
import org.mozilla.fenix.GleanMetrics.PowerSavingMode
import org.mozilla.fenix.GleanMetrics.Preferences
import org.mozilla.fenix.GleanMetrics.SearchDefaultEngine
import org.mozilla.fenix.GleanMetrics.TabStrip
import org.mozilla.fenix.GleanMetrics.TopSites
import org.mozilla.fenix.components.fake.FakeMetricController
import org.mozilla.fenix.components.metrics.MozillaProductDetector
import org.mozilla.fenix.components.toolbar.ToolbarPosition
import org.mozilla.fenix.distributions.DefaultDistributionBrowserStoreProvider
import org.mozilla.fenix.distributions.DistributionIdManager
import org.mozilla.fenix.distributions.DistributionProviderChecker
import org.mozilla.fenix.distributions.DistributionSettings
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.mozilla.fenix.settings.ShortcutType
import org.mozilla.fenix.settings.doh.DohSettingsProvider
import org.mozilla.fenix.settings.doh.ProtectionLevel
import org.mozilla.fenix.utils.Settings
import org.mozilla.gecko.search.SearchWidgetProvider
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
class FenixApplicationTest {

    @get:Rule val gleanTestRule = FenixGleanTestRule(ApplicationProvider.getApplicationContext())

    private lateinit var application: FenixApplication
    private lateinit var browsersCache: BrowsersCache
    private lateinit var mozillaProductDetector: MozillaProductDetector
    private lateinit var browserStore: BrowserStore

    private val testDistributionProviderChecker =
        object : DistributionProviderChecker {
            override suspend fun queryProvider(): String? = null
        }

    private val testDistributionSettings =
        object : DistributionSettings {
            override fun getDistributionId(): String = ""

            override fun saveDistributionId(id: String) = Unit

            override fun setMarketingTelemetryPreferences() = Unit
        }

    @Before
    fun setUp() {
        application = ApplicationProvider.getApplicationContext()
        browsersCache = mockk(relaxed = true)
        mozillaProductDetector = mockk(relaxed = true)
        browserStore = BrowserStore()

        every { testContext.components.core } returns mockk(relaxed = true)
        every { testContext.components.nimbus } returns mockk(relaxed = true)
        every { testContext.components.aiControlsFeatureBlock } returns AIFeatureBlock.inMemory()
        every { testContext.components.aiFeatureRegistry } returns AIFeatureRegistry.inMemory()
        every { testContext.components.distributionIdManager } returns
            DistributionIdManager(
                packageManager = testContext.packageManagerWrapper,
                browserStoreProvider = DefaultDistributionBrowserStoreProvider(browserStore),
                distributionProviderChecker = testDistributionProviderChecker,
                distributionSettings = testDistributionSettings,
                metricController = FakeMetricController(),
            )
    }

    @Test
    fun `GIVEN there are unsupported addons installed WHEN subscribing for new add-ons checks THEN register for checks`() {
        val checker = mockk<DefaultSupportedAddonsChecker>(relaxed = true)
        val unSupportedExtension: WebExtension = mockk()
        val metadata: Metadata = mockk()

        every { unSupportedExtension.getMetadata() } returns metadata
        every { metadata.disabledFlags } returns DisabledFlags.select(DisabledFlags.APP_SUPPORT)

        application.subscribeForNewAddonsIfNeeded(checker, listOf(unSupportedExtension))

        verify { checker.registerForChecks() }
    }

    @Test
    fun `GIVEN there are no unsupported addons installed WHEN subscribing for new add-ons checks THEN unregister for checks`() {
        val checker = mockk<DefaultSupportedAddonsChecker>(relaxed = true)
        val unSupportedExtension: WebExtension = mockk()
        val metadata: Metadata = mockk()

        every { unSupportedExtension.getMetadata() } returns metadata
        every { metadata.disabledFlags } returns DisabledFlags.select(DisabledFlags.USER)

        application.subscribeForNewAddonsIfNeeded(checker, listOf(unSupportedExtension))

        verify { checker.unregisterForChecks() }
    }

    @Test
    @Config(sdk = [Build.VERSION_CODES.O])
    fun `WHEN setStartupMetrics is called THEN sets some base metrics`() = runBlocking {
        val expectedAppName = "org.mozilla.fenix"
        val expectedAppInstallSource = "org.mozilla.install.source"
        val settings = spyk(Settings(testContext))
        val dohSettingsProvider = mockk<DohSettingsProvider>()
        val powerManagerInfoProvider = mockk<PowerManagerInfoProvider>()
        val application = spyk(application)
        val packageManager: PackageManager = mockk()

        every { application.packageManager } returns packageManager
        @Suppress("DEPRECATION")
        every { packageManager.getInstallerPackageName(any()) } returns expectedAppInstallSource
        DefaultBrowserUtils.setAsDefaultBrowser(testContext.packageName)
        every { mozillaProductDetector.getMozillaBrowserDefault(any()) } returns expectedAppName
        every { mozillaProductDetector.getInstalledMozillaProducts(any()) } returns listOf(expectedAppName)
        every { settings.adjustCampaignId } returns "ID"
        every { settings.adjustAdGroup } returns "group"
        every { settings.adjustCreative } returns "creative"
        every { settings.adjustNetwork } returns "network"
        // Testing [settings.migrateSearchWidgetInstalledPrefIfNeeded]
        settings.preferences.edit().putInt("pref_key_search_widget_installed", 5).apply()
        shadowOf(AppWidgetManager.getInstance(testContext))
            .bindAppWidgetId(1, ComponentName(testContext, SearchWidgetProvider::class.java))
        settings.preferences.edit().putString("pref_key_current_wallpaper", "default").apply()
        every { settings.openTabsCount } returns 1
        every { settings.topSitesSize } returns 2
        every { settings.installedAddonsCount } returns 3
        every { settings.installedAddonsList } returns "test1,test2,test3"
        every { settings.enabledAddonsCount } returns 2
        every { settings.enabledAddonsList } returns "test1,test2"
        every { settings.desktopBookmarksSize } returns 4
        every { settings.mobileBookmarksSize } returns 5
        every { settings.toolbarPosition } returns ToolbarPosition.BOTTOM
        every { settings.shouldUseExpandedToolbar } returns true
        every { settings.toolbarSimpleShortcutKey } returns ShortcutType.SHARE.value
        every { settings.toolbarExpandedShortcutKey } returns ShortcutType.HOMEPAGE.value
        every { settings.toolbarTabStripShortcutKey } returns ShortcutType.BOOKMARK.value
        every { settings.getTabViewPingString() } returns "test"
        every { settings.getTabTimeoutPingString() } returns "test"
        every { settings.shouldShowSearchSuggestions } returns true
        every { settings.shouldUseTrackingProtection } returns true
        every { settings.isRemoteDebuggingEnabled } returns true
        every { settings.isTelemetryEnabled } returns true
        every { settings.isExperimentationEnabled } returns true
        every { settings.shouldShowHistorySuggestions } returns true
        every { settings.shouldShowBookmarkSuggestions } returns true
        every { settings.shouldShowClipboardSuggestions } returns true
        every { settings.openLinksInAPrivateTab } returns true
        every { settings.shouldShowSearchSuggestionsInPrivate } returns true
        every { settings.shouldShowVoiceSearch } returns true
        every { settings.openLinksInExternalApp } returns "never"
        every { settings.shouldUseFixedTopToolbar } returns true
        every { settings.useStandardTrackingProtection } returns true
        every { settings.switchServiceIsEnabled } returns true
        every { settings.touchExplorationIsEnabled } returns true
        every { settings.shouldUseLightTheme } returns true
        every { settings.signedInFxaAccount } returns true
        every { settings.showRecentTabsFeature } returns true
        every { settings.showBookmarksHomeFeature } returns false
        every { settings.showTopSitesFeature } returns true
        every { settings.historyMetadataUIFeature } returns false
        every { settings.showPocketRecommendationsFeature } returns true
        every { settings.showContileFeature } returns true
        every { application.reportHomeScreenMetrics(settings) } just Runs
        every { application.deviceTotalRAM } returns 7L
        every { settings.inactiveTabsAreEnabled } returns true
        every { settings.isIsolatedProcessEnabled } returns true
        every { settings.isAppZygoteEnabled } returns true
        every { application.isDeviceRamAboveThreshold } returns true
        every { powerManagerInfoProvider.isPowerSaveMode() } returns true
        every { dohSettingsProvider.getSelectedProtectionLevel() } returns ProtectionLevel.Max
        every { settings.getHttpsOnlyMode() } returns HttpsOnlyMode.ENABLED_PRIVATE_ONLY
        every { settings.shouldEnableGlobalPrivacyControl } returns true
        every { settings.isTabStripEnabled } returns true

        assertTrue(settings.contileContextId.isNotEmpty())
        assertNotNull(TopSites.contextId.testGetValue())
        assertEquals(TopSites.contextId.testGetValue()!!.toString(), settings.contileContextId)

        application.setStartupMetrics(
            browserStore = browserStore,
            settings = settings,
            dohSettingsProvider,
            mozillaProductDetector = mozillaProductDetector,
            powerManagerInfoProvider = powerManagerInfoProvider,
            isAutomotiveDevice = true,
        )

        // Verify that browser defaults metrics are set.
        assertEquals("Mozilla", Metrics.distributionId.testGetValue())
        assertEquals(true, Metrics.defaultBrowser.testGetValue())
        assertEquals(expectedAppName, Metrics.defaultMozBrowser.testGetValue())
        assertEquals(listOf(expectedAppName), Metrics.mozillaProducts.testGetValue())
        assertEquals("ID", Metrics.adjustCampaign.testGetValue())
        assertEquals("group", Metrics.adjustAdGroup.testGetValue())
        assertEquals("creative", Metrics.adjustCreative.testGetValue())
        assertEquals("network", Metrics.adjustNetwork.testGetValue())
        assertEquals(true, Metrics.searchWidgetInstalled.testGetValue())
        assertEquals(true, Metrics.hasOpenTabs.testGetValue())
        assertEquals(1, Metrics.tabsOpenCount.testGetValue())
        assertEquals(true, Metrics.hasTopSites.testGetValue())
        assertEquals(2, Metrics.topSitesCount.testGetValue())
        assertEquals(true, Addons.hasInstalledAddons.testGetValue())
        assertEquals(listOf("test1", "test2", "test3"), Addons.installedAddons.testGetValue())
        assertEquals(true, Addons.hasEnabledAddons.testGetValue())
        assertEquals(listOf("test1", "test2"), Addons.enabledAddons.testGetValue())
        assertEquals(true, Preferences.searchSuggestionsEnabled.testGetValue())
        assertEquals(true, Preferences.showSponsorSuggestionsEnabled.testGetValue())
        assertEquals(true, Preferences.showNonSponsorSuggestionsEnabled.testGetValue())
        assertEquals(true, Preferences.remoteDebuggingEnabled.testGetValue())
        assertEquals(true, Preferences.telemetryEnabled.testGetValue())
        assertEquals(true, Preferences.studiesEnabled.testGetValue())
        assertEquals(true, Preferences.browsingHistorySuggestion.testGetValue())
        assertEquals(true, Preferences.bookmarksSuggestion.testGetValue())
        assertEquals(true, Preferences.clipboardSuggestionsEnabled.testGetValue())
        assertEquals(true, Preferences.voiceSearchEnabled.testGetValue())
        assertEquals("never", Preferences.openLinksInAppEnabled.testGetValue())
        assertEquals(true, Preferences.signedInSync.testGetValue())
        assertEquals(emptyList<String>(), Preferences.syncItems.testGetValue())
        assertEquals("fixed_top", Preferences.toolbarPositionSetting.testGetValue())
        assertEquals("expanded", Preferences.toolbarModeSetting.testGetValue())
        assertEquals(ShortcutType.SHARE.value, Preferences.toolbarSimpleShortcut.testGetValue())
        assertEquals(ShortcutType.HOMEPAGE.value, Preferences.toolbarExpandedShortcut.testGetValue())
        assertEquals(ShortcutType.BOOKMARK.value, Preferences.toolbarTabStripShortcut.testGetValue())
        assertEquals("standard", Preferences.enhancedTrackingProtection.testGetValue())
        assertEquals(listOf("switch", "touch exploration"), Preferences.accessibilityServices.testGetValue())
        assertEquals(true, Preferences.inactiveTabsEnabled.testGetValue())
        assertEquals(true, Preferences.isolatedContentProcessesEnabled.testGetValue())
        assertEquals(true, Preferences.appZygoteIsolatedContentProcessesEnabled.testGetValue())
        assertEquals(true, Metrics.defaultWallpaper.testGetValue())
        assertEquals(true, Metrics.ramMoreThanThreshold.testGetValue())
        assertEquals(true, PowerSavingMode.activeAtStartup.testGetValue())
        assertEquals(7L, Metrics.deviceTotalRam.testGetValue())
        assertEquals(true, Metrics.isAndroidAutomotive.testGetValue())
        assertEquals("Max", Preferences.dohProtectionLevel.testGetValue())
        assertEquals("ENABLED_PRIVATE_ONLY", Preferences.httpsOnlyMode.testGetValue())
        assertEquals(true, Preferences.globalPrivacyControlEnabled.testGetValue())
        assertEquals(true, TabStrip.enabled.testGetValue())

        val contextId = TopSites.contextId.testGetValue()

        assertNotNull(contextId)
        assertEquals(contextId.toString(), settings.contileContextId)

        // Verify that search engine defaults are NOT set. This test does
        // not mock most of the objects telemetry is collected from.
        assertNull(SearchDefaultEngine.code.testGetValue())
        assertNull(SearchDefaultEngine.name.testGetValue())
        assertNull(SearchDefaultEngine.searchUrl.testGetValue())

        application.setStartupMetrics(
            browserStore = browserStore,
            settings = settings,
            mozillaProductDetector = mozillaProductDetector,
        )

        assertEquals(contextId, TopSites.contextId.testGetValue())
        assertEquals(contextId.toString(), settings.contileContextId)
    }

    @Test
    @Config(sdk = [28])
    fun `GIVEN the current etp mode is custom WHEN tracking the etp metric THEN track also the cookies option on SDK 28`() =
        runBlocking {
            val settings: Settings =
                mockk(relaxed = true) {
                    every { shouldUseTrackingProtection } returns true
                    every { useCustomTrackingProtection } returns true
                    every { blockCookiesSelectionInCustomTrackingProtection } returns "Test"
                }

            application.setStartupMetrics(
                browserStore = browserStore,
                settings = settings,
                mozillaProductDetector = mozillaProductDetector,
            )

            assertEquals("Test", Preferences.etpCustomCookiesSelection.testGetValue())
        }

    @Test
    fun `GIVEN the current etp mode is custom WHEN tracking the etp metric THEN track also the cookies option`() =
        runBlocking {
            val settings: Settings =
                mockk(relaxed = true) {
                    every { shouldUseTrackingProtection } returns true
                    every { useCustomTrackingProtection } returns true
                    every { blockCookiesSelectionInCustomTrackingProtection } returns "Test"
                }

            val packageManager: PackageManager = testContext.packageManager
            shadowOf(packageManager)
                .setInstallSourceInfo(testContext.packageName, "initiating.package", "installing.package")

            application.setStartupMetrics(
                browserStore = browserStore,
                settings = settings,
                mozillaProductDetector = mozillaProductDetector,
            )

            assertEquals("Test", Preferences.etpCustomCookiesSelection.testGetValue())
        }

    @Test
    fun `WHEN the search configuration is updated in remote settings THEN set a new search configuration being available`() {
        val browserStore = BrowserStore(BrowserState(search = SearchState(isNewSearchConfigurationAvailable = false)))

        application.setupRefreshingSearchEngines(listOf("search-config-v2"), browserStore)

        assertTrue(browserStore.state.search.isNewSearchConfigurationAvailable)
    }

    @Test
    fun `WHEN the search configuration overrides are updated in remote settings THEN set a new search configuration being available`() {
        val browserStore = BrowserStore(BrowserState(search = SearchState(isNewSearchConfigurationAvailable = false)))

        application.setupRefreshingSearchEngines(listOf("search-config-overrides-v2"), browserStore)

        assertTrue(browserStore.state.search.isNewSearchConfigurationAvailable)
    }

    @Test
    fun `WHEN the search engine icons are updated in remote settings THEN set a new search configuration being available`() {
        val browserStore = BrowserStore(BrowserState(search = SearchState(isNewSearchConfigurationAvailable = false)))

        application.setupRefreshingSearchEngines(listOf("search-config-icons"), browserStore)

        assertTrue(browserStore.state.search.isNewSearchConfigurationAvailable)
    }

    @Test
    fun `GIVEN the user is locked WHEN WorkManager initializes THEN it does not throw`() {
        shadowOf(testContext.getSystemService(UserManager::class.java)).setUserUnlocked(false)
        WorkManagerTestInitHelper.closeWorkDatabase()

        WorkManagerTestInitHelper.initializeTestWorkManager(testContext, application.workManagerConfiguration)
    }

    @Test
    fun `GIVEN the user is unlocked WHEN WorkManager fails to initialize THEN the error is rethrown`() {
        val handler = application.workManagerConfiguration.initializationExceptionHandler!!

        assertThrows(IllegalStateException::class.java) { handler.accept(IllegalStateException()) }
    }
}
