/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.bindings

import kotlin.test.assertFalse
import kotlin.test.assertTrue
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.action.TabListAction
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import org.junit.Test
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.components.appstate.SupportedMenuNotifications

class NormalTabToolbarMenuHighlightBindingTest {
    private val appStore = AppStore()
    private val highlight = MutableStateFlow(false)

    private val hasMenuNotification
        get() = NOTIFICATION in appStore.state.supportedMenuNotifications

    @Test
    fun `GIVEN a normal tab is selected WHEN the toolbar should be highlighted THEN add the menu notification`() =
        runTest {
            startBinding(browserStore(selectedTabId = NORMAL_TAB_ID))
            assertFalse(hasMenuNotification)

            highlight.value = true
            testScheduler.advanceUntilIdle()

            assertTrue(hasMenuNotification)
        }

    @Test
    fun `GIVEN the toolbar is highlighted WHEN it should no longer be THEN remove the menu notification`() = runTest {
        highlight.value = true
        startBinding(browserStore(selectedTabId = NORMAL_TAB_ID))
        assertTrue(hasMenuNotification)

        highlight.value = false
        testScheduler.advanceUntilIdle()

        assertFalse(hasMenuNotification)
    }

    @Test
    fun `GIVEN a private tab is selected WHEN the toolbar should be highlighted THEN don't add the menu notification`() =
        runTest {
            highlight.value = true
            startBinding(browserStore(selectedTabId = PRIVATE_TAB_ID))

            assertFalse(hasMenuNotification)
        }

    @Test
    fun `GIVEN the toolbar is highlighted WHEN switching between normal and private tabs THEN add or remove the menu notification`() =
        runTest {
            highlight.value = true
            val browserStore = browserStore(selectedTabId = NORMAL_TAB_ID)
            startBinding(browserStore)
            assertTrue(hasMenuNotification)

            browserStore.dispatch(TabListAction.SelectTabAction(PRIVATE_TAB_ID))
            testScheduler.advanceUntilIdle()
            assertFalse(hasMenuNotification)

            browserStore.dispatch(TabListAction.SelectTabAction(NORMAL_TAB_ID))
            testScheduler.advanceUntilIdle()
            assertTrue(hasMenuNotification)
        }

    @Test
    fun `GIVEN no tab is selected WHEN the toolbar should be highlighted THEN don't add the menu notification`() =
        runTest {
            highlight.value = true
            startBinding(browserStore(selectedTabId = null))

            assertFalse(hasMenuNotification)
        }

    @Test
    fun `GIVEN another menu notification is set WHEN adding and removing this notification THEN keep the other one`() =
        runTest {
            val appStore = AppStore(AppState(supportedMenuNotifications = setOf(OTHER_NOTIFICATION)))
            startBinding(browserStore(selectedTabId = NORMAL_TAB_ID), appStore = appStore)

            highlight.value = true
            testScheduler.advanceUntilIdle()
            assertTrue(OTHER_NOTIFICATION in appStore.state.supportedMenuNotifications)

            highlight.value = false
            testScheduler.advanceUntilIdle()
            assertTrue(OTHER_NOTIFICATION in appStore.state.supportedMenuNotifications)
        }

    private fun TestScope.startBinding(
        browserStore: BrowserStore,
        appStore: AppStore = this@NormalTabToolbarMenuHighlightBindingTest.appStore,
    ) {
        NormalTabToolbarMenuHighlightBinding(
                appStore = appStore,
                notification = NOTIFICATION,
                highlight = highlight,
                browserStore = browserStore,
                mainDispatcher = StandardTestDispatcher(testScheduler),
            )
            .start()
        testScheduler.advanceUntilIdle()
    }

    private fun browserStore(selectedTabId: String?) =
        BrowserStore(
            BrowserState(
                tabs =
                    listOf(
                        createTab(url = "https://www.mozilla.org", id = NORMAL_TAB_ID),
                        createTab(url = "https://www.mozilla.org", id = PRIVATE_TAB_ID, private = true),
                    ),
                selectedTabId = selectedTabId,
            )
        )

    private companion object {
        const val NORMAL_TAB_ID = "normal"
        const val PRIVATE_TAB_ID = "private"
        val NOTIFICATION = SupportedMenuNotifications.TabGroups
        val OTHER_NOTIFICATION = SupportedMenuNotifications.Downloads
    }
}
