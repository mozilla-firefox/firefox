/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.state.search

import android.graphics.Bitmap
import android.net.Uri
import androidx.core.net.toUri

// OpenSearch parameter for search terms.
const val OS_SEARCH_ENGINE_TERMS_PARAM = "{" + "searchTerms" + "}"

/**
 * The search access points a [SearchEngine] can be configured for, naming the keys of [SearchEngine.accessPoints].
 *
 * The configuration may name access points that are not listed here, so these are named values rather than the full
 * set.
 */
object SearchAccessPoint {
    /** The access point a search engine falls back to for any access point it does not define. */
    const val DEFAULT = "default"

    /** A search started from selected text on a page. */
    const val CONTEXT = "context"

    /** A search started from a top site. */
    const val TOPSITE = "topsite"

    /** A search started from the search widget. */
    const val WIDGET = "widget"
}

/**
 * The URLs and telemetry suffix a search engine uses at one search access point.
 *
 * An access point present on a [SearchEngine] is used exactly as the configuration specifies it. In particular its URLs
 * already omit the partner code parameter when the access point has no partner code, rather than carrying it empty. An
 * access point that is absent falls back to [SearchAccessPoint.DEFAULT] as a whole, not field by field.
 *
 * @property resultUrls the search results urls, best first, as [SearchEngine.resultUrls].
 * @property suggestUrl the search suggestion url.
 * @property trendingUrl the trending search url.
 * @property partnerCode the partner code for this access point, already substituted into its URLs, and null when the
 *   access point has none.
 * @property telemetrySuffix the telemetry suffix for this access point.
 */
data class SearchEngineAccessPoint(
    val resultUrls: List<String> = emptyList(),
    val suggestUrl: String? = null,
    val trendingUrl: String? = null,
    val partnerCode: String? = null,
    val telemetrySuffix: String? = null,
)

/**
 * Returns the [SearchEngineAccessPoint] named [accessPoint], or the one named [SearchAccessPoint.DEFAULT] when this
 * engine does not define [accessPoint]. Returns `null` when the engine defines no access points at all, which is the
 * case for every engine not configured per access point.
 */
fun SearchEngine.accessPoint(accessPoint: String): SearchEngineAccessPoint? =
    accessPoints[accessPoint] ?: accessPoints[SearchAccessPoint.DEFAULT]

/**
 * A data class representing a search engine.
 *
 * @property id the ID of this search engine.
 * @property name the name of this search engine.
 * @property icon the icon of this search engine.
 * @property inputEncoding the input encoding of this search engine.
 * @property type the type of this search engine.
 * @property resultUrls the list of the queried suggestions result urls.
 * @property suggestUrl the search suggestion url.
 * @property trendingUrl the trending search url.
 * @property isGeneral whether the search engine is a general search engine.
 * @property isOptional whether the search engine is an additional engine not included in the default list.
 * @property telemetrySuffix the telemetry suffix of this search engine, for the default search access point.
 * @property accessPoints the per search access point URLs and telemetry suffixes, keyed by access point name. Empty for
 *   engines from a configuration that does not vary them, in which case the single-valued properties above apply
 *   everywhere.
 */
data class SearchEngine(
    val id: String,
    val name: String,
    val icon: Bitmap,
    val inputEncoding: String? = null,
    val type: Type,
    val resultUrls: List<String> = emptyList(),
    val suggestUrl: String? = null,
    val trendingUrl: String? = null,
    val isGeneral: Boolean = false,
    val isOptional: Boolean = false,
    val telemetrySuffix: String? = null,
    val accessPoints: Map<String, SearchEngineAccessPoint> = emptyMap(),
) {
    /** A enum class representing a search engine type. */
    enum class Type {
        /** A bundled search engine. */
        BUNDLED,

        /** A bundled search engine that was loaded additionally, requested by the application. */
        BUNDLED_ADDITIONAL,

        /** A custom search engine added by the user. */
        CUSTOM,

        /** A search engine add by the application. */
        APPLICATION,
    }

    // Cache these parameters to avoid repeated parsing.
    // Assume we always have at least one entry in `resultUrls`.
    val resultsUrl: Uri by lazy { this.resultUrls[0].toUri() }

    // This assumes that search parameters are always "on their own" within the param value,
    // e.g. always in a form of ?q={searchTerms}, never ?q=somePrefix-{searchTerms}
    val searchParameterName by lazy {
        resultsUrl.queryParameterNames.find {
            try {
                resultsUrl.getQueryParameter(it) == OS_SEARCH_ENGINE_TERMS_PARAM
            } catch (e: UnsupportedOperationException) {
                false
            }
        }
    }
}
