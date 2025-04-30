/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings

import android.content.Context
import android.os.Bundle
import androidx.annotation.VisibleForTesting
import androidx.navigation.findNavController
import androidx.preference.Preference
import androidx.preference.PreferenceFragmentCompat
import androidx.preference.SwitchPreference
import org.mozilla.fenix.R
import org.mozilla.fenix.components.metrics.MetricServiceType
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.getPreferenceKey
import org.mozilla.fenix.ext.nav
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.ext.showToolbar
import org.mozilla.fenix.utils.Settings

/**
 * Lets the user toggle telemetry on/off.
 */
class DataChoicesFragment : PreferenceFragmentCompat() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val context = requireContext()
        preferenceManager.sharedPreferences?.registerOnSharedPreferenceChangeListener(this) { _, key ->
            if (key == getPreferenceKey(R.string.pref_key_telemetry)) {
                if (context.settings().isTelemetryEnabled) {
                    context.components.analytics.metrics.start(MetricServiceType.Data)
                    context.settings().isExperimentationEnabled = true
                    requireComponents.nimbus.sdk.globalUserParticipation = true
                } else {
                    context.components.analytics.metrics.stop(MetricServiceType.Data)
                    context.settings().isExperimentationEnabled = false
                    requireComponents.nimbus.sdk.globalUserParticipation = false
                }
                updateStudiesSection()
                // Reset experiment identifiers on both opt-in and opt-out; it's likely
                // that in future we will need to pass in the new telemetry client_id
                // to this method when the user opts back in.
                context.components.nimbus.sdk.resetTelemetryIdentifiers()
            } else if (key == getPreferenceKey(R.string.pref_key_marketing_telemetry)) {
                if (context.settings().isMarketingTelemetryEnabled) {
                    context.components.analytics.metrics.start(MetricServiceType.Marketing)
                } else {
                    context.components.analytics.metrics.stop(MetricServiceType.Marketing)
                }
            } else if (key == getPreferenceKey(R.string.pref_key_daily_usage_ping)) {
                with(context.components.analytics.metrics) {
                    if (context.settings().isDailyUsagePingEnabled) {
                        start(MetricServiceType.UsageReporting)
                    } else {
                        stop(MetricServiceType.UsageReporting)
                    }
                }
            }
        }
    }

    override fun onResume() {
        super.onResume()
        showToolbar(getString(R.string.preferences_data_collection))
        updateStudiesSection()
    }

    override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
        setPreferencesFromResource(R.xml.data_choices_preferences, rootKey)

        requirePreference<SwitchPreference>(R.string.pref_key_telemetry).apply {
            isChecked = context.settings().isTelemetryEnabled
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        val marketingTelemetryPref =
            requirePreference<SwitchPreference>(R.string.pref_key_marketing_telemetry).apply {
                isChecked = context.settings().isMarketingTelemetryEnabled
                onPreferenceChangeListener = SharedPreferenceUpdater()
                isVisible = shouldShowMarketingTelemetryPreference(requireContext().settings())
            }

        requirePreference<Preference>(R.string.pref_key_learn_about_marketing_telemetry).apply {
            isVisible = marketingTelemetryPref.isVisible
        }

        requirePreference<SwitchPreference>(R.string.pref_key_daily_usage_ping).apply {
            isChecked = context.settings().isDailyUsagePingEnabled
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreference>(R.string.pref_key_crash_reporting_always_report).apply {
            isChecked = context.settings().crashReportAlwaysSend
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }
    }

    @VisibleForTesting
    internal fun shouldShowMarketingTelemetryPreference(settings: Settings) =
        settings.hasMadeMarketingTelemetrySelection

    override fun onPreferenceTreeClick(preference: Preference): Boolean {
        context?.also { context ->
            when (preference.key) {
                getPreferenceKey(R.string.pref_key_learn_about_telemetry) -> openLearnMoreUrlInSandboxedTab(
                    context,
                    SupportUtils.getSumoURLForTopic(
                        context = context,
                        topic = SupportUtils.SumoTopic.TECHNICAL_AND_INTERACTION_DATA,
                    ),
                )

                getPreferenceKey(R.string.pref_key_learn_about_marketing_telemetry) -> openLearnMoreUrlInSandboxedTab(
                    context,
                    SupportUtils.getSumoURLForTopic(
                        context = context,
                        topic = SupportUtils.SumoTopic.MARKETING_DATA,
                    ),
                )

                getPreferenceKey(R.string.pref_key_learn_about_daily_usage_ping) -> openLearnMoreUrlInSandboxedTab(
                    context,
                    SupportUtils.getSumoURLForTopic(
                        context = context,
                        topic = SupportUtils.SumoTopic.USAGE_PING_SETTINGS,
                    ),
                )

                getPreferenceKey(R.string.pref_key_learn_about_crash_reporting) -> openLearnMoreUrlInSandboxedTab(
                    context,
                    SupportUtils.getSumoURLForTopic(
                        context = context,
                        topic = SupportUtils.SumoTopic.CRASH_REPORTS,
                    ),
                )
            }
        }
        return super.onPreferenceTreeClick(preference)
    }

    private fun openLearnMoreUrlInSandboxedTab(context: Context, url: String) {
        SupportUtils.launchSandboxCustomTab(
            context = context,
            url = url,
        )
    }

    private fun updateStudiesSection() {
        val studiesPreference = requirePreference<Preference>(R.string.pref_key_studies_section)
        val settings = requireContext().settings()
        val stringId = if (settings.isExperimentationEnabled) {
            R.string.studies_on
        } else {
            R.string.studies_off
        }
        studiesPreference.isEnabled = settings.isTelemetryEnabled
        studiesPreference.summary = getString(stringId)

        studiesPreference.setOnPreferenceClickListener {
            val action = DataChoicesFragmentDirections.actionDataChoicesFragmentToStudiesFragment()
            view?.findNavController()?.nav(R.id.dataChoicesFragment, action)
            true
        }
    }
}
