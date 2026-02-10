/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.llm.mlpa

import kotlinx.coroutines.test.runTest
import mozilla.components.concept.llm.CloudLlmProvider
import mozilla.components.lib.llm.mlpa.fakes.FakeMlpaService
import mozilla.components.lib.llm.mlpa.fakes.failureTokenProvider
import mozilla.components.lib.llm.mlpa.fakes.successTokenProvider
import org.junit.Assert.assertTrue
import org.junit.Test

class MlpaLlmProviderTest {
    @Test
    fun `GIVEN a token provider that returns a token WHEN I prepare THEN the provider transitions to the ready state`() =
        runTest {
            val provider = MlpaLlmProvider(
                tokenProvider = successTokenProvider,
                mlpaService = FakeMlpaService(),
            )

            assertTrue(provider.state.value is CloudLlmProvider.State.Available)

            provider.prepare()

            assertTrue(provider.state.value is CloudLlmProvider.State.Ready)
        }

    @Test
    fun `GIVEN a token provider that returns a failure WHEN I prepare THEN the provider transitions to the unavailable state`() =
        runTest {
            val provider = MlpaLlmProvider(
                tokenProvider = failureTokenProvider,
                mlpaService = FakeMlpaService(),
            )

            assertTrue(provider.state.value is CloudLlmProvider.State.Available)

            provider.prepare()

            assertTrue(provider.state.value is CloudLlmProvider.State.Unavailable)
        }
}
