/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.llm.mlpa.service

import kotlinx.serialization.json.Json
import mozilla.components.concept.fetch.Client
import mozilla.components.concept.fetch.MutableHeaders
import mozilla.components.concept.fetch.Request
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
    override suspend fun verify(request: AuthenticationService.Request): Result<AuthenticationService.Response> {
        val fetchRequest = Request(
            url = "${config.baseUrl}/verify/play",
            method = Request.Method.POST,
            headers = MutableHeaders(),
            body = Request.Body.fromString(json.encodeToString(request)),
        )

        return Result.runCatching {
            val httpResponse = client.fetch(fetchRequest)
            if (httpResponse.isClientError) {
                throw VerificationServiceFailed("Received status code ${httpResponse.status}")
            }

            httpResponse
                .use { httpResponse.body.string(Charsets.UTF_8) }
                .let { json.decodeFromString(it) }
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
    ): Result<ChatService.Response> {
        val bodyString = json.encodeToString(request)
        val fetchRequest = Request(
            url = "${config.baseUrl}/chat/completions",
            method = Request.Method.POST,
            headers = MutableHeaders(
                "authorization" to authorizationToken.value,
                "content-type" to "application/json",
                "service-type" to "s2s",
            ),
            body = Request.Body.fromString(bodyString),
        )

        return Result.runCatching {
            val httpResponse = client.fetch(fetchRequest)
            if (httpResponse.isClientError) {
                throw ChatServiceFailed("Received status code ${httpResponse.status}")
            }

            httpResponse
                .use { httpResponse.body.string(Charsets.UTF_8) }
                .let { json.decodeFromString(it) }
        }
    }
}
