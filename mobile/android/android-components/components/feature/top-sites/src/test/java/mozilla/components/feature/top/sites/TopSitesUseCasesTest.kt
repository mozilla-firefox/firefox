/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.top.sites

import kotlinx.coroutines.test.runTest
import mozilla.components.support.test.mock
import org.junit.Test
import org.mockito.Mockito.verify

class TopSitesUseCasesTest {

    @Test
    fun `AddPinnedSiteUseCase`() = runTest {
        val topSitesStorage: TopSitesStorage = mock()
        val useCases = TopSitesUseCases(topSitesStorage)

        useCases.addPinnedSites("Mozilla", "https://www.mozilla.org", isDefault = true)
        verify(topSitesStorage)
            .addTopSite(
                "Mozilla",
                "https://www.mozilla.org",
                isDefault = true,
            )
    }

    @Test
    fun `RemoveTopSiteUseCase`() = runTest {
        val topSitesStorage: TopSitesStorage = mock()
        val topSite =
            TopSite.DefaultPinned(
                id = 1,
                title = "Firefox",
                url = "https://firefox.com",
                createdAt = 1,
            )

        val useCases = TopSitesUseCases(topSitesStorage)

        useCases.removeTopSites(topSite)

        verify(topSitesStorage).removeTopSite(topSite)
    }

    @Test
    fun `UpdateTopSiteUseCase`() = runTest {
        val topSitesStorage: TopSitesStorage = mock()
        val topSite =
            TopSite.DefaultPinned(
                id = 1,
                title = "Firefox",
                url = "https://firefox.com",
                createdAt = 1,
            )

        val useCases = TopSitesUseCases(topSitesStorage)

        val title = "New title"
        val url = "https://www.example.com/new-url"
        useCases.updateTopSites(topSite, title, url)

        verify(topSitesStorage).updateTopSite(topSite, title, url)
    }
}
