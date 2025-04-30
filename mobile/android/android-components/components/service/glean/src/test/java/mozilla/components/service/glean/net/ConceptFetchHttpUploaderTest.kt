/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.glean.net

import mozilla.components.concept.fetch.Client
import mozilla.components.concept.fetch.Request
import mozilla.components.concept.fetch.Response
import mozilla.components.lib.fetch.httpurlconnection.HttpURLConnectionClient
import mozilla.components.lib.fetch.okhttp.OkHttpClient
import mozilla.components.support.test.any
import mozilla.components.support.test.argumentCaptor
import mozilla.components.support.test.mock
import mozilla.telemetry.glean.config.Configuration
import mozilla.telemetry.glean.net.CapablePingUploadRequest
import mozilla.telemetry.glean.net.HttpStatus
import mozilla.telemetry.glean.net.Incapable
import mozilla.telemetry.glean.net.PingUploadRequest
import mozilla.telemetry.glean.net.RecoverableFailure
import okhttp3.mockwebserver.Dispatcher
import okhttp3.mockwebserver.MockResponse
import okhttp3.mockwebserver.MockWebServer
import okhttp3.mockwebserver.RecordedRequest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.mockito.Mockito.spy
import org.mockito.Mockito.verify
import org.mockito.Mockito.`when`
import java.io.IOException
import java.net.CookieHandler
import java.net.CookieManager
import java.net.HttpCookie
import java.net.URI
import java.util.concurrent.TimeUnit

class ConceptFetchHttpUploaderTest {
    private val testPath: String = "/some/random/path/not/important"
    private val testPing: String = "{ 'ping': 'test' }"
    private val testDefaultConfig = Configuration()

    /**
     * Create a mock webserver that accepts all requests.
     * @return a [MockWebServer] instance
     */
    private fun getMockWebServer(): MockWebServer {
        val server = MockWebServer()
        server.dispatcher =
            object : Dispatcher() {
                override fun dispatch(request: RecordedRequest): MockResponse {
                    return MockResponse().setBody("OK")
                }
            }

        return server
    }

    @Test
    fun `connection timeouts must be properly set`() {
        val uploader =
            spy<ConceptFetchHttpUploader>(ConceptFetchHttpUploader(lazy { HttpURLConnectionClient() }))

        val uploadRequest = PingUploadRequest(url = testPath, data = testPing.toByteArray(), emptyMap(), emptyList())
        val request = uploader.buildRequest(uploadRequest)

        assertEquals(
            Pair(ConceptFetchHttpUploader.DEFAULT_READ_TIMEOUT, TimeUnit.MILLISECONDS),
            request.readTimeout,
        )
        assertEquals(
            Pair(ConceptFetchHttpUploader.DEFAULT_CONNECTION_TIMEOUT, TimeUnit.MILLISECONDS),
            request.connectTimeout,
        )
    }

    @Test
    fun `Glean headers are correctly dispatched`() {
        val mockClient: Client = mock()
        `when`(mockClient.fetch(any())).thenReturn(
            Response("URL", 200, mock(), mock()),
        )

        val expectedHeaders = mapOf(
            "Content-Type" to "application/json; charset=utf-8",
            "Test-header" to "SomeValue",
            "OtherHeader" to "Glean/Test 25.0.2",
        )

        val uploader = ConceptFetchHttpUploader(lazy { mockClient })
        val uploadRequest = CapablePingUploadRequest(PingUploadRequest(url = testPath, data = testPing.toByteArray(), expectedHeaders, emptyList()))
        uploader.upload(uploadRequest)
        val requestCaptor = argumentCaptor<Request>()
        verify(mockClient).fetch(requestCaptor.capture())

        expectedHeaders.forEach { (headerName, headerValue) ->
            assertEquals(
                headerValue,
                requestCaptor.value.headers!![headerName],
            )
        }
    }

    @Test
    fun `Cookie policy must be properly set`() {
        val uploader =
            spy<ConceptFetchHttpUploader>(ConceptFetchHttpUploader(lazy { HttpURLConnectionClient() }))

        val uploadRequest = PingUploadRequest(url = testPath, data = testPing.toByteArray(), emptyMap(), emptyList())
        val request = uploader.buildRequest(uploadRequest)

        assertEquals(request.cookiePolicy, Request.CookiePolicy.OMIT)
    }

    @Test
    fun `upload() returns true for successful submissions (200)`() {
        val mockClient: Client = mock()
        `when`(mockClient.fetch(any())).thenReturn(
            Response(
                "URL",
                200,
                mock(),
                mock(),
            ),
        )

        val uploader = spy<ConceptFetchHttpUploader>(ConceptFetchHttpUploader(lazy { mockClient }))

        val uploadRequest = CapablePingUploadRequest(PingUploadRequest(url = testPath, data = testPing.toByteArray(), emptyMap(), emptyList()))
        assertEquals(HttpStatus(200), uploader.upload(uploadRequest))
    }

    @Test
    fun `upload() returns false for server errors (5xx)`() {
        for (responseCode in 500..527) {
            val mockClient: Client = mock()
            `when`(mockClient.fetch(any())).thenReturn(
                Response(
                    "URL",
                    responseCode,
                    mock(),
                    mock(),
                ),
            )

            val uploader = spy<ConceptFetchHttpUploader>(ConceptFetchHttpUploader(lazy { mockClient }))

            val uploadRequest = CapablePingUploadRequest(PingUploadRequest(url = testPath, data = testPing.toByteArray(), emptyMap(), emptyList()))
            assertEquals(HttpStatus(responseCode), uploader.upload(uploadRequest))
        }
    }

    @Test
    fun `upload() returns true for successful submissions (2xx)`() {
        for (responseCode in 200..226) {
            val mockClient: Client = mock()
            `when`(mockClient.fetch(any())).thenReturn(
                Response(
                    "URL",
                    responseCode,
                    mock(),
                    mock(),
                ),
            )

            val uploader = spy<ConceptFetchHttpUploader>(ConceptFetchHttpUploader(lazy { mockClient }))

            val uploadRequest = CapablePingUploadRequest(PingUploadRequest(url = testPath, data = testPing.toByteArray(), emptyMap(), emptyList()))
            assertEquals(HttpStatus(responseCode), uploader.upload(uploadRequest))
        }
    }

    @Test
    fun `upload() returns true for failing submissions with broken requests (4xx)`() {
        for (responseCode in 400..451) {
            val mockClient: Client = mock()
            `when`(mockClient.fetch(any())).thenReturn(
                Response(
                    "URL",
                    responseCode,
                    mock(),
                    mock(),
                ),
            )

            val uploader = spy<ConceptFetchHttpUploader>(ConceptFetchHttpUploader(lazy { mockClient }))

            val uploadRequest = CapablePingUploadRequest(PingUploadRequest(url = testPath, data = testPing.toByteArray(), emptyMap(), emptyList()))
            assertEquals(HttpStatus(responseCode), uploader.upload(uploadRequest))
        }
    }

    @Test
    fun `upload() correctly uploads the ping data with default configuration`() {
        val server = getMockWebServer()

        val client = ConceptFetchHttpUploader(lazy { HttpURLConnectionClient() })

        val submissionUrl = "http://" + server.hostName + ":" + server.port + testPath
        val uploadRequest = CapablePingUploadRequest(PingUploadRequest(url = submissionUrl, data = testPing.toByteArray(), mapOf("test" to "header"), emptyList()))
        assertEquals(HttpStatus(200), client.upload(uploadRequest))

        val request = server.takeRequest()
        assertEquals(testPath, request.path)
        assertEquals("POST", request.method)
        assertEquals(testPing, request.body.readUtf8())
        assertEquals("header", request.getHeader("test"))

        server.shutdown()
    }

    @Test
    fun `upload() correctly uploads the ping data with httpurlconnection client`() {
        val server = getMockWebServer()

        val client = ConceptFetchHttpUploader(lazy { HttpURLConnectionClient() })

        val submissionUrl = "http://" + server.hostName + ":" + server.port + testPath
        val uploadRequest = CapablePingUploadRequest(PingUploadRequest(url = submissionUrl, data = testPing.toByteArray(), mapOf("test" to "header"), emptyList()))
        assertEquals(HttpStatus(200), client.upload(uploadRequest))

        val request = server.takeRequest()
        assertEquals(testPath, request.path)
        assertEquals("POST", request.method)
        assertEquals(testPing, request.body.readUtf8())
        assertEquals("header", request.getHeader("test"))
        assertTrue(request.headers.values("Cookie").isEmpty())

        server.shutdown()
    }

    @Test
    fun `upload() correctly uploads the ping data with OkHttp client`() {
        val server = getMockWebServer()

        val client = ConceptFetchHttpUploader(lazy { OkHttpClient() })

        val submissionUrl = "http://" + server.hostName + ":" + server.port + testPath
        val uploadRequest = CapablePingUploadRequest(PingUploadRequest(url = submissionUrl, data = testPing.toByteArray(), mapOf("test" to "header"), emptyList()))
        assertEquals(HttpStatus(200), client.upload(uploadRequest))

        val request = server.takeRequest()
        assertEquals(testPath, request.path)
        assertEquals("POST", request.method)
        assertEquals(testPing, request.body.readUtf8())
        assertEquals("header", request.getHeader("test"))
        assertTrue(request.headers.values("Cookie").isEmpty())

        server.shutdown()
    }

    @Test
    fun `upload() must not transmit any cookie`() {
        val server = getMockWebServer()

        val testConfig = testDefaultConfig.copy(
            serverEndpoint = "http://localhost:" + server.port,
        )

        // Set the default cookie manager/handler to be used for the http upload.
        val cookieManager = CookieManager()
        CookieHandler.setDefault(cookieManager)

        // Store a sample cookie.
        val cookie = HttpCookie("cookie-time", "yes")
        cookie.domain = testConfig.serverEndpoint
        cookie.path = testPath
        cookie.version = 0
        cookieManager.cookieStore.add(URI(testConfig.serverEndpoint), cookie)

        // Store a cookie for a subdomain of the same domain's as the server endpoint,
        // to make sure we don't accidentally remove it.
        val cookie2 = HttpCookie("cookie-time2", "yes")
        cookie2.domain = "sub.localhost"
        cookie2.path = testPath
        cookie2.version = 0
        cookieManager.cookieStore.add(URI("http://sub.localhost:${server.port}/test"), cookie2)

        // Add another cookie for the same domain. This one should be removed as well.
        val cookie3 = HttpCookie("cookie-time3", "yes")
        cookie3.domain = "localhost"
        cookie3.path = testPath
        cookie3.version = 0
        cookieManager.cookieStore.add(URI("http://localhost:${server.port}/test"), cookie3)

        // Trigger the connection.
        val client = ConceptFetchHttpUploader(lazy { HttpURLConnectionClient() })
        val submissionUrl = testConfig.serverEndpoint + testPath
        val uploadRequest = CapablePingUploadRequest(PingUploadRequest(url = submissionUrl, data = testPing.toByteArray(), emptyMap(), emptyList()))
        assertEquals(HttpStatus(200), client.upload(uploadRequest))

        val request = server.takeRequest()
        assertEquals(testPath, request.path)
        assertEquals("POST", request.method)
        assertEquals(testPing, request.body.readUtf8())
        assertTrue(request.headers.values("Cookie").isEmpty())

        // Check that we still have a cookie.
        assertEquals(1, cookieManager.cookieStore.cookies.size)
        assertEquals("cookie-time2", cookieManager.cookieStore.cookies[0].name)

        server.shutdown()
    }

    @Test
    fun `upload() should return false when upload fails`() {
        val mockClient: Client = mock()
        `when`(mockClient.fetch(any())).thenThrow(IOException())

        val uploader = spy<ConceptFetchHttpUploader>(ConceptFetchHttpUploader(lazy { mockClient }))

        // And IOException during upload is a failed upload that we should retry. The client should
        // return false in this case.
        val uploadRequest = CapablePingUploadRequest(PingUploadRequest(url = "path", data = "ping".toByteArray(), emptyMap(), emptyList()))
        assertEquals(RecoverableFailure(0), uploader.upload(uploadRequest))
    }

    @Test
    fun `the lazy client should only be instantiated after the first upload`() {
        val mockClient: Client = mock()
        `when`(mockClient.fetch(any())).thenReturn(
            Response("URL", 200, mock(), mock()),
        )
        val uploader = spy<ConceptFetchHttpUploader>(ConceptFetchHttpUploader(lazy { mockClient }))
        assertFalse(uploader.client.isInitialized())

        // After calling upload, the client must get instantiated.
        val uploadRequest = CapablePingUploadRequest(PingUploadRequest(url = "path", data = "ping".toByteArray(), emptyMap(), emptyList()))
        uploader.upload(uploadRequest)
        assertTrue(uploader.client.isInitialized())
    }

    @Test
    fun `usePrivateRequest sends all requests with private flag`() {
        val mockClient: Client = mock()
        `when`(mockClient.fetch(any())).thenReturn(
            Response("URL", 200, mock(), mock()),
        )

        val expectedHeaders = mapOf(
            "Content-Type" to "application/json; charset=utf-8",
            "Test-header" to "SomeValue",
            "OtherHeader" to "Glean/Test 25.0.2",
        )

        val uploader = ConceptFetchHttpUploader(lazy { mockClient }, true)
        val uploadRequest = CapablePingUploadRequest(PingUploadRequest(url = "path", data = "ping".toByteArray(), expectedHeaders, emptyList()))
        uploader.upload(uploadRequest)

        val captor = argumentCaptor<Request>()

        verify(mockClient).fetch(captor.capture())

        assertTrue(captor.value.private)
    }

    @Test
    fun `upload() respects the uploader capabilities`() {
        val mockClient: Client = mock()
        `when`(mockClient.fetch(any())).thenReturn(
            Response(
                "URL",
                200,
                mock(),
                mock(),
            ),
        )

        // Ensure that an empty capabilities list succeed
        val incapableUploader = spy<ConceptFetchHttpUploader>(ConceptFetchHttpUploader(lazy { mockClient }))

        val plainPing = CapablePingUploadRequest(PingUploadRequest(url = testPath, data = testPing.toByteArray(), emptyMap(), emptyList()))
        assertEquals(HttpStatus(200), incapableUploader.upload(plainPing))

        // Ensure that we get Incapable when we have unsupported capabilities
        val pingWUnsupportedCapability = CapablePingUploadRequest(PingUploadRequest(url = testPath, data = testPing.toByteArray(), emptyMap(), listOf("some-unsupported-capability")))
        assertEquals(Incapable(0), incapableUploader.upload(pingWUnsupportedCapability))

        // Ensure that we don't submit OHTTP pings when supportsOhttp is false
        val pingWOhttpCapability = CapablePingUploadRequest(PingUploadRequest(url = testPath, data = testPing.toByteArray(), emptyMap(), listOf("ohttp")))
        assertEquals(Incapable(0), incapableUploader.upload(pingWOhttpCapability))

        // Ensure that when supportsOhttp is true, we are able to submit ohttp pings
        val ohttpCapableUploader = spy<ConceptFetchHttpUploader>(ConceptFetchHttpUploader(lazy { mockClient }, supportsOhttp = true))
        assertEquals(HttpStatus(200), ohttpCapableUploader.upload(pingWOhttpCapability))

        // Ensure that OHTTP capable uploader doesn't accept all capabilities
        assertEquals(Incapable(0), ohttpCapableUploader.upload(pingWUnsupportedCapability))

        // Ensure that OHTTP capable uploader doesn't accept all capabilities when there's ohttp in the list
        val pingWOhttpAndUnsupportedCapability = CapablePingUploadRequest(PingUploadRequest(url = testPath, data = testPing.toByteArray(), emptyMap(), listOf("ohttp", "some-unsupported-capability")))
        assertEquals(Incapable(0), ohttpCapableUploader.upload(pingWOhttpAndUnsupportedCapability))
    }
}
