/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.sports.ui

import androidx.annotation.DrawableRes
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.button.IconButton
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * Header of the sports widget displaying the followed team and the menu.
 *
 * @param teamName Display name of the followed team.
 * @param drawableResId Drawable of the followed team's badge.
 * @param onResults Invoked when "Results" is selected from the menu.
 * @param onStandings Invoked when "Standings" is selected from the menu.
 * @param onWallpapers Invoked when "Wallpapers" is selected from the menu.
 * @param onShare Invoked when "Share" is selected from the menu.
 * @param onRemove Invoked when "Remove Widget" is selected from the menu.
 * @param modifier [Modifier] to be applied to the header.
 */
@Suppress("LongParameterList")
@Composable
internal fun SportsWidgetHeader(
    teamName: String,
    @DrawableRes drawableResId: Int,
    onResults: () -> Unit,
    onStandings: () -> Unit,
    onWallpapers: () -> Unit,
    onShare: () -> Unit,
    onRemove: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(modifier = modifier.fillMaxWidth()) {
        Row(
            modifier = Modifier.fillMaxWidth().padding(start = FirefoxTheme.layout.space.static150),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Row(
                modifier = Modifier.weight(1f).semantics(mergeDescendants = true) { heading() },
                horizontalArrangement = Arrangement.spacedBy(FirefoxTheme.layout.space.static50),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Image(
                    painter = painterResource(drawableResId),
                    contentDescription = null,
                    modifier = Modifier.size(24.dp),
                )

                Text(
                    text = teamName,
                    style = FirefoxTheme.typography.headline8,
                )
            }

            SportsWidgetMenuButton(
                onResults = onResults,
                onStandings = onStandings,
                onWallpapers = onWallpapers,
                onShare = onShare,
                onRemove = onRemove,
            )
        }

        HorizontalDivider(thickness = 0.5.dp)
    }
}

@Composable
private fun SportsWidgetMenuButton(
    onResults: () -> Unit,
    onStandings: () -> Unit,
    onWallpapers: () -> Unit,
    onShare: () -> Unit,
    onRemove: () -> Unit,
) {
    var showMenu by remember { mutableStateOf(false) }

    Box {
        IconButton(
            onClick = { showMenu = true },
            contentDescription = stringResource(R.string.wrex_widget_more_options_content_description),
        ) {
            Icon(
                painter = painterResource(iconsR.drawable.mozac_ic_ellipsis_vertical_24),
                contentDescription = null,
            )
        }

        SportsWidgetMenu(
            expanded = showMenu,
            onDismissRequest = { showMenu = false },
            onResults = onResults,
            onStandings = onStandings,
            onWallpapers = onWallpapers,
            onShare = onShare,
            onRemove = onRemove,
        )
    }
}

@PreviewLightDark
@Composable
private fun SportsWidgetHeaderPreview() {
    FirefoxTheme {
        Surface {
            SportsWidgetHeader(
                teamName = "Mozilla ABC",
                drawableResId = iconsR.drawable.mozac_ic_soccer_ball_24,
                onResults = {},
                onStandings = {},
                onWallpapers = {},
                onShare = {},
                onRemove = {},
                modifier = Modifier.padding(bottom = FirefoxTheme.layout.space.static200),
            )
        }
    }
}
