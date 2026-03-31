/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.ai.controls

import mozilla.components.concept.ai.controls.AIControllableFeature
import mozilla.components.concept.ai.controls.AIFeatureMetadata
import mozilla.components.concept.ai.controls.AIFeatureRegistry
import org.junit.Test

class AIFeatureRegistryTest {
    @Test(expected = IllegalStateException::class)
    fun `registry cannot re-register features`() {
        val registry = AIFeatureRegistry.default()
        val featureA = AIControllableFeature.inMemory(
            id = AIFeatureMetadata.FeatureId("a"),
            initialEnabled = true,
        )

        registry.register(featureA)
        registry.register(featureA)
    }
}
