/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.sports.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.tooling.preview.PreviewParameterProvider
import androidx.compose.ui.unit.dp
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.compose.Image
import org.mozilla.fenix.home.sports.Team
import org.mozilla.fenix.theme.FirefoxTheme

private val CrestSize = 36.dp

/**
 * A team's image and name.
 *
 * @param team The [Team] to display.
 * @param modifier [Modifier] to be applied to the slot.
 */
@Composable
internal fun TeamSlot(
    team: Team,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier,
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(FirefoxTheme.layout.space.static50),
    ) {
        Image(
            url = team.iconUrl,
            modifier = Modifier.size(CrestSize),
            targetSize = CrestSize,
            placeholder = { CrestPlaceholder() },
            fallback = { CrestPlaceholder() },
        )

        Text(
            text = team.name,
            style = FirefoxTheme.typography.body2,
            color = MaterialTheme.colorScheme.onSurface,
            textAlign = TextAlign.Center,
            overflow = TextOverflow.Ellipsis,
            maxLines = 2,
        )
    }
}

@Composable
private fun CrestPlaceholder() {
    Box(
        modifier =
            Modifier.size(CrestSize)
                .background(color = MaterialTheme.colorScheme.surfaceContainerHigh, shape = CircleShape),
        contentAlignment = Alignment.Center,
    ) {
        Icon(
            painter = painterResource(iconsR.drawable.mozac_ic_shield_24),
            contentDescription = null,
            tint = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

private class TeamSlotPreviewProvider : PreviewParameterProvider<Team> {
    override val values =
        sequenceOf(
            Team(name = "WRE", id = 1L, iconUrl = "https://example.com/wre.png"),
            Team(name = "Wolverhampton Wanderers FC", id = 2L, iconUrl = "https://example.com/wol.png"),
        )
}

@PreviewLightDark
@Composable
private fun TeamSlotPreview(@PreviewParameter(TeamSlotPreviewProvider::class) team: Team) {
    FirefoxTheme {
        Surface {
            TeamSlot(team = team)
        }
    }
}
