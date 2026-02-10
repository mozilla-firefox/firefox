/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.llm.mlpa

import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.test.runTest
import mozilla.components.concept.llm.CloudLlmProvider
import mozilla.components.concept.llm.Llm
import mozilla.components.concept.llm.Prompt
import mozilla.components.lib.llm.mlpa.fakes.FakeMlpaService
import mozilla.components.lib.llm.mlpa.fakes.failureChatService
import mozilla.components.lib.llm.mlpa.fakes.failureTokenProvider
import mozilla.components.lib.llm.mlpa.fakes.successChatService
import mozilla.components.lib.llm.mlpa.fakes.successTokenProvider
import mozilla.components.lib.llm.mlpa.service.AuthorizationToken
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.exp

class MlpaLlmTest {
    @Test
    fun `GIVEN a successful response from the mlpa client WHEN prompt THEN I get a valid response`() = runTest {
        var expectedToken: AuthorizationToken? = null
        val llm = MlpaLlm(
            chatService = { token, request ->
                expectedToken = token
                successChatService.completion(token, request)
            },
            authorizationToken = AuthorizationToken("my-test-token"),
        )

        val actual = llm.prompt(Prompt("This is my prompt")).toList()
        val expected = listOf(
            Llm.Response.Success.ReplyPart("Hello World!"),
            Llm.Response.Success.ReplyFinished,
        )

        assertEquals(expectedToken, AuthorizationToken("my-test-token"))
        assertEquals(expected, actual)
    }

    @Test
    fun `GIVEN a failure response from the mlpa client WHEN prompt THEN I get a valid response`() = runTest {
        val llm = MlpaLlm(
            chatService = failureChatService,
            authorizationToken = AuthorizationToken("my-test-token"),
        )

        val actual = llm.prompt(Prompt("This is my prompt")).toList()
        val expected = listOf(
            Llm.Response.Failure("MlpaLlm Failed: Bad response!"),
        )

        assertEquals(expected, actual)
    }
}
