/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.concept.fetch

import android.net.Uri
import mozilla.components.concept.fetch.Request.CookiePolicy
import java.io.Closeable
import java.io.File
import java.io.IOException
import java.io.InputStream
import java.util.concurrent.TimeUnit

/**
 * The [Request] data class represents a resource request to be send by a [Client].
 *
 * It's API is inspired by the Request interface of the Web Fetch API:
 * https://developer.mozilla.org/en-US/docs/Web/API/Request
 *
 * @property url The URL of the request.
 * @property method The request method (GET, POST, ..)
 * @property headers Optional HTTP headers to be send with the request.
 * @property connectTimeout A timeout to be used when connecting to the resource.  If the timeout expires before the
 * connection can be established, a [java.net.SocketTimeoutException] is raised. A timeout of zero is interpreted as an
 * infinite timeout.
 * @property readTimeout A timeout to be used when reading from the resource. If the timeout expires before there is
 * data available for read, a java.net.SocketTimeoutException is raised. A timeout of zero is interpreted as an infinite
 * timeout.
 * @property body An optional body to be send with the request.
 * @property redirect Whether the [Client] should follow redirects (HTTP 3xx) for this request or not.
 * @property cookiePolicy A policy to specify whether or not cookies should be
 * sent with the request, defaults to [CookiePolicy.INCLUDE]
 * @property useCaches Whether caches should be used or a network request
 * should be forced, defaults to true (use caches).
 * @property private Whether the request should be performed in a private context, defaults to false.
 * The feature is not support in all [Client]s, check support before using.
 * @property useOhttp Whether the request should be performed using the OHTTP library, defaults to false.
 * Currently only GeckoViewFetchClient supports it.
 * @see [Headers.Names]
 * @see [Headers.Values]
 */
data class Request(
    val url: String,
    val method: Method = Method.GET,
    val headers: MutableHeaders? = MutableHeaders(),
    val connectTimeout: Pair<Long, TimeUnit>? = null,
    val readTimeout: Pair<Long, TimeUnit>? = null,
    val body: Body? = null,
    val redirect: Redirect = Redirect.FOLLOW,
    val cookiePolicy: CookiePolicy = CookiePolicy.INCLUDE,
    val useCaches: Boolean = true,
    val private: Boolean = false,
    val useOhttp: Boolean = false,
) {
    var referrerUrl: String? = null
    var conservative: Boolean = false

    /**
     * Create a Request for Backward compatibility.
     * @property referrerUrl An optional url of the referrer.
     * @property conservative Whether to turn off bleeding-edge network features to avoid breaking core browser
     * functionality, defaults to false. Set to true for Mozilla services only.
     */
    constructor(
        url: String,
        method: Method = Method.GET,
        headers: MutableHeaders? = MutableHeaders(),
        connectTimeout: Pair<Long, TimeUnit>? = null,
        readTimeout: Pair<Long, TimeUnit>? = null,
        body: Body? = null,
        redirect: Redirect = Redirect.FOLLOW,
        cookiePolicy: CookiePolicy = CookiePolicy.INCLUDE,
        useCaches: Boolean = true,
        private: Boolean = false,
        referrerUrl: String? = null,
        conservative: Boolean = false,
        useOhttp: Boolean = false,
    ) : this(
        url = url,
        method = method,
        headers = headers,
        connectTimeout = connectTimeout,
        readTimeout = readTimeout,
        body = body,
        redirect = redirect,
        cookiePolicy = cookiePolicy,
        useCaches = useCaches,
        private = private,
        useOhttp = useOhttp,
    ) {
        this.referrerUrl = referrerUrl
        this.conservative = conservative
    }

    /**
     * A [Body] to be send with the [Request].
     *
     * @param stream A stream that will be read and send to the resource.
     */
    class Body(
        private val stream: InputStream,
    ) : Closeable {
        companion object {
            /**
             * Create a [Body] from the provided [String].
             */
            fun fromString(value: String): Body = Body(value.byteInputStream())

            /**
             * Create a [Body] from the provided [File].
             */
            fun fromFile(file: File): Body = Body(file.inputStream())

            /**
             * Create a [Body] from the provided [unencodedParams] in the format of Content-Type
             * "application/x-www-form-urlencoded". Parameters are formatted as "key1=value1&key2=value2..."
             * and values are percent-encoded. If the given map is empty, the response body will contain the
             * empty string.
             *
             * @see [Headers.Values.CONTENT_TYPE_FORM_URLENCODED]
             */
            fun fromParamsForFormUrlEncoded(vararg unencodedParams: Pair<String, String>): Body {
                // It's unintuitive to use the Uri class format and encode
                // but its GET query syntax is exactly what we need.
                val uriBuilder = Uri.Builder()
                unencodedParams.forEach { (key, value) -> uriBuilder.appendQueryParameter(key, value) }
                val encodedBody = uriBuilder.build().encodedQuery ?: "" // null when the given map is empty.
                return Body(encodedBody.byteInputStream())
            }
        }

        /**
         * Executes the given [block] function on the body's stream and then closes it down correctly whether an
         * exception is thrown or not.
         */
        fun <R> useStream(block: (InputStream) -> R): R = use {
            block(stream)
        }

        /**
         * Closes this body and releases any system resources associated with it.
         */
        override fun close() {
            try {
                stream.close()
            } catch (e: IOException) {
                // Ignore
            }
        }
    }

    /**
     * Request methods.
     *
     * The request method token is the primary source of request semantics;
     * it indicates the purpose for which the client has made this request
     * and what is expected by the client as a successful result.
     *
     * https://tools.ietf.org/html/rfc7231#section-4
     */
    enum class Method {
        GET,
        HEAD,
        POST,
        PUT,
        DELETE,
        CONNECT,
        OPTIONS,
        TRACE,
    }

    enum class Redirect {
        /**
         * Automatically follow redirects.
         */
        FOLLOW,

        /**
         * Do not follow redirects and let caller handle them manually.
         */
        MANUAL,
    }

    enum class CookiePolicy {
        /**
         * Include cookies when sending the request.
         */
        INCLUDE,

        /**
         * Do not send cookies with the request.
         */
        OMIT,
    }
}

/**
 * Checks whether or not the request is for a data URI.
 */
fun Request.isDataUri() = url.startsWith("data:")

/**
 * Checks whether or not the request is for a data blob.
 */
fun Request.isBlobUri() = url.startsWith("blob:")
