/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.top.sites

/**
 * A contract that indicates how a top sites provider must behave.
 */
interface TopSitesProvider {

    /**
     * Provides a list of top sites.
     *
     * @param allowCache Whether or not the result may be provided from a previously
     * cached response.
     * @return a list of top sites from the provider.
     */
    suspend fun getTopSites(allowCache: Boolean = true): List<TopSite>

    /**
     * Refreshes the cache with the latest top sites response if the cache is expired.
     */
    suspend fun refreshTopSitesIfCacheExpired()
}
