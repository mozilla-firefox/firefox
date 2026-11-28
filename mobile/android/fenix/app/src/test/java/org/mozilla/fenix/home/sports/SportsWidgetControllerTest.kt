/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.sports

import android.net.Uri
import androidx.navigation.NavController
import androidx.navigation.NavDirections
import io.mockk.every
import io.mockk.mockk
import io.mockk.slot
import io.mockk.verify
import kotlin.test.Test
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.test.StandardTestDispatcher
import mozilla.components.concept.engine.prompt.ShareData
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.runner.RunWith
import org.mozilla.fenix.R
import org.mozilla.fenix.components.share.ShareSource
import org.mozilla.fenix.components.usecases.FenixBrowserUseCases
import org.mozilla.fenix.components.usecases.ShareUseCases
import org.mozilla.fenix.home.HomeFragmentDirections
import org.mozilla.fenix.utils.Settings
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
class SportsWidgetControllerTest {

    private lateinit var navController: NavController
    private lateinit var fenixBrowserUseCases: FenixBrowserUseCases
    private lateinit var settings: Settings
    private lateinit var shareUseCases: ShareUseCases
    private val thumbnailUri: Uri = mockk()
    private var thumbnailCreations = 0
    private val testDispatcher = StandardTestDispatcher()
    private lateinit var controller: SportsWidgetController

    @Before
    fun setup() {
        navController = mockk(relaxed = true)
        fenixBrowserUseCases = mockk(relaxed = true)
        settings = mockk(relaxed = true)
        shareUseCases = mockk(relaxed = true)

        every { navController.currentDestination } returns
            mockk {
                every { id } returns R.id.homeFragment
            }

        controller =
            SportsWidgetController(
                navController = navController,
                fenixBrowserUseCases = fenixBrowserUseCases,
                shareUseCases = shareUseCases,
                settings = settings,
                shareTitle = SHARE_TITLE,
                createShareThumbnail = {
                    thumbnailCreations++
                    thumbnailUri
                },
                scope = CoroutineScope(testDispatcher),
            )
    }

    @Config(sdk = [34])
    @Test
    fun `GIVEN the system share sheet is used WHEN handleSportsWidgetShareClicked is called THEN share the matches page with the label and thumbnail`() {
        every { settings.nativeShareSheetEnabled } returns true

        controller.handleSportsWidgetShareClicked(LABEL)
        testDispatcher.scheduler.advanceUntilIdle()

        verify {
            shareUseCases.shareItems(
                items =
                    listOf(ShareData(title = SHARE_TITLE, text = LABEL, url = WREXHAM_MATCHES_URL, private = false)),
                source = ShareSource.HOME,
                subject = SHARE_TITLE,
                thumbnailUri = thumbnailUri,
                text = LABEL,
                navigateToShareFragment = any(),
            )
        }
    }

    @Config(sdk = [34])
    @Test
    fun `GIVEN the native share sheet is disabled WHEN handleSportsWidgetShareClicked is called THEN share without creating a thumbnail`() {
        every { settings.nativeShareSheetEnabled } returns false

        controller.handleSportsWidgetShareClicked(LABEL)
        testDispatcher.scheduler.advanceUntilIdle()

        assertEquals(0, thumbnailCreations)
        verify {
            shareUseCases.shareItems(
                items = any(),
                source = ShareSource.HOME,
                subject = SHARE_TITLE,
                thumbnailUri = null,
                text = LABEL,
                navigateToShareFragment = any(),
            )
        }
    }

    @Config(sdk = [33])
    @Test
    fun `GIVEN the system share sheet is not supported WHEN handleSportsWidgetShareClicked is called THEN share without creating a thumbnail`() {
        every { settings.nativeShareSheetEnabled } returns true

        controller.handleSportsWidgetShareClicked(LABEL)
        testDispatcher.scheduler.advanceUntilIdle()

        assertEquals(0, thumbnailCreations)
        verify {
            shareUseCases.shareItems(
                items = any(),
                source = ShareSource.HOME,
                subject = SHARE_TITLE,
                thumbnailUri = null,
                text = LABEL,
                navigateToShareFragment = any(),
            )
        }
    }

    @Test
    fun `GIVEN the system share sheet is not used WHEN handleSportsWidgetShareClicked is called THEN navigate to the share fragment`() {
        every { settings.nativeShareSheetEnabled } returns false
        val navigateToShareFragment = slot<() -> Unit>()
        every {
            shareUseCases.shareItems(
                items = any(),
                source = any(),
                subject = any(),
                thumbnailUri = any(),
                text = any(),
                navigateToShareFragment = capture(navigateToShareFragment),
            )
        } answers { navigateToShareFragment.captured() }
        val directions = slot<NavDirections>()

        controller.handleSportsWidgetShareClicked(LABEL)
        testDispatcher.scheduler.advanceUntilIdle()

        verify { navController.navigate(capture(directions), null) }
        assertEquals(R.id.action_global_shareFragment, directions.captured.actionId)
        assertEquals(0, thumbnailCreations)
    }

    @Test
    fun `GIVEN homepage as a new tab is disabled WHEN handleSportsWidgetMatchesAndResultsClicked is called THEN open the matches page in a new tab`() {
        every { settings.enableHomepageAsNewTab } returns false

        controller.handleSportsWidgetMatchesAndResultsClicked()

        verify {
            navController.navigate(R.id.browserFragment)
            fenixBrowserUseCases.loadUrlOrSearch(
                searchTermOrURL = WREXHAM_MATCHES_URL,
                newTab = true,
                private = false,
            )
        }
    }

    @Test
    fun `GIVEN homepage as a new tab is enabled WHEN handleSportsWidgetMatchesAndResultsClicked is called THEN open the matches page in the homepage tab`() {
        every { settings.enableHomepageAsNewTab } returns true

        controller.handleSportsWidgetMatchesAndResultsClicked()

        verify {
            navController.navigate(R.id.browserFragment)
            fenixBrowserUseCases.loadUrlOrSearch(
                searchTermOrURL = WREXHAM_MATCHES_URL,
                newTab = false,
                private = false,
            )
        }
    }

    @Test
    fun `GIVEN homepage as a new tab is disabled WHEN handleSportsWidgetMatchClicked is called THEN open the matches page in a new tab`() {
        every { settings.enableHomepageAsNewTab } returns false

        controller.handleSportsWidgetMatchClicked()

        verify {
            navController.navigate(R.id.browserFragment)
            fenixBrowserUseCases.loadUrlOrSearch(
                searchTermOrURL = WREXHAM_MATCHES_URL,
                newTab = true,
                private = false,
            )
        }
    }

    @Test
    fun `GIVEN homepage as a new tab is enabled WHEN handleSportsWidgetMatchClicked is called THEN open the matches page in the homepage tab`() {
        every { settings.enableHomepageAsNewTab } returns true

        controller.handleSportsWidgetMatchClicked()

        verify {
            navController.navigate(R.id.browserFragment)
            fenixBrowserUseCases.loadUrlOrSearch(
                searchTermOrURL = WREXHAM_MATCHES_URL,
                newTab = false,
                private = false,
            )
        }
    }

    @Test
    fun `WHEN handleSportsWidgetWallpapersClicked is called THEN navigate to the wallpaper settings`() {
        controller.handleSportsWidgetWallpapersClicked()

        verify {
            navController.navigate(
                HomeFragmentDirections.actionGlobalWallpaperSettingsFragment(),
                null,
            )
        }
    }

    @Test
    fun `WHEN handleSportsWidgetRemoved is called THEN navigate to home settings`() {
        controller.handleSportsWidgetRemoved()

        verify {
            navController.navigate(
                HomeFragmentDirections.actionGlobalHomeSettingsFragment(),
                null,
            )
        }
    }

    private companion object {
        const val SHARE_TITLE = "Firefox - Wrexham AFC"
        const val LABEL = "Moz Cup"
    }
}
