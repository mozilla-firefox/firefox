/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui

import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.layout.layout
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import kotlin.math.roundToInt
import mozilla.components.compose.base.button.IconButton
import mozilla.components.compose.base.button.TextButton
import mozilla.components.compose.base.text.Text as AcornText
import mozilla.components.compose.base.text.value
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.menu.R
import mozilla.components.compose.menu.data.MenuItemSummary
import mozilla.components.ui.icons.R as iconsR

private val BANNER_MIN_HEIGHT = 76.dp
private const val ILLUSTRATION_MAX_WIDTH_FRACTION = 0.5f

/**
 * A full-width promotional banner shown in the menu, that has a call-to-action button.
 *
 * @param title The title of the banner.
 * @param summary The description of the banner.
 * @param icon The illustration to show in the banner.
 * @param actionButtonText The text of the call-to-action button.
 * @param onClick Invoked when the user taps the banner or its call-to-action button.
 * @param onDismiss Invoked when the user taps the dismiss icon (“X”).
 * @param modifier [Modifier] to be applied to the layout.
 */
@Composable
internal fun MenuBanner(
    title: AcornText,
    summary: MenuItemSummary,
    icon: MenuItemIcon,
    actionButtonText: AcornText,
    onClick: () -> Unit,
    onDismiss: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val isRtl = LocalLayoutDirection.current == LayoutDirection.Rtl

    Surface(
        onClick = onClick,
        modifier = modifier.fillMaxWidth().heightIn(min = BANNER_MIN_HEIGHT),
        shape = MaterialTheme.shapes.large,
        color = MaterialTheme.colorScheme.secondaryContainer,
    ) {
        Box {
            Row(
                modifier = Modifier.padding(end = AcornTheme.layout.space.static50),
                verticalAlignment = Alignment.Bottom,
            ) {
                Column(
                    modifier =
                        Modifier.weight(1f)
                            .align(Alignment.CenterVertically)
                            .padding(top = AcornTheme.layout.space.static200),
                    verticalArrangement = Arrangement.spacedBy(AcornTheme.layout.space.static50),
                ) {
                    Column(modifier = Modifier.padding(start = AcornTheme.layout.space.static200)) {
                        Text(
                            text = title.value,
                            style = AcornTheme.typography.headline7,
                            color = MaterialTheme.colorScheme.onSurface,
                            overflow = TextOverflow.Ellipsis,
                            maxLines = 2,
                        )

                        Text(
                            text = summary.text.value,
                            style = AcornTheme.typography.body2,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            overflow = TextOverflow.Ellipsis,
                            maxLines = summary.maxLines,
                        )
                    }

                    TextButton(
                        text = actionButtonText.value,
                        onClick = onClick,
                        modifier = Modifier.padding(start = AcornTheme.layout.space.static50),
                    )
                }

                Image(
                    painter = icon.painter,
                    contentDescription = null,
                    modifier =
                        Modifier.maxWidthFraction(ILLUSTRATION_MAX_WIDTH_FRACTION)
                            .padding(end = AcornTheme.layout.space.static200)
                            .graphicsLayer(scaleX = if (isRtl) -1f else 1f),
                )
            }

            MenuBannerDismissButton(onDismiss = onDismiss)
        }
    }
}

/**
 * The button dismissing a menu banner, shown at the top end of it.
 *
 * @param onDismiss Invoked when the user taps the button.
 */
@Composable
private fun BoxScope.MenuBannerDismissButton(onDismiss: () -> Unit) {
    IconButton(
        onClick = onDismiss,
        contentDescription = stringResource(R.string.mozac_menu_banner_dismiss_description),
        modifier =
            Modifier.align(Alignment.TopEnd).size(AcornTheme.layout.size.static600).semantics(
                mergeDescendants = true
            ) {},
    ) {
        Icon(
            painter = painterResource(id = iconsR.drawable.mozac_ic_cross_20),
            contentDescription = null,
            modifier =
                Modifier.padding(
                        top = AcornTheme.layout.space.static100,
                        end = AcornTheme.layout.space.static150,
                    )
                    .align(Alignment.TopEnd),
            tint = MaterialTheme.colorScheme.secondary,
        )
    }
}

/** Limits the width of the content to [fraction] of the width offered to it. */
internal fun Modifier.maxWidthFraction(fraction: Float) = layout { measurable, constraints ->
    val maxWidth =
        if (constraints.hasBoundedWidth) {
            (constraints.maxWidth * fraction).roundToInt()
        } else {
            constraints.maxWidth
        }
    val placeable = measurable.measure(constraints.copy(minWidth = 0, maxWidth = maxWidth))
    layout(placeable.width, placeable.height) { placeable.placeRelative(0, 0) }
}

@PreviewLightDark
@Composable
private fun MenuBannerPreview() {
    AcornTheme {
        Surface(color = MaterialTheme.colorScheme.surfaceContainer) {
            MenuBanner(
                title = AcornText.String("Banner title"),
                summary = MenuItemSummary(text = AcornText.String("Banner description"), maxLines = 2),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_globe_24),
                actionButtonText = AcornText.String("Button"),
                onClick = {},
                onDismiss = {},
                modifier = Modifier.padding(AcornTheme.layout.space.static200),
            )
        }
    }
}
