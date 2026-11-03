/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.topsites

import android.content.res.Resources
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.mapNotNull
import kotlinx.coroutines.withContext
import mozilla.components.browser.state.search.RegionState
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.feature.top.sites.DefaultTopSitesProvider
import mozilla.components.feature.top.sites.DefaultTopSitesStorage
import mozilla.components.feature.top.sites.PinnedSiteStorage
import mozilla.components.lib.crash.CrashReporter
import mozilla.components.lib.state.helpers.AbstractBinding
import org.mozilla.fenix.Config
import org.mozilla.fenix.R
import org.mozilla.fenix.home.blocklist.BlocklistHandler
import org.mozilla.fenix.home.topsites.utils.fetchDefaultTopSites
import org.mozilla.fenix.utils.Settings

/**
 * The default top sites that were previously seeded as default pinned sites into the pinned sites storage. Only these
 * could have been removed by the user, so only these are migrated to the homescreen blocklist.
 */
private val LEGACY_DEFAULT_PINNED_SITE_URLS =
    setOf(
        "https://www.wikipedia.org/",
        "https://tenki.jp/",
        "https://m.yahoo.co.jp/",
        "https://ameblo.jp/",
        "https://ja.wikipedia.org/wiki/%E3%83%A1%E3%82%A4%E3%83%B3%E3%83%9A%E3%83%BC%E3%82%B8",
    )

/**
 * A binding for observing [RegionState] and adding the default pinned sites that are included in the application on the
 * first run of the application.
 *
 * @param browserStore The [BrowserStore] to observe state changes.
 * @param topSitesStorage An instance of the [DefaultTopSitesStorage] used to add the default pinned sites.
 * @param pinnedSitesStorage An instance of the [PinnedSiteStorage] used to find the default top sites that the user has
 *   already removed.
 * @param defaultTopSitesProvider An instance of the [DefaultTopSitesProvider] holding the default top sites that would
 *   be served by the application.
 * @param blocklistHandler [BlocklistHandler] used to record the default top sites the user had removed.
 * @param settings [Settings] used for accessing the application preferences.
 * @param resources [Resources] used for accessing application resources.
 * @param crashReporter [CrashReporter] used for recording caught exceptions.
 * @param isReleased Whether or not the build is in a release channel.
 * @param mainDispatcher The dispatcher on which to observe state changes.
 * @param ioDispatcher The dispatcher used for I/O operations, specifically reading and parsing the default pinned
 *   shortcuts JSON.
 */
@Suppress("LongParameterList")
class DefaultPinnedSitesBinding(
    browserStore: BrowserStore,
    private val topSitesStorage: DefaultTopSitesStorage,
    private val pinnedSitesStorage: PinnedSiteStorage,
    private val defaultTopSitesProvider: DefaultTopSitesProvider,
    private val blocklistHandler: BlocklistHandler,
    private val settings: Settings,
    private val resources: Resources,
    private val crashReporter: CrashReporter,
    private val isReleased: Boolean = Config.channel.isReleased,
    mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
) : AbstractBinding<BrowserState>(browserStore, mainDispatcher = mainDispatcher) {

    override suspend fun onState(flow: Flow<BrowserState>) {
        if (settings.defaultPinnedTopSitesAdded && settings.removedDefaultPinnedTopSitesMigrated) return

        flow
            .mapNotNull { it.search.region }
            .distinctUntilChanged()
            .collect { regionState ->
                if (isReleased && regionState == RegionState.Default) {
                    return@collect
                }

                if (settings.defaultPinnedTopSitesAdded) {
                    migrateDismissedDefaultTopSites()
                } else {
                    addDefaultPinnedSites(region = regionState.current)
                }
            }
    }

    private suspend fun addDefaultPinnedSites(region: String) {
        val defaultPinnedSites = getPinnedSites(region = region)

        if (defaultPinnedSites.isNotEmpty()) {
            topSitesStorage.addTopSites(
                topSites = defaultPinnedSites.map { it.title to it.url },
                isDefault = true,
            )
            settings.defaultPinnedTopSitesAdded = true
            // A fresh install has no previously removed default pinned top sites to migrate.
            settings.removedDefaultPinnedTopSitesMigrated = true
        }
    }

    /**
     * Adds the default top sites that are no longer in the pinned sites storage to the homescreen blocklist. This only
     * applies to existing users.
     *
     * Prior to the introduction of "default" and "default pinned" sites, the removal of a "default pinned" site from
     * the pinned site storage would indicate that the user had removed it and made it so that it won't reappear. Now
     * that [FenixDefaultTopSitesProvider] is used to serve "default" top sites that act as backfills of the remaining
     * top site slots after showing default pinned, user pinned and frecent sites. We need to migrate previously removed
     * default pinned top sites to the blocklist to ensure that they are not shown again.
     *
     * Only [LEGACY_DEFAULT_PINNED_SITE_URLS] are considered, so that a default top site that was never seeded as a
     * default pinned site, such as one added after this migration was written, is not mistaken for a removed one.
     */
    private suspend fun migrateDismissedDefaultTopSites() {
        val pinnedUrls = pinnedSitesStorage.getPinnedSites().map { it.url }

        defaultTopSitesProvider
            .getDefaultTopSites()
            .filter { it.url in LEGACY_DEFAULT_PINNED_SITE_URLS && it.url !in pinnedUrls }
            .forEach { blocklistHandler.addUrlToBlocklist(it.url) }

        settings.removedDefaultPinnedTopSitesMigrated = true
    }

    internal suspend fun getPinnedSites(region: String): List<DefaultTopSite> =
        withContext(ioDispatcher) {
            fetchDefaultTopSites(
                    resources = resources,
                    rawResId = R.raw.default_pinned_shortcuts,
                    crashReporter = crashReporter,
                    region = region,
                )
                .map { it.toDefaultTopSite() }
        }
}
