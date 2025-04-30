/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components

import android.annotation.SuppressLint
import android.app.Application
import android.content.Context
import androidx.compose.runtime.Composable
import androidx.compose.ui.platform.LocalContext
import androidx.core.app.NotificationManagerCompat
import com.google.android.play.core.review.ReviewManagerFactory
import mozilla.appservices.remotesettings.RemoteSettingsServer
import mozilla.components.feature.addons.AddonManager
import mozilla.components.feature.addons.amo.AMOAddonsProvider
import mozilla.components.feature.addons.migration.DefaultSupportedAddonsChecker
import mozilla.components.feature.addons.update.DefaultAddonUpdater
import mozilla.components.feature.autofill.AutofillConfiguration
import mozilla.components.lib.crash.store.CrashAction
import mozilla.components.lib.crash.store.CrashMiddleware
import mozilla.components.lib.publicsuffixlist.PublicSuffixList
import mozilla.components.support.base.android.NotificationsDelegate
import mozilla.components.support.base.worker.Frequency
import mozilla.components.support.remotesettings.DefaultRemoteSettingsSyncScheduler
import mozilla.components.support.remotesettings.RemoteSettingsService
import org.mozilla.fenix.BuildConfig
import org.mozilla.fenix.Config
import org.mozilla.fenix.FeatureFlags
import org.mozilla.fenix.R
import org.mozilla.fenix.autofill.AutofillConfirmActivity
import org.mozilla.fenix.autofill.AutofillSearchActivity
import org.mozilla.fenix.autofill.AutofillUnlockActivity
import org.mozilla.fenix.browser.tabstrip.isTabStripEnabled
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.components.appstate.setup.checklist.SetupChecklistState
import org.mozilla.fenix.components.appstate.setup.checklist.getSetupChecklistCollection
import org.mozilla.fenix.components.metrics.MetricsMiddleware
import org.mozilla.fenix.crashes.CrashReportingAppMiddleware
import org.mozilla.fenix.crashes.SettingsCrashReportCache
import org.mozilla.fenix.datastore.pocketStoriesSelectedCategoriesDataStore
import org.mozilla.fenix.distributions.DefaultDistributionBrowserStoreProvider
import org.mozilla.fenix.distributions.DefaultDistributionProviderChecker
import org.mozilla.fenix.distributions.DistributionIdManager
import org.mozilla.fenix.ext.asRecentTabs
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.filterState
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.ext.sort
import org.mozilla.fenix.home.PocketUpdatesMiddleware
import org.mozilla.fenix.home.blocklist.BlocklistHandler
import org.mozilla.fenix.home.blocklist.BlocklistMiddleware
import org.mozilla.fenix.home.middleware.HomeTelemetryMiddleware
import org.mozilla.fenix.home.setup.store.DefaultSetupChecklistRepository
import org.mozilla.fenix.home.setup.store.SetupChecklistPreferencesMiddleware
import org.mozilla.fenix.home.setup.store.SetupChecklistTelemetryMiddleware
import org.mozilla.fenix.messaging.state.MessagingMiddleware
import org.mozilla.fenix.nimbus.FxNimbus
import org.mozilla.fenix.onboarding.FenixOnboarding
import org.mozilla.fenix.perf.AppStartReasonProvider
import org.mozilla.fenix.perf.StartupActivityLog
import org.mozilla.fenix.perf.StartupStateProvider
import org.mozilla.fenix.perf.StrictModeManager
import org.mozilla.fenix.perf.lazyMonitored
import org.mozilla.fenix.utils.ClipboardHandler
import org.mozilla.fenix.utils.Settings
import org.mozilla.fenix.utils.isLargeScreenSize
import org.mozilla.fenix.wifi.WifiConnectionMonitor
import java.util.concurrent.TimeUnit

private const val AMO_COLLECTION_MAX_CACHE_AGE = 2 * 24 * 60L // Two days in minutes

/**
 * Provides access to all components. This class is an implementation of the Service Locator
 * pattern, which helps us manage the dependencies in our app.
 *
 * Note: these aren't just "components" from "android-components": they're any "component" that
 * can be considered a building block of our app.
 */
class Components(private val context: Context) {
    val backgroundServices by lazyMonitored {
        BackgroundServices(
            context,
            push,
            analytics.crashReporter,
            core.lazyHistoryStorage,
            core.lazyBookmarksStorage,
            core.lazyPasswordsStorage,
            core.lazyRemoteTabsStorage,
            core.lazyAutofillStorage,
            strictMode,
        )
    }
    val services by lazyMonitored { Services(context, core.store, backgroundServices.accountManager) }
    val core by lazyMonitored { Core(context, analytics.crashReporter, strictMode) }

    val useCases by lazyMonitored {
        UseCases(
            context,
            lazyMonitored { core.engine },
            lazyMonitored { core.store },
            lazyMonitored { core.webAppShortcutManager },
            lazyMonitored { core.topSitesStorage },
            lazyMonitored { core.bookmarksStorage },
            lazyMonitored { core.historyStorage },
            lazyMonitored { backgroundServices.syncedTabsCommands },
            lazyMonitored { appStore },
            lazyMonitored { core.client },
            lazyMonitored { strictMode },
        )
    }

    private val notificationManagerCompat = NotificationManagerCompat.from(context)

    val notificationsDelegate: NotificationsDelegate by lazyMonitored {
        NotificationsDelegate(notificationManagerCompat)
    }

    val intentProcessors by lazyMonitored {
        IntentProcessors(
            context,
            core.store,
            useCases.sessionUseCases,
            useCases.tabsUseCases,
            useCases.customTabsUseCases,
            useCases.searchUseCases,
            core.webAppManifestStorage,
            core.engine,
        )
    }

    val addonsProvider by lazyMonitored {
        // Check if we have a customized (overridden) AMO collection (supported in Nightly & Beta)
        if (FeatureFlags.customExtensionCollectionFeature && context.settings().amoCollectionOverrideConfigured()) {
            AMOAddonsProvider(
                context,
                core.client,
                collectionUser = context.settings().overrideAmoUser,
                collectionName = context.settings().overrideAmoCollection,
            )
        }
        // Use build config otherwise
        else if (BuildConfig.AMO_COLLECTION_USER.isNotEmpty() &&
            BuildConfig.AMO_COLLECTION_NAME.isNotEmpty()
        ) {
            AMOAddonsProvider(
                context,
                core.client,
                serverURL = BuildConfig.AMO_SERVER_URL,
                collectionUser = BuildConfig.AMO_COLLECTION_USER,
                collectionName = BuildConfig.AMO_COLLECTION_NAME,
                maxCacheAgeInMinutes = AMO_COLLECTION_MAX_CACHE_AGE,
            )
        }
        // Fall back to defaults
        else {
            AMOAddonsProvider(context, core.client, maxCacheAgeInMinutes = AMO_COLLECTION_MAX_CACHE_AGE)
        }
    }

    @Suppress("MagicNumber")
    val addonUpdater by lazyMonitored {
        DefaultAddonUpdater(context, Frequency(12, TimeUnit.HOURS), notificationsDelegate)
    }

    @Suppress("MagicNumber")
    val supportedAddonsChecker by lazyMonitored {
        DefaultSupportedAddonsChecker(
            context,
            Frequency(12, TimeUnit.HOURS),
        )
    }

    @Suppress("MagicNumber")
    val remoteSettingsSyncScheduler by lazyMonitored {
        DefaultRemoteSettingsSyncScheduler(
            context,
            Frequency(24, TimeUnit.HOURS),
        )
    }

    val addonManager by lazyMonitored {
        AddonManager(core.store, core.engine, addonsProvider, addonUpdater)
    }

    val analytics by lazyMonitored { Analytics(context, performance.visualCompletenessQueue.queue) }
    val nimbus by lazyMonitored { NimbusComponents(context) }
    val publicSuffixList by lazyMonitored { PublicSuffixList(context) }
    val clipboardHandler by lazyMonitored { ClipboardHandler(context) }
    val performance by lazyMonitored { PerformanceComponent() }
    val push by lazyMonitored { Push(context, analytics.crashReporter) }
    val wifiConnectionMonitor by lazyMonitored { WifiConnectionMonitor(context as Application) }
    val strictMode by lazyMonitored { StrictModeManager(Config, this) }

    val settings by lazyMonitored { Settings(context) }
    val fenixOnboarding by lazyMonitored { FenixOnboarding(context) }

    val reviewPromptController by lazyMonitored {
        ReviewPromptController(
            manager = ReviewManagerFactory.create(context),
            reviewSettings = FenixReviewSettings(settings),
        )
    }

    @delegate:SuppressLint("NewApi")
    val autofillConfiguration by lazyMonitored {
        AutofillConfiguration(
            storage = core.passwordsStorage,
            publicSuffixList = publicSuffixList,
            unlockActivity = AutofillUnlockActivity::class.java,
            confirmActivity = AutofillConfirmActivity::class.java,
            searchActivity = AutofillSearchActivity::class.java,
            applicationName = context.getString(R.string.app_name),
            httpClient = core.client,
        )
    }

    val appStartReasonProvider by lazyMonitored { AppStartReasonProvider() }
    val startupActivityLog by lazyMonitored { StartupActivityLog() }
    val startupStateProvider by lazyMonitored { StartupStateProvider(startupActivityLog, appStartReasonProvider) }

    val appStore by lazyMonitored {
        val blocklistHandler = BlocklistHandler(settings)

        AppStore(
            initialState = AppState(
                collections = core.tabCollectionStorage.cachedTabCollections,
                expandedCollections = emptySet(),
                topSites = core.topSitesStorage.cachedTopSites.sort(),
                bookmarks = emptyList(),
                showCollectionPlaceholder = settings.showCollectionsPlaceholderOnHome,
                // Provide an initial state for recent tabs to prevent re-rendering on the home screen.
                //  This will otherwise cause a visual jump as the section gets rendered from no state
                //  to some state.
                recentTabs = if (settings.showRecentTabsFeature) {
                    core.store.state.asRecentTabs()
                } else {
                    emptyList()
                },
                recentHistory = emptyList(),
                setupChecklistState = setupChecklistState(),
            ).run { filterState(blocklistHandler) },
            middlewares = listOf(
                BlocklistMiddleware(blocklistHandler),
                PocketUpdatesMiddleware(
                    lazyMonitored { core.pocketStoriesService },
                    context.pocketStoriesSelectedCategoriesDataStore,
                ),
                MessagingMiddleware(
                    controller = nimbus.messaging,
                    settings = settings,
                ),
                MetricsMiddleware(metrics = analytics.metrics),
                CrashReportingAppMiddleware(
                    CrashMiddleware(
                        cache = SettingsCrashReportCache(settings),
                        crashReporter = analytics.crashReporter,
                        currentTimeInMillis = { System.currentTimeMillis() },
                    ),
                ),
                HomeTelemetryMiddleware(),
                SetupChecklistPreferencesMiddleware(DefaultSetupChecklistRepository(context)),
                SetupChecklistTelemetryMiddleware(),
            ),
        ).also {
            it.dispatch(AppAction.SetupChecklistAction.Init)
            it.dispatch(AppAction.CrashActionWrapper(CrashAction.Initialize))
        }
    }

    private fun setupChecklistState() = if (settings.showSetupChecklist) {
        val type = FxNimbus.features.setupChecklist.value().setupChecklistType
        SetupChecklistState(
            checklistItems = getSetupChecklistCollection(
                settings = settings,
                collection = type,
                tabStripEnabled = context.isTabStripEnabled(),
            ),
        )
    } else {
        null
    }

    val remoteSettingsService = lazyMonitored {
        RemoteSettingsService(
            context,
            if (context.settings().useProductionRemoteSettingsServer) {
                RemoteSettingsServer.Prod
            } else {
                RemoteSettingsServer.Stage
            },
            channel = BuildConfig.BUILD_TYPE,
            // Need to send this value separately, since `isLargeScreenSize()` is a fenix extension
            isLargeScreenSize = context.isLargeScreenSize(),
        )
    }

    val fxSuggest by lazyMonitored { FxSuggest(context, remoteSettingsService.value) }

    val distributionIdManager by lazyMonitored {
        DistributionIdManager(
            context = context,
            browserStoreProvider = DefaultDistributionBrowserStoreProvider(core.store),
            distributionProviderChecker = DefaultDistributionProviderChecker(context),
        )
    }
}

/**
 * Returns the [Components] object from within a [Composable].
 */
val components: Components
    @Composable
    get() = LocalContext.current.components
