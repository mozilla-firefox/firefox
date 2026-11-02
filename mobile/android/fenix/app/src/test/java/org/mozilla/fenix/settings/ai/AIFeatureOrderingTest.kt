/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.ai

import mozilla.components.concept.ai.controls.AIControllableFeature
import mozilla.components.concept.ai.controls.AIFeatureMetadata
import mozilla.components.feature.summarize.PageSummaryFeature
import org.junit.Assert.assertEquals
import org.junit.Test
import org.mozilla.fenix.listentopage.ListenToPageAIControlFeature
import org.mozilla.fenix.search.VoiceSearchAIControlFeature
import org.mozilla.fenix.translations.TranslationsAIControllableFeature

class AIFeatureOrderingTest {

    @Test
    fun `sortedForDisplay places known features in canonical order regardless of input order`() {
        val features =
            listOf(
                AIControllableFeature.inMemory(id = VoiceSearchAIControlFeature.id),
                AIControllableFeature.inMemory(id = ListenToPageAIControlFeature.id),
                AIControllableFeature.inMemory(id = PageSummaryFeature.id),
                AIControllableFeature.inMemory(id = TranslationsAIControllableFeature.id),
            )

        assertEquals(
            listOf(
                TranslationsAIControllableFeature.id,
                PageSummaryFeature.id,
                VoiceSearchAIControlFeature.id,
                ListenToPageAIControlFeature.id,
            ),
            features.sortedForDisplay().map { it.id },
        )
    }

    @Test
    fun `sortedForDisplay sends unknown features to the end and keeps known features ordered`() {
        val unknownA = AIControllableFeature.inMemory(id = AIFeatureMetadata.FeatureId("unknown.a"))
        val unknownB = AIControllableFeature.inMemory(id = AIFeatureMetadata.FeatureId("unknown.b"))
        val translations = AIControllableFeature.inMemory(id = TranslationsAIControllableFeature.id)

        val sorted = listOf(unknownA, translations, unknownB).sortedForDisplay().map { it.id }

        assertEquals(
            listOf(
                TranslationsAIControllableFeature.id,
                AIFeatureMetadata.FeatureId("unknown.a"),
                AIFeatureMetadata.FeatureId("unknown.b"),
            ),
            sorted,
        )
    }

    @Test
    fun `sortedForDisplay preserves input order when every feature is unknown`() {
        val first = AIControllableFeature.inMemory(id = AIFeatureMetadata.FeatureId("first"))
        val second = AIControllableFeature.inMemory(id = AIFeatureMetadata.FeatureId("second"))
        val third = AIControllableFeature.inMemory(id = AIFeatureMetadata.FeatureId("third"))

        assertEquals(
            listOf(first.id, second.id, third.id),
            listOf(first, second, third).sortedForDisplay().map { it.id },
        )
    }
}
