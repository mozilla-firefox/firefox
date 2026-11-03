/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage

import kotlin.math.roundToLong
import mozilla.components.feature.listentopage.playback.ArticleMeasurements

/**
 * Reduces the given [action] and current [state] into a new [ListenState].
 *
 * @param state The current [ListenState].
 * @param action The [ListenAction] to process.
 * @return The resulting [ListenState] after applying the action.
 */
fun listenReducer(state: ListenState, action: ListenAction): ListenState =
    when (action) {
        is ListenAction.Session -> reduceSession(state, action)
        is ListenAction.Content -> reduceContent(state, action)
        is ListenAction.Voices -> reduceVoices(state, action)
        is ListenAction.Controls -> reduceControls(state, action)
        is ListenAction.Playback -> reducePlayback(state, action)
        is ListenAction.Synthesis -> reduceSynthesis(state, action)
        is PlaylistAction -> reducePlaylistAction(state, action)
        is ListenAction.ModeChanged -> state.copy(mode = action.mode)
        ListenAction.ErrorDismissed -> state.copy(error = null)
    }

private fun reduceControls(state: ListenState, action: ListenAction.Controls): ListenState =
    when (action) {
        is ListenAction.Controls.PlaybackSpeedSelected ->
            state.copy(playbackState = state.playbackState.copy(speed = action.playbackSpeed))

        ListenAction.Controls.PlayPauseClicked,
        ListenAction.Controls.RewindClicked,
        ListenAction.Controls.ForwardClicked,
        ListenAction.Controls.VoicesClicked,
        ListenAction.Controls.VoicesDismissed,
        ListenAction.Controls.PlaybackSpeedClicked,
        ListenAction.Controls.PlaybackSpeedDismissed -> state
    }

private fun reduceSession(state: ListenState, action: ListenAction.Session): ListenState =
    when (action) {
        is ListenAction.Session.ListenRequested -> ListenState(tabId = action.tabId, url = action.url)

        is ListenAction.Session.StopRequested -> {
            ListenState(tabId = null, url = null, voiceState = state.voiceState.copy())
        }
    }

private fun reduceContent(state: ListenState, action: ListenAction.Content): ListenState =
    when (action) {
        is ListenAction.Content.ContentReady -> {
            val described = state.copy(title = action.title, site = action.site)
            if (action.languageTag == state.languageTag) {
                described
            } else {
                described.copy(languageTag = action.languageTag, voiceState = VoiceState())
            }
        }

        ListenAction.Content.ContentUnavailable -> state.copy(error = ListenError.ContentUnavailable)
    }

private fun reducePlayback(state: ListenState, action: ListenAction.Playback): ListenState =
    when (action) {
        is ListenAction.Playback.StateChangeObserved -> state.copy(playbackState = action.playbackState)

        is ListenAction.Playback.PlaybackStarted ->
            state.copy(
                playbackState =
                    state.playbackState.copy(
                        phase = PlaybackPhase.Playing,
                        chunk = action.chunk,
                        positionMs = action.positionMs,
                    ),
                playlist = state.playlist.playing(action.chunk.index),
            )

        is PlayerReportReceived -> {
            val playlist = state.playlist.received(action.report)
            val report = playlist.lastReadReport
            val measurements = state.articleMeasurements
            state.copy(
                playlist = playlist,
                articleProgress =
                    if (report != null && measurements != null) {
                        measurements.progressAt(report, playlist, state.articleProgress)
                    } else {
                        state.articleProgress
                    },
            )
        }

        is VoiceChangeStarted -> state.copy(articleProgress = state.progressAtTextPosition(action.positionInArticle))

        ListenAction.Playback.PlaybackWaiting ->
            if (state.playbackState.phase == PlaybackPhase.Paused) {
                state
            } else {
                state.copy(playbackState = state.playbackState.copy(phase = PlaybackPhase.Buffering))
            }

        ListenAction.Playback.PlaybackEnded ->
            state.copy(
                playbackState = state.playbackState.copy(phase = PlaybackPhase.Ended),
                articleProgress = state.articleProgress.atEnd(),
            )

        ListenAction.Playback.PlaybackFailed ->
            state.copy(
                playbackState = state.playbackState.copy(phase = PlaybackPhase.Failed),
                error = ListenError.PlaybackFailed,
            )
        is ListenAction.Playback.SeekRequested -> {
            val durationMs = state.articleProgress.durationMs
            val fraction = action.fraction.coerceIn(0f, 1f)
            if (durationMs <= 0) {
                state
            } else {
                state.copy(
                    articleProgress =
                        state.articleProgress.copy(
                            positionMs = (durationMs * fraction).roundToLong(),
                            barPosition = fraction,
                        )
                )
            }
        }
        is ListenAction.Playback.ArticleProgressReverted ->
            state.copy(
                articleProgress =
                    ArticleProgress(
                            action.positionMs,
                            action.durationMs,
                            action.chunkDurationsMs,
                            action.barPosition,
                        )
                        .correctLength()
            )
    }

/**
 * How far through the article [report] puts playback. The position and its chunk are both taken from [report], so that
 * they always match: [Playlist.playingChunkIndex] only moves on once the report has been read.
 */
private fun ArticleMeasurements.progressAt(
    report: PlaybackState,
    playlist: Playlist,
    previous: ArticleProgress,
): ArticleProgress {
    val mapper = mapper(playlist.chunkDurationsBeforePlaylistMs)
    val isArticleFinished = report.phase == PlaybackPhase.Ended && isLastChunk(report.chunk.index)
    return mapper
        .updatedProgress(previous, report.positionMs, isArticleFinished)
        .copy(chunkDurationsMs = mapper.chunkDurationsMs())
        .correctLength()
}

/** The progress at [textPosition], between 0 and 1 of the article's text, or none before the article is split. */
private fun ListenState.progressAtTextPosition(textPosition: Float): ArticleProgress =
    articleMeasurements?.getArticleProgressAtTextPosition(textPosition) ?: ArticleProgress()

/** This progress at the end of the article, with the bar full, however the article got there. */
private fun ArticleProgress.atEnd(): ArticleProgress =
    if (durationMs > 0) copy(positionMs = durationMs, barPosition = 1f) else this

/** This progress with a length of at least zero, and a position inside it. */
private fun ArticleProgress.correctLength(): ArticleProgress {
    val lengthMs = durationMs.coerceAtLeast(0)
    return copy(positionMs = positionMs.coerceIn(0, lengthMs), durationMs = lengthMs)
}

private fun reducePlaylistAction(state: ListenState, action: PlaylistAction): ListenState {
    val playlist = reducePlaylist(state.playlist, action)

    // The player stays paused where a seek finished the article, so a seek away from there is a paused one.
    val isSeekStartedAfterPlaybackFinish = action is PlaylistAction.SeekStarted && state.playlist.isFinished
    return if (isSeekStartedAfterPlaybackFinish && state.playbackState.phase == PlaybackPhase.Ended) {
        state.copy(playlist = playlist, playbackState = state.playbackState.copy(phase = PlaybackPhase.Paused))
    } else {
        state.copy(playlist = playlist)
    }
}

private fun reducePlaylist(playlist: Playlist, action: PlaylistAction): Playlist =
    when (action) {
        PlaylistAction.ArticleStarted -> Playlist()
        PlaylistAction.Cleared -> playlist.cleared()
        is PlaylistAction.ChunkQueued -> playlist.queued(action.chunkIndex)
        is PlaylistAction.SeekStarted -> playlist.seeking(action.articlePositionMs, action.chunkIndex)
        PlaylistAction.SeekAbandoned -> playlist.seekAbandoned()
        PlaylistAction.Finished -> playlist.finished()
        is PlaylistAction.Restarted ->
            playlist.restarted(action.chunkIndex, action.chunkDurationsBeforePlaylistMs, action.startMs)
    }

private fun reduceSynthesis(state: ListenState, action: ListenAction.Synthesis): ListenState =
    when (action) {
        ListenAction.Synthesis.SynthesisFailed -> state.copy(error = ListenError.SynthesisFailed)
        is MeasurementAction ->
            state.copy(articleMeasurements = reduceArticleMeasurements(state.articleMeasurements, action))
    }

private fun reduceVoices(state: ListenState, action: ListenAction.Voices): ListenState =
    when (action) {
        is ListenAction.Voices.VoiceSelected ->
            state.copy(voiceState = state.voiceState.copy(selectedVoice = action.voice))

        is ListenAction.Voices.AvailableVoicesLoaded ->
            state.copy(
                voiceState =
                    state.voiceState.copy(
                        availableVoices = action.voices,
                        selectedVoice = action.selectedVoice,
                        loadState = VoiceLoadState.Loaded,
                    )
            )

        // The empty list is the answer, not the absence of one: the engine has been asked and has nothing offline for
        // this language.
        ListenAction.Voices.NoOfflineVoicesAvailable ->
            state.copy(
                voiceState = VoiceState(loadState = VoiceLoadState.Loaded),
                error = ListenError.NoOfflineVoice,
            )
    }

private fun reduceArticleMeasurements(
    measurements: ArticleMeasurements?,
    action: MeasurementAction,
): ArticleMeasurements? =
    when (action) {
        MeasurementAction.Started -> ArticleMeasurements()
        MeasurementAction.Cleared -> null
        is MeasurementAction.ArticleSplit ->
            measurements?.copy(chunkCharacters = action.chunkCharacters, speechRate = action.speechRate)
        is MeasurementAction.ChunkMeasured ->
            measurements?.copy(measuredDurations = measurements.measuredDurations + (action.index to action.duration))
    }
