/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.button.FilledButton
import mozilla.components.compose.base.button.OutlinedButton
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.feature.summarize.DownloadErrorAction
import mozilla.components.feature.summarize.R

@Composable
internal fun DownloadError(
    modifier: Modifier = Modifier,
    dispatchAction: (DownloadErrorAction) -> Unit = {},
) {
    DownloadErrorContent(
        modifier = modifier,
        onClickLearnMore = { dispatchAction(DownloadErrorAction.LearnMoreClicked) },
        onClickTryAgain = { dispatchAction(DownloadErrorAction.TryAgainClicked) },
        onClickCancel = { dispatchAction(DownloadErrorAction.CancelClicked) },
    )
}

@Composable
private fun DownloadErrorContent(
    modifier: Modifier = Modifier,
    onClickLearnMore: () -> Unit,
    onClickTryAgain: () -> Unit,
    onClickCancel: () -> Unit,
) {
    Column(modifier) {
        DownloadErrorDescription(
            modifier = modifier,
            onClickLearnMore = onClickLearnMore,
        )

        Spacer(modifier = Modifier.height(AcornTheme.layout.space.static300))

        DownloadErrorButtons(
            modifier = modifier,
            onClickTryAgain = onClickTryAgain,
            onClickCancel = onClickCancel,
        )
    }
}

@Composable
private fun DownloadErrorDescription(
    modifier: Modifier = Modifier,
    onClickLearnMore: () -> Unit,
) {
    Column(modifier, horizontalAlignment = Alignment.CenterHorizontally) {
        Row {
            Icon(
                painter = painterResource(mozilla.components.ui.icons.R.drawable.mozac_ic_warning_fill_24),
                contentDescription = null,
                modifier = Modifier.padding(end = 8.dp),
            )

            Text(
                text = stringResource(R.string.mozac_summarize_download_error_title),
                style = AcornTheme.typography.headline6,
                color = MaterialTheme.colorScheme.onSurface,
                textAlign = TextAlign.Center,
            )
        }

        Spacer(modifier = Modifier.height(AcornTheme.layout.space.static100))

        Text(
            text = stringResource(R.string.mozac_summarize_download_error_fallback_message),
            style = AcornTheme.typography.body2,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            textAlign = TextAlign.Center,
        )

        Spacer(modifier = Modifier.height(AcornTheme.layout.space.static200))

        LearnMoreLinkText(onClick = onClickLearnMore)
    }
}

@Composable
private fun DownloadErrorButtons(
    modifier: Modifier = Modifier,
    onClickTryAgain: () -> Unit,
    onClickCancel: () -> Unit,
) {
    Column(
        modifier = modifier,
    ) {
        FilledButton(
            modifier = Modifier.fillMaxWidth(),
            onClick = onClickTryAgain,
            text = stringResource(R.string.mozac_summarize_download_error_positive_button),
        )

        Spacer(Modifier.height(AcornTheme.layout.space.static200))

        OutlinedButton(
            modifier = Modifier.fillMaxWidth(),
            text = stringResource(R.string.mozac_summarize_download_error_negative_button),
            onClick = onClickCancel,
        )
    }
}

@PreviewLightDark
@Composable
private fun PreviewDownloadError() = AcornTheme {
    Surface {
        DownloadError()
    }
}
