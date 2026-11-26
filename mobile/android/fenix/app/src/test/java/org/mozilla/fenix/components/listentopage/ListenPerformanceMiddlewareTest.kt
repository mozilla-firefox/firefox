/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.listentopage

import java.util.Locale
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import mozilla.components.feature.listentopage.ChunkState
import mozilla.components.feature.listentopage.ListenAction
import mozilla.components.feature.listentopage.ListenState
import mozilla.components.feature.listentopage.ListenStore
import mozilla.components.feature.listentopage.PlaybackState
import mozilla.components.feature.listentopage.facts.ListenToPageFacts
import mozilla.components.feature.listentopage.listenReducer
import mozilla.components.feature.listentopage.playback.PlaybackPhase
import mozilla.components.feature.listentopage.playback.PlaybackSpeed
import mozilla.components.feature.listentopage.voices.Voice
import mozilla.components.support.base.Component
import mozilla.components.support.base.facts.Action
import mozilla.components.support.base.facts.Fact
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.ListenToPage
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.robolectric.RobolectricTestRunner

@OptIn(ExperimentalCoroutinesApi::class)
@RunWith(RobolectricTestRunner::class)
class ListenPerformanceMiddlewareTest {

    @get:Rule val gleanTestRule = FenixGleanTestRule(testContext)

    private var now = 0L
    private val profiler = FakeProfiler(clock = { now })

    private lateinit var middleware: ListenPerformanceMiddleware

    @Test
    fun `WHEN audio starts after listening is requested THEN a time to first audio marker is recorded`() {
        val store = createStore()

        store.dispatch(LISTEN_REQUESTED)
        now += 250
        store.dispatch(ListenAction.System.Playback.PlaybackStarted(ChunkState(index = 0), positionMs = 0))

        assertEquals(
            listOf(Marker(TIME_TO_FIRST_AUDIO, startTime = 0.0, endTime = 250.0, text = null)),
            profiler.markers,
        )
        assertEquals(1L, ListenToPage.timeToFirstAudio.testGetValue()?.count)
    }

    @Test
    fun `WHEN the player reports playing THEN time to first audio ends`() {
        val store = createStore()

        store.dispatch(LISTEN_REQUESTED)
        store.dispatch(ListenAction.System.Playback.StateChangeObserved(PlaybackState(phase = PlaybackPhase.Playing)))

        assertEquals(TIME_TO_FIRST_AUDIO, profiler.markers.single().name)
    }

    @Test
    fun `WHEN listening is requested again before audio starts THEN the time is measured from the new request`() {
        val store = createStore()

        store.dispatch(LISTEN_REQUESTED)
        now += 100
        store.dispatch(LISTEN_REQUESTED)
        now += 100
        store.dispatch(ListenAction.System.Playback.PlaybackStarted(ChunkState(index = 0), positionMs = 0))

        assertEquals(100.0, profiler.markers.single().startTime)
    }

    @Test
    fun `WHEN later chunks start playing THEN time to first audio is only recorded once`() {
        val store = createStore()

        store.dispatch(LISTEN_REQUESTED)
        store.dispatch(ListenAction.System.Playback.PlaybackStarted(ChunkState(index = 0), positionMs = 0))
        store.dispatch(ListenAction.System.Playback.PlaybackStarted(ChunkState(index = 1), positionMs = 0))

        assertEquals(1, profiler.markers.size)
        assertEquals(1L, ListenToPage.timeToFirstAudio.testGetValue()?.count)
    }

    @Test
    fun `WHEN play is pressed THEN time to first audio is not started`() {
        val store = createStore()

        store.dispatch(ListenAction.User.Controls.PlayPauseClicked)
        store.dispatch(ListenAction.System.Playback.PlaybackStarted(ChunkState(index = 0), positionMs = 0))

        assertTrue(profiler.markers.isEmpty())
        assertNull(ListenToPage.timeToFirstAudio.testGetValue())
    }

    @Test
    fun `WHEN the session fails before audio starts THEN a failed marker is recorded without telemetry`() {
        val store = createStore()

        store.dispatch(LISTEN_REQUESTED)
        now += 300
        store.dispatch(ListenAction.System.Synthesis.SynthesisFailed)

        assertEquals(
            Marker(
                name = "$TIME_TO_FIRST_AUDIO.Failed",
                startTime = 0.0,
                endTime = 300.0,
                text = "failed=SynthesisFailed",
            ),
            profiler.markers.first { it.name == "$TIME_TO_FIRST_AUDIO.Failed" },
        )
        assertNull(ListenToPage.timeToFirstAudio.testGetValue())
    }

    @Test
    fun `WHEN the session stops before audio starts THEN nothing is recorded`() {
        val store = createStore()

        store.dispatch(LISTEN_REQUESTED)
        store.dispatch(ListenAction.User.Session.StopRequested())
        store.dispatch(ListenAction.System.Playback.PlaybackStarted(ChunkState(index = 0), positionMs = 0))

        assertTrue(profiler.markers.isEmpty())
        assertNull(ListenToPage.timeToFirstAudio.testGetValue())
    }

    @Test
    fun `WHEN the profiler is not running THEN telemetry is still recorded`() {
        profiler.active = false
        val store = createStore()

        store.dispatch(LISTEN_REQUESTED)
        store.dispatch(ListenAction.System.Playback.PlaybackStarted(ChunkState(index = 0), positionMs = 0))

        assertTrue(profiler.markers.isEmpty())
        assertEquals(1L, ListenToPage.timeToFirstAudio.testGetValue()?.count)
    }

    @Test
    fun `WHEN the player runs out of audio THEN a chunk gap is recorded until the next chunk plays`() {
        val store = createStore(playing)

        store.dispatch(ListenAction.System.Playback.PlaybackWaiting)
        now += 800
        store.dispatch(ListenAction.System.Playback.PlaybackStarted(ChunkState(index = 3), positionMs = 0))

        assertEquals(
            listOf(Marker(CHUNK_GAP, startTime = 0.0, endTime = 800.0, text = "cause=waiting")),
            profiler.markers,
        )
        assertEquals(1L, ListenToPage.chunkGapDuration.testGetValue()?.count)
    }

    @Test
    fun `WHEN the player waits for the opening THEN no chunk gap is recorded`() {
        val store = createStore()

        store.dispatch(ListenAction.System.Playback.PlaybackWaiting)
        store.dispatch(ListenAction.System.Playback.PlaybackStarted(ChunkState(index = 0), positionMs = 0))

        assertTrue(profiler.markers.isEmpty())
        assertNull(ListenToPage.chunkGapDuration.testGetValue())
    }

    @Test
    fun `WHEN an article that ended is played again THEN no chunk gap is recorded`() {
        val store = createStore(ListenState(playbackState = PlaybackState(phase = PlaybackPhase.Ended)))

        store.dispatch(ListenAction.System.Playback.PlaybackWaiting)
        store.dispatch(ListenAction.System.Playback.PlaybackStarted(ChunkState(index = 0), positionMs = 0))

        assertTrue(profiler.markers.isEmpty())
        assertNull(ListenToPage.chunkGapDuration.testGetValue())
    }

    @Test
    fun `WHEN the player buffers while playing THEN a chunk gap is recorded until it plays again`() {
        val store = createStore(playing)

        store.dispatch(ListenAction.System.Playback.StateChangeObserved(PlaybackState(phase = PlaybackPhase.Buffering)))
        now += 50
        store.dispatch(ListenAction.System.Playback.StateChangeObserved(PlaybackState(phase = PlaybackPhase.Playing)))

        assertEquals(
            listOf(Marker(CHUNK_GAP, startTime = 0.0, endTime = 50.0, text = "cause=buffering")),
            profiler.markers,
        )
    }

    @Test
    fun `WHEN the player buffers before it has played THEN no chunk gap is recorded`() {
        val store = createStore()

        store.dispatch(ListenAction.System.Playback.StateChangeObserved(PlaybackState(phase = PlaybackPhase.Buffering)))
        store.dispatch(ListenAction.System.Playback.StateChangeObserved(PlaybackState(phase = PlaybackPhase.Playing)))

        assertTrue(profiler.markers.isEmpty())
    }

    @Test
    fun `WHEN the player waits after a seek or voice change THEN no chunk gap is recorded`() {
        val requests =
            listOf(
                ListenAction.User.Controls.SeekRequested(fraction = 0.5f),
                ListenAction.User.Controls.RewindClicked,
                ListenAction.User.Controls.ForwardClicked,
                ListenAction.User.Voices.VoiceSelected(Voice("en-gb", Locale.UK)),
            )

        requests.forEach { request ->
            val store = createStore(playing)

            store.dispatch(request)
            store.dispatch(ListenAction.System.Playback.PlaybackWaiting)
            store.dispatch(ListenAction.System.Playback.PlaybackStarted(ChunkState(index = 3), positionMs = 0))

            assertTrue("Gap recorded after $request", profiler.markers.none { it.name == CHUNK_GAP })
        }
    }

    @Test
    fun `WHEN audio plays after a seek THEN later chunk gaps are recorded again`() {
        val store = createStore(playing)
        store.dispatch(ListenAction.User.Controls.SeekRequested(fraction = 0.5f))
        store.dispatch(ListenAction.System.Playback.PlaybackWaiting)
        store.dispatch(ListenAction.System.Playback.PlaybackStarted(ChunkState(index = 3), positionMs = 0))

        store.dispatch(ListenAction.System.Playback.PlaybackWaiting)
        store.dispatch(ListenAction.System.Playback.PlaybackStarted(ChunkState(index = 4), positionMs = 0))

        assertEquals(1, profiler.markers.count { it.name == CHUNK_GAP })
    }

    @Test
    fun `WHEN a session stops during a seek THEN the next session records chunk gaps`() {
        val store = createStore(playing)
        store.dispatch(ListenAction.User.Controls.SeekRequested(fraction = 0.5f))
        store.dispatch(ListenAction.User.Session.StopRequested())

        store.dispatch(LISTEN_REQUESTED)
        store.dispatch(ListenAction.System.Playback.PlaybackStarted(ChunkState(index = 0), positionMs = 0))
        store.dispatch(ListenAction.System.Playback.PlaybackWaiting)
        store.dispatch(ListenAction.System.Playback.PlaybackStarted(ChunkState(index = 1), positionMs = 0))

        assertEquals(CHUNK_GAP, profiler.markers.last().name)
    }

    @Test
    fun `WHEN playback is paused during a chunk gap THEN the gap is not recorded`() {
        val pauses =
            listOf(
                ListenAction.User.Controls.PlayPauseClicked,
                ListenAction.System.Playback.StateChangeObserved(PlaybackState(phase = PlaybackPhase.Paused)),
            )

        pauses.forEach { pause ->
            val store = createStore(playing)

            store.dispatch(ListenAction.System.Playback.PlaybackWaiting)
            store.dispatch(pause)
            store.dispatch(ListenAction.System.Playback.PlaybackStarted(ChunkState(index = 3), positionMs = 0))

            assertTrue("Gap recorded after $pause", profiler.markers.isEmpty())
        }
        assertNull(ListenToPage.chunkGapDuration.testGetValue())
    }

    @Test
    fun `WHEN playback fails during a chunk gap THEN a failed chunk gap marker is recorded`() {
        val store = createStore(playing)

        store.dispatch(ListenAction.System.Playback.PlaybackWaiting)
        now += 1200
        store.dispatch(ListenAction.System.Playback.PlaybackFailed)

        assertEquals(
            listOf(
                Marker(
                    name = "$CHUNK_GAP.Failed",
                    startTime = 0.0,
                    endTime = 1200.0,
                    text = "failed=PlaybackFailed, cause=waiting",
                )
            ),
            profiler.markers,
        )
        assertNull(ListenToPage.chunkGapDuration.testGetValue())
    }

    @Test
    fun `WHEN a seek finds its chunk queued THEN its latency is recorded as queued`() {
        val store = createStore(playing)

        store.dispatch(ListenAction.User.Controls.SeekRequested(fraction = 0.5f))
        now += 40
        store.dispatch(ListenAction.System.Playback.StateChangeObserved(PlaybackState(phase = PlaybackPhase.Playing)))

        assertEquals(
            listOf(Marker(SEEK_LATENCY, startTime = 0.0, endTime = 40.0, text = "path=queued")),
            profiler.markers,
        )
        assertEquals(1L, ListenToPage.seekLatencyQueued.testGetValue()?.count)
        assertNull(ListenToPage.seekLatency.testGetValue())
    }

    @Test
    fun `WHEN a seek has to remake its chunk THEN its latency is recorded as remade`() {
        val store = createStore(playing)

        store.dispatch(ListenAction.User.Controls.SeekRequested(fraction = 0.5f))
        store.dispatch(ListenAction.System.Playback.PlaybackWaiting)
        now += 900
        store.dispatch(ListenAction.System.Playback.PlaybackStarted(ChunkState(index = 3), positionMs = 0))

        assertEquals(
            listOf(Marker(SEEK_LATENCY, startTime = 0.0, endTime = 900.0, text = "path=remade")),
            profiler.markers,
        )
        assertEquals(1L, ListenToPage.seekLatency.testGetValue()?.count)
        assertNull(ListenToPage.seekLatencyQueued.testGetValue())
    }

    @Test
    fun `WHEN the reader skips forward or back THEN the seek latency is recorded`() {
        listOf(ListenAction.User.Controls.RewindClicked, ListenAction.User.Controls.ForwardClicked).forEach { skip ->
            profiler.markers.clear()
            val store = createStore(playing)

            store.dispatch(skip)
            store.dispatch(ListenAction.System.Playback.PlaybackStarted(ChunkState(index = 3), positionMs = 0))

            assertEquals("No latency after $skip", SEEK_LATENCY, profiler.markers.single().name)
        }
    }

    @Test
    fun `WHEN a seek is interrupted by another seek THEN only the later one is recorded`() {
        val store = createStore(playing)

        store.dispatch(ListenAction.User.Controls.SeekRequested(fraction = 0.5f))
        now = 100
        store.dispatch(ListenAction.User.Controls.RewindClicked)
        now = 300
        store.dispatch(ListenAction.System.Playback.PlaybackStarted(ChunkState(index = 3), positionMs = 0))

        assertEquals(
            listOf(Marker(SEEK_LATENCY, startTime = 100.0, endTime = 300.0, text = "path=queued")),
            profiler.markers,
        )
        assertEquals(1L, ListenToPage.seekLatencyQueued.testGetValue()?.count)
    }

    @Test
    fun `WHEN a seek is interrupted by a voice change THEN it is not recorded`() {
        val store = createStore(playing)

        store.dispatch(ListenAction.User.Controls.SeekRequested(fraction = 0.5f))
        store.dispatch(ListenAction.User.Voices.VoiceSelected(Voice("en-gb", Locale.UK)))
        store.dispatch(ListenAction.System.Playback.PlaybackStarted(ChunkState(index = 3), positionMs = 0))

        assertTrue(profiler.markers.none { it.name.startsWith(SEEK_LATENCY) })
        assertNull(ListenToPage.seekLatencyQueued.testGetValue())
    }

    @Test
    fun `WHEN playback is paused during a seek THEN it is not recorded`() {
        val pauses =
            listOf(
                ListenAction.User.Controls.PlayPauseClicked,
                ListenAction.System.Playback.StateChangeObserved(PlaybackState(phase = PlaybackPhase.Paused)),
            )

        pauses.forEach { pause ->
            val store = createStore(playing)

            store.dispatch(ListenAction.User.Controls.SeekRequested(fraction = 0.5f))
            store.dispatch(pause)
            store.dispatch(ListenAction.System.Playback.PlaybackStarted(ChunkState(index = 3), positionMs = 0))

            assertTrue("Latency recorded after $pause", profiler.markers.none { it.name.startsWith(SEEK_LATENCY) })
        }
    }

    @Test
    fun `WHEN the reader seeks while paused THEN no latency is recorded`() {
        val store = createStore(ListenState(playbackState = PlaybackState(phase = PlaybackPhase.Paused)))

        store.dispatch(ListenAction.User.Controls.SeekRequested(fraction = 0.5f))
        store.dispatch(ListenAction.System.Playback.StateChangeObserved(PlaybackState(phase = PlaybackPhase.Playing)))

        assertTrue(profiler.markers.none { it.name.startsWith(SEEK_LATENCY) })
    }

    @Test
    fun `WHEN playback fails during a seek THEN a failed seek latency marker is recorded`() {
        val store = createStore(playing)

        store.dispatch(ListenAction.User.Controls.SeekRequested(fraction = 0.5f))
        store.dispatch(ListenAction.System.Playback.PlaybackWaiting)
        store.dispatch(ListenAction.System.Synthesis.SynthesisFailed)

        assertTrue(profiler.markers.any { it.name == "$SEEK_LATENCY.Failed" })
        assertNull(ListenToPage.seekLatency.testGetValue())
    }

    @Test
    fun `WHEN the player runs out of audio THEN it is counted`() {
        val store = createStore(playing)

        store.dispatch(ListenAction.System.Playback.PlaybackWaiting)

        assertEquals(1, ListenToPage.audioRanOut.testGetValue())
    }

    @Test
    fun `WHEN running out is reported more than once before audio plays THEN it is only counted once`() {
        val store = createStore(playing)

        store.dispatch(ListenAction.System.Playback.PlaybackWaiting)
        store.dispatch(ListenAction.System.Playback.PlaybackWaiting)

        assertEquals(1, ListenToPage.audioRanOut.testGetValue())
    }

    @Test
    fun `WHEN the player runs out of audio twice THEN both are counted`() {
        val store = createStore(playing)

        store.dispatch(ListenAction.System.Playback.PlaybackWaiting)
        store.dispatch(ListenAction.System.Playback.PlaybackStarted(ChunkState(index = 3), positionMs = 0))
        store.dispatch(ListenAction.System.Playback.PlaybackWaiting)

        assertEquals(2, ListenToPage.audioRanOut.testGetValue())
    }

    @Test
    fun `WHEN the player waits for the opening THEN it is not counted as audio running out`() {
        val store = createStore()

        store.dispatch(LISTEN_REQUESTED)
        store.dispatch(ListenAction.System.Playback.PlaybackWaiting)
        store.dispatch(ListenAction.System.Playback.PlaybackStarted(ChunkState(index = 0), positionMs = 0))

        assertNull(ListenToPage.audioRanOut.testGetValue())
    }

    @Test
    fun `WHEN the player is waiting after a seek THEN it is not counted as audio running out`() {
        val store = createStore(playing)

        store.dispatch(ListenAction.User.Controls.SeekRequested(fraction = 0.5f))
        store.dispatch(ListenAction.System.Playback.PlaybackWaiting)

        assertNull(ListenToPage.audioRanOut.testGetValue())
    }

    @Test
    fun `WHEN the player buffers between queued chunks THEN it is not counted as audio running out`() {
        val store = createStore(playing)

        store.dispatch(ListenAction.System.Playback.StateChangeObserved(PlaybackState(phase = PlaybackPhase.Buffering)))

        assertNull(ListenToPage.audioRanOut.testGetValue())
    }

    @Test
    fun `WHEN listening is requested THEN each phase of time to first audio is recorded in sequence`() {
        val store = createStore()

        store.dispatch(LISTEN_REQUESTED)
        now = 100
        store.dispatch(ListenAction.System.Content.ContentReady(languageTag = "en-US"))
        now = 150
        middleware.process(openingSynthesisStartedFact())
        now = 400
        middleware.process(chunkSynthesizedFact(index = 0))
        now = 500
        store.dispatch(ListenAction.System.Playback.PlaybackStarted(ChunkState(index = 0), positionMs = 0))

        assertEquals(
            listOf(
                Marker(EXTRACTION, startTime = 0.0, endTime = 100.0, text = null),
                Marker(PREPARATION, startTime = 100.0, endTime = 150.0, text = null),
                Marker(OPENING_SYNTHESIS, startTime = 150.0, endTime = 400.0, text = null),
                Marker(PLAYBACK_START, startTime = 400.0, endTime = 500.0, text = null),
                Marker(PHASE_SUM, startTime = 0.0, endTime = 500.0, text = PHASE_SUM_TEXT),
                Marker(TIME_TO_FIRST_AUDIO, startTime = 0.0, endTime = 500.0, text = null),
            ),
            phaseMarkers(),
        )
        assertEquals(1L, ListenToPage.extractionDuration.testGetValue()?.count)
        assertEquals(1L, ListenToPage.preparationDuration.testGetValue()?.count)
        assertEquals(1L, ListenToPage.openingSynthesisDuration.testGetValue()?.count)
        assertEquals(1L, ListenToPage.playbackStartDuration.testGetValue()?.count)
    }

    @Test
    fun `WHEN play is pressed after extraction THEN preparation still starts when the article is ready`() {
        val store = createStore()
        store.dispatch(LISTEN_REQUESTED)
        now = 100
        store.dispatch(ListenAction.System.Content.ContentReady(languageTag = "en-US"))

        now = 5_000
        store.dispatch(ListenAction.User.Controls.PlayPauseClicked)
        now = 5_050
        middleware.process(openingSynthesisStartedFact())

        assertEquals(Marker(PREPARATION, startTime = 100.0, endTime = 5_050.0, text = null), profiler.markers.last())
    }

    @Test
    fun `WHEN a phase fails THEN no phase sum is recorded`() {
        val store = createStore()
        store.dispatch(LISTEN_REQUESTED)
        store.dispatch(ListenAction.System.Content.ContentReady(languageTag = "en-US"))
        middleware.process(openingSynthesisStartedFact())
        store.dispatch(ListenAction.System.Synthesis.SynthesisFailed)

        store.dispatch(ListenAction.System.Playback.PlaybackStarted(ChunkState(index = 0), positionMs = 0))

        assertTrue(profiler.markers.none { it.name == PHASE_SUM })
    }

    @Test
    fun `WHEN a session stops after extraction THEN the next session's phase sum only counts its own phases`() {
        val store = createStore()
        store.dispatch(LISTEN_REQUESTED)
        now = 1_000
        store.dispatch(ListenAction.System.Content.ContentReady(languageTag = "en-US"))
        store.dispatch(ListenAction.User.Session.StopRequested())

        now = 2_000
        store.dispatch(LISTEN_REQUESTED)
        now = 2_100
        store.dispatch(ListenAction.System.Content.ContentReady(languageTag = "en-US"))
        now = 2_150
        middleware.process(openingSynthesisStartedFact())
        now = 2_400
        middleware.process(chunkSynthesizedFact(index = 0))
        now = 2_500
        store.dispatch(ListenAction.System.Playback.PlaybackStarted(ChunkState(index = 0), positionMs = 0))

        assertEquals(PHASE_SUM_TEXT, profiler.markers.single { it.name == PHASE_SUM }.text)
    }

    @Test
    fun `WHEN the opening is made again outside time to first audio THEN no phase is recorded`() {
        val store = createStore(playing)

        middleware.process(openingSynthesisStartedFact())
        middleware.process(chunkSynthesizedFact(index = 0))
        store.dispatch(ListenAction.System.Playback.PlaybackStarted(ChunkState(index = 0), positionMs = 0))

        assertTrue(phaseMarkers().isEmpty())
    }

    @Test
    fun `WHEN the opening chunk is synthesized THEN it ends the opening phase without a chunk sample`() {
        val store = createStore()
        store.dispatch(LISTEN_REQUESTED)
        store.dispatch(ListenAction.System.Content.ContentReady(languageTag = "en-US"))
        middleware.process(openingSynthesisStartedFact())

        middleware.process(chunkSynthesizedFact(index = 0))

        assertTrue(phaseMarkers().any { it.name == OPENING_SYNTHESIS })
        assertTrue(profiler.markers.any { it.name == "ListenToPage.ChunkSynthesis" })
        assertNull(ListenToPage.chunkSynthesisDuration.testGetValue())
    }

    @Test
    fun `WHEN a later chunk is synthesized during the opening THEN the opening phase continues`() {
        val store = createStore()
        store.dispatch(LISTEN_REQUESTED)
        store.dispatch(ListenAction.System.Content.ContentReady(languageTag = "en-US"))
        middleware.process(openingSynthesisStartedFact())

        middleware.process(chunkSynthesizedFact(index = 1))

        assertTrue(profiler.markers.none { it.name == OPENING_SYNTHESIS })
    }

    @Test
    fun `WHEN synthesis fails during the opening THEN the opening phase is recorded as failed`() {
        val store = createStore()
        store.dispatch(LISTEN_REQUESTED)
        store.dispatch(ListenAction.System.Content.ContentReady(languageTag = "en-US"))
        middleware.process(openingSynthesisStartedFact())

        store.dispatch(ListenAction.System.Synthesis.SynthesisFailed)

        assertTrue(profiler.markers.any { it.name == "$OPENING_SYNTHESIS.Failed" })
        assertNull(ListenToPage.openingSynthesisDuration.testGetValue())
    }

    @Test
    fun `WHEN the page has no content THEN extraction is recorded as failed`() {
        val store = createStore()
        store.dispatch(LISTEN_REQUESTED)

        store.dispatch(ListenAction.System.Content.ContentUnavailable)

        assertEquals(listOf("$TIME_TO_FIRST_AUDIO.Failed", "$EXTRACTION.Failed"), profiler.markers.map { it.name })
        assertNull(ListenToPage.extractionDuration.testGetValue())
    }

    @Test
    fun `WHEN an opening fact comes from another component THEN preparation does not end`() {
        val store = createStore()
        store.dispatch(LISTEN_REQUESTED)
        store.dispatch(ListenAction.System.Content.ContentReady(languageTag = "en-US"))

        middleware.process(openingSynthesisStartedFact().copy(component = Component.FEATURE_DOWNLOADS))

        assertTrue(profiler.markers.none { it.name == PREPARATION })
    }

    @Test
    fun `WHEN a session is stopped THEN a summary of its chunks and gaps is recorded`() {
        val store = createStore()
        store.dispatch(LISTEN_REQUESTED)
        store.dispatch(ListenAction.System.Content.ContentReady(languageTag = "en-US"))
        middleware.process(engineBoundFact("com.example.tts"))
        middleware.process(chunkSynthesizedFact(ListenToPageFacts.ChunkSynthesis(0, 25, 100, 2_500, null)))
        store.dispatch(ListenAction.System.Playback.PlaybackStarted(ChunkState(index = 0), positionMs = 0))
        middleware.process(chunkSynthesizedFact(ListenToPageFacts.ChunkSynthesis(1, 600, 400, 60_000, 900)))
        middleware.process(chunkSynthesizedFact(ListenToPageFacts.ChunkSynthesis(2, 300, 200, null, 500)))
        middleware.process(chunkSynthesizedFact(ListenToPageFacts.ChunkSynthesis(3, 300, 300, null, null)))
        store.dispatch(ListenAction.User.Controls.PlaybackSpeedSelected(PlaybackSpeed.X1_5))
        store.dispatch(ListenAction.System.Playback.PlaybackWaiting)
        now += 800
        store.dispatch(ListenAction.System.Playback.PlaybackStarted(ChunkState(index = 1), positionMs = 0))

        store.dispatch(ListenAction.User.Session.StopRequested())

        assertEquals(
            mapOf(
                "language" to "en-US",
                "engine" to "com.example.tts",
                "chunks" to "3",
                "chunk_chars_avg" to "400",
                "chunk_audio_ms_avg" to "60000",
                "chunk_audio_kb_avg" to "700",
                "chunk_synthesis_ms_avg" to "300",
                "gaps" to "1",
                "gap_ms_total" to "800",
                "playback_speed_end" to "1.5",
            ),
            ListenToPage.synthesisSummary.testGetValue()?.single()?.extra,
        )
    }

    @Test
    fun `WHEN a new request replaces a session THEN the summary of the replaced session is recorded`() {
        val store = createStore()
        store.dispatch(LISTEN_REQUESTED)
        store.dispatch(ListenAction.System.Content.ContentReady(languageTag = "de"))
        middleware.process(chunkSynthesizedFact(ListenToPageFacts.ChunkSynthesis(1, 600, 400, 60_000, null)))

        store.dispatch(LISTEN_REQUESTED)
        middleware.process(chunkSynthesizedFact(ListenToPageFacts.ChunkSynthesis(1, 900, 700, 80_000, null)))
        store.dispatch(ListenAction.User.Session.StopRequested())

        assertEquals(
            listOf("600", "900"),
            ListenToPage.synthesisSummary.testGetValue()?.map { it.extra?.get("chunk_chars_avg") },
        )
        assertEquals("de", ListenToPage.synthesisSummary.testGetValue()?.first()?.extra?.get("language"))
    }

    @Test
    fun `WHEN a session only made its opening THEN no summary is recorded`() {
        val store = createStore()
        store.dispatch(LISTEN_REQUESTED)
        middleware.process(chunkSynthesizedFact(ListenToPageFacts.ChunkSynthesis(0, 25, 100, 2_500, null)))

        store.dispatch(ListenAction.User.Session.StopRequested())

        assertNull(ListenToPage.synthesisSummary.testGetValue())
    }

    @Test
    fun `WHEN a chunk finishes after its session stopped THEN it is not summarized`() {
        val store = createStore()
        store.dispatch(LISTEN_REQUESTED)
        store.dispatch(ListenAction.User.Session.StopRequested())

        middleware.process(chunkSynthesizedFact(ListenToPageFacts.ChunkSynthesis(1, 600, 400, 60_000, null)))
        store.dispatch(LISTEN_REQUESTED)
        store.dispatch(ListenAction.User.Session.StopRequested())

        assertNull(ListenToPage.synthesisSummary.testGetValue())
    }

    @Test
    fun `WHEN no chunk's audio was measured THEN the summary has no average audio length or size`() {
        val store = createStore()
        store.dispatch(LISTEN_REQUESTED)
        middleware.process(chunkSynthesizedFact(ListenToPageFacts.ChunkSynthesis(1, 600, 400, null, null)))

        store.dispatch(ListenAction.User.Session.StopRequested())

        val extra = ListenToPage.synthesisSummary.testGetValue()?.single()?.extra
        assertEquals("1", extra?.get("chunks"))
        assertNull(extra?.get("chunk_audio_ms_avg"))
        assertNull(extra?.get("chunk_audio_kb_avg"))
    }

    /** The markers of time to first audio and its phases, without the per-chunk markers recorded alongside them. */
    private fun phaseMarkers() = profiler.markers.filter { it.name.startsWith(TIME_TO_FIRST_AUDIO) }

    private fun createStore(initialState: ListenState = ListenState()): ListenStore {
        middleware =
            ListenPerformanceMiddleware(
                profiler = profiler,
                scope = CoroutineScope(UnconfinedTestDispatcher()),
                elapsedMs = { now },
            )
        return ListenStore(initialState = initialState, reducer = ::listenReducer, middleware = listOf(middleware))
    }

    private fun openingSynthesisStartedFact() =
        Fact(
            component = Component.FEATURE_LISTENTOPAGE,
            action = Action.IMPLEMENTATION_DETAIL,
            item = ListenToPageFacts.Items.OPENING_SYNTHESIS_STARTED,
        )

    private fun chunkSynthesizedFact(index: Int) =
        Fact(
            component = Component.FEATURE_LISTENTOPAGE,
            action = Action.IMPLEMENTATION_DETAIL,
            item = ListenToPageFacts.Items.CHUNK_SYNTHESIZED,
            metadata =
                mapOf(
                    ListenToPageFacts.MetadataKeys.CHUNK_SYNTHESIS to
                        ListenToPageFacts.ChunkSynthesis(
                            index,
                            length = 25,
                            synthesisMs = 250,
                            audioMs = 2_500,
                            fileKilobytes = 120,
                        )
                ),
        )

    @Test
    fun `WHEN a chunk is synthesized THEN its duration is recorded`() {
        processFact(chunkSynthesizedFact(ListenToPageFacts.ChunkSynthesis(2, 600, 400, 60_000, null)))

        assertEquals(1L, ListenToPage.chunkSynthesisDuration.testGetValue()?.count)
    }

    @Test
    fun `WHEN a chunk is synthesized THEN a marker ending now spans its synthesis`() {
        now = 1000

        processFact(chunkSynthesizedFact(ListenToPageFacts.ChunkSynthesis(2, 600, 400, 60_000, null)))

        assertEquals(
            listOf(
                Marker(
                    name = "ListenToPage.ChunkSynthesis",
                    startTime = 600.0,
                    endTime = 1000.0,
                    text = "index=2, length=600, audioMs=60000, megabytes=null, engine=null",
                )
            ),
            profiler.markers,
        )
    }

    @Test
    fun `WHEN the profiler is not running THEN a chunk's duration is still recorded`() {
        profiler.active = false

        processFact(chunkSynthesizedFact(ListenToPageFacts.ChunkSynthesis(2, 600, 400, null, null)))

        assertTrue(profiler.markers.isEmpty())
        assertEquals(1L, ListenToPage.chunkSynthesisDuration.testGetValue()?.count)
    }

    @Test
    fun `WHEN a fact comes from another component THEN it is ignored`() {
        processFact(
            chunkSynthesizedFact(ListenToPageFacts.ChunkSynthesis(2, 600, 400, null, null))
                .copy(component = Component.FEATURE_DOWNLOADS)
        )

        assertTrue(profiler.markers.isEmpty())
        assertNull(ListenToPage.chunkSynthesisDuration.testGetValue())
    }

    @Test
    fun `WHEN a chunk fact carries no chunk synthesis THEN it is ignored`() {
        processFact(
            chunkSynthesizedFact(ListenToPageFacts.ChunkSynthesis(2, 600, 400, null, null)).copy(metadata = null)
        )

        assertTrue(profiler.markers.isEmpty())
        assertNull(ListenToPage.chunkSynthesisDuration.testGetValue())
    }

    @Test
    fun `WHEN an engine is bound for each session THEN each binding is recorded as an event`() {
        processFact(engineBoundFact("com.example.tts"))
        processFact(engineBoundFact("com.example.other"))

        assertEquals(
            listOf("com.example.tts", "com.example.other"),
            ListenToPage.engineBound.testGetValue()?.map { it.extra?.get("engine") },
        )
    }

    @Test
    fun `WHEN an engine is bound THEN later chunk markers name it`() {
        processFact(engineBoundFact("com.example.tts"))

        processFact(chunkSynthesizedFact(ListenToPageFacts.ChunkSynthesis(2, 600, 400, 60_000, null)))

        assertEquals(
            "index=2, length=600, audioMs=60000, megabytes=null, engine=com.example.tts",
            profiler.markers.single().text,
        )
    }

    @Test
    fun `WHEN the opening chunk is synthesized THEN its size is recorded apart from later chunks`() {
        processFact(chunkSynthesizedFact(ListenToPageFacts.ChunkSynthesis(0, 25, 100, 2_500, 117)))

        assertEquals(1L, ListenToPage.openingChunkAudioSize.testGetValue()?.count)
        // Glean keeps memory in bytes whatever unit it was given in.
        assertEquals(117L * 1024, ListenToPage.openingChunkAudioSize.testGetValue()?.sum)
        assertNull(ListenToPage.chunkAudioSize.testGetValue())
    }

    @Test
    fun `WHEN a later chunk is synthesized THEN its size is recorded apart from the opening`() {
        processFact(chunkSynthesizedFact(ListenToPageFacts.ChunkSynthesis(3, 600, 400, 60_000, 2_813)))

        assertEquals(2_813L * 1024, ListenToPage.chunkAudioSize.testGetValue()?.sum)
        assertNull(ListenToPage.openingChunkAudioSize.testGetValue())
    }

    @Test
    fun `WHEN a chunk has a size THEN its marker shows it in megabytes`() {
        processFact(chunkSynthesizedFact(ListenToPageFacts.ChunkSynthesis(3, 600, 400, 60_000, 2_949)))

        assertEquals(
            "index=3, length=600, audioMs=60000, megabytes=2.88, engine=null",
            profiler.markers.single().text,
        )
    }

    @Test
    fun `WHEN a chunk has no size THEN no size is recorded`() {
        processFact(chunkSynthesizedFact(ListenToPageFacts.ChunkSynthesis(3, 600, 400, 60_000, null)))

        assertNull(ListenToPage.chunkAudioSize.testGetValue())
        assertEquals(1L, ListenToPage.chunkSynthesisDuration.testGetValue()?.count)
    }

    private val factMiddleware by lazy {
        ListenPerformanceMiddleware(
            profiler = profiler,
            scope = CoroutineScope(UnconfinedTestDispatcher()),
            elapsedMs = { now },
        )
    }

    private fun processFact(fact: Fact) = factMiddleware.process(fact)

    private fun engineBoundFact(packageName: String) =
        Fact(
            component = Component.FEATURE_LISTENTOPAGE,
            action = Action.IMPLEMENTATION_DETAIL,
            item = ListenToPageFacts.Items.ENGINE_BOUND,
            value = packageName,
        )

    private fun chunkSynthesizedFact(chunkSynthesis: ListenToPageFacts.ChunkSynthesis) =
        Fact(
            component = Component.FEATURE_LISTENTOPAGE,
            action = Action.IMPLEMENTATION_DETAIL,
            item = ListenToPageFacts.Items.CHUNK_SYNTHESIZED,
            metadata = mapOf(ListenToPageFacts.MetadataKeys.CHUNK_SYNTHESIS to chunkSynthesis),
        )

    private companion object {
        const val TIME_TO_FIRST_AUDIO = "ListenToPage.TimeToFirstAudio"
        val LISTEN_REQUESTED = ListenAction.User.Session.ListenRequested(tabId = "tab", url = "https://example.com")
        const val CHUNK_GAP = "ListenToPage.ChunkGap"
        const val SEEK_LATENCY = "ListenToPage.SeekLatency"
        const val EXTRACTION = "$TIME_TO_FIRST_AUDIO.Extraction"
        const val PREPARATION = "$TIME_TO_FIRST_AUDIO.Preparation"
        const val OPENING_SYNTHESIS = "$TIME_TO_FIRST_AUDIO.OpeningSynthesis"
        const val PLAYBACK_START = "$TIME_TO_FIRST_AUDIO.PlaybackStart"
        const val PHASE_SUM = "$TIME_TO_FIRST_AUDIO.PhaseSum"

        // The phases of each full sequence below: 100ms of extraction, 50 of preparation, 250 of opening synthesis
        // and 100 until playback starts.
        const val PHASE_SUM_TEXT = "Extraction=100, Preparation=50, OpeningSynthesis=250, PlaybackStart=100"

        val playing = ListenState(playbackState = PlaybackState(phase = PlaybackPhase.Playing))
    }
}
