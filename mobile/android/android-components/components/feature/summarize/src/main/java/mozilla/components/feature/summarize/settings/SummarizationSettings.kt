/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize.settings

import android.content.Context
import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.booleanPreferencesKey
import androidx.datastore.preferences.core.intPreferencesKey
import androidx.datastore.preferences.preferencesDataStore
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.map

/**
 * Defines the contract for persisting and retrieving user settings related to
 * the summarization feature.
 *
 * Implementations are responsible for the underlying storage mechanism.
 *
 * @see [SummarizationSettings.Companion.inMemory] for a simple in-memory implementation
 * suitable for testing or previews.
 */
interface SummarizationSettings {

    /**
     * @return A [Flow] emitting the user's current preference for whether the summarization
     * feature is enabled.
     */
    suspend fun getFeatureEnabledUserStatus(): Flow<Boolean>

    /**
     * Persists the user's preference for whether the summarization feature is enabled.
     *
     * @param newValue `true` to enable the feature, `false` to disable it.
     */
    suspend fun setFeatureEnabledUserStatus(newValue: Boolean)

    /**
     * @return A [Flow] emitting the user's current preference for whether the shake gesture
     * is enabled.
     */
    suspend fun getGestureEnabledUserStatus(): Flow<Boolean>

    /**
     * Persists the user's preference for whether the shake gesture is enabled.
     *
     * @param newValue `true` to enable the gesture, `false` to disable it.
     */
    suspend fun setGestureEnabledUserStatus(newValue: Boolean)

    /**
     * @return A [Flow] emitting whether the user has consented to the shake gesture interaction.
     */
    suspend fun getHasConsentedToShake(): Flow<Boolean>

    /**
     * Persists the user's consent status for the shake gesture interaction.
     *
     * @param newValue `true` to indicate the user has consented, `false` to revoke consent.
     */
    suspend fun setHasConsentedToShake(newValue: Boolean)

    /**
     * Increments the count of times the user has rejected the shake consent prompt.
     */
    suspend fun incrementShakeConsentRejectedCount()

    companion object {
        /**
         * Creates a simple in-memory implementation of [SummarizationSettings].
         *
         * This implementation does not persist data across sessions and is intended
         * for use in tests, Compose previews, or other scenarios where a lightweight,
         * non-persistent implementation is needed.
         *
         * @param hasConsentedToShake The initial value for the shake consent setting.
         * Defaults to `false`.
         * @return An in-memory [SummarizationSettings] instance.
         */
        fun inMemory(
            isFeatureEnabled: Boolean = false,
            isGestureEnabled: Boolean = false,
            hasConsentedToShake: Boolean = false,
            shakeConsentRejectedCount: Int = 0,
        ) = object : SummarizationSettings {
            var isFeatureEnabledFlow =
                MutableStateFlow(isFeatureEnabled)
            var isGestureEnabledFlow =
                MutableStateFlow(isGestureEnabled)
            var hasConsentedToShakeFlow =
                MutableStateFlow(hasConsentedToShake)
            private var shakeConsentRejectedCount = 0

            override suspend fun getFeatureEnabledUserStatus(): Flow<Boolean> = isFeatureEnabledFlow

            override suspend fun setFeatureEnabledUserStatus(newValue: Boolean) {
                isFeatureEnabledFlow.emit(newValue)
            }

            override suspend fun getGestureEnabledUserStatus(): Flow<Boolean> = isGestureEnabledFlow

            override suspend fun setGestureEnabledUserStatus(newValue: Boolean) {
                isGestureEnabledFlow.emit(newValue)
            }

            override suspend fun getHasConsentedToShake(): Flow<Boolean> = hasConsentedToShakeFlow

            override suspend fun setHasConsentedToShake(newValue: Boolean) {
                hasConsentedToShakeFlow.emit(newValue)
            }

            override suspend fun incrementShakeConsentRejectedCount() {
                this.shakeConsentRejectedCount++
                if (this.shakeConsentRejectedCount >= MAX_SHAKE_CONSENT_REJECTION) {
                    isGestureEnabledFlow.value = false
                }
            }
        }

        /**
         * Creates a [DataStore]-backed implementation of [SummarizationSettings].
         *
         * Data is persisted across sessions using [Preferences].
         *
         * @param context The application [Context] used to access the data store.
         * @return A persistent [SummarizationSettings] instance backed by [DataStore].
         */
        fun dataStore(
            context: Context,
        ): SummarizationSettings = DataStoreBackedSettings(context.dataStore)
    }
}

internal class DataStoreBackedSettings(private val dataStore: DataStore<Preferences>) : SummarizationSettings {
    private val featureEnabledKey = booleanPreferencesKey("feature_enabled_user_status_key")
    private val gestureEnabledKey = booleanPreferencesKey("gesture_enabled_user_status_key")
    private val hasConsentedToShakeKey = booleanPreferencesKey("has_consented_to_shake_key")
    private val shakeConsentRejectedCountKey = intPreferencesKey("shake_consent_rejected_count_key")

    override suspend fun getFeatureEnabledUserStatus(): Flow<Boolean> = dataStore.data.map { preferences ->
        preferences[featureEnabledKey] ?: true
    }

    override suspend fun setFeatureEnabledUserStatus(newValue: Boolean) {
        dataStore.updateData {
            it.toMutablePreferences().also { preferences ->
                preferences[featureEnabledKey] = newValue
            }
        }
    }

    override suspend fun getGestureEnabledUserStatus(): Flow<Boolean> = dataStore.data.map { preferences ->
        preferences[gestureEnabledKey] ?: true
    }

    override suspend fun setGestureEnabledUserStatus(newValue: Boolean) {
        dataStore.updateData {
            it.toMutablePreferences().also { preferences ->
                preferences[gestureEnabledKey] = newValue
            }
        }
    }

    override suspend fun getHasConsentedToShake(): Flow<Boolean> = dataStore.data.map { preferences ->
        preferences[hasConsentedToShakeKey] ?: false
    }

    override suspend fun setHasConsentedToShake(newValue: Boolean) {
        dataStore.updateData {
            it.toMutablePreferences().also { preferences ->
                preferences[hasConsentedToShakeKey] = newValue
            }
        }
    }

    override suspend fun incrementShakeConsentRejectedCount() {
        dataStore.updateData {
            it.toMutablePreferences().also { preferences ->
                val updatedCount = (preferences[shakeConsentRejectedCountKey] ?: 0) + 1
                preferences[shakeConsentRejectedCountKey] = updatedCount
                if (updatedCount >= MAX_SHAKE_CONSENT_REJECTION) {
                    preferences[gestureEnabledKey] = false
                }
            }
        }
    }
}

private val Context.dataStore: DataStore<Preferences> by preferencesDataStore(name = "summarization_feature_settings")
private const val MAX_SHAKE_CONSENT_REJECTION = 3
