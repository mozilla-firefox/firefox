/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.toolbar

import android.content.Context
import android.view.Gravity
import android.view.ViewGroup
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.wrapContentHeight
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.coordinatorlayout.widget.CoordinatorLayout.LayoutParams
import androidx.fragment.app.Fragment
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.ViewModelProvider
import androidx.navigation.NavController
import mozilla.components.browser.state.helper.Target
import mozilla.components.browser.state.state.CustomTabSessionState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.browser.thumbnails.BrowserThumbnails
import mozilla.components.compose.base.Divider
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.browser.toolbar.BrowserToolbar
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarState
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarStore
import mozilla.components.feature.tabs.TabsUseCases
import mozilla.components.feature.toolbar.ToolbarBehaviorController
import mozilla.components.lib.state.ext.observeAsComposableState
import org.mozilla.fenix.browser.browsingmode.BrowsingModeManager
import org.mozilla.fenix.browser.store.BrowserScreenStore
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.StoreProvider
import org.mozilla.fenix.components.toolbar.BrowserToolbarMiddleware.LifecycleDependencies
import org.mozilla.fenix.components.toolbar.ToolbarPosition.BOTTOM
import org.mozilla.fenix.components.toolbar.ToolbarPosition.TOP
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.utils.Settings

/**
 * A wrapper over the [BrowserToolbar] composable to allow for extra customisation and
 * integration in the same framework as the [BrowserToolbarView]
 *
 * @param context [Context] used for various system interactions.
 * @param lifecycleOwner [Fragment] as a [LifecycleOwner] to used to organize lifecycle dependent operations.
 * @param container [ViewGroup] which will serve as parent of this View.
 * @param navController [NavController] to use for navigating to other in-app destinations.
 * @param appStore [AppStore] to sync from.
 * @param browserScreenStore [BrowserScreenStore] used for integration with other browser screen functionalities.
 * @param browserStore [BrowserStore] used for observing the browsing details.
 * @param browsingModeManager [BrowsingModeManager] for querying the current browsing mode.
 * @param tabsUseCases [TabsUseCases] for managing tabs.
 * @param thumbnailsFeature [BrowserThumbnails] for requesting screenshots of the current tab.
 * @param settings [Settings] object to get the toolbar position and other settings.
 * @param customTabSession [CustomTabSessionState] if the toolbar is shown in a custom tab.
 * @param tabStripContent Composable content for the tab strip.
 */
@Suppress("LongParameterList")
class BrowserToolbarComposable(
    private val context: Context,
    private val lifecycleOwner: Fragment,
    container: ViewGroup,
    private val navController: NavController,
    private val appStore: AppStore,
    private val browserScreenStore: BrowserScreenStore,
    private val browserStore: BrowserStore,
    private val browsingModeManager: BrowsingModeManager,
    private val tabsUseCases: TabsUseCases,
    private val thumbnailsFeature: BrowserThumbnails?,
    private val settings: Settings,
    customTabSession: CustomTabSessionState? = null,
    private val tabStripContent: @Composable () -> Unit,
) : FenixBrowserToolbarView(
    context = context,
    settings = settings,
    customTabSession = customTabSession,
) {
    private var showDivider by mutableStateOf(true)

    private val middleware = getOrCreate<BrowserToolbarMiddleware>()
    private val store = StoreProvider.get(lifecycleOwner) {
        BrowserToolbarStore(
            initialState = BrowserToolbarState(),
            middleware = listOf(middleware),
        )
    }

    override val layout = ScrollableToolbarComposeView(context, this) {
        val shouldShowDivider by remember { mutableStateOf(showDivider) }
        val shouldShowTabStrip: Boolean = remember { shouldShowTabStrip() }
        val progressBarValue = store.observeAsComposableState { it.displayState.progressBarConfig?.progress }.value ?: 0

        DisposableEffect(context) {
            val toolbarController = ToolbarBehaviorController(
                toolbar = this@BrowserToolbarComposable,
                store = browserStore,
                customTabId = customTabSession?.id,
            )
            toolbarController.start()
            onDispose { toolbarController.stop() }
        }

        AcornTheme {
            when (shouldShowTabStrip) {
                true -> Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .wrapContentHeight(),
                ) {
                    tabStripContent()
                    BrowserToolbar(shouldShowDivider, progressBarValue, settings.shouldUseBottomToolbar)
                }

                false -> BrowserToolbar(shouldShowDivider, progressBarValue, settings.shouldUseBottomToolbar)
            }
        }
    }.apply {
        if (!shouldShowTabStrip()) {
            val params = LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT)

            when (settings.toolbarPosition) {
                TOP -> params.gravity = Gravity.TOP
                BOTTOM -> params.gravity = Gravity.BOTTOM
            }

            layoutParams = params
        }
    }

    init {
        container.addView(layout)
        setToolbarBehavior()
    }

    @Composable
    private fun BrowserToolbar(
        shouldShowDivider: Boolean,
        progressBarValue: Int,
        shouldUseBottomToolbar: Boolean,
    ) {
        // Ensure the divider is shown together with the toolbar
        Box {
            BrowserToolbar(
                store = store,
                browserStore = context.components.core.store,
                onTextEdit = {},
                onTextCommit = {},
                target = Target.SelectedTab,
            )
            @Suppress("MagicNumber")
            if (shouldShowDivider && progressBarValue !in 1..99) {
                Divider(
                    modifier = Modifier.align(
                        when (shouldUseBottomToolbar) {
                            true -> Alignment.TopCenter
                            false -> Alignment.BottomCenter
                        },
                    ),
                )
            }
        }
    }

    override fun updateDividerVisibility(isVisible: Boolean) {
        showDivider = isVisible
    }

    private inline fun <reified T> getOrCreate(): T = when (T::class.java) {
        BrowserToolbarMiddleware::class.java ->
            ViewModelProvider(
                lifecycleOwner,
                BrowserToolbarMiddleware.viewModelFactory(
                    appStore = appStore,
                    browserScreenStore = browserScreenStore,
                    browserStore = browserStore,
                    tabsUseCases = tabsUseCases,
                    settings = settings,
                ),
            ).get(BrowserToolbarMiddleware::class.java).also {
                it.updateLifecycleDependencies(
                    LifecycleDependencies(
                        context = context,
                        lifecycleOwner = lifecycleOwner,
                        navController = navController,
                        browsingModeManager = browsingModeManager,
                        thumbnailsFeature = thumbnailsFeature,
                    ),
                )
            } as T

        else -> throw IllegalArgumentException("Unknown type: ${T::class.java}")
    }
}
