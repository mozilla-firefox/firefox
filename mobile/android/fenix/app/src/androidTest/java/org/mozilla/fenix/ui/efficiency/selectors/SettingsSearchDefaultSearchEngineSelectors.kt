/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.selectors

import org.mozilla.fenix.R
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.settings.search.ui.SearchEngineDropdownMenuTestTag
import org.mozilla.fenix.ui.efficiency.helpers.Selector
import org.mozilla.fenix.ui.efficiency.helpers.SelectorContainer
import org.mozilla.fenix.ui.efficiency.helpers.SelectorGroup
import org.mozilla.fenix.ui.efficiency.helpers.SelectorStrategy

object SettingsSearchDefaultSearchEngineSelectors : SelectorContainer {
    enum class Group : SelectorGroup {
        DEFAULT_SEARCH_ENGINES
    }

    val DEFAULT_SEARCH_ENGINE_TITLE =
        navigationToolbarTitle(
            title = getStringResource(R.string.preferences_default_search_engine),
            description = "Default search engine toolbar title",
        )

    fun DEFAULT_SEARCH_ENGINE_OPTION(engineName: String = "") =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID_AND_TEXT,
            value = "engine_text",
            secondaryValue = engineName,
            description = "Default search engine option: $engineName",
            groups = setOf(Group.DEFAULT_SEARCH_ENGINES),
        )

    // The "Add search engine" row at the bottom of the default-engine list; opens the custom-engine form.
    val ADD_SEARCH_ENGINE_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = getStringResource(R.string.search_engine_add_custom_search_engine_title),
            description = "Add search engine button",
        )

    // Overflow (three-dot) button on a custom engine's row. It appears in both the normal and private browsing lists,
    // so take the first.
    val ENGINE_OVERFLOW_MENU =
        Selector(
            strategy = SelectorStrategy.COMPOSE_ON_ALL_NODES_BY_TAG_ON_FIRST,
            value = SearchEngineDropdownMenuTestTag.OVERFLOW_BUTTON,
            description = "Custom engine overflow menu button",
        )

    val OVERFLOW_EDIT_ITEM =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = SearchEngineDropdownMenuTestTag.EDIT,
            description = "Edit item in the custom engine overflow menu",
        )
}
