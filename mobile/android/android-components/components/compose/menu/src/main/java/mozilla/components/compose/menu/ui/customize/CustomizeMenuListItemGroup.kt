/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawWithCache
import androidx.compose.ui.graphics.BlendMode
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.CompositingStrategy
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.layout.layout
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.tooling.preview.PreviewParameter
import kotlin.math.roundToInt
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.menu.data.MenuItem
import mozilla.components.compose.menu.ui.MenuListItem
import mozilla.components.compose.menu.ui.getListItemShape
import mozilla.components.compose.menu.ui.menuListGroupSemantics
import mozilla.components.compose.menu.ui.utils.MenuListItemsParameterProvider

/** Maximum number of items shown. */
private const val COLLAPSED_ITEMS_COUNT = 3

/** Fraction of the last item which is visible, when there are more items than shown, as in the design. */
private const val PEEK_RATIO = 4f / 7f

/**
 * Display a list of [MenuItem]s as a vertical list, showing only the first [COLLAPSED_ITEMS_COUNT] of them, the last
 * only partly and fading out. Lists with fewer items are shown in full.
 *
 * @param items The list of [MenuItem]s to display.
 * @param modifier The modifier to apply to this layout.
 * @param showDivider Whether to show dividers between items.
 * @param contentAlpha Alpha applied to the whole list.
 */
@Composable
internal fun CustomizeMenuListItemGroup(
    items: List<MenuItem>,
    modifier: Modifier = Modifier,
    showDivider: Boolean = false,
    contentAlpha: Float = 1f,
) {
    val shownItems = remember(items) { items.take(COLLAPSED_ITEMS_COUNT) }
    val isCollapsed = items.size >= COLLAPSED_ITEMS_COUNT

    Column(
        modifier =
            modifier
                .let { if (isCollapsed) it.fadeBottomGradient(contentAlpha) else it.alpha(contentAlpha) }
                .clip(shape = MaterialTheme.shapes.extraLarge)
                .menuListGroupSemantics(shownItems.size),
        verticalArrangement = Arrangement.spacedBy(AcornTheme.layout.space.static25),
    ) {
        shownItems.forEachIndexed { index, item ->
            if (index > 0 && showDivider) {
                HorizontalDivider(
                    modifier = Modifier.padding(horizontal = AcornTheme.layout.space.static50),
                    color = MaterialTheme.colorScheme.outlineVariant,
                )
            }
            MenuListItem(
                title = item.title,
                modifier =
                    Modifier.let { if (isCollapsed && index == shownItems.lastIndex) it.peekOnly() else it }
                        .clip(getListItemShape(itemIndex = index, totalItems = shownItems.size)),
                index = index,
                onClickEvent = null,
                onInteraction = {},
                contentDescription = item.contentDescription,
                summary = item.summary,
                icon = item.icon,
                state = item.state,
            )
        }
    }
}

/**
 * Report only the [PEEK_RATIO] of the height of this, with the rest of its content overflowing the reported bounds for
 * an ancestor to clip.
 */
private fun Modifier.peekOnly() = layout { measurable, constraints ->
    val placeable = measurable.measure(constraints)
    layout(placeable.width, (placeable.height * PEEK_RATIO).roundToInt()) { placeable.place(0, 0) }
}

/** Fade out the bottom of this, with the given [alpha] applied to the result. */
@Composable
private fun Modifier.fadeBottomGradient(alpha: Float): Modifier {
    val fadeHeight = AcornTheme.layout.space.static200
    return this.graphicsLayer(alpha = alpha, compositingStrategy = CompositingStrategy.Offscreen).drawWithCache {
        val brush =
            Brush.verticalGradient(
                colors = listOf(Color.Black, Color.Transparent),
                startY = maxOf(0f, size.height - fadeHeight.toPx()),
                endY = size.height,
            )
        onDrawWithContent {
            drawContent()
            drawRect(brush = brush, blendMode = BlendMode.DstIn)
        }
    }
}

@PreviewLightDark
@Composable
private fun CustomizeMenuListItemGroupPreview(
    @PreviewParameter(MenuListItemsParameterProvider::class) items: List<MenuItem>
) = AcornTheme {
    CustomizeMenuListItemGroup(items)
}

@Preview(fontScale = 2f)
@Composable
private fun CustomizeMenuListItemGroupLargeFontPreview(
    @PreviewParameter(MenuListItemsParameterProvider::class) items: List<MenuItem>
) = AcornTheme {
    CustomizeMenuListItemGroup(items)
}
