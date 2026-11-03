/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.theme

import android.app.Activity
import androidx.fragment.app.DialogFragment
import androidx.fragment.app.Fragment
import androidx.fragment.app.FragmentManager
import androidx.fragment.app.FragmentManager.FragmentLifecycleCallbacks
import androidx.lifecycle.LifecycleOwner
import androidx.navigation.fragment.NavHostFragment
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import mozilla.components.lib.state.ext.flowScoped
import org.mozilla.fenix.browser.BrowserFragment
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.customtabs.ExternalAppBrowserFragment
import org.mozilla.fenix.home.HomeFragment
import org.mozilla.fenix.tabstray.ui.TabManagementFragment
import org.mozilla.fenix.utils.Settings

/**
 * Uses the [ThemeManager] to set the status bar color based on the current fragment.
 *
 * @param themeManager The [ThemeManager] to use for setting the status bar color.
 * @param activity The [Activity] to set the status bar color on.
 * @param appStore The [AppStore] used to observe the search state.
 * @param settings The [Settings] used to read whether the tab strip is enabled.
 * @param tabStripStatusBarView View class that sets the status bar background with the tab strip gradient when the tab
 *   strip is visible.
 * @param mainDispatcher The [CoroutineDispatcher] used to observe wallpaper changes.
 */
class StatusBarColorManager(
    private val themeManager: ThemeManager,
    private val activity: Activity,
    private val appStore: AppStore,
    private val settings: Settings,
    private val tabStripStatusBarView: TabStripStatusBarView,
    private val mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
) : FragmentLifecycleCallbacks() {

    private var wallpaperScope: CoroutineScope? = null

    override fun onFragmentResumed(fragmentManager: FragmentManager, fragment: Fragment) {
        if (fragment is NavHostFragment || fragment is DialogFragment) {
            return
        }

        // These fragments theme their own system bars independently of the browsing mode,
        // so the browsing-mode status bar theming must not run for them.
        if (fragment is ExternalAppBrowserFragment || fragment is TabManagementFragment) {
            return
        }

        themeManager.applyStatusBarTheme(activity)
        updateTabStripGradient(fragment)
    }

    private fun updateTabStripGradient(fragment: Fragment) {
        // Stop observing the previous fragment's wallpaper.
        wallpaperScope?.cancel()
        wallpaperScope = null

        when {
            // Don't show the status bar gradient if tab strip is not enabled, shown at the bottom
            // or private mode is enabled.
            !settings.isTabStripEnabled || settings.shouldUseBottomTabStrip || themeManager.currentTheme.isPrivate -> {
                tabStripStatusBarView.hide()
            }

            // Always show the gradient on the browser.
            fragment is BrowserFragment -> {
                tabStripStatusBarView.show()
            }

            // Observe the search state and show the tab strip gradient accordingly.
            fragment is HomeFragment -> {
                observeHomepageGradient()
            }

            else -> {
                tabStripStatusBarView.hide()
            }
        }
    }

    /**
     * Observe the search state while on the homepage and show the tab strip gradient accordingly.
     *
     * Show it only when a search is active, as the tab strip has no background of its own and the wallpaper shows
     * through the status bar otherwise.
     */
    private fun observeHomepageGradient() {
        wallpaperScope =
            appStore.flowScoped(
                owner = activity as? LifecycleOwner,
                dispatcher = mainDispatcher,
            ) { flow ->
                flow
                    .map { state -> state.searchState.isSearchActive }
                    .distinctUntilChanged()
                    .collect { shouldShowGradient ->
                        if (shouldShowGradient) {
                            tabStripStatusBarView.show()
                        } else {
                            tabStripStatusBarView.hide()
                        }
                    }
            }
    }
}
