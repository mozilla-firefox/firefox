/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.llm.harness

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.flatMapLatest
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.yield
import mozilla.components.concept.llm.CloudLlmProvider
import mozilla.components.concept.llm.Llm
import mozilla.components.concept.llm.LlmFailoverStrategy
import mozilla.components.concept.llm.LlmPreparationStrategy
import mozilla.components.concept.llm.LlmProvider
import mozilla.components.concept.llm.LlmSession
import mozilla.components.concept.llm.LlmSessionConfig
import mozilla.components.concept.llm.LocalLlmProvider

internal class LlmSessionImpl(private val config: LlmSessionConfig) : LlmSession {

    private val historyMutex = Mutex()
    private val history = mutableListOf<Llm.Message>()

    private val _activeProvider = MutableStateFlow(config.picker.pick(config.providers))
    override val activeProvider: StateFlow<LlmProvider> = _activeProvider.asStateFlow()

    override fun launchIn(scope: CoroutineScope) {
        val providersToPrepare = when (config.preparationStrategy) {
            LlmPreparationStrategy.ActiveOnly -> listOf(_activeProvider.value)
            LlmPreparationStrategy.All -> config.providers
        }
        providersToPrepare.forEach { scope.launch { prepareProvider(it) } }
    }

    override suspend fun send(message: String): Flow<String> {
        val llm = getLlm()
        yield()

        return flow {
            val historySnapshot = historyMutex.withLock {
                val size = history.size
                history.add(Llm.Message.User(message))
                size
            }

            try {
                var turnResult = llm.prompt(buildContextWindow(llm))
                var rounds = 0

                while (true) {
                    when (val result = turnResult) {
                        is Llm.LlmTurnResult.ToolCalls -> {
                            if (rounds >= config.maxToolRounds) {
                                throw Llm.Exception.unknown("Max tool rounds exceeded")
                            }
                            historyMutex.withLock {
                                history.add(Llm.Message.AssistantToolCall(result.calls))
                            }
                            for (call in result.calls) {
                                val tool = config.tools.find { it.name == call.toolName }
                                    ?: throw Llm.Exception.unknown("Unknown tool: ${call.toolName}")
                                val toolResult = tool.execute(call.arguments)
                                historyMutex.withLock {
                                    history.add(Llm.Message.Tool(call.id, toolResult))
                                }
                            }
                            rounds++
                            turnResult = llm.prompt(buildContextWindow(llm))
                        }
                        is Llm.LlmTurnResult.Text -> {
                            val responseBuilder = StringBuilder()
                            result.flow.collect { token ->
                                responseBuilder.append(token)
                                emit(token)
                            }
                            historyMutex.withLock {
                                history.add(Llm.Message.Assistant(responseBuilder.toString()))
                            }
                            break
                        }
                    }
                }
            } catch (e: Throwable) {
                historyMutex.withLock {
                    while (history.size > historySnapshot) history.removeLastOrNull()
                }
                throw e
            }
        }
    }

    override suspend fun switchProvider(provider: LlmProvider): Boolean {
        require(provider in config.providers) { "Provider not in this session's provider list" }

        if (isUnavailable(provider)) return false

        if (config.preparationStrategy == LlmPreparationStrategy.ActiveOnly) {
            prepareProvider(provider)
        }

        return if (getLlmForProvider(provider) != null) {
            _activeProvider.value = provider
            true
        } else {
            false
        }
    }

    @OptIn(ExperimentalCoroutinesApi::class)
    private suspend fun getLlm(): Llm =
        awaitLlm(_activeProvider.value) ?: when (config.failoverStrategy) {
            LlmFailoverStrategy.FailFast ->
                throw Llm.Exception.unknown("No LLM provider is available")
            LlmFailoverStrategy.AutoFailover, LlmFailoverStrategy.ManualFailover ->
                _activeProvider
                    .flatMapLatest { provider -> flow { emit(awaitLlm(provider)) } }
                    .filterNotNull()
                    .first()
        }

    private suspend fun awaitLlm(provider: LlmProvider): Llm? = when (provider) {
        is CloudLlmProvider -> provider.state
            .first { it is CloudLlmProvider.State.Ready || it is CloudLlmProvider.State.Unavailable }
            .let { (it as? CloudLlmProvider.State.Ready)?.llm }
        is LocalLlmProvider -> provider.state
            .first { it is LocalLlmProvider.State.Ready || it is LocalLlmProvider.State.Unavailable || it is LocalLlmProvider.State.Failed }
            .let { (it as? LocalLlmProvider.State.Ready)?.llm }
    }

    private suspend fun buildContextWindow(llm: Llm): Llm.ContextWindow {
        val messages = historyMutex.withLock {
            buildList {
                if (config.systemPrompt.isNotEmpty()) add(Llm.Message.System(config.systemPrompt))
                addAll(history)
            }
        }
        return config.contextWindowStrategy.trim(Llm.ContextWindow(messages, config.tools), llm)
    }

    private suspend fun prepareProvider(provider: LlmProvider) {
        when (provider) {
            is CloudLlmProvider -> provider.prepare()
            is LocalLlmProvider -> provider.downloadIfNeeded()
        }
    }

    private fun isUnavailable(provider: LlmProvider): Boolean = when (provider) {
        is CloudLlmProvider -> provider.state.value is CloudLlmProvider.State.Unavailable
        is LocalLlmProvider -> provider.state.value is LocalLlmProvider.State.Unavailable ||
            provider.state.value is LocalLlmProvider.State.Failed
    }

    private fun getLlmForProvider(provider: LlmProvider): Llm? = when (provider) {
        is CloudLlmProvider -> (provider.state.value as? CloudLlmProvider.State.Ready)?.llm
        is LocalLlmProvider -> (provider.state.value as? LocalLlmProvider.State.Ready)?.llm
    }
}
