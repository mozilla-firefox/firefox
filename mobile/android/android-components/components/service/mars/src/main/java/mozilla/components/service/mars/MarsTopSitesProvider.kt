/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.mars

import android.content.Context
import android.text.format.DateUtils
import android.util.AtomicFile
import androidx.annotation.VisibleForTesting
import kotlinx.serialization.SerializationException
import kotlinx.serialization.json.Json
import mozilla.components.concept.fetch.Client
import mozilla.components.concept.fetch.Headers.Names.CONTENT_TYPE
import mozilla.components.concept.fetch.Headers.Names.USER_AGENT
import mozilla.components.concept.fetch.Headers.Values.CONTENT_TYPE_APPLICATION_JSON
import mozilla.components.concept.fetch.MutableHeaders
import mozilla.components.concept.fetch.Request
import mozilla.components.concept.fetch.Request.Body
import mozilla.components.concept.fetch.Request.Method
import mozilla.components.feature.top.sites.TopSite
import mozilla.components.feature.top.sites.TopSitesProvider
import mozilla.components.support.base.ext.fetchBodyOrNull
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.ktx.util.readAndDeserialize
import mozilla.components.support.ktx.util.writeString
import org.json.JSONObject
import java.io.File
import java.io.IOException
import java.util.Date

private const val MARS_ENDPOINT_URL = "https://ads.mozilla.org/v1/ads"

private const val REQUEST_BODY_CONTEXT_ID_KEY = "context_id"
private const val REQUEST_BODY_PLACEMENTS_KEY = "placements"
private const val REQUEST_BODY_PLACEMENT_KEY = "placement"
private const val REQUEST_BODY_COUNT_KEY = "count"

internal const val CACHE_FILE_NAME = "mozilla_components_service_mars_tiles.json"

/**
 * Provides access to the MARS API for fetching top sites tile.
 *
 * @param context A reference to the application context.
 * @property requestConfig Configuration for the top sites tile request.
 * @property client [Client] used for interacting with the MARS API endpoint.
 * @property endPointURL The url of the endpoint to fetch from. Defaults to [MARS_ENDPOINT_URL].
 * @property maxCacheAgeInSeconds Maximum time (in seconds) the cache should remain valid
 * before a refresh is attempted. Defaults to -1, meaning the max age defined by the server
 * will be used.
 */
class MarsTopSitesProvider(
    context: Context,
    private val client: Client,
    private val requestConfig: MarsTopSitesRequestConfig,
    private val endPointURL: String = MARS_ENDPOINT_URL,
    private val maxCacheAgeInSeconds: Long = -1,
) : TopSitesProvider {

    private val applicationContext = context.applicationContext
    private val logger = Logger("MarsTopSitesProvider")
    private val diskCacheLock = Any()

    // Current state of the cache.
    @VisibleForTesting
    @Volatile
    internal var cacheState: CacheState = CacheState()

    /**
     * Fetches from the top sites [endPointURL] to provide a list of provided top sites.
     * Returns a cached response if [allowCache] is true and the cache is not expired
     * (@see [maxCacheAgeInSeconds]).
     *
     * @param allowCache Whether or not the result may be provided from a previously cached
     * response.
     */
    override suspend fun getTopSites(allowCache: Boolean): List<TopSite.Provided> {
        val cachedTopSites = if (allowCache && !isCacheExpired()) {
            readFromDiskCache()
        } else {
            null
        }

        if (!cachedTopSites.isNullOrEmpty()) {
            return cachedTopSites
        }

        return try {
            fetchTopSites()
        } catch (e: IOException) {
            logger.error("Failed to fetch tiles from the MARS API endpoint", e)
            throw e
        }
    }

    /**
     * Refreshes the cache with the latest tiles response from [endPointURL] if the cache
     * is expired.
     */
    override suspend fun refreshTopSitesIfCacheExpired() {
        if (!isCacheExpired()) return

        getTopSites(allowCache = false)
    }

    private fun fetchTopSites(): List<TopSite.Provided> {
        val request = Request(
            url = endPointURL,
            method = Method.POST,
            headers = getRequestHeaders(),
            body = getRequestBody(),
            conservative = true,
        )

        return client.fetchBodyOrNull(
            request = request,
            logger = logger,
        )?.let { responseBody ->
            try {
                val json = Json { ignoreUnknownKeys = true }
                json.decodeFromString<MarsTopSitesResponse>(responseBody).getTopSites().also {
                    if (maxCacheAgeInSeconds > 0) {
                        writeToDiskCache(responseBody)
                    }
                }
            } catch (e: SerializationException) {
                listOf()
            }
        } ?: listOf()
    }

    private fun getRequestHeaders(): MutableHeaders {
        return if (requestConfig.userAgent.isNullOrEmpty()) {
            MutableHeaders(
                CONTENT_TYPE to "$CONTENT_TYPE_APPLICATION_JSON; charset=UTF-8",
            )
        } else {
            MutableHeaders(
                CONTENT_TYPE to "$CONTENT_TYPE_APPLICATION_JSON; charset=UTF-8",
                USER_AGENT to requestConfig.userAgent,
            )
        }
    }

    private fun getRequestBody(): Body {
        val params = mapOf(
            REQUEST_BODY_CONTEXT_ID_KEY to requestConfig.contextId,
            REQUEST_BODY_PLACEMENTS_KEY to requestConfig.placements.map {
                mapOf(
                    REQUEST_BODY_PLACEMENT_KEY to it.placement,
                    REQUEST_BODY_COUNT_KEY to it.count,
                )
            },
        )

        return Body(JSONObject(params).toString().byteInputStream())
    }

    @VisibleForTesting
    internal fun readFromDiskCache(): List<TopSite.Provided>? {
        synchronized(diskCacheLock) {
            return getCacheFile().readAndDeserialize {
                val json = Json { ignoreUnknownKeys = true }
                json.decodeFromString<MarsTopSitesResponse>(it).getTopSites()
            }
        }
    }

    @VisibleForTesting
    internal fun writeToDiskCache(responseBody: String) {
        synchronized(diskCacheLock) {
            getCacheFile().let {
                it.writeString { responseBody }

                // Update the cache state to reflect the current status.
                cacheState = cacheState.computeMaxAges(
                    lastModified = System.currentTimeMillis(),
                    localMaxAge = maxCacheAgeInSeconds * DateUtils.SECOND_IN_MILLIS,
                    serverMaxAge = 0L,
                )
            }
        }
    }

    @VisibleForTesting
    internal fun isCacheExpired(): Boolean {
        cacheState.getCacheMaxAge()?.let { return Date().time > it }

        val file = getBaseCacheFile()

        cacheState = if (file.exists()) {
            cacheState.computeMaxAges(
                lastModified = file.lastModified(),
                localMaxAge = maxCacheAgeInSeconds * DateUtils.SECOND_IN_MILLIS,
                serverMaxAge = 0L,
            )
        } else {
            cacheState.invalidate()
        }

        // If cache is invalid, we should also consider it as expired
        return Date().time > (cacheState.getCacheMaxAge() ?: -1L)
    }

    private fun getCacheFile(): AtomicFile = AtomicFile(getBaseCacheFile())

    @VisibleForTesting
    internal fun getBaseCacheFile(): File = File(applicationContext.filesDir, CACHE_FILE_NAME)
}
