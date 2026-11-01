/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray.ui

import android.content.Context
import androidx.navigation.NavController
import io.mockk.Runs
import io.mockk.every
import io.mockk.just
import io.mockk.mockk
import io.mockk.spyk
import io.mockk.verify
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.NavGraphDirections
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.helpers.ExceptionAssertions.assertNoUncaughtException
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.mozilla.fenix.helpers.MockkRetryTestRule
import org.mozilla.fenix.home.HomeScreenViewModel
import org.mozilla.fenix.navigation.NavControllerProvider
import org.mozilla.fenix.tabstray.data.createTab
import org.mozilla.fenix.tabstray.redux.state.Page
import org.mozilla.fenix.tabstray.redux.state.TabsTrayState
import org.mozilla.fenix.utils.Settings
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class TabManagementFragmentTest {
    private lateinit var context: Context
    private lateinit var fragment: TabManagementFragment

    @get:Rule val mockkRule = MockkRetryTestRule()

    @get:Rule val gleanTestRule = FenixGleanTestRule(testContext)

    @Before
    fun setup() {
        context = mockk(relaxed = true)

        fragment = spyk(TabManagementFragment())
        every { fragment.context } returns context
        every { fragment.viewLifecycleOwner } returns mockk(relaxed = true)
        every { fragment.view } returns mockk()
    }

    @Test
    fun `WHEN navigateToHomeAndDeleteSession is called with a sessionId THEN it navigates to home and transmits there the sessionId`() {
        val viewModel: HomeScreenViewModel = mockk(relaxed = true)
        every { fragment.homeViewModel } returns viewModel
        val navController: NavController = mockk(relaxed = true)

        val navControllerProvider: NavControllerProvider = mockk()
        every { navControllerProvider.getNavController(fragment) } returns navController

        fragment.navigateToHomeAndDeleteSession(
            "test",
            navControllerProvider = navControllerProvider,
        )

        verify { viewModel.sessionToDelete = "test" }
        verify { navController.navigate(NavGraphDirections.actionGlobalHome()) }
    }

    @Test
    fun `WHEN dismissTabManager is called THEN it dismisses the tab manager`() {
        val navController: NavController = mockk(relaxed = true)
        every { fragment.recordBreadcrumb(any()) } just Runs

        fragment.dismissTabManager(navController = navController)

        verify { navController.popBackStack() }
    }

    @Test
    fun `WHEN all conditions are met THEN shouldShowLockPbmBanner returns true`() {
        val result = testShouldShowLockPbmBanner()
        assertTrue(result)
    }

    @Test
    fun `WHEN isPrivateMode is false THEN shouldShowLockPbmBanner returns false`() {
        val result = testShouldShowLockPbmBanner(isPrivateMode = false)
        assertFalse(result)
    }

    @Test
    fun `WHEN hasPrivateTabs is false THEN shouldShowLockPbmBanner returns false`() {
        val result = testShouldShowLockPbmBanner(hasPrivateTabs = false)
        assertFalse(result)
    }

    @Test
    fun `WHEN biometricAvailable is false THEN shouldShowLockPbmBanner returns false`() {
        val result = testShouldShowLockPbmBanner(biometricAvailable = false)
        assertFalse(result)
    }

    @Test
    fun `WHEN privateLockEnabled is true THEN shouldShowLockPbmBanner returns false`() {
        val result = testShouldShowLockPbmBanner(privateLockEnabled = true)
        assertFalse(result)
    }

    @Test
    fun `WHEN shouldShowBanner is false THEN shouldShowLockPbmBanner returns false`() {
        val result = testShouldShowLockPbmBanner(shouldShowBanner = false)
        assertFalse(result)
    }

    @Test
    fun `GIVEN transition animation setting is disabled THEN tabManagerOpeningAnimationEnabled returns false`() {
        val mockSettings = mockk<Settings>()
        every { mockSettings.tabManagerOpeningAnimationEnabled } returns false
        every { context.components.settings } returns mockSettings
        assertFalse(
            fragment.shouldPerformTransitionAnimation(
                selectedPage = Page.NormalTabs,
                mode = TabsTrayState.Mode.Normal,
                tabState = fakeTab(isPrivate = false),
            )
        )
    }

    @Test
    fun `GIVEN TabTray mode is multi-select THEN tabManagerOpeningAnimationEnabled returns false`() {
        val mockSettings = mockk<Settings>()
        every { mockSettings.tabManagerOpeningAnimationEnabled } returns true
        every { context.components.settings } returns mockSettings
        assertFalse(
            fragment.shouldPerformTransitionAnimation(
                selectedPage = Page.NormalTabs,
                mode = TabsTrayState.Mode.Select(setOf(fakeTab(isPrivate = false))),
                tabState = fakeTab(isPrivate = false),
            )
        )
    }

    @Test
    fun `GIVEN transition to normal tab on normal TabsTray page THEN tabManagerOpeningAnimationEnabled returns true`() {
        val mockSettings = mockk<Settings>()
        every { mockSettings.tabManagerOpeningAnimationEnabled } returns true
        every { context.components.settings } returns mockSettings
        assertTrue(
            fragment.shouldPerformTransitionAnimation(
                selectedPage = Page.NormalTabs,
                mode = TabsTrayState.Mode.Normal,
                tabState = fakeTab(isPrivate = false),
            )
        )
    }

    @Test
    fun `GIVEN transition to private tab on private TabsTray page THEN tabManagerOpeningAnimationEnabled returns true`() {
        val mockSettings = mockk<Settings>()
        every { mockSettings.tabManagerOpeningAnimationEnabled } returns true
        every { context.components.settings } returns mockSettings
        assertTrue(
            fragment.shouldPerformTransitionAnimation(
                selectedPage = Page.PrivateTabs,
                mode = TabsTrayState.Mode.Normal,
                tabState = fakeTab(isPrivate = true),
            )
        )
    }

    @Test
    fun `GIVEN transition to private tab on normal TabsTray page THEN tabManagerOpeningAnimationEnabled returns false`() {
        val mockSettings = mockk<Settings>()
        every { mockSettings.tabManagerOpeningAnimationEnabled } returns true
        every { context.components.settings } returns mockSettings
        assertFalse(
            fragment.shouldPerformTransitionAnimation(
                selectedPage = Page.NormalTabs,
                mode = TabsTrayState.Mode.Normal,
                tabState = fakeTab(isPrivate = true),
            )
        )
    }

    @Test
    fun `GIVEN transition to normal tab on private TabsTray page THEN tabManagerOpeningAnimationEnabled returns false`() {
        val mockSettings = mockk<Settings>()
        every { mockSettings.tabManagerOpeningAnimationEnabled } returns true
        every { context.components.settings } returns mockSettings
        assertFalse(
            fragment.shouldPerformTransitionAnimation(
                selectedPage = Page.PrivateTabs,
                mode = TabsTrayState.Mode.Normal,
                tabState = fakeTab(isPrivate = false),
            )
        )
    }

    @Test
    fun `GIVEN transition to normal tab on synced TabsTray page THEN tabManagerOpeningAnimationEnabled returns false`() {
        val mockSettings = mockk<Settings>()
        every { mockSettings.tabManagerOpeningAnimationEnabled } returns true
        every { context.components.settings } returns mockSettings
        assertFalse(
            fragment.shouldPerformTransitionAnimation(
                selectedPage = Page.SyncedTabs,
                mode = TabsTrayState.Mode.Normal,
                tabState = fakeTab(isPrivate = false),
            )
        )
    }

    @Test
    fun `GIVEN transition to private tab on synced TabsTray page THEN tabManagerOpeningAnimationEnabled returns false`() {
        val mockSettings = mockk<Settings>()
        every { mockSettings.tabManagerOpeningAnimationEnabled } returns true
        every { context.components.settings } returns mockSettings
        assertFalse(
            fragment.shouldPerformTransitionAnimation(
                selectedPage = Page.SyncedTabs,
                mode = TabsTrayState.Mode.Normal,
                tabState = fakeTab(isPrivate = true),
            )
        )
    }

    @Test
    fun `GIVEN fragment is detached WHEN bookmark SnackBar is invoked THEN a crash does not result`() {
        assertNoUncaughtException {
            TabManagementFragment().showBookmarkSnackbar(tabSize = 1, parentFolderTitle = null)
        }
    }

    @Test
    fun `GIVEN fragment is detached WHEN synced tab undo SnackBar is invoked THEN a crash does not result`() {
        assertNoUncaughtException {
            TabManagementFragment().showUndoSnackbarForSyncedTab {}
        }
    }

    private fun fakeTab(isPrivate: Boolean) =
        createTab(
            url = "www.mozilla.org",
            private = isPrivate,
        )

    private fun testShouldShowLockPbmBanner(
        isPrivateMode: Boolean = true,
        hasPrivateTabs: Boolean = true,
        biometricAvailable: Boolean = true,
        privateLockEnabled: Boolean = false,
        shouldShowBanner: Boolean = true,
    ): Boolean {
        return fragment.shouldShowLockPbmBanner(
            isPrivateMode = isPrivateMode,
            hasPrivateTabs = hasPrivateTabs,
            biometricAvailable = biometricAvailable,
            privateLockEnabled = privateLockEnabled,
            shouldShowBanner = shouldShowBanner,
        )
    }
}
