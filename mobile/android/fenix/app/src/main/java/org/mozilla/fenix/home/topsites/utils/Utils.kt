/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.topsites.utils

import android.content.res.Resources
import androidx.annotation.RawRes
import kotlinx.serialization.SerializationException
import kotlinx.serialization.json.Json
import mozilla.components.concept.base.crash.Breadcrumb
import mozilla.components.lib.crash.CrashReporter
import mozilla.components.support.base.log.logger.Logger
import org.mozilla.fenix.home.topsites.DefaultTopSiteItem
import org.mozilla.fenix.home.topsites.DefaultTopSitesList

private val logger = Logger("fetchDefaultTopSites")

/**
 * Fetches the default top sites available in [region] from the provided raw resource, in the order they should appear.
 *
 * @param resources [Resources] used for accessing the raw resource.
 * @param rawResId The raw resource ID holding the default top sites JSON.
 * @param crashReporter [CrashReporter] used for recording caught exceptions.
 * @param region The current region of the user.
 * @param experiments The experiments the user is enrolled in.
 */
internal fun fetchDefaultTopSites(
    resources: Resources,
    @RawRes rawResId: Int,
    crashReporter: CrashReporter,
    region: String,
    experiments: Set<String> = emptySet(),
): List<DefaultTopSiteItem> {
    val items =
        try {
            val json = Json { ignoreUnknownKeys = true }
            val jsonString = resources.openRawResource(rawResId).bufferedReader().use { it.readText() }
            json.decodeFromString<DefaultTopSitesList>(jsonString).data
        } catch (e: Resources.NotFoundException) {
            crashReporter.report("Failed to find ${resources.rawResourceName(rawResId)}", e)
            emptyList()
        } catch (e: SerializationException) {
            crashReporter.report("Failed to parse ${resources.rawResourceName(rawResId)}", e)
            emptyList()
        } catch (e: IllegalArgumentException) {
            crashReporter.report("Failed to parse ${resources.rawResourceName(rawResId)}", e)
            emptyList()
        }

    return items
        .filter { item ->
            val includedInRegions = item.includeRegions.isEmpty() || region in item.includeRegions
            val notExcludedInRegions = item.excludeRegions.isEmpty() || region !in item.excludeRegions

            val includedInExperiments =
                item.includeExperiments.isEmpty() || item.includeExperiments.any { it in experiments }
            val notExcludedInExperiments = item.excludeExperiments.none { it in experiments }

            includedInRegions && notExcludedInRegions && includedInExperiments && notExcludedInExperiments
        }
        .sortedBy { it.order }
}

private fun Resources.rawResourceName(@RawRes rawResId: Int): String =
    try {
        getResourceEntryName(rawResId)
    } catch (e: Resources.NotFoundException) {
        logger.warn("Failed to find a name for resource $rawResId", e)
        rawResId.toString()
    }

private fun CrashReporter.report(message: String, e: Exception) {
    logger.error(message, e)
    recordCrashBreadcrumb(Breadcrumb(message = message))
    submitCaughtException(e)
}
