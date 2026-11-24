/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.recentvisits.interactor

import io.mockk.mockk
import io.mockk.verify
import mozilla.components.concept.storage.DocumentType
import mozilla.components.concept.storage.HistoryMetadata
import mozilla.components.concept.storage.HistoryMetadataKey
import org.junit.Before
import org.junit.Test
import org.mozilla.fenix.home.bookmarks.controller.BookmarksController
import org.mozilla.fenix.home.logo.TrackingProtectionController
import org.mozilla.fenix.home.pocket.controller.PocketStoriesController
import org.mozilla.fenix.home.privatebrowsing.controller.PrivateBrowsingController
import org.mozilla.fenix.home.recentsyncedtabs.controller.RecentSyncedTabController
import org.mozilla.fenix.home.recenttabs.controller.RecentTabController
import org.mozilla.fenix.home.recentvisits.RecentlyVisitedItem.RecentHistoryGroup
import org.mozilla.fenix.home.recentvisits.RecentlyVisitedItem.RecentHistoryHighlight
import org.mozilla.fenix.home.recentvisits.controller.RecentVisitsController
import org.mozilla.fenix.home.search.HomeSearchController
import org.mozilla.fenix.home.sessioncontrol.DefaultSessionControlController
import org.mozilla.fenix.home.sessioncontrol.SessionControlInteractor
import org.mozilla.fenix.home.sports.SportsWidgetController
import org.mozilla.fenix.home.termsofuse.PrivacyNoticeBannerController
import org.mozilla.fenix.home.toolbar.ToolbarController
import org.mozilla.fenix.home.topsites.controller.TopSiteController

class RecentVisitsInteractorTest {
    private val defaultSessionControlController: DefaultSessionControlController = mockk(relaxed = true)
    private val recentTabController: RecentTabController = mockk(relaxed = true)
    private val recentSyncedTabController: RecentSyncedTabController = mockk(relaxed = true)
    private val bookmarksController: BookmarksController = mockk(relaxed = true)
    private val recentVisitsController: RecentVisitsController = mockk(relaxed = true)
    private val pocketStoriesController: PocketStoriesController = mockk(relaxed = true)
    private val privateBrowsingController: PrivateBrowsingController = mockk(relaxed = true)
    private val toolbarController: ToolbarController = mockk(relaxed = true)
    private val homeSearchController: HomeSearchController = mockk(relaxed = true)
    private val topSiteController: TopSiteController = mockk(relaxed = true)
    private val privacyNoticeBannerController: PrivacyNoticeBannerController = mockk(relaxed = true)
    private val trackingProtectionController: TrackingProtectionController = mockk(relaxed = true)
    private val sportsWidgetController: SportsWidgetController = mockk(relaxed = true)

    private lateinit var interactor: SessionControlInteractor

    @Before
    fun setup() {
        interactor =
            SessionControlInteractor(
                defaultSessionControlController,
                recentTabController,
                recentSyncedTabController,
                bookmarksController,
                recentVisitsController,
                pocketStoriesController,
                privateBrowsingController,
                toolbarController,
                homeSearchController,
                topSiteController,
                privacyNoticeBannerController,
                trackingProtectionController,
                sportsWidgetController,
            )
    }

    @Test
    fun handleRecentHistoryGroupClicked() {
        val historyGroup =
            RecentHistoryGroup(
                title = "mozilla",
                historyMetadata =
                    listOf(
                        HistoryMetadata(
                            key = HistoryMetadataKey("http://www.mozilla.com", null, null),
                            title = "mozilla",
                            createdAt = System.currentTimeMillis(),
                            updatedAt = System.currentTimeMillis(),
                            totalViewTime = 10,
                            documentType = DocumentType.Regular,
                            previewImageUrl = null,
                        )
                    ),
            )

        interactor.onRecentHistoryGroupClicked(historyGroup)
        verify {
            recentVisitsController.handleRecentHistoryGroupClicked(historyGroup)
        }
    }

    @Test
    fun handleHistoryShowAllClicked() {
        interactor.onHistoryShowAllClicked()
        verify { recentVisitsController.handleHistoryShowAllClicked() }
    }

    @Test
    fun onRemoveRecentHistoryGroup() {
        val historyMetadataKey =
            HistoryMetadataKey(
                "http://www.mozilla.com",
                "mozilla",
                null,
            )

        val historyGroup =
            RecentHistoryGroup(
                title = "mozilla",
                historyMetadata =
                    listOf(
                        HistoryMetadata(
                            key = historyMetadataKey,
                            title = "mozilla",
                            createdAt = System.currentTimeMillis(),
                            updatedAt = System.currentTimeMillis(),
                            totalViewTime = 10,
                            documentType = DocumentType.Regular,
                            previewImageUrl = null,
                        )
                    ),
            )

        interactor.onRemoveRecentHistoryGroup(historyGroup.title)

        verify {
            recentVisitsController.handleRemoveRecentHistoryGroup(historyGroup.title)
        }
    }

    @Test
    fun onRecentHistoryHighlightClicked() {
        val historyHighlight: RecentHistoryHighlight = mockk()

        interactor.onRecentHistoryHighlightClicked(historyHighlight)

        verify { recentVisitsController.handleRecentHistoryHighlightClicked(historyHighlight) }
    }

    @Test
    fun onRemoveRecentHistoryHighlight() {
        interactor.onRemoveRecentHistoryHighlight("url")

        verify { recentVisitsController.handleRemoveRecentHistoryHighlight("url") }
    }
}
