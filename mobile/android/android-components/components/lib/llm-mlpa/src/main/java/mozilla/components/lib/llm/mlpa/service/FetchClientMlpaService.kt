/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.llm.mlpa.service

import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.serialization.json.Json
import mozilla.components.concept.fetch.Client
import mozilla.components.concept.fetch.MutableHeaders
import mozilla.components.concept.fetch.Request
import mozilla.components.concept.fetch.Response
import mozilla.components.concept.fetch.isClientError

/**
 * Default [MlpaService] implementation backed by a generic HTTP [Client].
 *
 * This service communicates with the MLPA backend using JSON over HTTP.
 * It serializes requests using kotlinx.serialization and deserializes
 * successful responses into strongly typed service models.
 *
 * @param client The HTTP client used to execute network requests.
 * @param config Configuration containing the base URL for MLPA endpoints.
 */
class FetchClientMlpaService(
    private val client: Client,
    private val config: MlpaConfig,
    private val dispatcher: CoroutineDispatcher = Dispatchers.IO,
) : MlpaService {
    private val json by lazy {
        Json {
            ignoreUnknownKeys = true
        }
    }

    /**
     * Calls the `/verify/play` endpoint to verify an authentication request.
     *
     * @param request the required properties of this request as an [AuthenticationService.Request]
     * @return [Result.success] with the parsed response on success containing a [AuthenticationService.Response],
     * or [Result.failure] if the HTTP call is not successful.
     */
    override suspend fun verify(
        request: AuthenticationService.Request,
    ): Result<AuthenticationService.Response> = withContext(dispatcher) {
            val fetchRequest = Request(
                url = "${config.baseUrl}/verify/play",
                method = Request.Method.POST,
                headers = MutableHeaders(),
                body = Request.Body.fromString(json.encodeToString(request)),
            )

            return@withContext Result.runCatching {
                val httpResponse = client.fetch(fetchRequest)
                if (httpResponse.isClientError) {
                    throw VerificationServiceFailed("Received status code ${httpResponse.status}")
                }

                json.decodeFromString(httpResponse.bodyString)
            }
        }

    /**
     * Calls the `/chat/completions` endpoint to request a chat completion.
     *
     * @param authorizationToken the authorizationToken to include in the request headers.
     * @param request the required properties of this request as an [ChatService.Request]
     * @return [Result.success] with the parsed response on success containing a [ChatService.Response],
     * or [Result.failure] if the HTTP call is not successful.
     */
    override suspend fun completion(
        authorizationToken: AuthorizationToken,
        request: ChatService.Request,
    ): Result<ChatService.Response> = withContext(dispatcher) {
        val bodyString = json.encodeToString(request)
        val fetchRequest = Request(
            url = "${config.baseUrl}/v1/chat/completions",
            method = Request.Method.POST,
            headers = MutableHeaders(
                "authorization" to "Bearer ${authorizationToken.value}",
                "content-type" to "application/json",
                "service-type" to "s2s-android",
            ).apply {
                if (authorizationToken is AuthorizationToken.Integrity) {
                    set("use-play-integrity", "true")
                }
            },
            body = Request.Body.fromString(bodyString),
        )

        return@withContext Result.runCatching {
            val httpResponse = client.fetch(fetchRequest)

            httpResponse.error?.also {
                throw ChatServiceException(it)
            }

            json.decodeFromString(httpResponse.bodyString)
        }
    }

    private val Response.bodyString get() = use { body.string(Charsets.UTF_8) }
    private val Response.retryAfter: Long? get() = headers["Retry-After"]?.toLongOrNull()
    private val Response.error: ChatServiceError? get() = when (status) {
        in 200..299 -> null
        401 -> ChatServiceError.InvalidToken
        403 -> ChatServiceError.UserBlocked
        413 -> ChatServiceError.RequestTooLarge
        429 -> when (json.decodeFromString<ChatService.ResponseErrorCode>(bodyString).error) {
            1 -> ChatServiceError.BudgetExceeded(retryAfter)
            2 -> ChatServiceError.RateLimited(retryAfter)
            else -> ChatServiceError.ServerError(status)
        }
        502 -> ChatServiceError.UpstreamError(json.decodeFromString<ChatService.ResponseErrorReason>(bodyString).error)
        else -> ChatServiceError.ServerError(status)
    }
}
