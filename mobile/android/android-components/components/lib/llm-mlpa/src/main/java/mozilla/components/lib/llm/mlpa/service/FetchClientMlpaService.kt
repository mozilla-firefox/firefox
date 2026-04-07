/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.llm.mlpa.service

import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.emitAll
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.flow.flowOn
import kotlinx.coroutines.withContext
import kotlinx.serialization.SerializationException
import kotlinx.serialization.json.Json
import mozilla.components.concept.fetch.Client
import mozilla.components.concept.fetch.MutableHeaders
import mozilla.components.concept.fetch.Request
import mozilla.components.concept.fetch.Response
import mozilla.components.concept.fetch.isClientError
import mozilla.components.lib.llm.mlpa.service.ext.contentFlow
import java.io.IOException

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
            encodeDefaults = true
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
                client.fetch(fetchRequest).use { httpResponse ->
                    if (httpResponse.isClientError) {
                        throw VerificationServiceFailed("Received status code ${httpResponse.status}")
                    }

                    json.decodeFromString(httpResponse.body.string(Charsets.UTF_8))
                }
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
    override fun completion(
        authorizationToken: AuthorizationToken,
        request: ChatService.Request,
    ): Flow<String> {
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

        return flow {
            val httpResponse = try {
                client.fetch(fetchRequest)
            } catch (e: IOException) {
                throw ChatServiceError.NetworkError(e)
            }
            httpResponse.use {
                it.error?.also { error -> throw error }

                if (request.stream) {
                    emitAll(it.contentFlow)
                } else {
                    emit(it.nonStreamedResponse)
                }
            }
        }.flowOn(dispatcher)
    }

    private val Response.nonStreamedResponse get() = try {
        json.decodeFromString<ChatService.Response>(bodyString).choices.first().message.content
    } catch (e: SerializationException) {
        throw ChatServiceError.ResponseParseError(e)
    }

    private val Response.bodyString get() = use { body.string(Charsets.UTF_8) }
    private val Response.retryAfter: Long? get() = headers["Retry-After"]?.toLongOrNull()
    private val Response.error: ChatServiceError? get() = when (status) {
        in 200..299 -> null
        401 -> ChatServiceError.InvalidToken()
        403 -> ChatServiceError.UserBlocked()
        413 -> ChatServiceError.RequestTooLarge()
        429 -> try {
            when (json.decodeFromString<ChatService.ResponseErrorCode>(bodyString).error) {
                1 -> ChatServiceError.BudgetExceeded(retryAfter)
                2 -> ChatServiceError.RateLimited(retryAfter)
                else -> ChatServiceError.ServerError(status)
            }
        } catch (e: SerializationException) {
            ChatServiceError.RateLimitResponseParseError(e)
        }
        502 -> try {
            ChatServiceError.UpstreamError(json.decodeFromString<ChatService.ResponseErrorReason>(bodyString).error)
        } catch (e: SerializationException) {
            ChatServiceError.UpstreamResponseParseError(e)
        }
        else -> ChatServiceError.ServerError(status)
    }
}
