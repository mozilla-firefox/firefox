/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.topsites

import mozilla.components.feature.top.sites.PinnedSiteStorage
import mozilla.components.feature.top.sites.TopSite
import mozilla.components.feature.top.sites.TopSitesFrecencyConfig
import mozilla.components.feature.top.sites.TopSitesProviderConfig
import mozilla.components.feature.top.sites.TopSitesStorage
import mozilla.components.support.base.observer.Observable
import mozilla.components.support.base.observer.ObserverRegistry

/**
 * Default implementation of [TopSitesStorage].
 *
 * @param pinnedSitesStorage An instance of [PinnedSiteStorage], used for storing pinned sites.
 */
class DefaultTopSitesStorage(private val pinnedSitesStorage: PinnedSiteStorage) :
    TopSitesStorage, Observable<TopSitesStorage.Observer> by ObserverRegistry() {

    override suspend fun addTopSite(title: String, url: String, isDefault: Boolean) {
        pinnedSitesStorage.addPinnedSite(title, url, isDefault)
        notifyObservers { onStorageUpdated() }
    }

    override suspend fun addTopSites(topSites: List<Pair<String, String>>, isDefault: Boolean) = Unit

    override suspend fun removeTopSite(topSite: TopSite) {
        pinnedSitesStorage.removePinnedSite(topSite)
        notifyObservers { onStorageUpdated() }
    }

    override suspend fun updateTopSite(topSite: TopSite, title: String, url: String) {
        pinnedSitesStorage.updatePinnedSite(topSite, title, url)
        notifyObservers { onStorageUpdated() }
    }

    override suspend fun getTopSites(
        targetSitesCount: Int,
        frecencyConfig: TopSitesFrecencyConfig?,
        providerConfig: TopSitesProviderConfig?,
    ): List<TopSite> = pinnedSitesStorage.getPinnedSites().take(targetSitesCount)

    companion object {
        const val TOP_SITES_MAX_LIMIT = 4
    }
}
