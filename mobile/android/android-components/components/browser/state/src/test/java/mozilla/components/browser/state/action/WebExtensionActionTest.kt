/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.state.action

import kotlin.test.assertNotNull
import mozilla.components.browser.state.reducer.BrowserStateReducer
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.WebExtensionState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.state.extension.PendingExtensionUpdate
import mozilla.components.browser.state.state.extension.WebExtensionPromptRequest
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.webextension.WebExtensionBrowserAction
import mozilla.components.concept.engine.webextension.WebExtensionPageAction
import mozilla.components.support.test.mock
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test

class WebExtensionActionTest {

    @Test
    fun `InstallWebExtension - Adds an extension to the BrowserState extensions`() {
        var state = BrowserState()

        assertTrue(state.extensions.isEmpty())

        val extension = WebExtensionState("id", "url")
        state = BrowserStateReducer.reduce(state, WebExtensionAction.InstallWebExtensionAction(extension))

        assertFalse(state.extensions.isEmpty())
        assertEquals(extension, state.extensions.values.first())

        // Installing the same extension twice should have no effect
        val oldState = state
        state = BrowserStateReducer.reduce(state, WebExtensionAction.InstallWebExtensionAction(extension))
        assertEquals(oldState, state)
    }

    @Test
    fun `InstallWebExtension - Keeps existing browser and page actions`() {
        var state = BrowserState()
        assertTrue(state.extensions.isEmpty())

        val extension = WebExtensionState("id", "url", "name")
        val mockedBrowserAction = mock<WebExtensionBrowserAction>()
        val mockedPageAction = mock<WebExtensionPageAction>()
        state =
            BrowserStateReducer.reduce(state, WebExtensionAction.UpdateBrowserAction(extension.id, mockedBrowserAction))
        state = BrowserStateReducer.reduce(state, WebExtensionAction.UpdatePageAction(extension.id, mockedPageAction))
        state = BrowserStateReducer.reduce(state, WebExtensionAction.InstallWebExtensionAction(extension))

        assertFalse(state.extensions.isEmpty())
        assertEquals(
            extension.copy(
                browserAction = mockedBrowserAction,
                pageAction = mockedPageAction,
            ),
            state.extensions.values.first(),
        )
    }

    @Test
    fun `UninstallWebExtension - Removes all state of the uninstalled extension`() {
        val tab1 = createTab("url")
        val tab2 = createTab("url")
        var state = BrowserState(tabs = listOf(tab1, tab2))

        assertTrue(state.extensions.isEmpty())

        val extension1 = WebExtensionState("id1", "url")
        val extension2 = WebExtensionState("i2", "url")
        state = BrowserStateReducer.reduce(state, WebExtensionAction.InstallWebExtensionAction(extension1))

        assertFalse(state.extensions.isEmpty())
        assertEquals(extension1, state.extensions.values.first())

        val mockedBrowserAction = mock<WebExtensionBrowserAction>()
        state =
            BrowserStateReducer.reduce(
                state,
                WebExtensionAction.UpdateBrowserAction(extension1.id, mockedBrowserAction),
            )
        assertEquals(mockedBrowserAction, state.extensions.values.first().browserAction)

        state =
            BrowserStateReducer.reduce(
                state,
                WebExtensionAction.UpdateTabBrowserAction(tab1.id, extension1.id, mockedBrowserAction),
            )
        val extensionsTab1 = state.tabs.first().extensionState
        assertEquals(mockedBrowserAction, extensionsTab1.values.first().browserAction)

        state =
            BrowserStateReducer.reduce(
                state,
                WebExtensionAction.UpdateTabBrowserAction(tab2.id, extension2.id, mockedBrowserAction),
            )

        state = BrowserStateReducer.reduce(state, WebExtensionAction.UninstallWebExtensionAction(extension1.id))
        assertTrue(state.extensions.isEmpty())
        assertTrue(state.tabs.first().extensionState.isEmpty())
        assertFalse(state.tabs.last().extensionState.isEmpty())
        assertEquals(mockedBrowserAction, state.tabs.last().extensionState.values.last().browserAction)
    }

    @Test
    fun `UninstallAllWebExtensions - Removes all state of all extensions`() {
        val tab1 = createTab("url")
        val tab2 = createTab("url")
        var state = BrowserState(tabs = listOf(tab1, tab2))
        assertTrue(state.extensions.isEmpty())

        val extension1 = WebExtensionState("id1", "url")
        val extension2 = WebExtensionState("i2", "url")
        state = BrowserStateReducer.reduce(state, WebExtensionAction.InstallWebExtensionAction(extension1))
        state = BrowserStateReducer.reduce(state, WebExtensionAction.InstallWebExtensionAction(extension2))
        assertEquals(2, state.extensions.size)

        val mockedBrowserAction = mock<WebExtensionBrowserAction>()
        state =
            BrowserStateReducer.reduce(
                state,
                WebExtensionAction.UpdateBrowserAction(extension1.id, mockedBrowserAction),
            )
        assertEquals(mockedBrowserAction, state.extensions["id1"]?.browserAction)
        state =
            BrowserStateReducer.reduce(
                state,
                WebExtensionAction.UpdateTabBrowserAction(tab1.id, extension1.id, mockedBrowserAction),
            )
        state =
            BrowserStateReducer.reduce(
                state,
                WebExtensionAction.UpdateTabBrowserAction(tab2.id, extension2.id, mockedBrowserAction),
            )

        state = BrowserStateReducer.reduce(state, WebExtensionAction.UninstallAllWebExtensionsAction)
        assertTrue(state.extensions.isEmpty())
        assertTrue(state.tabs.first().extensionState.isEmpty())
        assertTrue(state.tabs.last().extensionState.isEmpty())
    }

    @Test
    fun `UpdateBrowserAction - Updates a global browser action of an existing WebExtensionState on the BrowserState`() {
        var state = BrowserState()
        val mockedBrowserAction = mock<WebExtensionBrowserAction>()
        val mockedBrowserAction2 = mock<WebExtensionBrowserAction>()

        assertTrue(state.extensions.isEmpty())
        state = BrowserStateReducer.reduce(state, WebExtensionAction.UpdateBrowserAction("id", mockedBrowserAction))
        assertEquals(mockedBrowserAction, state.extensions.values.first().browserAction)

        val extension = WebExtensionState("id", "url")
        state = BrowserStateReducer.reduce(state, WebExtensionAction.InstallWebExtensionAction(extension))
        assertFalse(state.extensions.isEmpty())
        assertEquals(mockedBrowserAction, state.extensions.values.first().browserAction)

        state = BrowserStateReducer.reduce(state, WebExtensionAction.UpdateBrowserAction("id", mockedBrowserAction2))
        assertEquals(mockedBrowserAction2, state.extensions.values.first().browserAction)
    }

    @Test
    fun `UpdateTabBrowserAction - Updates the browser action of an existing WebExtensionState on a given tab`() {
        val tab = createTab("url")
        var state = BrowserState(tabs = listOf(tab))
        val mockedBrowserAction = mock<WebExtensionBrowserAction>()

        assertTrue(tab.extensionState.isEmpty())

        val extension = WebExtensionState("id", "url")

        state =
            BrowserStateReducer.reduce(
                state,
                WebExtensionAction.UpdateTabBrowserAction(
                    tab.id,
                    extension.id,
                    mockedBrowserAction,
                ),
            )

        val extensions = state.tabs.first().extensionState

        assertEquals(mockedBrowserAction, extensions.values.first().browserAction)
    }

    @Test
    fun `UpdateTabBrowserAction - Updates an existing browser action`() {
        val mockedBrowserAction1 = mock<WebExtensionBrowserAction>()
        val mockedBrowserAction2 = mock<WebExtensionBrowserAction>()

        val tab =
            createTab(
                "url",
                extensions =
                    mapOf(
                        "extensionId" to
                            WebExtensionState(
                                "extensionId",
                                "url",
                                "name",
                                true,
                                browserAction = mockedBrowserAction1,
                            )
                    ),
            )
        var state = BrowserState(tabs = listOf(tab))

        state =
            BrowserStateReducer.reduce(
                state,
                WebExtensionAction.UpdateTabBrowserAction(
                    tab.id,
                    "extensionId",
                    mockedBrowserAction2,
                ),
            )

        val extensions = state.tabs.first().extensionState

        assertEquals(mockedBrowserAction2, extensions.values.first().browserAction)
    }

    @Test
    fun `UpdatePageAction - Updates a global page action of an existing WebExtensionState on the BrowserState`() {
        var state = BrowserState()
        val mockedPageAction = mock<WebExtensionPageAction>()
        val mockedPageAction2 = mock<WebExtensionPageAction>()

        assertTrue(state.extensions.isEmpty())
        state = BrowserStateReducer.reduce(state, WebExtensionAction.UpdatePageAction("id", mockedPageAction))
        assertEquals(mockedPageAction, state.extensions.values.first().pageAction)

        val extension = WebExtensionState("id", "url")
        state = BrowserStateReducer.reduce(state, WebExtensionAction.InstallWebExtensionAction(extension))
        assertFalse(state.extensions.isEmpty())
        assertEquals(mockedPageAction, state.extensions.values.first().pageAction)

        state = BrowserStateReducer.reduce(state, WebExtensionAction.UpdatePageAction("id", mockedPageAction2))
        assertEquals(mockedPageAction2, state.extensions.values.first().pageAction)
    }

    @Test
    fun `UpdateTabPageAction - Updates the page action of an existing WebExtensionState on a given tab`() {
        val tab = createTab("url")
        var state = BrowserState(tabs = listOf(tab))
        val mockedPageAction = mock<WebExtensionPageAction>()

        assertTrue(tab.extensionState.isEmpty())

        val extension = WebExtensionState("id", "url")

        state =
            BrowserStateReducer.reduce(
                state,
                WebExtensionAction.UpdateTabPageAction(
                    tab.id,
                    extension.id,
                    mockedPageAction,
                ),
            )

        val extensions = state.tabs.first().extensionState

        assertEquals(mockedPageAction, extensions.values.first().pageAction)
    }

    @Test
    fun `UpdateTabPageAction - Updates an existing page action`() {
        val mockedPageAction1 = mock<WebExtensionPageAction>()
        val mockedPageAction2 = mock<WebExtensionPageAction>()

        val tab =
            createTab(
                "url",
                extensions =
                    mapOf(
                        "extensionId" to
                            WebExtensionState(
                                "extensionId",
                                "url",
                                "name",
                                true,
                                pageAction = mockedPageAction1,
                            )
                    ),
            )
        var state = BrowserState(tabs = listOf(tab))

        state =
            BrowserStateReducer.reduce(
                state,
                WebExtensionAction.UpdateTabPageAction(
                    tab.id,
                    "extensionId",
                    mockedPageAction2,
                ),
            )

        val extensions = state.tabs.first().extensionState

        assertEquals(mockedPageAction2, extensions.values.first().pageAction)
    }

    @Test
    fun `UpdatePopupSessionAction - Adds popup session to the web extension state`() {
        var state = BrowserState()

        val extension = WebExtensionState("id", "url")
        state = BrowserStateReducer.reduce(state, WebExtensionAction.InstallWebExtensionAction(extension))

        assertEquals(extension, state.extensions[extension.id])
        assertNull(state.extensions[extension.id]?.popupSessionId)
        assertNull(state.extensions[extension.id]?.popupSession)

        val engineSession: EngineSession = mock()
        state =
            BrowserStateReducer.reduce(
                state,
                WebExtensionAction.UpdatePopupSessionAction(extension.id, popupSession = engineSession),
            )
        assertEquals(engineSession, state.extensions[extension.id]?.popupSession)

        state =
            BrowserStateReducer.reduce(
                state,
                WebExtensionAction.UpdatePopupSessionAction(extension.id, popupSession = null),
            )
        assertNull(state.extensions[extension.id]?.popupSession)

        state = BrowserStateReducer.reduce(state, WebExtensionAction.UpdatePopupSessionAction(extension.id, "popupId"))
        assertEquals("popupId", state.extensions[extension.id]?.popupSessionId)

        state = BrowserStateReducer.reduce(state, WebExtensionAction.UpdatePopupSessionAction(extension.id, null))
        assertNull(state.extensions[extension.id]?.popupSessionId)
    }

    @Test
    fun `UpdateOptionsPageSessionAction - Adds options page session to the web extension state`() {
        var state = BrowserState()
        val extension = WebExtensionState("id")
        state = BrowserStateReducer.reduce(state, WebExtensionAction.InstallWebExtensionAction(extension))

        assertEquals(extension, state.extensions[extension.id])
        assertNull(state.extensions[extension.id]?.activeOptionsPage)

        state =
            BrowserStateReducer.reduce(
                state,
                WebExtensionAction.UpdateOptionsPageSessionAction(
                    extension.id,
                    "testSessionId",
                    "testUrl",
                    "testTitle",
                ),
            )
        assertEquals("testSessionId", state.extensions[extension.id]?.activeOptionsPage?.instanceId)
        assertEquals("testUrl", state.extensions[extension.id]?.activeOptionsPage?.url)
        assertEquals("testTitle", state.extensions[extension.id]?.activeOptionsPage?.name)
    }

    @Test
    fun `UpdateOptionsPageSessionAction - Ignored when another options page session is already active`() {
        val extensionA = WebExtensionState("idA")
        val extensionB = WebExtensionState("idB")
        var state = BrowserState(extensions = mapOf(extensionA.id to extensionA, extensionB.id to extensionB))

        state =
            BrowserStateReducer.reduce(
                state,
                WebExtensionAction.UpdateOptionsPageSessionAction(
                    extensionA.id,
                    "instanceIdA",
                    "urlA",
                    "nameA",
                ),
            )
        assertEquals("instanceIdA", state.extensions[extensionA.id]?.activeOptionsPage?.instanceId)
        assertEquals("urlA", state.extensions[extensionA.id]?.activeOptionsPage?.url)
        assertNull(state.extensions[extensionB.id]?.activeOptionsPage)

        state =
            BrowserStateReducer.reduce(
                state,
                WebExtensionAction.UpdateOptionsPageSessionAction(
                    extensionB.id,
                    "instanceIdB",
                    "urlB",
                    "nameB",
                ),
            )
        assertEquals("instanceIdA", state.extensions[extensionA.id]?.activeOptionsPage?.instanceId)
        assertEquals("urlA", state.extensions[extensionA.id]?.activeOptionsPage?.url)
        assertNull(state.extensions[extensionB.id]?.activeOptionsPage)
    }

    @Test
    fun `UpdateWebExtensionEnabledAction - Updates enabled state of an existing web extension`() {
        var state = BrowserState()
        val extension = WebExtensionState("id", "url")

        state = BrowserStateReducer.reduce(state, WebExtensionAction.InstallWebExtensionAction(extension))
        assertTrue(state.extensions[extension.id]?.enabled!!)

        state =
            BrowserStateReducer.reduce(state, WebExtensionAction.UpdateWebExtensionEnabledAction(extension.id, false))
        assertFalse(state.extensions[extension.id]?.enabled!!)

        state =
            BrowserStateReducer.reduce(state, WebExtensionAction.UpdateWebExtensionEnabledAction(extension.id, true))
        assertTrue(state.extensions[extension.id]?.enabled!!)
    }

    @Test
    fun `UpdateWebExtension -  Update an existing extension`() {
        val existingExtension = WebExtensionState("id", "url")
        val updatedExtension = WebExtensionState("id", "url2")

        var state = BrowserState(extensions = mapOf("id" to existingExtension))

        state = BrowserStateReducer.reduce(state, WebExtensionAction.UpdateWebExtensionAction(updatedExtension))
        assertEquals(updatedExtension, state.extensions.values.first())
        assertSame(updatedExtension, state.extensions.values.first())
    }

    @Test
    fun `UpdateWebExtensionAllowedInPrivateBrowsingAction - Updates allowedInPrivateBrowsing state of an existing web extension`() {
        var state = BrowserState()
        val extension = WebExtensionState("id", "url", allowedInPrivateBrowsing = false)

        state = BrowserStateReducer.reduce(state, WebExtensionAction.InstallWebExtensionAction(extension))
        assertFalse(state.extensions[extension.id]?.allowedInPrivateBrowsing!!)

        state =
            BrowserStateReducer.reduce(
                state,
                WebExtensionAction.UpdateWebExtensionAllowedInPrivateBrowsingAction(extension.id, true),
            )
        assertTrue(state.extensions[extension.id]?.allowedInPrivateBrowsing!!)

        state =
            BrowserStateReducer.reduce(
                state,
                WebExtensionAction.UpdateWebExtensionAllowedInPrivateBrowsingAction(extension.id, false),
            )
        assertFalse(state.extensions[extension.id]?.allowedInPrivateBrowsing!!)
    }

    @Test
    fun `UpdateWebExtensionTabAction - Marks tab active for web extensions`() {
        val tab = createTab(url = "https://mozilla.org")
        var state = BrowserState(tabs = listOf(tab))

        assertNull(state.activeWebExtensionTabId)

        state = BrowserStateReducer.reduce(state, WebExtensionAction.UpdateActiveWebExtensionTabAction(tab.id))
        assertEquals(tab.id, state.activeWebExtensionTabId)

        state = BrowserStateReducer.reduce(state, WebExtensionAction.UpdateActiveWebExtensionTabAction(null))
        assertNull(state.activeWebExtensionTabId)
    }

    @Test
    fun `WHEN UpdatePromptRequestWebExtensionAction is dispatched THEN a WebExtensionPromptRequest is added to the store`() {
        var state = BrowserState()

        assertNull(state.webExtensionPromptRequest)

        val promptRequest =
            WebExtensionPromptRequest.AfterInstallation.Permissions.Required(mock(), mock(), mock(), mock(), mock())

        state =
            BrowserStateReducer.reduce(state, WebExtensionAction.UpdatePromptRequestWebExtensionAction(promptRequest))

        assertNotNull(state.webExtensionPromptRequest)
        assertEquals(promptRequest, state.webExtensionPromptRequest)
    }

    @Test
    fun `WHEN ConsumePromptRequestWebExtensionAction is dispatched THEN the WebExtensionPromptRequest is removed from the store`() {
        val promptRequest =
            WebExtensionPromptRequest.AfterInstallation.Permissions.Required(mock(), mock(), mock(), mock(), mock())

        var state = BrowserState(webExtensionPromptRequest = promptRequest)
        assertNotNull(state.webExtensionPromptRequest)

        state = BrowserStateReducer.reduce(state, WebExtensionAction.ConsumePromptRequestWebExtensionAction)
        assertNull(state.webExtensionPromptRequest)
    }

    @Test
    fun `WHEN UpdatePendingExtensionUpdateAction is dispatched THEN the update replaces the one pending for the extension`() {
        var state = BrowserState()
        assertTrue(state.pendingExtensionUpdates.isEmpty())

        val firstUpdate = pendingExtensionUpdate()
        state = BrowserStateReducer.reduce(state, WebExtensionAction.UpdatePendingExtensionUpdateAction(firstUpdate))
        assertEquals(firstUpdate, state.pendingExtensionUpdates["id"])

        val secondUpdate = pendingExtensionUpdate()
        state = BrowserStateReducer.reduce(state, WebExtensionAction.UpdatePendingExtensionUpdateAction(secondUpdate))
        assertEquals(1, state.pendingExtensionUpdates.size)
        assertEquals(secondUpdate, state.pendingExtensionUpdates["id"])
    }

    @Test
    fun `WHEN RemovePendingExtensionUpdateAction is dispatched THEN the pending update is removed from the store`() {
        val pendingUpdate = pendingExtensionUpdate()
        var state = BrowserState(pendingExtensionUpdates = mapOf("id" to pendingUpdate))

        state = BrowserStateReducer.reduce(state, WebExtensionAction.RemovePendingExtensionUpdateAction("other"))
        assertEquals(pendingUpdate, state.pendingExtensionUpdates["id"])

        state = BrowserStateReducer.reduce(state, WebExtensionAction.RemovePendingExtensionUpdateAction("id"))
        assertTrue(state.pendingExtensionUpdates.isEmpty())
    }

    private fun pendingExtensionUpdate() =
        PendingExtensionUpdate(
            extensionId = "id",
            permissions = listOf("privacy"),
            origins = emptyList(),
            dataCollectionPermissions = emptyList(),
            onPermissionsGranted = {},
        )
}
