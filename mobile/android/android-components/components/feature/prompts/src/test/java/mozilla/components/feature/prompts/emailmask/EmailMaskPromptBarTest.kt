/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.prompts.emailmask

import android.content.Context
import android.view.View
import android.view.ViewGroup
import android.view.WindowInsets
import androidx.activity.ComponentActivity
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.test.junit4.v2.createAndroidComposeRule
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class EmailMaskPromptBarTest {

    @get:Rule val composeTestRule = createAndroidComposeRule<ComponentActivity>()

    private lateinit var barView: InsetsReadingView

    @Before
    fun setup() {
        composeTestRule.runOnUiThread {
            barView = InsetsReadingView(composeTestRule.activity)
            windowRootView().addView(barView)
        }
        composeTestRule.setContent {
            CompositionLocalProvider(LocalView provides barView) {
                EmailMaskPromptBar(shouldShowCfr = false, cfrText = "", onCfrDismiss = {}, onMaskEmailClicked = {})
            }
        }
    }

    @Test
    fun `WHEN the window lays out its views THEN the bar checks whether the keyboard is shown`() {
        val readsBeforeLayout = barView.rootWindowInsetsReads

        layOutWindow()

        assertTrue(barView.rootWindowInsetsReads > readsBeforeLayout)
    }

    @Test
    fun `WHEN the bar's view leaves the window before the bar is removed THEN later layouts no longer reach the bar`() {
        composeTestRule.runOnUiThread {
            windowRootView().removeView(barView)
            removeBar()
        }
        val readsAfterRemoval = barView.rootWindowInsetsReads

        layOutWindow()

        assertEquals(readsAfterRemoval, barView.rootWindowInsetsReads)
    }

    @Test
    fun `WHEN the bar is removed while its view is still in the window THEN later layouts no longer reach the bar`() {
        composeTestRule.runOnUiThread { removeBar() }
        val readsAfterRemoval = barView.rootWindowInsetsReads

        layOutWindow()

        assertEquals(readsAfterRemoval, barView.rootWindowInsetsReads)
    }

    private fun windowRootView() = composeTestRule.activity.window.decorView as ViewGroup

    private fun removeBar() {
        composeTestRule.activity.setContentView(View(composeTestRule.activity))
    }

    private fun layOutWindow() {
        composeTestRule.runOnUiThread { windowRootView().viewTreeObserver.dispatchOnGlobalLayout() }
    }

    private class InsetsReadingView(context: Context) : View(context) {
        var rootWindowInsetsReads = 0
            private set

        override fun getRootWindowInsets(): WindowInsets? {
            rootWindowInsetsReads++
            return super.getRootWindowInsets()
        }
    }
}
