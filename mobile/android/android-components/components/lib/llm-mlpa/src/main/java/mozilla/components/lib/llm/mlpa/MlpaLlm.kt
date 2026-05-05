/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.llm.mlpa

import kotlinx.coroutines.flow.flowOf
import kotlinx.serialization.json.Json
import mozilla.components.concept.llm.Llm
import mozilla.components.concept.llm.LlmTool
import mozilla.components.lib.llm.mlpa.service.AuthorizationToken
import mozilla.components.lib.llm.mlpa.service.ChatService
import mozilla.components.lib.llm.mlpa.service.ChatService.Request
import mozilla.components.lib.llm.mlpa.service.ChatService.Request.Message
import mozilla.components.lib.llm.mlpa.service.ChatService.Request.ModelID

internal class MlpaLlm(
    val chatService: ChatService,
    val authorizationToken: AuthorizationToken,
) : Llm {
    override suspend fun prompt(contextWindow: Llm.ContextWindow): Llm.LlmTurnResult {
        val request = contextWindow.asRequest
        return if (contextWindow.tools.isEmpty()) {
            Llm.LlmTurnResult.Text(chatService.completion(authorizationToken, request))
        } else {
            when (val response = chatService.completionWithTools(authorizationToken, request)) {
                is ChatService.ToolAwareResponse.Text ->
                    Llm.LlmTurnResult.Text(flowOf(response.content))
                is ChatService.ToolAwareResponse.ToolCalls ->
                    Llm.LlmTurnResult.ToolCalls(
                        response.calls.map { Llm.ToolCall(it.id, it.toolName, it.arguments) },
                    )
            }
        }
    }
}

internal val Llm.ContextWindow.asRequest: Request
    get() {
        val requestTools = tools.map { it.toRequestTool() }.takeIf { it.isNotEmpty() }
        return Request(
            model = ModelID.mozSummarization,
            messages = messages.map { it.toRequestMessage() },
            tools = requestTools,
            toolChoice = when {
                tools.isEmpty() -> null
                messages.any { it is Llm.Message.Tool } -> "auto"
                else -> "required"
            },
        )
    }

private fun Llm.Message.toRequestMessage(): Message = when (this) {
    is Llm.Message.System -> Message.system(message)
    is Llm.Message.User -> Message.user(message)
    is Llm.Message.Assistant -> Message.assistant(message)
    is Llm.Message.AssistantToolCall -> Message.assistantToolCall(
        calls.map { call ->
            Message.ToolCall(
                id = call.id,
                function = Message.ToolCall.Function(
                    name = call.toolName,
                    arguments = call.arguments,
                ),
            )
        },
    )
    is Llm.Message.Tool -> Message.tool(toolCallId, message)
}

private fun LlmTool.toRequestTool(): Request.Tool = Request.Tool(
    function = Request.Tool.Function(
        name = name,
        description = description,
        parameters = Json.parseToJsonElement(parametersSchema),
    ),
)
