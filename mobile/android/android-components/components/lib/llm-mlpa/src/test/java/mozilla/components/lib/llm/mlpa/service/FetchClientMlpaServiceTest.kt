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
                FetchClientMlpaService(FakeClient.success(json.asBody), MlpaConfig.prodProd)

            val response = mlpaService.verify(
                request = AuthenticationService.Request(
                    userId = UserId("my-user-id"),
                    integrityToken = IntegrityToken("my-integrity-token"),
                    packageName = PackageName("my.package.name"),
                ),
            )

            val expected = AuthenticationService.Response(
                accessToken = AuthorizationToken.Integrity("my-authorization-token"),
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
                FetchClientMlpaService(FakeClient.success(json.asBody), MlpaConfig.prodProd)

            val response = mlpaService.verify(
                request = AuthenticationService.Request(
                    userId = UserId("my-user-id"),
                    integrityToken = IntegrityToken("my-integrity-token"),
                    packageName = PackageName("my.package.name"),
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
            val mlpaService = FetchClientMlpaService(FakeClient.failure(401), MlpaConfig.prodProd)

            val response = mlpaService.verify(
                request = AuthenticationService.Request(
                    userId = UserId("my-user-id"),
                    integrityToken = IntegrityToken("my-integrity-token"),
                    packageName = PackageName("my.package.name"),
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

            val fakeClient = FakeClient.success(json.asBody)
            val mlpaService = FetchClientMlpaService(fakeClient, MlpaConfig.prodProd)

            val response = mlpaService.completion(
                authorizationToken = AuthorizationToken.Integrity("my-token"),
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
            assertEquals("s2s-android", fakeClient.lastRequest?.headers?.get("service-type"))
            assertEquals("true", fakeClient.lastRequest?.headers?.get("use-play-integrity"))
        }

    @Test
    fun `GIVEN a successful response with an fxa token WHEN try to chat THEN dont include the use-play-integrity header`() =
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

            val fakeClient = FakeClient.success(json.asBody)
            val mlpaService = FetchClientMlpaService(fakeClient, MlpaConfig.prodProd)

            val response = mlpaService.completion(
                authorizationToken = AuthorizationToken.Fxa("my-token"),
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
            assertEquals("s2s-android", fakeClient.lastRequest?.headers?.get("service-type"))
            assertEquals(null, fakeClient.lastRequest?.headers?.get("use-play-integrity"))
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
                FetchClientMlpaService(FakeClient.success(json.asBody), MlpaConfig.prodProd)

            val response = mlpaService.completion(
                authorizationToken = AuthorizationToken.Integrity("my-token"),
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
            val mlpaService = FetchClientMlpaService(FakeClient.failure(401), MlpaConfig.prodProd)

            val response = mlpaService.completion(
                authorizationToken = AuthorizationToken.Integrity("my-token"),
                request = ChatService.Request(
                    model = ChatService.Request.ModelID.mistral,
                    messages = listOf(ChatService.Request.Message.user("hello")),
                ),
            )

            assertTrue(response.isFailure)

            response.onFailure {
                assertEquals("Chat Service Failed: Received status code 401", it.message)
            }
        }
}
