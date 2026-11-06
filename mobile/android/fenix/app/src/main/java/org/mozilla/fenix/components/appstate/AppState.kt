/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.appstate

import mozilla.components.concept.storage.BookmarkNode
import mozilla.components.feature.tab.collections.TabCollection
import mozilla.components.feature.top.sites.TopSite
import mozilla.components.lib.crash.Crash.NativeCodeCrash
import mozilla.components.lib.crash.store.CrashState
import mozilla.components.lib.state.State
import org.mozilla.fenix.browser.StandardSnackbarError
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.components.appstate.blockedtrackers.BlockedTrackersState
import org.mozilla.fenix.components.appstate.lens.LensState
import org.mozilla.fenix.components.appstate.qrScanner.QrScannerState
import org.mozilla.fenix.components.appstate.readerview.ReaderViewState
import org.mozilla.fenix.components.appstate.recommendations.ContentRecommendationsState
import org.mozilla.fenix.components.appstate.search.SearchState
import org.mozilla.fenix.components.appstate.setup.checklist.SetupChecklistState
import org.mozilla.fenix.components.appstate.snackbar.SnackbarState
import org.mozilla.fenix.components.appstate.sports.SportsWidgetState
import org.mozilla.fenix.components.appstate.webcompat.WebCompatState
import org.mozilla.fenix.home.HomeFragment
import org.mozilla.fenix.home.bookmarks.Bookmark
import org.mozilla.fenix.home.recentsyncedtabs.RecentSyncedTabState
import org.mozilla.fenix.home.recenttabs.RecentTab
import org.mozilla.fenix.home.recentvisits.RecentlyVisitedItem
import org.mozilla.fenix.library.history.PendingDeletionHistory
import org.mozilla.fenix.messaging.MessagingState
import org.mozilla.fenix.microsurvey.MicrosurveyState
import org.mozilla.fenix.reviewprompt.ReviewPromptState
import org.mozilla.fenix.reviewprompt.ReviewPromptState.Unknown
import org.mozilla.fenix.wallpapers.WallpaperState

/**
 * Global application specific state that needs to live as long as the app (or longer than a Fragment).
 *
 * Before using the [AppState] to hold a feature’s state, consider the following constraints:
 * - Persistence to disk is not required.
 * - The state is needed for the lifetime of the app or on frequently used screens.
 * - Losing this state is acceptable if the operating system reclaims memory and the app restarts.
 * - Recreating the state does not involve expensive operations (for example, network or disk I/O).
 *
 * @property isForeground Whether or not the app is in the foreground.
 * @property inactiveTabsExpanded A flag to know if the Inactive Tabs section of the Tabs Tray should be expanded when
 *   the tray is opened.
 * @property firstFrameDrawn Flag indicating whether the first frame of the homescreen has been drawn.
 * @property openInFirefoxRequested Flag indicating whether a custom tab should be opened in the browser.
 * @property nonFatalCrashes List of non-fatal crashes that allow the app to continue being used.
 * @property collections The list of [TabCollection] to display in the [HomeFragment].
 * @property expandedCollections A set containing the ids of the [TabCollection] that are expanded in the
 *   [HomeFragment].
 * @property mode Whether the app is in private browsing mode.
 * @property orientation Current orientation of the application.
 * @property topSites The list of [TopSite] in the [HomeFragment].
 * @property recentTabs The list of recent [RecentTab] in the [HomeFragment].
 * @property recentSyncedTabState The [RecentSyncedTabState] in the [HomeFragment].
 * @property bookmarks The list of recently saved [BookmarkNode]s to show on the [HomeFragment].
 * @property recentHistory The list of [RecentlyVisitedItem]s.
 * @property recommendationState The [ContentRecommendationsState] to display.
 * @property messaging State related messages.
 * @property microsurvey The [MicrosurveyState] derived from the MICROSURVEY message surface.
 * @property pendingDeletionHistoryItems The set of History items marked for removal in the UI, awaiting to be removed
 *   once the Undo snackbar hides away. Also serves as an in memory cache of all stories mapped by category allowing for
 *   quick stories filtering.
 * @property wallpaperState The [WallpaperState] to display in the [HomeFragment].
 * @property standardSnackbarError A snackbar error message to display.
 * @property readerViewState The [ReaderViewState] to display.
 * @property isPowerSavingModeActive Whether Power Saving Mode is currently active.
 * @property powerSavingModeReaderViewTabIds The IDs of the tabs whose current reader view was forced by Power Saving
 *   Mode.
 * @property snackbarState The [SnackbarState] to display.
 * @property supportedMenuNotifications The set of currently active [SupportedMenuNotifications].
 * @property showFindInPage Whether or not to show the find in page feature.
 * @property crashState State related to the crash reporter.
 * @property wasLastTabClosedPrivate Whether the last remaining tab that was closed in private mode. This is used to
 *   display an undo snackbar message relevant to the browsing mode. If null, no snackbar is shown.
 * @property wasNativeDefaultBrowserPromptShown Whether the native default browser prompt was shown to the user.
 * @property webCompatState The [WebCompatState] when the feature was last used.
 * @property setupChecklistState Optional [SetupChecklistState] for the Setup Checklist feature.
 * @property searchState The current search state.
 * @property lensState The [LensState] for Google Lens image search.
 * @property qrScannerState The [QrScannerState] when the feature was last used.
 * @property isPrivateScreenLocked Whether the private browsing mode is currently locked behind authentication.
 * @property reviewPrompt Whether we should show a review prompt and whether we ran the eligibility check at all
 * @property voiceSearchState The [VoiceSearchState] representing the current state of voice search functionality.
 * @property isDefaultBrowser Whether Firefox is the default browser or not.
 * @property blockedTrackersState The [BlockedTrackersState] with data about blocked trackers.
 * @property longfoxEntryPointReady Whether the fox peek animation should play on the next homepage view.
 * @property isTabsTrayVisible Whether TabsTray is shown.
 * @property sportsWidgetState The [SportsWidgetState] of the Wrexham widget on the homepage.
 */
data class AppState(
    val isForeground: Boolean = true,
    val inactiveTabsExpanded: Boolean = false,
    val firstFrameDrawn: Boolean = false,
    val openInFirefoxRequested: Boolean = false,
    val nonFatalCrashes: List<NativeCodeCrash> = emptyList(),
    val collections: List<TabCollection> = emptyList(),
    val expandedCollections: Set<Long> = emptySet(),
    val mode: BrowsingMode = BrowsingMode.Normal,
    val orientation: OrientationMode = OrientationMode.Undefined,
    val topSites: List<TopSite> = emptyList(),
    val recentTabs: List<RecentTab> = emptyList(),
    val recentSyncedTabState: RecentSyncedTabState = RecentSyncedTabState.None,
    val bookmarks: List<Bookmark> = emptyList(),
    val recentHistory: List<RecentlyVisitedItem> = emptyList(),
    val recommendationState: ContentRecommendationsState = ContentRecommendationsState(),
    val messaging: MessagingState = MessagingState(),
    val microsurvey: MicrosurveyState = MicrosurveyState(),
    val pendingDeletionHistoryItems: Set<PendingDeletionHistory> = emptySet(),
    val wallpaperState: WallpaperState = WallpaperState.default,
    val standardSnackbarError: StandardSnackbarError? = null,
    val readerViewState: ReaderViewState = ReaderViewState.None,
    val isPowerSavingModeActive: Boolean = false,
    val powerSavingModeReaderViewTabIds: Set<String> = emptySet(),
    val snackbarState: SnackbarState = SnackbarState.None(),
    val supportedMenuNotifications: Set<SupportedMenuNotifications> = emptySet(),
    val showFindInPage: Boolean = false,
    val crashState: CrashState = CrashState.Idle,
    val wasLastTabClosedPrivate: Boolean? = null,
    val wasNativeDefaultBrowserPromptShown: Boolean = false,
    val webCompatState: WebCompatState? = null,
    val setupChecklistState: SetupChecklistState? = null,
    val searchState: SearchState = SearchState.EMPTY,
    val lensState: LensState = LensState.DEFAULT,
    val qrScannerState: QrScannerState = QrScannerState.DEFAULT,
    val isPrivateScreenLocked: Boolean = false,
    val reviewPrompt: ReviewPromptState = Unknown,
    val voiceSearchState: VoiceSearchState = VoiceSearchState(),
    val isDefaultBrowser: Boolean = false,
    val blockedTrackersState: BlockedTrackersState = BlockedTrackersState(),
    val longfoxEntryPointReady: Boolean = false,
    val isTabsTrayVisible: Boolean = false,
    val sportsWidgetState: SportsWidgetState = SportsWidgetState(),
) : State
