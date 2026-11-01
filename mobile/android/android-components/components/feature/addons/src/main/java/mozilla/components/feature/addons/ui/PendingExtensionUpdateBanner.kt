/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.addons.ui

import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.PreviewLightDark
import mozilla.components.compose.base.Banner
import mozilla.components.compose.base.BannerColors
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.feature.addons.R

/**
 * Banner for an extension update that waits for the user to grant new permissions.
 *
 * @param message The text of the banner.
 * @param onReviewClick Invoked when "Review changes" is clicked.
 * @param onCancelClick Invoked when the close button is clicked. The update should be cancelled.
 * @param modifier [Modifier] to be applied to the banner.
 */
@Composable
fun PendingExtensionUpdateBanner(
    message: String,
    onReviewClick: () -> Unit,
    onCancelClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Banner(
        messageText = message,
        modifier = modifier,
        colors = BannerColors.bannerColors(backgroundColor = MaterialTheme.colorScheme.surfaceContainerHighest),
        positiveButtonText = stringResource(R.string.mozac_feature_addons_update_review_changes_button),
        positiveOnClick = onReviewClick,
        onCloseButtonClick = onCancelClick,
    )
}

@PreviewLightDark
@Composable
private fun PendingExtensionUpdateBannerPreview() {
    AcornTheme {
        PendingExtensionUpdateBanner(
            message = "uBlock Origin requires new settings to update.",
            onReviewClick = {},
            onCancelClick = {},
        )
    }
}
