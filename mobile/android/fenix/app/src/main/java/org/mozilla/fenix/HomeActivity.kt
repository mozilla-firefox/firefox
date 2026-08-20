/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix

import android.app.assist.AssistContent
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.Intent.ACTION_MAIN
import android.content.Intent.FLAG_ACTIVITY_REORDER_TO_FRONT
import android.content.pm.PackageManager
import android.content.res.Configuration
import android.os.Build
import android.os.Bundle
import android.os.StrictMode
import android.text.format.DateUtils
import android.util.AttributeSet
import android.view.ActionMode
import android.view.KeyEvent
import android.view.LayoutInflater
import android.view.MotionEvent
import android.view.View
import android.view.ViewConfiguration
import android.view.ViewGroup
import android.view.WindowManager.LayoutParams.FLAG_SECURE
import androidx.activity.BackEventCompat
import androidx.annotation.CallSuper
import androidx.annotation.IdRes
import androidx.annotation.VisibleForTesting
import androidx.appcompat.app.ActionBar
import androidx.appcompat.widget.Toolbar
import androidx.core.app.NotificationManagerCompat
import androidx.core.net.toUri
import androidx.core.splashscreen.SplashScreen.Companion.installSplashScreen
import androidx.core.text.layoutDirection
import androidx.core.view.doOnLayout
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.navigation.NavController
import androidx.navigation.NavDirections
import androidx.navigation.fragment.NavHostFragment
import androidx.navigation.ui.AppBarConfiguration
import androidx.navigation.ui.NavigationUI
import java.util.Locale
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Dispatchers.IO
import kotlinx.coroutines.Dispatchers.Main
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import mozilla.appservices.places.BookmarkRoot
import mozilla.components.browser.state.action.MediaSessionAction
import mozilla.components.browser.state.action.SearchAction
import mozilla.components.browser.state.action.WebExtensionAction
import mozilla.components.browser.state.search.SearchEngine
import mozilla.components.browser.state.selector.getNormalOrPrivateTabs
import mozilla.components.browser.state.selector.selectedTab
import mozilla.components.browser.state.state.ActiveOptionsPage
import mozilla.components.browser.state.state.WebExtensionState
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.EngineView
import mozilla.components.concept.storage.HistoryMetadataKey
import mozilla.components.feature.contextmenu.DefaultSelectionActionDelegate
import mozilla.components.feature.customtabs.isCustomTabIntent
import mozilla.components.feature.media.ext.findActiveMediaTab
import mozilla.components.feature.privatemode.notification.PrivateNotificationFeature
import mozilla.components.feature.search.BrowserStoreSearchAdapter
import mozilla.components.lib.crash.store.CrashAction
import mozilla.components.service.fxa.sync.SyncReason
import mozilla.components.support.base.feature.ActivityResultHandler
import mozilla.components.support.base.feature.UserInteractionHandler
import mozilla.components.support.base.feature.UserInteractionOnBackPressedCallback
import mozilla.components.support.ktx.android.arch.lifecycle.addObservers
import mozilla.components.support.ktx.android.content.call
import mozilla.components.support.ktx.android.content.email
import mozilla.components.support.ktx.android.content.share
import mozilla.components.support.locale.LocaleAwareAppCompatActivity
import mozilla.components.support.utils.BootUtils
import mozilla.components.support.utils.Browsers
import mozilla.components.support.utils.BrowsersCache
import mozilla.components.support.utils.BuildManufacturerChecker
import mozilla.components.support.utils.SafeIntent
import mozilla.components.support.utils.toSafeIntent
import mozilla.components.support.webextensions.WebExtensionOptionsPageObserver
import mozilla.components.support.webextensions.WebExtensionPopupObserver
import mozilla.components.ui.icons.R as iconsR
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.experiments.nimbus.initializeTooling
import org.mozilla.fenix.GleanMetrics.AppIcon
import org.mozilla.fenix.GleanMetrics.Events
import org.mozilla.fenix.GleanMetrics.Metrics
import org.mozilla.fenix.GleanMetrics.NativeShareSheet
import org.mozilla.fenix.GleanMetrics.SplashScreen
import org.mozilla.fenix.GleanMetrics.StartOnHome
import org.mozilla.fenix.addons.ExtensionsProcessDisabledBackgroundController
import org.mozilla.fenix.addons.ExtensionsProcessDisabledForegroundController
import org.mozilla.fenix.bindings.ExternalAppLinkStatusBinding
import org.mozilla.fenix.bindings.HomepageTabBinding
import org.mozilla.fenix.bindings.SummarizeToolbarHighlightBinding
import org.mozilla.fenix.bookmarks.DesktopFolders
import org.mozilla.fenix.browser.BrowserFragment
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.browser.browsingmode.BrowsingModeManager
import org.mozilla.fenix.browser.browsingmode.DefaultBrowsingModeManager
import org.mozilla.fenix.components.DefaultHomepageAsANewTabPreferenceRepository
import org.mozilla.fenix.components.DefaultShortcutManagerCompatWrapper
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.AppAction.ShareAction
import org.mozilla.fenix.components.appstate.OrientationMode
import org.mozilla.fenix.components.ipprotection.ErrorMessages
import org.mozilla.fenix.components.ipprotection.IPProtectionInfoPrompter
import org.mozilla.fenix.components.menu.MenuAccessPoint
import org.mozilla.fenix.components.menu.share.QRCodeDialogFragment
import org.mozilla.fenix.components.metrics.BreadcrumbsRecorder
import org.mozilla.fenix.components.metrics.GrowthDataWorker
import org.mozilla.fenix.components.metrics.InstallReferrerHandlingService
import org.mozilla.fenix.components.metrics.fonts.FontEnumerationWorker
import org.mozilla.fenix.components.share.QR_CODE_URI_KEY
import org.mozilla.fenix.components.share.SEND_TO_DEVICES_ACTION
import org.mozilla.fenix.components.share.SendToDevicesDialogFragment
import org.mozilla.fenix.crashes.CrashActionDispatcher
import org.mozilla.fenix.crashes.CrashReporterBinding
import org.mozilla.fenix.crashes.UnsubmittedCrashDialog
import org.mozilla.fenix.customtabs.ExternalAppBrowserActivity
import org.mozilla.fenix.databinding.ActivityHomeBinding
import org.mozilla.fenix.debugsettings.data.DefaultDebugSettingsRepository
import org.mozilla.fenix.debugsettings.gleandebugtools.DefaultGleanDebugToolsStorage
import org.mozilla.fenix.debugsettings.ui.FenixOverlay
import org.mozilla.fenix.downloads.DownloadSnackbar
import org.mozilla.fenix.e2e.EdgeToEdgeFragmentLifecycleCallbacks
import org.mozilla.fenix.experiments.ResearchSurfaceDialogFragment
import org.mozilla.fenix.experiments.UninstallSurveyManager
import org.mozilla.fenix.ext.alreadyOnDestination
import org.mozilla.fenix.ext.breadcrumb
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.getBreadcrumbMessage
import org.mozilla.fenix.ext.getIntentSessionId
import org.mozilla.fenix.ext.getIntentSource
import org.mozilla.fenix.ext.getNavDirections
import org.mozilla.fenix.ext.hasTopDestination
import org.mozilla.fenix.ext.isAllowedDuringOnboardingIntent
import org.mozilla.fenix.ext.nav
import org.mozilla.fenix.ext.openSetDefaultBrowserOption
import org.mozilla.fenix.ext.recordEventInNimbus
import org.mozilla.fenix.ext.setNavigationIcon
import org.mozilla.fenix.extension.WebExtensionPromptFeature
import org.mozilla.fenix.home.HomeFragment
import org.mozilla.fenix.home.TopSitesRefresher
import org.mozilla.fenix.home.intent.AssistIntentProcessor
import org.mozilla.fenix.home.intent.CrashReporterIntentProcessor
import org.mozilla.fenix.home.intent.HomeDeepLinkIntentProcessor
import org.mozilla.fenix.home.intent.OpenBrowserIntentProcessor
import org.mozilla.fenix.home.intent.OpenPasswordManagerIntentProcessor
import org.mozilla.fenix.home.intent.OpenRecentlyClosedIntentProcessor
import org.mozilla.fenix.home.intent.OpenSpecificTabIntentProcessor
import org.mozilla.fenix.home.intent.SpeechProcessingIntentProcessor
import org.mozilla.fenix.home.intent.StartSearchIntentProcessor
import org.mozilla.fenix.home.topsites.DefaultTopSitesBinding
import org.mozilla.fenix.messaging.FenixMessageSurfaceId
import org.mozilla.fenix.messaging.MessageNotificationWorker
import org.mozilla.fenix.nimbus.FxNimbus
import org.mozilla.fenix.onboarding.ensureMarketingChannelExists
import org.mozilla.fenix.onboarding.seedOnboardingCompletedTimestampForDebugIfNeeded
import org.mozilla.fenix.pbmlock.DefaultPrivateBrowsingLockStorage
import org.mozilla.fenix.pbmlock.PrivateBrowsingLockFeature
import org.mozilla.fenix.perf.DefaultStartupPathProvider
import org.mozilla.fenix.perf.MarkersActivityLifecycleCallbacks
import org.mozilla.fenix.perf.MarkersFragmentLifecycleCallbacks
import org.mozilla.fenix.perf.Performance
import org.mozilla.fenix.perf.PerformanceInflater
import org.mozilla.fenix.perf.ProfilerMarkers
import org.mozilla.fenix.perf.StartupPathProvider
import org.mozilla.fenix.perf.StartupTimeline
import org.mozilla.fenix.perf.StartupTypeTelemetry
import org.mozilla.fenix.session.PrivateNotificationService
import org.mozilla.fenix.settings.SupportUtils
import org.mozilla.fenix.shortcut.NewTabShortcutIntentProcessor.Companion.ACTION_OPEN_PRIVATE_TAB
import org.mozilla.fenix.splashscreen.ApplyExperimentsOperation
import org.mozilla.fenix.splashscreen.DefaultExperimentsOperationStorage
import org.mozilla.fenix.splashscreen.DefaultSplashScreenStorage
import org.mozilla.fenix.splashscreen.FetchExperimentsOperation
import org.mozilla.fenix.splashscreen.SplashScreenManager
import org.mozilla.fenix.splashscreen.SplashScreenOperation
import org.mozilla.fenix.tabhistory.TabHistoryDialogFragment
import org.mozilla.fenix.theme.DefaultThemeManager
import org.mozilla.fenix.theme.StatusBarColorManager
import org.mozilla.fenix.theme.TabStripStatusBarView
import org.mozilla.fenix.theme.ThemeManager
import org.mozilla.fenix.translations.TranslationsAIControllableFeatureRegistrar
import org.mozilla.fenix.translations.TranslationsEnabledSettings
import org.mozilla.fenix.utils.AccessibilityUtils.announcePrivateModeForAccessibility
import org.mozilla.fenix.utils.Settings
import org.mozilla.fenix.utils.changeAppLauncherIcon

/**
 * The main activity of the application. The application is primarily a single Activity (this one) with fragments
 * switching out to display different views. The most important views shown here are the:
 * - home screen
 * - browser screen
 */
@SuppressWarnings("TooManyFunctions", "LargeClass", "LongMethod")
open class HomeActivity : LocaleAwareAppCompatActivity(), NavHostActivity, CrashActionDispatcher {
    @VisibleForTesting internal lateinit var binding: ActivityHomeBinding
    lateinit var themeManager: ThemeManager
    lateinit var browsingModeManager: BrowsingModeManager

    private var isVisuallyComplete = false

    private var privateNotificationObserver: PrivateNotificationFeature<PrivateNotificationService>? = null

    private var isToolbarInflated = false

    private val webExtensionPopupObserver by lazy {
        WebExtensionPopupObserver(components.core.store, ::openPopup)
    }

    private val webExtensionOptionsPageObserver by lazy {
        WebExtensionOptionsPageObserver(components.core.store, ::openOptionsPage)
    }

    private val webExtensionPromptFeature by lazy {
        WebExtensionPromptFeature(
            store = components.core.store,
            context = this@HomeActivity,
            fragmentManager = supportFragmentManager,
            navController = navHost.navController,
            onLinkClicked = { url, shouldOpenInBrowser ->
                if (shouldOpenInBrowser) {
                    @Suppress("DEPRECATION")
                    openToBrowserAndLoad(
                        searchTermOrURL = url,
                        newTab = true,
                        from = BrowserDirection.FromGlobal,
                    )
                } else {
                    startActivity(
                        SupportUtils.createCustomTabIntent(
                            context = this,
                            url = url,
                        )
                    )
                }
            },
        )
    }

    private val ipProtectionPrompter by lazy {
        IPProtectionInfoPrompter(
            store = components.ipProtection.store,
            appStore = components.appStore,
            errorMessages =
                ErrorMessages(
                    dataLimitReached =
                        this.getString(
                            R.string.ip_protection_data_limit_reached_snackbar,
                            FxNimbus.features.ipProtection.value().dataLimitGigabyte,
                        )
                ),
        )
    }

    private val translationsAIControllableFeatureRegistrar by lazy {
        with(components) {
            TranslationsAIControllableFeatureRegistrar(
                aiRegistry = aiFeatureRegistry,
                browserStore = core.store,
                translationsEnabledSettings = TranslationsEnabledSettings.dataStore(this@HomeActivity),
                scope = lifecycleScope,
            )
        }
    }

    private val defaultTopSitesBinding by lazy {
        DefaultTopSitesBinding(
            browserStore = components.core.store,
            topSitesStorage = components.core.topSitesStorage,
            settings = components.settings,
            resources = resources,
            crashReporter = components.analytics.crashReporter,
        )
    }

    private val aboutHomeBinding by lazy {
        AboutHomeBinding(
            browserStore = components.core.store,
            navController = navHost.navController,
        )
    }

    private val homepageTabBinding by lazy {
        HomepageTabBinding(
            browserStore = components.core.store,
            browsingModeManager = browsingModeManager,
            fenixBrowserUseCases = components.useCases.fenixBrowserUseCases,
            repository = DefaultHomepageAsANewTabPreferenceRepository(components.settings),
        )
    }

    private val downloadSnackbar by lazy {
        DownloadSnackbar(
            store = components.core.store,
            appStore = components.appStore,
        )
    }

    private val crashReporterBinding by lazy {
        CrashReporterBinding(
            store = components.appStore,
            onReporting = ::showCrashReporter,
        )
    }

    private val externalAppLinkStatusBinding by lazy {
        ExternalAppLinkStatusBinding(
            settings = components.settings,
            appLinksUseCases = components.useCases.appLinksUseCases,
            browserStore = components.core.store,
            appStore = components.appStore,
        )
    }

    private val summarizeToolbarHighlightBinding by lazy {
        SummarizeToolbarHighlightBinding(
            appStore = components.appStore,
            featureDiscoverySettings = components.core.summarizeFeatureSettings,
            browserStore = components.core.store,
            mainDispatcher = Main,
        )
    }

    private val extensionsProcessDisabledForegroundController by lazy {
        ExtensionsProcessDisabledForegroundController(this@HomeActivity)
    }

    private val extensionsProcessDisabledBackgroundController by lazy {
        ExtensionsProcessDisabledBackgroundController(
            browserStore = components.core.store,
            appStore = components.appStore,
        )
    }

    private val serviceWorkerSupport by lazy {
        ServiceWorkerSupportFeature(this)
    }

    private val privateBrowsingLockFeature by lazy {
        PrivateBrowsingLockFeature(
            appStore = components.appStore,
            browserStore = components.core.store,
            storage =
                DefaultPrivateBrowsingLockStorage(
                    preferences = components.settings.preferences,
                    privateBrowsingLockPrefKey = getString(R.string.pref_key_private_browsing_locked),
                ),
        )
    }

    private var inflater: LayoutInflater? = null

    private val navHost by lazy {
        supportFragmentManager.findFragmentById(R.id.container) as NavHostFragment
    }

    private val externalSourceIntentProcessors by lazy {
        listOf(
            HomeDeepLinkIntentProcessor(
                activity = this,
                shareUseCases = components.useCases.shareUseCases,
            ),
            SpeechProcessingIntentProcessor(this, components.core.store),
            AssistIntentProcessor(),
            StartSearchIntentProcessor { components.fenixOnboarding.userHasBeenOnboarded() },
            OpenBrowserIntentProcessor(this, ::getIntentSessionId),
            OpenSpecificTabIntentProcessor(this),
            OpenPasswordManagerIntentProcessor(),
            OpenRecentlyClosedIntentProcessor(),
        )
    }

    private val uninstallSurveyManager by lazy {
        UninstallSurveyManager(this, DefaultShortcutManagerCompatWrapper())
    }

    // See onKeyDown for why this is necessary
    private var backLongPressJob: Job? = null

    private lateinit var navigationToolbar: Toolbar

    // Tracker for contextual menu (Copy|Search|Select all|etc...)
    private var actionMode: ActionMode? = null

    private val startupPathProvider: StartupPathProvider = DefaultStartupPathProvider()
    private lateinit var startupTypeTelemetry: StartupTypeTelemetry

    private val onBackPressedCallback =
        object :
            UserInteractionOnBackPressedCallback(
                fragmentManager = supportFragmentManager,
                dispatcher = onBackPressedDispatcher,
            ) {
            override fun handleOnBackPressed() {
                if (shouldUsePredictiveBackLongPress()) {
                    backLongPressJob?.cancel()
                }
                super.handleOnBackPressed()
            }

            private fun isButtonPress(backEvent: BackEventCompat): Boolean {
                return (
                // Both touchX and touchY being 0 means this is a back button press and not a back gesture.
                // Android 16+ will introduce a better way of checking for this.
                // See https://bugzilla.mozilla.org/show_bug.cgi?id=1944282
                (backEvent.touchX == 0.0f && backEvent.touchY == 0.0f) ||
                    // touchX and touchY are also documented to return NaN for button presses
                    (backEvent.touchX.isNaN() && backEvent.touchY.isNaN()))
            }

            override fun handleOnBackStarted(backEvent: BackEventCompat) {
                if (shouldUsePredictiveBackLongPress() && isButtonPress(backEvent)) {
                    backLongPressJob = lifecycleScope.launch {
                        delay(ViewConfiguration.getLongPressTimeout().toLong())
                        handleBackLongPress()
                    }
                }
            }

            override fun handleOnBackCancelled() {
                if (shouldUsePredictiveBackLongPress()) {
                    backLongPressJob?.cancel()
                }
            }
        }

    @Suppress("CognitiveComplexMethod", "CyclomaticComplexMethod")
    final override fun onCreate(savedInstanceState: Bundle?) {
        // DO NOT MOVE ANYTHING ABOVE THIS getProfilerTime CALL.
        val startTimeProfiler = components.core.engine.profiler?.getProfilerTime()

        // Setup nimbus-cli tooling. This is a NOOP when launching normally.
        components.nimbus.sdk.initializeTooling(applicationContext, intent)
        components.strictMode.attachListenerToDisablePenaltyDeath(supportFragmentManager)
        MarkersFragmentLifecycleCallbacks.register(supportFragmentManager, components.core.engine)

        // There is disk read violations on some devices such as samsung and pixel for android 9/10
        components.strictMode.allowViolation(StrictMode::allowThreadDiskReads) {
            // Browsing mode & theme setup should always be called before super.onCreate.
            browsingModeManager = createBrowsingModeManager(intent)
            setupTheme()

            super.onCreate(savedInstanceState)
        }

        // Must run after the edge-to-edge capable theme is applied by
        // `setupTheme` (or installSplashScreen). It must also be run before
        // the `setContent` starts attaching fragments.
        EdgeToEdgeFragmentLifecycleCallbacks.register(supportFragmentManager, window)

        // Checks if Activity is currently in PiP mode if launched from external intents, then exits it
        checkAndExitPiP()

        // Diagnostic breadcrumb for "Display already aquired" crash:
        // https://github.com/mozilla-mobile/android-components/issues/7960
        breadcrumb(
            message = "onCreate()",
            data =
                mapOf(
                    "recreated" to (savedInstanceState != null).toString(),
                    "intent" to (intent?.action ?: "null"),
                ),
        )

        components.publicSuffixList.prefetch()

        // Changing a language on the Language screen restarts the activity, but the activity keeps
        // the old layout direction. We have to update the direction manually.
        window.decorView.layoutDirection = Locale.getDefault().layoutDirection

        binding = ActivityHomeBinding.inflate(layoutInflater)

        Performance.processIntentIfPerformanceTest(intent, this)

        // Persist or clear a Glean debug view tag across restarts (Nightly/Debug only).
        DefaultGleanDebugToolsStorage.persistDebugViewTagIfRequested(intent, components.settings)

        components.settings.seedOnboardingCompletedTimestampForDebugIfNeeded()

        val shouldShowOnboarding =
            !intent.isAllowedDuringOnboardingIntent(packageName) &&
                with(components) {
                    settings.shouldShowOnboarding(
                        hasUserBeenOnboarded = fenixOnboarding.userHasBeenOnboarded(),
                        forceOnboardingForBenchmark = intent.getBooleanExtra(EXTRA_FORCE_ONBOARDING, false),
                    )
                }

        SplashScreenManager(
                splashScreenOperation = createSplashScreenOperation(shouldShowOnboarding),
                scope = lifecycleScope,
                splashScreenTimeout = FxNimbus.features.splashScreen.value().maximumDurationMs.toLong(),
                storage = DefaultSplashScreenStorage(components.settings),
                showSplashScreen = { installSplashScreen().setKeepOnScreenCondition(it) },
                onSplashScreenFinished = { result ->
                    if (result.sendTelemetry) {
                        SplashScreen.firstLaunchExtended.record(
                            SplashScreen.FirstLaunchExtendedExtra(dataFetched = result.wasDataFetched)
                        )
                    }

                    if (savedInstanceState == null && shouldShowOnboarding) {
                        navHost.navController.navigate(NavGraphDirections.actionGlobalOnboarding())
                    }
                },
            )
            .showSplashScreen()

        lifecycleScope.launch {
            val debugSettingsRepository =
                DefaultDebugSettingsRepository(
                    context = this@HomeActivity,
                    writeScope = this,
                )

            debugSettingsRepository.debugDrawerEnabled.distinctUntilChanged().collect { enabled ->
                with(binding.debugOverlay) {
                    if (enabled) {
                        visibility = View.VISIBLE

                        setContent {
                            FenixOverlay(
                                browserStore = components.core.store,
                                inactiveTabsEnabled = components.settings.inactiveTabsAreEnabled,
                                loginsStorage = components.core.passwordsStorage,
                                tabGroupRepository = components.core.tabGroupRepository,
                            )
                        }
                    } else {
                        setContent {}

                        visibility = View.GONE
                    }
                }
            }
        }

        setContentView(binding.root)
        ProfilerMarkers.addListenerForOnGlobalLayout(components.core.engine, this, binding.root)

        privateNotificationObserver =
            PrivateNotificationFeature(
                    applicationContext,
                    components.core.store,
                    PrivateNotificationService::class,
                )
                .also {
                    it.start()
                }

        if (!shouldShowOnboarding) {
            lifecycleScope.launch(IO) {
                showFullscreenMessageIfNeeded(applicationContext)
            }

            // Unless the activity is recreated, navigate to home first (without rendering it)
            // to add it to the back stack.
            if (savedInstanceState == null) {
                navigateToHome(navHost.navController)
            }

            if (shouldNavigateToBrowserOnColdStart(savedInstanceState)) {
                if (!shouldStartOnHome()) {
                    navigateToBrowserOnColdStart()
                }
                maybeShowSetAsDefaultBrowserPrompt()
            } else {
                StartOnHome.enterHomeScreen.record(NoExtras())
            }
        }

        // This will record an event in Nimbus' internal event store. Used for behavioral targeting
        recordEventInNimbus("app_opened")
        if (components.settings.isTelemetryEnabled) {
            lifecycle.addObserver(
                BreadcrumbsRecorder(
                    components.analytics.crashReporter,
                    navHost.navController,
                    ::getBreadcrumbMessage,
                )
            )

            val safeIntent = intent?.toSafeIntent()
            safeIntent?.let(::getIntentSource)?.also { source ->
                Events.appOpened.record(Events.AppOpenedExtra(source = source))

                if (safeIntent.action.equals(ACTION_OPEN_PRIVATE_TAB) && source == APP_ICON) {
                    AppIcon.newPrivateTabTapped.record(NoExtras())
                }
            }
        }
        supportActionBar?.hide()

        lifecycle.addObservers(
            webExtensionPopupObserver,
            webExtensionOptionsPageObserver,
            extensionsProcessDisabledForegroundController,
            extensionsProcessDisabledBackgroundController,
            serviceWorkerSupport,
            crashReporterBinding,
            defaultTopSitesBinding,
            TopSitesRefresher(
                settings = components.settings,
                topSitesProvider = components.core.macTopSitesProvider,
                startupPathProvider = startupPathProvider,
                visualCompletenessQueue = components.performance.visualCompletenessQueue,
            ),
            downloadSnackbar,
            privateBrowsingLockFeature,
            externalAppLinkStatusBinding,
            summarizeToolbarHighlightBinding,
            components.core.summarizationSettingsBinding,
            translationsAIControllableFeatureRegistrar,
            ipProtectionPrompter,
        )

        addAboutHomeBinding(lifecycle)
        addHomepageTabBinding(lifecycle)

        if (!isCustomTabIntent(intent)) {
            lifecycle.addObserver(webExtensionPromptFeature)
        }

        if (shouldAddToRecentsScreen(intent)) {
            intent.removeExtra(START_IN_RECENTS_SCREEN)
            moveTaskToBack(true)
        }

        captureSnapshotTelemetryMetrics()

        startupTelemetryOnCreateCalled(intent.toSafeIntent())
        startupPathProvider.attachOnActivityOnCreate(lifecycle, intent)
        startupTypeTelemetry =
            StartupTypeTelemetry(components.startupStateProvider, startupPathProvider).apply {
                attachOnHomeActivityOnCreate(lifecycle)
            }

        components.core.requestInterceptor.setNavigationController(navHost.navController)

        supportFragmentManager.registerFragmentLifecycleCallbacks(
            StatusBarColorManager(
                themeManager = themeManager,
                activity = this,
                appStore = components.appStore,
                settings = components.settings,
                tabStripStatusBarView =
                    TabStripStatusBarView(
                        rootView = window.decorView as ViewGroup,
                        lifecycle = lifecycle,
                    ),
            ),
            true,
        )

        if (components.settings.showContileFeature) {
            components.core.macTopSitesUpdater.startPeriodicWork()
        }

        if (!components.settings.hiddenEnginesRestored) {
            components.settings.hiddenEnginesRestored = true
            components.useCases.searchUseCases.restoreHiddenSearchEngines.invoke()
        }

        components.backgroundServices.accountManagerAvailableQueue.runIfReadyOrQueue {
            lifecycleScope.launch(IO) {
                // If we're authenticated, kick-off a sync and a device state refresh.
                components.backgroundServices.accountManager.authenticatedAccount()?.let {
                    components.backgroundServices.accountManager.syncNow(reason = SyncReason.Startup)
                }
            }
        }

        components.core.engine.profiler?.addMarker(
            MarkersActivityLifecycleCallbacks.MARKER_NAME,
            startTimeProfiler,
            "HomeActivity.onCreate",
        )

        components.notificationsDelegate.bindToActivity(this)

        components.settings.coldStartsBetweenSetAsDefaultPrompts++

        components.appStore.dispatch(
            AppAction.OrientationChange(orientation = OrientationMode.fromInteger(resources.configuration.orientation))
        )

        onBackPressedDispatcher.addCallback(
            owner = this,
            onBackPressedCallback = onBackPressedCallback,
        )

        lifecycleScope.launch(IO) {
            uninstallSurveyManager.updateUninstallSurveyShortcut()
        }

        if (components.settings.uninstallSurveyFeatureFlagEnabled) {
            uninstallSurveyManager.showUninstallSurvey(intent, navHost.navController)
        }

        StartupTimeline.onActivityCreateEndHome(this) // DO NOT MOVE ANYTHING BELOW HERE.
    }

    @VisibleForTesting
    internal fun maybeShowSetAsDefaultBrowserPrompt(
        shouldShowSetAsDefaultPrompt: Boolean = components.settings.shouldShowSetAsDefaultPrompt(),
        isDefaultBrowser: Boolean = Browsers.isDefaultBrowser(applicationContext),
        isTheCorrectBuildVersion: Boolean = Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q,
    ) {
        if (shouldShowSetAsDefaultPrompt && !isDefaultBrowser && isTheCorrectBuildVersion) {
            // This is to avoid disk read violations on some devices such as samsung and pixel for android 9/10
            components.strictMode.allowViolation(StrictMode::allowThreadDiskReads) {
                components.appStore.dispatch(AppAction.UpdateWasNativeDefaultBrowserPromptShown(true))
                showSetDefaultBrowserPrompt()
                Metrics.setAsDefaultBrowserNativePromptShown.record()
                components.settings.setAsDefaultPromptCalled()
            }
        }
    }

    @VisibleForTesting
    internal fun showSetDefaultBrowserPrompt() {
        openSetDefaultBrowserOption()
    }

    private fun checkAndExitPiP() {
        if (isInPictureInPictureMode && intent != null) {
            // Exit PiP mode
            moveTaskToBack(false)
            startActivity(Intent(this, this::class.java).setFlags(FLAG_ACTIVITY_REORDER_TO_FRONT))
        }
    }

    private fun startupTelemetryOnCreateCalled(safeIntent: SafeIntent) {
        // We intentionally only record this in HomeActivity and not ExternalBrowserActivity (e.g.
        // PWAs) so we don't include more unpredictable code paths in the results.
        components.performance.coldStartupDurationTelemetry.onHomeActivityOnCreate(
            components.performance.visualCompletenessQueue,
            components.startupStateProvider,
            safeIntent,
            binding.rootContainer,
        )
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray,
    ) {
        when (requestCode) {
            REQUEST_CODE_CAMERA_PERMISSIONS -> {
                if (grantResults.isNotEmpty() && grantResults[0] == PackageManager.PERMISSION_DENIED) {
                    // if denied, do not relaunch QR Scanner
                    components.appStore.dispatch(AppAction.QrScannerAction.QrScannerRequestConsumed)
                } else {
                    components.appStore.dispatch(AppAction.QrScannerAction.QrScannerRequested)
                }
            }
            else -> super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        }
    }

    @CallSuper
    override fun onResume() {
        super.onResume()

        // Diagnostic breadcrumb for "Display already aquired" crash:
        // https://github.com/mozilla-mobile/android-components/issues/7960
        breadcrumb(message = "onResume()")

        binding.root.doOnLayout {
            if (browsingModeManager.mode.isPrivate) {
                it.announcePrivateModeForAccessibility()
            }
        }

        lifecycleScope.launch(IO) {
            if (components.settings.checkIfFenixIsDefaultBrowserOnAppResume()) {
                if (components.appStore.state.wasNativeDefaultBrowserPromptShown) {
                    Metrics.defaultBrowserChangedViaNativeSystemPrompt.record(NoExtras())
                }

                components.appStore.dispatch(AppAction.UpdateDefaultBrowserStatus(true))
                Events.defaultBrowserChanged.record(NoExtras())
            }

            GrowthDataWorker.sendActivatedSignalIfNeeded(applicationContext)
            FontEnumerationWorker.sendActivatedSignalIfNeeded(applicationContext)

            if (components.core.sentFromFirefoxManager.shouldShowSnackbar) {
                components.appStore.dispatch(ShareAction.ShareToWhatsApp)
            }
        }

        lifecycleScope.launch(IO) {
            // Register the [MARKETING_CHANNEL_ID] channel so that it appears in the Android Settings App.
            ensureMarketingChannelExists(applicationContext)
            if (NotificationManagerCompat.from(applicationContext).areNotificationsEnabled()) {
                MessageNotificationWorker.setMessageNotificationWorker(applicationContext)
            } else {
                MessageNotificationWorker.cancelMessageNotificationWorker(applicationContext)
            }
        }

        onBackPressedCallback.isEnabled = true

        // This was done in order to refresh search engines when app is running in background
        // and the user changes the system language
        // More details here: https://github.com/mozilla-mobile/fenix/pull/27793#discussion_r1029892536
        components.core.store.dispatch(SearchAction.RefreshSearchEnginesAction)
    }

    override fun onRestart() {
        super.onRestart()

        browsingModeManager.updateMode(intent)
    }

    final override fun onStart() {
        // DO NOT MOVE ANYTHING ABOVE THIS getProfilerTime CALL.
        val startProfilerTime = components.core.engine.profiler?.getProfilerTime()

        components.termsOfUseManager.onStart()

        super.onStart()

        // Diagnostic breadcrumb for "Display already aquired" crash:
        // https://github.com/mozilla-mobile/android-components/issues/7960
        breadcrumb(message = "onStart()")

        ProfilerMarkers.homeActivityOnStart(binding.rootContainer, components.core.engine.profiler)

        if (components.settings.longfoxPeekAnimationShownCount < Settings.LONGFOX_PEEK_ANIMATION_MAX_SHOWS) {
            components.settings.appLaunchCount++
            components.appStore.dispatch(
                AppAction.UpdateShowFoxPeekAnimation(components.settings.shouldShowLongfoxPeekAnimationThisTime())
            )
        }

        components.core.engine.profiler?.addMarker(
            MarkersActivityLifecycleCallbacks.MARKER_NAME,
            startProfilerTime,
            "HomeActivity.onStart",
        ) // DO NOT MOVE ANYTHING BELOW THIS addMarker CALL.
    }

    final override fun onStop() {
        // DO NOT MOVE ANYTHING ABOVE THIS getProfilerTime CALL.
        val startTimeProfiler = components.core.engine.profiler?.getProfilerTime()

        super.onStop()

        // Diagnostic breadcrumb for "Display already aquired" crash:
        // https://github.com/mozilla-mobile/android-components/issues/7960
        breadcrumb(
            message = "onStop()",
            data = mapOf("finishing" to isFinishing.toString()),
        )

        if (FxNimbus.features.alternativeAppLauncherIcon.value().enabled) {
            // User has been enrolled in alternative app icon experiment.
            // Note: Updating the icon will subsequently trigger a call to onDestroy().
            with(applicationContext) {
                changeAppLauncherIcon(
                    context = this,
                    appAlias = ComponentName(this, "$packageName.App"),
                    alternativeAppAlias = ComponentName(this, "$packageName.AlternativeApp"),
                    resetToDefault = FxNimbus.features.alternativeAppLauncherIcon.value().resetToDefault,
                    crashReporter = components.analytics.crashReporter,
                )
            }
        }

        components.core.engine.profiler?.addMarker(
            MarkersActivityLifecycleCallbacks.MARKER_NAME,
            startTimeProfiler,
            "HomeActivity.onStop",
        )
    }

    final override fun onPause() {
        // We should return to the browser if there were normal tabs when we left the app
        components.settings.shouldReturnToBrowser =
            components.core.store.state.getNormalOrPrivateTabs(private = false).isNotEmpty()

        lifecycleScope.launch(IO) {
            val desktopFolders =
                DesktopFolders(
                    applicationContext,
                    showMobileRoot = false,
                )
            components.settings.desktopBookmarksSize = desktopFolders.count()

            components.settings.mobileBookmarksSize =
                components.core.bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Mobile.id)).toInt()
        }

        super.onPause()

        // Diagnostic breadcrumb for "Display already aquired" crash:
        // https://github.com/mozilla-mobile/android-components/issues/7960
        breadcrumb(
            message = "onPause()",
            data = mapOf("finishing" to isFinishing.toString()),
        )

        // Every time the application goes into the background, it is possible that the user
        // is about to change the browsers installed on their system. Therefore, we reset the cache of
        // all the installed browsers.
        //
        // NB: There are ways for the user to install new products without leaving the browser.
        BrowsersCache.resetAll()
    }

    override fun onProvideAssistContent(outContent: AssistContent?) {
        super.onProvideAssistContent(outContent)
        val currentTabUrl = components.core.store.state.selectedTab?.content?.url
        outContent?.webUri = currentTabUrl?.let { it.toUri() }
    }

    @CallSuper
    override fun onDestroy() {
        val startTimeProfiler = components.core.engine.profiler?.getProfilerTime()

        super.onDestroy()

        // Diagnostic breadcrumb for "Display already aquired" crash:
        // https://github.com/mozilla-mobile/android-components/issues/7960
        breadcrumb(
            message = "onDestroy()",
            data = mapOf("finishing" to isFinishing.toString()),
        )

        components.core.macTopSitesUpdater.stopPeriodicWork()
        components.core.pocketStoriesService.stopPeriodicContentRecommendationsRefresh()
        components.core.pocketStoriesService.stopPeriodicSponsoredContentsRefresh()
        privateNotificationObserver?.stop()
        components.notificationsDelegate.unBindActivity(this)

        // clear hierarchy change listener set by AndroidX SplashScreen
        // https://bugzilla.mozilla.org/show_bug.cgi?id=1950295
        (window.decorView as? ViewGroup)?.setOnHierarchyChangeListener(null)

        val activityStartedWithLink = startupPathProvider.startupPathForActivity == StartupPathProvider.StartupPath.VIEW
        if (this !is ExternalAppBrowserActivity && !activityStartedWithLink) {
            stopMediaSession()
        }

        components.core.engine.profiler?.addMarker(
            MarkersActivityLifecycleCallbacks.MARKER_NAME,
            startTimeProfiler,
            "HomeActivity.onDestroy",
        )
    }

    final override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)

        // Diagnostic breadcrumb for "Display already aquired" crash:
        // https://github.com/mozilla-mobile/android-components/issues/7960
        breadcrumb(message = "onConfigurationChanged()")

        components.appStore.dispatch(
            AppAction.OrientationChange(orientation = OrientationMode.fromInteger(newConfig.orientation))
        )
    }

    final override fun recreate() {
        // Diagnostic breadcrumb for "Display already aquired" crash:
        // https://github.com/mozilla-mobile/android-components/issues/7960
        breadcrumb(message = "recreate()")

        super.recreate()
    }

    /** Handles intents received when the activity is open. */
    final override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        handleNewIntent(intent)
        startupPathProvider.onIntentReceived(intent)
    }

    @VisibleForTesting
    internal fun handleNewIntent(intent: Intent) {
        if (this is ExternalAppBrowserActivity) {
            return
        }

        // Warm-launch path for the Glean debug intent; onCreate handles cold start (Nightly/Debug only).
        DefaultGleanDebugToolsStorage.persistDebugViewTagIfRequested(intent, components.settings)

        if (intent.action == SEND_TO_DEVICES_ACTION) {
            handleSendToDevicesActionIntent(intent)
            return
        }

        val qrCodeUri = intent.getStringExtra(QR_CODE_URI_KEY)
        if (qrCodeUri != null) {
            if (supportFragmentManager.findFragmentByTag(QRCodeDialogFragment.TAG) == null) {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
                    NativeShareSheet.qrCodeTapped.record(NoExtras())
                }

                QRCodeDialogFragment.newInstance(qrCodeUri)
                    .showNow(
                        supportFragmentManager,
                        QRCodeDialogFragment.TAG,
                    )
            }
        } else {
            // Diagnostic breadcrumb for "Display already aquired" crash:
            // https://github.com/mozilla-mobile/android-components/issues/7960
            breadcrumb(
                message = "onNewIntent()",
                data = mapOf("intent" to intent.action.toString()),
            )

            val tab = components.core.store.state.findActiveMediaTab()
            if (tab != null) {
                components.useCases.sessionUseCases.exitFullscreen(tab.id)
            }

            val intentProcessors =
                listOf(CrashReporterIntentProcessor(components.appStore)) + externalSourceIntentProcessors
            intentProcessors.forEach { it.process(intent, navHost.navController, this.intent, components.settings) }
            browsingModeManager.updateMode(intent)
        }
    }

    private fun handleSendToDevicesActionIntent(intent: Intent) {
        val urls = intent.getStringArrayListExtra(SendToDevicesDialogFragment.EXTRA_URLS)
        if (urls.isNullOrEmpty()) return
        val titles = intent.getStringArrayListExtra(SendToDevicesDialogFragment.EXTRA_TITLES).orEmpty()
        val isPrivate =
            intent.getStringExtra(SendToDevicesDialogFragment.EXTRA_PRIVACY) ==
                SendToDevicesDialogFragment.PRIVACY_PRIVATE

        if (supportFragmentManager.findFragmentByTag(SendToDevicesDialogFragment.TAG) == null) {
            SendToDevicesDialogFragment.newInstance(urls, titles, isPrivate)
                .showNow(
                    supportFragmentManager,
                    SendToDevicesDialogFragment.TAG,
                )
        }
    }

    /** Overrides view inflation to inject a custom [EngineView] from [components]. */
    final override fun onCreateView(
        parent: View?,
        name: String,
        context: Context,
        attrs: AttributeSet,
    ): View? =
        when (name) {
            EngineView::class.java.name ->
                components.core.engine
                    .createView(context, attrs)
                    .apply {
                        selectionActionDelegate =
                            DefaultSelectionActionDelegate(
                                BrowserStoreSearchAdapter(
                                    components.core.store,
                                    tabId = getIntentSessionId(intent.toSafeIntent()),
                                ),
                                resources = context.resources,
                                shareTextClicked = { share(it) },
                                emailTextClicked = { email(it) },
                                callTextClicked = { call(it) },
                                actionSorter = ::actionSorter,
                            )
                    }
                    .asView()
            else -> super.onCreateView(parent, name, context, attrs)
        }

    final override fun onActionModeStarted(mode: ActionMode?) {
        actionMode = mode
        super.onActionModeStarted(mode)
    }

    final override fun onActionModeFinished(mode: ActionMode?) {
        actionMode = null
        super.onActionModeFinished(mode)
    }

    fun finishActionMode() {
        actionMode?.finish().also { actionMode = null }
    }

    @Suppress("MagicNumber")
    // Defining the positions as constants doesn't seem super useful here.
    private fun actionSorter(actions: Array<String>): Array<String> {
        val order = hashMapOf<String, Int>()

        order["CUSTOM_CONTEXT_MENU_EMAIL"] = 0
        order["CUSTOM_CONTEXT_MENU_CALL"] = 1
        order["org.mozilla.geckoview.COPY"] = 2
        order["CUSTOM_CONTEXT_MENU_SEARCH"] = 3
        order["CUSTOM_CONTEXT_MENU_SEARCH_PRIVATELY"] = 4
        order["org.mozilla.geckoview.PASTE"] = 5
        order["org.mozilla.geckoview.SELECT_ALL"] = 6
        order["CUSTOM_CONTEXT_MENU_SHARE"] = 7

        return actions
            .sortedBy { actionName ->
                // Sort the actions in our preferred order, putting "other" actions unsorted at the end
                order[actionName] ?: actions.size
            }
            .toTypedArray()
    }

    @Deprecated("Deprecated in Java")
    // https://github.com/mozilla-mobile/fenix/issues/19919
    final override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        supportFragmentManager.primaryNavigationFragment?.childFragmentManager?.fragments?.forEach {
            if (it is ActivityResultHandler && it.onActivityResult(requestCode, data, resultCode)) {
                return
            }
        }
        @Suppress("DEPRECATION") super.onActivityResult(requestCode, resultCode, data)
    }

    private fun shouldUseCustomBackLongPress(): Boolean {
        // Huawei devices seem to have problems with onKeyLongPress
        // See https://github.com/mozilla-mobile/fenix/issues/13498
        return BuildManufacturerChecker().isHuawei()
    }

    /**
     * Get whether to use [OnBackPressedDispatcher] listeners for back button long presses instead of deprecated `onKey`
     * callbacks. Requires `enableOnBackInvokedCallback` feature.
     */
    private fun shouldUsePredictiveBackLongPress(): Boolean {
        // When predictive back handlers are enabled (android:enableOnBackInvokedCallback),
        // legacy onKeyDown etc do not trigger
        // See https://bugzilla.mozilla.org/show_bug.cgi?id=1932300
        // While the bug impacts Android 13, the new handlers are only available from Android 14+
        // It's possible that some devices (Pixel) still fire the old handlers in Android 13+,
        // so we don't enable the new handlers for that version
        return Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE
    }

    private fun handleBackLongPress(): Boolean {
        supportFragmentManager.primaryNavigationFragment?.childFragmentManager?.fragments?.forEach {
            if (it is OnLongPressedListener && it.onBackLongPressed()) {
                return true
            }
        }
        return false
    }

    private fun handleForwardLongPress(): Boolean {
        supportFragmentManager.primaryNavigationFragment?.childFragmentManager?.fragments?.forEach {
            if (it is OnLongPressedListener && it.onForwardLongPressed()) {
                return true
            }
        }
        return false
    }

    override fun dispatchTouchEvent(ev: MotionEvent?): Boolean {
        ProfilerMarkers.addForDispatchTouchEvent(components.core.engine.profiler, ev)
        return super.dispatchTouchEvent(ev)
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        // The KEYCODE_MENU event is handled here instead of onKeyDown or onKeyUp because
        // after navigating to another fragment like Settings or Bookmarks, and then back this
        // key event is somehow getting consumed before it reaches onKeyDown or onKeyUp.
        if (event.action == KeyEvent.ACTION_DOWN && event.keyCode == KeyEvent.KEYCODE_MENU) {
            val navHostFragment = supportFragmentManager.findFragmentById(R.id.container) as? NavHostFragment
            val currentFragment = navHostFragment?.childFragmentManager?.primaryNavigationFragment
            when (currentFragment) {
                is HomeFragment -> {
                    val action = NavGraphDirections.actionGlobalMenuDialogFragment(MenuAccessPoint.Home)
                    navHost.navController.navigate(action)
                    return true
                }

                is BrowserFragment -> {
                    val action = NavGraphDirections.actionGlobalMenuDialogFragment(MenuAccessPoint.Browser)
                    navHost.navController.navigate(action)
                    return true
                }
            }
        }
        return super.dispatchKeyEvent(event)
    }

    final override fun onKeyDown(keyCode: Int, event: KeyEvent?): Boolean {
        // Inspired by
        // https://searchfox.org/mozilla-esr68/source/mobile/android/base/java/org/mozilla/gecko/BrowserApp.java#584-613
        // Android N and Huawei devices have broken onKeyLongPress events for the back button, so we
        // instead implement the long press behavior ourselves
        // - For short presses, we cancel the callback in onKeyUp
        // - For long presses, the normal keypress is marked as cancelled, hence won't be handled elsewhere
        //   (but Android still provides the haptic feedback), and the long press action is run
        if (shouldUseCustomBackLongPress() && keyCode == KeyEvent.KEYCODE_BACK && !shouldUsePredictiveBackLongPress()) {
            backLongPressJob = lifecycleScope.launch {
                delay(ViewConfiguration.getLongPressTimeout().toLong())
                handleBackLongPress()
            }
        }

        if (keyCode == KeyEvent.KEYCODE_FORWARD) {
            event?.startTracking()
            return true
        }

        return super.onKeyDown(keyCode, event)
    }

    @Suppress("ReturnCount")
    final override fun onKeyUp(keyCode: Int, event: KeyEvent): Boolean {
        if (shouldUseCustomBackLongPress() && keyCode == KeyEvent.KEYCODE_BACK && !shouldUsePredictiveBackLongPress()) {
            backLongPressJob?.cancel()

            // check if the key has been pressed for longer than the time needed for a press to turn into a long press
            // and if tab history is already visible we do not want to dismiss it.
            if (
                event.eventTime - event.downTime >= ViewConfiguration.getLongPressTimeout() &&
                    navHost.navController.hasTopDestination(TabHistoryDialogFragment.NAME)
            ) {
                // returning true avoids further processing of the KeyUp event and avoids dismissing tab history.
                return true
            }
        }

        if (keyCode == KeyEvent.KEYCODE_FORWARD) {
            if (navHost.navController.hasTopDestination(TabHistoryDialogFragment.NAME)) {
                // returning true avoids further processing of the KeyUp event
                return true
            }

            supportFragmentManager.primaryNavigationFragment?.childFragmentManager?.fragments?.forEach {
                if (it is UserInteractionHandler && it.onForwardPressed()) {
                    return true
                }
            }
        }

        return super.onKeyUp(keyCode, event)
    }

    final override fun onKeyLongPress(keyCode: Int, event: KeyEvent?): Boolean {
        // onKeyLongPress is broken in Android N so we don't handle back button long presses here
        // for N. The version check ensures we don't handle back button long presses twice.
        if (
            !shouldUseCustomBackLongPress() && keyCode == KeyEvent.KEYCODE_BACK && !shouldUsePredictiveBackLongPress()
        ) {
            return handleBackLongPress()
        }

        if (keyCode == KeyEvent.KEYCODE_FORWARD) {
            return handleForwardLongPress()
        }

        return super.onKeyLongPress(keyCode, event)
    }

    final override fun onUserLeaveHint() {
        // The notification permission prompt will trigger onUserLeaveHint too.
        // We shouldn't treat this situation as user leaving.
        if (!components.notificationsDelegate.isRequestingPermission) {
            supportFragmentManager.primaryNavigationFragment?.childFragmentManager?.fragments?.forEach {
                if (it is UserInteractionHandler && it.onHomePressed()) {
                    return
                }
            }
        }

        super.onUserLeaveHint()
    }

    /**
     * Determines whether the activity should be pushed to be backstack (i.e., 'minimized' to the recents screen) upon
     * starting.
     *
     * @param intent - The intent that started this activity. Is checked for having the 'START_IN_RECENTS_SCREEN'-extra.
     * @return true if the activity should be started and pushed to the recents screen, false otherwise.
     */
    private fun shouldAddToRecentsScreen(intent: Intent?): Boolean {
        intent?.toSafeIntent()?.let {
            return it.getBooleanExtra(START_IN_RECENTS_SCREEN, false)
        }
        return false
    }

    private fun createSplashScreenOperation(shouldShowOnboarding: Boolean): SplashScreenOperation {
        val nimbusOperation =
            if (FxNimbus.features.splashScreen.value().offTrainOnboarding) {
                ApplyExperimentsOperation(
                    storage = DefaultExperimentsOperationStorage(components.settings),
                    nimbus = components.nimbus.sdk,
                )
            } else {
                FetchExperimentsOperation(
                    storage = DefaultExperimentsOperationStorage(components.settings),
                    nimbus = components.nimbus.sdk,
                )
            }

        if (shouldShowOnboarding) {
            InstallReferrerHandlingService(applicationContext).start()
        }

        return nimbusOperation
    }

    private fun setupTheme() {
        themeManager = createThemeManager()
        // ExternalAppBrowserActivity exclusively handles it's own theming unless in private mode.
        if (this !is ExternalAppBrowserActivity || browsingModeManager.mode.isPrivate) {
            themeManager.setActivityTheme(this)
            themeManager.applyStatusBarTheme(this)
        }
    }

    // Stop active media when activity is destroyed.
    private fun stopMediaSession() {
        if (isFinishing) {
            components.core.store.state.tabs.forEach {
                it.mediaSessionState?.controller?.stop()
            }

            components.core.store.state.findActiveMediaTab()?.let {
                components.core.store.dispatch(MediaSessionAction.DeactivatedMediaSessionAction(it.id))
            }
        }
    }

    /**
     * Returns the [supportActionBar], inflating it if necessary. Everyone should call this instead of supportActionBar.
     */
    final override fun getSupportActionBarAndInflateIfNecessary(): ActionBar {
        if (!isToolbarInflated) {
            navigationToolbar = binding.navigationToolbarStub.inflate() as Toolbar

            setSupportActionBar(navigationToolbar)
            // Add ids to this that we don't want to have a toolbar back button
            setupNavigationToolbar()
            setNavigationIcon(iconsR.drawable.mozac_ic_back_24)

            isToolbarInflated = true
        }
        return supportActionBar!!
    }

    @Suppress("SpreadOperator")
    private fun setupNavigationToolbar(vararg topLevelDestinationIds: Int) {
        NavigationUI.setupWithNavController(
            navigationToolbar,
            navHost.navController,
            AppBarConfiguration.Builder(*topLevelDestinationIds).build(),
        )

        navigationToolbar.setNavigationOnClickListener {
            onBackPressedDispatcher.onBackPressed()
        }
    }

    /**
     * Navigates to the browser fragment and loads a URL or performs a search (depending on the value of
     * [searchTermOrURL]).
     *
     * @param searchTermOrURL The entered search term to search or URL to be loaded.
     * @param newTab Whether or not to load the URL in a new tab.
     * @param from The [BrowserDirection] to indicate which fragment the browser is being opened from.
     * @param customTabSessionId Optional custom tab session ID if navigating from a custom tab.
     * @param engine Optional [SearchEngine] to use when performing a search.
     * @param forceSearch Whether or not to force performing a search.
     * @param flags Flags that will be used when loading the URL (not applied to searches).
     * @param historyMetadata The [HistoryMetadataKey] of the new tab in case this tab was opened from history.
     * @param additionalHeaders The extra headers to use when loading the URL.
     */
    @Deprecated(
        message =
            "Use NavController.openToBrowser() and " +
                "components.useCases.fenixBrowserUseCases.loadUrlOrSearch() instead",
        level = DeprecationLevel.WARNING,
    )
    fun openToBrowserAndLoad(
        searchTermOrURL: String,
        newTab: Boolean,
        from: BrowserDirection,
        customTabSessionId: String? = null,
        engine: SearchEngine? = null,
        forceSearch: Boolean = false,
        flags: EngineSession.LoadUrlFlags = EngineSession.LoadUrlFlags.none(),
        historyMetadata: HistoryMetadataKey? = null,
        additionalHeaders: Map<String, String>? = null,
    ) {
        openToBrowser(from, customTabSessionId)

        components.useCases.fenixBrowserUseCases.loadUrlOrSearch(
            searchTermOrURL = searchTermOrURL,
            newTab = newTab,
            forceSearch = forceSearch,
            private = browsingModeManager.mode.isPrivate,
            searchEngine = engine,
            flags = flags,
            historyMetadata = historyMetadata,
            additionalHeaders = additionalHeaders,
        )
    }

    fun openToBrowser(from: BrowserDirection, customTabSessionId: String? = null) {
        if (navHost.navController.alreadyOnDestination(R.id.browserFragment)) return
        @IdRes val fragmentId = if (from.fragmentId != 0) from.fragmentId else null
        val directions = getNavDirections(from, customTabSessionId)
        if (directions != null) {
            navHost.navController.nav(fragmentId, directions)
        }
    }

    @VisibleForTesting
    internal fun navigateToBrowserOnColdStart() {
        if (this is ExternalAppBrowserActivity) {
            return
        }

        // Normal tabs + cold start -> Should go back to browser if we had any tabs open when we left last
        // except for PBM + Cold Start there won't be any tabs since they're evicted so we never will navigate
        if (components.settings.shouldReturnToBrowser && !browsingModeManager.mode.isPrivate) {
            // Navigate to home first (without rendering it) to add it to the back stack.
            openToBrowser(BrowserDirection.FromGlobal, null)
        }
    }

    @VisibleForTesting
    internal fun navigateToHome(navController: NavController) {
        if (this is ExternalAppBrowserActivity) {
            return
        }

        navController.navigate(NavGraphDirections.actionStartupHome())
    }

    @VisibleForTesting
    internal open fun addAboutHomeBinding(lifecycle: Lifecycle) {
        lifecycle.addObserver(aboutHomeBinding)
    }

    @VisibleForTesting
    internal open fun addHomepageTabBinding(lifecycle: Lifecycle) {
        lifecycle.addObserver(homepageTabBinding)
    }

    final override fun attachBaseContext(base: Context) {
        base.components.strictMode.allowViolation(StrictMode::allowThreadDiskReads) {
            super.attachBaseContext(base)
        }
    }

    final override fun getSystemService(name: String): Any? {
        // Issue #17759 had a crash with the PerformanceInflater.kt on Android 5.0 and 5.1
        // when using the TimePicker. Since the inflater was created for performance monitoring
        // purposes and that we test on new android versions, this means that any difference in
        // inflation will be caught on those devices.
        if (LAYOUT_INFLATER_SERVICE == name) {
            if (inflater == null) {
                inflater = PerformanceInflater(LayoutInflater.from(baseContext), this)
            }
            return inflater
        }
        return super.getSystemService(name)
    }

    private fun createBrowsingModeManager(intent: Intent?): BrowsingModeManager {
        return DefaultBrowsingModeManager(
            intent = intent,
            settings = components.settings,
            onModeChange = { newMode ->
                updateSecureWindowFlags(newMode)

                if (::themeManager.isInitialized) {
                    themeManager.currentTheme = newMode
                }

                components.appStore.dispatch(AppAction.BrowsingModeManagerModeChanged(mode = newMode))
            },
        )
    }

    private fun updateSecureWindowFlags(mode: BrowsingMode = browsingModeManager.mode) {
        if (mode == BrowsingMode.Private && !components.settings.shouldSecureModeBeOverridden) {
            window.addFlags(FLAG_SECURE)
        } else {
            window.clearFlags(FLAG_SECURE)
        }
    }

    private fun createThemeManager(): ThemeManager {
        return DefaultThemeManager(browsingModeManager.mode, this)
    }

    private fun openPopup(webExtensionState: WebExtensionState) {
        val action =
            NavGraphDirections.actionGlobalWebExtensionActionPopupFragment(
                webExtensionId = webExtensionState.id,
                webExtensionTitle = webExtensionState.name,
            )
        navHost.navController.navigate(action)
    }

    private fun openOptionsPage(activeOptionsPage: ActiveOptionsPage) {
        if (
            !suppressOptionsPageInAddonManagement(
                navHost.navController.currentDestination?.id,
                activeOptionsPage,
            )
        ) {
            createOpenOptionsPageDirections(activeOptionsPage)?.let {
                navHost.navController.navigate(it)
            }
        }
    }

    @VisibleForTesting
    internal fun suppressOptionsPageInAddonManagement(
        currentDestinationId: Int?,
        activeOptionsPage: ActiveOptionsPage,
    ): Boolean {
        if (currentDestinationId !in ADDON_MANAGEMENT_DESTINATIONS) {
            return false
        }
        findExtensionForOptionsPage(activeOptionsPage)?.let {
            components.core.store.dispatch(WebExtensionAction.ClearOptionsPageSession(it.id))
        }
        return true
    }

    @VisibleForTesting
    internal fun createOpenOptionsPageDirections(activeOptionsPage: ActiveOptionsPage): NavDirections? {
        val extensionState = findExtensionForOptionsPage(activeOptionsPage)

        return extensionState?.let {
            NavGraphDirections.actionGlobalWebExtensionActionOptionsPageFragment(
                optionsPageUrl = activeOptionsPage.url,
                webExtensionName = activeOptionsPage.name,
                webExtensionId = it.id,
            )
        }
    }

    private fun findExtensionForOptionsPage(activeOptionsPage: ActiveOptionsPage): WebExtensionState? =
        components.core.store.state.extensions.values.firstOrNull { it.activeOptionsPage == activeOptionsPage }

    /** The root container is null at this point, so let the HomeActivity know that we are visually complete. */
    fun setVisualCompletenessQueueReady() {
        isVisuallyComplete = true
    }

    private fun captureSnapshotTelemetryMetrics() {
        lifecycleScope.launch {
            val recentlyUsedPwaCount =
                withContext(Dispatchers.IO) {
                    components.core.webAppShortcutManager.recentlyUsedWebAppsCount(
                        activeThresholdMs = PWA_RECENTLY_USED_THRESHOLD
                    )
                }
            if (recentlyUsedPwaCount == 0) {
                Metrics.hasRecentPwas.set(false)
            } else {
                Metrics.hasRecentPwas.set(true)
                // This metric's lifecycle is set to 'application', meaning that it gets reset upon
                // application restart. Combined with the behaviour of the metric type itself (a growing counter),
                // it's important that this metric is only set once per application's lifetime.
                // Otherwise, we're going to over-count.
                Metrics.recentlyUsedPwaCount.add(recentlyUsedPwaCount)
            }
        }
    }

    @VisibleForTesting
    internal fun isActivityColdStarted(startingIntent: Intent, activityIcicle: Bundle?): Boolean {
        // First time opening this activity in the task.
        // Cold start / start from Recents after back press.
        return activityIcicle == null &&
            // Activity was restarted from Recents after it was destroyed by Android while in background
            // in cases of memory pressure / "Don't keep activities".
            startingIntent.flags and Intent.FLAG_ACTIVITY_LAUNCHED_FROM_HISTORY == 0
    }

    /**
     * Indicates if the user should be redirected to the [BrowserFragment] or to the [HomeFragment], links from an
     * external apps should always opened in the [BrowserFragment].
     */
    @VisibleForTesting
    internal fun shouldStartOnHome(intent: Intent? = this.intent): Boolean {
        return components.strictMode.allowViolation(StrictMode::allowThreadDiskReads) {
            // We only want to open on home when users tap the app,
            // we want to ignore other cases when the app gets open by users clicking on links.
            getSettings().shouldStartOnHome() && intent?.action == ACTION_MAIN
        }
    }

    fun processIntent(intent: Intent): Boolean {
        return externalSourceIntentProcessors.any {
            it.process(
                intent,
                navHost.navController,
                this.intent,
                components.settings,
            )
        }
    }

    @VisibleForTesting internal fun getSettings(): Settings = components.settings

    private fun shouldNavigateToBrowserOnColdStart(savedInstanceState: Bundle?): Boolean {
        return isActivityColdStarted(intent, savedInstanceState) && !processIntent(intent)
    }

    private suspend fun showFullscreenMessageIfNeeded(context: Context) {
        val messaging = context.components.nimbus.messaging
        val nextMessage = messaging.getNextMessage(FenixMessageSurfaceId.SURVEY) ?: return
        val researchSurfaceDialogFragment =
            ResearchSurfaceDialogFragment.newInstance(
                keyMessageText = nextMessage.text,
                keyAcceptButtonText = nextMessage.buttonLabel,
                keyDismissButtonText = null,
            )

        researchSurfaceDialogFragment.onAccept = {
            processIntent(messaging.getIntentForMessage(nextMessage))
            components.appStore.dispatch(AppAction.MessagingAction.MessageClicked(nextMessage))
        }

        researchSurfaceDialogFragment.onDismiss = {
            components.appStore.dispatch(AppAction.MessagingAction.MessageDismissed(nextMessage))
        }

        lifecycleScope.launch(Main) {
            researchSurfaceDialogFragment.showNow(
                supportFragmentManager,
                ResearchSurfaceDialogFragment.FRAGMENT_TAG,
            )
        }

        // Update message as displayed.
        val currentBootUniqueIdentifier = BootUtils.getBootIdentifier(context)

        messaging.onMessageDisplayed(nextMessage, currentBootUniqueIdentifier)
    }

    /** Dispatches the received [CrashAction] from [UnsubmittedCrashDialog] */
    override fun dispatchCrashAction(action: CrashAction) {
        components.appStore.dispatch(AppAction.CrashActionWrapper(action))
    }

    private fun showCrashReporter(crashIDs: List<String>?) {
        UnsubmittedCrashDialog.create(crashIDs = crashIDs).show(supportFragmentManager, UnsubmittedCrashDialog.TAG)
    }

    companion object {
        const val OPEN_TO_BROWSER = "open_to_browser"
        const val OPEN_TO_BROWSER_AND_LOAD = "open_to_browser_and_load"
        const val OPEN_TO_SEARCH = "open_to_search"
        const val PRIVATE_BROWSING_MODE = "private_browsing_mode"
        const val START_IN_RECENTS_SCREEN = "start_in_recents_screen"
        const val OPEN_PASSWORD_MANAGER = "open_password_manager"
        const val UNINSTALL_SURVEY = "uninstall_survey"
        const val APP_ICON = "APP_ICON"

        // Intent extra to force onboarding to show in the benchmark build, where it is otherwise suppressed
        const val EXTRA_FORCE_ONBOARDING = "EXTRA_FORCE_ONBOARDING"

        // PWA must have been used within last 30 days to be considered "recently used" for the
        // telemetry purposes.
        private const val PWA_RECENTLY_USED_THRESHOLD = DateUtils.DAY_IN_MILLIS * 30L

        private const val REQUEST_CODE_CAMERA_PERMISSIONS = 1

        private val ADDON_MANAGEMENT_DESTINATIONS =
            setOf(
                R.id.addonsManagementFragment,
                R.id.installedAddonDetailsFragment,
                R.id.addonInternalSettingsFragment,
                R.id.addonDetailsFragment,
                R.id.addonPermissionsDetailFragment,
            )
    }
}
