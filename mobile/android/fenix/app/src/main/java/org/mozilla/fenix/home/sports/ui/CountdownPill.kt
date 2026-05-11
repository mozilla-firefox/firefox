/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.sports.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.State
import androidx.compose.runtime.getValue
import androidx.compose.runtime.produceState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.tooling.preview.PreviewParameterProvider
import org.mozilla.fenix.R
import org.mozilla.fenix.home.sports.CountdownTime
import org.mozilla.fenix.home.sports.countdownFlow
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * Displays a live countdown to [dateInUtc], updating every minute. The timer pauses
 * when the app is backgrounded and stops when this composable leaves the composition.
 *
 * @param dateInUtc ISO 8601 UTC date string (e.g. "2025-06-28T14:00:00Z") remaining until kickoff.
 */
@Composable
internal fun CountdownPill(dateInUtc: String) {
    val countdown by rememberCountdownState(dateInUtc)
    CountdownPill(
        days = countdown.days,
        hours = countdown.hours,
        mins = countdown.mins,
    )
}

@Composable
private fun CountdownPill(
    days: String,
    hours: String,
    mins: String,
) {
    val countdownPillContentDescription = stringResource(
        R.string.sports_widget_countdown_remaining_content_description,
        days.toIntOrNull() ?: 0,
        hours.toIntOrNull() ?: 0,
        mins.toIntOrNull() ?: 0,
    )
    Surface(
        shape = MaterialTheme.shapes.extraLarge,
        color = MaterialTheme.colorScheme.surfaceContainerHighest,
        modifier = Modifier.clearAndSetSemantics {
            this.contentDescription = countdownPillContentDescription
        },
    ) {
        Row(
            modifier = Modifier.padding(
                horizontal = FirefoxTheme.layout.space.static500,
                vertical = FirefoxTheme.layout.space.static50,
            ),
        ) {
            CountdownUnit(
                value = days,
                label = stringResource(R.string.sports_widget_countdown_days),
            )
            CountdownSeparator()
            CountdownUnit(
                value = hours,
                label = stringResource(R.string.sports_widget_countdown_hours),
            )
            CountdownSeparator()
            CountdownUnit(
                value = mins,
                label = stringResource(R.string.sports_widget_countdown_minutes),
            )
        }
    }
}

@Composable
private fun CountdownUnit(value: String, label: String) {
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        Text(
            text = value,
            style = FirefoxTheme.typography.headline5,
            color = MaterialTheme.colorScheme.primary,
        )

        Text(
            text = label,
            style = FirefoxTheme.typography.caption,
            color = MaterialTheme.colorScheme.primary,
        )
    }
}

@Composable
private fun CountdownSeparator() {
    Text(
        text = ":",
        modifier = Modifier.padding(horizontal = FirefoxTheme.layout.space.static100),
        color = MaterialTheme.colorScheme.primary,
        style = FirefoxTheme.typography.headline5,
    )
}

@Composable
private fun rememberCountdownState(utcDate: String): State<CountdownTime> =
    produceState(
        initialValue = CountdownTime("00", "00", "00"),
        key1 = utcDate,
    ) {
        countdownFlow(utcDate).collect { value = it }
    }

private data class CountdownPillPreviewState(val dateInUtc: String)

private class CountdownPillPreviewProvider : PreviewParameterProvider<CountdownPillPreviewState> {
    override val values = sequenceOf(
        CountdownPillPreviewState(dateInUtc = "2026-06-11T19:00:00Z"),
        CountdownPillPreviewState(dateInUtc = "2026-06-04T02:34:00Z"),
        CountdownPillPreviewState(dateInUtc = "2026-06-04T00:00:05Z"),
    )
}

@PreviewLightDark
@Composable
private fun CountdownPillPreview(
    @PreviewParameter(CountdownPillPreviewProvider::class) state: CountdownPillPreviewState,
) {
    FirefoxTheme {
        Surface {
            CountdownPill(dateInUtc = state.dateInUtc)
        }
    }
}
