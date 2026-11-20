/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.ui

import kotlin.test.assertEquals
import mozilla.components.feature.listentopage.ListenError
import mozilla.components.feature.listentopage.playback.PlaybackPhase
import org.junit.Test

class PlaybackUiStateTest {
    @Test
    fun `Test that when content is playing and an error is present, the state is error`() {
        val state = PlaybackPhase.Playing.toUiState(error = ListenError.ContentUnavailable)
        assertEquals(expected = PlaybackUiState.Error(ListenError.ContentUnavailable), actual = state)
    }

    @Test
    fun `Test that when content is playing and an error is not present, the state is playing`() {
        val state = PlaybackPhase.Playing.toUiState(error = null)
        assertEquals(expected = PlaybackUiState.Playing, actual = state)
    }

    @Test
    fun `Test that when content is paused and an error is present, the state is error`() {
        val state = PlaybackPhase.Paused.toUiState(error = ListenError.ContentUnavailable)
        assertEquals(expected = PlaybackUiState.Error(ListenError.ContentUnavailable), actual = state)
    }

    @Test
    fun `Test that when content is paused and an error is not present, the state is paused`() {
        val state = PlaybackPhase.Paused.toUiState(error = null)
        assertEquals(expected = PlaybackUiState.Paused, actual = state)
    }

    @Test
    fun `Test that when content is buffering and an error is present, the state is error`() {
        val state = PlaybackPhase.Buffering.toUiState(error = ListenError.ContentUnavailable)
        assertEquals(expected = PlaybackUiState.Error(ListenError.ContentUnavailable), actual = state)
    }

    @Test
    fun `Test that when content is buffering and an error is not present, the state is buffering`() {
        val state = PlaybackPhase.Buffering.toUiState(error = null)
        assertEquals(expected = PlaybackUiState.Buffering, actual = state)
    }
}
