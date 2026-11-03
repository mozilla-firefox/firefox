/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.top.sites

import mozilla.components.browser.storage.sync.PlacesHistoryStorage
import mozilla.components.support.base.observer.Observable

/** Abstraction layer above the [PinnedSiteStorage] and [PlacesHistoryStorage] storages. */
interface TopSitesStorage : Observable<TopSitesStorage.Observer> {
    /**
     * Adds a new top site.
     *
     * @param title The title string.
     * @param url The URL string.
     * @param isDefault Whether or not the pinned site added should be a default pinned site. This is used to identify
     *   pinned sites that are added by the application.
     */
    suspend fun addTopSite(title: String, url: String, isDefault: Boolean = false)

    /**
     * Adds a list of top sites.
     *
     * @param topSites A list containing a title to url pair of top sites to be added.
     * @param isDefault Whether or not the pinned site added should be a default pinned site. This is used to identify
     *   pinned sites that are added by the application.
     */
    suspend fun addTopSites(topSites: List<Pair<String, String>>, isDefault: Boolean = false)

    /**
     * Removes the given [TopSite].
     *
     * @param topSite The top site.
     */
    suspend fun removeTopSite(topSite: TopSite)

    /**
     * Updates the given [TopSite].
     *
     * @param topSite The top site.
     * @param title The new title for the top site.
     * @param url The new url for the top site.
     */
    suspend fun updateTopSite(topSite: TopSite, title: String, url: String)

    /**
     * Return a unified list of top sites based on the given number of sites desired. If `frecencyConfig` is specified,
     * fill in any missing top sites with frecent top site results.
     *
     * @param targetSitesCount The number of top sites to fill up to.
     * @param frecencyConfig An instance of [TopSitesFrecencyConfig] that specifies which top frecent sites to be
     *   included.
     * @param providerConfig An instance of [TopSitesProviderConfig] that specifies whether or not to fetch top sites
     *   from the [TopSitesProvider].
     */
    suspend fun getTopSites(
        targetSitesCount: Int,
        frecencyConfig: TopSitesFrecencyConfig? = null,
        providerConfig: TopSitesProviderConfig? = null,
    ): List<TopSite>

    /** Interface to be implemented by classes that want to observe the top site storage. */
    interface Observer {
        /** Notify the observer when changes are made to the storage. */
        fun onStorageUpdated()
    }
}
