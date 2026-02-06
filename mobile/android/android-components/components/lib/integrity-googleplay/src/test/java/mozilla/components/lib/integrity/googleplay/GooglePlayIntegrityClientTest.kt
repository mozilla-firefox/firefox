/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.integrity.googleplay

import com.google.android.play.core.integrity.StandardIntegrityException
import com.google.android.play.core.integrity.model.StandardIntegrityErrorCode.INTEGRITY_TOKEN_PROVIDER_INVALID
import kotlinx.coroutines.test.runTest
import mozilla.components.concept.integrity.IntegrityToken
import mozilla.components.support.test.mock
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.mockito.Mockito.`when`

class GooglePlayIntegrityClientTest {
    @Test
    fun `GIVEN an empty string WHEN I try to create a GoogleProjectNumber then I get an Invalid GoogleProjectNumber`() {
        assertEquals(
            "GoogleProjectNumber should be invalid",
            GoogleProjectNumber.Invalid,
            GoogleProjectNumber.create(""),
        )
    }

    @Test
    fun `GIVEN a value that can't be cast to a Long WHEN I try to create a GoogleProjectNumber then I get an Invalid GoogleProjectNumber`() {
        assertEquals(
            "GoogleProjectNumber should be invalid",
            GoogleProjectNumber.Invalid,
            GoogleProjectNumber.create("100Z"),
        )
    }

    @Test
    fun `GIVEN a value that can be cast to a Long WHEN I try to create a GoogleProjectNumber then I get a Valid GoogleProjectNumber`() {
        assertEquals(
            "GoogleProjectNumber should be invalid",
            GoogleProjectNumber.Valid(1337L),
            GoogleProjectNumber.create("1337"),
        )
    }

    @Test
    fun `GIVEN an Invalid GoogleProjectNumber WHEN I create a TokenProviderFactory THEN I get a TokenProvider that returns a failure`() = runTest {
        val tokenProvider = TokenProviderFactory.create(
            { mock() },
            GoogleProjectNumber.Invalid,
        )

        val result = tokenProvider.create()

        assertTrue(
            "Result should be InvalidProjectNumber",
            result.exceptionOrNull() is InvalidProjectNumber,
        )
    }

    @Test
    fun `GIVEN a Valid GoogleProjectNumber WHEN I create a TokenProviderFactory THEN I get a GooglePlayTokenProviderFactory`() {
        val tokenProvider = TokenProviderFactory.create(
            { mock() },
            GoogleProjectNumber.Valid(100L),
        )
        assertTrue(
            "Result should be an instance of GooglePlayTokenProviderFactory",
            tokenProvider is GooglePlayTokenProviderFactory,
        )
    }

    @Test
    fun `GIVEN a GooglePlayIntegrityClient without a tokenProvider WHEN warmUp is called THEN we get a tokenProvider from the factory`() = runTest {
        val tokenProvider = TokenProvider { _ -> Result.success(IntegrityToken("test-token")) }
        val client = GooglePlayIntegrityClient(
            { Result.success(tokenProvider) },
            { "test-hash" },
        )

        assertTrue(client.tokenProvider == null)
        client.warmUp()
        assertEquals(tokenProvider, client.tokenProvider)
    }

    @Test
    fun `GIVEN an expired tokenProvider WHEN request is called THEN we get a tokenProvider from the factory`() = runTest {
        val exception: StandardIntegrityException = mock {
            `when`(this.errorCode).thenReturn(INTEGRITY_TOKEN_PROVIDER_INVALID)
        }

        val expiredTokenProvider = TokenProvider { _ -> Result.failure(exception) }
        val tokenProvider = TokenProvider { _ -> Result.success(IntegrityToken("test-token")) }

        val tokenProviders = mutableListOf(expiredTokenProvider, tokenProvider)
        val client = GooglePlayIntegrityClient(
            {
                Result.success(tokenProviders.removeAt(0))
            },
            { "test-hash" },
        )

        assertTrue(client.tokenProvider == null)
        client.warmUp()
        val result = client.request()
        assertTrue(result.isSuccess)
        assertTrue(tokenProviders.isEmpty())
    }

    @Test
    fun `GIVEN an exception from a TokenProvider that isn't INTEGRITY_TOKEN_PROVIDER_INVALID WHEN request is called THEN return the result`() = runTest {
        val tokenProvider = TokenProvider { _ -> Result.failure(IllegalStateException("test exception")) }
        val client = GooglePlayIntegrityClient(
            { Result.success(tokenProvider) },
            { "test-hash" },
        )

        val result = client.request()
        assertTrue(result.isFailure)
    }
}
