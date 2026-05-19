/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize

import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.concept.llm.CloudLlmProvider
import mozilla.components.concept.llm.ErrorCode
import mozilla.components.concept.llm.Llm
import mozilla.components.concept.llm.Prompt
import mozilla.components.feature.summarize.SummarizationState.Error
import mozilla.components.feature.summarize.SummarizationState.Finished
import mozilla.components.feature.summarize.SummarizationState.Inert
import mozilla.components.feature.summarize.SummarizationState.Loading
import mozilla.components.feature.summarize.SummarizationState.ShakeConsentRequired
import mozilla.components.feature.summarize.SummarizationState.Summarized
import mozilla.components.feature.summarize.SummarizationState.Summarizing
import mozilla.components.feature.summarize.content.Content
import mozilla.components.feature.summarize.content.ContentProvider
import mozilla.components.feature.summarize.content.PageContentExtractor
import mozilla.components.feature.summarize.content.PageMetadata
import mozilla.components.feature.summarize.ext.defaultInstructions
import mozilla.components.feature.summarize.ext.recipeInstructions
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
    private val errorReporter = ErrorReporter { _, exception -> reportedErrors.add(exception) }
    private val noopReporter = ErrorReporter { _, _ -> }
    private val parser = Parser()

    @Before
    fun setUp() {
        reportedErrors.clear()
    }

    @Test
    fun `test that we can consent to shake`() = runTest {
        val settings = SummarizationSettings.inMemory()
        val provider = FakeCloudProvider(preparedState = CloudLlmProvider.State.Ready(FakeLlm.successful))
        val pageTitle = "Article Headline"
        val store = SummarizationStore(
            initialState = Inert(true),
            reducer = ::summarizationReducer,
            middleware = listOf(
                SummarizationMiddleware(
                    settings = settings,
                    llmProvider = provider,
                    contentProvider = { Result.success(Content(PageMetadata(pageTitle = pageTitle))) },
                    errorReporter = noopReporter,
                    scope = backgroundScope,
                    dispatcher = StandardTestDispatcher(testScheduler),
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
            Summarizing(provider.info, parser.parse("# $pageTitle\nThis is the article\n")),
            Summarizing(provider.info, parser.parse("# $pageTitle\nThis is the article\nThis is some content...\n")),
            Summarizing(provider.info, parser.parse("# $pageTitle\nThis is the article\nThis is some content...\nThis is some *bold* content.\n")),
            Summarized(provider.info, parser.parse("# $pageTitle\nThis is the article\nThis is some content...\nThis is some *bold* content.\n")),
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
                    llmProvider = FakeCloudProvider(preparedState = CloudLlmProvider.State.Ready(FakeLlm.successful)),
                    contentProvider = { Result.success(Content()) },
                    errorReporter = noopReporter,
                    scope = backgroundScope,
                    dispatcher = StandardTestDispatcher(testScheduler),
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
        val provider = FakeCloudProvider(preparedState = CloudLlmProvider.State.Ready(llm))
        val content = "this is expected content."
        val pageTitle = "Article Headline"
        val store = SummarizationStore(
            initialState = Inert(true),
            reducer = ::summarizationReducer,
            middleware = listOf(
                SummarizationMiddleware(
                    llmProvider = provider,
                    settings = SummarizationSettings.inMemory(hasConsentedToShake = true),
                    contentProvider = { Result.success(Content(PageMetadata(listOf("Article"), 0, "en", pageTitle = pageTitle), content)) },
                    errorReporter = noopReporter,
                    scope = backgroundScope,
                    dispatcher = StandardTestDispatcher(testScheduler),
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
            Summarizing(provider.info, parser.parse("# $pageTitle\nThis is the article\n")),
            Summarizing(provider.info, parser.parse("# $pageTitle\nThis is the article\nThis is some content...\n")),
            Summarizing(provider.info, parser.parse("# $pageTitle\nThis is the article\nThis is some content...\nThis is some *bold* content.\n")),
            Summarized(provider.info, parser.parse("# $pageTitle\nThis is the article\nThis is some content...\nThis is some *bold* content.\n")),
        )

        assertEquals(expected, states)
        assertEquals(Prompt(content, defaultInstructions("en")), llm.lastPrompt)
    }

    @Test
    fun `page language is forwarded to model for default case`() = runTest {
        val llm = FakeLlm.successful
        val provider = FakeCloudProvider(preparedState = CloudLlmProvider.State.Ready(llm))
        val content = "this is expected content."
        val pageTitle = "Article Headline"
        val language = "de"
        val store = SummarizationStore(
            initialState = Inert(true),
            reducer = ::summarizationReducer,
            middleware = listOf(
                SummarizationMiddleware(
                    llmProvider = provider,
                    settings = SummarizationSettings.inMemory(hasConsentedToShake = true),
                    contentProvider = { Result.success(Content(PageMetadata(listOf("Article"), 0, language, pageTitle = pageTitle), content)) },
                    errorReporter = noopReporter,
                    scope = backgroundScope,
                    dispatcher = StandardTestDispatcher(testScheduler),
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
            Summarizing(provider.info, parser.parse("# $pageTitle\nThis is the article\n")),
            Summarizing(provider.info, parser.parse("# $pageTitle\nThis is the article\nThis is some content...\n")),
            Summarizing(provider.info, parser.parse("# $pageTitle\nThis is the article\nThis is some content...\nThis is some *bold* content.\n")),
            Summarized(provider.info, parser.parse("# $pageTitle\nThis is the article\nThis is some content...\nThis is some *bold* content.\n")),
        )

        assertEquals(expected, states)
        assertEquals(Prompt(content, defaultInstructions(language)), llm.lastPrompt)
    }

    @Test
    fun `page language is forwarded to model for recipe case`() = runTest {
        val llm = FakeLlm.successful
        val provider = FakeCloudProvider(preparedState = CloudLlmProvider.State.Ready(llm))
        val content = "this is expected content."
        val pageTitle = "Article Headline"
        val language = "de"
        val store = SummarizationStore(
            initialState = Inert(true),
            reducer = ::summarizationReducer,
            middleware = listOf(
                SummarizationMiddleware(
                    llmProvider = provider,
                    settings = SummarizationSettings.inMemory(hasConsentedToShake = true),
                    contentProvider = { Result.success(Content(PageMetadata(listOf("Recipe"), 0, language, pageTitle = pageTitle), content)) },
                    errorReporter = noopReporter,
                    scope = backgroundScope,
                    dispatcher = StandardTestDispatcher(testScheduler),
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
            Summarizing(provider.info, parser.parse("# $pageTitle\nThis is the article\n")),
            Summarizing(provider.info, parser.parse("# $pageTitle\nThis is the article\nThis is some content...\n")),
            Summarizing(provider.info, parser.parse("# $pageTitle\nThis is the article\nThis is some content...\nThis is some *bold* content.\n")),
            Summarized(provider.info, parser.parse("# $pageTitle\nThis is the article\nThis is some content...\nThis is some *bold* content.\n")),
        )

        assertEquals(expected, states)
        assertEquals(Prompt(content, recipeInstructions(language)), llm.lastPrompt)
    }

    @Test
    fun `if the page extractor fails, the failure is forwarded as a summarization failure`() = runTest {
        val failureThrowable = PageContentExtractor.Exception(NullPointerException())
        val provider = FakeCloudProvider(preparedState = CloudLlmProvider.State.Ready(FakeLlm.successful))
        val store = SummarizationStore(
            initialState = Inert(true),
            reducer = ::summarizationReducer,
            middleware = listOf(
                SummarizationMiddleware(
                    llmProvider = provider,
                    settings = SummarizationSettings.inMemory(hasConsentedToShake = true),
                    contentProvider = { Result.failure(failureThrowable) },
                    errorReporter = errorReporter,
                    scope = backgroundScope,
                    dispatcher = StandardTestDispatcher(testScheduler),
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
    fun `if the page metadata indicates a recipe, the llm is prompted with the recipe instructions even if the content is readerable`() = runTest {
        val llm = FakeLlm.successful
        val content = "this is expected content."
        val provider = FakeCloudProvider(preparedState = CloudLlmProvider.State.Ready(llm))
        var usingReaderContent = true
        val pageTitle = "Article Headline"
        val store = SummarizationStore(
            initialState = Inert(true),
            reducer = ::summarizationReducer,
            middleware = listOf(
                SummarizationMiddleware(
                    settings = SummarizationSettings.inMemory(hasConsentedToShake = true),
                    llmProvider = provider,
                    contentProvider = ContentProvider.fromPage(
                        pageTitle = pageTitle,
                        pageMetadataExtractor = {
                            Result.success(PageMetadata(listOf("Recipe"), 0, "en", isReaderable = true, pageTitle = pageTitle))
                        },
                        pageContentExtractor = { options ->
                            usingReaderContent = options.shouldUseReaderModeContent
                            Result.success(content)
                        },
                    ),
                    errorReporter = noopReporter,
                    scope = backgroundScope,
                    dispatcher = StandardTestDispatcher(testScheduler),
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
            Summarizing(provider.info, parser.parse("# $pageTitle\nThis is the article\n")),
            Summarizing(provider.info, parser.parse("# $pageTitle\nThis is the article\nThis is some content...\n")),
            Summarizing(provider.info, parser.parse("# $pageTitle\nThis is the article\nThis is some content...\nThis is some *bold* content.\n")),
            Summarized(provider.info, parser.parse("# $pageTitle\nThis is the article\nThis is some content...\nThis is some *bold* content.\n")),
        )

        assertFalse(usingReaderContent)
        assertEquals(expected, states)
        assertEquals(Prompt(content, recipeInstructions("en")), llm.lastPrompt)
    }

    @Test
    fun `if the page metadata indicates readerable, we use readermode content`() = runTest {
        val llm = FakeLlm.successful
        val content = "this is expected content."
        val provider = FakeCloudProvider(preparedState = CloudLlmProvider.State.Ready(llm))
        val pageTitle = "Article Headline"
        var usingReaderContent = false
        val store = SummarizationStore(
            initialState = Inert(true),
            reducer = ::summarizationReducer,
            middleware = listOf(
                SummarizationMiddleware(
                    settings = SummarizationSettings.inMemory(hasConsentedToShake = true),
                    llmProvider = provider,
                    contentProvider = ContentProvider.fromPage(
                        pageTitle = pageTitle,
                        pageMetadataExtractor = {
                            Result.success(PageMetadata(listOf("Article"), 0, "en", isReaderable = true))
                        },
                        pageContentExtractor = { options ->
                            usingReaderContent = options.shouldUseReaderModeContent
                            Result.success(content)
                        },
                    ),
                    errorReporter = noopReporter,
                    scope = backgroundScope,
                    dispatcher = StandardTestDispatcher(testScheduler),
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
            Summarizing(provider.info, parser.parse("# $pageTitle\nThis is the article\n")),
            Summarizing(provider.info, parser.parse("# $pageTitle\nThis is the article\nThis is some content...\n")),
            Summarizing(provider.info, parser.parse("# $pageTitle\nThis is the article\nThis is some content...\nThis is some *bold* content.\n")),
            Summarized(provider.info, parser.parse("# $pageTitle\nThis is the article\nThis is some content...\nThis is some *bold* content.\n")),
        )

        assertTrue(usingReaderContent)
        assertEquals(expected, states)
        assertEquals(Prompt(content, defaultInstructions("en")), llm.lastPrompt)
    }

    @Test
    fun `dismissing an error screen transitions to the ErrorDismissed finished state`() = runTest {
        val failureThrowable = PageContentExtractor.Exception(NullPointerException())
        val provider = FakeCloudProvider(preparedState = CloudLlmProvider.State.Ready(FakeLlm.successful))
        val store = SummarizationStore(
            initialState = Inert(true),
            reducer = ::summarizationReducer,
            middleware = listOf(
                SummarizationMiddleware(
                    llmProvider = provider,
                    settings = SummarizationSettings.inMemory(hasConsentedToShake = true),
                    contentProvider = { Result.failure(failureThrowable) },
                    errorReporter = noopReporter,
                    scope = backgroundScope,
                ),
            ),
        )

        val states = mutableListOf<SummarizationState>()
        backgroundScope.launch {
            store.stateFlow.toList(states)
        }

        val errorScreenExpected = listOf(
            Inert(true),
            Loading(provider.info),
            Error(SummarizationError.SummarizationFailed(failureThrowable)),
        )

        store.dispatch(ViewAppeared)
        testScheduler.advanceTimeBy(15.seconds)

        assertEquals(errorScreenExpected, states)

        store.dispatch(ErrorAction.ErrorDismissed)
        testScheduler.advanceTimeBy(1.seconds)

        val expected = listOf(
            Inert(true),
            Loading(provider.info),
            Error(SummarizationError.SummarizationFailed(failureThrowable)),
            Finished.ErrorDismissed,
        )

        assertEquals(expected, states)
    }

    @Test
    fun `dismissing a content too long error screen transitions to the ErrorDismissed finished state`() = runTest {
        val contentTooLongException = Llm.Exception("Content too long", ErrorCode(1005))
        val provider = FakeCloudProvider(
            preparedState = CloudLlmProvider.State.Ready(
            llm = object : Llm {
                override suspend fun prompt(prompt: Prompt): Flow<String> = throw contentTooLongException
            },
        ),
        )
        val store = SummarizationStore(
            initialState = Inert(true),
            reducer = ::summarizationReducer,
            middleware = listOf(
                SummarizationMiddleware(
                    llmProvider = provider,
                    settings = SummarizationSettings.inMemory(hasConsentedToShake = true),
                    contentProvider = { Result.success(Content()) },
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

        assertEquals(
            listOf(Inert(true), Loading(provider.info), Error(SummarizationError.ContentTooLong)),
            states,
        )

        store.dispatch(ErrorAction.ErrorDismissed)
        testScheduler.advanceTimeBy(1.seconds)

        assertEquals(
            listOf(Inert(true), Loading(provider.info), Error(SummarizationError.ContentTooLong), Finished.ErrorDismissed),
            states,
        )
    }

    @Test
    fun `page metadata language is inserted into prompt`() = runTest {
        val llm = FakeLlm.successful
        val content = "this is expected content."
        val provider = FakeCloudProvider(preparedState = CloudLlmProvider.State.Ready(llm))
        val pageTitle = "Article Headline"
        val store = SummarizationStore(
            initialState = Inert(true),
            reducer = ::summarizationReducer,
            middleware = listOf(
                SummarizationMiddleware(
                    settings = SummarizationSettings.inMemory(hasConsentedToShake = true),
                    llmProvider = provider,
                    contentProvider = {
                        Result.success(
                            Content(
                                PageMetadata(
                                    listOf("Recipe"),
                                    0,
                                    "es",
                                    pageTitle = pageTitle,
                                ),
                                    content,
                            ),
                        )
                    },
                    errorReporter = noopReporter,
                    scope = backgroundScope,
                    dispatcher = StandardTestDispatcher(testScheduler),
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
            Summarizing(provider.info, parser.parse("# $pageTitle\nThis is the article\n")),
            Summarizing(provider.info, parser.parse("# $pageTitle\nThis is the article\nThis is some content...\n")),
            Summarizing(provider.info, parser.parse("# $pageTitle\nThis is the article\nThis is some content...\nThis is some *bold* content.\n")),
            Summarized(provider.info, parser.parse("# $pageTitle\nThis is the article\nThis is some content...\nThis is some *bold* content.\n")),
        )

        assertEquals(expected, states)
        assertEquals(Prompt(content, recipeInstructions("es")), llm.lastPrompt)
    }

    @Test
    fun `if a title is not provided, skip prepending a title line`() = runTest {
        val llm = FakeLlm.successful
        val provider = FakeCloudProvider(preparedState = CloudLlmProvider.State.Ready(llm))
        val content = "this is expected content."
        val store = SummarizationStore(
            initialState = Inert(true),
            reducer = ::summarizationReducer,
            middleware = listOf(
                SummarizationMiddleware(
                    llmProvider = provider,
                    settings = SummarizationSettings.inMemory(hasConsentedToShake = true),
                    contentProvider = { Result.success(Content(PageMetadata(listOf("Article"), 0, "en"), content)) },
                    errorReporter = noopReporter,
                    scope = backgroundScope,
                    dispatcher = StandardTestDispatcher(testScheduler),
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
            Summarizing(provider.info, parser.parse("This is the article\n")),
            Summarizing(provider.info, parser.parse("This is the article\nThis is some content...\n")),
            Summarizing(provider.info, parser.parse("This is the article\nThis is some content...\nThis is some *bold* content.\n")),
            Summarized(provider.info, parser.parse("This is the article\nThis is some content...\nThis is some *bold* content.\n")),
        )

        assertEquals(expected, states)
        assertEquals(Prompt(content, defaultInstructions("en")), llm.lastPrompt)
    }

    @OptIn(ExperimentalCoroutinesApi::class)
    @Test
    fun `llm provider errors are reported`() = runTest {
        val errorCode = ErrorCode(666)
        val message = "cloud not clouding"
        val exception = Llm.Exception(message, errorCode)
        val provider = FakeCloudProvider(preparedState = CloudLlmProvider.State.Unavailable(exception))
        val store = SummarizationStore(
            initialState = Inert(true),
            reducer = ::summarizationReducer,
            middleware = listOf(
                SummarizationMiddleware(
                    llmProvider = provider,
                    settings = SummarizationSettings.inMemory(hasConsentedToShake = true),
                    contentProvider = { Result.failure(IllegalStateException()) },
                    errorReporter = errorReporter,
                    scope = backgroundScope,
                    dispatcher = StandardTestDispatcher(testScheduler),
                ),
            ),
        )

        val states = mutableListOf<SummarizationState>()
        backgroundScope.launch {
            store.stateFlow.toList(states)
        }

        // we need to runCurrent here to allow the queued collection of the stateflow along with
        // various jobs launched by middleware to be processed
        store.dispatch(ViewAppeared)
        testScheduler.runCurrent()

        val expected = listOf<SummarizationState>(
            Inert(true),
            Loading(provider.info),
            Error(SummarizationError.SummarizationFailed(exception)),
        )
        assertEquals(expected, states)
    }
}
