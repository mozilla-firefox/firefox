/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage

import java.util.Locale
import kotlin.time.Duration.Companion.seconds
import mozilla.components.feature.listentopage.playback.ArticleMeasurements
import mozilla.components.feature.listentopage.playback.shownDurationMs
import mozilla.components.feature.listentopage.synthesis.SpeechRate
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

private const val TAB_ID = "tab-1"
private const val URL = "https://example.org/article"

/** A session with every field set, so a reset is visible in every one of them. */
private val fullState =
    ListenState(
        tabId = TAB_ID,
        url = URL,
        title = "Match Preview: Wrexham AFC vs Sunderland AFC",
        site = "bbc.co.uk",
        languageTag = "de-DE",
        mode = PlayerMode.Compact,
        error = ListenError.PlaybackFailed,
        voiceState =
            VoiceState(
                availableVoices =
                    listOf(Locale.GERMANY, Locale.forLanguageTag("de-AT"), Locale.forLanguageTag("de-CH")).map {
                        Voice(id = "de-voice-${it.country}", locale = it)
                    }
            ),
        playbackState =
            PlaybackState(
                phase = PlaybackPhase.Playing,
                chunk = ChunkState(index = 3, durationMs = 30_000),
                positionMs = 12_000,
            ),
        articleProgress = ArticleProgress(positionMs = 102_000, durationMs = 600_000),
    )

class ListenReducerTest {

    @Test
    fun `test that a listen request starts a session on the given tab`() {
        val state = listenReducer(ListenState(), ListenAction.Session.ListenRequested(TAB_ID, URL))

        assertEquals(TAB_ID, state.tabId)
    }

    @Test
    fun `test that a listen request drops everything the previous session held`() {
        val state = listenReducer(fullState, ListenAction.Session.ListenRequested("tab-2", URL))

        assertEquals("tab-2", state.tabId)
        assertEquals(URL, state.url)
        assertNull(state.title)
        assertNull(state.site)
        assertNull(state.error)
        assertEquals(PlayerMode.Expanded, state.mode)
        assertEquals("de-DE", state.languageTag)
        assertEquals(fullState.voiceState, state.voiceState)
        assertEquals(PlaybackState(), state.playbackState)
        assertEquals(ArticleProgress(), state.articleProgress)
    }

    @Test
    fun `test that a listen request records the article url`() {
        val state = listenReducer(ListenState(), ListenAction.Session.ListenRequested(TAB_ID, URL))

        assertEquals(URL, state.url)
    }

    @Test
    fun `test that stopping resets appropriate fields`() {
        assertEquals(
            ListenState(voiceState = fullState.voiceState.copy()),
            listenReducer(fullState, ListenAction.Session.StopRequested()),
        )
    }

    @Test
    fun `test that dismissing an error clears it and leaves the session alone`() {
        val state = listenReducer(fullState, ListenAction.ErrorDismissed)

        assertNull(state.error)
        assertEquals(fullState.copy(error = null), state)
    }

    @Test
    fun `test that dismissing clears every kind of error`() {
        val errors =
            listOf(
                ListenError.NoOfflineVoice,
                ListenError.ContentUnavailable,
                ListenError.SynthesisFailed,
                ListenError.PlaybackFailed,
            )

        errors.forEach { error ->
            val state = listenReducer(fullState.copy(error = error), ListenAction.ErrorDismissed)

            assertNull("$error was not cleared", state.error)
        }
    }

    @Test
    fun `test that changing the mode records it`() {
        val collapsed = listenReducer(ListenState(), ListenAction.ModeChanged(PlayerMode.Compact))
        val expanded = listenReducer(collapsed, ListenAction.ModeChanged(PlayerMode.Expanded))

        assertEquals(ListenState(mode = PlayerMode.Compact), collapsed)
        assertEquals(ListenState(), expanded)
    }

    @Test
    fun `test that dismissing when there is no error changes nothing`() {
        val noError = fullState.copy(error = null)

        assertEquals(noError, listenReducer(noError, ListenAction.ErrorDismissed))
    }

    @Test
    fun `test that selecting a voice records it`() {
        val state =
            listenReducer(
                ListenState(),
                ListenAction.Voices.VoiceSelected(Voice(id = "en-us-female", locale = Locale.US)),
            )

        assertEquals(Voice(id = "en-us-female", locale = Locale.US), state.voiceState.selectedVoice)
    }

    @Test
    fun `test that a picked playback speed is recorded`() {
        val state = listenReducer(ListenState(), ListenAction.Controls.PlaybackSpeedSelected(PlaybackSpeed.X0_5))

        assertEquals(PlaybackSpeed.X0_5, state.playbackState.speed)
    }

    @Test
    fun `test that loaded voices are recorded`() {
        val voices =
            listOf(Voice(id = "en-us-female", locale = Locale.US), Voice(id = "en-us-male", locale = Locale.US))

        val state = listenReducer(ListenState(), ListenAction.Voices.AvailableVoicesLoaded(voices, voices.first()))

        assertEquals(voices, state.voiceState.availableVoices)
    }

    @Test
    fun `test that loading voices again replaces the voices of the previous language`() {
        val german = listOf(Voice("de-de", Locale.GERMANY))
        val loaded = listenReducer(ListenState(), ListenAction.Voices.AvailableVoicesLoaded(german, german.first()))

        val english = listOf(Voice("en-us", Locale.US))
        val state = listenReducer(loaded, ListenAction.Voices.AvailableVoicesLoaded(english, english.first()))

        assertEquals(english, state.voiceState.availableVoices)
    }

    @Test
    fun `test that loading voices records the voice they were resolved for`() {
        val voices =
            listOf(Voice(id = "en-us-female", locale = Locale.US), Voice(id = "en-us-male", locale = Locale.US))

        val state =
            listenReducer(
                fullState,
                ListenAction.Voices.AvailableVoicesLoaded(
                    voices,
                    selectedVoice = Voice(id = "en-us-male", locale = Locale.US),
                ),
            )

        assertEquals(voices, state.voiceState.availableVoices)
        assertEquals(Voice(id = "en-us-male", locale = Locale.US), state.voiceState.selectedVoice)
    }

    @Test
    fun `test that having no offline voice is reported as an error`() {
        val state = listenReducer(ListenState(), ListenAction.Voices.NoOfflineVoicesAvailable)

        assertEquals(ListenError.NoOfflineVoice, state.error)
    }

    @Test
    fun `test that having no offline voice leaves the article alone`() {
        val state = listenReducer(fullState.copy(error = null), ListenAction.Voices.NoOfflineVoicesAvailable)

        assertEquals(
            fullState.copy(
                error = ListenError.NoOfflineVoice,
                voiceState = VoiceState(loadState = VoiceLoadState.Loaded),
            ),
            state,
        )
    }

    // An empty list only means the language has no offline voice, so the voices of the language before it cannot be
    // left behind for the popup to offer.
    @Test
    fun `test that having no offline voice empties the voices and marks them loaded`() {
        val state = listenReducer(fullState, ListenAction.Voices.NoOfflineVoicesAvailable)

        assertEquals(emptyList<Voice>(), state.voiceState.availableVoices)
        assertNull(state.voiceState.selectedVoice)
        assertEquals(VoiceLoadState.Loaded, state.voiceState.loadState)
    }

    @Test
    fun `test that loaded voices are marked as loaded`() {
        val voices = listOf(Voice(id = "en-us-female", locale = Locale.US))

        val state = listenReducer(ListenState(), ListenAction.Voices.AvailableVoicesLoaded(voices, voices.first()))

        assertEquals(VoiceLoadState.Loaded, state.voiceState.loadState)
    }

    @Test
    fun `test that a session has no voices by default`() {
        val initial = ListenState()

        assertEquals(emptyList<Voice>(), initial.voiceState.availableVoices)
        assertNull(initial.voiceState.selectedVoice)
        assertEquals(VoiceLoadState.NotLoaded, initial.voiceState.loadState)
    }

    @Test
    fun `test that a failed synthesis becomes an error the session can show`() {
        val state = listenReducer(ListenState(tabId = "tab-1"), ListenAction.Synthesis.SynthesisFailed)

        assertEquals(ListenError.SynthesisFailed, state.error)
        assertEquals("tab-1", state.tabId)
    }

    @Test
    fun `test that a failed synthesis can be dismissed`() {
        val failed = listenReducer(ListenState(tabId = "tab-1"), ListenAction.Synthesis.SynthesisFailed)

        val dismissed = listenReducer(failed, ListenAction.ErrorDismissed)

        assertNull(dismissed.error)
    }

    @Test
    fun `test that a session has no tab, no error and the expanded mode by default`() {
        val initial = ListenState()

        assertNull(initial.tabId)
        assertNull(initial.url)
        assertNull(initial.title)
        assertNull(initial.languageTag)
        assertNull(initial.error)
        assertEquals(PlayerMode.Expanded, initial.mode)
    }

    @Test
    fun `test that an article in the language of the previous one keeps its voices`() {
        val state =
            listenReducer(
                fullState,
                ListenAction.Content.ContentReady(
                    languageTag = "de-DE",
                    title = fullState.title,
                    site = fullState.site,
                ),
            )

        assertEquals(fullState, state)
    }

    @Test
    fun `test that a ready article records its title and site`() {
        val state =
            listenReducer(
                ListenState(tabId = TAB_ID, url = URL),
                ListenAction.Content.ContentReady(languageTag = "en-US", title = "An article", site = "example.org"),
            )

        assertEquals("An article", state.title)
        assertEquals("example.org", state.site)
    }

    @Test
    fun `test that a ready article with no title records none`() {
        val state =
            listenReducer(fullState, ListenAction.Content.ContentReady(languageTag = "de-DE", site = "example.org"))

        assertNull(state.title)
        assertEquals("example.org", state.site)
    }

    @Test
    fun `test that an article in another language drops the voices of the previous one`() {
        val state = listenReducer(fullState, ListenAction.Content.ContentReady(languageTag = "fr-FR"))

        assertEquals("fr-FR", state.languageTag)
        assertEquals(VoiceState(), state.voiceState)
    }

    @Test
    fun `test that a session is playing nothing by default`() {
        val initial = ListenState()

        assertEquals(PlaybackPhase.Idle, initial.playbackState.phase)
        assertEquals(0, initial.playbackState.chunk.index)
        assertEquals(0, initial.playbackState.positionMs)
        assertNull(initial.playbackState.chunk.durationMs)
    }

    // The bar has nothing to draw until the opening has been measured, which is a few seconds into the session.
    @Test
    fun `test that a session has got nowhere through its article by default`() {
        val initial = ListenState()

        assertEquals(ArticleProgress(), initial.articleProgress)
        assertEquals(0f, initial.articleProgress.fraction, 0f)
    }

    @Test
    fun `test that what the player reports is recorded`() {
        val reported = PlaybackState(phase = PlaybackPhase.Playing, chunk = ChunkState(index = 2), positionMs = 4_000)

        val state = listenReducer(ListenState(), ListenAction.Playback.StateChangeObserved(reported))

        assertEquals(reported, state.playbackState)
    }

    @Test
    fun `test that what the player reports leaves the rest of the session alone`() {
        val reported = PlaybackState(phase = PlaybackPhase.Paused, positionMs = 1_000)

        val state = listenReducer(fullState, ListenAction.Playback.StateChangeObserved(reported))

        assertEquals(fullState.copy(playbackState = reported), state)
    }

    @Test
    fun `test that how far through the article playback has got is recorded`() {
        val state = listenReducer(ListenState(), ListenAction.Playback.ArticleProgressReverted(30_000, 600_000))

        assertEquals(ArticleProgress(positionMs = 30_000, durationMs = 600_000), state.articleProgress)
        assertEquals(0.05f, state.articleProgress.fraction, 0.0001f)
    }

    // A real duration shorter than the estimate it replaced shortens the whole article, and a position left outside it
    // would hold the bar at full while there is still audio to play.
    @Test
    fun `test that a position outside a shortened article is pulled back inside it`() {
        val state = listenReducer(fullState, ListenAction.Playback.ArticleProgressReverted(90_000, 60_000))

        assertEquals(ArticleProgress(positionMs = 60_000, durationMs = 60_000), state.articleProgress)
    }

    @Test
    fun `test that article progress leaves the rest of the session alone`() {
        val state = listenReducer(fullState, ListenAction.Playback.ArticleProgressReverted(1_000, 2_000))

        assertEquals(fullState.copy(articleProgress = ArticleProgress(1_000, 2_000)), state)
    }

    @Test
    fun `test that an article of no length reads as no progress rather than as not a number`() {
        val state = listenReducer(fullState, ListenAction.Playback.ArticleProgressReverted(0, 0))

        assertEquals(ArticleProgress(), state.articleProgress)
        assertEquals(0f, state.articleProgress.fraction, 0f)
    }

    @Test
    fun `test that a seek moves the elapsed time to that share of the article`() {
        val state = listenReducer(fullState, ListenAction.Playback.SeekRequested(0.25f))

        assertEquals(
            fullState.copy(articleProgress = fullState.articleProgress.copy(positionMs = 150_000, barPosition = 0.25f)),
            state,
        )
    }

    @Test
    fun `test that a seek lands against the estimated length, so the bar is back in step with it`() {
        // A bar held back at a third while the estimate grows is drawn against 300s rather than the 600s estimated.
        val held =
            ListenState(
                articleProgress = ArticleProgress(positionMs = 100_000, durationMs = 600_000, barPosition = 1 / 3f)
            )

        val state = listenReducer(held, ListenAction.Playback.SeekRequested(0.5f))

        assertEquals(300_000, state.articleProgress.positionMs)
        assertEquals(0.5f, state.articleProgress.fraction, 0f)
        assertEquals(600_000, state.articleProgress.shownDurationMs())
    }

    @Test
    fun `test that a seek to the end after the estimate grew lands at the end of the article`() {
        val held =
            ListenState(
                articleProgress = ArticleProgress(positionMs = 100_000, durationMs = 600_000, barPosition = 1 / 3f)
            )

        val state = listenReducer(held, ListenAction.Playback.SeekRequested(1f))

        assertEquals("the end of the drawn length was taken as the end", 600_000, state.articleProgress.positionMs)
    }

    @Test
    fun `test that a seek past either end of the article stops at that end`() {
        assertEquals(
            600_000,
            listenReducer(fullState, ListenAction.Playback.SeekRequested(1.5f)).articleProgress.positionMs,
        )
        assertEquals(0, listenReducer(fullState, ListenAction.Playback.SeekRequested(-0.5f)).articleProgress.positionMs)
    }

    @Test
    fun `test that a seek in an article of no length stays at the start`() {
        val state = listenReducer(ListenState(), ListenAction.Playback.SeekRequested(0.5f))

        assertEquals(ArticleProgress(), state.articleProgress)
    }

    @Test
    fun `test that the end of the article fills the bar`() {
        val playing = fullState.copy(articleProgress = ArticleProgress(positionMs = 590_000, durationMs = 600_000))

        val state = listenReducer(playing, ListenAction.Playback.PlaybackEnded)

        assertEquals(PlaybackPhase.Ended, state.playbackState.phase)
        assertEquals(600_000, state.articleProgress.positionMs)
        assertEquals(1f, state.articleProgress.fraction, 0f)
    }

    @Test
    fun `test that a seek away from an article a seek finished leaves it paused while it waits`() {
        val finished =
            fullState.copy(
                playbackState = fullState.playbackState.copy(phase = PlaybackPhase.Ended),
                playlist = Playlist().finished(),
            )

        val seeking = listenReducer(finished, PlaylistAction.SeekStarted(articlePositionMs = 300_000, chunkIndex = 2))
        val waiting = listenReducer(seeking, ListenAction.Playback.PlaybackWaiting)

        assertEquals(PlaybackPhase.Paused, seeking.playbackState.phase)
        assertEquals(PlaybackPhase.Paused, waiting.playbackState.phase)
    }

    @Test
    fun `test that a seek away from an article read to its end does not pause it`() {
        val readOut = fullState.copy(playbackState = fullState.playbackState.copy(phase = PlaybackPhase.Ended))

        val state = listenReducer(readOut, PlaylistAction.SeekStarted(articlePositionMs = 300_000, chunkIndex = 2))

        assertEquals(PlaybackPhase.Ended, state.playbackState.phase)
    }

    @Test
    fun `test that a failed player is reported as an error`() {
        val state = listenReducer(ListenState(), ListenAction.Playback.PlaybackFailed)

        assertEquals(ListenError.PlaybackFailed, state.error)
        assertEquals(PlaybackPhase.Failed, state.playbackState.phase)
    }

    @Test
    fun `test that the chunk the player has reached is recorded`() {
        val chunk = ChunkState(index = 4, durationMs = 60_000)

        val state = listenReducer(ListenState(), ListenAction.Playback.PlaybackStarted(chunk, positionMs = 1_000))

        assertEquals(chunk, state.playbackState.chunk)
        assertEquals(1_000, state.playbackState.positionMs)
        assertEquals(PlaybackPhase.Playing, state.playbackState.phase)
    }

    @Test
    fun `test that waiting on the next chunk is recorded as a wait rather than an end`() {
        val playing = fullState.copy(playbackState = fullState.playbackState.copy(phase = PlaybackPhase.Playing))

        val state = listenReducer(playing, ListenAction.Playback.PlaybackWaiting)

        assertEquals(PlaybackPhase.Buffering, state.playbackState.phase)
        assertEquals(playing.playbackState.chunk, state.playbackState.chunk)
    }

    @Test
    fun `test that a paused player stays paused while it waits`() {
        val paused = fullState.copy(playbackState = fullState.playbackState.copy(phase = PlaybackPhase.Paused))

        val state = listenReducer(paused, ListenAction.Playback.PlaybackWaiting)

        assertEquals(PlaybackPhase.Paused, state.playbackState.phase)
    }

    @Test
    fun `test that the article being read out is recorded as ended`() {
        val readOut = fullState.copy(error = null)

        val state = listenReducer(readOut, ListenAction.Playback.PlaybackEnded)

        assertEquals(PlaybackPhase.Ended, state.playbackState.phase)
        assertNull(state.error)
    }

    @Test
    fun `test that a failed player can be dismissed`() {
        val failed = listenReducer(ListenState(), ListenAction.Playback.PlaybackFailed)

        assertNull(listenReducer(failed, ListenAction.ErrorDismissed).error)
    }

    // An error the user has not seen yet outlives a report that has nothing to say about it, so that a phase change
    // arriving in between cannot take the dialog away.
    @Test
    fun `test that a player with nothing wrong leaves an existing error alone`() {
        val state =
            listenReducer(
                fullState.copy(error = ListenError.SynthesisFailed),
                ListenAction.Playback.StateChangeObserved(PlaybackState(phase = PlaybackPhase.Paused)),
            )

        assertEquals(ListenError.SynthesisFailed, state.error)
    }

    @Test
    fun `test that a report the playlist reads moves the article on from what synthesis has measured`() {
        val sized =
            ListenState(
                articleMeasurements =
                    ArticleMeasurements(chunkCharacters = listOf(100, 200), measuredDurations = mapOf(0 to 10.seconds))
            )

        val state = listenReducer(sized, PlayerReportReceived(playing(positionMs = 4_000)))

        assertEquals(4_000, state.articleProgress.positionMs)
        // The second chunk is sized at the rate the first one measured at: a hundred characters in ten seconds.
        assertEquals(listOf(10_000L, 20_000L), state.articleProgress.chunkDurationsMs)
        assertEquals(30_000, state.articleProgress.durationMs)
    }

    @Test
    fun `test that a report the playlist does not read leaves the article where it was`() {
        val restarted =
            ListenState(
                articleProgress = ArticleProgress(positionMs = 61_000, durationMs = 120_000),
                articleMeasurements =
                    ArticleMeasurements(chunkCharacters = listOf(100), measuredDurations = mapOf(0 to 10.seconds)),
                playlist =
                    Playlist()
                        .restarted(chunkIndex = 0, chunkDurationsBeforePlaylistMs = emptyList(), startMs = 61_000),
            )

        val state = listenReducer(restarted, PlayerReportReceived(playing(positionMs = 1_000)))

        assertNull(state.playlist.lastReadReport)
        assertEquals(restarted.articleProgress, state.articleProgress)
    }

    @Test
    fun `test that a report with nothing being synthesized is read but does not move the article`() {
        val state = listenReducer(fullState, PlayerReportReceived(playing(positionMs = 4_000)))

        assertEquals(4_000L, state.playlist.lastReadReport?.positionMs)
        assertEquals(fullState.articleProgress, state.articleProgress)
    }

    @Test
    fun `test that a measured chunk is kept until synthesis is cleared`() {
        val started = listenReducer(ListenState(), MeasurementAction.Started)

        val measured = listenReducer(started, MeasurementAction.ChunkMeasured(index = 3, duration = 7.seconds))
        assertEquals(mapOf(3 to 7.seconds), measured.articleMeasurements?.measuredDurations)

        assertNull(listenReducer(measured, MeasurementAction.Cleared).articleMeasurements)
    }

    @Test
    fun `test that a chunk measured with nothing being synthesized is ignored`() {
        val state = listenReducer(ListenState(), MeasurementAction.ChunkMeasured(index = 3, duration = 7.seconds))

        assertNull(state.articleMeasurements)
    }

    @Test
    fun `test that re-splitting the article invalidates estimate cache so new chunks are calculated at the new speech rate`() {
        val split =
            ListenState()
                .reducedBy(
                    MeasurementAction.Started,
                    MeasurementAction.ArticleSplit(listOf(100, 200), SpeechRate(charsPerSecond = 10.0)),
                )
        assertEquals(listOf(10_000L, 20_000L), split.chunkDurationsMs())

        val resplit =
            listenReducer(
                split,
                MeasurementAction.ArticleSplit(listOf(100, 200, 300), SpeechRate(charsPerSecond = 20.0)),
            )

        assertEquals(listOf(5_000L, 10_000L, 15_000L), resplit.chunkDurationsMs())
    }

    @Test
    fun `test that switching voices invalidates estimate cache so new voice measurements are estimated`() {
        val oldVoice =
            ListenState()
                .reducedBy(
                    MeasurementAction.Started,
                    MeasurementAction.ArticleSplit(listOf(100, 200, 300), SpeechRate(charsPerSecond = 10.0)),
                    MeasurementAction.ChunkMeasured(index = 0, duration = 20.seconds),
                )
        assertEquals(listOf(20_000L, 40_000L, 60_000L), oldVoice.chunkDurationsMs())

        // What the middleware dispatches when the voice changes.
        val newVoice =
            oldVoice.reducedBy(
                MeasurementAction.Cleared,
                MeasurementAction.Started,
                MeasurementAction.ArticleSplit(listOf(100, 200, 300), SpeechRate(charsPerSecond = 20.0)),
            )
        assertEquals(listOf(5_000L, 10_000L, 15_000L), newVoice.chunkDurationsMs())

        val newVoiceMeasured = listenReducer(newVoice, MeasurementAction.ChunkMeasured(index = 0, duration = 4.seconds))
        assertEquals(listOf(4_000L, 8_000L, 12_000L), newVoiceMeasured.chunkDurationsMs())
    }

    @Test
    fun `test that measuring a chunk updates the estimates of the chunks that are not measured`() {
        val split =
            ListenState()
                .reducedBy(
                    MeasurementAction.Started,
                    MeasurementAction.ArticleSplit(listOf(100, 200, 300), SpeechRate(charsPerSecond = 10.0)),
                )
        assertEquals(listOf(10_000L, 20_000L, 30_000L), split.chunkDurationsMs())

        // The second chunk read twice as fast as the opening, so the chunks around it are estimated at its speed.
        val measured = listenReducer(split, MeasurementAction.ChunkMeasured(index = 1, duration = 10.seconds))

        assertEquals(listOf(5_000L, 10_000L, 15_000L), measured.chunkDurationsMs())
    }

    @Test
    fun `test that the playlist records what is handed to the player`() {
        val restarted =
            listenReducer(
                ListenState(),
                PlaylistAction.Restarted(
                    chunkIndex = 6,
                    chunkDurationsBeforePlaylistMs = List(6) { 10_000L },
                    startMs = 61_000,
                ),
            )
        val queued = listenReducer(restarted, PlaylistAction.ChunkQueued(7))

        assertEquals(7, queued.playlist.lastQueuedChunkIndex)
        assertEquals(List(6) { 10_000L }, queued.playlist.chunkDurationsBeforePlaylistMs)
    }

    private fun playing(positionMs: Long) = PlaybackState(phase = PlaybackPhase.Playing, positionMs = positionMs)

    private fun ListenState.reducedBy(vararg actions: ListenAction) = actions.fold(this, ::listenReducer)

    private fun ListenState.chunkDurationsMs() = articleMeasurements!!.mapper().chunkDurationsMs()
}
