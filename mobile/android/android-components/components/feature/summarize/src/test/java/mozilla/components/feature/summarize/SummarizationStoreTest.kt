/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize

import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.runTest
import mozilla.components.feature.summarize.SummarizationState.Finished
import mozilla.components.feature.summarize.SummarizationState.Inert
import mozilla.components.feature.summarize.SummarizationState.ShakeConsentRequired
import mozilla.components.feature.summarize.SummarizationState.Summarized
import mozilla.components.feature.summarize.SummarizationState.Summarizing
import mozilla.components.feature.summarize.fakes.FakeCloudProvider
import mozilla.components.feature.summarize.fakes.FakeLlm
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.time.Duration.Companion.seconds

class SummarizationStoreTest {
    @Test
    fun `test that we can consent to shake`() = runTest {
        val settings = SummarizationSettings.inMemory()

        val store = SummarizationStore(
            initialState = Inert(true),
            reducer = ::summarizationReducer,
            middleware = listOf(
                SummarizationMiddleware(
                    settings = settings,
                    llmProvider = FakeCloudProvider(llm = FakeLlm.successful),
                    scope = backgroundScope,
                ),
            ),
        )

        val states = mutableListOf<SummarizationState>()
        backgroundScope.launch {
            store.stateFlow.toList(states)
        }

        store.dispatch(ViewAppeared)
        store.dispatch(OffDeviceSummarizationShakeConsentAction.AllowClicked)

        testScheduler.advanceTimeBy(15.seconds)

        val expected = listOf<SummarizationState>(
            Inert(true),
            ShakeConsentRequired,
            Summarizing(listOf("# This is the article\n")),
            Summarizing(listOf("# This is the article\n", "This is some content...\n")),
            Summarizing(listOf("# This is the article\n", "This is some content...\n", "This is some *bold* content.\n")),
            Summarized("# This is the article\nThis is some content...\nThis is some *bold* content.\n"),
        )

        assertEquals(expected, states)
        assertTrue(settings.getHasConsentedToShake())
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
        assertFalse(settings.getHasConsentedToShake())
    }

    @Test
    fun `If a user has already consented to shake, test that we can prompt an llm`() = runTest {
        val store = SummarizationStore(
            initialState = Inert(true),
            reducer = ::summarizationReducer,
            middleware = listOf(
                SummarizationMiddleware(
                    settings = SummarizationSettings.inMemory(hasConsentedToShakeInitial = true),
                    llmProvider = FakeCloudProvider(llm = FakeLlm.successful),
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
            Summarizing(listOf("# This is the article\n")),
            Summarizing(listOf("# This is the article\n", "This is some content...\n")),
            Summarizing(listOf("# This is the article\n", "This is some content...\n", "This is some *bold* content.\n")),
            Summarized("# This is the article\nThis is some content...\nThis is some *bold* content.\n"),
        )

        assertEquals(expected, states)
    }
}
