/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.nimbus.view

import androidx.annotation.StringRes
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import org.mozilla.experiments.nimbus.AvailableExperiment
import org.mozilla.fenix.R
import org.mozilla.fenix.compose.list.TextListItem
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * List of Nimbus Experiments.
 *
 * @param experiments List of [NimbusExperimentItem] that are going to be displayed.
 * @param onExperimentClick Invoked when the user clicks on an [AvailableExperiment].
 */
@Composable
fun NimbusExperiments(
    experiments: List<NimbusExperimentItem> = listOf(),
    onExperimentClick: (AvailableExperiment) -> Unit,
) {
    LazyColumn(
        modifier = Modifier.fillMaxSize(),
    ) {
        items(experiments) { item ->
            when (item) {
                is NimbusExperimentItem.Header -> NimbusExperimentHeader(titleResourceId = item.title)
                is NimbusExperimentItem.Experiment -> TextListItem(
                    label = item.experiment.userFacingName,
                    description = item.experiment.userFacingDescription,
                    maxDescriptionLines = Int.MAX_VALUE,
                    onClick = {
                        onExperimentClick(item.experiment)
                    },
                )
                is NimbusExperimentItem.EmptyState -> NimbusExperimentEmptyState(text = item.text)
            }
        }
    }
}

/**
 * Item types for the list of experiments to be displayed.
 */
sealed class NimbusExperimentItem {
    /**
     * Title header for an experiment section. Typically, for "enrolled" or "unenrolled" sections.
     *
     * @property title the title to display.
     */
    data class Header(
        @param:StringRes val title: Int,
    ) : NimbusExperimentItem()

    /**
     * An experiment item.
     *
     * @property experiment the experiment to display.
     */
    data class Experiment(val experiment: AvailableExperiment) : NimbusExperimentItem()

    /**
     * An empty section if there are no items to show.
     *
     * @property text the string to show when we have an empty state.
     */
    data class EmptyState(
        @param:StringRes val text: Int,
    ) : NimbusExperimentItem()
}

@Composable
private fun NimbusExperimentHeader(
    @StringRes titleResourceId: Int,
) {
    Text(
        text = stringResource(titleResourceId),
        style = MaterialTheme.typography.titleSmall,
        color = MaterialTheme.colorScheme.primary,
        modifier = Modifier.padding(
            start = 16.dp,
            end = 16.dp,
            top = 16.dp,
            bottom = 8.dp,
        ),
    )
}

@Composable
private fun NimbusExperimentEmptyState(
    @StringRes text: Int,
) {
    Text(
        text = stringResource(text),
        style = MaterialTheme.typography.bodyMedium,
        color = MaterialTheme.colorScheme.secondary,
        modifier = Modifier.padding(
            start = 16.dp,
            end = 16.dp,
            top = 8.dp,
            bottom = 8.dp,
        ),
    )
}

@Composable
@PreviewLightDark
private fun NimbusExperimentsPreview() {
    val testExperiment = AvailableExperiment(
        userFacingName = "Name",
        userFacingDescription = "Description",
        slug = "slug",
        branches = emptyList(),
        referenceBranch = null,
    )

    FirefoxTheme {
        NimbusExperiments(
            experiments = listOf(
                NimbusExperimentItem.Header(R.string.preferences_nimbus_experiments_active),
                NimbusExperimentItem.EmptyState(R.string.preferences_nimbus_experiments_no_items),
                NimbusExperimentItem.Header(R.string.preferences_nimbus_experiments_inactive),
                NimbusExperimentItem.Experiment(testExperiment),
                NimbusExperimentItem.Experiment(testExperiment),
            ),
            onExperimentClick = {},
        )
    }
}
