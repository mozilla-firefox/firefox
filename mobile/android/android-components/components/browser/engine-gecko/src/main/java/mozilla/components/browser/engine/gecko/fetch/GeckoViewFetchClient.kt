/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.engine.gecko.fetch

import android.content.Context
import androidx.annotation.VisibleForTesting
import mozilla.components.concept.fetch.Client
import mozilla.components.concept.fetch.Headers
import mozilla.components.concept.fetch.MutableHeaders
import mozilla.components.concept.fetch.Request
import mozilla.components.concept.fetch.Response
import mozilla.components.concept.fetch.Response.Companion.SUCCESS
import mozilla.components.concept.fetch.isBlobUri
import mozilla.components.concept.fetch.isDataUri
import org.mozilla.geckoview.GeckoRuntime
import org.mozilla.geckoview.GeckoWebExecutor
import org.mozilla.geckoview.WebRequest
import org.mozilla.geckoview.WebRequest.CACHE_MODE_DEFAULT
import org.mozilla.geckoview.WebRequest.CACHE_MODE_RELOAD
import org.mozilla.geckoview.WebRequestError
import org.mozilla.geckoview.WebResponse
import java.io.IOException
import java.net.SocketTimeoutException
import java.nio.ByteBuffer
import java.util.concurrent.TimeUnit
import java.util.concurrent.TimeoutException

/**
 * GeckoView ([GeckoWebExecutor]) based implementation of [Client].
 */
class GeckoViewFetchClient(
    context: Context,
    runtime: GeckoRuntime = GeckoRuntime.getDefault(context),
    private val maxReadTimeOut: Pair<Long, TimeUnit> = Pair(MAX_READ_TIMEOUT_MINUTES, TimeUnit.MINUTES),
) : Client() {

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal var executor: GeckoWebExecutor = GeckoWebExecutor(runtime)

    @Throws(IOException::class)
    override fun fetch(request: Request): Response {
        if (request.isDataUri()) {
            return fetchDataUri(request)
        }

        val webRequest = request.toWebRequest()

        val readTimeOut = request.readTimeout ?: maxReadTimeOut
        val readTimeOutMillis = readTimeOut.let { (timeout, unit) ->
            unit.toMillis(timeout)
        }

        return try {
            val webResponse = executor.fetch(webRequest, request.fetchFlags).poll(readTimeOutMillis)
            webResponse?.toResponse() ?: throw IOException("Fetch failed with null response")
        } catch (e: TimeoutException) {
            throw SocketTimeoutException()
        } catch (e: WebRequestError) {
            throw IOException(e)
        }
    }

    private val Request.fetchFlags: Int
        get() {
            var fetchFlags = 0
            if (cookiePolicy == Request.CookiePolicy.OMIT) {
                fetchFlags += GeckoWebExecutor.FETCH_FLAGS_ANONYMOUS
            }
            if (private) {
                fetchFlags += GeckoWebExecutor.FETCH_FLAGS_PRIVATE
            }
            if (redirect == Request.Redirect.MANUAL) {
                fetchFlags += GeckoWebExecutor.FETCH_FLAGS_NO_REDIRECTS
            }
            if (useOhttp) {
                fetchFlags += GeckoWebExecutor.FETCH_FLAGS_OHTTP
            }
            return fetchFlags
        }

    companion object {
        const val MAX_READ_TIMEOUT_MINUTES = 5L
    }
}

private fun Request.toWebRequest(): WebRequest = WebRequest.Builder(url)
    .method(method.name)
    .addHeadersFrom(this)
    .addBodyFrom(this)
    .referrer(referrerUrl)
    .cacheMode(if (useCaches) CACHE_MODE_DEFAULT else CACHE_MODE_RELOAD)
    .beConservative(conservative)
    .build()

private fun WebRequest.Builder.addHeadersFrom(request: Request): WebRequest.Builder {
    request.headers?.forEach { header ->
        addHeader(header.name, header.value)
    }

    return this
}

private fun WebRequest.Builder.addBodyFrom(request: Request): WebRequest.Builder {
    request.body?.let { body ->
        body.useStream { inStream ->
            val bytes = inStream.readBytes()
            val buffer = ByteBuffer.allocateDirect(bytes.size)
            buffer.put(bytes)
            this.body(buffer)
        }
    }

    return this
}

internal fun WebResponse.toResponse(): Response {
    val isDataUri = uri.startsWith("data:")
    val isBlobUri = uri.startsWith("blob:")
    val headers = translateHeaders(this)
    // We use the same API for blobs, data URLs and HTTP requests, but blobs won't receive a status code.
    // If no exception is thrown we assume success.
    val status = if (isBlobUri || isDataUri) SUCCESS else statusCode
    return Response(
        uri,
        status,
        headers,
        body?.let {
            Response.Body(it, headers["Content-Type"])
        } ?: Response.Body.empty(),
    )
}

private fun translateHeaders(webResponse: WebResponse): Headers {
    val headers = MutableHeaders()
    webResponse.headers.forEach { (k, v) ->
        v.split(",").forEach { headers.append(k, it.trim()) }
    }

    return headers
}
