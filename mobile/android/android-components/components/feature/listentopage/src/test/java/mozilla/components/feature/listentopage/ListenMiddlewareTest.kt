/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage

import androidx.test.ext.junit.runners.AndroidJUnit4
import java.io.File
import java.util.Locale
import kotlin.time.Duration.Companion.milliseconds
import kotlin.time.Duration.Companion.seconds
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.advanceTimeBy
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.withContext
import mozilla.components.browser.state.action.ContentAction
import mozilla.components.browser.state.action.ReaderAction
import mozilla.components.browser.state.action.TabListAction
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.ReaderState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.feature.listentopage.content.Content
import mozilla.components.feature.listentopage.content.ContentProvider
import mozilla.components.feature.listentopage.content.LanguageSource
import mozilla.components.feature.listentopage.content.TextChunker
import mozilla.components.feature.listentopage.facts.ListenToPageFacts
import mozilla.components.feature.listentopage.fakes.FakeAudioFileCache
import mozilla.components.feature.listentopage.fakes.FakePlaybackController
import mozilla.components.feature.listentopage.fakes.FakeSpeechSynthesizer
import mozilla.components.feature.listentopage.playback.AUDIO_WINDOW_RADIUS
import mozilla.components.feature.listentopage.playback.ArticleDisplayData
import mozilla.components.feature.listentopage.playback.AudioFileCache
import mozilla.components.feature.listentopage.playback.DirectoryAudioFileCache
import mozilla.components.feature.listentopage.playback.PlaybackController
import mozilla.components.feature.listentopage.playback.SEEK_BACK_INCREMENT_MS
import mozilla.components.feature.listentopage.playback.SEEK_FORWARD_INCREMENT_MS
import mozilla.components.feature.listentopage.settings.ListenEnabledState
import mozilla.components.feature.listentopage.settings.ListenSettings
import mozilla.components.feature.listentopage.synthesis.NoOfflineVoiceAvailableException
import mozilla.components.feature.listentopage.synthesis.SpeechSynthesisException
import mozilla.components.feature.listentopage.synthesis.SpeechSynthesizer
import mozilla.components.lib.state.Middleware
import mozilla.components.support.base.facts.Facts
import mozilla.components.support.base.facts.processor.CollectionProcessor
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import org.junit.runner.RunWith

private const val TAB_ID = "tab-1"
private const val OTHER_TAB_ID = "tab-2"
private const val URL = "https://example.org/article"
private const val READER_URL = "moz-extension://readerview/readerview.html?url=$URL&id=reader-1"
private const val ENGINE = "com.example.tts"

// Far enough past the window the lookahead fills that a seek there has to make the chunk itself.
private const val SEEK_TARGET_CHUNK = 6

// The opening, then the lookahead filling the window, then whatever a seek asks for next.
private const val FIRST_REQUEST_AFTER_LOOKAHEAD = AUDIO_WINDOW_RADIUS + 2

// How long the audio the fake synthesizer writes lasts, which is what the queue measures every chunk of an article at.
private const val FAKE_AUDIO_MS = 5_000L

/** A chunker that finds nothing to read in anything, which the real one does only for text with no words in it. */
private object NothingToChunk : TextChunker {
    override fun chunk(text: String, maxInputLength: Int, languageTag: String): List<String> = emptyList()

    override fun firstSentenceEnd(text: String, languageTag: String): Int = text.length
}

@OptIn(ExperimentalCoroutinesApi::class)
@RunWith(AndroidJUnit4::class)
class ListenMiddlewareTest {

    @get:Rule val temporaryFolder = TemporaryFolder()

    // The middleware watches the player for as long as a session lasts, so its scope cannot be the test's own: runTest
    // waits for that scope's children and the watch never finishes on its own. These share the test's scheduler, so
    // advanceUntilIdle still drives them, but they are nobody's child and so nothing waits on them. Not
    // backgroundScope, which would be detached too but which advanceUntilIdle does not run: it stops as soon as no
    // foreground work is left.
    private val middlewareScopes = mutableListOf<CoroutineScope>()

    @After
    fun tearDown() {
        middlewareScopes.forEach { it.cancel() }
        middlewareScopes.clear()
        Facts.clearProcessors()
    }

    @Test
    fun `test that binding the engine reports which engine it is`() = runTest {
        val facts = CollectionProcessor().also { Facts.registerProcessor(it) }
        val store =
            storeWith(synthesizerProvider = { FakeSpeechSynthesizer(enginePackageName = ENGINE) }) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }

        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        assertEquals(listOf(ENGINE), facts.engineBoundValues())
    }

    @Test
    fun `test that each session reports the engine it binds`() = runTest {
        val facts = CollectionProcessor().also { Facts.registerProcessor(it) }
        val store =
            storeWith(synthesizerProvider = { FakeSpeechSynthesizer(enginePackageName = ENGINE) }) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()
        store.dispatch(ListenAction.Session.StopRequested())
        advanceUntilIdle()

        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        assertEquals(listOf(ENGINE, ENGINE), facts.engineBoundValues())
    }

    private fun CollectionProcessor.engineBoundValues() =
        facts.filter { it.item == ListenToPageFacts.Items.ENGINE_BOUND }.map { it.value }

    @Test
    fun `test that the article of the requested tab is extracted and its language recorded`() = runTest {
        var extractedTabId: String? = null
        val store = storeWith { tabId ->
            extractedTabId = tabId
            Result.success(Content(text = "Article text", languageTag = "de-DE"))
        }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        assertEquals(TAB_ID, extractedTabId)
        assertEquals("de-DE", store.state.languageTag)
        assertNull(store.state.error)
    }

    @Test
    fun `test that a failed extraction reports the content as unavailable`() = runTest {
        val store = storeWith { Result.failure(RuntimeException("Content failed")) }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        assertEquals(ListenError.ContentUnavailable, store.state.error)
        assertNull(store.state.languageTag)
    }

    @Test
    fun `test that a page with no usable text reports the content as unavailable`() = runTest {
        val store = storeWith { Result.success(Content(text = "   ", languageTag = "en-US")) }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        assertEquals(ListenError.ContentUnavailable, store.state.error)
        assertNull(store.state.languageTag)
    }

    @Test
    fun `test that an article extracted after the session stopped is dropped`() = runTest {
        val extraction = CompletableDeferred<Result<Content>>()
        val store = storeWith { extraction.await() }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()
        store.dispatch(ListenAction.Session.StopRequested())
        extraction.complete(Result.success(Content(text = "Article text", languageTag = "de-DE")))
        advanceUntilIdle()

        assertEquals(ListenState(), store.state)
    }

    @Test
    fun `test that the voices of the article language are loaded once the article is ready`() = runTest {
        val voices =
            listOf(
                Voice(id = "de-de-female", locale = Locale.GERMANY),
                Voice(id = "de-de-male", locale = Locale.GERMANY),
            )
        val synthesizer = FakeSpeechSynthesizer(voices = voices)
        val store =
            storeWith(synthesizerProvider = { synthesizer }) {
                Result.success(Content(text = "Article text", languageTag = "de-DE"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        assertEquals(listOf("de-DE"), synthesizer.voiceRequests)
        assertEquals(voices, store.state.voiceState.availableVoices)
        assertEquals(Voice(id = "de-de-female", locale = Locale.GERMANY), store.state.voiceState.selectedVoice)
        assertEquals(VoiceLoadState.Loaded, store.state.voiceState.loadState)
        assertNull(store.state.error)
    }

    // The engine reads with the voice it was last given, so an article synthesized before the voices are known is read
    // out in whichever voice the engine happens to default to.
    @Test
    fun `test that the voices are loaded before the article is synthesized`() = runTest {
        val calls = mutableListOf<String>()
        val synthesizer =
            object : SpeechSynthesizer {
                override val maxInputLength = 4000

                override val enginePackageName = ENGINE

                override suspend fun synthesizeToFile(text: String): File {
                    calls.add("synthesize")
                    return File("/audio/1.wav")
                }

                override suspend fun setVoice(voice: Voice) = Unit

                override fun close() = Unit

                override suspend fun loadAvailableVoices(langTag: String): List<Voice> {
                    calls.add("loadVoices")
                    return listOf(Voice(id = "en-us-female", locale = Locale.US))
                }
            }
        val store =
            storeWith(synthesizerProvider = { synthesizer }) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        assertEquals(listOf("loadVoices", "synthesize"), calls)
    }

    @Test
    fun `test that an article in a language with no offline voice is not read out`() = runTest {
        val synthesizer = FakeSpeechSynthesizer(voices = emptyList())
        val playback = FakePlaybackController()
        val store =
            storeWith(synthesizerProvider = { synthesizer }, playbackController = playback) {
                Result.success(Content(text = "Article text.", languageTag = "ja-JP"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        assertTrue(synthesizer.requests.isEmpty())
        assertTrue(playback.played.isEmpty())
    }

    @Test
    fun `test that an article language with no offline voice is reported as an error`() = runTest {
        val store =
            storeWith(synthesizerProvider = { FakeSpeechSynthesizer(voices = emptyList()) }) {
                Result.success(Content(text = "Article text", languageTag = "ja-JP"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        assertEquals(ListenError.NoOfflineVoice, store.state.error)
        assertTrue(store.state.voiceState.availableVoices.isEmpty())
        assertEquals(VoiceLoadState.Loaded, store.state.voiceState.loadState)
    }

    @Test
    fun `test that no voices are loaded when the article could not be extracted`() = runTest {
        val synthesizer = FakeSpeechSynthesizer()
        val store =
            storeWith(synthesizerProvider = { synthesizer }) { Result.failure(RuntimeException("Content failed")) }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        assertTrue(synthesizer.voiceRequests.isEmpty())
        assertEquals(ListenError.ContentUnavailable, store.state.error)
    }

    @Test
    fun `test that the extracted article is synthesized and played`() = runTest {
        val synthesizer = FakeSpeechSynthesizer()
        val playback = FakePlaybackController()
        val store =
            storeWith(synthesizerProvider = { synthesizer }, playbackController = playback) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        assertEquals(listOf("Article text."), synthesizer.requests)
        assertEquals(listOf(File("/audio/1.wav")), playback.played)
    }

    @Test
    fun `test that a session is read out as soon as it starts, without a press of play`() = runTest {
        val synthesizer = FakeSpeechSynthesizer()
        val playback = FakePlaybackController()
        val store =
            storeWith(synthesizerProvider = { synthesizer }, playbackController = playback) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }

        store.dispatch(ListenAction.Session.ListenRequested(TAB_ID, URL))
        advanceUntilIdle()

        assertEquals(listOf("en-US"), synthesizer.voiceRequests)
        assertEquals(listOf("Article text."), synthesizer.requests)
        assertEquals(listOf(File("/audio/1.wav")), playback.played)
    }

    @Test
    fun `test that pressing play while the article is playing pauses it`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(playbackController = playback) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()
        playback.status.value = PlaybackState(phase = PlaybackPhase.Playing)
        advanceUntilIdle()

        store.dispatch(ListenAction.Controls.PlayPauseClicked)
        advanceUntilIdle()

        assertEquals(1, playback.paused)
        assertEquals(0, playback.resumed)
    }

    @Test
    fun `test that pressing play while the article is paused resumes it`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(playbackController = playback) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()
        playback.status.value = PlaybackState(phase = PlaybackPhase.Paused)
        advanceUntilIdle()

        store.dispatch(ListenAction.Controls.PlayPauseClicked)
        advanceUntilIdle()

        assertEquals(0, playback.paused)
        assertEquals(1, playback.resumed)
        assertEquals(1, playback.played.size)
    }

    @Test
    fun `test that a session shows buffering while its opening is made`() = runTest {
        val synthesizer = FakeSpeechSynthesizer(timePerRequest = 1.seconds)
        val store =
            storeWith(synthesizerProvider = { synthesizer }) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }

        store.dispatch(ListenAction.Session.ListenRequested(TAB_ID, URL))
        advanceTimeBy(500.milliseconds)

        assertTrue("the opening was already made", synthesizer.files.isEmpty())
        assertEquals(PlaybackPhase.Buffering, store.state.playbackState.phase)
    }

    @Test
    fun `test that a press of play while the opening is made does not read the article out twice`() = runTest {
        val synthesizer = FakeSpeechSynthesizer(timePerRequest = 1.seconds)
        val playback = FakePlaybackController()
        val store =
            storeWith(synthesizerProvider = { synthesizer }, playbackController = playback) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }

        store.dispatch(ListenAction.Session.ListenRequested(TAB_ID, URL))
        store.dispatch(ListenAction.Controls.PlayPauseClicked)
        advanceUntilIdle()

        assertEquals(listOf("Article text."), synthesizer.requests)
        assertEquals(listOf(File("/audio/1.wav")), playback.played)
    }

    @Test
    fun `test that every chunk of the article says what the notification shows`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(
                synthesizerProvider = { FakeSpeechSynthesizer(maxInputLength = 20) },
                playbackController = playback,
                browserStore = browserStoreWithOpenTabs(title = "An article"),
            ) {
                Result.success(Content(text = "One. Two. Three is over the limit.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        assertTrue(playback.queued.isNotEmpty())
        assertEquals(playback.played.size + playback.queued.size, playback.displayDataList.size)
        assertEquals(
            setOf(ArticleDisplayData(title = "An article", site = "example.org")),
            playback.displayDataList.toSet(),
        )
    }

    @Test
    fun `test that a reader view article is named by its own site rather than the reader URL`() = runTest {
        val playback = FakePlaybackController()
        val readerTab =
            createTab(
                url = READER_URL,
                id = TAB_ID,
                readerState = ReaderState(active = true, activeUrl = URL),
            )
        val store =
            storeWith(
                playbackController = playback,
                browserStore = BrowserStore(BrowserState(tabs = listOf(readerTab), selectedTabId = TAB_ID)),
            ) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        assertEquals("example.org", playback.displayDataList.first().site)
    }

    @Test
    fun `test that a URL naming no site leaves the notification without one`() = runTest {
        val playback = FakePlaybackController()
        val hostless = createTab(url = "data:text/html,<p>Article</p>", id = TAB_ID)
        val store =
            storeWith(
                playbackController = playback,
                browserStore = BrowserStore(BrowserState(tabs = listOf(hostless), selectedTabId = TAB_ID)),
            ) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        assertNull(playback.displayDataList.first().site)
    }

    @Test
    fun `test that an article from a private tab is described as private`() = runTest {
        val playback = FakePlaybackController()
        val privateTab = createTab(url = URL, id = TAB_ID, title = "An article", private = true)
        val store =
            storeWith(
                playbackController = playback,
                browserStore = BrowserStore(BrowserState(tabs = listOf(privateTab), selectedTabId = TAB_ID)),
            ) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        assertEquals(
            ArticleDisplayData(title = "An article", site = "example.org", isPrivate = true),
            playback.displayDataList.first(),
        )
        assertEquals("An article", store.state.title)
        assertEquals("example.org", store.state.site)
    }

    // Reported as having no title rather than given one, so that whatever reads this knows the page itself was
    // untitled. The notification shows the app's name in that case, and puts it on in ListenPlaybackController.
    @Test
    fun `test that a page with no title is reported as having none rather than given one`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(playbackController = playback) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        assertEquals(ArticleDisplayData(title = null, site = "example.org"), playback.displayDataList.first())
        assertNull(store.state.title)
    }

    @Test
    fun `test that the store holds the title and site the notification shows, before the article is read out`() =
        runTest {
            val store =
                storeWith(browserStore = browserStoreWithOpenTabs(title = "An article")) {
                    Result.success(Content(text = "Article text.", languageTag = "en-US"))
                }
            store.dispatch(ListenAction.Session.ListenRequested(TAB_ID, URL))
            advanceUntilIdle()

            assertEquals("An article", store.state.title)
            assertEquals("example.org", store.state.site)
        }

    @Test
    fun `test that the article is opened on its first sentence`() = runTest {
        val synthesizer = FakeSpeechSynthesizer(maxInputLength = 20)
        val playback = FakePlaybackController()
        val store =
            storeWith(synthesizerProvider = { synthesizer }, playbackController = playback) {
                Result.success(Content(text = "One. Two. Three is over the limit.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        // The requests after it are the queue working ahead of what is playing.
        assertEquals("One.", synthesizer.requests.first())
        assertEquals(1, playback.played.size)
    }

    @Test
    fun `test that an article with no sentence end that fits is cut at the engine limit`() = runTest {
        val synthesizer = FakeSpeechSynthesizer(maxInputLength = 5)
        val playback = FakePlaybackController()
        val store =
            storeWith(synthesizerProvider = { synthesizer }, playbackController = playback) {
                Result.success(Content(text = "abcdefghij", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        assertEquals("abcde", synthesizer.requests.first())
    }

    @Test
    fun `test that the first request is one sentence, not everything the engine accepts`() = runTest {
        val synthesizer = FakeSpeechSynthesizer()
        val article = (1..100).joinToString(" ") { "Sentence $it is right here." }
        val store =
            storeWith(synthesizerProvider = { synthesizer }) {
                Result.success(Content(text = article, languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        // The article is inside the engine's limit, so before this it went to the engine in one request and nothing
        // played until the whole of it had been synthesized.
        assertEquals("Sentence 1 is right here.", synthesizer.requests.first())
        assertTrue(article.length > synthesizer.requests.first().length * 50)
    }

    @Test
    fun `test that the article language source is reported with the content`() = runTest {
        val actions = mutableListOf<ListenAction>()
        val store =
            storeWith(recordInto = actions) {
                Result.success(
                    Content(
                        text = "An article in the fallback language.",
                        languageTag = "en-US",
                        languageSource = LanguageSource.Fallback,
                    )
                )
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        val ready = actions.filterIsInstance<ListenAction.Content.ContentReady>().single()
        assertEquals(LanguageSource.Fallback, ready.languageSource)
    }

    // The engine offers a voice here, so the synthesis is reached and it is the synthesis that fails.
    @Test
    fun `test that a synthesis failure does not play anything`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(
                synthesizerProvider = { failingSynthesizer { throw SpeechSynthesisException(-1) } },
                playbackController = playback,
            ) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        assertTrue(playback.played.isEmpty())
    }

    @Test
    fun `test that a synthesis failure is reported rather than only logged`() = runTest {
        val failing =
            object : SpeechSynthesizer {
                override val maxInputLength = 4000

                override val enginePackageName = "com.example.tts"

                override suspend fun synthesizeToFile(text: String): File = throw SpeechSynthesisException(-1)

                override suspend fun setVoice(voice: Voice) = Unit

                override fun close() = Unit

                override suspend fun loadAvailableVoices(langTag: String): List<Voice> =
                    listOf(Voice(id = "en-us-female", locale = Locale.US))
            }
        val store =
            storeWith(synthesizerProvider = { failing }) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        assertEquals(ListenError.SynthesisFailed, store.state.error)
    }

    // The voice list is not empty here, so the error can only have come from the synthesis path rather than from the
    // check that reports a language with no offline voice before anything is synthesized.
    @Test
    fun `test that a synthesis failing for want of an offline voice reports it as an error`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(
                synthesizerProvider = { failingSynthesizer { throw NoOfflineVoiceAvailableException() } },
                playbackController = playback,
            ) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        assertEquals(ListenError.NoOfflineVoice, store.state.error)
        assertTrue(playback.played.isEmpty())
    }

    @Test
    fun `test that a synthesis failing for reasons other than an offline voice is reported`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(
                synthesizerProvider = { failingSynthesizer { throw SpeechSynthesisException(-1) } },
                playbackController = playback,
            ) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        assertEquals(ListenError.SynthesisFailed, store.state.error)
        assertTrue(playback.played.isEmpty())
    }

    // The point of the playlist. A chunk handed over in place of what is playing cannot be joined onto it, and the
    // silence while the player tears one file down and prepares the next is what a reader hears at every boundary.
    @Test
    fun `test that the chunks after the opening are queued behind it rather than played in its place`() = runTest {
        val synthesizer = FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root)
        val playback = FakePlaybackController()
        val store =
            storeWith(synthesizerProvider = { synthesizer }, playbackController = playback) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        val opening = playback.played.single()
        assertTrue("nothing was queued behind the opening", playback.queued.isNotEmpty())
        assertFalse("the opening was queued as well as played", playback.queued.contains(opening))
    }

    // The chunks the lookahead has made are queued while the opening is still playing rather than when it ends,
    // because a player can only read ahead across a join into an item it already holds.
    @Test
    fun `test that the chunk after the opening is queued before the opening has finished`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(
                synthesizerProvider = { FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root) },
                playbackController = playback,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }

        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        // No report of the opening ending, and no report of it playing either: the queueing cannot be waiting on the
        // player to say anything.
        assertEquals(PlaybackPhase.Buffering, store.state.playbackState.phase)
        assertTrue(playback.queued.isNotEmpty())
    }

    @Test
    fun `test that the article keeps going for as long as chunks are left`() = runTest {
        val synthesizer = FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root)
        val playback = FakePlaybackController()
        val store =
            storeWith(synthesizerProvider = { synthesizer }, playbackController = playback) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        // The player walks its own playlist, so a report is it saying which chunk it has reached rather than asking
        // for the next one.
        repeat(4) { chunk ->
            playback.status.value = PlaybackState(phase = PlaybackPhase.Playing, chunk = ChunkState(index = chunk))
            advanceUntilIdle()
        }

        // The queue keeps running ahead of the reader, and no chunk is ever handed over twice.
        val handedOver = playback.played + playback.queued
        assertTrue("the article stopped at chunk ${handedOver.size}", handedOver.size > 4)
        assertEquals(handedOver.distinct(), handedOver)
        assertEquals(3, store.state.playbackState.chunk.index)
        assertNull(store.state.error)
    }

    @Test
    fun `test that the end of the last chunk ends the article rather than looping`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(
                synthesizerProvider = { FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root) },
                playbackController = playback,
            ) {
                Result.success(Content(text = "The only sentence there is.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        playback.status.value = PlaybackState(phase = PlaybackPhase.Ended)
        advanceUntilIdle()

        assertEquals(1, playback.played.size)
    }

    @Test
    fun `test that a report which is not the end of a chunk plays nothing new`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(
                synthesizerProvider = { FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root) },
                playbackController = playback,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        listOf(PlaybackPhase.Playing, PlaybackPhase.Paused, PlaybackPhase.Buffering).forEach {
            playback.status.value = PlaybackState(phase = it, positionMs = 1_000)
            advanceUntilIdle()
        }

        assertEquals(1, playback.played.size)
    }

    // Handling an end report is asynchronous: refillOrEnd reads appendedThrough, then queues the work behind
    // requestTurn, so a second report can compute the same missing chunk before the first has queued it. Enqueuing
    // republishes the player's state, so the refill can provoke the very report it then has to ignore.
    @Test
    fun `test that a double report of playback ending enqueues the next chunk only once`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(
                synthesizerProvider = {
                    FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root, timePerRequest = 1.seconds)
                },
                playbackController = playback,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)

        // Far enough for the opening to be playing, not far enough for the lookahead behind it to have finished, so
        // that the refill is still waiting its turn when the second report arrives.
        advanceTimeBy(1500.milliseconds)
        val queuedBefore = playback.queued.size

        // The positions differ only so that the two reports are different values. A StateFlow drops a value equal to
        // the one before it, so repeating the same position would leave the second report undelivered and the test
        // passing without ever exercising the guard. Neither number stands for anything the player measured.
        playback.status.value = PlaybackState(phase = PlaybackPhase.Ended, positionMs = 30_000)
        advanceTimeBy(10.milliseconds)
        playback.status.value = PlaybackState(phase = PlaybackPhase.Ended, positionMs = 30_004)
        advanceUntilIdle()

        // Without the guard the second report queues the same chunk the first one did, and the reader hears it twice.
        assertTrue(playback.queued.size > queuedBefore)
        assertEquals(playback.queued.distinct(), playback.queued)
    }

    @Test
    fun `test that the article progress covers the chunks that have not been synthesized yet`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(
                synthesizerProvider = { FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root) },
                playbackController = playback,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        // The article is longer than the handful of chunks that have audio, because the rest of it is sized from its
        // characters at the rate the opening measured at rather than counting as nothing.
        val measuredSoFarMs = (AUDIO_WINDOW_RADIUS + 1) * FAKE_AUDIO_MS
        assertTrue(
            "the article is only ${store.state.articleProgress.durationMs}ms long",
            store.state.articleProgress.durationMs > measuredSoFarMs,
        )
        assertEquals(0f, store.state.articleProgress.fraction, 0f)
    }

    @Test
    fun `test that the article progress counts the chunks already played`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(
                synthesizerProvider = { FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root) },
                playbackController = playback,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        // The playlist moves on to the second chunk, which the player reports as the item it is playing, at a position
        // the session counts across the whole article.
        playback.status.value =
            PlaybackState(
                phase = PlaybackPhase.Playing,
                chunk = ChunkState(index = 1),
                positionMs = FAKE_AUDIO_MS + 1_000,
            )
        advanceUntilIdle()

        // The first chunk in full, and a second of the one after it, even though the bar is nowhere near the end.
        assertEquals(FAKE_AUDIO_MS + 1_000, store.state.articleProgress.positionMs)
        assertTrue(
            "the bar is at ${store.state.articleProgress.fraction}",
            store.state.articleProgress.fraction < 1f,
        )
    }

    @Test
    fun `test that the article progress reaches the end of the article`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(
                synthesizerProvider = { FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root) },
                playbackController = playback,
            ) {
                Result.success(Content(text = "The only sentence there is.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        playback.status.value = PlaybackState(phase = PlaybackPhase.Playing, positionMs = 2_000)
        advanceUntilIdle()
        val midway = store.state.articleProgress

        // Short of the length of the audio, as the last position the player samples before a chunk ends always is.
        playback.status.value = PlaybackState(phase = PlaybackPhase.Ended, positionMs = FAKE_AUDIO_MS - 3)
        advanceUntilIdle()
        val ended = store.state.articleProgress

        assertEquals(2_000, midway.positionMs)
        assertEquals(FAKE_AUDIO_MS, midway.durationMs)
        assertEquals(1f, ended.fraction, 0f)
        assertEquals(ended.durationMs, ended.positionMs)
    }

    // The rate is measured from the opening's own audio, so a session whose audio cannot be measured at all has no
    // basis for any estimate. The bar reads zero for the whole of it rather than guessing.
    @Test
    fun `test that the article progress stays at zero while nothing can be measured`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(synthesizerProvider = { FakeSpeechSynthesizer() }, playbackController = playback) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        playback.status.value = PlaybackState(phase = PlaybackPhase.Playing, positionMs = 4_000)
        advanceUntilIdle()

        assertEquals(ArticleProgress(), store.state.articleProgress)
        assertEquals(0f, store.state.articleProgress.fraction, 0f)
    }

    @Test
    fun `test that a new session starts its article from the beginning of the bar`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(
                synthesizerProvider = { FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root) },
                playbackController = playback,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()
        playback.status.value = PlaybackState(phase = PlaybackPhase.Ended, positionMs = FAKE_AUDIO_MS)
        advanceUntilIdle()
        assertTrue(store.state.articleProgress.positionMs > 0)

        store.listenAndPlay(OTHER_TAB_ID, URL)
        advanceUntilIdle()

        assertEquals(0, store.state.articleProgress.positionMs)
    }

    @Test
    fun `test that a player running dry mid-article is a wait rather than the end of the article`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(
                synthesizerProvider = { FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root) },
                playbackController = playback,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()
        val queuedBefore = playback.queued.size

        playback.status.value = PlaybackState(phase = PlaybackPhase.Ended)
        advanceUntilIdle()

        assertEquals(PlaybackPhase.Buffering, store.state.playbackState.phase)
        assertTrue("nothing was queued for the reader to carry on with", playback.queued.size > queuedBefore)
        assertEquals(playback.queued.distinct(), playback.queued)
        assertNull(store.state.error)
    }

    @Test
    fun `test that running out with no chunks left is the end of the article`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(
                synthesizerProvider = { FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root) },
                playbackController = playback,
            ) {
                Result.success(Content(text = "The only sentence there is.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        playback.status.value = PlaybackState(phase = PlaybackPhase.Ended)
        advanceUntilIdle()

        assertEquals(PlaybackPhase.Ended, store.state.playbackState.phase)
        assertEquals(1, playback.played.size)
        assertTrue(playback.queued.isEmpty())
        assertEquals(0, playback.resumed)
        assertNull(store.state.error)
    }

    @Test
    fun `test that a player running dry is started again on the chunk queued behind it`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(
                synthesizerProvider = { FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root) },
                playbackController = playback,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()
        val queuedBefore = playback.queued.size

        playback.status.value = PlaybackState(phase = PlaybackPhase.Ended)
        advanceUntilIdle()

        // The refill queues its chunk behind the opening and the chunks queued before it. Its playlist index counts
        // the opening too.
        assertEquals(listOf(queuedBefore + 1), playback.chunksResumedAfterPlaybackEnded)
        // Resuming would ask for audio focus again, and take it from an app that started playing while we waited.
        assertEquals(0, playback.resumed)
    }

    @Test
    fun `test that failing to work ahead does not put an error over a player still reading`() = runTest {
        // The opening and the chunk after it are made; the one after that is not. Nobody is waiting on it, so the
        // article carries on and the reader is told nothing.
        val synthesizer = FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root, failAtRequest = 3)
        val playback = FakePlaybackController()
        val store =
            storeWith(synthesizerProvider = { synthesizer }, playbackController = playback) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        // The opening plays and the one chunk that was made is queued behind it, with nothing said about the third.
        assertEquals(1, playback.played.size)
        assertEquals(1, playback.queued.size)
        assertNull(store.state.error)

        // And the article still moves on, onto the chunk that was made before the failure.
        playback.status.value = PlaybackState(phase = PlaybackPhase.Playing, chunk = ChunkState(index = 1))
        advanceUntilIdle()

        assertNull(store.state.error)
    }

    @Test
    fun `test that a chunk the lookahead never made is made on its own when the player runs dry`() = runTest {
        // The lookahead fails on the chunk after the opening, so the player runs dry on the opening. This will cause
        // the next chunk to be manually created
        val synthesizer =
            FakeSpeechSynthesizer(
                audioDirectory = temporaryFolder.root,
                timePerRequest = 1.seconds,
                failAtRequest = 2,
            )
        val playback = FakePlaybackController()
        val store =
            storeWith(synthesizerProvider = { synthesizer }, playbackController = playback) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        assertEquals(1, playback.played.size)
        assertTrue("the lookahead queued a chunk it never made", playback.queued.isEmpty())

        playback.status.value = PlaybackState(phase = PlaybackPhase.Ended)
        advanceTimeBy(1100.milliseconds)

        assertEquals(1, playback.queued.size)
    }

    @Test
    fun `test that the lookahead is not thrown away by the move to the next chunk`() = runTest {
        // Every chunk of the window is asked for once. Cancelling the request in flight to move the article on made
        // the engine do the same chunk again, and left the half-written file behind with nothing tracking it.
        val synthesizer =
            FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root, timePerRequest = 500.milliseconds)
        val playback = FakePlaybackController()
        val store =
            storeWith(synthesizerProvider = { synthesizer }, playbackController = playback) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        repeat(3) {
            playback.status.value = PlaybackState(phase = PlaybackPhase.Playing)
            advanceUntilIdle()
            playback.status.value = PlaybackState(phase = PlaybackPhase.Ended)
            advanceUntilIdle()
        }

        assertEquals(synthesizer.requests.distinct(), synthesizer.requests)
    }

    @Test
    fun `test that a request in place of a live session stops the one before it moving on`() = runTest {
        val playback = FakePlaybackController()
        val extraction = CompletableDeferred<Result<Content>>()
        val store =
            storeWith(
                synthesizerProvider = { FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root) },
                playbackController = playback,
            ) { tabId ->
                if (tabId == TAB_ID) {
                    Result.success(Content(text = longArticle(), languageTag = "en-US"))
                } else {
                    extraction.await()
                }
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()
        val readSoFar = playback.played.size

        // The second session is still being extracted, and the player still holds the first article's opening. The end
        // of that opening belongs to a session the reader has left, so it must not read out another of its chunks.
        store.listenAndPlay(OTHER_TAB_ID, URL)
        advanceUntilIdle()
        playback.status.value = PlaybackState(phase = PlaybackPhase.Ended)
        advanceUntilIdle()

        assertEquals(readSoFar, playback.played.size)

        // Let the second extraction finish, so the middleware has nothing left running when the body returns.
        extraction.complete(Result.failure(RuntimeException("Extraction abandoned")))
        advanceUntilIdle()
    }

    @Test
    fun `test that a request in place of a live session throws away only that session's audio`() = runTest {
        val cache = FakeAudioFileCache()
        val synthesizer = FakeSpeechSynthesizer()
        val store =
            storeWith(synthesizerProvider = { synthesizer }, audioCache = cache) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        // A session replacing another keeps the engine, so this one fake writes for both and its files stay uniquely
        // named across them. Everything it wrote up to here belongs to the article the reader is leaving.
        val leftBehind = synthesizer.files.toList()

        store.listenAndPlay(OTHER_TAB_ID, URL)
        advanceUntilIdle()

        assertTrue("Nothing was made to delete", leftBehind.isNotEmpty())
        assertEquals(leftBehind.toSet(), cache.deleted.toSet())
        assertFalse(cache.cleared)
    }

    @Test
    fun `test that a session starting while the last one is still ending keeps its own audio`() = runTest {
        val directory = temporaryFolder.newFolder("audio")
        val cache = slowToEmpty(DirectoryAudioFileCache({ directory }, ioDispatcher = Dispatchers.Unconfined))
        val store =
            storeWith(
                synthesizerProvider = { FakeSpeechSynthesizer(audioDirectory = directory) },
                audioCache = cache,
            ) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        // Nothing advanced between them, so the stop is still emptying the cache directory when the session after it
        // starts writing into it. Emptying is a recursive delete of the directory both of them share.
        store.dispatch(ListenAction.Session.StopRequested())
        store.listenAndPlay(OTHER_TAB_ID, URL)
        advanceUntilIdle()

        assertTrue("The new session's audio was deleted under it", directory.list().orEmpty().isNotEmpty())
    }

    @Test
    fun `test that an article the chunker finds nothing to read in is reported as unavailable content`() = runTest {
        val synthesizer = FakeSpeechSynthesizer()
        val store =
            storeWith(
                synthesizerProvider = { synthesizer },
                chunker = NothingToChunk,
            ) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        // The page is what had nothing in it, so the reader gets the snackbar for an article that cannot be read and
        // not the dialog that an engine failing part way through an article puts over the player.
        assertEquals(ListenError.ContentUnavailable, store.state.error)
        assertEquals(emptyList<String>(), synthesizer.requests)
    }

    @Test
    fun `test that a new session starts from its own opening rather than where the last one had got to`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(
                synthesizerProvider = { FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root) },
                playbackController = playback,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()
        playback.status.value = PlaybackState(phase = PlaybackPhase.Playing, chunk = ChunkState(index = 1))
        advanceUntilIdle()
        val readTo = playback.queued.last()

        store.dispatch(ListenAction.Session.StopRequested())
        advanceUntilIdle()
        store.listenAndPlay(OTHER_TAB_ID, URL)
        advanceUntilIdle()

        // The new session starts its own playlist rather than queueing behind where the last one had got to.
        assertNotEquals(readTo, playback.played.last())
        assertEquals(2, playback.played.size)
    }

    @Test
    fun `test that stopping the session gives up the playback`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(playbackController = playback) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()
        store.dispatch(ListenAction.Session.StopRequested())
        advanceUntilIdle()

        assertTrue(playback.released)
    }

    @Test
    fun `test that stopping waits for a request the engine has already taken before emptying the cache`() = runTest {
        val directory = File(temporaryFolder.root, "audio")
        val cache = DirectoryAudioFileCache({ directory }, ioDispatcher = Dispatchers.Unconfined)
        val store =
            storeWith(synthesizerProvider = { lateWritingSynthesizer(cache) }, audioCache = cache) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)

        // Stop while the engine still has the request, so its write lands after the session has been told to stop.
        advanceTimeBy(500.milliseconds)
        store.dispatch(ListenAction.Session.StopRequested())
        advanceUntilIdle()

        assertEquals(emptyList<String>(), directory.list()?.toList().orEmpty())
    }

    /**
     * An engine whose write cannot be cancelled, which is what the platform one is: the work is in another app's
     * process, so our cancellation asks it to stop but cannot un-write a file it has already produced.
     */
    private fun lateWritingSynthesizer(cache: AudioFileCache) =
        object : SpeechSynthesizer {
            override val maxInputLength = 4000

            override val enginePackageName = "com.example.tts"

            override suspend fun synthesizeToFile(text: String): File =
                withContext(NonCancellable) {
                    delay(1.seconds)
                    cache.create("late").apply { writeBytes(ByteArray(64)) }
                }

            override suspend fun setVoice(voice: Voice) = Unit

            override fun close() = Unit

            override suspend fun loadAvailableVoices(langTag: String): List<Voice> =
                listOf(Voice(id = "voice-1", locale = Locale.US))
        }

    // The engine is an IPC binding that lives until it is shut down, and close() is terminal, so the next session has
    // to get an engine of its own.
    @Test
    fun `test that stopping the session closes the engine and the next session builds another`() = runTest {
        val engines = mutableListOf<FakeSpeechSynthesizer>()
        val store =
            storeWith(synthesizerProvider = { FakeSpeechSynthesizer().also(engines::add) }) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        assertEquals(1, engines.size)
        assertFalse(engines[0].closed)

        store.dispatch(ListenAction.Session.StopRequested())
        advanceUntilIdle()

        assertTrue(engines[0].closed)

        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        assertEquals(2, engines.size)
        assertFalse(engines[1].closed)
    }

    @Test
    fun `test that the engine is built once for a session`() = runTest {
        var built = 0
        val store =
            storeWith(synthesizerProvider = { FakeSpeechSynthesizer().also { built++ } }) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()
        store.dispatch(ListenAction.Content.ContentReady(languageTag = "en-US"))
        advanceUntilIdle()

        assertEquals(1, built)
    }

    @Test
    fun `test that stopping the session empties the audio cache`() = runTest {
        val audioCache = FakeAudioFileCache()
        val store =
            storeWith(audioCache = audioCache) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        assertFalse(audioCache.cleared)

        store.dispatch(ListenAction.Session.StopRequested())
        advanceUntilIdle()

        assertTrue(audioCache.cleared)
    }

    // The article carries the tab it came from, so a session cannot read the article the session before it left
    // behind, however the two raced.
    @Test
    fun `test that an article belonging to an earlier session is not read out`() = runTest {
        val synthesizer = FakeSpeechSynthesizer()
        val secondExtraction = CompletableDeferred<Result<Content>>()
        val store =
            storeWith(synthesizerProvider = { synthesizer }) { tabId ->
                if (tabId == TAB_ID) {
                    Result.success(Content(text = "First article.", languageTag = "en-US"))
                } else {
                    secondExtraction.await()
                }
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        assertEquals(listOf("First article."), synthesizer.requests)

        // The second session never gets its own article, so the first one's is all that is left in the field.
        store.listenAndPlay(OTHER_TAB_ID, URL)
        advanceUntilIdle()
        store.dispatch(ListenAction.Content.ContentReady(languageTag = "en-US"))
        advanceUntilIdle()

        assertEquals(listOf("First article."), synthesizer.requests)

        // Let the second extraction finish, so the middleware has nothing left running when the body returns.
        secondExtraction.complete(Result.failure(RuntimeException("Extraction abandoned")))
        advanceUntilIdle()
    }

    @Test
    fun `test that the article is cleared when the session stops`() = runTest {
        val synthesizer = FakeSpeechSynthesizer()
        val store =
            storeWith(synthesizerProvider = { synthesizer }) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()
        store.dispatch(ListenAction.Session.StopRequested())
        advanceUntilIdle()

        // The article is gone, so nothing is left to synthesize a second time.
        store.dispatch(ListenAction.Content.ContentReady(languageTag = "en-US"))
        advanceUntilIdle()

        assertEquals(listOf("Article text."), synthesizer.requests)
    }

    @Test
    fun `test that the voice saved for the article language is the one selected`() = runTest {
        val voices =
            listOf(Voice(id = "en-us-female", locale = Locale.US), Voice(id = "en-us-male", locale = Locale.US))
        val settings = ListenSettings.inMemory(ENGINE, voiceIds = mapOf("en" to "en-us-male"))
        settings.setSelectedVoiceId("en", "en-us-male")
        val store =
            storeWith(
                synthesizerProvider = { FakeSpeechSynthesizer(voices = voices, enginePackageName = ENGINE) },
                settings = settings,
            ) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        assertEquals(Voice(id = "en-us-male", locale = Locale.US), store.state.voiceState.selectedVoice)
    }

    @Test
    fun `test that a saved voice the engine no longer has falls back to the best one it does`() = runTest {
        val voices =
            listOf(Voice(id = "en-us-female", locale = Locale.US), Voice(id = "en-us-male", locale = Locale.US))
        val settings = ListenSettings.inMemory(ENGINE, voiceIds = mapOf("en" to "en-us-uninstalled"))
        settings.setSelectedVoiceId("en", "en-us-uninstalled")
        val store =
            storeWith(
                synthesizerProvider = { FakeSpeechSynthesizer(voices = voices, enginePackageName = ENGINE) },
                settings = settings,
            ) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        assertEquals(Voice(id = "en-us-female", locale = Locale.US), store.state.voiceState.selectedVoice)
        assertNull(store.state.error)
    }

    @Test
    fun `test that picking a voice saves it for the article language`() = runTest {
        val settings = ListenSettings.inMemory()
        val store =
            storeWith(settings = settings) {
                Result.success(Content(text = "Texte de l'article.", languageTag = "fr-FR"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()
        store.dispatch(ListenAction.Voices.VoiceSelected(Voice(id = "fr-fr-male", locale = Locale.FRANCE)))
        advanceUntilIdle()

        assertEquals("fr-fr-male", settings.getSelectedVoiceId("fr"))
    }

    // The same regions are offered whatever region the article is written in, so the choice made among them has to
    // carry across articles too: saving it per region would forget it between one English page and the next.
    @Test
    fun `test that a voice picked on an article of one region is selected on an article of another`() = runTest {
        val voices = listOf(Voice("en-us-female", Locale.US), Voice("en-gb-male", Locale.UK))
        val store =
            storeWith(synthesizerProvider = { FakeSpeechSynthesizer(voices = voices) }) { tabId ->
                if (tabId == TAB_ID) {
                    Result.success(Content(text = "Article text.", languageTag = "en-US"))
                } else {
                    Result.success(Content(text = "Article text.", languageTag = "en-GB"))
                }
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()
        store.dispatch(ListenAction.Voices.VoiceSelected(Voice("en-gb-male", Locale.UK)))
        advanceUntilIdle()

        store.listenAndPlay(OTHER_TAB_ID, URL)
        advanceUntilIdle()

        assertEquals(Voice("en-gb-male", Locale.UK), store.state.voiceState.selectedVoice)
    }

    @Test
    fun `test that the selected voice is given to the engine before the article is synthesized`() = runTest {
        val voices =
            listOf(Voice(id = "en-us-female", locale = Locale.US), Voice(id = "en-us-male", locale = Locale.US))
        val synthesizer = FakeSpeechSynthesizer(voices = voices)
        val store =
            storeWith(synthesizerProvider = { synthesizer }) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        assertEquals(listOf(Voice(id = "en-us-female", locale = Locale.US)), synthesizer.voicesSet)

        store.dispatch(ListenAction.Voices.VoiceSelected(Voice(id = "en-us-male", locale = Locale.US)))
        advanceUntilIdle()

        assertEquals(
            listOf(Voice(id = "en-us-female", locale = Locale.US), Voice(id = "en-us-male", locale = Locale.US)),
            synthesizer.voicesSet,
        )
    }

    // The audio already made is in the voice before this one, so playing it to its end would keep reading the article
    // in the voice the user has just turned down.
    @Test
    fun `test that picking a voice throws the audio of the previous one away and makes it again`() = runTest {
        val audioCache = FakeAudioFileCache()
        val synthesizer =
            FakeSpeechSynthesizer(voices = listOf(Voice("en-us-female", Locale.US), Voice("en-us-male", Locale.US)))
        val playback = FakePlaybackController()
        val store =
            storeWith(
                synthesizerProvider = { synthesizer },
                playbackController = playback,
                audioCache = audioCache,
            ) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        assertEquals(emptyList<File>(), audioCache.deleted)

        store.dispatch(ListenAction.Voices.VoiceSelected(Voice(id = "en-us-male", locale = Locale.US)))
        advanceUntilIdle()

        // Deleted chunk by chunk rather than by emptying the cache, because the playback carries on into the new voice
        // and still holds the file it is reading.
        assertEquals(listOf(File("/audio/1.wav")), audioCache.deleted)
        assertEquals(listOf("Article text.", "Article text."), synthesizer.requests)
        assertEquals(listOf(File("/audio/1.wav"), File("/audio/2.wav")), playback.played)
    }

    // Re-tapping the voice the article is already being read in is reachable from the popup, which checks the selected
    // voice rather than disabling it.
    @Test
    fun `test that picking the voice already reading leaves the audio alone`() = runTest {
        val audioCache = FakeAudioFileCache()
        val voices = listOf(Voice("en-us-female", Locale.US), Voice("en-gb-male", Locale.UK))
        val synthesizer = FakeSpeechSynthesizer(voices = voices)
        val playback = FakePlaybackController()
        val store =
            storeWith(
                synthesizerProvider = { synthesizer },
                playbackController = playback,
                audioCache = audioCache,
            ) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        store.dispatch(ListenAction.Voices.VoiceSelected(Voice("en-us-female", Locale.US)))
        advanceUntilIdle()

        assertEquals(emptyList<File>(), audioCache.deleted)
        assertEquals(listOf("Article text."), synthesizer.requests)
        assertEquals(listOf(File("/audio/1.wav")), playback.played)
    }

    // The tap is still what turns the voice a load picked for itself into a choice the user has made, so it is saved
    // even though nothing is synthesized again.
    @Test
    fun `test that picking the voice already reading still saves it`() = runTest {
        val settings = ListenSettings.inMemory()
        val voices = listOf(Voice("en-us-female", Locale.US), Voice("en-gb-male", Locale.UK))
        val store =
            storeWith(synthesizerProvider = { FakeSpeechSynthesizer(voices = voices) }, settings = settings) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        assertNull(settings.getSelectedVoiceId("en"))

        store.dispatch(ListenAction.Voices.VoiceSelected(Voice("en-us-female", Locale.US)))
        advanceUntilIdle()

        assertEquals("en-us-female", settings.getSelectedVoiceId("en"))
    }

    @Test
    fun `test that picking a voice stops the voice before at once and carries on from the same place in the text`() =
        runTest {
            val playback = FakePlaybackController()
            // Every chunk after the opening reads slower than the opening, so it measures longer than estimated.
            val synthesizer =
                FakeSpeechSynthesizer(
                    voices = listOf(Voice("a", Locale.US), Voice("b", Locale.UK)),
                    audioDirectory = temporaryFolder.root,
                    audioDurationOf = { text ->
                        (text.length * if (text.startsWith("Sentence 1 ")) 50 else 75).milliseconds
                    },
                )
            val store = storeReadingHalfway(playback, synthesizer)
            val before = store.state.articleProgress
            val textShareBefore =
                store.state.articleMeasurements!!.getTextPositionAfterVoiceChanged(
                    before.positionMs,
                    before.chunkDurationsMs,
                )!!

            store.dispatch(ListenAction.Voices.VoiceSelected(Voice(id = "b", locale = Locale.UK)))
            advanceUntilIdle()

            val after = store.state.articleProgress
            val textShareAfter =
                store.state.articleMeasurements!!.getTextPositionAfterVoiceChanged(
                    after.positionMs,
                    after.chunkDurationsMs,
                )!!
            assertTrue("the voice before read on", before.positionMs in playback.stopAndBufferPositionsMs)
            assertEquals(textShareBefore, textShareAfter, 0.0001f)
            assertEquals(
                "playback restarted away from where it is shown",
                after.positionMs,
                playback.restartedPositionsMs.last(),
            )
            assertEquals("the article was started again from its top", 1, playback.articlesStarted)
        }

    @Test
    fun `test that picking a voice while the article is paused leaves it paused`() = runTest {
        val playback = FakePlaybackController()
        val store = storeReadingHalfway(playback)
        playback.status.value = playback.status.value.copy(phase = PlaybackPhase.Paused)
        advanceUntilIdle()
        val restartsBefore = playback.restartedPositionsMs.size

        store.dispatch(ListenAction.Voices.VoiceSelected(Voice(id = "b", locale = Locale.UK)))
        advanceUntilIdle()

        assertEquals(restartsBefore + 1, playback.restartedPositionsMs.size)
        assertEquals(0, playback.resumed)
        assertEquals(PlaybackPhase.Paused, store.state.playbackState.phase)
    }

    @Test
    fun `test that the times shown for a paused article change only once when the voice is picked`() = runTest {
        val playback = FakePlaybackController()
        val synthesizer =
            FakeSpeechSynthesizer(
                voices = listOf(Voice("a", Locale.US), Voice("b", Locale.UK)),
                audioDirectory = temporaryFolder.root,
                timePerRequest = 1.seconds,
            )
        val store = storeReadingHalfway(playback, synthesizer)
        playback.status.value = playback.status.value.copy(phase = PlaybackPhase.Paused)
        advanceUntilIdle()
        val restartsBefore = playback.restartedPositionsMs.size

        store.dispatch(ListenAction.Voices.VoiceSelected(Voice(id = "b", locale = Locale.UK)))
        while (playback.restartedPositionsMs.size == restartsBefore) advanceTimeBy(100.milliseconds)
        runCurrent()
        val shown = store.state.articleProgress

        // The paused player reports where the new playlist starts, before the chunks around it are made.
        playback.status.value =
            PlaybackState(
                phase = PlaybackPhase.Paused,
                chunk = ChunkState(index = playback.restartedChunks.last()),
                positionMs = playback.restartedPositionsMs.last(),
            )
        runCurrent()

        assertEquals("the elapsed time changed again", shown.positionMs, store.state.articleProgress.positionMs)
        assertEquals("the estimate changed again", shown.durationMs, store.state.articleProgress.durationMs)
    }

    @Test
    fun `test that the article moves on with the audio after switching to another voice and back`() = runTest {
        val playback = FakePlaybackController()
        val store = storeReadingHalfway(playback)

        store.dispatch(ListenAction.Voices.VoiceSelected(Voice(id = "b", locale = Locale.UK)))
        advanceUntilIdle()
        store.dispatch(ListenAction.Voices.VoiceSelected(Voice(id = "a", locale = Locale.US)))
        advanceUntilIdle()
        val restartedMs = playback.restartedPositionsMs.last()
        playback.status.value =
            PlaybackState(
                phase = PlaybackPhase.Playing,
                chunk = ChunkState(index = playback.restartedChunks.last()),
                positionMs = restartedMs,
            )
        advanceUntilIdle()
        val restartedAtMs = store.state.articleProgress.positionMs

        playback.status.value = playback.status.value.copy(positionMs = restartedMs + 1_000)
        advanceUntilIdle()

        assertTrue("the progress stopped", store.state.articleProgress.positionMs > restartedAtMs)
    }

    /** A store reading a long article with two voices to pick from, halfway through it in the first. */
    private fun TestScope.storeReadingHalfway(
        playback: FakePlaybackController,
        synthesizer: FakeSpeechSynthesizer =
            FakeSpeechSynthesizer(
                voices = listOf(Voice("a", Locale.US), Voice("b", Locale.UK)),
                audioDirectory = temporaryFolder.root,
            ),
    ): ListenStore {
        val store =
            storeWith(
                synthesizerProvider = { synthesizer },
                playbackController = playback,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()
        playback.status.value = PlaybackState(phase = PlaybackPhase.Playing, positionMs = 2_000)
        advanceUntilIdle()

        store.dispatch(ListenAction.Playback.SeekRequested(0.5f))
        advanceUntilIdle()
        playback.status.value =
            PlaybackState(
                phase = PlaybackPhase.Playing,
                chunk = ChunkState(index = playback.restartedChunks.last()),
                positionMs = playback.restartedPositionsMs.last(),
            )
        advanceUntilIdle()

        return store
    }

    // Binding an engine with no session would leave it bound for the life of the process, because only a session
    // closes one.
    @Test
    fun `test that picking a voice with no session running builds no engine`() = runTest {
        var built = 0
        val store =
            storeWith(synthesizerProvider = { FakeSpeechSynthesizer().also { built++ } }) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.dispatch(ListenAction.Voices.VoiceSelected(Voice(id = "en-us-male", locale = Locale.US)))
        advanceUntilIdle()

        assertEquals(0, built)
    }

    @Test
    fun `test that switching the speech engine drops the voice saved for the article language`() = runTest {
        val voices =
            listOf(Voice(id = "en-us-female", locale = Locale.US), Voice(id = "en-us-male", locale = Locale.US))
        val settings = ListenSettings.inMemory()
        settings.clearSavedVoicesOnEngineChange("com.example.tts")
        settings.setSelectedVoiceId("en", "en-us-male")
        val store =
            storeWith(
                synthesizerProvider = {
                    FakeSpeechSynthesizer(voices = voices, enginePackageName = "com.example.other.tts")
                },
                settings = settings,
            ) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()
        assertEquals(Voice(id = "en-us-female", locale = Locale.US), store.state.voiceState.selectedVoice)
        assertNull(settings.getSelectedVoiceId("en"))
    }

    @Test
    fun `test that a session on an article in the same language does not ask the engine again`() = runTest {
        val synthesizer = FakeSpeechSynthesizer()
        val store =
            storeWith(synthesizerProvider = { synthesizer }) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()
        store.listenAndPlay(OTHER_TAB_ID, URL)
        advanceUntilIdle()

        assertEquals(listOf("en-US"), synthesizer.voiceRequests)
    }

    @Test
    fun `test that a session on an article in another language asks the engine again`() = runTest {
        val synthesizer = FakeSpeechSynthesizer()
        val store =
            storeWith(synthesizerProvider = { synthesizer }) { tabId ->
                if (tabId == TAB_ID) {
                    Result.success(Content(text = "Article text.", languageTag = "en-US"))
                } else {
                    Result.success(Content(text = "Texte de l'article.", languageTag = "fr-FR"))
                }
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()
        store.listenAndPlay(OTHER_TAB_ID, URL)
        advanceUntilIdle()

        assertEquals(listOf("en-US", "fr-FR"), synthesizer.voiceRequests)
    }

    @Test
    fun `test that what the player reports is recorded in the state`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(playbackController = playback) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        val reported =
            PlaybackState(
                phase = PlaybackPhase.Playing,
                chunk = ChunkState(index = 1, durationMs = 30_000),
                positionMs = 4_000,
            )
        playback.status.value = reported
        advanceUntilIdle()

        assertEquals(reported, store.state.playbackState)
    }

    // The notification's own controls and anything taking audio focus move the player without passing through the
    // store, so a state that followed the commands it sent would be wrong about whether audio is playing.
    @Test
    fun `test that a pause the store never asked for is still recorded`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(playbackController = playback) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        playback.status.value = PlaybackState(phase = PlaybackPhase.Playing)
        advanceUntilIdle()
        playback.status.value = PlaybackState(phase = PlaybackPhase.Paused, positionMs = 9_000)
        advanceUntilIdle()

        assertEquals(PlaybackPhase.Paused, store.state.playbackState.phase)
        assertEquals(9_000, store.state.playbackState.positionMs)
    }

    @Test
    fun `test that a failing player is reported as an error`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(playbackController = playback) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        playback.status.value = PlaybackState(phase = PlaybackPhase.Failed)
        advanceUntilIdle()

        assertEquals(ListenError.PlaybackFailed, store.state.error)
    }

    @Test
    fun `test that the player is no longer watched once the session stops`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(playbackController = playback) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()
        store.dispatch(ListenAction.Session.StopRequested())
        advanceUntilIdle()

        playback.status.value = PlaybackState(phase = PlaybackPhase.Playing, positionMs = 4_000)
        advanceUntilIdle()

        assertEquals(PlaybackState(), store.state.playbackState)
    }

    // The player is watched from the request, not from the first thing played, because the session can fail before
    // anything is given to the player and the sheet still has to know it is not playing.
    @Test
    fun `test that the player is watched again by the session after a stop`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(playbackController = playback) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()
        store.dispatch(ListenAction.Session.StopRequested())
        advanceUntilIdle()
        store.listenAndPlay(OTHER_TAB_ID, URL)
        advanceUntilIdle()

        playback.status.value = PlaybackState(phase = PlaybackPhase.Playing, positionMs = 4_000)
        advanceUntilIdle()

        assertEquals(PlaybackPhase.Playing, store.state.playbackState.phase)
    }

    @Test
    fun `test that closing the tab being listened to stops the session`() = runTest {
        val browserStore = browserStoreWithOpenTabs()
        val actions = mutableListOf<ListenAction>()
        val store =
            storeWith(browserStore = browserStore, recordInto = actions) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        browserStore.dispatch(TabListAction.RemoveTabAction(TAB_ID))
        advanceUntilIdle()

        assertEquals(
            listOf(
                ListenAction.Session.ListenRequested(TAB_ID, URL),
                ListenAction.Session.StopRequested(ListenAction.Session.StopSource.TabClosed),
            ),
            actions.filterIsInstance<ListenAction.Session>(),
        )
    }

    @Test
    fun `test that the playback ending outside the app stops the session and releases the playback`() = runTest {
        val playback = FakePlaybackController()
        val actions = mutableListOf<ListenAction>()
        val store =
            storeWith(playbackController = playback, recordInto = actions) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        playback.sessionEnded.emit(Unit)
        advanceUntilIdle()

        assertEquals(
            listOf(
                ListenAction.Session.ListenRequested(TAB_ID, URL),
                ListenAction.Session.StopRequested(ListenAction.Session.StopSource.MediaSessionEndedOutsideApp),
            ),
            actions.filterIsInstance<ListenAction.Session>(),
        )
        assertNull(store.state.tabId)
        assertTrue(playback.released)
    }

    @Test
    fun `test that turning the feature off stops the session and releases the playback`() = runTest {
        val playback = FakePlaybackController()
        val settings = ListenSettings.inMemory(enabled = ListenEnabledState.Enabled)
        val actions = mutableListOf<ListenAction>()
        val store =
            storeWith(playbackController = playback, settings = settings, recordInto = actions) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        settings.setEnabled(false)
        advanceUntilIdle()

        assertEquals(
            listOf(
                ListenAction.Session.ListenRequested(TAB_ID, URL),
                ListenAction.Session.StopRequested(ListenAction.Session.StopSource.FeatureDisabled),
            ),
            actions.filterIsInstance<ListenAction.Session>(),
        )
        assertNull(store.state.tabId)
        assertTrue(playback.released)
    }

    @Test
    fun `test that turning the feature off after the session was stopped does not stop it again`() = runTest {
        val settings = ListenSettings.inMemory(enabled = ListenEnabledState.Enabled)
        val actions = mutableListOf<ListenAction>()
        val store =
            storeWith(settings = settings, recordInto = actions) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()
        store.dispatch(ListenAction.Session.StopRequested())
        advanceUntilIdle()

        settings.setEnabled(false)
        advanceUntilIdle()

        assertEquals(
            listOf(ListenAction.Session.ListenRequested(TAB_ID, URL), ListenAction.Session.StopRequested()),
            actions.filterIsInstance<ListenAction.Session>(),
        )
    }

    @Test
    fun `test that the playback ending after the session was stopped does not stop it again`() = runTest {
        val playback = FakePlaybackController()
        val actions = mutableListOf<ListenAction>()
        val store =
            storeWith(playbackController = playback, recordInto = actions) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()
        store.dispatch(ListenAction.Session.StopRequested())
        advanceUntilIdle()

        playback.sessionEnded.emit(Unit)
        advanceUntilIdle()

        assertEquals(
            listOf(
                ListenAction.Session.ListenRequested(TAB_ID, URL),
                ListenAction.Session.StopRequested(),
            ),
            actions.filterIsInstance<ListenAction.Session>(),
        )
    }

    @Test
    fun `test that closing another tab does not stop the session`() = runTest {
        val browserStore = browserStoreWithOpenTabs()
        val actions = mutableListOf<ListenAction>()
        val store =
            storeWith(browserStore = browserStore, recordInto = actions) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        browserStore.dispatch(TabListAction.RemoveTabAction(OTHER_TAB_ID))
        advanceUntilIdle()

        assertEquals(
            listOf(ListenAction.Session.ListenRequested(TAB_ID, URL)),
            actions.filterIsInstance<ListenAction.Session>(),
        )
    }

    @Test
    fun `test that leaving reader mode stops the session`() = runTest {
        val browserStore = browserStoreWithOpenTabs()
        val actions = mutableListOf<ListenAction>()
        val store =
            storeWith(browserStore = browserStore, recordInto = actions) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        browserStore.dispatch(ReaderAction.UpdateReaderActiveAction(TAB_ID, false))
        advanceUntilIdle()

        assertEquals(
            listOf(
                ListenAction.Session.ListenRequested(TAB_ID, URL),
                ListenAction.Session.StopRequested(ListenAction.Session.StopSource.ReaderModeClosed),
            ),
            actions.filterIsInstance<ListenAction.Session>(),
        )
    }

    @Test
    fun `test that leaving reader mode on another tab does not stop the session`() = runTest {
        val browserStore = browserStoreWithOpenTabs()
        val actions = mutableListOf<ListenAction>()
        val store =
            storeWith(browserStore = browserStore, recordInto = actions) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        browserStore.dispatch(ReaderAction.UpdateReaderActiveAction(OTHER_TAB_ID, false))
        advanceUntilIdle()

        assertEquals(
            listOf(ListenAction.Session.ListenRequested(TAB_ID, URL)),
            actions.filterIsInstance<ListenAction.Session>(),
        )
    }

    @Test
    fun `test that navigating the tab being listened to stops the session`() = runTest {
        val browserStore = browserStoreWithOpenTabs()
        val actions = mutableListOf<ListenAction>()
        val store =
            storeWith(browserStore = browserStore, recordInto = actions) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        browserStore.dispatch(ContentAction.UpdateUrlAction(TAB_ID, "https://example.org/other-article"))
        advanceUntilIdle()

        assertEquals(
            listOf(
                ListenAction.Session.ListenRequested(TAB_ID, URL),
                ListenAction.Session.StopRequested(ListenAction.Session.StopSource.TabClosed),
            ),
            actions.filterIsInstance<ListenAction.Session>(),
        )
    }

    @Test
    fun `test that returning from reader view to the article does not stop the session`() = runTest {
        val browserStore =
            BrowserStore(
                BrowserState(
                    tabs =
                        listOf(
                            createTab(
                                url = READER_URL,
                                id = TAB_ID,
                                readerState = ReaderState(active = true, activeUrl = URL),
                            )
                        ),
                    selectedTabId = TAB_ID,
                )
            )
        val actions = mutableListOf<ListenAction>()
        val store =
            storeWith(browserStore = browserStore, recordInto = actions) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, READER_URL)
        advanceUntilIdle()

        browserStore.dispatch(ReaderAction.ClearReaderActiveUrlAction(TAB_ID))
        browserStore.dispatch(ContentAction.UpdateUrlAction(TAB_ID, URL))
        advanceUntilIdle()

        assertEquals(
            listOf(ListenAction.Session.ListenRequested(TAB_ID, READER_URL)),
            actions.filterIsInstance<ListenAction.Session>(),
        )
    }

    @Test
    fun `test that navigating another tab does not stop the session`() = runTest {
        val browserStore = browserStoreWithOpenTabs()
        val actions = mutableListOf<ListenAction>()
        val store =
            storeWith(browserStore = browserStore, recordInto = actions) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        browserStore.dispatch(ContentAction.UpdateUrlAction(OTHER_TAB_ID, "https://example.org/other-article"))
        advanceUntilIdle()

        assertEquals(
            listOf(ListenAction.Session.ListenRequested(TAB_ID, URL)),
            actions.filterIsInstance<ListenAction.Session>(),
        )
    }

    @Test
    fun `test that closing the tab an earlier session listened to does not stop the current session`() = runTest {
        val browserStore = browserStoreWithOpenTabs()
        val actions = mutableListOf<ListenAction>()
        val store =
            storeWith(browserStore = browserStore, recordInto = actions) {
                Result.success(Content(text = "Article text.", languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()
        store.listenAndPlay(OTHER_TAB_ID, URL)
        advanceUntilIdle()

        browserStore.dispatch(TabListAction.RemoveTabAction(TAB_ID))
        advanceUntilIdle()

        assertEquals(
            listOf(
                ListenAction.Session.ListenRequested(TAB_ID, URL),
                ListenAction.Session.ListenRequested(OTHER_TAB_ID, URL),
            ),
            actions.filterIsInstance<ListenAction.Session>(),
        )
    }

    /** An engine that offers a voice, so that only [synthesizeToFile] can fail a test that uses it. */
    private fun failingSynthesizer(failure: () -> Nothing) =
        object : SpeechSynthesizer {
            override val maxInputLength = 4000

            override val enginePackageName = "com.example.tts"

            override suspend fun synthesizeToFile(text: String): File = failure()

            override suspend fun setVoice(voice: Voice) = Unit

            override fun close() = Unit

            override suspend fun loadAvailableVoices(langTag: String): List<Voice> =
                listOf(Voice(id = "voice-1", locale = Locale.US))
        }

    @Test
    fun `test that seeking into a chunk already queued moves the player without making anything`() = runTest {
        val synthesizer = FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root)
        val playback = FakePlaybackController()
        val store =
            storeWith(synthesizerProvider = { synthesizer }, playbackController = playback) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        val madeBefore = synthesizer.requests.size
        val lengths = store.state.articleProgress.chunkDurationsMs

        // A second into the chunk the lookahead queued behind the opening.
        store.seekTo(lengths[0] + 1_000)
        advanceUntilIdle()

        assertEquals(listOf(1 to lengths[0] + 1_000), playback.seekedChunkPositions)
        assertTrue("the playlist was rebuilt for audio it already held", playback.restartedPositionsMs.isEmpty())
        assertEquals("a chunk was made again", madeBefore, synthesizer.requests.size)
    }

    @Test
    fun `test that what the player reports before a seek reaches it does not put the article back`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(
                synthesizerProvider = { FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root) },
                playbackController = playback,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()
        val target = store.state.articleProgress.chunkDurationsMs[0] + 1_000

        store.seekTo(target)
        // Sampled by the controller before the seek reached the player.
        playback.status.value = PlaybackState(phase = PlaybackPhase.Playing, positionMs = 500)
        runCurrent()

        assertEquals(target, store.state.articleProgress.positionMs)
    }

    @Test
    fun `test that what the player reports before a seek past what is queued reaches it does not put the article back`() =
        runTest {
            val playback = FakePlaybackController()
            val shown = mutableListOf<Long>()
            val store =
                storeWith(
                    synthesizerProvider = {
                        FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root, timePerRequest = 1.seconds)
                    },
                    playbackController = playback,
                    recordPositionsInto = shown,
                ) {
                    Result.success(Content(text = longArticle(), languageTag = "en-US"))
                }
            store.listenAndPlay(TAB_ID, URL)
            advanceUntilIdle()
            playback.status.value = PlaybackState(phase = PlaybackPhase.Playing)
            advanceUntilIdle()
            val target = store.state.articleProgress.chunkDurationsMs.take(SEEK_TARGET_CHUNK).sum() + 1_000

            shown.clear()

            store.seekTo(target)
            // Sampled by the controller before the buffering reached the session, as the chunk sought is being made.
            playback.status.value = PlaybackState(phase = PlaybackPhase.Playing, positionMs = 1_500)
            runCurrent()

            assertTrue("the article was put back to where it was: $shown", shown.all { it >= target })
        }

    @Test
    fun `test that seeking past what is queued makes that chunk and starts the playlist on it`() = runTest {
        val actions = mutableListOf<ListenAction>()
        val playback = FakePlaybackController()
        val store =
            storeWith(
                synthesizerProvider = { FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root) },
                playbackController = playback,
                recordInto = actions,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        // Well past the window the lookahead filled, so nothing there has been made yet.
        val lengths = store.state.articleProgress.chunkDurationsMs
        val chunkStartMs = lengths.take(SEEK_TARGET_CHUNK).sum()
        store.seekTo(chunkStartMs + 1_000)
        advanceUntilIdle()

        assertEquals(1, playback.restartedPositionsMs.size)
        assertEquals(chunkStartMs + 1_000, playback.restartedPositionsMs.single())
        assertEquals(listOf(chunkStartMs), playback.restartChunkStartsMs)
        assertTrue(
            "the player was moved inside a playlist that does not hold the chunk",
            playback.seekedChunkPositions.isEmpty(),
        )
        assertTrue(actions.contains(ListenAction.Playback.PlaybackWaiting))
        assertNull(store.state.error)
    }

    @Test
    fun `test that the player's first item reads as the chunk the playlist was restarted on`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(
                synthesizerProvider = { FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root) },
                playbackController = playback,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        val target = store.state.articleProgress.chunkDurationsMs.take(SEEK_TARGET_CHUNK).sum() + 1_000
        store.seekTo(target)
        advanceUntilIdle()

        playback.reportPlayingFromRestart()
        advanceUntilIdle()

        assertEquals(SEEK_TARGET_CHUNK, store.state.playbackState.chunk.index)
    }

    @Test
    fun `test that a second seek supersedes one still waiting on its audio to be synthesized`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(
                synthesizerProvider = {
                    FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root, timePerRequest = 1.seconds)
                },
                playbackController = playback,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        val abandoned = store.state.articleProgress.chunkDurationsMs.take(SEEK_TARGET_CHUNK).sum() + 1_000
        store.seekTo(abandoned)
        advanceTimeBy(100.milliseconds)

        // Read after the first seek, whose buffering position is reported with the lengths as they are now.
        val wanted = store.state.articleProgress.chunkDurationsMs.take(SEEK_TARGET_CHUNK + 2).sum() + 1_000
        store.seekTo(wanted)
        advanceUntilIdle()

        assertEquals("the abandoned seek took the playback over as well", 1, playback.restartedPositionsMs.size)

        // Which chunk the playlist now starts on, read through a report from it.
        playback.reportPlayingFromRestart()
        advanceUntilIdle()

        assertEquals(SEEK_TARGET_CHUNK + 2, store.state.playbackState.chunk.index)
    }

    @Test
    fun `test that seeking to a chunk that has to be made shows the target and buffers there until it is made`() =
        runTest {
            val playback = FakePlaybackController()
            val store =
                storeWith(
                    synthesizerProvider = {
                        FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root, timePerRequest = 1.seconds)
                    },
                    playbackController = playback,
                ) {
                    Result.success(Content(text = longArticle(), languageTag = "en-US"))
                }
            store.listenAndPlay(TAB_ID, URL)
            advanceUntilIdle()
            playback.status.value = PlaybackState(phase = PlaybackPhase.Playing, positionMs = 1_000)
            advanceUntilIdle()

            val targetMs = store.state.articleProgress.chunkDurationsMs.take(SEEK_TARGET_CHUNK).sum() + 1_000
            store.seekTo(targetMs)
            advanceTimeBy(10.milliseconds)

            assertEquals(listOf(targetMs), playback.stopAndBufferPositionsMs)
            assertEquals(targetMs, store.state.articleProgress.positionMs)
            assertEquals(PlaybackPhase.Buffering, store.state.playbackState.phase)
            assertTrue("the playlist restarted before its chunk was made", playback.restartedPositionsMs.isEmpty())

            advanceUntilIdle()
            assertEquals(1, playback.restartedPositionsMs.size)
        }

    @Test
    fun `test that a seek that leaves little of its chunk makes the next chunk before playing`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(
                synthesizerProvider = {
                    FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root, timePerRequest = 1.seconds)
                },
                playbackController = playback,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()
        playback.status.value = PlaybackState(phase = PlaybackPhase.Playing)
        advanceUntilIdle()
        val lengths = store.state.articleProgress.chunkDurationsMs

        val queuedBefore = playback.queued.size

        // A second short of the end of the five seconds every chunk the fake makes lasts.
        store.seekTo(lengths.take(SEEK_TARGET_CHUNK).sum() + FAKE_AUDIO_MS - 1_000)
        advanceTimeBy(1_500.milliseconds)
        assertTrue("played before the next chunk was made", playback.restartedPositionsMs.isEmpty())

        advanceTimeBy(1_000.milliseconds)
        assertEquals(1, playback.restartedPositionsMs.size)
        assertEquals("the next chunk was not queued at once", queuedBefore + 1, playback.queued.size)
    }

    @Test
    fun `test that each chunk is queued as soon as it is made`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(
                synthesizerProvider = {
                    FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root, timePerRequest = 1.seconds)
                },
                playbackController = playback,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()
        playback.status.value = PlaybackState(phase = PlaybackPhase.Playing)
        advanceUntilIdle()
        val lengths = store.state.articleProgress.chunkDurationsMs

        store.seekTo(lengths.take(SEEK_TARGET_CHUNK).sum() + 1_000)
        advanceTimeBy(1_500.milliseconds)
        assertEquals(1, playback.restartedPositionsMs.size)
        val queuedBefore = playback.queued.size

        advanceTimeBy(1_000.milliseconds)

        assertEquals(
            "the chunk after the target waited for the rest of the window",
            queuedBefore + 1,
            playback.queued.size,
        )
    }

    @Test
    fun `test that the article moves on with the audio after a seek past what is queued`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(
                synthesizerProvider = {
                    FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root, timePerRequest = 1.seconds)
                },
                playbackController = playback,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        val targetMs = store.state.articleProgress.chunkDurationsMs.take(SEEK_TARGET_CHUNK).sum() + 1_000
        store.seekTo(targetMs)
        advanceTimeBy(1_500.milliseconds)
        assertEquals("the target chunk was not made in time", 1, playback.restartedPositionsMs.size)

        // The new playlist starts at the target chunk. The chunks around it are still being made and measured while it
        // plays.
        val startMs = playback.restartedPositionsMs.single()
        playback.status.value = PlaybackState(phase = PlaybackPhase.Playing, positionMs = startMs + 1_000)
        runCurrent()
        val before = store.state.articleProgress.positionMs
        advanceTimeBy(2_000.milliseconds)
        playback.status.value = PlaybackState(phase = PlaybackPhase.Playing, positionMs = startMs + 3_000)
        runCurrent()

        assertEquals(
            "the article did not move on with the audio",
            before + 2_000,
            store.state.articleProgress.positionMs,
        )
    }

    @Test
    fun `test that a seek into a chunk the player turns out not to hold restarts the playlist there`() = runTest {
        val playback = FakePlaybackController(shouldReportSeek = true)
        val store =
            storeWith(
                synthesizerProvider = { FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root) },
                playbackController = playback,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()
        val lengths = store.state.articleProgress.chunkDurationsMs
        playback.status.value =
            PlaybackState(phase = PlaybackPhase.Playing, chunk = ChunkState(index = 1), positionMs = lengths[0] + 2_000)
        advanceUntilIdle()
        playback.hasSeekedChunk = false

        store.seekTo(1_000)
        advanceUntilIdle()

        assertEquals("the seek was not tried first", listOf(0 to 1_000L), playback.seekedChunkPositions)
        assertEquals(listOf(0), playback.restartedChunks)
    }

    @Test
    fun `test that the article goes back with a seek back inside what is queued and moves on from there`() = runTest {
        val playback = FakePlaybackController(shouldReportSeek = true)
        val store =
            storeWith(
                synthesizerProvider = { FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root) },
                playbackController = playback,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()
        val lengths = store.state.articleProgress.chunkDurationsMs
        playback.status.value =
            PlaybackState(phase = PlaybackPhase.Playing, chunk = ChunkState(index = 1), positionMs = lengths[0] + 2_000)
        advanceUntilIdle()

        // Back into the opening, which is still queued.
        store.seekTo(1_000)
        advanceUntilIdle()

        assertEquals(listOf(0 to 1_000L), playback.seekedChunkPositions)
        assertEquals("the article stayed where it was", 1_000L, store.state.articleProgress.positionMs)

        playback.status.value = playback.status.value.copy(positionMs = 2_000)
        advanceUntilIdle()
        assertEquals("the article did not move on from the seek", 2_000L, store.state.articleProgress.positionMs)
        assertTrue(lengths[0] > 2_000)
    }

    @Test
    fun `test that skipping forward moves the article on once`() = runTest {
        val positions = mutableListOf<Long>()
        val playback = FakePlaybackController(shouldReportSeek = true)
        val store =
            storeWith(
                synthesizerProvider = {
                    FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root, audioDuration = 20.seconds)
                },
                playbackController = playback,
                recordPositionsInto = positions,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()
        playback.status.value = PlaybackState(phase = PlaybackPhase.Playing, positionMs = 1_000)
        advanceUntilIdle()
        val before = store.state.articleProgress.positionMs
        positions.clear()

        store.dispatch(ListenAction.Controls.ForwardClicked)
        advanceUntilIdle()

        assertEquals(listOf(before + SEEK_FORWARD_INCREMENT_MS), positions)
    }

    @Test
    fun `test that a seek near the end of the article ends it, even when the player only reports that it ended`() =
        runTest {
            val playback = FakePlaybackController()
            val store =
                storeWith(
                    synthesizerProvider = { FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root) },
                    playbackController = playback,
                ) {
                    Result.success(Content(text = longArticle(), languageTag = "en-US"))
                }
            store.listenAndPlay(TAB_ID, URL)
            advanceUntilIdle()
            playback.status.value = PlaybackState(phase = PlaybackPhase.Playing)
            advanceUntilIdle()

            // Short of the end, so it is played rather than finished at once, with little of the last chunk left.
            val durationMs = store.state.articleProgress.durationMs
            store.dispatch(ListenAction.Playback.SeekRequested((durationMs - 1_000).toFloat() / durationMs))
            advanceUntilIdle()
            assertEquals("the last chunk was not restarted on", 1, playback.restartedPositionsMs.size)

            // The little audio left plays out between two reports, so the first report from it is that it ended.
            playback.status.value =
                PlaybackState(
                    phase = PlaybackPhase.Ended,
                    chunk = ChunkState(index = playback.restartedChunks.last()),
                    positionMs = playback.restartedPositionsMs.last() + 500,
                )
            advanceUntilIdle()

            assertEquals(PlaybackPhase.Ended, store.state.playbackState.phase)
        }

    @Test
    fun `test that a seek to the end of the article finishes it at once, without making or playing anything`() =
        runTest {
            val playback = FakePlaybackController()
            val store =
                storeWith(
                    synthesizerProvider = { FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root) },
                    playbackController = playback,
                ) {
                    Result.success(Content(text = longArticle(), languageTag = "en-US"))
                }
            store.listenAndPlay(TAB_ID, URL)
            advanceUntilIdle()
            playback.status.value = PlaybackState(phase = PlaybackPhase.Playing, positionMs = 2_000)
            advanceUntilIdle()

            store.dispatch(ListenAction.Playback.SeekRequested(1f))
            advanceUntilIdle()

            assertTrue("the last chunk was made to be played", playback.restartedPositionsMs.isEmpty())
            assertEquals(listOf(store.state.articleProgress.durationMs), playback.finishedAtPositionsMs)
            assertEquals(PlaybackPhase.Ended, store.state.playbackState.phase)
            assertEquals(1f, store.state.articleProgress.fraction, 0f)

            // The player stops where it was, and says so.
            playback.status.value = PlaybackState(phase = PlaybackPhase.Paused, positionMs = 2_000)
            advanceUntilIdle()

            assertEquals("the article went back to where it was", PlaybackPhase.Ended, store.state.playbackState.phase)
            assertEquals(1f, store.state.articleProgress.fraction, 0f)
        }

    @Test
    fun `test that playing an article a seek finished starts it again from the top`() = runTest {
        val playback = FakePlaybackController(shouldReportSeek = true)
        val store =
            storeWith(
                synthesizerProvider = { FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root) },
                playbackController = playback,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()
        playback.status.value = PlaybackState(phase = PlaybackPhase.Playing, positionMs = 2_000)
        advanceUntilIdle()
        store.dispatch(ListenAction.Playback.SeekRequested(1f))
        advanceUntilIdle()

        store.dispatch(ListenAction.Controls.PlayPauseClicked)
        advanceUntilIdle()

        assertEquals(listOf(0 to 0L), playback.seekedChunkPositions)
        assertEquals(1, playback.resumed)
        assertEquals(0L, store.state.articleProgress.positionMs)
    }

    @Test
    fun `test that a seek back from an article a seek finished stays paused while its chunk is made`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(
                synthesizerProvider = {
                    FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root, timePerRequest = 1.seconds)
                },
                playbackController = playback,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()
        playback.status.value = PlaybackState(phase = PlaybackPhase.Playing, positionMs = 2_000)
        advanceUntilIdle()
        store.dispatch(ListenAction.Playback.SeekRequested(1f))
        advanceUntilIdle()

        val phases = mutableListOf<PlaybackPhase>()
        store.observeManually { phases.add(it.playbackState.phase) }.resume()

        store.dispatch(ListenAction.Playback.SeekRequested(0.5f))
        runCurrent()

        assertTrue("the chunk was not made again", playback.stopAndBufferPositionsMs.isNotEmpty())
        assertFalse("the article was shown playing on the way", PlaybackPhase.Buffering in phases)
        assertEquals(PlaybackPhase.Paused, store.state.playbackState.phase)
    }

    @Test
    fun `test that playing an article a seek finished shows it waiting while its top is made`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(
                synthesizerProvider = {
                    FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root, timePerRequest = 1.seconds)
                },
                playbackController = playback,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()
        playback.status.value = PlaybackState(phase = PlaybackPhase.Playing, positionMs = 2_000)
        advanceUntilIdle()
        store.dispatch(ListenAction.Playback.SeekRequested(1f))
        advanceUntilIdle()

        store.dispatch(ListenAction.Controls.PlayPauseClicked)
        runCurrent()

        assertEquals(PlaybackPhase.Buffering, store.state.playbackState.phase)
    }

    @Test
    fun `test that a seek past what is queued stays at the time it moved the article to`() = runTest {
        val positions = mutableListOf<Long>()
        val playback = FakePlaybackController(shouldReportSeek = true)
        val store =
            storeWith(
                // Long enough that the position asked for in the chunk is inside its audio once it is made.
                synthesizerProvider = {
                    FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root, audioDuration = 120.seconds)
                },
                playbackController = playback,
                recordPositionsInto = positions,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()
        val targetMs = store.state.articleProgress.chunkDurationsMs.take(SEEK_TARGET_CHUNK).sum() + 1_000
        positions.clear()

        store.seekTo(targetMs)
        advanceUntilIdle()
        assertEquals("the chunk did not have to be made", 1, playback.restartedPositionsMs.size)
        playback.status.value =
            PlaybackState(phase = PlaybackPhase.Playing, positionMs = playback.restartedPositionsMs.single())
        advanceUntilIdle()

        assertEquals(listOf(targetMs), positions.distinct())
    }

    @Test
    fun `test that the article keeps moving with the audio through the chunk a seek lands in`() = runTest {
        for (forward in listOf(true, false)) {
            val playback = FakePlaybackController(shouldReportSeek = true)
            val store =
                storeWith(
                    synthesizerProvider = {
                        FakeSpeechSynthesizer(
                            audioDirectory = temporaryFolder.root,
                            audioDuration = 20.seconds,
                            timePerRequest = 1.seconds,
                        )
                    },
                    playbackController = playback,
                ) {
                    Result.success(Content(text = longArticle(), languageTag = "en-US"))
                }
            store.listenAndPlay(TAB_ID, URL)
            advanceUntilIdle()
            val lengths = store.state.articleProgress.chunkDurationsMs
            val startChunk = if (forward) 0 else SEEK_TARGET_CHUNK
            if (!forward) {
                store.seekTo(lengths.take(startChunk).sum() + 1_000)
                advanceUntilIdle()
            }
            playback.status.value = playback.status.value.copy(phase = PlaybackPhase.Playing, positionMs = 5_000)
            advanceUntilIdle()

            val targetChunk = if (forward) SEEK_TARGET_CHUNK else 0
            val targetMs = store.state.articleProgress.chunkDurationsMs.take(targetChunk).sum() + 1_000
            store.seekTo(targetMs)
            advanceTimeBy(1_500.milliseconds)

            var last = store.state.articleProgress.positionMs
            val (index, startMs) =
                playback.seekedChunkPositions.lastOrNull()?.takeIf { !forward }
                    ?: (0 to playback.restartedPositionsMs.last())
            for (second in 1..15) {
                playback.status.value =
                    PlaybackState(
                        phase = PlaybackPhase.Playing,
                        chunk = ChunkState(index = index),
                        positionMs = startMs + second * 1_000L,
                    )
                advanceTimeBy(1_000.milliseconds)
                val now = store.state.articleProgress.positionMs
                assertTrue("forward=$forward: the article stopped at $now after $second s", now > last)
                last = now
            }
            store.dispatch(ListenAction.Session.StopRequested())
            advanceUntilIdle()
        }
    }

    @Test
    fun `test that what the player reports left over from a restart does not move the article`() = runTest {
        val playback = FakePlaybackController(shouldReportSeek = true)
        val store =
            storeWith(
                synthesizerProvider = {
                    FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root, audioDuration = 120.seconds)
                },
                playbackController = playback,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()
        val targetMs = store.state.articleProgress.chunkDurationsMs.take(SEEK_TARGET_CHUNK).sum() + 1_000
        store.seekTo(targetMs)
        advanceUntilIdle()
        val startMs = playback.restartedPositionsMs.single()
        val seeks = playback.seekedChunkPositions.toList()

        val before = store.state.articleProgress.positionMs

        // What the real controller reports first after a restart: the playlist it replaced, ended.
        playback.status.value = PlaybackState(phase = PlaybackPhase.Ended, positionMs = 0)
        advanceUntilIdle()

        assertEquals(
            "the article moved for the restart's left-over report",
            before,
            store.state.articleProgress.positionMs,
        )
        assertEquals("the left-over end of the playlist moved the player on", seeks, playback.seekedChunkPositions)
        assertTrue(
            "the left-over end of the playlist moved the player on",
            playback.chunksResumedAfterPlaybackEnded.isEmpty(),
        )

        playback.status.value = PlaybackState(phase = PlaybackPhase.Buffering, positionMs = startMs)
        advanceUntilIdle()
        playback.status.value = PlaybackState(phase = PlaybackPhase.Playing, positionMs = startMs + 1_000)
        advanceUntilIdle()

        assertEquals(targetMs + 1_000, store.state.articleProgress.positionMs)
    }

    @Test
    fun `test that the notification is told how long the article lasts`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(
                synthesizerProvider = { FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root) },
                playbackController = playback,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        playback.status.value = PlaybackState(phase = PlaybackPhase.Playing, positionMs = 1_000)
        advanceUntilIdle()

        assertTrue("the notification was never told", playback.articleDurationsMs.isNotEmpty())
        assertEquals(store.state.articleProgress.durationMs, playback.articleDurationsMs.last())
    }

    @Test
    fun `test that every file is handed to the player with the length it was measured at`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(
                synthesizerProvider = { FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root) },
                playbackController = playback,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()
        // Reported so that the store holds the lengths as they stand, as the reports of a playing player keep it.
        playback.status.value = PlaybackState(phase = PlaybackPhase.Playing)
        advanceUntilIdle()

        val lengths = store.state.articleProgress.chunkDurationsMs
        assertTrue("no file was handed over", playback.fileDurationsMs.isNotEmpty())
        assertEquals(lengths.take(playback.fileDurationsMs.size), playback.fileDurationsMs)
    }

    @Test
    fun `test that a seek from the notification moves playback to that point in the article`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(
                synthesizerProvider = { FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root) },
                playbackController = playback,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        val lengths = store.state.articleProgress.chunkDurationsMs
        playback.seekRequests.emit(lengths[0] + 1_000)
        advanceUntilIdle()

        assertEquals(listOf(1 to lengths[0] + 1_000), playback.seekedChunkPositions)
    }

    @Test
    fun `test that rewinding moves the article back ten seconds`() = runTest {
        val playback = FakePlaybackController()
        val store = storeWithTwoChunkProgress(playback, positionMs = 45_000)

        store.dispatch(ListenAction.Controls.RewindClicked)
        advanceUntilIdle()

        assertEquals(listOf(0 to 35_000L), playback.seekedChunkPositions)
    }

    @Test
    fun `test that rewinding near the start of the article stops at the start`() = runTest {
        val playback = FakePlaybackController()
        val store = storeWithTwoChunkProgress(playback, positionMs = 4_000)

        store.dispatch(ListenAction.Controls.RewindClicked)
        advanceUntilIdle()

        assertEquals(listOf(0 to 0L), playback.seekedChunkPositions)
    }

    @Test
    fun `test that skipping forward moves the article on thirty seconds, across chunks`() = runTest {
        val playback = FakePlaybackController()
        val store = storeWithTwoChunkProgress(playback, positionMs = 20_000)

        store.dispatch(ListenAction.Controls.ForwardClicked)
        advanceUntilIdle()

        assertEquals(listOf(1 to 50_000L), playback.seekedChunkPositions)
    }

    @Test
    fun `test that picking a playback speed sets it on the player`() = runTest {
        val playback = FakePlaybackController()
        val store = storeWithTwoChunkProgress(playback, positionMs = 20_000)

        store.dispatch(ListenAction.Controls.PlaybackSpeedSelected(PlaybackSpeed.X1_5))
        advanceUntilIdle()

        assertEquals(listOf(PlaybackSpeed.X1_5), playback.playbackSpeedSet)
    }

    @Test
    fun `test that skipping forward past the end of the article finishes it`() = runTest {
        val playback = FakePlaybackController()
        val store = storeWithTwoChunkProgress(playback, positionMs = 70_000)

        store.dispatch(ListenAction.Controls.ForwardClicked)
        advanceUntilIdle()

        assertTrue("playback was moved to the end to play nothing", playback.seekedChunkPositions.isEmpty())
        assertEquals(PlaybackPhase.Ended, store.state.playbackState.phase)
        assertEquals(80_000, store.state.articleProgress.positionMs)
    }

    @Test
    fun `test that rewinding moves the elapsed time back`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(
                // Long enough that a rewind from the second chunk lands inside the first rather than at its start.
                synthesizerProvider = {
                    FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root, audioDuration = 20.seconds)
                },
                playbackController = playback,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        // Three seconds into the second chunk, which lasts twenty.
        playback.status.value =
            PlaybackState(phase = PlaybackPhase.Playing, chunk = ChunkState(index = 1), positionMs = 23_000)
        advanceUntilIdle()
        val before = store.state.articleProgress.positionMs

        store.dispatch(ListenAction.Controls.RewindClicked)
        advanceUntilIdle()
        val (chunk, positionMs) = playback.seekedChunkPositions.single()
        playback.status.value =
            PlaybackState(phase = PlaybackPhase.Playing, chunk = ChunkState(index = chunk), positionMs = positionMs)
        advanceUntilIdle()

        assertEquals(before - SEEK_BACK_INCREMENT_MS, store.state.articleProgress.positionMs)
    }

    @Test
    fun `test that a chunk a seek is moving into is not thrown away by the window the player was last in`() = runTest {
        val synthesizer = FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root, timePerRequest = 1.seconds)
        val audioCache = FakeAudioFileCache()
        val playback = FakePlaybackController()
        val store =
            storeWith(
                synthesizerProvider = { synthesizer },
                playbackController = playback,
                audioCache = audioCache,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()
        for (chunk in 1..3) reportPlaying(playback, chunk)
        val sought = synthesizer.files[2]

        // Playing on into chunk 5 moves the window past chunk 2, but has to wait its turn behind the chunk chunk 4 is
        // having made, as it does while the reader skips back faster than the player reports.
        playback.status.value = playingIn(4)
        runCurrent()
        playback.status.value = playingIn(5)
        runCurrent()
        store.seekTo(store.state.articleProgress.chunkDurationsMs.take(2).sum() + 1_000)
        advanceUntilIdle()

        assertEquals(2, playback.seekedChunkPositions.single().first)
        assertFalse("the chunk sought was thrown away", sought in audioCache.deleted)
    }

    @Test
    fun `test that rewinding keeps the audio already queued ahead of the player`() = runTest {
        val audioCache = FakeAudioFileCache()
        val playback = FakePlaybackController()
        val store =
            storeWith(
                synthesizerProvider = { FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root) },
                playbackController = playback,
                audioCache = audioCache,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()
        for (chunk in 1..4) {
            playback.status.value =
                PlaybackState(
                    phase = PlaybackPhase.Playing,
                    chunk = ChunkState(index = chunk),
                    positionMs = FAKE_AUDIO_MS * chunk + 2_000,
                )
            advanceUntilIdle()
        }
        val queuedAhead = playback.queued.takeLast(AUDIO_WINDOW_RADIUS)

        store.dispatch(ListenAction.Controls.RewindClicked)
        advanceUntilIdle()
        val (chunk, positionMs) = playback.seekedChunkPositions.single()
        playback.status.value =
            PlaybackState(phase = PlaybackPhase.Playing, chunk = ChunkState(index = chunk), positionMs = positionMs)
        advanceUntilIdle()

        assertEquals("the rewind did not land in the chunk before", 2, chunk)
        assertTrue("audio the player still has queued was deleted", audioCache.deleted.none { it in queuedAhead })
    }

    @Test
    fun `test that the player correcting its position by a little does not move the elapsed time back`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(
                synthesizerProvider = { FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root) },
                playbackController = playback,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        playback.status.value =
            PlaybackState(phase = PlaybackPhase.Playing, chunk = ChunkState(index = 1), positionMs = 3_000)
        advanceUntilIdle()
        val before = store.state.articleProgress.positionMs

        // As far back as the player corrects the position it estimated once the audio has started.
        playback.status.value =
            PlaybackState(phase = PlaybackPhase.Playing, chunk = ChunkState(index = 1), positionMs = 2_800)
        advanceUntilIdle()

        assertEquals(before, store.state.articleProgress.positionMs)
    }

    /**
     * A session reading [longArticle], with its progress set to two chunks of 40 seconds each and [positionMs] into
     * them, so that a test knows where a skip lands. Both chunks are queued by the time this returns.
     */
    private fun TestScope.storeWithTwoChunkProgress(playback: FakePlaybackController, positionMs: Long): ListenStore {
        val store =
            storeWith(
                synthesizerProvider = { FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root) },
                playbackController = playback,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        store.dispatch(
            ListenAction.Playback.ArticleProgressReverted(
                positionMs = positionMs,
                durationMs = 80_000,
                chunkDurationsMs = listOf(40_000, 40_000),
            )
        )
        return store
    }

    @Test
    fun `test that seeking with no session running does nothing`() = runTest {
        val playback = FakePlaybackController()
        val store = storeWith(playbackController = playback) { Result.success(Content("Article text", "en-US")) }

        store.dispatch(ListenAction.Playback.SeekRequested(0.5f))
        advanceUntilIdle()

        assertTrue(playback.restartedPositionsMs.isEmpty())
        assertTrue(playback.seekedChunkPositions.isEmpty())
        assertNull(store.state.error)
    }

    @Test
    fun `test that a seek with nowhere to go puts the elapsed time back where it was`() = runTest {
        val store = storeWith { Result.success(Content("Article text", "en-US")) }
        store.dispatch(ListenAction.Playback.ArticleProgressReverted(positionMs = 30_000, durationMs = 60_000))
        val before = store.state.articleProgress

        store.dispatch(ListenAction.Playback.SeekRequested(0.75f))

        assertEquals(before, store.state.articleProgress)
    }

    @Test
    fun `test that a seek moves the elapsed time to where the reader let go before the player gets there`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(
                synthesizerProvider = { FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root) },
                playbackController = playback,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        val target = store.state.articleProgress.chunkDurationsMs.take(SEEK_TARGET_CHUNK).sum() + 1_000
        store.seekTo(target)

        assertTrue("the player was already moved", playback.restartedPositionsMs.isEmpty())
        assertEquals(target, store.state.articleProgress.positionMs)
    }

    @Test
    fun `test that a seek whose synthesis fails puts the elapsed time back where it was`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(
                synthesizerProvider = {
                    FakeSpeechSynthesizer(
                        audioDirectory = temporaryFolder.root,
                        failAtRequest = FIRST_REQUEST_AFTER_LOOKAHEAD,
                    )
                },
                playbackController = playback,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()
        // Reported so that the store holds the lengths as they stand, as the reports of a playing player keep it.
        playback.status.value = PlaybackState(phase = PlaybackPhase.Playing)
        advanceUntilIdle()
        val before = store.state.articleProgress

        store.seekTo(before.chunkDurationsMs.take(SEEK_TARGET_CHUNK).sum() + 1_000)
        advanceUntilIdle()

        assertEquals(ListenError.SynthesisFailed, store.state.error)
        assertEquals(before, store.state.articleProgress)
    }

    @Test
    fun `test that a seek whose synthesis fails moves the buffering back to where it was`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(
                synthesizerProvider = {
                    FakeSpeechSynthesizer(
                        audioDirectory = temporaryFolder.root,
                        failAtRequest = FIRST_REQUEST_AFTER_LOOKAHEAD,
                    )
                },
                playbackController = playback,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()
        val target = store.state.articleProgress.chunkDurationsMs.take(SEEK_TARGET_CHUNK).sum() + 1_000

        store.seekTo(target)
        advanceUntilIdle()

        assertEquals(listOf(target, 0L), playback.stopAndBufferPositionsMs)
        assertEquals(0L, playback.status.value.positionMs)
    }

    @Test
    fun `test that a seek overtaken by another does not put the elapsed time back`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(
                synthesizerProvider = {
                    FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root, timePerRequest = 1.seconds)
                },
                playbackController = playback,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        // Reported so that the store holds the lengths as they stand once the lookahead was measured, as the reports
        // of a playing player keep it.
        playback.status.value = playback.status.value.copy(phase = PlaybackPhase.Playing)
        advanceUntilIdle()

        val lengths = store.state.articleProgress.chunkDurationsMs
        val firstSeek = lengths.take(SEEK_TARGET_CHUNK).sum() + 1_000
        val secondSeek = lengths.take(SEEK_TARGET_CHUNK + 2).sum() + 1_000

        store.seekTo(firstSeek)
        advanceTimeBy(100.milliseconds)
        store.seekTo(secondSeek)
        advanceUntilIdle()

        assertEquals(secondSeek, store.state.articleProgress.positionMs)
    }

    @Test
    fun `test that a seek cut short by a cancelled session does not put the elapsed time back`() = runTest {
        val store =
            storeWith(
                synthesizerProvider = {
                    FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root, timePerRequest = 1.seconds)
                }
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        store.seekTo(store.state.articleProgress.chunkDurationsMs.take(SEEK_TARGET_CHUNK).sum() + 1_000)
        advanceTimeBy(100.milliseconds)
        store.dispatch(ListenAction.Session.StopRequested())
        advanceUntilIdle()

        assertEquals(ArticleProgress(), store.state.articleProgress)
    }

    @Test
    fun `test that seeking into a queued chunk the system reclaimed makes it again`() = runTest {
        val synthesizer = FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root)
        val cache = FakeAudioFileCache()
        val playback = FakePlaybackController()
        val store =
            storeWith(
                synthesizerProvider = { synthesizer },
                playbackController = playback,
                audioCache = cache,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        val lengths = store.state.articleProgress.chunkDurationsMs

        // The chunk a second into the article is one the player already holds, so this is the seek that would
        // otherwise be answered by moving the player onto audio that is no longer there.
        cache.reclaim(synthesizer.files[1])
        store.seekTo(lengths[0] + 1_000)
        advanceUntilIdle()

        assertTrue("the player was moved onto audio the system had taken", playback.seekedChunkPositions.isEmpty())
        assertEquals(lengths[0] + 1_000, playback.restartedPositionsMs.single())
        assertNotEquals(
            "the playlist was restarted on the file that had gone",
            synthesizer.files[1],
            playback.restartedFiles.single(),
        )
        assertNull(store.state.error)
    }

    @Test
    fun `test that the system emptying the cache mid-playback is not the end of the article`() = runTest {
        val synthesizer = FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root)
        val cache = FakeAudioFileCache()
        val playback = FakePlaybackController()
        val store =
            storeWith(
                synthesizerProvider = { synthesizer },
                playbackController = playback,
                audioCache = cache,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        val played = synthesizer.files[0]

        // The player is part way through the opening when the system takes the directory, so the first it is heard of
        // is the player failing on a file that was there a moment ago.
        cache.reclaimEverything()
        playback.status.value = PlaybackState(phase = PlaybackPhase.Failed, chunk = ChunkState(index = 0))
        advanceUntilIdle()

        assertNull("the reader was shown an error instead of the article carrying on", store.state.error)
        assertNotEquals(
            "the playlist was restarted on the file the player had just failed on",
            played,
            playback.restartedFiles.single(),
        )
    }

    @Test
    fun `test that the article carries on from where it had got to after the cache is emptied`() = runTest {
        val playback = FakePlaybackController()
        val cache = FakeAudioFileCache()
        val store =
            storeWith(
                synthesizerProvider = { FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root) },
                playbackController = playback,
                audioCache = cache,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        // Two chunks in and a second into the third, so that the recovery has a place to come back to rather than the
        // top of the article.
        val lengths = store.state.articleProgress.chunkDurationsMs
        val positionMs = lengths.take(2).sum() + 1_000
        playback.status.value =
            PlaybackState(phase = PlaybackPhase.Playing, chunk = ChunkState(index = 2), positionMs = positionMs)
        advanceUntilIdle()

        cache.reclaimEverything()
        playback.status.value =
            PlaybackState(phase = PlaybackPhase.Failed, chunk = ChunkState(index = 2), positionMs = positionMs)
        advanceUntilIdle()

        assertEquals(positionMs, store.state.articleProgress.positionMs)
        assertEquals(positionMs, playback.restartedPositionsMs.single())
    }

    @Test
    fun `test that a player failure recreates a chunk even if a file is found for the chunk`() = runTest {
        val synthesizer = FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root)
        val cache = FakeAudioFileCache()
        val playback = FakePlaybackController()
        val store =
            storeWith(
                synthesizerProvider = { synthesizer },
                playbackController = playback,
                audioCache = cache,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        val played = synthesizer.files[0]
        assertTrue("the premise is a file the player failed on while it was still there", played.exists())

        reportFailure(playback, index = 0)
        advanceUntilIdle()

        assertTrue("the file that failed was kept rather than thrown away", cache.deleted.contains(played))
        assertNotEquals(
            "the playlist was restarted on the file the player had just failed on",
            played,
            playback.restartedFiles.single(),
        )
        assertNull(store.state.error)
    }

    @Test
    fun `test that a chunk that keeps failing is reported rather than made forever`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(
                synthesizerProvider = { FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root) },
                playbackController = playback,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        repeat(MAX_RECOVERY_ATTEMPTS) {
            reportFailure(playback, index = 0, positionMs = (it + 1).toLong())
            advanceUntilIdle()
            assertNull("the article was given up on attempt ${it + 1}", store.state.error)
        }

        reportFailure(playback, index = 0, positionMs = (MAX_RECOVERY_ATTEMPTS + 1).toLong())
        advanceUntilIdle()

        assertEquals(ListenError.PlaybackFailed, store.state.error)
        assertEquals(MAX_RECOVERY_ATTEMPTS, playback.restartedPositionsMs.size)
    }

    @Test
    fun `test that a later chunk gets its own count after one that kept failing`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(
                synthesizerProvider = { FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root) },
                playbackController = playback,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        repeat(MAX_RECOVERY_ATTEMPTS) {
            reportFailure(playback, index = 0, positionMs = (it + 1).toLong())
            advanceUntilIdle()
        }
        val restartsBefore = playback.restartedPositionsMs.size

        reportFailure(playback, index = 1, positionMs = FAKE_AUDIO_MS + 1_000)
        advanceUntilIdle()

        assertNull("the count carried over from the chunk before it", store.state.error)
        assertEquals(restartsBefore + 1, playback.restartedPositionsMs.size)
    }

    @Test
    fun `test that a failure does not take the playback from a seek the reader asked for`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(
                synthesizerProvider = {
                    FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root, timePerRequest = 1.seconds)
                },
                playbackController = playback,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        val lengths = store.state.articleProgress.chunkDurationsMs
        val wanted = lengths.take(SEEK_TARGET_CHUNK).sum() + 1_000

        store.seekTo(wanted)
        advanceTimeBy(100.milliseconds)
        reportFailure(playback, index = 0)
        advanceUntilIdle()

        assertEquals("the failure restarted the playback as well", 1, playback.restartedPositionsMs.size)

        playback.reportPlayingFromRestart()
        advanceUntilIdle()

        assertEquals(SEEK_TARGET_CHUNK, store.state.playbackState.chunk.index)
        assertNull(store.state.error)
    }

    @Test
    fun `test that a seek the reader asked for takes the playback from a failure being recovered`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(
                synthesizerProvider = {
                    FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root, timePerRequest = 1.seconds)
                },
                playbackController = playback,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        reportFailure(playback, index = 0)
        advanceTimeBy(100.milliseconds)

        // Read after the failure, which is the report that refreshes the lengths the seek is measured against.
        val lengths = store.state.articleProgress.chunkDurationsMs
        store.seekTo(lengths.take(SEEK_TARGET_CHUNK).sum() + 1_000)
        advanceUntilIdle()

        assertEquals("the recovery restarted the playback as well", 1, playback.restartedPositionsMs.size)

        playback.reportPlayingFromRestart()
        advanceUntilIdle()

        assertEquals(SEEK_TARGET_CHUNK, store.state.playbackState.chunk.index)
    }

    @Test
    fun `test that a failure declined for a seek does not spend one of the chunk's attempts`() = runTest {
        val playback = FakePlaybackController()
        val store =
            storeWith(
                synthesizerProvider = {
                    FakeSpeechSynthesizer(audioDirectory = temporaryFolder.root, timePerRequest = 1.seconds)
                },
                playbackController = playback,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en-US"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        val lengths = store.state.articleProgress.chunkDurationsMs
        store.seekTo(lengths.take(SEEK_TARGET_CHUNK).sum() + 1_000)
        advanceTimeBy(100.milliseconds)
        repeat(MAX_RECOVERY_ATTEMPTS + 1) {
            reportFailure(playback, index = 0, positionMs = (it + 1).toLong())
        }
        advanceUntilIdle()

        val restartsAfterSeek = playback.restartedPositionsMs.size
        repeat(MAX_RECOVERY_ATTEMPTS) {
            reportFailure(playback, index = 0, positionMs = (it + 100).toLong())
            advanceUntilIdle()
        }

        assertNull("the declined failures were counted against the chunk", store.state.error)
        assertEquals(restartsAfterSeek + MAX_RECOVERY_ATTEMPTS, playback.restartedPositionsMs.size)
    }

    @Test
    fun `test that a seek whose synthesis fails does not keep the playback buffering against later repairs`() =
        runTest {
            val playback = FakePlaybackController()
            val store =
                storeWith(
                    synthesizerProvider = {
                        FakeSpeechSynthesizer(
                            audioDirectory = temporaryFolder.root,
                            failAtRequest = FIRST_REQUEST_AFTER_LOOKAHEAD,
                        )
                    },
                    playbackController = playback,
                ) {
                    Result.success(Content(text = longArticle(), languageTag = "en-US"))
                }
            store.listenAndPlay(TAB_ID, URL)
            advanceUntilIdle()

            val lengths = store.state.articleProgress.chunkDurationsMs
            store.seekTo(lengths.take(SEEK_TARGET_CHUNK).sum() + 1_000)
            advanceUntilIdle()

            assertEquals(ListenError.SynthesisFailed, store.state.error)
            val restartsBefore = playback.restartedPositionsMs.size

            reportFailure(playback, index = 0)
            advanceUntilIdle()

            assertEquals(
                "the seek that failed was still keeping the playback buffering",
                restartsBefore + 1,
                playback.restartedPositionsMs.size,
            )
        }

    @Test
    fun `test that VoicesClicked refreshes the voice list and adds downloaded voice`() = runTest {
        val oldList = listOf(Voice(id = "1", locale = Locale.US))
        val newVoice = Voice(id = "2", locale = Locale.UK)
        val newList = oldList + newVoice

        val playback = FakePlaybackController()
        val synthesizer = FakeSpeechSynthesizer(voices = oldList, enginePackageName = ENGINE)
        val store =
            storeWith(
                synthesizerProvider = {
                    synthesizer
                },
                playbackController = playback,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        synthesizer.updateVoices(newList)
        store.dispatch(ListenAction.Controls.VoicesClicked)
        advanceUntilIdle()

        assertEquals(newList, store.state.voiceState.availableVoices)
    }

    @Test
    fun `test that VoicesClicked refreshes the voice list and removes deleted voice`() = runTest {
        val oldList = listOf(Voice(id = "1", locale = Locale.US), Voice(id = "2", locale = Locale.UK))
        val newList = listOf(Voice(id = "1", locale = Locale.US))

        val playback = FakePlaybackController()
        val synthesizer = FakeSpeechSynthesizer(voices = oldList, enginePackageName = ENGINE)
        val store =
            storeWith(
                synthesizerProvider = {
                    synthesizer
                },
                playbackController = playback,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        synthesizer.updateVoices(newList)
        store.dispatch(ListenAction.Controls.VoicesClicked)
        advanceUntilIdle()

        assertEquals(newList, store.state.voiceState.availableVoices)
    }

    @Test
    fun `test that VoicesClicked sends an error and pauses playback if no voices are available`() = runTest {
        val oldList = listOf(Voice(id = "1", locale = Locale.US), Voice(id = "2", locale = Locale.UK))
        val newList = emptyList<Voice>()

        val playback = FakePlaybackController()
        val synthesizer = FakeSpeechSynthesizer(voices = oldList, enginePackageName = ENGINE)
        val store =
            storeWith(
                synthesizerProvider = {
                    synthesizer
                },
                playbackController = playback,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()

        synthesizer.updateVoices(newList)
        store.dispatch(ListenAction.Controls.VoicesClicked)
        advanceUntilIdle()

        assertEquals(ListenError.NoOfflineVoice, store.state.error)
        assertEquals(PlaybackPhase.Paused, playback.status.value.phase)
    }

    @Test
    fun `test that VoicesClicked keeps the selected voice if it is available`() = runTest {
        val us = Voice(id = "1", locale = Locale.US)
        val uk = Voice(id = "2", locale = Locale.UK)
        val ca = Voice(id = "3", locale = Locale.CANADA)
        val oldList = listOf(us, uk, ca)
        val newList = oldList - uk

        val playback = FakePlaybackController()
        val synthesizer = FakeSpeechSynthesizer(voices = oldList, enginePackageName = ENGINE)
        val store =
            storeWith(
                synthesizerProvider = {
                    synthesizer
                },
                settings = ListenSettings.inMemory(ENGINE, voiceIds = mapOf("en" to "3")),
                playbackController = playback,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()
        assertEquals(ca, store.state.voiceState.selectedVoice)

        synthesizer.updateVoices(newList)
        store.dispatch(ListenAction.Controls.VoicesClicked)
        advanceUntilIdle()

        assertEquals(ca, store.state.voiceState.selectedVoice)
    }

    @Test
    fun `test that VoicesClicked chooses a fallback voice if the selected voice is not available`() = runTest {
        val us = Voice(id = "1", locale = Locale.US)
        val uk = Voice(id = "2", locale = Locale.UK)
        val ca = Voice(id = "3", locale = Locale.CANADA)
        val oldList = listOf(us, uk, ca)
        val newList = oldList - uk

        val playback = FakePlaybackController()
        val synthesizer = FakeSpeechSynthesizer(voices = oldList, enginePackageName = ENGINE)
        val store =
            storeWith(
                synthesizerProvider = {
                    synthesizer
                },
                settings = ListenSettings.inMemory(ENGINE, voiceIds = mapOf("en" to "2")),
                playbackController = playback,
            ) {
                Result.success(Content(text = longArticle(), languageTag = "en"))
            }
        store.listenAndPlay(TAB_ID, URL)
        advanceUntilIdle()
        assertEquals(uk, store.state.voiceState.selectedVoice)

        synthesizer.updateVoices(newList)
        store.dispatch(ListenAction.Controls.VoicesClicked)
        advanceUntilIdle()

        assertEquals(us, store.state.voiceState.selectedVoice)
    }

    /** What the player reports once it plays the playlist it was last restarted on, from where it was restarted. */
    private fun FakePlaybackController.reportPlayingFromRestart() {
        status.value =
            PlaybackState(
                phase = PlaybackPhase.Playing,
                chunk = ChunkState(index = restartedChunks.last()),
                positionMs = restartedPositionsMs.last(),
            )
    }

    private fun reportFailure(playback: FakePlaybackController, index: Int, positionMs: Long = 0) {
        playback.status.value =
            PlaybackState(phase = PlaybackPhase.Failed, chunk = ChunkState(index = index), positionMs = positionMs)
    }

    private fun longArticle() = (1..400).joinToString(" ") { "Sentence $it is right here." }

    /**
     * [cache], with the emptying slowed down enough for a session starting in the middle of it to be seen writing into
     * the directory it is deleting.
     */
    private fun slowToEmpty(cache: AudioFileCache) =
        object : AudioFileCache by cache {
            override suspend fun clear() {
                delay(1.seconds)
                cache.clear()
            }
        }

    /**
     * A browser with both tabs open and in reader mode, and [TAB_ID] selected, which is what the middleware sees when a
     * session starts: listening is only offered from the reader view of the selected tab.
     */
    private fun browserStoreWithOpenTabs(title: String = "") =
        BrowserStore(
            BrowserState(
                tabs =
                    listOf(
                        createTab(url = URL, id = TAB_ID, title = title, readerState = ReaderState(active = true)),
                        createTab(url = URL, id = OTHER_TAB_ID, readerState = ReaderState(active = true)),
                    ),
                selectedTabId = TAB_ID,
            )
        )

    /** Starts a session, which reads the article out as soon as it can. */
    private fun ListenStore.listenAndPlay(tabId: String, url: String) {
        dispatch(ListenAction.Session.ListenRequested(tabId, url))
    }

    /**
     * Seeks to [positionMs] into the article, as the progress bar does: as a share of the article's estimated length.
     */
    private fun ListenStore.seekTo(positionMs: Long) =
        dispatch(ListenAction.Playback.SeekRequested(positionMs.toFloat() / state.articleProgress.durationMs))

    /** The player reading chunk [chunk], two seconds in. */
    private fun playingIn(chunk: Int) =
        PlaybackState(
            phase = PlaybackPhase.Playing,
            chunk = ChunkState(index = chunk),
            positionMs = FAKE_AUDIO_MS * chunk + 2_000,
        )

    /** Reports the player reading chunk [chunk], and lets the session answer. */
    private fun TestScope.reportPlaying(playback: FakePlaybackController, chunk: Int) {
        playback.status.value = playingIn(chunk)
        advanceUntilIdle()
    }

    /** Records every action that reaches the store into [into], and lets it through. */
    private fun recordingMiddleware(into: MutableList<ListenAction>): Middleware<ListenState, ListenAction> =
        { _, next, action ->
            into.add(action)
            next(action)
        }

    /** Records the position each time a report from the player, or a revert, moves the article. */
    private fun positionRecordingMiddleware(into: MutableList<Long>): Middleware<ListenState, ListenAction> =
        { store, next, action ->
            val before = store.state.articleProgress
            next(action)
            val after = store.state.articleProgress
            val isProgressAction =
                action is PlayerReportReceived || action is ListenAction.Playback.ArticleProgressReverted
            if (isProgressAction && after != before) into.add(after.positionMs)
        }

    private fun TestScope.storeWith(
        synthesizerProvider: () -> SpeechSynthesizer = { FakeSpeechSynthesizer() },
        playbackController: PlaybackController = FakePlaybackController(),
        audioCache: AudioFileCache = FakeAudioFileCache(),
        settings: ListenSettings = ListenSettings.inMemory(),
        chunker: TextChunker = TextChunker.android(),
        browserStore: BrowserStore = browserStoreWithOpenTabs(),
        recordInto: MutableList<ListenAction>? = null,
        recordPositionsInto: MutableList<Long>? = null,
        contentProvider: ContentProvider,
    ): ListenStore {
        val middlewareScope = CoroutineScope(StandardTestDispatcher(testScheduler))
        middlewareScopes.add(middlewareScope)

        return ListenStore(
            initialState = ListenState(),
            reducer = ::listenReducer,
            middleware =
                listOfNotNull(
                    recordInto?.let(::recordingMiddleware),
                    recordPositionsInto?.let(::positionRecordingMiddleware),
                    ListenMiddleware(
                        browserStore = browserStore,
                        contentProvider = contentProvider,
                        synthesizerProvider = synthesizerProvider,
                        audioCache = audioCache,
                        playbackController = playbackController,
                        settings = settings,
                        scope = middlewareScope,
                        ioDispatcher = Dispatchers.Unconfined,
                        chunker = chunker,
                    ),
                ),
        )
    }
}
