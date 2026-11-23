/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.samples.acorn.components.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.core.graphics.ColorUtils
import mozilla.components.compose.base.annotation.FlexibleWindowLightDarkPreview
import mozilla.components.compose.base.button.IconButton
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.ui.colors.ColorGroup
import mozilla.components.ui.colors.ColorSwatch
import mozilla.components.ui.colors.NovaColors
import mozilla.components.ui.colors.novaColorGroups
import mozilla.components.ui.icons.R as iconsR

private fun Color.toHexString(): String {
    return "#%08X".format(toArgb())
}

private const val LUMINANCE_THRESHOLD = 0.4f

private fun Color.getReadableTextColor(): Color {
    return if (ColorUtils.calculateLuminance(toArgb()) > LUMINANCE_THRESHOLD) {
        NovaColors.Black
    } else {
        NovaColors.White
    }
}

@Composable
private fun SwatchCell(swatch: ColorSwatch, modifier: Modifier = Modifier) {
    val textColor = swatch.color.getReadableTextColor()

    Box(
        contentAlignment = Alignment.Center,
        modifier = modifier.width(120.dp).background(swatch.color).padding(16.dp),
    ) {
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Text(
                text = swatch.name,
                color = textColor,
                textAlign = TextAlign.Center,
                style = AcornTheme.typography.caption,
            )

            Text(
                text = swatch.color.toHexString(),
                color = textColor,
                textAlign = TextAlign.Center,
                style = AcornTheme.typography.caption,
            )
        }
    }
}

@Composable
private fun ColorGroupSection(group: ColorGroup) {
    Column(
        modifier = Modifier.padding(horizontal = 16.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Text(text = group.name, style = AcornTheme.typography.subtitle1)

        FlowRow(modifier = Modifier.clip(RoundedCornerShape(16.dp))) {
            group.swatches.forEach { swatch ->
                SwatchCell(swatch)
            }
        }
    }
}

/** Displays the Nova color palette organized by color group. */
@Composable
fun ColorsScreen(onNavigateUp: () -> Unit = {}) {
    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Text(
                        text = "Colors",
                        style = AcornTheme.typography.headline5,
                    )
                },
                navigationIcon = {
                    IconButton(
                        onClick = onNavigateUp,
                        contentDescription = "Navigate back",
                    ) {
                        Icon(
                            painter = painterResource(iconsR.drawable.mozac_ic_back_24),
                            contentDescription = null,
                        )
                    }
                },
                actions = { ThemeToggleButton() },
            )
        }
    ) { innerPadding ->
        Column(
            modifier =
                Modifier.fillMaxSize()
                    .padding(innerPadding)
                    .verticalScroll(rememberScrollState())
                    .padding(vertical = 16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            novaColorGroups.forEachIndexed { index, group ->
                ColorGroupSection(group)

                if (index < novaColorGroups.lastIndex) {
                    HorizontalDivider()
                }
            }
        }
    }
}

@FlexibleWindowLightDarkPreview
@Composable
private fun ColorsScreenPreview() {
    AcornTheme {
        Surface {
            ColorsScreen()
        }
    }
}
