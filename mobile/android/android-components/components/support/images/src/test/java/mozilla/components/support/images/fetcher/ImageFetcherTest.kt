/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.support.images.fetcher

import androidx.test.ext.junit.runners.AndroidJUnit4
import java.io.ByteArrayInputStream
import java.io.IOException
import java.io.InputStream
import kotlin.ranges.contains
import kotlin.test.BeforeTest
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertTrue
import mozilla.components.concept.fetch.Client
import mozilla.components.concept.fetch.Headers
import mozilla.components.concept.fetch.MutableHeaders
import mozilla.components.concept.fetch.Request
import mozilla.components.concept.fetch.Response
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class ImageFetcherTest {

    private lateinit var client: FakeClient
    private lateinit var fetcher: ImageFetcher

    @BeforeTest
    fun setUp() {
        client = FakeClient()
        fetcher = ImageFetcher.fromClient(client)
    }

    @Test
    fun `GIVEN successful client response, WHEN config specifies max size equal to the response THEN result is successful`() {
        val data = ByteArray(DEFAULT_BUFFER_SIZE + 1)
        val stream = TestInputStream(data)

        client.response = successfulResponse(stream)
        val result =
            fetcher.fetch(
                request = testRequest(),
                config = ImageFetcherConfig(maxSizeBytes = DEFAULT_BUFFER_SIZE + 1L),
            )

        assertContentEquals(data, result.getOrThrow())
    }

    @Test
    fun `GIVEN successful client response with mismatched Content-Length header, WHEN Content-Length is more than config max THEN result is an ImageTooLargeException failure`() {
        client.response = successfulResponse(stream = ByteArray(1000).inputStream(), contentLengthHeader = "1001")

        val result =
            fetcher.fetch(
                request = testRequest(),
                config = ImageFetcherConfig(maxSizeBytes = 1000),
            )

        assertFailsWith<ImageTooLargeException> { result.getOrThrow() }
    }

    @Test
    fun `GIVEN response causes ImageTooLargeException WHEN fetch returns THEN stream is closed`() {
        val stream = TestInputStream(ByteArray(1000))
        client.response =
            successfulResponse(
                stream = stream,
                contentLengthHeader = "1001",
            )

        val result =
            fetcher.fetch(
                request = testRequest(),
                config = ImageFetcherConfig(maxSizeBytes = 1000),
            )

        assertFailsWith<ImageTooLargeException> { result.getOrThrow() }
        assertTrue(stream.closed)
    }

    @Test
    fun `GIVEN fetch overflows because response is too large WHEN fetch returns THEN the additional bytes read is less than the buffer size`() {
        val maxSize = 500L
        val stream = TestInputStream(ByteArray(STREAM_BUFFER_SIZE * 3))
        client.response = successfulResponse(stream = stream)

        val result =
            fetcher.fetch(
                request = testRequest(),
                config = ImageFetcherConfig(maxSizeBytes = maxSize),
            )

        assertFailsWith<ImageTooLargeException> { result.getOrThrow() }

        // the stream must stop at the next "read" after the response was determined to be too large
        val excessBytesRead = stream.bytesConsumed - maxSize
        assertTrue(excessBytesRead in 0..STREAM_BUFFER_SIZE)
    }

    @Test
    fun `GIVEN successful client response with mismatched Content-Length header, WHEN actual length is more than config max size THEN result is an ImageTooLargeException failure`() {
        client.response = successfulResponse(stream = ByteArray(1001).inputStream(), contentLengthHeader = "1000")

        val result =
            fetcher.fetch(
                request = testRequest(),
                config = ImageFetcherConfig(maxSizeBytes = 1000),
            )

        assertFailsWith<ImageTooLargeException> { result.getOrThrow() }
    }

    @Test
    fun `GIVEN client response, WHEN Content-Length header is negative THEN result uses the configured max size`() {
        val stream = TestInputStream(ByteArray(1001))
        client.response = successfulResponse(stream = stream, contentLengthHeader = "-1")

        val result =
            fetcher.fetch(
                request = testRequest(),
                config = ImageFetcherConfig(maxSizeBytes = 1000),
            )

        assertFailsWith<ImageTooLargeException> { result.getOrThrow() }
        val excessBytesRead = stream.bytesConsumed - 1000
        assertTrue(excessBytesRead in 0..STREAM_BUFFER_SIZE)
    }

    @Test
    fun `GIVEN successful client response WHEN Content-Length header is more than config max THEN fetch fails is ImageTooLargeException`() {
        client.response = successfulResponse(ByteArray(1000).inputStream(), contentLengthHeader = "1000")

        val result =
            fetcher.fetch(
                request = testRequest(),
                config = ImageFetcherConfig(maxSizeBytes = 999),
            )

        assertFailsWith<ImageTooLargeException> { result.getOrThrow() }
    }

    @Test
    fun `GIVEN successful client response WHEN config does not specify a max size THEN result is successful and contains the full body`() {
        client.response = successfulResponse(ByteArray(1000).inputStream(), contentLengthHeader = "1000")

        val result =
            fetcher.fetch(
                request = testRequest(),
                config = ImageFetcherConfig(maxSizeBytes = null),
            )

        assertEquals(1000, result.getOrThrow().size)
    }

    @Test
    fun `GIVEN client response in non-successful status range WHEN fetch returns THEN an ImageFetchException is returned`() {
        client.response = errorResponse(status = 400)

        val result =
            fetcher.fetch(
                request = testRequest(),
                config = ImageFetcherConfig(maxSizeBytes = 1000),
            )

        val exception = assertFailsWith<ImageFetchException> { result.getOrThrow() }
        assertEquals(400, exception.status)
    }

    @Test
    fun `GIVEN client throws an IO exception WHEN fetch returns THEN the IO exception is returned to the caller`() {
        client.exception = IOException("Oops!")

        val result =
            fetcher.fetch(
                request = testRequest(),
                config = ImageFetcherConfig(maxSizeBytes = 1000),
            )

        val exception = result.exceptionOrNull()
        assertEquals("Oops!", exception?.message)
    }

    private fun successfulResponse(
        stream: InputStream,
        contentLengthHeader: String? = null,
        url: String = "https://example.com/image",
    ): Response {
        val headers = MutableHeaders()
        if (contentLengthHeader != null) {
            headers.set(Headers.Names.CONTENT_LENGTH, contentLengthHeader)
        }
        return Response(
            url = url,
            status = 200,
            headers = headers,
            body = Response.Body(stream),
        )
    }

    private fun errorResponse(url: String = "https://example.com/image", status: Int = 500): Response {
        return Response(
            url = url,
            status = status,
            headers = MutableHeaders(),
            body = Response.Body(ByteArrayInputStream(ByteArray(0))),
        )
    }

    private fun testRequest(url: String = "https://example.com/image"): Request {
        return Request(url)
    }

    private class FakeClient : Client() {
        var response: Response? = null
        var exception: Throwable? = null

        override fun fetch(request: Request): Response {
            exception?.let { throw it }
            return requireNotNull(response)
        }
    }

    private class TestInputStream(data: ByteArray) : ByteArrayInputStream(data) {
        var closed = false
            private set

        val bytesConsumed: Int
            get() = pos

        override fun available(): Int = 0

        override fun close() {
            closed = true
            super.close()
        }
    }
}
