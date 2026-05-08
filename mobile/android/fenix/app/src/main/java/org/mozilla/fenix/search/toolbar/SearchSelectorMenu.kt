/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.search.toolbar

import android.content.Context
import androidx.appcompat.content.res.AppCompatResources
import mozilla.components.browser.menu2.BrowserMenuController
import mozilla.components.browser.state.search.SearchEngine
import mozilla.components.concept.menu.MenuController
import mozilla.components.concept.menu.MenuStyle
import mozilla.components.concept.menu.candidate.DecorativeTextMenuCandidate
import mozilla.components.concept.menu.candidate.DrawableMenuIcon
import mozilla.components.concept.menu.candidate.MenuCandidate
import mozilla.components.concept.menu.candidate.TextMenuCandidate
import mozilla.components.support.ktx.android.content.getColorFromAttr
import mozilla.components.support.ktx.android.util.dpToPx
import org.mozilla.fenix.R
import com.google.android.material.R as materialR
import mozilla.components.ui.icons.R as iconsR

typealias MozSearchEngine = SearchEngine

/**
 * A popup menu composed of [SearchSelectorMenu.Item] objects.
 *
 * @param context [Context] used for various Android interactions.
 * @param interactor [ToolbarInteractor] for handling menu item interactions.
 */
class SearchSelectorMenu(
    private val context: Context,
    private val interactor: SearchSelectorInteractor,
) {

    /**
     * Items that will appear in the search selector menu.
     */
    sealed class Item {
        /**
         * The menu item to navigate to the search settings.
         */
        object SearchSettings : Item()

        /**
         * The menu item to display a search engine.
         *
         * @property searchEngine The [SearchEngine] that was selected.
         */
        data class SearchEngine(val searchEngine: MozSearchEngine) : Item()
    }

    val menuController: MenuController by lazy {
        BrowserMenuController(
            style = MenuStyle(
                // Adjusting the menu to have 4dp spacing. By default, search_selector has horizontal
                // spacing of 8dp, and vertical spacing with the container view of 6dp.
                horizontalOffset = (-4).dpToPx(context.resources.displayMetrics),
                verticalOffset = (-2).dpToPx(context.resources.displayMetrics),
                completelyOverlap = true,
            ),
        )
    }

    internal fun menuItems(searchEngines: List<MenuCandidate>): List<MenuCandidate> {
        val headerCandidate = DecorativeTextMenuCandidate(
            text = context.getString(R.string.search_header_menu_item_2),
        )
        val settingsCandidate = TextMenuCandidate(
            text = context.getString(R.string.search_settings_menu_item),
            start = DrawableMenuIcon(
                drawable = AppCompatResources.getDrawable(
                    context,
                    iconsR.drawable.mozac_ic_settings_24,
                ),
                tint = context.getColorFromAttr(materialR.attr.colorOnSurface),
            ),
        ) {
            interactor.onMenuItemTapped(Item.SearchSettings)
        }
        return listOf(headerCandidate) + searchEngines + listOf(settingsCandidate)
    }
}
