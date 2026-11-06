/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.telemetry

import android.content.Context
import android.os.Build
import android.os.RemoteException
import android.os.StrictMode
import androidx.annotation.VisibleForTesting
import androidx.core.app.NotificationManagerCompat
import androidx.preference.PreferenceManager
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import mozilla.components.browser.state.search.SearchEngine
import mozilla.components.browser.state.state.selectedOrDefaultSearchEngine
import mozilla.components.feature.search.ext.waitForSelectedOrDefaultSearchEngine
import mozilla.components.feature.search.telemetry.SearchProviderModel
import mozilla.components.service.glean.net.ConceptFetchHttpUploader
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.utils.Browsers
import mozilla.telemetry.glean.Glean
import mozilla.telemetry.glean.config.Configuration
import org.mozilla.experiments.nimbus.Nimbus.Pings as NimbusPings
import org.mozilla.focus.BuildConfig
import org.mozilla.focus.Components
import org.mozilla.focus.GleanMetrics.Browser
import org.mozilla.focus.GleanMetrics.GleanBuildInfo
import org.mozilla.focus.GleanMetrics.Metrics
import org.mozilla.focus.GleanMetrics.MozillaProducts
import org.mozilla.focus.GleanMetrics.Notifications
import org.mozilla.focus.GleanMetrics.Pings
import org.mozilla.focus.GleanMetrics.Preferences
import org.mozilla.focus.GleanMetrics.Shortcuts
import org.mozilla.focus.GleanMetrics.TrackingProtection
import org.mozilla.focus.R
import org.mozilla.focus.ext.components
import org.mozilla.focus.ext.settings
import org.mozilla.focus.topsites.DefaultTopSitesStorage.Companion.TOP_SITES_MAX_LIMIT
import org.mozilla.focus.utils.AppConstants
import org.mozilla.focus.utils.Settings

/**
 * Glean telemetry service.
 *
 * To track events, use Glean's generated bindings directly.
 */
class GleanMetricsService(
    context: Context,
    mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
    val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
) : MetricsService {

    private val serviceScope = CoroutineScope(SupervisorJob() + mainDispatcher)
    private val activationPing = ActivationPing(context, serviceScope, ioDispatcher)

    companion object {
        // collection name to fetch from server for SERP telemetry
        const val COLLECTION_NAME = "search-telemetry-v2"

        // urls for prod and stage remote settings server
        internal const val REMOTE_PROD_ENDPOINT_URL = "https://firefox.settings.services.mozilla.com"
        internal const val REMOTE_STAGE_ENDPOINT_URL = "https://firefox.settings.services.allizom.org"

        private val isEnabledByDefault: Boolean
            get() = !AppConstants.isKlarBuild

        private fun isDeviceWithTelemetryDisabled(): Boolean {
            val brand = "blackberry"
            val device = "bbf100"

            return Build.BRAND == brand && Build.DEVICE == device
        }

        /**
         * Determines whether or not telemetry is enabled. Currently, according to our lean data policy, general
         * telemetry is disabled.
         */
        @JvmStatic
        @Suppress("FunctionOnlyReturningConstant", "UNUSED_PARAMETER")
        fun isTelemetryEnabled(context: Context? = null): Boolean = false

        /**
         * Determines whether or not daily usage telemetry should be enabled by default. This matches whether general
         * telemetry was enabled prior to the switch being removed. Currently, according to our lean data policy,
         * general telemetry is disabled.
         */
        @JvmStatic
        fun shouldTelemetryBeEnabledByDefault(context: Context): Boolean {
            if (isDeviceWithTelemetryDisabled()) {
                return false
            }

            // The first access to shared preferences will require a disk read.
            val threadPolicy = StrictMode.allowThreadDiskReads()
            try {
                val resources = context.resources
                val preferences = PreferenceManager.getDefaultSharedPreferences(context)

                return preferences.getBoolean(
                    resources.getString(R.string.pref_key_telemetry),
                    isEnabledByDefault,
                ) && !AppConstants.isDevBuild
            } finally {
                StrictMode.setThreadPolicy(threadPolicy)
            }
        }
    }

    override fun initialize(context: Context) {
        val components = context.components
        val settings = context.settings
        val telemetryEnabled = isTelemetryEnabled(context)

        Glean.initialize(
            applicationContext = context,
            uploadEnabled = telemetryEnabled,
            configuration =
                Configuration(
                    channel = BuildConfig.FLAVOR,
                    httpClient =
                        ConceptFetchHttpUploader(
                            client = lazy(LazyThreadSafetyMode.NONE) { components.client },
                            usePrivateRequest = true,
                            supportsOhttp = true,
                        ),
                ),
            buildInfo = GleanBuildInfo.buildInfo,
        )

        Glean.registerPings(Pings)
        Glean.registerPings(NimbusPings.nimbusTargetingContext)

        if (telemetryEnabled) {
            serviceScope.launch {
                val providerList = components.serpTelemetryRepository.updateProviderList()
                installSearchTelemetryExtensions(components, providerList)
            }
        }

        // Do this immediately after init.
        serviceScope.launch {
            // Wait for preferences to be collected before we send the activation ping.
            collectPrefMetrics(components, settings, context)

            components.store.waitForSelectedOrDefaultSearchEngine { searchEngine ->
                if (searchEngine != null) {
                    Browser.defaultSearchEngine.set(getDefaultSearchEngineIdentifierForTelemetry(context))
                }

                activationPing.checkAndSend()
            }
        }
    }

    private suspend fun collectPrefMetrics(
        components: Components,
        settings: Settings,
        context: Context,
    ) =
        withContext(ioDispatcher) {
            val installedBrowsers = Browsers.all(context)
            val hasFenixInstalled = FenixProductDetector.getInstalledFenixVersions(context).isNotEmpty()
            val isFenixDefaultBrowser = FenixProductDetector.isFenixDefaultBrowser(installedBrowsers.defaultBrowser)
            val isFocusDefaultBrowser = Browsers.isDefaultBrowser(context)

            Metrics.searchWidgetInstalled.set(settings.searchWidgetInstalled)

            Browser.isDefault.set(isFocusDefaultBrowser)
            Browser.localeOverride.set(components.store.state.locale?.displayName ?: "none")
            val shortcutsOnHomeNumber =
                components.topSitesStorage
                    .getTopSites(
                        targetSitesCount = TOP_SITES_MAX_LIMIT,
                        frecencyConfig = null,
                    )
                    .size
            Shortcuts.shortcutsOnHomeNumber.set(shortcutsOnHomeNumber.toLong())

            val installSourcePackage =
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                    context.packageManager.getInstallSourceInfo(context.packageName).installingPackageName
                } else {
                    @Suppress("DEPRECATION") context.packageManager.getInstallerPackageName(context.packageName)
                }

            Browser.installSource.set(installSourcePackage.orEmpty())

            // Fenix telemetry
            MozillaProducts.hasFenixInstalled.set(hasFenixInstalled)
            MozillaProducts.isFenixDefaultBrowser.set(isFenixDefaultBrowser)

            // tracking protection metrics
            TrackingProtection.hasAdvertisingBlocked.set(settings.shouldBlockAdTrackers())
            TrackingProtection.hasAnalyticsBlocked.set(settings.shouldBlockAnalyticTrackers())
            TrackingProtection.hasContentBlocked.set(settings.shouldBlockOtherTrackers())
            TrackingProtection.hasSocialBlocked.set(settings.shouldBlockSocialTrackers())

            // theme telemetry
            val currentTheme =
                when {
                    settings.lightThemeSelected -> {
                        "Light"
                    }

                    settings.darkThemeSelected -> {
                        "Dark"
                    }

                    settings.useDefaultThemeSelected -> {
                        "Follow device"
                    }

                    else -> ""
                }
            if (currentTheme.isNotEmpty()) {
                Preferences.userTheme.set(currentTheme)
            }

            try {
                Notifications.permissionGranted.set(NotificationManagerCompat.from(context).areNotificationsEnabled())
            } catch (e: RemoteException) {
                Logger.warn("Failed to check notifications state", e)
            }
        }

    private fun getDefaultSearchEngineIdentifierForTelemetry(context: Context): String {
        val searchEngine = context.components.store.state.search.selectedOrDefaultSearchEngine
        return if (searchEngine?.type == SearchEngine.Type.CUSTOM) {
            "custom"
        } else {
            searchEngine?.name ?: "<none>"
        }
    }

    @VisibleForTesting
    internal suspend fun installSearchTelemetryExtensions(
        components: Components,
        providerList: List<SearchProviderModel>,
    ) {
        val engine = components.engine
        components.store.apply {
            components.adsTelemetry.install(engine, this@apply, providerList)
            components.searchTelemetry.install(engine, this@apply, providerList)
        }
    }
}
