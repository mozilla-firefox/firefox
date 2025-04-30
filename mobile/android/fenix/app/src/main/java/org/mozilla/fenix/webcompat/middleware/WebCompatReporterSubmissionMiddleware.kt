/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.webcompat.middleware

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch
import kotlinx.serialization.json.Json
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.MiddlewareContext
import mozilla.components.support.base.log.logger.Logger
import org.json.JSONObject
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

/**
 * [Middleware] that reacts to submission related [WebCompatReporterAction]s.
 *
 * @param appStore [AppStore] used to dispatch [AppAction]s.
 * @param browserStore [BrowserStore] used to access [BrowserState].
 * @param webCompatReporterRetrievalService The service that handles submission requests.
 * @param scope The [CoroutineScope] for launching coroutines.
 */
class WebCompatReporterSubmissionMiddleware(
    private val appStore: AppStore,
    private val browserStore: BrowserStore,
    private val webCompatReporterRetrievalService: WebCompatReporterRetrievalService,
    private val scope: CoroutineScope,
) : Middleware<WebCompatReporterState, WebCompatReporterAction> {

    private val logger = Logger("WebCompatReporterSubmissionMiddleware")

    override fun invoke(
        context: MiddlewareContext<WebCompatReporterState, WebCompatReporterAction>,
        next: (WebCompatReporterAction) -> Unit,
        action: WebCompatReporterAction,
    ) {
        next(action)

        when (action) {
            is WebCompatReporterAction.SendReportClicked -> {
                scope.launch {
                    val webCompatInfo = webCompatReporterRetrievalService.retrieveInfo()
                    webCompatInfo?.let {
                        val enteredUrlMatchesTabUrl = context.state.enteredUrl == webCompatInfo.url
                        if (enteredUrlMatchesTabUrl) {
                            setTabAntiTrackingMetrics(antiTracking = webCompatInfo.antitracking)
                            setTabFrameworksMetrics(frameworks = webCompatInfo.frameworks)
                            setTabLanguageMetrics(languages = webCompatInfo.languages)
                            setTabUserAgentMetrics(userAgent = webCompatInfo.userAgent)
                        }

                        setBrowserInfoMetrics(browserInfo = webCompatInfo.browser)
                        setDevicePixelRatioMetrics(devicePixelRatio = webCompatInfo.devicePixelRatio)
                    }
                    setUrlMetrics(url = context.state.enteredUrl)
                    setReasonMetrics(reason = context.state.reason)
                    setDescriptionMetrics(description = context.state.problemDescription)

                    Pings.brokenSiteReport.submit()
                    context.store.dispatch(WebCompatReporterAction.ReportSubmitted)
                    appStore.dispatch(AppAction.WebCompatAction.WebCompatReportSent)
                }
            }
            is WebCompatReporterAction.SendMoreInfoClicked -> {
                scope.launch {
                    val selectedTabId = browserStore.state.selectedTabId
                    val selectedTab = browserStore.state.tabs.find { it.id == selectedTabId }
                    val engineSession = selectedTab?.engineState?.engineSession

                    val webCompatInfo = webCompatReporterRetrievalService.retrieveInfo()
                    webCompatInfo?.let {
                        val json = Json
                        val info = JSONObject().apply {
                            put("reason", context.state.reason)
                            put("description", context.state.problemDescription)
                            put("endpointUrl", "https://webcompat.com/issues/new")
                            put("reportUrl", context.state.enteredUrl)
                            put(
                                "reporterConfig",
                                JSONObject().apply {
                                    put("src", "android-components-reporter")
                                    put("utm_campaign", "report-site-issue-button")
                                    put("utm_source", "android-components-reporter")
                                },
                            )
                            put("webcompatInfo", JSONObject(json.encodeToString(webCompatInfo)))
                        }

                        engineSession?.sendMoreWebCompatInfo(
                            info = info,
                            onResult = {
                                logger.debug("SendMoreWebCompatInfo succeeded")
                            },
                            onException = {
                                logger.error("Error with SendMoreWebCompatInfo", it)
                            },
                        )
                    }
                    context.store.dispatch(WebCompatReporterAction.SendMoreInfoSubmitted)
                }
            }
            else -> {}
        }
    }

    private fun setTabAntiTrackingMetrics(antiTracking: WebCompatInfoDto.WebCompatAntiTrackingDto) {
        BrokenSiteReportTabInfoAntitracking.blockList.set(antiTracking.blockList)
        BrokenSiteReportTabInfoAntitracking.btpHasPurgedSite.set(antiTracking.btpHasPurgedSite)
        BrokenSiteReportTabInfoAntitracking.hasMixedActiveContentBlocked.set(
            antiTracking.hasMixedActiveContentBlocked,
        )
        BrokenSiteReportTabInfoAntitracking.hasMixedDisplayContentBlocked.set(
            antiTracking.hasMixedDisplayContentBlocked,
        )
        BrokenSiteReportTabInfoAntitracking.hasTrackingContentBlocked.set(
            antiTracking.hasTrackingContentBlocked,
        )
        BrokenSiteReportTabInfoAntitracking.isPrivateBrowsing.set(antiTracking.isPrivateBrowsing)
    }

    private fun setBrowserInfoMetrics(browserInfo: WebCompatInfoDto.WebCompatBrowserDto) {
        val addons = BrokenSiteReportBrowserInfo.AddonsObject()
        for (addon in browserInfo.addons) {
            addons.add(
                BrokenSiteReportBrowserInfo.AddonsObjectItem(
                    id = addon.id,
                    name = addon.name,
                    temporary = addon.temporary,
                    version = addon.version,
                ),
            )
        }
        BrokenSiteReportBrowserInfo.addons.set(addons)

        val experiments = BrokenSiteReportBrowserInfo.ExperimentsObject()
        for (experiment in browserInfo.experiments) {
            experiments.add(
                BrokenSiteReportBrowserInfo.ExperimentsObjectItem(
                    branch = experiment.branch,
                    slug = experiment.slug,
                    kind = experiment.kind,
                ),
            )
        }
        BrokenSiteReportBrowserInfo.experiments.set(experiments)

        browserInfo.app?.let {
            BrokenSiteReportBrowserInfoApp.defaultUseragentString.set(it.defaultUserAgent)
        }

        BrokenSiteReportBrowserInfoApp.defaultLocales.set(browserInfo.locales)

        BrokenSiteReportBrowserInfoApp.fissionEnabled.set(browserInfo.platform.fissionEnabled)
        BrokenSiteReportBrowserInfoSystem.memory.set(browserInfo.platform.memoryMB)

        setBrowserInfoGraphicsMetrics(browserInfo.graphics)
        setBrowserInfoPrefsMetrics(browserInfo.prefs)
    }

    private fun setBrowserInfoGraphicsMetrics(graphicsInfo: WebCompatInfoDto.WebCompatBrowserDto.GraphicsDto?) {
        graphicsInfo?.let {
            it.devices?.let { devices ->
                BrokenSiteReportBrowserInfoGraphics.devicesJson.set(devices.toString())
            }

            BrokenSiteReportBrowserInfoGraphics.driversJson.set(it.drivers.toString())

            it.features?.let { features ->
                BrokenSiteReportBrowserInfoGraphics.featuresJson.set(features.toString())
            }

            it.hasTouchScreen?.let { hasTouchScreen ->
                BrokenSiteReportBrowserInfoGraphics.hasTouchScreen.set(hasTouchScreen)
            }

            it.monitors?.let { monitors ->
                BrokenSiteReportBrowserInfoGraphics.monitorsJson.set(monitors.toString())
            }
        }
    }

    private fun setBrowserInfoPrefsMetrics(prefsInfo: WebCompatInfoDto.WebCompatBrowserDto.PrefsDto) {
        BrokenSiteReportBrowserInfoPrefs.opaqueResponseBlocking.set(prefsInfo.browserOpaqueResponseBlocking)
        BrokenSiteReportBrowserInfoPrefs.installtriggerEnabled.set(prefsInfo.extensionsInstallTriggerEnabled)
        BrokenSiteReportBrowserInfoPrefs.softwareWebrender.set(prefsInfo.gfxWebRenderSoftware)
        BrokenSiteReportBrowserInfoPrefs.cookieBehavior.set(prefsInfo.networkCookieBehavior)
        BrokenSiteReportBrowserInfoPrefs.globalPrivacyControlEnabled.set(prefsInfo.privacyGlobalPrivacyControlEnabled)
        BrokenSiteReportBrowserInfoPrefs.resistFingerprintingEnabled.set(prefsInfo.privacyResistFingerprinting)
    }

    private fun setDevicePixelRatioMetrics(devicePixelRatio: Double) {
        BrokenSiteReportBrowserInfoGraphics.devicePixelRatio.set(devicePixelRatio.toString())
    }

    private fun setTabFrameworksMetrics(frameworks: WebCompatInfoDto.WebCompatFrameworksDto) {
        BrokenSiteReportTabInfoFrameworks.fastclick.set(frameworks.fastclick)
        BrokenSiteReportTabInfoFrameworks.marfeel.set(frameworks.marfeel)
        BrokenSiteReportTabInfoFrameworks.mobify.set(frameworks.mobify)
    }

    private fun setTabLanguageMetrics(languages: List<String>) {
        BrokenSiteReportTabInfo.languages.set(languages)
    }

    private fun setUrlMetrics(url: String) {
        BrokenSiteReport.url.set(url)
    }

    private fun setReasonMetrics(reason: WebCompatReporterState.BrokenSiteReason?) {
        reason?.let {
            BrokenSiteReport.breakageCategory.set(reason.name)
        }
    }

    private fun setDescriptionMetrics(description: String) {
        BrokenSiteReport.description.set(description)
    }

    private fun setTabUserAgentMetrics(userAgent: String) {
        BrokenSiteReportTabInfo.useragentString.set(userAgent)
    }
}
