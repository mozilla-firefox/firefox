/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.webcompat.middleware

import androidx.test.ext.junit.runners.AndroidJUnit4
import io.mockk.mockk
import io.mockk.verify
import kotlinx.coroutines.test.runTest
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.addJsonObject
import kotlinx.serialization.json.buildJsonArray
import kotlinx.serialization.json.buildJsonObject
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.EngineSession
import mozilla.components.support.test.mock
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.test.rule.MainCoroutineRule
import mozilla.telemetry.glean.testing.GleanTestRule
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.BrokenSiteReport
import org.mozilla.fenix.GleanMetrics.BrokenSiteReportBrowserInfo
import org.mozilla.fenix.GleanMetrics.BrokenSiteReportBrowserInfoApp
import org.mozilla.fenix.GleanMetrics.BrokenSiteReportBrowserInfoGraphics
import org.mozilla.fenix.GleanMetrics.BrokenSiteReportBrowserInfoPrefs
import org.mozilla.fenix.GleanMetrics.BrokenSiteReportBrowserInfoSystem
import org.mozilla.fenix.GleanMetrics.BrokenSiteReportTabInfo
import org.mozilla.fenix.GleanMetrics.BrokenSiteReportTabInfoAntitracking
import org.mozilla.fenix.GleanMetrics.BrokenSiteReportTabInfoFrameworks
import org.mozilla.fenix.GleanMetrics.Pings
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.webcompat.store.WebCompatReporterAction
import org.mozilla.fenix.webcompat.store.WebCompatReporterState
import org.mozilla.fenix.webcompat.store.WebCompatReporterStore

@RunWith(AndroidJUnit4::class)
class WebCompatReporterSubmissionMiddlewareTest {
    private val appStore: AppStore = mockk()

    @get:Rule
    val coroutinesTestRule = MainCoroutineRule()

    @get:Rule
    val gleanTestRule = GleanTestRule(testContext)

    @Test
    fun `GIVEN the URL is not changed WHEN WebCompatInfo is retrieved successfully THEN all report broken site pings are submitted`() = runTest {
        val webCompatReporterRetrievalService: WebCompatReporterRetrievalService =
            FakeWebCompatReporterRetrievalService()

        val store = createStore(service = webCompatReporterRetrievalService)

        Pings.brokenSiteReport.testBeforeNextSubmit {
            assertEquals(
                "basic",
                BrokenSiteReportTabInfoAntitracking.blockList.testGetValue(),
            )
            assertEquals(
                false,
                BrokenSiteReportTabInfoAntitracking.btpHasPurgedSite.testGetValue(),
            )
            assertEquals(
                false,
                BrokenSiteReportTabInfoAntitracking.hasMixedActiveContentBlocked.testGetValue(),
            )
            assertEquals(
                false,
                BrokenSiteReportTabInfoAntitracking.hasMixedDisplayContentBlocked.testGetValue(),
            )
            assertEquals(
                false,
                BrokenSiteReportTabInfoAntitracking.hasTrackingContentBlocked.testGetValue(),
            )
            assertEquals(
                false,
                BrokenSiteReportTabInfoAntitracking.isPrivateBrowsing.testGetValue(),
            )

            assertEquals(
                BrokenSiteReportBrowserInfo.AddonsObject(),
                BrokenSiteReportBrowserInfo.addons.testGetValue(),
            )

            assertEquals(
                BrokenSiteReportBrowserInfo.ExperimentsObject(),
                BrokenSiteReportBrowserInfo.experiments.testGetValue(),
            )

            assertEquals(
                "testDefaultUserAgent",
                BrokenSiteReportBrowserInfoApp.defaultUseragentString.testGetValue(),
            )

            assertEquals(
                """[{"id":"device1"},{"id":"device2"},{"id":"device3"}]""",
                BrokenSiteReportBrowserInfoGraphics.devicesJson.testGetValue(),
            )
            assertEquals(
                """[{"id":"driver1"},{"id":"driver2"},{"id":"driver3"}]""",
                BrokenSiteReportBrowserInfoGraphics.driversJson.testGetValue(),
            )
            assertEquals(
                """{"id":"feature1"}""",
                BrokenSiteReportBrowserInfoGraphics.featuresJson.testGetValue(),
            )
            assertEquals(
                true,
                BrokenSiteReportBrowserInfoGraphics.hasTouchScreen.testGetValue(),
            )
            assertEquals(
                """[{"id":"monitor1"},{"id":"monitor2"},{"id":"monitor3"}]""",
                BrokenSiteReportBrowserInfoGraphics.monitorsJson.testGetValue(),
            )

            assertEquals(
                listOf("en-CA", "en-US"),
                BrokenSiteReportBrowserInfoApp.defaultLocales.testGetValue(),
            )

            assertEquals(
                false,
                BrokenSiteReportBrowserInfoApp.fissionEnabled.testGetValue(),
            )
            assertEquals(
                1L,
                BrokenSiteReportBrowserInfoSystem.memory.testGetValue(),
            )

            assertEquals(
                false,
                BrokenSiteReportBrowserInfoPrefs.opaqueResponseBlocking.testGetValue(),
            )
            assertEquals(
                false,
                BrokenSiteReportBrowserInfoPrefs.installtriggerEnabled.testGetValue(),
            )
            assertEquals(
                false,
                BrokenSiteReportBrowserInfoPrefs.softwareWebrender.testGetValue(),
            )
            assertEquals(
                1L,
                BrokenSiteReportBrowserInfoPrefs.cookieBehavior.testGetValue(),
            )
            assertEquals(
                false,
                BrokenSiteReportBrowserInfoPrefs.globalPrivacyControlEnabled.testGetValue(),
            )
            assertEquals(
                false,
                BrokenSiteReportBrowserInfoPrefs.resistFingerprintingEnabled.testGetValue(),
            )

            assertEquals(
                "1.5",
                BrokenSiteReportBrowserInfoGraphics.devicePixelRatio.testGetValue(),
            )

            assertEquals(
                true,
                BrokenSiteReportTabInfoFrameworks.fastclick.testGetValue(),
            )
            assertEquals(
                true,
                BrokenSiteReportTabInfoFrameworks.marfeel.testGetValue(),
            )
            assertEquals(
                true,
                BrokenSiteReportTabInfoFrameworks.mobify.testGetValue(),
            )

            assertEquals(
                listOf("en-CA", "en-US"),
                BrokenSiteReportTabInfo.languages.testGetValue(),
            )

            assertEquals(
                "testUserAgent",
                BrokenSiteReportTabInfo.useragentString.testGetValue(),
            )

            assertEquals(store.state.enteredUrl, BrokenSiteReport.url.testGetValue())
            assertEquals(
                store.state.reason?.name,
                BrokenSiteReport.breakageCategory.testGetValue(),
            )
            assertEquals(
                store.state.problemDescription,
                BrokenSiteReport.description.testGetValue(),
            )
        }
    }

    fun `WHEN the report is sent successfully THEN appState is updated`() {
        val webCompatReporterRetrievalService: WebCompatReporterRetrievalService = FakeWebCompatReporterRetrievalService()

        val store = createStore(service = webCompatReporterRetrievalService)

        store.dispatch(WebCompatReporterAction.SendReportClicked)

        verify { appStore.dispatch(AppAction.WebCompatAction.WebCompatReportSent) }
    }

    @Test
    fun `GIVEN the URL is changed WHEN WebCompatInfo is retrieved successfully THEN only non tab related report broken site pings are submitted`() = runTest {
        val webCompatReporterRetrievalService: WebCompatReporterRetrievalService = FakeWebCompatReporterRetrievalService()

        val store = createStore(service = webCompatReporterRetrievalService)

        Pings.brokenSiteReport.testBeforeNextSubmit {
            assertNull(BrokenSiteReportTabInfoAntitracking.blockList.testGetValue())
            assertNull(BrokenSiteReportTabInfoAntitracking.btpHasPurgedSite.testGetValue())
            assertNull(BrokenSiteReportTabInfoAntitracking.hasMixedActiveContentBlocked.testGetValue())
            assertNull(BrokenSiteReportTabInfoAntitracking.hasMixedDisplayContentBlocked.testGetValue())
            assertNull(BrokenSiteReportTabInfoAntitracking.hasTrackingContentBlocked.testGetValue())
            assertNull(BrokenSiteReportTabInfoAntitracking.isPrivateBrowsing.testGetValue())

            assertNull(BrokenSiteReportBrowserInfo.addons.testGetValue())
            assertNull(BrokenSiteReportBrowserInfo.experiments.testGetValue())

            assertEquals(
                BrokenSiteReportBrowserInfo.AddonsObject(),
                BrokenSiteReportBrowserInfo.addons.testGetValue(),
            )

            assertEquals(
                BrokenSiteReportBrowserInfo.ExperimentsObject(),
                BrokenSiteReportBrowserInfo.experiments.testGetValue(),
            )

            assertEquals(
                "testDefaultUserAgent",
                BrokenSiteReportBrowserInfoApp.defaultUseragentString.testGetValue(),
            )

            assertEquals(
                """[{"id":"device1"},{"id":"device2"},{"id":"device3"}]""",
                BrokenSiteReportBrowserInfoGraphics.devicesJson.testGetValue(),
            )
            assertEquals(
                """[{"id":"driver1"},{"id":"driver2"},{"id":"driver3"}]""",
                BrokenSiteReportBrowserInfoGraphics.driversJson.testGetValue(),
            )
            assertEquals(
                """{"id":"feature1"}""",
                BrokenSiteReportBrowserInfoGraphics.featuresJson.testGetValue(),
            )
            assertEquals(
                true,
                BrokenSiteReportBrowserInfoGraphics.hasTouchScreen.testGetValue(),
            )
            assertEquals(
                """[{"id":"monitor1"},{"id":"monitor2"},{"id":"monitor3"}]""",
                BrokenSiteReportBrowserInfoGraphics.monitorsJson.testGetValue(),
            )

            assertEquals(
                BrokenSiteReportBrowserInfo.AddonsObject(),
                BrokenSiteReportBrowserInfo.addons.testGetValue(),
            )

            assertEquals(
                BrokenSiteReportBrowserInfo.ExperimentsObject(),
                BrokenSiteReportBrowserInfo.experiments.testGetValue(),
            )

            assertEquals(
                listOf("en-CA", "en-US"),
                BrokenSiteReportBrowserInfoApp.defaultLocales.testGetValue(),
            )

            assertEquals(
                false,
                BrokenSiteReportBrowserInfoApp.fissionEnabled.testGetValue(),
            )
            assertEquals(
                1L,
                BrokenSiteReportBrowserInfoSystem.memory.testGetValue(),
            )

            assertEquals(
                false,
                BrokenSiteReportBrowserInfoPrefs.opaqueResponseBlocking.testGetValue(),
            )
            assertEquals(
                false,
                BrokenSiteReportBrowserInfoPrefs.installtriggerEnabled.testGetValue(),
            )
            assertEquals(
                false,
                BrokenSiteReportBrowserInfoPrefs.softwareWebrender.testGetValue(),
            )
            assertEquals(
                1L,
                BrokenSiteReportBrowserInfoPrefs.cookieBehavior.testGetValue(),
            )
            assertEquals(
                false,
                BrokenSiteReportBrowserInfoPrefs.globalPrivacyControlEnabled.testGetValue(),
            )
            assertEquals(
                false,
                BrokenSiteReportBrowserInfoPrefs.resistFingerprintingEnabled.testGetValue(),
            )

            assertEquals(
                "1.5",
                BrokenSiteReportBrowserInfoGraphics.devicePixelRatio.testGetValue(),
            )

            assertNull(BrokenSiteReportTabInfoFrameworks.fastclick.testGetValue())
            assertNull(BrokenSiteReportTabInfoFrameworks.marfeel.testGetValue())
            assertNull(BrokenSiteReportTabInfoFrameworks.mobify.testGetValue())

            assertNull(BrokenSiteReportTabInfo.languages.testGetValue())

            assertNotEquals(store.state.tabUrl, BrokenSiteReport.url.testGetValue())
            assertEquals(store.state.enteredUrl, BrokenSiteReport.url.testGetValue())
            assertEquals(
                store.state.reason?.name,
                BrokenSiteReport.breakageCategory.testGetValue(),
            )
            assertEquals(
                store.state.problemDescription,
                BrokenSiteReport.description.testGetValue(),
            )

            assertNull(BrokenSiteReportTabInfo.useragentString.testGetValue())
        }

        store.dispatch(WebCompatReporterAction.SendReportClicked)
    }

    @Test
    fun `WHEN WebCompatInfo is not retrieved successfully THEN only the form fields are submitted`() = runTest {
        val webCompatReporterRetrievalService = object : WebCompatReporterRetrievalService {
            override suspend fun retrieveInfo(): WebCompatInfoDto? = null
        }

        val store = createStore(service = webCompatReporterRetrievalService)

        Pings.brokenSiteReport.testBeforeNextSubmit {
            assertNull(BrokenSiteReportTabInfoAntitracking.blockList.testGetValue())
            assertNull(BrokenSiteReportTabInfoAntitracking.btpHasPurgedSite.testGetValue())
            assertNull(BrokenSiteReportTabInfoAntitracking.hasMixedActiveContentBlocked.testGetValue())
            assertNull(BrokenSiteReportTabInfoAntitracking.hasMixedDisplayContentBlocked.testGetValue())
            assertNull(BrokenSiteReportTabInfoAntitracking.hasTrackingContentBlocked.testGetValue())
            assertNull(BrokenSiteReportTabInfoAntitracking.isPrivateBrowsing.testGetValue())

            assertNull(BrokenSiteReportBrowserInfo.addons.testGetValue())
            assertNull(BrokenSiteReportBrowserInfo.experiments.testGetValue())

            assertNull(BrokenSiteReportBrowserInfoApp.defaultUseragentString.testGetValue())

            assertNull(BrokenSiteReportBrowserInfoGraphics.devicesJson.testGetValue())
            assertNull(BrokenSiteReportBrowserInfoGraphics.driversJson.testGetValue())
            assertNull(BrokenSiteReportBrowserInfoGraphics.featuresJson.testGetValue())
            assertNull(BrokenSiteReportBrowserInfoGraphics.hasTouchScreen.testGetValue())
            assertNull(BrokenSiteReportBrowserInfoGraphics.monitorsJson.testGetValue())
            assertNull(BrokenSiteReportBrowserInfoApp.defaultLocales.testGetValue())

            assertNull(BrokenSiteReportBrowserInfoApp.fissionEnabled.testGetValue())
            assertNull(BrokenSiteReportBrowserInfoSystem.memory.testGetValue())

            assertNull(BrokenSiteReportBrowserInfoPrefs.opaqueResponseBlocking.testGetValue())
            assertNull(BrokenSiteReportBrowserInfoPrefs.installtriggerEnabled.testGetValue())
            assertNull(BrokenSiteReportBrowserInfoPrefs.softwareWebrender.testGetValue())
            assertNull(BrokenSiteReportBrowserInfoPrefs.cookieBehavior.testGetValue())
            assertNull(BrokenSiteReportBrowserInfoPrefs.globalPrivacyControlEnabled.testGetValue())
            assertNull(BrokenSiteReportBrowserInfoPrefs.resistFingerprintingEnabled.testGetValue())

            assertNull(BrokenSiteReportBrowserInfoGraphics.devicePixelRatio.testGetValue())

            assertNull(BrokenSiteReportTabInfoFrameworks.fastclick.testGetValue())
            assertNull(BrokenSiteReportTabInfoFrameworks.marfeel.testGetValue())
            assertNull(BrokenSiteReportTabInfoFrameworks.mobify.testGetValue())

            assertNull(BrokenSiteReportTabInfo.languages.testGetValue())

            assertEquals(store.state.enteredUrl, BrokenSiteReport.url.testGetValue())
            assertEquals(
                store.state.reason?.name,
                BrokenSiteReport.breakageCategory.testGetValue(),
            )
            assertEquals(
                store.state.problemDescription,
                BrokenSiteReport.description.testGetValue(),
            )

            assertNull(BrokenSiteReportTabInfo.useragentString.testGetValue())
        }

        store.dispatch(WebCompatReporterAction.SendReportClicked)
    }

    private fun createStore(service: WebCompatReporterRetrievalService): WebCompatReporterStore {
        val engineSession: EngineSession = mock()
        val tab = createTab(
            url = "https://www.mozilla.org",
            id = "test-tab",
            engineSession = engineSession,
        )
        val browserStore = BrowserStore(
            initialState = BrowserState(
                tabs = listOf(tab),
                selectedTabId = tab.id,
            ),
        )

        return WebCompatReporterStore(
            initialState = WebCompatReporterState(
                tabUrl = "https://www.mozilla.org",
                enteredUrl = "https://www.mozilla.org/en-US/firefox/new/",
                reason = WebCompatReporterState.BrokenSiteReason.Slow,
                problemDescription = "",
            ),
            middleware = listOf(
                WebCompatReporterSubmissionMiddleware(
                    appStore = appStore,
                    browserStore = browserStore,
                    webCompatReporterRetrievalService = service,
                    scope = coroutinesTestRule.scope,
                ),
            ),
        )
    }

    private class FakeWebCompatReporterRetrievalService : WebCompatReporterRetrievalService {

        override suspend fun retrieveInfo(): WebCompatInfoDto =
            WebCompatInfoDto(
                antitracking = WebCompatInfoDto.WebCompatAntiTrackingDto(
                    blockList = "basic",
                    btpHasPurgedSite = false,
                    hasMixedActiveContentBlocked = false,
                    hasMixedDisplayContentBlocked = false,
                    hasTrackingContentBlocked = false,
                    isPrivateBrowsing = false,
                ),
                browser = WebCompatInfoDto.WebCompatBrowserDto(
                    addons = listOf(
                        WebCompatInfoDto.WebCompatBrowserDto.AddonDto(id = "id.temp", name = "name1", temporary = true, version = "version1"),
                        WebCompatInfoDto.WebCompatBrowserDto.AddonDto(id = "id.perm", name = "name2", temporary = false, version = "version2"),
                    ),
                    app = WebCompatInfoDto.WebCompatBrowserDto.AppDto(
                        defaultUserAgent = "testDefaultUserAgent",
                    ),
                    experiments = listOf(
                        WebCompatInfoDto.WebCompatBrowserDto.ExperimentDto(branch = "branch1", slug = "slug1", kind = "kind1"),
                        WebCompatInfoDto.WebCompatBrowserDto.ExperimentDto(branch = "branch2", slug = "slug2", kind = "kind2"),
                    ),
                    graphics = WebCompatInfoDto.WebCompatBrowserDto.GraphicsDto(
                        devices = buildJsonArray {
                            addJsonObject {
                                put("id", JsonPrimitive("device1"))
                            }
                            addJsonObject {
                                put("id", JsonPrimitive("device2"))
                            }
                            addJsonObject {
                                put("id", JsonPrimitive("device3"))
                            }
                        },
                        drivers = buildJsonArray {
                            addJsonObject {
                                put("id", JsonPrimitive("driver1"))
                            }
                            addJsonObject {
                                put("id", JsonPrimitive("driver2"))
                            }
                            addJsonObject {
                                put("id", JsonPrimitive("driver3"))
                            }
                        },
                        features = buildJsonObject { put("id", JsonPrimitive("feature1")) },
                        hasTouchScreen = true,
                        monitors = buildJsonArray {
                            addJsonObject {
                                put("id", JsonPrimitive("monitor1"))
                            }
                            addJsonObject {
                                put("id", JsonPrimitive("monitor2"))
                            }
                            addJsonObject {
                                put("id", JsonPrimitive("monitor3"))
                            }
                        },
                    ),
                    locales = listOf("en-CA", "en-US"),
                    platform = WebCompatInfoDto.WebCompatBrowserDto.PlatformDto(
                        fissionEnabled = false,
                        memoryMB = 1,
                    ),
                    prefs = WebCompatInfoDto.WebCompatBrowserDto.PrefsDto(
                        browserOpaqueResponseBlocking = false,
                        extensionsInstallTriggerEnabled = false,
                        gfxWebRenderSoftware = false,
                        networkCookieBehavior = 1,
                        privacyGlobalPrivacyControlEnabled = false,
                        privacyResistFingerprinting = false,
                    ),
                ),
                url = "https://www.mozilla.org",
                devicePixelRatio = 1.5,
                frameworks = WebCompatInfoDto.WebCompatFrameworksDto(
                    fastclick = true,
                    marfeel = true,
                    mobify = true,
                ),
                languages = listOf("en-CA", "en-US"),
                userAgent = "testUserAgent",
            )
    }
}
