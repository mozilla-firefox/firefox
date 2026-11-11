/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home

import android.content.Intent
import androidx.lifecycle.Lifecycle
import io.mockk.every
import io.mockk.mockk
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runTest
import mozilla.components.feature.top.sites.TopSite
import mozilla.components.feature.top.sites.TopSitesProvider
import mozilla.components.support.utils.RunWhenReadyQueue
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.mozilla.fenix.helpers.lifecycle.TestLifecycleOwner
import org.mozilla.fenix.perf.StartupPathProvider
import org.mozilla.fenix.utils.Settings

/** Class to test the [TopSitesRefresher] */
class TopSitesRefresherTest {

    private val testDispatcher = StandardTestDispatcher()
    private val testScope = TestScope(testDispatcher)

    private val lifecycleOwner = TestLifecycleOwner()

    private val topSitesProvider = FakeTopSitesProvider()
    private val settings: Settings = mockk(relaxed = true)

    private val visualCompletenessQueue = RunWhenReadyQueue(testScope)
    private val startupPathProvider = FakeStartupPathProvider(expectedPath = StartupPathProvider.StartupPath.NOT_SET)
    private lateinit var topSitesRefresher: TopSitesRefresher

    @Before
    fun setUp() {
        topSitesRefresher =
            TopSitesRefresher(
                settings = settings,
                topSitesProvider = topSitesProvider,
                visualCompletenessQueue = visualCompletenessQueue,
                startupPathProvider = startupPathProvider,
                dispatcher = testDispatcher,
            )

        lifecycleOwner.registerObserver(observer = topSitesRefresher)
    }

    @Test
    fun `WHEN lifecycle resumes AND we want to show contile feature THEN top sites are refreshed`() =
        runTest(context = testDispatcher) {
            every { settings.showContileFeature } returns true

            lifecycleOwner.onResume()
            testScheduler.advanceUntilIdle()

            assertTrue(topSitesProvider.cacheRefreshed)
        }

    @Test
    fun `WHEN lifecycle resumes AND we DO NOT want to show contile feature THEN top sites are NOT refreshed`() =
        runTest(context = testDispatcher) {
            every { settings.showContileFeature } returns false

            lifecycleOwner.onResume()
            testScheduler.advanceUntilIdle()

            assertFalse(topSitesProvider.cacheRefreshed)
        }

    @Test
    fun `GIVEN app link startup WHEN lifecycle resumes AND visual completeness is ready THEN top sites are refreshed`() =
        runTest(context = testDispatcher) {
            // given we want to show top sites
            every { settings.showContileFeature } returns true

            // given that this is an app link startup
            startupPathProvider.expectedPath = StartupPathProvider.StartupPath.VIEW

            // given that the visual completeness queue is read
            visualCompletenessQueue.ready()

            // When we resume
            lifecycleOwner.onResume()
            testScheduler.advanceUntilIdle()

            // Then validate that cache is refreshed
            assertTrue(
                "App link startup with visual completeness should refresh cache",
                topSitesProvider.cacheRefreshed,
            )
        }

    @Test
    fun `GIVEN app link startup WHEN lifecycle resumes AND visual completeness is NOT ready THEN top sites are NOT refreshed`() =
        runTest(context = testDispatcher) {
            // given we want to show top sites
            every { settings.showContileFeature } returns true

            // given that this is an app link startup
            startupPathProvider.expectedPath = StartupPathProvider.StartupPath.VIEW

            // When we resume
            lifecycleOwner.onResume()
            testScheduler.advanceUntilIdle()

            // Then validate that cache is not refreshed
            assertFalse(
                "App link startup should not refresh cache before visual completeness",
                topSitesProvider.cacheRefreshed,
            )
        }

    @Test
    fun `GIVEN app link startup WHEN lifecycle resumes THEN top sites are NOT refreshed only after visual completeness is ready`() =
        runTest(context = testDispatcher) {
            // given we want to show top sites
            every { settings.showContileFeature } returns true

            // given that this is an app link startup
            startupPathProvider.expectedPath = StartupPathProvider.StartupPath.VIEW

            // When we resume
            lifecycleOwner.onResume()
            testScheduler.advanceUntilIdle()

            // Then validate that cache is not refreshed
            assertFalse(
                "App link startup should not refresh cache BEFORE visual completeness",
                topSitesProvider.cacheRefreshed,
            )

            // When visual completeness queue is ready
            visualCompletenessQueue.ready()
            testScheduler.advanceUntilIdle()

            // Then cache should be refreshed
            assertTrue(
                "App link startup should refresh cache AFTER visual completeness",
                topSitesProvider.cacheRefreshed,
            )
        }

    private class FakeTopSitesProvider : TopSitesProvider {

        var expectedTopSites: List<TopSite> = emptyList()
        var cacheRefreshed: Boolean = false

        override suspend fun getTopSites(allowCache: Boolean): List<TopSite> = expectedTopSites

        override suspend fun refreshTopSitesIfCacheExpired() {
            cacheRefreshed = true
        }
    }

    private class FakeStartupPathProvider(var expectedPath: StartupPathProvider.StartupPath) : StartupPathProvider {
        override val startupPathForActivity: StartupPathProvider.StartupPath
            get() = expectedPath

        override fun attachOnActivityOnCreate(lifecycle: Lifecycle, intent: Intent?) = Unit

        override fun onIntentReceived(intent: Intent?) = Unit
    }
}
