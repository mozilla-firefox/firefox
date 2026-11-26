/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.customtabs

import android.content.Intent
import android.os.Bundle
import android.provider.Browser
import androidx.browser.customtabs.CustomTabsIntent
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlin.test.assertIs
import kotlin.test.assertNotNull
import mozilla.components.browser.state.action.BrowserAction
import mozilla.components.browser.state.action.CustomTabListAction
import mozilla.components.browser.state.action.EngineAction
import mozilla.components.browser.state.engine.EngineMiddleware
import mozilla.components.browser.state.selector.findCustomTab
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.SessionState.Source
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.EngineSession.LoadUrlFlags
import mozilla.components.feature.intent.ext.EXTRA_SESSION_ID
import mozilla.components.feature.session.SessionUseCases
import mozilla.components.feature.tabs.CustomTabsUseCases
import mozilla.components.support.test.any
import mozilla.components.support.test.middleware.CaptureActionsMiddleware
import mozilla.components.support.test.mock
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.test.whenever
import mozilla.components.support.utils.toSafeIntent
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.ArgumentMatchers.eq
import org.mockito.Mockito.verify

@RunWith(AndroidJUnit4::class)
class CustomTabIntentProcessorTest {
    @Test
    fun processCustomTabIntentWithDefaultHandlers() {
        val middleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val store = BrowserStore(middleware = listOf(middleware) + EngineMiddleware.create(engine = mock()))
        val useCases = SessionUseCases(store)
        val customTabsUseCases = CustomTabsUseCases(store, useCases.loadUrl)

        val handler = CustomTabIntentProcessor(customTabsUseCases.add, testContext.resources)
        val intent = createCustomTabIntent()

        handler.process(intent)

        var customTabId: String? = null

        middleware.assertFirstAction(CustomTabListAction.AddCustomTabAction::class) { action ->
            customTabId = action.tab.id
        }

        middleware.assertFirstAction(EngineAction.LoadUrlAction::class) { action ->
            assertEquals(customTabId, action.tabId)
            assertEquals("http://mozilla.org", action.url)
            assertEquals(LoadUrlFlags.external(), action.flags)
        }

        verify(intent).putExtra(eq(EXTRA_SESSION_ID), any<String>())

        val customTab = store.state.findCustomTab(customTabId!!)
        assertNotNull(customTab)
        assertEquals("http://mozilla.org", customTab.content.url)
        assertIs<Source.External.CustomTab>(customTab.source)
        assertNotNull(customTab.config)
        assertFalse(customTab.content.private)
    }

    @Test
    fun processCustomTabIntentWithAdditionalHeaders() {
        val middleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val store = BrowserStore(middleware = listOf(middleware) + EngineMiddleware.create(engine = mock()))
        val useCases = SessionUseCases(store)
        val customTabsUseCases = CustomTabsUseCases(store, useCases.loadUrl)

        val handler = CustomTabIntentProcessor(customTabsUseCases.add, testContext.resources)
        val intent = createCustomTabIntent()

        val headersBundle =
            Bundle().apply {
                putString("X-Extra-Header", "true")
            }
        whenever(intent.getBundleExtra(Browser.EXTRA_HEADERS)).thenReturn(headersBundle)
        val headers = handler.getAdditionalHeaders(intent.toSafeIntent())

        handler.process(intent)

        var customTabId: String? = null

        middleware.assertFirstAction(CustomTabListAction.AddCustomTabAction::class) { action ->
            customTabId = action.tab.id
        }

        middleware.assertFirstAction(EngineAction.LoadUrlAction::class) { action ->
            assertEquals(customTabId, action.tabId)
            assertEquals("http://mozilla.org", action.url)
            assertEquals(LoadUrlFlags.external(), action.flags)
            assertEquals(headers, action.additionalHeaders)
        }

        verify(intent).putExtra(eq(EXTRA_SESSION_ID), any<String>())

        val customTab = store.state.findCustomTab(customTabId!!)
        assertNotNull(customTab)
        assertEquals("http://mozilla.org", customTab.content.url)
        assertIs<Source.External.CustomTab>(customTab.source)
        assertNotNull(customTab.config)
        assertFalse(customTab.content.private)
    }

    @Test
    fun processPrivateCustomTabIntentWithDefaultHandlers() {
        val middleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val store = BrowserStore(middleware = listOf(middleware) + EngineMiddleware.create(engine = mock()))
        val useCases = SessionUseCases(store)
        val customTabsUseCases = CustomTabsUseCases(store, useCases.loadUrl)

        val handler = CustomTabIntentProcessor(customTabsUseCases.add, testContext.resources, true)
        val intent = createCustomTabIntent()

        handler.process(intent)

        var customTabId: String? = null

        middleware.assertFirstAction(CustomTabListAction.AddCustomTabAction::class) { action ->
            customTabId = action.tab.id
        }

        middleware.assertFirstAction(EngineAction.LoadUrlAction::class) { action ->
            assertEquals(customTabId, action.tabId)
            assertEquals("http://mozilla.org", action.url)
            assertEquals(LoadUrlFlags.external(), action.flags)
        }

        verify(intent).putExtra(eq(EXTRA_SESSION_ID), any<String>())

        val customTab = store.state.findCustomTab(customTabId!!)
        assertNotNull(customTab)
        assertEquals("http://mozilla.org", customTab.content.url)
        assertIs<Source.External.CustomTab>(customTab.source)
        assertNotNull(customTab.config)
        assertTrue(customTab.content.private)
    }

    @Test
    fun `GIVEN desktop mode is enabled WHEN processing without desktopMode THEN the custom tab is in desktop mode`() {
        val store =
            BrowserStore(
                initialState = BrowserState(desktopMode = true),
                middleware = EngineMiddleware.create(engine = mock()),
            )
        val customTabsUseCases = CustomTabsUseCases(store, SessionUseCases(store).loadUrl)
        val handler = CustomTabIntentProcessor(customTabsUseCases.add, testContext.resources)

        handler.process(createCustomTabIntent())

        val tab = store.state.customTabs.single()
        assertTrue(tab.content.desktopMode)
    }

    @Test
    fun `GIVEN desktop mode is enabled WHEN processing with desktopMode true THEN the custom tab is in desktop mode`() {
        val store =
            BrowserStore(
                initialState = BrowserState(desktopMode = true),
                middleware = EngineMiddleware.create(engine = mock()),
            )
        val customTabsUseCases = CustomTabsUseCases(store, SessionUseCases(store).loadUrl)
        val handler = CustomTabIntentProcessor(customTabsUseCases.add, testContext.resources, desktopMode = true)

        handler.process(createCustomTabIntent())

        val tab = store.state.customTabs.single()
        assertTrue(tab.content.desktopMode)
    }

    @Test
    fun `GIVEN desktop mode is enabled WHEN processing with desktopMode false THEN the custom tab is not in desktop mode`() {
        val store =
            BrowserStore(
                initialState = BrowserState(desktopMode = true),
                middleware = EngineMiddleware.create(engine = mock()),
            )
        val customTabsUseCases = CustomTabsUseCases(store, SessionUseCases(store).loadUrl)
        val handler = CustomTabIntentProcessor(customTabsUseCases.add, testContext.resources, desktopMode = false)

        handler.process(createCustomTabIntent())

        val tab = store.state.customTabs.single()
        assertFalse(tab.content.desktopMode)
    }

    @Test
    fun `GIVEN desktop mode is disabled WHEN processing with desktopMode true THEN the custom tab is in desktop mode`() {
        val store =
            BrowserStore(
                initialState = BrowserState(desktopMode = false),
                middleware = EngineMiddleware.create(engine = mock()),
            )
        val customTabsUseCases = CustomTabsUseCases(store, SessionUseCases(store).loadUrl)
        val handler = CustomTabIntentProcessor(customTabsUseCases.add, testContext.resources, desktopMode = true)

        handler.process(createCustomTabIntent())

        val tab = store.state.customTabs.single()
        assertTrue(tab.content.desktopMode)
    }

    @Test
    fun `GIVEN desktop mode is disabled WHEN processing with desktopMode false THEN the custom tab is not in desktop mode`() {
        val store =
            BrowserStore(
                initialState = BrowserState(desktopMode = false),
                middleware = EngineMiddleware.create(engine = mock()),
            )
        val customTabsUseCases = CustomTabsUseCases(store, SessionUseCases(store).loadUrl)
        val handler = CustomTabIntentProcessor(customTabsUseCases.add, testContext.resources, desktopMode = false)

        handler.process(createCustomTabIntent())

        val tab = store.state.customTabs.single()
        assertFalse(tab.content.desktopMode)
    }

    private fun createCustomTabIntent(): Intent {
        val intent = mock<Intent>()
        whenever(intent.action).thenReturn(Intent.ACTION_VIEW)
        whenever(intent.hasExtra(CustomTabsIntent.EXTRA_SESSION)).thenReturn(true)
        whenever(intent.dataString).thenReturn("http://mozilla.org")
        whenever(intent.putExtra(any<String>(), any<String>())).thenReturn(intent)
        return intent
    }
}
