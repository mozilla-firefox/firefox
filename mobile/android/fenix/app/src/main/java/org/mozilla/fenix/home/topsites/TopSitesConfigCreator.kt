/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.topsites

import androidx.core.net.toUri
import mozilla.components.browser.state.state.selectedOrDefaultSearchEngine
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.storage.FrecencyThresholdOption
import mozilla.components.feature.top.sites.TopSitesConfig
import mozilla.components.feature.top.sites.TopSitesFeature
import mozilla.components.feature.top.sites.TopSitesFrecencyConfig
import mozilla.components.feature.top.sites.TopSitesProviderConfig
import org.mozilla.fenix.ext.containsQueryParameters
import org.mozilla.fenix.home.HomeFragment
import org.mozilla.fenix.home.topsites.TopSitesConfigConstants.AMAZON_SEARCH_ENGINE_NAME
import org.mozilla.fenix.home.topsites.TopSitesConfigConstants.AMAZON_SPONSORED_TITLE
import org.mozilla.fenix.home.topsites.TopSitesConfigConstants.EBAY_SPONSORED_TITLE
import org.mozilla.fenix.home.topsites.TopSitesConfigConstants.TOP_SITES_PROVIDER_LIMIT
import org.mozilla.fenix.home.topsites.TopSitesConfigConstants.TOP_SITES_PROVIDER_MAX_THRESHOLD
import org.mozilla.fenix.nimbus.FxNimbus
import org.mozilla.fenix.utils.Settings

/**
 * Top level function that creates [TopSitesConfig] for Fenix based on information from the [BrowserStore]
 * and [Settings].
 *
 * This is meant to be used with the [TopSitesFeature] and it exists instead of the lambda which
 * holds an implicit reference to the [HomeFragment].
 */
internal fun getTopSitesConfig(
    settings: Settings,
    store: BrowserStore,
): () -> TopSitesConfig {
    return {
        TopSitesConfig(
            totalSites = settings.topSitesMaxLimit,
            frecencyConfig = if (FxNimbus.features.homepageHideFrecentTopSites.value().enabled) {
                null
            } else {
                TopSitesFrecencyConfig(
                    frecencyTresholdOption = FrecencyThresholdOption.SKIP_ONE_TIME_PAGES,
                ) { !it.url.toUri().containsQueryParameters(settings.frecencyFilterQuery) }
            },
            providerConfig = TopSitesProviderConfig(
                showProviderTopSites = settings.showContileFeature,
                limit = TOP_SITES_PROVIDER_LIMIT,
                maxThreshold = TOP_SITES_PROVIDER_MAX_THRESHOLD,
                providerFilter = { topSite ->
                    when (store.state.search.selectedOrDefaultSearchEngine?.name) {
                        AMAZON_SEARCH_ENGINE_NAME -> topSite.title != AMAZON_SPONSORED_TITLE
                        EBAY_SPONSORED_TITLE -> topSite.title != EBAY_SPONSORED_TITLE
                        else -> true
                    }
                },
            ),
        )
    }
}
