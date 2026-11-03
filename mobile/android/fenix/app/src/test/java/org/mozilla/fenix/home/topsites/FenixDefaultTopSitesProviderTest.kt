/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.topsites

import android.content.res.Resources
import io.mockk.every
import io.mockk.mockk
import io.mockk.verify
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.action.SearchAction
import mozilla.components.browser.state.search.RegionState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.feature.top.sites.TopSite
import mozilla.components.lib.crash.CrashReporter
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Test
import org.mozilla.fenix.R
import org.mozilla.fenix.home.blocklist.BlocklistHandler
import org.mozilla.fenix.home.blocklist.stripAndHash
import org.mozilla.fenix.utils.Settings

class FenixDefaultTopSitesProviderTest {

    private lateinit var browserStore: BrowserStore
    private lateinit var settings: Settings
    private lateinit var resources: Resources
    private lateinit var crashReporter: CrashReporter

    private val dispatcher = StandardTestDispatcher()

    private val usRegionSite = defaultTopSite(id = 1, title = "US Region Site", url = "https://www.example1.com/")
    private val caExcludedRegionSite =
        defaultTopSite(id = 2, title = "CA Excluded Region Site", url = "https://www.example2.com/")
    private val allRegionSite = defaultTopSite(id = 3, title = "All Region Site", url = "https://www.example3.com/")
    private val example4 = defaultTopSite(id = 4, title = "www.example4.com", url = "https://www.example4.com/")
    private val example5 = defaultTopSite(id = 5, title = "www.example5.com", url = "https://www.example5.com/")
    private val example6 = defaultTopSite(id = 6, title = "www.example6.com", url = "https://www.example6.com/")
    private val example7 = defaultTopSite(id = 7, title = "www.example7.com", url = "https://www.example7.com/")
    private val example8 = defaultTopSite(id = 8, title = "www.example8.com", url = "https://www.example8.com/")

    private var experiments = emptySet<String>()

    @Before
    fun setUp() {
        settings = mockk(relaxed = true)
        resources = mockk(relaxed = true)
        crashReporter = mockk(relaxed = true)
        browserStore = BrowserStore()

        every { resources.openRawResource(R.raw.default_shortcuts) } answers
            {
                this.javaClass.classLoader!!.getResourceAsStream("raw/test_default_pinned_shortcuts.json")!!
            }

        every { settings.homescreenBlocklist } returns setOf()
    }

    @Test
    fun `WHEN the default top sites are requested THEN the sites for that region are returned`() =
        runTest(dispatcher) {
            setRegion("US")

            val topSites = createProvider().getDefaultTopSites()

            assertEquals(
                listOf(usRegionSite, caExcludedRegionSite, allRegionSite, example4, example5, example6, example7),
                topSites,
            )
        }

    @Test
    fun `GIVEN a region that is in the exclusion of a site WHEN the default top sites are requested THEN the sites for that region are returned`() =
        runTest(dispatcher) {
            setRegion("CA")

            val topSites = createProvider().getDefaultTopSites()

            assertEquals(listOf(allRegionSite, example4, example5, example6, example7), topSites)
        }

    @Test
    fun `GIVEN no region is set WHEN the default top sites are requested THEN the sites for the default region are returned`() =
        runTest(dispatcher) {
            val topSites = createProvider().getDefaultTopSites()

            assertEquals(
                listOf(caExcludedRegionSite, allRegionSite, example4, example5, example6, example7),
                topSites,
            )
        }

    @Test
    fun `GIVEN a dismissed site WHEN the default top sites are requested THEN the dismissed site is not returned`() =
        runTest(dispatcher) {
            setRegion("US")
            every { settings.homescreenBlocklist } returns setOf(allRegionSite.url.stripAndHash())

            val topSites = createProvider().getDefaultTopSites()

            assertEquals(
                listOf(usRegionSite, caExcludedRegionSite, example4, example5, example6, example7),
                topSites,
            )
        }

    @Test
    fun `WHEN the default top sites are requested more than once THEN the raw resource is only read once`() =
        runTest(dispatcher) {
            setRegion("US")
            val provider = createProvider()

            provider.getDefaultTopSites()
            provider.getDefaultTopSites()

            verify(exactly = 1) { resources.openRawResource(R.raw.default_shortcuts) }
        }

    @Test
    fun `GIVEN an enrolled experiment WHEN the default top sites are requested THEN the sites are filtered by that experiment`() =
        runTest(dispatcher) {
            setRegion("US")
            experiments = setOf("test")

            val topSites = createProvider().getDefaultTopSites()

            assertEquals(
                listOf(usRegionSite, caExcludedRegionSite, allRegionSite, example4, example5, example6, example8),
                topSites,
            )
        }

    @Test
    fun `GIVEN the region changed WHEN the default top sites are requested THEN the sites for the new region are returned`() =
        runTest(dispatcher) {
            setRegion("US")
            val provider = createProvider()

            assertEquals(
                listOf(usRegionSite, caExcludedRegionSite, allRegionSite, example4, example5, example6, example7),
                provider.getDefaultTopSites(),
            )

            setRegion("CA")

            assertEquals(
                listOf(allRegionSite, example4, example5, example6, example7),
                provider.getDefaultTopSites(),
            )
        }

    private fun setRegion(region: String) {
        browserStore.dispatch(SearchAction.SetRegionAction(RegionState(home = region, current = region)))
    }

    private fun createProvider() =
        FenixDefaultTopSitesProvider(
            browserStore = browserStore,
            resources = resources,
            crashReporter = crashReporter,
            blocklistHandler = BlocklistHandler(settings),
            experiments = { experiments },
            ioDispatcher = dispatcher,
        )

    private fun defaultTopSite(id: Long, title: String, url: String) =
        TopSite.Default(id = id, title = title, url = url, createdAt = null)
}
