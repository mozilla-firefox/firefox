/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.library.bookmarks

import android.content.Context
import android.content.DialogInterface
import android.os.Bundle
import android.text.SpannableString
import android.view.LayoutInflater
import android.view.Menu
import android.view.MenuInflater
import android.view.MenuItem
import android.view.View
import android.view.ViewGroup
import androidx.appcompat.app.AlertDialog
import androidx.compose.ui.platform.ComposeView
import androidx.compose.ui.platform.ViewCompositionStrategy
import androidx.core.content.getSystemService
import androidx.core.view.MenuProvider
import androidx.core.view.isVisible
import androidx.fragment.app.activityViewModels
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.navigation.NavDirections
import androidx.navigation.NavHostController
import androidx.navigation.fragment.findNavController
import androidx.navigation.fragment.navArgs
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers.IO
import kotlinx.coroutines.Dispatchers.Main
import kotlinx.coroutines.MainScope
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitAll
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import mozilla.appservices.places.BookmarkRoot
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.prompt.ShareData
import mozilla.components.concept.storage.BookmarkNode
import mozilla.components.concept.storage.BookmarkNodeType
import mozilla.components.feature.accounts.push.SendTabUseCases
import mozilla.components.lib.state.ext.consumeFrom
import mozilla.components.support.base.feature.UserInteractionHandler
import mozilla.components.support.base.feature.ViewBoundFeatureWrapper
import mozilla.components.support.ktx.kotlin.toShortUrl
import mozilla.components.ui.widgets.withCenterAlignedButtons
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.fenix.BrowserDirection
import org.mozilla.fenix.GleanMetrics.BookmarksManagement
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.NavGraphDirections
import org.mozilla.fenix.NavHostActivity
import org.mozilla.fenix.R
import org.mozilla.fenix.components.StoreProvider
import org.mozilla.fenix.components.accounts.FenixFxAEntryPoint
import org.mozilla.fenix.compose.snackbar.Snackbar
import org.mozilla.fenix.compose.snackbar.SnackbarState
import org.mozilla.fenix.databinding.FragmentBookmarkBinding
import org.mozilla.fenix.ext.bookmarkStorage
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.getRootView
import org.mozilla.fenix.ext.hideToolbar
import org.mozilla.fenix.ext.nav
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.ext.setTextColor
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.library.LibraryPageFragment
import org.mozilla.fenix.library.bookmarks.ui.BookmarksListSortOrder
import org.mozilla.fenix.library.bookmarks.ui.BookmarksMiddleware
import org.mozilla.fenix.library.bookmarks.ui.BookmarksScreen
import org.mozilla.fenix.library.bookmarks.ui.BookmarksState
import org.mozilla.fenix.library.bookmarks.ui.BookmarksStore
import org.mozilla.fenix.library.bookmarks.ui.BookmarksSyncMiddleware
import org.mozilla.fenix.library.bookmarks.ui.BookmarksTelemetryMiddleware
import org.mozilla.fenix.library.bookmarks.ui.LifecycleHolder
import org.mozilla.fenix.snackbar.FenixSnackbarDelegate
import org.mozilla.fenix.snackbar.SnackbarBinding
import org.mozilla.fenix.tabstray.Page
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.utils.allowUndo
import org.mozilla.fenix.utils.lastSavedFolderCache

/**
 * The screen that displays the user's bookmark list in their Library.
 */
@Suppress("TooManyFunctions", "LargeClass")
class BookmarkFragment : LibraryPageFragment<BookmarkNode>(), UserInteractionHandler, MenuProvider {

    private lateinit var bookmarkStore: BookmarkFragmentStore
    private var bookmarkView: BookmarkView? = null
    private lateinit var bookmarkInteractor: BookmarkFragmentInteractor

    private val sharedViewModel: BookmarksSharedViewModel by activityViewModels()
    private val desktopFolders by lazy { DesktopFolders(requireContext(), showMobileRoot = false) }

    private var pendingBookmarksToDelete: MutableSet<BookmarkNode> = mutableSetOf()

    private var _binding: FragmentBookmarkBinding? = null
    private val binding get() = _binding!!
    private val snackbarBinding = ViewBoundFeatureWrapper<SnackbarBinding>()

    override val selectedItems get() = bookmarkStore.state.mode.selectedItems

    @Suppress("LongMethod")
    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View {
        if (requireContext().settings().useNewBookmarks) {
            return ComposeView(requireContext()).apply {
                setViewCompositionStrategy(ViewCompositionStrategy.DisposeOnViewTreeLifecycleDestroyed)
                val buildStore = { navController: NavHostController ->
                    val store = StoreProvider.get(this@BookmarkFragment) {
                        val lifecycleHolder = LifecycleHolder(
                            context = requireContext(),
                            navController = this@BookmarkFragment.findNavController(),
                            composeNavController = navController,
                            homeActivity = (requireActivity() as HomeActivity),
                        )

                        BookmarksStore(
                            initialState = BookmarksState.default.copy(
                                sortOrder = BookmarksListSortOrder.fromString(
                                    value = requireContext().settings().bookmarkListSortOrder,
                                    default = BookmarksListSortOrder.Alphabetical(true),
                                ),
                            ),
                            middleware = listOf(
                                BookmarksTelemetryMiddleware(),
                                BookmarksSyncMiddleware(requireComponents.backgroundServices.syncStore, lifecycleScope),
                                BookmarksMiddleware(
                                    bookmarksStorage = requireContext().bookmarkStorage,
                                    clipboardManager = requireActivity().getSystemService(),
                                    addNewTabUseCase = requireComponents.useCases.tabsUseCases.addTab,
                                    navigateToSignIntoSync = {
                                        lifecycleHolder.navController
                                            .navigate(
                                                BookmarkFragmentDirections.actionGlobalTurnOnSync(
                                                    entrypoint = FenixFxAEntryPoint.BookmarkView,
                                                ),
                                            )
                                    },
                                    getNavController = { lifecycleHolder.composeNavController },
                                    exitBookmarks = { lifecycleHolder.navController.popBackStack() },
                                    wasPreviousAppDestinationHome = {
                                        lifecycleHolder.navController
                                            .previousBackStackEntry?.destination?.id == R.id.homeFragment
                                    },
                                    navigateToSearch = {
                                        lifecycleHolder.navController.navigate(
                                            NavGraphDirections.actionGlobalSearchDialog(sessionId = null),
                                        )
                                    },
                                    shareBookmark = { url, title ->
                                        lifecycleHolder.navController.nav(
                                            R.id.bookmarkFragment,
                                            BookmarkFragmentDirections.actionGlobalShareFragment(
                                                data = arrayOf(
                                                    ShareData(url = url, title = title),
                                                ),
                                            ),
                                        )
                                    },
                                    showTabsTray = ::showTabTray,
                                    resolveFolderTitle = {
                                        friendlyRootTitle(
                                            context = lifecycleHolder.context,
                                            node = it,
                                            rootTitles = composeRootTitles(lifecycleHolder.context),
                                        ) ?: ""
                                    },
                                    showUrlCopiedSnackbar = {
                                        showSnackBarWithText(resources.getString(R.string.url_copied))
                                    },
                                    getBrowsingMode = {
                                        lifecycleHolder.homeActivity.browsingModeManager.mode
                                    },
                                    openTab = { url, openInNewTab ->
                                        lifecycleHolder.homeActivity.openToBrowserAndLoad(
                                            searchTermOrURL = url,
                                            newTab = openInNewTab,
                                            from = BrowserDirection.FromBookmarks,
                                            flags = EngineSession.LoadUrlFlags.select(
                                                EngineSession.LoadUrlFlags.ALLOW_JAVASCRIPT_URL,
                                            ),
                                        )
                                    },
                                    saveBookmarkSortOrder = {
                                        lifecycleHolder.context.settings().bookmarkListSortOrder = it.asString
                                    },
                                    lastSavedFolderCache = context.settings().lastSavedFolderCache,
                                ),
                            ),
                            lifecycleHolder = lifecycleHolder,
                        )
                    }

                    store.lifecycleHolder?.apply {
                        this.navController = this@BookmarkFragment.findNavController()
                        this.composeNavController = navController
                        this.homeActivity = (requireActivity() as HomeActivity)
                        this.context = requireContext()
                    }

                    store
                }
                setContent {
                    FirefoxTheme {
                        BookmarksScreen(buildStore = buildStore)
                    }
                }
            }
        }

        _binding = FragmentBookmarkBinding.inflate(inflater, container, false)

        bookmarkStore = StoreProvider.get(this) {
            BookmarkFragmentStore(BookmarkFragmentState(null))
        }

        bookmarkInteractor = BookmarkFragmentInteractor(
            bookmarksController = DefaultBookmarkController(
                activity = requireActivity() as HomeActivity,
                navController = findNavController(),
                clipboardManager = requireContext().getSystemService(),
                scope = viewLifecycleOwner.lifecycleScope,
                store = bookmarkStore,
                sharedViewModel = sharedViewModel,
                tabsUseCases = activity?.components?.useCases?.tabsUseCases,
                loadBookmarkNode = ::loadBookmarkNode,
                showSnackbar = ::showSnackBarWithText,
                deleteBookmarkNodes = ::deleteMulti,
                deleteBookmarkFolder = ::showRemoveFolderDialog,
                showTabTray = ::showTabTray,
                warnLargeOpenAll = ::warnLargeOpenAll,
            ),
        )

        bookmarkView = BookmarkView(binding.bookmarkLayout, bookmarkInteractor, findNavController())
            .apply {
                binding.bookmarkFoldersSignIn.visibility = View.GONE
            }

        viewLifecycleOwner.lifecycle.addObserver(
            BookmarkDeselectNavigationListener(
                findNavController(),
                sharedViewModel,
                bookmarkInteractor,
            ),
        )

        snackbarBinding.set(
            feature = SnackbarBinding(
                context = requireContext(),
                browserStore = requireContext().components.core.store,
                appStore = requireContext().components.appStore,
                snackbarDelegate = FenixSnackbarDelegate(binding.root),
                navController = findNavController(),
                tabsUseCases = requireContext().components.useCases.tabsUseCases,
                sendTabUseCases = SendTabUseCases(requireComponents.backgroundServices.accountManager),
                customTabSessionId = null,
            ),
            owner = this,
            view = binding.root,
        )

        return binding.root
    }

    private fun showSnackBarWithText(text: String) {
        view?.let {
            Snackbar.make(
                snackBarParentView = it,
                snackbarState = SnackbarState(
                    message = text,
                    duration = SnackbarState.Duration.Preset.Long,
                ),
            ).show()
        }
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        if (requireContext().settings().useNewBookmarks) { return }

        requireActivity().addMenuProvider(this, viewLifecycleOwner, Lifecycle.State.RESUMED)

        val accountManager = requireComponents.backgroundServices.accountManager
        consumeFrom(bookmarkStore) {
            bookmarkView?.update(it)

            // Only display the sign-in prompt if we're inside of the virtual "Desktop Bookmarks" node.
            // Don't want to pester user too much with it, and if there are lots of bookmarks present,
            // it'll just get visually lost. Inside of the "Desktop Bookmarks" node, it'll nicely stand-out,
            // since there are always only three other items in there. It's also the right place contextually.
            bookmarkView?.binding?.bookmarkFoldersSignIn?.isVisible =
                it.tree?.guid == BookmarkRoot.Root.id && accountManager.authenticatedAccount() == null
        }
    }

    override fun onResume() {
        super.onResume()
        if (requireContext().settings().useNewBookmarks) {
            hideToolbar()
            return
        }

        (activity as NavHostActivity).getSupportActionBarAndInflateIfNecessary().show()

        // Reload bookmarks when returning to this fragment in case they have been edited
        val args by navArgs<BookmarkFragmentArgs>()
        val currentGuid = bookmarkStore.state.tree?.guid
            ?: args.currentRoot.ifEmpty {
                BookmarkRoot.Mobile.id
            }
        loadInitialBookmarkFolder(currentGuid)
    }

    private fun loadInitialBookmarkFolder(currentGuid: String) {
        viewLifecycleOwner.lifecycleScope.launch(Main) {
            val currentRoot = loadBookmarkNode(currentGuid)

            if (isActive && currentRoot != null) {
                bookmarkInteractor.onBookmarksChanged(currentRoot)
            }
        }
    }

    override fun onCreateMenu(menu: Menu, inflater: MenuInflater) {
        when (val mode = bookmarkStore.state.mode) {
            is BookmarkFragmentState.Mode.Normal -> {
                if (mode.showMenu) {
                    inflater.inflate(R.menu.bookmarks_menu, menu)
                }
            }
            is BookmarkFragmentState.Mode.Selecting -> {
                if (mode.selectedItems.any { it.type != BookmarkNodeType.ITEM }) {
                    inflater.inflate(R.menu.bookmarks_select_multi_not_item, menu)
                } else {
                    inflater.inflate(R.menu.bookmarks_select_multi, menu)

                    menu.findItem(R.id.delete_bookmarks_multi_select).title =
                        SpannableString(getString(R.string.bookmark_menu_delete_button)).apply {
                            setTextColor(requireContext(), R.attr.textCritical)
                        }
                }
            }
            else -> {
                // no-op
            }
        }
    }

    override fun onMenuItemSelected(item: MenuItem): Boolean {
        return when (item.itemId) {
            R.id.bookmark_search -> {
                bookmarkInteractor.onSearch()
                true
            }
            R.id.close_bookmarks -> {
                close()
                true
            }
            R.id.add_bookmark_folder -> {
                navigateToBookmarkFragment(
                    BookmarkFragmentDirections
                        .actionBookmarkFragmentToBookmarkAddFolderFragment(),
                )
                true
            }
            R.id.open_bookmarks_in_new_tabs_multi_select -> {
                openItemsInNewTab { node -> node.url }

                showTabTray()
                BookmarksManagement.openInNewTabs.record(NoExtras())
                true
            }
            R.id.open_bookmarks_in_private_tabs_multi_select -> {
                openItemsInNewTab(private = true) { node -> node.url }

                showTabTray(openInPrivate = true)
                BookmarksManagement.openInPrivateTabs.record(NoExtras())
                true
            }
            R.id.share_bookmark_multi_select -> {
                val shareTabs = bookmarkStore.state.mode.selectedItems.map {
                    ShareData(url = it.url, title = it.title)
                }
                navigateToBookmarkFragment(
                    BookmarkFragmentDirections.actionGlobalShareFragment(
                        data = shareTabs.toTypedArray(),
                    ),
                )
                true
            }
            R.id.delete_bookmarks_multi_select -> {
                deleteMulti(bookmarkStore.state.mode.selectedItems)
                true
            }
            // other options are not handled by this menu provider
            else -> false
        }
    }

    private fun showTabTray(openInPrivate: Boolean = false) {
        navigateToBookmarkFragment(
            BookmarkFragmentDirections.actionGlobalTabsTrayFragment(
                page = if (openInPrivate) {
                    Page.PrivateTabs
                } else {
                    Page.NormalTabs
                },
            ),
        )
    }

    private fun navigateToBookmarkFragment(directions: NavDirections) {
        findNavController().nav(
            R.id.bookmarkFragment,
            directions,
        )
    }

    override fun onBackPressed(): Boolean {
        if (requireContext().settings().useNewBookmarks) {
            return false
        }
        sharedViewModel.selectedFolder = null
        return bookmarkView?.onBackPressed() ?: false
    }

    private suspend fun loadBookmarkNode(guid: String, recursive: Boolean = false): BookmarkNode? = withContext(IO) {
        // Only runs if the fragment is attached same as [runIfFragmentIsAttached]
        context?.let {
            requireContext().bookmarkStorage
                .getTree(guid, recursive)
                ?.minus(pendingBookmarksToDelete)
                ?.let { desktopFolders.withOptionalDesktopFolders(it) }
        }
    }

    private suspend fun refreshBookmarks() {
        // The bookmark tree in our 'state' can be null - meaning, no bookmark tree has been selected.
        // If that's the case, we don't know what node to refresh, and so we bail out.
        // See https://github.com/mozilla-mobile/fenix/issues/4671
        val currentGuid = bookmarkStore.state.tree?.guid ?: return
        loadBookmarkNode(currentGuid)
            ?.let { node ->
                val rootNode = node - pendingBookmarksToDelete
                bookmarkInteractor.onBookmarksChanged(rootNode)
            }
    }

    private fun warnLargeOpenAll(numberOfTabs: Int, function: () -> (Unit)) {
        AlertDialog.Builder(requireContext()).apply {
            setTitle(String.format(context.getString(R.string.open_all_warning_title), numberOfTabs))
            setMessage(context.getString(R.string.open_all_warning_message, context.getString(R.string.app_name)))
            setPositiveButton(
                R.string.open_all_warning_confirm,
            ) { dialog, _ ->
                function()
                dialog.dismiss()
            }
            setNegativeButton(
                R.string.open_all_warning_cancel,
            ) { dialog: DialogInterface, _ ->
                dialog.dismiss()
            }
            setCancelable(false)
            create().withCenterAlignedButtons()
            show()
        }
    }

    private fun deleteMulti(
        selected: Set<BookmarkNode>,
        eventType: BookmarkRemoveType = BookmarkRemoveType.MULTIPLE,
    ) {
        selected.iterator().forEach {
            if (it.type == BookmarkNodeType.FOLDER) {
                showRemoveFolderDialog(selected)
                return
            }
        }
        updatePendingBookmarksToDelete(selected)

        val message = when (eventType) {
            BookmarkRemoveType.MULTIPLE -> {
                getRemoveBookmarksSnackBarMessage(selected, containsFolders = false)
            }
            BookmarkRemoveType.FOLDER,
            BookmarkRemoveType.SINGLE,
            -> {
                val bookmarkNode = selected.first()
                getString(
                    R.string.bookmark_deletion_snackbar_message,
                    bookmarkNode.url?.toShortUrl(requireContext().components.publicSuffixList)
                        ?: bookmarkNode.title,
                )
            }
        }

        MainScope().allowUndo(
            requireActivity().getRootView()!!,
            message,
            getString(R.string.bookmark_undo_deletion),
            {
                undoPendingDeletion(selected)
            },
            operation = getDeleteOperation(eventType),
        )
    }

    private fun getRemoveBookmarksSnackBarMessage(
        selected: Set<BookmarkNode>,
        containsFolders: Boolean,
    ): String {
        return if (selected.size > 1) {
            return if (containsFolders) {
                getString(R.string.bookmark_deletion_multiple_snackbar_message_3)
            } else {
                getString(R.string.bookmark_deletion_multiple_snackbar_message_2)
            }
        } else {
            val bookmarkNode = selected.first()
            getString(
                R.string.bookmark_deletion_snackbar_message,
                bookmarkNode.url?.toShortUrl(requireContext().components.publicSuffixList)
                    ?: bookmarkNode.title,
            )
        }
    }

    private fun getDialogConfirmationMessage(selected: Set<BookmarkNode>): String {
        return if (selected.size > 1) {
            getString(
                R.string.bookmark_delete_multiple_folders_confirmation_dialog,
                getString(R.string.app_name),
            )
        } else {
            getString(R.string.bookmark_delete_folder_confirmation_dialog)
        }
    }

    override fun onDestroyView() {
        super.onDestroyView()
        bookmarkView?.onDestroy()
        _binding = null
    }

    private fun showRemoveFolderDialog(selected: Set<BookmarkNode>) {
        activity?.let { activity ->
            AlertDialog.Builder(activity).apply {
                val dialogConfirmationMessage = getDialogConfirmationMessage(selected)
                setMessage(dialogConfirmationMessage)
                setNegativeButton(R.string.delete_browsing_data_prompt_cancel) { dialog: DialogInterface, _ ->
                    dialog.cancel()
                }
                setPositiveButton(R.string.delete_browsing_data_prompt_allow) { dialog: DialogInterface, _ ->
                    updatePendingBookmarksToDelete(selected)
                    dialog.dismiss()
                    val snackbarMessage =
                        getRemoveBookmarksSnackBarMessage(selected, containsFolders = true)
                    // Use fragment's lifecycle; the view may be gone by the time dialog is interacted with.
                    MainScope().allowUndo(
                        requireActivity().getRootView()!!,
                        snackbarMessage,
                        getString(R.string.bookmark_undo_deletion),
                        {
                            undoPendingDeletion(selected)
                        },
                        operation = getDeleteOperation(BookmarkRemoveType.FOLDER),
                    )
                }
                create().withCenterAlignedButtons()
            }
                .show()
        }
    }

    private fun updatePendingBookmarksToDelete(selected: Set<BookmarkNode>) {
        pendingBookmarksToDelete.addAll(selected)
        val selectedFolder = sharedViewModel.selectedFolder ?: return
        val bookmarkTree = selectedFolder - pendingBookmarksToDelete
        bookmarkInteractor.onBookmarksChanged(bookmarkTree)
    }

    private suspend fun undoPendingDeletion(selected: Set<BookmarkNode>) {
        pendingBookmarksToDelete.removeAll(selected)
        refreshBookmarks()
    }

    private fun getDeleteOperation(event: BookmarkRemoveType): (suspend (context: Context) -> Unit) {
        return { context ->
            CoroutineScope(IO).launch {
                pendingBookmarksToDelete.map {
                    async { context.bookmarkStorage.deleteNode(it.guid) }
                }.awaitAll()
            }
            when (event) {
                BookmarkRemoveType.FOLDER ->
                    BookmarksManagement.folderRemove.record(NoExtras())
                BookmarkRemoveType.MULTIPLE ->
                    BookmarksManagement.multiRemoved.record(NoExtras())
                BookmarkRemoveType.SINGLE ->
                    BookmarksManagement.removed.record(NoExtras())
            }
            refreshBookmarks()
        }
    }
}
