/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.synthesis

import android.content.Context
import android.os.Bundle
import android.speech.tts.TextToSpeech
import android.speech.tts.TextToSpeech.ERROR_NETWORK
import android.speech.tts.TextToSpeech.ERROR_NETWORK_TIMEOUT
import android.speech.tts.UtteranceProgressListener
import android.speech.tts.Voice as TtsVoice
import java.io.File
import java.util.Locale
import java.util.UUID
import java.util.concurrent.ConcurrentHashMap
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException
import kotlinx.coroutines.CancellableContinuation
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withContext
import mozilla.components.feature.listentopage.playback.AudioFileCache
import mozilla.components.feature.listentopage.voices.Voice

/** Type that is used to manage the synthesis of speech from text. */
interface SpeechSynthesizer {
    /**
     * The longest text this engine accepts in one request. Anything longer is rejected outright, which is why the
     * article is split into chunks before it gets here.
     */
    val maxInputLength: Int
    val enginePackageName: String

    /**
     * Synthesizes [text] and returns the audio file, or throws [SpeechSynthesisException].
     *
     * It is read in the voice [setVoice] was last given.
     */
    suspend fun synthesizeToFile(text: String): File

    /**
     * Reads everything synthesized from here on in [voice], which has to be one [loadAvailableVoices] returned.
     *
     * The engine keeps the voice it was last given, so audio already made stays in the voice it was made with.
     */
    suspend fun setVoice(voice: Voice)

    /** Releases the engine. Nothing may be synthesized afterwards. */
    fun close()

    /**
     * Loads the offline voices the engine has for the language of [langTag], one per region and best first.
     *
     * The region of [langTag] is ignored, so an article in "en-US" is offered the engine's British, Australian and
     * Indian English alongside its American English
     */
    suspend fun loadAvailableVoices(langTag: String): List<Voice>

    companion object {
        /** Construct a SpeechSynthesizer using the standard Android TTS engine. */
        fun android(context: Context, audioCache: AudioFileCache): SpeechSynthesizer =
            AndroidTtsSpeechSynthesizer(context, audioCache)
    }
}

/**
 * Thrown when the speech engine fails.
 *
 * @param errorCode One of the `TextToSpeech.ERROR_*` constants, or the status from a failed engine start.
 */
class SpeechSynthesisException(val errorCode: Int) : Exception("Speech synthesis failed with error code $errorCode")

/** Thrown when the speech engine fails synthesis specifically because an offline voice could not be found. */
class NoOfflineVoiceAvailableException : Exception("Could not find offline voice while attempting speech synthesis")

/**
 * [SpeechSynthesizer] backed by the platform [TextToSpeech] engine, which is the one the user chose in the system
 * settings. This is the only file in the module that imports `android.speech.tts`.
 *
 * @param context Used to bind the engine.
 * @param audioCache Where the audio files are written.
 * @param ioDispatcher The dispatcher the request runs on. `TextToSpeech.synthesizeToFile` reads and writes the disk
 *   before it hands the work to the engine, so it cannot run on the caller's thread: callers dispatch on
 *   `Dispatchers.Main`, where that trips StrictMode.
 */
internal class AndroidTtsSpeechSynthesizer(
    context: Context,
    private val audioCache: AudioFileCache,
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
) : SpeechSynthesizer {

    // The engine binds asynchronously. Without waiting for the callback the first request always fails, so the wait
    // lives inside the first request rather than in a prepare() a caller could forget.
    private val started = CompletableDeferred<Int>()

    private val tts = TextToSpeech(context) { status -> started.complete(status) }

    // The requests the engine is working on, by utterance id. The engine keeps one listener for all of them, so the
    // listener cannot belong to a request: setting it per request drops the callbacks of the request before it and
    // leaves that coroutine suspended for good.
    private val requests = ConcurrentHashMap<String, Request>()

    /*
     * Rank voices by quality first, then latency. Voices with same quality and latency are ranked by name as
     * the Tie break.
     */
    internal val voiceRanking: Comparator<TtsVoice> =
        compareByDescending<TtsVoice> { it.quality }.thenBy { it.latency }.thenBy { it.name }

    init {
        tts.setOnUtteranceProgressListener(
            object : UtteranceProgressListener() {
                override fun onStart(startedId: String?) = Unit

                override fun onDone(doneId: String?) = finish(doneId) { it.continuation.resume(it.file) }

                @Deprecated("Kept because some engines only call the one-argument overload.")
                override fun onError(errorId: String?) = onError(errorId, TextToSpeech.ERROR)

                override fun onError(errorId: String?, errorCode: Int) =
                    finish(errorId) { it.continuation.resumeWithException(SpeechSynthesisException(errorCode)) }
            }
        )
    }

    override val maxInputLength: Int
        get() = TextToSpeech.getMaxSpeechInputLength()

    override val enginePackageName: String
        get() = tts.defaultEngine

    override suspend fun synthesizeToFile(text: String): File {
        val status = started.await()
        if (status != TextToSpeech.SUCCESS) {
            throw SpeechSynthesisException(status)
        }
        require(text.length <= maxInputLength) {
            "Text of ${text.length} characters exceeds the engine limit of $maxInputLength"
        }

        val utteranceId = UUID.randomUUID().toString()
        val file = audioCache.create(utteranceId)
        return try {
            withContext(ioDispatcher) { awaitSynthesis(text, file, utteranceId) }
        } catch (e: SpeechSynthesisException) {
            audioCache.delete(file)
            when (e.errorCode) {
                ERROR_NETWORK_TIMEOUT,
                ERROR_NETWORK -> throw NoOfflineVoiceAvailableException()
                else -> throw e
            }
        }
    }

    override fun close() {
        tts.stop()
        tts.shutdown()
    }

    override suspend fun setVoice(voice: Voice) {
        if (started.await() != TextToSpeech.SUCCESS) {
            return
        }

        runCatching { tts.voices }.getOrNull().orEmpty().firstOrNull { it.name == voice.id }?.let(tts::setVoice)
    }

    /*
    We fetch offline voices by ensuring that the voice both does not require a network connection and is already
    installed. We then keep every region of the article language, and offer the best voice of each.
     */
    override suspend fun loadAvailableVoices(langTag: String): List<Voice> {
        if (started.await() != TextToSpeech.SUCCESS) {
            return emptyList()
        }

        val offlineVoices = runCatching {
            tts.voices.filter { it.isAvailableOffline() }
        }
            .getOrDefault(listOf())
            .associateWith { it.locale.normalized() }

        // Matched on a language range rather than by comparing the language strings, so that a tag too malformed to
        // parse is a language nothing matches instead of a parse failure.
        val ranges = languageRangeOrNull(Locale.forLanguageTag(langTag).language) ?: return emptyList()
        val regions = Locale.filter(ranges, offlineVoices.values.toList()).toSet()

        return offlineVoices
            .filterValues { it in regions }
            .keys
            .sortedWith(voiceRanking)
            // The engines ship several near-identical voices per region, which would fill the list with rows the user
            // cannot tell apart, so each region is offered only by its best.
            .distinctBy { offlineVoices.getValue(it) }
            .map { Voice(id = it.name, locale = offlineVoices.getValue(it)) }
    }

    /**
     * Runs one request and suspends until the engine reports the result.
     *
     * Failure is reported back to [synthesizeToFile] rather than handled here: the listener runs on an engine thread
     * and cleaning up the file has to happen in a coroutine.
     */
    private suspend fun awaitSynthesis(
        text: String,
        file: File,
        utteranceId: String,
    ): File = suspendCancellableCoroutine { continuation ->
        requests[utteranceId] = Request(file, continuation)

        val result = tts.synthesizeToFile(text, Bundle(), file, utteranceId)
        if (result != TextToSpeech.SUCCESS) {
            finish(utteranceId) { it.continuation.resumeWithException(SpeechSynthesisException(result)) }
            return@suspendCancellableCoroutine
        }

        continuation.invokeOnCancellation {
            requests.remove(utteranceId)

            // The engine has no way to cancel one utterance: stop() drops everything it holds. So a caller must not
            // run two requests at once and then cancel one of them.
            //
            // Deliberately leaves the file behind: cancellation can arrive on the main thread and deleting reads the
            // disk. The abandoned file is in the cache directory and goes with the next clear().
            tts.stop()
        }
    }

    /**
     * Hands the request for [utteranceId] its [result], if it has not been given one already.
     *
     * The engine can report one failure twice, by returning an error and by calling the listener. Taking the request
     * out of [requests] is what makes the first report the only one: resuming a continuation twice throws
     * IllegalStateException, which then masks the real error.
     */
    private fun finish(utteranceId: String?, result: (Request) -> Unit) {
        requests.remove(utteranceId ?: return)?.let(result)
    }

    /** One request the engine has been given, waiting for the engine to report on it. */
    private class Request(val file: File, val continuation: CancellableContinuation<File>)

    private fun languageRangeOrNull(langTag: String) =
        Result.runCatching {
                Locale.LanguageRange.parse(langTag)
            }
            .getOrNull()

    private fun android.speech.tts.Voice.isAvailableOffline() =
        !isNetworkConnectionRequired && features.orEmpty().none { it == TextToSpeech.Engine.KEY_FEATURE_NOT_INSTALLED }
}

private val alpha3Languages: Map<String, String> by lazy {
    Locale.getISOLanguages().associateByNotNull { runCatching { Locale.forLanguageTag(it).isO3Language }.getOrNull() }
}

private val alpha3Regions: Map<String, String> by lazy {
    Locale.getISOCountries().associateByNotNull {
        runCatching { Locale.forLanguageTag("und-$it").isO3Country }.getOrNull()
    }
}

private inline fun Array<String>.associateByNotNull(key: (String) -> String?): Map<String, String> =
    mapNotNull { value ->
        key(value)?.takeIf { it.isNotEmpty() }?.let { it to value }
    }
    .toMap()

/**
 * Some engines, Samsung's among them, name the locale of a voice in three-letter codes and append the voice to it as a
 * variant, as in `spa_MEX_default`. Nothing matches those against a language tag, so they are rewritten to the
 * two-letter language and region the rest of the platform uses, dropping the variant.
 */
internal fun Locale.normalized(): Locale {
    val language = alpha3Languages[language] ?: language
    val region = alpha3Regions[country] ?: country
    if (language == this.language && region == country) {
        return this
    }

    return runCatching { Locale.Builder().setLanguage(language).setRegion(region).build() }.getOrDefault(this)
}
