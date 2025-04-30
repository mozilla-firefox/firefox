/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.concept.fetch

/**
 * A collection of HTTP [Headers] (immutable) of a [Request] or [Response].
 */
interface Headers : Iterable<Header> {
    /**
     * Returns the number of headers (key / value combinations).
     */
    val size: Int

    /**
     * Gets the [Header] at the specified [index].
     */
    operator fun get(index: Int): Header

    /**
     * Returns the last values corresponding to the specified header field name. Or null if the header does not exist.
     */
    operator fun get(name: String): String?

    /**
     * Returns the list of values corresponding to the specified header field name.
     */
    fun getAll(name: String): List<String>

    /**
     * Sets the [Header] at the specified [index].
     */
    operator fun set(index: Int, header: Header)

    /**
     * Returns true if a [Header] with the given [name] exists.
     */
    operator fun contains(name: String): Boolean

    /**
     * A collection of common HTTP header names.
     *
     * A list of common HTTP request headers can be found at
     *   https://en.wikipedia.org/wiki/List_of_HTTP_header_fields#Standard_request_fields
     *
     * A list of common HTTP response headers can be found at
     *   https://en.wikipedia.org/wiki/List_of_HTTP_header_fields#Standard_response_fields
     *
     * @see [Headers.Values]
     */
    object Names {
        const val CONTENT_DISPOSITION = "Content-Disposition"
        const val CONTENT_RANGE = "Content-Range"
        const val RANGE = "Range"
        const val CONTENT_LENGTH = "Content-Length"
        const val CONTENT_TYPE = "Content-Type"
        const val COOKIE = "Cookie"
        const val REFERRER = "Referer"
        const val USER_AGENT = "User-Agent"
        const val E_TAG = "ETag"
        const val IF_NONE_MATCH = "If-None-Match"
    }

    /**
     * A collection of common HTTP header values.
     *
     * @see [Headers.Names]
     */
    object Values {
        const val CONTENT_TYPE_FORM_URLENCODED = "application/x-www-form-urlencoded"
        const val CONTENT_TYPE_APPLICATION_JSON = "application/json"
    }
}

/**
 * Represents a [Header] containing of a [name] and [value].
 */
data class Header(
    val name: String,
    val value: String,
) {
    init {
        require(name.isNotEmpty()) {
            "Header name cannot be empty"
        }
    }
}

/**
 * A collection of HTTP [Headers] (mutable) of a [Request] or [Response].
 */
class MutableHeaders(headers: List<Header>) : Headers, MutableIterable<Header> {

    private val headers = headers.toMutableList()

    constructor(vararg pairs: Pair<String, String>) : this(
        pairs.map { (name, value) -> Header(name, value) }.toMutableList(),
    )

    /**
     * Gets the [Header] at the specified [index].
     */
    override fun get(index: Int): Header = headers[index]

    /**
     * Returns the last value corresponding to the specified header field name. Or null if the header does not exist.
     */
    override fun get(name: String) =
        headers.lastOrNull { header -> header.name.equals(name, ignoreCase = true) }?.value

    /**
     * Returns the list of values corresponding to the specified header field name.
     */
    override fun getAll(name: String): List<String> = headers
        .filter { header -> header.name.equals(name, ignoreCase = true) }
        .map { header -> header.value }

    /**
     * Sets the [Header] at the specified [index].
     */
    override fun set(index: Int, header: Header) {
        headers[index] = header
    }

    /**
     * Returns an iterator over the headers that supports removing elements during iteration.
     */
    override fun iterator(): MutableIterator<Header> = headers.iterator()

    /**
     * Returns true if a [Header] with the given [name] exists.
     */
    override operator fun contains(name: String): Boolean =
        headers.any { it.name.equals(name, ignoreCase = true) }

    /**
     * Returns the number of headers (key / value combinations).
     */
    override val size: Int
        get() = headers.size

    /**
     * Append a header without removing the headers already present.
     */
    fun append(name: String, value: String): MutableHeaders {
        headers.add(Header(name, value))
        return this
    }

    /**
     * Set the only occurrence of the header; potentially overriding an already existing header.
     */
    fun set(name: String, value: String): MutableHeaders {
        headers.forEachIndexed { index, current ->
            if (current.name.equals(name, ignoreCase = true)) {
                headers[index] = Header(name, value)
                return this
            }
        }

        return append(name, value)
    }

    override fun equals(other: Any?) = other is MutableHeaders && headers == other.headers

    override fun hashCode() = headers.hashCode()
}

fun List<Header>.toMutableHeaders() = MutableHeaders(this)
