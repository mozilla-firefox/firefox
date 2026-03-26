/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize

import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.runTest
import mozilla.components.feature.summarize.SummarizationState.Finished
import mozilla.components.feature.summarize.SummarizationState.Inert
import mozilla.components.feature.summarize.SummarizationState.Loading
import mozilla.components.feature.summarize.SummarizationState.ShakeConsentRequired
import mozilla.components.feature.summarize.SummarizationState.Summarized
import mozilla.components.feature.summarize.SummarizationState.Summarizing
import mozilla.components.feature.summarize.content.PageContentExtractor
import mozilla.components.feature.summarize.content.PageMetadata
import mozilla.components.feature.summarize.fakes.FakeCloudProvider
import mozilla.components.feature.summarize.fakes.FakeLlm
import mozilla.components.feature.summarize.settings.SummarizationSettings
import mozilla.components.ui.richtext.parsing.Parser
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import kotlin.time.Duration.Companion.seconds

class SummarizationStoreTest {

    private val reportedErrors = mutableListOf<Throwable>()
    private val errorReporter = ErrorReporter { reportedErrors.add(it) }
    private val noopReporter = ErrorReporter { }
    private val parser = Parser()

    @Before
    fun setUp() {
        reportedErrors.clear()
    }

    @Test
    fun `test that we can consent to shake`() = runTest {
        val settings = SummarizationSettings.inMemory()
        val provider = FakeCloudProvider(llm = FakeLlm.successful)

        val store = SummarizationStore(
            initialState = Inert(true),
            reducer = ::summarizationReducer,
            middleware = listOf(
                SummarizationMiddleware(
                    settings = settings,
                    llmProvider = provider,
                    pageContentExtractor = { Result.success("") },
                    pageMetadataExtractor = { Result.success(PageMetadata(listOf(), "")) },
                    errorReporter = noopReporter,
                    scope = backgroundScope,
                ),
            ),
        )

        val states = mutableListOf<SummarizationState>()
        backgroundScope.launch {
            store.stateFlow.toList(states)
        }

        store.dispatch(ViewAppeared)
        testScheduler.advanceTimeBy(15.seconds)
        store.dispatch(OffDeviceSummarizationShakeConsentAction.AllowClicked)
        testScheduler.advanceTimeBy(15.seconds)

        val expected = listOf<SummarizationState>(
            Inert(true),
            ShakeConsentRequired,
            Loading(provider.info),
            Summarizing(provider.info, parser.parse("# This is the article\n")),
            Summarizing(provider.info, parser.parse("# This is the article\nThis is some content...\n")),
            Summarizing(provider.info, parser.parse("# This is the article\nThis is some content...\nThis is some *bold* content.\n")),
            Summarized(provider.info, parser.parse("# This is the article\nThis is some content...\nThis is some *bold* content.\n")),
        )

        assertEquals(expected, states)
        assertTrue(settings.getHasConsentedToShake().first())
    }

    @Test
    fun `test that we can decline consenting to shake`() = runTest {
        val settings = SummarizationSettings.inMemory()

        val store = SummarizationStore(
            initialState = Inert(true),
            reducer = ::summarizationReducer,
            middleware = listOf(
                SummarizationMiddleware(
                    settings = settings,
                    llmProvider = FakeCloudProvider(llm = FakeLlm.successful),
                    pageContentExtractor = { Result.success("") },
                    pageMetadataExtractor = { Result.success(PageMetadata(listOf(), "")) },
                    errorReporter = noopReporter,
                    scope = backgroundScope,
                ),
            ),
        )

        val states = mutableListOf<SummarizationState>()
        backgroundScope.launch {
            store.stateFlow.toList(states)
        }
        testScheduler.advanceTimeBy(1.seconds)

        store.dispatch(ViewAppeared)
        testScheduler.advanceTimeBy(1.seconds)

        store.dispatch(OffDeviceSummarizationShakeConsentAction.CancelClicked)
        testScheduler.advanceTimeBy(1.seconds)

        val expected = listOf<SummarizationState>(
            Inert(true),
            ShakeConsentRequired,
            Finished.Cancelled,
        )

        assertEquals(expected, states)
        assertFalse(settings.getHasConsentedToShake().first())
    }

    @Test
    fun `If a user has already consented to shake, the llm is prompted with the default instructions`() = runTest {
        val llm = FakeLlm.successful
        val provider = FakeCloudProvider(llm = llm)
        val content = "this is expected content."
        val store = SummarizationStore(
            initialState = Inert(true),
            reducer = ::summarizationReducer,
            middleware = listOf(
                SummarizationMiddleware(
                    llmProvider = provider,
                    settings = SummarizationSettings.inMemory(hasConsentedToShake = true),
                    pageContentExtractor = { Result.success(content) },
                    pageMetadataExtractor = { Result.success(PageMetadata(listOf("Article"), "en")) },
                    errorReporter = noopReporter,
                    scope = backgroundScope,
                ),
            ),
        )

        val states = mutableListOf<SummarizationState>()
        backgroundScope.launch {
            store.stateFlow.toList(states)
        }
        testScheduler.advanceTimeBy(1.seconds)

        store.dispatch(ViewAppeared)
        testScheduler.advanceTimeBy(15.seconds)

        val expected = listOf<SummarizationState>(
            Inert(true),
            Loading(provider.info),
            Summarizing(provider.info, parser.parse("# This is the article\n")),
            Summarizing(provider.info, parser.parse("# This is the article\nThis is some content...\n")),
            Summarizing(provider.info, parser.parse("# This is the article\nThis is some content...\nThis is some *bold* content.\n")),
            Summarized(provider.info, parser.parse("# This is the article\nThis is some content...\nThis is some *bold* content.\n")),
        )

        assertEquals(expected, states)
        assertEquals("${defaultInstructions("en")} $content", llm.promptCapture)
    }

    @Test
    fun `if the page extractor fails, the failure is forwarded as a summarization failure`() = runTest {
        val failureThrowable = PageContentExtractor.Exception()
        val provider = FakeCloudProvider(llm = FakeLlm.successful)
        val store = SummarizationStore(
            initialState = Inert(true),
            reducer = ::summarizationReducer,
            middleware = listOf(
                SummarizationMiddleware(
                    llmProvider = provider,
                    settings = SummarizationSettings.inMemory(hasConsentedToShake = true),
                    pageContentExtractor = { Result.failure(failureThrowable) },
                    pageMetadataExtractor = { Result.success(PageMetadata(listOf(), "")) },
                    errorReporter = errorReporter,
                    scope = backgroundScope,
                ),
            ),
        )

        val states = mutableListOf<SummarizationState>()
        backgroundScope.launch {
            store.stateFlow.toList(states)
        }
        testScheduler.advanceTimeBy(1.seconds)

        store.dispatch(ViewAppeared)
        testScheduler.advanceTimeBy(15.seconds)

        val expected = listOf<SummarizationState>(
            Inert(true),
            Loading(provider.info),
            SummarizationState.Error(SummarizationError.SummarizationFailed(failureThrowable)),
        )

        assertEquals(expected, states)
        assertEquals(listOf(failureThrowable), reportedErrors)
    }

    @Test
    fun `if the page metadata indicates a recipe, the llm is prompted with the recipe instructions`() = runTest {
        val llm = FakeLlm.successful
        val content = "this is expected content."
        val provider = FakeCloudProvider(llm = llm)
        val store = SummarizationStore(
            initialState = Inert(true),
            reducer = ::summarizationReducer,
            middleware = listOf(
                SummarizationMiddleware(
                    settings = SummarizationSettings.inMemory(hasConsentedToShake = true),
                    llmProvider = provider,
                    pageContentExtractor = { Result.success(content) },
                    pageMetadataExtractor = { Result.success(PageMetadata(listOf("Recipe"), "en")) },
                    errorReporter = noopReporter,
                    scope = backgroundScope,
                ),
            ),
        )

        val states = mutableListOf<SummarizationState>()
        backgroundScope.launch {
            store.stateFlow.toList(states)
        }
        testScheduler.advanceTimeBy(1.seconds)

        store.dispatch(ViewAppeared)
        testScheduler.advanceTimeBy(15.seconds)

        val expected = listOf<SummarizationState>(
            Inert(true),
            Loading(provider.info),
            Summarizing(provider.info, parser.parse("# This is the article\n")),
            Summarizing(provider.info, parser.parse("# This is the article\nThis is some content...\n")),
            Summarizing(provider.info, parser.parse("# This is the article\nThis is some content...\nThis is some *bold* content.\n")),
            Summarized(provider.info, parser.parse("# This is the article\nThis is some content...\nThis is some *bold* content.\n")),
        )

        assertEquals(expected, states)
        assertEquals("${recipeInstructions("en")} $content", llm.promptCapture)
    }

    @Test
    fun `page metadata language is inserted into prompt`() = runTest {
        val llm = FakeLlm.successful
        val content = "this is expected content."
        val provider = FakeCloudProvider(llm = llm)
        val store = SummarizationStore(
            initialState = Inert(true),
            reducer = ::summarizationReducer,
            middleware = listOf(
                SummarizationMiddleware(
                    settings = SummarizationSettings.inMemory(hasConsentedToShake = true),
                    llmProvider = provider,
                    pageContentExtractor = { Result.success(content) },
                    pageMetadataExtractor = { Result.success(PageMetadata(listOf("Recipe"), "es")) },
                    errorReporter = noopReporter,
                    scope = backgroundScope,
                ),
            ),
        )

        val states = mutableListOf<SummarizationState>()
        backgroundScope.launch {
            store.stateFlow.toList(states)
        }
        testScheduler.advanceTimeBy(1.seconds)

        store.dispatch(ViewAppeared)
        testScheduler.advanceTimeBy(15.seconds)

        val expected = listOf<SummarizationState>(
            Inert(true),
            Loading(provider.info),
            Summarizing(provider.info, parser.parse("# This is the article\n")),
            Summarizing(provider.info, parser.parse("# This is the article\nThis is some content...\n")),
            Summarizing(provider.info, parser.parse("# This is the article\nThis is some content...\nThis is some *bold* content.\n")),
            Summarized(provider.info, parser.parse("# This is the article\nThis is some content...\nThis is some *bold* content.\n")),
        )

        assertEquals(expected, states)
        assertEquals("${recipeInstructions("es")} $content", llm.promptCapture)
    }

    @Test
    fun `if extracting page metadata fails, the llm is prompted with the default instructions`() = runTest {
        val llm = FakeLlm.successful
        val content = "this is expected content."
        val provider = FakeCloudProvider(llm = llm)
        val store = SummarizationStore(
            initialState = Inert(true),
            reducer = ::summarizationReducer,
            middleware = listOf(
                SummarizationMiddleware(
                    settings = SummarizationSettings.inMemory(hasConsentedToShake = true),
                    llmProvider = provider,
                    pageContentExtractor = { Result.success(content) },
                    pageMetadataExtractor = { Result.failure(IllegalStateException()) },
                    errorReporter = noopReporter,
                    scope = backgroundScope,
                ),
            ),
        )

        val states = mutableListOf<SummarizationState>()
        backgroundScope.launch {
            store.stateFlow.toList(states)
        }
        testScheduler.advanceTimeBy(1.seconds)

        store.dispatch(ViewAppeared)
        testScheduler.advanceTimeBy(15.seconds)

        val expected = listOf<SummarizationState>(
            Inert(true),
            Loading(provider.info),
            Summarizing(provider.info, parser.parse("# This is the article\n")),
            Summarizing(provider.info, parser.parse("# This is the article\nThis is some content...\n")),
            Summarizing(provider.info, parser.parse("# This is the article\nThis is some content...\nThis is some *bold* content.\n")),
            Summarized(provider.info, parser.parse("# This is the article\nThis is some content...\nThis is some *bold* content.\n")),
        )

        assertEquals(expected, states)
        assertEquals("${defaultInstructions("en")} $content", llm.promptCapture)
    }
}
