/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.ui.animation

import android.content.Context
import android.provider.Settings
import androidx.compose.foundation.layout.size
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.assert
import androidx.compose.ui.test.assertHasClickAction
import androidx.compose.ui.test.assertHasNoClickAction
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.compose.ui.unit.dp
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.airbnb.lottie.LottieCompositionFactory
import kotlin.test.assertNotNull
import kotlin.time.Duration.Companion.milliseconds
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

private const val MINIMAL_LOTTIE_JSON =
    """{"v":"5.5.7","fr":60,"ip":0,"op":60,"w":100,"h":100,"nm":"test","ddd":0,"assets":[],"layers":[]}"""

@RunWith(AndroidJUnit4::class)
class AnimatedIllustrationTest {

    @get:Rule val composeTestRule = createComposeRule()

    private val context: Context = ApplicationProvider.getApplicationContext()

    @Test
    fun `GIVEN the animator duration scale is zero WHEN checking reduced motion THEN it returns true`() {
        Settings.Global.putFloat(context.contentResolver, Settings.Global.ANIMATOR_DURATION_SCALE, 0.0f)

        assertTrue(isReducedMotionEnabled(context))
    }

    @Test
    fun `GIVEN the animator duration scale is non-zero WHEN checking reduced motion THEN it returns false`() {
        Settings.Global.putFloat(context.contentResolver, Settings.Global.ANIMATOR_DURATION_SCALE, 1.0f)

        assertFalse(isReducedMotionEnabled(context))
    }

    @Test
    fun `GIVEN reduced motion is enabled WHEN the illustration is shown THEN the static drawable is displayed`() {
        Settings.Global.putFloat(context.contentResolver, Settings.Global.ANIMATOR_DURATION_SCALE, 0.0f)

        composeTestRule.setContent {
            AnimatedIllustration(
                animationResource = 0,
                staticDrawableResource = android.R.drawable.ic_menu_help,
                contentDescription = "test illustration",
            )
        }

        composeTestRule.onNodeWithTag(AnimatedIllustrationTestTag.STATIC).assertIsDisplayed()
        composeTestRule.onNodeWithTag(AnimatedIllustrationTestTag.ANIMATION).assertDoesNotExist()
    }

    @Test
    fun `GIVEN a loaded composition WHEN the illustration is shown THEN the animation is displayed`() {
        val composition = LottieCompositionFactory.fromJsonStringSync(MINIMAL_LOTTIE_JSON, null).value
        assertNotNull(composition)

        composeTestRule.setContent {
            AnimatedIllustration(
                composition = composition,
                staticDrawableResource = android.R.drawable.ic_menu_help,
                contentDescription = "test illustration",
                modifier = Modifier.size(100.dp),
            )
        }

        composeTestRule.onNodeWithTag(AnimatedIllustrationTestTag.ANIMATION).assertIsDisplayed()
        composeTestRule.onNodeWithTag(AnimatedIllustrationTestTag.STATIC).assertDoesNotExist()
    }

    @Test
    fun `GIVEN an iteration delay WHEN the illustration is shown THEN the animation is displayed`() {
        val composition = LottieCompositionFactory.fromJsonStringSync(MINIMAL_LOTTIE_JSON, null).value
        assertNotNull(composition)

        composeTestRule.setContent {
            AnimatedIllustration(
                composition = composition,
                staticDrawableResource = android.R.drawable.ic_menu_help,
                contentDescription = "test illustration",
                iterations = 2,
                iterationDelay = 10.milliseconds,
                modifier = Modifier.size(100.dp),
            )
        }

        composeTestRule.onNodeWithTag(AnimatedIllustrationTestTag.ANIMATION).assertIsDisplayed()
        composeTestRule.onNodeWithTag(AnimatedIllustrationTestTag.STATIC).assertDoesNotExist()
    }

    @Test
    fun `GIVEN replay on click WHEN the animation is clicked THEN it stays displayed`() {
        val composition = LottieCompositionFactory.fromJsonStringSync(MINIMAL_LOTTIE_JSON, null).value
        assertNotNull(composition)

        composeTestRule.setContent {
            AnimatedIllustration(
                composition = composition,
                staticDrawableResource = android.R.drawable.ic_menu_help,
                iterations = 1,
                replayOnClick = true,
                modifier = Modifier.size(100.dp),
            )
        }

        val animation = composeTestRule.onNodeWithTag(AnimatedIllustrationTestTag.ANIMATION)
        animation.assertHasClickAction()
        animation.assert(SemanticsMatcher.expectValue(SemanticsProperties.Role, Role.Button))
        animation.performClick()
        animation.assertIsDisplayed()
    }

    @Test
    fun `GIVEN replay on click is disabled WHEN the animation is shown THEN it has no click action`() {
        val composition = LottieCompositionFactory.fromJsonStringSync(MINIMAL_LOTTIE_JSON, null).value
        assertNotNull(composition)

        composeTestRule.setContent {
            AnimatedIllustration(
                composition = composition,
                staticDrawableResource = android.R.drawable.ic_menu_help,
                modifier = Modifier.size(100.dp),
            )
        }

        composeTestRule.onNodeWithTag(AnimatedIllustrationTestTag.ANIMATION).assertHasNoClickAction()
    }
}
