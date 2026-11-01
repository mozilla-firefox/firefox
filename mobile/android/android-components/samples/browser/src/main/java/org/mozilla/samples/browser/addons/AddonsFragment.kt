/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.samples.browser.addons

import android.content.Intent
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Toast
import androidx.fragment.app.Fragment
import androidx.lifecycle.lifecycleScope
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import java.util.concurrent.CancellationException
import kotlinx.coroutines.launch
import mozilla.components.browser.state.action.WebExtensionAction
import mozilla.components.feature.addons.Addon
import mozilla.components.feature.addons.AddonManagerException
import mozilla.components.feature.addons.R as addonsR
import mozilla.components.feature.addons.ui.AddonInstallationDialogFragment
import mozilla.components.feature.addons.ui.AddonsManagerAdapter
import mozilla.components.feature.addons.ui.AddonsManagerAdapterDelegate
import mozilla.components.feature.addons.ui.PermissionsDialogFragment
import mozilla.components.feature.addons.ui.PermissionsDialogMode
import mozilla.components.feature.addons.ui.translateName
import org.mozilla.samples.browser.R
import org.mozilla.samples.browser.databinding.FragmentAddOnsBinding
import org.mozilla.samples.browser.databinding.OverlayAddOnProgressBinding
import org.mozilla.samples.browser.ext.components

/** Fragment use for managing add-ons. */
class AddonsFragment : Fragment(), AddonsManagerAdapterDelegate {
    private lateinit var recyclerView: RecyclerView
    private var adapter: AddonsManagerAdapter? = null

    private var _binding: FragmentAddOnsBinding? = null
    private val binding
        get() = _binding!!

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View {
        _binding = FragmentAddOnsBinding.inflate(inflater, container, false)
        return binding.root
    }

    override fun onViewCreated(rootView: View, savedInstanceState: Bundle?) {
        super.onViewCreated(rootView, savedInstanceState)
        bindRecyclerView(rootView)
    }

    override fun onStart() {
        super.onStart()

        findPreviousPermissionDialogFragment()?.let { dialog ->
            val updateInstanceId = dialog.promptInstanceId
            dialog.onPositiveButtonClicked =
                if (updateInstanceId == null) {
                    onConfirmPermissionButtonClicked
                } else {
                    { addon, _, _ -> answerPendingUpdate(addon.id, updateInstanceId, granted = true) }
                }
        }
    }

    private fun bindRecyclerView(rootView: View) {
        recyclerView = rootView.findViewById(R.id.add_ons_list)
        recyclerView.layoutManager = LinearLayoutManager(requireContext())
        viewLifecycleOwner.lifecycleScope.launch {
            try {
                val context = requireContext()
                // Call getAddons directly. It will internally switch to ioDispatcher.
                val addons = context.components.addonManager.getAddons()

                if (adapter == null) {
                    adapter =
                        AddonsManagerAdapter(
                            addonsManagerDelegate = this@AddonsFragment,
                            addons = addons,
                            store = context.components.store,
                        )
                    recyclerView.adapter = adapter
                } else {
                    adapter?.updateAddons(addons)
                }
            } catch (e: AddonManagerException) {
                Toast.makeText(
                        activity,
                        addonsR.string.mozac_feature_addons_failed_to_load_extensions,
                        Toast.LENGTH_SHORT,
                    )
                    .show()
            }
        }
    }

    override fun onAddonItemClicked(addon: Addon) {
        val context = requireContext()

        if (addon.isInstalled()) {
            val intent = Intent(context, InstalledAddonDetailsActivity::class.java)
            intent.putExtra("add_on", addon)
            context.startActivity(intent)
        } else {
            val intent = Intent(context, AddonDetailsActivity::class.java)
            intent.putExtra("add_on", addon)
            this.startActivity(intent)
        }
    }

    override fun onInstallAddonButtonClicked(addon: Addon) {
        showPermissionDialog(addon)
    }

    override fun onReviewExtensionUpdateClicked(addon: Addon) {
        val pendingUpdate = requireContext().components.store.state.pendingExtensionUpdates[addon.id] ?: return
        val dialog =
            PermissionsDialogFragment.newInstance(
                addon = addon,
                mode = PermissionsDialogMode.UPDATE,
                permissions = pendingUpdate.permissions,
                origins = pendingUpdate.origins,
                dataCollectionPermissions = pendingUpdate.dataCollectionPermissions,
                promptInstanceId = pendingUpdate.instanceId,
                onPositiveButtonClicked = { _, _, _ ->
                    answerPendingUpdate(addon.id, pendingUpdate.instanceId, granted = true)
                },
            )

        if (!isAlreadyADialogCreated() && isAdded) {
            dialog.show(parentFragmentManager, PERMISSIONS_DIALOG_FRAGMENT_TAG)
        }
    }

    override fun onCancelExtensionUpdateClicked(addon: Addon) {
        val pendingUpdate = requireContext().components.store.state.pendingExtensionUpdates[addon.id] ?: return
        answerPendingUpdate(addon.id, pendingUpdate.instanceId, granted = false)
    }

    private fun answerPendingUpdate(addonId: String, instanceId: String, granted: Boolean) {
        val store = requireContext().components.store
        val pendingUpdate =
            store.state.pendingExtensionUpdates[addonId]?.takeIf { it.instanceId == instanceId } ?: return

        store.dispatch(WebExtensionAction.RemovePendingExtensionUpdateAction(addonId))
        pendingUpdate.complete(granted)
        // Refresh the add-on's item to hide its request to review the update. For simplicity, all items are refreshed.
        adapter?.run { notifyItemRangeChanged(0, itemCount) }
    }

    override fun onNotYetSupportedSectionClicked(unsupportedAddons: List<Addon>) {
        val intent = Intent(context, NotYetSupportedAddonActivity::class.java)
        intent.putExtra("add_ons", ArrayList(unsupportedAddons))
        requireContext().startActivity(intent)
    }

    private fun isAlreadyADialogCreated(): Boolean {
        return findPreviousPermissionDialogFragment() != null && findPreviousInstallationDialogFragment() != null
    }

    private fun findPreviousPermissionDialogFragment(): PermissionsDialogFragment? {
        return parentFragmentManager.findFragmentByTag(PERMISSIONS_DIALOG_FRAGMENT_TAG) as? PermissionsDialogFragment
    }

    private fun findPreviousInstallationDialogFragment(): AddonInstallationDialogFragment? {
        return parentFragmentManager.findFragmentByTag(INSTALLATION_DIALOG_FRAGMENT_TAG)
            as? AddonInstallationDialogFragment
    }

    private fun showPermissionDialog(addon: Addon) {
        if (isInstallationInProgress) {
            return
        }

        val dialog =
            PermissionsDialogFragment.newInstance(
                addon = addon,
                onPositiveButtonClicked = onConfirmPermissionButtonClicked,
                permissions = addon.permissions,
                origins =
                    addon.optionalOrigins.map {
                        it.name
                    },
                dataCollectionPermissions = emptyList(),
            )

        if (!isAlreadyADialogCreated() && isAdded) {
            dialog.show(parentFragmentManager, PERMISSIONS_DIALOG_FRAGMENT_TAG)
        }
    }

    private fun showInstallationDialog(addon: Addon) {
        if (isInstallationInProgress) {
            return
        }
        val dialog = AddonInstallationDialogFragment.newInstance(addon = addon)

        if (!isAlreadyADialogCreated() && isAdded) {
            dialog.show(parentFragmentManager, INSTALLATION_DIALOG_FRAGMENT_TAG)
        }
    }

    private val onConfirmPermissionButtonClicked: ((Addon, Boolean, Boolean) -> Unit) = { addon, _, _ ->
        val includedBinding = OverlayAddOnProgressBinding.bind(binding.addonProgressOverlay.addonProgressOverlay)

        includedBinding.root.visibility = View.VISIBLE
        isInstallationInProgress = true

        val installOperation =
            requireContext()
                .components
                .addonManager
                .installAddon(
                    url = addon.downloadUrl,
                    onSuccess = { installedAddon ->
                        context?.let {
                            adapter?.updateAddon(installedAddon)
                            includedBinding.root.visibility = View.GONE
                            isInstallationInProgress = false
                            showInstallationDialog(installedAddon)
                        }
                    },
                    onError = { e ->
                        // No need to display an error message if installation was cancelled by the user.
                        if (e !is CancellationException) {
                            Toast.makeText(
                                    requireContext(),
                                    getString(
                                        addonsR.string.mozac_feature_addons_failed_to_install,
                                        addon.translateName(requireContext()),
                                    ),
                                    Toast.LENGTH_SHORT,
                                )
                                .show()
                        }

                        includedBinding.root.visibility = View.GONE
                        isInstallationInProgress = false
                    },
                )

        includedBinding.cancelButton.setOnClickListener {
            viewLifecycleOwner.lifecycleScope.launch {
                // Hide the installation progress overlay once cancellation is successful.
                if (installOperation.cancel().await()) {
                    includedBinding.root.visibility = View.GONE
                }
            }
        }
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }

    /** Whether or not an add-on installation is in progress. */
    private var isInstallationInProgress = false

    companion object {
        private const val PERMISSIONS_DIALOG_FRAGMENT_TAG = "ADDONS_PERMISSIONS_DIALOG_FRAGMENT"
        private const val INSTALLATION_DIALOG_FRAGMENT_TAG = "ADDONS_INSTALLATION_DIALOG_FRAGMENT"
    }
}
