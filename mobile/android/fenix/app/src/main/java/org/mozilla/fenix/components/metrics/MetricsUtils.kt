/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.metrics

import android.util.Base64
import androidx.annotation.VisibleForTesting
import com.google.android.gms.common.GooglePlayServicesNotAvailableException
import com.google.android.gms.common.GooglePlayServicesRepairableException
import java.io.IOException
import java.security.NoSuchAlgorithmException
import java.security.spec.InvalidKeySpecException
import javax.crypto.SecretKeyFactory
import javax.crypto.spec.PBEKeySpec
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import mozilla.components.browser.state.search.SearchAccessPoint
import mozilla.components.browser.state.search.SearchEngine
import mozilla.components.browser.state.search.accessPoint
import mozilla.components.support.base.log.logger.Logger
import org.mozilla.experiments.nimbus.NimbusEventStore
import org.mozilla.fenix.GleanMetrics.Events
import org.mozilla.fenix.GleanMetrics.Metrics

/**
 * A utility object for recording various metrics related to user interactions within the application. This includes
 * metrics for searches, bookmark actions, and generating hashed identifiers for advertising purposes.
 *
 * This object provides helper functions to:
 * - Record search metrics, categorizing them by search engine, source, and whether the engine is the default.
 * - Record bookmark action metrics, such as adding, editing, deleting, or opening bookmarks, along with the source of
 *   the action.
 * - Retrieve and hash the Google Advertising ID, allowing for a privacy-preserving way to track users across different
 *   products while ensuring different identifiers for each product.
 */
object MetricsUtils {
    // The number of iterations to compute the hash. RFC 2898 suggests
    // a minimum of 1000 iterations.
    private const val PBKDF2_ITERATIONS = 1000
    private const val PBKDF2_KEY_LEN_BITS = 256

    /** Possible sources for a performed search. */
    enum class Source {
        ACTION,
        SHORTCUT,
        SUGGESTION,
        TOPSITE,
        WIDGET,
        CONTEXT,
        DIGITAL_ASSISTANT,
        NONE,
    }

    /**
     * Records the appropriate metric for performed searches.
     *
     * @param engine the engine used for searching.
     * @param isDefault whether the engine is the default engine or not.
     * @param searchAccessPoint the source of the search. Can be one of the values of [Source].
     * @param nimbusEventStore used to record the search event in the Nimbus internal event store.
     */
    fun recordSearchMetrics(
        engine: SearchEngine,
        isDefault: Boolean,
        searchAccessPoint: Source,
        nimbusEventStore: NimbusEventStore,
    ) {
        val telemetrySuffix =
            engine.accessPoint(searchAccessPoint.toSearchAccessPoint())?.telemetrySuffix ?: engine.telemetrySuffix
        val telemetryId =
            if (engine.type == SearchEngine.Type.CUSTOM) {
                "custom"
            } else {
                val baseId = engine.id.lowercase()
                if (!telemetrySuffix.isNullOrEmpty()) {
                    "$baseId-$telemetrySuffix"
                } else {
                    baseId
                }
            }
        val source = searchAccessPoint.name.lowercase()

        Metrics.searchCount["$telemetryId.$source"].add()

        val performedSearchExtra =
            if (isDefault) {
                "default.$source"
            } else {
                "shortcut.$source"
            }

        Events.performedSearch.record(Events.PerformedSearchExtra(performedSearchExtra))
        nimbusEventStore.recordEvent("performed_search")
    }

    /**
     * Records appropriate metrics for adding a bookmark.
     *
     * Note: this was split off from [recordBookmarkMetrics], because [nimbusEventStore] was needed only for the case of
     * adding a bookmark. There was no good way to do it in [recordBookmarkMetrics] without either unnecessarily
     * requiring [nimbusEventStore] from callers that only do edits/deletes/opens or making [nimbusEventStore] nullable
     * which makes it possible to accidentally skip recording the event in Nimbus.
     *
     * @param source Describes where the action was called from.
     * @param nimbusEventStore [NimbusEventStore] used to record the event for use in behavioral targeting.
     * @param count Number of times to record the metric.
     */
    fun recordBookmarkAddMetric(
        source: BookmarkAction.Source,
        nimbusEventStore: NimbusEventStore,
        count: Int = 1,
    ) {
        Metrics.bookmarksAdd[source.label()].add(count)

        nimbusEventStore.recordEvent(
            count = count.toLong(),
            eventId = "bookmark_added",
        )
    }

    /**
     * Records the appropriate metric for performed Bookmark action.
     *
     * @param action The [BookmarkAction] being counted.
     * @param source Describes where the action was called from.
     */
    fun recordBookmarkMetrics(
        action: BookmarkAction,
        source: BookmarkAction.Source,
    ) {
        when (action) {
            BookmarkAction.EDIT -> Metrics.bookmarksEdit[source.label()].add()
            BookmarkAction.DELETE -> Metrics.bookmarksDelete[source.label()].add()
            BookmarkAction.OPEN -> Metrics.bookmarksOpen[source.label()].add()
        }
    }

    /** Describes which bookmark action is being recorded. */
    enum class BookmarkAction {
        EDIT,
        DELETE,
        OPEN;

        /** Possible sources for a bookmark action. */
        enum class Source {
            ADD_BOOKMARK_TOAST,
            BOOKMARK_EDIT_PAGE,
            BOOKMARK_PANEL,
            BROWSER_NAVBAR,
            BROWSER_TOOLBAR,
            MENU_DIALOG,
            PAGE_ACTION_MENU,
            TABS_TRAY,
            TEST,
        }
    }

    private fun BookmarkAction.Source.label() = name.lowercase()

    /** Get the default salt to use for hashing. This is a convenience function to help with unit tests. */
    @Suppress("FunctionOnlyReturningConstant")
    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun getHashingSalt(): String = "org.mozilla.fenix-salt"

    /**
     * Query the Google Advertising API to get the Google Advertising ID.
     *
     * This is meant to be used off the main thread. The API will throw an exception and we will print a log message
     * otherwise.
     *
     * @param retrieveAdvertisingIdInfo A lambda function that retrieves the advertising ID.
     * @return a String containing the Google Advertising ID or null.
     */
    @Suppress("TooGenericExceptionCaught")
    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun getAdvertisingID(retrieveAdvertisingIdInfo: () -> String?): String? {
        return try {
            retrieveAdvertisingIdInfo()
        } catch (e: GooglePlayServicesNotAvailableException) {
            Logger.debug("getAdvertisingID() - Google Play not installed on the device")
            null
        } catch (e: GooglePlayServicesRepairableException) {
            Logger.debug("getAdvertisingID() - recoverable error connecting to Google Play Services")
            null
        } catch (e: IllegalStateException) {
            // This is unlikely to happen, as this should be running off the main thread.
            Logger.debug("getAdvertisingID() - AdvertisingIdClient must be called off the main thread")
            null
        } catch (e: IOException) {
            Logger.debug("getAdvertisingID() - unable to connect to Google Play Services")
            null
        } catch (e: NullPointerException) {
            Logger.debug("getAdvertisingID() - no Google Advertising ID available")
            null
        }
    }

    /**
     * Produces a hashed version of the Google Advertising ID. We want users using more than one of our products to
     * report a different ID in each of them. This function runs off the main thread and is CPU-bound.
     *
     * The `customSalt` parameter allows for dynamic setting of the salt for certain experiments or any one-off
     * applications.
     *
     * @param retrieveAdvertisingIdInfo A function that retrieves the Google Advertising ID.
     * @param encodeToString A function responsible for encoding a byte array to a string. It accepts two arguments:
     *     1. `data`: The `ByteArray` to be encoded.
     *     2. `flag`: An `Int` representing encoder flag bit to control the encoding process. (e.g., `Base64.NO_WRAP`).
     *
     * @param customSalt An optional custom salt to use for hashing. If not provided, a default salt will be used.
     * @return a hashed and salted Google Advertising ID or null if it was not possible to get the Google Advertising ID
     *   or if an error occurred during hashing.
     */
    suspend fun getHashedIdentifier(
        retrieveAdvertisingIdInfo: () -> String?,
        encodeToString: (data: ByteArray, flag: Int) -> String,
        customSalt: String? = null,
    ): String? =
        withContext(Dispatchers.Default) {
            getAdvertisingID(retrieveAdvertisingIdInfo = retrieveAdvertisingIdInfo)?.let { unhashedID ->
                // Add some salt to the ID, before hashing. We have a default salt that is used for
                // all the hashes unless you specifically provide something different. This is done
                // to stabalize all hashing within a single product. The customSalt allows for tweaking
                // in case there are specific use-cases that require something custom.
                val salt = customSalt ?: getHashingSalt()

                // Apply hashing.
                try {
                    // Note that we intentionally want to use slow hashing functions here in order
                    // to increase the cost of potentially repeatedly guess the original unhashed
                    // identifier.
                    val keySpec =
                        PBEKeySpec(
                            unhashedID.toCharArray(),
                            salt.toByteArray(),
                            PBKDF2_ITERATIONS,
                            PBKDF2_KEY_LEN_BITS,
                        )

                    val keyFactory = SecretKeyFactory.getInstance("PBKDF2WithHmacSHA1")
                    val hashedBytes = keyFactory.generateSecret(keySpec).encoded
                    encodeToString(hashedBytes, Base64.NO_WRAP)
                } catch (e: java.lang.NullPointerException) {
                    Logger.error("getHashedIdentifier() - missing or wrong salt parameter")
                    null
                } catch (e: IllegalArgumentException) {
                    Logger.error("getHashedIdentifier() - wrong parameter", e)
                    null
                } catch (e: NoSuchAlgorithmException) {
                    Logger.error("getHashedIdentifier() - algorithm not available")
                    null
                } catch (e: InvalidKeySpecException) {
                    Logger.error("getHashedIdentifier() - invalid key spec")
                    null
                }
            }
        }

    /** Enum marking the access point of the Tab Groups feature, for telemetry. */
    enum class TabGroupAccessPoint {
        TABS_TRAY,
        BROWSER_MENU,
    }
}

/**
 * Maps a [MetricsUtils.Source] onto a search-config-v3 search access point name, for selecting the partner code and
 * telemetry suffix that apply to a search from that source.
 *
 * [MetricsUtils.Source.DIGITAL_ASSISTANT] has no access point of its own, so it takes the default one.
 */
fun MetricsUtils.Source.toSearchAccessPoint(): String =
    when (this) {
        MetricsUtils.Source.CONTEXT -> SearchAccessPoint.CONTEXT
        MetricsUtils.Source.TOPSITE -> SearchAccessPoint.TOPSITE
        MetricsUtils.Source.WIDGET -> SearchAccessPoint.WIDGET
        MetricsUtils.Source.ACTION,
        MetricsUtils.Source.SHORTCUT,
        MetricsUtils.Source.SUGGESTION,
        MetricsUtils.Source.DIGITAL_ASSISTANT,
        MetricsUtils.Source.NONE -> SearchAccessPoint.DEFAULT
    }
