/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.fakes

import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.Locale
import kotlin.time.Duration
import kotlin.time.Duration.Companion.seconds
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.yield
import mozilla.components.feature.listentopage.PlaybackState
import mozilla.components.feature.listentopage.playback.ArticleDisplayData
import mozilla.components.feature.listentopage.playback.AudioFileCache
import mozilla.components.feature.listentopage.playback.ChunkFile
import mozilla.components.feature.listentopage.playback.PlaybackController
import mozilla.components.feature.listentopage.playback.PlaybackPhase
import mozilla.components.feature.listentopage.playback.PlaybackSpeed
import mozilla.components.feature.listentopage.synthesis.SpeechSynthesisException
import mozilla.components.feature.listentopage.synthesis.SpeechSynthesizer
import mozilla.components.feature.listentopage.voices.Voice

private const val FAKE_ENGINE_ERROR = -1

/**
 * A fake implementation of [SpeechSynthesizer] for use in tests and Compose previews.
 *
 * It never touches the speech engine, and by default it never writes a file either: it names one and records what it
 * was asked to say.
 *
 * @property maxInputLength The limit to report. Set it low to exercise a caller's chunking.
 * @property voices The voices to offer for every language tag. Set it empty to exercise a caller's no-voice handling.
 * @property audioDirectory Where to write the audio, or `null` to name a file without creating one.
 * @property audioDuration How long the audio it writes lasts. Ignored without an [audioDirectory].
 * @property audioDurationOf How long the audio it writes for a text lasts, which is [audioDuration] unless set.
 * @property timePerRequest How long a request takes. Set it to make synthesis slow enough for a caller to be seen
 *   falling behind what is playing, which is otherwise instantaneous and so never behind.
 * @property failAtRequest The request to fail, counting from one, or `null` to fail none. A caller's handling of a
 *   chunk failing part way through an article cannot be exercised by an engine that fails from the start, because the
 *   article never gets going.
 * @property failWith What to fail that request with.
 * @property requests The text of every request, in the order it arrived.
 * @property files Every file it wrote, in the order it wrote them, so a test can name the ones a session left behind.
 * @property voiceRequests The language tag of every voice lookup, in the order it arrived.
 * @property closed Whether [close] has been called.
 * @property enginePackageName The engine to report
 * @property voicesSet Every voice the engine was told to read with, in order.
 */
class FakeSpeechSynthesizer(
    override val maxInputLength: Int = 4000,
    private var voices: List<Voice> = listOf(Voice(id = "voice-1", locale = Locale.US)),
    override val enginePackageName: String = "com.example.tts",
    private val audioDirectory: File? = null,
    private val audioDuration: Duration = 5.seconds,
    private val audioDurationOf: (text: String) -> Duration = { audioDuration },
    private val timePerRequest: Duration = Duration.ZERO,
    private val failAtRequest: Int? = null,
    private val failWith: Exception = SpeechSynthesisException(FAKE_ENGINE_ERROR),
) : SpeechSynthesizer {
    val requests = mutableListOf<String>()
    val files = mutableListOf<File>()
    val voiceRequests = mutableListOf<String>()
    var closed = false
    val voicesSet = mutableListOf<Voice>()

    override suspend fun synthesizeToFile(text: String): File {
        requests.add(text)
        delay(timePerRequest)
        if (requests.size == failAtRequest) {
            throw failWith
        }

        val named = File(audioDirectory ?: File("/audio"), "${requests.size}.wav")

        // The directory is made here rather than once, because a caller emptying its cache deletes the directory
        // itself and the real cache makes it again on the next request.
        files.add(named)

        return named.also {
            if (audioDirectory != null) {
                audioDirectory.mkdirs()
                it.writeBytes(silentWav(audioDurationOf(text)))
            }
        }
    }

    override suspend fun setVoice(voice: Voice) {
        voicesSet.add(voice)
    }

    override fun close() {
        closed = true
    }

    override suspend fun loadAvailableVoices(langTag: String): List<Voice> {
        voiceRequests.add(langTag)
        return voices
    }

    /**
     * Updates the set of available voices. Used for testing voice language updates.
     *
     * @param newVoices The new set of voices to return
     */
    fun updateVoices(newVoices: List<Voice>) {
        this.voices = newVoices
    }
}

/**
 * A fake implementation of [AudioFileCache] for use in tests and Compose previews.
 *
 * It names files without creating them, so nothing has to clean up after it. Having no disk to ask, it takes every file
 * it has not been told is gone to be present, and [reclaim] is how a test tells it otherwise.
 *
 * @property cleared Whether [clear] has been called.
 * @property deleted Every file it was asked to delete, in order.
 * @property heldBytes What [sizeBytes] reports.
 */
class FakeAudioFileCache : AudioFileCache {
    var cleared = false
    val deleted = mutableListOf<File>()
    var heldBytes: Long? = null

    // Every file this has heard of, so that clearing can take away files it never named itself: the engine writes
    // into this directory without going through create(), exactly as the platform one does.
    private val known = mutableSetOf<File>()
    private val missing = mutableSetOf<File>()

    override suspend fun create(key: String): File =
        File("/audio/$key.wav").also {
            known.add(it)
            missing.remove(it)
        }

    override suspend fun exists(file: File): Boolean {
        known.add(file)

        return file !in missing
    }

    override suspend fun delete(file: File) {
        deleted.add(file)
        known.add(file)
        missing.add(file)
    }

    override suspend fun clear() {
        cleared = true
        missing.addAll(known)
    }

    override suspend fun sizeBytes(): Long? = heldBytes

    /**
     * Takes [files] away behind the cache's back, as the system reclaiming the cache directory does.
     *
     * Nothing is recorded, because the point of it is that the session is never told.
     */
    fun reclaim(vararg files: File) {
        known.addAll(files)
        missing.addAll(files)
    }

    /** Takes away every file this has heard of, as the system emptying the cache directory does. */
    fun reclaimEverything() {
        missing.addAll(known)
    }
}

/**
 * A fake implementation of [PlaybackController] for use in tests and Compose previews.
 *
 * It records what it was asked to play rather than starting a media session.
 *
 * @property played Every file it was asked to play, in order.
 * @property queued Every file it was asked to queue behind what is playing, in order. A caller's joins are only gapless
 *   for the chunks that reach this rather than [played], which replaces what is playing.
 * @property displayData What each file in the playlist says it is, in the order the files were handed over.
 * @property paused How many times [pause] has been called.
 * @property resumed How many times [resume] has been called.
 * @property articlesStarted How many times [startArticle] has been called.
 * @property released Whether [release] has been called.
 * @property status What to report about the playback. Set it to drive a caller's monitoring, including changes no
 *   command of theirs asked for.
 * @property shouldReportSeek Whether a seek into an item reports where it went before it returns and then suspends.
 * @property hasSeekedChunk Whether a seek finds the chunk it asks for, as it does unless the player has lost it.
 * @property chunksResumedAfterPlaybackEnded Every chunk it moved on to by itself, as the real controller does when
 *   audio is queued after the playback ran out, in order.
 * @property seekRequests Emit into it to stand in for a seek from outside the app.
 * @property sessionEnded Emit into it to stand in for the playback ending outside the app.
 * @property seekedChunkPositions Every chunk it was asked to seek into, with the position into the article it was asked
 *   to seek to, in order.
 * @property restartedFiles Every file it was restarted on, in order.
 * @property restartedChunks Every chunk it was restarted on, in order.
 * @property restartedPositionsMs Every position into the article it was restarted at, in order.
 * @property restartChunkStartsMs Where in the article each chunk it was restarted on starts, in order.
 * @property articleDurationsMs Every article duration it was told about, in order.
 * @property stopAndBufferPositionsMs Every article position it was asked to stop and buffer at for a seek, in order.
 * @property finishedAtPositionsMs Every article position it was asked to finish the article at, in order.
 * @property fileDurationsMs How long each file it was given lasts, in the order the files were given.
 * @property playbackSpeedSet Every speed it was asked to read at, in order.
 */
class FakePlaybackController(private val shouldReportSeek: Boolean = false) : PlaybackController {
    val played = mutableListOf<File>()
    val queued = mutableListOf<File>()
    val displayDataList = mutableListOf<ArticleDisplayData>()
    var paused = 0
    var resumed = 0
    var articlesStarted = 0
    val seekedChunkPositions = mutableListOf<Pair<Int, Long>>()
    var hasSeekedChunk = true
    val chunksResumedAfterPlaybackEnded = mutableListOf<Int>()
    val restartedFiles = mutableListOf<File>()
    val restartedChunks = mutableListOf<Int>()
    val restartedPositionsMs = mutableListOf<Long>()
    val restartChunkStartsMs = mutableListOf<Long>()
    val articleDurationsMs = mutableListOf<Long>()
    val fileDurationsMs = mutableListOf<Long?>()
    val stopAndBufferPositionsMs = mutableListOf<Long>()
    val finishedAtPositionsMs = mutableListOf<Long>()
    val playbackSpeedSet = mutableListOf<PlaybackSpeed>()
    var released = false

    override val status = MutableStateFlow(PlaybackState())

    override val seekRequests = MutableSharedFlow<Long>(extraBufferCapacity = 1)

    override val sessionEnded = MutableSharedFlow<Unit>(extraBufferCapacity = 1)

    override suspend fun setArticleDuration(durationMs: Long) {
        articleDurationsMs.add(durationMs)
    }

    // What the article now playing says it is, as the real controller keeps it.
    private var displayData: ArticleDisplayData? = null

    override suspend fun startArticle(chunk: ChunkFile, articleDisplayData: ArticleDisplayData, tabId: String) {
        articlesStarted += 1
        fileDurationsMs.add(chunk.durationMs)
        displayData = articleDisplayData
        played.add(chunk.file)
        displayDataList.add(articleDisplayData)

        status.value = PlaybackState(phase = PlaybackPhase.Buffering, speed = status.value.speed)
    }

    override suspend fun enqueue(chunk: ChunkFile) {
        val displayData = displayData ?: return
        fileDurationsMs.add(chunk.durationMs)
        queued.add(chunk.file)
        displayDataList.add(displayData)

        // Like the real one, which moves a player that ran out on to what was added, and so out of having ended.
        if (status.value.phase == PlaybackPhase.Ended) {
            chunksResumedAfterPlaybackEnded.add(chunk.index)
            status.value = status.value.copy(phase = PlaybackPhase.Buffering)
        }
    }

    override suspend fun pause() {
        paused += 1
        status.value = PlaybackState(phase = PlaybackPhase.Paused, speed = status.value.speed)
    }

    override suspend fun resume() {
        resumed += 1
        status.value = PlaybackState(phase = PlaybackPhase.Buffering, speed = status.value.speed)
    }

    override suspend fun seekTo(chunkIndex: Int, positionMs: Long): Boolean {
        seekedChunkPositions.add(chunkIndex to positionMs)
        if (!hasSeekedChunk) return false

        if (shouldReportSeek) {
            status.value =
                status.value.copy(chunk = status.value.chunk.copy(index = chunkIndex), positionMs = positionMs)
            yield()
        }
        return true
    }

    override suspend fun restartAt(chunk: ChunkFile, chunkStartMs: Long, positionMs: Long) {
        fileDurationsMs.add(chunk.durationMs)
        restartedFiles.add(chunk.file)
        restartedChunks.add(chunk.index)
        restartedPositionsMs.add(positionMs)
        restartChunkStartsMs.add(chunkStartMs)
        played.add(chunk.file)
    }

    override suspend fun finishAt(positionMs: Long) {
        finishedAtPositionsMs.add(positionMs)

        // Like the session, which pauses the player and shows the end.
        status.value = status.value.copy(phase = PlaybackPhase.Paused, positionMs = positionMs)
    }

    override suspend fun stopAndBufferAt(positionMs: Long) {
        stopAndBufferPositionsMs.add(positionMs)

        // Like the session, which reports the position it buffers at as buffering, or as paused for a player that was
        // paused.
        val phase = if (status.value.phase == PlaybackPhase.Paused) PlaybackPhase.Paused else PlaybackPhase.Buffering
        status.value = status.value.copy(phase = phase, positionMs = positionMs)
    }

    override suspend fun setSpeed(speed: PlaybackSpeed) {
        playbackSpeedSet.add(speed)
        status.value = status.value.copy(speed = speed)
    }

    override suspend fun release() {
        released = true
        displayData = null
        status.value = PlaybackState()
    }
}

/** A WAV file of [duration]'s worth of silence, in the format the platform engine was measured producing. */
private fun silentWav(duration: Duration): ByteArray {
    val sampleBytes = (duration.inWholeMilliseconds * BYTES_PER_SECOND / MILLIS_PER_SECOND).toInt()
    val buffer = ByteBuffer.allocate(RIFF_HEADER_BYTES + sampleBytes).order(ByteOrder.LITTLE_ENDIAN)

    buffer.put("RIFF".toByteArray(Charsets.US_ASCII))
    buffer.putInt(RIFF_HEADER_BYTES - RIFF_TAG_AND_SIZE_BYTES + sampleBytes)
    buffer.put("WAVE".toByteArray(Charsets.US_ASCII))

    buffer.put("fmt ".toByteArray(Charsets.US_ASCII))
    buffer.putInt(FORMAT_CHUNK_BYTES)
    buffer.putShort(PCM_ENCODING)
    buffer.putShort(CHANNELS)
    buffer.putInt(SAMPLE_RATE)
    buffer.putInt(BYTES_PER_SECOND)
    buffer.putShort(BYTES_PER_FRAME)
    buffer.putShort(BITS_PER_SAMPLE)

    buffer.put("data".toByteArray(Charsets.US_ASCII))
    buffer.putInt(sampleBytes)

    // The samples are left as they were allocated, which for silence is what they should be anyway.
    return buffer.array()
}

// 24 kHz, 16-bit, mono, which is what both measured devices produced for every language and every voice.
private const val SAMPLE_RATE = 24_000
private const val BYTES_PER_SECOND = 48_000
private const val MILLIS_PER_SECOND = 1_000
private const val PCM_ENCODING: Short = 1
private const val CHANNELS: Short = 1
private const val BITS_PER_SAMPLE: Short = 16
private const val BYTES_PER_FRAME: Short = 2

private const val FORMAT_CHUNK_BYTES = 16
private const val RIFF_TAG_AND_SIZE_BYTES = 8
private const val RIFF_HEADER_BYTES = 44
