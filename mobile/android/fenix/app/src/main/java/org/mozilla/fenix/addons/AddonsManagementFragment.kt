/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.addons

import android.content.Context
import android.graphics.Typeface
import android.graphics.fonts.FontStyle.FONT_WEIGHT_MEDIUM
import android.os.Build
import android.os.Bundle
import android.view.View
import android.view.accessibility.AccessibilityNodeInfo
import androidx.annotation.VisibleForTesting
import androidx.core.content.ContextCompat
import androidx.core.view.isVisible
import androidx.fragment.app.Fragment
import androidx.lifecycle.lifecycleScope
import androidx.navigation.fragment.findNavController
import androidx.recyclerview.widget.LinearLayoutManager
import com.google.android.material.R as materialR
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Dispatchers.IO
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch
import mozilla.components.browser.state.selector.pendingExtensionUpdatesNeedingAttention
import mozilla.components.concept.engine.webextension.InstallationMethod
import mozilla.components.feature.addons.Addon
import mozilla.components.feature.addons.AddonManager
import mozilla.components.feature.addons.AddonManagerException
import mozilla.components.feature.addons.R as addonsR
import mozilla.components.feature.addons.ui.AddonsManagerAdapter
import mozilla.components.lib.state.ext.consumeFlow
import org.mozilla.fenix.R
import org.mozilla.fenix.databinding.FragmentAddOnsManagementBinding
import org.mozilla.fenix.e2e.SystemInsetsPaddedFragment
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.openToBrowser
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.ext.runIfFragmentIsAttached
import org.mozilla.fenix.ext.showToolbar
import org.mozilla.fenix.settings.SupportUtils.AMO_HOMEPAGE_FOR_ANDROID
import org.mozilla.fenix.theme.ThemeManager

/** Fragment use for managing add-ons. */
@Suppress("TooManyFunctions", "LargeClass")
class AddonsManagementFragment : Fragment(R.layout.fragment_add_ons_management), SystemInsetsPaddedFragment {

    private var binding: FragmentAddOnsManagementBinding? = null

    private var addons: List<Addon> = emptyList()

    private var adapter: AddonsManagerAdapter? = null

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        binding = FragmentAddOnsManagementBinding.bind(view)
        bindRecyclerView()
        observePendingExtensionUpdates()
    }

    override fun onResume() {
        super.onResume()
        showToolbar(getString(R.string.preferences_extensions))
    }

    // The add-on items show pending updates, which the adapter cannot detect, so redraw them when those change.
    private fun observePendingExtensionUpdates() {
        consumeFlow(requireComponents.core.store) { flow ->
            flow
                .map { it.pendingExtensionUpdatesNeedingAttention.keys }
                .distinctUntilChanged()
                .collect { adapter?.run { notifyItemRangeChanged(0, itemCount) } }
        }
    }

    override fun onDestroyView() {
        super.onDestroyView()
        // letting go of the resources to avoid memory leak.
        adapter = null
        binding = null
    }

    @Suppress("CognitiveComplexMethod")
    private fun bindRecyclerView() {
        val managementView =
            AddonsManagementView(
                navController = findNavController(),
                onInstallButtonClicked = ::installAddon,
                onMoreAddonsButtonClicked = ::openAMO,
                onLearnMoreClicked = { link, addon ->
                    binding?.root?.openLearnMoreLink(link, addon)
                },
                onReviewUpdateClicked = { showPendingExtensionUpdate(requireComponents.core.store, it.id) },
                onCancelUpdateClicked = { cancelPendingExtensionUpdate(requireComponents.core.store, it.id) },
            )

        val recyclerView = binding?.addOnsList
        recyclerView?.layoutManager = LinearLayoutManager(requireContext())
        val shouldRefresh = adapter != null

        lifecycleScope.launch(IO) {
            try {
                addons = requireContext().components.addonManager.getAddons()
                lifecycleScope.launch(Dispatchers.Main) {
                    runIfFragmentIsAttached {
                        if (!shouldRefresh) {
                            adapter =
                                AddonsManagerAdapter(
                                    addonsManagerDelegate = managementView,
                                    addons = addons,
                                    style = createAddonStyle(requireContext()),
                                    store = requireComponents.core.store,
                                )
                        }
                        binding?.addOnsProgressBar?.isVisible = false
                        binding?.addOnsEmptyMessage?.isVisible = false

                        recyclerView?.adapter = adapter
                        recyclerView?.accessibilityDelegate =
                            object : View.AccessibilityDelegate() {
                                override fun onInitializeAccessibilityNodeInfo(
                                    host: View,
                                    info: AccessibilityNodeInfo,
                                ) {
                                    super.onInitializeAccessibilityNodeInfo(host, info)

                                    adapter?.let {
                                        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                                            info.collectionInfo =
                                                AccessibilityNodeInfo.CollectionInfo(
                                                    it.itemCount,
                                                    1,
                                                    false,
                                                )
                                        } else {
                                            @Suppress("DEPRECATION")
                                            info.collectionInfo =
                                                AccessibilityNodeInfo.CollectionInfo.obtain(
                                                    it.itemCount,
                                                    1,
                                                    false,
                                                )
                                        }
                                    }
                                }
                            }

                        if (shouldRefresh) {
                            adapter?.updateAddons(addons)
                        }
                    }
                }
            } catch (e: AddonManagerException) {
                lifecycleScope.launch(Dispatchers.Main) {
                    runIfFragmentIsAttached {
                        binding?.let {
                            showSnackBar(
                                it.root,
                                getString(addonsR.string.mozac_feature_addons_failed_to_load_extensions),
                            )
                        }
                        binding?.addOnsProgressBar?.isVisible = false
                        binding?.addOnsEmptyMessage?.isVisible = true
                    }
                }
            }
        }
    }

    private fun createAddonStyle(context: Context): AddonsManagerAdapter.Style {
        val sectionsTypeFace =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                Typeface.create(Typeface.DEFAULT, FONT_WEIGHT_MEDIUM, false)
            } else {
                Typeface.create(Typeface.DEFAULT, Typeface.BOLD)
            }

        return AddonsManagerAdapter.Style(
            sectionsTextColor = ThemeManager.resolveAttribute(materialR.attr.colorOnSurface, context),
            addonNameTextColor = ThemeManager.resolveAttribute(materialR.attr.colorOnSurface, context),
            addonSummaryTextColor = ThemeManager.resolveAttribute(materialR.attr.colorOnSurfaceVariant, context),
            sectionsTypeFace = sectionsTypeFace,
            addonAllowPrivateBrowsingLabelDrawableRes = R.drawable.ic_add_on_private_browsing_label,
        )
    }

    @VisibleForTesting
    internal fun provideAddonManager(): AddonManager {
        return requireContext().components.addonManager
    }

    internal fun installAddon(addon: Addon) {
        binding?.addonProgressOverlay?.overlayCardView?.visibility = View.VISIBLE

        if (requireComponents.appStore.state.mode.isPrivate) {
            binding
                ?.addonProgressOverlay
                ?.overlayCardView
                ?.setBackgroundColor(
                    ContextCompat.getColor(
                        requireContext(),
                        R.color.fx_mobile_private_layer_color_3,
                    )
                )
        }

        val installOperation =
            provideAddonManager()
                .installAddon(
                    url = addon.downloadUrl,
                    installationMethod = InstallationMethod.MANAGER,
                    onSuccess = {
                        runIfFragmentIsAttached {
                            adapter?.updateAddon(it)
                            binding?.addonProgressOverlay?.overlayCardView?.visibility = View.GONE
                        }
                    },
                    onError = { _ ->
                        binding?.addonProgressOverlay?.overlayCardView?.visibility = View.GONE
                    },
                )
        binding?.addonProgressOverlay?.cancelButton?.setOnClickListener {
            lifecycleScope.launch(Dispatchers.Main) {
                val safeBinding = binding
                // Hide the installation progress overlay once cancellation is successful.
                if (installOperation.cancel().await()) {
                    safeBinding?.addonProgressOverlay?.overlayCardView?.visibility = View.GONE
                }
            }
        }
    }

    private fun openAMO() {
        findNavController().openToBrowser()
        requireComponents.useCases.fenixBrowserUseCases.loadUrlOrSearch(
            searchTermOrURL = AMO_HOMEPAGE_FOR_ANDROID,
            newTab = true,
        )
    }
}
