/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.integrity.googleplay

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.work.testing.WorkManagerTestInitHelper
import com.google.android.play.core.integrity.StandardIntegrityException
import com.google.android.play.core.integrity.model.StandardIntegrityErrorCode.INTEGRITY_TOKEN_PROVIDER_INVALID
import kotlin.test.assertIs
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.delay
import kotlinx.coroutines.test.runTest
import mozilla.components.concept.integrity.IntegrityToken
import mozilla.components.lib.integrity.googleplay.GleanMetrics.Integrity
import mozilla.components.support.test.mock
import mozilla.telemetry.glean.Glean
import mozilla.telemetry.glean.config.Configuration
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.`when`

@OptIn(ExperimentalCoroutinesApi::class) // testScheduler.currentTime
@RunWith(AndroidJUnit4::class)
class GooglePlayIntegrityClientTest {

    private val context: Context
        get() = ApplicationProvider.getApplicationContext()

    @Before
    fun setUp() {
        WorkManagerTestInitHelper.initializeTestWorkManager(context)
        Glean.resetGlean(context = context, config = Configuration(), clearStores = true)
    }

    @After
    fun tearDown() {
        WorkManagerTestInitHelper.closeWorkDatabase()
    }

    @Test
    fun `GIVEN a null project number WHEN I create a TokenProviderFactory THEN it returns a failure with InvalidProjectNumber`() =
        runTest {
            val factory = TokenProviderFactory.create({ mock() }, null)

            val result = factory.create()

            assertIs<InvalidProjectNumber>(
                result.exceptionOrNull(),
                "Result should be InvalidProjectNumber",
            )
        }

    @Test
    fun `GIVEN a valid project number WHEN I create a TokenProviderFactory THEN I get a GooglePlayTokenProviderFactory`() {
        val factory = TokenProviderFactory.create({ mock() }, 100L)
        assertIs<GooglePlayTokenProviderFactory>(
            factory,
            "Result should be an instance of GooglePlayTokenProviderFactory",
        )
    }

    @Test
    fun `GIVEN a GooglePlayIntegrityClient without a tokenProvider WHEN warmUp is called THEN we get a tokenProvider from the factory`() =
        runTest {
            val tokenProvider = TokenProvider { _ -> Result.success(IntegrityToken("test-token")) }
            val client =
                GooglePlayIntegrityClient(
                    { Result.success(tokenProvider) },
                    { "test-hash" },
                )

            assertTrue(client.tokenProvider == null)
            client.warmUp()
            assertEquals(Result.success(tokenProvider), client.tokenProvider)
        }

    @Test
    fun `GIVEN a forConsumer view WHEN warmUp is called THEN it shares the underlying client's tokenProvider`() =
        runTest {
            val tokenProvider = TokenProvider { _ -> Result.success(IntegrityToken("test-token")) }
            val client =
                GooglePlayIntegrityClient(
                    { Result.success(tokenProvider) },
                    { "test-hash" },
                )

            assertTrue(client.tokenProvider == null)
            client.forConsumer(IntegrityConsumer.Summarize).warmUp()
            assertEquals(Result.success(tokenProvider), client.tokenProvider)
        }

    @Test
    fun `GIVEN a warmup times out THEN false is returned`() = runTest {
        val tokenProvider = TokenProvider { _ ->
            Result.success(IntegrityToken("test-token"))
        }

        val client =
            GooglePlayIntegrityClient(
                tokenProviderFactory = {
                    delay(WarmUpTimeout * 2)
                    Result.success(tokenProvider)
                },
                requestHashProvider = { "test-hash" },
            )

        val warmUpSucceeded = client.warmUp()

        assertFalse("Warm up should not succeed", warmUpSucceeded)
    }

    @Test
    fun `GIVEN a warmup times out THEN the warmedUp event is recorded as unsuccessful and timeout value as the duration`() =
        runTest {
            val tokenProvider = TokenProvider { _ ->
                Result.success(IntegrityToken("test-token"))
            }
            val client =
                GooglePlayIntegrityClient(
                    tokenProviderFactory = {
                        delay(WarmUpTimeout * 2)
                        Result.success(tokenProvider)
                    },
                    requestHashProvider = { "test-hash" },
                    currentTimeMillis = { testScheduler.currentTime },
                )

            client.warmUp()

            val eventExtra = Integrity.warmedUp.testGetValue()!!.last().extra
            assertEquals("false", eventExtra?.get("success"))
            assertEquals(WarmUpTimeout.inWholeMilliseconds.toString(), eventExtra?.get("duration_ms"))
        }

    @Test
    fun `GIVEN a successful warmup is delayed for less than timeout value THEN warm up result is successful`() =
        runTest {
            val tokenProvider = TokenProvider { _ ->
                Result.success(IntegrityToken("test-token"))
            }

            val client =
                GooglePlayIntegrityClient(
                    tokenProviderFactory = {
                        delay(WarmUpTimeout / 2)
                        Result.success(tokenProvider)
                    },
                    requestHashProvider = { "test-hash" },
                )

            val warmUpSucceeded = client.warmUp()

            assertTrue("Warm up should succeed", warmUpSucceeded)
        }

    @Test
    fun `GIVEN an expired tokenProvider WHEN request is called THEN we get a tokenProvider from the factory`() =
        runTest {
            val exception: StandardIntegrityException = mock {
                `when`(this.errorCode).thenReturn(INTEGRITY_TOKEN_PROVIDER_INVALID)
            }

            val expiredTokenProvider = TokenProvider { _ -> Result.failure(exception) }
            val tokenProvider = TokenProvider { _ -> Result.success(IntegrityToken("test-token")) }

            val tokenProviders = mutableListOf(expiredTokenProvider, tokenProvider)
            val client =
                GooglePlayIntegrityClient(
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
    fun `GIVEN persistent provider expiry WHEN request is called THEN it retries 5 times before returning success`() =
        runTest {
            val exception: StandardIntegrityException = mock {
                `when`(this.errorCode).thenReturn(INTEGRITY_TOKEN_PROVIDER_INVALID)
            }
            var factoryCalls = 0
            val alwaysExpiring = TokenProvider { _ ->
                if (factoryCalls < 5) {
                    Result.failure(exception)
                } else {
                    Result.success(IntegrityToken("test-value"))
                }
            }
            val client =
                GooglePlayIntegrityClient(
                    {
                        factoryCalls++
                        Result.success(alwaysExpiring)
                    },
                    { "test-hash" },
                )

            val result = client.request()

            // We shouldn't fail because our final request should return a result after 5 retries.
            assertFalse(result.isFailure)
            assertEquals(5, factoryCalls)
        }

    @Test
    fun `GIVEN an exception from a TokenProvider that isn't INTEGRITY_TOKEN_PROVIDER_INVALID WHEN request is called THEN return the result`() =
        runTest {
            val tokenProvider = TokenProvider { _ -> Result.failure(IllegalStateException("test exception")) }
            val client =
                GooglePlayIntegrityClient(
                    { Result.success(tokenProvider) },
                    { "test-hash" },
                )

            val result = client.request()
            assertTrue(result.isFailure)
        }

    @Test
    fun `WHEN request is called directly THEN token_request event records consumer as unknown`() = runTest {
        val tokenProvider = TokenProvider { _ -> Result.success(IntegrityToken("test-token")) }
        val client =
            GooglePlayIntegrityClient(
                { Result.success(tokenProvider) },
                { "test-hash" },
            )

        client.request()

        val events = Integrity.tokenRequest.testGetValue()!!
        assertEquals(1, events.size)
        assertEquals("unknown", events.last().extra?.get("consumer"))
    }

    @Test
    fun `WHEN forConsumer Summarize issues a request THEN token_request event records consumer as summarize`() =
        runTest {
            val tokenProvider = TokenProvider { _ -> Result.success(IntegrityToken("test-token")) }
            val client =
                GooglePlayIntegrityClient(
                    { Result.success(tokenProvider) },
                    { "test-hash" },
                )

            client.forConsumer(IntegrityConsumer.Summarize).request()

            val events = Integrity.tokenRequest.testGetValue()!!
            assertEquals(1, events.size)
            assertEquals("summarize", events.last().extra?.get("consumer"))
        }

    @Test
    fun `WHEN forConsumer IpProtection issues a request THEN token_request event records consumer as ip_protection`() =
        runTest {
            val tokenProvider = TokenProvider { _ -> Result.success(IntegrityToken("test-token")) }
            val client =
                GooglePlayIntegrityClient(
                    { Result.success(tokenProvider) },
                    { "test-hash" },
                )

            client.forConsumer(IntegrityConsumer.IpProtection).request()

            val events = Integrity.tokenRequest.testGetValue()!!
            assertEquals(1, events.size)
            assertEquals("ip_protection", events.last().extra?.get("consumer"))
        }

    @Test
    fun `GIVEN two consumer views WHEN each issues a request THEN both events are tagged with their respective consumer`() =
        runTest {
            val tokenProvider = TokenProvider { _ -> Result.success(IntegrityToken("test-token")) }
            val client =
                GooglePlayIntegrityClient(
                    { Result.success(tokenProvider) },
                    { "test-hash" },
                )

            client.forConsumer(IntegrityConsumer.Summarize).request()
            client.forConsumer(IntegrityConsumer.IpProtection).request()

            val events = Integrity.tokenRequest.testGetValue()!!
            assertEquals(2, events.size)
            assertEquals("summarize", events[0].extra?.get("consumer"))
            assertEquals("ip_protection", events[1].extra?.get("consumer"))
        }
}
