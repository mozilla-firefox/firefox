/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.datastore

import android.content.Context
import androidx.datastore.core.DataStore
import androidx.datastore.core.IOException
import androidx.datastore.core.handlers.ReplaceFileCorruptionHandler
import androidx.datastore.dataStore
import androidx.datastore.preferences.core.MutablePreferences
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.preferencesDataStore

/**
 * Application / process unique [DataStore] for IO operations related to Pocket recommended stories selected categories.
 */
internal val Context.pocketStoriesSelectedCategoriesDataStore: DataStore<SelectedPocketStoriesCategories> by
    dataStore(
        fileName = "pocket_recommendations_selected_categories.pb",
        serializer = SelectedPocketStoriesCategorySerializer,
    )

/** Application / process unique [DataStore] for the user customized browser menu structure. */
internal val Context.customizedBrowserMenuInfoDataStore: DataStore<CustomizedMenuRecord> by
    dataStore(
        fileName = "browser_menu_customization.pb",
        serializer = CustomizedMenuRecordSerializer,
        corruptionHandler = ReplaceFileCorruptionHandler { CustomizedMenuRecord.getDefaultInstance() },
    )

/** [DataStore] for accessing user preferences in Fenix. */
val Context.preferencesDataStore: DataStore<Preferences> by preferencesDataStore(name = "fenix_preferences")

/**
 * Helper function used to safely edit a Preferences DataStore. If an IOException is thrown, [onError] will be invoked.
 *
 * @param onError Invoked when an IOException is thrown after attempting to edit the DataStore's preferences.
 * @param transform block which accepts MutablePreferences that contains all the preferences currently in DataStore.
 *   Changes to this MutablePreferences object will be persisted once transform completes.
 */
suspend fun DataStore<Preferences>.editOrCatch(
    onError: (IOException) -> Unit,
    transform: suspend (MutablePreferences) -> Unit,
) {
    try {
        edit(transform)
    } catch (exception: IOException) {
        onError(exception)
    }
}
