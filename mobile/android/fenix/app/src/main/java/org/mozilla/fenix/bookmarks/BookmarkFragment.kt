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
import androidx.fragment.app.Fragment
import androidx.lifecycle.coroutineScope
import androidx.lifecycle.lifecycleScope
import androidx.navigation.NavDirections
import androidx.navigation.NavHostController
import androidx.navigation.fragment.findNavController
import kotlinx.coroutines.flow.MutableSharedFlow
import mozilla.components.browser.state.state.searchEngines
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarState
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarStore
import mozilla.components.compose.browser.toolbar.store.Mode
import mozilla.components.feature.importer.ImporterResult
import mozilla.components.lib.state.helpers.StoreProvider.Companion.fragmentStore
import mozilla.components.support.base.feature.ViewBoundFeatureWrapper
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.R
import org.mozilla.fenix.components.LensFeature
import org.mozilla.fenix.components.QrScanFenixFeature
import org.mozilla.fenix.components.VoiceSearchFeature
import org.mozilla.fenix.components.accounts.FenixFxAEntryPoint
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.metrics.MetricsUtils
import org.mozilla.fenix.components.search.BOOKMARKS_SEARCH_ENGINE_ID
import org.mozilla.fenix.components.share.ShareSource
import org.mozilla.fenix.e2e.SystemInsetsPaddedFragment
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
import org.mozilla.fenix.search.SearchFragmentStore
import org.mozilla.fenix.search.createInitialSearchFragmentState
import org.mozilla.fenix.tabstray.redux.state.Page
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * The screen that displays the user's bookmark list in their Library.
 */
@Suppress("TooManyFunctions", "LargeClass")
class BookmarkFragment : Fragment(), SystemInsetsPaddedFragment {

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
    private var lensFeature: ViewBoundFeatureWrapper<LensFeature>? =
        ViewBoundFeatureWrapper<LensFeature>()
    private val lensLauncher: ActivityResultLauncher<Intent> =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
            lensFeature?.get()?.handleCameraActivityResult(
                result.resultCode,
                result.data,
                qrScanFenixFeature?.get(),
            )
        }
    private val lensCameraPermissionLauncher: ActivityResultLauncher<String> =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { isGranted ->
            lensFeature?.get()?.onCameraPermissionResult(isGranted)
        }

    private val importResultFlow = MutableSharedFlow<ImporterResult>(extraBufferCapacity = 1)

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
                        showBookmarksImport = requireContext().settings().importBookmarksFeatureFlagEnabled,
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
                            BookmarksSyncMiddleware(
                                requireComponents.backgroundServices.syncStore,
                                lifecycleScope,
                            ),
                            BrowserToolbarSyncToBookmarksMiddleware(toolbarStore, lifecycleScope),
                            BookmarksMiddleware(
                                lifecycleScope = lifecycleScope,
                                bookmarksStorage = requireContext().bookmarkStorage,
                                addNewTabUseCase = requireComponents.useCases.tabsUseCases.addTab,
                                fenixBrowserUseCases = requireComponents.useCases.fenixBrowserUseCases,
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
                                navigateToSignIntoSync = {
                                    navController
                                        .navigate(
                                            BookmarkFragmentDirections.actionGlobalTurnOnSync(
                                                entrypoint = FenixFxAEntryPoint.BookmarkView,
                                            ),
                                        )
                                },
                                navigateToImportDialog = {
                                    ImportBookmarksDialogFragment().show(
                                        childFragmentManager,
                                        ImportBookmarksDialogFragment.TAG,
                                    )
                                },
                                shareBookmarks = { bookmarks ->
                                    requireComponents.useCases.shareUseCases.shareItems(
                                        items = bookmarks.asShareDataArray().toList(),
                                        source = ShareSource.BOOKMARKS,
                                        navigateToShareFragment = {
                                            navController.nav(
                                                R.id.bookmarkFragment,
                                                BookmarkFragmentDirections.actionGlobalShareFragment(
                                                    data = bookmarks.asShareDataArray(),
                                                ),
                                            )
                                        },
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
                                editBookmarkUseCase = requireComponents.useCases.bookmarksUseCases.editBookmark,
                                reportResultGlobally = {
                                    requireComponents.appStore.dispatch(
                                        AppAction.BookmarkAction.BookmarkOperationResultReported(it),
                                    )
                                },
                                importResults = { importResultFlow },
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
                    )
                }
            }
        }
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        qrScanFenixFeature = QrScanFenixFeature.register(this, qrScanLauncher)
        voiceSearchFeature = VoiceSearchFeature.register(this, voiceSearchLauncher)
        lensFeature = LensFeature.register(this, lensLauncher, lensCameraPermissionLauncher)

        childFragmentManager.setFragmentResultListener(
            ImportBookmarksDialogFragment.REQUEST_KEY,
            viewLifecycleOwner,
        ) { _, bundle ->
            ImportBookmarksDialogFragment.decodeResult(bundle)?.let {
                importResultFlow.tryEmit(it)
            }
        }
    }

    private fun buildToolbarStore() = fragmentStore(BrowserToolbarState(mode = Mode.EDIT)) {
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

    private fun buildSearchStore(
        toolbarStore: BrowserToolbarStore,
    ) = fragmentStore(
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
                    appStore = requireComponents.appStore,
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

    override fun onResume() {
        super.onResume()
        hideToolbar()
    }

    private fun showTabTray(openInPrivate: Boolean = false) {
        val directions = BookmarkFragmentDirections.actionGlobalTabManagementFragment(
            page = if (openInPrivate) {
                Page.PrivateTabs
            } else {
                Page.NormalTabs
            },
        )
        navigateToBookmarkFragment(directions = directions)
    }

    private fun navigateToBookmarkFragment(directions: NavDirections) {
        findNavController().nav(
            R.id.bookmarkFragment,
            directions,
        )
    }
}
