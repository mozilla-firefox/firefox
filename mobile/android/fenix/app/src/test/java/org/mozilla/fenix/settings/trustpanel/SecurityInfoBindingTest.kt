/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.trustpanel

import io.mockk.mockk
import java.security.cert.X509Certificate
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.action.ContentAction
import mozilla.components.browser.state.action.TabListAction
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.SecurityInfo
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.settings.trustpanel.store.TrustPanelAction
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class SecurityInfoBindingTest {
    private val testDispatcher = StandardTestDispatcher()
    private val certificate: X509Certificate = mockk()
    private val otherCertificate: X509Certificate = mockk()
    private val updates = mutableListOf<TrustPanelAction.UpdateSecurityInfo>()
    private var hostChanged = false

    @Test
    fun `WHEN binding starts THEN dispatch the security info of the session`() = runTest {
        startBinding(createBrowserStore(), sessionId = "a", urlHost = "a.example")

        assertEquals(listOf(TrustPanelAction.UpdateSecurityInfo(isSecured = true, certificate = certificate)), updates)
    }

    @Test
    fun `WHEN the session navigates from HTTPS to HTTP on the same host THEN dispatch security info without a certificate`() =
        runTest {
            val browserStore = createBrowserStore()
            startBinding(browserStore, sessionId = "a", urlHost = "a.example")

            browserStore.dispatch(ContentAction.UpdateUrlAction("a", "http://a.example/page"))
            browserStore.dispatch(ContentAction.UpdateSecurityInfoAction("a", SecurityInfo.Insecure()))
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(TrustPanelAction.UpdateSecurityInfo(isSecured = false, certificate = null), updates.last())
            assertFalse(hostChanged)
        }

    @Test
    fun `WHEN the session's certificate changes THEN dispatch security info with the new certificate`() = runTest {
        val browserStore = createBrowserStore()
        startBinding(browserStore, sessionId = "a", urlHost = "a.example")

        browserStore.dispatch(
            ContentAction.UpdateSecurityInfoAction("a", SecurityInfo.Secure(certificate = otherCertificate))
        )
        testDispatcher.scheduler.advanceUntilIdle()

        assertEquals(otherCertificate, updates.last().certificate)
    }

    @Test
    fun `WHEN the session navigates to a different host THEN report the host change instead of its security info`() =
        runTest {
            val browserStore = createBrowserStore()
            startBinding(browserStore, sessionId = "a", urlHost = "a.example")

            browserStore.dispatch(ContentAction.UpdateUrlAction("a", "https://other.example"))
            browserStore.dispatch(
                ContentAction.UpdateSecurityInfoAction("a", SecurityInfo.Secure(certificate = otherCertificate))
            )
            testDispatcher.scheduler.advanceUntilIdle()

            assertTrue(hostChanged)
            assertEquals(1, updates.size)
        }

    @Test
    fun `GIVEN the session already navigated to a different host WHEN binding starts THEN report the host change`() =
        runTest {
            startBinding(createBrowserStore(), sessionId = "a", urlHost = "previous.example")

            assertTrue(hostChanged)
            assertTrue(updates.isEmpty())
        }

    @Test
    fun `WHEN other session state changes THEN do not dispatch security info`() = runTest {
        val browserStore = createBrowserStore()
        startBinding(browserStore, sessionId = "a", urlHost = "a.example")

        browserStore.dispatch(ContentAction.UpdateTitleAction("a", "New title"))
        browserStore.dispatch(ContentAction.UpdateProgressAction("a", 50))
        testDispatcher.scheduler.advanceUntilIdle()

        assertEquals(1, updates.size)
    }

    @Test
    fun `WHEN another tab is selected or changes THEN do not dispatch its security info`() = runTest {
        val browserStore = createBrowserStore()
        startBinding(browserStore, sessionId = "a", urlHost = "a.example")

        browserStore.dispatch(TabListAction.SelectTabAction("b"))
        browserStore.dispatch(ContentAction.UpdateUrlAction("b", "https://other.example"))
        browserStore.dispatch(ContentAction.UpdateSecurityInfoAction("b", SecurityInfo.Insecure()))
        testDispatcher.scheduler.advanceUntilIdle()

        assertEquals(listOf(TrustPanelAction.UpdateSecurityInfo(isSecured = true, certificate = certificate)), updates)
        assertFalse(hostChanged)
    }

    @Test
    fun `GIVEN a private tab WHEN binding starts THEN dispatch its security info like a normal tab`() = runTest {
        startBinding(createBrowserStore(), sessionId = "private", urlHost = "private.example")

        assertEquals(otherCertificate, updates.single().certificate)
    }

    @Test
    fun `GIVEN a page without a host WHEN its URL changes without a host THEN do not report a host change`() = runTest {
        val browserStore = createBrowserStore()
        startBinding(browserStore, sessionId = "internal", urlHost = null)

        browserStore.dispatch(ContentAction.UpdateUrlAction("internal", "about:reader?url=https://a.example#top"))
        testDispatcher.scheduler.advanceUntilIdle()

        assertFalse(hostChanged)
    }

    @Test
    fun `GIVEN a page without a host WHEN the session navigates to a website THEN report the host change`() = runTest {
        val browserStore = createBrowserStore()
        startBinding(browserStore, sessionId = "internal", urlHost = null)

        browserStore.dispatch(ContentAction.UpdateUrlAction("internal", "https://a.example"))
        testDispatcher.scheduler.advanceUntilIdle()

        assertTrue(hostChanged)
    }

    private fun createBrowserStore() =
        BrowserStore(
            BrowserState(
                tabs =
                    listOf(
                        createTab(
                            url = "https://a.example",
                            id = "a",
                            securityInfo = SecurityInfo.Secure(certificate = certificate),
                        ),
                        createTab(
                            url = "https://b.example",
                            id = "b",
                            securityInfo = SecurityInfo.Secure(certificate = otherCertificate),
                        ),
                        createTab(
                            url = "https://private.example",
                            id = "private",
                            private = true,
                            securityInfo = SecurityInfo.Secure(certificate = otherCertificate),
                        ),
                        createTab(
                            url = "about:reader?url=https://a.example",
                            id = "internal",
                            securityInfo = SecurityInfo.Insecure(),
                        ),
                    ),
                selectedTabId = "a",
            )
        )

    private fun startBinding(
        browserStore: BrowserStore,
        sessionId: String,
        urlHost: String?,
    ) {
        SecurityInfoBinding(
                browserStore = browserStore,
                sessionId = sessionId,
                urlHost = urlHost,
                onSecurityInfoUpdate = { updates.add(it) },
                onHostChange = { hostChanged = true },
                mainDispatcher = testDispatcher,
            )
            .start()
        testDispatcher.scheduler.advanceUntilIdle()
    }
}
