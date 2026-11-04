/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

package org.mozilla.geckoview.test

import android.net.Uri
import android.os.SystemClock
import androidx.test.filters.MediumTest
import androidx.test.platform.app.InstrumentationRegistry
import java.io.IOException
import java.math.BigInteger
import java.net.UnknownHostException
import java.nio.ByteBuffer
import java.nio.charset.Charset
import java.security.MessageDigest
import java.util.Random
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.runBlocking
import org.hamcrest.MatcherAssert.assertThat
import org.hamcrest.Matchers.equalTo
import org.hamcrest.Matchers.greaterThan
import org.hamcrest.Matchers.isEmptyOrNullString
import org.hamcrest.Matchers.not
import org.hamcrest.Matchers.notNullValue
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.assertThrows
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.junit.runners.Parameterized
import org.mozilla.gecko.util.ThreadUtils
import org.mozilla.geckoview.GeckoResult
import org.mozilla.geckoview.GeckoWebExecutor
import org.mozilla.geckoview.StorageController
import org.mozilla.geckoview.WebRequest
import org.mozilla.geckoview.WebRequestError
import org.mozilla.geckoview.WebResponse
import org.mozilla.geckoview.test.util.RuntimeCreator
import org.mozilla.geckoview.test.util.TestServer

@MediumTest
@RunWith(Parameterized::class)
class WebExecutorTest {
    companion object {
        const val TEST_PORT: Int = 4242
        const val TEST_ENDPOINT: String = "http://localhost:$TEST_PORT"

        @get:Parameterized.Parameters(name = "{0}")
        @JvmStatic
        val parameters: List<Array<out Any>> =
            listOf(
                arrayOf("#conservative"),
                arrayOf("#normal"),
            )
    }

    @field:Parameterized.Parameter(0) @JvmField var id: String = ""

    lateinit var executor: GeckoWebExecutor
    lateinit var server: TestServer

    @Before
    fun setup() {
        // Using @UiThreadTest here does not seem to block
        // the tests which are not using @UiThreadTest, so we do that
        // ourselves here as GeckoRuntime needs to be initialized
        // on the UI thread.
        runBlocking(Dispatchers.Main) {
            executor = GeckoWebExecutor(RuntimeCreator.getRuntime())
        }

        server = TestServer(InstrumentationRegistry.getInstrumentation().targetContext)
        server.start(TEST_PORT)
    }

    @After
    fun cleanup() {
        server.stop()
    }

    private fun fetch(request: WebRequest): WebResponse {
        return fetch(request, GeckoWebExecutor.FETCH_FLAGS_NONE)
    }

    private fun fetch(request: WebRequest, flags: Int): WebResponse {
        return executor.fetch(request, flags).pollDefault()!!
    }

    fun WebResponse.getBodyBytes(): ByteBuffer {
        body!!.use {
            return ByteBuffer.wrap(it.readBytes())
        }
    }

    fun WebResponse.getJSONBody(): JSONObject {
        val bytes = this.getBodyBytes()
        val bodyString = Charset.forName("UTF-8").decode(bytes).toString()
        return JSONObject(bodyString)
    }

    private fun randomString(count: Int): String {
        val chars = "01234567890abcdefghijklmnopqrstuvwxyz[],./?;'"
        val builder = StringBuilder(count)
        val rand = Random(System.currentTimeMillis())

        for (i in 0 until count) {
            builder.append(chars[rand.nextInt(chars.length)])
        }

        return builder.toString()
    }

    fun webRequestBuilder(uri: String): WebRequest.Builder {
        val beConservative =
            when (id) {
                "#conservative" -> true
                else -> false
            }
        return WebRequest.Builder(uri).beConservative(beConservative)
    }

    fun webRequest(uri: String): WebRequest {
        return webRequestBuilder(uri).build()
    }

    @Test
    fun smoke() {
        val uri = "$TEST_ENDPOINT/anything"
        val bodyString = randomString(8192)
        val referrer = "http://foo/bar"

        val request =
            webRequestBuilder(uri)
                .method("POST")
                .header("Header1", "Clobbered")
                .header("Header1", "Value")
                .addHeader("Header2", "Value1")
                .addHeader("Header2", "Value2")
                .referrer(referrer)
                .header("Content-Type", "text/plain")
                .body(bodyString)
                .build()

        val response = fetch(request)

        assertThat("URI should match", response.uri, equalTo(uri))
        assertThat("Status could should match", response.statusCode, equalTo(200))
        assertThat(
            "Content type should match",
            response.headers["Content-Type"],
            equalTo("application/json; charset=utf-8"),
        )
        assertThat("Redirected should match", response.redirected, equalTo(false))
        assertThat("isSecure should match", response.isSecure, equalTo(false))

        val body = response.getJSONBody()
        assertThat("Method should match", body.getString("method"), equalTo("POST"))
        assertThat("Headers should match", body.getJSONObject("headers").getString("Header1"), equalTo("Value"))
        assertThat(
            "Headers should match",
            body.getJSONObject("headers").getString("Header2"),
            equalTo("Value1, Value2"),
        )
        assertThat(
            "Headers should match",
            body.getJSONObject("headers").getString("Content-Type"),
            equalTo("text/plain"),
        )
        assertThat("Referrer should match", body.getJSONObject("headers").getString("Referer"), equalTo("http://foo/"))
        assertThat("Data should match", body.getString("data"), equalTo(bodyString))
    }

    @Test
    fun testFetchAsset() {
        val response = fetch(webRequest("$TEST_ENDPOINT/assets/www/hello.html"))
        assertThat("Status should match", response.statusCode, equalTo(200))
        assertThat("Body should have bytes", response.getBodyBytes().remaining(), greaterThan(0))
    }

    @Test
    fun testContentDispositionQueryParam() {
        // The "contentDisposition" query param makes the server set that header verbatim on the
        // asset response.
        val disposition = "attachment; filename=\"server-name.txt\""
        val response =
            fetch(webRequest("$TEST_ENDPOINT/assets/www/hello.html?contentDisposition=${Uri.encode(disposition)}"))
        assertThat("Status should match", response.statusCode, equalTo(200))
        assertThat("Content-Disposition should match", response.headers["Content-Disposition"], equalTo(disposition))
    }

    @Test
    fun testStatus() {
        val response = fetch(webRequest("$TEST_ENDPOINT/status/500"))
        assertThat("Status code should match", response.statusCode, equalTo(500))
    }

    @Test
    fun testRedirect() {
        val response = fetch(webRequest("$TEST_ENDPOINT/redirect-to?url=/status/200"))

        assertThat("URI should match", response.uri, equalTo(TEST_ENDPOINT + "/status/200"))
        assertThat("Redirected should match", response.redirected, equalTo(true))
        assertThat("Status code should match", response.statusCode, equalTo(200))
    }

    @Test
    fun testDisallowRedirect() {
        val response =
            fetch(webRequest("$TEST_ENDPOINT/redirect-to?url=/status/200"), GeckoWebExecutor.FETCH_FLAGS_NO_REDIRECTS)

        assertThat("URI should match", response.uri, equalTo("$TEST_ENDPOINT/redirect-to?url=/status/200"))
        assertThat("Redirected should match", response.redirected, equalTo(false))
        assertThat("Status code should match", response.statusCode, equalTo(302))
    }

    @Test
    fun testRedirectLoop() {
        val thrown =
            assertThrows(WebRequestError::class.java) {
                fetch(webRequest("$TEST_ENDPOINT/redirect/100"))
            }
        assertThat(
            thrown,
            equalTo(WebRequestError(WebRequestError.ERROR_REDIRECT_LOOP, WebRequestError.ERROR_CATEGORY_NETWORK)),
        )
    }

    @Test
    fun testAuth() {
        // We don't support authentication yet, but want to make sure it doesn't do anything
        // silly like try to prompt the user.
        val response = fetch(webRequest("$TEST_ENDPOINT/basic-auth/foo/bar"))
        assertThat("Status code should match", response.statusCode, equalTo(401))
    }

    @Test
    fun testSslError() {
        val uri =
            if (env.isAutomation) {
                "https://expired.example.com/"
            } else {
                "https://expired.badssl.com/"
            }

        try {
            fetch(webRequest(uri))
            throw IllegalStateException("fetch() should have thrown")
        } catch (e: WebRequestError) {
            assertThat("Category should match", e.category, equalTo(WebRequestError.ERROR_CATEGORY_SECURITY))
            assertThat("Code should match", e.code, equalTo(WebRequestError.ERROR_SECURITY_BAD_CERT))
            assertThat("Certificate should be present", e.certificate, notNullValue())
            assertThat(
                "Certificate issuer should be present",
                e.certificate?.issuerX500Principal?.name,
                not(isEmptyOrNullString()),
            )
        }
    }

    @Test
    fun testSecure() {
        val response = fetch(webRequest("https://example.com"))
        assertThat("Status should match", response.statusCode, equalTo(200))
        assertThat("isSecure should match", response.isSecure, equalTo(true))

        val expectedSubject =
            if (env.isAutomation) {
                "CN=example.com"
            } else {
                "CN=www.example.org,OU=Technology,O=Internet Corporation for Assigned Names and Numbers,L=Los Angeles,ST=California,C=US"
            }

        val expectedIssuer =
            if (env.isAutomation) {
                "OU=Profile Guided Optimization,O=Mozilla Testing,CN=Temporary Certificate Authority"
            } else {
                "CN=DigiCert SHA2 Secure Server CA,O=DigiCert Inc,C=US"
            }

        assertThat(
            "Subject should match",
            response.certificate?.subjectX500Principal?.name,
            equalTo(expectedSubject),
        )
        assertThat(
            "Issuer should match",
            response.certificate?.issuerX500Principal?.name,
            equalTo(expectedIssuer),
        )
    }

    @Test
    fun testCookies() {
        val uptimeMillis = SystemClock.uptimeMillis()
        val response = fetch(webRequest("$TEST_ENDPOINT/cookies/set/uptimeMillis/$uptimeMillis"))

        // We get redirected to /cookies which returns the cookies that were sent in the request
        assertThat("URI should match", response.uri, equalTo("$TEST_ENDPOINT/cookies"))
        assertThat("Status code should match", response.statusCode, equalTo(200))

        val body = response.getJSONBody()
        assertThat(
            "Body should match",
            body.getJSONObject("cookies").getString("uptimeMillis"),
            equalTo(uptimeMillis.toString()),
        )

        val anotherBody = fetch(webRequest("$TEST_ENDPOINT/cookies")).getJSONBody()
        assertThat(
            "Body should match",
            anotherBody.getJSONObject("cookies").getString("uptimeMillis"),
            equalTo(uptimeMillis.toString()),
        )
    }

    @Test
    fun testAnonymousSendCookies() {
        val uptimeMillis = SystemClock.uptimeMillis()
        val response =
            fetch(
                webRequest("$TEST_ENDPOINT/cookies/set/uptimeMillis/$uptimeMillis"),
                GeckoWebExecutor.FETCH_FLAGS_ANONYMOUS,
            )

        // We get redirected to /cookies which returns the cookies that were sent in the request
        assertThat("URI should match", response.uri, equalTo("$TEST_ENDPOINT/cookies"))
        assertThat("Status code should match", response.statusCode, equalTo(200))

        val body = response.getJSONBody()
        assertThat(
            "Cookies should not be set for the test server",
            body.getJSONObject("cookies").length(),
            equalTo(0),
        )
    }

    @Test
    fun testAnonymousGetCookies() {
        // Ensure a cookie is set for the test server
        testCookies()

        val response =
            fetch(
                webRequest("$TEST_ENDPOINT/cookies"),
                GeckoWebExecutor.FETCH_FLAGS_ANONYMOUS,
            )

        assertThat("Status code should match", response.statusCode, equalTo(200))
        val cookies = response.getJSONBody().getJSONObject("cookies")
        assertThat("Cookies should be empty", cookies.length(), equalTo(0))
    }

    @Test
    fun testPrivateCookies() {
        val clearData = GeckoResult<Void>()
        ThreadUtils.runOnUiThread {
            clearData.completeFrom(
                RuntimeCreator.getRuntime().storageController.clearData(StorageController.ClearFlags.ALL)
            )
        }

        clearData.pollDefault()

        val uptimeMillis = SystemClock.uptimeMillis()
        val response =
            fetch(
                webRequest("$TEST_ENDPOINT/cookies/set/uptimeMillis/$uptimeMillis"),
                GeckoWebExecutor.FETCH_FLAGS_PRIVATE,
            )

        // We get redirected to /cookies which returns the cookies that were sent in the request
        assertThat("URI should match", response.uri, equalTo("$TEST_ENDPOINT/cookies"))
        assertThat("Status code should match", response.statusCode, equalTo(200))

        val body = response.getJSONBody()
        assertThat(
            "Cookies should be set for the test server",
            body.getJSONObject("cookies").getString("uptimeMillis"),
            equalTo(uptimeMillis.toString()),
        )

        val anotherBody =
            fetch(webRequest("$TEST_ENDPOINT/cookies"), GeckoWebExecutor.FETCH_FLAGS_PRIVATE).getJSONBody()
        assertThat(
            "Body should match",
            anotherBody.getJSONObject("cookies").getString("uptimeMillis"),
            equalTo(uptimeMillis.toString()),
        )

        val yetAnotherBody = fetch(webRequest("$TEST_ENDPOINT/cookies")).getJSONBody()
        assertThat(
            "Cookies set in private session are not supposed to be seen in normal download",
            yetAnotherBody.getJSONObject("cookies").length(),
            equalTo(0),
        )
    }

    @Test
    fun testSpeculativeConnect() {
        // We don't have a way to know if it succeeds or not, but at least we can ensure
        // it doesn't explode.
        executor.speculativeConnect("http://localhost")

        // This is just a fence to ensure the above actually ran.
        fetch(webRequest("$TEST_ENDPOINT/cookies"))
    }

    @Test
    fun testResolveV4() {
        val addresses = executor.resolve("localhost").pollDefault()!!
        assertThat(
            "Addresses should not be null",
            addresses,
            notNullValue(),
        )
        assertThat(
            "First address should be loopback",
            addresses.first().isLoopbackAddress,
            equalTo(true),
        )
        assertThat(
            "First address size should be 4",
            addresses.first().address.size,
            equalTo(4),
        )
    }

    @Test
    fun testResolveV6() {
        val addresses = executor.resolve("ip6-localhost").pollDefault()!!
        assertThat(
            "Addresses should not be null",
            addresses,
            notNullValue(),
        )
        assertThat(
            "First address should be loopback",
            addresses.first().isLoopbackAddress,
            equalTo(true),
        )
        assertThat(
            "First address size should be 16",
            addresses.first().address.size,
            equalTo(16),
        )
    }

    @Test
    fun testFetchUnknownHost() {
        val thrown =
            assertThrows(WebRequestError::class.java) {
                fetch(webRequest("https://this.should.not.resolve"))
            }
        assertThat(
            thrown,
            equalTo(WebRequestError(WebRequestError.ERROR_UNKNOWN_HOST, WebRequestError.ERROR_CATEGORY_URI)),
        )
    }

    @Test(expected = UnknownHostException::class)
    fun testResolveError() {
        executor.resolve("this.should.not.resolve").pollDefault()
    }

    @Test
    fun testFetchStream() {
        val expectedCount = 1 * 1024 * 1024 // 1MB
        val response = executor.fetch(webRequest("$TEST_ENDPOINT/bytes/$expectedCount")).pollDefault()!!

        assertThat("Status code should match", response.statusCode, equalTo(200))
        assertThat("Content-Length should match", response.headers["Content-Length"]!!.toInt(), equalTo(expectedCount))

        val stream = response.body!!
        val bytes = stream.readBytes()
        stream.close()

        assertThat("Byte counts should match", bytes.size, equalTo(expectedCount))

        val digest = MessageDigest.getInstance("SHA-256").digest(bytes)
        assertThat(
            "Hashes should match",
            response.headers["X-SHA-256"],
            equalTo(String.format("%064x", BigInteger(1, digest))),
        )
    }

    @Test(expected = IOException::class)
    fun testFetchStreamError() {
        val expectedCount = 1 * 1024 * 1024 // 1MB
        val response =
            executor
                .fetch(
                    webRequest("$TEST_ENDPOINT/bytes/$expectedCount"),
                    GeckoWebExecutor.FETCH_FLAGS_STREAM_FAILURE_TEST,
                )
                .pollDefault()!!

        assertThat("Status code should match", response.statusCode, equalTo(200))
        assertThat("Content-Length should match", response.headers["Content-Length"]!!.toInt(), equalTo(expectedCount))

        val stream = response.body!!
        val bytes = ByteArray(1)
        stream.read(bytes)
    }

    @Test(expected = IOException::class)
    fun readClosedStream() {
        val response = executor.fetch(webRequest("$TEST_ENDPOINT/bytes/1024")).pollDefault()!!

        assertThat("Status code should match", response.statusCode, equalTo(200))

        val stream = response.body!!
        stream.close()
        stream.readBytes()
    }

    @Test(expected = IOException::class)
    fun readTimeout() {
        // Must outlast any thread stall at 1 byte per 250 ms; a fully buffered body never times out.
        val expectedCount = 200
        val response = executor.fetch(webRequest("$TEST_ENDPOINT/trickle/$expectedCount")).pollDefault()!!

        assertThat("Status code should match", response.statusCode, equalTo(200))
        assertThat("Content-Length should match", response.headers["Content-Length"]!!.toInt(), equalTo(expectedCount))

        // Only allow 1ms of blocking.
        response.setReadTimeoutMillis(1)

        val stream = response.body!!
        stream.readBytes()
    }

    @Test
    fun testFetchStreamCancel() {
        val expectedCount = 1 * 1024 * 1024 // 1MB
        val response = executor.fetch(webRequest("$TEST_ENDPOINT/bytes/$expectedCount")).pollDefault()!!

        assertThat("Status code should match", response.statusCode, equalTo(200))
        assertThat("Content-Length should match", response.headers["Content-Length"]!!.toInt(), equalTo(expectedCount))

        val stream = response.body!!

        assertThat("Stream should have 0 bytes available", stream.available(), equalTo(0))

        // Wait a second. Not perfect, but should be enough time for at least one buffer
        // to be appended if things are not going as they should.
        SystemClock.sleep(1000)

        assertThat("Stream should still have 0 bytes available", stream.available(), equalTo(0))

        stream.close()
    }

    @Test
    fun unsupportedUriScheme() {
        val illegal =
            mapOf(
                "" to "",
                "a" to "a",
                "ab" to "ab",
                "abc" to "abc",
                "htt" to "htt",
                "123456789" to "123456789",
                "1234567890" to "1234567890",
                "12345678901" to "1234567890",
                "file://test" to "file://tes",
                "moz-extension://what" to "moz-extens",
            )

        for ((uri, truncated) in illegal) {
            try {
                fetch(webRequest(uri))
                throw IllegalStateException("fetch() should have thrown")
            } catch (e: IllegalArgumentException) {
                assertThat(
                    "Message should match",
                    e.message,
                    equalTo("Unsupported URI scheme: $truncated"),
                )
            }
        }

        val legal =
            listOf(
                "http://$TEST_ENDPOINT\n",
                "http://$TEST_ENDPOINT/🥲",
                "http://$TEST_ENDPOINT/abc",
            )

        for (uri in legal) {
            try {
                fetch(webRequest(uri))
                throw IllegalStateException("fetch() should have thrown")
            } catch (e: WebRequestError) {
                assertThat(
                    "Request should pass initial validation.",
                    true,
                    equalTo(true),
                )
            }
        }
    }
}
