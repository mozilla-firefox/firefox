/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize.ui

import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.concept.llm.ErrorCode
import mozilla.components.feature.summarize.R

@Composable
internal fun InfoError(
    modifier: Modifier = Modifier,
    errorCode: ErrorCode,
) {
    var showErrorCode by remember { mutableStateOf(false) }

    Column(modifier.fillMaxWidth(), horizontalAlignment = Alignment.CenterHorizontally) {
        Icon(
            painter = painterResource(mozilla.components.ui.icons.R.drawable.mozac_ic_warning_fill_24),
            contentDescription = null,
            modifier = Modifier
                .padding(end = 8.dp)
                .onLongPress { showErrorCode = true },
        )

        Spacer(modifier = Modifier.height(AcornTheme.layout.space.static300))

        Text(
            text = stringResource(R.string.mozac_summarize_info_error_title),
            style = AcornTheme.typography.headline6,
            color = MaterialTheme.colorScheme.onSurface,
            textAlign = TextAlign.Center,
        )

        Spacer(modifier = Modifier.height(AcornTheme.layout.space.static100))

        Text(
            text = stringResource(R.string.mozac_summarize_info_error_message),
            style = AcornTheme.typography.body2,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            textAlign = TextAlign.Center,
        )

        if (showErrorCode) {
            Text(
                text = stringResource(R.string.mozac_summarize_info_error_code, errorCode.value),
                style = AcornTheme.typography.body2,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                textAlign = TextAlign.Center,
            )
        }

        Spacer(modifier = Modifier.height(AcornTheme.layout.space.static200))
    }
}

@Composable
private fun Modifier.onLongPress(onLongPress: () -> Unit): Modifier {
    return this.pointerInput(Unit) {
        detectTapGestures(onLongPress = { onLongPress() })
    }
}

@PreviewLightDark
@Composable
private fun PreviewInfoError() = AcornTheme {
    Surface {
        InfoError(errorCode = ErrorCode(1000))
    }
}
