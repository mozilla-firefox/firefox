/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.reviewprompt

import android.app.Activity
import androidx.test.ext.junit.runners.AndroidJUnit4
import io.mockk.coVerify
import io.mockk.mockk
import java.lang.ref.WeakReference
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.support.test.middleware.CaptureActionsMiddleware
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.PlayStoreReviewPromptController
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.AppAction.ReviewPromptAction.ReviewPromptShown
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.reviewprompt.ReviewPromptState.NotEligible

@RunWith(AndroidJUnit4::class)
class ShowPlayStoreReviewPromptTest {

    private val testDispatcher = StandardTestDispatcher()
    private var customPromptShown = false
    lateinit var promptController: PlayStoreReviewPromptController
    lateinit var mockActivity: Activity
    lateinit var activityRef: WeakReference<Activity>

    @Before
    fun setup() {
        promptController = mockk(relaxed = true)
        mockActivity = mockk(relaxed = true)
        activityRef = WeakReference(mockActivity)
        customPromptShown = false
    }

    @Test
    fun `GIVEN observing review prompt state WHEN eligible for custom prompt THEN custom prompt shown`() =
        runTest(testDispatcher) {
            val appStore =
                AppStore(
                    initialState =
                        AppState(reviewPrompt = ReviewPromptState.Eligible(ReviewPromptState.Eligible.Type.Custom))
                )
            val feature =
                ShowReviewPromptBinding(
                    appStore,
                    promptController,
                    activityRef,
                    uiScope = this,
                    { customPromptShown = true },
                    mainDispatcher = testDispatcher,
                )

            feature.start()
            testDispatcher.scheduler.advanceUntilIdle()

            coVerify(exactly = 0) {
                promptController.tryPromptReview(mockActivity)
            }

            // We show the custom prompt..
            assertTrue(customPromptShown)
            // ..then we cleared out the eligible state.
            assertEquals(NotEligible, appStore.state.reviewPrompt)
        }

    @Test
    fun `GIVEN observing review prompt state WHEN state is unknown THEN does nothing`() =
        runTest(testDispatcher) {
            val captureMiddleware = CaptureActionsMiddleware<AppState, AppAction>()
            val appStore =
                AppStore(
                    initialState = AppState(reviewPrompt = ReviewPromptState.Unknown),
                    middlewares = listOf(captureMiddleware),
                )
            val feature =
                ShowReviewPromptBinding(
                    appStore,
                    promptController,
                    activityRef,
                    uiScope = this,
                    { customPromptShown = true },
                    mainDispatcher = testDispatcher,
                )

            feature.start()
            testDispatcher.scheduler.advanceUntilIdle()

            coVerify(exactly = 0) {
                promptController.tryPromptReview(mockActivity)
            }

            captureMiddleware.assertNotDispatched(ReviewPromptShown::class)
        }

    @Test
    fun `GIVEN observing review prompt state WHEN not eligible THEN does nothing`() =
        runTest(testDispatcher) {
            val captureMiddleware = CaptureActionsMiddleware<AppState, AppAction>()
            val appStore =
                AppStore(
                    initialState = AppState(reviewPrompt = NotEligible),
                    middlewares = listOf(captureMiddleware),
                )
            val feature =
                ShowReviewPromptBinding(
                    appStore,
                    promptController,
                    activityRef,
                    uiScope = this,
                    { customPromptShown = true },
                    mainDispatcher = testDispatcher,
                )

            feature.start()
            testDispatcher.scheduler.advanceUntilIdle()

            coVerify(exactly = 0) {
                promptController.tryPromptReview(mockActivity)
            }
            captureMiddleware.assertNotDispatched(ReviewPromptShown::class)
        }

    @Test
    fun `GIVEN observing review prompt state WHEN eligible for play store prompt THEN show it`() =
        runTest(testDispatcher) {
            val captureMiddleware = CaptureActionsMiddleware<AppState, AppAction>()
            val appStore =
                AppStore(
                    initialState =
                        AppState(reviewPrompt = ReviewPromptState.Eligible(ReviewPromptState.Eligible.Type.PlayStore)),
                    middlewares = listOf(captureMiddleware),
                )
            val feature =
                ShowReviewPromptBinding(
                    appStore,
                    promptController,
                    activityRef,
                    uiScope = this,
                    { customPromptShown = true },
                    mainDispatcher = testDispatcher,
                )

            feature.start()
            testDispatcher.scheduler.advanceUntilIdle()

            coVerify(exactly = 1) {
                promptController.tryPromptReview(mockActivity)
            }
            captureMiddleware.assertLastAction(ReviewPromptShown::class)
        }
}
