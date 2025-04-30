/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.engine

import android.content.Context
import androidx.preference.Preference
import org.mozilla.focus.GleanMetrics.TrackingProtection
import org.mozilla.focus.R
import org.mozilla.focus.components.EngineProvider
import org.mozilla.focus.ext.components

/**
 * SharedPreference listener that will update the engine whenever the user changes settings.
 */
class EngineSharedPreferencesListener(
    private val context: Context,
) : Preference.OnPreferenceChangeListener {

    override fun onPreferenceChange(preference: Preference, newValue: Any?): Boolean {
        when (preference.key) {
            context.getString(R.string.pref_key_performance_enable_cookies) ->
                updateTrackingProtectionPolicy(newValue as String)

            context.getString(R.string.pref_key_safe_browsing) ->
                updateSafeBrowsingPolicy(newValue as Boolean)

            context.getString(R.string.pref_key_performance_block_javascript) ->
                updateJavaScriptSetting(newValue as Boolean)

            context.getString(R.string.pref_key_performance_block_webfonts) ->
                updateWebFontsBlocking(newValue as Boolean)
        }

        return true
    }

    internal fun updateTrackingProtectionPolicy(
        source: String? = null,
        tracker: String? = null,
        isEnabled: Boolean = false,
    ) {
        val policy = EngineProvider.createTrackingProtectionPolicy(context)
        val components = context.components

        components.engineDefaultSettings.trackingProtectionPolicy = policy
        components.settingsUseCases.updateTrackingProtection(policy)

        if (source != null && tracker != null) {
            TrackingProtection.trackerSettingChanged.record(
                TrackingProtection.TrackerSettingChangedExtra(
                    sourceOfChange = source,
                    trackerChanged = tracker,
                    isEnabled = isEnabled,
                ),
            )
        }
        components.sessionUseCases.reload()
    }

    private fun updateSafeBrowsingPolicy(newValue: Boolean) {
        EngineProvider.setupSafeBrowsing(context.components.engine, newValue)
        context.components.sessionUseCases.reload()
    }

    private fun updateJavaScriptSetting(newValue: Boolean) {
        val components = context.components

        components.engineDefaultSettings.javascriptEnabled = !newValue
        components.engine.settings.javascriptEnabled = !newValue
        components.sessionUseCases.reload()
    }

    private fun updateWebFontsBlocking(newValue: Boolean) {
        val components = context.components

        components.engineDefaultSettings.webFontsEnabled = !newValue
        components.engine.settings.webFontsEnabled = !newValue
        components.sessionUseCases.reload()
    }

    enum class ChangeSource(val source: String) {
        SETTINGS("Settings"),
        PANEL("Panel"),
    }

    enum class TrackerChanged(val tracker: String) {
        ADVERTISING("Advertising"),
        ANALYTICS("Analytics"),
        SOCIAL("Social"),
        CONTENT("Content"),
    }
}
