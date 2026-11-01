/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.support.webextensions

import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlin.coroutines.ContinuationInterceptor
import kotlin.test.assertNotNull
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.action.BrowserAction
import mozilla.components.browser.state.action.ContentAction
import mozilla.components.browser.state.action.CustomTabListAction
import mozilla.components.browser.state.action.EngineAction
import mozilla.components.browser.state.action.TabListAction
import mozilla.components.browser.state.action.WebExtensionAction
import mozilla.components.browser.state.selector.findTab
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.SessionState
import mozilla.components.browser.state.state.WebExtensionState
import mozilla.components.browser.state.state.createCustomTab
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.state.extension.PendingExtensionUpdate
import mozilla.components.browser.state.state.extension.WebExtensionPromptRequest
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.Engine
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.webextension.Action
import mozilla.components.concept.engine.webextension.ActionHandler
import mozilla.components.concept.engine.webextension.Incognito
import mozilla.components.concept.engine.webextension.Metadata
import mozilla.components.concept.engine.webextension.PermissionPromptResponse
import mozilla.components.concept.engine.webextension.TabHandler
import mozilla.components.concept.engine.webextension.WebExtension
import mozilla.components.concept.engine.webextension.WebExtensionDelegate
import mozilla.components.concept.engine.webextension.WebExtensionInstallException
import mozilla.components.support.base.Component
import mozilla.components.support.base.facts.Action as FactsAction
import mozilla.components.support.base.facts.processor.CollectionProcessor
import mozilla.components.support.test.any
import mozilla.components.support.test.argumentCaptor
import mozilla.components.support.test.eq
import mozilla.components.support.test.middleware.CaptureActionsMiddleware
import mozilla.components.support.test.mock
import mozilla.components.support.test.whenever
import mozilla.components.support.webextensions.WebExtensionSupport.toState
import mozilla.components.support.webextensions.facts.WebExtensionFacts.Items.WEB_EXTENSIONS_INITIALIZED
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.never
import org.mockito.Mockito.times
import org.mockito.Mockito.verify

@RunWith(AndroidJUnit4::class)
class WebExtensionSupportTest {

    private val captureMiddleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()

    @After
    fun tearDown() {
        WebExtensionSupport.installedExtensions.clear()
        captureMiddleware.reset()
    }

    @Test
    fun `sets web extension delegate on engine`() {
        val engine: Engine = mock()
        val store = BrowserStore()

        WebExtensionSupport.initialize(engine, store)
        verify(engine).registerWebExtensionDelegate(any())
    }

    @Test
    fun `queries engine for installed extensions and adds state to the store`() {
        val store = BrowserStore(middleware = listOf(captureMiddleware))

        val ext1: WebExtension = mock()
        val ext1Meta: Metadata = mock()
        whenever(ext1Meta.name).thenReturn("ext1")
        val ext2: WebExtension = mock()
        whenever(ext1.id).thenReturn("1")
        whenever(ext1.url).thenReturn("url1")
        whenever(ext1.getMetadata()).thenReturn(ext1Meta)
        whenever(ext1.isEnabled()).thenReturn(true)
        whenever(ext1.isAllowedInPrivateBrowsing()).thenReturn(true)

        whenever(ext2.id).thenReturn("2")
        whenever(ext2.url).thenReturn("url2")
        whenever(ext2.isEnabled()).thenReturn(false)
        whenever(ext2.isAllowedInPrivateBrowsing()).thenReturn(false)

        val engine: Engine = mock()
        val callbackCaptor = argumentCaptor<((List<WebExtension>) -> Unit)>()
        whenever(engine.listInstalledWebExtensions(callbackCaptor.capture(), any())).thenAnswer {
            callbackCaptor.value.invoke(listOf(ext1, ext2))
        }

        CollectionProcessor.withFactCollection { facts ->
            WebExtensionSupport.initialize(engine, store)

            val interactionFact = facts[0]
            assertEquals(FactsAction.INTERACTION, interactionFact.action)
            assertEquals(Component.SUPPORT_WEBEXTENSIONS, interactionFact.component)
            assertEquals(WEB_EXTENSIONS_INITIALIZED, interactionFact.item)
            assertEquals(2, interactionFact.metadata?.size)
            assertTrue(interactionFact.metadata?.containsKey("installed")!!)
            assertTrue(interactionFact.metadata?.containsKey("enabled")!!)
            assertEquals(listOf(ext1.id, ext2.id), interactionFact.metadata?.get("installed"))
            assertEquals(listOf(ext1.id), interactionFact.metadata?.get("enabled"))
        }
        assertEquals(ext1, WebExtensionSupport.installedExtensions[ext1.id])
        assertEquals(ext2, WebExtensionSupport.installedExtensions[ext2.id])

        captureMiddleware.assertFirstAction(WebExtensionAction.InstallWebExtensionAction::class) { action ->
            assertEquals(ext1.id, action.extension.id)
            assertEquals(ext1.url, action.extension.url)
            assertEquals("ext1", action.extension.name)
            assertTrue(ext1.id, action.extension.enabled)
            assertTrue(ext1.id, action.extension.allowedInPrivateBrowsing)
        }

        captureMiddleware.assertLastAction(WebExtensionAction.InstallWebExtensionAction::class) { action ->
            assertEquals(ext2.id, action.extension.id)
            assertEquals(ext2.url, action.extension.url)
            assertNull(action.extension.name)
            assertFalse(ext1.id, action.extension.enabled)
            assertFalse(ext1.id, action.extension.allowedInPrivateBrowsing)
        }
    }

    @Test
    fun `reacts to new tab being opened by adding tab to store`() {
        val store = BrowserStore(middleware = listOf(captureMiddleware))
        val engine: Engine = mock()
        val ext: WebExtension = mock()
        val engineSession: EngineSession = mock()

        val delegateCaptor = argumentCaptor<WebExtensionDelegate>()
        WebExtensionSupport.initialize(engine, store)
        verify(engine).registerWebExtensionDelegate(delegateCaptor.capture())

        delegateCaptor.value.onNewTab(ext, engineSession, true, "https://mozilla.org", false)

        captureMiddleware.assertFirstAction(TabListAction.AddTabAction::class) { action ->
            assertEquals("https://mozilla.org", action.tab.content.url)
        }

        captureMiddleware.assertFirstAction(EngineAction.LinkEngineSessionAction::class) { action ->
            assertSame(engineSession, action.engineSession)
        }
    }

    @Test
    fun `allows overriding onNewTab behaviour`() {
        val store = BrowserStore()
        val engine: Engine = mock()
        val ext: WebExtension = mock()
        val engineSession: EngineSession = mock()
        var onNewTabCalled = false

        val delegateCaptor = argumentCaptor<WebExtensionDelegate>()
        WebExtensionSupport.initialize(
            engine,
            store,
            onNewTabOverride = { _, _, _, _, _ ->
                onNewTabCalled = true
                "123"
            },
        )
        verify(engine).registerWebExtensionDelegate(delegateCaptor.capture())

        delegateCaptor.value.onNewTab(ext, engineSession, true, "https://mozilla.org", false)
        assertTrue(onNewTabCalled)
    }

    @Test
    fun `forwards active flag to onNewTabOverride`() {
        val store = BrowserStore()
        val engine: Engine = mock()
        val ext: WebExtension = mock()
        val engineSession: EngineSession = mock()
        val capturedSelected = mutableListOf<Boolean>()
        var selectTabOverrideCallCount = 0

        val delegateCaptor = argumentCaptor<WebExtensionDelegate>()
        WebExtensionSupport.initialize(
            engine,
            store,
            onNewTabOverride = { _, _, _, selected, _ ->
                capturedSelected.add(selected)
                "session-${capturedSelected.size}"
            },
            onSelectTabOverride = { _, _ ->
                selectTabOverrideCallCount++
            },
        )
        verify(engine).registerWebExtensionDelegate(delegateCaptor.capture())

        delegateCaptor.value.onNewTab(ext, engineSession, true, "https://mozilla.org", false)
        delegateCaptor.value.onNewTab(ext, engineSession, false, "https://mozilla.org", false)

        assertEquals(listOf(true, false), capturedSelected)
        // onSelectTabOverride should only fire for the active=true case; the override
        // is responsible for the active=false case via its own selected argument.
        assertEquals(1, selectTabOverrideCallCount)
    }

    @Test
    fun `reacts to new tab being opened in background by adding unselected tab to store`() {
        val store = BrowserStore(middleware = listOf(captureMiddleware))
        val engine: Engine = mock()
        val ext: WebExtension = mock()
        val engineSession: EngineSession = mock()

        val delegateCaptor = argumentCaptor<WebExtensionDelegate>()
        WebExtensionSupport.initialize(engine, store)
        verify(engine).registerWebExtensionDelegate(delegateCaptor.capture())

        delegateCaptor.value.onNewTab(ext, engineSession, false, "https://mozilla.org", false)

        captureMiddleware.assertFirstAction(TabListAction.AddTabAction::class) { action ->
            assertEquals("https://mozilla.org", action.tab.content.url)
            assertFalse(action.select)
        }
    }

    @Test
    fun `reacts to tab being closed by removing tab from store`() {
        val engine: Engine = mock()
        val ext: WebExtension = mock()
        whenever(ext.id).thenReturn("test")
        whenever(ext.isEnabled()).thenReturn(true)
        whenever(ext.hasTabHandler(any())).thenReturn(false, true)
        val engineSession: EngineSession = mock()
        val tabId = "testTabId"
        val store =
            BrowserStore(
                BrowserState(
                    tabs =
                        listOf(
                            createTab(
                                id = tabId,
                                url = "https://www.mozilla.org",
                                engineSession = engineSession,
                            )
                        )
                ),
                middleware = listOf(captureMiddleware),
            )
        val installedList = mutableListOf(ext)
        val callbackCaptor = argumentCaptor<((List<WebExtension>) -> Unit)>()
        whenever(engine.listInstalledWebExtensions(callbackCaptor.capture(), any())).thenAnswer {
            callbackCaptor.value.invoke(installedList)
        }

        val tabHandlerCaptor = argumentCaptor<TabHandler>()
        WebExtensionSupport.initialize(engine, store)

        verify(ext).registerTabHandler(eq(engineSession), tabHandlerCaptor.capture())
        tabHandlerCaptor.value.onCloseTab(ext, engineSession)

        captureMiddleware.findFirstAction(TabListAction.RemoveTabAction::class)
    }

    @Test
    fun `reacts to custom tab being closed by removing tab from store`() {
        val engine: Engine = mock()
        val ext: WebExtension = mock()
        whenever(ext.id).thenReturn("test")
        whenever(ext.isEnabled()).thenReturn(true)
        whenever(ext.hasTabHandler(any())).thenReturn(false, true)
        val engineSession: EngineSession = mock()
        val tabId = "testTabId"
        val store =
            BrowserStore(
                BrowserState(
                    customTabs =
                        listOf(
                            createCustomTab(
                                id = tabId,
                                url = "https://www.mozilla.org",
                                engineSession = engineSession,
                                source = SessionState.Source.Internal.CustomTab,
                            )
                        )
                ),
                middleware = listOf(captureMiddleware),
            )
        val installedList = mutableListOf(ext)
        val callbackCaptor = argumentCaptor<((List<WebExtension>) -> Unit)>()
        whenever(engine.listInstalledWebExtensions(callbackCaptor.capture(), any())).thenAnswer {
            callbackCaptor.value.invoke(installedList)
        }

        val tabHandlerCaptor = argumentCaptor<TabHandler>()
        WebExtensionSupport.initialize(engine, store)

        verify(ext).registerTabHandler(eq(engineSession), tabHandlerCaptor.capture())
        tabHandlerCaptor.value.onCloseTab(ext, engineSession)

        captureMiddleware.assertFirstAction(CustomTabListAction.RemoveCustomTabAction::class) { action ->
            assertEquals(tabId, action.tabId)
        }
    }

    @Test
    fun `allows overriding onCloseTab behaviour`() {
        val engine: Engine = mock()
        val ext: WebExtension = mock()
        whenever(ext.id).thenReturn("test")
        whenever(ext.isEnabled()).thenReturn(true)
        whenever(ext.hasTabHandler(any())).thenReturn(false, true)
        val engineSession: EngineSession = mock()
        var onCloseTabCalled = false
        val tabId = "testTabId"
        val store =
            BrowserStore(
                BrowserState(
                    tabs =
                        listOf(
                            createTab(
                                id = tabId,
                                url = "https://www.mozilla.org",
                                engineSession = engineSession,
                            )
                        )
                )
            )

        val installedList = mutableListOf(ext)
        val callbackCaptor = argumentCaptor<((List<WebExtension>) -> Unit)>()
        whenever(engine.listInstalledWebExtensions(callbackCaptor.capture(), any())).thenAnswer {
            callbackCaptor.value.invoke(installedList)
        }

        val tabHandlerCaptor = argumentCaptor<TabHandler>()
        WebExtensionSupport.initialize(
            engine,
            store,
            onSelectTabOverride = { _, _ -> },
            onCloseTabOverride = { _, _ -> onCloseTabCalled = true },
        )

        verify(ext).registerTabHandler(eq(engineSession), tabHandlerCaptor.capture())
        tabHandlerCaptor.value.onCloseTab(ext, engineSession)
        assertTrue(onCloseTabCalled)
    }

    @Test
    fun `reacts to tab being updated`() {
        val engine: Engine = mock()
        val ext: WebExtension = mock()
        whenever(ext.id).thenReturn("test")
        whenever(ext.isEnabled()).thenReturn(true)
        whenever(ext.hasTabHandler(any())).thenReturn(false, true)
        val engineSession: EngineSession = mock()
        val tabId = "testTabId"
        val store =
            BrowserStore(
                BrowserState(
                    tabs =
                        listOf(
                            createTab(
                                id = tabId,
                                url = "https://www.mozilla.org",
                                engineSession = engineSession,
                            )
                        )
                )
            )

        val installedList = mutableListOf(ext)
        val callbackCaptor = argumentCaptor<((List<WebExtension>) -> Unit)>()
        whenever(engine.listInstalledWebExtensions(callbackCaptor.capture(), any())).thenAnswer {
            callbackCaptor.value.invoke(installedList)
        }

        val tabHandlerCaptor = argumentCaptor<TabHandler>()
        WebExtensionSupport.initialize(engine, store)

        // Update tab to select it
        verify(ext).registerTabHandler(eq(engineSession), tabHandlerCaptor.capture())
        assertNull(store.state.selectedTabId)
        assertTrue(tabHandlerCaptor.value.onUpdateTab(ext, engineSession, true, null))
        assertEquals("testTabId", store.state.selectedTabId)

        // Update URL of tab
        assertTrue(tabHandlerCaptor.value.onUpdateTab(ext, engineSession, false, "url"))
        verify(engineSession).loadUrl("url")

        // Update non-existing tab
        store.dispatch(TabListAction.RemoveTabAction(tabId))
        assertFalse(tabHandlerCaptor.value.onUpdateTab(ext, engineSession, true, "url"))
    }

    @Test
    fun `reacts to custom tab being updated`() {
        val engine: Engine = mock()
        val ext: WebExtension = mock()
        whenever(ext.id).thenReturn("test")
        whenever(ext.isEnabled()).thenReturn(true)
        whenever(ext.hasTabHandler(any())).thenReturn(false, true)
        val engineSession: EngineSession = mock()
        val tabId = "testTabId"
        val store =
            BrowserStore(
                BrowserState(
                    customTabs =
                        listOf(
                            createCustomTab(
                                id = tabId,
                                url = "https://www.mozilla.org",
                                engineSession = engineSession,
                                source = SessionState.Source.Internal.CustomTab,
                            )
                        )
                )
            )

        val installedList = mutableListOf(ext)
        val callbackCaptor = argumentCaptor<((List<WebExtension>) -> Unit)>()
        whenever(engine.listInstalledWebExtensions(callbackCaptor.capture(), any())).thenAnswer {
            callbackCaptor.value.invoke(installedList)
        }

        val tabHandlerCaptor = argumentCaptor<TabHandler>()
        WebExtensionSupport.initialize(engine, store)

        // Update tab to select it
        verify(ext).registerTabHandler(eq(engineSession), tabHandlerCaptor.capture())
        assertNull(store.state.selectedTabId)
        assertTrue(tabHandlerCaptor.value.onUpdateTab(ext, engineSession, true, null))

        // Update URL of tab
        assertTrue(tabHandlerCaptor.value.onUpdateTab(ext, engineSession, false, "url"))
        verify(engineSession).loadUrl("url")

        // Update non-existing tab
        store.dispatch(CustomTabListAction.RemoveCustomTabAction(tabId))
        assertFalse(tabHandlerCaptor.value.onUpdateTab(ext, engineSession, true, "url"))
    }

    @Test
    fun `reacts to new extension being installed`() {
        val engineSession: EngineSession = mock()
        val tab = createTab(id = "1", url = "https://www.mozilla.org", engineSession = engineSession)

        val customTabEngineSession: EngineSession = mock()
        val customTab =
            createCustomTab(
                id = "2",
                url = "https://www.mozilla.org",
                engineSession = customTabEngineSession,
                source = SessionState.Source.Internal.CustomTab,
            )

        val store =
            BrowserStore(
                BrowserState(
                    tabs = listOf(tab),
                    customTabs = listOf(customTab),
                ),
                middleware = listOf(captureMiddleware),
            )

        val engine: Engine = mock()
        val ext: WebExtension = mock()
        whenever(ext.id).thenReturn("extensionId")
        whenever(ext.url).thenReturn("url")
        whenever(ext.supportActions).thenReturn(true)
        whenever(ext.isBuiltIn()).thenReturn(false)

        val delegateCaptor = argumentCaptor<WebExtensionDelegate>()
        WebExtensionSupport.initialize(engine, store)
        verify(engine).registerWebExtensionDelegate(delegateCaptor.capture())

        // Verify that we dispatch to the store and mark the extension as installed
        delegateCaptor.value.onInstalled(ext)

        captureMiddleware.assertFirstAction(WebExtensionAction.InstallWebExtensionAction::class) { action ->
            assertEquals(
                WebExtensionState(
                    ext.id,
                    ext.url,
                    ext.getMetadata()?.name,
                    ext.isEnabled(),
                ),
                action.extension,
            )
        }

        captureMiddleware.assertFirstAction(WebExtensionAction.UpdatePromptRequestWebExtensionAction::class) { action ->
            assertEquals(
                WebExtensionPromptRequest.AfterInstallation.PostInstallation(ext),
                action.promptRequest,
            )
        }

        assertEquals(ext, WebExtensionSupport.installedExtensions[ext.id])

        // Verify that we register action and tab handlers for all existing sessions on the extension
        val actionHandlerCaptor = argumentCaptor<ActionHandler>()
        val tabHandlerCaptor = argumentCaptor<TabHandler>()
        verify(ext).registerActionHandler(eq(customTabEngineSession), actionHandlerCaptor.capture())
        verify(ext).registerTabHandler(eq(customTabEngineSession), tabHandlerCaptor.capture())
        verify(ext).registerActionHandler(eq(engineSession), actionHandlerCaptor.capture())
        verify(ext).registerTabHandler(eq(engineSession), tabHandlerCaptor.capture())

        // Verify we only register the handlers once
        whenever(ext.hasActionHandler(engineSession)).thenReturn(true)
        whenever(ext.hasTabHandler(engineSession)).thenReturn(true)

        actionHandlerCaptor.value.onBrowserAction(ext, engineSession, mock())

        captureMiddleware.assertLastAction(WebExtensionAction.UpdateTabBrowserAction::class) { action ->
            assertEquals(ext.id, action.extensionId)
        }

        store.dispatch(ContentAction.UpdateUrlAction(sessionId = "1", url = "https://www.firefox.com"))
        verify(ext, times(1)).registerActionHandler(eq(engineSession), actionHandlerCaptor.capture())
        verify(ext, times(1)).registerTabHandler(eq(engineSession), tabHandlerCaptor.capture())

        tabHandlerCaptor.value.onUpdateTab(ext, engineSession, true, null)

        captureMiddleware.assertFirstAction(TabListAction.SelectTabAction::class) { action ->
            assertEquals("1", action.tabId)
        }

        tabHandlerCaptor.value.onUpdateTab(ext, engineSession, true, "url")
        verify(engineSession).loadUrl("url")
    }

    @Test
    fun `reacts to install permission request`() {
        val store = BrowserStore(middleware = listOf(captureMiddleware))
        val engine: Engine = mock()
        val ext: WebExtension = mock()
        val onPermissionsGranted: ((PermissionPromptResponse) -> Unit) = mock()
        val permissions = listOf("permissions")
        val origins = listOf("https://www.mozilla.org")
        val dataCollectionPermissions = listOf("locationInfo")

        val delegateCaptor = argumentCaptor<WebExtensionDelegate>()
        WebExtensionSupport.initialize(engine, store)
        verify(engine).registerWebExtensionDelegate(delegateCaptor.capture())

        // Verify they we confirm the permission request
        delegateCaptor.value.onInstallPermissionRequest(
            ext,
            permissions,
            origins,
            dataCollectionPermissions,
            onPermissionsGranted,
        )

        captureMiddleware.assertFirstAction(WebExtensionAction.UpdatePromptRequestWebExtensionAction::class) { action ->
            assertEquals(
                WebExtensionPromptRequest.AfterInstallation.Permissions.Required(
                    ext,
                    permissions,
                    origins,
                    dataCollectionPermissions,
                    onPermissionsGranted,
                ),
                action.promptRequest,
            )
        }
    }

    @Test
    fun `reacts to extension being uninstalled`() {
        val store = BrowserStore(middleware = listOf(captureMiddleware))

        val engine: Engine = mock()
        val ext: WebExtension = mock()
        whenever(ext.id).thenReturn("extensionId")
        whenever(ext.url).thenReturn("url")
        whenever(ext.supportActions).thenReturn(true)

        val delegateCaptor = argumentCaptor<WebExtensionDelegate>()
        WebExtensionSupport.initialize(engine, store)
        verify(engine).registerWebExtensionDelegate(delegateCaptor.capture())

        delegateCaptor.value.onInstalled(ext)

        captureMiddleware.assertFirstAction(WebExtensionAction.InstallWebExtensionAction::class) { action ->
            assertEquals(
                WebExtensionState(
                    ext.id,
                    ext.url,
                    ext.getMetadata()?.name,
                    ext.isEnabled(),
                ),
                action.extension,
            )
        }

        assertEquals(ext, WebExtensionSupport.installedExtensions[ext.id])

        // Verify that we dispatch to the store and mark the extension as uninstalled
        delegateCaptor.value.onUninstalled(ext)

        captureMiddleware.assertFirstAction(WebExtensionAction.UninstallWebExtensionAction::class) { action ->
            assertEquals(ext.id, action.extensionId)
        }

        assertNull(WebExtensionSupport.installedExtensions[ext.id])
    }

    @Test
    fun `GIVEN BuiltIn extension WHEN calling onInstalled THEN do not show the PostInstallation prompt`() {
        val store = BrowserStore(middleware = listOf(captureMiddleware))

        val engine: Engine = mock()
        val ext: WebExtension = mock()
        whenever(ext.id).thenReturn("extensionId")
        whenever(ext.url).thenReturn("url")
        whenever(ext.supportActions).thenReturn(true)
        whenever(ext.isBuiltIn()).thenReturn(true)

        val delegateCaptor = argumentCaptor<WebExtensionDelegate>()
        WebExtensionSupport.initialize(engine, store)
        verify(engine).registerWebExtensionDelegate(delegateCaptor.capture())

        delegateCaptor.value.onInstalled(ext)

        captureMiddleware.assertNotDispatched(WebExtensionAction.UpdatePromptRequestWebExtensionAction::class)
    }

    @Test
    fun `GIVEN already installed extension WHEN calling onInstalled THEN do not show the PostInstallation prompt`() {
        val store = BrowserStore(middleware = listOf(captureMiddleware))

        val engine: Engine = mock()
        val ext: WebExtension = mock()
        whenever(ext.id).thenReturn("extensionId")

        val delegateCaptor = argumentCaptor<WebExtensionDelegate>()
        WebExtensionSupport.initialize(engine, store)
        verify(engine).registerWebExtensionDelegate(delegateCaptor.capture())

        // We simulate a first install...
        delegateCaptor.value.onInstalled(ext)
        // ... and then an update, which also calls `onInstalled()`.
        delegateCaptor.value.onInstalled(ext)

        captureMiddleware.assertFirstAction(WebExtensionAction.UpdatePromptRequestWebExtensionAction::class) { action ->
            assertEquals(WebExtensionPromptRequest.AfterInstallation.PostInstallation(ext), action.promptRequest)
        }
    }

    @Test
    fun `GIVEN extension WHEN calling onInstallationFailedRequest THEN show the installation prompt error`() {
        val store = BrowserStore(middleware = listOf(captureMiddleware))
        val engine: Engine = mock()
        val ext: WebExtension = mock()
        val exception = WebExtensionInstallException.Blocklisted(throwable = Exception())

        whenever(ext.id).thenReturn("extensionId")
        whenever(ext.url).thenReturn("url")
        whenever(ext.supportActions).thenReturn(true)
        whenever(ext.isBuiltIn()).thenReturn(false)

        val delegateCaptor = argumentCaptor<WebExtensionDelegate>()
        WebExtensionSupport.initialize(engine, store)
        verify(engine).registerWebExtensionDelegate(delegateCaptor.capture())

        delegateCaptor.value.onInstallationFailedRequest(ext, exception)

        captureMiddleware.assertFirstAction(WebExtensionAction.UpdatePromptRequestWebExtensionAction::class) { action ->
            assertEquals(
                WebExtensionPromptRequest.BeforeInstallation.InstallationFailed(ext, exception),
                action.promptRequest,
            )
        }
    }

    @Test
    fun `reacts to extension being enabled`() {
        val store = BrowserStore(middleware = listOf(captureMiddleware))

        val engine: Engine = mock()
        val ext: WebExtension = mock()
        whenever(ext.id).thenReturn("extensionId")
        whenever(ext.url).thenReturn("url")
        whenever(ext.supportActions).thenReturn(true)

        val delegateCaptor = argumentCaptor<WebExtensionDelegate>()
        WebExtensionSupport.initialize(engine, store)
        verify(engine).registerWebExtensionDelegate(delegateCaptor.capture())

        delegateCaptor.value.onEnabled(ext)
        captureMiddleware.assertFirstAction(WebExtensionAction.UpdateWebExtensionEnabledAction::class) { action ->
            assertEquals(ext.id, action.extensionId)
            assertTrue(action.enabled)
        }

        assertEquals(ext, WebExtensionSupport.installedExtensions[ext.id])
    }

    @Test
    fun `reacts to extension being disabled`() {
        val store = BrowserStore(middleware = listOf(captureMiddleware))

        val engine: Engine = mock()
        val ext: WebExtension = mock()
        whenever(ext.id).thenReturn("extensionId")
        whenever(ext.url).thenReturn("url")
        whenever(ext.supportActions).thenReturn(true)

        val delegateCaptor = argumentCaptor<WebExtensionDelegate>()
        WebExtensionSupport.initialize(engine, store)
        verify(engine).registerWebExtensionDelegate(delegateCaptor.capture())

        delegateCaptor.value.onDisabled(ext)
        captureMiddleware.assertFirstAction(WebExtensionAction.UpdateWebExtensionEnabledAction::class) { action ->
            assertEquals(ext.id, action.extensionId)
            assertFalse(action.enabled)
        }
        assertEquals(ext, WebExtensionSupport.installedExtensions[ext.id])
    }

    @Test
    fun `reacts to optional permissions for an extension being changed`() {
        val store = BrowserStore(middleware = listOf(captureMiddleware))
        val engine: Engine = mock()
        val ext: WebExtension = mock()
        whenever(ext.id).thenReturn("extensionId")
        whenever(ext.url).thenReturn("url")

        val delegateCaptor = argumentCaptor<WebExtensionDelegate>()
        WebExtensionSupport.initialize(engine, store)
        verify(engine).registerWebExtensionDelegate(delegateCaptor.capture())

        assertNull(WebExtensionSupport.installedExtensions[ext.id])

        delegateCaptor.value.onOptionalPermissionsChanged(ext)

        assertEquals(ext, WebExtensionSupport.installedExtensions[ext.id])
    }

    @Test
    fun `observes store and registers handlers on new engine sessions`() = runTest {
        val tab = createTab(id = "1", url = "https://www.mozilla.org")
        val customTab =
            createCustomTab(id = "2", url = "https://www.mozilla.org", source = SessionState.Source.Internal.CustomTab)
        val store =
            BrowserStore(
                BrowserState(
                    tabs = listOf(tab),
                    customTabs = listOf(customTab),
                ),
                middleware = listOf(captureMiddleware),
            )

        val engine: Engine = mock()
        val ext: WebExtension = mock()
        whenever(ext.id).thenReturn("extensionId")
        whenever(ext.url).thenReturn("url")
        whenever(ext.supportActions).thenReturn(true)

        // Install extension
        val delegateCaptor = argumentCaptor<WebExtensionDelegate>()
        WebExtensionSupport.initialize(
            engine,
            store,
            mainDispatcher = coroutineContext[ContinuationInterceptor] as CoroutineDispatcher,
        )
        testScheduler.advanceUntilIdle()

        verify(engine).registerWebExtensionDelegate(delegateCaptor.capture())
        delegateCaptor.value.onInstalled(ext)
        testScheduler.advanceUntilIdle()

        // Verify that action/tab handler is registered when a new engine session is created
        val actionHandlerCaptor = argumentCaptor<ActionHandler>()
        val tabHandlerCaptor = argumentCaptor<TabHandler>()

        verify(ext, never()).registerActionHandler(any(), any())
        verify(ext, never())
            .registerTabHandler(
                session = any(),
                tabHandler = any(),
            )

        val engineSession1: EngineSession = mock()
        store.dispatch(EngineAction.LinkEngineSessionAction(tab.id, engineSession1))
        testScheduler.advanceUntilIdle()

        verify(ext).registerActionHandler(eq(engineSession1), actionHandlerCaptor.capture())
        verify(ext).registerTabHandler(eq(engineSession1), tabHandlerCaptor.capture())

        val engineSession2: EngineSession = mock()
        store.dispatch(EngineAction.LinkEngineSessionAction(customTab.id, engineSession2))
        testScheduler.advanceUntilIdle()

        verify(ext).registerActionHandler(eq(engineSession2), actionHandlerCaptor.capture())
        verify(ext).registerTabHandler(eq(engineSession2), tabHandlerCaptor.capture())
    }

    @Test
    fun `reacts to browser action being defined by dispatching to the store`() {
        val store = BrowserStore(middleware = listOf(captureMiddleware))
        val engine: Engine = mock()
        val ext: WebExtension = mock()
        val browserAction: Action = mock()
        whenever(ext.id).thenReturn("test")

        val delegateCaptor = argumentCaptor<WebExtensionDelegate>()
        WebExtensionSupport.initialize(engine, store)
        verify(engine).registerWebExtensionDelegate(delegateCaptor.capture())

        delegateCaptor.value.onBrowserActionDefined(ext, browserAction)

        captureMiddleware.assertFirstAction(WebExtensionAction.UpdateBrowserAction::class) { action ->
            assertEquals(ext.id, action.extensionId)
            assertEquals(browserAction, action.browserAction)
        }
    }

    @Test
    fun `reacts to page action being defined by dispatching to the store`() {
        val store = BrowserStore(middleware = listOf(captureMiddleware))
        val engine: Engine = mock()
        val ext: WebExtension = mock()
        val pageAction: Action = mock()
        whenever(ext.id).thenReturn("test")

        val delegateCaptor = argumentCaptor<WebExtensionDelegate>()
        WebExtensionSupport.initialize(engine, store)
        verify(engine).registerWebExtensionDelegate(delegateCaptor.capture())

        delegateCaptor.value.onPageActionDefined(ext, pageAction)
        captureMiddleware.assertFirstAction(WebExtensionAction.UpdatePageAction::class) { action ->
            assertEquals(ext.id, action.extensionId)
            assertEquals(pageAction, action.pageAction)
        }
    }

    @Test
    fun `reacts to call for opening options page by dispatching to the store`() {
        val store = BrowserStore(middleware = listOf(captureMiddleware))
        val engine: Engine = mock()
        val ext: WebExtension = mock()
        val metaData: Metadata = mock()
        whenever(ext.id).thenReturn("testId")
        whenever(ext.getMetadata()).thenReturn(metaData)
        whenever(metaData.openOptionsPageInTab).thenReturn(false)
        whenever(metaData.optionsPageUrl).thenReturn("testUrl")
        whenever(metaData.name).thenReturn("testName")

        val delegateCaptor = argumentCaptor<WebExtensionDelegate>()
        WebExtensionSupport.initialize(engine, store)
        verify(engine).registerWebExtensionDelegate(delegateCaptor.capture())

        delegateCaptor.value.onOpenOptionsPage(ext)
        captureMiddleware.assertFirstAction(WebExtensionAction.UpdateOptionsPageSessionAction::class) { action ->
            assertEquals(ext.id, action.extensionId)
            assertTrue(action.optionsPageInstanceId.isNotEmpty())
            assertEquals(metaData.optionsPageUrl, action.optionsPageUrl)
            assertEquals(metaData.name, action.extensionTranslatedName)
        }
    }

    @Test
    fun `reacts to action popup being toggled by opening tab as needed`() {
        val engine: Engine = mock()

        val ext: WebExtension = mock()
        whenever(ext.id).thenReturn("test")

        val engineSession: EngineSession = mock()
        val browserAction: Action = mock()
        val store =
            BrowserStore(
                BrowserState(extensions = mapOf(ext.id to WebExtensionState(ext.id))),
                middleware = listOf(captureMiddleware),
            )

        val delegateCaptor = argumentCaptor<WebExtensionDelegate>()
        WebExtensionSupport.initialize(engine, store, openPopupInTab = true)
        verify(engine).registerWebExtensionDelegate(delegateCaptor.capture())

        // Toggling should open tab
        delegateCaptor.value.onToggleActionPopup(ext, engineSession, browserAction, false)

        captureMiddleware.assertFirstAction(TabListAction.AddTabAction::class) { action ->
            assertEquals("", action.tab.content.url)
        }

        captureMiddleware.assertFirstAction(EngineAction.LinkEngineSessionAction::class) { action ->
            assertSame(engineSession, action.engineSession)
        }

        captureMiddleware.assertFirstAction(WebExtensionAction.UpdatePopupSessionAction::class) { action ->
            assertNotNull(action.popupSessionId)
        }
    }

    @Test
    fun `reacts to action popup being toggled in private browsing mode by opening popup in a private tab`() {
        val engine: Engine = mock()

        val ext: WebExtension = mock()
        whenever(ext.id).thenReturn("test")

        val engineSession: EngineSession = mock()
        val browserAction: Action = mock()
        val store =
            BrowserStore(
                BrowserState(extensions = mapOf(ext.id to WebExtensionState(ext.id))),
                middleware = listOf(captureMiddleware),
            )

        val delegateCaptor = argumentCaptor<WebExtensionDelegate>()
        WebExtensionSupport.initialize(engine, store, openPopupInTab = true)
        verify(engine).registerWebExtensionDelegate(delegateCaptor.capture())

        // WebExtensionDelegate.onToggleActionPopup with isPrivate=true should
        // open a private tab. Note that this happens despite the extension
        // not having been initialized with metaData.allowedInPrivateBrowsing
        // set to true, because the enforcement of private browsing access
        // happens at the caller in GeckoEngine.onToggleActionPopup.
        delegateCaptor.value.onToggleActionPopup(ext, engineSession, browserAction, true)

        captureMiddleware.assertFirstAction(TabListAction.AddTabAction::class) { action ->
            assertTrue(action.tab.content.private)
        }
    }

    @Test
    fun `reacts to action popup being toggled by selecting tab as needed`() {
        val engine: Engine = mock()

        val ext: WebExtension = mock()
        whenever(ext.id).thenReturn("test")

        val engineSession: EngineSession = mock()
        val browserAction: Action = mock()
        val store =
            BrowserStore(
                BrowserState(
                    tabs = listOf(createTab(id = "popupTab", url = "https://www.mozilla.org")),
                    extensions =
                        mapOf(
                            ext.id to
                                WebExtensionState(
                                    ext.id,
                                    popupSessionId = "popupTab",
                                )
                        ),
                ),
                middleware = listOf(captureMiddleware),
            )

        val delegateCaptor = argumentCaptor<WebExtensionDelegate>()
        WebExtensionSupport.initialize(engine, store, openPopupInTab = true)
        verify(engine).registerWebExtensionDelegate(delegateCaptor.capture())

        // Toggling again should select popup tab
        delegateCaptor.value.onToggleActionPopup(ext, engineSession, browserAction, false)

        captureMiddleware.assertFirstAction(TabListAction.SelectTabAction::class) { action ->
            assertEquals("popupTab", action.tabId)
        }
    }

    @Test
    fun `reacts to action popup being toggled by closing tab as needed`() {
        val engine: Engine = mock()

        val ext: WebExtension = mock()
        whenever(ext.id).thenReturn("test")

        val engineSession: EngineSession = mock()
        val browserAction: Action = mock()
        val store =
            BrowserStore(
                BrowserState(
                    tabs = listOf(createTab(id = "popupTab", url = "https://www.mozilla.org")),
                    selectedTabId = "popupTab",
                    extensions =
                        mapOf(
                            ext.id to
                                WebExtensionState(
                                    ext.id,
                                    popupSessionId = "popupTab",
                                )
                        ),
                ),
                middleware = listOf(captureMiddleware),
            )

        val delegateCaptor = argumentCaptor<WebExtensionDelegate>()
        WebExtensionSupport.initialize(engine, store, openPopupInTab = true)
        verify(engine).registerWebExtensionDelegate(delegateCaptor.capture())

        // Toggling again should close tab
        delegateCaptor.value.onToggleActionPopup(ext, engineSession, browserAction, false)

        captureMiddleware.assertFirstAction(TabListAction.RemoveTabAction::class) { action ->
            assertEquals("popupTab", action.tabId)
        }
    }

    @Test
    fun `reacts to action popup being toggled by opening a popup`() {
        val engine: Engine = mock()

        val ext: WebExtension = mock()
        whenever(ext.id).thenReturn("test")

        val engineSession: EngineSession = mock()
        val browserAction: Action = mock()
        val store =
            BrowserStore(
                BrowserState(extensions = mapOf(ext.id to WebExtensionState(ext.id))),
                middleware = listOf(captureMiddleware),
            )

        val delegateCaptor = argumentCaptor<WebExtensionDelegate>()

        WebExtensionSupport.initialize(engine, store)
        verify(engine).registerWebExtensionDelegate(delegateCaptor.capture())

        // Toggling should allow state to have popup EngineSession instance
        delegateCaptor.value.onToggleActionPopup(ext, engineSession, browserAction, false)

        captureMiddleware.assertFirstAction(WebExtensionAction.UpdatePopupSessionAction::class) { action ->
            assertNotNull(action.popupSession)
            assertEquals(ext.id, action.extensionId)
        }
    }

    @Test
    fun `invokes onUpdatePermissionRequest callback`() {
        var executed = false
        val engine: Engine = mock()
        val ext: WebExtension = mock()
        whenever(ext.id).thenReturn("test")
        val store = BrowserStore(BrowserState(extensions = mapOf(ext.id to WebExtensionState(ext.id))))

        val delegateCaptor = argumentCaptor<WebExtensionDelegate>()
        WebExtensionSupport.initialize(
            runtime = engine,
            store = store,
            onUpdatePermissionRequest = { _, _, _, _, _ ->
                executed = true
            },
        )

        verify(engine).registerWebExtensionDelegate(delegateCaptor.capture())
        delegateCaptor.value.onUpdatePermissionRequest(mock(), mock(), mock(), mock(), mock())
        assertTrue(executed)
    }

    @Test
    fun `invokes onExtensionsLoaded callback`() {
        var executed = false
        val engine: Engine = mock()

        val ext: WebExtension = mock()
        whenever(ext.id).thenReturn("test")
        whenever(ext.isBuiltIn()).thenReturn(false)

        val builtInExt: WebExtension = mock()
        whenever(builtInExt.id).thenReturn("test2")
        whenever(builtInExt.isBuiltIn()).thenReturn(true)

        val store = BrowserStore(BrowserState(extensions = mapOf(ext.id to WebExtensionState(ext.id))))

        val callbackCaptor = argumentCaptor<((List<WebExtension>) -> Unit)>()
        whenever(engine.listInstalledWebExtensions(callbackCaptor.capture(), any())).thenAnswer {
            callbackCaptor.value.invoke(listOf(ext, builtInExt))
        }

        val onExtensionsLoaded: ((List<WebExtension>) -> Unit) = {
            assertEquals(1, it.size)
            assertEquals(ext, it[0])
            executed = true
        }
        WebExtensionSupport.initialize(
            runtime = engine,
            store = store,
            onExtensionsLoaded = onExtensionsLoaded,
        )
        assertTrue(executed)
    }

    @Test
    fun `reacts to extension list being updated in the engine`() {
        val store = BrowserStore(middleware = listOf(captureMiddleware))
        val ext: WebExtension = mock()
        whenever(ext.id).thenReturn("test")
        whenever(ext.isEnabled()).thenReturn(true)
        val installedList = mutableListOf(ext)

        val engine: Engine = mock()
        val callbackCaptor = argumentCaptor<((List<WebExtension>) -> Unit)>()
        whenever(engine.listInstalledWebExtensions(callbackCaptor.capture(), any())).thenAnswer {
            callbackCaptor.value.invoke(installedList)
        }

        val delegateCaptor = argumentCaptor<WebExtensionDelegate>()
        WebExtensionSupport.initialize(engine, store)
        assertEquals(1, WebExtensionSupport.installedExtensions.size)
        assertEquals(ext, WebExtensionSupport.installedExtensions[ext.id])
        verify(engine).registerWebExtensionDelegate(delegateCaptor.capture())

        delegateCaptor.value.onExtensionListUpdated()

        captureMiddleware.assertFirstAction(WebExtensionAction.InstallWebExtensionAction::class) { action ->
            assertEquals(ext.id, action.extension.id)
        }

        captureMiddleware.findFirstAction(WebExtensionAction.UninstallAllWebExtensionsAction::class)

        captureMiddleware.assertLastAction(WebExtensionAction.InstallWebExtensionAction::class) { action ->
            assertEquals(ext.id, action.extension.id)
        }

        // Verify installed extensions are cleared
        installedList.clear()
        delegateCaptor.value.onExtensionListUpdated()
        assertTrue(WebExtensionSupport.installedExtensions.isEmpty())
    }

    @Test
    fun `reacts to WebExtensionDelegate onReady by updating the extension details stored in the installedExtensions map`() {
        val store = BrowserStore(middleware = listOf(captureMiddleware))
        val ext: WebExtension = mock()
        whenever(ext.id).thenReturn("test")
        whenever(ext.isEnabled()).thenReturn(true)
        val extMeta: Metadata = mock()
        whenever(extMeta.incognito).thenReturn(Incognito.SPANNING)
        whenever(ext.getMetadata()).thenReturn(extMeta)
        val installedList = mutableListOf(ext)

        val engine: Engine = mock()
        val callbackCaptor = argumentCaptor<((List<WebExtension>) -> Unit)>()
        whenever(engine.listInstalledWebExtensions(callbackCaptor.capture(), any())).thenAnswer {
            callbackCaptor.value.invoke(installedList)
        }

        // Initialize WebExtensionSupport and expect the extension metadata
        // to be the one coming from the first mock WebExtension instance.
        WebExtensionSupport.initialize(engine, store)
        assertEquals(1, WebExtensionSupport.installedExtensions.size)
        assertEquals(ext, WebExtensionSupport.installedExtensions[ext.id])
        assertEquals(extMeta, WebExtensionSupport.installedExtensions[ext.id]?.getMetadata())

        val delegateCaptor = argumentCaptor<WebExtensionDelegate>()
        verify(engine).registerWebExtensionDelegate(delegateCaptor.capture())

        // Mock a call to the WebExtensionDelegate.onReady delegate and the
        // extension and its metadata instances stored in the installExtensions
        // map to have been updated as a side-effect of that.
        val extOnceReady: WebExtension = mock()
        whenever(extOnceReady.id).thenReturn("test")
        whenever(extOnceReady.isEnabled()).thenReturn(true)
        val extOnceReadyMeta: Metadata = mock()
        whenever(extOnceReady.getMetadata()).thenReturn(extOnceReadyMeta)

        delegateCaptor.value.onReady(extOnceReady)

        assertEquals(1, WebExtensionSupport.installedExtensions.size)
        assertEquals(extOnceReady, WebExtensionSupport.installedExtensions[ext.id])
        assertEquals(
            extOnceReadyMeta,
            WebExtensionSupport.installedExtensions[ext.id]?.getMetadata(),
        )
    }

    @Test
    fun `reacts to extensions process spawning disabled`() {
        val store = BrowserStore()
        val engine: Engine = mock()

        assertFalse(store.state.showExtensionsProcessDisabledPrompt)
        val delegateCaptor = argumentCaptor<WebExtensionDelegate>()
        WebExtensionSupport.initialize(engine, store)

        verify(engine).registerWebExtensionDelegate(delegateCaptor.capture())

        delegateCaptor.value.onDisabledExtensionProcessSpawning()

        assertTrue(store.state.showExtensionsProcessDisabledPrompt)
    }

    @Test
    fun `closes tabs from unsupported extensions`() = runTest {
        val store =
            BrowserStore(
                BrowserState(
                    tabs =
                        listOf(
                            createTab(id = "1", url = "https://www.mozilla.org", restored = true),
                            createTab(id = "2", url = "moz-extension://1234-5678/test", restored = true),
                            createTab(id = "3", url = "moz-extension://1234-5678-9/", restored = true),
                        )
                )
            )

        val ext1: WebExtension = mock()
        val ext1Meta: Metadata = mock()
        whenever(ext1Meta.baseUrl).thenReturn("moz-extension://1234-5678/")
        whenever(ext1.id).thenReturn("1")
        whenever(ext1.url).thenReturn("url1")
        whenever(ext1.getMetadata()).thenReturn(ext1Meta)
        whenever(ext1.isEnabled()).thenReturn(true)
        whenever(ext1.isAllowedInPrivateBrowsing()).thenReturn(true)

        val ext2: WebExtension = mock()
        whenever(ext2.id).thenReturn("2")
        whenever(ext2.url).thenReturn("url2")
        whenever(ext2.isEnabled()).thenReturn(true)
        whenever(ext2.isAllowedInPrivateBrowsing()).thenReturn(false)

        val engine: Engine = mock()
        val callbackCaptor = argumentCaptor<((List<WebExtension>) -> Unit)>()
        whenever(engine.listInstalledWebExtensions(callbackCaptor.capture(), any())).thenAnswer {
            callbackCaptor.value.invoke(listOf(ext1, ext2))
        }

        WebExtensionSupport.initialize(
            engine,
            store,
            mainDispatcher = coroutineContext[ContinuationInterceptor] as CoroutineDispatcher,
        )
        testScheduler.advanceUntilIdle()

        assertNotNull(store.state.findTab("1"))
        assertNotNull(store.state.findTab("2"))
        assertNull(store.state.findTab("3"))

        // Make sure we're running a single cleanup and stop the scope after
        store.dispatch(
            TabListAction.AddTabAction(
                createTab(
                    id = "4",
                    url = "moz-extension://1234-5678-90/",
                )
            )
        )

        assertNotNull(store.state.findTab("4"))
    }

    @Test
    fun `WHEN an extension is updated THEN an update of it still waiting for the user is cancelled`() {
        var answer: Boolean? = null
        val pendingUpdate = pendingExtensionUpdate("extensionId") { answer = it }
        val store = BrowserStore(BrowserState(pendingExtensionUpdates = mapOf("extensionId" to pendingUpdate)))

        val ext: WebExtension = mock()
        whenever(ext.id).thenReturn("extensionId")
        whenever(ext.url).thenReturn("url")

        WebExtensionSupport.markExtensionAsUpdated(store, ext)

        assertEquals(false, answer)
        assertTrue(store.state.pendingExtensionUpdates.isEmpty())
    }

    @Test
    fun `WHEN an extension is uninstalled THEN an update of it still waiting for the user is cancelled`() {
        var answer: Boolean? = null
        val pendingUpdate = pendingExtensionUpdate("extensionId") { answer = it }
        val store = BrowserStore(BrowserState(pendingExtensionUpdates = mapOf("extensionId" to pendingUpdate)))

        val engine: Engine = mock()
        val ext: WebExtension = mock()
        whenever(ext.id).thenReturn("extensionId")
        whenever(ext.url).thenReturn("url")

        val delegateCaptor = argumentCaptor<WebExtensionDelegate>()
        WebExtensionSupport.initialize(engine, store)
        verify(engine).registerWebExtensionDelegate(delegateCaptor.capture())

        delegateCaptor.value.onUninstalled(ext)

        assertEquals(false, answer)
        assertTrue(store.state.pendingExtensionUpdates.isEmpty())
    }

    @Test
    fun `WHEN an extension is installed THEN an update of it still waiting for the user is cancelled`() {
        var answer: Boolean? = null
        val pendingUpdate = pendingExtensionUpdate("extensionId") { answer = it }
        val store = BrowserStore(BrowserState(pendingExtensionUpdates = mapOf("extensionId" to pendingUpdate)))

        val engine: Engine = mock()
        val ext: WebExtension = mock()
        whenever(ext.id).thenReturn("extensionId")
        whenever(ext.url).thenReturn("url")

        val delegateCaptor = argumentCaptor<WebExtensionDelegate>()
        WebExtensionSupport.initialize(engine, store)
        verify(engine).registerWebExtensionDelegate(delegateCaptor.capture())

        delegateCaptor.value.onInstalled(ext)

        assertEquals(false, answer)
        assertTrue(store.state.pendingExtensionUpdates.isEmpty())
    }

    private fun pendingExtensionUpdate(extensionId: String, onPermissionsGranted: (Boolean) -> Unit) =
        PendingExtensionUpdate(
            extensionId = extensionId,
            permissions = listOf("privacy"),
            origins = emptyList(),
            dataCollectionPermissions = emptyList(),
            onPermissionsGranted = onPermissionsGranted,
        )

    @Test
    fun `marks extensions as updated`() {
        val engineSession: EngineSession = mock()
        val tab = createTab(id = "1", url = "https://www.mozilla.org", engineSession = engineSession)

        val customTabEngineSession: EngineSession = mock()
        val customTab =
            createCustomTab(
                id = "2",
                url = "https://www.mozilla.org",
                engineSession = customTabEngineSession,
                source = SessionState.Source.Internal.CustomTab,
            )

        val store =
            BrowserStore(
                BrowserState(
                    tabs = listOf(tab),
                    customTabs = listOf(customTab),
                ),
                middleware = listOf(captureMiddleware),
            )

        val ext: WebExtension = mock()
        whenever(ext.id).thenReturn("extensionId")
        whenever(ext.url).thenReturn("url")
        whenever(ext.supportActions).thenReturn(true)

        WebExtensionSupport.markExtensionAsUpdated(store, ext)
        assertSame(ext, WebExtensionSupport.installedExtensions[ext.id])

        captureMiddleware.assertFirstAction(WebExtensionAction.UpdateWebExtensionAction::class) { action ->
            assertEquals(ext.toState(), action.updatedExtension)
        }

        // Verify that we register new action and tab handlers for the updated extension
        val actionHandlerCaptor = argumentCaptor<ActionHandler>()
        val tabHandlerCaptor = argumentCaptor<TabHandler>()
        verify(ext).registerActionHandler(eq(customTabEngineSession), actionHandlerCaptor.capture())
        verify(ext).registerTabHandler(eq(customTabEngineSession), tabHandlerCaptor.capture())
        verify(ext).registerActionHandler(eq(engineSession), actionHandlerCaptor.capture())
        verify(ext).registerTabHandler(eq(engineSession), tabHandlerCaptor.capture())
    }

    @Test
    fun `reacts to optional permissions request`() {
        val store = BrowserStore(middleware = listOf(captureMiddleware))

        val engine: Engine = mock()
        val ext: WebExtension = mock()
        val permissions = listOf("perm1", "perm2")
        val origins = listOf("http://example.com/*", "https://example.org/*")
        val dataCollectionPermissions = listOf("locationInfo")
        val onPermissionsGranted: ((Boolean) -> Unit) = mock()
        val delegateCaptor = argumentCaptor<WebExtensionDelegate>()
        WebExtensionSupport.initialize(engine, store)
        verify(engine).registerWebExtensionDelegate(delegateCaptor.capture())

        delegateCaptor.value.onOptionalPermissionsRequest(
            ext,
            permissions,
            origins,
            dataCollectionPermissions,
            onPermissionsGranted,
        )

        captureMiddleware.assertFirstAction(WebExtensionAction.UpdatePromptRequestWebExtensionAction::class) { action ->
            assertEquals(
                WebExtensionPromptRequest.AfterInstallation.Permissions.Optional(
                    ext,
                    permissions,
                    origins,
                    dataCollectionPermissions,
                    onPermissionsGranted,
                ),
                action.promptRequest,
            )
        }
    }
}
