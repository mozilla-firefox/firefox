/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.button.OutlinedButton
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.feature.summarize.DownloadInProgressAction
import mozilla.components.feature.summarize.R
import mozilla.components.feature.summarize.SummarizationState

/**
 * Composable to be rendering while downloading an on-device model.
 */
@Composable
internal fun DownloadProgress(
    modifier: Modifier = Modifier,
    downloadState: SummarizationState.Downloading,
    dispatchAction: (DownloadInProgressAction) -> Unit = {},
) {
    DownloadProgressContent(
        modifier = modifier,
        bytesDownloaded = downloadState.bytesDownloaded,
        bytesToDownload = downloadState.bytesToDownload,
        downloadProgress = downloadState.downloadProgress,
        onClickCancel = { dispatchAction(DownloadInProgressAction.CancelClicked) },
    )
}

@Composable
private fun DownloadProgressContent(
    modifier: Modifier = Modifier,
    bytesDownloaded: Float,
    bytesToDownload: Float,
    downloadProgress: Float,
    onClickCancel: () -> Unit,
) {
    Column(modifier, horizontalAlignment = Alignment.CenterHorizontally) {
        Text(
            text = stringResource(R.string.mozac_summarize_download_progress_title),
            style = AcornTheme.typography.headline6,
            color = MaterialTheme.colorScheme.onSurface,
            textAlign = TextAlign.Center,
        )

        Spacer(Modifier.height(AcornTheme.layout.space.static100))

        Text(
            text = stringResource(R.string.mozac_summarize_download_progress_message),
            style = AcornTheme.typography.body2,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            textAlign = TextAlign.Center,
        )

        Spacer(Modifier.height(AcornTheme.layout.space.static300))

        LinearProgressIndicator(
            progress = { downloadProgress },
            modifier = Modifier.fillMaxWidth(),
        )

        Text(
            text = stringResource(R.string.mozac_summarize_download_progress_caption, bytesDownloaded, bytesToDownload),
            style = AcornTheme.typography.caption,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier
                .align(Alignment.Start)
                .padding(8.dp),
        )

        Spacer(Modifier.height(AcornTheme.layout.space.static300))

        OutlinedButton(
            modifier = Modifier.fillMaxWidth(),
            text = stringResource(R.string.mozac_summarize_download_progress_button_negative),
            onClick = onClickCancel,
        )
    }
}

@PreviewLightDark
@Preview(locale = "ar", name = "RTL")
@Composable
private fun PreviewDownloadProgress() = AcornTheme {
    Surface {
        val bytesToDownload = 12.13f
        val bytesDownloaded = 9.04f
        DownloadProgress(
            downloadState = SummarizationState.Downloading(
                bytesToDownload = bytesToDownload,
                bytesDownloaded = bytesDownloaded,
            ),
        )
    }
}
