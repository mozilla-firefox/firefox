/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.tooling.preview.PreviewLightDark
import mozilla.components.compose.base.button.FilledButton
import mozilla.components.compose.base.button.OutlinedButton
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.feature.summarize.OffDeviceSummarizationShakeConsentAction
import mozilla.components.feature.summarize.R

/**
 * Composable to be rendered to request user consent to allow off-device summarization.
 */
@Composable
internal fun OffDeviceSummarizationConsent(
    modifier: Modifier = Modifier,
    dispatchAction: (OffDeviceSummarizationShakeConsentAction) -> Unit = {},
) {
    OffDeviceSummarizationConsentContent(
        modifier = modifier,
        onClickLearnMore = {
            dispatchAction(OffDeviceSummarizationShakeConsentAction.LearnMoreClicked)
        },
        onClickAllow = {
            dispatchAction(OffDeviceSummarizationShakeConsentAction.AllowClicked)
        },
        onClickCancel = {
            dispatchAction(OffDeviceSummarizationShakeConsentAction.CancelClicked)
        },
    )
}

@Composable
private fun OffDeviceSummarizationConsentContent(
    modifier: Modifier,
    onClickLearnMore: () -> Unit,
    onClickAllow: () -> Unit,
    onClickCancel: () -> Unit,
) {
    Column(modifier) {
        OffDeviceSummarizationDescription(
            onClickLearnMore = onClickLearnMore,
        )

        Spacer(modifier = Modifier.height(AcornTheme.layout.space.static300))

        OffDeviceSummarizationButtons(
            onClickAllow = onClickAllow,
            onClickCancel = onClickCancel,
        )
    }
}

@Composable
private fun OffDeviceSummarizationDescription(
    modifier: Modifier = Modifier,
    onClickLearnMore: () -> Unit,
) {
    Column(modifier = modifier, horizontalAlignment = Alignment.CenterHorizontally) {
        Text(
            text = stringResource(R.string.mozac_summarize_shake_consent_off_device_title),
            style = AcornTheme.typography.headline6,
            color = MaterialTheme.colorScheme.onSurface,
            textAlign = TextAlign.Center,
        )

        Spacer(modifier = Modifier.height(AcornTheme.layout.space.static100))

        Text(
            text = stringResource(R.string.mozac_summarize_shake_consent_off_device_message),
            style = AcornTheme.typography.body2,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            textAlign = TextAlign.Center,
        )

        Spacer(modifier = Modifier.height(AcornTheme.layout.space.static200))

        LearnMoreLinkText(onClick = onClickLearnMore)
    }
}

@Composable
private fun OffDeviceSummarizationButtons(
    modifier: Modifier = Modifier,
    onClickAllow: () -> Unit,
    onClickCancel: () -> Unit,
) {
    Column(
        modifier = modifier,
    ) {
        FilledButton(
            modifier = Modifier.fillMaxWidth(),
            onClick = onClickAllow,
            text = stringResource(R.string.mozac_summarize_shake_consent_off_device_button_positive),
        )

        Spacer(Modifier.height(AcornTheme.layout.space.static200))

        OutlinedButton(
            modifier = Modifier.fillMaxWidth(),
            text = stringResource(R.string.mozac_summarize_shake_consent_off_device_negative_button),
            onClick = onClickCancel,
        )
    }
}

@PreviewLightDark
@Composable
private fun PreviewOffDeviceSummarizationContent() = AcornTheme {
    Surface {
        OffDeviceSummarizationConsent()
    }
}
