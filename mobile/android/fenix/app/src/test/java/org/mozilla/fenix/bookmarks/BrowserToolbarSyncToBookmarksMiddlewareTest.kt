/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.bookmarks

import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runTest
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarAction.EnterEditMode
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarAction.ExitEditMode
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarState
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarStore
import mozilla.components.compose.browser.toolbar.store.Mode
import org.junit.Assert.assertFalse
import org.junit.Test

class BrowserToolbarSyncToBookmarksMiddlewareTest {

    private val testDispatcher = StandardTestDispatcher()
    private val testScope = TestScope(testDispatcher)

    @Test
    fun `GIVEN in the process of searching in bookmarks WHEN the toolbar exits search mode THEN the search is dismissed`() = runTest(testDispatcher) {
        val toolbarStore = BrowserToolbarStore(BrowserToolbarState(Mode.EDIT))
        val middleware = BrowserToolbarSyncToBookmarksMiddleware(toolbarStore, testScope)

        val bookmarksStore = BookmarksStore(
            initialState = BookmarksState.default.copy(isSearching = true),
            middleware = listOf(middleware),
        ).also {
            it.dispatch(ViewAppeared())
        }

        toolbarStore.dispatch(ExitEditMode)
        testScheduler.advanceUntilIdle()

        assertFalse(bookmarksStore.state.isSearching)
    }

    @Test
    fun `GIVEN not in the process of searching in bookmarks WHEN the toolbar exits search mode THEN the search mode is not changed`() = runTest(testDispatcher) {
        val toolbarStore = BrowserToolbarStore(BrowserToolbarState(Mode.EDIT))
        val middleware = BrowserToolbarSyncToBookmarksMiddleware(toolbarStore, testScope)

        val bookmarksStore = BookmarksStore(
            initialState = BookmarksState.default.copy(isSearching = false),
            middleware = listOf(middleware),
        ).also {
            it.dispatch(ViewAppeared())
        }

        toolbarStore.dispatch(ExitEditMode)
        testScheduler.advanceUntilIdle()

        assertFalse(bookmarksStore.state.isSearching)
    }

    @Test
    fun `GIVEN not in the process of searching in bookmarks WHEN the toolbar enters search mode THEN the search mode is not changed`() = runTest(testDispatcher) {
        val toolbarStore = BrowserToolbarStore(BrowserToolbarState(Mode.DISPLAY))
        val middleware = BrowserToolbarSyncToBookmarksMiddleware(toolbarStore, testScope)

        val bookmarksStore = BookmarksStore(
            initialState = BookmarksState.default.copy(isSearching = false),
            middleware = listOf(middleware),
        ).also {
            it.dispatch(ViewAppeared())
        }

        toolbarStore.dispatch(EnterEditMode(false))
        testScheduler.advanceUntilIdle()

        assertFalse(bookmarksStore.state.isSearching)
    }
}
