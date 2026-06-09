/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.llm.ext

import kotlinx.coroutines.test.runTest
import mozilla.components.lib.llm.mlpa.MlpaTokenProvider
import mozilla.components.lib.llm.mlpa.service.AuthorizationToken
import org.junit.Assert.assertEquals
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test

class MlpaTokenProviderTest {

    @Test
    fun `GIVEN the first provider succeeds WHEN fetching THEN its token is returned`() = runTest {
        val expected = AuthorizationToken.Integrity("first")
        val provider = MlpaTokenProvider.choose(
             { Result.success(expected) },
            { Result.failure(IllegalStateException("integrity failed")) },
        )

        assertEquals(expected, provider.fetchToken().getOrThrow() as AuthorizationToken.Integrity)
    }

    @Test
    fun `GIVEN the first provider fails WHEN fetching THEN the next provider's token is returned`() = runTest {
        val expected = AuthorizationToken.Integrity("second")
        val provider = MlpaTokenProvider.choose(
            { Result.failure(FxaMissingAccessToken()) },
            { Result.success(expected) },
        )

        assertEquals(expected, provider.fetchToken().getOrThrow())
    }

    @Test
    fun `GIVEN all providers fail WHEN fetching THEN the last provider's failure is propagated`() = runTest {
        val terminal = IllegalStateException("integrity failed")
        val provider = MlpaTokenProvider.choose(
            { Result.failure(FxaMissingAccessToken()) },
            { Result.failure(terminal) },
        )

        val result = provider.fetchToken()

        assertTrue(result.isFailure)
        assertSame(terminal, result.exceptionOrNull())
    }
}
