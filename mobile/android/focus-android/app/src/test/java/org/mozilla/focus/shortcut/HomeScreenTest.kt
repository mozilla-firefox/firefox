/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
package org.mozilla.focus.shortcut

import android.content.Context
import android.content.pm.ShortcutManager
import junit.framework.TestCase.assertEquals
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.support.test.robolectric.testContext
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mock
import org.mockito.Mockito.never
import org.mockito.Mockito.spy
import org.mockito.Mockito.verify
import org.mockito.Mockito.`when`
import org.mockito.MockitoAnnotations
import org.mozilla.focus.Components
import org.mozilla.focus.FocusApplication
import org.mozilla.focus.shortcut.HomeScreen.generateTitleFromUrl
import org.mozilla.focus.state.AppAction
import org.mozilla.focus.state.AppState
import org.mozilla.focus.state.AppStore
import org.mozilla.focus.state.Screen
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class HomeScreenTest {
    private lateinit var context: Context

    @Mock private lateinit var applicationContext: FocusApplication

    @Mock private lateinit var components: Components

    @Mock private lateinit var appStore: AppStore

    @Mock private lateinit var shortcutManager: ShortcutManager

    private val testDispatcher = StandardTestDispatcher()

    @Before
    fun setUp() {
        context = spy(testContext)
        MockitoAnnotations.openMocks(this)
        `when`(context.getSystemService(ShortcutManager::class.java)).thenReturn(shortcutManager)

        `when`(context.applicationContext).thenReturn(applicationContext)
        `when`(applicationContext.components).thenReturn(components)

        `when`(components.appStore).thenReturn(appStore)
    }

    @Test
    fun testGenerateTitleFromUrl() {
        assertEquals("mozilla.org", generateTitleFromUrl("https://www.mozilla.org"))
        assertEquals("facebook.com", generateTitleFromUrl("http://m.facebook.com/home"))
        assertEquals("", generateTitleFromUrl("mozilla"))
    }

    @Test
    fun `test checkIfPinningSupported when state is null`() =
        runTest(testDispatcher) {
            val appState = AppState(Screen.Home)
            `when`(appStore.state).thenReturn(appState)

            `when`(shortcutManager.isRequestPinShortcutSupported).thenReturn(true)

            HomeScreen.checkIfPinningSupported(
                context,
                scope = this,
                ioDispatcher = testDispatcher,
            )
            testDispatcher.scheduler.advanceUntilIdle()

            verify(shortcutManager).isRequestPinShortcutSupported

            verify(appStore).dispatch(AppAction.UpdateIsPinningSupported(true))
        }

    @Test
    fun `test checkIfPinningSupported when state is not null`() =
        runTest(testDispatcher) {
            val appState = AppState(Screen.Home, isPinningSupported = true)
            `when`(appStore.state).thenReturn(appState)

            HomeScreen.checkIfPinningSupported(
                context,
                scope = this,
                ioDispatcher = testDispatcher,
            )

            testDispatcher.scheduler.advanceUntilIdle()

            verify(shortcutManager, never()).isRequestPinShortcutSupported
            verify(appStore, never()).dispatch(AppAction.UpdateIsPinningSupported(true))
        }
}
