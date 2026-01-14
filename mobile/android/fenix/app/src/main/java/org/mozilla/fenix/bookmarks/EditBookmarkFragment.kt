/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.bookmarks

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.compose.ui.platform.ComposeView
import androidx.compose.ui.platform.ViewCompositionStrategy
import androidx.core.content.getSystemService
import androidx.fragment.app.Fragment
import androidx.lifecycle.lifecycleScope
import androidx.navigation.NavHostController
import androidx.navigation.fragment.findNavController
import androidx.navigation.fragment.navArgs
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarState
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarStore
import mozilla.components.compose.browser.toolbar.store.Mode
import mozilla.components.lib.state.helpers.StoreProvider.Companion.fragmentStore
import org.mozilla.fenix.R
import org.mozilla.fenix.components.accounts.FenixFxAEntryPoint
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.ext.bookmarkStorage
import org.mozilla.fenix.ext.nav
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.search.SearchFragmentState
import org.mozilla.fenix.search.SearchFragmentStore
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.utils.lastSavedFolderCache

/**
 * Menu to edit the name, URL, and location of a bookmark item.
 */
class EditBookmarkFragment : Fragment(R.layout.fragment_edit_bookmark) {

    private val args by navArgs<EditBookmarkFragmentArgs>()

    @Suppress("LongMethod")
    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View? {
        return ComposeView(requireContext()).apply {
                setViewCompositionStrategy(ViewCompositionStrategy.DisposeOnViewTreeLifecycleDestroyed)
                val buildStore = { composeNavController: NavHostController ->
                    val appStore = requireComponents.appStore
                    val navController = findNavController()
                    val isSignedIntoSync = requireComponents
                        .backgroundServices.accountManager.authenticatedAccount() != null

                    val store by fragmentStore(
                        BookmarksState.default.copy(
                            isSignedIntoSync = isSignedIntoSync,
                        ),
                    ) {
                        BookmarksStore(
                            initialState = it,
                            middleware = listOf(
                                BookmarksMiddleware(
                                    bookmarksStorage = requireContext().bookmarkStorage,
                                    clipboardManager = requireContext().getSystemService(),
                                    addNewTabUseCase = requireComponents.useCases.tabsUseCases.addTab,
                                    fenixBrowserUseCases = requireComponents.useCases.fenixBrowserUseCases,
                                    useNewSearchUX = settings().shouldUseComposableToolbar,
                                    openBookmarksInNewTab = if (settings().enableHomepageAsNewTab) {
                                        false
                                    } else {
                                        appStore.state.mode.isPrivate
                                    },
                                    getNavController = { composeNavController },
                                    exitBookmarks = { navController.popBackStack() },
                                    navigateToBrowser = {
                                        navController.navigate(R.id.browserFragment)
                                    },
                                    navigateToSearch = { },
                                    navigateToSignIntoSync = {
                                        navController
                                            .navigate(
                                                BookmarkFragmentDirections.actionGlobalTurnOnSync(
                                                    entrypoint = FenixFxAEntryPoint.BookmarkView,
                                                ),
                                            )
                                    },
                                    shareBookmarks = { bookmarks ->
                                        navController.nav(
                                            R.id.bookmarkFragment,
                                            BookmarkFragmentDirections.actionGlobalShareFragment(
                                                data = bookmarks.asShareDataArray(),
                                            ),
                                        )
                                    },
                                    showTabsTray = { },
                                    resolveFolderTitle = {
                                        friendlyRootTitle(
                                            context = context,
                                            node = it,
                                            rootTitles = composeRootTitles(context),
                                        ) ?: ""
                                    },
                                    getBrowsingMode = {
                                        appStore.state.mode
                                    },
                                    lastSavedFolderCache = context.settings().lastSavedFolderCache,
                                    saveBookmarkSortOrder = {},
                                    reportResultGlobally = {
                                        requireComponents.appStore.dispatch(
                                            AppAction.BookmarkAction.BookmarkOperationResultReported(it),
                                        )
                                    },
                                    lifecycleScope = lifecycleScope,
                                ),
                            ),
                            bookmarkToLoad = args.guidToEdit,
                        )
                    }

                    store
                }
                setContent {
                    FirefoxTheme {
                        BookmarksScreen(
                            buildStore = buildStore,
                            startDestination = BookmarksDestinations.EDIT_BOOKMARK,
                            toolbarStore = BrowserToolbarStore(BrowserToolbarState(mode = Mode.EDIT)),
                            searchStore = SearchFragmentStore(SearchFragmentState.EMPTY),
                            bookmarksSearchEngine = null,
                        )
                    }
                }
            }
    }

    override fun onDestroyView() {
        super.onDestroyView()
        activity?.title = getString(R.string.app_name)
    }
}
