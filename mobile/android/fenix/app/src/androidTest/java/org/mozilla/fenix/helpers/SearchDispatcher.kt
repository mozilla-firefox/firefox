/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.helpers

import android.os.Handler
import android.os.Looper
import androidx.test.platform.app.InstrumentationRegistry
import java.io.IOException
import mockwebserver3.Dispatcher
import mockwebserver3.MockResponse
import mockwebserver3.RecordedRequest
import okio.Buffer
import okio.source
import org.json.JSONArray

private const val HTTP_OK = 200
private const val HTTP_NOT_FOUND = 404

/**
 * A [MockWebServer] [Dispatcher] that will return a generic search results page in the body of requests and responds
 * with status 200.
 *
 * If the dispatcher is unable to read a requested asset, it will fail the test by throwing an Exception on the main
 * thread.
 *
 * @see SearchMockServerRule
 */
class SearchDispatcher : Dispatcher() {
    private val mainThreadHandler = Handler(Looper.getMainLooper())

    override fun dispatch(request: RecordedRequest): MockResponse {
        val assetManager = InstrumentationRegistry.getInstrumentation().context.assets
        return try {
            when {
                request.target.contains(TRENDING_SEARCHES_PATH) ->
                    MockResponse.Builder()
                        .code(HTTP_OK)
                        .body(Buffer().apply { writeUtf8(TRENDING_SEARCHES_BODY) })
                        .addHeader("content-type: application/json; charset=utf-8")
                        .build()
                request.target.contains("searchResults.html?search=") ->
                    assetManager.open("pages/generic4.html").use { inputStream ->
                        MockResponse.Builder()
                            .code(HTTP_OK)
                            .body(Buffer().apply { writeAll(inputStream.source()) })
                            .addHeader("content-type: text/html; charset=utf-8")
                            .build()
                    }
                else -> MockResponse(code = HTTP_NOT_FOUND)
            }
        } catch (e: IOException) {
            // e.g. file not found.
            // We're on a background thread so we need to forward the exception to the main thread.
            mainThreadHandler.postAtFrontOfQueue {
                throw IllegalStateException("Could not load asset for: ${request.target}", e)
            }
            return MockResponse(code = HTTP_NOT_FOUND)
        }
    }

    companion object {
        /** Path component of the custom search engine's trending URL this dispatcher answers. */
        const val TRENDING_SEARCHES_PATH = "trendingSearches"

        /** The fixed set of trending searches returned so tests can assert on deterministic content. */
        val TRENDING_SEARCHES = listOf("Trending1", "Trending2", "Trending3", "Trending4", "Trending5")

        // Google-suggest response shape: the suggestions live at array index 1, which is what
        // defaultResponseParser (feature-search) reads for a custom engine.
        private val TRENDING_SEARCHES_BODY = JSONArray().put("").put(JSONArray(TRENDING_SEARCHES)).toString()
    }
}
