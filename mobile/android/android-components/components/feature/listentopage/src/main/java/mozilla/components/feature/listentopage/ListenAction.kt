/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage

import kotlin.time.Duration
import mozilla.components.feature.listentopage.content.LanguageSource
import mozilla.components.feature.listentopage.synthesis.SpeechRate
import mozilla.components.lib.state.Action

/** Actions for the [ListenStore]. */
sealed interface ListenAction : Action {
    /** Actions that start and end a listening session. */
    sealed interface Session : ListenAction {
        /**
         * The user asked to listen to an article.
         *
         * @property tabId The tab the article is in.
         * @property url The article.
         */
        data class ListenRequested(
            val tabId: String,
            val url: String,
        ) : Session

        /**
         * The listening session is over. It resets to the initial state.
         *
         * @property source What ended the session.
         */
        data class StopRequested(val source: StopSource = StopSource.CloseButton) : Session

        /** What ended a listening session. */
        enum class StopSource {
            /** The user tapped the player's close button. */
            CloseButton,

            /** The listened tab was closed or user navigated elsewhere. */
            TabClosed,

            /** The user left reader mode on the listened tab. */
            ReaderModeClosed,

            /**
             * The media session ended outside the app, such as when the app was removed from the recent apps or the
             * notification was dismissed.
             */
            MediaSessionEndedOutsideApp,

            /** The user turned the feature off. */
            FeatureDisabled,
        }
    }

    /** Actions reporting the article the session reads out. */
    sealed interface Content : ListenAction {
        /**
         * The article was extracted from the page.
         *
         * @property languageTag language of the article.
         * @property languageSource Where [languageTag] came from.
         * @property title The article title, or `null` when the page has none.
         * @property site The site the article is from, or `null` when its URL names none.
         */
        data class ContentReady(
            val languageTag: String,
            val languageSource: LanguageSource = LanguageSource.Page,
            val title: String? = null,
            val site: String? = null,
        ) : Content

        /** The page gave back no usable text. */
        data object ContentUnavailable : Content
    }

    /** Actions reporting the change in selected voice. */
    sealed interface Voices : ListenAction {
        /**
         * The voice was changed.
         *
         * @property voice The voice selected for the article.
         * @property source Source for voice selection.
         */
        data class VoiceSelected(
            val voice: Voice,
            val source: VoiceSource = VoiceSource.UserSelected,
        ) : Voices

        /** What chose a voice. */
        enum class VoiceSource {
            /** The user picked the voice from the list. */
            UserSelected,

            /** The voice was chosen automatically after the saved one became unavailable. */
            Fallback,
        }

        /** Available voices were loaded from the engine. */
        data class AvailableVoicesLoaded(val voices: List<Voice>, val selectedVoice: Voice) : Voices

        /** The engine has no installed, network-free voice for the article language. */
        data object NoOfflineVoicesAvailable : Voices
    }

    /**
     * Actions reporting what the user did with the player controls. Closing the player is [Session.StopRequested]
     * rather than a control of its own.
     */
    sealed interface Controls : ListenAction {
        /** The user asked to start or pause playback. */
        data object PlayPauseClicked : Controls

        /** The user asked to skip back. */
        data object RewindClicked : Controls

        /** The user asked to skip forward. */
        data object ForwardClicked : Controls

        /** The user asked for the voices to pick from. */
        data object VoicesClicked : Controls

        /** The user asked for the speeds to pick from. */
        data object PlaybackSpeedClicked : Controls

        /**
         * The user picked a playback speed.
         *
         * @property playbackSpeed The speed to read the article out at.
         */
        data class PlaybackSpeedSelected(val playbackSpeed: PlaybackSpeed) : Controls

        /** The user dismissed the voices list without picking one. */
        data object VoicesDismissed : Controls

        /** The user dismissed the speeds list without picking one. */
        data object PlaybackSpeedDismissed : Controls
    }

    /** Actions reporting what the player is doing. */
    sealed interface Playback : ListenAction {
        /**
         * The player reported a change.
         *
         * @property playbackState What the player is doing now.
         */
        data class StateChangeObserved(val playbackState: PlaybackState) : Playback

        /**
         * The progress was put back to what it was before a seek that could not be made.
         *
         * @property positionMs How far into the article.
         * @property durationMs How long the whole article lasts.
         * @property chunkDurationsMs How long each chunk lasts, as they stood when this was worked out.
         * @property barPosition Where the progress bar is drawn. See [ArticleProgress.barPosition].
         */
        data class ArticleProgressReverted(
            val positionMs: Long,
            val durationMs: Long,
            val chunkDurationsMs: List<Long> = emptyList(),
            val barPosition: Float? = null,
        ) : Playback

        /**
         * The player moved on to a chunk and is reading it out.
         *
         * @property chunk The chunk being read.
         * @property positionMs How far into [chunk] the player has got.
         */
        data class PlaybackStarted(val chunk: ChunkState, val positionMs: Long) : Playback

        /**
         * The reader chose a place in the article to carry on from.
         *
         * @property fraction How far into the article to move to.
         */
        data class SeekRequested(val fraction: Float) : Playback

        /**
         * Playback waits for audio that is still being made: the opening when a session starts, or the next chunk after
         * the player read out every chunk it had been given.
         */
        data object PlaybackWaiting : Playback

        /** The article was read to its end, or a seek moved playback to its end. */
        data object PlaybackEnded : Playback

        /** The player could not read the article out. */
        data object PlaybackFailed : Playback
    }

    /** Actions reporting what the speech engine did with the article. */
    sealed interface Synthesis : ListenAction {
        /** The engine could not turn the article into audio. */
        data object SynthesisFailed : Synthesis
    }

    /**
     * The player was expanded or collapsed.
     *
     * @property mode What the player shows.
     */
    data class ModeChanged(val mode: PlayerMode) : ListenAction

    /** The error that needs to be cleared it is shown. */
    data object ErrorDismissed : ListenAction
}

// The actions below are only dispatched by the session's own middleware, so they are internal. They sit outside
// ListenAction because Kotlin makes everything nested in an interface public.

/**
 * The player sent a report.
 *
 * @property report What the player reported, naming the chunk it is reading.
 */
internal data class PlayerReportReceived(val report: PlaybackState) : ListenAction.Playback

/**
 * The article was split again for a new voice, so its progress moves to the same share of the length it now has.
 *
 * @property positionInArticle How far through the article playback had got in the voice before, between 0 and 1.
 */
internal data class VoiceChangeStarted(val positionInArticle: Float) : ListenAction.Playback

/** Actions recording what has been handed to the player, which its reports are read against. */
internal sealed interface PlaylistAction : ListenAction {
    /** A new article is starting at its top, so nothing is queued, and a seek in the article before it is forgotten. */
    data object ArticleStarted : PlaylistAction

    /**
     * What was handed to the player no longer counts, because the session is replacing it. A seek in progress is kept,
     * since it is what the new playlist is being made for.
     */
    data object Cleared : PlaylistAction

    /**
     * The chunk at [chunkIndex] was handed to the player behind what was queued.
     *
     * @property chunkIndex The index of the chunk handed over.
     */
    data class ChunkQueued(val chunkIndex: Int) : PlaylistAction

    /**
     * Playback is being moved to [articlePositionMs] into the article, which is inside the chunk at [chunkIndex].
     *
     * @property articlePositionMs Where in the article playback is moving to.
     * @property chunkIndex The index of the chunk that holds it.
     */
    data class SeekStarted(val articlePositionMs: Long, val chunkIndex: Int) : PlaylistAction

    /** The seek was given up. */
    data object SeekAbandoned : PlaylistAction

    /** A seek moved playback to the end of the article, which finished it there. */
    data object Finished : PlaylistAction

    /**
     * The player was given a new playlist that starts on the chunk at [chunkIndex], [startMs] into the article.
     *
     * @property chunkIndex The index of the chunk the new playlist starts on.
     * @property chunkDurationsBeforePlaylistMs The durations of the chunks before the chunk at [chunkIndex] that the
     *   playlist was placed by.
     * @property startMs Where in the article playback starts.
     */
    data class Restarted(
        val chunkIndex: Int,
        val chunkDurationsBeforePlaylistMs: List<Long>,
        val startMs: Long,
    ) : PlaylistAction
}

/**
 * Actions copying into [ListenState.articleMeasurements] what synthesis has found out about the article. Each one is
 * dispatched where the synthesis queue changes, so that the two always agree.
 */
internal sealed interface MeasurementAction : ListenAction.Synthesis {
    /** Synthesis of the article started again, so nothing is known yet about how long it lasts. */
    data object Started : MeasurementAction

    /** The article's audio was thrown away, so there is nothing to work its length out from. */
    data object Cleared : MeasurementAction

    /**
     * The article was split into chunks once its opening was made.
     *
     * @property chunkCharacters How many characters each chunk holds, in reading order.
     * @property speechRate What the opening measured at, or `null` when it could not be measured.
     */
    data class ArticleSplit(val chunkCharacters: List<Int>, val speechRate: SpeechRate?) : MeasurementAction

    /**
     * Chunk at [index] was made and measured.
     *
     * @property index The index of the chunk.
     * @property duration How long its audio lasts.
     */
    data class ChunkMeasured(val index: Int, val duration: Duration) : MeasurementAction
}
