/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.browser.tabstrip

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import org.mozilla.fenix.theme.FirefoxTheme

private val cardShape = RoundedCornerShape(999.dp)
internal val defaultTabStripCardElevation = 0.dp
internal val selectedTabStripCardElevation = 4.dp

/**
 * Card composable used in Tab Strip items.
 *
 * @param modifier The modifier to be applied to the card.
 * @param backgroundColor The background color of the card.
 * @param elevation The elevation of the card.
 * @param content The content of the card.
 */
@Composable
fun TabStripCard(
    modifier: Modifier = Modifier,
    backgroundColor: Color = FirefoxTheme.colors.layer3,
    elevation: Dp = defaultTabStripCardElevation,
    content: @Composable () -> Unit,
) {
    Card(
        shape = cardShape,
        colors = CardDefaults.cardColors(containerColor = backgroundColor),
        elevation = CardDefaults.cardElevation(defaultElevation = elevation),
        modifier = modifier,
    ) {
        content()
    }
}

@PreviewLightDark
@Composable
private fun TabStripCardPreview() {
    FirefoxTheme {
        TabStripCard {
            Box(
                modifier = Modifier
                    .height(56.dp)
                    .width(200.dp)
                    .padding(8.dp),
                contentAlignment = Alignment.Center,
            ) {
                Text(
                    text = "Tab Strip Card",
                    style = FirefoxTheme.typography.subtitle1,
                )
            }
        }
    }
}
