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
import android.view.KeyEvent
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.view.animation.OvershootInterpolator
import androidx.activity.compose.BackHandler
import androidx.annotation.VisibleForTesting
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.SizeTransform
import androidx.compose.animation.core.FastOutLinearInEasing
import androidx.compose.animation.core.LinearOutSlowInEasing
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CornerSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.SnackbarHostState
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.core.graphics.drawable.toDrawable
import androidx.core.net.toUri
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat.Type.systemBars
import androidx.fragment.compose.content
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import androidx.navigation.fragment.findNavController
import androidx.navigation.fragment.navArgs
import com.google.android.material.R as materialR
import com.google.android.material.bottomsheet.BottomSheetBehavior
import com.google.android.material.bottomsheet.BottomSheetDialog
import com.google.android.material.bottomsheet.BottomSheetDialogFragment
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch
import mozilla.components.browser.state.selector.findCustomTab
import mozilla.components.browser.state.selector.selectedTab
import mozilla.components.browser.state.state.SessionState
import mozilla.components.concept.engine.translate.TranslationSupport
import mozilla.components.concept.engine.translate.findLanguage
import mozilla.components.feature.addons.Addon
import mozilla.components.feature.automotive.isAndroidAutomotiveAvailable
import mozilla.components.feature.ipprotection.store.IPProtectionAction
import mozilla.components.feature.ipprotection.store.state.IPProtectionStatus
import mozilla.components.feature.ipprotection.store.state.isEligible
import mozilla.components.lib.state.helpers.StoreProvider.Companion.fragmentStore
import mozilla.components.service.fxa.manager.AccountState.NotAuthenticated
import mozilla.components.support.base.feature.ViewBoundFeatureWrapper
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.ktx.android.util.dpToPx
import mozilla.components.support.utils.ext.getWindowInsets
import mozilla.components.support.utils.ext.isLandscape
import mozilla.components.support.utils.ext.pixelSizeFor
import mozilla.components.support.utils.ext.top
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.fenix.GleanMetrics.Events
import org.mozilla.fenix.GleanMetrics.Vpn
import org.mozilla.fenix.R
import org.mozilla.fenix.addons.pendingExtensionUpdateNames
import org.mozilla.fenix.components.Components
import org.mozilla.fenix.components.appstate.SupportedMenuNotifications
import org.mozilla.fenix.components.components
import org.mozilla.fenix.components.menu.compose.Addons
import org.mozilla.fenix.components.menu.compose.CustomTabAddons
import org.mozilla.fenix.components.menu.compose.CustomTabMenu
import org.mozilla.fenix.components.menu.compose.MainMenu
import org.mozilla.fenix.components.menu.compose.MenuDialogBottomSheet
import org.mozilla.fenix.components.menu.compose.MenuHandleState
import org.mozilla.fenix.components.menu.compose.MoreSettingsSubmenu
import org.mozilla.fenix.components.menu.middleware.MenuDialogMiddleware
import org.mozilla.fenix.components.menu.middleware.MenuNavigationMiddleware
import org.mozilla.fenix.components.menu.middleware.MenuTelemetryMiddleware
import org.mozilla.fenix.components.menu.store.BrowserMenuState
import org.mozilla.fenix.components.menu.store.ExtensionMenuState
import org.mozilla.fenix.components.menu.store.IPProtectionMenuState
import org.mozilla.fenix.components.menu.store.MenuAction
import org.mozilla.fenix.components.menu.store.MenuState
import org.mozilla.fenix.components.menu.store.MenuStore
import org.mozilla.fenix.components.menu.store.NavigationEvent
import org.mozilla.fenix.components.menu.store.SummarizationMenuState
import org.mozilla.fenix.components.menu.store.TranslationInfo
import org.mozilla.fenix.components.menu.store.WebExtensionMenuItem
import org.mozilla.fenix.ext.canGoBackInHistoryOrToStories
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.openSetDefaultBrowserOption
import org.mozilla.fenix.ext.openToBrowser
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.ext.runIfFragmentIsAttached
import org.mozilla.fenix.ipprotection.ui.IPProtectionSnackbarBinding
import org.mozilla.fenix.nimbus.FxNimbus
import org.mozilla.fenix.settings.SupportUtils
import org.mozilla.fenix.settings.deletebrowsingdata.DefaultDeleteBrowsingDataController
import org.mozilla.fenix.settings.deletebrowsingdata.DefaultDeleteBrowsingDataController.DataStorage
import org.mozilla.fenix.settings.deletebrowsingdata.DefaultDeleteBrowsingDataController.DeleteDataUseCases
import org.mozilla.fenix.settings.deletebrowsingdata.DefaultDeleteBrowsingDataController.Stores
import org.mozilla.fenix.settings.deletebrowsingdata.DeleteBrowsingDataController
import org.mozilla.fenix.snackbar.FenixSnackbarDelegate
import org.mozilla.fenix.tabgroups.flow.TabGroupFlowEntryPoint
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.translations.TranslationsEnabledSettings
import org.mozilla.fenix.utils.DELAY_MS_MAIN_MENU
import org.mozilla.fenix.utils.DELAY_MS_SUB_MENU
import org.mozilla.fenix.utils.DURATION_MS_MAIN_MENU
import org.mozilla.fenix.utils.DURATION_MS_SUB_MENU
import org.mozilla.fenix.utils.contentGrowth
import org.mozilla.fenix.utils.enterMenu
import org.mozilla.fenix.utils.enterSubmenu
import org.mozilla.fenix.utils.exitMenu
import org.mozilla.fenix.utils.exitSubmenu
import org.mozilla.fenix.webcompat.DefaultWebCompatReporterMoreInfoSender
import org.mozilla.fenix.webcompat.middleware.DefaultWebCompatReporterRetrievalService

private const val EXPANDED_OFFSET = 56
private const val HIDING_FRICTION = 0.9f
private const val PRIVATE_HOME_MENU_BACKGROUND_ALPHA = 100

private object MenuAnimationConfig {
    const val DURATION = 300L
    const val START_OFFSET_RATIO = 0.2f
}

/** A bottom sheet fragment displaying the menu dialog. */
@Suppress("LargeClass")
class MenuDialogFragment : BottomSheetDialogFragment() {

    private val args by navArgs<MenuDialogFragmentArgs>()
    private val webExtensionsMenuBinding = ViewBoundFeatureWrapper<WebExtensionsMenuBinding>()
    private val ipProtectionMenuBinding = ViewBoundFeatureWrapper<IPProtectionMenuBinding>()
    private val ipProtectionSnackbarBinding = ViewBoundFeatureWrapper<IPProtectionSnackbarBinding>()
    private val snackbarHostState = SnackbarHostState()
    private var bottomSheetBehavior: BottomSheetBehavior<View>? = null
    private var isPrivate: Boolean = false
    private val browserStore by lazy { requireComponents.core.store }
    private lateinit var menuStore: MenuStore

    private val deleteBrowsingDataController: DeleteBrowsingDataController by lazy {
        DefaultDeleteBrowsingDataController(
            deleteDataUseCases =
                DeleteDataUseCases(
                    removeAllTabs = requireComponents.useCases.tabsUseCases.removeAllTabs,
                    removeAllDownloads = requireComponents.useCases.downloadUseCases.removeAllDownloads,
                ),
            dataStorage =
                DataStorage(
                    history = requireComponents.core.historyStorage,
                    permissions = requireComponents.core.permissionStorage,
                ),
            stores =
                Stores(
                    appStore = requireComponents.appStore,
                    browserStore = requireComponents.core.store,
                ),
            engine = requireComponents.core.engine,
            settings = requireComponents.settings,
        )
    }

    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog {
        Events.toolbarMenuVisible.record(NoExtras())

        return object : BottomSheetDialog(requireContext(), theme) {
                override fun onKeyDown(
                    keyCode: Int,
                    event: KeyEvent,
                ): Boolean {
                    if (event.action == KeyEvent.ACTION_DOWN && event.keyCode == KeyEvent.KEYCODE_MENU) {
                        // Repeats from a held down hardware key would otherwise keep dismissing
                        // and reopening the menu.
                        if (event.repeatCount == 0) {
                            dismiss()
                        }
                        return true
                    }
                    return super.onKeyDown(keyCode, event)
                }
            }
            .apply {
                setOnShowListener {
                    val safeActivity = activity ?: return@setOnShowListener
                    val appStore = safeActivity.components.appStore
                    isPrivate = appStore.state.mode.isPrivate

                    if (isPrivate && args.accesspoint == MenuAccessPoint.Home) {
                        window?.setBackgroundDrawable(
                            Color.BLACK.toDrawable().mutate().apply {
                                alpha = PRIVATE_HOME_MENU_BACKGROUND_ALPHA
                            }
                        )
                    }

                    val bottomSheet = findViewById<View?>(materialR.id.design_bottom_sheet)
                    bottomSheet?.let {
                        ViewCompat.setOnApplyWindowInsetsListener(it) { view, insets ->
                            val systemBarInsets = insets.getInsets(systemBars())
                            view.setPadding(0, systemBarInsets.top, 0, systemBarInsets.bottom)
                            insets
                        }
                    }
                    bottomSheet?.setBackgroundResource(R.drawable.bottom_sheet_with_top_rounded_corners)

                    // https://bugzilla.mozilla.org/show_bug.cgi?id=1982004
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.VANILLA_ICE_CREAM) {
                        bottomSheet?.let { sheet ->
                            sheet.translationY = sheet.height * MenuAnimationConfig.START_OFFSET_RATIO
                            sheet
                                .animate()
                                .translationY(0f)
                                .setInterpolator(OvershootInterpolator())
                                .setDuration(MenuAnimationConfig.DURATION)
                                .start()
                        }
                    }

                    bottomSheetBehavior = bottomSheet?.let {
                        BottomSheetBehavior.from(it).apply {
                            maxWidth = calculateMenuSheetWidth()
                            peekHeight = resources.displayMetrics.heightPixels
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
    ): View = content {
        FirefoxTheme {
            val context = LocalContext.current

            val components = components
            val settings = components.settings
            val defaultBrowser = settings.isDefaultBrowser
            val appStore = components.appStore

            val customTab =
                args.customTabSessionId?.let {
                    browserStore.state.findCustomTab(it)
                }
            val browserTab = browserStore.state.selectedTab
            val selectedTab: SessionState? = customTab ?: browserTab

            val appLinksUseCases = components.useCases.appLinksUseCases
            val webAppUseCases = components.useCases.webAppUseCases

            val scrollState = rememberScrollState()

            val descCustom = stringResource(R.string.browser_custom_tab_menu_handlebar_content_description)
            val descMain = stringResource(R.string.browser_close_main_menu_handlebar_content_description)

            var handlebarContentDescription by remember {
                mutableStateOf(if (args.accesspoint == MenuAccessPoint.External) descCustom else descMain)
            }

            var isExtensionsExpanded by remember { mutableStateOf(false) }

            val isMoreMenuExpanded by remember {
                menuStore.stateFlow.map { state -> state.isMoreMenuExpanded }
            }
                .collectAsState(initial = false)

            val isTranslationsEnabled =
                TranslationsEnabledSettings.dataStore(requireContext()).isEnabled.collectAsState(true)

            MenuDialogBottomSheet(
                modifier = Modifier.padding(top = 16.dp, bottom = 16.dp).width(32.dp),
                onRequestDismiss = ::dismiss,
                menuHandleState =
                    MenuHandleState(
                        contentDescription = handlebarContentDescription,
                        useDarkBackground =
                            !settings.shouldUseBottomToolbar &&
                                !settings.shouldUseExpandedToolbar &&
                                (isExtensionsExpanded || isMoreMenuExpanded) &&
                                args.accesspoint == MenuAccessPoint.Browser,
                    ),
                snackbarHostState = snackbarHostState,
                cornerShape =
                    MaterialTheme.shapes.extraLarge.copy(
                        bottomStart = CornerSize(0.dp),
                        bottomEnd = CornerSize(0.dp),
                    ),
            ) {
                val syncStore = components.backgroundServices.syncStore
                val tabCollectionStorage = components.core.tabCollectionStorage
                val printContentUseCase = components.useCases.sessionUseCases.printContent
                val saveToPdfUseCase = components.useCases.sessionUseCases.saveToPdf
                val isTranslationEngineSupported = browserStore.state.translationEngine.isEngineSupported ?: false
                val isTranslationSupported =
                    isTranslationEngineSupported &&
                        FxNimbus.features.translations.value().mainFlowBrowserMenuEnabled &&
                        isTranslationsEnabled.value
                val isPdf = selectedTab?.content?.isPdf ?: false
                val isWebCompatEnabled by remember {
                    menuStore.stateFlow.map { it.isWebCompatEnabled }
                }
                    .collectAsState(initial = menuStore.state.isWebCompatEnabled)
                val supportedLanguages = components.core.store.state.translationEngine.supportedLanguages
                val translateLanguageCode =
                    selectedTab?.translationsState?.translationEngineState?.requestedTranslationPair?.toLanguage
                val isExtensionsProcessDisabled = browserStore.state.extensionsProcessDisabled
                val pendingExtensionUpdateNames = browserStore.state.pendingExtensionUpdateNames()
                val isDesktopMode by remember {
                    menuStore.stateFlow.map { state -> state.isDesktopMode }
                }
                    .collectAsState(initial = false)

                val recommendedAddons by remember {
                    menuStore.stateFlow.map { state ->
                        state.extensionMenuState.recommendedAddons
                    }
                }
                    .collectAsState(initial = emptyList())

                val isBookmarked by remember {
                    menuStore.stateFlow.map { state ->
                        state.browserMenuState != null && state.browserMenuState.bookmarkState.isBookmarked
                    }
                }
                    .collectAsState(initial = false)

                val isPinned by remember {
                    menuStore.stateFlow.map { state ->
                        state.browserMenuState != null && state.browserMenuState.isPinned
                    }
                }
                    .collectAsState(initial = false)

                val isReaderViewActive by remember {
                    menuStore.stateFlow.map { state ->
                        state.isReaderModeActive
                    }
                }
                    .collectAsState(initial = false)

                val addonInstallationInProgress by remember {
                    menuStore.stateFlow.map { state -> state.extensionMenuState.addonInstallationInProgress }
                }
                    .collectAsState(initial = null)

                val browserWebExtensionMenuItem by remember {
                    menuStore.stateFlow.map { state -> state.extensionMenuState.browserWebExtensionMenuItem }
                }
                    .collectAsState(initial = emptyList())

                val availableAddons by remember {
                    menuStore.stateFlow.map { state -> state.extensionMenuState.availableAddons }
                }
                    .collectAsState(initial = emptyList())

                val webExtensionsCount by remember {
                    menuStore.stateFlow.map { state -> state.extensionMenuState.webExtensionsCount }
                }
                    .collectAsState(initial = 0)

                val isAllWebExtensionsDisabled by remember {
                    menuStore.stateFlow.map { state -> state.extensionMenuState.allWebExtensionsDisabled }
                }
                    .collectAsState(initial = false)

                val initRoute =
                    when (args.accesspoint) {
                        MenuAccessPoint.Browser,
                        MenuAccessPoint.Home -> Route.MainMenu

                        MenuAccessPoint.External -> Route.CustomTabMenu
                    }

                val translationInfo =
                    TranslationInfo(
                        isTranslationSupported = isTranslationSupported,
                        isPdf = isPdf,
                        isTranslated = selectedTab?.translationsState?.isTranslated ?: false,
                        translatedLanguage =
                            if (translateLanguageCode != null && supportedLanguages != null) {
                                TranslationSupport(
                                        fromLanguages = supportedLanguages.fromLanguages,
                                        toLanguages = supportedLanguages.toLanguages,
                                    )
                                    .findLanguage(translateLanguageCode)
                                    ?.localizedDisplayName ?: ""
                            } else {
                                ""
                            },
                        onTranslatePageMenuClick = {
                            selectedTab?.let {
                                menuStore.dispatch(MenuAction.Navigate.Translate)
                            }
                        },
                    )

                val summarizationMenuState by remember {
                    menuStore.stateFlow.map { state -> state.summarizationMenuState }
                }
                    .collectAsState(initial = SummarizationMenuState.Default)

                val ipProtectionMenuState by remember {
                    menuStore.stateFlow.map { state -> state.ipProtectionMenuState }
                }
                    .collectAsState(initial = menuStore.state.ipProtectionMenuState)

                val contentState: Route by remember { mutableStateOf(initRoute) }

                var shouldShowMenuBanner by remember { mutableStateOf(settings.shouldShowMenuBanner) }

                val extensionsMenuItemDescription =
                    getExtensionsMenuItemDescription(
                        isExtensionsProcessDisabled = isExtensionsProcessDisabled,
                        isAllWebExtensionsDisabled = isAllWebExtensionsDisabled,
                        availableAddons = availableAddons,
                        browserWebExtensionMenuItems = browserWebExtensionMenuItem,
                    )
                val pendingExtensionUpdatesDescription =
                    getPendingExtensionUpdatesDescription(
                        accessPoint = args.accesspoint,
                        isExtensionsProcessDisabled = isExtensionsProcessDisabled,
                        pendingExtensionUpdateNames = pendingExtensionUpdateNames.values.toList(),
                    )

                val webExtensionMenuItems =
                    remember(availableAddons, browserWebExtensionMenuItem) {
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
                            (enterMenu(
                                    duration = DURATION_MS_MAIN_MENU,
                                    delay = DELAY_MS_MAIN_MENU,
                                    easing = LinearOutSlowInEasing,
                                ))
                                .togetherWith(exitSubmenu(DURATION_MS_MAIN_MENU, FastOutLinearInEasing)) using
                                SizeTransform { initialSize, targetSize ->
                                    contentGrowth(initialSize, targetSize, DURATION_MS_MAIN_MENU)
                                }
                        } else {
                            enterSubmenu(
                                    duration = DURATION_MS_SUB_MENU,
                                    delay = DELAY_MS_SUB_MENU,
                                    easing = LinearOutSlowInEasing,
                                )
                                .togetherWith(
                                    exitMenu(
                                        duration = DURATION_MS_SUB_MENU,
                                        easing = FastOutLinearInEasing,
                                    )
                                ) using
                                SizeTransform { initialSize, targetSize ->
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
                                syncStore.stateFlow.map { state -> state.account }
                            }
                                .collectAsState(initial = null)
                            val accountState by remember {
                                syncStore.stateFlow.map { state -> state.accountState }
                            }
                                .collectAsState(initial = NotAuthenticated)
                            val isSiteLoading by remember {
                                browserStore.stateFlow.map { state ->
                                    state.selectedTab?.content?.loading == true
                                }
                            }
                                .collectAsState(initial = false)

                            val appLinksRedirect =
                                if (selectedTab?.content?.url != null) {
                                    appLinksUseCases.appLinkRedirect(selectedTab.content.url)
                                } else {
                                    null
                                }

                            val isDownloadHighlighted by remember {
                                appStore.stateFlow.map { state ->
                                    state.supportedMenuNotifications.contains(SupportedMenuNotifications.Downloads)
                                }
                            }
                                .collectAsState(initial = false)
                            val isOpenInAppMenuHighlighted by remember {
                                appStore.stateFlow.map { state ->
                                    state.supportedMenuNotifications.contains(SupportedMenuNotifications.OpenInApp)
                                }
                            }
                                .collectAsState(initial = false)

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
                                isReaderViewActive = isReaderViewActive,
                                isMoreMenuHighlighted =
                                    isOpenInAppMenuHighlighted || summarizationMenuState.overflowMenuHighlighted,
                                canGoBack = browserStore.state.canGoBackInHistoryOrToStories(),
                                canGoForward = selectedTab?.content?.canGoForward ?: true,
                                extensionsMenuItemDescription = extensionsMenuItemDescription,
                                pendingExtensionUpdatesDescription = pendingExtensionUpdatesDescription,
                                scrollState = scrollState,
                                showBanner = shouldShowMenuBanner && !defaultBrowser,
                                isDownloadHighlighted = isDownloadHighlighted,
                                webExtensionMenuCount = webExtensionsCount,
                                isAllWebExtensionsDisabled = isAllWebExtensionsDisabled,
                                showIPProtection = components.ipProtection.store.state.isEligible,
                                showPasswords = settings.isAutofillSupported,
                                ipProtectionMenuState = ipProtectionMenuState,
                                onMozillaAccountButtonClick = {
                                    menuStore.dispatch(
                                        MenuAction.Navigate.MozillaAccount(
                                            accountState = accountState,
                                            accesspoint = args.accesspoint,
                                        )
                                    )
                                },
                                onSettingsButtonClick = {
                                    menuStore.dispatch(MenuAction.Navigate.Settings)
                                },
                                onCustomizeHomepageButtonClick = {
                                    menuStore.dispatch(MenuAction.Navigate.CustomizeHomepage)
                                },
                                onBookmarkPageMenuClick = {
                                    menuStore.dispatch(MenuAction.AddBookmark)
                                },
                                onEditBookmarkButtonClick = {
                                    menuStore.dispatch(MenuAction.Navigate.EditBookmark())
                                },
                                onSwitchToDesktopSiteMenuClick = {
                                    if (isDesktopMode) {
                                        menuStore.dispatch(MenuAction.RequestMobileSite)
                                    } else {
                                        menuStore.dispatch(MenuAction.RequestDesktopSite)
                                    }
                                },
                                onFindInPageMenuClick = {
                                    menuStore.dispatch(MenuAction.FindInPage)
                                },
                                onBannerClick = {
                                    menuStore.dispatch(MenuAction.DefaultBrowserMenuBannerClicked)
                                    (context as? Activity)?.openSetDefaultBrowserOption()
                                },
                                onBannerDismiss = {
                                    menuStore.dispatch(MenuAction.DefaultBrowserMenuBannerDismissed)
                                    shouldShowMenuBanner = false
                                },
                                onExtensionsMenuClick = {
                                    if (
                                        isAllWebExtensionsDisabled ||
                                            isExtensionsProcessDisabled ||
                                            extensionsMenuItemDescription == null
                                    ) {
                                        menuStore.dispatch(MenuAction.Navigate.ManageExtensions)
                                    } else {
                                        isExtensionsExpanded = !isExtensionsExpanded
                                    }
                                },
                                onMoreMenuClick = {
                                    menuStore.dispatch(MenuAction.OnMoreMenuClicked)
                                },
                                onBookmarksMenuClick = {
                                    menuStore.dispatch(MenuAction.Navigate.Bookmarks)
                                },
                                onHistoryMenuClick = {
                                    menuStore.dispatch(MenuAction.Navigate.History)
                                },
                                onDownloadsMenuClick = {
                                    menuStore.dispatch(MenuAction.Navigate.Downloads)
                                },
                                onPasswordsMenuClick = {
                                    menuStore.dispatch(MenuAction.Navigate.Passwords)
                                },
                                onCustomizeReaderViewMenuClick = {
                                    menuStore.dispatch(MenuAction.CustomizeReaderView)
                                },
                                onQuitMenuClick = {
                                    menuStore.dispatch(MenuAction.DeleteBrowsingDataAndQuit)
                                },
                                onBackButtonClick = { viewHistory: Boolean ->
                                    menuStore.dispatch(MenuAction.Navigate.Back(viewHistory))
                                },
                                onForwardButtonClick = { viewHistory: Boolean ->
                                    menuStore.dispatch(MenuAction.Navigate.Forward(viewHistory))
                                },
                                onRefreshButtonClick = { bypassCache: Boolean ->
                                    menuStore.dispatch(MenuAction.Navigate.Reload(bypassCache))
                                },
                                onStopButtonClick = {
                                    menuStore.dispatch(MenuAction.Navigate.Stop)
                                },
                                onShareButtonClick = {
                                    selectedTab?.let {
                                        menuStore.dispatch(MenuAction.Navigate.Share)
                                    }
                                },
                                onIPProtectionClick = {
                                    handleIPProtectionClick(ipProtectionMenuState, components, menuStore)
                                },
                                onIPProtectionNavigate = {
                                    Vpn.settingsPageTapped.record(Vpn.SettingsPageTappedExtra(entrypoint = "Menu"))
                                    menuStore.dispatch(MenuAction.Navigate.IPProtectionSettings)
                                },
                                moreSettingsSubmenu = {
                                    MoreSettingsSubmenu(
                                        isReaderViewActive = isReaderViewActive,
                                        isWebCompatEnabled = isWebCompatEnabled,
                                        isPinned = isPinned,
                                        isPrivate = isPrivate,
                                        isInstallable = webAppUseCases.isInstallable(),
                                        isAddToHomeScreenSupported =
                                            selectedTab != null && webAppUseCases.isPinningSupported(),
                                        hasExternalApp = appLinksRedirect?.hasExternalApp() ?: false,
                                        externalAppName = appLinksRedirect?.appName ?: "",
                                        isOpenInAppMenuHighlighted = isOpenInAppMenuHighlighted,
                                        translationInfo = translationInfo,
                                        showShortcuts = settings.showTopSitesFeature,
                                        showSaveToCollection = settings.collections,
                                        isAndroidAutomotiveAvailable = context.isAndroidAutomotiveAvailable(),
                                        summarizationMenuState = summarizationMenuState,
                                        onWebCompatReporterClick = {
                                            menuStore.dispatch(MenuAction.Navigate.WebCompatReporter)
                                        },
                                        onSummarizePageMenuExposed = {
                                            menuStore.dispatch(MenuAction.OnSummarizationMenuExposed)
                                        },
                                        onSummarizePageClick = {
                                            menuStore.dispatch(MenuAction.Navigate.Summarizer)
                                        },
                                        onShortcutsMenuClick = {
                                            if (!isPinned) {
                                                menuStore.dispatch(MenuAction.AddShortcut)
                                            } else {
                                                menuStore.dispatch(MenuAction.RemoveShortcut)
                                            }
                                        },
                                        onAddToHomeScreenMenuClick = {
                                            menuStore.dispatch(MenuAction.Navigate.AddToHomeScreen)
                                        },
                                        onSaveToCollectionMenuClick = {
                                            menuStore.dispatch(
                                                MenuAction.Navigate.SaveToCollection(
                                                    hasCollection =
                                                        tabCollectionStorage.cachedTabCollections.isNotEmpty()
                                                )
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
                                            menuStore.dispatch(MenuAction.OpenInApp)
                                        },
                                        onMoveToNonPrivateTabMenuClick = {
                                            menuStore.dispatch(MenuAction.MoveToNonPrivateTab)
                                        },
                                        showTabGroupsInMenu = settings.showTabGroupsInMenu,
                                        onAddToTabGroupClick = {
                                            if (selectedTab != null) {
                                                menuStore.dispatch(
                                                    MenuAction.Navigate.OpenTabGroupFlow(
                                                        entryPoint =
                                                            TabGroupFlowEntryPoint.AddTabToGroup(tabId = selectedTab.id)
                                                    )
                                                )
                                            } else {
                                                Logger.warn("No active selected tab - cannot add tab to groups.")
                                            }
                                        },
                                    )
                                },
                                extensionSubmenu = {
                                    Addons(
                                        accessPoint = args.accesspoint,
                                        pendingExtensionUpdateIds = pendingExtensionUpdateNames.keys,
                                        availableAddons = availableAddons,
                                        webExtensionMenuItems = webExtensionMenuItems,
                                        addonInstallationInProgress = addonInstallationInProgress,
                                        recommendedAddons = recommendedAddons,
                                        onAddonClick = { addon ->
                                            menuStore.dispatch(MenuAction.Navigate.AddonDetails(addon = addon))
                                        },
                                        onAddonSettingsClick = { addon ->
                                            menuStore.dispatch(MenuAction.Navigate.InstalledAddonDetails(addon = addon))
                                        },
                                        onInstallAddonClick = { addon ->
                                            menuStore.dispatch(MenuAction.InstallAddon(addon = addon))
                                        },
                                        onManageExtensionsMenuClick = {
                                            menuStore.dispatch(MenuAction.Navigate.ManageExtensions)
                                        },
                                        onDiscoverMoreExtensionsMenuClick = {
                                            menuStore.dispatch(MenuAction.Navigate.DiscoverMoreExtensions)
                                        },
                                        onWebExtensionMenuItemClick = {
                                            Events.browserMenuAction.record(
                                                Events.BrowserMenuActionExtra(
                                                    item = "web_extension_browser_action_clicked"
                                                )
                                            )
                                        },
                                    )
                                },
                            )
                        }

                        Route.CustomTabMenu -> {
                            val isSiteLoading by remember {
                                browserStore.stateFlow.map { state ->
                                    args.customTabSessionId?.let {
                                        state.findCustomTab(it)?.content?.loading
                                    } ?: false
                                }
                            }
                                .collectAsState(false)
                            handlebarContentDescription = descCustom

                            CustomTabMenu(
                                canGoBack = customTab?.content?.canGoBack ?: true,
                                canGoForward = customTab?.content?.canGoForward ?: true,
                                isBottomToolbar = settings.shouldUseBottomToolbar,
                                isSiteLoading = isSiteLoading,
                                scrollState = scrollState,
                                isBookmarked = isBookmarked,
                                isPdf = customTab?.content?.isPdf == true,
                                isDesktopMode = isDesktopMode,
                                isSandboxCustomTab = args.isSandboxCustomTab,
                                isExtensionsExpanded = isExtensionsExpanded,
                                isExtensionsProcessDisabled = isExtensionsProcessDisabled,
                                isAllWebExtensionsDisabled = isAllWebExtensionsDisabled,
                                shouldShowExtensionsMenu = settings.shouldShowCustomTabExtensions,
                                webExtensionMenuCount = webExtensionsCount,
                                extensionsMenuDescription = extensionsMenuItemDescription,
                                customTabMenuItems = customTab?.config?.menuItems,
                                onCustomMenuItemClick = { intent: PendingIntent ->
                                    menuStore.dispatch(
                                        MenuAction.CustomMenuItemAction(
                                            intent = intent,
                                            url = customTab?.content?.url,
                                        )
                                    )
                                },
                                onSwitchToDesktopSiteMenuClick = {
                                    if (isDesktopMode) {
                                        menuStore.dispatch(MenuAction.RequestMobileSite)
                                    } else {
                                        menuStore.dispatch(MenuAction.RequestDesktopSite)
                                    }
                                },
                                onBookmarkPageMenuClick = {
                                    menuStore.dispatch(MenuAction.AddBookmark)
                                },
                                onEditBookmarkMenuClick = {
                                    menuStore.dispatch(MenuAction.Navigate.EditBookmark())
                                },
                                onFindInPageMenuClick = {
                                    menuStore.dispatch(MenuAction.FindInPage)
                                },
                                onOpenInFirefoxMenuClick = {
                                    menuStore.dispatch(MenuAction.OpenInFirefox)
                                },
                                onBackButtonClick = { viewHistory: Boolean ->
                                    menuStore.dispatch(MenuAction.Navigate.Back(viewHistory))
                                },
                                onForwardButtonClick = { viewHistory: Boolean ->
                                    menuStore.dispatch(MenuAction.Navigate.Forward(viewHistory))
                                },
                                onRefreshButtonClick = { bypassCache: Boolean ->
                                    menuStore.dispatch(MenuAction.Navigate.Reload(bypassCache))
                                },
                                onStopButtonClick = {
                                    menuStore.dispatch(MenuAction.Navigate.Stop)
                                },
                                onShareButtonClick = {
                                    menuStore.dispatch(MenuAction.Navigate.Share)
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
                                                    item = "web_extension_browser_action_clicked"
                                                )
                                            )
                                        },
                                    )
                                },
                                translationInfo = translationInfo,
                                isReaderViewActive = isReaderViewActive,
                                summarizationMenuState = summarizationMenuState,
                                onSummarizePageMenuExposed = {
                                    menuStore.dispatch(MenuAction.OnSummarizationMenuExposed)
                                },
                                onSummarizePageClick = {
                                    menuStore.dispatch(MenuAction.Navigate.Summarizer)
                                },
                            )
                        }
                    }
                }
            }
        }
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        menuStore =
            fragmentStore(initialState = createInitialMenuState()) {
                    MenuStore(
                        initialState = it,
                        middleware =
                            listOf(
                                createMenuDialogMiddleware(),
                                createMenuNavigationMiddleware(),
                                createMenuTelemetryMiddleware(),
                            ),
                    )
                }
                .value

        webExtensionsMenuBinding.set(
            feature =
                WebExtensionsMenuBinding(
                    browserStore = browserStore,
                    customTabId = args.customTabSessionId,
                    menuStore = menuStore,
                    iconSize = 24.dpToPx(requireContext().resources.displayMetrics),
                    onDismiss = { this@MenuDialogFragment.dismiss() },
                ),
            owner = this@MenuDialogFragment,
            view = view,
        )

        ipProtectionMenuBinding.set(
            feature =
                IPProtectionMenuBinding(
                    ipProtectionStore = requireComponents.ipProtection.store,
                    onIPProtectionStatusUpdate = {
                        menuStore.dispatch(MenuAction.UpdateIPProtectionMenuState(it))
                    },
                ),
            owner = this@MenuDialogFragment,
            view = view,
        )

        ipProtectionSnackbarBinding.set(
            feature =
                IPProtectionSnackbarBinding(
                    appStore = requireComponents.appStore,
                    context = requireContext(),
                    navController = findNavController(),
                    snackbarDelegate =
                        FenixSnackbarDelegate(
                            snackbarHostState = snackbarHostState,
                            scope = viewLifecycleOwner.lifecycleScope,
                            context = requireContext(),
                        ),
                ),
            owner = this,
            view = view,
        )

        viewLifecycleOwner.lifecycleScope.launch {
            viewLifecycleOwner.repeatOnLifecycle(Lifecycle.State.STARTED) {
                menuStore.navigationEvents.collect(::handleNavigationEvent)
            }
        }
    }

    private fun createInitialMenuState(): MenuState {
        val customTab =
            args.customTabSessionId?.let {
                browserStore.state.findCustomTab(it)
            }
        val selectedTab = customTab ?: browserStore.state.selectedTab

        return MenuState(
            browserMenuState =
                if (selectedTab != null) {
                    BrowserMenuState(selectedTab = selectedTab)
                } else {
                    null
                },
            isDesktopMode =
                when (args.accesspoint) {
                    MenuAccessPoint.Home -> {
                        false // this is not supported on Home
                    }
                    MenuAccessPoint.External -> {
                        selectedTab?.content?.desktopMode ?: false
                    }
                    else -> {
                        selectedTab?.content?.desktopMode ?: false
                    }
                },
            extensionMenuState = ExtensionMenuState(accesspoint = args.accesspoint),
        )
    }

    private fun createMenuDialogMiddleware(): MenuDialogMiddleware {
        val components = requireComponents
        val appStore = components.appStore
        val settings = components.settings
        val appLinksUseCases = components.useCases.appLinksUseCases

        return MenuDialogMiddleware(
            appStore = appStore,
            addonManager = components.addonManager,
            settings = settings,
            summarizeMenuSettings = components.core.summarizeFeatureSettings,
            summarizationEligibilityChecker = components.core.summarizationEligibilityChecker,
            bookmarksStorage = components.core.bookmarksStorage,
            pinnedSiteStorage = components.core.pinnedSiteStorage,
            appLinksUseCases = appLinksUseCases,
            addBookmarkUseCase = components.useCases.bookmarksUseCases.addBookmark,
            addPinnedSiteUseCase = components.useCases.topSitesUseCase.addPinnedSites,
            removePinnedSitesUseCase = components.useCases.topSitesUseCase.removeTopSites,
            requestDesktopSiteUseCase = components.useCases.sessionUseCases.requestDesktopSite,
            migratePrivateTabUseCase = components.useCases.tabsUseCases.migratePrivateTabUseCase,
            materialAlertDialogBuilder = MaterialAlertDialogBuilder(requireContext()),
            topSitesMaxLimit = components.settings.topSitesMaxLimit,
            mainDispatcher = Dispatchers.Main,
        )
    }

    private fun handleNavigationEvent(event: NavigationEvent) {
        when (event) {
            is NavigationEvent.Dismiss -> dismiss()
            is NavigationEvent.DeleteBrowsingDataAndQuit ->
                activity?.let { activity ->
                    activity.lifecycleScope.launch {
                        deleteBrowsingDataController.clearBrowsingDataOnQuit {
                            activity.finishAndRemoveTask()
                        }
                    }
                }
            is NavigationEvent.SendPendingIntentWithUrl -> {
                sendPendingIntentWithUrl(event.intent, event.url)
                dismiss()
            }
            is NavigationEvent.OpenToBrowser -> openToBrowser(event.params)
        }
    }

    private fun createMenuNavigationMiddleware(): MenuNavigationMiddleware {
        val components = requireComponents
        val webAppUseCases = components.useCases.webAppUseCases
        val settings = components.settings

        val webCompatReporterMoreInfoSender =
            DefaultWebCompatReporterMoreInfoSender(
                webCompatReporterRetrievalService =
                    DefaultWebCompatReporterRetrievalService(browserStore = browserStore)
            )

        val coroutineScope = viewLifecycleOwner.lifecycleScope

        return MenuNavigationMiddleware(
            browserStore = browserStore,
            navController = findNavController(),
            sessionUseCases = components.useCases.sessionUseCases,
            webAppUseCases = webAppUseCases,
            shareUseCases = components.useCases.shareUseCases,
            settings = settings,
            scope = coroutineScope,
            webCompatReporterMoreInfoSender = webCompatReporterMoreInfoSender,
        )
    }

    private fun createMenuTelemetryMiddleware(): MenuTelemetryMiddleware<MenuState, MenuAction> {
        return MenuTelemetryMiddleware(accessPoint = args.accesspoint)
    }

    private fun handleIPProtectionClick(
        ipProtectionMenuState: IPProtectionMenuState,
        components: Components,
        store: MenuStore,
    ) {
        when (ipProtectionMenuState.status) {
            IPProtectionStatus.Disabled -> {
                Vpn.menuTurnedOn.record()
                components.ipProtection.store.dispatch(IPProtectionAction.ActivationAction.Toggle)
            }

            IPProtectionStatus.Enabled -> {
                Vpn.menuTurnedOff.record()
                components.ipProtection.store.dispatch(IPProtectionAction.ActivationAction.Toggle)
            }

            IPProtectionStatus.AuthRequired -> {
                // If authorization is required, the user clicked the "Try it" button.
                Vpn.menuTryItTapped.record(NoExtras())
                store.dispatch(MenuAction.Navigate.IPProtectionSettings)
            }

            IPProtectionStatus.AwaitingService -> store.dispatch(MenuAction.Navigate.IPProtectionSettings)

            IPProtectionStatus.Activating,
            IPProtectionStatus.DataLimitReached,
            IPProtectionStatus.ConnectionError -> {
                components.ipProtection.store.dispatch(IPProtectionAction.ActivationAction.Toggle)
            }

            IPProtectionStatus.Unavailable,
            IPProtectionStatus.Uninitialized -> Unit
        }
    }

    private fun getExtensionsMenuItemDescription(
        isExtensionsProcessDisabled: Boolean,
        isAllWebExtensionsDisabled: Boolean,
        availableAddons: List<Addon>,
        browserWebExtensionMenuItems: List<WebExtensionMenuItem>,
    ): String? {
        val isBrowserOrExternal =
            args.accesspoint == MenuAccessPoint.Browser || args.accesspoint == MenuAccessPoint.External

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

    @VisibleForTesting
    internal fun getPendingExtensionUpdatesDescription(
        accessPoint: MenuAccessPoint,
        isExtensionsProcessDisabled: Boolean,
        pendingExtensionUpdateNames: List<String>,
    ): String? {
        // Only a disabled extensions process outranks a pending update. Custom tabs can't act on it.
        if (
            pendingExtensionUpdateNames.isEmpty() ||
                isExtensionsProcessDisabled ||
                accessPoint == MenuAccessPoint.External
        ) {
            return null
        }
        return requireContext()
            .getString(
                R.string.browser_menu_extensions_review_update_description,
                pendingExtensionUpdateNames.joinToString(),
            )
    }

    private fun openToBrowser(params: BrowserNavigationParams) = runIfFragmentIsAttached {
        val url =
            params.url
                ?: params.sumoTopic?.let {
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

        val orientationMaxHeight =
            if (requireContext().isLandscape()) {
                resources.displayMetrics.heightPixels
            } else {
                resources.displayMetrics.heightPixels - EXPANDED_OFFSET.dpToPx(resources.displayMetrics)
            }

        return orientationMaxHeight - topBarHeight
    }
}
