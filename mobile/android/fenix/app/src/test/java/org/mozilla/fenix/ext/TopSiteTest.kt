/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ext

import mozilla.components.feature.top.sites.TopSite
import org.junit.Assert.assertEquals
import org.junit.Test
import org.mozilla.fenix.settings.SupportUtils

class TopSiteTest {

    val defaultGoogleTopSite =
        TopSite.DefaultPinned(
            id = 1L,
            title = "Google",
            url = SupportUtils.GOOGLE_URL,
            createdAt = 0,
        )
    val providedSite1 =
        TopSite.Provided(
            id = 3,
            title = "Mozilla",
            url = "https://mozilla.com",
            clickUrl = "https://mozilla.com/click",
            imageUrl = "https://test.com/image2.jpg",
            impressionUrl = "https://example.com",
            createdAt = 3,
        )
    val providedSite2 =
        TopSite.Provided(
            id = 3,
            title = "Firefox",
            url = "https://firefox.com",
            clickUrl = "https://firefox.com/click",
            imageUrl = "https://test.com/image2.jpg",
            impressionUrl = "https://example.com",
            createdAt = 3,
        )
    val pinnedSite1 =
        TopSite.Pinned(
            id = 1L,
            title = "DuckDuckGo",
            url = "https://duckduckgo.com",
            createdAt = 0,
        )
    val pinnedSite2 =
        TopSite.Pinned(
            id = 1L,
            title = "Mozilla",
            url = "mozilla.org",
            createdAt = 0,
        )
    val frecentSite =
        TopSite.Frecent(
            id = 1L,
            title = "Mozilla",
            url = "mozilla.org",
            createdAt = 0,
        )

    @Test
    fun `GIVEN the default Google top site is the first item WHEN the list of top sites is sorted THEN the order doesn't change`() {
        val topSites =
            listOf(
                defaultGoogleTopSite,
                providedSite1,
                providedSite2,
                pinnedSite1,
                pinnedSite2,
                frecentSite,
            )

        assertEquals(topSites.sort(), topSites)
    }

    @Test
    fun `GIVEN the default Google top site is after the provided top sites WHEN the list of top sites is sorted THEN the default Google top site should be first`() {
        val topSites =
            listOf(
                providedSite1,
                providedSite2,
                defaultGoogleTopSite,
                pinnedSite1,
                pinnedSite2,
                frecentSite,
            )
        val expected =
            listOf(
                defaultGoogleTopSite,
                providedSite1,
                providedSite2,
                pinnedSite1,
                pinnedSite2,
                frecentSite,
            )

        assertEquals(topSites.sort(), expected)
    }

    @Test
    fun `GIVEN the default Google top site is the last item WHEN the list of top sites is sorted THEN the default Google top site should be first`() {
        val topSites =
            listOf(
                providedSite1,
                providedSite2,
                pinnedSite1,
                pinnedSite2,
                frecentSite,
                defaultGoogleTopSite,
            )
        val expected =
            listOf(
                defaultGoogleTopSite,
                providedSite1,
                providedSite2,
                pinnedSite1,
                pinnedSite2,
                frecentSite,
            )

        assertEquals(topSites.sort(), expected)
    }
}
