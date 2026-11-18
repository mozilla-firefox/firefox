/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import android.icu.text.ListFormatter
import android.os.Build
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.text.intl.Locale
import mozilla.components.compose.base.text.value
import mozilla.components.compose.menu.R
import mozilla.components.compose.menu.data.MenuItemsGroup
import mozilla.components.compose.menu.ui.displayedInGrid

/**
 * Make a group of the customize menu screen a single node for accessibility services, reading the names of its items
 * then, for a group shown in the menu, its position among the shown groups. Its items and drag handle are not separate
 * nodes, and the dimmed items of hidden groups are not read as disabled.
 *
 * @param group The group.
 * @param groups The groups of the screen, as last saved.
 */
@Composable
internal fun Modifier.customizeMenuGroupSemantics(group: MenuItemsGroup, groups: CustomizeMenuGroups): Modifier {
    val label = group.itemNames()
    val shownIndex = groups.shown.indexOfFirst { it.id == group.id }
    val description =
        if (shownIndex == -1) {
            label
        } else {
            stringResource(R.string.mozac_menu_customize_shown_section, label, shownIndex + 1, groups.shown.size)
        }
    return this.clearAndSetSemantics { contentDescription = description }
}

/** The names of the items of this group shown in the customize menu screen, including the ones cut off from view. */
@Composable
private fun MenuItemsGroup.itemNames(): String {
    val namedItems =
        when (this) {
            is MenuItemsGroup.Grid -> items.displayedInGrid()
            is MenuItemsGroup.Row -> items
        }
    return formatList(namedItems.map { (it.contentDescription ?: it.title.text).value }, Locale.current.platformLocale)
}

/** The [names] as a list in the [locale], in its narrow style (without "and" in English) from API 33 on. */
private fun formatList(names: List<String>, locale: java.util.Locale): String =
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
        ListFormatter.getInstance(locale, ListFormatter.Type.AND, ListFormatter.Width.NARROW).format(names)
    } else {
        ListFormatter.getInstance(locale).format(names)
    }
