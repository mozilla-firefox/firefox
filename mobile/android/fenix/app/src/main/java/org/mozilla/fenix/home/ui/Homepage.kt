/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.ReadOnlyComposable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.PointerEventPass
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.colorResource
import androidx.compose.ui.res.dimensionResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.testTag
import androidx.compose.ui.semantics.testTagsAsResourceId
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.feature.top.sites.TopSite
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.fenix.GleanMetrics.History
import org.mozilla.fenix.GleanMetrics.HomeBookmarks
import org.mozilla.fenix.GleanMetrics.RecentlyVisitedHomepage
import org.mozilla.fenix.R
import org.mozilla.fenix.components.appstate.setup.checklist.SetupChecklistState
import org.mozilla.fenix.compose.MessageCard
import org.mozilla.fenix.compose.home.HomeSectionHeader
import org.mozilla.fenix.home.bookmarks.Bookmark
import org.mozilla.fenix.home.bookmarks.interactor.BookmarksInteractor
import org.mozilla.fenix.home.bookmarks.view.Bookmarks
import org.mozilla.fenix.home.bookmarks.view.BookmarksMenuItem
import org.mozilla.fenix.home.collections.Collections
import org.mozilla.fenix.home.collections.CollectionsState
import org.mozilla.fenix.home.fake.FakeHomepagePreview
import org.mozilla.fenix.home.interactor.HomepageInteractor
import org.mozilla.fenix.home.pocket.ui.PocketSection
import org.mozilla.fenix.home.recentsyncedtabs.view.RecentSyncedTab
import org.mozilla.fenix.home.recenttabs.RecentTab
import org.mozilla.fenix.home.recenttabs.interactor.RecentTabInteractor
import org.mozilla.fenix.home.recenttabs.view.RecentTabMenuItem
import org.mozilla.fenix.home.recenttabs.view.RecentTabs
import org.mozilla.fenix.home.recentvisits.RecentlyVisitedItem
import org.mozilla.fenix.home.recentvisits.RecentlyVisitedItem.RecentHistoryGroup
import org.mozilla.fenix.home.recentvisits.RecentlyVisitedItem.RecentHistoryHighlight
import org.mozilla.fenix.home.recentvisits.interactor.RecentVisitsInteractor
import org.mozilla.fenix.home.recentvisits.view.RecentVisitMenuItem
import org.mozilla.fenix.home.recentvisits.view.RecentlyVisited
import org.mozilla.fenix.home.sessioncontrol.CollectionInteractor
import org.mozilla.fenix.home.sessioncontrol.MessageCardInteractor
import org.mozilla.fenix.home.setup.ui.SetupChecklist
import org.mozilla.fenix.home.store.HeaderState
import org.mozilla.fenix.home.store.HomepageState
import org.mozilla.fenix.home.store.NimbusMessageState
import org.mozilla.fenix.home.termsofuse.PrivacyNoticeBanner
import org.mozilla.fenix.home.termsofuse.PrivacyNoticeBannerInteractor
import org.mozilla.fenix.home.topsites.TopSiteColors
import org.mozilla.fenix.home.topsites.TopSites
import org.mozilla.fenix.home.topsites.interactor.TopSiteInteractor
import org.mozilla.fenix.home.ui.HomepageTestTag.HOMEPAGE
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.theme.Theme
import org.mozilla.fenix.utils.isLargeScreenSize
import org.mozilla.fenix.wallpapers.WallpaperState
import mozilla.components.ui.icons.R as iconsR

/**
 * Top level composable for the homepage.
 *
 * @param state State representing the homepage.
 * @param interactor [HomepageInteractor] for interactions with the homepage UI.
 * @param onTopSitesItemBound Invoked during the composition of a top site item.
 * @param modifier [Modifier] to be applied to the layout.
 */
@Suppress("LongMethod", "CyclomaticComplexMethod", "CognitiveComplexMethod")
@Composable
internal fun Homepage(
    state: HomepageState,
    interactor: HomepageInteractor,
    onTopSitesItemBound: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val scrollState = rememberScrollState()

    BoxWithConstraints(
        modifier = modifier
            .fillMaxSize()
            .background(AcornTheme.colors.layer3),
    ) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(all = 32.dp)
                .semantics {
                    testTagsAsResourceId = true
                    testTag = HOMEPAGE
                }
                .pointerInput(state.isSearchInProgress) {
                    if (state.isSearchInProgress) {
                        awaitEachGesture {
                            awaitFirstDown(false, PointerEventPass.Initial)
                            interactor.onHomeContentFocusedWhileSearchIsActive()
                        }
                    }
                }
                .verticalScroll(scrollState),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            if (state is HomepageState.Normal && false) {
                BannerCardSection(
                    shouldShowPrivacyNoticeBanner = state.shouldShowPrivacyNoticeBanner,
                    nimbusMessage = state.nimbusMessage,
                    privacyNoticeBannerInteractor = interactor,
                    messageCardInteractor = interactor,
                )
            }

            if (state.headerState.showHeader && false) {
                HomepageHeader(
                    wordmarkTextColor = state.headerState.wordmarkTextColor,
                    privateBrowsingButtonColor = state.headerState.privateBrowsingButtonColor,
                    browsingMode = state.browsingMode,
                    browsingModeChanged = interactor::onPrivateModeButtonClicked,
                )
            } else {
                Spacer(modifier = Modifier.height(16.dp))
            }

            if (state.firstFrameDrawn) {
                with(state) {
                    when (this) {
                        is HomepageState.Private -> {
                            PrivateBrowsingDescription(
                                onLearnMoreClick = interactor::onLearnMoreClicked,
                            )
                        }

                        is HomepageState.Normal -> {
                            if (showTopSites) {
                                TopSitesSection(
                                    topSites = topSites,
                                    topSiteColors = topSiteColors,
                                    interactor = interactor,
                                    onTopSitesItemBound = onTopSitesItemBound,
                                )
                            }

                            MaybeAddSetupChecklist(setupChecklistState, interactor)

                            if (false) {
                                RecentTabsSection(
                                    interactor = interactor,
                                    cardBackgroundColor = cardBackgroundColor,
                                    recentTabs = recentTabs,
                                )

                                if (showRecentSyncedTab) {
                                    Box(
                                        modifier = Modifier.padding(
                                            start = horizontalMargin,
                                            end = horizontalMargin,
                                            top = verticalMargin,
                                        ),
                                    ) {
                                        RecentSyncedTab(
                                            tab = syncedTab,
                                            backgroundColor = cardBackgroundColor,
                                            buttonBackgroundColor = if (syncedTab != null) {
                                                buttonBackgroundColor
                                            } else {
                                                MaterialTheme.colorScheme.surfaceContainerHighest
                                            },
                                            buttonTextColor = buttonTextColor,
                                            onRecentSyncedTabClick = interactor::onRecentSyncedTabClicked,
                                            onSeeAllSyncedTabsButtonClick = interactor::onSyncedTabShowAllClicked,
                                            onRemoveSyncedTab = interactor::onRemovedRecentSyncedTab,
                                        )
                                    }
                                }
                            }

                            if (showBookmarks) {
                                BookmarksSection(
                                    bookmarks = bookmarks,
                                    cardBackgroundColor = AcornTheme.colors.backgroundPlate,
                                    interactor = interactor,
                                )
                            }

                            if (false) {
                                RecentlyVisitedSection(
                                    recentVisits = recentlyVisited,
                                    cardBackgroundColor = cardBackgroundColor,
                                    interactor = interactor,
                                )
                            }

                            if (false) {
                                CollectionsSection(
                                    collectionsState = collectionsState,
                                    interactor = interactor,
                                )
                            }

                            if (false) {
                                Spacer(
                                    modifier = if (isMinimalLayout()) {
                                        Modifier.weight(1f)
                                    } else {
                                        Modifier.padding(top = 72.dp)
                                    },
                                )

                                PocketSection(
                                    state = pocketState,
                                    cardBackgroundColor = cardBackgroundColor,
                                    interactor = interactor,
                                )
                            }

                            Spacer(Modifier.height(bottomPadding.dp))
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun MaybeAddSetupChecklist(
    setupChecklistState: SetupChecklistState?,
    interactor: HomepageInteractor,
) {
    val isTabletDevice = LocalContext.current.isLargeScreenSize()
    if (!isTabletDevice && setupChecklistState != null && setupChecklistState.isVisible) {
        SetupChecklist(
            setupChecklistState = setupChecklistState,
            interactor = interactor,
        )
    }
}

@Composable
private fun BannerCardSection(
    shouldShowPrivacyNoticeBanner: Boolean,
    nimbusMessage: NimbusMessageState?,
    privacyNoticeBannerInteractor: PrivacyNoticeBannerInteractor,
    messageCardInteractor: MessageCardInteractor,
) {
    if (shouldShowPrivacyNoticeBanner) {
        Spacer(modifier = Modifier.height(12.dp))

        PrivacyNoticeBanner(
            modifier = Modifier.padding(horizontal = 16.dp),
            interactor = privacyNoticeBannerInteractor,
        )
    }
    nimbusMessage?.apply {
        Spacer(modifier = Modifier.height(12.dp))

        MessageCard(
            messageCardState = cardState,
            modifier = Modifier.padding(horizontal = 16.dp),
            onClick = { messageCardInteractor.onMessageClicked(message) },
            onCloseButtonClick = { messageCardInteractor.onMessageClosedClicked(message) },
        )
    }
}

@Composable
internal fun TopSitesSection(
    topSites: List<TopSite>,
    topSiteColors: TopSiteColors = TopSiteColors.colors(),
    interactor: TopSiteInteractor,
    onTopSitesItemBound: () -> Unit,
) {
    HomeSectionHeader(
        headerText = stringResource(R.string.homepage_shortcuts_title),
        modifier = Modifier.padding(horizontal = horizontalMargin),
        description = stringResource(R.string.homepage_shortcuts_show_all_content_description),
        onButtonClick = interactor::onShowAllTopSitesClicked,
    )

    Spacer(Modifier.height(16.dp))

    TopSites(
        topSites = topSites,
        topSiteColors = topSiteColors,
        interactor = interactor,
        onTopSitesItemBound = onTopSitesItemBound,
    )
}

@Composable
private fun RecentTabsSection(
    interactor: RecentTabInteractor,
    cardBackgroundColor: Color,
    recentTabs: List<RecentTab>,
) {
    Spacer(modifier = Modifier.height(40.dp))

    Column(modifier = Modifier.padding(horizontal = horizontalMargin)) {
        HomeSectionHeader(
            headerText = stringResource(R.string.recent_tabs_header),
            description = stringResource(R.string.recent_tabs_show_all_content_description_2),
            onButtonClick = interactor::onRecentTabShowAllClicked,
        )

        Spacer(Modifier.height(16.dp))

        RecentTabs(
            recentTabs = recentTabs,
            backgroundColor = cardBackgroundColor,
            onRecentTabClick = { interactor.onRecentTabClicked(it) },
            menuItems = listOf(
                RecentTabMenuItem(
                    title = stringResource(id = R.string.recent_tab_menu_item_remove),
                    onClick = interactor::onRemoveRecentTab,
                ),
            ),
        )
    }
}

@Composable
private fun BookmarksSection(
    bookmarks: List<Bookmark>,
    cardBackgroundColor: Color,
    interactor: BookmarksInteractor,
) {
    LaunchedEffect(Unit) {
        HomeBookmarks.shown.record(NoExtras())
    }

    Spacer(modifier = Modifier.height(40.dp))

    HomeSectionHeader(
        headerText = stringResource(R.string.home_bookmarks_title),
        modifier = Modifier.padding(horizontal = horizontalMargin),
        description = stringResource(R.string.home_bookmarks_show_all_content_description),
        onButtonClick = interactor::onShowAllBookmarksClicked,
    )

    Spacer(Modifier.height(16.dp))

    Bookmarks(
        bookmarks = bookmarks,
        menuItems = listOf(
            BookmarksMenuItem(
                stringResource(id = R.string.home_bookmarks_menu_item_remove),
                onClick = interactor::onBookmarkRemoved,
            ),
        ),
        backgroundColor = cardBackgroundColor,
        onBookmarkClick = interactor::onBookmarkClicked,
    )
}

@Composable
private fun RecentlyVisitedSection(
    recentVisits: List<RecentlyVisitedItem>,
    cardBackgroundColor: Color,
    interactor: RecentVisitsInteractor,
) {
    Spacer(modifier = Modifier.height(40.dp))

    Box(modifier = Modifier.padding(horizontal = horizontalMargin)) {
        HomeSectionHeader(
            headerText = stringResource(R.string.history_metadata_header_2),
            description = stringResource(R.string.past_explorations_show_all_content_description_2),
            onButtonClick = interactor::onHistoryShowAllClicked,
        )
    }

    Spacer(Modifier.height(16.dp))

    RecentlyVisited(
        recentVisits = recentVisits,
        menuItems = listOfNotNull(
            RecentVisitMenuItem(
                title = stringResource(R.string.recently_visited_menu_item_remove),
                onClick = { visit ->
                    when (visit) {
                        is RecentHistoryGroup -> interactor.onRemoveRecentHistoryGroup(visit.title)
                        is RecentHistoryHighlight -> interactor.onRemoveRecentHistoryHighlight(
                            visit.url,
                        )
                    }
                },
            ),
        ),
        backgroundColor = cardBackgroundColor,
        onRecentVisitClick = { recentlyVisitedItem, pageNumber ->
            when (recentlyVisitedItem) {
                is RecentHistoryHighlight -> {
                    RecentlyVisitedHomepage.historyHighlightOpened.record(NoExtras())
                    interactor.onRecentHistoryHighlightClicked(recentlyVisitedItem)
                }

                is RecentHistoryGroup -> {
                    RecentlyVisitedHomepage.searchGroupOpened.record(NoExtras())
                    History.recentSearchesTapped.record(
                        History.RecentSearchesTappedExtra(
                            pageNumber.toString(),
                        ),
                    )
                    interactor.onRecentHistoryGroupClicked(recentlyVisitedItem)
                }
            }
        },
    )
}

@Composable
private fun CollectionsSection(
    collectionsState: CollectionsState,
    interactor: CollectionInteractor,
) {
    when (collectionsState) {
        is CollectionsState.Content -> {
            Column(modifier = Modifier.padding(horizontal = horizontalMargin)) {
                Spacer(Modifier.height(56.dp))

                HomeSectionHeader(headerText = stringResource(R.string.collections_header))

                Spacer(Modifier.height(10.dp))

                with(collectionsState) {
                    Collections(
                        collections = collections,
                        expandedCollections = expandedCollections,
                        showAddTabToCollection = showSaveTabsToCollection,
                        interactor = interactor,
                    )
                }
            }
        }

        CollectionsState.Gone -> {} // no-op. Nothing is shown where there are no collections.

        is CollectionsState.Placeholder -> {
            Box(
                modifier = Modifier.padding(
                    start = horizontalMargin,
                    end = horizontalMargin,
                    top = 40.dp,
                    bottom = 12.dp,
                ),
            ) {
                CollectionsPlaceholder(
                    showAddTabsToCollection = collectionsState.showSaveTabsToCollection,
                    colors = collectionsState.colors,
                    interactor = interactor,
                )
            }
        }
    }
}

@Composable
@PreviewLightDark
private fun HomepagePreview() {
    FirefoxTheme {
        Surface {
            Homepage(
                state = HomepageState.Normal(
                    shouldShowPrivacyNoticeBanner = false,
                    nimbusMessage = null,
                    topSites = FakeHomepagePreview.topSites(),
                    recentTabs = FakeHomepagePreview.recentTabs(),
                    syncedTab = FakeHomepagePreview.recentSyncedTab(),
                    bookmarks = FakeHomepagePreview.bookmarks(),
                    recentlyVisited = FakeHomepagePreview.recentHistory(),
                    collectionsState = FakeHomepagePreview.collectionsPlaceholder(),
                    pocketState = FakeHomepagePreview.pocketState(),
                    showTopSites = true,
                    showRecentTabs = true,
                    showRecentSyncedTab = true,
                    showBookmarks = true,
                    showRecentlyVisited = true,
                    showPocketStories = true,
                    showCollections = true,
                    headerState = HeaderState(
                        showHeader = false,
                        wordmarkTextColor = null,
                        privateBrowsingButtonColor = colorResource(
                            getAttr(
                                iconsR.attr.mozac_ic_private_mode_circle_fill_icon_color,
                            ),
                        ),
                    ),
                    searchBarVisible = true,
                    searchBarEnabled = false,
                    firstFrameDrawn = true,
                    setupChecklistState = null,
                    topSiteColors = TopSiteColors.colors(),
                    cardBackgroundColor = WallpaperState.default.cardBackgroundColor,
                    buttonTextColor = WallpaperState.default.buttonTextColor,
                    buttonBackgroundColor = WallpaperState.default.buttonBackgroundColor,
                    isSearchInProgress = false,
                    bottomPadding = 68,
                ),
                interactor = FakeHomepagePreview.homepageInteractor,
                onTopSitesItemBound = {},
                modifier = Modifier.fillMaxSize(),
            )
        }
    }
}

@Composable
@PreviewLightDark
private fun HomepageBannerPreview() {
    FirefoxTheme {
        Surface {
            Homepage(
                state = HomepageState.Normal(
                    shouldShowPrivacyNoticeBanner = true,
                    nimbusMessage = null,
                    topSites = FakeHomepagePreview.topSites(),
                    recentTabs = FakeHomepagePreview.recentTabs(),
                    syncedTab = FakeHomepagePreview.recentSyncedTab(),
                    bookmarks = FakeHomepagePreview.bookmarks(),
                    recentlyVisited = FakeHomepagePreview.recentHistory(),
                    collectionsState = FakeHomepagePreview.collectionsPlaceholder(),
                    pocketState = FakeHomepagePreview.pocketState(),
                    showTopSites = true,
                    showRecentTabs = true,
                    showRecentSyncedTab = true,
                    showBookmarks = true,
                    showRecentlyVisited = true,
                    showPocketStories = true,
                    showCollections = true,
                    headerState = HeaderState(
                        showHeader = true,
                        wordmarkTextColor = null,
                        privateBrowsingButtonColor = colorResource(
                            getAttr(
                                iconsR.attr.mozac_ic_private_mode_circle_fill_icon_color,
                            ),
                        ),
                    ),
                    searchBarVisible = true,
                    searchBarEnabled = false,
                    firstFrameDrawn = true,
                    setupChecklistState = null,
                    topSiteColors = TopSiteColors.colors(),
                    cardBackgroundColor = WallpaperState.default.cardBackgroundColor,
                    buttonTextColor = WallpaperState.default.buttonTextColor,
                    buttonBackgroundColor = WallpaperState.default.buttonBackgroundColor,
                    isSearchInProgress = false,
                    bottomPadding = 68,
                ),
                interactor = FakeHomepagePreview.homepageInteractor,
                onTopSitesItemBound = {},
                modifier = Modifier.fillMaxSize(),
            )
        }
    }
}

@Composable
@PreviewLightDark
private fun HomepagePreviewCollections() {
    FirefoxTheme {
        Surface {
            Homepage(
                state = HomepageState.Normal(
                    shouldShowPrivacyNoticeBanner = false,
                    nimbusMessage = null,
                    topSites = FakeHomepagePreview.topSites(),
                    recentTabs = FakeHomepagePreview.recentTabs(),
                    syncedTab = FakeHomepagePreview.recentSyncedTab(),
                    bookmarks = FakeHomepagePreview.bookmarks(),
                    recentlyVisited = FakeHomepagePreview.recentHistory(),
                    collectionsState = FakeHomepagePreview.collectionState(),
                    pocketState = FakeHomepagePreview.pocketState(),
                    showTopSites = false,
                    showRecentTabs = false,
                    showRecentSyncedTab = false,
                    showBookmarks = false,
                    showRecentlyVisited = true,
                    showPocketStories = true,
                    showCollections = true,
                    headerState = HeaderState(
                        showHeader = false,
                        wordmarkTextColor = null,
                        privateBrowsingButtonColor = colorResource(
                            getAttr(
                                iconsR.attr.mozac_ic_private_mode_circle_fill_icon_color,
                            ),
                        ),
                    ),
                    searchBarVisible = true,
                    searchBarEnabled = false,
                    firstFrameDrawn = true,
                    setupChecklistState = null,
                    topSiteColors = TopSiteColors.colors(),
                    cardBackgroundColor = WallpaperState.default.cardBackgroundColor,
                    buttonTextColor = WallpaperState.default.buttonTextColor,
                    buttonBackgroundColor = WallpaperState.default.buttonBackgroundColor,
                    isSearchInProgress = false,
                    bottomPadding = 68,
                ),
                interactor = FakeHomepagePreview.homepageInteractor,
                onTopSitesItemBound = {},
                modifier = Modifier.fillMaxSize(),
            )
        }
    }
}

@Composable
@PreviewLightDark
private fun MinimalHomepagePreview() {
    FirefoxTheme {
        Surface {
            Homepage(
                state = HomepageState.Normal(
                    shouldShowPrivacyNoticeBanner = false,
                    nimbusMessage = null,
                    topSites = FakeHomepagePreview.topSites(),
                    recentTabs = FakeHomepagePreview.recentTabs(),
                    syncedTab = FakeHomepagePreview.recentSyncedTab(),
                    bookmarks = FakeHomepagePreview.bookmarks(),
                    recentlyVisited = FakeHomepagePreview.recentHistory(),
                    collectionsState = FakeHomepagePreview.collectionState(),
                    pocketState = FakeHomepagePreview.pocketState(),
                    showTopSites = true,
                    showRecentTabs = false,
                    showRecentSyncedTab = false,
                    showBookmarks = false,
                    showRecentlyVisited = false,
                    showPocketStories = true,
                    showCollections = false,
                    HeaderState(
                        showHeader = false,
                        wordmarkTextColor = null,
                        privateBrowsingButtonColor = colorResource(
                            getAttr(
                                iconsR.attr.mozac_ic_private_mode_circle_fill_icon_color,
                            ),
                        ),
                    ),
                    searchBarVisible = false,
                    searchBarEnabled = false,
                    firstFrameDrawn = true,
                    setupChecklistState = null,
                    topSiteColors = TopSiteColors.colors(),
                    cardBackgroundColor = WallpaperState.default.cardBackgroundColor,
                    buttonTextColor = WallpaperState.default.buttonTextColor,
                    buttonBackgroundColor = WallpaperState.default.buttonBackgroundColor,
                    isSearchInProgress = false,
                    bottomPadding = 68,
                ),
                interactor = FakeHomepagePreview.homepageInteractor,
                onTopSitesItemBound = {},
                modifier = Modifier.fillMaxSize(),
            )
        }
    }
}

@Composable
@Preview
private fun PrivateHomepagePreview() {
    FirefoxTheme(theme = Theme.Private) {
        Homepage(
            state = HomepageState.Private(
                headerState = HeaderState(
                    showHeader = false,
                    wordmarkTextColor = null,
                    privateBrowsingButtonColor = colorResource(
                        getAttr(
                            iconsR.attr.mozac_ic_private_mode_circle_fill_icon_color,
                        ),
                    ),
                ),
                firstFrameDrawn = true,
                isSearchInProgress = false,
            ),
            interactor = FakeHomepagePreview.homepageInteractor,
            onTopSitesItemBound = {},
            modifier = Modifier.fillMaxSize(),
        )
    }
}

internal val horizontalMargin: Dp
    @Composable
    @ReadOnlyComposable
    get() = dimensionResource(R.dimen.home_item_horizontal_margin)

private val verticalMargin: Dp
    @Composable
    @ReadOnlyComposable
    get() = dimensionResource(R.dimen.home_item_vertical_margin)
