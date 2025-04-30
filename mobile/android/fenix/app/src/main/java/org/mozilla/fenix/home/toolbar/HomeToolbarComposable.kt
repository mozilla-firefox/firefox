/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.toolbar

import android.content.Context
import android.view.Gravity
import android.view.ViewGroup
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.ComposeView
import androidx.coordinatorlayout.widget.CoordinatorLayout
import androidx.core.view.updateLayoutParams
import androidx.fragment.app.Fragment
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.ViewModelProvider
import androidx.navigation.NavController
import mozilla.components.browser.state.helper.Target
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.base.Divider
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.browser.toolbar.BrowserToolbar
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarState
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarStore
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.browsingmode.BrowsingModeManager
import org.mozilla.fenix.browser.tabstrip.isTabStripEnabled
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.StoreProvider
import org.mozilla.fenix.components.toolbar.ToolbarPosition.BOTTOM
import org.mozilla.fenix.components.toolbar.ToolbarPosition.TOP
import org.mozilla.fenix.databinding.FragmentHomeBinding
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.home.toolbar.BrowserToolbarMiddleware.LifecycleDependencies
import org.mozilla.fenix.utils.Settings

/**
 * A wrapper over the [BrowserToolbar] composable to allow for extra customisation and
 * integration in the same framework as the [HomeToolbarView].
 *
 * @param context [Context] used for various system interactions.
 * @param lifecycleOwner [Fragment] as a [LifecycleOwner] to used to organize lifecycle dependent operations.
 * @param navController [NavController] to use for navigating to other in-app destinations.
 * @param homeBinding [FragmentHomeBinding] which will serve as parent for this composable.
 * @param appStore [AppStore] to sync from.
 * @param browserStore [BrowserStore] to sync from.
 * @param browsingModeManager [BrowsingModeManager] for querying the current browsing mode.
 * @param settings [Settings] for querying various application settings.
 * @param tabStripContent [Composable] as the tab strip content to be displayed together with this toolbar.
 */
@Suppress("LongParameterList")
internal class HomeToolbarComposable(
    private val context: Context,
    private val lifecycleOwner: Fragment,
    private val navController: NavController,
    private val homeBinding: FragmentHomeBinding,
    private val appStore: AppStore,
    private val browserStore: BrowserStore,
    private val browsingModeManager: BrowsingModeManager,
    private val settings: Settings,
    private val tabStripContent: @Composable () -> Unit,
) : FenixHomeToolbar {
    private var showDivider by mutableStateOf(true)

    private val middleware = getOrCreate<BrowserToolbarMiddleware>()
    private val store = StoreProvider.get(lifecycleOwner) {
        BrowserToolbarStore(
            initialState = BrowserToolbarState(),
            middleware = listOf(middleware),
        )
    }

    override val layout = ComposeView(context).apply {
        setContent {
            val shouldShowDivider by remember { mutableStateOf(showDivider) }
            val shouldShowTabStrip: Boolean = remember { context.isTabStripEnabled() }

            AcornTheme {
                when (shouldShowTabStrip) {
                    true -> Column {
                        tabStripContent()
                        BrowserToolbar(shouldShowDivider, settings.shouldUseBottomToolbar)
                    }

                    false -> BrowserToolbar(shouldShowDivider, settings.shouldUseBottomToolbar)
                }
            }
        }
        homeBinding.homeLayout.addView(this)
    }

    override fun build(browserState: BrowserState) {
        layout.updateLayoutParams {
            (this as? CoordinatorLayout.LayoutParams)?.gravity = when (settings.toolbarPosition) {
                TOP -> Gravity.TOP
                BOTTOM -> Gravity.BOTTOM
            }
        }

        updateHomeAppBarIntegration()
    }

    override fun updateDividerVisibility(isVisible: Boolean) {
        showDivider = isVisible
    }

    override fun updateButtonVisibility(
        browserState: BrowserState,
        shouldAddNavigationBar: Boolean,
    ) {
        // To be added later
    }

    override fun updateTabCounter(browserState: BrowserState) {
        // To be added later
    }

    override fun updateAddressBarVisibility(isVisible: Boolean) {
        // To be added later
    }

    @Composable
    private fun BrowserToolbar(shouldShowDivider: Boolean, shouldUseBottomToolbar: Boolean) {
        // Ensure the divider is shown together with the toolbar
        Box {
            BrowserToolbar(
                store = store,
                browserStore = context.components.core.store,
                onTextEdit = {},
                onTextCommit = {},
                target = Target.Tab("none"),
            )
            if (shouldShowDivider) {
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

    private fun updateHomeAppBarIntegration() {
        if (!settings.shouldUseBottomToolbar) {
            homeBinding.homeAppBar.updateLayoutParams<ViewGroup.MarginLayoutParams> {
                topMargin = context.resources.getDimensionPixelSize(R.dimen.home_fragment_top_toolbar_header_margin) +
                    when (context.isTabStripEnabled()) {
                        true -> context.resources.getDimensionPixelSize(R.dimen.tab_strip_height)
                        false -> 0
                    }
            }
        }
    }

    private inline fun <reified T> getOrCreate(): T = when (T::class.java) {
        BrowserToolbarMiddleware::class.java ->
            ViewModelProvider(
                lifecycleOwner,
                BrowserToolbarMiddleware.viewModelFactory(appStore, browserStore),
            ).get(BrowserToolbarMiddleware::class.java).also {
                it.updateLifecycleDependencies(
                    LifecycleDependencies(
                        context = context,
                        lifecycleOwner = lifecycleOwner,
                        navController = navController,
                        browsingModeManager = browsingModeManager,
                    ),
                )
            } as T

        else -> throw IllegalArgumentException("Unknown type: ${T::class.java}")
    }
}
