/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.top.sites

import androidx.core.net.toUri
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.storage.sync.PlacesHistoryStorage
import mozilla.components.concept.storage.FrecencyThresholdOption
import mozilla.components.concept.storage.TopFrecentSiteInfo
import mozilla.components.feature.top.sites.ext.toTopSite
import mozilla.components.support.test.any
import mozilla.components.support.test.mock
import mozilla.components.support.test.whenever
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.anyInt
import org.mockito.Mockito.never
import org.mockito.Mockito.verify

private const val WIKIPEDIA_URL = "https://wikipedia.org/"

@RunWith(AndroidJUnit4::class)
class DefaultTopSitesStorageTest {

    private val pinnedSitesStorage: PinnedSiteStorage = mock()
    private val historyStorage: PlacesHistoryStorage = mock()
    private val topSitesProvider: TopSitesProvider = mock()
    private val defaultTopSitesProvider: DefaultTopSitesProvider = mock()

    private val pinnedSite = TopSite.Pinned(id = 1, title = "Mozilla", url = "https://mozilla.com", createdAt = 1)
    private val frecentSite = TopFrecentSiteInfo("https://example.com", "Example")
    private val defaultSiteWikipedia =
        TopSite.Default(id = null, title = "Wikipedia", url = WIKIPEDIA_URL, createdAt = null)
    private val defaultSiteFirefox =
        TopSite.Default(id = null, title = "Firefox", url = "https://firefox.com", createdAt = null)

    @Test
    fun `default top sites are added to pinned site storage on init`() = runTest {
        val defaultTopSites =
            listOf(
                Pair("Mozilla", "https://mozilla.com"),
                Pair("Firefox", "https://firefox.com"),
            )

        DefaultTopSitesStorage(
            pinnedSitesStorage = pinnedSitesStorage,
            historyStorage = historyStorage,
            defaultTopSites = defaultTopSites,
        )
        testScheduler.advanceUntilIdle()

        verify(pinnedSitesStorage).addAllPinnedSites(defaultTopSites, isDefault = true)
    }

    @Test
    fun addPinnedSite() = runTest {
        val defaultTopSitesStorage =
            DefaultTopSitesStorage(
                pinnedSitesStorage = pinnedSitesStorage,
                historyStorage = historyStorage,
            )

        defaultTopSitesStorage.addTopSite("Mozilla", "https://mozilla.com", isDefault = false)

        verify(pinnedSitesStorage)
            .addPinnedSite(
                "Mozilla",
                "https://mozilla.com",
                isDefault = false,
            )
    }

    @Test
    fun `GIVEN a list of top sites WHEN add top sites is invoked THEN add top sites to storage`() = runTest {
        val defaultTopSitesStorage =
            DefaultTopSitesStorage(
                pinnedSitesStorage = pinnedSitesStorage,
                historyStorage = historyStorage,
            )
        val topSites =
            listOf(
                Pair("Mozilla", "https://mozilla.com"),
                Pair("Firefox", "https://firefox.com"),
            )
        val isDefault = false

        defaultTopSitesStorage.addTopSites(topSites = topSites, isDefault = isDefault)

        verify(pinnedSitesStorage).addAllPinnedSites(topSites = topSites, isDefault = isDefault)
    }

    @Test
    fun removeTopSite() = runTest {
        val defaultTopSitesStorage =
            DefaultTopSitesStorage(
                pinnedSitesStorage = pinnedSitesStorage,
                historyStorage = historyStorage,
            )

        val frecentSite =
            TopSite.Frecent(
                id = 1,
                title = "Mozilla",
                url = "https://mozilla.com",
                createdAt = 1,
            )

        defaultTopSitesStorage.removeTopSite(frecentSite)

        verify(historyStorage).deleteVisitsFor(frecentSite.url)

        val pinnedSite =
            TopSite.Pinned(
                id = 2,
                title = "Firefox",
                url = "https://firefox.com",
                createdAt = 2,
            )

        defaultTopSitesStorage.removeTopSite(pinnedSite)

        verify(pinnedSitesStorage).removePinnedSite(pinnedSite)
        verify(historyStorage).deleteVisitsFor(pinnedSite.url)

        val defaultSite =
            TopSite.DefaultPinned(
                id = 3,
                title = "Wikipedia",
                url = "https://wikipedia.com",
                createdAt = 3,
            )

        defaultTopSitesStorage.removeTopSite(defaultSite)

        verify(pinnedSitesStorage).removePinnedSite(defaultSite)
        verify(historyStorage).deleteVisitsFor(defaultSite.url)
    }

    @Test
    fun updateTopSite() = runTest {
        val defaultTopSitesStorage =
            DefaultTopSitesStorage(
                pinnedSitesStorage = pinnedSitesStorage,
                historyStorage = historyStorage,
            )

        val defaultSite =
            TopSite.DefaultPinned(
                id = 1,
                title = "Firefox",
                url = "https://firefox.com",
                createdAt = 1,
            )

        defaultTopSitesStorage.updateTopSite(defaultSite, "Mozilla Firefox", "https://mozilla.com")

        verify(pinnedSitesStorage).updatePinnedSite(defaultSite, "Mozilla Firefox", "https://mozilla.com")

        val pinnedSite =
            TopSite.Pinned(
                id = 2,
                title = "Wikipedia",
                url = "https://wikipedia.com",
                createdAt = 2,
            )

        defaultTopSitesStorage.updateTopSite(pinnedSite, "Wiki", "https://en.wikipedia.org/wiki/Wiki")

        verify(pinnedSitesStorage).updatePinnedSite(pinnedSite, "Wiki", "https://en.wikipedia.org/wiki/Wiki")

        val frecentSite =
            TopSite.Frecent(
                id = 1,
                title = "Mozilla",
                url = "https://mozilla.com",
                createdAt = 1,
            )

        defaultTopSitesStorage.updateTopSite(frecentSite, "Moz", "")

        verify(pinnedSitesStorage, never()).updatePinnedSite(frecentSite, "Moz", "")
    }

    @Test
    fun `GIVEN frecencyConfig and providerConfig are null WHEN getTopSites is called THEN only default and pinned sites are returned`() =
        runTest {
            val defaultTopSitesStorage =
                DefaultTopSitesStorage(
                    pinnedSitesStorage = pinnedSitesStorage,
                    historyStorage = historyStorage,
                )

            val defaultSite =
                TopSite.DefaultPinned(
                    id = 1,
                    title = "Firefox",
                    url = "https://firefox.com",
                    createdAt = 1,
                )
            val pinnedSite =
                TopSite.Pinned(
                    id = 2,
                    title = "Wikipedia",
                    url = "https://wikipedia.com",
                    createdAt = 2,
                )

            whenever(pinnedSitesStorage.getPinnedSites())
                .thenReturn(
                    listOf(
                        defaultSite,
                        pinnedSite,
                    )
                )
            whenever(pinnedSitesStorage.getPinnedSitesCount()).thenReturn(2)

            var topSites = defaultTopSitesStorage.getTopSites(targetSitesCount = 0)

            assertEquals(listOf(defaultSite, pinnedSite), topSites)
            assertEquals(defaultTopSitesStorage.cachedTopSites, topSites)

            topSites = defaultTopSitesStorage.getTopSites(targetSitesCount = 1)

            assertEquals(listOf(defaultSite, pinnedSite), topSites)
            assertEquals(defaultTopSitesStorage.cachedTopSites, topSites)

            topSites = defaultTopSitesStorage.getTopSites(targetSitesCount = 2)

            assertEquals(2, topSites.size)
            assertEquals(defaultSite, topSites[0])
            assertEquals(pinnedSite, topSites[1])
            assertEquals(defaultTopSitesStorage.cachedTopSites, topSites)

            topSites = defaultTopSitesStorage.getTopSites(targetSitesCount = 5)

            assertEquals(2, topSites.size)
            assertEquals(defaultSite, topSites[0])
            assertEquals(pinnedSite, topSites[1])
            assertEquals(defaultTopSitesStorage.cachedTopSites, topSites)
        }

    @Test
    fun `GIVEN providerConfig is specified WHEN getTopSites is called THEN default, pinned and provided top sites are returned`() =
        runTest {
            val defaultTopSitesStorage =
                DefaultTopSitesStorage(
                    pinnedSitesStorage = pinnedSitesStorage,
                    historyStorage = historyStorage,
                    topSitesProvider = topSitesProvider,
                )

            val defaultSite =
                TopSite.DefaultPinned(
                    id = 1,
                    title = "Firefox",
                    url = "https://firefox.com",
                    createdAt = 1,
                )
            val pinnedSite =
                TopSite.Pinned(
                    id = 2,
                    title = "Wikipedia",
                    url = "https://wikipedia.com",
                    createdAt = 2,
                )
            val providedSite =
                TopSite.Provided(
                    id = 3,
                    title = "Mozilla",
                    url = "https://mozilla.com",
                    clickUrl = "https://mozilla.com/click",
                    imageUrl = "https://test.com/image2.jpg",
                    impressionUrl = "https://example.com",
                    createdAt = 3,
                )

            whenever(pinnedSitesStorage.getPinnedSites())
                .thenReturn(
                    listOf(
                        defaultSite,
                        pinnedSite,
                    )
                )
            whenever(topSitesProvider.getTopSites()).thenReturn(listOf(providedSite))

            var topSites = defaultTopSitesStorage.getTopSites(targetSitesCount = 0)

            assertEquals(listOf(defaultSite, pinnedSite), topSites)
            assertEquals(defaultTopSitesStorage.cachedTopSites, topSites)

            topSites =
                defaultTopSitesStorage.getTopSites(
                    targetSitesCount = 1,
                    providerConfig = TopSitesProviderConfig(showProviderTopSites = true),
                )

            assertEquals(listOf(defaultSite, pinnedSite), topSites)
            assertEquals(defaultTopSitesStorage.cachedTopSites, topSites)

            topSites =
                defaultTopSitesStorage.getTopSites(
                    targetSitesCount = 2,
                    providerConfig = TopSitesProviderConfig(showProviderTopSites = true),
                )

            assertEquals(2, topSites.size)
            assertEquals(defaultSite, topSites[0])
            assertEquals(pinnedSite, topSites[1])
            assertEquals(defaultTopSitesStorage.cachedTopSites, topSites)

            topSites =
                defaultTopSitesStorage.getTopSites(
                    targetSitesCount = 5,
                    providerConfig = TopSitesProviderConfig(showProviderTopSites = true),
                )

            assertEquals(3, topSites.size)
            assertEquals(providedSite, topSites[0])
            assertEquals(defaultSite, topSites[1])
            assertEquals(pinnedSite, topSites[2])
            assertEquals(defaultTopSitesStorage.cachedTopSites, topSites)

            topSites =
                defaultTopSitesStorage.getTopSites(
                    targetSitesCount = 5,
                    providerConfig = TopSitesProviderConfig(showProviderTopSites = false),
                )

            assertEquals(2, topSites.size)
            assertEquals(defaultSite, topSites[0])
            assertEquals(pinnedSite, topSites[1])
            assertEquals(defaultTopSitesStorage.cachedTopSites, topSites)

            topSites =
                defaultTopSitesStorage.getTopSites(
                    targetSitesCount = 5,
                    providerConfig =
                        TopSitesProviderConfig(
                            showProviderTopSites = true,
                            maxThreshold = 8,
                        ),
                )

            assertEquals(3, topSites.size)
            assertEquals(providedSite, topSites[0])
            assertEquals(defaultSite, topSites[1])
            assertEquals(pinnedSite, topSites[2])
            assertEquals(defaultTopSitesStorage.cachedTopSites, topSites)

            topSites =
                defaultTopSitesStorage.getTopSites(
                    targetSitesCount = 5,
                    providerConfig =
                        TopSitesProviderConfig(
                            showProviderTopSites = true,
                            maxThreshold = 2,
                        ),
                )

            assertEquals(2, topSites.size)
            assertEquals(defaultSite, topSites[0])
            assertEquals(pinnedSite, topSites[1])
            assertEquals(defaultTopSitesStorage.cachedTopSites, topSites)
        }

    @Test
    fun `GIVEN providerConfig with maxThreshold is specified WHEN getTopSites is called THEN the correct number of provided top sites are returned`() =
        runTest {
            val defaultTopSitesStorage =
                DefaultTopSitesStorage(
                    pinnedSitesStorage = pinnedSitesStorage,
                    historyStorage = historyStorage,
                    topSitesProvider = topSitesProvider,
                )

            val defaultSite =
                TopSite.DefaultPinned(
                    id = 1,
                    title = "Firefox",
                    url = "https://firefox.com",
                    createdAt = 1,
                )
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
            val providedSite1 =
                TopSite.Provided(
                    id = 4,
                    title = "Mozilla",
                    url = "https://mozilla.com",
                    clickUrl = "https://mozilla.com/click",
                    imageUrl = "https://test.com/image2.jpg",
                    impressionUrl = "https://example.com",
                    createdAt = 3,
                )
            val providedSite2 =
                TopSite.Provided(
                    id = 5,
                    title = "Pocket",
                    url = "https://pocket.com",
                    clickUrl = "https://mozilla.com/click",
                    imageUrl = "https://test.com/image2.jpg",
                    impressionUrl = "https://example.com",
                    createdAt = 3,
                )

            whenever(pinnedSitesStorage.getPinnedSites())
                .thenReturn(
                    listOf(
                        defaultSite,
                        pinnedSite1,
                        pinnedSite2,
                        defaultSite,
                        pinnedSite1,
                        pinnedSite2,
                    )
                )
            whenever(topSitesProvider.getTopSites()).thenReturn(listOf(providedSite1, providedSite2))

            var topSites =
                defaultTopSitesStorage.getTopSites(
                    targetSitesCount = 8,
                    providerConfig =
                        TopSitesProviderConfig(
                            showProviderTopSites = true,
                            maxThreshold = 8,
                        ),
                )

            assertEquals(8, topSites.size)
            assertEquals(providedSite1, topSites[0])
            assertEquals(providedSite2, topSites[1])
            assertEquals(defaultSite, topSites[2])
            assertEquals(pinnedSite1, topSites[3])
            assertEquals(pinnedSite2, topSites[4])
            assertEquals(defaultSite, topSites[5])
            assertEquals(pinnedSite1, topSites[6])
            assertEquals(pinnedSite2, topSites[7])
            assertEquals(defaultTopSitesStorage.cachedTopSites, topSites)

            whenever(pinnedSitesStorage.getPinnedSites())
                .thenReturn(
                    listOf(
                        defaultSite,
                        pinnedSite1,
                        pinnedSite2,
                        defaultSite,
                        pinnedSite1,
                        pinnedSite2,
                        defaultSite,
                    )
                )

            topSites =
                defaultTopSitesStorage.getTopSites(
                    targetSitesCount = 8,
                    providerConfig =
                        TopSitesProviderConfig(
                            showProviderTopSites = true,
                            maxThreshold = 8,
                        ),
                )

            assertEquals(8, topSites.size)
            assertEquals(providedSite1, topSites[0])
            assertEquals(defaultSite, topSites[1])
            assertEquals(pinnedSite1, topSites[2])
            assertEquals(pinnedSite2, topSites[3])
            assertEquals(defaultSite, topSites[4])
            assertEquals(pinnedSite1, topSites[5])
            assertEquals(pinnedSite2, topSites[6])
            assertEquals(defaultSite, topSites[7])
            assertEquals(defaultTopSitesStorage.cachedTopSites, topSites)

            whenever(pinnedSitesStorage.getPinnedSites())
                .thenReturn(
                    listOf(
                        defaultSite,
                        pinnedSite1,
                        pinnedSite2,
                        defaultSite,
                        pinnedSite1,
                        pinnedSite2,
                        defaultSite,
                        pinnedSite1,
                    )
                )

            topSites =
                defaultTopSitesStorage.getTopSites(
                    targetSitesCount = 8,
                    providerConfig =
                        TopSitesProviderConfig(
                            showProviderTopSites = true,
                            maxThreshold = 8,
                        ),
                )

            assertEquals(8, topSites.size)
            assertEquals(defaultSite, topSites[0])
            assertEquals(pinnedSite1, topSites[1])
            assertEquals(pinnedSite2, topSites[2])
            assertEquals(defaultSite, topSites[3])
            assertEquals(pinnedSite1, topSites[4])
            assertEquals(pinnedSite2, topSites[5])
            assertEquals(defaultSite, topSites[6])
            assertEquals(pinnedSite1, topSites[7])
            assertEquals(defaultTopSitesStorage.cachedTopSites, topSites)
        }

    @Test
    fun `GIVEN providerConfig with maxThreshold and limit specified WHEN getTopSites is called THEN the correct number of provided top sites are returned`() =
        runTest {
            val defaultTopSitesStorage =
                DefaultTopSitesStorage(
                    pinnedSitesStorage = pinnedSitesStorage,
                    historyStorage = historyStorage,
                    topSitesProvider = topSitesProvider,
                )

            val defaultSite =
                TopSite.DefaultPinned(
                    id = 1,
                    title = "Firefox",
                    url = "https://firefox.com",
                    createdAt = 1,
                )
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
            val providedSite1 =
                TopSite.Provided(
                    id = 4,
                    title = "Mozilla",
                    url = "https://mozilla.com",
                    clickUrl = "https://mozilla.com/click",
                    imageUrl = "https://test.com/image2.jpg",
                    impressionUrl = "https://example.com",
                    createdAt = 3,
                )
            val providedSite2 =
                TopSite.Provided(
                    id = 5,
                    title = "Pocket",
                    url = "https://pocket.com",
                    clickUrl = "https://mozilla.com/click",
                    imageUrl = "https://test.com/image2.jpg",
                    impressionUrl = "https://example.com",
                    createdAt = 3,
                )

            val pinnedSites =
                listOf(
                    defaultSite,
                    pinnedSite1,
                    pinnedSite2,
                    defaultSite,
                    pinnedSite1,
                    pinnedSite2,
                )
            val providedSites = listOf(providedSite1, providedSite2)
            whenever(pinnedSitesStorage.getPinnedSites()).thenReturn(pinnedSites)
            whenever(topSitesProvider.getTopSites()).thenReturn(providedSites)

            var topSites =
                defaultTopSitesStorage.getTopSites(
                    targetSitesCount = 8,
                    providerConfig =
                        TopSitesProviderConfig(
                            showProviderTopSites = true,
                            limit = 2,
                            maxThreshold = 8,
                        ),
                )

            assertEquals(8, topSites.size)
            assertEquals(providedSite1, topSites[0])
            assertEquals(providedSite2, topSites[1])
            assertEquals(defaultSite, topSites[2])
            assertEquals(pinnedSite1, topSites[3])
            assertEquals(pinnedSite2, topSites[4])
            assertEquals(defaultSite, topSites[5])
            assertEquals(pinnedSite1, topSites[6])
            assertEquals(pinnedSite2, topSites[7])
            assertEquals(defaultTopSitesStorage.cachedTopSites, topSites)

            topSites =
                defaultTopSitesStorage.getTopSites(
                    targetSitesCount = 8,
                    providerConfig =
                        TopSitesProviderConfig(
                            showProviderTopSites = true,
                            limit = 1,
                            maxThreshold = 8,
                        ),
                )

            assertEquals(7, topSites.size)
            assertEquals(providedSite1, topSites[0])
            assertEquals(defaultSite, topSites[1])
            assertEquals(pinnedSite1, topSites[2])
            assertEquals(pinnedSite2, topSites[3])
            assertEquals(defaultSite, topSites[4])
            assertEquals(pinnedSite1, topSites[5])
            assertEquals(pinnedSite2, topSites[6])
            assertEquals(defaultTopSitesStorage.cachedTopSites, topSites)

            whenever(pinnedSitesStorage.getPinnedSites()).thenReturn(pinnedSites + defaultSite)
            topSites =
                defaultTopSitesStorage.getTopSites(
                    targetSitesCount = 8,
                    providerConfig =
                        TopSitesProviderConfig(
                            showProviderTopSites = true,
                            limit = 2,
                            maxThreshold = 8,
                        ),
                )

            assertEquals(8, topSites.size)
            assertEquals(providedSite1, topSites[0])
            assertEquals(defaultSite, topSites[1])
            assertEquals(pinnedSite1, topSites[2])
            assertEquals(pinnedSite2, topSites[3])
            assertEquals(defaultSite, topSites[4])
            assertEquals(pinnedSite1, topSites[5])
            assertEquals(pinnedSite2, topSites[6])
            assertEquals(defaultSite, topSites[7])
            assertEquals(defaultTopSitesStorage.cachedTopSites, topSites)

            whenever(pinnedSitesStorage.getPinnedSites()).thenReturn(pinnedSites + defaultSite + pinnedSite1)
            topSites =
                defaultTopSitesStorage.getTopSites(
                    targetSitesCount = 8,
                    providerConfig =
                        TopSitesProviderConfig(
                            showProviderTopSites = true,
                            limit = 2,
                            maxThreshold = 8,
                        ),
                )

            assertEquals(8, topSites.size)
            assertEquals(defaultSite, topSites[0])
            assertEquals(pinnedSite1, topSites[1])
            assertEquals(pinnedSite2, topSites[2])
            assertEquals(defaultSite, topSites[3])
            assertEquals(pinnedSite1, topSites[4])
            assertEquals(pinnedSite2, topSites[5])
            assertEquals(defaultSite, topSites[6])
            assertEquals(pinnedSite1, topSites[7])
            assertEquals(defaultTopSitesStorage.cachedTopSites, topSites)
        }

    @Test
    fun `GIVEN frecencyConfig and providerConfig are specified WHEN getTopSites is called THEN default, pinned, provided and frecent top sites are returned`() =
        runTest {
            val defaultTopSitesStorage =
                DefaultTopSitesStorage(
                    pinnedSitesStorage = pinnedSitesStorage,
                    historyStorage = historyStorage,
                    topSitesProvider = topSitesProvider,
                )

            val defaultSite =
                TopSite.DefaultPinned(
                    id = 1,
                    title = "Firefox",
                    url = "https://firefox.com",
                    createdAt = 1,
                )
            val pinnedSite =
                TopSite.Pinned(
                    id = 2,
                    title = "Wikipedia",
                    url = "https://wikipedia.com",
                    createdAt = 2,
                )
            val providedSite =
                TopSite.Provided(
                    id = 3,
                    title = "Mozilla",
                    url = "https://mozilla.com",
                    clickUrl = "https://mozilla.com/click",
                    imageUrl = "https://test.com/image2.jpg",
                    impressionUrl = "https://example.com",
                    createdAt = 3,
                )

            whenever(pinnedSitesStorage.getPinnedSites())
                .thenReturn(
                    listOf(
                        defaultSite,
                        pinnedSite,
                    )
                )
            whenever(topSitesProvider.getTopSites()).thenReturn(listOf(providedSite))

            val frecentSite1 = TopFrecentSiteInfo("https://getpocket.com", "Pocket")
            whenever(historyStorage.getTopFrecentSites(anyInt(), any())).thenReturn(listOf(frecentSite1))

            var topSites =
                defaultTopSitesStorage.getTopSites(
                    targetSitesCount = 0,
                    frecencyConfig = TopSitesFrecencyConfig(frecencyTresholdOption = FrecencyThresholdOption.NONE),
                    providerConfig = TopSitesProviderConfig(showProviderTopSites = true),
                )

            assertEquals(listOf(defaultSite, pinnedSite), topSites)

            topSites =
                defaultTopSitesStorage.getTopSites(
                    targetSitesCount = 1,
                    frecencyConfig = TopSitesFrecencyConfig(frecencyTresholdOption = FrecencyThresholdOption.NONE),
                    providerConfig = TopSitesProviderConfig(showProviderTopSites = true),
                )

            assertEquals(listOf(defaultSite, pinnedSite), topSites)
            assertEquals(defaultTopSitesStorage.cachedTopSites, topSites)

            topSites =
                defaultTopSitesStorage.getTopSites(
                    targetSitesCount = 2,
                    frecencyConfig = TopSitesFrecencyConfig(frecencyTresholdOption = FrecencyThresholdOption.NONE),
                    providerConfig = TopSitesProviderConfig(showProviderTopSites = true),
                )

            assertEquals(2, topSites.size)
            assertEquals(defaultSite, topSites[0])
            assertEquals(pinnedSite, topSites[1])
            assertEquals(defaultTopSitesStorage.cachedTopSites, topSites)

            topSites =
                defaultTopSitesStorage.getTopSites(
                    targetSitesCount = 3,
                    frecencyConfig = TopSitesFrecencyConfig(frecencyTresholdOption = FrecencyThresholdOption.NONE),
                    providerConfig = TopSitesProviderConfig(showProviderTopSites = true),
                )

            assertEquals(3, topSites.size)
            assertEquals(providedSite, topSites[0])
            assertEquals(defaultSite, topSites[1])
            assertEquals(pinnedSite, topSites[2])
            assertEquals(defaultTopSitesStorage.cachedTopSites, topSites)

            topSites =
                defaultTopSitesStorage.getTopSites(
                    targetSitesCount = 5,
                    frecencyConfig = TopSitesFrecencyConfig(frecencyTresholdOption = FrecencyThresholdOption.NONE),
                    providerConfig = TopSitesProviderConfig(showProviderTopSites = true),
                )

            assertEquals(4, topSites.size)
            assertEquals(providedSite, topSites[0])
            assertEquals(defaultSite, topSites[1])
            assertEquals(pinnedSite, topSites[2])
            assertEquals(frecentSite1.toTopSite(), topSites[3])
            assertEquals(defaultTopSitesStorage.cachedTopSites, topSites)
        }

    @Test
    fun `getTopSites returns pinned and frecent sites when frecencyConfig is specified`() = runTest {
        val defaultTopSitesStorage =
            DefaultTopSitesStorage(
                pinnedSitesStorage = pinnedSitesStorage,
                historyStorage = historyStorage,
            )

        val defaultSite =
            TopSite.DefaultPinned(
                id = 1,
                title = "Firefox",
                url = "https://firefox.com",
                createdAt = 1,
            )
        val pinnedSite =
            TopSite.Pinned(
                id = 2,
                title = "Wikipedia",
                url = "https://wikipedia.com",
                createdAt = 2,
            )

        whenever(pinnedSitesStorage.getPinnedSites())
            .thenReturn(
                listOf(
                    defaultSite,
                    pinnedSite,
                )
            )
        whenever(pinnedSitesStorage.getPinnedSitesCount()).thenReturn(2)

        val frecentSite1 = TopFrecentSiteInfo("https://mozilla.com", "Mozilla")
        whenever(historyStorage.getTopFrecentSites(anyInt(), any())).thenReturn(listOf(frecentSite1))

        var topSites =
            defaultTopSitesStorage.getTopSites(
                targetSitesCount = 0,
                frecencyConfig = TopSitesFrecencyConfig(frecencyTresholdOption = FrecencyThresholdOption.NONE),
            )

        assertEquals(listOf(defaultSite, pinnedSite), topSites)

        topSites =
            defaultTopSitesStorage.getTopSites(
                targetSitesCount = 1,
                frecencyConfig = TopSitesFrecencyConfig(frecencyTresholdOption = FrecencyThresholdOption.NONE),
            )

        assertEquals(listOf(defaultSite, pinnedSite), topSites)
        assertEquals(defaultTopSitesStorage.cachedTopSites, topSites)

        topSites =
            defaultTopSitesStorage.getTopSites(
                targetSitesCount = 2,
                frecencyConfig = TopSitesFrecencyConfig(frecencyTresholdOption = FrecencyThresholdOption.NONE),
            )

        assertEquals(2, topSites.size)
        assertEquals(defaultSite, topSites[0])
        assertEquals(pinnedSite, topSites[1])
        assertEquals(defaultTopSitesStorage.cachedTopSites, topSites)

        topSites =
            defaultTopSitesStorage.getTopSites(
                targetSitesCount = 5,
                frecencyConfig = TopSitesFrecencyConfig(frecencyTresholdOption = FrecencyThresholdOption.NONE),
            )

        assertEquals(3, topSites.size)
        assertEquals(defaultSite, topSites[0])
        assertEquals(pinnedSite, topSites[1])
        assertEquals(frecentSite1.toTopSite(), topSites[2])
        assertEquals(defaultTopSitesStorage.cachedTopSites, topSites)

        val frecentSite2 = TopFrecentSiteInfo("https://example.com", "Example")
        val frecentSite3 = TopFrecentSiteInfo("https://getpocket.com", "Pocket")
        whenever(historyStorage.getTopFrecentSites(anyInt(), any()))
            .thenReturn(
                listOf(
                    frecentSite1,
                    frecentSite2,
                    frecentSite3,
                )
            )

        topSites =
            defaultTopSitesStorage.getTopSites(
                targetSitesCount = 5,
                frecencyConfig = TopSitesFrecencyConfig(frecencyTresholdOption = FrecencyThresholdOption.NONE),
            )

        assertEquals(5, topSites.size)
        assertEquals(defaultSite, topSites[0])
        assertEquals(pinnedSite, topSites[1])
        assertEquals(frecentSite1.toTopSite(), topSites[2])
        assertEquals(frecentSite2.toTopSite(), topSites[3])
        assertEquals(frecentSite3.toTopSite(), topSites[4])
        assertEquals(defaultTopSitesStorage.cachedTopSites, topSites)

        val frecentSite4 = TopFrecentSiteInfo("https://example2.com", "Example2")
        whenever(historyStorage.getTopFrecentSites(anyInt(), any()))
            .thenReturn(
                listOf(
                    frecentSite1,
                    frecentSite2,
                    frecentSite3,
                    frecentSite4,
                )
            )

        topSites =
            defaultTopSitesStorage.getTopSites(
                targetSitesCount = 5,
                frecencyConfig = TopSitesFrecencyConfig(frecencyTresholdOption = FrecencyThresholdOption.NONE),
            )

        assertEquals(5, topSites.size)
        assertEquals(defaultSite, topSites[0])
        assertEquals(pinnedSite, topSites[1])
        assertEquals(frecentSite1.toTopSite(), topSites[2])
        assertEquals(frecentSite2.toTopSite(), topSites[3])
        assertEquals(frecentSite3.toTopSite(), topSites[4])
        assertEquals(defaultTopSitesStorage.cachedTopSites, topSites)
    }

    @Test
    fun `getTopSites filters out frecent sites that already exist in pinned sites`() = runTest {
        val defaultTopSitesStorage =
            DefaultTopSitesStorage(
                pinnedSitesStorage = pinnedSitesStorage,
                historyStorage = historyStorage,
            )

        val defaultSiteFirefox =
            TopSite.DefaultPinned(
                id = 1,
                title = "Firefox",
                url = "https://firefox.com",
                createdAt = 1,
            )
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
                    defaultSiteFirefox,
                    pinnedSite1,
                    pinnedSite2,
                )
            )
        whenever(pinnedSitesStorage.getPinnedSitesCount()).thenReturn(3)

        val frecentSiteWithNoTitle = TopFrecentSiteInfo("https://mozilla.com", "")
        val frecentSiteFirefox = TopFrecentSiteInfo("https://firefox.com", "Firefox")
        val frecentSite1 = TopFrecentSiteInfo("https://getpocket.com", "Pocket")
        val frecentSite2 = TopFrecentSiteInfo("https://www.example.com", "Example")

        whenever(historyStorage.getTopFrecentSites(anyInt(), any()))
            .thenReturn(
                listOf(
                    frecentSiteWithNoTitle,
                    frecentSiteFirefox,
                    frecentSite1,
                    frecentSite2,
                )
            )

        val topSites =
            defaultTopSitesStorage.getTopSites(
                targetSitesCount = 5,
                frecencyConfig = TopSitesFrecencyConfig(frecencyTresholdOption = FrecencyThresholdOption.NONE),
            )

        verify(historyStorage).getTopFrecentSites(5, frecencyThreshold = FrecencyThresholdOption.NONE)

        assertEquals(5, topSites.size)
        assertEquals(defaultSiteFirefox, topSites[0])
        assertEquals(pinnedSite1, topSites[1])
        assertEquals(pinnedSite2, topSites[2])
        assertEquals(frecentSiteWithNoTitle.toTopSite(), topSites[3])
        assertEquals(frecentSite1.toTopSite(), topSites[4])
        assertEquals("mozilla.com", frecentSiteWithNoTitle.toTopSite().title)
        assertEquals(defaultTopSitesStorage.cachedTopSites, topSites)
    }

    @Test
    fun `GIVEN providerFilter is set WHEN getTopSites is called THEN the provided top sites are filtered`() = runTest {
        val defaultTopSitesStorage =
            DefaultTopSitesStorage(
                pinnedSitesStorage = pinnedSitesStorage,
                historyStorage = historyStorage,
                topSitesProvider = topSitesProvider,
            )

        val filteredUrl = "https://test.com"

        val providerConfig =
            TopSitesProviderConfig(
                showProviderTopSites = true,
                providerFilter = { topSite -> topSite.url != filteredUrl },
            )

        val defaultSite =
            TopSite.DefaultPinned(
                id = 1,
                title = "Firefox",
                url = "https://firefox.com",
                createdAt = 1,
            )
        val pinnedSite =
            TopSite.Pinned(
                id = 2,
                title = "Test",
                url = filteredUrl,
                createdAt = 2,
            )
        val providedSite =
            TopSite.Provided(
                id = 3,
                title = "Mozilla",
                url = "https://mozilla.com",
                clickUrl = "https://mozilla.com/click",
                imageUrl = "https://test.com/image2.jpg",
                impressionUrl = "https://example.com",
                createdAt = 3,
            )
        val providedFilteredSite =
            TopSite.Provided(
                id = 3,
                title = "Filtered",
                url = filteredUrl,
                clickUrl = "https://test.com/click",
                imageUrl = "https://test.com/image2.jpg",
                impressionUrl = "https://example.com",
                createdAt = 3,
            )

        whenever(pinnedSitesStorage.getPinnedSites())
            .thenReturn(
                listOf(
                    defaultSite,
                    pinnedSite,
                )
            )
        whenever(topSitesProvider.getTopSites()).thenReturn(listOf(providedSite, providedFilteredSite))

        val frecentSite1 = TopFrecentSiteInfo("https://getpocket.com", "Pocket")
        whenever(historyStorage.getTopFrecentSites(anyInt(), any())).thenReturn(listOf(frecentSite1))

        var topSites =
            defaultTopSitesStorage.getTopSites(
                targetSitesCount = 3,
                frecencyConfig = TopSitesFrecencyConfig(frecencyTresholdOption = FrecencyThresholdOption.NONE),
                providerConfig = providerConfig,
            )

        assertEquals(3, topSites.size)
        assertEquals(providedSite, topSites[0])
        assertEquals(defaultSite, topSites[1])
        assertEquals(pinnedSite, topSites[2])
        assertEquals(defaultTopSitesStorage.cachedTopSites, topSites)

        topSites =
            defaultTopSitesStorage.getTopSites(
                targetSitesCount = 4,
                frecencyConfig = TopSitesFrecencyConfig(frecencyTresholdOption = FrecencyThresholdOption.NONE),
                providerConfig = providerConfig,
            )

        assertEquals(4, topSites.size)
        assertEquals(providedSite, topSites[0])
        assertEquals(defaultSite, topSites[1])
        assertEquals(pinnedSite, topSites[2])
        assertEquals(frecentSite1.toTopSite(), topSites[3])
        assertEquals(defaultTopSitesStorage.cachedTopSites, topSites)
    }

    @Test
    fun `GIVEN frecent top sites exist as a pinned or provided site WHEN top sites are retrieved THEN filters out frecent sites that already exist in pinned or provided sites`() =
        runTest {
            val defaultTopSitesStorage =
                DefaultTopSitesStorage(
                    pinnedSitesStorage = pinnedSitesStorage,
                    historyStorage = historyStorage,
                    topSitesProvider = topSitesProvider,
                )

            val defaultSiteFirefox =
                TopSite.DefaultPinned(
                    id = 1,
                    title = "Firefox",
                    url = "https://firefox.com",
                    createdAt = 1,
                )
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
            val providedSite =
                TopSite.Provided(
                    id = 3,
                    title = "Firefox",
                    url = "https://getfirefox.com",
                    clickUrl = "https://getfirefox.com/click",
                    imageUrl = "https://test.com/image2.jpg",
                    impressionUrl = "https://example.com",
                    createdAt = 3,
                )

            whenever(pinnedSitesStorage.getPinnedSites())
                .thenReturn(
                    listOf(
                        defaultSiteFirefox,
                        pinnedSite1,
                        pinnedSite2,
                    )
                )
            whenever(pinnedSitesStorage.getPinnedSitesCount()).thenReturn(3)
            whenever(topSitesProvider.getTopSites()).thenReturn(listOf(providedSite))

            val frecentSiteWithNoTitle = TopFrecentSiteInfo("https://mozilla.com", "")
            val frecentSiteFirefox = TopFrecentSiteInfo("https://firefox.com", "Firefox")
            val frecentSite1 = TopFrecentSiteInfo("https://getpocket.com", "Pocket")
            val frecentSite2 = TopFrecentSiteInfo("https://www.example.com", "Example")
            val frecentSite3 = TopFrecentSiteInfo("https://www.getfirefox.com", "Firefox")

            whenever(historyStorage.getTopFrecentSites(anyInt(), any()))
                .thenReturn(
                    listOf(
                        frecentSiteWithNoTitle,
                        frecentSiteFirefox,
                        frecentSite1,
                        frecentSite2,
                        frecentSite3,
                    )
                )

            val topSites =
                defaultTopSitesStorage.getTopSites(
                    targetSitesCount = 10,
                    frecencyConfig = TopSitesFrecencyConfig(frecencyTresholdOption = FrecencyThresholdOption.NONE),
                    providerConfig = TopSitesProviderConfig(showProviderTopSites = true),
                )

            verify(historyStorage).getTopFrecentSites(10, frecencyThreshold = FrecencyThresholdOption.NONE)

            assertEquals(6, topSites.size)
            assertEquals(providedSite, topSites[0])
            assertEquals(defaultSiteFirefox, topSites[1])
            assertEquals(pinnedSite1, topSites[2])
            assertEquals(pinnedSite2, topSites[3])
            assertEquals(frecentSiteWithNoTitle.toTopSite(), topSites[4])
            assertEquals(frecentSite1.toTopSite(), topSites[5])
            assertEquals("mozilla.com", frecentSiteWithNoTitle.toTopSite().title)
            assertEquals(defaultTopSitesStorage.cachedTopSites, topSites)
        }

    @Test
    fun `GIVEN frecencyFilter is set WHEN getTopSites is called THEN the frecent top sites are filtered`() = runTest {
        val defaultTopSitesStorage =
            DefaultTopSitesStorage(
                pinnedSitesStorage = pinnedSitesStorage,
                historyStorage = historyStorage,
                topSitesProvider = topSitesProvider,
            )

        val filterMethod: ((TopSite) -> Boolean) = { topSite ->
            val uri = topSite.url.toUri()
            if (!uri.queryParameterNames.contains("key")) {
                true
            } else {
                uri.getQueryParameter("key") != "value"
            }
        }

        val filteredUrl = "https://test.com/?key=value"

        val frecencyConfig =
            TopSitesFrecencyConfig(
                frecencyTresholdOption = FrecencyThresholdOption.NONE,
                frecencyFilter = filterMethod,
            )

        val defaultSite =
            TopSite.DefaultPinned(
                id = 1,
                title = "Firefox",
                url = "https://firefox.com",
                createdAt = 1,
            )
        val pinnedSite =
            TopSite.Pinned(
                id = 2,
                title = "Test",
                url = "https://test.com",
                createdAt = 2,
            )

        whenever(pinnedSitesStorage.getPinnedSites())
            .thenReturn(
                listOf(
                    defaultSite,
                    pinnedSite,
                )
            )

        val providedFilteredSite =
            TopSite.Provided(
                id = 3,
                title = "Filtered",
                url = "https://test.com",
                clickUrl = "https://test.com/click",
                imageUrl = "https://test.com/image2.jpg",
                impressionUrl = "https://example.com",
                createdAt = 3,
            )

        whenever(topSitesProvider.getTopSites()).thenReturn(listOf(providedFilteredSite))

        val frecentSite = TopFrecentSiteInfo("https://getpocket.com", "Pocket")

        val frecentFilteredSite = TopFrecentSiteInfo(filteredUrl, "testSearch")

        whenever(historyStorage.getTopFrecentSites(anyInt(), any()))
            .thenReturn(
                listOf(
                    frecentSite,
                    frecentFilteredSite,
                )
            )

        var topSites =
            defaultTopSitesStorage.getTopSites(
                targetSitesCount = 4,
                frecencyConfig = frecencyConfig,
                providerConfig = TopSitesProviderConfig(showProviderTopSites = true),
            )

        assertEquals(4, topSites.size)
        assertTrue(topSites.contains(frecentSite.toTopSite()))
        assertTrue(topSites.contains(providedFilteredSite))
        assertTrue(topSites.contains(defaultSite))
        assertTrue(topSites.contains(pinnedSite))
        assertEquals(defaultTopSitesStorage.cachedTopSites, topSites)

        topSites =
            defaultTopSitesStorage.getTopSites(
                targetSitesCount = 5,
                frecencyConfig = frecencyConfig,
                providerConfig = TopSitesProviderConfig(showProviderTopSites = true),
            )

        assertEquals(4, topSites.size)
        assertTrue(topSites.contains(frecentSite.toTopSite()))
        assertTrue(topSites.contains(providedFilteredSite))
        assertTrue(topSites.contains(defaultSite))
        assertTrue(topSites.contains(pinnedSite))
        assertEquals(defaultTopSitesStorage.cachedTopSites, topSites)
    }

    @Test
    fun `GIVEN frecent top sites host exist as a provided site WHEN top sites are retrieved THEN filters out frecent sites with host that already exist in provided sites`() =
        runTest {
            val defaultTopSitesStorage =
                DefaultTopSitesStorage(
                    pinnedSitesStorage = pinnedSitesStorage,
                    historyStorage = historyStorage,
                    topSitesProvider = topSitesProvider,
                )

            val defaultSiteFirefox =
                TopSite.DefaultPinned(
                    id = 1,
                    title = "Firefox",
                    url = "https://firefox.com",
                    createdAt = 1,
                )
            val pinnedSite1 =
                TopSite.Pinned(
                    id = 2,
                    title = "Google",
                    url = "https://google.com",
                    createdAt = 2,
                )
            val providedSite1 =
                TopSite.Provided(
                    id = 3,
                    title = "Amazon",
                    url = "https://www.amazon.com/?tag=sponsored-shortcut",
                    clickUrl = "https://www.amazon.com/click",
                    imageUrl = "https://test.com/image2.jpg",
                    impressionUrl = "https://example.com",
                    createdAt = 3,
                )
            val providedSite2 =
                TopSite.Provided(
                    id = 4,
                    title = "UnderArmour",
                    url = "https://www.underarmour.com/?tag=sponsored-shortcut",
                    clickUrl = "https://www.underarmour.com/click",
                    imageUrl = "https://test.com/image2.jpg",
                    impressionUrl = "https://example.com",
                    createdAt = 4,
                )

            whenever(pinnedSitesStorage.getPinnedSites())
                .thenReturn(
                    listOf(
                        defaultSiteFirefox,
                        pinnedSite1,
                    )
                )
            whenever(pinnedSitesStorage.getPinnedSitesCount()).thenReturn(2)
            whenever(topSitesProvider.getTopSites())
                .thenReturn(
                    listOf(
                        providedSite1,
                        providedSite2,
                    )
                )

            val frecentSite1 = TopFrecentSiteInfo("https://www.amazon.com", "Amazon")
            val frecentSite2 =
                TopFrecentSiteInfo(
                    "https://www.amazon.com/Wireless-Charging-Station-Charger-AirPods/dp/B09KTY5GM7?pf_rd_r=NCJV8SPRQ2K43XM6WWKS&pf_rd_p=7b590888-dba4-4742-b2f2-7b20b1700e00&pd_rd_r=4fbaf1df-96be-470a-9811-0bc2aa8b415f&pd_rd_w=Viqqz&pd_rd_wg=9Emfa",
                    "Amazon",
                )
            val frecentSite3 = TopFrecentSiteInfo("https://www.underarmour.com", "UnderArmour")
            val frecentSite4 =
                TopFrecentSiteInfo(
                    "https://www.underarmour.com/en-us/p/curry_brand_shoes_and_gear/mens_curry_sour_then_sweet_crewneck/195253758836.html",
                    "UnderArmour",
                )
            val frecentSite5 = TopFrecentSiteInfo("https://www.example.com", "Example")
            val frecentSite6 = TopFrecentSiteInfo("https://www.getfirefox.com", "Firefox")

            whenever(historyStorage.getTopFrecentSites(anyInt(), any()))
                .thenReturn(
                    listOf(
                        frecentSite1,
                        frecentSite2,
                        frecentSite3,
                        frecentSite4,
                        frecentSite5,
                        frecentSite6,
                    )
                )

            val topSites =
                defaultTopSitesStorage.getTopSites(
                    targetSitesCount = 10,
                    frecencyConfig = TopSitesFrecencyConfig(frecencyTresholdOption = FrecencyThresholdOption.NONE),
                    providerConfig = TopSitesProviderConfig(showProviderTopSites = true),
                )

            verify(historyStorage).getTopFrecentSites(10, frecencyThreshold = FrecencyThresholdOption.NONE)

            assertEquals(6, topSites.size)
            assertEquals(providedSite1, topSites[0])
            assertEquals(providedSite2, topSites[1])
            assertFalse(topSites.contains(frecentSite1.toTopSite()))
            assertFalse(topSites.contains(frecentSite2.toTopSite()))
            assertFalse(topSites.contains(frecentSite3.toTopSite()))
            assertFalse(topSites.contains(frecentSite4.toTopSite()))
            assertEquals(defaultSiteFirefox, topSites[2])
            assertEquals(pinnedSite1, topSites[3])
            assertEquals(defaultTopSitesStorage.cachedTopSites, topSites)
        }

    @Test
    fun `GIVEN a default top sites provider WHEN getTopSites is called THEN the default top sites fill the remaining slots`() =
        runTest {
            val defaultTopSitesStorage =
                DefaultTopSitesStorage(
                    pinnedSitesStorage = pinnedSitesStorage,
                    historyStorage = historyStorage,
                    defaultTopSitesProvider = defaultTopSitesProvider,
                )

            whenever(pinnedSitesStorage.getPinnedSites()).thenReturn(listOf(pinnedSite))
            whenever(historyStorage.getTopFrecentSites(anyInt(), any())).thenReturn(listOf(frecentSite))
            whenever(defaultTopSitesProvider.getDefaultTopSites())
                .thenReturn(listOf(defaultSiteWikipedia, defaultSiteFirefox))

            val topSites =
                defaultTopSitesStorage.getTopSites(
                    targetSitesCount = 5,
                    frecencyConfig = TopSitesFrecencyConfig(frecencyTresholdOption = FrecencyThresholdOption.NONE),
                )

            assertEquals(
                listOf(pinnedSite, frecentSite.toTopSite(), defaultSiteWikipedia, defaultSiteFirefox),
                topSites,
            )
        }

    @Test
    fun `GIVEN a default top site is already pinned WHEN getTopSites is called THEN the default top site is skipped`() =
        runTest {
            val defaultTopSitesStorage =
                DefaultTopSitesStorage(
                    pinnedSitesStorage = pinnedSitesStorage,
                    historyStorage = historyStorage,
                    defaultTopSitesProvider = defaultTopSitesProvider,
                )

            val pinnedSite = TopSite.Pinned(id = 3, title = "Wikipedia", url = WIKIPEDIA_URL, createdAt = 3)

            whenever(pinnedSitesStorage.getPinnedSites()).thenReturn(listOf(pinnedSite))
            whenever(historyStorage.getTopFrecentSites(anyInt(), any())).thenReturn(emptyList())
            whenever(defaultTopSitesProvider.getDefaultTopSites())
                .thenReturn(listOf(defaultSiteWikipedia, defaultSiteFirefox))

            val topSites =
                defaultTopSitesStorage.getTopSites(
                    targetSitesCount = 5,
                    frecencyConfig = TopSitesFrecencyConfig(frecencyTresholdOption = FrecencyThresholdOption.NONE),
                )

            assertEquals(listOf(pinnedSite, defaultSiteFirefox), topSites)
        }

    @Test
    fun `GIVEN a default top site is already a frecent site WHEN getTopSites is called THEN the default top site is skipped`() =
        runTest {
            val defaultTopSitesStorage =
                DefaultTopSitesStorage(
                    pinnedSitesStorage = pinnedSitesStorage,
                    historyStorage = historyStorage,
                    defaultTopSitesProvider = defaultTopSitesProvider,
                )

            val frecentSite = TopFrecentSiteInfo(WIKIPEDIA_URL, "Wikipedia")

            whenever(pinnedSitesStorage.getPinnedSites()).thenReturn(emptyList())
            whenever(historyStorage.getTopFrecentSites(anyInt(), any())).thenReturn(listOf(frecentSite))
            whenever(defaultTopSitesProvider.getDefaultTopSites())
                .thenReturn(listOf(defaultSiteWikipedia, defaultSiteFirefox))

            val topSites =
                defaultTopSitesStorage.getTopSites(
                    targetSitesCount = 5,
                    frecencyConfig = TopSitesFrecencyConfig(frecencyTresholdOption = FrecencyThresholdOption.NONE),
                )

            assertEquals(listOf(frecentSite.toTopSite(), defaultSiteFirefox), topSites)
        }

    @Test
    fun `GIVEN there are no slots left WHEN getTopSites is called THEN the default top sites provider is not used`() =
        runTest {
            val defaultTopSitesStorage =
                DefaultTopSitesStorage(
                    pinnedSitesStorage = pinnedSitesStorage,
                    historyStorage = historyStorage,
                    defaultTopSitesProvider = defaultTopSitesProvider,
                )

            whenever(pinnedSitesStorage.getPinnedSites()).thenReturn(listOf(pinnedSite))
            whenever(historyStorage.getTopFrecentSites(anyInt(), any())).thenReturn(emptyList())

            val topSites =
                defaultTopSitesStorage.getTopSites(
                    targetSitesCount = 1,
                    frecencyConfig = TopSitesFrecencyConfig(frecencyTresholdOption = FrecencyThresholdOption.NONE),
                )

            assertEquals(listOf(pinnedSite), topSites)
            verify(defaultTopSitesProvider, never()).getDefaultTopSites()
        }

    @Test
    fun `GIVEN there is only one slot left for multiple default top sites WHEN getTopSites is called THEN only the first default top site is added`() =
        runTest {
            val defaultTopSitesStorage =
                DefaultTopSitesStorage(
                    pinnedSitesStorage = pinnedSitesStorage,
                    historyStorage = historyStorage,
                    defaultTopSitesProvider = defaultTopSitesProvider,
                )

            whenever(pinnedSitesStorage.getPinnedSites()).thenReturn(listOf(pinnedSite))
            whenever(historyStorage.getTopFrecentSites(anyInt(), any())).thenReturn(listOf(frecentSite))
            whenever(defaultTopSitesProvider.getDefaultTopSites())
                .thenReturn(listOf(defaultSiteWikipedia, defaultSiteFirefox))

            val topSites =
                defaultTopSitesStorage.getTopSites(
                    targetSitesCount = 3,
                    frecencyConfig = TopSitesFrecencyConfig(frecencyTresholdOption = FrecencyThresholdOption.NONE),
                )

            assertEquals(listOf(pinnedSite, frecentSite.toTopSite(), defaultSiteWikipedia), topSites)
        }

    @Test
    fun `GIVEN frecent sites fill every slot WHEN getTopSites is called THEN the default top sites provider is not used`() =
        runTest {
            val defaultTopSitesStorage =
                DefaultTopSitesStorage(
                    pinnedSitesStorage = pinnedSitesStorage,
                    historyStorage = historyStorage,
                    defaultTopSitesProvider = defaultTopSitesProvider,
                )

            val otherFrecentSite = TopFrecentSiteInfo("https://example.org", "Example")

            whenever(pinnedSitesStorage.getPinnedSites()).thenReturn(emptyList())
            whenever(historyStorage.getTopFrecentSites(anyInt(), any()))
                .thenReturn(listOf(frecentSite, otherFrecentSite))

            val topSites =
                defaultTopSitesStorage.getTopSites(
                    targetSitesCount = 2,
                    frecencyConfig = TopSitesFrecencyConfig(frecencyTresholdOption = FrecencyThresholdOption.NONE),
                )

            assertEquals(listOf(frecentSite.toTopSite(), otherFrecentSite.toTopSite()), topSites)
            verify(defaultTopSitesProvider, never()).getDefaultTopSites()
        }

    @Test
    fun `GIVEN provided top sites WHEN getTopSites is called THEN the default top sites fill the slots left after the provided, pinned and frecent top sites`() =
        runTest {
            val defaultTopSitesStorage =
                DefaultTopSitesStorage(
                    pinnedSitesStorage = pinnedSitesStorage,
                    historyStorage = historyStorage,
                    topSitesProvider = topSitesProvider,
                    defaultTopSitesProvider = defaultTopSitesProvider,
                )

            val providedSite =
                TopSite.Provided(
                    id = 2,
                    title = "Amazon",
                    url = "https://www.amazon.com/?tag=sponsored-shortcut",
                    clickUrl = "https://www.amazon.com/click",
                    imageUrl = "https://test.com/image.jpg",
                    impressionUrl = "https://example.com",
                    createdAt = 2,
                )

            whenever(topSitesProvider.getTopSites()).thenReturn(listOf(providedSite))
            whenever(pinnedSitesStorage.getPinnedSites()).thenReturn(listOf(pinnedSite))
            whenever(historyStorage.getTopFrecentSites(anyInt(), any())).thenReturn(listOf(frecentSite))
            whenever(defaultTopSitesProvider.getDefaultTopSites())
                .thenReturn(listOf(defaultSiteWikipedia, defaultSiteFirefox))

            val topSites =
                defaultTopSitesStorage.getTopSites(
                    targetSitesCount = 4,
                    frecencyConfig = TopSitesFrecencyConfig(frecencyTresholdOption = FrecencyThresholdOption.NONE),
                    providerConfig = TopSitesProviderConfig(showProviderTopSites = true),
                )

            assertEquals(listOf(providedSite, pinnedSite, frecentSite.toTopSite(), defaultSiteWikipedia), topSites)
        }

    @Test
    fun `GIVEN the default top sites provider throws an exception WHEN getTopSites is called THEN the top sites are returned without any default top sites`() =
        runTest {
            val defaultTopSitesStorage =
                DefaultTopSitesStorage(
                    pinnedSitesStorage = pinnedSitesStorage,
                    historyStorage = historyStorage,
                    defaultTopSitesProvider = defaultTopSitesProvider,
                )

            whenever(pinnedSitesStorage.getPinnedSites()).thenReturn(listOf(pinnedSite))
            whenever(historyStorage.getTopFrecentSites(anyInt(), any())).thenReturn(emptyList())
            whenever(defaultTopSitesProvider.getDefaultTopSites()).thenThrow(IllegalStateException("test"))

            val topSites =
                defaultTopSitesStorage.getTopSites(
                    targetSitesCount = 5,
                    frecencyConfig = TopSitesFrecencyConfig(frecencyTresholdOption = FrecencyThresholdOption.NONE),
                )

            assertEquals(listOf(pinnedSite), topSites)
        }

    @Test(expected = CancellationException::class)
    fun `GIVEN the job is cancelled WHEN getTopSites is called THEN the cancellation propagates`() = runTest {
        val defaultTopSitesStorage =
            DefaultTopSitesStorage(
                pinnedSitesStorage = pinnedSitesStorage,
                historyStorage = historyStorage,
                defaultTopSitesProvider = defaultTopSitesProvider,
            )

        whenever(pinnedSitesStorage.getPinnedSites()).thenReturn(listOf(pinnedSite))
        whenever(historyStorage.getTopFrecentSites(anyInt(), any())).thenReturn(emptyList())
        whenever(defaultTopSitesProvider.getDefaultTopSites()).thenThrow(CancellationException())

        defaultTopSitesStorage.getTopSites(
            targetSitesCount = 5,
            frecencyConfig = TopSitesFrecencyConfig(frecencyTresholdOption = FrecencyThresholdOption.NONE),
        )
    }

    @Test
    fun `GIVEN more pinned sites than targetSitesCount WHEN getTopSites is called THEN all the pinned sites are returned without any other top sites`() =
        runTest {
            val defaultTopSitesStorage =
                DefaultTopSitesStorage(
                    pinnedSitesStorage = pinnedSitesStorage,
                    historyStorage = historyStorage,
                    defaultTopSitesProvider = defaultTopSitesProvider,
                )

            val pinnedSite2 = TopSite.Pinned(id = 2, title = "Firefox", url = "https://firefox.com", createdAt = 2)

            whenever(pinnedSitesStorage.getPinnedSites()).thenReturn(listOf(pinnedSite, pinnedSite2))
            whenever(historyStorage.getTopFrecentSites(anyInt(), any())).thenReturn(listOf(frecentSite))

            val topSites =
                defaultTopSitesStorage.getTopSites(
                    targetSitesCount = 1,
                    frecencyConfig = TopSitesFrecencyConfig(frecencyTresholdOption = FrecencyThresholdOption.NONE),
                )

            assertEquals(listOf(pinnedSite, pinnedSite2), topSites)
            verify(defaultTopSitesProvider, never()).getDefaultTopSites()
        }
}
