/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize.fakes

import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.flow
import mozilla.components.concept.llm.CloudLlmProvider
import mozilla.components.concept.llm.Llm
import mozilla.components.concept.llm.LlmProvider
import mozilla.components.concept.llm.LocalLlmProvider
import mozilla.components.concept.llm.Prompt
import mozilla.components.feature.summarize.R
import kotlin.time.Duration.Companion.seconds

/**
 * A fake implementation of [CloudLlmProvider] for use in tests and Compose previews.
 *
 * @property state The mutable state flow representing the current provider state.
 * Defaults to [CloudLlmProvider.State.Available].
 * @property llm The [Llm] instance to transition to when [prepare] is called.
 */
data class FakeCloudProvider(
    override val state: MutableStateFlow<CloudLlmProvider.State> = MutableStateFlow(CloudLlmProvider.State.Available),
    val llm: Llm,
) : CloudLlmProvider {
    override val info = LlmProvider.Info(nameRes = R.string.mozac_summarize_fake_llm_name)

    override suspend fun prepare() {
        state.value = CloudLlmProvider.State.Ready(llm)
    }
}

/**
 * A fake implementation of [Llm] for use in tests and Compose previews.
 *
 * Emits each item in [responses] sequentially, with a 2-second delay between
 * each emission to simulate real LLM streaming latency.
 *
 * @property responses values to emit.
 */
data class FakeLlm(
    val responses: List<String> = listOf(),
) : Llm {

    var promptCapture = ""

    override suspend fun prompt(prompt: Prompt): Flow<String> = flow {
        for (response in responses) {
            emit(response)
            delay(2.seconds)
        }
    }.also {
        promptCapture = prompt.value
    }

    companion object {
        val successful get() = FakeLlm(
            listOf(
               "# This is the article\n",
               "This is some content...\n",
               "This is some *bold* content.\n",
            ),
        )
    }
}

internal data class FakeLocalProvider(
    override val state: MutableStateFlow<LocalLlmProvider.State> = MutableStateFlow(
        LocalLlmProvider.State.ReadyToDownload,
    ),
    val llm: Llm,
) : LocalLlmProvider {
    override val info = LlmProvider.Info(nameRes = R.string.mozac_summarize_fake_llm_name)

    override suspend fun downloadIfNeeded() {
        state.value = LocalLlmProvider.State.Downloading(TOTAL_SIZE, INITIAL_SIZE)
        delay(0.5.seconds)
        state.value = LocalLlmProvider.State.Downloading(TOTAL_SIZE, PARTIAL_SIZE)
        delay(1.seconds)
        state.value = LocalLlmProvider.State.Ready(llm)
    }
}

private const val INITIAL_SIZE = 0L
private const val PARTIAL_SIZE = 5_000L
private const val TOTAL_SIZE = 10_000L
