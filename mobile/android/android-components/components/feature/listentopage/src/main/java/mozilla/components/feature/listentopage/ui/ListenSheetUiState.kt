/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.ui

import mozilla.components.feature.listentopage.ListenError
import mozilla.components.feature.listentopage.ListenState
import mozilla.components.feature.listentopage.PlayerMode
import mozilla.components.feature.listentopage.VoiceState
import mozilla.components.feature.listentopage.playback.PlaybackPhase
import mozilla.components.feature.listentopage.playback.PlaybackSpeed

/**
 * The parts of [ListenState] that the player shows and that change only on user actions or playback phase changes. Kept
 * apart from [ListenState.articleProgress], which changes several times a second during playback.
 *
 * @property article The article being read.
 * @property mode Whether the player is shown expanded or compact.
 * @property playbackPhase What the player is currently doing.
 * @property speed How fast the article is read.
 * @property voiceState State relating to narrator voice.
 * @property error The last error, or `null`.
 */
data class ListenSheetUiState(
    val article: ArticleDetails,
    val mode: PlayerMode,
    val playbackPhase: PlaybackPhase,
    val speed: PlaybackSpeed,
    val voiceState: VoiceState,
    val error: ListenError?,
)

/** Maps this state to what the player shows, leaving out what changes as playback progresses. */
fun ListenState.toListenSheetUiState() =
    ListenSheetUiState(
        article = ArticleDetails(title = title, site = site, url = url),
        mode = mode,
        playbackPhase = playbackState.phase,
        speed = playbackState.speed,
        voiceState = voiceState,
        error = error,
    )
