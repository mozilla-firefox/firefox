/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:OptIn(ExperimentalAndroidComponentsApi::class)

package mozilla.components.feature.ipprotection

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.concept.engine.ipprotection.IPProtectionHandler
import mozilla.components.concept.integrity.IntegrityClient
import mozilla.components.concept.integrity.IntegrityToken
import mozilla.components.feature.ipprotection.auth.gpi.IPProtectionGpiProvider
import mozilla.components.lib.integrity.googleplay.GooglePlayIntegrityClient
import mozilla.components.lib.integrity.googleplay.IntegrityConsumer
import mozilla.components.support.test.argumentCaptor
import mozilla.components.support.test.coMock
import mozilla.components.support.test.mock
import mozilla.components.support.test.whenever
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.mockito.Mockito.verify

class IPProtectionGpiProviderTest {

    @Test
    fun `WHEN the provider warms up THEN it delegates to the integrity client`() = runTest {
        val integrityClient =
            coMock<GooglePlayIntegrityClient> {
                whenever(warmUp()).thenReturn(true)
            }
        val handler = mock<IPProtectionHandler>()
        val testScope = CoroutineScope(StandardTestDispatcher(testScheduler))

        IPProtectionGpiProvider(integrityClient).configure(handler, testScope)

        val captor = argumentCaptor<IPProtectionHandler.GpiProvider>()
        verify(handler).setGpiProvider(captor.capture())

        var warmedUp: Boolean? = null
        captor.value.warmUp { warmedUp = it }
        testScheduler.advanceUntilIdle()

        assertTrue(warmedUp == true)
    }

    @Test
    fun `GIVEN the integrity client fails to warm up, WHEN GPI provider warms up, THEN a failure is reported`() =
        runTest {
            val integrityClient =
                coMock<GooglePlayIntegrityClient> {
                    whenever(warmUp()).thenThrow(RuntimeException("warm-up-failed"))
                }
            val handler = FakeIPProtectionHandler()
            val testScope = CoroutineScope(StandardTestDispatcher(testScheduler))

            IPProtectionGpiProvider(integrityClient).configure(handler, testScope)

            var warmedUpSuccessfully: Boolean? = null
            handler.gpiProvider!!.warmUp { successful ->
                warmedUpSuccessfully = successful
            }
            testScheduler.advanceUntilIdle()
            assertEquals(false, warmedUpSuccessfully)
        }

    @Test
    fun `WHEN a token is requested THEN the consumer client token is returned`() = runTest {
        val integrityClient =
            mock<GooglePlayIntegrityClient> {
                whenever(forConsumer(IntegrityConsumer.IpProtection))
                    .thenReturn(IntegrityClient { Result.success(IntegrityToken("gpi-token")) })
            }
        val handler = mock<IPProtectionHandler>()
        val testScope = CoroutineScope(StandardTestDispatcher(testScheduler))

        IPProtectionGpiProvider(integrityClient).configure(handler, testScope)

        val captor = argumentCaptor<IPProtectionHandler.GpiProvider>()
        verify(handler).setGpiProvider(captor.capture())

        var token: String? = null
        captor.value.getToken { token = it }
        testScheduler.advanceUntilIdle()

        assertEquals("gpi-token", token)
    }

    @Test
    fun `WHEN the token request fails THEN null is returned`() = runTest {
        val integrityClient =
            mock<GooglePlayIntegrityClient> {
                whenever(forConsumer(IntegrityConsumer.IpProtection))
                    .thenReturn(IntegrityClient { Result.failure(RuntimeException("boom")) })
            }
        val handler = mock<IPProtectionHandler>()
        val testScope = CoroutineScope(StandardTestDispatcher(testScheduler))

        IPProtectionGpiProvider(integrityClient).configure(handler, testScope)

        val captor = argumentCaptor<IPProtectionHandler.GpiProvider>()
        verify(handler).setGpiProvider(captor.capture())

        var token: String? = "unset"
        captor.value.getToken { token = it }
        testScheduler.advanceUntilIdle()

        assertNull(token)
    }
}
