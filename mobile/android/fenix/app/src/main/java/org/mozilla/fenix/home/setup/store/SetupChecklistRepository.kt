/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.setup.store

import android.content.Context
import android.content.SharedPreferences
import androidx.annotation.StringRes
import androidx.annotation.VisibleForTesting
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.launch
import org.mozilla.fenix.R
import org.mozilla.fenix.ext.settings

/**
 * The repository for managing setup checklist preferences.
 */
interface SetupChecklistRepository {
    /**
     * An update to a [SetupChecklistPreference].
     */
    data class SetupChecklistPreferenceUpdate(
        val preference: SetupChecklistPreference,
        val value: Boolean,
    )

    /**
     * Updates a specific preference.
     *
     * @param type The type of preference to modify.
     * @param value The value to update the preference value to.
     */
    fun setPreference(type: SetupChecklistPreference, value: Boolean)

    /**
     * A [Flow] of [SetupChecklistPreferenceUpdate]s.
     */
    val setupChecklistPreferenceUpdates: Flow<SetupChecklistPreferenceUpdate>

    /**
     * Initializes the repository and starts the [SharedPreferences] listener.
     */
    fun init()
}

/**
 * Enum representing the types of privacy preferences available.
 *
 * @property preferenceKey The string resource key for the preference.
 */
enum class SetupChecklistPreference(@StringRes val preferenceKey: Int) {
    SetToDefault(R.string.pref_key_default_browser),
    SignIn(R.string.pref_key_fxa_signed_in),
    ThemeComplete(R.string.pref_key_setup_step_theme),
    ToolbarComplete(R.string.pref_key_setup_step_toolbar),
    ExtensionsComplete(R.string.pref_key_setup_step_extensions),
    InstallSearchWidget(R.string.pref_key_search_widget_installed_2),
    ShowSetupChecklist(R.string.pref_key_setup_checklist_complete),
}

/**
 * The default implementation of [SetupChecklistRepository].
 *
 * @param context the Android context.
 * @param coroutineScope the coroutine scope used for emitting flows.
 */
class DefaultSetupChecklistRepository(
    private val context: Context,
    private val coroutineScope: CoroutineScope = CoroutineScope(Dispatchers.Main),
) : SetupChecklistRepository {
    private val settings = context.settings()
    private val _setupChecklistPreferenceUpdates =
        MutableSharedFlow<SetupChecklistRepository.SetupChecklistPreferenceUpdate>()

    override val setupChecklistPreferenceUpdates: Flow<SetupChecklistRepository.SetupChecklistPreferenceUpdate>
        get() = _setupChecklistPreferenceUpdates.asSharedFlow()

    override fun init() {
        settings.preferences.registerOnSharedPreferenceChangeListener(onPreferenceChange)
    }

    override fun setPreference(type: SetupChecklistPreference, value: Boolean) {
        when (type) {
            SetupChecklistPreference.ToolbarComplete ->
                settings.hasCompletedSetupStepToolbar = value

            SetupChecklistPreference.ThemeComplete ->
                settings.hasCompletedSetupStepTheme = value

            SetupChecklistPreference.ExtensionsComplete ->
                settings.hasCompletedSetupStepExtensions = value

            SetupChecklistPreference.ShowSetupChecklist ->
                settings.showSetupChecklist = value

            // no-ops
            // these preferences are handled elsewhere outside of the setup checklist feature.
            SetupChecklistPreference.SetToDefault,
            SetupChecklistPreference.SignIn,
            SetupChecklistPreference.InstallSearchWidget,
            -> {}
        }
    }

    @VisibleForTesting
    internal val onPreferenceChange =
        SharedPreferences.OnSharedPreferenceChangeListener { sharedPreferences, key ->
            val preference = SetupChecklistPreference.entries.find {
                context.getString(it.preferenceKey) == key
            }

            preference?.let {
                val preferenceValue = sharedPreferences.getBoolean(key, false)

                submitPreferenceUpdate(
                    SetupChecklistRepository.SetupChecklistPreferenceUpdate(
                        preference = preference,
                        value = preferenceValue,
                    ),
                )
            }
        }

    @VisibleForTesting
    internal fun submitPreferenceUpdate(
        preferenceUpdate: SetupChecklistRepository.SetupChecklistPreferenceUpdate,
    ) = coroutineScope.launch { _setupChecklistPreferenceUpdates.emit(preferenceUpdate) }
}
