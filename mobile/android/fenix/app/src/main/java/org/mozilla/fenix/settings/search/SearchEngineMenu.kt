/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.search

import android.content.Context
import mozilla.components.browser.menu.BrowserMenuBuilder
import mozilla.components.browser.menu.item.SimpleBrowserMenuItem
import org.mozilla.fenix.R
import org.mozilla.fenix.theme.ThemeManager
import androidx.appcompat.R as appcompatR

class SearchEngineMenu(
    private val context: Context,
    private val allowDeletion: Boolean,
    private val isCustomSearchEngine: Boolean,
    private val onItemTapped: (Item) -> Unit = {},
) {
    sealed class Item {
        object Delete : Item()
        object Edit : Item()
    }

    val menuBuilder by lazy { BrowserMenuBuilder(menuItems) }

    private val menuItems by lazy {
        val items = mutableListOf<SimpleBrowserMenuItem>()

        if (isCustomSearchEngine) {
            items.add(
                SimpleBrowserMenuItem(
                    label = context.getString(R.string.search_engine_edit),
                ) {
                    onItemTapped.invoke(Item.Edit)
                },
            )
        }

        if (allowDeletion) {
            items.add(
                SimpleBrowserMenuItem(
                    context.getString(R.string.search_engine_delete),
                    textColorResource = ThemeManager.resolveAttribute(appcompatR.attr.colorError, context),
                ) {
                    onItemTapped.invoke(Item.Delete)
                },
            )
        }

        items
    }
}
