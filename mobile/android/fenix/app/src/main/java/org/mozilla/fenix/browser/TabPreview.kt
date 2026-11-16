/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.browser

import android.content.Context
import android.util.AttributeSet
import android.view.Gravity
import android.view.LayoutInflater
import android.view.View
import androidx.compose.foundation.layout.Column
import androidx.compose.ui.platform.ComposeView
import androidx.compose.ui.platform.ViewCompositionStrategy
import androidx.coordinatorlayout.widget.CoordinatorLayout
import androidx.core.view.doOnNextLayout
import androidx.core.view.isVisible
import androidx.core.view.updateLayoutParams
import kotlin.math.min
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import mozilla.components.browser.state.search.SearchEngine
import mozilla.components.browser.state.selector.getNormalOrPrivateTabs
import mozilla.components.browser.state.selector.selectedTab
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.state.selectedOrDefaultSearchEngine
import mozilla.components.browser.thumbnails.loader.ThumbnailLoader
import mozilla.components.browser.toolbar.R as toolbarR
import mozilla.components.compose.browser.toolbar.BrowserToolbar
import mozilla.components.compose.browser.toolbar.NavigationBar
import mozilla.components.compose.browser.toolbar.concept.Action
import mozilla.components.compose.browser.toolbar.concept.Action.ActionButton
import mozilla.components.compose.browser.toolbar.concept.Action.ActionButtonRes
import mozilla.components.compose.browser.toolbar.concept.Action.TabCounterAction
import mozilla.components.compose.browser.toolbar.concept.PageOrigin
import mozilla.components.compose.browser.toolbar.store.BrowserDisplayToolbarAction
import mozilla.components.compose.browser.toolbar.store.BrowserDisplayToolbarAction.PageOriginUpdated
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarAction
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarEvent
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarStore
import mozilla.components.compose.browser.toolbar.store.ToolbarGravity
import mozilla.components.concept.base.images.ImageLoadRequest
import mozilla.components.concept.engine.utils.ABOUT_HOME_URL
import mozilla.components.feature.summarize.R as summariesR
import mozilla.components.support.ktx.android.view.toScope
import mozilla.components.support.ktx.kotlin.applyRegistrableDomainSpan
import mozilla.components.support.ktx.kotlin.isContentUrl
import mozilla.components.support.ktx.util.URLStringUtils
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.browsingmode.BrowsingMode.Normal
import org.mozilla.fenix.browser.browsingmode.BrowsingMode.Private
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration
import org.mozilla.fenix.components.toolbar.ToolbarPosition
import org.mozilla.fenix.databinding.TabPreviewBinding
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.isTallWindow
import org.mozilla.fenix.ext.isWideWindow
import org.mozilla.fenix.home.toolbar.BrowserSimpleToolbar
import org.mozilla.fenix.nimbus.FxNimbus
import org.mozilla.fenix.search.BrowserToolbarSearchMiddleware
import org.mozilla.fenix.settings.ShortcutType
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.translations.TranslationsEnabledSettings

/**
 * A 'dummy' view of a tab used by [ToolbarGestureHandler] to support switching tabs by swiping the address bar.
 *
 * The view is responsible for showing the preview and a dummy toolbar of the inactive tab during swiping.
 */
class TabPreview
@JvmOverloads
constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyle: Int = 0,
) : CoordinatorLayout(context, attrs, defStyle) {
    private val binding = TabPreviewBinding.inflate(LayoutInflater.from(context), this)
    private val thumbnailLoader = ThumbnailLoader(context.components.core.thumbnailStorage)
    private val appStore = context.components.appStore
    private val browserStore = context.components.core.store
    private val summarizationFeatureSettings = context.components.core.summarizeFeatureSettings
    private val translationsFeatureSettings = TranslationsEnabledSettings.dataStore(context)

    private lateinit var mockToolbarView: View
    private val browserToolbarStore: BrowserToolbarStore by
        lazy(LazyThreadSafetyMode.NONE) {
            BrowserToolbarStore()
        }

    private enum class ToolbarAction {
        NewTab,
        Back,
        Forward,
        RefreshOrStop,
        Menu,
        TabCounter,
        SiteInfo,
        Bookmark,
        EditBookmark,
        Share,
        Translate,
        Homepage,
        Summarize,
    }

    private data class ToolbarActionConfig(
        val action: ToolbarAction,
        val isVisible: () -> Boolean = { true },
    )

    @Suppress("LongMethod", "CyclomaticComplexMethod", "CognitiveComplexMethod")
    private fun buildAction(
        toolbarAction: ToolbarAction,
        tab: TabSessionState?,
    ): Action {
        val tabsCount = currentOpenedTabsCount
        val isPrivateMode = context.components.appStore.state.mode.isPrivate

        return when (toolbarAction) {
            ToolbarAction.NewTab ->
                ActionButtonRes(
                    drawableResId = iconsR.drawable.mozac_ic_plus_24,
                    contentDescription =
                        if (isPrivateMode) {
                            R.string.home_screen_shortcut_open_new_private_tab_2
                        } else {
                            R.string.home_screen_shortcut_open_new_tab_2
                        },
                    onClick = object : BrowserToolbarEvent {},
                )

            ToolbarAction.Back ->
                ActionButtonRes(
                    drawableResId = iconsR.drawable.mozac_ic_back_24,
                    contentDescription = R.string.browser_menu_back,
                    state =
                        if (tab?.content?.canGoBack == true) {
                            ActionButton.State.DEFAULT
                        } else {
                            ActionButton.State.DISABLED
                        },
                    onClick = object : BrowserToolbarEvent {},
                )

            ToolbarAction.Forward ->
                ActionButtonRes(
                    drawableResId = iconsR.drawable.mozac_ic_forward_24,
                    contentDescription = R.string.browser_menu_forward,
                    state =
                        if (tab?.content?.canGoForward == true) {
                            ActionButton.State.DEFAULT
                        } else {
                            ActionButton.State.DISABLED
                        },
                    onClick = object : BrowserToolbarEvent {},
                )

            ToolbarAction.RefreshOrStop ->
                ActionButtonRes(
                    drawableResId = iconsR.drawable.mozac_ic_arrow_clockwise_24,
                    contentDescription = R.string.browser_menu_refresh,
                    onClick = object : BrowserToolbarEvent {},
                )

            ToolbarAction.Menu ->
                ActionButtonRes(
                    drawableResId = iconsR.drawable.mozac_ic_more_vertical_round_24,
                    contentDescription = R.string.content_description_menu,
                    highlighted =
                        BrowserMenuConfiguration.hasNotificationToShow(
                            context.components.appStore.state.supportedMenuNotifications,
                            context.components.menuCustomizationRepository.customizedMenuInfo.value,
                        ),
                    onClick = object : BrowserToolbarEvent {},
                )

            ToolbarAction.TabCounter -> {
                TabCounterAction(
                    count = tabsCount,
                    contentDescription = "",
                    showPrivacyMask = isPrivateMode,
                    onClick = object : BrowserToolbarEvent {},
                )
            }

            ToolbarAction.Bookmark -> {
                ActionButtonRes(
                    drawableResId = iconsR.drawable.mozac_ic_bookmark_24,
                    contentDescription = R.string.browser_menu_bookmark_this_page_2,
                    onClick = object : BrowserToolbarEvent {},
                )
            }

            ToolbarAction.EditBookmark -> {
                ActionButtonRes(
                    drawableResId = iconsR.drawable.mozac_ic_bookmark_fill_24,
                    contentDescription = R.string.browser_menu_edit_bookmark,
                    onClick = object : BrowserToolbarEvent {},
                    state = ActionButton.State.ACTIVE,
                )
            }

            ToolbarAction.Share ->
                ActionButtonRes(
                    drawableResId = iconsR.drawable.mozac_ic_share_android_24,
                    contentDescription = R.string.browser_menu_share,
                    onClick = object : BrowserToolbarEvent {},
                )

            ToolbarAction.SiteInfo -> {
                val highlight =
                    (tab?.content?.permissionHighlights?.permissionsChanged == true) ||
                        (tab?.trackingProtection?.ignoredOnTrackingProtection == true)

                if (tab?.content?.url?.isContentUrl() == true) {
                    ActionButtonRes(
                        drawableResId = iconsR.drawable.mozac_ic_page_portrait_24,
                        contentDescription = toolbarR.string.mozac_browser_toolbar_content_description_site_info,
                        highlighted = highlight,
                        onClick = object : BrowserToolbarEvent {},
                    )
                } else if (tab?.content?.securityInfo?.isSecure != true) {
                    ActionButtonRes(
                        drawableResId = iconsR.drawable.mozac_ic_shield_slash_24,
                        contentDescription = toolbarR.string.mozac_browser_toolbar_content_description_site_info,
                        highlighted = highlight,
                        onClick = object : BrowserToolbarEvent {},
                    )
                } else if (tab.trackingProtection.enabled && !tab.trackingProtection.ignoredOnTrackingProtection) {
                    ActionButtonRes(
                        drawableResId = iconsR.drawable.mozac_ic_shield_checkmark_24,
                        contentDescription = toolbarR.string.mozac_browser_toolbar_content_description_site_info,
                        highlighted = highlight,
                        onClick = object : BrowserToolbarEvent {},
                    )
                } else {
                    ActionButtonRes(
                        drawableResId = iconsR.drawable.mozac_ic_shield_cross_24,
                        contentDescription = toolbarR.string.mozac_browser_toolbar_content_description_site_info,
                        highlighted = highlight,
                        onClick = object : BrowserToolbarEvent {},
                    )
                }
            }

            ToolbarAction.Translate ->
                ActionButtonRes(
                    drawableResId = iconsR.drawable.mozac_ic_translate_24,
                    contentDescription = R.string.browser_toolbar_translate,
                    state =
                        if (tab?.translationsState?.isTranslated == true) {
                            ActionButton.State.ACTIVE
                        } else {
                            ActionButton.State.DEFAULT
                        },
                    onClick = object : BrowserToolbarEvent {},
                )

            ToolbarAction.Homepage ->
                ActionButtonRes(
                    drawableResId = iconsR.drawable.mozac_ic_home_24,
                    contentDescription = R.string.browser_menu_homepage,
                    onClick = object : BrowserToolbarEvent {},
                )

            ToolbarAction.Summarize ->
                ActionButtonRes(
                    drawableResId = iconsR.drawable.mozac_ic_lightning_24,
                    contentDescription = summariesR.string.mozac_summarize_settings_summarize_pages,
                    state =
                        when (appStore.state.mode) {
                            Normal -> ActionButton.State.DEFAULT
                            Private -> ActionButton.State.DISABLED
                        },
                    onClick = object : BrowserToolbarEvent {},
                )
        }
    }

    private fun buildSearchEngineSelector(selectedSearchEngine: SearchEngine?): List<Action> {
        return listOfNotNull(
            BrowserToolbarSearchMiddleware.buildSearchSelector(
                selectedSearchEngine = selectedSearchEngine,
                searchEngineShortcuts = emptyList(),
                resources = context.resources,
            )
        )
    }

    init {
        initializeView()
    }

    @Suppress("LongMethod")
    private fun initializeView() {
        bindToolbar()

        val isToolbarAtTop = context.components.settings.toolbarPosition == ToolbarPosition.TOP
        if (isToolbarAtTop) {
            mockToolbarView.updateLayoutParams<LayoutParams> {
                gravity = Gravity.TOP
            }
        }
    }

    override fun onLayout(changed: Boolean, left: Int, top: Int, right: Int, bottom: Int) {
        super.onLayout(changed, left, top, right, bottom)

        binding.previewThumbnail.translationY =
            if (context.components.settings.toolbarPosition == ToolbarPosition.TOP) {
                mockToolbarView.height.toFloat()
            } else {
                0f
            }
    }

    private fun ToolbarPosition.toGravity(): ToolbarGravity =
        when (this) {
            ToolbarPosition.TOP -> ToolbarGravity.Top
            ToolbarPosition.BOTTOM -> ToolbarGravity.Bottom
        }

    private fun BrowserToolbarStore.dispatchAll(vararg actions: BrowserToolbarAction) {
        actions.forEach { browserToolbarStore.dispatch(it) }
    }

    /** Load a preview for a thumbnail. */
    fun loadDestinationPreview(destination: TabSessionState) {
        doOnNextLayout {
            val thumbnail = binding.previewThumbnail
            val size = min(thumbnail.height, thumbnail.width)

            thumbnailLoader.loadIntoView(
                thumbnail,
                ImageLoadRequest(destination.id, size, destination.content.private),
            )

            toScope().launch {
                val prefs = context.components.settings
                val url = destination.content.url
                val isHome = url == ABOUT_HOME_URL
                val topToolbar = prefs.toolbarPosition == ToolbarPosition.TOP
                val homeSearchEnabled = prefs.enableHomepageSearchBar

                browserToolbarStore.dispatch(
                    BrowserToolbarAction.ToolbarGravityUpdated(prefs.toolbarPosition.toGravity())
                )

                when {
                    isHome && topToolbar && homeSearchEnabled -> dispatchHomeSearchBarActions(destination)
                    isHome -> dispatchHomeActions(destination)
                    else -> dispatchWebPageActions(destination)
                }

                bindToolbar()
            }
        }
    }

    private suspend fun dispatchHomeSearchBarActions(destination: TabSessionState) {
        browserToolbarStore.dispatchAll(
            BrowserDisplayToolbarAction.NavigationActionsUpdated(buildNavigationActions(destination))
        )
    }

    private suspend fun dispatchHomeActions(destination: TabSessionState) {
        val selectedEngine = context.components.core.store.state.search.selectedOrDefaultSearchEngine

        browserToolbarStore.dispatchAll(
            BrowserDisplayToolbarAction.PageActionsStartUpdated(buildSearchEngineSelector(selectedEngine)),
            BrowserDisplayToolbarAction.BrowserActionsEndUpdated(buildComposableToolbarBrowserEndActions(destination)),
            PageOriginUpdated(buildComposableToolbarPageOrigin(destination)),
            BrowserDisplayToolbarAction.NavigationActionsUpdated(buildNavigationActions(destination)),
        )
    }

    private suspend fun dispatchWebPageActions(destination: TabSessionState) {
        browserToolbarStore.dispatchAll(
            BrowserDisplayToolbarAction.BrowserActionsStartUpdated(
                buildComposableToolbarBrowserStartActions(destination)
            ),
            BrowserDisplayToolbarAction.BrowserActionsEndUpdated(buildComposableToolbarBrowserEndActions(destination)),
            BrowserDisplayToolbarAction.PageActionsStartUpdated(buildComposableToolbarPageStartActions(destination)),
            BrowserDisplayToolbarAction.PageActionsEndUpdated(buildComposableToolbarPageEndActions(destination)),
            BrowserDisplayToolbarAction.NavigationActionsUpdated(buildNavigationActions(destination)),
            PageOriginUpdated(buildComposableToolbarPageOrigin(destination)),
        )
    }

    private val currentOpenedTabsCount: Int
        get() {
            val store = context.components.core.store
            return store.state.selectedTab?.let {
                store.state.getNormalOrPrivateTabs(it.content.private).size
            } ?: store.state.tabs.size
        }

    private fun bindToolbar() {
        buildBottomComposableToolbar()
        mockToolbarView = buildTopComposableToolbar()
    }

    private fun buildTopComposableToolbar(): ComposeView {
        return binding.composableTopToolbar.apply {
            setViewCompositionStrategy(ViewCompositionStrategy.DisposeOnViewTreeLifecycleDestroyed)

            setContent {
                FirefoxTheme {
                    Column {
                        if (
                            context.components.settings.enableHomepageSearchBar &&
                                context.components.settings.toolbarPosition == ToolbarPosition.TOP
                        ) {
                            BrowserSimpleToolbar(
                                store = browserToolbarStore,
                                appStore = context.components.appStore,
                            )
                        } else if (context.components.settings.toolbarPosition == ToolbarPosition.TOP) {
                            BrowserToolbar(store = browserToolbarStore)
                        }
                    }
                }
            }
                .apply {
                    isVisible = true
                }
        }
    }

    @Suppress("CognitiveComplexMethod")
    private fun buildBottomComposableToolbar(): ComposeView {
        return binding.composableBottomToolbar.apply {
            setViewCompositionStrategy(ViewCompositionStrategy.DisposeOnViewTreeLifecycleDestroyed)

            setContent {
                FirefoxTheme {
                    Column {
                        if (context.components.settings.toolbarPosition == ToolbarPosition.BOTTOM) {
                            BrowserToolbar(store = browserToolbarStore)
                        }

                        if (browserToolbarStore.state.displayState.navigationActions.isNotEmpty()) {
                            val toolbarGravity =
                                when (context.components.settings.shouldUseBottomToolbar) {
                                    true -> ToolbarGravity.Bottom
                                    false -> ToolbarGravity.Top
                                }
                            NavigationBar(
                                actions = browserToolbarStore.state.displayState.navigationActions,
                                toolbarGravity = toolbarGravity,
                                showDivider = toolbarGravity == ToolbarGravity.Top,
                                onInteraction = {},
                            )
                        }
                    }
                }
            }
                .apply {
                    isVisible = true
                }
        }
    }

    private fun buildComposableToolbarPageStartActions(tab: TabSessionState?): List<Action> {
        return listOf(ToolbarActionConfig(ToolbarAction.SiteInfo))
            .filter { config ->
                config.isVisible()
            }
            .map { config ->
                buildAction(config.action, tab)
            }
    }

    private fun buildComposableToolbarPageEndActions(tab: TabSessionState?): List<Action> {
        val settings = context.components.settings
        val isWideScreen = context.isWideWindow()
        val tabStripEnabled = settings.isTabStripEnabled
        val shareShortcutEnabled = ShortcutType.fromValue(settings.toolbarSimpleShortcutKey) == ShortcutType.SHARE

        return listOf(
                ToolbarActionConfig(ToolbarAction.Share) {
                    isWideScreen && !tabStripEnabled && !shareShortcutEnabled
                }
            )
            .filter { config ->
                config.isVisible()
            }
            .map { config ->
                buildAction(config.action, tab)
            }
    }

    private fun buildComposableToolbarBrowserStartActions(tab: TabSessionState?): List<Action> {
        val isWideScreen = context.isWideWindow()

        return listOf(
                ToolbarActionConfig(ToolbarAction.Back) { isWideScreen },
                ToolbarActionConfig(ToolbarAction.Forward) { isWideScreen },
                ToolbarActionConfig(ToolbarAction.RefreshOrStop) { isWideScreen },
            )
            .filter { config ->
                config.isVisible()
            }
            .map { config ->
                buildAction(config.action, tab)
            }
    }

    private suspend fun buildComposableToolbarBrowserEndActions(tab: TabSessionState?): List<Action> {
        val settings = context.components.settings
        val isWideWindow = context.isWideWindow()
        val isTallWindow = context.isTallWindow()
        val shouldUseExpandedToolbar = settings.shouldUseExpandedToolbar

        val primarySlotAction = ShortcutType.fromValue(settings.activeSimpleToolbarShortcutKey)?.toToolbarAction(tab)

        return listOfNotNull(
                primarySlotAction?.let {
                    ToolbarActionConfig(primarySlotAction) {
                        (!shouldUseExpandedToolbar || !isTallWindow || isWideWindow) &&
                            tab?.content?.url != ABOUT_HOME_URL
                    }
                },
                ToolbarActionConfig(ToolbarAction.TabCounter) {
                    !shouldUseExpandedToolbar || !isTallWindow || isWideWindow
                },
                ToolbarActionConfig(ToolbarAction.Menu) {
                    !shouldUseExpandedToolbar || !isTallWindow || isWideWindow
                },
            )
            .filter { config ->
                config.isVisible()
            }
            .map { config ->
                buildAction(config.action, tab)
            }
    }

    private suspend fun buildNavigationActions(tab: TabSessionState): List<Action> {
        val settings = context.components.settings
        val isWideWindow = context.isWideWindow()
        val isTallWindow = context.isTallWindow()
        val shouldUseExpandedToolbar = settings.shouldUseExpandedToolbar

        val primarySlotAction =
            ShortcutType.fromValue(settings.toolbarExpandedShortcutKey)?.toToolbarAction(tab) ?: getBookmarkAction(tab)

        return listOf(
                ToolbarActionConfig(primarySlotAction) { shouldUseExpandedToolbar && isTallWindow && !isWideWindow },
                ToolbarActionConfig(ToolbarAction.Share) { shouldUseExpandedToolbar && isTallWindow && !isWideWindow },
                ToolbarActionConfig(ToolbarAction.NewTab) { shouldUseExpandedToolbar && isTallWindow && !isWideWindow },
                ToolbarActionConfig(ToolbarAction.TabCounter) {
                    shouldUseExpandedToolbar && isTallWindow && !isWideWindow
                },
                ToolbarActionConfig(ToolbarAction.Menu) { shouldUseExpandedToolbar && isTallWindow && !isWideWindow },
            )
            .filter { config ->
                config.isVisible()
            }
            .map { config ->
                buildAction(config.action, tab)
            }
    }

    private suspend fun buildComposableToolbarPageOrigin(tab: TabSessionState): PageOrigin {
        val url = tab.content.url

        val displayUrl =
            if (url == ABOUT_HOME_URL) {
                ""
            } else {
                val spannedUrl = url.applyRegistrableDomainSpan(context.components.publicSuffixList)
                URLStringUtils.toDisplayUrl(spannedUrl)
            }

        return PageOrigin(
            hint = R.string.search_hint,
            title = null,
            url = displayUrl,
            onClick = object : BrowserToolbarEvent {},
        )
    }

    private suspend fun getBookmarkAction(tab: TabSessionState?): ToolbarAction {
        val isBookmarked =
            tab?.content?.url?.let { url ->
                context.components.core.bookmarksStorage.getBookmarksWithUrl(url).getOrDefault(emptyList()).isNotEmpty()
            } ?: return ToolbarAction.Bookmark

        return if (isBookmarked) ToolbarAction.EditBookmark else ToolbarAction.Bookmark
    }

    private suspend fun ShortcutType.toToolbarAction(tab: TabSessionState?) =
        when (this) {
            ShortcutType.NEW_TAB -> ToolbarAction.NewTab
            ShortcutType.SHARE -> ToolbarAction.Share
            ShortcutType.BOOKMARK -> getBookmarkAction(tab)
            ShortcutType.TRANSLATE ->
                when (isTranslationsFeatureAvailable()) {
                    true -> ToolbarAction.Translate
                    else -> ToolbarAction.NewTab
                }
            ShortcutType.HOMEPAGE -> ToolbarAction.Homepage
            ShortcutType.BACK -> ToolbarAction.Back
            ShortcutType.SUMMARIZE ->
                when {
                    summarizationFeatureSettings.canShowFeature -> ToolbarAction.Summarize
                    // The tab strip already provides a new tab button, so fall back to the default tab strip shortcut.
                    context.components.settings.isTabStripEnabled -> ToolbarAction.Share
                    else -> ToolbarAction.NewTab
                }
            ShortcutType.NONE -> null
        }

    private suspend fun isTranslationsFeatureAvailable(): Boolean {
        val isTranslationEngineSupported = browserStore.state.translationEngine.isEngineSupported ?: false
        return isTranslationEngineSupported &&
            FxNimbus.features.translations.value().mainFlowToolbarEnabled &&
            translationsFeatureSettings.isEnabled.first()
    }
}
