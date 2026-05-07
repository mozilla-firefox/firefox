# LLM Follow-up Question: Implementation Plan

This plan covers two parallel goals:
1. Cleaning up and properly landing the LLM library changes prototyped in `aip-llm-context`.
2. Adding a single follow-up question to the shake-to-summarize flow using those libraries.

Execute these in dependency order. Steps 1–4 are library-only and have no visible product change. Steps 5–6 are the Fenix wiring cleanup and the feature change respectively.

---

## Step 1: `concept-llm` — Tool calling and session types

This step lands all new types needed by the harness and callers. Nothing in this step requires the harness or MLPA to exist.

### `Llm.kt`

Extend `ContextWindow` with two new fields (both have defaults so existing callers compile unchanged):

```kotlin
data class ContextWindow(
    val messages: List<Message>,
    // Tools available to the model this turn. Empty means no tool calling for this request.
    val tools: List<LlmTool> = emptyList(),
    // Controls whether the model must call a tool, may call one, or must not.
    // The harness sets this per-turn; callers that don't use tools can ignore it.
    val toolChoice: ToolChoice = ToolChoice.Auto,
)
```

Extend `Message` with two new variants. The existing `System`, `User`, and `Assistant` variants cover single-turn text conversation. Tool calling requires two more turns in the conversation history:

```kotlin
// Represents a model turn where the model requested tool calls instead of producing text.
// The harness appends this to history after receiving Llm.TurnResult.ToolCalls, so the
// model sees its own tool requests when it produces the final text response.
data class AssistantToolCall(val calls: List<ToolCall>) : Message() {
    override val message: String = ""
}
// Represents the result of executing a tool call, fed back to the model.
// toolCallId links this result to the specific ToolCall that requested it.
data class Tool(val toolCallId: String, override val message: String) : Message()
```

Replace `LlmTurnResult` with flat event variants and change `prompt()` to return a `Flow<TurnResult>` rather than a `suspend` function returning a sealed class. The old design nested a `Flow<String>` inside `Text`, which required a channel-based peek in `MlpaLlm` to determine response type before returning. The flat flow design lets `MlpaLlm` simply map `TurnEvent` → `TurnResult` with no channel, while the harness collects the flow once and handles both cases in a single loop:

```kotlin
sealed class TurnResult {
    // One token from a text response — emitted progressively as the model streams.
    data class TextDelta(val text: String) : TurnResult()
    // The model requested tool calls instead of producing text. The harness executes them,
    // appends the results to history, and calls prompt() again to get the text response.
    data class ToolCalls(val calls: List<ToolCall>) : TurnResult()
}
```

`prompt()` becomes a non-suspending function:

```kotlin
fun prompt(contextWindow: ContextWindow): Flow<TurnResult>
```

Add `ToolCall` — the structured record the model returns when requesting a tool invocation:

```kotlin
// id correlates this call with its Tool message result when the harness feeds it back.
data class ToolCall(val id: String, val toolName: String, val arguments: String)
```

### New file: `ToolChoice.kt`

`ToolChoice` lives in `concept-llm` rather than `lib-llm-mlpa` so that any `Llm` implementation can read it from `ContextWindow`. Backends that don't support the concept (e.g. Gemini Nano) simply ignore the field.

```kotlin
enum class ToolChoice {
    Auto,     // model decides whether to call a tool
    Required, // model must call a tool this turn — used by the harness on the first tool turn
    None,     // model must not call any tools
}
```

### New file: `LlmTool.kt`

The interface a feature registers at session construction time. The harness calls `execute()` when the model requests the tool, hiding the round-trip from consumers of `LlmSession.send()`.

```kotlin
interface LlmTool {
    val name: String
    val description: String
    // JSON Schema string — the model generates arguments conforming to this schema.
    val parametersSchema: String
    suspend fun execute(arguments: String): String
}
```

### New file: `LlmSession.kt`

The public-facing API that feature modules interact with. Hides provider state management, context window trimming, failover, and the tool-call loop from consumers. `companion object` is intentionally empty — `LlmSession.create()` is defined as an extension in `lib-llm-harness` to keep `concept-llm` free of implementation dependencies.

Configuration types are nested here rather than top-level because they are only meaningful in the context of constructing a session.

```kotlin
interface LlmSession {
    companion object

    data class Config(
        val providers: List<LlmProvider>,
        val picker: Picker = Picker.Default,
        val systemPrompt: String = "",
        val tools: List<LlmTool> = emptyList(),
        val maxToolRounds: Int = 5,
        val contextWindowStrategy: ContextWindowStrategy = ContextWindowStrategy.Default,
        val failoverStrategy: FailoverStrategy = FailoverStrategy.AutoFailover,
        val preparationStrategy: PreparationStrategy = PreparationStrategy.ActiveOnly,
    )

    sealed interface FailoverStrategy {
        // Re-runs Picker over remaining providers and waits for one to become ready.
        data object AutoFailover : FailoverStrategy
        // Surfaces the failure immediately as a flow exception. No switching attempted.
        data object FailFast : FailoverStrategy
        // Suspends pending send() calls until the consumer calls switchProvider() explicitly.
        data object ManualFailover : FailoverStrategy
    }

    sealed interface PreparationStrategy {
        // Only prepares the initially active provider at launchIn time.
        data object ActiveOnly : PreparationStrategy
        // Prepares all configured providers at launchIn time — useful when you want
        // multiple providers warm before the first send().
        data object All : PreparationStrategy
    }

    fun interface Picker {
        fun pick(providers: List<LlmProvider>): LlmProvider
        companion object {
            val Default: Picker = Picker { it.first() }
        }
    }

    val activeProvider: StateFlow<LlmProvider>
    suspend fun send(message: String): Flow<String>
    fun launchIn(scope: CoroutineScope)
    suspend fun switchProvider(provider: LlmProvider): Boolean
}
```

### New file: `LlmProvider.kt`

Separates on-device (download required) and cloud (auth/prepare required) provider lifecycles. The sealed `State` hierarchies differ because local providers have a download phase that cloud providers don't.

```kotlin
sealed interface LlmProvider {
    data class Info(val nameRes: Int, val iconRes: Int? = null)
    val info: Info
}

interface CloudLlmProvider : LlmProvider {
    sealed interface State {
        // Reachable but not yet initialised — call prepare() to move to Ready.
        object Available : State
        data class Unavailable(val exception: Llm.Exception) : State
        @JvmInline value class Ready(val llm: Llm) : State
    }
    val state: StateFlow<State>
    suspend fun prepare()
}

interface LocalLlmProvider : LlmProvider {
    sealed interface State {
        object Idle : State
        object Unavailable : State
        // Model is present on disk and ready for inference.
        @JvmInline value class Ready(val llm: Llm) : State
        object ReadyToDownload : State
        data class Downloading(val bytesToDownload: Long, val bytesDownloaded: Long) : State
        object Failed : State
    }
    val state: StateFlow<State>
    // No-op if the model is already present locally.
    suspend fun downloadIfNeeded()
}
```

### New file: `ContextWindowStrategy.kt`

Invoked by the harness before every `Llm.prompt()` call to ensure the context fits within the model's token limit. The `Default` implementation preserves system messages and drops oldest conversation turns first.

Note: when constructing trimmed `ContextWindow` instances inside `Default`, forward `toolChoice` from the original so the harness's tool-use intent is not lost during trimming.

```kotlin
interface ContextWindowStrategy {
    suspend fun trim(contextWindow: Llm.ContextWindow, llm: Llm): Llm.ContextWindow

    companion object {
        val Default: ContextWindowStrategy = object : ContextWindowStrategy {
            override suspend fun trim(contextWindow: Llm.ContextWindow, llm: Llm): Llm.ContextWindow {
                if (llm.countTokens(contextWindow) <= llm.contextWindowSize) return contextWindow

                val systemMessages = contextWindow.messages.filterIsInstance<Llm.Message.System>()
                val conversation = contextWindow.messages
                    .filter { it !is Llm.Message.System }
                    .toMutableList()

                while (conversation.isNotEmpty()) {
                    conversation.removeAt(0)
                    val candidate = Llm.ContextWindow(
                        systemMessages + conversation,
                        contextWindow.tools,
                        contextWindow.toolChoice,  // preserve harness intent
                    )
                    if (llm.countTokens(candidate) <= llm.contextWindowSize) return candidate
                }
                return Llm.ContextWindow(systemMessages, contextWindow.tools, contextWindow.toolChoice)
            }
        }
    }
}
```

---

## Step 2: `lib-llm-gemininano`

`GeminiNanoLlm` needs to update its `prompt()` signature from `suspend fun` to `fun`, and return `Flow<Llm.TurnResult>` wrapping its text tokens as `TextDelta` events:

```kotlin
override fun prompt(contextWindow: Llm.ContextWindow): Flow<Llm.TurnResult> =
    existingTextFlow(contextWindow).map { Llm.TurnResult.TextDelta(it) }
```

`toolChoice` and `tools` on `ContextWindow` can be ignored — Gemini Nano does not support tool calling. Update `GeminiNanoLlmTest` to match the new return type; default `ContextWindow` values mean construction call sites compile unchanged.

---

## Step 3: `lib-llm-harness`

### `LlmSessionImpl.kt` (new file, internal)

Full implementation. The only change from the prototype is in `buildContextWindow()`: it now sets `toolChoice` on the `ContextWindow` based on whether any tool results already exist in history, rather than leaving that logic in `MlpaLlm`.

```kotlin
internal class LlmSessionImpl(private val config: LlmSession.Config) : LlmSession {

    private val historyMutex = Mutex()
    private val history = mutableListOf<Llm.Message>()

    private val _activeProvider = MutableStateFlow(config.picker.pick(config.providers))
    override val activeProvider: StateFlow<LlmProvider> = _activeProvider.asStateFlow()

    override fun launchIn(scope: CoroutineScope) {
        val providersToPrepare = when (config.preparationStrategy) {
            LlmSession.PreparationStrategy.ActiveOnly -> listOf(_activeProvider.value)
            LlmSession.PreparationStrategy.All -> config.providers
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
                var contextWindow = buildContextWindow(llm)
                var rounds = 0
                var done = false

                while (!done) {
                    val pendingToolCalls = mutableListOf<Llm.ToolCall>()
                    val responseBuilder = StringBuilder()

                    llm.prompt(contextWindow).collect { result ->
                        when (result) {
                            is Llm.TurnResult.TextDelta -> {
                                done = true
                                responseBuilder.append(result.text)
                                emit(result.text)
                            }
                            is Llm.TurnResult.ToolCalls -> {
                                pendingToolCalls.addAll(result.calls)
                            }
                        }
                    }

                    if (!done) {
                        if (rounds >= config.maxToolRounds) {
                            throw Llm.Exception.unknown("Max tool rounds exceeded")
                        }
                        historyMutex.withLock {
                            history.add(Llm.Message.AssistantToolCall(pendingToolCalls))
                        }
                        for (call in pendingToolCalls) {
                            val tool = config.tools.find { it.name == call.toolName }
                                ?: throw Llm.Exception.unknown("Unknown tool: ${call.toolName}")
                            val toolResult = tool.execute(call.arguments)
                            historyMutex.withLock {
                                history.add(Llm.Message.Tool(call.id, toolResult))
                            }
                        }
                        rounds++
                        contextWindow = buildContextWindow(llm)
                    } else {
                        historyMutex.withLock {
                            history.add(Llm.Message.Assistant(responseBuilder.toString()))
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

        if (config.preparationStrategy == LlmSession.PreparationStrategy.ActiveOnly) {
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
            LlmSession.FailoverStrategy.FailFast ->
                throw Llm.Exception.unknown("No LLM provider is available")
            LlmSession.FailoverStrategy.AutoFailover, LlmSession.FailoverStrategy.ManualFailover ->
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
        // Required on the first tool turn so the model calls the tool rather than answering
        // from training data. Auto thereafter so the model can produce text once it has results.
        val toolChoice = if (config.tools.isNotEmpty() && history.none { it is Llm.Message.Tool }) {
            ToolChoice.Required
        } else {
            ToolChoice.Auto
        }
        return config.contextWindowStrategy.trim(
            Llm.ContextWindow(messages, config.tools, toolChoice),
            llm,
        )
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
```

### `LlmSessionExt.kt` (new file)

```kotlin
fun LlmSession.Companion.create(config: LlmSession.Config): LlmSession = LlmSessionImpl(config)
```

---

## Step 4: `lib-llm-mlpa`

This is the largest step. Three separate concerns, best done as one atomic change.

### 4a. Collapse `ChatService.completion` and `completionWithTools`

Replace both methods with a single `completion()` that returns `Flow<TurnEvent>`:

**`MlpaService.kt`** — add `TurnEvent`, remove `completionWithTools`, `ToolAwareResponse`, and the `ToolCallRecord` from the public interface (it becomes an internal detail of `FetchClientMlpaService`):

```kotlin
interface ChatService {
    fun completion(authorizationToken: AuthorizationToken, request: Request): Flow<TurnEvent>

    sealed class TurnEvent {
        data class TextDelta(val text: String) : TurnEvent()
        data class ToolCalls(val calls: List<ToolCallRecord>) : TurnEvent()
    }

    data class ToolCallRecord(val id: String, val toolName: String, val arguments: String)
    // ... (ResponseErrorCode, ResponseErrorReason, Response, Request all unchanged)
}
```

**`Response.kt`** — merge `contentFlow` and `toolAwareStreamResponse` into a single `turnEventFlow: Flow<TurnEvent>` extension on `Response`.

Emission contract:
- **Text responses**: emit one `TurnEvent.TextDelta` per SSE event as they arrive — do not buffer.
- **Tool call responses**: accumulate all `tool_calls` deltas internally (as `toolAwareStreamResponse` already does) and emit a single `TurnEvent.ToolCalls` once the stream ends.

A given SSE stream will contain either content deltas or tool_calls deltas, never both. Use the first non-empty field in the first event to branch early rather than checking every event.

**`FetchClientMlpaService.kt`**:
- Replace `completion()` and `completionWithTools()` with a single `override fun completion()` returning `Flow<TurnEvent>`.
- The existing `flow { }.flowOn(dispatcher)` pattern is preserved.
- Remove all `Log.e` debug statements.

**`MlpaLlm.kt`** — `prompt()` is now a simple non-suspending map from `Flow<TurnEvent>` to `Flow<Llm.TurnResult>`. No channel required:

```kotlin
override fun prompt(contextWindow: Llm.ContextWindow): Flow<Llm.TurnResult> =
    chatService.completion(authorizationToken, contextWindow.asRequest).map { event ->
        when (event) {
            is ChatService.TurnEvent.TextDelta ->
                Llm.TurnResult.TextDelta(event.text)
            is ChatService.TurnEvent.ToolCalls ->
                Llm.TurnResult.ToolCalls(
                    event.calls.map { Llm.ToolCall(it.id, it.toolName, it.arguments) },
                )
        }
    }
```

`asRequest` reads `toolChoice` directly from `ContextWindow` instead of inspecting message history:

```kotlin
toolChoice = when {
    tools.isEmpty() -> null
    else -> when (contextWindow.toolChoice) {
        ToolChoice.Required -> "required"
        ToolChoice.Auto -> "auto"
        ToolChoice.None -> "none"
    }
},
```

**Fakes (`Fakes.kt`)** — update `successChatService`, `failureChatService`, `invalidTokenService` to return `Flow<TurnEvent>` instead of `Flow<String>`:

```kotlin
val successChatService = object : ChatService {
    override fun completion(authorizationToken: AuthorizationToken, request: ChatService.Request) =
        listOf(ChatService.TurnEvent.TextDelta("Hello World!")).asFlow()
}

val failureChatService = object : ChatService {
    override fun completion(authorizationToken: AuthorizationToken, request: ChatService.Request) =
        flow<ChatService.TurnEvent> { throw IllegalStateException("Bad response!") }
}

val invalidTokenService = object : ChatService {
    override fun completion(authorizationToken: AuthorizationToken, request: ChatService.Request) =
        flow<ChatService.TurnEvent> { throw ChatServiceError.InvalidToken() }
}
```

### 4b. Fix null serialization

In `FetchClientMlpaService`, add `explicitNulls = false` to the `Json` builder:

```kotlin
private val json by lazy {
    Json {
        ignoreUnknownKeys = true
        encodeDefaults = true
        explicitNulls = false
    }
}
```

Remove all `@EncodeDefault(EncodeDefault.Mode.NEVER)` annotations from `ChatService.Request.Message` and `ChatService.Request` in `MlpaService.kt`. They are no longer needed.

Add a test asserting that a `Message` with `role = Assistant` and `null` content serializes without a `"content"` key.

---

## Step 5: Fenix — Remove `FenixMlpaService`

`FenixMlpaService` was a temporary proxy for toggling between prod and non-prod MLPA environments. With `ChatService` now having a single method, leaving this proxy in place is a maintenance risk.

**`Llm.kt`** — replace the `fenixMlpaService` lazy property and `mlpaProvider` construction:

```kotlin
// Before
val fenixMlpaService by lazyMonitored { FenixMlpaService(client) }
val mlpaProvider: MlpaLlmProvider by lazyMonitored {
    MlpaLlmProvider(..., mlpaService = fenixMlpaService)
}

// After
private val mlpaService by lazyMonitored {
    FetchClientMlpaService(client, /* select MlpaConfig based on previous useProd toggle */)
}
val mlpaProvider: MlpaLlmProvider by lazyMonitored {
    MlpaLlmProvider(..., mlpaService = mlpaService)
}
```

The `useProd` flag in `FenixMlpaService` defaulted to `true`. Replicate the same selection logic (build config flag, settings toggle, or hardcoded prod) directly at the `FetchClientMlpaService` construction site.

**Delete `FenixMlpaService.kt`**.

---

## Step 6: `feature-summarize` — `LlmSession` adoption and follow-up question

### 6a. Dependencies

Add `lib-llm-harness` to `feature-summarize/build.gradle`:

```groovy
implementation project(':lib-llm-harness')
```

### 6b. New and removed actions (`SummarizationAction.kt`)

Remove:
- `LlmProviderAction` sealed interface and its three variants (`ProviderAvailable`, `ProviderInitialized`, `ProviderFailed`)

Add:
```kotlin
data class FollowUpTextChanged(val text: String) : SummarizationAction
data object FollowUpSubmitted : SummarizationAction
data class ReceivedFollowUpDocument(val document: RichDocument) : SummarizationAction
data object FollowUpCompleted : SummarizationAction
```

### 6c. New and changed states (`SummarizationState.kt`)

Remove `Summarized`.

Replace with:
```kotlin
data class AwaitingFollowUp(
    val info: LlmProvider.Info,
    val document: RichDocument,
    val userFollowUp: String = "",
) : SummarizationState()

data class RespondingToFollowUp(
    val info: LlmProvider.Info,
    val summary: RichDocument,
    val followUpResponse: RichDocument = RichDocument(listOf()),
) : SummarizationState()

data class FollowUpComplete(
    val info: LlmProvider.Info,
    val summary: RichDocument,
    val followUp: RichDocument,
) : SummarizationState()
```

`Settings` state currently transitions to and from `Summarized`. Update it to use `AwaitingFollowUp` instead — on `SettingsBackClicked`, construct `AwaitingFollowUp(info, document)` with an empty `userFollowUp`.

`Loading` and `Summarizing` states are unchanged.

### 6d. Reducer (`SummarizationReducer.kt`)

Remove the `LlmProviderAction.ProviderFailed` branch.

Update `SettingsClicked` to match on `AwaitingFollowUp` instead of `Summarized`. Update `SettingsBackClicked` to return `AwaitingFollowUp`.

Update `complete()`:
```kotlin
private fun SummarizationState.complete(): SummarizationState {
    if (this !is SummarizationState.Summarizing) return this
    return SummarizationState.AwaitingFollowUp(info, document)
}
```

Add follow-up transitions:
```kotlin
is FollowUpTextChanged -> when (state) {
    is SummarizationState.AwaitingFollowUp -> state.copy(userFollowUp = action.text)
    else -> state
}
is FollowUpSubmitted -> when (state) {
    is SummarizationState.AwaitingFollowUp ->
        SummarizationState.RespondingToFollowUp(state.info, state.document)
    else -> state
}
is ReceivedFollowUpDocument -> when (state) {
    is SummarizationState.RespondingToFollowUp -> state.copy(followUpResponse = action.document)
    else -> state
}
is FollowUpCompleted -> when (state) {
    is SummarizationState.RespondingToFollowUp ->
        SummarizationState.FollowUpComplete(state.info, state.summary, state.followUpResponse)
    else -> state
}
```

Update `updateDocument` to no longer need to handle `Summarized`.

### 6e. Middleware (`SummarizationMiddleware.kt`)

Remove `observeCloudLlmProvider` and all `LlmProviderAction` handling.

Add a session field:
```kotlin
private var session: LlmSession? = null
```

Replace `observePrompt` with two new private functions:

**`runSummarization`** — called from `ViewAppeared` (after consent check) and `AllowClicked`:
```kotlin
private suspend fun runSummarization(store: SummarizationStore) = runCatching {
    val content = contentProvider.getContent().getOrThrow()
    store.dispatch(ContentExtracted(content))

    val newSession = LlmSession.create(
        LlmSession.Config(
            providers = listOf(llmProvider),
            systemPrompt = content.metadata.systemPrompt,
        ),
    )
    session = newSession
    newSession.launchIn(scope)

    store.dispatch(SummarizationRequested(llmProvider.info))

    newSession.send(content.body)
        .mapToRichDocument(pageTitle = content.metadata.pageTitle, dispatcher = dispatcher)
        .onCompletion { if (it == null) store.dispatch(SummarizationCompleted) }
        .collect { store.dispatch(ReceivedParsedDocument(it)) }
}.onFailure { store.dispatch(SummarizationFailed(it)) }
```

**`runFollowUp`** — called from `FollowUpSubmitted`:
```kotlin
private suspend fun runFollowUp(store: SummarizationStore, question: String) = runCatching {
    val currentSession = session ?: return@runCatching

    currentSession.send(question)
        .mapToRichDocument(dispatcher = dispatcher)
        .onCompletion { if (it == null) store.dispatch(FollowUpCompleted) }
        .collect { store.dispatch(ReceivedFollowUpDocument(it)) }
}.onFailure { store.dispatch(SummarizationFailed(it)) }
```

Update `invoke` to handle `FollowUpSubmitted` and clear the session on `ViewDismissed`:
```kotlin
is FollowUpSubmitted -> scope.launch {
    val question = (store.state as? SummarizationState.AwaitingFollowUp)?.userFollowUp ?: return@launch
    runFollowUp(store, question)
}
ViewDismissed -> session = null
```

### 6f. `ext/Content.kt`

The `Content.prompt` extension (which built a `ContextWindow` for direct `llm.prompt()` calls) is no longer used. Remove it.

The `metadata.systemPrompt` private extension is still needed by the middleware. Make it `internal` so `SummarizationMiddleware` can access it.

### 6g. UI (`SummarizationScreen.kt`)

Add handling for three state changes:

- **`AwaitingFollowUp`**: render the `RichDocument` summary (existing `SummarizedContent` composable) followed by a text input field bound to `userFollowUp`, with a submit button that dispatches `FollowUpSubmitted` and a text change handler that dispatches `FollowUpTextChanged`.
- **`RespondingToFollowUp`**: render the summary, then a loading/streaming indicator as `followUpResponse` updates via `ReceivedFollowUpDocument`.
- **`FollowUpComplete`**: render the summary `RichDocument`, a visual separator, then the follow-up `RichDocument`.

The settings gear icon should be hidden (or disabled) in `RespondingToFollowUp` and `FollowUpComplete` states.

---

## Testing notes

- `SummarizationStoreTest`: update all existing `Summarized` references to `AwaitingFollowUp`; add tests for the follow-up state machine transitions (`FollowUpTextChanged` → `FollowUpSubmitted` → `ReceivedFollowUpDocument` → `FollowUpCompleted`).
- `SummarizationMiddlewareTest` (if it exists) or new middleware test: mock `ContentProvider`, `LlmSession`, verify `runSummarization` dispatches the right actions in order; verify `runFollowUp` uses the same session; verify `ViewDismissed` clears the session.
- `MlpaLlmProviderTest` / `MlpaLlmTest`: update fakes for the `Flow<TurnEvent>` signature.
- `FetchClientMlpaServiceTest`: add a null serialization test (see Step 4b).

---

## Out of scope

- Converting `feature-summarize` to use tool calls (metadata extraction via tools is a future concern).
- Multiple follow-up rounds.
- UI polish for the follow-up screen.
- `FollowUpComplete` settings navigation (the settings gear is hidden in that state for now).
