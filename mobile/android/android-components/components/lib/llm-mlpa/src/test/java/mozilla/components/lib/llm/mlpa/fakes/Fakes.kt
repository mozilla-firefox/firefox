/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.llm.mlpa.fakes

import mozilla.components.concept.fetch.Client
import mozilla.components.concept.fetch.Headers
import mozilla.components.concept.fetch.MutableHeaders
import mozilla.components.concept.fetch.Request
import mozilla.components.concept.fetch.Response
import mozilla.components.concept.integrity.IntegrityClient
import mozilla.components.concept.integrity.IntegrityToken
import mozilla.components.lib.llm.mlpa.MlpaTokenProvider
import mozilla.components.lib.llm.mlpa.UserIdProvider
import mozilla.components.lib.llm.mlpa.service.AuthenticationService
import mozilla.components.lib.llm.mlpa.service.AuthorizationToken
import mozilla.components.lib.llm.mlpa.service.ChatService
import mozilla.components.lib.llm.mlpa.service.MlpaService
import mozilla.components.lib.llm.mlpa.service.UserId
import java.io.ByteArrayInputStream
import java.io.InputStream
import java.nio.charset.StandardCharsets

val successIntegrityClient = IntegrityClient {
    Result.success(IntegrityToken("my-integrity-token"))
}

val failureIntegrityClient = IntegrityClient {
    Result.failure(IllegalStateException("Missing Token!"))
}

val successTokenProvider = MlpaTokenProvider {
    Result.success(AuthorizationToken("my-test-token"))
}

val userIdProvider = UserIdProvider { UserId("test-user-id") }

val failureTokenProvider = MlpaTokenProvider {
    Result.failure(IllegalStateException("Missing Token!"))
}

val successAuthenticationService = AuthenticationService { request ->
    Result.success(
        AuthenticationService.Response(
            AuthorizationToken("my-test-token"),
            tokenType = "bearer",
            expiresIn = 6000,
        ),
    )
}

val failureAuthenticationService = AuthenticationService { request ->
    Result.failure(IllegalStateException("Bad MLPA Response"))
}

val successChatService = ChatService { token, request ->
    Result.success(
        ChatService.Response(
            choices = listOf(
                ChatService.Response.Choice(
                    ChatService.Response.Message("Hello World!"),
                ),
            ),
        ),
    )
}

val failureChatService = ChatService { token, request ->
    Result.failure(IllegalStateException("Bad response!"))
}

data class FakeMlpaService(
    val authService: AuthenticationService = successAuthenticationService,
    val chatService: ChatService = successChatService,
) : MlpaService, ChatService by chatService, AuthenticationService by authService

data class FakeClient(
    val status: Int = 200,
    val headers: Headers = MutableHeaders(),
    val body: Response.Body = Response.Body.empty(),
) : Client() {
    override fun fetch(request: Request): Response {
        return Response(
            url = request.url,
            status = status,
            headers = headers,
            body = body,
        )
    }

    companion object {
        fun success(body: Response.Body = Response.Body.empty()) = FakeClient(body = body)
        fun failure(status: Int) = FakeClient(status = status)
    }
}

val String.asBody: Response.Body get() = Response.Body(
    ByteArrayInputStream(this.toByteArray(StandardCharsets.UTF_8)),
)
