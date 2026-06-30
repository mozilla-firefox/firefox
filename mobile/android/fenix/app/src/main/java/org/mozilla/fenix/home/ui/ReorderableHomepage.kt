/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.ui

import androidx.annotation.DrawableRes
import androidx.annotation.StringRes
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
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
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.pointer.PointerEventPass
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.colorResource
import androidx.compose.ui.res.dimensionResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.testTag
import androidx.compose.ui.semantics.testTagsAsResourceId
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.setup.checklist.SetupChecklistState
import org.mozilla.fenix.components.appstate.sports.SportsWidgetState
import org.mozilla.fenix.components.components
import org.mozilla.fenix.compose.MessageCard
import org.mozilla.fenix.compose.home.HomeSectionHeader
import org.mozilla.fenix.home.bookmarks.view.Bookmarks
import org.mozilla.fenix.home.bookmarks.view.BookmarksMenuItem
import org.mozilla.fenix.home.collections.Collections
import org.mozilla.fenix.home.collections.CollectionsState
import org.mozilla.fenix.home.fake.FakeHomepagePreview
import org.mozilla.fenix.home.interactor.HomepageInteractor
import org.mozilla.fenix.home.pocket.ui.PocketSection
import org.mozilla.fenix.home.recentsyncedtabs.view.RecentSyncedTab
import org.mozilla.fenix.home.recenttabs.view.RecentTabMenuItem
import org.mozilla.fenix.home.recenttabs.view.RecentTabs
import org.mozilla.fenix.home.recentvisits.RecentlyVisitedItem.RecentHistoryGroup
import org.mozilla.fenix.home.recentvisits.RecentlyVisitedItem.RecentHistoryHighlight
import org.mozilla.fenix.home.recentvisits.view.RecentVisitMenuItem
import org.mozilla.fenix.home.recentvisits.view.RecentlyVisited
import org.mozilla.fenix.home.setup.ui.SetupChecklist
import org.mozilla.fenix.home.sports.CountrySelectorSource
import org.mozilla.fenix.home.sports.ui.SportsCountrySelectorBottomSheet
import org.mozilla.fenix.home.sports.ui.SportsWidget
import org.mozilla.fenix.home.store.HeaderState
import org.mozilla.fenix.home.store.HomepageState
import org.mozilla.fenix.home.termsofuse.PrivacyNoticeBanner
import org.mozilla.fenix.home.topsites.TOP_SITES_TO_SHOW
import org.mozilla.fenix.home.topsites.TopSiteColors
import org.mozilla.fenix.home.topsites.store.DialogState
import org.mozilla.fenix.home.topsites.store.toPopularSite
import org.mozilla.fenix.home.topsites.ui.AddShortcutBottomSheet
import org.mozilla.fenix.home.topsites.ui.AddShortcutDialog
import org.mozilla.fenix.home.ui.HomepageSection.Companion.fromOrderString
import org.mozilla.fenix.home.ui.HomepageSection.Companion.toOrderString
import org.mozilla.fenix.home.ui.HomepageTestTag.HOMEPAGE
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.trackingprotection.TrackersBlockedCard
import org.mozilla.fenix.utils.isLargeScreenSize
import org.mozilla.fenix.wallpapers.WallpaperState
import mozilla.components.ui.icons.R as iconsR

/**
 * The reorderable homepage sections, listed in their canonical (default) order. The integer order
 * configuration passed to [ReorderableHomepage] indexes into [entries]: the value at the first
 * position is the section drawn first, and so on. Index `0` therefore refers to [TOP_SITES].
 *
 * @property titleRes User-facing name of the section, shown when reordering the homepage in settings.
 * @property iconRes Icon shown next to the section name when reordering the homepage in settings.
 */
enum class HomepageSection(
    @param:StringRes val titleRes: Int,
    @param:DrawableRes val iconRes: Int,
) {
    WORDMARK(R.string.customize_toggle_header_wordmark, iconsR.drawable.mozac_ic_logo_firefox_24),
    HEADER_BUTTONS(R.string.customize_toggle_header_buttons, iconsR.drawable.mozac_ic_private_mode_24),
    TOP_SITES(R.string.top_sites_toggle_top_recent_sites_4, iconsR.drawable.mozac_ic_pin_24),
    PRIVACY_REPORT(R.string.customize_toggle_privacy_report, iconsR.drawable.mozac_ic_shield_24),
    SPORTS_WIDGET(R.string.customize_toggle_world_cup, iconsR.drawable.mozac_ic_soccer_ball_24),
    SETUP_CHECKLIST(R.string.customize_toggle_setup_checklist, iconsR.drawable.mozac_ic_checkmark_24),
    RECENT_TABS(R.string.customize_toggle_jump_back_in, iconsR.drawable.mozac_ic_sync_tabs_24),
    BOOKMARKS(R.string.customize_toggle_bookmarks, iconsR.drawable.mozac_ic_bookmark_tray_24),
    RECENTLY_VISITED(R.string.customize_toggle_recently_visited, iconsR.drawable.mozac_ic_history_24),
    COLLECTIONS(R.string.collections_header, iconsR.drawable.mozac_ic_collection_24),
    POCKET_STORIES(R.string.customize_toggle_pocket_3, iconsR.drawable.mozac_ic_newsfeed_24),
    ;

    companion object {
        private const val SEPARATOR = ","

        /**
         * Serializes [order] into a string suitable for persisting in preferences. Pair with
         * [fromOrderString] to read it back.
         */
        fun toOrderString(order: List<HomepageSection>): String =
            order.joinToString(SEPARATOR) { it.name }

        /**
         * Parses an order previously produced by [toOrderString]. Unknown names are skipped and any
         * sections missing from [value] are appended in their canonical [entries] order, so the
         * result always contains every section exactly once. A null or blank [value] yields the
         * canonical order.
         */
        fun fromOrderString(value: String?): List<HomepageSection> {
            if (value.isNullOrBlank()) return entries

            val parsed = value.split(SEPARATOR)
                .mapNotNull { name -> entries.firstOrNull { it.name == name } }
                .distinct()

            return parsed + entries.filterNot { it in parsed }
        }
    }
}

/**
 * A homepage screen whose content sections are rendered in a caller-defined order.
 *
 * The header is always pinned to the top; the remaining sections are drawn in the order described
 * by [sectionOrder]. Each entry of [sectionOrder] is an index into [HomepageSection.entries] (e.g.
 * `0` is [HomepageSection.TOP_SITES]). Indices are rendered in the order they appear, so
 * `listOf(8, 0)` draws the Pocket stories first and the top sites second. Out-of-range or duplicate
 * indices are ignored, and any section omitted from [sectionOrder] is not drawn. Individual sections
 * still honor their own visibility flags on [state], so a reordered-in section that is toggled off
 * produces nothing.
 *
 * This is intentionally separate from [Homepage] and only supports [HomepageState.Normal].
 *
 * @param state Normal-mode state representing the homepage.
 * @param interactor [HomepageInteractor] for interactions with the homepage UI.
 * @param onTopSitesItemBound Invoked during the composition of a top site item.
 * @param sectionOrder Order configuration: indices into [HomepageSection.entries], first drawn first.
 * @param modifier [Modifier] to be applied to the layout.
 */
@Suppress("LongMethod", "CyclomaticComplexMethod")
@Composable
internal fun ReorderableHomepage(
    state: HomepageState.Normal,
    interactor: HomepageInteractor,
    onTopSitesItemBound: () -> Unit,
    modifier: Modifier = Modifier,
    sectionOrder: List<Int> = HomepageSection.entries.indices.toList(),
) {
    val scrollState = rememberScrollState()
    var showSportsCountrySelector by remember { mutableStateOf(false) }
    var shortcutsDialogState by remember { mutableStateOf<DialogState>(DialogState.Closed) }

    val orderedSections = remember(sectionOrder) {
        sectionOrder.mapNotNull { HomepageSection.entries.getOrNull(it) }.distinct()
    }

    BoxWithConstraints(
        modifier = modifier.fillMaxSize(),
    ) {
        val scope = this
        Column(
            modifier = Modifier
                .fillMaxSize()
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
            // Explicit gap at the very top of the homepage, above the header and all sections,
            // independent of which header/section renders first.
            Spacer(Modifier.height(SECTION_SPACING))

            BannerCardSection(state = state, interactor = interactor)

            ReorderableHeader(
                state = state,
                interactor = interactor,
                onLogoClicked = { showSportsCountrySelector = true },
            )

            if (state.firstFrameDrawn) {
                LongfoxEffects(state = state, interactor = interactor)

                val isTabletDevice = LocalContext.current.isLargeScreenSize()
                val visibleSections = orderedSections.filter { it.isVisible(state, isTabletDevice) }

                visibleSections.forEachIndexed { index, section ->
                    if (index > 0) {
                        Spacer(Modifier.height(SECTION_SPACING))
                    }

                    when (section) {
                        HomepageSection.WORDMARK -> WordmarkSection(
                            state = state,
                            interactor = interactor,
                            onShowCountrySelector = { showSportsCountrySelector = true },
                        )

                        HomepageSection.HEADER_BUTTONS ->
                            HeaderButtonsSection(state = state, interactor = interactor)

                        HomepageSection.TOP_SITES -> TopSitesSection(
                            topSites = state.topSites,
                            topSiteColors = state.topSiteColors,
                            showHeader = state.showTopSitesHeader,
                            interactor = interactor,
                            onTopSitesItemBound = onTopSitesItemBound,
                            showAddShortcut = components.settings.enableAddShortcutsImprovement &&
                                state.topSites.size < TOP_SITES_TO_SHOW,
                            onAddShortcutClicked = {
                                shortcutsDialogState = DialogState.AddShortcutBottomSheet
                            },
                        )

                        HomepageSection.PRIVACY_REPORT ->
                            TrackersBlockedSection(state = state, interactor = interactor)

                        HomepageSection.SPORTS_WIDGET -> SportsWidgetSection(
                            state = state,
                            interactor = interactor,
                            onShowCountrySelector = { showSportsCountrySelector = true },
                        )

                        HomepageSection.SETUP_CHECKLIST -> MaybeSetupChecklistSection(
                            setupChecklistState = state.setupChecklistState,
                            interactor = interactor,
                        )

                        HomepageSection.RECENT_TABS ->
                            RecentTabsSection(state = state, interactor = interactor)

                        HomepageSection.BOOKMARKS ->
                            BookmarksSection(state = state, interactor = interactor)

                        HomepageSection.RECENTLY_VISITED ->
                            RecentlyVisitedSection(state = state, interactor = interactor)

                        HomepageSection.COLLECTIONS ->
                            CollectionsSection(collectionsState = state.collectionsState, interactor = interactor)

                        HomepageSection.POCKET_STORIES ->
                            PocketStoriesSection(state = state, interactor = interactor)
                    }
                }

                Spacer(Modifier.height(state.bottomPadding.dp))

                ReorderableHomepageDialogs(
                    state = state,
                    interactor = interactor,
                    showSportsCountrySelector = showSportsCountrySelector,
                    onDismissSportsCountrySelector = { showSportsCountrySelector = false },
                    shortcutsDialogState = shortcutsDialogState,
                    onShortcutsDialogStateChange = { shortcutsDialogState = it },
                )
            }
        }
    }
}

/**
 * Consistent vertical gap inserted between visible homepage sections, applied uniformly so that
 * spacing does not depend on the configured section order.
 */
private val SECTION_SPACING = 30.dp

/**
 * Whether [this] section renders any content for the given [state]. Used to decide which sections
 * take part in inter-section spacing, so hidden sections never leave a phantom gap.
 */
private fun HomepageSection.isVisible(
    state: HomepageState.Normal,
    isTabletDevice: Boolean,
): Boolean = when (this) {
    HomepageSection.WORDMARK -> state.headerState is HeaderState.Experimental.Normal
    HomepageSection.HEADER_BUTTONS -> state.headerState is HeaderState.Experimental.Normal
    HomepageSection.TOP_SITES -> state.showTopSites
    HomepageSection.PRIVACY_REPORT -> state.showPrivacyReport
    HomepageSection.SPORTS_WIDGET -> state.sportsWidgetState.isShown
    HomepageSection.SETUP_CHECKLIST ->
        !isTabletDevice && state.setupChecklistState?.isVisible == true
    HomepageSection.RECENT_TABS -> state.showRecentTabs
    HomepageSection.BOOKMARKS -> state.showBookmarks
    HomepageSection.RECENTLY_VISITED -> state.showRecentlyVisited
    HomepageSection.COLLECTIONS ->
        state.showCollections && state.collectionsState is CollectionsState.Content
    HomepageSection.POCKET_STORIES -> state.showPocketStoriesCarousel
}

@Composable
private fun BannerCardSection(
    state: HomepageState.Normal,
    interactor: HomepageInteractor,
) {
    if (state.shouldShowPrivacyNoticeBanner) {
        Spacer(modifier = Modifier.height(12.dp))

        PrivacyNoticeBanner(
            modifier = Modifier.padding(horizontal = 16.dp),
            interactor = interactor,
        )
    }
    state.nimbusMessage?.apply {
        Spacer(modifier = Modifier.height(12.dp))

        MessageCard(
            messageCardState = cardState,
            modifier = Modifier.padding(horizontal = 16.dp),
            onClick = { interactor.onMessageClicked(message) },
            onCloseButtonClick = { interactor.onMessageClosedClicked(message) },
        )
    }
}

@Composable
private fun ReorderableHeader(
    state: HomepageState.Normal,
    interactor: HomepageInteractor,
    onLogoClicked: () -> Unit,
) {
    val browsingModeChanged = interactor::onPrivateModeButtonClicked
    val settings = components.settings
    val shouldDisplaySportsLogo =
        settings.enableHomepageSportsWidget && settings.showHomepageSportsWidget
    val onLogoClick = {
        if (settings.showHomepageSportsWidget) {
            interactor.onCountrySelectorShown(CountrySelectorSource.SPORTS_LOGO)
            onLogoClicked()
        }
    }

    when (val headerState = state.headerState) {
        // Nothing is pinned for the experimental header: the wordmark (WORDMARK) and the
        // private/stories buttons (HEADER_BUTTONS) are rendered as reorderable sections instead.
        is HeaderState.Experimental.Normal -> {}

        is HeaderState.Experimental.Private -> {
            ExperimentalPrivateHomepageHeader(
                onHomeTapped = { browsingModeChanged(BrowsingMode.Normal) },
            )
        }

        is HeaderState.Normal -> {
            HomepageHeader(
                wordmarkTextColor = headerState.wordmarkTextColor,
                privateBrowsingButtonColor = headerState.privateBrowsingButtonColor,
                browsingMode = state.browsingMode,
                browsingModeChanged = browsingModeChanged,
                isSportsWidgetEnabled = shouldDisplaySportsLogo,
                onLogoClicked = onLogoClick,
            )
        }
    }
}

@Composable
private fun WordmarkSection(
    state: HomepageState.Normal,
    interactor: HomepageInteractor,
    onShowCountrySelector: () -> Unit,
) {
    val headerState = state.headerState
    if (headerState !is HeaderState.Experimental.Normal) return

    val settings = components.settings
    val shouldDisplaySportsLogo =
        settings.enableHomepageSportsWidget && settings.showHomepageSportsWidget
    ExperimentalHomepageWordmark(
        wordmarkTextColor = headerState.wordmarkTextColor,
        isSportsWidgetEnabled = shouldDisplaySportsLogo,
        onLogoClicked = {
            if (settings.showHomepageSportsWidget) {
                interactor.onCountrySelectorShown(CountrySelectorSource.SPORTS_LOGO)
                onShowCountrySelector()
            }
        },
        contentPadding = PaddingValues(horizontal = 16.dp),
        topSpacing = 0.dp,
    )
}

@Composable
private fun HeaderButtonsSection(
    state: HomepageState.Normal,
    interactor: HomepageInteractor,
) {
    val headerState = state.headerState
    if (headerState !is HeaderState.Experimental.Normal) return

    val settings = components.settings
    ExperimentalHomepageHeaderButtons(
        showStoriesButton = headerState.showStoriesButton,
        showButtonAnimation = headerState.showButtonAnimation,
        onPrivateModeTapped = { interactor.onPrivateModeButtonClicked(BrowsingMode.Private) },
        onStoriesTapped = { interactor.onDiscoverMoreClicked() },
        onNewsAnimationShown = { settings.recordNewsButtonAnimationShown() },
        contentPadding = PaddingValues(horizontal = 16.dp),
    )
}

@Composable
private fun LongfoxEffects(
    state: HomepageState.Normal,
    interactor: HomepageInteractor,
) {
    val settings = components.settings
    val appStore = components.appStore
    LaunchedEffect(state.showLongfoxAnimation) {
        if (state.showLongfoxAnimation) {
            settings.longfoxPeekAnimationShownCount++
            appStore.dispatch(AppAction.UpdateShowFoxPeekAnimation(false))
        }
    }

    val longfoxEntryPointShown = state.longfoxEnabled && state.showPrivacyReport
    LaunchedEffect(longfoxEntryPointShown) {
        if (longfoxEntryPointShown) {
            interactor.onLongfoxEntryPointShown()
        }
    }
}

@Composable
private fun TrackersBlockedSection(
    state: HomepageState.Normal,
    interactor: HomepageInteractor,
) {
    TrackersBlockedCard(
        trackersBlockedCount = state.trackersBlockedCount,
        onPrivacyReportTapped = interactor::onPrivacyReportTapped,
        onLongfoxEntryPointClicked = interactor::onLongfoxEntryPointClicked,
        longfoxEnabled = state.longfoxEnabled,
        showLongfoxAnimation = state.showLongfoxAnimation,
    )
}

@Composable
private fun SportsWidgetSection(
    state: HomepageState.Normal,
    interactor: HomepageInteractor,
    onShowCountrySelector: () -> Unit,
) {
    SportsWidget(
        sportsWidgetState = state.sportsWidgetState,
        onDismiss = interactor::onSportsWidgetDismissed,
        onCountdownWidgetDismiss = interactor::onCountdownWidgetDismissed,
        onViewSchedule = interactor::onViewScheduleClicked,
        onFollowTeam = { source ->
            interactor.onCountrySelectorShown(source)
            onShowCountrySelector()
        },
        onSkip = interactor::onSkippedFollowTeam,
        onGetCustomWallpaper = interactor::onGetCustomWallpaperClicked,
        onShare = interactor::onSportsWidgetShareClicked,
        onRefresh = { source -> interactor.onRefreshClicked(source) },
        onMatchClicked = { homeTeam, awayTeam, date ->
            interactor.onMatchClicked(homeTeam, awayTeam, date)
        },
        onCardShown = interactor::onSportsWidgetCardShown,
        showTopSpacing = true,
    )
}

@Composable
private fun MaybeSetupChecklistSection(
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
private fun RecentTabsSection(
    state: HomepageState.Normal,
    interactor: HomepageInteractor,
) {
    Column(modifier = Modifier.padding(horizontal = horizontalMargin)) {
        HomeSectionHeader(
            headerText = stringResource(R.string.recent_tabs_header),
            description = stringResource(R.string.recent_tabs_show_all_content_description_2),
            onButtonClick = interactor::onRecentTabShowAllClicked,
        )

        Spacer(Modifier.height(16.dp))

        RecentTabs(
            recentTabs = state.recentTabs,
            backgroundColor = state.cardBackgroundColor,
            onRecentTabClick = { interactor.onRecentTabClicked(it) },
            menuItems = listOf(
                RecentTabMenuItem(
                    title = stringResource(id = R.string.recent_tab_menu_item_remove),
                    onClick = interactor::onRemoveRecentTab,
                ),
            ),
        )
    }

    if (state.showRecentSyncedTab) {
        Box(
            modifier = Modifier.padding(
                start = horizontalMargin,
                end = horizontalMargin,
                top = dimensionResource(R.dimen.home_item_vertical_margin),
            ),
        ) {
            RecentSyncedTab(
                tab = state.syncedTab,
                backgroundColor = state.cardBackgroundColor,
                buttonBackgroundColor = if (state.syncedTab != null) {
                    state.buttonBackgroundColor
                } else {
                    MaterialTheme.colorScheme.surfaceContainerHighest
                },
                buttonTextColor = state.buttonTextColor,
                onRecentSyncedTabClick = interactor::onRecentSyncedTabClicked,
                onSeeAllSyncedTabsButtonClick = interactor::onSyncedTabShowAllClicked,
                onRemoveSyncedTab = interactor::onRemovedRecentSyncedTab,
            )
        }
    }
}

@Composable
private fun BookmarksSection(
    state: HomepageState.Normal,
    interactor: HomepageInteractor,
) {
    LaunchedEffect(Unit) {
//        HomeBookmarks.shown.record(NoExtras())
    }

    HomeSectionHeader(
        headerText = stringResource(R.string.home_bookmarks_title),
        modifier = Modifier.padding(horizontal = horizontalMargin),
        description = stringResource(R.string.home_bookmarks_show_all_content_description),
        onButtonClick = interactor::onShowAllBookmarksClicked,
    )

    Spacer(Modifier.height(16.dp))

    Bookmarks(
        bookmarks = state.bookmarks,
        menuItems = listOf(
            BookmarksMenuItem(
                stringResource(id = R.string.home_bookmarks_menu_item_remove),
                onClick = interactor::onBookmarkRemoved,
            ),
        ),
        backgroundColor = state.cardBackgroundColor,
        onBookmarkClick = interactor::onBookmarkClicked,
    )
}

@Composable
private fun RecentlyVisitedSection(
    state: HomepageState.Normal,
    interactor: HomepageInteractor,
) {
    Box(modifier = Modifier.padding(horizontal = horizontalMargin)) {
        HomeSectionHeader(
            headerText = stringResource(R.string.history_metadata_header_2),
            description = stringResource(R.string.past_explorations_show_all_content_description_2),
            onButtonClick = interactor::onHistoryShowAllClicked,
        )
    }

    Spacer(Modifier.height(16.dp))

    RecentlyVisited(
        recentVisits = state.recentlyVisited,
        menuItems = listOfNotNull(
            RecentVisitMenuItem(
                title = stringResource(R.string.recently_visited_menu_item_remove),
                onClick = { visit ->
                    when (visit) {
                        is RecentHistoryGroup -> interactor.onRemoveRecentHistoryGroup(visit.title)
                        is RecentHistoryHighlight -> interactor.onRemoveRecentHistoryHighlight(visit.url)
                    }
                },
            ),
        ),
        backgroundColor = state.cardBackgroundColor,
        onRecentVisitClick = { recentlyVisitedItem, pageNumber ->
            when (recentlyVisitedItem) {
                is RecentHistoryHighlight -> {
//                    RecentlyVisitedHomepage.historyHighlightOpened.record(NoExtras())
                    interactor.onRecentHistoryHighlightClicked(recentlyVisitedItem)
                }

                is RecentHistoryGroup -> {
//                    RecentlyVisitedHomepage.searchGroupOpened.record(NoExtras())
//                    History.recentSearchesTapped.record(
//                        History.RecentSearchesTappedExtra(pageNumber.toString()),
//                    )
                    interactor.onRecentHistoryGroupClicked(recentlyVisitedItem)
                }
            }
        },
    )
}

@Composable
private fun CollectionsSection(
    collectionsState: CollectionsState,
    interactor: HomepageInteractor,
) {
    when (collectionsState) {
        is CollectionsState.Content -> {
            Column(modifier = Modifier.padding(horizontal = horizontalMargin)) {
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

        CollectionsState.Gone -> {} // no-op. Nothing is shown when there are no collections.
    }
}

@Composable
private fun PocketStoriesSection(
    state: HomepageState.Normal,
    interactor: HomepageInteractor,
) {
    PocketSection(
        state = state.pocketState,
        cardBackgroundColor = state.cardBackgroundColor,
        interactor = interactor,
    )
}

@Composable
private fun ReorderableHomepageDialogs(
    state: HomepageState.Normal,
    interactor: HomepageInteractor,
    showSportsCountrySelector: Boolean,
    onDismissSportsCountrySelector: () -> Unit,
    shortcutsDialogState: DialogState,
    onShortcutsDialogStateChange: (DialogState) -> Unit,
) {
    if (showSportsCountrySelector) {
        val selectedCountryCode = state.sportsWidgetState.countriesSelected.firstOrNull()
        SportsCountrySelectorBottomSheet(
            selectedCountryCode = selectedCountryCode,
            eliminatedCountryCodes = state.sportsWidgetState.eliminatedCountries,
            onCountrySelected = { countryCode ->
                val selection = if (countryCode == selectedCountryCode) {
                    emptySet()
                } else {
                    setOf(countryCode)
                }
                interactor.onCountriesSelected(selection)
                onDismissSportsCountrySelector()
            },
            onDismiss = onDismissSportsCountrySelector,
        )
    }

    when (shortcutsDialogState) {
        DialogState.AddShortcutBottomSheet,
        DialogState.AddShortcut,
        -> ShortcutsDialog(
            shortcutsDialogState = shortcutsDialogState,
            interactor = interactor,
            onShortcutsDialogStateChange = onShortcutsDialogStateChange,
        )

        DialogState.Closed -> Unit
    }
}

@Composable
private fun ShortcutsDialog(
    shortcutsDialogState: DialogState,
    interactor: HomepageInteractor,
    onShortcutsDialogStateChange: (DialogState) -> Unit,
) {
    when (shortcutsDialogState) {
        DialogState.AddShortcutBottomSheet -> {
            val merinoManifestProvider = components.core.merinoManifestProvider
            val popularSites by produceState(
                initialValue = emptyList(),
                key1 = merinoManifestProvider,
            ) {
                value = withContext(Dispatchers.IO) {
                    merinoManifestProvider.getTopDomains(
                        limit = POPULAR_SITES_TO_SHOW,
                    ).map { it.toPopularSite() }
                }
            }

            AddShortcutBottomSheet(
                popularSites = popularSites,
                onDismiss = { onShortcutsDialogStateChange(DialogState.Closed) },
                onAddWebsiteClicked = { onShortcutsDialogStateChange(DialogState.AddShortcut) },
                onAddPopularSiteClick = { site ->
                    interactor.onSaveShortcut(title = site.title, url = site.url)
                    onShortcutsDialogStateChange(DialogState.Closed)
                },
            )
        }

        DialogState.AddShortcut -> {
            AddShortcutDialog(
                onDismiss = { onShortcutsDialogStateChange(DialogState.Closed) },
                onConfirm = { title, url ->
                    interactor.onSaveShortcut(title = title, url = url)
                    onShortcutsDialogStateChange(DialogState.Closed)
                },
            )
        }

        DialogState.Closed -> Unit
    }
}

private const val POPULAR_SITES_TO_SHOW = 8

@Composable
@PreviewLightDark
private fun ReorderableHomepagePreview() {
    FirefoxTheme {
        Surface {
            ReorderableHomepage(
                state = HomepageState.Normal(
                    shouldShowPrivacyNoticeBanner = false,
                    nimbusMessage = null,
                    topSites = FakeHomepagePreview.topSites(),
                    recentTabs = FakeHomepagePreview.recentTabs(),
                    syncedTab = FakeHomepagePreview.recentSyncedTab(),
                    bookmarks = FakeHomepagePreview.bookmarks(),
                    recentlyVisited = FakeHomepagePreview.recentHistory(),
                    collectionsState = CollectionsState.Gone,
                    pocketState = FakeHomepagePreview.pocketState(),
                    showTopSites = true,
                    showRecentTabs = true,
                    showRecentSyncedTab = true,
                    showBookmarks = true,
                    showRecentlyVisited = true,
                    showPocketStoriesCarousel = true,
                    showCollections = true,
                    showPrivacyReport = true,
                    longfoxEnabled = false,
                    showLongfoxAnimation = false,
                    trackersBlockedCount = 754,
                    sportsWidgetState = SportsWidgetState(),
                    headerState = HeaderState.Normal(
                        wordmarkTextColor = null,
                        privateBrowsingButtonColor = colorResource(
                            getAttr(iconsR.attr.mozac_ic_private_mode_circle_fill_icon_color),
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
                    showTopSitesHeader = true,
                ),
                interactor = FakeHomepagePreview.homepageInteractor,
                onTopSitesItemBound = {},
                // Stories first, then bookmarks, then top sites.
                sectionOrder = listOf(8, 5, 0),
                modifier = Modifier.fillMaxSize(),
            )
        }
    }
}
