/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.logins.fragment

import android.os.Bundle
import androidx.lifecycle.lifecycleScope
import androidx.navigation.fragment.findNavController
import androidx.preference.Preference
import androidx.preference.PreferenceFragmentCompat
import androidx.preference.SwitchPreferenceCompat
import mozilla.components.concept.sync.SyncEngine
import mozilla.components.feature.autofill.preference.AutofillPreference
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.fenix.GleanMetrics.Logins
import org.mozilla.fenix.R
import org.mozilla.fenix.components.accounts.FenixFxAEntryPoint
import org.mozilla.fenix.e2e.SystemInsetsPaddedFragment
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.navigateWithBreadcrumb
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.ext.showToolbar
import org.mozilla.fenix.settings.SharedPreferenceUpdater
import org.mozilla.fenix.settings.SyncPreferenceView
import org.mozilla.fenix.settings.requirePreference

/** Settings screen allowing users to configure the passwords manager functionality of the application. */
@Suppress("TooManyFunctions")
class SavedLoginsAuthFragment : PreferenceFragmentCompat(), SystemInsetsPaddedFragment {

    override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
        setPreferencesFromResource(R.xml.logins_preferences, rootKey)
    }

    override fun onResume() {
        super.onResume()
        showToolbar(getString(R.string.preferences_passwords_logins_and_passwords_2))

        requirePreference<Preference>(R.string.pref_key_save_logins_settings).apply {
            summary =
                getString(
                    if (context.components.settings.shouldPromptToSaveLogins) {
                        R.string.preferences_passwords_save_logins_ask_to_save
                    } else {
                        R.string.preferences_passwords_save_logins_never_save
                    }
                )
            setOnPreferenceClickListener {
                navigateToSaveLoginSettingFragment()
                true
            }
        }

        requirePreference<AutofillPreference>(R.string.pref_key_android_autofill).apply {
            update()
        }

        requirePreference<Preference>(R.string.pref_key_login_exceptions).apply {
            setOnPreferenceClickListener {
                navigateToLoginExceptionFragment()
                true
            }
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_autofill_logins).apply {
            title =
                context.getString(
                    R.string.preferences_passwords_autofill2,
                    getString(R.string.app_name),
                )
            summary =
                context.getString(
                    R.string.preferences_passwords_autofill_description,
                    getString(R.string.app_name),
                )
            isChecked = context.components.settings.shouldAutofillLogins
            onPreferenceChangeListener =
                object : SharedPreferenceUpdater() {
                    override fun onPreferenceChange(preference: Preference, newValue: Any?): Boolean {
                        context.components.core.engine.settings.loginAutofillEnabled = newValue as Boolean
                        return super.onPreferenceChange(preference, newValue)
                    }
                }
        }

        requirePreference<Preference>(R.string.pref_key_saved_logins).setOnPreferenceClickListener {
            navigateToSavedLoginsFragment()
            true
        }

        SyncPreferenceView(
            syncPreference = requirePreference(R.string.pref_key_sync_logins),
            lifecycleOwner = viewLifecycleOwner,
            coroutineScope = viewLifecycleOwner.lifecycleScope,
            accountManager = requireComponents.backgroundServices.accountManager,
            syncEngine = SyncEngine.Passwords,
            loggedOffTitle = requireContext().getString(R.string.preferences_passwords_sync_logins_across_devices_2),
            loggedInTitle = requireContext().getString(R.string.preferences_passwords_sync_logins_2),
            onSyncSignInClicked = {
                val directions =
                    SavedLoginsAuthFragmentDirections.actionSavedLoginsAuthFragmentToTurnOnSyncFragment(
                        entrypoint = FenixFxAEntryPoint.SavedLogins
                    )
                findNavController().navigate(directions)
            },
            onReconnectClicked = {
                val directions =
                    SavedLoginsAuthFragmentDirections.actionGlobalAccountProblemFragment(
                        entrypoint = FenixFxAEntryPoint.SavedLogins
                    )
                findNavController().navigate(directions)
            },
        )
    }

    private fun navigateToSavedLoginsFragment() {
        if (findNavController().currentDestination?.id == R.id.savedLoginsAuthFragment) {
            Logins.openLogins.record(NoExtras())
            val directions = SavedLoginsAuthFragmentDirections.actionSavedLoginsAuthFragmentToLoginsListFragment()
            findNavController().navigate(directions)
        }
    }

    private fun navigateToSaveLoginSettingFragment() {
        val directions = SavedLoginsAuthFragmentDirections.actionSavedLoginsAuthFragmentToSavedLoginsSettingFragment()
        findNavController()
            .navigateWithBreadcrumb(
                directions = directions,
                navigateFrom = "SavedLoginsAuthFragment",
                navigateTo = "ActionSavedLoginsAuthFragmentToSavedLoginsSettingFragment",
                crashReporter = requireComponents.analytics.crashReporter,
            )
    }

    private fun navigateToLoginExceptionFragment() {
        val directions = SavedLoginsAuthFragmentDirections.actionSavedLoginsAuthFragmentToLoginExceptionsFragment()
        findNavController().navigate(directions)
    }
}
