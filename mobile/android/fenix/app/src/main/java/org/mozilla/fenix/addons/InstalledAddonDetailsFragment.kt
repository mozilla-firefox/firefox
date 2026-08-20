/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.addons

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.CompoundButton
import androidx.annotation.StringRes
import androidx.annotation.VisibleForTesting
import androidx.core.view.isVisible
import androidx.fragment.app.Fragment
import androidx.lifecycle.lifecycleScope
import androidx.navigation.findNavController
import androidx.navigation.fragment.findNavController
import com.google.android.material.materialswitch.MaterialSwitch
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import mozilla.components.concept.base.crash.Breadcrumb
import mozilla.components.concept.engine.webextension.EnableSource
import mozilla.components.feature.addons.Addon
import mozilla.components.feature.addons.AddonManager
import mozilla.components.feature.addons.AddonManagerException
import mozilla.components.feature.addons.R as addonsR
import mozilla.components.feature.addons.ui.AddonsManagerAdapter
import mozilla.components.feature.addons.ui.translateName
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.ktx.android.content.appName
import mozilla.components.support.ktx.android.content.appVersionName
import org.mozilla.fenix.BuildConfig
import org.mozilla.fenix.databinding.FragmentInstalledAddOnDetailsBinding
import org.mozilla.fenix.e2e.SystemInsetsPaddedFragment
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.runIfFragmentIsAttached
import org.mozilla.fenix.ext.showToolbar

/** An activity to show the details of a installed add-on. */
@Suppress("LargeClass", "TooManyFunctions")
class InstalledAddonDetailsFragment : Fragment(), SystemInsetsPaddedFragment {
    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE) internal lateinit var addon: Addon
    internal val logger = Logger("InstalledAddonDetailsFragment")

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal val binding
        get() = _binding!!

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    @Suppress("VariableNaming")
    internal var _binding: FragmentInstalledAddOnDetailsBinding? = null

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View {
        if (!::addon.isInitialized) {
            addon = AddonDetailsFragmentArgs.fromBundle(requireNotNull(arguments)).addon
        }

        setBindingAndBindUI(
            FragmentInstalledAddOnDetailsBinding.inflate(
                inflater,
                container,
                false,
            )
        )

        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        bindAddon()
    }

    override fun onResume() {
        super.onResume()
        context?.let {
            showToolbar(title = addon.translateName(it))
        }
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun setBindingAndBindUI(binding: FragmentInstalledAddOnDetailsBinding) {
        _binding = binding
        bindUI()
    }

    @VisibleForTesting internal fun provideAddonManager() = requireContext().components.addonManager

    @VisibleForTesting
    internal fun bindAddon(
        ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
        mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
    ) {
        lifecycleScope.launch(ioDispatcher) {
            // Only needed in case we are not able to find the add-on.
            var breadcrumb: Breadcrumb? = null
            try {
                val latestAddon = provideAddonManager().getAddonByID(addon.id)
                runIfFragmentIsAttached {
                    if (latestAddon == null) {
                        breadcrumb = Breadcrumb("Addon ${addon.id} not found, isInstalled: ${addon.isInstalled()}")
                        throw AddonManagerException(Exception("Addon ${addon.id} not found"))
                    } else {
                        withContext(mainDispatcher) {
                            addon = latestAddon
                            bindUI()
                        }
                    }
                    withContext(mainDispatcher) {
                        binding.addOnProgressBar.isVisible = false
                        binding.addonContainer.isVisible = true
                    }
                }
            } catch (e: AddonManagerException) {
                val crashReporter = context?.components?.analytics?.crashReporter
                breadcrumb?.let {
                    crashReporter?.recordCrashBreadcrumb(it)
                }
                crashReporter?.submitCaughtException(e)
                lifecycleScope.launch(mainDispatcher) {
                    logger.error("Unable to bind addon", e)
                    showUnableToQueryAddonsMessage()
                }
            }
        }
    }

    @VisibleForTesting
    internal fun showUnableToQueryAddonsMessage() {
        runIfFragmentIsAttached {
            showSnackBar(
                binding.root,
                getString(addonsR.string.mozac_feature_addons_failed_to_load_extensions),
            )
            findNavController().popBackStack()
        }
    }

    @VisibleForTesting
    internal fun bindUI() {
        bindEnableSwitch()
        bindSettings()
        bindDetails()
        bindPermissions()
        bindAllowInPrivateBrowsingSwitch()
        bindRemoveButton()
        bindReportButton()
        context?.let {
            val messageBarWarningView = binding.root.findViewById<View>(addonsR.id.add_on_messagebar_warning)
            val messageBarErrorView = binding.root.findViewById<View>(addonsR.id.add_on_messagebar_error)

            AddonsManagerAdapter.bindMessageBars(
                it,
                messageBarWarningView,
                messageBarErrorView,
                onLearnMoreLinkClicked = { link ->
                    binding.root.openLearnMoreLink(link, addon)
                },
                addon,
                addon.translateName(it),
                it.appName,
                it.appVersionName,
            )
        }
    }

    @VisibleForTesting internal fun provideEnableSwitch() = binding.enableSwitch

    @VisibleForTesting internal fun providePrivateBrowsingSwitch() = binding.allowInPrivateBrowsingSwitch

    private val onEnableSwitchCheckedChange = CompoundButton.OnCheckedChangeListener { buttonView, isChecked ->
        toggleAddon(buttonView as MaterialSwitch, providePrivateBrowsingSwitch(), isChecked)
    }

    @VisibleForTesting
    internal fun bindEnableSwitch() {
        val switch = provideEnableSwitch()
        switch.isChecked = addon.isEnabled()
        // When the ad-on is blocklisted or not correctly signed, we do not want to enable the toggle switch
        // because users shouldn't be able to re-enable an add-on in this state.
        if (
            addon.isDisabledAsBlocklisted() ||
                addon.isDisabledAsNotCorrectlySigned() ||
                addon.isDisabledAsIncompatible()
        ) {
            switch.isEnabled = false
            return
        }
        switch.setOnCheckedChangeListener(onEnableSwitchCheckedChange)
    }

    @VisibleForTesting
    internal fun toggleAddon(
        switch: MaterialSwitch,
        privateBrowsingSwitch: MaterialSwitch,
        isChecked: Boolean,
    ) {
        val addonManager = provideAddonManager()
        switch.isClickable = false
        // Re-enabled from onToggleSuccess/onToggleError once the operation completes.
        disableButtons()
        if (isChecked) {
            enableAddon(
                addonManager,
                onSuccess = { onToggleSuccess(it, switch, privateBrowsingSwitch, isEnabling = true) },
                onError = {
                    onToggleError(
                        addonsR.string.mozac_feature_addons_failed_to_enable,
                        switch,
                        privateBrowsingSwitch,
                        isChecked,
                    )
                },
            )
        } else {
            binding.settings.isVisible = false
            addonManager.disableAddon(
                addon,
                onSuccess = { onToggleSuccess(it, switch, privateBrowsingSwitch, isEnabling = false) },
                onError = {
                    onToggleError(
                        addonsR.string.mozac_feature_addons_failed_to_disable,
                        switch,
                        privateBrowsingSwitch,
                        isChecked,
                    )
                },
            )
        }
    }

    private fun onToggleSuccess(
        updatedAddon: Addon,
        switch: MaterialSwitch,
        privateBrowsingSwitch: MaterialSwitch,
        isEnabling: Boolean,
    ) {
        runIfFragmentIsAttached {
            this.addon = updatedAddon
            switch.isClickable = true
            switch.setCheckedSilently(updatedAddon.isEnabled())
            privateBrowsingSwitch.isVisible = updatedAddon.isEnabled()
            if (isEnabling) {
                privateBrowsingSwitch.isChecked =
                    updatedAddon.incognito != Addon.Incognito.NOT_ALLOWED && updatedAddon.isAllowedInPrivateBrowsing()
                binding.settings.isVisible = shouldSettingsBeVisible()
            }
            enableButtons()
        }
    }

    private fun onToggleError(
        @StringRes messageRes: Int,
        switch: MaterialSwitch,
        privateBrowsingSwitch: MaterialSwitch,
        isChecked: Boolean,
    ) {
        runIfFragmentIsAttached {
            switch.isClickable = true
            switch.setCheckedSilently(addon.isEnabled())
            if (!isChecked) {
                binding.settings.isVisible = shouldSettingsBeVisible()
            }
            enableButtons()
            context?.let { ctx ->
                showAddonFailureSnackbar(getString(messageRes, addon.translateName(ctx))) {
                    // If the addon state is already updated by another action,
                    // we don't need this retry anymore, so skip it.
                    if (addon.isEnabled() != isChecked) {
                        toggleAddon(switch, privateBrowsingSwitch, isChecked)
                    }
                }
            }
        }
    }

    private fun MaterialSwitch.setCheckedSilently(isChecked: Boolean) {
        setOnCheckedChangeListener(null)
        this.isChecked = isChecked
        setOnCheckedChangeListener(onEnableSwitchCheckedChange)
    }

    @VisibleForTesting
    internal fun showAddonFailureSnackbar(message: String, onRetry: () -> Unit) {
        showRetryableSnackBar(binding.root, message, retryIfFragmentIsAttached(onRetry))
    }

    @VisibleForTesting
    internal fun retryIfFragmentIsAttached(onRetry: () -> Unit): () -> Unit = {
        runIfFragmentIsAttached { onRetry() }
    }

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun enableAddon(
        addonManager: AddonManager,
        onSuccess: (Addon) -> Unit,
        onError: (Throwable) -> Unit,
    ) {
        // If the addon is migrated from Fennec and supported in Fenix, for the addon to be enabled,
        // we need to also request the addon to be enabled as supported by the app
        if (addon.isSupported() && addon.isDisabledAsUnsupported()) {
            addonManager.enableAddon(
                addon,
                EnableSource.APP_SUPPORT,
                { enabledAddon ->
                    addonManager.enableAddon(enabledAddon, EnableSource.USER, onSuccess, onError)
                },
                onError,
            )
        } else {
            addonManager.enableAddon(addon, EnableSource.USER, onSuccess, onError)
        }
    }

    @VisibleForTesting
    internal fun bindAllowInPrivateBrowsingSwitch() {
        val switch = providePrivateBrowsingSwitch()
        switch.isVisible = addon.isEnabled()

        if (addon.incognito == Addon.Incognito.NOT_ALLOWED) {
            switch.isChecked = false
            switch.isEnabled = false
            switch.text =
                requireContext().getString(addonsR.string.mozac_feature_addons_not_allowed_in_private_browsing)
            return
        }

        switch.isChecked = addon.isAllowedInPrivateBrowsing()

        switch.setOnCheckedChangeListener { v, isChecked ->
            val addonManager = v.context.components.addonManager
            switch.isClickable = false
            disableButtons()
            addonManager.setAddonAllowedInPrivateBrowsing(
                addon,
                isChecked,
                onSuccess = {
                    runIfFragmentIsAttached {
                        this.addon = it
                        switch.isClickable = true
                        enableButtons()
                    }
                },
                onError = {
                    runIfFragmentIsAttached {
                        switch.isChecked = addon.isAllowedInPrivateBrowsing()
                        switch.isClickable = true
                        enableButtons()
                    }
                },
            )
        }
    }

    private fun bindReportButton() {
        binding.reportAddOn.setOnClickListener { v ->
            val shouldCreatePrivateSession = v.context.components.appStore.state.mode.isPrivate

            v.context.components.useCases.tabsUseCases.selectOrAddTab(
                url = "${BuildConfig.AMO_BASE_URL}/android/feedback/addon/${addon.id}/",
                private = shouldCreatePrivateSession,
                ignoreFragment = true,
            )

            // Send user to the newly open tab.
            v.findNavController().navigate(InstalledAddonDetailsFragmentDirections.actionGlobalBrowser(null))
        }
    }

    private fun bindSettings() {
        binding.settings.apply {
            isVisible = shouldSettingsBeVisible()
            setOnClickListener { v ->
                val settingUrl = addon.installedState?.optionsPageUrl ?: return@setOnClickListener
                val directions =
                    if (addon.installedState?.openOptionsPageInTab == true) {
                        val components = v.context.components
                        val shouldCreatePrivateSession = v.context.components.appStore.state.mode.isPrivate

                        // If the addon settings page is already open in a tab, select that one
                        components.useCases.tabsUseCases.selectOrAddTab(
                            url = settingUrl,
                            private = shouldCreatePrivateSession,
                            ignoreFragment = true,
                        )

                        InstalledAddonDetailsFragmentDirections.actionGlobalBrowser(null)
                    } else {
                        InstalledAddonDetailsFragmentDirections
                            .actionInstalledAddonFragmentToAddonInternalSettingsFragment(
                                webExtensionName = addon.translateName(requireContext()),
                                optionsPageUrl = settingUrl,
                                webExtensionId = addon.id,
                            )
                    }
                this.findNavController().navigate(directions)
            }
        }
    }

    private fun bindDetails() {
        binding.details.setOnClickListener {
            val directions =
                InstalledAddonDetailsFragmentDirections.actionInstalledAddonFragmentToAddonDetailsFragment(addon)
            binding.root.findNavController().navigate(directions)
        }
    }

    private fun bindPermissions() {
        binding.permissions.setOnClickListener {
            val directions =
                InstalledAddonDetailsFragmentDirections.actionInstalledAddonFragmentToAddonPermissionsDetailsFragment(
                    addon
                )
            binding.root.findNavController().navigate(directions)
        }
    }

    @VisibleForTesting
    internal fun bindRemoveButton() {
        binding.removeAddOn.setOnClickListener {
            fun uninstall() {
                setAllInteractiveViewsClickable(binding, false)
                provideAddonManager()
                    .uninstallAddon(
                        addon,
                        onSuccess = {
                            runIfFragmentIsAttached {
                                setAllInteractiveViewsClickable(binding, true)
                                binding.root.findNavController().popBackStack()
                            }
                        },
                        onError = { _, _ ->
                            runIfFragmentIsAttached {
                                setAllInteractiveViewsClickable(binding, true)
                                context?.let { ctx ->
                                    showAddonFailureSnackbar(
                                        getString(
                                            addonsR.string.mozac_feature_addons_failed_to_uninstall,
                                            addon.translateName(ctx),
                                        )
                                    ) {
                                        uninstall()
                                    }
                                }
                            }
                        },
                    )
            }

            uninstall()
        }
    }

    private fun setAllInteractiveViewsClickable(
        binding: FragmentInstalledAddOnDetailsBinding,
        clickable: Boolean,
    ) {
        binding.enableSwitch.isClickable = clickable
        binding.settings.isClickable = clickable
        binding.details.isClickable = clickable
        binding.permissions.isClickable = clickable
        binding.removeAddOn.isClickable = clickable
        binding.reportAddOn.isClickable = clickable
    }

    private fun enableButtons() {
        binding.removeAddOn.isEnabled = true
        binding.reportAddOn.isEnabled = true
    }

    private fun disableButtons() {
        binding.removeAddOn.isEnabled = false
        binding.reportAddOn.isEnabled = false
    }

    private fun shouldSettingsBeVisible() = !addon.installedState?.optionsPageUrl.isNullOrEmpty()
}
