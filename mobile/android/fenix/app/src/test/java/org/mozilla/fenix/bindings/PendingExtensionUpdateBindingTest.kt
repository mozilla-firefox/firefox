/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.bindings

import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.action.WebExtensionAction
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.WebExtensionState
import mozilla.components.browser.state.state.extension.PendingExtensionUpdate
import mozilla.components.browser.state.store.BrowserStore
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.SupportedMenuNotifications

class PendingExtensionUpdateBindingTest {
    private val appStore = AppStore()

    private val hasMenuNotification
        get() = SupportedMenuNotifications.ExtensionUpdate in appStore.state.supportedMenuNotifications

    @Test
    fun `WHEN an update of an enabled extension becomes pending and is then removed THEN the menu notification is added and removed`() =
        runTest {
            val browserStore = browserStore(enabled = true)
            PendingExtensionUpdateBinding(appStore, browserStore, StandardTestDispatcher(testScheduler)).start()
            testScheduler.advanceUntilIdle()
            assertFalse(hasMenuNotification)

            browserStore.dispatch(WebExtensionAction.UpdatePendingExtensionUpdateAction(pendingExtensionUpdate()))
            testScheduler.advanceUntilIdle()
            assertTrue(hasMenuNotification)

            browserStore.dispatch(WebExtensionAction.RemovePendingExtensionUpdateAction("id"))
            testScheduler.advanceUntilIdle()
            assertFalse(hasMenuNotification)
        }

    @Test
    fun `WHEN an update of a disabled extension becomes pending THEN no menu notification is added`() = runTest {
        val browserStore = browserStore(enabled = false)
        PendingExtensionUpdateBinding(appStore, browserStore, StandardTestDispatcher(testScheduler)).start()
        testScheduler.advanceUntilIdle()

        browserStore.dispatch(WebExtensionAction.UpdatePendingExtensionUpdateAction(pendingExtensionUpdate()))
        testScheduler.advanceUntilIdle()

        assertFalse(hasMenuNotification)
    }

    @Test
    fun `WHEN an extension with a pending update is disabled THEN the menu notification is removed`() = runTest {
        val browserStore = browserStore(enabled = true)
        browserStore.dispatch(WebExtensionAction.UpdatePendingExtensionUpdateAction(pendingExtensionUpdate()))
        PendingExtensionUpdateBinding(appStore, browserStore, StandardTestDispatcher(testScheduler)).start()
        testScheduler.advanceUntilIdle()
        assertTrue(hasMenuNotification)

        browserStore.dispatch(WebExtensionAction.UpdateWebExtensionEnabledAction("id", enabled = false))
        testScheduler.advanceUntilIdle()
        assertFalse(hasMenuNotification)
    }

    @Test
    fun `WHEN a disabled extension with a pending update is enabled THEN the menu notification is added`() = runTest {
        val browserStore = browserStore(enabled = false)
        browserStore.dispatch(WebExtensionAction.UpdatePendingExtensionUpdateAction(pendingExtensionUpdate()))
        PendingExtensionUpdateBinding(appStore, browserStore, StandardTestDispatcher(testScheduler)).start()
        testScheduler.advanceUntilIdle()
        assertFalse(hasMenuNotification)

        browserStore.dispatch(WebExtensionAction.UpdateWebExtensionEnabledAction("id", enabled = true))
        testScheduler.advanceUntilIdle()
        assertTrue(hasMenuNotification)
    }

    private fun browserStore(enabled: Boolean) =
        BrowserStore(BrowserState(extensions = mapOf("id" to WebExtensionState("id", enabled = enabled))))

    private fun pendingExtensionUpdate() =
        PendingExtensionUpdate(
            extensionId = "id",
            permissions = listOf("privacy"),
            origins = emptyList(),
            dataCollectionPermissions = emptyList(),
            onPermissionsGranted = {},
        )
}
