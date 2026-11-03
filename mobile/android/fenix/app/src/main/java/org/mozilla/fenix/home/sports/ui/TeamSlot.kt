/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.sports.ui

import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.size
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.tooling.preview.PreviewParameterProvider
import androidx.compose.ui.unit.dp
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.home.sports.Team
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * A team's image and name.
 *
 * @param team The [Team] to display, or null when the team has not been determined yet.
 * @param modifier [Modifier] to be applied to the slot.
 */
@Composable
internal fun TeamSlot(
    team: Team?,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier,
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(FirefoxTheme.layout.space.static50),
    ) {
        Image(
            painter = painterResource(team?.drawableResId ?: iconsR.drawable.mozac_ic_soccer_ball_24),
            contentDescription = null,
            modifier = Modifier.size(36.dp),
        )

        Text(
            text = team?.key ?: "--",
            style = FirefoxTheme.typography.body2,
            color = MaterialTheme.colorScheme.onSurface,
        )
    }
}

private class TeamSlotPreviewProvider : PreviewParameterProvider<Team?> {
    override val values = sequenceOf(Team(key = "WRE", drawableResId = R.drawable.team_badge_wrexham), null)
}

@PreviewLightDark
@Composable
private fun TeamSlotPreview(@PreviewParameter(TeamSlotPreviewProvider::class) team: Team?) {
    FirefoxTheme {
        Surface {
            TeamSlot(team = team)
        }
    }
}
