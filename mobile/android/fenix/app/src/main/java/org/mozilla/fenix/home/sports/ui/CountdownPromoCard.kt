/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.sports.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.translate
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.button.FilledButton
import org.mozilla.fenix.R
import org.mozilla.fenix.compose.PromoCard
import org.mozilla.fenix.compose.PromoCardColors
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * Card counting down to kickoff and prompting the user to pick a team to follow.
 *
 * @param dateInUtc ISO 8601 UTC date string (e.g. "2025-06-28T14:00:00Z") remaining until kickoff.
 * @param onViewSchedule Callback invoked when the "View schedule" button is tapped.
 * @param onDismiss Callback invoked when the close button is tapped.
 * @param modifier The [Modifier] to be applied to the card.
 */
@Composable
fun CountdownPromoCard(
    dateInUtc: String,
    onViewSchedule: () -> Unit,
    onDismiss: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val contentDescription = stringResource(R.string.sports_widget_close_content_description)
    val sportPainter = painterResource(R.drawable.firefox_sport)

    Box(
        modifier = modifier.background(
            color = MaterialTheme.colorScheme.surfaceContainerLowest,
            shape = MaterialTheme.shapes.large,
        ),
    ) {
        PromoCard(
            closeButtonContentDescription = contentDescription,
            onDismiss = onDismiss,
            modifier = Modifier
                .clip(MaterialTheme.shapes.large)
                .drawBehind {
                    val targetWidth = 150.dp.toPx()
                    val imgSize = sportPainter.intrinsicSize
                    val scaledSize = imgSize * (targetWidth / imgSize.width)
                    val leftOffset = if (layoutDirection == LayoutDirection.Rtl) 0f else size.width - scaledSize.width
                    translate(
                        left = leftOffset,
                        top = size.height - scaledSize.height,
                    ) {
                        with(sportPainter) { draw(scaledSize) }
                    }
                },
            title = {
                Text(
                    text = stringResource(R.string.sports_widget_countdown_to_world_cup),
                    style = FirefoxTheme.typography.headline7,
                    color = MaterialTheme.colorScheme.onSurface,
                    modifier = Modifier.padding(end = FirefoxTheme.layout.space.static500),
                )
            },
            message = {
                CountdownPill(dateInUtc = dateInUtc)
            },
            actions = {
                FilledButton(
                    text = stringResource(R.string.sports_widget_view_schedule),
                    onClick = onViewSchedule,
                )
            },
            contentSpacing = FirefoxTheme.layout.space.static200,
            colors = PromoCardColors.promoCardColors(
                backgroundColor = Color.Transparent,
            ),
        )
    }
}

@PreviewLightDark
@Composable
private fun CountdownPromoCardPreview() {
    FirefoxTheme {
        Surface {
            CountdownPromoCard(
                dateInUtc = "2026-06-11T19:00:00Z",
                onViewSchedule = {},
                onDismiss = {},
                modifier = Modifier.padding(16.dp),
            )
        }
    }
}
