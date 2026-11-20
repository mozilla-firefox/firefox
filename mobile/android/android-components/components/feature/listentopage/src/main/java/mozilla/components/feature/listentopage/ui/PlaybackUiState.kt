/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.ui

import androidx.compose.ui.tooling.preview.PreviewParameterProvider
import mozilla.components.feature.listentopage.ListenError
import mozilla.components.feature.listentopage.playback.PlaybackPhase

internal sealed interface PlaybackUiState {
    data object Playing : PlaybackUiState

    data object Buffering : PlaybackUiState

    data object Paused : PlaybackUiState

    data class Error(val listenError: ListenError) : PlaybackUiState
}

internal fun PlaybackPhase.toUiState(error: ListenError?): PlaybackUiState =
    when {
        error != null -> PlaybackUiState.Error(error)
        this == PlaybackPhase.Playing -> PlaybackUiState.Playing
        this == PlaybackPhase.Buffering -> PlaybackUiState.Buffering
        else -> PlaybackUiState.Paused
    }

internal class PlaybackUIStatePreviewProvider : PreviewParameterProvider<PlaybackUiState> {
    val data =
        listOf(
            "Playing" to PlaybackUiState.Playing,
            "Buffering" to PlaybackUiState.Buffering,
            "Paused" to PlaybackUiState.Paused,
            "Error:PlaybackFailed" to PlaybackUiState.Error(ListenError.PlaybackFailed),
            "Error:SynthesisFailed" to PlaybackUiState.Error(ListenError.SynthesisFailed),
            "Error:NoOfflineVoice" to PlaybackUiState.Error(ListenError.NoOfflineVoice),
            "Error:ContentUnavailable" to PlaybackUiState.Error(ListenError.ContentUnavailable),
        )

    override val values: Sequence<PlaybackUiState>
        get() = data.map { it.second }.asSequence()

    override fun getDisplayName(index: Int): String? {
        return data[index].first
    }
}
