/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage

import mozilla.components.lib.state.Middleware
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class ListenStoreTest {
    @Test
    fun `test that the store starts with no session and reduces dispatched actions`() {
        val store = ListenStore(initialState = ListenState(), reducer = ::listenReducer)

        assertEquals(ListenState(), store.state)

        store.dispatch(ListenAction.User.Session.ListenRequested("tab-1", "https://example.org/article"))
        assertEquals("tab-1", store.state.tabId)

        store.dispatch(ListenAction.User.Session.StopRequested())
        assertNull(store.state.tabId)
    }

    @Test
    fun `test that the store can start from a given state`() {
        val store =
            ListenStore(
                initialState = ListenState(tabId = "tab-1", error = ListenError.NoOfflineVoice),
                reducer = ::listenReducer,
            )

        store.dispatch(ListenAction.User.ErrorDismissed)

        assertNull(store.state.error)
        assertEquals("tab-1", store.state.tabId)
    }

    @Test
    fun `test that middleware sees every action`() {
        val seen = mutableListOf<ListenAction>()
        val recorder: Middleware<ListenState, ListenAction> = { _, next, action ->
            seen.add(action)
            next(action)
        }
        val store = ListenStore(initialState = ListenState(), reducer = ::listenReducer, middleware = listOf(recorder))

        store.dispatch(ListenAction.User.Session.ListenRequested("tab-1", "https://example.org/article"))
        store.dispatch(ListenAction.User.ErrorDismissed)

        assertEquals(
            listOf(
                ListenAction.User.Session.ListenRequested("tab-1", "https://example.org/article"),
                ListenAction.User.ErrorDismissed,
            ),
            seen,
        )
    }
}
