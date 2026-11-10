/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.collections

import androidx.compose.foundation.Image
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.PreviewLightDark
import mozilla.components.compose.base.LinkTextState
import mozilla.components.compose.base.PromoCard
import mozilla.components.compose.base.PromoCardColors
import mozilla.components.compose.base.modifier.onShown
import org.mozilla.fenix.GleanMetrics.CollectionsMigration
import org.mozilla.fenix.R
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * Collections to tab groups migration card.
 *
 * @param source The [CollectionsMigrationPromoSource] where the card is shown.
 * @param modifier The [Modifier] to be applied to the [PromoCard].
 * @param onClick Invoked when the user clicks on the "View my tab groups" link. When null, the link is not shown.
 * @param onDismiss Invoked when the user clicks the close button. When null, the close button is not shown.
 */
@Composable
fun CollectionsMigrationPromoCard(
    source: CollectionsMigrationPromoSource,
    modifier: Modifier = Modifier,
    onClick: (() -> Unit)? = null,
    onDismiss: (() -> Unit)? = null,
) {
    val linkText = stringResource(R.string.collections_migration_homepage_card_link)

    PromoCard(
        description = stringResource(R.string.collections_migration_homepage_card_message),
        modifier =
            modifier.onShown(
                threshold = 0.5f,
                onVisible = {
                    CollectionsMigration.promoImpression.record(
                        CollectionsMigration.PromoImpressionExtra(source = source.telemetryName)
                    )
                },
            ),
        title = stringResource(R.string.collections_migration_homepage_banner_title),
        footer =
            onClick?.let { onLinkClick ->
                linkText to
                    LinkTextState(
                        text = linkText,
                        url = "",
                        onClick = { onLinkClick() },
                    )
            },
        illustration = {
            Image(
                painter = painterResource(id = R.drawable.mozac_ic_kit_tab_groups_list_view),
                contentDescription = null,
            )
        },
        verticalAlignment = Alignment.CenterVertically,
        colors =
            PromoCardColors.promoCardColors(
                backgroundColor = MaterialTheme.colorScheme.primaryContainer,
                actionsTextColor = MaterialTheme.colorScheme.tertiary,
            ),
        closeButtonContentDescription =
            stringResource(R.string.collections_migration_card_close_button_content_description),
        onDismiss = onDismiss,
    )
}

@Composable
@PreviewLightDark
private fun CollectionsMigrationPromoCardPreview() {
    FirefoxTheme {
        CollectionsMigrationPromoCard(source = CollectionsMigrationPromoSource.HOMEPAGE, onClick = {})
    }
}

@Composable
@PreviewLightDark
private fun CollectionsMigrationPromoCardWithCloseButtonPreview() {
    FirefoxTheme {
        CollectionsMigrationPromoCard(source = CollectionsMigrationPromoSource.TABS_TRAY, onDismiss = {})
    }
}
