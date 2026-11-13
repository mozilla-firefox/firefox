/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.deletebrowsingdata

import android.content.DialogInterface
import android.os.Bundle
import android.view.View
import androidx.fragment.app.Fragment
import androidx.lifecycle.lifecycleScope
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Dispatchers.IO
import kotlinx.coroutines.Dispatchers.Main
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import mozilla.components.lib.state.ext.flowScoped
import mozilla.components.ui.widgets.withCenterAlignedButtons
import org.mozilla.fenix.R
import org.mozilla.fenix.compose.snackbar.Snackbar
import org.mozilla.fenix.compose.snackbar.SnackbarState
import org.mozilla.fenix.databinding.FragmentDeleteBrowsingDataBinding
import org.mozilla.fenix.e2e.SystemInsetsPaddedFragment
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.ext.runIfFragmentIsAttached
import org.mozilla.fenix.ext.showToolbar
import org.mozilla.fenix.utils.Settings

/** Settings screen allowing users to choose what browsing data to delete. */
@SuppressWarnings("TooManyFunctions", "LargeClass")
class DeleteBrowsingDataFragment : Fragment(R.layout.fragment_delete_browsing_data), SystemInsetsPaddedFragment {

    private lateinit var controller: DeleteBrowsingDataController
    private var scope: CoroutineScope? = null
    private lateinit var settings: Settings

    private var _binding: FragmentDeleteBrowsingDataBinding? = null
    private val binding
        get() = _binding!!

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        val tabsUseCases = requireComponents.useCases.tabsUseCases
        val downloadUseCases = requireComponents.useCases.downloadUseCases

        _binding = FragmentDeleteBrowsingDataBinding.bind(view)
        controller =
            DefaultDeleteBrowsingDataController(
                deleteDataUseCases =
                    DefaultDeleteBrowsingDataController.DeleteDataUseCases(
                        removeAllTabs = tabsUseCases.removeAllTabs,
                        removeAllDownloads = downloadUseCases.removeAllDownloads,
                    ),
                dataStorage =
                    DefaultDeleteBrowsingDataController.DataStorage(
                        history = requireComponents.core.historyStorage,
                        permissions = requireComponents.core.permissionStorage,
                    ),
                stores =
                    DefaultDeleteBrowsingDataController.Stores(
                        appStore = requireComponents.appStore,
                        browserStore = requireComponents.core.store,
                    ),
                engine = requireComponents.core.engine,
                settings = requireComponents.settings,
                coroutineContext = requireActivity().lifecycleScope.coroutineContext,
            )
        settings = requireComponents.settings

        getCheckboxes().iterator().forEach {
            it.onCheckListener = { _ ->
                updateDeleteButton()
                updatePreference(it)
            }
        }

        getCheckboxes().iterator().forEach {
            it.isChecked =
                when (it.id) {
                    R.id.open_tabs_item -> settings.deleteOpenTabs
                    R.id.browsing_history_item -> settings.deleteBrowsingHistory
                    R.id.cookies_and_site_data_item -> settings.deleteCookies
                    R.id.cached_files_item -> settings.deleteCache
                    R.id.site_permissions_item -> settings.deleteSitePermissions
                    R.id.downloads_item -> settings.deleteDownloads
                    else -> true
                }
        }

        binding.deleteData.setOnClickListener {
            askToDelete()
        }
        updateDeleteButton()
    }

    private fun updatePreference(it: DeleteBrowsingDataItem) {
        when (it.id) {
            R.id.open_tabs_item -> settings.deleteOpenTabs = it.isChecked
            R.id.browsing_history_item -> settings.deleteBrowsingHistory = it.isChecked
            R.id.cookies_and_site_data_item -> settings.deleteCookies = it.isChecked
            R.id.cached_files_item -> settings.deleteCache = it.isChecked
            R.id.site_permissions_item -> settings.deleteSitePermissions = it.isChecked
            R.id.downloads_item -> settings.deleteDownloads = it.isChecked
            else -> return
        }
    }

    override fun onStart() {
        super.onStart()

        scope =
            requireComponents.core.store.flowScoped(viewLifecycleOwner, Dispatchers.Main) { flow ->
                flow
                    .map { state -> state.tabs.size }
                    .distinctUntilChanged()
                    .collect { openTabs -> updateTabCount(openTabs) }
            }
    }

    override fun onResume() {
        super.onResume()
        showToolbar(getString(R.string.preferences_delete_browsing_data))

        getCheckboxes().forEach {
            it.visibility = View.VISIBLE
        }

        updateItemCounts()
    }

    private fun updateDeleteButton(deleteInProgress: Boolean = false) {
        runIfFragmentIsAttached {
            val enabled = getCheckboxes().any { it.isChecked } && !deleteInProgress

            binding.deleteData.isEnabled = enabled
            binding.deleteData.alpha = if (enabled) ENABLED_ALPHA else DISABLED_ALPHA
        }
    }

    private fun updateCheckboxes(deleteInProgress: Boolean = false) {
        runIfFragmentIsAttached {
            getCheckboxes().forEach {
                it.isEnabled = !deleteInProgress
                binding.deleteData.alpha = if (!deleteInProgress) ENABLED_ALPHA else DISABLED_ALPHA
            }
        }
    }

    private fun askToDelete() {
        runIfFragmentIsAttached {
            MaterialAlertDialogBuilder(requireContext())
                .apply {
                    setMessage(
                        requireContext()
                            .getString(
                                R.string.delete_browsing_data_prompt_message_3,
                                requireContext().getString(R.string.app_name),
                            )
                    )

                    setNegativeButton(R.string.delete_browsing_data_prompt_cancel) { it: DialogInterface, _ ->
                        it.cancel()
                    }

                    setPositiveButton(R.string.delete_browsing_data_prompt_allow) { it: DialogInterface, _ ->
                        it.dismiss()
                        deleteSelected()
                    }
                    create().withCenterAlignedButtons()
                }
                .show()
        }
    }

    private fun deleteSelected() {
        runIfFragmentIsAttached {
            startDeletion()
            lifecycleScope.launch(IO) {
                getCheckboxes().forEachIndexed { i, v ->
                    if (v.isChecked) {
                        when (i) {
                            OPEN_TABS_INDEX -> controller.deleteTabs()
                            HISTORY_INDEX -> controller.deleteBrowsingHistory()
                            COOKIES_INDEX -> controller.deleteCookiesAndSiteData()
                            CACHED_INDEX -> controller.deleteCachedFiles()
                            PERMS_INDEX -> controller.deleteSitePermissions()
                            DOWNLOADS_INDEX -> controller.deleteDownloads()
                        }
                    }
                }

                withContext(Main) {
                    finishDeletion()
                }
            }
        }
    }

    private fun startDeletion() {
        updateDeleteButton(deleteInProgress = true)
        updateCheckboxes(deleteInProgress = true)
        binding.progressBar.visibility = View.VISIBLE
        binding.deleteBrowsingDataWrapper.isEnabled = false
        binding.deleteBrowsingDataWrapper.isClickable = false
        binding.deleteBrowsingDataWrapper.alpha = DISABLED_ALPHA
    }

    private fun finishDeletion() {
        updateDeleteButton(deleteInProgress = false)
        updateCheckboxes(deleteInProgress = false)
        binding.progressBar.visibility = View.GONE
        binding.deleteBrowsingDataWrapper.isEnabled = true
        binding.deleteBrowsingDataWrapper.isClickable = true
        binding.deleteBrowsingDataWrapper.alpha = ENABLED_ALPHA

        updateItemCounts()

        Snackbar.make(
                snackBarParentView = requireView(),
                snackbarState = SnackbarState(message = getString(R.string.preferences_delete_browsing_data_snackbar)),
            )
            .show()
    }

    override fun onPause() {
        super.onPause()
        binding.progressBar.visibility = View.GONE
    }

    override fun onStop() {
        super.onStop()
        scope?.cancel()
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }

    private fun updateItemCounts() {
        updateTabCount()
        updateHistoryCount()
        updateCookies()
        updateCachedImagesAndFiles()
        updateSitePermissions()
    }

    private fun updateTabCount(openTabs: Int = requireComponents.core.store.state.tabs.size) {
        binding.openTabsItem.apply {
            subtitleView.text =
                resources.getString(
                    R.string.preferences_delete_browsing_data_tabs_subtitle,
                    openTabs,
                )
        }
    }

    private fun updateHistoryCount() {
        binding.browsingHistoryItem.subtitleView.text = ""

        viewLifecycleOwner.lifecycleScope.launch(IO) {
            val historyCount = requireComponents.core.historyStorage.getVisited().size
            launch(Main) {
                binding.browsingHistoryItem.apply {
                    subtitleView.text =
                        resources.getString(
                            R.string.preferences_delete_browsing_data_browsing_data_subtitle,
                            historyCount,
                        )
                }
            }
        }
    }

    private fun updateCookies() {
        // NO OP until we have GeckoView methods to count cookies
    }

    private fun updateCachedImagesAndFiles() {
        // NO OP until we have GeckoView methods to count cached images and files
    }

    private fun updateSitePermissions() {
        // NO OP until we have GeckoView methods for cookies and cached files, for consistency
    }

    private fun getCheckboxes(): List<DeleteBrowsingDataItem> {
        return listOf(
            binding.openTabsItem,
            binding.browsingHistoryItem,
            binding.cookiesAndSiteDataItem,
            binding.cachedFilesItem,
            binding.sitePermissionsItem,
            binding.downloadsItem,
        )
    }

    companion object {
        private const val ENABLED_ALPHA = 1f
        private const val DISABLED_ALPHA = 0.6f

        private const val OPEN_TABS_INDEX = 0
        private const val HISTORY_INDEX = 1
        private const val COOKIES_INDEX = 2
        private const val CACHED_INDEX = 3
        private const val PERMS_INDEX = 4
        private const val DOWNLOADS_INDEX = 5
    }
}
