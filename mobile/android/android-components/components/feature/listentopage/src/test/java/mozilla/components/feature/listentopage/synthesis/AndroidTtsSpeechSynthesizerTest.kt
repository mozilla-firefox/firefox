/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.synthesis

import android.speech.tts.TextToSpeech
import android.speech.tts.Voice as TtsVoice
import androidx.test.ext.junit.runners.AndroidJUnit4
import java.io.File
import java.util.Locale
import kotlin.test.assertIs
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.test.runTest
import mozilla.components.feature.listentopage.playback.DirectoryAudioFileCache
import mozilla.components.feature.listentopage.voices.Voice
import mozilla.components.support.test.robolectric.testContext
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import org.junit.runner.RunWith
import org.robolectric.Shadows.shadowOf
import org.robolectric.shadows.ShadowTextToSpeech

private const val NOT_INSTALLED = TextToSpeech.Engine.KEY_FEATURE_NOT_INSTALLED

// There is no Locale constant for it, and it is the third English region the tests need.
private val AUSTRALIA: Locale = Locale.forLanguageTag("en-AU")

@RunWith(AndroidJUnit4::class)
class AndroidTtsSpeechSynthesizerTest {

    @get:Rule val temporaryFolder = TemporaryFolder()

    // Unconfined so that the file work runs in place. The tests that deliver the engine callbacks themselves rely on
    // the request having reached the engine before they fire one.
    private val cache by lazy { DirectoryAudioFileCache({ temporaryFolder.root }, Dispatchers.Unconfined) }

    // Started, because loadAvailableVoices waits for the engine to finish binding and the shadow never fires the
    // init callback of its own accord. Built per test rather than in a @Before, because the synthesis tests fire the
    // callback on an engine of their own and an instance here would be the one the shadow reports as the last.
    private fun startedSynthesizer(): SpeechSynthesizer = synthesizerWith(engineStatus = TextToSpeech.SUCCESS)

    @After
    fun tearDown() {
        ShadowTextToSpeech.reset()
    }

    // The engine reports no voices at all until it has finished binding, and reports them as an empty list rather
    // than as a failure. Without the wait the lookup returns nothing for a language the engine does have voices for,
    // which reaches the user as a language it has no voice for.
    @Test
    fun `test that the lookup waits for the engine to finish binding`() = runTest {
        installVoice("en-us-gonzo", Locale.US)
        val synthesizer = AndroidTtsSpeechSynthesizer(testContext, cache, Dispatchers.Unconfined)

        val voices = async(start = CoroutineStart.UNDISPATCHED) { synthesizer.loadAvailableVoices("en-US") }

        assertTrue(voices.isActive)

        engineShadow().onInitListener.onInit(TextToSpeech.SUCCESS)

        assertEquals(listOf(Voice("en-us-gonzo", Locale.US)), voices.await())
    }

    @Test
    fun `test that an engine that fails to start offers no voices`() = runTest {
        installVoice("en-us-gonzo", Locale.US)
        val synthesizer = synthesizerWith(engineStatus = TextToSpeech.ERROR)

        assertEquals(emptyList<Voice>(), synthesizer.loadAvailableVoices("en-US"))
    }

    @Test
    fun `test that a voice needing a network connection is not offered`() = runTest {
        installVoice("en-us-network", Locale.US, requiresNetwork = true)

        assertEquals(emptyList<Voice>(), startedSynthesizer().loadAvailableVoices("en-US"))
    }

    @Test
    fun `test that a voice that is not installed on the device is not offered`() = runTest {
        installVoice("en-us-absent", Locale.US, features = setOf(NOT_INSTALLED))

        assertEquals(emptyList<Voice>(), startedSynthesizer().loadAvailableVoices("en-US"))
    }

    @Test
    fun `test that only the offline voices are offered when the engine mixes them`() = runTest {
        installVoice("en-us-offline", Locale.US)
        installVoice("en-us-network", Locale.US, requiresNetwork = true)
        installVoice("en-us-absent", Locale.US, features = setOf(NOT_INSTALLED))

        assertEquals(
            listOf(Voice(id = "en-us-offline", locale = Locale.US)),
            startedSynthesizer().loadAvailableVoices("en-US"),
        )
    }

    @Test
    fun `test that every region of the article language is offered and no other language is`() = runTest {
        installVoice("en-us-gonzo", Locale.US)
        installVoice("en-gb-gonzo", Locale.UK)
        installVoice("en-au-gonzo", AUSTRALIA)
        installVoice("de-de-gonzo", Locale.GERMANY)

        val voices = startedSynthesizer().loadAvailableVoices("en-US")

        assertEquals(setOf(Locale.US, Locale.UK, AUSTRALIA), voices.map { it.locale }.toSet())
    }

    // The reader of an American page may well want to hear it in a British accent, so the region of the article
    // language narrows nothing: it is the language alone that decides which voices are offered.
    @Test
    fun `test that the region of the article language does not narrow the list`() = runTest {
        installVoice("en-gb-gonzo", Locale.UK)
        installVoice("en-us-gonzo", Locale.US)

        assertEquals(
            setOf(Locale.UK, Locale.US),
            startedSynthesizer().loadAvailableVoices("en-GB").map { it.locale }.toSet(),
        )
    }

    @Test
    fun `test that another region of the same language is used when the exact region has none`() = runTest {
        installVoice("en-us-gonzo", Locale.US)

        assertEquals(
            listOf(Voice(id = "en-us-gonzo", locale = Locale.US)),
            startedSynthesizer().loadAvailableVoices("en-GB"),
        )
    }

    // The engines ship several near-identical voices per region, which would otherwise fill the list with rows the
    // user cannot tell apart, because a row is named after its region.
    @Test
    fun `test that a region is offered only by its best voice`() = runTest {
        installVoice("en-us-poor", Locale.US, quality = TtsVoice.QUALITY_LOW)
        installVoice("en-us-good", Locale.US, quality = TtsVoice.QUALITY_HIGH)
        installVoice("en-gb-good", Locale.UK, quality = TtsVoice.QUALITY_HIGH)

        assertEquals(
            listOf(Voice("en-gb-good", Locale.UK), Voice("en-us-good", Locale.US)),
            startedSynthesizer().loadAvailableVoices("en-US"),
        )
    }

    @Test
    fun `test that a language with no offline voice is offered nothing`() = runTest {
        installVoice("de-de-gonzo", Locale.GERMANY)

        assertEquals(emptyList<Voice>(), startedSynthesizer().loadAvailableVoices("ja-JP"))
    }

    @Test
    fun `test that an engine with no voices at all offers nothing`() = runTest {
        assertEquals(emptyList<Voice>(), startedSynthesizer().loadAvailableVoices("en-US"))
    }

    @Test
    fun `test that a malformed language tag is offered nothing rather than throwing`() = runTest {
        installVoice("en-us-gonzo", Locale.US)

        assertEquals(emptyList<Voice>(), startedSynthesizer().loadAvailableVoices("not a language tag"))
    }

    @Test
    fun `test that an empty language tag is offered nothing rather than throwing`() = runTest {
        installVoice("en-us-gonzo", Locale.US)

        assertEquals(emptyList<Voice>(), startedSynthesizer().loadAvailableVoices(""))
    }

    // Each region is offered by one voice, so the ranking decides the order the regions come out in.
    @Test
    fun `test that regions are ranked by quality first and then by latency`() = runTest {
        installVoice("normal-fast", Locale.US, quality = TtsVoice.QUALITY_NORMAL, latency = TtsVoice.LATENCY_LOW)
        installVoice("high-slow", Locale.UK, quality = TtsVoice.QUALITY_HIGH, latency = TtsVoice.LATENCY_HIGH)
        installVoice("high-fast", Locale.CANADA, quality = TtsVoice.QUALITY_HIGH, latency = TtsVoice.LATENCY_LOW)
        installVoice("low-fast", AUSTRALIA, quality = TtsVoice.QUALITY_LOW, latency = TtsVoice.LATENCY_VERY_LOW)

        assertEquals(
            listOf("high-fast", "high-slow", "normal-fast", "low-fast"),
            startedSynthesizer().loadAvailableVoices("en-US").map { it.id },
        )
    }

    @Test
    fun `test that a region whose voices are of equal quality and latency is offered by the first by name`() = runTest {
        installVoice("en-us-zeta", Locale.US)
        installVoice("en-us-alpha", Locale.US)

        assertEquals(listOf(Voice("en-us-alpha", Locale.US)), startedSynthesizer().loadAvailableVoices("en-US"))
    }

    @Test
    fun `test that regions whose voices are of equal quality and latency are ranked by name`() = runTest {
        installVoice("en-us-zeta", Locale.US)
        installVoice("en-gb-alpha", Locale.UK)

        assertEquals(
            listOf(Voice("en-gb-alpha", Locale.UK), Voice("en-us-zeta", Locale.US)),
            startedSynthesizer().loadAvailableVoices("en-US"),
        )
    }

    @Test
    fun `test that voices named in three-letter codes are offered under their two-letter locale`() = runTest {
        installVoice("eng-usa", alpha3Locale("eng", "USA", "default"))
        installVoice("eng-gbr", alpha3Locale("eng", "GBR", "default"))
        installVoice("spa-mex", alpha3Locale("spa", "MEX", "default"))

        assertEquals(
            listOf(Voice("eng-gbr", Locale.UK), Voice("eng-usa", Locale.US)),
            startedSynthesizer().loadAvailableVoices("en-US"),
        )
    }

    @Test
    fun `test that voices in three-letter codes that differ only by variant are offered as one region`() = runTest {
        installVoice("por-bra-f00", alpha3Locale("por", "BRA", "f00"), quality = TtsVoice.QUALITY_HIGH)
        installVoice("por-bra-default", alpha3Locale("por", "BRA", "default"))

        assertEquals(
            listOf(Voice("por-bra-f00", Locale.forLanguageTag("pt-BR"))),
            startedSynthesizer().loadAvailableVoices("pt-PT"),
        )
    }

    @Test
    fun `test that a locale already in two-letter codes is left as it is`() {
        assertEquals(Locale.US, Locale.US.normalized())
    }

    // The lookup finds nothing rather than failing, so without this the engine would go on reading in the voice it
    // already had and the caller would never learn that the one it asked for was gone.
    @Test
    fun `test that a voice the engine does not have leaves the one it is reading with alone`() = runTest {
        installVoice("en-us-gonzo", Locale.US)
        val synthesizer = synthesizerWith(engineStatus = TextToSpeech.SUCCESS)
        synthesizer.setVoice(Voice("en-us-gonzo", Locale.US))

        synthesizer.setVoice(Voice("en-us-uninstalled", Locale.US))

        assertEquals("en-us-gonzo", engineShadow().currentVoice.name)
    }

    @Test
    fun `test that a synthesized utterance is returned as a file in the cache`() = runTest {
        val synthesizer = synthesizerWith(engineStatus = TextToSpeech.SUCCESS)

        val file = synthesizer.synthesizeToFile("Article text")

        assertEquals(temporaryFolder.root, file.parentFile)
        assertTrue(file.exists())
        assertEquals("Article text", engineShadow().lastSynthesizeToFileText)
    }

    @Test
    fun `test that every utterance gets its own file`() = runTest {
        val synthesizer = synthesizerWith(engineStatus = TextToSpeech.SUCCESS)

        val first = synthesizer.synthesizeToFile("First")
        val second = synthesizer.synthesizeToFile("Second")

        assertNotEquals(first, second)
    }

    @Test
    fun `test that a failure to start the engine is reported and nothing is synthesized`() = runTest {
        val synthesizer = synthesizerWith(engineStatus = TextToSpeech.ERROR)

        val failure = synthesizer.synthesisFailure("Article text")

        assertEquals(TextToSpeech.ERROR, failure.errorCode)
        assertNull(engineShadow().lastSynthesizeToFileText)
    }

    @Test
    fun `test that an engine error carries its code and leaves no audio behind`() = runTest {
        val synthesizer =
            synthesizerWith(
                engineStatus = TextToSpeech.SUCCESS,
                synthesisResult = TextToSpeech.ERROR_SYNTHESIS,
            )

        val failure = synthesizer.synthesisFailure("Article text")

        assertEquals(TextToSpeech.ERROR_SYNTHESIS, failure.errorCode)
        assertEquals(emptyList<String>(), temporaryFolder.root.list().orEmpty().toList())
    }

    // The engine only reaches for the network when it has no offline voice for the language, so both network codes are
    // reported as a missing offline voice rather than as a generic synthesis failure.
    @Test
    fun `test that a network error is reported as a missing offline voice and leaves no audio behind`() = runTest {
        val synthesizer =
            synthesizerWith(
                engineStatus = TextToSpeech.SUCCESS,
                synthesisResult = TextToSpeech.ERROR_NETWORK,
            )

        synthesizer.noOfflineVoiceFailure("Article text")

        assertEquals(emptyList<String>(), temporaryFolder.root.list().orEmpty().toList())
    }

    @Test
    fun `test that a network timeout is reported as a missing offline voice and leaves no audio behind`() = runTest {
        val synthesizer =
            synthesizerWith(
                engineStatus = TextToSpeech.SUCCESS,
                synthesisResult = TextToSpeech.ERROR_NETWORK_TIMEOUT,
            )

        synthesizer.noOfflineVoiceFailure("Article text")

        assertEquals(emptyList<String>(), temporaryFolder.root.list().orEmpty().toList())
    }

    @Test
    fun `test that text longer than the engine accepts is rejected before anything is written`() = runTest {
        val synthesizer = synthesizerWith(engineStatus = TextToSpeech.SUCCESS)

        val failure = runCatching {
            synthesizer.synthesizeToFile("a".repeat(synthesizer.maxInputLength + 1))
        }
            .exceptionOrNull()

        assertIs<IllegalArgumentException>(failure)
        assertNull(engineShadow().lastSynthesizeToFileText)
    }

    @Test
    fun `test that a result for another utterance is ignored`() = runTest {
        val pending = startPendingSynthesis()

        engineShadow().utteranceProgressListener.onError("another-utterance", TextToSpeech.ERROR_SYNTHESIS)
        engineShadow().utteranceProgressListener.onDone("another-utterance")

        assertTrue(pending.isActive)

        engineShadow().utteranceProgressListener.onDone(lastUtteranceId())

        assertEquals(engineShadow().lastSynthesizeToFile, pending.await().getOrThrow())
    }

    // The engine can report one failure two times, so the second report must not resume the request again. Were the
    // guard against that gone, the listener call below would throw IllegalStateException and fail this test.
    @Test
    fun `test that an error after the utterance finished is ignored`() = runTest {
        val synthesizer = synthesizerWith(engineStatus = TextToSpeech.SUCCESS)

        val file = synthesizer.synthesizeToFile("Article text")
        engineShadow().utteranceProgressListener.onError(lastUtteranceId(), TextToSpeech.ERROR_SYNTHESIS)

        assertTrue(file.exists())
    }

    @Test
    fun `test that a completion after the utterance failed is ignored`() = runTest {
        val synthesizer =
            synthesizerWith(
                engineStatus = TextToSpeech.SUCCESS,
                synthesisResult = TextToSpeech.ERROR_SYNTHESIS,
            )

        val failure = synthesizer.synthesisFailure("Article text")
        engineShadow().utteranceProgressListener.onDone(lastUtteranceId())

        assertEquals(TextToSpeech.ERROR_SYNTHESIS, failure.errorCode)
    }

    @Test
    @Suppress("DEPRECATION")
    fun `test that an error without a code is reported as a generic error`() = runTest {
        val pending = startPendingSynthesis()

        engineShadow().utteranceProgressListener.onError(lastUtteranceId())

        assertEquals(TextToSpeech.ERROR, pending.awaitFailure().errorCode)
        assertEquals(emptyList<String>(), temporaryFolder.root.list().orEmpty().toList())
    }

    @Test
    fun `test that cancelling stops the engine and leaves the audio behind`() = runTest {
        val pending = startPendingSynthesis()
        val file = engineShadow().lastSynthesizeToFile

        pending.cancelAndJoin()

        assertTrue(engineShadow().isStopped)
        assertTrue(file.exists())
    }

    @Test
    fun `test that closing shuts the engine down`() = runTest {
        val synthesizer = synthesizerWith(engineStatus = TextToSpeech.SUCCESS)

        synthesizer.close()

        assertTrue(engineShadow().isShutdown)
    }

    // The engine keeps one listener for every request, so a second request must not cost the first one its callbacks.
    @Test
    fun `test that two requests at once each get their own result`() = runTest {
        val synthesizer = AndroidTtsSpeechSynthesizer(testContext, cache, Dispatchers.Unconfined)
        engineShadow().onInitListener.onInit(TextToSpeech.SUCCESS)

        val first = async(start = CoroutineStart.UNDISPATCHED) { synthesizer.synthesizeToFile("First") }
        val firstId = lastUtteranceId()
        val second = async(start = CoroutineStart.UNDISPATCHED) { synthesizer.synthesizeToFile("Second") }
        val secondId = lastUtteranceId()

        assertNotEquals(firstId, secondId)

        engineShadow().utteranceProgressListener.onDone(secondId)
        engineShadow().utteranceProgressListener.onDone(firstId)

        assertEquals(firstId, first.await().nameWithoutExtension)
        assertEquals(secondId, second.await().nameWithoutExtension)
    }

    /**
     * Builds a synthesizer that reports [synthesisResult] for every request, and fires the init callback, which the
     * shadow does not fire on its own because the real engine delivers it asynchronously.
     *
     * The shadow reports [synthesisResult] through the utterance listener rather than from `synthesizeToFile`, which
     * always returns success. The path that rejects a failed return value therefore cannot be reached from here.
     */
    private fun synthesizerWith(engineStatus: Int, synthesisResult: Int = TextToSpeech.SUCCESS): SpeechSynthesizer {
        val synthesizer = AndroidTtsSpeechSynthesizer(testContext, cache, Dispatchers.Unconfined)

        engineShadow().simulateSynthesizeToFileResult(synthesisResult)
        engineShadow().onInitListener.onInit(engineStatus)

        return synthesizer
    }

    /**
     * Starts a request that stays pending, so that a test can deliver the engine callbacks itself: without a call to
     * [ShadowTextToSpeech.simulateSynthesizeToFileResult] the shadow reports no result of its own.
     * [CoroutineStart.UNDISPATCHED] runs the request as far as its suspension point, so the engine already holds the
     * text when this returns.
     */
    private fun CoroutineScope.startPendingSynthesis(text: String = "Article text"): Deferred<Result<File>> {
        val synthesizer = AndroidTtsSpeechSynthesizer(testContext, cache, Dispatchers.Unconfined)

        engineShadow().onInitListener.onInit(TextToSpeech.SUCCESS)

        return async(start = CoroutineStart.UNDISPATCHED) { runCatching { synthesizer.synthesizeToFile(text) } }
    }

    private fun engineShadow(): ShadowTextToSpeech = shadowOf(ShadowTextToSpeech.getLastTextToSpeechInstance())

    /** The identifier of the request the engine holds, which [AndroidTtsSpeechSynthesizer] keeps to itself. */
    private fun lastUtteranceId(): String = engineShadow().lastSynthesizeToFile.nameWithoutExtension

    private suspend fun SpeechSynthesizer.synthesisFailure(text: String): SpeechSynthesisException {
        val failure = runCatching { synthesizeToFile(text) }.exceptionOrNull()

        assertIs<SpeechSynthesisException>(failure)
        return failure
    }

    private suspend fun SpeechSynthesizer.noOfflineVoiceFailure(text: String): NoOfflineVoiceAvailableException {
        val failure = runCatching { synthesizeToFile(text) }.exceptionOrNull()

        assertIs<NoOfflineVoiceAvailableException>(failure)
        return failure
    }

    private suspend fun Deferred<Result<File>>.awaitFailure(): SpeechSynthesisException {
        val failure = await().exceptionOrNull()

        assertIs<SpeechSynthesisException>(failure)
        return failure
    }

    // The deprecated constructor is the only way to build this, as Locale.Builder rejects three-letter regions.
    @Suppress("DEPRECATION")
    private fun alpha3Locale(language: String, region: String, variant: String) = Locale(language, region, variant)

    private fun installVoice(
        name: String,
        locale: Locale,
        quality: Int = TtsVoice.QUALITY_NORMAL,
        latency: Int = TtsVoice.LATENCY_NORMAL,
        requiresNetwork: Boolean = false,
        features: Set<String> = emptySet(),
    ) = ShadowTextToSpeech.addVoice(TtsVoice(name, locale, quality, latency, requiresNetwork, features))
}
