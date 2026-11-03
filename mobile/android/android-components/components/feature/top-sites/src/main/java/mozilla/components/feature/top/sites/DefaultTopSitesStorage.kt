/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.top.sites

import kotlinx.coroutines.CancellationException
import mozilla.components.browser.storage.sync.PlacesHistoryStorage
import mozilla.components.feature.top.sites.ext.hasHost
import mozilla.components.feature.top.sites.ext.hasUrl
import mozilla.components.feature.top.sites.ext.toTopSite
import mozilla.components.feature.top.sites.facts.emitTopSitesCountFact
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.base.observer.Observable
import mozilla.components.support.base.observer.ObserverRegistry

/**
 * Default implementation of [TopSitesStorage].
 *
 * @param pinnedSitesStorage An instance of [PinnedSiteStorage], used for storing pinned sites.
 * @param historyStorage An instance of [PlacesHistoryStorage], used for retrieving top frecent sites from history.
 * @param topSitesProvider An optional instance of [TopSitesProvider], used for retrieving additional top sites from a
 *   provider. The returned top sites are added before pinned sites.
 * @param defaultTopSitesProvider An optional instance of [DefaultTopSitesProvider], used for retrieving the default top
 *   sites bundled with the application. The default top sites backfill the slots that are left over once the pinned and
 *   frecent top sites have been added.
 */
class DefaultTopSitesStorage
private constructor(
    private val pinnedSitesStorage: PinnedSiteStorage,
    private val historyStorage: PlacesHistoryStorage,
    private val topSitesProvider: TopSitesProvider? = null,
    private val defaultTopSitesProvider: DefaultTopSitesProvider? = null,
) : TopSitesStorage, Observable<TopSitesStorage.Observer> by ObserverRegistry() {

    private val logger = Logger("DefaultTopSitesStorage")

    // Cache of the last retrieved top sites
    var cachedTopSites = listOf<TopSite>()

    companion object {
        /**
         * Creates a [DefaultTopSitesStorage] and optionally initializes it with a list of default top sites.
         *
         * @param pinnedSitesStorage An instance of [PinnedSiteStorage], used for storing pinned sites.
         * @param historyStorage An instance of [PlacesHistoryStorage], used for retrieving top frecent sites from
         *   history.
         * @param topSitesProvider An optional instance of [TopSitesProvider], used for retrieving additional top sites
         *   from a provider.
         * @param defaultTopSitesProvider An optional instance of [DefaultTopSitesProvider], used for retrieving the
         *   default top sites bundled with the application.
         * @param defaultTopSites A list of [Pair]s (title to URL) representing the default top sites to be added to the
         *   storage if they do not already exist.
         * @return A new instance of [DefaultTopSitesStorage].
         */
        suspend operator fun invoke(
            pinnedSitesStorage: PinnedSiteStorage,
            historyStorage: PlacesHistoryStorage,
            topSitesProvider: TopSitesProvider? = null,
            defaultTopSitesProvider: DefaultTopSitesProvider? = null,
            defaultTopSites: List<Pair<String, String>>,
        ): DefaultTopSitesStorage {
            val storage =
                DefaultTopSitesStorage(
                    pinnedSitesStorage,
                    historyStorage,
                    topSitesProvider,
                    defaultTopSitesProvider,
                )
            if (defaultTopSites.isNotEmpty()) {
                pinnedSitesStorage.addAllPinnedSites(defaultTopSites, isDefault = true)
            }
            return storage
        }

        /**
         * Creates a [DefaultTopSitesStorage].
         *
         * @param pinnedSitesStorage An instance of [PinnedSiteStorage], used for storing pinned sites.
         * @param historyStorage An instance of [PlacesHistoryStorage], used for retrieving top frecent sites from
         *   history.
         * @param topSitesProvider An optional instance of [TopSitesProvider], used for retrieving additional top sites
         *   from a provider.
         * @param defaultTopSitesProvider An optional instance of [DefaultTopSitesProvider], used for retrieving the
         *   default top sites bundled with the application.
         * @return A new instance of [DefaultTopSitesStorage].
         */
        operator fun invoke(
            pinnedSitesStorage: PinnedSiteStorage,
            historyStorage: PlacesHistoryStorage,
            topSitesProvider: TopSitesProvider? = null,
            defaultTopSitesProvider: DefaultTopSitesProvider? = null,
        ) = DefaultTopSitesStorage(pinnedSitesStorage, historyStorage, topSitesProvider, defaultTopSitesProvider)
    }

    override suspend fun addTopSite(title: String, url: String, isDefault: Boolean) {
        pinnedSitesStorage.addPinnedSite(title, url, isDefault)
        notifyObservers { onStorageUpdated() }
    }

    override suspend fun addTopSites(topSites: List<Pair<String, String>>, isDefault: Boolean) {
        pinnedSitesStorage.addAllPinnedSites(topSites = topSites, isDefault = isDefault)
        notifyObservers { onStorageUpdated() }
    }

    override suspend fun removeTopSite(topSite: TopSite) {
        if (topSite is TopSite.DefaultPinned || topSite is TopSite.Pinned) {
            pinnedSitesStorage.removePinnedSite(topSite)
        }

        // Remove the top site from both history and pinned sites storage to avoid having it
        // show up as a frecent site if it is a pinned site.
        if (topSite !is TopSite.Provided) {
            historyStorage.deleteVisitsFor(topSite.url)
        }

        notifyObservers { onStorageUpdated() }
    }

    override suspend fun updateTopSite(topSite: TopSite, title: String, url: String) {
        if (topSite is TopSite.DefaultPinned || topSite is TopSite.Pinned) {
            pinnedSitesStorage.updatePinnedSite(topSite, title, url)
        }

        notifyObservers { onStorageUpdated() }
    }

    /**
     * Return a unified list of [TopSite]s. Returns all the pinned sites available and uses [targetSitesCount] to
     * backfill any remaining top site slots using frecent and provider sites so that the total number of top sites
     * should have a minimum size of [targetSitesCount] if possible, however, this can return fewer if there are no
     * sites to backfill with.
     */
    @Suppress("ComplexCondition", "TooGenericExceptionCaught")
    override suspend fun getTopSites(
        targetSitesCount: Int,
        frecencyConfig: TopSitesFrecencyConfig?,
        providerConfig: TopSitesProviderConfig?,
    ): List<TopSite> {
        val topSites = ArrayList<TopSite>()
        val pinnedSites = pinnedSitesStorage.getPinnedSites()
        var providerTopSites = emptyList<TopSite>()
        var numSitesRequired = (targetSitesCount - pinnedSites.size).coerceAtLeast(0)

        if (
            numSitesRequired > 0 &&
                topSitesProvider != null &&
                providerConfig != null &&
                providerConfig.showProviderTopSites &&
                pinnedSites.size < providerConfig.maxThreshold
        ) {
            try {
                providerTopSites =
                    topSitesProvider
                        .getTopSites(allowCache = true)
                        .filter { providerConfig.providerFilter?.invoke(it) ?: true }
                        // The total amount of pinned and provider top sites that are returned in
                        // `topSites` should not exceed the `providerConfig.maxThreshold`.
                        // To maintain this constraint, first determine the amount of provider top
                        // sites that can be taken from the available pool provider top sites, and
                        // then ensure the number of provider top sites added does not exceed the
                        // `limit`.
                        .take(providerConfig.maxThreshold - pinnedSites.size)
                        .take(providerConfig.limit)
                topSites.addAll(providerTopSites)
                numSitesRequired -= providerTopSites.size
            } catch (e: Exception) {
                logger.error("Failed to fetch top sites from provider", e)
            }
        }

        topSites.addAll(pinnedSites)

        if (frecencyConfig?.frecencyTresholdOption != null && numSitesRequired > 0) {
            // Get `targetSitesCount` number of frecent sites from history storage to account for potential duplicates
            // and filtering needed with provider and pinned sites.
            val numItems = targetSitesCount
            val frecentSites =
                historyStorage
                    .getTopFrecentSites(numItems = numItems, frecencyThreshold = frecencyConfig.frecencyTresholdOption)
                    .map { it.toTopSite() }
                    .filter {
                        !pinnedSites.hasUrl(it.url) &&
                            !providerTopSites.hasHost(it.url) &&
                            frecencyConfig.frecencyFilter?.invoke(it) ?: true
                    }
                    .take(numSitesRequired)

            topSites.addAll(frecentSites)
            numSitesRequired -= frecentSites.size
        }

        topSites.addAll(backfillTopSites(currentTopSites = topSites, numSitesRequired = numSitesRequired))

        if (topSites != cachedTopSites) {
            emitTopSitesCountFact(pinnedSites.size)
            cachedTopSites = topSites
        }

        return topSites
    }

    /**
     * Returns the default top sites that backfill the [numSitesRequired] slots left available in [currentTopSites], and
     * skips any that are already there.
     */
    @Suppress("TooGenericExceptionCaught")
    private suspend fun backfillTopSites(
        currentTopSites: List<TopSite>,
        numSitesRequired: Int,
    ): List<TopSite.Default> {
        if (defaultTopSitesProvider == null || numSitesRequired <= 0) {
            return emptyList()
        }

        return try {
            defaultTopSitesProvider
                .getDefaultTopSites()
                .filterNot { currentTopSites.hasUrl(it.url) }
                .take(numSitesRequired)
        } catch (e: CancellationException) {
            throw e
        } catch (e: Exception) {
            logger.error("Failed to fetch the default top sites from provider", e)
            emptyList()
        }
    }
}
