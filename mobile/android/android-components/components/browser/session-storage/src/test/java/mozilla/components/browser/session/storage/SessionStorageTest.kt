/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.session.storage

import android.util.JsonWriter
import androidx.core.net.toUri
import androidx.test.ext.junit.runners.AndroidJUnit4
import java.io.IOException
import kotlin.test.assertNotNull
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.session.storage.facts.SessionStorageFacts
import mozilla.components.browser.state.ext.getUrl
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.EngineState
import mozilla.components.browser.state.state.ReaderState
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.state.recover.RecoverableTab
import mozilla.components.concept.engine.EngineSessionState
import mozilla.components.support.base.Component
import mozilla.components.support.base.facts.processor.CollectionProcessor
import mozilla.components.support.ktx.util.writeString
import mozilla.components.support.test.fakes.engine.FakeEngine
import mozilla.components.support.test.fakes.engine.FakeEngineSessionState
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.doReturn
import org.mockito.Mockito.spy
import org.mockito.Mockito.verify

@RunWith(AndroidJUnit4::class)
class SessionStorageTest {
    @Test
    fun `Restored browser state should contain tabs of saved state`() = runTest {
        // Build the state

        val engineSessionState1 = FakeEngineSessionState("engineState1")

        val tab1 =
            createTab("https://www.mozilla.org", id = "tab1")
                .copy(engineState = EngineState(engineSessionState = engineSessionState1))
        val tab2 = createTab("https://getpocket.com", id = "tab2", contextId = "context2")
        val tab3 = createTab("https://www.firefox.com", id = "tab3", parent = tab1)

        val state =
            BrowserState(
                tabs = listOf(tab1, tab2, tab3),
                selectedTabId = tab1.id,
            )

        // Persist the state

        val engine = FakeEngine()

        val storage = SessionStorage(testContext, engine, applicationScope = this)
        val persisted = storage.save(state)
        assertTrue(persisted)

        // Read it back

        val restoredState = storage.restore()
        assertNotNull(restoredState)

        assertEquals(3, restoredState.tabs.size)
        assertEquals("tab1", restoredState.selectedTabId)

        tab1.assertSameAs(restoredState.tabs[0])
        tab2.assertSameAs(restoredState.tabs[1])
        tab3.assertSameAs(restoredState.tabs[2])
    }

    @Test
    fun `Predicate is applied when restoring browser state`() = runTest {
        // Build the state

        val engineSessionState1 = FakeEngineSessionState("engineState1")

        val tab1 =
            createTab("https://www.mozilla.org", id = "tab1")
                .copy(engineState = EngineState(engineSessionState = engineSessionState1))
        val tab2 = createTab("https://getpocket.com", id = "tab2", contextId = "context")
        val tab3 = createTab("https://www.firefox.com", id = "tab3", parent = tab1)
        val tab4 =
            createTab(
                "https://example.com",
                id = "tab4",
                contextId = "context",
                lastAccess = System.currentTimeMillis(),
            )

        val state =
            BrowserState(
                tabs = listOf(tab1, tab2, tab3, tab4),
                selectedTabId = tab1.id,
            )

        // Persist the state

        val engine = FakeEngine()

        val storage = SessionStorage(testContext, engine, applicationScope = this)
        val persisted = storage.save(state)
        assertTrue(persisted)

        // Read it back and filter using predicate
        val restoredState = storage.restore {
            it.state.contextId == "context"
        }
        assertNotNull(restoredState)

        // Only the two "context" tabs should be restored
        assertEquals(2, restoredState.tabs.size)
        tab2.assertSameAs(restoredState.tabs[0])
        tab4.assertSameAs(restoredState.tabs[1])

        // The selected tab was not restored so the most recently accessed restored tab
        // should be selected.
        assertEquals("tab4", restoredState.selectedTabId)
    }

    @Test
    fun `Tabs with unreadable URI are not restored when restoring browser state`() = runTest {
        // Build the state

        val engineSessionState1 = FakeEngineSessionState("engineState1")

        val tab1 =
            createTab("content://readable", id = "tab1")
                .copy(engineState = EngineState(engineSessionState = engineSessionState1))
        val tab2 = createTab("content://unreadable", id = "tab2", contextId = "context")

        val state =
            BrowserState(
                tabs = listOf(tab1, tab2),
                selectedTabId = tab2.id,
            )

        // Persist the state

        val engine = FakeEngine()
        val context = testContext

        val storage = spy(SessionStorage(context, engine, applicationScope = this))
        doReturn(true).`when`(storage).isUriReadable(tab1.getUrl()!!.toUri())
        doReturn(false).`when`(storage).isUriReadable(tab2.getUrl()!!.toUri())
        val persisted = storage.save(state)
        assertTrue(persisted)

        // Read it back and filter using predicate
        val restoredState = storage.restore()
        assertNotNull(restoredState)

        // Only the tab with readable URI should be restored
        assertEquals(1, restoredState.tabs.size)
        tab1.assertSameAs(restoredState.tabs[0])

        // The selected tab was not restored so the most recently accessed restored tab
        // should be selected.
        assertEquals(tab1.id, restoredState.selectedTabId)
    }

    @Test
    fun `Saving empty state`() = runTest {
        val engine = FakeEngine()

        val storage = spy(SessionStorage(testContext, engine, applicationScope = this))
        storage.save(BrowserState())

        verify(storage).clear()

        assertNull(storage.restore())
    }

    @Test
    fun `Restore emits a fact with the duration, file size and tab counts`() = runTest {
        val state =
            BrowserState(
                tabs =
                    listOf(
                        createTab("https://www.mozilla.org", id = "tab1"),
                        createTab("https://getpocket.com", id = "tab2"),
                    ),
                selectedTabId = "tab1",
            )
        val engine = FakeEngine()
        val storage = SessionStorage(testContext, engine, applicationScope = this)
        assertTrue(storage.save(state))

        CollectionProcessor.withFactCollection { facts ->
            assertNotNull(storage.restore())

            val fact = facts.single { it.item == SessionStorageFacts.Items.RESTORE }
            assertEquals(Component.BROWSER_SESSION_STORAGE, fact.component)
            assertEquals(2, fact.metadata?.get(SessionStorageFacts.Metadata.TABS_IN_FILE))
            assertEquals(2, fact.metadata?.get(SessionStorageFacts.Metadata.TABS_RESTORED))
            assertEquals(
                getFileForEngine(testContext, engine).baseFile.length(),
                fact.metadata?.get(SessionStorageFacts.Metadata.FILE_BYTES),
            )
            assertTrue((fact.metadata?.get(SessionStorageFacts.Metadata.DURATION_MS) as Long) >= 0)
        }
    }

    @Test
    fun `Restore counts discarded tabs as tabs in the file but not as restored tabs`() = runTest {
        val state =
            BrowserState(
                tabs =
                    listOf(
                        createTab("https://www.mozilla.org", id = "tab1"),
                        createTab("https://getpocket.com", id = "tab2"),
                        createTab("https://www.firefox.com", id = "tab3"),
                    ),
                selectedTabId = "tab1",
            )
        val storage = SessionStorage(testContext, FakeEngine(), applicationScope = this)
        assertTrue(storage.save(state))

        CollectionProcessor.withFactCollection { facts ->
            val restored = storage.restore { it.state.id == "tab1" }
            assertEquals(1, restored?.tabs?.size)

            val fact = facts.single { it.item == SessionStorageFacts.Items.RESTORE }
            assertEquals(3, fact.metadata?.get(SessionStorageFacts.Metadata.TABS_IN_FILE))
            assertEquals(1, fact.metadata?.get(SessionStorageFacts.Metadata.TABS_RESTORED))
        }
    }

    @Test
    fun `Restore of a broken file emits no fact`() = runTest {
        val engine = FakeEngine()
        val storage = SessionStorage(testContext, engine, applicationScope = this)
        getFileForEngine(testContext, engine).writeString { "{" }

        CollectionProcessor.withFactCollection { facts ->
            assertNull(storage.restore())

            assertTrue(facts.isEmpty())
        }
    }

    @Test
    fun `Save emits a fact with the duration, file size and number of tabs without private tabs`() = runTest {
        val engine = FakeEngine()
        val storage = SessionStorage(testContext, engine, applicationScope = this)
        val state =
            BrowserState(
                tabs =
                    listOf(
                        createTab("https://www.mozilla.org", id = "tab1"),
                        createTab("https://getpocket.com", id = "tab2"),
                        createTab("https://www.firefox.com", id = "private", private = true),
                    )
            )

        CollectionProcessor.withFactCollection { facts ->
            assertTrue(storage.save(state))

            val fact = facts.single()
            assertEquals(SessionStorageFacts.Items.SAVE, fact.item)
            assertEquals(2, fact.metadata?.get(SessionStorageFacts.Metadata.TABS_IN_FILE))
            assertEquals(
                getFileForEngine(testContext, engine).baseFile.length(),
                fact.metadata?.get(SessionStorageFacts.Metadata.FILE_BYTES),
            )
            assertTrue((fact.metadata?.get(SessionStorageFacts.Metadata.DURATION_MS) as Long) >= 0)
        }
    }

    @Test
    fun `Saving an empty state emits no fact`() = runTest {
        val storage = SessionStorage(testContext, FakeEngine(), applicationScope = this)

        CollectionProcessor.withFactCollection { facts ->
            assertTrue(storage.save(BrowserState()))

            assertTrue(facts.isEmpty())
        }
    }

    @Test
    fun `Restore emits no fact when there is no saved session`() = runTest {
        val storage = SessionStorage(testContext, FakeEngine(), applicationScope = this)
        storage.clear()

        CollectionProcessor.withFactCollection { facts ->
            assertNull(storage.restore())

            assertTrue(facts.isEmpty())
        }
    }

    @Test
    fun `Restore emits no fact when every tab is discarded`() = runTest {
        val state =
            BrowserState(
                tabs =
                    listOf(
                        createTab("https://www.mozilla.org", id = "tab1"),
                        createTab("https://getpocket.com", id = "tab2"),
                    ),
                selectedTabId = "tab1",
            )
        val storage = SessionStorage(testContext, FakeEngine(), applicationScope = this)
        assertTrue(storage.save(state))

        CollectionProcessor.withFactCollection { facts ->
            assertNull(storage.restore { false })

            assertTrue(facts.isEmpty())
        }
    }

    @Test
    fun `Save emits no fact when the write fails`() = runTest {
        val storage = SessionStorage(testContext, FakeEngine(), applicationScope = this)
        val tab = createTabWithEngineStateThatThrows(IOException("Write failed"))

        CollectionProcessor.withFactCollection { facts ->
            assertFalse(storage.save(BrowserState(tabs = listOf(tab))))

            assertTrue(facts.isEmpty())
        }
    }

    @Test
    fun `Save emits no fact when it runs out of memory`() = runTest {
        val storage = SessionStorage(testContext, FakeEngine(), applicationScope = this)
        val tab = createTabWithEngineStateThatThrows(OutOfMemoryError())

        CollectionProcessor.withFactCollection { facts ->
            assertFalse(storage.save(BrowserState(tabs = listOf(tab))))

            assertTrue(facts.isEmpty())
        }
    }

    private fun createTabWithEngineStateThatThrows(throwable: Throwable): TabSessionState {
        val engineState =
            object : EngineSessionState {
                override fun writeTo(writer: JsonWriter) {
                    throw throwable
                }
            }
        return createTab("https://www.mozilla.org", id = "tab1")
            .copy(engineState = EngineState(engineSessionState = engineState))
    }

    @Test
    fun `Should return empty browser state after clearing`() = runTest {
        val engine = FakeEngine()

        val tab1 = createTab("https://www.mozilla.org", id = "tab1")
        val tab2 = createTab("https://getpocket.com", id = "tab2")

        val state =
            BrowserState(
                tabs = listOf(tab1, tab2),
                selectedTabId = tab1.id,
            )

        // Persist the state

        val storage = SessionStorage(testContext, engine, applicationScope = this)
        val persisted = storage.save(state)
        assertTrue(persisted)

        storage.clear()

        // Read it back. Expect null, indicating empty.

        val restoredState = storage.restore()
        assertNull(restoredState)
    }

    /**
     * This test is deserializing a version 2 snapshot taken from an actual device.
     *
     * This snapshot was written with the legacy org.json serializer implementation.
     *
     * If this test fails then this is an indication that we may have introduced a regression. We should NOT change the
     * test input to make the test pass since such an input does exist on actual devices too.
     */
    @Test
    fun deserializeVersion2BrowsingSessionLegacyOrgJson() = runTest {
        // Do not change this string! (See comment above)
        val json =
            """
            {"version":2,"selectedTabId":"c367f2ec-c456-4184-9e47-6ed102a3c32c","sessionStateTuples":[{"session":{"url":"https:\/\/www.mozilla.org\/en-US\/firefox\/","uuid":"e4bc40f1-6da5-4da2-8e32-352f2acdc2bb","parentUuid":"","title":"Firefox - Protect your life online with privacy-first products — Mozilla","readerMode":false,"lastAccess":0},"engineSession":{"GECKO_STATE":"{\"scrolldata\":{\"zoom\":{\"resolution\":1,\"displaySize\":{\"height\":1731,\"width\":1080}}},\"history\":{\"entries\":[{\"referrerInfo\":\"BBoSnxDOS9qmDeAnom1e0AAAAAAAAAAAwAAAAAAAAEYAAAAAAAEBAAAAAAEA\",\"presState\":[{\"scroll\":\"0,14962\",\"scrollOriginDowngrade\":false,\"stateKey\":\"0>3>fp>1>0>\"}],\"persist\":true,\"cacheKey\":0,\"ID\":1,\"url\":\"https:\\\/\\\/www.mozilla.org\\\/en-US\\\/\",\"title\":\"Internet for people, not profit — Mozilla\",\"loadReplace\":true,\"docIdentifier\":2147483650,\"loadReplace2\":true,\"partitionedPrincipalToInherit_base64\":\"eyIwIjp7IjAiOiJtb3otbnVsbHByaW5jaXBhbDp7ZjBlNTI0M2EtMjA3MS00NjNjLWExMzAtZmU1MTljODU2YTQxfSJ9fQ==\",\"triggeringPrincipal_base64\":\"eyIwIjp7IjAiOiJtb3otbnVsbHByaW5jaXBhbDp7MDBkNTJjYjYtYTEyOC00NjNkLWExNzItMDhjYWVhYzIzZjRkfSJ9fQ==\",\"principalToInherit_base64\":\"eyIwIjp7IjAiOiJtb3otbnVsbHByaW5jaXBhbDp7ZjBlNTI0M2EtMjA3MS00NjNjLWExMzAtZmU1MTljODU2YTQxfSJ9fQ==\",\"resultPrincipalURI\":\"https:\\\/\\\/www.mozilla.org\\\/en-US\\\/\",\"hasUserInteraction\":true,\"originalURI\":\"http:\\\/\\\/mozilla.org\\\/\",\"docshellUUID\":\"{21708b71-e22f-4996-99ff-71593e48e7c7}\"},{\"referrerInfo\":\"BBoSnxDOS9qmDeAnom1e0AAAAAAAAAAAwAAAAAAAAEYBAAAAHmh0dHBzOi8vd3d3Lm1vemlsbGEub3JnL2VuLVVTLwAAAAEBAQAAAB5odHRwczovL3d3dy5tb3ppbGxhLm9yZy9lbi1VUy8BAA==\",\"persist\":true,\"cacheKey\":0,\"ID\":2,\"csp\":\"CdntGuXUQAS\\\/4CfOuSPZrAAAAAAAAAAAwAAAAAAAAEYB3pRy0IA0EdOTmQAQS6D9QJIHOlRteE8wkTq4cYEyCMYAAAAC\\\/\\\/\\\/\\\/\\\/wAAAbsBAAAAHmh0dHBzOi8vd3d3Lm1vemlsbGEub3JnL2VuLVVTLwAAAAAAAAAFAAAACAAAAA8AAAAA\\\/\\\/\\\/\\\/\\\/wAAAAD\\\/\\\/\\\/\\\/\\\/AAAACAAAAA8AAAAXAAAABwAAABcAAAAHAAAAFwAAAAcAAAAeAAAAAAAAAAD\\\/\\\/\\\/\\\/\\\/AAAAAP\\\/\\\/\\\/\\\/8AAAAA\\\/\\\/\\\/\\\/\\\/wAAAAD\\\/\\\/\\\/\\\/\\\/AQAAAAAAAAAAACx7IjEiOnsiMCI6Imh0dHBzOi8vd3d3Lm1vemlsbGEub3JnL2VuLVVTLyJ9fQAAAAEAAAfsAGMAaABpAGwAZAAtAHMAcgBjACAAJwBzAGUAbABmACcAIABoAHQAdABwAHMAOgAvAC8AKgAuAG0AbwB6AGkAbABsAGEALgBuAGUAdAAgAGgAdAB0AHAAcwA6AC8ALwAqAC4AbQBvAHoAaQBsAGwAYQAuAG8AcgBnACAAaAB0AHQAcABzADoALwAvACoALgBtAG8AegBpAGwAbABhAC4AYwBvAG0AIABoAHQAdABwAHMAOgAvAC8AdwB3AHcALgBnAG8AbwBnAGwAZQB0AGEAZwBtAGEAbgBhAGcAZQByAC4AYwBvAG0AIABoAHQAdABwAHMAOgAvAC8AdwB3AHcALgBnAG8AbwBnAGwAZQAtAGEAbgBhAGwAeQB0AGkAYwBzAC4AYwBvAG0AIABoAHQAdABwAHMAOgAvAC8AdwB3AHcALgB5AG8AdQB0AHUAYgBlAC0AbgBvAGMAbwBvAGsAaQBlAC4AYwBvAG0AIABoAHQAdABwAHMAOgAvAC8AdAByAGEAYwBrAGUAcgB0AGUAcwB0AC4AbwByAGcAIABoAHQAdABwAHMAOgAvAC8AdwB3AHcALgBzAHUAcgB2AGUAeQBnAGkAegBtAG8ALgBjAG8AbQAgAGgAdAB0AHAAcwA6AC8ALwBhAGMAYwBvAHUAbgB0AHMALgBmAGkAcgBlAGYAbwB4AC4AYwBvAG0AIABoAHQAdABwAHMAOgAvAC8AYQBjAGMAbwB1AG4AdABzAC4AZgBpAHIAZQBmAG8AeAAuAGMAbwBtAC4AYwBuACAAaAB0AHQAcABzADoALwAvAHcAdwB3AC4AeQBvAHUAdAB1AGIAZQAuAGMAbwBtADsAIABzAGMAcgBpAHAAdAAtAHMAcgBjACAAJwBzAGUAbABmACcAIABoAHQAdABwAHMAOgAvAC8AKgAuAG0AbwB6AGkAbABsAGEALgBuAGUAdAAgAGgAdAB0AHAAcwA6AC8ALwAqAC4AbQBvAHoAaQBsAGwAYQAuAG8AcgBnACAAaAB0AHQAcABzADoALwAvACoALgBtAG8AegBpAGwAbABhAC4AYwBvAG0AIAAnAHUAbgBzAGEAZgBlAC0AaQBuAGwAaQBuAGUAJwAgACcAdQBuAHMAYQBmAGUALQBlAHYAYQBsACcAIABoAHQAdABwAHMAOgAvAC8AdwB3AHcALgBnAG8AbwBnAGwAZQB0AGEAZwBtAGEAbgBhAGcAZQByAC4AYwBvAG0AIABoAHQAdABwAHMAOgAvAC8AdwB3AHcALgBnAG8AbwBnAGwAZQAtAGEAbgBhAGwAeQB0AGkAYwBzAC4AYwBvAG0AIABoAHQAdABwAHMAOgAvAC8AdABhAGcAbQBhAG4AYQBnAGUAcgAuAGcAbwBvAGcAbABlAC4AYwBvAG0AIABoAHQAdABwAHMAOgAvAC8AdwB3AHcALgB5AG8AdQB0AHUAYgBlAC4AYwBvAG0AIABoAHQAdABwAHMAOgAvAC8AcwAuAHkAdABpAG0AZwAuAGMAbwBtACAAaAB0AHQAcABzADoALwAvAGMAZABuAC0AMwAuAGMAbwBuAHYAZQByAHQAZQB4AHAAZQByAGkAbQBlAG4AdABzAC4AYwBvAG0AIABoAHQAdABwAHMAOgAvAC8AYQBwAHAALgBjAG8AbgB2AGUAcgB0AC4AYwBvAG0AIABoAHQAdABwAHMAOgAvAC8AZABhAHQAYQAuAHQAcgBhAGMAawAuAGMAbwBuAHYAZQByAHQAZQB4AHAAZQByAGkAbQBlAG4AdABzAC4AYwBvAG0AIABoAHQAdABwAHMAOgAvAC8AMQAwADAAMwAzADUAMAAuAHQAcgBhAGMAawAuAGMAbwBuAHYAZQByAHQAZQB4AHAAZQByAGkAbQBlAG4AdABzAC4AYwBvAG0AIABoAHQAdABwAHMAOgAvAC8AMQAwADAAMwAzADQAMwAuAHQAcgBhAGMAawAuAGMAbwBuAHYAZQByAHQAZQB4AHAAZQByAGkAbQBlAG4AdABzAC4AYwBvAG0AOwAgAGkAbQBnAC0AcwByAGMAIAAnAHMAZQBsAGYAJwAgAGgAdAB0AHAAcwA6AC8ALwAqAC4AbQBvAHoAaQBsAGwAYQAuAG4AZQB0ACAAaAB0AHQAcABzADoALwAvACoALgBtAG8AegBpAGwAbABhAC4AbwByAGcAIABoAHQAdABwAHMAOgAvAC8AKgAuAG0AbwB6AGkAbABsAGEALgBjAG8AbQAgAGQAYQB0AGEAOgAgAGgAdAB0AHAAcwA6AC8ALwBtAG8AegBpAGwAbABhAC4AbwByAGcAIABoAHQAdABwAHMAOgAvAC8AdwB3AHcALgBnAG8AbwBnAGwAZQB0AGEAZwBtAGEAbgBhAGcAZQByAC4AYwBvAG0AIABoAHQAdABwAHMAOgAvAC8AdwB3AHcALgBnAG8AbwBnAGwAZQAtAGEAbgBhAGwAeQB0AGkAYwBzAC4AYwBvAG0AIABoAHQAdABwAHMAOgAvAC8AYQBkAHMAZQByAHYAaQBjAGUALgBnAG8AbwBnAGwAZQAuAGMAbwBtACAAaAB0AHQAcABzADoALwAvAGEAZABzAGUAcgB2AGkAYwBlAC4AZwBvAG8AZwBsAGUALgBkAGUAIABoAHQAdABwAHMAOgAvAC8AYQBkAHMAZQByAHYAaQBjAGUALgBnAG8AbwBnAGwAZQAuAGQAawAgAGgAdAB0AHAAcwA6AC8ALwBjAHIAZQBhAHQAaQB2AGUAYwBvAG0AbQBvAG4AcwAuAG8AcgBnACAAaAB0AHQAcABzADoALwAvAGMAZABuAC0AMwAuAGMAbwBuAHYAZQByAHQAZQB4AHAAZQByAGkAbQBlAG4AdABzAC4AYwBvAG0AIABoAHQAdABwAHMAOgAvAC8AbABvAGcAcwAuAGMAbwBuAHYAZQByAHQAZQB4AHAAZQByAGkAbQBlAG4AdABzAC4AYwBvAG0AIABoAHQAdABwAHMAOgAvAC8AYQBkAC4AZABvAHUAYgBsAGUAYwBsAGkAYwBrAC4AbgBlAHQAOwAgAGYAcgBhAG0AZQAtAHMAcgBjACAAJwBzAGUAbABmACcAIABoAHQAdABwAHMAOgAvAC8AKgAuAG0AbwB6AGkAbABsAGEALgBuAGUAdAAgAGgAdAB0AHAAcwA6AC8ALwAqAC4AbQBvAHoAaQBsAGwAYQAuAG8AcgBnACAAaAB0AHQAcABzADoALwAvACoALgBtAG8AegBpAGwAbABhAC4AYwBvAG0AIABoAHQAdABwAHMAOgAvAC8AdwB3AHcALgBnAG8AbwBnAGwAZQB0AGEAZwBtAGEAbgBhAGcAZQByAC4AYwBvAG0AIABoAHQAdABwAHMAOgAvAC8AdwB3AHcALgBnAG8AbwBnAGwAZQAtAGEAbgBhAGwAeQB0AGkAYwBzAC4AYwBvAG0AIABoAHQAdABwAHMAOgAvAC8AdwB3AHcALgB5AG8AdQB0AHUAYgBlAC0AbgBvAGMAbwBvAGsAaQBlAC4AYwBvAG0AIABoAHQAdABwAHMAOgAvAC8AdAByAGEAYwBrAGUAcgB0AGUAcwB0AC4AbwByAGcAIABoAHQAdABwAHMAOgAvAC8AdwB3AHcALgBzAHUAcgB2AGUAeQBnAGkAegBtAG8ALgBjAG8AbQAgAGgAdAB0AHAAcwA6AC8ALwBhAGMAYwBvAHUAbgB0AHMALgBmAGkAcgBlAGYAbwB4AC4AYwBvAG0AIABoAHQAdABwAHMAOgAvAC8AYQBjAGMAbwB1AG4AdABzAC4AZgBpAHIAZQBmAG8AeAAuAGMAbwBtAC4AYwBuACAAaAB0AHQAcABzADoALwAvAHcAdwB3AC4AeQBvAHUAdAB1AGIAZQAuAGMAbwBtADsAIABzAHQAeQBsAGUALQBzAHIAYwAgACcAcwBlAGwAZgAnACAAaAB0AHQAcABzADoALwAvACoALgBtAG8AegBpAGwAbABhAC4AbgBlAHQAIABoAHQAdABwAHMAOgAvAC8AKgAuAG0AbwB6AGkAbABsAGEALgBvAHIAZwAgAGgAdAB0AHAAcwA6AC8ALwAqAC4AbQBvAHoAaQBsAGwAYQAuAGMAbwBtACAAJwB1AG4AcwBhAGYAZQAtAGkAbgBsAGkAbgBlACcAIABoAHQAdABwAHMAOgAvAC8AYQBwAHAALgBjAG8AbgB2AGUAcgB0AC4AYwBvAG0AOwAgAGMAbwBuAG4AZQBjAHQALQBzAHIAYwAgACcAcwBlAGwAZgAnACAAaAB0AHQAcABzADoALwAvACoALgBtAG8AegBpAGwAbABhAC4AbgBlAHQAIABoAHQAdABwAHMAOgAvAC8AKgAuAG0AbwB6AGkAbABsAGEALgBvAHIAZwAgAGgAdAB0AHAAcwA6AC8ALwAqAC4AbQBvAHoAaQBsAGwAYQAuAGMAbwBtACAAaAB0AHQAcABzADoALwAvAHcAdwB3AC4AZwBvAG8AZwBsAGUAdABhAGcAbQBhAG4AYQBnAGUAcgAuAGMAbwBtACAAaAB0AHQAcABzADoALwAvAHcAdwB3AC4AZwBvAG8AZwBsAGUALQBhAG4AYQBsAHkAdABpAGMAcwAuAGMAbwBtACAAaAB0AHQAcABzADoALwAvAGwAbwBnAHMALgBjAG8AbgB2AGUAcgB0AGUAeABwAGUAcgBpAG0AZQBuAHQAcwAuAGMAbwBtACAAaAB0AHQAcABzADoALwAvADEAMAAwADMAMwA1ADAALgBtAGUAdAByAGkAYwBzAC4AYwBvAG4AdgBlAHIAdABlAHgAcABlAHIAaQBtAGUAbgB0AHMALgBjAG8AbQAgAGgAdAB0AHAAcwA6AC8ALwAxADAAMAAzADMANAAzAC4AbQBlAHQAcgBpAGMAcwAuAGMAbwBuAHYAZQByAHQAZQB4AHAAZQByAGkAbQBlAG4AdABzAC4AYwBvAG0AIABoAHQAdABwAHMAOgAvAC8AYQBjAGMAbwB1AG4AdABzAC4AZgBpAHIAZQBmAG8AeAAuAGMAbwBtAC8AIABoAHQAdABwAHMAOgAvAC8AYQBjAGMAbwB1AG4AdABzAC4AZgBpAHIAZQBmAG8AeAAuAGMAbwBtAC4AYwBuAC8AOwAgAGQAZQBmAGEAdQBsAHQALQBzAHIAYwAgACcAcwBlAGwAZgAnACAAaAB0AHQAcABzADoALwAvACoALgBtAG8AegBpAGwAbABhAC4AbgBlAHQAIABoAHQAdABwAHMAOgAvAC8AKgAuAG0AbwB6AGkAbABsAGEALgBvAHIAZwAgAGgAdAB0AHAAcwA6AC8ALwAqAC4AbQBvAHoAaQBsAGwAYQAuAGMAbwBtAAA=\",\"url\":\"https:\\\/\\\/www.mozilla.org\\\/en-US\\\/firefox\\\/\",\"title\":\"Firefox - Protect your life online with privacy-first products — Mozilla\",\"loadReplace\":false,\"docIdentifier\":2147483651,\"loadReplace2\":true,\"partitionedPrincipalToInherit_base64\":\"eyIxIjp7IjAiOiJodHRwczovL3d3dy5tb3ppbGxhLm9yZy9lbi1VUy8ifX0=\",\"triggeringPrincipal_base64\":\"eyIxIjp7IjAiOiJodHRwczovL3d3dy5tb3ppbGxhLm9yZy9lbi1VUy8ifX0=\",\"principalToInherit_base64\":\"eyIxIjp7IjAiOiJodHRwczovL3d3dy5tb3ppbGxhLm9yZy9lbi1VUy8ifX0=\",\"resultPrincipalURI\":\"https:\\\/\\\/www.mozilla.org\\\/en-US\\\/firefox\\\/\",\"hasUserInteraction\":false,\"originalURI\":\"https:\\\/\\\/www.mozilla.org\\\/en-US\\\/firefox\\\/\",\"docshellUUID\":\"{21708b71-e22f-4996-99ff-71593e48e7c7}\"}],\"requestedIndex\":0,\"fromIdx\":-1,\"index\":2,\"userContextId\":0}}"}},{"session":{"url":"https:\/\/en.m.wikipedia.org\/wiki\/Mona_Lisa","uuid":"c367f2ec-c456-4184-9e47-6ed102a3c32c","parentUuid":"","title":"Mona Lisa - Wikipedia","readerMode":false,"lastAccess":1599582163154},"engineSession":{"GECKO_STATE":"{\"scrolldata\":{\"zoom\":{\"resolution\":1,\"displaySize\":{\"height\":1731,\"width\":1080}}},\"history\":{\"entries\":[{\"referrerInfo\":\"BBoSnxDOS9qmDeAnom1e0AAAAAAAAAAAwAAAAAAAAEYAAAAAAAEBAAAAAAEA\",\"presState\":[{\"scroll\":\"0,14352\",\"scrollOriginDowngrade\":false,\"stateKey\":\"0>3>fp>0>4>\"}],\"persist\":true,\"cacheKey\":0,\"ID\":4,\"url\":\"https:\\\/\\\/www.wikipedia.org\\\/\",\"title\":\"Wikipedia\",\"loadReplace\":true,\"docIdentifier\":2147483653,\"loadReplace2\":true,\"partitionedPrincipalToInherit_base64\":\"eyIwIjp7IjAiOiJtb3otbnVsbHByaW5jaXBhbDp7MjFhYTQzNjEtZDcyZi00ZDUyLThjYjUtM2ZiMzlkOTg2OWFmfSJ9fQ==\",\"triggeringPrincipal_base64\":\"eyIwIjp7IjAiOiJtb3otbnVsbHByaW5jaXBhbDp7NTgxYWY4MTUtZDg2Yy00MTQ3LTg2ODMtN2U2YWJhMjYzYjlkfSJ9fQ==\",\"principalToInherit_base64\":\"eyIwIjp7IjAiOiJtb3otbnVsbHByaW5jaXBhbDp7MjFhYTQzNjEtZDcyZi00ZDUyLThjYjUtM2ZiMzlkOTg2OWFmfSJ9fQ==\",\"resultPrincipalURI\":\"https:\\\/\\\/www.wikipedia.org\\\/\",\"hasUserInteraction\":true,\"originalURI\":\"http:\\\/\\\/wikipedia.org\\\/\",\"docshellUUID\":\"{37d2c68a-4432-4246-8427-ad1348ca07e9}\"},{\"referrerInfo\":\"BBoSnxDOS9qmDeAnom1e0AAAAAAAAAAAwAAAAAAAAEYBAAAAGmh0dHBzOi8vd3d3Lndpa2lwZWRpYS5vcmcvAAAAAQEBAAAAGmh0dHBzOi8vd3d3Lndpa2lwZWRpYS5vcmcvAQA=\",\"persist\":true,\"cacheKey\":0,\"ID\":5,\"csp\":\"CdntGuXUQAS\\\/4CfOuSPZrAAAAAAAAAAAwAAAAAAAAEYB3pRy0IA0EdOTmQAQS6D9QJIHOlRteE8wkTq4cYEyCMYAAAAC\\\/\\\/\\\/\\\/\\\/wAAAbsBAAAAGmh0dHBzOi8vd3d3Lndpa2lwZWRpYS5vcmcvAAAAAAAAAAUAAAAIAAAAEQAAAAD\\\/\\\/\\\/\\\/\\\/AAAAAP\\\/\\\/\\\/\\\/8AAAAIAAAAEQAAABkAAAABAAAAGQAAAAEAAAAZAAAAAQAAABoAAAAAAAAAAP\\\/\\\/\\\/\\\/8AAAAA\\\/\\\/\\\/\\\/\\\/wAAAAD\\\/\\\/\\\/\\\/\\\/AAAAAP\\\/\\\/\\\/\\\/8BAAAAAAAAAAAAKHsiMSI6eyIwIjoiaHR0cHM6Ly93d3cud2lraXBlZGlhLm9yZy8ifX0AAAAA\",\"url\":\"https:\\\/\\\/en.m.wikipedia.org\\\/wiki\\\/Main_Page\",\"title\":\"Wikipedia, the free encyclopedia\",\"loadReplace\":true,\"docIdentifier\":2147483654,\"loadReplace2\":true,\"partitionedPrincipalToInherit_base64\":\"eyIwIjp7IjAiOiJtb3otbnVsbHByaW5jaXBhbDp7N2RkZmU5MDQtNjgyMC00MThiLTg0MWEtNTMwYWIzMmUxZDBjfSJ9fQ==\",\"triggeringPrincipal_base64\":\"eyIxIjp7IjAiOiJodHRwczovL3d3dy53aWtpcGVkaWEub3JnLyJ9fQ==\",\"principalToInherit_base64\":\"eyIwIjp7IjAiOiJtb3otbnVsbHByaW5jaXBhbDp7N2RkZmU5MDQtNjgyMC00MThiLTg0MWEtNTMwYWIzMmUxZDBjfSJ9fQ==\",\"resultPrincipalURI\":\"https:\\\/\\\/en.m.wikipedia.org\\\/wiki\\\/Main_Page\",\"hasUserInteraction\":true,\"originalURI\":\"https:\\\/\\\/en.wikipedia.org\\\/\",\"docshellUUID\":\"{37d2c68a-4432-4246-8427-ad1348ca07e9}\"},{\"referrerInfo\":\"BBoSnxDOS9qmDeAnom1e0AAAAAAAAAAAwAAAAAAAAEYBAAAAKWh0dHBzOi8vZW4ubS53aWtpcGVkaWEub3JnL3dpa2kvTWFpbl9QYWdlAAAABAEBAAAAKWh0dHBzOi8vZW4ubS53aWtpcGVkaWEub3JnL3dpa2kvTWFpbl9QYWdlAQA=\",\"persist\":true,\"cacheKey\":0,\"ID\":6,\"csp\":\"CdntGuXUQAS\\\/4CfOuSPZrAAAAAAAAAAAwAAAAAAAAEYB3pRy0IA0EdOTmQAQS6D9QJIHOlRteE8wkTq4cYEyCMYAAAAC\\\/\\\/\\\/\\\/\\\/wAAAbsBAAAAKWh0dHBzOi8vZW4ubS53aWtpcGVkaWEub3JnL3dpa2kvTWFpbl9QYWdlAAAAAAAAAAUAAAAIAAAAEgAAAAD\\\/\\\/\\\/\\\/\\\/AAAAAP\\\/\\\/\\\/\\\/8AAAAIAAAAEgAAABoAAAAPAAAAGgAAAA8AAAAaAAAABgAAACAAAAAJAAAAAP\\\/\\\/\\\/\\\/8AAAAA\\\/\\\/\\\/\\\/\\\/wAAAAD\\\/\\\/\\\/\\\/\\\/AAAAAP\\\/\\\/\\\/\\\/8BAAAAAAAAAAAAN3siMSI6eyIwIjoiaHR0cHM6Ly9lbi5tLndpa2lwZWRpYS5vcmcvd2lraS9NYWluX1BhZ2UifX0AAAAA\",\"url\":\"https:\\\/\\\/en.m.wikipedia.org\\\/wiki\\\/Portal:Arts\",\"title\":\"Portal:Arts - Wikipedia\",\"loadReplace\":false,\"docIdentifier\":2147483655,\"loadReplace2\":true,\"partitionedPrincipalToInherit_base64\":\"eyIxIjp7IjAiOiJodHRwczovL2VuLm0ud2lraXBlZGlhLm9yZy93aWtpL01haW5fUGFnZSJ9fQ==\",\"triggeringPrincipal_base64\":\"eyIxIjp7IjAiOiJodHRwczovL2VuLm0ud2lraXBlZGlhLm9yZy93aWtpL01haW5fUGFnZSJ9fQ==\",\"principalToInherit_base64\":\"eyIxIjp7IjAiOiJodHRwczovL2VuLm0ud2lraXBlZGlhLm9yZy93aWtpL01haW5fUGFnZSJ9fQ==\",\"resultPrincipalURI\":\"https:\\\/\\\/en.m.wikipedia.org\\\/wiki\\\/Portal:Arts\",\"hasUserInteraction\":true,\"originalURI\":\"https:\\\/\\\/en.m.wikipedia.org\\\/wiki\\\/Portal:Arts\",\"docshellUUID\":\"{37d2c68a-4432-4246-8427-ad1348ca07e9}\"},{\"referrerInfo\":\"BBoSnxDOS9qmDeAnom1e0AAAAAAAAAAAwAAAAAAAAEYBAAAAK2h0dHBzOi8vZW4ubS53aWtpcGVkaWEub3JnL3dpa2kvUG9ydGFsOkFydHMAAAAEAQEAAAAraHR0cHM6Ly9lbi5tLndpa2lwZWRpYS5vcmcvd2lraS9Qb3J0YWw6QXJ0cwEA\",\"persist\":true,\"cacheKey\":0,\"ID\":7,\"csp\":\"CdntGuXUQAS\\\/4CfOuSPZrAAAAAAAAAAAwAAAAAAAAEYB3pRy0IA0EdOTmQAQS6D9QJIHOlRteE8wkTq4cYEyCMYAAAAC\\\/\\\/\\\/\\\/\\\/wAAAbsBAAAAK2h0dHBzOi8vZW4ubS53aWtpcGVkaWEub3JnL3dpa2kvUG9ydGFsOkFydHMAAAAAAAAABQAAAAgAAAASAAAAAP\\\/\\\/\\\/\\\/8AAAAA\\\/\\\/\\\/\\\/\\\/wAAAAgAAAASAAAAGgAAABEAAAAaAAAAEQAAABoAAAAGAAAAIAAAAAsAAAAA\\\/\\\/\\\/\\\/\\\/wAAAAD\\\/\\\/\\\/\\\/\\\/AAAAAP\\\/\\\/\\\/\\\/8AAAAA\\\/\\\/\\\/\\\/\\\/wEAAAAAAAAAAAA5eyIxIjp7IjAiOiJodHRwczovL2VuLm0ud2lraXBlZGlhLm9yZy93aWtpL1BvcnRhbDpBcnRzIn19AAAAAA==\",\"url\":\"https:\\\/\\\/en.m.wikipedia.org\\\/wiki\\\/Mona_Lisa\",\"title\":\"Mona Lisa - Wikipedia\",\"loadReplace\":false,\"docIdentifier\":2147483656,\"loadReplace2\":true,\"partitionedPrincipalToInherit_base64\":\"eyIxIjp7IjAiOiJodHRwczovL2VuLm0ud2lraXBlZGlhLm9yZy93aWtpL1BvcnRhbDpBcnRzIn19\",\"triggeringPrincipal_base64\":\"eyIxIjp7IjAiOiJodHRwczovL2VuLm0ud2lraXBlZGlhLm9yZy93aWtpL1BvcnRhbDpBcnRzIn19\",\"principalToInherit_base64\":\"eyIxIjp7IjAiOiJodHRwczovL2VuLm0ud2lraXBlZGlhLm9yZy93aWtpL1BvcnRhbDpBcnRzIn19\",\"resultPrincipalURI\":\"https:\\\/\\\/en.m.wikipedia.org\\\/wiki\\\/Mona_Lisa\",\"hasUserInteraction\":false,\"originalURI\":\"https:\\\/\\\/en.m.wikipedia.org\\\/wiki\\\/Mona_Lisa\",\"docshellUUID\":\"{37d2c68a-4432-4246-8427-ad1348ca07e9}\"}],\"requestedIndex\":0,\"fromIdx\":-1,\"index\":4,\"userContextId\":0}}"}}]}
            """
                .trimIndent()

        val engine = FakeEngine(expectToRestoreRealEngineSessionState = true)

        assertTrue(getFileForEngine(testContext, engine).writeString { json })

        val storage = SessionStorage(testContext, engine, applicationScope = this)
        val browsingSession = storage.restore()

        assertNotNull(browsingSession)
        assertEquals(2, browsingSession.tabs.size)

        browsingSession.tabs[0].state.apply {
            assertEquals("https://www.mozilla.org/en-US/firefox/", url)
            assertEquals(
                "Firefox - Protect your life online with privacy-first products — Mozilla",
                title,
            )
            assertEquals("e4bc40f1-6da5-4da2-8e32-352f2acdc2bb", id)
            assertNull(parentId)
            assertEquals(0, lastAccess)
            assertNotNull(readerState)
            assertFalse(readerState.active)
        }

        browsingSession.tabs[1].state.apply {
            assertEquals("https://en.m.wikipedia.org/wiki/Mona_Lisa", url)
            assertEquals("Mona Lisa - Wikipedia", title)
            assertEquals("c367f2ec-c456-4184-9e47-6ed102a3c32c", id)
            assertNull(parentId)
            assertEquals(1599582163154, lastAccess)
            assertNotNull(readerState)
            assertFalse(readerState.active)
        }
    }

    /**
     * This test is deserializing a version 2 snapshot taken from an actual device.
     *
     * This snapshot was written with the JsonWriter serializer implementation.
     *
     * If this test fails then this is an indication that we may have introduced a regression. We should NOT change the
     * test input to make the test pass since such an input does exist on actual devices too.
     */
    @Test
    fun deserializeVersion2BrowsingSessionJsonWriter() = runTest {
        // Do not change this string! (See comment above)
        val json =
            """
            {"version":2,"selectedTabId":"d6facb8a-0775-45a1-8bc1-2397e2d2bc53","sessionStateTuples":[{"session":{"url":"https://www.theverge.com/","uuid":"28f428ba-2b19-4c24-993b-763fda2be65c","parentUuid":"","title":"The Verge","contextId":null,"readerMode":false,"lastAccess":1599815361779},"engineSession":{"GECKO_STATE":"{\"scrolldata\":{\"scroll\":\"0,1074\",\"zoom\":{\"resolution\":1,\"displaySize\":{\"height\":1584,\"width\":1080}}},\"history\":{\"entries\":[{\"referrerInfo\":\"BBoSnxDOS9qmDeAnom1e0AAAAAAAAAAAwAAAAAAAAEYAAAAAAAEBAAAAAAEA\",\"persist\":true,\"cacheKey\":0,\"ID\":2086656668,\"url\":\"https:\\/\\/www.theverge.com\\/\",\"title\":\"The Verge\",\"structuredCloneVersion\":8,\"docIdentifier\":2147483649,\"structuredCloneState\":\"AgAAAAAA8f8AAAAACAD\\/\\/wAAAAATAP\\/\\/\",\"partitionedPrincipalToInherit_base64\":\"eyIwIjp7IjAiOiJtb3otbnVsbHByaW5jaXBhbDp7MGU4ZmNlMTQtNDE5Yi00MWQ5LTg2YjQtMGVlM2VkYmE5Zjc4fSJ9fQ==\",\"triggeringPrincipal_base64\":\"eyIwIjp7IjAiOiJtb3otbnVsbHByaW5jaXBhbDp7OGM1NTlmYWQtMmRjZC00NjY5LWE5MjEtODViYTFiODBmNWJhfSJ9fQ==\",\"principalToInherit_base64\":\"eyIwIjp7IjAiOiJtb3otbnVsbHByaW5jaXBhbDp7MGU4ZmNlMTQtNDE5Yi00MWQ5LTg2YjQtMGVlM2VkYmE5Zjc4fSJ9fQ==\",\"resultPrincipalURI\":null,\"hasUserInteraction\":true,\"originalURI\":\"https:\\/\\/www.theverge.com\\/\",\"docshellUUID\":\"{9464a0d3-6687-4179-a3a4-15817791801d}\"}],\"requestedIndex\":0,\"fromIdx\":-1,\"index\":1,\"userContextId\":0}}"}},{"session":{"url":"https://www.theverge.com/2020/9/10/21429838/google-android-11-go-edition-devices-2gb-ram-20-percent","uuid":"d6facb8a-0775-45a1-8bc1-2397e2d2bc53","parentUuid":"28f428ba-2b19-4c24-993b-763fda2be65c","title":"Android 11 Go is available today, and it will launch apps 20 percent faster","contextId":null,"readerMode":true,"lastAccess":1599815364500,"readerModeArticleUrl":"https://www.theverge.com/2020/9/10/21429838/google-android-11-go-edition-devices-2gb-ram-20-percent"},"engineSession":{"GECKO_STATE":"{\"scrolldata\":{\"zoom\":{\"resolution\":1,\"displaySize\":{\"height\":1731,\"width\":1080}}},\"history\":{\"entries\":[{\"referrerInfo\":\"BBoSnxDOS9qmDeAnom1e0AAAAAAAAAAAwAAAAAAAAEYBAAAAGWh0dHBzOi8vd3d3LnRoZXZlcmdlLmNvbS8AAAAIAQEAAAAZaHR0cHM6Ly93d3cudGhldmVyZ2UuY29tLwEA\",\"persist\":true,\"cacheKey\":0,\"ID\":2087434596,\"csp\":\"CdntGuXUQAS\\/4CfOuSPZrAAAAAAAAAAAwAAAAAAAAEYB3pRy0IA0EdOTmQAQS6D9QJIHOlRteE8wkTq4cYEyCMYAAAAC\\/\\/\\/\\/\\/wAAAbsBAAAAGWh0dHBzOi8vd3d3LnRoZXZlcmdlLmNvbS8AAAAAAAAABQAAAAgAAAAQAAAACP\\/\\/\\/\\/8AAAAI\\/\\/\\/\\/\\/wAAAAgAAAAQAAAAGAAAAAEAAAAYAAAAAQAAABgAAAABAAAAGQAAAAAAAAAZ\\/\\/\\/\\/\\/wAAAAD\\/\\/\\/\\/\\/AAAAGP\\/\\/\\/\\/8AAAAY\\/\\/\\/\\/\\/wEAAAAAAAAAAAAneyIxIjp7IjAiOiJodHRwczovL3d3dy50aGV2ZXJnZS5jb20vIn19AAAAAQAAAWsAZABlAGYAYQB1AGwAdAAtAHMAcgBjACAAaAB0AHQAcABzADoAIABkAGEAdABhADoAIAAnAHUAbgBzAGEAZgBlAC0AaQBuAGwAaQBuAGUAJwAgACcAdQBuAHMAYQBmAGUALQBlAHYAYQBsACcAOwAgAGMAaABpAGwAZAAtAHMAcgBjACAAaAB0AHQAcABzADoAIABkAGEAdABhADoAIABiAGwAbwBiADoAOwAgAGMAbwBuAG4AZQBjAHQALQBzAHIAYwAgAGgAdAB0AHAAcwA6ACAAZABhAHQAYQA6ACAAYgBsAG8AYgA6ADsAIABmAG8AbgB0AC0AcwByAGMAIABoAHQAdABwAHMAOgAgAGQAYQB0AGEAOgA7ACAAaQBtAGcALQBzAHIAYwAgAGgAdAB0AHAAcwA6ACAAZABhAHQAYQA6ACAAYgBsAG8AYgA6ADsAIABtAGUAZABpAGEALQBzAHIAYwAgAGgAdAB0AHAAcwA6ACAAZABhAHQAYQA6ACAAYgBsAG8AYgA6ADsAIABvAGIAagBlAGMAdAAtAHMAcgBjACAAaAB0AHQAcABzADoAOwAgAHMAYwByAGkAcAB0AC0AcwByAGMAIABoAHQAdABwAHMAOgAgAGQAYQB0AGEAOgAgAGIAbABvAGIAOgAgACcAdQBuAHMAYQBmAGUALQBpAG4AbABpAG4AZQAnACAAJwB1AG4AcwBhAGYAZQAtAGUAdgBhAGwAJwA7ACAAcwB0AHkAbABlAC0AcwByAGMAIABoAHQAdABwAHMAOgAgACcAdQBuAHMAYQBmAGUALQBpAG4AbABpAG4AZQAnADsAIABiAGwAbwBjAGsALQBhAGwAbAAtAG0AaQB4AGUAZAAtAGMAbwBuAHQAZQBuAHQAOwAgAHUAcABnAHIAYQBkAGUALQBpAG4AcwBlAGMAdQByAGUALQByAGUAcQB1AGUAcwB0AHMAAA==\",\"url\":\"https:\\/\\/www.theverge.com\\/2020\\/9\\/10\\/21429838\\/google-android-11-go-edition-devices-2gb-ram-20-percent\",\"title\":\"Android 11 Go is available today, and it will launch apps 20 percent faster - The Verge\",\"structuredCloneVersion\":8,\"docIdentifier\":7,\"structuredCloneState\":\"AgAAAAAA8f8AAAAACAD\\/\\/wAAAAATAP\\/\\/\",\"partitionedPrincipalToInherit_base64\":\"eyIwIjp7IjAiOiJtb3otbnVsbHByaW5jaXBhbDp7ZjQ4NDNkYjYtMDlmYi00ZWJiLTg4ODQtMjE4MzdjNWNhOTIwfSJ9fQ==\",\"triggeringPrincipal_base64\":\"eyIxIjp7IjAiOiJodHRwczovL3d3dy50aGV2ZXJnZS5jb20vIn19\",\"principalToInherit_base64\":\"eyIwIjp7IjAiOiJtb3otbnVsbHByaW5jaXBhbDp7ZjQ4NDNkYjYtMDlmYi00ZWJiLTg4ODQtMjE4MzdjNWNhOTIwfSJ9fQ==\",\"resultPrincipalURI\":null,\"hasUserInteraction\":false,\"originalURI\":\"https:\\/\\/www.theverge.com\\/2020\\/9\\/10\\/21429838\\/google-android-11-go-edition-devices-2gb-ram-20-percent\",\"docshellUUID\":\"{22a1a76f-6d3c-496a-bbd7-345bc32a8ba9}\"},{\"persist\":true,\"cacheKey\":0,\"ID\":7,\"url\":\"moz-extension:\\/\\/249b38c7-13b9-45de-b5ca-93a37c7321e1\\/readerview.html?url=https%3A%2F%2Fwww.theverge.com%2F2020%2F9%2F10%2F21429838%2Fgoogle-android-11-go-edition-devices-2gb-ram-20-percent&id=137438953484\",\"title\":\"Android 11 Go is available today, and it will launch apps 20 percent faster\",\"docIdentifier\":8,\"partitionedPrincipalToInherit_base64\":\"eyIxIjp7IjAiOiJtb3otZXh0ZW5zaW9uOi8vMjQ5YjM4YzctMTNiOS00NWRlLWI1Y2EtOTNhMzdjNzMyMWUxL3JlYWRlcnZpZXcuaHRtbD91cmw9aHR0cHMlM0ElMkYlMkZ3d3cudGhldmVyZ2UuY29tJTJGMjAyMCUyRjklMkYxMCUyRjIxNDI5ODM4JTJGZ29vZ2xlLWFuZHJvaWQtMTEtZ28tZWRpdGlvbi1kZXZpY2VzLTJnYi1yYW0tMjAtcGVyY2VudCZpZD0xMzc0Mzg5NTM0ODQifX0=\",\"triggeringPrincipal_base64\":\"eyIxIjp7IjAiOiJtb3otZXh0ZW5zaW9uOi8vMjQ5YjM4YzctMTNiOS00NWRlLWI1Y2EtOTNhMzdjNzMyMWUxL3JlYWRlcnZpZXcuaHRtbD91cmw9aHR0cHMlM0ElMkYlMkZ3d3cudGhldmVyZ2UuY29tJTJGMjAyMCUyRjklMkYxMCUyRjIxNDI5ODM4JTJGZ29vZ2xlLWFuZHJvaWQtMTEtZ28tZWRpdGlvbi1kZXZpY2VzLTJnYi1yYW0tMjAtcGVyY2VudCZpZD0xMzc0Mzg5NTM0ODQifX0=\",\"principalToInherit_base64\":\"eyIxIjp7IjAiOiJtb3otZXh0ZW5zaW9uOi8vMjQ5YjM4YzctMTNiOS00NWRlLWI1Y2EtOTNhMzdjNzMyMWUxL3JlYWRlcnZpZXcuaHRtbD91cmw9aHR0cHMlM0ElMkYlMkZ3d3cudGhldmVyZ2UuY29tJTJGMjAyMCUyRjklMkYxMCUyRjIxNDI5ODM4JTJGZ29vZ2xlLWFuZHJvaWQtMTEtZ28tZWRpdGlvbi1kZXZpY2VzLTJnYi1yYW0tMjAtcGVyY2VudCZpZD0xMzc0Mzg5NTM0ODQifX0=\",\"resultPrincipalURI\":null,\"hasUserInteraction\":false,\"docshellUUID\":\"{22a1a76f-6d3c-496a-bbd7-345bc32a8ba9}\"}],\"requestedIndex\":0,\"fromIdx\":-1,\"index\":2,\"userContextId\":0}}"}}]}
            """
                .trimIndent()

        val engine = FakeEngine(expectToRestoreRealEngineSessionState = true)

        assertTrue(getFileForEngine(testContext, engine).writeString { json })

        val storage = SessionStorage(testContext, engine, applicationScope = this)
        val browsingSession = storage.restore()

        assertNotNull(browsingSession)
        assertEquals(2, browsingSession.tabs.size)

        browsingSession.tabs[0].state.apply {
            assertEquals("https://www.theverge.com/", url)
            assertEquals("The Verge", title)
            assertEquals("28f428ba-2b19-4c24-993b-763fda2be65c", id)
            assertNull(parentId)
            assertEquals(1599815361779, lastAccess)
            assertNotNull(readerState)
            assertFalse(readerState.active)
        }

        browsingSession.tabs[1].state.apply {
            assertEquals(
                "https://www.theverge.com/2020/9/10/21429838/google-android-11-go-edition-devices-2gb-ram-20-percent",
                url,
            )
            assertEquals(
                "Android 11 Go is available today, and it will launch apps 20 percent faster",
                title,
            )
            assertEquals("d6facb8a-0775-45a1-8bc1-2397e2d2bc53", id)
            assertEquals("28f428ba-2b19-4c24-993b-763fda2be65c", parentId)
            assertEquals(1599815364500, lastAccess)
            assertNotNull(readerState)
            assertTrue(readerState.active)
            assertEquals(
                "https://www.theverge.com/2020/9/10/21429838/google-android-11-go-edition-devices-2gb-ram-20-percent",
                readerState.activeUrl,
            )
        }
    }

    @Test
    fun `Restored browsing session contains all expected session properties`() = runTest {
        val firstTab =
            createTab(
                    id = "first-tab",
                    url = "https://www.mozilla.org",
                    title = "Mozilla",
                    lastAccess = 101010,
                )
                .copy(
                    engineState = EngineState(engineSessionState = FakeEngineSessionState("engine-state-of-first-tab"))
                )

        val secondTab =
            createTab(
                    id = "second-tab",
                    url = "https://www.firefox.com",
                    title = "Firefox",
                    contextId = "Work",
                    lastAccess = 12345678,
                    parent = firstTab,
                    readerState =
                        ReaderState(
                            readerable = true,
                            active = true,
                        ),
                )
                .copy(
                    engineState = EngineState(engineSessionState = FakeEngineSessionState("engine-state-of-second-tab"))
                )

        val state =
            BrowserState(
                tabs = listOf(firstTab, secondTab),
                selectedTabId = firstTab.id,
            )

        val engine = FakeEngine()

        val storage = SessionStorage(testContext, engine, applicationScope = this)
        val persisted = storage.save(state)
        assertTrue(persisted)

        // Read it back
        val browsingSession = storage.restore()

        assertNotNull(browsingSession)
        assertEquals(2, browsingSession.tabs.size)

        browsingSession.tabs[0].state.apply {
            assertEquals("https://www.mozilla.org", url)
            assertEquals("Mozilla", title)
            assertEquals("first-tab", id)
            assertNull(parentId)
            assertEquals(101010, lastAccess)
            assertNotNull(readerState)
            assertNull(contextId)
            assertFalse(readerState.active)
        }

        browsingSession.tabs[1].state.apply {
            assertEquals("https://www.firefox.com", url)
            assertEquals("Firefox", title)
            assertEquals("second-tab", id)
            assertEquals("first-tab", parentId)
            assertEquals(12345678, lastAccess)
            assertEquals("Work", contextId)
            assertNotNull(readerState)
            assertTrue(readerState.active)
        }
    }

    @Test
    fun `Saving state with selected tab id for a tab that does not exist`() = runTest {
        val state =
            BrowserState(
                tabs =
                    listOf(
                        createTab(url = "https://www.mozilla.org", id = "mozilla"),
                        createTab(url = "https://getpocket.com", id = "pocket"),
                    ),
                selectedTabId = "Does Not Exist",
            )

        val engine = FakeEngine()

        val storage = SessionStorage(testContext, engine, applicationScope = this)
        val persisted = storage.save(state)
        assertTrue(persisted)

        // Read it back
        val browsingSession = storage.restore()
        assertNotNull(browsingSession)

        assertEquals(2, browsingSession.tabs.size)
        assertEquals("https://www.mozilla.org", browsingSession.tabs[0].state.url)
        assertEquals("https://getpocket.com", browsingSession.tabs[1].state.url)

        // Selected tab doesn't exist so we take to most recently accessed one, or
        // the first one if all tabs have the same last access value.
        assertEquals("mozilla", browsingSession.selectedTabId)
    }

    @Test
    fun `WHEN saving state with crash parent tab THEN don't save tab`() = runTest {
        val state =
            BrowserState(
                tabs =
                    listOf(
                        createTab(url = "about:crashparent", id = "crash"),
                        createTab(url = "https://getpocket.com", id = "pocket"),
                    ),
                selectedTabId = "crash",
            )

        val engine = FakeEngine()

        val storage = SessionStorage(testContext, engine, applicationScope = this)
        val persisted = storage.save(state)
        assertTrue(persisted)

        // Read it back
        val browsingSession = storage.restore()
        assertNotNull(browsingSession)

        assertEquals(1, browsingSession.tabs.size)
        assertEquals("https://getpocket.com", browsingSession.tabs[0].state.url)

        // Selected tab doesn't exist so we take to most recently accessed one, or
        // the first one if all tabs have the same last access value.
        assertEquals("pocket", browsingSession.selectedTabId)
    }
}

internal fun TabSessionState.assertSameAs(tab: RecoverableTab) {
    assertEquals(content.url, tab.state.url)
    assertEquals(content.title, tab.state.title)
    assertEquals(id, tab.state.id)
    assertEquals(parentId, tab.state.parentId)
    assertEquals(contextId, tab.state.contextId)
    assertEquals(lastAccess, tab.state.lastAccess)
    assertEquals(readerState, tab.state.readerState)
    assertEquals(content.private, tab.state.private)
    assertEquals(
        (engineState.engineSessionState as? FakeEngineSessionState)?.value ?: "---",
        (tab.engineSessionState as FakeEngineSessionState).value,
    )
}
