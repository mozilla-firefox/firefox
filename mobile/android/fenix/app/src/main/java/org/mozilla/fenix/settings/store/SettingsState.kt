/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.store

import androidx.annotation.StringRes
import mozilla.components.lib.state.State
import org.mozilla.fenix.R
import org.mozilla.fenix.settings.ui.TextValue

/**
 * State for the Settings screen following MVI pattern.
 *
 * @property isLoading Whether the settings are currently loading and not ready for display.
 * @property settingsItems List of the settings items to display.
 * @property searchQuery Current search query (if searching).
 * @property allSearchableItems All settings items that can be searched for.
 * @property filteredItems Filtered settings items based on search (if searching).
 */
data class SettingsState(
    val currentDestinationId: Int = R.string.settings,
    val isLoading: Boolean = true,
    val settingsItems: List<SettingsItem> = emptyList(),
    val searchQuery: String = "",
    val allSearchableItems: List<SettingsItem> = emptyList(),
    val filteredItems: List<SettingsItem> = emptyList(),
) : State

/**
 * Generic setting item.
 * Items are identified by their [title]'s resource ID for uniqueness.
 *
 * @property title The title of the setting item.
 * @property summary Optional summary of the setting item.
 * @property icon Optional icon for the setting item.
 * @property isVisible Whether the setting item is visible.
 * @property isEnabled Whether the setting item is enabled.
 */
sealed class SettingsItem {
    abstract val title: TextValue
    abstract val summary: TextValue?
    abstract val icon: Int?
    abstract val isVisible: Boolean
    abstract val isEnabled: Boolean

    /**
     * General settings item - a simple clickable preference.
     */
    data class SimplePreference(
        override val title: TextValue,
        override val summary: TextValue? = null,
        override val icon: Int? = null,
        override val isVisible: Boolean = true,
        override val isEnabled: Boolean = true,
        val id: Int = title.resId,
        val keywords: List<String> = emptyList(),
    ) : SettingsItem()

    /**
     * A toggle/switch preference.
     */
    data class TogglePreference(
        override val title: TextValue,
        override val summary: TextValue? = null,
        override val icon: Int? = null,
        override val isVisible: Boolean = true,
        override val isEnabled: Boolean = true,
        val isChecked: Boolean,
        @param:StringRes val preferenceKey: Int,
        val id: Int = title.resId,
        val keywords: List<String> = emptyList(),
    ) : SettingsItem()

    /**
     * Category/group header that can contain child settings items.
     *
     * @property items List of child settings items within this category.
     */
    data class Category(
        override val title: TextValue,
        override val summary: TextValue? = null,
        override val icon: Int? = null,
        override val isVisible: Boolean = true,
        override val isEnabled: Boolean = true,
        val items: List<SettingsItem> = emptyList(),
    ) : SettingsItem()
}
