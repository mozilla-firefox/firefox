/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.listentopage

import kotlinx.coroutines.test.StandardTestDispatcher
import mozilla.components.browser.state.action.BrowserAction
import mozilla.components.browser.state.action.ListenToPageAction
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.feature.listentopage.ListenAction
import mozilla.components.feature.listentopage.ListenError
import mozilla.components.feature.listentopage.ListenState
import mozilla.components.feature.listentopage.ListenStore
import mozilla.components.feature.listentopage.PlaybackPhase
import mozilla.components.feature.listentopage.PlaybackState
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Test

class ListenToPageBindingTest {

    private val testDispatcher = StandardTestDispatcher()

    private var nextState = ListenState()
    private val listenStore: ListenStore = Store(ListenState(), { _, _ -> nextState })

    private val dispatched = mutableListOf<ListenToPageAction>()
    private val captureMiddleware: Middleware<BrowserState, BrowserAction> = { _, next, action ->
        if (action is ListenToPageAction) dispatched += action
        next(action)
    }
    private val browserStore = BrowserStore(initialState = BrowserState(), middleware = listOf(captureMiddleware))

    @Before
    fun setUp() {
        ListenToPageBinding(listenStore, browserStore, testDispatcher).start()
        testDispatcher.scheduler.advanceUntilIdle()
        dispatched.clear()
    }

    private fun emitPlaybackStateChange(
        tabId: String?,
        phase: PlaybackPhase = PlaybackPhase.Idle,
        error: ListenError? = null,
    ) {
        nextState = ListenState(tabId = tabId, playbackState = PlaybackState(phase = phase), error = error)
        listenStore.dispatch(ListenAction.Controls.PlayPauseClicked)
        testDispatcher.scheduler.advanceUntilIdle()
    }

    @Test
    fun `WHEN the tab starts playing THEN the tab is marked as active`() {
        emitPlaybackStateChange(tabId = "tab", phase = PlaybackPhase.Playing)

        assertEquals(listOf(ListenToPageAction.ListenToPageActivated("tab")), dispatched)
    }

    @Test
    fun `WHEN the tab is buffering THEN the tab is marked as active`() {
        emitPlaybackStateChange(tabId = "tab", phase = PlaybackPhase.Buffering)

        assertEquals(listOf(ListenToPageAction.ListenToPageActivated("tab")), dispatched)
    }

    @Test
    fun `WHEN playback pauses THEN the tab is marked as inactive`() {
        emitPlaybackStateChange(tabId = "tab", phase = PlaybackPhase.Playing)
        emitPlaybackStateChange(tabId = "tab", phase = PlaybackPhase.Paused)

        assertEquals(
            listOf(
                ListenToPageAction.ListenToPageActivated("tab"),
                ListenToPageAction.ListenToPageDeactivated("tab"),
            ),
            dispatched,
        )
    }

    @Test
    fun `GIVEN an error WHEN the phase is playing THEN the tab is marked as inactive`() {
        emitPlaybackStateChange(tabId = "tab", phase = PlaybackPhase.Playing, error = ListenError.PlaybackFailed)

        assertEquals(listOf(ListenToPageAction.ListenToPageDeactivated("tab")), dispatched)
    }

    @Test
    fun `WHEN the session moves to another tab THEN the previous tab is marked as inactive`() {
        emitPlaybackStateChange(tabId = "first", phase = PlaybackPhase.Playing)
        emitPlaybackStateChange(tabId = "second", phase = PlaybackPhase.Playing)

        assertEquals(
            listOf(
                ListenToPageAction.ListenToPageActivated("first"),
                ListenToPageAction.ListenToPageDeactivated("first"),
                ListenToPageAction.ListenToPageActivated("second"),
            ),
            dispatched,
        )
    }

    @Test
    fun `WHEN the session ends THEN the previous tab is marked as inactive`() {
        emitPlaybackStateChange(tabId = "tab", phase = PlaybackPhase.Playing)
        emitPlaybackStateChange(tabId = null)

        assertEquals(
            listOf(
                ListenToPageAction.ListenToPageActivated("tab"),
                ListenToPageAction.ListenToPageDeactivated("tab"),
            ),
            dispatched,
        )
    }

    @Test
    fun `WHEN the state changes without affecting playback THEN nothing is dispatched again`() {
        emitPlaybackStateChange(tabId = "tab", phase = PlaybackPhase.Playing)
        emitPlaybackStateChange(tabId = "tab", phase = PlaybackPhase.Buffering)
        emitPlaybackStateChange(tabId = "tab", phase = PlaybackPhase.Playing)

        assertEquals(listOf(ListenToPageAction.ListenToPageActivated("tab")), dispatched)
    }
}
