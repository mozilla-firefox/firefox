/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.state.action

import mozilla.components.browser.state.reducer.BrowserStateReducer
import mozilla.components.browser.state.selector.findTab
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.createTab
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ListenToPageActionTest {
    private val tab = createTab("https://www.mozilla.org", id = "tab", listenToPageActive = false)
    private val otherTab = createTab("https://www.firefox.com", id = "other", listenToPageActive = false)
    private val state = BrowserState(tabs = listOf(tab, otherTab))

    @Test
    fun `ListenToPageActivated - marks the tab as being read out loud`() {
        val newState = BrowserStateReducer.reduce(state, ListenToPageAction.ListenToPageActivated(tab.id))

        assertTrue(newState.findTab(tab.id)!!.listenToPageActive)
        assertFalse(newState.findTab(otherTab.id)!!.listenToPageActive)
    }

    @Test
    fun `ListenToPageDeactivated - marks the tab as no longer being read out loud`() {
        val playingState = BrowserStateReducer.reduce(state, ListenToPageAction.ListenToPageActivated(tab.id))

        val newState = BrowserStateReducer.reduce(playingState, ListenToPageAction.ListenToPageDeactivated(tab.id))

        assertFalse(newState.findTab(tab.id)!!.listenToPageActive)
    }

    @Test
    fun `ListenToPageActivated - does nothing for an unknown tab`() {
        val newState = BrowserStateReducer.reduce(state, ListenToPageAction.ListenToPageActivated("unknown"))

        assertEquals(state, newState)
    }
}
