/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.llm.mlpa

import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.flow
import mozilla.components.concept.llm.Llm
import mozilla.components.concept.llm.Prompt
import mozilla.components.lib.llm.mlpa.service.AuthorizationToken
import mozilla.components.lib.llm.mlpa.service.ChatService
import mozilla.components.lib.llm.mlpa.service.ChatService.Request
import mozilla.components.lib.llm.mlpa.service.ChatService.Request.Message
import mozilla.components.lib.llm.mlpa.service.ChatService.Request.ModelID

internal class MlpaLlm(
    val chatService: ChatService,
    val authorizationToken: AuthorizationToken,
) : Llm {
    override suspend fun prompt(prompt: Prompt): Flow<Llm.Response> = flow {
        chatService.completion(authorizationToken, prompt.asRequest)
            .onSuccess {
                emit(Llm.Response.Success.ReplyPart(it.choices.first().message.content))
                emit(Llm.Response.Success.ReplyFinished)
            }
            .onFailure {
                emit(Llm.Response.Failure("MlpaLlm Failed: ${it.message}"))
            }
    }
}

internal val Prompt.asRequest
    get() = Request(
        model = ModelID.mistral,
        messages = listOf(
            Message.user(value),
        ),
    )
