/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.selectors

import org.mozilla.fenix.ui.efficiency.helpers.Selector
import org.mozilla.fenix.ui.efficiency.helpers.SelectorContainer
import org.mozilla.fenix.ui.efficiency.helpers.SelectorGroup
import org.mozilla.fenix.ui.efficiency.helpers.SelectorStrategy

object CollectionsSelectors : SelectorContainer {
    enum class Group : SelectorGroup {
        COLLECTION_ITEM,
        COLLECTION_TAB_ITEM,
        COLLECTION_CONTROLS,
        COLLECTION_THREE_DOT_MENU,
    }

    @Suppress("FunctionName")
    fun COLLECTION_WITH_TITLE(collectionTitle: String = "") =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = collectionTitle,
            description = "Home screen collection with title: $collectionTitle ",
            groups = setOf(Group.COLLECTION_ITEM),
        )

    @Suppress("FunctionName")
    fun COLLECTION_TAB_WITH_TITLE(tabTitle: String = "") =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = tabTitle,
            description = "Collection tab with title: $tabTitle ",
            groups = setOf(Group.COLLECTION_TAB_ITEM),
        )

    @Suppress("FunctionName")
    fun COLLECTION_TAB_WITH_URL(url: String = "") =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT_SUBSTRING,
            value = url,
            description = "Collection tab with url: $url",
            groups = setOf(Group.COLLECTION_TAB_ITEM),
        )

    val COLLECTION_ITEM_REMOVE_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_DESCRIPTION_CONTAINS,
            value = "Remove tab from collection",
            description = "Collection item remove button",
            groups = setOf(Group.COLLECTION_CONTROLS),
        )

    val COLLECTION_TAB_SHARE_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_DESCRIPTION_CONTAINS,
            value = "Share",
            description = "Collection tab share button",
            groups = setOf(Group.COLLECTION_CONTROLS),
        )

    val COLLECTION_TAB_MAIN_MENU_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_DESCRIPTION_CONTAINS,
            value = "Collection menu",
            description = "Collection tab main menu button",
            groups = setOf(Group.COLLECTION_CONTROLS),
        )

    val OPEN_TABS_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = "Open tabs",
            description = "Collection tab Open tabs menu button",
            groups = setOf(Group.COLLECTION_THREE_DOT_MENU),
        )

    val DELETE_COLLECTION_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = "Delete collection",
            description = "Collection tab Delete Collection menu button",
            groups = setOf(Group.COLLECTION_THREE_DOT_MENU),
        )
}
