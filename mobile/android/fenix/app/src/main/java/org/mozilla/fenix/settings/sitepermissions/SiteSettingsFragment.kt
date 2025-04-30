/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.sitepermissions

import android.os.Bundle
import androidx.core.content.ContextCompat
import androidx.navigation.findNavController
import androidx.preference.Preference
import androidx.preference.Preference.OnPreferenceClickListener
import androidx.preference.PreferenceFragmentCompat
import androidx.preference.SwitchPreference
import mozilla.components.browser.state.action.DefaultDesktopModeAction
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.fenix.GleanMetrics.Autoplay
import org.mozilla.fenix.R
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.getPreferenceKey
import org.mozilla.fenix.ext.navigateWithBreadcrumb
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.ext.showToolbar
import org.mozilla.fenix.settings.PhoneFeature
import org.mozilla.fenix.settings.requirePreference

/**
 * Screen for managing settings related to site permissions and content defaults.
 */
@SuppressWarnings("TooManyFunctions")
class SiteSettingsFragment : PreferenceFragmentCompat() {

    override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
        setPreferencesFromResource(R.xml.site_permissions_preferences, rootKey)

        // This should be setup in onCreatePreferences so we setup only once when the fragment is created
        bindDesktopMode()
    }

    override fun onResume() {
        super.onResume()
        showToolbar(getString(R.string.preferences_site_settings))
        setupPreferences()
    }

    private fun setupPreferences() {
        bindCategoryPhoneFeatures()
        bindExceptions()
    }

    private fun bindDesktopMode() {
        requirePreference<SwitchPreference>(R.string.pref_key_desktop_browsing).apply {
            icon?.setTint(ContextCompat.getColor(context, R.color.fx_mobile_icon_color_primary))
            isChecked = requireComponents.core.store.state.desktopMode
            isPersistent = false
            onPreferenceChangeListener =
                Preference.OnPreferenceChangeListener { _, _ ->
                    requireComponents.core.store.dispatch(DefaultDesktopModeAction.ToggleDesktopMode)
                    true
                }
        }
    }

    private fun bindExceptions() {
        val keyExceptions = getPreferenceKey(R.string.pref_key_show_site_exceptions)
        val exceptionsCategory = requireNotNull(findPreference(keyExceptions))

        exceptionsCategory.onPreferenceClickListener = OnPreferenceClickListener {
            val directions = SiteSettingsFragmentDirections.actionSitePermissionsToExceptions()
            requireView().findNavController().navigate(directions)
            true
        }
    }

    private fun bindCategoryPhoneFeatures() {
        PhoneFeature.entries
            // Autoplay inaudible should be set in the same menu as autoplay audible, so it does
            // not need to be bound
            .filter { it != PhoneFeature.AUTOPLAY_INAUDIBLE }
            .forEach(::initPhoneFeature)
    }

    private fun initPhoneFeature(phoneFeature: PhoneFeature) {
        val context = requireContext()
        val settings = context.settings()

        val cameraPhoneFeatures = requirePreference<Preference>(phoneFeature.getPreferenceId())
        cameraPhoneFeatures.summary = phoneFeature.getActionLabel(context, settings = settings)

        cameraPhoneFeatures.onPreferenceClickListener = OnPreferenceClickListener {
            navigateToPhoneFeature(phoneFeature)
            true
        }
    }

    private fun navigateToPhoneFeature(phoneFeature: PhoneFeature) {
        val directions = SiteSettingsFragmentDirections
            .actionSitePermissionsToManagePhoneFeatures(phoneFeature)

        if (phoneFeature == PhoneFeature.AUTOPLAY_AUDIBLE) {
            Autoplay.visitedSetting.record(NoExtras())
        }
        context?.let {
            requireView().findNavController().navigateWithBreadcrumb(
                directions = directions,
                navigateFrom = "SitePermissionsFragment",
                navigateTo = "ActionSitePermissionsToManagePhoneFeatures",
                crashReporter = it.components.analytics.crashReporter,
            )
        }
    }
}
