/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.tooling.preview.PreviewLightDark
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.menu.data.BannerMenuItem
import mozilla.components.compose.menu.data.MenuItem
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.ui.MenuGridItem
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.compose.menu.ui.MenuItemState
import mozilla.components.compose.menu.ui.displayedInGrid
import mozilla.components.compose.menu.ui.getGridItemShape
import mozilla.components.compose.menu.ui.menuGridGroupSemantics
import mozilla.components.ui.icons.R as iconsR

/**
 * Display a grid of [MenuItem]s using [MenuGridItem].
 *
 * @param items The list of [MenuItem]s to display. Items without an icon and [BannerMenuItem]s, which need the full
 *   width of the menu, are not shown.
 * @param modifier The modifier to apply to this layout.
 * @param contentAlpha Alpha applied to content.
 */
@Composable
internal fun CustomizeMenuGridContainer(
    items: List<MenuItem>,
    modifier: Modifier = Modifier,
    contentAlpha: Float = 1f,
) {
    val displayedItems = remember(items) { items.displayedInGrid() }
    Row(
        modifier =
            modifier
                .fillMaxWidth()
                .height(IntrinsicSize.Min)
                .clip(MaterialTheme.shapes.extraLarge)
                .menuGridGroupSemantics(displayedItems.size),
        horizontalArrangement = Arrangement.spacedBy(AcornTheme.layout.space.static25),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        displayedItems.forEachIndexed { index, item ->
            val icon = item.icon ?: return@forEachIndexed

            MenuGridItem(
                title = item.title.text,
                icon = icon,
                onClickEvent = null,
                onInteraction = {},
                modifier = Modifier.weight(1f).fillMaxHeight().alpha(contentAlpha),
                contentDescription = item.contentDescription,
                index = index,
                state = item.state,
                showContainer = true,
                shape = getGridItemShape(index, displayedItems.size),
            )
        }
    }
}

@PreviewLightDark
@Composable
private fun MenuGridContainerPreview() = AcornTheme {
    CustomizeMenuGridContainer(
        items =
            listOf(
                StandardMenuItem(
                    title = MenuItemTitle(Text.String("Back")),
                    contentDescription = Text.String("Back"),
                    icon = MenuItemIconRes(iconsR.drawable.mozac_ic_back_24),
                    state = MenuItemState.DISABLED,
                ),
                StandardMenuItem(
                    title = MenuItemTitle(Text.String("Forward")),
                    contentDescription = Text.String("Forward"),
                    icon = MenuItemIconRes(iconsR.drawable.mozac_ic_forward_24),
                    state = MenuItemState.DISABLED,
                ),
                StandardMenuItem(
                    title = MenuItemTitle(Text.String("Share")),
                    contentDescription = Text.String("Share"),
                    icon = MenuItemIconRes(iconsR.drawable.mozac_ic_share_android_24),
                ),
                StandardMenuItem(
                    title = MenuItemTitle(Text.String("Refresh")),
                    contentDescription = Text.String("Refresh"),
                    icon = MenuItemIconRes(iconsR.drawable.mozac_ic_arrow_clockwise_24),
                ),
            )
    )
}
