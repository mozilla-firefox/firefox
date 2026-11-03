/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.toolbar

import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import androidx.annotation.VisibleForTesting
import androidx.appcompat.app.AppCompatActivity
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.wrapContentHeight
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.ComposeView
import androidx.coordinatorlayout.widget.CoordinatorLayout
import androidx.coordinatorlayout.widget.CoordinatorLayout.LayoutParams
import androidx.core.view.isVisible
import mozilla.components.browser.state.action.AwesomeBarAction
import mozilla.components.browser.state.state.CustomTabSessionState
import mozilla.components.browser.state.state.ExternalAppType
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.base.utils.BackInvokedHandler
import mozilla.components.compose.browser.toolbar.BrowserToolbar
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarAction.ToolbarGravityUpdated
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarStore
import mozilla.components.compose.browser.toolbar.store.ToolbarGravity
import mozilla.components.compose.browser.toolbar.store.ToolbarGravity.Bottom
import mozilla.components.compose.browser.toolbar.store.ToolbarGravity.Top
import mozilla.components.concept.engine.EngineView
import mozilla.components.concept.toolbar.ScrollableToolbar
import mozilla.components.feature.toolbar.ToolbarBehaviorController
import mozilla.components.lib.state.ext.observeAsComposableState
import mozilla.components.support.ktx.android.view.findViewInHierarchy
import mozilla.components.support.utils.KeyboardState
import mozilla.components.support.utils.ext.isKeyboardVisible
import mozilla.components.support.utils.keyboardAsState
import mozilla.components.ui.widgets.behavior.DependencyGravity
import mozilla.components.ui.widgets.behavior.EngineViewScrollingBehavior
import mozilla.components.ui.widgets.behavior.EngineViewScrollingBehaviorFactory
import org.mozilla.fenix.browser.store.BrowserScreenStore
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction.SearchAction.SearchEnded
import org.mozilla.fenix.components.toolbar.ToolbarPosition.BOTTOM
import org.mozilla.fenix.components.toolbar.ToolbarPosition.TOP
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.utils.Settings

/**
 * A wrapper over the [BrowserToolbar] composable that owns the toolbar [View] and its scrolling behaviour.
 *
 * @param activity [AppCompatActivity] hosting the toolbar.
 * @param container [ViewGroup] which will serve as parent of this View.
 * @param toolbarStore [BrowserToolbarStore] containing the composable toolbar state.
 * @param browserScreenStore [BrowserScreenStore] used for integration with other browser screen functionalities.
 * @param appStore [AppStore] used for integration with other application features.
 * @param browserStore [BrowserStore] used for observing the browsing details.
 * @param settings [Settings] object to get the toolbar position and other settings.
 * @param customTabSession [CustomTabSessionState] if the toolbar is shown in a custom tab.
 * @param tabStripContent Composable content for the tab strip.
 * @param tabGroupsStripContent Composable content for the tab groups strip.
 * @param searchSuggestionsContent [Composable] as the search suggestions content to be displayed together with this
 *   toolbar.
 * @param navigationBarContent [Composable] content for the navigation bar.
 */
@Suppress("LongParameterList")
class BrowserToolbarComposable(
    private val activity: AppCompatActivity,
    private val container: ViewGroup,
    private val toolbarStore: BrowserToolbarStore,
    private val browserScreenStore: BrowserScreenStore,
    private val appStore: AppStore,
    private val browserStore: BrowserStore,
    private val settings: Settings,
    private val customTabSession: CustomTabSessionState? = null,
    private val tabStripContent: @Composable () -> Unit,
    private val tabGroupsStripContent: @Composable () -> Unit,
    private val searchSuggestionsContent: @Composable (Modifier) -> Unit,
    private val navigationBarContent: (@Composable () -> Unit)?,
) : ScrollableToolbar {
    init {
        if (!settings.shouldUseMinimalBottomToolbarWhenEnteringText) {
            setupShowingToolbarsAfterKeyboardHidden()
        }

        // Reset the toolbar position whenever coming back to browsing
        // like after changing the toolbar position in settings.
        toolbarStore.dispatch(ToolbarGravityUpdated(buildToolbarGravityConfig()))
    }

    val layout: View =
        ScrollableToolbarComposeView(activity, this) {
                val isSearching = toolbarStore.observeAsComposableState { it.isEditMode() }.value
                val shouldShowTabStripAtTop = remember {
                    customTabSession == null && settings.shouldShowTabStripAtTop
                }
                val shouldShowTabStripAtBottom = remember {
                    customTabSession == null && settings.shouldShowTabStripAtBottom
                }
                val shouldShowTabGroupsStrip: Boolean = remember {
                    customTabSession == null && settings.shouldShowTabGroupsStrip
                }
                val isSearchActive = appStore.observeAsComposableState { it.searchState.isSearchActive }.value
                val hasBottomStrip = (shouldShowTabStripAtBottom && !isSearchActive) || shouldShowTabGroupsStrip
                val customColors = browserScreenStore.observeAsComposableState { it.customTabColors }
                val shouldUseBottomToolbar = remember { settings.shouldUseBottomToolbar }

                val toolbarState by toolbarStore.stateFlow.collectAsState()
                val toolbarCFR = toolbarState.displayState.cfr

                DisposableEffect(activity) {
                    val toolbarController =
                        ToolbarBehaviorController(
                            toolbar = this@BrowserToolbarComposable,
                            store = browserStore,
                            customTabId = customTabSession?.id,
                        )
                    toolbarController.start()
                    onDispose { toolbarController.stop() }
                }

                BackInvokedHandler(isSearching) {
                    appStore.dispatch(SearchEnded)
                    browserStore.dispatch(AwesomeBarAction.EngagementFinished(abandoned = true))
                }

                FirefoxTheme {
                    val materialColors = MaterialTheme.colorScheme
                    val colorScheme =
                        remember(customColors.value, materialColors) {
                            materialColors.copy(
                                // Toolbar background
                                surface = customColors.value?.toolbarColor?.let { Color(it) } ?: materialColors.surface,
                                // Page origin background
                                surfaceContainerHighest =
                                    when (customTabSession) {
                                        // show a different background only for normal tabs
                                        null -> materialColors.surfaceContainerHighest
                                        else ->
                                            customColors.value?.toolbarColor?.let { Color(it) }
                                                ?: materialColors.surface
                                    },
                                onSurface =
                                    customColors.value?.readableColor?.let { Color(it) } ?: materialColors.onSurface,
                                onSurfaceVariant =
                                    customColors.value?.secondaryReadableColor?.let { Color(it) }
                                        ?: materialColors.onSurfaceVariant,
                            )
                        }

                    MaterialTheme(colorScheme = colorScheme) {
                        when (!shouldUseBottomToolbar) {
                            true ->
                                Column(modifier = Modifier.fillMaxWidth().wrapContentHeight()) {
                                    if (shouldShowTabStripAtTop) {
                                        tabStripContent()
                                    }
                                    BrowserToolbar(
                                        store = toolbarStore,
                                        cfr = toolbarCFR,
                                        useMinimalBottomToolbarWhenEnteringText =
                                            settings.shouldUseMinimalBottomToolbarWhenEnteringText,
                                    )
                                    if (customTabSession == null) {
                                        searchSuggestionsContent(Modifier.weight(1f))
                                    }
                                }

                            false ->
                                Column(modifier = Modifier.fillMaxWidth().wrapContentHeight()) {
                                    if (customTabSession == null) {
                                        searchSuggestionsContent(Modifier.weight(1f))
                                    }
                                    TopDividerColumn(showDivider = hasBottomStrip, modifier = Modifier.fillMaxWidth()) {
                                        if (shouldShowTabStripAtBottom) {
                                            Box(modifier = Modifier.background(MaterialTheme.colorScheme.surface)) {
                                                tabStripContent()
                                            }
                                        }
                                        if (shouldShowTabGroupsStrip) {
                                            tabGroupsStripContent()
                                        }
                                        BrowserToolbar(
                                            store = toolbarStore,
                                            cfr = toolbarCFR,
                                            useMinimalBottomToolbarWhenEnteringText =
                                                settings.shouldUseMinimalBottomToolbarWhenEnteringText,
                                            showDivider = !hasBottomStrip,
                                        )
                                        navigationBarContent?.invoke()
                                    }
                                }
                        }
                    }
                }
            }
            .apply {
                layoutParams =
                    LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT).apply {
                        gravity =
                            when (settings.toolbarPosition) {
                                TOP -> Gravity.TOP
                                BOTTOM -> Gravity.BOTTOM
                            }
                    }
            }

    init {
        container.addView(layout)
        setToolbarBehavior(settings.toolbarPosition)
    }

    @VisibleForTesting
    internal val isPwaTabOrTwaTab: Boolean
        get() =
            customTabSession?.config?.externalAppType == ExternalAppType.PROGRESSIVE_WEB_APP ||
                customTabSession?.config?.externalAppType == ExternalAppType.TRUSTED_WEB_ACTIVITY

    override fun expand() {
        // expand only for normal tabs and custom tabs not for PWA or TWA
        if (isPwaTabOrTwaTab) {
            return
        }

        (layout.layoutParams as CoordinatorLayout.LayoutParams).apply {
            (behavior as? EngineViewScrollingBehavior)?.forceExpand()
        }
    }

    override fun collapse() {
        // collapse only for normal tabs and custom tabs not for PWA or TWA. Mirror expand()
        if (isPwaTabOrTwaTab) {
            return
        }

        (layout.layoutParams as CoordinatorLayout.LayoutParams).apply {
            (behavior as? EngineViewScrollingBehavior)?.forceCollapse()
        }
    }

    override fun enableScrolling() {
        if (!container.isKeyboardVisible()) {
            (layout.layoutParams as CoordinatorLayout.LayoutParams).apply {
                (behavior as? EngineViewScrollingBehavior)?.enableScrolling()
            }
        }
    }

    override fun disableScrolling() {
        (layout.layoutParams as CoordinatorLayout.LayoutParams).apply {
            (behavior as? EngineViewScrollingBehavior)?.disableScrolling()
        }
    }

    internal fun gone() {
        layout.isVisible = false
    }

    internal fun visible() {
        layout.isVisible = true
    }

    /**
     * Sets whether the toolbar will have a dynamic behavior (to be scrolled) or not.
     *
     * This will intrinsically check and disable the dynamic behavior if
     * - this is disabled in app settings
     * - toolbar is placed at the bottom and tab shows a PWA or TWA
     *
     * Also if the user has not explicitly set a toolbar position and has a screen reader enabled the toolbar will be
     * placed at the top and in a fixed position.
     *
     * @param toolbarPosition [ToolbarPosition] to set the toolbar to.
     * @param shouldDisableScroll force disable of the dynamic behavior irrespective of the intrinsic checks.
     */
    fun setToolbarBehavior(toolbarPosition: ToolbarPosition, shouldDisableScroll: Boolean = false) {
        when (toolbarPosition) {
            ToolbarPosition.BOTTOM -> {
                if (settings.isDynamicToolbarEnabled && !settings.shouldUseFixedTopToolbar) {
                    setDynamicToolbarBehavior(true)
                } else {
                    expandToolbarAndMakeItFixed()
                }
            }
            ToolbarPosition.TOP -> {
                if (settings.shouldUseFixedTopToolbar || !settings.isDynamicToolbarEnabled || shouldDisableScroll) {
                    expandToolbarAndMakeItFixed()
                } else {
                    setDynamicToolbarBehavior(false)
                }
            }
        }
    }

    @VisibleForTesting
    internal fun expandToolbarAndMakeItFixed() {
        expand()
        (layout.layoutParams as CoordinatorLayout.LayoutParams).apply {
            behavior = null
        }
    }

    @VisibleForTesting
    internal fun setDynamicToolbarBehavior(isToolbarAtBottom: Boolean) {
        (container.findViewInHierarchy { it is EngineView } as? EngineView)?.let { engineView ->
            (layout.layoutParams as CoordinatorLayout.LayoutParams).apply {
                behavior =
                    EngineViewScrollingBehaviorFactory(useScrollData = settings.useNewDynamicToolbarBehaviour)
                        .build(
                            engineView = engineView,
                            dependency = layout,
                            dependencyGravity =
                                when (isToolbarAtBottom) {
                                    true -> DependencyGravity.Bottom
                                    false -> DependencyGravity.Top
                                },
                        )
            }
        }
    }

    private fun setupShowingToolbarsAfterKeyboardHidden() {
        container.addView(
            ComposeView(container.context).apply {
                setContent {
                    val keyboardState by keyboardAsState()
                    LaunchedEffect(keyboardState) {
                        if (keyboardState == KeyboardState.Closed) {
                            expand()
                        }
                    }
                }
            }
        )
    }

    private fun buildToolbarGravityConfig(): ToolbarGravity =
        when (settings.shouldUseBottomToolbar) {
            true -> Bottom
            false -> Top
        }
}
