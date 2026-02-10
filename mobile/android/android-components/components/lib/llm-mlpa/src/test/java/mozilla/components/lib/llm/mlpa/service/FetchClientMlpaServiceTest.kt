/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.llm.mlpa.service

import kotlinx.coroutines.test.runTest
import kotlinx.serialization.ExperimentalSerializationApi
import kotlinx.serialization.MissingFieldException
import mozilla.components.concept.integrity.IntegrityToken
import mozilla.components.lib.llm.mlpa.fakes.FakeClient
import mozilla.components.lib.llm.mlpa.fakes.asBody
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class FetchClientMlpaServiceTest {
    @Test
    fun `GIVEN a successful response WHEN try to verify an integrity token THEN return a constructed Response`() =
        runTest {
            val json = """
                {
                    "access_token": "my-authorization-token",
                    "token_type": "bearer",
                    "expires_in": 6000
                }
            """.trimIndent()

            val mlpaService =
                FetchClientMlpaService(FakeClient.success(json.asBody), MlpaConfig.live)

            val response = mlpaService.verify(
                request = AuthenticationService.Request(
                    userId = UserId("my-user-id"),
                    integrityToken = IntegrityToken("my-integrity-token"),
                ),
            )

            val expected = AuthenticationService.Response(
                accessToken = AuthorizationToken("my-authorization-token"),
                tokenType = "bearer",
                expiresIn = 6000,
            )

            assertEquals(response.getOrThrow(), expected)
        }

    @OptIn(ExperimentalSerializationApi::class)
    @Test
    fun `GIVEN a malformed response WHEN we try to verify an integrity THEN return a failure`() =
        runTest {
            val json = """
                {
                    "blarp_token": "my-authorization-token",
                    "token_type": "bearer",
                    "expires_in": 6000
                }
            """.trimIndent()

            val mlpaService =
                FetchClientMlpaService(FakeClient.success(json.asBody), MlpaConfig.live)

            val response = mlpaService.verify(
                request = AuthenticationService.Request(
                    userId = UserId("my-user-id"),
                    integrityToken = IntegrityToken("my-integrity-token"),
                ),
            )

            assertTrue(response.isFailure)

            response.onFailure {
                assertTrue(it is MissingFieldException)
            }
        }

    @OptIn(ExperimentalSerializationApi::class)
    @Test
    fun `GIVEN a failure response WHEN we try to verify an integrity THEN return a failure`() =
        runTest {
            val mlpaService = FetchClientMlpaService(FakeClient.failure(401), MlpaConfig.live)

            val response = mlpaService.verify(
                request = AuthenticationService.Request(
                    userId = UserId("my-user-id"),
                    integrityToken = IntegrityToken("my-integrity-token"),
                ),
            )

            assertTrue(response.isFailure)

            response.onFailure {
                assertEquals("Verification Service Failed: Received status code 401", it.message)
            }
        }

    @Test
    fun `GIVEN a successful response WHEN try to chat THEN return a constructed Response`() =
        runTest {
            val json = """
                {
                    "choices": [
                        {
                            "message": {
                                "content" : "world!"
                            }
                        }
                    ]
                }
            """.trimIndent()

            val mlpaService =
                FetchClientMlpaService(FakeClient.success(json.asBody), MlpaConfig.live)

            val response = mlpaService.completion(
                authorizationToken = AuthorizationToken("my-token"),
                request = ChatService.Request(
                    model = ChatService.Request.ModelID.mistral,
                    messages = listOf(ChatService.Request.Message.user("hello")),
                ),
            )

            val expected = ChatService.Response(
                choices = listOf(
                    ChatService.Response.Choice(ChatService.Response.Message("world!")),
                ),
            )

            assertEquals(response.getOrThrow(), expected)
        }

    @OptIn(ExperimentalSerializationApi::class)
    @Test
    fun `GIVEN a malformed response WHEN try to chat THEN return a failure`() =
        runTest {
            val json = """
                {
                    "not_expected": [
                        {
                            "message": {
                                "content" : "world!"
                            }
                        }
                    ]
                }
            """.trimIndent()

            val mlpaService =
                FetchClientMlpaService(FakeClient.success(json.asBody), MlpaConfig.live)

            val response = mlpaService.completion(
                authorizationToken = AuthorizationToken("my-token"),
                request = ChatService.Request(
                    model = ChatService.Request.ModelID.mistral,
                    messages = listOf(ChatService.Request.Message.user("hello")),
                ),
            )

            assertTrue(response.isFailure)

            response.onFailure {
                assertTrue(it is MissingFieldException)
            }
        }

    @Test
    fun `GIVEN an error status code WHEN try to chat THEN return a failure`() =
        runTest {
            val mlpaService = FetchClientMlpaService(FakeClient.failure(401), MlpaConfig.live)

            val response = mlpaService.completion(
                authorizationToken = AuthorizationToken("my-token"),
                request = ChatService.Request(
                    model = ChatService.Request.ModelID.mistral,
                    messages = listOf(ChatService.Request.Message.user("hello")),
                ),
            )

            assertTrue(response.isFailure)

            response.onFailure {
                assertEquals("Verification Service Failed: Received status code 401", it.message)
            }
        }
}
