/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.ui

import kotlin.test.assertEquals
import mozilla.components.feature.listentopage.ArticleProgress
import mozilla.components.feature.listentopage.ListenState
import mozilla.components.feature.listentopage.PlaybackState
import mozilla.components.feature.listentopage.PlayerMode
import mozilla.components.feature.listentopage.playback.PlaybackPhase
import mozilla.components.feature.listentopage.playback.PlaybackSpeed
import org.junit.Test

class ListenSheetUiStateTest {
    @Test
    fun `Test that the ui state carries what the player shows`() {
        val state =
            ListenState(
                title = "Title",
                site = "example.com",
                url = "https://example.com",
                mode = PlayerMode.Compact,
                playbackState = PlaybackState(phase = PlaybackPhase.Playing, speed = PlaybackSpeed.Default),
            )

        val expected =
            ListenSheetUiState(
                article = ArticleDetails(title = "Title", site = "example.com", url = "https://example.com"),
                mode = PlayerMode.Compact,
                playbackPhase = PlaybackPhase.Playing,
                speed = PlaybackSpeed.Default,
                voiceState = state.voiceState,
                error = null,
            )
        assertEquals(expected = expected, actual = state.toListenSheetUiState())
    }

    @Test
    fun `Test that playback progress does not change the ui state`() {
        val state = ListenState(title = "Title", playbackState = PlaybackState(phase = PlaybackPhase.Playing))
        val progressed =
            state.copy(
                articleProgress = ArticleProgress(positionMs = 5_000, durationMs = 60_000),
                playbackState = state.playbackState.copy(positionMs = 5_000),
            )

        assertEquals(expected = state.toListenSheetUiState(), actual = progressed.toListenSheetUiState())
    }
}
