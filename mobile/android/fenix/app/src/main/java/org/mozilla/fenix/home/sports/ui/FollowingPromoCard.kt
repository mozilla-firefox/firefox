/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.sports.ui

import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight.Companion.W700
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.PromoCard
import mozilla.components.compose.base.PromoCardColors
import org.mozilla.fenix.R
import org.mozilla.fenix.home.sports.Team
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * Card displayed after a user has picked a team to follow.
 *
 * @param team The followed [Team] containing the country key and flag resource id.
 * @param modifier The [Modifier] to be applied to the card.
 */
@Composable
fun FollowingPromoCard(
    team: Team,
    modifier: Modifier = Modifier,
) {
    PromoCard(
        modifier = modifier,
        title = {
            Image(
                painter = painterResource(team.flagResId),
                contentDescription = null,
                modifier = Modifier.clip(RoundedCornerShape(8.dp)),
            )

            Spacer(modifier = Modifier.height(6.dp))

            val title = stringResource(R.string.sports_widget_team_followed_title).split("\n", limit = 2)
            Column(
                modifier = Modifier.semantics(mergeDescendants = true) {},
            ) {
                Text(
                    text = title[0],
                    style = FirefoxTheme.typography.headline8,
                )
                Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static25))
                Text(
                    text = team.key,
                    style = FirefoxTheme.typography.headline5.copy(fontWeight = W700),
                )
            }

            Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static50))
        },
        message = {
            Text(text = stringResource(R.string.sports_widget_team_followed_description))
        },
        illustration = {
            Image(
                painter = painterResource(R.drawable.firefox_sport),
                contentDescription = null,
                modifier = Modifier
                    .width(100.dp)
                    .clip(RoundedCornerShape(8.dp)),
            )
        },
        contentSpacing = 0.dp,
        colors = PromoCardColors.promoCardColors(
            backgroundColor = MaterialTheme.colorScheme.surfaceContainerLowest,
            messageTextColor = MaterialTheme.colorScheme.secondary,
            titleTextColor = MaterialTheme.colorScheme.onSurface,
        ),
    )
}

@PreviewLightDark
@Composable
private fun FollowingPromoCardPreview() {
    FirefoxTheme {
        Surface {
            FollowingPromoCard(
                team = Team(key = "SEN", flagResId = R.drawable.flag_sn),
                modifier = Modifier.padding(16.dp),
            )
        }
    }
}
