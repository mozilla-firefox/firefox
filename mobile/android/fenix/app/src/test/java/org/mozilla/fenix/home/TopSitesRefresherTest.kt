/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home

import io.mockk.every
import io.mockk.mockk
import mozilla.components.feature.top.sites.TopSite
import mozilla.components.feature.top.sites.TopSitesProvider
import mozilla.components.support.test.rule.MainCoroutineRule
import mozilla.components.support.test.rule.runTestOnMain
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.helpers.lifecycle.TestLifecycleOwner
import org.mozilla.fenix.utils.Settings

/**
 * Class to test the [TopSitesRefresher]
 */
class TopSitesRefresherTest {

    @get:Rule
    val coroutinesTestRule = MainCoroutineRule()

    private val lifecycleOwner = TestLifecycleOwner()

    private val topSitesProvider = FakeTopSitesProvider()
    private val settings: Settings = mockk(relaxed = true)

    @Before
    fun setUp() {
        lifecycleOwner.registerObserver(
            observer = TopSitesRefresher(
                settings = settings,
                topSitesProvider = topSitesProvider,
                dispatcher = coroutinesTestRule.testDispatcher,
            ),
        )
    }

    @Test
    fun `WHEN lifecycle resumes AND we want to show contile feature THEN top sites are refreshed`() =
        runTestOnMain {
            every { settings.showContileFeature } returns true

            lifecycleOwner.onResume()

            assertTrue(topSitesProvider.cacheRefreshed)
        }

    @Test
    fun `WHEN lifecycle resumes AND we DO NOT want to show contile feature THEN top sites are NOT refreshed`() =
        runTestOnMain {
            every { settings.showContileFeature } returns false

            lifecycleOwner.onResume()

            assertFalse(topSitesProvider.cacheRefreshed)
        }

    private class FakeTopSitesProvider : TopSitesProvider {

        var expectedTopSites: List<TopSite> = emptyList()
        var cacheRefreshed: Boolean = false

        override suspend fun getTopSites(allowCache: Boolean): List<TopSite> = expectedTopSites

        override suspend fun refreshTopSitesIfCacheExpired() {
            cacheRefreshed = true
        }
    }
}
