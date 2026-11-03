/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.sports.ui

import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.tooling.preview.PreviewParameterProvider
import mozilla.components.compose.base.button.FilledButton
import org.mozilla.fenix.R
import org.mozilla.fenix.home.sports.SportCardErrorState
import org.mozilla.fenix.home.sports.messageResId
import org.mozilla.fenix.home.sports.titleResId
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * Card shown inside the sports widget when match data cannot be displayed.
 *
 * @param state The [SportCardErrorState] message to display.
 * @param onRefresh Callback invoked when the user taps the "Refresh" button.
 * @param modifier Modifier applied to the card.
 */
@Composable
fun SportsWidgetErrorCard(
    state: SportCardErrorState,
    onRefresh: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Box(modifier = modifier.fillMaxWidth().padding(horizontal = FirefoxTheme.layout.space.static200)) {
        Image(
            painter = painterResource(R.drawable.fox_expressive_face_tail),
            contentDescription = null,
            modifier = Modifier.align(Alignment.BottomEnd),
        )

        Column(modifier = Modifier.padding(bottom = FirefoxTheme.layout.space.static300)) {
            Text(
                text = stringResource(state.titleResId),
                style = FirefoxTheme.typography.headline8,
                color = MaterialTheme.colorScheme.onSurface,
            )

            Text(
                text = stringResource(state.messageResId),
                style = FirefoxTheme.typography.body2,
                color = MaterialTheme.colorScheme.secondary,
            )

            Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static150))

            FilledButton(
                text = stringResource(R.string.wrex_widget_error_refresh),
                onClick = onRefresh,
            )
        }
    }
}

private class SportsWidgetErrorPreviewProvider : PreviewParameterProvider<SportCardErrorState> {
    override val values = SportCardErrorState.entries.asSequence()
}

@PreviewLightDark
@Composable
private fun SportsWidgetErrorCardPreview(
    @PreviewParameter(SportsWidgetErrorPreviewProvider::class) error: SportCardErrorState
) {
    FirefoxTheme {
        Surface {
            SportsWidgetErrorCard(
                state = error,
                onRefresh = {},
                modifier = Modifier.padding(top = FirefoxTheme.layout.space.static200),
            )
        }
    }
}
