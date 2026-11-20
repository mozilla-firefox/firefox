/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.metrics

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import androidx.work.testing.TestListenableWorkerBuilder
import com.android.installreferrer.api.InstallReferrerClient
import com.android.installreferrer.api.InstallReferrerStateListener
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.async
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.yield
import mozilla.components.support.test.robolectric.testContext
import mozilla.telemetry.glean.Glean
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.MetaAttribution
import org.mozilla.fenix.GleanMetrics.Pings
import org.mozilla.fenix.GleanMetrics.PlayStoreAttribution
import org.mozilla.fenix.GleanMetrics.Referrals
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.mozilla.fenix.nimbus.FxNimbus
import org.mozilla.fenix.nimbus.PairingSigninPrompt
import org.mozilla.fenix.utils.Settings
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class InstallReferrerWorkerTest {
    val context: Context = ApplicationProvider.getApplicationContext()

    @get:Rule val gleanTestRule = FenixGleanTestRule(testContext)

    @After
    fun tearDown() {
        FxNimbus.features.pairingSigninPrompt.withCachedValue(null)
    }

    @Test
    fun `WHEN retrieving minimum UTM params from setting THEN result should match`() {
        val settings = Settings(context)
        val expected = UTMParams(source = "", medium = "", campaign = "", content = "", term = "")
        val observed = UTMParams.fromSettings(settings)

        assertEquals(observed, expected)
        assertTrue(observed.isEmpty())
    }

    @Test
    fun `WHEN retrieving maximum UTM params from setting THEN result should match`() {
        val expected =
            UTMParams(source = "source", medium = "medium", campaign = "campaign", content = "content", term = "term")
        val settings = Settings(context)

        expected.intoSettings(settings)
        val observed = UTMParams.fromSettings(settings)

        assertEquals(observed, expected)

        assertFalse(observed.isEmpty())
    }

    @Test
    fun `WHEN parsing referrer response with no UTM params from setting THEN UTM params in settings should set to empty strings`() {
        val settings = Settings(context)
        val params = UTMParams.parseUTMParameters("")
        params.recordInstallReferrer(settings)

        val expected = UTMParams(source = "", medium = "", campaign = "", content = "", term = "")
        val observed = UTMParams.fromSettings(settings)
        assertEquals(observed, expected)

        assertNull(PlayStoreAttribution.source.testGetValue())
        assertNull(PlayStoreAttribution.medium.testGetValue())
        assertNull(PlayStoreAttribution.campaign.testGetValue())
        assertNull(PlayStoreAttribution.content.testGetValue())
        assertNull(PlayStoreAttribution.term.testGetValue())

        assertTrue(observed.isEmpty())
    }

    @Test
    fun `WHEN parsing referrer response with partial UTM params from setting THEN UTM params in settings should match expected`() {
        val settings = Settings(context)
        val params = UTMParams.parseUTMParameters("utm_campaign=CAMPAIGN")
        params.recordInstallReferrer(settings)

        val expected = UTMParams(source = "", medium = "", campaign = "CAMPAIGN", content = "", term = "")
        val observed = UTMParams.fromSettings(settings)
        assertEquals(observed, expected)

        assertEquals("", PlayStoreAttribution.source.testGetValue())
        assertEquals("", PlayStoreAttribution.medium.testGetValue())
        assertEquals("CAMPAIGN", PlayStoreAttribution.campaign.testGetValue())
        assertEquals("", PlayStoreAttribution.content.testGetValue())
        assertEquals("", PlayStoreAttribution.term.testGetValue())

        assertFalse(observed.isEmpty())
    }

    @Test
    fun `WHEN parsing referrer response with full UTM params from setting THEN UTM params in settings should match expected`() {
        val settings = Settings(context)
        val params =
            UTMParams.parseUTMParameters(
                "utm_source=SOURCE&utm_medium=MEDIUM&utm_campaign=CAMPAIGN&utm_content=CONTENT&utm_term=TERM"
            )
        params.recordInstallReferrer(settings)

        val expected =
            UTMParams(source = "SOURCE", medium = "MEDIUM", campaign = "CAMPAIGN", content = "CONTENT", term = "TERM")
        val observed = UTMParams.fromSettings(settings)
        assertEquals(expected, observed)

        assertEquals("SOURCE", PlayStoreAttribution.source.testGetValue())
        assertEquals("MEDIUM", PlayStoreAttribution.medium.testGetValue())
        assertEquals("CAMPAIGN", PlayStoreAttribution.campaign.testGetValue())
        assertEquals("CONTENT", PlayStoreAttribution.content.testGetValue())
        assertEquals("TERM", PlayStoreAttribution.term.testGetValue())

        assertEquals("SOURCE", Glean.testGetAttribution().source)
        assertEquals("MEDIUM", Glean.testGetAttribution().medium)
        assertEquals("CAMPAIGN", Glean.testGetAttribution().campaign)
        assertEquals("CONTENT", Glean.testGetAttribution().content)
        assertEquals("TERM", Glean.testGetAttribution().term)

        assertFalse(observed.isEmpty())
    }

    @Test
    fun `WHEN recording install referrer with no UTM params THEN Glean attribution is not updated`() {
        val settings = Settings(context)
        val params = UTMParams.parseUTMParameters("")
        params.recordInstallReferrer(settings)

        assertNull(Glean.testGetAttribution().source)
    }

    @Test
    fun `WHEN receiving a Meta encrypted attribution THEN will decrypt correctly`() {
        val metaParams =
            MetaParams.extractMetaAttribution(
                """{"app":12345, "t":1234567890,"source":{"data":"DATA","nonce":"NONCE"}}"""
            )
        val expectedMetaParams = MetaParams("12345", "1234567890", "DATA", "NONCE")

        assertEquals(metaParams, expectedMetaParams)
    }

    @Test
    fun `WHEN receiving a Meta encrypted attribution in percent format THEN will decrypt correctly`() {
        val metaParams =
            MetaParams.extractMetaAttribution(
                "%7B%22app%22%3A12345%2C%22t%22%3A1234567890%2C%22source%22%3A%7B%22data%22%3A%22DATA%22%2C%22nonce%22%3A%22NONCE%22%7D%7D"
            )
        val expectedMetaParams = MetaParams("12345", "1234567890", "DATA", "NONCE")

        assertEquals(metaParams, expectedMetaParams)
    }

    @Test
    fun `WHEN receiving a Meta encrypted attribution in bad format THEN it should not crash`() {
        val metaParams =
            MetaParams.extractMetaAttribution(
                "%7B%22app%22%3A12345%2C%22t%22%3A1234567890%2C%22source%22%3A%7B%22data%22%3A%22DATA%22%2C%22nonce%22%3A%22NONCE%22%7B%7D"
            )

        assertNull(metaParams)
    }

    @Test
    fun `WHEN receiving a null or empty attribution THEN it should return null`() {
        assertNull(MetaParams.extractMetaAttribution(null))
        assertNull(MetaParams.extractMetaAttribution(""))
        assertNull(MetaParams.extractMetaAttribution("    "))
    }

    @Test
    fun `WHEN a referrer value contains an equals sign THEN it is preserved`() {
        val params = UTMParams.parseInstallReferrer("k=val=ue&other=plain")

        assertEquals("val=ue", params["k"])
        assertEquals("plain", params["other"])
    }

    @Test
    fun `WHEN a referrer carries a base64 padded value THEN the padding is preserved`() {
        val params = UTMParams.parseInstallReferrer("""utm_content={"data":"abc=="}""")

        assertEquals("""{"data":"abc=="}""", params["utm_content"])
    }

    @Test
    fun `WHEN a referrer segment is malformed THEN sibling params still parse`() {
        val params = UTMParams.parseInstallReferrer("k1=v1&malformed&k2=v2")

        assertEquals("v1", params["k1"])
        assertEquals("v2", params["k2"])
        assertFalse(params.containsKey("malformed"))
    }

    @Test
    fun `WHEN the referrer response is empty THEN an empty map is returned`() {
        assertEquals(emptyMap<String, String>(), UTMParams.parseInstallReferrer(""))
    }

    @Test
    fun `WHEN utm_content contains an equals sign THEN it is preserved on UTMParams`() {
        val utmParams = UTMParams.parseUTMParameters("utm_source=foo&utm_content=abc==")

        assertEquals("foo", utmParams.source)
        assertEquals("abc==", utmParams.content)
    }

    @Test
    fun `WHEN parsing referrer response with meta attribution THEN both UTM and Meta params should match expected`() {
        val utmParams =
            UTMParams.parseUTMParameters(
                """utm_content={"app":12345, "t":1234567890,"source":{"data":"DATA","nonce":"NONCE"}}"""
            )
        val expectedUtmParams =
            UTMParams(
                source = "",
                medium = "",
                campaign = "",
                content = """{"app":12345, "t":1234567890,"source":{"data":"DATA","nonce":"NONCE"}}""",
                term = "",
            )

        assertEquals(utmParams, expectedUtmParams)

        val metaParams = MetaParams.extractMetaAttribution(utmParams.content)
        val expectedMetaParams = MetaParams("12345", "1234567890", "DATA", "NONCE")

        assertEquals(metaParams, expectedMetaParams)
    }

    @Test
    fun `WHEN recording Meta attribution THEN correct values should be recorded to telemetry`() {
        // The data and nonce are from Meta's example https://developers.facebook.com/docs/app-ads/install-referrer/
        val metaParams =
            MetaParams(
                "12345",
                "1234567890",
                "afe56cf6228c6ea8c79da49186e718e92a579824596ae1d0d4d20d7793dca797bd4034ccf467bfae5c79a3981e7a2968c41949237e2b2db678c1c3d39c9ae564c5cafd52f2b77a3dc77bf1bae063114d0283b97417487207735da31ddc1531d5645a9c3e602c195a0ebf69c272aa5fda3a2d781cb47e117310164715a54c7a5a032740584e2789a7b4e596034c16425139a77e507c492b629c848573c714a03a2e7d25b9459b95842332b460f3682d19c35dbc7d53e3a51e0497ff6a6cbb367e760debc4194ae097498108df7b95eac2fa9bac4320077b510be3b7b823248bfe02ae501d9fe4ba179c7de6733c92bf89d523df9e31238ef497b9db719484cbab7531dbf6c5ea5a8087f95d59f5e4f89050e0f1dc03e464168ad76a64cca64b79",
                "b7203c6a6fb633d16e9cf5c1",
            )

        assertNull(MetaAttribution.app.testGetValue())
        assertNull(MetaAttribution.t.testGetValue())
        assertNull(MetaAttribution.data.testGetValue())
        assertNull(MetaAttribution.nonce.testGetValue())
        metaParams.recordMetaAttribution()

        val expectedApp = "12345"
        val expectedT = "1234567890"
        val expectedData =
            "afe56cf6228c6ea8c79da49186e718e92a579824596ae1d0d4d20d7793dca797bd4034ccf467bfae5c79a3981e7a2968c41949237e2b2db678c1c3d39c9ae564c5cafd52f2b77a3dc77bf1bae063114d0283b97417487207735da31ddc1531d5645a9c3e602c195a0ebf69c272aa5fda3a2d781cb47e117310164715a54c7a5a032740584e2789a7b4e596034c16425139a77e507c492b629c848573c714a03a2e7d25b9459b95842332b460f3682d19c35dbc7d53e3a51e0497ff6a6cbb367e760debc4194ae097498108df7b95eac2fa9bac4320077b510be3b7b823248bfe02ae501d9fe4ba179c7de6733c92bf89d523df9e31238ef497b9db719484cbab7531dbf6c5ea5a8087f95d59f5e4f89050e0f1dc03e464168ad76a64cca64b79"
        val expectedNonce = "b7203c6a6fb633d16e9cf5c1"

        val recordedApp = MetaAttribution.app.testGetValue()
        assertEquals(recordedApp, expectedApp)
        val recordedT = MetaAttribution.t.testGetValue()
        assertEquals(recordedT, expectedT)
        val recordedData = MetaAttribution.data.testGetValue()
        assertEquals(recordedData, expectedData)
        val recordedNonce = MetaAttribution.nonce.testGetValue()
        assertEquals(recordedNonce, expectedNonce)
    }

    @Test
    fun `WHEN MAX_RETRIES is 3 THEN allows exactly 3 retries`() {
        assertTrue(InstallReferrerWorker.shouldRetry(0))
        assertTrue(InstallReferrerWorker.shouldRetry(1))
        assertTrue(InstallReferrerWorker.shouldRetry(2))
        assertFalse(InstallReferrerWorker.shouldRetry(3))
    }

    @Test
    fun `WHEN install referrer connection succeeds THEN returns OK with referrer response`() = runTest {
        val expectedReferrer = "utm_source=test&utm_medium=organic"
        val fakeClient =
            FakeInstallReferrerClient(
                responseCode = InstallReferrerClient.InstallReferrerResponse.OK,
                referrerResponse = expectedReferrer,
            )

        val (responseCode, referrerResponse) = InstallReferrerWorker.fetchInstallReferrer(fakeClient)

        assertEquals(InstallReferrerClient.InstallReferrerResponse.OK, responseCode)
        assertEquals(expectedReferrer, referrerResponse)
        assertTrue(fakeClient.connectionStarted)
        assertTrue(fakeClient.connectionEnded)
    }

    @Test
    fun `WHEN install referrer returns SERVICE_UNAVAILABLE THEN returns SERVICE_UNAVAILABLE with null response`() =
        runTest {
            val fakeClient =
                FakeInstallReferrerClient(
                    responseCode = InstallReferrerClient.InstallReferrerResponse.SERVICE_UNAVAILABLE
                )

            val (responseCode, referrerResponse) = InstallReferrerWorker.fetchInstallReferrer(fakeClient)

            assertEquals(InstallReferrerClient.InstallReferrerResponse.SERVICE_UNAVAILABLE, responseCode)
            assertNull(referrerResponse)
            assertTrue(fakeClient.connectionStarted)
            assertTrue(fakeClient.connectionEnded)
        }

    @Test
    fun `WHEN install referrer returns FEATURE_NOT_SUPPORTED THEN returns error with null response`() = runTest {
        val fakeClient =
            FakeInstallReferrerClient(
                responseCode = InstallReferrerClient.InstallReferrerResponse.FEATURE_NOT_SUPPORTED
            )

        val (responseCode, referrerResponse) = InstallReferrerWorker.fetchInstallReferrer(fakeClient)

        assertEquals(InstallReferrerClient.InstallReferrerResponse.FEATURE_NOT_SUPPORTED, responseCode)
        assertNull(referrerResponse)
        assertTrue(fakeClient.connectionStarted)
        assertTrue(fakeClient.connectionEnded)
    }

    @Test
    fun `WHEN service disconnects THEN returns SERVICE_UNAVAILABLE with null response and ends connection`() = runTest {
        val fakeClient = FakeInstallReferrerClient(simulateDisconnect = true)

        val (responseCode, referrerResponse) = InstallReferrerWorker.fetchInstallReferrer(fakeClient)

        assertEquals(InstallReferrerClient.InstallReferrerResponse.SERVICE_UNAVAILABLE, responseCode)
        assertNull(referrerResponse)
        assertTrue(fakeClient.connectionStarted)
        assertTrue(fakeClient.connectionEnded)
    }

    @Test
    fun `WHEN getInstallReferrer throws RemoteException THEN returns OK with null response`() = runTest {
        val fakeClient =
            FakeInstallReferrerClient(
                responseCode = InstallReferrerClient.InstallReferrerResponse.OK,
                throwRemoteException = true,
            )

        val (responseCode, referrerResponse) = InstallReferrerWorker.fetchInstallReferrer(fakeClient)

        assertEquals(InstallReferrerClient.InstallReferrerResponse.OK, responseCode)
        assertNull(referrerResponse)
        assertTrue(fakeClient.connectionStarted)
        assertTrue(fakeClient.connectionEnded)
    }

    @Test
    fun `WHEN getInstallReferrer throws SecurityException THEN returns OK with null response`() = runTest {
        val fakeClient =
            FakeInstallReferrerClient(
                responseCode = InstallReferrerClient.InstallReferrerResponse.OK,
                throwSecurityException = true,
            )

        val (responseCode, referrerResponse) = InstallReferrerWorker.fetchInstallReferrer(fakeClient)

        assertEquals(InstallReferrerClient.InstallReferrerResponse.OK, responseCode)
        assertNull(referrerResponse)
        assertTrue(fakeClient.connectionStarted)
        assertTrue(fakeClient.connectionEnded)
    }

    @Test
    fun `WHEN coroutine is cancelled THEN endConnection is called`() = runTest {
        val fakeClient =
            FakeInstallReferrerClient(
                responseCode = InstallReferrerClient.InstallReferrerResponse.OK,
                referrerResponse = "test",
                delayCallback = true,
            )

        val job =
            launch(start = CoroutineStart.UNDISPATCHED) {
                try {
                    InstallReferrerWorker.fetchInstallReferrer(fakeClient)
                } catch (e: CancellationException) {
                    throw e
                }
            }

        assertTrue(fakeClient.connectionStarted)
        assertFalse(fakeClient.connectionEnded)

        job.cancel()
        job.join()
        yield()

        assertTrue(fakeClient.connectionEnded)
    }

    @Test
    fun `GIVEN client throws IllegalStateException WHEN setup finishes OK THEN returns SERVICE_UNAVAILABLE with null response and ends connection`() =
        runTest {
            val fakeClient =
                FakeInstallReferrerClient(
                    responseCode = InstallReferrerClient.InstallReferrerResponse.OK,
                    throwIllegalStateException = true,
                )

            val (responseCode, referrerResponse) = InstallReferrerWorker.fetchInstallReferrer(fakeClient)

            assertEquals(InstallReferrerClient.InstallReferrerResponse.SERVICE_UNAVAILABLE, responseCode)
            assertNull(referrerResponse)
            assertTrue(fakeClient.connectionEnded)
        }

    @Test
    fun `GIVEN the coroutine was cancelled WHEN setup finishes afterwards THEN getInstallReferrer is not called`() =
        runTest {
            val fakeClient =
                FakeInstallReferrerClient(
                    responseCode = InstallReferrerClient.InstallReferrerResponse.OK,
                    throwIllegalStateException = true,
                    delayCallback = true,
                )

            val job =
                launch(start = CoroutineStart.UNDISPATCHED) { InstallReferrerWorker.fetchInstallReferrer(fakeClient) }
            job.cancel()
            job.join()

            fakeClient.fireSetupFinished()

            assertFalse(fakeClient.getInstallReferrerCalled)
        }

    @Test
    fun `GIVEN setup already finished WHEN service disconnects afterwards THEN the original result is kept`() =
        runTest {
            val fakeClient =
                FakeInstallReferrerClient(
                    responseCode = InstallReferrerClient.InstallReferrerResponse.OK,
                    referrerResponse = "test",
                    delayCallback = true,
                )

            val result =
                async(start = CoroutineStart.UNDISPATCHED) { InstallReferrerWorker.fetchInstallReferrer(fakeClient) }
            fakeClient.fireSetupFinished()
            fakeClient.fireServiceDisconnected()

            assertEquals(Pair(InstallReferrerClient.InstallReferrerResponse.OK, "test"), result.await())
        }

    @Test
    fun `GIVEN service disconnected WHEN setup finishes again after a rebind THEN the connection is ended`() = runTest {
        val fakeClient =
            FakeInstallReferrerClient(
                responseCode = InstallReferrerClient.InstallReferrerResponse.OK,
                referrerResponse = "test",
                delayCallback = true,
            )

        val result =
            async(start = CoroutineStart.UNDISPATCHED) { InstallReferrerWorker.fetchInstallReferrer(fakeClient) }
        fakeClient.fireServiceDisconnected()
        fakeClient.connectionEnded = false

        fakeClient.fireSetupFinished()

        assertEquals(Pair(InstallReferrerClient.InstallReferrerResponse.SERVICE_UNAVAILABLE, null), result.await())
        assertTrue(fakeClient.connectionEnded)
        assertFalse(fakeClient.getInstallReferrerCalled)
    }

    @Test
    fun `WHEN handleSuccess receives a Meta attribution referrer THEN isMetaAttribution is set to true`() {
        val worker = TestListenableWorkerBuilder<InstallReferrerWorker>(context).build()
        val settings = Settings(context)
        val metaReferrer = """utm_content={"app":12345,"t":1234567890,"source":{"data":"DATA","nonce":"NONCE"}}"""

        worker.handleSuccess(metaReferrer, InstallReferrerClient.InstallReferrerResponse.OK, settings)

        assertTrue(settings.isUserMetaAttributed)
    }

    @Test
    fun `WHEN handleSuccess receives a non-Meta referrer THEN isMetaAttribution is set to false`() {
        val worker = TestListenableWorkerBuilder<InstallReferrerWorker>(context).build()
        val settings = Settings(context)
        settings.isUserMetaAttributed = true

        worker.handleSuccess(
            "utm_source=google&utm_medium=cpc",
            InstallReferrerClient.InstallReferrerResponse.OK,
            settings,
        )

        assertFalse(settings.isUserMetaAttributed)
    }

    @Test
    fun `WHEN handleSuccess receives a null referrer THEN isMetaAttribution is not changed`() {
        val worker = TestListenableWorkerBuilder<InstallReferrerWorker>(context).build()
        val settings = Settings(context)
        settings.isUserMetaAttributed = true

        worker.handleSuccess(null, InstallReferrerClient.InstallReferrerResponse.OK, settings)

        assertTrue(settings.isUserMetaAttributed)
    }

    @Test
    fun `WHEN handleSuccess receives a TikTok-attributed referrer THEN isUserTikTokAttributed is set to true`() {
        val worker = TestListenableWorkerBuilder<InstallReferrerWorker>(context).build()
        val settings = Settings(context)
        val referrer = "&adjust_external_click_id=E.C.P.C.04.AAA&utm_medium=paid"

        worker.handleSuccess(referrer, InstallReferrerClient.InstallReferrerResponse.OK, settings)

        assertTrue(settings.isUserTikTokAttributed)
    }

    @Test
    fun `WHEN handleSuccess receives a non-TikTok referrer THEN isUserTikTokAttributed is set to false`() {
        val worker = TestListenableWorkerBuilder<InstallReferrerWorker>(context).build()
        val settings = Settings(context)
        settings.isUserTikTokAttributed = true

        worker.handleSuccess(
            "utm_source=google&utm_medium=cpc",
            InstallReferrerClient.InstallReferrerResponse.OK,
            settings,
        )

        assertFalse(settings.isUserTikTokAttributed)
    }

    @Test
    fun `WHEN handleSuccess receives a null referrer THEN isUserTikTokAttributed is not changed`() {
        val worker = TestListenableWorkerBuilder<InstallReferrerWorker>(context).build()
        val settings = Settings(context)
        settings.isUserTikTokAttributed = true

        worker.handleSuccess(null, InstallReferrerClient.InstallReferrerResponse.OK, settings)

        assertTrue(settings.isUserTikTokAttributed)
    }

    @Test
    fun `WHEN handleSuccess receives a Reddit-attributed referrer THEN isUserRedditAttributed is set to true`() {
        val worker = TestListenableWorkerBuilder<InstallReferrerWorker>(context).build()
        val settings = Settings(context)
        val referrer = "adjust_external_click_id=reddit_abc123&utm_medium=paid"

        worker.handleSuccess(referrer, InstallReferrerClient.InstallReferrerResponse.OK, settings)

        assertTrue(settings.isUserRedditAttributed)
    }

    @Test
    fun `WHEN handleSuccess receives a non-Reddit referrer THEN isUserRedditAttributed is set to false`() {
        val worker = TestListenableWorkerBuilder<InstallReferrerWorker>(context).build()
        val settings = Settings(context)
        settings.isUserRedditAttributed = true

        worker.handleSuccess(
            "utm_source=google&utm_medium=cpc",
            InstallReferrerClient.InstallReferrerResponse.OK,
            settings,
        )

        assertFalse(settings.isUserRedditAttributed)
    }

    @Test
    fun `WHEN handleSuccess receives a null referrer THEN isUserRedditAttributed is not changed`() {
        val worker = TestListenableWorkerBuilder<InstallReferrerWorker>(context).build()
        val settings = Settings(context)
        settings.isUserRedditAttributed = true

        worker.handleSuccess(null, InstallReferrerClient.InstallReferrerResponse.OK, settings)

        assertTrue(settings.isUserRedditAttributed)
    }

    @Test
    fun `WHEN handleSuccess receives an X-attributed referrer THEN isUserXTwitterAttributed is set to true`() {
        val worker = TestListenableWorkerBuilder<InstallReferrerWorker>(context).build()
        val settings = Settings(context)
        val referrer = "utm_source=x&utm_medium=paid"

        worker.handleSuccess(referrer, InstallReferrerClient.InstallReferrerResponse.OK, settings)

        assertTrue(settings.isUserXTwitterAttributed)
    }

    @Test
    fun `WHEN handleSuccess receives a non-X referrer THEN isUserXTwitterAttributed is set to false`() {
        val worker = TestListenableWorkerBuilder<InstallReferrerWorker>(context).build()
        val settings = Settings(context)
        settings.isUserXTwitterAttributed = true

        worker.handleSuccess(
            "utm_source=google&utm_medium=cpc",
            InstallReferrerClient.InstallReferrerResponse.OK,
            settings,
        )

        assertFalse(settings.isUserXTwitterAttributed)
    }

    @Test
    fun `WHEN handleSuccess receives a null referrer THEN isUserXTwitterAttributed is not changed`() {
        val worker = TestListenableWorkerBuilder<InstallReferrerWorker>(context).build()
        val settings = Settings(context)
        settings.isUserXTwitterAttributed = true

        worker.handleSuccess(null, InstallReferrerClient.InstallReferrerResponse.OK, settings)

        assertTrue(settings.isUserXTwitterAttributed)
    }

    @Test
    fun `WHEN handleSuccess receives a Moloco-attributed referrer THEN isUserMolocoAttributed is set to true`() {
        val worker = TestListenableWorkerBuilder<InstallReferrerWorker>(context).build()
        val settings = Settings(context)
        val referrer = "adjust_external_click_id=moloco_9b1deb4d-3b7d-4bad-9bdd-2b0d7b3d41a6&utm_medium=paid"

        worker.handleSuccess(referrer, InstallReferrerClient.InstallReferrerResponse.OK, settings)

        assertTrue(settings.isUserMolocoAttributed)
    }

    @Test
    fun `WHEN handleSuccess receives a non-Moloco referrer THEN isUserMolocoAttributed is set to false`() {
        val worker = TestListenableWorkerBuilder<InstallReferrerWorker>(context).build()
        val settings = Settings(context)
        settings.isUserMolocoAttributed = true

        worker.handleSuccess(
            "utm_source=google&utm_medium=cpc",
            InstallReferrerClient.InstallReferrerResponse.OK,
            settings,
        )

        assertFalse(settings.isUserMolocoAttributed)
    }

    @Test
    fun `WHEN handleSuccess receives a null referrer THEN isUserMolocoAttributed is not changed`() {
        val worker = TestListenableWorkerBuilder<InstallReferrerWorker>(context).build()
        val settings = Settings(context)
        settings.isUserMolocoAttributed = true

        worker.handleSuccess(null, InstallReferrerClient.InstallReferrerResponse.OK, settings)

        assertTrue(settings.isUserMolocoAttributed)
    }

    @Test
    fun `WHEN handleSuccess receives a Rakuten-attributed referrer THEN isUserRakutenAttributed is set to true`() {
        val worker = TestListenableWorkerBuilder<InstallReferrerWorker>(context).build()
        val settings = Settings(context)
        val referrer = "utm_source=Rakuten&utm_medium=paid"

        worker.handleSuccess(referrer, InstallReferrerClient.InstallReferrerResponse.OK, settings)

        assertTrue(settings.isUserRakutenAttributed)
    }

    @Test
    fun `WHEN handleSuccess receives a non-Rakuten referrer THEN isUserRakutenAttributed is set to false`() {
        val worker = TestListenableWorkerBuilder<InstallReferrerWorker>(context).build()
        val settings = Settings(context)
        settings.isUserRakutenAttributed = true

        worker.handleSuccess(
            "utm_source=google&utm_medium=cpc",
            InstallReferrerClient.InstallReferrerResponse.OK,
            settings,
        )

        assertFalse(settings.isUserRakutenAttributed)
    }

    @Test
    fun `WHEN handleSuccess receives a null referrer THEN isUserRakutenAttributed is not changed`() {
        val worker = TestListenableWorkerBuilder<InstallReferrerWorker>(context).build()
        val settings = Settings(context)
        settings.isUserRakutenAttributed = true

        worker.handleSuccess(null, InstallReferrerClient.InstallReferrerResponse.OK, settings)

        assertTrue(settings.isUserRakutenAttributed)
    }

    @Test
    fun `WHEN handleSuccess receives a Skyflag-attributed referrer THEN isUserSkyflagAttributed is set to true`() {
        val worker = TestListenableWorkerBuilder<InstallReferrerWorker>(context).build()
        val settings = Settings(context)
        val referrer = "utm_source=skyflag&utm_medium=paid"

        worker.handleSuccess(referrer, InstallReferrerClient.InstallReferrerResponse.OK, settings)

        assertTrue(settings.isUserSkyflagAttributed)
    }

    @Test
    fun `WHEN handleSuccess receives a non-Skyflag referrer THEN isUserSkyflagAttributed is set to false`() {
        val worker = TestListenableWorkerBuilder<InstallReferrerWorker>(context).build()
        val settings = Settings(context)
        settings.isUserSkyflagAttributed = true

        worker.handleSuccess(
            "utm_source=google&utm_medium=cpc",
            InstallReferrerClient.InstallReferrerResponse.OK,
            settings,
        )

        assertFalse(settings.isUserSkyflagAttributed)
    }

    @Test
    fun `WHEN handleSuccess receives a null referrer THEN isUserSkyflagAttributed is not changed`() {
        val worker = TestListenableWorkerBuilder<InstallReferrerWorker>(context).build()
        val settings = Settings(context)
        settings.isUserSkyflagAttributed = true

        worker.handleSuccess(null, InstallReferrerClient.InstallReferrerResponse.OK, settings)

        assertTrue(settings.isUserSkyflagAttributed)
    }

    @Test
    fun `WHEN handleSuccess receives a Quora-attributed referrer THEN isUserQuoraAttributed is set to true`() {
        val worker = TestListenableWorkerBuilder<InstallReferrerWorker>(context).build()
        val settings = Settings(context)
        val referrer = "utm_source=quora&utm_medium=paid"

        worker.handleSuccess(referrer, InstallReferrerClient.InstallReferrerResponse.OK, settings)

        assertTrue(settings.isUserQuoraAttributed)
    }

    @Test
    fun `WHEN handleSuccess receives a non-Quora referrer THEN isUserQuoraAttributed is set to false`() {
        val worker = TestListenableWorkerBuilder<InstallReferrerWorker>(context).build()
        val settings = Settings(context)
        settings.isUserQuoraAttributed = true

        worker.handleSuccess(
            "utm_source=google&utm_medium=cpc",
            InstallReferrerClient.InstallReferrerResponse.OK,
            settings,
        )

        assertFalse(settings.isUserQuoraAttributed)
    }

    @Test
    fun `WHEN handleSuccess receives a null referrer THEN isUserQuoraAttributed is not changed`() {
        val worker = TestListenableWorkerBuilder<InstallReferrerWorker>(context).build()
        val settings = Settings(context)
        settings.isUserQuoraAttributed = true

        worker.handleSuccess(null, InstallReferrerClient.InstallReferrerResponse.OK, settings)

        assertTrue(settings.isUserQuoraAttributed)
    }

    @Test
    fun `GIVEN a referral referrer WHEN handleSuccess runs THEN the referrals ping is submitted with the code`() {
        val worker = TestListenableWorkerBuilder<InstallReferrerWorker>(context).build()
        val settings = Settings(context)
        val job =
            Pings.referrals.testBeforeNextSubmit {
                assertEquals(REFERRAL_CODE, Referrals.code.testGetValue())
            }

        worker.handleSuccess(referralReferrer(), InstallReferrerClient.InstallReferrerResponse.OK, settings)

        job.join()
    }

    @Test
    fun `GIVEN a referral referrer WHEN handleSuccess runs THEN the code does not reach attribution telemetry`() {
        val worker = TestListenableWorkerBuilder<InstallReferrerWorker>(context).build()
        val settings = Settings(context)
        val job =
            Pings.playStoreAttribution.testBeforeNextSubmit {
                // Empty rather than absent: recordInstallReferrer sets all five params. The raw
                // response drops utm_content entirely, so both fields agree that a referral install is
                // indistinguishable from one carrying no utm_content at all.
                assertEquals("", PlayStoreAttribution.content.testGetValue())
                assertEquals(
                    "utm_source=SOURCE&utm_term=TERM",
                    PlayStoreAttribution.installReferrerResponse.testGetValue(),
                )
            }

        worker.handleSuccess(referralReferrer(), InstallReferrerClient.InstallReferrerResponse.OK, settings)

        job.join()
        assertTrue(Glean.testGetAttribution().content.isNullOrEmpty())
        // utmContent is the only source for the nimbus ping's
        // install_referrer_response_utm_content and the equivalent JEXL targeting attribute.
        assertEquals("", settings.utmContent)
        // Recorded for the debug drawer only, and only because unit tests run against the debug build type.
        assertEquals(REFERRAL_CODE, settings.referralCode)
    }

    @Test
    fun `GIVEN a referral referrer WHEN handleSuccess runs THEN the other UTM params are still recorded`() {
        val worker = TestListenableWorkerBuilder<InstallReferrerWorker>(context).build()
        val settings = Settings(context)
        val job =
            Pings.playStoreAttribution.testBeforeNextSubmit {
                assertEquals("SOURCE", PlayStoreAttribution.source.testGetValue())
                assertEquals("TERM", PlayStoreAttribution.term.testGetValue())
            }

        worker.handleSuccess(referralReferrer(), InstallReferrerClient.InstallReferrerResponse.OK, settings)

        job.join()
        assertEquals("SOURCE", settings.utmSource)
    }

    @Test
    fun `GIVEN a referral-only referrer WHEN handleSuccess runs THEN no UTM params are recorded`() {
        val worker = TestListenableWorkerBuilder<InstallReferrerWorker>(context).build()
        val settings = Settings(context)
        val job =
            Pings.playStoreAttribution.testBeforeNextSubmit {
                assertNull(PlayStoreAttribution.source.testGetValue())
                assertNull(PlayStoreAttribution.content.testGetValue())
                assertEquals("", PlayStoreAttribution.installReferrerResponse.testGetValue())
            }

        worker.handleSuccess(
            "utm_content=$REFERRAL_UTM_CONTENT_PREFIX$REFERRAL_CODE",
            InstallReferrerClient.InstallReferrerResponse.OK,
            settings,
        )

        job.join()
        assertTrue(UTMParams.fromSettings(settings).isEmpty())
    }

    @Test
    fun `GIVEN a referral preamble in utm_campaign WHEN handleSuccess runs THEN no ping is submitted and the campaign is recorded`() {
        val worker = TestListenableWorkerBuilder<InstallReferrerWorker>(context).build()
        val settings = Settings(context)
        val referrer = "utm_campaign=$REFERRAL_UTM_CONTENT_PREFIX$REFERRAL_CODE"
        val attributionJob =
            Pings.playStoreAttribution.testBeforeNextSubmit {
                assertEquals(
                    "$REFERRAL_UTM_CONTENT_PREFIX$REFERRAL_CODE",
                    PlayStoreAttribution.campaign.testGetValue(),
                )
                assertEquals(referrer, PlayStoreAttribution.installReferrerResponse.testGetValue())
            }

        worker.handleSuccess(referrer, InstallReferrerClient.InstallReferrerResponse.OK, settings)

        attributionJob.join()
        assertNull(Referrals.code.testGetValue())
        assertFalse(settings.referralPingSubmitted)
    }

    @Test
    fun `GIVEN a referral referrer WHEN handleSuccess runs twice THEN the code is recorded once`() {
        val worker = TestListenableWorkerBuilder<InstallReferrerWorker>(context).build()
        val settings = Settings(context)
        val job =
            Pings.referrals.testBeforeNextSubmit {
                assertEquals(REFERRAL_CODE, Referrals.code.testGetValue())
            }

        worker.handleSuccess(referralReferrer(), InstallReferrerClient.InstallReferrerResponse.OK, settings)

        job.join()
        assertTrue(settings.referralPingSubmitted)

        worker.handleSuccess(referralReferrer(), InstallReferrerClient.InstallReferrerResponse.OK, settings)

        // referrals.code has ping lifetime, so the first submission cleared it. Still absent means
        // the second run never called set(), and so never submitted.
        assertNull(Referrals.code.testGetValue())
    }

    @Test
    fun `GIVEN a non-referral referrer WHEN handleSuccess runs THEN the raw referrer response is unchanged`() {
        val worker = TestListenableWorkerBuilder<InstallReferrerWorker>(context).build()
        val settings = Settings(context)
        val referrer = "utm_source=SOURCE&utm_content=CONTENT"
        val job =
            Pings.playStoreAttribution.testBeforeNextSubmit {
                assertEquals(referrer, PlayStoreAttribution.installReferrerResponse.testGetValue())
                assertEquals("CONTENT", PlayStoreAttribution.content.testGetValue())
            }

        worker.handleSuccess(referrer, InstallReferrerClient.InstallReferrerResponse.OK, settings)

        job.join()
        assertEquals("", settings.referralCode)
    }

    @Test
    fun `GIVEN a referrer with the pairing campaign WHEN handleSuccess is called THEN pairingCampaign is recorded as true`() {
        FxNimbus.features.pairingSigninPrompt.withCachedValue(PairingSigninPrompt(campaignUtmContent = "pairing123"))

        val worker = TestListenableWorkerBuilder<InstallReferrerWorker>(context).build()
        val settings = Settings(context)
        val referrer = "utm_source=accounts.firefox.com&utm_medium=referral&utm_content=pairing123"

        val job =
            Pings.playStoreAttribution.testBeforeNextSubmit {
                assertEquals(true, PlayStoreAttribution.pairingCampaign.testGetValue())
            }

        worker.handleSuccess(referrer, InstallReferrerClient.InstallReferrerResponse.OK, settings)

        job.join()
    }

    @Test
    fun `GIVEN a referrer with a campaign which is not the pairing campaign WHEN handleSuccess is called THEN pairingCampaign is recorded as false`() {
        FxNimbus.features.pairingSigninPrompt.withCachedValue(PairingSigninPrompt(campaignUtmContent = "pairing123"))

        val worker = TestListenableWorkerBuilder<InstallReferrerWorker>(context).build()
        val settings = Settings(context)
        val referrer = "utm_source=google&utm_medium=referral&utm_content=other_campaign"

        val job =
            Pings.playStoreAttribution.testBeforeNextSubmit {
                assertEquals(false, PlayStoreAttribution.pairingCampaign.testGetValue())
            }

        worker.handleSuccess(referrer, InstallReferrerClient.InstallReferrerResponse.OK, settings)

        job.join()
    }

    @Test
    fun `GIVEN a null referrer WHEN handleSuccess is called THEN pairingCampaign is not recorded`() {
        val worker = TestListenableWorkerBuilder<InstallReferrerWorker>(context).build()
        val settings = Settings(context)

        val job =
            Pings.playStoreAttribution.testBeforeNextSubmit {
                assertNull(PlayStoreAttribution.pairingCampaign.testGetValue())
            }

        worker.handleSuccess(null, InstallReferrerClient.InstallReferrerResponse.OK, settings)

        job.join()
    }

    private fun referralReferrer() =
        "utm_source=SOURCE&utm_content=$REFERRAL_UTM_CONTENT_PREFIX$REFERRAL_CODE&utm_term=TERM"
}

private const val REFERRAL_CODE = "0123456789ABCXYZ"

private class FakeInstallReferrerClient(
    private val responseCode: Int = InstallReferrerClient.InstallReferrerResponse.OK,
    private val referrerResponse: String? = null,
    private val simulateDisconnect: Boolean = false,
    private val throwRemoteException: Boolean = false,
    private val throwSecurityException: Boolean = false,
    private val throwIllegalStateException: Boolean = false,
    private val delayCallback: Boolean = false,
) : InstallReferrerClientWrapper {
    var connectionStarted = false
    var connectionEnded = false
    var getInstallReferrerCalled = false
    private var storedListener: InstallReferrerStateListener? = null

    fun fireSetupFinished() {
        storedListener?.onInstallReferrerSetupFinished(responseCode)
    }

    fun fireServiceDisconnected() {
        storedListener?.onInstallReferrerServiceDisconnected()
    }

    override fun startConnection(listener: InstallReferrerStateListener) {
        connectionStarted = true
        if (delayCallback) {
            storedListener = listener
        } else {
            if (simulateDisconnect) {
                listener.onInstallReferrerServiceDisconnected()
            } else {
                listener.onInstallReferrerSetupFinished(responseCode)
            }
        }
    }

    override fun getInstallReferrer(): String? {
        getInstallReferrerCalled = true
        if (throwIllegalStateException) {
            throw IllegalStateException("Service not connected. Please start a connection before using the service.")
        }
        if (throwRemoteException) {
            throw android.os.RemoteException("Test remote exception")
        }
        if (throwSecurityException) {
            throw SecurityException("Test security exception")
        }
        return referrerResponse
    }

    override fun endConnection() {
        connectionEnded = true
    }
}
