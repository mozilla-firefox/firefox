/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.appstate

import androidx.annotation.VisibleForTesting
import mozilla.components.lib.crash.store.crashReducer
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.readerview.ReaderViewStateReducer
import org.mozilla.fenix.components.appstate.recommendations.ContentRecommendationsReducer
import org.mozilla.fenix.components.appstate.reducer.FindInPageStateReducer
import org.mozilla.fenix.components.appstate.setup.checklist.SetupChecklistReducer
import org.mozilla.fenix.components.appstate.shopping.ShoppingStateReducer
import org.mozilla.fenix.components.appstate.snackbar.SnackbarState
import org.mozilla.fenix.components.appstate.snackbar.SnackbarStateReducer
import org.mozilla.fenix.components.appstate.webcompat.WebCompatReducer
import org.mozilla.fenix.ext.filterOutTab
import org.mozilla.fenix.home.recentsyncedtabs.RecentSyncedTabState
import org.mozilla.fenix.home.recentvisits.RecentlyVisitedItem
import org.mozilla.fenix.home.recentvisits.RecentlyVisitedItem.RecentHistoryGroup
import org.mozilla.fenix.messaging.state.MessagingReducer
import org.mozilla.fenix.share.ShareActionReducer

/**
 * Reducer for [AppStore].
 */
internal object AppStoreReducer {
    @Suppress("LongMethod")
    fun reduce(state: AppState, action: AppAction): AppState = when (action) {
        is AppAction.UpdateInactiveExpanded ->
            state.copy(inactiveTabsExpanded = action.expanded)
        is AppAction.UpdateFirstFrameDrawn -> {
            state.copy(firstFrameDrawn = action.drawn)
        }
        is AppAction.AddNonFatalCrash ->
            state.copy(nonFatalCrashes = state.nonFatalCrashes + action.crash)
        is AppAction.RemoveNonFatalCrash ->
            state.copy(nonFatalCrashes = state.nonFatalCrashes - action.crash)
        is AppAction.RemoveAllNonFatalCrashes ->
            state.copy(nonFatalCrashes = emptyList())

        is AppAction.MessagingAction -> MessagingReducer.reduce(state, action)

        is AppAction.Change -> state.copy(
            collections = action.collections,
            mode = action.mode,
            topSites = action.topSites,
            bookmarks = action.bookmarks,
            recentTabs = action.recentTabs,
            recentHistory = action.recentHistory,
            recentSyncedTabState = action.recentSyncedTabState,
        )
        is AppAction.CollectionExpanded -> {
            val newExpandedCollection = state.expandedCollections.toMutableSet()

            if (action.expand) {
                newExpandedCollection.add(action.collection.id)
            } else {
                newExpandedCollection.remove(action.collection.id)
            }

            state.copy(expandedCollections = newExpandedCollection)
        }
        is AppAction.CollectionsChange -> state.copy(collections = action.collections)
        is AppAction.ModeChange -> state.copy(mode = action.mode)
        is AppAction.OrientationChange -> state.copy(orientation = action.orientation)
        is AppAction.TopSitesChange -> state.copy(topSites = action.topSites)
        is AppAction.RemoveCollectionsPlaceholder -> {
            state.copy(showCollectionPlaceholder = false)
        }
        is AppAction.RecentTabsChange -> {
            state.copy(
                recentTabs = action.recentTabs,
                recentHistory = state.recentHistory,
            )
        }
        is AppAction.RemoveRecentTab -> {
            state.copy(
                recentTabs = state.recentTabs.filterOutTab(action.recentTab),
            )
        }
        is AppAction.RecentSyncedTabStateChange -> {
            state.copy(
                recentSyncedTabState = action.state,
            )
        }
        is AppAction.BookmarksChange -> state.copy(bookmarks = action.bookmarks)
        is AppAction.RemoveBookmark -> {
            state.copy(bookmarks = state.bookmarks.filterNot { it.url == action.bookmark.url })
        }
        is AppAction.RecentHistoryChange -> state.copy(
            recentHistory = action.recentHistory,
        )
        is AppAction.RemoveRecentHistoryHighlight -> state.copy(
            recentHistory = state.recentHistory.filterNot {
                it is RecentlyVisitedItem.RecentHistoryHighlight && it.url == action.highlightUrl
            },
        )
        is AppAction.RemoveRecentSyncedTab -> state.copy(
            recentSyncedTabState = when (state.recentSyncedTabState) {
                is RecentSyncedTabState.Success -> RecentSyncedTabState.Success(
                    state.recentSyncedTabState.tabs - action.syncedTab,
                )
                else -> state.recentSyncedTabState
            },
        )
        is AppAction.DisbandSearchGroupAction -> state.copy(
            recentHistory = state.recentHistory.filterNot {
                it is RecentHistoryGroup && it.title.equals(action.searchTerm, true)
            },
        )

        is AppAction.AddPendingDeletionSet ->
            state.copy(pendingDeletionHistoryItems = state.pendingDeletionHistoryItems + action.historyItems)

        is AppAction.UndoPendingDeletionSet ->
            state.copy(pendingDeletionHistoryItems = state.pendingDeletionHistoryItems - action.historyItems)
        is AppAction.WallpaperAction.UpdateCurrentWallpaper ->
            state.copy(
                wallpaperState = state.wallpaperState.copy(currentWallpaper = action.wallpaper),
            )
        is AppAction.WallpaperAction.UpdateAvailableWallpapers ->
            state.copy(
                wallpaperState = state.wallpaperState.copy(availableWallpapers = action.wallpapers),
            )
        is AppAction.WallpaperAction.UpdateWallpaperDownloadState -> {
            val wallpapers = state.wallpaperState.availableWallpapers.map {
                if (it.name == action.wallpaper.name) {
                    it.copy(assetsFileState = action.imageState)
                } else {
                    it
                }
            }
            val wallpaperState = state.wallpaperState.copy(availableWallpapers = wallpapers)
            state.copy(wallpaperState = wallpaperState)
        }
        is AppAction.AppLifecycleAction.ResumeAction -> {
            state.copy(isForeground = true)
        }
        is AppAction.AppLifecycleAction.PauseAction -> {
            state.copy(isForeground = false)
        }

        is AppAction.UpdateStandardSnackbarErrorAction -> state.copy(
            standardSnackbarError = action.standardSnackbarError,
        )

        is AppAction.TabStripAction.UpdateLastTabClosed -> state.copy(
            wasLastTabClosedPrivate = action.private,
        )

        is AppAction.UpdateSearchDialogVisibility -> state.copy(isSearchDialogVisible = action.isVisible)

        is AppAction.TranslationsAction.TranslationStarted -> state.copy(
            snackbarState = SnackbarState.TranslationInProgress(sessionId = action.sessionId),
        )

        is AppAction.BookmarkAction.BookmarkAdded -> {
            state.copy(
                snackbarState = SnackbarState.BookmarkAdded(
                    guidToEdit = action.guidToEdit,
                    parentNode = action.parentNode,
                ),
            )
        }

        is AppAction.BookmarkAction.BookmarkDeleted -> state.copy(
            snackbarState = SnackbarState.BookmarkDeleted(title = action.title),
        )

        is AppAction.DeleteAndQuitStarted -> {
            state.copy(snackbarState = SnackbarState.DeletingBrowserDataInProgress)
        }

        is AppAction.SiteDataCleared -> state.copy(
            snackbarState = SnackbarState.SiteDataCleared,
        )

        is AppAction.CurrentTabClosed -> state.copy(
            snackbarState = SnackbarState.CurrentTabClosed(action.isPrivate),
        )

        is AppAction.OpenInFirefoxStarted -> {
            state.copy(openInFirefoxRequested = true)
        }

        is AppAction.OpenInFirefoxFinished -> {
            state.copy(openInFirefoxRequested = false)
        }

        is AppAction.UserAccountAuthenticated -> state.copy(
            snackbarState = SnackbarState.UserAccountAuthenticated,
        )

        is AppAction.ShareAction -> ShareActionReducer.reduce(state, action)
        is AppAction.FindInPageAction -> FindInPageStateReducer.reduce(state, action)
        is AppAction.ReaderViewAction -> ReaderViewStateReducer.reduce(state, action)
        is AppAction.ShortcutAction -> ShortcutStateReducer.reduce(state, action)
        is AppAction.ShoppingAction -> ShoppingStateReducer.reduce(state, action)
        is AppAction.CrashActionWrapper -> state.copy(
            crashState = crashReducer(state.crashState, action.inner),
        )

        is AppAction.SnackbarAction -> SnackbarStateReducer.reduce(state, action)
        is AppAction.UpdateWasNativeDefaultBrowserPromptShown -> {
            state.copy(wasNativeDefaultBrowserPromptShown = action.wasShown)
        }

        is AppAction.ContentRecommendationsAction -> ContentRecommendationsReducer.reduce(
            state = state,
            action = action,
        )

        is AppAction.WebCompatAction -> WebCompatReducer.reduce(state = state, action = action)
        is AppAction.SetupChecklistAction -> SetupChecklistReducer.reduce(
            state = state,
            action = action,
        )
    }
}

/**
 * Removes a [RecentHistoryGroup] identified by [groupTitle] if it exists in the current list.
 *
 * @param groupTitle [RecentHistoryGroup.title] of the item that should be removed.
 */
@VisibleForTesting
internal fun List<RecentlyVisitedItem>.filterOut(groupTitle: String?): List<RecentlyVisitedItem> {
    return when (groupTitle != null) {
        true -> filterNot { it is RecentHistoryGroup && it.title.equals(groupTitle, true) }
        false -> this
    }
}
