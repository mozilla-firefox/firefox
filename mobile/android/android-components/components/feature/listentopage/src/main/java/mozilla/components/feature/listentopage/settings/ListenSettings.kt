/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.settings

import android.content.Context
import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.booleanPreferencesKey
import androidx.datastore.preferences.core.emptyPreferences
import androidx.datastore.preferences.core.stringPreferencesKey
import androidx.datastore.preferences.preferencesDataStore
import java.io.IOException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.catch
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn

/**
 * Defines the contract for persisting and retrieving user settings related to the Listen to Page feature.
 *
 * Implementations are responsible for the underlying storage mechanism.
 *
 * @see [ListenSettings.Companion.inMemory] for a simple in-memory implementation suitable for testing or previews.
 */
interface ListenSettings {

    /** Whether the embedding app has made the feature available, for example through remote configuration. */
    val available: Boolean

    /** Whether the user has the feature turned on. */
    val enabled: StateFlow<ListenEnabledState>

    /**
     * Persists whether the user has the feature turned on.
     *
     * @param enabled `true` to turn the feature on.
     */
    suspend fun setEnabled(enabled: Boolean)

    /**
     * @param languageTag The language of the article.
     * @return The id of the voice saved for [languageTag], or `null` when there is none.
     */
    suspend fun getSelectedVoiceId(languageTag: String): String?

    /**
     * Persists the voice the user picked for one article language.
     *
     * @param languageTag The language of the article.
     * @param voiceId The id of the picked voice.
     */
    suspend fun setSelectedVoiceId(languageTag: String, voiceId: String)

    /**
     * Drops every saved voice when [enginePackageName] is not the engine the choices were made in, and records it as
     * the engine the choices from now on belong to.
     *
     * @param enginePackageName The package name of the engine the voices are about to be loaded from.
     */
    suspend fun clearSavedVoicesOnEngineChange(enginePackageName: String)

    companion object {
        /**
         * Creates a simple in-memory implementation of [ListenSettings].
         *
         * This implementation does not persist data across sessions and is intended for use in tests, Compose previews,
         * or other scenarios where a lightweight, non-persistent implementation is needed.
         *
         * @return An in-memory [ListenSettings] instance.
         */
        fun inMemory(
            enginePackageName: String? = null,
            voiceIds: Map<String, String> = emptyMap(),
            available: Boolean = true,
            enabled: ListenEnabledState = ListenEnabledState.Enabled,
        ): ListenSettings =
            object : ListenSettings {
                private val savedVoiceIds = voiceIds.toMutableMap()
                private var savedEnginePackageName = enginePackageName
                private val _enabled = MutableStateFlow(enabled)

                override val available: Boolean = available

                override val enabled: StateFlow<ListenEnabledState> = _enabled.asStateFlow()

                override suspend fun setEnabled(enabled: Boolean) {
                    _enabled.value = ListenEnabledState.from(enabled)
                }

                override suspend fun getSelectedVoiceId(languageTag: String): String? = savedVoiceIds[languageTag]

                override suspend fun setSelectedVoiceId(languageTag: String, voiceId: String) {
                    savedVoiceIds[languageTag] = voiceId
                }

                override suspend fun clearSavedVoicesOnEngineChange(enginePackageName: String) {
                    if (savedEnginePackageName != enginePackageName) {
                        savedVoiceIds.clear()
                        savedEnginePackageName = enginePackageName
                    }
                }
            }

        /**
         * Creates a [DataStore]-backed implementation of [ListenSettings].
         *
         * Data is persisted across sessions using [Preferences].
         *
         * @param context The application [Context] used to access the data store.
         * @param available Whether the feature is available. Availability is not persisted here, so the embedding app
         *   should supply it each time the settings are created.
         * @param scope The scope that keeps [ListenSettings.enabled] up to date.
         * @return A persistent [ListenSettings] instance backed by [DataStore].
         */
        fun dataStore(context: Context, available: Boolean, scope: CoroutineScope): ListenSettings =
            DataStoreBackedSettings(context.dataStore, available, scope)
    }
}

/** Whether the user has turned the Listen to Page feature on. */
enum class ListenEnabledState {
    /** The stored value has not been read yet. */
    Loading,

    /** The user has never turned the feature on or off. */
    Unset,

    /** The user has turned the feature on. */
    Enabled,

    /** The user has turned the feature off. */
    Disabled;

    internal companion object {
        fun from(enabled: Boolean?) =
            when (enabled) {
                null -> Unset
                true -> Enabled
                false -> Disabled
            }
    }
}

/** Whether the feature can be shown to the user right now. It is only `true` once the user has turned it on. */
fun ListenSettings.shouldShow(): Boolean = available && enabled.value == ListenEnabledState.Enabled

internal class DataStoreBackedSettings(
    private val dataStore: DataStore<Preferences>,
    override val available: Boolean,
    scope: CoroutineScope,
) : ListenSettings {
    private val enginePackageNameKey = stringPreferencesKey("engine_package_name_key")
    private val enabledKey = booleanPreferencesKey("enabled_key")

    // The hot flow will start in the Loading state, but then a null read from the preference file on first run
    // will update the state to Unset.
    override val enabled: StateFlow<ListenEnabledState> =
        dataStore.data
            .catch { exception ->
                if (exception is IOException) {
                    emit(emptyPreferences())
                } else {
                    throw exception
                }
            }
            .map { preferences -> ListenEnabledState.from(preferences[enabledKey]) }
            .stateIn(scope, SharingStarted.Eagerly, initialValue = ListenEnabledState.Loading)

    override suspend fun setEnabled(enabled: Boolean) {
        dataStore.updateData {
            it.toMutablePreferences().also { preferences ->
                preferences[enabledKey] = enabled
            }
        }
    }

    override suspend fun getSelectedVoiceId(languageTag: String): String? =
        dataStore.data.first()[selectedVoiceKey(languageTag)]

    override suspend fun setSelectedVoiceId(languageTag: String, voiceId: String) {
        dataStore.updateData {
            it.toMutablePreferences().also { preferences ->
                preferences[selectedVoiceKey(languageTag)] = voiceId
            }
        }
    }

    override suspend fun clearSavedVoicesOnEngineChange(enginePackageName: String) {
        dataStore.updateData { stored ->
            if (stored[enginePackageNameKey] == enginePackageName) {
                return@updateData stored
            }
            stored.toMutablePreferences().also { preferences ->
                preferences
                    .asMap()
                    .keys
                    .filter { it.name.startsWith(SELECTED_VOICE_KEY_PREFIX) }
                    .forEach { preferences -= it }
                preferences[enginePackageNameKey] = enginePackageName
            }
        }
    }

    private fun selectedVoiceKey(languageTag: String) = stringPreferencesKey("$SELECTED_VOICE_KEY_PREFIX$languageTag")
}

private val Context.dataStore: DataStore<Preferences> by preferencesDataStore(name = "listen_to_page_settings")

private const val SELECTED_VOICE_KEY_PREFIX = "selected_voice_"
