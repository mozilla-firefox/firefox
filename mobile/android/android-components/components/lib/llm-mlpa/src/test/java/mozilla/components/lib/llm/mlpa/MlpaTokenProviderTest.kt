/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.llm.mlpa

import kotlinx.coroutines.test.runTest
import mozilla.components.lib.llm.mlpa.fakes.failureAuthenticationService
import mozilla.components.lib.llm.mlpa.fakes.failureIntegrityClient
import mozilla.components.lib.llm.mlpa.fakes.successAuthenticationService
import mozilla.components.lib.llm.mlpa.fakes.successIntegrityClient
import mozilla.components.lib.llm.mlpa.fakes.userIdProvider
import mozilla.components.lib.llm.mlpa.service.AuthorizationToken
import mozilla.components.lib.llm.mlpa.service.UserId
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class MlpaTokenProviderTest {
    @Test
    fun `GIVEN a static provider WHEN I fetch the token THEN return the provided token`() =
        runTest {
            val expected = AuthorizationToken("my-test-token")
            val provider = MlpaTokenProvider.static(expected)
            val result = provider.fetchToken()

            assertTrue(result.isSuccess)

            result.onSuccess {
                assertEquals(expected, it)
            }
        }

    @Test
    fun `GIVEN the happy path WHEN I fetch the token THEN return the provided token`() = runTest {
        var actualUserId: UserId? = null

        val provider = MlpaTokenProvider.mlpaIntegrityHandshake(
            integrityClient = successIntegrityClient,
            authenticationService = { request ->
                actualUserId = request.userId
                successAuthenticationService.verify(request)
            },
            userIdProvider = userIdProvider,
        )

        val actual = provider.fetchToken()

        assertEquals("test-user-id", actualUserId?.value)
        assertEquals("my-test-token", actual.getOrThrow().value)
    }

    @Test
    fun `GIVEN a failed integrity token WHEN I fetch the token THEN propagate the failure`() =
        runTest {
            val provider = MlpaTokenProvider.mlpaIntegrityHandshake(
                integrityClient = failureIntegrityClient,
                authenticationService = successAuthenticationService,
                userIdProvider = userIdProvider,
            )

            val actual = provider.fetchToken()

            assertTrue(actual.isFailure)

            actual.onFailure {
                assertEquals("Missing Token!", it.message)
            }
        }

    @Test
    fun `GIVEN a failed mlpa response WHEN I fetch the token THEN propagate the failure`() =
        runTest {
            val provider = MlpaTokenProvider.mlpaIntegrityHandshake(
                integrityClient = successIntegrityClient,
                authenticationService = failureAuthenticationService,
                userIdProvider = userIdProvider,
            )

            val actual = provider.fetchToken()

            assertTrue(actual.isFailure)

            actual.onFailure {
                assertEquals("Bad MLPA Response", it.message)
            }
        }
}
