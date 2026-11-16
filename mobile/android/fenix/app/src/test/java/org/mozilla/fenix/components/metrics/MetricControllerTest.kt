/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.metrics

import io.mockk.MockKAnnotations
import io.mockk.every
import io.mockk.impl.annotations.MockK
import io.mockk.mockk
import io.mockk.verify
import io.mockk.verifyAll
import kotlin.test.assertNotNull
import mozilla.components.browser.thumbnails.facts.BrowserThumbnailsFacts
import mozilla.components.compose.browser.awesomebar.AwesomeBarFacts as ComposeAwesomeBarFacts
import mozilla.components.feature.autofill.facts.AutofillFacts
import mozilla.components.feature.awesomebar.facts.AwesomeBarFacts
import mozilla.components.feature.awesomebar.facts.SuggestionCardType
import mozilla.components.feature.awesomebar.provider.BookmarksStorageSuggestionProvider
import mozilla.components.feature.awesomebar.provider.ClipboardSuggestionProvider
import mozilla.components.feature.awesomebar.provider.HistoryStorageSuggestionProvider
import mozilla.components.feature.awesomebar.provider.SearchSuggestionProvider
import mozilla.components.feature.awesomebar.provider.SessionSuggestionProvider
import mozilla.components.feature.contextmenu.facts.ContextMenuFacts
import mozilla.components.feature.downloads.facts.ShareResourceFacts
import mozilla.components.feature.media.facts.MediaFacts
import mozilla.components.feature.prompts.dialog.GeneratedPasswordFacts
import mozilla.components.feature.prompts.dialog.LoginDialogFacts
import mozilla.components.feature.prompts.facts.CreditCardAutofillDialogFacts
import mozilla.components.feature.protection.dashboard.TrackerCategory
import mozilla.components.feature.protection.dashboard.facts.ProtectionDashboardFacts
import mozilla.components.feature.pwa.ProgressiveWebAppFacts
import mozilla.components.feature.search.telemetry.ads.AdsTelemetry
import mozilla.components.feature.search.telemetry.incontent.InContentTelemetry
import mozilla.components.feature.sitepermissions.SitePermissionsFacts
import mozilla.components.feature.syncedtabs.facts.SyncedTabsFacts
import mozilla.components.feature.top.sites.facts.TopSitesFacts
import mozilla.components.support.base.Component
import mozilla.components.support.base.facts.Action
import mozilla.components.support.base.facts.Fact
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.webextensions.facts.WebExtensionFacts
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.AndroidAutofill
import org.mozilla.fenix.GleanMetrics.Awesomebar
import org.mozilla.fenix.GleanMetrics.BrowserSearch
import org.mozilla.fenix.GleanMetrics.BrowserThumbnails
import org.mozilla.fenix.GleanMetrics.ContextMenu
import org.mozilla.fenix.GleanMetrics.ContextualMenu
import org.mozilla.fenix.GleanMetrics.CreditCards
import org.mozilla.fenix.GleanMetrics.GeneratedPasswordDialog
import org.mozilla.fenix.GleanMetrics.LoginDialog
import org.mozilla.fenix.GleanMetrics.MediaNotification
import org.mozilla.fenix.GleanMetrics.NativeShareSheet
import org.mozilla.fenix.GleanMetrics.PerfAwesomebar
import org.mozilla.fenix.GleanMetrics.ProgressiveWebApp
import org.mozilla.fenix.GleanMetrics.ShareSheet
import org.mozilla.fenix.GleanMetrics.SitePermissions
import org.mozilla.fenix.GleanMetrics.SyncedTabs
import org.mozilla.fenix.GleanMetrics.TrackingProtection
import org.mozilla.fenix.browser.TabGroupContextMenuCandidate
import org.mozilla.fenix.components.metrics.ReleaseMetricController.Companion
import org.mozilla.fenix.components.metrics.ReleaseMetricController.Companion.SHARE_LINK_CONTEXT_MENU_ITEM_ID
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.mozilla.fenix.utils.Settings
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class MetricControllerTest {

    @get:Rule val gleanTestRule = FenixGleanTestRule(testContext)

    @MockK(relaxUnitFun = true) private lateinit var dataService1: MetricsService

    @MockK(relaxUnitFun = true) private lateinit var dataService2: MetricsService

    @MockK(relaxUnitFun = true) private lateinit var marketingService1: MetricsService

    @MockK(relaxUnitFun = true) private lateinit var marketingService2: MetricsService

    @MockK(relaxUnitFun = true) private lateinit var usageReportingService: MetricsService

    @Before
    fun setup() {
        MockKAnnotations.init(this)

        every { dataService1.type } returns MetricServiceType.Data
        every { dataService2.type } returns MetricServiceType.Data
        every { marketingService1.type } returns MetricServiceType.Marketing
        every { marketingService2.type } returns MetricServiceType.Marketing
        every { usageReportingService.type } returns MetricServiceType.UsageReporting
    }

    @Test
    fun `debug metric controller emits logs`() {
        val logger = mockk<Logger>(relaxed = true)
        val controller = DebugMetricController(logger)

        controller.start(MetricServiceType.Data)
        verify { logger.debug("DebugMetricController: start") }

        controller.stop(MetricServiceType.Data)
        verify { logger.debug("DebugMetricController: stop") }
    }

    @Test
    fun `release metric controller starts and stops all data services`() {
        var enabled = true
        val controller =
            createReleaseMetricController(
                services =
                    listOf(dataService1, marketingService1, dataService2, marketingService2, usageReportingService),
                isDataTelemetryEnabled = { enabled },
                isMarketingDataTelemetryEnabled = { enabled },
                isUsageTelemetryEnabled = { enabled },
            )

        controller.start(MetricServiceType.Data)
        verify { dataService1.start() }
        verify { dataService2.start() }

        enabled = false

        controller.stop(MetricServiceType.Data)
        verify { dataService1.stop() }
        verify { dataService2.stop() }

        verifyAll(inverse = true) {
            marketingService1.start()
            marketingService1.stop()
            marketingService2.start()
            marketingService2.stop()
            usageReportingService.start()
            usageReportingService.stop()
        }
    }

    @Test
    fun `release metric controller starts data service only if enabled`() {
        val controller =
            createReleaseMetricController(
                services = listOf(dataService1),
                isDataTelemetryEnabled = { false },
            )

        controller.start(MetricServiceType.Data)
        verify(inverse = true) { dataService1.start() }

        controller.stop(MetricServiceType.Data)
        verify(inverse = true) { dataService1.stop() }
    }

    @Test
    fun `release metric controller doesn't start usage reporting service if disabled`() {
        val controller =
            createReleaseMetricController(
                services = listOf(usageReportingService),
                isUsageTelemetryEnabled = { false },
            )
        controller.start(MetricServiceType.UsageReporting)
        verify(inverse = true) { usageReportingService.start() }
    }

    @Test
    fun `release metric controller starts usage reporting service if enabled`() {
        val controller = createReleaseMetricController(services = listOf(usageReportingService))
        controller.start(MetricServiceType.UsageReporting)
        verify { usageReportingService.start() }
    }

    @Test
    fun `release metric controller starts usage reporting service if data service disabled`() {
        val controller =
            createReleaseMetricController(
                services = listOf(dataService1, usageReportingService),
                isDataTelemetryEnabled = { false },
            )
        controller.start(MetricServiceType.UsageReporting)
        verify { usageReportingService.start() }
    }

    @Test
    fun `release metric controller doesn't stop usage reporting service if enabled`() {
        val controller = createReleaseMetricController(services = listOf(dataService1, usageReportingService))
        controller.start(MetricServiceType.UsageReporting)
        controller.stop(MetricServiceType.UsageReporting)
        verify(inverse = true) { usageReportingService.stop() }
    }

    @Test
    fun `release metric controller stops usage reporting service if disabled`() {
        var enabled = true
        val controller =
            createReleaseMetricController(
                services = listOf(dataService1, usageReportingService),
                isUsageTelemetryEnabled = { enabled },
            )
        controller.start(MetricServiceType.UsageReporting)
        enabled = false
        controller.stop(MetricServiceType.UsageReporting)
        verify { usageReportingService.stop() }
    }

    @Test
    fun `release metric controller starts service only once`() {
        var enabled = true
        val controller =
            createReleaseMetricController(
                services = listOf(dataService1),
                isDataTelemetryEnabled = { enabled },
            )

        controller.start(MetricServiceType.Data)
        controller.start(MetricServiceType.Data)
        verify(exactly = 1) { dataService1.start() }

        enabled = false

        controller.stop(MetricServiceType.Data)
        controller.stop(MetricServiceType.Data)
        verify(exactly = 1) { dataService1.stop() }
    }

    @Test
    fun `WHEN AwesomeBar duration fact is processed THEN the correct metric is recorded`() {
        val controller = createReleaseMetricController()
        val action = mockk<Action>()
        val duration = 1000L
        var metadata =
            mapOf<String, Pair<*, Long>>(
                ComposeAwesomeBarFacts.MetadataKeys.DURATION_PAIR to
                    Pair(
                        mockk<HistoryStorageSuggestionProvider>(),
                        duration,
                    )
            )
        var fact =
            Fact(
                Component.COMPOSE_AWESOMEBAR,
                action,
                ComposeAwesomeBarFacts.Items.PROVIDER_DURATION,
                metadata = metadata,
            )
        // Verify history based suggestions
        assertNull(PerfAwesomebar.historySuggestions.testGetValue())

        with(controller) {
            fact.process()
        }

        assertNotNull(PerfAwesomebar.historySuggestions.testGetValue())

        // Verify bookmark based suggestions
        metadata =
            mapOf(
                ComposeAwesomeBarFacts.MetadataKeys.DURATION_PAIR to
                    Pair(
                        mockk<BookmarksStorageSuggestionProvider>(),
                        duration,
                    )
            )
        fact = fact.copy(metadata = metadata)
        assertNull(PerfAwesomebar.bookmarkSuggestions.testGetValue())

        with(controller) {
            fact.process()
        }

        assertNotNull(PerfAwesomebar.bookmarkSuggestions.testGetValue())

        // Verify session based suggestions
        metadata =
            mapOf(
                ComposeAwesomeBarFacts.MetadataKeys.DURATION_PAIR to
                    Pair(
                        mockk<SessionSuggestionProvider>(),
                        duration,
                    )
            )
        fact = fact.copy(metadata = metadata)
        assertNull(PerfAwesomebar.sessionSuggestions.testGetValue())

        with(controller) {
            fact.process()
        }

        assertNotNull(PerfAwesomebar.sessionSuggestions.testGetValue())

        // Verify search engine suggestions
        metadata =
            mapOf(
                ComposeAwesomeBarFacts.MetadataKeys.DURATION_PAIR to
                    Pair(
                        mockk<SearchSuggestionProvider>(),
                        duration,
                    )
            )
        fact = fact.copy(metadata = metadata)
        assertNull(PerfAwesomebar.searchEngineSuggestions.testGetValue())

        with(controller) {
            fact.process()
        }

        assertNotNull(PerfAwesomebar.searchEngineSuggestions.testGetValue())

        // Verify clipboard based suggestions
        metadata =
            mapOf(
                ComposeAwesomeBarFacts.MetadataKeys.DURATION_PAIR to
                    Pair(
                        mockk<ClipboardSuggestionProvider>(),
                        duration,
                    )
            )
        fact = fact.copy(metadata = metadata)
        assertNull(PerfAwesomebar.clipboardSuggestions.testGetValue())

        with(controller) {
            fact.process()
        }

        assertNotNull(PerfAwesomebar.clipboardSuggestions.testGetValue())
    }

    @Test
    fun `release metric controller starts and stops all marketing services`() {
        var enabled = true
        val controller =
            createReleaseMetricController(
                services = listOf(dataService1, marketingService1, dataService2, marketingService2),
                isDataTelemetryEnabled = { enabled },
                isMarketingDataTelemetryEnabled = { enabled },
                isUsageTelemetryEnabled = { enabled },
            )

        controller.start(MetricServiceType.Marketing)
        verify { marketingService1.start() }
        verify { marketingService2.start() }

        enabled = false

        controller.stop(MetricServiceType.Marketing)
        verify { marketingService1.stop() }
        verify { marketingService2.stop() }

        verifyAll(inverse = true) {
            dataService1.start()
            dataService1.stop()
            dataService2.start()
            dataService2.stop()
        }
    }

    @Test
    fun `topsites fact should set value in SharedPreference`() {
        val enabled = true
        val settings: Settings = mockk(relaxed = true)
        val controller =
            createReleaseMetricController(
                services = listOf(dataService1),
                isDataTelemetryEnabled = { enabled },
                isMarketingDataTelemetryEnabled = { enabled },
                isUsageTelemetryEnabled = { enabled },
                settings = settings,
            )

        val fact =
            Fact(
                Component.FEATURE_TOP_SITES,
                Action.INTERACTION,
                TopSitesFacts.Items.COUNT,
                "1",
            )

        verify(exactly = 0) { settings.topSitesSize = any() }
        with(controller) {
            fact.process()
        }
        verify(exactly = 1) { settings.topSitesSize = any() }
    }

    @Test
    fun `web extension fact should set value in SharedPreference`() {
        val enabled = true
        val settings = Settings(testContext)
        val controller =
            createReleaseMetricController(
                services = listOf(dataService1),
                isDataTelemetryEnabled = { enabled },
                isMarketingDataTelemetryEnabled = { enabled },
                isUsageTelemetryEnabled = { enabled },
                settings = settings,
            )
        val fact =
            Fact(
                Component.SUPPORT_WEBEXTENSIONS,
                Action.INTERACTION,
                WebExtensionFacts.Items.WEB_EXTENSIONS_INITIALIZED,
                metadata =
                    mapOf(
                        "installed" to listOf("test1", "test2", "test3", "test4"),
                        "enabled" to listOf("test2", "test4"),
                    ),
            )

        assertEquals(settings.installedAddonsCount, 0)
        assertEquals(settings.installedAddonsList, "")
        assertEquals(settings.enabledAddonsCount, 0)
        assertEquals(settings.enabledAddonsList, "")
        with(controller) {
            fact.process()
        }
        assertEquals(settings.installedAddonsCount, 4)
        assertEquals(settings.installedAddonsList, "test1,test2,test3,test4")
        assertEquals(settings.enabledAddonsCount, 2)
        assertEquals(settings.enabledAddonsList, "test2,test4")
    }

    @Test
    fun `WHEN processing a fact with FEATURE_PROMPTS component THEN the right metric is recorded with no extras`() {
        val controller = createReleaseMetricController()
        val action = mockk<Action>()

        // Verify display interaction
        assertNull(LoginDialog.displayed.testGetValue())
        var fact = Fact(Component.FEATURE_PROMPTS, action, LoginDialogFacts.Items.DISPLAY)

        controller.run {
            fact.process()
        }

        assertNotNull(LoginDialog.displayed.testGetValue())
        assertEquals(1, LoginDialog.displayed.testGetValue()!!.size)
        assertNull(LoginDialog.displayed.testGetValue()!!.single().extra)

        // Verify cancel interaction
        assertNull(LoginDialog.cancelled.testGetValue())
        fact = Fact(Component.FEATURE_PROMPTS, action, LoginDialogFacts.Items.CANCEL)

        controller.run {
            fact.process()
        }

        assertNotNull(LoginDialog.cancelled.testGetValue())
        assertEquals(1, LoginDialog.cancelled.testGetValue()!!.size)
        assertNull(LoginDialog.cancelled.testGetValue()!!.single().extra)

        // Verify never save interaction
        assertNull(LoginDialog.neverSave.testGetValue())
        fact = Fact(Component.FEATURE_PROMPTS, action, LoginDialogFacts.Items.NEVER_SAVE)

        controller.run {
            fact.process()
        }

        assertNotNull(LoginDialog.neverSave.testGetValue())
        assertEquals(1, LoginDialog.neverSave.testGetValue()!!.size)
        assertNull(LoginDialog.neverSave.testGetValue()!!.single().extra)

        // Verify save interaction
        assertNull(LoginDialog.saved.testGetValue())
        fact = Fact(Component.FEATURE_PROMPTS, action, LoginDialogFacts.Items.SAVE)

        controller.run {
            fact.process()
        }

        assertNotNull(LoginDialog.saved.testGetValue())
        assertEquals(1, LoginDialog.saved.testGetValue()!!.size)
        assertNull(LoginDialog.saved.testGetValue()!!.single().extra)

        // Verify generated password shown interaction
        assertNull(GeneratedPasswordDialog.shown.testGetValue())
        fact = Fact(Component.FEATURE_PROMPTS, action, GeneratedPasswordFacts.Items.SHOWN)

        controller.run {
            fact.process()
        }

        assertNotNull(GeneratedPasswordDialog.shown.testGetValue())
        assertEquals(1, GeneratedPasswordDialog.shown.testGetValue()!!.size)
        assertNull(GeneratedPasswordDialog.shown.testGetValue()!!.single().extra)

        // Verify generated password filled interaction
        assertNull(GeneratedPasswordDialog.filled.testGetValue())
        fact = Fact(Component.FEATURE_PROMPTS, action, GeneratedPasswordFacts.Items.FILLED)

        controller.run {
            fact.process()
        }

        assertNotNull(GeneratedPasswordDialog.filled.testGetValue())
        assertEquals(1, GeneratedPasswordDialog.filled.testGetValue()!!.size)
        assertNull(GeneratedPasswordDialog.filled.testGetValue()!!.single().extra)
    }

    @Test
    fun `WHEN processing a FEATURE_MEDIA NOTIFICATION fact THEN the right metric is recorded`() {
        val controller = createReleaseMetricController()
        // Verify the play action
        var fact = Fact(Component.FEATURE_MEDIA, Action.PLAY, MediaFacts.Items.NOTIFICATION)
        assertNull(MediaNotification.play.testGetValue())

        controller.run {
            fact.process()
        }

        assertNotNull(MediaNotification.play.testGetValue())
        assertEquals(1, MediaNotification.play.testGetValue()!!.size)
        assertNull(MediaNotification.play.testGetValue()!!.single().extra)

        // Verify the pause action
        fact = Fact(Component.FEATURE_MEDIA, Action.PAUSE, MediaFacts.Items.NOTIFICATION)
        assertNull(MediaNotification.pause.testGetValue())

        controller.run {
            fact.process()
        }

        assertNotNull(MediaNotification.pause.testGetValue())
        assertEquals(1, MediaNotification.pause.testGetValue()!!.size)
        assertNull(MediaNotification.pause.testGetValue()!!.single().extra)

        // Verify the next action
        fact = Fact(Component.FEATURE_MEDIA, Action.NEXT, MediaFacts.Items.NOTIFICATION)
        assertNull(MediaNotification.next.testGetValue())

        controller.run {
            fact.process()
        }

        assertNotNull(MediaNotification.next.testGetValue())
        assertEquals(1, MediaNotification.next.testGetValue()!!.size)
        assertNull(MediaNotification.next.testGetValue()!!.single().extra)

        // Verify the previous action
        fact = Fact(Component.FEATURE_MEDIA, Action.PREVIOUS, MediaFacts.Items.NOTIFICATION)
        assertNull(MediaNotification.previous.testGetValue())

        controller.run {
            fact.process()
        }

        assertNotNull(MediaNotification.previous.testGetValue())
        assertEquals(1, MediaNotification.previous.testGetValue()!!.size)
        assertNull(MediaNotification.previous.testGetValue()!!.single().extra)
    }

    @Test
    fun `WHEN processing a FEATURE_PROTECTION_DASHBOARD tracker category fact THEN the matching metric is recorded`() {
        val controller = createReleaseMetricController()

        fun process(category: TrackerCategory) = controller.run {
            Fact(
                    Component.FEATURE_PROTECTION_DASHBOARD,
                    Action.CLICK,
                    ProtectionDashboardFacts.Items.TRACKER_CATEGORY,
                    value = category.name,
                )
                .process()
        }

        assertNull(TrackingProtection.privacyReportTrackingCookiesTapped.testGetValue())
        process(TrackerCategory.CROSS_SITE_COOKIES)
        assertNotNull(TrackingProtection.privacyReportTrackingCookiesTapped.testGetValue())

        assertNull(TrackingProtection.privacyReportSocialTapped.testGetValue())
        process(TrackerCategory.SOCIAL_MEDIA_TRACKERS)
        assertNotNull(TrackingProtection.privacyReportSocialTapped.testGetValue())

        assertNull(TrackingProtection.privacyReportFingerprintsTapped.testGetValue())
        process(TrackerCategory.FINGERPRINTERS)
        assertNotNull(TrackingProtection.privacyReportFingerprintsTapped.testGetValue())

        assertNull(TrackingProtection.privacyReportTrackingContentTapped.testGetValue())
        process(TrackerCategory.TRACKING_CONTENT)
        assertNotNull(TrackingProtection.privacyReportTrackingContentTapped.testGetValue())
    }

    @Test
    fun `WHEN processing a FEATURE_AUTOFILL fact THEN the right metric is recorded`() {
        val controller = createReleaseMetricController()
        var fact =
            Fact(
                Component.FEATURE_AUTOFILL,
                mockk(relaxed = true),
                AutofillFacts.Items.AUTOFILL_REQUEST,
                metadata = mapOf(AutofillFacts.Metadata.HAS_MATCHING_LOGINS to true),
            )

        with(controller) {
            assertNull(AndroidAutofill.requestMatchingLogins.testGetValue())

            fact.process()

            assertNotNull(AndroidAutofill.requestMatchingLogins.testGetValue())

            fact = fact.copy(metadata = mapOf(AutofillFacts.Metadata.HAS_MATCHING_LOGINS to false))
            assertNull(AndroidAutofill.requestNoMatchingLogins.testGetValue())

            fact.process()

            assertNotNull(AndroidAutofill.requestNoMatchingLogins.testGetValue())

            fact = fact.copy(item = AutofillFacts.Items.AUTOFILL_SEARCH, action = Action.DISPLAY, metadata = null)
            assertNull(AndroidAutofill.searchDisplayed.testGetValue())

            fact.process()

            assertNotNull(AndroidAutofill.searchDisplayed.testGetValue())

            fact = fact.copy(action = Action.SELECT)
            assertNull(AndroidAutofill.searchItemSelected.testGetValue())

            fact.process()

            assertNotNull(AndroidAutofill.searchItemSelected.testGetValue())

            fact = fact.copy(item = AutofillFacts.Items.AUTOFILL_CONFIRMATION, action = Action.CONFIRM)
            assertNull(AndroidAutofill.confirmSuccessful.testGetValue())

            fact.process()

            assertNotNull(AndroidAutofill.confirmSuccessful.testGetValue())

            fact = fact.copy(action = Action.DISPLAY)
            assertNull(AndroidAutofill.confirmCancelled.testGetValue())

            fact.process()

            assertNotNull(AndroidAutofill.confirmCancelled.testGetValue())

            fact = fact.copy(item = AutofillFacts.Items.AUTOFILL_LOCK, action = Action.CONFIRM)
            assertNull(AndroidAutofill.unlockSuccessful.testGetValue())

            fact.process()

            assertNotNull(AndroidAutofill.unlockSuccessful.testGetValue())

            fact = fact.copy(action = Action.DISPLAY)
            assertNull(AndroidAutofill.unlockCancelled.testGetValue())

            fact.process()

            assertNotNull(AndroidAutofill.unlockCancelled.testGetValue())
        }
    }

    @Test
    fun `WHEN processing a ContextualMenu fact THEN the right metric is recorded`() {
        val controller = createReleaseMetricController()
        val action = mockk<Action>()
        // Verify copy button interaction
        var fact =
            Fact(
                Component.FEATURE_CONTEXTMENU,
                action,
                ContextMenuFacts.Items.TEXT_SELECTION_OPTION,
                metadata = mapOf("textSelectionOption" to Companion.CONTEXT_MENU_COPY),
            )
        assertNull(ContextualMenu.copyTapped.testGetValue())

        with(controller) {
            fact.process()
        }

        assertNotNull(ContextualMenu.copyTapped.testGetValue())
        assertEquals(1, ContextualMenu.copyTapped.testGetValue()!!.size)
        assertNull(ContextualMenu.copyTapped.testGetValue()!!.single().extra)

        // Verify search button interaction
        fact =
            Fact(
                Component.FEATURE_CONTEXTMENU,
                action,
                ContextMenuFacts.Items.TEXT_SELECTION_OPTION,
                metadata = mapOf("textSelectionOption" to Companion.CONTEXT_MENU_SEARCH),
            )
        assertNull(ContextualMenu.searchTapped.testGetValue())

        with(controller) {
            fact.process()
        }

        assertNotNull(ContextualMenu.searchTapped.testGetValue())
        assertEquals(1, ContextualMenu.searchTapped.testGetValue()!!.size)
        assertNull(ContextualMenu.searchTapped.testGetValue()!!.single().extra)

        // Verify select all button interaction
        fact =
            Fact(
                Component.FEATURE_CONTEXTMENU,
                action,
                ContextMenuFacts.Items.TEXT_SELECTION_OPTION,
                metadata = mapOf("textSelectionOption" to Companion.CONTEXT_MENU_SELECT_ALL),
            )
        assertNull(ContextualMenu.selectAllTapped.testGetValue())

        with(controller) {
            fact.process()
        }

        assertNotNull(ContextualMenu.selectAllTapped.testGetValue())
        assertEquals(1, ContextualMenu.selectAllTapped.testGetValue()!!.size)
        assertNull(ContextualMenu.selectAllTapped.testGetValue()!!.single().extra)

        // Verify share button interaction
        fact =
            Fact(
                Component.FEATURE_CONTEXTMENU,
                action,
                ContextMenuFacts.Items.TEXT_SELECTION_OPTION,
                metadata = mapOf("textSelectionOption" to Companion.CONTEXT_MENU_SHARE),
            )
        assertNull(ContextualMenu.shareTapped.testGetValue())

        with(controller) {
            fact.process()
        }

        assertNotNull(ContextualMenu.shareTapped.testGetValue())
        assertEquals(1, ContextualMenu.shareTapped.testGetValue()!!.size)
        assertNull(ContextualMenu.shareTapped.testGetValue()!!.single().extra)
    }

    @Test
    fun `WHEN a share duration fact is processed THEN the share type, content type and duration are recorded`() {
        val controller = createReleaseMetricController()
        val fact =
            Fact(
                component = Component.FEATURE_DOWNLOADS,
                action = Action.IMPLEMENTATION_DETAIL,
                item = ShareResourceFacts.Items.SHARE_DURATION,
                metadata =
                    mapOf(
                        ShareResourceFacts.MetadataKeys.SHARE_TYPE to ShareResourceFacts.ShareType.DIRECT,
                        ShareResourceFacts.MetadataKeys.CONTENT_TYPE to ShareResourceFacts.ShareContentType.PDF,
                        ShareResourceFacts.MetadataKeys.DURATION_MS to 150L,
                    ),
            )

        with(controller) { fact.process() }

        val extra = ShareSheet.shareFileResourceSuccess.testGetValue()?.single()?.extra
        assertEquals("direct", extra?.get("share_type"))
        assertEquals("pdf", extra?.get("content_type"))
        assertEquals("150", extra?.get("duration_ms"))
    }

    @Test
    fun `WHEN a share failed fact is processed THEN the share type, content type and failure reason are recorded`() {
        val controller = createReleaseMetricController()
        val fact =
            Fact(
                component = Component.FEATURE_DOWNLOADS,
                action = Action.IMPLEMENTATION_DETAIL,
                item = ShareResourceFacts.Items.SHARE_FAILED,
                metadata =
                    mapOf(
                        ShareResourceFacts.MetadataKeys.SHARE_TYPE to ShareResourceFacts.ShareType.DIRECT,
                        ShareResourceFacts.MetadataKeys.CONTENT_TYPE to ShareResourceFacts.ShareContentType.PDF,
                        ShareResourceFacts.MetadataKeys.FAILURE_REASON to ShareResourceFacts.FailureReason.TIMEOUT,
                    ),
            )

        with(controller) { fact.process() }

        val extra = ShareSheet.shareFileResourceFail.testGetValue()?.single()?.extra
        assertEquals("direct", extra?.get("share_type"))
        assertEquals("pdf", extra?.get("content_type"))
        assertEquals("timeout", extra?.get("failure_reason"))
        assertNull(ShareSheet.shareFileResourceSuccess.testGetValue())
    }

    @Test
    fun `WHEN the share-link context menu item is tapped THEN native_share_sheet_shown is recorded with context_menu_link source`() {
        val controller = createReleaseMetricController()
        val fact =
            Fact(
                Component.FEATURE_CONTEXTMENU,
                mockk(relaxed = true),
                ContextMenuFacts.Items.ITEM,
                metadata = mapOf("item" to SHARE_LINK_CONTEXT_MENU_ITEM_ID),
            )

        assertNull(NativeShareSheet.shown.testGetValue())

        with(controller) {
            fact.process()
        }

        val shownEvents = NativeShareSheet.shown.testGetValue()
        assertEquals(1, shownEvents?.size)
        assertEquals("context_menu_link", shownEvents?.single()?.extra?.get("source"))

        // The existing ContextMenu.itemTapped recording continues to fire alongside.
        val tappedEvents = ContextMenu.itemTapped.testGetValue()
        assertEquals(1, tappedEvents?.size)
        assertEquals("share_link", tappedEvents?.single()?.extra?.get("named"))
    }

    @Test
    fun `WHEN the open-in-tab-group context menu item is tapped THEN item_tapped is recorded as open_in_tab_group`() {
        val controller = createReleaseMetricController()
        val fact =
            Fact(
                Component.FEATURE_CONTEXTMENU,
                mockk(relaxed = true),
                ContextMenuFacts.Items.ITEM,
                metadata = mapOf("item" to TabGroupContextMenuCandidate.OPEN_IN_TAB_GROUP_ID),
            )

        assertNull(ContextMenu.itemTapped.testGetValue())

        with(controller) {
            fact.process()
        }

        val tappedEvents = ContextMenu.itemTapped.testGetValue()
        assertEquals(1, tappedEvents?.size)
        assertEquals("open_in_tab_group", tappedEvents?.single()?.extra?.get("named"))
    }

    @Test
    fun `WHEN processing a CreditCardAutofillDialog fact THEN the right metric is recorded`() {
        val controller = createReleaseMetricController()
        val action = mockk<Action>(relaxed = true)
        val itemsToEvents =
            listOf(
                CreditCardAutofillDialogFacts.Items.AUTOFILL_CREDIT_CARD_FORM_DETECTED to CreditCards.formDetected,
                CreditCardAutofillDialogFacts.Items.AUTOFILL_CREDIT_CARD_SUCCESS to CreditCards.autofilled,
                CreditCardAutofillDialogFacts.Items.AUTOFILL_CREDIT_CARD_PROMPT_SHOWN to
                    CreditCards.autofillPromptShown,
                CreditCardAutofillDialogFacts.Items.AUTOFILL_CREDIT_CARD_PROMPT_EXPANDED to
                    CreditCards.autofillPromptExpanded,
                CreditCardAutofillDialogFacts.Items.AUTOFILL_CREDIT_CARD_PROMPT_DISMISSED to
                    CreditCards.autofillPromptDismissed,
                CreditCardAutofillDialogFacts.Items.AUTOFILL_CREDIT_CARD_CREATED to CreditCards.savePromptCreate,
                CreditCardAutofillDialogFacts.Items.AUTOFILL_CREDIT_CARD_UPDATED to CreditCards.savePromptUpdate,
                CreditCardAutofillDialogFacts.Items.AUTOFILL_CREDIT_CARD_SAVE_PROMPT_SHOWN to
                    CreditCards.savePromptShown,
            )

        itemsToEvents.forEach { (item, event) ->
            val fact = Fact(Component.FEATURE_PROMPTS, action, item)
            controller.run {
                fact.process()
            }

            assertEquals(1, event.testGetValue()!!.size)
            assertEquals(null, event.testGetValue()!!.single().extra)
        }
    }

    @Test
    fun `GIVEN pwa facts WHEN they are processed THEN the right metric is recorded`() {
        val controller = createReleaseMetricController()
        val action = mockk<Action>(relaxed = true)

        // a PWA shortcut from homescreen was opened
        val openPWA =
            Fact(
                Component.FEATURE_PWA,
                action,
                ProgressiveWebAppFacts.Items.HOMESCREEN_ICON_TAP,
            )

        assertNull(ProgressiveWebApp.homescreenTap.testGetValue())
        controller.run {
            openPWA.process()
        }
        assertNotNull(ProgressiveWebApp.homescreenTap.testGetValue())

        // a PWA shortcut was installed
        val installPWA =
            Fact(
                Component.FEATURE_PWA,
                action,
                ProgressiveWebAppFacts.Items.INSTALL_SHORTCUT,
            )

        assertNull(ProgressiveWebApp.installTap.testGetValue())

        controller.run {
            installPWA.process()
        }

        assertNotNull(ProgressiveWebApp.installTap.testGetValue())
    }

    @Test
    fun `WHEN processing a suggestion fact THEN the right metric is recorded`() {
        val controller = createReleaseMetricController()

        // Verify synced tabs suggestion clicked
        assertNull(SyncedTabs.syncedTabsSuggestionClicked.testGetValue())
        var fact =
            Fact(Component.FEATURE_SYNCEDTABS, Action.CANCEL, SyncedTabsFacts.Items.SYNCED_TABS_SUGGESTION_CLICKED)

        with(controller) {
            fact.process()
        }

        assertNotNull(SyncedTabs.syncedTabsSuggestionClicked.testGetValue())

        // Verify bookmark suggestion clicked
        assertNull(Awesomebar.bookmarkSuggestionClicked.testGetValue())
        fact = Fact(Component.FEATURE_AWESOMEBAR, Action.CANCEL, AwesomeBarFacts.Items.BOOKMARK_SUGGESTION_CLICKED)

        with(controller) {
            fact.process()
        }

        assertNotNull(Awesomebar.bookmarkSuggestionClicked.testGetValue())

        // Verify clipboard suggestion clicked
        assertNull(Awesomebar.clipboardSuggestionClicked.testGetValue())
        fact = Fact(Component.FEATURE_AWESOMEBAR, Action.CANCEL, AwesomeBarFacts.Items.CLIPBOARD_SUGGESTION_CLICKED)

        with(controller) {
            fact.process()
        }

        assertNotNull(Awesomebar.clipboardSuggestionClicked.testGetValue())

        // Verify history suggestion clicked
        assertNull(Awesomebar.historySuggestionClicked.testGetValue())
        fact = Fact(Component.FEATURE_AWESOMEBAR, Action.CANCEL, AwesomeBarFacts.Items.HISTORY_SUGGESTION_CLICKED)

        with(controller) {
            fact.process()
        }

        assertNotNull(Awesomebar.historySuggestionClicked.testGetValue())

        // Verify search action clicked
        assertNull(Awesomebar.searchActionClicked.testGetValue())
        fact = Fact(Component.FEATURE_AWESOMEBAR, Action.CANCEL, AwesomeBarFacts.Items.SEARCH_ACTION_CLICKED)

        with(controller) {
            fact.process()
        }

        assertNotNull(Awesomebar.searchActionClicked.testGetValue())

        // Verify search suggestion action clicked
        assertNull(Awesomebar.searchSuggestionClicked.testGetValue())
        fact = Fact(Component.FEATURE_AWESOMEBAR, Action.CANCEL, AwesomeBarFacts.Items.SEARCH_SUGGESTION_CLICKED)

        with(controller) {
            fact.process()
        }

        assertNotNull(Awesomebar.searchSuggestionClicked.testGetValue())

        // Verify trending search suggestion action clicked
        assertNull(Awesomebar.trendingSearchSuggestionClicked.testGetValue())
        fact =
            Fact(
                Component.FEATURE_AWESOMEBAR,
                Action.CANCEL,
                AwesomeBarFacts.Items.TRENDING_SEARCH_SUGGESTION_CLICKED,
                "3",
            )

        with(controller) {
            fact.process()
        }

        assertNotNull(Awesomebar.trendingSearchSuggestionClicked.testGetValue())
        assertNotNull(Awesomebar.trendingSearchSuggestionClicked.testGetValue()!![0].extra)
        assertEquals("3", Awesomebar.trendingSearchSuggestionClicked.testGetValue()!![0].extra!!["position"])

        // Verify top site suggestion action clicked
        assertNull(Awesomebar.topSiteSuggestionClicked.testGetValue())
        fact = Fact(Component.FEATURE_AWESOMEBAR, Action.CANCEL, AwesomeBarFacts.Items.TOP_SITE_SUGGESTION_CLICKED, "2")

        with(controller) {
            fact.process()
        }

        assertNotNull(Awesomebar.topSiteSuggestionClicked.testGetValue())
        assertNotNull(Awesomebar.topSiteSuggestionClicked.testGetValue()!![0].extra)
        assertEquals("2", Awesomebar.topSiteSuggestionClicked.testGetValue()!![0].extra!!["position"])

        // Verify recent search suggestion action clicked
        assertNull(Awesomebar.recentSearchSuggestionClicked.testGetValue())
        fact =
            Fact(
                Component.FEATURE_AWESOMEBAR,
                Action.CANCEL,
                AwesomeBarFacts.Items.RECENT_SEARCH_SUGGESTION_CLICKED,
                "1",
            )

        with(controller) {
            fact.process()
        }

        assertNotNull(Awesomebar.recentSearchSuggestionClicked.testGetValue())
        assertNotNull(Awesomebar.recentSearchSuggestionClicked.testGetValue()!![0].extra)
        assertEquals("1", Awesomebar.recentSearchSuggestionClicked.testGetValue()!![0].extra!!["position"])

        // Verify bookmark opened tab suggestion clicked
        assertNull(Awesomebar.openedTabSuggestionClicked.testGetValue())
        fact = Fact(Component.FEATURE_AWESOMEBAR, Action.CANCEL, AwesomeBarFacts.Items.OPENED_TAB_SUGGESTION_CLICKED)

        with(controller) {
            fact.process()
        }

        assertNotNull(Awesomebar.openedTabSuggestionClicked.testGetValue())

        // Verify a search term suggestion clicked
        assertNull(Awesomebar.searchTermSuggestionClicked.testGetValue())
        fact = Fact(Component.FEATURE_AWESOMEBAR, Action.CANCEL, AwesomeBarFacts.Items.SEARCH_TERM_SUGGESTION_CLICKED)

        with(controller) {
            fact.process()
        }

        assertNotNull(Awesomebar.searchTermSuggestionClicked.testGetValue())

        // Verify trending search suggestions displayed
        assertNull(Awesomebar.trendingSearchSuggestionsDisplayed.testGetValue())
        fact =
            Fact(
                Component.FEATURE_AWESOMEBAR,
                Action.CANCEL,
                AwesomeBarFacts.Items.TRENDING_SEARCH_SUGGESTIONS_DISPLAYED,
                "6",
            )

        with(controller) {
            fact.process()
        }

        assertNotNull(Awesomebar.trendingSearchSuggestionsDisplayed.testGetValue())
        assertNotNull(Awesomebar.trendingSearchSuggestionsDisplayed.testGetValue()!![0].extra)
        assertEquals("6", Awesomebar.trendingSearchSuggestionsDisplayed.testGetValue()!![0].extra!!["count"])

        // Verify top site suggestions displayed
        assertNull(Awesomebar.topSiteSuggestionsDisplayed.testGetValue())
        fact =
            Fact(Component.FEATURE_AWESOMEBAR, Action.CANCEL, AwesomeBarFacts.Items.TOP_SITE_SUGGESTIONS_DISPLAYED, "5")

        with(controller) {
            fact.process()
        }

        assertNotNull(Awesomebar.topSiteSuggestionsDisplayed.testGetValue())
        assertNotNull(Awesomebar.topSiteSuggestionsDisplayed.testGetValue()!![0].extra)
        assertEquals("5", Awesomebar.topSiteSuggestionsDisplayed.testGetValue()!![0].extra!!["count"])

        // Verify recent site suggestions displayed
        assertNull(Awesomebar.recentSearchSuggestionsDisplayed.testGetValue())
        fact =
            Fact(
                Component.FEATURE_AWESOMEBAR,
                Action.CANCEL,
                AwesomeBarFacts.Items.RECENT_SEARCH_SUGGESTIONS_DISPLAYED,
                "4",
            )

        with(controller) {
            fact.process()
        }

        assertNotNull(Awesomebar.recentSearchSuggestionsDisplayed.testGetValue())
        assertNotNull(Awesomebar.recentSearchSuggestionsDisplayed.testGetValue()!![0].extra)
        assertEquals("4", Awesomebar.recentSearchSuggestionsDisplayed.testGetValue()!![0].extra!!["count"])

        // Verify optimized suggestion cards displayed
        assertNull(Awesomebar.optimizedSuggestionCardDisplayed.testGetValue())
        fact =
            Fact(
                Component.FEATURE_AWESOMEBAR,
                Action.DISPLAY,
                AwesomeBarFacts.Items.OPTIMIZED_SUGGESTION_CARD_DISPLAYED,
                SuggestionCardType.SPORTS.value,
                mapOf("extra" to "basketball"),
            )

        with(controller) {
            fact.process()
        }

        assertNotNull(Awesomebar.optimizedSuggestionCardDisplayed.testGetValue())
        assertNotNull(Awesomebar.optimizedSuggestionCardDisplayed.testGetValue()!![0].extra)
        assertEquals("sports", Awesomebar.optimizedSuggestionCardDisplayed.testGetValue()!![0].extra!!["card_type"])
        assertEquals("basketball", Awesomebar.optimizedSuggestionCardDisplayed.testGetValue()!![0].extra!!["extra"])

        // Verify optimized suggestion cards clicked
        assertNull(Awesomebar.optimizedSuggestionCardClicked.testGetValue())
        fact =
            Fact(
                Component.FEATURE_AWESOMEBAR,
                Action.CLICK,
                AwesomeBarFacts.Items.OPTIMIZED_SUGGESTION_CARD_CLICKED,
                SuggestionCardType.STOCKS.value,
                mapOf("extra" to "up"),
            )

        with(controller) {
            fact.process()
        }

        assertNotNull(Awesomebar.optimizedSuggestionCardClicked.testGetValue())
        assertNotNull(Awesomebar.optimizedSuggestionCardClicked.testGetValue()!![0].extra)
        assertEquals("stocks", Awesomebar.optimizedSuggestionCardClicked.testGetValue()!![0].extra!!["card_type"])
        assertEquals("up", Awesomebar.optimizedSuggestionCardClicked.testGetValue()!![0].extra!!["extra"])
    }

    @Test
    fun `GIVEN advertising search facts WHEN the list is processed THEN the right metric is recorded`() {
        val controller = createReleaseMetricController(isMarketingDataTelemetryEnabled = { false })
        val action = mockk<Action>()

        // an ad was clicked in a Search Engine Result Page
        val addClickedInSearchFact =
            Fact(
                Component.FEATURE_SEARCH,
                action,
                AdsTelemetry.SERP_ADD_CLICKED,
                "provider",
            )

        assertNull(BrowserSearch.adClicks["provider"].testGetValue())
        controller.run {
            addClickedInSearchFact.process()
        }
        assertNotNull(BrowserSearch.adClicks["provider"].testGetValue())
        assertEquals(1, BrowserSearch.adClicks["provider"].testGetValue())

        // the user opened a Search Engine Result Page of one of our search providers which contains ads
        val searchWithAdsOpenedFact =
            Fact(
                Component.FEATURE_SEARCH,
                action,
                AdsTelemetry.SERP_SHOWN_WITH_ADDS,
                "provider",
            )

        assertNull(BrowserSearch.withAds["provider"].testGetValue())

        controller.run {
            searchWithAdsOpenedFact.process()
        }

        assertNotNull(BrowserSearch.withAds["provider"].testGetValue())
        assertEquals(1, BrowserSearch.withAds["provider"].testGetValue())

        // the user performed a search
        val inContentSearchFact =
            Fact(
                Component.FEATURE_SEARCH,
                action,
                InContentTelemetry.IN_CONTENT_SEARCH,
                "provider",
            )

        assertNull(BrowserSearch.inContent["provider"].testGetValue())

        controller.run {
            inContentSearchFact.process()
        }

        assertNotNull(BrowserSearch.inContent["provider"].testGetValue())
        assertEquals(1, BrowserSearch.inContent["provider"].testGetValue())

        // the user performed another search
        controller.run {
            inContentSearchFact.process()
        }

        assertEquals(2, BrowserSearch.inContent["provider"].testGetValue())
    }

    @Test
    fun `GIVEN a site permissions prompt is shown WHEN processing the fact THEN the right metric is recorded`() {
        val controller = createReleaseMetricController()
        val fact =
            Fact(
                component = Component.FEATURE_SITEPERMISSIONS,
                action = Action.DISPLAY,
                item = SitePermissionsFacts.Items.PERMISSIONS,
                value = "test",
            )
        assertNull(SitePermissions.promptShown.testGetValue())

        controller.run {
            fact.process()
        }

        assertEquals(1, SitePermissions.promptShown.testGetValue()!!.size)
        assertEquals(
            "test",
            SitePermissions.promptShown.testGetValue()!!.single().extra!!["permissions"],
        )
    }

    @Test
    fun `GIVEN site permissions are allowed WHEN processing the fact THEN the right metric is recorded`() {
        val controller = createReleaseMetricController()
        val fact =
            Fact(
                component = Component.FEATURE_SITEPERMISSIONS,
                action = Action.CONFIRM,
                item = SitePermissionsFacts.Items.PERMISSIONS,
                value = "allow",
            )
        assertNull(SitePermissions.promptShown.testGetValue())

        controller.run {
            fact.process()
        }

        assertEquals(1, SitePermissions.permissionsAllowed.testGetValue()!!.size)
        assertEquals(
            "allow",
            SitePermissions.permissionsAllowed.testGetValue()!!.single().extra!!["permissions"],
        )
    }

    @Test
    fun `GIVEN site permissions are denied WHEN processing the fact THEN the right metric is recorded`() {
        val controller = createReleaseMetricController()
        val fact =
            Fact(
                component = Component.FEATURE_SITEPERMISSIONS,
                action = Action.CANCEL,
                item = SitePermissionsFacts.Items.PERMISSIONS,
                value = "deny",
            )
        assertNull(SitePermissions.promptShown.testGetValue())

        controller.run {
            fact.process()
        }

        assertEquals(1, SitePermissions.permissionsDenied.testGetValue()!!.size)
        assertEquals("deny", SitePermissions.permissionsDenied.testGetValue()!!.single().extra!!["permissions"])
    }

    @Test
    fun `WHEN capture_attempted fact is processed THEN captureAttempted labeled counter is incremented for the trigger`() {
        val controller = createReleaseMetricController()

        listOf(
                BrowserThumbnailsFacts.CaptureAttemptedTriggers.LOAD_COMPLETED,
                BrowserThumbnailsFacts.CaptureAttemptedTriggers.TAB_COUNTER_CLICK,
                BrowserThumbnailsFacts.CaptureAttemptedTriggers.SWIPE_TO_SWITCH_TABS,
                BrowserThumbnailsFacts.CaptureAttemptedTriggers.EXTERNAL_REQUEST,
            )
            .forEach { trigger ->
                assertNull(BrowserThumbnails.captureAttempted[trigger].testGetValue())

                controller.run {
                    Fact(
                            component = Component.BROWSER_THUMBNAILS,
                            action = Action.IMPLEMENTATION_DETAIL,
                            item = BrowserThumbnailsFacts.Items.CAPTURE_ATTEMPTED,
                            value = trigger,
                        )
                        .process()
                }

                assertEquals(1, BrowserThumbnails.captureAttempted[trigger].testGetValue())
            }
    }

    @Test
    fun `WHEN capture_attempted fact has a null value THEN no captureAttempted label is incremented`() {
        val controller = createReleaseMetricController()

        controller.run {
            Fact(
                    component = Component.BROWSER_THUMBNAILS,
                    action = Action.IMPLEMENTATION_DETAIL,
                    item = BrowserThumbnailsFacts.Items.CAPTURE_ATTEMPTED,
                    value = null,
                )
                .process()
        }

        listOf(
                BrowserThumbnailsFacts.CaptureAttemptedTriggers.LOAD_COMPLETED,
                BrowserThumbnailsFacts.CaptureAttemptedTriggers.TAB_COUNTER_CLICK,
                BrowserThumbnailsFacts.CaptureAttemptedTriggers.SWIPE_TO_SWITCH_TABS,
                BrowserThumbnailsFacts.CaptureAttemptedTriggers.EXTERNAL_REQUEST,
            )
            .forEach { trigger ->
                assertNull(BrowserThumbnails.captureAttempted[trigger].testGetValue())
            }
    }

    @Test
    fun `WHEN capture_result fact is processed THEN captureResult labeled counter is incremented for the outcome`() {
        val controller = createReleaseMetricController()

        listOf(
                BrowserThumbnailsFacts.CaptureResults.SUCCEEDED,
                BrowserThumbnailsFacts.CaptureResults.NULL_BITMAP,
                BrowserThumbnailsFacts.CaptureResults.LOW_MEMORY,
            )
            .forEach { result ->
                assertNull(BrowserThumbnails.captureResult[result].testGetValue())

                controller.run {
                    Fact(
                            component = Component.BROWSER_THUMBNAILS,
                            action = Action.IMPLEMENTATION_DETAIL,
                            item = BrowserThumbnailsFacts.Items.CAPTURE_RESULT,
                            value = result,
                        )
                        .process()
                }

                assertEquals(1, BrowserThumbnails.captureResult[result].testGetValue())
            }
    }

    @Test
    fun `WHEN capture_result fact has a null value THEN no captureResult label is incremented`() {
        val controller = createReleaseMetricController()

        controller.run {
            Fact(
                    component = Component.BROWSER_THUMBNAILS,
                    action = Action.IMPLEMENTATION_DETAIL,
                    item = BrowserThumbnailsFacts.Items.CAPTURE_RESULT,
                    value = null,
                )
                .process()
        }

        listOf(
                BrowserThumbnailsFacts.CaptureResults.SUCCEEDED,
                BrowserThumbnailsFacts.CaptureResults.NULL_BITMAP,
                BrowserThumbnailsFacts.CaptureResults.LOW_MEMORY,
            )
            .forEach { result ->
                assertNull(BrowserThumbnails.captureResult[result].testGetValue())
            }
    }

    @Test
    fun `WHEN capture_duration fact is processed THEN captureDuration timing distribution accumulates the sample`() {
        val controller = createReleaseMetricController()

        assertNull(BrowserThumbnails.captureDuration.testGetValue())

        controller.run {
            Fact(
                    component = Component.BROWSER_THUMBNAILS,
                    action = Action.IMPLEMENTATION_DETAIL,
                    item = BrowserThumbnailsFacts.Items.CAPTURE_DURATION,
                    metadata = mapOf(BrowserThumbnailsFacts.MetadataKeys.DURATION_MS to 42L),
                )
                .process()
        }

        val distribution = BrowserThumbnails.captureDuration.testGetValue()
        assertNotNull(distribution)
        assertEquals(1L, distribution.count)
    }

    @Test
    fun `WHEN capture_duration fact has missing duration metadata THEN nothing is accumulated`() {
        val controller = createReleaseMetricController()

        controller.run {
            Fact(
                    component = Component.BROWSER_THUMBNAILS,
                    action = Action.IMPLEMENTATION_DETAIL,
                    item = BrowserThumbnailsFacts.Items.CAPTURE_DURATION,
                    metadata = null,
                )
                .process()
        }

        assertNull(BrowserThumbnails.captureDuration.testGetValue())
    }

    @Test
    fun `WHEN disk_write_duration fact is processed THEN diskWriteDuration timing distribution accumulates the sample`() {
        val controller = createReleaseMetricController()

        assertNull(BrowserThumbnails.diskWriteDuration.testGetValue())

        controller.run {
            Fact(
                    component = Component.BROWSER_THUMBNAILS,
                    action = Action.IMPLEMENTATION_DETAIL,
                    item = BrowserThumbnailsFacts.Items.DISK_WRITE_DURATION,
                    metadata = mapOf(BrowserThumbnailsFacts.MetadataKeys.DURATION_MS to 128L),
                )
                .process()
        }

        val distribution = BrowserThumbnails.diskWriteDuration.testGetValue()
        assertNotNull(distribution)
        assertEquals(1L, distribution.count)
    }

    @Test
    fun `WHEN disk_write_duration fact has missing duration metadata THEN nothing is accumulated`() {
        val controller = createReleaseMetricController()

        controller.run {
            Fact(
                    component = Component.BROWSER_THUMBNAILS,
                    action = Action.IMPLEMENTATION_DETAIL,
                    item = BrowserThumbnailsFacts.Items.DISK_WRITE_DURATION,
                    metadata = null,
                )
                .process()
        }

        assertNull(BrowserThumbnails.diskWriteDuration.testGetValue())
    }

    private fun createReleaseMetricController(
        services: List<MetricsService> = emptyList(),
        isDataTelemetryEnabled: () -> Boolean = { true },
        isMarketingDataTelemetryEnabled: () -> Boolean = { true },
        isUsageTelemetryEnabled: () -> Boolean = { true },
        settings: Settings = mockk(),
    ) =
        ReleaseMetricController(
            services = services,
            isDataTelemetryEnabled = isDataTelemetryEnabled,
            isMarketingDataTelemetryEnabled = isMarketingDataTelemetryEnabled,
            isUsageTelemetryEnabled = isUsageTelemetryEnabled,
            settings = settings,
        )
}
