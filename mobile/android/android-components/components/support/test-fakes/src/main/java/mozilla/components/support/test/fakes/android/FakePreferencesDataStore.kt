/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.support.test.fakes.android

import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.mutablePreferencesOf
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.flow.update

/**
 * An in-memory [DataStore] of [Preferences] for tests.
 *
 * @param initialPreferences The preferences the store starts with.
 * @param readFailure When set, reading [data] throws it, as a store whose file cannot be read would.
 */
class FakePreferencesDataStore(
    initialPreferences: Preferences = mutablePreferencesOf(),
    private val readFailure: Throwable? = null,
) : DataStore<Preferences> {
    val preferences = MutableStateFlow(initialPreferences)

    override val data: Flow<Preferences> = readFailure?.let { failure -> flow { throw failure } } ?: preferences

    override suspend fun updateData(transform: suspend (t: Preferences) -> Preferences): Preferences {
        return preferences
            .update {
                transform(preferences.value).toMutablePreferences()
            }
            .let { preferences.value }
    }
}
