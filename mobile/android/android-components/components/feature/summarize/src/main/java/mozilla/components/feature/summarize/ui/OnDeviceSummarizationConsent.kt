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
import mozilla.components.feature.summarize.LocalProductName
import mozilla.components.feature.summarize.R
import mozilla.components.feature.summarize.SummarizationAction.OnDeviceSummarizationShakeConsentAction

/**
 * Composable to be rendered to request user consent to allow on-device summarization.
 */
@Composable
internal fun OnDeviceSummarizationConsent(
    modifier: Modifier = Modifier,
    dispatchAction: (OnDeviceSummarizationShakeConsentAction) -> Unit = {},
) {
    OnDeviceSummarizationConsentContent(
        modifier = modifier,
        onClickLearnMore = {
            dispatchAction(OnDeviceSummarizationShakeConsentAction.AllowClicked)
        },
        onClickAllow = {
            dispatchAction(OnDeviceSummarizationShakeConsentAction.AllowClicked)
        },
        onClickCancel = {
            dispatchAction(OnDeviceSummarizationShakeConsentAction.CancelClicked)
        },
    )
}

@Composable
private fun OnDeviceSummarizationConsentContent(
    modifier: Modifier,
    onClickLearnMore: () -> Unit,
    onClickAllow: () -> Unit,
    onClickCancel: () -> Unit,
) {
    Column(modifier) {
        OnDeviceSummarizationDescription(
            onClickLearnMore = onClickLearnMore,
        )

        Spacer(modifier = Modifier.height(AcornTheme.layout.space.static300))

        OnDeviceSummarizationButtons(
            onClickAllow = onClickAllow,
            onClickCancel = onClickCancel,
        )
    }
}

@Composable
private fun OnDeviceSummarizationDescription(
    modifier: Modifier = Modifier,
    onClickLearnMore: () -> Unit,
) {
    val productName = LocalProductName.current.value

    Column(modifier = modifier, horizontalAlignment = Alignment.CenterHorizontally) {
        Text(
            text = stringResource(R.string.mozac_summarize_shake_consent_on_device_title),
            style = AcornTheme.typography.headline6,
            color = MaterialTheme.colorScheme.onSurface,
            textAlign = TextAlign.Center,
        )

        Spacer(modifier = Modifier.height(AcornTheme.layout.space.static100))

        Text(
            text = stringResource(R.string.mozac_summarize_shake_consent_on_device_message, productName),
            style = AcornTheme.typography.body2,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            textAlign = TextAlign.Center,
        )

        Spacer(modifier = Modifier.height(AcornTheme.layout.space.static200))

        LearnMoreLinkText(onClick = onClickLearnMore)
    }
}

@Composable
private fun OnDeviceSummarizationButtons(
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
            text = stringResource(R.string.mozac_summarize_shake_consent_on_device_button_positive),
        )

        Spacer(Modifier.height(AcornTheme.layout.space.static200))

        OutlinedButton(
            modifier = Modifier.fillMaxWidth(),
            text = stringResource(R.string.mozac_summarize_shake_consent_on_device_negative_button),
            onClick = onClickCancel,
        )
    }
}

@PreviewLightDark
@Composable
private fun PreviewOnDeviceSummarizationContent() = AcornTheme {
    Surface {
        OnDeviceSummarizationConsent()
    }
}
