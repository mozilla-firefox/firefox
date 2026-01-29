/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu

import android.app.Activity
import android.app.Dialog
import android.app.PendingIntent
import android.content.Intent
import android.content.res.Configuration
import android.graphics.Color
import android.os.Build
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.view.animation.OvershootInterpolator
import androidx.activity.compose.BackHandler
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.SizeTransform
import androidx.compose.animation.core.FastOutLinearInEasing
import androidx.compose.animation.core.LinearOutSlowInEasing
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.ComposeView
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.ViewCompositionStrategy
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.core.graphics.drawable.toDrawable
import androidx.core.net.toUri
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat.Type.systemBars
import androidx.lifecycle.lifecycleScope
import androidx.navigation.fragment.findNavController
import androidx.navigation.fragment.navArgs
import com.google.android.material.bottomsheet.BottomSheetBehavior
import com.google.android.material.bottomsheet.BottomSheetDialogFragment
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.serialization.json.Json
import mozilla.components.browser.state.selector.findCustomTab
import mozilla.components.browser.state.selector.selectedTab
import mozilla.components.concept.engine.translate.TranslationSupport
import mozilla.components.concept.engine.translate.findLanguage
import mozilla.components.feature.addons.Addon
import mozilla.components.service.fxa.manager.AccountState.NotAuthenticated
import mozilla.components.support.base.feature.ViewBoundFeatureWrapper
import mozilla.components.support.ktx.android.util.dpToPx
import mozilla.components.support.utils.ext.getWindowInsets
import mozilla.components.support.utils.ext.isLandscape
import mozilla.components.support.utils.ext.top
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.fenix.GleanMetrics.Events
import org.mozilla.fenix.R
import org.mozilla.fenix.automotive.isAndroidAutomotiveAvailable
import org.mozilla.fenix.components.appstate.SupportedMenuNotifications
import org.mozilla.fenix.components.components
import org.mozilla.fenix.components.menu.compose.Addons
import org.mozilla.fenix.components.menu.compose.CustomTabAddons
import org.mozilla.fenix.components.menu.compose.CustomTabMenu
import org.mozilla.fenix.components.menu.compose.MainMenu
import org.mozilla.fenix.components.menu.compose.MenuCFRState
import org.mozilla.fenix.components.menu.compose.MenuDialogBottomSheet
import org.mozilla.fenix.components.menu.compose.MoreSettingsSubmenu
import org.mozilla.fenix.components.menu.middleware.MenuDialogMiddleware
import org.mozilla.fenix.components.menu.middleware.MenuNavigationMiddleware
import org.mozilla.fenix.components.menu.middleware.MenuTelemetryMiddleware
import org.mozilla.fenix.components.menu.store.BrowserMenuState
import org.mozilla.fenix.components.menu.store.ExtensionMenuState
import org.mozilla.fenix.components.menu.store.MenuAction
import org.mozilla.fenix.components.menu.store.MenuState
import org.mozilla.fenix.components.menu.store.MenuStore
import org.mozilla.fenix.components.menu.store.TranslationInfo
import org.mozilla.fenix.components.menu.store.WebExtensionMenuItem
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.openSetDefaultBrowserOption
import org.mozilla.fenix.ext.openToBrowser
import org.mozilla.fenix.ext.pixelSizeFor
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.ext.runIfFragmentIsAttached
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.nimbus.FxNimbus
import org.mozilla.fenix.settings.SupportUtils
import org.mozilla.fenix.settings.deletebrowsingdata.DefaultDeleteBrowsingDataController
import org.mozilla.fenix.settings.deletebrowsingdata.DefaultDeleteBrowsingDataController.DataStorage
import org.mozilla.fenix.settings.deletebrowsingdata.DefaultDeleteBrowsingDataController.DeleteDataUseCases
import org.mozilla.fenix.settings.deletebrowsingdata.DefaultDeleteBrowsingDataController.Stores
import org.mozilla.fenix.settings.deletebrowsingdata.DeleteBrowsingDataController
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.utils.DELAY_MS_MAIN_MENU
import org.mozilla.fenix.utils.DELAY_MS_SUB_MENU
import org.mozilla.fenix.utils.DURATION_MS_MAIN_MENU
import org.mozilla.fenix.utils.DURATION_MS_SUB_MENU
import org.mozilla.fenix.utils.contentGrowth
import org.mozilla.fenix.utils.enterMenu
import org.mozilla.fenix.utils.enterSubmenu
import org.mozilla.fenix.utils.exitMenu
import org.mozilla.fenix.utils.exitSubmenu
import org.mozilla.fenix.utils.lastSavedFolderCache
import org.mozilla.fenix.webcompat.DefaultWebCompatReporterMoreInfoSender
import org.mozilla.fenix.webcompat.middleware.DefaultWebCompatReporterRetrievalService
import org.mozilla.fenix.webcompat.middleware.WebCompatInfoDeserializer
import com.google.android.material.R as materialR

// EXPANDED_MIN_RATIO is used for BottomSheetBehavior.halfExpandedRatio().
// That value needs to be less than the PEEK_HEIGHT.
// If EXPANDED_MIN_RATIO is greater than the PEEK_HEIGHT, then there will be
// three states instead of the expected two states required by design.
private const val PEEK_HEIGHT = 460
private const val EXPANDED_MIN_RATIO = 0.0001f
private const val EXPANDED_OFFSET = 56
private const val HIDING_FRICTION = 0.9f
private const val PRIVATE_HOME_MENU_BACKGROUND_ALPHA = 100

private object MenuAnimationConfig {
    const val DURATION = 300L
    const val START_OFFSET_RATIO = 0.2f
}

/**
 * A bottom sheet fragment displaying the menu dialog.
 */
@Suppress("LargeClass")
class MenuDialogFragment : BottomSheetDialogFragment() {

    private val args by navArgs<MenuDialogFragmentArgs>()
    private val webExtensionsMenuBinding = ViewBoundFeatureWrapper<WebExtensionsMenuBinding>()
    private var bottomSheetBehavior: BottomSheetBehavior<View>? = null
    private var isPrivate: Boolean = false

    private val deleteBrowsingDataController: DeleteBrowsingDataController by lazy {
        DefaultDeleteBrowsingDataController(
            deleteDataUseCases = DeleteDataUseCases(
                removeAllTabs =
                    requireComponents.useCases.tabsUseCases.removeAllTabs,
                removeAllDownloads =
                    requireComponents.useCases.downloadUseCases.removeAllDownloads,
            ),
            dataStorage = DataStorage(
                history = requireComponents.core.historyStorage,
                permissions = requireComponents.core.permissionStorage,
            ),
            stores = Stores(
                appStore = requireComponents.appStore,
                browserStore = requireComponents.core.store,
            ),
            engine = requireComponents.core.engine,
            settings = requireComponents.settings,
            coroutineContext = lifecycleScope.coroutineContext,
        )
    }

    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog {
        Events.toolbarMenuVisible.record(NoExtras())

        return super.onCreateDialog(savedInstanceState).apply {
            setOnShowListener {
                val safeActivity = activity ?: return@setOnShowListener
                val appStore = safeActivity.components.appStore

                isPrivate = appStore.state.mode.isPrivate

                if (isPrivate && args.accesspoint == MenuAccessPoint.Home) {
                    window?.setBackgroundDrawable(
                        Color.BLACK.toDrawable().mutate().apply {
                            alpha = PRIVATE_HOME_MENU_BACKGROUND_ALPHA
                        },
                    )
                }

                val bottomSheet = findViewById<View?>(materialR.id.design_bottom_sheet)
                bottomSheet?.let {
                    ViewCompat.setOnApplyWindowInsetsListener(it) { view, insets ->
                        view.setPadding(
                            0,
                            insets.getInsets(systemBars()).top,
                            0,
                            insets.getInsets(systemBars()).bottom,
                        )
                        insets
                    }
                }
                bottomSheet?.setBackgroundResource(R.drawable.bottom_sheet_with_top_rounded_corners)

                // https://bugzilla.mozilla.org/show_bug.cgi?id=1982004
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.VANILLA_ICE_CREAM) {
                    bottomSheet?.let { sheet ->
                        sheet.translationY = sheet.height * MenuAnimationConfig.START_OFFSET_RATIO
                        sheet.animate()
                            .translationY(0f)
                            .setInterpolator(OvershootInterpolator())
                            .setDuration(MenuAnimationConfig.DURATION)
                            .start()
                    }
                }

                bottomSheetBehavior = bottomSheet?.let {
                    BottomSheetBehavior.from(it).apply {
                        maxWidth = calculateMenuSheetWidth()
                        isFitToContents = true
                        peekHeight = PEEK_HEIGHT.dpToPx(resources.displayMetrics)
                        halfExpandedRatio = EXPANDED_MIN_RATIO
                        maxHeight = calculateMenuSheetHeight()
                        skipCollapsed = true
                        state = BottomSheetBehavior.STATE_EXPANDED
                        hideFriction = HIDING_FRICTION
                    }
                }
            }
        }
    }

    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)
        bottomSheetBehavior?.apply {
            maxWidth = calculateMenuSheetWidth()
            maxHeight = calculateMenuSheetHeight()
        }
    }

    @Suppress("LongMethod", "CyclomaticComplexMethod", "CognitiveComplexMethod")
    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View = ComposeView(requireContext()).apply {
        setViewCompositionStrategy(ViewCompositionStrategy.DisposeOnViewTreeLifecycleDestroyed)

        setContent {
            FirefoxTheme {
                val context = LocalContext.current

                val components = components
                val settings = components.settings
                val defaultBrowser = settings.isDefaultBrowser
                val appStore = components.appStore
                val browserStore = components.core.store

                val selectedTab = browserStore.state.selectedTab
                val customTab = args.customTabSessionId?.let {
                    browserStore.state.findCustomTab(it)
                }

                val appLinksUseCases = components.useCases.appLinksUseCases
                val webAppUseCases = components.useCases.webAppUseCases

                val webCompatReporterMoreInfoSender =
                    DefaultWebCompatReporterMoreInfoSender(
                        webCompatReporterRetrievalService =
                            DefaultWebCompatReporterRetrievalService(
                                browserStore = requireComponents.core.store,
                                webCompatInfoDeserializer = WebCompatInfoDeserializer(
                                    json = Json {
                                        ignoreUnknownKeys = true
                                        useAlternativeNames = false
                                    },
                                ),
                            ),
                    )

                val coroutineScope = rememberCoroutineScope()
                val scrollState = rememberScrollState()

                val store = remember {
                    MenuStore(
                        initialState = MenuState(
                            browserMenuState = if (selectedTab != null) {
                                BrowserMenuState(selectedTab = selectedTab)
                            } else {
                                null
                            },
                            customTabSessionId = args.customTabSessionId,
                            isDesktopMode = when (args.accesspoint) {
                                MenuAccessPoint.Home -> {
                                    false // this is not supported on Home
                                }
                                MenuAccessPoint.External -> {
                                    customTab?.content?.desktopMode ?: false
                                }
                                else -> {
                                    selectedTab?.content?.desktopMode ?: false
                                }
                            },
                            extensionMenuState = ExtensionMenuState(
                                accesspoint = args.accesspoint,
                            ),
                        ),
                        middleware = listOf(
                            MenuDialogMiddleware(
                                appStore = appStore,
                                addonManager = components.addonManager,
                                settings = settings,
                                bookmarksStorage = components.core.bookmarksStorage,
                                pinnedSiteStorage = components.core.pinnedSiteStorage,
                                appLinksUseCases = appLinksUseCases,
                                addBookmarkUseCase = components.useCases.bookmarksUseCases.addBookmark,
                                addPinnedSiteUseCase = components.useCases.topSitesUseCase.addPinnedSites,
                                removePinnedSitesUseCase = components.useCases.topSitesUseCase.removeTopSites,
                                requestDesktopSiteUseCase = components.useCases.sessionUseCases.requestDesktopSite,
                                materialAlertDialogBuilder = MaterialAlertDialogBuilder(context),
                                topSitesMaxLimit = components.settings.topSitesMaxLimit,
                                onDeleteAndQuit = {
                                    activity?.let { activity ->
                                        activity.lifecycleScope.launch {
                                            deleteBrowsingDataController.clearBrowsingDataOnQuit {
                                                activity.finishAndRemoveTask()
                                            }
                                        }
                                    }
                                },
                                onDismiss = {
                                    withContext(Dispatchers.Main) {
                                        this@MenuDialogFragment.dismiss()
                                    }
                                },
                                onSendPendingIntentWithUrl = ::sendPendingIntentWithUrl,
                                mainDispatcher = Dispatchers.Main,
                                lastSavedFolderCache = context.settings().lastSavedFolderCache,
                            ),
                            MenuNavigationMiddleware(
                                browserStore = browserStore,
                                navController = findNavController(),
                                openToBrowser = ::openToBrowser,
                                sessionUseCases = components.useCases.sessionUseCases,
                                webAppUseCases = webAppUseCases,
                                settings = settings,
                                onDismiss = {
                                    withContext(Dispatchers.Main) {
                                        this@MenuDialogFragment.dismiss()
                                    }
                                },
                                scope = coroutineScope,
                                customTab = customTab,
                                webCompatReporterMoreInfoSender = webCompatReporterMoreInfoSender,
                            ),
                            MenuTelemetryMiddleware(
                                accessPoint = args.accesspoint,
                            ),
                        ),
                    )
                }

                val descCustom = stringResource(R.string.browser_custom_tab_menu_handlebar_content_description)
                val descMain = stringResource(R.string.browser_close_main_menu_handlebar_content_description)

                var handlebarContentDescription by remember {
                    mutableStateOf(
                        if (args.accesspoint == MenuAccessPoint.External) descCustom else descMain,
                    )
                }

                var isExtensionsExpanded by remember { mutableStateOf(false) }

                var isMoreMenuExpanded by remember { mutableStateOf(false) }

                MenuDialogBottomSheet(
                    modifier = Modifier
                        .padding(top = 16.dp, bottom = 16.dp)
                        .width(32.dp),
                    onRequestDismiss = ::dismiss,
                    handlebarContentDescription = handlebarContentDescription,
                    isMenuDragBarDark = !settings.shouldUseBottomToolbar &&
                            !settings.shouldUseExpandedToolbar &&
                            (isExtensionsExpanded || isMoreMenuExpanded) &&
                            args.accesspoint == MenuAccessPoint.Browser,
                    cornerShape = RoundedCornerShape(topStart = 28.dp, topEnd = 28.dp),
                    menuCfrState = if (settings.shouldShowMenuCFR) {
                        MenuCFRState(
                            showCFR = settings.shouldShowMenuCFR,
                            titleRes = R.string.menu_cfr_title,
                            messageRes = R.string.menu_cfr_body,
                            orientation = appStore.state.orientation,
                            onShown = {
                                store.dispatch(MenuAction.OnCFRShown)
                            },
                            onDismiss = {
                                store.dispatch(MenuAction.OnCFRDismiss)
                            },
                        )
                    } else {
                        null
                    },
                ) {
                    val syncStore = components.backgroundServices.syncStore
                    val tabCollectionStorage = components.core.tabCollectionStorage
                    val printContentUseCase = components.useCases.sessionUseCases.printContent
                    val saveToPdfUseCase = components.useCases.sessionUseCases.saveToPdf
                    val isTranslationEngineSupported =
                        browserStore.state.translationEngine.isEngineSupported ?: false
                    val isTranslationSupported =
                        isTranslationEngineSupported &&
                            FxNimbus.features.translations.value().mainFlowBrowserMenuEnabled
                    val isPdf = selectedTab?.content?.isPdf ?: false
                    val isWebCompatEnabled by remember {
                        store.stateFlow.map { it.isWebCompatEnabled }
                    }.collectAsState(initial = store.state.isWebCompatEnabled)
                    val supportedLanguages = components.core.store.state.translationEngine.supportedLanguages
                    val translateLanguageCode = selectedTab?.translationsState?.translationEngineState
                        ?.requestedTranslationPair?.toLanguage
                    val isExtensionsProcessDisabled = browserStore.state.extensionsProcessDisabled
                    val isWebCompatReporterSupported =
                        FxNimbus.features.menuRedesign.value().reportSiteIssue
                    val isDesktopMode by remember {
                        store.stateFlow.map { state -> state.isDesktopMode }
                    }.collectAsState(initial = false)

                    webExtensionsMenuBinding.set(
                        feature = WebExtensionsMenuBinding(
                            browserStore = browserStore,
                            customTabId = args.customTabSessionId,
                            menuStore = store,
                            iconSize = 24.dpToPx(requireContext().resources.displayMetrics),
                            onDismiss = { this@MenuDialogFragment.dismiss() },
                        ),
                        owner = this@MenuDialogFragment,
                        view = this,
                    )

                    val recommendedAddons by remember {
                        store.stateFlow
                            .map { state ->
                                state.extensionMenuState.recommendedAddons
                            }
                    }.collectAsState(initial = emptyList())

                    val isBookmarked by remember {
                        store.stateFlow
                            .map { state ->
                                state.browserMenuState != null &&
                                    state.browserMenuState.bookmarkState.isBookmarked
                            }
                    }.collectAsState(initial = false)

                    val isPinned by remember {
                        store.stateFlow
                            .map { state ->
                                state.browserMenuState != null &&
                                    state.browserMenuState.isPinned
                            }
                    }.collectAsState(initial = false)

                    val isReaderViewActive by remember {
                        store.stateFlow
                            .map { state ->
                                state.browserMenuState != null &&
                                    state.browserMenuState.selectedTab.readerState.active
                            }
                    }.collectAsState(initial = false)

                    val addonInstallationInProgress by remember {
                        store.stateFlow
                            .map { state -> state.extensionMenuState.addonInstallationInProgress }
                    }.collectAsState(initial = null)

                    val browserWebExtensionMenuItem by remember {
                        store.stateFlow
                            .map { state -> state.extensionMenuState.browserWebExtensionMenuItem }
                    }.collectAsState(initial = emptyList())

                    val availableAddons by remember {
                        store.stateFlow.map { state -> state.extensionMenuState.availableAddons }
                    }.collectAsState(initial = emptyList())

                    val webExtensionsCount by remember {
                        store.stateFlow.map { state -> state.extensionMenuState.webExtensionsCount }
                    }.collectAsState(initial = 0)

                    val isAllWebExtensionsDisabled by remember {
                        store.stateFlow
                            .map { state -> state.extensionMenuState.allWebExtensionsDisabled }
                    }.collectAsState(initial = false)

                    val initRoute = when (args.accesspoint) {
                        MenuAccessPoint.Browser,
                        MenuAccessPoint.Home,
                        -> Route.MainMenu

                        MenuAccessPoint.External -> Route.CustomTabMenu
                    }

                    val translationInfo = TranslationInfo(
                        isTranslationSupported = isTranslationSupported,
                        isPdf = isPdf,
                        isTranslated = selectedTab?.translationsState?.isTranslated
                            ?: false,
                        translatedLanguage = if (
                            translateLanguageCode != null && supportedLanguages != null
                        ) {
                            TranslationSupport(
                                fromLanguages = supportedLanguages.fromLanguages,
                                toLanguages = supportedLanguages.toLanguages,
                            ).findLanguage(translateLanguageCode)?.localizedDisplayName
                                ?: ""
                        } else {
                            ""
                        },
                        onTranslatePageMenuClick = {
                            selectedTab?.let {
                                store.dispatch(MenuAction.Navigate.Translate)
                            }
                        },
                    )

                    val contentState: Route by remember { mutableStateOf(initRoute) }

                    var shouldShowMenuBanner by
                    remember { mutableStateOf(settings.shouldShowMenuBanner) }

                    val extensionsMenuItemDescription = getExtensionsMenuItemDescription(
                        isExtensionsProcessDisabled = isExtensionsProcessDisabled,
                        isAllWebExtensionsDisabled = isAllWebExtensionsDisabled,
                        availableAddons = availableAddons,
                        browserWebExtensionMenuItems = browserWebExtensionMenuItem,
                    )

                    val webExtensionMenuItems = remember(availableAddons, browserWebExtensionMenuItem) {
                        browserWebExtensionMenuItem.associateWith { menuItem ->
                            availableAddons.find { addon -> addon.id == menuItem.id }
                        }
                    }

                    BackHandler {
                        this@MenuDialogFragment.dismissAllowingStateLoss()
                    }

                    AnimatedContent(
                        targetState = contentState,
                        transitionSpec = {
                            if (contentState == Route.MainMenu) {
                                (
                                    enterMenu(
                                        duration = DURATION_MS_MAIN_MENU,
                                        delay = DELAY_MS_MAIN_MENU,
                                        easing = LinearOutSlowInEasing,
                                    )
                                    ).togetherWith(
                                    exitSubmenu(DURATION_MS_MAIN_MENU, FastOutLinearInEasing),
                                ) using SizeTransform { initialSize, targetSize ->
                                    contentGrowth(initialSize, targetSize, DURATION_MS_MAIN_MENU)
                                }
                            } else {
                                enterSubmenu(
                                    duration = DURATION_MS_SUB_MENU,
                                    delay = DELAY_MS_SUB_MENU,
                                    easing = LinearOutSlowInEasing,
                                ).togetherWith(
                                    exitMenu(
                                        duration = DURATION_MS_SUB_MENU,
                                        easing = FastOutLinearInEasing,
                                    ),
                                ) using SizeTransform { initialSize, targetSize ->
                                    contentGrowth(
                                        initialSize = initialSize,
                                        targetSize = targetSize,
                                        duration = DURATION_MS_SUB_MENU,
                                    )
                                }
                            }
                        },
                        label = "MenuDialogAnimation",
                    ) { route ->
                        when (route) {
                            Route.MainMenu -> {
                                handlebarContentDescription = descMain

                                val account by remember {
                                    syncStore.stateFlow
                                        .map { state -> state.account }
                                }.collectAsState(initial = null)
                                val accountState by remember {
                                    syncStore.stateFlow
                                        .map { state -> state.accountState }
                                }.collectAsState(initial = NotAuthenticated)
                                val isSiteLoading by remember {
                                    browserStore.stateFlow
                                        .map { state -> state.selectedTab?.content?.loading == true }
                                }.collectAsState(initial = false)

                                val appLinksRedirect = if (selectedTab?.content?.url != null) {
                                    appLinksUseCases.appLinkRedirect(selectedTab.content.url)
                                } else {
                                    null
                                }

                                val isDownloadHighlighted by remember {
                                    appStore.stateFlow
                                        .map { state ->
                                            state.supportedMenuNotifications.contains(
                                                SupportedMenuNotifications.Downloads,
                                            )
                                        }
                                }.collectAsState(initial = false)
                                val isOpenInAppMenuHighlighted by remember {
                                    appStore.stateFlow
                                        .map { state ->
                                            state.supportedMenuNotifications.contains(
                                                SupportedMenuNotifications.OpenInApp,
                                            )
                                        }
                                }.collectAsState(initial = false)

                                MainMenu(
                                    accessPoint = args.accesspoint,
                                    account = account,
                                    accountState = accountState,
                                    showQuitMenu = settings.shouldDeleteBrowsingDataOnQuit,
                                    isBottomToolbar = settings.shouldUseBottomToolbar,
                                    isExpandedToolbarEnabled = settings.shouldUseExpandedToolbar,
                                    isSiteLoading = isSiteLoading,
                                    isExtensionsProcessDisabled = isExtensionsProcessDisabled,
                                    isExtensionsExpanded = isExtensionsExpanded,
                                    isMoreMenuExpanded = isMoreMenuExpanded,
                                    isBookmarked = isBookmarked,
                                    isDesktopMode = isDesktopMode,
                                    isPdf = isPdf,
                                    isPrivate = isPrivate,
                                    isReaderViewActive = isReaderViewActive,
                                    isMoreMenuHighlighted = isOpenInAppMenuHighlighted,
                                    canGoBack = selectedTab?.content?.canGoBack ?: true,
                                    canGoForward = selectedTab?.content?.canGoForward ?: true,
                                    extensionsMenuItemDescription = extensionsMenuItemDescription,
                                    scrollState = scrollState,
                                    showBanner = shouldShowMenuBanner && !defaultBrowser,
                                    isDownloadHighlighted = isDownloadHighlighted,
                                    webExtensionMenuCount = webExtensionsCount,
                                    isAllWebExtensionsDisabled = isAllWebExtensionsDisabled,
                                    onMozillaAccountButtonClick = {
                                        store.dispatch(
                                            MenuAction.Navigate.MozillaAccount(
                                                accountState = accountState,
                                                accesspoint = args.accesspoint,
                                            ),
                                        )
                                    },
                                    onSettingsButtonClick = {
                                        store.dispatch(MenuAction.Navigate.Settings)
                                    },
                                    onBookmarkPageMenuClick = {
                                        store.dispatch(MenuAction.AddBookmark)
                                    },
                                    onEditBookmarkButtonClick = {
                                        store.dispatch(MenuAction.Navigate.EditBookmark)
                                    },
                                    onSwitchToDesktopSiteMenuClick = {
                                        if (isDesktopMode) {
                                            store.dispatch(MenuAction.RequestMobileSite)
                                        } else {
                                            store.dispatch(MenuAction.RequestDesktopSite)
                                        }
                                    },
                                    onFindInPageMenuClick = {
                                        store.dispatch(MenuAction.FindInPage)
                                    },
                                    onBannerClick = {
                                        store.dispatch(MenuAction.MenuBanner)
                                        (context as? Activity)?.openSetDefaultBrowserOption()
                                    },
                                    onBannerDismiss = {
                                        store.dispatch(MenuAction.DismissMenuBanner)
                                        shouldShowMenuBanner = false
                                    },
                                    onExtensionsMenuClick = {
                                        if (
                                            isAllWebExtensionsDisabled ||
                                            isExtensionsProcessDisabled ||
                                            extensionsMenuItemDescription == null
                                        ) {
                                            store.dispatch(MenuAction.Navigate.ManageExtensions)
                                        } else {
                                            isExtensionsExpanded = !isExtensionsExpanded
                                        }
                                    },
                                    onMoreMenuClick = {
                                        isMoreMenuExpanded = !isMoreMenuExpanded
                                    },
                                    onBookmarksMenuClick = {
                                        store.dispatch(MenuAction.Navigate.Bookmarks)
                                    },
                                    onHistoryMenuClick = {
                                        store.dispatch(MenuAction.Navigate.History)
                                    },
                                    onDownloadsMenuClick = {
                                        store.dispatch(MenuAction.Navigate.Downloads)
                                    },
                                    onPasswordsMenuClick = {
                                        store.dispatch(MenuAction.Navigate.Passwords)
                                    },
                                    onCustomizeReaderViewMenuClick = {
                                        store.dispatch(MenuAction.CustomizeReaderView)
                                    },
                                    onQuitMenuClick = {
                                        store.dispatch(MenuAction.DeleteBrowsingDataAndQuit)
                                    },
                                    onBackButtonClick = { viewHistory: Boolean ->
                                        store.dispatch(MenuAction.Navigate.Back(viewHistory))
                                    },
                                    onForwardButtonClick = { viewHistory: Boolean ->
                                        store.dispatch(MenuAction.Navigate.Forward(viewHistory))
                                    },
                                    onRefreshButtonClick = { bypassCache: Boolean ->
                                        store.dispatch(MenuAction.Navigate.Reload(bypassCache))
                                    },
                                    onStopButtonClick = {
                                        store.dispatch(MenuAction.Navigate.Stop)
                                    },
                                    onShareButtonClick = {
                                        selectedTab?.let {
                                            store.dispatch(MenuAction.Navigate.Share)
                                        }
                                    },
                                    moreSettingsSubmenu = {
                                        MoreSettingsSubmenu(
                                            isReaderViewActive = isReaderViewActive,
                                            isWebCompatEnabled = isWebCompatEnabled,
                                            isPinned = isPinned,
                                            isInstallable = webAppUseCases.isInstallable(),
                                            isAddToHomeScreenSupported = selectedTab != null &&
                                                    webAppUseCases.isPinningSupported(),
                                            hasExternalApp = appLinksRedirect?.hasExternalApp() ?: false,
                                            externalAppName = appLinksRedirect?.appName ?: "",
                                            isWebCompatReporterSupported = isWebCompatReporterSupported,
                                            isOpenInAppMenuHighlighted = isOpenInAppMenuHighlighted,
                                            translationInfo = translationInfo,
                                            showShortcuts = settings.showTopSitesFeature,
                                            isAndroidAutomotiveAvailable = context.isAndroidAutomotiveAvailable(),
                                            showSummarization = settings.shakeToSummarizeFeatureEnabled,
                                            onWebCompatReporterClick = {
                                                store.dispatch(MenuAction.Navigate.WebCompatReporter)
                                            },
                                            onSummarizePageClick = {
                                                store.dispatch(MenuAction.Navigate.Summarizer)
                                            },
                                            onShortcutsMenuClick = {
                                                if (!isPinned) {
                                                    store.dispatch(MenuAction.AddShortcut)
                                                } else {
                                                    store.dispatch(MenuAction.RemoveShortcut)
                                                }
                                            },
                                            onAddToHomeScreenMenuClick = {
                                                store.dispatch(MenuAction.Navigate.AddToHomeScreen)
                                            },
                                            onSaveToCollectionMenuClick = {
                                                store.dispatch(
                                                    MenuAction.Navigate.SaveToCollection(
                                                        hasCollection =
                                                            tabCollectionStorage.cachedTabCollections.isNotEmpty(),
                                                    ),
                                                )
                                            },
                                            onSaveAsPDFMenuClick = {
                                                saveToPdfUseCase()
                                                dismiss()
                                            },
                                            onPrintMenuClick = {
                                                printContentUseCase()
                                                dismiss()
                                            },
                                            onOpenInAppMenuClick = {
                                                store.dispatch(MenuAction.OpenInApp)
                                            },
                                        )
                                    },
                                    extensionSubmenu = {
                                        Addons(
                                            accessPoint = args.accesspoint,
                                            availableAddons = availableAddons,
                                            webExtensionMenuItems = webExtensionMenuItems,
                                            addonInstallationInProgress = addonInstallationInProgress,
                                            recommendedAddons = recommendedAddons,
                                            onAddonClick = { addon ->
                                                store.dispatch(
                                                    MenuAction.Navigate.AddonDetails(
                                                        addon = addon,
                                                    ),
                                                )
                                            },
                                            onAddonSettingsClick = { addon ->
                                                store.dispatch(
                                                    MenuAction.Navigate.InstalledAddonDetails(
                                                        addon = addon,
                                                    ),
                                                )
                                            },
                                            onInstallAddonClick = { addon ->
                                                store.dispatch(
                                                    MenuAction.InstallAddon(addon = addon),
                                                )
                                            },
                                            onManageExtensionsMenuClick = {
                                                store.dispatch(MenuAction.Navigate.ManageExtensions)
                                            },
                                            onDiscoverMoreExtensionsMenuClick = {
                                                store.dispatch(MenuAction.Navigate.DiscoverMoreExtensions)
                                            },
                                            onWebExtensionMenuItemClick = {
                                                Events.browserMenuAction.record(
                                                    Events.BrowserMenuActionExtra(
                                                        item = "web_extension_browser_action_clicked",
                                                    ),
                                                )
                                            },
                                        )
                                    },
                                )
                            }

                            Route.CustomTabMenu -> {
                                val isSiteLoading by remember {
                                    browserStore.stateFlow.map { state ->
                                        args.customTabSessionId
                                            ?.let {
                                                state.findCustomTab(it)?.content?.loading
                                            }
                                            ?: false
                                    }
                                }.collectAsState(false)
                                handlebarContentDescription = descCustom

                                CustomTabMenu(
                                    canGoBack = customTab?.content?.canGoBack ?: true,
                                    canGoForward = customTab?.content?.canGoForward ?: true,
                                    isBottomToolbar = settings.shouldUseBottomToolbar,
                                    isSiteLoading = isSiteLoading,
                                    scrollState = scrollState,
                                    isPdf = customTab?.content?.isPdf == true,
                                    isDesktopMode = isDesktopMode,
                                    isSandboxCustomTab = args.isSandboxCustomTab,
                                    isPrivate = isPrivate,
                                    isExtensionsExpanded = isExtensionsExpanded,
                                    isExtensionsProcessDisabled = isExtensionsProcessDisabled,
                                    isAllWebExtensionsDisabled = isAllWebExtensionsDisabled,
                                    shouldShowExtensionsMenu = settings.shouldShowCustomTabExtensions,
                                    webExtensionMenuCount = webExtensionsCount,
                                    extensionsMenuDescription = extensionsMenuItemDescription,
                                    customTabMenuItems = customTab?.config?.menuItems,
                                    onCustomMenuItemClick = { intent: PendingIntent ->
                                        store.dispatch(
                                            MenuAction.CustomMenuItemAction(
                                                intent = intent,
                                                url = customTab?.content?.url,
                                            ),
                                        )
                                    },
                                    onSwitchToDesktopSiteMenuClick = {
                                        if (isDesktopMode) {
                                            store.dispatch(MenuAction.RequestMobileSite)
                                        } else {
                                            store.dispatch(MenuAction.RequestDesktopSite)
                                        }
                                    },
                                    onFindInPageMenuClick = {
                                        store.dispatch(MenuAction.FindInPage)
                                    },
                                    onOpenInFirefoxMenuClick = {
                                        store.dispatch(MenuAction.OpenInFirefox)
                                    },
                                    onBackButtonClick = { viewHistory: Boolean ->
                                        store.dispatch(MenuAction.Navigate.Back(viewHistory))
                                    },
                                    onForwardButtonClick = { viewHistory: Boolean ->
                                        store.dispatch(MenuAction.Navigate.Forward(viewHistory))
                                    },
                                    onRefreshButtonClick = { bypassCache: Boolean ->
                                        store.dispatch(MenuAction.Navigate.Reload(bypassCache))
                                    },
                                    onStopButtonClick = {
                                        store.dispatch(MenuAction.Navigate.Stop)
                                    },
                                    onShareButtonClick = {
                                        store.dispatch(MenuAction.Navigate.Share)
                                    },
                                    onExtensionsMenuClick = {
                                        if (!isAllWebExtensionsDisabled && !isExtensionsProcessDisabled) {
                                            isExtensionsExpanded = !isExtensionsExpanded
                                        }
                                    },
                                    extensionSubmenu = {
                                        CustomTabAddons(
                                            webExtensionMenuItems = webExtensionMenuItems,
                                            onWebExtensionMenuItemClick = {
                                                Events.browserMenuAction.record(
                                                    Events.BrowserMenuActionExtra(
                                                        item = "web_extension_browser_action_clicked",
                                                    ),
                                                )
                                            },
                                        )
                                    },
                                )
                            }
                        }
                    }
                }
            }
        }
    }

    private fun getExtensionsMenuItemDescription(
        isExtensionsProcessDisabled: Boolean,
        isAllWebExtensionsDisabled: Boolean,
        availableAddons: List<Addon>,
        browserWebExtensionMenuItems: List<WebExtensionMenuItem>,
    ): String? {
        val isBrowserOrExternal = args.accesspoint == MenuAccessPoint.Browser ||
                args.accesspoint == MenuAccessPoint.External

        return when {
            args.accesspoint == MenuAccessPoint.Home -> null

            isExtensionsProcessDisabled -> {
                requireContext().getString(R.string.browser_menu_extensions_disabled_description)
            }

             isBrowserOrExternal && browserWebExtensionMenuItems.isNotEmpty() -> {
                browserWebExtensionMenuItems.joinToString(separator = ", ") { it.label }
            }

            isAllWebExtensionsDisabled -> {
                requireContext().getString(R.string.browser_menu_no_extensions_installed_description)
            }

             isBrowserOrExternal && availableAddons.isEmpty() -> {
                requireContext().getString(R.string.browser_menu_try_a_recommended_extension_description)
            }

            else -> null
        }
    }

    private fun openToBrowser(params: BrowserNavigationParams) = runIfFragmentIsAttached {
        val url = params.url ?: params.sumoTopic?.let {
            SupportUtils.getSumoURLForTopic(
                context = requireContext(),
                topic = it,
            )
        }

        url?.let {
            findNavController().openToBrowser()
            requireComponents.useCases.fenixBrowserUseCases.loadUrlOrSearch(
                searchTermOrURL = url,
                newTab = true,
            )
        }
    }

    private fun sendPendingIntentWithUrl(intent: PendingIntent, url: String?) = runIfFragmentIsAttached {
        url?.let { url ->
            intent.send(
                requireContext(),
                0,
                Intent(null, url.toUri()),
            )
        }
    }

    private fun calculateMenuSheetWidth(): Int {
        val isLandscape = requireContext().isLandscape()
        val screenWidthPx = requireContext().resources.configuration.screenWidthDp.dpToPx(resources.displayMetrics)
        val totalHorizontalPadding = 2 * pixelSizeFor(R.dimen.browser_menu_padding)
        val minScreenWidth = pixelSizeFor(R.dimen.browser_menu_max_width) + totalHorizontalPadding

        // We only want to restrict the width of the menu if the device is in landscape mode AND the
        // device's screen width is smaller than the menu's max width and total horizontal padding combined.
        // Otherwise, the menu being at max width would still leave sufficient padding on each side in landscape mode.
        return if (isLandscape && screenWidthPx < minScreenWidth) {
            screenWidthPx - totalHorizontalPadding
        } else {
            pixelSizeFor(R.dimen.browser_menu_max_width)
        }
    }

    private fun calculateMenuSheetHeight(): Int {
        val bottomSheet = dialog?.findViewById<View?>(materialR.id.design_bottom_sheet)
        val topBarHeight = bottomSheet?.getWindowInsets()?.top() ?: 0

        val orientationMaxHeight = if (requireContext().isLandscape()) {
            resources.displayMetrics.heightPixels
        } else {
            resources.displayMetrics.heightPixels - EXPANDED_OFFSET.dpToPx(resources.displayMetrics)
        }

        return orientationMaxHeight - topBarHeight
    }
}
