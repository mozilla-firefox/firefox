/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.library.history

import android.annotation.SuppressLint
import android.app.Dialog
import android.content.DialogInterface
import android.content.Intent
import android.os.Bundle
import android.text.SpannableString
import android.view.LayoutInflater
import android.view.Menu
import android.view.MenuInflater
import android.view.MenuItem
import android.view.View
import android.view.ViewGroup
import android.widget.RadioGroup
import androidx.activity.compose.LocalActivity
import androidx.activity.result.ActivityResultLauncher
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.isImeVisible
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusManager
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.ComposeView
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.platform.SoftwareKeyboardController
import androidx.compose.ui.platform.ViewCompositionStrategy
import androidx.core.content.ContextCompat
import androidx.core.graphics.toColorInt
import androidx.core.view.MenuProvider
import androidx.core.view.isVisible
import androidx.core.view.updateLayoutParams
import androidx.fragment.app.DialogFragment
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.coroutineScope
import androidx.lifecycle.lifecycleScope
import androidx.navigation.NavDirections
import androidx.navigation.NavOptions
import androidx.navigation.fragment.findNavController
import androidx.paging.Pager
import androidx.paging.PagingConfig
import androidx.paging.PagingData
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Dispatchers.IO
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.mapNotNull
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import mozilla.components.browser.state.action.AwesomeBarAction
import mozilla.components.browser.state.action.AwesomeBarAction.EngagementFinished
import mozilla.components.browser.state.action.EngineAction
import mozilla.components.browser.state.action.HistoryMetadataAction
import mozilla.components.browser.state.action.RecentlyClosedAction
import mozilla.components.browser.state.state.searchEngines
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.browser.storage.sync.PlacesHistoryStorage
import mozilla.components.compose.base.utils.BackInvokedHandler
import mozilla.components.compose.browser.awesomebar.AwesomeBar
import mozilla.components.compose.browser.awesomebar.AwesomeBarDefaults
import mozilla.components.compose.browser.awesomebar.AwesomeBarOrientation
import mozilla.components.compose.browser.toolbar.BrowserToolbar
import mozilla.components.compose.browser.toolbar.store.BrowserEditToolbarAction
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarState
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarStore
import mozilla.components.compose.browser.toolbar.store.Mode
import mozilla.components.compose.browser.toolbar.ui.BrowserToolbarQuery
import mozilla.components.concept.engine.prompt.ShareData
import mozilla.components.lib.state.ext.consumeFrom
import mozilla.components.lib.state.ext.flowScoped
import mozilla.components.lib.state.ext.observeAsComposableState
import mozilla.components.lib.state.helpers.StoreProvider.Companion.fragmentStore
import mozilla.components.support.base.feature.UserInteractionHandler
import mozilla.components.support.base.feature.ViewBoundFeatureWrapper
import mozilla.components.support.ktx.android.view.hideKeyboard
import mozilla.components.support.ktx.kotlin.toShortUrl
import mozilla.components.ui.widgets.withCenterAlignedButtons
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.NavHostActivity
import org.mozilla.fenix.R
import org.mozilla.fenix.addons.showSnackBar
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.LensFeature
import org.mozilla.fenix.components.QrScanFenixFeature
import org.mozilla.fenix.components.VoiceSearchFeature
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.history.DefaultPagedHistoryProvider
import org.mozilla.fenix.components.metrics.MetricsUtils
import org.mozilla.fenix.components.search.HISTORY_SEARCH_ENGINE_ID
import org.mozilla.fenix.databinding.FragmentHistoryBinding
import org.mozilla.fenix.e2e.SystemInsetsPaddedFragment
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.getRootView
import org.mozilla.fenix.ext.nav
import org.mozilla.fenix.ext.pixelSizeFor
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.ext.runIfFragmentIsAttached
import org.mozilla.fenix.ext.setTextColor
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.library.LibraryPageFragment
import org.mozilla.fenix.library.history.HistoryFragmentAction.SearchClicked
import org.mozilla.fenix.library.history.HistoryFragmentAction.SearchDismissed
import org.mozilla.fenix.library.history.state.HistoryTelemetryMiddleware
import org.mozilla.fenix.library.history.state.bindings.MenuBinding
import org.mozilla.fenix.library.history.state.bindings.PendingDeletionBinding
import org.mozilla.fenix.pbmlock.registerForVerification
import org.mozilla.fenix.pbmlock.verifyUser
import org.mozilla.fenix.search.BrowserStoreToFenixSearchMapperMiddleware
import org.mozilla.fenix.search.BrowserToolbarSearchMiddleware
import org.mozilla.fenix.search.BrowserToolbarSearchStatusSyncMiddleware
import org.mozilla.fenix.search.BrowserToolbarToFenixSearchMapperMiddleware
import org.mozilla.fenix.search.FenixSearchMiddleware
import org.mozilla.fenix.search.SearchFragmentAction.SuggestionClicked
import org.mozilla.fenix.search.SearchFragmentAction.SuggestionSelected
import org.mozilla.fenix.search.SearchFragmentStore
import org.mozilla.fenix.search.awesomebar.DeleteHistoryEntryDelegate
import org.mozilla.fenix.search.createInitialSearchFragmentState
import org.mozilla.fenix.tabstray.redux.state.Page
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.utils.allowUndo
import androidx.appcompat.R as appcompatR
import org.mozilla.fenix.GleanMetrics.History as GleanHistory

private const val MATERIAL_DESIGN_SCRIM = "#52000000"

/**
 * Browser history screen.
 */
@SuppressWarnings("TooManyFunctions", "LargeClass")
class HistoryFragment :
    LibraryPageFragment<History>(),
    UserInteractionHandler,
    MenuProvider,
    SystemInsetsPaddedFragment {
    private lateinit var historyStore: HistoryFragmentStore
    private lateinit var searchStore: SearchFragmentStore
    private val toolbarStore by buildToolbarStore()

    private lateinit var historyProvider: DefaultPagedHistoryProvider

    private var deleteHistory: MenuItem? = null

    private var history: Flow<PagingData<History>> = Pager(
        PagingConfig(PAGE_SIZE),
        null,
    ) {
        HistoryDataSource(
            historyProvider = historyProvider,
        )
    }.flow

    private var _historyView: HistoryView? = null
    private val historyView: HistoryView
        get() = _historyView!!
    private var _binding: FragmentHistoryBinding? = null
    private val binding get() = _binding!!
    private var searchLayout: ComposeView? = null

    private val pendingDeletionBinding = ViewBoundFeatureWrapper<PendingDeletionBinding>()

    private var verificationResultLauncher: ActivityResultLauncher<Intent> =
        registerForVerification(onVerified = ::openHistoryInPrivate)
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
            lensFeature?.get()?.handleImageResult(result.resultCode, result.data)
        }
    private val lensCameraPermissionLauncher: ActivityResultLauncher<String> =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { isGranted ->
            lensFeature?.get()?.onCameraPermissionResult(isGranted)
        }

    private val menuBinding by lazy {
        MenuBinding(
            store = historyStore,
            invalidateOptionsMenu = { activity?.invalidateOptionsMenu() },
        )
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View {
        _binding = FragmentHistoryBinding.inflate(inflater, container, false)
        val view = binding.root
        historyStore = fragmentStore(HistoryFragmentState.initial) {
            HistoryFragmentStore(
                initialState = it,
                middleware = listOf(
                    HistoryTelemetryMiddleware(
                        isInPrivateMode = requireComponents.appStore.state.mode == BrowsingMode.Private,
                    ),
                ),
            )
        }.value
        searchStore = buildSearchStore(toolbarStore).value

        _historyView = HistoryView(
            container = binding.historyLayout,
            onZeroItemsLoaded = {
                historyStore.dispatch(
                    HistoryFragmentAction.ChangeEmptyState(isEmpty = true),
                )
            },
            store = historyStore,
            onEmptyStateChanged = {
                historyStore.dispatch(
                    HistoryFragmentAction.ChangeEmptyState(it),
                )
            },
            onRecentlyClosedClicked = ::navigateToRecentlyClosed,
            onHistoryItemClicked = ::openItem,
            onDeleteInitiated = ::onDeleteInitiated,
            accountManager = requireContext().components.backgroundServices.accountManager,
            scope = lifecycleScope,
        )

        return view
    }

    /**
     * All the current selected items. Individual history entries and entries from a group.
     * When a history group is selected, this will instead contain all the history entries in that group.
     */
    override val selectedItems
        get() = historyStore.state.mode.selectedItems.fold(emptyList<History>()) { accumulator, item ->
            when (item) {
                is History.Group -> accumulator + item.items
                else -> accumulator + item
            }
        }.toSet()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        historyProvider = DefaultPagedHistoryProvider(requireComponents.core.historyStorage)

        GleanHistory.opened.record(NoExtras())
    }

    private fun showDeleteSnackbar(
        items: Set<History>,
    ) {
        val appStore = requireComponents.appStore
        val browserStore = requireComponents.core.store
        val historyStorage = requireComponents.core.historyStorage

        GleanHistory.deleteSnackbarShown.record(NoExtras())

        CoroutineScope(Dispatchers.Main).allowUndo(
            view = requireActivity().getRootView()!!,
            message = getMultiSelectSnackBarMessage(items),
            undoActionTitle = getString(R.string.snackbar_deleted_undo),
            onCancel = {
                undo(appStore = appStore, items = items)
                GleanHistory.deleteSnackbarUndoClicked.record(NoExtras())
            },
            operation = {
                delete(
                    browserStore = browserStore,
                    historyStorage = historyStorage,
                    items = items,
                )
            },
        )
    }

    private fun onTimeFrameDeleted() {
        runIfFragmentIsAttached {
            historyView.historyAdapter.refresh()
            showSnackBar(
                binding.root,
                getString(R.string.preferences_delete_browsing_data_snackbar),
            )
        }
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        requireActivity().addMenuProvider(this, viewLifecycleOwner, Lifecycle.State.RESUMED)

        qrScanFenixFeature = QrScanFenixFeature.register(this, qrScanLauncher)
        voiceSearchFeature = VoiceSearchFeature.register(this, voiceSearchLauncher)
        lensFeature = LensFeature.register(this, lensLauncher, lensCameraPermissionLauncher)

        consumeFrom(historyStore) {
            historyView.update(it)
            updateDeleteMenuItemView(!it.isEmpty)
        }

        requireContext().components.appStore.flowScoped(viewLifecycleOwner, Dispatchers.Main) { flow ->
            flow.mapNotNull { state -> state.pendingDeletionHistoryItems }.collect { items ->
                historyStore.dispatch(
                    HistoryFragmentAction.UpdatePendingDeletionItems(pendingDeletionItems = items),
                )
            }
        }

        viewLifecycleOwner.lifecycleScope.launch {
            history.collect {
                historyView.historyAdapter.submitData(it)
            }
        }

        startStateBindings()

        pendingDeletionBinding.set(
            feature = PendingDeletionBinding(requireContext().components.appStore, historyView),
            owner = this,
            view = view,
        )
    }

    private fun startStateBindings() {
        menuBinding.start()
    }

    private fun stopStateBindings() {
        menuBinding.stop()
    }

    private fun updateDeleteMenuItemView(isEnabled: Boolean) {
        val closedTabs = requireContext().components.core.store.state.closedTabs.size
        if (!isEnabled && closedTabs == 0) {
            deleteHistory?.isEnabled = false
            deleteHistory?.icon?.setTint(
                ContextCompat.getColor(requireContext(), R.color.fx_mobile_icon_color_disabled),
            )
        }
    }

    override fun onResume() {
        super.onResume()

        val isSearchActive = context?.components?.appStore?.state?.searchState?.isSearchActive == true

        if (isSearchActive) {
            handleShowingSearchUX()
        } else {
            (activity as NavHostActivity).getSupportActionBarAndInflateIfNecessary().show()
        }
    }

    override fun onCreateMenu(menu: Menu, inflater: MenuInflater) {
        if (historyStore.state.mode is HistoryFragmentState.Mode.Editing) {
            inflater.inflate(R.menu.history_select_multi, menu)
            menu.findItem(R.id.share_history_multi_select)?.isVisible = true
            menu.findItem(R.id.delete_history_multi_select)?.title =
                SpannableString(getString(R.string.bookmark_menu_delete_button)).apply {
                    setTextColor(requireContext(), appcompatR.attr.colorError)
                }
        } else {
            inflater.inflate(R.menu.history_menu, menu)
            deleteHistory = menu.findItem(R.id.history_delete)
            updateDeleteMenuItemView(!historyStore.state.isEmpty)
        }
    }

    override fun onMenuItemSelected(item: MenuItem): Boolean = when (item.itemId) {
        R.id.share_history_multi_select -> {
            val selectedHistory = historyStore.state.mode.selectedItems
            val shareTabs = mutableListOf<ShareData>()

            for (history in selectedHistory) {
                when (history) {
                    is History.Regular -> {
                        shareTabs.add(ShareData(url = history.url, title = history.title))
                    }
                    is History.Group -> {
                        shareTabs.addAll(
                            history.items.map { metadata ->
                                ShareData(url = metadata.url, title = metadata.title)
                            },
                        )
                    }
                    else -> {
                        // no-op, There is no [History.Metadata] in the HistoryFragment.
                    }
                }
            }

            share(shareTabs)
            historyStore.dispatch(HistoryFragmentAction.ExitEditMode)
            true
        }
        R.id.delete_history_multi_select -> {
            with(historyStore) {
                dispatch(HistoryFragmentAction.DeleteItems(state.mode.selectedItems))
                onDeleteInitiated(state.mode.selectedItems)
                dispatch(HistoryFragmentAction.ExitEditMode)
            }
            true
        }
        R.id.open_history_in_new_tabs_multi_select -> {
            openItemsInNewTab { selectedItem ->
                GleanHistory.openedItemsInNewTabs.record(NoExtras())
                (selectedItem as? History.Regular)?.url ?: (selectedItem as? History.Metadata)?.url
            }

            showTabTray()
            historyStore.dispatch(HistoryFragmentAction.ExitEditMode)
            true
        }
        R.id.open_history_in_private_tabs_multi_select -> {
            handleOpenHistoryInPrivateTabsMultiSelectMenuItem()
            true
        }
        R.id.history_search -> {
            historyStore.dispatch(SearchClicked)
            handleShowingSearchUX()
            true
        }
        R.id.history_delete -> {
            DeleteConfirmationDialogFragment(
                onDeleteTimeRange = ::onDeleteTimeRange,
            ).show(childFragmentManager, null)
            true
        }
        // other options are not handled by this menu provider
        else -> false
    }

    @Suppress("LongMethod", "CognitiveComplexMethod")
    @OptIn(ExperimentalLayoutApi::class) // for WindowInsets.isImeVisible
    private fun handleShowingSearchUX() {
        if (searchLayout == null) {
            searchLayout = (binding.composeViewStub.inflate() as? ComposeView)?.apply {
                setViewCompositionStrategy(ViewCompositionStrategy.DisposeOnViewTreeLifecycleDestroyed)

                setContent {
                    FirefoxTheme {
                        val historyState = historyStore.observeAsComposableState { it }.value
                        val searchState = searchStore.observeAsComposableState { it }.value
                        val awesomebarBackground = AwesomeBarDefaults.colors().background
                        val awesomebarScrim by remember(searchState.query.isEmpty()) {
                            derivedStateOf {
                                when (searchState.query.isNotEmpty()) {
                                    true -> awesomebarBackground
                                    else -> Color(MATERIAL_DESIGN_SCRIM.toColorInt())
                                }
                            }
                        }
                        val activity = LocalActivity.current
                        val deleteHistoryDelegate = remember(activity?.getRootView(), searchStore) {
                            activity?.getRootView()?.let {
                                DeleteHistoryEntryDelegate(it, it.context.components, searchStore)
                            }
                        }

                        val view = LocalView.current
                        val focusManager = LocalFocusManager.current
                        val keyboardController = LocalSoftwareKeyboardController.current

                        LaunchedEffect(historyState.isSearching) {
                            if (!historyState.isSearching) {
                                closeSearchUx(searchLayout, focusManager, keyboardController)
                            }
                        }

                        BackInvokedHandler(historyState.isSearching) {
                            historyStore.dispatch(SearchDismissed)
                        }

                        Column {
                            BrowserToolbar(toolbarStore)
                            Box(
                                modifier = Modifier
                                    .background(awesomebarScrim)
                                    .fillMaxSize()
                                    .pointerInput(WindowInsets.isImeVisible) {
                                        detectTapGestures(
                                            // Exit search for any touches in the empty area of the awesomebar
                                            onPress = {
                                                focusManager.clearFocus()
                                                keyboardController?.hide()
                                                historyStore.dispatch(SearchDismissed)
                                            },
                                        )
                                    },
                            ) {
                                if (searchState.shouldShowSearchSuggestions) {
                                    AwesomeBar(
                                        text = searchState.query,
                                        providers = searchState.searchSuggestionsProviders,
                                        hiddenSuggestions = searchState.hiddenSuggestions,
                                        orientation = AwesomeBarOrientation.TOP,
                                        onSuggestionClicked = { suggestion ->
                                            searchStore.dispatch(SuggestionClicked(suggestion))
                                        },
                                        onAutoComplete = { suggestion ->
                                            searchStore.dispatch(SuggestionSelected(suggestion))
                                        },
                                        onRemoveClicked = { suggestion ->
                                            deleteHistoryDelegate?.handleDeletingHistoryEntry(suggestion)
                                        },
                                        onVisibilityStateUpdated = {
                                            requireComponents.core.store.dispatch(
                                                AwesomeBarAction.VisibilityStateUpdated(it),
                                            )
                                        },
                                        onScroll = { view.hideKeyboard() },
                                        profiler = requireComponents.core.engine.profiler,
                                    )
                                }
                            }
                        }
                    }
                }
            }
        }
        val appStore = requireComponents.appStore
        if (!appStore.state.searchState.isSearchActive) {
            val historySearchEngine = requireComponents.core.store.state.search.searchEngines.firstOrNull {
                it.id == HISTORY_SEARCH_ENGINE_ID
            }
            historySearchEngine?.let {
                appStore.dispatch(AppAction.SearchAction.SearchEngineSelected(it, false))
            }
            appStore.dispatch(AppAction.SearchAction.SearchStarted())
        }

        showSearchUx()
    }

    private fun showSearchUx() {
        (activity as? AppCompatActivity)?.supportActionBar?.hide()
        binding.historyLayout.updateLayoutParams {
            (this as? ViewGroup.MarginLayoutParams)?.topMargin =
                pixelSizeFor(R.dimen.composable_browser_toolbar_height)
        }
        searchLayout?.isVisible = true
    }

    private fun closeSearchUx(
        searchLayout: ComposeView?,
        focusManager: FocusManager,
        keyboardController: SoftwareKeyboardController?,
    ) {
        focusManager.clearFocus()
        keyboardController?.hide()

        (activity as NavHostActivity).getSupportActionBarAndInflateIfNecessary().show()
        binding.historyLayout.updateLayoutParams {
            (this as? ViewGroup.MarginLayoutParams)?.topMargin = 0
        }
        searchLayout?.isVisible = false
        requireComponents.appStore.dispatch(AppAction.SearchAction.SearchEnded)
        toolbarStore.dispatch(BrowserEditToolbarAction.SearchQueryUpdated(BrowserToolbarQuery("")))
        requireComponents.core.store.dispatch(EngagementFinished(abandoned = true))
    }

    private fun handleOpenHistoryInPrivateTabsMultiSelectMenuItem() {
        if (requireComponents.appStore.state.isPrivateScreenLocked) {
            verifyUser(
                fallbackVerification = verificationResultLauncher,
                onVerified = {
                    openHistoryInPrivate()
                },
            )
        } else {
            openHistoryInPrivate()
        }
    }

    private fun openHistoryInPrivate() {
        openItemsInNewTab(private = true) { selectedItem ->
            GleanHistory.openedItemsInNewTabs.record(NoExtras())
            (selectedItem as? History.Regular)?.url ?: (selectedItem as? History.Metadata)?.url
        }

        (activity as HomeActivity).apply {
            browsingModeManager.mode = BrowsingMode.Private
            supportActionBar?.hide()
        }

        showTabTray(openInPrivate = true)
        historyStore.dispatch(HistoryFragmentAction.ExitEditMode)
    }

    private fun showTabTray(openInPrivate: Boolean = false) {
        findNavController().nav(
            R.id.historyFragment,
            HistoryFragmentDirections.actionGlobalTabManagementFragment(
                page = if (openInPrivate) {
                    Page.PrivateTabs
                } else {
                    Page.NormalTabs
                },
            ),
        )
    }

    private fun getMultiSelectSnackBarMessage(historyItems: Set<History>): String {
        return if (historyItems.size > 1) {
            getString(R.string.history_delete_multiple_items_snackbar)
        } else {
            val historyItem = historyItems.first()

            String.format(
                requireContext().getString(R.string.history_delete_single_item_snackbar),
                if (historyItem is History.Regular) {
                    historyItem.url.toShortUrl(requireComponents.publicSuffixList)
                } else {
                    historyItem.title
                },
            )
        }
    }

    override fun onBackPressed(): Boolean {
        // The state needs to be updated accordingly if Edit mode is active
        return if (historyStore.state.mode is HistoryFragmentState.Mode.Editing) {
            historyStore.dispatch(HistoryFragmentAction.BackPressed)
            true
        } else {
            false
        }
    }

    override fun onDestroyView() {
        super.onDestroyView()
        stopStateBindings()
        _historyView = null
        _binding = null
    }

    private fun openItem(item: History) {
        when (item) {
            is History.Regular -> openRegularItem(item)
            is History.Group -> {
                findNavController().navigate(
                    HistoryFragmentDirections.actionGlobalHistoryMetadataGroup(
                        title = item.title,
                        historyMetadataItems = item.items.toTypedArray(),
                    ),
                    NavOptions.Builder()
                        .setPopUpTo(R.id.historyMetadataGroupFragment, true)
                        .build(),
                )
            }
            else -> Unit
        }
    }

    private fun openRegularItem(item: History.Regular) = runIfFragmentIsAttached {
        requireComponents.useCases.fenixBrowserUseCases.loadUrlOrSearch(
            searchTermOrURL = item.url,
            newTab = requireComponents.settings.enableHomepageAsNewTab.not(),
            private = (requireActivity() as HomeActivity).browsingModeManager.mode.isPrivate,
        )
        findNavController().navigate(R.id.browserFragment)
    }

    private fun onDeleteInitiated(items: Set<History>) {
        val appStore = requireContext().components.appStore

        appStore.dispatch(AppAction.AddPendingDeletionSet(items.toPendingDeletionHistory()))
        showDeleteSnackbar(items)
    }

    private fun share(data: List<ShareData>) {
        GleanHistory.shared.record(NoExtras())
        val directions = HistoryFragmentDirections.actionGlobalShareFragment(
            data = data.toTypedArray(),
        )
        navigateToHistoryFragment(directions)
    }

    private fun navigateToHistoryFragment(directions: NavDirections) {
        findNavController().nav(
            R.id.historyFragment,
            directions,
        )
    }

    private fun navigateToRecentlyClosed() {
        findNavController().navigate(
            HistoryFragmentDirections.actionGlobalRecentlyClosed(),
            NavOptions.Builder().setPopUpTo(R.id.recentlyClosedFragment, true).build(),
        )
    }

    private suspend fun undo(appStore: AppStore, items: Set<History>) = withContext(IO) {
        val pendingDeletionItems = items.map { it.toPendingDeletionHistory() }.toSet()
        appStore.dispatch(AppAction.UndoPendingDeletionSet(pendingDeletionItems))
    }

    private suspend fun delete(
        browserStore: BrowserStore,
        historyStorage: PlacesHistoryStorage,
        items: Set<History>,
    ) = withContext(IO) {
        historyStore.dispatch(HistoryFragmentAction.EnterDeletionMode)
        for (item in items) {
            when (item) {
                is History.Regular -> historyStorage.deleteVisitsFor(item.url)
                is History.Group -> {
                    // NB: If we have non-search groups, this logic needs to be updated.
                    historyProvider.deleteMetadataSearchGroup(item)
                    browserStore.dispatch(
                        HistoryMetadataAction.DisbandSearchGroupAction(searchTerm = item.title),
                    )
                }
                // We won't encounter individual metadata entries outside of groups.
                is History.Metadata -> {}
            }
        }
        historyStore.dispatch(HistoryFragmentAction.ExitDeletionMode)
    }

    private fun onDeleteTimeRange(selectedTimeFrame: RemoveTimeFrame?) {
        historyStore.dispatch(HistoryFragmentAction.DeleteTimeRange(selectedTimeFrame))
        historyStore.dispatch(HistoryFragmentAction.EnterDeletionMode)

        val browserStore = requireComponents.core.store
        val historyStorage = requireContext().components.core.historyStorage
        lifecycleScope.launch {
            if (selectedTimeFrame == null) {
                historyStorage.deleteEverything()
            } else {
                val longRange = selectedTimeFrame.toLongRange()
                historyStorage.deleteVisitsBetween(
                    startTime = longRange.first,
                    endTime = longRange.last,
                )
            }
            browserStore.dispatch(RecentlyClosedAction.RemoveAllClosedTabAction)
            browserStore.dispatch(EngineAction.PurgeHistoryAction)

            historyStore.dispatch(HistoryFragmentAction.ExitDeletionMode)

            onTimeFrameDeleted()
        }
    }

    internal class DeleteConfirmationDialogFragment(
        private val onDeleteTimeRange: (selectedTimeFrame: RemoveTimeFrame?) -> Unit,
    ) : DialogFragment() {
        @SuppressLint("InflateParams")
        override fun onCreateDialog(savedInstanceState: Bundle?): Dialog =
            MaterialAlertDialogBuilder(requireContext()).apply {
                val layout = getLayoutInflater().inflate(R.layout.delete_history_time_range_dialog, null)
                val radioGroup = layout.findViewById<RadioGroup>(R.id.radio_group)
                radioGroup.check(R.id.last_hour_button)
                setView(layout)

                setNegativeButton(R.string.delete_browsing_data_prompt_cancel) { dialog: DialogInterface, _ ->
                    GleanHistory.removePromptCancelled.record(NoExtras())
                    dialog.cancel()
                }
                setPositiveButton(R.string.delete_browsing_data_prompt_allow) { dialog: DialogInterface, _ ->
                    val selectedTimeFrame = when (radioGroup.checkedRadioButtonId) {
                        R.id.last_hour_button -> RemoveTimeFrame.LastHour
                        R.id.today_and_yesterday_button -> RemoveTimeFrame.TodayAndYesterday
                        R.id.everything_button -> null
                        else -> throw IllegalStateException("Unexpected radioButtonId")
                    }
                    onDeleteTimeRange(selectedTimeFrame)
                    dialog.dismiss()
                }

                GleanHistory.removePromptOpened.record(NoExtras())
            }.create().withCenterAlignedButtons()
    }

    private fun buildToolbarStore() = fragmentStore(
        BrowserToolbarState(mode = Mode.EDIT),
    ) {
        val lifecycleScope = viewLifecycleOwner.lifecycle.coroutineScope

        BrowserToolbarStore(
            initialState = it,
            middleware = listOf(
                BrowserToolbarSyncToHistoryMiddleware(historyStore),
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
    }

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
                    fragment = this@HistoryFragment,
                    engine = requireComponents.core.engine,
                    useCases = requireComponents.useCases,
                    nimbusComponents = requireComponents.nimbus,
                    settings = requireComponents.settings,
                    appStore = requireComponents.appStore,
                    browserStore = requireComponents.core.store,
                    toolbarStore = toolbarStore,
                    navController = findNavController(),
                    browsingModeManager = (requireActivity() as HomeActivity).browsingModeManager,
                ),
            ),
        )
    }

    companion object {
        private const val PAGE_SIZE = 25
    }
}
