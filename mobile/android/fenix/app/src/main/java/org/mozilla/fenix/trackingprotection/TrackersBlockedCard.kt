/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.trackingprotection

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.modifier.thenConditional
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.theme.FirefoxTheme

internal const val PROTECTION_STATUS_PILL_TEST_TAG = "trackersBlockedCard.protectionStatusPill"

/**
 * A card that displays the number of trackers blocked.
 *
 * @param trackersBlockedCount The number of trackers blocked to display.
 * @param modifier Modifier to be applied to the card.
 * @param onPrivacyReportTapped Invoked when the card is tapped. If null, the card is not clickable.
 */
@Composable
fun TrackersBlockedCard(
    trackersBlockedCount: Int,
    modifier: Modifier = Modifier,
    onPrivacyReportTapped: (() -> Unit)? = null,
) {
    val shape = MaterialTheme.shapes.extraLarge
    Row(
        modifier =
            modifier
                .testTag(PROTECTION_STATUS_PILL_TEST_TAG)
                .background(
                    color = MaterialTheme.colorScheme.surfaceBright,
                    shape = shape,
                )
                .clip(shape)
                .thenConditional(
                    Modifier.clickable { onPrivacyReportTapped?.invoke() },
                    { onPrivacyReportTapped != null },
                )
                .padding(horizontal = 16.dp, vertical = 8.dp),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(
            painter = painterResource(iconsR.drawable.mozac_ic_shield_checkmark_20),
            contentDescription = null,
            modifier = Modifier.size(20.dp),
            tint = MaterialTheme.colorScheme.tertiary,
        )

        Text(
            text =
                if (trackersBlockedCount > 0) {
                    pluralStringResource(
                        R.plurals.trackers_blocked_count_2,
                        trackersBlockedCount,
                        trackersBlockedCount,
                    )
                } else {
                    stringResource(R.string.trackers_blocked_empty)
                },
            style = FirefoxTheme.typography.body2,
            color = MaterialTheme.colorScheme.onSurface,
        )
    }
}

@PreviewLightDark
@Composable
private fun TrackersBlockedCardPreview() {
    FirefoxTheme {
        Surface {
            TrackersBlockedCard(
                trackersBlockedCount = 754,
                onPrivacyReportTapped = {},
            )
        }
    }
}

@PreviewLightDark
@Composable
private fun TrackersBlockedCardEmptyPreview() {
    FirefoxTheme {
        Surface {
            TrackersBlockedCard(
                trackersBlockedCount = 0,
                onPrivacyReportTapped = {},
            )
        }
    }
}
