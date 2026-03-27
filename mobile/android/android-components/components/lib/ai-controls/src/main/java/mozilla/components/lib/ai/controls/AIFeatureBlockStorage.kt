/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.ai.controls

import android.content.Context
import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.booleanPreferencesKey
import androidx.datastore.preferences.preferencesDataStore
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.map

internal interface AIFeatureBlockStorage {
    val isBlocked: Flow<Boolean>

    suspend fun setBlocked(isBlocked: Boolean)

    companion object {
        /**
         * Creates a simple in-memory implementation of [AIFeatureBlockStorage] for use in tests or previews.
         */
        fun inMemory(initialBlocked: Boolean = false): AIFeatureBlockStorage =
            InMemoryAiFeatureBlockStorage(initialBlocked)
    }
}

private class InMemoryAiFeatureBlockStorage(initialBlocked: Boolean) : AIFeatureBlockStorage {
    private val _isBlocked = MutableStateFlow(initialBlocked)
    override val isBlocked: Flow<Boolean> = _isBlocked

    override suspend fun setBlocked(isBlocked: Boolean) {
        _isBlocked.value = isBlocked
    }
}

internal fun AIFeatureBlockStorage.Companion.dataStore(context: Context): AIFeatureBlockStorage =
    DataStoreBackedAIFeatureBlockStorage(context.dataStore)

internal class DataStoreBackedAIFeatureBlockStorage(
    private val dataStore: DataStore<Preferences>,
) : AIFeatureBlockStorage {
    private val isBlockedKey = booleanPreferencesKey("is_blocked_key")

    override val isBlocked: Flow<Boolean>
        get() = dataStore.data.map { preferences ->
            preferences[isBlockedKey] ?: false
        }

    override suspend fun setBlocked(isBlocked: Boolean) {
        dataStore.updateData {
            it.toMutablePreferences().also { preferences ->
                preferences[isBlockedKey] = isBlocked
            }
        }
    }
}

private val Context.dataStore by preferencesDataStore(name = "ai_feature_block_storage")
