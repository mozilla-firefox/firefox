/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home

import android.annotation.SuppressLint
import android.content.Intent
import android.net.ConnectivityManager
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.activity.result.ActivityResultLauncher
import androidx.activity.result.contract.ActivityResultContracts
import androidx.annotation.VisibleForTesting
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.displayCutoutPadding
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.systemBarsPadding
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.ComposeView
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.ViewCompositionStrategy
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.content.getSystemService
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.lifecycle.Observer
import androidx.lifecycle.lifecycleScope
import androidx.navigation.NavController
import androidx.navigation.fragment.findNavController
import androidx.navigation.fragment.navArgs
import kotlinx.coroutines.Dispatchers.IO
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.launch
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.base.snackbar.Snackbar
import mozilla.components.compose.base.snackbar.displaySnackbar
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarState
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarStore
import mozilla.components.concept.engine.utils.ABOUT_HOME_URL
import mozilla.components.concept.sync.AccountObserver
import mozilla.components.concept.sync.AuthType
import mozilla.components.concept.sync.OAuthAccount
import mozilla.components.feature.accounts.push.SendTabUseCases
import mozilla.components.feature.tab.collections.TabCollection
import mozilla.components.feature.top.sites.presenter.DefaultTopSitesPresenter
import mozilla.components.lib.state.ext.flow
import mozilla.components.lib.state.ext.observeAsComposableState
import mozilla.components.service.nimbus.messaging.Message
import mozilla.components.support.base.feature.ViewBoundFeatureWrapper
import mozilla.components.support.ktx.android.view.toScope
import mozilla.components.support.utils.BuildManufacturerChecker
import mozilla.components.support.utils.DateTimeProvider
import mozilla.components.support.utils.DefaultDateTimeProvider
import mozilla.components.support.utils.ext.navigateToDefaultBrowserAppsSettings
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.fenix.BrowserDirection
import org.mozilla.fenix.GleanMetrics.HomeScreen
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.NavGraphDirections
import org.mozilla.fenix.R
import org.mozilla.fenix.biometricauthentication.AuthenticationStatus
import org.mozilla.fenix.biometricauthentication.BiometricAuthenticationManager
import org.mozilla.fenix.browser.BrowserFragmentDirections
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.browser.tabstrip.TabStrip
import org.mozilla.fenix.browser.tabstrip.TabStripColors
import org.mozilla.fenix.components.Components
import org.mozilla.fenix.components.HomepageThumbnailIntegration
import org.mozilla.fenix.components.LensFeature
import org.mozilla.fenix.components.QrScanFenixFeature
import org.mozilla.fenix.components.TabCollectionStorage
import org.mozilla.fenix.components.VoiceSearchFeature
import org.mozilla.fenix.components.accounts.FenixFxAEntryPoint
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.AppAction.ContentRecommendationsAction
import org.mozilla.fenix.components.appstate.AppAction.MessagingAction
import org.mozilla.fenix.components.appstate.AppAction.MessagingAction.MicrosurveyAction
import org.mozilla.fenix.components.appstate.AppAction.ReviewPromptAction.CheckIfEligibleForReviewPrompt
import org.mozilla.fenix.components.appstate.AppAction.SportsWidgetAction
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.components.components
import org.mozilla.fenix.components.metrics.installSourcePackage
import org.mozilla.fenix.components.toolbar.ToolbarPosition
import org.mozilla.fenix.compose.snackbar.SnackbarState
import org.mozilla.fenix.ext.application
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.getRootView
import org.mozilla.fenix.ext.hideToolbar
import org.mozilla.fenix.ext.isOnline
import org.mozilla.fenix.ext.nav
import org.mozilla.fenix.ext.recordEventInNimbus
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.ext.tabClosedUndoMessage
import org.mozilla.fenix.home.bookmarks.BookmarksFeature
import org.mozilla.fenix.home.bookmarks.controller.DefaultBookmarksController
import org.mozilla.fenix.home.ext.showWallpaperOnboardingDialog
import org.mozilla.fenix.home.logo.LogoController
import org.mozilla.fenix.home.logo.TrackingProtectionController
import org.mozilla.fenix.home.pocket.controller.DefaultPocketStoriesController
import org.mozilla.fenix.home.privatebrowsing.controller.DefaultPrivateBrowsingController
import org.mozilla.fenix.home.recentsyncedtabs.RecentSyncedTabFeature
import org.mozilla.fenix.home.recentsyncedtabs.controller.DefaultRecentSyncedTabController
import org.mozilla.fenix.home.recenttabs.RecentTabsListFeature
import org.mozilla.fenix.home.recenttabs.controller.DefaultRecentTabsController
import org.mozilla.fenix.home.recentvisits.RecentVisitsFeature
import org.mozilla.fenix.home.recentvisits.controller.DefaultRecentVisitsController
import org.mozilla.fenix.home.search.DefaultHomeSearchController
import org.mozilla.fenix.home.sessioncontrol.DefaultSessionControlController
import org.mozilla.fenix.home.sessioncontrol.SessionControlController
import org.mozilla.fenix.home.sessioncontrol.SessionControlControllerCallback
import org.mozilla.fenix.home.sessioncontrol.SessionControlInteractor
import org.mozilla.fenix.home.sports.DefaultSportsController
import org.mozilla.fenix.home.sports.SportCardErrorState
import org.mozilla.fenix.home.store.HomeToolbarStoreBuilder
import org.mozilla.fenix.home.store.HomepageState
import org.mozilla.fenix.home.termsofuse.DefaultPrivacyNoticeBannerController
import org.mozilla.fenix.home.toolbar.DefaultToolbarController
import org.mozilla.fenix.home.toolbar.FenixHomeToolbar
import org.mozilla.fenix.home.toolbar.HomeNavigationBar
import org.mozilla.fenix.home.toolbar.HomeToolbarComposable
import org.mozilla.fenix.home.toolbar.HomeToolbarComposable.Companion.DirectToSearchConfig
import org.mozilla.fenix.home.topsites.DefaultTopSitesView
import org.mozilla.fenix.home.topsites.TopSitesBinding
import org.mozilla.fenix.home.topsites.controller.DefaultTopSiteController
import org.mozilla.fenix.home.topsites.getTopSitesConfig
import org.mozilla.fenix.home.ui.Homepage
import org.mozilla.fenix.home.ui.WallpaperBackground
import org.mozilla.fenix.messaging.DefaultMessageController
import org.mozilla.fenix.messaging.FenixMessageSurfaceId
import org.mozilla.fenix.messaging.MessagingFeature
import org.mozilla.fenix.microsurvey.ui.MicrosurveyRequestPrompt
import org.mozilla.fenix.microsurvey.ui.ext.toMicrosurveyUIData
import org.mozilla.fenix.nimbus.FxNimbus
import org.mozilla.fenix.onboarding.OnboardingFragmentDirections
import org.mozilla.fenix.onboarding.OnboardingReason
import org.mozilla.fenix.onboarding.OnboardingTelemetryRecorder
import org.mozilla.fenix.onboarding.continuous.ContinuousOnboardingFeatureDefault
import org.mozilla.fenix.onboarding.continuous.ContinuousOnboardingStageProviderDefault
import org.mozilla.fenix.pbmlock.NavigationOrigin
import org.mozilla.fenix.pbmlock.observePrivateModeLock
import org.mozilla.fenix.perf.MarkersFragmentLifecycleCallbacks
import org.mozilla.fenix.perf.StartupTimeline
import org.mozilla.fenix.reviewprompt.ShowReviewPromptBinding
import org.mozilla.fenix.search.awesomebar.AwesomeBarComposable
import org.mozilla.fenix.snackbar.FenixSnackbarDelegate
import org.mozilla.fenix.snackbar.SnackbarBinding
import org.mozilla.fenix.tabstray.redux.state.Page
import org.mozilla.fenix.tabstray.ui.AccessPoint
import org.mozilla.fenix.termsofuse.store.DefaultPrivacyNoticeBannerRepository
import org.mozilla.fenix.termsofuse.store.PrivacyNoticeBannerAction
import org.mozilla.fenix.termsofuse.store.PrivacyNoticeBannerMiddleware
import org.mozilla.fenix.termsofuse.store.PrivacyNoticeBannerState
import org.mozilla.fenix.termsofuse.store.PrivacyNoticeBannerStore
import org.mozilla.fenix.termsofuse.store.PrivacyNoticeBannerTelemetryMiddleware
import org.mozilla.fenix.termsofuse.store.Surface
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.trackingprotection.TrackersBlockedFeature
import org.mozilla.fenix.utils.Settings
import org.mozilla.fenix.utils.allowUndo
import org.mozilla.fenix.utils.getUndoDelay
import org.mozilla.fenix.utils.showAddSearchWidgetPromptIfSupported
import org.mozilla.fenix.wallpapers.Wallpaper
import java.lang.ref.WeakReference
import org.mozilla.fenix.ipprotection.store.Surface as IPProtectionSurface

/**
 * The home screen.
 */
@Suppress("TooManyFunctions", "LargeClass")
class HomeFragment : Fragment() {
    private val args by navArgs<HomeFragmentArgs>()

    @VisibleForTesting
    internal lateinit var bundleArgs: Bundle

    private val homeViewModel: HomeScreenViewModel by activityViewModels()

    private val snackbarHostState = SnackbarHostState()

    @VisibleForTesting
    internal var homeNavigationBar: HomeNavigationBar? = null

    private var awesomeBarComposable: AwesomeBarComposable? = null

    private val browsingModeManager get() = (activity as HomeActivity).browsingModeManager

    private val collectionStorageObserver = object : TabCollectionStorage.Observer {
        @SuppressLint("NotifyDataSetChanged")
        override fun onTabsAdded(tabCollection: TabCollection, sessions: List<TabSessionState>) {
            if (sessions.size == 1) {
                showComposeSnackbar(
                    SnackbarState(
                        message = requireContext().getString(R.string.create_collection_tab_saved_2),
                        duration = SnackbarState.Duration.Preset.Long,
                    ),
                )
            }
        }
    }

    private val store: BrowserStore
        get() = requireComponents.core.store

    private val privacyNoticeBannerRepository by lazy {
        DefaultPrivacyNoticeBannerRepository(
            settings = requireComponents.settings,
        )
    }
    private val dateTimeProvider: DateTimeProvider by lazy { DefaultDateTimeProvider() }

    private lateinit var privacyNoticeBannerStore: PrivacyNoticeBannerStore

    private var _sessionControlController: SessionControlController? = null
    private val sessionControlController: SessionControlController
        get() = _sessionControlController!!

    private var _sessionControlInteractor: SessionControlInteractor? = null
    private val sessionControlInteractor: SessionControlInteractor
        get() = _sessionControlInteractor!!

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal var nullableToolbarView: FenixHomeToolbar? = null

    private val toolbarView: FenixHomeToolbar
        get() = nullableToolbarView!!

    @VisibleForTesting
    internal val messagingFeatureHomescreen = ViewBoundFeatureWrapper<MessagingFeature>()

    @VisibleForTesting
    internal val messagingFeatureMicrosurvey = ViewBoundFeatureWrapper<MessagingFeature>()

    private val recentTabsListFeature = ViewBoundFeatureWrapper<RecentTabsListFeature>()
    private val recentSyncedTabFeature = ViewBoundFeatureWrapper<RecentSyncedTabFeature>()
    private val bookmarksFeature = ViewBoundFeatureWrapper<BookmarksFeature>()
    private val historyMetadataFeature = ViewBoundFeatureWrapper<RecentVisitsFeature>()
    private val tabsCleanupFeature = ViewBoundFeatureWrapper<TabsCleanupFeature>()
    private val thumbnailsFeature = ViewBoundFeatureWrapper<HomepageThumbnailIntegration>()
    private val snackbarBinding = ViewBoundFeatureWrapper<SnackbarBinding>()
    private val showReviewPromptBinding = ViewBoundFeatureWrapper<ShowReviewPromptBinding>()
    private val topSitesBinding = ViewBoundFeatureWrapper<TopSitesBinding>()
    private val trackersBlockedFeature = ViewBoundFeatureWrapper<TrackersBlockedFeature>()

    private val homepageEdgeToEdgeFeature = ViewBoundFeatureWrapper<HomepageEdgeToEdgeFeature>()
    private var qrScanFenixFeature: ViewBoundFeatureWrapper<QrScanFenixFeature>? =
        ViewBoundFeatureWrapper()
    private val qrScanLauncher: ActivityResultLauncher<Intent> =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
            qrScanFenixFeature?.get()?.handleToolbarQrScanResults(result.resultCode, result.data)
        }
    private var voiceSearchFeature: ViewBoundFeatureWrapper<VoiceSearchFeature>? =
        ViewBoundFeatureWrapper()
    private val voiceSearchLauncher: ActivityResultLauncher<Intent> =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
            voiceSearchFeature?.get()?.handleVoiceSearchResult(result.resultCode, result.data)
        }
    private var lensFeature: ViewBoundFeatureWrapper<LensFeature>? =
        ViewBoundFeatureWrapper()
    private val lensLauncher: ActivityResultLauncher<Intent> =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
            lensFeature?.get()?.handleImageResult(result.resultCode, result.data)
        }
    private val lensCameraPermissionLauncher: ActivityResultLauncher<String> =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { isGranted ->
            lensFeature?.get()?.onCameraPermissionResult(isGranted)
        }

    private val destinationChangedListener =
        NavController.OnDestinationChangedListener { _, destination, _ ->
            if (destination.id != R.id.homeFragment) {
                privacyNoticeBannerStore.dispatch(PrivacyNoticeBannerAction.OnNavigatedAwayFromHome)
            }
        }

    private val setToDefaultPromptRequestLauncher: ActivityResultLauncher<Intent> =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
            with(requireContext()) {
                maybeNavigateToSystemSetToDefaultAction(
                    resultCode = result.resultCode,
                    settings = settings(),
                    dateTimeProvider = dateTimeProvider,
                    isChecklistTask = true,
                ) {
                    navigateToDefaultBrowserAppsSettings(BuildManufacturerChecker())
                }
            }
        }

    private val continuousOnboardingDefaultBrowserLauncher: ActivityResultLauncher<Intent> =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
            continuousOnboardingFeature.onDefaultBrowserStepCompleted(
                activity = requireActivity(),
                resultCode = result.resultCode,
            )
        }

    private val telemetryRecorder by lazy {
        OnboardingTelemetryRecorder(
            onboardingReason = if (requireComponents.settings.enablePersistentOnboarding) {
                OnboardingReason.EXISTING_USER
            } else {
                OnboardingReason.NEW_USER
            },
            installSource = installSourcePackage(
                packageManager = requireContext().application.packageManager,
                packageName = requireContext().application.packageName,
            ),
        )
    }

    private val continuousOnboardingFeature by lazy {
        val settings = requireContext().settings()
        ContinuousOnboardingFeatureDefault(
            settings = settings,
            telemetryRecorder = telemetryRecorder,
            stageProvider = ContinuousOnboardingStageProviderDefault(settings),
            navigateToSyncSignIn = {
                findNavController().nav(
                    id = R.id.homeFragment,
                    directions = OnboardingFragmentDirections.actionGlobalTurnOnSync(
                        entrypoint = FenixFxAEntryPoint.NewUserOnboarding,
                    ),
                )
            },
        )
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        // DO NOT ADD ANYTHING ABOVE THIS getProfilerTime CALL!
        val profilerStartTime = requireComponents.core.engine.profiler?.getProfilerTime()

        super.onCreate(savedInstanceState)

        bundleArgs = args.toBundle()
        if (savedInstanceState != null) {
            bundleArgs.putBoolean(FOCUS_ON_ADDRESS_BAR, false)
        }
        bundleArgs.getString(SESSION_TO_DELETE)?.let {
            homeViewModel.sessionToDelete = it
        }

        // DO NOT MOVE ANYTHING BELOW THIS addMarker CALL!
        requireComponents.core.engine.profiler?.addMarker(
            MarkersFragmentLifecycleCallbacks.MARKER_NAME,
            profilerStartTime,
            "HomeFragment.onCreate",
        )
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View {
        // DO NOT ADD ANYTHING ABOVE THIS getProfilerTime CALL!
        val profilerStartTime = requireComponents.core.engine.profiler?.getProfilerTime()

        val activity = activity as HomeActivity
        val view = ComposeView(activity).apply {
            id = R.id.homepageView
            setViewCompositionStrategy(ViewCompositionStrategy.DisposeOnViewTreeLifecycleDestroyed)
        }

        nullableToolbarView = buildToolbar(activity, view)
        initComposeHomepage(view = view, activity = activity)

        // DO NOT MOVE ANYTHING BELOW THIS addMarker CALL!
        requireComponents.core.engine.profiler?.addMarker(
            MarkersFragmentLifecycleCallbacks.MARKER_NAME,
            profilerStartTime,
            "HomeFragment.onCreateView",
        )
        return view
    }

    private fun buildToolbar(activity: HomeActivity, view: View): FenixHomeToolbar {
        val toolbarStore by buildToolbarStore(activity)

        if (homepageEdgeToEdgeFeature.get() == null) {
            homepageEdgeToEdgeFeature.set(
                feature = HomepageEdgeToEdgeFeature(
                    appStore = requireComponents.appStore,
                    activity = activity,
                    settings = activity.settings(),
                    browsingModeManager = browsingModeManager,
                    toolbarStore = toolbarStore,
                ),
                owner = viewLifecycleOwner,
                view = view,
            )
        }

        homeNavigationBar = HomeNavigationBar(
            context = activity,
            toolbarStore = toolbarStore,
            settings = activity.settings(),
            hideWhenKeyboardShown = true,
        )

        return HomeToolbarComposable(
            context = activity,
            navController = findNavController(),
            toolbarStore = toolbarStore,
            appStore = activity.components.appStore,
            browserStore = activity.components.core.store,
            browsingModeManager = activity.browsingModeManager,
            settings = activity.settings(),
            directToSearchConfig = DirectToSearchConfig(
                startSearch = bundleArgs.getBoolean(FOCUS_ON_ADDRESS_BAR) ||
                        FxNimbus.features.oneClickSearch.value().enabled,
                startVoiceSearch = bundleArgs.getBoolean(START_VOICE_SEARCH),
                sessionId = args.sessionToStartSearchFor,
                source = args.searchAccessPoint,
            ),
            coroutineScope = view.toScope(),
            tabStripContent = { TabStrip(toolbarStore) },
            searchSuggestionsContent = { modifier ->
                (awesomeBarComposable ?: initializeAwesomeBarComposable(toolbarStore, modifier))
                    ?.SearchSuggestions()
            },
        )
    }

    private fun buildToolbarStore(activity: HomeActivity) = HomeToolbarStoreBuilder.build(
        context = activity,
        fragment = this,
        navController = findNavController(),
        appStore = requireContext().components.appStore,
        browserStore = requireContext().components.core.store,
        browsingModeManager = activity.browsingModeManager,
    )

    private fun initMessagingFeature(view: View) {
        if (requireContext().settings().isExperimentationEnabled) {
            messagingFeatureHomescreen.set(
                feature = MessagingFeature(
                    appStore = requireComponents.appStore,
                    surface = FenixMessageSurfaceId.HOMESCREEN,
                    runWhenReadyQueue = requireComponents.performance.visualCompletenessQueue,
                ),
                owner = viewLifecycleOwner,
                view = view,
            )

            initializeMicrosurveyFeature(requireContext().settings().microsurveyFeatureEnabled, view)
        }
    }

    @VisibleForTesting
    internal fun initializeMicrosurveyFeature(isMicrosurveyEnabled: Boolean, view: View) {
        if (isMicrosurveyEnabled) {
            messagingFeatureMicrosurvey.set(
                feature = MessagingFeature(
                    appStore = requireComponents.appStore,
                    surface = FenixMessageSurfaceId.MICROSURVEY,
                    runWhenReadyQueue = requireComponents.performance.visualCompletenessQueue,
                ),
                owner = viewLifecycleOwner,
                view = view,
            )
        }
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        // DO NOT ADD ANYTHING ABOVE THIS getProfilerTime CALL!
        val profilerStartTime = requireComponents.core.engine.profiler?.getProfilerTime()

        super.onViewCreated(view, savedInstanceState)

        recordHomepageTelemetry()

        observePrivateModeLock {
            findNavController().navigate(
                NavGraphDirections.actionGlobalUnlockPrivateTabsFragment(NavigationOrigin.HOME_PAGE),
            )
        }

        toolbarView.build(requireContext().settings().enableHomepageSearchBar)

        requireComponents.appStore.state.wasLastTabClosedPrivate?.also {
            showUndoSnackbar(requireContext().tabClosedUndoMessage(it))
            requireComponents.appStore.dispatch(AppAction.TabStripAction.UpdateLastTabClosed(null))
        }

        qrScanFenixFeature = QrScanFenixFeature.register(this, qrScanLauncher)
        voiceSearchFeature = VoiceSearchFeature.register(this, voiceSearchLauncher)
        lensFeature = LensFeature.register(this, lensLauncher, lensCameraPermissionLauncher)

        initStoriesState()
        initMessagingFeature(view = view)
        initTopSitesBinding(view = view)
        initRecentTabsListFeature(view = view)
        initPrivacyReportFeature(view = view)
        initBookmarksFeature(view = view)
        initHistoryMetadataFeature(view = view)
        initThumbnailsFeature(view = view)
        initReviewPromptBinding(view = view)
        initTabsCleanupFeature(view = view)
        initSnackbarBinding(view = view)

        privacyNoticeBannerStore = PrivacyNoticeBannerStore(
            initialState = PrivacyNoticeBannerState(
                visible = privacyNoticeBannerRepository.shouldShowPrivacyNoticeBanner(),
            ),
            middleware = listOf(
                PrivacyNoticeBannerMiddleware(
                    repository = privacyNoticeBannerRepository,
                ),
                PrivacyNoticeBannerTelemetryMiddleware(),
            ),
        )

        initController()
        initInteractor()

        continuousOnboardingFeature.maybeRunContinuousOnboarding(
            activity = requireActivity(),
            launcher = continuousOnboardingDefaultBrowserLauncher,
        )

        // DO NOT MOVE ANYTHING BELOW THIS addMarker CALL!
        requireComponents.core.engine.profiler?.addMarker(
            MarkersFragmentLifecycleCallbacks.MARKER_NAME,
            profilerStartTime,
            "HomeFragment.onViewCreated",
        )
    }

    @Suppress("LongMethod")
    private fun initComposeHomepage(
        view: ComposeView,
        activity: HomeActivity,
    ) {
        view.setContent {
            FirefoxTheme {
                val settings = LocalContext.current.settings()
                val appState = with(components.appStore) {
                    remember {
                        // Ignore AppState changes where only the browsing mode differs.
                        // This avoids unnecessary recompositions triggered by theme/browsing mode transitions,
                        // which are handled outside Compose via ThemeManager recreating the activity.
                        // Without this, transient states can cause visual glitches (e.g., incorrect theme/frame)
                        flow().distinctUntilChanged { old, new -> old.mode != new.mode }
                    }.collectAsState(state)
                }
                val privacyNoticeBannerState = privacyNoticeBannerStore.flow().collectAsState(
                    initial = privacyNoticeBannerStore.state,
                )
                val isToolbarAtTop = settings.toolbarPosition == ToolbarPosition.TOP

                val isMicrosurveyDismissed by activity.isMicrosurveyPromptDismissed
                val microsurveyVisible by remember(isMicrosurveyDismissed) {
                    derivedStateOf {
                        settings.microsurveyFeatureEnabled &&
                            !appState.value.mode.isPrivate &&
                            !isMicrosurveyDismissed &&
                            appState.value.messaging.messageToShow.containsKey(
                                FenixMessageSurfaceId.MICROSURVEY,
                            )
                    }
                }

                LaunchedEffect(microsurveyVisible) {
                    settings.shouldShowMicrosurveyPrompt = microsurveyVisible
                }

                Scaffold(
                    modifier = Modifier
                        .fillMaxSize()
                        .systemBarsPadding()
                        .displayCutoutPadding()
                        .imePadding(),
                    topBar = {
                        if (isToolbarAtTop) {
                            AndroidView(factory = { toolbarView.layout })
                        }
                    },
                    bottomBar = {
                        if (isToolbarAtTop) {
                            homeNavigationBar?.let { navBar ->
                                AndroidView(factory = { navBar.layout })
                            }
                        } else {
                            Column {
                                AndroidView(factory = { toolbarView.layout })

                                homeNavigationBar?.let { navBar ->
                                    AndroidView(factory = { navBar.layout })
                                }
                            }
                        }
                    },
                    containerColor = Color.Transparent,
                ) { innerPadding ->
                    HomeContent(
                        appState = appState.value,
                        privacyNoticeBannerState = privacyNoticeBannerState.value,
                        settings = settings,
                        innerPadding = innerPadding,
                        microsurveyVisible = microsurveyVisible,
                        microsurveyMessage = appState.value.messaging.messageToShow[
                            FenixMessageSurfaceId.MICROSURVEY,
                        ],
                        onMicrosurveyDismiss = {
                            activity.isMicrosurveyPromptDismissed.value = true
                        },
                    )
                }

                LaunchedEffect(Unit) {
                    onFirstHomepageFrameDrawn()
                }
            }
        }
    }

    @Composable
    private fun HomeContent(
        appState: AppState,
        privacyNoticeBannerState: PrivacyNoticeBannerState,
        settings: Settings,
        innerPadding: PaddingValues,
        microsurveyVisible: Boolean,
        microsurveyMessage: Message?,
        onMicrosurveyDismiss: () -> Unit,
    ) {
        Box(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding),
        ) {
            if (!appState.mode.isPrivate) {
                WallpaperBackground(
                    wallpaper = appState.wallpaperState.currentWallpaper,
                    loadBitmap = components.useCases.wallpaperUseCases.loadBitmap::invoke,
                    onLoadFailed = {
                        requireContext().settings().currentWallpaperTextColor = 0L
                        showComposeSnackbar(
                            SnackbarState(
                                message = resources.getString(
                                    R.string.wallpaper_select_error_snackbar_message,
                                ),
                            ),
                        )
                    },
                )
            }

            Homepage(
                state = HomepageState.build(
                    appState = appState,
                    privacyNoticeBannerState = privacyNoticeBannerState,
                    settings = settings,
                    browsingModeManager = browsingModeManager,
                ),
                interactor = sessionControlInteractor,
                onTopSitesItemBound = {
                    StartupTimeline.onTopSitesItemBound(activity = (requireActivity() as HomeActivity))
                },
            )

            if (microsurveyVisible) {
                MicrosurveyPrompt(
                    message = microsurveyMessage,
                    modifier = Modifier.align(Alignment.BottomCenter),
                    onDismiss = onMicrosurveyDismiss,
                )
            }

            SnackbarHost(
                hostState = snackbarHostState,
                modifier = Modifier.align(Alignment.BottomCenter),
            ) { snackbarData ->
                Snackbar(snackbarData = snackbarData)
            }
        }
    }

    @Composable
    private fun MicrosurveyPrompt(
        message: Message?,
        modifier: Modifier = Modifier,
        onDismiss: () -> Unit,
    ) {
        val microsurvey = remember(message?.id) { message?.toMicrosurveyUIData() } ?: return
        val appStore = requireComponents.appStore
        val navController = findNavController()

        Column(modifier = modifier) {
            HorizontalDivider()

            MicrosurveyRequestPrompt(
                microsurvey = microsurvey,
                onStartSurveyClicked = {
                    appStore.dispatch(MicrosurveyAction.Started(microsurvey.id))
                    navController.nav(
                        R.id.homeFragment,
                        HomeFragmentDirections.actionGlobalMicrosurveyDialog(microsurvey.id),
                    )
                },
                onCloseButtonClicked = {
                    appStore.dispatch(MicrosurveyAction.Dismissed(microsurvey.id))
                    onDismiss()
                },
            )
        }
    }

    private fun onFirstHomepageFrameDrawn() {
        val components = requireContext().components
        val appStore = components.appStore
        val appState = appStore.state

        with(components.settings) {
            if (showWallpaperOnboardingDialog()) {
                sessionControlInteractor.showWallpapersOnboardingDialog(
                    appState.wallpaperState,
                )
            }
        }

        if (!appStore.state.mode.isPrivate) {
            sessionControlInteractor.reportSessionMetrics(state = appState)
        }

        // We want some parts of the home screen UI to be rendered first if they are
        // the most prominent parts of the visible part of the screen.
        // For this reason, we wait for the home screen recycler view to finish it's
        // layout and post an update for when it's best for non-visible parts of the
        // home screen to render itself.
        appStore.dispatch(AppAction.UpdateFirstFrameDrawn(drawn = true))
    }

    @Composable
    private fun TabStrip(toolbarStore: BrowserToolbarStore? = null) {
        // Tabs will not be shown as selected on the homepage when Homepage as a New Tab is not
        // enabled.
        val isSelectDisabled = !requireContext().settings().enableHomepageAsNewTab
        val toolbarState: BrowserToolbarState? = toolbarStore?.observeAsComposableState { it }?.value

        FirefoxTheme {
            TabStrip(
                isSelectDisabled = isSelectDisabled,
                showActionButtons = false,
                tabStripColors = TabStripColors.build(
                    toolbarState = toolbarState,
                    browsingModeManager = (requireActivity() as HomeActivity).browsingModeManager,
                    settings = requireContext().settings(),
                ),
                onAddTabClick = {
                    if (requireContext().settings().enableHomepageAsNewTab) {
                        requireComponents.useCases.fenixBrowserUseCases.addNewHomepageTab(
                            private = (requireActivity() as HomeActivity).browsingModeManager.mode.isPrivate,
                        )
                    } else {
                        sessionControlInteractor.onNavigateSearch()
                    }
                },
                onSelectedTabClick = { url ->
                    if (url != ABOUT_HOME_URL) {
                        (requireActivity() as HomeActivity).openToBrowser(BrowserDirection.FromHome)
                    }
                },
                onLastTabClose = {},
                onCloseTabClick = { isPrivate ->
                    showUndoSnackbar(requireContext().tabClosedUndoMessage(isPrivate))
                },
                onTabCounterClick = { openTabsTray() },
            )
        }
    }

    private fun showUndoSnackbar(message: String) {
        viewLifecycleOwner.lifecycleScope.allowUndo(
            snackbarHostState = snackbarHostState,
            message = message,
            undoActionTitle = requireContext().getString(R.string.snackbar_deleted_undo),
            onCancel = {
                requireComponents.useCases.tabsUseCases.undo.invoke()
                findNavController().navigate(
                    HomeFragmentDirections.actionGlobalBrowser(null),
                )
            },
            operation = {},
            undoDelay = requireContext().getUndoDelay(),
        )
    }

    private fun showComposeSnackbar(snackbarState: SnackbarState) {
        val snackbarData = snackbarState.toSnackbarData()

        viewLifecycleOwner.lifecycleScope.launch {
            snackbarHostState.displaySnackbar(
                visuals = snackbarData.visuals,
                onActionPerformed = { snackbarData.performAction() },
                onDismissPerformed = { snackbarState.onDismiss() },
            )
        }
    }

    override fun onDestroyView() {
        super.onDestroyView()

        nullableToolbarView = null
        homeNavigationBar = null

        _sessionControlController?.unregisterCallback()
        _sessionControlController = null

        _sessionControlInteractor = null
        awesomeBarComposable = null

        bundleArgs.clear()
    }

    override fun onStart() {
        super.onStart()

        val settings = requireContext().settings()
        if (settings.privateModeAndStoriesEntryPointEnabled) {
            settings.incrementNewsButtonForegroundCount()
        }

        findNavController().addOnDestinationChangedListener(destinationChangedListener)

        subscribeToTabCollections()
        updateLastHomeActivity()

        requireComponents.backgroundServices.accountManagerAvailableQueue.runIfReadyOrQueue {
            // By the time this code runs, we may not be attached to a context or have a view lifecycle owner.
            if ((this@HomeFragment).view?.context == null) {
                return@runIfReadyOrQueue
            }

            requireComponents.backgroundServices.accountManager.register(
                object : AccountObserver {
                    override fun onAuthenticated(account: OAuthAccount, authType: AuthType) {
                        if (authType != AuthType.Existing) {
                            showComposeSnackbar(
                                SnackbarState(
                                    message = requireContext().getString(
                                        R.string.onboarding_firefox_account_sync_is_on,
                                    ),
                                ),
                            )
                        }
                    }
                },
                owner = this@HomeFragment.viewLifecycleOwner,
            )
        }

        // We only want this observer live just before we navigate away to the collection creation screen
        requireComponents.core.tabCollectionStorage.unregister(collectionStorageObserver)

        // Trigger review prompt logic and show the appropriate prompt variation if applicable
        requireComponents.appStore.dispatch(CheckIfEligibleForReviewPrompt)

        if (requireComponents.termsOfUseManager.shouldShowTermsOfUsePromptOnHomepage()) {
            findNavController().navigate(
                BrowserFragmentDirections.actionGlobalTermsOfUseDialog(Surface.HOMEPAGE_NEW_TAB),
            )
        } else if (requireComponents.ipProtectionManager.shouldShowIPProtectionPrompt()) {
            findNavController().navigate(
                BrowserFragmentDirections.actionGlobalIpProtectionDialog(IPProtectionSurface.HOMEPAGE),
            )
        }
    }

    @VisibleForTesting
    internal fun removeCollection(tabCollection: TabCollection) {
        lifecycleScope.launch(IO) {
            requireComponents.core.tabCollectionStorage.removeCollection(tabCollection)
        }
    }

    private fun initStoriesState() {
        lifecycleScope.launch(IO) {
            val settings = requireContext().settings()

            val showStories =
                settings.showPocketRecommendationsFeature ||
                    settings.privateModeAndStoriesEntryPointEnabled

            val showSponsoredStories = showStories && settings.showPocketSponsoredStories

            if (showStories) {
                requireComponents.appStore.dispatch(
                    ContentRecommendationsAction.ContentRecommendationsFetched(
                        recommendations = requireComponents.core.pocketStoriesService.getContentRecommendations(),
                    ),
                )
            } else {
                requireComponents.appStore.dispatch(ContentRecommendationsAction.PocketStoriesClean)
            }

            if (showSponsoredStories) {
                requireComponents.appStore.dispatch(
                    ContentRecommendationsAction.SponsoredContentsChange(
                        sponsoredContents = requireComponents.core.pocketStoriesService.getSponsoredContents(),
                    ),
                )
            }
        }
    }

    override fun onResume() {
        super.onResume()

        hideToolbar()

        val components = requireComponents
        // Whenever a tab is selected its last access timestamp is automatically updated by A-C.
        // However, in the case of resuming the app to the home fragment, we already have an
        // existing selected tab, but its last access timestamp is outdated. No action is
        // triggered to cause an automatic update on warm start (no tab selection occurs). So we
        // update it manually here.
        components.useCases.sessionUseCases.updateLastAccess()

        evaluateMessagesForMicrosurvey(components)

        val sportsWidgetState = components.appStore.state.sportsWidgetState
        val needsFetch = sportsWidgetState.hasWorldCupStarted || sportsWidgetState.isOneWeekToWorldCup
        if (sportsWidgetState.isShown && (needsFetch || sportsWidgetState.isCountdownShown)) {
            // Fetches the full tournament schedule once we're within seven days of kickoff
            // or past it. The middleware caches the response so a later team selection
            // re-derives cards without another network call.
            //
            // When offline, skip the fetch and surface ConnectionInterrupted so the widget
            // shows an error card instead of the countdown / promo flow. Countdown mode
            // (pre-7-day window) has no data to fetch, but still flips to the error card
            // when offline so the user knows the widget isn't current. Conversely, when
            // back online with nothing to fetch (countdown phase), clear any stale error
            // so the countdown UI returns without requiring a manual Refresh tap.
            val isOnline = requireContext().getSystemService<ConnectivityManager>()?.isOnline() == true
            val action = when {
                !isOnline -> SportsWidgetAction.FetchFailed(SportCardErrorState.ConnectionInterrupted)
                needsFetch -> SportsWidgetAction.FetchMatches
                else -> SportsWidgetAction.ErrorStateCleared
            }
            components.appStore.dispatch(action)
        }

        BiometricAuthenticationManager.biometricAuthenticationNeededInfo.shouldShowAuthenticationPrompt =
            true
        BiometricAuthenticationManager.biometricAuthenticationNeededInfo.authenticationStatus =
            AuthenticationStatus.NOT_AUTHENTICATED
    }

    private fun evaluateMessagesForMicrosurvey(components: Components) =
        components.appStore.dispatch(MessagingAction.Evaluate(FenixMessageSurfaceId.MICROSURVEY))

    override fun onPause() {
        super.onPause()

        // Counterpart to the update in onResume to keep the last access timestamp of the selected
        // tab up-to-date.
        requireComponents.useCases.sessionUseCases.updateLastAccess()
    }

    override fun onStop() {
        super.onStop()
        updateLastHomeActivity()

        findNavController().removeOnDestinationChangedListener(destinationChangedListener)
    }

    private fun subscribeToTabCollections(): Observer<List<TabCollection>> {
        return Observer<List<TabCollection>> {
            requireComponents.core.tabCollectionStorage.cachedTabCollections = it
            requireComponents.appStore.dispatch(AppAction.CollectionsChange(it))
        }.also { observer ->
            requireComponents.core.tabCollectionStorage.getCollections().observe(this, observer)
        }
    }

    private fun registerCollectionStorageObserver() {
        requireComponents.core.tabCollectionStorage.register(collectionStorageObserver, this)
    }

    private fun openTabsTray() {
        findNavController().nav(
            R.id.homeFragment,
            HomeFragmentDirections.actionGlobalTabManagementFragment(
                page = when (browsingModeManager.mode) {
                    BrowsingMode.Normal -> Page.NormalTabs
                    BrowsingMode.Private -> Page.PrivateTabs
                },
            ),
        )
    }

    /**
     * Shows a prompt to add a search widget to the home screen if supported by the device.
     *
     * This function should be called when the fragment's view is active (e.g., in response
     * to a user interaction). It launches a coroutine within the [viewLifecycleOwner]'s
     * [androidx.lifecycle.LifecycleCoroutineScope] to display the widget prompt using
     * [showAddSearchWidgetPromptIfSupported].
     *
     * The actual display logic, including handling success and failure callbacks, is managed by
     * [showAddSearchWidgetPromptIfSupported].
     */
    private fun showAddSearchWidgetPrompt() {
        viewLifecycleOwner.lifecycleScope.launch {
            showAddSearchWidgetPromptIfSupported(requireActivity())
        }
    }

    internal fun isEdgeToEdgeBackgroundEnabled(): Boolean {
        val settings = requireContext().settings()
        return settings.enableHomepageEdgeToEdgeBackgroundFeature &&
                settings.currentWallpaperName == Wallpaper.EDGE_TO_EDGE
    }

    private fun initializeAwesomeBarComposable(
        toolbarStore: BrowserToolbarStore,
        modifier: Modifier,
    ) = context?.let {
        AwesomeBarComposable(
            activity = requireActivity() as HomeActivity,
            fragment = this,
            modifier = modifier,
            components = requireComponents,
            appStore = requireComponents.appStore,
            browserStore = requireComponents.core.store,
            toolbarStore = toolbarStore,
            navController = findNavController(),
            tabId = args.sessionToStartSearchFor,
            searchAccessPoint = args.searchAccessPoint,
            isEdgeToEdgeBackgroundEnabled = isEdgeToEdgeBackgroundEnabled(),
        ).also {
            awesomeBarComposable = it
        }
    }

    private fun initTopSitesBinding(view: View) {
        if (requireContext().settings().showTopSitesFeature) {
            topSitesBinding.set(
                feature = TopSitesBinding(
                    browserStore = requireComponents.core.store,
                    presenter = DefaultTopSitesPresenter(
                        view = DefaultTopSitesView(
                            appStore = requireComponents.appStore,
                            settings = requireComponents.settings,
                        ),
                        storage = requireComponents.core.topSitesStorage,
                        config = getTopSitesConfig(
                            settings = requireComponents.settings,
                            store = requireComponents.core.store,
                        ),
                    ),
                ),
                owner = viewLifecycleOwner,
                view = view,
            )
        }
    }

    private fun initRecentTabsListFeature(view: View) {
        if (requireContext().settings().showRecentTabsFeature) {
            recentTabsListFeature.set(
                feature = RecentTabsListFeature(
                    browserStore = requireComponents.core.store,
                    appStore = requireComponents.appStore,
                ),
                owner = viewLifecycleOwner,
                view = view,
            )

            recentSyncedTabFeature.set(
                feature = RecentSyncedTabFeature(
                    context = requireContext(),
                    appStore = requireComponents.appStore,
                    syncStore = requireComponents.backgroundServices.syncStore,
                    storage = requireComponents.backgroundServices.syncedTabsStorage,
                    accountManager = requireComponents.backgroundServices.accountManager,
                    historyStorage = requireComponents.core.historyStorage,
                    coroutineScope = viewLifecycleOwner.lifecycleScope,
                ),
                owner = viewLifecycleOwner,
                view = view,
            )
        }
    }

    private fun initPrivacyReportFeature(view: View) {
        if (requireContext().settings().showPrivacyReportFeature) {
            trackersBlockedFeature.set(
                feature = TrackersBlockedFeature(
                    appStore = requireComponents.appStore,
                    fetchTotalTrackersBlocked = requireComponents.useCases
                        .trackingProtectionUseCases.fetchTotalTrackersBlocked,
                ),
                owner = viewLifecycleOwner,
                view = view,
            )
        }
    }

    private fun initBookmarksFeature(view: View) {
        if (requireContext().settings().showBookmarksHomeFeature) {
            bookmarksFeature.set(
                feature = BookmarksFeature(
                    appStore = requireComponents.appStore,
                    bookmarksUseCase = run {
                        requireComponents.useCases.bookmarksUseCases
                    },
                    scope = viewLifecycleOwner.lifecycleScope,
                ),
                owner = viewLifecycleOwner,
                view = view,
            )
        }
    }

    private fun initHistoryMetadataFeature(view: View) {
        if (requireContext().settings().historyMetadataUIFeature) {
            historyMetadataFeature.set(
                feature = RecentVisitsFeature(
                    appStore = requireComponents.appStore,
                    historyMetadataStorage = requireComponents.core.historyStorage,
                    historyHighlightsStorage = requireComponents.core.lazyHistoryStorage,
                    scope = viewLifecycleOwner.lifecycleScope,
                ),
                owner = viewLifecycleOwner,
                view = view,
            )
        }
    }

    private fun initThumbnailsFeature(view: View) {
        thumbnailsFeature.set(
            feature = HomepageThumbnailIntegration(
                context = requireContext(),
                view = view,
                store = requireComponents.core.store,
                appStore = requireComponents.appStore,
            ),
            owner = this,
            view = view,
        )
    }

    private fun initReviewPromptBinding(view: View) {
        showReviewPromptBinding.set(
            feature = ShowReviewPromptBinding(
                appStore = requireComponents.appStore,
                promptController = requireComponents.playStoreReviewPromptController,
                activityRef = WeakReference(activity),
                uiScope = viewLifecycleOwner.lifecycleScope,
                navigationDirection = { findNavController().navigate(it) },
            ),
            owner = viewLifecycleOwner,
            view = view,
        )
    }

    private fun initTabsCleanupFeature(view: View) {
        tabsCleanupFeature.set(
            feature = TabsCleanupFeature(
                context = requireContext(),
                viewModel = homeViewModel,
                browserStore = requireComponents.core.store,
                browsingModeManager = browsingModeManager,
                navController = findNavController(),
                tabsUseCases = requireComponents.useCases.tabsUseCases,
                fenixBrowserUseCases = requireComponents.useCases.fenixBrowserUseCases,
                settings = requireComponents.settings,
                snackbarHostState = snackbarHostState,
                viewLifecycleScope = viewLifecycleOwner.lifecycleScope,
            ),
            owner = viewLifecycleOwner,
            view = view,
        )
    }

    private fun initSnackbarBinding(view: View) {
        snackbarBinding.set(
            feature = SnackbarBinding(
                context = requireContext(),
                browserStore = requireComponents.core.store,
                appStore = requireComponents.appStore,
                snackbarDelegate = FenixSnackbarDelegate(
                    snackbarHostState = snackbarHostState,
                    scope = viewLifecycleOwner.lifecycleScope,
                    context = requireContext(),
                ),
                navController = findNavController(),
                tabsUseCases = requireComponents.useCases.tabsUseCases,
                sendTabUseCases = SendTabUseCases(requireComponents.backgroundServices.accountManager),
                customTabSessionId = null,
            ),
            owner = this,
            view = view,
        )
    }

    @Suppress("LongMethod")
    private fun initInteractor() {
        _sessionControlInteractor = SessionControlInteractor(
            controller = sessionControlController,
            recentTabController = DefaultRecentTabsController(
                selectTabUseCase = requireComponents.useCases.tabsUseCases.selectTab,
                navController = findNavController(),
                appStore = requireComponents.appStore,
            ),
            recentSyncedTabController = DefaultRecentSyncedTabController(
                fenixBrowserUseCases = requireComponents.useCases.fenixBrowserUseCases,
                tabsUseCase = requireComponents.useCases.tabsUseCases,
                navController = findNavController(),
                accessPoint = AccessPoint.HomeRecentSyncedTab,
                appStore = requireComponents.appStore,
                settings = requireComponents.settings,
            ),
            bookmarksController = DefaultBookmarksController(
                navController = findNavController(),
                appStore = requireComponents.appStore,
                browserStore = requireComponents.core.store,
                settings = requireComponents.settings,
                fenixBrowserUseCases = requireComponents.useCases.fenixBrowserUseCases,
                selectTabUseCase = requireComponents.useCases.tabsUseCases.selectTab,
            ),
            recentVisitsController = DefaultRecentVisitsController(
                navController = findNavController(),
                appStore = requireComponents.appStore,
                settings = requireComponents.settings,
                fenixBrowserUseCases = requireComponents.useCases.fenixBrowserUseCases,
                selectOrAddTabUseCase = requireComponents.useCases.tabsUseCases.selectOrAddTab,
                storage = requireComponents.core.historyStorage,
                scope = viewLifecycleOwner.lifecycleScope,
                store = requireComponents.core.store,
            ),
            pocketStoriesController = DefaultPocketStoriesController(
                navControllerRef = WeakReference(findNavController()),
                appStore = requireComponents.appStore,
                settings = requireComponents.settings,
                fenixBrowserUseCases = requireComponents.useCases.fenixBrowserUseCases,
                marsUseCases = requireComponents.useCases.marsUseCases,
                viewLifecycleScope = viewLifecycleOwner.lifecycleScope,
            ),
            privateBrowsingController = DefaultPrivateBrowsingController(
                navController = findNavController(),
                browsingModeManager = browsingModeManager,
                fenixBrowserUseCases = requireComponents.useCases.fenixBrowserUseCases,
                settings = requireComponents.settings,
            ),
            toolbarController = DefaultToolbarController(
                appStore = requireComponents.appStore,
            ),
            homeSearchController = DefaultHomeSearchController(
                appStore = requireComponents.appStore,
            ),
            topSiteController = buildTopSitesController(),
            privacyNoticeBannerController = DefaultPrivacyNoticeBannerController(
                privacyNoticeBannerStore = privacyNoticeBannerStore,
            ),
            trackingProtectionController = TrackingProtectionController(
                navController = findNavController(),
            ),
            logoController = LogoController(
                longFoxFeature = requireComponents.core.longFoxFeature,
                container = requireActivity().getRootView() as? ViewGroup,
            ),
            sportsController = DefaultSportsController(
                appStore = requireComponents.appStore,
                settings = requireComponents.settings,
                navController = findNavController(),
                fenixBrowserUseCases = requireComponents.useCases.fenixBrowserUseCases,
                browserStore = requireComponents.core.store,
                connectivityManager = requireContext().getSystemService<ConnectivityManager>(),
            ),
        )
    }

    private fun buildTopSitesController() = DefaultTopSiteController(
        activityRef = WeakReference(requireActivity()),
        store = store,
        navControllerRef = WeakReference(findNavController()),
        settings = requireComponents.settings,
        addTabUseCase = requireComponents.useCases.tabsUseCases.addTab,
        selectTabUseCase = requireComponents.useCases.tabsUseCases.selectTab,
        fenixBrowserUseCases = requireComponents.useCases.fenixBrowserUseCases,
        topSitesUseCases = requireComponents.useCases.topSitesUseCase,
        marsUseCases = requireComponents.useCases.marsUseCases,
        mozAdsUseCases = requireComponents.useCases.mozAdsUseCases,
        viewLifecycleScope = viewLifecycleOwner.lifecycleScope,
    )

    private fun initController() {
        val activity = activity as HomeActivity
        _sessionControlController = DefaultSessionControlController(
            activityRef = WeakReference(activity),
            settings = requireComponents.settings,
            engine = requireComponents.core.engine,
            messageController = DefaultMessageController(
                appStore = requireComponents.appStore,
                messagingController = requireComponents.nimbus.messaging,
                processIntent = { intent ->
                    intent?.let { startActivity(it) }
                },
            ),
            store = store,
            tabCollectionStorage = requireComponents.core.tabCollectionStorage,
            addTabUseCase = requireComponents.useCases.tabsUseCases.addTab,
            restoreUseCase = requireComponents.useCases.tabsUseCases.restore,
            selectTabUseCase = requireComponents.useCases.tabsUseCases.selectTab,
            reloadUrlUseCase = requireComponents.useCases.sessionUseCases.reload,
            fenixBrowserUseCases = requireComponents.useCases.fenixBrowserUseCases,
            appStore = requireComponents.appStore,
            navControllerRef = WeakReference(findNavController()),
            viewLifecycleScope = viewLifecycleOwner.lifecycleScope,
            shareUseCases = requireComponents.useCases.shareUseCases,
            showAddSearchWidgetPrompt = ::showAddSearchWidgetPrompt,
            requestSetDefaultBrowserPrompt = {
                maybeRequestDefaultBrowserPrompt(
                    WeakReference(activity),
                    setToDefaultPromptRequestLauncher,
                )
            },
        ).apply {
            registerCallback(
                object : SessionControlControllerCallback {
                    override fun registerCollectionStorageObserver() {
                        this@HomeFragment.registerCollectionStorageObserver()
                    }

                    override fun removeCollection(tabCollection: TabCollection) {
                        this@HomeFragment.removeCollection(tabCollection)
                    }

                    override fun showTabTray() {
                        this@HomeFragment.openTabsTray()
                    }
                },
            )
        }
    }

    private fun recordHomepageTelemetry() {
        HomeScreen.homeScreenDisplayed.record(NoExtras())

        with(requireContext()) {
            if (settings().isExperimentationEnabled) {
                recordEventInNimbus("home_screen_displayed")
            }
        }

        HomeScreen.homeScreenViewCount.add()

        if (!browsingModeManager.mode.isPrivate) {
            HomeScreen.standardHomepageViewCount.add()
        }

        FxNimbus.features.homescreen.recordExposure()
    }

    /**
     * Updates the last time the user was active on the [HomeFragment].
     * This is useful to determine if the user has to start on the [HomeFragment]
     * or it should go directly to the [BrowserFragment].
     */
    @VisibleForTesting
    internal fun updateLastHomeActivity() {
        requireContext().settings().lastHomeActivity = System.currentTimeMillis()
    }

    companion object {
        // Navigation arguments passed to HomeFragment
        const val FOCUS_ON_ADDRESS_BAR = "focusOnAddressBar"
        const val START_VOICE_SEARCH = "startVoiceSearch"
        private const val SESSION_TO_DELETE = "sessionToDelete"
    }
}
