/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home

import android.content.Intent
import android.graphics.Rect
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.FrameLayout
import androidx.activity.result.ActivityResultLauncher
import androidx.activity.result.contract.ActivityResultContracts
import androidx.annotation.VisibleForTesting
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.displayCutoutPadding
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.systemBarsPadding
import androidx.compose.material3.ColorScheme
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.boundsInRoot
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.ComposeView
import androidx.compose.ui.platform.ViewCompositionStrategy
import androidx.compose.ui.unit.dp
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.updatePadding
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.lifecycle.Observer
import androidx.lifecycle.lifecycleScope
import androidx.navigation.NavController
import androidx.navigation.fragment.findNavController
import androidx.navigation.fragment.navArgs
import java.lang.ref.WeakReference
import kotlin.math.roundToInt
import kotlinx.coroutines.Dispatchers.IO
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.launch
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.base.modifier.thenConditional
import mozilla.components.compose.base.snackbar.Snackbar
import mozilla.components.compose.base.snackbar.displaySnackbar
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarState
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarStore
import mozilla.components.concept.engine.utils.ABOUT_HOME_URL
import mozilla.components.concept.sync.AccountObserver
import mozilla.components.concept.sync.AuthType
import mozilla.components.concept.sync.OAuthAccount
import mozilla.components.feature.accounts.push.SendTabUseCases
import mozilla.components.feature.ipprotection.IPProtectionWarningBinding
import mozilla.components.feature.tab.collections.TabCollection
import mozilla.components.feature.top.sites.presenter.DefaultTopSitesPresenter
import mozilla.components.lib.state.ext.flow
import mozilla.components.lib.state.ext.observeAsComposableState
import mozilla.components.support.base.feature.UserInteractionHandler
import mozilla.components.support.base.feature.ViewBoundFeatureWrapper
import mozilla.components.support.ktx.android.view.createWindowInsetsController
import mozilla.components.support.ktx.android.view.toScope
import mozilla.components.support.utils.BuildManufacturerChecker
import mozilla.components.support.utils.ColorUtils.isDark
import mozilla.components.support.utils.DateTimeProvider
import mozilla.components.support.utils.DefaultDateTimeProvider
import mozilla.components.support.utils.ext.navigateToDefaultBrowserAppsSettings
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.fenix.BrowserDirection
import org.mozilla.fenix.GleanMetrics.HomeScreen
import org.mozilla.fenix.GleanMetrics.Vpn
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.NavGraphDirections
import org.mozilla.fenix.OnLongPressedListener
import org.mozilla.fenix.R
import org.mozilla.fenix.biometricauthentication.AuthenticationStatus
import org.mozilla.fenix.biometricauthentication.BiometricAuthenticationManager
import org.mozilla.fenix.browser.BrowserFragmentDirections
import org.mozilla.fenix.browser.SwipeGestureLayout
import org.mozilla.fenix.browser.TabPreview
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
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.components.components
import org.mozilla.fenix.components.metrics.installSourcePackage
import org.mozilla.fenix.components.toolbar.ToolbarPosition
import org.mozilla.fenix.components.toolbar.TopDividerColumn
import org.mozilla.fenix.compose.snackbar.SnackbarState
import org.mozilla.fenix.compose.verticalFadingEdges
import org.mozilla.fenix.ext.application
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.hideToolbar
import org.mozilla.fenix.ext.nav
import org.mozilla.fenix.ext.recordEventInNimbus
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.ext.tabClosedUndoMessage
import org.mozilla.fenix.home.bookmarks.BookmarksFeature
import org.mozilla.fenix.home.bookmarks.controller.DefaultBookmarksController
import org.mozilla.fenix.home.collections.migration.CollectionsMigrationCardAction
import org.mozilla.fenix.home.collections.migration.CollectionsMigrationCardMiddleware
import org.mozilla.fenix.home.collections.migration.CollectionsMigrationCardState
import org.mozilla.fenix.home.collections.migration.CollectionsMigrationCardStore
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
import org.mozilla.fenix.home.store.HomeToolbarStoreBuilder
import org.mozilla.fenix.home.store.HomepageState
import org.mozilla.fenix.home.termsofuse.DefaultPrivacyNoticeBannerController
import org.mozilla.fenix.home.toolbar.DefaultToolbarController
import org.mozilla.fenix.home.toolbar.FenixHomeToolbar
import org.mozilla.fenix.home.toolbar.HomeNavigationBar
import org.mozilla.fenix.home.toolbar.HomeToolbarComposable
import org.mozilla.fenix.home.toolbar.HomeToolbarComposable.Companion.DirectToSearchConfig
import org.mozilla.fenix.home.toolbar.WallpaperToolbarScrim
import org.mozilla.fenix.home.toolbar.homepageToolbarColors
import org.mozilla.fenix.home.topsites.DefaultTopSitesView
import org.mozilla.fenix.home.topsites.TopSitesBinding
import org.mozilla.fenix.home.topsites.controller.DefaultTopSiteController
import org.mozilla.fenix.home.topsites.controller.TopSitesSource
import org.mozilla.fenix.home.topsites.getTopSitesConfig
import org.mozilla.fenix.home.ui.HomeSwipeIntegration
import org.mozilla.fenix.home.ui.Homepage
import org.mozilla.fenix.home.ui.WallpaperBackground
import org.mozilla.fenix.ipprotection.store.Surface as IPProtectionSurface
import org.mozilla.fenix.ipprotection.ui.IPProtectionBottomSheetFragment
import org.mozilla.fenix.messaging.DefaultMessageController
import org.mozilla.fenix.messaging.FenixMessageSurfaceId
import org.mozilla.fenix.messaging.MessagingFeature
import org.mozilla.fenix.microsurvey.ui.MicrosurveyRequestPrompt
import org.mozilla.fenix.microsurvey.ui.ext.MicrosurveyUIData
import org.mozilla.fenix.nimbus.FxNimbus
import org.mozilla.fenix.onboarding.OnboardingFragmentDirections
import org.mozilla.fenix.onboarding.OnboardingReason
import org.mozilla.fenix.onboarding.OnboardingTelemetryRecorder
import org.mozilla.fenix.onboarding.continuous.ContinuousOnboardingFeature
import org.mozilla.fenix.pbmlock.NavigationOrigin
import org.mozilla.fenix.pbmlock.observePrivateModeLock
import org.mozilla.fenix.perf.MarkersFragmentLifecycleCallbacks
import org.mozilla.fenix.perf.StartupTimeline
import org.mozilla.fenix.reviewprompt.CustomReviewPromptBottomSheetFragment
import org.mozilla.fenix.reviewprompt.ShowReviewPromptBinding
import org.mozilla.fenix.search.awesomebar.AwesomeBarComposable
import org.mozilla.fenix.snackbar.FenixSnackbarDelegate
import org.mozilla.fenix.snackbar.SnackbarBinding
import org.mozilla.fenix.tabgroups.TabGroupsStrip
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
import org.mozilla.fenix.wallpapers.LocalWallpaperState
import org.mozilla.fenix.wallpapers.Wallpaper

/** The home screen. */
@Suppress("TooManyFunctions", "LargeClass")
class HomeFragment : Fragment(), UserInteractionHandler, OnLongPressedListener {
    private val args by navArgs<HomeFragmentArgs>()

    @VisibleForTesting internal lateinit var bundleArgs: Bundle

    private val homeViewModel: HomeScreenViewModel by activityViewModels()

    private val snackbarHostState = SnackbarHostState()

    @VisibleForTesting internal var homeNavigationBar: HomeNavigationBar? = null

    private var awesomeBarComposable: AwesomeBarComposable? = null

    private val browsingModeManager
        get() = (activity as HomeActivity).browsingModeManager

    private val collectionStorageObserver =
        object : TabCollectionStorage.Observer {
            override fun onTabsAdded(tabCollection: TabCollection, sessions: List<TabSessionState>) {
                if (sessions.size == 1) {
                    showComposeSnackbar(
                        SnackbarState(
                            message = requireContext().getString(R.string.create_collection_tab_saved_2),
                            duration = SnackbarState.Duration.Preset.Long,
                        )
                    )
                }
            }
        }

    private val store: BrowserStore
        get() = requireComponents.core.store

    private val privacyNoticeBannerRepository by lazy {
        DefaultPrivacyNoticeBannerRepository(settings = requireComponents.settings)
    }

    private val dateTimeProvider: DateTimeProvider by lazy { DefaultDateTimeProvider() }

    private lateinit var privacyNoticeBannerStore: PrivacyNoticeBannerStore
    private lateinit var collectionsMigrationCardStore: CollectionsMigrationCardStore

    private var _sessionControlController: SessionControlController? = null

    private var homeTabPreview: TabPreview? = null
    private var homepageComposeView: ComposeView? = null
    private var toolbarBoundsInRoot: Rect? = null
    private var navbarBoundsInRoot: Rect? = null

    private val sessionControlController: SessionControlController
        get() = _sessionControlController!!

    private var _sessionControlInteractor: SessionControlInteractor? = null
    private val sessionControlInteractor: SessionControlInteractor
        get() = _sessionControlInteractor!!

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE) internal var nullableToolbarView: FenixHomeToolbar? = null

    private val toolbarView: FenixHomeToolbar
        get() = nullableToolbarView!!

    // Bounds of the homepage content (excluding the toolbar and system bar padding) used to crop the tab thumbnail.
    private var homepageContentBounds: Rect? = null

    @VisibleForTesting internal val messagingFeatureHomescreen = ViewBoundFeatureWrapper<MessagingFeature>()

    @VisibleForTesting internal val messagingFeatureMicrosurvey = ViewBoundFeatureWrapper<MessagingFeature>()

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
    private val ipProtectionWarningBinding = ViewBoundFeatureWrapper<IPProtectionWarningBinding>()
    private val continuousOnboardingFeature = ViewBoundFeatureWrapper<ContinuousOnboardingFeature>()

    private val homepageEdgeToEdgeFeature = ViewBoundFeatureWrapper<HomepageEdgeToEdgeFeature>()
    private var qrScanFenixFeature: ViewBoundFeatureWrapper<QrScanFenixFeature>? = ViewBoundFeatureWrapper()
    private val qrScanLauncher: ActivityResultLauncher<Intent> =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
            qrScanFenixFeature?.get()?.handleToolbarQrScanResults(result.resultCode, result.data)
        }
    private var voiceSearchFeature: ViewBoundFeatureWrapper<VoiceSearchFeature>? = ViewBoundFeatureWrapper()
    private val voiceSearchLauncher: ActivityResultLauncher<Intent> =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
            voiceSearchFeature?.get()?.handleVoiceSearchResult(result.resultCode, result.data)
        }
    private var lensFeature: ViewBoundFeatureWrapper<LensFeature>? = ViewBoundFeatureWrapper()
    private val lensLauncher: ActivityResultLauncher<Intent> =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
            lensFeature
                ?.get()
                ?.handleCameraActivityResult(
                    result.resultCode,
                    result.data,
                    qrScanFenixFeature?.get(),
                )
        }
    private val lensCameraPermissionLauncher: ActivityResultLauncher<String> =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { isGranted ->
            lensFeature?.get()?.onCameraPermissionResult(isGranted)
        }

    private val destinationChangedListener = NavController.OnDestinationChangedListener { _, destination, _ ->
        if (destination.id != R.id.homeFragment) {
            privacyNoticeBannerStore.dispatch(PrivacyNoticeBannerAction.OnNavigatedAwayFromHome)
        }
    }

    private val setToDefaultPromptRequestLauncher: ActivityResultLauncher<Intent> =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
            with(requireContext()) {
                maybeNavigateToSystemSetToDefaultAction(
                    resultCode = result.resultCode,
                    settings = components.settings,
                    dateTimeProvider = dateTimeProvider,
                    isChecklistTask = true,
                ) {
                    navigateToDefaultBrowserAppsSettings(BuildManufacturerChecker())
                }
            }
        }

    private val continuousOnboardingDefaultBrowserLauncher: ActivityResultLauncher<Intent> =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
            continuousOnboardingFeature.get()?.onDefaultBrowserStepCompleted(result.resultCode)
        }

    private val telemetryRecorder by lazy {
        OnboardingTelemetryRecorder(
            onboardingReason =
                if (requireComponents.settings.enablePersistentOnboarding) {
                    OnboardingReason.EXISTING_USER
                } else {
                    OnboardingReason.NEW_USER
                },
            installSource =
                installSourcePackage(
                    packageManager = requireContext().application.packageManager,
                    packageName = requireContext().application.packageName,
                ),
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
        val composeView =
            ComposeView(activity).apply {
                id = R.id.homepageView
                setViewCompositionStrategy(ViewCompositionStrategy.DisposeOnViewTreeLifecycleDestroyed)
            }
        homepageComposeView = composeView
        nullableToolbarView = buildToolbar(activity, composeView)
        initComposeHomepage(view = composeView)
        val view =
            if (isToolbarSwipeToSwitchTabsEnabled()) {
                wrapInSwipeLayout(activity, composeView)
            } else {
                composeView
            }

        // DO NOT MOVE ANYTHING BELOW THIS addMarker CALL!
        requireComponents.core.engine.profiler?.addMarker(
            MarkersFragmentLifecycleCallbacks.MARKER_NAME,
            profilerStartTime,
            "HomeFragment.onCreateView",
        )
        return view
    }

    private fun wrapInSwipeLayout(
        context: HomeActivity,
        composeView: ComposeView,
    ): SwipeGestureLayout {
        val fill = ViewGroup.LayoutParams.MATCH_PARENT
        val matchParent = { FrameLayout.LayoutParams(fill, fill) }
        return SwipeGestureLayout(context).apply {
            layoutParams = ViewGroup.LayoutParams(fill, fill)
            addView(composeView, matchParent())
            addView(
                TabPreview(context)
                    .apply {
                        visibility = View.GONE
                        isClickable = false
                        isFocusable = false
                        ViewCompat.setOnApplyWindowInsetsListener(this) { v, insets ->
                            val bars = insets.getInsets(WindowInsetsCompat.Type.systemBars())
                            v.updatePadding(
                                left = bars.left,
                                top = bars.top,
                                right = bars.right,
                                bottom = bars.bottom,
                            )
                            insets
                        }
                    }
                    .also { homeTabPreview = it },
                matchParent(),
            )
        }
    }

    private fun buildToolbar(activity: HomeActivity, view: View): FenixHomeToolbar {
        val toolbarStore by buildToolbarStore(activity)

        if (homepageEdgeToEdgeFeature.get() == null) {
            homepageEdgeToEdgeFeature.set(
                feature =
                    HomepageEdgeToEdgeFeature(
                        appStore = requireComponents.appStore,
                        activity = activity,
                        settings = activity.components.settings,
                        browsingModeManager = browsingModeManager,
                        toolbarStore = toolbarStore,
                    ),
                owner = viewLifecycleOwner,
                view = view,
            )
        }

        homeNavigationBar =
            HomeNavigationBar(
                toolbarStore = toolbarStore,
                appStore = activity.components.appStore,
                browsingModeManager = activity.browsingModeManager,
                settings = activity.components.settings,
                hideWhenKeyboardShown = true,
            )

        return HomeToolbarComposable(
            context = activity,
            navController = findNavController(),
            toolbarStore = toolbarStore,
            appStore = activity.components.appStore,
            browserStore = activity.components.core.store,
            browsingModeManager = activity.browsingModeManager,
            settings = activity.components.settings,
            directToSearchConfig =
                DirectToSearchConfig(
                    startSearch =
                        bundleArgs.getBoolean(FOCUS_ON_ADDRESS_BAR) || FxNimbus.features.oneClickSearch.value().enabled,
                    startVoiceSearch = bundleArgs.getBoolean(START_VOICE_SEARCH),
                    sessionId = args.sessionToStartSearchFor,
                    source = args.searchAccessPoint,
                ),
            coroutineScope = view.toScope(),
            tabStripContent = { TabStrip(toolbarStore) },
            searchSuggestionsContent = { modifier ->
                (awesomeBarComposable ?: initializeAwesomeBarComposable(toolbarStore, modifier))?.SearchSuggestions()
            },
            navigationBarContent = { homeNavigationBar?.Content() },
        )
    }

    private fun buildToolbarStore(activity: HomeActivity) =
        HomeToolbarStoreBuilder.build(
            context = activity,
            fragment = this,
            navController = findNavController(),
            appStore = requireContext().components.appStore,
            browserStore = requireContext().components.core.store,
            browsingModeManager = activity.browsingModeManager,
        )

    private fun initMessagingFeature(view: View) {
        if (requireComponents.settings.isExperimentationEnabled) {
            val messagingFeature =
                MessagingFeature(
                    appStore = requireComponents.appStore,
                    surface = FenixMessageSurfaceId.HOMESCREEN,
                )
            messagingFeatureHomescreen.set(
                feature = messagingFeature,
                owner = viewLifecycleOwner,
                view = view,
            )

            viewLifecycleOwner.lifecycle.addObserver(messagingFeature)

            initializeMicrosurveyFeature(requireComponents.settings.microsurveyFeatureEnabled, view)
        }
    }

    @VisibleForTesting
    internal fun initializeMicrosurveyFeature(isMicrosurveyEnabled: Boolean, view: View) {
        if (isMicrosurveyEnabled) {
            val messagingFeature =
                MessagingFeature(
                    appStore = requireComponents.appStore,
                    surface = FenixMessageSurfaceId.MICROSURVEY,
                )
            messagingFeatureMicrosurvey.set(
                feature = messagingFeature,
                owner = viewLifecycleOwner,
                view = view,
            )

            viewLifecycleOwner.lifecycle.addObserver(messagingFeature)
        }
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        // DO NOT ADD ANYTHING ABOVE THIS getProfilerTime CALL!
        val profilerStartTime = requireComponents.core.engine.profiler?.getProfilerTime()

        super.onViewCreated(view, savedInstanceState)

        recordHomepageTelemetry()

        observePrivateModeLock {
            findNavController()
                .navigate(NavGraphDirections.actionGlobalUnlockPrivateTabsFragment(NavigationOrigin.HOME_PAGE))
        }

        toolbarView.build(requireComponents.settings.enableHomepageSearchBar)

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
        initSwipeToSwitchTabs(view)
        initHistoryMetadataFeature(view = view)
        initThumbnailsFeature(view = view)
        initReviewPromptBinding(view = view)
        initTabsCleanupFeature(view = view)
        initSnackbarBinding(view = view)
        initIpProtectionBindings(view = view)
        initContinuousOnboardingFeature()

        privacyNoticeBannerStore =
            PrivacyNoticeBannerStore(
                initialState =
                    PrivacyNoticeBannerState(visible = privacyNoticeBannerRepository.shouldShowPrivacyNoticeBanner()),
                middleware =
                    listOf(
                        PrivacyNoticeBannerMiddleware(repository = privacyNoticeBannerRepository),
                        PrivacyNoticeBannerTelemetryMiddleware(),
                    ),
            )

        val collectionsMigrationRepository = requireComponents.collectionsMigrationRepository
        collectionsMigrationCardStore =
            CollectionsMigrationCardStore(
                initialState =
                    CollectionsMigrationCardState(
                        visible = collectionsMigrationRepository.shouldShowCollectionsMigrationCard()
                    ),
                middleware = listOf(CollectionsMigrationCardMiddleware(repository = collectionsMigrationRepository)),
            )

        initController()
        initInteractor()

        // DO NOT MOVE ANYTHING BELOW THIS addMarker CALL!
        requireComponents.core.engine.profiler?.addMarker(
            MarkersFragmentLifecycleCallbacks.MARKER_NAME,
            profilerStartTime,
            "HomeFragment.onViewCreated",
        )
    }

    /**
     * Whether swiping the toolbar to switch tabs should be enabled on the homepage. This only applies when Home behaves
     * as a tab (HNT) and the tab strip is not in use.
     */
    private fun isToolbarSwipeToSwitchTabsEnabled(): Boolean =
        with(requireComponents.settings) {
            isSwipeToolbarToSwitchTabsEnabled && !isTabStripEnabled && enableHomepageAsNewTab
        }

    @Suppress("ReturnCount")
    private fun initSwipeToSwitchTabs(view: View) {
        if (!isToolbarSwipeToSwitchTabsEnabled()) {
            return
        }

        val gestureLayout = view as? SwipeGestureLayout ?: return
        val contentLayout = homepageComposeView ?: return
        val tabPreview = homeTabPreview ?: return

        HomeSwipeIntegration(
                activity = requireActivity(),
                store = requireComponents.core.store,
                selectTabUseCase = requireComponents.useCases.tabsUseCases.selectTab,
                contentLayout = contentLayout,
                gestureLayout = gestureLayout,
                navController = findNavController(),
                navBarLayoutRect = { navbarBoundsInRoot.toScreenRect(contentLayout) },
                toolbarLayoutRect = { toolbarBoundsInRoot.toScreenRect(contentLayout) },
                tabPreview = tabPreview,
            )
            .initializeSwipeUI()
    }

    private fun Rect?.toScreenRect(contentLayout: ComposeView): Rect? {
        if (this == null) return null
        val location = IntArray(2)
        contentLayout.getLocationOnScreen(location)
        return Rect(
            left + location[0],
            top + location[1],
            right + location[0],
            bottom + location[1],
        )
    }

    @Suppress("LongMethod", "CognitiveComplexMethod")
    private fun initComposeHomepage(view: ComposeView) {
        view.setContent {
            FirefoxTheme {
                val settings = components.settings
                val appState =
                    with(components.appStore) {
                        remember {
                                // Ignore AppState changes where only the browsing mode differs.
                                // This avoids unnecessary recompositions triggered by theme/browsing mode transitions,
                                // which are handled outside Compose via ThemeManager recreating the activity.
                                // Without this, transient states can cause visual glitches (e.g., incorrect
                                // theme/frame)
                                flow().distinctUntilChanged { old, new -> old.mode != new.mode }
                            }
                            .collectAsState(state)
                    }
                val privacyNoticeBannerState =
                    privacyNoticeBannerStore.flow().collectAsState(initial = privacyNoticeBannerStore.state)
                val collectionsMigrationCardState =
                    collectionsMigrationCardStore.flow().collectAsState(initial = collectionsMigrationCardStore.state)
                val isToolbarAtTop = settings.toolbarPosition == ToolbarPosition.TOP
                val captureToolbarBounds = remember { isToolbarSwipeToSwitchTabsEnabled() }

                val microsurveyVisible =
                    settings.microsurveyFeatureEnabled &&
                        !appState.value.mode.isPrivate &&
                        appState.value.microsurvey.current != null

                val isPrivateMode = appState.value.mode.isPrivate
                val currentWallpaper = appState.value.wallpaperState.currentWallpaper
                val universalEdgeToEdge = settings.enableUniversalEdgeToEdgeWallpapers
                val showToolbarScrim = shouldShowToolbarScrim(appState.value)
                var topBarHeight by remember { mutableIntStateOf(0) }
                var bottomBarHeight by remember { mutableIntStateOf(0) }
                val fadeContentEdges = isOverUniversalEdgeToEdgeWallpaper(appState.value)

                LaunchedEffect(currentWallpaper.name, isPrivateMode, universalEdgeToEdge) {
                    if (universalEdgeToEdge) {
                        applyWallpaperSystemBarsTheme(activity as HomeActivity, settings, isPrivateMode)
                    }
                }

                CompositionLocalProvider(LocalWallpaperState provides appState.value.wallpaperState) {
                    Box(
                        modifier =
                            Modifier.fillMaxSize().thenConditional(
                                // Without the universal treatment the wallpaper is inset by the system
                                // bars; with it, the wallpaper stays edge-to-edge and only the Scaffold
                                // content is inset (below).
                                Modifier.systemBarsPadding().displayCutoutPadding()
                            ) {
                                !universalEdgeToEdge
                            }
                    ) {
                        if (!isPrivateMode) {
                            WallpaperBackground(currentWallpaper)
                        }

                        if (showToolbarScrim) {
                            WallpaperToolbarScrim(
                                topContentHeight = topBarHeight,
                                bottomContentHeight = bottomBarHeight,
                            )
                        }

                        Scaffold(
                            modifier =
                                Modifier.fillMaxSize()
                                    .thenConditional(Modifier.systemBarsPadding().displayCutoutPadding()) {
                                        universalEdgeToEdge
                                    }
                                    .imePadding(),
                            topBar = {
                                HomeTopBar(
                                    settings = settings,
                                    isToolbarAtTop = isToolbarAtTop,
                                    captureToolbarBounds = captureToolbarBounds,
                                    modifier = Modifier.onSizeChanged { topBarHeight = it.height },
                                )
                            },
                            bottomBar = {
                                HomeBottomBar(
                                    settings = settings,
                                    isToolbarAtTop = isToolbarAtTop,
                                    isPrivateMode = isPrivateMode,
                                    captureToolbarBounds = captureToolbarBounds,
                                    modifier = Modifier.onSizeChanged { bottomBarHeight = it.height },
                                )
                            },
                            containerColor = Color.Transparent,
                        ) { innerPadding ->
                            HomeContent(
                                appState = appState.value,
                                privacyNoticeBannerState = privacyNoticeBannerState.value,
                                collectionsMigrationCardState = collectionsMigrationCardState.value,
                                settings = settings,
                                innerPadding = innerPadding,
                                fadeContentEdges = fadeContentEdges,
                                microsurveyVisible = microsurveyVisible,
                                microsurvey = appState.value.microsurvey.current,
                            )
                        }
                    }
                }

                LaunchedEffect(Unit) {
                    onFirstHomepageFrameDrawn()
                }
            }
        }
    }

    /**
     * Draws [currentWallpaper] behind the homepage content. If its image fails to load, clears the saved wallpaper text
     * color and shows an error snackbar.
     */
    @Composable
    private fun WallpaperBackground(currentWallpaper: Wallpaper) {
        WallpaperBackground(
            wallpaper = currentWallpaper,
            loadBitmap = components.useCases.wallpaperUseCases.loadBitmap::invoke,
            onLoadFailed = {
                requireComponents.settings.currentWallpaperTextColor = 0L
                showComposeSnackbar(
                    SnackbarState(message = resources.getString(R.string.wallpaper_select_error_snackbar_message))
                )
            },
        )
    }

    /**
     * Content of the homepage Scaffold's top bar: the toolbar when it is at the top, otherwise the tab strip if
     * enabled.
     */
    @Composable
    private fun HomeTopBar(
        settings: Settings,
        isToolbarAtTop: Boolean,
        captureToolbarBounds: Boolean,
        modifier: Modifier = Modifier,
    ) {
        Box(modifier = modifier) {
            if (isToolbarAtTop) {
                ToolbarSlot(captureToolbarBounds, { toolbarBoundsInRoot = it }) {
                    toolbarView.Content()
                }
            } else if (settings.shouldShowTabStripAtTop) {
                TabStrip()
            }
        }
    }

    /**
     * Content of the homepage Scaffold's bottom bar: the navigation bar when the toolbar is at the top, otherwise the
     * toolbar. Either is preceded by the enabled tab strips.
     */
    @Composable
    private fun HomeBottomBar(
        settings: Settings,
        isToolbarAtTop: Boolean,
        isPrivateMode: Boolean,
        captureToolbarBounds: Boolean,
        modifier: Modifier = Modifier,
    ) {
        val bottomChromeColors =
            homepageToolbarColors(
                isPrivateMode = isPrivateMode,
                shouldUseEdgeToEdgeColors = isEdgeToEdgeBackgroundEnabled(),
            )
        Box(modifier = modifier) {
            if (isToolbarAtTop) {
                HomeNavigationBottomBar(settings, captureToolbarBounds, bottomChromeColors)
            } else {
                HomeToolbarBottomBar(settings, captureToolbarBounds, bottomChromeColors)
            }
        }
    }

    /** Bottom bar used when the toolbar is at the top: the enabled tab strips above the navigation bar. */
    @Composable
    private fun HomeNavigationBottomBar(
        settings: Settings,
        captureToolbarBounds: Boolean,
        colors: ColorScheme,
    ) {
        ToolbarSlot(captureToolbarBounds, { navbarBoundsInRoot = it }) {
            TopDividerColumn(
                showDivider = settings.shouldShowTabStripAtBottom || settings.shouldShowTabGroupsStrip,
                modifier = Modifier.fillMaxWidth(),
                dividerColor = colors.outlineVariant,
            ) {
                if (settings.shouldShowTabStripAtBottom) {
                    TabStrip()
                }
                if (settings.shouldShowTabGroupsStrip) {
                    TabGroupsStrip(containerColor = colors.surface)
                }
                homeNavigationBar?.Content()
            }
        }
    }

    /** Bottom bar used when the toolbar is at the bottom: the tab groups strip, if enabled, above the toolbar. */
    @Composable
    private fun HomeToolbarBottomBar(
        settings: Settings,
        captureToolbarBounds: Boolean,
        colors: ColorScheme,
    ) {
        ToolbarSlot(captureToolbarBounds, { toolbarBoundsInRoot = it }) {
            TopDividerColumn(
                showDivider = settings.shouldShowTabGroupsStrip,
                modifier = Modifier.fillMaxWidth(),
                dividerColor = colors.outlineVariant,
            ) {
                if (settings.shouldShowTabGroupsStrip) {
                    TabGroupsStrip(containerColor = colors.surface)
                }
                toolbarView.Content()
            }
        }
    }

    /**
     * Renders a home toolbar slot, capturing its on-screen bounds via [onBounds] only when [captureBounds] is true
     * (i.e. when swipe-to-switch-tabs is active). When disabled, the content is rendered directly so the home layout is
     * unchanged.
     */
    @Composable
    private fun ToolbarSlot(
        captureBounds: Boolean,
        onBounds: (Rect) -> Unit,
        content: @Composable () -> Unit,
    ) {
        if (captureBounds) {
            Box(
                modifier =
                    Modifier.onGloballyPositioned {
                        onBounds(it.boundsInRoot().toAndroidRect())
                    }
            ) {
                content()
            }
        } else {
            content()
        }
    }

    private fun androidx.compose.ui.geometry.Rect.toAndroidRect(): Rect =
        Rect(left.toInt(), top.toInt(), right.toInt(), bottom.toInt())

    /**
     * Matches the status bar and navigation bar icon appearance to the current wallpaper's text color so they stay
     * legible over the edge-to-edge wallpaper. Falls back to the theme's system bar appearance when there is no
     * wallpaper text color or while in private browsing mode.
     */
    private fun applyWallpaperSystemBarsTheme(
        activity: HomeActivity,
        settings: Settings,
        isPrivateMode: Boolean,
    ) {
        val wallpaperTextColor = settings.currentWallpaperTextColor
        if (isPrivateMode || wallpaperTextColor == 0L) {
            activity.themeManager.applyStatusBarTheme(activity)
        } else {
            val isLightAppearance = isDark(wallpaperTextColor.toInt())
            activity.window.createWindowInsetsController().apply {
                isAppearanceLightStatusBars = isLightAppearance
                isAppearanceLightNavigationBars = isLightAppearance
            }
        }
    }

    @Suppress("LongParameterList")
    @Composable
    private fun HomeContent(
        appState: AppState,
        privacyNoticeBannerState: PrivacyNoticeBannerState,
        collectionsMigrationCardState: CollectionsMigrationCardState,
        settings: Settings,
        innerPadding: PaddingValues,
        fadeContentEdges: Boolean,
        microsurveyVisible: Boolean,
        microsurvey: MicrosurveyUIData?,
    ) {
        Box(
            modifier =
                Modifier.fillMaxSize().padding(innerPadding).onGloballyPositioned { coordinates ->
                    val bounds = coordinates.boundsInRoot()
                    homepageContentBounds =
                        Rect(
                            bounds.left.roundToInt(),
                            bounds.top.roundToInt(),
                            bounds.right.roundToInt(),
                            bounds.bottom.roundToInt(),
                        )
                }
        ) {
            Homepage(
                state =
                    HomepageState.build(
                        appState = appState,
                        privacyNoticeBannerState = privacyNoticeBannerState,
                        collectionsMigrationCardState = collectionsMigrationCardState,
                        settings = settings,
                        browsingModeManager = browsingModeManager,
                    ),
                interactor = sessionControlInteractor,
                onCollectionsMigrationCardAction = { action ->
                    collectionsMigrationCardStore.dispatch(action)

                    when (action) {
                        is CollectionsMigrationCardAction.ViewTabGroupsClicked -> openTabGroups()
                    }
                },
                onTopSitesItemBound = {
                    StartupTimeline.onTopSitesItemBound(activity = (requireActivity() as HomeActivity))
                },
                modifier =
                    Modifier.thenConditional(Modifier.verticalFadingEdges(HOMEPAGE_EDGE_FADE_LENGTH)) {
                        fadeContentEdges
                    },
            )

            if (microsurveyVisible) {
                MicrosurveyPrompt(
                    microsurvey = microsurvey,
                    modifier = Modifier.align(Alignment.BottomCenter),
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
        microsurvey: MicrosurveyUIData?,
        modifier: Modifier = Modifier,
    ) {
        if (microsurvey == null) return
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
                sessionControlInteractor.showWallpapersOnboardingDialog(appState.wallpaperState)
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

    /** Whether the homepage is drawn over a universal edge-to-edge wallpaper, outside private mode and search. */
    private fun isOverUniversalEdgeToEdgeWallpaper(appState: AppState): Boolean =
        requireComponents.settings.enableUniversalEdgeToEdgeWallpapers &&
            !appState.mode.isPrivate &&
            !appState.searchState.isSearchActive

    /** Whether the gradient scrims are drawn behind the homepage toolbars over a universal edge-to-edge wallpaper. */
    private fun shouldShowToolbarScrim(appState: AppState): Boolean =
        isOverUniversalEdgeToEdgeWallpaper(appState) &&
            !Wallpaper.isLocalWallpaper(appState.wallpaperState.currentWallpaper.name)

    @Composable
    private fun TabStrip(toolbarStore: BrowserToolbarStore? = null) {
        // Tabs will not be shown as selected on the homepage when Homepage as a New Tab is not
        // enabled.
        val isSelectDisabled = !requireComponents.settings.enableHomepageAsNewTab
        val toolbarState: BrowserToolbarState? = toolbarStore?.observeAsComposableState { it }?.value
        // Tab strip doesn't show the gradient when displayed over edge to edge wallpaper.
        val isOverEdgeToEdgeWallpaper =
            requireComponents.appStore.observeAsComposableState { isOverUniversalEdgeToEdgeWallpaper(it) }.value

        FirefoxTheme {
            TabStrip(
                isSelectDisabled = isSelectDisabled,
                showTabCounterButton = false,
                tabStripColors =
                    TabStripColors.build(
                        toolbarState = toolbarState,
                        browsingModeManager = (requireActivity() as HomeActivity).browsingModeManager,
                        settings = requireComponents.settings,
                        isOverEdgeToEdgeWallpaper = isOverEdgeToEdgeWallpaper,
                    ),
                hideWhenKeyboardShown = requireComponents.settings.shouldUseBottomTabStrip,
                onAddTabClick = {
                    if (requireComponents.settings.enableHomepageAsNewTab) {
                        requireComponents.useCases.fenixBrowserUseCases.addNewHomepageTab(
                            private = (requireActivity() as HomeActivity).browsingModeManager.mode.isPrivate
                        )
                    } else {
                        findNavController()
                            .navigate(
                                NavGraphDirections.actionGlobalHome(
                                    focusOnAddressBar = !requireComponents.settings.enableHomepageTrendingRecentSearch
                                )
                            )
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
                findNavController().navigate(HomeFragmentDirections.actionGlobalBrowser(null))
            },
            operation = {},
            undoDelay = requireComponents.settings.getUndoDelay(),
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

        homepageComposeView = null
        homeTabPreview = null
        toolbarBoundsInRoot = null
        navbarBoundsInRoot = null

        _sessionControlController?.unregisterCallback()
        _sessionControlController = null

        _sessionControlInteractor = null
        awesomeBarComposable = null

        bundleArgs.clear()
    }

    override fun onStart() {
        super.onStart()

        val settings = requireComponents.settings
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
                                    message = requireContext().getString(R.string.onboarding_firefox_account_sync_is_on)
                                )
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
            findNavController()
                .navigate(BrowserFragmentDirections.actionGlobalTermsOfUseDialog(Surface.HOMEPAGE_NEW_TAB))
        }
    }

    @VisibleForTesting
    internal fun removeCollection(tabCollection: TabCollection) {
        requireComponents.applicationScope.launch {
            requireComponents.core.tabCollectionStorage.removeCollection(tabCollection)
        }
    }

    private fun initStoriesState() {
        val components = context?.components ?: return
        lifecycleScope.launch(IO) {
            val settings = components.settings

            val showStories =
                settings.showPocketRecommendationsFeature || settings.privateModeAndStoriesEntryPointEnabled

            val showSponsoredStories = showStories && settings.showPocketSponsoredStories

            if (showStories) {
                components.appStore.dispatch(
                    ContentRecommendationsAction.ContentRecommendationsFetched(
                        recommendations = components.core.pocketStoriesService.getContentRecommendations()
                    )
                )
            } else {
                components.appStore.dispatch(ContentRecommendationsAction.PocketStoriesClean)
            }

            if (showSponsoredStories) {
                components.appStore.dispatch(
                    ContentRecommendationsAction.SponsoredContentsChange(
                        sponsoredContents = components.core.pocketStoriesService.getSponsoredContents()
                    )
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

        BiometricAuthenticationManager.biometricAuthenticationNeededInfo.shouldShowAuthenticationPrompt = true
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

    override fun onBackPressed(): Boolean {
        if (context == null || !requireComponents.settings.enableHomepageAsNewTab) {
            return false
        }

        return goBackFromHomepage(
            browserStore = store,
            navController = findNavController(),
        )
    }

    override fun onBackLongPressed(): Boolean {
        if (context == null || !requireComponents.settings.enableHomepageAsNewTab) {
            return false
        }

        navigateToGlobalTabHistoryDialogFragment(navController = findNavController())
        return true
    }

    override fun onForwardPressed(): Boolean {
        if (context == null || !requireComponents.settings.enableHomepageAsNewTab) {
            return false
        }

        return goForwardFromHomepage(
            browserStore = store,
            navController = findNavController(),
        )
    }

    override fun onForwardLongPressed(): Boolean {
        if (context == null || !requireComponents.settings.enableHomepageAsNewTab) {
            return false
        }

        navigateToGlobalTabHistoryDialogFragment(navController = findNavController())
        return true
    }

    private fun navigateToGlobalTabHistoryDialogFragment(navController: NavController) {
        navController.navigate(NavGraphDirections.actionGlobalTabHistoryDialogFragment(activeSessionId = null))
    }

    private fun subscribeToTabCollections(): Observer<List<TabCollection>> {
        return Observer<List<TabCollection>> {
                requireComponents.core.tabCollectionStorage.cachedTabCollections = it
                requireComponents.appStore.dispatch(AppAction.CollectionsChange(it))
            }
            .also { observer ->
                requireComponents.core.tabCollectionStorage.getCollections().observe(this, observer)
            }
    }

    private fun registerCollectionStorageObserver() {
        requireComponents.core.tabCollectionStorage.register(collectionStorageObserver, this)
    }

    private fun openTabsTray() {
        findNavController()
            .nav(
                R.id.homeFragment,
                HomeFragmentDirections.actionGlobalTabManagementFragment(
                    page =
                        when (browsingModeManager.mode) {
                            BrowsingMode.Normal -> Page.NormalTabs
                            BrowsingMode.Private -> Page.PrivateTabs
                        }
                ),
            )
    }

    private fun openTabGroups() {
        findNavController()
            .nav(
                R.id.homeFragment,
                HomeFragmentDirections.actionGlobalTabManagementFragment(page = Page.TabGroups),
            )
    }

    /**
     * Shows a prompt to add a search widget to the home screen if supported by the device.
     *
     * This function should be called when the fragment's view is active (e.g., in response to a user interaction). It
     * launches a coroutine within the [viewLifecycleOwner]'s [androidx.lifecycle.LifecycleCoroutineScope] to display
     * the widget prompt using [showAddSearchWidgetPromptIfSupported].
     */
    private fun showAddSearchWidgetPrompt() {
        viewLifecycleOwner.lifecycleScope.launch {
            showAddSearchWidgetPromptIfSupported(requireActivity())
        }
    }

    internal fun isEdgeToEdgeBackgroundEnabled(): Boolean {
        val settings = requireComponents.settings
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
            )
            .also {
                awesomeBarComposable = it
            }
    }

    private fun initTopSitesBinding(view: View) {
        if (requireComponents.settings.showTopSitesFeature) {
            topSitesBinding.set(
                feature =
                    TopSitesBinding(
                        browserStore = requireComponents.core.store,
                        presenter =
                            DefaultTopSitesPresenter(
                                view =
                                    DefaultTopSitesView(
                                        appStore = requireComponents.appStore,
                                        settings = requireComponents.settings,
                                    ),
                                storage = requireComponents.core.topSitesStorage,
                                config =
                                    getTopSitesConfig(
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
        if (requireComponents.settings.showRecentTabsFeature) {
            recentTabsListFeature.set(
                feature =
                    RecentTabsListFeature(
                        browserStore = requireComponents.core.store,
                        appStore = requireComponents.appStore,
                    ),
                owner = viewLifecycleOwner,
                view = view,
            )

            recentSyncedTabFeature.set(
                feature =
                    RecentSyncedTabFeature(
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
        if (requireComponents.settings.showPrivacyReportFeature) {
            trackersBlockedFeature.set(
                feature =
                    TrackersBlockedFeature(
                        browserStore = requireComponents.core.store,
                        appStore = requireComponents.appStore,
                        currentSessionId = requireComponents.core.store.state.selectedTabId,
                        trackingProtectionUseCases = requireComponents.useCases.trackingProtectionUseCases,
                    ),
                owner = viewLifecycleOwner,
                view = view,
            )
        }
    }

    private fun initBookmarksFeature(view: View) {
        if (requireComponents.settings.showBookmarksHomeFeature) {
            bookmarksFeature.set(
                feature =
                    BookmarksFeature(
                        appStore = requireComponents.appStore,
                        bookmarksUseCase =
                            run {
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
        if (requireComponents.settings.historyMetadataUIFeature) {
            historyMetadataFeature.set(
                feature =
                    RecentVisitsFeature(
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
            feature =
                HomepageThumbnailIntegration(
                    context = requireContext(),
                    // Screenshot only the homepage content, not the swipe layout root, otherwise the
                    // TabPreview overlay gets baked into the thumbnail and compounds on every swipe.
                    view = homepageComposeView ?: view,
                    store = requireComponents.core.store,
                    appStore = requireComponents.appStore,
                    homepageContentBounds = { homepageContentBounds },
                ),
            owner = this,
            view = view,
        )
    }

    private fun initReviewPromptBinding(view: View) {
        showReviewPromptBinding.set(
            feature =
                ShowReviewPromptBinding(
                    appStore = requireComponents.appStore,
                    promptController = requireComponents.playStoreReviewPromptController,
                    activityRef = WeakReference(activity),
                    uiScope = viewLifecycleOwner.lifecycleScope,
                    showCustomReviewPrompt = { CustomReviewPromptBottomSheetFragment.showPrompt(this) },
                ),
            owner = viewLifecycleOwner,
            view = view,
        )
    }

    private fun initTabsCleanupFeature(view: View) {
        tabsCleanupFeature.set(
            feature =
                TabsCleanupFeature(
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
            feature =
                SnackbarBinding(
                    context = requireContext(),
                    browserStore = requireComponents.core.store,
                    appStore = requireComponents.appStore,
                    snackbarDelegate =
                        FenixSnackbarDelegate(
                            snackbarHostState = snackbarHostState,
                            scope = viewLifecycleOwner.lifecycleScope,
                            context = requireContext(),
                        ),
                    navController = findNavController(),
                    tabsUseCases = requireComponents.useCases.tabsUseCases,
                    sendTabUseCases = SendTabUseCases(requireComponents.backgroundServices.accountManager),
                    customTabSessionId = null,
                    applicationScope = requireComponents.applicationScope,
                    viewHasFocus = { view.hasWindowFocus() },
                ),
            owner = this,
            view = view,
        )
    }

    private fun initIpProtectionBindings(view: View) {
        ipProtectionWarningBinding.set(
            feature =
                IPProtectionWarningBinding(
                    store = requireComponents.ipProtection.store,
                    proxyUnavailable = {
                        Vpn.proxyUnavailable.record()
                        findNavController().navigate(HomeFragmentDirections.actionGlobalIpProtectionUnavailableDialog())
                    },
                ),
            owner = this,
            view = view,
        )
    }

    private fun initContinuousOnboardingFeature() {
        ContinuousOnboardingFeature.register(
            fragment = this,
            binding = continuousOnboardingFeature,
            launcher = continuousOnboardingDefaultBrowserLauncher,
            telemetryRecorder = telemetryRecorder,
            navigateToSyncSignIn = {
                findNavController()
                    .nav(
                        id = R.id.homeFragment,
                        directions =
                            OnboardingFragmentDirections.actionGlobalTurnOnSync(
                                entrypoint = FenixFxAEntryPoint.NewUserOnboarding
                            ),
                    )
            },
            navigateToIpProtection = {
                IPProtectionBottomSheetFragment.showPrompt(fragment = this, surface = IPProtectionSurface.HOMEPAGE)
            },
        )
    }

    @Suppress("LongMethod")
    private fun initInteractor() {
        _sessionControlInteractor =
            SessionControlInteractor(
                controller = sessionControlController,
                recentTabController =
                    DefaultRecentTabsController(
                        selectTabUseCase = requireComponents.useCases.tabsUseCases.selectTab,
                        navController = findNavController(),
                        appStore = requireComponents.appStore,
                    ),
                recentSyncedTabController =
                    DefaultRecentSyncedTabController(
                        fenixBrowserUseCases = requireComponents.useCases.fenixBrowserUseCases,
                        tabsUseCase = requireComponents.useCases.tabsUseCases,
                        navController = findNavController(),
                        accessPoint = AccessPoint.HomeRecentSyncedTab,
                        appStore = requireComponents.appStore,
                        settings = requireComponents.settings,
                    ),
                bookmarksController =
                    DefaultBookmarksController(
                        navController = findNavController(),
                        appStore = requireComponents.appStore,
                        browserStore = requireComponents.core.store,
                        settings = requireComponents.settings,
                        fenixBrowserUseCases = requireComponents.useCases.fenixBrowserUseCases,
                        selectTabUseCase = requireComponents.useCases.tabsUseCases.selectTab,
                    ),
                recentVisitsController =
                    DefaultRecentVisitsController(
                        navController = findNavController(),
                        appStore = requireComponents.appStore,
                        settings = requireComponents.settings,
                        fenixBrowserUseCases = requireComponents.useCases.fenixBrowserUseCases,
                        selectOrAddTabUseCase = requireComponents.useCases.tabsUseCases.selectOrAddTab,
                        storage = requireComponents.core.historyStorage,
                        scope = viewLifecycleOwner.lifecycleScope,
                        store = requireComponents.core.store,
                    ),
                pocketStoriesController =
                    DefaultPocketStoriesController(
                        navControllerRef = WeakReference(findNavController()),
                        appStore = requireComponents.appStore,
                        settings = requireComponents.settings,
                        fenixBrowserUseCases = requireComponents.useCases.fenixBrowserUseCases,
                        marsUseCases = requireComponents.useCases.marsUseCases,
                        viewLifecycleScope = viewLifecycleOwner.lifecycleScope,
                    ),
                privateBrowsingController =
                    DefaultPrivateBrowsingController(
                        navController = findNavController(),
                        browsingModeManager = browsingModeManager,
                        fenixBrowserUseCases = requireComponents.useCases.fenixBrowserUseCases,
                        settings = requireComponents.settings,
                    ),
                toolbarController = DefaultToolbarController(appStore = requireComponents.appStore),
                homeSearchController = DefaultHomeSearchController(appStore = requireComponents.appStore),
                topSiteController = buildTopSitesController(),
                privacyNoticeBannerController =
                    DefaultPrivacyNoticeBannerController(privacyNoticeBannerStore = privacyNoticeBannerStore),
                trackingProtectionController =
                    TrackingProtectionController(
                        navController = findNavController(),
                        currentSessionId = requireComponents.core.store.state.selectedTabId,
                    ),
                logoController =
                    LogoController(
                        longFoxFeature = requireComponents.core.longFoxFeature,
                        context = requireActivity(),
                        longFoxEnabled = requireComponents.settings.longfoxEnabled,
                    ),
            )
    }

    private fun buildTopSitesController() =
        DefaultTopSiteController(
            activityRef = WeakReference(requireActivity()),
            store = store,
            appStore = requireComponents.appStore,
            navControllerRef = WeakReference(findNavController()),
            settings = requireComponents.settings,
            defaultTopSitesProvider = requireComponents.core.defaultTopSitesProvider,
            addTabUseCase = requireComponents.useCases.tabsUseCases.addTab,
            selectTabUseCase = requireComponents.useCases.tabsUseCases.selectTab,
            fenixBrowserUseCases = requireComponents.useCases.fenixBrowserUseCases,
            topSitesUseCases = requireComponents.useCases.topSitesUseCase,
            mozAdsUseCases = requireComponents.useCases.mozAdsUseCases,
            viewLifecycleScope = viewLifecycleOwner.lifecycleScope,
            source = TopSitesSource.HOMEPAGE,
        )

    private fun initController() {
        val activity = activity as HomeActivity
        _sessionControlController =
            DefaultSessionControlController(
                    activityRef = WeakReference(activity),
                    settings = requireComponents.settings,
                    engine = requireComponents.core.engine,
                    messageController =
                        DefaultMessageController(
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
                    applicationScope = requireComponents.applicationScope,
                    shareUseCases = requireComponents.useCases.shareUseCases,
                    showAddSearchWidgetPrompt = ::showAddSearchWidgetPrompt,
                    requestSetDefaultBrowserPrompt = {
                        maybeRequestDefaultBrowserPrompt(
                            WeakReference(activity),
                            setToDefaultPromptRequestLauncher,
                        )
                    },
                )
                .apply {
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
                        }
                    )
                }
    }

    private fun recordHomepageTelemetry() {
        HomeScreen.homeScreenDisplayed.record(NoExtras())

        with(requireContext()) {
            if (components.settings.isExperimentationEnabled) {
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
     * Updates the last time the user was active on the [HomeFragment]. This is useful to determine if the user has to
     * start on the [HomeFragment] or it should go directly to the [BrowserFragment].
     */
    @VisibleForTesting
    internal fun updateLastHomeActivity() {
        requireComponents.settings.recordLastHomeActivity()
    }

    companion object {
        // Navigation arguments passed to HomeFragment
        const val FOCUS_ON_ADDRESS_BAR = "focusOnAddressBar"
        const val START_VOICE_SEARCH = "startVoiceSearch"
        private const val SESSION_TO_DELETE = "sessionToDelete"

        // Distance over which the homepage content fades out where it meets the toolbars.
        private val HOMEPAGE_EDGE_FADE_LENGTH = 24.dp
    }
}
