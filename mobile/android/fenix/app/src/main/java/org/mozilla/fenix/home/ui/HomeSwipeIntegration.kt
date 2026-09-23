/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.ui

import android.app.Activity
import android.graphics.Rect
import android.view.View
import androidx.navigation.NavController
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.utils.ABOUT_HOME_URL
import mozilla.components.feature.tabs.TabsUseCases
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.SwipeGestureLayout
import org.mozilla.fenix.browser.TabPreview
import org.mozilla.fenix.components.toolbar.gestures.ToolbarHorizontalGesturesHandler

/**
 * Adds swipe-to-switch-tabs support from the homepage.
 *
 * When "Homepage as a tab" is enabled, Home represents a special tab (`about:home`). Swiping on the Home toolbar should
 * switch to adjacent tabs with a sliding preview, just like the browser does. Because the Home toolbar and navigation
 * bar are Compose composables rather than Android views, their on-screen bounds are supplied through
 * [toolbarLayoutRect] and [navBarLayoutRect] instead of being read off a `View`. When the swipe lands on a non-home
 * tab, this class navigates from HomeFragment to BrowserFragment so the selected tab's content becomes visible.
 *
 * @param activity The [Activity] hosting the home screen.
 * @param store The [BrowserStore] used to resolve the adjacent tab to switch to.
 * @param selectTabUseCase Used to select the destination tab when a swipe completes.
 * @param contentLayout The view translated while swiping (the homepage content).
 * @param gestureLayout The [SwipeGestureLayout] that intercepts the swipe gestures.
 * @param navController Used to navigate to the browser when swiping onto a non-home tab.
 * @param navBarLayoutRect Supplies the navigation bar's current bounds in screen coordinates, or null if it is absent
 *   or has not been laid out yet.
 * @param toolbarLayoutRect Supplies the toolbar's current bounds in screen coordinates, or null if it has not been laid
 *   out yet.
 * @param tabPreview The [TabPreview] used to display the adjacent tab while swiping.
 */
class HomeSwipeIntegration(
    private val activity: Activity,
    private val store: BrowserStore,
    private val selectTabUseCase: TabsUseCases.SelectTabUseCase,
    private val contentLayout: View,
    private val gestureLayout: SwipeGestureLayout,
    private val navController: NavController,
    private val navBarLayoutRect: () -> Rect?,
    private val toolbarLayoutRect: () -> Rect?,
    private val tabPreview: TabPreview,
) {

    /**
     * Registers the toolbar swipe gesture handler on the home UI, enabling switching between adjacent tabs. Callers are
     * responsible for gating this on the relevant settings.
     */
    fun initializeSwipeUI() {
        gestureLayout.addGestureListener(
            ToolbarHorizontalGesturesHandler(
                activity = activity,
                contentLayout = contentLayout,
                tabPreview = tabPreview,
                toolbarLayoutRect = { toolbarLayoutRect() ?: Rect() },
                navBarLayoutRect = { navBarLayoutRect() },
                store = store,
                selectTabUseCase = selectTabUseCase,
                onSwipeStarted = {},
                onTabSwitched = { tab ->
                    if (
                        tab.content.url != ABOUT_HOME_URL && navController.currentDestination?.id == R.id.homeFragment
                    ) {
                        navController.navigate(R.id.browserFragment)
                    }
                },
            )
        )
    }
}
