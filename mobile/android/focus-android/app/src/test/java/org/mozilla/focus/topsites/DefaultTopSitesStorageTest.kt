/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.topsites

import kotlinx.coroutines.test.runTest
import mozilla.components.feature.top.sites.PinnedSiteStorage
import mozilla.components.feature.top.sites.TopSite
import mozilla.components.support.test.mock
import mozilla.components.support.test.whenever
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.mockito.Mockito.verify

class DefaultTopSitesStorageTest {

    private val pinnedSitesStorage: PinnedSiteStorage = mock()

    @Test
    fun `WHEN a top site is added THEN the pinned sites storage is called`() = runTest {
        val defaultTopSitesStorage = DefaultTopSitesStorage(pinnedSitesStorage)

        defaultTopSitesStorage.addTopSite("Mozilla", "https://mozilla.com", isDefault = false)

        verify(pinnedSitesStorage)
            .addPinnedSite(
                "Mozilla",
                "https://mozilla.com",
                isDefault = false,
            )
    }

    @Test
    fun `WHEN a top site is removed THEN the pinned sites storage is called`() = runTest {
        val defaultTopSitesStorage = DefaultTopSitesStorage(pinnedSitesStorage)

        val pinnedSite =
            TopSite.Pinned(
                id = 2,
                title = "Firefox",
                url = "https://firefox.com",
                createdAt = 2,
            )

        defaultTopSitesStorage.removeTopSite(pinnedSite)

        verify(pinnedSitesStorage).removePinnedSite(pinnedSite)
    }

    @Test
    fun `WHEN a top site is updated THEN the pinned sites storage is called`() = runTest {
        val defaultTopSitesStorage = DefaultTopSitesStorage(pinnedSitesStorage)

        val pinnedSite =
            TopSite.Pinned(
                id = 2,
                title = "Wikipedia",
                url = "https://wikipedia.com",
                createdAt = 2,
            )
        defaultTopSitesStorage.updateTopSite(
            pinnedSite,
            "Wiki",
            "https://en.wikipedia.org/wiki/Wiki",
        )

        verify(pinnedSitesStorage)
            .updatePinnedSite(
                pinnedSite,
                "Wiki",
                "https://en.wikipedia.org/wiki/Wiki",
            )
    }

    @Test
    fun `WHEN getTopSites is called THEN the appropriate top sites are returned`() = runTest {
        val defaultTopSitesStorage = DefaultTopSitesStorage(pinnedSitesStorage)

        val pinnedSite1 =
            TopSite.Pinned(
                id = 2,
                title = "Wikipedia",
                url = "https://wikipedia.com",
                createdAt = 2,
            )
        val pinnedSite2 =
            TopSite.Pinned(
                id = 3,
                title = "Example",
                url = "https://example.com",
                createdAt = 3,
            )

        whenever(pinnedSitesStorage.getPinnedSites())
            .thenReturn(
                listOf(
                    pinnedSite1,
                    pinnedSite2,
                )
            )

        var topSites =
            defaultTopSitesStorage.getTopSites(
                targetSitesCount = 0,
                frecencyConfig = null,
            )

        assertTrue(topSites.isEmpty())

        topSites = defaultTopSitesStorage.getTopSites(targetSitesCount = 1)

        assertEquals(1, topSites.size)
        assertEquals(pinnedSite1, topSites[0])

        topSites = defaultTopSitesStorage.getTopSites(targetSitesCount = 2)

        assertEquals(2, topSites.size)
        assertEquals(pinnedSite1, topSites[0])
        assertEquals(pinnedSite2, topSites[1])

        topSites = defaultTopSitesStorage.getTopSites(targetSitesCount = 5)

        assertEquals(2, topSites.size)
        assertEquals(pinnedSite1, topSites[0])
        assertEquals(pinnedSite2, topSites[1])
    }
}
