/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.ipprotection.storage

import android.content.Context
import android.content.Context.MODE_PRIVATE
import androidx.core.content.edit

internal const val SHARED_PREF_NAME = "mozac_feature_ip_protection"
internal const val SELECTED_LOCATION_KEY = "mozac_ip_protection_selected_location"

/** Repository for persisting the user's selected IP protection location. */
interface IPProtectionLocationRepository {
    /** Returns the stored location code, or null if none has been saved. */
    suspend fun getSelectedLocationCode(): String?

    /** Persists [code] as the selected location, or clears if null. */
    suspend fun setSelectedLocationCode(code: String?)
}

/** Default [IPProtectionLocationRepository] implementation. */
class DefaultIPProtectionLocationRepository(context: Context) : IPProtectionLocationRepository {

    private val prefs by lazy { context.getSharedPreferences(SHARED_PREF_NAME, MODE_PRIVATE) }

    override suspend fun getSelectedLocationCode(): String? = prefs.getString(SELECTED_LOCATION_KEY, null)

    override suspend fun setSelectedLocationCode(code: String?) = prefs.edit { putString(SELECTED_LOCATION_KEY, code) }
}
