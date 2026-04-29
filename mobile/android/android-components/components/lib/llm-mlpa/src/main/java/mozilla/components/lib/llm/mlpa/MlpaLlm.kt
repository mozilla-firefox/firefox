/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.llm.mlpa

import kotlinx.coroutines.flow.Flow
import mozilla.components.concept.llm.Llm
import mozilla.components.lib.llm.mlpa.service.AuthorizationToken
import mozilla.components.lib.llm.mlpa.service.ChatService
import mozilla.components.lib.llm.mlpa.service.ChatService.Request
import mozilla.components.lib.llm.mlpa.service.ChatService.Request.Message
import mozilla.components.lib.llm.mlpa.service.ChatService.Request.ModelID

internal class MlpaLlm(
    val chatService: ChatService,
    val authorizationToken: AuthorizationToken,
) : Llm {
    override suspend fun prompt(contextWindow: Llm.ContextWindow): Flow<String> = chatService.completion(
        authorizationToken,
        request = contextWindow.asRequest,
    )
}

internal val Llm.ContextWindow.asRequest
    get() = Request(
        model = ModelID.mozSummarization,
        messages = messages.map { message ->
            when (message) {
                is Llm.Message.System -> Message.system(message.message)
                is Llm.Message.User -> Message.user(message.message)
                is Llm.Message.Assistant -> Message.assistant(message.message)
            }
        },
        stream = true,
    )
