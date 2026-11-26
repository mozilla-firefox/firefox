/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.listentopage

import android.os.SystemClock
import java.util.Locale
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import mozilla.components.concept.base.profiler.Profiler
import mozilla.components.feature.listentopage.ListenAction
import mozilla.components.feature.listentopage.ListenState
import mozilla.components.feature.listentopage.facts.ListenToPageFacts
import mozilla.components.feature.listentopage.playback.PlaybackPhase
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import mozilla.components.support.base.Component
import mozilla.components.support.base.facts.Fact
import mozilla.components.support.base.facts.FactProcessor
import mozilla.components.support.base.log.logger.Logger
import mozilla.telemetry.glean.private.TimingDistributionMetricType
import org.mozilla.fenix.GleanMetrics.ListenToPage

/**
 * [Middleware] that records profiler markers for how long the listen to page feature takes to respond to the user.
 *
 * It is also a [FactProcessor] for measurements the feature records that aren't surfaced as actions. The middleware has
 * to be registered for the facts to be processed correctly.
 *
 * @param profiler Records the markers.
 * @param scope The scope markers are added on. It has to be the main thread, see bug 1960730.
 * @param elapsedMs Gets current time.
 */
class ListenPerformanceMiddleware(
    private val profiler: Profiler,
    private val scope: CoroutineScope = CoroutineScope(Dispatchers.Main),
    private val elapsedMs: () -> Long = SystemClock::elapsedRealtime,
) : Middleware<ListenState, ListenAction>, FactProcessor {

    private val logger = Logger("ListenPerformance")

    private val spans = mutableMapOf<String, Span>()

    // The engine is reported from the thread that binds it, and chunks from the thread that synthesizes them.
    @Volatile private var engine: String? = null

    // Track whether a seek or voice change has stopped audio until plays again. We want to avoid measuring this as part
    // of a gap between chunks - that should solely be synthesis time.
    private var seekOrVoiceChangePending = false

    // How long each phase of time to first audio took this session, in the order they ended.
    private var phaseDurationsMs = emptyMap<String, Long>()

    // Whether the seek being measured had to have its chunk made again, rather than finding it queued.
    private var seekRemade = false

    // Collects useful data while a session is active or null outside a session.
    @Volatile private var summary: SynthesisSummary? = null

    override fun invoke(
        store: Store<ListenState, ListenAction>,
        next: (ListenAction) -> Unit,
        action: ListenAction,
    ) {
        val previous = store.state

        next(action)

        when (action) {
            // Reading starts as soon as the reader asks to listen, so that is when the wait for audio starts.
            is ListenAction.User.Session.ListenRequested -> {
                recordSummary(previous)
                summary = SynthesisSummary()
                seekOrVoiceChangePending = false
                cancelAll(action)
                start(
                    markerName = TIME_TO_FIRST_AUDIO,
                    onEnd = { durationMs -> ListenToPage.timeToFirstAudio.accumulateSamples(listOf(durationMs)) },
                )
                startPhase(EXTRACTION, ListenToPage.extractionDuration)
            }

            is ListenAction.User.Session.StopRequested -> {
                recordSummary(previous)
                summary = null
                seekOrVoiceChangePending = false
                cancelAll(action)
            }

            is ListenAction.System.Content.ContentReady -> {
                end(EXTRACTION)
                if (TIME_TO_FIRST_AUDIO in spans) startPreparation()
            }

            ListenAction.System.Content.ContentUnavailable,
            ListenAction.System.Voices.NoOfflineVoicesAvailable,
            ListenAction.System.Synthesis.SynthesisFailed,
            ListenAction.System.Playback.PlaybackFailed -> failAll(action)

            // Reading starts without a press, so this pauses or resumes it.
            ListenAction.User.Controls.PlayPauseClicked -> {
                cancel(CHUNK_GAP, action)
                cancel(SEEK_LATENCY, action)
            }

            is ListenAction.User.Voices.VoiceSelected,
            is ListenAction.User.Controls.SeekRequested,
            ListenAction.User.Controls.RewindClicked,
            ListenAction.User.Controls.ForwardClicked -> onPlaybackMoved(action, previous.playbackState.phase)

            ListenAction.System.Playback.PlaybackWaiting -> {
                if (SEEK_LATENCY in spans) seekRemade = true
                // Only a wait after audio has played is a gap. The waits for the opening and for a replay are not.
                if (previous.playbackState.phase == PlaybackPhase.Playing) startChunkGap(ChunkGapCause.Waiting)
            }

            is ListenAction.System.Playback.PlaybackStarted -> onPlaying()

            is ListenAction.System.Playback.StateChangeObserved -> action.handleStateChange(previous)

            is ListenAction.System.Voices.AvailableVoicesLoaded,
            ListenAction.User.Voices.VoicesClicked,
            ListenAction.User.Voices.VoicesDismissed,
            ListenAction.User.Controls.PlaybackSpeedClicked,
            ListenAction.User.Controls.PlaybackSpeedDismissed,
            is ListenAction.User.Controls.PlaybackSpeedSelected,
            is ListenAction.System.Playback.ArticleProgressReverted,
            ListenAction.System.Playback.PlaybackEnded,
            is ListenAction.User.ModeChanged,
            ListenAction.User.ErrorDismissed -> Unit

            // The session's own actions, which are internal to it.
            else -> Unit
        }
    }

    /** Starts the preparation, which runs from the article being ready until its opening starts to be synthesized. */
    private fun startPreparation() {
        startPhase(PREPARATION, ListenToPage.preparationDuration)
    }

    private fun startChunkGap(cause: ChunkGapCause) {
        if (seekOrVoiceChangePending) return

        val started =
            start(
                markerName = CHUNK_GAP,
                text = "cause=${cause.name.lowercase()}",
                onEnd = { durationMs ->
                    ListenToPage.chunkGapDuration.accumulateSamples(listOf(durationMs))
                    summary?.addGap(durationMs)
                },
            )
        if (started && cause == ChunkGapCause.Waiting) {
            ListenToPage.audioRanOut.add()
        }
    }

    private fun onPlaying() {
        seekOrVoiceChangePending = false
        if (end(PLAYBACK_START)) recordPhaseSum()
        end(TIME_TO_FIRST_AUDIO)
        end(CHUNK_GAP)
        end(SEEK_LATENCY, text = "path=${if (seekRemade) REMADE else QUEUED}")
    }

    /**
     * A seek or voice change moved playback. An open seek span is dropped, since we drop synthesis for it.
     * User-initiated pauses are also discounted, as we don't want those large gaps represented in the data.
     */
    private fun onPlaybackMoved(action: ListenAction, phase: PlaybackPhase) {
        seekOrVoiceChangePending = true
        cancel(CHUNK_GAP, action)
        cancel(SEEK_LATENCY, action)

        val isSeek = action !is ListenAction.User.Voices.VoiceSelected
        if (isSeek && (phase == PlaybackPhase.Playing || phase == PlaybackPhase.Buffering)) {
            seekRemade = false
            start(
                markerName = SEEK_LATENCY,
                onEnd = { durationMs ->
                    val metric = if (seekRemade) ListenToPage.seekLatency else ListenToPage.seekLatencyQueued
                    metric.accumulateSamples(listOf(durationMs))
                },
            )
        }
    }

    // Returns whether this call started a new marker span
    private fun start(
        markerName: String,
        text: String? = null,
        onEnd: ((durationMs: Long) -> Unit)? = null,
    ): Boolean {
        if (markerName in spans) return false
        val profilerStartTime = profiler.takeIf { it.isProfilerActive() }?.getProfilerTime()
        spans[markerName] = Span(elapsedMs(), profilerStartTime, text, onEnd)
        logger.debug("$markerName started ($text)")
        return true
    }

    private fun end(markerName: String, text: String? = null): Boolean {
        val span = spans.remove(markerName) ?: return false
        val durationMs = record(markerName, span, text ?: span.text)
        span.onEnd?.invoke(durationMs)
        return true
    }

    /** Starts a phase of time to first audio, which records its duration into [metric] and the phase sum. */
    private fun startPhase(markerName: String, metric: TimingDistributionMetricType) {
        start(
            markerName,
            onEnd = { durationMs ->
                metric.accumulateSamples(listOf(durationMs))
                phaseDurationsMs = phaseDurationsMs + (markerName to durationMs)
            },
        )
    }

    /**
     * Records the sum of every phase of time to first audio, ending now. The phases do not have to be contiguous, since
     * play can be pressed long after extraction, so the marker covers their total duration rather than when they ran.
     */
    private fun recordPhaseSum() {
        val phases = phaseDurationsMs
        phaseDurationsMs = emptyMap()
        if (phases.keys != PHASES) return

        val totalMs = phases.values.sum()
        val text = phases.entries.joinToString { (name, ms) -> "${name.removePrefix("$TIME_TO_FIRST_AUDIO.")}=$ms" }
        logger.debug("$PHASE_SUM took ${totalMs}ms ($text)")

        val endTime = profiler.getProfilerTime() ?: return
        scope.launch {
            profiler.addMarker(PHASE_SUM, endTime - totalMs, endTime, text)
        }
    }

    private fun failAll(action: ListenAction) {
        val reason = "failed=${action::class.simpleName}"
        spans.forEach { (markerName, span) ->
            record("$markerName$FAILED_SUFFIX", span, listOfNotNull(reason, span.text).joinToString())
        }
        spans.clear()
        phaseDurationsMs = emptyMap()
    }

    private fun record(markerName: String, span: Span, text: String?): Long {
        val durationMs = elapsedMs() - span.startTimeMs
        logger.debug("$markerName took ${durationMs}ms ($text)")

        val profilerStartTime = span.profilerStartTime
        val profilerEndTime = profiler.getProfilerTime()
        if (profilerStartTime != null && profilerEndTime != null) {
            scope.launch {
                profiler.addMarker(markerName, profilerStartTime, profilerEndTime, text)
            }
        }
        return durationMs
    }

    private fun cancel(markerName: String, action: ListenAction) {
        spans.remove(markerName) ?: return
        logger.debug("$markerName cancelled by ${action::class.simpleName}")
    }

    private fun cancelAll(action: ListenAction) {
        spans.keys.forEach { logger.debug("$it cancelled by ${action::class.simpleName}") }
        spans.clear()
        phaseDurationsMs = emptyMap()
    }

    private fun ListenAction.System.Playback.StateChangeObserved.handleStateChange(prevState: ListenState) =
        when (playbackState.phase) {
            PlaybackPhase.Playing -> onPlaying()
            PlaybackPhase.Paused -> {
                cancel(CHUNK_GAP, this)
                cancel(SEEK_LATENCY, this)
            }
            PlaybackPhase.Buffering if prevState.playbackState.phase == PlaybackPhase.Playing ->
                startChunkGap(ChunkGapCause.Buffering)
            else -> Unit
        }

    override fun process(fact: Fact) {
        if (fact.component != Component.FEATURE_LISTENTOPAGE) return

        when (fact.item) {
            ListenToPageFacts.Items.OPENING_SYNTHESIS_STARTED ->
                if (end(PREPARATION)) {
                    startPhase(OPENING_SYNTHESIS, ListenToPage.openingSynthesisDuration)
                }

            ListenToPageFacts.Items.CHUNK_SYNTHESIZED -> fact.recordChunkSynthesized()
            ListenToPageFacts.Items.ENGINE_BOUND -> fact.recordEngineBound()
        }
    }

    private fun Fact.recordChunkSynthesized() {
        val chunk =
            metadata?.get(ListenToPageFacts.MetadataKeys.CHUNK_SYNTHESIS) as? ListenToPageFacts.ChunkSynthesis ?: return
        if (chunk.index == 0 && end(OPENING_SYNTHESIS)) {
            startPhase(PLAYBACK_START, ListenToPage.playbackStartDuration)
        }

        val text =
            "index=${chunk.index}, length=${chunk.length}, audioMs=${chunk.audioMs}, " +
                "megabytes=${chunk.fileKilobytes?.toMegabytes()}, engine=$engine"

        logger.debug("$CHUNK_SYNTHESIS took ${chunk.synthesisMs}ms ($text)")

        // The opening is a single sentence and has its own measurement, so it is kept out of the other chunks' samples.
        if (chunk.index != 0) {
            ListenToPage.chunkSynthesisDuration.accumulateSamples(listOf(chunk.synthesisMs))
            summary?.addChunk(chunk)
        }
        chunk.fileKilobytes?.let { kilobytes ->
            val size = if (chunk.index == 0) ListenToPage.openingChunkAudioSize else ListenToPage.chunkAudioSize
            size.accumulate(kilobytes)
        }

        val endTime = profiler.getProfilerTime() ?: return
        scope.launch {
            profiler.addMarker(CHUNK_SYNTHESIS, endTime - chunk.synthesisMs, endTime, text)
        }
    }

    private fun Fact.recordEngineBound() {
        val packageName = value ?: return
        engine = packageName
        logger.debug("Speech engine bound: $packageName")
        ListenToPage.engineBound.record(ListenToPage.EngineBoundExtra(engine = packageName))
    }

    /** Records the summary of the session that [state] describes, if it made any chunks after the opening. */
    private fun recordSummary(state: ListenState) {
        val extra =
            summary?.toExtra(
                language = state.languageTag,
                engine = engine,
                playbackSpeed = state.playbackState.speed.multiplier.toString(),
            ) ?: return
        ListenToPage.synthesisSummary.record(extra)
    }

    /**
     * Totals of one session's chunks after the opening, and of its gaps. Facts are processed outside the store's lock
     * and actions can be dispatched from any thread, so its methods are synchronized.
     */
    private class SynthesisSummary {
        private var chunks = 0
        private var chars = 0L
        private var synthesisMs = 0L
        private var measuredChunks = 0
        private var audioMs = 0L
        private var sizedChunks = 0
        private var kilobytes = 0L
        private var gaps = 0
        private var gapMs = 0L

        @Synchronized
        fun addChunk(chunk: ListenToPageFacts.ChunkSynthesis) {
            chunks++
            chars += chunk.length
            synthesisMs += chunk.synthesisMs
            chunk.audioMs?.let {
                measuredChunks++
                audioMs += it
            }
            chunk.fileKilobytes?.let {
                sizedChunks++
                kilobytes += it
            }
        }

        @Synchronized
        fun addGap(durationMs: Long) {
            gaps++
            gapMs += durationMs
        }

        @Synchronized
        fun toExtra(language: String?, engine: String?, playbackSpeed: String): ListenToPage.SynthesisSummaryExtra? {
            if (chunks == 0) return null
            return ListenToPage.SynthesisSummaryExtra(
                language = language,
                engine = engine,
                chunks = chunks,
                chunkCharsAvg = (chars / chunks).toInt(),
                chunkAudioMsAvg = if (measuredChunks > 0) (audioMs / measuredChunks).toInt() else null,
                chunkAudioKbAvg = if (sizedChunks > 0) (kilobytes / sizedChunks).toInt() else null,
                chunkSynthesisMsAvg = (synthesisMs / chunks).toInt(),
                gaps = gaps,
                gapMsTotal = gapMs.toInt(),
                playbackSpeedEnd = playbackSpeed,
            )
        }
    }

    private enum class ChunkGapCause {
        // Waiting on synthesis
        Waiting,

        // The media player experienced an internal buffering state
        Buffering,
    }

    private class Span(
        val startTimeMs: Long,
        val profilerStartTime: Double?,
        val text: String?,
        val onEnd: ((durationMs: Long) -> Unit)?,
    )

    private companion object {
        const val CHUNK_SYNTHESIS = "ListenToPage.ChunkSynthesis"
        const val TIME_TO_FIRST_AUDIO = "ListenToPage.TimeToFirstAudio"
        const val EXTRACTION = "$TIME_TO_FIRST_AUDIO.Extraction"
        const val PREPARATION = "$TIME_TO_FIRST_AUDIO.Preparation"
        const val OPENING_SYNTHESIS = "$TIME_TO_FIRST_AUDIO.OpeningSynthesis"
        const val PLAYBACK_START = "$TIME_TO_FIRST_AUDIO.PlaybackStart"
        const val PHASE_SUM = "$TIME_TO_FIRST_AUDIO.PhaseSum"
        val PHASES = setOf(EXTRACTION, PREPARATION, OPENING_SYNTHESIS, PLAYBACK_START)
        const val CHUNK_GAP = "ListenToPage.ChunkGap"
        const val SEEK_LATENCY = "ListenToPage.SeekLatency"
        const val QUEUED = "queued"
        const val REMADE = "remade"
        const val FAILED_SUFFIX = ".Failed"
    }
}

/** These kilobytes in megabytes, to two decimal places, for reading in a marker or the log. */
private fun Long.toMegabytes(): String = String.format(Locale.ROOT, "%.2f", this / KILOBYTES_PER_MEGABYTE)

private const val KILOBYTES_PER_MEGABYTE = 1024.0
