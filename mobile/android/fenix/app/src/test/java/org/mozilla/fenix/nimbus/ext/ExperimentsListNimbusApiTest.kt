/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.nimbus.ext

import io.mockk.every
import io.mockk.mockk
import mozilla.components.service.nimbus.NimbusApi
import org.junit.Assert.assertEquals
import org.junit.Test
import org.mozilla.experiments.nimbus.AvailableExperiment
import org.mozilla.experiments.nimbus.EnrolledExperiment
import org.mozilla.fenix.R
import org.mozilla.fenix.nimbus.view.NimbusExperimentItem

class ExperimentsListNimbusApiTest {

    @Test
    fun `WHEN no experiments available THEN returns headers with empty states`() {
        val nimbusApi = mockk<NimbusApi>()
        every { nimbusApi.getAvailableExperiments() } returns emptyList()
        every { nimbusApi.getActiveExperiments() } returns emptyList()

        val result = nimbusApi.partitionedExperimentLists()

        assertEquals(4, result.size)
        assertEquals(NimbusExperimentItem.Header(R.string.preferences_nimbus_experiments_active), result[0])
        assertEquals(NimbusExperimentItem.EmptyState(R.string.preferences_nimbus_experiments_no_items), result[1])
        assertEquals(NimbusExperimentItem.Header(R.string.preferences_nimbus_experiments_inactive), result[2])
        assertEquals(NimbusExperimentItem.EmptyState(R.string.preferences_nimbus_experiments_no_items), result[3])
    }

    @Test
    fun `WHEN all experiments are active THEN returns only active experiments with inactive empty state`() {
        val experiment1 = createAvailableExperiment("exp1")
        val experiment2 = createAvailableExperiment("exp2")
        val enrolledExperiment1 = createEnrolledExperiment("exp1")
        val enrolledExperiment2 = createEnrolledExperiment("exp2")

        val nimbusApi = mockk<NimbusApi>()
        every { nimbusApi.getAvailableExperiments() } returns listOf(experiment1, experiment2)
        every { nimbusApi.getActiveExperiments() } returns listOf(enrolledExperiment1, enrolledExperiment2)

        val result = nimbusApi.partitionedExperimentLists()

        assertEquals(5, result.size)
        assertEquals(NimbusExperimentItem.Header(R.string.preferences_nimbus_experiments_active), result[0])
        assertEquals(NimbusExperimentItem.Experiment(experiment1), result[1])
        assertEquals(NimbusExperimentItem.Experiment(experiment2), result[2])
        assertEquals(NimbusExperimentItem.Header(R.string.preferences_nimbus_experiments_inactive), result[3])
        assertEquals(NimbusExperimentItem.EmptyState(R.string.preferences_nimbus_experiments_no_items), result[4])
    }

    @Test
    fun `WHEN all experiments are inactive THEN returns only inactive experiments`() {
        val experiment1 = createAvailableExperiment("exp1")
        val experiment2 = createAvailableExperiment("exp2")

        val nimbusApi = mockk<NimbusApi>()
        every { nimbusApi.getAvailableExperiments() } returns listOf(experiment1, experiment2)
        every { nimbusApi.getActiveExperiments() } returns emptyList()

        val result = nimbusApi.partitionedExperimentLists()

        assertEquals(5, result.size)
        assertEquals(NimbusExperimentItem.Header(R.string.preferences_nimbus_experiments_active), result[0])
        assertEquals(NimbusExperimentItem.EmptyState(R.string.preferences_nimbus_experiments_no_items), result[1])
        assertEquals(NimbusExperimentItem.Header(R.string.preferences_nimbus_experiments_inactive), result[2])
        assertEquals(NimbusExperimentItem.Experiment(experiment1), result[3])
        assertEquals(NimbusExperimentItem.Experiment(experiment2), result[4])
    }

    @Test
    fun `WHEN experiments are mixed active and inactive THEN returns both sections with experiments`() {
        val activeExperiment1 = createAvailableExperiment("active1")
        val activeExperiment2 = createAvailableExperiment("active2")
        val inactiveExperiment1 = createAvailableExperiment("inactive1")
        val inactiveExperiment2 = createAvailableExperiment("inactive2")
        val enrolledExperiment1 = createEnrolledExperiment("active1")
        val enrolledExperiment2 = createEnrolledExperiment("active2")

        val nimbusApi = mockk<NimbusApi>()
        every { nimbusApi.getAvailableExperiments() } returns listOf(
            activeExperiment1,
            inactiveExperiment1,
            activeExperiment2,
            inactiveExperiment2,
        )
        every { nimbusApi.getActiveExperiments() } returns listOf(enrolledExperiment1, enrolledExperiment2)

        val result = nimbusApi.partitionedExperimentLists()

        assertEquals(6, result.size)
        assertEquals(NimbusExperimentItem.Header(R.string.preferences_nimbus_experiments_active), result[0])
        assertEquals(NimbusExperimentItem.Experiment(activeExperiment1), result[1])
        assertEquals(NimbusExperimentItem.Experiment(activeExperiment2), result[2])
        assertEquals(NimbusExperimentItem.Header(R.string.preferences_nimbus_experiments_inactive), result[3])
        assertEquals(NimbusExperimentItem.Experiment(inactiveExperiment1), result[4])
        assertEquals(NimbusExperimentItem.Experiment(inactiveExperiment2), result[5])
    }

    @Test
    fun `WHEN single inactive experiment THEN returns correct structure`() {
        val experiment = createAvailableExperiment("exp1")

        val nimbusApi = mockk<NimbusApi>()
        every { nimbusApi.getAvailableExperiments() } returns listOf(experiment)
        every { nimbusApi.getActiveExperiments() } returns emptyList()

        val result = nimbusApi.partitionedExperimentLists()

        assertEquals(4, result.size)
        assertEquals(NimbusExperimentItem.Header(R.string.preferences_nimbus_experiments_active), result[0])
        assertEquals(NimbusExperimentItem.EmptyState(R.string.preferences_nimbus_experiments_no_items), result[1])
        assertEquals(NimbusExperimentItem.Header(R.string.preferences_nimbus_experiments_inactive), result[2])
        assertEquals(NimbusExperimentItem.Experiment(experiment), result[3])
    }

    private fun createAvailableExperiment(slug: String) = AvailableExperiment(
        slug = slug,
        userFacingName = "Experiment $slug",
        userFacingDescription = "Description for $slug",
        branches = emptyList(),
        referenceBranch = null,
    )

    private fun createEnrolledExperiment(slug: String) = EnrolledExperiment(
        featureIds = emptyList(),
        slug = slug,
        userFacingName = "Enrolled $slug",
        userFacingDescription = "Enrolled description for $slug",
        branchSlug = "control",
    )
}
