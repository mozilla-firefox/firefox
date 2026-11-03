/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.blocklist

import io.mockk.Runs
import io.mockk.every
import io.mockk.just
import io.mockk.mockk
import io.mockk.slot
import mozilla.components.browser.state.state.ContentState
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.concept.sync.DeviceType
import mozilla.components.feature.top.sites.TopSite
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.home.bookmarks.Bookmark
import org.mozilla.fenix.home.recentsyncedtabs.RecentSyncedTab
import org.mozilla.fenix.home.recentsyncedtabs.RecentSyncedTabState
import org.mozilla.fenix.home.recenttabs.RecentTab
import org.mozilla.fenix.home.recentvisits.RecentlyVisitedItem
import org.mozilla.fenix.utils.Settings
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class BlocklistHandlerTest {
    private val mockSettings: Settings = mockk()

    private lateinit var blocklistHandler: BlocklistHandler

    @Before
    fun setup() {
        blocklistHandler = BlocklistHandler(mockSettings)
    }

    @Test
    fun `WHEN url added to blocklist THEN settings updated with hash`() {
        val addedUrl = "url"
        val updateSlot = slot<Set<String>>()
        every { mockSettings.homescreenBlocklist } returns setOf()
        every { mockSettings.homescreenBlocklist = capture(updateSlot) } just Runs

        blocklistHandler.addUrlToBlocklist(addedUrl)

        assertEquals(setOf(addedUrl.stripAndHash()), updateSlot.captured)
    }

    @Test
    fun `WHEN url removed from blocklist THEN settings updated without its hash`() {
        val removedUrl = "https://www.mozilla.org/"
        val url = "https://www.firefox.com/"
        val updateSlot = slot<Set<String>>()
        every { mockSettings.homescreenBlocklist } returns setOf(removedUrl.stripAndHash(), url.stripAndHash())
        every { mockSettings.homescreenBlocklist = capture(updateSlot) } just Runs

        blocklistHandler.removeUrlFromBlocklist(removedUrl)

        assertEquals(setOf(url.stripAndHash()), updateSlot.captured)
    }

    @Test
    fun `GIVEN bookmark is not in blocklist THEN will not be filtered`() {
        val bookmarks = listOf(Bookmark(url = "test"))
        every { mockSettings.homescreenBlocklist } returns setOf()

        val filtered =
            with(blocklistHandler) {
                bookmarks.filteredByBlocklist()
            }

        assertEquals(bookmarks, filtered)
    }

    @Test
    fun `GIVEN bookmark is in blocklist THEN will be filtered`() {
        val blockedUrl = "test"
        val bookmarks = listOf(Bookmark(url = blockedUrl))
        every { mockSettings.homescreenBlocklist } returns setOf(blockedUrl.stripAndHash())

        val filtered =
            with(blocklistHandler) {
                bookmarks.filteredByBlocklist()
            }

        assertEquals(listOf<String>(), filtered)
    }

    @Test
    fun `GIVEN recent history is not in blocklist THEN will not be filtered`() {
        val recentHistory = listOf(RecentlyVisitedItem.RecentHistoryHighlight(url = "test", title = ""))
        every { mockSettings.homescreenBlocklist } returns setOf()

        val filtered =
            with(blocklistHandler) {
                recentHistory.filteredByBlocklist()
            }

        assertEquals(recentHistory, filtered)
    }

    @Test
    fun `GIVEN recent history is in blocklist THEN will be filtered`() {
        val blockedUrl = "test"
        val recentHistory = listOf(RecentlyVisitedItem.RecentHistoryHighlight(url = blockedUrl, title = ""))
        every { mockSettings.homescreenBlocklist } returns setOf(blockedUrl.stripAndHash())

        val filtered =
            with(blocklistHandler) {
                recentHistory.filteredByBlocklist()
            }

        assertEquals(listOf<String>(), filtered)
    }

    @Test
    fun `GIVEN recent tab is not in blocklist THEN will not be filtered`() {
        val mockSessionState: TabSessionState = mockk()
        val mockContent: ContentState = mockk()
        val tabs = listOf(RecentTab.Tab(mockSessionState))
        every { mockSessionState.content } returns mockContent
        every { mockContent.url } returns "test"
        every { mockSettings.homescreenBlocklist } returns setOf()

        val filtered =
            with(blocklistHandler) {
                tabs.filteredByBlocklist()
            }

        assertEquals(tabs, filtered)
    }

    @Test
    fun `GIVEN recent tab is in blocklist THEN will be filtered`() {
        val blockedUrl = "test"
        val mockSessionState: TabSessionState = mockk()
        val mockContent: ContentState = mockk()
        val tabs = listOf(RecentTab.Tab(mockSessionState))
        every { mockSessionState.content } returns mockContent
        every { mockContent.url } returns blockedUrl
        every { mockSettings.homescreenBlocklist } returns setOf(blockedUrl.stripAndHash())

        val filtered =
            with(blocklistHandler) {
                tabs.filteredByBlocklist()
            }

        assertEquals(listOf<String>(), filtered)
    }

    @Test
    fun `GIVEN recently synced tab is a sponsored url WHEN the tabs are filtered THEN the sponsored url be filtered`() {
        val blockedUrl = "test.com/?query=value"
        val mockSessionState: TabSessionState = mockk()
        val mockContent: ContentState = mockk()
        val tabs =
            RecentSyncedTabState.Success(
                listOf(
                    RecentSyncedTab(
                        "",
                        DeviceType.DESKTOP,
                        "title",
                        blockedUrl,
                        null,
                    )
                )
            )
        every { mockSessionState.content } returns mockContent
        every { mockContent.url } returns blockedUrl
        every { mockSettings.frecencyFilterQuery } returns "query=value"

        val filtered =
            with(blocklistHandler) {
                tabs.filterContile()
            }

        assertEquals(RecentSyncedTabState.None, filtered)
    }

    @Test
    fun `GIVEN recently visited item is a sponsored url WHEN the tabs are filtered THEN the sponsored url be filtered`() {
        val blockedUrl = "test.com/?query=value"
        val mockSessionState: TabSessionState = mockk()
        val mockContent: ContentState = mockk()
        val tabs = listOf(RecentlyVisitedItem.RecentHistoryHighlight("title", blockedUrl))
        every { mockSessionState.content } returns mockContent
        every { mockContent.url } returns blockedUrl
        every { mockSettings.frecencyFilterQuery } returns "query=value"

        val filtered =
            with(blocklistHandler) {
                tabs.filterContile()
            }

        assertEquals(listOf<String>(), filtered)
    }

    @Test
    fun `GIVEN recent tab is a sponsored url WHEN the tabs are filtered THEN the sponsored url be filtered`() {
        val blockedUrl = "test.com/?query=value"
        val mockSessionState: TabSessionState = mockk()
        val mockContent: ContentState = mockk()
        val tabs = listOf(RecentTab.Tab(mockSessionState))
        every { mockSessionState.content } returns mockContent
        every { mockContent.url } returns blockedUrl
        every { mockSettings.frecencyFilterQuery } returns "query=value"

        val filtered =
            with(blocklistHandler) {
                tabs.filterContile()
            }

        assertEquals(listOf<String>(), filtered)
    }

    @Test
    fun `GIVEN top site is not in blocklist WHEN the top sites are filtered THEN it will not be filtered`() {
        val topSites = listOf(defaultTopSite(url = "https://www.firefox.com/"))
        every { mockSettings.homescreenBlocklist } returns setOf()

        val filtered =
            with(blocklistHandler) {
                topSites.filteredByBlocklist()
            }

        assertEquals(topSites, filtered)
    }

    @Test
    fun `GIVEN top site is in blocklist WHEN the top sites are filtered THEN it will be filtered`() {
        val blockedUrl = "https://www.firefox.com/"
        var topSites = listOf(defaultTopSite(url = blockedUrl))
        every { mockSettings.homescreenBlocklist } returns setOf(blockedUrl.stripAndHash())

        var filtered =
            with(blocklistHandler) {
                topSites.filteredByBlocklist()
            }

        assertEquals(listOf<TopSite>(), filtered)

        val topSite = defaultTopSite(url = "https://www.mozilla.com/")
        topSites = listOf(defaultTopSite(url = blockedUrl), topSite)

        filtered =
            with(blocklistHandler) {
                topSites.filteredByBlocklist()
            }

        assertEquals(listOf(topSite), filtered)
    }

    @Test
    fun `GIVEN a blocked top site url WHEN top site shares its host but not its path THEN the blocked top site is filtered and top site is not filtered`() {
        val blockedUrl = "https://mozilla.org/blocked"
        val topSite = defaultTopSite(url = "https://mozilla.org/firefox")
        val topSites = listOf(topSite, defaultTopSite(url = blockedUrl))
        every { mockSettings.homescreenBlocklist } returns setOf(blockedUrl.stripAndHash())

        val filtered =
            with(blocklistHandler) {
                topSites.filteredByBlocklist()
            }

        assertEquals(listOf(topSite), filtered)
    }

    @Test
    fun `GIVEN a list of top sites WHEN filtered by the blocklist THEN only the blocked url is filtered`() {
        val blockedUrl = "https://www.firefox.com/"
        val allowedTopSites =
            listOf(
                frecentTopSite(url = "https://www.wikipedia.org/"),
                providedTopSite(url = "https://www.mozilla.com/"),
            )
        val topSites = listOf(defaultTopSite(url = blockedUrl)) + allowedTopSites
        every { mockSettings.homescreenBlocklist } returns setOf(blockedUrl.stripAndHash())

        val filtered =
            with(blocklistHandler) {
                topSites.filteredByBlocklist()
            }

        assertEquals(allowedTopSites, filtered)
    }

    @Test
    fun `GIVEN urls with different paths WHEN the URL is compared with a blocklist entry THEN return whether they are the same blocklist entry`() {
        assertTrue("https://www.wikipedia.org/".hasSameBlocklistEntryAs("wikipedia.org"))
        assertTrue("http://m.wikipedia.org".hasSameBlocklistEntryAs("https://wikipedia.org/"))
        assertTrue("https://www.wikipedia.org/sampleArticle/".hasSameBlocklistEntryAs("wikipedia.org/sampleArticle"))
        assertFalse("https://www.wikipedia.org/".hasSameBlocklistEntryAs("https://ja.wikipedia.org/"))
        assertFalse("https://wikipedia.org".hasSameBlocklistEntryAs("https://wikipedia.org/sampleArticle"))
        assertFalse("https://wikipedia.org/sampleArticle".hasSameBlocklistEntryAs("https://wikipedia.org/otherArticle"))
    }

    private fun defaultTopSite(url: String) = TopSite.Default(id = null, title = url, url = url, createdAt = null)

    private fun frecentTopSite(url: String) = TopSite.Frecent(id = null, title = url, url = url, createdAt = null)

    private fun providedTopSite(url: String) =
        TopSite.Provided(
            id = null,
            title = url,
            url = url,
            clickUrl = url,
            imageUrl = url,
            impressionUrl = url,
            createdAt = null,
        )
}
