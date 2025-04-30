/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.topsites

/**
 * Constants used for the [mozilla.components.feature.top.sites.TopSitesConfig]
 */
internal object TopSitesConfigConstants {

    /**
     * Only fetch top sites from the [mozilla.components.feature.top.sites.TopSitesProvider]
     * when the number of default and pinned sites are below this maximum threshold.
     */
    internal const val TOP_SITES_PROVIDER_MAX_THRESHOLD = 8

    /**
     * Number of top sites to take from the [mozilla.components.feature.top.sites.TopSitesProvider].
     */
    internal const val TOP_SITES_PROVIDER_LIMIT = 2

    /**
     * The maximum number of top sites to display.
     */
    internal const val TOP_SITES_MAX_COUNT = 16

    /**
     * Sponsored top sites titles for Amazon used for filtering
     */
    const val AMAZON_SPONSORED_TITLE = "Amazon"

    /**
     * Sponsored top sites search engine for Amazon used for filtering
     */
    const val AMAZON_SEARCH_ENGINE_NAME = "Amazon.com"

    /**
     * Sponsored top sites titles for eBay used for filtering
     */
    const val EBAY_SPONSORED_TITLE = "eBay"
}
