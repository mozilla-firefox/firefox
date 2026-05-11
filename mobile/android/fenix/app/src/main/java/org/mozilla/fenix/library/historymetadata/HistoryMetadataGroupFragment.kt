/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.library.historymetadata

import android.app.Dialog
import android.content.Context
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
import androidx.activity.result.ActivityResultLauncher
import androidx.core.view.MenuProvider
import androidx.fragment.app.DialogFragment
import androidx.lifecycle.Lifecycle
import androidx.navigation.fragment.findNavController
import androidx.navigation.fragment.navArgs
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.map
import mozilla.components.lib.state.ext.consumeFrom
import mozilla.components.lib.state.ext.flowScoped
import mozilla.components.lib.state.helpers.StoreProvider.Companion.fragmentStore
import mozilla.components.support.base.feature.UserInteractionHandler
import mozilla.components.support.ktx.kotlin.toShortUrl
import mozilla.components.ui.widgets.withCenterAlignedButtons
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.R
import org.mozilla.fenix.addons.showSnackBar
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.databinding.FragmentHistoryMetadataGroupBinding
import org.mozilla.fenix.e2e.SystemInsetsPaddedFragment
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.nav
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.ext.runIfFragmentIsAttached
import org.mozilla.fenix.ext.setTextColor
import org.mozilla.fenix.ext.showToolbar
import org.mozilla.fenix.library.LibraryPageFragment
import org.mozilla.fenix.library.history.History
import org.mozilla.fenix.library.historymetadata.controller.DefaultHistoryMetadataGroupController
import org.mozilla.fenix.library.historymetadata.interactor.DefaultHistoryMetadataGroupInteractor
import org.mozilla.fenix.library.historymetadata.interactor.HistoryMetadataGroupInteractor
import org.mozilla.fenix.library.historymetadata.view.HistoryMetadataGroupView
import org.mozilla.fenix.pbmlock.registerForVerification
import org.mozilla.fenix.pbmlock.verifyUser
import org.mozilla.fenix.tabstray.redux.state.Page
import org.mozilla.fenix.utils.allowUndo
import androidx.appcompat.R as appcompatR

/**
 * Displays a list of history metadata items for a history metadata search group.
 */
@SuppressWarnings("TooManyFunctions")
class HistoryMetadataGroupFragment :
    LibraryPageFragment<History.Metadata>(), UserInteractionHandler, MenuProvider, SystemInsetsPaddedFragment {

    private lateinit var historyMetadataGroupStore: HistoryMetadataGroupFragmentStore
    private lateinit var interactor: HistoryMetadataGroupInteractor

    private var _historyMetadataGroupView: HistoryMetadataGroupView? = null
    private val historyMetadataGroupView: HistoryMetadataGroupView
        get() = _historyMetadataGroupView!!
    private var _binding: FragmentHistoryMetadataGroupBinding? = null
    private val binding get() = _binding!!

    private val args by navArgs<HistoryMetadataGroupFragmentArgs>()
    private var verificationResultLauncher: ActivityResultLauncher<Intent> =
        registerForVerification(onVerified = ::openHistoryInPrivate)

    override val selectedItems: Set<History.Metadata>
        get() = historyMetadataGroupStore.state.items.filter { it.selected }.toSet()

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View {
        _binding = FragmentHistoryMetadataGroupBinding.inflate(inflater, container, false)

        val historyItems = args.historyMetadataItems.filterIsInstance<History.Metadata>()
        historyMetadataGroupStore = fragmentStore(
            HistoryMetadataGroupFragmentState(
                items = historyItems,
                pendingDeletionItems = requireContext().components.appStore.state.pendingDeletionHistoryItems,
                isEmpty = historyItems.isEmpty(),
            ),
        ) { HistoryMetadataGroupFragmentStore(it) }.value

        interactor = DefaultHistoryMetadataGroupInteractor(
            controller = DefaultHistoryMetadataGroupController(
                historyStorage = requireComponents.core.historyStorage,
                browserStore = requireComponents.core.store,
                appStore = requireComponents.appStore,
                store = historyMetadataGroupStore,
                selectOrAddUseCase = requireComponents.useCases.tabsUseCases.selectOrAddTab,
                fenixBrowserUseCases = requireComponents.useCases.fenixBrowserUseCases,
                navController = findNavController(),
                settings = requireComponents.settings,
                scope = CoroutineScope(Dispatchers.IO),
                searchTerm = args.title,
                deleteSnackbar = ::deleteSnackbar,
                promptDeleteAll = ::promptDeleteAll,
                allDeletedSnackbar = ::allDeletedSnackbar,
            ),
        )

        _historyMetadataGroupView = HistoryMetadataGroupView(
            container = binding.historyMetadataGroupLayout,
            interactor = interactor,
            title = args.title,
            onEmptyStateChanged = {
                historyMetadataGroupStore.dispatch(
                    HistoryMetadataGroupFragmentAction.ChangeEmptyState(it),
                )
            },
        )

        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        requireActivity().addMenuProvider(this, viewLifecycleOwner, Lifecycle.State.RESUMED)

        consumeFrom(historyMetadataGroupStore) { state ->
            historyMetadataGroupView.update(state)
            activity?.invalidateOptionsMenu()
        }

        requireContext().components.appStore.flowScoped(viewLifecycleOwner, Dispatchers.Main) { flow ->
            flow.map { state -> state.pendingDeletionHistoryItems }.collect { items ->
                historyMetadataGroupStore.dispatch(
                    HistoryMetadataGroupFragmentAction.UpdatePendingDeletionItems(
                        pendingDeletionItems = items,
                    ),
                )
            }
        }
    }

    override fun onResume() {
        super.onResume()
        showToolbar(args.title)
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _historyMetadataGroupView = null
        _binding = null
    }

    override fun onBackPressed(): Boolean = interactor.onBackPressed(selectedItems)

    override fun onCreateMenu(menu: Menu, inflater: MenuInflater) {
        if (selectedItems.isNotEmpty()) {
            inflater.inflate(R.menu.history_select_multi, menu)

            menu.findItem(R.id.delete_history_multi_select)?.let { deleteItem ->
                deleteItem.title = SpannableString(deleteItem.title).apply {
                    setTextColor(requireContext(), appcompatR.attr.colorError)
                }
            }
        } else {
            inflater.inflate(R.menu.history_menu, menu)
        }
    }

    override fun onMenuItemSelected(item: MenuItem): Boolean {
        return when (item.itemId) {
            R.id.share_history_multi_select -> {
                interactor.onShareMenuItem(selectedItems)
                true
            }
            R.id.delete_history_multi_select -> {
                interactor.onDelete(selectedItems)
                true
            }
            R.id.open_history_in_new_tabs_multi_select -> {
                openItemsInNewTab { selectedItem ->
                    selectedItem.url
                }

                showTabTray()
                true
            }
            R.id.open_history_in_private_tabs_multi_select -> {
                handleOpenHistoryInPrivateTabsMultiSelectMenuItem()
                true
            }
            R.id.history_delete -> {
                interactor.onDeleteAll()
                true
            }
            // other options are not handled by this menu provider
            else -> false
        }
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
            selectedItem.url
        }

        (activity as HomeActivity).apply {
            browsingModeManager.mode = BrowsingMode.Private
            supportActionBar?.hide()
        }

        showTabTray(openInPrivate = true)
    }

    private fun deleteSnackbar(
        items: Set<History.Metadata>,
        undo: suspend (items: Set<History.Metadata>) -> Unit,
        delete: (Set<History.Metadata>) -> suspend (context: Context) -> Unit,
    ) {
        CoroutineScope(Dispatchers.IO).allowUndo(
            requireView(),
            getSnackBarMessage(items),
            getString(R.string.snackbar_deleted_undo),
            {
                undo.invoke(items)
            },
            delete(items),
        )
    }

    private fun promptDeleteAll() {
        if (childFragmentManager.findFragmentByTag(DeleteAllConfirmationDialogFragment.TAG)
                as? DeleteAllConfirmationDialogFragment != null
        ) {
            return
        }

        DeleteAllConfirmationDialogFragment(interactor, args.title).show(
            childFragmentManager,
            DeleteAllConfirmationDialogFragment.TAG,
        )
    }

    private fun allDeletedSnackbar() {
        runIfFragmentIsAttached {
            showSnackBar(
                binding.root,
                getString(R.string.delete_history_group_snackbar),
            )
        }
    }

    private fun showTabTray(openInPrivate: Boolean = false) {
        findNavController().nav(
            R.id.historyMetadataGroupFragment,
            HistoryMetadataGroupFragmentDirections.actionGlobalTabManagementFragment(
                page = if (openInPrivate) {
                    Page.PrivateTabs
                } else {
                    Page.NormalTabs
                },
            ),
        )
    }

    private fun getSnackBarMessage(historyItems: Set<History.Metadata>): String {
        val historyItem = historyItems.first()
        return String.format(
            requireContext().getString(R.string.history_delete_single_item_snackbar),
            historyItem.url.toShortUrl(requireComponents.publicSuffixList),
        )
    }

    internal class DeleteAllConfirmationDialogFragment(
        private val interactor: HistoryMetadataGroupInteractor,
        private val groupName: String,
    ) : DialogFragment() {
        override fun onCreateDialog(savedInstanceState: Bundle?): Dialog =
            MaterialAlertDialogBuilder(requireContext())
                .setMessage(
                    String.format(
                        getString(R.string.delete_all_history_group_prompt_message),
                        groupName,
                    ),
                )
                .setNegativeButton(R.string.delete_history_group_prompt_cancel) { dialog: DialogInterface, _ ->
                    dialog.cancel()
                }
                .setPositiveButton(R.string.delete_history_group_prompt_allow) { dialog: DialogInterface, _ ->
                    interactor.onDeleteAllConfirmed()
                    dialog.dismiss()
                }
                .create().withCenterAlignedButtons()

        companion object {
            const val TAG = "DELETE_CONFIRMATION_DIALOG_FRAGMENT"
        }
    }
}
