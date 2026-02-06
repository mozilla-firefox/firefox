/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix

import android.annotation.SuppressLint
import android.app.ActivityManager
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.Build.VERSION.SDK_INT
import android.os.StrictMode
import android.os.SystemClock
import android.util.Log.INFO
import androidx.annotation.VisibleForTesting
import androidx.appcompat.app.AppCompatDelegate
import androidx.compose.runtime.Composable
import androidx.core.app.NotificationManagerCompat
import androidx.core.content.getSystemService
import androidx.core.net.toUri
import androidx.lifecycle.ProcessLifecycleOwner
import androidx.work.Configuration.Builder
import androidx.work.Configuration.Provider
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.DelicateCoroutinesApi
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Dispatchers.IO
import kotlinx.coroutines.GlobalScope
import kotlinx.coroutines.async
import kotlinx.coroutines.launch
import mozilla.appservices.autofill.AutofillApiException
import mozilla.components.browser.state.action.SystemAction
import mozilla.components.browser.state.selector.selectedTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.browser.storage.sync.GlobalPlacesDependencyProvider
import mozilla.components.concept.base.crash.Breadcrumb
import mozilla.components.concept.engine.webextension.WebExtension
import mozilla.components.concept.engine.webextension.isUnsupported
import mozilla.components.concept.push.PushProcessor
import mozilla.components.concept.storage.FrecencyThresholdOption
import mozilla.components.feature.addons.migration.DefaultSupportedAddonsChecker
import mozilla.components.feature.addons.update.GlobalAddonDependencyProvider
import mozilla.components.feature.autofill.AutofillUseCases
import mozilla.components.feature.fxsuggest.GlobalFxSuggestDependencyProvider
import mozilla.components.feature.search.ext.buildSearchUrl
import mozilla.components.feature.search.ext.waitForSelectedOrDefaultSearchEngine
import mozilla.components.feature.syncedtabs.commands.GlobalSyncedTabsCommandsProvider
import mozilla.components.feature.top.sites.TopSitesFrecencyConfig
import mozilla.components.feature.top.sites.TopSitesProviderConfig
import mozilla.components.feature.webcompat.reporter.WebCompatReporterFeature
import mozilla.components.lib.crash.CrashReporter
import mozilla.components.service.fxa.manager.SyncEnginesStorage
import mozilla.components.service.sync.autofill.GlobalAutofillDependencyProvider
import mozilla.components.service.sync.logins.GlobalLoginsDependencyProvider
import mozilla.components.service.sync.logins.LoginsApiException
import mozilla.components.support.AppServicesInitializer
import mozilla.components.support.base.ext.areNotificationsEnabledSafe
import mozilla.components.support.base.ext.isNotificationChannelEnabled
import mozilla.components.support.base.facts.register
import mozilla.components.support.base.log.Log
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.base.log.sink.AndroidLogSink
import mozilla.components.support.ktx.android.arch.lifecycle.addObservers
import mozilla.components.support.ktx.android.content.isMainProcess
import mozilla.components.support.ktx.android.content.runOnlyInMainProcess
import mozilla.components.support.locale.LocaleAwareApplication
import mozilla.components.support.remotesettings.GlobalRemoteSettingsDependencyProvider
import mozilla.components.support.rusthttp.RustHttpConfig
import mozilla.components.support.utils.BrowsersCache
import mozilla.components.support.utils.logElapsedTime
import mozilla.components.support.webextensions.WebExtensionSupport
import mozilla.telemetry.glean.Glean
import org.mozilla.fenix.GleanMetrics.Addons
import org.mozilla.fenix.GleanMetrics.Addresses
import org.mozilla.fenix.GleanMetrics.AndroidAutofill
import org.mozilla.fenix.GleanMetrics.CreditCards
import org.mozilla.fenix.GleanMetrics.CustomizeHome
import org.mozilla.fenix.GleanMetrics.Events.marketingNotificationAllowed
import org.mozilla.fenix.GleanMetrics.Logins
import org.mozilla.fenix.GleanMetrics.Metrics
import org.mozilla.fenix.GleanMetrics.PerfStartup
import org.mozilla.fenix.GleanMetrics.Preferences
import org.mozilla.fenix.GleanMetrics.SearchDefaultEngine
import org.mozilla.fenix.GleanMetrics.TabStrip
import org.mozilla.fenix.GleanMetrics.TermsOfUse
import org.mozilla.fenix.components.Components
import org.mozilla.fenix.components.Core
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.initializeGlean
import org.mozilla.fenix.components.metrics.MozillaProductDetector
import org.mozilla.fenix.components.startMetricsIfEnabled
import org.mozilla.fenix.experiments.maybeFetchExperiments
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.containsQueryParameters
import org.mozilla.fenix.ext.isCustomEngine
import org.mozilla.fenix.ext.isKnownSearchDomain
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.home.topsites.TopSitesConfigConstants.TOP_SITES_PROVIDER_LIMIT
import org.mozilla.fenix.home.topsites.TopSitesConfigConstants.TOP_SITES_PROVIDER_MAX_THRESHOLD
import org.mozilla.fenix.lifecycle.StoreLifecycleObserver
import org.mozilla.fenix.lifecycle.VisibilityLifecycleObserver
import org.mozilla.fenix.nimbus.FxNimbus
import org.mozilla.fenix.onboarding.MARKETING_CHANNEL_ID
import org.mozilla.fenix.perf.ApplicationExitInfoMetrics
import org.mozilla.fenix.perf.MarkersActivityLifecycleCallbacks
import org.mozilla.fenix.perf.ProfilerMarkerFactProcessor
import org.mozilla.fenix.perf.StartupTimeline
import org.mozilla.fenix.perf.StorageStatsMetrics
import org.mozilla.fenix.perf.runBlockingIncrement
import org.mozilla.fenix.push.PushFxaIntegration
import org.mozilla.fenix.push.WebPushEngineIntegration
import org.mozilla.fenix.session.VisibilityLifecycleCallback
import org.mozilla.fenix.settings.doh.DefaultDohSettingsProvider
import org.mozilla.fenix.settings.doh.DohSettingsProvider
import org.mozilla.fenix.startupCrash.StartupCrashActivity
import org.mozilla.fenix.theme.DefaultThemeProvider
import org.mozilla.fenix.theme.Theme
import org.mozilla.fenix.theme.Theme.Private
import org.mozilla.fenix.theme.ThemeProvider
import org.mozilla.fenix.utils.Settings
import org.mozilla.fenix.utils.isLargeScreenSize
import org.mozilla.fenix.wallpapers.Wallpaper
import java.util.Date
import java.util.concurrent.TimeUnit
import kotlin.math.roundToLong
import mozilla.components.support.AppServicesInitializer.Config as AppServicesConfig

private const val RAM_THRESHOLD_MEGABYTES = 1024
private const val BYTES_TO_MEGABYTES_CONVERSION = 1024.0 * 1024.0

/**
 * The main application class for Fenix. Records data to measure initialization performance.
 * Installs [CrashReporter], initializes [Glean] in fenix builds and setup [Megazord] in the main process.
 */
@Suppress("Registered", "TooManyFunctions", "LargeClass")
open class FenixApplication : LocaleAwareApplication(), Provider, ThemeProvider {
    init {
        // [TIMER] Record startup timestamp as early as reasonable with some degree of consistency.
        //
        // Measured after:
        //  - Static class initializers
        //  - Kotlin companion-object-init blocks
        //
        // but before:
        //  - ContentProvider initialization
        //  - Application.onCreate
        //
        StartupTimeline.onApplicationInit()
    }

    private val logger = Logger("FenixApplication")

    open val components by lazy { Components(this) }

    var visibilityLifecycleCallback: VisibilityLifecycleCallback? = null
        private set

    override fun onCreate() {
        super.onCreate()
        initializeFenixProcess()
    }

    /**
     * Process-level initialization for Fenix and its services. Sets up required native subsystems
     * such as Nimbus, Glean and Gecko. Note that Robolectric tests override this with an empty
     * implementation that skips this initialization.
     */
    @SuppressLint("NewApi")
    protected open fun initializeFenixProcess() {
        // [TIMER] Record the start of the [PerfStartup.applicationOnCreate] metric here. Do this
        // manually because Glean has not started initializing yet. Note that by this point the
        // content providers from Fenix and its libraries have run their initializers already.
        val start = SystemClock.elapsedRealtimeNanos()

        // Capture A-C logs to Android logcat. Note that gecko maybe directly post to logcat
        // regardless of what we do here.
        Log.addSink(FenixLogSink(logsDebug = Config.channel.isDebug, AndroidLogSink()))

        // Register a deferred initializer for crash reporting that will be called lazily when
        // CrashReporter.requireInstance is first accessed. This allows all processes to register
        // the initializer without immediately constructing the Components object and its dependencies.
        // Non-main processes genera

        // Some of our non-main processes are for CrashReporter business and need to know our
        // configuration from Fenix (the Analytics object). To avoid the Gecko processes that don't
        // need this from initializing the Components/Objects/CrashReporter we register a lazy
        // initializer so only processes that need it setup this Fenix code.
        //
        // Note: This doesn't setup any [UncaughtExceptionHandler].
        // Note: Gecko processes have their own crash handling mechanisms.
        CrashReporter.registerDeferredInitializer(::setupCrashReporting)

        // While this [initializeFenixProcess] method is run for _all processes_ in the app, we only
        // do global initialization for the _main process_ here. This main process initialization
        // includes setting up native libraries and global 'components' instances.
        //
        // Note: If a crash happens during this initialization (before visual completeness), then
        //       the [StartupCrashActivity] may be launched in a separate process. See the README.md
        //       in fenix/startupCrash for more information.
        //
        // Note: Gecko service processes don't use Nimbus or the Kotlin components. They also do
        //       their own loading of Gecko libraries.
        //
        // Note: The A-C / Fenix crash service processes are responsible for their own setup and
        //       should minimize their dependencies to avoid also crashing.
        runOnlyInMainProcess {
            // Initialization is split into two phases based on if libmegazord is fully initialized.
            setupEarlyMain()
            setupPostMegazord()

            // [TIMER] Record the end of the `PerfStartup.applicationOnCreate` metric. Note that
            // glean will queue this if the backend is still starting up.
            val stop = SystemClock.elapsedRealtimeNanos()
            val durationMillis = TimeUnit.NANOSECONDS.toMillis(stop - start)
            PerfStartup.applicationOnCreate.accumulateSamples(listOf(durationMillis))
        }
    }

    // Begin initialization of Glean if we have data-upload consent, otherwise we will have to
    // wait until we do. Note that Glean initialization is asynchronous any may not be finished
    // when this method returns.
    @OptIn(DelicateCoroutinesApi::class) // GlobalScope usage
    private fun maybeInitializeGlean() {
        // We delay the Glean initialization until we have user consent from onboarding.
        // If onboarding is disabled (when in local builds), continue to initialize Glean.
        if (components.fenixOnboarding.userHasBeenOnboarded() || !FeatureFlags.onboardingFeatureEnabled) {
            initializeGlean(this, logger, settings().isTelemetryEnabled, components.core.client)
        }
    }

    /**
     * This phase of main-process initialization runs before application-services is fully setup
     * so care must be taken. This phases begins loading the Nimbus, Glean, Gecko libraries.
     *
     * By the end of this, application-services, Nimbus and Gecko are initialized. Glean may or may
     * not be initialized.
     */
    private fun setupEarlyMain() {
        // ⚠️ The sequence of CrashReporter / Nimbus / Engine / Glean is particularly subtle due to
        // interdependencies among them.
        //
        // - We want the CrashReporter as soon as reasonable to give the best visibility. Note that
        //   CrashReporter records Nimbus experiment list when a crash happens so it has a lazy
        //   dependency on Nimbus.
        //
        // - Nimbus should be initialized quite early to ensure consistent experiment values are
        //   applied. In particular, we want to do it before Engine so that we have the right values
        //   before pages load. See: https://github.com/mozilla-mobile/fenix/issues/26320
        //
        // - Glean will queue (most) messages before being started so it is safe for Nimbus to begin
        //   before Glean does and any metrics will be processed once Glean is ready.

        // Setup the crash reporter and register the [UncaughtExceptionHandler].
        setupCrashReporting()

        // Begin application-services initialization. The megazord contains Nimbus, but not Glean.
        setupMegazordInitial()

        // Initialize Nimbus and its backend.
        initializeNimbus()

        ProfilerMarkerFactProcessor.create { components.core.engine.profiler }.register()

        // Ensure the Engine instance is initialized such that it can receive commands. Note
        // that full initialization is typically running off-thread and it may be a while
        // before pages can begin to render.
        components.core.engine.warmUp()

        // Kick off initialization of Glean backend off-thread. Glean will continue to queue
        // metric samples until the backend is ready. If we don't have data-upload consent then
        // this will be a no-op and initialization may be attempted after onboarding.
        maybeInitializeGlean()

        // Initialize the [BrowserStore] so that [setStartupMetrics] can reference this.
        // Note: This is a historical artifact and should be revisited.
        val store = components.core.store

        // StartupMetrics accesses shared preferences so do this off thread.
        @OptIn(DelicateCoroutinesApi::class)
        GlobalScope.launch(IO) {
            setStartupMetrics(store, settings())
        }

        // Start setup for concept-fetch networking in megazord. This runs off-thread, but we wait
        // before for its completion synchronously.
        val megazordDeferred = setupMegazordNetwork()

        setDayNightTheme()
        components.strictMode.enableStrictMode(true)
        warmBrowsersCache()

        initializeWebExtensionSupport()

        // Make sure to call this function before registering a storage worker
        // (e.g. components.core.historyStorage.registerStorageMaintenanceWorker())
        // as the storage maintenance worker needs a places storage globally when
        // it is needed while the app is not running and WorkManager wakes up the app
        // for the periodic task.
        GlobalPlacesDependencyProvider.initialize(components.core.historyStorage)
        GlobalLoginsDependencyProvider.initialize(lazy { components.core.passwordsStorage })
        GlobalAutofillDependencyProvider.initialize(lazy { components.core.autofillStorage })

        GlobalSyncedTabsCommandsProvider.initialize(lazy { components.backgroundServices.syncedTabsCommands })

        initializeRemoteSettingsSupport()

        restoreBrowserState()
        restoreDownloads()
        restoreMessaging()

        // [IMPORTANT] Don't progress further until application-services is actually ready to go.
        // This makes it easier to reason about behaviour and avoids issues in the Rust code.
        runBlockingIncrement { megazordDeferred.await() }
    }

    /**
     * The remainder of main-process initialization happens here now that we have ensured the
     * application-services initialization is completed. This also queues a bunch of follow-up
     * work to the visualCompletenessQueue that will be run after the Activity has started
     * rendering.
     */
    private fun setupPostMegazord() {
        setupLeakCanary()

        if (components.fenixOnboarding.userHasBeenOnboarded()) {
            startMetricsIfEnabled(
                logger = logger,
                analytics = components.analytics,
                isTelemetryEnabled = settings().isTelemetryEnabled,
                isMarketingTelemetryEnabled = settings().isMarketingTelemetryEnabled &&
                    settings().hasMadeMarketingTelemetrySelection,
                isDailyUsagePingEnabled = settings().isDailyUsagePingEnabled,
            )
        } else {
            CoroutineScope(IO).launch {
                components.distributionIdManager.startAdjustIfSkippingConsentScreen()
            }
        }

        setupPush()

        GlobalFxSuggestDependencyProvider.initialize(components.fxSuggest.storage)

        visibilityLifecycleCallback = VisibilityLifecycleCallback(getSystemService())
        registerActivityLifecycleCallbacks(visibilityLifecycleCallback)
        registerActivityLifecycleCallbacks(MarkersActivityLifecycleCallbacks(components.core.engine))

        components.appStartReasonProvider.registerInAppOnCreate(this)
        components.startupActivityLog.registerInAppOnCreate(this)
        components.appLinkIntentLaunchTypeProvider.registerInAppOnCreate(this)

        initVisualCompletenessQueueAndQueueTasks()

        ProcessLifecycleOwner.get().lifecycle.addObservers(
            StoreLifecycleObserver(
                appStore = components.appStore,
                browserStore = components.core.store,
            ),
            VisibilityLifecycleObserver(),
        )

        components.analytics.metricsStorage.tryRegisterAsUsageRecorder(this)

        CoroutineScope(IO).launch {
            components.useCases.wallpaperUseCases.fetchCurrentWallpaperUseCase.invoke()
        }
    }

    @OptIn(DelicateCoroutinesApi::class) // GlobalScope usage
    private fun restoreBrowserState() = GlobalScope.launch(Dispatchers.Main) {
        val store = components.core.store
        val sessionStorage = components.core.sessionStorage

        components.useCases.tabsUseCases.restore(sessionStorage, settings().getTabTimeout())

        // Now that we have restored our previous state (if there's one) let's setup auto saving the state while
        // the app is used.
        sessionStorage.autoSave(store)
            .periodicallyInForeground(interval = 30, unit = TimeUnit.SECONDS)
            .whenGoingToBackground()
            .whenSessionsChange()
    }

    private fun restoreDownloads() {
        components.useCases.downloadUseCases.restoreDownloads()
    }

    private fun initVisualCompletenessQueueAndQueueTasks() {
        val queue = components.performance.visualCompletenessQueue

        @OptIn(DelicateCoroutinesApi::class) // GlobalScope usage
        fun queueInitStorageAndServices() {
            queue.runIfReadyOrQueue {
                GlobalScope.launch(IO) {
                    logger.info("Running post-visual completeness tasks...")
                    logElapsedTime(logger, "Storage initialization") {
                        components.core.historyStorage.warmUp()
                        components.core.bookmarksStorage.warmUp()
                        components.core.passwordsStorage.warmUp()
                        components.core.autofillStorage.warmUp()

                        // Populate the top site cache to improve initial load experience
                        // of the home fragment when the app is launched to a tab. The actual
                        // database call is not expensive. However, the additional context
                        // switches delay rendering top sites when the cache is empty, which
                        // we can prevent with this.
                        components.core.topSitesStorage.getTopSites(
                            totalSites = components.settings.topSitesMaxLimit,
                            frecencyConfig = if (FxNimbus.features.homepageHideFrecentTopSites.value().enabled) {
                                null
                            } else {
                                TopSitesFrecencyConfig(
                                    frecencyTresholdOption = FrecencyThresholdOption.SKIP_ONE_TIME_PAGES,
                                ) {
                                    !it.url.toUri()
                                        .containsQueryParameters(components.settings.frecencyFilterQuery)
                                }
                            },
                            providerConfig = TopSitesProviderConfig(
                                showProviderTopSites = components.settings.showContileFeature,
                                limit = TOP_SITES_PROVIDER_LIMIT,
                                maxThreshold = TOP_SITES_PROVIDER_MAX_THRESHOLD,
                            ),
                        )

                        // This service uses `historyStorage`, and so we can only touch it when we know
                        // it's safe to touch `historyStorage. By 'safe', we mainly mean that underlying
                        // places library will be able to load, which requires first running Megazord.init().
                        // The visual completeness tasks are scheduled after the Megazord.init() call.
                        components.core.historyMetadataService.cleanup(
                            System.currentTimeMillis() - Core.HISTORY_METADATA_MAX_AGE_IN_MS,
                        )

                        // If Firefox Suggest is enabled, register a worker to periodically ingest
                        // new search suggestions. The worker requires us to have called
                        // `GlobalFxSuggestDependencyProvider.initialize`, which we did before
                        // scheduling these tasks. When disabled we stop the periodic work.
                        if (settings().enableFxSuggest) {
                            components.fxSuggest.ingestionScheduler.startPeriodicIngestion()
                        } else {
                            components.fxSuggest.ingestionScheduler.stopPeriodicIngestion()
                        }
                    }
                    components.core.fileUploadsDirCleaner.cleanUploadsDirectory()
                }
                // Account manager initialization needs to happen on the main thread.
                GlobalScope.launch(Dispatchers.Main) {
                    logElapsedTime(logger, "Kicking-off account manager") {
                        components.backgroundServices.accountManager
                    }
                }
            }
        }

        fun queueMetrics() {
            queue.runIfReadyOrQueue {
                // Because it may be slow to capture the storage stats, it might be preferred to
                // create a WorkManager task for this metric, however, I ran out of
                // implementation time and WorkManager is harder to test.
                StorageStatsMetrics.report(this.applicationContext)
            }
        }

        @OptIn(DelicateCoroutinesApi::class) // GlobalScope usage
        fun queueIncrementNumberOfAppLaunches() {
            queue.runIfReadyOrQueue {
                GlobalScope.launch(IO) {
                    settings().numberOfAppLaunches += 1
                }
            }
        }

        @OptIn(DelicateCoroutinesApi::class) // GlobalScope usage
        fun queueRestoreLocale() {
            queue.runIfReadyOrQueue {
                GlobalScope.launch(IO) {
                    components.useCases.localeUseCases.restore()
                }
            }
        }

        fun queueStorageMaintenance() {
            queue.runIfReadyOrQueue {
                // Make sure GlobalPlacesDependencyProvider.initialize(components.core.historyStorage)
                // is called before this call. When app is not running and WorkManager wakes up
                // the app for the periodic task, it will require a globally provided places storage
                // to run the maintenance on.
                components.core.historyStorage.registerStorageMaintenanceWorker()
                components.core.passwordsStorage.registerStorageMaintenanceWorker()
                components.core.autofillStorage.registerStorageMaintenanceWorker()
            }
        }

        @OptIn(DelicateCoroutinesApi::class) // GlobalScope usage
        fun queueIntegrityClientWarmUp() {
            queue.runIfReadyOrQueue {
                GlobalScope.launch(IO) {
                    components.integrityClient.warmUp()
                }
            }
        }

        @OptIn(DelicateCoroutinesApi::class) // GlobalScope usage
        fun queueNimbusFetchInForeground() {
            queue.runIfReadyOrQueue {
                GlobalScope.launch(IO) {
                    components.nimbus.sdk.maybeFetchExperiments(
                        context = this@FenixApplication,
                    )
                }
            }
        }

        @OptIn(DelicateCoroutinesApi::class) // GlobalScope usage
        fun queueSuggestIngest() {
            queue.runIfReadyOrQueue {
                GlobalScope.launch(IO) {
                    components.fxSuggest.storage.runStartupIngestion()
                }
            }
        }

        fun queueDownloadWallpapers() {
            queue.runIfReadyOrQueue {
                downloadWallpapers()
            }
        }

        // We init these items in the visual completeness queue to avoid them initing in the critical
        // startup path, before the UI finishes drawing (i.e. visual completeness).
        queueInitStorageAndServices()
        queueMetrics()
        queueIncrementNumberOfAppLaunches()
        queueRestoreLocale()
        queueStorageMaintenance()
        queueIntegrityClientWarmUp()
        queueNimbusFetchInForeground()
        queueDownloadWallpapers()
        if (settings().enableFxSuggest) {
            queueSuggestIngest()
        }
        queueCollectProcessExitInfo()
    }

    @OptIn(DelicateCoroutinesApi::class) // GlobalScope usage
    private fun queueCollectProcessExitInfo() {
        if (SDK_INT >= Build.VERSION_CODES.R && settings().isTelemetryEnabled) {
            GlobalScope.launch(IO) {
                ApplicationExitInfoMetrics.recordProcessExits(applicationContext)
            }
        }
    }

    protected open fun setupLeakCanary() {
        // no-op, LeakCanary is disabled by default
    }

    open fun updateLeakCanaryState(isEnabled: Boolean) {
        // no-op, LeakCanary is disabled by default
    }

    private fun setupPush() {
        // Sets the PushFeature as the singleton instance for push messages to go to.
        // We need the push feature setup here to deliver messages in the case where the service
        // starts up the app first.
        components.push.feature?.let {
            logger.info("AutoPushFeature is configured, initializing it...")

            // Install the AutoPush singleton to receive messages.
            PushProcessor.install(it)

            WebPushEngineIntegration(components.core.engine, it).start()

            // Perform a one-time initialization of the account manager if a message is received.
            PushFxaIntegration(it, lazy { components.backgroundServices.accountManager }).launch()

            // Initialize the service. This could potentially be done in a coroutine in the future.
            it.initialize()
        }
    }

    private fun setupCrashReporting(): CrashReporter {
        return components
            .analytics
            .crashReporter
            .install(this, ::handleCaughtException)
    }

    private fun handleCaughtException() {
        if (
            isMainProcess() &&
            components.settings.useNewCrashReporterFlow &&
            !components.performance.visualCompletenessQueue.isReady()
        ) {
            val intent = Intent(applicationContext, StartupCrashActivity::class.java)

            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            applicationContext.startActivity(intent)
        }
    }

    private fun initializeNimbus() {
        // This lazily constructs the Nimbus object…
        val nimbus = components.nimbus.sdk
        // … which we then can populate the feature configuration.
        FxNimbus.initialize { nimbus }
    }

    /**
     * Initiate Megazord sequence! Megazord Battle Mode!
     *
     * The application-services combined libraries are known as the "megazord". We use the default `full`
     * megazord - it contains everything that fenix needs, and (currently) nothing more.
     *
     * Documentation on what megazords are, and why they're needed:
     * - https://github.com/mozilla/application-services/blob/main/docs/design/megazords.md
     * - https://mozilla.github.io/application-services/book/design/megazords.html
     *
     * This is the initialization of the megazord without setting up networking, i.e. needing the
     * engine for networking. This should do the minimum work necessary as it is done on the main
     * thread, early in the app startup sequence.
     */
    private fun setupMegazordInitial() {
        // Rust components must be initialized at the very beginning, before any other Rust call, ...
        AppServicesInitializer.init(
            AppServicesConfig(components.analytics.crashReporter),
        )
    }

    @OptIn(DelicateCoroutinesApi::class) // GlobalScope usage
    private fun setupMegazordNetwork(): Deferred<Unit> {
        return GlobalScope.async(IO) {
            if (Config.channel.isDebug) {
                RustHttpConfig.allowEmulatorLoopback()
            }
            RustHttpConfig.setClient(lazy { components.core.client })
        }
    }

    @VisibleForTesting
    internal fun restoreMessaging() {
        if (settings().isExperimentationEnabled) {
            components.appStore.dispatch(AppAction.MessagingAction.Restore)
        }
    }

    @SuppressLint("NewApi")
    override fun onTrimMemory(level: Int) {
        super.onTrimMemory(level)

        // Additional logging and breadcrumb to debug memory issues:
        // https://github.com/mozilla-mobile/fenix/issues/12731

        logger.info("onTrimMemory(), level=$level, main=${isMainProcess()}")

        runOnlyInMainProcess {
            components.analytics.crashReporter.recordCrashBreadcrumb(
                Breadcrumb(
                    category = "Memory",
                    message = "onTrimMemory()",
                    data = mapOf(
                        "level" to level.toString(),
                        "main" to isMainProcess().toString(),
                    ),
                    level = Breadcrumb.Level.INFO,
                ),
            )

            components.core.icons.onTrimMemory(level)
            components.core.store.dispatch(SystemAction.LowMemoryAction(level))
        }
    }

    private fun setDayNightTheme() {
        val settings = this.settings()
        when {
            settings.shouldUseLightTheme -> {
                AppCompatDelegate.setDefaultNightMode(
                    AppCompatDelegate.MODE_NIGHT_NO,
                )
            }
            settings.shouldUseDarkTheme -> {
                AppCompatDelegate.setDefaultNightMode(
                    AppCompatDelegate.MODE_NIGHT_YES,
                )
            }
            SDK_INT < Build.VERSION_CODES.P && settings.shouldUseAutoBatteryTheme -> {
                AppCompatDelegate.setDefaultNightMode(
                    AppCompatDelegate.MODE_NIGHT_AUTO_BATTERY,
                )
            }
            SDK_INT >= Build.VERSION_CODES.P && settings.shouldFollowDeviceTheme -> {
                AppCompatDelegate.setDefaultNightMode(
                    AppCompatDelegate.MODE_NIGHT_FOLLOW_SYSTEM,
                )
            }
            // First run of app no default set, set the default to Follow System for 28+ and Normal Mode otherwise
            else -> {
                if (SDK_INT >= Build.VERSION_CODES.P) {
                    AppCompatDelegate.setDefaultNightMode(
                        AppCompatDelegate.MODE_NIGHT_FOLLOW_SYSTEM,
                    )
                    settings.shouldFollowDeviceTheme = true
                } else {
                    AppCompatDelegate.setDefaultNightMode(
                        AppCompatDelegate.MODE_NIGHT_NO,
                    )
                    settings.shouldUseLightTheme = true
                }
            }
        }
    }

    @OptIn(DelicateCoroutinesApi::class) // GlobalScope usage
    private fun warmBrowsersCache() {
        // We avoid blocking the main thread for BrowsersCache on startup by loading it on
        // background thread.
        GlobalScope.launch(Dispatchers.Default) {
            BrowsersCache.all(this@FenixApplication)
        }
    }

    private fun initializeRemoteSettingsSupport() {
        GlobalRemoteSettingsDependencyProvider.initialize(components.remoteSettingsService.value)
        components.remoteSettingsSyncScheduler.registerForSync()
    }

    @Suppress("ForbiddenComment")
    private fun initializeWebExtensionSupport() {
        try {
            GlobalAddonDependencyProvider.initialize(
                components.addonManager,
                components.addonUpdater,
                onCrash = { exception ->
                    components.analytics.crashReporter.submitCaughtException(exception)
                },
            )
            WebExtensionSupport.initialize(
                components.core.engine,
                components.core.store,
                onNewTabOverride = { _, engineSession, url ->
                    val shouldCreatePrivateSession =
                        components.core.store.state.selectedTab?.content?.private
                            ?: components.settings.openLinksInAPrivateTab

                    components.useCases.tabsUseCases.addTab(
                        url = url,
                        selectTab = true,
                        engineSession = engineSession,
                        private = shouldCreatePrivateSession,
                    )
                },
                onCloseTabOverride = { _, sessionId ->
                    components.useCases.tabsUseCases.removeTab(sessionId)
                },
                onSelectTabOverride = { _, sessionId ->
                    components.useCases.tabsUseCases.selectTab(sessionId)
                },
                onExtensionsLoaded = { extensions ->
                    components.addonUpdater.registerForFutureUpdates(extensions)
                    subscribeForNewAddonsIfNeeded(components.supportedAddonsChecker, extensions)

                    // Bug 1948634 - Make sure the webcompat-reporter extension is fully uninstalled.
                    // This is added here because we need gecko to load the extension first.
                    //
                    // TODO: Bug 1953359 - remove the code below in the next release.
                    if (Config.channel.isNightlyOrDebug || Config.channel.isBeta) {
                        logger.debug("Attempting to uninstall the WebCompat Reporter extension")
                        WebCompatReporterFeature.uninstall(components.core.engine)
                    }
                },
                onUpdatePermissionRequest = components.addonUpdater::onUpdatePermissionRequest,
            )
        } catch (e: UnsupportedOperationException) {
            logger.error("Failed to initialize web extension support", e)
        }
    }

    @VisibleForTesting
    internal fun subscribeForNewAddonsIfNeeded(
        checker: DefaultSupportedAddonsChecker,
        installedExtensions: List<WebExtension>,
    ) {
        val hasUnsupportedAddons = installedExtensions.any { it.isUnsupported() }
        if (hasUnsupportedAddons) {
            checker.registerForChecks()
        } else {
            // As checks are a persistent subscriptions, we have to make sure
            // we remove any previous subscriptions.
            checker.unregisterForChecks()
        }
    }

    /**
     * This function is called right after Glean is initialized. Part of this function depends on
     * shared preferences to be updated so the correct value is sent with the metrics ping.
     *
     * The reason we're using shared preferences to track these values is due to the limitations of
     * the current metrics ping design. The values set here will be sent in every metrics ping even
     * if these values have not changed since the last startup.
     */
    @Suppress("CognitiveComplexMethod", "LongMethod", "CyclomaticComplexMethod")
    @VisibleForTesting
    internal suspend fun setStartupMetrics(
        browserStore: BrowserStore,
        settings: Settings,
        dohSettingsProvider: DohSettingsProvider = DefaultDohSettingsProvider(
            components.core.engine,
            settings,
        ),
        browsersCache: BrowsersCache = BrowsersCache,
        mozillaProductDetector: MozillaProductDetector = MozillaProductDetector,
    ) {
        setPreferenceMetrics(settings, dohSettingsProvider)
        with(Metrics) {
            // Set this early to guarantee it's in every ping from here on.
            distributionId.set(components.distributionIdManager.getDistributionId())

            if (settings.hasAcceptedTermsOfService) {
                setTermsOfUseStartUpMetrics(settings)
            }

            defaultBrowser.set(browsersCache.all(applicationContext).isDefaultBrowser)
            mozillaProductDetector.getMozillaBrowserDefault(applicationContext)?.also {
                defaultMozBrowser.set(it)
            }

            mozillaProducts.set(
                mozillaProductDetector.getInstalledMozillaProducts(
                    applicationContext,
                ),
            )

            adjustCampaign.set(settings.adjustCampaignId)
            adjustAdGroup.set(settings.adjustAdGroup)
            adjustCreative.set(settings.adjustCreative)
            adjustNetwork.set(settings.adjustNetwork)

            settings.migrateSearchWidgetInstalledPrefIfNeeded()
            searchWidgetInstalled.set(settings.searchWidgetInstalled)

            val openTabsCount = settings.openTabsCount
            hasOpenTabs.set(openTabsCount > 0)
            if (openTabsCount > 0) {
                tabsOpenCount.add(openTabsCount)
            }

            val openPrivateTabsCount = settings.openPrivateTabsCount
            if (openPrivateTabsCount > 0) {
                privateTabsOpenCount.add(openPrivateTabsCount)
            }

            val topSitesSize = settings.topSitesSize
            hasTopSites.set(topSitesSize > 0)
            if (topSitesSize > 0) {
                topSitesCount.add(topSitesSize)
            }

            val installedAddonSize = settings.installedAddonsCount
            Addons.hasInstalledAddons.set(installedAddonSize > 0)
            if (installedAddonSize > 0) {
                Addons.installedAddons.set(settings.installedAddonsList.split(','))
            }

            val enabledAddonSize = settings.enabledAddonsCount
            Addons.hasEnabledAddons.set(enabledAddonSize > 0)
            if (enabledAddonSize > 0) {
                Addons.enabledAddons.set(settings.enabledAddonsList.split(','))
            }

            val desktopBookmarksSize = settings.desktopBookmarksSize
            hasDesktopBookmarks.set(desktopBookmarksSize > 0)
            if (desktopBookmarksSize > 0) {
                desktopBookmarksCount.add(desktopBookmarksSize)
            }

            val mobileBookmarksSize = settings.mobileBookmarksSize
            hasMobileBookmarks.set(mobileBookmarksSize > 0)
            if (mobileBookmarksSize > 0) {
                mobileBookmarksCount.add(mobileBookmarksSize)
            }

            tabViewSetting.set(settings.getTabViewPingString())
            closeTabSetting.set(settings.getTabTimeoutPingString())

            val isDefaultTheCurrentWallpaper =
                Wallpaper.nameIsDefault(settings.currentWallpaperName)

            defaultWallpaper.set(isDefaultTheCurrentWallpaper)

            val notificationManagerCompat = NotificationManagerCompat.from(applicationContext)
            notificationsAllowed.set(notificationManagerCompat.areNotificationsEnabledSafe())
            marketingNotificationAllowed.set(
                notificationManagerCompat.isNotificationChannelEnabled(MARKETING_CHANNEL_ID),
            )

            ramMoreThanThreshold.set(isDeviceRamAboveThreshold)
            deviceTotalRam.set(getDeviceTotalRAM())

            isLargeDevice.set(isLargeScreenSize())
        }

        with(AndroidAutofill) {
            val autofillUseCases = AutofillUseCases()
            supported.set(autofillUseCases.isSupported(applicationContext))
            enabled.set(autofillUseCases.isEnabled(applicationContext))
        }

        browserStore.waitForSelectedOrDefaultSearchEngine { searchEngine ->
            searchEngine?.let {
                val sendSearchUrl =
                    !searchEngine.isCustomEngine() || searchEngine.isKnownSearchDomain()
                if (sendSearchUrl) {
                    SearchDefaultEngine.apply {
                        code.set(
                            if (searchEngine.telemetrySuffix.isNullOrEmpty()) {
                                searchEngine.id
                            } else {
                                "${searchEngine.id}-${searchEngine.telemetrySuffix}"
                            },
                        )
                        name.set(searchEngine.name)
                        searchUrl.set(searchEngine.buildSearchUrl(""))
                    }
                } else {
                    SearchDefaultEngine.apply {
                        code.set(searchEngine.id)
                        name.set("custom")
                    }
                }
            }
        }

        setAutofillMetrics()
    }

    private fun setTermsOfUseStartUpMetrics(settings: Settings) {
        TermsOfUse.version.set(settings.termsOfUseAcceptedVersion.toLong())
        TermsOfUse.date.set(Date(settings.termsOfUseAcceptedTimeInMillis))
    }

    @VisibleForTesting
    internal fun getDeviceTotalRAM(): Long {
        val memoryInfo = getMemoryInfo()
        return if (SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
            memoryInfo.advertisedMem
        } else {
            memoryInfo.totalMem
        }
    }

    @VisibleForTesting
    internal fun getMemoryInfo(): ActivityManager.MemoryInfo {
        val memoryInfo = ActivityManager.MemoryInfo()
        val activityManager = getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager
        activityManager.getMemoryInfo(memoryInfo)

        return memoryInfo
    }

    private fun deviceRamApproxMegabytes(): Long {
        val deviceRamBytes = getMemoryInfo().totalMem
        return deviceRamBytes.toRoundedMegabytes()
    }

    private fun Long.toRoundedMegabytes(): Long = (this / BYTES_TO_MEGABYTES_CONVERSION).roundToLong()

    internal val isDeviceRamAboveThreshold by lazy {
        deviceRamApproxMegabytes() > RAM_THRESHOLD_MEGABYTES
    }

    @Suppress("CyclomaticComplexMethod")
    private fun setPreferenceMetrics(
        settings: Settings,
        dohSettingsProvider: DohSettingsProvider,
    ) {
        with(Preferences) {
            searchSuggestionsEnabled.set(settings.shouldShowSearchSuggestions)
            showSponsorSuggestionsEnabled.set(settings.showSponsoredSuggestions)
            showNonSponsorSuggestionsEnabled.set(settings.showNonSponsoredSuggestions)
            remoteDebuggingEnabled.set(settings.isRemoteDebuggingEnabled)
            studiesEnabled.set(settings.isExperimentationEnabled)
            telemetryEnabled.set(settings.isTelemetryEnabled)
            browsingHistorySuggestion.set(settings.shouldShowHistorySuggestions)
            bookmarksSuggestion.set(settings.shouldShowBookmarkSuggestions)
            clipboardSuggestionsEnabled.set(settings.shouldShowClipboardSuggestions)
            searchShortcutsEnabled.set(settings.shouldShowSearchShortcuts)
            voiceSearchEnabled.set(settings.shouldShowVoiceSearch)
            openLinksInAppEnabled.set(settings.openLinksInExternalApp)
            signedInSync.set(settings.signedInFxaAccount)
            isolatedContentProcessesEnabled.set(settings.isIsolatedProcessEnabled)
            appZygoteIsolatedContentProcessesEnabled.set(settings.isAppZygoteEnabled)
            TabStrip.enabled.set(settings.isTabStripEnabled)

            val syncedItems = SyncEnginesStorage(applicationContext).getStatus().entries.filter {
                it.value
            }.map { it.key.nativeName }
            syncItems.set(syncedItems)

            toolbarPositionSetting.set(
                when {
                    settings.shouldUseFixedTopToolbar -> "fixed_top"
                    settings.shouldUseBottomToolbar -> "bottom"
                    else -> "top"
                },
            )

            toolbarModeSetting.set(
                when {
                    settings.shouldUseExpandedToolbar -> "expanded"
                    else -> "simple"
                },
            )

            if (settings.shouldShowToolbarCustomization) {
                toolbarSimpleShortcut.set(settings.toolbarSimpleShortcut)
                toolbarExpandedShortcut.set(settings.toolbarExpandedShortcut)
            }

            enhancedTrackingProtection.set(
                when {
                    !settings.shouldUseTrackingProtection -> ""
                    settings.useStandardTrackingProtection -> "standard"
                    settings.useStrictTrackingProtection -> "strict"
                    settings.useCustomTrackingProtection -> "custom"
                    else -> ""
                },
            )
            etpCustomCookiesSelection.set(settings.blockCookiesSelectionInCustomTrackingProtection)

            val accessibilitySelection = mutableListOf<String>()

            if (settings.switchServiceIsEnabled) {
                accessibilitySelection.add("switch")
            }

            if (settings.touchExplorationIsEnabled) {
                accessibilitySelection.add("touch exploration")
            }

            accessibilityServices.set(accessibilitySelection.toList())

            userTheme.set(
                when {
                    settings.shouldUseLightTheme -> "light"
                    settings.shouldUseDarkTheme -> "dark"
                    settings.shouldFollowDeviceTheme -> "system"
                    settings.shouldUseAutoBatteryTheme -> "battery"
                    else -> ""
                },
            )

            inactiveTabsEnabled.set(settings.inactiveTabsAreEnabled)
            dohProtectionLevel.set(dohSettingsProvider.getSelectedProtectionLevel().toString())
            httpsOnlyMode.set(settings.getHttpsOnlyMode().toString())
            globalPrivacyControlEnabled.set(settings.shouldEnableGlobalPrivacyControl)
        }
        reportHomeScreenMetrics(settings)
    }

    private fun setAutofillMetrics() {
        @OptIn(DelicateCoroutinesApi::class)
        GlobalScope.launch(IO) {
            try {
                val autoFillStorage = applicationContext.components.core.autofillStorage
                Addresses.savedAll.set(autoFillStorage.getAllAddresses().size.toLong())
                CreditCards.savedAll.set(autoFillStorage.getAllCreditCards().size.toLong())
            } catch (e: AutofillApiException) {
                logger.error("Failed to fetch autofill data", e)
            }

            try {
                val passwordsStorage = applicationContext.components.core.passwordsStorage
                Logins.savedAll.set(passwordsStorage.list().size.toLong())
            } catch (e: LoginsApiException) {
                logger.error("Failed to fetch list of logins", e)
            }
        }
    }

    @VisibleForTesting
    internal fun reportHomeScreenMetrics(settings: Settings) {
        reportOpeningScreenMetrics(settings)
        reportHomeScreenSectionMetrics(settings)
    }

    private fun reportOpeningScreenMetrics(settings: Settings) {
        CustomizeHome.openingScreen.set(
            when {
                settings.alwaysOpenTheHomepageWhenOpeningTheApp -> "homepage"
                settings.alwaysOpenTheLastTabWhenOpeningTheApp -> "last tab"
                settings.openHomepageAfterFourHoursOfInactivity -> "homepage after four hours"
                else -> ""
            },
        )
    }

    private fun reportHomeScreenSectionMetrics(settings: Settings) {
        // These settings are backed by Nimbus features.
        // We break them out here so they can be recorded when
        // `nimbus.applyPendingExperiments()` is called.
        CustomizeHome.jumpBackIn.set(settings.showRecentTabsFeature)
        CustomizeHome.bookmarks.set(settings.showBookmarksHomeFeature)
        CustomizeHome.mostVisitedSites.set(settings.showTopSitesFeature)
        CustomizeHome.recentlyVisited.set(settings.historyMetadataUIFeature)
        CustomizeHome.pocket.set(settings.showPocketRecommendationsFeature)
        CustomizeHome.sponsoredPocket.set(settings.showPocketSponsoredStories)
        CustomizeHome.contile.set(settings.showContileFeature)
    }

    override fun onConfigurationChanged(config: android.content.res.Configuration) {
        // Workaround for androidx appcompat issue where follow system day/night mode config changes
        // are not triggered when also using createConfigurationContext like we do in LocaleManager
        // https://issuetracker.google.com/issues/143570309#comment3
        applicationContext.resources.configuration.uiMode = config.uiMode

        if (isMainProcess()) {
            // We can only do this on the main process as resetAfter will access components.core, which
            // will initialize the engine and create an additional GeckoRuntime from the Gecko
            // child process, causing a crash.

            // There's a strict mode violation in A-Cs LocaleAwareApplication which
            // reads from shared prefs: https://github.com/mozilla-mobile/android-components/issues/8816
            components.strictMode.allowViolation(StrictMode::allowThreadDiskReads) {
                super.onConfigurationChanged(config)
            }
        } else {
            super.onConfigurationChanged(config)
        }
    }

    override val workManagerConfiguration = Builder().setMinimumLoggingLevel(INFO).build()

    @OptIn(DelicateCoroutinesApi::class)
    open fun downloadWallpapers() {
        GlobalScope.launch {
            components.useCases.wallpaperUseCases.initialize()
        }
    }

    @Composable
    override fun provideTheme(): Theme {
        return if (components.appStore.state.mode.isPrivate) {
            Private
        } else {
            DefaultThemeProvider.provideTheme()
        }
    }
}
