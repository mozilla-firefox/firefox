/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.ai

import mozilla.components.concept.ai.controls.AIFeatureMetadata
import mozilla.components.feature.summarize.PageSummaryFeature
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import org.mozilla.fenix.R

class DestinationTest {

    @Test
    fun `page summary feature maps to pageSummariesSettingsFragment destination`() {
        val destination = PageSummaryFeature.destination

        assertEquals(R.id.pageSummariesSettingsFragment, (destination as Destination).id)
    }

    @Test
    fun `page summary feature destination has correct label`() {
        val destination = PageSummaryFeature.destination

        assertEquals(R.string.ai_controls_more_page_summary_settings, (destination as Destination).label)
    }

    @Test
    fun `unknown feature id returns null destination`() {
        val metadata = object : AIFeatureMetadata {
            override val id = AIFeatureMetadata.FeatureId("unknown.feature")
            override val description = AIFeatureMetadata.Description(0, 0)
        }

        assertNull(metadata.destination)
    }
}
