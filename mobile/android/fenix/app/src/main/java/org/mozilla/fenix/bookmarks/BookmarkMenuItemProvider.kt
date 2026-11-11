/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.bookmarks

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.async
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.MenuItem
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.compose.menu.ui.MenuItemState
import mozilla.components.concept.storage.BookmarksStorage
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.NavGraphDirections
import org.mozilla.fenix.R
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction.BookmarkAction
import org.mozilla.fenix.components.bookmarks.BookmarksUseCase
import org.mozilla.fenix.components.menu.MenuHost
import org.mozilla.fenix.components.menu.MenuItemProvider
import org.mozilla.fenix.components.menu.MenuTarget
import org.mozilla.fenix.components.menu.middleware.getTabUrl
import org.mozilla.fenix.components.menu.store.MenuAction
import org.mozilla.fenix.components.metrics.MetricsUtils

/**
 * [MenuItemProvider] for the menu item allowing to bookmark the current page, or to edit the bookmark it already has.
 *
 * @param browserStore [BrowserStore] allowing to integrate with the current open tabs.
 * @param target [MenuTarget] for which this menu item would be shown for.
 * @param bookmarksStorage [BookmarksStorage] used to check whether that page is already bookmarked.
 * @param addBookmark [BookmarksUseCase.AddBookmarksUseCase] for bookmarking the current page.
 * @param appStore [AppStore] for informing the rest of the application about a new bookmark.
 * @param scope [CoroutineScope] tied to the lifetime of the menu, on which to wait for a bookmark to be added.
 * @param applicationScope [CoroutineScope] tied to the lifetime of the application, on which to query and add the
 *   bookmarks, since that cannot be interrupted.
 */
class BookmarkMenuItemProvider(
    private val browserStore: BrowserStore,
    private val target: MenuTarget,
    bookmarksStorage: BookmarksStorage,
    private val addBookmark: BookmarksUseCase.AddBookmarksUseCase,
    private val appStore: AppStore,
    private val scope: CoroutineScope,
    private val applicationScope: CoroutineScope,
) : MenuItemProvider {
    // Without a page shown there is nothing to bookmark. Knowing whether the shown page already is bookmarked means
    // reading from disk, which the menu should not wait for, so it starts out offered as not bookmarked.
    private val mutableItems =
        MutableStateFlow<List<MenuItem>>(
            listOfNotNull(
                if (target == MenuTarget.CustomizeMenu) {
                    DEFAULT_MENU_ITEM
                } else if (target == MenuTarget.Preview) {
                    DEFAULT_MENU_ITEM.copy(onClickEvent = MenuAction.AddBookmark)
                } else {
                    DEFAULT_MENU_ITEM.copy(onClickEvent = MenuAction.AddBookmark).takeIf {
                        target.browserSessionFrom(browserStore.state) != null
                    }
                }
            )
        )

    override val itemsFlow: StateFlow<List<MenuItem>> = mutableItems.asStateFlow()

    init {
        if (target != MenuTarget.CustomizeMenu && target != MenuTarget.Preview) {
            // BookmarksStorage#getBookmarksWithUrl will run until completion even if the coroutine is canceled, so it
            // is deliberately run on a scope outliving this menu. As such it must not reference this provider, which
            // would then be leaked together with the menu it holds onto while waiting for the bookmarks.
            val url = target.browserSessionFrom(browserStore.state)?.getTabUrl()
            val mutableItems = mutableItems
            if (url != null) {
                applicationScope.launch {
                    val guidToEdit =
                        bookmarksStorage
                            .getBookmarksWithUrl(url)
                            .getOrDefault(emptyList())
                            .firstOrNull { it.url == url }
                            ?.guid ?: return@launch

                    mutableItems.value = listOf(editBookmarkItem(guidToEdit))
                }
            }
        }
    }

    override fun handles(event: MenuEvent) =
        target != MenuTarget.Preview && (event == MenuAction.AddBookmark || event is MenuAction.Navigate.EditBookmark)

    override fun onEvent(event: MenuEvent, menu: MenuHost) {
        when (event) {
            MenuAction.AddBookmark -> bookmarkCurrentPage(menu)
            is MenuAction.Navigate.EditBookmark -> {
                val guidToEdit = event.guidToEdit ?: return
                menu.navigate(
                    NavGraphDirections.actionGlobalBookmarkEditFragment(
                        guidToEdit = guidToEdit,
                        requiresSnackbarPaddingForToolbar = true,
                    )
                )
            }
            else -> Unit
        }
    }

    private fun bookmarkCurrentPage(menu: MenuHost) {
        val tab = target.browserSessionFrom(browserStore.state) ?: return
        val url = tab.getTabUrl() ?: return
        val title = tab.content.title

        scope.launch {
            // Saving a bookmark will run until completion even if the coroutine is canceled, so the work outliving the
            // menu must not reference this provider, which would otherwise be leaked while waiting for it.
            val addBookmark = addBookmark
            val result = applicationScope.async { addBookmark(url = url, title = title) }.await()

            appStore.dispatch(
                BookmarkAction.BookmarkAdded(
                    guidToEdit = result.guidToEdit,
                    parentNode = result.parentNode,
                    source = MetricsUtils.BookmarkAction.Source.MENU_DIALOG,
                )
            )
            menu.dismiss()
        }
    }

    companion object {
        private val DEFAULT_MENU_ITEM =
            StandardMenuItem(
                title = MenuItemTitle(Text.Resource(R.string.browser_menu_bookmark_this_page_2)),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_bookmark_24),
            )
    }
}

private fun editBookmarkItem(guidToEdit: String) =
    StandardMenuItem(
        title = MenuItemTitle(Text.Resource(R.string.browser_menu_edit_bookmark)),
        icon = MenuItemIconRes(iconsR.drawable.mozac_ic_bookmark_fill_24),
        onClickEvent = MenuAction.Navigate.EditBookmark(guidToEdit = guidToEdit),
        state = MenuItemState.ACTIVE,
    )
