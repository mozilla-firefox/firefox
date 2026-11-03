/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.browser

import android.content.Context
import android.content.pm.ApplicationInfo
import android.content.res.Resources
import android.os.Build
import android.view.View
import android.view.Window
import android.view.WindowManager
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.LifecycleRegistry
import androidx.navigation.NavController
import io.mockk.Runs
import io.mockk.every
import io.mockk.just
import io.mockk.mockk
import io.mockk.spyk
import io.mockk.verify
import kotlin.coroutines.ContinuationInterceptor
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.action.RestoreCompleteAction
import mozilla.components.browser.state.action.TabListAction
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.FenixApplication
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.R
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.components.toolbar.BrowserToolbarComposable
import org.mozilla.fenix.ext.application
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.utils.Settings
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class BrowserFragmentTest {

    private lateinit var store: BrowserStore
    private lateinit var testTab: TabSessionState
    private lateinit var browserFragment: BrowserFragment
    private lateinit var view: View
    private lateinit var homeActivity: HomeActivity
    private lateinit var fenixApplication: FenixApplication
    private lateinit var context: Context

    private lateinit var resources: Resources
    private lateinit var lifecycleOwner: MockedLifecycleOwner
    private lateinit var navController: NavController
    private lateinit var settings: Settings
    private lateinit var appStore: AppStore

    @Before
    fun setup() {
        context = spyk(testContext)
        resources = spyk(testContext.resources)
        every { context.resources } returns resources

        fenixApplication = mockk(relaxed = true)
        every { context.application } returns fenixApplication

        homeActivity = mockk(relaxed = true)
        every { homeActivity.resources } returns testContext.resources

        view = mockk(relaxed = true)
        lifecycleOwner = MockedLifecycleOwner(Lifecycle.State.STARTED)
        navController = mockk(relaxed = true)
        settings = mockk(relaxed = true)
        appStore = AppStore(initialState = AppState())

        browserFragment = spyk(BrowserFragment())
        every { browserFragment.view } returns view
        every { browserFragment.isAdded } returns true
        every { browserFragment.browserToolbar } returns mockk<BrowserToolbarComposable>(relaxed = true)
        every { browserFragment.childFragmentManager } returns mockk(relaxed = true)
        every { browserFragment.activity } returns homeActivity
        every { browserFragment.lifecycle } returns lifecycleOwner.lifecycle
        every { browserFragment.viewLifecycleOwner } returns lifecycleOwner
        every { context.components.settings } returns settings

        every { context.components.appStore } returns appStore
        every { browserFragment.requireContext() } returns context
        every { browserFragment.initializeUI(any(), any()) } returns mockk()
        every { browserFragment.fullScreenChanged(any()) } just Runs

        testTab = createTab(url = "https://mozilla.org")
        store = BrowserStore()
        every { context.components.core.store } returns store
    }

    @Test
    fun `GIVEN browser UI is not initialized WHEN selected tab changes THEN browser UI is initialized`() = runTest {
        browserFragment.observeTabSelection(
            store,
            coroutineContext[ContinuationInterceptor] as CoroutineDispatcher,
        )

        verify(exactly = 0) { browserFragment.initializeUI(view, testTab) }

        addAndSelectTab(testTab)
        testScheduler.advanceUntilIdle()

        verify(exactly = 1) { browserFragment.initializeUI(view, testTab) }
    }

    @Test
    fun `GIVEN browser UI is initialized WHEN selected tab changes THEN toolbar is expanded`() = runTest {
        browserFragment.browserInitialized = true
        browserFragment.observeTabSelection(
            store,
            coroutineContext[ContinuationInterceptor] as CoroutineDispatcher,
        )

        val toolbar: BrowserToolbarComposable = mockk(relaxed = true)
        every { browserFragment.browserToolbar } returns toolbar

        val newSelectedTab = createTab("https://firefox.com")
        addAndSelectTab(newSelectedTab)
        testScheduler.advanceUntilIdle()

        verify(exactly = 1) { toolbar.expand() }
    }

    @Test
    fun `GIVEN browser UI is initialized WHEN selected tab changes THEN full screen mode is exited`() = runTest {
        browserFragment.browserInitialized = true
        browserFragment.observeTabSelection(
            store,
            coroutineContext[ContinuationInterceptor] as CoroutineDispatcher,
        )

        val newSelectedTab = createTab("https://firefox.com")
        addAndSelectTab(newSelectedTab)
        testScheduler.advanceUntilIdle()

        verify(exactly = 1) { browserFragment.fullScreenChanged(false) }
    }

    @Test
    fun `GIVEN tabs are restored WHEN there are no tabs THEN navigate to home`() = runTest {
        store = BrowserStore(initialState = BrowserState(tabs = listOf(testTab)))
        every { context.components.core.store } returns store

        browserFragment.observeRestoreComplete(
            store,
            navController,
            coroutineContext[ContinuationInterceptor] as CoroutineDispatcher,
        )
        store.dispatch(RestoreCompleteAction)
        testScheduler.advanceUntilIdle()

        verify(exactly = 1) { navController.popBackStack(R.id.homeFragment, false) }
    }

    @Test
    fun `GIVEN tabs are restored WHEN there are tabs THEN do not navigate`() = runTest {
        addAndSelectTab(testTab)
        browserFragment.observeRestoreComplete(
            store,
            navController,
            coroutineContext[ContinuationInterceptor] as CoroutineDispatcher,
        )
        store.dispatch(RestoreCompleteAction)
        testScheduler.advanceUntilIdle()

        verify(exactly = 0) { navController.popBackStack(R.id.homeFragment, false) }
    }

    @Test
    fun `GIVEN tabs are restored WHEN there is no selected tab THEN navigate to home`() = runTest {
        store = BrowserStore(initialState = BrowserState(tabs = listOf(testTab)))
        every { context.components.core.store } returns store

        browserFragment.observeRestoreComplete(
            store,
            navController,
            coroutineContext[ContinuationInterceptor] as CoroutineDispatcher,
        )
        store.dispatch(RestoreCompleteAction)
        testScheduler.advanceUntilIdle()

        verify(exactly = 1) { navController.popBackStack(R.id.homeFragment, false) }
    }

    @Test
    fun `WHEN isPullToRefreshEnabledInBrowser is disabled THEN pull down refresh is disabled`() {
        every { context.components.settings.isPullToRefreshEnabledInBrowser } returns true
        assertTrue(browserFragment.shouldPullToRefreshBeEnabled())

        every { context.components.settings.isPullToRefreshEnabledInBrowser } returns false
        assertTrue(!browserFragment.shouldPullToRefreshBeEnabled())
    }

    private fun addAndSelectTab(tab: TabSessionState) {
        store.dispatch(TabListAction.AddTabAction(tab))
        store.dispatch(TabListAction.SelectTabAction(tab.id))
    }

    internal class MockedLifecycleOwner(initialState: Lifecycle.State) : LifecycleOwner {
        override val lifecycle: Lifecycle =
            LifecycleRegistry(this).apply {
                currentState = initialState
            }
    }

    @Test
    fun `WHEN updating the last browse activity THEN update the associated preference`() {
        val settings: Settings = mockk(relaxed = true)

        every { browserFragment.context } returns context
        every { context.components.settings } returns settings

        browserFragment.updateLastBrowseActivity()

        verify(exactly = 1) { settings.recordLastBrowseActivity() }
    }

    @Test
    fun `WHEN the viewport fit changes THEN the cutout mode is applied to the window`() {
        val layoutParams = WindowManager.LayoutParams()
        val window: Window = mockk(relaxed = true)
        every { window.attributes } returns layoutParams
        every { homeActivity.window } returns window
        every { homeActivity.applicationInfo } returns
            ApplicationInfo().apply { targetSdkVersion = Build.VERSION_CODES.UPSIDE_DOWN_CAKE }

        browserFragment.viewportFitChange(WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES)

        assertEquals(
            WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES,
            layoutParams.layoutInDisplayCutoutMode,
        )
        // Mutating the params is not enough, they only reach WindowManager through setAttributes.
        verify(exactly = 1) { window.attributes = layoutParams }
    }
}
