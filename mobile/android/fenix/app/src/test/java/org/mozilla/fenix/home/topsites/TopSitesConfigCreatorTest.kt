/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.topsites

import io.mockk.every
import io.mockk.mockk
import mozilla.components.browser.state.search.SearchEngine
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.SearchState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.feature.top.sites.TopSite
import mozilla.components.feature.top.sites.TopSitesConfig
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.mozilla.fenix.home.topsites.TopSitesConfigConstants.AMAZON_SPONSORED_TITLE
import org.mozilla.fenix.home.topsites.TopSitesConfigConstants.EBAY_SPONSORED_TITLE
import org.mozilla.fenix.nimbus.FxNimbus
import org.mozilla.fenix.nimbus.HomepageHideFrecentTopSites
import org.mozilla.fenix.utils.Settings

/**
 * Tests the top-level function [getTopSitesConfig] we use to create [TopSitesConfig]
 */
class TopSitesConfigCreatorTest {
    private lateinit var settings: Settings

    private val browserStore = BrowserStore(
        initialState = BrowserState(search = SearchState(regionSearchEngines = listOf())),
    )

    @Before
    fun setUp() {
        settings = mockk(relaxed = true)
        every { settings.topSitesMaxLimit } returns 10
    }

    @Test
    fun `WHEN hide top sites flag is not enabled THEN it returns TopSitesConfig with non-null frequencyConfig`() {
        FxNimbus.features.homepageHideFrecentTopSites.withCachedValue(
            HomepageHideFrecentTopSites(
                enabled = false,
            ),
        )

        val topSitesConfig = getTopSitesConfig(settings = settings, store = browserStore).invoke()

        assertNotNull(topSitesConfig.frecencyConfig)
    }

    @Test
    fun `WHEN hide top sites flag is enabled THEN it returns TopSitesConfig with null frequencyConfig`() {
        FxNimbus.features.homepageHideFrecentTopSites.withCachedValue(
            HomepageHideFrecentTopSites(enabled = true),
        )

        val topSitesConfig = getTopSitesConfig(settings = settings, store = browserStore).invoke()

        assertNull(topSitesConfig.frecencyConfig)
    }

    @Test
    fun `GIVEN a topSitesMaxLimit THEN it returns TopSitesConfig with totalSites = topSitesMaxLimit`() {
        val topSitesMaxLimit = 15
        every { settings.topSitesMaxLimit } returns topSitesMaxLimit

        val topSitesConfig = getTopSitesConfig(settings = settings, store = browserStore).invoke()

        assertEquals(topSitesMaxLimit, topSitesConfig.totalSites)
    }

    @Test
    fun `GIVEN the selected search engine is set to eBay THEN providerFilter filters the eBay provided top sites`() {
        val searchEngine: SearchEngine = mockk()
        val browserStore = BrowserStore(
            initialState = BrowserState(
                search = SearchState(
                    regionSearchEngines = listOf(searchEngine),
                ),
            ),
        )

        every { searchEngine.name } returns EBAY_SPONSORED_TITLE

        val eBayTopSite = TopSite.Provided(1L, EBAY_SPONSORED_TITLE, "eBay.com", "", "", "", 0L)
        val amazonTopSite =
            TopSite.Provided(2L, AMAZON_SPONSORED_TITLE, "Amazon.com", "", "", "", 0L)
        val firefoxTopSite = TopSite.Provided(3L, "Firefox", "mozilla.org", "", "", "", 0L)
        val providedTopSites = listOf(eBayTopSite, amazonTopSite, firefoxTopSite)

        val topSitesConfig = getTopSitesConfig(settings = settings, store = browserStore).invoke()

        val filteredProvidedSites = providedTopSites.filter {
            topSitesConfig.providerConfig?.providerFilter?.invoke(it) != false
        }
        assertTrue(filteredProvidedSites.containsAll(listOf(amazonTopSite, firefoxTopSite)))
        assertFalse(filteredProvidedSites.contains(eBayTopSite))
    }
}
