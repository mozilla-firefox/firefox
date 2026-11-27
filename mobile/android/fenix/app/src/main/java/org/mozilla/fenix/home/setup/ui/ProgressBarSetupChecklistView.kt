/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.setup.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.CornerSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.annotation.FlexibleWindowLightDarkPreview
import org.mozilla.fenix.theme.FirefoxTheme

private val heightProgressBarChecklist = 12.dp

/**
 * The progress bar for checklist tasks.
 *
 * @param numberOfTasks The total of tasks that can be completed.
 * @param numberOfTasksCompleted The number of tasks completed.
 */
@Composable
fun ProgressBarSetupChecklistView(numberOfTasks: Int, numberOfTasksCompleted: Int) {
    Surface {
        ProgressBarBackground()

        ProgressBarCompleted(numberOfTasks, numberOfTasksCompleted)

        ProgressBarSegmentation(numberOfTasks)
    }
}

/** The background for progressbar. */
@Composable
private fun ProgressBarBackground() {
    Row(
        modifier =
            Modifier.fillMaxWidth()
                .height(heightProgressBarChecklist)
                .clip(CircleShape)
                .background(MaterialTheme.colorScheme.tertiary.copy(alpha = 0.45f))
    ) {}
}

/** The progress bar for checklist tasks. */
@Composable
private fun ProgressBarCompleted(numberOfTasks: Int, numberOfTasksCompleted: Int) {
    val progress by
        remember(numberOfTasksCompleted) {
            mutableFloatStateOf(numberOfTasksCompleted.toFloat() / numberOfTasks.toFloat())
        }

    var shape = MaterialTheme.shapes.extraLarge

    if (numberOfTasksCompleted < numberOfTasks) {
        shape =
            MaterialTheme.shapes.extraLarge.copy(
                topEnd = CornerSize(0.dp),
                bottomEnd = CornerSize(0.dp),
            )
    }

    Row(
        modifier =
            Modifier.fillMaxWidth(progress)
                .height(heightProgressBarChecklist)
                .clip(shape)
                .background(MaterialTheme.colorScheme.tertiary)
    ) {}
}

/** The layer to create the segmented visual. */
@Composable
private fun ProgressBarSegmentation(numberOfTasks: Int) {
    Row(modifier = Modifier.height(heightProgressBarChecklist)) {
        for (task in 1..numberOfTasks) {
            Row(
                modifier =
                    Modifier.fillMaxWidth().height(heightProgressBarChecklist).background(Color.Transparent).weight(1f)
            ) {}

            if (task != numberOfTasks) {
                Row(
                    modifier =
                        Modifier.height(heightProgressBarChecklist)
                            .width(4.dp)
                            .background(MaterialTheme.colorScheme.surface)
                ) {}
            }
        }
    }
}

@FlexibleWindowLightDarkPreview
@Composable
private fun PreviewProgressIndicatorSetupChecklist() {
    FirefoxTheme {
        Surface {
            Box(modifier = Modifier.padding(16.dp)) {
                ProgressBarSetupChecklistView(
                    numberOfTasks = 6,
                    numberOfTasksCompleted = 3,
                )
            }
        }
    }
}
