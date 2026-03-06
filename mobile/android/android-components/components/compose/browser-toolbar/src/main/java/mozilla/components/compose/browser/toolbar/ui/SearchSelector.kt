/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.browser.toolbar.ui

import android.graphics.drawable.Drawable
import android.view.SoundEffectConstants
import androidx.annotation.DrawableRes
import androidx.appcompat.content.res.AppCompatResources
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.ColorFilter
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.testTag
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import com.google.accompanist.drawablepainter.rememberDrawablePainter
import mozilla.components.compose.base.menu.CustomPlacementPopup
import mozilla.components.compose.base.menu.CustomPlacementPopupVerticalContent
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.browser.toolbar.R
import mozilla.components.compose.browser.toolbar.concept.BrowserToolbarTestTags.SEARCH_SELECTOR
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarEvent
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarMenu
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem
import mozilla.components.ui.icons.R as iconsR

/**
 * Search selector toolbar action.
 *
 * @param icon A [Drawable] to display in the search selector. If null will use [fallbackIcon] instead.
 * @param contentDescription The content description to use.
 * @param menu The [BrowserToolbarMenu] to show when the search selector is clicked.
 * @param modifier [Modifier] to apply to the background.
 * @param onInteraction Invoked for user interactions with the menu items.
 * @param fallbackIcon The resource id of the icon to use for this button if a [Drawable] is not provided.
 * @param shouldTint Whether the icon should have a default tint applied or not. Defaults to `true`.
 * @param onClick Optional [BrowserToolbarEvent] to be dispatched when this button is clicked.
 */
@Composable
fun SearchSelector(
    icon: Drawable?,
    contentDescription: String,
    menu: BrowserToolbarMenu,
    onInteraction: (BrowserToolbarEvent) -> Unit,
    modifier: Modifier = Modifier,
    @DrawableRes fallbackIcon: Int = iconsR.drawable.mozac_ic_search_24,
    shouldTint: Boolean = true,
    onClick: BrowserToolbarEvent? = null,
) {
    val view = LocalView.current
    val validIcon = icon ?: AppCompatResources.getDrawable(view.context, fallbackIcon)
    var showMenu by remember { mutableStateOf(false) }

    Card(
        modifier = modifier
            .padding(horizontal = 4.dp)
            .width(72.dp)
            .height(48.dp)
            .semantics(mergeDescendants = true) {
                this.contentDescription = contentDescription
                this.testTag = SEARCH_SELECTOR
            }
            .clickable {
                view.playSoundEffect(SoundEffectConstants.CLICK)
                showMenu = true
                onClick?.let {
                    onInteraction(onClick)
                }
            },
        shape = RoundedCornerShape(90.dp),
        colors = CardDefaults.cardColors(
            AcornTheme.colors.iconNormal,
        ),
        elevation = CardDefaults.elevatedCardElevation(
            defaultElevation = 0.dp,
        ),
    ) {
        Row(
            modifier = Modifier.fillMaxSize(),
            horizontalArrangement = Arrangement.Center,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Image(
                painter = rememberDrawablePainter(validIcon),
                contentDescription = null,
                modifier = Modifier
                    .size(24.dp)
                    .clip(RoundedCornerShape(999.dp)),
                contentScale = ContentScale.Crop,
                colorFilter = when (shouldTint) {
                    true -> ColorFilter.tint(AcornTheme.colors.iconInverse)
                    else -> null
                },
            )

            Spacer(modifier = Modifier.width(12.dp))

            Icon(
                painter = painterResource(R.drawable.mozac_compose_browser_toolbar_chevron_down_6),
                contentDescription = null,
                tint = AcornTheme.colors.iconInverse,
            )
        }

        CustomPlacementPopup(
            isVisible = showMenu,
            onDismissRequest = { showMenu = false },
        ) {
            CustomPlacementPopupVerticalContent {
                menu.toMenuItems().forEach { menuItem ->
                    menuItemComposable(menuItem) { event ->
                        showMenu = false
                        onInteraction(event)
                    }.invoke()
                }
            }
        }
    }
}

@PreviewLightDark
@Composable
private fun SearchSelectorPreview() {
    AcornTheme {
        SearchSelector(
            icon = null,
            contentDescription = "test",
            modifier = Modifier.background(color = MaterialTheme.colorScheme.surfaceDim),
            menu = object : BrowserToolbarMenu {
                override fun items() = emptyList<BrowserToolbarMenuItem>()
            },
            onInteraction = {},
        )
    }
}
