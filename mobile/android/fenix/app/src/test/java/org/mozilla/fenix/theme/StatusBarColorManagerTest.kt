/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.theme

import android.app.Activity
import androidx.fragment.app.DialogFragment
import androidx.fragment.app.Fragment
import androidx.fragment.app.FragmentManager
import androidx.navigation.fragment.NavHostFragment
import io.mockk.every
import io.mockk.mockk
import io.mockk.verify
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import org.junit.Test
import org.mozilla.fenix.browser.BrowserFragment
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.components.appstate.search.SearchState
import org.mozilla.fenix.customtabs.ExternalAppBrowserFragment
import org.mozilla.fenix.home.HomeFragment
import org.mozilla.fenix.utils.Settings
import org.mozilla.fenix.wallpapers.Wallpaper
import org.mozilla.fenix.wallpapers.WallpaperState

class StatusBarColorManagerTest {

    private val testDispatcher = StandardTestDispatcher()
    private val themeManager: ThemeManager =
        mockk(relaxed = true) {
            every { currentTheme } returns BrowsingMode.Normal
        }
    private val activity: Activity = mockk(relaxed = true)
    private val settings: Settings = mockk(relaxed = true)
    private val tabStripStatusBarView: TabStripStatusBarView = mockk(relaxed = true)
    private val fragmentManager: FragmentManager = mockk(relaxed = true)
    private val homeFragment: HomeFragment = mockk(relaxed = true)

    @Test
    fun `GIVEN tab strip is enabled WHEN the browser fragment resumes THEN the gradient is shown`() =
        runTest(testDispatcher) {
            val manager = buildStatusBarColorManager(isTabStripEnabled = true)
            manager.onFragmentResumed(fragmentManager, mockk<BrowserFragment>())

            verify { tabStripStatusBarView.show() }
        }

    @Test
    fun `GIVEN tab strip is enabled and edge-to-edge wallpaper WHEN the browser fragment resumes THEN the gradient is still shown`() =
        runTest(testDispatcher) {
            val manager =
                buildStatusBarColorManager(
                    isTabStripEnabled = true,
                    appStore = buildAppStore(Wallpaper.EdgeToEdge),
                )
            manager.onFragmentResumed(fragmentManager, mockk<BrowserFragment>())

            verify { tabStripStatusBarView.show() }
        }

    @Test
    fun `GIVEN a non-default wallpaper WHEN the home fragment resumes THEN the gradient is hidden`() =
        runTest(testDispatcher) {
            val manager =
                buildStatusBarColorManager(
                    isTabStripEnabled = true,
                    appStore = buildAppStore(nonDefaultWallpaper()),
                )
            manager.onFragmentResumed(fragmentManager, homeFragment)
            testScheduler.advanceUntilIdle()

            verify { tabStripStatusBarView.hide() }
            verify(exactly = 0) { tabStripStatusBarView.show() }
        }

    @Test
    fun `GIVEN a non-default wallpaper and an active search WHEN the home fragment resumes THEN the gradient is shown`() =
        runTest(testDispatcher) {
            val manager =
                buildStatusBarColorManager(
                    isTabStripEnabled = true,
                    appStore = buildAppStore(nonDefaultWallpaper(), isSearchActive = true),
                )
            manager.onFragmentResumed(fragmentManager, homeFragment)
            testScheduler.advanceUntilIdle()

            verify { tabStripStatusBarView.show() }
        }

    @Test
    fun `GIVEN the default wallpaper WHEN the home fragment resumes THEN the gradient is hidden`() =
        runTest(testDispatcher) {
            val manager =
                buildStatusBarColorManager(
                    isTabStripEnabled = true,
                    appStore = buildAppStore(Wallpaper.Default),
                )
            manager.onFragmentResumed(fragmentManager, homeFragment)
            testScheduler.advanceUntilIdle()

            verify { tabStripStatusBarView.hide() }
            verify(exactly = 0) { tabStripStatusBarView.show() }
        }

    @Test
    fun `GIVEN the edge-to-edge wallpaper WHEN the home fragment resumes THEN the gradient is hidden`() =
        runTest(testDispatcher) {
            val manager =
                buildStatusBarColorManager(
                    isTabStripEnabled = true,
                    appStore = buildAppStore(Wallpaper.EdgeToEdge),
                )
            manager.onFragmentResumed(fragmentManager, homeFragment)
            testScheduler.advanceUntilIdle()

            verify { tabStripStatusBarView.hide() }
            verify(exactly = 0) { tabStripStatusBarView.show() }
        }

    @Test
    fun `GIVEN tab strip is enabled WHEN a fragment without a tab strip resumes THEN the gradient is hidden`() =
        runTest(testDispatcher) {
            val manager = buildStatusBarColorManager(isTabStripEnabled = true)
            manager.onFragmentResumed(fragmentManager, mockk<Fragment>())

            verify {
                themeManager.applyStatusBarTheme(activity)
                tabStripStatusBarView.hide()
            }
        }

    @Test
    fun `GIVEN private browsing mode is enabled WHEN a tab strip fragment resumes THEN the gradient is hidden`() =
        runTest(testDispatcher) {
            every { themeManager.currentTheme } returns BrowsingMode.Private

            val manager = buildStatusBarColorManager(isTabStripEnabled = true)
            manager.onFragmentResumed(fragmentManager, homeFragment)

            verify {
                themeManager.applyStatusBarTheme(activity)
                tabStripStatusBarView.hide()
            }
        }

    @Test
    fun `GIVEN tab strip is disabled WHEN a tab strip fragment resumes THEN the gradient is hidden`() =
        runTest(testDispatcher) {
            val manager = buildStatusBarColorManager(isTabStripEnabled = false)
            manager.onFragmentResumed(fragmentManager, homeFragment)

            verify {
                themeManager.applyStatusBarTheme(activity)
                tabStripStatusBarView.hide()
            }
        }

    @Test
    fun `GIVEN NavHostFragment, DialogFragment and ExternalAppBrowserFragment as ignored fragments WHEN an ignored fragment resumes THEN do nothing`() =
        runTest(testDispatcher) {
            val manager = buildStatusBarColorManager(isTabStripEnabled = true)

            manager.onFragmentResumed(fragmentManager, mockk<NavHostFragment>())
            manager.onFragmentResumed(fragmentManager, mockk<DialogFragment>())
            manager.onFragmentResumed(fragmentManager, mockk<ExternalAppBrowserFragment>())

            verify(exactly = 0) {
                themeManager.applyStatusBarTheme(any())
                tabStripStatusBarView.show()
                tabStripStatusBarView.hide()
            }
        }

    private fun buildAppStore(currentWallpaper: Wallpaper, isSearchActive: Boolean = false) =
        AppStore(
            initialState =
                AppState(
                    wallpaperState =
                        WallpaperState(
                            currentWallpaper = currentWallpaper,
                            availableWallpapers = emptyList(),
                        ),
                    searchState =
                        SearchState.EMPTY.copy(
                            isSearchActive = isSearchActive,
                            sourceTabId = if (isSearchActive) "source-tab-id" else null,
                        ),
                )
        )

    private fun nonDefaultWallpaper() = Wallpaper.Default.copy(name = "amethyst")

    private fun buildStatusBarColorManager(
        isTabStripEnabled: Boolean,
        appStore: AppStore = AppStore(),
    ): StatusBarColorManager {
        every { settings.isTabStripEnabled } returns isTabStripEnabled

        return StatusBarColorManager(
            themeManager = themeManager,
            activity = activity,
            settings = settings,
            appStore = appStore,
            tabStripStatusBarView = tabStripStatusBarView,
            mainDispatcher = testDispatcher,
        )
    }
}
