/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.topsites

import android.content.res.Resources
import io.mockk.coEvery
import io.mockk.coVerify
import io.mockk.every
import io.mockk.mockk
import io.mockk.verify
import java.io.ByteArrayInputStream
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import kotlinx.serialization.SerializationException
import mozilla.components.browser.state.action.SearchAction
import mozilla.components.browser.state.search.RegionState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.feature.top.sites.DefaultTopSitesProvider
import mozilla.components.feature.top.sites.DefaultTopSitesStorage
import mozilla.components.feature.top.sites.PinnedSiteStorage
import mozilla.components.feature.top.sites.TopSite
import mozilla.components.lib.crash.CrashReporter
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.mozilla.fenix.R
import org.mozilla.fenix.home.blocklist.BlocklistHandler
import org.mozilla.fenix.utils.Settings

class DefaultPinnedSitesBindingTest {

    private lateinit var browserStore: BrowserStore
    private lateinit var topSitesStorage: DefaultTopSitesStorage
    private lateinit var pinnedSitesStorage: PinnedSiteStorage
    private lateinit var defaultTopSitesProvider: DefaultTopSitesProvider
    private lateinit var blocklistHandler: BlocklistHandler
    private lateinit var settings: Settings
    private lateinit var resources: Resources
    private lateinit var crashReporter: CrashReporter

    private val dispatcher = StandardTestDispatcher()

    // A legacy default pinned top site that the user had removed from the pinned sites storage.
    private val removedLegacyDefaultPinnedSite =
        TopSite.Default(id = null, title = "Wikipedia", url = "https://www.wikipedia.org/", createdAt = null)

    // A legacy default pinned top site that is still in the pinned sites storage, as served by the provider.
    private val stillPinnedLegacyDefaultPinnedSite =
        TopSite.Default(id = null, title = "tenki.jp", url = "https://tenki.jp/", createdAt = null)

    // The pinned sites storage entry of [stillPinnedLegacyDefaultPinnedSite].
    private val pinnedLegacyDefaultPinnedSite =
        TopSite.DefaultPinned(id = 1, title = "tenki.jp", url = "https://tenki.jp/", createdAt = 1)

    // A default top site that was never seeded as a default pinned site.
    private val newDefaultSite =
        TopSite.Default(id = null, title = "Mozilla", url = "https://mozilla.org/", createdAt = null)

    @Before
    fun setUp() {
        topSitesStorage = mockk(relaxed = true)
        pinnedSitesStorage = mockk(relaxed = true)
        defaultTopSitesProvider = mockk(relaxed = true)
        blocklistHandler = mockk(relaxed = true)
        settings = mockk(relaxed = true)
        resources = mockk(relaxed = true)
        crashReporter = mockk(relaxed = true)

        browserStore = BrowserStore()

        every { resources.openRawResource(R.raw.default_pinned_shortcuts) } answers
            {
                this.javaClass.classLoader!!.getResourceAsStream("raw/test_default_pinned_shortcuts.json")!!
            }
    }

    @Test
    fun `GIVEN build is in the release channel WHEN region is set to the default THEN do nothing`() =
        runTest(dispatcher) {
            every { settings.defaultPinnedTopSitesAdded } returns false

            val binding = createBinding()
            binding.start()

            browserStore.dispatch(SearchAction.SetRegionAction(RegionState.Default))

            coVerify(exactly = 0) {
                topSitesStorage.addTopSites(topSites = any(), isDefault = any())
                settings.defaultPinnedTopSitesAdded = any()
            }
        }

    @Test
    fun `GIVEN debug build WHEN region is set to the default THEN add the default sites to storage`() =
        runTest(dispatcher) {
            every { settings.defaultPinnedTopSitesAdded } returns false

            val binding = createBinding(isReleased = false)
            binding.start()

            browserStore.dispatch(SearchAction.SetRegionAction(RegionState.Default))

            val pinnedSites = binding.getPinnedSites(region = "XX")
            dispatcher.scheduler.advanceUntilIdle()

            coVerify {
                topSitesStorage.addTopSites(topSites = pinnedSites.map { it.title to it.url }, isDefault = true)
                settings.defaultPinnedTopSitesAdded = true
            }
        }

    @Test
    fun `WHEN region is set to a non-default value THEN add the sites for that region to storage`() =
        runTest(dispatcher) {
            every { settings.defaultPinnedTopSitesAdded } returns false

            val region = "CA"
            val binding = createBinding()
            binding.start()

            browserStore.dispatch(SearchAction.SetRegionAction(RegionState(home = region, current = region)))

            val pinnedSites = binding.getPinnedSites(region = region)
            dispatcher.scheduler.advanceUntilIdle()

            coVerify {
                topSitesStorage.addTopSites(topSites = pinnedSites.map { it.title to it.url }, isDefault = true)
                settings.defaultPinnedTopSitesAdded = true
            }
            coVerify(exactly = 0) { defaultTopSitesProvider.getDefaultTopSites() }
            verify(exactly = 0) { blocklistHandler.addUrlToBlocklist(any()) }
            verify { settings.removedDefaultPinnedTopSitesMigrated = true }
        }

    @Test
    fun `GIVEN default pinned top sites have already been added and their removal have been migrated WHEN region is set to a non-default value THEN do nothing`() =
        runTest(dispatcher) {
            every { settings.defaultPinnedTopSitesAdded } returns true
            every { settings.removedDefaultPinnedTopSitesMigrated } returns true

            val region = "CA"
            val binding = createBinding()
            binding.start()

            browserStore.dispatch(SearchAction.SetRegionAction(RegionState(home = region, current = region)))
            dispatcher.scheduler.advanceUntilIdle()

            coVerify(exactly = 0) {
                topSitesStorage.addTopSites(topSites = any(), isDefault = any())
                settings.defaultPinnedTopSitesAdded = any()
                defaultTopSitesProvider.getDefaultTopSites()
            }
        }

    @Test
    fun `GIVEN a legacy default pinned top site is no longer pinned WHEN the removed default top sites are migrated THEN it is added to the blocklist`() =
        runTest(dispatcher) {
            every { settings.defaultPinnedTopSitesAdded } returns true
            every { settings.removedDefaultPinnedTopSitesMigrated } returns false
            coEvery { pinnedSitesStorage.getPinnedSites() } returns listOf(pinnedLegacyDefaultPinnedSite)
            coEvery { defaultTopSitesProvider.getDefaultTopSites() } returns
                listOf(removedLegacyDefaultPinnedSite, stillPinnedLegacyDefaultPinnedSite)

            createBinding().start()

            browserStore.dispatch(SearchAction.SetRegionAction(RegionState(home = "CA", current = "CA")))
            dispatcher.scheduler.advanceUntilIdle()

            verify { blocklistHandler.addUrlToBlocklist(removedLegacyDefaultPinnedSite.url) }
            verify(exactly = 0) { blocklistHandler.addUrlToBlocklist(stillPinnedLegacyDefaultPinnedSite.url) }
            verify { settings.removedDefaultPinnedTopSitesMigrated = true }
        }

    @Test
    fun `GIVEN a default top site that was never a default pinned site WHEN the removed default top sites are migrated THEN it is not added to the blocklist`() =
        runTest(dispatcher) {
            every { settings.defaultPinnedTopSitesAdded } returns true
            every { settings.removedDefaultPinnedTopSitesMigrated } returns false
            coEvery { pinnedSitesStorage.getPinnedSites() } returns emptyList()
            coEvery { defaultTopSitesProvider.getDefaultTopSites() } returns
                listOf(removedLegacyDefaultPinnedSite, newDefaultSite)

            createBinding().start()

            browserStore.dispatch(SearchAction.SetRegionAction(RegionState(home = "CA", current = "CA")))
            dispatcher.scheduler.advanceUntilIdle()

            verify { blocklistHandler.addUrlToBlocklist(removedLegacyDefaultPinnedSite.url) }
            verify(exactly = 0) { blocklistHandler.addUrlToBlocklist(newDefaultSite.url) }
            verify { settings.removedDefaultPinnedTopSitesMigrated = true }
        }

    @Test
    fun `GIVEN the legacy default pinned top sites are all still pinned WHEN the removed default top sites are migrated THEN nothing is added to the blocklist`() =
        runTest(dispatcher) {
            every { settings.defaultPinnedTopSitesAdded } returns true
            every { settings.removedDefaultPinnedTopSitesMigrated } returns false
            coEvery { pinnedSitesStorage.getPinnedSites() } returns listOf(pinnedLegacyDefaultPinnedSite)
            coEvery { defaultTopSitesProvider.getDefaultTopSites() } returns listOf(stillPinnedLegacyDefaultPinnedSite)

            createBinding().start()

            browserStore.dispatch(SearchAction.SetRegionAction(RegionState(home = "CA", current = "CA")))
            dispatcher.scheduler.advanceUntilIdle()

            verify(exactly = 0) { blocklistHandler.addUrlToBlocklist(any()) }
            verify { settings.removedDefaultPinnedTopSitesMigrated = true }
        }

    @Test
    fun `GIVEN region is in an included region WHEN getPinnedSites is called THEN the sites for that region are returned`() =
        runTest(dispatcher) {
            val binding = createBinding()
            val pinnedSites = binding.getPinnedSites(region = "US")

            assertEquals(7, pinnedSites.size)
            assertEquals("US Region Site", pinnedSites[0].title)
            assertEquals("https://www.example1.com/", pinnedSites[0].url)
            assertEquals("CA Excluded Region Site", pinnedSites[1].title)
            assertEquals("https://www.example2.com/", pinnedSites[1].url)
            assertEquals("All Region Site", pinnedSites[2].title)
            assertEquals("https://www.example3.com/", pinnedSites[2].url)
            assertEquals("www.example4.com", pinnedSites[3].title)
            assertEquals("https://www.example4.com/", pinnedSites[3].url)
            assertEquals("www.example5.com", pinnedSites[4].title)
            assertEquals("https://www.example5.com/", pinnedSites[4].url)
            assertEquals("www.example6.com", pinnedSites[5].title)
            assertEquals("https://www.example6.com/", pinnedSites[5].url)
            assertEquals("www.example7.com", pinnedSites[6].title)
            assertEquals("https://www.example7.com/", pinnedSites[6].url)
        }

    @Test
    fun `GIVEN region is in an excluded region WHEN getPinnedSites is called THEN the sites for that region are not returned`() =
        runTest(dispatcher) {
            val binding = createBinding()
            val pinnedSites = binding.getPinnedSites(region = "CA")

            assertEquals(5, pinnedSites.size)
            assertEquals("All Region Site", pinnedSites[0].title)
            assertEquals("https://www.example3.com/", pinnedSites[0].url)
            assertEquals("www.example4.com", pinnedSites[1].title)
            assertEquals("https://www.example4.com/", pinnedSites[1].url)
            assertEquals("www.example5.com", pinnedSites[2].title)
            assertEquals("https://www.example5.com/", pinnedSites[2].url)
            assertEquals("www.example6.com", pinnedSites[3].title)
            assertEquals("https://www.example6.com/", pinnedSites[3].url)
            assertEquals("www.example7.com", pinnedSites[4].title)
            assertEquals("https://www.example7.com/", pinnedSites[4].url)
        }

    @Test
    fun `GIVEN the default region region WHEN getPinnedSites is called THEN the sites for that region are returned`() =
        runTest(dispatcher) {
            val binding = createBinding()
            val pinnedSites = binding.getPinnedSites(region = "XX")

            assertEquals(6, pinnedSites.size)
            assertEquals("CA Excluded Region Site", pinnedSites[0].title)
            assertEquals("https://www.example2.com/", pinnedSites[0].url)
            assertEquals("All Region Site", pinnedSites[1].title)
            assertEquals("https://www.example3.com/", pinnedSites[1].url)
            assertEquals("www.example4.com", pinnedSites[2].title)
            assertEquals("https://www.example4.com/", pinnedSites[2].url)
            assertEquals("www.example5.com", pinnedSites[3].title)
            assertEquals("https://www.example5.com/", pinnedSites[3].url)
            assertEquals("www.example6.com", pinnedSites[4].title)
            assertEquals("https://www.example6.com/", pinnedSites[4].url)
            assertEquals("www.example7.com", pinnedSites[5].title)
            assertEquals("https://www.example7.com/", pinnedSites[5].url)
        }

    @Test
    fun `GIVEN the raw resource is missing WHEN getPinnedSites is called THEN return empty list and report crash`() =
        runTest(dispatcher) {
            every { resources.openRawResource(R.raw.default_pinned_shortcuts) } throws Resources.NotFoundException()

            val binding = createBinding()
            val pinnedSites = binding.getPinnedSites(region = "XX")

            assertTrue(pinnedSites.isEmpty())
            verify {
                crashReporter.recordCrashBreadcrumb(any())
                crashReporter.submitCaughtException(any<Resources.NotFoundException>())
            }
            coVerify(exactly = 0) {
                topSitesStorage.addTopSites(topSites = any(), isDefault = any())
                settings.defaultPinnedTopSitesAdded = any()
            }
        }

    @Test
    fun `GIVEN invalid json WHEN getPinnedSites is called THEN return empty list and report crash`() =
        runTest(dispatcher) {
            val malformedJson =
                "{\"data\": [{\"url\": \"https://example.com\", \"title\": \"Valid\"}, {\"id\": \"invalid\"}]}"
            every { resources.openRawResource(R.raw.default_pinned_shortcuts) } returns
                ByteArrayInputStream(malformedJson.toByteArray())

            val binding = createBinding()
            val pinnedSites = binding.getPinnedSites(region = "XX")

            assertTrue(pinnedSites.isEmpty())
            verify {
                crashReporter.recordCrashBreadcrumb(any())
                crashReporter.submitCaughtException(any<SerializationException>())
            }
            coVerify(exactly = 0) {
                topSitesStorage.addTopSites(topSites = any(), isDefault = any())
                settings.defaultPinnedTopSitesAdded = any()
            }
        }

    @Test
    fun `GIVEN malformed json WHEN getPinnedSites is called THEN return empty list and report crash for illegal argument`() =
        runTest(dispatcher) {
            val malformedJson = "this is not a valid json"
            every { resources.openRawResource(R.raw.default_pinned_shortcuts) } returns
                ByteArrayInputStream(malformedJson.toByteArray())

            val binding = createBinding()
            val pinnedSites = binding.getPinnedSites(region = "XX")

            assertTrue(pinnedSites.isEmpty())
            verify {
                crashReporter.recordCrashBreadcrumb(any())
                crashReporter.submitCaughtException(any<SerializationException>())
            }
            coVerify(exactly = 0) {
                topSitesStorage.addTopSites(topSites = any(), isDefault = any())
                settings.defaultPinnedTopSitesAdded = any()
            }
        }

    private fun createBinding(isReleased: Boolean = true) =
        DefaultPinnedSitesBinding(
            browserStore = browserStore,
            topSitesStorage = topSitesStorage,
            pinnedSitesStorage = pinnedSitesStorage,
            defaultTopSitesProvider = defaultTopSitesProvider,
            blocklistHandler = blocklistHandler,
            settings = settings,
            resources = resources,
            crashReporter = crashReporter,
            isReleased = isReleased,
            mainDispatcher = dispatcher,
            ioDispatcher = dispatcher,
        )
}
