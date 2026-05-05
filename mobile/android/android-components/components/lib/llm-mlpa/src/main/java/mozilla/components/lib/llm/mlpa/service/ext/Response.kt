/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.llm.mlpa.service.ext

import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.channelFlow
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.flow.filterNot
import kotlinx.coroutines.flow.map
import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable
import kotlinx.serialization.SerializationException
import kotlinx.serialization.json.Json
import mozilla.components.concept.fetch.Response
import mozilla.components.lib.llm.mlpa.service.ChatService
import mozilla.components.lib.llm.mlpa.service.ChatServiceError

private const val DATA_PREFIX = "data: "
private const val END_OF_STREAM_MARKER = "[DONE]"

/**
 * A [Flow] of content strings parsed from a server-sent events (SSE) stream in this [Response].
 *
 * Lines are filtered, stripped of the `data: ` prefix, deserialized as [Event] objects, and
 * mapped to their text content.
 */
internal val Response.contentFlow: Flow<String> get() = lineFlow
        .filterNot { it.isEmpty() || it.contains(END_OF_STREAM_MARKER) }
        .map { it.drop(DATA_PREFIX.length) }
        .events()
        .content()

/**
 * Reads the full SSE stream and returns a [ChatService.ToolAwareResponse].
 *
 * Text deltas are accumulated into a plain-text response; tool-call deltas are accumulated by
 * index and returned as a [ChatService.ToolAwareResponse.ToolCalls].
 */
internal suspend fun Response.toolAwareStreamResponse(): ChatService.ToolAwareResponse {
    data class PartialCall(
        var id: String = "",
        var name: String = "",
        val arguments: StringBuilder = StringBuilder(),
    )

    val toolCallAccum = mutableMapOf<Int, PartialCall>()
    val textAccum = StringBuilder()

    lineFlow
        .filterNot { it.isEmpty() || it.contains(END_OF_STREAM_MARKER) }
        .map { it.drop(DATA_PREFIX.length) }
        .events()
        .collect { event ->
            for (choice in event.choices) {
                choice.delta.content?.let { textAccum.append(it) }
                for (tc in choice.delta.toolCalls) {
                    val partial = toolCallAccum.getOrPut(tc.index) { PartialCall() }
                    tc.id?.let { partial.id = it }
                    tc.function?.name?.let { partial.name = it }
                    tc.function?.arguments?.let { partial.arguments.append(it) }
                }
            }
        }

    return if (toolCallAccum.isNotEmpty()) {
        ChatService.ToolAwareResponse.ToolCalls(
            toolCallAccum.entries
                .sortedBy { it.key }
                .map { (_, tc) -> ChatService.ToolCallRecord(tc.id, tc.name, tc.arguments.toString()) },
        )
    } else {
        ChatService.ToolAwareResponse.Text(textAccum.toString())
    }
}

private val Response.lineFlow get() = channelFlow {
    body.useBufferedReader { reader ->
        reader.lineSequence().forEach { line ->
            trySend(line)
        }
    }
}

private fun Flow<String>.events(): Flow<Event> {
    val json = Json {
        ignoreUnknownKeys = true
    }

    return map {
        try {
            json.decodeFromString(it)
        } catch (e: SerializationException) {
            if (it.contains("error")) {
                throw ChatServiceError.StreamError(e)
            } else {
                throw ChatServiceError.StreamEventParseError(e)
            }
        }
    }
}

private fun Flow<Event>.content() = map {
    it.choices.joinToString("") { choice -> choice.delta.content ?: "" }
}

@Serializable
private data class Event(
    val id: String = "",
    val created: Long = 0L,
    val choices: List<Choice> = emptyList(),
) {
    @Serializable
    data class Choice(
        val index: Int = 0,
        val delta: Delta = Delta(),
    ) {
        @Serializable
        data class Delta(
            val content: String? = null,
            @SerialName("tool_calls") val toolCalls: List<ToolCallDelta> = emptyList(),
        )
    }
}

@Serializable
private data class ToolCallDelta(
    val index: Int = 0,
    val id: String? = null,
    val function: FunctionDelta? = null,
) {
    @Serializable
    data class FunctionDelta(
        val name: String? = null,
        val arguments: String? = null,
    )
}
