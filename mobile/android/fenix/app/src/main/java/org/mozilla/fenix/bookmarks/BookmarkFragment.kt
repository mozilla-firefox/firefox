/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.bookmarks

import android.content.Intent
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.activity.result.ActivityResultLauncher
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.ui.platform.ComposeView
import androidx.compose.ui.platform.ViewCompositionStrategy
import androidx.core.content.getSystemService
import androidx.fragment.app.Fragment
import androidx.lifecycle.coroutineScope
import androidx.lifecycle.lifecycleScope
import androidx.navigation.NavDirections
import androidx.navigation.NavHostController
import androidx.navigation.fragment.findNavController
import mozilla.components.browser.state.state.searchEngines
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarState
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarStore
import mozilla.components.compose.browser.toolbar.store.Mode
import mozilla.components.lib.state.helpers.StoreProvider.Companion.fragmentStore
import mozilla.components.support.base.feature.ViewBoundFeatureWrapper
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.NavGraphDirections
import org.mozilla.fenix.R
import org.mozilla.fenix.components.QrScanFenixFeature
import org.mozilla.fenix.components.VoiceSearchFeature
import org.mozilla.fenix.components.accounts.FenixFxAEntryPoint
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.metrics.MetricsUtils
import org.mozilla.fenix.components.search.BOOKMARKS_SEARCH_ENGINE_ID
import org.mozilla.fenix.ext.bookmarkStorage
import org.mozilla.fenix.ext.hideToolbar
import org.mozilla.fenix.ext.nav
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.pbmlock.registerForVerification
import org.mozilla.fenix.pbmlock.verifyUser
import org.mozilla.fenix.search.BrowserStoreToFenixSearchMapperMiddleware
import org.mozilla.fenix.search.BrowserToolbarSearchMiddleware
import org.mozilla.fenix.search.BrowserToolbarSearchStatusSyncMiddleware
import org.mozilla.fenix.search.BrowserToolbarToFenixSearchMapperMiddleware
import org.mozilla.fenix.search.FenixSearchMiddleware
import org.mozilla.fenix.search.SearchFragmentState
import org.mozilla.fenix.search.SearchFragmentStore
import org.mozilla.fenix.search.createInitialSearchFragmentState
import org.mozilla.fenix.tabstray.Page
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.utils.lastSavedFolderCache

/**
 * The screen that displays the user's bookmark list in their Library.
 */
@Suppress("TooManyFunctions", "LargeClass")
class BookmarkFragment : Fragment() {

    private val verificationResultLauncher = registerForVerification()
    private var qrScanFenixFeature: ViewBoundFeatureWrapper<QrScanFenixFeature>? =
        ViewBoundFeatureWrapper<QrScanFenixFeature>()
    private val qrScanLauncher: ActivityResultLauncher<Intent> =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
            qrScanFenixFeature?.get()?.handleToolbarQrScanResults(result.resultCode, result.data)
        }
    private var voiceSearchFeature: ViewBoundFeatureWrapper<VoiceSearchFeature>? =
        ViewBoundFeatureWrapper<VoiceSearchFeature>()
    private val voiceSearchLauncher: ActivityResultLauncher<Intent> =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
            voiceSearchFeature?.get()?.handleVoiceSearchResult(result.resultCode, result.data)
        }

    @Suppress("LongMethod")
    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View {
        return ComposeView(requireContext()).apply {
                setViewCompositionStrategy(ViewCompositionStrategy.DisposeOnViewTreeLifecycleDestroyed)
                val toolbarStore = buildToolbarStore()
                val searchStore = buildSearchStore(toolbarStore)
                val buildStore = { composeNavController: NavHostController ->
                    val appStore = requireComponents.appStore
                    val navController = this@BookmarkFragment.findNavController()

                    val store by fragmentStore(
                        BookmarksState.default.copy(
                            sortOrder = BookmarksListSortOrder.fromString(
                                value = requireContext().settings().bookmarkListSortOrder,
                                default = BookmarksListSortOrder.Alphabetical(true),
                            ),
                        ),
                    ) {
                        BookmarksStore(
                            initialState = it,
                            middleware = listOf(
                                // NB: Order matters — this middleware must be first to intercept actions
                                // related to private mode and trigger verification before any other middleware runs.
                                PrivateBrowsingLockMiddleware(
                                    appStore = requireComponents.appStore,
                                    requireAuth = {
                                        verifyUser(fallbackVerification = verificationResultLauncher)
                                    },
                                ),
                                BookmarksTelemetryMiddleware(),
                                BookmarksSyncMiddleware(requireComponents.backgroundServices.syncStore, lifecycleScope),
                                BrowserToolbarSyncToBookmarksMiddleware(toolbarStore, lifecycleScope),
                                BookmarksMiddleware(
                                    lifecycleScope = lifecycleScope,
                                    bookmarksStorage = requireContext().bookmarkStorage,
                                    clipboardManager = requireActivity().getSystemService(),
                                    addNewTabUseCase = requireComponents.useCases.tabsUseCases.addTab,
                                    fenixBrowserUseCases = requireComponents.useCases.fenixBrowserUseCases,
                                    useNewSearchUX = settings().shouldUseComposableToolbar,
                                    openBookmarksInNewTab = if (settings().enableHomepageAsNewTab) {
                                        false
                                    } else {
                                            navController
                                                .previousBackStackEntry?.destination?.id == R.id.homeFragment
                                    },
                                    getNavController = { composeNavController },
                                    exitBookmarks = { navController.popBackStack() },
                                    navigateToBrowser = {
                                        navController.navigate(R.id.browserFragment)
                                    },
                                    navigateToSearch = {
                                        navController.navigate(
                                            NavGraphDirections.actionGlobalSearchDialog(sessionId = null),
                                        )
                                    },
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
                                    showTabsTray = ::showTabTray,
                                    resolveFolderTitle = {
                                        friendlyRootTitle(
                                            context = requireContext(),
                                            node = it,
                                            rootTitles = composeRootTitles(requireContext()),
                                        ) ?: ""
                                    },
                                    getBrowsingMode = {
                                        appStore.state.mode
                                    },
                                    saveBookmarkSortOrder = {
                                        requireContext().settings().bookmarkListSortOrder =
                                            it.asString
                                    },
                                    lastSavedFolderCache = requireContext().settings().lastSavedFolderCache,
                                    reportResultGlobally = {
                                        requireComponents.appStore.dispatch(
                                            AppAction.BookmarkAction.BookmarkOperationResultReported(it),
                                        )
                                    },
                                ),
                            ),
                        )
                    }

                    store
                }
                setContent {
                    FirefoxTheme {
                        BookmarksScreen(
                            buildStore = buildStore,
                            appStore = requireComponents.appStore,
                            toolbarStore = toolbarStore,
                            searchStore = searchStore,
                            bookmarksSearchEngine = requireComponents.core.store.state.search.searchEngines
                                .firstOrNull { it.id == BOOKMARKS_SEARCH_ENGINE_ID },
                            useNewSearchUX = settings().shouldUseComposableToolbar,
                        )
                    }
                }
            }
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        if (requireContext().settings().shouldUseComposableToolbar) {
            qrScanFenixFeature = QrScanFenixFeature.register(this, qrScanLauncher)
            voiceSearchFeature = VoiceSearchFeature.register(this, voiceSearchLauncher)
        }
    }

    private fun buildToolbarStore() = when (requireComponents.settings.shouldUseComposableToolbar) {
        false -> {
            // Default empty store. This is not used without the composable toolbar.
            BrowserToolbarStore(BrowserToolbarState(mode = Mode.EDIT))
        }
        else -> fragmentStore(BrowserToolbarState(mode = Mode.EDIT)) {
            val lifecycleScope = viewLifecycleOwner.lifecycle.coroutineScope

            BrowserToolbarStore(
                initialState = it,
                middleware = listOf(
                    BrowserToolbarSearchStatusSyncMiddleware(
                        appStore = requireComponents.appStore,
                        browsingModeManager = (requireActivity() as HomeActivity).browsingModeManager,
                        scope = lifecycleScope,
                    ),
                    BrowserToolbarSearchMiddleware(
                        uiContext = requireActivity(),
                        appStore = requireComponents.appStore,
                        browserStore = requireComponents.core.store,
                        components = requireComponents,
                        navController = findNavController(),
                        browsingModeManager = (requireActivity() as HomeActivity).browsingModeManager,
                        settings = requireComponents.settings,
                        scope = lifecycleScope,
                    ),
                ),
            )
        }.value
    }

    private fun buildSearchStore(
        toolbarStore: BrowserToolbarStore,
    ) = when (requireComponents.settings.shouldUseComposableToolbar) {
        false -> {
            // Default empty store. This is not used without the composable toolbar.
            SearchFragmentStore(SearchFragmentState.EMPTY)
        }
        else -> fragmentStore(
            createInitialSearchFragmentState(
                context = requireContext(),
                components = requireComponents,
                tabId = null,
                pastedText = null,
                searchAccessPoint = MetricsUtils.Source.NONE,
            ),
        ) {
            val lifecycleScope = viewLifecycleOwner.lifecycle.coroutineScope

            SearchFragmentStore(
                initialState = it,
                middleware = listOf(
                    BrowserToolbarToFenixSearchMapperMiddleware(
                        toolbarStore = toolbarStore,
                        browsingModeManager = (requireActivity() as HomeActivity).browsingModeManager,
                        scope = lifecycleScope,
                    ),
                    BrowserStoreToFenixSearchMapperMiddleware(
                        browserStore = requireComponents.core.store,
                        scope = lifecycleScope,
                    ),
                    FenixSearchMiddleware(
                        fragment = this@BookmarkFragment,
                        engine = requireComponents.core.engine,
                        useCases = requireComponents.useCases,
                        nimbusComponents = requireComponents.nimbus,
                        settings = requireComponents.settings,
                        appStore = requireComponents.appStore,
                        browserStore = requireComponents.core.store,
                        toolbarStore = toolbarStore,
                        navController = this@BookmarkFragment.findNavController(),
                        browsingModeManager = (requireActivity() as HomeActivity).browsingModeManager,
                    ),
                ),
            )
        }.value
    }

    override fun onResume() {
        super.onResume()
        hideToolbar()
    }

    private fun showTabTray(openInPrivate: Boolean = false) {
        val directions = if (requireContext().settings().tabManagerEnhancementsEnabled) {
            BookmarkFragmentDirections.actionGlobalTabManagementFragment(
                page = if (openInPrivate) {
                    Page.PrivateTabs
                } else {
                    Page.NormalTabs
                },
            )
        } else {
            BookmarkFragmentDirections.actionGlobalTabsTrayFragment(
                page = if (openInPrivate) {
                    Page.PrivateTabs
                } else {
                    Page.NormalTabs
                },
            )
        }
        navigateToBookmarkFragment(directions = directions)
    }

    private fun navigateToBookmarkFragment(directions: NavDirections) {
        findNavController().nav(
            R.id.bookmarkFragment,
            directions,
        )
    }
}
