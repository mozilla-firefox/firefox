/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.nimbus.ext

import kotlinx.coroutines.Dispatchers.IO
import kotlinx.coroutines.withContext
import mozilla.components.service.nimbus.NimbusApi
import org.mozilla.fenix.R
import org.mozilla.fenix.nimbus.view.NimbusExperimentItem
import org.mozilla.fenix.nimbus.view.NimbusExperimentItem.EmptyState
import org.mozilla.fenix.nimbus.view.NimbusExperimentItem.Experiment
import org.mozilla.fenix.nimbus.view.NimbusExperimentItem.Header

/**
 * Separates the experiment list into an "active" and "inactive" items based on enrollment for
 * rendering with [org.mozilla.fenix.nimbus.view.NimbusExperiments].
 */
internal fun NimbusApi.partitionedExperimentLists(): List<NimbusExperimentItem> {
    val availableExperiments = getAvailableExperiments()
    val activeExperimentSlugs = getActiveExperiments().map { it.slug }.toSet()

    val (active, inactive) = availableExperiments.partition { it.slug in activeExperimentSlugs }

    return buildList {
        add(Header(R.string.preferences_nimbus_experiments_active))

        if (active.isEmpty()) {
            add(EmptyState(R.string.preferences_nimbus_experiments_no_items))
        } else {
            addAll(active.map { Experiment(it) })
        }

        add(Header(R.string.preferences_nimbus_experiments_inactive))

        if (inactive.isEmpty()) {
            add(EmptyState(R.string.preferences_nimbus_experiments_no_items))
        } else {
            addAll(inactive.map { Experiment(it) })
        }
    }
}

/**
 * Separates the experiment list into an "active" and "inactive" items based on enrollment for
 * rendering with [org.mozilla.fenix.nimbus.view.NimbusExperiments] using
 * the [kotlinx.coroutines.Dispatchers.IO] dispatcher.
 */
suspend fun NimbusApi.fetchPartitionedExperimentListsAsync(): List<NimbusExperimentItem> =
    withContext(IO) { partitionedExperimentLists() }
