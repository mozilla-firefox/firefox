/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.sports.ui

import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.tooling.preview.PreviewParameterProvider
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.InfoCard
import mozilla.components.compose.base.InfoType
import mozilla.components.compose.base.LinkTextState
import mozilla.components.compose.base.annotation.FlexibleWindowLightDarkPreview
import org.mozilla.fenix.R
import org.mozilla.fenix.home.sports.SportCardErrorState
import org.mozilla.fenix.home.sports.isRefreshable
import org.mozilla.fenix.home.sports.messageResId
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * A warning [InfoCard] shown inside the sports widget when match data cannot be displayed.
 *
 * @param error The [SportsCardErrorState] message to display.
 * @param onRefresh Callback invoked when the user taps the "Refresh" link. Ignored for
 * [SportsCardErrorState.ConnectionInterrupted] which has no refresh action.
 * @param modifier Modifier applied to the card.
 */
@Composable
fun SportsWidgetErrorCard(
    error: SportCardErrorState,
    onRefresh: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val description = stringResource(error.messageResId)
    val refreshText = stringResource(R.string.sports_widget_error_refresh)

    InfoCard(
        modifier = modifier,
        type = InfoType.Error,
        description = description,
        footer = if (error.isRefreshable) {
            refreshText to LinkTextState(
                text = refreshText,
                url = "",
                onClick = { onRefresh() },
            )
        } else {
            null
        },
    )
}

private class SportsWidgetErrorPreviewProvider :
    PreviewParameterProvider<SportCardErrorState> {
    override val values = SportCardErrorState.entries.asSequence()
}

@FlexibleWindowLightDarkPreview
@Composable
private fun SportsWidgetErrorCardPreview(
    @PreviewParameter(SportsWidgetErrorPreviewProvider::class) error: SportCardErrorState,
) {
    FirefoxTheme {
        Surface {
            SportsWidgetErrorCard(
                error = error,
                onRefresh = {},
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(16.dp),
            )
        }
    }
}
