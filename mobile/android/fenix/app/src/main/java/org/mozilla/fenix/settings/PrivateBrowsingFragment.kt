/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings

import android.os.Bundle
import android.view.WindowManager
import androidx.preference.Preference
import androidx.preference.PreferenceFragmentCompat
import androidx.preference.SwitchPreference
import org.mozilla.fenix.Config
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.R
import org.mozilla.fenix.components.PrivateShortcutCreateManager
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.ext.showToolbar

/**
 * Lets the user customize Private browsing options.
 */
class PrivateBrowsingFragment : PreferenceFragmentCompat() {
    override fun onResume() {
        super.onResume()
        showToolbar(getString(R.string.preferences_private_browsing_options))
    }

    override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
        setPreferencesFromResource(R.xml.private_browsing_preferences, rootKey)
        updatePreferences()
    }

    private fun updatePreferences() {
        requirePreference<Preference>(R.string.pref_key_add_private_browsing_shortcut).apply {
            setOnPreferenceClickListener {
                PrivateShortcutCreateManager.createPrivateShortcut(requireContext())
                true
            }
        }

        requirePreference<SwitchPreference>(R.string.pref_key_open_links_in_a_private_tab).apply {
            onPreferenceChangeListener = SharedPreferenceUpdater()
            isChecked = context.settings().openLinksInAPrivateTab
        }

        requirePreference<SwitchPreference>(R.string.pref_key_allow_screenshots_in_private_mode).apply {
            onPreferenceChangeListener = object : SharedPreferenceUpdater() {
                override fun onPreferenceChange(preference: Preference, newValue: Any?): Boolean {
                    if ((activity as? HomeActivity)?.browsingModeManager?.mode?.isPrivate == true &&
                        newValue == false
                    ) {
                        activity?.window?.addFlags(WindowManager.LayoutParams.FLAG_SECURE)
                    } else {
                        activity?.window?.clearFlags(WindowManager.LayoutParams.FLAG_SECURE)
                    }
                    return super.onPreferenceChange(preference, newValue)
                }
            }
        }

        requirePreference<SwitchPreference>(R.string.pref_key_private_browsing_locked_enabled).apply {
            onPreferenceChangeListener = SharedPreferenceUpdater()
            isChecked = context.settings().privateBrowsingLockedEnabled
            isVisible = Config.channel.isDebug
        }
    }
}
