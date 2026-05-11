/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.sports.ui

import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.PromoCard
import mozilla.components.compose.base.PromoCardColors
import mozilla.components.compose.base.button.FilledButton
import mozilla.components.compose.base.button.TextButton
import org.mozilla.fenix.R
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * Card prompting the user to follow the World Cup.
 *
 * @param onFollowTeam Callback invoked when the "Follow your team" button is tapped.
 * @param onSkip Callback invoked when the "Skip" button is tapped.
 * @param onDismiss Callback invoked when the close button is tapped.
 * @param modifier The [Modifier] to be applied to the card.
 */
@Composable
fun FollowTeamPromoCard(
    onFollowTeam: () -> Unit,
    onSkip: () -> Unit,
    onDismiss: () -> Unit,
    modifier: Modifier = Modifier,
) {
    PromoCard(
        closeButtonContentDescription = null,
        onDismiss = onDismiss,
        modifier = modifier,
        title = {
            Text(text = stringResource(R.string.sports_widget_card_title))

            Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static50))
        },
        message = { Text(text = stringResource(R.string.sports_widget_card_description)) },
        actions = {
            Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static150))

            FilledButton(
                text = stringResource(R.string.sports_widget_country_selector_title),
                onClick = onFollowTeam,
            )

            TextButton(
                text = stringResource(R.string.sports_widget_skip),
                onClick = onSkip,
            )
        },
        contentSpacing = 0.dp,
        colors = PromoCardColors.promoCardColors(
            backgroundColor = MaterialTheme.colorScheme.surfaceContainerLowest,
        ),
    )
}

@PreviewLightDark
@Composable
private fun FollowTeamPromoCardPreview() {
    FirefoxTheme {
        Surface {
            FollowTeamPromoCard(
                onFollowTeam = {},
                onSkip = {},
                onDismiss = {},
                modifier = Modifier.padding(16.dp),
            )
        }
    }
}
