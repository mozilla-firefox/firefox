/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray.ui

import android.content.Intent
import android.os.Bundle
import android.view.Gravity
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.activity.compose.BackHandler
import androidx.activity.result.ActivityResultLauncher
import androidx.annotation.UiThread
import androidx.annotation.VisibleForTesting
import androidx.biometric.BiometricManager
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.MutableTransitionState
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.SnackbarHostState
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.platform.LocalResources
import androidx.compose.ui.res.stringResource
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.fragment.app.setFragmentResultListener
import androidx.fragment.compose.content
import androidx.lifecycle.lifecycleScope
import androidx.navigation.NavController
import androidx.navigation.fragment.findNavController
import androidx.navigation.fragment.navArgs
import androidx.navigation3.runtime.entryProvider
import androidx.navigation3.scene.DialogSceneStrategy
import androidx.navigation3.ui.NavDisplay
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch
import mozilla.appservices.places.BookmarkRoot
import mozilla.components.browser.state.selector.privateTabs
import mozilla.components.compose.base.modifier.thenConditional
import mozilla.components.compose.base.snackbar.displaySnackbar
import mozilla.components.concept.base.crash.Breadcrumb
import mozilla.components.feature.accounts.push.CloseTabsUseCases
import mozilla.components.feature.downloads.ui.DownloadCancelDialogFragment
import mozilla.components.lib.state.helpers.StoreProvider.Companion.storeProvider
import mozilla.components.support.base.feature.ViewBoundFeatureWrapper
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.fenix.Config
import org.mozilla.fenix.GleanMetrics.PrivateBrowsingLocked
import org.mozilla.fenix.GleanMetrics.TabsTray
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.R
import org.mozilla.fenix.compose.navigation.BottomSheetSceneStrategy
import org.mozilla.fenix.ext.actualInactiveTabs
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.hideToolbar
import org.mozilla.fenix.ext.pixelSizeFor
import org.mozilla.fenix.ext.registerForActivityResult
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.ext.runIfFragmentIsAttached
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.home.HomeScreenViewModel
import org.mozilla.fenix.navigation.DefaultNavControllerProvider
import org.mozilla.fenix.navigation.NavControllerProvider
import org.mozilla.fenix.pbmlock.registerForVerification
import org.mozilla.fenix.pbmlock.verifyUser
import org.mozilla.fenix.settings.biometric.DefaultBiometricUtils
import org.mozilla.fenix.settings.biometric.ext.isAuthenticatorAvailable
import org.mozilla.fenix.settings.biometric.ext.isHardwareAvailable
import org.mozilla.fenix.share.ShareFragment
import org.mozilla.fenix.tabgroups.AddToTabGroup
import org.mozilla.fenix.tabgroups.CloseLastTabAndDeleteTabGroupConfirmationDialog
import org.mozilla.fenix.tabgroups.DeleteTabGroupConfirmationDialog
import org.mozilla.fenix.tabgroups.EditTabGroup
import org.mozilla.fenix.tabgroups.ExpandedTabGroup
import org.mozilla.fenix.tabstray.InactiveTabsBinding
import org.mozilla.fenix.tabstray.PbmLockStatusBinding
import org.mozilla.fenix.tabstray.TabsTrayTelemetryMiddleware
import org.mozilla.fenix.tabstray.binding.SecureTabManagerBinding
import org.mozilla.fenix.tabstray.controller.DefaultTabManagerController
import org.mozilla.fenix.tabstray.controller.DefaultTabManagerInteractor
import org.mozilla.fenix.tabstray.controller.TabInteractionHandler
import org.mozilla.fenix.tabstray.controller.TabManagerController
import org.mozilla.fenix.tabstray.controller.TabManagerInteractor
import org.mozilla.fenix.tabstray.data.TabData
import org.mozilla.fenix.tabstray.data.TabsTrayItem
import org.mozilla.fenix.tabstray.navigation.TabManagerNavDestination
import org.mozilla.fenix.tabstray.redux.action.TabGroupAction
import org.mozilla.fenix.tabstray.redux.action.TabsTrayAction
import org.mozilla.fenix.tabstray.redux.middleware.TabSearchMiddleware
import org.mozilla.fenix.tabstray.redux.middleware.TabSearchNavigationMiddleware
import org.mozilla.fenix.tabstray.redux.middleware.TabStorageMiddleware
import org.mozilla.fenix.tabstray.redux.state.Page
import org.mozilla.fenix.tabstray.redux.state.TabsTrayState
import org.mozilla.fenix.tabstray.redux.store.TabsTrayStore
import org.mozilla.fenix.tabstray.syncedtabs.SyncedTabsIntegration
import org.mozilla.fenix.tabstray.ui.animation.defaultPredictivePopTransitionSpec
import org.mozilla.fenix.tabstray.ui.animation.defaultTransitionSpec
import org.mozilla.fenix.tabstray.ui.animation.popTransitionSpec
import org.mozilla.fenix.tabstray.ui.tabsearch.TabSearchScreen
import org.mozilla.fenix.tabstray.ui.tabstray.TabsTray
import org.mozilla.fenix.tabstray.ui.theme.TabManagerThemeProvider
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.theme.ThemeManager
import org.mozilla.fenix.trackingprotection.TrackersBlockedFeature
import org.mozilla.fenix.utils.Settings
import org.mozilla.fenix.utils.getSnackbarTimeout
import kotlin.math.abs

/**
 * The fullscreen fragment for displaying the tabs management UI.
 */
@Suppress("TooManyFunctions", "LargeClass")
class TabManagementFragment : Fragment() {

    private lateinit var tabManagerInteractor: TabManagerInteractor
    private lateinit var tabManagerController: TabManagerController
    private lateinit var enablePbmPinLauncher: ActivityResultLauncher<Intent>

    @VisibleForTesting
    internal var verificationResultLauncher: ActivityResultLauncher<Intent> =
        registerForVerification(onVerified = ::openPrivateTabsPage)

    @VisibleForTesting
    internal lateinit var tabsTrayStore: TabsTrayStore

    private val inactiveTabsBinding = ViewBoundFeatureWrapper<InactiveTabsBinding>()
    private val pbmLockStatusBinding = ViewBoundFeatureWrapper<PbmLockStatusBinding>()
    private val secureTabManagerBinding = ViewBoundFeatureWrapper<SecureTabManagerBinding>()
    private val syncedTabsIntegration = ViewBoundFeatureWrapper<SyncedTabsIntegration>()
    private val trackersBlockedFeature = ViewBoundFeatureWrapper<TrackersBlockedFeature>()
    private lateinit var snackbarHostState: SnackbarHostState

    private val animationDurationMs = 200

    private val tabInteractionHandler =
        object : TabInteractionHandler {
            override fun onMove(
                sourceKey: String,
                targetKey: String?,
                placeAfter: Boolean,
            ) {
                tabsTrayStore.dispatch(
                    TabsTrayAction.ReorderTabsTrayItem(
                        sourceId = sourceKey,
                        destinationId = targetKey,
                        placeAfter = placeAfter,
                    ),
                )
                tabsTrayStore.dispatch(
                    TabsTrayAction.TabDragCancel,
                )
            }

            override fun onDrop(sourceKey: String, targetKey: String) {
                tabsTrayStore.dispatch(
                    TabGroupAction.DragAndDropCompleted(
                        sourceKey,
                        targetKey,
                    ),
                )
            }

            override fun onDragCancel() {
                tabsTrayStore.dispatch(
                    TabsTrayAction.TabDragCancel,
                )
            }

            override fun onDragStart(sourceKey: String, preserveSelectMode: Boolean) {
                tabsTrayStore.dispatch(
                    TabsTrayAction.TabDragStart(
                        sourceId = sourceKey,
                        preserveSelectMode = preserveSelectMode,
                    ),
                )
            }
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        recordBreadcrumb("TabManagementFragment onCreate")

        enablePbmPinLauncher = registerForActivityResult(
            onSuccess = {
                PrivateBrowsingLocked.authSuccess.record()
                PrivateBrowsingLocked.featureEnabled.record()
                requireContext().settings().privateBrowsingModeLocked = true
            },
            onFailure = {
                PrivateBrowsingLocked.authFailure.record()
            },
        )
    }

    @OptIn(ExperimentalMaterial3Api::class)
    @Suppress("LongMethod", "CognitiveComplexMethod")
    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View {
        tabsTrayStore = setupStore()

        tabManagerController = DefaultTabManagerController(
            accountManager = requireComponents.backgroundServices.accountManager,
            context = requireContext(),
            appStore = requireComponents.appStore,
            tabsTrayStore = tabsTrayStore,
            browserStore = requireComponents.core.store,
            settings = requireContext().settings(),
            browsingModeManager = (activity as HomeActivity).browsingModeManager,
            navController = findNavController(),
            navigateToHomeAndDeleteSession = ::navigateToHomeAndDeleteSession,
            profiler = requireComponents.core.engine.profiler,
            tabsUseCases = requireComponents.useCases.tabsUseCases,
            fenixBrowserUseCases = requireComponents.useCases.fenixBrowserUseCases,
            shareUseCases = requireComponents.useCases.shareUseCases,
            closeSyncedTabsUseCases = requireComponents.useCases.closeSyncedTabsUseCases,
            addBookmarkUseCase = requireComponents.useCases.bookmarksUseCases.addBookmark,
            collectionStorage = requireComponents.core.tabCollectionStorage,
            showUndoSnackbarForTab = ::showUndoSnackbarForTab,
            showUndoSnackbarForInactiveTab = ::showUndoSnackbarForInactiveTab,
            showUndoSnackbarForSyncedTab = ::showUndoSnackbarForSyncedTab,
            showCancelledDownloadWarning = ::showCancelledDownloadWarning,
            showBookmarkSnackbar = ::showBookmarkSnackbar,
            showCollectionSnackbar = ::showCollectionSnackbar,
        )

        tabManagerInteractor = DefaultTabManagerInteractor(controller = tabManagerController)

        val settings = requireContext().settings()
        val showPrivacyReport = settings.showPrivacyReportInTabManager

        return content {
            val state by tabsTrayStore.stateFlow.collectAsState()
            val appState by requireComponents.appStore.stateFlow.collectAsState()
            snackbarHostState = remember { SnackbarHostState() }
            val trackersBlockedCount = when (showPrivacyReport) {
                true -> appState.blockedTrackersState.trackersBlockedCount
                false -> null
            }

            BackHandler {
                when {
                    tabsTrayStore.state.mode is TabsTrayState.Mode.Select -> {
                        tabsTrayStore.dispatch(TabsTrayAction.ExitSelectMode)
                    }

                    else -> {
                        onTabsTrayDismissed()
                    }
                }
            }

            FirefoxTheme(theme = TabManagerThemeProvider(selectedPage = state.selectedPage).provideTheme()) {
                val transitionColor = MaterialTheme.colorScheme.surfaceContainer

                val tabTrayVisibilityState = remember {
                    MutableTransitionState(false).apply { targetState = true }
                }
                val tabSelectedState = remember { mutableStateOf<TabsTrayItem.Tab?>(null) }
                val sceneStrategy = remember {
                    listOf(DialogSceneStrategy<TabManagerNavDestination>(), BottomSheetSceneStrategy())
                }
                val shouldPerformTransitionAnimation = remember {
                    derivedStateOf {
                        shouldPerformTransitionAnimation(
                            state.selectedPage,
                            state.mode,
                            tabSelectedState.value,
                        )
                    }
                }
                val handleTabClick: (TabsTrayItem.Tab) -> Unit = { tab ->
                    tabSelectedState.value = tab
                    if (shouldPerformTransitionAnimation.value) {
                        tabTrayVisibilityState.targetState = false
                    } else {
                        performTabClick(tab = tab)
                    }
                }
                val windowSize = FirefoxTheme.windowSize
                val resources = LocalResources.current

                // When the TabTray is hidden by an action, if a new tab is being selected, navigate to it.
                LaunchedEffect(tabTrayVisibilityState.currentState) {
                    tabSelectedState.value?.let {
                        if (!tabTrayVisibilityState.currentState) {
                            performTabClick(tab = it)
                        }
                    }
                }

                AnimatedVisibility(
                    enter = fadeIn(animationSpec = tween(durationMillis = animationDurationMs)),
                    exit = fadeOut(animationSpec = tween(durationMillis = animationDurationMs)),
                    visibleState = tabTrayVisibilityState,
                    modifier = Modifier.thenConditional(
                        Modifier.drawBehind(onDraw = { drawRect(color = transitionColor) }),
                        { !tabTrayVisibilityState.targetState },
                    ),
                ) {
                    NavDisplay(
                        backStack = state.backStack,
                        onBack = { tabsTrayStore.dispatch(TabsTrayAction.NavigateBackInvoked) },
                        transitionSpec = defaultTransitionSpec(),
                        popTransitionSpec = popTransitionSpec(),
                        predictivePopTransitionSpec = defaultPredictivePopTransitionSpec(),
                        sceneStrategies = sceneStrategy,
                        entryProvider = entryProvider {
                            entry<TabManagerNavDestination.Root> {
                                TabsTray(
                                    tabsTrayStore = tabsTrayStore,
                                    snackbarHostState = snackbarHostState,
                                    onTabPageClick = { page ->
                                        onTabPageClick(
                                            tabsTrayInteractor = tabManagerInteractor,
                                            page = page,
                                        )
                                    },
                                    onTabClose = { tab ->
                                        tabManagerInteractor.onTabClosed(tab, TAB_MANAGER_FEATURE_NAME)
                                    },
                                    onItemClick = {
                                        // Either start the transition animation and delay the click handling
                                        // until it is complete, or directly proceed.
                                        when (it) {
                                            is TabsTrayItem.Tab -> handleTabClick(it)

                                            is TabsTrayItem.TabGroup -> {
                                                tabsTrayStore.dispatch(TabGroupAction.TabGroupClicked(group = it))
                                            }
                                        }
                                    },
                                    onItemLongClick = { item ->
                                        tabsTrayStore.dispatch(
                                            TabsTrayAction.TabItemLongClicked(
                                                item,
                                            ),
                                        )
                                    },
                                    onInactiveTabsHeaderClick =
                                        tabManagerInteractor::onInactiveTabsHeaderClicked,
                                    onDeleteAllInactiveTabsClick =
                                        tabManagerInteractor::onDeleteAllInactiveTabsClicked,
                                    onInactiveTabsAutoCloseDialogShown = {
                                        tabsTrayStore.dispatch(TabsTrayAction.TabAutoCloseDialogShown)
                                    },
                                    onInactiveTabAutoCloseDialogCloseButtonClick =
                                        tabManagerInteractor::onAutoCloseDialogCloseButtonClicked,
                                    onEnableInactiveTabAutoCloseClick = {
                                        tabManagerInteractor.onEnableAutoCloseClicked()
                                        showInactiveTabsAutoCloseConfirmationSnackbar()
                                    },
                                    onInactiveTabClick = tabManagerInteractor::onInactiveTabClicked,
                                    onInactiveTabClose = tabManagerInteractor::onInactiveTabClosed,
                                    onSyncedTabClick = tabManagerInteractor::onSyncedTabClicked,
                                    onSyncedTabClose = tabManagerInteractor::onSyncedTabClosed,
                                    onSignInClick = tabManagerInteractor::onSignInClicked,
                                    onSaveToCollectionClick =
                                        tabManagerInteractor::onAddSelectedTabsToCollectionClicked,
                                    onShareSelectedTabsClick = tabManagerInteractor::onShareSelectedTabs,
                                    onTabSettingsClick = tabManagerController::onTabSettingsClicked,
                                    onRecentlyClosedClick = tabManagerController::onOpenRecentlyClosedClicked,
                                    onAccountSettingsClick = tabManagerController::onAccountSettingsClicked,
                                    onDeleteAllTabsClick = {
                                        if (tabsTrayStore.state.selectedPage == Page.NormalTabs) {
                                            tabsTrayStore.dispatch(TabsTrayAction.CloseAllNormalTabs)
                                        } else if (tabsTrayStore.state.selectedPage == Page.PrivateTabs) {
                                            tabsTrayStore.dispatch(TabsTrayAction.CloseAllPrivateTabs)
                                        }

                                        tabManagerController.onCloseAllTabsClicked(
                                            private = tabsTrayStore.state.selectedPage == Page.PrivateTabs,
                                        )
                                    },
                                    onDeleteSelectedTabsClick =
                                        tabManagerInteractor::onDeleteSelectedTabsClicked,
                                    onBookmarkSelectedTabsClick =
                                        tabManagerInteractor::onBookmarkSelectedTabsClicked,
                                    onForceSelectedTabsAsInactiveClick =
                                        tabManagerInteractor::onForceSelectedTabsAsInactiveClicked,
                                    onTabsTrayPbmLockedClick = ::onTabsTrayPbmLockedClick,
                                    onTabsTrayPbmLockedDismiss = {
                                        requireContext().settings().shouldShowLockPbmBanner = false
                                        PrivateBrowsingLocked.bannerNegativeClicked.record()
                                    },
                                    onTabAutoCloseBannerViewOptionsClick = {
                                        tabManagerController.onTabSettingsClicked()
                                        requireContext().settings().shouldShowAutoCloseTabsBanner =
                                            false
                                        requireContext().settings().lastCfrShownTimeInMillis =
                                            System.currentTimeMillis()
                                    },
                                    onTabAutoCloseBannerDismiss = {
                                        requireContext().settings().shouldShowAutoCloseTabsBanner =
                                            false
                                        requireContext().settings().lastCfrShownTimeInMillis =
                                            System.currentTimeMillis()
                                    },
                                    onTabAutoCloseBannerShown = {},
                                    tabInteractionHandler = tabInteractionHandler,
                                    onInactiveTabsCFRShown = {
                                        TabsTray.inactiveTabsCfrVisible.record(NoExtras())
                                    },
                                    onInactiveTabsCFRClick = {
                                        requireContext().settings().shouldShowInactiveTabsOnboardingPopup =
                                            false
                                        requireContext().settings().lastCfrShownTimeInMillis =
                                            System.currentTimeMillis()
                                        tabManagerController.onTabSettingsClicked()
                                        TabsTray.inactiveTabsCfrSettings.record(NoExtras())
                                    },
                                    onInactiveTabsCFRDismiss = {
                                        requireContext().settings().shouldShowInactiveTabsOnboardingPopup =
                                            false
                                        requireContext().settings().lastCfrShownTimeInMillis =
                                            System.currentTimeMillis()
                                        TabsTray.inactiveTabsCfrDismissed.record(NoExtras())
                                    },
                                    onTabGroupOnboardingDismiss = {
                                        // TODO (Bug 2038234): Persistence will be handled later by the middleware.
                                        tabsTrayStore.dispatch(TabGroupAction.OnboardingDismissed)
                                    },
                                    onOpenNewNormalTabClicked = tabManagerInteractor::onNormalTabsFabClicked,
                                    onOpenNewPrivateTabClicked = tabManagerInteractor::onPrivateTabsFabClicked,
                                    onSyncedTabsFabClicked = tabManagerInteractor::onSyncedTabsFabClicked,
                                    onUnlockPbmClick = {
                                        verifyUser(fallbackVerification = verificationResultLauncher)
                                    },
                                    trackersBlockedCount = trackersBlockedCount,
                                    onPrivacyReportTapped = tabManagerController::onPrivacyReportTapped,
                                )
                            }

                            entry<TabManagerNavDestination.TabSearch> {
                                TabSearchScreen(store = tabsTrayStore)
                            }

                            entry<TabManagerNavDestination.ExpandedTabGroup>(
                                metadata = { destination ->
                                    BottomSheetSceneStrategy.bottomSheet(
                                        handleContentDescription = resources.getString(
                                            R.string.tab_group_sheet_dismiss_description,
                                        ),
                                        showBetaLabel = true,
                                        fullyExpandOnFirstOpen = destination.group.shouldFullyExpandOnFirstOpen(
                                            windowSize = windowSize,
                                        ),
                                    )
                                },
                            ) { args ->
                                val expandedGroup by tabsTrayStore.observeTabGroup(tabGroup = args.group)
                                    .collectAsState(initial = args.group)

                                ExpandedTabGroup(
                                    group = expandedGroup,
                                    onItemClick = {
                                        when (it) {
                                            is TabsTrayItem.Tab -> handleTabClick(it)

                                            else -> {}
                                        }
                                    },
                                    onTabClose = { tab ->
                                        tabsTrayStore.dispatch(
                                            TabGroupAction.TabClosed(tab = tab, group = expandedGroup),
                                        )
                                    },
                                    onDeleteTabGroupClick = {
                                        tabsTrayStore.dispatch(TabGroupAction.DeleteClicked(expandedGroup))
                                    },
                                    onEditTabGroupClick = {
                                        tabsTrayStore.dispatch(
                                            action = TabGroupAction.EditTabGroupClicked(group = expandedGroup),
                                        )
                                    },
                                    onCloseTabGroupClick = {
                                        tabsTrayStore.dispatch(
                                            action = TabGroupAction.CloseTabGroupClicked(group = expandedGroup),
                                        )
                                    },
                                )
                            }

                            entry<TabManagerNavDestination.DeleteTabGroupConfirmationDialog>(
                                metadata = DialogSceneStrategy.dialog(),
                            ) { args ->
                                DeleteTabGroupConfirmationDialog(
                                    onConfirmDelete = {
                                        tabsTrayStore.dispatch(TabGroupAction.DeleteConfirmed(args.group))
                                    },
                                    onCancel = {
                                        tabsTrayStore.dispatch(TabsTrayAction.NavigateBackInvoked)
                                    },
                                )
                            }

                            entry<TabManagerNavDestination.EditTabGroup>(
                                metadata = BottomSheetSceneStrategy.bottomSheet(
                                    skipPartiallyExpanded = true,
                                    handleContentDescription = stringResource(
                                        id = R.string.edit_tab_group_bottom_sheet_grabber_content_description,
                                    ),
                                    showBetaLabel = true,
                                ),
                            ) {
                                EditTabGroup(tabsTrayStore = tabsTrayStore)
                            }

                            entry<TabManagerNavDestination.AddToTabGroup>(
                                metadata = BottomSheetSceneStrategy.bottomSheet(
                                    handleContentDescription = stringResource(
                                        id = R.string.add_to_tab_group_bottom_sheet_grabber_content_description,
                                    ),
                                    showBetaLabel = true,
                                ),
                            ) {
                                AddToTabGroup(
                                    tabGroups = tabsTrayStore.state.tabGroupState.groups,
                                    onAddToNewTabGroup = {
                                        tabsTrayStore.dispatch(TabGroupAction.AddToNewTabGroup)
                                    },
                                    onAddToExistingTabGroup = { group ->
                                        tabsTrayStore.dispatch(
                                            TabGroupAction.SelectedTabsAddedToGroup(groupId = group.id),
                                        )
                                    },
                                )
                            }

                            entry<TabManagerNavDestination.CloseTabAndDeleteGroupConfirmationDialog>(
                                metadata = DialogSceneStrategy.dialog(),
                            ) { args ->
                                CloseLastTabAndDeleteTabGroupConfirmationDialog(
                                    onConfirmDelete = {
                                        tabsTrayStore.dispatch(
                                            TabGroupAction.CloseTabAndDeleteGroupConfirmed(args.group),
                                        )
                                    },
                                    onCancel = {
                                        tabsTrayStore.dispatch(TabsTrayAction.NavigateBackInvoked)
                                    },
                                )
                            }
                        },
                    )
                }
            }
        }
    }

    private fun setupStore(): TabsTrayStore {
        val args by navArgs<TabManagementFragmentArgs>()
        val settings = requireContext().settings()

        args.accessPoint.takeIf { it != AccessPoint.None }?.let {
            TabsTray.accessPoint[it.name.lowercase()].add()
        }

        return storeProvider.get { restoredState ->
            TabsTrayStore(
                initialState = restoredState?.copy(
                    config = restoredState.config.copy(displayTabsInGrid = settings.gridTabView),
                ) ?: createInitialState(args, settings),
                middlewares = listOf(
                    TabsTrayTelemetryMiddleware(requireComponents.nimbus.events),
                    TabSearchMiddleware(),
                    TabSearchNavigationMiddleware(onSearchResultClicked = ::performTabClick),
                    TabStorageMiddleware(
                        inactiveTabsEnabled = requireComponents.settings.inactiveTabsAreEnabled,
                        tabGroupsEnabled = requireComponents.settings.tabGroupsEnabled,
                        tabDataFlow = requireComponents.core.store.stateFlow.map { TabData(it) },
                        tabGroupRepository = requireComponents.core.tabGroupRepository,
                        removeTabsUseCase = requireComponents.useCases.tabsUseCases.removeTabs,
                        moveTabsUseCase = requireComponents.useCases.tabsUseCases.moveTabs,
                        mainScope = lifecycleScope,
                    ),
                ),
            )
        }
    }

    private fun createInitialState(
        args: TabManagementFragmentArgs,
        settings: Settings,
    ): TabsTrayState {
        val appState = requireComponents.appStore.state
        val coreState = requireComponents.core.store.state

        return TabsTrayState(
            selectedPage = args.page,
            mode = if (args.enterMultiselect) TabsTrayState.Mode.Select(emptySet()) else TabsTrayState.Mode.Normal,
            inactiveTabs = TabsTrayState.InactiveTabsState(
                isExpanded = appState.inactiveTabsExpanded,
                showCFR = settings.shouldShowInactiveTabsOnboardingPopup &&
                    settings.canShowCfr && settings.cfrPopupsEnabled,
                showAutoCloseDialog = settings.shouldShowInactiveTabsAutoCloseDialog(
                    coreState.actualInactiveTabs(settings).size,
                ),
            ),
            privateBrowsing = TabsTrayState.PrivateBrowsingState(
                isLocked = appState.isPrivateScreenLocked,
                showLockBanner = shouldShowLockPbmBanner(
                    isPrivateMode = appState.mode.isPrivate,
                    hasPrivateTabs = coreState.privateTabs.isNotEmpty(),
                    biometricAvailable = BiometricManager.from(requireContext()).isHardwareAvailable(),
                    privateLockEnabled = settings.privateBrowsingModeLocked,
                    shouldShowBanner = shouldShowBanner(settings),
                ),
            ),
            sync = TabsTrayState.SyncState(isSignedIn = settings.signedInFxaAccount),
            config = TabsTrayState.TabsTrayConfig(
                tabGroupsEnabled = settings.tabGroupsEnabled,
                tabGroupsDragAndDropEnabled = settings.tabGroupsDragAndDropEnabled,
                tabGroupsOnboardingEnabled = settings.tabGroupsOnboardingEnabled,
                displayTabsInGrid = settings.gridTabView,
                isInDebugMode = Config.channel.isDebug || requireComponents.settings.showSecretDebugMenuThisSession,
                showTabAutoCloseBanner = settings.shouldShowAutoCloseTabsBanner &&
                    settings.canShowCfr && settings.cfrPopupsEnabled,
            ),
        )
    }

    /**
     * @param tab: TabsTrayItem
     *
     * This method performs the tab click handling.  Separate from
     * onTabClick() in that an animation may play prior to handling the user action.
     */
    private fun performTabClick(tab: TabsTrayItem.Tab) {
        if (shouldConsiderShowingTabSwipeCFR()) {
            val normalTabs = tabsTrayStore.state.normalTabsState.items
            val currentTabId = tabsTrayStore.state.selectedTabId

            if (normalTabs.size >= 2 && currentTabId != null) {
                val currentTabPosition = getTabPositionFromId(normalTabs, currentTabId)
                val newTabPosition = getTabPositionFromId(normalTabs, tab.id)

                if (abs(currentTabPosition - newTabPosition) == 1) {
                    requireContext().settings().shouldShowTabSwipeCFR =
                        true
                }
            }
        }

        tabManagerInteractor.onTabSelected(
            tab = tab,
            source = TAB_MANAGER_FEATURE_NAME,
        )
    }

    private fun shouldConsiderShowingTabSwipeCFR(settings: Settings = requireContext().settings()) =
        with(settings) {
            !hasShownTabSwipeCFR && !isTabStripEnabled && isSwipeToolbarToSwitchTabsEnabled
        }

    override fun onPause() {
        super.onPause()
        recordBreadcrumb("TabManagementFragment onPause")
    }

    private fun shouldShowBanner(settings: Settings) =
        with(settings) { privateBrowsingLockedFeatureEnabled && shouldShowLockPbmBanner }

    override fun onStart() {
        super.onStart()
        recordBreadcrumb("TabManagementFragment onStart")
        findPreviousDialogFragment()?.let { dialog ->
            dialog.onAcceptClicked = ::onCancelDownloadWarningAccepted
        }
    }

    override fun onDestroyView() {
        super.onDestroyView()
        recordBreadcrumb("TabManagementFragment onDestroyView")
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        TabsTray.opened.record(NoExtras())

        inactiveTabsBinding.set(
            feature = InactiveTabsBinding(
                tabsTrayStore = tabsTrayStore,
                appStore = requireComponents.appStore,
            ),
            owner = this,
            view = view,
        )
        pbmLockStatusBinding.set(
            feature = PbmLockStatusBinding(
                tabsTrayStore = tabsTrayStore,
                appStore = requireComponents.appStore,
            ),
            owner = this,
            view = view,
        )

        secureTabManagerBinding.set(
            feature = SecureTabManagerBinding(
                store = tabsTrayStore,
                settings = requireComponents.settings,
                window = activity?.window,
            ),
            owner = this,
            view = view,
        )

        syncedTabsIntegration.set(
            feature = SyncedTabsIntegration(
                store = tabsTrayStore,
                context = requireContext(),
                navController = findNavController(),
                storage = requireComponents.backgroundServices.syncedTabsStorage,
                commands = requireComponents.backgroundServices.syncedTabsCommands,
                accountManager = requireComponents.backgroundServices.accountManager,
                lifecycleOwner = this,
            ),
            owner = this,
            view = view,
        )

        if (requireContext().settings().showPrivacyReportInTabManager) {
            trackersBlockedFeature.set(
                feature = TrackersBlockedFeature(
                    browserStore = requireComponents.core.store,
                    appStore = requireComponents.appStore,
                    currentSessionId = requireComponents.core.store.state.selectedTabId,
                    trackingProtectionUseCases = requireComponents.useCases.trackingProtectionUseCases,
                ),
                owner = this,
                view = view,
            )
        }

        setFragmentResultListener(ShareFragment.RESULT_KEY) { _, _ ->
            dismissTabManager()
        }
    }

    override fun onResume() {
        super.onResume()
        hideToolbar()
    }

    private fun onCancelDownloadWarningAccepted(tabId: String?, source: String?) {
        if (tabId != null) {
            tabManagerInteractor.onDeletePrivateTabWarningAccepted(tabId, source)
        } else {
            tabManagerController.onCloseAllPrivateTabsWarningConfirmed(private = true)
        }
    }

    private fun showCancelledDownloadWarning(downloadCount: Int, tabId: String?, source: String?) {
        recordBreadcrumb("DownloadCancelDialogFragment show")

        val dialog = DownloadCancelDialogFragment.newInstance(
            downloadCount = downloadCount,
            tabId = tabId,
            source = source,
            promptStyling = DownloadCancelDialogFragment.PromptStyling(
                gravity = Gravity.BOTTOM,
                shouldWidthMatchParent = true,
                positiveButtonBackgroundColor = ThemeManager.resolveAttribute(
                    R.attr.accent,
                    requireContext(),
                ),
                positiveButtonTextColor = ThemeManager.resolveAttribute(
                    R.attr.textOnColorPrimary,
                    requireContext(),
                ),
                positiveButtonRadius = pixelSizeFor(R.dimen.tab_corner_radius).toFloat(),
            ),
            onPositiveButtonClicked = ::onCancelDownloadWarningAccepted,
        )
        dialog.show(parentFragmentManager, DOWNLOAD_CANCEL_DIALOG_FRAGMENT_TAG)
    }

    @UiThread
    internal fun showUndoSnackbarForSyncedTab(closeOperation: CloseTabsUseCases.UndoableOperation) {
        lifecycleScope.launch {
            snackbarHostState.displaySnackbar(
                message = getString(R.string.snackbar_tab_closed),
                actionLabel = getString(R.string.snackbar_deleted_undo),
                timeout = requireContext().getSnackbarTimeout(hasAction = true),
                onActionPerformed = { closeOperation.undo() },
            )
        }
    }

    private fun showUndoSnackbarForTab(isPrivate: Boolean) {
        val snackbarMessage =
            when (isPrivate) {
                true -> getString(R.string.snackbar_private_tab_closed)
                false -> getString(R.string.snackbar_tab_closed)
            }
        val page = if (isPrivate) Page.PrivateTabs else Page.NormalTabs
        val undoUseCases = requireComponents.useCases.tabsUseCases.undo

        lifecycleScope.launch {
            snackbarHostState.displaySnackbar(
                message = snackbarMessage,
                actionLabel = getString(R.string.snackbar_deleted_undo),
                timeout = requireContext().getSnackbarTimeout(hasAction = true),
                onActionPerformed = {
                    undoUseCases.invoke()
                    runIfFragmentIsAttached {
                        tabsTrayStore.dispatch(TabsTrayAction.PageSelected(page))
                    }
                },
            )
        }
    }

    private fun showUndoSnackbarForInactiveTab(numClosed: Int) {
        val snackbarMessage =
            when (numClosed == 1) {
                true -> getString(R.string.snackbar_tab_closed)
                false -> getString(R.string.snackbar_num_tabs_closed, numClosed.toString())
            }

        lifecycleScope.launch {
            snackbarHostState.displaySnackbar(
                message = snackbarMessage,
                actionLabel = getString(R.string.snackbar_deleted_undo),
                timeout = requireContext().getSnackbarTimeout(hasAction = true),
                onActionPerformed = {
                    requireComponents.useCases.tabsUseCases.undo.invoke()
                    tabsTrayStore.dispatch(TabsTrayAction.PageSelected(Page.NormalTabs))
                },
            )
        }
    }

    internal val homeViewModel: HomeScreenViewModel by activityViewModels()

    @VisibleForTesting
    internal fun navigateToHomeAndDeleteSession(
        sessionId: String,
        navControllerProvider: NavControllerProvider = DefaultNavControllerProvider(),
    ) {
        homeViewModel.sessionToDelete = sessionId
        navControllerProvider
            .getNavController(this)
            .navigate(TabManagementFragmentDirections.actionGlobalHome())
    }

    @VisibleForTesting
    internal fun getTabPositionFromId(tabsList: List<TabsTrayItem>, tabId: String): Int {
        tabsList.forEachIndexed { index, tab ->
            if (tab is TabsTrayItem.Tab && tab.id == tabId) return index
        }
        return -1
    }

    /**
     * Dismisses the Tab Manager.
     *
     * @param navController [NavController] used to perform the navigation action.
     */
    @VisibleForTesting
    internal fun dismissTabManager(
        navController: NavController = findNavController(),
    ) {
        // This should always be the last thing we do because nothing (e.g. telemetry)
        // is guaranteed after that.
        recordBreadcrumb("TabManagementFragment dismissTabManager")
        navController.popBackStack()
    }

    /**
     * Records a breadcrumb for crash reporting.
     *
     * @param message The message to record.
     */
    @VisibleForTesting
    internal fun recordBreadcrumb(message: String) {
        context?.components?.analytics?.crashReporter?.recordCrashBreadcrumb(
            Breadcrumb(message = message),
        )
    }

    private fun showCollectionSnackbar(
        tabSize: Int,
        isNewCollection: Boolean = false,
    ) {
        val messageResId = when {
            isNewCollection -> R.string.create_collection_tabs_saved_new_collection_2
            tabSize == 1 -> R.string.create_collection_tab_saved_2
            else -> return // Don't show snackbar for multiple tabs
        }
        lifecycleScope.launch {
            snackbarHostState.displaySnackbar(
                message = getString(messageResId),
                timeout = requireContext().getSnackbarTimeout(hasAction = false),
            )
        }
    }

    private fun showBookmarkSnackbar(
        tabSize: Int,
        parentFolderTitle: String?,
    ) {
        val displayFolderTitle = parentFolderTitle ?: getString(R.string.library_bookmarks)
        val displayResId = when {
            tabSize > 1 -> {
                R.string.snackbar_message_bookmarks_saved_in_2
            }

            else -> {
                R.string.bookmark_saved_in_folder_snackbar
            }
        }
        lifecycleScope.launch {
            snackbarHostState.displaySnackbar(
                message = getString(displayResId, displayFolderTitle),
                actionLabel = getString(R.string.create_collection_view),
                timeout = requireContext().getSnackbarTimeout(hasAction = true),
                onActionPerformed = {
                    findNavController().navigate(
                        TabManagementFragmentDirections.actionGlobalBookmarkFragment(BookmarkRoot.Mobile.id),
                    )
                },
            )
        }
    }

    private fun findPreviousDialogFragment(): DownloadCancelDialogFragment? {
        return parentFragmentManager
            .findFragmentByTag(DOWNLOAD_CANCEL_DIALOG_FRAGMENT_TAG) as? DownloadCancelDialogFragment
    }

    private fun showInactiveTabsAutoCloseConfirmationSnackbar() {
        lifecycleScope.launch {
            snackbarHostState.displaySnackbar(
                message = getString(R.string.inactive_tabs_auto_close_message_snackbar),
                timeout = requireContext().getSnackbarTimeout(hasAction = false),
            )
        }
    }

    /**
     * This can only turn the feature ON and should not handle turning the feature OFF.
     */
    private fun onTabsTrayPbmLockedClick(
        navControllerProvider: NavControllerProvider = DefaultNavControllerProvider(),
    ) {
        val isAuthenticatorAvailable =
            BiometricManager.from(requireContext()).isAuthenticatorAvailable()
        if (!isAuthenticatorAvailable) {
            navControllerProvider.getNavController(this)
                .navigate(TabManagementFragmentDirections.actionGlobalPrivateBrowsingFragment())
        } else {
            DefaultBiometricUtils.bindBiometricsCredentialsPromptOrShowWarning(
                titleRes = R.string.pbm_authentication_enable_lock,
                view = requireView(),
                onShowPinVerification = { intent -> enablePbmPinLauncher.launch(intent) },
                onAuthSuccess = {
                    PrivateBrowsingLocked.bannerPositiveClicked.record()
                    PrivateBrowsingLocked.authSuccess.record()
                    PrivateBrowsingLocked.featureEnabled.record()
                    requireContext().settings().privateBrowsingModeLocked = true
                    requireContext().settings().shouldShowLockPbmBanner = false
                },
                onAuthFailure = {
                    PrivateBrowsingLocked.authFailure.record()
                },
            )
        }
    }

    private fun onTabsTrayDismissed() {
        recordBreadcrumb("TabManagementFragment onTabsTrayDismissed")
        TabsTray.closed.record(NoExtras())
        dismissTabManager()
    }

    @VisibleForTesting
    internal fun onTabPageClick(
        tabsTrayInteractor: TabManagerInteractor,
        page: Page,
    ) {
        tabsTrayInteractor.onTabPageClicked(page)
    }

    private fun openPrivateTabsPage() {
        tabManagerInteractor.onTabPageClicked(Page.PrivateTabs)
    }

    /**
     * @param selectedPage: The currently selected [TabsTray] [Page]
     * @param mode: The current [TabsTrayState] operating mode
     * @param tabState: The selected [TabsTrayItem.Tab]
     * The TabsTray transition animation should be performed if enabled in settings,
     * if the selected tab is on the current active tab page,
     * and the current TabsTray mode is the default (normal) mode (e.g., not a special select mode).
     */
    internal fun shouldPerformTransitionAnimation(
        selectedPage: Page,
        mode: TabsTrayState.Mode,
        tabState: TabsTrayItem.Tab?,
    ): Boolean {
        return requireContext().settings().tabManagerOpeningAnimationEnabled &&
            tabMatchesPage(selectedPage, tabState) &&
            mode is TabsTrayState.Mode.Normal
    }

    /**
     * @param selectedPage: The selected [TabsTray] [Page]
     * @param tabState: The selected [TabsTrayItem.Tab]
     *
     * Returns true if the selected page is private and the tab is private, or
     * the selected page is normal and the tab is normal.  Returns false otherwise.
     */
    private fun tabMatchesPage(selectedPage: Page, tabState: TabsTrayItem.Tab?): Boolean {
        return (selectedPage == Page.NormalTabs && tabState?.private == false) ||
            (selectedPage == Page.PrivateTabs && tabState?.private == true)
    }

    /**
     * Determines whether the Lock Private Browsing Mode banner should be shown.
     *
     * The banner is shown only when all of the following conditions are met:
     * - The app is currently in private browsing mode
     * - There are existing private tabs open
     * - Biometric hardware is available on the device
     * - The user has not already enabled the private browsing lock
     * - The user has not already dismissed or acknowledged the Pbm banner from tabs tray
     *
     * We only want to show the banner when the feature is available,
     * applicable, and relevant to the current user context.
     */
    @VisibleForTesting
    internal fun shouldShowLockPbmBanner(
        isPrivateMode: Boolean,
        hasPrivateTabs: Boolean,
        biometricAvailable: Boolean,
        privateLockEnabled: Boolean,
        shouldShowBanner: Boolean,
    ): Boolean {
        return isPrivateMode && hasPrivateTabs && biometricAvailable && !privateLockEnabled && shouldShowBanner
    }

    private companion object {
        private const val DOWNLOAD_CANCEL_DIALOG_FRAGMENT_TAG = "DOWNLOAD_CANCEL_DIALOG_FRAGMENT_TAG"
        private const val TAB_MANAGER_FEATURE_NAME = "Tab Manager"
    }
}
